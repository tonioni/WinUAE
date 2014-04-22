/*
* UAE - The Un*x Amiga Emulator
*
* Gayle (and motherboard resources) memory bank
*
* (c) 2006 - 2013 Toni Wilen
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
#include "blkdev.h"
#include "scsi.h"
#include "threaddep/thread.h"

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

/* A4000T NCR */
#define NCR_OFFSET 0x40
#define NCR_ALT_OFFSET 0x80
#define NCR_MASK 0x3f

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
#define IDE_NSECTOR	0x02	    /* sector count, nr of sectors to read/write */
#define IDE_SECTOR	0x03	    /* starting sector */
#define IDE_LCYL	0x04	    /* starting cylinder */
#define IDE_HCYL	0x05	    /* high byte of starting cyl */
#define IDE_SELECT	0x06	    /* 101dhhhh , d=drive, hhhh=head */
#define IDE_STATUS	0x07	    /* see status-bits */
#define IDE_DEVCON	0x0406
#define IDE_DRVADDR	0x0407
/* STATUS bits */
#define IDE_STATUS_ERR 0x01		// 0
#define IDE_STATUS_IDX 0x02		// 1
#define IDE_STATUS_DRQ 0x08		// 3
#define IDE_STATUS_DSC 0x10		// 4
#define IDE_STATUS_DRDY 0x40	// 6
#define IDE_STATUS_BSY 0x80		// 7
#define ATAPI_STATUS_CHK IDE_STATUS_ERR
/* ERROR bits */
#define IDE_ERR_UNC 0x40
#define IDE_ERR_MC 0x20
#define IDE_ERR_IDNF 0x10
#define IDE_ERR_MCR 0x08
#define IDE_ERR_ABRT 0x04
#define IDE_ERR_NM 0x02
#define ATAPI_ERR_EOM 0x02
#define ATAPI_ERR_ILI 0x01
/* ATAPI interrupt reason (Sector Count) */
#define ATAPI_IO 0x02
#define ATAPI_CD 0x01

/*
*  These are at different offsets from the base
*/
#define GAYLE_IRQ_4000  0x3020    /* WORD register MSB = 1, Harddisk is source of interrupt */
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


#define ATAPI_MAX_TRANSFER 32768
#define MAX_IDE_MULTIPLE_SECTORS 64
#define SECBUF_SIZE (131072 * 2)

struct ide_registers
{
	uae_u8 ide_select, ide_nsector, ide_sector, ide_lcyl, ide_hcyl, ide_devcon, ide_error, ide_feat;
	uae_u8 ide_nsector2, ide_sector2, ide_lcyl2, ide_hcyl2, ide_feat2;
	uae_u8 ide_status;
};

struct ide_hdf
{
	struct hd_hardfiledata hdhfd;
	struct ide_registers regs;
	struct ide_registers *regs0;
	struct ide_registers *regs1;
	struct ide_hdf *pair;

	uae_u8 secbuf[SECBUF_SIZE];
	int data_offset;
	int data_size;
	int data_multi;
	int direction; // 0 = read, 1 = write
	bool intdrq;
	bool lba48;
	bool lba48cmd;
	uae_u8 multiple_mode;
	int irq_delay;
	int irq;
	int num;
	int type;
	int blocksize;
	int maxtransferstate;
	int ide_drv;

	bool atapi;
	bool atapi_drdy;
	int cd_unit_num;
	int packet_state;
	int packet_data_size;
	int packet_data_offset;
	int packet_transfer_size;
	struct scsi_data *scsi;
};

#define TOTAL_IDE 3
#define GAYLE_IDE_ID 0
#define PCMCIA_IDE_ID 2

static struct ide_hdf *idedrive[TOTAL_IDE * 2];
static struct ide_registers ideregs[TOTAL_IDE * 2];
struct hd_hardfiledata *pcmcia_sram;

static int pcmcia_card;
static int pcmcia_readonly;
static int pcmcia_type;
static uae_u8 pcmcia_configuration[20];
static int pcmcia_configured;

static int gayle_id_cnt;
static uae_u8 gayle_irq, gayle_int, gayle_cs, gayle_cs_mask, gayle_cfg;
static int ide_splitter;

static smp_comm_pipe requests;
static volatile int gayle_thread_running;

STATIC_INLINE void pw (struct ide_hdf *ide, int offset, uae_u16 w)
{
	ide->secbuf[offset * 2 + 0] = (uae_u8)w;
	ide->secbuf[offset * 2 + 1] = w >> 8;
}
static void ps (struct ide_hdf *ide, int offset, const TCHAR *src, int max)
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
	pcmcia_configured = -1;
	if (PCMCIA_LOG > 0)
		write_log (_T("PCMCIA reset\n"));
}

static uae_u8 checkpcmciaideirq (void)
{
	if (!idedrive || pcmcia_type != PCMCIA_IDE || pcmcia_configured < 0)
		return 0;
	if (ideregs[PCMCIA_IDE_ID].ide_devcon & 2)
		return 0;
	if (idedrive[PCMCIA_IDE_ID * 2]->irq)
		return GAYLE_IRQ_BSY;
	return 0;
}

static bool isdrive (struct ide_hdf *ide)
{
	return ide && (ide->hdhfd.size != 0 || ide->atapi);
}

