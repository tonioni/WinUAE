/*
* UAE - The Un*x Amiga Emulator
*
* A590/A2091/A3000/CDTV SCSI expansion (DMAC/SuperDMAC + WD33C93) emulation
*
* Copyright 2007-2013 Toni Wilen
*
*/

#define A2091_DEBUG 0
#define A2091_DEBUG_IO 0
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
#include "a2091.h"
#include "blkdev.h"
#include "gui.h"
#include "zfile.h"
#include "filesys.h"
#include "autoconf.h"
#include "cdtv.h"
#include "savestate.h"
#include "threaddep/thread.h"

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
#define ISTR_INTX	(1<<8)	/* XT/AT Interrupt pending */
#define ISTR_INT_F	(1<<7)	/* Interrupt Follow */
#define ISTR_INTS	(1<<6)	/* SCSI Peripheral Interrupt */
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
#define WD_CDB_2		0x04
#define WD_CDB_3		0x05
#define WD_CDB_4		0x06
#define WD_CDB_5		0x07
#define WD_CDB_6		0x08
#define WD_CDB_7		0x09
#define WD_CDB_8		0x0a
#define WD_CDB_9		0x0b
#define WD_CDB_10		0x0c
#define WD_CDB_11		0x0d
#define WD_CDB_12		0x0e
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

static int configured;
static uae_u8 dmacmemory[100];
static uae_u8 *rom;
static int rombankswitcher, rombank;
static int rom_size, rom_mask;

static int old_dmac = 0;
static uae_u32 dmac_istr, dmac_cntr;
static uae_u32 dmac_dawr;
static uae_u32 dmac_acr;
static uae_u32 dmac_wtc;
static int dmac_dma;
static volatile uae_u8 sasr, scmd, auxstatus;
static volatile int wd_used;
static volatile int wd_phase, wd_next_phase, wd_busy, wd_data_avail;
static volatile bool wd_selected;
static volatile int wd_dataoffset;
static volatile uae_u8 wd_data[32];

static int superdmac;

#define WD_STATUS_QUEUE 2
static volatile int scsidelay_irq[WD_STATUS_QUEUE];
static volatile uae_u8 scsidelay_status[WD_STATUS_QUEUE];
static volatile int queue_index;

static smp_comm_pipe requests;
static volatile int scsi_thread_running;

static int wd33c93_ver = 1; // A

struct scsi_data *scsis[8];
static struct scsi_data *scsi;

uae_u8 wdregs[32];

static int isirq (void)
{
	if (superdmac) {
		if ((dmac_cntr & SCNTR_INTEN) && (dmac_istr & (ISTR_INTS | ISTR_E_INT)))
			return 1;
	} else {
		if ((dmac_cntr & CNTR_INTEN) && (dmac_istr & (ISTR_INTS | ISTR_E_INT)))
			return 1;
	}
	return 0;
}

void rethink_a2091 (void)
{
	if (currprefs.cs_cdtvscsi)
		return;
	if (isirq ()) {
		uae_int_requested |= 2;
#if A2091_DEBUG > 2 || A3000_DEBUG > 2
		write_log (_T("Interrupt_RETHINK\n"));
#endif
	} else {
		uae_int_requested &= ~2;
	}
}

static void INT2 (void)
{
	if (currprefs.cs_cdtvscsi)
		return;
	if (!(auxstatus & ASR_INT))
		return;
	dmac_istr |= ISTR_INTS;
	if (isirq ())
		uae_int_requested |= 2;
}

void scsi_dmac_start_dma (void)
{
#if A3000_DEBUG > 0 || A2091_DEBUG > 0
	write_log (_T("DMAC DMA started, ADDR=%08X, LEN=%08X words\n"), dmac_acr, dmac_wtc);
#endif
	dmac_dma = 1;
}
void scsi_dmac_stop_dma (void)
{
	dmac_dma = 0;
	dmac_istr &= ~ISTR_E_INT;
}

static void dmac_reset (void)
{
#if WD33C93_DEBUG > 0
	if (superdmac)
		write_log (_T("A3000 %s SCSI reset\n"), WD33C93);
	else
		write_log (_T("A2091 %s SCSI reset\n"), WD33C93);
#endif
}

static void incsasr (int w)
{
	if (sasr == WD_AUXILIARY_STATUS || sasr == WD_DATA || sasr == WD_COMMAND)
		return;
	if (w && sasr == WD_SCSI_STATUS)
		return;
	sasr++;
	sasr &= 0x1f;
}

static void dmac_cint (void)
{
	dmac_istr = 0;
	rethink_a2091 ();
}

static void doscsistatus (uae_u8 status)
{
	wdregs[WD_SCSI_STATUS] = status;
	auxstatus |= ASR_INT;
#if WD33C93_DEBUG > 1
	write_log (_T("%s STATUS=%02X\n"), WD33C93, status);
#endif
	if (currprefs.cs_cdtvscsi) {
		cdtv_scsi_int ();
		return;
	}
	if (!currprefs.a2091 && currprefs.cs_mbdmac != 1)
		return;
	INT2();
#if A2091_DEBUG > 2 || A3000_DEBUG > 2
	write_log (_T("Interrupt\n"));
#endif
}

static void set_status (uae_u8 status, int delay)
{
	queue_index++;
	if (queue_index >= WD_STATUS_QUEUE)
		queue_index = 0;
	scsidelay_status[queue_index] = status;
	scsidelay_irq[queue_index] = delay == 0 ? 1 : (delay <= 2 ? 2 : delay);
}

static void set_status (uae_u8 status)
{
	set_status (status, 0);
}

static uae_u32 gettc (void)
{
	return wdregs[WD_TRANSFER_COUNT_LSB] | (wdregs[WD_TRANSFER_COUNT] << 8) | (wdregs[WD_TRANSFER_COUNT_MSB] << 16);
}
static void settc (uae_u32 tc)
{
	wdregs[WD_TRANSFER_COUNT_LSB] = tc & 0xff;
	wdregs[WD_TRANSFER_COUNT] = (tc >> 8) & 0xff;
	wdregs[WD_TRANSFER_COUNT_MSB] = (tc >> 16) & 0xff;
}
static bool decreasetc (void)
{
	uae_u32 tc = gettc ();
	if (!tc)
		return true;
	tc--;
	settc (tc);
	return tc == 0;
}

