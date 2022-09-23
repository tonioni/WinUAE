/*
* UAE - The Un*x Amiga Emulator
*
* WIN32 CDROM/HD low level access code (IOCTL)
*
* Copyright 2002-2010 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WINDDK

#include "options.h"
#include "traps.h"
#include "uae.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"
#include "win32.h"
#include "audio.h"

#include <devioctl.h>
#include <ntddcdrm.h>
#include <windows.h>
#include <mmsystem.h>
#include <winioctl.h>
#include <setupapi.h>   // for SetupDiXxx functions.
#include <stddef.h>

#include "cda_play.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#include <ntddscsi.h>

#define IOCTL_DATA_BUFFER 8192

struct dev_info_ioctl {
	HANDLE h;
	uae_u8 *tempbuffer;
	TCHAR drvletter;
	TCHAR drvlettername[10];
	TCHAR devname[30];
	int type;
	CDROM_TOC cdromtoc;
	uae_u8 trackmode[100];
	UINT errormode;
	int fullaccess;
	struct device_info di;
	bool open;
	bool usesptiread;
	bool changed;
	struct cda_play cda;
};

static struct dev_info_ioctl ciw32[MAX_TOTAL_SCSI_DEVICES];
static int unittable[MAX_TOTAL_SCSI_DEVICES];
static int bus_open;
static uae_sem_t play_sem;

static void seterrormode (struct dev_info_ioctl *ciw)
{
	ciw->errormode = SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
}
static void reseterrormode (struct dev_info_ioctl *ciw)
{
	SetErrorMode (ciw->errormode);
}

static int sys_cddev_open (struct dev_info_ioctl *ciw, int unitnum);
static void sys_cddev_close (struct dev_info_ioctl *ciw, int unitnum);

static int getunitnum (struct dev_info_ioctl *ciw)
{
	if (!ciw)
		return -1;
	int idx = (int)(ciw - &ciw32[0]);
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (unittable[i] - 1 == idx)
			return i;
	}
	return -1;
}

static struct dev_info_ioctl *unitcheck (int unitnum)
{
	if (unitnum < 0 || unitnum >= MAX_TOTAL_SCSI_DEVICES)
		return NULL;
	if (unittable[unitnum] <= 0)
		return NULL;
	unitnum = unittable[unitnum] - 1;
	if (ciw32[unitnum].drvletter == 0)
		return NULL;
	return &ciw32[unitnum];
}

static struct dev_info_ioctl *unitisopen (int unitnum)
{
	struct dev_info_ioctl *di = unitcheck (unitnum);
	if (!di)
		return NULL;
	if (di->open == false)
		return NULL;
	return di;
}

static int mcierr (TCHAR *str, DWORD err)
{
	TCHAR es[1000];
	if (err == MMSYSERR_NOERROR)
		return MMSYSERR_NOERROR;
	if (mciGetErrorString (err, es, sizeof es / sizeof(TCHAR)))
		write_log (_T("MCIErr: %s: %d = '%s'\n"), str, err, es);
	return err;
}

static int win32_error (struct dev_info_ioctl *ciw, int unitnum, const TCHAR *format,...)
{
	LPVOID lpMsgBuf;
	va_list arglist;
	TCHAR buf[1000];
	DWORD err = GetLastError ();

	if (err == ERROR_WRONG_DISK) {
		write_log (_T("IOCTL: media change, re-opening device\n"));
		sys_cddev_close (ciw, unitnum);
		if (sys_cddev_open (ciw, unitnum))
			write_log (_T("IOCTL: re-opening failed!\n"));
		return -1;
	}
	va_start (arglist, format);
	_vsntprintf (buf, sizeof buf / sizeof (TCHAR), format, arglist);
	FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	write_log (_T("IOCTL ERR: unit=%d,%s,%d: %s\n"), unitnum, buf, err, (TCHAR*)lpMsgBuf);
	va_end (arglist);
	return err;
}

static int close_createfile (struct dev_info_ioctl *ciw)
{
	ciw->fullaccess = 0;
	if (ciw->h != INVALID_HANDLE_VALUE) {
		if (log_scsi)
			write_log (_T("IOCTL: IOCTL close\n"));
		CloseHandle (ciw->h);
		if (log_scsi)
			write_log (_T("IOCTL: IOCTL close completed\n"));
		ciw->h = INVALID_HANDLE_VALUE;
		return 1;
	}
	return 0;
}

static int open_createfile (struct dev_info_ioctl *ciw, int fullaccess)
{
	int cnt = 50;
	DWORD len;

	if (ciw->h != INVALID_HANDLE_VALUE) {
		if (fullaccess && ciw->fullaccess == 0) {
			close_createfile (ciw);
		} else {
			return 1;
		}
	}
	if (log_scsi)
		write_log (_T("IOCTL: opening IOCTL %s\n"), ciw->devname);
	for (;;) {
		if (fullaccess) {
			ciw->h = CreateFile (ciw->devname, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (ciw->h != INVALID_HANDLE_VALUE)
				ciw->fullaccess = 1;
		} else {
			DWORD flags = GENERIC_READ;
			ciw->h = CreateFile (ciw->devname, flags, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (ciw->h == INVALID_HANDLE_VALUE) {
				ciw->h = CreateFile (ciw->devname, flags, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (ciw->h == INVALID_HANDLE_VALUE) {
					flags |= GENERIC_WRITE;
					ciw->h = CreateFile (ciw->devname, flags, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				}
			}
		}
		if (ciw->h != INVALID_HANDLE_VALUE)
			break;
		write_log (_T("IOCTL: failed to open '%s', err=%d\n"), ciw->devname, GetLastError ());
		return 0;
	}
	if (!DeviceIoControl (ciw->h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &len, NULL))
		write_log (_T("IOCTL: FSCTL_ALLOW_EXTENDED_DASD_IO returned %d\n"), GetLastError ());
	if (log_scsi)
		write_log (_T("IOCTL: IOCTL open completed\n"));
	return 1;
}


typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
	SCSI_PASS_THROUGH_DIRECT spt;
	ULONG Filler;
	UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

static int do_raw_scsi (struct dev_info_ioctl *ciw, int unitnum, uae_u8 *cmd, int cmdlen, uae_u8 *data, int datalen)
{
	DWORD status, returned;
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	uae_u8 *p = ciw->tempbuffer;
	if (!open_createfile (ciw, 1))
		return 0;
	memset (&swb, 0, sizeof (swb));
	memcpy (swb.spt.Cdb, cmd, cmdlen);
	memset(data, 0, datalen > 2352 + SUB_CHANNEL_SIZE ? 2352 + SUB_CHANNEL_SIZE : datalen);
	swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
	swb.spt.CdbLength = cmdlen;
	swb.spt.DataIn = SCSI_IOCTL_DATA_IN;
	swb.spt.DataTransferLength = datalen;
	swb.spt.DataBuffer = p;
	swb.spt.TimeOutValue = 80 * 60;
	swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
	swb.spt.SenseInfoLength = 32;
	seterrormode (ciw);
	status = DeviceIoControl (ciw->h, IOCTL_SCSI_PASS_THROUGH_DIRECT,
		&swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&returned, NULL);
	reseterrormode (ciw);
	if (!status) {
		DWORD err = GetLastError ();
		write_log (_T("IOCTL_RAW_SCSI unit %d, CMD=%d, ERR=%d\n"), unitnum, cmd[0], err);
		return 0;
	}
	int tlen = swb.spt.DataTransferLength > datalen ? datalen : swb.spt.DataTransferLength;
	memcpy (data, p, tlen);
	return tlen;
}

static int spti_inquiry (struct dev_info_ioctl *ciw, int unitnum, uae_u8 *data, int *datalen)
{
	uae_u8 cmd[6] = { 0x12,0,0,0,36,0 }; /* INQUIRY */
	int len = sizeof cmd;
	*datalen = do_raw_scsi (ciw, unitnum, cmd, len, data, 254);
	return 1;
}

