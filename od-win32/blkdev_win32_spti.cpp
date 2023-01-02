/*
* UAE - The Un*x Amiga Emulator
*
* WIN32 CDROM/HD low level access code (SPTI)
*
* Copyright 2002-2010 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"

#ifdef WINDDK

#include "traps.h"
#include "memory.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#include <stddef.h>

#include <devioctl.h>
#include <ntddstor.h>
#include <winioctl.h>
#include <initguid.h>   // Guid definition
#include <devguid.h>    // Device guids
#include <setupapi.h>   // for SetupDiXxx functions.
#include <ntddscsi.h>
#include <mmsystem.h>

#include "cda_play.h"

#define INQUIRY_SIZE 36

typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
	SCSI_PASS_THROUGH_DIRECT spt;
	ULONG Filler;
	UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

struct dev_info_spti {
	TCHAR *drvpath;
	TCHAR *name;
	uae_u8 *inquirydata;
	TCHAR *ident;
	TCHAR drvletter;
	TCHAR drvlettername[10];
	int mediainserted;
	HANDLE handle;
	int isatapi;
	int removable;
	int type;
	int bus, path, target, lun;
	int scanmode;
	uae_u8 *scsibuf;
	bool open;
	bool enabled;
	struct device_info di;
	struct cda_play cda;
	uae_u8 sense[32];
	int senselen;
};

static uae_sem_t scgp_sem;
static struct dev_info_spti dev_info[MAX_TOTAL_SCSI_DEVICES];
static int unittable[MAX_TOTAL_SCSI_DEVICES];
static int total_devices;
static int bus_open;

static int getunitnum (struct dev_info_spti *di)
{
	if (!di)
		return -1;
	int idx = addrdiff(di, &dev_info[0]);
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (unittable[i] - 1 == idx)
			return i;
	}
	return -1;
}

static struct dev_info_spti *unitcheck (int unitnum)
{
	if (unitnum < 0 || unitnum >= MAX_TOTAL_SCSI_DEVICES)
		return NULL;
	if (unittable[unitnum] <= 0)
		return NULL;
	unitnum = unittable[unitnum] - 1;
//	if (dev_info[unitnum].drvletter == 0)
//		return NULL;
	return &dev_info[unitnum];
}

static struct dev_info_spti *unitisopen (int unitnum)
{
	struct dev_info_spti *di = unitcheck (unitnum);
	if (!di)
		return NULL;
	if (di->open == false)
		return NULL;
	return di;
}

static int doscsi (struct dev_info_spti *di, int unitnum, SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER *swb, int *err)
{
	DWORD status, returned;

	*err = 0;
	if (log_scsi) {
		write_log (_T("SCSI, H=%X:%d:%d:%d:%d:\n"), di->handle, di->bus, di->path, di->target, di->lun);
		scsi_log_before (swb->spt.Cdb, swb->spt.CdbLength,
			swb->spt.DataIn == SCSI_IOCTL_DATA_OUT ? (uae_u8*)swb->spt.DataBuffer : NULL, swb->spt.DataTransferLength);
		for (int i = 0; i < swb->spt.CdbLength; i++) {
			if (i > 0)
				write_log(_T("."));
			write_log(_T("%02x"), swb->spt.Cdb[i]);
		}
		write_log(_T("\n"));
	}
	gui_flicker_led (LED_CD, unitnum, 1);
	swb->spt.ScsiStatus = 0;
	if (di->bus >= 0) {
		swb->spt.PathId = di->path;
		swb->spt.TargetId = di->target;
		swb->spt.Lun = di->lun;
	}
	status = DeviceIoControl (di->handle, IOCTL_SCSI_PASS_THROUGH_DIRECT,
		swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&returned, NULL);
	if (!status) {
		int lasterror = GetLastError();
		*err = lasterror;
		write_log (_T("SCSI ERROR, H=%X:%d:%d:%d:%d: "), di->handle, di->bus, di->path, di->target, di->lun);
		write_log (_T("Status = %d, Error code = %d, LastError=%d\n"), status, swb->spt.ScsiStatus, lasterror);
		scsi_log_before (swb->spt.Cdb, swb->spt.CdbLength,
			swb->spt.DataIn == SCSI_IOCTL_DATA_OUT ? (uae_u8*)swb->spt.DataBuffer : 0,swb->spt.DataTransferLength);
	}
	if (log_scsi)
		scsi_log_after (swb->spt.DataIn == SCSI_IOCTL_DATA_IN ? (uae_u8*)swb->spt.DataBuffer : NULL, swb->spt.DataTransferLength,
		swb->SenseBuf, swb->spt.SenseInfoLength);
	if (swb->spt.SenseInfoLength > 0 && (swb->SenseBuf[2] == 0 || swb->SenseBuf[2] == 1))
		swb->spt.SenseInfoLength = 0; /* 0 and 1 = success, not error.. */
	if (swb->spt.SenseInfoLength > 0)
		return 0;
	gui_flicker_led (LED_CD, unitnum, 1);
	return status;
}


#define MODE_SELECT_6  0x15
#define MODE_SENSE_6   0x1A
#define MODE_SELECT_10 0x55
#define MODE_SENSE_10  0x5A

