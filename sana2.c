 /*
  * UAE - The Un*x Amiga Emulator
  *
  * SanaII emulation
  *
  * partially based on code from 3c589 PCMCIA driver by Neil Cafferke
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
#include "win32_uaenet.h"

#define SANA2NAME "uaenet.device"

#define MAX_ASYNC_REQUESTS 200
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
#define ETH_HEADER_SIZE (ADDR_SIZE + ADDR_SIZE + 2)

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

#define KNOWN_EVENTS (S2EVENT_ERROR|S2EVENT_TX|S2EVENT_RX|S2EVENT_ONLINE|\
   S2EVENT_OFFLINE|S2EVENT_BUFF|S2EVENT_HARDWARE|S2EVENT_SOFTWARE)

#define DRIVE_NEWSTYLE 0x4E535459L   /* 'NSTY' */
#define NSCMD_DEVICEQUERY 0x4000

#define SANA2OPB_MINE   0
#define SANA2OPF_MINE   (1<<SANA2OPB_MINE)
#define SANA2OPB_PROM   1
#define SANA2OPF_PROM   (1<<SANA2OPB_PROM)
#define SANA2IOB_RAW    7 
#define SANA2IOF_RAW    (1<<SANA2IOB_RAW)
#define SANA2IOB_BCAST  6
#define SANA2IOF_BCAST  (1<<SANA2IOB_BCAST)
#define SANA2IOB_MCAST  5
#define SANA2IOF_MCAST  (1<<SANA2IOB_MCAST)

struct s2packet {
    uae_u8 *data;
    int len;
};

volatile int uaenet_int_requested;

static char *getdevname (void)
{
    return "uaenet.device";
}

struct asyncreq {
    struct asyncreq *next;
    uaecptr request;
    struct s2packet *s2p;
    int ready;
};

struct devstruct {
    int unitnum, unit, opencnt, exclusive;
    struct asyncreq *ar;
    struct asyncreq *s2p;
    smp_comm_pipe requests;
    uae_thread_id tid;
    int thread_running;
    uae_sem_t sync_sem;
    void *sysdata;
    uae_u32 packetsreceived;
    uae_u32 packetssent;
    uae_u32 baddata;
    uae_u32 overruns;
    uae_u32 unknowntypesreceived;
    uae_u32 reconfigurations;
    uae_u32 online_micro;
    uae_u32 online_secs;
    int configured;
    int adapter;
    int online;
    uae_u8 mac[ADDR_SIZE];
    struct tapdata *td;
};

struct priv_devstruct {
    int inuse;
    int unit;
    int flags; /* OpenDevice() */
    uae_u8 tracks[65536];
    uaecptr copytobuff;
    uaecptr copyfrombuff;
    uaecptr packetfilter;
    struct tapdata *td;
    uaecptr tempbuf;
};

static struct tapdata td;
static struct devstruct devst[MAX_TOTAL_DEVICES];
static struct priv_devstruct pdevst[MAX_OPEN_DEVICES];
static uae_u32 nscmd_cmd;
static uae_sem_t change_sem, async_sem;

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
    put_long (request + 24, 0);
    dev->opencnt--;
    if (!dev->opencnt) {
	dev->exclusive = 0;
	pdev->inuse = 0;
	if (pdev->tempbuf) {
	    m68k_areg (&context->regs, 1) = pdev->tempbuf;
	    m68k_dreg (&context->regs, 0) = pdev->td->mtu + ETH_HEADER_SIZE;
	    CallLib (context, get_long (4), -0xD2); /* FreeMem */
	    pdev->tempbuf = 0;
	}
	write_comm_pipe_u32 (&dev->requests, 0, 1);
	uaenet_close (dev->sysdata);
    }
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

#define TAG_DONE   0
#define TAG_IGNORE 1
#define TAG_MORE   2
#define TAG_SKIP   3
#define TAG_USER   (1 << 31)
#define S2_Dummy        (TAG_USER + 0xB0000)
#define S2_CopyToBuff   (S2_Dummy + 1)
#define S2_CopyFromBuff (S2_Dummy + 2)
#define S2_PacketFilter (S2_Dummy + 3)

