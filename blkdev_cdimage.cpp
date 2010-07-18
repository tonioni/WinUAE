/*
* UAE
*
* CD32/CDTV image file support
*
* Copyright 2010 Toni Wilen
*
*/
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "blkdev.h"
#include "zfile.h"
#include "gui.h"
#include "fsdb.h"
#include "threaddep/thread.h"
#include "scsidev.h"
#include <mp3decoder.h>
#include <memory.h>
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#define scsi_log write_log

#define USE 1

#define CDDA_BUFFERS 6

#define AUDIO_STATUS_NOT_SUPPORTED  0x00
#define AUDIO_STATUS_IN_PROGRESS    0x11
#define AUDIO_STATUS_PAUSED         0x12
#define AUDIO_STATUS_PLAY_COMPLETE  0x13
#define AUDIO_STATUS_PLAY_ERROR     0x14
#define AUDIO_STATUS_NO_STATUS      0x15

struct cdtoc
{
	struct zfile *handle;
	int offset;
	uae_u8 *data;
	struct zfile *subhandle;
	int suboffset;
	uae_u8 *subdata;

	int filesize;
	TCHAR *fname;
	int address;
	uae_u8 adr, ctrl;
	int track;
	int size;
	int mp3;
	int subcode;
};

struct cdunit {
	bool enabled;
	bool open;
	uae_u8 buffer[2352];
	struct cdtoc toc[102];
	int tracks;
	uae_u64 cdsize;
	int blocksize;

	int cdda_play_finished;
	int cdda_play;
	int cdda_paused;
	int cdda_volume[2];
	int cdda_scan;
	int cd_last_pos;
	int cdda_start, cdda_end;
	play_subchannel_callback cdda_subfunc;

	int imagechange;
	int donotmountme;
	TCHAR newfile[MAX_DPATH];
	uae_sem_t sub_sem;
	struct device_info di;
};

static struct cdunit cdunits[MAX_TOTAL_SCSI_DEVICES];
static int bus_open;

static struct cdunit *unitisopen (int unitnum)
{
	struct cdunit *cdu = &cdunits[unitnum];
	if (cdu->open)
		return cdu;
	return NULL;
}

static struct cdtoc *findtoc (struct cdunit *cdu, int *sectorp)
{
	int i;
	int sector;

	sector = *sectorp;
	for (i = 0; i <= cdu->tracks; i++) {
		struct cdtoc *t = &cdu->toc[i];
		if (t->address > sector) {
			if (i == 0) {
				*sectorp = 0;
				return t;
			}
			t--;
			sector -= t->address;
			*sectorp = sector;
			return t;
		}
	}
	return NULL;
}

#ifdef _WIN32

static HWAVEOUT cdda_wavehandle;

static void cdda_closewav (void)
{
	if (cdda_wavehandle != NULL)
		waveOutClose (cdda_wavehandle);
	cdda_wavehandle = NULL;
}

static int cdda_openwav (void)
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
	mmr = waveOutOpen (&cdda_wavehandle, WAVE_MAPPER, &wav, 0, 0, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT);
	if (mmr != MMSYSERR_NOERROR) {
		write_log (L"CDDA: wave open %d\n", mmr);
		cdda_closewav ();
		return 0;
	}
	return 1;
}

static int getsub (uae_u8 *dst, struct cdunit *cdu, struct cdtoc *t, int sector)
{
	int ret = 0;
	uae_sem_wait (&cdu->sub_sem);
	if (t->subcode) {
		if (t->subhandle) {
			zfile_fseek (t->subhandle, sector * SUB_CHANNEL_SIZE + t->suboffset, SEEK_SET);
			if (zfile_fread (dst, SUB_CHANNEL_SIZE, 1, t->subhandle) > 0)
				ret = t->subcode;
		} else {
			memcpy (dst, t->subdata + sector * SUB_CHANNEL_SIZE + t->suboffset, SUB_CHANNEL_SIZE);
			ret = t->subcode;
		}
	}
	if (!ret) {
		memset (dst, 0, SUB_CHANNEL_SIZE);
		// regenerate Q-subchannel
		uae_u8 *s = dst + 12;
		s[0] = (t->ctrl << 4) | (t->adr << 0);
		s[1] = tobcd (t - &cdu->toc[0] + 1);
		s[2] = tobcd (1);
		int msf = lsn2msf (sector);
		tolongbcd (s + 7, msf);
		msf = lsn2msf (sector - t->address - 150);
		tolongbcd (s + 3, msf);
		ret = 2;
	}
	uae_sem_post (&cdu->sub_sem);
	return ret;
}

