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
#define REGDEBUG 0
#define MEMDEBUG 0
#define MEMDEBUGMASK 0x7fffff
#define MEMDEBUGTEST 0x3fc000
#define MEMDEBUGCLEAR 0
#define PICASSOIV_DEBUG_IO 0

#if MEMLOGR
static bool memlogr = true;
static bool memlogw = true;
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
#include "xwin.h"

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
	uae_u8 er_type;
	struct gfxboard_func *func;
};

#define ISP4() (gb->rbc->rtgmem_type == GFXBOARD_PICASSO4_Z2 || gb->rbc->rtgmem_type == GFXBOARD_PICASSO4_Z3)

// Picasso II: 8* 4x256 (1M) or 16* 4x256 (2M)
// Piccolo: 8* 4x256 + 2* 16x256 (2M)

static const struct gfxboard boards[] =
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
		0x00000000, 0x00200000, 0x00400000, 0x00400000, CIRRUS_ID_CLGD5446, 2, 2, false,
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
		0x00000000, 0x00200000, 0x00200000, 0x10000, 0, 0, 2, false,
		0, 0xc1, &a2410_func
	},
#if 0
	{
		_T("Resolver"), _T("DMI"), _T("Resolver"),
		2129, 1, 0,
		0x00000000, 0x00200000, 0x00200000, 0x10000, 0, 0, 2, false,
		0, 0xc1, &a2410_func
	},
#endif
	{
		_T("x86 bridgeboard VGA"), _T("x86"), _T("VGA"),
		0, 0, 0,
		0x00000000, 0x00100000, 0x00200000, 0x00000000, CIRRUS_ID_CLGD5426, 0, 0, false,
		ROMTYPE_x86_VGA
	},
	{
		_T("Harlequin"), _T("ACS"), _T("Harlequin_PAL"),
		2118, 100, 0,
		0x00000000, 0x00200000, 0x00200000, 0x10000, 0, 0, 2, false,
		ROMTYPE_HARLEQUIN, 0xc2, &harlequin_func
	},
	{
		NULL
	}
};

struct rtggfxboard
{
	bool active;
	int rtg_index;
	int monitor_id;
	struct rtgboardconfig *rbc;
	TCHAR memorybankname[40];
	TCHAR memorybanknamenojit[40];
	TCHAR wbsmemorybankname[40];
	TCHAR lbsmemorybankname[40];
	TCHAR regbankname[40];

	int configured_mem, configured_regs;
	const struct gfxboard *board;
	uae_u8 expamem_lo;
	uae_u8 *automemory;
	uae_u32 banksize_mask;
	uaecptr io_start, io_end;
	uaecptr mem_start[2], mem_end[2];

	uae_u8 picassoiv_bank, picassoiv_flifi;
	uae_u8 p4autoconfig[256];
	struct zfile *p4rom;
	bool p4z2;
	uae_u32 p4_mmiobase;
	uae_u32 p4_special_mask;
	uae_u32 p4_vram_bank[2];

	CirrusVGAState vga;
	uae_u8 *vram, *vramend, *vramrealstart;
	int vram_start_offset;
	uae_u32 gfxboardmem_start;
	bool monswitch_current, monswitch_new;
	bool monswitch_keep_trying;
	bool monswitch_reset;
	int monswitch_delay;
	int fullrefresh;
	bool modechanged;
	uae_u8 *gfxboard_surface, *fakesurface_surface;
	bool gfxboard_vblank;
	bool gfxboard_intena;
	bool vram_enabled, vram_offset_enabled;
	hwaddr vram_offset[2];
	uae_u8 cirrus_pci[0x44];
	uae_u8 p4i2c;
	uae_u8 p4_pci[0x44];
	int vga_width, vga_height;
	bool vga_refresh_active;
	bool vga_changed;
	int device_settings;

	uae_u32 vgaioregionptr, vgavramregionptr, vgabank0regionptr, vgabank1regionptr;

	const MemoryRegionOps *vgaio, *vgaram, *vgalowram, *vgammio;
	MemoryRegion vgaioregion, vgavramregion;
	DisplaySurface gfxsurface, fakesurface;

	addrbank gfxboard_bank_memory;
	addrbank gfxboard_bank_memory_nojit;
	addrbank gfxboard_bank_wbsmemory;
	addrbank gfxboard_bank_lbsmemory;
	addrbank gfxboard_bank_nbsmemory;
	addrbank gfxboard_bank_registers;
	addrbank gfxboard_bank_special;

	addrbank *gfxmem_bank;
	uae_u8 *vram_back;
	
	struct autoconfig_info *aci;

	struct gfxboard_func *func;
	void *userdata;
};

static struct rtggfxboard rtggfxboards[MAX_RTG_BOARDS];
static struct rtggfxboard *only_gfx_board;
static int rtg_visible[MAX_AMIGADISPLAYS];
static int rtg_initial[MAX_AMIGADISPLAYS];
static int total_active_gfx_boards;
static int vram_ram_a8;
static DisplaySurface fakesurface;

DECLARE_MEMORY_FUNCTIONS(gfxboard);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, mem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, mem_nojit);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, bsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, wbsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, lbsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, nbsmem);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboard, regs);
DECLARE_MEMORY_FUNCTIONS_WITH_SUFFIX(gfxboards, regs);

static const addrbank tmpl_gfxboard_bank_memory = {
	gfxboard_lget_mem, gfxboard_wget_mem, gfxboard_bget_mem,
	gfxboard_lput_mem, gfxboard_wput_mem, gfxboard_bput_mem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_mem, gfxboard_wget_mem,
	ABFLAG_RAM | ABFLAG_THREADSAFE | ABFLAG_CACHE_ENABLE_ALL, 0, 0
};

static const addrbank tmpl_gfxboard_bank_memory_nojit = {
	gfxboard_lget_mem_nojit, gfxboard_wget_mem_nojit, gfxboard_bget_mem_nojit,
	gfxboard_lput_mem_nojit, gfxboard_wput_mem_nojit, gfxboard_bput_mem_nojit,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_mem_nojit, gfxboard_wget_mem_nojit,
	ABFLAG_RAM | ABFLAG_THREADSAFE | ABFLAG_CACHE_ENABLE_ALL, S_READ, S_WRITE
};

static const addrbank tmpl_gfxboard_bank_wbsmemory = {
	gfxboard_lget_wbsmem, gfxboard_wget_wbsmem, gfxboard_bget_wbsmem,
	gfxboard_lput_wbsmem, gfxboard_wput_wbsmem, gfxboard_bput_wbsmem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_wbsmem, gfxboard_wget_wbsmem,
	ABFLAG_RAM | ABFLAG_THREADSAFE | ABFLAG_PPCIOSPACE | ABFLAG_CACHE_ENABLE_ALL, S_READ, S_WRITE
};

static const addrbank tmpl_gfxboard_bank_lbsmemory = {
	gfxboard_lget_lbsmem, gfxboard_wget_lbsmem, gfxboard_bget_lbsmem,
	gfxboard_lput_lbsmem, gfxboard_wput_lbsmem, gfxboard_bput_lbsmem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, NULL,
	gfxboard_lget_lbsmem, gfxboard_wget_lbsmem,
	ABFLAG_RAM | ABFLAG_THREADSAFE | ABFLAG_PPCIOSPACE | ABFLAG_CACHE_ENABLE_ALL, S_READ, S_WRITE
};

static const addrbank tmpl_gfxboard_bank_nbsmemory = {
	gfxboard_lget_nbsmem, gfxboard_wget_nbsmem, gfxboard_bget_bsmem,
	gfxboard_lput_nbsmem, gfxboard_wput_nbsmem, gfxboard_bput_bsmem,
	gfxboard_xlate, gfxboard_check, NULL, NULL, _T("Picasso IV banked VRAM"),
	gfxboard_lget_nbsmem, gfxboard_wget_nbsmem,
	ABFLAG_RAM | ABFLAG_THREADSAFE | ABFLAG_PPCIOSPACE | ABFLAG_CACHE_ENABLE_ALL, S_READ, S_WRITE
};

