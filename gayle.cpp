/*
* UAE - The Un*x Amiga Emulator
*
* Gayle (and motherboard resources) memory bank
*
* (c) 2006 - 2011 Toni Wilen
*/

#define GAYLE_LOG 0
#define IDE_LOG 0
#define MBRES_LOG 0
#define PCMCIA_LOG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "filesys.h"
#include "gayle.h"
#include "savestate.h"
#include "uae.h"
#include "gui.h"
#include "a2091.h"
#include "ncr_scsi.h"

#define PCMCIA_SRAM 1
#define PCMCIA_IDE 2

/*
600000 to 9FFFFF	4 MB	Credit Card memory if CC present
A00000 to A1FFFF	128 KB	Credit Card Attributes
A20000 to A3FFFF	128 KB	Credit Card I/O
A40000 to A5FFFF	128 KB	Credit Card Bits
A60000 to A7FFFF	128 KB	PC I/O

D80000 to D8FFFF	64 KB SPARE chip select
D90000 to D9FFFF	64 KB ARCNET chip select
DA0000 to DA3FFF	16 KB IDE drive
DA4000 to DA4FFF	16 KB IDE reserved
DA8000 to DAFFFF	32 KB Credit Card and IDE configregisters
DB0000 to DBFFFF	64 KB Not used (reserved for external IDE)
* DC0000 to DCFFFF	64 KB Real Time Clock (RTC)
DD0000 to DDFFFF	64 KB A3000 DMA controller
DD0000 to DD1FFF        A4000 DMAC
DD2000 to DDFFFF        A4000 IDE
DE0000 to DEFFFF	64 KB Motherboard resources
*/

#define NCR_OFFSET 0x40

/* Gayle definitions from Linux drivers and preliminary Gayle datasheet */

/* PCMCIA stuff */

#define GAYLE_RAM               0x600000
#define GAYLE_RAMSIZE           0x400000
#define GAYLE_ATTRIBUTE         0xa00000
#define GAYLE_ATTRIBUTESIZE     0x020000
#define GAYLE_IO                0xa20000     /* 16bit and even 8bit registers */
#define GAYLE_IOSIZE            0x010000
#define GAYLE_IO_8BITODD        0xa30000     /* odd 8bit registers */

#define GAYLE_ADDRESS   0xda8000      /* gayle main registers base address */
#define GAYLE_RESET     0xa40000      /* write 0x00 to start reset,
read 1 byte to stop reset */
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
#define IDE_ERR_UNC 0x40
#define IDE_ERR_MC 0x20
#define IDE_ERR_IDNF 0x10
#define IDE_ERR_MCR 0x08
#define IDE_ERR_ABRT 0x04
#define IDE_ERR_NM 0x02

/*
*  These are at different offsets from the base
*/
#define GAYLE_IRQ_4000  0x3020    /* MSB = 1, Harddisk is source of interrupt */
#define GAYLE_CS_1200	0x8000
#define GAYLE_IRQ_1200  0x9000
#define GAYLE_INT_1200  0xA000
#define GAYLE_CFG_1200	0xB000

/* DA8000 */
#define GAYLE_CS_IDE	0x80	/* IDE int status */
#define GAYLE_CS_CCDET	0x40    /* credit card detect */
#define GAYLE_CS_BVD1	0x20    /* battery voltage detect 1 */
#define GAYLE_CS_SC	0x20    /* credit card status change */
#define GAYLE_CS_BVD2	0x10    /* battery voltage detect 2 */
#define GAYLE_CS_DA	0x10    /* digital audio */
#define GAYLE_CS_WR	0x08    /* write enable (1 == enabled) */
#define GAYLE_CS_BSY	0x04    /* credit card busy */
#define GAYLE_CS_IRQ	0x04    /* interrupt request */
#define GAYLE_CS_DAEN   0x02    /* enable digital audio */ 
#define GAYLE_CS_DIS    0x01    /* disable PCMCIA slot */ 

/* DA9000 */
#define GAYLE_IRQ_IDE	    0x80
#define GAYLE_IRQ_CCDET	    0x40    /* credit card detect */
#define GAYLE_IRQ_BVD1	    0x20    /* battery voltage detect 1 */
#define GAYLE_IRQ_SC	    0x20    /* credit card status change */
#define GAYLE_IRQ_BVD2	    0x10    /* battery voltage detect 2 */
#define GAYLE_IRQ_DA	    0x10    /* digital audio */
#define GAYLE_IRQ_WR	    0x08    /* write enable (1 == enabled) */
#define GAYLE_IRQ_BSY	    0x04    /* credit card busy */
#define GAYLE_IRQ_IRQ	    0x04    /* interrupt request */
#define GAYLE_IRQ_RESET	    0x02    /* reset machine after CCDET change */ 
#define GAYLE_IRQ_BERR      0x01    /* generate bus error after CCDET change */ 

/* DAA000 */
#define GAYLE_INT_IDE	    0x80    /* IDE interrupt enable */
#define GAYLE_INT_CCDET	    0x40    /* credit card detect change enable */
#define GAYLE_INT_BVD1	    0x20    /* battery voltage detect 1 change enable */
#define GAYLE_INT_SC	    0x20    /* credit card status change enable */
#define GAYLE_INT_BVD2	    0x10    /* battery voltage detect 2 change enable */
#define GAYLE_INT_DA	    0x10    /* digital audio change enable */
#define GAYLE_INT_WR	    0x08    /* write enable change enabled */
#define GAYLE_INT_BSY	    0x04    /* credit card busy */
#define GAYLE_INT_IRQ	    0x04    /* credit card interrupt request */
#define GAYLE_INT_BVD_LEV   0x02    /* BVD int level, 0=lev2,1=lev6 */ 
#define GAYLE_INT_BSY_LEV   0x01    /* BSY int level, 0=lev2,1=lev6 */ 

/* 0xDAB000 GAYLE_CONFIG */
#define GAYLE_CFG_0V            0x00
#define GAYLE_CFG_5V            0x01
#define GAYLE_CFG_12V           0x02
#define GAYLE_CFG_100NS         0x08
#define GAYLE_CFG_150NS         0x04
#define GAYLE_CFG_250NS         0x00
#define GAYLE_CFG_720NS         0x0c

#define IDE_GAYLE 0
#define IDE_ADIDE 1

#define MAX_IDE_MULTIPLE_SECTORS 64
#define SECBUF_SIZE (512 * (MAX_IDE_MULTIPLE_SECTORS * 2))

struct ide_registers
{
	uae_u8 ide_select, ide_nsector, ide_sector, ide_lcyl, ide_hcyl, ide_devcon, ide_error, ide_feat;
	uae_u8 ide_nsector2, ide_sector2, ide_lcyl2, ide_hcyl2, ide_feat2;
	uae_u8 ide_drv;
};

struct ide_hdf
{
	struct hd_hardfiledata hdhfd;
	struct ide_registers *regs;

	uae_u8 secbuf[SECBUF_SIZE];
	int data_offset;
	int data_size;
	int data_multi;
	int lba48;
	uae_u8 multiple_mode;
	uae_u8 status;
	int irq_delay;
	int irq;
	int num;
	int type;
	int blocksize;
	int maxtransferstate;
};

#define TOTAL_IDE 3
#define GAYLE_IDE_ID 0
#define PCMCIA_IDE_ID 2

static struct ide_hdf *idedrive[TOTAL_IDE * 2];
static struct ide_registers ideregs[TOTAL_IDE];
struct hd_hardfiledata *pcmcia_sram;

static int pcmcia_card;
static int pcmcia_readonly;
static int pcmcia_type;
static uae_u8 pcmcia_configuration[20];
static bool pcmcia_configured;

static int gayle_id_cnt;
static uae_u8 gayle_irq, gayle_int, gayle_cs, gayle_cs_mask, gayle_cfg;
static int ide2, ide_splitter;

static struct ide_hdf *ide;

STATIC_INLINE void pw (int offset, uae_u16 w)
{
	ide->secbuf[offset * 2 + 0] = (uae_u8)w;
	ide->secbuf[offset * 2 + 1] = w >> 8;
}
static void ps (int offset, const TCHAR *src, int max)
{
	int i, len;
	char *s;

	offset *= 2;
	s = ua (src);
	len = strlen (s);
	for (i = 0; i < max; i++) {
		char c = ' ';
		if (i < len)
			c = s[i];
		ide->secbuf[offset ^ 1] = c;
		offset++;
	}
	xfree (s);
}

static void pcmcia_reset (void)
{
	memset (pcmcia_configuration, 0, sizeof pcmcia_configuration);
	pcmcia_configured = false;
	if (PCMCIA_LOG > 0)
		write_log (_T("PCMCIA reset\n"));
}

static uae_u8 checkpcmciaideirq (void)
{
	if (!idedrive || pcmcia_type != PCMCIA_IDE || !pcmcia_configured)
		return 0;
	if (ideregs[PCMCIA_IDE_ID].ide_devcon & 2)
		return 0;
	if (idedrive[PCMCIA_IDE_ID * 2]->irq)
		return GAYLE_IRQ_BSY;
	return 0;
}

static uae_u8 checkgayleideirq (void)
{
	int i;
	if (!idedrive)
		return 0;
	for (i = 0; i < 2; i++) {
		if (ideregs[i].ide_devcon & 2)
			continue;
		if (idedrive[i]->irq || idedrive[i + 2]->irq) {
			/* IDE killer feature. Do not eat interrupt to make booting faster. */
			if (idedrive[i]->irq && idedrive[i]->hdhfd.size == 0)
				idedrive[i]->irq = 0;
			return GAYLE_IRQ_IDE;
		}
	}
	return 0;
}

