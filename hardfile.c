 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Hardfile emulation
  *
  * Copyright 1995 Bernd Schmidt
  *           2002 Toni Wilen (scsi emulation, 64-bit support)
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "disk.h"
#include "autoconf.h"
#include "filesys.h"
#include "execlib.h"
#include "native2amiga.h"
#include "gui.h"
#include "uae.h"

#define CMD_INVALID	0
#define CMD_RESET	1
#define CMD_READ	2
#define CMD_WRITE	3
#define CMD_UPDATE	4
#define CMD_CLEAR	5
#define CMD_STOP	6
#define CMD_START	7
#define CMD_FLUSH	8
#define CMD_MOTOR	9
#define CMD_SEEK	10
#define CMD_FORMAT	11
#define CMD_REMOVE	12
#define CMD_CHANGENUM	13
#define CMD_CHANGESTATE	14
#define CMD_PROTSTATUS	15
#define CMD_GETDRIVETYPE 18
#define CMD_GETNUMTRACKS 19
#define CMD_ADDCHANGEINT 20
#define CMD_REMCHANGEINT 21
#define CMD_GETGEOMETRY	22
#define HD_SCSICMD 28

/* Trackdisk64 support */
#define TD_READ64 24
#define TD_WRITE64 25
#define TD_SEEK64 26
#define TD_FORMAT64 27

/* New Style Devices (NSD) support */
#define DRIVE_NEWSTYLE 0x4E535459L   /* 'NSTY' */
#define NSCMD_DEVICEQUERY 0x4000
#define NSCMD_TD_READ64 0xc000
#define NSCMD_TD_WRITE64 0xc001
#define NSCMD_TD_SEEK64 0xc002
#define NSCMD_TD_FORMAT64 0xc003

#undef DEBUGME
//#define DEBUGME
#ifdef DEBUGME
#define hf_log write_log
#define hf_log2 write_log
#define scsi_log write_log
#else
#define hf_log
#define hf_log2
#define scsi_log
#endif

#define MAX_ASYNC_REQUESTS 50
#define ASYNC_REQUEST_NONE 0
#define ASYNC_REQUEST_TEMP 1
#define ASYNC_REQUEST_CHANGEINT 10

static struct hardfileprivdata {
    volatile uaecptr d_request[MAX_ASYNC_REQUESTS];
    volatile int d_request_type[MAX_ASYNC_REQUESTS];
    volatile uae_u32 d_request_data[MAX_ASYNC_REQUESTS];
    smp_comm_pipe requests;
    uae_thread_id tid;
    int thread_running;
    uae_sem_t sync_sem;
    int opencount;
};

static uae_sem_t change_sem;

static struct hardfileprivdata hardfpd[MAX_FILESYSTEM_UNITS];

static uae_u32 nscmd_cmd;

static void wl (uae_u8 *p, int v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}
static int rl (uae_u8 *p)
{
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}

static uae_u64 cmd_read (struct hardfiledata *hfd, uaecptr dataptr, uae_u64 offset, uae_u64 len)
{
    uae_u64 got = 0;
    uae_u8 buffer[FILESYS_MAX_BLOCKSIZE];

    gui_hd_led (1);
    hf_log2 ("cmd_read: %p %04.4x-%08.8x %08.8x\n", dataptr, (uae_u32)(offset >> 32), (uae_u32)offset, (uae_u32)len);
    while (len > 0) {
        int i, got2;
	got2 = hdf_read (hfd, buffer, offset, hfd->blocksize);
        if (got2 != hfd->blocksize)
	    break;
	for (i = 0; i < got2; i++)
	    put_byte(dataptr + i, buffer[i]);
        len -= got2;
        got += got2;
        dataptr += got2;
	offset += got2;
    }
    return got;
}