static uae_u8 checkgayleideirq (void)
{
	int i;
	bool irq = false;

	if (!idedrive)
		return 0;
	for (i = 0; i < 2; i++) {
		if (!(idedrive[i]->regs.ide_devcon & 2) && (idedrive[i]->irq || idedrive[i + 2]->irq))
			irq = true;
		/* IDE killer feature. Do not eat interrupt to make booting faster. */
		if (idedrive[i]->irq && !isdrive (idedrive[i]))
			idedrive[i]->irq = 0;
		if (idedrive[i + 2]->irq && !isdrive (idedrive[i + 2]))
			idedrive[i + 2]->irq = 0;
	}
	return irq ? GAYLE_IRQ_IDE : 0;
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
				uae_reset (0, 0);
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

static void ide_interrupt (struct ide_hdf *ide)
{
	ide->regs.ide_status |= IDE_STATUS_BSY;
	ide->regs.ide_status &= ~IDE_STATUS_DRQ;
	ide->irq_delay = 2;
}
static void ide_fast_interrupt (struct ide_hdf *ide)
{
	ide->regs.ide_status |= IDE_STATUS_BSY;
	ide->regs.ide_status &= ~IDE_STATUS_DRQ;
	ide->irq_delay = 1;
}

#if 0
uae_u16 isideint(void)
{
	if (!(gayle_irq & 0x80))
		return 0;
	gayle_irq &= ~0x80;
	return 0x8000;
}
#endif

static void ide_interrupt_do (struct ide_hdf *ide)
{
	uae_u8 os = ide->regs.ide_status;
	ide->regs.ide_status &= ~IDE_STATUS_DRQ;
	if (ide->intdrq)
		ide->regs.ide_status |= IDE_STATUS_DRQ;
	ide->regs.ide_status &= ~IDE_STATUS_BSY;
	if (IDE_LOG > 1)
		write_log (_T("IDE INT %02X -> %02X\n"), os, ide->regs.ide_status);
	ide->intdrq = false;
	ide->irq_delay = 0;
	if (ide->regs.ide_devcon & 2)
		return;
	ide->irq = 1;
	rethink_gayle ();
}

static void ide_fail_err (struct ide_hdf *ide, uae_u8 err)
{
	ide->regs.ide_error |= err;
	if (ide->ide_drv == 1 && !isdrive (ide->pair))
		idedrive[ide->ide_drv >= 2 ? 2 : 0]->regs.ide_status |= IDE_STATUS_ERR;
	ide->regs.ide_status |= IDE_STATUS_ERR;
	ide_interrupt (ide);
}
static void ide_fail (struct ide_hdf *ide)
{
	ide_fail_err (ide, IDE_ERR_ABRT);
}

static void ide_data_ready (struct ide_hdf *ide)
{
	memset (ide->secbuf, 0, ide->blocksize);
	ide->data_offset = 0;
	ide->data_size = ide->blocksize;
	ide->data_multi = 1;
	ide->intdrq = true;
	ide_interrupt (ide);
}

static void ide_recalibrate (struct ide_hdf *ide)
{
	write_log (_T("IDE%d recalibrate\n"), ide->num);
	ide->regs.ide_sector = 0;
	ide->regs.ide_lcyl = ide->regs.ide_hcyl = 0;
	ide_interrupt (ide);
}
static void ide_identify_drive (struct ide_hdf *ide)
{
	uae_u64 totalsecs;
	int v;
	uae_u8 *buf = ide->secbuf;
	TCHAR tmp[100];
	bool atapi = ide->atapi;

	if (!isdrive (ide)) {
		ide_fail (ide);
		return;
	}
	memset (buf, 0, ide->blocksize);
	if (IDE_LOG > 0)
		write_log (_T("IDE%d identify drive\n"), ide->num);
	ide_data_ready (ide);
	ide->direction = 0;
	pw (ide, 0, atapi ? 0x85c0 : 1 << 6);
	pw (ide, 1, ide->hdhfd.cyls_def);
	pw (ide, 2, 0xc837);
	pw (ide, 3, ide->hdhfd.heads_def);
	pw (ide, 4, ide->blocksize * ide->hdhfd.secspertrack_def);
	pw (ide, 5, ide->blocksize);
	pw (ide, 6, ide->hdhfd.secspertrack_def);
	ps (ide, 10, _T("68000"), 20); /* serial */
	pw (ide, 20, 3);
	pw (ide, 21, ide->blocksize);
	pw (ide, 22, 4);
	ps (ide, 23, _T("0.5"), 8); /* firmware revision */
	if (ide->atapi)
		_tcscpy (tmp, _T("UAE-ATAPI"));
	else
		_stprintf (tmp, _T("UAE-IDE %s"), ide->hdhfd.hfd.product_id);
	ps (ide, 27, tmp, 40); /* model */
	pw (ide, 47, MAX_IDE_MULTIPLE_SECTORS >> (ide->blocksize / 512 - 1)); /* max sectors in multiple mode */
	pw (ide, 48, 1);
	pw (ide, 49, (1 << 9) | (1 << 8)); /* LBA and DMA supported */
	pw (ide, 51, 0x200); /* PIO cycles */
	pw (ide, 52, 0x200); /* DMA cycles */
	pw (ide, 53, 1 | 2 | 4);
	pw (ide, 54, ide->hdhfd.cyls);
	pw (ide, 55, ide->hdhfd.heads);
	pw (ide, 56, ide->hdhfd.secspertrack);
	totalsecs = ide->hdhfd.cyls * ide->hdhfd.heads * ide->hdhfd.secspertrack;
	pw (ide, 57, (uae_u16)totalsecs);
	pw (ide, 58, (uae_u16)(totalsecs >> 16));
	v = idedrive[ide->ide_drv]->multiple_mode;
	pw (ide, 59, (v > 0 ? 0x100 : 0) | v);
	totalsecs = ide->blocksize ? ide->hdhfd.size / ide->blocksize : 0;
	if (totalsecs > 0x0fffffff)
		totalsecs = 0x0fffffff;
	pw (ide, 60, (uae_u16)totalsecs);
	pw (ide, 61, (uae_u16)(totalsecs >> 16));
	pw (ide, 62, 0x0f);
	pw (ide, 63, 0x0f);
	pw (ide, 64, 0x03); /* PIO3 and PIO4 */
	pw (ide, 65, 120); /* MDMA2 supported */
	pw (ide, 66, 120);
	pw (ide, 67, 120);
	pw (ide, 68, 120);
	pw (ide, 80, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6)); /* ATA-1 to ATA-6 */
	pw (ide, 81, 0x1c); /* ATA revision */
	pw (ide, 82, (1 << 14) | (atapi ? 0x10 | 4 : 0)); /* NOP, ATAPI: PACKET and Removable media features supported */
	pw (ide, 83, (1 << 14) | (1 << 13) | (1 << 12) | (ide->lba48 ? (1 << 10) : 0)); /* cache flushes, LBA 48 supported */
	pw (ide, 84, 1 << 14);
	pw (ide, 85, 1 << 14);
	pw (ide, 86, (1 << 14) | (1 << 13) | (1 << 12) | (ide->lba48 ? (1 << 10) : 0)); /* cache flushes, LBA 48 enabled */
	pw (ide, 87, 1 << 14);
	pw (ide, 88, (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0)); /* UDMA modes */
	pw (ide, 93, (1 << 14) | (1 << 13) | (1 << 0));
	if (ide->lba48) {
		totalsecs = ide->hdhfd.size / ide->blocksize;
		pw (ide, 100, (uae_u16)(totalsecs >> 0));
		pw (ide, 101, (uae_u16)(totalsecs >> 16));
		pw (ide, 102, (uae_u16)(totalsecs >> 32));
		pw (ide, 103, (uae_u16)(totalsecs >> 48));
	}
}

static void set_signature (struct ide_hdf *ide)
{
	if (ide->atapi) {
		ide->regs.ide_sector = 1;
		ide->regs.ide_nsector = 1;
		ide->regs.ide_lcyl = 0x14;
		ide->regs.ide_hcyl = 0xeb;
		ide->regs.ide_status = 0;
		ide->atapi_drdy = false;
	} else {
		ide->regs.ide_nsector = 1;
		ide->regs.ide_sector = 1;
		ide->regs.ide_lcyl = 0;
		ide->regs.ide_hcyl = 0;
		ide->regs.ide_status = 0;
	}
	ide->regs.ide_error = 0x01; // device ok
	ide->packet_state = 0;
}

static void reset_device (struct ide_hdf *ide, bool both)
{
	set_signature (ide);
	if (both)
		set_signature (ide->pair);
}

static void ide_execute_drive_diagnostics (struct ide_hdf *ide, bool irq)
{
	reset_device (ide, irq);
	if (irq)
		ide_interrupt (ide);
	else
		ide->regs.ide_status &= ~IDE_STATUS_BSY;
}

static void ide_initialize_drive_parameters (struct ide_hdf *ide)
{
	if (ide->hdhfd.size) {
		ide->hdhfd.secspertrack = ide->regs.ide_nsector == 0 ? 256 : ide->regs.ide_nsector;
		ide->hdhfd.heads = (ide->regs.ide_select & 15) + 1;
		if (ide->hdhfd.hfd.ci.pcyls)
			ide->hdhfd.cyls = ide->hdhfd.hfd.ci.pcyls;
		else
			ide->hdhfd.cyls = (ide->hdhfd.size / ide->blocksize) / (ide->hdhfd.secspertrack * ide->hdhfd.heads);
		if (ide->hdhfd.heads * ide->hdhfd.cyls * ide->hdhfd.secspertrack > 16515072 || ide->lba48) {
			if (ide->hdhfd.hfd.ci.pcyls)
				ide->hdhfd.cyls = ide->hdhfd.hfd.ci.pcyls;
			else
				ide->hdhfd.cyls = ide->hdhfd.cyls_def;
			ide->hdhfd.heads = ide->hdhfd.heads_def;
			ide->hdhfd.secspertrack = ide->hdhfd.secspertrack_def;
		}
	} else {
		ide->regs.ide_error |= IDE_ERR_ABRT;
		ide->regs.ide_status |= IDE_STATUS_ERR;
	}
	write_log (_T("IDE%d initialize drive parameters, CYL=%d,SPT=%d,HEAD=%d\n"),
		ide->num, ide->hdhfd.cyls, ide->hdhfd.secspertrack, ide->hdhfd.heads);
	ide_interrupt (ide);
}
static void ide_set_multiple_mode (struct ide_hdf *ide)
{
	write_log (_T("IDE%d drive multiple mode = %d\n"), ide->num, ide->regs.ide_nsector);
	ide->multiple_mode = ide->regs.ide_nsector;
	ide_interrupt (ide);
}
static void ide_set_features (struct ide_hdf *ide)
{
	int type = ide->regs.ide_nsector >> 3;
	int mode = ide->regs.ide_nsector & 7;

	write_log (_T("IDE%d set features %02X (%02X)\n"), ide->num, ide->regs.ide_feat, ide->regs.ide_nsector);
	ide_fail (ide);
}

