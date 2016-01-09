/*
* UAE - The Un*x Amiga Emulator
*
* A2410
*
*/
#include "sysconfig.h"
#include "sysdeps.h"

#include "debug.h"
#include "mameglue.h"
#include "tm34010/tms34010.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "picasso96.h"
#include "statusline.h"
#include "newcpu.h"
#include "gfxboard.h"

static int tms_vp, tms_hp;
rectangle tms_rectangle;
static mscreen tms_screen;
static tms340x0_device tms_device;
static address_space tms_space;
mscreen *m_screen;

#define OVERLAY_WIDTH 1024

static int fullrefresh;
static int request_fullrefresh;
static uae_u8 *program_ram;
static int a2410_palette_index;
static uae_u8 a2410_palette[4 * (256 + 4)];
static uae_u32 a2410_palette_32[256 + 4];
static uae_u8 a2410_palette_temp[4];
static uae_u8 a2410_palette_control[4];
static uae_u16 a2410_control;
static bool a2410_modified[1024];
static int a2410_displaywidth;
static int a2410_displayend;
static int a2410_vertical_start;
static bool a2410_enabled;
static uae_u8 a2410_overlay_mask[2];
static int a2410_overlay_blink_rate_on;
static int a2410_overlay_blink_rate_off;
static int a2410_overlay_blink_cnt;
static int tms_configured;
static uae_u8 tms_config[128];
extern addrbank tms_bank;

int mscreen::hpos()
{
	if (a2410_displayend) {
		tms_hp++;
		tms_hp %= a2410_displayend;
	} else {
		tms_hp = 0;
	}
	return tms_hp;
}
int mscreen::vpos()
{
	return tms_vp;
}

static void tms_execute_single(void)
{
	tms_device.m_icount = 2;
	tms_device.execute_run();
}

#define A2410_BANK_FRAMEBUFFER 1
#define A2410_BANK_PROGRAM 2
#define A2410_BANK_RAMDAC 3
#define A2410_BANK_CONTROL 4
#define A2410_BANK_TMSIO 5
#define A2410_BANK_DMA 6

static uaecptr makeaddr(UINT32 a, int *bank)
{
	uaecptr addr = 0;
	UINT32 aa = a << 3;
	if ((aa & 0xf0000000) == 0xc0000000) {
		*bank = A2410_BANK_TMSIO;
		addr = (a & 0xff) >> 1;
	} else if ((aa & 0x01900000) == 0x00800000) {
		*bank = A2410_BANK_RAMDAC;
		addr = (a >> 1) & 3;
	} else if ((aa & 0x01900000) == 0x00900000) {
		*bank = A2410_BANK_CONTROL;
	} else if ((aa & 0x30000000) == 0x10000000) {
		addr = a & 0xffffff;
		*bank = A2410_BANK_DMA;
	} else if ((aa & 0x01800000) == 0x01800000) {
		addr = a & 0xfffff;
		*bank = A2410_BANK_PROGRAM;
	} else if ((aa & 0x01800000) == 0x00000000) {
		addr = a & 0xfffff;
		*bank = A2410_BANK_FRAMEBUFFER;
	} else {
		*bank = 0;
		write_log(_T("Unknown BANK %08x PC=%08x\n"), aa, M68K_GETPC);
	}
	return addr;
}

/* CONTROL
 *
 * bit 0 - OSC_SEL - oscillator select
 * bit 1 - _SOG_EN - sync on green
 * bit 2 - SWAP - byte swap enable for DMA
 * bit 3 - SBR - bus request from TMS34010 (Sets bit 7 when bus granted)
 * bit 4 - D_BLANK (read only) - seems to be related to BLANK output of TMS and VSYNC
 * bit 5 - AGBG (read only) - comes straight from Zorro II BCLR signal ?
 * bit 6 - AID = Z3Sense pin when read, HS_INV when write (Horizontal Sync Invert)
 * bit 7 - LGBACK when read, VS_INV when write (Vertical Sync Invert) - LGBACK: A2410 owns the Zorro bus now.
 */