static uae_u64 cmd_write (struct hardfiledata *hfd, uaecptr dataptr, uae_u64 offset, uae_u64 len)
{
    uae_u64 got = 0;
    uae_u8 buffer[FILESYS_MAX_BLOCKSIZE];

    gui_hd_led (1);
    hf_log2 ("cmd_write: %p %04.4x-%08.8x %08.8x\n", dataptr, (uae_u32)(offset >> 32), (uae_u32)offset, (uae_u32)len);
    while (len > 0) {
        int i, got2;
        for (i = 0; i < hfd->blocksize; i++)
	    buffer[i] = get_byte (dataptr + i);
	got2 = hdf_write (hfd, buffer, offset, hfd->blocksize);
	if (got2 != hfd->blocksize)
	    break;
	len -= got2;
	got += got2;
	dataptr += got2;
	offset += got2;
    }
    return got;
}

static int handle_scsi (uaecptr request, struct hardfiledata *hfd)
{
    uae_u32 acmd = get_long (request + 40);
    uaecptr scsi_data = get_long (acmd + 0);
    uae_u32 scsi_len = get_long (acmd + 4);
    uaecptr scsi_cmd = get_long (acmd + 12);
    uae_u16 scsi_cmd_len = get_word (acmd + 16);
    uae_u8 scsi_flags = get_byte (acmd + 20);
    uaecptr scsi_sense = get_long (acmd + 22);
    uae_u16 scsi_sense_len = get_word (acmd + 26);
    uae_u8 cmd = get_byte (scsi_cmd);
    uae_u8 cmdbuf[256];
    int status, ret = 0, lr, ls;
    uae_u32 i;
    uae_u8 r[256], s[256];
    uae_u64 len, offset;

    scsi_sense_len  = (scsi_flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
        (scsi_flags & 2) ? scsi_sense_len : /* SCSIF_AUTOSENSE */
        32;
    status = 0;
    memset (r, 0, sizeof (r));
    lr = 0; ls = 0;
    scsi_log ("hdf scsiemu: cmd=%02.2X,%d flags=%02.2X sense=%p,%d data=%p,%d\n",
	cmd, scsi_cmd_len, scsi_flags, scsi_sense, scsi_sense_len, scsi_data, scsi_len);
    for (i = 0; i < scsi_cmd_len; i++) {
	cmdbuf[i] = get_byte (scsi_cmd + i);
	scsi_log ("%02.2X%c", get_byte (scsi_cmd + i), i < scsi_cmd_len - 1 ? '.' : ' ');
    }
    scsi_log ("\n");

    switch (cmd)
    {
	case 0x00: /* TEST UNIT READY */
	break;
	case 0x08: /* READ (6) */
	offset = ((cmdbuf[1] & 31) << 16) | (cmdbuf[2] << 8) | cmdbuf[3];
	offset *= hfd->blocksize;
	len = cmdbuf[4];
	if (!len) len = 256;
	len *= hfd->blocksize;
	scsi_len = (uae_u32)cmd_read (hfd, scsi_data, offset, len);
        break;
	case 0x0a: /* WRITE (6) */
	offset = ((cmdbuf[1] & 31) << 16) | (cmdbuf[2] << 8) | cmdbuf[3];
	offset *= hfd->blocksize;
	len = cmdbuf[4];
	if (!len) len = 256;
	len *= hfd->blocksize;
	scsi_len = (uae_u32)cmd_write (hfd, scsi_data, offset, len);
        break;
	case 0x12: /* INQUIRY */
	len = cmdbuf[4];
	r[2] = 2; /* supports SCSI-2 */
	r[3] = 2; /* response data format */
	r[4] = 32; /* additional length */
	r[7] = 0x20; /* 16 bit bus */
	i = 0; /* vendor id */
	while (i < 8 && hfd->vendor_id[i]) {
	    r[8 + i] = hfd->vendor_id[i];
	    i++;
	}
	while (i < 8) {
	    r[8 + i] = 32;
	    i++;
	}
	i = 0; /* product id */
	while (i < 16 && hfd->product_id[i]) {
	    r[16 + i] = hfd->product_id[i];
	    i++;
	}
	while (i < 16) {
	    r[16 + i] = 32;
	    i++;
	}
	i = 0; /* product revision */
	while (i < 4 && hfd->product_rev[i]) {
	    r[32 + i] = hfd->product_rev[i];
	    i++;
	}
	while (i < 4) {
	    r[32 + i] = 32;
	    i++;
	}
	scsi_len = lr = len < 36 ? (uae_u32)len : 36;
	break;
	case 0x25: /* READ_CAPACITY */
	wl (r, (uae_u32)(hfd->size / hfd->blocksize - 1));
	wl (r + 4, hfd->blocksize);
	scsi_len = lr = 8;
	break;
	case 0x28: /* READ (10) */
	offset = rl (cmdbuf + 2);
	offset *= hfd->blocksize;
        len = rl (cmdbuf + 7 - 2) & 0xffff;
	len *= hfd->blocksize;
	scsi_len = (uae_u32)cmd_read (hfd, scsi_data, offset, len);
	break;
	case 0x2a: /* WRITE (10) */
	offset = rl (cmdbuf + 2);
	offset *= hfd->blocksize;
        len = rl (cmdbuf + 7 - 2) & 0xffff;
	len *= hfd->blocksize;
	scsi_len = (uae_u32)cmd_write (hfd, scsi_data, offset, len);
        break;
	case 0xa8: /* READ (12) */
	offset = rl (cmdbuf + 2);
	offset *= hfd->blocksize;
        len = rl (cmdbuf + 6);
	len *= hfd->blocksize;
	scsi_len = (uae_u32)cmd_read (hfd, scsi_data, offset, len);
	break;
	case 0xaa: /* WRITE (12) */
	offset = rl (cmdbuf + 2);
	offset *= hfd->blocksize;
        len = rl (cmdbuf + 6);
	len *= hfd->blocksize;
	scsi_len = (uae_u32)cmd_write (hfd, scsi_data, offset, len);
        break;
	default:
	lr = -1;
	write_log ("unsupported scsi command 0x%02.2X\n", cmd);
	status = 2; /* CHECK CONDITION */
	s[0] = 0x70;
	s[2] = 5; /* ILLEGAL REQUEST */
	s[12] = 0x24; /* ILLEGAL FIELD IN CDB */
	ls = 12;
	break;
    }
    put_word (acmd + 18, status != 0 ? 0 : scsi_cmd_len); /* fake scsi_CmdActual */
    put_byte (acmd + 21, status); /* scsi_Status */
    if (lr > 0) {
	scsi_log ("RD:");
	i = 0;
	while (i < lr && i < scsi_len) {
	    if (i < 24)
		scsi_log ("%02.2X%c", r[i], i < scsi_len - 1 ? '.' : ' ');
	    put_byte (scsi_data + i, r[i]);
	    i++;
	}
	scsi_log ("\n");
    }
    i = 0;
    if (ls > 0 && scsi_sense) {
	while (i < ls && i < scsi_sense_len) {
	    put_byte (scsi_sense + i, s[i]);
	    i++;
	}
    }
    while (i < scsi_sense_len && scsi_sense) {
	put_byte (scsi_sense + i, 0);
	i++;
    }
    if (lr < 0) {
	put_long (acmd + 8, 0); /* scsi_Actual */
	ret = 20;
    } else {
	put_long (acmd + 8, scsi_len); /* scsi_Actual */
    }
    return ret;
}

static int add_async_request (struct hardfileprivdata *hfpd, uaecptr request, int type, uae_u32 data)
{
    int i;

    i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (hfpd->d_request[i] == request) {
	    hfpd->d_request_type[i] = type;
	    hfpd->d_request_data[i] = data;
	    hf_log ("old async request %p (%d) added\n", request, type);
	    return 0;
	}
	i++;
    }
    i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (hfpd->d_request[i] == 0) {
	    hfpd->d_request[i] = request;
	    hfpd->d_request_type[i] = type;
	    hfpd->d_request_data[i] = data;
	    hf_log ("async request %p (%d) added (total=%d)\n", request, type, i);
	    return 0;
	}
	i++;
    }
    hf_log ("async request overflow %p!\n", request);
    return -1;
}