static void sub_deinterleave (const uae_u8 *s, uae_u8 *d)
{
	for (int i = 0; i < 8 * 12; i ++) {
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

static void sub_to_deinterleaved (const uae_u8 *s, uae_u8 *d)
{
	for (int i = 0; i < 8 * 12; i ++) {
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

static int spti_read (struct dev_info_ioctl *ciw, int unitnum, uae_u8 *data, int sector, int sectorsize)
{
	uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
	int tlen = sectorsize;

	write_log(_T("spti_read %d %d %d\n"), unitnum, sector, sectorsize);

	if (sectorsize == 2048 || sectorsize == 2336 || sectorsize == 2328) {
		cmd[9] |= 1 << 4; // userdata
	} else if (sectorsize >= 2352) {
		cmd[9] |= 1 << 4; // userdata
		cmd[9] |= 1 << 3; // EDC&ECC
		cmd[9] |= 1 << 7; // sync
		cmd[9] |= 3 << 5; // header code
		if (sectorsize > 2352) {
			cmd[10] |= 1; // RAW P-W
		}
		if (sectorsize > 2352 + SUB_CHANNEL_SIZE) {
			cmd[9] |= 0x2 << 1; // C2
		}
	}
	cmd[3] = (uae_u8)(sector >> 16);
	cmd[4] = (uae_u8)(sector >> 8);
	cmd[5] = (uae_u8)(sector >> 0);
	if (unitnum >= 0)
		gui_flicker_led (LED_CD, unitnum, LED_CD_ACTIVE);
	int len = sizeof cmd;
	return do_raw_scsi (ciw, unitnum, cmd, len, data, tlen);
}

extern void encode_l2 (uae_u8 *p, int address);

static int read2048 (struct dev_info_ioctl *ciw, int sector)
{
	LARGE_INTEGER offset;

	seterrormode (ciw);
	offset.QuadPart = (uae_u64)sector * 2048;
	if (SetFilePointer (ciw->h, offset.LowPart, &offset.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR) {
		reseterrormode (ciw);
		return 0;
	}
	DWORD dtotal = 0;
	ReadFile (ciw->h, ciw->tempbuffer, 2048, &dtotal, 0);
	reseterrormode (ciw);
	return dtotal;
}

static int read_block (struct dev_info_ioctl *ciw, int unitnum, uae_u8 *data, int sector, int size, int sectorsize)
{
	DWORD len;
	uae_u8 *p = ciw->tempbuffer;
	int ret;
	int origsize = size;
	int origsector = sector;
	uae_u8 *origdata = data;
	bool got;

retry:
	if (!open_createfile (ciw, ciw->usesptiread ? 1 : 0))
		return 0;
	ret = 0;
	while (size > 0) {
		int track = cdtracknumber(&ciw->di.toc, sector);
		if (track < 0)
			return 0;
		got = false;
		if (!ciw->usesptiread && sectorsize == 2048 && ciw->trackmode[track] == 0) {
			if (read2048 (ciw, sector) == 2048) {
				memcpy (data, p, 2048);
				sector++;
				data += sectorsize;
				ret += sectorsize;
				got = true;
			}
		}
		if (!got && !ciw->usesptiread) {
			RAW_READ_INFO rri;
			rri.DiskOffset.QuadPart = sector * 2048;
			rri.SectorCount = 1;
			rri.TrackMode = (sectorsize > 2352 + 96) ? RawWithC2AndSubCode : RawWithSubCode;
			len = sectorsize;
			memset (p, 0, sectorsize);
			seterrormode (ciw);
			if (DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri, p, IOCTL_DATA_BUFFER, &len, NULL)) {
				reseterrormode (ciw);
				if (data) {
					uae_u8 mode = data[15];
					uae_u8 oldmode = ciw->trackmode[track];
					ciw->trackmode[track] = mode;
					if (oldmode == 0xff && mode == 0 && sectorsize == 2048) {
						// it is MODE0 track, we can do normal read
						goto retry;
					}
					if (sectorsize >= 2352) {
						memcpy (data, p, sectorsize);
						data += sectorsize;
						ret += sectorsize;
					} else {
						memcpy (data, p + 16, sectorsize);
						data += sectorsize;
						ret += sectorsize;
					}
				}
				got = true;
			} else {
				reseterrormode (ciw);
				DWORD err = GetLastError ();
				write_log (_T("IOCTL_CDROM_RAW_READ(%d,%d) failed, err=%d\n"), sector, rri.TrackMode, err);
				if ((err == ERROR_INVALID_FUNCTION || err == ERROR_INVALID_PARAMETER) && origsector == sector && origdata == data && origsector >= 0) {
					write_log (_T("-> fallback to SPTI mode\n"));
					ciw->usesptiread = true;
					size = origsize;
					goto retry;
				}
			}
		}
		if (!got) {
			int len = spti_read (ciw, unitnum, data, sector, sectorsize);
			if (len) {
				if (data) {
					memcpy (data, p, sectorsize);
					data += sectorsize;
					ret += sectorsize;
				}
				got = true;
			}
		}
		if (!got) {
			int dtotal = read2048 (ciw, sector);
			if (dtotal != 2048)
				return ret;
			if (sectorsize >= 2352) {
				memset (data, 0, 16);
				memcpy (data + 16, p, 2048);
				encode_l2 (data, sector + 150);
				if (sectorsize > 2352)
					memset (data + 2352, 0, sectorsize - 2352);
				sector++;
				data += sectorsize;
				ret += sectorsize;
			} else if (sectorsize == 2048) {
				memcpy (data, p, 2048);
				sector++;
				data += sectorsize;
				ret += sectorsize;
			}
			got = true;
		}
		sector++;
		size--;
	}
	return ret;
}

static int read_block_cda(struct cda_play *cda, int unitnum, uae_u8 *data, int sector, int size, int sectorsize)
{
	return read_block(&ciw32[unitnum], unitnum, data, sector, size, sectorsize);
}

/* pause/unpause CD audio */
static int ioctl_command_pause (int unitnum, int paused)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return -1;
	int old = ciw->cda.cdda_paused;
	if ((paused && ciw->cda.cdda_play) || !paused)
		ciw->cda.cdda_paused = paused;
	return old;
}


/* stop CD audio */
static int ioctl_command_stop (int unitnum)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	ciw_cdda_stop(&ciw->cda);
		
	return 1;
}

static uae_u32 ioctl_command_volume (int unitnum, uae_u16 volume_left, uae_u16 volume_right)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return -1;
	uae_u32 old = (ciw->cda.cdda_volume[1] << 16) | (ciw->cda.cdda_volume[0] << 0);
	ciw->cda.cdda_volume[0] = volume_left;
	ciw->cda.cdda_volume[1] = volume_right;
	return old;
}

/* play CD audio */
static int ioctl_command_play (int unitnum, int startlsn, int endlsn, int scan, play_status_callback statusfunc, play_subchannel_callback subfunc)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	ciw->cda.di = &ciw->di;
	ciw->cda.cdda_play_finished = 0;
	ciw->cda.cdda_subfunc = subfunc;
	ciw->cda.cdda_statusfunc = statusfunc;
	ciw->cda.cdda_scan = scan > 0 ? 10 : (scan < 0 ? 10 : 0);
	ciw->cda.cdda_delay = ciw_cdda_setstate(&ciw->cda, -1, -1);
	ciw->cda.cdda_delay_frames = ciw_cdda_setstate(&ciw->cda, -2, -1);
	ciw_cdda_setstate(&ciw->cda, AUDIO_STATUS_NOT_SUPPORTED, -1);
	ciw->cda.read_block = read_block_cda;

	if (!open_createfile (ciw, 0)) {
		ciw_cdda_setstate(&ciw->cda, AUDIO_STATUS_PLAY_ERROR, -1);
		return 0;
	}
	if (!isaudiotrack (&ciw->di.toc, startlsn)) {
		ciw_cdda_setstate(&ciw->cda, AUDIO_STATUS_PLAY_ERROR, -1);
		return 0;
	}
	if (!ciw->cda.cdda_play) {
		uae_start_thread (_T("ioctl_cdda_play"), ciw_cdda_play, &ciw->cda, NULL);
	}
	ciw->cda.cdda_start = startlsn;
	ciw->cda.cdda_end = endlsn;
	ciw->cda.cd_last_pos = ciw->cda.cdda_start;
	ciw->cda.cdda_play++;

	return 1;
}

