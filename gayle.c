 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Gayle (and motherboard resources) memory bank
  *
  * (c) 2006 - 2007 Toni Wilen
  */

#define GAYLE_LOG 0
#define IDE_LOG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "gayle.h"
#include "zfile.h"
#include "filesys.h"
#include "savestate.h"
#include "gui.h"

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

/* Gayle definitions from Linux drivers */

/* PCMCIA stuff */

#define GAYLE_RAM               (0x600000+zTwoBase)
#define GAYLE_RAMSIZE           (0x400000)
#define GAYLE_ATTRIBUTE         (0xa00000+zTwoBase)
#define GAYLE_ATTRIBUTESIZE     (0x020000)
#define GAYLE_IO                (0xa20000+zTwoBase)     /* 16bit and even 8bit registers */
#define GAYLE_IOSIZE            (0x010000)
#define GAYLE_IO_8BITODD        (0xa30000+zTwoBase)     /* odd 8bit registers */
/* offset for accessing odd IO registers */
#define GAYLE_ODD               (GAYLE_IO_8BITODD-GAYLE_IO-1)

#define GAYLE_ADDRESS   (0xda8000)      /* gayle main registers base address */
#define GAYLE_RESET     (0xa40000)      /* write 0x00 to start reset,
                                           read 1 byte to stop reset */
/* GAYLE_CARDSTATUS bit def */
#define GAYLE_CS_CCDET          0x40    /* credit card detect */
#define GAYLE_CS_BVD1           0x20    /* battery voltage detect 1 */
#define GAYLE_CS_SC             0x20    /* credit card status change */
#define GAYLE_CS_BVD2           0x10    /* battery voltage detect 2 */
#define GAYLE_CS_DA             0x10    /* digital audio */
#define GAYLE_CS_WR             0x08    /* write enable (1 == enabled) */
#define GAYLE_CS_BSY            0x04    /* credit card busy */
#define GAYLE_CS_IRQ            0x04    /* interrupt request */

/* GAYLE_CONFIG bit def
   (bit 0-1 for program voltage, bit 2-3 for access speed */    
#define GAYLE_CFG_0V            0x00
#define GAYLE_CFG_5V            0x01
#define GAYLE_CFG_12V           0x02

#define GAYLE_CFG_100NS         0x08
#define GAYLE_CFG_150NS         0x04
#define GAYLE_CFG_250NS         0x00      
#define GAYLE_CFG_720NS         0x0c     

/* IDE stuff */

/*  Bases of the IDE interfaces */
#define GAYLE_BASE_4000 0xdd2020    /* A4000/A4000T */
#define GAYLE_BASE_1200 0xda0000    /* A1200/A600 and E-Matrix 530 */
/* IDE drive registers */
#define IDE_DATA	0x00
#define IDE_ERROR	0x01	    /* see err-bits */
#define IDE_NSECTOR	0x02	    /* nr of sectors to read/write */
#define IDE_SECTOR	0x03	    /* starting sector */
#define IDE_LCYL	0x04	    /* starting cylinder */
#define IDE_HCYL	0x05	    /* high byte of starting cyl */
#define IDE_SELECT	0x06	    /* 101dhhhh , d=drive, hhhh=head */
#define IDE_STATUS	0x07	    /* see status-bits */
#define IDE_DEVCON	0x0406
#define IDE_DRVADDR	0x0407
/* STATUS bits */
#define IDE_STATUS_ERR 0x01
#define IDE_STATUS_IDX 0x02
#define IDE_STATUS_DRQ 0x08
#define IDE_STATUS_DSC 0x10
#define IDE_STATUS_DRDY 0x40
#define IDE_STATUS_BSY 0x80
/* ERROR bits */
#define IDE_ERR_ABRT 0x04

/*
 *  These are at different offsets from the base
 */
#define GAYLE_IRQ_4000  0x3020    /* MSB = 1, Harddisk is source of interrupt */
#define GAYLE_IRQ_1200  0x9000
#define GAYLE_INT_1200  0xA000

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

struct ide_hdf
{
    struct hardfiledata hfd;
    int cyls, heads, secspertrack;
    int cyls_def, heads_def, secspertrack_def;
    uae_u64 size;
    char *path;

    uae_u8 secbuf[512 * 256];
    int data_offset;
    int data_size;
    int data_multi;
    uae_u8 multiple_mode;
    uae_u8 status;
    int irq_delay;
    int num;
};

static struct ide_hdf idedrive[4];

static int gayle_id_cnt;
static uae_u8 gayle_irq, gayle_intena;
static uae_u8 ide_select, ide_nsector, ide_sector, ide_lcyl, ide_hcyl, ide_devcon, ide_error, ide_feat;
static uae_u8 ide_nsector2, ide_sector2, ide_lcyl2, ide_hcyl2, ide_feat2;
static int ide_drv, ide2, ide_splitter;

static struct ide_hdf *ide;

STATIC_INLINE pw(int offset, uae_u16 w)
{
    ide->secbuf[offset * 2 + 0] = (uae_u8)w;
    ide->secbuf[offset * 2 + 1] = w >> 8;
}
static void ps(int offset, char *s, int max)
{
    int i, len;

    offset *= 2;
    len = strlen(s);
    for (i = 0; i < max; i++) {
	char c = ' ';
	if (i < len)
	    c = s[i];
	ide->secbuf[offset ^ 1] = c;
	offset++;
    }
}

