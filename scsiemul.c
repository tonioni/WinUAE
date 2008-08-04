 /*
  * UAE - The Un*x Amiga Emulator
  *
  * scsi.device emulation
  *
  * Copyright 1995 Bernd Schmidt
  * Copyright 1999 Patrick Ohly
  * Copyright 2001 Brian King
  * Copyright 2002 Toni Wilen
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
#include "scsidev.h"
#include "uae.h"
#include "execio.h"

#define CDDEV_COMMANDS

#define UAEDEV_SCSI "uaescsi.device"
#define UAEDEV_SCSI_ID 1
#define UAEDEV_DISK "uaedisk.device"
#define UAEDEV_DISK_ID 2

#define MAX_ASYNC_REQUESTS 20
#define MAX_OPEN_DEVICES 20

#define ASYNC_REQUEST_NONE 0
#define ASYNC_REQUEST_TEMP 1
#define ASYNC_REQUEST_CHANGEINT 10

struct devstruct {
    int unitnum, aunit;
    int opencnt;
    int changenum;
    int allow_scsi;
    int allow_ioctl;
    int drivetype;
    int iscd;
    volatile uaecptr d_request[MAX_ASYNC_REQUESTS];
    volatile int d_request_type[MAX_ASYNC_REQUESTS];
    volatile uae_u32 d_request_data[MAX_ASYNC_REQUESTS];
    struct device_info di;

    smp_comm_pipe requests;
    int thread_running;
    uae_sem_t sync_sem;
};

struct priv_devstruct {
    int inuse;
    int unit;
    int mode;
    int scsi;
    int ioctl;
    int noscsi;
    int type;
    int flags; /* OpenDevice() */
};

static struct devstruct devst[MAX_TOTAL_DEVICES];
static struct priv_devstruct pdevst[MAX_OPEN_DEVICES];
static uae_u32 nscmd_cmd;
static uae_sem_t change_sem;

static struct device_info *devinfo (int mode, int unitnum, struct device_info *di)
{
    return sys_command_info (mode, unitnum, di);
}

static void io_log (const char *msg, uaecptr request)
{
    if (log_scsi)
	write_log ("%s: %08X %d %08X %d %d io_actual=%d io_error=%d\n",
	    msg, request, get_word (request + 28), get_long (request + 40),
	    get_long (request + 36), get_long (request + 44),
	    get_long (request + 32), get_byte (request + 31));
}

static struct devstruct *getdevstruct (int unit)
{
    int i;
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (unit >= 0 && devst[i].aunit == unit) return &devst[i];
    }
    return 0;
}

static struct priv_devstruct *getpdevstruct (uaecptr request)
{
    int i = get_long (request + 24);
    if (i < 0 || i >= MAX_OPEN_DEVICES || pdevst[i].inuse == 0) {
	write_log ("uaescsi.device: corrupt iorequest %08X %d\n", request, i);
	return 0;
    }
    return &pdevst[i];
}

static char *getdevname (int type)
{
    switch (type) {
	case UAEDEV_SCSI_ID:
	return UAEDEV_SCSI;
	case UAEDEV_DISK_ID:
	return UAEDEV_DISK;
	default:
	return "NULL";
    }
}

static void *dev_thread (void *devs);
static int start_thread (struct devstruct *dev)
{
    if (dev->thread_running)
	return 1;
    init_comm_pipe (&dev->requests, 100, 1);
    uae_sem_init (&dev->sync_sem, 0, 0);
    uae_start_thread ("uaescsi", dev_thread, dev, NULL);
    uae_sem_wait (&dev->sync_sem);
    return dev->thread_running;
}

