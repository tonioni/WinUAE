/*
* UAE - The Un*x Amiga Emulator
*
* A590/A2091/A3000/CDTV SCSI expansion (DMAC/SuperDMAC + WD33C93) emulation
* Includes A590 + XT drive emulation.
*
* Copyright 2007-2014 Toni Wilen
*
*/

#define A2091_DEBUG 0
#define A2091_DEBUG_IO 0
#define XT_DEBUG 0
#define A3000_DEBUG 0
#define A3000_DEBUG_IO 0
#define WD33C93_DEBUG 0
#define WD33C93_DEBUG_PIO 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "scsi.h"
#include "threaddep/thread.h"
#include "a2091.h"
#include "blkdev.h"
#include "gui.h"
#include "zfile.h"
#include "filesys.h"
#include "autoconf.h"
#include "cdtv.h"
#include "savestate.h"

#define ROM_VECTOR 0x2000
#define ROM_OFFSET 0x2000

/* SuperDMAC CNTR bits. */
#define SCNTR_TCEN	(1<<5)
#define SCNTR_PREST	(1<<4)
#define SCNTR_PDMD	(1<<3)
#define SCNTR_INTEN	(1<<2)
#define SCNTR_DDIR	(1<<1)
#define SCNTR_IO_DX	(1<<0)
/* DMAC CNTR bits. */
#define CNTR_TCEN	(1<<7)
#define CNTR_PREST	(1<<6)
#define CNTR_PDMD	(1<<5)
#define CNTR_INTEN	(1<<4)
#define CNTR_DDIR	(1<<3)
/* ISTR bits. */
#define ISTR_INT_F	(1<<7)	/* Interrupt Follow */
#define ISTR_INTS	(1<<6)	/* SCSI or XT Peripheral Interrupt */
#define ISTR_E_INT	(1<<5)	/* End-Of-Process Interrupt */
#define ISTR_INT_P	(1<<4)	/* Interrupt Pending */
#define ISTR_UE_INT	(1<<3)	/* Under-Run FIFO Error Interrupt */
#define ISTR_OE_INT	(1<<2)	/* Over-Run FIFO Error Interrupt */
#define ISTR_FF_FLG	(1<<1)	/* FIFO-Full Flag */
#define ISTR_FE_FLG	(1<<0)	/* FIFO-Empty Flag */

/* wd register names */
#define WD_OWN_ID		0x00
#define WD_CONTROL		0x01
#define WD_TIMEOUT_PERIOD	0x02
#define WD_CDB_1		0x03
#define WD_T_SECTORS	0x03
#define WD_CDB_2		0x04
#define WD_T_HEADS		0x04
#define WD_CDB_3		0x05
#define WD_T_CYLS_0		0x05
#define WD_CDB_4		0x06
#define WD_T_CYLS_1		0x06
#define WD_CDB_5		0x07
#define WD_L_ADDR_0		0x07
#define WD_CDB_6		0x08
#define WD_L_ADDR_1		0x08
#define WD_CDB_7		0x09
#define WD_L_ADDR_2		0x09
#define WD_CDB_8		0x0a
#define WD_L_ADDR_3		0x0a
#define WD_CDB_9		0x0b
#define WD_SECTOR		0x0b
#define WD_CDB_10		0x0c
#define WD_HEAD			0x0c
#define WD_CDB_11		0x0d
#define WD_CYL_0		0x0d
#define WD_CDB_12		0x0e
#define WD_CYL_1		0x0e
#define WD_TARGET_LUN		0x0f
#define WD_COMMAND_PHASE	0x10
#define WD_SYNCHRONOUS_TRANSFER 0x11
#define WD_TRANSFER_COUNT_MSB	0x12
#define WD_TRANSFER_COUNT	0x13
#define WD_TRANSFER_COUNT_LSB	0x14
#define WD_DESTINATION_ID	0x15
#define WD_SOURCE_ID		0x16
#define WD_SCSI_STATUS		0x17
#define WD_COMMAND		0x18
#define WD_DATA			0x19
#define WD_QUEUE_TAG		0x1a
#define WD_AUXILIARY_STATUS	0x1f
/* WD commands */
#define WD_CMD_RESET		0x00
#define WD_CMD_ABORT		0x01
#define WD_CMD_ASSERT_ATN	0x02
#define WD_CMD_NEGATE_ACK	0x03
#define WD_CMD_DISCONNECT	0x04
#define WD_CMD_RESELECT		0x05
#define WD_CMD_SEL_ATN		0x06
#define WD_CMD_SEL		0x07
#define WD_CMD_SEL_ATN_XFER	0x08
#define WD_CMD_SEL_XFER		0x09
#define WD_CMD_RESEL_RECEIVE	0x0a
#define WD_CMD_RESEL_SEND	0x0b
#define WD_CMD_WAIT_SEL_RECEIVE	0x0c
#define WD_CMD_TRANS_ADDR	0x18
#define WD_CMD_TRANS_INFO	0x20
#define WD_CMD_TRANSFER_PAD	0x21
#define WD_CMD_SBT_MODE		0x80

/* paused or aborted interrupts */
#define CSR_MSGIN			0x20
#define CSR_SDP				0x21
#define CSR_SEL_ABORT		0x22
#define CSR_RESEL_ABORT		0x25
#define CSR_RESEL_ABORT_AM	0x27
#define CSR_ABORT			0x28
/* successful completion interrupts */
#define CSR_RESELECT		0x10
#define CSR_SELECT			0x11
#define CSR_TRANS_ADDR		0x15
#define CSR_SEL_XFER_DONE	0x16
#define CSR_XFER_DONE		0x18
/* terminated interrupts */
#define CSR_INVALID			0x40
#define CSR_UNEXP_DISC		0x41
#define CSR_TIMEOUT			0x42
#define CSR_PARITY			0x43
#define CSR_PARITY_ATN		0x44
#define CSR_BAD_STATUS		0x45
#define CSR_UNEXP			0x48
/* service required interrupts */
#define CSR_RESEL			0x80
#define CSR_RESEL_AM		0x81
#define CSR_DISC			0x85
#define CSR_SRV_REQ			0x88
/* SCSI Bus Phases */
#define PHS_DATA_OUT	0x00
#define PHS_DATA_IN		0x01
#define PHS_COMMAND		0x02
#define PHS_STATUS		0x03
#define PHS_MESS_OUT	0x06
#define PHS_MESS_IN		0x07

/* Auxialiry status */
#define ASR_INT			0x80	/* Interrupt pending */
#define ASR_LCI			0x40	/* Last command ignored */
#define ASR_BSY			0x20	/* Busy, only cmd/data/asr readable */
#define ASR_CIP			0x10	/* Busy, cmd unavail also */
#define ASR_xxx			0x0c
#define ASR_PE			0x02	/* Parity error (even) */
#define ASR_DBR			0x01	/* Data Buffer Ready */
/* Status */
#define CSR_CAUSE		0xf0
#define CSR_RESET		0x00	/* chip was reset */
#define CSR_CMD_DONE	0x10	/* cmd completed */
#define CSR_CMD_STOPPED	0x20	/* interrupted or abrted*/
#define CSR_CMD_ERR		0x40	/* end with error */
#define CSR_BUS_SERVICE	0x80	/* REQ pending on the bus */
/* Control */
#define CTL_DMA			0x80	/* Single byte dma */
#define CTL_DBA_DMA		0x40	/* direct buffer access (bus master) */
#define CTL_BURST_DMA	0x20	/* continuous mode (8237) */
#define CTL_NO_DMA		0x00	/* Programmed I/O */
#define CTL_HHP			0x10	/* Halt on host parity error */
#define CTL_EDI			0x08	/* Ending disconnect interrupt */
#define CTL_IDI			0x04	/* Intermediate disconnect interrupt*/
#define CTL_HA			0x02	/* Halt on ATN */
#define CTL_HSP			0x01	/* Halt on SCSI parity error */

/* SCSI Messages */
#define MSG_COMMAND_COMPLETE 0x00
#define MSG_SAVE_DATA_POINTER 0x02
#define MSG_RESTORE_DATA_POINTERS 0x03
#define MSG_NOP 0x08
#define MSG_IDENTIFY 0x80

/* XT hard disk controller registers */
#define XD_DATA         0x00    /* data RW register */
#define XD_RESET        0x01    /* reset WO register */
#define XD_STATUS       0x01    /* status RO register */
#define XD_SELECT       0x02    /* select WO register */
#define XD_JUMPER       0x02    /* jumper RO register */
#define XD_CONTROL      0x03    /* DMAE/INTE WO register */
#define XD_RESERVED     0x03    /* reserved */

/* XT hard disk controller commands (incomplete list) */
#define XT_CMD_TESTREADY   0x00    /* test drive ready */
#define XT_CMD_RECALIBRATE 0x01    /* recalibrate drive */
#define XT_CMD_SENSE       0x03    /* request sense */
#define XT_CMD_FORMATDRV   0x04    /* format drive */
#define XT_CMD_VERIFY      0x05    /* read verify */
#define XT_CMD_FORMATTRK   0x06    /* format track */
#define XT_CMD_FORMATBAD   0x07    /* format bad track */
#define XT_CMD_READ        0x08    /* read */
#define XT_CMD_WRITE       0x0A    /* write */
#define XT_CMD_SEEK        0x0B    /* seek */
/* Controller specific commands */
#define XT_CMD_DTCSETPARAM 0x0C    /* set drive parameters (DTC 5150X & CX only?) */

/* Bits for command status byte */
#define XT_CSB_ERROR       0x02    /* error */
#define XT_CSB_LUN         0x20    /* logical Unit Number */

/* XT hard disk controller status bits */
#define XT_STAT_READY      0x01    /* controller is ready */
#define XT_STAT_INPUT      0x02    /* data flowing from controller to host */
#define XT_STAT_COMMAND    0x04    /* controller in command phase */
#define XT_STAT_SELECT     0x08    /* controller is selected */
#define XT_STAT_REQUEST    0x10    /* controller requesting data */
#define XT_STAT_INTERRUPT  0x20    /* controller requesting interrupt */

/* XT hard disk controller control bits */
#define XT_INT          0x02    /* Interrupt enable */
#define XT_DMA_MODE     0x01    /* DMA enable */

#define XT_UNIT 7
#define XT_SECTORS 17 /* hardwired */

static struct wd_state wd_a2091;
static struct wd_state wd_a2091_2;
static struct wd_state wd_a3000;
struct wd_state wd_cdtv;

static struct wd_state *wda2091[] = {
		&wd_a2091,
		&wd_a2091_2,
};

static struct wd_state *wdscsi[] = {
		&wd_a2091,
		&wd_a2091_2,
		&wd_a3000,
		&wd_cdtv,
		NULL
};

