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
	int filesize;
	uae_u8 *data;
	TCHAR *fname;
	int address;
	uae_u8 adr, ctrl;
	int track;
	int size;
	int mp3;
};

static uae_u8 buffer[2352];
static struct cdtoc toc[102];
static int tracks;

static int cdda_play_finished;
static int cdda_play;
static int cdda_paused;
static int cdda_volume;
static int cdda_volume_main;
static uae_u32 cd_last_pos;
static int cdda_start, cdda_end;

/* convert minutes, seconds and frames -> logical sector number */
static int msf2lsn (int	msf)
{
	int sector = (((msf >> 16) & 0xff) * 60 * 75 + ((msf >> 8) & 0xff) * 75 + ((msf >> 0) & 0xff));
	return sector - 150;
}

/* convert logical sector number -> minutes, seconds and frames */
static int lsn2msf (int	sectors)
{
	int msf;
	sectors += 150;
	msf = (sectors / (75 * 60)) << 16;
	msf |= ((sectors / 75) % 60) << 8;
	msf |= (sectors % 75) << 0;
	return msf;
}

static struct cdtoc *findtoc (int *sectorp)
{
	int i;
	int sector;

	sector = *sectorp;
	for (i = 0; i <= tracks; i++) {
		struct cdtoc *t = &toc[i];
		if (t->address > sector) {
			if (i == 0)
				return NULL;
			t--;
			sector -= t->address;
			*sectorp = sector;
			return t;
		}
	}
	return NULL;
}


static int mp3_bitrates[] = {
  0,  32,  64,  96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1,
  0,  32,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, -1,
  0,  32,  40,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, -1,
  0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, -1,
  0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1
};
static int mp3_frequencies[] = {
	44100, 48000, 32000, 0,
	22050, 24000, 16000, 0,
	11025, 12000,  8000, 0
};
static int mp3_samplesperframe[] = {
	 384,  384,  384,
	1152, 1152, 1152,
	1152,  576,  576
};

static uae_u32 mp3decoder_getsize (struct zfile *zf)
{
	uae_u32 size;
	int frames;

	frames = 0;
	size = 0;
	for (;;) {
		int ver, layer, bitrate, freq, padding, bitindex, iscrc;
		int samplerate, framelen, bitrateidx, channelmode;
		int isstereo;
		uae_u8 header[4];

		if (zfile_fread (header, sizeof header, 1, zf) != 1)
			return size;
		if (header[0] != 0xff || ((header[1] & (0x80 | 0x40 | 0x20)) != (0x80 | 0x40 | 0x20))) {
			zfile_fseek (zf, -3, SEEK_CUR);
			continue;
		}
		ver = (header[1] >> 3) & 3;
		if (ver == 1)
			return 0;
		if (ver == 0)
			ver = 2;
		else if (ver == 2)
			ver = 1;
		else if (ver == 3)
			ver = 0;
		layer = 4 - ((header[1] >> 1) & 3);
		if (layer == 4)
			return 0;
		iscrc = ((header[1] >> 0) & 1) ? 0 : 2;
		bitrateidx = (header[2] >> 4) & 15;
		freq = mp3_frequencies[(header[2] >> 2) & 3];
		if (!freq)
			return 0;
		channelmode = (header[3] >> 6) & 3;
		isstereo = channelmode != 3;
		if (ver == 0) {
			bitindex = layer - 1;
		} else {
			if (layer == 1)
				bitindex = 3;
			else
				bitindex = 4;
		}
		bitrate = mp3_bitrates[bitindex * 16 + bitrateidx] * 1000;
		if (bitrate <= 0)
			return 0;
		padding = (header[2] >> 1) & 1;
		samplerate = mp3_samplesperframe[(layer - 1) * 3 + ver];
		framelen = ((samplerate / 8 * bitrate) / freq) + padding;
		if (framelen <= 4)
			return 0;
		zfile_fseek (zf, framelen + iscrc - 4, SEEK_CUR);
		frames++;
		size += samplerate * 2 * (isstereo ? 2 : 1);
	}
	return size;
}

#ifdef _WIN32

#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

#define MP3_BLOCK_SIZE 522

static HACMSTREAM g_mp3stream = NULL;

static void mp3decoder_close (void)
{
	if (g_mp3stream)
		acmStreamClose (g_mp3stream, 0);
	g_mp3stream = NULL;
}