static bool canwddma (void)
{
	uae_u8 mode = wdregs[WD_CONTROL] >> 5;
	if (mode != 0 && mode != 4 && mode != 1) {
		write_log (_T("%s weird DMA mode %d!!\n"), WD33C93, mode);
	}
	return mode == 4 || mode == 1;
}

static TCHAR *scsitostring (void)
{
	static TCHAR buf[200];
	TCHAR *p;
	int i;

	p = buf;
	p[0] = 0;
	for (i = 0; i < scsi->offset && i < sizeof wd_data; i++) {
		if (i > 0) {
			_tcscat (p, _T("."));
			p++;
		}
		_stprintf (p, _T("%02X"), wd_data[i]);
		p += _tcslen (p);
	}
	return buf;
}

static void dmacheck (void)
{
	dmac_acr++;
	if (old_dmac && (dmac_cntr & CNTR_TCEN)) {
		if (dmac_wtc == 0)
			dmac_istr |= ISTR_E_INT;
		else
			dmac_wtc--;
	}
}

static void setphase (uae_u8 phase)
{
	wdregs[WD_COMMAND_PHASE] = phase;
}

static bool do_dma (void)
{
	wd_data_avail = 0;
	if (currprefs.cs_cdtvscsi)
		cdtv_getdmadata (&dmac_acr);
	if (scsi->direction == 0) {
		write_log (_T("%s DMA but no data!?\n"), WD33C93);
	} else if (scsi->direction < 0) {
		uaecptr odmac_acr = dmac_acr;
		for (;;) {
			uae_u8 v;
			int status = scsi_receive_data (scsi, &v);
			put_byte (dmac_acr, v);
			if (wd_dataoffset < sizeof wd_data)
				wd_data[wd_dataoffset++] = v;
			dmacheck ();
			if (decreasetc ())
				break;
			if (status)
				break;
		}
#if WD33C93_DEBUG > 0
		write_log (_T("%s Done DMA from WD, %d/%d %08X\n"), WD33C93, scsi->offset, scsi->data_len, odmac_acr);
#endif
		return true;
	} else if (scsi->direction > 0) {
		uaecptr odmac_acr = dmac_acr;
		for (;;) {
			int status;
			uae_u8 v = get_byte (dmac_acr);
			if (wd_dataoffset < sizeof wd_data)
				wd_data[wd_dataoffset++] = v;
			status = scsi_send_data (scsi, v);
			dmacheck ();
			if (decreasetc ())
				break;
			if (status)
				break;
		}
#if WD33C93_DEBUG > 0
		write_log (_T("%s Done DMA to WD, %d/%d %08x\n"), WD33C93, scsi->offset, scsi->data_len, odmac_acr);
#endif
		return true;
	}
	return false;
}


static bool wd_do_transfer_out (void)
{
#if WD33C93_DEBUG > 0
	write_log (_T("%s SCSI O [%02X] %d/%d TC=%d %s\n"), WD33C93, wdregs[WD_COMMAND_PHASE], scsi->offset, scsi->data_len, gettc (), scsitostring ());
#endif
	if (wdregs[WD_COMMAND_PHASE] < 0x20) {
		int msg = wd_data[0];
		/* message was sent */
		setphase (0x20);
		wd_phase = CSR_XFER_DONE | PHS_COMMAND;
		scsi->status = 0;
		scsi_start_transfer (scsi);
#if WD33C93_DEBUG > 0
		write_log (_T("%s SCSI got MESSAGE %02X\n"), WD33C93, msg);
#endif
		scsi->message[0] = msg;
	} else if (wdregs[WD_COMMAND_PHASE] == 0x30) {
#if WD33C93_DEBUG > 0
		write_log (_T("%s SCSI got COMMAND %02X\n"), WD33C93, wd_data[0]);
#endif
		if (scsi->offset < scsi->data_len) {
			// data missing, ask for more
			wd_phase = CSR_XFER_DONE | PHS_COMMAND;
			setphase (0x30 + scsi->offset);
			set_status (wd_phase, 1);
			return false;
		}
		settc (0);
		scsi_start_transfer (scsi);
		scsi_emulate_analyze (scsi);
		if (scsi->direction > 0) {
			/* if write command, need to wait for data */
			if (scsi->data_len <= 0 || scsi->direction == 0) {
				// Status phase if command didn't return anything and don't want anything
				wd_phase = CSR_XFER_DONE | PHS_STATUS;
				setphase (0x46);
			} else {
				wd_phase = CSR_XFER_DONE | PHS_DATA_OUT;
				setphase (0x45);
			}
		} else {
			scsi_emulate_cmd (scsi);
			if (scsi->data_len <= 0 || scsi->direction == 0) {
				// Status phase if command didn't return anything and don't want anything
				wd_phase = CSR_XFER_DONE | PHS_STATUS;
				setphase (0x46);
			} else {
				wd_phase = CSR_XFER_DONE | PHS_DATA_IN;
				setphase (0x45); // just skip all reselection and message stuff for now..
			}
		}
	} else if (wdregs[WD_COMMAND_PHASE] == 0x46 || wdregs[WD_COMMAND_PHASE] == 0x45) {
		if (scsi->offset < scsi->data_len) {
			// data missing, ask for more
			wd_phase = CSR_XFER_DONE | (scsi->direction < 0 ? PHS_DATA_IN : PHS_DATA_OUT);
			set_status (wd_phase, 10);
			return false;
		}
		settc (0);
		if (scsi->direction > 0) {
			/* data was sent */
			scsi_emulate_cmd (scsi);
			scsi->data_len = 0;
			wd_phase = CSR_XFER_DONE | PHS_STATUS;
		}
		scsi_start_transfer (scsi);
		setphase (0x47);
	}
	wd_dataoffset = 0;
	set_status (wd_phase, scsi->direction <= 0 ? 0 : 1);
	wd_busy = 0;
	return true;
}