/* AARGHHH!! */
static uae_u32 REGPARAM2 uaenet_int_handler (TrapContext *ctx);
static void initint (TrapContext *ctx)
{
    uae_u32 tmp1;
    static int init;

    if (init)
	return;
    init = 1;
    tmp1 = here ();
    calltrap (deftrap2 (uaenet_int_handler, TRAPFLAG_EXTRA_STACK | TRAPFLAG_NO_RETVAL, "uaenet_STUPID_int_handler"));
    dw (0x4ef9);
    dl (get_long (ctx->regs.vbr + 0x78));
    put_long (ctx->regs.vbr + 0x78, tmp1);
}

static uae_u32 REGPARAM2 dev_open_2 (TrapContext *context)
{
    uaecptr ioreq = m68k_areg (&context->regs, 1);
    uae_u32 unit = m68k_dreg (&context->regs, 0);
    uae_u32 flags = m68k_dreg (&context->regs, 1);
    uaecptr buffermgmt = get_long (ioreq + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2 + 4 + 4 + 4);
    struct devstruct *dev = getdevstruct (unit);
    struct priv_devstruct *pdev = 0;
    int i;
    uaecptr tagp, tagpnext;
    
    initint(context);
    if (log_net)
	write_log ("opening %s:%d ioreq=%08.8X\n", SANA2NAME, unit, ioreq);
    if (!dev)
	return openfail (ioreq, 32); /* badunitnum */
    if (!buffermgmt)
	return openfail (ioreq, S2ERR_BAD_ARGUMENT);
    if ((flags & SANA2OPF_MINE) && (dev->exclusive || dev->opencnt > 0))
	return openfail (ioreq, -6); /* busy */
    if (flags & SANA2OPF_PROM) {
	if (dev->exclusive || dev->opencnt > 0)
	    return openfail (ioreq, -6); /* busy */
	return openfail (ioreq, S2ERR_NOT_SUPPORTED);
    }
    dev->sysdata = xcalloc (uaenet_getdatalenght(), 1);
    if (!uaenet_open (dev->sysdata, &td, dev, unit)) {
	xfree (dev->sysdata);
	return openfail (ioreq, 32); /* badunitnum */
    }
    if (get_word (m68k_areg (&context->regs, 6) + 32) == 0) {
	for (i = 0; i < MAX_OPEN_DEVICES; i++) {
	    pdev = &pdevst[i];
	    if (pdev->inuse == 0)
		break;
	}
	pdev->unit = unit;
	pdev->flags = flags;
	pdev->inuse = 1;
	pdev->td = &td;
	dev->td = &td;
	dev->adapter = td.active;
	put_long (ioreq + 24, pdev - pdevst);
	start_thread (dev);
    } else {
	for (i = 0; i < MAX_OPEN_DEVICES; i++) {
	    pdev = &pdevst[i];
	    if (pdev->inuse && pdev->unit == unit) break;
	}
	if (i == MAX_OPEN_DEVICES) {
	    uaenet_close (dev->sysdata);
	    xfree (dev->sysdata);
	    return openfail (ioreq, -1);
	}
	put_long (ioreq + 24, pdev - pdevst);
    }
    tagpnext =  buffermgmt;
    while (tagpnext) {
	uae_u32 tag = get_long (tagpnext);
	uae_u32 val = get_long (tagpnext + 4);
	tagp = tagpnext;
	tagpnext += 8;
	switch (tag)
	{
	    case TAG_DONE:
	    tagpnext = 0;
	    break;
	    case TAG_IGNORE:
	    break;
	    case TAG_MORE:
	    tagpnext = val;
	    break;
	    case TAG_SKIP:
	    tagpnext = tagp + val * 8;
	    break;
	    case S2_CopyToBuff:
	    pdev->copytobuff = val;
	    break;
	    case S2_CopyFromBuff:
	    pdev->copyfrombuff = val;
	    break;
	    case S2_PacketFilter:
	    pdev->packetfilter = val;
	    break;
	}
    }
    if (!pdev->copyfrombuff || !pdev->copyfrombuff) {
	uaenet_close (dev->sysdata);
	xfree (dev->sysdata);
	return openfail (ioreq, S2ERR_BAD_ARGUMENT);
    }
    m68k_dreg (&context->regs, 0) = dev->td->mtu + ETH_HEADER_SIZE;
    m68k_dreg (&context->regs, 1) = 0;
    pdev->tempbuf = CallLib (context, get_long (4), -0xC6); /* AllocMem */
    if (!pdev->tempbuf) {
	uaenet_close (dev->sysdata);
	xfree (dev->sysdata);
	return openfail (ioreq, S2ERR_BAD_ARGUMENT);
    }
    dev->exclusive = flags & SANA2OPF_MINE;
    dev->opencnt = get_word (m68k_areg (&context->regs, 6) + 32) + 1;
    put_word (m68k_areg (&context->regs, 6) + 32, dev->opencnt);
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

static void freepacket (struct s2packet *s2p)
{
    xfree (s2p->data);
    xfree (s2p);
}

static void add_async_packet (struct devstruct *dev, struct s2packet *s2p, uaecptr request, int signal)
{
    struct asyncreq *ar, *ar2;

    ar = (struct asyncreq*)xcalloc (sizeof (struct asyncreq), 1);
    ar->s2p = s2p;
    if (!dev->s2p) {
	dev->s2p = ar;
    } else {
	ar2 = dev->s2p;
	while (ar2->next)
	    ar2 = ar2->next;
	ar2->next = ar;
    }
    ar->request = request;
    if (signal)
	uaenet_int_requested = 1;
}

static void rem_async_packet (struct devstruct *dev, uaecptr request)
{
    struct asyncreq *ar, *prevar;

    uae_sem_wait (&async_sem);
    ar = dev->s2p;
    prevar = NULL;
    while (ar) {
	if (ar->request == request) {
	    if (prevar == NULL)
		dev->s2p = ar->next;
	    else
		prevar->next = ar->next;
	    uae_sem_post (&async_sem);
	    freepacket (ar->s2p);
	    xfree (ar);
	    return;
	}
	prevar = ar;
	ar = ar->next;
    }
    uae_sem_post (&async_sem);
}

static struct asyncreq *get_async_request (struct devstruct *dev, uaecptr request, int ready)
{
    struct asyncreq *ar;
    int ret = 0;

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

    if (log_net)
	write_log ("%s:%d async request %x added\n", getdevname(), dev->unit, request);

    uae_sem_wait (&async_sem);
    ar = (struct asyncreq*)xcalloc (sizeof (struct asyncreq), 1);
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
	    if (log_net)
		write_log ("%s:%d async request %x removed\n", getdevname(), dev->unit, request);
	    return 1;
	}
	prevar = ar;
	ar = ar->next;
    }
    uae_sem_post (&async_sem);
    write_log ("%s:%d async request %x not found for removal!\n", getdevname(), dev->unit, request);
    return 0;
}