void rethink_gayle (void)
{
	int lev2 = 0;
	int lev6 = 0;
	uae_u8 mask;

	if (currprefs.cs_ide == IDE_A4000) {
		gayle_irq |= checkgayleideirq ();
		if ((gayle_irq & GAYLE_IRQ_IDE) && !(intreq & 0x0008))
			INTREQ_0 (0x8000 | 0x0008);
		return;
	}

	if (currprefs.cs_ide != IDE_A600A1200 && !currprefs.cs_pcmcia)
		return;
	gayle_irq |= checkgayleideirq ();
	gayle_irq |= checkpcmciaideirq ();
	mask = gayle_int & gayle_irq;
	if (mask & (GAYLE_IRQ_IDE | GAYLE_IRQ_WR))
		lev2 = 1;
	if (mask & GAYLE_IRQ_CCDET)
		lev6 = 1;
	if (mask & (GAYLE_IRQ_BVD1 | GAYLE_IRQ_BVD2)) {
		if (gayle_int & GAYLE_INT_BVD_LEV)
			lev6 = 1;
		else
			lev2 = 1;
	}
	if (mask & GAYLE_IRQ_BSY) {
		if (gayle_int & GAYLE_INT_BSY_LEV)
			lev6 = 1;
		else
			lev2 = 1;
	}
	if (lev2 && !(intreq & 0x0008))
		INTREQ_0 (0x8000 | 0x0008);
	if (lev6 && !(intreq & 0x2000))
		INTREQ_0 (0x8000 | 0x2000);
}

static void gayle_cs_change (uae_u8 mask, int onoff)
{
	int changed = 0;
	if ((gayle_cs & mask) && !onoff) {
		gayle_cs &= ~mask;
		changed = 1;
	} else if (!(gayle_cs & mask) && onoff) {
		gayle_cs |= mask;
		changed = 1;
	}
	if (changed) {
		gayle_irq |= mask;
		rethink_gayle ();
		if ((mask & GAYLE_CS_CCDET) && (gayle_irq & (GAYLE_IRQ_RESET | GAYLE_IRQ_BERR)) != (GAYLE_IRQ_RESET | GAYLE_IRQ_BERR)) {
			if (gayle_irq & GAYLE_IRQ_RESET)
				uae_reset (0);
			if (gayle_irq & GAYLE_IRQ_BERR)
				Exception (2);
		}
	}
}

static void card_trigger (int insert)
{
	if (insert) {
		if (pcmcia_card) {
			gayle_cs_change (GAYLE_CS_CCDET, 1);
			gayle_cfg = GAYLE_CFG_100NS;
			if (!pcmcia_readonly)
				gayle_cs_change (GAYLE_CS_WR, 1);
		}
	} else {
		gayle_cfg = 0;
		gayle_cs_change (GAYLE_CS_CCDET, 0);
		gayle_cs_change (GAYLE_CS_BVD2, 0);
		gayle_cs_change (GAYLE_CS_BVD1, 0);
		gayle_cs_change (GAYLE_CS_WR, 0);
		gayle_cs_change (GAYLE_CS_BSY, 0);
	}
	rethink_gayle ();
}

static void write_gayle_cfg (uae_u8 val)
{
	gayle_cfg = val;
}
static uae_u8 read_gayle_cfg (void)
{
	return gayle_cfg & 0x0f;
}
static void write_gayle_irq (uae_u8 val)
{
	gayle_irq = (gayle_irq & val) | (val & (GAYLE_IRQ_RESET | GAYLE_IRQ_BERR));
	if ((gayle_irq & (GAYLE_IRQ_RESET | GAYLE_IRQ_BERR)) == (GAYLE_IRQ_RESET | GAYLE_IRQ_BERR))
		pcmcia_reset ();
}
static uae_u8 read_gayle_irq (void)
{
	return gayle_irq;
}
static void write_gayle_int (uae_u8 val)
{
	gayle_int = val;
}
static uae_u8 read_gayle_int (void)
{
	return gayle_int;
}
static void write_gayle_cs (uae_u8 val)
{
	int ov = gayle_cs;

	gayle_cs_mask = val & ~3;
	gayle_cs &= ~3;
	gayle_cs |= val & 3;
	if ((ov & 1) != (gayle_cs & 1)) {
		gayle_map_pcmcia ();
		/* PCMCIA disable -> enable */
		card_trigger (!(gayle_cs & GAYLE_CS_DIS) ? 1 : 0);
		if (PCMCIA_LOG)
			write_log (_T("PCMCIA slot: %s PC=%08X\n"), !(gayle_cs & 1) ? _T("enabled") : _T("disabled"), M68K_GETPC);
	}
}
static uae_u8 read_gayle_cs (void)
{
	uae_u8 v;

	v = gayle_cs_mask | gayle_cs;
	v |= checkgayleideirq ();
	v |= checkpcmciaideirq ();
	return v;
}


static void ide_interrupt (void)
{
	if (ide->regs->ide_devcon & 2)
		return;
	//ide->status |= IDE_STATUS_BSY;
	ide->irq_delay = 2;
}

static void ide_interrupt_do (struct ide_hdf *ide)
{
	ide->status &= ~IDE_STATUS_BSY;
	ide->irq_delay = 0;
	ide->irq = 1;
	rethink_gayle ();
}

static void ide_fail_err (uae_u8 err)
{
	ide->regs->ide_error |= err;
	if (ide->regs->ide_drv == 1 && idedrive[ide2 + 1]->hdhfd.size == 0)
		idedrive[ide2]->status |= IDE_STATUS_ERR;
	ide->status |= IDE_STATUS_ERR;
	ide_interrupt ();
}
static void ide_fail (void)
{
	ide_fail_err (IDE_ERR_ABRT);
}

static void ide_data_ready (void)
{
	memset (ide->secbuf, 0, ide->blocksize);
	ide->data_offset = 0;
	ide->status |= IDE_STATUS_DRQ;
	ide->data_size = ide->blocksize;
	ide->data_multi = 1;
	ide_interrupt ();
}

static void ide_recalibrate (void)
{
	write_log (_T("IDE%d recalibrate\n"), ide->num);
	ide->regs->ide_sector = 0;
	ide->regs->ide_lcyl = ide->regs->ide_hcyl = 0;
	ide_interrupt ();
}
static void ide_identify_drive (void)
{
	uae_u64 totalsecs;
	int v;
	uae_u8 *buf = ide->secbuf;
	TCHAR tmp[100];

	if (ide->hdhfd.size == 0) {
		ide_fail ();
		return;
	}
	memset (buf, 0, ide->blocksize);
	if (IDE_LOG > 0)
		write_log (_T("IDE%d identify drive\n"), ide->num);
	ide_data_ready ();
	ide->data_size *= -1;
	pw (0, 1 << 6);
	pw (1, ide->hdhfd.cyls_def);
	pw (2, 0xc837);
	pw (3, ide->hdhfd.heads_def);
	pw (4, ide->blocksize * ide->hdhfd.secspertrack_def);
	pw (5, ide->blocksize);
	pw (6, ide->hdhfd.secspertrack_def);
	ps (10, _T("68000"), 20); /* serial */
	pw (20, 3);
	pw (21, ide->blocksize);
	pw (22, 4);
	ps (23, _T("0.4"), 8); /* firmware revision */
	_stprintf (tmp, _T("UAE-IDE %s"), ide->hdhfd.hfd.product_id);
	ps (27, tmp, 40); /* model */
	pw (47, MAX_IDE_MULTIPLE_SECTORS >> (ide->blocksize / 512 - 1)); /* max sectors in multiple mode */
	pw (48, 1);
	pw (49, (1 << 9) | (1 << 8)); /* LBA and DMA supported */
	pw (51, 0x200); /* PIO cycles */
	pw (52, 0x200); /* DMA cycles */
	pw (53, 1 | 2 | 4);
	pw (54, ide->hdhfd.cyls);
	pw (55, ide->hdhfd.heads);
	pw (56, ide->hdhfd.secspertrack);
	totalsecs = ide->hdhfd.cyls * ide->hdhfd.heads * ide->hdhfd.secspertrack;
	pw (57, (uae_u16)totalsecs);
	pw (58, (uae_u16)(totalsecs >> 16));
	v = idedrive[ide->regs->ide_drv]->multiple_mode;
	pw (59, (v > 0 ? 0x100 : 0) | v);
	totalsecs = ide->hdhfd.size / ide->blocksize;
	if (totalsecs > 0x0fffffff)
		totalsecs = 0x0fffffff;
	pw (60, (uae_u16)totalsecs);
	pw (61, (uae_u16)(totalsecs >> 16));
	pw (62, 0x0f);
	pw (63, 0x0f);
	pw (64, 0x03); /* PIO3 and PIO4 */
	pw (65, 120); /* MDMA2 supported */
	pw (66, 120);
	pw (67, 120);
	pw (68, 120);
	pw (80, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6)); /* ATA-1 to ATA-6 */
	pw (81, 0x1c); /* ATA revision */
	pw (82, (1 << 14)); /* NOP command supported */
	pw (83, (1 << 14) | (1 << 13) | (1 << 12) | (ide->lba48 ? (1 << 10) : 0)); /* cache flushes, LBA 48 supported */
	pw (84, 1 << 14);
	pw (85, 1 << 14);
	pw (86, (1 << 14) | (1 << 13) | (1 << 12) | (ide->lba48 ? (1 << 10) : 0)); /* cache flushes, LBA 48 enabled */
	pw (87, 1 << 14);
	pw (88, (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0)); /* UDMA modes */
	pw (93, (1 << 14) | (1 << 13) | (1 << 0));
	if (ide->lba48) {
		totalsecs = ide->hdhfd.size / ide->blocksize;
		pw (100, (uae_u16)(totalsecs >> 0));
		pw (101, (uae_u16)(totalsecs >> 16));
		pw (102, (uae_u16)(totalsecs >> 32));
		pw (103, (uae_u16)(totalsecs >> 48));
	}
}

static void ide_execute_drive_diagnostics (bool irq)
{
	ide->regs->ide_error = 1;
	ide->regs->ide_sector = ide->regs->ide_nsector = 1;
	ide->regs->ide_select = 0;
	ide->regs->ide_lcyl = ide->regs->ide_hcyl = 0;
	if (irq)
		ide_interrupt ();
	else
		ide->status = ~IDE_STATUS_BSY;
}

