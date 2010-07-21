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
#include <winioctl.h>
#include <setupapi.h>   // for SetupDiXxx functions.
#include <stddef.h>
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#include <ntddscsi.h>

#define IOCTL_DATA_BUFFER 8192
#define CDDA_BUFFERS 6

struct dev_info_ioctl {
	HANDLE h;
	uae_u8 *tempbuffer;
	TCHAR drvletter;
	TCHAR drvlettername[10];
	TCHAR devname[30];
	int type;
	CDROM_TOC cdromtoc;
	UINT errormode;
	int playend;
	int fullaccess;
	int cdda_play_finished;
	int cdda_play;
	int cdda_paused;
	int cdda_volume[2];
	int cdda_scan;
	int cdda_volume_main;
	uae_u32 cd_last_pos;
	HWAVEOUT cdda_wavehandle;
	int cdda_start, cdda_end;
	uae_u8 subcode[SUB_CHANNEL_SIZE * CDDA_BUFFERS];
	uae_u8 subcodebuf[SUB_CHANNEL_SIZE];
	bool subcodevalid;
	play_subchannel_callback cdda_subfunc;
	struct device_info di;
	uae_sem_t sub_sem, sub_sem2;
	bool open;
};

static struct dev_info_ioctl ciw32[MAX_TOTAL_SCSI_DEVICES];
static int unittable[MAX_TOTAL_SCSI_DEVICES];
static int bus_open;

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
	int idx = ciw - &ciw32[0];
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
	if (mciGetErrorString (err, es, sizeof es))
		write_log (L"MCIErr: %s: %d = '%s'\n", str, err, es);
	return err;
}

static int win32_error (struct dev_info_ioctl *ciw, int unitnum, const TCHAR *format,...)
{
	LPVOID lpMsgBuf;
	va_list arglist;
	TCHAR buf[1000];
	DWORD err = GetLastError ();

	if (err == ERROR_WRONG_DISK) {
		write_log (L"IOCTL: media change, re-opening device\n");
		sys_cddev_close (ciw, unitnum);
		if (!sys_cddev_open (ciw, unitnum))
			write_log (L"IOCTL: re-opening failed!\n");
		return -1;
	}
	va_start (arglist, format);
	_vsntprintf (buf, sizeof buf / sizeof (TCHAR), format, arglist);
	FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	if (log_scsi)
		write_log (L"IOCTL: unit=%d,%s,%d: %s\n", unitnum, buf, err, (TCHAR*)lpMsgBuf);
	va_end (arglist);
	return err;
}

static int close_createfile (struct dev_info_ioctl *ciw)
{
	ciw->fullaccess = 0;
	if (ciw->h != INVALID_HANDLE_VALUE) {
		if (log_scsi)
			write_log (L"IOCTL: IOCTL close\n");
		CloseHandle (ciw->h);
		if (log_scsi)
			write_log (L"IOCTL: IOCTL close completed\n");
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
		write_log (L"IOCTL: opening IOCTL %s\n", ciw->devname);
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
		write_log (L"IOCTL: failed to open '%s', err=%d\n", ciw->devname, GetLastError ());
		return 0;
	}
	if (!DeviceIoControl (ciw->h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &len, NULL))
		write_log (L"IOCTL: FSCTL_ALLOW_EXTENDED_DASD_IO returned %d\n", GetLastError ());
	if (log_scsi)
		write_log (L"IOCTL: IOCTL open completed\n");
	return 1;
}

static void cdda_closewav (struct dev_info_ioctl *ciw)
{
	if (ciw->cdda_wavehandle != NULL)
		waveOutClose (ciw->cdda_wavehandle);
	ciw->cdda_wavehandle = NULL;
}

// DAE CDDA based on Larry Osterman's "Playing Audio CDs" blog series

static int cdda_openwav (struct dev_info_ioctl *ciw)
{
	WAVEFORMATEX wav = { 0 };
	MMRESULT mmr;

	wav.cbSize = 0;
	wav.nChannels = 2;
	wav.nSamplesPerSec = 44100;
	wav.wBitsPerSample = 16;
	wav.nBlockAlign = wav.wBitsPerSample / 8 * wav.nChannels;
	wav.nAvgBytesPerSec = wav.nBlockAlign * wav.nSamplesPerSec;
	wav.wFormatTag = WAVE_FORMAT_PCM;
	mmr = waveOutOpen (&ciw->cdda_wavehandle, WAVE_MAPPER, &wav, 0, 0, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT);
	if (mmr != MMSYSERR_NOERROR) {
		write_log (L"IOCTL CDDA: wave open %d\n", mmr);
		cdda_closewav (ciw);
		return 0;
	}
	return 1;
}