/* read qcode */
static int ioctl_command_qcode (int unitnum, uae_u8 *buf, int sector, bool all)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	uae_u8 *p;
	int trk;
	CDROM_TOC *toc = &ciw->cdromtoc;
	int pos;
	int msf;
	int start, end;
	int status;
	bool valid = false;
	bool regenerate = true;

	if (all)
		return 0;

	memset (buf, 0, SUBQ_SIZE);
	p = buf;

	status = AUDIO_STATUS_NO_STATUS;
	if (ciw->cda.cdda_play) {
		status = AUDIO_STATUS_IN_PROGRESS;
		if (ciw->cda.cdda_paused)
			status = AUDIO_STATUS_PAUSED;
	} else if (ciw->cda.cdda_play_finished) {
		status = AUDIO_STATUS_PLAY_COMPLETE;
	}

	p[1] = status;
	p[3] = 12;

	p = buf + 4;

	if (sector < 0)
		pos = ciw->cda.cd_last_pos;
	else
		pos = sector;

	if (!regenerate) {
		if (sector < 0 && ciw->cda.subcodevalid && ciw->cda.cdda_play) {
			uae_sem_wait (&ciw->cda.sub_sem2);
			uae_u8 subbuf[SUB_CHANNEL_SIZE];
			sub_deinterleave (ciw->cda.subcodebuf, subbuf);
			memcpy (p, subbuf + 12, 12);
			uae_sem_post (&ciw->cda.sub_sem2);
			valid = true;
		}
		if (!valid && sector >= 0) {
			DWORD len;
			uae_sem_wait (&ciw->cda.sub_sem);
			seterrormode (ciw);
			RAW_READ_INFO rri;
			rri.DiskOffset.QuadPart = 2048 * (pos + 0);
			rri.SectorCount = 1;
			rri.TrackMode = RawWithSubCode;
			memset (ciw->tempbuffer, 0, CD_RAW_SECTOR_WITH_SUBCODE_SIZE);
			if (!DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri, ciw->tempbuffer, CD_RAW_SECTOR_WITH_SUBCODE_SIZE, &len, NULL)) {
				DWORD err = GetLastError ();
				write_log (_T("IOCTL_CDROM_RAW_READ SUBQ CDDA sector %d returned %d\n"), pos, err);
			}
			reseterrormode (ciw);
			uae_u8 subbuf[SUB_CHANNEL_SIZE];
			sub_deinterleave (ciw->tempbuffer + 2352, subbuf);
			uae_sem_post (&ciw->cda.sub_sem);
			memcpy (p, subbuf + 12, 12);
			valid = true;
		}
	}

	if (!valid) {
		start = end = 0;
		for (trk = 0; trk <= toc->LastTrack; trk++) {
			TRACK_DATA *td = &toc->TrackData[trk];
			start = msf2lsn ((td->Address[1] << 16) | (td->Address[2] << 8) | td->Address[3]);
			end = msf2lsn ((td[1].Address[1] << 16) | (td[1].Address[2] << 8) | td[1].Address[3]);
			if (pos < start)
				break;
			if (pos >= start && pos < end)
				break;
		}
		p[0] = (toc->TrackData[trk].Control << 4) | (toc->TrackData[trk].Adr << 0);
		p[1] = tobcd (trk + 1);
		p[2] = tobcd (1);
		msf = lsn2msf (pos);
		tolongbcd (p + 7, msf);
		msf = lsn2msf (pos - start - 150);
		tolongbcd (p + 3, msf);
	}