static void ide_initialize_drive_parameters (void)
{
	if (ide->hdhfd.size) {
		ide->hdhfd.secspertrack = ide->regs->ide_nsector == 0 ? 256 : ide->regs->ide_nsector;
		ide->hdhfd.heads = (ide->regs->ide_select & 15) + 1;
		ide->hdhfd.cyls = (ide->hdhfd.size / ide->blocksize) / (ide->hdhfd.secspertrack * ide->hdhfd.heads);
		if (ide->hdhfd.heads * ide->hdhfd.cyls * ide->hdhfd.secspertrack > 16515072 || ide->lba48) {
			ide->hdhfd.cyls = ide->hdhfd.cyls_def;
			ide->hdhfd.heads = ide->hdhfd.heads_def;
			ide->hdhfd.secspertrack = ide->hdhfd.secspertrack_def;
		}
	} else {
		ide->regs->ide_error |= IDE_ERR_ABRT;
		ide->status |= IDE_STATUS_ERR;
	}
	write_log (_T("IDE%d initialize drive parameters, CYL=%d,SPT=%d,HEAD=%d\n"),
		ide->num, ide->hdhfd.cyls, ide->hdhfd.secspertrack, ide->hdhfd.heads);
	ide_interrupt ();
}
static void ide_set_multiple_mode (void)
{
	write_log (_T("IDE%d drive multiple mode = %d\n"), ide->num, ide->regs->ide_nsector);
	ide->multiple_mode = ide->regs->ide_nsector;
	ide_interrupt ();
}
static void ide_set_features (void)
{
	int type = ide->regs->ide_nsector >> 3;
	int mode = ide->regs->ide_nsector & 7;

	write_log (_T("IDE%d set features %02X (%02X)\n"), ide->num, ide->regs->ide_feat, ide->regs->ide_nsector);
	ide_fail ();
}

static void get_lbachs (struct ide_hdf *ide, uae_u64 *lbap, unsigned int *cyl, unsigned int *head, unsigned int *sec, int lba48)
{
	if (lba48 && (ide->regs->ide_select & 0x40)) {
		uae_u64 lba;
		lba = (ide->regs->ide_hcyl << 16) | (ide->regs->ide_lcyl << 8) | ide->regs->ide_sector;
		lba |= ((ide->regs->ide_hcyl2 << 16) | (ide->regs->ide_lcyl2 << 8) | ide->regs->ide_sector2) << 24;
		*lbap = lba;
	} else {
		if (ide->regs->ide_select & 0x40) {
			*lbap = ((ide->regs->ide_select & 15) << 24) | (ide->regs->ide_hcyl << 16) | (ide->regs->ide_lcyl << 8) | ide->regs->ide_sector;
		} else {
			*cyl = (ide->regs->ide_hcyl << 8) | ide->regs->ide_lcyl;
			*head = ide->regs->ide_select & 15;
			*sec = ide->regs->ide_sector;
			*lbap = (((*cyl) * ide->hdhfd.heads + (*head)) * ide->hdhfd.secspertrack) + (*sec) - 1;
		}
	}
}

static int get_nsec (int lba48)
{
	if (lba48)
		return (ide->regs->ide_nsector == 0 && ide->regs->ide_nsector2 == 0) ? 65536 : (ide->regs->ide_nsector2 * 256 + ide->regs->ide_nsector);
	else
		return ide->regs->ide_nsector == 0 ? 256 : ide->regs->ide_nsector;
}
static int dec_nsec (int lba48, int v)
{
	if (lba48) {
		uae_u16 nsec;
		nsec = ide->regs->ide_nsector2 * 256 + ide->regs->ide_nsector;
		ide->regs->ide_nsector -= v;
		ide->regs->ide_nsector2 = nsec >> 8;
		ide->regs->ide_nsector = nsec & 0xff;
		return (ide->regs->ide_nsector2 << 8) | ide->regs->ide_nsector;
	} else {
		ide->regs->ide_nsector -= v;
		return ide->regs->ide_nsector;
	}
}

static void put_lbachs (struct ide_hdf *ide, uae_u64 lba, unsigned int cyl, unsigned int head, unsigned int sec, unsigned int inc, int lba48)
{
	if (lba48) {
		lba += inc;
		ide->regs->ide_hcyl = (lba >> 16) & 0xff;
		ide->regs->ide_lcyl = (lba >> 8) & 0xff;
		ide->regs->ide_sector = lba & 0xff;
		lba >>= 24;
		ide->regs->ide_hcyl2 = (lba >> 16) & 0xff;
		ide->regs->ide_lcyl2 = (lba >> 8) & 0xff;
		ide->regs->ide_sector2 = lba & 0xff;
	} else {
		if (ide->regs->ide_select & 0x40) {
			lba += inc;
			ide->regs->ide_select &= ~15;
			ide->regs->ide_select |= (lba >> 24) & 15;
			ide->regs->ide_hcyl = (lba >> 16) & 0xff;
			ide->regs->ide_lcyl = (lba >> 8) & 0xff;
			ide->regs->ide_sector = lba & 0xff;
		} else {
			sec += inc;
			while (sec >= ide->hdhfd.secspertrack) {
				sec -= ide->hdhfd.secspertrack;
				head++;
				if (head >= ide->hdhfd.heads) {
					head -= ide->hdhfd.heads;
					cyl++;
				}
			}
			ide->regs->ide_select &= ~15;
			ide->regs->ide_select |= head;
			ide->regs->ide_sector = sec;
			ide->regs->ide_hcyl = cyl >> 8;
			ide->regs->ide_lcyl = (uae_u8)cyl;
		}
	}
}

static void check_maxtransfer (int state)
{
	if (state == 1) {
		// transfer was started
		if (ide->maxtransferstate < 2 && ide->regs->ide_nsector == 0) {
			ide->maxtransferstate = 1;
		} else if (ide->maxtransferstate == 2) {
			// second transfer was started (part of split)
			write_log (_T("IDE maxtransfer check detected split >256 block transfer\n"));
			ide->maxtransferstate = 0;
		} else {
			ide->maxtransferstate = 0;
		}
	} else if (state == 2) {
		// address was read
		if (ide->maxtransferstate == 1)
			ide->maxtransferstate++;
		else
			ide->maxtransferstate = 0;
	}
}

static void ide_read_sectors (int flags)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;
	int multi = flags & 1;
	int lba48 = flags & 2;

	if (multi && ide->multiple_mode == 0) {
		ide_fail ();
		return;
	}
	check_maxtransfer(1);
	gui_flicker_led (LED_HD, ide->num, 1);
	nsec = get_nsec (lba48);
	get_lbachs (ide, &lba, &cyl, &head, &sec, lba48);
	if (IDE_LOG > 0)
		write_log (_T("IDE%d read off=%d, sec=%d (%d) lba%d\n"), ide->num, (uae_u32)lba, nsec, ide->multiple_mode, lba48 ? 48 : 28);
	if (lba * ide->blocksize >= ide->hdhfd.size) {
		ide_data_ready ();
		ide_fail_err (IDE_ERR_IDNF);
		return;
	}
	ide->data_multi = multi ? ide->multiple_mode : 1;
	ide->data_offset = 0;
	ide->status |= IDE_STATUS_DRQ;
	ide->data_size = nsec * ide->blocksize;
	ide_interrupt ();
}

static void ide_write_sectors (int flags)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;
	int multi = flags & 1;
	int lba48 = flags & 2;

	if (multi && ide->multiple_mode == 0) {
		ide_fail ();
		return;
	}
	check_maxtransfer(1);
	gui_flicker_led (LED_HD, ide->num, 2);
	nsec = get_nsec (lba48);
	get_lbachs (ide, &lba, &cyl, &head, &sec, lba48);
	if (lba * ide->blocksize >= ide->hdhfd.size) {
		ide_data_ready ();
		ide_fail_err (IDE_ERR_IDNF);
		return;
	}
	if (IDE_LOG > 0)
		write_log (_T("IDE%d write off=%d, sec=%d (%d) lba%d\n"), ide->num, (uae_u32)lba, nsec, ide->multiple_mode, lba48 ? 48 : 28);
	if (nsec * ide->blocksize > ide->hdhfd.size - lba * ide->blocksize)
		nsec = (ide->hdhfd.size - lba * ide->blocksize) / ide->blocksize;
	if (nsec <= 0) {
		ide_data_ready ();
		ide_fail_err (IDE_ERR_IDNF);
		return;
	}
	ide->data_multi = multi ? ide->multiple_mode : 1;
	ide->data_offset = 0;
	ide->status |= IDE_STATUS_DRQ;
	ide->data_size = nsec * ide->blocksize;
}