static uae_u8 get_a2410_control(void)
{
	uae_u8 v = a2410_control;
	v &= ~(0x10 | 0x40 | 0x80);
	v |= 0x20;
	if (v & 0x08) // SBR
		v |= 0x80; // LGBACK
	if (currprefs.cs_compatible == CP_A3000 || currprefs.cs_compatible == CP_A3000T ||
		currprefs.cs_compatible == CP_A4000 || currprefs.cs_compatible == CP_A4000T ||
		currprefs.cs_z3autoconfig)
		v |= 0x40; // AID
	return v;
}

UINT32 total_cycles(void)
{
	return get_cycles() / CYCLE_UNIT;
}

void m_to_shiftreg_cb(address_space space, offs_t offset, UINT16 *shiftreg)
{
	memcpy(shiftreg, &gfxmem_bank.baseaddr[TOWORD(offset)], 256 * sizeof(UINT16));
}
void m_from_shiftreg_cb(address_space space, offs_t offset, UINT16* shiftreg)
{
	memcpy(&gfxmem_bank.baseaddr[TOWORD(offset)], shiftreg, 256 * sizeof(UINT16));
}

UINT16 direct_read_data::read_decrypted_word(UINT32 pc)
{
	uae_u16 v = 0;
	int bank;
	uaecptr addr = makeaddr(pc, &bank);
	v = program_ram[addr] << 8;
	v |= program_ram[addr + 1];
	//write_log(_T("TMS instruction word read RAM %08x (%08x) =%04x\n"), pc, addr, v);
	return v;
}
UINT16 direct_read_data::read_raw_word(UINT32 pc)
{
	uae_u16 v = 0;
	int bank;
	uaecptr addr = makeaddr(pc, &bank);
	v = program_ram[addr] << 8;
	v |= program_ram[addr + 1];
	//write_log(_T("TMS instruction word read RAM %08x (%08x) =%04x\n"), pc, addr, v);
	return v;
}

static void mark_overlay(int addr)
{
	if (!a2410_enabled)
		return;
	addr &= 0x1ffff;
	addr /= OVERLAY_WIDTH / 8;
	a2410_modified[addr] = true;
}

static void a2410_create_palette32(int offset)
{
	int idx = a2410_palette_index / 4 + offset;
	if (a2410_palette[idx * 4 + 0] != a2410_palette_temp[0] ||
		a2410_palette[idx * 4 + 1] != a2410_palette_temp[1] ||
		a2410_palette[idx * 4 + 2] != a2410_palette_temp[2]) {
		request_fullrefresh = 1;
	}
	a2410_palette[idx * 4 + 0] = a2410_palette_temp[0];
	a2410_palette[idx * 4 + 1] = a2410_palette_temp[1];
	a2410_palette[idx * 4 + 2] = a2410_palette_temp[2];
	a2410_palette_32[idx] =
		(a2410_palette_temp[0] << 16) |
		(a2410_palette_temp[1] <<  8) |
		(a2410_palette_temp[2] <<  0);
#if 0
	write_log(_T("PAL %d: %02x %02x %02x = %08x\n"),
		idx,
		a2410_palette_temp[0],
		a2410_palette_temp[1],
		a2410_palette_temp[2],
		a2410_palette_32[idx]);
#endif
}

