 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Gayle memory bank
  *
  * (c) 2006 Toni Wilen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "memory.h"

/*
D80000 to D8FFFF		64 KB SPARE chip select
D90000 to D9FFFF		64 KB ARCNET chip select
DA0000 to DA3FFF		16 KB IDE drive
DA4000 to DA4FFF		16 KB IDE reserved
DA8000 to DAFFFF		32 KB Credit Card and IDE configregisters
DB0000 to DBFFFF		64 KB Not used(reserved for external IDE)
* DC0000 to DCFFFF		64 KB Real Time Clock(RTC)
DD0000 to DDFFFF		64 KB RESERVED for DMA controller
DE0000 to DEFFFF		64 KB Not Used
*/

static int gayle_read (uaecptr addr, int size)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 0xfffff;
    if(addr >= 0xd0000 && addr <= 0xdffff)
        return 0xffff;
    if(addr >= 0xe0000 && addr <= 0xeffff)
        return 0x7f7f;
    return 0;
}
static void gayle_write (uaecptr addr, int val, int size)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    addr &= 0x3ffff;
}

static uae_u32 gayle_lget (uaecptr) REGPARAM;
static uae_u32 gayle_wget (uaecptr) REGPARAM;
static uae_u32 gayle_bget (uaecptr) REGPARAM;
static void gayle_lput (uaecptr, uae_u32) REGPARAM;
static void gayle_wput (uaecptr, uae_u32) REGPARAM;
static void gayle_bput (uaecptr, uae_u32) REGPARAM;

addrbank gayle_bank = {
    gayle_lget, gayle_wget, gayle_bget,
    gayle_lput, gayle_wput, gayle_bput,
    default_xlate, default_check, NULL
};

uae_u32 REGPARAM2 gayle_lget (uaecptr addr)
{
    addr &= 0xFFFF;
    return (uae_u32)(gayle_wget (addr) << 16) + gayle_wget (addr + 2);
}
uae_u32 REGPARAM2 gayle_wget (uaecptr addr)
{
    return gayle_read (addr, 2);
}
uae_u32 REGPARAM2 gayle_bget (uaecptr addr)
{
    return gayle_read (addr, 1);
}

void REGPARAM2 gayle_lput (uaecptr addr, uae_u32 value)
{
    gayle_wput (addr, value >> 16);
    gayle_wput (addr + 2, value & 0xffff);
}

void REGPARAM2 gayle_wput (uaecptr addr, uae_u32 value)
{
    gayle_write (addr, value, 2);
}

void REGPARAM2 gayle_bput (uaecptr addr, uae_u32 value)
{
    gayle_write (addr, value, 1);
}
