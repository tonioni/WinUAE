 /*
  * UAE - The Un*x Amiga Emulator
  *
  * SanaII emulation
  *
  * Copyright 2007 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "autoconf.h"
#include "traps.h"
#include "execlib.h"
#include "native2amiga.h"
#include "blkdev.h"
#include "uae.h"
#include "sana2.h"
#include "tun_uae.h"

#define SANA2NAME "uaenet.device"

#define MAX_ASYNC_REQUESTS 20
#define MAX_OPEN_DEVICES 20

#define CMD_READ	2
#define CMD_WRITE	3
#define CMD_FLUSH	8
#define CMD_NONSTD	9
#define S2_START                (CMD_NONSTD)
#define S2_DEVICEQUERY          (S2_START+ 0)
#define S2_GETSTATIONADDRESS    (S2_START+ 1)
#define S2_CONFIGINTERFACE      (S2_START+ 2)
#define S2_ADDMULTICASTADDRESS  (S2_START+ 5)
#define S2_DELMULTICASTADDRESS  (S2_START+ 6)
#define S2_MULTICAST            (S2_START+ 7)
#define S2_BROADCAST            (S2_START+ 8)
#define S2_TRACKTYPE            (S2_START+ 9)
#define S2_UNTRACKTYPE          (S2_START+10)
#define S2_GETTYPESTATS         (S2_START+11)
#define S2_GETSPECIALSTATS      (S2_START+12)
#define S2_GETGLOBALSTATS       (S2_START+13)
#define S2_ONEVENT              (S2_START+14)
#define S2_READORPHAN           (S2_START+15)
#define S2_ONLINE               (S2_START+16)
#define S2_OFFLINE              (S2_START+17)


#define S2WireType_Ethernet             1
#define S2WireType_IEEE802              6

#define SANA2_MAX_ADDR_BITS     (128)
#define SANA2_MAX_ADDR_BYTES    ((SANA2_MAX_ADDR_BITS+7)/8)
#define ADDR_SIZE 6

#define S2ERR_NO_ERROR          0       /* peachy-keen                  */
#define S2ERR_NO_RESOURCES      1       /* resource allocation failure  */
#define S2ERR_BAD_ARGUMENT      3       /* garbage somewhere            */
#define S2ERR_BAD_STATE         4       /* inappropriate state          */
#define S2ERR_BAD_ADDRESS       5       /* who?                         */
#define S2ERR_MTU_EXCEEDED      6       /* too much to chew             */
#define S2ERR_NOT_SUPPORTED     8       /* hardware can't support cmd   */
#define S2ERR_SOFTWARE          9       /* software error detected      */
#define S2ERR_OUTOFSERVICE      10      /* driver is OFFLINE            */
#define S2ERR_TX_FAILURE        11      /* Transmission attempt failed  */

#define S2WERR_GENERIC_ERROR    0       /* no specific info available   */
#define S2WERR_NOT_CONFIGURED   1       /* unit not configured          */
#define S2WERR_UNIT_ONLINE      2       /* unit is currently online     */
#define S2WERR_UNIT_OFFLINE     3       /* unit is currently offline    */
#define S2WERR_ALREADY_TRACKED  4       /* protocol already tracked     */
#define S2WERR_NOT_TRACKED      5       /* protocol not tracked         */
#define S2WERR_BUFF_ERROR       6       /* buff mgt func returned error */
#define S2WERR_SRC_ADDRESS      7       /* source address problem       */
#define S2WERR_DST_ADDRESS      8       /* destination address problem  */
#define S2WERR_BAD_BROADCAST    9       /* broadcast address problem    */
#define S2WERR_BAD_MULTICAST    10      /* multicast address problem    */
#define S2WERR_MULTICAST_FULL   11      /* multicast address list full  */
#define S2WERR_BAD_EVENT        12      /* unsupported event class      */
#define S2WERR_BAD_STATDATA     13      /* statdata failed sanity check */
#define S2WERR_IS_CONFIGURED    15      /* attempt to config twice      */
#define S2WERR_NULL_POINTER     16      /* null pointer detected        */
#define S2WERR_TOO_MANY_RETIRES 17      /* tx failed - too many retries */
#define S2WERR_RCVREL_HDW_ERR   18      /* Driver fixable HW error      */