static void write_ramdac(int addr, uae_u8 v)
{
	int coloridx = a2410_palette_index & 3;
	switch (addr)
	{
		case 0:
		a2410_palette_index = v * 4;
		break;
		case 1:
		a2410_palette_temp[coloridx] = v;
		a2410_palette_index++;
		if ((a2410_palette_index & 3) == 3) {
			a2410_create_palette32(0);
			a2410_palette_index++;
		}
		if (a2410_palette_index >= 256 * 4)
			a2410_palette_index = 0;
		break;
		case 2:
		if (a2410_palette_index >= 4 * 4 && a2410_palette_index < 8 * 4) {
			a2410_palette_control[a2410_palette_index / 4 - 4] = v;
		}
		a2410_overlay_mask[0] = 0xff;
		a2410_overlay_mask[1] = 0xff;
		if (!(a2410_palette_control[6 - 4] & 1))
			a2410_overlay_mask[0] = 0;
		if (!(a2410_palette_control[6 - 4] & 2))
			a2410_overlay_mask[1] = 0;
		switch((a2410_palette_control[6 - 4] >> 4) & 3)
		{
			case 0:
			a2410_overlay_blink_rate_on = 16;
			a2410_overlay_blink_rate_off = 48;
			break;
			case 1:
			a2410_overlay_blink_rate_on = 16;
			a2410_overlay_blink_rate_off = 16;
			break;
			case 2:
			a2410_overlay_blink_rate_on = 32;
			a2410_overlay_blink_rate_off = 32;
			break;
			case 3:
			a2410_overlay_blink_rate_on = 64;
			a2410_overlay_blink_rate_off = 64;
			break;
		}
		break;
		case 3:
		if (a2410_palette_index < 4 * 4) {
			a2410_palette_temp[coloridx] = v;
			a2410_palette_index++;
			if ((a2410_palette_index & 3) == 3) {
				a2410_create_palette32(256);
				a2410_palette_index++;
			}
			if (a2410_palette_index >= 4 * 4)
				a2410_palette_index = 0;
		}
		break;
		default:
		write_log(_T("Unknown write RAMDAC address %08x PC=%08x\n"), addr, M68K_GETPC);
		break;
	}
}
static uae_u8 read_ramdac(int addr)
{
	uae_u8 v = 0;
	switch (addr)
	{
		case 0:
		v = a2410_palette_index / 4;
		break;
		case 1:
		v = a2410_palette[a2410_palette_index];
		a2410_palette_index++;
		if ((a2410_palette_index & 3) == 3)
			a2410_palette_index++;
		if (a2410_palette_index >= 256 * 4)
			a2410_palette_index = 0;
		break;
		case 2:
		if (a2410_palette_index >= 4 * 4 && a2410_palette_index < 8 * 4) {
			v = a2410_palette_control[a2410_palette_index / 4 - 4];
		}
		break;
		case 3:
		if (a2410_palette_index < 4 * 4) {
			v = a2410_palette[a2410_palette_index + 256 * 4];
			a2410_palette_index++;
			if ((a2410_palette_index & 3) == 3)
				a2410_palette_index = 0;
			if (a2410_palette_index >= 4 * 4)
				a2410_palette_index = 0;
		}
		break;
		default:
		write_log(_T("Unknown read RAMDAC address %08x PC=%08x\n"), addr, M68K_GETPC);
		break;
	}
	return v;
}

static bool valid_dma(uaecptr addr)
{
	// prevent recursive DMA
	return addr < (tms_configured << 16) || addr >= ((tms_configured + 1) << 16);
}