static void ide_interrupt(void)
{
    ide->status |= IDE_STATUS_BSY;
    ide->irq_delay = 2;
}
static void ide_interrupt_do(struct ide_hdf *ide)
{
    ide->status &= ~IDE_STATUS_BSY;
    if (gayle_intena & GAYLE_IRQ_IDE) {
	gayle_irq |= GAYLE_IRQ_IDE;
	INTREQ_f (0x8000 | 0x0008);
    }
}

static void ide_fail(void)
{
    ide_error |= IDE_ERR_ABRT;
    if (ide_drv == 1 && idedrive[ide2 + 1].size == 0)
	idedrive[ide2].status |= IDE_STATUS_ERR;
    ide->status |= IDE_STATUS_ERR;
    ide_interrupt();
}

static void ide_data_ready(int blocks)
{
    memset(ide->secbuf, 0, 512 * 256);
    ide->data_offset = 0;
    ide->status |= IDE_STATUS_DRQ;
    ide->data_size = blocks * 512;
    ide->data_multi = 1;
    if (!(ide_devcon & 2))
	ide_interrupt();
}

static void ide_recalibrate(void)
{
    write_log("IDE%d recalibrate\n", ide->num);
    ide_sector = 0;
    ide_lcyl = ide_hcyl = 0;
    ide_interrupt();
}
static void ide_identify_drive(void)
{
    int totalsecs;
    int v;
    uae_u8 *buf = ide->secbuf;

    if (ide->size == 0) {
	ide_fail();
	return;
    }
    if (IDE_LOG > 0)
	write_log("IDE%d identify drive\n", ide->num);
    ide_data_ready(1);
    pw(0, (1 << 10) | (1 << 6) | (1 << 2) | (1 << 3));
    pw(1, ide->cyls_def);
    pw(3, ide->heads_def);
    pw(4, 512 * ide->secspertrack_def);
    pw(5, 512);
    pw(6, ide->secspertrack_def);
    ps(10, "68000", 20); /* serial */
    pw(20, 3);
    pw(21, 512);
    ps(23, "0.2", 8); /* firmware revision */
    ps(27, "UAE-IDE", 40); /* model */
    pw(47, 128); /* max 128 sectors multiple mode */
    pw(48, 1);
    pw(49, (1 << 9) | (1 << 8)); /* LBA and DMA supported */
    pw(51, 240 << 8); /* PIO0 to PIO2 supported */
    pw(53, 1 | 2);
    pw(54, ide->cyls);
    pw(55, ide->heads);
    pw(56, ide->secspertrack);
    totalsecs = ide->cyls * ide->heads * ide->secspertrack;
    pw(57, totalsecs);
    pw(58, totalsecs >> 16);
    v = idedrive[ide_drv].multiple_mode;
    pw(59, (v > 0 ? 0x100 : 0) | v);
    totalsecs = ide->size / 512;
    pw(60, totalsecs);
    pw(61, totalsecs >> 16);
    pw(62, 0x0f);
    pw(63, 0x0f);
    pw(64, 0x03); /* PIO3 and PIO4 */
    pw(65, 120 << 8); /* MDMA2 supported */
    pw(66, 120 << 8);
    pw(67, 120 << 8);
    pw(68, 120 << 8);
    pw(80, (1 << 1) | (1 << 2) | (1 << 3)); /* ATA-1 to ATA-3 */
    pw(81, 0x000A); /* ATA revision */
}
static void ide_initialize_drive_parameters(void)
{
    if (ide->size) {
	ide->secspertrack = ide_nsector == 0 ? 256 : ide_nsector;
	ide->heads = (ide_select & 15) + 1;
	ide->cyls = (ide->size / 512) / (ide->secspertrack * ide->heads);
	if (ide->heads * ide->cyls * ide->secspertrack > 16515072) {
	    ide->cyls = ide->cyls_def;
	    ide->heads = ide->heads_def;
	    ide->secspertrack = ide->secspertrack_def;
	}
    } else {
	ide_error |= IDE_ERR_ABRT;
	ide->status |= IDE_STATUS_ERR;
    }
    write_log("IDE%d initialize drive parameters, CYL=%d,SPT=%d,HEAD=%d\n",
	ide->num, ide->cyls, ide->secspertrack, ide->heads);
    ide_interrupt();
}
static void ide_set_multiple_mode(void)
{
    write_log("IDE%d drive multiple mode = %d\n", ide->num, ide_nsector);
    ide->multiple_mode = ide_nsector;
    ide_interrupt();
}
static void ide_set_features(void)
{
    write_log("IDE%d set features %02.2X (%02.2X)\n", ide->num, ide_feat, ide_nsector);
    ide_fail();
}