static void get_lbachs (struct ide_hdf *ide, uae_u64 *lbap, unsigned int *cyl, unsigned int *head, unsigned int *sec)
{
	if (ide->lba48 && ide->lba48cmd && (ide->regs.ide_select & 0x40)) {
		uae_u64 lba;
		lba = (ide->regs.ide_hcyl << 16) | (ide->regs.ide_lcyl << 8) | ide->regs.ide_sector;
		lba |= ((ide->regs.ide_hcyl2 << 16) | (ide->regs.ide_lcyl2 << 8) | ide->regs.ide_sector2) << 24;
		*lbap = lba;
	} else {
		if (ide->regs.ide_select & 0x40) {
			*lbap = ((ide->regs.ide_select & 15) << 24) | (ide->regs.ide_hcyl << 16) | (ide->regs.ide_lcyl << 8) | ide->regs.ide_sector;
		} else {
			*cyl = (ide->regs.ide_hcyl << 8) | ide->regs.ide_lcyl;
			*head = ide->regs.ide_select & 15;
			*sec = ide->regs.ide_sector;
			*lbap = (((*cyl) * ide->hdhfd.heads + (*head)) * ide->hdhfd.secspertrack) + (*sec) - 1;
		}
	}
}

static int get_nsec (struct ide_hdf *ide)
{
	if (ide->lba48 && ide->lba48cmd)
		return (ide->regs.ide_nsector == 0 && ide->regs.ide_nsector2 == 0) ? 65536 : (ide->regs.ide_nsector2 * 256 + ide->regs.ide_nsector);
	else
		return ide->regs.ide_nsector == 0 ? 256 : ide->regs.ide_nsector;
}
static int dec_nsec (struct ide_hdf *ide, int v)
{
	if (ide->lba48 && ide->lba48cmd) {
		uae_u16 nsec;
		nsec = ide->regs.ide_nsector2 * 256 + ide->regs.ide_nsector;
		ide->regs.ide_nsector -= v;
		ide->regs.ide_nsector2 = nsec >> 8;
		ide->regs.ide_nsector = nsec & 0xff;
		return (ide->regs.ide_nsector2 << 8) | ide->regs.ide_nsector;
	} else {
		ide->regs.ide_nsector -= v;
		return ide->regs.ide_nsector;
	}
}

static void put_lbachs (struct ide_hdf *ide, uae_u64 lba, unsigned int cyl, unsigned int head, unsigned int sec, unsigned int inc)
{
	if (ide->lba48 && ide->lba48cmd) {
		lba += inc;
		ide->regs.ide_hcyl = (lba >> 16) & 0xff;
		ide->regs.ide_lcyl = (lba >> 8) & 0xff;
		ide->regs.ide_sector = lba & 0xff;
		lba >>= 24;
		ide->regs.ide_hcyl2 = (lba >> 16) & 0xff;
		ide->regs.ide_lcyl2 = (lba >> 8) & 0xff;
		ide->regs.ide_sector2 = lba & 0xff;
	} else {
		if (ide->regs.ide_select & 0x40) {
			lba += inc;
			ide->regs.ide_select &= ~15;
			ide->regs.ide_select |= (lba >> 24) & 15;
			ide->regs.ide_hcyl = (lba >> 16) & 0xff;
			ide->regs.ide_lcyl = (lba >> 8) & 0xff;
			ide->regs.ide_sector = lba & 0xff;
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
			ide->regs.ide_select &= ~15;
			ide->regs.ide_select |= head;
			ide->regs.ide_sector = sec;
			ide->regs.ide_hcyl = cyl >> 8;
			ide->regs.ide_lcyl = (uae_u8)cyl;
		}
	}
}

