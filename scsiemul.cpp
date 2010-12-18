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

#define UAEDEV_SCSI L"uaescsi.device"
#define UAEDEV_SCSI_ID 1
#define UAEDEV_DISK L"uaedisk.device"
#define UAEDEV_DISK_ID 2

#define MAX_ASYNC_REQUESTS 20
#define MAX_OPEN_DEVICES 20

#define ASYNC_REQUEST_NONE 0
#define ASYNC_REQUEST_TEMP 1
#define ASYNC_REQUEST_CHANGEINT 10
#define ASYNC_REQUEST_FRAMEINT 11

struct devstruct {
	int unitnum, aunit;
	int opencnt;
	int changenum;
	int drivetype;
	int iscd;
	volatile uaecptr d_request[MAX_ASYNC_REQUESTS];
	volatile int d_request_type[MAX_ASYNC_REQUESTS];
	volatile uae_u32 d_request_data[MAX_ASYNC_REQUESTS];
	struct device_info di;
	uaecptr changeint;
	int changeint_mediastate;

	int configblocksize;
	int volumelevel;
	int fadeframes;
	int fadetarget;
	int fadecounter;

	smp_comm_pipe requests;
	int thread_running;
	uae_sem_t sync_sem;
};

struct priv_devstruct {
	int inuse;
	int unit;
	int mode;
	int type;
	int flags; /* OpenDevice() */
};

static struct devstruct devst[MAX_TOTAL_SCSI_DEVICES];
static struct priv_devstruct pdevst[MAX_OPEN_DEVICES];
static uae_u32 nscmd_cmd;
static uae_sem_t change_sem;

static struct device_info *devinfo (struct devstruct *devst, struct device_info *di)
{
	struct device_info *dio = sys_command_info (devst->unitnum, di, 0);
	if (dio) {
		if (!devst->configblocksize)
			devst->configblocksize = dio->bytespersector;
	}	
	return dio;
}

static void io_log (const TCHAR *msg, uaecptr request)
{
	if (log_scsi)
		write_log (L"%s: %08X %d %08X %d %d io_actual=%d io_error=%d\n",
		msg, request, get_word (request + 28), get_long (request + 40),
		get_long (request + 36), get_long (request + 44),
		get_long (request + 32), get_byte (request + 31));
}

static struct devstruct *getdevstruct (int unit)
{
	int i;
	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (unit >= 0 && devst[i].aunit == unit)
			return &devst[i];
	}
	return 0;
}

static struct priv_devstruct *getpdevstruct (uaecptr request)
{
	int i = get_long (request + 24);
	if (i < 0 || i >= MAX_OPEN_DEVICES || pdevst[i].inuse == 0) {
		write_log (L"uaescsi.device: corrupt iorequest %08X %d\n", request, i);
		return 0;
	}
	return &pdevst[i];
}

static TCHAR *getdevname (int type)
{
	switch (type) {
	case UAEDEV_SCSI_ID:
		return UAEDEV_SCSI;
	case UAEDEV_DISK_ID:
		return UAEDEV_DISK;
	default:
		return L"NULL";
	}
}

static void *dev_thread (void *devs);
static int start_thread (struct devstruct *dev)
{
	if (dev->thread_running)
		return 1;
	init_comm_pipe (&dev->requests, 100, 1);
	uae_sem_init (&dev->sync_sem, 0, 0);
	uae_start_thread (L"uaescsi", dev_thread, dev, NULL);
	uae_sem_wait (&dev->sync_sem);
	return dev->thread_running;
}

static void dev_close_3 (struct devstruct *dev, struct priv_devstruct *pdev)
{
	if (!dev->opencnt) return;
	dev->opencnt--;
	if (!dev->opencnt) {
		sys_command_close (dev->unitnum);
		pdev->inuse = 0;
		write_comm_pipe_u32 (&dev->requests, 0, 1);
	}
}