static int execscsicmd (struct dev_info_spti *di, int unitnum, uae_u8 *data, int len, uae_u8 *inbuf, int inlen, int timeout, int *errp, int bypass)
{
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	DWORD status;
	int err = 0;
	int dolen;

	if (!bypass) {
		if (data[0] == 0x03 && di->senselen > 0) {
			int l = di->senselen > inlen ? inlen : di->senselen;
			memcpy(inbuf, di->sense, l);
			di->senselen = 0;
			return l;
		}
		int r = blkdev_is_audio_command(data[0]);
		if (r > 0) {
			di->senselen = sizeof(di->sense);
			return blkdev_execute_audio_command(unitnum, data, len, inbuf, inlen, di->sense, &di->senselen);
		} else if (r < 0) {
			ciw_cdda_stop(&di->cda);
		}
	}

	uae_sem_wait (&scgp_sem);
	memset (&swb, 0, sizeof (swb));
	swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
	swb.spt.CdbLength = len;
	if (inbuf) {
		swb.spt.DataIn = SCSI_IOCTL_DATA_IN;
		swb.spt.DataTransferLength = inlen;
		swb.spt.DataBuffer = inbuf;
		memset (inbuf, 0, inlen);
	} else {
		swb.spt.DataIn = SCSI_IOCTL_DATA_OUT;
	}
	swb.spt.TimeOutValue = timeout < 0 ? 80 * 60 : (timeout == 0 ? 5 : timeout);
	swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
	swb.spt.SenseInfoLength = 32;
	memcpy (swb.spt.Cdb, data, len);
	status = doscsi (di, unitnum, &swb, &err);
	uae_sem_post (&scgp_sem);
	dolen = swb.spt.DataTransferLength;
	if (errp)
		*errp = err;
	if (!status)
		return -1;
	return dolen;
}

static int read_block_cda(struct cda_play *cda, int unitnum, uae_u8 *data, int sector, int size, int sectorsize)
{
	struct dev_info_spti *di = unitisopen(unitnum);
	if (!di)
		return 0;
	if (sectorsize != 2352 + 96)
		return 0;
	uae_u8 cmd[12] = { 0xbe, 0, (uae_u8)(sector >> 24), (uae_u8)(sector >> 16), (uae_u8)(sector >> 8), (uae_u8)(sector >> 0), (uae_u8)(size >> 16), (uae_u8)(size >> 8), (uae_u8)(size >> 0), 0x10, 1, 0 };
	int err = 0;
	int len = execscsicmd(di, unitnum, cmd, sizeof(cmd), data, size * sectorsize, -1, &err, true);
	if (len >= 0)
		return len;
	return -1;
}