static int release_async_request (struct hardfileprivdata *hfpd, uaecptr request)
{
    int i = 0;

    while (i < MAX_ASYNC_REQUESTS) {
	if (hfpd->d_request[i] == request) {
	    int type = hfpd->d_request_type[i];
	    hfpd->d_request[i] = 0;
	    hfpd->d_request_data[i] = 0;
	    hfpd->d_request_type[i] = 0;
	    hf_log ("async request %p removed\n", request);
	    return type;
	}
	i++;
    }
    hf_log ("tried to remove non-existing request %p\n", request);
    return -1;
}

static void abort_async (struct hardfileprivdata *hfpd, uaecptr request, int errcode, int type)
{
    int i;
    hf_log ("aborting async request %p\n", request);
    i = 0;
    while (i < MAX_ASYNC_REQUESTS) {
	if (hfpd->d_request[i] == request && hfpd->d_request_type[i] == ASYNC_REQUEST_TEMP) {
	    /* ASYNC_REQUEST_TEMP = request is processing */
	    sleep_millis (1);
	    i = 0;
	    continue;
	}
	i++;
    }
    i = release_async_request (hfpd, request);
    if (i >= 0)
	hf_log ("asyncronous request=%08.8X aborted, error=%d\n", request, errcode);
}

static void *hardfile_thread (void *devs);
static int start_thread (int unit)
{
    struct hardfileprivdata *hfpd = &hardfpd[unit];

    if (hfpd->thread_running)
        return 1;
    memset (hfpd, 0, sizeof (struct hardfileprivdata));
    init_comm_pipe (&hfpd->requests, 100, 1);
    uae_sem_init (&hfpd->sync_sem, 0, 0);
    uae_start_thread (hardfile_thread, hfpd, &hfpd->tid);
    uae_sem_wait (&hfpd->sync_sem);
    return hfpd->thread_running;
}