static void ide_do_command (uae_u8 cmd)
{
	int lba48 = ide->lba48;

	if (IDE_LOG > 1)
		write_log (_T("**** IDE%d command %02X\n"), ide->num, cmd);
	ide->status &= ~ (IDE_STATUS_DRDY | IDE_STATUS_DRQ | IDE_STATUS_ERR);
	ide->regs->ide_error = 0;

	if (cmd == 0x10) { /* recalibrate */
		ide_recalibrate ();
	} else if (cmd == 0xec) { /* identify drive */
		ide_identify_drive ();
	} else if (cmd == 0x90) { /* execute drive diagnostics */
		ide_execute_drive_diagnostics (true);
	} else if (cmd == 0x91) { /* initialize drive parameters */
		ide_initialize_drive_parameters ();
	} else if (cmd == 0xc6) { /* set multiple mode */
		ide_set_multiple_mode ();
	} else if (cmd == 0x20 || cmd == 0x21) { /* read sectors */
		ide_read_sectors (0);
	} else if (cmd == 0x24 && lba48) { /* read sectors ext */
		ide_read_sectors (2);
	} else if (cmd == 0xc4) { /* read multiple */
		ide_read_sectors (1);
	} else if (cmd == 0x29 && lba48) { /* read multiple ext */
		ide_read_sectors (1|2);
	} else if (cmd == 0x30 || cmd == 0x31) { /* write sectors */
		ide_write_sectors (0);
	} else if (cmd == 0x34 && lba48) { /* write sectors ext */
		ide_write_sectors (2);
	} else if (cmd == 0xc5) { /* write multiple */
		ide_write_sectors (1);
	} else if (cmd == 0x39 && lba48) { /* write multiple ext */
		ide_write_sectors (1|2);
	} else if (cmd == 0x50) { /* format track (nop) */
		ide_interrupt ();
	} else if (cmd == 0xa1) { /* ATAPI identify (IDE HD is not ATAPI) */
		ide_fail ();
	} else if (cmd == 0xef) { /* set features  */
		ide_set_features ();
	} else if (cmd == 0x00) { /* nop */
		ide_fail ();
	} else if (cmd == 0xe0 || cmd == 0xe1 || cmd == 0xe7 || cmd == 0xea) { /* standby now/idle/flush cache/flush cache ext */
		ide_interrupt ();
	} else if (cmd == 0xe5) { /* check power mode */
		ide->regs->ide_nsector = 0xff;
		ide_interrupt ();
	} else {
		ide_fail ();
		write_log (_T("IDE%d: unknown command %x\n"), ide->num, cmd);
	}
}

static uae_u16 ide_get_data (void)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;
	bool irq = false;
	bool last = false;
	uae_u16 v;

	if (IDE_LOG > 4)
		write_log (_T("IDE%d DATA read\n"), ide->num);
	if (ide->data_size == 0) {
		if (IDE_LOG > 0)
			write_log (_T("IDE%d DATA read without DRQ!?\n"), ide->num);
		if (ide->hdhfd.size == 0)
			return 0xffff;
		return 0;
	}
	nsec = 0;
	if (ide->data_offset == 0 && ide->data_size >= 0) {
		get_lbachs (ide, &lba, &cyl, &head, &sec, ide->lba48);
		nsec = get_nsec (ide->lba48);
		if (nsec * ide->blocksize > ide->hdhfd.size - lba * ide->blocksize)
			nsec = (ide->hdhfd.size - lba * ide->blocksize) / ide->blocksize;
		if (nsec <= 0) {
			ide_data_ready ();
			ide_fail_err (IDE_ERR_IDNF);
			return 0;
		}
		if (nsec > ide->data_multi)
			nsec = ide->data_multi;
		hdf_read (&ide->hdhfd.hfd, ide->secbuf, lba * ide->blocksize, nsec * ide->blocksize);
		if (!dec_nsec (ide->lba48, nsec))
			last = true;
		if (IDE_LOG > 1)
			write_log (_T("IDE%d read, read %d bytes to buffer\n"), ide->num, nsec * ide->blocksize);
	}

	v = ide->secbuf[ide->data_offset + 1] | (ide->secbuf[ide->data_offset + 0] << 8);
	ide->data_offset += 2;
	if (ide->data_size < 0) {
		ide->data_size += 2;
	} else {
		ide->data_size -= 2;
		if (((ide->data_offset % ide->blocksize) == 0) && ((ide->data_offset / ide->blocksize) % ide->data_multi) == 0) {
			irq = true;
			ide->data_offset = 0;
		}
	}
	if (ide->data_size == 0) {
		ide->status &= ~IDE_STATUS_DRQ;
		if (IDE_LOG > 1)
			write_log (_T("IDE%d read finished\n"), ide->num);
	}
	if (nsec) {
		put_lbachs (ide, lba, cyl, head, sec, last ? nsec - 1 : nsec, ide->lba48);
	}
	if (irq) {
		ide_interrupt ();
	}
	return v;
}

static void ide_write_drive (bool last)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;

	nsec = ide->data_offset / ide->blocksize;
	if (!nsec)
		return;
	get_lbachs (ide, &lba, &cyl, &head, &sec, ide->lba48);
	hdf_write (&ide->hdhfd.hfd, ide->secbuf, lba * ide->blocksize, ide->data_offset);
	put_lbachs (ide, lba, cyl, head, sec, last ? nsec - 1 : nsec, ide->lba48);
	dec_nsec (ide->lba48, nsec);
	if (IDE_LOG > 1)
		write_log (_T("IDE%d write interrupt, %d bytes written\n"), ide->num, ide->data_offset);
	ide->data_offset = 0;
}

static void ide_put_data (uae_u16 v)
{
	int irq = 0;

	if (IDE_LOG > 4)
		write_log (_T("IDE%d DATA write %04x %d/%d\n"), ide->num, v, ide->data_offset, ide->data_size);
	if (ide->data_size == 0) {
		if (IDE_LOG > 0)
			write_log (_T("IDE%d DATA write without DRQ!?\n"), ide->num);
		return;
	}
	ide->secbuf[ide->data_offset + 1] = v & 0xff;
	ide->secbuf[ide->data_offset + 0] = v >> 8;
	ide->data_offset += 2;
	ide->data_size -= 2;
	if (ide->data_size == 0) {
		irq = 1;
		ide_write_drive (true);
		ide->status &= ~IDE_STATUS_DRQ;
		if (IDE_LOG > 1)
			write_log (_T("IDE%d write finished\n"), ide->num);
	} else if (((ide->data_offset % ide->blocksize) == 0) && ((ide->data_offset / ide->blocksize) % ide->data_multi) == 0) {
		irq = 1;
		ide_write_drive (false);
	}
	if (irq)
		ide_interrupt ();
}

static int get_gayle_ide_reg (uaecptr addr)
{
	uaecptr a = addr;
	addr &= 0xffff;
	if (addr >= 0x3020 && addr <= 0x3021 && currprefs.cs_ide == IDE_A4000)
		return -1;
	addr &= ~0x2020;
	addr >>= 2;
	ide2 = 0;
	if (addr & 0x400) {
		if (ide_splitter) {
			ide2 = 2;
			addr &= ~0x400;
		}
	}
	ide = idedrive[ide2];
	if (ide->regs->ide_drv)
		ide = idedrive[ide2 + 1];
	return addr;
}

static uae_u32 ide_read_reg (int ide_reg)
{
	uae_u8 v = 0;

	switch (ide_reg)
	{
	case IDE_DRVADDR:
		v = ((ide->regs->ide_drv ? 2 : 1) | ((ide->regs->ide_select & 15) << 2)) ^ 0xff;
		break;
	case IDE_DATA:
		break;
	case IDE_ERROR:
		v = ide->regs->ide_error;
		break;
	case IDE_NSECTOR:
		if (ide->regs->ide_devcon & 0x80)
			v = ide->regs->ide_nsector2;
		else
			v = ide->regs->ide_nsector;
		break;
	case IDE_SECTOR:
		if (ide->regs->ide_devcon & 0x80)
			v = ide->regs->ide_sector2;
		else
			v = ide->regs->ide_sector;
		check_maxtransfer (2);
		break;
	case IDE_LCYL:
		if (ide->regs->ide_devcon & 0x80)
			v = ide->regs->ide_lcyl2;
		else
			v = ide->regs->ide_lcyl;
		break;
	case IDE_HCYL:
		if (ide->regs->ide_devcon & 0x80)
			v = ide->regs->ide_hcyl2;
		else
			v = ide->regs->ide_hcyl;
		break;
	case IDE_SELECT:
		v = ide->regs->ide_select;
		break;
	case IDE_STATUS:
		ide->irq = 0; /* fall through */
	case IDE_DEVCON: /* ALTSTATUS when reading */
		if (ide->hdhfd.size == 0) {
			v = 0;
			if (ide->regs->ide_error)
				v |= IDE_STATUS_ERR;
		} else {
			v = ide->status;
			v |= IDE_STATUS_DRDY | IDE_STATUS_DSC;
		}
		break;
	}
	if (IDE_LOG > 2 && ide_reg > 0)
		write_log (_T("IDE%d register %d->%02X\n"), ide->num, ide_reg, (uae_u32)v & 0xff);
	return v;
}

static void ide_write_reg (int ide_reg, uae_u32 val)
{
	ide->regs->ide_devcon &= ~0x80; /* clear HOB */
	if (IDE_LOG > 2 && ide_reg > 0)
		write_log (_T("IDE%d register %d=%02X\n"), ide->num, ide_reg, (uae_u32)val & 0xff);
	switch (ide_reg)
	{
	case IDE_DRVADDR:
		break;
	case IDE_DEVCON:
		if ((ide->regs->ide_devcon & 4) == 0 && (val & 4) != 0)
			ide_execute_drive_diagnostics (false);
		ide->regs->ide_devcon = val;
		break;
	case IDE_DATA:
		break;
	case IDE_ERROR:
		ide->regs->ide_feat2 = ide->regs->ide_feat;
		ide->regs->ide_feat = val;
		break;
	case IDE_NSECTOR:
		ide->regs->ide_nsector2 = ide->regs->ide_nsector;
		ide->regs->ide_nsector = val;
		break;
	case IDE_SECTOR:
		ide->regs->ide_sector2 = ide->regs->ide_sector;
		ide->regs->ide_sector = val;
		break;
	case IDE_LCYL:
		ide->regs->ide_lcyl2 = ide->regs->ide_lcyl;
		ide->regs->ide_lcyl = val;
		break;
	case IDE_HCYL:
		ide->regs->ide_hcyl2 = ide->regs->ide_hcyl;
		ide->regs->ide_hcyl = val;
		break;
	case IDE_SELECT:
		ide->regs->ide_select = val;
		ide->regs->ide_drv = (val & 0x10) ? 1 : 0;
		break;
	case IDE_STATUS:
		ide->irq = 0;
		ide_do_command (val);
		break;
	}
}