static int execscsicmd_direct (int unitnum, struct amigascsi *as)
{
	struct dev_info_spti *di = unitisopen (unitnum);
	if (!di)
		return -1;

	if (as->cmd[0] == 0x03 && di->senselen > 0) {
		memcpy(as->data, di->sense, di->senselen);
		as->actual = di->senselen;
		as->status = 0;
		as->sactual = 0;
		as->cmdactual = as->cmd_len;
		di->senselen = 0;
		return 0;
	}
	int r = blkdev_is_audio_command(as->cmd[0]);
	if (r > 0) {
		di->senselen = sizeof(di->sense);
		int len = blkdev_execute_audio_command(unitnum, as->cmd, as->cmd_len, as->data, as->len, di->sense, &di->senselen);
		as->actual = len;
		as->cmdactual = as->cmd_len;
		as->sactual = 0;
		as->status = 0;
		if (len < 0) {
			/* copy sense? */
			if (as->sense_len > 32)
				as->sense_len = 32;
			int senselen = (as->flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
				(as->flags & 2) ? as->sense_len : /* SCSIF_AUTOSENSE */ 32;
			if (senselen > 0) {
				for (as->sactual = 0; as->sactual < di->senselen && as->sactual < senselen; as->sactual++) {
					as->sensedata[as->sactual] = di->sense[as->sactual];
				}
			}
			as->actual = 0;
			as->status = 2;
			return 45;
		}
		return 0;
	} else if (r < 0) {
		ciw_cdda_stop(&di->cda);
	}

	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	DWORD status;
	int sactual = 0, i;
	int io_error = 0, err, parm;
	uae_u8 *scsi_datap, *scsi_datap_org;
	uae_u32 scsi_cmd_len_orig = as->cmd_len;

	memset (&swb, 0, sizeof (swb));
	swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
	swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);

	if (as->len > DEVICE_SCSI_BUFSIZE)
		as->len = DEVICE_SCSI_BUFSIZE;

	uae_sem_wait (&scgp_sem);

	/* the Amiga does not tell us how long the timeout shall be, so make it _very_ long (specified in seconds) */
	swb.spt.TimeOutValue = 80 * 60;
	scsi_datap = scsi_datap_org = as->len ? as->data : 0;
	swb.spt.DataIn = (as->flags & 1) ? SCSI_IOCTL_DATA_IN : SCSI_IOCTL_DATA_OUT;
	for (i = 0; i < as->cmd_len; i++)
		swb.spt.Cdb[i] = as->cmd[i];
	if (as->sense_len > 32)
		as->sense_len = 32;
	swb.spt.SenseInfoLength  = (as->flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
		(as->flags & 2) ? as->sense_len : /* SCSIF_AUTOSENSE */ 32;
	if (dev_info[unitnum].isatapi)
		scsi_atapi_fixup_pre (swb.spt.Cdb, &as->cmd_len, &scsi_datap, &as->len, &parm);

	memcpy (di->scsibuf, scsi_datap, as->len);

	swb.spt.CdbLength = (UCHAR)as->cmd_len;
	swb.spt.DataTransferLength = as->len;
	swb.spt.DataBuffer = di->scsibuf;

	status = doscsi (di, unitnum, &swb, &err);

	memcpy (scsi_datap, di->scsibuf, as->len);

	as->cmdactual = status == 0 ? 0 : scsi_cmd_len_orig; /* fake scsi_CmdActual */
	as->status = swb.spt.ScsiStatus; /* scsi_Status */
	if (swb.spt.ScsiStatus) {
		io_error = 45; /* HFERR_BadStatus */
		/* copy sense? */
		for (sactual = 0; sactual < as->sense_len && sactual < swb.spt.SenseInfoLength; sactual++)
			as->sensedata[sactual] = swb.SenseBuf[sactual];
		as->actual = 0; /* scsi_Actual */
	} else {
		for (int i = 0; i < as->sense_len; i++)
			as->sensedata[i] = 0;
		sactual = 0;
		if (status == 0) {
			io_error = 20; /* io_Error, but not specified */
			as->actual = 0; /* scsi_Actual */
		} else {
			as->len = swb.spt.DataTransferLength;
			if (dev_info[unitnum].isatapi)
				scsi_atapi_fixup_post (swb.spt.Cdb, as->cmd_len, scsi_datap_org, scsi_datap, &as->len, parm);
			io_error = 0;
			as->actual = as->len; /* scsi_Actual */
		}
	}
	as->sactual = sactual;
	uae_sem_post (&scgp_sem);

	if (scsi_datap != scsi_datap_org)
		free (scsi_datap);

	return io_error;
}

static uae_u8 *execscsicmd_out (int unitnum, uae_u8 *data, int len)
{
	struct dev_info_spti *di = unitisopen (unitnum);
	if (!di)
		return 0;
	int v = execscsicmd (di, unitnum, data, len, 0, 0, -1, NULL, false);
	if (v < 0)
		return 0;
	return data;
}

static uae_u8 *execscsicmd_in (int unitnum, uae_u8 *data, int len, int *outlen)
{
	struct dev_info_spti *di = unitisopen (unitnum);
	if (!di)
		return 0;
	int v = execscsicmd (di, unitnum, data, len, di->scsibuf, DEVICE_SCSI_BUFSIZE, -1, NULL, false);
	if (v < 0)
		return 0;
	if (v == 0)
		return 0;
	if (outlen)
		*outlen = v < *outlen ? v : *outlen;
	return di->scsibuf;
}

static uae_u8 *execscsicmd_in_internal (struct dev_info_spti *di, int unitnum, uae_u8 *data, int len, int *outlen, int timeout)
{
	int v = execscsicmd (di, unitnum, data, len, di->scsibuf, DEVICE_SCSI_BUFSIZE, timeout, NULL, false);
	if (v < 0)
		return 0;
	if (v == 0)
		return 0;
	if (outlen)
		*outlen = v;
	return di->scsibuf;
}

static void close_scsi_device2 (struct dev_info_spti *di)
{
	di->cda.subcodevalid = false;
	if (di->open == false)
		return;
	di->open = false;
	if (di->handle != INVALID_HANDLE_VALUE) {
		CloseHandle(di->handle);
		uae_sem_destroy(&di->cda.sub_sem);
		uae_sem_destroy(&di->cda.sub_sem2);
	}
	di->handle = INVALID_HANDLE_VALUE;
}

static void close_scsi_device (int unitnum)
{
	struct dev_info_spti *di = unitisopen (unitnum);
	if (!di)
		return;
	close_scsi_device2 (di);
	blkdev_cd_change (unitnum, di->drvletter ? di->drvlettername : di->name);
	unittable[unitnum] = 0;
}

static void free_scsi_device (struct dev_info_spti *di)
{
	close_scsi_device2 (di);
	xfree (di->name);
	xfree (di->drvpath);
	xfree (di->inquirydata);
	VirtualFree (di->scsibuf, 0, MEM_RELEASE);
	di->name = NULL;
	di->drvpath = NULL;
	di->inquirydata = NULL;
	di->scsibuf = NULL;
	memset (di, 0, sizeof (struct dev_info_spti));
}

static int rescan (void);

static void close_scsi_bus (void)
{
	if (!bus_open) {
		write_log (_T("SPTI close_bus() when already closed!\n"));
		return;
	}
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		free_scsi_device (&dev_info[i]);
		unittable[i] = 0;
	}
	total_devices = 0;
	bus_open = 0;
	write_log (_T("SPTI driver closed.\n"));
}

static int open_scsi_bus (int flags)
{
	if (bus_open) {
		write_log (_T("SPTI open_bus() more than once!\n"));
		return 1;
	}
	total_devices = 0;
	uae_sem_init (&scgp_sem, 0, 1);
	rescan ();
	bus_open = 1;
	write_log (_T("SPTI driver open, %d devices.\n"), total_devices);
	return total_devices;
}

