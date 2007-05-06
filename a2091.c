 /*
  * UAE - The Un*x Amiga Emulator
  *
  * A590/A2091/A3000 (DMAC/SuperDMAC + WD33C93) emulation
  *
  * Copyright 2007 Toni Wilen
  *
  */

#define A2091_DEBUG 0
#define A3000_DEBUG 0
#define WD33C93_DEBUG 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "a2091.h"
#include "blkdev.h"
#include "gui.h"
#include "zfile.h"
#include "threaddep/thread.h"

#define ROM_VECTOR 0x2000
#define ROM_OFFSET 0x2000
#define ROM_SIZE 16384
#define ROM_MASK (ROM_SIZE - 1)

/* SuperDMAC CNTR bits. */
#define SCNTR_TCEN               (1<<5)
#define SCNTR_PREST              (1<<4)
#define SCNTR_PDMD               (1<<3)
#define SCNTR_INTEN              (1<<2)
#define SCNTR_DDIR               (1<<1)
#define SCNTR_IO_DX              (1<<0)
/* DMAC CNTR bits. */
#define CNTR_TCEN               (1<<7)
#define CNTR_PREST              (1<<6)
#define CNTR_PDMD               (1<<5)
#define CNTR_INTEN              (1<<4)
#define CNTR_DDIR               (1<<3)
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

/* wd register names */
#define WD_OWN_ID    0x00
#define WD_CONTROL      0x01
#define WD_TIMEOUT_PERIOD  0x02
#define WD_CDB_1     0x03
#define WD_CDB_2     0x04
#define WD_CDB_3     0x05
#define WD_CDB_4     0x06
#define WD_CDB_5     0x07
#define WD_CDB_6     0x08
#define WD_CDB_7     0x09
#define WD_CDB_8     0x0a
#define WD_CDB_9     0x0b
#define WD_CDB_10    0x0c
#define WD_CDB_11    0x0d
#define WD_CDB_12    0x0e
#define WD_TARGET_LUN      0x0f
#define WD_COMMAND_PHASE   0x10
#define WD_SYNCHRONOUS_TRANSFER 0x11
#define WD_TRANSFER_COUNT_MSB 0x12
#define WD_TRANSFER_COUNT  0x13
#define WD_TRANSFER_COUNT_LSB 0x14
#define WD_DESTINATION_ID  0x15
#define WD_SOURCE_ID    0x16
#define WD_SCSI_STATUS     0x17
#define WD_COMMAND      0x18
#define WD_DATA      0x19
#define WD_QUEUE_TAG    0x1a
#define WD_AUXILIARY_STATUS   0x1f 
/* WD commands */
#define WD_CMD_RESET    0x00
#define WD_CMD_ABORT    0x01
#define WD_CMD_ASSERT_ATN  0x02
#define WD_CMD_NEGATE_ACK  0x03
#define WD_CMD_DISCONNECT  0x04
#define WD_CMD_RESELECT    0x05
#define WD_CMD_SEL_ATN     0x06
#define WD_CMD_SEL      0x07
#define WD_CMD_SEL_ATN_XFER   0x08
#define WD_CMD_SEL_XFER    0x09
#define WD_CMD_RESEL_RECEIVE  0x0a
#define WD_CMD_RESEL_SEND  0x0b
#define WD_CMD_WAIT_SEL_RECEIVE 0x0c
#define WD_CMD_TRANS_ADDR  0x18
#define WD_CMD_TRANS_INFO  0x20
#define WD_CMD_TRANSFER_PAD   0x21          
#define WD_CMD_SBT_MODE    0x80

#define CSR_MSGIN    0x20      
#define CSR_SDP         0x21
#define CSR_SEL_ABORT      0x22
#define CSR_RESEL_ABORT    0x25
#define CSR_RESEL_ABORT_AM 0x27   
#define CSR_ABORT    0x28

static int configured;
static uae_u8 dmacmemory[100];
static uae_u8 *rom;

static uae_u32 dmac_istr, dmac_cntr;
static uae_u32 dmac_dawr;
static uae_u32 dmac_acr;
static uae_u32 dmac_wtc;
static int dmac_dma;
static uae_u8 sasr, scmd, auxstatus;
static int wd_used;