#if 0
	uae_u8 *s = buf + 4;
	write_log(_T("CTRLADR:%02X TRK=%02X IDX=%02X MSF=%02X:%02X:%02X %02X:%02X:%02X\n"),
		s[0], s[1], s[2],
		s[3], s[4], s[5],
		s[7], s[8], s[9]);
#endif

	return 1;

}

static int ioctl_command_rawread (int unitnum, uae_u8 *data, int sector, int size, int sectorsize, uae_u32 extra)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	uae_u8 *p = ciw->tempbuffer;
	int ret = 0;

	if (log_scsi)
		write_log (_T("IOCTL rawread unit=%d sector=%d blocksize=%d\n"), unitnum, sector, sectorsize);
	ciw_cdda_stop(&ciw->cda);
	gui_flicker_led (LED_CD, unitnum, LED_CD_ACTIVE);
	if (sectorsize > 0) { 
		if (sectorsize != 2336 && sectorsize != 2352 && sectorsize != 2048 &&
			sectorsize != 2336 + 96 && sectorsize != 2352 + 96 && sectorsize != 2048 + 96)
			return 0;
		while (size-- > 0) {
			if (!read_block (ciw, unitnum, data, sector, 1, sectorsize))
				break;
			ciw->cda.cd_last_pos = sector;
			data += sectorsize;
			ret += sectorsize;
			sector++;
		}
	} else {
		uae_u8 sectortype = extra >> 16;
		uae_u8 cmd9 = extra >> 8;
		int sync = (cmd9 >> 7) & 1;
		int headercodes = (cmd9 >> 5) & 3;
		int userdata = (cmd9 >> 4) & 1;
		int edcecc = (cmd9 >> 3) & 1;
		int errorfield = (cmd9 >> 1) & 3;
		uae_u8 subs = extra & 7;
		if (subs != 0 && subs != 1 && subs != 2 && subs != 4)
			return -1;
		if (errorfield >= 3)
			return -1;
		uae_u8 *d = data;

		if (isaudiotrack (&ciw->di.toc, sector)) {

			if (sectortype != 0 && sectortype != 1)
				return -2;

			for (int i = 0; i < size; i++) {
				uae_u8 *odata = data;
				int blocksize = errorfield == 0 ? 2352 : (errorfield == 1 ? 2352 + 294 : 2352 + 296);
				int readblocksize = errorfield == 0 ? 2352 : 2352 + 296;

				if (!read_block (ciw, unitnum, NULL, sector, 1, readblocksize)) {
					reseterrormode (ciw);
					return ret;
				}
				ciw->cda.cd_last_pos = sector;

				if (subs == 0) {
					memcpy (data, p, blocksize);
					data += blocksize;
				} else if (subs == 4) { // all, de-interleaved
					memcpy (data, p, blocksize);
					data += blocksize;
					sub_to_deinterleaved (p + readblocksize, data);
					data += SUB_CHANNEL_SIZE;
				} else if (subs == 2) { // q-only
					memcpy (data, p, blocksize);
					data += blocksize;
					uae_u8 subdata[SUB_CHANNEL_SIZE];
					sub_to_deinterleaved (p + readblocksize, subdata);
					memcpy (data, subdata + SUB_ENTRY_SIZE, SUB_ENTRY_SIZE);
					p += SUB_ENTRY_SIZE;
				} else if (subs == 1) { // all, interleaved
					memcpy (data, p, blocksize);
					memcpy (data + blocksize, p + readblocksize, SUB_CHANNEL_SIZE);
					data += blocksize + SUB_CHANNEL_SIZE;
				}
				ret += (int)(data - odata);
				sector++;
			}
		}


	}
	return ret;
}