static int mediacheck (struct dev_info_spti *di, int unitnum)
{
	uae_u8 cmd [6] = { 0,0,0,0,0,0 }; /* TEST UNIT READY */
	if (di->open == false)
		return -1;
	int v = execscsicmd (di, unitnum, cmd, sizeof cmd, 0, 0, 0, NULL, false);
	return v >= 0 ? 1 : 0;
}

static int mediacheck_full (struct dev_info_spti *di, int unitnum, struct device_info *dinfo)
{
	uae_u8 cmd1[10] = { 0x25,0,0,0,0,0,0,0,0,0 }; /* READ CAPACITY */
	uae_u8 *p;
	int outlen;

	dinfo->sectorspertrack = 0;
	dinfo->trackspercylinder = 0;
	dinfo->bytespersector = 0;
	dinfo->cylinders = 0;
	dinfo->write_protected = 1;
	if (di->open == false)
		return 0;
	outlen = 32;
	p = execscsicmd_in_internal (di, unitnum, cmd1, sizeof cmd1, &outlen, 0);
	if (p && outlen >= 8) {
		dinfo->bytespersector = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
		dinfo->sectorspertrack = ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]) + 1;
		dinfo->trackspercylinder = 1;
		dinfo->cylinders = 1;
	}
	if (di->type == INQ_DASD) {
		uae_u8 cmd2[10] = { 0x5a,0x08,0,0,0,0,0,0,0xf0,0 }; /* MODE SENSE */
		outlen = 32;
		p = execscsicmd_in_internal (di, unitnum, cmd2, sizeof cmd2, &outlen, 0);
		if (p && outlen >= 4) {
			dinfo->write_protected = (p[3] & 0x80) ? 1 : 0;
		}
	}
	sys_command_cd_toc(unitnum, &di->di.toc);
	//	write_log (_T("mediacheck_full(%d,%d,%d,%d,%d)\n"),
	//	di->bytespersector, di->sectorspertrack, di->trackspercylinder, di->cylinders, di->write_protected);
	return 1;
}

static void update_device_info (int unitnum)
{
	struct dev_info_spti *dispti = unitisopen (unitnum);
	if (!dispti)
		return;
	struct device_info *di = &dispti->di;
	_tcscpy (di->label, dispti->drvletter ? dispti->drvlettername : dispti->name);
	_tcscpy (di->mediapath, dispti->drvpath);
	di->bus = 0;
	di->target = unitnum;
	di->lun = 0;
	di->media_inserted = mediacheck (dispti, unitnum);
	di->removable = dispti->removable;
	mediacheck_full (dispti, unitnum, di);
	di->type = dispti->type;
	di->unitnum = unitnum + 1;
	di->backend = _T("SPTI");
	if (log_scsi) {
		write_log (_T("MI=%d TP=%d WP=%d CY=%d BK=%d RMB=%d '%s'\n"),
			di->media_inserted, di->type, di->write_protected, di->cylinders, di->bytespersector, di->removable, di->label);
	}
}

static void checkcapabilities (struct dev_info_spti *di)
{
	STORAGE_ADAPTER_DESCRIPTOR desc;
	STORAGE_PROPERTY_QUERY query;
	DWORD ret, status;

	memset (&query, 0, sizeof STORAGE_PROPERTY_QUERY);
	query.PropertyId = StorageAdapterProperty;
	query.QueryType = PropertyStandardQuery;
	status = DeviceIoControl (di->handle, IOCTL_STORAGE_QUERY_PROPERTY,
		&query, sizeof query, &desc, sizeof desc, &ret, NULL);
	if (status) {
		if (desc.Size > offsetof (STORAGE_ADAPTER_DESCRIPTOR, BusType))
			write_log (_T("SCSI CAPS: BusType=%d, MaxTransfer=0x%08X, Mask=0x%08X\n"),
			desc.BusType, desc.MaximumTransferLength, desc.AlignmentMask);
	}
}

static int inquiry (struct dev_info_spti *di, int unitnum, uae_u8 *inquirydata)
{
	uae_u8 cmd[6] = { 0x12,0,0,0,36,0 }; /* INQUIRY */
	int outlen = INQUIRY_SIZE;
	uae_u8 *p = execscsicmd_in_internal (di, unitnum, cmd, sizeof (cmd), &outlen, 0);
	int inqlen = 0;

	di->isatapi = 0;
	di->removable = 0;
	di->type = 0x1f;
	if (!p) {
		if (log_scsi)
			write_log (_T("SPTI: INQUIRY failed\n"));
		return 0;
	}
	inqlen = outlen > INQUIRY_SIZE ? INQUIRY_SIZE : outlen;
	if (outlen >= 1) {
		di->type = p[0] & 31;
		di->removable = (p[1] & 0x80) ? 1 : 0;
	}
	if (outlen >= 2 && (p[0] & 31) == 5 && (p[2] & 7) == 0)
		di->isatapi = 1;
	memcpy (inquirydata, p, inqlen);
	if (log_scsi) {
		if (outlen >= INQUIRY_SIZE) {
			char tmp[20];
			TCHAR *s1, *s2;

			memcpy (tmp, p + 8, 8);
			tmp[8] = 0;
			s1 = au (tmp);
			memcpy (tmp, p + 16, 16);
			tmp[16] = 0;
			s2 = au (tmp);
			write_log (_T("SPTI: INQUIRY: %02X%02X%02X %d '%s' '%s'\n"),
				p[0], p[1], p[2], di->isatapi, s1, s2);
			xfree (s2);
			xfree (s1);
		}
	}
	return inqlen;
}

