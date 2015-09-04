/*
* UAE - The Un*x Amiga Emulator
*
* uaeserial.device
*
* Copyright 2004/2006 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "traps.h"
#include "autoconf.h"
#include "execlib.h"
#include "native2amiga.h"
#include "uaeserial.h"
#include "serial.h"
#include "execio.h"

#define MAX_TOTAL_DEVICES 8

int log_uaeserial = 0;

#define SDCMD_QUERY	9
#define SDCMD_BREAK	10
#define SDCMD_SETPARAMS	11

#define SerErr_DevBusy	       1
#define SerErr_BaudMismatch    2
#define SerErr_BufErr	       4
#define SerErr_InvParam        5
#define SerErr_LineErr	       6
#define SerErr_ParityErr       9
#define SerErr_TimerErr       11
#define SerErr_BufOverflow    12
#define SerErr_NoDSR	      13
#define SerErr_DetectedBreak  15

#define SERB_XDISABLED	7	/* io_SerFlags xOn-xOff feature disabled bit */
#define SERF_XDISABLED	(1<<7)	/*    "     xOn-xOff feature disabled mask */
#define	SERB_EOFMODE	6	/*    "     EOF mode enabled bit */
#define	SERF_EOFMODE	(1<<6)	/*    "     EOF mode enabled mask */
#define	SERB_SHARED	5	/*    "     non-exclusive access bit */
#define	SERF_SHARED	(1<<5)	/*    "     non-exclusive access mask */
#define SERB_RAD_BOOGIE 4	/*    "     high-speed mode active bit */
#define SERF_RAD_BOOGIE (1<<4)	/*    "     high-speed mode active mask */
#define	SERB_QUEUEDBRK	3	/*    "     queue this Break ioRqst */
#define	SERF_QUEUEDBRK	(1<<3)	/*    "     queue this Break ioRqst */
#define	SERB_7WIRE	2	/*    "     RS232 7-wire protocol */
#define	SERF_7WIRE	(1<<2)	/*    "     RS232 7-wire protocol */
#define	SERB_PARTY_ODD	1	/*    "     parity feature enabled bit */
#define	SERF_PARTY_ODD	(1<<1)	/*    "     parity feature enabled mask */
#define	SERB_PARTY_ON	0	/*    "     parity-enabled bit */
#define	SERF_PARTY_ON	(1<<0)	/*    "     parity-enabled mask */

#define	IO_STATB_XOFFREAD 12	   /* io_Status receive currently xOFF'ed bit */
#define	IO_STATF_XOFFREAD (1<<12)  /*	 "     receive currently xOFF'ed mask */
#define	IO_STATB_XOFFWRITE 11	   /*	 "     transmit currently xOFF'ed bit */
#define	IO_STATF_XOFFWRITE (1<<11) /*	 "     transmit currently xOFF'ed mask */
#define	IO_STATB_READBREAK 10	   /*	 "     break was latest input bit */
#define	IO_STATF_READBREAK (1<<10) /*	 "     break was latest input mask */
#define	IO_STATB_WROTEBREAK 9	   /*	 "     break was latest output bit */
#define	IO_STATF_WROTEBREAK (1<<9) /*	 "     break was latest output mask */
#define	IO_STATB_OVERRUN 8	   /*	 "     status word RBF overrun bit */
#define	IO_STATF_OVERRUN (1<<8)	   /*	 "     status word RBF overrun mask */

