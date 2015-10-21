/*
* UAE - The Un*x Amiga Emulator
*
* Cirrus Logic based graphics board emulation
*
* Copyright 2013 Toni Wilen
*
*/

#define VRAMLOG 0
#define MEMLOGR 0
#define MEMLOGW 0
#define MEMLOGINDIRECT 0
#define MEMDEBUG 0
#define MEMDEBUGMASK 0x7fffff
#define MEMDEBUGTEST 0x1ff000
#define PICASSOIV_DEBUG_IO 0

#if MEMLOGR
static bool memlogr = false;
static bool memlogw = false;
#endif

#define BYTESWAP_WORD -1
#define BYTESWAP_LONG 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "debug.h"
#include "custom.h"
#include "newcpu.h"
#include "picasso96.h"
#include "statusline.h"
#include "rommgr.h"
#include "zfile.h"
#include "gfxboard.h"
#include "rommgr.h"

#include "qemuvga/qemuuaeglue.h"
#include "qemuvga/vga.h"

#define MONITOR_SWITCH_DELAY 25

#define GFXBOARD_AUTOCONFIG_SIZE 131072

#define BOARD_REGISTERS_SIZE 0x00010000

#define BOARD_MANUFACTURER_PICASSO 2167
#define BOARD_MODEL_MEMORY_PICASSOII 11
#define BOARD_MODEL_REGISTERS_PICASSOII 12

#define BOARD_MODEL_MEMORY_PICASSOIV 24
#define BOARD_MODEL_REGISTERS_PICASSOIV 23
#define PICASSOIV_REG  0x00600000
#define PICASSOIV_IO   0x00200000
#define PICASSOIV_VRAM1 0x01000000
#define PICASSOIV_VRAM2 0x00800000
#define PICASSOIV_ROM_OFFSET 0x0200
#define PICASSOIV_FLASH_OFFSET 0x8000
#define PICASSOIV_FLASH_BANK 0x8000
#define PICASSOIV_MAX_FLASH (GFXBOARD_AUTOCONFIG_SIZE - 32768)

#define PICASSOIV_BANK_UNMAPFLASH 2
#define PICASSOIV_BANK_MAPRAM 4
#define PICASSOIV_BANK_FLASHBANK 128

#define PICASSOIV_INT_VBLANK 128

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
	const TCHAR *name;
	const TCHAR *manufacturername;
	const TCHAR *configname;
	int manufacturer;
	int model_memory;
	int model_registers;
	int serial;
	int vrammin;
	int vrammax;
	int banksize;
	int chiptype;
	int configtype;
	int irq;
	bool swap;
	uae_u32 romtype;
};

#define ISP4() (currprefs.rtgmem_type == GFXBOARD_PICASSO4_Z2 || currprefs.rtgmem_type == GFXBOARD_PICASSO4_Z3)

// Picasso II: 8* 4x256 (1M) or 16* 4x256 (2M)
// Piccolo: 8* 4x256 + 2* 16x256 (2M)

