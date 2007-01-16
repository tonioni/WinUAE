 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Gayle (and motherboard resources) memory bank
  *
  * (c) 2006 Toni Wilen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "gayle.h"

/*
D80000 to D8FFFF		64 KB SPARE chip select
D90000 to D9FFFF		64 KB ARCNET chip select
DA0000 to DA3FFF		16 KB IDE drive
DA4000 to DA4FFF		16 KB IDE reserved
DA8000 to DAFFFF		32 KB Credit Card and IDE configregisters
DB0000 to DBFFFF		64 KB Not used(reserved for external IDE)
* DC0000 to DCFFFF		64 KB Real Time Clock(RTC)
DD0000 to DDFFFF		64 KB RESERVED for DMA controller
DE0000 to DEFFFF		64 KB Motherboard resources
*/

/* Gayle definitions from Linux driver */

/*
 *  Bases of the IDE interfaces
 */
#define GAYLE_BASE_4000 0xdd2020    /* A4000/A4000T */
#define GAYLE_BASE_1200 0xda0000    /* A1200/A600 and E-Matrix 530 */
/*
 *  Offsets from one of the above bases
 */
#define GAYLE_DATA      0x00
#define GAYLE_ERROR     0x06	    /* see err-bits */
#define GAYLE_NSECTOR   0x0a	    /* nr of sectors to read/write */
#define GAYLE_SECTOR    0x0e	    /* starting sector */
#define GAYLE_LCYL      0x12	    /* starting cylinder */
#define GAYLE_HCYL      0x16	    /* high byte of starting cyl */
#define GAYLE_SELECT    0x1a	    /* 101dhhhh , d=drive, hhhh=head */
#define GAYLE_STATUS    0x1e	    /* see status-bits */
#define GAYLE_CONTROL   0x101a
/*
 *  These are at different offsets from the base
 */
#define GAYLE_IRQ_4000  0x3020    /* MSB = 1, Harddisk is source of */
#define GAYLE_IRQ_1200  0x9000    /* interrupt */

/* GAYLE_IRQ bit def */
#define GAYLE_IRQ_IDE	0x80
#define GAYLE_IRQ_CCDET	0x40
#define GAYLE_IRQ_BVD1	0x20
#define GAYLE_IRQ_SC	0x20
#define GAYLE_IRQ_BVD2	0x10
#define GAYLE_IRQ_DA	0x10
#define GAYLE_IRQ_WR	0x08
#define GAYLE_IRQ_BSY	0x04
#define GAYLE_IRQ_IRQ	0x04
#define GAYLE_IRQ_IDEACK1 0x02
#define GAYLE_IRQ_IDEACK0 0x01

#define GAYLE_LOG 0

static uae_u8 gayle_irq;

static void ide_interrupt(void)
{
    gayle_irq |= GAYLE_IRQ_IDE;
    INTREQ (0x8000 | 0x0008);
}

static int ide_read (uaecptr addr, int size)
{
    addr &= 0xffff;
    if (GAYLE_LOG)
	write_log ("IDE_READ %08.8X\n", addr);
    if (addr == 0x201c) // AR1200 IDE detection hack
	return 0;
    if (currprefs.cs_ide <= 0)
	return 0xffff;
    if (addr == GAYLE_IRQ_4000) {
	if (currprefs.cs_ide == 2) {
	    uae_u8 v = gayle_irq;
	    gayle_irq = 0;
	    return v;
	}
	return 0;
    } else if (addr == GAYLE_IRQ_1200) {
	if (currprefs.cs_ide == 1)
	    return gayle_irq;
	return 0;
    }
    if (currprefs.cs_ide == 2 && (addr & 0x2020) == 0x2020)
	addr &= ~0x2020;
    switch (addr)
    {
	case GAYLE_DATA:
	case GAYLE_ERROR:
	case GAYLE_NSECTOR:
	case GAYLE_SECTOR:
	case GAYLE_LCYL:
	case GAYLE_HCYL:
	case GAYLE_SELECT:
	case GAYLE_STATUS:
	break;
    }
    return 0xffff;
}

