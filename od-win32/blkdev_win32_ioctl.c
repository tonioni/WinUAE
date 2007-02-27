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

#include "uae.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"
#include "win32.h"

#include <devioctl.h>
#include <ntddcdrm.h>
#include <windows.h>
#include <mmsystem.h>

struct dev_info_ioctl {
    HANDLE h;
    uae_u8 *tempbuffer;
    char drvletter;
    char devname[30];
    int mediainserted;
    int type;
    int blocksize;
    int mciid;
    CDROM_TOC toc;
    UINT errormode;
    int playend;
};

#define IOCTL_DATA_BUFFER 4096

static int MCICDA;

static struct dev_info_ioctl ciw32[MAX_TOTAL_DEVICES];

static void seterrormode (int unitnum)
{
    ciw32[unitnum].errormode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
}
static void reseterrormode (int unitnum)
{
    SetErrorMode (ciw32[unitnum].errormode);
}

static void close_device (int unitnum);
static int open_device (int unitnum);

static void mcierr(char *str, DWORD err)
{
    char es[1000];
    if (err == MMSYSERR_NOERROR)
	return;
    if (mciGetErrorString(err, es, sizeof es))
	write_log("MCIErr: %s: %d = '%s'\n", str, err, es);
}

static int win32_error (int unitnum, const char *format,...)
{
    LPVOID lpMsgBuf;
    va_list arglist;
    char buf[1000];
    DWORD err = GetLastError();

    if (err == ERROR_WRONG_DISK) {
	write_log ("IOCTL: media change, re-opening device\n");
	close_device (unitnum);
	if (!open_device (unitnum))
	    write_log ("IOCTL: re-opening failed!\n");
	return -1;
    }
    va_start (arglist, format );
    vsprintf (buf, format, arglist);
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
	NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	(LPTSTR)&lpMsgBuf, 0, NULL);
    if (log_scsi)
	write_log ("IOCTL: unit=%d %s,%d: %s\n", unitnum, buf, err, (char*)lpMsgBuf);
    va_end(arglist);
    return err;
}

static int close_createfile(int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    if (ciw->h != INVALID_HANDLE_VALUE) {
	if (log_scsi)
	    write_log("IOCTL: IOCTL close\n");
	CloseHandle(ciw->h);
	if (log_scsi)
	    write_log("IOCTL: IOCTL close completed\n");
	ciw->h = INVALID_HANDLE_VALUE;
	return 1;
    }
    return 0;
}

static int close_mci(int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];
    MCI_GENERIC_PARMS gp = { 0 };

    ciw->playend = -1;
    if (ciw->mciid > 0) {
	if (log_scsi)
	    write_log("IOCTL: MCI close\n");
	mcierr("MCI_STOP", mciSendCommand(ciw->mciid, MCI_STOP, MCI_WAIT, (DWORD_PTR)&gp));
	mcierr("MCI_CLOSE", mciSendCommand(ciw->mciid, MCI_CLOSE, MCI_WAIT, (DWORD_PTR)&gp));
	if (log_scsi)
	    write_log("IOCTL: MCI close completed\n");
        ciw->mciid = 0;
	return 1;
    }
    return 0;
}