#define S2EVENT_ERROR           (1L<<0) /* error catch all              */
#define S2EVENT_TX              (1L<<1) /* transmitter error catch all  */
#define S2EVENT_RX              (1L<<2) /* receiver error catch all     */
#define S2EVENT_ONLINE          (1L<<3) /* unit is in service           */
#define S2EVENT_OFFLINE         (1L<<4) /* unit is not in service       */
#define S2EVENT_BUFF            (1L<<5) /* buff mgt function error      */
#define S2EVENT_HARDWARE        (1L<<6) /* hardware error catch all     */
#define S2EVENT_SOFTWARE        (1L<<7) /* software error catch all     */

#define DRIVE_NEWSTYLE 0x4E535459L   /* 'NSTY' */
#define NSCMD_DEVICEQUERY 0x4000

#define ASYNC_REQUEST_NONE 0
#define ASYNC_REQUEST_TEMP 1

struct devstruct {
    int unitnum, aunit;
    int opencnt;
    int changenum;
    volatile uaecptr d_request[MAX_ASYNC_REQUESTS];
    volatile int d_request_type[MAX_ASYNC_REQUESTS];
    volatile uae_u32 d_request_data[MAX_ASYNC_REQUESTS];
    smp_comm_pipe requests;
    uae_thread_id tid;
    int thread_running;
    uae_sem_t sync_sem;
};

struct priv_devstruct {
    int inuse;
    int unit;
    int mode;
    int flags; /* OpenDevice() */
    int configured;
    int adapter;
    uae_u8 mac[ADDR_SIZE];
    struct tapdata *td;

    int packetsreceived;
    int packetssent;
    int baddata;
    int overruns;
    int unknowntypesreceived;
    int reconfigurations;
};

static struct tapdata td;
static struct devstruct devst[MAX_TOTAL_DEVICES];
static struct priv_devstruct pdevst[MAX_OPEN_DEVICES];
static uae_u32 nscmd_cmd;
static uae_sem_t change_sem;

static struct device_info *devinfo (int mode, int unitnum, struct device_info *di)
{
    return sys_command_info (mode, unitnum, di);
}

static struct devstruct *getdevstruct (int unit)
{
    return &devst[unit];
}

static struct priv_devstruct *getpdevstruct (uaecptr request)
{
    int i = get_long (request + 24);
    if (i < 0 || i >= MAX_OPEN_DEVICES || pdevst[i].inuse == 0) {
	write_log ("%s: corrupt iorequest %08.8X %d\n", SANA2NAME, request, i);
	return 0;
    }
    return &pdevst[i];
}

static void *dev_thread (void *devs);
static int start_thread (struct devstruct *dev)
{
    if (dev->thread_running)
	return 1;
    init_comm_pipe (&dev->requests, 100, 1);
    uae_sem_init (&dev->sync_sem, 0, 0);
    uae_start_thread (SANA2NAME, dev_thread, dev, &dev->tid);
    uae_sem_wait (&dev->sync_sem);
    return dev->thread_running;
}

static void dev_close_3 (struct devstruct *dev, struct priv_devstruct *pdev)
{
    if (!dev->opencnt) return;
    dev->opencnt--;
    if (!dev->opencnt) {
	pdev->inuse = 0;
	write_comm_pipe_u32 (&dev->requests, 0, 1);
    }
}

static uae_u32 REGPARAM2 dev_close_2 (TrapContext *context)
{
    uae_u32 request = m68k_areg (&context->regs, 1);
    struct priv_devstruct *pdev = getpdevstruct (request);
    struct devstruct *dev;

    if (!pdev)
	return 0;
    dev = getdevstruct (pdev->unit);
    if (log_net)
	write_log ("%s:%d close, req=%08.8X\n", SANA2NAME, pdev->unit, request);
    if (!dev)
	return 0;
    dev_close_3 (dev, pdev);
    put_long (request + 24, 0);
    put_word (m68k_areg (&context->regs, 6) + 32, get_word (m68k_areg (&context->regs, 6) + 32) - 1);
    return 0;
}

static uae_u32 REGPARAM2 dev_close (TrapContext *context)
{
    return dev_close_2 (context);
}
static uae_u32 REGPARAM2 diskdev_close (TrapContext *context)
{
    return dev_close_2 (context);
}

static int openfail (uaecptr ioreq, int error)
{
    put_long (ioreq + 20, -1);
    put_byte (ioreq + 31, error);
    return (uae_u32)-1;
}