static void *cdda_play (void *v)
{
	DWORD len;
	struct dev_info_ioctl *ciw = (struct dev_info_ioctl*)v;
	int cdda_pos;
	int num_sectors = CDDA_BUFFERS;
	int bufnum;
	int buffered;
	uae_u8 *px[2], *p;
	int bufon[2];
	int i;
	WAVEHDR whdr[2];
	MMRESULT mmr;
	int volume[2], volume_main;
	int oldplay;
	int firstloops;

	for (i = 0; i < 2; i++) {
		memset (&whdr[i], 0, sizeof (WAVEHDR));
		whdr[i].dwFlags = WHDR_DONE;
	}

	while (ciw->cdda_play == 0)
		Sleep (10);
	oldplay = -1;

	p = (uae_u8*)VirtualAlloc (NULL, 2 * num_sectors * 4096, MEM_COMMIT, PAGE_READWRITE);
	px[0] = p;
	px[1] = p + num_sectors * 4096;
	bufon[0] = bufon[1] = 0;
	bufnum = 0;
	buffered = 0;
	volume[0] = volume[1] = -1;
	volume_main = -1;

	if (cdda_openwav (ciw)) {

		for (i = 0; i < 2; i++) {
			memset (&whdr[i], 0, sizeof (WAVEHDR));
			whdr[i].dwBufferLength = 2352 * num_sectors;
			whdr[i].lpData = (LPSTR)px[i];
			mmr = waveOutPrepareHeader (ciw->cdda_wavehandle, &whdr[i], sizeof (WAVEHDR));
			if (mmr != MMSYSERR_NOERROR) {
				write_log (L"IOCTL CDDA: waveOutPrepareHeader %d:%d\n", i, mmr);
				goto end;
			}
			whdr[i].dwFlags |= WHDR_DONE;
		}

		while (ciw->cdda_play > 0) {

			while (!(whdr[bufnum].dwFlags & WHDR_DONE)) {
				Sleep (10);
				if (ciw->cdda_play <= 0)
					goto end;
			}
			bufon[bufnum] = 0;

			if (oldplay != ciw->cdda_play) {
				cdda_pos = ciw->cdda_start;
				oldplay = ciw->cdda_play;
				firstloops = 25;
				write_log (L"IOCTL CDDA: playing from %d to %d\n", ciw->cdda_start, ciw->cdda_end);
				ciw->subcodevalid = false;
				while (ciw->cdda_paused && ciw->cdda_play > 0)
					Sleep (10);
			}

			if ((cdda_pos < ciw->cdda_end || ciw->cdda_end == 0xffffffff) && !ciw->cdda_paused && ciw->cdda_play) {
				RAW_READ_INFO rri;
				int sectors = num_sectors;

				if (!isaudiotrack (&ciw->di.toc, cdda_pos))
					goto end; // data track?

				uae_sem_wait (&ciw->sub_sem);
				ciw->subcodevalid = false;
				memset (ciw->subcode, 0, sizeof ciw->subcode);

				if (firstloops > 0) {

					firstloops--;
					if (ciw->cdda_subfunc)
						ciw->cdda_subfunc (ciw->subcode, sectors);
					memset (px[bufnum], 0, sectors * 2352);

				} else {

					firstloops = -1;
					seterrormode (ciw);
					rri.DiskOffset.QuadPart = 2048 * (cdda_pos + 0);
					rri.SectorCount = sectors;
					rri.TrackMode = RawWithSubCode;
					if (!DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri, px[bufnum], sectors * CD_RAW_SECTOR_WITH_SUBCODE_SIZE, &len, NULL)) {
						DWORD err = GetLastError ();
						write_log (L"IOCTL_CDROM_RAW_READ CDDA sector %d returned %d\n", cdda_pos, err);
						if (ciw->cdda_subfunc)
							ciw->cdda_subfunc (ciw->subcode, sectors);
					} else {
						for (i = 0; i < sectors; i++) {
							memcpy (ciw->subcode + i * SUB_CHANNEL_SIZE, px[bufnum] + CD_RAW_SECTOR_WITH_SUBCODE_SIZE * i + 2352, SUB_CHANNEL_SIZE);
						}
						if (ciw->cdda_subfunc)
							ciw->cdda_subfunc (ciw->subcode, sectors); 
						for (i = 1; i < sectors; i++) {
							memmove (px[bufnum] + 2352 * i, px[bufnum] + CD_RAW_SECTOR_WITH_SUBCODE_SIZE * i, 2352);
						}
						ciw->subcodevalid = true;
					}
					reseterrormode (ciw);
				}

				uae_sem_post (&ciw->sub_sem);
				if (ciw->subcodevalid) {
					uae_sem_wait (&ciw->sub_sem2);
					memcpy (ciw->subcodebuf, ciw->subcode + (sectors - 1) * SUB_CHANNEL_SIZE, SUB_CHANNEL_SIZE);
					uae_sem_post (&ciw->sub_sem2);
				}

				volume_main = currprefs.sound_volume;
				int vol_mult[2];
				for (int j = 0; j < 2; j++) {
					volume[j] = ciw->cdda_volume[j];
					vol_mult[j] = (100 - volume_main) * volume[j] / 100;
					if (vol_mult[j])
						vol_mult[j]++;
					if (vol_mult[j] >= 32768)
						vol_mult[j] = 32768;
				}
				uae_s16 *p = (uae_s16*)(px[bufnum]);
				for (i = 0; i < num_sectors * 2352 / 4; i++) {
					p[i * 2 + 0] = p[i * 2 + 0] * vol_mult[0] / 32768;
					p[i * 2 + 1] = p[i * 2 + 1] * vol_mult[1] / 32768;
				}
		
				bufon[bufnum] = 1;
				mmr = waveOutWrite (ciw->cdda_wavehandle, &whdr[bufnum], sizeof (WAVEHDR));
				if (mmr != MMSYSERR_NOERROR) {
					write_log (L"IOCTL CDDA: waveOutWrite %d\n", mmr);
					break;
				}

				if (firstloops < 0) {
					if (ciw->cdda_scan) {
						cdda_pos += ciw->cdda_scan * num_sectors;
						if (cdda_pos < 0)
							cdda_pos = 0;
					} else  {
						cdda_pos += num_sectors;
					}
					if (cdda_pos - num_sectors < ciw->cdda_end && cdda_pos >= ciw->cdda_end) {
						ciw->cdda_play_finished = 1;
						ciw->cdda_play = -1;
						cdda_pos = ciw->cdda_end;
					}
				}

				ciw->cd_last_pos = cdda_pos;

			}


			if (bufon[0] == 0 && bufon[1] == 0) {
				while (!(whdr[0].dwFlags & WHDR_DONE) || !(whdr[1].dwFlags & WHDR_DONE))
					Sleep (10);
				while (ciw->cdda_paused && ciw->cdda_play > 0)
					Sleep (10);
			}

			bufnum = 1 - bufnum;

		}
	}