static void abort_async (struct devstruct *dev, uaecptr request)
{
    struct asyncreq *ar = get_async_request (dev, request, 1);
    if (!ar) {
	write_log ("%s:%d: abort sync but no request %x found!\n", getdevname(), dev->unit, request);
	return;
    }
    if (log_net)
	write_log ("%s:%d asyncronous request=%08.8X aborted\n", getdevname(), dev->unit, request);
    put_byte (request + 31, -2);
    put_byte (request + 30, get_byte (request + 30) | 0x20);
    write_comm_pipe_u32 (&dev->requests, request, 1);
}

static void signalasync (struct devstruct *dev, struct asyncreq *ar, int actual, int err)
{
    uaecptr request = ar->request;
    int command = get_word (request + 28);
    if (log_net)
        write_log ("%s:%d CMD=%d async request %x completed\n", getdevname(), dev->unit, command, request);
    put_long (request + 32, actual);
    put_byte (request + 31, err);
    ar->ready = 1;
    write_comm_pipe_u32 (&dev->requests, request, 1);
}

static uae_u32 copytobuff (TrapContext *ctx, uaecptr from, uaecptr to, uae_u32 len, uaecptr func)
{
    m68k_areg (&ctx->regs, 0) = to;
    m68k_areg (&ctx->regs, 1) = from;
    m68k_dreg (&ctx->regs, 0) = len;
    return CallFunc (ctx, func);
}
static uae_u32 copyfrombuff (TrapContext *ctx, uaecptr from, uaecptr to, uae_u32 len, uaecptr func)
{
    m68k_areg (&ctx->regs, 0) = to;
    m68k_areg (&ctx->regs, 1) = from;
    m68k_dreg (&ctx->regs, 0) = len;
    return CallFunc (ctx, func);
}

static struct s2packet *createreadpacket (struct priv_devstruct *pdev, uae_u8 *d, int len)
{
    struct s2packet *s2p = xcalloc (sizeof (struct s2packet), 1);
    s2p->data = xmalloc (pdev->td->mtu + ETH_HEADER_SIZE);
    memcpy (s2p->data, d, len);
    s2p->len = len;
    return s2p;
}