static int ioctl_command_readwrite (int unitnum, int sector, int size, int write, uae_u8 *data)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	if (ciw->usesptiread)
		return ioctl_command_rawread (unitnum, data, sector, size, 2048, 0);

	ciw_cdda_stop(&ciw->cda);

	DWORD dtotal;
	int cnt = 3;
	uae_u8 *p = ciw->tempbuffer;
	int blocksize = ciw->di.bytespersector;

	if (!open_createfile (ciw, 0))
		return 0;
	ciw->cda.cd_last_pos = sector;
	while (cnt-- > 0) {
		LARGE_INTEGER offset;
		gui_flicker_led (LED_CD, unitnum, LED_CD_ACTIVE);
		seterrormode (ciw);
		offset.QuadPart = (uae_u64)sector * ciw->di.bytespersector;
		if (SetFilePointer (ciw->h, offset.LowPart, &offset.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR) {
			reseterrormode (ciw);
			if (win32_error (ciw, unitnum, _T("SetFilePointer")) < 0)
				continue;
			return 0;
		}
		reseterrormode (ciw);
		break;
	}
	while (size-- > 0) {
		gui_flicker_led (LED_CD, unitnum, LED_CD_ACTIVE);
		seterrormode (ciw);
		if (write) {
			if (data) {
				memcpy (p, data, blocksize);
				data += blocksize;
			}
			if (!WriteFile (ciw->h, p, blocksize, &dtotal, 0)) {
				int err;
				reseterrormode (ciw);
				err = win32_error (ciw, unitnum, _T("WriteFile"));
				if (err < 0)
					continue;
				if (err == ERROR_WRITE_PROTECT)
					return -1;
				return 0;
			}
		} else {
			dtotal = 0;
			if (!ReadFile (ciw->h, p, blocksize, &dtotal, 0)) {
				reseterrormode (ciw);
				if (win32_error (ciw, unitnum, _T("ReadFile")) < 0)
					continue;
				return 0;
			}
			if (dtotal == 0) {
				static int reported;
				/* ESS Mega (CDTV) "fake" data area returns zero bytes and no error.. */
				spti_read (ciw, unitnum, data, sector, 2048);
				if (reported++ < 100)
					write_log (_T("IOCTL unit %d, sector %d: ReadFile()==0. SPTI=%d\n"), unitnum, sector, GetLastError ());
				return 1;
			}
			if (data) {
				memcpy (data, p, blocksize);
				data += blocksize;
			}
		}
		reseterrormode (ciw);
		gui_flicker_led (LED_CD, unitnum, LED_CD_ACTIVE);
	}
	return 1;
}

static int ioctl_command_write (int unitnum, uae_u8 *data, int sector, int size)
{
	return ioctl_command_readwrite (unitnum, sector, size, 1, data);
}

static int ioctl_command_read (int unitnum, uae_u8 *data, int sector, int size)
{
	return ioctl_command_readwrite (unitnum, sector, size, 0, data);
}

static int fetch_geometry (struct dev_info_ioctl *ciw, int unitnum, struct device_info *di)
{
	DISK_GEOMETRY geom;
	DWORD len;
	int cnt = 3;

	if (!open_createfile (ciw, 0))
		return 0;
	uae_sem_wait (&ciw->cda.sub_sem);
	seterrormode (ciw);
	while (cnt-- > 0) {
		if (!DeviceIoControl (ciw->h, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof (geom), &len, NULL)) {
			DWORD err = GetLastError ();
			ciw->changed = true;
			if (err == ERROR_WRONG_DISK) {
				if (win32_error (ciw, unitnum, _T("IOCTL_CDROM_GET_DRIVE_GEOMETRY")) < 0)
					continue;
			}
			reseterrormode (ciw);
			uae_sem_post (&ciw->cda.sub_sem);
			return 0;
		}
		break;
	}
	reseterrormode (ciw);
	uae_sem_post (&ciw->cda.sub_sem);
	if (di) {
		di->cylinders = geom.Cylinders.LowPart;
		di->sectorspertrack = geom.SectorsPerTrack;
		di->trackspercylinder = geom.TracksPerCylinder;
		di->bytespersector = geom.BytesPerSector;
	}
	return 1;
}

static int ismedia (struct dev_info_ioctl *ciw, int unitnum)
{
	return fetch_geometry (ciw, unitnum, &ciw->di);
}

static int eject (int unitnum, bool eject)
{
	DWORD len;
	struct dev_info_ioctl *ciw = unitisopen (unitnum);

	if (!ciw)
		return 0;
	if (!unitisopen (unitnum))
		return 0;
	ciw_cdda_stop(&ciw->cda);
	if (!open_createfile (ciw, 0))
		return 0;
	int ret = 0;
	seterrormode (ciw);
	if (!DeviceIoControl (ciw->h, eject ? IOCTL_STORAGE_EJECT_MEDIA : IOCTL_STORAGE_LOAD_MEDIA, NULL, 0, NULL, 0, &len, NULL)) {
		ret = 1;
	}
	reseterrormode (ciw);
	return ret;
}