static bool wd_do_transfer_in (void)
{
#if WD33C93_DEBUG > 0
	write_log (_T("%s SCSI I [%02X] %d/%d TC=%d %s\n"), WD33C93, wdregs[WD_COMMAND_PHASE], scsi->offset, scsi->data_len, gettc (), scsitostring ());
#endif
	wd_dataoffset = 0;
	if (wdregs[WD_COMMAND_PHASE] >= 0x36 && wdregs[WD_COMMAND_PHASE] < 0x46) {
		if (scsi->offset < scsi->data_len) {
			// data missing, ask for more
			wd_phase = CSR_XFER_DONE | (scsi->direction < 0 ? PHS_DATA_IN : PHS_DATA_OUT);
			set_status (wd_phase, 1);
			return false;
		}
		if (gettc () != 0) {
			wd_phase = CSR_UNEXP | PHS_STATUS;
			setphase (0x46);
		} else {
			wd_phase = CSR_XFER_DONE | PHS_STATUS;
			setphase (0x46);
		}
		scsi_start_transfer (scsi);
	} else if (wdregs[WD_COMMAND_PHASE] == 0x46 || wdregs[WD_COMMAND_PHASE] == 0x47) {
		setphase (0x50);
		wd_phase = CSR_XFER_DONE | PHS_MESS_IN;
		scsi_start_transfer (scsi);
	} else if (wdregs[WD_COMMAND_PHASE] == 0x50) {
		setphase (0x60);
		wd_phase = CSR_DISC;
		wd_selected = false;
		scsi_start_transfer (scsi);
	}
	set_status (wd_phase, 1);
	scsi->direction = 0;
	return true;
}

static void wd_cmd_sel_xfer (bool atn)
{
	int i, tmp_tc;
	int delay = 0;

	wd_data_avail = 0;
	tmp_tc = gettc ();
	scsi = scsis[wdregs[WD_DESTINATION_ID] & 7];
	if (!scsi) {
		set_status (CSR_TIMEOUT, 0);
		wdregs[WD_COMMAND_PHASE] = 0x00;
#if WD33C93_DEBUG > 0
		write_log (_T("* %s select and transfer%s, ID=%d: No device\n"),
		WD33C93, atn ? _T(" with atn") : _T(""), wdregs[WD_DESTINATION_ID] & 0x7);
#endif
		return;
	}
	if (!wd_selected) {
		scsi->message[0] = 0x80;
		wd_selected = true;
		wdregs[WD_COMMAND_PHASE] = 0x10;
	}
#if WD33C93_DEBUG > 0
	write_log (_T("* %s select and transfer%s, ID=%d PHASE=%02X TC=%d wddma=%d dmac=%d\n"),
		WD33C93, atn ? _T(" with atn") : _T(""), wdregs[WD_DESTINATION_ID] & 0x7, wdregs[WD_COMMAND_PHASE], tmp_tc, wdregs[WD_CONTROL] >> 5, dmac_dma);
#endif
	if (wdregs[WD_COMMAND_PHASE] <= 0x30) {
		scsi->buffer[0] = 0;
		scsi->status = 0;
		memcpy (scsi->cmd, &wdregs[3], 16);
		scsi->data_len = tmp_tc;
		scsi_emulate_analyze (scsi);
		settc (scsi->cmd_len);
		wd_dataoffset = 0;
		scsi_start_transfer (scsi);
		scsi->direction = 2;
		scsi->data_len = scsi->cmd_len;
		for (i = 0; i < gettc (); i++) {
			uae_u8 b = scsi->cmd[i];
			wd_data[i] = b;
			scsi_send_data (scsi, b);
			wd_dataoffset++;
		}
		// 0x30 = command phase has started
		scsi->data_len = tmp_tc;
		scsi_emulate_analyze (scsi);
		wdregs[WD_COMMAND_PHASE] = 0x30 + gettc ();
		settc (0);
#if WD33C93_DEBUG > 0
		write_log (_T("%s: Got Command %s, datalen=%d\n"), WD33C93, scsitostring (), scsi->data_len);
#endif
	}

	if (wdregs[WD_COMMAND_PHASE] <= 0x41) {
		wdregs[WD_COMMAND_PHASE] = 0x44;
#if 0
		if (wdregs[WD_CONTROL] & CTL_IDI) {
			wd_phase = CSR_DISC;
			set_status (wd_phase, delay);
			wd_phase = CSR_RESEL;
			set_status (wd_phase, delay + 10);
			return;
		}
#endif
		wdregs[WD_COMMAND_PHASE] = 0x44;
	}

	// target replied or start/continue data phase (if data available)
	if (wdregs[WD_COMMAND_PHASE] == 0x44) {
		if (scsi->direction <= 0) {
			scsi_emulate_cmd (scsi);
		}
		scsi_start_transfer (scsi);
		wdregs[WD_COMMAND_PHASE] = 0x45;
	}
		
	if (wdregs[WD_COMMAND_PHASE] == 0x45) {
		settc (tmp_tc);
		wd_dataoffset = 0;
		setphase (0x45);

		if (gettc () == 0) {
			if (scsi->direction != 0) {
				// TC = 0 but we may have data
				if (scsi->direction < 0) {
					if (scsi->data_len == 0) {
						// no data, continue normally to status phase
						setphase (0x46);
						goto end;
					}
				}
				wd_phase = CSR_UNEXP;
				if (scsi->direction < 0)
					wd_phase |= PHS_DATA_IN;
				else
					wd_phase |= PHS_DATA_OUT;
				set_status (wd_phase, 1);
				return;
			}
		}

		if (scsi->direction) {
			if (canwddma ()) {
				if (scsi->direction <= 0) {
					do_dma ();
					if (scsi->offset < scsi->data_len) {
						// buffer not completely retrieved?
						wd_phase = CSR_UNEXP | PHS_DATA_IN;
						set_status (wd_phase, 1);
						return;
					}
					if (gettc () > 0) {
						// requested more data than was available.
						wd_phase = CSR_UNEXP | PHS_STATUS;
						set_status (wd_phase, 1);
						return;
					}
					setphase (0x46);
				} else {
					if (do_dma ()) {
						setphase (0x46);
						if (scsi->offset < scsi->data_len) {
							// not enough data?
							wd_phase = CSR_UNEXP | PHS_DATA_OUT;
							set_status (wd_phase, 1);
							return;
						}
						// got all data -> execute it
						scsi_emulate_cmd (scsi);
					}
				}
			} else {
				// no dma = Service Request
				wd_phase = CSR_SRV_REQ;
				if (scsi->direction < 0)
					wd_phase |= PHS_DATA_IN;
				else
					wd_phase |= PHS_DATA_OUT;
				set_status (wd_phase, 1);
				return;
			}
		} else {
			// TC > 0 but no data to transfer
			if (gettc ()) {
				wd_phase = CSR_UNEXP | PHS_STATUS;
				set_status (wd_phase, 1);
				return;
			}
			wdregs[WD_COMMAND_PHASE] = 0x46;
		}
	}

	end:
	if (wdregs[WD_COMMAND_PHASE] == 0x46) {
		scsi->buffer[0] = 0;
		wdregs[WD_COMMAND_PHASE] = 0x50;
		wdregs[WD_TARGET_LUN] = scsi->status;
		scsi->buffer[0] = scsi->status;
	}

	// 0x60 = command complete
	wdregs[WD_COMMAND_PHASE] = 0x60;
	if (!(wdregs[WD_CONTROL] & CTL_EDI)) {
		wd_phase = CSR_SEL_XFER_DONE;
		delay += 2;
		set_status (wd_phase, delay);
		delay += 2;
		wd_phase = CSR_DISC;
		set_status (wd_phase, delay);
	} else {
		delay += 2;
		wd_phase = CSR_SEL_XFER_DONE;
		set_status (wd_phase, delay);
	}
	wd_selected = 0;
}