static void get_lbachs(struct ide_hdf *ide, int *lba, int *cyl, int *head, int *sec)
{
    if (ide_select & 0x40) {
	*lba = ((ide_select & 15) << 24) | (ide_hcyl << 16) | (ide_lcyl << 8) | ide_sector;
    } else {
	*cyl = (ide_hcyl << 8) | ide_lcyl;
	*head = ide_select & 15;
	*sec = ide_sector;
	*lba = (((*cyl) * ide->heads + (*head)) * ide->secspertrack) + (*sec) - 1;
    }
}
static void put_lbachs(struct ide_hdf *ide, int lba, int cyl, int head, int sec, int inc)
{
    if (ide_select & 0x40) {
	lba += inc;
	ide_select &= ~15;
	ide_select |= (lba >> 24) & 15;
	ide_hcyl = (lba >> 16) & 0xff;
	ide_lcyl = (lba >> 8) & 0xff;
	ide_sector = lba & 0xff;
    } else {
	sec += inc;
	while (sec >= ide->secspertrack) {
	    sec -= ide->secspertrack;
	    head++;
	    if (head >= ide->heads) {
		head -= ide->heads;
		cyl++;
	    }
	}
	ide_select &= ~15;
	ide_select |= head;
	ide_sector = sec;
	ide_hcyl = cyl >> 8;
	ide_lcyl = (uae_u8)cyl;
    }
    ide_nsector = 0;
}

static void ide_read_sectors(int multi)
{
    int cyl, head, sec, nsec, lba;

    if (multi && ide->multiple_mode == 0) {
	ide_fail();
	return;
    }
    gui_hd_led (1);
    nsec = ide_nsector == 0 ? 256 : ide_nsector;
    get_lbachs(ide, &lba, &cyl, &head, &sec);
    if (IDE_LOG > 0)
	write_log("IDE%d read offset=%d, sectors=%d (%d)\n", ide->num, lba, nsec, ide->multiple_mode);
    if (multi && ide->multiple_mode > nsec)
	nsec = ide->multiple_mode;
    if (lba * 512 >= ide->size) {
	ide_fail();
	return;
    }
    if (nsec * 512 > ide->size - lba * 512)
	nsec = (ide->size - lba * 512) / 512;
    ide_data_ready(nsec);
    hdf_read (&ide->hfd, ide->secbuf, lba * 512, nsec * 512);
    put_lbachs(ide, lba, cyl, head, sec, nsec);
    ide->data_multi = multi ? ide->multiple_mode : 1;
}
static void ide_write_sectors(int multi)
{
    int cyl, head, sec, nsec, lba;

    if (multi && ide->multiple_mode == 0) {
	ide_fail();
	return;
    }
    gui_hd_led (2);
    nsec = ide_nsector == 0 ? 256 : ide_nsector;
    get_lbachs(ide, &lba, &cyl, &head, &sec);
    if (lba * 512 >= ide->size) {
	ide_fail();
	return;
    }
    if (IDE_LOG > 0)
	write_log("IDE%d write offset=%d, sectors=%d (%d)\n", ide->num, lba, nsec, ide->multiple_mode);
    if (nsec * 512 > ide->size - lba * 512)
	nsec = (ide->size - lba * 512) / 512;
    ide_data_ready(nsec);
    ide->data_multi = multi ? ide->multiple_mode : 1;
}

static void ide_do_command(uae_u8 cmd)
{
    if (IDE_LOG > 1)
	write_log("**** IDE%d command %02.2X\n", ide->num, cmd);
    ide->status &= ~(IDE_STATUS_DRDY | IDE_STATUS_DRQ | IDE_STATUS_ERR);
    ide_error = 0;

    if (cmd == 0x10) { /* recalibrate */
	ide_recalibrate();
    } else if (cmd == 0xec) { /* identify drive */
	ide_identify_drive();
    } else if (cmd == 0x91) { /* initialize drive parameters */
	ide_initialize_drive_parameters();
    } else if (cmd == 0xc6) { /* set multiple mode */
	ide_set_multiple_mode();
    } else if (cmd == 0x20 || cmd == 0x21) { /* read sectors */
	ide_read_sectors(0);
    } else if (cmd == 0x30 || cmd == 0x31) { /* write sectors */
	ide_write_sectors(0);
    } else if (cmd == 0xc4) { /* read multiple */
	ide_read_sectors(1);
    } else if (cmd == 0xc5) { /* write multiple */
	ide_write_sectors(1);
    } else if (cmd == 0x50) { /* format track (nop) */
	ide_interrupt();
    } else if (cmd == 0xa1) { /* ATAPI identify (IDE HD is not ATAPI) */
	ide_fail();
    } else if (cmd == 0xef) { /* set features  */
	ide_set_features();
    } else {
	ide_fail();
	write_log("IDE: unknown command %x\n", cmd);
    }
}