static struct gfxboard boards[] =
{
	{
		_T("Picasso II"), _T("Village Tronic"), _T("PicassoII"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOII, BOARD_MODEL_REGISTERS_PICASSOII,
		0x00020000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5426, 2, 0, false
	},
	{
		_T("Picasso II+"), _T("Village Tronic"), _T("PicassoII+"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOII, BOARD_MODEL_REGISTERS_PICASSOII,
		0x00100000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5428, 2, 2, false
	},
	{
		_T("Piccolo Zorro II"), _T("Ingenieurbüro Helfrich"), _T("Piccolo_Z2"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO, BOARD_MODEL_REGISTERS_PICCOLO,
		0x00000000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5426, 2, 6, true
	},
	{
		_T("Piccolo Zorro III"), _T("Ingenieurbüro Helfrich"), _T("Piccolo_Z3"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO, BOARD_MODEL_REGISTERS_PICCOLO,
		0x00000000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5426, 3, 6, true
	},
	{
		_T("Piccolo SD64 Zorro II"), _T("Ingenieurbüro Helfrich"), _T("PiccoloSD64_Z2"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO64, BOARD_MODEL_REGISTERS_PICCOLO64,
		0x00000000, 0x00200000, 0x00400000, 0x00400000, CIRRUS_ID_CLGD5434, 2, 6, true
	},
	{
		_T("Piccolo SD64 Zorro III"), _T("Ingenieurbüro Helfrich"), _T("PiccoloSD64_Z3"),
		BOARD_MANUFACTURER_PICCOLO, BOARD_MODEL_MEMORY_PICCOLO64, BOARD_MODEL_REGISTERS_PICCOLO64,
		0x00000000, 0x00200000, 0x00400000, 0x04000000, CIRRUS_ID_CLGD5434, 3, 6, true
	},
	{
		_T("Spectrum 28/24 Zorro II"), _T("Great Valley Products"), _T("Spectrum28/24_Z2"),
		BOARD_MANUFACTURER_SPECTRUM, BOARD_MODEL_MEMORY_SPECTRUM, BOARD_MODEL_REGISTERS_SPECTRUM,
		0x00000000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5428, 2, 6, true
	},
	{
		_T("Spectrum 28/24 Zorro III"), _T("Great Valley Products"), _T("Spectrum28/24_Z3"),
		BOARD_MANUFACTURER_SPECTRUM, BOARD_MODEL_MEMORY_SPECTRUM, BOARD_MODEL_REGISTERS_SPECTRUM,
		0x00000000, 0x00100000, 0x00200000, 0x00200000, CIRRUS_ID_CLGD5428, 3, 6, true
	},
	{
		_T("Picasso IV Zorro II"), _T("Village Tronic"), _T("PicassoIV_Z2"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOIV, BOARD_MODEL_REGISTERS_PICASSOIV,
		0x00000000, 0x00400000, 0x00400000, 0x00400000, CIRRUS_ID_CLGD5446, 2, 2, false,
		ROMTYPE_PICASSOIV
	},
	{
		// REG:00600000 IO:00200000 VRAM:01000000
		_T("Picasso IV Zorro III"), _T("Village Tronic"), _T("PicassoIV_Z3"),
		BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOIV, 0,
		0x00000000, 0x00400000, 0x00400000, 0x04000000, CIRRUS_ID_CLGD5446, 3, 2, false,
		ROMTYPE_PICASSOIV
	},
	{
		_T("A2410"), _T("Commodore"), _T("A2410"),
		1030, 0, 0,
		0x00000000, 0x00200000, 0x00200000, 0x00000000, 0, 0, 2, false
	},
	{
		_T("x86 bridgeboard VGA"), _T("x86"), _T("VGA"),
		0, 0, 0,
		0x00000000, 0x00100000, 0x00100000, 0x00000000, CIRRUS_ID_CLGD5426, 0, 0, false,
		ROMTYPE_x86_VGA
	},
	{
		NULL
	}
};

static TCHAR memorybankname[40];
static TCHAR wbsmemorybankname[40];
static TCHAR lbsmemorybankname[40];
static TCHAR regbankname[40];

static int configured_mem, configured_regs;
static struct gfxboard *board;
static uae_u8 expamem_lo;
static uae_u8 *automemory;
static uae_u32 banksize_mask;

static uae_u8 picassoiv_bank, picassoiv_flifi;
static uae_u8 p4autoconfig[256];
static struct zfile *p4rom;
static bool p4z2;
static uae_u32 p4_mmiobase;
static uae_u32 p4_special_mask;
static uae_u32 p4_vram_bank[2];

static CirrusVGAState vga;
static uae_u8 *vram, *vramrealstart;
static int vram_start_offset;
static uae_u32 gfxboardmem_start;
static bool monswitch_current, monswitch_new;
static bool monswitch_reset;
static int monswitch_delay;
static int fullrefresh;
static bool modechanged;
static uae_u8 *gfxboard_surface, *fakesurface_surface;
static bool gfxboard_vblank;
static bool gfxboard_intena;
static bool vram_enabled, vram_offset_enabled;
static hwaddr vram_offset[2];
static uae_u8 cirrus_pci[0x44];
static uae_u8 p4_pci[0x44];
static int vga_width, vga_height;
static bool vga_refresh_active;
static bool vga_changed;

static uae_u32 vgaioregionptr, vgavramregionptr, vgabank0regionptr, vgabank1regionptr;

static const MemoryRegionOps *vgaio, *vgaram, *vgalowram, *vgammio;
static MemoryRegion vgaioregion, vgavramregion;

DECLARE_MEMORY_FUNCTIONS(gfxboard);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, mem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, mem_nojit);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, bsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, wbsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, lbsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, nbsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, regs);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboards, regs);

static addrbank gfxboard_bank_memory = {
	gfxboard_lget_mem, gfxboard_wget_mem, gfxboard_bget_mem,
	gfxboard_lput_mem, gfxboard_wput_mem, gfxboard_bput_mem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_mem, gfxboard_wget_mem,
	ABFLAG_RAM | ABFLAG_THREADSAFE, 0, 0
};

static addrbank gfxboard_bank_memory_nojit = {
	gfxboard_lget_mem_nojit, gfxboard_wget_mem_nojit, gfxboard_bget_mem_nojit,
	gfxboard_lput_mem_nojit, gfxboard_wput_mem_nojit, gfxboard_bput_mem_nojit,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_mem_nojit, gfxboard_wget_mem_nojit,
	ABFLAG_RAM | ABFLAG_THREADSAFE, S_READ, S_WRITE
};

static addrbank gfxboard_bank_wbsmemory = {
	gfxboard_lget_wbsmem, gfxboard_wget_wbsmem, gfxboard_bget_wbsmem,
	gfxboard_lput_wbsmem, gfxboard_wput_wbsmem, gfxboard_bput_wbsmem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_wbsmem, gfxboard_wget_wbsmem,
	ABFLAG_RAM | ABFLAG_THREADSAFE, S_READ, S_WRITE
};

static addrbank gfxboard_bank_lbsmemory = {
	gfxboard_lget_lbsmem, gfxboard_wget_lbsmem, gfxboard_bget_lbsmem,
	gfxboard_lput_lbsmem, gfxboard_wput_lbsmem, gfxboard_bput_lbsmem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_lbsmem, gfxboard_wget_lbsmem,
	ABFLAG_RAM | ABFLAG_THREADSAFE, S_READ, S_WRITE
};

static addrbank gfxboard_bank_nbsmemory = {
	gfxboard_lget_nbsmem, gfxboard_wget_nbsmem, gfxboard_bget_bsmem,
	gfxboard_lput_nbsmem, gfxboard_wput_nbsmem, gfxboard_bput_bsmem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, _T("Picasso IV banked VRAM"),
	gfxboard_lget_nbsmem, gfxboard_wget_nbsmem,
	ABFLAG_RAM | ABFLAG_THREADSAFE, S_READ, S_WRITE
};

static addrbank gfxboard_bank_registers = {
	gfxboard_lget_regs, gfxboard_wget_regs, gfxboard_bget_regs,
	gfxboard_lput_regs, gfxboard_wput_regs, gfxboard_bput_regs,
	default_xlate, default_check, NULL, NULL, NULL,
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

static addrbank gfxboard_bank_special = {
	gfxboards_lget_regs, gfxboards_wget_regs, gfxboards_bget_regs,
	gfxboards_lput_regs, gfxboards_wput_regs, gfxboards_bput_regs,
	default_xlate, default_check, NULL, NULL, _T("Picasso IV MISC"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

static void init_board (void)
{
	int vramsize = board->vrammax;

	vga_width = 0;
	mapped_free(&gfxmem_bank);
	vram_start_offset = 0;
	if (ISP4() && !p4z2) // JIT direct compatibility hack
		vram_start_offset = 0x01000000;
	vramsize += vram_start_offset;
	xfree (fakesurface_surface);
	fakesurface_surface = xmalloc (uae_u8, 4 * 10000);
	vram_offset[0] = vram_offset[1] = 0;
	vram_enabled = true;
	vram_offset_enabled = false;
	if (board->manufacturer) {
		gfxmem_bank.label = board->configtype == 3 ? _T("z3_gfx") : _T("z2_gfx");
	} else {
		gfxmem_bank.label = _T("ram_a8");
	}
	gfxmem_bank.allocated = vramsize;
	mapped_malloc (&gfxmem_bank);
	vram = gfxmem_bank.baseaddr;
	vramrealstart = vram;
	vram += vram_start_offset;
	gfxmem_bank.baseaddr = vram;
	gfxmem_bank.allocated = currprefs.rtgmem_size;
	vga.vga.vram_size_mb = currprefs.rtgmem_size >> 20;
	vgaioregion.opaque = &vgaioregionptr;
	vgavramregion.opaque = &vgavramregionptr;
	vga.vga.vram.opaque = &vgavramregionptr;
	vga_common_init(&vga.vga);
	cirrus_init_common(&vga, board->chiptype, 0,  NULL, NULL, board->manufacturer == 0);
	picasso_allocatewritewatch (currprefs.rtgmem_size);
}

static void vga_update_size(void)
{
	// this forces qemu_console_resize() call
	vga.vga.graphic_mode = -1;
	vga.vga.hw_ops->gfx_update(&vga);
}

static bool gfxboard_setmode(void)
{
	int bpp = vga.vga.get_bpp(&vga.vga);
	if (bpp == 0)
		bpp = 8;
	vga_update_size();
	if (vga_width <= 16 || vga_height <= 16)
		return false;
	picasso96_state.Width = vga_width;
	picasso96_state.Height = vga_height;
	picasso96_state.BytesPerPixel = bpp / 8;
	picasso96_state.RGBFormat = RGBFB_CLUT;
	write_log(_T("GFXBOARD %dx%dx%d\n"), vga_width, vga_height, bpp);
	gfx_set_picasso_modeinfo(vga_width, vga_height, bpp, RGBFB_NONE);
	fullrefresh = 2;
	vga_changed = false;
	return true;
}

bool gfxboard_toggle (int mode)
{
	if (currprefs.rtgmem_type == GFXBOARD_A2410) {
		return tms_toggle(mode);
	}

	if (vram == NULL)
		return false;
	if (monswitch_current) {
		vga_width = 0;
		monswitch_new = false;
		monswitch_delay = 1;
		picasso_requested_on = 0;
		return true;
	} else {
		vga_update_size();
		if (vga_width > 16 && vga_height > 16) {
			if (!gfxboard_setmode())
				return false;
			monswitch_new = true;
			monswitch_delay = 1;
			picasso_requested_on = 1;
			return true;
		}
	}
	return false;
}

static bool gfxboard_checkchanged (void)
{
	int bpp = vga.vga.get_bpp (&vga.vga);
	if (bpp == 0)
		bpp = 8;
	if (vga_width <= 16 || vga_height <= 16)
		return false;
	if (picasso96_state.Width != vga_width ||
		picasso96_state.Height != vga_height ||
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
	if (width != vga_width || vga_height != height)
		vga_changed = true;
	vga_width = width;
	vga_height = height;
}

void linear_memory_region_set_dirty(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
}

void vga_memory_region_set_dirty(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
	if (vga.vga.graphic_mode != 1)
		return;
	if (!fullrefresh)
		fullrefresh = 1;
}

#if 0
static uae_u8 pal64 (uae_u8 v)
{
	v = (v << 2) | ((v >> 2) & 3);
	return v;
}
#endif

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
	if (s == &fakesurface || !vga_refresh_active)
		return 0;
	if (gfxboard_surface == NULL)
		gfxboard_surface = gfx_lock_picasso (false, false);
	return picasso_vidinfo.rowbytes;
}
uint8_t *surface_data(DisplaySurface *s)
{
	if (vga_changed)
		return NULL;
	if (s == &fakesurface || !vga_refresh_active)
		return fakesurface_surface;
	if (gfxboard_surface == NULL)
		gfxboard_surface = gfx_lock_picasso (false, false);
	return gfxboard_surface;
}

void gfxboard_refresh (void)
{
	fullrefresh = 2;
}

void gfxboard_hsync_handler(void)
{
	if (currprefs.rtgmem_type == GFXBOARD_A2410) {
		tms_hsync_handler();
	}
}

void gfxboard_vsync_handler (void)
{
	if (currprefs.rtgmem_type == GFXBOARD_A2410) {
		tms_vsync_handler();
		return;
	}

	if (!configured_mem || !configured_regs)
		return;

	if (monswitch_current && (modechanged || gfxboard_checkchanged ())) {
		modechanged = false;
		if (!gfxboard_setmode ()) {
			picasso_requested_on = 0;
			return;
		}
		init_hz_p96 ();
		picasso_requested_on = 1;
		return;
	}

	if (monswitch_new != monswitch_current) {
		if (monswitch_delay > 0)
			monswitch_delay--;
		if (monswitch_delay == 0) {
			if (!monswitch_new)
				picasso_requested_on = 0;
			monswitch_current = monswitch_new;
			vga_update_size();
			write_log (_T("GFXBOARD ACTIVE=%d\n"), monswitch_current);
		}
	} else {
		monswitch_delay = 0;
	}

	if (!monswitch_delay && monswitch_current && picasso_on && picasso_requested_on && !vga_changed) {
		picasso_getwritewatch (vram_start_offset);
		if (fullrefresh)
			vga.vga.graphic_mode = -1;
		vga_refresh_active = true;
		vga.vga.hw_ops->gfx_update(&vga);
		vga_refresh_active = false;
	}

	if (picasso_on && !vga_changed) {
		if (currprefs.leds_on_screen & STATUSLINE_RTG) {
			if (gfxboard_surface == NULL) {
				gfxboard_surface = gfx_lock_picasso (false, false);
			}
			if (gfxboard_surface) {
				if (!(currprefs.leds_on_screen & STATUSLINE_TARGET))
					picasso_statusline (gfxboard_surface);
			}
		}
		if (fullrefresh > 0)
			fullrefresh--;
	}

	if (gfxboard_surface)
		gfx_unlock_picasso (true);
	gfxboard_surface = NULL;

	// Vertical Sync End Register, 0x20 = Disable Vertical Interrupt, 0x10 = Clear Vertical Interrupt.
	if (board->irq) {
		if ((!(vga.vga.cr[0x11] & 0x20) && (vga.vga.cr[0x11] & 0x10) && !(vga.vga.gr[0x17] & 4))) {
			if (gfxboard_intena) {
				gfxboard_vblank = true;
				if (board->irq == 2)
					INTREQ (0x8000 | 0x0008);
				else
					INTREQ (0x8000 | 0x2000);
			}
		}
	}

}

double gfxboard_get_vsync (void)
{
	return vblank_hz; // FIXME
}

void dpy_gfx_update(QemuConsole *con, int x, int y, int w, int h)
{
	picasso_invalidate (x, y, w, h);
}

void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size)
{
	if (!stricmp(name, "vga.bank0")) {
		mr->opaque = &vgabank0regionptr;
	} else if (!stricmp(name, "vga.bank1")) {
		mr->opaque = &vgabank1regionptr;
	}
}

static void jit_reset (void)
{
#ifdef JIT
	if (currprefs.cachesize && (!currprefs.comptrustbyte || !currprefs.comptrustword || !currprefs.comptrustlong)) {
		flush_icache (0, 3);
	}
#endif
}

static void remap_vram (hwaddr offset0, hwaddr offset1, bool enabled)
{
	jit_reset ();
	vram_offset[0] = offset0;
	vram_offset[1] = offset1;
#if VRAMLOG
	if (vram_enabled != enabled)
		write_log (_T("VRAM state=%d\n"), enabled);
	bool was_vram_offset_enabled = vram_offset_enabled;
#endif
	vram_enabled = enabled && (vga.vga.sr[0x07] & 0x01);
#if 0
	vram_enabled = false;
#endif
	// offset==0 and offset1==0x8000: linear vram mapping
	vram_offset_enabled = offset0 != 0 || offset1 != 0x8000;
#if VRAMLOG
	if (vram_offset_enabled || was_vram_offset_enabled)
		write_log (_T("VRAM offset %08x and %08x\n"), offset0, offset1);
#endif
}

void memory_region_set_alias_offset(MemoryRegion *mr,
                                    hwaddr offset)
{
	if (mr->opaque == &vgabank0regionptr) {
		if (offset != vram_offset[0]) {
			//write_log (_T("vgavramregion0 %08x\n"), offset);
			remap_vram (offset, vram_offset[1], vram_enabled);
		}
	} else if (mr->opaque == &vgabank1regionptr) {
		if (offset != vram_offset[1]) {
			//write_log (_T("vgavramregion1 %08x\n"), offset);
			remap_vram (vram_offset[0], offset, vram_enabled);
		}
	} else if (mr->opaque == &vgaioregionptr) {
		write_log (_T("vgaioregion %d\n"), offset);
	} else {
		write_log (_T("unknown region %d\n"), offset);
	}

}
void memory_region_set_enabled(MemoryRegion *mr, bool enabled)
{
	if (mr->opaque == &vgabank0regionptr || mr->opaque == &vgabank1regionptr) {
		if (enabled != vram_enabled)  {
			//write_log (_T("enable vgavramregion %d\n"), enabled);
			remap_vram (vram_offset[0], vram_offset[1], enabled);
		}
	} else if (mr->opaque == &vgaioregionptr) {
		write_log (_T("enable vgaioregion %d\n"), enabled);
	} else {
		write_log (_T("enable unknown region %d\n"), enabled);
	}
}
void memory_region_reset_dirty(MemoryRegion *mr, hwaddr addr,
                               hwaddr size, unsigned client)
{
	//write_log (_T("memory_region_reset_dirty %08x %08x\n"), addr, size);
}
bool memory_region_get_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size, unsigned client)
{
	if (mr->opaque != &vgavramregionptr)
		return false;
	//write_log (_T("memory_region_get_dirty %08x %08x\n"), addr, size);
	if (fullrefresh)
		return true;
	return picasso_is_vram_dirty (addr + gfxmem_bank.start, size);
}

static QEMUResetHandler *reset_func;
static void *reset_parm;
void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
	reset_func = func;
	reset_parm = opaque;
	reset_func (reset_parm);
}

static void p4_pci_check (void)
{
	uaecptr b0, b1;

	b0 = p4_pci[0x10 + 2] << 16;
	b1 = p4_pci[0x14 + 2] << 16;

	p4_vram_bank[0] = b0;
	p4_vram_bank[1] = b1;
#if PICASSOIV_DEBUG_IO
	write_log (_T("%08X %08X\n"), p4_vram_bank[0], p4_vram_bank[1]);
#endif
}

static void reset_pci (void)
{
	cirrus_pci[0] = 0x00;
	cirrus_pci[1] = 0xb8;
	cirrus_pci[2] = 0x10;
	cirrus_pci[3] = 0x13;

	cirrus_pci[4] = 2;
	cirrus_pci[5] = 0;
	cirrus_pci[6] = 0;
	cirrus_pci[7] &= ~(1 | 2 | 32);

	cirrus_pci[8] = 3;
	cirrus_pci[9] = 0;
	cirrus_pci[10] = 0;
	cirrus_pci[11] = 68;

	cirrus_pci[0x10] &= ~1; // B revision
	cirrus_pci[0x13] &= ~1; // memory
}

static void set_monswitch(bool newval)
{
	if (monswitch_new == newval)
		return;
	monswitch_new = newval; 
	monswitch_delay = MONITOR_SWITCH_DELAY;
}

static void picassoiv_checkswitch (void)
{
	if (ISP4()) {
		bool rtg_active = (picassoiv_flifi & 1) == 0 || (vga.vga.cr[0x51] & 8) == 0;
		// do not switch to P4 RTG until monitor switch is set to native at least
		// once after reset.
		if (monswitch_reset && rtg_active)
			return;
		monswitch_reset = false;
		set_monswitch(rtg_active);
	}
}

static void bput_regtest (uaecptr addr, uae_u8 v)
{
	addr += 0x3b0;
	if (addr == 0x3d5) { // CRxx
		if (vga.vga.cr_index == 0x11) {
			if (!(vga.vga.cr[0x11] & 0x10)) {
				gfxboard_vblank = false;
			}
		}
	}
	if (!(vga.vga.sr[0x07] & 0x01) && vram_enabled) {
		remap_vram (vram_offset[0], vram_offset[1], false);
	}
	picassoiv_checkswitch ();
}

static uae_u8 bget_regtest (uaecptr addr, uae_u8 v)
{
	addr += 0x3b0;
	// Input Status 0
	if (addr == 0x3c2) {
		if (gfxboard_vblank) {
			// Disable Vertical Interrupt == 0?
			// Clear Vertical Interrupt == 1
			// GR17 bit 2 = INTR disable
			if (!(vga.vga.cr[0x11] & 0x20) && (vga.vga.cr[0x11] & 0x10) && !(vga.vga.gr[0x17] & 4)) {
				v |= 0x80; // VGA Interrupt Pending
			}
		}
		v |= 0x10; // DAC sensing
	}
	if (addr == 0x3c5) {
		if (vga.vga.sr_index == 8) {
			// TODO: DDC
		}
	}
	return v;
}

void vga_io_put(int portnum, uae_u8 v)
{
	if (!vgaio)
		return;
	portnum -= 0x3b0;
	bput_regtest(portnum, v);
	vgaio->write(&vga, portnum, v, 1);
}
uae_u8 vga_io_get(int portnum)
{
	uae_u8 v = 0xff;
	if (!vgaio)
		return v;
	portnum -= 0x3b0;
	v = vgaio->read(&vga, portnum, 1);
	v = bget_regtest(portnum, v);
	return v;
}
void vga_ram_put(int offset, uae_u8 v)
{
	if (!vgalowram)
		return;
	offset -= 0xa0000;
	vgalowram->write(&vga, offset, v, 1);
}
uae_u8 vga_ram_get(int offset)
{
	if (!vgalowram)
		return 0xff;
	offset -= 0xa0000;
	return vgalowram->read(&vga, offset, 1);
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
	if (mr->opaque == &vgavramregionptr)
		return vram;
	return NULL;
}

void memory_region_init_ram(MemoryRegion *mr,
                            const char *name,
                            uint64_t size)
{
	if (!stricmp (name, "vga.vram")) {
		vgavramregion.opaque = mr->opaque;
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
		mr->opaque = vgaioregion.opaque;
	} else if (!stricmp (name, "cirrus-linear-io")) {
		vgaram = ops;
	} else if (!stricmp (name, "cirrus-low-memory")) {
		vgalowram = ops;
	} else if (!stricmp (name, "cirrus-mmio")) {
		vgammio = ops;
	}
}

int is_surface_bgr(DisplaySurface *surface)
{
	return board->swap;
}

static uaecptr fixaddr_bs (uaecptr addr, int mask, int *bs)
{
	bool swapped = false;
	addr &= gfxmem_bank.mask;
	if (p4z2) {
		if (addr < 0x200000) {
			addr |= p4_vram_bank[0];
			if (addr >= 0x400000 && addr < 0x600000) {
				*bs = BYTESWAP_WORD;
				swapped = true;
			} else if (addr >= 0x800000 && addr < 0xa00000) {
				*bs = BYTESWAP_LONG;
				swapped = true;
			}
		} else {
			addr |= p4_vram_bank[1];
			if (addr >= 0x600000 && addr < 0x800000) {
				*bs = BYTESWAP_WORD;
				swapped = true;
			} else if (addr >= 0xa00000 && addr < 0xc00000) {
				*bs = BYTESWAP_LONG;
				swapped = true;
			}
		}
	}
#ifdef JIT
	if (mask && (vram_offset_enabled || !vram_enabled || swapped || p4z2))
		special_mem |= mask;
#endif
	if (vram_offset_enabled) {
		if (addr & 0x8000) {
			addr += vram_offset[1] & ~0x8000;
		} else {
			addr += vram_offset[0];
		}
	}
	addr &= gfxmem_bank.mask;
	return addr;
}

STATIC_INLINE uaecptr fixaddr (uaecptr addr, int mask)
{
#ifdef JIT
	if (mask && (vram_offset_enabled || !vram_enabled))
		special_mem |= mask;
#endif
	if (vram_offset_enabled) {
		if (addr & 0x8000) {
			addr += vram_offset[1] & ~0x8000;
		} else {
			addr += vram_offset[0];
		}
	}
	addr &= gfxmem_bank.mask;
	return addr;
}

STATIC_INLINE uaecptr fixaddr (uaecptr addr)
{
	if (vram_offset_enabled) {
		if (addr & 0x8000) {
			addr += vram_offset[1] & ~0x8000;
		} else {
			addr += vram_offset[0];
		}
	}
	addr &= gfxmem_bank.mask;
	return addr;
}

STATIC_INLINE const MemoryRegionOps *getvgabank (uaecptr *paddr)
{
	uaecptr addr = *paddr;
	addr &= gfxmem_bank.mask;
	*paddr = addr;
	return vgaram;
}

static uae_u32 gfxboard_lget_vram (uaecptr addr, int bs)
{
	uae_u32 v;
	if (!vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (&addr);
		addr &= gfxmem_bank.mask;
		if (bs < 0) { // WORD
			v  = bank->read (&vga, addr + 1, 1) << 24;
			v |= bank->read (&vga, addr + 0, 1) << 16;
			v |= bank->read (&vga, addr + 3, 1) <<  8;
			v |= bank->read (&vga, addr + 2, 1) <<  0;
		} else if (bs > 0) { // LONG
			v  = bank->read (&vga, addr + 3, 1) << 24;
			v |= bank->read (&vga, addr + 2, 1) << 16;
			v |= bank->read (&vga, addr + 1, 1) <<  8;
			v |= bank->read (&vga, addr + 0, 1) <<  0;
		} else {
			v  = bank->read (&vga, addr + 0, 1) << 24;
			v |= bank->read (&vga, addr + 1, 1) << 16;
			v |= bank->read (&vga, addr + 2, 1) <<  8;
			v |= bank->read (&vga, addr + 3, 1) <<  0;
		}
	} else {
		uae_u8 *m = vram + addr;
		if (bs < 0) {
			v  = (*((uae_u16*)(m + 0))) << 16;
			v |= (*((uae_u16*)(m + 2))) << 0;
		} else if (bs > 0) {
			v = *((uae_u32*)m);
		} else {
			v = do_get_mem_long ((uae_u32*)m);
		}
	}
#if MEMLOGR
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogr)
	write_log (_T("R %08X L %08X BS=%d EN=%d\n"), addr, v, bs, vram_enabled);
#endif
	return v;
}
static uae_u16 gfxboard_wget_vram (uaecptr addr, int bs)
{
	uae_u32 v;
	if (!vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (&addr);
		if (bs) {
			v  = bank->read (&vga, addr + 0, 1) <<  0;
			v |= bank->read (&vga, addr + 1, 1) <<  8;
		} else {
			v  = bank->read (&vga, addr + 0, 1) <<  8;
			v |= bank->read (&vga, addr + 1, 1) <<  0;
		}
	} else {
		uae_u8 *m = vram + addr;
		if (bs)
			v = *((uae_u16*)m);
		else
			v = do_get_mem_word ((uae_u16*)m);
	}
#if MEMLOGR
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogr)
	write_log (_T("R %08X W %08X BS=%d EN=%d\n"), addr, v, bs, vram_enabled);
#endif
	return v;
}
static uae_u8 gfxboard_bget_vram (uaecptr addr, int bs)
{
	uae_u32 v;
	if (!vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (&addr);
		if (bs)
			v = bank->read (&vga, addr ^ 1, 1);
		else
			v = bank->read (&vga, addr + 0, 1);
	} else {
		if (bs)
			v = vram[addr ^ 1];
		else
			v = vram[addr];
	}
#if MEMLOGR
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogr)
	write_log (_T("R %08X B %08X BS=0 EN=%d\n"), addr, v, vram_enabled);
#endif
	return v;
}