static const addrbank tmpl_gfxboard_bank_registers = {
	gfxboard_lget_regs, gfxboard_wget_regs, gfxboard_bget_regs,
	gfxboard_lput_regs, gfxboard_wput_regs, gfxboard_bput_regs,
	default_xlate, default_check, NULL, NULL, NULL,
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

static const addrbank tmpl_gfxboard_bank_special = {
	gfxboards_lget_regs, gfxboards_wget_regs, gfxboards_bget_regs,
	gfxboards_lput_regs, gfxboards_wput_regs, gfxboards_bput_regs,
	default_xlate, default_check, NULL, NULL, _T("Picasso IV MISC"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

static void ew(struct rtggfxboard *gb, int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		gb->automemory[addr] = (value & 0xf0);
		gb->automemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		gb->automemory[addr] = ~(value & 0xf0);
		gb->automemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

int gfxboard_get_devnum(struct uae_prefs *p, int index)
{
	int devnum = 0;
	uae_u32 romtype = gfxboard_get_romtype(&p->rtgboards[index]);
	if (!romtype)
		return devnum;
	for (int i = 0; i < index; i++) {
		if (gfxboard_get_romtype(&p->rtgboards[i]) == romtype)
			devnum++;
	}
	return devnum;
}

void gfxboard_get_a8_vram(int index)
{
	addrbank *ab = gfxmem_banks[index];
	if (vram_ram_a8 > 0) {
		addrbank *prev = gfxmem_banks[vram_ram_a8 - 1];
		ab->baseaddr = prev->baseaddr;
	} else {
		mapped_malloc(ab);
		vram_ram_a8 = index + 1;
	}
}

void gfxboard_free_vram(int index)
{
	addrbank *ab = gfxmem_banks[index];
	if (vram_ram_a8 - 1 == index || vram_ram_a8 == 0) {
		mapped_free(ab);
	}
	if (vram_ram_a8 - 1 == index)
		vram_ram_a8 = 0;
}

static void init_board (struct rtggfxboard *gb)
{
	struct rtgboardconfig *rbc = gb->rbc;
	int vramsize = gb->board->vrammax;
	int chiptype = gb->board->chiptype;

	if (gb->board->romtype == ROMTYPE_x86_VGA) {
		struct romconfig *rc = get_device_romconfig(&currprefs, gb->board->romtype, 0);
		chiptype = CIRRUS_ID_CLGD5426;
		if (rc && rc->device_settings == 1) {
			chiptype = CIRRUS_ID_CLGD5429;
		}
	}

	gb->active = true;
	gb->vga_width = 0;
	gb->vga_height = 0;
	mapped_free(gb->gfxmem_bank);
	gb->vram_start_offset = 0;
	if (ISP4() && !gb->p4z2) { // JIT direct compatibility hack
		gb->vram_start_offset = 0x01000000;
	}
	vramsize += gb->vram_start_offset;
	xfree (gb->fakesurface_surface);
	gb->fakesurface_surface = xmalloc (uae_u8, 4 * 10000);
	gb->vram_offset[0] = gb->vram_offset[1] = 0;
	gb->vram_enabled = true;
	gb->vram_offset_enabled = false;
	gb->gfxmem_bank->reserved_size = vramsize;
	gb->gfxmem_bank->start = gb->gfxboardmem_start;
	if (gb->board->manufacturer) {
		gb->gfxmem_bank->label = _T("*");
		mapped_malloc(gb->gfxmem_bank);
	} else {
		gb->gfxmem_bank->label = _T("*");
		gb->vram_back = xmalloc(uae_u8, vramsize);
		if (&get_mem_bank(0x800000) == &dummy_bank)
			gb->gfxmem_bank->start = 0x800000;
		else
			gb->gfxmem_bank->start = 0xa00000;
		gfxboard_get_a8_vram(gb->rbc->rtg_index);
	}
	gb->vram = gb->gfxmem_bank->baseaddr;
	gb->vramend = gb->gfxmem_bank->baseaddr + vramsize;
	gb->vramrealstart = gb->vram;
	gb->vram += gb->vram_start_offset;
	gb->vramend += gb->vram_start_offset;
	//gb->gfxmem_bank->baseaddr = gb->vram;
	// restore original value because this is checked against
	// configured size in expansion.cpp
	gb->gfxmem_bank->allocated_size = rbc->rtgmem_size;
	gb->gfxmem_bank->reserved_size = rbc->rtgmem_size;
	gb->vga.vga.vram_size_mb = rbc->rtgmem_size >> 20;
	gb->vgaioregion.opaque = &gb->vgaioregionptr;
	gb->vgaioregion.data = gb;
	gb->vgavramregion.opaque = &gb->vgavramregionptr;
	gb->vgavramregion.data = gb;
	gb->vga.vga.vram.opaque = &gb->vgavramregionptr;
	gb->vga.vga.vram.data = gb;
	gb->vga.cirrus_vga_io.data = gb;
	gb->vga.low_mem_container.data = gb;
	gb->vga.low_mem.data = gb;
	gb->vga.cirrus_bank[0].data = gb;
	gb->vga.cirrus_bank[1].data = gb;
	gb->vga.cirrus_linear_io.data = gb;
	gb->vga.cirrus_linear_bitblt_io.data = gb;
	gb->vga.cirrus_mmio_io.data = gb;
	gb->gfxsurface.data = gb;
	gb->fakesurface.data = gb;
	vga_common_init(&gb->vga.vga);
	gb->vga.vga.con = (void*)gb;
	cirrus_init_common(&gb->vga, chiptype, 0,  NULL, NULL, gb->board->manufacturer == 0, gb->board->romtype == ROMTYPE_x86_VGA);
	picasso_allocatewritewatch(gb->rbc->rtg_index, gb->rbc->rtgmem_size);
}

static int GetBytesPerPixel(RGBFTYPE RGBfmt)
{
	switch (RGBfmt)
	{
		case RGBFB_CLUT:
		return 1;

		case RGBFB_A8R8G8B8:
		case RGBFB_A8B8G8R8:
		case RGBFB_R8G8B8A8:
		case RGBFB_B8G8R8A8:
		return 4;

		case RGBFB_B8G8R8:
		case RGBFB_R8G8B8:
		return 3;

		case RGBFB_R5G5B5:
		case RGBFB_R5G6B5:
		case RGBFB_R5G6B5PC:
		case RGBFB_R5G5B5PC:
		case RGBFB_B5G6R5PC:
		case RGBFB_B5G5R5PC:
		return 2;
	}
	return 0;
}

static bool gfxboard_setmode(struct rtggfxboard *gb, struct gfxboard_mode *mode)
{
	struct amigadisplay *ad = &adisplays[gb->monitor_id];
	struct picasso96_state_struct *state = &picasso96_state[gb->monitor_id];

	state->Width = mode->width;
	state->Height = mode->height;
	int bpp = GetBytesPerPixel(mode->mode);
	state->BytesPerPixel = bpp;
	state->RGBFormat = mode->mode;
	write_log(_T("GFXBOARD %dx%dx%d\n"), mode->width, mode->height, bpp);
	if (!ad->picasso_requested_on && !ad->picasso_on) {
		ad->picasso_requested_on = true;
		set_config_changed();
	}
	return true;
}

static void gfxboard_free_slot2(struct rtggfxboard *gb)
{
	struct amigadisplay *ad = &adisplays[gb->monitor_id];
	gb->active = false;
	if (rtg_visible[gb->monitor_id] == gb->rtg_index) {
		rtg_visible[gb->monitor_id] = -1;
		ad->picasso_requested_on = false;
		set_config_changed();
	}
	gb->userdata = NULL;
	gb->func = NULL;
	xfree(gb->automemory);
	gb->automemory = NULL;
}

bool gfxboard_allocate_slot(int board, int idx)
{
	struct rtggfxboard *gb = &rtggfxboards[idx];
	gb->active = true;
	gb->rtg_index = idx;
	const struct gfxboard *gfxb = &boards[board - GFXBOARD_HARDWARE];
	gb->board = gfxb;
	return true;
}
void gfxboard_free_slot(int idx)
{
	struct rtggfxboard *gb = &rtggfxboards[idx];
	gfxboard_free_slot2(gb);
}

static int gfx_temp_bank_idx;

static uae_u32 REGPARAM2 gtb_wget(uaecptr addr)
{
	struct rtggfxboard *gb = &rtggfxboards[gfx_temp_bank_idx];
	addr &= gb->banksize_mask;
	return 0;
}
static uae_u32 REGPARAM2 gtb_bget(uaecptr addr)
{
	struct rtggfxboard *gb = &rtggfxboards[gfx_temp_bank_idx];
	addr &= gb->banksize_mask;
	if (addr < GFXBOARD_AUTOCONFIG_SIZE)
		return gb->automemory[addr];
	return 0xff;
}
static void REGPARAM2 gtb_bput(uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = &rtggfxboards[gfx_temp_bank_idx];
	b &= 0xff;
	addr &= gb->banksize_mask;
	if (addr == 0x48) {
		gfx_temp_bank_idx++;
		map_banks_z2(gb->gfxmem_bank, expamem_board_pointer >> 16, expamem_board_size >> 16);
		gb->func->configured(gb->userdata, expamem_board_pointer);
		expamem_next(gb->gfxmem_bank, NULL);
		return;
	}
	if (addr == 0x4c) {
		expamem_shutup(gb->gfxmem_bank);
		return;
	}
}
static void REGPARAM2 gtb_wput(uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = &rtggfxboards[gfx_temp_bank_idx];
	b &= 0xffff;
	addr &= gb->banksize_mask;
	if (addr == 0x44) {
		gfx_temp_bank_idx++;
		map_banks_z3(gb->gfxmem_bank, expamem_board_pointer >> 16, expamem_board_size >> 16);
		gb->func->configured(gb->userdata, expamem_board_pointer);
		expamem_next(gb->gfxmem_bank, NULL);
		return;
	}
}

static addrbank gfx_temp_bank =
{
	gtb_wget, gtb_wget, gtb_bget,
	gtb_wput, gtb_wput, gtb_bput,
	default_xlate, default_check, NULL, NULL, _T("GFXBOARD_AUTOCONFIG"),
	gtb_wget, gtb_wget,
	ABFLAG_IO, S_READ, S_WRITE
};

bool gfxboard_init_board(struct autoconfig_info *aci)
{
	const struct gfxboard *gfxb = &boards[aci->prefs->rtgboards[aci->devnum].rtgmem_type - GFXBOARD_HARDWARE];
	struct rtggfxboard *gb = &rtggfxboards[aci->devnum];
	gb->func = gfxb->func;
	gb->monitor_id = aci->prefs->rtgboards[aci->devnum].monitor_id;
	memset(aci->autoconfig_bytes, 0xff, sizeof aci->autoconfig_bytes);
	if (!gb->automemory)
		gb->automemory = xmalloc(uae_u8, GFXBOARD_AUTOCONFIG_SIZE);
	memset(gb->automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	ew(gb, 0x00, gfxb->er_type);
	ew(gb, 0x04, gfxb->model_memory);
	ew(gb, 0x10, (gfxb->manufacturer >> 8) & 0xff);
	ew(gb, 0x14, (gfxb->manufacturer >> 0) & 0xff);
	ew(gb, 0x18, (gfxb->serial >> 24) & 0xff);
	ew(gb, 0x1c, (gfxb->serial >> 16) & 0xff);
	ew(gb, 0x20, (gfxb->serial >>  8) & 0xff);
	ew(gb, 0x24, (gfxb->serial >>  0) & 0xff);
	memcpy(aci->autoconfig_raw, gb->automemory, sizeof aci->autoconfig_raw);
	if (!gb->func->init(aci))
		return false;
	for(int i = 0; i < sizeof aci->autoconfig_bytes; i++) {
		if (aci->autoconfig_bytes[i] != 0xff)
			ew(gb, i * 4, aci->autoconfig_bytes[i]);
	}
	memcpy(aci->autoconfig_raw, gb->automemory, sizeof aci->autoconfig_raw);
	if (!aci->doinit)
		return true;
	gb->banksize_mask = gfxb->banksize - 1;
	gb->userdata = aci->userdata;
	gb->active = true;
	gb->rtg_index = aci->devnum;
	gb->board = gfxb;
	gfx_temp_bank_idx = aci->devnum;
	gb->gfxmem_bank = aci->addrbank;
	aci->addrbank = &gfx_temp_bank;
	return true;
}

static void vga_update_size(struct rtggfxboard *gb)
{
	// this forces qemu_console_resize() call
	gb->vga.vga.graphic_mode = -1;
	gb->vga.vga.monid = gb->monitor_id;
	gb->vga.vga.hw_ops->gfx_update(&gb->vga);
}

static bool gfxboard_setmode_qemu(struct rtggfxboard *gb)
{
	int bpp = gb->vga.vga.get_bpp(&gb->vga.vga);
	if (bpp == 0)
		bpp = 8;
	vga_update_size(gb);
	if (gb->vga_width <= 16 || gb->vga_height <= 16)
		return false;
	struct gfxboard_mode mode;
	mode.width = gb->vga_width;
	mode.height = gb->vga_height;
	mode.mode = RGBFB_NONE;
	for (int i = 0; i < RGBFB_MaxFormats; i++) {
		RGBFTYPE t = (RGBFTYPE)i;
		if (GetBytesPerPixel(t) == bpp / 8) {
			mode.mode = t;
			break;
		}
	}
	gfxboard_setmode(gb, &mode);
	gfx_set_picasso_modeinfo(gb->monitor_id, mode.mode);
	gb->fullrefresh = 2;
	gb->vga_changed = false;
	return true;
}

bool gfxboard_set(int monid, bool rtg)
{
	bool r;
	struct amigadisplay *ad = &adisplays[monid];
	r = ad->picasso_on;
	if (rtg) {
		ad->picasso_requested_on = 1;
	} else {
		ad->picasso_requested_on = 0;
	}
	set_config_changed();
	return r;
}

void gfxboard_rtg_disable(int monid, int index)
{
	if (monid > 0)
		return;
	if (index == rtg_visible[monid] && rtg_visible[monid] >= 0) {
		struct rtggfxboard *gb = &rtggfxboards[index];
		if (rtg_visible[monid] >= 0 && gb->func) {
			gb->func->toggle(gb->userdata, 0);
		}
		rtg_visible[monid] = -1;
	}
}

bool gfxboard_rtg_enable_initial(int monid, int index)
{
	struct amigadisplay *ad = &adisplays[monid];
	// if some RTG already enabled and located in monitor 0, don't override
	if ((rtg_visible[monid] >= 0 || rtg_initial[monid] >= 0) && !monid)
		return false;
	if (ad->picasso_on)
		return false;
	rtg_initial[monid] = index;
	gfxboard_toggle(monid, index, false);
	// check_prefs_picasso() calls gfxboard_toggle when ready
	return true;
}


int gfxboard_toggle(int monid, int index, int log)
{
	bool initial = false;

	if (rtg_visible[monid] < 0 && rtg_initial[monid] >= 0 && rtg_initial[monid] < MAX_RTG_BOARDS) {
		index = rtg_initial[monid];
		initial = true;
	}

	gfxboard_rtg_disable(monid, rtg_visible[monid]);

	rtg_visible[monid] = -1;

	if (index < 0)
		goto end;

	struct rtggfxboard *gb = &rtggfxboards[index];
	if (!gb->active)
		goto end;

	if (gb->func) {
		bool r = gb->func->toggle(gb->userdata, 1);
		if (r) {
			rtg_initial[monid] = MAX_RTG_BOARDS;
			rtg_visible[monid] = gb->rtg_index;
			if (log && !monid)
				statusline_add_message(STATUSTYPE_DISPLAY, _T("RTG %d: %s"), index + 1, gb->board->name);
			return index;
		}
		goto end;
	}

	if (gb->vram == NULL)
		return -1;
	vga_update_size(gb);
	if (gb->vga_width > 16 && gb->vga_height > 16) {
		if (!gfxboard_setmode_qemu(gb))
			goto end;
		rtg_initial[monid] = MAX_RTG_BOARDS;
		rtg_visible[monid] = gb->rtg_index;
		gb->monswitch_new = true;
		gb->monswitch_delay = 1;
		if (log && !monid)
			statusline_add_message(STATUSTYPE_DISPLAY, _T("RTG %d: %s"), index + 1, gb->board->name);
		return index;
	}
end:
	if (initial) {
		rtg_initial[monid] = -1;
		return -2;
	}
	return -1;
}

static bool gfxboard_checkchanged(struct rtggfxboard *gb)
{
	struct picasso96_state_struct *state = &picasso96_state[gb->monitor_id];
	int bpp = gb->vga.vga.get_bpp (&gb->vga.vga);
	if (bpp == 0)
		bpp = 8;
	if (gb->vga_width <= 16 || gb->vga_height <= 16)
		return false;
	if (state->Width != gb->vga_width ||
		state->Height != gb->vga_height ||
		state->BytesPerPixel != bpp / 8)
		return true;
	return false;
}

DisplaySurface *qemu_console_surface(QemuConsole *con)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)con;
	return &gb->gfxsurface;
}

void qemu_console_resize(QemuConsole *con, int width, int height)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)con;
	if (width != gb->vga_width || gb->vga_height != height)
		gb->vga_changed = true;
	gb->vga_width = width;
	gb->vga_height = height;
}

void linear_memory_region_set_dirty(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
}

void vga_memory_region_set_dirty(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (gb->vga.vga.graphic_mode != 1)
		return;
	if (!gb->fullrefresh)
		gb->fullrefresh = 1;
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
	struct rtggfxboard *gb;
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		gb = &rtggfxboards[i];
		if (data >= gb->vram && data < gb->vramend) {
			gb->modechanged = true;
			return &gb->fakesurface;
		}
	}
	return NULL;
}

