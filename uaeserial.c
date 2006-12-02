 /*
  * UAE - The Un*x Amiga Emulator
  *
  * uaeserial.device
  *
  * Copyright 2004 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "uae.h"
#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "autoconf.h"
#include "execlib.h"
#include "native2amiga.h"

#define MAX_ASYNC_REQUESTS 20
#define MAX_TOTAL_DEVICES 8
#define MAX_OPEN_DEVICES 20

#define ASYNC_REQUEST_NONE 0
#define ASYNC_REQUEST_TEMP 1

static int log_serial = 1;

#define CMD_INVALID	0
#define CMD_RESET	1
#define CMD_READ	2
#define CMD_WRITE	3
#define CMD_UPDATE	4
#define CMD_CLEAR	5
#define CMD_STOP	6
#define CMD_START	7
#define CMD_FLUSH	8
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


struct devstruct {
    int unit;
    char *name;
    int uniq;
    int exclusive;
    volatile uaecptr d_request[MAX_ASYNC_REQUESTS];
    volatile int d_request_type[MAX_ASYNC_REQUESTS];
    volatile uae_u32 d_request_data[MAX_ASYNC_REQUESTS];

    smp_comm_pipe requests;
    uae_thread_id tid;
    int thread_running;
    uae_sem_t sync_sem;
};

static int uniq;

static struct devstruct devst[MAX_TOTAL_DEVICES];

static uae_sem_t change_sem;

static char *getdevname (int type)
{
    return "uaeserial.device";
}

static void io_log (char *msg, uaecptr request)
{
    if (log_serial)
        write_log ("%s: %08X %d %08.8X %d %d io_actual=%d io_error=%d\n",
	    msg, request,get_word(request + 28),get_long(request + 40),get_long(request + 36),get_long(request + 44),
	    get_long (request + 32), get_byte (request + 31));
}

static void memcpyha (uae_u32 dst, char *src, int size)
{
    while (size--)
	put_byte (dst++, *src++);
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
    uae_start_thread (dev_thread, dev, &dev->tid);
    uae_sem_wait (&dev->sync_sem);
    return dev->thread_running;
}

static void dev_close_3 (struct devstruct *dev)
{
    dev->unit = -1;
    xfree(dev->name);
    write_comm_pipe_u32 (&dev->requests, 0, 1);
}

static uae_u32 dev_close_2 (void)
{
    uae_u32 request = m68k_areg (regs, 1);
    struct devstruct *dev;

    dev = getdevstruct (pdev->uniq);
    if (log_serial)
	write_log ("%s:%d close, req=%08.8X\n", dev->name, dev->unit, request);
    if (!dev)
	return 0;
    dev_close_3 (dev);
    put_long (request + 24, 0);
    put_word (m68k_areg(regs, 6) + 32, get_word (m68k_areg(regs, 6) + 32) - 1);
    return 0;
}

static uae_u32 dev_close (void)
{
    return dev_close_2 ();
}
static uae_u32 diskdev_close (void)
{
    return dev_close_2 ();
}

static int openfail (uaecptr ioreq, int error)
{
    put_long (ioreq + 20, -1);
    put_byte (ioreq + 31, error);
    return (uae_u32)-1;
}

static uae_u32 dev_open_2 (int type)
{
    uaecptr ioreq = m68k_areg(regs, 1);
    uae_u32 unit = m68k_dreg (regs, 0);
    uae_u32 flags = m68k_dreg (regs, 1);
    struct devstruct *dev;
    int i;
    char devname[256];

    if (log_serial)
	write_log ("opening %s:%d ioreq=%08.8X\n", getdevname (type), unit, ioreq);
    sprintf(devname,"COM%d", unit);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (devst[i].unit == unit && devst[i].exclusive)
	    return openfail (ioreq, -6); /* busy */
    }
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (devst[i].unit == -1)
	    break;
    }
    dev = &devst[i];
    if (i == MAX_TOTAL_DEVICES)
	return openfail (ioreq, 32); /* badunitnum */
    dev->unit = unit;
    dev->name = my_strdup (devname);
    dev->uniq = ++uniq;
    dev->exclusive = (get_word(ioreq + io_SerFlags) & SERF_SHARED) ? 0 : 1;
    put_long (ioreq + 24, i);
    start_thread (dev);

    put_word (m68k_areg(regs, 6) + 32, get_word (m68k_areg(regs, 6) + 32) + 1);
    put_byte (ioreq + 31, 0);
    put_byte (ioreq + 8, 7);
    return 0;
}