static uae_u32 gayle_read2 (uaecptr addr)
{
	int ide_reg;
	uae_u8 v = 0;

	addr &= 0xffff;
	if ((IDE_LOG > 2 && (addr != 0x2000 && addr != 0x2001 && addr != 0x2020 && addr != 0x2021 && addr != GAYLE_IRQ_1200)) || IDE_LOG > 4)
		write_log (_T("IDE_READ %08X PC=%X\n"), addr, M68K_GETPC);
	if (currprefs.cs_ide <= 0) {
		if (addr == 0x201c) // AR1200 IDE detection hack
			return 0x7f;
		return 0xff;
	}
	if (addr >= GAYLE_IRQ_4000 && addr <= GAYLE_IRQ_4000 + 1 && currprefs.cs_ide == IDE_A4000) {
		uae_u8 v = gayle_irq;
		gayle_irq = 0;
		return v;
	}
	if (addr >= 0x4000) {
		if (addr == GAYLE_IRQ_1200) {
			if (currprefs.cs_ide == IDE_A600A1200)
				return read_gayle_irq ();
			return 0;
		} else if (addr == GAYLE_INT_1200) {
			if (currprefs.cs_ide == IDE_A600A1200)
				return read_gayle_int ();
			return 0;
		}
		return 0;
	}
	ide_reg = get_gayle_ide_reg (addr);
	/* Emulated "ide killer". Prevents long KS boot delay if no drives installed */
	if (idedrive[0]->hdhfd.size == 0 && idedrive[2]->hdhfd.size == 0) {
		if (ide_reg == IDE_STATUS)
			return 0x7f;
		return 0xff;
	}
	return ide_read_reg (ide_reg);
}

static void gayle_write2 (uaecptr addr, uae_u32 val)
{
	int ide_reg;

	if ((IDE_LOG > 2 && (addr != 0x2000 && addr != 0x2001 && addr != 0x2020 && addr != 0x2021 && addr != GAYLE_IRQ_1200)) || IDE_LOG > 4)
		write_log (_T("IDE_WRITE %08X=%02X PC=%X\n"), addr, (uae_u32)val & 0xff, M68K_GETPC);
	if (currprefs.cs_ide <= 0)
		return;
	if (currprefs.cs_ide == IDE_A600A1200) {
		if (addr == GAYLE_IRQ_1200) {
			write_gayle_irq (val);
			return;
		}
		if (addr == GAYLE_INT_1200) {
			write_gayle_int (val);
			return;
		}
	}
	if (addr >= 0x4000)
		return;
	ide_reg = get_gayle_ide_reg (addr);
	ide_write_reg (ide_reg, val);
}

static int gayle_read (uaecptr addr)
{
	uaecptr oaddr = addr;
	uae_u32 v = 0;
	int got = 0;
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.cs_ide == IDE_A600A1200) {
		if ((addr & 0xA0000) != 0xA0000)
			return 0;
	}
	addr &= 0xffff;
	if (currprefs.cs_pcmcia) {
		if (currprefs.cs_ide != IDE_A600A1200) {
			if (addr == GAYLE_IRQ_1200) {
				v = read_gayle_irq ();
				got = 1;
			} else if (addr == GAYLE_INT_1200) {
				v = read_gayle_int ();
				got = 1;
			}
		}
		if (addr == GAYLE_CS_1200) {
			v = read_gayle_cs ();
			got = 1;
			if (PCMCIA_LOG)
				write_log (_T("PCMCIA STATUS READ %08X=%02X PC=%08X\n"), oaddr, (uae_u32)v & 0xff, M68K_GETPC);
		} else if (addr == GAYLE_CFG_1200) {
			v = read_gayle_cfg ();
			got = 1;
			if (PCMCIA_LOG)
				write_log (_T("PCMCIA CONFIG READ %08X=%02X PC=%08X\n"), oaddr, (uae_u32)v & 0xff, M68K_GETPC);
		}
	}
	if (!got)
		v = gayle_read2 (addr);
	if (GAYLE_LOG)
		write_log (_T("GAYLE_READ %08X=%02X PC=%08X\n"), oaddr, (uae_u32)v & 0xff, M68K_GETPC);
	return v;
}

static void gayle_write (uaecptr addr, int val)
{
	uaecptr oaddr = addr;
	int got = 0;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.cs_ide == IDE_A600A1200) {
		if ((addr & 0xA0000) != 0xA0000)
			return;
	}
	addr &= 0xffff;
	if (currprefs.cs_pcmcia) {
		if (currprefs.cs_ide != IDE_A600A1200) {
			if (addr == GAYLE_IRQ_1200) {
				write_gayle_irq (val);
				got = 1;
			} else if (addr == GAYLE_INT_1200) {
				write_gayle_int (val);
				got = 1;
			}
		}
		if (addr == GAYLE_CS_1200) {
			write_gayle_cs (val);
			got = 1;
			if (PCMCIA_LOG > 1)
				write_log (_T("PCMCIA STATUS WRITE %08X=%02X PC=%08X\n"), oaddr, (uae_u32)val & 0xff, M68K_GETPC);
		} else if (addr == GAYLE_CFG_1200) {
			write_gayle_cfg (val);
			got = 1;
			if (PCMCIA_LOG > 1)
				write_log (_T("PCMCIA CONFIG WRITE %08X=%02X PC=%08X\n"), oaddr, (uae_u32)val & 0xff, M68K_GETPC);
		}
	}

	if (GAYLE_LOG)
		write_log (_T("GAYLE_WRITE %08X=%02X PC=%08X\n"), oaddr, (uae_u32)val & 0xff, M68K_GETPC);
	if (!got)
		gayle_write2 (addr, val);
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
	default_xlate, default_check, NULL, _T("Gayle (low)"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static int isa4000t (uaecptr addr)
{
	if (currprefs.cs_mbdmac != 2)
		return 0;
	if ((addr & 0xffff) >= (GAYLE_BASE_4000 & 0xffff))
		return 0;
	return 1;
}

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
	if (isa4000t (addr)) {
		addr -= NCR_OFFSET;
		return (ncr_bget2 (addr) << 8) | ncr_bget2 (addr + 1);
	}
	ide_reg = get_gayle_ide_reg (addr);
	if (ide_reg == IDE_DATA)
		return ide_get_data ();
	v = gayle_bget (addr) << 8;
	v |= gayle_bget (addr + 1);
	return v;
}
static uae_u32 REGPARAM2 gayle_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (isa4000t (addr)) {
		addr -= NCR_OFFSET;
		return ncr_bget2 (addr);
	}
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
	if (isa4000t (addr)) {
		addr -= NCR_OFFSET;
		ncr_bput2 (addr, value >> 8);
		ncr_bput2 (addr + 1, value);
		return;
	}
	ide_reg = get_gayle_ide_reg (addr);
	if (ide_reg == IDE_DATA) {
		ide_put_data (value);
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
	if (isa4000t (addr)) {
		addr -= NCR_OFFSET;
		ncr_bput2 (addr, value);
		return;
	}
	gayle_write (addr, value);
}

static void gayle2_write (uaecptr addr, uae_u32 v)
{
	gayle_id_cnt = 0;
}