static int open_createfile(int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];
    int closed = 0;
    int cnt = 50;
    DWORD flags;

    if (ciw->h != INVALID_HANDLE_VALUE)
	return 1;
    closed = close_mci(unitnum);
    if (log_scsi)
	write_log("IOCTL: opening IOCTL %s\n", ciw->devname);
    for (;;) {
	flags = GENERIC_READ;
	ciw->h = CreateFile(ciw->devname, flags, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (ciw->h == INVALID_HANDLE_VALUE) {
	    ciw->h = CreateFile(ciw->devname, flags, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	    if (ciw->h == INVALID_HANDLE_VALUE) {
		flags |= GENERIC_WRITE;
		ciw->h = CreateFile(ciw->devname, flags, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (ciw->h == INVALID_HANDLE_VALUE) {
		    DWORD err = GetLastError();
		    if (err == ERROR_SHARING_VIOLATION) {
			if (closed && cnt > 0) {
			    cnt--;
			    Sleep(10);
			    continue;
			}
		    }
		    if (closed)
			write_log("IOCTL: failed to re-open '%s', err=%d\n", ciw->devname, GetLastError());
		    return 0;
		}
	    }
	}
	break;
    }
    if (log_scsi)
        write_log("IOCTL: IOCTL open completed\n");
    return 1;
}

static int open_mci(int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];
    char elname[10];
    char alname[100];
    MCI_OPEN_PARMS mciOpen;
    DWORD err, flags;
    int closed = 0;

    if (ciw->mciid > 0 || MCICDA == 0)
	return 1;
    ciw->playend = -1;
    closed = close_createfile(unitnum);
    if (log_scsi)
	write_log("IOCTL: MCI opening %c:\n", ciw->drvletter);
    memset (&mciOpen, 0, sizeof(mciOpen));
    mciOpen.lpstrDeviceType = (LPSTR)MCI_DEVTYPE_CD_AUDIO;
    sprintf(elname,"%c:", ciw->drvletter);
    sprintf(alname,"CD%u:", GetCurrentTime());
    mciOpen.lpstrElementName = elname;
    mciOpen.lpstrAlias = alname;
    flags = MCI_OPEN_ELEMENT | MCI_OPEN_SHAREABLE | MCI_OPEN_ALIAS | MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID | MCI_WAIT;
    err = mciSendCommand(0, MCI_OPEN, flags, (DWORD)(LPVOID)&mciOpen);
    ciw->mciid = mciOpen.wDeviceID;
    if (err != MMSYSERR_NOERROR) {
	if (closed)
	    mcierr("MCI_OPEN", err);
	return 0;
    }
    if (log_scsi)
        write_log("IOCTL: MCI open completed\n");
    return 1;
}

/* pause/unpause CD audio */
static int ioctl_command_pause (int unitnum, int paused)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    if (ciw->mciid > 0) {

        MCI_GENERIC_PARMS gp = { 0 };
	if (paused)
	    mcierr("MCI_PAUSE", mciSendCommand(ciw->mciid, MCI_PAUSE, MCI_WAIT, (DWORD_PTR)&gp));
	else
	    mcierr("MCI_RESUME", mciSendCommand(ciw->mciid, MCI_RESUME, MCI_WAIT, (DWORD_PTR)&gp));

    } else {

	DWORD len;
	int command = paused ? IOCTL_CDROM_PAUSE_AUDIO : IOCTL_CDROM_RESUME_AUDIO;
	int cnt = 3;

	while (cnt-- > 0) {
	    seterrormode (unitnum);
	    if (!DeviceIoControl(ciw32[unitnum].h, command, NULL, 0, NULL, 0, &len, NULL)) {
		reseterrormode (unitnum);
		if (win32_error (unitnum, paused ? "IOCTL_CDROM_PAUSE_AUDIO" : "IOCTL_CDROM_RESUME_AUDIO") < 0)
		    continue;
		return 0;
	    }
	    reseterrormode (unitnum);
	    break;
	}
    }
    return 1;
}

/* stop CD audio */
static int ioctl_command_stop (int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    if (ciw->mciid > 0) {

        MCI_GENERIC_PARMS gp = { 0 };
	mcierr("MCI_STOP", mciSendCommand(ciw->mciid, MCI_STOP, MCI_WAIT, (DWORD_PTR)&gp));
	ciw->playend = -1;

    } else {

	DWORD len;
	int cnt = 3;

	while (cnt-- > 0) {
	    seterrormode (unitnum);
	    if(!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_STOP_AUDIO, NULL, 0, NULL, 0, &len, NULL)) {
		reseterrormode (unitnum);
		if (win32_error (unitnum, "IOCTL_CDROM_STOP_AUDIO") < 0)
		    continue;
		return 0;
	    }
	    reseterrormode (unitnum);
	    break;
	}
    }
    return 1;
}

