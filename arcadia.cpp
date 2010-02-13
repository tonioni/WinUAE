/*
* UAE - The Un*x Amiga Emulator
*
* Arcadia emulation
*
* Copyright 2005-2007 Toni Wilen
*
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "arcadia.h"
#include "zfile.h"

/* supported roms (mame	0.90)
*
* - ar_airh
* - ar_bowl
* - ar_fast
* - ar_ldrb
* - ar_ldrba
* - ar_ninj
* - ar_rdwr
* - ar_socc (99u8)
* - ar_sdwr
* - ar_spot
* - ar_sprg
* - ar_xeon
*
*/

static void multigame (int);

int arcadia_flag, arcadia_coin[2];
struct arcadiarom *arcadia_bios, *arcadia_game;

#define BIOS_VERSION_211 2
#define BIOS_VERSION_300 3

static struct arcadiarom roms[]	= {

	{ 49, L"ar_bios.zip", L"scpa21",	    ARCADIA_BIOS, 0, 6, 1, 0, 2, 3, 4, 5, 7 },
	{ 50, L"ar_bios.zip", L"gcp-", 	    ARCADIA_BIOS, 3, 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 51, L"ar_bios.zip", L"scpav3_0.",	    ARCADIA_BIOS, 0, 6, 1, 0, 2, 3, 4, 5, 7 },

	{ 33, L"ar_airh.zip", L"airh_",	    ARCADIA_GAME, 1, 5, 0, 2, 4, 7, 6, 1, 3 },
	{ 34, L"ar_bowl.zip", L"bowl_",	    ARCADIA_GAME, 1, 7, 6, 0, 1, 2, 3, 4, 5 },
	{ 35, L"ar_dart.zip", L"dart_",	    ARCADIA_GAME, 1, 4, 0, 7, 6, 3, 1, 2, 5 },
	{ 36, L"ar_fast.zip", L"fastv28.",	    ARCADIA_GAME, 0, 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 37, L"ar_ldrb.zip", L"lbg240",	    ARCADIA_GAME, 0, 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 38, L"ar_ldrba.zip",L"ldrb_",	    ARCADIA_GAME, 1, 2, 3, 4, 1, 0, 7, 5, 6 },
	{ 39, L"ar_ninj.zip", L"ninj_",	    ARCADIA_GAME, 1, 1, 6, 5, 7, 4, 2, 0, 3 },
	{ 40, L"ar_rdwr.zip", L"rdwr_",	    ARCADIA_GAME, 1, 3, 1, 6, 4, 0, 5, 2, 7 },
	{ 41, L"ar_sdwr.zip", L"sdwr_",	    ARCADIA_GAME, 1, 6, 3, 4, 5, 2, 1, 0, 7 },
	{ 42, L"ar_spot.zip", L"spotv2.",	    ARCADIA_GAME, 0, 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 43, L"ar_sprg.zip", L"sprg_",	    ARCADIA_GAME, 1, 4, 7, 3, 0, 6, 5, 2, 1 },
	{ 44, L"ar_xeon.zip", L"xeon_",	    ARCADIA_GAME, 1, 3, 1, 2, 4, 0, 5, 6, 7 },
	{ 45, L"ar_socc.zip", L"socc30.",	    ARCADIA_GAME, 2, 0, 7, 1, 6, 5, 4, 3, 2 },

	{ -1 }
};

static uae_u8 *arbmemory, *arbbmemory;
static int boot_read;

#define	arb_start 0x800000
#define	arb_mask 0x1fffff
#define	allocated_arbmemory 0x200000

#define arbb_start 0xf00000
#define	arbb_mask 0x7ffff
#define	allocated_arbbmemory 0x80000

#define	nvram_offset 0x1fc000
#define	bios_offset 0x180000
#define	NVRAM_SIZE 0x4000

static int nvwrite;