static void check_maxtransfer (struct ide_hdf *ide, int state)
{
	if (state == 1) {
		// transfer was started
		if (ide->maxtransferstate < 2 && ide->regs.ide_nsector == 0) {
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

static void setdrq (struct ide_hdf *ide)
{
	ide->regs.ide_status |= IDE_STATUS_DRQ;
	ide->regs.ide_status &= ~IDE_STATUS_BSY;
}
static void setbsy (struct ide_hdf *ide)
{
	ide->regs.ide_status |= IDE_STATUS_BSY;
	ide->regs.ide_status &= ~IDE_STATUS_DRQ;
}

static void process_rw_command (struct ide_hdf *ide)
{
	setbsy (ide);
	write_comm_pipe_u32 (&requests, ide->num, 1);
}
static void process_packet_command (struct ide_hdf *ide)
{
	setbsy (ide);
	write_comm_pipe_u32 (&requests, ide->num | 0x80, 1);
}

static void atapi_data_done (struct ide_hdf *ide)
{
	ide->regs.ide_nsector = ATAPI_IO | ATAPI_CD;
	ide->regs.ide_status = IDE_STATUS_DRDY;
	ide->data_size = 0;
	ide->packet_data_offset = 0;
	ide->data_offset = 0;
}

static bool atapi_set_size (struct ide_hdf *ide)
{
	int size;
	size = ide->data_size;
	ide->data_offset = 0;
	if (!size) {
		ide->packet_state = 0;
		ide->packet_transfer_size = 0;
		return false;
	}
	if (ide->packet_state == 2) {
		if (size > ide->packet_data_size)
			size = ide->packet_data_size;
		if (size > ATAPI_MAX_TRANSFER)
			size = ATAPI_MAX_TRANSFER;
		ide->packet_transfer_size = size & ~1;
		ide->regs.ide_lcyl = size & 0xff;
		ide->regs.ide_hcyl = size >> 8;
	} else {
		ide->packet_transfer_size = 12;
	}
	if (IDE_LOG > 1)
		write_log (_T("ATAPI data transfer %d/%d bytes\n"), ide->packet_transfer_size, ide->data_size);
	return true;
}

static void atapi_packet (struct ide_hdf *ide)
{
	ide->packet_data_offset = 0;
	ide->packet_data_size = (ide->regs.ide_hcyl << 8) | ide->regs.ide_lcyl;
	if (ide->packet_data_size == 65535)
		ide->packet_data_size = 65534;
	ide->data_size = 12;
	if (IDE_LOG > 0)
		write_log (_T("ATAPI packet command. Data size = %d\n"), ide->packet_data_size);
	ide->packet_state = 1;
	ide->data_multi = 1;
	ide->data_offset = 0;
	ide->regs.ide_nsector = ATAPI_CD;
	ide->regs.ide_error = 0;
	if (atapi_set_size (ide))
		setdrq (ide);
}

static void do_packet_command (struct ide_hdf *ide)
{
	memcpy (ide->scsi->cmd, ide->secbuf, 12);
	ide->scsi->cmd_len = 12;
	if (IDE_LOG > 0) {
		uae_u8 *c = ide->scsi->cmd;
		write_log (_T("ATASCSI %02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x\n"),
			c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11]);
	}
	ide->direction = 0;
	scsi_emulate_analyze (ide->scsi);
	if (ide->scsi->direction <= 0) {
		// data in
		ide->scsi->data_len = SECBUF_SIZE;
		scsi_emulate_cmd (ide->scsi);
		ide->data_size = ide->scsi->data_len;
		ide->regs.ide_status = 0;
		if (ide->scsi->status) {
			// error
			ide->regs.ide_error = (ide->scsi->sense[2] << 4) | 4;
			atapi_data_done (ide);
			ide->regs.ide_status |= ATAPI_STATUS_CHK;
			atapi_set_size (ide);
			return;
		} else if (ide->scsi->data_len) {
			// data in
			memcpy (ide->secbuf, ide->scsi->buffer, ide->scsi->data_len);
			ide->regs.ide_nsector = ATAPI_IO;
		} else {
			// no data
			atapi_data_done (ide);
		}
	} else {
		// data out
		ide->direction = 1;
		ide->regs.ide_nsector = 0;
		ide->data_size = ide->scsi->data_len;
	}
	ide->packet_state = 2; // data phase
	if (atapi_set_size (ide))
		ide->intdrq = true;
}

static void do_process_packet_command (struct ide_hdf *ide)
{
	if (ide->packet_state == 1) {
		do_packet_command (ide);
	} else {
		ide->packet_data_offset += ide->packet_transfer_size;
		if (!ide->direction) {
			// data still remaining, next transfer
			if (atapi_set_size (ide))
				ide->intdrq = true;
		} else {
			if (atapi_set_size (ide)) {
				ide->intdrq = true;
			} else {
				memcpy (&ide->scsi->buffer, ide->secbuf, ide->data_size);
				ide->scsi->data_len = ide->data_size;
				scsi_emulate_cmd (ide->scsi);
				if (IDE_LOG > 1)
					write_log (_T("IDE%d ATAPI write finished, %d bytes\n"), ide->num, ide->data_size);
			}
		}
	}
	ide_fast_interrupt (ide);
}

static void do_process_rw_command (struct ide_hdf *ide)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;
	bool last;

	ide->data_offset = 0;
	get_lbachs (ide, &lba, &cyl, &head, &sec);
	nsec = get_nsec (ide);
	if (IDE_LOG > 1)
		write_log (_T("IDE%d off=%d, nsec=%d (%d) lba48=%d\n"), ide->num, (uae_u32)lba, nsec, ide->multiple_mode, ide->lba48 + ide->lba48cmd);
	if (nsec * ide->blocksize > ide->hdhfd.size - lba * ide->blocksize) {
		nsec = (ide->hdhfd.size - lba * ide->blocksize) / ide->blocksize;
		if (IDE_LOG > 1)
			write_log (_T("IDE%d nsec changed to %d\n"), ide->num, nsec);
	}
	if (nsec <= 0) {
		ide_data_ready (ide);
		ide_fail_err (ide, IDE_ERR_IDNF);
		return;
	}
	if (nsec > ide->data_multi)
		nsec = ide->data_multi;

	if (ide->direction) {
		hdf_write (&ide->hdhfd.hfd, ide->secbuf, lba * ide->blocksize, nsec * ide->blocksize);
		if (IDE_LOG > 1)
			write_log (_T("IDE%d write, %d bytes written\n"), ide->num, nsec * ide->blocksize);
	} else {
		hdf_read (&ide->hdhfd.hfd, ide->secbuf, lba * ide->blocksize, nsec * ide->blocksize);
		if (IDE_LOG > 1)
			write_log (_T("IDE%d read, read %d bytes\n"), ide->num, nsec * ide->blocksize);
	}
	ide->intdrq = true;
	last = dec_nsec (ide, nsec) == 0;
	put_lbachs (ide, lba, cyl, head, sec, last ? nsec - 1 : nsec);
	if (last && ide->direction) {
		ide->intdrq = false;
		if (IDE_LOG > 1)
			write_log (_T("IDE%d write finished\n"), ide->num);
	}
	ide_fast_interrupt (ide);
}

static void ide_read_sectors (struct ide_hdf *ide, int flags)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;
	int multi = flags & 1;

	ide->lba48cmd = (flags & 2) != 0;
	if (multi && ide->multiple_mode == 0) {
		ide_fail (ide);
		return;
	}
	check_maxtransfer (ide, 1);
	gui_flicker_led (LED_HD, ide->num, 1);
	nsec = get_nsec (ide);
	get_lbachs (ide, &lba, &cyl, &head, &sec);
	if (lba * ide->blocksize >= ide->hdhfd.size) {
		ide_data_ready (ide);
		ide_fail_err (ide, IDE_ERR_IDNF);
		return;
	}
	if (IDE_LOG > 0)
		write_log (_T("IDE%d read off=%d, sec=%d (%d) lba48=%d\n"), ide->num, (uae_u32)lba, nsec, ide->multiple_mode, ide->lba48 + ide->lba48cmd);
	ide->data_multi = multi ? ide->multiple_mode : 1;
	ide->data_offset = 0;
	ide->data_size = nsec * ide->blocksize;
	ide->direction = 0;
	// read start: preload sector(s), then trigger interrupt.
	process_rw_command (ide);
}

static void ide_write_sectors (struct ide_hdf *ide, int flags)
{
	unsigned int cyl, head, sec, nsec;
	uae_u64 lba;
	int multi = flags & 1;

	ide->lba48cmd = (flags & 2) != 0;
	if (multi && ide->multiple_mode == 0) {
		ide_fail (ide);
		return;
	}
	check_maxtransfer (ide, 1);
	gui_flicker_led (LED_HD, ide->num, 2);
	nsec = get_nsec (ide);
	get_lbachs (ide, &lba, &cyl, &head, &sec);
	if (lba * ide->blocksize >= ide->hdhfd.size) {
		ide_data_ready (ide);
		ide_fail_err (ide, IDE_ERR_IDNF);
		return;
	}
	if (IDE_LOG > 0)
		write_log (_T("IDE%d write off=%d, sec=%d (%d) lba48=%d\n"), ide->num, (uae_u32)lba, nsec, ide->multiple_mode, ide->lba48 + ide->lba48cmd);
	if (nsec * ide->blocksize > ide->hdhfd.size - lba * ide->blocksize)
		nsec = (ide->hdhfd.size - lba * ide->blocksize) / ide->blocksize;
	if (nsec <= 0) {
		ide_data_ready (ide);
		ide_fail_err (ide, IDE_ERR_IDNF);
		return;
	}
	ide->data_multi = multi ? ide->multiple_mode : 1;
	ide->data_offset = 0;
	ide->data_size = nsec * ide->blocksize;
	ide->direction = 1;
	// write start: set DRQ and clear BSY. No interrupt.
	ide->regs.ide_status |= IDE_STATUS_DRQ;
	ide->regs.ide_status &= ~IDE_STATUS_BSY;
}

static void ide_do_command (struct ide_hdf *ide, uae_u8 cmd)
{
	int lba48 = ide->lba48;

	if (IDE_LOG > 1)
		write_log (_T("**** IDE%d command %02X\n"), ide->num, cmd);
	ide->regs.ide_status &= ~ (IDE_STATUS_DRDY | IDE_STATUS_DRQ | IDE_STATUS_ERR);
	ide->regs.ide_error = 0;
	ide->lba48cmd = false;

	if (ide->atapi) {

		gui_flicker_led (LED_CD, ide->num, 1);
		ide->atapi_drdy = true;
		if (cmd == 0x00) { /* nop */
			ide_interrupt (ide);
		} else if (cmd == 0x08) { /* device reset */
			ide_execute_drive_diagnostics (ide, true);
		} else if (cmd == 0xa1) { /* identify packet device */
			ide_identify_drive (ide);
		} else if (cmd == 0xa0) { /* packet */
			atapi_packet (ide);
		} else if (cmd == 0x90) { /* execute drive diagnostics */
			ide_execute_drive_diagnostics (ide, true);
		} else {
			ide_execute_drive_diagnostics (ide, false);
			ide->atapi_drdy = false;
			ide_fail (ide);
			write_log (_T("IDE%d: unknown ATAPI command 0x%02x\n"), ide->num, cmd);
		}

	} else {

		if (cmd == 0x10) { /* recalibrate */
			ide_recalibrate (ide);
		} else if (cmd == 0xec) { /* identify drive */
			ide_identify_drive (ide);
		} else if (cmd == 0x90) { /* execute drive diagnostics */
			ide_execute_drive_diagnostics (ide, true);
		} else if (cmd == 0x91) { /* initialize drive parameters */
			ide_initialize_drive_parameters (ide);
		} else if (cmd == 0xc6) { /* set multiple mode */
			ide_set_multiple_mode (ide);
		} else if (cmd == 0x20 || cmd == 0x21) { /* read sectors */
			ide_read_sectors (ide, 0);
		} else if (cmd == 0x24 && lba48) { /* read sectors ext */
			ide_read_sectors (ide, 2);
		} else if (cmd == 0xc4) { /* read multiple */
			ide_read_sectors (ide, 1);
		} else if (cmd == 0x29 && lba48) { /* read multiple ext */
			ide_read_sectors (ide, 1|2);
		} else if (cmd == 0x30 || cmd == 0x31) { /* write sectors */
			ide_write_sectors (ide, 0);
		} else if (cmd == 0x34 && lba48) { /* write sectors ext */
			ide_write_sectors (ide, 2);
		} else if (cmd == 0xc5) { /* write multiple */
			ide_write_sectors (ide, 1);
		} else if (cmd == 0x39 && lba48) { /* write multiple ext */
			ide_write_sectors (ide, 1|2);
		} else if (cmd == 0x50) { /* format track (nop) */
			ide_interrupt (ide);
		} else if (cmd == 0xef) { /* set features  */
			ide_set_features (ide);
		} else if (cmd == 0x00) { /* nop */
			ide_fail (ide);
		} else if (cmd == 0xe0 || cmd == 0xe1 || cmd == 0xe7 || cmd == 0xea) { /* standby now/idle/flush cache/flush cache ext */
			ide_interrupt (ide);
		} else if (cmd == 0xe5) { /* check power mode */
			ide->regs.ide_nsector = 0xff;
			ide_interrupt (ide);
		} else {
			ide_fail (ide);
			write_log (_T("IDE%d: unknown ATA command 0x%02x\n"), ide->num, cmd);
		}
	}
}