UINT8 address_space::read_byte(UINT32 a)
{
	int bank;
	uae_u8 v = 0;
	UINT32 aa = a << 3;
	uaecptr addr = makeaddr(a, &bank);

	addr ^= 1;
	switch (bank)
	{
		case A2410_BANK_PROGRAM:
		v = program_ram[addr];
		//write_log(_T("TMS byte read RAM %08x (%08x) =%02x PC=%08x\n"), aa, addr, v, M68K_GETPC);
		break;
		case A2410_BANK_FRAMEBUFFER:
		v = gfxmem_bank.baseaddr[addr];
		//write_log(_T("TMS byte read framebuffer %08x (%08x) = %02x PC=%08x\n"), aa, addr, v, M68K_GETPC);
		break;
		case A2410_BANK_RAMDAC:
		v = read_ramdac(addr);
		//write_log(_T("RAMDAC READ %08x = %02x PC=%08x\n"), aa, v, M68K_GETPC);
		break;
		case A2410_BANK_CONTROL:
		v = get_a2410_control();
		write_log(_T("CONTROL READ %08x = %02x PC=%08x\n"), aa, v, M68K_GETPC);
		break;
		case A2410_BANK_DMA:
		if (valid_dma(addr)) {
			if (a2410_control & 4)
				addr ^= 1;
			v = get_byte(addr);
		}
		break;
		default:
		write_log(_T("UNKNOWN READ %08x = %02x PC=%08x\n"), aa, v, M68K_GETPC);
		break;

	}
	return v;
}
UINT16 address_space::read_word(UINT32 a)
{
	int bank;
	uae_u16 v = 0;
	UINT32 aa = a << 3;
	uaecptr addr = makeaddr(a, &bank);

	switch (bank)
	{
		case A2410_BANK_TMSIO:
		v = tms_device.io_register_r(*this, addr);
		//write_log(_T("TMS IO word read %08x (%08x) = %04x PC=%08x PC=%08x\n"), aa, addr, v, M68K_GETPC);
		break;
		case A2410_BANK_PROGRAM:
		v = program_ram[addr] << 8;
		v |= program_ram[addr + 1];
		//write_log(_T("TMS program word read RAM %08x (%08x) = %04x PC=%08x\n"), aa, addr, v, M68K_GETPC);
		break;
		case A2410_BANK_FRAMEBUFFER:
		v = gfxmem_bank.baseaddr[addr] << 8;
		v |= gfxmem_bank.baseaddr[addr + 1];
		//write_log(_T("TMS gfx word read %08x (%08x) = %04x PC=%08x\n"), aa, addr, v, M68K_GETPC);
		break;
		case A2410_BANK_RAMDAC:
		v = read_ramdac(addr);
		//write_log(_T("RAMDAC READ %08x = %02x PC=%08x\n"), aa, v, M68K_GETPC);
		break;
		case A2410_BANK_CONTROL:
		v = get_a2410_control();
		write_log(_T("CONTROL READ %08x = %02x PC=%08x\n"), aa, v, M68K_GETPC);
		break;
		case A2410_BANK_DMA:
		if (valid_dma(addr)) {
			v = get_word(addr);
			if (a2410_control & 4)
				v = (v >> 8) | (v << 8);
		}
		break;
		default:
		write_log(_T("UNKNOWN READ %08x = %04x PC=%08x\n"), aa, v, M68K_GETPC);
		break;
	}
	return v;
}

void address_space::write_byte(UINT32 a, UINT8 b)
{
	int bank;
	UINT32 aa = a << 3;
	uaecptr addr = makeaddr(a, &bank);
	addr ^= 1;
	switch (bank)
	{
		case A2410_BANK_PROGRAM:
		program_ram[addr] = b;
		if (addr < 0x40000)
			mark_overlay(addr);
		//write_log(_T("TMS program byte write %08x (%08x) = %02x PC=%08x\n"), aa, addr, b, M68K_GETPC);
		break;
		case A2410_BANK_FRAMEBUFFER:
		gfxmem_bank.baseaddr[addr] = b;
		//write_log(_T("TMS gfx byte write %08x (%08x) = %02x PC=%08x\n"), aa, addr, b, M68K_GETPC);
		break;
		case A2410_BANK_RAMDAC:
		//write_log(_T("RAMDAC WRITE %08x = %02x PC=%08x\n"), aa, b, M68K_GETPC);
		write_ramdac(addr, b);
		break;
		case A2410_BANK_CONTROL:
		write_log(_T("CONTROL WRITE %08x = %02x PC=%08x\n"), aa, b, M68K_GETPC);
		a2410_control = b;
		break;
		case A2410_BANK_DMA:
		if (valid_dma(addr)) {
			if (a2410_control & 4)
				addr ^= 1;
			put_byte(addr, b);
		}
		break;
		default:
		write_log(_T("UNKNOWN WRITE %08x = %02x PC=%08x\n"), aa, b, M68K_GETPC);
		break;
	}
}