static int load_rom8 (TCHAR *xpath, uae_u8 *mem,	int extra)
{
	struct zfile *zf;
	TCHAR path[MAX_DPATH];
	int i;
	uae_u8 *tmp = xmalloc (uae_u8, 131072);
	TCHAR *bin = extra == 1 ? L".bin" : L"";

	memset (tmp, 0, 131072);
	_stprintf (path, L"%s%s%s", xpath, extra == 3 ? L"-hi" : (extra == 2 ? L"hi" : L"h"), bin);
	zf = zfile_fopen (path, L"rb", ZFD_NORMAL);
	if (!zf)
		goto end;
	if (zfile_fread (tmp, 65536, 1, zf) == 0)
		goto end;
	zfile_fclose (zf);
	_stprintf (path, L"%s%s%s", xpath, extra == 3 ? L"-lo" : (extra == 2 ? L"lo" : L"l"), bin);
	zf = zfile_fopen (path, L"rb", ZFD_NORMAL);
	if (!zf)
		goto end;
	if (zfile_fread (tmp + 65536, 65536, 1, zf) == 0)
		goto end;
	zfile_fclose (zf);
	for (i = 0; i < 65536; i++) {
		mem[i * 2 + 0] = tmp[i];
		mem[i * 2 + 1] = tmp[i + 65536];
	}
	xfree (tmp);
	return 1;
end:
	xfree (tmp);
	return 0;
}

static struct arcadiarom *is_arcadia (const TCHAR *xpath, int cnt)
{
	TCHAR path[MAX_DPATH], *p;
	struct arcadiarom *rom = NULL;
	int i;

	_tcscpy (path, xpath);
	p = path;
	for (i = _tcslen (xpath) - 1; i > 0; i--) {
		if (path[i] == '\\' || path[i] == '/') {
			path[i++] = 0;
			p = path + i;
			break;
		}
	}
	for (i = 0; roms[i].romid > 0; i++) {
		if (!_tcsicmp (p, roms[i].name) || !_tcsicmp (p, roms[i].rom)) {
			if (cnt > 0) {
				cnt--;
				continue;
			}
			rom = &roms[i];
			break;
		}
	}
	if (!rom)
		return 0;
	return rom;
}

static int load_roms (struct arcadiarom *rom)
{
	TCHAR path[MAX_DPATH], path2[MAX_DPATH], path3[MAX_DPATH], *p;
	int i, offset;
	TCHAR *xpath;

	if (rom->type == ARCADIA_BIOS)
		xpath = currprefs.romextfile;
	else
		xpath = currprefs.cartfile;

	_tcscpy (path3, xpath);
	p = path3 + _tcslen (path3) - 1;
	while (p > path3) {
		if (p[0] == '\\' || p[0] == '/') {
			*p = 0;
			break;
		}
		p--;
	}
	if (p == path3)
		*p = 0;
	_tcscpy (path2, xpath);

	offset = 0;
	if (rom->type == ARCADIA_BIOS)
		offset = bios_offset;
	i = 0;
	for (;;) {
		_stprintf (path, L"%s%d", xpath, i + 1);
		if (!load_rom8 (path, arbmemory + 2 * 65536 * i + offset, rom->extra)) {
			if (i == 0)
				write_log (L"Arcadia: %s rom load failed ('%s')\n", rom->type == ARCADIA_BIOS ? L"bios" : L"game", path);
			break;
		}
		i++;
	}
	if (i == 0)
		return 0;
	write_log (L"Arcadia: %s rom %s loaded\n", rom->type == ARCADIA_BIOS ? L"bios" : L"game", xpath);
	return 1;
}

static uae_u8 bswap (uae_u8 v,int b7,int b6,int	b5,int b4,int b3,int b2,int b1,int b0)
{
	uae_u8 b = 0;

	b |= ((v >> b7) & 1) << 7;
	b |= ((v >> b6) & 1) << 6;
	b |= ((v >> b5) & 1) << 5;
	b |= ((v >> b4) & 1) << 4;
	b |= ((v >> b3) & 1) << 3;
	b |= ((v >> b2) & 1) << 2;
	b |= ((v >> b1) & 1) << 1;
	b |= ((v >> b0) & 1) << 0;
	return b;
}

