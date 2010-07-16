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
	int mediainserted;
	int type;
	int blocksize;
	CDROM_TOC toc;
	UINT errormode;
	int playend;
	int fullaccess;
	int cdda_play_finished;
	int cdda_play;
	int cdda_paused;
	int cdda_volume;
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
};

static int MCICDA;

static struct dev_info_ioctl ciw32[MAX_TOTAL_DEVICES];

static void seterrormode (int unitnum)
{
	ciw32[unitnum].errormode = SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
}
static void reseterrormode (int unitnum)
{
	SetErrorMode (ciw32[unitnum].errormode);
}

static void close_device (int unitnum);
static int open_device (int unitnum);

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

static bool unitisopen (int unitnum)
{
	if (!unitcheck (unitnum))
		return false;
	if (ciw32[unitnum].tempbuffer == NULL)
		return false;
	return true;
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

static int win32_error (int unitnum, const TCHAR *format,...)
{
	LPVOID lpMsgBuf;
	va_list arglist;
	TCHAR buf[1000];
	DWORD err = GetLastError ();

	if (err == ERROR_WRONG_DISK) {
		write_log (L"IOCTL: media change, re-opening device\n");
		close_device (unitnum);
		if (!open_device (unitnum))
			write_log (L"IOCTL: re-opening failed!\n");
		return -1;
	}
	va_start (arglist, format);
	_vsntprintf (buf, sizeof buf / sizeof (TCHAR), format, arglist);
	FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	if (log_scsi)
		write_log (L"IOCTL: unit=%d %s,%d: %s\n", unitnum, buf, err, (TCHAR*)lpMsgBuf);
	va_end (arglist);
	return err;
}

static int close_createfile (int unitnum)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

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

static int open_createfile (int unitnum, int fullaccess)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];
	int cnt = 50;
	DWORD len;

	if (ciw->h != INVALID_HANDLE_VALUE) {
		if (fullaccess && ciw->fullaccess == 0) {
			close_createfile (unitnum);
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
		write_log (L"CDDA: wave open %d\n", mmr);
		cdda_closewav (ciw);
		return 0;
	}
	return 1;
}

static void *cdda_play (void *v)
{
	DWORD len;
	struct dev_info_ioctl *ciw = (struct dev_info_ioctl*)v;
	int unitnum = ciw32 - ciw;
	int cdda_pos;
	int num_sectors = CDDA_BUFFERS;
	int bufnum;
	int buffered;
	uae_u8 *px[2], *p;
	int bufon[2];
	int i;
	WAVEHDR whdr[2];
	MMRESULT mmr;
	int volume, volume_main;
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
	volume = -1;
	volume_main = -1;

	if (cdda_openwav (ciw)) {

		for (i = 0; i < 2; i++) {
			memset (&whdr[i], 0, sizeof (WAVEHDR));
			whdr[i].dwBufferLength = 2352 * num_sectors;
			whdr[i].lpData = (LPSTR)px[i];
			mmr = waveOutPrepareHeader (ciw->cdda_wavehandle, &whdr[i], sizeof (WAVEHDR));
			if (mmr != MMSYSERR_NOERROR) {
				write_log (L"CDDA: waveOutPrepareHeader %d:%d\n", i, mmr);
				goto end;
			}
			whdr[i].dwFlags |= WHDR_DONE;
		}

		while (ciw->cdda_play > 0) {

			while (!(whdr[bufnum].dwFlags & WHDR_DONE)) {
				Sleep (10);
				if (!ciw->cdda_play)
					goto end;
			}
			bufon[bufnum] = 0;

			if (oldplay != ciw->cdda_play) {
				cdda_pos = ciw->cdda_start;
				oldplay = ciw->cdda_play;
				firstloops = 25;
				write_log (L"CDDA: playing from %d to %d\n", ciw->cdda_start, ciw->cdda_end);
				ciw->subcodevalid = false;
			}

			if ((cdda_pos < ciw->cdda_end || ciw->cdda_end == 0xffffffff) && !ciw->cdda_paused && ciw->cdda_play) {
				RAW_READ_INFO rri;
				int sectors = num_sectors;

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
					seterrormode (unitnum);
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
					reseterrormode (unitnum);
				}

				uae_sem_post (&ciw->sub_sem);
				if (ciw->subcodevalid) {
					uae_sem_wait (&ciw->sub_sem2);
					memcpy (ciw->subcodebuf, ciw->subcode + (sectors - 1) * SUB_CHANNEL_SIZE, SUB_CHANNEL_SIZE);
					uae_sem_post (&ciw->sub_sem2);
				}


				volume = ciw->cdda_volume;
				volume_main = currprefs.sound_volume;
				int vol_mult = (100 - volume_main) * volume / 100;
				if (vol_mult)
					vol_mult++;
				if (vol_mult >= 65536)
					vol_mult = 65536;
				uae_s16 *p = (uae_s16*)(px[bufnum]);
				for (i = 0; i < num_sectors * 2352 / 4; i++) {
					p[i * 2 + 0] = p[i * 2 + 0] * vol_mult / 65536;
					p[i * 2 + 1] = p[i * 2 + 1] * vol_mult / 65536;
				}
		
				bufon[bufnum] = 1;
				mmr = waveOutWrite (ciw->cdda_wavehandle, &whdr[bufnum], sizeof (WAVEHDR));
				if (mmr != MMSYSERR_NOERROR) {
					write_log (L"CDDA: waveOutWrite %d\n", mmr);
					break;
				}

				if (firstloops < 0) {
					cdda_pos += num_sectors;
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
	write_log (L"CDDA: thread killed\n");
	return NULL;
}

static void cdda_stop (int unitnum)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

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
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	if (!unitisopen (unitnum))
		return 0;
	ciw->cdda_paused = paused;
	return 1;
}


/* stop CD audio */
static int ioctl_command_stop (int unitnum)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	if (!unitisopen (unitnum))
		return 0;

	cdda_stop (unitnum);
		
	return 1;
}

static void ioctl_command_volume (int unitnum, uae_u16 volume)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	ciw->cdda_volume = volume;
}