/* play CD audio */
static int ioctl_command_play (int unitnum, uae_u32 start, uae_u32 end, int scan)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    open_mci(unitnum);

    if (ciw->mciid > 0) {

	MCI_SET_PARMS setParms = {0};
	MCI_PLAY_PARMS playParms = {0};

	setParms.dwTimeFormat = MCI_FORMAT_MSF;
	mcierr("MCI_SET", mciSendCommand(ciw->mciid, MCI_SET, MCI_SET_TIME_FORMAT | MCI_WAIT, (DWORD_PTR)&setParms));
	playParms.dwFrom = MCI_MAKE_MSF((start >> 16) & 0xff, (start >> 8) & 0xff, start & 0xff);
	playParms.dwTo = MCI_MAKE_MSF((end >> 16) & 0xff, (end >> 8) & 0xff, end & 0xff);
	mcierr("MCI_PLAY", mciSendCommand(ciw->mciid, MCI_PLAY, MCI_FROM | MCI_TO, (DWORD_PTR)&playParms));
	ciw->playend = end;

    } else {

	DWORD len;
	CDROM_PLAY_AUDIO_MSF pa;
	int cnt = 3;

#if 0
	{
	VOLUME_CONTROL vc;
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_GET_VOLUME, NULL, 0, &vc, sizeof(vc), &len, NULL))
	    write_log("IOCTL_CDROM_GET_VOLUME %d\n", GetLastError());
	vc.PortVolume[0] = 0xff;
	vc.PortVolume[1] = 0xff;
	vc.PortVolume[2] = 0xff;
	vc.PortVolume[3] = 0xff;
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_SET_VOLUME, &vc, sizeof(vc), NULL, 0, &len, NULL))
	    write_log("IOCTL_CDROM_SET_VOLUME %d\n", GetLastError());
	}
#endif   

	while (cnt-- > 0) {
	    pa.StartingM = start >> 16;
	    pa.StartingS = start >> 8;
	    pa.StartingF = start >> 0;
	    pa.EndingM = end >> 16;
	    pa.EndingS = end >> 8;
	    pa.EndingF = end >> 0;
	    seterrormode (unitnum);
	    if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_PLAY_AUDIO_MSF, &pa, sizeof(pa), NULL, 0, &len, NULL)) {
		reseterrormode (unitnum);
		if (win32_error (unitnum, "IOCTL_CDROM_PLAY_AUDIO_MSF %02.%02.%02-%02.%02.%02",
		    pa.StartingM, pa.StartingS, pa.StartingF, pa.EndingM, pa.EndingS, pa.EndingF ) < 0) continue;
		return 0;
	    }
	    reseterrormode (unitnum);
	    break;
	}
    }

    return 1;
}

/* convert minutes, seconds and frames -> logical sector number */
static int msf2lsn (int	msf)
{
    int sector = (((msf >> 16) & 0xff) * 60 * 75 + ((msf >> 8) & 0xff) * 75 + ((msf >> 0) & 0xff));
    return sector;
}

/* convert logical sector number -> minutes, seconds and frames */
static int lsn2msf (int	sectors)
{
    int msf;
    msf = (sectors / (75 * 60)) << 16;
    msf |= ((sectors / 75) % 60) << 8;
    msf |= (sectors % 75) << 0;
    return msf;
}