#define io_CtlChar	0x30    /* ULONG control char's (order = xON,xOFF,INQ,ACK) */
#define io_RBufLen	0x34	/* ULONG length in bytes of serial port's read buffer */
#define io_ExtFlags	0x38	/* ULONG additional serial flags (see bitdefs below) */
#define io_Baud		0x3c	/* ULONG baud rate requested (true baud) */
#define io_BrkTime	0x40	/* ULONG duration of break signal in MICROseconds */
#define io_TermArray0	0x44	/* ULONG termination character array */
#define io_TermArray1	0x48	/* ULONG termination character array */
#define io_ReadLen	0x4c	/* UBYTE bits per read character (# of bits) */
#define io_WriteLen	0x4d	/* UBYTE bits per write character (# of bits) */
#define io_StopBits	0x4e	/* UBYTE stopbits for read (# of bits) */
#define io_SerFlags	0x4f	/* UBYTE see SerFlags bit definitions below  */
#define io_Status	0x50	/* UWORD */

/* status of serial port, as follows:
*		   BIT	ACTIVE	FUNCTION
*		    0	 ---	reserved
*		    1	 ---	reserved
*		    2	 high	Connected to parallel "select" on the A1000.
*				Connected to both the parallel "select" and
*				serial "ring indicator" pins on the A500
*				& A2000.  Take care when making cables.
*		    3	 low	Data Set Ready
*		    4	 low	Clear To Send
*		    5	 low	Carrier Detect
*		    6	 low	Ready To Send
*		    7	 low	Data Terminal Ready
*		    8	 high	read overrun
*		    9	 high	break sent
*		   10	 high	break received
*		   11	 high	transmit x-OFFed
*		   12	 high	receive x-OFFed
*		13-15		reserved
*/


struct asyncreq {
	struct asyncreq *next;
	uaecptr request;
	int ready;
};

struct devstruct {
	int open;
	int unit;
	int uniq;
	int exclusive;

	struct asyncreq *ar;

	smp_comm_pipe requests;
	int thread_running;
	uae_sem_t sync_sem;

	void *sysdata;
};

static int uniq;
static uae_u32 nscmd_cmd;
static struct devstruct devst[MAX_TOTAL_DEVICES];
static uae_sem_t change_sem, async_sem;

static const TCHAR *getdevname (void)
{
	return _T("uaeserial.device");
}

static void io_log (const TCHAR *msg, uaecptr request)
{
	if (log_uaeserial)
		write_log (_T("%s: %08X %d %08X %d %d io_actual=%d io_error=%d\n"),
		msg, request, get_word (request + 28), get_long (request + 40),
		get_long (request + 36), get_long (request + 44),
		get_long (request + 32), get_byte (request + 31));
}

static struct devstruct *getdevstruct (int uniq)
{
	int i;
	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		if (devst[i].uniq == uniq)
			return &devst[i];
	}
	return 0;
}

static void *dev_thread (void *devs);
static int start_thread (struct devstruct *dev)
{
	init_comm_pipe (&dev->requests, 100, 1);
	uae_sem_init (&dev->sync_sem, 0, 0);
	uae_start_thread (_T("uaeserial"), dev_thread, dev, NULL);
	uae_sem_wait (&dev->sync_sem);
	return dev->thread_running;
}

static void dev_close_3 (struct devstruct *dev)
{
	uaeser_close (dev->sysdata);
	dev->open = 0;
	xfree (dev->sysdata);
	write_comm_pipe_u32 (&dev->requests, 0, 1);
}

static uae_u32 REGPARAM2 dev_close (TrapContext *context)
{
	uae_u32 request = m68k_areg (regs, 1);
	struct devstruct *dev;

	dev = getdevstruct (get_long (request + 24));
	if (!dev)
		return 0;
	if (log_uaeserial)
		write_log (_T("%s:%d close, req=%x\n"), getdevname(), dev->unit, request);
	dev_close_3 (dev);
	put_long (request + 24, 0);
	put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) - 1);
	return 0;
}