/* play CD audio */
static int ioctl_command_play (int unitnum, int startlsn, int endlsn, int scan, play_subchannel_callback subfunc)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	if (!unitisopen (unitnum))
		return 0;

	if (!open_createfile (unitnum, 0))
		return 0;
	ciw->cdda_paused = 0;
	ciw->cdda_play_finished = 0;
	ciw->cdda_subfunc = subfunc;
	if (!ciw->cdda_play) {
		uae_start_thread (L"cdda_play", cdda_play, ciw, NULL);
	}
	ciw->cdda_start = startlsn;
	ciw->cdda_end = endlsn;
	ciw->cd_last_pos = ciw->cdda_start;
	ciw->cdda_play++;

	return 1;
}

static void sub_deinterleave (uae_u8 *s, uae_u8 *d)
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
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	if (!unitisopen (unitnum)) {
		write_log (L"qcode: %d no unit\n", unitnum);
		return 0;
	}

	uae_u8 *p;
	int trk;
	CDROM_TOC *toc = &ciw->toc;
	int pos;
	int msf;
	int start, end;
	int status;
	bool valid = false;
	bool regenerate = false;

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
			seterrormode (unitnum);
			RAW_READ_INFO rri;
			rri.DiskOffset.QuadPart = 2048 * (pos + 0);
			rri.SectorCount = 1;
			rri.TrackMode = RawWithSubCode;
			memset (ciw->tempbuffer, 0, CD_RAW_SECTOR_WITH_SUBCODE_SIZE);
			if (!DeviceIoControl (ciw->h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri, ciw->tempbuffer, CD_RAW_SECTOR_WITH_SUBCODE_SIZE, &len, NULL)) {
				DWORD err = GetLastError ();
				write_log (L"IOCTL_CDROM_RAW_READ SUBQ CDDA sector %d returned %d\n", pos, err);
			}
			reseterrormode (unitnum);
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

static uae_u8 *spti_read (int unitnum, int sector, int sectorsize)
{
	DWORD status, returned;
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	uae_u8 *p = ciw32[unitnum].tempbuffer;
	/* number of bytes returned depends on type of track:
	* CDDA = 2352
	* Mode1 = 2048
	* Mode2 = 2336
	* Mode2 Form 1 = 2048
	* Mode2 Form 2 = 2328
	*/
	uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, 1, 0x10, 0, 0 };
	int len = sizeof cmd;

	if (!unitisopen (unitnum))
		return 0;

	if (!open_createfile (unitnum, 1))
		return 0;
	ciw32[unitnum].cd_last_pos = sector;
	cmd[3] = (uae_u8)(sector >> 16);
	cmd[4] = (uae_u8)(sector >> 8);
	cmd[5] = (uae_u8)(sector >> 0);
	gui_flicker_led (LED_CD, unitnum, 1);
	memset (&swb, 0, sizeof (swb));
	memcpy (swb.spt.Cdb, cmd, len);
	swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
	swb.spt.CdbLength = len;
	swb.spt.DataIn = SCSI_IOCTL_DATA_IN;
	swb.spt.DataTransferLength = IOCTL_DATA_BUFFER;
	swb.spt.DataBuffer = p;
	memset (p, 0, IOCTL_DATA_BUFFER);
	swb.spt.TimeOutValue = 80 * 60;
	swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
	swb.spt.SenseInfoLength = 32;

	seterrormode (unitnum);
	status = DeviceIoControl (ciw32[unitnum].h, IOCTL_SCSI_PASS_THROUGH_DIRECT,
		&swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&returned, NULL);
	reseterrormode (unitnum);
	if (!status) {
		DWORD err = GetLastError ();
		write_log (L"IOCTL_RAW_SCSI unit %d, ERR=%d ", unitnum, err);
		return 0;
	}
	return p;
}