int surface_bits_per_pixel(DisplaySurface *s)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)s->data;
	if (rtg_visible[gb->monitor_id] < 0)
		return 32;
	if (s == &gb->fakesurface)
		return 32;
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[gb->monitor_id];
	return vidinfo->pixbytes * 8;
}
int surface_bytes_per_pixel(DisplaySurface *s)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)s->data;
	if (rtg_visible[gb->monitor_id] < 0)
		return 4;
	if (s == &gb->fakesurface)
		return 4;
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[gb->monitor_id];
	return vidinfo->pixbytes;
}

int surface_stride(DisplaySurface *s)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)s->data;
	if (rtg_visible[gb->monitor_id] < 0)
		return 0;
	if (s == &gb->fakesurface || !gb->vga_refresh_active)
		return 0;
	if (gb->gfxboard_surface == NULL)
		gb->gfxboard_surface = gfx_lock_picasso(gb->monitor_id, false, false);
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[gb->monitor_id];
	return vidinfo->rowbytes;
}
uint8_t *surface_data(DisplaySurface *s)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)s->data;
	if (!gb)
		return NULL;
	if (rtg_visible[gb->monitor_id] < 0)
		return NULL;
	if (gb->vga_changed)
		return NULL;
	if (s == &gb->fakesurface || !gb->vga_refresh_active)
		return gb->fakesurface_surface;
	if (gb->gfxboard_surface == NULL)
		gb->gfxboard_surface = gfx_lock_picasso(gb->monitor_id, false, false);
	return gb->gfxboard_surface;
}

void gfxboard_refresh(int monid)
{
	if (monid >= 0) {
		for (int i = 0; i < MAX_RTG_BOARDS; i++) {
			struct rtgboardconfig *rbc = &currprefs.rtgboards[i];
			if (rbc->monitor_id == monid && rbc->rtgmem_size) {
				if (rbc->rtgmem_type >= GFXBOARD_HARDWARE) {
					struct rtggfxboard *gb = &rtggfxboards[i];
					gb->fullrefresh = 2;
				} else {
					picasso_refresh(monid);
				}
			}
		}
	} else {
		for (int i = 0; i < MAX_RTG_BOARDS; i++) {
			struct rtgboardconfig *rbc = &currprefs.rtgboards[i];
			if (rbc->rtgmem_size) {
				gfxboard_refresh(rbc->monitor_id);
			}
		}
	}
}

void gfxboard_hsync_handler(void)
{
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		struct rtggfxboard *gb = &rtggfxboards[i];
		if (gb->func && gb->userdata) {
			gb->func->hsync(gb->userdata);
		}
	}
}

void gfxboard_vsync_handler(bool full_redraw_required, bool redraw_required)
{
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		struct rtggfxboard *gb = &rtggfxboards[i];
		struct amigadisplay *ad = &adisplays[gb->monitor_id];
		struct picasso96_state_struct *state = &picasso96_state[gb->monitor_id];

		if (gb->func) {

			if (gb->userdata) {
				struct gfxboard_mode mode = { 0 };
				mode.redraw_required = full_redraw_required;
				gb->func->vsync(gb->userdata, &mode);
				if (mode.mode && mode.width && mode.height) {
					if (state->Width != mode.width ||
						state->Height != mode.height ||
						state->RGBFormat != mode.mode ||
						!ad->picasso_on) {
						if (mode.width && mode.height && mode.mode) {
							gfxboard_setmode(gb, &mode);
							gfx_set_picasso_modeinfo(gb->monitor_id, mode.mode);
						}
					}
				}
			}

		} else 	if (gb->configured_mem > 0 && gb->configured_regs > 0) {

			if (gb->monswitch_keep_trying) {
				vga_update_size(gb);
				if (gb->vga_width > 16 && gb->vga_height > 16) {
					gb->monswitch_keep_trying = false;
					gb->monswitch_new = true;
					gb->monswitch_delay = 0;
				}
			}

			if (gb->monswitch_new != gb->monswitch_current) {
				if (gb->monswitch_delay > 0)
					gb->monswitch_delay--;
				if (gb->monswitch_delay == 0) {
					if (!gb->monswitch_new && rtg_visible[gb->monitor_id] == i) {
						gfxboard_rtg_disable(gb->monitor_id, i);
					}
					gb->monswitch_current = gb->monswitch_new;
					vga_update_size(gb);
					write_log(_T("GFXBOARD %d MONITOR=%d ACTIVE=%d\n"), i, gb->monitor_id, gb->monswitch_current);
					if (gb->monitor_id > 0) {
						if (gb->monswitch_new)
							gfxboard_toggle(gb->monitor_id, i, 0);
					} else {
						if (gb->monswitch_current) {
							if (!gfxboard_rtg_enable_initial(gb->monitor_id, i)) {
								// Nothing visible? Re-enable our display.
								if (rtg_visible[gb->monitor_id] < 0) {
									gfxboard_toggle(gb->monitor_id, i, 0);
								}
							}
						} else {
							if (ad->picasso_requested_on) {
								ad->picasso_requested_on = false;
								set_config_changed();
							}
						}
					}
				}
			} else {
				gb->monswitch_delay = 0;
			}
			// Vertical Sync End Register, 0x20 = Disable Vertical Interrupt, 0x10 = Clear Vertical Interrupt.
			if (gb->board->irq) {
				if ((!(gb->vga.vga.cr[0x11] & 0x20) && (gb->vga.vga.cr[0x11] & 0x10) && !(gb->vga.vga.gr[0x17] & 4))) {
					if (gb->gfxboard_intena) {
						gb->gfxboard_vblank = true;
						//write_log(_T("VGA interrupt %d\n"), gb->board->irq);
						if (gb->board->irq == 2)
							INTREQ(0x8000 | 0x0008);
						else
							INTREQ(0x8000 | 0x2000);
					}
				}
			}
		}
	}

	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		struct rtggfxboard *gb = &rtggfxboards[i];
		struct amigadisplay *ad = &adisplays[gb->monitor_id];
		struct picasso96_state_struct *state = &picasso96_state[gb->monitor_id];

		if (gb->monitor_id == 0) {
			// if default monitor: show rtg_visible
			if (rtg_visible[gb->monitor_id] < 0)
				continue;
			if (i != rtg_visible[gb->monitor_id])
				continue;
		}

		if (gb->configured_mem <= 0 || gb->configured_regs <= 0)
			continue;

		if (gb->monswitch_current && (gb->modechanged || gfxboard_checkchanged(gb))) {
			gb->modechanged = false;
			if (!gfxboard_setmode_qemu(gb)) {
				gfxboard_rtg_disable(gb->monitor_id, i);
				continue;
			}
			continue;
		}

		if (!redraw_required)
			continue;

		if (!gb->monswitch_delay && gb->monswitch_current && ad->picasso_on && ad->picasso_requested_on && !gb->vga_changed) {
			picasso_getwritewatch(i, gb->vram_start_offset);
			if (gb->fullrefresh)
				gb->vga.vga.graphic_mode = -1;
			gb->vga_refresh_active = true;
			gb->vga.vga.hw_ops->gfx_update(&gb->vga);
			gb->vga_refresh_active = false;
		}

		if (ad->picasso_on && !gb->vga_changed) {
			if (!gb->monitor_id) {
				if (currprefs.leds_on_screen & STATUSLINE_RTG) {
					if (gb->gfxboard_surface == NULL) {
						gb->gfxboard_surface = gfx_lock_picasso(gb->monitor_id, false, false);
					}
					if (gb->gfxboard_surface) {
						if (softstatusline())
							picasso_statusline(gb->monitor_id, gb->gfxboard_surface);
					}
				}
			}
			if (gb->fullrefresh > 0)
				gb->fullrefresh--;
		}
		gfx_unlock_picasso(gb->monitor_id, true);
		gb->gfxboard_surface = NULL;
	}
}

double gfxboard_get_vsync (void)
{
	return vblank_hz; // FIXME
}

void dpy_gfx_update(QemuConsole *con, int x, int y, int w, int h)
{
	picasso_invalidate(0, x, y, w, h);
}

void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (!stricmp(name, "vga.bank0")) {
		mr->opaque = &gb->vgabank0regionptr;
	} else if (!stricmp(name, "vga.bank1")) {
		mr->opaque = &gb->vgabank1regionptr;
	}
}

static void jit_reset (void)
{
#ifdef JIT
	if (currprefs.cachesize && (!currprefs.comptrustbyte || !currprefs.comptrustword || !currprefs.comptrustlong)) {
		flush_icache (3);
	}
#endif
}

static void remap_vram (struct rtggfxboard *gb, hwaddr offset0, hwaddr offset1, bool enabled)
{
	jit_reset ();
	gb->vram_offset[0] = offset0;
	gb->vram_offset[1] = offset1;
#if VRAMLOG
	if (gb->vram_enabled != enabled)
		write_log (_T("VRAM state=%d\n"), enabled);
	bool was_vram_offset_enabled = gb->vram_offset_enabled;
#endif
	gb->vram_enabled = enabled && (gb->vga.vga.sr[0x07] & 0x01);
#if 0
	vram_enabled = false;
#endif
	// offset==0 and offset1==0x8000: linear vram mapping
	gb->vram_offset_enabled = offset0 != 0 || offset1 != 0x8000;
#if VRAMLOG
	if (gb->vram_offset_enabled || was_vram_offset_enabled)
		write_log (_T("VRAM offset %08x and %08x\n"), offset0, offset1);
#endif
}

void memory_region_set_alias_offset(MemoryRegion *mr, hwaddr offset)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (mr->opaque == &gb->vgabank0regionptr) {
		if (offset != gb->vram_offset[0]) {
			//write_log (_T("vgavramregion0 %08x\n"), offset);
			remap_vram (gb, offset, gb->vram_offset[1], gb->vram_enabled);
		}
	} else if (mr->opaque == &gb->vgabank1regionptr) {
		if (offset != gb->vram_offset[1]) {
			//write_log (_T("vgavramregion1 %08x\n"), offset);
			remap_vram (gb, gb->vram_offset[0], offset, gb->vram_enabled);
		}
	} else if (mr->opaque == &gb->vgaioregionptr) {
		write_log (_T("vgaioregion %d\n"), offset);
	} else {
		write_log (_T("unknown region %d\n"), offset);
	}

}
void memory_region_set_enabled(MemoryRegion *mr, bool enabled)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (mr->opaque == &gb->vgabank0regionptr || mr->opaque == &gb->vgabank1regionptr) {
		if (enabled != gb->vram_enabled)  {
			//write_log (_T("enable vgavramregion %d\n"), enabled);
			remap_vram (gb, gb->vram_offset[0], gb->vram_offset[1], enabled);
		}
	} else if (mr->opaque == &gb->vgaioregionptr) {
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
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (mr->opaque != &gb->vgavramregionptr)
		return false;
	//write_log (_T("memory_region_get_dirty %08x %08x\n"), addr, size);
	if (gb->fullrefresh)
		return true;
	return picasso_is_vram_dirty (gb->rtg_index, addr + gb->gfxmem_bank->start, size);
}