static void decrypt_roms (struct arcadiarom *rom)
{
	int i, start = 1, end = 0x20000;

	if (rom->type == ARCADIA_BIOS) {
		start += bios_offset;
		end += bios_offset;
	}
	for (i = start; i < end; i += 2) {
		arbmemory[i] = bswap (arbmemory[i],
			rom->b7,rom->b6,rom->b5,rom->b4,rom->b3,rom->b2,rom->b1,rom->b0);
		if (rom->extra == 2)
			arbmemory[i - 1] = bswap (arbmemory[i - 1],7,6,5,4,3,2,1,0);
	}
}

static uae_u32 REGPARAM2 arbb_lget (uaecptr addr)
{
	uae_u32 *m;

	addr -= arbb_start & arbb_mask;
	addr &= arbb_mask;
	m = (uae_u32 *)(arbbmemory + addr);
	return do_get_mem_long (m);
}
static uae_u32 REGPARAM2 arbb_wget (uaecptr addr)
{
	uae_u16 *m;

	addr -= arbb_start & arbb_mask;
	addr &= arbb_mask;
	m = (uae_u16 *)(arbbmemory + addr);
	return do_get_mem_word (m);
}
static uae_u32 REGPARAM2 arbb_bget (uaecptr addr)
{
	addr -= arbb_start & arbb_mask;
	addr &= arbb_mask;
	return arbbmemory[addr];
}

static void REGPARAM2 arbb_lput (uaecptr addr, uae_u32 l)
{
}

static void REGPARAM2 arbb_wput (uaecptr addr, uae_u32 w)
{
}

static void REGPARAM2 arbb_bput (uaecptr addr, uae_u32 b)
{
}

static addrbank arcadia_boot_bank = {
	arbb_lget, arbb_wget, arbb_bget,
	arbb_lput, arbb_wput, arbb_bput,
	default_xlate, default_check, NULL, L"Arcadia BIOS",
	arbb_lget, arbb_wget, ABFLAG_ROM
};

static uae_u32 REGPARAM2 arb_lget (uaecptr addr)
{
	uae_u32 *m;

	addr -= arb_start & arb_mask;
	addr &= arb_mask;
	m = (uae_u32 *)(arbmemory + addr);
	return do_get_mem_long (m);
}
static uae_u32 REGPARAM2 arb_wget (uaecptr addr)
{
	uae_u16 *m;

	addr -= arb_start & arb_mask;
	addr &= arb_mask;
	m = (uae_u16 *)(arbmemory + addr);
	return do_get_mem_word (m);
}
static uae_u32 REGPARAM2 arb_bget (uaecptr addr)
{
	addr -= arb_start & arb_mask;
	addr &= arb_mask;
	return arbmemory[addr];
}
static void REGPARAM2 arb_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

	addr -= arb_start & arb_mask;
	addr &= arb_mask;
	if (addr >= nvram_offset) {
		m = (uae_u32 *)(arbmemory + addr);
		do_put_mem_long (m, l);
		nvwrite++;
	}
}
static void REGPARAM2 arb_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

	addr -= arb_start & arb_mask;
	addr &= arb_mask;
	if (addr >= nvram_offset) {
		m = (uae_u16 *)(arbmemory + addr);
		do_put_mem_word (m, w);
		nvwrite++;
		if (addr == 0x1ffffe)
			multigame(w);
	}
}
static void REGPARAM2 arb_bput (uaecptr addr, uae_u32 b)
{
	addr -= arb_start & arb_mask;
	addr &= arb_mask;
	if (addr >= nvram_offset) {
		arbmemory[addr] = b;
		nvwrite++;
	}
}

static addrbank arcadia_rom_bank = {
	arb_lget, arb_wget, arb_bget,
	arb_lput, arb_wput, arb_bput,
	default_xlate, default_check, NULL, L"Arcadia Game ROM",
	arb_lget, arb_wget, ABFLAG_ROM
};