static int mangleunit (int unit)
{
    if (unit <= 99)
	return unit;
    if (unit == 100)
	return 8;
    if (unit == 110)
	return 9;
    return -1;
}

static uae_u32 hardfile_open (void)
{
    uaecptr tmp1 = m68k_areg(regs, 1); /* IOReq */
    int unit = mangleunit (m68k_dreg (regs, 0));
    struct hardfileprivdata *hfpd = &hardfpd[unit];

    hf_log ("hardfile_open, unit %d (%d)\n", unit, m68k_dreg (regs, 0));
    /* Check unit number */
    if (unit >= 0 && get_hardfile_data (unit) && start_thread (unit)) {
	hfpd->opencount++;
	put_word (m68k_areg(regs, 6) + 32, get_word (m68k_areg(regs, 6) + 32) + 1);
	put_long (tmp1 + 24, unit); /* io_Unit */
	put_byte (tmp1 + 31, 0); /* io_Error */
	put_byte (tmp1 + 8, 7); /* ln_type = NT_REPLYMSG */
	return 0;
    }

    put_long (tmp1 + 20, (uae_u32)-1);
    put_byte (tmp1 + 31, (uae_u8)-1);
    return (uae_u32)-1;
}

static uae_u32 hardfile_close (void)
{
    uaecptr request = m68k_areg(regs, 1); /* IOReq */
    int unit = mangleunit (get_long (request + 24));
    struct hardfileprivdata *hfpd = &hardfpd[unit];

    if (!hfpd->opencount) return 0;
    hfpd->opencount--;
    if (hfpd->opencount == 0)
	write_comm_pipe_u32 (&hfpd->requests, 0, 1);
    put_word (m68k_areg(regs, 6) + 32, get_word (m68k_areg(regs, 6) + 32) - 1);
    return 0;
}

