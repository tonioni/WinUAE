 /*
  * UAE - The Un*x Amiga Emulator
  *
  * WIN32 CDROM/HD low level access code (SPTI)
  *
  * Copyright 2002 Toni Wilen
  *
  */


#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WINDDK

#include "memory.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"

#include <stddef.h>

#include <windows.h>
#include <devioctl.h>
#include <ntddscsi.h>

typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
  SCSI_PASS_THROUGH_DIRECT spt;
  ULONG Filler;
  UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

static int unitcnt = 0;

struct dev_info_spti {
    char drvletter;
    char drvpath[10];
    int mediainserted;
    HANDLE handle;
    int isatapi;
};

static uae_sem_t scgp_sem;
static struct dev_info_spti dev_info[MAX_TOTAL_DEVICES];
static uae_u8 *scsibuf;

static int doscsi (HANDLE *h, SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER *swb, int *err)
{
    DWORD status, returned;

    *err = 0;
    if (log_scsi) {
	write_log ("SCSI, H=%X: ", h);
	scsi_log_before (swb->spt.Cdb, swb->spt.CdbLength,
	    swb->spt.DataIn == SCSI_IOCTL_DATA_OUT ? swb->spt.DataBuffer : 0,swb->spt.DataTransferLength);
    }
    gui_cd_led (1);
    status = DeviceIoControl (h, IOCTL_SCSI_PASS_THROUGH_DIRECT,
	swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
	swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
	&returned, NULL);
    if (!status) {
	int lasterror = GetLastError();
	*err = lasterror;
        write_log("SCSI ERROR, H=%X: ", h);
	write_log("Error code = %d, LastError=%d\n", swb->spt.ScsiStatus, lasterror);
	scsi_log_before (swb->spt.Cdb, swb->spt.CdbLength,
	    swb->spt.DataIn == SCSI_IOCTL_DATA_OUT ? swb->spt.DataBuffer : 0,swb->spt.DataTransferLength);
    }
    if (log_scsi)
	scsi_log_after (swb->spt.DataIn == SCSI_IOCTL_DATA_IN ? swb->spt.DataBuffer : 0, swb->spt.DataTransferLength,
	    swb->SenseBuf, swb->spt.SenseInfoLength);
    if (swb->spt.SenseInfoLength > 0 && (swb->SenseBuf[0] == 0 || swb->SenseBuf[0] == 1))
	swb->spt.SenseInfoLength = 0; /* 0 and 1 = success, not error.. */
    if (swb->spt.SenseInfoLength > 0)
	return 0;
    gui_cd_led (1);
    return status;
}


#define MODE_SELECT_6  0x15
#define MODE_SENSE_6   0x1A
#define MODE_SELECT_10 0x55
#define MODE_SENSE_10  0x5A

static int execscsicmd (int unitnum, uae_u8 *data, int len, uae_u8 *inbuf, int inlen)
{
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
    DWORD status;
    int err, dolen;

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
    swb.spt.TimeOutValue = 80 * 60;
    swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
    swb.spt.SenseInfoLength = 32;
    memcpy (swb.spt.Cdb, data, len);
    status = doscsi (dev_info[unitnum].handle, &swb, &err);
    uae_sem_post (&scgp_sem);
    dolen = swb.spt.DataTransferLength;
    if (!status)
	return -1;
    return dolen;
}