static void wd_cmd_trans_info (void)
{
	if (wdregs[WD_COMMAND_PHASE] == 0x20) {
		wdregs[WD_COMMAND_PHASE] = 0x30;
		scsi->status = 0;
	}
	wd_busy = 1;
	if (wdregs[WD_COMMAND] & 0x80)
		settc (1);
	if (gettc () == 0)
		settc (1);
	wd_dataoffset = 0;


//	if (wdregs[WD_COMMAND_PHASE] >= 0x36 && wdregs[WD_COMMAND_PHASE] <= 0x3f) {
//		wdregs[WD_COMMAND_PHASE] = 0x45;
//	} else if (wdregs[WD_COMMAND_PHASE] == 0x41) {
//		wdregs[WD_COMMAND_PHASE] = 0x46;
//	}

#if 0
	if (wdregs[WD_COMMAND_PHASE] >= 0x40 && scsi->direction < 0) {
		if (wd_tc > scsi->data_len) {
			wd_tc = scsi->data_len;
			if (wd_tc < 0)
				wd_tc = 0;
		}
	}
#endif
	if (wdregs[WD_COMMAND_PHASE] == 0x30) {
		scsi->direction = 2; // command
		scsi->cmd_len = scsi->data_len = gettc ();
	} else if (wdregs[WD_COMMAND_PHASE] == 0x10) {
		scsi->direction = 1; // message
		scsi->data_len = gettc ();
	} else if (wdregs[WD_COMMAND_PHASE] == 0x45) {
		scsi_emulate_analyze (scsi);
	} else if (wdregs[WD_COMMAND_PHASE] == 0x46 || wdregs[WD_COMMAND_PHASE] == 0x47) {
		scsi->buffer[0] = scsi->status;
		wdregs[WD_TARGET_LUN] = scsi->status;
		scsi->direction = -1; // status
		scsi->data_len = 1;
	} else if (wdregs[WD_COMMAND_PHASE] == 0x50) {
		scsi->direction = -1;
		scsi->data_len = gettc ();
	}

	if (canwddma ()) {
		wd_data_avail = -1;
	} else {
		wd_data_avail = 1;
	}

#if WD33C93_DEBUG > 0
	write_log (_T("* %s transfer info phase=%02x TC=%d dir=%d data=%d/%d wddma=%d dmac=%d\n"),
		WD33C93, wdregs[WD_COMMAND_PHASE], gettc (), scsi->direction, scsi->offset, scsi->data_len, wdregs[WD_CONTROL] >> 5, dmac_dma);
#endif

}

static void wd_cmd_sel (bool atn)
{
#if WD33C93_DEBUG > 0
	write_log (_T("* %s select%s, ID=%d\n"), WD33C93, atn ? _T(" with atn") : _T(""), wdregs[WD_DESTINATION_ID] & 0x7);
#endif
	wd_phase = 0;
	wdregs[WD_COMMAND_PHASE] = 0;

	scsi = scsis[wdregs[WD_DESTINATION_ID] & 7];
	if (!scsi) {
#if WD33C93_DEBUG > 0
		write_log (_T("%s no drive\n"), WD33C93);
#endif
		set_status (CSR_TIMEOUT, 1000);
		return;
	}
	scsi_start_transfer (scsi);
	wd_selected = true;
	scsi->message[0] = 0x80;
	set_status (CSR_SELECT, 2);
	if (atn) {
		wdregs[WD_COMMAND_PHASE] = 0x10;
		set_status (CSR_SRV_REQ | PHS_MESS_OUT, 4);
	} else {
		wdregs[WD_COMMAND_PHASE] = 0x10; // connected as an initiator
		set_status (CSR_SRV_REQ | PHS_COMMAND, 4);
	} 
}

static void wd_cmd_reset (bool irq)
{
	int i;

#if WD33C93_DEBUG > 0
	if (irq)
		write_log (_T("%s reset\n"), WD33C93);
#endif
	for (i = 1; i < 0x16; i++)
		wdregs[i] = 0;
	wdregs[0x18] = 0;
	sasr = 0;
	wd_selected = false;
	scsi = NULL;
	scsidelay_irq[0] = 0;
	scsidelay_irq[1] = 0;
	auxstatus = 0;
	wd_data_avail = 0;
	if (irq) {
		set_status ((wdregs[0] & 0x08) ? 1 : 0, 50);
	} else {
		dmac_dma = 0;
		dmac_istr = 0;
		dmac_cntr = 0;
	}
}

static void wd_cmd_abort (void)
{
#if WD33C93_DEBUG > 0
	write_log (_T("%s abort\n"), WD33C93);
#endif
}