static uae_u16 ide_get_data(void)
{
    int irq = 0;
    uae_u16 v;

    if (ide->data_size == 0) {
	if (IDE_LOG > 0)
	    write_log("IDE%d DATA read without DRQ!?\n", ide->num);
	if (ide->size == 0)
	    return 0xffff;
	return 0;
    }
    v = ide->secbuf[ide->data_offset + 1] | (ide->secbuf[ide->data_offset + 0] << 8);
    ide->data_offset+=2;
    ide->data_size-=2;
    if (((ide->data_offset % 512) == 0) && ((ide->data_offset / 512) % ide->data_multi) == 0)
        irq = 1;
    if (ide->data_size == 0) {
	irq = 1;
	ide->status &= ~IDE_STATUS_DRQ;
    }
    if (irq)
	ide_interrupt();
    return v;
}
static void ide_put_data(uae_u16 v)
{
    int irq = 0;

    if (ide->data_size == 0) {
	if (IDE_LOG > 0)
	    write_log("IDE%d DATA write without DRQ!?\n", ide->num);
	return;
    }
    ide->secbuf[ide->data_offset + 1] = v & 0xff;
    ide->secbuf[ide->data_offset + 0] = v >> 8;
    ide->data_offset+=2;
    ide->data_size-=2;
    if (((ide->data_offset % 512) == 0) && ((ide->data_offset / 512) % ide->data_multi) == 0) {
	if (IDE_LOG > 0)
	    write_log("IDE%d write interrupt, %d total bytes transferred so far\n", ide->num, ide->data_offset);
	irq = 1;
    }
    if (ide->data_size == 0) {
	int lba, cyl, head, sec, nsec;
	ide->status &= ~IDE_STATUS_DRQ;
	nsec = ide->data_offset / 512;
	get_lbachs(ide, &lba, &cyl, &head, &sec);
	if (IDE_LOG > 0)
	    write_log("IDE%d write finished, %d bytes (%d) written\n", ide->num, ide->data_offset, ide->data_offset / 512);
	hdf_write(&ide->hfd, ide->secbuf, lba * 512, ide->data_offset);
	put_lbachs(ide, lba, cyl, head, sec, nsec);
	irq = 1;
    }
    if (irq)
	ide_interrupt();
}

static int get_ide_reg(uaecptr addr)
{
    uaecptr a = addr;
    addr &= 0xffff;
    if (addr >= 0x3020 && addr <= 0x3021 && currprefs.cs_ide == 2)
	return -1;
    addr &= ~0x2020;
    addr >>= 2;
    ide2 = 0;
    if (addr & 0x400) {
	if (ide_splitter ) {
	    ide2 = 2;
	    addr &= ~0x400;
	}
    }
    ide = &idedrive[ide_drv + ide2];
    return addr;
}

static uae_u32 ide_read (uaecptr addr)
{
    int ide_reg;
    uae_u8 v = 0;

    addr &= 0xffff;
    if (IDE_LOG > 1 && addr != 0x2000 && addr != 0x2001 && addr != 0x2020 && addr != 0x2021 && addr != GAYLE_IRQ_1200)
	write_log ("IDE_READ %08.8X PC=%X\n", addr, M68K_GETPC);
    if (currprefs.cs_ide <= 0) {
	if (addr == 0x201c) // AR1200 IDE detection hack
	    return 0;
	return 0xff;
    }
    if (addr >= GAYLE_IRQ_4000 && addr <= GAYLE_IRQ_4000 + 1 && currprefs.cs_ide == 2) {
        uae_u8 v = gayle_irq;
        gayle_irq = 0;
        return v;
    }
    if (addr >= 0x4000) {
	if (addr == GAYLE_IRQ_1200) {
	    if (currprefs.cs_ide == 1)
		return gayle_irq;
	    return 0;
	} else if (addr == GAYLE_INT_1200) {
	    if (currprefs.cs_ide == 1)
		return gayle_intena;
	    return 0;
	}
	return 0;
    }
    ide_reg = get_ide_reg(addr);
    /* Emulated "ide hack". Prevents long KS boot delay if no drives installed */
    if (idedrive[0].size == 0 && idedrive[2].size == 0)
	return 0xff;
    switch (ide_reg)
    {
	case IDE_DRVADDR:
	    v = 0;
	break;
	case IDE_DATA:
	break;
	case IDE_ERROR:
	    v = ide_error;
	break;
	case IDE_NSECTOR:
	    if (ide_devcon & 0x80)
		v = ide_nsector2;
	    else
		v = ide_nsector;
	break;
	case IDE_SECTOR:
	    if (ide_devcon & 0x80)
		v = ide_sector2;
	    else
		v = ide_sector;
	break;
	case IDE_LCYL:
	    if (ide_devcon & 0x80)
		v = ide_lcyl2;
	    else
		v = ide_lcyl;
	break;
	case IDE_HCYL:
	    if (ide_devcon & 0x80)
		v = ide_hcyl2;
	    else
		v = ide_hcyl;
	break;
	case IDE_SELECT:
	    v = ide_select;
	break;
        case IDE_DEVCON:
	    v = ide_devcon;
	break;
	case IDE_STATUS:
	    if (ide->size == 0) {
		v = 0;
		if (ide_error)
		    v |= IDE_STATUS_ERR;
	    } else {
		v = ide->status;
		v |= IDE_STATUS_DRDY;
	    }
	break;
    }
    if (IDE_LOG > 2 && ide_reg > 0)
	write_log("IDE%d register %d->%02.2X\n", ide->num, ide_reg, (uae_u32)v & 0xff);
    return v;
}