static int isirq (struct wd_state *wd)
{
	if (!wd->enabled)
		return 0;
	if (wd->superdmac) {
		if (wd->auxstatus & ASR_INT)
			wd->dmac_istr |= ISTR_INTS;
		if ((wd->dmac_cntr & SCNTR_INTEN) && (wd->dmac_istr & (ISTR_INTS | ISTR_E_INT)))
			return 1;
	} else {
		if (wd->xt_irq)
			wd->dmac_istr |= ISTR_INTS;
		if (wd->auxstatus & ASR_INT)
			wd->dmac_istr |= ISTR_INTS;
		if ((wd->dmac_cntr & CNTR_INTEN) && (wd->dmac_istr & (ISTR_INTS | ISTR_E_INT)))
			return 1;
	}
	return 0;
}

void rethink_a2091 (void)
{
	if (isirq (&wd_a2091) ||isirq (&wd_a2091_2) || isirq (&wd_a3000)) {
		uae_int_requested |= 2;
#if A2091_DEBUG > 2 || A3000_DEBUG > 2
		write_log (_T("Interrupt_RETHINK\n"));
#endif
	} else {
		uae_int_requested &= ~2;
	}
}

static void dmac_scsi_int(struct wd_state *wd)
{
	if (!wd->enabled)
		return;
	if (!(wd->auxstatus & ASR_INT))
		return;
	if (isirq (wd))
		uae_int_requested |= 2;
}

static void dmac_xt_int(struct wd_state *wd)
{
	if (!wd->enabled)
		return;
	wd->xt_irq = true;
	if (isirq(wd))
		uae_int_requested |= 2;
}

void scsi_dmac_start_dma (struct wd_state *wd)
{
#if A3000_DEBUG > 0 || A2091_DEBUG > 0
	write_log (_T("DMAC DMA started, ADDR=%08X, LEN=%08X words\n"), wd->dmac_acr, wd->dmac_wtc);
#endif
	wd->dmac_dma = 1;
}
void scsi_dmac_stop_dma (struct wd_state *wd)
{
	wd->dmac_dma = 0;
	wd->dmac_istr &= ~ISTR_E_INT;
}

static void dmac_reset (struct wd_state *wd)
{
#if WD33C93_DEBUG > 0
	if (wd->superdmac)
		write_log (_T("A3000 %s SCSI reset\n"), WD33C93);
	else
		write_log (_T("A2091 %s SCSI reset\n"), WD33C93);
#endif
}

static void incsasr (struct wd_state *wd, int w)
{
	if (wd->sasr == WD_AUXILIARY_STATUS || wd->sasr == WD_DATA || wd->sasr == WD_COMMAND)
		return;
	if (w && wd->sasr == WD_SCSI_STATUS)
		return;
	wd->sasr++;
	wd->sasr &= 0x1f;
}

static void dmac_cint (struct wd_state *wd)
{
	wd->dmac_istr = 0;
	rethink_a2091 ();
}

static void doscsistatus (struct wd_state *wd, uae_u8 status)
{
	wd->wdregs[WD_SCSI_STATUS] = status;
	wd->auxstatus |= ASR_INT;
#if WD33C93_DEBUG > 1
	write_log (_T("%s STATUS=%02X\n"), WD33C93, status);
#endif
	if (!wd->enabled)
		return;
	if (wd->cdtv) {
		cdtv_scsi_int ();
		return;
	}
	dmac_scsi_int(wd);
#if A2091_DEBUG > 2 || A3000_DEBUG > 2
	write_log (_T("Interrupt\n"));
#endif
}

static void set_status (struct wd_state *wd, uae_u8 status, int delay)
{
	wd->queue_index++;
	if (wd->queue_index >= WD_STATUS_QUEUE)
		wd->queue_index = 0;
	wd->scsidelay_status[wd->queue_index] = status;
	wd->scsidelay_irq[wd->queue_index] = delay == 0 ? 1 : (delay <= 2 ? 2 : delay);
}

static void set_status (struct wd_state *wd, uae_u8 status)
{
	set_status (wd, status, 0);
}

static uae_u32 gettc (struct wd_state *wd)
{
	return wd->wdregs[WD_TRANSFER_COUNT_LSB] | (wd->wdregs[WD_TRANSFER_COUNT] << 8) | (wd->wdregs[WD_TRANSFER_COUNT_MSB] << 16);
}
static void settc (struct wd_state *wd, uae_u32 tc)
{
	wd->wdregs[WD_TRANSFER_COUNT_LSB] = tc & 0xff;
	wd->wdregs[WD_TRANSFER_COUNT] = (tc >> 8) & 0xff;
	wd->wdregs[WD_TRANSFER_COUNT_MSB] = (tc >> 16) & 0xff;
}
static bool decreasetc (struct wd_state *wd)
{
	uae_u32 tc = gettc (wd);
	if (!tc)
		return true;
	tc--;
	settc (wd, tc);
	return tc == 0;
}

static bool canwddma (struct wd_state *wd)
{
	uae_u8 mode = wd->wdregs[WD_CONTROL] >> 5;
	if (mode != 0 && mode != 4 && mode != 1) {
		write_log (_T("%s weird DMA mode %d!!\n"), WD33C93, mode);
	}
	return mode == 4 || mode == 1;
}

#if WD33C93_DEBUG > 0
static TCHAR *scsitostring (struct wd_state *wd)
{
	static TCHAR buf[200];
	TCHAR *p;
	int i;

	p = buf;
	p[0] = 0;
	for (i = 0; i < wd->scsi->offset && i < sizeof wd->wd_data; i++) {
		if (i > 0) {
			_tcscat (p, _T("."));
			p++;
		}
		_stprintf (p, _T("%02X"), wd->wd_data[i]);
		p += _tcslen (p);
	}
	return buf;
}
#endif

static void dmacheck (struct wd_state *wd)
{
	wd->dmac_acr++;
	if (wd->old_dmac && (wd->dmac_cntr & CNTR_TCEN)) {
		if (wd->dmac_wtc == 0)
			wd->dmac_istr |= ISTR_E_INT;
		else
			wd->dmac_wtc--;
	}
}

static void setphase (struct wd_state *wd, uae_u8 phase)
{
	wd->wdregs[WD_COMMAND_PHASE] = phase;
}

static bool do_dma (struct wd_state *wd)
{
	wd->wd_data_avail = 0;
	if (wd->cdtv)
		cdtv_getdmadata (&wd->dmac_acr);
	if (wd->scsi->direction == 0) {
		write_log (_T("%s DMA but no data!?\n"), WD33C93);
	} else if (wd->scsi->direction < 0) {
#if WD33C93_DEBUG > 0
		uaecptr odmac_acr = wd->dmac_acr;
#endif
		for (;;) {
			uae_u8 v;
			int status = scsi_receive_data (wd->scsi, &v);
			put_byte (wd->dmac_acr, v);
			if (wd->wd_dataoffset < sizeof wd->wd_data)
				wd->wd_data[wd->wd_dataoffset++] = v;
			dmacheck (wd);
			if (decreasetc (wd))
				break;
			if (status)
				break;
		}
#if WD33C93_DEBUG > 0
		write_log (_T("%s Done DMA from WD, %d/%d %08X\n"), WD33C93, wd->scsi->offset, wd->scsi->data_len, odmac_acr);
#endif
		return true;
	} else if (wd->scsi->direction > 0) {
#if WD33C93_DEBUG > 0
		uaecptr odmac_acr = wd->dmac_acr;
#endif
		for (;;) {
			int status;
			uae_u8 v = get_byte (wd->dmac_acr);
			if (wd->wd_dataoffset < sizeof wd->wd_data)
				wd->wd_data[wd->wd_dataoffset++] = v;
			status = scsi_send_data (wd->scsi, v);
			dmacheck (wd);
			if (decreasetc (wd))
				break;
			if (status)
				break;
		}
#if WD33C93_DEBUG > 0
		write_log (_T("%s Done DMA to WD, %d/%d %08x\n"), WD33C93, wd->scsi->offset, wd->scsi->data_len, odmac_acr);
#endif
		return true;
	}
	return false;
}


static bool wd_do_transfer_out (struct wd_state *wd)
{
#if WD33C93_DEBUG > 0
	write_log (_T("%s SCSI O [%02X] %d/%d TC=%d %s\n"), WD33C93, wd->wdregs[WD_COMMAND_PHASE], wd->scsi->offset, wd->scsi->data_len, gettc (wd), scsitostring (wd));
#endif
	if (wd->wdregs[WD_COMMAND_PHASE] < 0x20) {
		int msg = wd->wd_data[0];
		/* message was sent */
		setphase (wd, 0x20);
		wd->wd_phase = CSR_XFER_DONE | PHS_COMMAND;
		wd->scsi->status = 0;
		scsi_start_transfer (wd->scsi);
#if WD33C93_DEBUG > 0
		write_log (_T("%s SCSI got MESSAGE %02X\n"), WD33C93, msg);
#endif
		wd->scsi->message[0] = msg;
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x30) {
#if WD33C93_DEBUG > 0
		write_log (_T("%s SCSI got COMMAND %02X\n"), WD33C93, wd->wd_data[0]);
#endif
		if (wd->scsi->offset < wd->scsi->data_len) {
			// data missing, ask for more
			wd->wd_phase = CSR_XFER_DONE | PHS_COMMAND;
			setphase (wd, 0x30 + wd->scsi->offset);
			set_status (wd, wd->wd_phase, 1);
			return false;
		}
		settc (wd, 0);
		scsi_start_transfer (wd->scsi);
		scsi_emulate_analyze (wd->scsi);
		if (wd->scsi->direction > 0) {
			/* if write command, need to wait for data */
			if (wd->scsi->data_len <= 0 || wd->scsi->direction == 0) {
				// Status phase if command didn't return anything and don't want anything
				wd->wd_phase = CSR_XFER_DONE | PHS_STATUS;
				setphase (wd, 0x46);
			} else {
				wd->wd_phase = CSR_XFER_DONE | PHS_DATA_OUT;
				setphase (wd, 0x45);
			}
		} else {
			scsi_emulate_cmd (wd->scsi);
			if (wd->scsi->data_len <= 0 || wd->scsi->direction == 0) {
				// Status phase if command didn't return anything and don't want anything
				wd->wd_phase = CSR_XFER_DONE | PHS_STATUS;
				setphase (wd, 0x46);
			} else {
				wd->wd_phase = CSR_XFER_DONE | PHS_DATA_IN;
				setphase (wd, 0x45); // just skip all reselection and message stuff for now..
			}
		}
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x46 || wd->wdregs[WD_COMMAND_PHASE] == 0x45) {
		if (wd->scsi->offset < wd->scsi->data_len) {
			// data missing, ask for more
			wd->wd_phase = CSR_XFER_DONE | (wd->scsi->direction < 0 ? PHS_DATA_IN : PHS_DATA_OUT);
			set_status (wd, wd->wd_phase, 10);
			return false;
		}
		settc (wd, 0);
		if (wd->scsi->direction > 0) {
			/* data was sent */
			scsi_emulate_cmd (wd->scsi);
			wd->scsi->data_len = 0;
			wd->wd_phase = CSR_XFER_DONE | PHS_STATUS;
		}
		scsi_start_transfer (wd->scsi);
		setphase (wd, 0x47);
	}
	wd->wd_dataoffset = 0;
	set_status (wd, wd->wd_phase, wd->scsi->direction <= 0 ? 0 : 1);
	wd->wd_busy = 0;
	return true;
}