static int open_scsi_device2 (struct dev_info_spti *di, int unitnum)
{
	HANDLE h;
	TCHAR *dev;

	if (di->bus >= 0) {
		dev = xmalloc (TCHAR, 100);
		_stprintf (dev, _T("\\\\.\\Scsi%d:"), di->bus);
	} else {
		dev = my_strdup (di->drvpath);
	}
	if (!di->scsibuf)
		di->scsibuf = (uae_u8*)VirtualAlloc (NULL, DEVICE_SCSI_BUFSIZE, MEM_COMMIT, PAGE_READWRITE);
	h = CreateFile(dev,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
	di->handle = h;
	if (h == INVALID_HANDLE_VALUE) {
		write_log (_T("SPTI: failed to open unit %d err=%d ('%s')\n"), unitnum, GetLastError (), dev);
	} else {
		int err = 0;
		uae_u8 inqdata[INQUIRY_SIZE + 1] = { 0 };
		checkcapabilities (di);
		execscsicmd(di, unitnum, inqdata, 6, NULL, 0, 0, &err, false);
		if (err) {
			write_log(_T("SPTI: TUR failed unit %d, err=%d ('%s':%d:%d:%d:%d)\n"), unitnum, err, dev,
				di->bus, di->path, di->target, di->lun);
			close_scsi_device2(di);
			xfree(dev);
			return 0;
		}
		if (!inquiry (di, unitnum, inqdata)) {
			write_log (_T("SPTI: inquiry failed unit %d ('%s':%d:%d:%d:%d)\n"), unitnum, dev,
				di->bus, di->path, di->target, di->lun);
			close_scsi_device2 (di);
			xfree (dev);
			return 0;
		}
		inqdata[INQUIRY_SIZE] = 0;
		di->name = my_strdup_ansi ((char*)inqdata + 8);
		if (di->type == INQ_ROMD) {
			// This fake CD device hangs if it sees SCSI read command.
			if (!memcmp(inqdata + 8, "HUAWEI  Mass Storage    ", 8 + 16)) {
				write_log(_T("SPTI: '%s' ignored.\n"), di->name);
				close_scsi_device2(di);
				xfree(dev);
				return 0;
			}
			di->mediainserted = mediacheck (di, unitnum);
			write_log (_T("SPTI: unit %d (%c:\\) opened [%s], %s, '%s'\n"),
				unitnum, di->drvletter ? di->drvletter : '*',
				di->isatapi ? _T("ATAPI") : _T("SCSI"),
				di->mediainserted ? _T("media inserted") : _T("drive empty"),
				di->name);
		} else {
			write_log (_T("SPTI: unit %d, type %d, '%s'\n"),
				unitnum, di->type, di->name);
		}
		di->inquirydata = xmalloc (uae_u8, INQUIRY_SIZE);
		memcpy (di->inquirydata, inqdata, INQUIRY_SIZE);
		xfree (dev);
		di->open = true;
		update_device_info (unitnum);
		if (di->type == INQ_ROMD)
			blkdev_cd_change (unitnum, di->drvletter ? di->drvlettername : di->name);
		di->cda.cdda_volume[0] = 0x7fff;
		di->cda.cdda_volume[1] = 0x7fff;
		uae_sem_init(&di->cda.sub_sem, 0, 1);
		uae_sem_init(&di->cda.sub_sem2, 0, 1);
		di->cda.cd_last_pos = 150;
		return 1;
	}
	xfree (dev);
	return 0;
}

int open_scsi_device (int unitnum, const TCHAR *ident, int flags)
{
	struct dev_info_spti *di = NULL;
	if (ident && ident[0]) {
		for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
			di = &dev_info[i];
			if (unittable[i] == 0 && di->drvletter != 0) {
				if (!_tcsicmp (di->drvlettername, ident)) {
					unittable[unitnum] = i + 1;
					if (open_scsi_device2 (di, unitnum))
						return 1;
					unittable[unitnum] = 0;
					return 0;
				}
			}
		}
		return 0;
	}
	di = &dev_info[unitnum];
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (unittable[i] == unitnum + 1)
			return 0;
	}
	if (di->enabled == 0)
		return 0;
	unittable[unitnum] = unitnum + 1;
	if (open_scsi_device2 (di, unitnum))
		return 1;
	unittable[unitnum] = 0;
	return 0;
}