static void sub_to_interleaved (const uae_u8 *s, uae_u8 *d)
{
	for (int i = 0; i < 8 * 12; i ++) {
		int dmask = 0x80;
		int smask = 1 << (7 - (i & 7));
		(*d) = 0;
		for (int j = 0; j < 8; j++) {
			(*d) |= (s[(i / 8) + j * 12] & smask) ? dmask : 0;
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

static void dosub (struct cdunit *cdu, struct cdtoc *t, int sector)
{
	uae_u8 *d;
	uae_u8 subbuf[SUB_CHANNEL_SIZE];
	uae_u8 subbuf2[SUB_CHANNEL_SIZE];

	if (!cdu->cdda_subfunc)
		return;

	if (!t) {
		memset (subbuf, 0, sizeof subbuf);
		cdu->cdda_subfunc (subbuf, 1);
		return;
	}
	memset (subbuf, 0, SUB_CHANNEL_SIZE);
	int mode = getsub (subbuf, cdu, t, sector);
	if (mode == 2) { // deinterleaved -> interleaved
		sub_to_interleaved (subbuf, subbuf2);
		d = subbuf2;
	} else {
		d = subbuf;
	}
	cdu->cdda_subfunc (d, 1);
}

static void *cdda_play_func (void *v)
{
	int cdda_pos;
	int num_sectors = CDDA_BUFFERS;
	int quit = 0;
	int bufnum;
	int buffered;
	uae_u8 *px[2], *p;
	int bufon[2];
	int i;
	WAVEHDR whdr[2];
	MMRESULT mmr;
	int volume[2], volume_main;
	int oldplay;
	mp3decoder *mp3dec = NULL;
	struct cdunit *cdu = (struct cdunit*)v;
	int firstloops;

	for (i = 0; i < 2; i++) {
		memset (&whdr[i], 0, sizeof (WAVEHDR));
		whdr[i].dwFlags = WHDR_DONE;
	}

	while (cdu->cdda_play == 0)
		Sleep (10);
	oldplay = -1;

	p = xmalloc (uae_u8, 2 * num_sectors * 4096);
	px[0] = p;
	px[1] = p + num_sectors * 4096;
	bufon[0] = bufon[1] = 0;
	bufnum = 0;
	buffered = 0;
	volume[0] = volume[1] = -1;
	volume_main = -1;

	if (cdda_openwav ()) {

		for (i = 0; i < 2; i++) {
			memset (&whdr[i], 0, sizeof (WAVEHDR));
			whdr[i].dwBufferLength = 2352 * num_sectors;
			whdr[i].lpData = (LPSTR)px[i];
			mmr = waveOutPrepareHeader (cdda_wavehandle, &whdr[i], sizeof (WAVEHDR));
			if (mmr != MMSYSERR_NOERROR) {
				write_log (L"CDDA: waveOutPrepareHeader %d:%d\n", i, mmr);
				goto end;
			}
			whdr[i].dwFlags |= WHDR_DONE;
		}

		while (cdu->cdda_play > 0) {

			if (oldplay != cdu->cdda_play) {
				struct cdtoc *t;
				int sector;

				cdda_pos = cdu->cdda_start;
				oldplay = cdu->cdda_play;
				cdu->cd_last_pos = cdda_pos;
				sector = cdu->cdda_start;
				t = findtoc (cdu, &sector);
				if (!t) {
					write_log (L"CDDA: illegal sector number %d\n", cdu->cdda_start);
				} else {
					write_log (L"CDDA: playing from %d to %d, track %d ('%s', offset %d, secoffset %d)\n",
						cdu->cdda_start, cdu->cdda_end, t->track, t->fname, t->offset, sector);
					if (t->mp3 && !t->data) {
						if (!mp3dec) {
							try {
								mp3dec = new mp3decoder();
							} catch (exception) { };
						}
						if (mp3dec)
							t->data = mp3dec->get (t->handle, t->filesize);
					}
				}
				firstloops = 25;
				while (cdu->cdda_paused && cdu->cdda_play > 0)
					Sleep (10);
			}

			while (!(whdr[bufnum].dwFlags & WHDR_DONE)) {
				Sleep (10);
				if (!cdu->cdda_play)
					goto end;
			}
			bufon[bufnum] = 0;

			if (!isaudiotrack (&cdu->di.toc, cdda_pos))
				goto end; // data track?

			if ((cdda_pos < cdu->cdda_end || cdu->cdda_end == 0xffffffff) && !cdu->cdda_paused && cdu->cdda_play > 0) {
				struct cdtoc *t;
				int sector, cnt;
				int dofinish = 0;

				memset (px[bufnum], 0, num_sectors * 2352);

				if (firstloops > 0) {

					firstloops--;
					for (cnt = 0; cnt < num_sectors; cnt++)
						dosub (cdu, NULL, -1);
					
				} else {

					for (cnt = 0; cnt < num_sectors; cnt++) {
						sector = cdda_pos;

						if (cdu->cdda_scan) {
							cdda_pos += cdu->cdda_scan;
							if (cdda_pos < 0)
								cdda_pos = 0;
						} else  {
							cdda_pos++;
						}
						if (cdda_pos - num_sectors < cdu->cdda_end && cdda_pos >= cdu->cdda_end)
							dofinish = 1;

						t = findtoc (cdu, &sector);
						if (t) {
							if (t->handle && !(t->ctrl & 4)) {
								uae_u8 *dst = px[bufnum] + cnt * t->size;
								if (t->mp3 && t->data) {
									memcpy (dst, t->data + sector * t->size + t->offset, t->size);
								} else if (!t->mp3) {
									if (sector * t->size + t->offset + t->size < t->filesize) {
										zfile_fseek (t->handle, sector * t->size + t->offset, SEEK_SET);
										zfile_fread (dst, t->size, 1, t->handle);
									}
								}
							}
							dosub (cdu, t, cdda_pos);
						}
					}

				}
	
				volume_main = currprefs.sound_volume;
				int vol_mult[2];
				for (int j = 0; j < 2; j++) {
					volume[j] = cdu->cdda_volume[j];
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
				mmr = waveOutWrite (cdda_wavehandle, &whdr[bufnum], sizeof (WAVEHDR));
				if (mmr != MMSYSERR_NOERROR) {
					write_log (L"CDDA: waveOutWrite %d\n", mmr);
					break;
				}

				cdu->cd_last_pos = cdda_pos;

				if (dofinish) {
					cdu->cdda_play_finished = 1;
					cdu->cdda_play = -1;
					cdda_pos = cdu->cdda_end + 1;
				}

			}


			if (bufon[0] == 0 && bufon[1] == 0) {
				while (!(whdr[0].dwFlags & WHDR_DONE) || !(whdr[1].dwFlags & WHDR_DONE))
					Sleep (10);
				while (cdu->cdda_paused && cdu->cdda_play > 0)
					Sleep (10);
			}

			bufnum = 1 - bufnum;

		}
	}

end:
	while (!(whdr[0].dwFlags & WHDR_DONE) || !(whdr[1].dwFlags & WHDR_DONE))
		Sleep (10);
	for (i = 0; i < 2; i++)
		waveOutUnprepareHeader  (cdda_wavehandle, &whdr[i], sizeof (WAVEHDR));

	cdda_closewav ();
	xfree (p);
	delete mp3dec;
	cdu->cdda_play = 0;
	write_log (L"CDDA: thread killed\n");
	return NULL;
}

#endif

static void cdda_stop (struct cdunit *cdu)
{
	if (cdu->cdda_play > 0) {
		cdu->cdda_play = -1;
		while (cdu->cdda_play) {
			Sleep (10);
		}
	}
	cdu->cdda_play_finished = 0;
	cdu->cdda_paused = 0;
}


static int command_pause (int unitnum, int paused)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return -1;
	int old = cdu->cdda_paused;
	cdu->cdda_paused = paused;
	return old;
}

static int command_stop (int unitnum)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return 0;
	cdda_stop (cdu);
	return 1;
}

static int command_play (int unitnum, int startlsn, int endlsn, int scan, play_subchannel_callback subfunc)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return 0;
	if (!isaudiotrack (&cdu->di.toc, startlsn))
		return 0;
	cdu->cdda_play_finished = 0;
	cdu->cd_last_pos = startlsn;
	cdu->cdda_start = startlsn;
	cdu->cdda_end = endlsn;
	cdu->cdda_subfunc = subfunc;
	cdu->cdda_scan = scan > 0 ? 10 : (scan < 0 ? 10 : 0);
	if (!cdu->cdda_play)
		uae_start_thread (L"cdda_play", cdda_play_func, cdu, NULL);
	cdu->cdda_play++;
	return 1;
}

static int command_qcode (int unitnum, uae_u8 *buf, int sector)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return 0;

	uae_u8 subbuf[SUB_CHANNEL_SIZE];
	uae_u8 *p;
	int trk;
	int pos;
	int status;

	memset (buf, 0, SUBQ_SIZE);
	p = buf;

	status = AUDIO_STATUS_NO_STATUS;
	if (cdu->cdda_play > 0) {
		status = AUDIO_STATUS_IN_PROGRESS;
		if (cdu->cdda_paused)
			status = AUDIO_STATUS_PAUSED;
	} else if (cdu->cdda_play_finished) {
		status = AUDIO_STATUS_PLAY_COMPLETE;
	}
	if (sector < 0)
		pos = cdu->cd_last_pos;
	else
		pos = sector;

	p[1] = status;
	p[3] = 12;

	p = buf + 4;

	struct cdtoc *td = NULL;
	for (trk = 0; trk <= cdu->tracks; trk++) {
		td = &cdu->toc[trk];
		if (pos < td->address) {
			if (trk > 0)
				td--;
			break;
		}
		if (pos >= td->address && pos < td[1].address)
			break;
	}
	if (!td)
		return 0;
	getsub (subbuf, cdu, td, pos);
	memcpy (p, subbuf + 12, 12);
//	write_log (L"%6d %02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x\n",
//		pos, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
	return 1;
}