static void gfxboard_lput_vram (uaecptr addr, uae_u32 l, int bs)
{
#if MEMDEBUG
	if ((addr & MEMDEBUGMASK) >= MEMDEBUGTEST && l)
		write_log (_T("%08X L %08X\n"), addr, l);
#endif
#if MEMLOGW
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogw)
	write_log (_T("W %08X L %08X\n"), addr, l);
#endif
	if (!vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (&addr);
		if (bs < 0) { // WORD
			bank->write (&vga, addr + 1, l >> 24, 1);
			bank->write (&vga, addr + 0, (l >> 16) & 0xff, 1);
			bank->write (&vga, addr + 3, (l >> 8) & 0xff, 1);
			bank->write (&vga, addr + 2, (l >> 0) & 0xff, 1);
		} else if (bs > 0) { // LONG
			bank->write (&vga, addr + 3, l >> 24, 1);
			bank->write (&vga, addr + 2, (l >> 16) & 0xff, 1);
			bank->write (&vga, addr + 1, (l >> 8) & 0xff, 1);
			bank->write (&vga, addr + 0, (l >> 0) & 0xff, 1);
		} else {
			bank->write (&vga, addr + 0, l >> 24, 1);
			bank->write (&vga, addr + 1, (l >> 16) & 0xff, 1);
			bank->write (&vga, addr + 2, (l >> 8) & 0xff, 1);
			bank->write (&vga, addr + 3, (l >> 0) & 0xff, 1);
		}
	} else {
		uae_u8 *m = vram + addr;
		if (bs < 0) {
			*((uae_u16*)(m + 0)) = l >> 16;
			*((uae_u16*)(m + 2)) = l >>  0;
		} else if (bs > 0) {
			*((uae_u32*)m) = l;
		} else {
			do_put_mem_long ((uae_u32*) m, l);
		}
	}
}
static void gfxboard_wput_vram (uaecptr addr, uae_u16 w, int bs)
{
#if MEMDEBUG
	if ((addr & MEMDEBUGMASK) >= MEMDEBUGTEST && w)
		write_log (_T("%08X W %04X\n"), addr, w & 0xffff);
#endif
#if MEMLOGW
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogw)
	write_log (_T("W %08X W %04X\n"), addr, w & 0xffff);