static uae_u32 REGPARAM2 dev_open_2 (TrapContext *context)
{
    uaecptr ioreq = m68k_areg (&context->regs, 1);
    uae_u32 unit = m68k_dreg (&context->regs, 0);
    uae_u32 flags = m68k_dreg (&context->regs, 1);
    struct devstruct *dev = getdevstruct (unit);
    struct priv_devstruct *pdev = 0;
    int i;

    if (log_net)
	write_log ("opening %s:%d ioreq=%08.8X\n", SANA2NAME, unit, ioreq);
    if (!dev)
	return openfail (ioreq, 32); /* badunitnum */
    if (!dev->opencnt) {
	for (i = 0; i < MAX_OPEN_DEVICES; i++) {
	    pdev = &pdevst[i];
	    if (pdev->inuse == 0) break;
	}
	pdev->unit = unit;
	pdev->flags = flags;
	pdev->inuse = 1;
	pdev->td = &td;
	pdev->adapter = td.active;
	put_long (ioreq + 24, pdev - pdevst);
	start_thread (dev);
    } else {
	for (i = 0; i < MAX_OPEN_DEVICES; i++) {
	    pdev = &pdevst[i];
	    if (pdev->inuse && pdev->unit == unit) break;
	}
	if (i == MAX_OPEN_DEVICES)
	    return openfail (ioreq, -1);
	put_long (ioreq + 24, pdev - pdevst);
    }
    dev->opencnt++;

    put_word (m68k_areg (&context->regs, 6) + 32, get_word (m68k_areg (&context->regs, 6) + 32) + 1);
    put_byte (ioreq + 31, 0);
    put_byte (ioreq + 8, 7);
    return 0;
}

static uae_u32 REGPARAM2 dev_open (TrapContext *context)
{
    return dev_open_2 (context);
}
static uae_u32 REGPARAM2 dev_expunge (TrapContext *context)
{
    return 0;
}
static uae_u32 REGPARAM2 diskdev_expunge (TrapContext *context)
{
    return 0;
}

static int is_async_request (struct devstruct *dev, uaecptr request)
{
    int i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (dev->d_request[i] == request) return 1;
	i++;
    }
    return 0;
}

static int add_async_request (struct devstruct *dev, uaecptr request, int type, uae_u32 data)
{
    int i;

//    if (log_net)
//	write_log ("%s: async request %08x (%d) added\n", SANA2NAME, request, type);
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

//    if (log_net)
//	write_log ("async request %p removed\n", request);
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
    if (i >= 0 && log_net)
	write_log ("%s: asyncronous request=%08.8X aborted, error=%d\n", SANA2NAME, request, errcode);
}

