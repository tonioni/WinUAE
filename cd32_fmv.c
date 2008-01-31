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

static int fmv_mask;
static uae_u8 *rom;
static int rom_size = 262144;
static uaecptr fmv_start = 0x00200000;
static int fmv_size = 1048576;

static uae_u8 fmv_bget2 (uaecptr addr)
{
#ifdef FMV_DEBUG
    write_log ("fmv_bget2 %08X PC=%8X\n", addr, M68K_GETPC);
#endif
    if (addr >= rom_size) {
	write_log ("fmv_bget2 %08X PC=%8X\n", addr, M68K_GETPC);
	return 0;
    }
    return rom[addr];
}
static void fmv_bput2 (uaecptr addr, uae_u8 v)
{
    if (addr >= rom_size && addr < 0xf0000) {
	;//write_log ("fmv_bput2 %08X=%02X PC=%8X\n", addr, v & 0xff, M68K_GETPC);
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
    write_log ("fmv_lget %08X=%08X PC=%08X\n", addr, v, M68K_GETPC);
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
    write_log ("fmv_wget %08X=%04X PC=%08X\n", addr, v, M68K_GETPC);
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
    write_log ("fmv_lput %08X=%08X PC=%08X\n", addr, l, M68K_GETPC);
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
    write_log ("fmv_wput %04X=%04X PC=%08X\n", addr, w & 65535, M68K_GETPC);
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
    write_log ("fmv_wgeti %08X %08X PC=%08X\n", addr, v, M68K_GETPC);
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
    write_log ("fmv_lgeti %08X %08X PC=%08X\n", addr, v, M68K_GETPC);
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
    fmv_xlate, fmv_check, NULL, "CD32 FMV module",
    fmv_lgeti, fmv_wgeti, ABFLAG_ROM | ABFLAG_IO
};



void cd32_fmv_init (uaecptr start)
{
    int ids[] = { 72, -1 };
    struct romlist *rl = getromlistbyids (ids);
    struct zfile *z;

    write_log ("CD32 FMV mapped @$%lx\n", start);
    if (start != fmv_start)
	return;
    if (!rl)
	return;
    write_log ("CD32 FMV ROM '%s' %d.%d\n", rl->path, rl->rd->ver, rl->rd->rev);
    z = zfile_fopen(rl->path, "rb");
    if (z) {
	rom = mapped_malloc (fmv_size, "fast");
	if (rom) {
	    zfile_fread(rom, rom_size, 1, z);
	    zfile_fclose (z);
	}
    }
    fmv_mask = fmv_size - 1;
    fmv_bank.baseaddr = rom;
    rom[0x282] = 0;
    map_banks (&fmv_bank, start >> 16, fmv_size >> 16, 0);
}