static uae_u32 gayle2_read (uaecptr addr)
{
	uae_u8 v = 0;
	addr &= 0xffff;
	if (addr == 0x1000) {
		/* Gayle ID. Gayle = 0xd0. AA Gayle = 0xd1 */
		if (gayle_id_cnt == 0 || gayle_id_cnt == 1 || gayle_id_cnt == 3 || ((currprefs.chipset_mask & CSMASK_AGA) && gayle_id_cnt == 7) ||
			(currprefs.cs_cd32cd && !currprefs.cs_ide && !currprefs.cs_pcmcia && gayle_id_cnt == 2))
			v = 0x80;
		else
			v = 0x00;
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
	default_xlate, default_check, NULL, _T("Gayle (high)"),
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

	if (MBRES_LOG > 0)
		write_log (_T("MBRES_WRITE %08X=%08X (%d) PC=%08X S=%d\n"), addr, val, size, M68K_GETPC, regs.s);
	if (1 || regs.s) { /* CPU FC = supervisor only */
		uae_u32 addr2 = addr & 3;
		uae_u32 addr64 = (addr >> 6) & 3;
		if (addr == 0x1002)
			garyidoffset = -1;
		if (addr64 == 0 && addr2 == 0x03)
			ramsey_config = val;
		if (addr2 == 0x02)
			gary_coldboot = (val & 0x80) ? 1 : 0;
		if (addr2 == 0x01)
			gary_toenb = (val & 0x80) ? 1 : 0;
		if (addr2 == 0x00)
			gary_timeout = (val & 0x80) ? 1 : 0;
	}
}

static uae_u32 mbres_read (uaecptr addr, int size)
{
	uae_u32 v = 0;
	addr &= 0xffff;

	if (1 || regs.s) { /* CPU FC = supervisor only (only newest ramsey/gary? never implemented?) */
		uae_u32 addr2 = addr & 3;
		uae_u32 addr64 = (addr >> 6) & 3;
		/* Gary ID (I don't think this exists in real chips..) */
		if (addr == 0x1002 && currprefs.cs_fatgaryrev >= 0) {
			garyidoffset++;
			garyidoffset &= 7;
			v = (currprefs.cs_fatgaryrev << garyidoffset) & 0x80;
		}
		for (;;) {
			if (addr64 == 1 && addr2 == 0x03) { /* RAMSEY revision */
				if (currprefs.cs_ramseyrev >= 0)
					v = currprefs.cs_ramseyrev;
				break;
			}
			if (addr64 == 0 && addr2 == 0x03) { /* RAMSEY config */
				if (currprefs.cs_ramseyrev >= 0)
					v = ramsey_config;
				break;
			}
			if (addr2 == 0x03) {
				v = 0xff;
				break;
			}
			if (addr2 == 0x02) { /* coldreboot flag */
				if (currprefs.cs_fatgaryrev >= 0)
					v = gary_coldboot ? 0x80 : 0x00;
			}
			if (addr2 == 0x01) { /* toenb flag */
				if (currprefs.cs_fatgaryrev >= 0)
					v = gary_toenb ? 0x80 : 0x00;
			}
			if (addr2 == 0x00) { /* timeout flag */
				if (currprefs.cs_fatgaryrev >= 0)
					v = gary_timeout ? 0x80 : 0x00;
			}
			v |= 0x7f;
			break;
		}
	} else {
		v = 0xff;
	}
	if (MBRES_LOG > 0)
		write_log (_T("MBRES_READ %08X=%08X (%d) PC=%08X S=%d\n"), addr, v, size, M68K_GETPC, regs.s);
	return v;
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
	default_xlate, default_check, NULL, _T("Motherboard Resources"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

void gayle_hsync (void)
{
	int i;

	for (i = 0; i < TOTAL_IDE * 2; i++) {
		struct ide_hdf *ide = idedrive[i];
		if (ide->irq_delay > 0) {
			ide->irq_delay--;
			if (ide->irq_delay == 0)
				ide_interrupt_do (ide);
		}
	}
}

static void alloc_ide_mem (struct ide_hdf **ide, int max)
{
	int i;

	for (i = 0; i < max; i++) {
		if (!ide[i])
			ide[i] = xcalloc (struct ide_hdf, 1);
	}
}

static struct ide_hdf *add_ide_unit (int ch, const TCHAR *path, int blocksize, int readonly,
	const TCHAR *devname, int sectors, int surfaces, int reserved,
	int bootpri, TCHAR *filesys)
{
	struct ide_hdf *ide;

	alloc_ide_mem (idedrive, TOTAL_IDE * 2);
	ide = idedrive[ch];
	if (!hdf_hd_open (&ide->hdhfd, path, blocksize, readonly, devname, sectors, surfaces, reserved, bootpri, filesys))
		return NULL;
	ide->blocksize = blocksize;
	ide->lba48 = ide->hdhfd.size >= 128 * (uae_u64)0x40000000 ? 1 : 0;
	ide->status = 0;
	ide->data_offset = 0;
	ide->data_size = 0;
	return ide;
}

static int pcmcia_common_size, pcmcia_attrs_size;
static int pcmcia_common_mask;
static uae_u8 *pcmcia_common;
static uae_u8 *pcmcia_attrs;
static int pcmcia_write_min, pcmcia_write_max;

static int get_pcmcmia_ide_reg (uaecptr addr)
{
	int reg;

	addr &= 0x80000 - 1;
	if (addr < 0x20000)
		return -1; /* attribute */
	if (addr >= 0x40000)
		return -1;
	addr -= 0x20000;
	reg = addr & 15;
	ide = idedrive[PCMCIA_IDE_ID * 2];
	if (ide->regs->ide_drv)
		ide = idedrive[PCMCIA_IDE_ID * 2 + 1];
	if (reg < 8)
		return reg;
	if (reg == 8)
		reg = IDE_DATA;
	else if (reg == 9)
		reg = IDE_DATA;
	else if (reg == 13)
		reg = IDE_ERROR;
	else if (reg == 14)
		reg = IDE_DEVCON;
	else if (reg == 15)
		reg = IDE_DRVADDR;
	else
		reg = -1;
	return reg;
}

static uae_u32 gayle_attr_read (uaecptr addr)
{
	uae_u8 v = 0;

	if (PCMCIA_LOG > 1)
		write_log (_T("PCMCIA ATTR R: %x %x\n"), addr, M68K_GETPC);
	addr &= 0x80000 - 1;
	if (addr >= 0x40000) {
		if (PCMCIA_LOG > 0)
			write_log (_T("GAYLE: Reset disabled\n"));
		return v;
	}
	if (addr >= pcmcia_attrs_size)
		return v;
	if (pcmcia_type == PCMCIA_IDE) {
		if (addr >= 0x200 && addr < 0x200 + sizeof (pcmcia_configuration) * 2) {
			int offset = (addr - 0x200) / 2;
			return pcmcia_configuration[offset];
		}
		if (pcmcia_configured) {
			int reg = get_pcmcmia_ide_reg (addr);
			if (reg >= 0) {
				if (reg == 0) {
					static uae_u16 data;
					if (addr < 0x30000)
						data = ide_get_data ();
					else
						return data & 0xff;
					return (data >> 8) & 0xff;
				} else {
					return ide_read_reg (reg);
				}
			}
		}
	}
	v = pcmcia_attrs[addr / 2];
	return v;
}

static void gayle_attr_write (uaecptr addr, uae_u32 v)
{
	if (PCMCIA_LOG > 1)
		write_log (_T("PCMCIA ATTR W: %x=%x %x\n"), addr, v, M68K_GETPC);
	addr &= 0x80000 - 1;
	if (addr >= 0x40000) {
		if (PCMCIA_LOG > 0)
			write_log (_T("GAYLE: Reset enabled\n"));
		pcmcia_reset ();
	} else if (addr < pcmcia_attrs_size) {
		 if (pcmcia_type == PCMCIA_IDE) {
			 if (addr >= 0x200 && addr < 0x200 + sizeof (pcmcia_configuration) * 2) {
				int offset = (addr - 0x200) / 2;
				pcmcia_configuration[offset] = v;
				if (offset == 0) {
					if (v & 0x80) {
						pcmcia_reset ();
					} else {
						write_log (_T("PCMCIA IO configured = %02x\n"), v);
						if ((v & 0x3f) != 1)
							write_log (_T("WARNING: Only config index 1 is emulated!\n"));
						pcmcia_configured = true;
					}
				}
			}
			if (pcmcia_configured) {
				int reg = get_pcmcmia_ide_reg (addr);
				if (reg >= 0) {
					if (reg == 0) {
						static uae_u16 data;
						if (addr >= 0x30000) {
							data = (v & 0xff) << 8;
						} else {
							data |= v & 0xff;
							ide_put_data (data);
						}
						return;
					}
					ide_write_reg (reg, v);
				}
			 }
		 }
	}
}

static void initscideattr (int readonly)
{
	uae_u8 *rp;
	uae_u8 *p = pcmcia_attrs;
	struct hardfiledata *hfd = &pcmcia_sram->hfd;

	/* Mostly just copied from real CF cards.. */

	/* CISTPL_DEVICE */
	*p++ = 0x01;
	*p++ = 0x04;
	*p++ = 0xdf;
	*p++ = 0x4a;
	*p++ = 0x01;
	*p++ = 0xff;

	/* CISTPL_DEVICEOC */
	*p++ = 0x1c;
	*p++ = 0x04;
	*p++ = 0x02;
	*p++ = 0xd9;
	*p++ = 0x01;
	*p++ = 0xff;

	/* CISTPL_JEDEC */
	*p++ = 0x18;
	*p++ = 0x02;
	*p++ = 0xdf;
	*p++ = 0x01;

	/* CISTPL_VERS_1 */
	*p++= 0x15;
	rp = p++;
	*p++= 4; /* PCMCIA 2.1 */
	*p++= 1;
	strcpy ((char*)p, "UAE");
	p += strlen ((char*)p) + 1;
	strcpy ((char*)p, "68000");
	p += strlen ((char*)p) + 1;
	strcpy ((char*)p, "Generic Emulated PCMCIA IDE");
	p += strlen ((char*)p) + 1;
	*p++= 0xff;
	*rp = p - rp - 1;

	/* CISTPL_FUNCID */
	*p++ = 0x21;
	*p++ = 0x02;
	*p++ = 0x04;
	*p++ = 0x01;

	/* CISTPL_FUNCE */
	*p++ = 0x22;
	*p++ = 0x02;
	*p++ = 0x01;
	*p++ = 0x01;

	/* CISTPL_FUNCE */
	*p++ = 0x22;
	*p++ = 0x03;
	*p++ = 0x02;
	*p++ = 0x0c;
	*p++ = 0x0f;

	/* CISTPL_CONFIG */
	*p++ = 0x1a;
	*p++ = 0x05;
	*p++ = 0x01;
	*p++ = 0x01;
	*p++ = 0x00;
	*p++ = 0x02;
	*p++ = 0x0f;

	/* CISTPL_CFTABLEENTRY */
	*p++ = 0x1b;
	*p++ = 0x06;
	*p++ = 0xc0;
	*p++ = 0x01;
	*p++ = 0x21;
	*p++ = 0xb5;
	*p++ = 0x1e;
	*p++ = 0x4d;

	/* CISTPL_NO_LINK */
	*p++ = 0x14;
	*p++ = 0x00;

	/* CISTPL_END */
	*p++ = 0xff;
}

static void initsramattr (int size, int readonly)
{
	uae_u8 *rp;
	uae_u8 *p = pcmcia_attrs;
	int sm, su, code, units;
	struct hardfiledata *hfd = &pcmcia_sram->hfd;
	int real = hfd->flags & HFD_FLAGS_REALDRIVE;

	code = 0;
	su = 512;
	sm = 16384;
	while (size > sm) {
		sm *= 4;
		su *= 4;
		code++;
	}
	units = 31 - ((sm - size) / su);

	/* CISTPL_DEVICE */
	*p++ = 0x01;
	*p++ = 3;
	*p++ = (6 /* DTYPE_SRAM */ << 4) | (readonly ? 8 : 0) | (4 /* SPEED_100NS */);
	*p++ = (units << 3) | code; /* memory card size in weird units */
	*p++ = 0xff;

	/* CISTPL_DEVICEGEO */
	*p++ = 0x1e;
	*p++ = 7;
	*p++ = 2; /* 16-bit PCMCIA */
	*p++ = 0;
	*p++ = 1;
	*p++ = 1;
	*p++ = 1;
	*p++ = 1;
	*p++ = 0xff;

	/* CISTPL_VERS_1 */
	*p++= 0x15;
	rp = p++;
	*p++= 4; /* PCMCIA 2.1 */
	*p++= 1;
	if (real) {
		ua_copy ((char*)p, -1, hfd->product_id);
		p += strlen ((char*)p) + 1;
		ua_copy ((char*)p, -1, hfd->product_rev);
	} else {
		strcpy ((char*)p, "UAE");
		p += strlen ((char*)p) + 1;
		strcpy ((char*)p, "68000");
	}
	p += strlen ((char*)p) + 1;
	sprintf ((char*)p, "Generic Emulated %dKB PCMCIA SRAM Card", size >> 10);
	p += strlen ((char*)p) + 1;
	*p++= 0xff;
	*rp = p - rp - 1;

	/* CISTPL_FUNCID */
	*p++ = 0x21;
	*p++ = 2;
	*p++ = 1; /* Memory Card */
	*p++ = 0;

	/* CISTPL_MANFID */
	*p++ = 0x20;
	*p++ = 4;
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 1;
	*p++ = 1;

	/* CISTPL_END */
	*p++ = 0xff;
}

static void checkflush (int addr)
{
	if (pcmcia_card == 0 || pcmcia_sram == 0)
		return;
	if (addr >= 0 && pcmcia_common[0] == 0 && pcmcia_common[1] == 0 && pcmcia_common[2] == 0)
		return; // do not flush periodically if used as a ram expension
	if (addr < 0) {
		pcmcia_write_min = 0;
		pcmcia_write_max = pcmcia_common_size;
	}
	if (pcmcia_write_min >= 0) {
		if (abs (pcmcia_write_min - addr) >= 512 || abs (pcmcia_write_max - addr) >= 512) {
			int blocksize = pcmcia_sram->hfd.blocksize;
			int mask = ~(blocksize - 1);
			int start = pcmcia_write_min & mask;
			int end = (pcmcia_write_max + blocksize - 1) & mask;
			int len = end - start;
			if (len > 0) {
				hdf_write (&pcmcia_sram->hfd, pcmcia_common + start, start, len);
				pcmcia_write_min = -1;
				pcmcia_write_max = -1;
			}
		}
	}
	if (pcmcia_write_min < 0 || pcmcia_write_min > addr)
		pcmcia_write_min = addr;
	if (pcmcia_write_max < 0 || pcmcia_write_max < addr)
		pcmcia_write_max = addr;
}

static int freepcmcia (int reset)
{
	if (pcmcia_sram) {
		checkflush (-1);
		if (reset) {
			hdf_hd_close (pcmcia_sram);
			xfree (pcmcia_sram);
			pcmcia_sram = NULL;
		} else {
			pcmcia_sram->hfd.drive_empty = 1;
		}
	}
	if (pcmcia_card)
		gayle_cs_change (GAYLE_CS_CCDET, 0);
	
	pcmcia_reset ();
	pcmcia_card = 0;

	xfree (pcmcia_common);
	xfree (pcmcia_attrs);
	pcmcia_common = NULL;
	pcmcia_attrs = NULL;
	pcmcia_common_size = 0;
	pcmcia_attrs_size = 0;

	gayle_cfg = 0;
	gayle_cs = 0;
	return 1;
}

static int initpcmcia (const TCHAR *path, int readonly, int type, int reset)
{
	if (currprefs.cs_pcmcia == 0)
		return 0;
	freepcmcia (reset);
	if (!pcmcia_sram)
		pcmcia_sram = xcalloc (struct hd_hardfiledata, 1);
	if (!pcmcia_sram->hfd.handle_valid)
		reset = 1;

	if (type == PCMCIA_SRAM) {
		if (reset) {
			if (path)
				hdf_hd_open (pcmcia_sram, path, 512, readonly, NULL, 0, 0, 0, 0, NULL);
		} else {
			pcmcia_sram->hfd.drive_empty = 0;
		}

		if (pcmcia_sram->hfd.readonly)
			readonly = 1;
		pcmcia_common_size = 0;
		pcmcia_readonly = readonly;
		pcmcia_attrs_size = 256;
		pcmcia_attrs = xcalloc (uae_u8, pcmcia_attrs_size);
		pcmcia_type = type;

		if (!pcmcia_sram->hfd.drive_empty) {
			pcmcia_common_size = pcmcia_sram->hfd.virtsize;
			if (pcmcia_sram->hfd.virtsize > 4 * 1024 * 1024) {
				write_log (_T("PCMCIA SRAM: too large device, %d bytes\n"), pcmcia_sram->hfd.virtsize);
				pcmcia_common_size = 4 * 1024 * 1024;
			}
			pcmcia_common = xcalloc (uae_u8, pcmcia_common_size);
			write_log (_T("PCMCIA SRAM: '%s' open, size=%d\n"), path, pcmcia_common_size);
			hdf_read (&pcmcia_sram->hfd, pcmcia_common, 0, pcmcia_common_size);
			pcmcia_card = 1;
			initsramattr (pcmcia_common_size, readonly);
			if (!(gayle_cs & GAYLE_CS_DIS)) {
				gayle_map_pcmcia ();
				card_trigger (1);
			}
		}
	} else if (type == PCMCIA_IDE) {

		if (reset) {
			if (path)
				add_ide_unit (PCMCIA_IDE_ID * 2, path, 512, readonly, NULL, 0, 0, 0, 0, NULL);
		}

		pcmcia_common_size = 0;
		pcmcia_readonly = readonly;
		pcmcia_attrs_size = 0x40000;
		pcmcia_attrs = xcalloc (uae_u8, pcmcia_attrs_size);
		pcmcia_type = type;

		write_log (_T("PCMCIA IDE: '%s' open\n"), path);
		pcmcia_card = 1;
		initscideattr (readonly);
		if (!(gayle_cs & GAYLE_CS_DIS)) {
			gayle_map_pcmcia ();
			card_trigger (1);
		}
	}

	pcmcia_write_min = -1;
	pcmcia_write_max = -1;
	return 1;
}

static uae_u32 gayle_common_read (uaecptr addr)
{
	uae_u8 v = 0;
	if (PCMCIA_LOG > 2)
		write_log (_T("PCMCIA COMMON R: %x %x\n"), addr, M68K_GETPC);
	if (!pcmcia_common_size)
		return 0;
	addr -= PCMCIA_COMMON_START & (PCMCIA_COMMON_SIZE - 1);
	addr &= PCMCIA_COMMON_SIZE - 1;
	if (addr < pcmcia_common_size)
		v = pcmcia_common[addr];
	return v;
}

static void gayle_common_write (uaecptr addr, uae_u32 v)
{
	if (PCMCIA_LOG > 2)
		write_log (_T("PCMCIA COMMON W: %x=%x %x\n"), addr, v, M68K_GETPC);
	if (!pcmcia_common_size)
		return;
	if (pcmcia_readonly)
		return;
	addr -= PCMCIA_COMMON_START & (PCMCIA_COMMON_SIZE - 1);
	addr &= PCMCIA_COMMON_SIZE - 1;
	if (addr < pcmcia_common_size) {
		if (pcmcia_common[addr] != v) {
			checkflush (addr);
			pcmcia_common[addr] = v;
		}
	}
}

static uae_u32 REGPARAM3 gayle_common_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_common_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_common_bget (uaecptr) REGPARAM;
static void REGPARAM3 gayle_common_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_common_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_common_bput (uaecptr, uae_u32) REGPARAM;

static int REGPARAM2 gayle_common_check (uaecptr addr, uae_u32 size)
{
	if (!pcmcia_common_size)
		return 0;
	addr -= PCMCIA_COMMON_START & (PCMCIA_COMMON_SIZE - 1);
	addr &= PCMCIA_COMMON_SIZE - 1;
	return (addr + size) <= PCMCIA_COMMON_SIZE;
}

static uae_u8 *REGPARAM2 gayle_common_xlate (uaecptr addr)
{
	addr -= PCMCIA_COMMON_START & (PCMCIA_COMMON_SIZE - 1);
	addr &= PCMCIA_COMMON_SIZE - 1;
	return pcmcia_common + addr;
}

static addrbank gayle_common_bank = {
	gayle_common_lget, gayle_common_wget, gayle_common_bget,
	gayle_common_lput, gayle_common_wput, gayle_common_bput,
	gayle_common_xlate, gayle_common_check, NULL, _T("Gayle PCMCIA Common"),
	gayle_common_lget, gayle_common_wget, ABFLAG_RAM | ABFLAG_SAFE
};


static uae_u32 REGPARAM3 gayle_attr_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_attr_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gayle_attr_bget (uaecptr) REGPARAM;
static void REGPARAM3 gayle_attr_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_attr_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gayle_attr_bput (uaecptr, uae_u32) REGPARAM;

addrbank gayle_attr_bank = {
	gayle_attr_lget, gayle_attr_wget, gayle_attr_bget,
	gayle_attr_lput, gayle_attr_wput, gayle_attr_bput,
	default_xlate, default_check, NULL, _T("Gayle PCMCIA Attribute/Misc"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
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

	if (pcmcia_type == PCMCIA_IDE && pcmcia_configured) {
		int reg = get_pcmcmia_ide_reg (addr);
		if (reg == IDE_DATA) {
			// 16-bit register
			return ide_get_data ();
		}
	}

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

	if (pcmcia_type == PCMCIA_IDE && pcmcia_configured) {
		int reg = get_pcmcmia_ide_reg (addr);
		if (reg == IDE_DATA) {
			// 16-bit register
			ide_put_data (value);
			return;
		}
	}

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


static uae_u32 REGPARAM2 gayle_common_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = gayle_common_wget (addr) << 16;
	v |= gayle_common_wget (addr + 2);
	return v;
}
static uae_u32 REGPARAM2 gayle_common_wget (uaecptr addr)
{
	uae_u16 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = gayle_common_bget (addr) << 8;
	v |= gayle_common_bget (addr + 1);
	return v;
}
static uae_u32 REGPARAM2 gayle_common_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return gayle_common_read (addr);
}
static void REGPARAM2 gayle_common_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	gayle_common_wput (addr, value >> 16);
	gayle_common_wput (addr + 2, value & 0xffff);
}
static void REGPARAM2 gayle_common_wput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	gayle_common_bput (addr, value >> 8);
	gayle_common_bput (addr + 1, value & 0xff);
}
static void REGPARAM2 gayle_common_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	gayle_common_write (addr, value);
}