static int dev_do_io (struct devstruct *dev, uaecptr request)
{
    uae_u32 command = get_word (request + 28);
    uae_u32 packettype = get_long (request + 32 + 4);
    uaecptr data = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2 + 4);
    uae_u32 datalength = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2);
    uaecptr srcaddr = request + 32 + 4 + 4;
    uaecptr dstaddr = request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES;
    uaecptr statdata = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2 + 4 + 4);
    uaecptr buffermgmt = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2 + 4 + 4 + 4);
    uae_u32 io_error = 0;
    uae_u32 wire_error = 0;
    int i;
    int async = 0;
    struct priv_devstruct *pdev = getpdevstruct (request);

    if (!pdev)
	return 0;

    write_log ("S2: C=%d T=%d S=%02X%02X%02X%02X%02X%02X D=%02X%02X%02X%02X%02X%02X DAT=%08x LEN=%d STAT=%08x\n",
	command, packettype,
	get_byte (srcaddr + 0), get_byte (srcaddr + 1), get_byte (srcaddr + 2), get_byte (srcaddr + 3), get_byte (srcaddr + 4), get_byte (srcaddr + 5),
	get_byte (dstaddr + 0), get_byte (dstaddr + 1), get_byte (dstaddr + 2), get_byte (dstaddr + 3), get_byte (dstaddr + 4), get_byte (dstaddr + 5), 
	data, datalength, statdata);

    switch (command)
    {
	case NSCMD_DEVICEQUERY:
	    put_long (data + 4, 16); /* size */
	    put_word (data + 8, 7); /* NSDEVTYPE_SANA2 */
	    put_word (data + 10, 0);
	    put_long (data + 12, nscmd_cmd);
	    put_long (data + 32, 16);
	break;

	case CMD_READ:
	    goto offline;
	break;
	case S2_READORPHAN:
	    goto offline;
	break;

	case CMD_WRITE:
	    goto offline;
	break;
	case S2_BROADCAST:
	    goto offline;
	break;
	case S2_MULTICAST:
	    io_error = S2WERR_BAD_MULTICAST;
	break;

	case CMD_FLUSH:
	break;

	case S2_ADDMULTICASTADDRESS:
	break;
	case S2_DELMULTICASTADDRESS:
	break;

	case S2_DEVICEQUERY:
	{
	    int size = get_long (statdata);
	    if (size > 30)
		size = 30;
	    put_long (statdata + 4, size);
	    if (size >= 18)
		put_word (statdata + 16, ADDR_SIZE * 8);
	    if (size >= 22)
		put_long (statdata + 18, pdev->td->mtu);
	    if (size >= 26)
		put_long (statdata + 22, 10000000);
	    if (size >= 30)
		put_long (statdata + 26, S2WireType_Ethernet);
	}
	break;

	case S2_GETTYPESTATS:
	    io_error = S2ERR_BAD_STATE;
	    wire_error = S2WERR_NOT_TRACKED;
	break;

	case S2_GETGLOBALSTATS:
	    put_long (statdata + 0, pdev->packetsreceived);
	    put_long (statdata + 4, pdev->packetssent);
	    put_long (statdata + 8, pdev->baddata);
	    put_long (statdata + 12, pdev->overruns);
	    put_long (statdata + 16, pdev->unknowntypesreceived);
	    put_long (statdata + 20, pdev->reconfigurations);
	break;

	case S2_GETSPECIALSTATS:
	    put_long (statdata + 1, 0);
	break;

	case S2_GETSTATIONADDRESS:
	    for (i = 0; i < ADDR_SIZE; i++) {
		put_byte (srcaddr + i, pdev->td->mac[i]);
		put_byte (dstaddr + i, pdev->td->mac[i]);
	    }
	break;

	case S2_CONFIGINTERFACE:
	    if (pdev->configured) {
		io_error = S2ERR_BAD_STATE;
		wire_error = S2WERR_IS_CONFIGURED;
	    } else {
		for (i = 0; i < ADDR_SIZE; i++)
		    pdev->mac[i] = get_byte (srcaddr + i);
		pdev->configured = TRUE;
	    }
	break;

	case S2_ONLINE:
	    if (!pdev->configured) {
		io_error = S2ERR_BAD_STATE;
		wire_error = S2WERR_NOT_CONFIGURED;
	    }
	    if (!pdev->adapter) {
		io_error = S2ERR_OUTOFSERVICE;
		wire_error = S2WERR_RCVREL_HDW_ERR;
	    }
	    if (!io_error) {
		pdev->packetsreceived = 0;
		pdev->packetssent = 0;
		pdev->baddata = 0;
		pdev->overruns = 0;
		pdev->unknowntypesreceived = 0;
		pdev->reconfigurations = 0;
	    }
	break;

	case S2_OFFLINE:
	break;

	case S2_ONEVENT:
	    io_error = S2ERR_NOT_SUPPORTED;
	    wire_error = S2WERR_BAD_EVENT;
	break;

	default:
	io_error = -3;
	break;

	offline:
	io_error = S2ERR_OUTOFSERVICE;
	wire_error = S2WERR_UNIT_OFFLINE;
	break;
    }
    put_long (request + 32, wire_error);
    put_byte (request + 31, io_error);
    return async;
}

static int dev_can_quick (uae_u32 command)
{
    switch (command)
    {
	case NSCMD_DEVICEQUERY:
	return 1;
    }
    return 0;
}

static int dev_canquick (struct devstruct *dev, uaecptr request)
{
    uae_u32 command = get_word (request + 28);
    return dev_can_quick (command);
}

