 /*
  * UAE - The Un*x Amiga Emulator
  *
  * WIN32 CDROM/HD low level access code (IOCTL)
  *
  * Copyright 2002 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WINDDK

#include "config.h"
#include "uae.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"

#include <devioctl.h>
#include <ntddcdrm.h>

struct dev_info_ioctl {
    HANDLE h;
    uae_u8 *tempbuffer;
    char drvletter;
    char devname[30];
    int mediainserted;
    int type;
};

static UINT errormode;

static void seterrormode (void)
{
    errormode = SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX );
}
static void reseterrormode (void)
{
    SetErrorMode (errormode);
}

static struct dev_info_ioctl ciw32[MAX_TOTAL_DEVICES + 64 + 32];

static void close_device (int unitnum);
static int open_device (int unitnum);

static int win32_error (int unitnum, const char *format,...)
{
    LPVOID lpMsgBuf;
    va_list arglist;
    char buf[1000];
    DWORD err = GetLastError();

    if (err == 34) {
	write_log ("IOCTL: media change, re-opening device\n");
	close_device (unitnum);
	if (!open_device (unitnum))
	    write_log ("IOCTL: re-opening failed!\n");
	return -1;
    }
    va_start (arglist, format );
    vsprintf (buf, format, arglist);
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
	NULL,err,MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	(LPTSTR) &lpMsgBuf,0,NULL);
    if (log_scsi)
	write_log ("IOCTL: unit=%d %s,%d: %s ", unitnum, buf, err, (char*)lpMsgBuf);
    va_end( arglist );
    return err;
}


/* pause/unpause CD audio */
static int ioctl_command_pause (int unitnum, int paused)
{
    DWORD len;
    int command = paused ? IOCTL_CDROM_PAUSE_AUDIO : IOCTL_CDROM_RESUME_AUDIO;
    int cnt = 3;

    while (cnt-- > 0) {
        seterrormode ();
	if (!DeviceIoControl(ciw32[unitnum].h, command, NULL, 0, NULL, 0, &len, NULL)) {
	    reseterrormode ();
	    if (win32_error (unitnum, paused ? "IOCTL_CDROM_PAUSE_AUDIO" : "IOCTL_CDROM_RESUME_AUDIO") < 0)
		continue;
	    return 0;
	}
        reseterrormode ();
	break;
    }
    return 1;
}

/* stop CD audio */
static int ioctl_command_stop (int unitnum)
{
    DWORD len;
    int cnt = 3;

    while (cnt-- > 0) {
        seterrormode ();
	if(!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_STOP_AUDIO, NULL, 0, NULL, 0, &len, NULL)) {
	    reseterrormode ();
	    if (win32_error (unitnum, "IOCTL_CDROM_STOP_AUDIO") < 0)
		continue;
	    return 0;
	}
	reseterrormode ();
	break;
    }
    return 1;
}

/* play CD audio */
static int ioctl_command_play (int unitnum, uae_u32 start, uae_u32 end, int scan)
{
    DWORD len;
    CDROM_PLAY_AUDIO_MSF pa;
    int cnt = 3;

    while (cnt-- > 0) {
	pa.StartingM = start >> 16;
	pa.StartingS = start >> 8;
	pa.StartingF = start >> 0;
	pa.EndingM = end >> 16;
	pa.EndingS = end >> 8;
	pa.EndingF = end >> 0;
	seterrormode ();
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_PLAY_AUDIO_MSF, &pa, sizeof(pa), NULL, 0, &len, NULL)) {
	    reseterrormode ();
	    if (win32_error (unitnum, "IOCTL_CDROM_PLAY_AUDIO_MSF %02.%02.%02-%02.%02.%02",
		pa.StartingM, pa.StartingS, pa.StartingF, pa.EndingM, pa.EndingS, pa.EndingF ) < 0) continue;
	    return 0;
	}
	reseterrormode ();
	break;
    }
    return 1;
}

/* read qcode */
static uae_u8 *ioctl_command_qcode (int unitnum)
{
    SUB_Q_CHANNEL_DATA qcd;
    DWORD len;
    ULONG in = 1;
    uae_u8 *p = ciw32[unitnum].tempbuffer;
    int cnt = 3;

    memset (p, 0, 4 + 12);
    p[1] = 0x15;
    p[3] = 12;
    while (cnt-- > 0) {
	reseterrormode ();
	if(!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_READ_Q_CHANNEL, &in, sizeof(in), &qcd, sizeof (qcd), &len, NULL)) {
	    reseterrormode ();
	    if (win32_error (unitnum, "IOCTL_CDROM_READ_Q_CHANNEL") < 0)
		continue;
	    return 0;
	}
	break;
    }
    reseterrormode ();
    p[1] = qcd.CurrentPosition.Header.AudioStatus;
    p += 4;
    p[1] = (qcd.CurrentPosition.Control << 0) | (qcd.CurrentPosition.ADR << 4);
    p[2] = qcd.CurrentPosition.TrackNumber;
    p[3] = qcd.CurrentPosition.IndexNumber;
    p[5] = qcd.CurrentPosition.AbsoluteAddress[1];
    p[6] = qcd.CurrentPosition.AbsoluteAddress[2];
    p[7] = qcd.CurrentPosition.AbsoluteAddress[3];
    p[9] = qcd.CurrentPosition.TrackRelativeAddress[1];
    p[10] = qcd.CurrentPosition.TrackRelativeAddress[2];
    p[11] = qcd.CurrentPosition.TrackRelativeAddress[3];
    return ciw32[unitnum].tempbuffer;
}