static void handleread (TrapContext *ctx, struct priv_devstruct *pdev, uaecptr request, uae_u8 *d, int len)
{
    uae_u8 flags = get_byte (request + 30);
    uaecptr data = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2 + 4);
    uae_u32 datalength = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2);
    uaecptr srcaddr = request + 32 + 4 + 4;
    uaecptr dstaddr = request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES;

    memcpyha_safe (pdev->tempbuf, d, len);
    if (flags & SANA2IOF_RAW) {
	if (len > datalength)
	    len = datalength;
	copytobuff (ctx, pdev->tempbuf, data, len, pdev->copytobuff);
    } else {
	len -= ETH_HEADER_SIZE;
	if (len > datalength)
	    len = datalength;
	copytobuff (ctx, pdev->tempbuf + ETH_HEADER_SIZE, data, len, pdev->copytobuff);
    }
    memcpyha_safe (dstaddr, d, ADDR_SIZE);
    memcpyha_safe (srcaddr, d + ADDR_SIZE, ADDR_SIZE);
    put_long (request + 32 + 4, (d[2 * ADDR_SIZE] << 8) | d[2 * ADDR_SIZE + 1]);
    flags &= ~(SANA2IOF_BCAST | SANA2IOF_MCAST);
    if (d[0] == 0xff && d[1] == 0xff && d[2] == 0xff &&
	d[3] == 0xff && d[4] == 0xff && d[5] == 0xff)
	    flags |= SANA2IOF_BCAST;
    put_byte (request + 30, flags);
    put_byte (request + 31, 0);
}

void uaenet_gotdata (struct devstruct *dev, uae_u8 *d, int len)
{
    int i;
    struct asyncreq *ar;
    int gotit;
    uae_u16 type;

    type = (d[12] << 8) | d[13];
#if 1
    {
	char tmp[200];
	int j;
	write_log ("DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d\n",
	    d[0], d[1], d[2], d[3], d[4], d[5],
	    d[6], d[7], d[8], d[9], d[10], d[11],
	    type, len);
	j = 0;
	for (i = 14; i < 64 && i < len; i++, j++)
	    sprintf (tmp + j * 2, "%02X", d[i]);
	write_log ("%s\n", tmp);
    }
#endif

    uae_sem_wait (&async_sem);
    ar = dev->ar;
    gotit = 0;
    while (ar) {
	if (!ar->ready) {
	    uaecptr request = ar->request;
	    int command = get_word (request + 28);
            uae_u32 packettype = get_long (request + 32 + 4);
	    if (command == CMD_READ && packettype == type) {
		struct priv_devstruct *pdev = getpdevstruct (request);
		struct s2packet *s2p = createreadpacket (pdev, d, len);
		add_async_packet (dev, s2p, request, 1);
		dev->packetsreceived++;
		gotit = 1;
		break;
	    }
	}
	ar = ar->next;
    }
    if (!gotit) {
	/* S2_READORPHAN goes to every reader */
	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	    dev = &devst[i];
	    ar = dev->ar;
	    while (ar) {
		if (!ar->ready) {
		    uaecptr request = ar->request;
		    int command = get_word (request + 28);
		    if (command == S2_READORPHAN) {
			struct priv_devstruct *pdev = getpdevstruct (request);
			if (pdev->unit == dev->unit) {
			    struct s2packet *s2p = createreadpacket (pdev, d, len);
			    add_async_packet (dev, s2p, request, 1);
			    dev->packetsreceived++;
			    dev->unknowntypesreceived++;
			    break;
			}
		    }
		}
		ar = ar->next;
	    }
	}
    }
    uae_sem_post (&async_sem);
}