static bool wd_do_transfer_in (struct wd_state *wd)
{
#if WD33C93_DEBUG > 0
	write_log (_T("%s SCSI I [%02X] %d/%d TC=%d %s\n"), WD33C93, wd->wdregs[WD_COMMAND_PHASE], wd->scsi->offset, wd->scsi->data_len, gettc (wd), scsitostring (wd));
#endif
	wd->wd_dataoffset = 0;
	if (wd->wdregs[WD_COMMAND_PHASE] >= 0x36 && wd->wdregs[WD_COMMAND_PHASE] < 0x46) {
		if (wd->scsi->offset < wd->scsi->data_len) {
			// data missing, ask for more
			wd->wd_phase = CSR_XFER_DONE | (wd->scsi->direction < 0 ? PHS_DATA_IN : PHS_DATA_OUT);
			set_status (wd, wd->wd_phase, 1);
			return false;
		}
		if (gettc (wd) != 0) {
			wd->wd_phase = CSR_UNEXP | PHS_STATUS;
			setphase (wd, 0x46);
		} else {
			wd->wd_phase = CSR_XFER_DONE | PHS_STATUS;
			setphase (wd, 0x46);
		}
		scsi_start_transfer (wd->scsi);
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x46 || wd->wdregs[WD_COMMAND_PHASE] == 0x47) {
		setphase (wd, 0x50);
		wd->wd_phase = CSR_XFER_DONE | PHS_MESS_IN;
		scsi_start_transfer (wd->scsi);
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x50) {
		setphase (wd, 0x60);
		wd->wd_phase = CSR_DISC;
		wd->wd_selected = false;
		scsi_start_transfer (wd->scsi);
	}
	set_status (wd, wd->wd_phase, 1);
	wd->scsi->direction = 0;
	return true;
}

static void wd_cmd_sel_xfer (struct wd_state *wd, bool atn)
{
	int i, tmp_tc;
	int delay = 0;

	wd->wd_data_avail = 0;
	tmp_tc = gettc (wd);
	wd->scsi = wd->scsis[wd->wdregs[WD_DESTINATION_ID] & 7];
	if (!wd->scsi) {
		set_status (wd, CSR_TIMEOUT, 0);
		wd->wdregs[WD_COMMAND_PHASE] = 0x00;
#if WD33C93_DEBUG > 0
		write_log (_T("* %s select and transfer%s, ID=%d: No device\n"),
			WD33C93, atn ? _T(" with atn") : _T(""), wd->wdregs[WD_DESTINATION_ID] & 0x7);
#endif
		return;
	}
	if (!wd->wd_selected) {
		wd->scsi->message[0] = 0x80;
		wd->wd_selected = true;
		wd->wdregs[WD_COMMAND_PHASE] = 0x10;
	}
#if WD33C93_DEBUG > 0
	write_log (_T("* %s select and transfer%s, ID=%d PHASE=%02X TC=%d wddma=%d dmac=%d\n"),
		WD33C93, atn ? _T(" with atn") : _T(""), wd->wdregs[WD_DESTINATION_ID] & 0x7, wd->wdregs[WD_COMMAND_PHASE], tmp_tc, wd->wdregs[WD_CONTROL] >> 5, wd->dmac_dma);
#endif
	if (wd->wdregs[WD_COMMAND_PHASE] <= 0x30) {
		wd->scsi->buffer[0] = 0;
		wd->scsi->status = 0;
		memcpy (wd->scsi->cmd, &wd->wdregs[3], 16);
		wd->scsi->data_len = tmp_tc;
		scsi_emulate_analyze (wd->scsi);
		settc (wd, wd->scsi->cmd_len);
		wd->wd_dataoffset = 0;
		scsi_start_transfer (wd->scsi);
		wd->scsi->direction = 2;
		wd->scsi->data_len = wd->scsi->cmd_len;
		for (i = 0; i < gettc (wd); i++) {
			uae_u8 b = wd->scsi->cmd[i];
			wd->wd_data[i] = b;
			scsi_send_data (wd->scsi, b);
			wd->wd_dataoffset++;
		}
		// 0x30 = command phase has started
		wd->scsi->data_len = tmp_tc;
		scsi_emulate_analyze (wd->scsi);
		wd->wdregs[WD_COMMAND_PHASE] = 0x30 + gettc (wd);
		settc (wd, 0);
#if WD33C93_DEBUG > 0
		write_log (_T("%s: Got Command %s, datalen=%d\n"), WD33C93, scsitostring (wd), wd->scsi->data_len);
#endif
	}

	if (wd->wdregs[WD_COMMAND_PHASE] <= 0x41) {
		wd->wdregs[WD_COMMAND_PHASE] = 0x44;
#if 0
		if (wd->wdregs[WD_CONTROL] & CTL_IDI) {
			wd->wd_phase = CSR_DISC;
			set_status (wd, wd->wd_phase, delay);
			wd->wd_phase = CSR_RESEL;
			set_status (wd, wd->wd_phase, delay + 10);
			return;
		}
#endif
		wd->wdregs[WD_COMMAND_PHASE] = 0x44;
	}

	// target replied or start/continue data phase (if data available)
	if (wd->wdregs[WD_COMMAND_PHASE] == 0x44) {
		if (wd->scsi->direction <= 0) {
			scsi_emulate_cmd (wd->scsi);
		}
		scsi_start_transfer (wd->scsi);
		wd->wdregs[WD_COMMAND_PHASE] = 0x45;
	}
		
	if (wd->wdregs[WD_COMMAND_PHASE] == 0x45) {
		settc (wd, tmp_tc);
		wd->wd_dataoffset = 0;
		setphase (wd, 0x45);

		if (gettc (wd) == 0) {
			if (wd->scsi->direction != 0) {
				// TC = 0 but we may have data
				if (wd->scsi->direction < 0) {
					if (wd->scsi->data_len == 0) {
						// no data, continue normally to status phase
						setphase (wd, 0x46);
						goto end;
					}
				}
				wd->wd_phase = CSR_UNEXP;
				if (wd->scsi->direction < 0)
					wd->wd_phase |= PHS_DATA_IN;
				else
					wd->wd_phase |= PHS_DATA_OUT;
				set_status (wd, wd->wd_phase, 1);
				return;
			}
		}

		if (wd->scsi->direction) {
			if (canwddma (wd)) {
				if (wd->scsi->direction <= 0) {
					do_dma (wd);
					if (wd->scsi->offset < wd->scsi->data_len) {
						// buffer not completely retrieved?
						wd->wd_phase = CSR_UNEXP | PHS_DATA_IN;
						set_status (wd, wd->wd_phase, 1);
						return;
					}
					if (gettc (wd) > 0) {
						// requested more data than was available.
						wd->wd_phase = CSR_UNEXP | PHS_STATUS;
						set_status (wd, wd->wd_phase, 1);
						return;
					}
					setphase (wd, 0x46);
				} else {
					if (do_dma (wd)) {
						setphase (wd, 0x46);
						if (wd->scsi->offset < wd->scsi->data_len) {
							// not enough data?
							wd->wd_phase = CSR_UNEXP | PHS_DATA_OUT;
							set_status (wd, wd->wd_phase, 1);
							return;
						}
						// got all data -> execute it
						scsi_emulate_cmd (wd->scsi);
					}
				}
			} else {
				// no dma = Service Request
				wd->wd_phase = CSR_SRV_REQ;
				if (wd->scsi->direction < 0)
					wd->wd_phase |= PHS_DATA_IN;
				else
					wd->wd_phase |= PHS_DATA_OUT;
				set_status (wd, wd->wd_phase, 1);
				return;
			}
		} else {
			// TC > 0 but no data to transfer
			if (gettc (wd)) {
				wd->wd_phase = CSR_UNEXP | PHS_STATUS;
				set_status (wd, wd->wd_phase, 1);
				return;
			}
			wd->wdregs[WD_COMMAND_PHASE] = 0x46;
		}
	}

	end:
	if (wd->wdregs[WD_COMMAND_PHASE] == 0x46) {
		wd->scsi->buffer[0] = 0;
		wd->wdregs[WD_COMMAND_PHASE] = 0x50;
		wd->wdregs[WD_TARGET_LUN] = wd->scsi->status;
		wd->scsi->buffer[0] = wd->scsi->status;
	}

	// 0x60 = command complete
	wd->wdregs[WD_COMMAND_PHASE] = 0x60;
	if (!(wd->wdregs[WD_CONTROL] & CTL_EDI)) {
		wd->wd_phase = CSR_SEL_XFER_DONE;
		delay += 2;
		set_status (wd, wd->wd_phase, delay);
		delay += 2;
		wd->wd_phase = CSR_DISC;
		set_status (wd, wd->wd_phase, delay);
	} else {
		delay += 2;
		wd->wd_phase = CSR_SEL_XFER_DONE;
		set_status (wd, wd->wd_phase, delay);
	}
	wd->wd_selected = 0;
}


static void wd_cmd_trans_info (struct wd_state *wd)
{
	if (wd->wdregs[WD_COMMAND_PHASE] == 0x20) {
		wd->wdregs[WD_COMMAND_PHASE] = 0x30;
		wd->scsi->status = 0;
	}
	wd->wd_busy = 1;
	if (wd->wdregs[WD_COMMAND] & 0x80)
		settc (wd, 1);
	if (gettc (wd) == 0)
		settc (wd, 1);
	wd->wd_dataoffset = 0;

	if (wd->wdregs[WD_COMMAND_PHASE] == 0x30) {
		wd->scsi->direction = 2; // command
		wd->scsi->cmd_len = wd->scsi->data_len = gettc (wd);
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x10) {
		wd->scsi->direction = 1; // message
		wd->scsi->data_len = gettc (wd);
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x45) {
		scsi_emulate_analyze (wd->scsi);
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x46 || wd->wdregs[WD_COMMAND_PHASE] == 0x47) {
		wd->scsi->buffer[0] = wd->scsi->status;
		wd->wdregs[WD_TARGET_LUN] = wd->scsi->status;
		wd->scsi->direction = -1; // status
		wd->scsi->data_len = 1;
	} else if (wd->wdregs[WD_COMMAND_PHASE] == 0x50) {
		wd->scsi->direction = -1;
		wd->scsi->data_len = gettc (wd);
	}

	if (canwddma (wd)) {
		wd->wd_data_avail = -1;
	} else {
		wd->wd_data_avail = 1;
	}

#if WD33C93_DEBUG > 0
	write_log (_T("* %s transfer info phase=%02x TC=%d dir=%d data=%d/%d wddma=%d dmac=%d\n"),
		WD33C93, wd->wdregs[WD_COMMAND_PHASE], gettc (wd), wd->scsi->direction, wd->scsi->offset, wd->scsi->data_len, wd->wdregs[WD_CONTROL] >> 5, wd->dmac_dma);
#endif

}