void address_space::write_word(UINT32 a, UINT16 b)
{
	int bank;
	UINT32 aa = a << 3;
	uaecptr addr = makeaddr(a, &bank);
	switch (bank)
	{
		case A2410_BANK_TMSIO:
		tms_device.io_register_w(*this, addr, b);
		//write_log(_T("TMS IO word write %08x (%08x) = %04x PC=%08x\n"), aa, addr, b, M68K_GETPC);
		break;
		case A2410_BANK_PROGRAM:
		program_ram[addr] = b >> 8;
		program_ram[addr + 1] = b & 0xff;
		if (addr < 0x40000)
			mark_overlay(addr);
		//write_log(_T("TMS program word write RAM %08x (%08x) = %04x PC=%08x\n"), aa, addr, b, M68K_GETPC);
		break;
		case A2410_BANK_FRAMEBUFFER:
		gfxmem_bank.baseaddr[addr] = b >> 8;
		gfxmem_bank.baseaddr[addr + 1] = b & 0xff;
		//write_log(_T("TMS gfx word write %08x (%08x) = %04x PC=%08x\n"), aa, addr, b, M68K_GETPC);
		break;
		case A2410_BANK_RAMDAC:
		//write_log(_T("RAMDAC WRITE %08x = %04x IDX=%d/%d PC=%08x\n"), aa, b, a2410_palette_index / 4, a2410_palette_index & 3, M68K_GETPC);
		write_ramdac(addr, b);
		break;
		case A2410_BANK_CONTROL:
		write_log(_T("CONTROL WRITE %08x = %04x PC=%08x\n"), aa, b, M68K_GETPC);
		a2410_control = b;
		break;
		case A2410_BANK_DMA:
		if (valid_dma(addr)) {
			if (a2410_control & 4)
				b = (b >> 8) | (b << 8);
			put_word(addr, b);
		}
		break;
		default:
		write_log(_T("UNKNOWN WRITE %08x = %04x PC=%08x\n"), aa, b, M68K_GETPC);
		break;
	}
}

static uae_u32 REGPARAM2 tms_bget(uaecptr addr)
{
	uae_u32 v = 0xff;
	addr &= 65535;
	if (!tms_configured) {
		v = tms_config[addr];
	} else {
		uae_u16 vv = tms_device.host_r(tms_space, addr >> 1);
		if (!(addr & 1))
			vv >>= 8;
		v = (uae_u8)vv;
		//write_log(_T("TMS read %08x = %02x PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
		tms_execute_single();
	}
	return v;
}
static uae_u32 REGPARAM2 tms_wget(uaecptr addr)
{
	uae_u16 v;
	addr &= 65535;
	if (tms_configured) {
		v = tms_device.host_r(tms_space, addr >> 1);
		//write_log(_T("TMS read %08x = %04x PC=%08x\n"), addr, v & 0xffff, M68K_GETPC);
		tms_execute_single();
	} else {
		v = tms_bget(addr) << 8;
		v |= tms_bget(addr + 1);
	}
	return v;
}
static uae_u32 REGPARAM2 tms_lget(uaecptr addr)
{
	uae_u32 v;
	addr &= 65535;
	v = tms_wget(addr) << 16;
	v |= tms_wget(addr + 2);
	return v;
}


static void REGPARAM2 tms_wput(uaecptr addr, uae_u32 w)
{
	addr &= 65535;
	if (tms_configured) {
		//write_log(_T("TMS write %08x = %04x PC=%08x\n"), addr, w & 0xffff, M68K_GETPC);
		tms_device.host_w(tms_space, addr  >> 1, w);
		tms_execute_single();
	}
}

static void REGPARAM2 tms_lput(uaecptr addr, uae_u32 l)
{
	addr &= 65535;
	tms_wput(addr, l >> 16);
	tms_wput(addr + 2, l);
}

static void REGPARAM2 tms_bput(uaecptr addr, uae_u32 b)
{
	b &= 0xff;
	addr &= 65535;
	if (!tms_configured) {
		if (addr == 0x48 && !tms_configured) {
			map_banks_z2(&tms_bank, b, 0x10000 >> 16);
			tms_configured = b;
			expamem_next(&tms_bank, NULL);
			return;
		}
		if (addr == 0x4c && !tms_configured) {
			tms_configured = 0xff;
			expamem_shutup(&tms_bank);
			return;
		}
		return;
	}
	//write_log(_T("tms_bput %08x=%02x PC=%08x\n"), addr, b, M68K_GETPC);
	tms_device.host_w(tms_space, addr >> 1, (b << 8) | b);
	tms_execute_single();
}