static void ide_write (uaecptr addr, uae_u32 val, int size)
{
    addr &= 0xffff;
    if (GAYLE_LOG)
	write_log ("IDE_WRITE %08.8X=%08.8X (%d)\n", addr, val, size);
    if (currprefs.cs_ide <= 0)
	return;
    if (addr == GAYLE_IRQ_1200 && currprefs.cs_ide == 1) {
	gayle_irq &= val;
	return;
    }
    if (currprefs.cs_ide == 2 && (addr & 0x2020) == 0x2020)
	addr &= ~0x2020;
    switch (addr)
    {
	case GAYLE_DATA:
	case GAYLE_ERROR:
	case GAYLE_NSECTOR:
	case GAYLE_SECTOR:
	case GAYLE_LCYL:
	case GAYLE_HCYL:
	case GAYLE_SELECT:
	case GAYLE_STATUS:
	break;
    }
}

static int gayle_read (uaecptr addr, int size)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    if (GAYLE_LOG)
	write_log ("GAYLE_READ %08.8X PC=%08.8X\n", addr, M68K_GETPC);
    addr &= 0xfffff;
    if(addr >= 0xa0000 && addr <= 0xaffff)
	return ide_read(addr, size);
    else if(addr >= 0xd0000 && addr <= 0xdffff)
	return ide_read(addr, size);
    return 0;
}
static void gayle_write (uaecptr addr, int val, int size)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    if (GAYLE_LOG)
	write_log ("GAYLE_WRITE %08.8X=%08.8X (%d) PC=%08.8X\n", addr, val, size, M68K_GETPC);
    addr &= 0x3ffff;
    if(addr >= 0xa0000 && addr <= 0xaffff)
	ide_write(addr, val, size);
    else if(addr >= 0xd0000 && addr <= 0xdffff)
	ide_write(addr, val, size);
}

static uae_u32 REGPARAM3 gayle_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_bget (uaecptr) REGPARAM;
static void REGPARAM3 gayle_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_bput (uaecptr, uae_u32) REGPARAM;