void scsi_hsync (void)
{
	if (wd_data_avail < 0 && dmac_dma > 0) {
		bool v;
		do_dma ();
		if (scsi->direction < 0) {
			v = wd_do_transfer_in ();
		} else if (scsi->direction > 0) {
			v = wd_do_transfer_out ();
		} else {
			write_log (_T("%s data transfer attempt without data!\n"), WD33C93);
			v = true;
		}
		if (v) {
			scsi->direction = 0;
			wd_data_avail = 0;
		} else {
			dmac_dma = -1;
		}
	}
	if (auxstatus & ASR_INT)
		return;
	for (int i = 0; i < WD_STATUS_QUEUE; i++) {
		if (scsidelay_irq[i] == 1) {
			scsidelay_irq[i] = 0;
			doscsistatus(scsidelay_status[i]);
			wd_busy = 0;
		} else if (scsidelay_irq[i] > 1) {
			scsidelay_irq[i]--;
		}
	}
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

static void writewdreg (int sasr, uae_u8 val)
{
	switch (sasr)
	{
	case WD_OWN_ID:
		if (wd33c93_ver == 0)
			val &= ~(0x20 | 0x08);
		else if (wd33c93_ver == 1)
			val &= ~0x20;
		break;
	}
	if (sasr > WD_QUEUE_TAG && sasr < WD_AUXILIARY_STATUS)
		return;
	// queue tag is B revision only
	if (sasr == WD_QUEUE_TAG && wd33c93_ver < 2)
		return;
	wdregs[sasr] = val;
}

void wdscsi_put (uae_u8 d)
{
#if WD33C93_DEBUG > 1
	if (WD33C93_DEBUG > 3 || sasr != WD_DATA)
		write_log (_T("W %s REG %02X = %02X (%d) PC=%08X\n"), WD33C93, sasr, d, d, M68K_GETPC);
#endif
	if (!writeonlyreg (sasr)) {
		writewdreg (sasr, d);
	}
	if (!wd_used) {
		wd_used = 1;
		write_log (_T("%s in use\n"), WD33C93);
	}
	if (sasr == WD_COMMAND_PHASE) {
#if WD33C93_DEBUG > 1
		write_log (_T("%s PHASE=%02X\n"), WD33C93, d);
#endif
		;
	} else if (sasr == WD_DATA) {
#if WD33C93_DEBUG_PIO
		write_log (_T("%s WD_DATA WRITE %02x %d/%d\n"), WD33C93, d, scsi->offset, scsi->data_len);
#endif
		if (!wd_data_avail) {
			write_log (_T("%s WD_DATA WRITE without data request!?\n"), WD33C93);
			return;
		}
		if (wd_dataoffset < sizeof wd_data)
			wd_data[wd_dataoffset] = wdregs[sasr];
		wd_dataoffset++;
		decreasetc ();
		wd_data_avail = 1;
		if (scsi_send_data (scsi, wdregs[sasr]) || gettc () == 0) {
			wd_data_avail = 0;
			write_comm_pipe_u32 (&requests, makecmd (scsi, 2, 0), 1);
		}
	} else if (sasr == WD_COMMAND) {
		wd_busy = true;
		write_comm_pipe_u32 (&requests, makecmd (scsi, 0, d), 1);
		if (scsi && scsi->cd_emu_unit >= 0)
			gui_flicker_led (LED_CD, scsi->id, 1);
	}
	incsasr (1);
}

void wdscsi_sasr (uae_u8 b)
{
	sasr = b;
}
uae_u8 wdscsi_getauxstatus (void)
{
	return (auxstatus & ASR_INT) | (wd_busy || wd_data_avail < 0 ? ASR_BSY : 0) | (wd_data_avail != 0 ? ASR_DBR : 0);
}

uae_u8 wdscsi_get (void)
{
	uae_u8 v;
#if WD33C93_DEBUG > 1
	uae_u8 osasr = sasr;
#endif

	v = wdregs[sasr];
	if (sasr == WD_DATA) {
		if (!wd_data_avail) {
			write_log (_T("%s WD_DATA READ without data request!?\n"), WD33C93);
			return 0;
		}
		int status = scsi_receive_data (scsi, &v);
#if WD33C93_DEBUG_PIO
		write_log (_T("%s WD_DATA READ %02x %d/%d\n"), WD33C93, v, scsi->offset, scsi->data_len);
#endif
		if (wd_dataoffset < sizeof wd_data)
			wd_data[wd_dataoffset] = v;
		wd_dataoffset++;
		decreasetc ();
		wdregs[sasr] = v;
		wd_data_avail = 1;
		if (status || gettc () == 0) {
			wd_data_avail = 0;
			write_comm_pipe_u32 (&requests, makecmd (scsi, 1, 0), 1);
		}
	} else if (sasr == WD_SCSI_STATUS) {
		uae_int_requested &= ~2;
		auxstatus &= ~0x80;
		cdtv_scsi_clear_int ();
		dmac_istr &= ~ISTR_INTS;
#if 0
		if (wdregs[WD_COMMAND_PHASE] == 0x10) {
			wdregs[WD_COMMAND_PHASE] = 0x11;
			wd_phase = CSR_SRV_REQ | PHS_MESS_OUT;
			set_status (wd_phase, 1);
		}
#endif
	} else if (sasr == WD_AUXILIARY_STATUS) {
		v = wdscsi_getauxstatus ();
	}
	incsasr (0);
#if WD33C93_DEBUG > 1
	if (WD33C93_DEBUG > 3 || osasr != WD_DATA)
		write_log (_T("R %s REG %02X = %02X (%d) PC=%08X\n"), WD33C93, osasr, v, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 dmac_read_word (uaecptr addr)
{
	uae_u32 v = 0;

	if (addr < 0x40)
		return (dmacmemory[addr] << 8) | dmacmemory[addr + 1];
	if (addr >= ROM_OFFSET) {
		if (rom) {
			int off = addr & rom_mask;
			if (rombankswitcher && (addr & 0xffe0) == ROM_OFFSET)
				rombank = (addr & 0x02) >> 1;
			off += rombank * rom_size;
			return (rom[off] << 8) | rom[off + 1];
		}
		return 0;
	}

	addr &= ~1;
	switch (addr)
	{
	case 0x40:
		v = dmac_istr;
		if (v)
			v |= ISTR_INT_P;
		dmac_istr &= ~0xf;
		break;
	case 0x42:
		v = dmac_cntr;
		break;
	case 0x80:
		if (old_dmac)
			v = (dmac_wtc >> 16) & 0xffff;
		break;
	case 0x82:
		if (old_dmac)
			v = dmac_wtc & 0xffff;
		break;
	case 0xc0:
		v = 0xf8 | (1 << 0) | (1 << 1) | (1 << 2); // bits 0-2 = dip-switches
		break;
		/* XT IO */
	case 0xa0:
	case 0xa2:
	case 0xa4:
	case 0xa6:
	case 0xc2:
	case 0xc4:
	case 0xc6:
		v = 0xffff;
		break;
	case 0xe0:
		if (dmac_dma <= 0)
			scsi_dmac_start_dma ();
		break;
	case 0xe2:
		scsi_dmac_stop_dma ();
		break;
	case 0xe4:
		dmac_cint ();
		break;
	case 0xe8:
		/* FLUSH (new only) */
		if (!old_dmac && dmac_dma > 0)
			dmac_istr |= ISTR_FE_FLG;
		break;
	}
#if A2091_DEBUG_IO > 0
	write_log (_T("dmac_wget %04X=%04X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 dmac_read_byte (uaecptr addr)
{
	uae_u32 v = 0;

	if (addr < 0x40)
		return dmacmemory[addr];
	if (addr >= ROM_OFFSET) {
		if (rom) {
			int off = addr & rom_mask;
			if (rombankswitcher && (addr & 0xffe0) == ROM_OFFSET)
				rombank = (addr & 0x02) >> 1;
			off += rombank * rom_size;
			return rom[off];
		}
		return 0;
	}

	switch (addr)
	{
	case 0x91:
		v = wdscsi_getauxstatus ();
		break;
	case 0x93:
		v = wdscsi_get ();
		break;
	default:
		v = dmac_read_word (addr);
		if (!(addr & 1))
			v >>= 8;
		break;
	}
#if A2091_DEBUG_IO > 0
	write_log (_T("dmac_bget %04X=%02X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static void dmac_write_word (uaecptr addr, uae_u32 b)
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
		dmac_cntr = b;
		if (dmac_cntr & CNTR_PREST)
			dmac_reset ();
		break;
	case 0x80:
		dmac_wtc &= 0x0000ffff;
		dmac_wtc |= b << 16;
		break;
	case 0x82:
		dmac_wtc &= 0xffff0000;
		dmac_wtc |= b & 0xffff;
		break;
	case 0x84:
		dmac_acr &= 0x0000ffff;
		dmac_acr |= b << 16;
		break;
	case 0x86:
		dmac_acr &= 0xffff0000;
		dmac_acr |= b & 0xfffe;
		if (old_dmac)
			dmac_acr &= ~3;
		break;
	case 0x8e:
		dmac_dawr = b;
		break;
		break;
	case 0xe0:
		if (dmac_dma <= 0)
			scsi_dmac_start_dma ();
		break;
	case 0xe2:
		scsi_dmac_stop_dma ();
		break;
	case 0xe4:
		dmac_cint ();
		break;
	case 0xe8:
		/* FLUSH */
		dmac_istr |= ISTR_FE_FLG;
		break;
	}
}

static void dmac_write_byte (uaecptr addr, uae_u32 b)
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
		wdscsi_sasr (b);
		break;
	case 0x93:
		wdscsi_put (b);
		break;
	default:
		if (addr & 1)
			dmac_write_word (addr, b);
		else
			dmac_write_word (addr, b << 8);
	}
}


static uae_u32 REGPARAM2 dmac_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_read_word (addr) << 16;
	v |= dmac_read_word (addr + 2) & 0xffff;
	return v;
}

static uae_u32 REGPARAM2 dmac_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_read_word (addr);
	return v;
}

static uae_u32 REGPARAM2 dmac_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_read_byte (addr);
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
	dmac_write_word (addr + 0, l >> 16);
	dmac_write_word (addr + 2, l);
}

static void REGPARAM2 dmac_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
	dmac_write_word (addr, w);
}

static void REGPARAM2 dmac_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48 && !configured) {
		map_banks (&dmaca2091_bank, b, 0x10000 >> 16, 0x10000);
		write_log (_T("A590/A2091 Z2 autoconfigured at %02X0000\n"), b);
		configured = 1;
		expamem_next ();
		return;
	}
	if (addr == 0x4c && !configured) {
		write_log (_T("A590/A2091 DMAC AUTOCONFIG SHUT-UP!\n"));
		configured = 1;
		expamem_next ();
		return;
	}
	if (!configured)
		return;
	dmac_write_byte (addr, b);
}