#endif
	if (!vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (&addr);
		if (bs) {
			bank->write (&vga, addr + 0, (w >> 0) & 0xff, 1);
			bank->write (&vga, addr + 1, w >> 8, 1);
		} else {
			bank->write (&vga, addr + 0, w >> 8, 1);
			bank->write (&vga, addr + 1, (w >> 0) & 0xff, 1);
		}
	} else {
		uae_u8 *m = vram + addr;
		if (bs)
			*((uae_u16*)m) = w;
		else
			do_put_mem_word ((uae_u16*)m, w);
	}
}
static void gfxboard_bput_vram (uaecptr addr, uae_u8 b, int bs)
{
#if MEMDEBUG
	if ((addr & MEMDEBUGMASK) >= MEMDEBUGTEST && b)
		write_log (_T("%08X B %02X\n"), addr, b & 0xff);
#endif
#if MEMLOGW
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogw)
	write_log (_T("W %08X B %02X\n"), addr, b & 0xff);
#endif
	if (!vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (&addr);
#ifdef JIT
		special_mem |= S_WRITE;
#endif
		if (bs)
			bank->write (&vga, addr ^ 1, b, 1);
		else
			bank->write (&vga, addr, b, 1);
	} else {
		if (bs)
			vram[addr ^ 1] = b;
		else
			vram[addr] = b;
	}
}