static void resetparams (struct devstruct *dev, uaecptr req)
{
	put_long (req + io_CtlChar, 0x00001311);
	put_long (req + io_RBufLen, 1024);
	put_long (req + io_ExtFlags, 0);
	put_long (req + io_Baud, 9600);
	put_long (req + io_BrkTime, 250000);
	put_long (req + io_TermArray0, 0);
	put_long (req + io_TermArray1, 0);
	put_byte (req + io_ReadLen, 8);
	put_byte (req + io_WriteLen, 8);
	put_byte (req + io_StopBits, 1);
	put_byte (req + io_SerFlags, get_byte (req + io_SerFlags) & (SERF_XDISABLED | SERF_SHARED | SERF_7WIRE));
	put_word (req + io_Status, 0);
}

static int setparams (struct devstruct *dev, uaecptr req)
{
	int v;
	int rbuffer, baud, rbits, wbits, sbits, rtscts, parity, xonxoff;

	rbuffer = get_long (req + io_RBufLen);
	v = get_long (req + io_ExtFlags);
	if (v) {
		write_log (_T("UAESER: io_ExtFlags=%08x, not supported\n"), v);
		return 5;
	}
	baud = get_long (req + io_Baud);
	v = get_byte (req + io_SerFlags);
	if (v & SERF_EOFMODE) {
		write_log (_T("UAESER: SERF_EOFMODE not supported\n"));
		return 5;
	}
	xonxoff = (v & SERF_XDISABLED) ? 0 : 1;
	if (xonxoff) {
		xonxoff |= (get_long (req + io_CtlChar) << 8) & 0x00ffff00;
	}
	rtscts = (v & SERF_7WIRE) ? 1 : 0;
	parity = 0;
	if (v & SERF_PARTY_ON)
		parity = (v & SERF_PARTY_ODD) ? 1 : 2;
	rbits = get_byte (req + io_ReadLen);
	wbits = get_byte (req + io_WriteLen);
	sbits = get_byte (req + io_StopBits);
	if ((rbits != 7 && rbits != 8) || (wbits != 7 && wbits != 8) || (sbits != 1 && sbits != 2) || rbits != wbits) {
		write_log (_T("UAESER: Read=%d, Write=%d, Stop=%d, not supported\n"), rbits, wbits, sbits);
		return 5;
	}
	write_log (_T("%s:%d BAUD=%d BUF=%d BITS=%d+%d RTSCTS=%d PAR=%d XO=%06X\n"),
		getdevname(), dev->unit,
		baud, rbuffer, rbits, sbits, rtscts, parity, xonxoff);
	v = uaeser_setparams (dev->sysdata, baud, rbuffer,
		rbits, sbits, rtscts, parity, xonxoff);
	if (v) {
		write_log (_T("->failed\n"));
		return v;
	}
	return 0;
}

static int openfail (uaecptr ioreq, int error)
{
	put_long (ioreq + 20, -1);
	put_byte (ioreq + 31, error);
	return (uae_u32)-1;
}

static uae_u32 REGPARAM2 dev_open (TrapContext *context)
{
	uaecptr ioreq = m68k_areg (regs, 1);
	uae_u32 unit = m68k_dreg (regs, 0);
	uae_u32 flags = m68k_dreg (regs, 1);
	struct devstruct *dev;
	int i, err;

	if (get_word (ioreq + 0x12) < IOSTDREQ_SIZE)
		return openfail (ioreq, IOERR_BADLENGTH);
	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		if (devst[i].open && devst[i].unit == unit && devst[i].exclusive)
			return openfail (ioreq, IOERR_UNITBUSY);
	}
	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		if (!devst[i].open)
			break;
	}
	if (i == MAX_TOTAL_DEVICES)
		return openfail (ioreq, IOERR_OPENFAIL);
	dev = &devst[i];
	dev->sysdata = xcalloc (uae_u8, uaeser_getdatalength ());
	if (!uaeser_open (dev->sysdata, dev, unit)) {
		xfree (dev->sysdata);
		return openfail (ioreq, IOERR_OPENFAIL);
	}
	dev->unit = unit;
	dev->open = 1;
	dev->uniq = ++uniq;
	dev->exclusive = (get_word (ioreq + io_SerFlags) & SERF_SHARED) ? 0 : 1;
	put_long (ioreq + 24, dev->uniq);
	resetparams (dev, ioreq);
	err = setparams (dev, ioreq);
	if (err) {
		uaeser_close (dev->sysdata);
		dev->open = 0;
		xfree (dev->sysdata);
		return openfail (ioreq, err);
	}
	if (log_uaeserial)
		write_log (_T("%s:%d open ioreq=%08X\n"), getdevname(), unit, ioreq);
	start_thread (dev);

	put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) + 1);
	put_byte (ioreq + 31, 0);
	put_byte (ioreq + 8, 7);
	return 0;
}