static uae_u16 ide_get_data (struct ide_hdf *ide)
{
	bool irq = false;
	bool last = false;
	uae_u16 v;

	if (IDE_LOG > 4)
		write_log (_T("IDE%d DATA read\n"), ide->num);
	if (ide->data_size == 0) {
		if (IDE_LOG > 0)
			write_log (_T("IDE%d DATA but no data left!? %02X PC=%08X\n"), ide->num, ide->regs.ide_status, m68k_getpc ());
		if (!isdrive (ide))
			return 0xffff;
		return 0;
	}
	if (ide->packet_state) {
		v = ide->secbuf[ide->packet_data_offset + ide->data_offset + 1] | (ide->secbuf[ide->packet_data_offset + ide->data_offset + 0] << 8);
		ide->data_offset += 2;
		if (ide->data_size < 0)
			ide->data_size += 2;
		else
			ide->data_size -= 2;
		if (ide->data_offset == ide->packet_transfer_size) {
			if (IDE_LOG > 1)
				write_log (_T("IDE%d ATAPI partial read finished, %d bytes remaining\n"), ide->num, ide->data_size);
			if (ide->data_size == 0) {
				ide->packet_state = 0;
				atapi_data_done (ide);
				if (IDE_LOG > 1)
					write_log (_T("IDE%d ATAPI read finished, %d bytes\n"), ide->num, ide->packet_data_offset + ide->data_offset);
				irq = true;
			} else {
				process_packet_command (ide);
			}
		}
	} else {
		v = ide->secbuf[ide->data_offset + 1] | (ide->secbuf[ide->data_offset + 0] << 8);
		ide->data_offset += 2;
		if (ide->data_size < 0) {
			ide->data_size += 2;
		} else {
			ide->data_size -= 2;
			if (((ide->data_offset % ide->blocksize) == 0) && ((ide->data_offset / ide->blocksize) % ide->data_multi) == 0) {
				if (ide->data_size)
					process_rw_command (ide);
			}
		}
		if (ide->data_size == 0) {
			if (!(ide->regs.ide_status & IDE_STATUS_DRQ)) {
				write_log (_T("IDE%d read finished but DRQ was not active?\n"), ide->num);
			}
			ide->regs.ide_status &= ~IDE_STATUS_DRQ;
			if (IDE_LOG > 1)
				write_log (_T("IDE%d read finished\n"), ide->num);
		}
	}
	if (irq)
		ide_fast_interrupt (ide);
	return v;
}

static void ide_put_data (struct ide_hdf *ide, uae_u16 v)
{
	if (IDE_LOG > 4)
		write_log (_T("IDE%d DATA write %04x %d/%d\n"), ide->num, v, ide->data_offset, ide->data_size);
	if (ide->data_size == 0) {
		if (IDE_LOG > 0)
			write_log (_T("IDE%d DATA write without request!? %02X PC=%08X\n"), ide->num, ide->regs.ide_status, m68k_getpc ());
		return;
	}
	ide->secbuf[ide->packet_data_offset + ide->data_offset + 1] = v & 0xff;
	ide->secbuf[ide->packet_data_offset + ide->data_offset + 0] = v >> 8;
	ide->data_offset += 2;
	ide->data_size -= 2;
	if (ide->packet_state) {
		if (ide->data_offset == ide->packet_transfer_size) {
			if (IDE_LOG > 0) {
				uae_u16 v = (ide->regs.ide_hcyl << 8) | ide->regs.ide_lcyl;
				write_log (_T("Data size after command received = %d (%d)\n"), v, ide->packet_data_size);
			}
			process_packet_command (ide);
		}
	} else {
		if (ide->data_size == 0) {
			process_rw_command (ide);
		} else if (((ide->data_offset % ide->blocksize) == 0) && ((ide->data_offset / ide->blocksize) % ide->data_multi) == 0) {
			process_rw_command (ide);
		}
	}
}

static int get_gayle_ide_reg (uaecptr addr, struct ide_hdf **ide)
{
	int ide2;
	uaecptr a = addr;
	addr &= 0xffff;
	*ide = NULL;
	if (addr >= GAYLE_IRQ_4000 && addr <= GAYLE_IRQ_4000 + 1 && currprefs.cs_ide == IDE_A4000)
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
	*ide = idedrive[ide2 + idedrive[ide2]->ide_drv];
	return addr;
}

static uae_u32 ide_read_reg (struct ide_hdf *ide, int ide_reg)
{
	uae_u8 v = 0;
	bool isdrv = isdrive (ide);

	if (!ide)
		goto end;

	if (ide->regs.ide_status & IDE_STATUS_BSY)
		ide_reg = IDE_STATUS;
	if (!isdrive (ide)) {
		if (ide_reg == IDE_STATUS && ide->pair->irq)
			ide->pair->irq = 0;
		if (isdrive (ide->pair))
			v = 0x00;
		else
			v = 0xff;
		goto end;
	}

	switch (ide_reg)
	{
	case IDE_DRVADDR:
		v = ((ide->ide_drv ? 2 : 1) | ((ide->regs.ide_select & 15) << 2)) ^ 0xff;
		break;
	case IDE_DATA:
		break;
	case IDE_ERROR:
		v = ide->regs.ide_error;
		break;
	case IDE_NSECTOR:
		if (isdrv) {
			if (ide->regs.ide_devcon & 0x80)
				v = ide->regs.ide_nsector2;
			else
				v = ide->regs.ide_nsector;
		}
		break;
	case IDE_SECTOR:
		if (isdrv) {
			if (ide->regs.ide_devcon & 0x80)
				v = ide->regs.ide_sector2;
			else
				v = ide->regs.ide_sector;
			check_maxtransfer (ide, 2);
		}
		break;
	case IDE_LCYL:
		if (isdrv) {
			if (ide->regs.ide_devcon & 0x80)
				v = ide->regs.ide_lcyl2;
			else
				v = ide->regs.ide_lcyl;
		}
		break;
	case IDE_HCYL:
		if (isdrv) {
			if (ide->regs.ide_devcon & 0x80)
				v = ide->regs.ide_hcyl2;
			else
				v = ide->regs.ide_hcyl;
		}
		break;
	case IDE_SELECT:
		v = ide->regs.ide_select;
		break;
	case IDE_STATUS:
		ide->irq = 0;
		/* fall through */
	case IDE_DEVCON: /* ALTSTATUS when reading */
		if (!isdrv) {
			v = 0;
			if (ide->regs.ide_error)
				v |= IDE_STATUS_ERR;
		} else {
			v = ide->regs.ide_status;
			if (!ide->atapi || (ide->atapi && ide->atapi_drdy))
				v |= IDE_STATUS_DRDY | IDE_STATUS_DSC;
		}
		break;
	}
end:
	if (IDE_LOG > 2 && ide_reg > 0 && (1 || ide->num > 0))
		write_log (_T("IDE%d GET register %d->%02X (%08X)\n"), ide->num, ide_reg, (uae_u32)v & 0xff, m68k_getpc ());
	return v;
}