static void dev_close_3 (struct devstruct *dev, struct priv_devstruct *pdev)
{
    if (!dev->opencnt) return;
    dev->opencnt--;
    if (!dev->opencnt) {
	if (pdev->scsi)
	    sys_command_close (DF_SCSI, dev->unitnum);
	if (pdev->ioctl)
	    sys_command_close (DF_IOCTL, dev->unitnum);
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
    if (log_scsi)
	write_log ("%s:%d close, req=%08X\n", getdevname (pdev->type), pdev->unit, request);
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

static uae_u32 REGPARAM2 dev_open_2 (TrapContext *context, int type)
{
    uaecptr ioreq = m68k_areg (&context->regs, 1);
    uae_u32 unit = m68k_dreg (&context->regs, 0);
    uae_u32 flags = m68k_dreg (&context->regs, 1);
    struct devstruct *dev = getdevstruct (unit);
    struct priv_devstruct *pdev = 0;
    int i;

    if (log_scsi)
	write_log ("opening %s:%d ioreq=%08X\n", getdevname (type), unit, ioreq);
    if (get_word (ioreq + 0x12) < IOSTDREQ_SIZE)
	return openfail (ioreq, IOERR_BADLENGTH);
    if (!dev)
	return openfail (ioreq, 32); /* badunitnum */
    if (!dev->opencnt) {
	for (i = 0; i < MAX_OPEN_DEVICES; i++) {
	    pdev = &pdevst[i];
	    if (pdev->inuse == 0) break;
	}
	if (type == UAEDEV_SCSI_ID && sys_command_open (DF_SCSI, dev->unitnum)) {
	    pdev->scsi = 1;
	    pdev->mode = DF_SCSI;
	}
	if (type == UAEDEV_DISK_ID && sys_command_open (DF_IOCTL, dev->unitnum)) {
	    pdev->ioctl = 1;
	    pdev->mode = DF_IOCTL;
	}
	if (!pdev->scsi && !pdev->ioctl)
	    return openfail (ioreq, IOERR_OPENFAIL);
	pdev->type = type;
	pdev->unit = unit;
	pdev->flags = flags;
	pdev->inuse = 1;
	put_long (ioreq + 24, pdev - pdevst);
	start_thread (dev);
    } else {
	for (i = 0; i < MAX_OPEN_DEVICES; i++) {
	    pdev = &pdevst[i];
	    if (pdev->inuse && pdev->unit == unit) break;
	}
	if (i == MAX_OPEN_DEVICES)
	    return openfail (ioreq, IOERR_OPENFAIL);
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
    return dev_open_2 (context, UAEDEV_SCSI_ID);
}
static uae_u32 REGPARAM2 diskdev_open (TrapContext *context)
{
    return dev_open_2 (context, UAEDEV_DISK_ID);
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

void scsi_do_disk_change (int device_id, int insert)
{
    int i, j;

    uae_sem_wait (&change_sem);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (devst[i].di.id == device_id) {
	    devst[i].changenum++;
	    j = 0;
	    while (j < MAX_ASYNC_REQUESTS) {
		if (devst[i].d_request_type[j] == ASYNC_REQUEST_CHANGEINT) {
		    uae_Cause (devst[i].d_request_data[j]);
		}
		j++;
	    }
	}
    }
    uae_sem_post (&change_sem);
}

static int add_async_request (struct devstruct *dev, uaecptr request, int type, uae_u32 data)
{
    int i;

    if (log_scsi)
	write_log ("async request %08x (%d) added\n", request, type);
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

    if (log_scsi)
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
    if (i >= 0 && log_scsi)
	write_log ("asyncronous request=%08X aborted, error=%d\n", request, errcode);
}

static int command_read (int mode, struct devstruct *dev, uaecptr data, uae_u64 offset, uae_u32 length, uae_u32 *io_actual)
{
    int blocksize = dev->di.bytespersector;
    uae_u8 *temp;

    length /= blocksize;
    offset /= blocksize;
    while (length > 0) {
	temp = sys_command_read (mode, dev->unitnum, offset);
	if (!temp)
	    return 20;
	memcpyha_safe (data, temp, blocksize);
	data += blocksize;
	offset++;
	length--;
    }
    return 0;
}
static int command_write (int mode, struct devstruct *dev, uaecptr data, uae_u64 offset, uae_u32 length, uae_u32 *io_actual)
{
    uae_u32 blocksize = dev->di.bytespersector;
    struct device_scsi_info dsi;

    if (!sys_command_scsi_info(mode, dev->unitnum, &dsi))
	return 20;
    length /= blocksize;
    offset /= blocksize;
    while (length > 0) {
	int err;
	memcpyah_safe (dsi.buffer, data, blocksize);
	err = sys_command_write (mode, dev->unitnum, offset);
	if (!err)
	    return 20;
	if (err < 0)
	    return 28; // write protected
	data += blocksize;
	offset++;
	length--;
    }
    return 0;
}

static int command_cd_read (int mode, struct devstruct *dev, uaecptr data, uae_u64 offset, uae_u32 length, uae_u32 *io_actual)
{
    uae_u8 *temp;
    uae_u32 len, sector;

    uae_u32 startoffset = offset % dev->di.bytespersector;
    offset -= startoffset;
    sector = offset / dev->di.bytespersector;
    *io_actual = 0;
    while (length > 0) {
	temp = sys_command_cd_read (mode, dev->unitnum, sector);
	if (!temp)
	    return 20;
	if (startoffset > 0) {
	    len = dev->di.bytespersector - startoffset;
	    if (len > length) len = length;
	    memcpyha_safe (data, temp + startoffset, len);
	    length -= len;
	    data += len;
	    startoffset = 0;
	    *io_actual += len;
	} else if (length >= dev->di.bytespersector) {
	    len = dev->di.bytespersector;
	    memcpyha_safe (data, temp, len);
	    length -= len;
	    data += len;
	    *io_actual += len;
	} else {
	    memcpyha_safe (data, temp, length);
	    *io_actual += length;
	    length = 0;
	}
	sector++;
    }
    return 0;
}

static int dev_do_io (struct devstruct *dev, uaecptr request)
{
    uae_u32 command;
    uae_u32 io_data = get_long (request + 40); // 0x28
    uae_u32 io_length = get_long (request + 36); // 0x24
    uae_u32 io_actual = get_long (request + 32); // 0x20
    uae_u32 io_offset = get_long (request + 44); // 0x2c
    uae_u32 io_error = 0;
    uae_u64 io_offset64;
    int async = 0;
    int bmask = dev->di.bytespersector - 1;
    struct device_info di;
    struct priv_devstruct *pdev = getpdevstruct (request);

    if (!pdev)
	return 0;
    command = get_word (request + 28);

    switch (command)
    {
	case CMD_READ:
	if ((io_offset & bmask) || bmask == 0 || io_data == 0)
	    goto bad_command;
	if ((io_length & bmask) || io_length == 0)
	    goto bad_len;
	if (dev->drivetype == INQ_ROMD)
	    io_error = command_cd_read (pdev->mode, dev, io_data, io_offset, io_length, &io_actual);
	else
	    io_error = command_read (pdev->mode, dev, io_data, io_offset, io_length, &io_actual);
	break;
	case TD_READ64:
	case NSCMD_TD_READ64:
	io_offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
	if ((io_offset64 & bmask) || bmask == 0 || io_data == 0)
	    goto bad_command;
	if ((io_length & bmask) || io_length == 0)
	    goto bad_len;
	if (dev->drivetype == INQ_ROMD)
	    io_error = command_cd_read (pdev->mode, dev, io_data, io_offset64, io_length, &io_actual);
	else
	    io_error = command_read (pdev->mode, dev, io_data, io_offset64, io_length, &io_actual);
	break;

	case CMD_WRITE:
	if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
	    io_error = 28; /* writeprotect */
	} else if ((io_offset & bmask) || bmask == 0 || io_data == 0) {
	    goto bad_command;
	} else if ((io_length & bmask) || io_length == 0) {
	    goto bad_len;
	} else {
	    io_error = command_write (pdev->mode, dev, io_data, io_offset, io_length, &io_actual);
	}
	break;
	case TD_WRITE64:
	case NSCMD_TD_WRITE64:
	io_offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
	if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
	    io_error = 28; /* writeprotect */
	} else if ((io_offset64 & bmask) || bmask == 0 || io_data == 0) {
	    goto bad_command;
	} else if ((io_length & bmask) || io_length == 0) {
	    goto bad_len;
	} else {
	    io_error = command_write (pdev->mode, dev, io_data, io_offset64, io_length, &io_actual);
	}
	break;

	case CMD_FORMAT:
	if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
	    io_error = 28; /* writeprotect */
	} else if ((io_offset & bmask) || bmask == 0 || io_data == 0) {
	    goto bad_command;
	} else if ((io_length & bmask) || io_length == 0) {
	    goto bad_len;
	} else {
	    io_error = command_write (pdev->mode, dev, io_data, io_offset, io_length, &io_actual);
	}
	break;
	case TD_FORMAT64:
	case NSCMD_TD_FORMAT64:
	io_offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
	if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
	    io_error = 28; /* writeprotect */
	} else if ((io_offset64 & bmask) || bmask == 0 || io_data == 0) {
	    goto bad_command;
	} else if ((io_length & bmask) || io_length == 0) {
	    goto bad_len;
	} else {
	    io_error = command_write (pdev->mode, dev, io_data, io_offset64, io_length, &io_actual);
	}
	break;

	case CMD_UPDATE:
	case CMD_CLEAR:
	case CMD_FLUSH:
	case CMD_MOTOR:
	case CMD_SEEK:
	io_actual = 0;
	break;
	case CMD_REMOVE:
	io_actual = 0;
	break;
	case CMD_CHANGENUM:
	io_actual = dev->changenum;
	break;
	case CMD_CHANGESTATE:
	io_actual = devinfo(pdev->mode, dev->unitnum, &di)->media_inserted ? 0 : 1;
	break;
	case CMD_PROTSTATUS:
	io_actual = devinfo(pdev->mode, dev->unitnum, &di)->write_protected ? -1 : 0;
	break;
	case CMD_GETDRIVETYPE:
	io_actual = dev->drivetype;
	break;
	case CMD_GETNUMTRACKS:
	io_actual = dev->di.cylinders;
	break;
	case CMD_GETGEOMETRY:
	{
	    struct device_info di2, *di;
	    di = devinfo (pdev->mode, dev->unitnum, &di2);
	    put_long (io_data + 0, di->bytespersector);
	    put_long (io_data + 4, di->sectorspertrack * di->trackspercylinder * di->cylinders);
	    put_long (io_data + 8, di->cylinders);
	    put_long (io_data + 12, di->sectorspertrack * di->trackspercylinder);
	    put_long (io_data + 16, di->trackspercylinder);
	    put_long (io_data + 20, di->sectorspertrack);
	    put_long (io_data + 24, 0); /* bufmemtype */
	    put_byte (io_data + 28, di->type);
	    put_byte (io_data + 29, di->removable ? 1 : 0); /* flags */
	}
	break;
	case CMD_ADDCHANGEINT:
	io_error = add_async_request (dev, request, ASYNC_REQUEST_CHANGEINT, io_data);
	if (!io_error)
	    async = 1;
	break;
	case CMD_REMCHANGEINT:
	release_async_request (dev, request);
	break;
	case HD_SCSICMD:
	if (dev->allow_scsi && pdev->scsi) {
	    uae_u32 sdd = get_long (request + 40);
	    io_error = sys_command_scsi_direct (dev->unitnum, sdd);
	    if (log_scsi)
		write_log ("scsidev: did io: sdd %p request %p error %d\n", sdd, request, get_byte (request + 31));
	} else {
	    io_error = IOERR_NOCMD;
	}
	break;
	case NSCMD_DEVICEQUERY:
	    put_long (io_data + 0, 0);
	    put_long (io_data + 4, 16); /* size */
	    put_word (io_data + 8, NSDEVTYPE_TRACKDISK);
	    put_word (io_data + 10, 0);
	    put_long (io_data + 12, nscmd_cmd);
	    io_actual = 16;
	break;
	default:
	io_error = IOERR_NOCMD;
	break;
	bad_len:
	io_error = IOERR_BADLENGTH;
	break;
	bad_command:
	io_error = IOERR_BADADDRESS;
	break;
    }
    put_long (request + 32, io_actual);
    put_byte (request + 31, io_error);
    io_log ("dev_io",request);
    return async;
}