static uae_u32 REGPARAM2 dev_expunge (TrapContext *context)
{
	return 0;
}

static struct asyncreq *get_async_request (struct devstruct *dev, uaecptr request, int ready)
{
	struct asyncreq *ar;

	uae_sem_wait (&async_sem);
	ar = dev->ar;
	while (ar) {
		if (ar->request == request) {
			if (ready)
				ar->ready = 1;
			break;
		}
		ar = ar->next;
	}
	uae_sem_post (&async_sem);
	return ar;
}

static int add_async_request (struct devstruct *dev, uaecptr request)
{
	struct asyncreq *ar, *ar2;

	if (log_uaeserial)
		write_log (_T("%s:%d async request %x added\n"), getdevname(), dev->unit, request);

	uae_sem_wait (&async_sem);
	ar = xcalloc (struct asyncreq, 1);
	ar->request = request;
	if (!dev->ar) {
		dev->ar = ar;
	} else {
		ar2 = dev->ar;
		while (ar2->next)
			ar2 = ar2->next;
		ar2->next = ar;
	}
	uae_sem_post (&async_sem);
	return 1;
}

static int release_async_request (struct devstruct *dev, uaecptr request)
{
	struct asyncreq *ar, *prevar;

	uae_sem_wait (&async_sem);
	ar = dev->ar;
	prevar = NULL;
	while (ar) {
		if (ar->request == request) {
			if (prevar == NULL)
				dev->ar = ar->next;
			else
				prevar->next = ar->next;
			uae_sem_post (&async_sem);
			xfree (ar);
			if (log_uaeserial)
				write_log (_T("%s:%d async request %x removed\n"), getdevname(), dev->unit, request);
			return 1;
		}
		prevar = ar;
		ar = ar->next;
	}
	uae_sem_post (&async_sem);
	write_log (_T("%s:%d async request %x not found for removal!\n"), getdevname(), dev->unit, request);
	return 0;
}

static void abort_async (struct devstruct *dev, uaecptr request)
{
	struct asyncreq *ar = get_async_request (dev, request, 1);
	if (!ar) {
		write_log (_T("%s:%d: abort async but no request %x found!\n"), getdevname(), dev->unit, request);
		return;
	}
	if (log_uaeserial)
		write_log (_T("%s:%d asyncronous request=%08X aborted\n"), getdevname(), dev->unit, request);
	put_byte (request + 31, IOERR_ABORTED);
	put_byte (request + 30, get_byte (request + 30) | 0x20);
	write_comm_pipe_u32 (&dev->requests, request, 1);
}

static uae_u8 *memmap(uae_u32 addr, uae_u32 len)
{
	addrbank *bank_data = &get_mem_bank (addr);
	if (!bank_data->check (addr, len))
		return NULL;
	return bank_data->xlateaddr (addr);
}