/* read toc */
static int ioctl_command_toc2 (int unitnum, struct cd_toc_head *tocout, bool hide_errors)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	DWORD len;
	int i;
	struct cd_toc_head *th = &ciw->di.toc;
	struct cd_toc *t = th->toc;
	int cnt = 3;
	memset(ciw->trackmode, 0xff, sizeof(ciw->trackmode));
	CDROM_TOC *toc = &ciw->cdromtoc;

	if (!unitisopen (unitnum))
		return 0;

	if (!open_createfile (ciw, 0))
		return 0;
	while (cnt-- > 0) {
		seterrormode (ciw);
		if (!DeviceIoControl (ciw->h, IOCTL_CDROM_READ_TOC, NULL, 0, toc, sizeof (CDROM_TOC), &len, NULL)) {
			DWORD err = GetLastError ();
			reseterrormode (ciw);
			if (!hide_errors || (hide_errors && err == ERROR_WRONG_DISK)) {
				if (win32_error (ciw, unitnum, _T("IOCTL_CDROM_READ_TOC")) < 0)
					continue;
			}
			return 0;
		}
		reseterrormode (ciw);
		break;
	}

	memset (th, 0, sizeof (struct cd_toc_head));
	th->first_track = toc->FirstTrack;
	th->last_track = toc->LastTrack;
	th->tracks = th->last_track - th->first_track + 1;
	th->points = th->tracks + 3;
	th->firstaddress = 0;
	th->lastaddress = msf2lsn ((toc->TrackData[toc->LastTrack].Address[1] << 16) | (toc->TrackData[toc->LastTrack].Address[2] << 8) |
		(toc->TrackData[toc->LastTrack].Address[3] << 0));

	t->adr = 1;
	t->point = 0xa0;
	t->track = th->first_track;
	t++;

	th->first_track_offset = 1;
	for (i = 0; i < toc->LastTrack; i++) {
		t->adr = toc->TrackData[i].Adr;
		t->control = toc->TrackData[i].Control;
		t->paddress = msf2lsn ((toc->TrackData[i].Address[1] << 16) | (toc->TrackData[i].Address[2] << 8) |
			(toc->TrackData[i].Address[3] << 0));
		t->point = t->track = i + 1;
		t++;
	}

	th->last_track_offset = toc->LastTrack;
	t->adr = 1;
	t->point = 0xa1;
	t->track = th->last_track;
	t->paddress = th->lastaddress;
	t++;

	t->adr = 1;
	t->point = 0xa2;
	t->paddress = th->lastaddress;
	t++;

	for (i = th->first_track_offset; i <= th->last_track_offset + 1; i++) {
		uae_u32 addr;
		uae_u32 msf;
		t = &th->toc[i];
		if (i <= th->last_track_offset) {
			write_log (_T("%2d: "), t->track);
			addr = t->paddress;
			msf = lsn2msf (addr);
		} else {
			write_log (_T("    "));
			addr = th->toc[th->last_track_offset + 2].paddress;
			msf = lsn2msf (addr);
		}
		write_log (_T("%7d %02d:%02d:%02d"),
			addr, (msf >> 16) & 0x7fff, (msf >> 8) & 0xff, (msf >> 0) & 0xff);
		if (i <= th->last_track_offset) {
			write_log (_T(" %s %x"),
				(t->control & 4) ? _T("DATA    ") : _T("CDA     "), t->control);
		}
		write_log (_T("\n"));
	}

	memcpy (tocout, th, sizeof (struct cd_toc_head));
	return 1;
}
static int ioctl_command_toc (int unitnum, struct cd_toc_head *tocout)
{
	return ioctl_command_toc2 (unitnum, tocout, false);
}

static void update_device_info (int unitnum)
{
	struct dev_info_ioctl *ciw = unitcheck (unitnum);
	if (!ciw)
		return;
	struct device_info *di = &ciw->di;
	di->bus = unitnum;
	di->target = 0;
	di->lun = 0;
	di->media_inserted = 0;
	di->bytespersector = 2048;
	_stprintf (di->mediapath, _T("\\\\.\\%c:"), ciw->drvletter);
	if (fetch_geometry (ciw, unitnum, di)) { // || ioctl_command_toc (unitnum))
		di->media_inserted = 1;
	}
	if (ciw->changed) {
		ioctl_command_toc2 (unitnum, &di->toc, true);
		ciw->changed = false;
	}
	di->removable = ciw->type == DRIVE_CDROM ? 1 : 0;
	di->write_protected = ciw->type == DRIVE_CDROM ? 1 : 0;
	di->type = ciw->type == DRIVE_CDROM ? INQ_ROMD : INQ_DASD;
	di->unitnum = unitnum + 1;
	_tcscpy (di->label, ciw->drvlettername);
	di->backend = _T("IOCTL");
}

static void trim (TCHAR *s)
{
	while (s[0] != '\0' && s[_tcslen (s) - 1] == ' ')
		s[_tcslen (s) - 1] = 0;
}