/* read qcode */
static uae_u8 *ioctl_command_qcode (int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    if (ciw->mciid > 0) {

        static uae_u8 buf[4 + 12];
        MCI_STATUS_PARMS mciStatusParms;
	DWORD err, mode;
	uae_u8 *p;
	uae_u32 pos, pos2;
	int trk;

	memset(buf, 0, sizeof buf);
	memset(&mciStatusParms, 0, sizeof mciStatusParms);
	mciStatusParms.dwItem = MCI_STATUS_MODE;
        err = mciSendCommand(ciw->mciid, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD)(LPVOID)&mciStatusParms);
	if (err != MMSYSERR_NOERROR)
	    return 0;
	mode = mciStatusParms.dwReturn;
	mciStatusParms.dwItem = MCI_STATUS_CURRENT_TRACK;
        err = mciSendCommand(ciw->mciid, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD)(LPVOID)&mciStatusParms);
	if (err != MMSYSERR_NOERROR)
	    return 0;
	trk = mciStatusParms.dwReturn - 1;
	if (trk < 0)
	    trk = 0;
	mciStatusParms.dwItem = MCI_STATUS_POSITION;
        err = mciSendCommand(ciw->mciid, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD)(LPVOID)&mciStatusParms);
	if (err != MMSYSERR_NOERROR)
	    return 0;
	pos = (((mciStatusParms.dwReturn >> 16) & 0xff) << 0) | (((mciStatusParms.dwReturn >> 8) & 0xff) << 8) | (((mciStatusParms.dwReturn >> 0) & 0xff) << 16);

	p = buf;
	p[1] = AUDIO_STATUS_NO_STATUS;
	if (mode == MCI_MODE_PLAY)
	    p[1] = AUDIO_STATUS_IN_PROGRESS;
	else if (mode == MCI_MODE_PAUSE)
	    p[1] = AUDIO_STATUS_PAUSED;
	p[3] = 12;

	p = buf + 4;
	p[1] = (ciw->toc.TrackData[trk].Control << 0) | (ciw->toc.TrackData[trk].Adr << 4);
	p[2] = trk + 1;
	p[3] = 1;

	p[5] = (pos >> 16) & 0xff;
	p[6] = (pos >> 8) & 0xff;
	p[7] = (pos >> 0) & 0xff;

	pos = msf2lsn(pos);
	pos2 = (ciw->toc.TrackData[trk].Address[1] << 16) | (ciw->toc.TrackData[trk].Address[2] << 8) | (ciw->toc.TrackData[trk].Address[3] << 0);
	pos -= msf2lsn(pos2);
	if (pos < 0)
	    pos = 0;
	pos = lsn2msf(pos);

	p[9] = (pos >> 16) & 0xff;
	p[10] = (pos >> 8) & 0xff;
	p[11] = (pos >> 0) & 0xff;

	return buf;

    } else {

	SUB_Q_CHANNEL_DATA qcd;
	DWORD len;
	ULONG in = 1;
	uae_u8 *p = ciw32[unitnum].tempbuffer;
	int cnt = 3;

	memset (p, 0, 4 + 12);
	p[1] = 0x15;
	p[3] = 12;
	while (cnt-- > 0) {
	    reseterrormode (unitnum);
	    if(!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_READ_Q_CHANNEL, &in, sizeof(in), &qcd, sizeof (qcd), &len, NULL)) {
		reseterrormode (unitnum);
		if (win32_error (unitnum, "IOCTL_CDROM_READ_Q_CHANNEL") < 0)
		    continue;
		return 0;
	    }
	    break;
	}
	reseterrormode (unitnum);
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
}