end:
	ciw->subcodevalid = false;
	while (!(whdr[0].dwFlags & WHDR_DONE) || !(whdr[1].dwFlags & WHDR_DONE))
		Sleep (10);
	for (i = 0; i < 2; i++)
		waveOutUnprepareHeader  (ciw->cdda_wavehandle, &whdr[i], sizeof (WAVEHDR));

	cdda_closewav (ciw);
	VirtualFree (p, 0, MEM_RELEASE);
	ciw->cdda_play = 0;
	write_log (L"IOCTL CDDA: thread killed\n");
	return NULL;
}

static void cdda_stop (struct dev_info_ioctl *ciw)
{
	if (ciw->cdda_play > 0) {
		ciw->cdda_play = -1;
		while (ciw->cdda_play) {
			Sleep (10);
		}
	}
	ciw->cdda_play_finished = 0;
	ciw->cdda_paused = 0;
	ciw->subcodevalid = 0;
}

/* pause/unpause CD audio */
static int ioctl_command_pause (int unitnum, int paused)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return -1;
	int old = ciw->cdda_paused;
	ciw->cdda_paused = paused;
	return old;
}


/* stop CD audio */
static int ioctl_command_stop (int unitnum)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	cdda_stop (ciw);
		
	return 1;
}

static uae_u32 ioctl_command_volume (int unitnum, uae_u16 volume_left, uae_u16 volume_right)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return -1;
	uae_u32 old = (ciw->cdda_volume[1] << 16) | (ciw->cdda_volume[0] << 0);
	ciw->cdda_volume[0] = volume_left;
	ciw->cdda_volume[1] = volume_right;
	return old;
}