static int execscsicmd_direct (int unitnum, uaecptr acmd)
{
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
    DWORD status;
    int sactual = 0, i;
    uaecptr scsi_data = get_long (acmd + 0);
    uae_u32 scsi_len = get_long (acmd + 4);
    uaecptr scsi_cmd = get_long (acmd + 12);
    int scsi_cmd_len = get_word (acmd + 16);
    int scsi_cmd_len_orig = scsi_cmd_len;
    uae_u8 scsi_flags = get_byte (acmd + 20);
    uaecptr scsi_sense = get_long (acmd + 22);
    uae_u16 scsi_sense_len = get_word (acmd + 26);
    int io_error = 0, err, parm;
    addrbank *bank_data = &get_mem_bank (scsi_data);
    uae_u8 *scsi_datap, *scsi_datap_org;

    memset (&swb, 0, sizeof (swb));
    swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
    swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);

    /* do transfer directly to and from Amiga memory */
    if (!bank_data || !bank_data->check (scsi_data, scsi_len))
        return -5; /* IOERR_BADADDRESS */

    uae_sem_wait (&scgp_sem);

    /* the Amiga does not tell us how long the timeout shall be, so make it _very_ long (specified in seconds) */
    swb.spt.TimeOutValue = 80 * 60;
    scsi_datap = scsi_datap_org = scsi_len ? bank_data->xlateaddr (scsi_data) : 0;
    swb.spt.DataIn = (scsi_flags & 1) ? SCSI_IOCTL_DATA_IN : SCSI_IOCTL_DATA_OUT;
    for (i = 0; i < scsi_cmd_len; i++)
	swb.spt.Cdb[i] = get_byte (scsi_cmd + i);
    if (scsi_sense_len > 32)
	scsi_sense_len = 32;
    swb.spt.SenseInfoLength  = (scsi_flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
        (scsi_flags & 2) ? scsi_sense_len : /* SCSIF_AUTOSENSE */
        32;
    if (dev_info[unitnum].isatapi)
        scsi_atapi_fixup_pre (swb.spt.Cdb, &scsi_cmd_len, &scsi_datap, &scsi_len, &parm);
    swb.spt.CdbLength = (UCHAR)scsi_cmd_len;
    swb.spt.DataTransferLength = scsi_len;
    swb.spt.DataBuffer = scsi_datap;

    status = doscsi (dev_info[unitnum].handle, &swb, &err);

    put_word (acmd + 18, status == 0 ? 0 : scsi_cmd_len_orig); /* fake scsi_CmdActual */
    put_byte (acmd + 21, swb.spt.ScsiStatus); /* scsi_Status */
    if (swb.spt.ScsiStatus) {
        io_error = 45; /* HFERR_BadStatus */
        /* copy sense? */
        for (sactual = 0; scsi_sense && sactual < scsi_sense_len && sactual < swb.spt.SenseInfoLength; sactual++)
            put_byte (scsi_sense + sactual, swb.SenseBuf[sactual]);
        put_long (acmd + 8, 0); /* scsi_Actual */
    } else {
        int i;
        for (i = 0; i < scsi_sense_len; i++)
            put_byte (scsi_sense + i, 0);
        sactual = 0;
        if (status == 0) {
	    io_error = 20; /* io_Error, but not specified */
	    put_long (acmd + 8, 0); /* scsi_Actual */
        } else {
	    scsi_len = swb.spt.DataTransferLength;
	    if (dev_info[unitnum].isatapi)
		scsi_atapi_fixup_post (swb.spt.Cdb, scsi_cmd_len, scsi_datap_org, scsi_datap, &scsi_len, parm);
	    io_error = 0;
            put_long (acmd + 8, scsi_len); /* scsi_Actual */
        }
    }
    put_word (acmd + 28, sactual);
    uae_sem_post (&scgp_sem);

    if (scsi_datap != scsi_datap_org)
	free (scsi_datap);

    return io_error;
}

static uae_u8 *execscsicmd_out (int unitnum, uae_u8 *data, int len)
{
    int v = execscsicmd (unitnum, data, len, 0, 0);
    if (v < 0)
	return 0;
    return data;
}

static uae_u8 *execscsicmd_in (int unitnum, uae_u8 *data, int len, int *outlen)
{
    int v = execscsicmd (unitnum, data, len, scsibuf, DEVICE_SCSI_BUFSIZE);
    if (v < 0)
	return 0;
    if (v == 0)
	return 0;
    if (outlen)
	*outlen = v;
    return scsibuf;
}

static int adddrive (int unitnum, char drive)
{
    if (unitcnt >= MAX_TOTAL_DEVICES)
	return 0;
    sprintf (dev_info[unitcnt].drvpath, "\\\\.\\%c:", drive);
    dev_info[unitcnt].drvletter = drive;
    write_log ("SPTI: selected drive %c: (uaescsi.device:%d)\n", drive, unitnum);
    unitcnt++;
    return 1;
}

static int total_devices;

static int open_scsi_bus (int flags)
{
    int dwDriveMask;
    int drive, firstdrive;
    char tmp[10], tmp2[10];
    int i;

    total_devices = 0;
    firstdrive = 0;
    uae_sem_init (&scgp_sem, 0, 1);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	memset (&dev_info[i], 0, sizeof (struct dev_info_spti));
	dev_info[i].handle = INVALID_HANDLE_VALUE;
    }
    if (!scsibuf)
	scsibuf = VirtualAlloc (NULL, DEVICE_SCSI_BUFSIZE, MEM_COMMIT, PAGE_READWRITE);
    dwDriveMask = GetLogicalDrives ();
    device_debug ("SPTI: drive mask = %08.8X\n", dwDriveMask);
    dwDriveMask >>= 2; // Skip A and B drives...
    for( drive = 'C'; drive <= 'Z'; drive++) {
	if (dwDriveMask & 1) {
	    int dt;
	    sprintf( tmp, "%c:\\", drive );
	    sprintf( tmp2, "%c:\\.", drive );
	    dt = GetDriveType (tmp);
	    device_debug ("SPTI: drive %c type %d\n", drive, dt);
	    if (dt == DRIVE_CDROM) {
		if (!firstdrive)
		    firstdrive = drive;
		if (adddrive (total_devices, drive))
		    total_devices++;
	    }
	}
	dwDriveMask >>= 1;
    }
    return total_devices;
}