addrbank gayle_bank = {
    gayle_lget, gayle_wget, gayle_bget,
    gayle_lput, gayle_wput, gayle_bput,
    default_xlate, default_check, NULL, "Gayle",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
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

static uae_u8 ramsey_config;
static int gary_coldboot, gary_toenb, gary_timeout;
static int garyidoffset;

static void mbres_write (uaecptr addr, uae_u32 val, int size)
{
    addr &= 0xffff;

    if (GAYLE_LOG)
	write_log ("MBRES_WRITE %08.8X=%08.8X (%d) PC=%08.8X\n", addr, val, size, M68K_GETPC);
    if (addr == 0x1002)
	garyidoffset = -1;
    if (addr == 0x03)
	ramsey_config = val;
    if (addr == 0x02)
	gary_coldboot = (val & 0x80) ? 1 : 0;
    if (addr == 0x01)
	gary_toenb = (val & 0x80) ? 1 : 0;
    if (addr == 0x00)
	gary_timeout = (val & 0x80) ? 1 : 0;
}

static uae_u32 mbres_read (uaecptr addr, int size)
{
    addr &= 0xffff;

    if (GAYLE_LOG)
	write_log ("MBRES_READ %08.8X\n", addr);

    /* Gary ID (I don't think this exists in real chips..) */
    if (addr == 0x1002 && currprefs.cs_fatgaryrev >= 0) {
	garyidoffset++;
	garyidoffset &= 7;
	return (currprefs.cs_fatgaryrev << garyidoffset) & 0x80;
    }
    if (addr == 0x43) { /* RAMSEY revision */
	if (currprefs.cs_ramseyrev >= 0)
	    return currprefs.cs_ramseyrev;
    }
    if (addr == 0x03) { /* RAMSEY config */
	if (currprefs.cs_ramseyrev >= 0)
	    return ramsey_config;
    }
    if (addr == 0x02) { /* coldreboot flag */
	if (currprefs.cs_fatgaryrev >= 0)
	    return gary_coldboot ? 0x80 : 0x00;
    }
    if (addr == 0x01) { /* toenb flag */
	if (currprefs.cs_fatgaryrev >= 0)
	    return gary_toenb ? 0x80 : 0x00;
    }
    if (addr == 0x00) { /* timeout flag */
	if (currprefs.cs_fatgaryrev >= 0)
	    return gary_timeout ? 0x80 : 0x00;
    }
    return 0;
}

static uae_u32 REGPARAM3 mbres_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbres_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbres_bget (uaecptr) REGPARAM;
static void REGPARAM3 mbres_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbres_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbres_bput (uaecptr, uae_u32) REGPARAM;

uae_u32 REGPARAM2 mbres_lget (uaecptr addr)
{
    addr &= 0xFFFF;
    return (uae_u32)(mbres_wget (addr) << 16) + mbres_wget (addr + 2);
}
uae_u32 REGPARAM2 mbres_wget (uaecptr addr)
{
    return mbres_read (addr, 2);
}
uae_u32 REGPARAM2 mbres_bget (uaecptr addr)
{
    return mbres_read (addr, 1);
}

void REGPARAM2 mbres_lput (uaecptr addr, uae_u32 value)
{
    mbres_wput (addr, value >> 16);
    mbres_wput (addr + 2, value & 0xffff);
}

void REGPARAM2 mbres_wput (uaecptr addr, uae_u32 value)
{
    mbres_write (addr, value, 2);
}

void REGPARAM2 mbres_bput (uaecptr addr, uae_u32 value)
{
    mbres_write (addr, value, 1);
}

addrbank mbres_bank = {
    mbres_lget, mbres_wget, mbres_bget,
    mbres_lput, mbres_wput, mbres_bput,
    default_xlate, default_check, NULL, "Motherboard Resources",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void mbdmac_write (uae_u32 addr, uae_u32 val, int size)
{
    if (GAYLE_LOG)
	write_log ("DMAC_WRITE %08.8X=%08.8X (%d) PC=%08.8X\n", addr, val, size, M68K_GETPC);
}

static uae_u32 mbdmac_read (uae_u32 addr, int size)
{
    if (GAYLE_LOG)
	write_log ("DMAC_READ %08.8X\n", addr);
    return 0xff;
}

static uae_u32 REGPARAM3 mbdmac_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbdmac_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbdmac_bget (uaecptr) REGPARAM;
static void REGPARAM3 mbdmac_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbdmac_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbdmac_bput (uaecptr, uae_u32) REGPARAM;

uae_u32 REGPARAM2 mbdmac_lget (uaecptr addr)
{
    addr &= 0xFFFF;
    return (uae_u32)(mbdmac_wget (addr) << 16) + mbdmac_wget (addr + 2);
}
uae_u32 REGPARAM2 mbdmac_wget (uaecptr addr)
{
    return mbdmac_read (addr, 2);
}
uae_u32 REGPARAM2 mbdmac_bget (uaecptr addr)
{
    return mbdmac_read (addr, 1);
}

void REGPARAM2 mbdmac_lput (uaecptr addr, uae_u32 value)
{
    mbdmac_wput (addr, value >> 16);
    mbdmac_wput (addr + 2, value & 0xffff);
}

void REGPARAM2 mbdmac_wput (uaecptr addr, uae_u32 value)
{
    mbdmac_write (addr, value, 2);
}

void REGPARAM2 mbdmac_bput (uaecptr addr, uae_u32 value)
{
    mbdmac_write (addr, value, 1);
}

addrbank mbdmac_bank = {
    mbdmac_lget, mbdmac_wget, mbdmac_bget,
    mbdmac_lput, mbdmac_wput, mbdmac_bput,
    default_xlate, default_check, NULL, "DMAC",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

void gayle_reset (int hardreset)
{
    if (hardreset) {
	ramsey_config = 0;
	gary_coldboot = 1;
	gary_timeout = 0;
	gary_toenb = 0;
    }
}