addrbank tms_bank = {
	tms_lget, tms_wget, tms_bget,
	tms_lput, tms_wput, tms_bput,
	default_xlate, default_check, NULL, NULL, _T("A2410"),
	tms_lget, tms_wget,
	ABFLAG_IO, S_READ, S_WRITE
};

static bool a2410_modechanged;
static int a2410_gotmode;
static int a2410_width, a2410_height;
static int a2410_vram_start_offset;
static uae_u8 *a2410_surface;
static int a2410_interlace;
static int a2410_interrupt;
static int a2410_hsync_max;

void tms_reset(void)
{
	if (a2410_surface)
		gfx_unlock_picasso(true);
	a2410_surface = NULL;

	a2410_modechanged = false;
	a2410_gotmode = 0;
	a2410_interlace = 0;
	a2410_interrupt = 0;
	a2410_hsync_max = 2;

	if (program_ram)
		tms_device.device_reset();
	tms_configured = 0;
}

static void ew(int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		tms_config[addr] = (value & 0xf0);
		tms_config[addr + 2] = (value & 0x0f) << 4;
	} else {
		tms_config[addr] = ~(value & 0xf0);
		tms_config[addr + 2] = ~((value & 0x0f) << 4);
	}
}

void tms_free(void)
{
	if (a2410_surface)
		gfx_unlock_picasso(true);
	a2410_surface = NULL;
	mapped_free(&gfxmem_bank);
	xfree(program_ram);
	program_ram = NULL;
}

addrbank *tms_init(int devnum)
{
	memset(tms_config, 0xff, sizeof tms_config);
	ew(0x00, 0xc0 | 0x01);
	// product id
	ew(0x04, 0x00);
	// manufacturer id
	ew(0x10, 1030 >> 8);
	ew(0x14, 1030 & 0xff);

	mapped_free(&gfxmem_bank);
	xfree(program_ram);

	gfxmem_bank.label = _T("ram_a8");
	gfxmem_bank.allocated = 1 * 1024 * 1024;
	mapped_malloc(&gfxmem_bank);
	picasso_allocatewritewatch(gfxmem_bank.allocated);
	gfxmem_bank.start = 0xa80000;

	program_ram = xcalloc(uae_u8, 1 * 1024 * 1024);

	m_screen = &tms_screen;
	tms_device.device_start();
	tms_reset();

	return &tms_bank;
}

void mscreen::configure(int width, int height, rectangle vis)
{
	int ow = a2410_width, oh = a2410_height;
	a2410_width = vis.max_x - vis.min_x + 1;
	a2410_height = vis.max_y - vis.min_y + 1;
	a2410_interlace = vis.interlace ? 1 : 0;
	if (a2410_interlace)
		a2410_height *= 2;
	a2410_modechanged = true;
	a2410_gotmode = true;
	m_screen->height_v = height;
	m_screen->width_v = width;
	tms_rectangle = vis;
	write_log(_T("A2410 %d*%d -> %d*%d\n"), ow, oh, a2410_width, a2410_height);
	a2410_hsync_max = a2410_height / 300;
}

static void get_a2410_surface(void)
{
	bool gotsurf = false;
	if (picasso_on) {
		if (a2410_surface == NULL) {
			a2410_surface = gfx_lock_picasso(false, false);
			gotsurf = true;
		}
		if (a2410_surface && gotsurf) {
			if (!(currprefs.leds_on_screen & STATUSLINE_TARGET))
				picasso_statusline(a2410_surface);
		}
	}
}