static uae_u32 command_volume (int unitnum, uae_u16 volume_left, uae_u16 volume_right)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return -1;
	uae_u32 old = (cdu->cdda_volume[1] << 16) | (cdu->cdda_volume[0] << 0);
	cdu->cdda_volume[0] = volume_left;
	cdu->cdda_volume[1] = volume_right;
	return old;
}

static int command_rawread (int unitnum, uae_u8 *data, int sector, int size, int sectorsize, uae_u16 extra)
{
	int ret = 0;
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return 0;
	struct cdtoc *t = findtoc (cdu, &sector);
	int offset;

	if (!t || t->handle == NULL)
		return 0;
	cdda_stop (cdu);
	if (sectorsize > 0) {
		if (sectorsize > t->size)
			return 0;
		offset = 0;
		if (sectorsize == 2336 && t->size == 2352)
			offset = 16;
		zfile_fseek (t->handle, t->offset + sector * t->size + offset, SEEK_SET);
		zfile_fread (data, sectorsize, size, t->handle);
		cdu->cd_last_pos = sector;
		ret = sectorsize * size;
	} else {
		uae_u8 cmd9 = extra >> 8;
		int sync = (cmd9 >> 7) & 1;
		int headercodes = (cmd9 >> 5) & 3;
		int userdata = (cmd9 >> 4) & 1;
		int edcecc = (cmd9 >> 3) & 1;
		int errorfield = (cmd9 >> 1) & 3;
		uae_u8 subs = extra & 7;
		if (subs != 0 && subs != 1 && subs != 2 && subs != 4)
			return -1;

		if (isaudiotrack (&cdu->di.toc, sector)) {
			if (t->size != 2352)
				return -2;
			for (int i = 0; i < size; i++) {
				zfile_fseek (t->handle, t->offset + sector * t->size, SEEK_SET);
				zfile_fread (data, t->size, 1, t->handle);
				uae_u8 *p = data + t->size;
				if (subs) {
					uae_u8 subdata[SUB_CHANNEL_SIZE];
					getsub (subdata, cdu, t, sector);
					if (subs == 4) { // all, de-interleaved
						memcpy (p, subdata, SUB_CHANNEL_SIZE);
						p += SUB_CHANNEL_SIZE;
					} else if (subs == 2) { // q-only
						memcpy (p, subdata + SUB_ENTRY_SIZE, SUB_ENTRY_SIZE);
						p += SUB_ENTRY_SIZE;
					} else if (subs == 1) { // all, interleaved
						sub_to_interleaved (subdata, p);
						p += SUB_CHANNEL_SIZE;
					}
				}
				ret += p - data;
				data = p;
				sector++;
			}
		}
	}
	return ret;
}