static uae_u32 hardfile_expunge (void)
{
    return 0; /* Simply ignore this one... */
}

static uae_u32 hardfile_do_io (struct hardfiledata *hfd, struct hardfileprivdata *hfpd, uaecptr request)
{
    uae_u32 dataptr, offset, actual = 0, cmd;
    uae_u64 offset64;
    int unit = get_long (request + 24);
    uae_u32 error = 0, len;
    int async = 0;
    int bmask = hfd->blocksize - 1;

    cmd = get_word (request + 28); /* io_Command */
    dataptr = get_long (request + 40);
    switch (cmd)
    {
	case CMD_READ:
	if (dataptr & 1)
	    goto bad_command;
	offset = get_long (request + 44);
	if (offset & bmask)
	    goto bad_command;
	len = get_long (request + 36); /* io_Length */
	if (len & bmask)
	    goto bad_command;
	if (len + offset > hfd->size)
	    goto bad_command;
	actual = (uae_u32)cmd_read (hfd, dataptr, offset, len);
	break;

	case TD_READ64:
	case NSCMD_TD_READ64:
	if (dataptr & 1)
	    goto bad_command;
	offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
	if (offset64 & bmask)
	    goto bad_command;
	len = get_long (request + 36); /* io_Length */
	if (len & bmask)
	    goto bad_command;
	if (len + offset64 > hfd->size)
	    goto bad_command;
	actual = (uae_u32)cmd_read (hfd, dataptr, offset64, len);
	break;

	case CMD_WRITE:
	case CMD_FORMAT: /* Format */
	if (hfd->readonly) {
	    error = 28; /* write protect */
	} else {
	    if (dataptr & 1)
		goto bad_command;
	    offset = get_long (request + 44);
	    if (offset & bmask)
		goto bad_command;
	    len = get_long (request + 36); /* io_Length */
	    if (len & bmask)
		goto bad_command;
	    if (len + offset > hfd->size)
		goto bad_command;
	    actual = (uae_u32)cmd_write (hfd, dataptr, offset, len);
	}
	break;

	case TD_WRITE64:
	case TD_FORMAT64:
	case NSCMD_TD_WRITE64:
	case NSCMD_TD_FORMAT64:
	if (hfd->readonly) {
	    error = 28; /* write protect */
	} else {
	    if (dataptr & 1)
		goto bad_command;
	    offset64 = get_long (request + 44) | ((uae_u64)get_long (request + 32) << 32);
	    if (offset64 & bmask)
		goto bad_command;
	    len = get_long (request + 36); /* io_Length */
	    if (len & bmask)
		goto bad_command;
	    if (len + offset64 > hfd->size)
		goto bad_command;
	    put_long (request + 32, (uae_u32)cmd_write (hfd, dataptr, offset64, len));
	}
	break;

	bad_command:
	break;

	case NSCMD_DEVICEQUERY:
	    put_long (dataptr + 4, 16); /* size */
	    put_word (dataptr + 8, 5); /* NSDEVTYPE_TRACKDISK */
	    put_word (dataptr + 10, 0);
	    put_long (dataptr + 12, nscmd_cmd);
	    actual = 16;
	break;

	case CMD_GETDRIVETYPE:
	    actual = DRIVE_NEWSTYLE;
	    break;

	case CMD_GETNUMTRACKS:
	    hf_log ("CMD_GETNUMTRACKS - shouldn't happen\n");
	    actual = 0;
	    break;

	case CMD_PROTSTATUS:
	    if (hfd->readonly)
		actual = -1;
	    else
		actual = 0;
	break;

	case CMD_CHANGESTATE:
	actual = 0;
	break;

	/* Some commands that just do nothing and return zero */
	case CMD_UPDATE:
	case CMD_CLEAR:
	case CMD_MOTOR:
	case CMD_SEEK:
	case CMD_CHANGENUM:
	case TD_SEEK64:
	case NSCMD_TD_SEEK64:
	break;

	case CMD_REMOVE:
	break;

	case CMD_ADDCHANGEINT:
	error = add_async_request (hfpd, request, ASYNC_REQUEST_CHANGEINT, get_long (request + 40));
	if (!error)
	    async = 1;
	break;
	case CMD_REMCHANGEINT:
	release_async_request (hfpd, request);
	break;
 
	case HD_SCSICMD: /* SCSI */
	    if (hfd->nrcyls == 0)
		error = handle_scsi (request, hfd);
	    else /* we don't want users trashing their "partition" hardfiles with hdtoolbox */
		error = -3; /* io_Error */
	break;

	default:
	    /* Command not understood. */
	    error = -3; /* io_Error */
	break;
    }
    put_long (request + 32, actual);
    put_byte (request + 31, error);

    hf_log2 ("hf: unit=%d, request=%p, cmd=%d offset=%u len=%d, actual=%d error%=%d\n", unit, request,
	get_word(request + 28), get_long (request + 44), get_long (request + 36), actual, error);
 
    return async;
}

