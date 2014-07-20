/*
* UAE - The Un*x Amiga Emulator
*
* Misc accelerator board special features
* Blizzard 1230 IV, 1240/1260, 2060
*
* Copyright 2014 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "zfile.h"
#include "rommgr.h"
#include "autoconf.h"
#include "cpuboard.h"
#include "custom.h"
#include "newcpu.h"

#define BLIZZARD_RAM_BASE 0x68000000
#define BLIZZARD_RAM_ALIAS_BASE 0x48000000
#define BLIZZARD_MAPROM_BASE 0x4ff80000
#define BLIZZARD_MAPROM_ENABLE 0x80ffff00
#define BLIZZARD_BOARD_DISABLE 0x80fa0000

static int cpuboard_size = -1, cpuboard2_size = -1;
static int configured;
static int blizzard_jit;
static int maprom_state;
static uae_u32 maprom_base;
static int delayed_rom_protect;
static int f0rom_size, earom_size;

static bool is_blizzard(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV || currprefs.cpuboard_type == BOARD_BLIZZARD_1260 || currprefs.cpuboard_type == BOARD_BLIZZARD_2060; 
}

extern addrbank blizzardram_bank;
extern addrbank blizzardram_nojit_bank;
extern addrbank blizzardmaprom_bank;
extern addrbank blizzardea_bank;
extern addrbank blizzardf0_bank;

MEMORY_FUNCTIONS(blizzardram);

addrbank blizzardram_bank = {
	blizzardram_lget, blizzardram_wget, blizzardram_bget,
	blizzardram_lput, blizzardram_wput, blizzardram_bput,
	blizzardram_xlate, blizzardram_check, NULL, _T("Blizzard RAM"),
	blizzardram_lget, blizzardram_wget, ABFLAG_RAM
};

MEMORY_BGET(blizzardram_nojit, 1);
MEMORY_WGET(blizzardram_nojit, 1);
MEMORY_LGET(blizzardram_nojit, 1);
MEMORY_CHECK(blizzardram_nojit);
MEMORY_XLATE(blizzardram_nojit);

static void REGPARAM2 blizzardram_nojit_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardram_nojit_bank.mask;
	if (maprom_state && addr >= maprom_base)
		return;
	m = (uae_u32 *)(blizzardram_nojit_bank.baseaddr + addr);
	do_put_mem_long(m, l);
}
static void REGPARAM2 blizzardram_nojit_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardram_nojit_bank.mask;
	if (maprom_state && addr >= maprom_base)
		return;
	m = (uae_u16 *)(blizzardram_nojit_bank.baseaddr + addr);
	do_put_mem_word(m, w);
}
static void REGPARAM2 blizzardram_nojit_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardram_nojit_bank.mask;
	if (maprom_state && addr >= maprom_base)
		return;
	blizzardram_nojit_bank.baseaddr[addr] = b;
}

static addrbank blizzardram_nojit_bank = {
	blizzardram_nojit_lget, blizzardram_nojit_wget, blizzardram_nojit_bget,
	blizzardram_nojit_lput, blizzardram_nojit_wput, blizzardram_nojit_bput,
	blizzardram_nojit_xlate, blizzardram_nojit_check, NULL, _T("Blizzard RAM"),
	blizzardram_nojit_lget, blizzardram_nojit_wget, ABFLAG_RAM
};

MEMORY_BGET(blizzardmaprom, 1);
MEMORY_WGET(blizzardmaprom, 1);
MEMORY_LGET(blizzardmaprom, 1);
MEMORY_CHECK(blizzardmaprom);
MEMORY_XLATE(blizzardmaprom);

static void no_rom_protect(void)
{
	if (delayed_rom_protect)
		return;
	delayed_rom_protect = 10;
	protect_roms(false);
}

static void REGPARAM2 blizzardmaprom_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardmaprom_bank.mask;
	m = (uae_u32 *)(blizzardmaprom_bank.baseaddr + addr);
	do_put_mem_long(m, l);
	if (maprom_state) {
		no_rom_protect();
		m = (uae_u32 *)(kickmem_bank.baseaddr + addr);
		do_put_mem_long(m, l);
	}
}
static void REGPARAM2 blizzardmaprom_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardmaprom_bank.mask;
	m = (uae_u16 *)(blizzardmaprom_bank.baseaddr + addr);
	do_put_mem_word(m, w);
	if (maprom_state) {
		no_rom_protect();
		m = (uae_u16 *)(kickmem_bank.baseaddr + addr);
		do_put_mem_word(m, w);
	}
}
static void REGPARAM2 blizzardmaprom_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardmaprom_bank.mask;
	blizzardmaprom_bank.baseaddr[addr] = b;
	if (maprom_state) {
		no_rom_protect();
		kickmem_bank.baseaddr[addr] = b;
	}
}
static addrbank blizzardmaprom_bank = {
	blizzardmaprom_lget, blizzardmaprom_wget, blizzardmaprom_bget,
	blizzardmaprom_lput, blizzardmaprom_wput, blizzardmaprom_bput,
	blizzardmaprom_xlate, blizzardmaprom_check, NULL, _T("Blizzard MAPROM"),
	blizzardmaprom_lget, blizzardmaprom_wget, ABFLAG_RAM
};

static void REGPARAM3 blizzardea_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 blizzardea_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 blizzardea_bput(uaecptr, uae_u32) REGPARAM;

MEMORY_BGET(blizzardea, 0);
MEMORY_WGET(blizzardea, 0);
MEMORY_LGET(blizzardea, 0);
MEMORY_CHECK(blizzardea);
MEMORY_XLATE(blizzardea);

static addrbank blizzardea_bank = {
	blizzardea_lget, blizzardea_wget, blizzardea_bget,
	blizzardea_lput, blizzardea_wput, blizzardea_bput,
	blizzardea_xlate, blizzardea_check, NULL, _T("Blizzard EA Autoconfig"),
	blizzardea_lget, blizzardea_wget, ABFLAG_IO | ABFLAG_SAFE
};

static void REGPARAM3 blizzarde8_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 blizzarde8_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 blizzarde8_bput(uaecptr, uae_u32) REGPARAM;
static uae_u32 REGPARAM3 blizzarde8_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 blizzarde8_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 blizzarde8_bget(uaecptr) REGPARAM;
static int REGPARAM2 blizzarde8_check(uaecptr addr, uae_u32 size)
{
	return 0;
}
static uae_u8 *REGPARAM2 blizzarde8_xlate(uaecptr addr)
{
	return NULL;
}

static addrbank blizzarde8_bank = {
	blizzarde8_lget, blizzarde8_wget, blizzarde8_bget,
	blizzarde8_lput, blizzarde8_wput, blizzarde8_bput,
	blizzarde8_xlate, blizzarde8_check, NULL, _T("Blizzard E8 Autoconfig"),
	blizzarde8_lget, blizzarde8_wget, ABFLAG_IO | ABFLAG_SAFE
};

// Blizzard F0 ROM is really slow, shoud slow it down to see color flashing..

static uae_u32 REGPARAM2 blizzardf0_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 *m;

	addr &= blizzardf0_bank.mask;
	m = (uae_u32 *)(blizzardf0_bank.baseaddr + addr);
	return do_get_mem_long(m);
}
static uae_u32 REGPARAM2 blizzardf0_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u16 *m, v;

	addr &= blizzardf0_bank.mask;
	m = (uae_u16 *)(blizzardf0_bank.baseaddr + addr);
	v = do_get_mem_word(m);
	return v;
}
static uae_u32 REGPARAM2 blizzardf0_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v;
	addr &= blizzardf0_bank.mask;
	v = blizzardf0_bank.baseaddr[addr];
	return v;
}

MEMORY_CHECK(blizzardf0);
MEMORY_XLATE(blizzardf0);

static addrbank blizzardf0_bank = {
	blizzardf0_lget, blizzardf0_wget, blizzardf0_bget,
	blizzardea_lput, blizzardea_wput, blizzardea_bput,
	blizzardf0_xlate, blizzardf0_check, NULL, _T("Blizzard F00000"),
	blizzardf0_lget, blizzardf0_wget, ABFLAG_ROM
};

static void REGPARAM2 blizzardea_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzardea_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzardea_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}

static void REGPARAM2 blizzarde8_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzarde8_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzarde8_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48 && !configured) {
		map_banks(&blizzardea_bank, b, 0x20000 >> 16, 0x20000);
		write_log(_T("Blizzard Z2 autoconfigured at %02X0000\n"), b);
		configured = 1;
		expamem_next();
		return;
	}
	if (addr == 0x4c && !configured) {
		write_log(_T("Blizzard Z2 SHUT-UP!\n"));
		configured = 1;
		expamem_next();
		return;
	}
}
static uae_u32 REGPARAM2 blizzarde8_bget(uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = blizzardea_bank.baseaddr[addr & blizzardea_bank.mask];
	return v;
}
static uae_u32 REGPARAM2 blizzarde8_wget(uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (blizzardea_bank.baseaddr[addr & blizzardea_bank.mask] << 8) | blizzardea_bank.baseaddr[(addr + 1) & blizzardea_bank.mask];
	return v;
}
static uae_u32 REGPARAM2 blizzarde8_lget(uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (blizzarde8_wget(addr) << 16) | blizzarde8_wget(addr + 2);
	return v;
}

static void copymaprom(void)
{
	uae_u8 *src = get_real_address(BLIZZARD_MAPROM_BASE);
	uae_u8 *dst = get_real_address(0xf80000);
	protect_roms(false);
	memcpy(dst, src, 524288);
	protect_roms(true);
}

static uae_u32 REGPARAM2 blizzardio_bget(uaecptr addr)
{
	return 0;
}
static void REGPARAM2 blizzardio_bput(uaecptr addr, uae_u32 v)
{
	if ((addr & 65535) == (BLIZZARD_MAPROM_ENABLE & 65535)) {
		if (v != 0x42 || maprom_state || !currprefs.maprom)
			return;
		maprom_state = 1;
		write_log(_T("Blizzard MAPROM enabled\n"));
		copymaprom();
	}
}
static void REGPARAM2 blizzardio_wput(uaecptr addr, uae_u32 v)
{
	if((addr & 65535) == (BLIZZARD_BOARD_DISABLE & 65535)) {
		if (v != 0xcafe)
			return;
		write_log(_T("Blizzard board disable!\n"));
		cpu_halt(4); // not much choice..
	}
}

static addrbank blizzardio_bank = {
	blizzardio_bget, blizzardio_bget, blizzardio_bget,
	blizzardio_wput, blizzardio_wput, blizzardio_bput,
	default_xlate, default_check, NULL, _T("Blizzard IO"),
	blizzardio_bget, blizzardio_bget, ABFLAG_IO
};

void cpuboard_vsync(void)
{
	if (delayed_rom_protect <= 0)
		return;
	delayed_rom_protect--;
	if (delayed_rom_protect == 0)
		protect_roms(true);
}

void cpuboard_map(void)
{
	if (!currprefs.cpuboard_type)
		return;
	if (cpuboard_size) {
		if (blizzard_jit) {
			map_banks(&blizzardram_bank, blizzardram_bank.start >> 16, cpuboard_size >> 16, 0);
			map_banks(&blizzardram_bank, BLIZZARD_RAM_BASE >> 16, cpuboard_size >> 16, 0);
		} else {
			for (int i = 0; i < 0x08000000; i += cpuboard_size) {
				map_banks_nojitdirect(&blizzardram_nojit_bank, (BLIZZARD_RAM_ALIAS_BASE + i)  >> 16, cpuboard_size >> 16, 0);
				map_banks_nojitdirect(&blizzardram_nojit_bank, (BLIZZARD_RAM_BASE + i) >> 16, cpuboard_size >> 16, 0);
			}
			if (currprefs.maprom) {
				for (int i = 0; i < 0x08000000; i += cpuboard_size) {
					map_banks_nojitdirect(&blizzardmaprom_bank, (BLIZZARD_RAM_ALIAS_BASE + i + cpuboard_size - 524288) >> 16, 524288 >> 16, 0);
					map_banks_nojitdirect(&blizzardmaprom_bank, (BLIZZARD_RAM_BASE + i + cpuboard_size - 524288) >> 16, 524288 >> 16, 0);
				}
			}
		}
	}
	if (f0rom_size)
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, f0rom_size >> 16, 0);
	if (is_blizzard()) {
		map_banks(&blizzardio_bank, BLIZZARD_MAPROM_ENABLE >> 16, 65536 >> 16, 0);
		map_banks(&blizzardio_bank, BLIZZARD_BOARD_DISABLE >> 16, 65536 >> 16, 0);
	}
}

void cpuboard_reset(bool hardreset)
{
	canbang = 0;
	configured = false;
	delayed_rom_protect = 0;
	currprefs.cpuboardmem1_size = changed_prefs.cpuboardmem1_size;
	if (hardreset || !currprefs.maprom)
		maprom_state = 0;
}

void cpuboard_cleanup(void)
{
	configured = false;
	maprom_state = 0;

	if (blizzard_jit) {
		mapped_free(blizzardram_bank.baseaddr);
	} else {
		xfree(blizzardram_nojit_bank.baseaddr);
	}
	blizzardram_bank.baseaddr = NULL;
	blizzardram_nojit_bank.baseaddr = NULL;
	blizzardmaprom_bank.baseaddr = NULL;

	mapped_free(blizzardf0_bank.baseaddr);
	blizzardf0_bank.baseaddr = NULL;

	mapped_free(blizzardea_bank.baseaddr);
	blizzardea_bank.baseaddr = NULL;

	cpuboard_size = cpuboard2_size = -1;
}

void cpuboard_init(void)
{
	if (!currprefs.cpuboard_type)
		return;

	if (cpuboard_size == currprefs.cpuboardmem1_size)
		return;

	cpuboard_cleanup();

	cpuboard_size = currprefs.cpuboardmem1_size;

	if (is_blizzard()) {
		blizzardram_bank.start = BLIZZARD_RAM_ALIAS_BASE;
		blizzardram_bank.allocated = cpuboard_size;
		blizzardram_bank.mask = blizzardram_bank.allocated - 1;

		blizzardram_nojit_bank.start = blizzardram_bank.start;
		blizzardram_nojit_bank.allocated = blizzardram_bank.allocated;
		blizzardram_nojit_bank.mask = blizzardram_bank.mask;


		blizzard_jit = 0 && BLIZZARD_RAM_BASE + blizzardram_bank.allocated <= max_z3fastmem && currprefs.jit_direct_compatible_memory;
		if (blizzard_jit) {
			if (cpuboard_size)
				blizzardram_bank.baseaddr = mapped_malloc(blizzardram_bank.allocated, _T("blizzard"));
		} else {
			if (cpuboard_size)
				blizzardram_bank.baseaddr = xmalloc(uae_u8, blizzardram_bank.allocated);
		}
		blizzardram_nojit_bank.baseaddr = blizzardram_bank.baseaddr;

		blizzardmaprom_bank.baseaddr = blizzardram_bank.baseaddr + cpuboard_size - 524288;
		blizzardmaprom_bank.start = BLIZZARD_MAPROM_BASE;
		blizzardmaprom_bank.allocated = 524288;
		blizzardmaprom_bank.mask = 524288 - 1;

		maprom_base = blizzardram_bank.allocated - 524288;

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 262144;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		blizzardf0_bank.baseaddr = mapped_malloc(blizzardf0_bank.allocated, _T("rom_f0"));

		blizzardea_bank.allocated = 2 * 65536;
		blizzardea_bank.mask = blizzardea_bank.allocated - 1;
		// Blizzard 12xx autoconfig ROM must be mapped at $ea0000-$ebffff, board requires it.
		blizzardea_bank.baseaddr = mapped_malloc(blizzardea_bank.allocated, _T("rom_ea"));
	}
}

void cpuboard_clear(void)
{
}

bool cpuboard_maprom(void)
{
	if (!currprefs.cpuboard_type || !cpuboard_size)
		return false;
	if (maprom_state)
		copymaprom();
	return true;
}

addrbank *cpuboard_autoconfig_init(void)
{
	struct zfile *autoconfig_rom = NULL;
	int roms[2];
	bool autoconf = true;

	switch (currprefs.cpuboard_type)
	{
	case BOARD_BLIZZARD_1230_IV:
		roms[0] = 89;
		break;
	case BOARD_BLIZZARD_1260:
		roms[0] = 90;
		break;
	case BOARD_BLIZZARD_2060:
		roms[0] = 91;
		break;
	case BOARD_WARPENGINE_A4000:
		roms[0] = 93;
		break;
	default:
		expamem_next();
		return NULL;
	}
	roms[1] = -1;

	struct romlist *rl = getromlistbyids(roms);
	if (rl) {
		autoconfig_rom = read_rom(rl->rd);
	}
	if (!autoconfig_rom) {
		write_log (_T("ROM id %d not found for CPU board emulation\n"));
		expamem_next();
		return NULL;
	}
	protect_roms(false);
	if (currprefs.cpuboard_type == BOARD_BLIZZARD_2060) {
		f0rom_size = 65536;
		earom_size = 131072;
		// 2060 = 2x32k
		for (int i = 0; i < 32768; i++) {
			uae_u8 b = 0xff;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardf0_bank.baseaddr[i * 2 + 1] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardf0_bank.baseaddr[i * 2 + 0] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i * 2 + 1] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i * 2 + 0] = b;
		}
	} else if (currprefs.cpuboard_type == BOARD_WARPENGINE_A4000) {
		f0rom_size = 0;
		earom_size = 0;
		autoconf = false;
	} else {
		f0rom_size = 65536;
		earom_size = 131072;
		// 12xx = 1x32k
		for (int i = 0; i < 16384; i++) {
			uae_u8 b = 0xff;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardf0_bank.baseaddr[i] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i] = b;
		}
	}
	protect_roms(true);
	zfile_fclose(autoconfig_rom);

	if (f0rom_size)
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, f0rom_size >> 16, 0);
	if (!autoconf) {
		expamem_next();
		return NULL;
	}
	return &blizzarde8_bank;
}