void uaeser_signal (void *vdev, int sigmask)
{
	struct devstruct *dev = (struct devstruct*)vdev;
	struct asyncreq *ar;

	uae_sem_wait (&async_sem);
	ar = dev->ar;
	while (ar) {
		if (!ar->ready) {
			uaecptr request = ar->request;
			uae_u32 io_data = get_long (request + 40); // 0x28
			uae_u32 io_length = get_long (request + 36); // 0x24
			int command = get_word (request + 28);
			uae_u32 io_error = 0, io_actual = 0;
			uae_u8 *addr;
			int io_done = 0;

			switch (command)
			{
			case SDCMD_BREAK:
				if (ar == dev->ar) {
					uaeser_break (dev->sysdata,  get_long (request + io_BrkTime));
					io_done = 1;
				}
				break;
			case CMD_READ:
				if (sigmask & 1) {
					addr = memmap(io_data, io_length);
					if (addr) {
						if (uaeser_read (dev->sysdata, addr, io_length)) {
							io_error = 0;
							io_actual = io_length;
							io_done = 1;
						}
					} else {
						io_error = IOERR_BADADDRESS;
						io_done = 1;
					}
				}
				break;
			case CMD_WRITE:
				if (sigmask & 2) {
					io_error = IOERR_BADADDRESS;
					addr = memmap(io_data, io_length);
					if (addr && uaeser_write (dev->sysdata, addr, io_length))
						io_error = 0;
					io_actual = io_length;
					io_done = 1;
				}
				break;
			default:
				write_log (_T("%s:%d incorrect async request %x (cmd=%d) signaled?!"), getdevname(), dev->unit, request, command);
				break;
			}

			if (io_done) {
				if (log_uaeserial)
					write_log (_T("%s:%d async request %x completed\n"), getdevname(), dev->unit, request);
				put_long (request + 32, io_actual);
				put_byte (request + 31, io_error);
				ar->ready = 1;
				write_comm_pipe_u32 (&dev->requests, request, 1);
			}

		}
		ar = ar->next;
	}
	uae_sem_post (&async_sem);
}

static void cmd_reset(struct devstruct *dev, uaecptr req)
{
	while (dev->ar)
		abort_async (dev, dev->ar->request);
	put_long (req + io_RBufLen, 8192);
	put_long (req + io_ExtFlags, 0);
	put_long (req + io_Baud, 57600);
	put_long (req + io_BrkTime, 250000);
	put_long (req + io_TermArray0, 0);
	put_long (req + io_TermArray1, 0);
	put_long (req + io_ReadLen, 8);
	put_long (req + io_WriteLen, 8);
	put_long (req + io_StopBits, 1);
	put_long (req + io_SerFlags, SERF_XDISABLED);
	put_word (req + io_Status, 0);
}

static int dev_do_io (struct devstruct *dev, uaecptr request, int quick)
{
	uae_u32 command;
	uae_u32 io_data = get_long (request + 40); // 0x28
	uae_u32 io_length = get_long (request + 36); // 0x24
	uae_u32 io_actual = get_long (request + 32); // 0x20
	uae_u32 io_offset = get_long (request + 44); // 0x2c
	uae_u32 io_error = 0;
	uae_u16 io_status;
	int async = 0;

	if (!dev)
		return 0;
	command = get_word (request + 28);
	io_log (_T("dev_io_START"),request);

	switch (command)
	{
	case SDCMD_QUERY:
		if (uaeser_query (dev->sysdata, &io_status, &io_actual))
			put_byte (request + io_Status, io_status);
		else
			io_error = IOERR_BADADDRESS;
		break;
	case SDCMD_SETPARAMS:
		io_error = setparams(dev, request);
		break;
	case CMD_WRITE:
		async = 1;
		break;
	case CMD_READ:
		async = 1;
		break;
	case SDCMD_BREAK:
		if (get_byte (request + io_SerFlags) & SERF_QUEUEDBRK) {
			async = 1;
		} else {
			uaeser_break (dev->sysdata,  get_long (request + io_BrkTime));
		}
		break;
	case CMD_CLEAR:
		uaeser_clearbuffers(dev->sysdata);
		break;
	case CMD_RESET:
		cmd_reset(dev, request);
		break;
	case CMD_FLUSH:
	case CMD_START:
	case CMD_STOP:
		break;
	case NSCMD_DEVICEQUERY:
		put_long (io_data + 0, 0);
		put_long (io_data + 4, 16); /* size */
		put_word (io_data + 8, NSDEVTYPE_SERIAL);
		put_word (io_data + 10, 0);
		put_long (io_data + 12, nscmd_cmd);
		io_actual = 16;
		break;
	default:
		io_error = IOERR_NOCMD;
		break;
	}
	put_long (request + 32, io_actual);
	put_byte (request + 31, io_error);
	io_log (_T("dev_io_END"),request);
	return async;
}