static int ioctl_command_readwrite (int unitnum, int sector, int write)
{
    DWORD dtotal;
    int cnt = 3;

    if (!open_createfile(unitnum))
	return 0;
    while (cnt-- > 0) {
	gui_cd_led (1);
	seterrormode (unitnum);
	if (SetFilePointer (ciw32[unitnum].h, sector * ciw32[unitnum].blocksize, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
	    reseterrormode (unitnum);
	    if (win32_error (unitnum, "SetFilePointer") < 0)
		continue;
	    return 0;
	}
	reseterrormode (unitnum);
	break;
    }
    cnt = 3;
    while (cnt-- > 0) {
	gui_cd_led (1);
	seterrormode (unitnum);
	if (write) {
	    if (!WriteFile (ciw32[unitnum].h, ciw32[unitnum].tempbuffer, ciw32[unitnum].blocksize, &dtotal, 0)) {
		int err;
		reseterrormode (unitnum);
		err = win32_error (unitnum, "WriteFile");
		if (err < 0)
		    continue;
		if (err == ERROR_WRITE_PROTECT)
		    return -1;
		return 0;
	    }
	} else {
	    if (!ReadFile (ciw32[unitnum].h, ciw32[unitnum].tempbuffer, ciw32[unitnum].blocksize, &dtotal, 0)) {
		reseterrormode (unitnum);
		if (win32_error (unitnum, "ReadFile") < 0)
		    continue;
		return 0;
	    }
	}
	reseterrormode (unitnum);
	gui_cd_led (1);
	break;
    }
    return 1;
}

static int ioctl_command_write (int unitnum, int sector)
{
    return ioctl_command_readwrite (unitnum, sector, 1);
}
static uae_u8 *ioctl_command_read (int unitnum, int sector)
{
    if (ioctl_command_readwrite (unitnum, sector, 0) > 0)
	return ciw32[unitnum].tempbuffer;
    return NULL;
}

static int fetch_geometry (int unitnum, struct device_info *di)
{
    DISK_GEOMETRY geom;
    DWORD len;
    int cnt = 3;

    if (!open_createfile(unitnum))
	return 0;
    while (cnt-- > 0) {
	seterrormode (unitnum);
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof(geom), &len, NULL)) {
	    reseterrormode (unitnum);
	    if (win32_error (unitnum, "IOCTL_CDROM_GET_DRIVE_GEOMETRY") < 0)
		continue;
	    return 0;
	}
	reseterrormode (unitnum);
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

static int ismedia (int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    if (ciw->mciid > 0) {

	DWORD err;
	MCI_STATUS_PARMS mciStatusParms;

	mciStatusParms.dwItem = MCI_STATUS_MEDIA_PRESENT;
	err = mciSendCommand(ciw->mciid, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD)(LPVOID)&mciStatusParms);
	if (err != MMSYSERR_NOERROR)
	    return 0;
	if (mciStatusParms.dwReturn)
	    return 1;
	return 0;

    } else {
	struct device_info di;
	memset(&di, 0, sizeof di);
	return fetch_geometry (unitnum, &di);
    }
}

/* read toc */
static uae_u8 *ioctl_command_toc (int unitnum)
{
    DWORD len;
    int i;
    uae_u8 *p = ciw32[unitnum].tempbuffer;
    int cnt = 3;
    CDROM_TOC *toc = &ciw32[unitnum].toc;

    if (!open_createfile(unitnum))
	return 0;
    gui_cd_led (1);
    while (cnt-- > 0) {
	seterrormode (unitnum);
	if (!DeviceIoControl(ciw32[unitnum].h, IOCTL_CDROM_READ_TOC, NULL, 0, toc, sizeof(CDROM_TOC), &len, NULL)) {
	    reseterrormode (unitnum);
	    if (win32_error (unitnum, "IOCTL_CDROM_READ_TOC") < 0)
		continue;
	    return 0;
	}
	reseterrormode (unitnum);
	break;
    }

    p[0] = ((toc->LastTrack + 4) * 11) >> 8;
    p[1] = ((toc->LastTrack + 4) * 11) & 0xff;
    p[2] = 1;
    p[3] = toc->LastTrack;
    p += 4;
    memset (p, 0, 11);
    p[0] = 1;
    p[1] = (toc->TrackData[0].Control << 0) | (toc->TrackData[0].Adr << 4);
    p[3] = 0xa0;
    p[8] = 1;
    p += 11;
    memset (p, 0, 11);
    p[0] = 1;
    p[1] = 0x10;
    p[3] = 0xa1;
    p[8] = toc->LastTrack;
    p += 11;
    memset (p, 0, 11);
    p[0] = 1;
    p[1] = 0x10;
    p[3] = 0xa2;
    p[8] = toc->TrackData[toc->LastTrack].Address[1];
    p[9] = toc->TrackData[toc->LastTrack].Address[2];
    p[10] = toc->TrackData[toc->LastTrack].Address[3];
    p += 11;
    for (i = 0; i < toc->LastTrack; i++) {
	memset (p, 0, 11);
	p[0] = 1;
	p[1] = (toc->TrackData[i].Control << 0) | (toc->TrackData[i].Adr << 4);
	p[2] = 0;
	p[3] = i + 1;
	p[8] = toc->TrackData[i].Address[1];
	p[9] = toc->TrackData[i].Address[2];
	p[10] = toc->TrackData[i].Address[3];
	p += 11;
    }
    gui_cd_led (1);
    return ciw32[unitnum].tempbuffer;
}