/* Weird stuff, XT driver (which has nothing to do with SCSI or WD33C93) uses this WD33C93 command! */
static void wd_cmd_trans_addr(struct wd_state *wd)
{
	uae_u32 tcyls = (wd->wdregs[WD_T_CYLS_0] << 8) | wd->wdregs[WD_T_CYLS_1];
	uae_u32 theads = wd->wdregs[WD_T_HEADS];
	uae_u32 tsectors = wd->wdregs[WD_T_SECTORS];
	uae_u32 lba = (wd->wdregs[WD_L_ADDR_0] << 24) | (wd->wdregs[WD_L_ADDR_1] << 16) |
		(wd->wdregs[WD_L_ADDR_2] << 8) | (wd->wdregs[WD_L_ADDR_3] << 0);
	uae_u32 cyls, heads, sectors;

	cyls = lba / (theads * tsectors);
	heads = (lba - ((cyls * theads * tsectors))) / tsectors;
	sectors = (lba - ((cyls * theads * tsectors))) % tsectors;

	//write_log(_T("WD TRANS ADDR: LBA=%d TC=%d TH=%d TS=%d -> C=%d H=%d S=%d\n"), lba, tcyls, theads, tsectors, cyls, heads, sectors);

	wd->wdregs[WD_CYL_0] = cyls >> 8;
	wd->wdregs[WD_CYL_1] = cyls;
	wd->wdregs[WD_HEAD] = heads;
	wd->wdregs[WD_SECTOR] = sectors;

	// This is cheating, sector value hardwired on MFM drives. This hack allows to mount hardfiles
	// that are created using incompatible geometry. (XT MFM/RLL drives have real physical geometry)
	if (wd->xt_sectors != tsectors && wd->scsis[XT_UNIT]) {
		write_log(_T("XT drive sector value patched from %d to %d\n"), wd->xt_sectors, tsectors);
		wd->xt_sectors = tsectors;
	}

	if (cyls >= tcyls)
		set_status(wd, CSR_BAD_STATUS);
	else
		set_status(wd, CSR_TRANS_ADDR);
}

static void wd_cmd_sel (struct wd_state *wd, bool atn)
{
#if WD33C93_DEBUG > 0
	write_log (_T("* %s select%s, ID=%d\n"), WD33C93, atn ? _T(" with atn") : _T(""), wd->wdregs[WD_DESTINATION_ID] & 0x7);
#endif
	wd->wd_phase = 0;
	wd->wdregs[WD_COMMAND_PHASE] = 0;

	wd->scsi = wd->scsis[wd->wdregs[WD_DESTINATION_ID] & 7];
	if (!wd->scsi || (wd->wdregs[WD_DESTINATION_ID] & 7) == 7) {
#if WD33C93_DEBUG > 0
		write_log (_T("%s no drive\n"), WD33C93);
#endif
		set_status (wd, CSR_TIMEOUT, 1000);
		return;
	}
	scsi_start_transfer (wd->scsi);
	wd->wd_selected = true;
	wd->scsi->message[0] = 0x80;
	set_status (wd, CSR_SELECT, 2);
	if (atn) {
		wd->wdregs[WD_COMMAND_PHASE] = 0x10;
		set_status (wd, CSR_SRV_REQ | PHS_MESS_OUT, 4);
	} else {
		wd->wdregs[WD_COMMAND_PHASE] = 0x10; // connected as an initiator
		set_status (wd, CSR_SRV_REQ | PHS_COMMAND, 4);
	} 
}

static void wd_cmd_reset (struct wd_state *wd, bool irq)
{
	int i;

#if WD33C93_DEBUG > 0
	if (irq)
		write_log (_T("%s reset\n"), WD33C93);
#endif
	for (i = 1; i < 0x16; i++)
		wd->wdregs[i] = 0;
	wd->wdregs[0x18] = 0;
	wd->sasr = 0;
	wd->wd_selected = false;
	wd->scsi = NULL;
	wd->scsidelay_irq[0] = 0;
	wd->scsidelay_irq[1] = 0;
	wd->auxstatus = 0;
	wd->wd_data_avail = 0;
	if (irq) {
		set_status (wd, (wd->wdregs[0] & 0x08) ? 1 : 0, 50);
	} else {
		wd->dmac_dma = 0;
		wd->dmac_istr = 0;
		wd->dmac_cntr = 0;
	}
}

static void wd_cmd_abort (struct wd_state *wd)
{
#if WD33C93_DEBUG > 0
	write_log (_T("%s abort\n"), WD33C93);
#endif
}

static void xt_command_done(struct wd_state *wd);

static void scsi_hsync2 (struct wd_state *wd)
{
	bool irq = false;
	if (!wd->enabled)
		return;
	if (wd->wd_data_avail < 0 && wd->dmac_dma > 0) {
		bool v;
		do_dma (wd);
		if (wd->scsi->direction < 0) {
			v = wd_do_transfer_in (wd);
		} else if (wd->scsi->direction > 0) {
			v = wd_do_transfer_out (wd);
		} else {
			write_log (_T("%s data transfer attempt without data!\n"), WD33C93);
			v = true;
		}
		if (v) {
			wd->scsi->direction = 0;
			wd->wd_data_avail = 0;
		} else {
			wd->dmac_dma = -1;
		}
	}
	if (wd->dmac_dma > 0 && (wd->xt_status & (XT_STAT_INPUT | XT_STAT_REQUEST))) {
		wd->scsi = wd->scsis[XT_UNIT];
		if (do_dma(wd)) {
			xt_command_done(wd);
		}
	}

	if (wd->auxstatus & ASR_INT)
		return;
	for (int i = 0; i < WD_STATUS_QUEUE; i++) {
		if (wd->scsidelay_irq[i] == 1) {
			wd->scsidelay_irq[i] = 0;
			doscsistatus(wd, wd->scsidelay_status[i]);
			wd->wd_busy = 0;
		} else if (wd->scsidelay_irq[i] > 1) {
			wd->scsidelay_irq[i]--;
		}
	}
}
void scsi_hsync (void)
{
	scsi_hsync2(&wd_a2091);
	scsi_hsync2(&wd_a2091_2);
	scsi_hsync2(&wd_a3000);
	scsi_hsync2(&wd_cdtv);
}


static int writeonlyreg (int reg)
{
	if (reg == WD_SCSI_STATUS)
		return 1;
	return 0;
}

static uae_u32 makecmd (struct scsi_data *s, int msg, uae_u8 cmd)
{
	uae_u32 v = 0;
	if (s)
		v |= s->id << 24;
	v |= msg << 8;
	v |= cmd;
	return v;
}

static void writewdreg (struct wd_state *wd, int sasr, uae_u8 val)
{
	switch (sasr)
	{
	case WD_OWN_ID:
		if (wd->wd33c93_ver == 0)
			val &= ~(0x20 | 0x08);
		else if (wd->wd33c93_ver == 1)
			val &= ~0x20;
		break;
	}
	if (sasr > WD_QUEUE_TAG && sasr < WD_AUXILIARY_STATUS)
		return;
	// queue tag is B revision only
	if (sasr == WD_QUEUE_TAG && wd->wd33c93_ver < 2)
		return;
	wd->wdregs[sasr] = val;
}

void wdscsi_put (struct wd_state *wd, uae_u8 d)
{
#if WD33C93_DEBUG > 1
	if (WD33C93_DEBUG > 3 || sasr != WD_DATA)
		write_log (_T("W %s REG %02X = %02X (%d) PC=%08X\n"), WD33C93, sasr, d, d, M68K_GETPC);
#endif
	if (!writeonlyreg (wd->sasr)) {
		writewdreg (wd, wd->sasr, d);
	}
	if (!wd->wd_used) {
		wd->wd_used = 1;
		write_log (_T("%s %s in use\n"), wd->name, WD33C93);
	}
	if (wd->sasr == WD_COMMAND_PHASE) {
#if WD33C93_DEBUG > 1
		write_log (_T("%s PHASE=%02X\n"), WD33C93, d);
#endif
		;
	} else if (wd->sasr == WD_DATA) {
#if WD33C93_DEBUG_PIO
		write_log (_T("%s WD_DATA WRITE %02x %d/%d\n"), WD33C93, d, wd->scsi->offset, wd->scsi->data_len);
#endif
		if (!wd->wd_data_avail) {
			write_log (_T("%s WD_DATA WRITE without data request!?\n"), WD33C93);
			return;
		}
		if (wd->wd_dataoffset < sizeof wd->wd_data)
			wd->wd_data[wd->wd_dataoffset] = wd->wdregs[wd->sasr];
		wd->wd_dataoffset++;
		decreasetc (wd);
		wd->wd_data_avail = 1;
		if (scsi_send_data (wd->scsi, wd->wdregs[wd->sasr]) || gettc (wd) == 0) {
			wd->wd_data_avail = 0;
			write_comm_pipe_u32 (&wd->requests, makecmd (wd->scsi, 2, 0), 1);
		}
	} else if (wd->sasr == WD_COMMAND) {
		wd->wd_busy = true;
		write_comm_pipe_u32(&wd->requests, makecmd(wd->scsis[wd->wdregs[WD_DESTINATION_ID] & 7], 0, d), 1);
		if (wd->scsi && wd->scsi->cd_emu_unit >= 0)
			gui_flicker_led (LED_CD, wd->scsi->id, 1);
	}
	incsasr (wd, 1);
}

void wdscsi_sasr (struct wd_state *wd, uae_u8 b)
{
	wd->sasr = b;
}
uae_u8 wdscsi_getauxstatus (struct wd_state *wd)
{
	return (wd->auxstatus & ASR_INT) | (wd->wd_busy || wd->wd_data_avail < 0 ? ASR_BSY : 0) | (wd->wd_data_avail != 0 ? ASR_DBR : 0);
}