/* open device level access to cd rom drive */
static int sys_cddev_open (struct dev_info_ioctl *ciw, int unitnum)
{

	ciw->cda.cdda_volume[0] = 0x7fff;
	ciw->cda.cdda_volume[1] = 0x7fff;
	/* buffer must be page aligned for device access */
	ciw->tempbuffer = (uae_u8*)VirtualAlloc (NULL, IOCTL_DATA_BUFFER, MEM_COMMIT, PAGE_READWRITE);
	if (!ciw->tempbuffer) {
		write_log (_T("IOCTL: failed to allocate buffer"));
		return 1;
	}

	memset (ciw->di.vendorid, 0, sizeof ciw->di.vendorid);
	memset (ciw->di.productid, 0, sizeof ciw->di.productid);
	memset (ciw->di.revision, 0, sizeof ciw->di.revision);
	_tcscpy (ciw->di.vendorid, _T("UAE"));
	_stprintf (ciw->di.productid, _T("SCSI CD%d IMG"), unitnum);
	_tcscpy (ciw->di.revision, _T("0.2"));

#if 0
	uae_u8 inquiry[256];
	int datalen;
	memset (inquiry, 0, sizeof inquiry);
	if (spti_inquiry (ciw, unitnum, inquiry, &datalen)) {
		// check also that device type is non-zero and it is removable
		if (datalen >= 36 && (inquiry[0] & 31) && (inquiry[1] & 0x80) && inquiry[8]) {
			char tmp[20];
			TCHAR *s;
			memcpy (tmp, inquiry + 8, 8);
			tmp[8] = 0;
			s = au (tmp);
			trim (s);
			_tcscpy (ciw->di.vendorid, s);
			xfree (s);
			memcpy (tmp, inquiry + 16, 16);
			tmp[16] = 0;
			s = au (tmp);
			trim (s);
			_tcscpy (ciw->di.productid, s);
			xfree (s);
			memcpy (tmp, inquiry + 32, 4);
			tmp[4] = 0;
			s = au (tmp);
			trim (s);
			_tcscpy (ciw->di.revision, s);
			xfree (s);
		}
		close_createfile (ciw);
	}
#endif

	if (!open_createfile (ciw, 0)) {
		write_log (_T("IOCTL: failed to open '%s', err=%d\n"), ciw->devname, GetLastError ());
		goto error;
	}

	STORAGE_PROPERTY_QUERY query;
	UCHAR outBuf[20000];
	ULONG returnedLength;
	memset (&query, 0, sizeof query);
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	if (DeviceIoControl(
		ciw->h,
		IOCTL_STORAGE_QUERY_PROPERTY,
		&query,
		sizeof (STORAGE_PROPERTY_QUERY),
		&outBuf,
		sizeof (outBuf),
		&returnedLength,
		NULL
	)) {
		PSTORAGE_DEVICE_DESCRIPTOR devDesc;
		devDesc = (PSTORAGE_DEVICE_DESCRIPTOR) outBuf;
		int size = devDesc->Version;
		PUCHAR p = (PUCHAR) outBuf;
		for (;;) {
			if (offsetof(STORAGE_DEVICE_DESCRIPTOR, CommandQueueing) > size)
				break;
			if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, VendorIdOffset) && devDesc->VendorIdOffset && p[devDesc->VendorIdOffset]) {
				ciw->di.vendorid[0] = 0;
				ciw->di.productid[0] = 0;
				ciw->di.revision[0] = 0;
				int j = 0;
				for (int i = devDesc->VendorIdOffset; p[i] != (UCHAR) NULL && i < returnedLength && j < sizeof (ciw->di.vendorid) / sizeof (TCHAR) - 1; i++)
					ciw->di.vendorid[j++] = p[i];
			}
			if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductIdOffset) && devDesc->ProductIdOffset && p[devDesc->ProductIdOffset]) {
				int j = 0;
				for (int i = devDesc->ProductIdOffset; p[i] != (UCHAR) NULL && i < returnedLength && j < sizeof (ciw->di.productid) / sizeof (TCHAR) - 1; i++)
					ciw->di.productid[j++] = p[i];
			}
			if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductRevisionOffset) && devDesc->ProductRevisionOffset && p[devDesc->ProductRevisionOffset]) {
				int j = 0;
				for (int i = devDesc->ProductRevisionOffset; p[i] != (UCHAR) NULL && i < returnedLength && j < sizeof (ciw->di.revision) / sizeof (TCHAR) - 1; i++)
					 ciw->di.revision[j++] = p[i];
			}
			trim (ciw->di.vendorid);
			trim (ciw->di.productid);
			trim (ciw->di.revision);
			break;
		}
	}

	write_log (_T("IOCTL: device '%s' (%s/%s/%s) opened successfully (unit=%d,media=%d)\n"),
		ciw->devname, ciw->di.vendorid, ciw->di.productid, ciw->di.revision,
		unitnum, ciw->di.media_inserted);
	if (!_tcsicmp (ciw->di.vendorid, _T("iomega")) && !_tcsicmp (ciw->di.productid, _T("rrd"))) {
		write_log (_T("Device blacklisted\n"));
		goto error2;
	}
	uae_sem_init (&ciw->cda.sub_sem, 0, 1);
	uae_sem_init (&ciw->cda.sub_sem2, 0, 1);
	//ciw->usesptiread = true;
	ioctl_command_stop (unitnum);
	update_device_info (unitnum);
	ciw->open = true;
	return 0;
error:
	win32_error (ciw, unitnum, _T("CreateFile"));
error2:
	VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
	ciw->tempbuffer = NULL;
	CloseHandle (ciw->h);
	ciw->h = NULL;
	return -1;
}

/* close device handle */
static void sys_cddev_close (struct dev_info_ioctl *ciw, int unitnum)
{
	if (ciw->open == false)
		return;
	ciw_cdda_stop(&ciw->cda);
	close_createfile (ciw);
	VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
	ciw->tempbuffer = NULL;
	uae_sem_destroy (&ciw->cda.sub_sem);
	uae_sem_destroy (&ciw->cda.sub_sem2);
	ciw->open = false;
	write_log (_T("IOCTL: device '%s' closed\n"), ciw->devname, unitnum);
}