static int command_read (int unitnum, uae_u8 *data, int sector, int size)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return 0;

	struct cdtoc *t = findtoc (cdu, &sector);
	int offset;

	if (!t || t->handle == NULL)
		return NULL;
	cdda_stop (cdu);
	offset = 0;
	if (t->size > 2048)
		offset = 16;
	zfile_fseek (t->handle, t->offset + sector * t->size + offset, SEEK_SET);
	zfile_fread (data, size, 2048, t->handle);
	cdu->cd_last_pos = sector;
	return 1;
}

static int command_toc (int unitnum, struct cd_toc_head *th)
{
	struct cdunit *cdu = unitisopen (unitnum);
	if (!cdu)
		return 0;

	int i;

	memset (&cdu->di.toc, 0, sizeof (struct cd_toc_head));
	if (!cdu->tracks)
		return 0;

	memset (th, 0, sizeof (struct cd_toc_head));
	struct cd_toc *toc = &th->toc[0];
	th->first_track = 1;
	th->last_track = cdu->tracks;
	th->points = cdu->tracks + 3;
	th->tracks = cdu->tracks;
	th->lastaddress = cdu->toc[cdu->tracks].address;

	toc->adr = 1;
	toc->point = 0xa0;
	toc->track = th->first_track;
	toc++;

	th->first_track_offset = 1;
	for (i = 0; i < cdu->tracks; i++) {
		toc->adr = cdu->toc[i].adr;
		toc->control = cdu->toc[i].ctrl;
		toc->track = i + 1;
		toc->point = i + 1;
		toc->paddress = cdu->toc[i].address;
		toc++;
	}

	th->last_track_offset = cdu->tracks;
	toc->adr = 1;
	toc->point = 0xa1;
	toc->track = th->last_track;
	toc++;

	toc->adr = 1;
	toc->point = 0xa2;
	toc->paddress = th->lastaddress;
	toc++;

	memcpy (&cdu->di.toc, th, sizeof (struct cd_toc_head));
	gui_flicker_led (LED_CD, unitnum, 1);
	return 1;
}