uae_u8 wdscsi_get (struct wd_state *wd)
{
	uae_u8 v;
#if WD33C93_DEBUG > 1
	uae_u8 osasr = wd->sasr;
#endif

	v = wd->wdregs[wd->sasr];
	if (wd->sasr == WD_DATA) {
		if (!wd->wd_data_avail) {
			write_log (_T("%s WD_DATA READ without data request!?\n"), WD33C93);
			return 0;
		}
		int status = scsi_receive_data (wd->scsi, &v);
#if WD33C93_DEBUG_PIO
		write_log (_T("%s WD_DATA READ %02x %d/%d\n"), WD33C93, v, wd->scsi->offset, wd->scsi->data_len);
#endif
		if (wd->wd_dataoffset < sizeof wd->wd_data)
			wd->wd_data[wd->wd_dataoffset] = v;
		wd->wd_dataoffset++;
		decreasetc (wd);
		wd->wdregs[wd->sasr] = v;
		wd->wd_data_avail = 1;
		if (status || gettc (wd) == 0) {
			wd->wd_data_avail = 0;
			write_comm_pipe_u32 (&wd->requests, makecmd (wd->scsi, 1, 0), 1);
		}
	} else if (wd->sasr == WD_SCSI_STATUS) {
		uae_int_requested &= ~2;
		wd->auxstatus &= ~0x80;
		if (wd->cdtv)
			cdtv_scsi_clear_int ();
		wd->dmac_istr &= ~ISTR_INTS;
#if 0
		if (wd->wdregs[WD_COMMAND_PHASE] == 0x10) {
			wd->wdregs[WD_COMMAND_PHASE] = 0x11;
			wd->wd_phase = CSR_SRV_REQ | PHS_MESS_OUT;
			set_status (wd, wd->wd_phase, 1);
		}
#endif
	} else if (wd->sasr == WD_AUXILIARY_STATUS) {
		v = wdscsi_getauxstatus (wd);
	}
	incsasr (wd, 0);
#if WD33C93_DEBUG > 1
	if (WD33C93_DEBUG > 3 || osasr != WD_DATA)
		write_log (_T("R %s REG %02X = %02X (%d) PC=%08X\n"), WD33C93, osasr, v, v, M68K_GETPC);
#endif
	return v;
}

/* XT */

static void xt_default_geometry(struct wd_state *wd)
{
	wd->xt_cyls = wd->scsi->hfd->cyls > 1023 ? 1023 : wd->scsi->hfd->cyls;
	wd->xt_heads = wd->scsi->hfd->heads > 31 ? 31 : wd->scsi->hfd->heads;
}


static void xt_set_status(struct wd_state *wd, uae_u8 state)
{
	wd->xt_status = state;
	wd->xt_status |= XT_STAT_SELECT;
	wd->xt_status |= XT_STAT_READY;
}

static void xt_reset(struct wd_state *wd)
{
	wd->scsi = wd->scsis[XT_UNIT];
	if (!wd->scsi)
		return;
	wd->xt_control = 0;
	wd->xt_datalen = 0;
	wd->xt_status = 0;
	xt_default_geometry(wd);
	write_log(_T("XT reset\n"));
}

static void xt_command_done(struct wd_state *wd)
{
	switch (wd->xt_cmd[0])
	{
		case XT_CMD_DTCSETPARAM:
			wd->xt_heads = wd->scsi->buffer[2] & 0x1f;
			wd->xt_cyls = ((wd->scsi->buffer[0] & 3) << 8) | (wd->scsi->buffer[1]);
			wd->xt_sectors = XT_SECTORS;
			if (!wd->xt_heads || !wd->xt_cyls)
				xt_default_geometry(wd);
			write_log(_T("XT SETPARAM: cyls=%d heads=%d\n"), wd->xt_cyls, wd->xt_heads);
			break;
		case XT_CMD_WRITE:
			scsi_emulate_cmd(wd->scsi);
			break;

	}

	xt_set_status(wd, XT_STAT_INTERRUPT);
	if (wd->xt_control & XT_INT)
		dmac_xt_int(wd);
	wd->xt_datalen = 0;
	wd->xt_statusbyte = 0;
#if XT_DEBUG > 0
	write_log(_T("XT command %02x done\n"), wd->xt_cmd[0]);
#endif
}

static void xt_wait_data(struct wd_state *wd, int len)
{
	xt_set_status(wd, XT_STAT_REQUEST);
	wd->xt_offset = 0;
	wd->xt_datalen = len;
}

static void xt_sense(struct wd_state *wd)
{
	wd->xt_datalen = 4;
	wd->xt_offset = 0;
	memset(wd->scsi->buffer, 0, wd->xt_datalen);
}

static void xt_readwrite(struct wd_state *wd, int rw)
{
	struct scsi_data *scsi = wd->scsis[XT_UNIT];
	int transfer_len;
	uae_u32 lba;
	// 1 = head
	// 2 = bits 6,7: cyl high, bits 0-5: sectors
	// 3 = cyl (low)
	// 4 = transfer count
	lba = ((wd->xt_cmd[3] | ((wd->xt_cmd[2] << 2) & 0x300))) * (wd->xt_heads * wd->xt_sectors) +
		(wd->xt_cmd[1] & 0x1f) * wd->xt_sectors +
		(wd->xt_cmd[2] & 0x3f);

	wd->scsi = scsi;
	wd->xt_offset = 0;
	transfer_len = wd->xt_cmd[4] == 0 ? 256 : wd->xt_cmd[4];
	wd->xt_datalen = transfer_len * 512;

#if XT_DEBUG > 0
	write_log(_T("XT %s block %d, %d\n"), rw ? _T("WRITE") : _T("READ"), lba, transfer_len);
#endif

	scsi->cmd[0] = rw ? 0x0a : 0x08; /* WRITE(6) / READ (6) */
	scsi->cmd[1] = lba >> 16;
	scsi->cmd[2] = lba >> 8;
	scsi->cmd[3] = lba >> 0;
	scsi->cmd[4] = transfer_len;
	scsi->cmd[5] = 0;
	scsi_emulate_analyze(wd->scsi);
	if (rw) {
		wd->scsi->direction = 1;
		xt_set_status(wd, XT_STAT_REQUEST);
	} else {
		wd->scsi->direction = -1;
		scsi_emulate_cmd(scsi);
		xt_set_status(wd, XT_STAT_INPUT);
	}
	scsi_start_transfer(scsi);
	settc(wd, scsi->data_len);

	if (!(wd->xt_control & XT_DMA_MODE))
		xt_command_done(wd);
}

static void xt_command(struct wd_state *wd)
{
	wd->scsi = wd->scsis[XT_UNIT];
	switch(wd->xt_cmd[0])
	{
	case XT_CMD_READ:
		xt_readwrite(wd, 0);
		break;
	case XT_CMD_WRITE:
		xt_readwrite(wd, 1);
		break;
	case XT_CMD_SEEK:
		xt_command_done(wd);
		break;
	case XT_CMD_VERIFY:
		xt_command_done(wd);
		break;
	case XT_CMD_FORMATBAD:
	case XT_CMD_FORMATTRK:
		xt_command_done(wd);
		break;
	case XT_CMD_TESTREADY:
		xt_command_done(wd);
		break;
	case XT_CMD_RECALIBRATE:
		xt_command_done(wd);
		break;
	case XT_CMD_SENSE:
		xt_sense(wd);
		break;
	case XT_CMD_DTCSETPARAM:
		xt_wait_data(wd, 8);
		break;
	default:
		write_log(_T("XT unknown command %02X\n"), wd->xt_cmd[0]);
		xt_command_done(wd);
		wd->xt_status |= XT_STAT_INPUT;
		wd->xt_datalen = 1;
		wd->xt_statusbyte = XT_CSB_ERROR;
		break;
	}
}

static uae_u8 read_xt_reg(struct wd_state *wd, int reg)
{
	uae_u8 v = 0xff;

	wd->scsi = wd->scsis[XT_UNIT];
	if (!wd->scsi)
		return v;

	switch(reg)
	{
	case XD_DATA:
		if (wd->xt_status & XT_STAT_INPUT) {
			v = wd->scsi->buffer[wd->xt_offset];
			wd->xt_offset++;
			if (wd->xt_offset >= wd->xt_datalen) {
				xt_command_done(wd);
			}
		} else {
			v = wd->xt_statusbyte;
		}
		break;
	case XD_STATUS:
		v = wd->xt_status;
		break;
	case XD_JUMPER:
		// 20M: 0 40M: 2, xt.device checks it.
		v = wd->scsi->hfd->size >= 41615 * 2 * 512 ? 2 : 0;
		break;
	case XD_RESERVED:
		break;
	}
#if XT_DEBUG > 2
	write_log(_T("XT read %d: %02X\n"), reg, v);
#endif
	return v;
}

static void write_xt_reg(struct wd_state *wd, int reg, uae_u8 v)
{
	wd->scsi = wd->scsis[XT_UNIT];
	if (!wd->scsi)
		return;

#if XT_DEBUG > 2
	write_log(_T("XT write %d: %02X\n"), reg, v);
#endif

	switch (reg)
	{
	case XD_DATA:
#if XT_DEBUG > 1
		write_log(_T("XT data write %02X\n"), v);
#endif
		if (!(wd->xt_status & XT_STAT_REQUEST)) {
			wd->xt_offset = 0;
			xt_set_status(wd, XT_STAT_COMMAND | XT_STAT_REQUEST);
		}
		if (wd->xt_status & XT_STAT_REQUEST) {
			if (wd->xt_status & XT_STAT_COMMAND) {
				wd->xt_cmd[wd->xt_offset++] = v;
				xt_set_status(wd, XT_STAT_COMMAND | XT_STAT_REQUEST);
				if (wd->xt_offset == 6) {
					xt_command(wd);
				}
			} else {
				wd->scsi->buffer[wd->xt_offset] = v;
				wd->xt_offset++;
				if (wd->xt_offset >= wd->xt_datalen) {
					xt_command_done(wd);
				}
			}
		}
		break;
	case XD_RESET:
		xt_reset(wd);
		break;
	case XD_SELECT:
#if XT_DEBUG > 1
		write_log(_T("XT select %02X\n"), v);
#endif
		xt_set_status(wd, XT_STAT_SELECT);
		break;
	case XD_CONTROL:
		wd->xt_control = v;
		wd->xt_irq = 0;
		break;
	}
}

/* DMAC */