static QEMUResetHandler *reset_func;
static void *reset_parm;
void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
	reset_func = func;
	reset_parm = opaque;
	reset_func (reset_parm);
}

static void p4_pci_check (struct rtggfxboard *gb)
{
	uaecptr b0, b1;

	b0 = gb->p4_pci[0x10 + 2] << 16;
	b1 = gb->p4_pci[0x14 + 2] << 16;

	gb->p4_vram_bank[0] = b0;
	gb->p4_vram_bank[1] = b1;
#if PICASSOIV_DEBUG_IO
	write_log (_T("%08X %08X\n"), gb->p4_vram_bank[0], gb->p4_vram_bank[1]);
#endif
}

static void reset_pci (struct rtggfxboard *gb)
{
	gb->cirrus_pci[0] = 0x00;
	gb->cirrus_pci[1] = 0xb8;
	gb->cirrus_pci[2] = 0x10;
	gb->cirrus_pci[3] = 0x13;

	gb->cirrus_pci[4] = 2;
	gb->cirrus_pci[5] = 0;
	gb->cirrus_pci[6] = 0;
	gb->cirrus_pci[7] &= ~(1 | 2 | 32);

	gb->cirrus_pci[8] = 3;
	gb->cirrus_pci[9] = 0;
	gb->cirrus_pci[10] = 0;
	gb->cirrus_pci[11] = 68;

	gb->cirrus_pci[0x10] &= ~1; // B revision
	gb->cirrus_pci[0x13] &= ~1; // memory

	gb->p4i2c = 0xff;
}

static void set_monswitch(struct rtggfxboard *gb, bool newval)
{
	if (gb->monswitch_new == newval)
		return;
	gb->monswitch_new = newval;
	gb->monswitch_delay = MONITOR_SWITCH_DELAY;
}

static void picassoiv_checkswitch (struct rtggfxboard *gb)
{
	if (ISP4()) {
		bool rtg_active = (gb->picassoiv_flifi & 1) == 0 || (gb->vga.vga.cr[0x51] & 8) == 0;
		// do not switch to P4 RTG until monitor switch is set to native at least
		// once after reset.
		if (gb->monswitch_reset && rtg_active)
			return;
		gb->monswitch_reset = false;
		set_monswitch(gb, rtg_active);
	}
}

static void bput_regtest (struct rtggfxboard *gb, uaecptr addr, uae_u8 v)
{
	addr += 0x3b0;
	if (addr == 0x3d5) { // CRxx
		if (gb->vga.vga.cr_index == 0x11) {
			if (!(gb->vga.vga.cr[0x11] & 0x10)) {
				gb->gfxboard_vblank = false;
			}
		}
	}
	if (!(gb->vga.vga.sr[0x07] & 0x01) && gb->vram_enabled) {
		remap_vram (gb, gb->vram_offset[0], gb->vram_offset[1], false);
	}
	picassoiv_checkswitch (gb);
}

static uae_u8 bget_regtest (struct rtggfxboard *gb, uaecptr addr, uae_u8 v)
{
	addr += 0x3b0;
	// Input Status 0
	if (addr == 0x3c2) {
		if (gb->gfxboard_vblank) {
			// Disable Vertical Interrupt == 0?
			// Clear Vertical Interrupt == 1
			// GR17 bit 2 = INTR disable
			if (!(gb->vga.vga.cr[0x11] & 0x20) && (gb->vga.vga.cr[0x11] & 0x10) && !(gb->vga.vga.gr[0x17] & 4)) {
				v |= 0x80; // VGA Interrupt Pending
			}
		}
		v |= 0x10; // DAC sensing
	}
	if (addr == 0x3c5) {
		if (gb->vga.vga.sr_index == 8) {
			// TODO: DDC
		}
	}
	return v;
}

void vga_io_put(int board, int portnum, uae_u8 v)
{
	struct rtggfxboard *gb = &rtggfxboards[board];
	if (!gb->vgaio)
		return;
	portnum -= 0x3b0;
	bput_regtest(gb, portnum, v);
	gb->vgaio->write(&gb->vga, portnum, v, 1);
}
uae_u8 vga_io_get(int board, int portnum)
{
	struct rtggfxboard *gb = &rtggfxboards[board];
	uae_u8 v = 0xff;
	if (!gb->vgaio)
		return v;
	portnum -= 0x3b0;
	v = gb->vgaio->read(&gb->vga, portnum, 1);
	v = bget_regtest(gb, portnum, v);
	return v;
}
void vga_ram_put(int board, int offset, uae_u8 v)
{
	struct rtggfxboard *gb = &rtggfxboards[board];
	if (!gb->vgalowram)
		return;
	offset -= 0xa0000;
	gb->vgalowram->write(&gb->vga, offset, v, 1);
}
uae_u8 vga_ram_get(int board, int offset)
{
	struct rtggfxboard *gb = &rtggfxboards[board];
	if (!gb->vgalowram)
		return 0xff;
	offset -= 0xa0000;
	return gb->vgalowram->read(&gb->vga, offset, 1);
}
void vgalfb_ram_put(int board, int offset, uae_u8 v)
{
	struct rtggfxboard *gb = &rtggfxboards[board];
	if (!gb->vgaram)
		return;
	gb->vgaram->write(&gb->vga, offset, v, 1);
}
uae_u8 vgalfb_ram_get(int board, int offset)
{
	struct rtggfxboard *gb = &rtggfxboards[board];
	if (!gb->vgaram)
		return 0xff;
	return gb->vgaram->read(&gb->vga, offset, 1);
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (mr->opaque == &gb->vgavramregionptr)
		return gb->vram;
	return NULL;
}

void memory_region_init_ram(MemoryRegion *mr,
                            const char *name,
                            uint64_t size)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (!stricmp (name, "vga.vram")) {
		gb->vgavramregion.opaque = mr->opaque;
	}
}

void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
	struct rtggfxboard *gb = (struct rtggfxboard*)mr->data;
	if (!stricmp (name, "cirrus-io")) {
		gb->vgaio = ops;
		mr->opaque = gb->vgaioregion.opaque;
	} else if (!stricmp (name, "cirrus-linear-io")) {
		gb->vgaram = ops;
	} else if (!stricmp (name, "cirrus-low-memory")) {
		gb->vgalowram = ops;
	} else if (!stricmp (name, "cirrus-mmio")) {
		gb->vgammio = ops;
	}
}

int is_surface_bgr(DisplaySurface *surface)
{
	if (!surface || !surface->data)
		return 0;
	struct rtggfxboard *gb = (struct rtggfxboard*)surface->data;
	return gb->board->swap;
}

static uaecptr fixaddr_bs (struct rtggfxboard *gb, uaecptr addr, int mask, int *bs)
{
	bool swapped = false;
	addr &= gb->gfxmem_bank->mask;
	if (gb->p4z2) {
		if (addr < 0x200000) {
			addr |= gb->p4_vram_bank[0];
			if (addr >= 0x400000 && addr < 0x600000) {
				*bs = BYTESWAP_WORD;
				swapped = true;
			} else if (addr >= 0x800000 && addr < 0xa00000) {
				*bs = BYTESWAP_LONG;
				swapped = true;
			}
		} else {
			addr |= gb->p4_vram_bank[1];
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
	if (mask && (gb->vram_offset_enabled || !gb->vram_enabled || swapped || gb->p4z2))
		special_mem |= mask;
#endif
	if (gb->vram_offset_enabled) {
		if (addr & 0x8000) {
			addr += gb->vram_offset[1] & ~0x8000;
		} else {
			addr += gb->vram_offset[0];
		}
	}
	addr &= gb->gfxmem_bank->mask;
	return addr;
}

STATIC_INLINE uaecptr fixaddr (struct rtggfxboard *gb, uaecptr addr, int mask)
{
#ifdef JIT
	if (mask && (gb->vram_offset_enabled || !gb->vram_enabled))
		special_mem |= mask;
#endif
	if (gb->vram_offset_enabled) {
		if (addr & 0x8000) {
			addr += gb->vram_offset[1] & ~0x8000;
		} else {
			addr += gb->vram_offset[0];
		}
	}
	addr &= gb->gfxmem_bank->mask;
	return addr;
}

STATIC_INLINE uaecptr fixaddr (struct rtggfxboard *gb, uaecptr addr)
{
	if (gb->vram_offset_enabled) {
		if (addr & 0x8000) {
			addr += gb->vram_offset[1] & ~0x8000;
		} else {
			addr += gb->vram_offset[0];
		}
	}
	addr &= gb->gfxmem_bank->mask;
	return addr;
}

STATIC_INLINE const MemoryRegionOps *getvgabank (struct rtggfxboard *gb, uaecptr *paddr)
{
	uaecptr addr = *paddr;
	addr &= gb->gfxmem_bank->mask;
	*paddr = addr;
	return gb->vgaram;
}

static uae_u32 gfxboard_lget_vram (struct rtggfxboard *gb, uaecptr addr, int bs)
{
	uae_u32 v;
	if (!gb->vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (gb, &addr);
		if (bs < 0) { // WORD
			v  = bank->read (&gb->vga, addr + 1, 1) << 24;
			v |= bank->read (&gb->vga, addr + 0, 1) << 16;
			v |= bank->read (&gb->vga, addr + 3, 1) <<  8;
			v |= bank->read (&gb->vga, addr + 2, 1) <<  0;
		} else if (bs > 0) { // LONG
			v  = bank->read (&gb->vga, addr + 3, 1) << 24;
			v |= bank->read (&gb->vga, addr + 2, 1) << 16;
			v |= bank->read (&gb->vga, addr + 1, 1) <<  8;
			v |= bank->read (&gb->vga, addr + 0, 1) <<  0;
		} else {
			v  = bank->read (&gb->vga, addr + 0, 1) << 24;
			v |= bank->read (&gb->vga, addr + 1, 1) << 16;
			v |= bank->read (&gb->vga, addr + 2, 1) <<  8;
			v |= bank->read (&gb->vga, addr + 3, 1) <<  0;
		}
	} else {
		uae_u8 *m = gb->vram + addr;
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
		write_log (_T("R %08X L %08X BS=%d EN=%d\n"), addr, v, bs, gb->vram_enabled);
#endif
	return v;
}
static uae_u16 gfxboard_wget_vram (struct rtggfxboard *gb, uaecptr addr, int bs)
{
	uae_u32 v;
	if (!gb->vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (gb, &addr);
		if (bs) {
			v  = bank->read (&gb->vga, addr + 0, 1) <<  0;
			v |= bank->read (&gb->vga, addr + 1, 1) <<  8;
		} else {
			v  = bank->read (&gb->vga, addr + 0, 1) <<  8;
			v |= bank->read (&gb->vga, addr + 1, 1) <<  0;
		}
	} else {
		uae_u8 *m = gb->vram + addr;
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
		write_log (_T("R %08X W %08X BS=%d EN=%d\n"), addr, v, bs, gb->vram_enabled);
#endif
	return v;
}
static uae_u8 gfxboard_bget_vram (struct rtggfxboard *gb, uaecptr addr, int bs)
{
	uae_u32 v;
	if (!gb->vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (gb, &addr);
		if (bs)
			v = bank->read (&gb->vga, addr ^ 1, 1);
		else
			v = bank->read (&gb->vga, addr + 0, 1);
	} else {
		if (bs)
			v = gb->vram[addr ^ 1];
		else
			v = gb->vram[addr];
	}
#if MEMLOGR
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogr)
		write_log (_T("R %08X B %08X BS=0 EN=%d\n"), addr, v, gb->vram_enabled);
#endif
	return v;
}

static void gfxboard_lput_vram (struct rtggfxboard *gb, uaecptr addr, uae_u32 l, int bs)
{
#if MEMDEBUG
	if ((addr & MEMDEBUGMASK) >= MEMDEBUGTEST && (MEMDEBUGCLEAR || l))
		write_log (_T("%08X L %08X %08X\n"), addr, l, M68K_GETPC);
#endif
#if MEMLOGW
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogw)
		write_log (_T("W %08X L %08X\n"), addr, l);
#endif
	if (!gb->vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (gb, &addr);
		if (bs < 0) { // WORD
			bank->write (&gb->vga, addr + 1, l >> 24, 1);
			bank->write (&gb->vga, addr + 0, (l >> 16) & 0xff, 1);
			bank->write (&gb->vga, addr + 3, (l >> 8) & 0xff, 1);
			bank->write (&gb->vga, addr + 2, (l >> 0) & 0xff, 1);
		} else if (bs > 0) { // LONG
			bank->write (&gb->vga, addr + 3, l >> 24, 1);
			bank->write (&gb->vga, addr + 2, (l >> 16) & 0xff, 1);
			bank->write (&gb->vga, addr + 1, (l >> 8) & 0xff, 1);
			bank->write (&gb->vga, addr + 0, (l >> 0) & 0xff, 1);
		} else {
			bank->write (&gb->vga, addr + 0, l >> 24, 1);
			bank->write (&gb->vga, addr + 1, (l >> 16) & 0xff, 1);
			bank->write (&gb->vga, addr + 2, (l >> 8) & 0xff, 1);
			bank->write (&gb->vga, addr + 3, (l >> 0) & 0xff, 1);
		}
	} else {
		uae_u8 *m = gb->vram + addr;
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
static void gfxboard_wput_vram (struct rtggfxboard *gb, uaecptr addr, uae_u16 w, int bs)
{
#if MEMDEBUG
	if ((addr & MEMDEBUGMASK) >= MEMDEBUGTEST && (MEMDEBUGCLEAR || w))
		write_log (_T("%08X W %04X %08X\n"), addr, w & 0xffff, M68K_GETPC);
#endif
#if MEMLOGW
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogw)
		write_log (_T("W %08X W %04X\n"), addr, w & 0xffff);
#endif
	if (!gb->vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (gb, &addr);
		if (bs) {
			bank->write (&gb->vga, addr + 0, (w >> 0) & 0xff, 1);
			bank->write (&gb->vga, addr + 1, w >> 8, 1);
		} else {
			bank->write (&gb->vga, addr + 0, w >> 8, 1);
			bank->write (&gb->vga, addr + 1, (w >> 0) & 0xff, 1);
		}
	} else {
		uae_u8 *m = gb->vram + addr;
		if (bs)
			*((uae_u16*)m) = w;
		else
			do_put_mem_word ((uae_u16*)m, w);
	}
}
static void gfxboard_bput_vram (struct rtggfxboard *gb, uaecptr addr, uae_u8 b, int bs)
{
#if MEMDEBUG
	if ((addr & MEMDEBUGMASK) >= MEMDEBUGTEST && (MEMDEBUGCLEAR || b))
		write_log (_T("%08X B %02X %08X\n"), addr, b & 0xff, M68K_GETPC);
#endif
#if MEMLOGW
#if MEMLOGINDIRECT
	if (!vram_enabled || vram_offset_enabled)
#endif
	if (memlogw)
		write_log (_T("W %08X B %02X\n"), addr, b & 0xff);
#endif
	if (!gb->vram_enabled) {
		const MemoryRegionOps *bank = getvgabank (gb, &addr);
#ifdef JIT
		special_mem |= S_WRITE;
#endif
		if (bs)
			bank->write (&gb->vga, addr ^ 1, b, 1);
		else
			bank->write (&gb->vga, addr, b, 1);
	} else {
		if (bs)
			gb->vram[addr ^ 1] = b;
		else
			gb->vram[addr] = b;
	}
}

static struct rtggfxboard *lastgetgfxboard;
static rtggfxboard *getgfxboard(uaecptr addr)
{
	if (only_gfx_board)
		return only_gfx_board;
	if (lastgetgfxboard) {
		if (addr >= lastgetgfxboard->mem_start[0] && addr < lastgetgfxboard->mem_end[0])
			return lastgetgfxboard;
		if (addr >= lastgetgfxboard->io_start && addr < lastgetgfxboard->io_end)
			return lastgetgfxboard;
		if (addr >= lastgetgfxboard->mem_start[1] && addr < lastgetgfxboard->mem_end[1])
			return lastgetgfxboard;
		lastgetgfxboard = NULL;
	}
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		struct rtggfxboard *gb = &rtggfxboards[i];
		if (!gb->active)
			continue;
		if (gb->configured_mem < 0 || gb->configured_regs < 0) {
			lastgetgfxboard = gb;
			return gb;
		} else {
			if (addr >= gb->io_start && addr < gb->io_end) {
				lastgetgfxboard = gb;
				return gb;
			}
			if (addr >= gb->mem_start[0] && addr < gb->mem_end[0]) {
				lastgetgfxboard = gb;
				return gb;
			}
			if (addr >= gb->mem_start[1] && addr < gb->mem_end[1]) {
				lastgetgfxboard = gb;
				return gb;
			}
		}
	}
	return NULL;
}

// LONG byteswapped VRAM
static uae_u32 REGPARAM2 gfxboard_lget_lbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (gb, addr, BYTESWAP_LONG);
}
static uae_u32 REGPARAM2 gfxboard_wget_lbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (gb, addr, BYTESWAP_LONG);
}
static uae_u32 REGPARAM2 gfxboard_bget_lbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (gb, addr, BYTESWAP_LONG);
}