static struct s2packet *createwritepacket (TrapContext *ctx, uaecptr request)
{
    uae_u8 flags = get_byte (request + 30);
    uae_u32 datalength = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2);
    uaecptr data = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2 + 4);
    uaecptr srcaddr = request + 32 + 4 + 4;
    uaecptr dstaddr = request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES;
    uae_u32 packettype = get_long (request + 32 + 4);
    struct priv_devstruct *pdev = getpdevstruct (request);
    struct s2packet *s2p;

    if (!pdev)
	return NULL;
    s2p = xcalloc (sizeof (struct s2packet), 1);
    s2p->data = xmalloc (pdev->td->mtu + ETH_HEADER_SIZE);
    copyfrombuff (ctx, data, pdev->tempbuf, datalength, pdev->copyfrombuff);
    if (flags & SANA2IOF_RAW) {
	memcpyah_safe (s2p->data, pdev->tempbuf, datalength);
    } else {
	memcpyah_safe (s2p->data + ETH_HEADER_SIZE, pdev->tempbuf, datalength);
	memcpy (s2p->data + ADDR_SIZE, pdev->td->mac, ADDR_SIZE);
	memcpyah_safe (s2p->data, dstaddr, ADDR_SIZE);
	s2p->data[2 * ADDR_SIZE + 0] = packettype >> 8;
	s2p->data[2 * ADDR_SIZE + 1] = packettype;
    }
    s2p->len = datalength + ETH_HEADER_SIZE;
    return s2p;
}

int uaenet_getdata (struct devstruct *dev, uae_u8 *d, int *len)
{
    int gotit;
    struct asyncreq *ar;

    uae_sem_wait (&async_sem);
    ar = dev->ar;
    gotit = 0;
    while (ar) {
	if (!ar->ready) {
	    uaecptr request = ar->request;
	    int command = get_word (request + 28);
            uae_u32 packettype = get_long (request + 32 + 4);
	    if (command == CMD_WRITE || command == S2_BROADCAST) {
		struct priv_devstruct *pdev = getpdevstruct (request);
		struct asyncreq *ars2p = dev->s2p;
		while (ars2p) {
		    if (ars2p->request == request) {
			*len = ars2p->s2p->len;
			memcpy (d, ars2p->s2p->data, *len);
			break;
		    }
		    ars2p = ars2p->next;
		}
		dev->packetssent++;
		signalasync (dev, ar, *len, 0);
		gotit = 1;
		break;
	    }
	}
	ar = ar->next;
    }
    uae_sem_post (&async_sem);
    return gotit;
}

void checkevents (struct devstruct *dev, int mask)
{
    struct asyncreq *ar;

    uae_sem_wait (&async_sem);
    ar = dev->ar;
    while (ar) {
	if (!ar->ready) {
	    uaecptr request = ar->request;
	    int command = get_word (request + 28);
	    uae_u32 cmask = get_long (request + 32);
	    if (command == S2_ONEVENT && (mask & cmask))
		signalasync (dev, ar, 0, 0);
	}
	ar = ar->next;
    }
    uae_sem_post (&async_sem);
}

static int checksize (uaecptr request, struct devstruct *dev)
{
    uae_u32 datalength = get_long (request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES * 2);

    if (datalength > dev->td->mtu)
	return 0;
    return 1;
}