static int superdmac;

static uae_u8 wdregs[32];

#define WD33C93 "WD33C93"

void rethink_a2091(void)
{
    if (dmac_istr & ISTR_INTS)
	INTREQ_0 (0x8000 | 0x0008);
}

static void INT2(void)
{
    int irq = 0;

    if (!(auxstatus & 0x80))
	return;
    dmac_istr |= ISTR_INTS;
    if (superdmac) {
	if ((dmac_cntr & SCNTR_INTEN) && (dmac_istr & ISTR_INTS))
	    irq = 1;
    } else {
        if ((dmac_cntr & CNTR_INTEN) && (dmac_istr & ISTR_INTS))
	    irq = 1;
    }
    if (irq) {
	if (!(intreq & 8))
	    INTREQ_f(0x8000 | 0x0008);
    }
}

static void dmac_reset(void)
{
#if WD33C93_DEBUG > 0
    if (superdmac)
	write_log("A3000 %s SCSI reset\n", WD33C93);
    else
	write_log("A2091 %s SCSI reset\n", WD33C93);
#endif
}

static void incsasr(void)
{
    if (sasr == WD_AUXILIARY_STATUS || sasr == WD_DATA || sasr == WD_COMMAND)
	return;
    sasr++;
    sasr &= 0x1f;
}

static void dmac_cint(void)
{
    dmac_istr = 0;
}

static void set_status(uae_u8 status)
{
    wdregs[WD_SCSI_STATUS] = status;
    auxstatus |= 0x80;
    INT2();
}

static void wd_cmd_sel(void)
{
#if WD33C93_DEBUG > 0
    write_log("%s select, ID=%d\n", WD33C93, wdregs[WD_DESTINATION_ID]);
#endif
    set_status(0x42);
}

static void wd_cmd_abort(void)
{
#if WD33C93_DEBUG > 0
    write_log("%s abort\n", WD33C93);
#endif
    set_status(0x22);
}

static void putwd(uae_u8 d)
{
#if WD33C93_DEBUG > 1
    write_log("%s REG %02.2X (%d) = %02.2X (%d)\n", WD33C93, sasr, sasr, d, d);
#endif
    wdregs[sasr] = d;
    if (!wd_used) {
	wd_used = 1;
	write_log("%s in use\n", WD33C93);
    }
    if (sasr == WD_COMMAND) {
	switch (d)
	{
	    case WD_CMD_SEL_ATN:
	    case WD_CMD_SEL:
	        wd_cmd_sel();
	    break;
	}
    }
    incsasr();
}
static uae_u8 getwd(void)
{
    uae_u8 v;
    
    v = wdregs[sasr];
    if (sasr == WD_SCSI_STATUS) {
	auxstatus &= ~0x80;
	dmac_istr &= ~ISTR_INTS;
    }
    incsasr();
    return v;
}