uae_u8 *ioctl_command_rawread (int unitnum, uae_u8 *data, int sector, int size, int sectorsize)
{
	RAW_READ_INFO rri;
	DWORD len;
	uae_u8 *p = ciw32[unitnum].tempbuffer;

	if (log_scsi)
		write_log (L"IOCTL rawread unit=%d sector=%d blocksize=%d\n", unitnum, sector, sectorsize);
	if (!os_vista)
		return spti_read (unitnum, sector, sectorsize);
	if (!open_createfile (unitnum, 1))
		return 0;
	cdda_stop (unitnum);
	if (sectorsize != 2336 && sectorsize != 2352 && sectorsize != 2048)
		return 0;
	while (size-- > 0) {
		gui_flicker_led (LED_CD, unitnum, 1);
		seterrormode (unitnum);
		rri.DiskOffset.QuadPart = sector * 2048;
		rri.SectorCount = 1;
		rri.TrackMode = RawWithSubCode;
		len = sectorsize;
		memset (p, 0, sectorsize);
		if (!DeviceIoControl (ciw32[unitnum].h, IOCTL_CDROM_RAW_READ, &rri, sizeof rri,
			p, IOCTL_DATA_BUFFER, &len, NULL)) {
				DWORD err = GetLastError ();
				write_log (L"IOCTL rawread unit=%d sector=%d blocksize=%d, ERR=%d\n", unitnum, sector, sectorsize, err);
		}
		reseterrormode (unitnum);
		if (data) {
			memcpy (data, p, sectorsize);
			data += sectorsize;
		}
		ciw32[unitnum].cd_last_pos = sector;
		break;
	}
	if (sectorsize == 2352)
		return p;
	return p + 16;
}