static uae_u32 hardfile_abortio (void)
{
    uae_u32 request = m68k_areg(regs, 1);
    int unit = mangleunit (get_long (request + 24));
    struct hardfiledata *hfd = get_hardfile_data (unit);
    struct hardfileprivdata *hfpd = &hardfpd[unit];

    hf_log2 ("uaehf.device abortio ");
    if (!hfd) {
	put_byte (request + 31, 32);
	hf_log2 ("error\n");
	return get_byte (request + 31);
    }
    put_byte (request + 31, -2);
    hf_log2 ("unit=%d, request=%08.8X\n",  unit, request);
    abort_async (hfpd, request, -2, 0);
    return 0;
}

static int hardfile_can_quick (uae_u32 command)
{
    switch (command)
    {
	case CMD_RESET:
	case CMD_STOP:
	case CMD_START:
	case CMD_CHANGESTATE:
	case CMD_PROTSTATUS:
	case CMD_MOTOR:
	case CMD_GETDRIVETYPE:
	case CMD_GETNUMTRACKS:
	case NSCMD_DEVICEQUERY:
	return 1;
    }
    return 0;
}

static int hardfile_canquick (struct hardfiledata *hfd, uaecptr request)
{
    uae_u32 command = get_word (request + 28);
    return hardfile_can_quick (command);
}

static uae_u32 hardfile_beginio (void)
{
    uae_u32 request = m68k_areg(regs, 1);
    uae_u8 flags = get_byte (request + 30);
    int cmd = get_word (request + 28);
    int unit = mangleunit (get_long (request + 24));
    struct hardfiledata *hfd = get_hardfile_data (unit);
    struct hardfileprivdata *hfpd = &hardfpd[unit];

    put_byte (request + 8, NT_MESSAGE);
    if (!hfd) {
	put_byte (request + 31, 32);
	return get_byte (request + 31);
    }
    put_byte (request + 31, 0);
    if ((flags & 1) && hardfile_canquick (hfd, request)) {
        hf_log ("hf quickio unit=%d request=%p cmd=%d\n", unit, request, cmd);
	if (hardfile_do_io (hfd, hfpd, request))
	    hf_log2 ("uaehf.device cmd %d bug with IO_QUICK\n", cmd);
	return get_byte (request + 31);
    } else {
        hf_log2 ("hf asyncio unit=%d request=%p cmd=%d\n", unit, request, cmd);
        add_async_request (hfpd, request, ASYNC_REQUEST_TEMP, 0);
        put_byte (request + 30, get_byte (request + 30) & ~1);
	write_comm_pipe_u32 (&hfpd->requests, request, 1);
 	return 0;
    }
}