/* play CD audio */
static int ioctl_command_play (int unitnum, int startlsn, int endlsn, int scan, play_subchannel_callback subfunc)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	if (!open_createfile (ciw, 0))
		return 0;
	if (!isaudiotrack (&ciw->di.toc, startlsn))
		return 0;
	ciw->cdda_play_finished = 0;
	ciw->cdda_subfunc = subfunc;
	ciw->cdda_scan = scan > 0 ? 10 : (scan < 0 ? 10 : 0);
	if (!ciw->cdda_play) {
		uae_start_thread (L"cdimage_cdda_play", cdda_play, ciw, NULL);
	}
	ciw->cdda_start = startlsn;
	ciw->cdda_end = endlsn;
	ciw->cd_last_pos = ciw->cdda_start;
	ciw->cdda_play++;

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

/* read qcode */
static int ioctl_command_qcode (int unitnum, uae_u8 *buf, int sector)
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

	memset (buf, 0, SUBQ_SIZE);
	p = buf;

	status = AUDIO_STATUS_NO_STATUS;
	if (ciw->cdda_play) {
		status = AUDIO_STATUS_IN_PROGRESS;
		if (ciw->cdda_paused)
			status = AUDIO_STATUS_PAUSED;
	} else if (ciw->cdda_play_finished) {
		status = AUDIO_STATUS_PLAY_COMPLETE;
	}

	p[1] = status;
	p[3] = 12;

	p = buf + 4;

	if (sector < 0)
		pos = ciw->cd_last_pos;
	else
		pos = sector;

	if (!regenerate) {
		if (sector < 0 && ciw->subcodevalid && ciw->cdda_play) {
			uae_sem_wait (&ciw->sub_sem2);
			uae_u8 subbuf[SUB_CHANNEL_SIZE];
			sub_deinterleave (ciw->subcodebuf, subbuf);
			memcpy (p, subbuf + 12, 12);
			uae_sem_post (&ciw->sub_sem2);
			valid = true;
		}
		if (!valid && sector >= 0) {
			DWORD len;
			uae_sem_wait (&ciw->sub_sem);
			seterrormode (ciw);
			RAW_READ_INFO rri;
			rri.DiskOffset.QuadPart = 2048 * (pos + 0);
			rri.SectorCount = 1;
			rri.TrackMode = RawWithSubCode;
			memset (ciw->tempbuffer, 0, CD_RAW_SECTOR_WITH_SUBCODE_SIZE);
			if (!DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri, ciw->tempbuffer, CD_RAW_SECTOR_WITH_SUBCODE_SIZE, &len, NULL)) {
				DWORD err = GetLastError ();
				write_log (L"IOCTL_CDROM_RAW_READ SUBQ CDDA sector %d returned %d\n", pos, err);
			}
			reseterrormode (ciw);
			uae_u8 subbuf[SUB_CHANNEL_SIZE];
			sub_deinterleave (ciw->tempbuffer + 2352, subbuf);
			uae_sem_post (&ciw->sub_sem);
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

//	write_log (L"%6d %02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x\n",
//		pos, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
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
	swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
	swb.spt.CdbLength = cmdlen;
	swb.spt.DataIn = SCSI_IOCTL_DATA_IN;
	swb.spt.DataTransferLength = IOCTL_DATA_BUFFER;
	swb.spt.DataBuffer = p;
	memset (p, 0, IOCTL_DATA_BUFFER);
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
		write_log (L"IOCTL_RAW_SCSI unit %d, CMD=%d, ERR=%d ", unitnum, cmd[0], err);
		return 0;
	}
	memcpy (data, p, swb.spt.DataTransferLength > datalen ? datalen : swb.spt.DataTransferLength);
	return 1;
}

static int spti_inquiry (struct dev_info_ioctl *ciw, int unitnum, uae_u8 *data)
{
	uae_u8 cmd[6] = { 0x12,0,0,0,36,0 }; /* INQUIRY */
	int len = sizeof cmd;

	do_raw_scsi (ciw, unitnum, cmd, len, data, 256);
	return 1;
}