static void skipspace (TCHAR **s)
{
	while (_istspace (**s))
		(*s)++;
}
static void skipnspace (TCHAR **s)
{
	while (!_istspace (**s))
		(*s)++;
}

static TCHAR *nextstring (TCHAR **sp)
{
	TCHAR *s;
	TCHAR *out = NULL;

	skipspace (sp);
	s = *sp;
	if (*s == '\"') {
		s++;
		out = s;
		while (*s && *s != '\"')
			s++;
		*s++ = 0;
	} else if (*s) {
		out = s;
		skipnspace (&s);
		*s++ = 0;
	}
	*sp = s;
	return out;
}

static int readval (const TCHAR *s)
{
	int base = 10;
	TCHAR *endptr;
	if (s[0] == '0' && _totupper (s[1]) == 'X')
		s += 2, base = 16;
	return _tcstol (s, &endptr, base);
}

static int parseccd (struct cdunit *cdu, struct zfile *zcue, const TCHAR *img)
{
	int mode;
	int num, tracknum, trackmode;
	int adr, control, lba;
	bool gotlba;
	struct cdtoc *t;
	struct zfile *zimg, *zsub;
	TCHAR fname[MAX_DPATH];
	
	write_log (L"CCD TOC: '%s'\n", img);
	_tcscpy (fname, img);
	TCHAR *ext = _tcsrchr (fname, '.');
	if (ext)
		*ext = 0;
	_tcscat (fname, L".img");
	zimg = zfile_fopen (fname, L"rb", ZFD_NORMAL);
	if (!zimg) {
		write_log (L"CCD: can't open '%s'\n", fname);
		//return 0;
	}
	ext = _tcsrchr (fname, '.');
	if (ext)
		*ext = 0;
	_tcscat (fname, L".sub");
	zsub = zfile_fopen (fname, L"rb", ZFD_NORMAL);
	if (zsub)
		write_log (L"CCD: '%s' detected\n", fname);

	num = -1;
	mode = -1;
	for (;;) {
		TCHAR buf[MAX_DPATH], *p;
		if (!zfile_fgets (buf, sizeof buf / sizeof (TCHAR), zcue))
			break;
		p = buf;
		skipspace (&p);
		if (!_tcsnicmp (p, L"[DISC]", 6)) {
			mode = 1;
		} else if (!_tcsnicmp (p, L"[ENTRY ", 7)) {
			t = NULL;
			mode = 2;
			num = readval (p + 7);
			if (num < 0)
				break;
			adr = control = -1;
			gotlba = false;
		} else if (!_tcsnicmp (p, L"[TRACK ", 7)) {
			mode = 3;
			tracknum = readval (p + 7);
			trackmode = -1;
			if (tracknum <= 0 || tracknum > 99)
				break;
			t = &cdu->toc[tracknum - 1];
		}
		if (mode < 0)
			continue;
		if (mode == 1) {
			if (!_tcsnicmp (p, L"TocEntries=", 11)) {
				cdu->tracks = readval (p + 11) - 3;
				if (cdu->tracks <= 0 || cdu->tracks > 99)
					break;
			}
			continue;
		}
		if (cdu->tracks <= 0)
			break;
		
		if (mode == 2) {

			if (!_tcsnicmp (p, L"SESSION=", 8)) {
				if (readval (p + 8) != 1)
					mode = -1;
				continue;
			} else if (!_tcsnicmp (p, L"POINT=", 6)) {
				tracknum = readval (p + 6);
				if (tracknum <= 0)
					break;
				if (tracknum >= 0xa0 && tracknum != 0xa2) {
					mode = -1;
					continue;
				}
				if (tracknum == 0xa2)
					tracknum = cdu->tracks + 1;
				t = &cdu->toc[tracknum - 1];
				continue;
			}
			if (!_tcsnicmp (p, L"ADR=", 4))
				adr = readval (p + 4);
			if (!_tcsnicmp (p, L"CONTROL=", 8))
				control = readval (p + 8);
			if (!_tcsnicmp (p, L"PLBA=", 5)) {
				lba = readval (p + 5);
				gotlba = true;
			}
			if (gotlba && adr >= 0 && control >= 0) {
				t->adr = adr;
				t->ctrl = control;
				t->address = lba;
				t->offset = 0;
				t->size = 2352;
				t->offset = lba * t->size;
				t->track = tracknum;
				if (zsub) {
					t->subcode = 2;
					t->subhandle = zfile_dup (zsub);
					t->suboffset = 0;
				}
				if (zimg) {
					t->handle = zfile_dup (zimg);
					t->fname = my_strdup (zfile_getname (zimg));
					t->filesize = zfile_size (t->handle);
				}
				mode = -1;
			}

		} else if (mode == 3) {

			if (!_tcsnicmp (p, L"MODE=", 5))
				trackmode = _tstol (p + 5);
			if (trackmode < 0 || trackmode > 2)
				continue;
			
		}

	}
	return cdu->tracks;
}