static void ide_write (uaecptr addr, uae_u32 val)
{
    int ide_reg;

    addr &= 0xffff;
    ide_devcon &= ~0x80;
    if (IDE_LOG > 1 && addr != 0x2000 && addr != 0x2001 && addr != 0x2020 && addr != 0x2021 && addr != GAYLE_IRQ_1200)
	write_log ("IDE_WRITE %08.8X=%02.2X PC=%X\n", addr, (uae_u32)val & 0xff, M68K_GETPC);
    if (currprefs.cs_ide <= 0)
	return;
    if (currprefs.cs_ide == 1) {
	if (addr == GAYLE_IRQ_1200) {
	    gayle_irq = 0;
	    return;
	}
	if (addr == GAYLE_INT_1200) {
	    gayle_intena = val;
	    return;
	}
    }
    if (addr >= 0x4000)
	return;
    ide_reg = get_ide_reg(addr);
    if (IDE_LOG > 2 && ide_reg > 0)
	write_log("IDE%d register %d=%02.2X\n", ide->num, ide_reg, (uae_u32)val & 0xff);
    switch (ide_reg)
    {
	case IDE_DRVADDR:
	break;
	case IDE_DEVCON:
	    ide_devcon = val;
	break;
	case IDE_DATA:
	break;
	case IDE_ERROR:
	    ide_feat2 = ide_feat;
	    ide_feat = val;
	break;
	case IDE_NSECTOR:
	    ide_nsector2 = ide_nsector;
	    ide_nsector = val;
	break;
	case IDE_SECTOR:
	    ide_sector2 = ide_sector;
	    ide_sector = val;
	break;
	case IDE_LCYL:
	    ide_lcyl2 = ide_lcyl;
	    ide_lcyl = val;
	break;
	case IDE_HCYL:
	    ide_hcyl2 = ide_hcyl;
	    ide_hcyl = val;
	break;
	case IDE_SELECT:
	    ide_select = val;
	    ide_drv = (val & 0x10) ? 1 : 0;
	break;
	case IDE_STATUS:
	    ide_do_command (val);
	break;
    }
}

static int gayle_read (uaecptr addr)
{
    uaecptr oaddr = addr;
    uae_u32 v = 0;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = ide_read(addr);
    if (GAYLE_LOG)
        write_log ("GAYLE_READ %08.8X=%02.2X PC=%08.8X\n", oaddr, (uae_u32)v & 0xff, M68K_GETPC);
    return v;
}
static void gayle_write (uaecptr addr, int val)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    if (GAYLE_LOG)
	write_log ("GAYLE_WRITE %08.8X=%02.2X PC=%08.8X\n", addr, (uae_u32)val & 0xff, M68K_GETPC);
    ide_write(addr, val);
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
    default_xlate, default_check, NULL, "Gayle (low)",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static uae_u32 REGPARAM2 gayle_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = gayle_wget (addr) << 16;
    v |= gayle_wget (addr + 2);
    return v;
}
static uae_u32 REGPARAM2 gayle_wget (uaecptr addr)
{
    int ide_reg;
    uae_u16 v;

#ifdef JIT
    special_mem |= S_READ;
#endif
    ide_reg = get_ide_reg(addr);
    if (ide_reg == IDE_DATA)
	return ide_get_data();
    v = gayle_bget (addr) << 8;
    v |= gayle_bget (addr + 1);
    return v;
}
static uae_u32 REGPARAM2 gayle_bget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return gayle_read (addr);
}

static void REGPARAM2 gayle_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle_wput (addr, value >> 16);
    gayle_wput (addr + 2, value & 0xffff);
}

static void REGPARAM2 gayle_wput (uaecptr addr, uae_u32 value)
{
    int ide_reg;

#ifdef JIT
    special_mem |= S_WRITE;
#endif
    ide_reg = get_ide_reg(addr);
    if (ide_reg == IDE_DATA) {
	ide_put_data(value);
	return;
    }
    gayle_bput (addr, value >> 8);
    gayle_bput (addr + 1, value & 0xff);
}

static void REGPARAM2 gayle_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle_write (addr, value);
}

static void gayle2_write(uaecptr addr, uae_u32 v)
{
    gayle_id_cnt = 0;
}

static uae_u32 gayle2_read(uaecptr addr)
{
    uae_u8 v = 0;
    addr &= 0xffff;
    if (addr == 0x1000) {
	/* Gayle ID */
	if ((gayle_id_cnt & 3) == 2)
	    v = 0x7f;
	else 
	    v = 0x80;
	gayle_id_cnt++;
    }
    return v;
}

static uae_u32 REGPARAM3 gayle2_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle2_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle2_bget (uaecptr) REGPARAM;
static void REGPARAM3 gayle2_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle2_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle2_bput (uaecptr, uae_u32) REGPARAM;

addrbank gayle2_bank = {
    gayle2_lget, gayle2_wget, gayle2_bget,
    gayle2_lput, gayle2_wput, gayle2_bput,
    default_xlate, default_check, NULL, "Gayle (high)",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static uae_u32 REGPARAM2 gayle2_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = gayle2_wget (addr) << 16;
    v |= gayle2_wget (addr + 2);
    return v;
}
static uae_u32 REGPARAM2 gayle2_wget (uaecptr addr)
{
    uae_u16 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = gayle2_bget (addr) << 8;
    v |= gayle2_bget (addr + 1);
    return v;
}
static uae_u32 REGPARAM2 gayle2_bget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return gayle2_read (addr);
}