static int mp3decoder_open (void)
{
	MMRESULT mmr;
	LPWAVEFORMATEX waveFormat;
	LPMPEGLAYER3WAVEFORMAT mp3format;
	DWORD maxFormatSize;

	if (g_mp3stream)
		return 1;
	
	// find the biggest format size
	maxFormatSize = 0;
	mmr = acmMetrics (NULL, ACM_METRIC_MAX_SIZE_FORMAT, &maxFormatSize);
  
	// define desired output format
	waveFormat = (LPWAVEFORMATEX) LocalAlloc( LPTR, maxFormatSize );
	waveFormat->wFormatTag = WAVE_FORMAT_PCM;
	waveFormat->nChannels = 2; // stereo
	waveFormat->nSamplesPerSec = 44100; // 44.1kHz
	waveFormat->wBitsPerSample = 16; // 16 bits
	waveFormat->nBlockAlign = 4; // 4 bytes of data at a time are useful (1 sample)
	waveFormat->nAvgBytesPerSec = 4 * 44100; // byte-rate
	waveFormat->cbSize = 0; // no more data to follow
  
	// define MP3 input format
	mp3format = (LPMPEGLAYER3WAVEFORMAT) LocalAlloc( LPTR, maxFormatSize );
	mp3format->wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
	mp3format->wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
	mp3format->wfx.nChannels = 2;
	mp3format->wfx.nAvgBytesPerSec = 128 * (1024 / 8);  // not really used but must be one of 64, 96, 112, 128, 160kbps
	mp3format->wfx.wBitsPerSample = 0;                  // MUST BE ZERO
	mp3format->wfx.nBlockAlign = 1;                     // MUST BE ONE
	mp3format->wfx.nSamplesPerSec = 44100;              // 44.1kHz
	mp3format->fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
	mp3format->nBlockSize = MP3_BLOCK_SIZE;             // voodoo value #1
	mp3format->nFramesPerBlock = 1;                     // MUST BE ONE
	mp3format->nCodecDelay = 1393;                      // voodoo value #2
	mp3format->wID = MPEGLAYER3_ID_MPEG;
  
	mmr = acmStreamOpen( &g_mp3stream,               // open an ACM conversion stream
  		     NULL,                       // querying all ACM drivers
  		     (LPWAVEFORMATEX) mp3format, // converting from MP3
  		     waveFormat,                 // to WAV
  		     NULL,                       // with no filter
  		     0,                          // or async callbacks
  		     0,                          // (and no data for the callback)
  		     0                           // and no flags
  		     );
  
	LocalFree (mp3format);
	LocalFree (waveFormat);
	if (mmr == MMSYSERR_NOERROR)
		return 1;
	write_log (L"CUEMP3: couldn't open ACM mp3 decoder, %d\n", mmr);
	return 0;
}

static uae_u8 *mp3decoder_get (struct zfile *zf, int maxsize)
{
	MMRESULT mmr;
	unsigned long rawbufsize = 0;
	LPBYTE mp3buf;
	LPBYTE rawbuf;
	uae_u8 *outbuf = NULL;
	int outoffset = 0;
	ACMSTREAMHEADER mp3streamHead;

	write_log (L"CUEMP3: decoding '%s'..\n", zfile_getname (zf));
	mmr = acmStreamSize (g_mp3stream, MP3_BLOCK_SIZE, &rawbufsize, ACM_STREAMSIZEF_SOURCE);
	if (mmr != MMSYSERR_NOERROR) {
		write_log (L"CUEMP3: acmStreamSize, %d\n", mmr);
		return NULL;
	}
	// allocate our I/O buffers
	mp3buf = (LPBYTE) LocalAlloc (LPTR, MP3_BLOCK_SIZE);
	rawbuf = (LPBYTE) LocalAlloc (LPTR, rawbufsize);
  
	// prepare the decoder
	ZeroMemory (&mp3streamHead, sizeof (ACMSTREAMHEADER));
	mp3streamHead.cbStruct = sizeof (ACMSTREAMHEADER);
	mp3streamHead.pbSrc = mp3buf;
	mp3streamHead.cbSrcLength = MP3_BLOCK_SIZE;
	mp3streamHead.pbDst = rawbuf;
	mp3streamHead.cbDstLength = rawbufsize;
	mmr = acmStreamPrepareHeader (g_mp3stream, &mp3streamHead, 0);
	if (mmr != MMSYSERR_NOERROR) {
		write_log (L"CUEMP3: acmStreamPrepareHeader, %d\n", mmr);
		return NULL;
	}
	zfile_fseek (zf, 0, SEEK_SET);
	outbuf = xcalloc (maxsize, 1);
	for (;;) {
		int count = zfile_fread (mp3buf, 1, MP3_BLOCK_SIZE, zf);
		if (count != MP3_BLOCK_SIZE)
			break;
		// convert the data
		mmr = acmStreamConvert (g_mp3stream, &mp3streamHead, ACM_STREAMCONVERTF_BLOCKALIGN);
		if (mmr != MMSYSERR_NOERROR) {
			write_log (L"CUEMP3: acmStreamConvert, %d\n", mmr);
			return NULL;
		}
		if (outoffset + mp3streamHead.cbDstLengthUsed > maxsize)
			break;
		memcpy (outbuf + outoffset, rawbuf, mp3streamHead.cbDstLengthUsed);
		outoffset += mp3streamHead.cbDstLengthUsed;
	}
	acmStreamUnprepareHeader (g_mp3stream, &mp3streamHead, 0);
	LocalFree (rawbuf);
	LocalFree (mp3buf);
	write_log (L"CUEMP3: unpacked size %d bytes\n", outoffset);
	return outbuf;
}