static int ioctl_command_readwrite (int unitnum, int sector, int size, int write, int blocksize, uae_u8 *data, uae_u8 **ptr)
{
	DWORD dtotal;
	int cnt = 3;
	uae_u8 *p = ciw32[unitnum].tempbuffer;

	*ptr = NULL;

	if (!unitisopen (unitnum))
		return 0;

	if (!open_createfile (unitnum, 0))
		return 0;
	cdda_stop (unitnum);
	ciw32[unitnum].cd_last_pos = sector;
	while (cnt-- > 0) {
		gui_flicker_led (LED_CD, unitnum, 1);
		seterrormode (unitnum);
		if (SetFilePointer (ciw32[unitnum].h, sector * ciw32[unitnum].blocksize, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			reseterrormode (unitnum);
			if (win32_error (unitnum, L"SetFilePointer") < 0)
				continue;
			return 0;
		}
		reseterrormode (unitnum);
		break;
	}
	while (size-- > 0) {
		gui_flicker_led (LED_CD, unitnum, 1);
		seterrormode (unitnum);
		if (write) {
			if (data) {
				memcpy (p, data, blocksize);
				data += blocksize;
			}
			if (!WriteFile (ciw32[unitnum].h, p, blocksize, &dtotal, 0)) {
				int err;
				reseterrormode (unitnum);
				err = win32_error (unitnum, L"WriteFile");
				if (err < 0)
					continue;
				if (err == ERROR_WRITE_PROTECT)
					return -1;
				return 0;
			}
			*ptr = p;
		} else {
			dtotal = 0;
			if (!ReadFile (ciw32[unitnum].h, p, blocksize, &dtotal, 0)) {
				reseterrormode (unitnum);
				if (win32_error (unitnum, L"ReadFile") < 0)
					continue;
				return 0;
			}
			if (dtotal == 0) {
				/* ESS Mega (CDTV) "fake" data area returns zero bytes and no error.. */
				*ptr = spti_read (unitnum, sector, 2048);
				if (log_scsi)
					write_log (L"IOCTL unit %d, sector %d: ReadFile()==0. SPTI=%d\n", unitnum, sector, *ptr == 0 ? GetLastError () : 0);
				return 1;
			} else {
				*ptr = p;
			}
			if (data) {
				memcpy (data, p, blocksize);
				data += blocksize;
			}
		}
		reseterrormode (unitnum);
		gui_flicker_led (LED_CD, unitnum, 1);
	}
	return 1;
}

static int ioctl_command_write (int unitnum, uae_u8 *data, int sector, int size)
{
	uae_u8 *ptr;
	return ioctl_command_readwrite (unitnum, sector, size, 1, ciw32[unitnum].blocksize, data, &ptr);
}

static uae_u8 *ioctl_command_read (int unitnum, uae_u8 *data, int sector, int size)
{
	uae_u8 *ptr;
	if (ioctl_command_readwrite (unitnum, sector, size, 0, ciw32[unitnum].blocksize, data, &ptr) > 0)
		return ptr;
	return NULL;
}

static int fetch_geometry (int unitnum, struct device_info *di)
{
	DISK_GEOMETRY geom;
	DWORD len;
	int cnt = 3;

	if (!open_createfile (unitnum, 0))
		return 0;
	uae_sem_wait (&ciw32[unitnum].sub_sem);
	seterrormode (unitnum);
	while (cnt-- > 0) {
		if (!DeviceIoControl (ciw32[unitnum].h, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof (geom), &len, NULL)) {
			if (win32_error (unitnum, L"IOCTL_CDROM_GET_DRIVE_GEOMETRY") < 0)
				continue;
			reseterrormode (unitnum);
			uae_sem_post (&ciw32[unitnum].sub_sem);
			return 0;
		}
	}
	reseterrormode (unitnum);
	uae_sem_post (&ciw32[unitnum].sub_sem);
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
	return fetch_geometry (unitnum, &ciw->di);
}

/* read toc */
static struct cd_toc_head *ioctl_command_toc (int unitnum)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];
	DWORD len;
	int i;
	struct cd_toc_head *th = (struct cd_toc_head*)ciw->tempbuffer;
	struct cd_toc *t = th->toc;
	int cnt = 3;
	CDROM_TOC *toc = &ciw->toc;

	if (!unitisopen (unitnum))
		return NULL;

	if (!open_createfile (unitnum, 0))
		return 0;
	cdda_stop (unitnum);
	ciw32[unitnum].cd_last_pos = 0;
	gui_flicker_led (LED_CD, unitnum, 1);
	while (cnt-- > 0) {
		seterrormode (unitnum);
		if (!DeviceIoControl (ciw32[unitnum].h, IOCTL_CDROM_READ_TOC, NULL, 0, toc, sizeof (CDROM_TOC), &len, NULL)) {
			reseterrormode (unitnum);
			if (win32_error (unitnum, L"IOCTL_CDROM_READ_TOC") < 0)
				continue;
			return 0;
		}
		reseterrormode (unitnum);
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
	return th;
}

static void update_device_info (int unitnum)
{
	if (!unitcheck (unitnum))
		return;
	struct device_info *di = &ciw32[unitnum].di;
	di->bus = unitnum;
	di->target = 0;
	di->lun = 0;
	di->media_inserted = 0;
	di->bytespersector = 2048;
	_stprintf (di->mediapath, L"\\\\.\\%c:", ciw32[unitnum].drvletter);
	if (fetch_geometry (unitnum, di)) { // || ioctl_command_toc (unitnum))
		di->media_inserted = 1;
		ciw32[unitnum].blocksize = di->bytespersector;
	}
	di->write_protected = ciw32[unitnum].type == DRIVE_CDROM ? 1 : 0;
	di->type = ciw32[unitnum].type == DRIVE_CDROM ? INQ_ROMD : INQ_DASD;
	di->id = ciw32[unitnum].drvletter;
	_tcscpy (di->label, ciw32[unitnum].drvlettername);
	memcpy (&di->toc, &ciw32[unitnum].toc, sizeof (struct cd_toc_head));
}

/* open device level access to cd rom drive */
static int sys_cddev_open (int unitnum)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	ciw->cdda_volume = 0xffff;
	ciw->cdda_volume_main = currprefs.sound_volume;
	/* buffer must be page aligned for device access */
	ciw->tempbuffer = (uae_u8*)VirtualAlloc (NULL, IOCTL_DATA_BUFFER, MEM_COMMIT, PAGE_READWRITE);
	if (!ciw->tempbuffer) {
		write_log (L"IOCTL: failed to allocate buffer");
		return 1;
	}
	if (!open_createfile (unitnum, 0)) {
		write_log (L"IOCTL: failed to open '%s', err=%d\n", ciw->devname, GetLastError ());
		goto error;
	}
	uae_sem_init (&ciw->sub_sem, 0, 1);
	uae_sem_init (&ciw->sub_sem2, 0, 1);
	ciw->mediainserted = ioctl_command_toc (unitnum) ? 1 : 0;
	update_device_info (unitnum);
	write_log (L"IOCTL: device '%s' opened succesfully (unit number=%d,media=%d)\n", ciw->devname, unitnum, ciw->mediainserted);
	ioctl_command_stop (unitnum);
	return 0;