static void multigame(int v)
{
	if (v != 0)
		map_banks (&kickmem_bank, arb_start >> 16,
		8, 0);
	else
		map_banks (&arcadia_rom_bank, arb_start >> 16,
		allocated_arbmemory >> 16, 0);
}

int is_arcadia_rom (const TCHAR *path)
{
	struct arcadiarom *rom;

	rom = is_arcadia (path, 0);
	if (!rom || rom->type == NO_ARCADIA_ROM)
		return NO_ARCADIA_ROM;
	if (rom->type == ARCADIA_BIOS) {
		arcadia_bios = rom;
		return ARCADIA_BIOS;
	}
	arcadia_game = rom;
	return ARCADIA_GAME;
}

static void nvram_write (void)
{
	struct zfile *f = zfile_fopen (currprefs.flashfile, L"rb+", ZFD_NORMAL);
	if (!f) {
		f = zfile_fopen (currprefs.flashfile, L"wb", 0);
		if (!f)
			return;
	}
	zfile_fwrite (arbmemory + nvram_offset, NVRAM_SIZE, 1, f);
	zfile_fclose (f);
}

static void nvram_read (void)
{
	struct zfile *f;

	f = zfile_fopen (currprefs.flashfile, L"rb", ZFD_NORMAL);
	memset (arbmemory + nvram_offset, 0, NVRAM_SIZE);
	if (!f)
		return;
	zfile_fread (arbmemory + nvram_offset, NVRAM_SIZE, 1, f);
	zfile_fclose (f);
}

void arcadia_unmap (void)
{
	xfree (arbmemory);
	arbmemory = NULL;
	arcadia_bios = NULL;
	arcadia_game = NULL;
}

int arcadia_map_banks (void)
{
	if (!arcadia_bios)
		return 0;
	arbmemory = xmalloc (uae_u8, allocated_arbmemory);
	arbbmemory = arbmemory + bios_offset;
	memset (arbmemory, 0, allocated_arbmemory);
	if (!load_roms (arcadia_bios)) {
		arcadia_unmap ();
		return 0;
	}
	if (arcadia_game)
		load_roms (arcadia_game);
	decrypt_roms (arcadia_bios);
	if (arcadia_game)
		decrypt_roms (arcadia_game);
	nvram_read ();
	multigame(0);
	map_banks (&arcadia_boot_bank, 0xf0,
		8, 0);
	return 1;
}

void arcadia_vsync (void)
{
	static int cnt;

	cnt--;
	if (cnt > 0)
		return;
	cnt = 50;
	if (!nvwrite)
		return;
	nvram_write ();
	nvwrite = 0;
}

uae_u8 arcadia_parport (int port, uae_u8 pra, uae_u8 dra)
{
	uae_u8 v;

	v = (pra & dra) | (dra ^ 0xff);
	if (port) {
		if (dra & 1)
			arcadia_coin[0] = arcadia_coin[1] = 0;
		return 0;
	}
	v = 0;
	v |= (arcadia_flag & 1) ? 0 : 2;
	v |= (arcadia_flag & 2) ? 0 : 4;
	v |= (arcadia_flag & 4) ? 0 : 8;
	v |= (arcadia_coin[0] & 3) << 4;
	v |= (arcadia_coin[1] & 3) << 6;
	return v;
}

struct romdata *scan_arcadia_rom (TCHAR *path, int cnt)
{
	struct romdata *rd = 0;
	struct romlist **arc_rl;
	struct arcadiarom *arcadia_rom;
	int i;

	arcadia_rom = is_arcadia (path, cnt);
	if (arcadia_rom) {
		arc_rl = getarcadiaroms();
		for (i = 0; arc_rl[i]; i++) {
			if (arc_rl[i]->rd->id == arcadia_rom->romid) {
				rd = arc_rl[i]->rd;
				_tcscat (path, L"/");
				_tcscat (path, arcadia_rom->rom);
				break;
			}
		}
		xfree (arc_rl);
	}
	return rd;
}