static int dev_can_quick (uae_u32 command)
{
    switch (command)
    {
	case CMD_RESET:
	case CMD_STOP:
	case CMD_START:
	case CMD_CHANGESTATE:
	case CMD_PROTSTATUS:
	case CMD_GETDRIVETYPE:
	case CMD_GETNUMTRACKS:
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
	    write_log ("device %s command %d bug with IO_QUICK\n", getdevname (pdev->type), command);
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
	    if (log_scsi)
		write_log ("%s:%d async request %08X\n", getdevname(0), dev->unitnum, request);
	}
	uae_sem_post (&change_sem);
    }
    return 0;
}

static uae_u32 REGPARAM2 dev_init_2 (TrapContext *context, int type)
{
    uae_u32 base = m68k_dreg (&context->regs,0);
    if (log_scsi)
	write_log ("%s init\n", getdevname (type));
    return base;
}

static uae_u32 REGPARAM2 dev_init (TrapContext *context)
{
    return dev_init_2 (context, UAEDEV_SCSI_ID);
}
static uae_u32 REGPARAM2 diskdev_init (TrapContext *context)
{
    return dev_init_2 (context, UAEDEV_DISK_ID);
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
    put_byte (request + 31, IOERR_ABORTED);
    if (log_scsi)
	write_log ("abortio %s unit=%d, request=%08X\n", getdevname (pdev->type), pdev->unit, request);
    abort_async (dev, request, IOERR_ABORTED, 0);
    return 0;
}

