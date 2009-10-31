/*
* UAE - The Un*x Amiga Emulator
*
* CD32 FMV cartridge
*
* Copyright 2008 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "zfile.h"
#include "cd32_fmv.h"
#include "uae.h"

//#define FMV_DEBUG

#define FMV_BASE 0x40000
#define AUDIO_BASE 0x50000
#define VIDEO_BASE 0x70000
#define VIDEO_RAM 0x80000

// L64111 registers (from datasheet)
#define A_DATA 0
#define A_CONTROL1 2
#define A_CONTROL2 4
#define A_CONTROL3 6
#define A_INT1 8
#define A_INT2 10
#define A_TCR 12
#define A_TORH 14
#define A_TORL 16
#define A_PARAM1 18
#define A_PARAM2 20
#define A_PARAM3 22
#define A_PRESENT1 24
#define A_PRESENT2 26
#define A_PRESENT3 28
#define A_PRESENT4 30
#define A_PRESENT5 32
#define A_FIFO 34
#define A_CB_STATUS 36
#define A_CB_WRITE 38
#define A_CB_READ 40

static int fmv_mask;
static uae_u8 *rom;
static int rom_size = 262144;
static uaecptr fmv_start = 0x00200000;
static int fmv_size = 1048576;

static uae_u8 fmv_bget2 (uaecptr addr)
{
#ifdef FMV_DEBUG
	write_log (L"fmv_bget2 %08X PC=%8X\n", addr, M68K_GETPC);
#endif
	if (addr >= rom_size && addr < 0x80000) {
		write_log (L"fmv_bget2 %08X PC=%8X\n", addr, M68K_GETPC);
		return 0;
	}
	return rom[addr];
}
static void fmv_bput2 (uaecptr addr, uae_u8 v)
{
	if (addr >= rom_size && addr < 0x80000) {
		write_log (L"fmv_bput2 %08X=%02X PC=%8X\n", addr, v & 0xff, M68K_GETPC);
	}
}

static uae_u32 REGPARAM2 fmv_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	v = (fmv_bget2 (addr) << 24) | (fmv_bget2 (addr + 1) << 16) |
		(fmv_bget2 (addr + 2) << 8) | (fmv_bget2 (addr + 3));
#ifdef FMV_DEBUG
	write_log (L"fmv_lget %08X=%08X PC=%08X\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 fmv_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	v = (fmv_bget2 (addr) << 8) | fmv_bget2 (addr + 1);
#ifdef FMV_DEBUG
	write_log (L"fmv_wget %08X=%04X PC=%08X\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 fmv_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	v = fmv_bget2 (addr);
	return v;
}

static void REGPARAM2 fmv_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
#ifdef FMV_DEBUG
	write_log (L"fmv_lput %08X=%08X PC=%08X\n", addr, l, M68K_GETPC);
#endif
	fmv_bput2 (addr, l >> 24);
	fmv_bput2 (addr + 1, l >> 16);
	fmv_bput2 (addr + 2, l >> 8);
	fmv_bput2 (addr + 3, l);
}

static void REGPARAM2 fmv_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
#ifdef FMV_DEBUG
	write_log (L"fmv_wput %04X=%04X PC=%08X\n", addr, w & 65535, M68K_GETPC);
#endif
	fmv_bput2 (addr, w >> 8);
	fmv_bput2 (addr + 1, w);
}

static addrbank fmv_bank;

static void REGPARAM2 fmv_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	fmv_bput2 (addr, b);
}

static uae_u32 REGPARAM2 fmv_wgeti (uaecptr addr)
{
	uae_u32 v = 0;
	uae_u8 *m;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	m = rom + addr;
	if (addr < rom_size)
		return do_get_mem_word ((uae_u16 *)m);
#ifdef FMV_DEBUG
	write_log (L"fmv_wgeti %08X %08X PC=%08X\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 fmv_lgeti (uaecptr addr)
{
	uae_u32 v = 0;
	uae_u8 *m;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	m = rom + addr;
	if (addr < rom_size)
		return do_get_mem_long ((uae_u32 *)m);
#ifdef FMV_DEBUG
	write_log (L"fmv_lgeti %08X %08X PC=%08X\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static int REGPARAM2 fmv_check (uaecptr addr, uae_u32 size)
{
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	return (addr + size) <= fmv_size;
}

static uae_u8 *REGPARAM2 fmv_xlate (uaecptr addr)
{
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	return rom + addr;
}

static addrbank fmv_bank = {
	fmv_lget, fmv_wget, fmv_bget,
	fmv_lput, fmv_wput, fmv_bput,
	fmv_xlate, fmv_check, NULL, L"CD32 FMV module",
	fmv_lgeti, fmv_wgeti, ABFLAG_ROM | ABFLAG_IO
};



void cd32_fmv_init (uaecptr start)
{
	int ids[] = { 23, -1 };
	struct romlist *rl = getromlistbyids (ids);
	struct romdata *rd;
	struct zfile *z;

	write_log (L"CD32 FMV mapped @$%lx\n", start);
	if (start != fmv_start)
		return;
	if (!rl)
		return;
	rd = rl->rd;
	z = read_rom (&rd);
	if (z) {
		write_log (L"CD32 FMV ROM %d.%d\n", rd->ver, rd->rev);
		rom = mapped_malloc (fmv_size, L"fast");
		if (rom)
			zfile_fread (rom, rd->size, 1, z);
		zfile_fclose (z);
	}
	fmv_mask = fmv_size - 1;
	fmv_bank.baseaddr = rom;
	rom[0x282] = 0;
	map_banks (&fmv_bank, start >> 16, fmv_size >> 16, 0);
}