static void REGPARAM2 gayle2_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle2_wput (addr, value >> 16);
    gayle2_wput (addr + 2, value & 0xffff);
}

static void REGPARAM2 gayle2_wput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle2_bput (addr, value >> 8);
    gayle2_bput (addr + 1, value & 0xff);
}

static void REGPARAM2 gayle2_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle2_write (addr, value);
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

static uae_u32 REGPARAM2 mbres_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = mbres_wget (addr) << 16;
    v |= mbres_wget (addr + 2);
    return v;
}
static uae_u32 REGPARAM2 mbres_wget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return mbres_read (addr, 2);
}
static uae_u32 REGPARAM2 mbres_bget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return mbres_read (addr, 1);
}

static void REGPARAM2 mbres_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    mbres_wput (addr, value >> 16);
    mbres_wput (addr + 2, value & 0xffff);
}

static void REGPARAM2 mbres_wput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    mbres_write (addr, value, 2);
}

static void REGPARAM2 mbres_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
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
    int i;

    for (i = 0; i < 4; i++) {
	struct ide_hdf *ide = &idedrive[i];
	if (ide->irq_delay > 0) {
	    ide->irq_delay--;
	    if (ide->irq_delay == 0)
		ide_interrupt_do(ide);
	}
    }

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

addrbank mbdmac_bank = {
    mbdmac_lget, mbdmac_wget, mbdmac_bget,
    mbdmac_lput, mbdmac_wput, mbdmac_bput,
    default_xlate, default_check, NULL, "A3000 DMAC",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static uae_u32 gayle_attr_read(uaecptr addr)
{
    uae_u8 v = 0;
    write_log("R: %x %x\n", addr, M68K_GETPC);
    addr &= 262144 - 1;
    switch (addr)
    {
	case 0:
	v = 0x91;
	break;
	case 2:
        v = 0x05;
	break;
	case 4:
	v = 0x23;
	break;
    }
    return v;
}
static void gayle_attr_write(uaecptr addr, uae_u32 v)
{
    write_log("W: %x=%x %x\n", addr, v, M68K_GETPC);
    addr &= 262144 - 1;
    if (addr == 0x40000) {
	if (v)
	    write_log("GAYLE: Reset active\n");
	else
	    write_log("GAYLE: Reset non-active\n");
    }
}

static uae_u32 REGPARAM3 gayle_attr_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_attr_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_attr_bget (uaecptr) REGPARAM;
static void REGPARAM3 gayle_attr_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_attr_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_attr_bput (uaecptr, uae_u32) REGPARAM;

addrbank gayle_attr_bank = {
    gayle_attr_lget, gayle_attr_wget, gayle_attr_bget,
    gayle_attr_lput, gayle_attr_wput, gayle_attr_bput,
    default_xlate, default_check, NULL, "Gayle PCMCIA attribute",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static uae_u32 REGPARAM2 gayle_attr_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = gayle_attr_wget (addr) << 16;
    v |= gayle_attr_wget (addr + 2);
    return v;
}
static uae_u32 REGPARAM2 gayle_attr_wget (uaecptr addr)
{
    uae_u16 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    v = gayle_attr_bget (addr) << 8;
    v |= gayle_attr_bget (addr + 1);
    return v;
}
static uae_u32 REGPARAM2 gayle_attr_bget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return gayle_attr_read (addr);
}

static void REGPARAM2 gayle_attr_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle_attr_wput (addr, value >> 16);
    gayle_attr_wput (addr + 2, value & 0xffff);
}

static void REGPARAM2 gayle_attr_wput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle_attr_bput (addr, value >> 8);
    gayle_attr_bput (addr + 1, value & 0xff);
}

static void REGPARAM2 gayle_attr_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    gayle_attr_write (addr, value);
}


static int rl (uae_u8 *p)
{
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}

void gayle_free_ide_units(void)
{
    int i;

    for (i = 0; i < 4; i++) {
	struct ide_hdf *ide = &idedrive[i];
	if (ide->hfd.handle_valid)
	    hdf_close(&ide->hfd);
	xfree(ide->path);
	memset(ide, 0, sizeof (struct ide_hdf));
    }
}

void getchs2 (struct hardfiledata *hfd, int *pcyl, int *phead, int *psectorspertrack)
{
    unsigned int total = (unsigned int)(hfd->size / 512);
    int i, head , cyl, spt;
    int sptt[] = { 63, 127, 255, -1 };

    if (total > 16515072) {
	 /* >8G, CHS=16383/16/63 */
	*pcyl = 16383;
	*phead = 16;
	*psectorspertrack = 63;
	return;
    }

    for (i = 0; sptt[i] >= 0; i++) {
	spt = sptt[i];
	for (head = 4; head <= 16;head++) {
	    cyl = total / (head * spt);
	    if (hfd->size <= 512 * 1024 * 1024) {
		if (cyl <= 1023)
		    break;
	    } else {
		if (cyl < 16383)
		    break;
		if (cyl < 32767 && head >= 5)
		    break;
		if (cyl <= 65535)
		    break;
	    }
	}
	if (head <= 16)
	    break;
    }
    *pcyl = cyl;
    *phead = head;
    *psectorspertrack = spt;
}