static void REGPARAM2 gfxboard_lput_lbsmem (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return;
	gfxboard_lput_vram (gb, addr, l, BYTESWAP_LONG);
}
static void REGPARAM2 gfxboard_wput_lbsmem (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return;
	gfxboard_wput_vram (gb, addr, w, BYTESWAP_LONG);
}
static void REGPARAM2 gfxboard_bput_lbsmem (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return;
	gfxboard_bput_vram (gb, addr, w, BYTESWAP_LONG);
}

// WORD byteswapped VRAM
static uae_u32 REGPARAM2 gfxboard_lget_wbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (gb, addr, BYTESWAP_WORD);
}
static uae_u32 REGPARAM2 gfxboard_wget_wbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (gb, addr, BYTESWAP_WORD);
}
static uae_u32 REGPARAM2 gfxboard_bget_wbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (gb, addr, BYTESWAP_WORD);
}

static void REGPARAM2 gfxboard_lput_wbsmem (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return;
	gfxboard_lput_vram (gb, addr, l, BYTESWAP_WORD);
}
static void REGPARAM2 gfxboard_wput_wbsmem (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return;
	gfxboard_wput_vram (gb, addr, w, BYTESWAP_WORD);
}
static void REGPARAM2 gfxboard_bput_wbsmem (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return;
	gfxboard_bput_vram (gb, addr, w, BYTESWAP_WORD);
}

// normal or byteswapped (banked) vram
static uae_u32 REGPARAM2 gfxboard_lget_nbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	int bs = 0;
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr_bs (gb, addr, 0, &bs);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (gb, addr, bs);
}
static uae_u32 REGPARAM2 gfxboard_wget_nbsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	int bs = 0;
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr_bs (gb, addr, 0, &bs);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (gb, addr, bs);
}

static void REGPARAM2 gfxboard_lput_nbsmem (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	int bs = 0;
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr_bs (gb, addr, 0, &bs);
	if (addr == -1)
		return;
	gfxboard_lput_vram (gb, addr, l, bs);
}
static void REGPARAM2 gfxboard_wput_nbsmem (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	int bs = 0;
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr_bs (gb, addr, 0, &bs);
	if (addr == -1)
		return;
	gfxboard_wput_vram (gb, addr, w, bs);
}

static uae_u32 REGPARAM2 gfxboard_bget_bsmem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	int bs = 0;
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, 0);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (gb, addr, bs);
}
static void REGPARAM2 gfxboard_bput_bsmem (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	int bs = 0;
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr_bs (gb, addr, 0, &bs);
	if (addr == -1)
		return;
	gfxboard_bput_vram (gb, addr, b, bs);
}

// normal vram
static uae_u32 REGPARAM2 gfxboard_lget_mem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, S_READ);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (gb, addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_wget_mem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, S_READ);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (gb, addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_bget_mem (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, S_READ);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (gb, addr, 0);
}
static void REGPARAM2 gfxboard_lput_mem (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, S_WRITE);
	if (addr == -1)
		return;
	gfxboard_lput_vram (gb, addr, l, 0);
}
static void REGPARAM2 gfxboard_wput_mem (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, S_WRITE);
	if (addr == -1)
		return;
	gfxboard_wput_vram (gb, addr, w, 0);
}
static void REGPARAM2 gfxboard_bput_mem (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr(gb, addr, S_WRITE);
	if (addr == -1)
		return;
	gfxboard_bput_vram (gb, addr, b, 0);
}

// normal vram, no jit direct
static uae_u32 REGPARAM2 gfxboard_lget_mem_nojit (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr (gb, addr);
	if (addr == -1)
		return 0;
	return gfxboard_lget_vram (gb, addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_wget_mem_nojit (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr (gb, addr);
	if (addr == -1)
		return 0;
	return gfxboard_wget_vram (gb, addr, 0);
}
static uae_u32 REGPARAM2 gfxboard_bget_mem_nojit (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr (gb, addr);
	if (addr == -1)
		return 0;
	return gfxboard_bget_vram (gb, addr, 0);
}
static void REGPARAM2 gfxboard_lput_mem_nojit (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr (gb, addr);
	if (addr == -1)
		return;
	gfxboard_lput_vram (gb, addr, l, 0);
}
static void REGPARAM2 gfxboard_wput_mem_nojit (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr (gb, addr);
	if (addr == -1)
		return;
	gfxboard_wput_vram (gb, addr, w, 0);
}
static void REGPARAM2 gfxboard_bput_mem_nojit (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr = fixaddr (gb, addr);
	if (addr == -1)
		return;
	gfxboard_bput_vram (gb, addr, b, 0);
}

static int REGPARAM2 gfxboard_check (uaecptr addr, uae_u32 size)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr &= gb->gfxmem_bank->mask;
	return (addr + size) <= gb->rbc->rtgmem_size;
}
static uae_u8 *REGPARAM2 gfxboard_xlate (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr -= gb->gfxboardmem_start & gb->gfxmem_bank->mask;
	addr &= gb->gfxmem_bank->mask;
	return gb->vram + addr;
}

static uae_u32 REGPARAM2 gfxboard_bget_mem_autoconfig (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u32 v = 0;
	addr &= 65535;
	if (addr < GFXBOARD_AUTOCONFIG_SIZE)
		v = gb->automemory[addr];
	return v;
}

static void copyvrambank(addrbank *dst, const addrbank *src)
{
	dst->start = src->start;
	dst->startmask = src->startmask;
	dst->mask = src->mask;
	dst->allocated_size = src->allocated_size;
	dst->reserved_size = src->reserved_size;
	dst->baseaddr = src->baseaddr;
	dst->flags = src->flags;
	dst->jit_read_flag = src->jit_read_flag;
	dst->jit_write_flag = src->jit_write_flag;
}

static void REGPARAM2 gfxboard_wput_mem_autoconfig (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	if (gb->board->configtype == 2)
		return;
	b &= 0xffff;
	addr &= 65535;
	if (addr == 0x44) {
		uae_u32 start = expamem_board_pointer;
		if (!expamem_z3hack(&currprefs)) {
			start = ((b & 0xff00) | gb->expamem_lo) << 16;
		}
		gb->gfxboardmem_start = start;
		gb->gfxboard_bank_memory.bget = gfxboard_bget_mem;
		gb->gfxboard_bank_memory.bput = gfxboard_bput_mem;
		gb->gfxboard_bank_memory.wput = gfxboard_wput_mem;
		init_board(gb);
		copyvrambank(&gb->gfxboard_bank_memory, gb->gfxmem_bank);
		if (ISP4()) {
			if (validate_banks_z3(&gb->gfxboard_bank_memory, gb->gfxmem_bank->start >> 16, expamem_board_size >> 16)) {
				// main vram
				map_banks_z3(&gb->gfxboard_bank_memory, (gb->gfxmem_bank->start + PICASSOIV_VRAM1) >> 16, 0x400000 >> 16);
				map_banks_z3(&gb->gfxboard_bank_wbsmemory, (gb->gfxmem_bank->start + PICASSOIV_VRAM1 + 0x400000) >> 16, 0x400000 >> 16);
				// secondary
				map_banks_z3(&gb->gfxboard_bank_memory_nojit, (gb->gfxmem_bank->start + PICASSOIV_VRAM2) >> 16, 0x400000 >> 16);
				map_banks_z3(&gb->gfxboard_bank_wbsmemory, (gb->gfxmem_bank->start + PICASSOIV_VRAM2 + 0x400000) >> 16, 0x400000 >> 16);
				// regs
				map_banks_z3(&gb->gfxboard_bank_registers, (gb->gfxmem_bank->start + PICASSOIV_REG) >> 16, 0x200000 >> 16);
				map_banks_z3(&gb->gfxboard_bank_special, gb->gfxmem_bank->start >> 16, PICASSOIV_REG >> 16);
			}
			gb->picassoiv_bank = 0;
			gb->picassoiv_flifi = 1;
			gb->configured_regs = gb->gfxmem_bank->start >> 16;
		} else {
			map_banks_z3(&gb->gfxboard_bank_memory, gb->gfxmem_bank->start >> 16, gb->board->banksize >> 16);
		}
		gb->mem_start[0] = start;
		gb->mem_end[0] = gb->mem_start[0] + gb->board->banksize;
		gb->configured_mem = gb->gfxmem_bank->start >> 16;
		expamem_next (&gb->gfxboard_bank_memory, NULL);
		return;
	}
	if (addr == 0x4c) {
		gb->configured_mem = 0xff;
		expamem_shutup(&gb->gfxboard_bank_memory);
		return;
	}
}

static void REGPARAM2 gfxboard_bput_mem_autoconfig (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48) {
		if (gb->board->configtype == 2) {
			addrbank *ab;
			if (ISP4()) {
				ab = &gb->gfxboard_bank_nbsmemory;
				copyvrambank(ab, gb->gfxmem_bank);
				map_banks_z2 (ab, b, 0x00200000 >> 16);
				if (gb->configured_mem <= 0) {
					gb->configured_mem = b;
					gb->gfxboardmem_start = b << 16;
					init_board(gb);
					gb->mem_start[0] = b << 16;
					gb->mem_end[0] = gb->mem_start[0] + 0x00200000;
				} else {
					gb->gfxboard_bank_memory.bget = gfxboard_bget_mem;
					gb->gfxboard_bank_memory.bput = gfxboard_bput_mem;
					gb->mem_start[1] = b << 16;
					gb->mem_end[1] = gb->mem_start[1] + 0x00200000;
				}
			} else {
				ab = &gb->gfxboard_bank_memory;
				gb->gfxboard_bank_memory.bget = gfxboard_bget_mem;
				gb->gfxboard_bank_memory.bput = gfxboard_bput_mem;
				gb->gfxboard_bank_memory.wget = gfxboard_wget_mem;
				gb->gfxboard_bank_memory.wput = gfxboard_wput_mem;
				gb->gfxboardmem_start = b << 16;
				init_board (gb);
				copyvrambank(ab, gb->gfxmem_bank);
				map_banks_z2 (ab, b, gb->board->banksize >> 16);
				gb->configured_mem = b;
				gb->mem_start[0] = b << 16;
				gb->mem_end[0] = gb->mem_start[0] + gb->board->banksize;
			}
			expamem_next (ab, NULL);
		} else {
			gb->expamem_lo = b & 0xff;
		}
		return;
	}
	if (addr == 0x4c) {
		gb->configured_mem = 0xff;
		expamem_shutup(&gb->gfxboard_bank_memory);
		return;
	}
}