static void close_scsi_bus (void)
{
    VirtualFree (scsibuf, 0, MEM_RELEASE);
    scsibuf = 0;
}

static int isatapi (int unitnum)
{
    uae_u8 cmd[6] = { 0x12,0,0,0,36,0 }; /* INQUIRY */
    uae_u8 out[36];
    int outlen = sizeof (out);
    uae_u8 *p = execscsicmd_in (unitnum, cmd, sizeof (cmd), &outlen);
    int v = 0;

    if (!p) {
	if (log_scsi)
	    write_log("INQUIRY failed!?\n");
	return 0;
    }
    if (outlen >= 2 && (p[0] & 31) == 5 && (p[2] & 7) == 0)
	v = 1;
    if (log_scsi) {
	if (outlen >= 36) 
	    write_log("INQUIRY: %02.2X%02.2X%02.2X %d '%-8.8s' '%-16.16s'\n",
		p[0], p[1], p[2], v, p + 8, p + 16);
    }
    return v;
}

static int mediacheck (int unitnum)
{
    uae_u8 cmd [6] = { 0,0,0,0,0,0 }; /* TEST UNIT READY */
    int v;
    if (dev_info[unitnum].handle == INVALID_HANDLE_VALUE)
	return 0;
    v = execscsicmd (unitnum, cmd, sizeof (cmd), 0, 0);
    return v >= 0 ? 1 : 0;
}

int open_scsi_device (int unitnum)
{
    HANDLE h;
    char *dev;

    dev = dev_info[unitnum].drvpath;
    if (!dev[0])
	return 0;
    h = CreateFile(dev,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    dev_info[unitnum].handle = h;
    if (h == INVALID_HANDLE_VALUE) {
        write_log ("failed to open cd unit %d (%s)\n", unitnum, dev);
    } else {
        dev_info[unitnum].mediainserted = mediacheck (unitnum);
	dev_info[unitnum].isatapi = isatapi (unitnum);
	write_log ("cd unit %d %s opened (%s), %s\n", unitnum,
	    dev_info[unitnum].isatapi ? "[ATAPI]" : "[SCSI]", dev,
	    dev_info[unitnum].mediainserted ? "CD inserted" : "Drive empty");
        return 1;
    }
    return 0;
}

static void close_scsi_device (int unitnum)
{
    write_log ("cd unit %d closed\n", unitnum);
    if (dev_info[unitnum].handle != INVALID_HANDLE_VALUE)
	CloseHandle (dev_info[unitnum].handle);
    dev_info[unitnum].handle = INVALID_HANDLE_VALUE;
}

static struct device_info *info_device (int unitnum, struct device_info *di)
{
    if (unitnum >= MAX_TOTAL_DEVICES || dev_info[unitnum].handle == INVALID_HANDLE_VALUE)
	return 0;
    sprintf(di->label,"Drive %c", dev_info[unitnum].drvletter);
    di->bus = 0;
    di->target = unitnum;
    di->lun = 0;
    di->media_inserted = mediacheck (unitnum);
    di->write_protected = 1;
    di->bytespersector = 2048;
    di->cylinders = 1;
    di->type = INQ_ROMD;
    di->id = dev_info[unitnum].drvletter;
    return di;
}

void win32_spti_media_change (char driveletter, int insert)
{
    int i;

    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (dev_info[i].drvletter == driveletter && dev_info[i].mediainserted != insert) {
	    write_log ("SPTI: media change %c %d\n", driveletter, insert);
	    dev_info[i].mediainserted = insert;
	    scsi_do_disk_change (driveletter, insert);
	}
    }
}

static int check_isatapi (int unitnum)
{
    return dev_info[unitnum].isatapi;
}

struct device_functions devicefunc_win32_spti = {
    open_scsi_bus, close_scsi_bus, open_scsi_device, close_scsi_device, info_device,
    execscsicmd_out, execscsicmd_in, execscsicmd_direct,
    0, 0, 0, 0, 0, 0, 0, check_isatapi
};

#endif