static int dev_canquick (struct devstruct *dev, uaecptr request)
{
	return 0;
}

static uae_u32 REGPARAM2 dev_beginio (TrapContext *context)
{
	uae_u32 request = m68k_areg (regs, 1);
	uae_u8 flags = get_byte (request + 30);
	int command = get_word (request + 28);
	struct devstruct *dev = getdevstruct (get_long (request + 24));

	put_byte (request + 8, NT_MESSAGE);
	if (!dev) {
		put_byte (request + 31, 32);
		return get_byte (request + 31);
	}
	put_byte (request + 31, 0);
	if ((flags & 1) && dev_canquick (dev, request)) {
		if (dev_do_io (dev, request, 1))
			write_log (_T("device %s:%d command %d bug with IO_QUICK\n"), getdevname(), dev->unit, command);
		return get_byte (request + 31);
	} else {
		put_byte (request + 30, get_byte (request + 30) & ~1);
		write_comm_pipe_u32 (&dev->requests, request, 1);
		return 0;
	}
}

static void *dev_thread (void *devs)
{
	struct devstruct *dev = (struct devstruct*)devs;

	uae_set_thread_priority (NULL, 1);
	dev->thread_running = 1;
	uae_sem_post (&dev->sync_sem);
	for (;;) {
		uaecptr request = (uaecptr)read_comm_pipe_u32_blocking (&dev->requests);
		uae_sem_wait (&change_sem);
		if (!request) {
			dev->thread_running = 0;
			uae_sem_post (&dev->sync_sem);
			uae_sem_post (&change_sem);
			return 0;
		} else if (get_async_request (dev, request, 1)) {
			uae_ReplyMsg (request);
			release_async_request (dev, request);
		} else if (dev_do_io (dev, request, 0) == 0) {
			uae_ReplyMsg (request);
		} else {
			add_async_request (dev, request);
			uaeser_trigger (dev->sysdata);
		}
		uae_sem_post (&change_sem);
	}
	return 0;
}

static uae_u32 REGPARAM2 dev_init (TrapContext *context)
{
	uae_u32 base = m68k_dreg (regs, 0);
	if (log_uaeserial)
		write_log (_T("%s init\n"), getdevname ());
	return base;
}

static uae_u32 REGPARAM2 dev_abortio (TrapContext *context)
{
	uae_u32 request = m68k_areg (regs, 1);
	struct devstruct *dev = getdevstruct (get_long (request + 24));

	if (!dev) {
		put_byte (request + 31, 32);
		return get_byte (request + 31);
	}
	abort_async (dev, request);
	return 0;
}

static void dev_reset (void)
{
	int i;
	struct devstruct *dev;

	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		dev = &devst[i];
		if (dev->open) {
			while (dev->ar)
				abort_async (dev, dev->ar->request);
			dev_close_3 (dev);
			uae_sem_wait (&dev->sync_sem);
		}
		memset (dev, 0, sizeof (struct devstruct));
	}
}

static uaecptr ROM_uaeserialdev_resname = 0,
	ROM_uaeserialdev_resid = 0,
	ROM_uaeserialdev_init = 0;

