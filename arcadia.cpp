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

/* supported roms
*
* (mame 0.90)
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
* mame 0.152
*
* - ar_blast
* - ar_dlta
* - ar_pm
*
* mame 0.153
*
* - ar_argh

*/

static void multigame (int);

int arcadia_flag, arcadia_coin[2];
struct arcadiarom *arcadia_bios, *arcadia_game;

static struct arcadiarom roms[]	= {

	// oneplay 2.11
	{ 49, _T("ar_bios.zip"), _T("scpa_01_v2.11"), _T("scpa_01"),ARCADIA_BIOS, 3, 6, 1, 0, 2, 3, 4, 5, 7, _T("_v2.11."), { _T("u12"), _T("u16") } },
	// tenplay 2.11
	{ 50, _T("ar_bios.zip"), _T("gcp"), _T("gcp-"),				ARCADIA_BIOS, 7, 7, 6, 5, 4, 3, 2, 1, 0 },
	// oneplay 2.20
	{ 75, _T("ar_bios.zip"), _T("scpa_01_v2.20"), _T("scpa_01"),ARCADIA_BIOS, 3, 6, 1, 0, 2, 3, 4, 5, 7, _T("_v2.20."), { _T("u12"), _T("u16") } },
	// oneplay 3.00
	{ 51, _T("ar_bios.zip"), _T("scpa_01_v3.00"), _T("scpa_01"),ARCADIA_BIOS, 3, 6, 1, 0, 2, 3, 4, 5, 7, _T("_v3.0."), { _T("u12"), _T("u16") } },
	// tenplay 3.11
	{ 76, _T("ar_bios.zip"), _T("gcp_v11"), _T("gcp_v311_"),	ARCADIA_BIOS, 7, 7, 6, 5, 4, 3, 2, 1, 0, NULL, { _T(".u16"), _T(".u11"), _T(".u17"), _T(".u12") } },
	// tenplay 4.00
	{ 77, _T("ar_bios.zip"), _T("gcp_v400"), _T("gcp_v400_"),	ARCADIA_BIOS, 7, 7, 6, 5, 4, 3, 2, 1, 0, NULL, { _T(".u16"), _T(".u11"), _T(".u17"), _T(".u12") } },