static uae_u32 dmac_bget2 (uaecptr addr)
{
    uae_u32 v = 0;

    if (addr < 0x40)
	return dmacmemory[addr];
    if (addr >= ROM_OFFSET)
	return rom[addr & ROM_MASK];

    switch (addr)
    {
	case 0x41:
	v = dmac_istr;
	if (v)
	    v |= ISTR_INT_P;
	break;
	case 0x43:
	v = dmac_cntr;
	break;
	case 0x91:
	v = auxstatus;
	break;
	case 0x93:
	v = getwd();
	break;
	/* XT IO */
	case 0xa1:
	case 0xa3:
	case 0xa5:
	case 0xa7:
	v = 0xff;
	break;
    }    
#if A2091_DEBUG > 0
    write_log ("dmac_bget %04.4X=%02.2X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

static void dmac_bput2 (uaecptr addr, uae_u32 b)
{
    if (addr < 0x40)
	return;
    if (addr >= ROM_OFFSET)
	return;

    switch (addr)
    {
	case 0x43:
	dmac_cntr = b;
	if (dmac_cntr & CNTR_PREST)
	    dmac_reset ();
	break;
	case 0x80:
	dmac_wtc &= 0x00ffffff;
	dmac_wtc |= b << 24;
	break;
	case 0x81:
	dmac_wtc &= 0xff00ffff;
	dmac_wtc |= b << 16;
	break;
	case 0x82:
	dmac_wtc &= 0xffff00ff;
	dmac_wtc |= b << 8;
	break;
	case 0x83:
	dmac_wtc &= 0xffffff00;
	dmac_wtc |= b << 0;
	break;
	case 0x84:
	dmac_acr &= 0x00ffffff;
	dmac_acr |= b << 24;
	break;
	case 0x85:
	dmac_acr &= 0xff00ffff;
	dmac_acr |= b << 16;
	break;
	case 0x86:
	dmac_acr &= 0xffff00ff;
	dmac_acr |= b << 8;
	break;
	case 0x87:
	dmac_acr &= 0xffffff01;
	dmac_acr |= (b & ~ 1) << 0;
	break;
	case 0x8e:
	dmac_dawr &= 0x00ff;
	dmac_dawr |= b << 8;
	break;
	case 0x8f:
	dmac_dawr &= 0xff00;
	dmac_dawr |= b << 0;
	break;
	case 0x91:
	sasr = b;
	break;
	case 0x93:
	putwd(b);
	break;
	case 0xe0:
	case 0xe1:
	if (!dmac_dma) {
	    dmac_dma = 1;
	    write_log("a2091 dma started\n");
	}
	break;
	case 0xe2:
	case 0xe3:
	dmac_dma = 0;
	break;
	case 0xe4:
	case 0xe5:
	dmac_cint();
	break;
	case 0xe8:
	case 0xe9:
	dmac_dma = 0;
	break;
    }
#if A2091_DEBUG > 0
    write_log ("dmac_bput %04.4X=%02.2X PC=%08.8X\n", addr, b & 255, M68K_GETPC);
#endif
}



static uae_u32 REGPARAM2 dmac_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = (dmac_bget2 (addr) << 24) | (dmac_bget2 (addr + 1) << 16) |
	(dmac_bget2 (addr + 2) << 8) | (dmac_bget2 (addr + 3));
#ifdef A2091_DEBUG
    if (addr >= 0x40 && addr < ROM_OFFSET)
	write_log ("dmac_lget %08.8X=%08.8X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

static uae_u32 REGPARAM2 dmac_wget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = (dmac_bget2 (addr) << 8) | dmac_bget2 (addr + 1);
#if A2091_DEBUG > 0
    if (addr >= 0x40 && addr < ROM_OFFSET)
	write_log ("dmac_wget %08.8X=%04.4X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

static uae_u32 REGPARAM2 dmac_bget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = dmac_bget2 (addr);
    if (!configured)
	return v;
    return v;
}

static void REGPARAM2 dmac_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    addr &= 65535;
#if A2091_DEBUG > 0
    if (addr >= 0x40 && addr < ROM_OFFSET)
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
    addr &= 65535;
#if A2091_DEBUG > 0
    if (addr >= 0x40 && addr < ROM_OFFSET)
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
    b &= 0xff;
    addr &= 65535;
    if (addr == 0x48) {
	map_banks (&dmaca2091_bank, b, 0x10000 >> 16, 0x10000);
	write_log ("A590/A2091 autoconfigured at %02.2X0000\n", b);
	configured = 1;
	expamem_next();
	return;
    }
    if (addr == 0x4c) {
	write_log ("A590/A2091 DMAC AUTOCONFIG SHUT-UP!\n");
	configured = 1;
	expamem_next();
	return;
    }
    if (!configured)
	return;
    dmac_bput2 (addr, b);
}

static uae_u32 REGPARAM2 dmac_wgeti (uaecptr addr)
{
    uae_u32 v = 0xffff;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    if (addr >= ROM_OFFSET)
	v = (rom[addr & ROM_MASK] << 8) | rom[(addr + 1) & ROM_MASK];
    return v;
}
static uae_u32 REGPARAM2 dmac_lgeti (uaecptr addr)
{
    uae_u32 v = 0xffff;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = (dmac_wgeti(addr) << 16) | dmac_wgeti(addr + 2);
    return v;
}

addrbank dmaca2091_bank = {
    dmac_lget, dmac_wget, dmac_bget,
    dmac_lput, dmac_wput, dmac_bput,
    default_xlate, default_check, NULL, "A2091/A590",
    dmac_lgeti, dmac_wgeti, ABFLAG_IO
};


static void dmac_start_dma(void)
{
}
static void dmac_stop_dma(void)
{
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
    if (currprefs.cs_mbdmac > 1)
	return;
#if A3000_DEBUG > 0
    write_log ("DMAC_WRITE %08.8X=%02.2X PC=%08.8X\n", addr, val & 0xff, M68K_GETPC);
#endif
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
	if (dmac_cntr & SCNTR_PREST)
	    dmac_reset();
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
	putwd(val);
	break;
    }
}

static uae_u32 mbdmac_read (uae_u32 addr)
{
    uae_u32 vaddr = addr;
    uae_u32 v = 0xffffffff;
    
    if (currprefs.cs_mbdmac > 1)
	return 0;

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
	if (v)
	    v |= ISTR_INT_P;
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
	v = getwd();
	break;
    }
#if A3000_DEBUG > 0
    write_log ("DMAC_READ %08.8X=%02.2X PC=%X\n", vaddr, v & 0xff, M68K_GETPC);
#endif
    return v;
}