void gayle_map_pcmcia (void)
{
	if (currprefs.cs_pcmcia == 0)
		return;
	if (pcmcia_card == 0 || (gayle_cs & GAYLE_CS_DIS)) {
		map_banks (&dummy_bank, 0xa0, 8, 0);
		if (currprefs.chipmem_size <= 4 * 1024 * 1024 && getz2endaddr () <= 4 * 1024 * 1024)
			map_banks (&dummy_bank, PCMCIA_COMMON_START >> 16, PCMCIA_COMMON_SIZE >> 16, 0);
	} else {
		map_banks (&gayle_attr_bank, 0xa0, 8, 0);
		if (currprefs.chipmem_size <= 4 * 1024 * 1024 && getz2endaddr () <= 4 * 1024 * 1024)
			map_banks (&gayle_common_bank, PCMCIA_COMMON_START >> 16, PCMCIA_COMMON_SIZE >> 16, 0);
	}
}

static int rl (uae_u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}

void gayle_free_units (void)
{
	int i;

	for (i = 0; i < TOTAL_IDE * 2; i++) {
		struct ide_hdf *ide = idedrive[i];
		if (ide) {
			hdf_hd_close (&ide->hdhfd);
			memset (ide, 0, sizeof (struct ide_hdf));
		}
	}
	freepcmcia (1);
}