static void *hardfile_thread (void *devs)
{
    struct hardfileprivdata *hfpd = devs;

    set_thread_priority (2);
    hfpd->thread_running = 1;
    uae_sem_post (&hfpd->sync_sem);
    for (;;) {
	uaecptr request = (uaecptr)read_comm_pipe_u32_blocking (&hfpd->requests);
	uae_sem_wait (&change_sem);
        if (!request) {
	    hfpd->thread_running = 0;
	    uae_sem_post (&hfpd->sync_sem);
	    uae_sem_post (&change_sem);
	    return 0;
	} else if (hardfile_do_io (get_hardfile_data (hfpd - &hardfpd[0]), hfpd, request) == 0) {
            put_byte (request + 30, get_byte (request + 30) & ~1);
	    release_async_request (hfpd, request);
            uae_ReplyMsg (request);
	} else {
	    hf_log2 ("async request %08.8X\n", request);
	}
	uae_sem_post (&change_sem);
    }
}

void hardfile_reset (void)
{
    int i, j;
    struct hardfileprivdata *hfpd;

    for (i = 0; i < MAX_FILESYSTEM_UNITS; i++) {
	 hfpd = &hardfpd[i];
	if (hfpd->opencount > 0) {
	    for (j = 0; j < MAX_ASYNC_REQUESTS; j++) {
	        uaecptr request;
		if (request = hfpd->d_request[i]) 
		    abort_async (hfpd, request, 0, 0);
	    }
	}
	memset (hfpd, 0, sizeof (struct hardfileprivdata));
    }
}

void hardfile_install (void)
{
    uae_u32 functable, datatable;
    uae_u32 initcode, openfunc, closefunc, expungefunc;
    uae_u32 beginiofunc, abortiofunc;

    uae_sem_init (&change_sem, 0, 1);

    ROM_hardfile_resname = ds ("uaehf.device");
    ROM_hardfile_resid = ds ("UAE hardfile.device 0.2");

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
    dw (CMD_ADDCHANGEINT);
    dw (CMD_REMCHANGEINT);
    dw (HD_SCSICMD);
    dw (NSCMD_TD_READ64);
    dw (NSCMD_TD_WRITE64);
    dw (NSCMD_TD_SEEK64);
    dw (NSCMD_TD_FORMAT64);
    dw (0);

    /* initcode */
#if 0
    initcode = here ();
    calltrap (deftrap (hardfile_init)); dw (RTS);
#else
    initcode = filesys_initcode;
#endif
    /* Open */
    openfunc = here ();
    calltrap (deftrap (hardfile_open)); dw (RTS);

    /* Close */
    closefunc = here ();
    calltrap (deftrap (hardfile_close)); dw (RTS);

    /* Expunge */
    expungefunc = here ();
    calltrap (deftrap (hardfile_expunge)); dw (RTS);

    /* BeginIO */
    beginiofunc = here ();
    calltrap (deftrap (hardfile_beginio));
    dw (RTS);

    /* AbortIO */
    abortiofunc = here ();
    calltrap (deftrap (hardfile_abortio)); dw (RTS);

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
    dl (ROM_hardfile_resname);
    dw (0xE000); /* INITBYTE */
    dw (0x000E); /* LIB_FLAGS */
    dw (0x0600); /* LIBF_SUMUSED | LIBF_CHANGED */
    dw (0xD000); /* INITWORD */
    dw (0x0014); /* LIB_VERSION */
    dw (0x0004); /* 0.4 */
    dw (0xD000);
    dw (0x0016); /* LIB_REVISION */
    dw (0x0000);
    dw (0xC000);
    dw (0x0018); /* LIB_IDSTRING */
    dl (ROM_hardfile_resid);
    dw (0x0000); /* end of table */

    ROM_hardfile_init = here ();
    dl (0x00000100); /* ??? */
    dl (functable);
    dl (datatable);
    dl (initcode);
}