#define BTL2UNIT(bus, target, lun) (2 * (bus) + (target) / 8) * 100 + (lun) * 10 + (target % 8)

static void dev_reset (void)
{
    int i, j;
    struct devstruct *dev;
    struct device_info *discsi, discsi2;
    int unitnum = 0;

    device_func_init (DEVICE_TYPE_SCSI);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	dev = &devst[i];
	if (dev->opencnt > 0) {
	    for (j = 0; j < MAX_ASYNC_REQUESTS; j++) {
		uaecptr request;
		if (request = dev->d_request[i])
		    abort_async (dev, request, 0, 0);
	    }
	    dev->opencnt = 1;
	    sys_command_close (DF_SCSI, dev->unitnum);
	    sys_command_close (DF_IOCTL, dev->unitnum);
	}
	memset (dev, 0, sizeof (struct devstruct));
	dev->unitnum = dev->aunit = -1;
    }
    for (i = 0; i < MAX_OPEN_DEVICES; i++)
	memset (&pdevst[i], 0, sizeof (struct priv_devstruct));

    i = j = 0;
    while (i < MAX_TOTAL_DEVICES) {
	dev = &devst[i];
	discsi = 0;
	if (sys_command_open (DF_SCSI, j)) {
	    discsi = sys_command_info (DF_SCSI, j, &discsi2);
	    sys_command_close (DF_SCSI, j);
	}
	if (discsi) {
	    dev->unitnum = j;
	    dev->allow_scsi = 1;
	    dev->drivetype = discsi->type;
	    memcpy (&dev->di, discsi, sizeof (struct device_info));
	    if (discsi->type == INQ_ROMD)
		dev->iscd = 1;
	}
	i++;
	j++;
    }
    unitnum = 0;
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	dev = &devst[i];
	if (dev->unitnum >= 0 && dev->iscd) {
	    dev->aunit = unitnum;
	    unitnum++;
	}
    }
    if (unitnum == 0)
	unitnum = 1;
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	dev = &devst[i];
	if (dev->unitnum >= 0) {
	    if (!dev->iscd) {
		dev->aunit = unitnum;
		unitnum++;
	    }
	    write_log ("%s:%d = '%s'\n", UAEDEV_SCSI, dev->aunit, dev->di.label);
	}
	xfree(dev->di.label);
	dev->di.label = NULL;
    }
}

