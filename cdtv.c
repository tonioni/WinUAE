 /*
  * UAE - The Un*x Amiga Emulator
  *
  * CDTV DMAC/CDROM controller emulation
  *
  * Copyright 2004 Toni Wilen
  *
  */

#define CDTV_DEBUG

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "cdtv.h"

static uae_u32 REGPARAM3 dmac_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dmac_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dmac_bget (uaecptr) REGPARAM;
static void REGPARAM3 dmac_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dmac_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dmac_bput (uaecptr, uae_u32) REGPARAM;

static uae_u8 dmacmemory[0x100];
static uae_u8 wd[0x1f];

static int cdtv_command_len;
static uae_u8 cdtv_command_buf[6];

static void cdtv_do_interrupt (void)
{
    if (dmacmemory[0x41] & 0x10) {
	write_log ("cdtv doint\n");
	INTREQ (0x8008);
    }
}

static void cdtv_interrupt (void)
{
    write_log ("cdtv int\n");
    wd[0x1f] |= 0x80;
    dmacmemory[0x41] |= (1 << 4) | (1 << 6); /* ISTR */
    cdtv_do_interrupt ();
}

static void write_register (uae_u8 b)
{
    switch (dmacmemory[0x91])
    {
	case 0x18:
	write_log ("command=%02.2X\n", b);
	cdtv_interrupt ();
	break;
    }
}

static uae_u32 dmac_bget2 (uaecptr addr)
{
    addr -= DMAC_START;
    addr &= 65535;
    switch (addr)
    {
	case 0x41:
	return 0x10|0x40;
	case 0x91: /* AUXILIARY STATUS */
	return 0x80 | 0x01;

	case 0x93:
	case 0xa3:
	switch (dmacmemory[0x91])
	{
	    case 0x17: /* SCSI STATUS */
	    return 0x22;

	    default:
	    return dmacmemory[0x93];
	}
	break;
    }
    return dmacmemory[addr];
}

uae_u32 REGPARAM2 dmac_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = (dmac_bget2 (addr) << 24) | (dmac_bget2 (addr + 1) << 16) |
	(dmac_bget2 (addr + 2) << 8) | (dmac_bget2 (addr + 3));
#ifdef CDTV_DEBUG
    write_log ("dmac_lget %08.8X=%08.8X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

uae_u32 REGPARAM2 dmac_wget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = (dmac_bget2 (addr) << 8) | dmac_bget2 (addr + 1);
#ifdef CDTV_DEBUG
    write_log ("dmac_wget %08.8X=%04.4X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

uae_u32 REGPARAM2 dmac_bget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = dmac_bget2 (addr);
#ifdef CDTV_DEBUG
    write_log ("dmac_bget %08.8X=%02.2X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

static void dmac_bput2 (uaecptr addr, uae_u32 b)
{
    int i;

    addr -= DMAC_START;
    addr &= 65535;
#ifdef CDTV_DEBUG
    dmacmemory[addr] = b;
    switch (addr)
    {
	case 0x43:
	cdtv_do_interrupt ();
	break;
	case 0x93:
	case 0xa3:
	write_register (b);
	break;
	case 0xa1:
	if ((dmacmemory[0xb3] & 3) == 0) { /* PRB /CMD and /ENABLE */
	    if (cdtv_command_len >= sizeof (cdtv_command_buf))
		cdtv_command_len = sizeof (cdtv_command_buf) - 1;
	    cdtv_command_buf[cdtv_command_len++] = b;
	} else {
	    cdtv_command_len = 0;
	}
	if (cdtv_command_len == 6)
	    cdtv_interrupt ();
	break;
	case 0xa5:
	cdtv_command_len = 0;
	break;
	case 0xb3:
	if (!(dmacmemory[0xb3] & 1) && (b & 1)) { /* command sent? */
	    write_log ("CMD: ");
	    for (i = 0; i < cdtv_command_len; i++) {
		write_log ("%02.2X ", cdtv_command_buf[i]);
	    }
	    write_log("\n");
	}
	break;
	case 0xe0:
	write_log ("DMA PTR=%x LEN=%d\n", dmac_lget (0x84), dmac_lget (0x80));
	break;
	case 0xe4: /* CINT */
	dmacmemory[0x41] = 0;
	break;
	case 0xe8: /* FLUSH */
	cdtv_command_len = 0;
	break;
    }
#endif
}

static void REGPARAM2 dmac_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
#ifdef CDTV_DEBUG
    write_log ("dmac_lput %08.8X=%08.8X PC=%08.8X\n", addr, l, M68K_GETPC);
#endif
    dmac_bput2 (addr, l >> 24);
    dmac_bput2 (addr + 1, l >> 16);
    dmac_bput2 (addr + 2, l >> 8);
    dmac_bput2 (addr + 3, l);
}

static void REGPARAM2 dmac_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
#ifdef CDTV_DEBUG
    write_log ("dmac_wput %04.4X=%04.4X PC=%08.8X\n", addr, w & 65535, M68K_GETPC);
#endif
    dmac_bput2 (addr, w >> 8);
    dmac_bput2 (addr + 1, w);
}

static void REGPARAM2 dmac_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
#ifdef CDTV_DEBUG
    write_log ("dmac_bput %08.8X=%02.2X PC=%08.8X\n", addr, b & 255, M68K_GETPC);
#endif
    dmac_bput2 (addr, b);
}

void dmac_init (void)
{
    dmacmemory[6] = ~(2 << 4); /* CDTV DMAC product id */
}

addrbank dmac_bank = {
    dmac_lget, dmac_wget, dmac_bget,
    dmac_lput, dmac_wput, dmac_bput,
    default_xlate, default_check, NULL, "DMAC",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