static uae_u32 dev_open (void)
{
    return dev_open_2 (0);
}

static uae_u32 dev_expunge (void)
{
    return 0;
}

static int is_async_request (struct devstruct *dev, uaecptr request)
{
    int i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (dev->d_request[i] == request)
	    return 1;
	i++;
    }
    return 0;
}

static int add_async_request (struct devstruct *dev, uaecptr request, int type, uae_u32 data)
{
    int i;

    if (log_serial)
	write_log ("async request %p (%d) added\n", request, type);
    i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (dev->d_request[i] == request) {
	    dev->d_request_type[i] = type;
	    dev->d_request_data[i] = data;
	    return 0;
	}
	i++;
    }
    i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (dev->d_request[i] == 0) {
	    dev->d_request[i] = request;
	    dev->d_request_type[i] = type;
	    dev->d_request_data[i] = data;
	    return 0;
	}
	i++;
    }
    return -1;
}

static int release_async_request (struct devstruct *dev, uaecptr request)
{
    int i = 0;

    if (log_serial)
	write_log ("async request %p removed\n", request);
    while (i < MAX_ASYNC_REQUESTS) {
	if (dev->d_request[i] == request) {
	    int type = dev->d_request_type[i];
	    dev->d_request[i] = 0;
	    dev->d_request_data[i] = 0;
	    dev->d_request_type[i] = 0;
	    return type;
	}
	i++;
    }
    return -1;
}

static void abort_async (struct devstruct *dev, uaecptr request, int errcode, int type)
{
    int i;
    i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (dev->d_request[i] == request && dev->d_request_type[i] == ASYNC_REQUEST_TEMP) {
	    /* ASYNC_REQUEST_TEMP = request is processing */
	    sleep_millis (10);
	    i = 0;
	    continue;
	}
	i++;
    }
    i = release_async_request (dev, request);
    if (i >= 0 && log_serial)
	write_log ("asyncronous request=%08.8X aborted, error=%d\n", request, errcode);
}

static int dev_do_io (struct devstruct *dev, uaecptr request)
{
    uae_u32 command;
    uae_u32 io_data = get_long (request + 40); // 0x28
    uae_u32 io_length = get_long (request + 36); // 0x24
    uae_u32 io_actual = get_long (request + 32); // 0x20
    uae_u32 io_offset = get_long (request + 44); // 0x2c
    uae_u32 io_error = 0;
    int async = 0;
    struct devstruct *dev = getdevstruct (get_long(request + 24));

    if (!dev)
	return 0;
    command = get_word (request+28);

    switch (command)
    {
	case SDCMD_QUERY:
	break;
	case SDCMD_SETPARAMS:
	break;
	case CMD_WRITE:
	break;
	case CMD_READ:
	break;
	case SDCMD_BREAK;
	case CMD_FLUSH;
	case CMD_START:
	case CMD_STOP:
	case CMD_CLEAR:
	case CMD_RESET:
	break;
	default:
	io_error = -3;
	break;
    }
    put_long (request + 32, io_actual);
    put_byte (request + 31, io_error);
    io_log ("dev_io",request);
    return async;
}

static int dev_can_quick (uae_u32 command)
{
/*
    switch (command)
    {
	return 1;
    }
*/
    return 0;
}

static int dev_canquick (struct devstruct *dev, uaecptr request)
{
    uae_u32 command = get_word (request + 28);
    return dev_can_quick (command);
}