static HWAVEOUT cdda_wavehandle;

static void cdda_closewav (void)
{
	if (cdda_wavehandle != NULL)
		waveOutClose (cdda_wavehandle);
	cdda_wavehandle = NULL;
}

// DAE CDDA based on Larry Osterman's "Playing Audio CDs" blog series

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
	int volume, volume_main;
	int oldplay;

	for (i = 0; i < 2; i++) {
		memset (&whdr[i], 0, sizeof (WAVEHDR));
		whdr[i].dwFlags = WHDR_DONE;
	}

	while (cdda_play == 0)
		Sleep (10);
	oldplay = -1;

	p = xmalloc (2 * num_sectors * 4096);
	px[0] = p;
	px[1] = p + num_sectors * 4096;
	bufon[0] = bufon[1] = 0;
	bufnum = 0;
	buffered = 0;
	volume = -1;
	volume_main = -1;

	if (cdda_openwav ()) {

		for (i = 0; i < 2; i++) {
			memset (&whdr[i], 0, sizeof (WAVEHDR));
			whdr[i].dwBufferLength = 2352 * num_sectors;
			whdr[i].lpData = px[i];
			mmr = waveOutPrepareHeader (cdda_wavehandle, &whdr[i], sizeof (WAVEHDR));
			if (mmr != MMSYSERR_NOERROR) {
				write_log (L"CDDA: waveOutPrepareHeader %d:%d\n", i, mmr);
				goto end;
			}
			whdr[i].dwFlags |= WHDR_DONE;
		}

		while (cdda_play > 0) {

			if (oldplay != cdda_play) {
				struct cdtoc *t;
				int sector;
				sector = cdda_start;
				t = findtoc (&sector);
				if (!t)
					write_log (L"CDDA: illegal sector number %d\n", cdda_start);
				else
					write_log (L"CDDA: playing track %d (file '%s', offset %d, secoffset %d)\n",
						t->track, t->fname, t->offset, sector);
				cdda_pos = cdda_start;
				oldplay = cdda_play;
				if (t->mp3 && !t->data) {
					if (mp3decoder_open ()) {
						t->data = mp3decoder_get (t->handle, t->filesize);
					}
				}
			}

			while (!(whdr[bufnum].dwFlags & WHDR_DONE)) {
				Sleep (10);
				if (!cdda_play)
					goto end;
			}
			bufon[bufnum] = 0;

			if ((cdda_pos < cdda_end || cdda_end == 0xffffffff) && !cdda_paused && cdda_play) {
				struct cdtoc *t;
				int sector;

				sector = cdda_pos;
				t = findtoc (&sector);
				if (t && t->handle) {
					if (t->mp3) {
						if (t->data)
							memcpy (px[bufnum], t->data + sector * t->size + t->offset, t->size * num_sectors);
						else
							memset (px[bufnum], 0, t->size * num_sectors);
					} else {
						zfile_fseek (t->handle, sector * t->size + t->offset, SEEK_SET);
						if (zfile_fread (px[bufnum], t->size, num_sectors, t->handle) < num_sectors) {
							int i = num_sectors - 1;
							memset (px[bufnum], 0, t->size * num_sectors);
							while (i > 0) {
								if (zfile_fread (px[bufnum], t->size, i, t->handle) == i)
									break;
								i--;
							}
							if (i == 0)
								write_log (L"CDDA: read error, track %d (file '%s' offset %d secoffset %d)\n",
									t->track, t->fname, t->offset, sector);
						}
					}
				}
	
				bufon[bufnum] = 1;
				if (volume != cdda_volume || volume_main != currprefs.sound_volume) {
					int vol;
					volume = cdda_volume;
					volume_main = currprefs.sound_volume;
					vol = (100 - volume_main) * volume / 100;
					if (vol >= 0xffff)
						vol = 0xffff;
					waveOutSetVolume (cdda_wavehandle, vol | (vol << 16));
				}
				mmr = waveOutWrite (cdda_wavehandle, &whdr[bufnum], sizeof (WAVEHDR));
				if (mmr != MMSYSERR_NOERROR) {
					write_log (L"CDDA: waveOutWrite %d\n", mmr);
					break;
				}

				cdda_pos += num_sectors;
				if (cdda_pos >= cdda_end)
					cdda_play_finished = 1;
				cd_last_pos = cdda_pos;

			}


			if (bufon[0] == 0 && bufon[1] == 0) {
				while (!(whdr[0].dwFlags & WHDR_DONE) || !(whdr[1].dwFlags & WHDR_DONE))
					Sleep (10);
				while (cdda_paused && cdda_play > 0)
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
	cdda_play = 0;
	write_log (L"CDDA: thread killed\n");
	return NULL;
}

#endif

static void cdda_stop (void)
{
	if (cdda_play > 0) {
		cdda_play = -1;
		while (cdda_play) {
			Sleep (10);
		}
	}
	cdda_play_finished = 0;
	cdda_paused = 0;
}


static int command_pause (int unitnum, int paused)
{
	cdda_paused = paused;
	return 1;
}

static int command_stop (int unitnum)
{
	cdda_stop ();
	return 1;
}

static int command_play (int unitnum, uae_u32 start, uae_u32 end, int scan)
{
	cdda_paused = 0;
	cdda_play_finished = 0;
	cdda_start = msf2lsn (start);
	cdda_end = msf2lsn (end);
	if (!cdda_play)
		uae_start_thread (L"cdda_play", cdda_play_func, NULL, NULL);
	cdda_play++;
	return 1;
}

static uae_u8 *command_qcode (int unitnum)
{
	static uae_u8 buf[4 + 12];
	uae_u8 *p;
	int trk;
	int pos;
	int msf;
	int start, end;
	int status;

	memset (buf, 0, sizeof buf);
	p = buf;

	status = AUDIO_STATUS_NO_STATUS;
	if (cdda_play) {
		status = AUDIO_STATUS_IN_PROGRESS;
		if (cdda_paused)
			status = AUDIO_STATUS_PAUSED;
	} else if (cdda_play_finished) {
		status = AUDIO_STATUS_PLAY_COMPLETE;
	}
	pos = cd_last_pos;

	p[1] = status;
	p[3] = 12;

	p = buf + 4;

	if (pos >= 150)
		trk = 0;
	start = end = 0;
	for (trk = 0; trk <= tracks; trk++) {
		struct cdtoc *td = &toc[trk];
		if (pos < td->address)
			break;
		if (pos >= td->address && pos < td[1].address) {
			start = td->address;
			break;
		}
	}
	p[1] = (toc[trk].ctrl << 0) | (toc[trk].adr << 4);
	p[2] = trk + 1;
	p[3] = 1;
	msf = lsn2msf (pos);
	p[5] = (msf >> 16) & 0xff;
	p[6] = (msf >> 8) & 0xff;
	p[7] = (msf >> 0) & 0xff;
	pos -= start;
	if (pos < 0)
		pos = 0;
	msf = lsn2msf (pos);
	p[9] = (pos >> 16) & 0xff;
	p[10] = (pos >> 8) & 0xff;
	p[11] = (pos >> 0) & 0xff;

	return buf;
}

static void command_volume (int unitnum, uae_u16 volume)
{
	cdda_volume = volume;
}

uae_u8 *command_rawread (int unitnum, int sector, int sectorsize)
{
	cdda_stop ();
	return NULL;
}
static uae_u8 *command_read (int unitnum, int sector)
{
	struct cdtoc *t = findtoc (&sector);
	int offset;
	if (!t || t->handle == NULL)
		return NULL;
	cdda_stop ();
	offset = 0;
	if (t->size > 2048)
		offset = 16;
	zfile_fseek (t->handle, t->offset + sector * t->size + offset, SEEK_SET);
	zfile_fread (buffer, 2048, 1, t->handle);
	return buffer;
}

static uae_u8 *command_toc (int unitnum)
{
	static uae_u8 statictoc[11 * 102];
	uae_u8 *p = statictoc;
	int i;
	uae_u32 msf;

	cdda_stop ();
	if (!tracks)
		return NULL;
	p[0] = ((tracks + 4) * 11) >> 8;
	p[1] = ((tracks + 4) * 11) & 0xff;
	p[2] = 1;
	p[3] = tracks;
	p += 4;
	memset (p, 0, 11);
	p[0] = 1;
	p[1] = (toc[0].ctrl << 0) | (toc[0].adr << 4);
	p[3] = 0xa0;
	p[8] = 1;
	p += 11;
	memset (p, 0, 11);
	p[0] = 1;
	p[1] = 0x10;
	p[3] = 0xa1;
	p[8] = tracks;
	p += 11;
	memset (p, 0, 11);
	p[0] = 1;
	p[1] = 0x10;
	p[3] = 0xa2;
	msf = lsn2msf (toc[tracks].address);
	p[8] = msf >> 16;
	p[9] = msf >> 8;
	p[10] = msf >> 0;
	p += 11;
	for (i = 0; i < tracks; i++) {
		memset (p, 0, 11);
		p[0] = 1;
		p[1] = (toc[i].ctrl << 0) | (toc[i].adr << 4);
		p[2] = 0;
		p[3] = i + 1;
		msf = lsn2msf (toc[i].address);
		p[8] = msf >> 16;
		p[9] = msf >> 8;
		p[10] = msf >> 0;
		p += 11;
	}
	gui_flicker_led (LED_CD, unitnum, 1);
	return statictoc;
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

static int open_device (int unitnum)
{
	struct zfile *zcue;
	TCHAR *fname, *fnametype;
	int tracknum;
	int offset, secoffset, newfile;
	int i;
	TCHAR *p;
	TCHAR curdir[MAX_DPATH], oldcurdir[MAX_DPATH];
	uae_u64 siz;
	TCHAR *img = currprefs.cdimagefile;
	int ctrl;

	if (unitnum || !img)
		return 0;
	zcue = zfile_fopen (img, L"rb", 0);
	if (!zcue)
		return 0;

	fname = NULL;
	fnametype = NULL;
	tracknum = 0;
	offset = 0;
	secoffset = 0;
	newfile = 0;
	ctrl = 0;

	zfile_fseek (zcue, 0, SEEK_END);
	siz = zfile_ftell (zcue);
	zfile_fseek (zcue, 0, SEEK_SET);
	if (siz >= 16384) {
		if ((siz % 2048) == 0 || (siz % 2352) == 0) {
			struct cdtoc *t = &toc[0];
			tracks = 1;
			t->ctrl = 4;
			t->adr = 1;
			t->fname = my_strdup (img);
			t->handle = zcue;
			t->size = (siz % 2048) == 0 ? 2048 : 2352;
			write_log (L"CUE: plain CD image mounted!\n");
			zcue = NULL;
			goto isodone;
		}
		zfile_fclose (zcue);
		return 0;
	}

	write_log (L"CUE TOC: '%s'\n", img);
	_tcscpy (curdir, img);
	oldcurdir[0] = 0;
	p = curdir + _tcslen (curdir);
	while (p > curdir) {
		if (*p == '/' || *p == '\\')
			break;
		p--;
	}
	*p = 0;
	if (p > curdir)
		my_setcurrentdir (curdir, oldcurdir);
	for (;;) {
		TCHAR buf[MAX_DPATH];
		if (!zfile_fgets (buf, sizeof buf / sizeof (TCHAR), zcue))
			break;

		p = buf;
		skipspace (&p);

		if (!_tcsnicmp (p, L"FILE", 4)) {
			p += 4;
			xfree (fname);
			fname = my_strdup (nextstring (&p));
			fnametype = nextstring (&p);
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
			tracknum = _tstoi (nextstring (&p));
			tracktype = nextstring (&p);
			size = 2352;
			if (!_tcsicmp (tracktype, L"AUDIO")) {
				ctrl &= ~4;
			} else {
				ctrl |= 4;
				if (!_tcsicmp (tracktype, L"MODE1/2048"))
					size = 2048;
				else if (!_tcsicmp (tracktype, L"MODE1/2352"))
					size = 2352;
				else if (!_tcsicmp (tracktype, L"MODE2/2336"))
					size = 2336;
				else if (!_tcsicmp (tracktype, L"MODE2/2352"))
					size = 2352;
				else {
					write_log (L"CUE: unknown tracktype '%s' ('%s')\n", tracktype, fname);
				}
			}
			if (tracknum >= 1 && tracknum <= 99) {
				struct cdtoc *t = &toc[tracknum - 1];
				struct zfile *ztrack;

				if (tracknum > 1 && newfile) {
					t--;
					secoffset += t->filesize / t->size;
					t++;
				}

				newfile = 0;
				ztrack = zfile_fopen (fname, L"rb", 0);
				if (!ztrack) {
					TCHAR tmp[MAX_DPATH];
					_tcscpy (tmp, fname);
					p = tmp + _tcslen (tmp);
					while (p > tmp) {
						if (*p == '/' || *p == '\\') {
							ztrack = zfile_fopen (p + 1, L"rb", 0);
							if (ztrack) {
								xfree (fname);
								fname = my_strdup (p + 1);
							}
							break;
						}
						p--;
					}
				}
				t->track = tracknum;
				t->ctrl = ctrl;
				t->adr = 1;
				t->handle = ztrack;
				t->size = size;
				t->fname = my_strdup (fname);
				if (tracknum > tracks)
					tracks = tracknum;
				zfile_fseek (t->handle, 0, SEEK_END);
				t->filesize = zfile_ftell (t->handle);
				zfile_fseek (t->handle, 0, SEEK_SET);
			}
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
			if (tracknum >= 1 && tracknum <= 99) {
				struct cdtoc *t = &toc[tracknum - 1];
				if (!t->address) {
					t->address = tn + secoffset;
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
						t->offset = 0;
						t->filesize = mp3decoder_getsize (t->handle);
						if (t->filesize)
							t->mp3 = 1;
					}
				}
			}
		}
	}
isodone:
	if (tracks && toc[tracks - 1].handle) {
		struct cdtoc *t = &toc[tracks - 1];
		int size = t->filesize - offset * t->size;
		if (size < 0)
			size = 0;
		toc[tracks].address = toc[tracks - 1].address + size / t->size;
	}
	xfree (fname);
	if (oldcurdir[0])
		my_setcurrentdir (oldcurdir, NULL);
	for (i = 0; i <= tracks; i++) {
		struct cdtoc *t = &toc[i];
		uae_u32 msf = lsn2msf (t->address);
		if (i < tracks)
			write_log (L"%2d: ", i + 1);
		else
			write_log (L"    ");
		write_log (L"%7d %02d:%02d:%02d",
			t->address, (msf >> 16) & 0xff, (msf >> 8) & 0xff, (msf >> 0) & 0xff);
		if (i < tracks)
			write_log (L" %s %x %10d %s", (t->ctrl & 4) ? L"DATA" : L"CDA ", t->ctrl, t->offset, t->handle == NULL ? L"FILE ERROR" : L"");
		write_log (L"\n");
		if (i < tracks)
			write_log (L" - %s\n", t->fname);
	}
	zfile_fclose (zcue);
	mp3decoder_close ();
	return 1;
}
static void close_device (int unitnum)
{
	int i;

	for (i = 0; i < tracks; i++) {
		struct cdtoc *t = &toc[i];
		zfile_fclose (t->handle);
		xfree (t->fname);
		xfree (t->data);
	}
	memset (toc, 0, sizeof toc);
	tracks = 0;
}

static int ismedia (int unitnum, int quick)
{
	return currprefs.cdimagefile[0] ? 1 : 0;
}

static int open_bus (int flags)
{
	return ismedia (0, 1);
}

static void close_bus (void)
{
	mp3decoder_close ();
}

static struct device_info *info_device (int unitnum, struct device_info *di)
{
	if (unitnum)
		return 0;
	di->bus = unitnum;
	di->target = 0;
	di->lun = 0;
	di->media_inserted = 0;
	di->bytespersector = 2048;
	if (ismedia (unitnum, 1))
		di->media_inserted = 1;
	di->write_protected = 1;
	di->type = INQ_ROMD;
	di->id = 1;
	_tcscpy (di->label, L"IMG");
	return di;
}

struct device_functions devicefunc_cdimage = {
	open_bus, close_bus, open_device, close_device, info_device,
	0, 0, 0,
	command_pause, command_stop, command_play, command_volume, command_qcode,
	command_toc, command_read, command_rawread, 0,
	0, 0, ismedia
};