static uae_u8 *ioctl_command_read (int unitnum, int sector)
{
    DWORD dtotal;
    int cnt = 3;

    while (cnt-- > 0) {
	gui_cd_led (1);
	seterrormode ();
	if (SetFilePointer (ciw32[unitnum].h, sector * 2048, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
	    reseterrormode ();
	    if (win32_error (unitnum, "SetFilePointer") < 0)
		continue;
	    return 0;
	}
        reseterrormode ();
	break;
    }
    cnt = 3;
    while (cnt-- > 0) {
	gui_cd_led (1);
	if (!ReadFile (ciw32[unitnum].h, ciw32[unitnum].tempbuffer, 2048, &dtotal, 0)) {
	    reseterrormode ();
	    if (win32_error (unitnum, "ReadFile") < 0)
		continue;
	    return 0;
	}
	reseterrormode ();
	gui_cd_led (1);
	break;
    }
    return ciw32[unitnum].tempbuffer;
}

static int ioctl_command_write (int unitnum, int sector, uae_u8 *data)
{
    return 0;
}

static int fetch_geometry (int unitnum, struct device_info *di)
{
    DISK_GEOMETRY geom;
    DWORD len;
    int cnt = 3;

    while (cnt-- > 0) {
        seterrormode ();
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof(geom), &len, NULL)) {
	    reseterrormode ();
	    if (win32_error (unitnum, "IOCTL_CDROM_GET_DRIVE_GEOMETRY") < 0)
		continue;
	    return 0;
	}
        reseterrormode ();
	break;
    }
    if (di) {
        di->cylinders = geom.Cylinders.LowPart;
	di->sectorspertrack = geom.SectorsPerTrack;
	di->trackspercylinder = geom.TracksPerCylinder;
	di->bytespersector = geom.BytesPerSector;
    }
    return 1;
}


/* read toc */
static uae_u8 *ioctl_command_toc (int unitnum)
{
    CDROM_TOC toc;
    DWORD len;
    int i;
    uae_u8 *p = ciw32[unitnum].tempbuffer;
    int cnt = 3;

    gui_cd_led (1);
    while (cnt-- > 0) {
        seterrormode ();
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc), &len, NULL)) {
	    reseterrormode ();
	    if (win32_error (unitnum, "IOCTL_CDROM_READ_TOC") < 0)
		continue;
	    return 0;
	}
        reseterrormode ();
	break;
    }

    p[0] = ((toc.LastTrack + 4) * 11) >> 8;
    p[1] = ((toc.LastTrack + 4) * 11) & 0xff;
    p[2] = 1;
    p[3] = toc.LastTrack;
    p += 4;
    memset (p, 0, 11);
    p[0] = 1;
    p[1] = (toc.TrackData[0].Control << 0) | (toc.TrackData[0].Adr << 4);
    p[3] = 0xa0;
    p[8] = 1;
    p += 11;
    memset (p, 0, 11);
    p[0] = 1;
    p[1] = 0x10;
    p[3] = 0xa1;
    p[8] = toc.LastTrack;
    p += 11;
    memset (p, 0, 11);
    p[0] = 1;
    p[1] = 0x10;
    p[3] = 0xa2;
    p[8] = toc.TrackData[toc.LastTrack].Address[1];
    p[9] = toc.TrackData[toc.LastTrack].Address[2];
    p[10] = toc.TrackData[toc.LastTrack].Address[3];
    p += 11;
    for (i = 0; i < toc.LastTrack; i++) {
	memset (p, 0, 11);
	p[0] = 1;
	p[1] = (toc.TrackData[i].Control << 0) | (toc.TrackData[i].Adr << 4);
	p[2] = 0;
	p[3] = i + 1;
	p[8] = toc.TrackData[i].Address[1];
	p[9] = toc.TrackData[i].Address[2];
	p[10] = toc.TrackData[i].Address[3];
	p += 11;
    }
    gui_cd_led (1);
    return ciw32[unitnum].tempbuffer;
}