static uae_u32 dev_beginio (void)
{
    uae_u32 request = m68k_areg(regs, 1);
    uae_u8 flags = get_byte (request + 30);
    int command = get_word (request + 28);
    struct devstruct *dev = getdevstruct (get_long(request + 24));

    put_byte (request+8, NT_MESSAGE);
    if (!dev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    put_byte (request+31, 0);
    if ((flags & 1) && dev_canquick (dev, request)) {
	if (dev_do_io (dev, request))
	    write_log ("device %s:%d command %d bug with IO_QUICK\n", dev->name, dev->unit, command);
	return get_byte (request + 31);
    } else {
        add_async_request (dev, request, ASYNC_REQUEST_TEMP, 0);
        put_byte (request+30, get_byte (request + 30) & ~1);
	write_comm_pipe_u32 (&dev->requests, request, 1);
	return 0;
    }
}

static void *dev_thread (void *devs)
{
    struct devstruct *dev = devs;

    set_thread_priority (2);
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
	} else if (dev_do_io (dev, request) == 0) {
            put_byte (request + 30, get_byte (request + 30) & ~1);
	    release_async_request (dev, request);
            uae_ReplyMsg (request);
	} else {
	    if (log_serial)
		write_log ("async request %08.8X\n", request);
	}
	uae_sem_post (&change_sem);
    }
    return 0;
}

static uae_u32 dev_init_2 (int type)
{
    uae_u32 base = m68k_dreg (regs,0);
    if (log_serial)
	write_log ("%s init\n", getdevname (type));
    return base;
}

static uae_u32 dev_init (void)
{
    return dev_init_2 (0);
}

static uae_u32 dev_abortio (void)
{
    uae_u32 request = m68k_areg(regs, 1);
    struct devstruct *dev = getdevstruct (get_long(request + 24));

    if (!dev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    put_byte (request + 31, -2);
    if (log_serial)
	write_log ("abortio %s:%d, request=%08.8X\n", dev->name, dev->unit, request);
    abort_async (dev, request, -2, 0);
    return 0;
}

static void dev_reset (void)
{
    int i, j;
    struct uaeserial_info *uaedev;
    struct devstruct *dev;

    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	dev = &devst[i];
	if (dev->opencnt > 0) {
	    for (j = 0; j < MAX_ASYNC_REQUESTS; j++) {
	        uaecptr request;
		if (request = dev->d_request[i]) 
		    abort_async (dev, request, 0, 0);
	    }
	}
	free (dev->name);
	memset (dev, 0, sizeof (struct devstruct));
	dev->unit = -1;
    }
}

static uaecptr ROM_serialdev_resname = 0,
    ROM_serialdev_resid = 0,
    ROM_serialdev_init = 0;

uaecptr serialdev_startup (uaecptr resaddr)
{
    if (!currprefs.uaeserial)
	return resaddr;
    /* Build a struct Resident. This will set up and initialize
     * the serial.device */
    put_word(resaddr + 0x0, 0x4AFC);
    put_long(resaddr + 0x2, resaddr);
    put_long(resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
    put_word(resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
    put_word(resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
    put_long(resaddr + 0xE, ROM_serialdev_resname);
    put_long(resaddr + 0x12, ROM_serialdev_resid);
    put_long(resaddr + 0x16, ROM_serialdev_init); /* calls scsidev_init */
    resaddr += 0x1A;
    return resaddr;
}


void serialdev_install (void)
{
    uae_u32 functable, datatable;
    uae_u32 initcode, openfunc, closefunc, expungefunc;
    uae_u32 beginiofunc, abortiofunc;

    if (!currprefs.uaeserial)
	return;

    ROM_serialdev_resname = ds ("uaeserial.device");
    ROM_serialdev_resid = ds ("UAE serial.device 0.2");

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
    calltrap (deftrap (dev_beginio));
    dw (RTS);

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
    dl (ROM_serialdev_resname);
    dw (0xE000); /* INITBYTE */
    dw (0x000E); /* LIB_FLAGS */
    dw (0x0600); /* LIBF_SUMUSED | LIBF_CHANGED */
    dw (0xD000); /* INITWORD */
    dw (0x0014); /* LIB_VERSION */
    dw (0x0004); /* 0.4 */
    dw (0xD000); /* INITWORD */
    dw (0x0016); /* LIB_REVISION */
    dw (0x0000); /* end of table already ??? */
    dw (0xC000); /* INITLONG */
    dw (0x0018); /* LIB_IDSTRING */
    dl (ROM_serialdev_resid);
    dw (0x0000); /* end of table */

    ROM_serialdev_init = here ();
    dl (0x00000100); /* size of device base */
    dl (functable);
    dl (datatable);
    dl (initcode);
}

void serialdev_start_threads (void)
{
}

void serialdev_reset (void)
{
    if (!currprefs.uaeserial)
	return;
    dev_reset ();
}