	{ 33, _T("ar_airh.zip"), _T("airh"), _T("airh_"),			ARCADIA_GAME, 5|16, 5, 0, 2, 4, 7, 6, 1, 3 },
	{ 81, _T("ar_airh2.zip"), _T("airh2"), _T("arcadia4"),		ARCADIA_GAME, 0, 5, 0, 2, 4, 7, 6, 1, 3, NULL, { _T(".u10"), _T(".u6") } },
	{ 34, _T("ar_bowl.zip"), _T("bowl"), _T("bowl_"),			ARCADIA_GAME, 5|16, 7, 6, 0, 1, 2, 3, 4, 5 },
	{ 35, _T("ar_dart.zip"), _T("dart"), _T("dart_"),			ARCADIA_GAME, 5|16, 4, 0, 7, 6, 3, 1, 2, 5 },
	{ 82, _T("ar_dart2.zip"), _T("dart2"), _T("arcadia3"),		ARCADIA_GAME, 0, 4, 0, 7, 6, 3, 1, 2, 5, NULL, { _T(".u10"), _T(".u6"), _T(".u11"), _T(".u7"), _T(".u12"), _T(".u8"), _T(".u13"), _T(".u9"), _T(".u19"), _T(".u15"), _T(".u20"), _T(".u16") } },
	{ 36, _T("ar_fast.zip"), _T("fast-v28"), _T("fast-v28_"),	ARCADIA_GAME, 7, 7, 6, 5, 4, 3, 2, 1, 0, NULL, { _T(".u11"), _T(".u15"), _T(".u10"), _T(".u14"), _T(".u9"), _T(".u13"), _T(".u20"), _T(".u24"), _T(".u19"), _T(".u23"), _T(".u18"), _T(".u22"), _T(".u17"), _T(".u21"), _T(".u28"), _T(".u32")  } },
	{ 83, _T("ar_fasta.zip"), _T("fast-v27"), _T("fast-v27_"),	ARCADIA_GAME, 7, 7, 6, 5, 4, 3, 2, 1, 0, NULL, { _T(".u11"), _T(".u15"), _T(".u10"), _T(".u14"), _T(".u9"), _T(".u13"), _T(".u20"), _T(".u24"), _T(".u19"), _T(".u23"), _T(".u18"), _T(".u22"), _T(".u17"), _T(".u21"), _T(".u28"), _T(".u32")  } },
	{ 37, _T("ar_ldrba.zip"), _T("ldra"), _T("leader_board_0"),	ARCADIA_GAME, 7, 7, 6, 5, 4, 3, 2, 1, 0, _T("_v2.4."), { _T("u11"), _T("u15"), _T("u10"), _T("u14"), _T("u9"), _T("u13"), _T("u20"), _T("u24"), _T("u19"), _T("u23"), _T("u18"), _T("u22"), _T("u17"), _T("u21"), _T("u28"), _T("u32")  } },
	{ 38, _T("ar_ldrbb.zip"),_T("ldrbb"), _T("ldrb_"),			ARCADIA_GAME, 5, 7, 6, 5, 4, 3, 2, 1, 0, NULL, { _T(".u11"), _T("_gcp_22.u15"), _T(".u10"), _T(".u14"), _T(".u9"), _T(".u13"), _T(".u20"), _T(".u24"), _T(".u19"), _T(".u23"), _T(".u18"), _T(".u22"), _T(".u17"), _T(".u21"), _T(".u28"), _T(".u32")  } },
	{ 86, _T("ar_ldrb.zip"),_T("ldrb"), _T("leader_board_0"),	ARCADIA_GAME, 7, 2, 3, 4, 1, 0, 7, 5, 6, _T("_v2.5."), { _T("u11"), _T("u15"), _T("u10"), _T("u14"), _T("u9"), _T("u13"), _T("u20"), _T("u24"), _T("u19"), _T("u23"), _T("u18"), _T("u22"), _T("u17"), _T("u21"), _T("u28"), _T("u32")  } },
	{ 39, _T("ar_ninj.zip"), _T("ninj"), _T("ninj_"),			ARCADIA_GAME, 5|16, 1, 6, 5, 7, 4, 2, 0, 3 },
	{ 84, _T("ar_ninj2.zip"), _T("ninj2"), _T("arcadia5"),		ARCADIA_GAME, 0, 1, 6, 5, 7, 4, 2, 0, 3, NULL, { _T(".u10"), _T(".u6"), _T(".u11"), _T(".u7"), _T(".u12"), _T(".u8"), _T(".u13"), _T(".u9"), _T(".u19"), _T(".u15"), _T(".u20"), _T(".u16") } },
	{ 40, _T("ar_rdwr.zip"), _T("rdwr"), _T("rdwr_"),			ARCADIA_GAME, 5|16, 3, 1, 6, 4, 0, 5, 2, 7 },
	{ 41, _T("ar_sdwr.zip"), _T("sdwr"), _T("sdwr_"),			ARCADIA_GAME, 5|16, 6, 3, 4, 5, 2, 1, 0, 7 },
	{ 85, _T("ar_sdwr2.zip"), _T("sdwr2"), _T("arcadia1"),		ARCADIA_GAME, 0, 6, 3, 4, 5, 2, 1, 0, 7, NULL, { _T(".u10"), _T(".u6"), _T(".u11"), _T(".u7"), _T(".u12"), _T(".u8"), _T(".u13"), _T(".u9"), _T(".u19"), _T(".u15"), _T(".u20"), _T(".u16") } },
	{ 42, _T("ar_spot.zip"), _T("spotv2"), _T("spotv2."),		ARCADIA_GAME, 5, 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 43, _T("ar_sprg.zip"), _T("sprg"), _T("sprg_"),			ARCADIA_GAME, 5|16, 4, 7, 3, 0, 6, 5, 2, 1 },
	{ 44, _T("ar_xeon.zip"), _T("xeon"), _T("xeon_"),			ARCADIA_GAME, 5|16, 3, 1, 2, 4, 0, 5, 6, 7 },
	{ 45, _T("ar_socc.zip"), _T("socc30"), _T("socc30."),		ARCADIA_GAME, 6, 0, 7, 1, 6, 5, 4, 3, 2 },
	{ 78, _T("ar_blast.zip"), _T("blsb-v2-1"),_T("blsb-v2-1_"),	ARCADIA_GAME, 7|16, 4, 1, 7, 6, 2, 0, 3, 5 },
	{ 79, _T("ar_dlta.zip"),  _T("dlta_v3"), _T("dlta_v3_"),	ARCADIA_GAME, 7|16, 4, 1, 7, 6, 2, 0, 3, 5 },
	{ 80, _T("ar_pm.zip"), _T("pm"), _T("pm-"),					ARCADIA_GAME, 2|4|16, 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 88, _T("ar_argh.zip"), _T("argh"), _T("argh-"),			ARCADIA_GAME, 7, 5, 0, 2, 4, 7, 6, 1, 3, _T("-11-28-87"), { _T(".u12"), _T(".u16"), _T(".u11"), _T(".u15"), _T(".u10"), _T(".u14"), _T(".u9"), _T(".u13"), _T(".u20"), _T(".u24"), _T(".u19"), _T(".u23"), _T(".u18"), _T(".u22"), _T(".u17"), _T(".u21"), _T(".u28"), _T(".u32"), _T(".u27"), _T(".u31"), _T(".u26"), _T(".u30"), _T(".u25"), _T(".u29") } },

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

static int load_rom8 (const TCHAR *xpath, uae_u8 *mem, int extra, const TCHAR *ext, const TCHAR **exts)
{
	struct zfile *zf;
	TCHAR path[MAX_DPATH];
	int i;
	uae_u8 *tmp = xmalloc (uae_u8, 131072);
	const TCHAR *bin = (extra & 16) ? _T(".bin") : _T("");

	extra &= 3;
	memset (tmp, 0xff, 131072);
	_stprintf (path, _T("%s%s%s"), xpath, extra == 3 ? _T("-hi") : (extra == 2 ? _T("hi") : (extra == 1 ? _T("h") : _T(""))), bin);
	if (ext)
		_tcscat(path, ext);
	if (exts) {
		if (exts[0] == NULL)
			goto end;
		_tcscat (path, exts[0]);
	}
	//write_log (_T("%s\n"), path);
	zf = zfile_fopen (path, _T("rb"), ZFD_NORMAL);
	if (!zf)
		goto end;
	if (zfile_fread (tmp, 65536, 1, zf) == 0)
		goto end;
	zfile_fclose (zf);
	_stprintf (path, _T("%s%s%s"), xpath, extra == 3 ? _T("-lo") : (extra == 2 ? _T("lo") : (extra == 1 ? _T("l") : _T(""))), bin);
	if (ext)
		_tcscat(path, ext);
	if (exts)
		_tcscat (path, exts[1]);
	//write_log (_T("%s\n"), path);
	zf = zfile_fopen (path, _T("rb"), ZFD_NORMAL);
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
		if (!_tcsicmp (p, roms[i].name) || !_tcsicmp (p, roms[i].romid1)) {
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
	TCHAR path[MAX_DPATH], path3[MAX_DPATH], *p;
	int i, offset;
	TCHAR *xpath;

	offset = 0;
	if (rom->type == ARCADIA_BIOS) {
		xpath = currprefs.romextfile;
		offset = bios_offset;
	} else {
		xpath = currprefs.cartfile;
	}

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

	xpath = path3;
	_tcscat (path3, FSDB_DIR_SEPARATOR_S);
	_tcscat (path3, rom->romid2);

	i = 0;
	for (;;) {
		if (rom->extra & 4)
			_stprintf (path, _T("%s%d"), xpath, i + 1);
		else
			_tcscpy (path, xpath);
		if (!load_rom8 (path, arbmemory + 2 * 65536 * i + offset, rom->extra, rom->ext, rom->exts && rom->exts[0] ? &rom->exts[i * 2] : NULL)) {
			if (i == 0)
				write_log (_T("Arcadia: %s rom load failed ('%s')\n"), rom->type == ARCADIA_BIOS ? _T("bios") : _T("game"), path);
			break;
		}
		i++;
	}
	if (i == 0)
		return 0;
	write_log (_T("Arcadia: %s rom %s loaded\n"), rom->type == ARCADIA_BIOS ? _T("bios") : _T("game"), xpath);
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
	if (rom->romid == 88) {
		memcpy(arbmemory + bios_offset, arbmemory, 524288);
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

static int REGPARAM2 arbb_check (uaecptr addr, uae_u32 size)
{
	addr &= arbb_mask;
	return (addr + size) <= allocated_arbbmemory;
}

static uae_u8 *REGPARAM2 arbb_xlate (uaecptr addr)
{
	addr &= arbb_mask;
	return arbbmemory + addr;
}

static addrbank arcadia_boot_bank = {
	arbb_lget, arbb_wget, arbb_bget,
	arbb_lput, arbb_wput, arbb_bput,
	arbb_xlate, arbb_check, NULL, NULL, _T("Arcadia BIOS"),
	arbb_lget, arbb_wget, ABFLAG_ROM | ABFLAG_SAFE, S_READ, S_WRITE,
	NULL, arbb_mask
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

static int REGPARAM2 arb_check (uaecptr addr, uae_u32 size)
{
	addr &= arb_mask;
	return (addr + size) <= allocated_arbmemory;
}

static uae_u8 *REGPARAM2 arb_xlate (uaecptr addr)
{
	addr &= arb_mask;
	return arbmemory + addr;
}

static addrbank arcadia_rom_bank = {
	arb_lget, arb_wget, arb_bget,
	arb_lput, arb_wput, arb_bput,
	arb_xlate, arb_check, NULL, NULL, _T("Arcadia Game ROM"),
	arb_lget, arb_wget, ABFLAG_ROM | ABFLAG_SAFE, S_READ, S_WRITE,
	NULL, arb_mask
};


static void multigame (int v)
{
	if (v != 0)
		map_banks (&kickmem_bank, arb_start >> 16, 8, 0);
	else
		map_banks (&arcadia_rom_bank, arb_start >> 16, allocated_arbmemory >> 16, 0);
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
	struct zfile *f = zfile_fopen (currprefs.flashfile, _T("rb+"), ZFD_NORMAL);
	if (!f) {
		f = zfile_fopen (currprefs.flashfile, _T("wb"), 0);
		if (!f)
			return;
	}
	zfile_fwrite (arbmemory + nvram_offset, NVRAM_SIZE, 1, f);
	zfile_fclose (f);
}

static void nvram_read (void)
{
	struct zfile *f;

	f = zfile_fopen (currprefs.flashfile, _T("rb"), ZFD_NORMAL);
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
	arbmemory = xmalloc (uae_u8, allocated_arbmemory + allocated_arbbmemory);
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
	multigame (0);
	map_banks (&arcadia_boot_bank, 0xf0, 8, 0);
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
				_tcscat (path, FSDB_DIR_SEPARATOR_S);
				_tcscat (path, arcadia_rom->romid1);
				break;
			}
		}
		xfree (arc_rl);
	}
	return rd;
}