static int adddrive (const TCHAR *drvpath, int bus, int pathid, int targetid, int lunid, int scanmode)
{
	struct dev_info_spti *di;
	int cnt = total_devices, i;
	int freeit = 1;

	if (cnt >= MAX_TOTAL_SCSI_DEVICES)
		return 0;
	for (i = 0; i < total_devices; i++) {
		di = &dev_info[i];
		if (!_tcscmp (drvpath, di->drvpath))
			return 0;
	}
	write_log (_T("SPTI: unit %d '%s' added\n"), total_devices, drvpath);
	di = &dev_info[total_devices];
	di->drvpath = my_strdup (drvpath);
	di->type = 0;
	di->bus = bus;
	di->path = pathid;
	di->target = targetid;
	di->lun = lunid;
	di->scanmode = scanmode;
	di->drvletter = 0;
	di->enabled = true;

	for (TCHAR drvletter = 'C'; drvletter <= 'Z'; drvletter++) {
		TCHAR drvname[10];
		TCHAR volname[MAX_DPATH], volname2[MAX_DPATH];
		_stprintf (drvname, _T("%c:\\"), drvletter);
		if (GetVolumeNameForVolumeMountPoint (drvname, volname, sizeof volname / sizeof (TCHAR))) {
			TCHAR drvpath2[MAX_DPATH];
			_stprintf (drvpath2, _T("%s\\"), di->drvpath);
			if (GetVolumeNameForVolumeMountPoint (drvpath2, volname2, sizeof volname2 / sizeof (TCHAR))) {
				if (!_tcscmp (volname, volname2)) {
					di->drvletter = drvletter;
					_tcscpy (di->drvlettername, drvname);
					break;
				}
			}
		}
	}


	total_devices++;
	unittable[cnt] = cnt + 1;
	if (open_scsi_device2 (&dev_info[cnt], cnt)) {
		for (i = 0; i < cnt; i++) {
			if (!memcmp (di->inquirydata, dev_info[i].inquirydata, INQUIRY_SIZE) && di->scanmode != dev_info[i].scanmode) {
				write_log (_T("duplicate device, skipped..\n"));
				break;
			}
		}
		if (i == cnt) {
			freeit = 0;
			close_scsi_device2 (&dev_info[cnt]);
		}
	}
	if (freeit) {
		free_scsi_device (&dev_info[cnt]);
		total_devices--;
	}
	unittable[cnt] = 0;
	return 1;
}

static struct device_info *info_device (int unitnum, struct device_info *di, int quick, int session)
{
	struct dev_info_spti *dispti = unitcheck (unitnum);
	if (!dispti)
		return NULL;
	if (!quick)
		update_device_info (unitnum);
	dispti->di.open = dispti->open;
	memcpy (di, &dispti->di, sizeof (struct device_info));
	return di;
}

bool win32_spti_media_change (TCHAR driveletter, int insert)
{
	for (int i = 0; i < total_devices; i++) {
		struct dev_info_spti *di = &dev_info[i];
		if (di->drvletter == driveletter && di->mediainserted != insert) {
			write_log (_T("SPTI: media change %c %d\n"), dev_info[i].drvletter, insert);
			di->mediainserted = insert;
			int unitnum = getunitnum (di);
			if (unitnum >= 0) {
				update_device_info (unitnum);
				scsi_do_disk_change (unitnum, insert, NULL);
				filesys_do_disk_change (unitnum, insert != 0);
				blkdev_cd_change (unitnum, di->drvletter ? di->drvlettername : di->name);
				return true;
			}
		}
	}
	return false;
}

static int check_isatapi (int unitnum)
{
	struct dev_info_spti *di = unitcheck (unitnum);
	if (!di)
		return 0;
	return di->isatapi;
}

static int getCDROMProperty (int idx, HDEVINFO DevInfo, const GUID *guid)
{
	SP_DEVICE_INTERFACE_DATA interfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA interfaceDetailData = NULL;
	DWORD interfaceDetailDataSize, reqSize;
	DWORD status, errorCode;

	interfaceData.cbSize = sizeof (SP_INTERFACE_DEVICE_DATA);
	status = SetupDiEnumDeviceInterfaces (
		DevInfo,	// Interface Device Info handle
		0,		// Device Info data
		guid,		// Interface registered by driver
		idx,		// Member
		&interfaceData	// Device Interface Data
		);
	if (status == FALSE)
		return FALSE;

	status = SetupDiGetDeviceInterfaceDetail (
		DevInfo,	// Interface Device info handle
		&interfaceData,	// Interface data for the event class
		NULL,		// Checking for buffer size
		0,		// Checking for buffer size
		&reqSize,	// Buffer size required to get the detail data
		NULL		// Checking for buffer size
		);

	if (status == FALSE) {
		errorCode = GetLastError();
		if (errorCode != ERROR_INSUFFICIENT_BUFFER)
			return FALSE;
	}

	interfaceDetailDataSize = reqSize;
	interfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)xmalloc (uae_u8, interfaceDetailDataSize);
	if (interfaceDetailData == NULL)
		return FALSE;
	interfaceDetailData->cbSize = sizeof (SP_INTERFACE_DEVICE_DETAIL_DATA);

	status = SetupDiGetDeviceInterfaceDetail (
		DevInfo,		// Interface Device info handle
		&interfaceData,		// Interface data for the event class
		interfaceDetailData,	// Interface detail data
		interfaceDetailDataSize,// Interface detail data size
		&reqSize,		// Buffer size required to get the detail data
		NULL);			// Interface device info

	if (status == FALSE)
		return FALSE;

	adddrive (interfaceDetailData->DevicePath, -1, -1, -1, -1, 1);

	free (interfaceDetailData);

	return TRUE;
}