static uae_u32 REGPARAM2 dmac_wgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	if (addr >= ROM_OFFSET)
		v = (rom[addr & rom_mask] << 8) | rom[(addr + 1) & rom_mask];
	return v;
}
static uae_u32 REGPARAM2 dmac_lgeti (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = (dmac_wgeti (addr) << 16) | dmac_wgeti (addr + 2);
	return v;
}

static int REGPARAM2 dmac_check (uaecptr addr, uae_u32 size)
{
	return 1;
}

static uae_u8 *REGPARAM2 dmac_xlate (uaecptr addr)
{
	addr &= 0xffff;
	addr += rombank * rom_size;
	if (addr >= 0x8000)
		addr = 0x8000;
	return rom + addr;
}

addrbank dmaca2091_bank = {
	dmac_lget, dmac_wget, dmac_bget,
	dmac_lput, dmac_wput, dmac_bput,
	dmac_xlate, dmac_check, NULL, _T("A2091/A590"),
	dmac_lgeti, dmac_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

static void mbdmac_write_word (uae_u32 addr, uae_u32 val)
{
	if (currprefs.cs_mbdmac > 1)
		return;
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_WWRITE %08X=%04X PC=%08X\n"), addr, val & 0xffff, M68K_GETPC);
#endif
	addr &= 0xfffe;
	switch (addr)
	{
	case 0x02:
		dmac_dawr = val;
		break;
	case 0x04:
		dmac_wtc &= 0x0000ffff;
		dmac_wtc |= val << 16;
		break;
	case 0x06:
		dmac_wtc &= 0xffff0000;
		dmac_wtc |= val & 0xffff;
		break;
	case 0x0a:
		dmac_cntr = val;
		if (dmac_cntr & SCNTR_PREST)
			dmac_reset ();
		break;
	case 0x0c:
		dmac_acr &= 0x0000ffff;
		dmac_acr |= val << 16;
		break;
	case 0x0e:
		dmac_acr &= 0xffff0000;
		dmac_acr |= val & 0xfffe;
		break;
	case 0x12:
		if (dmac_dma <= 0)
			scsi_dmac_start_dma ();
		break;
	case 0x16:
		if (dmac_dma) {
			/* FLUSH */
			dmac_istr |= ISTR_FE_FLG;
			dmac_dma = 0;
		}
		break;
	case 0x1a:
		dmac_cint();
		break;
	case 0x1e:
		/* ISTR */
		break;
	case 0x3e:
		scsi_dmac_stop_dma ();
		break;
	}
}

static void mbdmac_write_byte (uae_u32 addr, uae_u32 val)
{
	if (currprefs.cs_mbdmac > 1)
		return;
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_BWRITE %08X=%02X PC=%08X\n"), addr, val & 0xff, M68K_GETPC);
#endif
	addr &= 0xffff;
	switch (addr)
	{

	case 0x41:
		sasr = val;
		break;
	case 0x49:
		sasr = val;
		break;
	case 0x43:
	case 0x47:
		wdscsi_put (val);
		break;
	default:
		if (addr & 1)
			mbdmac_write_word (addr, val);
		else
			mbdmac_write_word (addr, val << 8);
	}
}

static uae_u32 mbdmac_read_word (uae_u32 addr)
{
#if A3000_DEBUG_IO > 1
	uae_u32 vaddr = addr;
#endif
	uae_u32 v = 0xffffffff;

	if (currprefs.cs_mbdmac > 1)
		return 0;

	addr &= 0xfffe;
	switch (addr)
	{
	case 0x02:
		v = dmac_dawr;
		break;
	case 0x04:
	case 0x06:
		v = 0xffff;
		break;
	case 0x0a:
		v = dmac_cntr;
		break;
	case 0x0c:
		v = dmac_acr >> 16;
		break;
	case 0x0e:
		v = dmac_acr;
		break;
	case 0x12:
		if (dmac_dma <= 0)
			scsi_dmac_start_dma ();
		v = 0;
		break;
	case 0x1a:
		dmac_cint ();
		v = 0;
		break;;
	case 0x1e:
		v = dmac_istr;
		if (v & ISTR_INTS)
			v |= ISTR_INT_P;
		dmac_istr &= ~15;
		if (!dmac_dma)
			v |= ISTR_FE_FLG;
		break;
	case 0x3e:
		if (dmac_dma) {
			scsi_dmac_stop_dma ();
			dmac_istr |= ISTR_FE_FLG;
		}
		v = 0;
		break;
	}
#if A3000_DEBUG_IO > 1
	write_log (_T("DMAC_WREAD %08X=%04X PC=%X\n"), vaddr, v & 0xffff, M68K_GETPC);
#endif
	return v;
}

static uae_u32 mbdmac_read_byte (uae_u32 addr)
{
#if A3000_DEBUG_IO > 1
	uae_u32 vaddr = addr;
#endif
	uae_u32 v = 0xffffffff;

	if (currprefs.cs_mbdmac > 1)
		return 0;

	addr &= 0xffff;
	switch (addr)
	{
	case 0x41:
	case 0x49:
		v = wdscsi_getauxstatus ();
		break;
	case 0x43:
	case 0x47:
		v = wdscsi_get ();
		break;
	default:
		v = mbdmac_read_word (addr);
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
	v =  mbdmac_read_word (addr + 0) << 16;
	v |= mbdmac_read_word (addr + 2) << 0;
	return v;
}
static uae_u32 REGPARAM2 mbdmac_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v =  mbdmac_read_word (addr);
	return v;
}
static uae_u32 REGPARAM2 mbdmac_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return mbdmac_read_byte (addr);
}
static void REGPARAM2 mbdmac_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if ((addr & 0xffff) == 0x40) {
		// long write to 0x40 = write byte to SASR
		mbdmac_write_byte (0x41, l);
	} else {
		mbdmac_write_word (addr + 0, l >> 16);
		mbdmac_write_word (addr + 2, l >> 0);
	}
}
static void REGPARAM2 mbdmac_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	mbdmac_write_word (addr + 0, w);
}
static void REGPARAM2 mbdmac_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	mbdmac_write_byte (addr, b);
}