// LONG byteswapped VRAM
static uae_u32 REGPARAM2 gfxboard_lget_lbsmem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (addr, BYTESWAP_LONG);
}
static uae_u32 REGPARAM2 gfxboard_wget_lbsmem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (addr, BYTESWAP_LONG);
}
static uae_u32 REGPARAM2 gfxboard_bget_lbsmem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (addr, BYTESWAP_LONG);
}

static void REGPARAM2 gfxboard_lput_lbsmem (uaecptr addr, uae_u32 l)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return;
	gfxboard_lput_vram (addr, l, BYTESWAP_LONG);
}
static void REGPARAM2 gfxboard_wput_lbsmem (uaecptr addr, uae_u32 w)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return;
	gfxboard_wput_vram (addr, w, BYTESWAP_LONG);
}
static void REGPARAM2 gfxboard_bput_lbsmem (uaecptr addr, uae_u32 w)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return;
	gfxboard_bput_vram (addr, w, BYTESWAP_LONG);
}

// WORD byteswapped VRAM
static uae_u32 REGPARAM2 gfxboard_lget_wbsmem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (addr, BYTESWAP_WORD);
}
static uae_u32 REGPARAM2 gfxboard_wget_wbsmem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (addr, BYTESWAP_WORD);
}
static uae_u32 REGPARAM2 gfxboard_bget_wbsmem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (addr, BYTESWAP_WORD);
}

static void REGPARAM2 gfxboard_lput_wbsmem (uaecptr addr, uae_u32 l)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return;
	gfxboard_lput_vram (addr, l, BYTESWAP_WORD);
}
static void REGPARAM2 gfxboard_wput_wbsmem (uaecptr addr, uae_u32 w)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return;
	gfxboard_wput_vram (addr, w, BYTESWAP_WORD);
}
static void REGPARAM2 gfxboard_bput_wbsmem (uaecptr addr, uae_u32 w)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return;
	gfxboard_bput_vram (addr, w, BYTESWAP_WORD);
}

// normal or byteswapped (banked) vram
static uae_u32 REGPARAM2 gfxboard_lget_nbsmem (uaecptr addr)
{
	int bs = 0;
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr_bs (addr, 0, &bs);
	if (addr == -1)
		return 0;
//	activate_debugger();
	return gfxboard_lget_vram (addr, bs);
}
static uae_u32 REGPARAM2 gfxboard_wget_nbsmem (uaecptr addr)
{
	int bs = 0;
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr_bs (addr, 0, &bs);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (addr, bs);
}

static void REGPARAM2 gfxboard_lput_nbsmem (uaecptr addr, uae_u32 l)
{
	int bs = 0;
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr_bs (addr, 0, &bs);
	if (addr == -1)
		return;
	gfxboard_lput_vram (addr, l, bs);
}
static void REGPARAM2 gfxboard_wput_nbsmem (uaecptr addr, uae_u32 w)
{
	int bs = 0;
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr_bs (addr, 0, &bs);
	if (addr == -1)
		return;
	gfxboard_wput_vram (addr, w, bs);
}

static uae_u32 REGPARAM2 gfxboard_bget_bsmem (uaecptr addr)
{
	int bs = 0;
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (addr, bs);
}
static void REGPARAM2 gfxboard_bput_bsmem (uaecptr addr, uae_u32 b)
{
	int bs = 0;
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr_bs (addr, 0, &bs);
	if (addr == -1)
		return;
	gfxboard_bput_vram (addr, b, bs);
}

// normal vram
static uae_u32 REGPARAM2 gfxboard_lget_mem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, S_READ);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_wget_mem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, S_READ);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_bget_mem (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, S_READ);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (addr, 0);
}
static void REGPARAM2 gfxboard_lput_mem (uaecptr addr, uae_u32 l)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, S_WRITE);
	if (addr == -1)
		return;
	gfxboard_lput_vram (addr, l, 0);
}
static void REGPARAM2 gfxboard_wput_mem (uaecptr addr, uae_u32 w)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, S_WRITE);
	if (addr == -1)
		return;
	gfxboard_wput_vram (addr, w, 0);
}
static void REGPARAM2 gfxboard_bput_mem (uaecptr addr, uae_u32 b)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr, S_WRITE);
	if (addr == -1)
		return;
	gfxboard_bput_vram (addr, b, 0);
}

// normal vram, no jit direct
static uae_u32 REGPARAM2 gfxboard_lget_mem_nojit (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_wget_mem_nojit (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_bget_mem_nojit (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (addr, 0);
}
static void REGPARAM2 gfxboard_lput_mem_nojit (uaecptr addr, uae_u32 l)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr);
	if (addr == -1)
		return;
	gfxboard_lput_vram (addr, l, 0);
}
static void REGPARAM2 gfxboard_wput_mem_nojit (uaecptr addr, uae_u32 w)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr);
	if (addr == -1)
		return;
	gfxboard_wput_vram (addr, w, 0);
}
static void REGPARAM2 gfxboard_bput_mem_nojit (uaecptr addr, uae_u32 b)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr = fixaddr (addr);
	if (addr == -1)
		return;
	gfxboard_bput_vram (addr, b, 0);
}

static int REGPARAM2 gfxboard_check (uaecptr addr, uae_u32 size)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr &= gfxmem_bank.mask;
	return (addr + size) <= currprefs.rtgmem_size;
}
static uae_u8 *REGPARAM2 gfxboard_xlate (uaecptr addr)
{
	addr -= gfxboardmem_start & gfxmem_bank.mask;
	addr &= gfxmem_bank.mask;
	return vram + addr;
}

static uae_u32 REGPARAM2 gfxboard_bget_mem_autoconfig (uaecptr addr)
{
	uae_u32 v = 0;
	addr &= 65535;
	if (addr < GFXBOARD_AUTOCONFIG_SIZE)
		v = automemory[addr];
	return v;
}

static void REGPARAM2 gfxboard_wput_mem_autoconfig (uaecptr addr, uae_u32 b)
{
	if (board->configtype == 2)
		return;
	b &= 0xffff;
	addr &= 65535;
	if (addr == 0x44) {
		uae_u32 start;
		if (!expamem_z3hack(&currprefs)) {
			start = (b & 0xff00) | expamem_lo;
			gfxmem_bank.start = start << 16;
		}
		gfxboard_bank_memory.bget = gfxboard_bget_mem;
		gfxboard_bank_memory.bput = gfxboard_bput_mem;
		gfxboard_bank_memory.wput = gfxboard_wput_mem;
		init_board ();
		if (ISP4()) {
			if (validate_banks_z3(&gfxboard_bank_memory, gfxmem_bank.start >> 16, expamem_z3_size >> 16)) {
				// main vram
				map_banks_z3(&gfxboard_bank_memory, (gfxmem_bank.start + PICASSOIV_VRAM1) >> 16, 0x400000 >> 16);
				map_banks_z3(&gfxboard_bank_wbsmemory, (gfxmem_bank.start + PICASSOIV_VRAM1 + 0x400000) >> 16, 0x400000 >> 16);
				// secondary
				map_banks_z3(&gfxboard_bank_memory_nojit, (gfxmem_bank.start + PICASSOIV_VRAM2) >> 16, 0x400000 >> 16);
				map_banks_z3(&gfxboard_bank_wbsmemory, (gfxmem_bank.start + PICASSOIV_VRAM2 + 0x400000) >> 16, 0x400000 >> 16);
				// regs
				map_banks_z3(&gfxboard_bank_registers, (gfxmem_bank.start + PICASSOIV_REG) >> 16, 0x200000 >> 16);
				map_banks_z3(&gfxboard_bank_special, gfxmem_bank.start >> 16, PICASSOIV_REG >> 16);
			}
			picassoiv_bank = 0;
			picassoiv_flifi = 1;
			configured_regs = gfxmem_bank.start >> 16;
		} else {
			map_banks_z3(&gfxboard_bank_memory, gfxmem_bank.start >> 16, board->banksize >> 16);
		}
		configured_mem = gfxmem_bank.start >> 16;
		gfxboardmem_start = gfxmem_bank.start;
		expamem_next (&gfxboard_bank_memory, NULL);
		return;
	}
	if (addr == 0x4c) {
		configured_mem = 0xff;
		expamem_shutup(&gfxboard_bank_memory);
		return;
	}
}