static uaecptr mungeaddr (struct rtggfxboard *gb, uaecptr addr, bool write)
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
		if (gb->board->manufacturer == BOARD_MANUFACTURER_PICASSO) {
			if (addr == 0x1001) {
				gb->gfxboard_intena = true;
				return 0;
			}
			if (addr == 0x1000) {
				gb->gfxboard_intena = false;
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
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u32 v = 0xffffffff;
	addr = mungeaddr (gb, addr, false);
	if (addr)
		v = gb->vgaio->read (&gb->vga, addr, 4);
	return v;
}
static uae_u32 REGPARAM2 gfxboard_wget_regs (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u16 v = 0xffff;
	addr = mungeaddr (gb, addr, false);
	if (addr) {
		uae_u8 v1, v2;
		v1  = gb->vgaio->read (&gb->vga, addr + 0, 1);
		v1 = bget_regtest (gb, addr + 0, v1);
		v2 = gb->vgaio->read (&gb->vga, addr + 1, 1);
		v2 = bget_regtest (gb, addr + 1, v2);
		v = (v1 << 8) | v2;
	}
	return v;
}
static uae_u32 REGPARAM2 gfxboard_bget_regs (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u8 v = 0xff;
	addr &= 65535;
	if (addr >= 0x8000) {
		write_log (_T("GFX SPECIAL BGET IO %08X\n"), addr);
		return 0;
	}
	addr = mungeaddr (gb, addr, false);
	if (addr) {
		v = gb->vgaio->read (&gb->vga, addr, 1);
		v = bget_regtest (gb, addr, v);
#if REGDEBUG
		write_log(_T("GFX VGA BYTE GET IO %04X = %02X PC=%08x\n"), addr & 65535, v & 0xff, M68K_GETPC);
#endif
	}
	return v;
}

static void REGPARAM2 gfxboard_lput_regs (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);

	addr = mungeaddr (gb, addr, true);
	if (addr) {
		gb->vgaio->write (&gb->vga, addr + 0, l >> 24, 1);
		bput_regtest (gb, addr + 0, (l >> 24));
		gb->vgaio->write (&gb->vga, addr + 1, (l >> 16) & 0xff, 1);
		bput_regtest (gb, addr + 0, (l >> 16));
		gb->vgaio->write (&gb->vga, addr + 2, (l >>  8) & 0xff, 1);
		bput_regtest (gb, addr + 0, (l >>  8));
		gb->vgaio->write (&gb->vga, addr + 3, (l >>  0) & 0xff, 1);
		bput_regtest (gb, addr + 0, (l >>  0));
#if REGDEBUG
		write_log(_T("GFX VGA LONG PUT IO %04X = %04X PC=%08x\n"), addr & 65535, l, M68K_GETPC);
#endif
	}
}
static void REGPARAM2 gfxboard_wput_regs (uaecptr addr, uae_u32 w)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr = mungeaddr (gb, addr, true);
	if (addr) {
		gb->vgaio->write (&gb->vga, addr + 0, (w >> 8) & 0xff, 1);
		bput_regtest (gb, addr + 0, (w >> 8));
		gb->vgaio->write (&gb->vga, addr + 1, (w >> 0) & 0xff, 1);
		bput_regtest (gb, addr + 1, (w >> 0));
#if REGDEBUG
		write_log(_T("GFX VGA WORD PUT IO %04X = %04X PC=%08x\n"), addr & 65535, w & 0xffff, M68K_GETPC);
#endif
	}
}
static void REGPARAM2 gfxboard_bput_regs (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr &= 65535;
	if (addr >= 0x8000) {
		write_log (_T("GFX SPECIAL BPUT IO %08X = %02X\n"), addr, b & 0xff);
		switch (gb->board->manufacturer)
		{
		case BOARD_MANUFACTURER_PICASSO:
			{
				if ((addr & 1) == 0) {
					int idx = addr >> 12;
					if (idx == 0x0b || idx == 0x09) {
						set_monswitch(gb, false);
					} else if (idx == 0x0a || idx == 0x08) {
						set_monswitch(gb, true);
					}
				}
			}
		break;
		case BOARD_MANUFACTURER_PICCOLO:
		case BOARD_MANUFACTURER_SPECTRUM:
			set_monswitch(gb, (b & 0x20) != 0);
			gb->gfxboard_intena = (b & 0x40) != 0;
		break;
		}
		return;
	}
	addr = mungeaddr (gb, addr, true);
	if (addr) {
		gb->vgaio->write (&gb->vga, addr, b & 0xff, 1);
		bput_regtest (gb, addr, b);
#if REGDEBUG
		write_log(_T("GFX VGA BYTE PUT IO %04X = %02X PC=%08x\n"), addr & 65535, b & 0xff, M68K_GETPC);
#endif
	}
}

static uae_u32 REGPARAM2 gfxboard_bget_regs_autoconfig (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u32 v = 0;
	addr &= 65535;
	if (addr < GFXBOARD_AUTOCONFIG_SIZE)
		v = gb->automemory[addr];
	return v;
}

static void REGPARAM2 gfxboard_bput_regs_autoconfig (uaecptr addr, uae_u32 b)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addrbank *ab;
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48) {
		gb->gfxboard_bank_registers.bget = gfxboard_bget_regs;
		gb->gfxboard_bank_registers.bput = gfxboard_bput_regs;
		if (gb->p4z2) {
			ab = &gb->gfxboard_bank_special;
			map_banks_z2(ab, b, gb->gfxboard_bank_special.reserved_size >> 16);
			gb->io_start = b << 16;
			gb->io_end = gb->io_start + gb->gfxboard_bank_special.reserved_size;
		} else {
			ab = &gb->gfxboard_bank_registers;
			map_banks_z2(ab, b, gb->gfxboard_bank_registers.reserved_size >> 16);
			gb->io_start = b << 16;
			gb->io_end = gb->io_start + gb->gfxboard_bank_registers.reserved_size;
		}
		gb->configured_regs = b;
		expamem_next (ab, NULL);
		return;
	}
	if (addr == 0x4c) {
		gb->configured_regs = 0xff;
		expamem_next (NULL, NULL);
		return;
	}
}

static void gfxboard_free_board(struct rtggfxboard *gb)
{
	if (gb->rbc) {
		if (gb->func) {
			if (gb->userdata)
				gb->func->free(gb->userdata);
			gfxboard_free_slot2(gb);
			gb->rbc = NULL;
			return;
		}
	}
	if (gb->vram) {
		gb->gfxmem_bank->baseaddr = gb->vramrealstart;
		gfxboard_free_vram(gb->rbc->rtg_index);
		gb->gfxmem_bank = NULL;
	}
	gb->vram = NULL;
	gb->vramrealstart = NULL;
	xfree(gb->fakesurface_surface);
	gb->fakesurface_surface = NULL;
	gb->configured_mem = 0;
	gb->configured_regs = 0;
	gb->monswitch_new = false;
	gb->monswitch_current = false;
	gb->monswitch_delay = -1;
	gb->monswitch_reset = true;
	gb->modechanged = false;
	gb->gfxboard_vblank = false;
	gb->gfxboard_intena = false;
	gb->picassoiv_bank = 0;
	gb->active = false;
}

void gfxboard_free(void)
{
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		struct rtggfxboard *gb = &rtggfxboards[i];
		gfxboard_free_board(gb);
	}
}

void gfxboard_reset (void)
{
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		struct rtggfxboard *gb = &rtggfxboards[i];
		gb->rbc = &currprefs.rtgboards[gb->rtg_index];
		if (gb->func) {
			if (gb->userdata) {
				gb->func->reset(gb->userdata);
			}
			gfxboard_free_board(gb);
		} else {
			if (gb->rbc->rtgmem_type >= GFXBOARD_HARDWARE) {
				gb->board = &boards[gb->rbc->rtgmem_type - GFXBOARD_HARDWARE];
			}
			gfxboard_free_board(gb);
			if (gb->board) {
				if (gb->board->configtype == 3)
					gb->gfxboard_bank_memory.wput = gfxboard_wput_mem_autoconfig;
				if (reset_func) 
					reset_func (reset_parm);
			}
		}
		if (gb->monitor_id > 0) {
			close_rtg(gb->monitor_id);
		}
	}
	for (int i = 0; i < MAX_AMIGADISPLAYS; i++) {
		rtg_visible[i] = -1;
		rtg_initial[i] = -1;
	}
}