static int spti_read (struct dev_info_ioctl *ciw, int unitnum, uae_u8 *data, int sector, int sectorsize)
{
	/* number of bytes returned depends on type of track:
	* CDDA = 2352
	* Mode1 = 2048
	* Mode2 = 2336
	* Mode2 Form 1 = 2048
	* Mode2 Form 2 = 2328
	*/
	uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, 1, 0x10, 0, 0 };
	ciw->cd_last_pos = sector;
	cmd[3] = (uae_u8)(sector >> 16);
	cmd[4] = (uae_u8)(sector >> 8);
	cmd[5] = (uae_u8)(sector >> 0);
	gui_flicker_led (LED_CD, unitnum, 1);
	int len = sizeof cmd;
	return do_raw_scsi (ciw, unitnum,  cmd, len, data, sectorsize);
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

static int ioctl_command_rawread (int unitnum, uae_u8 *data, int sector, int size, int sectorsize, uae_u32 extra)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	RAW_READ_INFO rri;
	DWORD len;
	uae_u8 *p = ciw->tempbuffer;
	int ret = 0;

	if (log_scsi)
		write_log (L"IOCTL rawread unit=%d sector=%d blocksize=%d\n", unitnum, sector, sectorsize);
	if (!os_vista)
		return spti_read (ciw, unitnum, data, sector, sectorsize);
	if (!open_createfile (ciw, 1))
		return 0;
	cdda_stop (ciw);
	gui_flicker_led (LED_CD, unitnum, 1);
	if (sectorsize > 0) {
		if (sectorsize != 2336 && sectorsize != 2352 && sectorsize != 2048)
			return 0;
		seterrormode (ciw);
		rri.DiskOffset.QuadPart = sector * 2048;
		rri.SectorCount = 1;
		rri.TrackMode = RawWithSubCode;
		len = sectorsize;
		memset (p, 0, sectorsize);
		if (!DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri,
			p, IOCTL_DATA_BUFFER, &len, NULL)) {
				DWORD err = GetLastError ();
				write_log (L"IOCTL rawread unit=%d sector=%d blocksize=%d mode=%d, ERR=%d\n",
					unitnum, sector, sectorsize, rri.TrackMode, err);
		}
		reseterrormode (ciw);
		if (data) {
			if (sectorsize == 2352)
				memcpy (data, p, sectorsize);
			else
				memcpy (data, p + 16, sectorsize);
			data += sectorsize;
			ret += sectorsize;
		}
		ciw->cd_last_pos = sector;
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
				seterrormode (ciw);
				rri.DiskOffset.QuadPart = sector * 2048;
				rri.SectorCount = 1;
				rri.TrackMode = errorfield > 0 ? RawWithC2AndSubCode : RawWithSubCode;
				len = sectorsize;
				memset (p, 0, blocksize);
				if (!DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri, p, IOCTL_DATA_BUFFER, &len, NULL)) {
					DWORD err = GetLastError ();
					write_log (L"IOCTL rawread unit=%d sector=%d blocksize=%d mode=%d, ERR=%d\n",
						unitnum, sector, sectorsize, rri.TrackMode, err);
					if (err) {
						reseterrormode (ciw);
						return ret;
					}
				}
				reseterrormode (ciw);
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
				ret += data - odata;
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

	DWORD dtotal;
	int cnt = 3;
	uae_u8 *p = ciw->tempbuffer;
	int blocksize = ciw->di.bytespersector;

	if (!open_createfile (ciw, 0))
		return 0;
	cdda_stop (ciw);
	ciw->cd_last_pos = sector;
	while (cnt-- > 0) {
		gui_flicker_led (LED_CD, unitnum, 1);
		seterrormode (ciw);
		if (SetFilePointer (ciw->h, sector * ciw->di.bytespersector, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			reseterrormode (ciw);
			if (win32_error (ciw, unitnum, L"SetFilePointer") < 0)
				continue;
			return 0;
		}
		reseterrormode (ciw);
		break;
	}
	while (size-- > 0) {
		gui_flicker_led (LED_CD, unitnum, 1);
		seterrormode (ciw);
		if (write) {
			if (data) {
				memcpy (p, data, blocksize);
				data += blocksize;
			}
			if (!WriteFile (ciw->h, p, blocksize, &dtotal, 0)) {
				int err;
				reseterrormode (ciw);
				err = win32_error (ciw, unitnum, L"WriteFile");
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
				if (win32_error (ciw, unitnum, L"ReadFile") < 0)
					continue;
				return 0;
			}
			if (dtotal == 0) {
				/* ESS Mega (CDTV) "fake" data area returns zero bytes and no error.. */
				spti_read (ciw, unitnum, data, sector, 2048);
				if (log_scsi)
					write_log (L"IOCTL unit %d, sector %d: ReadFile()==0. SPTI=%d\n", unitnum, sector, GetLastError ());
				return 1;
			}
			if (data) {
				memcpy (data, p, blocksize);
				data += blocksize;
			}
		}
		reseterrormode (ciw);
		gui_flicker_led (LED_CD, unitnum, 1);
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
	uae_sem_wait (&ciw->sub_sem);
	seterrormode (ciw);
	while (cnt-- > 0) {
		if (!DeviceIoControl (ciw->h, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof (geom), &len, NULL)) {
			if (win32_error (ciw, unitnum, L"IOCTL_CDROM_GET_DRIVE_GEOMETRY") < 0)
				continue;
			reseterrormode (ciw);
			uae_sem_post (&ciw->sub_sem);
			return 0;
		}
		break;
	}
	reseterrormode (ciw);
	uae_sem_post (&ciw->sub_sem);
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

/* read toc */
static int ioctl_command_toc (int unitnum, struct cd_toc_head *tocout)
{
	struct dev_info_ioctl *ciw = unitisopen (unitnum);
	if (!ciw)
		return 0;

	DWORD len;
	int i;
	struct cd_toc_head *th = &ciw->di.toc;
	struct cd_toc *t = th->toc;
	int cnt = 3;
	CDROM_TOC *toc = &ciw->cdromtoc;

	if (!unitisopen (unitnum))
		return 0;

	if (!open_createfile (ciw, 0))
		return 0;
	gui_flicker_led (LED_CD, unitnum, 1);
	while (cnt-- > 0) {
		seterrormode (ciw);
		if (!DeviceIoControl (ciw->h, IOCTL_CDROM_READ_TOC, NULL, 0, toc, sizeof (CDROM_TOC), &len, NULL)) {
			reseterrormode (ciw);
			if (win32_error (ciw, unitnum, L"IOCTL_CDROM_READ_TOC") < 0)
				continue;
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
	t++;

	t->adr = 1;
	t->point = 0xa2;
	t->paddress = th->lastaddress;
	t++;

	gui_flicker_led (LED_CD, unitnum, 1);
	memcpy (tocout, th, sizeof (struct cd_toc_head));
	return 1;
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
	_stprintf (di->mediapath, L"\\\\.\\%c:", ciw->drvletter);
	if (fetch_geometry (ciw, unitnum, di)) { // || ioctl_command_toc (unitnum))
		di->media_inserted = 1;
	}
	ioctl_command_toc (unitnum, &di->toc);
	di->removable = ciw->type == DRIVE_CDROM ? 1 : 0;
	di->write_protected = ciw->type == DRIVE_CDROM ? 1 : 0;
	di->type = ciw->type == DRIVE_CDROM ? INQ_ROMD : INQ_DASD;
	di->unitnum = unitnum + 1;
	_tcscpy (di->label, ciw->drvlettername);
	di->backend = L"IOCTL";
}

static void trim (TCHAR *s)
{
	while (_tcslen (s) > 0 && s[_tcslen (s) - 1] == ' ')
		s[_tcslen (s) - 1] = 0;
}

/* open device level access to cd rom drive */
static int sys_cddev_open (struct dev_info_ioctl *ciw, int unitnum)
{
	uae_u8 inquiry[256];
	ciw->cdda_volume[0] = 0x7fff;
	ciw->cdda_volume[1] = 0x7fff;
	ciw->cdda_volume_main = currprefs.sound_volume;
	/* buffer must be page aligned for device access */
	ciw->tempbuffer = (uae_u8*)VirtualAlloc (NULL, IOCTL_DATA_BUFFER, MEM_COMMIT, PAGE_READWRITE);
	if (!ciw->tempbuffer) {
		write_log (L"IOCTL: failed to allocate buffer");
		return 1;
	}

	_tcscpy (ciw->di.vendorid, L"UAE");
	_stprintf (ciw->di.productid, L"SCSI CD%d IMG", unitnum);
	_tcscpy (ciw->di.revision, L"0.1");
	if (spti_inquiry (ciw, unitnum, inquiry)) {
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
		close_createfile (ciw);
	}

	if (!open_createfile (ciw, 0)) {
		write_log (L"IOCTL: failed to open '%s', err=%d\n", ciw->devname, GetLastError ());
		goto error;
	}
	uae_sem_init (&ciw->sub_sem, 0, 1);
	uae_sem_init (&ciw->sub_sem2, 0, 1);
	ioctl_command_stop (unitnum);
	update_device_info (unitnum);
	ciw->open = true;
	write_log (L"IOCTL: device '%s' (%s/%s/%s) opened succesfully (unit=%d,media=%d)\n",
		ciw->devname, ciw->di.vendorid, ciw->di.productid, ciw->di.revision,
		unitnum, ciw->di.media_inserted);
	return 0;
error:
	win32_error (ciw, unitnum, L"CreateFile");
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
	cdda_stop (ciw);
	close_createfile (ciw);
	VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
	ciw->tempbuffer = NULL;
	uae_sem_destroy (&ciw->sub_sem);
	uae_sem_destroy (&ciw->sub_sem2);
	ciw->open = false;
	write_log (L"IOCTL: device '%s' closed\n", ciw->devname, unitnum);
}

static int open_device (int unitnum, const TCHAR *ident)
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
		write_log (L"IOCTL close_bus() when already closed!\n");
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
	write_log (L"IOCTL driver closed.\n");
}

static int open_bus (int flags)
{
	int dwDriveMask;
	int drive;

	if (bus_open) {
		write_log (L"IOCTL open_bus() more than once!\n");
		return 1;
	}
	total_devices = 0;
	dwDriveMask = GetLogicalDrives ();
	if (log_scsi)
		write_log (L"IOCTL: drive mask = %08X\n", dwDriveMask);
	dwDriveMask >>= 2; // Skip A and B drives...
	for (drive = 'C'; drive <= 'Z' && total_devices < MAX_TOTAL_SCSI_DEVICES; drive++) {
		if (dwDriveMask & 1) {
			int dt;
			TCHAR tmp[10];
			_stprintf (tmp, L"%c:\\", drive);
			dt = GetDriveType (tmp);
			if (log_scsi)
				write_log (L"IOCTL: drive %c type %d\n", drive, dt);
			if (dt == DRIVE_CDROM) {
				if (log_scsi)
					write_log (L"IOCTL: drive %c: = unit %d\n", drive, total_devices);
				ciw32[total_devices].drvletter = drive;
				_tcscpy (ciw32[total_devices].drvlettername, tmp);
				ciw32[total_devices].type = dt;
				ciw32[total_devices].di.bytespersector = 2048;
				_stprintf (ciw32[total_devices].devname, L"\\\\.\\%c:", drive);
				ciw32[total_devices].h = INVALID_HANDLE_VALUE;
				total_devices++;
			}
		}
		dwDriveMask >>= 1;
	}
	bus_open = 1;
	write_log (L"IOCTL driver open, %d devices.\n", total_devices);
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

static struct device_info *info_device (int unitnum, struct device_info *di, int quick)
{
	struct dev_info_ioctl *ciw = unitcheck (unitnum);
	if (!ciw)
		return 0;
	if (!quick)
		update_device_info (unitnum);
	ciw->di.open = di->open;
	memcpy (di, &ciw->di, sizeof (struct device_info));
	return di;
}

void win32_ioctl_media_change (TCHAR driveletter, int insert)
{
	for (int i = 0; i < total_devices; i++) {
		struct dev_info_ioctl *ciw = &ciw32[i];
		if (ciw->drvletter == driveletter && ciw->di.media_inserted != insert) {
			write_log (L"IOCTL: media change %s %d\n", ciw->drvlettername, insert);
			ciw->di.media_inserted = insert;
			int unitnum = getunitnum (ciw);
			if (unitnum >= 0) {
				update_device_info (unitnum);
				scsi_do_disk_change (unitnum, insert, NULL);
				blkdev_cd_change (unitnum, ciw->drvlettername);
			}
		}
	}
}

struct device_functions devicefunc_win32_ioctl = {
	L"IOCTL",
	open_bus, close_bus, open_device, close_device, info_device,
	0, 0, 0,
	ioctl_command_pause, ioctl_command_stop, ioctl_command_play, ioctl_command_volume, ioctl_command_qcode,
	ioctl_command_toc, ioctl_command_read, ioctl_command_rawread, ioctl_command_write,
	0, ioctl_ismedia
};

#endif