/* open device level access to cd rom drive */
static int sys_cddev_open (int unitnum)
{
    DWORD len;
    struct dev_info_ioctl *ciw = &ciw32[unitnum];
    DWORD flags;

    /* buffer must be page aligned for device access */
    ciw->tempbuffer = VirtualAlloc (NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    if (!ciw->tempbuffer) {
	write_log ("IOCTL: failed to allocate buffer");
	return 1;
    }
    flags = GENERIC_READ;
    ciw->h = CreateFile(ciw->devname, flags, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (ciw->h == INVALID_HANDLE_VALUE) {
	flags |= GENERIC_WRITE;
        ciw->h = CreateFile(ciw->devname, flags, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (ciw->h == INVALID_HANDLE_VALUE) {
	    write_log ("IOCTL: failed to open device handle (%s)\n", ciw->devname);
	    goto error;
	}
    }
    DeviceIoControl(ciw->h, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &len, NULL);
    ciw->mediainserted = ioctl_command_toc (unitnum) ? 1 : 0;
    write_log ("IOCTL: device '%s' opened succesfully (unit number=%d,media=%d)\n", ciw->devname, unitnum, ciw->mediainserted);
    ioctl_command_stop (unitnum);
    return 0;
error:
    win32_error (unitnum, "CreateFile");
    VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
    ciw->tempbuffer = NULL;
    CloseHandle (ciw->h);
    ciw->h = NULL;
    return -1;
}

static int unitcheck (int unitnum)
{
    if (unitnum >= MAX_TOTAL_DEVICES) {
	if (unitnum < 'A' || unitnum > 'Z')
	    return 0;
	return 1;
    }
    if (ciw32[unitnum].drvletter == 0)
	return 0;
    return 1;
}

/* close device handle */
void sys_cddev_close (int unitnum)
{
    if (!unitcheck (unitnum))
	return;
    CloseHandle (ciw32[unitnum].h);
    ciw32[unitnum].h = NULL;
    VirtualFree (ciw32[unitnum].tempbuffer, 0, MEM_RELEASE);
    ciw32[unitnum].tempbuffer = NULL;
}

static int open_device (int unitnum)
{
    if (!unitcheck (unitnum))
	return 0;
    if (sys_cddev_open (unitnum) == 0)
	return 1;
    return 0;
}
static void close_device (int unitnum)
{
    sys_cddev_close (unitnum);
}

static void close_bus (void)
{
}

static int total_devices;

static int open_bus (int flags)
{
    int dwDriveMask;
    int drive, i;
    char tmp[10];

    for (i = 0; i < MAX_TOTAL_DEVICES; i++)
	memset (&ciw32[i], 0, sizeof (struct dev_info_ioctl));
    total_devices = 0;
    dwDriveMask = GetLogicalDrives();
    if (log_scsi)
	write_log ("IOCTL: drive mask = %08.8X\n", dwDriveMask);
    dwDriveMask >>= 2; // Skip A and B drives...
    for( drive = 'C'; drive <= 'Z'; drive++) {
	if (dwDriveMask & 1) {
	    int dt;
	    sprintf( tmp, "%c:\\", drive );
	    dt = GetDriveType (tmp);
	    if (log_scsi)
		write_log ("IOCTL: drive %c type %d\n", drive, dt);
	    if (((flags & (1 << INQ_ROMD)) && dt == DRIVE_CDROM) || ((flags & (1 << INQ_DASD)) && dt == DRIVE_FIXED)) {
	        if (log_scsi)
		    write_log ("IOCTL: drive %c: = unit %d\n", drive, total_devices);
		ciw32[total_devices].drvletter = drive;
		ciw32[total_devices].type = dt;
		sprintf (ciw32[total_devices].devname,"\\\\.\\%c:", drive);
		total_devices++;
	    }
	}
	dwDriveMask >>= 1;
    }
    return total_devices;
}

static struct device_info *info_device (int unitnum, struct device_info *di)
{
    if (!unitcheck (unitnum))
	return 0;
    di->bus = unitnum;
    di->target = 0;
    di->lun = 0;
    di->media_inserted = 0;
    if (fetch_geometry (unitnum, di)) // || ioctl_command_toc (unitnum))
	di->media_inserted = 1;
    di->write_protected = 1;
    di->type = ciw32[unitnum].type == DRIVE_CDROM ? INQ_ROMD : INQ_DASD;
    di->id = ciw32[unitnum].drvletter;
    sprintf (di->label, "Drive %c:", ciw32[unitnum].drvletter);
    return di;
}

void win32_ioctl_media_change (char driveletter, int insert)
{
    int i;

    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (ciw32[i].drvletter == driveletter && ciw32[i].mediainserted != insert) {
	    write_log ("IOCTL: media change %c %d\n", driveletter, insert);
	    ciw32[i].mediainserted = insert;
	    scsi_do_disk_change (driveletter, insert);
	}
    }
}

struct device_functions devicefunc_win32_ioctl = {
    open_bus, close_bus, open_device, close_device, info_device,
    0, 0, 0,
    ioctl_command_pause, ioctl_command_stop, ioctl_command_play, ioctl_command_qcode,
    ioctl_command_toc, ioctl_command_read, ioctl_command_write
};

#endif