static int dev_do_io (struct devstruct *dev, uaecptr request, int quick)
{
    uae_u8 flags = get_byte (request + 30);
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

#if 1
    write_log ("S2: C=%02d T=%04X S=%02X%02X%02X%02X%02X%02X D=%02X%02X%02X%02X%02X%02X LEN=%d\n",
	command, packettype,
	get_byte (srcaddr + 0), get_byte (srcaddr + 1), get_byte (srcaddr + 2), get_byte (srcaddr + 3), get_byte (srcaddr + 4), get_byte (srcaddr + 5),
	get_byte (dstaddr + 0), get_byte (dstaddr + 1), get_byte (dstaddr + 2), get_byte (dstaddr + 3), get_byte (dstaddr + 4), get_byte (dstaddr + 5), 
	datalength);
#endif

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
	    if (!dev->online)
		goto offline;
	    async = 1;
	break;

	case S2_READORPHAN:
	    if (!dev->online)
		goto offline;
	    async = 1;
	break;

	case S2_BROADCAST:
	    if (!dev->online)
		goto offline;
	    /* fall through */
	case CMD_WRITE:
	    if (!dev->online)
		goto offline;
	    if (!checksize (request, dev))
		goto toobig;
	    async = 1;
	break;

	case S2_MULTICAST:
	    if (!dev->online)
		goto offline;
	    if ((get_byte (dstaddr + 0) & 1) == 0) {
		io_error = S2ERR_BAD_ADDRESS;
		wire_error = S2WERR_BAD_MULTICAST;
		goto end;
	    }
	    if (!checksize (request, dev))
		goto toobig;
	    async = 1;
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
	    if (size >= 12)
		put_long (statdata + 8, 0);
	    if (size >= 16)
		put_long (statdata + 12, 0);
	    if (size >= 18)
		put_word (statdata + 16, ADDR_SIZE * 8);
	    if (size >= 22)
		put_long (statdata + 18, dev->td->mtu);
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
	    put_long (statdata + 0, dev->packetsreceived);
	    put_long (statdata + 4, dev->packetssent);
	    put_long (statdata + 8, dev->baddata);
	    put_long (statdata + 12, dev->overruns);
	    put_long (statdata + 16, dev->unknowntypesreceived);
	    put_long (statdata + 20, dev->reconfigurations);
	    put_long (statdata + 24, dev->online_secs);
	    put_long (statdata + 28, dev->online_micro);
	break;

	case S2_GETSPECIALSTATS:
	    put_long (statdata + 1, 0);
	break;

	case S2_GETSTATIONADDRESS:
	    for (i = 0; i < ADDR_SIZE; i++) {
		put_byte (srcaddr + i, dev->td->mac[i]);
		put_byte (dstaddr + i, dev->td->mac[i]);
	    }
	break;

	case S2_CONFIGINTERFACE:
	    if (dev->configured) {
		io_error = S2ERR_BAD_STATE;
		wire_error = S2WERR_IS_CONFIGURED;
	    } else {
		for (i = 0; i < ADDR_SIZE; i++)
		    dev->mac[i] = get_byte (srcaddr + i);
		dev->configured = TRUE;
	    }
	break;

	case S2_ONLINE:
#if 0 // Genesis does this before S2_CONFIGINTERFACE !?
	    if (!dev->configured) {
		io_error = S2ERR_BAD_STATE;
		wire_error = S2WERR_NOT_CONFIGURED;
	    }
#endif
	    if (!dev->adapter) {
		io_error = S2ERR_OUTOFSERVICE;
		wire_error = S2WERR_RCVREL_HDW_ERR;
	    }
	    if (!io_error) {
		time_t t;
		dev->packetsreceived = 0;
		dev->packetssent = 0;
		dev->baddata = 0;
		dev->overruns = 0;
		dev->unknowntypesreceived = 0;
		dev->reconfigurations = 0;
		dev->online = 1;
		t = time (NULL);
		dev->online_micro = 0;
		dev->online_secs = (uae_u32)t;
		checkevents (dev, S2EVENT_ONLINE);
	    }
	break;

	case S2_TRACKTYPE:
	    if (packettype < 65535) {
		if (pdev->tracks[packettype]) {
		    io_error = S2ERR_BAD_STATE;
		    wire_error = S2WERR_ALREADY_TRACKED;
		} else {
		    pdev->tracks[packettype] = 1;
		}
	    } else {
		io_error = S2ERR_BAD_ARGUMENT;
	    }
	break;
	case S2_UNTRACKTYPE:
	    if (packettype < 65535) {
		if (!pdev->tracks[packettype]) {
		    io_error = S2ERR_BAD_STATE;
		    wire_error = S2WERR_NOT_TRACKED;
		} else {
		    pdev->tracks[packettype] = 0;
		}
	    } else {
		io_error = S2ERR_BAD_ARGUMENT;
	    }
	break;

	case S2_OFFLINE:
	    dev->online = 0;
	    checkevents (dev, S2EVENT_OFFLINE);
	break;

	case S2_ONEVENT:
	{
	    uae_u32 events;
	    uae_u32 wanted_events = get_long (request + 32);
	    if (wanted_events & ~KNOWN_EVENTS) {
		io_error = S2ERR_NOT_SUPPORTED;
		events = S2WERR_BAD_EVENT;
	    } else {
		if (!dev->online)
		    events = S2EVENT_ONLINE;
		else
		    events = S2EVENT_OFFLINE;
		events &= wanted_events;
		if (events) {
		    wire_error = events;
		} else {
		    async = 1;
		}
	    }
	}
	break;

	default:
	io_error = -3;
	break;

	offline:
	io_error = S2ERR_OUTOFSERVICE;
	wire_error = S2WERR_UNIT_OFFLINE;
	break;
	toobig:
	io_error = S2ERR_MTU_EXCEEDED;
	wire_error = S2WERR_GENERIC_ERROR;
	break;

    }