static void REGPARAM2 gfxboard_bput_mem_autoconfig (uaecptr addr, uae_u32 b)
{
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48) {
		if (board->configtype == 2) {
			addrbank *ab;
			if (ISP4()) {
				ab = &gfxboard_bank_nbsmemory;
				if (configured_mem == 0)
					init_board ();
				map_banks_z2 (ab, b, 0x00200000 >> 16);
				if (configured_mem == 0) {
					configured_mem = b;
					gfxboardmem_start = b << 16;
				} else {
					gfxboard_bank_memory.bget = gfxboard_bget_mem;
					gfxboard_bank_memory.bput = gfxboard_bput_mem;
				}
			} else {
				ab = &gfxboard_bank_memory;
				gfxboard_bank_memory.bget = gfxboard_bget_mem;
				gfxboard_bank_memory.bput = gfxboard_bput_mem;
				init_board ();
				map_banks_z2 (ab, b, board->banksize >> 16);
				configured_mem = b;
				gfxboardmem_start = b << 16;
			}
			expamem_next (ab, NULL);
		} else {
			expamem_lo = b & 0xff;
		}
		return;
	}
	if (addr == 0x4c) {
		configured_mem = 0xff;
		expamem_shutup(&gfxboard_bank_memory);
		return;
	}
}

static uaecptr mungeaddr (uaecptr addr, bool write)
{
	addr &= 65535;
	if (addr >= 0x2000) {
		if (addr == 0x46e8) {
			// wakeup register
			return 0;
		}
		write_log (_T("GFXBOARD: %c unknown IO address %x\n"), write ? 'W' : 'R', addr);
		return 0;
	}
	if (addr >= 0x1000) {
		if (board->manufacturer == BOARD_MANUFACTURER_PICASSO) {
			if (addr == 0x1001) {
				gfxboard_intena = true;
				return 0;
			}
			if (addr == 0x1000) {
				gfxboard_intena = false;
				return 0;
			}
		}
		if ((addr & 0xfff) < 0x3b0) {
			write_log (_T("GFXBOARD: %c unknown IO address %x\n"), write ? 'W' : 'R', addr);
			return 0;
		}
		addr++;
	}
	addr &= 0x0fff;
	if (addr == 0x102) {
		// POS102
		return 0;
	}
	if (addr < 0x3b0) {
		write_log (_T("GFXBOARD: %c unknown IO address %x\n"), write ? 'W' : 'R', addr);
		return 0;
	}
	addr -= 0x3b0;
	return addr;
}

static uae_u32 REGPARAM2 gfxboard_lget_regs (uaecptr addr)
{
	uae_u32 v = 0xffffffff;
	addr = mungeaddr (addr, false);
	if (addr)
		v = vgaio->read (&vga, addr, 4);
	return v;
}
static uae_u32 REGPARAM2 gfxboard_wget_regs (uaecptr addr)
{
	uae_u16 v = 0xffff;
	addr = mungeaddr (addr, false);
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
	addr &= 65535;
	if (addr >= 0x8000) {
		write_log (_T("GFX SPECIAL BGET IO %08X\n"), addr);
		return 0;
	}
	addr = mungeaddr (addr, false);
	if (addr) {
		v = vgaio->read (&vga, addr, 1);
		v = bget_regtest (addr, v);
	}
	return v;
}

static void REGPARAM2 gfxboard_lput_regs (uaecptr addr, uae_u32 l)
{
	//write_log (_T("GFX LONG PUT IO %04X = %04X\n"), addr & 65535, l);
	addr = mungeaddr (addr, true);
	if (addr) {
		vgaio->write (&vga, addr + 0, l >> 24, 1);
		bput_regtest (addr + 0, (l >> 24));
		vgaio->write (&vga, addr + 1, (l >> 16) & 0xff, 1);
		bput_regtest (addr + 0, (l >> 16));
		vgaio->write (&vga, addr + 2, (l >>  8) & 0xff, 1);
		bput_regtest (addr + 0, (l >>  8));
		vgaio->write (&vga, addr + 3, (l >>  0) & 0xff, 1);
		bput_regtest (addr + 0, (l >>  0));
	}
}
static void REGPARAM2 gfxboard_wput_regs (uaecptr addr, uae_u32 w)
{
	//write_log (_T("GFX WORD PUT IO %04X = %04X\n"), addr & 65535, w & 0xffff);
	addr = mungeaddr (addr, true);
	if (addr) {
		vgaio->write (&vga, addr + 0, (w >> 8) & 0xff, 1);
		bput_regtest (addr + 0, (w >> 8));
		vgaio->write (&vga, addr + 1, (w >> 0) & 0xff, 1);
		bput_regtest (addr + 1, (w >> 0));
	}
}
static void REGPARAM2 gfxboard_bput_regs (uaecptr addr, uae_u32 b)
{
	//write_log (_T("GFX BYTE PUT IO %04X = %02X\n"), addr & 65535, b & 0xff);
	addr &= 65535;
	if (addr >= 0x8000) {
		write_log (_T("GFX SPECIAL BPUT IO %08X = %02X\n"), addr, b & 0xff);
		switch (board->manufacturer)
		{
		case BOARD_MANUFACTURER_PICASSO:
			{
				if ((addr & 1) == 0) {
					int idx = addr >> 12;
					if (idx == 0x0b || idx == 0x09) {
						set_monswitch(false);
					} else if (idx == 0x0a || idx == 0x08) {
						set_monswitch(true);
					}
				}
			}
		break;
		case BOARD_MANUFACTURER_PICCOLO:
		case BOARD_MANUFACTURER_SPECTRUM:
			set_monswitch((b & 0x20) != 0);
			gfxboard_intena = (b & 0x40) != 0;
		break;
		}
		return;
	}
	addr = mungeaddr (addr, true);
	if (addr) {
		vgaio->write (&vga, addr, b & 0xff, 1);
		bput_regtest (addr, b);
	}
}

static uae_u32 REGPARAM2 gfxboard_bget_regs_autoconfig (uaecptr addr)
{
	uae_u32 v = 0;
	addr &= 65535;
	if (addr < GFXBOARD_AUTOCONFIG_SIZE)
		v = automemory[addr];
	return v;
}

static void REGPARAM2 gfxboard_bput_regs_autoconfig (uaecptr addr, uae_u32 b)
{
	addrbank *ab;
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48) {
		gfxboard_bank_registers.bget = gfxboard_bget_regs;
		gfxboard_bank_registers.bput = gfxboard_bput_regs;
		if (p4z2) {
			ab = &gfxboard_bank_special;
			map_banks_z2(ab, b, gfxboard_bank_special.allocated >> 16);
		} else {
			ab = &gfxboard_bank_registers;
			map_banks_z2(ab, b, gfxboard_bank_registers.allocated >> 16);
		}
		configured_regs = b;
		expamem_next (ab, NULL);
		return;
	}
	if (addr == 0x4c) {
		configured_regs = 0xff;
		expamem_next (NULL, NULL);
		return;
	}
}

void gfxboard_free(void)
{
	if (currprefs.rtgmem_type == GFXBOARD_A2410) {
		tms_free();
		return;
	}
	if (vram) {
		gfxmem_bank.baseaddr = vramrealstart;
		mapped_free (&gfxmem_bank);
	}
	vram = NULL;
	vramrealstart = NULL;
	xfree (fakesurface_surface);
	fakesurface_surface = NULL;
	configured_mem = 0;
	configured_regs = 0;
	monswitch_new = false;
	monswitch_current = false;
	monswitch_delay = -1;
	monswitch_reset = true;
	modechanged = false;
	gfxboard_vblank = false;
	gfxboard_intena = false;
	picassoiv_bank = 0;
}

void gfxboard_reset (void)
{
	if (currprefs.rtgmem_type == GFXBOARD_A2410) {
		tms_reset();
		return;
	}
	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE) {
		board = &boards[currprefs.rtgmem_type - GFXBOARD_HARDWARE];
		gfxmem_bank.mask = currprefs.rtgmem_size - 1;
	}
	gfxboard_free();
	if (board) {
		if (board->configtype == 3)
			gfxboard_bank_memory.wput = gfxboard_wput_mem_autoconfig;
		if (reset_func) 
			reset_func (reset_parm);
	}
}

