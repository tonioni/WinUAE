/*
* UAE - The Un*x Amiga Emulator
*
* Cirrus Logic based graphics board emulation
*
* Copyright 2013 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "debug.h"
#include "custom.h"
#include "newcpu.h"
#include "picasso96.h"
#include "statusline.h"
#include "gfxboard.h"

#include "qemuvga/qemuuaeglue.h"
#include "qemuvga/vga.h"

#define BOARD_REGISTERS_SIZE 0x00010000

#define BOARD_MANUFACTURER_PICASSO 2167
#define BOARD_MODEL_MEMORY_PICASSOII 11
#define BOARD_MODEL_REGISTERS_PICASSOII 12

#define BOARD_MODEL_MEMORY_PICASSOIV 24
#define BOARD_MODEL_REGISTERS_PICASSOIV 23
#define PICASSOIV_REG  0x00600000
#define PICASSOIV_IO   0x00200000
#define PICASSOIV_VRAM 0x01000000


#define BOARD_MANUFACTURER_PICCOLO 2195
#define BOARD_MODEL_MEMORY_PICCOLO 5
#define BOARD_MODEL_REGISTERS_PICCOLO 6
#define BOARD_MODEL_MEMORY_PICCOLO64 10
#define BOARD_MODEL_REGISTERS_PICCOLO64 11

#define BOARD_MANUFACTURER_SPECTRUM 2193
#define BOARD_MODEL_MEMORY_SPECTRUM 1
#define BOARD_MODEL_REGISTERS_SPECTRUM 2

struct gfxboard
{
	TCHAR *name;
	int manufacturer;
	int model_memory;
	int model_registers;
	int serial;
	int vrammin;
	int vrammax;
	int banksize;
	int chiptype;
	bool z3;
	bool irq;
	bool swap;
};

#define PICASSOIV 10

static struct gfxboard boards[] =
{
	{
		_T("Picasso II"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOII, BOARD_MODEL_REGISTERS_PICASSOII,
		0x00020000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5426, false, false, false
	},
	{
		_T("Picasso II+"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOII, BOARD_MODEL_REGISTERS_PICASSOII,
		0x00100000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5428, false, true, false
	},
	{
		_T("Piccolo"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO, BOARD_MODEL_REGISTERS_PICCOLO,
		0x00000000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5426, false, true, true
	},
	{
		_T("Piccolo SD64 Zorro II"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO64, BOARD_MODEL_REGISTERS_PICCOLO64,
		0x00000000, 0x00200000, 0x00400000, 0x00200000, CIRRUS_ID_CLGD5434, false, true, true
	},
	{
		_T("Piccolo SD64 Zorro III"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO64, BOARD_MODEL_REGISTERS_PICCOLO64,
		0x00000000, 0x00200000, 0x00400000, 0x01000000, CIRRUS_ID_CLGD5434, true, true, true
	},
	{
		_T("Spectrum 28/24 Zorro II"),
		BOARD_MANUFACTURER_SPECTRUM, BOARD_MODEL_MEMORY_SPECTRUM, BOARD_MODEL_REGISTERS_SPECTRUM,
		0x00000000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5428, false, true, true
	},
	{
		_T("Spectrum 28/24 Zorro III"),
		BOARD_MANUFACTURER_SPECTRUM, BOARD_MODEL_MEMORY_SPECTRUM, BOARD_MODEL_REGISTERS_SPECTRUM,
		0x00000000, 0x00100000, 0x00200000, 0x01000000, CIRRUS_ID_CLGD5428, true, true, true
	},
	{
		_T("Picasso IV Zorro II"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOIV, BOARD_MODEL_REGISTERS_PICASSOIV,
		0x00000000, 0x00400000, 0x00400000, 0x00400000, CIRRUS_ID_CLGD5446, false, true, true
	},
	{
		// REG:00600000 IO:00200000 VRAM:01000000
		_T("Picasso IV Zorro III"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOIV, 0,
		0x00000000, 0x00400000, 0x00400000, 0x04000000, CIRRUS_ID_CLGD5446, true, true, true
	}
};


static int configured_mem, configured_regs;
static struct gfxboard *board;
static uae_u32 memory_mask;

static uae_u8 automemory[256];
static CirrusVGAState vga;
static uae_u8 *vram;
static uae_u32 gfxboardmem_start;
static bool monswitch;
static bool oldswitch;
static int fullrefresh;
static bool modechanged;
static uae_u8 *gfxboard_surface, *vram_address, *fakesurface_surface;
static bool gfxboard_vblank;
static bool blit_cpu_to_vram;

static const MemoryRegionOps *vgaio, *vgablitram;
static const MemoryRegion *vgaioregion, *vgavramregion;

static void init_board (void)
{
	int vramsize = board->vrammax;
	xfree (fakesurface_surface);
	fakesurface_surface = xmalloc (uae_u8, 4 * 10000);
	if (currprefs.rtgmem_type == PICASSOIV)
		vramsize = 16 * 1024 * 1024;
	vram = mapped_malloc (vramsize, board->z3 ? _T("z3_gfx") : _T("z2_gfx"));
	vga.vga.vram_size_mb = currprefs.rtgmem_size >> 20;
	vga_common_init(&vga.vga);
	cirrus_init_common(&vga, board->chiptype, 0,  NULL, NULL);
	picasso_allocatewritewatch (currprefs.rtgmem_size);
}

static void gfxboard_setmode (void)
{
	int bpp, width, height;

	bpp = vga.vga.get_bpp (&vga.vga);
	vga.vga.get_resolution (&vga.vga, &width, &height);

	picasso96_state.Width = width;
	picasso96_state.Height = height;
	picasso96_state.BytesPerPixel = bpp / 8;
	picasso96_state.RGBFormat = RGBFB_CLUT;
	write_log (_T("GFXBOARD %dx%dx%d\n"), width, height, bpp);
	gfx_set_picasso_modeinfo (width, height, bpp, RGBFB_NONE);
	fullrefresh = 2;
}

static bool gfxboard_checkchanged (void)
{
	int bpp, width, height;
	bpp = vga.vga.get_bpp (&vga.vga);
	vga.vga.get_resolution (&vga.vga, &width, &height);

	if (picasso96_state.Width != width ||
		picasso96_state.Height != height ||
		picasso96_state.BytesPerPixel != bpp / 8)
		return true;
	return false;
}

static DisplaySurface gfxsurface, fakesurface;
DisplaySurface *qemu_console_surface(QemuConsole *con)
{
	if (picasso_on)
		return &gfxsurface;
	modechanged = true;
	return &fakesurface;
}

void qemu_console_resize(QemuConsole *con, int width, int height)
{
}

static uae_u8 pal64 (uae_u8 v)
{
	v = (v << 2) | ((v >> 2) & 3);
	return v;
}

DisplaySurface* qemu_create_displaysurface_from(int width, int height, int bpp,
                                                int linesize, uint8_t *data,
                                                bool byteswap)
{
	modechanged = true;
	return &fakesurface;
}

int surface_bits_per_pixel(DisplaySurface *s)
{
	if (s == &fakesurface)
		return 32;
	return picasso_vidinfo.pixbytes * 8;
}
int surface_bytes_per_pixel(DisplaySurface *s)
{
	if (s == &fakesurface)
		return 4;
	return picasso_vidinfo.pixbytes;
}

int surface_stride(DisplaySurface *s)
{
	if (s == &fakesurface)
		return 0;
	if (gfxboard_surface == NULL)
		gfxboard_surface = gfx_lock_picasso (false, false);
	return picasso_vidinfo.rowbytes;
}
uint8_t *surface_data(DisplaySurface *s)
{
	if (s == &fakesurface)
		return fakesurface_surface;
	if (gfxboard_surface == NULL)
		gfxboard_surface = gfx_lock_picasso (false, false);
	return gfxboard_surface;
}

void gfxboard_refresh (void)
{
	fullrefresh = 2;
}

void gfxboard_vsync_handler (void)
{
	if (!configured_mem || !configured_regs)
		return;

	if ((modechanged || gfxboard_checkchanged ()) && monswitch) {
		gfxboard_setmode ();
		init_hz_p96 ();
		modechanged = false;
		picasso_requested_on = true;
		return;
	}

	if (monswitch != oldswitch) {
		if (!monswitch)
			picasso_requested_on = monswitch;
		oldswitch = monswitch;
		write_log (_T("GFXBOARD ACTIVE=%d\n"), monswitch);
	}

	if (monswitch) {
		picasso_getwritewatch ();
		vga.vga.hw_ops->gfx_update(&vga);
	}

	if (picasso_on) {
		if (currprefs.leds_on_screen & STATUSLINE_RTG) {
			if (gfxboard_surface == NULL) {
				gfxboard_surface = gfx_lock_picasso (false, false);
			}
			if (gfxboard_surface) {
				if (!(currprefs.leds_on_screen & STATUSLINE_TARGET))
					picasso_statusline (gfxboard_surface);
			}
		}
	}

	if (gfxboard_surface)
		gfx_unlock_picasso (true);
	gfxboard_surface = NULL;

	// Vertical Sync End Register, 0x10 = Clear Vertical Interrupt.
	if (board->irq && (vga.vga.cr[0x11] & 0x10)) {
		gfxboard_vblank = true;
		INTREQ (0x8000 | 0x0008);
	}

	if (fullrefresh > 0)
		fullrefresh--;
}

double gfxboard_get_vsync (void)
{
	return vblank_hz; // FIXME
}

void dpy_gfx_update(QemuConsole *con, int x, int y, int w, int h)
{
	picasso_invalidate (x, y, w, h);
}

void memory_region_reset_dirty(MemoryRegion *mr, hwaddr addr,
                               hwaddr size, unsigned client)
{
}
bool memory_region_get_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size, unsigned client)
{
	if (mr != vgavramregion)
		return false;
	if (fullrefresh)
		return true;
	return picasso_is_vram_dirty (addr + p96ram_start, size);
}

static QEMUResetHandler *reset_func;
static void *reset_parm;
void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
	reset_func = func;
	reset_parm = opaque;
	reset_func (reset_parm);
}

static void bput_regtest (uaecptr addr, uae_u8 v)
{
	addr += 0x3b0;
	if ((addr == 0x3b5 || addr == 0x3c5) && vga.vga.cr_index == 0x11) {
		if (!(vga.vga.cr[0x11] & 0x10))
			gfxboard_vblank = false;
	}
	if (vga.cirrus_srcptr != vga.cirrus_srcptr_end && !blit_cpu_to_vram) {
		blit_cpu_to_vram = true;
#ifdef JIT
		if (currprefs.cachesize && (!currprefs.comptrustbyte || !currprefs.comptrustword || !currprefs.comptrustlong))
			flush_icache (0, 3);
#endif
	}
}

static uae_u8 bget_regtest (uaecptr addr, uae_u8 v)
{
	addr += 0x3b0;
	if (gfxboard_vblank) {
		// Input Status 0
		if (addr == 0x3c2) {
			// Disable Vertical Interrupt == 0?
			// Clear Vertical Interrupt == 1
			if (!(vga.vga.cr[0x11] & 0x20) && (vga.vga.cr[0x11] & 0x10)) {
				v |= 0x80; // interrupt pending
			}
		}
	}
	return v;
}

static void checkblitend (void)
{
	if (vga.cirrus_srcptr == vga.cirrus_srcptr_end) {
		blit_cpu_to_vram = false;
	}
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
	if (mr == vgavramregion)
		return vram;
	return NULL;
}

void memory_region_init_ram(MemoryRegion *mr,
                            const char *name,
                            uint64_t size)
{
	if (!stricmp (name, "vga.vram")) {
		vgavramregion = mr;
	}
}

void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
	if (!stricmp (name, "cirrus-io")) {
		vgaio = ops;
		vgaioregion = mr;
	} else if (!stricmp (name, "cirrus-low-memory")) {
		vgablitram = ops;
	}
}

int is_surface_bgr(DisplaySurface *surface)
{
	return board->swap;
}

static uae_u32 REGPARAM2 gfxboard_lget_mem (uaecptr addr)
{
#if 0
#ifdef JIT
	special_mem |= S_READ;
#endif
	return vgavram->read (&vga, addr, 4);
#else
	uae_u8 *m;
	addr -= gfxboardmem_start & memory_mask;
	addr &= memory_mask;
	m = vram + addr;
	return do_get_mem_long ((uae_u32 *)m);
#endif
}
static uae_u32 REGPARAM2 gfxboard_wget_mem (uaecptr addr)
{
#if 0
#ifdef JIT
	special_mem |= S_READ;
#endif
	return vgavram->read (&vga, addr, 2);
#else
	uae_u8 *m;
	addr -= gfxboardmem_start & memory_mask;
	addr &= memory_mask;
	m = vram + addr;
	return do_get_mem_word ((uae_u16 *)m);
#endif
}
static uae_u32 REGPARAM2 gfxboard_bget_mem (uaecptr addr)
{
#if 0
#ifdef JIT
	special_mem |= S_READ;
#endif
	return vgavram->read (&vga, addr, 1);
#else
	addr -= gfxboardmem_start & memory_mask;
	addr &= memory_mask;
	return vram[addr];
#endif
}
static void REGPARAM2 gfxboard_lput_mem (uaecptr addr, uae_u32 l)
{
	if (blit_cpu_to_vram) {
#ifdef JIT
		special_mem |= S_WRITE;
#endif
		vgablitram->write (&vga, 0, l >> 24, 1);
		vgablitram->write (&vga, 0, l >> 16, 1);
		vgablitram->write (&vga, 0, l >> 8, 1);
		vgablitram->write (&vga, 0, l >> 0, 1);
		checkblitend ();
	} else {
		uae_u8 *m;
		addr -= gfxboardmem_start & memory_mask;
		addr &= memory_mask;
		m = vram + addr;
		do_put_mem_long ((uae_u32 *) m, l);
	}
}
static void REGPARAM2 gfxboard_wput_mem (uaecptr addr, uae_u32 w)
{
	if (blit_cpu_to_vram) {
#ifdef JIT
		special_mem |= S_WRITE;
#endif
		vgablitram->write (&vga, 0, w >> 8, 1);
		vgablitram->write (&vga, 0, w >> 0, 1);
		checkblitend ();
	} else {
		uae_u8 *m;
		addr -= gfxboardmem_start & memory_mask;
		addr &= memory_mask;
		m = vram + addr;
		do_put_mem_word ((uae_u16 *)m, w);
	}
}
static void REGPARAM2 gfxboard_bput_mem (uaecptr addr, uae_u32 b)
{
	if (blit_cpu_to_vram) {
#ifdef JIT
		special_mem |= S_WRITE;
#endif
		vgablitram->write (&vga, 0, b, 1);
		checkblitend ();
	} else {
		addr -= gfxboardmem_start & memory_mask;
		addr &= memory_mask;
		vram[addr] = b;
	}
}

static int REGPARAM2 gfxboard_check (uaecptr addr, uae_u32 size)
{
	addr -= gfxboardmem_start & memory_mask;
	addr &= memory_mask;
	return (addr + size) <= currprefs.rtgmem_size;
}
static uae_u8 *REGPARAM2 gfxboard_xlate (uaecptr addr)
{
	addr -= gfxboardmem_start & memory_mask;
	addr &= memory_mask;
	return vram + addr;
}

static uae_u32 REGPARAM2 gfxboard_bget_mem_autoconfig (uaecptr addr)
{
	uae_u32 v = 0;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	if (addr < 0x40)
		v = automemory[addr];
	return v;
}

static void REGPARAM2 gfxboard_wput_mem_autoconfig (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (!board->z3)
		return;
	b &= 0xffff;
	addr &= 65535;
	if (addr == 0x44) {
		put_word (regs.regs[11] + 0x20, p96ram_start >> 16);
		put_word (regs.regs[11] + 0x28, p96ram_start >> 16);
		gfxboard_bank_memory.bget = gfxboard_bget_mem;
		gfxboard_bank_memory.bput = gfxboard_bput_mem;
		gfxboard_bank_memory.wput = gfxboard_wput_mem;
		if (currprefs.rtgmem_type == PICASSOIV) {
			map_banks (&gfxboard_bank_memory, (p96ram_start + PICASSOIV_VRAM) >> 16, (board->banksize - PICASSOIV_VRAM) >> 16, currprefs.rtgmem_size);
			map_banks (&gfxboard_bank_registers, (p96ram_start + PICASSOIV_IO) >> 16, BOARD_REGISTERS_SIZE >> 16, BOARD_REGISTERS_SIZE);
		} else {
			map_banks (&gfxboard_bank_memory, p96ram_start >> 16, board->banksize >> 16, currprefs.rtgmem_size);
		}
		write_log (_T("%s autoconfigured at 0x%04X0000\n"), gfxboard_bank_memory.name, p96ram_start >> 16);
		configured_mem = p96ram_start >> 16;
		gfxboardmem_start = p96ram_start;
		expamem_next ();
		return;
	}
	if (addr == 0x4c) {
		write_log (_T("%s AUTOCONFIG SHUT-UP!\n"), gfxboard_bank_memory.name);
		configured_mem = 0xff;
		expamem_next ();
		return;
	}
}


static void REGPARAM2 gfxboard_bput_mem_autoconfig (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48) {
		if (!board->z3) {
			gfxboard_bank_memory.bget = gfxboard_bget_mem;
			gfxboard_bank_memory.bput = gfxboard_bput_mem;
			map_banks (&gfxboard_bank_memory, b, board->banksize >> 16, currprefs.rtgmem_size);
			write_log (_T("%s autoconfigured at 0x00%02X0000\n"), gfxboard_bank_memory.name, b);
			configured_mem = b;
			gfxboardmem_start = b << 16;
			expamem_next ();
		}
		return;
	}
	if (addr == 0x4c) {
		write_log (_T("%s AUTOCONFIG SHUT-UP!\n"), gfxboard_bank_memory.name);
		configured_mem = 0xff;
		expamem_next ();
		return;
	}
}

static uaecptr mungeaddr (uaecptr addr)
{
	addr &= 65535;
	if (addr >= 0x2000)
		return 0;
	if (addr >= 0x1000)
		addr++;
	addr &= 0x0fff;
	if (addr < 0x3b0)
		return 0;
	addr -= 0x3b0;
	return addr;
}

static uae_u32 REGPARAM2 gfxboard_lget_regs (uaecptr addr)
{
	uae_u32 v = 0xffffffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr = mungeaddr (addr);
	if (addr)
		v = vgaio->read (&vga, addr, 4);
	return v;
}
static uae_u32 REGPARAM2 gfxboard_wget_regs (uaecptr addr)
{
	uae_u16 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr = mungeaddr (addr);
	if (addr) {
		uae_u8 v1, v2;
		v1  = vgaio->read (&vga, addr + 0, 1);
		v1 = bget_regtest (addr + 0, v1);
		v2 = vgaio->read (&vga, addr + 1, 1);
		v2 = bget_regtest (addr + 1, v2);
		v = (v1 << 8) | v2;
	}
	return v;
}
static uae_u32 REGPARAM2 gfxboard_bget_regs (uaecptr addr)
{
	uae_u8 v = 0xff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	if (addr >= 0x8000)
		return 0;
	addr = mungeaddr (addr);
	if (addr) {
		v = vgaio->read (&vga, addr, 1);
		v = bget_regtest (addr, v);
	}
	return v;
}

static void REGPARAM2 gfxboard_lput_regs (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	//write_log (_T("GFX LONG PUT IO %04X = %04X\n"), addr & 65535, l);
	addr = mungeaddr (addr);
	if (addr)
		vgaio->write (&vga, addr, l, 4);
}
static void REGPARAM2 gfxboard_wput_regs (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	//write_log (_T("GFX WORD PUT IO %04X = %04X\n"), addr & 65535, w & 0xffff);
	addr = mungeaddr (addr);
	if (addr) {
		vgaio->write (&vga, addr + 0, (w >> 8) & 0xff, 1);
		bput_regtest (addr + 0, (w >> 8) & 0xff);
		vgaio->write (&vga, addr + 1, (w >> 0) & 0xff, 1);
		bput_regtest (addr + 1, (w >> 0) & 0xff);
	}
}
static void REGPARAM2 gfxboard_bput_regs (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	//write_log (_T("GFX BYTE PUT IO %04X = %02X\n"), addr & 65535, b & 0xff);
	addr &= 65535;
	if (addr >= 0x8000) {
		write_log (_T("GFX SPECIAL BYTE PUT IO %04X = %02X\n"), addr & 65535, b & 0xff);
		switch (board->manufacturer)
		{
		case BOARD_MANUFACTURER_PICASSO:
			{
				int idx = addr >> 12;
				if (idx == 11)
					monswitch = false;
				else if (idx == 10)
					monswitch = true;
			}
		break;
		case BOARD_MANUFACTURER_PICCOLO:
		case BOARD_MANUFACTURER_SPECTRUM:
			monswitch = (b & 0x20) != 0;
		break;
		}
		return;
	}
	addr = mungeaddr (addr);
	if (addr) {
		vgaio->write (&vga, addr, b & 0xff, 1);
		bput_regtest (addr, b);
	}
}

static uae_u32 REGPARAM2 gfxboard_bget_regs_autoconfig (uaecptr addr)
{
	uae_u32 v = 0;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	if (addr < 0x40)
		v = automemory[addr];
	return v;
}

static void REGPARAM2 gfxboard_bput_regs_autoconfig (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48) {
		gfxboard_bank_registers.bget = gfxboard_bget_regs;
		gfxboard_bank_registers.bput = gfxboard_bput_regs;
		map_banks (&gfxboard_bank_registers, b, BOARD_REGISTERS_SIZE >> 16, BOARD_REGISTERS_SIZE);
		write_log (_T("%s autoconfigured at 0x00%02X0000\n"), gfxboard_bank_registers.name, b);
		configured_regs = b;
		init_board ();
		expamem_next ();
		return;
	}
	if (addr == 0x4c) {
		write_log (_T("%s AUTOCONFIG SHUT-UP!\n"), gfxboard_bank_registers.name);
		configured_regs = 0xff;
		expamem_next ();
		return;
	}
}

static void ew (int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		automemory[addr] = (value & 0xf0);
		automemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		automemory[addr] = ~(value & 0xf0);
		automemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

void gfxboard_reset (void)
{
	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE) {
		board = &boards[currprefs.rtgmem_type - GFXBOARD_HARDWARE];
		memory_mask = currprefs.rtgmem_size - 1;
	}
	if (vram)
		mapped_free (vram);
	vram = NULL;
	xfree (fakesurface_surface);
	fakesurface_surface = NULL;
	configured_mem = 0;
	configured_regs = 0;
	gfxboard_bank_registers.bget = gfxboard_bget_regs_autoconfig;
	gfxboard_bank_registers.bput = gfxboard_bput_regs_autoconfig;
	gfxboard_bank_memory.bget = gfxboard_bget_mem_autoconfig;
	gfxboard_bank_memory.bput = gfxboard_bput_mem_autoconfig;
	if (board) {
		if (board->z3)
			gfxboard_bank_memory.wput = gfxboard_wput_mem_autoconfig;
		if (reset_func) 
			reset_func (reset_parm);
	}
}

addrbank gfxboard_bank_memory = {
	gfxboard_lget_mem, gfxboard_wget_mem, gfxboard_bget_mem,
	gfxboard_lput_mem, gfxboard_wput_mem, gfxboard_bput_mem_autoconfig,
	gfxboard_xlate, gfxboard_check, NULL, NULL,
	gfxboard_lget_mem, gfxboard_wget_mem, ABFLAG_RAM
};
addrbank gfxboard_bank_registers = {
	gfxboard_lget_regs, gfxboard_wget_regs, gfxboard_bget_regs,
	gfxboard_lput_regs, gfxboard_wput_regs, gfxboard_bput_regs_autoconfig,
	default_xlate, default_check, NULL, NULL,
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

bool gfxboard_is_z3 (int type)
{
	if (type == GFXBOARD_UAE_Z2)
		return false;
	if (type == GFXBOARD_UAE_Z3)
		return true;
	board = &boards[type - GFXBOARD_HARDWARE];
	return board->z3;
}

int gfxboard_get_vram_min (int type)
{
	if (type < 2)
		return -1;
	board = &boards[type - 2];
	return board->vrammax; //board->vrammin;
}

int gfxboard_get_vram_max (int type)
{
	if (type < 2)
		return -1;
	board = &boards[type - 2];
	return board->vrammax;
}

bool gfxboard_is_registers (int type)
{
	if (type < 2)
		return false;
	board = &boards[type - 2];
	return board->model_registers != 0;
}

void gfxboard_init_memory (void)
{
	int vram = currprefs.rtgmem_size;
	uae_u8 flags;
	memset (automemory, 0xff, sizeof automemory);
	
	flags = 0x05;
	vram /= 0x00100000;
	while (vram > 1) {
		flags++;
		vram >>= 1;
	}
	if (board->z3) {
		ew (0x00, 0x00 | 0x08 | 0x80); // 16M Z3
		ew (0x08, flags | 0x10);
	} else {
		ew (0x00, flags | 0x08 | 0xc0);
	}
	ew (0x04, board->model_memory);
	ew (0x10, board->manufacturer >> 8);
	ew (0x14, board->manufacturer);

	uae_u32 ser = board->serial;
	ew (0x18, ser >> 24); /* ser.no. Byte 0 */
	ew (0x1c, ser >> 16); /* ser.no. Byte 1 */
	ew (0x20, ser >>  8); /* ser.no. Byte 2 */
	ew (0x24, ser >>  0); /* ser.no. Byte 3 */

	gfxboard_bank_memory.name = board->name;
	gfxboard_bank_registers.name = board->name;
	map_banks (&gfxboard_bank_memory, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
}

void gfxboard_init_registers (void)
{
	if (!board->model_registers)
		return;
	memset (automemory, 0xff, sizeof automemory);
	ew (0x00, 0xc0 | 0x01); // 64k Z2
	ew (0x04, board->model_registers);
	ew (0x10, board->manufacturer >> 8);
	ew (0x14, board->manufacturer);

	uae_u32 ser = board->serial;
	ew (0x18, ser >> 24); /* ser.no. Byte 0 */
	ew (0x1c, ser >> 16); /* ser.no. Byte 1 */
	ew (0x20, ser >>  8); /* ser.no. Byte 2 */
	ew (0x24, ser >>  0); /* ser.no. Byte 3 */

	map_banks (&gfxboard_bank_registers, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
}