static uae_u32 dmac_read_word (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v = 0;

	if (addr < 0x40)
		return (wd->dmacmemory[addr] << 8) | wd->dmacmemory[addr + 1];
	if (addr >= ROM_OFFSET) {
		if (wd->rom) {
			int off = addr & wd->rom_mask;
			if (wd->rombankswitcher && (addr & 0xffe0) == ROM_OFFSET)
				wd->rombank = (addr & 0x02) >> 1;
			off += wd->rombank * wd->rom_size;
			return (wd->rom[off] << 8) | wd->rom[off + 1];
		}
		return 0;
	}

	addr &= ~1;
	switch (addr)
	{
	case 0x40:
		v = wd->dmac_istr;
		if (v && (wd->dmac_cntr & CNTR_INTEN))
			v |= ISTR_INT_P;
		wd->dmac_istr &= ~0xf;
		break;
	case 0x42:
		v = wd->dmac_cntr;
		break;
	case 0x80:
		if (wd->old_dmac)
			v = (wd->dmac_wtc >> 16) & 0xffff;
		break;
	case 0x82:
		if (wd->old_dmac)
			v = wd->dmac_wtc & 0xffff;
		break;
	case 0xc0:
		v = 0xf8 | (1 << 0) | (1 << 1) | (1 << 2); // bits 0-2 = dip-switches
		break;
	case 0xc2:
	case 0xc4:
	case 0xc6:
		v = 0xffff;
		break;
	case 0xe0:
		if (wd->dmac_dma <= 0)
			scsi_dmac_start_dma (wd);
		break;
	case 0xe2:
		scsi_dmac_stop_dma (wd);
		break;
	case 0xe4:
		dmac_cint (wd);
		break;
	case 0xe8:
		/* FLUSH (new only) */
		if (!wd->old_dmac && wd->dmac_dma > 0)
			wd->dmac_istr |= ISTR_FE_FLG;
		break;
	}
#if A2091_DEBUG_IO > 0
	write_log (_T("dmac_wget %04X=%04X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 dmac_read_byte (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v = 0;

	if (addr < 0x40)
		return wd->dmacmemory[addr];
	if (addr >= ROM_OFFSET) {
		if (wd->rom) {
			int off = addr & wd->rom_mask;
			if (wd->rombankswitcher && (addr & 0xffe0) == ROM_OFFSET)
				wd->rombank = (addr & 0x02) >> 1;
			off += wd->rombank * wd->rom_size;
			return wd->rom[off];
		}
		return 0;
	}

	switch (addr)
	{
	case 0x91:
		v = wdscsi_getauxstatus (wd);
		break;
	case 0x93:
		v = wdscsi_get (wd);
		break;
	case 0xa1:
	case 0xa3:
	case 0xa5:
	case 0xa7:
		v = read_xt_reg(wd, (addr - 0xa0) / 2);
		break;
	default:
		v = dmac_read_word (wd, addr);
		if (!(addr & 1))
			v >>= 8;
		break;
	}
#if A2091_DEBUG_IO > 0
	write_log (_T("dmac_bget %04X=%02X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static void dmac_write_word (struct wd_state *wd, uaecptr addr, uae_u32 b)
{
	if (addr < 0x40)
		return;
	if (addr >= ROM_OFFSET)
		return;

#if A2091_DEBUG_IO > 0
	write_log (_T("dmac_wput %04X=%04X PC=%08X\n"), addr, b & 65535, M68K_GETPC);
#endif

	addr &= ~1;
	switch (addr)
	{
	case 0x42:
		wd->dmac_cntr = b;
		if (wd->dmac_cntr & CNTR_PREST)
			dmac_reset (wd);
		break;
	case 0x80:
		wd->dmac_wtc &= 0x0000ffff;
		wd->dmac_wtc |= b << 16;
		break;
	case 0x82:
		wd->dmac_wtc &= 0xffff0000;
		wd->dmac_wtc |= b & 0xffff;
		break;
	case 0x84:
		wd->dmac_acr &= 0x0000ffff;
		wd->dmac_acr |= b << 16;
		break;
	case 0x86:
		wd->dmac_acr &= 0xffff0000;
		wd->dmac_acr |= b & 0xfffe;
		if (wd->old_dmac)
			wd->dmac_acr &= ~3;
		break;
	case 0x8e:
		wd->dmac_dawr = b;
		break;
		break;
	case 0xc2:
	case 0xc4:
	case 0xc6:
		break;
	case 0xe0:
		if (wd->dmac_dma <= 0)
			scsi_dmac_start_dma (wd);
		break;
	case 0xe2:
		scsi_dmac_stop_dma (wd);
		break;
	case 0xe4:
		dmac_cint (wd);
		break;
	case 0xe8:
		/* FLUSH */
		wd->dmac_istr |= ISTR_FE_FLG;
		break;
	}
}

static void dmac_write_byte (struct wd_state *wd, uaecptr addr, uae_u32 b)
{
	if (addr < 0x40)
		return;
	if (addr >= ROM_OFFSET)
		return;

#if A2091_DEBUG_IO > 0
	write_log (_T("dmac_bput %04X=%02X PC=%08X\n"), addr, b & 255, M68K_GETPC);
#endif

	switch (addr)
	{
	case 0x91:
		wdscsi_sasr (wd, b);
		break;
	case 0x93:
		wdscsi_put (wd, b);
		break;
	case 0xa1:
	case 0xa3:
	case 0xa5:
	case 0xa7:
		write_xt_reg(wd, (addr - 0xa0) / 2, b);
		break;
	default:
		if (addr & 1)
			dmac_write_word (wd, addr, b);
		else
			dmac_write_word (wd, addr, b << 8);
	}
}

static uae_u32 REGPARAM2 dmac_lget (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_read_word (wd, addr) << 16;
	v |= dmac_read_word (wd, addr + 2) & 0xffff;
	return v;
}

static uae_u32 REGPARAM2 dmac_wget (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_read_word (wd, addr);
	return v;
}

static uae_u32 REGPARAM2 dmac_bget (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_read_byte (wd, addr);
	return v;
}

static void REGPARAM2 dmac_lput (struct wd_state *wd, uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
	dmac_write_word (wd, addr + 0, l >> 16);
	dmac_write_word (wd, addr + 2, l);
}

static void REGPARAM2 dmac_wput (struct wd_state *wd, uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
	dmac_write_word (wd, addr, w);
}

extern addrbank dmaca2091_bank;
extern addrbank dmaca2091_2_bank;

static void REGPARAM2 dmac_bput (struct wd_state *wd, uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (wd->autoconfig) {
		if (addr == 0x48 && !wd->configured) {
			map_banks (wd == &wd_a2091 ? &dmaca2091_bank : &dmaca2091_2_bank, b, 0x10000 >> 16, 0x10000);
			write_log (_T("%s Z2 autoconfigured at %02X0000\n"), wd->name, b);
			wd->configured = 1;
			expamem_next ();
			return;
		}
		if (addr == 0x4c && !wd->configured) {
			write_log (_T("%s DMAC AUTOCONFIG SHUT-UP!\n"), wd->name);
			wd->configured = 1;
			expamem_next ();
			return;
		}
		if (!wd->configured)
			return;
	}
	dmac_write_byte (wd, addr, b);
}

static uae_u32 REGPARAM2 dmac_wgeti (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	if (addr >= ROM_OFFSET)
		v = (wd->rom[addr & wd->rom_mask] << 8) | wd->rom[(addr + 1) & wd->rom_mask];
	else
		write_log(_T("Invalid DMAC instruction access %08x\n"), addr);
	return v;
}
static uae_u32 REGPARAM2 dmac_lgeti (struct wd_state *wd, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_wgeti (wd, addr) << 16;
	v |= dmac_wgeti (wd, addr + 2);
	return v;
}

static int REGPARAM2 dmac_check (struct wd_state *wd, uaecptr addr, uae_u32 size)
{
	return 1;
}

static uae_u8 *REGPARAM2 dmac_xlate (struct wd_state *wd, uaecptr addr)
{
	addr &= 0xffff;
	addr += wd->rombank * wd->rom_size;
	if (addr >= 0x8000)
		addr = 0x8000;
	return wd->rom + addr;
}

static uae_u8 *REGPARAM2 dmac_a2091_xlate (uaecptr addr)
{
	return dmac_xlate(&wd_a2091, addr);
}
static int REGPARAM2 dmac_a2091_check (uaecptr addr, uae_u32 size)
{
	return dmac_check(&wd_a2091, addr, size);
}
static uae_u32 REGPARAM2 dmac_a2091_lgeti (uaecptr addr)
{
	return dmac_lgeti(&wd_a2091, addr);
}
static uae_u32 REGPARAM2 dmac_a2091_wgeti (uaecptr addr)
{
	return dmac_wgeti(&wd_a2091, addr);
}
static uae_u32 REGPARAM2 dmac_a2091_bget (uaecptr addr)
{
	return dmac_bget(&wd_a2091, addr);
}
static uae_u32 REGPARAM2 dmac_a2091_wget (uaecptr addr)
{
	return dmac_wget(&wd_a2091, addr);
}
static uae_u32 REGPARAM2 dmac_a2091_lget (uaecptr addr)
{
	return dmac_lget(&wd_a2091, addr);
}
static void REGPARAM2 dmac_a2091_bput (uaecptr addr, uae_u32 b)
{
	dmac_bput(&wd_a2091, addr, b);
}
static void REGPARAM2 dmac_a2091_wput (uaecptr addr, uae_u32 b)
{
	dmac_wput(&wd_a2091, addr, b);
}
static void REGPARAM2 dmac_a2091_lput (uaecptr addr, uae_u32 b)
{
	dmac_lput(&wd_a2091, addr, b);
}

static uae_u8 *REGPARAM2 dmac_a20912_xlate (uaecptr addr)
{
	return dmac_xlate(&wd_a2091_2, addr);
}
static int REGPARAM2 dmac_a20912_check (uaecptr addr, uae_u32 size)
{
	return dmac_check(&wd_a2091_2, addr, size);
}
static uae_u32 REGPARAM2 dmac_a20912_lgeti (uaecptr addr)
{
	return dmac_lgeti(&wd_a2091_2, addr);
}
static uae_u32 REGPARAM2 dmac_a20912_wgeti (uaecptr addr)
{
	return dmac_wgeti(&wd_a2091_2, addr);
}
static uae_u32 REGPARAM2 dmac_a20912_bget (uaecptr addr)
{
	return dmac_bget(&wd_a2091_2, addr);
}
static uae_u32 REGPARAM2 dmac_a20912_wget (uaecptr addr)
{
	return dmac_wget(&wd_a2091_2, addr);
}
static uae_u32 REGPARAM2 dmac_a20912_lget (uaecptr addr)
{
	return dmac_lget(&wd_a2091_2, addr);
}
static void REGPARAM2 dmac_a20912_bput (uaecptr addr, uae_u32 b)
{
	dmac_bput(&wd_a2091_2, addr, b);
}
static void REGPARAM2 dmac_a20912_wput (uaecptr addr, uae_u32 b)
{
	dmac_wput(&wd_a2091_2, addr, b);
}
static void REGPARAM2 dmac_a20912_lput (uaecptr addr, uae_u32 b)
{
	dmac_lput(&wd_a2091_2, addr, b);
}

addrbank dmaca2091_bank = {
	dmac_a2091_lget, dmac_a2091_wget, dmac_a2091_bget,
	dmac_a2091_lput, dmac_a2091_wput, dmac_a2091_bput,
	dmac_a2091_xlate, dmac_a2091_check, NULL, NULL, _T("A2091/A590"),
	dmac_a2091_lgeti, dmac_a2091_wgeti, ABFLAG_IO | ABFLAG_SAFE
};
addrbank dmaca2091_2_bank = {
	dmac_a20912_lget, dmac_a20912_wget, dmac_a20912_bget,
	dmac_a20912_lput, dmac_a20912_wput, dmac_a20912_bput,
	dmac_a20912_xlate, dmac_a20912_check, NULL, NULL, _T("A2091/A590 #2"),
	dmac_a20912_lgeti, dmac_a20912_wgeti, ABFLAG_IO | ABFLAG_SAFE
};


/* SUPERDMAC */

static void mbdmac_write_word (struct wd_state *wd, uae_u32 addr, uae_u32 val)
{
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_WWRITE %08X=%04X PC=%08X\n"), addr, val & 0xffff, M68K_GETPC);
#endif
	addr &= 0xfffe;
	switch (addr)
	{
	case 0x02:
		wd->dmac_dawr = val;
		break;
	case 0x04:
		wd->dmac_wtc &= 0x0000ffff;
		wd->dmac_wtc |= val << 16;
		break;
	case 0x06:
		wd->dmac_wtc &= 0xffff0000;
		wd->dmac_wtc |= val & 0xffff;
		break;
	case 0x0a:
		wd->dmac_cntr = val;
		if (wd->dmac_cntr & SCNTR_PREST)
			dmac_reset (wd);
		break;
	case 0x0c:
		wd->dmac_acr &= 0x0000ffff;
		wd->dmac_acr |= val << 16;
		break;
	case 0x0e:
		wd->dmac_acr &= 0xffff0000;
		wd->dmac_acr |= val & 0xfffe;
		break;
	case 0x12:
		if (wd->dmac_dma <= 0)
			scsi_dmac_start_dma (wd);
		break;
	case 0x16:
		if (wd->dmac_dma) {
			/* FLUSH */
			wd->dmac_istr |= ISTR_FE_FLG;
			wd->dmac_dma = 0;
		}
		break;
	case 0x1a:
		dmac_cint(wd);
		break;
	case 0x1e:
		/* ISTR */
		break;
	case 0x3e:
		scsi_dmac_stop_dma (wd);
		break;
	}
}

static void mbdmac_write_byte (struct wd_state *wd, uae_u32 addr, uae_u32 val)
{
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_BWRITE %08X=%02X PC=%08X\n"), addr, val & 0xff, M68K_GETPC);
#endif
	addr &= 0xffff;
	switch (addr)
	{

	case 0x41:
		wd->sasr = val;
		break;
	case 0x49:
		wd->sasr = val;
		break;
	case 0x43:
	case 0x47:
		wdscsi_put (wd, val);
		break;
	default:
		if (addr & 1)
			mbdmac_write_word (wd, addr, val);
		else
			mbdmac_write_word (wd, addr, val << 8);
	}
}

static uae_u32 mbdmac_read_word (struct wd_state *wd, uae_u32 addr)
{
#if A3000_DEBUG_IO > 1
	uae_u32 vaddr = addr;
#endif
	uae_u32 v = 0xffffffff;

	addr &= 0xfffe;
	switch (addr)
	{
	case 0x02:
		v = wd->dmac_dawr;
		break;
	case 0x04:
	case 0x06:
		v = 0xffff;
		break;
	case 0x0a:
		v = wd->dmac_cntr;
		break;
	case 0x0c:
		v = wd->dmac_acr >> 16;
		break;
	case 0x0e:
		v = wd->dmac_acr;
		break;
	case 0x12:
		if (wd->dmac_dma <= 0)
			scsi_dmac_start_dma (wd);
		v = 0;
		break;
	case 0x1a:
		dmac_cint (wd);
		v = 0;
		break;;
	case 0x1e:
		v = wd->dmac_istr;
		if (v & ISTR_INTS)
			v |= ISTR_INT_P;
		wd->dmac_istr &= ~15;
		if (!wd->dmac_dma)
			v |= ISTR_FE_FLG;
		break;
	case 0x3e:
		if (wd->dmac_dma) {
			scsi_dmac_stop_dma (wd);
			wd->dmac_istr |= ISTR_FE_FLG;
		}
		v = 0;
		break;
	}
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_WREAD %08X=%04X PC=%X\n"), vaddr, v & 0xffff, M68K_GETPC);
#endif
	return v;
}

static uae_u32 mbdmac_read_byte (struct wd_state *wd, uae_u32 addr)
{
#if A3000_DEBUG_IO > 1
	uae_u32 vaddr = addr;
#endif
	uae_u32 v = 0xffffffff;

	addr &= 0xffff;
	switch (addr)
	{
	case 0x41:
	case 0x49:
		v = wdscsi_getauxstatus (wd);
		break;
	case 0x43:
	case 0x47:
		v = wdscsi_get (wd);
		break;
	default:
		v = mbdmac_read_word (wd, addr);
		if (!(addr & 1))
			v >>= 8;
		break;
	}
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_BREAD %08X=%02X PC=%X\n"), vaddr, v & 0xff, M68K_GETPC);
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
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  mbdmac_read_word (&wd_a3000, addr + 0) << 16;
	v |= mbdmac_read_word (&wd_a3000, addr + 2) << 0;
	return v;
}
static uae_u32 REGPARAM2 mbdmac_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  mbdmac_read_word (&wd_a3000, addr);
	return v;
}
static uae_u32 REGPARAM2 mbdmac_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return mbdmac_read_byte (&wd_a3000, addr);
}
static void REGPARAM2 mbdmac_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if ((addr & 0xffff) == 0x40) {
		// long write to 0x40 = write byte to SASR
		mbdmac_write_byte (&wd_a3000, 0x41, l);
	} else {
		mbdmac_write_word (&wd_a3000, addr + 0, l >> 16);
		mbdmac_write_word (&wd_a3000, addr + 2, l >> 0);
	}
}
static void REGPARAM2 mbdmac_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	mbdmac_write_word (&wd_a3000, addr + 0, w);
}
static void REGPARAM2 mbdmac_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	mbdmac_write_byte (&wd_a3000, addr, b);
}