static void ide_write_reg (struct ide_hdf *ide, int ide_reg, uae_u32 val)
{
	if (!ide)
		return;
	
	ide->regs1->ide_devcon &= ~0x80; /* clear HOB */
	ide->regs0->ide_devcon &= ~0x80; /* clear HOB */
	if (IDE_LOG > 2 && ide_reg > 0 && (1 || ide->num > 0))
		write_log (_T("IDE%d PUT register %d=%02X (%08X)\n"), ide->num, ide_reg, (uae_u32)val & 0xff, m68k_getpc ());

	switch (ide_reg)
	{
	case IDE_DRVADDR:
		break;
	case IDE_DEVCON:
		if ((ide->regs.ide_devcon & 4) == 0 && (val & 4) != 0) {
			reset_device (ide, true);
			if (IDE_LOG > 1)
				write_log (_T("IDE%d: SRST\n"), ide->num);
		}
		ide->regs0->ide_devcon = val;
		ide->regs1->ide_devcon = val;
		break;
	case IDE_DATA:
		break;
	case IDE_ERROR:
		ide->regs0->ide_feat2 = ide->regs0->ide_feat;
		ide->regs0->ide_feat = val;
		ide->regs1->ide_feat2 = ide->regs1->ide_feat;
		ide->regs1->ide_feat = val;
		break;
	case IDE_NSECTOR:
		ide->regs0->ide_nsector2 = ide->regs0->ide_nsector;
		ide->regs0->ide_nsector = val;
		ide->regs1->ide_nsector2 = ide->regs1->ide_nsector;
		ide->regs1->ide_nsector = val;
		break;
	case IDE_SECTOR:
		ide->regs0->ide_sector2 = ide->regs0->ide_sector;
		ide->regs0->ide_sector = val;
		ide->regs1->ide_sector2 = ide->regs1->ide_sector;
		ide->regs1->ide_sector = val;
		break;
	case IDE_LCYL:
		ide->regs0->ide_lcyl2 = ide->regs0->ide_lcyl;
		ide->regs0->ide_lcyl = val;
		ide->regs1->ide_lcyl2 = ide->regs1->ide_lcyl;
		ide->regs1->ide_lcyl = val;
		break;
	case IDE_HCYL:
		ide->regs0->ide_hcyl2 = ide->regs0->ide_hcyl;
		ide->regs0->ide_hcyl = val;
		ide->regs1->ide_hcyl2 = ide->regs1->ide_hcyl;
		ide->regs1->ide_hcyl = val;
		break;
	case IDE_SELECT:
		ide->regs0->ide_select = val;
		ide->regs1->ide_select = val;
#if IDE_LOG > 2
		if (ide->ide_drv != (val & 0x10) ? 1 : 0)
			write_log (_T("DRIVE=%d\n"), (val & 0x10) ? 1 : 0);
#endif
		ide->pair->ide_drv = ide->ide_drv = (val & 0x10) ? 1 : 0;
		break;
	case IDE_STATUS:
		ide->irq = 0;
		if (isdrive (ide)) {
			ide->regs.ide_status |= IDE_STATUS_BSY;
			ide_do_command (ide, val);
		}
		break;
	}
}

static uae_u32 gayle_read2 (uaecptr addr)
{
	struct ide_hdf *ide = NULL;
	int ide_reg;
	uae_u8 v = 0;

	addr &= 0xffff;
	if ((IDE_LOG > 3 && (addr != 0x2000 && addr != 0x2001 && addr != 0x3020 && addr != 0x3021 && addr != GAYLE_IRQ_1200)) || IDE_LOG > 5)
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
	ide_reg = get_gayle_ide_reg (addr, &ide);
	/* Emulated "ide killer". Prevents long KS boot delay if no drives installed */
	if (!isdrive (idedrive[0]) && !isdrive (idedrive[1]) && !isdrive (idedrive[2]) && !isdrive (idedrive[3])) {
		if (ide_reg == IDE_STATUS)
			return 0x7f;
		return 0xff;
	}
	return ide_read_reg (ide, ide_reg);
}

static void gayle_write2 (uaecptr addr, uae_u32 val)
{
	struct ide_hdf *ide = NULL;
	int ide_reg;

	if ((IDE_LOG > 3 && (addr != 0x2000 && addr != 0x2001 && addr != 0x2020 && addr != 0x2021 && addr != GAYLE_IRQ_1200)) || IDE_LOG > 5)
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
	ide_reg = get_gayle_ide_reg (addr, &ide);
	ide_write_reg (ide, ide_reg, val);
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

static bool isa4000t (uaecptr *paddr)
{
	if (currprefs.cs_mbdmac != 2)
		return false;
	uaecptr addr = *paddr;
	if ((addr & 0xffff) >= (GAYLE_BASE_4000 & 0xffff))
		return false;
	addr &= 0xff;
	*paddr = addr;
	return true;
}

static uae_u32 REGPARAM2 gayle_lget (uaecptr addr)
{
	struct ide_hdf *ide = NULL;
	int ide_reg;
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
#ifdef NCR
	if (currprefs.cs_mbdmac == 2 && (addr & 0xffff) == 0x3000)
		return 0xffffffff; // NCR DIP BANK
	if (isa4000t (&addr)) {
		if (addr >= NCR_ALT_OFFSET) {
			addr &= NCR_MASK;
			v = (ncr_io_bget (addr + 3) << 0) | (ncr_io_bget (addr + 2) << 8) |
				(ncr_io_bget (addr + 1) << 16) | (ncr_io_bget (addr + 0) << 24);
		} else if (addr >= NCR_OFFSET) {
			addr &= NCR_MASK;
			v = (ncr_io_bget (addr + 3) << 0) | (ncr_io_bget (addr + 2) << 8) |
				(ncr_io_bget (addr + 1) << 16) | (ncr_io_bget (addr + 0) << 24);
		}
		return v;
	}
#endif
	ide_reg = get_gayle_ide_reg (addr, &ide);
	if (ide_reg == IDE_DATA) {
		v = ide_get_data (ide) << 16;
		v |= ide_get_data (ide);
		return v;
	}
	v = gayle_wget (addr) << 16;
	v |= gayle_wget (addr + 2);
	return v;
}
static uae_u32 REGPARAM2 gayle_wget (uaecptr addr)
{
	struct ide_hdf *ide = NULL;
	int ide_reg;
	uae_u16 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
#ifdef NCR
	if (currprefs.cs_mbdmac == 2 && (addr & (0xffff - 1)) == 0x3000)
		return 0xffff; // NCR DIP BANK
	if (isa4000t (&addr)) {
		if (addr >= NCR_OFFSET) {
			addr &= NCR_MASK;
			v = (ncr_io_bget (addr) << 8) | ncr_io_bget (addr + 1);
		}
		return v;
	}
#endif
	ide_reg = get_gayle_ide_reg (addr, &ide);
	if (ide_reg == IDE_DATA)
		return ide_get_data (ide);
	v = gayle_bget (addr) << 8;
	v |= gayle_bget (addr + 1);
	return v;
}
static uae_u32 REGPARAM2 gayle_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
#ifdef NCR
	if (currprefs.cs_mbdmac == 2 && (addr & (0xffff - 3)) == 0x3000)
		return 0xff; // NCR DIP BANK
	if (isa4000t (&addr)) {
		if (addr >= NCR_OFFSET) {
			addr &= NCR_MASK;
			return ncr_io_bget (addr);
		}
		return 0;
	}
#endif
	return gayle_read (addr);
}