static uae_u32 REGPARAM2 gfxboards_lget_regs (uaecptr addr)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u32 v = 0;
	addr &= gb->p4_special_mask;
	// pci config
	if (addr >= 0x400000 || (gb->p4z2 && !(gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			v = gb->p4_pci[addr2 + 0] << 24;
			v |= gb->p4_pci[addr2 + 1] << 16;
			v |= gb->p4_pci[addr2 + 2] <<  8;
			v |= gb->p4_pci[addr2 + 3] <<  0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI LGET %08x %08x\n"), addr, v);
#endif
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
			v = gb->cirrus_pci[addr2 + 0] << 24;
			v |= gb->cirrus_pci[addr2 + 1] << 16;
			v |= gb->cirrus_pci[addr2 + 2] <<  8;
			v |= gb->cirrus_pci[addr2 + 3] <<  0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI LGET %08x %08x\n"), addr, v);
#endif
		}
		return v;
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= gb->p4_mmiobase && addr < gb->p4_mmiobase + 0x8000) {
			uae_u32 addr2 = addr - gb->p4_mmiobase;
			v  = gb->vgammio->read(&gb->vga, addr2 + 0, 1) << 24;
			v |= gb->vgammio->read(&gb->vga, addr2 + 1, 1) << 16;
			v |= gb->vgammio->read(&gb->vga, addr2 + 2, 1) <<  8;
			v |= gb->vgammio->read(&gb->vga, addr2 + 3, 1) <<  0;
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
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u16 v = 0;
	addr &= gb->p4_special_mask;
	// pci config
	if (addr >= 0x400000 || (gb->p4z2 && !(gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			v = gb->p4_pci[addr2 + 0] << 8;
			v |= gb->p4_pci[addr2 + 1] << 0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI WGET %08x %04x\n"), addr, v);
#endif
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
			v = gb->cirrus_pci[addr2 + 0] << 8;
			v |= gb->cirrus_pci[addr2 + 1] << 0;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI WGET %08x %04x\n"), addr, v);
#endif
		}
		return v;
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= gb->p4_mmiobase && addr < gb->p4_mmiobase + 0x8000) {
			uae_u32 addr2 = addr - gb->p4_mmiobase;
			v  = gb->vgammio->read(&gb->vga, addr2 + 0, 1) << 8;
			v |= gb->vgammio->read(&gb->vga, addr2 + 1, 1) << 0;
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
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u8 v = 0xff;
	addr &= gb->p4_special_mask;

#if PICASSOIV_DEBUG_IO > 1
	write_log(_T("PicassoIV CL REG BGET %08x PC=%08x\n"), addr, M68K_GETPC);
#endif

	// pci config
	if (addr >= 0x400000 || (gb->p4z2 && !(gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		v = 0;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			if (addr2 == 0x802) {
				v = 2; // bridge version?
			} else if (addr2 == 0x808) {
				v = 4; // bridge revision
			} else {
				addr2 -= 0x800;
				v = gb->p4_pci[addr2];
			}
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI BGET %08x %02x\n"), addr, v);
#endif
		} else if (addr2 >= 0x1000 && addr2 <= 0x1040) {
			addr2 -= 0x1000;
			v = gb->cirrus_pci[addr2];
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI BGET %08x %02x\n"), addr, v);
#endif
		}
		return v;
	}

	if (gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= gb->p4_mmiobase && addr < gb->p4_mmiobase + 0x8000) {
			uae_u32 addr2 = addr - gb->p4_mmiobase;
			v = gb->vgammio->read(&gb->vga, addr2, 1);
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO BGET %08x %02x\n"), addr, v & 0xff);
#endif
			return v;
		}
	}
	if (addr == 0) {
		v = gb->picassoiv_bank;
		return v;
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) {
		v = 0;
		if (addr == 0x404) {
			v = 0x70; // FLIFI revision
			// FLIFI type in use
			if (currprefs.chipset_mask & CSMASK_AGA)
				v |= 4 | 8;
			else
				v |= 8;
		} else if (addr == 0x406) {
			// FLIFI I2C
			// bit 0 = clock out
			// bit 1 = data out
			// bit 2 = clock in
			// bit 7 = data in
			v = gb->p4i2c & 3;
			if (v & 1)
				v |= 4;
			if (v & 2)
				v |= 0x80;
		} else if (addr == 0x408) {
			v = gb->gfxboard_vblank ? 0x80 : 0;
		} else if (gb->p4z2 && addr >= 0x10000) {
			addr -= 0x10000;
			uaecptr addr2 = mungeaddr (gb, addr, true);
			if (addr2) {
				v = gb->vgaio->read (&gb->vga, addr2, 1);
				v = bget_regtest (gb, addr2, v);
				//write_log(_T("P4 VGA read %08X=%02X PC=%08x\n"), addr2, v, M68K_GETPC);
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
			v = gb->automemory[addr];
			return v;
		}
		addr -= PICASSOIV_FLASH_OFFSET;
		addr /= 2;
		v = gb->automemory[addr + PICASSOIV_FLASH_OFFSET + ((gb->picassoiv_bank & PICASSOIV_BANK_FLASHBANK) ? 0x8000 : 0)];
	}
	return v;
}
static void REGPARAM2 gfxboards_lput_regs (uaecptr addr, uae_u32 l)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	addr &= gb->p4_special_mask;
	if (addr >= 0x400000 || (gb->p4z2 && !(gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI LPUT %08x %08x\n"), addr, l);
#endif
			gb->p4_pci[addr2 + 0] = l >> 24;
			gb->p4_pci[addr2 + 1] = l >> 16;
			gb->p4_pci[addr2 + 2] = l >>  8;
			gb->p4_pci[addr2 + 3] = l >>  0;
			p4_pci_check (gb);
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI LPUT %08x %08x\n"), addr, l);
#endif
			gb->cirrus_pci[addr2 + 0] = l >> 24;
			gb->cirrus_pci[addr2 + 1] = l >> 16;
			gb->cirrus_pci[addr2 + 2] = l >>  8;
			gb->cirrus_pci[addr2 + 3] = l >>  0;
			reset_pci (gb);
		}
		return;
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= gb->p4_mmiobase && addr < gb->p4_mmiobase + 0x8000) {
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO LPUT %08x %08x\n"), addr, l);
#endif
			uae_u32 addr2 = addr - gb->p4_mmiobase;
			gb->vgammio->write(&gb->vga, addr2 + 0, l >> 24, 1);
			gb->vgammio->write(&gb->vga, addr2 + 1, l >> 16, 1);
			gb->vgammio->write(&gb->vga, addr2 + 2, l >>  8, 1);
			gb->vgammio->write(&gb->vga, addr2 + 3, l >>  0, 1);
			return;
		}
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV LPUT %08x %08x\n"), addr, l);
#endif
}
static void REGPARAM2 gfxboards_wput_regs (uaecptr addr, uae_u32 v)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u16 w = (uae_u16)v;
	addr &= gb->p4_special_mask;
	if (addr >= 0x400000 || (gb->p4z2 && !(gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI WPUT %08x %04x\n"), addr, w & 0xffff);
#endif
			gb->p4_pci[addr2 + 0] = w >> 8;
			gb->p4_pci[addr2 + 1] = w >> 0;
			p4_pci_check (gb);
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI WPUT %08x %04x\n"), addr, w & 0xffff);
#endif
			gb->cirrus_pci[addr2 + 0] = w >> 8;
			gb->cirrus_pci[addr2 + 1] = w >> 0;
			reset_pci (gb);
		}
		return;
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= gb->p4_mmiobase && addr < gb->p4_mmiobase + 0x8000) {
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO LPUT %08x %08x\n"), addr, w & 0xffff);
#endif
			uae_u32 addr2 = addr - gb->p4_mmiobase;
			gb->vgammio->write(&gb->vga, addr2 + 0, w >> 8, 1);
			gb->vgammio->write(&gb->vga, addr2 + 1, (w >> 0) & 0xff, 1);
			return;
		}
	}
	if (gb->p4z2 && addr >= 0x10000) {
		addr -= 0x10000;
		addr = mungeaddr (gb, addr, true);
		if (addr) {
			gb->vgaio->write (&gb->vga, addr + 0, w >> 8, 1);
			bput_regtest (gb, addr + 0, w >> 8);
			gb->vgaio->write (&gb->vga, addr + 1, (w >> 0) & 0xff, 1);
			bput_regtest (gb, addr + 1, w >> 0);
		}
		return;
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV WPUT %08x %04x\n"), addr, w & 0xffff);
#endif
}

static void REGPARAM2 gfxboards_bput_regs (uaecptr addr, uae_u32 v)
{
	struct rtggfxboard *gb = getgfxboard(addr);
	uae_u8 b = (uae_u8)v;
	addr &= gb->p4_special_mask;

#if PICASSOIV_DEBUG_IO > 1
	write_log(_T("PicassoIV CL REG BPUT %08x %02x PC=%08x\n"), addr, v, M68K_GETPC);
#endif

	if (addr >= 0x400000 || (gb->p4z2 && !(gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) && (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) && ((addr >= 0x800 && addr < 0xc00) || (addr >= 0x1000 && addr < 0x2000)))) {
		uae_u32 addr2 = addr & 0xffff;
		if (addr2 >= 0x0800 && addr2 < 0x840) {
			addr2 -= 0x800;
			gb->p4_pci[addr2] = b;
			p4_pci_check (gb);
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV PCI BPUT %08x %02x\n"), addr, b & 0xff);
#endif
		} else if (addr2 >= 0x1000 && addr2 < 0x1040) {
			addr2 -= 0x1000;
			gb->cirrus_pci[addr2] = b;
			reset_pci (gb);
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV CL PCI BPUT %08x %02x\n"), addr, b & 0xff);
#endif
		}
		return;
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_UNMAPFLASH) {
		if (addr == 0x404) {
			gb->picassoiv_flifi = b;
			picassoiv_checkswitch (gb);
		} else if (addr == 0x406) {
			gb->p4i2c = b;
		}
	}
	if (gb->picassoiv_bank & PICASSOIV_BANK_MAPRAM) {
		// memory mapped io
		if (addr >= gb->p4_mmiobase && addr < gb->p4_mmiobase + 0x8000) {
#if PICASSOIV_DEBUG_IO
			write_log (_T("PicassoIV MMIO BPUT %08x %08x\n"), addr, b & 0xff);
#endif
			uae_u32 addr2 = addr - gb->p4_mmiobase;
			gb->vgammio->write(&gb->vga, addr2, b, 1);
			return;
		}
	}
	if (gb->p4z2 && addr >= 0x10000) {
		addr -= 0x10000;
		addr = mungeaddr (gb, addr, true);
		if (addr) {
			gb->vgaio->write (&gb->vga, addr, b & 0xff, 1);
			bput_regtest (gb, addr, b);
			//write_log(_T("P4 VGA write %08x=%02x PC=%08x\n"), addr, b & 0xff, M68K_GETPC);
		}
		return;
	}
#if PICASSOIV_DEBUG_IO
	write_log (_T("PicassoIV BPUT %08x %02X\n"), addr, b & 0xff);
#endif
	if (addr == 0) {
		gb->picassoiv_bank = b;
	}
}

const TCHAR *gfxboard_get_name(int type)
{
	if (type == GFXBOARD_UAE_Z2)
		return _T("UAE Zorro II");
	if (type == GFXBOARD_UAE_Z3)
		return _T("UAE Zorro III");
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

struct gfxboard_func *gfxboard_get_func(struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type == GFXBOARD_UAE_Z2)
		return NULL;
	if (type == GFXBOARD_UAE_Z3)
		return NULL;
	return boards[type - GFXBOARD_HARDWARE].func;
}

int gfxboard_get_configtype(struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type == GFXBOARD_UAE_Z2)
		return 2;
	if (type == GFXBOARD_UAE_Z3)
		return 3;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - GFXBOARD_HARDWARE];
	return gb->board->configtype;
}

bool gfxboard_need_byteswap (struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type < GFXBOARD_HARDWARE)
		return false;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - GFXBOARD_HARDWARE];
	return gb->board->swap;
}

int gfxboard_get_autoconfig_size(struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type == GFXBOARD_PICASSO4_Z3)
		return 32 * 1024 * 1024;
	return -1;
}

int gfxboard_get_vram_min (struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type < GFXBOARD_HARDWARE)
		return -1;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - GFXBOARD_HARDWARE];
	return gb->board->vrammin;
}

int gfxboard_get_vram_max (struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type < GFXBOARD_HARDWARE)
		return -1;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - GFXBOARD_HARDWARE];
	return gb->board->vrammax;
}

bool gfxboard_is_registers (struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type < 2)
		return false;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - 2];
	return gb->board->model_registers != 0;
}

int gfxboard_num_boards (struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type < 2)
		return 1;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - 2];
	if (type == GFXBOARD_PICASSO4_Z2) {
		if (rbc->rtgmem_size < 0x400000)
			return 2;
		return 3;
	}
	if (gb->board->model_registers == 0)
		return 1;
	return 2;
}