uaecptr uaeserialdev_startup (uaecptr resaddr)
{
	if (!currprefs.uaeserial)
		return resaddr;
	if (log_uaeserial)
		write_log (_T("uaeserialdev_startup(0x%x)\n"), resaddr);
	/* Build a struct Resident. This will set up and initialize
	* the serial.device */
	put_word (resaddr + 0x0, 0x4AFC);
	put_long (resaddr + 0x2, resaddr);
	put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
	put_word (resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
	put_word (resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
	put_long (resaddr + 0xE, ROM_uaeserialdev_resname);
	put_long (resaddr + 0x12, ROM_uaeserialdev_resid);
	put_long (resaddr + 0x16, ROM_uaeserialdev_init);
	resaddr += 0x1A;
	return resaddr;
}


void uaeserialdev_install (void)
{
	uae_u32 functable, datatable;
	uae_u32 initcode, openfunc, closefunc, expungefunc;
	uae_u32 beginiofunc, abortiofunc;

	if (!currprefs.uaeserial)
		return;

	ROM_uaeserialdev_resname = ds (_T("uaeserial.device"));
	ROM_uaeserialdev_resid = ds (_T("UAE serial.device 0.1"));

	/* initcode */
	initcode = here ();
	calltrap (deftrap (dev_init)); dw (RTS);

	/* Open */
	openfunc = here ();
	calltrap (deftrap (dev_open)); dw (RTS);

	/* Close */
	closefunc = here ();
	calltrap (deftrap (dev_close)); dw (RTS);

	/* Expunge */
	expungefunc = here ();
	calltrap (deftrap (dev_expunge)); dw (RTS);

	/* BeginIO */
	beginiofunc = here ();
	calltrap (deftrap (dev_beginio)); dw (RTS);

	/* AbortIO */
	abortiofunc = here ();
	calltrap (deftrap (dev_abortio)); dw (RTS);

	/* FuncTable */
	functable = here ();
	dl (openfunc); /* Open */
	dl (closefunc); /* Close */
	dl (expungefunc); /* Expunge */
	dl (EXPANSION_nullfunc); /* Null */
	dl (beginiofunc); /* BeginIO */
	dl (abortiofunc); /* AbortIO */
	dl (0xFFFFFFFFul); /* end of table */

	/* DataTable */
	datatable = here ();
	dw (0xE000); /* INITBYTE */
	dw (0x0008); /* LN_TYPE */
	dw (0x0300); /* NT_DEVICE */
	dw (0xC000); /* INITLONG */
	dw (0x000A); /* LN_NAME */
	dl (ROM_uaeserialdev_resname);
	dw (0xE000); /* INITBYTE */
	dw (0x000E); /* LIB_FLAGS */
	dw (0x0600); /* LIBF_SUMUSED | LIBF_CHANGED */
	dw (0xD000); /* INITWORD */
	dw (0x0014); /* LIB_VERSION */
	dw (0x0004); /* 0.4 */
	dw (0xD000); /* INITWORD */
	dw (0x0016); /* LIB_REVISION */
	dw (0x0000);
	dw (0xC000); /* INITLONG */
	dw (0x0018); /* LIB_IDSTRING */
	dl (ROM_uaeserialdev_resid);
	dw (0x0000); /* end of table */

	ROM_uaeserialdev_init = here ();
	dl (0x00000100); /* size of device base */
	dl (functable);
	dl (datatable);
	dl (initcode);

	nscmd_cmd = here ();
	dw (NSCMD_DEVICEQUERY);
	dw (CMD_RESET);
	dw (CMD_READ);
	dw (CMD_WRITE);
	dw (CMD_CLEAR);
	dw (CMD_START);
	dw (CMD_STOP);
	dw (CMD_FLUSH);
	dw (SDCMD_BREAK);
	dw (SDCMD_SETPARAMS);
	dw (SDCMD_QUERY);
	dw (0);
}

void uaeserialdev_start_threads (void)
{
	uae_sem_init (&change_sem, 0, 1);
	uae_sem_init (&async_sem, 0, 1);
}

void uaeserialdev_reset (void)
{
	if (!currprefs.uaeserial)
		return;
	dev_reset ();
}