static void REGPARAM2 gayle_lput (uaecptr addr, uae_u32 value)
{
	struct ide_hdf *ide = NULL;
	int ide_reg;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (isa4000t (&addr)) {
		if (addr >= NCR_ALT_OFFSET) {
			addr &= NCR_MASK;
			ncr_io_bput (addr + 3, value >> 0);
			ncr_io_bput (addr + 2, value >> 8);
			ncr_io_bput (addr + 1, value >> 16);
			ncr_io_bput (addr + 0, value >> 24);
		} else if (addr >= NCR_OFFSET) {
			addr &= NCR_MASK;
			ncr_io_bput (addr + 3, value >> 0);
			ncr_io_bput (addr + 2, value >> 8);
			ncr_io_bput (addr + 1, value >> 16);
			ncr_io_bput (addr + 0, value >> 24);
		}
		return;
	}
	ide_reg = get_gayle_ide_reg (addr, &ide);
	if (ide_reg == IDE_DATA) {
		ide_put_data (ide, value >> 16);
		ide_put_data (ide, value & 0xffff);
		return;
	}
	gayle_wput (addr, value >> 16);
	gayle_wput (addr + 2, value & 0xffff);
}
static void REGPARAM2 gayle_wput (uaecptr addr, uae_u32 value)
{
	struct ide_hdf *ide = NULL;
	int ide_reg;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#ifdef NCR
	if (isa4000t (&addr)) {
		if (addr >= NCR_OFFSET) {
			addr &= NCR_MASK;
			ncr_io_bput (addr, value >> 8);
			ncr_io_bput (addr + 1, value);
		}
		return;
	}
#endif
	ide_reg = get_gayle_ide_reg (addr, &ide);
	if (ide_reg == IDE_DATA) {
		ide_put_data (ide, value);
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
#ifdef NCR
	if (isa4000t (&addr)) {
		if (addr >= NCR_OFFSET) {
			addr &= NCR_MASK;
			ncr_io_bput (addr, value);
		}
		return;
	}
#endif
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
static int garyidoffset;
static int gary_coldboot;
int gary_timeout;
int gary_toenb;

static void mbres_write (uaecptr addr, uae_u32 val, int size)
{
	if ((addr & 0xffff) >= 0x8000) {
		dummy_put(addr, size, val);
		return;
	}

	addr &= 0xffff;
	if (MBRES_LOG > 0)
		write_log (_T("MBRES_WRITE %08X=%08X (%d) PC=%08X S=%d\n"), addr, val, size, M68K_GETPC, regs.s);
	if (addr < 0x8000 && (1 || regs.s)) { /* CPU FC = supervisor only */
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

	if ((addr & 0xffff) >= 0x8000)
		return dummy_get(addr, size, false);

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
		if (!ide[i]) {
			ide[i] = xcalloc (struct ide_hdf, 1);
			ide[i]->cd_unit_num = -1;
		}
	}
}

static struct ide_hdf *add_ide_unit (int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	alloc_ide_mem (idedrive, TOTAL_IDE * 2);
	ide = idedrive[ch];
	if (ci)
		memcpy (&ide->hdhfd.hfd.ci, ci, sizeof (struct uaedev_config_info));
	if (ci->type == UAEDEV_CD && ci->device_emu_unit >= 0) {
		device_func_init (0);
		ide->scsi = scsi_alloc_cd (ch, ci->device_emu_unit, true);
		if (!ide->scsi) {
			write_log (_T("IDE: CD EMU unit %d failed to open\n"), ide->cd_unit_num);
			return NULL;
		}
		ide->cd_unit_num = ci->device_emu_unit;
		ide->atapi = true;
		ide->blocksize = 512;
		gui_flicker_led (LED_CD, ch, -1);

		write_log (_T("IDE%d CD %d\n"), ch, ide->cd_unit_num);

	} else if (ci->type == UAEDEV_HDF) {
		if (!hdf_hd_open (&ide->hdhfd))
			return NULL;
		ide->blocksize = ide->hdhfd.hfd.ci.blocksize;
		ide->lba48 = ide->hdhfd.size >= 128 * (uae_u64)0x40000000 ? 1 : 0;
		gui_flicker_led (LED_HD, ch, -1);
		ide->cd_unit_num = -1;

		write_log (_T("IDE%d HD '%s', LCHS=%d/%d/%d. PCHS=%d/%d/%d %uM. LBA48=%d\n"),
			ch, ide->hdhfd.hfd.ci.rootdir,
			ide->hdhfd.cyls, ide->hdhfd.heads, ide->hdhfd.secspertrack,
			ide->hdhfd.hfd.ci.pcyls, ide->hdhfd.hfd.ci.pheads, ide->hdhfd.hfd.ci.psecs,
			(int)(ide->hdhfd.size / (1024 * 1024)), ide->lba48);

	}
	ide->regs.ide_status = 0;
	ide->data_offset = 0;
	ide->data_size = 0;
	return ide;
}

static int pcmcia_common_size, pcmcia_attrs_size;
static int pcmcia_common_mask;
static uae_u8 *pcmcia_common;
static uae_u8 *pcmcia_attrs;
static int pcmcia_write_min, pcmcia_write_max;
static int pcmcia_oddevenflip;
static uae_u16 pcmcia_idedata;

static int get_pcmcmia_ide_reg (uaecptr addr, int width, struct ide_hdf **ide)
{
	int reg = -1;

	*ide = NULL;
	addr &= 0x80000 - 1;
	if (addr < 0x20000)
		return -1; /* attribute */
	if (addr >= 0x40000)
		return -1;
	addr -= 0x20000;
	// 8BITODD
	if (addr >= 0x10000) {
		addr &= ~0x10000;
		addr |= 1;
	}
	*ide = idedrive[PCMCIA_IDE_ID * 2];
	if ((*ide)->ide_drv)
		*ide = idedrive[PCMCIA_IDE_ID * 2 + 1];
	if (pcmcia_configured == 1) {
		// IO mapped linear
		reg = addr & 15;
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
	} else if (pcmcia_configured == 2) {
		// primary io mapped (PC)
		if (addr >= 0x1f0 && addr <= 0x1f7) {
			reg = addr - 0x1f0;
		} else if (addr == 0x3f6) {
			reg = IDE_DEVCON;
		} else if (addr == 0x3f7) {
			reg = IDE_DRVADDR;
		} else {
			reg = -1;
		}
	}
	return reg;
}

static uae_u32 gayle_attr_read (uaecptr addr)
{
	struct ide_hdf *ide = NULL;
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
		if (pcmcia_configured >= 0) {
			int reg = get_pcmcmia_ide_reg (addr, 1, &ide);
			if (reg >= 0) {
				if (reg == 0) {
					if (addr >= 0x30000) {
						return pcmcia_idedata & 0xff;
					} else {
						pcmcia_idedata = ide_get_data (ide);
						return (pcmcia_idedata >> 8) & 0xff;
					}
				} else {
					return ide_read_reg (ide, reg);
				}
			}
		}
	}
	v = pcmcia_attrs[addr / 2];
	return v;
}

static void gayle_attr_write (uaecptr addr, uae_u32 v)
{
	struct ide_hdf *ide = NULL;
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
						int index = v & 0x3f;
						if (index != 1 && index != 2) {
							write_log (_T("WARNING: Only config index 1 and 2 emulated, attempted to select %d!\n"), index);
						} else {
							pcmcia_configured = index;
							write_log (_T("PCMCIA IO configured = %02x\n"), v);
						}
					}
				}
			}
			if (pcmcia_configured >= 0) {
				int reg = get_pcmcmia_ide_reg (addr, 1, &ide);
				if (reg >= 0) {
					if (reg == 0) {
						if (addr >= 0x30000) {
							pcmcia_idedata = (v & 0xff) << 8;
						} else {
							pcmcia_idedata &= 0xff00;
							pcmcia_idedata |= v & 0xff;
							ide_put_data (ide, pcmcia_idedata);
						}
						return;
					}
					ide_write_reg (ide, reg, v);
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
			int blocksize = pcmcia_sram->hfd.ci.blocksize;
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
	_tcscpy (pcmcia_sram->hfd.ci.rootdir, path);
	pcmcia_sram->hfd.ci.readonly = readonly != 0;
	pcmcia_sram->hfd.ci.blocksize = 512;

	if (type == PCMCIA_SRAM) {
		if (reset) {
			if (path)
				hdf_hd_open (pcmcia_sram);
		} else {
			pcmcia_sram->hfd.drive_empty = 0;
		}

		if (pcmcia_sram->hfd.ci.readonly)
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
			if (path) {
				struct uaedev_config_info ci = { 0 };
				_tcscpy (ci.rootdir , path);
				ci.blocksize = 512;
				ci.readonly = readonly != 0;
				add_ide_unit (PCMCIA_IDE_ID * 2, &ci);
			}
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

	if (pcmcia_type == PCMCIA_IDE && pcmcia_configured >= 0) {
		struct ide_hdf *ide = NULL;
		int reg = get_pcmcmia_ide_reg (addr, 2, &ide);
		if (reg == IDE_DATA) {
			// 16-bit register
			pcmcia_idedata = ide_get_data (ide);
			return pcmcia_idedata;
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

	if (pcmcia_type == PCMCIA_IDE && pcmcia_configured >= 0) {
		struct ide_hdf *ide = NULL;
		int reg = get_pcmcmia_ide_reg (addr, 2, &ide);
		if (reg == IDE_DATA) {
			// 16-bit register
			pcmcia_idedata = value;
			ide_put_data (ide, pcmcia_idedata);
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
		map_banks_cond (&dummy_bank, 0xa0, 8, 0);
		if (currprefs.chipmem_size <= 4 * 1024 * 1024 && getz2endaddr () <= 4 * 1024 * 1024)
			map_banks_cond (&dummy_bank, PCMCIA_COMMON_START >> 16, PCMCIA_COMMON_SIZE >> 16, 0);
	} else {
		map_banks_cond (&gayle_attr_bank, 0xa0, 8, 0);
		if (currprefs.chipmem_size <= 4 * 1024 * 1024 && getz2endaddr () <= 4 * 1024 * 1024)
			map_banks_cond (&gayle_common_bank, PCMCIA_COMMON_START >> 16, PCMCIA_COMMON_SIZE >> 16, 0);
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
			if (ide->scsi) {
				scsi_free (ide->scsi);
			} else {
				hdf_hd_close (&ide->hdhfd);
			}
			memset (ide, 0, sizeof (struct ide_hdf));
			ide->cd_unit_num = -1;
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

int gayle_add_ide_unit (int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	if (ch >= 2 * 2)
		return -1;
	ide = add_ide_unit (ch, ci);
	if (ide == NULL)
		return 0;
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

static void *ide_thread (void *null)
{
	for (;;) {
		uae_u32 unit = read_comm_pipe_u32_blocking (&requests);
		struct ide_hdf *ide;
		if (gayle_thread_running == 0 || unit == 0xfffffff)
			break;
		ide = idedrive[unit & 0x7f];
		if (unit & 0x80)
			do_process_packet_command (ide);
		else
			do_process_rw_command (ide);
	}
	gayle_thread_running = -1;
	return 0;
}

static void initide (void)
{
	int i;

	if (!gayle_thread_running) {
		gayle_thread_running = 1;
		init_comm_pipe (&requests, 100, 1);
		uae_start_thread (_T("ide"), ide_thread, 0, NULL);
	}

	alloc_ide_mem (idedrive, TOTAL_IDE * 2);
	if (isrestore ())
		return;
	for (i = 0; i < TOTAL_IDE; i++) {
		struct ide_hdf *ide;

		ide = idedrive[i * 2 + 0];
		ide->regs0 = &ide->regs;
		ide->regs1 = &idedrive[i * 2 + 1]->regs;
		ide->pair = idedrive[i * 2 + 1];

		ide = idedrive[i * 2 + 1];
		ide->regs1 = &ide->regs;
		ide->regs0 = &idedrive[i * 2 + 0]->regs;
		ide->pair = idedrive[i * 2 + 0];

		reset_device (ide, true);
	}
	ide_splitter = 0;
	if (isdrive (idedrive[2]) || isdrive(idedrive[3])) {
		ide_splitter = 1;
		write_log (_T("IDE splitter enabled\n"));
	}
	for (i = 0; i < TOTAL_IDE * 2; i++)
		idedrive[i]->num = i;
	gayle_irq = gayle_int = 0;
}

void gayle_free (void)
{
	if (gayle_thread_running > 0) {
		gayle_thread_running = 0;
		write_comm_pipe_u32 (&requests, 0xffffffff, 1);
		while(gayle_thread_running == 0)
			sleep_millis (10);
		gayle_thread_running = 0;
	}
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
#ifdef NCR
	if (currprefs.cs_mbdmac == 2) {
		_tcscat (bankname, _T(" + NCR53C710 SCSI"));
		ncr_init ();
		ncr_reset ();
	}
#endif
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
	save_string (ide->hdhfd.hfd.ci.rootdir);
	save_u32 (ide->hdhfd.hfd.ci.blocksize);
	save_u32 (ide->hdhfd.hfd.ci.readonly);
	save_u8 (ide->multiple_mode);
	save_u32 (ide->hdhfd.cyls);
	save_u32 (ide->hdhfd.heads);
	save_u32 (ide->hdhfd.secspertrack);
	save_u8 (ide->regs.ide_select);
	save_u8 (ide->regs.ide_nsector);
	save_u8 (ide->regs.ide_nsector2);
	save_u8 (ide->regs.ide_sector);
	save_u8 (ide->regs.ide_sector2);
	save_u8 (ide->regs.ide_lcyl);
	save_u8 (ide->regs.ide_lcyl2);
	save_u8 (ide->regs.ide_hcyl);
	save_u8 (ide->regs.ide_hcyl2);
	save_u8 (ide->regs.ide_feat);
	save_u8 (ide->regs.ide_feat2);
	save_u8 (ide->regs.ide_error);
	save_u8 (ide->regs.ide_devcon);
	save_u64 (ide->hdhfd.hfd.virtual_size);
	save_u32 (ide->hdhfd.hfd.ci.sectors);
	save_u32 (ide->hdhfd.hfd.ci.surfaces);
	save_u32 (ide->hdhfd.hfd.ci.reserved);
	save_u32 (ide->hdhfd.hfd.ci.bootpri);
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
	_tcscpy (ide->hdhfd.hfd.ci.rootdir, path);
	blocksize = restore_u32 ();
	readonly = restore_u32 ();
	ide->multiple_mode = restore_u8 ();
	ide->hdhfd.cyls = restore_u32 ();
	ide->hdhfd.heads = restore_u32 ();
	ide->hdhfd.secspertrack = restore_u32 ();
	ide->regs.ide_select = restore_u8 ();
	ide->regs.ide_nsector = restore_u8 ();
	ide->regs.ide_sector = restore_u8 ();
	ide->regs.ide_lcyl = restore_u8 ();
	ide->regs.ide_hcyl = restore_u8 ();
	ide->regs.ide_feat = restore_u8 ();
	ide->regs.ide_nsector2 = restore_u8 ();
	ide->regs.ide_sector2 = restore_u8 ();
	ide->regs.ide_lcyl2 = restore_u8 ();
	ide->regs.ide_hcyl2 = restore_u8 ();
	ide->regs.ide_feat2 = restore_u8 ();
	ide->regs.ide_error = restore_u8 ();
	ide->regs.ide_devcon = restore_u8 ();
	ide->hdhfd.hfd.virtual_size = restore_u64 ();
	ide->hdhfd.hfd.ci.sectors = restore_u32 ();
	ide->hdhfd.hfd.ci.surfaces = restore_u32 ();
	ide->hdhfd.hfd.ci.reserved = restore_u32 ();
	ide->hdhfd.hfd.ci.bootpri = restore_u32 ();
	if (ide->hdhfd.hfd.virtual_size)
		gayle_add_ide_unit (num, NULL);
	else
		gayle_add_ide_unit (num, NULL);
	xfree (path);
	return src;
}