static uae_u32 REGPARAM3 mbdmac_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbdmac_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 mbdmac_bget (uaecptr) REGPARAM;
static void REGPARAM3 mbdmac_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbdmac_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 mbdmac_bput (uaecptr, uae_u32) REGPARAM;

static uae_u32 REGPARAM2 mbdmac_lget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return (mbdmac_wget (addr) << 16) | mbdmac_wget (addr + 2);
}
static uae_u32 REGPARAM2 mbdmac_wget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return (mbdmac_bget (addr) << 8) | mbdmac_bget(addr + 1);;
}
static uae_u32 REGPARAM2 mbdmac_bget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return mbdmac_read (addr);
}
static void REGPARAM2 mbdmac_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    mbdmac_wput (addr, value >> 16);
    mbdmac_wput (addr + 2, value & 0xffff);
}
static void REGPARAM2 mbdmac_wput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    mbdmac_bput (addr, value);
    mbdmac_bput (addr, value + 1);
}
static void REGPARAM2 mbdmac_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    mbdmac_write (addr, value);
}

addrbank mbdmac_a3000_bank = {
    mbdmac_lget, mbdmac_wget, mbdmac_bget,
    mbdmac_lput, mbdmac_wput, mbdmac_bput,
    default_xlate, default_check, NULL, "A3000 DMAC",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void ew (int addr, uae_u32 value)
{
    addr &= 0xffff;
    if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
	dmacmemory[addr] = (value & 0xf0);
	dmacmemory[addr + 2] = (value & 0x0f) << 4;
    } else {
	dmacmemory[addr] = ~(value & 0xf0);
	dmacmemory[addr + 2] = ~((value & 0x0f) << 4);
    }
}
void a2091_free (void)
{
}

void a2091_reset (void)
{
    configured = 0;
    wd_used = 0;
    superdmac = 0;
    superdmac = currprefs.cs_mbdmac ? 1 : 0;
}

void a2091_init (void)
{
    struct zfile *z;
    char path[MAX_DPATH];

    configured = 0;
    memset (dmacmemory, 0xff, 100);
    ew (0x00, 0xc0 | 0x01 | 0x10);
    /* A590/A2091 hardware id */
    ew (0x04, 0x03);
    ew (0x08, 0x40);
    /* commodore's manufacturer id */
    ew (0x10, 0x02);
    ew (0x14, 0x02);
    /* rom vector */
    ew (0x28, ROM_VECTOR >> 8);
    ew (0x2c, ROM_VECTOR);
    /* KS autoconfig handles the rest */
    map_banks (&dmaca2091_bank, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
    if (!rom) {
	fetch_datapath (path, sizeof path);
	strcat (path, "roms\\a2091_rev7.rom");
	write_log("A590/A2091 ROM path: '%s'\n", path);
	z = zfile_fopen(path, "rb");
	if (z) {
	    rom = xmalloc (ROM_SIZE);
	    zfile_fread (rom, ROM_SIZE, 1, z);
	    zfile_fclose(z);
	}
    }
}