static uae_u32 REGPARAM2 gfxboards_lget_regs (uaecptr addr)
{
	uae_u32 v = 0;
	addr &= p4_special_mask;
	// pci config
	if (addr >= 0x400000 || (p4z2 && !(picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			v =  p4_pci[addr2 + 0] << 24;
			v |= p4_pci[addr2 + 1] << 16;
			v |= p4_pci[addr2 + 2] <<  8;
			v |= p4_pci[addr2 + 3] <<  0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI LGET %08x %08x\n"), addr, v);
#endif
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
			v =  cirrus_pci[addr2 + 0] << 24;
			v |= cirrus_pci[addr2 + 1] << 16;
			v |= cirrus_pci[addr2 + 2] <<  8;
			v |= cirrus_pci[addr2 + 3] <<  0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI LGET %08x %08x\n"), addr, v);
#endif
		}
		return v;
	}
	if (picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= p4_mmiobase && addr < p4_mmiobase + 0x8000) {
			uae_u32 addr2 = addr - p4_mmiobase;
			v  = vgammio->read(&vga, addr2 + 0, 1) << 24;
			v |= vgammio->read(&vga, addr2 + 1, 1) << 16;
			v |= vgammio->read(&vga, addr2 + 2, 1) <<  8;
			v |= vgammio->read(&vga, addr2 + 3, 1) <<  0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO LGET %08x %08x\n"), addr, v);
#endif
			return v;
		}
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV LGET %08x %08x\n"), addr, v);
#endif
	return v;
}
static uae_u32 REGPARAM2 gfxboards_wget_regs (uaecptr addr)
{
	uae_u16 v = 0;
	addr &= p4_special_mask;
	// pci config
	if (addr >= 0x400000 || (p4z2 && !(picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			v =  p4_pci[addr2 + 0] << 8;
			v |= p4_pci[addr2 + 1] << 0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI WGET %08x %04x\n"), addr, v);
#endif
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
			v =  cirrus_pci[addr2 + 0] << 8;
			v |= cirrus_pci[addr2 + 1] << 0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI WGET %08x %04x\n"), addr, v);
#endif
		}
		return v;
	}
	if (picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= p4_mmiobase && addr < p4_mmiobase + 0x8000) {
			uae_u32 addr2 = addr - p4_mmiobase;
			v  = vgammio->read(&vga, addr2 + 0, 1) << 8;
			v |= vgammio->read(&vga, addr2 + 1, 1) << 0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO WGET %08x %04x\n"), addr, v & 0xffff);
#endif
			return v;
		}
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV WGET %04x %04x\n"), addr, v);
#endif
	return v;
}
static uae_u32 REGPARAM2 gfxboards_bget_regs (uaecptr addr)
{
	uae_u8 v = 0xff;
	addr &= p4_special_mask;

	// pci config
	if (addr >= 0x400000 || (p4z2 && !(picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		v = 0;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			v =  p4_pci[addr2];
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI BGET %08x %02x\n"), addr, v);
#endif
		} else if (addr2 >= 0x800 && addr2 <= 0x1000) {
			if (addr2 == 0x802)
				v = 2; // ???
			if (addr2 == 0x808)
				v = 4; // bridge revision
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI BGET %08x %02x\n"), addr, v);
#endif
		} else if (addr2 >= 0x1000 && addr2 <= 0x1040) {
			addr2 -= 0x1000;
			v = cirrus_pci[addr2];
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI BGET %08x %02x\n"), addr, v);
#endif
		}
		return v;
	}

	if (picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= p4_mmiobase && addr < p4_mmiobase + 0x8000) {
			uae_u32 addr2 = addr - p4_mmiobase;
			v = vgammio->read(&vga, addr2, 1);
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO BGET %08x %02x\n"), addr, v & 0xff);
#endif
			return v;
		}
	}
	if (addr == 0) {
		v = picassoiv_bank;
		return v;
	}
	if (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) {
		v = 0;
		if (addr == 0x404) {
			v = 0x70; // FLIFI revision
			// FLIFI type in use
			if (currprefs.chipset_mask & CSMASK_AGA)
				v |= 4 | 8;
			else
				v |= 8;
		} else if (addr == 0x408) {
			v = gfxboard_vblank ? 0x80 : 0;
		} else if (p4z2 && addr >= 0x10000) {
			addr -= 0x10000;
			uaecptr addr2 = mungeaddr (addr, true);
			if (addr2) {
				v = vgaio->read (&vga, addr2, 1);
				v = bget_regtest (addr2, v);
			}
			//write_log (_T("PicassoIV IO %08x %02x\n"), addr, v);
			return v;
		}
#if PICASSOIV_DEBUG_IO
		if (addr != 0x408)
			write_log (_T("PicassoIV BGET %08x %02x\n"), addr, v);
#endif
	} else {
		if (addr < PICASSOIV_FLASH_OFFSET) {
			v = automemory[addr];
			return v;
		}
		addr -= PICASSOIV_FLASH_OFFSET;
		addr /= 2;
		v = automemory[addr + PICASSOIV_FLASH_OFFSET + ((picassoiv_bank & PICASSOIV_BANK_FLASHBANK) ? 0x8000 : 0)];
	}
	return v;
}
static void REGPARAM2 gfxboards_lput_regs (uaecptr addr, uae_u32 l)
{
	addr &= p4_special_mask;
	if (addr >= 0x400000 || (p4z2 && !(picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI LPUT %08x %08x\n"), addr, l);
#endif
			p4_pci[addr2 + 0] = l >> 24;
			p4_pci[addr2 + 1] = l >> 16;
			p4_pci[addr2 + 2] = l >>  8;
			p4_pci[addr2 + 3] = l >>  0;
			p4_pci_check ();
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI LPUT %08x %08x\n"), addr, l);
#endif
			cirrus_pci[addr2 + 0] = l >> 24;
			cirrus_pci[addr2 + 1] = l >> 16;
			cirrus_pci[addr2 + 2] = l >>  8;
			cirrus_pci[addr2 + 3] = l >>  0;
			reset_pci ();
		}
		return;
	}
	if (picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= p4_mmiobase && addr < p4_mmiobase + 0x8000) {
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO LPUT %08x %08x\n"), addr, l);
#endif
			uae_u32 addr2 = addr - p4_mmiobase;
			vgammio->write(&vga, addr2 + 0, l >> 24, 1);
			vgammio->write(&vga, addr2 + 1, l >> 16, 1);
			vgammio->write(&vga, addr2 + 2, l >>  8, 1);
			vgammio->write(&vga, addr2 + 3, l >>  0, 1);
			return;
		}
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV LPUT %08x %08x\n"), addr, l);
#endif
}
static void REGPARAM2 gfxboards_wput_regs (uaecptr addr, uae_u32 v)
{
	uae_u16 w = (uae_u16)v;
	addr &= p4_special_mask;
	if (addr >= 0x400000 || (p4z2 && !(picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI WPUT %08x %04x\n"), addr, w & 0xffff);
#endif
			p4_pci[addr2 + 0] = w >> 8;
			p4_pci[addr2 + 1] = w >> 0;
			p4_pci_check ();
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI WPUT %08x %04x\n"), addr, w & 0xffff);
#endif
			cirrus_pci[addr2 + 0] = w >> 8;
			cirrus_pci[addr2 + 1] = w >> 0;
			reset_pci ();
		}
		return;
	}
	if (picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= p4_mmiobase && addr < p4_mmiobase + 0x8000) {
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO LPUT %08x %08x\n"), addr, w & 0xffff);
#endif
			uae_u32 addr2 = addr - p4_mmiobase;
			vgammio->write(&vga, addr2 + 0, w >> 8, 1);
			vgammio->write(&vga, addr2 + 1, (w >> 0) & 0xff, 1);
			return;
		}
	}
	if (p4z2 && addr >= 0x10000) {
		addr -= 0x10000;
		addr = mungeaddr (addr, true);
		if (addr) {
			vgaio->write (&vga, addr + 0, w >> 8, 1);
			bput_regtest (addr + 0, w >> 8);
			vgaio->write (&vga, addr + 1, (w >> 0) & 0xff, 1);
			bput_regtest (addr + 1, w >> 0);
		}
		return;
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV WPUT %08x %04x\n"), addr, w & 0xffff);
#endif
}

static void REGPARAM2 gfxboards_bput_regs (uaecptr addr, uae_u32 v)
{
	uae_u8 b = (uae_u8)v;
	addr &= p4_special_mask;
	if (addr >= 0x400000 || (p4z2 && !(picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			p4_pci[addr2] = b;
			p4_pci_check ();
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI BPUT %08x %02x\n"), addr, b & 0xff);
#endif
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
			cirrus_pci[addr2] = b;
			reset_pci ();
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI BPUT %08x %02x\n"), addr, b & 0xff);
#endif
		}
		return;
	}
	if (picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) {
		if (addr == 0x404) {
			picassoiv_flifi = b;
			picassoiv_checkswitch ();
		}
	}
	if (picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= p4_mmiobase && addr < p4_mmiobase + 0x8000) {
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO BPUT %08x %08x\n"), addr, b & 0xff);
#endif
			uae_u32 addr2 = addr - p4_mmiobase;
			vgammio->write(&vga, addr2, b, 1);
			return;
		}
	}
	if (p4z2 && addr >= 0x10000) {
		addr -= 0x10000;
		addr = mungeaddr (addr, true);
		if (addr) {
			vgaio->write (&vga, addr, b & 0xff, 1);
			bput_regtest (addr, b);
		}
		return;
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV BPUT %08x %02X\n"), addr, b & 0xff);
#endif
	if (addr == 0) {
		picassoiv_bank = b;
	}
}

const TCHAR *gfxboard_get_name(int type)
{
	if (type == GFXBOARD_UAE_Z2)
		return _T("UAE Zorro II");
	if (type == GFXBOARD_UAE_Z3)
		return _T("UAE Zorro III (*)");
	return boards[type - GFXBOARD_HARDWARE].name;
}

const TCHAR *gfxboard_get_manufacturername(int type)
{
	if (type == GFXBOARD_UAE_Z2)
		return NULL;
	if (type == GFXBOARD_UAE_Z3)
		return NULL;
	return boards[type - GFXBOARD_HARDWARE].manufacturername;
}

const TCHAR *gfxboard_get_configname(int type)
{
	if (type == GFXBOARD_UAE_Z2)
		return _T("ZorroII");
	if (type == GFXBOARD_UAE_Z3)
		return _T("ZorroIII");
	return boards[type - GFXBOARD_HARDWARE].configname;
}

int gfxboard_get_configtype(int type)
{
	if (type == GFXBOARD_UAE_Z2)
		return 2;
	if (type == GFXBOARD_UAE_Z3)
		return 3;
	board = &boards[type - GFXBOARD_HARDWARE];
	return board->configtype;
}

bool gfxboard_need_byteswap (int type)
{
	if (type < GFXBOARD_HARDWARE)
		return false;
	board = &boards[type - GFXBOARD_HARDWARE];
	return board->swap;
}

int gfxboard_get_autoconfig_size(int type)
{
	if (type == GFXBOARD_PICASSO4_Z3)
		return 32 * 1024 * 1024;
	return -1;
}

int gfxboard_get_vram_min (int type)
{
	if (type < GFXBOARD_HARDWARE)
		return -1;
	board = &boards[type - GFXBOARD_HARDWARE];
	return board->vrammin;
}

int gfxboard_get_vram_max (int type)
{
	if (type < GFXBOARD_HARDWARE)
		return -1;
	board = &boards[type - GFXBOARD_HARDWARE];
	return board->vrammax;
}

bool gfxboard_is_registers (int type)
{
	if (type < 2)
		return false;
	board = &boards[type - 2];
	return board->model_registers != 0;
}

int gfxboard_num_boards (int type)
{
	if (type < 2)
		return 1;
	board = &boards[type - 2];
	if (type == GFXBOARD_PICASSO4_Z2)
		return 3;
	if (board->model_registers == 0)
		return 1;
	return 2;
}

uae_u32 gfxboard_get_romtype(int type)
{
	if (type < 2)
		return 0;
	board = &boards[type - 2];
	return board->romtype;
}

static void gfxboard_init (void)
{
	if (!automemory)
		automemory = xmalloc (uae_u8, GFXBOARD_AUTOCONFIG_SIZE);
	memset (automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	p4z2 = false;
	zfile_fclose (p4rom);
	p4rom = NULL;
	banksize_mask = board->banksize - 1;
	memset (cirrus_pci, 0, sizeof cirrus_pci);
	reset_pci ();
}

static void copyp4autoconfig (int startoffset)
{
	int size = 0;
	int offset = 0;
	memset (automemory, 0xff, 64);
	while (size < 32) {
		uae_u8 b = p4autoconfig[size + startoffset];
		automemory[offset] = b;
		offset += 2;
		size++;
	}
}

static void loadp4rom (void)
{
	int size, offset;
	uae_u8 b;
	// rom loader code
	zfile_fseek (p4rom, 256, SEEK_SET);
	offset = PICASSOIV_ROM_OFFSET;
	size = 0;
	while (size < 4096 - 256) {
		if (!zfile_fread (&b, 1, 1, p4rom))
			break;
		automemory[offset] = b;
		offset += 2;
		size++;
	}
	// main flash code
	zfile_fseek (p4rom, 16384, SEEK_SET);
	zfile_fread (&automemory[PICASSOIV_FLASH_OFFSET], 1, PICASSOIV_MAX_FLASH, p4rom);
	zfile_fclose (p4rom);
	p4rom = NULL;
	write_log (_T("PICASSOIV: flash rom loaded\n"));
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

addrbank *gfxboard_init_memory (int devnum)
{
	int bank;
	uae_u8 z2_flags, z3_flags, type;

	gfxboard_init ();

	memset (automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	
	z2_flags = 0x05; // 1M
	z3_flags = 0x06; // 1M
	bank = board->banksize;
	bank /= 0x00100000;
	while (bank > 1) {
		z2_flags++;
		z3_flags++;
		bank >>= 1;
	}
	if (board->configtype == 3) {
		type = 0x00 | 0x08 | 0x80; // 16M Z3
		ew (0x08, z3_flags | 0x10 | 0x20);
	} else {
		type = z2_flags | 0x08 | 0xc0;
	}
	ew (0x04, board->model_memory);
	ew (0x10, board->manufacturer >> 8);
	ew (0x14, board->manufacturer);

	uae_u32 ser = board->serial;
	ew (0x18, ser >> 24); /* ser.no. Byte 0 */
	ew (0x1c, ser >> 16); /* ser.no. Byte 1 */
	ew (0x20, ser >>  8); /* ser.no. Byte 2 */
	ew (0x24, ser >>  0); /* ser.no. Byte 3 */

	ew (0x00, type);

	if (ISP4()) {
		int roms[] = { 91, -1 };
		struct romlist *rl = getromlistbyids(roms, NULL);
		TCHAR path[MAX_DPATH];
		fetch_rompath (path, sizeof path / sizeof (TCHAR));

		p4rom = read_device_rom(&currprefs, ROMTYPE_PICASSOIV, 0, roms);

		if (!p4rom && currprefs.picassoivromfile[0] && zfile_exists(currprefs.picassoivromfile))
			p4rom = read_rom_name(currprefs.picassoivromfile);

		if (!p4rom && rl)
			p4rom = read_rom(rl->rd);

		if (!p4rom) {
			_tcscat (path, _T("picasso_iv_flash.rom"));
			p4rom = read_rom_name (path);
			if (!p4rom)
				p4rom = read_rom_name (_T("picasso_iv_flash.rom"));
		}
		if (p4rom) {
			zfile_fread (p4autoconfig, sizeof p4autoconfig, 1, p4rom);
			copyp4autoconfig (board->configtype == 3 ? 192 : 0);
			if (board->configtype == 3) {
				loadp4rom ();
				p4_mmiobase = 0x200000;
				p4_special_mask = 0x7fffff;
			} else {
				p4z2 = true;
				p4_mmiobase = 0x8000;
				p4_special_mask = 0x1ffff;
			}
			gfxboard_intena = true;
		} else {
			error_log (_T("Picasso IV: '%s' flash rom image not found!\nAvailable from http://www.sophisticated-development.de/\nPIV_FlashImageXX -> picasso_iv_flash.rom"), path);
			gui_message (_T("Picasso IV: '%s' flash rom image not found!\nAvailable from http://www.sophisticated-development.de/\nPIV_FlashImageXX -> picasso_iv_flash.rom"), path);
		}
	}

	_stprintf (memorybankname, _T("%s VRAM"), board->name);
	_stprintf (wbsmemorybankname, _T("%s VRAM WORDSWAP"), board->name);
	_stprintf (lbsmemorybankname, _T("%s VRAM LONGSWAP"), board->name);
	_stprintf (regbankname, _T("%s REG"), board->name);

	gfxboard_bank_memory.name = memorybankname;
	gfxboard_bank_memory_nojit.name = memorybankname;
	gfxboard_bank_wbsmemory.name = wbsmemorybankname;
	gfxboard_bank_lbsmemory.name = lbsmemorybankname;
	gfxboard_bank_registers.name = regbankname;

	gfxboard_bank_memory.bget = gfxboard_bget_mem_autoconfig;
	gfxboard_bank_memory.bput = gfxboard_bput_mem_autoconfig;

	if (currprefs.rtgmem_type == GFXBOARD_VGA) {
		init_board();
		configured_mem = 1;
		configured_regs = 1;
		return &expamem_null;
	}

	return &gfxboard_bank_memory;
}

addrbank *gfxboard_init_memory_p4_z2 (int devnum)
{
	if (board->configtype == 3)
		return &expamem_null;

	copyp4autoconfig (64);
	return &gfxboard_bank_memory;
}

addrbank *gfxboard_init_registers (int devnum)
{
	if (!board->model_registers)
		return &expamem_null;

	memset (automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	ew (0x00, 0xc0 | 0x01); // 64k Z2
	ew (0x04, board->model_registers);
	ew (0x10, board->manufacturer >> 8);
	ew (0x14, board->manufacturer);

	uae_u32 ser = board->serial;
	ew (0x18, ser >> 24); /* ser.no. Byte 0 */
	ew (0x1c, ser >> 16); /* ser.no. Byte 1 */
	ew (0x20, ser >>  8); /* ser.no. Byte 2 */
	ew (0x24, ser >>  0); /* ser.no. Byte 3 */

	gfxboard_bank_registers.allocated = BOARD_REGISTERS_SIZE;

	if (ISP4()) {
		uae_u8 v;
		copyp4autoconfig (128);
		loadp4rom ();
		v = (((automemory[0] & 0xf0) | (automemory[2] >> 4)) & 3) - 1;
		gfxboard_bank_special.allocated = 0x10000 << v;
	}

	gfxboard_bank_registers.bget = gfxboard_bget_regs_autoconfig;
	gfxboard_bank_registers.bput = gfxboard_bput_regs_autoconfig;

	return &gfxboard_bank_registers;
}