addrbank mbdmac_a3000_bank = {
	mbdmac_lget, mbdmac_wget, mbdmac_bget,
	mbdmac_lput, mbdmac_wput, mbdmac_bput,
	default_xlate, default_check, NULL, NULL, _T("A3000 DMAC"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

static void ew (struct wd_state *wd, int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		wd->dmacmemory[addr] = (value & 0xf0);
		wd->dmacmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		wd->dmacmemory[addr] = ~(value & 0xf0);
		wd->dmacmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static void *scsi_thread (void *wdv)
{
	struct wd_state *wd = (struct wd_state*)wdv;
	for (;;) {
		uae_u32 v = read_comm_pipe_u32_blocking (&wd->requests);
		if (wd->scsi_thread_running == 0 || v == 0xfffffff)
			break;
		int cmd = v & 0x7f;
		int msg = (v >> 8) & 0xff;
		int unit = (v >> 24) & 0xff;
		wd->scsi = wd->scsis[unit];
		//write_log (_T("scsi_thread got msg=%d cmd=%d\n"), msg, cmd);
		if (msg == 0) {
			if (WD33C93_DEBUG > 0)
				write_log (_T("%s command %02X\n"), WD33C93, cmd);
			switch (cmd)
			{
			case WD_CMD_RESET:
				wd_cmd_reset (wd, true);
				break;
			case WD_CMD_ABORT:
				wd_cmd_abort (wd);
				break;
			case WD_CMD_SEL:
				wd_cmd_sel (wd, false);
				break;
			case WD_CMD_SEL_ATN:
				wd_cmd_sel (wd, true);
				break;
			case WD_CMD_SEL_ATN_XFER:
				wd_cmd_sel_xfer (wd, true);
				break;
			case WD_CMD_SEL_XFER:
				wd_cmd_sel_xfer (wd, false);
				break;
			case WD_CMD_TRANS_INFO:
				wd_cmd_trans_info (wd);
				break;
			case WD_CMD_TRANS_ADDR:
				wd_cmd_trans_addr(wd);
				break;
			default:
				wd->wd_busy = false;
				write_log (_T("%s unimplemented/unknown command %02X\n"), WD33C93, cmd);
				set_status (wd, CSR_INVALID, 10);
				break;
			}
		} else if (msg == 1) {
			wd_do_transfer_in (wd);
		} else if (msg == 2) {
			wd_do_transfer_out (wd);
		}
	}
	wd->scsi_thread_running = -1;
	return 0;
}

void init_scsi (struct wd_state *wd)
{
	wd->configured = 0;
	wd->enabled = true;
	wd->wd_used = 0;
	wd->wd33c93_ver = 1;
	if (wd == &wd_cdtv) {
		wd->cdtv = true;
		wd->name = _T("CDTV");
	}
	if (!wd->scsi_thread_running) {
		wd->scsi_thread_running = 1;
		init_comm_pipe (&wd->requests, 100, 1);
		uae_start_thread (_T("scsi"), scsi_thread, wd, NULL);
	}
}

static void freescsi (struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close (sd->hfd);
	scsi_free (sd);
}

int add_wd_scsi_hd (struct wd_state *wd, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level)
{
	freescsi (wd->scsis[ch]);
	wd->scsis[ch] = NULL;
	if (!hfd) {
		hfd = xcalloc (struct hd_hardfiledata, 1);
		memcpy (&hfd->hfd.ci, ci, sizeof (struct uaedev_config_info));
	}
	if (!hdf_hd_open (hfd))
		return 0;
	hfd->ansi_version = scsi_level;
	wd->scsis[ch] = scsi_alloc_hd (ch, hfd);
	return wd->scsis[ch] ? 1 : 0;
}

int add_wd_scsi_cd (struct wd_state *wd, int ch, int unitnum)
{
	device_func_init (0);
	freescsi (wd->scsis[ch]);
	wd->scsis[ch] = scsi_alloc_cd (ch, unitnum, false);
	return wd->scsis[ch] ? 1 : 0;
}

int add_wd_scsi_tape (struct wd_state *wd, int ch, const TCHAR *tape_directory, bool readonly)
{
	freescsi (wd->scsis[ch]);
	wd->scsis[ch] = scsi_alloc_tape (ch, tape_directory, readonly);
	return wd->scsis[ch] ? 1 : 0;
}

static void freenativescsi (struct wd_state *wd)
{
	int i;
	for (i = 0; i < 8; i++) {
		freescsi (wd->scsis[i]);
		wd->scsis[i] = NULL;
	}
}

static void addnativescsi (struct wd_state *wd)
{
	int i, j;
	int devices[MAX_TOTAL_SCSI_DEVICES];
	int types[MAX_TOTAL_SCSI_DEVICES];
	struct device_info dis[MAX_TOTAL_SCSI_DEVICES];

	freenativescsi (wd);
	i = 0;
	while (i < MAX_TOTAL_SCSI_DEVICES) {
		types[i] = -1;
		devices[i] = -1;
		if (sys_command_open (i)) {
			if (sys_command_info (i, &dis[i], 0)) {
				devices[i] = i;
				types[i] = 100 - i;
				if (dis[i].type == INQ_ROMD)
					types[i] = 1000 - i;
			}
			sys_command_close (i);
		}
		i++;
	}
	i = 0;
	while (devices[i] >= 0) {
		j = i + 1;
		while (devices[j] >= 0) {
			if (types[i] > types[j]) {
				int tmp = types[i];
				types[i] = types[j];
				types[j] = tmp;
			}
			j++;
		}
		i++;
	}
	i = 0; j = 0;
	while (devices[i] >= 0 && j < 7) {
		if (wd->scsis[j] == NULL) {
			wd->scsis[j] = scsi_alloc_native(j, devices[i]);
			write_log (_T("SCSI: %d:'%s'\n"), j, dis[i].label);
			i++;
		}
		j++;
	}
}

int a3000_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	struct wd_state *wd = &wd_a3000;
	if (ci->type == UAEDEV_CD)
		return add_wd_scsi_cd (wd, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_wd_scsi_tape (wd, ch, ci->rootdir, ci->readonly);
	else
		return add_wd_scsi_hd (wd, ch, NULL, ci, 2);
}

void a3000scsi_reset (void)
{
	struct wd_state *wd = &wd_a3000;
	init_scsi (wd);
	wd->enabled = true;
	wd->configured = -1;
	wd->superdmac = 1;
	map_banks (&mbdmac_a3000_bank, 0xDD, 1, 0);
	wd_cmd_reset (wd, false);
	wd->name = _T("A3000");
}

void a3000scsi_free (void)
{
	struct wd_state *wd = &wd_a3000;
	freenativescsi (wd);
	if (wd->scsi_thread_running > 0) {
		wd->scsi_thread_running = 0;
		write_comm_pipe_u32 (&wd->requests, 0xffffffff, 1);
		while(wd->scsi_thread_running == 0)
			sleep_millis (10);
		wd->scsi_thread_running = 0;
	}
}

int a2091_add_scsi_unit(int ch, struct uaedev_config_info *ci, int devnum)
{
	struct wd_state *wd = wda2091[devnum];

	if (ci->type == UAEDEV_CD)
		return add_wd_scsi_cd(wd, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_wd_scsi_tape(wd, ch, ci->rootdir, ci->readonly);
	else
		return add_wd_scsi_hd(wd, ch, NULL, ci, 1);
}

void a2091_free_device (struct wd_state *wd)
{
	freenativescsi (wd);
	xfree (wd->rom);
	wd->rom = NULL;
}
void a2091_free (void)
{
	a2091_free_device(&wd_a2091);
	a2091_free_device(&wd_a2091_2);
}

static void a2091_reset_device(struct wd_state *wd)
{
	wd->configured = 0;
	wd->wd_used = 0;
	wd->superdmac = 0;
	wd->wd33c93_ver = 1;
	wd->old_dmac = 0;
	if (currprefs.scsi == 2)
		addnativescsi (wd);
	wd_cmd_reset (wd, false);
	if (wd == &wd_a2091)
		wd->name = _T("A2091/A590");
	if (wd == &wd_a2091_2)
		wd->name = _T("A2091/A590 #2");
	xt_reset(wd);
}

void a2091_reset (void)
{
	a2091_reset_device(&wd_a2091);
	a2091_reset_device(&wd_a2091_2);
}

addrbank *a2091_init (int devnum)
{
	struct wd_state *wd = wda2091[devnum];
	int roms[6];
	int slotsize;
	struct romlist *rl;

	if (devnum > 0 && !wd->enabled) {
		expamem_next();
		return NULL;
	}

	init_scsi(wd);
	wd->configured = 0;
	wd->autoconfig = true;
	memset (wd->dmacmemory, 0xff, sizeof wd->dmacmemory);
	ew (wd, 0x00, 0xc0 | 0x01 | 0x10);
	/* A590/A2091 hardware id */
	ew (wd, 0x04, wd->old_dmac ? 0x02 : 0x03);
	/* commodore's manufacturer id */
	ew (wd, 0x10, 0x02);
	ew (wd, 0x14, 0x02);
	/* rom vector */
	ew (wd, 0x28, ROM_VECTOR >> 8);
	ew (wd, 0x2c, ROM_VECTOR);

	ew (wd, 0x18, 0x00); /* ser.no. Byte 0 */
	ew (wd, 0x1c, 0x00); /* ser.no. Byte 1 */
	ew (wd, 0x20, 0x00); /* ser.no. Byte 2 */
	ew (wd, 0x24, 0x00); /* ser.no. Byte 3 */

	roms[0] = 55; // 7.0
	roms[1] = 54; // 6.6
	roms[2] = 53; // 6.0
	roms[3] = 56; // guru
	roms[4] = 87;
	roms[5] = -1;

	wd->rombankswitcher = 0;
	wd->rombank = 0;
	slotsize = 65536;
	wd->rom = xmalloc (uae_u8, slotsize);
	wd->rom_size = 16384;
	wd->rom_mask = wd->rom_size - 1;
	if (_tcscmp (currprefs.a2091romfile, _T(":NOROM"))) {
		struct zfile *z = read_rom_name (devnum && currprefs.a2091romfile2[0] ? currprefs.a2091romfile2 : currprefs.a2091romfile);
		if (!z) {
			rl = getromlistbyids (roms);
			if (rl) {
				z = read_rom (rl->rd);
			}
		}
		if (z) {
			write_log (_T("A590/A2091 BOOT ROM '%s'\n"), zfile_getname (z));
			wd->rom_size = zfile_size (z);
			zfile_fread (wd->rom, wd->rom_size, 1, z);
			zfile_fclose (z);
			if (wd->rom_size == 32768) {
				wd->rombankswitcher = 1;
				for (int i = wd->rom_size - 1; i >= 0; i--) {
					wd->rom[i * 2 + 0] = wd->rom[i];
					wd->rom[i * 2 + 1] = 0xff;
				}
			} else {
				for (int i = 1; i < slotsize / wd->rom_size; i++)
					memcpy (wd->rom + i * wd->rom_size, wd->rom, wd->rom_size);
			}
			wd->rom_mask = wd->rom_size - 1;
		} else {
			romwarning (roms);
		}
	}
	return wd == &wd_a2091 ? &dmaca2091_bank : &dmaca2091_2_bank;
}

uae_u8 *save_scsi_dmac (int wdtype, int *len, uae_u8 *dstptr)
{
	struct wd_state *wd = wdscsi[wdtype];
	uae_u8 *dstbak, *dst;

	if (!wd->enabled)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);

	// model (0=original,1=rev2,2=superdmac)
	save_u32 (currprefs.cs_mbdmac == 1 ? 2 : 1);
	save_u32 (0); // reserved flags
	save_u8 (wd->dmac_istr);
	save_u8 (wd->dmac_cntr);
	save_u32 (wd->dmac_wtc);
	save_u32 (wd->dmac_acr);
	save_u16 (wd->dmac_dawr);
	save_u32 (wd->dmac_dma ? 1 : 0);
	save_u8 (wd->configured);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_scsi_dmac (int wdtype, uae_u8 *src)
{
	struct wd_state *wd = wdscsi[wdtype];
	restore_u32 ();
	restore_u32 ();
	wd->dmac_istr = restore_u8 ();
	wd->dmac_cntr = restore_u8 ();
	wd->dmac_wtc = restore_u32 ();
	wd->dmac_acr = restore_u32 ();
	wd->dmac_dawr = restore_u16 ();
	restore_u32 ();
	wd->configured = restore_u8 ();
	return src;
}

uae_u8 *save_scsi_device (int wdtype, int num, int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	struct scsi_data *s;
	struct wd_state *wd = wdscsi[wdtype];

	if (!wd->enabled)
		return NULL;
	s = wd->scsis[num];
	if (!s)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (num);
	save_u32 (s->device_type); // flags
	switch (s->device_type)
	{
	case UAEDEV_HDF:
	case 0:
		save_u64 (s->hfd->size);
		save_string (s->hfd->hfd.ci.rootdir);
		save_u32 (s->hfd->hfd.ci.blocksize);
		save_u32 (s->hfd->hfd.ci.readonly);
		save_u32 (s->hfd->cyls);
		save_u32 (s->hfd->heads);
		save_u32 (s->hfd->secspertrack);
		save_u64 (s->hfd->hfd.virtual_size);
		save_u32 (s->hfd->hfd.ci.sectors);
		save_u32 (s->hfd->hfd.ci.surfaces);
		save_u32 (s->hfd->hfd.ci.reserved);
		save_u32 (s->hfd->hfd.ci.bootpri);
		save_u32 (s->hfd->ansi_version);
		if (num == 7) {
			save_u16(wd->xt_cyls);
			save_u16(wd->xt_heads);
			save_u16(wd->xt_sectors);
			save_u8(wd->xt_status);
			save_u8(wd->xt_control);
		}
	break;
	case UAEDEV_CD:
		save_u32 (s->cd_emu_unit);
	break;
	case UAEDEV_TAPE:
		save_u32 (s->cd_emu_unit);
		save_u32 (s->tape->blocksize);
		save_u32 (s->tape->wp);
		save_string (s->tape->tape_dir);
	break;
	}
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_scsi_device (int wdtype, uae_u8 *src)
{
	struct wd_state *wd = wdscsi[wdtype];
	int num, num2;
	struct hd_hardfiledata *hfd;
	struct scsi_data *s;
	uae_u64 size;
	uae_u32 flags;
	int blocksize, readonly;
	TCHAR *path;

	num = restore_u32 ();

	flags = restore_u32 ();
	switch (flags & 15)
	{
	case UAEDEV_HDF:
	case 0:
		hfd = xcalloc (struct hd_hardfiledata, 1);
		s = wd->scsis[num] = scsi_alloc_hd (num, hfd);
		size = restore_u64 ();
		path = restore_string ();
		_tcscpy (s->hfd->hfd.ci.rootdir, path);
		blocksize = restore_u32 ();
		readonly = restore_u32 ();
		s->hfd->cyls = restore_u32 ();
		s->hfd->heads = restore_u32 ();
		s->hfd->secspertrack = restore_u32 ();
		s->hfd->hfd.virtual_size = restore_u64 ();
		s->hfd->hfd.ci.sectors = restore_u32 ();
		s->hfd->hfd.ci.surfaces = restore_u32 ();
		s->hfd->hfd.ci.reserved = restore_u32 ();
		s->hfd->hfd.ci.bootpri = restore_u32 ();
		s->hfd->ansi_version = restore_u32 ();
		s->hfd->hfd.ci.blocksize = blocksize;
		if (num == 7) {
			wd->xt_cyls = restore_u16();
			wd->xt_heads = restore_u8();
			wd->xt_sectors = restore_u8();
			wd->xt_status = restore_u8();
			wd->xt_control = restore_u8();
		}
		if (size)
			add_wd_scsi_hd (wd, num, hfd, NULL, s->hfd->ansi_version);
		xfree (path);
	break;
	case UAEDEV_CD:
		num2 = restore_u32 ();
		add_wd_scsi_cd (wd, num, num2);
	break;
	case UAEDEV_TAPE:
		num2 = restore_u32 ();
		blocksize = restore_u32 ();
		readonly = restore_u32 ();
		path = restore_string ();
		add_wd_scsi_tape (wd, num, path, readonly != 0);
		xfree (path);
	break;
	}
	return src;
}