end:
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
    put_byte (request + 31, 0);
    if ((flags & 1) && dev_canquick (dev, request)) {
	if (dev_do_io (dev, request, 1))
	    write_log ("%s: command %d bug with IO_QUICK\n", SANA2NAME, command);
	return get_byte (request + 31);
    } else {
	if (command == CMD_WRITE || command == S2_BROADCAST) {
	    struct s2packet *s2p;
	    uae_sem_wait (&async_sem);
	    if (command == S2_BROADCAST) {
		uaecptr dstaddr = request + 32 + 4 + 4 + SANA2_MAX_ADDR_BYTES;
		put_byte (dstaddr +  0, 0xff);
		put_byte (dstaddr +  1, 0xff);
		put_byte (dstaddr +  2, 0xff);
		put_byte (dstaddr +  3, 0xff);
		put_byte (dstaddr +  4, 0xff);
		put_byte (dstaddr +  5, 0xff);
	    }
	    s2p = createwritepacket (context, request);
	    add_async_packet (dev, s2p, request, 0);
	    uae_sem_post (&async_sem);
	}
	put_byte (request + 30, get_byte (request + 30) & ~1);
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
	} else if (get_async_request (dev, request, 1)) {
	    uae_ReplyMsg (request);
	    release_async_request (dev, request);
	    rem_async_packet (dev, request);
	} else if (dev_do_io (dev, request, 0) == 0) {
	    uae_ReplyMsg (request);
	    rem_async_packet (dev, request);
	} else {
	    add_async_request (dev, request);
	    uaenet_trigger (dev->sysdata);
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
    struct devstruct *dev = getdevstruct (get_long (request + 24));

    if (!dev) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    abort_async (dev, request);
    return 0;
}

static uae_u32 REGPARAM2 uaenet_int_handler (TrapContext *ctx)
{
    int i;
    int ours = 0;

    uae_sem_wait (&async_sem);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	struct devstruct *dev = &devst[i];
	struct asyncreq *ar = dev->s2p;
	while (ar) {
	    int restart = 0;
	    if (!ar->ready) {
		uaecptr request = ar->request;
	        int command = get_word (request + 28);
		if (command == CMD_READ || command == S2_READORPHAN) {
		    struct priv_devstruct *pdev = getpdevstruct (request);
		    ar->ready = 1;
		    if (pdev) {
			handleread (ctx, pdev, request, ar->s2p->data, ar->s2p->len);
			write_comm_pipe_u32 (&dev->requests, request, 1);
			restart = 1;
			ours = 1;
		    }
		}
	    }
	    if (restart) {
		ar = dev->s2p;
		restart = 0;
	    } else {
		ar = ar->next;
	    }
	}
    }
    uaenet_int_requested = 0;
    uae_sem_post (&async_sem);
    return ours;
}

static void dev_reset (void)
{
    int i;
    struct devstruct *dev;
    int unitnum = 0;

    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	dev = &devst[i];
	if (dev->opencnt) {
	    while (dev->ar)
		abort_async (dev, dev->ar->request);
	    write_comm_pipe_u32 (&dev->requests, 0, 1);
	    uae_sem_wait (&dev->sync_sem);
	}
	memset (dev, 0, sizeof (struct devstruct));
	dev->unitnum = -1;
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

    ROM_netdev_resname = ds (getdevname());
    ROM_netdev_resid = ds ("UAE net.device 0.1");

    /* initcode */
    initcode = here ();
    calltrap (deftrap2 (dev_init, TRAPFLAG_EXTRA_STACK, "uaenet.int")); dw (RTS);

    /* Open */
    openfunc = here ();
    calltrap (deftrap2 (dev_open, TRAPFLAG_EXTRA_STACK, "uaenet.open")); dw (RTS);

    /* Close */
    closefunc = here ();
    calltrap (deftrap2 (dev_close, TRAPFLAG_EXTRA_STACK, "uaenet.close")); dw (RTS);

    /* Expunge */
    expungefunc = here ();
    calltrap (deftrap2 (dev_expunge, TRAPFLAG_EXTRA_STACK, "uaenet.expunge")); dw (RTS);

    /* BeginIO */
    beginiofunc = here ();
    calltrap (deftrap2 (dev_beginio, TRAPFLAG_EXTRA_STACK, "uaenet.beginio"));
    dw (RTS);

    /* AbortIO */
    abortiofunc = here ();
    calltrap (deftrap2 (dev_abortio, TRAPFLAG_EXTRA_STACK, "uaenet.abortio")); dw (RTS);

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
    uae_sem_init (&async_sem, 0, 1);
}

void netdev_reset (void)
{
    if (!currprefs.sana2[0])
	return;
    dev_reset ();
}