static uae_u32 REGPARAM2 dev_close_2 (TrapContext *context)
{
	uae_u32 request = m68k_areg (regs, 1);
	struct priv_devstruct *pdev = getpdevstruct (request);
	struct devstruct *dev;

	if (!pdev)
		return 0;
	dev = getdevstruct (pdev->unit);
	if (log_scsi)
		write_log (L"%s:%d close, req=%08X\n", getdevname (pdev->type), pdev->unit, request);
	if (!dev)
		return 0;
	dev_close_3 (dev, pdev);
	put_long (request + 24, 0);
	put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) - 1);
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
	uaecptr ioreq = m68k_areg (regs, 1);
	uae_u32 unit = m68k_dreg (regs, 0);
	uae_u32 flags = m68k_dreg (regs, 1);
	struct devstruct *dev = getdevstruct (unit);
	struct priv_devstruct *pdev = 0;
	int i;

	if (log_scsi)
		write_log (L"opening %s:%d ioreq=%08X\n", getdevname (type), unit, ioreq);
	if (get_word (ioreq + 0x12) < IOSTDREQ_SIZE && get_word (ioreq + 0x12) > 0)
		return openfail (ioreq, IOERR_BADLENGTH);
	if (!dev)
		return openfail (ioreq, 32); /* badunitnum */
	if (!dev->opencnt) {
		for (i = 0; i < MAX_OPEN_DEVICES; i++) {
			pdev = &pdevst[i];
			if (pdev->inuse == 0)
				break;
		}
		if (!sys_command_open (dev->unitnum))
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

	put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) + 1);
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

#if 0
static int scsiemul_switchscsi (const TCHAR *name)
{
	struct devstruct *dev = NULL;
	struct device_info *discsi, discsi2;
	int i, opened[MAX_TOTAL_SCSI_DEVICES];
	bool wasopen = false;

	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++)
		opened[i] = sys_command_isopen (i);

	dev = &devst[0];
	if (dev->opencnt)
		wasopen = true;
	sys_command_close (dev->unitnum);

	dev = NULL;
	if (device_func_init (DEVICE_TYPE_ANY)) {
		if (devst[0].di.media_inserted < 0)
			devst[0].di.media_inserted = 0;
		i = 0;
		while (i < MAX_TOTAL_SCSI_DEVICES && dev == NULL) {
			discsi = 0;
			if (sys_command_open ( i)) {
				discsi = sys_command_info (i, &discsi2, 0);
				if (discsi && discsi->type == INQ_ROMD) {
					if (!_tcsicmp (currprefs.cdimagefile[0], discsi->label)) {
						dev = &devst[0];
						dev->unitnum = i;
						dev->drivetype = discsi->type;
						memcpy (&dev->di, discsi, sizeof (struct device_info));
						dev->iscd = 1;
						write_log (L"%s mounted as uaescsi.device:0\n", discsi->label);
						if (dev->di.media_inserted) {
							dev->di.media_inserted = 0;
							scsi_do_disk_change (dev->di.id, 1, NULL);
						}
					}
				}
				if (opened[i] == 0 && !wasopen)
					sys_command_close ( i);
			}
			i++;
		}
		if (dev)
			return dev->unitnum;
	}
	return -1;
}
#endif

// pollmode is 1 if no change interrupts found -> increase time of media change
int scsi_do_disk_change (int unitnum, int insert, int *pollmode)
{
	int i, j, ret;

	ret = -1;
	if (!change_sem)
		return ret;
	uae_sem_wait (&change_sem);
	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		struct devstruct *dev = &devst[i];
		if (dev->di.unitnum == unitnum + 1) {
			ret = i;
			if ((dev->changeint_mediastate > 0 && insert == 0) || (dev->changeint_mediastate <= 0 && insert)) {
				dev->changeint_mediastate = insert;
				if (pollmode)
					*pollmode = 1;
				if (dev->aunit >= 0) {
					struct priv_devstruct *pdev = &pdevst[dev->aunit];
					devinfo (dev, &dev->di);
				}
				dev->changenum++;
				j = 0;
				while (j < MAX_ASYNC_REQUESTS) {
					if (dev->d_request_type[j] == ASYNC_REQUEST_CHANGEINT) {
						uae_Cause (dev->d_request_data[j]);
						if (pollmode)
							*pollmode = 0;
					}
					j++;
				}
				if (dev->changeint) {
					uae_Cause (dev->changeint);
					if (pollmode)
						*pollmode = 0;
				}
			}
		}
	}
	uae_sem_post (&change_sem);
	return ret;
}