#define	SCSI_INFO_BUFFER_SIZE 0x5000
static void scanscsi (void)
{
	PSCSI_BUS_DATA BusData;
	PSCSI_INQUIRY_DATA InquiryData;
	PSCSI_ADAPTER_BUS_INFO AdapterInfo;
	HANDLE h;
	BOOL status;
	BOOL Claimed;
	ULONG returnedLength;
	SHORT Bus, Luns;
	DWORD bytesTransferred;
	int idx;
	TCHAR DeviceName[256];

	AdapterInfo = (PSCSI_ADAPTER_BUS_INFO)xmalloc (uae_u8, SCSI_INFO_BUFFER_SIZE);
	if (AdapterInfo == NULL)
		return;

	idx = 0;
	for (;;) {
		_stprintf (DeviceName, _T("\\\\.\\Scsi%d:"), idx++);
		h = CreateFile (DeviceName,
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL, // no SECURITY_ATTRIBUTES structure
			OPEN_EXISTING, // No special create flags
			0, // No special attributes
			NULL);
		if (h == INVALID_HANDLE_VALUE) {
			xfree(AdapterInfo);
			return;
		}

		if(!DeviceIoControl (h,
			IOCTL_SCSI_RESCAN_BUS,
			NULL,
			0,
			NULL,
			0,
			&bytesTransferred,
			NULL)) {
				write_log (_T("Rescan SCSI port %d failed [Error %d]\n"), idx - 1, GetLastError());
				CloseHandle (h);
				continue;
		}

		// Get the SCSI inquiry data for all devices for the given SCSI bus
		status = DeviceIoControl (
			h,
			IOCTL_SCSI_GET_INQUIRY_DATA,
			NULL,
			0,
			AdapterInfo,
			SCSI_INFO_BUFFER_SIZE,
			&returnedLength,
			NULL);

		if (!status) {
			write_log (_T("Error in IOCTL_SCSI_GET_INQUIRY_DATA\n") );
			CloseHandle (h);
			continue;
		}

		for (Bus = 0; Bus < AdapterInfo->NumberOfBuses; Bus++) {
			int luncheck = 0;
			BusData = &AdapterInfo->BusData[Bus];
			InquiryData = (PSCSI_INQUIRY_DATA) ((PUCHAR)AdapterInfo + BusData->InquiryDataOffset);
			for (Luns = 0; Luns < BusData->NumberOfLogicalUnits; Luns++) {
				TCHAR label[100];
				int type = InquiryData->InquiryData[0] & 0x1f;
				Claimed = InquiryData->DeviceClaimed;
				write_log (_T("SCSI=%d Initiator=%d Path=%d Target=%d LUN=%d Claimed=%s Type=%d\n"),
					idx - 1,
					BusData->InitiatorBusId, InquiryData->PathId, InquiryData->TargetId,
					InquiryData->Lun, Claimed ? _T("Yes") : _T("No "), type);
				if (Claimed == 0 && !luncheck) {
					luncheck = 1;
					_stprintf (label, _T("SCSI(%d):%d:%d:%d:%d"), idx - 1, BusData->InitiatorBusId,
						InquiryData->PathId, InquiryData->TargetId, InquiryData->Lun);
					adddrive (label, idx - 1, InquiryData->PathId, InquiryData->TargetId, InquiryData->Lun, 3);
				}
				InquiryData = (PSCSI_INQUIRY_DATA) ((PUCHAR)AdapterInfo + InquiryData->NextInquiryDataOffset);
			}   // for Luns
		}	// for Bus
		CloseHandle (h);
	}
}

static const GUID *guids[] = {
	&GUID_DEVINTERFACE_CDROM,
	&GUID_DEVCLASS_IMAGE,
	&GUID_DEVCLASS_TAPEDRIVE,
	NULL };
static const TCHAR *scsinames[] = { _T("Tape"), _T("Scanner"), _T("Changer"), NULL };