static int parsecue (struct cdunit *cdu, struct zfile *zcue, const TCHAR *img)
{
	int tracknum, index0, pregap;
	int offset, secoffset, newfile;
	TCHAR *fname, *fnametype;
	int ctrl;
	mp3decoder *mp3dec = NULL;

	fname = NULL;
	fnametype = NULL;
	tracknum = 0;
	offset = 0;
	secoffset = 0;
	newfile = 0;
	ctrl = 0;
	index0 = -1;
	pregap = 0;

	write_log (L"CUE TOC: '%s'\n", img);
	for (;;) {
		TCHAR buf[MAX_DPATH], *p;
		if (!zfile_fgets (buf, sizeof buf / sizeof (TCHAR), zcue))
			break;

		p = buf;
		skipspace (&p);

		if (!_tcsnicmp (p, L"FILE", 4)) {
			p += 4;
			xfree (fname);
			fname = my_strdup (nextstring (&p));
			fnametype = nextstring (&p);
			if (!fnametype)
				break;
			if (_tcsicmp (fnametype, L"BINARY") && _tcsicmp (fnametype, L"WAVE") && _tcsicmp (fnametype, L"MP3")) {
				write_log (L"CUE: unknown file type '%s' ('%s')\n", fnametype, fname);
			}
			offset = 0;
			newfile = 1;
			ctrl = 0;
		} else if (!_tcsnicmp (p, L"FLAGS", 5)) {
			ctrl &= ~(1 | 2 | 8);
			for (;;) {
				TCHAR *f = nextstring (&p);
				if (!f)
					break;
				if (!_tcsicmp (f, L"PRE"))
					ctrl |= 1;
				if (!_tcsicmp (f, L"DCP"))
					ctrl |= 2;
				if (!_tcsicmp (f, L"4CH"))
					ctrl |= 8;
			}
		} else if (!_tcsnicmp (p, L"TRACK", 5)) {
			int size;
			TCHAR *tracktype;
			
			p += 5;
			//pregap = 0;
			index0 = -1;
			tracknum = _tstoi (nextstring (&p));
			tracktype = nextstring (&p);
			if (!tracktype)
				break;
			size = 2352;
			if (!_tcsicmp (tracktype, L"AUDIO")) {
				ctrl &= ~4;
			} else {
				ctrl |= 4;
				if (!_tcsicmp (tracktype, L"MODE1/2048"))
					size = 2048;
				else if (!_tcsicmp (tracktype, L"MODE1/2352"))
					size = 2352;
				else if (!_tcsicmp (tracktype, L"MODE2/2336") || !_tcsicmp (tracktype, L"CDI/2336"))
					size = 2336;
				else if (!_tcsicmp (tracktype, L"MODE2/2352") || !_tcsicmp (tracktype, L"CDI/2352"))
					size = 2352;
				else {
					write_log (L"CUE: unknown tracktype '%s' ('%s')\n", tracktype, fname);
				}
			}
			if (tracknum >= 1 && tracknum <= 99) {
				struct cdtoc *t = &cdu->toc[tracknum - 1];
				struct zfile *ztrack;

				if (tracknum > 1 && newfile) {
					t--;
					secoffset += t->filesize / t->size;
					t++;
				}

				newfile = 0;
				ztrack = zfile_fopen (fname, L"rb", ZFD_ARCHIVE);
				if (!ztrack) {
					TCHAR tmp[MAX_DPATH];
					_tcscpy (tmp, fname);
					p = tmp + _tcslen (tmp);
					while (p > tmp) {
						if (*p == '/' || *p == '\\') {
							ztrack = zfile_fopen (p + 1, L"rb", ZFD_ARCHIVE);
							if (ztrack) {
								xfree (fname);
								fname = my_strdup (p + 1);
							}
							break;
						}
						p--;
					}
				}
				if (!ztrack) {
					TCHAR tmp[MAX_DPATH];
					TCHAR *s2;
					_tcscpy (tmp, zfile_getname (zcue));
					s2 = _tcsrchr (tmp, '\\');
					if (!s2)
						s2 = _tcsrchr (tmp, '/');
					if (s2) {
						s2[0] = 0;
						_tcscat (tmp, L"\\");
						_tcscat (tmp, fname);
						ztrack = zfile_fopen (tmp, L"rb", ZFD_ARCHIVE);
					}
				}
				t->track = tracknum;
				t->ctrl = ctrl;
				t->adr = 1;
				t->handle = ztrack;
				t->size = size;
				t->fname = my_strdup (fname);
				if (tracknum > cdu->tracks)
					cdu->tracks = tracknum;
				if (t->handle)
					t->filesize = zfile_size (t->handle);
			}
		} else if (!_tcsnicmp (p, L"PREGAP", 6)) {
			TCHAR *tt;
			int tn;
			p += 6;
			tt = nextstring (&p);
			tn = _tstoi (tt) * 60 * 75;
			tn += _tstoi (tt + 3) * 75;
			tn += _tstoi (tt + 6);
			pregap += tn;
		} else if (!_tcsnicmp (p, L"INDEX", 5)) {
			int idxnum;
			int tn = 0;
			TCHAR *tt;
			p += 5;
			idxnum = _tstoi (nextstring (&p));
			tt = nextstring (&p);
			tn = _tstoi (tt) * 60 * 75;
			tn += _tstoi (tt + 3) * 75;
			tn += _tstoi (tt + 6);
			if (idxnum == 0) {
				index0 = tn;
			} else if (idxnum == 1 && tracknum >= 1 && tracknum <= 99) {
				struct cdtoc *t = &cdu->toc[tracknum - 1];
				if (!t->address) {
					t->address = tn + secoffset;
					t->address += pregap;
					if (tracknum > 1) {
						offset += t->address - t[-1].address;
					} else {
						offset += t->address;
					}
					if (!secoffset)
						t->offset = offset * t->size;
					if (!_tcsicmp (fnametype, L"WAVE") && t->handle) {
						struct zfile *zf = t->handle;
						uae_u8 buf[16] = { 0 };
						zfile_fread (buf, 12, 1, zf);
						if (!memcmp (buf, "RIFF", 4) && !memcmp (buf + 8, "WAVE", 4)) {
							int size;
							for (;;) {
								memset (buf, 0, sizeof buf);
								if (zfile_fread (buf, 8, 1, zf) != 1)
									break;
								size = (buf[4] << 0) | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
								if (!memcmp (buf, "data", 4))
									break;
								if (size <= 0)
									break;
								zfile_fseek (zf, size, SEEK_CUR);
							}
							t->offset += zfile_ftell (zf);
							t->filesize = size;
						}
					} else if (!_tcsicmp (fnametype, L"MP3") && t->handle) {
						if (!mp3dec) {
							try {
								mp3dec = new mp3decoder();
							} catch (exception) { }
						}
						if (mp3dec) {
							t->offset = 0;
							t->filesize = mp3dec->getsize (t->handle);
							if (t->filesize)
								t->mp3 = 1;
						}
					}
				}
			}
		}
	}

	struct cdtoc *t = &cdu->toc[cdu->tracks - 1];
	int size = t->filesize;
	if (!secoffset)
		size -= offset * t->size;
	if (size < 0)
		size = 0;
	cdu->toc[cdu->tracks].address = t->address + size / t->size;

	xfree (fname);

	delete mp3dec;

	return cdu->tracks;
}