static int add_async_request (struct devstruct *dev, uaecptr request, int type, uae_u32 data)
{
	int i;

	if (log_scsi)
		write_log (L"async request %08x (%d) added\n", request, type);
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
		write_log (L"async request %p removed\n", request);
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
		write_log (L"asyncronous request=%08X aborted, error=%d\n", request, errcode);
}

static int command_read (struct devstruct *dev, uaecptr data, uae_u64 offset, uae_u32 length, uae_u32 *io_actual)
{
	int blocksize = dev->di.bytespersector;

	length /= blocksize;
	offset /= blocksize;
	while (length > 0) {
		uae_u8 buffer[4096];
		if (!sys_command_read (dev->unitnum, buffer, offset, 1))
			return 20;
		memcpyha_safe (data, buffer, blocksize);
		data += blocksize;
		offset++;
		length--;
	}
	return 0;
}

static int command_write (struct devstruct *dev, uaecptr data, uae_u64 offset, uae_u32 length, uae_u32 *io_actual)
{
	uae_u32 blocksize = dev->di.bytespersector;
	length /= blocksize;
	offset /= blocksize;
	while (length > 0) {
		uae_u8 buffer[4096];
		int err;
		memcpyah_safe (buffer, data, blocksize);
		err = sys_command_write (dev->unitnum, buffer, offset, 1);
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

static int command_cd_read (struct devstruct *dev, uaecptr data, uae_u64 offset, uae_u32 length, uae_u32 *io_actual)
{
	uae_u32 len, sector, startoffset;
	int blocksize;

	blocksize = dev->configblocksize;
	*io_actual = 0;
	startoffset = offset % blocksize;
	offset -= startoffset;
	sector = offset / blocksize;
	while (length > 0) {
		uae_u8 temp[4096];
		if (blocksize != 2048) {
			if (!sys_command_cd_rawread (dev->unitnum, temp, sector, 1, blocksize))
				return 20;
		} else {
			if (!sys_command_cd_read (dev->unitnum, temp, sector, 1))
				return 20;
		}
		if (startoffset > 0) {
			len = blocksize - startoffset;
			if (len > length) len = length;
			memcpyha_safe (data, temp + startoffset, len);
			length -= len;
			data += len;
			startoffset = 0;
			*io_actual += len;
		} else if (length >= blocksize) {
			len = blocksize;
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
	struct priv_devstruct *pdev = getpdevstruct (request);

	if (!pdev)
		return 0;
	command = get_word (request + 28);

//	write_log (L"%d: DATA=%08X LEN=%08X OFFSET=%08X ACTUAL=%08X\n",
//		command, io_data, io_length, io_offset, io_actual);

	switch (command)
	{
	case CMD_READ:
		//write_log (L"CMD_READ %08x %d %d %08x\n", io_data, io_offset, io_length, bmask);
		if (dev->di.media_inserted <= 0)
			goto no_media;
		if (dev->drivetype == INQ_ROMD) {
			io_error = command_cd_read (dev, io_data, io_offset, io_length, &io_actual);
		} else {
			if ((io_offset & bmask) || bmask == 0 || io_data == 0)
				goto bad_command;
			if ((io_length & bmask) || io_length == 0)
				goto bad_len;
			io_error = command_read (dev, io_data, io_offset, io_length, &io_actual);
		}
		break;
	case TD_READ64:
	case NSCMD_TD_READ64:
		if (dev->di.media_inserted <= 0)
			goto no_media;
		io_offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
		if ((io_offset64 & bmask) || bmask == 0 || io_data == 0)
			goto bad_command;
		if ((io_length & bmask) || io_length == 0)
			goto bad_len;
		if (dev->drivetype == INQ_ROMD)
			io_error = command_cd_read (dev, io_data, io_offset64, io_length, &io_actual);
		else
			io_error = command_read (dev, io_data, io_offset64, io_length, &io_actual);
		break;

	case CMD_WRITE:
		if (dev->di.media_inserted <= 0)
			goto no_media;
		if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
			io_error = 28; /* writeprotect */
		} else if ((io_offset & bmask) || bmask == 0 || io_data == 0) {
			goto bad_command;
		} else if ((io_length & bmask) || io_length == 0) {
			goto bad_len;
		} else {
			io_error = command_write (dev, io_data, io_offset, io_length, &io_actual);
		}
		break;
	case TD_WRITE64:
	case NSCMD_TD_WRITE64:
		if (dev->di.media_inserted <= 0)
			goto no_media;
		io_offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
		if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
			io_error = 28; /* writeprotect */
		} else if ((io_offset64 & bmask) || bmask == 0 || io_data == 0) {
			goto bad_command;
		} else if ((io_length & bmask) || io_length == 0) {
			goto bad_len;
		} else {
			io_error = command_write (dev, io_data, io_offset64, io_length, &io_actual);
		}
		break;

	case CMD_FORMAT:
		if (dev->di.media_inserted <= 0)
			goto no_media;
		if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
			io_error = 28; /* writeprotect */
		} else if ((io_offset & bmask) || bmask == 0 || io_data == 0) {
			goto bad_command;
		} else if ((io_length & bmask) || io_length == 0) {
			goto bad_len;
		} else {
			io_error = command_write (dev, io_data, io_offset, io_length, &io_actual);
		}
		break;
	case TD_FORMAT64:
	case NSCMD_TD_FORMAT64:
		if (dev->di.media_inserted <= 0)
			goto no_media;
		io_offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
		if (dev->di.write_protected || dev->drivetype == INQ_ROMD) {
			io_error = 28; /* writeprotect */
		} else if ((io_offset64 & bmask) || bmask == 0 || io_data == 0) {
			goto bad_command;
		} else if ((io_length & bmask) || io_length == 0) {
			goto bad_len;
		} else {
			io_error = command_write (dev, io_data, io_offset64, io_length, &io_actual);
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
		io_actual = dev->changeint;
		dev->changeint = io_data;
		dev->changeint_mediastate = dev->di.media_inserted;
		break;
	case CMD_CHANGENUM:
		io_actual = dev->changenum;
		break;
	case CMD_CHANGESTATE:
		if (dev->di.media_inserted >= 0) {
			io_actual = devinfo (dev, &dev->di)->media_inserted > 0 ? 0 : 1;
		} else {
			io_actual = 1;
		}
		break;
	case CMD_PROTSTATUS:
		io_actual = devinfo (dev, &dev->di)->write_protected ? -1 : 0;
		break;
	case CMD_GETDRIVETYPE:
		io_actual = dev->drivetype;
		break;
	case CMD_GETNUMTRACKS:
		if (dev->di.media_inserted <= 0)
			goto no_media;
		io_actual = dev->di.cylinders;
		break;
	case CMD_GETGEOMETRY:
		{
			struct device_info *di;
			di = devinfo (dev, &dev->di);
			if (di->media_inserted <= 0)
				goto no_media;
			put_long (io_data + 0, di->bytespersector);
			put_long (io_data + 4, di->sectorspertrack * di->trackspercylinder * di->cylinders);
			put_long (io_data + 8, di->cylinders);
			put_long (io_data + 12, di->sectorspertrack * di->trackspercylinder);
			put_long (io_data + 16, di->trackspercylinder);
			put_long (io_data + 20, di->sectorspertrack);
			put_long (io_data + 24, 0); /* bufmemtype */
			put_byte (io_data + 28, di->type);
			put_byte (io_data + 29, di->removable ? 1 : 0); /* flags */
			io_actual = 30;
		}
		break;
	case CMD_ADDCHANGEINT:
		dev->changeint_mediastate = dev->di.media_inserted;
		io_error = add_async_request (dev, request, ASYNC_REQUEST_CHANGEINT, io_data);
		if (!io_error)
			async = 1;
		break;
	case CMD_REMCHANGEINT:
		release_async_request (dev, request);
		break;

	case CD_TOCLSN:
	case CD_TOCMSF:
	{
		int msf = command == CD_TOCMSF;
		struct cd_toc_head toc;
		if (sys_command_cd_toc (dev->di.unitnum, &toc)) {
			if (io_offset == 0 && io_length > 0) {
				int pos = toc.lastaddress;
				put_byte (io_data, toc.first_track);
				put_byte (io_data + 1, toc.last_track);
				if (msf)
					pos = lsn2msf (pos);
				put_long (io_data + 2, pos);
				io_offset++;
				io_length--;
				io_data += 6;
				io_actual++;
			}
			for (int i = toc.first_track_offset; i < toc.last_track_offset && io_length > 0; i++) {
				if (io_offset == toc.toc[i].point) {
					int pos = toc.toc[i].paddress;
					put_byte (io_data, (toc.toc[i].control << 4) | toc.toc[i].adr);
					put_byte (io_data + 1, toc.toc[i].point);
					if (msf)
						pos = lsn2msf (pos);
					put_long (io_data + 2, pos);
					io_offset++;
					io_length--;
					io_data += 6;
					io_actual++;
				}
			}
		} else {
			io_error = IOERR_NotSpecified;
		}
	}
	break;
	case CD_ADDFRAMEINT:
		io_error = add_async_request (dev, request, ASYNC_REQUEST_FRAMEINT, io_data);
		if (!io_error)
			async = 1;
		break;
	case CD_REMFRAMEINT:
		release_async_request (dev, request);
		break;
	case CD_ATTENUATE:
	{
		if (io_offset != -1) {
			dev->fadeframes = io_length & 0x7fff;
			dev->fadetarget = io_offset & 0x7fff;
		}
		io_actual = dev->volumelevel;
	}
	break;
	case CD_INFO:
	{
		uae_u16 status = 0;
		struct cd_toc_head toc;
		uae_u8 subq[SUBQ_SIZE] = { 0 };
		sys_command_cd_qcode (dev->di.unitnum, subq);
		status |= 1 << 0; // door closed
		if (dev->di.media_inserted) {
			status |= 1 << 1;
			status |= 1 << 2; // motor on
			if (sys_command_cd_toc (dev->di.unitnum, &toc)) {
				status |= 1 << 3; // toc
				if (subq[1] == AUDIO_STATUS_IN_PROGRESS || subq[1] == AUDIO_STATUS_PAUSED)
					status |= 1 << 5; // audio play
				if (subq[1] == AUDIO_STATUS_PAUSED)
					status |= 1 << 6; // paused
				if (isdatatrack (&toc, 0))
					status |= 1 << 4; // data track
			}
		}
		put_word (io_data +  0, 75);		// PlaySpeed
		put_word (io_data +  2, 1200);		// ReadSpeed (randomly chose 16x)
		put_word (io_data +  4, 1200);		// ReadXLSpeed
		put_word (io_data +  6, dev->configblocksize); // SectorSize
		put_word (io_data +  8, -1);		// XLECC
		put_word (io_data + 10, 0);			// EjectReset
		put_word (io_data + 12, 0);			// Reserved * 4
		put_word (io_data + 14, 0);
		put_word (io_data + 16, 0);
		put_word (io_data + 18, 0);
		put_word (io_data + 20, 1200);		// MaxSpeed
		put_word (io_data + 22, 0xffff);	// AudioPrecision (volume)
		put_word (io_data + 24, status);	// Status
		put_word (io_data + 26, 0);			// Reserved2 * 4
		put_word (io_data + 28, 0);
		put_word (io_data + 30, 0);
		put_word (io_data + 32, 0);
		io_actual = 34;
	}
	break;
	case CD_CONFIG:
	{
		while (get_long (io_data) != TAG_DONE) {
			uae_u32 tag = get_long (io_data);
			uae_u32 data = get_long (io_data + 4);
			if (tag == 4) {
				// TAGCD_SECTORSIZE
				if (data == 2048 || data == 2336 || data == 2352)
					dev->configblocksize = data;
				else
					io_error = IOERR_BADADDRESS;

			}
			io_data += 8;
		}
		break;
	}
	case CD_PAUSE:
	{
		int old = sys_command_cd_pause (dev->di.unitnum, io_length);
		if (old >= 0)
			io_actual = old;
		else
			io_error = IOERR_BADADDRESS;
		break;
	}
	case CD_PLAYLSN:
	{
		int start = io_offset;
		int end = io_length + start;
		if (!sys_command_cd_play (dev->di.unitnum, start, end, 0))
			io_error = IOERR_BADADDRESS;
	}
	break;
	case CD_PLAYMSF:
	{
		int start = msf2lsn (io_offset);
		int end = msf2lsn (io_length) + start;
		if (!sys_command_cd_play (dev->di.unitnum, start, end, 0))
			io_error = IOERR_BADADDRESS;
	}
	break;
	case CD_PLAYTRACK:
	{
		struct cd_toc_head toc;
		int ok = 0;
		if (sys_command_cd_toc (dev->di.unitnum, &toc)) {
			for (int i = toc.first_track_offset; i < toc.last_track_offset; i++) {
				if (i == io_offset && i + io_length <= toc.last_track_offset) {
					ok = sys_command_cd_play (dev->di.unitnum, toc.toc[i].address, toc.toc[i + io_length].address, 0);
					break;
				}
			}
		}
		if (!ok)
			io_error = IOERR_BADADDRESS;
	}
	break;
	case CD_QCODEMSF:
	case CD_QCODELSN:
	{
		uae_u8 subq[SUBQ_SIZE];
		if (sys_command_cd_qcode (dev->di.unitnum, subq)) {
			if (subq[1] == AUDIO_STATUS_IN_PROGRESS || subq[1] == AUDIO_STATUS_PAUSED) {
				put_byte (io_data + 0, subq[4 + 0]);
				put_byte (io_data + 1, frombcd (subq[4 + 1]));
				put_byte (io_data + 2, frombcd (subq[4 + 2]));
				put_byte (io_data + 3, subq[4 + 6]);
				int trackpos = fromlongbcd (subq + 4 + 3);
				int diskpos = fromlongbcd (subq + 4 + 7);
				if (command == CD_QCODELSN) {
					trackpos = msf2lsn (trackpos);
					diskpos = msf2lsn (diskpos);
				}
				put_long (io_data + 4, trackpos);
				put_long (io_data + 8, diskpos);
				io_actual = 12;
			} else {
				io_error = IOERR_InvalidState;
			}
		} else {
			io_error = IOERR_BADADDRESS;
		}
	}
	break;

	case HD_SCSICMD:
		{
			uae_u32 sdd = get_long (request + 40);
			io_error = sys_command_scsi_direct (dev->unitnum, sdd);
			if (log_scsi)
				write_log (L"scsidev: did io: sdd %p request %p error %d\n", sdd, request, get_byte (request + 31));
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
no_media:
		io_error = TDERR_DiskChanged;
		break;
	}
	put_long (request + 32, io_actual);
	put_byte (request + 31, io_error);
	io_log (L"dev_io",request);
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
	uae_u32 request = m68k_areg (regs, 1);
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
		if (dev_do_io (dev, request))
			write_log (L"device %s command %d bug with IO_QUICK\n", getdevname (pdev->type), command);
		return get_byte (request + 31);
	} else {
		add_async_request (dev, request, ASYNC_REQUEST_TEMP, 0);
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
		} else if (dev_do_io (dev, request) == 0) {
			put_byte (request + 30, get_byte (request + 30) & ~1);
			release_async_request (dev, request);
			uae_ReplyMsg (request);
		} else {
			if (log_scsi)
				write_log (L"%s:%d async request %08X\n", getdevname(0), dev->unitnum, request);
		}
		uae_sem_post (&change_sem);
	}
	return 0;
}

static uae_u32 REGPARAM2 dev_init_2 (TrapContext *context, int type)
{
	uae_u32 base = m68k_dreg (regs,0);
	if (log_scsi)
		write_log (L"%s init\n", getdevname (type));
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
	uae_u32 request = m68k_areg (regs, 1);
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
		write_log (L"abortio %s unit=%d, request=%08X\n", getdevname (pdev->type), pdev->unit, request);
	abort_async (dev, request, IOERR_ABORTED, 0);
	return 0;
}

#define BTL2UNIT(bus, target, lun) (2 * (bus) + (target) / 8) * 100 + (lun) * 10 + (target % 8)

static void dev_reset (void)
{
	int i, j;
	struct devstruct *dev;
	int unitnum = 0;

	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		dev = &devst[i];
		if (dev->opencnt > 0) {
			for (j = 0; j < MAX_ASYNC_REQUESTS; j++) {
				uaecptr request;
				if (request = dev->d_request[i])
					abort_async (dev, request, 0, 0);
			}
			dev->opencnt = 1;
			sys_command_close (dev->unitnum);
		}
		memset (dev, 0, sizeof (struct devstruct));
		dev->unitnum = dev->aunit = -1;
	}
	for (i = 0; i < MAX_OPEN_DEVICES; i++)
		memset (&pdevst[i], 0, sizeof (struct priv_devstruct));

	device_func_init (0);
	i = 0;
	while (i < MAX_TOTAL_SCSI_DEVICES) {
		dev = &devst[i];
		struct device_info *discsi, discsi2;
		if (sys_command_open (i)) {
			discsi = sys_command_info (i, &discsi2, 0);
			if (discsi) {
				dev->unitnum = i;
				dev->drivetype = discsi->type;
				memcpy (&dev->di, discsi, sizeof (struct device_info));
				dev->changeint_mediastate = discsi->media_inserted;
				dev->configblocksize = discsi->bytespersector;
				if (discsi->type == INQ_ROMD)
					dev->iscd = 1;
			}
		}
		i++;
	}
	unitnum = 0;
	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		dev = &devst[i];
		if (dev->unitnum >= 0)
			sys_command_close (dev->unitnum);
		if (dev->unitnum >= 0 && dev->iscd) {
			dev->aunit = unitnum;
			dev->volumelevel = 0x7fff;
			unitnum++;
		}
	}
	if (unitnum == 0)
		unitnum = 1;
	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		dev = &devst[i];
		if (dev->unitnum >= 0) {
			if (!dev->iscd) {
				dev->aunit = unitnum;
				unitnum++;
			}
			write_log (L"%s:%d = %s:'%s'\n", UAEDEV_SCSI, dev->aunit, dev->di.backend, dev->di.label);
		}
		dev->di.label[0] = 0;
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
		write_log (L"diskdev_startup(0x%x)\n", resaddr);
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
		write_log (L"scsidev_startup(0x%x)\n", resaddr);
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
		write_log (L"diskdev_install(): 0x%x\n", here ());

	ROM_diskdev_resname = ds (UAEDEV_DISK);
	ROM_diskdev_resid = ds (L"UAE disk.device 0.1");

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
		write_log (L"scsidev_install(): 0x%x\n", here ());

	ROM_scsidev_resname = ds (UAEDEV_SCSI);
	ROM_scsidev_resid = ds (L"UAE scsi.device 0.2");

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
		write_log (L"scsidev_start_threads()\n");
	uae_sem_init (&change_sem, 0, 1);
}

void scsidev_reset (void)
{
	if (currprefs.scsi != 1)
		return;
	dev_reset ();
}