static int open_device (int unitnum, const TCHAR *ident, int flags)
{
	struct dev_info_ioctl *ciw = NULL;
	if (ident && ident[0]) {
		for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
			ciw = &ciw32[i];
			if (unittable[i] == 0 && ciw->drvletter != 0) {
				if (!_tcsicmp (ciw->drvlettername, ident)) {
					unittable[unitnum] = i + 1;
					if (sys_cddev_open (ciw, unitnum) == 0)
						return 1;
					unittable[unitnum] = 0;
					return 0;
				}
			}
		}
		return 0;
	}
	ciw = &ciw32[unitnum];
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (unittable[i] == unitnum + 1)
			return 0;
	}
	if (ciw->drvletter == 0)
		return 0;
	unittable[unitnum] = unitnum + 1;
	if (sys_cddev_open (ciw, unitnum) == 0)
		return 1;
	unittable[unitnum] = 0;
	blkdev_cd_change (unitnum, ciw->drvlettername);
	return 0;
}

static void close_device (int unitnum)
{
	struct dev_info_ioctl *ciw = unitcheck (unitnum);
	if (!ciw)
		return;
	sys_cddev_close (ciw, unitnum);
	blkdev_cd_change (unitnum, ciw->drvlettername);
	unittable[unitnum] = 0;
}

static int total_devices;

static void close_bus (void)
{
	if (!bus_open) {
		write_log (_T("IOCTL close_bus() when already closed!\n"));
		return;
	}
	total_devices = 0;
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		sys_cddev_close (&ciw32[i], i);
		memset (&ciw32[i], 0, sizeof (struct dev_info_ioctl));
		ciw32[i].h = INVALID_HANDLE_VALUE;
		unittable[i] = 0;
	}
	bus_open = 0;
	uae_sem_destroy(&play_sem);
	write_log (_T("IOCTL driver closed.\n"));
}

static int open_bus (int flags)
{
	int dwDriveMask;
	int drive;

	if (bus_open) {
		write_log (_T("IOCTL open_bus() more than once!\n"));
		return 1;
	}
	total_devices = 0;
	dwDriveMask = GetLogicalDrives ();
	if (log_scsi)
		write_log (_T("IOCTL: drive mask = %08X\n"), dwDriveMask);
	dwDriveMask >>= 2; // Skip A and B drives...
	for (drive = 'C'; drive <= 'Z' && total_devices < MAX_TOTAL_SCSI_DEVICES; drive++) {
		if (dwDriveMask & 1) {
			int dt;
			TCHAR tmp[10];
			_stprintf (tmp, _T("%c:\\"), drive);
			dt = GetDriveType (tmp);
			if (log_scsi)
				write_log (_T("IOCTL: drive %c type %d\n"), drive, dt);
			if (dt == DRIVE_CDROM) {
				if (log_scsi)
					write_log (_T("IOCTL: drive %c: = unit %d\n"), drive, total_devices);
				ciw32[total_devices].drvletter = drive;
				_tcscpy (ciw32[total_devices].drvlettername, tmp);
				ciw32[total_devices].type = dt;
				ciw32[total_devices].di.bytespersector = 2048;
				_stprintf (ciw32[total_devices].devname, _T("\\\\.\\%c:"), drive);
				ciw32[total_devices].h = INVALID_HANDLE_VALUE;
				total_devices++;
			}
		}
		dwDriveMask >>= 1;
	}
	bus_open = 1;
	uae_sem_init(&play_sem, 0, 1);
	write_log (_T("IOCTL driver open, %d devices.\n"), total_devices);
	return total_devices;
}

static int ioctl_ismedia (int unitnum, int quick)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return -1;
	if (quick) {
		return ciw->di.media_inserted;
	}
	update_device_info (unitnum);
	return ismedia (ciw, unitnum);
}

static struct device_info *info_device (int unitnum, struct device_info *di, int quick, int session)
{
	struct dev_info_ioctl *ciw = unitcheck (unitnum);
	if (!ciw)
		return 0;
	if (!quick)
		update_device_info (unitnum);
	ciw->di.open = ciw->open;
	memcpy (di, &ciw->di, sizeof (struct device_info));
	return di;
}

bool win32_ioctl_media_change (TCHAR driveletter, int insert)
{
	for (int i = 0; i < total_devices; i++) {
		struct dev_info_ioctl *ciw = &ciw32[i];
		if (ciw->drvletter == driveletter && ciw->di.media_inserted != insert) {
			write_log (_T("IOCTL: media change %s %d\n"), ciw->drvlettername, insert);
			ciw->di.media_inserted = insert;
			ciw->changed = true;
			int unitnum = getunitnum (ciw);
			if (unitnum >= 0) {
				update_device_info (unitnum);
				scsi_do_disk_change (unitnum, insert, NULL);
				filesys_do_disk_change (unitnum, insert != 0);
				blkdev_cd_change (unitnum, ciw->drvlettername);
				return true;
			}
		}
	}
	return false;
}

static int ioctl_scsiemu (int unitnum, uae_u8 *cmd)
{
	uae_u8 c = cmd[0];
	if (c == 0x1b) {
		int mode = cmd[4] & 3;
		if (mode == 2)
			eject (unitnum, true);
		else if (mode == 3)
			eject (unitnum, false);
		return 1;
	}
	return -1;
}

struct device_functions devicefunc_scsi_ioctl = {
	_T("IOCTL"),
	open_bus, close_bus, open_device, close_device, info_device,
	0, 0, 0,
	ioctl_command_pause, ioctl_command_stop, ioctl_command_play, ioctl_command_volume, ioctl_command_qcode,
	ioctl_command_toc, ioctl_command_read, ioctl_command_rawread, ioctl_command_write,
	0, ioctl_ismedia, ioctl_scsiemu
};

#endif