static int parse_image (struct cdunit *cdu, const TCHAR *img)
{
	struct zfile *zcue;
	int i;
	const TCHAR *ext;
	int secoffset;

	secoffset = 0;
	cdu->tracks = 0;
	if (!img)
		return 0;
	zcue = zfile_fopen (img, L"rb", ZFD_ARCHIVE | ZFD_CD);
	if (!zcue)
		return 0;

	ext = _tcsrchr (img, '.');
	if (ext) {
		TCHAR curdir[MAX_DPATH];
		TCHAR oldcurdir[MAX_DPATH], *p;

		ext++;
		oldcurdir[0] = 0;
		_tcscpy (curdir, img);
		p = curdir + _tcslen (curdir);
		while (p > curdir) {
			if (*p == '/' || *p == '\\')
				break;
			p--;
		}
		*p = 0;
		if (p > curdir)
			my_setcurrentdir (curdir, oldcurdir);

		if (!_tcsicmp (ext, L"cue"))
			parsecue (cdu, zcue, img);
		else if (!_tcsicmp (ext, L"ccd"))
			parseccd (cdu, zcue, img);

		if (oldcurdir[0])
			my_setcurrentdir (oldcurdir, NULL);
	}
	if (!cdu->tracks) {
		uae_u64 siz = zfile_size (zcue);
		if (siz >= 16384 && (siz % 2048) == 0 || (siz % 2352) == 0) {
			struct cdtoc *t = &cdu->toc[0];
			cdu->tracks = 1;
			t->ctrl = 4;
			t->adr = 1;
			t->fname = my_strdup (img);
			t->handle = zcue;
			t->size = (siz % 2048) == 0 ? 2048 : 2352;
			t->filesize = siz;
			write_log (L"CUE: plain CD image mounted!\n");
			cdu->toc[1].address = t->address + t->filesize / t->size;
			zcue = NULL;
		}
	}

	for (i = 0; i <= cdu->tracks; i++) {
		struct cdtoc *t = &cdu->toc[i];
		uae_u32 msf = lsn2msf (t->address);
		if (i < cdu->tracks)
			write_log (L"%2d: ", i + 1);
		else
			write_log (L"    ");
		write_log (L"%7d %02d:%02d:%02d",
			t->address, (msf >> 16) & 0xff, (msf >> 8) & 0xff, (msf >> 0) & 0xff);
		if (i < cdu->tracks)
			write_log (L" %s %x %10d %s", (t->ctrl & 4) ? L"DATA    " : (t->subcode ? L"CDA+SUB" : L"CDA     "),
				t->ctrl, t->offset, t->handle == NULL ? L"[FILE ERROR]" : L"");
		write_log (L"\n");
		if (i < cdu->tracks)
			write_log (L" - %s\n", t->fname);
	}

	cdu->blocksize = 2048;
	cdu->cdsize = cdu->toc[cdu->tracks].address * cdu->blocksize;

	zfile_fclose (zcue);
	return 1;
}