#if 0
#include "zfile.h"
static void dumphdf (struct hardfiledata *hfd)
{
	int i;
	uae_u8 buf[512];
	int off;
	struct zfile *zf;

	zf = zfile_fopen ("c:\\d\\tmp.dmp", "wb");
	off = 0;
	for (i = 0; i < 128; i++) {
		hdf_read (hfd, buf, off, 512);
		zfile_fwrite (buf, 1, 512, zf);
		off += 512;
	}
	zfile_fclose (zf);
}
#endif

int gayle_add_ide_unit (int ch, TCHAR *path, int blocksize, int readonly,
	TCHAR *devname, int sectors, int surfaces, int reserved,
	int bootpri, TCHAR *filesys)
{
	struct ide_hdf *ide;

	if (ch >= 2 * 2)
		return -1;
	ide = add_ide_unit (ch, path, blocksize, readonly, devname, sectors, surfaces, reserved, bootpri, filesys);
	if (ide == NULL)
		return 0;
	write_log (_T("GAYLE_IDE%d '%s', CHS=%d,%d,%d. %uM. LBA48=%d\n"),
		ch, path, ide->hdhfd.cyls, ide->hdhfd.heads, ide->hdhfd.secspertrack, (int)(ide->hdhfd.size / (1024 * 1024)), ide->lba48);
	ide->type = IDE_GAYLE;
	//dumphdf (&ide->hdhfd.hfd);
	return 1;
}

int gayle_add_pcmcia_sram_unit (const TCHAR *path, int readonly)
{
	return initpcmcia (path, readonly, PCMCIA_SRAM, 1);
}

int gayle_add_pcmcia_ide_unit (const TCHAR *path, int readonly)
{
	return initpcmcia (path, readonly, PCMCIA_IDE, 1);
}

int gayle_modify_pcmcia_sram_unit (const TCHAR *path, int readonly, int insert)
{
	if (insert)
		return initpcmcia (path, readonly, PCMCIA_SRAM, pcmcia_sram ? 0 : 1);
	else
		return freepcmcia (0);
}

int gayle_modify_pcmcia_ide_unit (const TCHAR *path, int readonly, int insert)
{
	if (insert)
		return initpcmcia (path, readonly, PCMCIA_IDE, pcmcia_sram ? 0 : 1);
	else
		return freepcmcia (0);
}

static void initide (void)
{
	int i;

	alloc_ide_mem (idedrive, TOTAL_IDE * 2);
	if (isrestore ())
		return;
	for (i = 0; i < TOTAL_IDE; i++) {
		ideregs[i].ide_error = 1;
		ideregs[i].ide_sector = ideregs[i].ide_nsector = 1;
		ideregs[i].ide_select = 0;
		ideregs[i].ide_lcyl = ideregs[i].ide_hcyl = ideregs[i].ide_devcon = ideregs[i].ide_feat = 0;
		idedrive[i * 2 + 0]->regs = &ideregs[i];
		idedrive[i * 2 + 1]->regs = &ideregs[i];
	}
	ide_splitter = 0;
	if (idedrive[2]->hdhfd.size) {
		ide_splitter = 1;
		write_log (_T("IDE splitter enabled\n"));
	}
	for (i = 0; i < TOTAL_IDE * 2; i++)
		idedrive[i]->num = i;
	gayle_irq = gayle_int = 0;
}

void gayle_reset (int hardreset)
{
	static TCHAR bankname[100];

	initide ();
	if (hardreset) {
		ramsey_config = 0;
		gary_coldboot = 1;
		gary_timeout = 0;
		gary_toenb = 0;
	}
	_tcscpy (bankname, _T("Gayle (low)"));
	if (currprefs.cs_ide == IDE_A4000)
		_tcscpy (bankname, _T("A4000 IDE"));
	if (currprefs.cs_mbdmac == 2) {
		_tcscat (bankname, _T(" + NCR53C710 SCSI"));
		ncr_reset ();
	}
	gayle_bank.name = bankname;
}

uae_u8 *restore_gayle (uae_u8 *src)
{
	changed_prefs.cs_ide = restore_u8 ();
	gayle_int = restore_u8 ();
	gayle_irq = restore_u8 ();
	gayle_cs = restore_u8 ();
	gayle_cs_mask = restore_u8 ();
	gayle_cfg = restore_u8 ();
	ideregs[0].ide_error = 0;
	ideregs[1].ide_error = 0;
	return src;
}

uae_u8 *save_gayle (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;

	if (currprefs.cs_ide <= 0)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u8 (currprefs.cs_ide);
	save_u8 (gayle_int);
	save_u8 (gayle_irq);
	save_u8 (gayle_cs);
	save_u8 (gayle_cs_mask);
	save_u8 (gayle_cfg);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *save_ide (int num, int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	struct ide_hdf *ide;

	if (num >= TOTAL_IDE * 2 || idedrive[num] == NULL)
		return NULL;
	if (currprefs.cs_ide <= 0)
		return NULL;
	ide = idedrive[num];
	if (ide->hdhfd.size == 0)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (num);
	save_u64 (ide->hdhfd.size);
	save_string (ide->hdhfd.path);
	save_u32 (ide->hdhfd.hfd.blocksize);
	save_u32 (ide->hdhfd.hfd.readonly);
	save_u8 (ide->multiple_mode);
	save_u32 (ide->hdhfd.cyls);
	save_u32 (ide->hdhfd.heads);
	save_u32 (ide->hdhfd.secspertrack);
	save_u8 (ide->regs->ide_select);
	save_u8 (ide->regs->ide_nsector);
	save_u8 (ide->regs->ide_nsector2);
	save_u8 (ide->regs->ide_sector);
	save_u8 (ide->regs->ide_sector2);
	save_u8 (ide->regs->ide_lcyl);
	save_u8 (ide->regs->ide_lcyl2);
	save_u8 (ide->regs->ide_hcyl);
	save_u8 (ide->regs->ide_hcyl2);
	save_u8 (ide->regs->ide_feat);
	save_u8 (ide->regs->ide_feat2);
	save_u8 (ide->regs->ide_error);
	save_u8 (ide->regs->ide_devcon);
	save_u64 (ide->hdhfd.hfd.virtual_size);
	save_u32 (ide->hdhfd.hfd.secspertrack);
	save_u32 (ide->hdhfd.hfd.heads);
	save_u32 (ide->hdhfd.hfd.reservedblocks);
	save_u32 (ide->hdhfd.bootpri);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_ide (uae_u8 *src)
{
	int num, readonly, blocksize;
	uae_u64 size;
	TCHAR *path;
	struct ide_hdf *ide;

	alloc_ide_mem (idedrive, TOTAL_IDE * 2);
	num = restore_u32 ();
	ide = idedrive[num];
	size = restore_u64 ();
	path = restore_string ();
	blocksize = restore_u32 ();
	readonly = restore_u32 ();
	ide->multiple_mode = restore_u8 ();
	ide->hdhfd.cyls = restore_u32 ();
	ide->hdhfd.heads = restore_u32 ();
	ide->hdhfd.secspertrack = restore_u32 ();
	ide->regs->ide_select = restore_u8 ();
	ide->regs->ide_nsector = restore_u8 ();
	ide->regs->ide_sector = restore_u8 ();
	ide->regs->ide_lcyl = restore_u8 ();
	ide->regs->ide_hcyl = restore_u8 ();
	ide->regs->ide_feat = restore_u8 ();
	ide->regs->ide_nsector2 = restore_u8 ();
	ide->regs->ide_sector2 = restore_u8 ();
	ide->regs->ide_lcyl2 = restore_u8 ();
	ide->regs->ide_hcyl2 = restore_u8 ();
	ide->regs->ide_feat2 = restore_u8 ();
	ide->regs->ide_error = restore_u8 ();
	ide->regs->ide_devcon = restore_u8 ();
	ide->hdhfd.hfd.virtual_size = restore_u64 ();
	ide->hdhfd.hfd.secspertrack = restore_u32 ();
	ide->hdhfd.hfd.heads = restore_u32 ();
	ide->hdhfd.hfd.reservedblocks = restore_u32 ();
	ide->hdhfd.bootpri = restore_u32 ();
	if (ide->hdhfd.hfd.virtual_size)
		gayle_add_ide_unit (num, path, blocksize, readonly, ide->hdhfd.hfd.device_name,
		ide->hdhfd.hfd.secspertrack, ide->hdhfd.hfd.heads, ide->hdhfd.hfd.reservedblocks, ide->hdhfd.bootpri, NULL);
	else
		gayle_add_ide_unit (num, path, blocksize, readonly, 0, 0, 0, 0, 0, 0);
	xfree (path);
	return src;
}