bool tms_toggle(int mode)
{
	if (!tms_configured)
		return false;

	if (a2410_enabled) {
		a2410_enabled = false;
		a2410_modechanged = false;
		picasso_requested_on = 0;
		a2410_gotmode = -1;
		return true;
	} else {
		if (!a2410_gotmode)
			return false;
		a2410_gotmode = 1;
		a2410_modechanged = true;
		return true;
	}
	return false;
}

static void tms_vsync_handler2(bool internalsync)
{
	if (!tms_configured)
		return;

	tms34010_display_params parms;
	tms_device.get_display_params(&parms);
	bool enabled = parms.enabled != 0 && a2410_gotmode > 0;

	if (enabled != a2410_enabled || a2410_modechanged) {
		if (a2410_surface)
			gfx_unlock_picasso(false);
		a2410_surface = NULL;

		if (!enabled) {
			picasso_requested_on = 0;
		} else {
			if (a2410_modechanged) {
				picasso96_state.Width = a2410_width;
				picasso96_state.Height = a2410_height;
				picasso96_state.BytesPerPixel = 1;
				picasso96_state.RGBFormat = RGBFB_CLUT;
				write_log(_T("A2410 %d*%d\n"), a2410_width, a2410_height);
				gfx_set_picasso_modeinfo(a2410_width, a2410_height, 1, RGBFB_NONE);
				init_hz_p96();
			}
			picasso_requested_on = 1;
			a2410_modechanged = false;
			fullrefresh = 2;
		}
		a2410_enabled = enabled;
		write_log(_T("A2410 ACTIVE=%d\n"), a2410_enabled);
	}

	if (picasso_on) {
		if (currprefs.leds_on_screen & STATUSLINE_RTG) {
			get_a2410_surface();
		}
		if (internalsync) {
			if (fullrefresh > 0)
				fullrefresh--;
			if (request_fullrefresh) {
				fullrefresh = 1;
				request_fullrefresh = 0;
			}
		}
	}

	if (a2410_surface)
		gfx_unlock_picasso(true);
	a2410_surface = NULL;

	a2410_interlace = -a2410_interlace;

	a2410_overlay_blink_cnt++;
	if (a2410_overlay_blink_cnt == 0 || a2410_overlay_blink_cnt == a2410_overlay_blink_rate_on) {
		// any blink mode enabled?
		if (a2410_palette_control[5 - 4] != 0 || (a2410_palette_control[6 - 4] & (4 | 8)))
			fullrefresh++;
	}
	if (a2410_overlay_blink_cnt > a2410_overlay_blink_rate_off + a2410_overlay_blink_rate_on) {
		a2410_overlay_blink_cnt = 0;
	}
}


static void a2410_rethink(void)
{
	if (a2410_interrupt)
		INTREQ_0(0x8000 | 0x0008);
}

bool tms_vsync_handler(void)
{
	bool flushed = false;
	if (!a2410_enabled)
		tms_vsync_handler2(false);

	if (a2410_surface) {
		gfx_unlock_picasso(false);
		flushed = true;
	}
	a2410_surface = NULL;
	return flushed;
}