static void unload_image (struct cdunit *cdu)
{
	int i;

	for (i = 0; i < cdu->tracks; i++) {
		struct cdtoc *t = &cdu->toc[i];
		zfile_fclose (t->handle);
		if (t->handle != t->subhandle)
			zfile_fclose (t->subhandle);
		xfree (t->fname);
		xfree (t->data);
		xfree (t->subdata);
	}
	memset (cdu->toc, 0, sizeof cdu->toc);
	cdu->tracks = 0;
	cdu->cdsize = 0;
}


static int open_device (int unitnum, const TCHAR *ident)
{
	struct cdunit *cdu = &cdunits[unitnum];

	if (cdu->open)
		return 0;
	uae_sem_init (&cdu->sub_sem, 0, 1);
	parse_image (cdu, ident);
	cdu->open = true;
	cdu->enabled = true;
	cdu->cdda_volume[0] = 0x7fff;
	cdu->cdda_volume[1] = 0x7fff;
#ifdef RETROPLATFORM
	rp_cd_change (unitnum, 0);
	rp_cd_image_change (unitnum, currprefs.cdimagefile[unitnum]);
#endif
	return 1;
}

static void close_device (int unitnum)
{
	struct cdunit *cdu = &cdunits[unitnum];
	if (cdu->open == false)
		return;
	cdda_stop (cdu);
	unload_image (cdu);
	uae_sem_destroy (&cdu->sub_sem);
	cdu->open = false;
	cdu->enabled = false;
#ifdef RETROPLATFORM
	rp_cd_change (unitnum, 1);
	rp_cd_image_change (unitnum, currprefs.cdimagefile[unitnum]);
#endif
}

static int ismedia (int unitnum, int quick)
{
	struct cdunit *cdu = &cdunits[unitnum];
	if (!cdu->enabled)
		return -1;
	return cdu->tracks > 0 ? 1 : 0;
}

static struct device_info *info_device (int unitnum, struct device_info *di, int quick)
{
	struct cdunit *cdu = &cdunits[unitnum];
	memset (di, 0, sizeof (struct device_info));
	if (!cdu->enabled)
		return 0;
	di->open = cdu->open;
	di->removable = 1;
	di->bus = unitnum;
	di->target = 0;
	di->lun = 0;
	di->media_inserted = 0;
	di->bytespersector = 2048;
	di->mediapath[0] = 0;
	di->cylinders = 1;
	di->trackspercylinder = 1;
	di->sectorspertrack = cdu->cdsize / di->bytespersector;
	if (ismedia (unitnum, 1)) {
		di->media_inserted = 1;
		_tcscpy (di->mediapath, currprefs.cdimagefile[0]);
	}
	memset (&di->toc, 0, sizeof (struct cd_toc_head));
	command_toc (unitnum, &di->toc);
	di->write_protected = 1;
	di->type = INQ_ROMD;
	di->unitnum = unitnum + 1;
	_tcscpy (di->label, L"CDEMU");
	return di;
}

static void close_bus (void)
{
	if (!bus_open) {
		write_log (L"IMAGE close_bus() when already closed!\n");
		return;
	}
	bus_open = 0;
	write_log (L"IMAGE driver closed.\n");
}

static int open_bus (int flags)
{
	if (bus_open) {
		write_log (L"IOCTL open_bus() more than once!\n");
		return 1;
	}
	bus_open = 1;
	write_log (L"Image driver open.\n");
	return 1;
}

struct device_functions devicefunc_cdimage = {
	L"IMAGE",
	open_bus, close_bus, open_device, close_device, info_device,
	0, 0, 0,
	command_pause, command_stop, command_play, command_volume, command_qcode,
	command_toc, command_read, command_rawread, 0,
	0, ismedia
};