static void pl(uae_u8 *p, int off, uae_u32 v)
{
    p += off * 4;
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v >> 0;
}

static void rdb_crc(uae_u8 *p)
{
    uae_u32 sum;
    int i, blocksize;
    
    sum =0;
    blocksize = rl (p + 1 * 4);
    for (i = 0; i < blocksize; i++)
	sum += rl (p + i * 4);
    sum = -sum;
    pl (p, 2, sum);
}

static void create_virtual_rdb(struct hardfiledata *hfd, uae_u32 dostype, int bootpri, char *filesys)
{
    uae_u8 *rdb, *part, *denv;
    int cyl = hfd->heads * hfd->secspertrack;
    int cyls = 262144 / (cyl * 512);
    int size = cyl * cyls * 512;

    rdb = xcalloc (size, 1);
    hfd->virtual_rdb = rdb;
    hfd->virtual_size = size;
    part = rdb + 512;
    pl(rdb, 0, 0x5244534b);
    pl(rdb, 1, 64);
    pl(rdb, 2, 0); // chksum
    pl(rdb, 3, 0); // hostid
    pl(rdb, 4, 512); // blockbytes
    pl(rdb, 5, 0); // flags
    pl(rdb, 6, -1); // badblock
    pl(rdb, 7, 1); // part
    pl(rdb, 8, -1); // fs
    pl(rdb, 9, -1); // driveinit
    pl(rdb, 10, -1); // reserved
    pl(rdb, 11, -1); // reserved
    pl(rdb, 12, -1); // reserved
    pl(rdb, 13, -1); // reserved
    pl(rdb, 14, -1); // reserved
    pl(rdb, 15, -1); // reserved
    pl(rdb, 16, hfd->nrcyls);
    pl(rdb, 17, hfd->secspertrack);
    pl(rdb, 18, hfd->heads);
    pl(rdb, 19, 0); // interleave
    pl(rdb, 20, 0); // park
    pl(rdb, 21, -1); // res
    pl(rdb, 22, -1); // res
    pl(rdb, 23, -1); // res
    pl(rdb, 24, 0); // writeprecomp
    pl(rdb, 25, 0); // reducedwrite
    pl(rdb, 26, 0); // steprate
    pl(rdb, 27, -1); // res
    pl(rdb, 28, -1); // res
    pl(rdb, 29, -1); // res
    pl(rdb, 30, -1); // res
    pl(rdb, 31, -1); // res
    pl(rdb, 32, 0); // rdbblockslo
    pl(rdb, 33, cyl * cyls); // rdbblockshi
    pl(rdb, 34, cyls); // locyl
    pl(rdb, 35, hfd->nrcyls + cyls); // hicyl
    pl(rdb, 36, cyl); // cylblocks
    pl(rdb, 37, 0); // autopark
    pl(rdb, 38, 2); // highrdskblock
    pl(rdb, 39, -1); // res
    strcpy (rdb + 40 * 4, hfd->vendor_id);
    strcpy (rdb + 42 * 4, hfd->product_id);
    strcpy (rdb + 46 * 4, "UAE");
    rdb_crc(rdb);

    pl(part, 0, 0x50415254);
    pl(part, 1, 64);
    pl(part, 2, 0);
    pl(part, 3, 0);
    pl(part, 4, -1);
    pl(part, 5, 1); // bootable
    pl(part, 6, -1);
    pl(part, 7, -1);
    pl(part, 8, 0); // devflags
    part[9 * 4] = strlen(hfd->device_name);
    strcpy (part + 9 * 4 + 1, hfd->device_name);

    denv = part + 128;
    pl(denv, 0, 80);
    pl(denv, 1, 512 / 4);
    pl(denv, 2, 0); // secorg
    pl(denv, 3, hfd->heads);
    pl(denv, 4, hfd->blocksize / 512);
    pl(denv, 5, hfd->secspertrack);
    pl(denv, 6, hfd->reservedblocks);
    pl(denv, 7, 0); // prealloc
    pl(denv, 8, 0); // interleave
    pl(denv, 9, cyls); // lowcyl
    pl(denv, 10, hfd->nrcyls + cyls - 1);
    pl(denv, 11, 50);
    pl(denv, 12, 0);
    pl(denv, 13, 0x00ffffff);
    pl(denv, 14, 0x7ffffffe);
    pl(denv, 15, bootpri);
    pl(denv, 16, dostype);
    rdb_crc(part);

    hfd->size += size;
    hfd->size2 += size;

}