error:
	win32_error (unitnum, L"CreateFile");
	VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
	ciw->tempbuffer = NULL;
	CloseHandle (ciw->h);
	ciw->h = NULL;
	return -1;
}

/* close device handle */
void sys_cddev_close (int unitnum)
{
	struct dev_info_ioctl *ciw = &ciw32[unitnum];

	if (!unitcheck (unitnum))
		return;
	cdda_stop (unitnum);
	close_createfile (unitnum);
	VirtualFree (ciw->tempbuffer, 0, MEM_RELEASE);
	ciw->tempbuffer = NULL;
	uae_sem_destroy (&ciw->sub_sem);
	uae_sem_destroy (&ciw->sub_sem2);
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

static int check_bus (int flags)
{
	return 1;
}

static int open_bus (int flags)
{
	int dwDriveMask;
	int drive, i;
	TCHAR tmp[10];

	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		memset (&ciw32[i], 0, sizeof (struct dev_info_ioctl));
		ciw32[i].h = INVALID_HANDLE_VALUE;
	}
	MCICDA = 1;//os_vista ? 1 : 0;
	total_devices = 0;
	dwDriveMask = GetLogicalDrives ();
	if (log_scsi)
		write_log (L"IOCTL: drive mask = %08X\n", dwDriveMask);
	dwDriveMask >>= 2; // Skip A and B drives...
	for (drive = 'C'; drive <= 'Z' && total_devices < MAX_TOTAL_DEVICES; drive++) {
		if (dwDriveMask & 1) {
			int dt;
			_stprintf (tmp, L"%c:\\", drive);
			dt = GetDriveType (tmp);
			if (log_scsi)
				write_log (L"IOCTL: drive %c type %d\n", drive, dt);
			if (((flags & (1 << INQ_ROMD)) && dt == DRIVE_CDROM) || ((flags & (1 << INQ_DASD)) && dt == DRIVE_FIXED)) {
				if (log_scsi)
					write_log (L"IOCTL: drive %c: = unit %d\n", drive, total_devices);
				ciw32[total_devices].drvletter = drive;
				_tcscpy (ciw32[total_devices].drvlettername, tmp);
				ciw32[total_devices].type = dt;
				ciw32[total_devices].blocksize = 2048;
				_stprintf (ciw32[total_devices].devname, L"\\\\.\\%c:", drive);
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
		return -1;
	if (!unitisopen (unitnum))
		return -1;
	if (quick) {
		struct dev_info_ioctl *ciw = &ciw32[unitnum];
		return ciw->mediainserted;
	}
	update_device_info (unitnum);
	return ismedia (unitnum);
}

static struct device_info *info_device (int unitnum, struct device_info *di, int quick)
{
	if (!unitcheck (unitnum))
		return 0;
	if (!quick)
		update_device_info (unitnum);
	memcpy (di, &ciw32[unitnum].di, sizeof (struct device_info));
	return di;
}

void win32_ioctl_media_change (TCHAR driveletter, int insert)
{
	int i;

	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		if (ciw32[i].drvletter == driveletter && ciw32[i].mediainserted != insert) {
			write_log (L"IOCTL: media change %s %d\n", ciw32[i].drvlettername, insert);
			ciw32[i].mediainserted = insert;
			update_device_info (i);
			scsi_do_disk_change (driveletter, insert, NULL);
#ifdef RETROPLATFORM
			rp_cd_image_change (i, ciw32[i].drvlettername);
#endif
		}
	}
}

static struct device_scsi_info *ioctl_scsi_info (int unitnum, struct device_scsi_info *dsi)
{
	if (!unitcheck (unitnum))
		return 0;
	dsi->buffer = ciw32[unitnum].tempbuffer;
	dsi->bufsize = IOCTL_DATA_BUFFER;
	return dsi;
}

struct device_functions devicefunc_win32_ioctl = {
	check_bus, open_bus, close_bus, open_device, close_device, info_device,
	0, 0, 0,
	ioctl_command_pause, ioctl_command_stop, ioctl_command_play, ioctl_command_volume, ioctl_command_qcode,
	ioctl_command_toc, ioctl_command_read, ioctl_command_rawread, ioctl_command_write,
	0, ioctl_scsi_info, ioctl_ismedia
};

#endif