static void tms_hsync_handler2(void)
{
	if (!tms_configured)
		return;

	tms_device.m_icount = 100;
	tms_device.execute_run();
	int a2410_vpos = tms_vp;
	tms_vp = tms_device.scanline_callback(NULL, tms_vp, a2410_interlace < 0);

	a2410_rethink();

	if (!a2410_enabled)
		return;

	if (a2410_vpos == 0) {
		tms_vsync_handler2(true);
		picasso_getwritewatch(a2410_vram_start_offset);
	}

	if (a2410_modechanged)
		return;

	tms34010_display_params parms;
	tms_device.get_display_params(&parms);

	a2410_displaywidth = parms.hsblnk - parms.heblnk;
	a2410_displayend = parms.heblnk;
	a2410_vertical_start = parms.veblnk;

	int overlay_yoffset = a2410_vpos - a2410_vertical_start;

	int coladdr = parms.coladdr;
	int vramoffset = ((parms.rowaddr << 8) & 0x7ffff);
	uae_u16 *vram = (uae_u16*)gfxmem_bank.baseaddr + vramoffset;

	int overlayoffset = a2410_vpos - parms.veblnk;

	if (overlay_yoffset < 0)
		return;

	if (a2410_interlace) {
		overlay_yoffset *= 2;
		if (a2410_interlace < 0)
			overlay_yoffset++;
	}

	if (overlay_yoffset >= a2410_height || overlay_yoffset >= picasso_vidinfo.height)
		return;


	if (!fullrefresh && !a2410_modified[overlay_yoffset]) {
		if (!picasso_is_vram_dirty(gfxmem_bank.start + (vramoffset << 1), a2410_displaywidth)) {
			if (!picasso_is_vram_dirty(gfxmem_bank.start + ((vramoffset + 0x200) << 1), a2410_displaywidth)) {
				return;
			}
		}
	}

	a2410_modified[overlay_yoffset] = false;

	get_a2410_surface();
	uae_u8 *dst = a2410_surface;
	if (!dst)
		return;
	dst += overlay_yoffset * picasso_vidinfo.rowbytes;
	uae_u32 *dst32 = (uae_u32*)dst;

	uae_u8 *overlay0 = program_ram + overlayoffset * OVERLAY_WIDTH / 8;
	uae_u8 *overlay1 = overlay0 + 0x20000;

	bool overlay0color = !(a2410_palette_control[6 - 4] & 0x40);
	uae_u16 bitmap_mask = a2410_palette_control[4 - 4];

	uae_u8 overlay_mask[2] = { a2410_overlay_mask[0], a2410_overlay_mask[1] };
	if (a2410_overlay_blink_cnt >= a2410_overlay_blink_rate_on) {
		if (a2410_palette_control[6 - 4] & 4)
			overlay_mask[0] = 0;
		if (a2410_palette_control[6 - 4] & 8)
			overlay_mask[1] = 0;
		bitmap_mask &= ~a2410_palette_control[5 - 4];
	}

	int xx = 0;
	int overlay_offset = 0;
	int overlay_bitcount = 0;
	uae_u8 opix0 = 0, opix1 = 0;

	for (int x = parms.heblnk; x < parms.hsblnk && xx < picasso_vidinfo.width; x += 2, xx += 2) {

		if (a2410_vpos >= parms.veblnk && a2410_vpos < parms.vsblnk) {

			if (!overlay_bitcount && overlayoffset >= 0) {
				opix0 = overlay0[overlay_offset ^ 1] & a2410_overlay_mask[0];
				opix1 = overlay1[overlay_offset ^ 1] & a2410_overlay_mask[1];
				overlay_offset++;
			}

			uae_u16 pix = vram[coladdr & 0x1ff];
			coladdr++;

			if (overlay0color || opix0 || opix1) {

				int pal;
				uae_u8 ov;

				pal = (pix >> 8) & bitmap_mask;
				ov = opix0 & 1;
				ov |= (opix1 & 1) << 1;
				if (ov || overlay0color)
					pal = 256 + ov;
				*dst32++ = a2410_palette_32[pal];
				opix0 >>= 1;
				opix1 >>= 1;

				pal = pix & bitmap_mask;
				ov = opix0 & 1;
				ov |= (opix1 & 1) << 1;
				if (ov || overlay0color)
					pal = 256 + ov;
				*dst32++ = a2410_palette_32[pal];
				opix0 >>= 1;
				opix1 >>= 1;


			} else {

				*dst32++ = a2410_palette_32[pix >> 8];
				*dst32++ = a2410_palette_32[pix & 0xff];

				opix0 >>= 2;
				opix1 >>= 2;

			}

			overlay_bitcount += 2;
			overlay_bitcount &= 7;
		}
	}

	while (xx < picasso_vidinfo.width) {
		*dst32++ = 0;
		xx++;
	}

}

void tms_hsync_handler(void)
{
	for (int i = 0; i < a2410_hsync_max; i++)
		tms_hsync_handler2();
}

void standard_irq_callback(int level)
{
	a2410_interrupt = level;
	a2410_rethink();
}