static int rescan (void)
{
	int idx, idx2;

	for (idx2 = 0; guids[idx2]; idx2++) {
		HDEVINFO hDevInfo = SetupDiGetClassDevs(
			guids[idx2],
			NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
		if (hDevInfo != INVALID_HANDLE_VALUE) {
			for (idx = 0; ; idx++) {
				if (!getCDROMProperty (idx, hDevInfo, guids[idx2]))
					break;
			}
			SetupDiDestroyDeviceInfoList (hDevInfo);
		}
	}

	for (idx2 = 0; scsinames[idx2]; idx2++) {
		int max = 10;
		for (idx = 0; idx < max; idx++) {
			TCHAR tmp[100];
			HANDLE h;
			_stprintf (tmp, _T("\\\\.\\%s%d"), scsinames[idx2], idx);
			h = CreateFile (tmp, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, 0, NULL);
			if (h != INVALID_HANDLE_VALUE) {
				adddrive (tmp, -1, -1, -1, -1, 2);
				CloseHandle (h);
				if (idx == max - 1)
					max++;
			}
		}
	}
	if (currprefs.win32_uaescsimode == UAESCSI_SPTISCAN) {
		write_log (_T("SCSI adapter enumeration..\n"));
		scanscsi ();
		write_log (_T("SCSI adapter enumeration ends\n"));
	}
	return 1;
}


/* pause/unpause CD audio */
static int ioctl_command_pause(int unitnum, int paused)
{
	struct dev_info_spti *di = unitisopen(unitnum);
	if (!di)
		return -1;
	int old = di->cda.cdda_paused;
	if ((paused && di->cda.cdda_play) || !paused)
		di->cda.cdda_paused = paused;
	return old;
}


/* stop CD audio */
static int ioctl_command_stop(int unitnum)
{
	struct dev_info_spti *di = unitisopen(unitnum);
	if (!di)
		return 0;

	ciw_cdda_stop(&di->cda);

	return 1;
}

static uae_u32 ioctl_command_volume(int unitnum, uae_u16 volume_left, uae_u16 volume_right)
{
	struct dev_info_spti *di = unitisopen(unitnum);
	if (!di)
		return -1;
	uae_u32 old = (di->cda.cdda_volume[1] << 16) | (di->cda.cdda_volume[0] << 0);
	di->cda.cdda_volume[0] = volume_left;
	di->cda.cdda_volume[1] = volume_right;
	return old;
}

/* play CD audio */
static int ioctl_command_play(int unitnum, int startlsn, int endlsn, int scan, play_status_callback statusfunc, play_subchannel_callback subfunc)
{
	struct dev_info_spti *di = unitisopen(unitnum);
	if (!di)
		return 0;

	di->cda.di = &di->di;
	di->cda.subcodevalid = false;
	di->cda.cdda_play_finished = 0;
	di->cda.cdda_subfunc = subfunc;
	di->cda.cdda_statusfunc = statusfunc;
	di->cda.cdda_scan = scan > 0 ? 10 : (scan < 0 ? 10 : 0);
	di->cda.cdda_delay = ciw_cdda_setstate(&di->cda, -1, -1);
	di->cda.cdda_delay_frames = ciw_cdda_setstate(&di->cda, -2, -1);
	ciw_cdda_setstate(&di->cda, AUDIO_STATUS_NOT_SUPPORTED, -1);
	di->cda.read_block = read_block_cda;

	if (!isaudiotrack(&di->di.toc, startlsn)) {
		ciw_cdda_setstate(&di->cda, AUDIO_STATUS_PLAY_ERROR, -1);
		return 0;
	}
	if (!di->cda.cdda_play) {
		uae_start_thread(_T("ioctl_cdda_play"), ciw_cdda_play, &di->cda, NULL);
	}
	di->cda.cdda_start = startlsn;
	di->cda.cdda_end = endlsn;
	di->cda.cd_last_pos = di->cda.cdda_start;
	di->cda.cdda_play++;

	return 1;
}

static void sub_deinterleave(const uae_u8 *s, uae_u8 *d)
{
	for (int i = 0; i < 8 * 12; i++) {
		int dmask = 0x80;
		int smask = 1 << (7 - (i / 12));
		(*d) = 0;
		for (int j = 0; j < 8; j++) {
			(*d) |= (s[(i % 12) * 8 + j] & smask) ? dmask : 0;
			dmask >>= 1;
		}
		d++;
	}
}

/* read qcode */
static int ioctl_command_qcode(int unitnum, uae_u8 *buf, int sector, bool all)
{
	struct dev_info_spti *di = unitisopen(unitnum);
	if (!di)
		return 0;
	struct cda_play *cd = &di->cda;

	if (all)
		return 0;

	memset(buf, 0, SUBQ_SIZE);
	uae_u8 *p = buf;

	int status = AUDIO_STATUS_NO_STATUS;
	if (di->cda.cdda_play) {
		status = AUDIO_STATUS_IN_PROGRESS;
		if (di->cda.cdda_paused)
			status = AUDIO_STATUS_PAUSED;
	} else if (di->cda.cdda_play_finished) {
		status = AUDIO_STATUS_PLAY_COMPLETE;
	}

	p[1] = status;
	p[3] = 12;

	p = buf + 4;

	if (cd->subcodevalid) {
		uae_sem_wait(&di->cda.sub_sem2);
		uae_u8 subbuf[SUB_CHANNEL_SIZE];
		sub_deinterleave(di->cda.subcodebuf, subbuf);
		memcpy(p, subbuf + 12, 12);
		uae_sem_post(&di->cda.sub_sem2);
	} else {
		int pos = di->cda.cd_last_pos;
		int trk = cdtracknumber(&di->di.toc, pos);
		if (trk >= 0) {
			struct cd_toc *t = &di->di.toc.toc[trk];
			p[0] = (t->control << 4) | (t->adr << 0);
			p[1] = tobcd(trk);
			p[2] = tobcd(1);
			uae_u32 msf = lsn2msf(pos);
			tolongbcd(p + 7, msf);
			msf = lsn2msf(pos - t->paddress - 150);
			tolongbcd(p + 3, msf);
		} else {
			p[1] = AUDIO_STATUS_NO_STATUS;
		}
	}

	return 1;
}
#endif


struct device_functions devicefunc_scsi_spti = {
	_T("SPTI"),
	open_scsi_bus, close_scsi_bus, open_scsi_device, close_scsi_device, info_device,
	execscsicmd_out, execscsicmd_in, execscsicmd_direct,
	ioctl_command_pause, ioctl_command_stop, ioctl_command_play, ioctl_command_volume, ioctl_command_qcode,
	0, 0, 0, 0, check_isatapi, 0, 0
};