static uae_u32 REGPARAM2 dev_beginio (TrapContext *context)
{
    uae_u32 request = m68k_areg (&context->regs, 1);
    uae_u8 flags = get_byte (request + 30);
    int command = get_word (request + 28);
    struct priv_devstruct *pdev = getpdevstruct (request);
    struct devstruct *dev;

    put_byte (request + 8, NT_MESSAGE);
    if (!pdev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    dev = getdevstruct (pdev->unit);
    if (!dev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    put_byte (request+31, 0);
    if ((flags & 1) && dev_canquick (dev, request)) {
	if (dev_do_io (dev, request))
	    write_log ("%s: command %d bug with IO_QUICK\n", SANA2NAME, command);
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
    struct devstruct *dev = (struct devstruct*)devs;

    uae_set_thread_priority (2);
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
	    if (log_net)
		write_log ("%s:%d async request %08.8X\n", SANA2NAME, dev->unitnum, request);
	}
	uae_sem_post (&change_sem);
    }
    return 0;
}

static uae_u32 REGPARAM2 dev_init_2 (TrapContext *context)
{
    uae_u32 base = m68k_dreg (&context->regs,0);
    if (log_net)
	write_log ("%s init\n", SANA2NAME);
    return base;
}

static uae_u32 REGPARAM2 dev_init (TrapContext *context)
{
    return dev_init_2 (context);
}

static uae_u32 REGPARAM2 dev_abortio (TrapContext *context)
{
    uae_u32 request = m68k_areg (&context->regs, 1);
    struct priv_devstruct *pdev = getpdevstruct (request);
    struct devstruct *dev;

    if (!pdev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    dev = getdevstruct (pdev->unit);
    if (!dev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    put_byte (request + 31, -2);
    if (log_net)
	write_log ("%s abortio: unit=%d, request=%08.8X\n", SANA2NAME, pdev->unit, request);
    abort_async (dev, request, -2, 0);
    return 0;
}

static void dev_reset (void)
{
    int i, j;
    struct devstruct *dev;
    int unitnum = 0;

    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	dev = &devst[i];
	if (dev->opencnt > 0) {
	    for (j = 0; j < MAX_ASYNC_REQUESTS; j++) {
		uaecptr request;
		if (request = dev->d_request[i])
		    abort_async (dev, request, 0, 0);
	    }
	    dev->opencnt = 1;
	}
	memset (dev, 0, sizeof (struct devstruct));
	dev->unitnum = dev->aunit = -1;
    }
    for (i = 0; i < MAX_OPEN_DEVICES; i++)
	memset (&pdevst[i], 0, sizeof (struct priv_devstruct));

}

static uaecptr ROM_netdev_resname = 0,
    ROM_netdev_resid = 0,
    ROM_netdev_init = 0;

uaecptr netdev_startup (uaecptr resaddr)
{
    if (!currprefs.sana2[0])
	return resaddr;
    if (log_net)
	write_log ("netdev_startup(0x%x)\n", resaddr);
    /* Build a struct Resident. This will set up and initialize
     * the uaescsi.device */
    put_word (resaddr + 0x0, 0x4AFC);
    put_long (resaddr + 0x2, resaddr);
    put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
    put_word (resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
    put_word (resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
    put_long (resaddr + 0xE, ROM_netdev_resname);
    put_long (resaddr + 0x12, ROM_netdev_resid);
    put_long (resaddr + 0x16, ROM_netdev_init); /* calls scsidev_init */
    resaddr += 0x1A;
    return resaddr;
}

void netdev_install (void)
{
    uae_u32 functable, datatable;
    uae_u32 initcode, openfunc, closefunc, expungefunc;
    uae_u32 beginiofunc, abortiofunc;

    if (!currprefs.sana2[0])
	return;
    if (log_net)
	write_log ("netdev_install(): 0x%x\n", here ());

    tap_open_driver (&td, currprefs.sana2);

    ROM_netdev_resname = ds ("uaenet.device");
    ROM_netdev_resid = ds ("UAE net.device 0.1");

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
    dl (ROM_netdev_resname);
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
    dl (ROM_netdev_resid);
    dw (0x0000); /* end of table */

    ROM_netdev_init = here ();
    dl (0x00000100); /* size of device base */
    dl (functable);
    dl (datatable);
    dl (initcode);

    nscmd_cmd = here ();
    dw (NSCMD_DEVICEQUERY);
    dw (CMD_READ);
    dw (CMD_WRITE);
    dw (CMD_FLUSH);
    dw (S2_DEVICEQUERY);
    dw (S2_GETSTATIONADDRESS);
    dw (S2_CONFIGINTERFACE);
    dw (S2_ADDMULTICASTADDRESS);
    dw (S2_DELMULTICASTADDRESS);
    dw (S2_MULTICAST);
    dw (S2_BROADCAST);
    dw (S2_TRACKTYPE);
    dw (S2_UNTRACKTYPE);
    dw (S2_GETTYPESTATS);
    dw (S2_GETSPECIALSTATS);
    dw (S2_GETGLOBALSTATS);
    dw (S2_ONEVENT);
    dw (S2_READORPHAN);
    dw (S2_ONLINE);
    dw (S2_OFFLINE);
    dw (0);

}

void netdev_start_threads (void)
{
    if (!currprefs.sana2[0])
	return;
    if (log_net)
	write_log ("netdev_start_threads()\n");
    uae_sem_init (&change_sem, 0, 1);
}

void netdev_reset (void)
{
    if (!currprefs.sana2[0])
	return;
    dev_reset ();
    tap_close_driver (&td);
}