addrbank mbdmac_a3000_bank = {
	mbdmac_lget, mbdmac_wget, mbdmac_bget,
	mbdmac_lput, mbdmac_wput, mbdmac_bput,
	default_xlate, default_check, NULL, _T("A3000 DMAC"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
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

static void *scsi_thread (void *null)
{
	for (;;) {
		uae_u32 v = read_comm_pipe_u32_blocking (&requests);
		if (scsi_thread_running == 0 || v == 0xfffffff)
			break;
		int cmd = v & 0x7f;
		int msg = (v >> 8) & 0xff;
		int unit = (v >> 24) & 0xff;
		//write_log (_T("scsi_thread got msg=%d cmd=%d\n"), msg, cmd);
		if (msg == 0) {
			if (WD33C93_DEBUG > 0)
				write_log (_T("%s command %02X\n"), WD33C93, cmd);
			switch (cmd)
			{
			case WD_CMD_RESET:
				wd_cmd_reset (true);
				break;
			case WD_CMD_ABORT:
				wd_cmd_abort ();
				break;
			case WD_CMD_SEL:
				wd_cmd_sel (false);
				break;
			case WD_CMD_SEL_ATN:
				wd_cmd_sel (true);
				break;
			case WD_CMD_SEL_ATN_XFER:
				wd_cmd_sel_xfer (true);
				break;
			case WD_CMD_SEL_XFER:
				wd_cmd_sel_xfer (false);
				break;
			case WD_CMD_TRANS_INFO:
				wd_cmd_trans_info ();
				break;
			default:
				wd_busy = false;
				write_log (_T("%s unimplemented/unknown command %02X\n"), WD33C93, cmd);
				set_status (CSR_INVALID, 10);
				break;
			}
		} else if (msg == 1) {
			wd_do_transfer_in ();
		} else if (msg == 2) {
			wd_do_transfer_out ();
		}
	}
	scsi_thread_running = -1;
	return 0;
}

void init_scsi (void)
{
	if (!scsi_thread_running) {
		scsi_thread_running = 1;
		init_comm_pipe (&requests, 100, 1);
		uae_start_thread (_T("scsi"), scsi_thread, 0, NULL);
	}
}

static void freescsi (struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close (sd->hfd);
	scsi_free (sd);
}

int add_scsi_hd (int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level)
{
	init_scsi ();
	freescsi (scsis[ch]);
	scsis[ch] = NULL;
	if (!hfd) {
		hfd = xcalloc (struct hd_hardfiledata, 1);
		memcpy (&hfd->hfd.ci, ci, sizeof (struct uaedev_config_info));
	}
	if (!hdf_hd_open (hfd))
		return 0;
	hfd->ansi_version = scsi_level;
	scsis[ch] = scsi_alloc_hd (ch, hfd);
	return scsis[ch] ? 1 : 0;
}

int add_scsi_cd (int ch, int unitnum)
{
	init_scsi ();
	device_func_init (0);
	freescsi (scsis[ch]);
	scsis[ch] = scsi_alloc_cd (ch, unitnum, false);
	return scsis[ch] ? 1 : 0;
}

int add_scsi_tape (int ch, const TCHAR *tape_directory, bool readonly)
{
	init_scsi ();
	freescsi (scsis[ch]);
	scsis[ch] = scsi_alloc_tape (ch, tape_directory, readonly);
	return scsis[ch] ? 1 : 0;
}

static void freenativescsi (void)
{
	int i;
	for (i = 0; i < 7; i++) {
		freescsi (scsis[i]);
		scsis[i] = NULL;
	}
}

static void addnativescsi (void)
{
	int i, j;
	int devices[MAX_TOTAL_SCSI_DEVICES];
	int types[MAX_TOTAL_SCSI_DEVICES];
	struct device_info dis[MAX_TOTAL_SCSI_DEVICES];

	freenativescsi ();
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
		if (scsis[j] == NULL) {
			scsis[j] = scsi_alloc_native(j, devices[i]);
			write_log (_T("SCSI: %d:'%s'\n"), j, dis[i].label);
			i++;
		}
		j++;
	}
}

int a3000_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	init_scsi ();
	if (ci->type == UAEDEV_CD)
		return add_scsi_cd (ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_scsi_tape (ch, ci->rootdir, ci->readonly);
	else
		return add_scsi_hd (ch, NULL, ci, 2);
}

void a3000scsi_reset (void)
{
	init_scsi ();
	map_banks (&mbdmac_a3000_bank, 0xDD, 1, 0);
	wd_cmd_reset (false);
}

void a3000scsi_free (void)
{
	freenativescsi ();
	if (scsi_thread_running > 0) {
		scsi_thread_running = 0;
		write_comm_pipe_u32 (&requests, 0xffffffff, 1);
		while(scsi_thread_running == 0)
			sleep_millis (10);
		scsi_thread_running = 0;
	}
}

int a2091_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_scsi_cd (ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_scsi_tape (ch, ci->rootdir, ci->readonly);
	else
		return add_scsi_hd (ch, NULL, ci, 1);
}


void a2091_free (void)
{
	freenativescsi ();
	xfree (rom);
	rom = NULL;
}

void a2091_reset (void)
{
	configured = 0;
	wd_used = 0;
	superdmac = 0;
	superdmac = currprefs.cs_mbdmac ? 1 : 0;
	if (currprefs.scsi == 2)
		addnativescsi ();
	wd_cmd_reset (false);
}

void a2091_init (void)
{
	int roms[6];
	int slotsize;
	struct romlist *rl;

	init_scsi ();
	configured = 0;
	memset (dmacmemory, 0xff, sizeof dmacmemory);
	ew (0x00, 0xc0 | 0x01 | 0x10);
	/* A590/A2091 hardware id */
	ew (0x04, old_dmac ? 0x02 : 0x03);
	/* commodore's manufacturer id */
	ew (0x10, 0x02);
	ew (0x14, 0x02);
	/* rom vector */
	ew (0x28, ROM_VECTOR >> 8);
	ew (0x2c, ROM_VECTOR);

	ew (0x18, 0x00); /* ser.no. Byte 0 */
	ew (0x1c, 0x00); /* ser.no. Byte 1 */
	ew (0x20, 0x00); /* ser.no. Byte 2 */
	ew (0x24, 0x00); /* ser.no. Byte 3 */

	roms[0] = 55; // 7.0
	roms[1] = 54; // 6.6
	roms[2] = 53; // 6.0
	roms[3] = 56; // guru
	roms[4] = 87;
	roms[5] = -1;

	rombankswitcher = 0;
	rombank = 0;
	slotsize = 65536;
	rom = xmalloc (uae_u8, slotsize);
	rom_size = 16384;
	rom_mask = rom_size - 1;
	if (_tcscmp (currprefs.a2091romfile, _T(":NOROM"))) {
		struct zfile *z = read_rom_name (currprefs.a2091romfile);
		if (!z) {
			rl = getromlistbyids (roms);
			if (rl) {
				z = read_rom (&rl->rd);
			}
		}
		if (z) {
			write_log (_T("A590/A2091 BOOT ROM '%s'\n"), zfile_getname (z));
			rom_size = zfile_size (z);
			zfile_fread (rom, rom_size, 1, z);
			zfile_fclose (z);
			if (rom_size == 32768) {
				rombankswitcher = 1;
				for (int i = rom_size - 1; i >= 0; i--) {
					rom[i * 2 + 0] = rom[i];
					rom[i * 2 + 1] = 0xff;
				}
			} else {
				for (int i = 1; i < slotsize / rom_size; i++)
					memcpy (rom + i * rom_size, rom, rom_size);
			}
			rom_mask = rom_size - 1;
		} else {
			romwarning (roms);
		}
	}
	map_banks (&dmaca2091_bank, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
}

uae_u8 *save_scsi_dmac (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	
	if (!currprefs.a2091 && !currprefs.cs_mbdmac)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);

	// model (0=original,1=rev2,2=superdmac)
	save_u32 (currprefs.cs_mbdmac ? 2 : 1);
	save_u32 (0); // reserved flags
	save_u8 (dmac_istr);
	save_u8 (dmac_cntr);
	save_u32 (dmac_wtc);
	save_u32 (dmac_acr);
	save_u16 (dmac_dawr);
	save_u32 (dmac_dma ? 1 : 0);
	save_u8 (configured);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_scsi_dmac (uae_u8 *src)
{
	restore_u32 ();
	restore_u32 ();
	dmac_istr = restore_u8 ();
	dmac_cntr = restore_u8 ();
	dmac_wtc = restore_u32 ();
	dmac_acr = restore_u32 ();
	dmac_dawr = restore_u16 ();
	restore_u32 ();
	configured = restore_u8 ();
	return src;
}

uae_u8 *save_scsi_device (int num, int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	struct scsi_data *s;

	s = scsis[num];
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

uae_u8 *restore_scsi_device (uae_u8 *src)
{
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
		s = scsis[num] = scsi_alloc_hd (num, hfd);
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
		if (size)
			add_scsi_hd (num, hfd, NULL, s->hfd->ansi_version);
		xfree (path);
	break;
	case UAEDEV_CD:
		num2 = restore_u32 ();
		add_scsi_cd (num, num2);
	break;
	case UAEDEV_TAPE:
		num2 = restore_u32 ();
		blocksize = restore_u32 ();
		readonly = restore_u32 ();
		path = restore_string ();
		add_scsi_tape (num, path, readonly != 0);
		xfree (path);
	break;
	}
	return src;
}
