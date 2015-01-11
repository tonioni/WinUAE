/*
* UAE - The Un*x Amiga Emulator
*
* Other IDE controllers
*
* (c) 2015 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#include "memory.h"
#include "newcpu.h"
#include "uae.h"
#include "gui.h"
#include "filesys.h"
#include "threaddep/thread.h"
#include "debug.h"
#include "ide.h"
#include "idecontrollers.h"
#include "zfile.h"
#include "custom.h"
#include "rommgr.h"
#include "cpuboard.h"

#define DEBUG_IDE 0

#define GVP_IDE 0 // GVP A3001
#define TOTAL_IDE 1

#define GVP_IDE_ROM_OFFSET 0x8000

static struct ide_board gvp_ide_rom_board, gvp_ide_controller_board;
static struct ide_hdf *idecontroller_drive[TOTAL_IDE * 2];
static struct ide_thread_state idecontroller_its;

static void init_ide(struct ide_board *board, struct ide_hdf **idetable)
{
	alloc_ide_mem (idetable, 2, &idecontroller_its);
	board->ide = idetable[0];
	idetable[0]->board = board;
	idetable[1]->board = board;
	idetable[0]->byteswap = true;
	idetable[1]->byteswap = true;
	ide_initialize(idetable, 0);
	idecontroller_its.idetable = idecontroller_drive;
	idecontroller_its.idetotal = TOTAL_IDE * 2;
	start_ide_thread(&idecontroller_its);
}

static bool ide_irq_check(void)
{
	bool irq = ide_interrupt_check(idecontroller_drive, 2);
	gvp_ide_controller_board.irq = irq;
	return irq;
}

void idecontroller_rethink(void)
{
	if (!gvp_ide_controller_board.configured)
		return;
	if (gvp_ide_controller_board.intena && ide_irq_check() && !(intreq & 0x0008)) {
		INTREQ_0(0x8000 | 0x0008);
	}
}

void idecontroller_hsync(void)
{
	if (!gvp_ide_controller_board.configured)
		return;
	if (ide_irq_check())
		idecontroller_rethink();
}

void idecontroller_free_units(void)
{
	for (int i = 0; i < TOTAL_IDE * 2; i++) {
		remove_ide_unit(idecontroller_drive, i);
	}
}

int gvp_add_ide_unit(int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	ide = add_ide_unit (idecontroller_drive, 2, ch, ci);
	if (ide == NULL)
		return 0;
	return 1;
}


void idecontroller_free(void)
{
	stop_ide_thread(&idecontroller_its);
}

void idecontroller_reset(void)
{
	gvp_ide_controller_board.configured = 0;
	gvp_ide_controller_board.intena = false;
}

static bool is_gvp2_intreq(uaecptr addr)
{
	if (currprefs.cpuboard_type == BOARD_A3001_II && (addr & 0x440) == 0x440)
		return true;
	return false;
}
static bool is_gvp1_intreq(uaecptr addr)
{
	if (currprefs.cpuboard_type == BOARD_A3001_I && (addr & 0x440) == 0x40)
		return true;
	return false;
}

static int get_gvp_reg(uaecptr addr, struct ide_board *board, struct ide_hdf **idep)
{
	struct ide_hdf *ide;
	int reg = -1;

	if (addr & 0x1000) {
		reg = IDE_SECONDARY + ((addr >> 8) & 7);
	} else if (addr & 0x0800) {
		reg = (addr >> 8) & 7;
	}
	if (!(addr & 0x400) && (addr & 0x20)) {
		if (reg < 0)
			reg = 0;
		int extra = (addr >> 1) & 15;
		if (extra >= 8)
			reg |= IDE_SECONDARY;
		reg |= extra;
	}
	if (reg >= 0)
		reg &= IDE_SECONDARY | 7;

	ide = board->ide;
	if (idecontroller_drive[GVP_IDE]->ide_drv)
		ide = ide->pair;
	*idep = ide;
	return reg;
}

static uae_u32 ide_read_byte(struct ide_board *board, uaecptr addr)
{
	uae_u8 v = 0;
	addr &= 0xffff;
	if (addr < 0x40)
		return board->acmemory[addr];
	if (addr >= GVP_IDE_ROM_OFFSET) {
		if (board->rom) {
			if (addr & 1)
				v = 0xe8; // board id
			else 
				v = board->rom[((addr - GVP_IDE_ROM_OFFSET) / 2) & board->rom_mask];
			return v;
		}
		v = 0xe8;
#if DEBUG_IDE
		write_log(_T("GVP BOOT GET %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
		return v;
	}
	if (board->configured) {
		if (board == &gvp_ide_rom_board && currprefs.cpuboard_type == BOARD_A3001_II) {
			if (addr == 0x42) {
				v = 0xff;
			}
#if DEBUG_IDE
			write_log(_T("GVP BOOT GET %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
		} else {
			struct ide_hdf *ide;
			int regnum = get_gvp_reg(addr, board, &ide);
#if DEBUG_IDE
			write_log(_T("GVP IDE GET %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
			if (regnum >= 0) {
				v = ide_read_reg(ide, regnum);
			} else if (is_gvp2_intreq(addr)) {
				v = board->irq ? 0x40 : 0x00;
#if DEBUG_IDE
				write_log(_T("GVP IRQ %02x\n"), v);
#endif
				ide_irq_check();
			} else if (is_gvp1_intreq(addr)) {
				v = gvp_ide_controller_board.irq ? 0x80 : 0x00;
#if DEBUG_IDE
				write_log(_T("GVP IRQ %02x\n"), v);
#endif
				ide_irq_check();
			}
		}
	} else {
		v = 0xff;
	}
	return v;
}

static uae_u32 ide_read_word(struct ide_board *board, uaecptr addr)
{
	uae_u32 v = 0xffff;

	addr &= 65535;
	if (board->configured && (board == &gvp_ide_controller_board || currprefs.cpuboard_type == BOARD_A3001_I)) {
		if (addr < 0x60) {
			if (is_gvp1_intreq(addr))
				v = gvp_ide_controller_board.irq ? 0x8000 : 0x0000;
			else if (addr == 0x40) {
				if (currprefs.cpuboard_type == BOARD_A3001_II)
					v = board->intena ? 8 : 0;
			}
#if DEBUG_IDE
			write_log(_T("GVP IO WORD READ %08x %08x\n"), addr, M68K_GETPC);
#endif
		} else {
			struct ide_hdf *ide;
			int regnum = get_gvp_reg(addr, board, &ide);
			if (regnum == IDE_DATA) {
				v = ide_get_data(ide);
#if DEBUG_IDE > 2
				write_log(_T("IDE WORD READ %04x\n"), v);
#endif
			} else {
				v = ide_read_byte(board, addr) << 8;
				v |= ide_read_byte(board, addr + 1);
			}
		}
	}
	return v;
}

static void ide_write_byte(struct ide_board *board, uaecptr addr, uae_u8 v)
{
	addr &= 65535;
	if (!board->configured) {
		addrbank *ab = board->bank;
		if (addr == 0x48) {
			map_banks_z2(ab, v, 0x10000 >> 16);
			board->configured = 1;
			expamem_next(ab, NULL);
			return;
		}
		if (addr == 0x4c) {
			board->configured = 1;
			expamem_shutup(ab);
			return;
		}
	}
	if (board->configured) {
		if (board == &gvp_ide_rom_board && currprefs.cpuboard_type == BOARD_A3001_II) {
#if DEBUG_IDE
			write_log(_T("GVP BOOT PUT %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
		} else {
			struct ide_hdf *ide;
			int regnum = get_gvp_reg(addr, board, &ide);
#if DEBUG_IDE
			write_log(_T("GVP IDE PUT %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
			if (regnum >= 0)
				ide_write_reg(ide, regnum, v);
		}
	}
}

static void ide_write_word(struct ide_board *board, uaecptr addr, uae_u16 v)
{
	addr &= 65535;
	if (board->configured && (board == &gvp_ide_controller_board || currprefs.cpuboard_type == BOARD_A3001_I)) {
		if (addr < 0x60) {
#if DEBUG_IDE
			write_log(_T("GVP IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
			if (addr == 0x40 && currprefs.cpuboard_type == BOARD_A3001_II)
				board->intena = (v & 8) != 0;
		} else {
			struct ide_hdf *ide;
			int regnum = get_gvp_reg(addr, board, &ide);
			if (regnum == IDE_DATA) {
				ide_put_data(ide, v);
#if DEBUG_IDE > 2
				write_log(_T("IDE WORD WRITE %04x\n"), v);
#endif
			} else {
				ide_write_byte(board, addr, v >> 8);
				ide_write_byte(board, addr + 1, v & 0xff);
			}
		}
	}
}

static uae_u32 REGPARAM2 ide_controller_gvp_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  ide_read_word (&gvp_ide_controller_board, addr + 0) << 16;
	v |= ide_read_word (&gvp_ide_controller_board, addr + 2) << 0;
	return v;
}
static uae_u32 REGPARAM2 ide_controller_gvp_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  ide_read_word (&gvp_ide_controller_board, addr);
	return v;
}
static uae_u32 REGPARAM2 ide_controller_gvp_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return ide_read_byte (&gvp_ide_controller_board, addr);
}
static void REGPARAM2 ide_controller_gvp_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	ide_write_word (&gvp_ide_controller_board, addr + 0, l >> 16);
	ide_write_word (&gvp_ide_controller_board, addr + 2, l >> 0);
}
static void REGPARAM2 ide_controller_gvp_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	ide_write_word (&gvp_ide_controller_board, addr + 0, w);
}
static void REGPARAM2 ide_controller_gvp_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	ide_write_byte (&gvp_ide_controller_board, addr, b);
}
addrbank gvp_ide_controller_bank = {
	ide_controller_gvp_lget, ide_controller_gvp_wget, ide_controller_gvp_bget,
	ide_controller_gvp_lput, ide_controller_gvp_wput, ide_controller_gvp_bput,
	default_xlate, default_check, NULL, NULL, _T("GVP IDE"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

static uae_u32 REGPARAM2 ide_rom_gvp_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  ide_read_word (&gvp_ide_rom_board, addr + 0) << 16;
	v |= ide_read_word (&gvp_ide_rom_board, addr + 2) << 0;
	return v;
}
static uae_u32 REGPARAM2 ide_rom_gvp_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  ide_read_word (&gvp_ide_rom_board, addr);
	return v;
}
static uae_u32 REGPARAM2 ide_rom_gvp_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return ide_read_byte (&gvp_ide_rom_board, addr);
}
static void REGPARAM2 ide_rom_gvp_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	ide_write_word (&gvp_ide_rom_board, addr + 0, l >> 16);
	ide_write_word (&gvp_ide_rom_board, addr + 2, l >> 0);
}
static void REGPARAM2 ide_rom_gvp_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	ide_write_word (&gvp_ide_rom_board, addr + 0, w);
}
static void REGPARAM2 ide_rom_gvp_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	ide_write_byte (&gvp_ide_rom_board, addr, b);
}
addrbank gvp_ide_rom_bank = {
	ide_rom_gvp_lget, ide_rom_gvp_wget, ide_rom_gvp_bget,
	ide_rom_gvp_lput, ide_rom_gvp_wput, ide_rom_gvp_bput,
	default_xlate, default_check, NULL, NULL, _T("GVP BOOT"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

static void ew(struct ide_board *ide, int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		ide->acmemory[addr] = (value & 0xf0);
		ide->acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		ide->acmemory[addr] = ~(value & 0xf0);
		ide->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static const uae_u8 gvp_ide2_rom_autoconfig[16] = { 0xd1, 0x0d, 0x00, 0x00, 0x07, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 };
static const uae_u8 gvp_ide2_controller_autoconfig[16] = { 0xc1, 0x0b, 0x00, 0x00, 0x07, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uae_u8 gvp_ide1_controller_autoconfig[16] = { 0xd1, 0x08, 0x00, 0x00, 0x07, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 };

addrbank *gvp_ide_rom_autoconfig_init(void)
{
	struct ide_board *ide = &gvp_ide_rom_board;
	int roms[2];
	struct romlist *rl;
	const uae_u8 *autoconfig;

	if (currprefs.cpuboard_type == BOARD_A3001_I) {
		ide->bank = &gvp_ide_rom_bank;
		autoconfig = gvp_ide1_controller_autoconfig;
		init_ide(ide, &idecontroller_drive[GVP_IDE]);
		ide->rom_size = 8192;
		gvp_ide_controller_board.intena = true;
		gvp_ide_controller_board.configured = -1;
		roms[0] = 114;
		roms[1] = -1;
	} else {
		ide->bank = &gvp_ide_rom_bank;
		autoconfig = gvp_ide2_rom_autoconfig;
		ide->rom_size = 16384;
		roms[0] = -1;
	}
	ide->configured = 0;
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);


	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	const TCHAR *romname = currprefs.acceleratorromfile;
	struct zfile *z = read_rom_name(romname);
	if (!z) {
		rl = getromlistbyids(roms, romname);
		if (rl) {
			z = read_rom(rl->rd);
		}
	}
	if (z) {
		for (int i = 0; i < 16; i++) {
			uae_u8 b = autoconfig[i];
			ew(ide, i * 4, b);
		}
		write_log(_T("GVP IDE BOOT ROM '%s'\n"), zfile_getname(z));
		int size = zfile_fread(ide->rom, 1, ide->rom_size, z);
		zfile_fclose(z);
	} else {
		romwarning(roms);
	}
	return ide->bank;
}

addrbank *gvp_ide_controller_autoconfig_init(void)
{
	struct ide_board *ide = &gvp_ide_controller_board;

	init_ide(ide, &idecontroller_drive[GVP_IDE]);
	ide->configured = 0;
	ide->bank = &gvp_ide_controller_bank;
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = gvp_ide2_controller_autoconfig[i];
		ew(ide, i * 4, b);
	}
	return ide->bank;
}

