 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Gayle (and motherboard resources) memory bank
  *
  * (c) 2006 Toni Wilen
  */

#define GAYLE_LOG 0

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
DD0000 to DDFFFF		64 KB A3000 DMA controller
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

/* CNTR bits. */ 
#define CNTR_TCEN               (1<<5)
#define CNTR_PREST              (1<<4)
#define CNTR_PDMD               (1<<3)
#define CNTR_INTEN              (1<<2)
#define CNTR_DDIR               (1<<1)
#define CNTR_IO_DX              (1<<0)
/* ISTR bits. */
#define ISTR_INTX               (1<<8)
#define ISTR_INT_F              (1<<7)
#define ISTR_INTS               (1<<6)  
#define ISTR_E_INT              (1<<5)
#define ISTR_INT_P              (1<<4)
#define ISTR_UE_INT             (1<<3)  
#define ISTR_OE_INT             (1<<2)
#define ISTR_FF_FLG             (1<<1)
#define ISTR_FE_FLG             (1<<0)  

static uae_u32 dmac_wtc, dmac_cntr, dmac_acr, dmac_istr, dmac_dawr, dmac_dma;
static uae_u8 sasr, scmd;
static int wdcmd_active;

static void wd_cmd(uae_u8 data)
{
    switch (sasr)
    {
        case 0x15:
        write_log("DESTINATION ID: %02.2X\n", data);
	break;
	case 0x18:
        write_log("COMMAND: %02.2X\n", data);
	wdcmd_active = 10;
	break;
	case 0x19:
        write_log("DATA: %02.2X\n", data);
	break;
	default:
	write_log("unsupported WD33C33A register %02.2X\n", sasr);
	break;
    }
}

void mbdmac_hsync(void)
{
    if (wdcmd_active == 0)
	return;
    wdcmd_active--;
    if (wdcmd_active > 0)
	return;
    if (!(dmac_cntr & CNTR_INTEN)) {
	wdcmd_active = 1;
	return;
    }
    dmac_istr |= ISTR_INT_P | ISTR_E_INT | ISTR_INTS;
}

static void dmac_start_dma(void)
{
    dmac_istr |= ISTR_E_INT;
}
static void dmac_stop_dma(void)
{
    dmac_dma = 0;
}
static void dmac_cint(void)
{
    dmac_istr = 0;
}
static void dmacreg_write(uae_u32 *reg, int addr, uae_u32 val, int size)
{
    addr = (size - 1) - addr;
    (*reg) &= ~(0xff << (addr * 8));
    (*reg) |= (val & 0xff) << (addr * 8);
}
static uae_u32 dmacreg_read(uae_u32 val, int addr, int size)
{
    addr = (size - 1) - addr;
    return (val >> (addr * 8)) & 0xff;
}

static void mbdmac_write (uae_u32 addr, uae_u32 val)
{
    if (GAYLE_LOG)
	write_log ("DMAC_WRITE %08.8X=%02.2X PC=%08.8X\n", addr, val & 0xff, M68K_GETPC);
    addr &= 0xffff;
    switch (addr)
    {
	case 0x02:
	case 0x03:
	dmacreg_write(&dmac_dawr, addr - 0x02, val, 2);
	break;
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
	dmacreg_write(&dmac_wtc, addr - 0x04, val, 4);
	break;
	case 0x0a:
	case 0x0b:
	dmacreg_write(&dmac_cntr, addr - 0x0a, val, 2);
	break;
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
	dmacreg_write(&dmac_acr, addr - 0x0c, val, 4);
	break;
	case 0x12:
	case 0x13:
	if (!dmac_dma) {
	    dmac_dma = 1;
	    dmac_start_dma();
	}
	break;
	case 0x16:
	case 0x17:
	/* FLUSH */
	break;
	case 0x1a:
	case 0x1b:
	dmac_cint();
	break;
	case 0x1e:
	case 0x1f:
	/* ISTR */
	break;
	case 0x3e:
	case 0x3f:
	dmac_dma = 0;
	dmac_stop_dma();
	break;
	case 0x49:
	sasr = val;
	break;
	case 0x43:
	wd_cmd(val);
	break;
    }
}

static uae_u32 mbdmac_read (uae_u32 addr)
{
    uae_u32 vaddr = addr;
    uae_u32 v = 0xffffffff;
    addr &= 0xffff;
    switch (addr)
    {
	case 0x02:
	case 0x03:
	v = dmacreg_read(dmac_dawr, addr - 0x02, 2);
	break;
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
	v = dmacreg_read(dmac_wtc, addr - 0x04, 4);
	break;
	case 0x0a:
	case 0x0b:
	v = dmacreg_read(dmac_cntr, addr - 0x0a, 2);
	break;
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
	v = dmacreg_read(dmac_acr, addr - 0x0c, 4);
	break;
	case 0x12:
	case 0x13:
	if (dmac_dma) {
	    dmac_dma = 1;
	    dmac_start_dma();
	}
	v = 0;
	break;
	case 0x1a:
	case 0x1b:
	dmac_cint();
	v = 0;
	break;;
	case 0x1e:
	case 0x1f:
	v = dmacreg_read(dmac_istr, addr - 0x1e, 2);
	break;
	case 0x3e:
	case 0x3f:
	dmac_dma = 0;
	dmac_stop_dma();
	v = 0;
	break;
	case 0x49:
	v = sasr;
	break;
	case 0x43:
	v = scmd;
	break;
    }
    if (GAYLE_LOG)
	write_log ("DMAC_READ %08.8X=%02.2X PC=%X\n", vaddr, v & 0xff, M68K_GETPC);
    return v;
}

static uae_u32 REGPARAM3 mbdmac_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbdmac_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbdmac_bget (uaecptr) REGPARAM;
static void REGPARAM3 mbdmac_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbdmac_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbdmac_bput (uaecptr, uae_u32) REGPARAM;

uae_u32 REGPARAM2 mbdmac_lget (uaecptr addr)
{
    return (mbdmac_wget (addr) << 16) | mbdmac_wget (addr + 2);
}
uae_u32 REGPARAM2 mbdmac_wget (uaecptr addr)
{
    return (mbdmac_bget (addr) << 8) | mbdmac_bget(addr + 1);;
}
uae_u32 REGPARAM2 mbdmac_bget (uaecptr addr)
{
    return mbdmac_read (addr);
}

void REGPARAM2 mbdmac_lput (uaecptr addr, uae_u32 value)
{
    mbdmac_wput (addr, value >> 16);
    mbdmac_wput (addr + 2, value & 0xffff);
}

void REGPARAM2 mbdmac_wput (uaecptr addr, uae_u32 value)
{
    mbdmac_bput (addr, value);
    mbdmac_bput (addr, value + 1);
}

void REGPARAM2 mbdmac_bput (uaecptr addr, uae_u32 value)
{
    mbdmac_write (addr, value);
}

addrbank mbdmac_bank = {
    mbdmac_lget, mbdmac_wget, mbdmac_bget,
    mbdmac_lput, mbdmac_wput, mbdmac_bput,
    default_xlate, default_check, NULL, "A3000 DMAC",
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