static uaecptr ROM_scsidev_resname = 0,
    ROM_scsidev_resid = 0,
    ROM_scsidev_init = 0;

static uaecptr ROM_diskdev_resname = 0,
    ROM_diskdev_resid = 0,
    ROM_diskdev_init = 0;


static uaecptr diskdev_startup (uaecptr resaddr)
{
    /* Build a struct Resident. This will set up and initialize
     * the cd.device */
    if (log_scsi)
	write_log ("diskdev_startup(0x%x)\n", resaddr);
    put_word (resaddr + 0x0, 0x4AFC);
    put_long (resaddr + 0x2, resaddr);
    put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
    put_word (resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
    put_word (resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
    put_long (resaddr + 0xE, ROM_diskdev_resname);
    put_long (resaddr + 0x12, ROM_diskdev_resid);
    put_long (resaddr + 0x16, ROM_diskdev_init);
    resaddr += 0x1A;
    return resaddr;
}

uaecptr scsidev_startup (uaecptr resaddr)
{
    if (currprefs.scsi != 1)
	return resaddr;
    if (log_scsi)
	write_log ("scsidev_startup(0x%x)\n", resaddr);
    /* Build a struct Resident. This will set up and initialize
     * the uaescsi.device */
    put_word (resaddr + 0x0, 0x4AFC);
    put_long (resaddr + 0x2, resaddr);
    put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
    put_word (resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
    put_word (resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
    put_long (resaddr + 0xE, ROM_scsidev_resname);
    put_long (resaddr + 0x12, ROM_scsidev_resid);
    put_long (resaddr + 0x16, ROM_scsidev_init); /* calls scsidev_init */
    resaddr += 0x1A;
    return resaddr;
    return diskdev_startup (resaddr);
}

static void diskdev_install (void)
{
    uae_u32 functable, datatable;
    uae_u32 initcode, openfunc, closefunc, expungefunc;
    uae_u32 beginiofunc, abortiofunc;

    if (currprefs.scsi != 1)
	return;
    if (log_scsi)
	write_log ("diskdev_install(): 0x%x\n", here ());

    ROM_diskdev_resname = ds (UAEDEV_DISK);
    ROM_diskdev_resid = ds ("UAE disk.device 0.1");

    /* initcode */
    initcode = here ();
    calltrap (deftrap (diskdev_init)); dw (RTS);

    /* Open */
    openfunc = here ();
    calltrap (deftrap (diskdev_open)); dw (RTS);

    /* Close */
    closefunc = here ();
    calltrap (deftrap (diskdev_close)); dw (RTS);

    /* Expunge */
    expungefunc = here ();
    calltrap (deftrap (diskdev_expunge)); dw (RTS);

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
    dl (ROM_diskdev_resname);
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
    dl (ROM_diskdev_resid);
    dw (0x0000); /* end of table */

    ROM_diskdev_init = here ();
    dl (0x00000100); /* size of device base */
    dl (functable);
    dl (datatable);
    dl (initcode);
}


void scsidev_install (void)
{
    uae_u32 functable, datatable;
    uae_u32 initcode, openfunc, closefunc, expungefunc;
    uae_u32 beginiofunc, abortiofunc;

    if (currprefs.scsi != 1)
	return;
    if (log_scsi)
	write_log ("scsidev_install(): 0x%x\n", here ());

    ROM_scsidev_resname = ds (UAEDEV_SCSI);
    ROM_scsidev_resid = ds ("UAE scsi.device 0.2");

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
    dl (ROM_scsidev_resname);
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
    dl (ROM_scsidev_resid);
    dw (0x0000); /* end of table */

    ROM_scsidev_init = here ();
    dl (0x00000100); /* size of device base */
    dl (functable);
    dl (datatable);
    dl (initcode);

    nscmd_cmd = here ();
    dw (NSCMD_DEVICEQUERY);
    dw (CMD_RESET);
    dw (CMD_READ);
    dw (CMD_WRITE);
    dw (CMD_UPDATE);
    dw (CMD_CLEAR);
    dw (CMD_START);
    dw (CMD_STOP);
    dw (CMD_FLUSH);
    dw (CMD_MOTOR);
    dw (CMD_SEEK);
    dw (CMD_FORMAT);
    dw (CMD_REMOVE);
    dw (CMD_CHANGENUM);
    dw (CMD_CHANGESTATE);
    dw (CMD_PROTSTATUS);
    dw (CMD_GETDRIVETYPE);
    dw (CMD_GETGEOMETRY);
    dw (CMD_ADDCHANGEINT);
    dw (CMD_REMCHANGEINT);
    dw (HD_SCSICMD);
    dw (NSCMD_TD_READ64);
    dw (NSCMD_TD_WRITE64);
    dw (NSCMD_TD_SEEK64);
    dw (NSCMD_TD_FORMAT64);
    dw (0);

    diskdev_install ();
}

void scsidev_start_threads (void)
{
    if (currprefs.scsi != 1) /* quite useless.. */
	return;
    if (log_scsi)
	write_log ("scsidev_start_threads()\n");
    uae_sem_init (&change_sem, 0, 1);
}

void scsidev_reset (void)
{
    if (currprefs.scsi != 1)
	return;
    dev_reset ();
}