int gayle_add_ide_unit(int ch, char *path, int blocksize, int readonly,
		       char *devname, int sectors, int surfaces, int reserved,
		       int bootpri, char *filesys)
{
    struct ide_hdf *ide;

    if (ch >= 4)
	return -1;
    ide = &idedrive[ch];
    ide->hfd.readonly = readonly;
    ide->hfd.blocksize = blocksize;
    ide->size = 0;
    if (!hdf_open(&ide->hfd, path))
	return -1;
    ide->path = my_strdup(path);
    ide->hfd.heads = surfaces;
    ide->hfd.reservedblocks = reserved;
    ide->hfd.secspertrack = sectors;
    if (devname)
	strcpy (ide->hfd.device_name, devname);
    getchs2(&ide->hfd, &ide->cyls, &ide->heads, &ide->secspertrack);
    ide->cyls_def = ide->cyls;
    ide->secspertrack_def = ide->secspertrack;
    ide->heads_def = ide->heads;
    if (ide->hfd.heads && ide->hfd.secspertrack) {
	uae_u8 buf[512] = { 0 };
        hdf_read(&ide->hfd, buf, 0, 512);
	if (buf[0] != 0 && memcmp(buf, "RDSK", 4)) {
	    ide->hfd.nrcyls = (ide->hfd.size / blocksize) / (sectors * surfaces);
	    create_virtual_rdb(&ide->hfd, rl (buf), bootpri, filesys);
	    while (ide->hfd.nrcyls * surfaces * sectors > ide->cyls_def * ide->secspertrack_def * ide->heads_def) {
		ide->cyls_def++;
	    }
	}
    }
    ide->size = ide->hfd.size;
    write_log("IDE%d ('%s'), CHS=%d,%d,%d\n", ch, path, ide->cyls, ide->heads, ide->secspertrack);
    ide->status = 0;
    ide->data_offset = 0;
    ide->data_size = 0;
    return 1;
}

static void initide(void)
{
    int i;

    if (savestate_state == STATE_RESTORE)
	return;
    ide_error = 1;
    ide_sector = ide_nsector = 1;
    ide_select = 0;
    ide_lcyl = ide_hcyl = ide_devcon = ide_feat = 0;
    ide_drv = 0;
    ide_splitter = 0;
    if (idedrive[2].size) {
	ide_splitter = 1;
	write_log("IDE splitter enabled\n");
    }
    for (i = 0; i < 4; i++)
	idedrive[i].num = i;
    gayle_irq = gayle_intena = 0;
    if (currprefs.cs_ide == 2)
	gayle_intena = 0xff;

}
    
void gayle_reset (int hardreset)
{
    initide();
    if (hardreset) {
	ramsey_config = 0;
	gary_coldboot = 1;
	gary_timeout = 0;
	gary_toenb = 0;
    }
}

uae_u8 *restore_gayle(uae_u8 *src)
{
    changed_prefs.cs_ide = restore_u8();
    gayle_intena = restore_u8();
    gayle_irq = restore_u8();
    ide_error = 0;
    return src;
}

uae_u8 *save_gayle (int *len)
{
    uae_u8 *dstbak, *dst;

    if (currprefs.cs_ide <= 0)
	return NULL;
    dstbak = dst = malloc (1000);
    save_u8 (currprefs.cs_ide);
    save_u8 (gayle_intena);
    save_u8 (gayle_irq);
    *len = dst - dstbak;
    return dstbak;
}

uae_u8 *save_ide (int num, int *len)
{
    uae_u8 *dstbak, *dst;
    struct ide_hdf *ide = &idedrive[num];

    if (currprefs.cs_ide <= 0)
	return NULL;
    if (ide->size == 0)
	return NULL;
    dstbak = dst = malloc (1000);
    save_u32(num);
    save_u64(ide->size);
    save_string(ide->path);
    save_u32(ide->hfd.blocksize);
    save_u32(ide->hfd.readonly);
    save_u8(ide->multiple_mode);
    save_u32(ide->cyls);
    save_u32(ide->heads);
    save_u32(ide->secspertrack);
    save_u8(ide_select);
    save_u8(ide_nsector);
    save_u8(ide_nsector2);
    save_u8(ide_sector);
    save_u8(ide_sector2);
    save_u8(ide_lcyl);
    save_u8(ide_lcyl2);
    save_u8(ide_hcyl);
    save_u8(ide_hcyl2);
    save_u8(ide_feat);
    save_u8(ide_feat2);
    save_u8(ide_error);
    save_u8(ide_devcon);
    *len = dst - dstbak;
    return dstbak;
}

uae_u8 *restore_ide (uae_u8 *src)
{
    int num, readonly, blocksize;
    uae_u64 size;
    char *path;
    struct ide_hdf *ide;

    num = restore_u32();
    ide = &idedrive[num];
    size = restore_u64();
    path = restore_string();
    blocksize = restore_u32();
    readonly = restore_u32();
    ide->multiple_mode = restore_u8();
    ide->cyls = restore_u32();
    ide->heads = restore_u32();
    ide->secspertrack = restore_u32();
    ide_select = restore_u8();
    ide_nsector = restore_u8();
    ide_sector = restore_u8();
    ide_lcyl = restore_u8();
    ide_hcyl = restore_u8();
    ide_feat = restore_u8();
    ide_nsector2 = restore_u8();
    ide_sector2 = restore_u8();
    ide_lcyl2 = restore_u8();
    ide_hcyl2 = restore_u8();
    ide_feat2 = restore_u8();
    ide_error = restore_u8();
    ide_devcon = restore_u8();
    gayle_add_ide_unit (num, path, blocksize, readonly, 0, 0, 0, 0, 0, 0);
    xfree(path);
    return src;
}