uae_u32 gfxboard_get_romtype(struct rtgboardconfig *rbc)
{
	int type = rbc->rtgmem_type;
	if (type < 2)
		return 0;
	struct rtggfxboard *gb = &rtggfxboards[rbc->rtg_index];
	gb->board = &boards[type - 2];
	return gb->board->romtype;
}

static void gfxboard_init (struct autoconfig_info *aci, struct rtggfxboard *gb)
{
	struct uae_prefs *p = aci->prefs;
	gb->rtg_index = aci->devnum;
	if (!gb->automemory)
		gb->automemory = xmalloc (uae_u8, GFXBOARD_AUTOCONFIG_SIZE);
	memset (gb->automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	struct rtgboardconfig *rbc = &p->rtgboards[gb->rtg_index];
	gb->rbc = rbc;
	gb->monitor_id = rbc->monitor_id;
	if (!aci->doinit)
		return;
	gb->gfxmem_bank = gfxmem_banks[gb->rtg_index];
	gb->gfxmem_bank->mask = gb->rbc->rtgmem_size - 1;
	gb->p4z2 = false;
	zfile_fclose (gb->p4rom);
	gb->p4rom = NULL;
	gb->banksize_mask = gb->board->banksize - 1;
	memset (gb->cirrus_pci, 0, sizeof gb->cirrus_pci);
	reset_pci (gb);
}

static void copyp4autoconfig (struct rtggfxboard *gb, int startoffset)
{
	int size = 0;
	int offset = 0;
	memset (gb->automemory, 0xff, 64);
	while (size < 32) {
		uae_u8 b = gb->p4autoconfig[size + startoffset];
		gb->automemory[offset] = b;
		offset += 2;
		size++;
	}
}

static void loadp4rom (struct rtggfxboard *gb)
{
	int size, offset;
	uae_u8 b;
	// rom loader code
	zfile_fseek (gb->p4rom, 256, SEEK_SET);
	offset = PICASSOIV_ROM_OFFSET;
	size = 0;
	while (size < 4096 - 256) {
		if (!zfile_fread (&b, 1, 1, gb->p4rom))
			break;
		gb->automemory[offset] = b;
		offset += 2;
		size++;
	}
	// main flash code
	zfile_fseek (gb->p4rom, 16384, SEEK_SET);
	zfile_fread (&gb->automemory[PICASSOIV_FLASH_OFFSET], 1, PICASSOIV_MAX_FLASH, gb->p4rom);
	zfile_fclose (gb->p4rom);
	gb->p4rom = NULL;
	write_log (_T("PICASSOIV: flash rom loaded\n"));
}

bool gfxboard_init_memory (struct autoconfig_info *aci)
{
	struct rtggfxboard *gb = &rtggfxboards[aci->devnum];
	int bank;
	uae_u8 z2_flags, z3_flags, type;
	struct uae_prefs *p = aci->prefs;

	gfxboard_init (aci, gb);

	memset (gb->automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	
	z2_flags = 0x05; // 1M
	z3_flags = 0x06; // 1M
	bank = gb->board->banksize;
	bank /= 0x00100000;
	while (bank > 1) {
		z2_flags++;
		z3_flags++;
		bank >>= 1;
	}
	if (gb->board->configtype == 3) {
		type = 0x00 | 0x08 | 0x80; // 16M Z3
		ew (gb, 0x08, z3_flags | 0x10 | 0x20);
	} else {
		type = z2_flags | 0x08 | 0xc0;
	}
	ew (gb, 0x04, gb->board->model_memory);
	ew (gb, 0x10, gb->board->manufacturer >> 8);
	ew (gb, 0x14, gb->board->manufacturer);

	uae_u32 ser = gb->board->serial;
	ew (gb, 0x18, ser >> 24); /* ser.no. Byte 0 */
	ew (gb, 0x1c, ser >> 16); /* ser.no. Byte 1 */
	ew (gb, 0x20, ser >>  8); /* ser.no. Byte 2 */
	ew (gb, 0x24, ser >>  0); /* ser.no. Byte 3 */

	ew (gb, 0x00, type);

	if (ISP4()) {
		int roms[] = { 91, -1 };
		struct romlist *rl = getromlistbyids(roms, NULL);
		TCHAR path[MAX_DPATH];
		fetch_rompath (path, sizeof path / sizeof (TCHAR));

		gb->p4rom = read_device_rom(p, ROMTYPE_PICASSOIV, 0, roms);

		if (!gb->p4rom && p->picassoivromfile[0] && zfile_exists(p->picassoivromfile))
			gb->p4rom = read_rom_name(p->picassoivromfile);

		if (!gb->p4rom && rl)
			gb->p4rom = read_rom(rl->rd);

		if (!gb->p4rom) {
			_tcscat (path, _T("picasso_iv_flash.rom"));
			gb->p4rom = read_rom_name (path);
			if (!gb->p4rom)
				gb->p4rom = read_rom_name (_T("picasso_iv_flash.rom"));
		}
		if (gb->p4rom) {
			zfile_fread (gb->p4autoconfig, sizeof gb->p4autoconfig, 1, gb->p4rom);
			copyp4autoconfig (gb, gb->board->configtype == 3 ? 192 : 0);
			if (gb->board->configtype == 3) {
				loadp4rom (gb);
				gb->p4_mmiobase = 0x200000;
				gb->p4_special_mask = 0x7fffff;
			} else {
				gb->p4z2 = true;
				gb->p4_mmiobase = 0x8000;
				gb->p4_special_mask = 0x1ffff;
			}
			gb->gfxboard_intena = true;
		} else {
			error_log (_T("Picasso IV: '%s' flash rom image not found!\nAvailable from http://www.sophisticated-development.de/\nPIV_FlashImageXX -> picasso_iv_flash.rom"), path);
			gui_message (_T("Picasso IV: '%s' flash rom image not found!\nAvailable from http://www.sophisticated-development.de/\nPIV_FlashImageXX -> picasso_iv_flash.rom"), path);
		}
	}

	aci->label = gb->board->name;
	aci->direct_vram = true;
	aci->addrbank = &gb->gfxboard_bank_memory;
	if (gb->rbc->rtgmem_type == GFXBOARD_VGA) {
		aci->zorro = -1;
		if (gb->monitor_id > 0) {
			gb->monswitch_keep_trying = true;
		}
	}
	aci->parent = aci;
	if (!aci->doinit) {
		if (gb->rbc->rtgmem_type == GFXBOARD_VGA) {
			static const int parent[] = { ROMTYPE_A1060, ROMTYPE_A2088, ROMTYPE_A2088T, ROMTYPE_A2286, ROMTYPE_A2386, 0 };
			aci->parent_romtype = parent;
		} else {
			memcpy(aci->autoconfig_raw, gb->automemory, sizeof aci->autoconfig_raw);
		}
		return true;
	}

	gb->configured_mem = -1;
	gb->rtg_index = aci->devnum;

	total_active_gfx_boards = 0;
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		if (p->rtgboards[i].rtgmem_size && p->rtgboards[i].rtgmem_type >= GFXBOARD_HARDWARE) {
			total_active_gfx_boards++;
		}
	}
	only_gfx_board = NULL;
	if (total_active_gfx_boards == 1)
		only_gfx_board = gb;


	_stprintf(gb->memorybankname, _T("%s VRAM"), gb->board->name);
	_stprintf(gb->memorybanknamenojit, _T("%s VRAM NOJIT"), gb->board->name);
	_stprintf(gb->wbsmemorybankname, _T("%s VRAM WORDSWAP"), gb->board->name);
	_stprintf(gb->lbsmemorybankname, _T("%s VRAM LONGSWAP"), gb->board->name);
	_stprintf(gb->regbankname, _T("%s REG"), gb->board->name);

	memcpy(&gb->gfxboard_bank_memory, &tmpl_gfxboard_bank_memory, sizeof addrbank);
	memcpy(&gb->gfxboard_bank_wbsmemory, &tmpl_gfxboard_bank_wbsmemory, sizeof addrbank);
	memcpy(&gb->gfxboard_bank_lbsmemory, &tmpl_gfxboard_bank_lbsmemory, sizeof addrbank);
	memcpy(&gb->gfxboard_bank_nbsmemory, &tmpl_gfxboard_bank_nbsmemory, sizeof addrbank);
	memcpy(&gb->gfxboard_bank_registers, &tmpl_gfxboard_bank_registers, sizeof addrbank);
	memcpy(&gb->gfxboard_bank_special, &tmpl_gfxboard_bank_special, sizeof addrbank);
	memcpy(&gb->gfxboard_bank_memory_nojit, &tmpl_gfxboard_bank_memory_nojit, sizeof addrbank);
	
	gb->gfxboard_bank_memory.name = gb->memorybankname;
	gb->gfxboard_bank_memory_nojit.name = gb->memorybanknamenojit;
	gb->gfxboard_bank_wbsmemory.name = gb->wbsmemorybankname;
	gb->gfxboard_bank_lbsmemory.name = gb->lbsmemorybankname;
	gb->gfxboard_bank_registers.name = gb->regbankname;

	gb->gfxboard_bank_memory.bget = gfxboard_bget_mem_autoconfig;
	gb->gfxboard_bank_memory.bput = gfxboard_bput_mem_autoconfig;
	gb->gfxboard_bank_memory.wput = gfxboard_wput_mem_autoconfig;

	gb->active = true;

	if (gb->rbc->rtgmem_type == GFXBOARD_VGA) {
		init_board(gb);
		gb->configured_mem = 1;
		gb->configured_regs = 1;
		return true;
	}

	return true;
}

bool gfxboard_init_memory_p4_z2 (struct autoconfig_info *aci)
{
	struct rtggfxboard *gb = &rtggfxboards[aci->devnum];
	if (gb->board->configtype == 3) {
		aci->addrbank = &expamem_null;
		return true;
	}

	copyp4autoconfig (gb, 64);
	memcpy(aci->autoconfig_raw, gb->automemory, sizeof aci->autoconfig_raw);
	aci->addrbank = &gb->gfxboard_bank_memory;
	aci->label = gb->board->name;
	aci->parent_of_previous = true;
	aci->direct_vram = true;

	if (!aci->doinit)
		return true;

	memcpy(&gb->gfxboard_bank_memory, &tmpl_gfxboard_bank_memory, sizeof addrbank);
	gb->gfxboard_bank_memory.bget = gfxboard_bget_mem_autoconfig;
	gb->gfxboard_bank_memory.bput = gfxboard_bput_mem_autoconfig;
	return true;
}

bool gfxboard_init_registers (struct autoconfig_info *aci)
{
	struct rtggfxboard *gb = &rtggfxboards[aci->devnum];
	if (!gb->board->model_registers) {
		aci->addrbank = &expamem_null;
		return true;
	}

	memset (gb->automemory, 0xff, GFXBOARD_AUTOCONFIG_SIZE);
	ew (gb, 0x00, 0xc0 | 0x01); // 64k Z2
	ew (gb, 0x04, gb->board->model_registers);
	ew (gb, 0x10, gb->board->manufacturer >> 8);
	ew (gb, 0x14, gb->board->manufacturer);

	uae_u32 ser = gb->board->serial;
	ew (gb, 0x18, ser >> 24); /* ser.no. Byte 0 */
	ew (gb, 0x1c, ser >> 16); /* ser.no. Byte 1 */
	ew (gb, 0x20, ser >>  8); /* ser.no. Byte 2 */
	ew (gb, 0x24, ser >>  0); /* ser.no. Byte 3 */

	if (ISP4()) {
		uae_u8 v;
		copyp4autoconfig (gb, 128);
		loadp4rom (gb);
		if (aci->doinit) {
			v = (((gb->automemory[0] & 0xf0) | (gb->automemory[2] >> 4)) & 3) - 1;
			gb->gfxboard_bank_special.reserved_size = 0x10000 << v;
		}
	}

	memcpy(aci->autoconfig_raw, gb->automemory, sizeof aci->autoconfig_raw);
	aci->label = gb->board->name;

	aci->addrbank = &gb->gfxboard_bank_registers;
	aci->parent_of_previous = true;

	if (!aci->doinit)
		return true;

	gb->gfxboard_bank_registers.bget = gfxboard_bget_regs_autoconfig;
	gb->gfxboard_bank_registers.bput = gfxboard_bput_regs_autoconfig;

	gb->gfxboard_bank_registers.reserved_size = BOARD_REGISTERS_SIZE;
	gb->configured_regs = -1;

	return true;
}