/* open device level access to cd rom drive */
static int sys_cddev_open (int unitnum)
{
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    /* buffer must be page aligned for device access */
    ciw->tempbuffer = VirtualAlloc (NULL, IOCTL_DATA_BUFFER, MEM_COMMIT, PAGE_READWRITE);
    if (!ciw->tempbuffer) {
	write_log ("IOCTL: failed to allocate buffer");
	return 1;
    }
    if (!open_createfile(unitnum)) {
	write_log ("IOCTL: failed to open '%s', err=%d\n", ciw->devname, GetLastError());
	goto error;
    }
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
    struct dev_info_ioctl *ciw = &ciw32[unitnum];

    if (!unitcheck (unitnum))
	return;
    close_createfile(unitnum);
    close_mci(unitnum);
    VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
    ciw->tempbuffer = NULL;
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

    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	memset (&ciw32[i], 0, sizeof (struct dev_info_ioctl));
	ciw32[i].h = INVALID_HANDLE_VALUE;
    }
    MCICDA = 1;//os_vista ? 1 : 0;
    total_devices = 0;
    dwDriveMask = GetLogicalDrives();
    if (log_scsi)
	write_log ("IOCTL: drive mask = %08.8X\n", dwDriveMask);
    dwDriveMask >>= 2; // Skip A and B drives...
    for( drive = 'C'; drive <= 'Z'; drive++) {
	if (dwDriveMask & 1) {
	    int dt;
	    sprintf(tmp, "%c:\\", drive);
	    dt = GetDriveType (tmp);
	    if (log_scsi)
		write_log ("IOCTL: drive %c type %d\n", drive, dt);
	    if (((flags & (1 << INQ_ROMD)) && dt == DRIVE_CDROM) || ((flags & (1 << INQ_DASD)) && dt == DRIVE_FIXED)) {
		if (log_scsi)
		    write_log ("IOCTL: drive %c: = unit %d\n", drive, total_devices);
		ciw32[total_devices].drvletter = drive;
		ciw32[total_devices].type = dt;
		ciw32[total_devices].blocksize = 2048;
		sprintf (ciw32[total_devices].devname,"\\\\.\\%c:", drive);
		total_devices++;
	    }
	}
	dwDriveMask >>= 1;
    }
    return total_devices;
}

static int ioctl_ismedia (int unitnum, int quick)
{
    if (!unitcheck (unitnum))
	return 0;
    if (quick) {
        struct dev_info_ioctl *ciw = &ciw32[unitnum];
	return ciw->mediainserted;
    } 
    return ismedia(unitnum);
}

static struct device_info *info_device (int unitnum, struct device_info *di)
{
    if (!unitcheck (unitnum))
	return 0;
    di->bus = unitnum;
    di->target = 0;
    di->lun = 0;
    di->media_inserted = 0;
    di->bytespersector = 2048;
    if (fetch_geometry (unitnum, di)) { // || ioctl_command_toc (unitnum))
	di->media_inserted = 1;
	ciw32[unitnum].blocksize = di->bytespersector;
    }
    di->write_protected = ciw32[unitnum].type == DRIVE_CDROM ? 1 : 0;
    di->type = ciw32[unitnum].type == DRIVE_CDROM ? INQ_ROMD : INQ_DASD;
    di->id = ciw32[unitnum].drvletter;
    di->label = xmalloc(16);
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

static struct device_scsi_info *ioctl_scsi_info (int unitnum, struct device_scsi_info *dsi)
{
    dsi->buffer = ciw32[unitnum].tempbuffer;
    dsi->bufsize = IOCTL_DATA_BUFFER;
    return dsi;
}

struct device_functions devicefunc_win32_ioctl = {
    open_bus, close_bus, open_device, close_device, info_device,
    0, 0, 0,
    ioctl_command_pause, ioctl_command_stop, ioctl_command_play, ioctl_command_qcode,
    ioctl_command_toc, ioctl_command_read, ioctl_command_write, 0, ioctl_scsi_info, ioctl_ismedia
};

#endif
