
/*
* UAE
*
* mp3 decoder helper class
*
* Copyright 2010 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include <zfile.h>
#include <mp3decoder.h>

#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

#define MP3_BLOCK_SIZE 522

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

struct mpegaudio_header
{
	int ver;
	int layer;
	int bitrate;
	int freq;
	int padding;
	int iscrc;
	int samplerate;
	int channelmode;
	int modeext;
	int isstereo;
	int framesize;
	int firstframe;
};

static int get_header(struct zfile *zf, struct mpegaudio_header *head, bool keeplooking)
{
	for (;;) {
		int bitindex, bitrateidx;
		uae_u8 header[4];

		if (zfile_fread(header, sizeof header, 1, zf) != 1)
			return -1;
		if (header[0] != 0xff || ((header[1] & (0x80 | 0x40 | 0x20)) != (0x80 | 0x40 | 0x20))) {
			zfile_fseek(zf, -3, SEEK_CUR);
			if (keeplooking)
				continue;
			return 0;
		}
		if (head->firstframe < 0)
			head->firstframe = zfile_ftell32(zf);

		head->ver = (header[1] >> 3) & 3;
		if (head->ver == 1) {
			write_log(_T("MP3: ver==1?!\n"));
			return 0;
		}
		if (head->ver == 0)
			head->ver = 2;
		else if (head->ver == 2)
			head->ver = 1;
		else if (head->ver == 3)
			head->ver = 0;
		head->layer = 4 - ((header[1] >> 1) & 3);
		if (head->layer == 4) {
			write_log(_T("MP3: layer==4?!\n"));
			if (keeplooking)
				continue;
			return 0;
		}
		head->iscrc = ((header[1] >> 0) & 1) ? 0 : 2;
		bitrateidx = (header[2] >> 4) & 15;
		head->freq = mp3_frequencies[(header[2] >> 2) & 3];
		if (!head->freq) {
			write_log(_T("MP3: reserved frequency?!\n"));
			if (keeplooking)
				continue;
			return 0;
		}
		head->channelmode = (header[3] >> 6) & 3;
		head->modeext = (header[3] >> 4) & 3;
		head->isstereo = head->channelmode != 3;
		if (head->ver == 0) {
			bitindex = head->layer - 1;
		} else {
			if (head->layer == 1)
				bitindex = 3;
			else
				bitindex = 4;
		}
		head->bitrate = mp3_bitrates[bitindex * 16 + bitrateidx] * 1000;
		if (head->bitrate <= 0) {
			write_log(_T("MP3: reserved bitrate?!\n"));
			return 0;
		}
		head->padding = (header[2] >> 1) & 1;
		head->samplerate = mp3_samplesperframe[(head->layer - 1) * 3 + head->ver];
		switch (head->layer)
		{
		case 1:
			head->framesize = (12 * head->bitrate / head->freq + head->padding) * 4;
			break;
		case 2:
		case 3:
			head->framesize = 144 * head->bitrate / head->freq + head->padding;
			break;
		}
		if (head->framesize <= 4) {
			write_log(_T("MP3: too small frame size?!\n"));
			if (keeplooking)
				continue;
			return 0;
		}
		return 1;
	}
}

mp3decoder::~mp3decoder()
{
	if (g_mp3stream)
		acmStreamClose((HACMSTREAM)g_mp3stream, 0);
	g_mp3stream = NULL;
}

mp3decoder::mp3decoder(struct zfile *zf)
{
	MMRESULT mmr;
	LPWAVEFORMATEX waveFormat, inwave;
	LPMPEGLAYER3WAVEFORMAT mp3format = NULL;
	LPMPEG1WAVEFORMAT mp2format = NULL;
	DWORD maxFormatSize;
	struct mpegaudio_header head;

	if (get_header(zf, &head, true) <= 0) {
		write_log(_T("MPA: couldn't find mpeg audio header\n"));
		throw exception();
	}
	// find the biggest format size
	maxFormatSize = 0;
	mmr = acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &maxFormatSize);

	// define desired output format
	waveFormat = (LPWAVEFORMATEX)LocalAlloc(LPTR, maxFormatSize);
	waveFormat->wFormatTag = WAVE_FORMAT_PCM;
	waveFormat->nChannels = head.isstereo ? 2 : 1;
	waveFormat->nSamplesPerSec = 44100;
	waveFormat->wBitsPerSample = 16; // 16 bits
	waveFormat->nBlockAlign = 2 * waveFormat->nChannels;
	waveFormat->nAvgBytesPerSec = waveFormat->nBlockAlign * waveFormat->nSamplesPerSec; // byte-rate
	waveFormat->cbSize = 0; // no more data to follow

	if (head.layer == 3) {
		// define MP3 input format
		mp3format = (LPMPEGLAYER3WAVEFORMAT)LocalAlloc(LPTR, maxFormatSize);
		inwave = &mp3format->wfx;
		inwave->cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
		inwave->wFormatTag = WAVE_FORMAT_MPEGLAYER3;
		mp3format->fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
		mp3format->nBlockSize = MP3_BLOCK_SIZE;             // voodoo value #1
		mp3format->nFramesPerBlock = 1;                     // MUST BE ONE
		mp3format->nCodecDelay = 1393;                      // voodoo value #2
		mp3format->wID = MPEGLAYER3_ID_MPEG;
	} else {
		// There is no Windows MP2 ACM codec. This code is totally useless.
		mp2format = (LPMPEG1WAVEFORMAT)LocalAlloc(LPTR, maxFormatSize);
		inwave = &mp2format->wfx;
		mp2format->dwHeadBitrate = head.bitrate;
		mp2format->fwHeadMode = head.isstereo ? (head.channelmode == 1 ? ACM_MPEG_JOINTSTEREO : ACM_MPEG_STEREO) : ACM_MPEG_SINGLECHANNEL;
		mp2format->fwHeadLayer = head.layer == 1 ? ACM_MPEG_LAYER1 : ACM_MPEG_LAYER2;
		mp2format->fwHeadFlags = ACM_MPEG_ID_MPEG1;
		mp2format->fwHeadModeExt = 0x0f;
		mp2format->wHeadEmphasis = 1;
		inwave->cbSize = sizeof(MPEG1WAVEFORMAT) - sizeof(WAVEFORMATEX);
		inwave->wFormatTag = WAVE_FORMAT_MPEG;
	}
	inwave->nBlockAlign = 1;
	inwave->wBitsPerSample = 0;                  // MUST BE ZERO
	inwave->nChannels = head.isstereo ? 2 : 1;
	inwave->nSamplesPerSec = head.freq;
	inwave->nAvgBytesPerSec = head.bitrate / 8;

	mmr = acmStreamOpen((LPHACMSTREAM)&g_mp3stream,               // open an ACM conversion stream
		NULL,                       // querying all ACM drivers
		(LPWAVEFORMATEX)inwave,		// converting from MP3
		waveFormat,                 // to WAV
		NULL,                       // with no filter
		0,                          // or async callbacks
		0,                          // (and no data for the callback)
		0                           // and no flags
		);

	LocalFree(mp3format);
	LocalFree(mp2format);
	LocalFree(waveFormat);
	if (mmr != MMSYSERR_NOERROR) {
		write_log(_T("MP3: couldn't open ACM mp3 decoder, %d\n"), mmr);
		throw exception();
	}
}

mp3decoder::mp3decoder()
{
	MMRESULT mmr;
	LPWAVEFORMATEX waveFormat;
	LPMPEGLAYER3WAVEFORMAT mp3format;
	DWORD maxFormatSize;

	// find the biggest format size
	maxFormatSize = 0;
	mmr = acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &maxFormatSize);
  
	// define desired output format
	waveFormat = (LPWAVEFORMATEX)LocalAlloc(LPTR, maxFormatSize);
	waveFormat->wFormatTag = WAVE_FORMAT_PCM;
	waveFormat->nChannels = 2; // stereo
	waveFormat->nSamplesPerSec = 44100; // 44.1kHz
	waveFormat->wBitsPerSample = 16; // 16 bits
	waveFormat->nBlockAlign = 4; // 4 bytes of data at a time are useful (1 sample)
	waveFormat->nAvgBytesPerSec = 4 * 44100; // byte-rate
	waveFormat->cbSize = 0; // no more data to follow
  
	// define MP3 input format
	mp3format = (LPMPEGLAYER3WAVEFORMAT)LocalAlloc(LPTR, maxFormatSize);
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
  
	mmr = acmStreamOpen((LPHACMSTREAM)&g_mp3stream,               // open an ACM conversion stream
		 NULL,                       // querying all ACM drivers
		 (LPWAVEFORMATEX) mp3format, // converting from MP3
		 waveFormat,                 // to WAV
		 NULL,                       // with no filter
		 0,                          // or async callbacks
		 0,                          // (and no data for the callback)
		 0                           // and no flags
		 );
  
	LocalFree(mp3format);
	LocalFree(waveFormat);
	if (mmr != MMSYSERR_NOERROR) {
		write_log(_T("MP3: couldn't open ACM mp3 decoder, %d\n"), mmr);
		throw exception();
	}
}

uae_u8 *mp3decoder::get (struct zfile *zf, uae_u8 *outbuf, int maxsize)
{
	MMRESULT mmr;
	unsigned long rawbufsize = 0;
	LPBYTE mp3buf;
	LPBYTE rawbuf;
	int outoffset = 0;
	ACMSTREAMHEADER mp3streamHead;
	HACMSTREAM h = (HACMSTREAM)g_mp3stream;

	write_log(_T("MP3: decoding '%s'..\n"), zfile_getname(zf));
	mmr = acmStreamSize(h, MP3_BLOCK_SIZE, &rawbufsize, ACM_STREAMSIZEF_SOURCE);
	if (mmr != MMSYSERR_NOERROR) {
		write_log (_T("MP3: acmStreamSize, %d\n"), mmr);
		return NULL;
	}
	// allocate our I/O buffers
	mp3buf = (LPBYTE)LocalAlloc(LPTR, MP3_BLOCK_SIZE);
	rawbuf = (LPBYTE)LocalAlloc(LPTR, rawbufsize);
  
	// prepare the decoder
	ZeroMemory(&mp3streamHead, sizeof (ACMSTREAMHEADER));
	mp3streamHead.cbStruct = sizeof (ACMSTREAMHEADER);
	mp3streamHead.pbSrc = mp3buf;
	mp3streamHead.cbSrcLength = MP3_BLOCK_SIZE;
	mp3streamHead.pbDst = rawbuf;
	mp3streamHead.cbDstLength = rawbufsize;
	mmr = acmStreamPrepareHeader(h, &mp3streamHead, 0);
	if (mmr != MMSYSERR_NOERROR) {
		write_log(_T("MP3: acmStreamPrepareHeader, %d\n"), mmr);
		return NULL;
	}
	zfile_fseek(zf, 0, SEEK_SET);
	for (;;) {
		size_t count = zfile_fread(mp3buf, 1, MP3_BLOCK_SIZE, zf);
		if (count != MP3_BLOCK_SIZE)
			break;
		// convert the data
		mmr = acmStreamConvert(h, &mp3streamHead, ACM_STREAMCONVERTF_BLOCKALIGN);
		if (mmr != MMSYSERR_NOERROR) {
			write_log(_T("MP3: acmStreamConvert, %d\n"), mmr);
			return NULL;
		}
		if (outoffset + mp3streamHead.cbDstLengthUsed > maxsize)
			break;
		memcpy(outbuf + outoffset, rawbuf, mp3streamHead.cbDstLengthUsed);
		outoffset += mp3streamHead.cbDstLengthUsed;
	}
	acmStreamUnprepareHeader(h, &mp3streamHead, 0);
	LocalFree(rawbuf);
	LocalFree(mp3buf);
	write_log(_T("MP3: unpacked size %d bytes\n"), outoffset);
	return outbuf;
}

uae_u32 mp3decoder::getsize (struct zfile *zf)
{
	uae_u32 size;
	int frames, sameframes;
	int firstframe;
	int oldbitrate;
	int timelen = -1;

	firstframe = -1;
	oldbitrate = -1;
	sameframes = -1;
	frames = 0;
	size = 0;
	uae_u8 id3[10];

	if (zfile_fread(id3, sizeof id3, 1, zf) != 1)
		return 0;
	if (id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3' && id3[3] == 3 && id3[4] != 0xff && id3[6] < 0x80 && id3[7] < 0x80 && id3[8] < 0x80 && id3[9] < 0x80) {
		int unsync = id3[5] & 0x80;
		int exthead = id3[5] & 0x40;
		int len = (id3[9] << 0) | (id3[8] << 7) | (id3[7] << 14) | (id3[6] << 21);
		len &= 0x0fffffff;
		uae_u8 *tag = xmalloc (uae_u8, len + 1);
		if (zfile_fread (tag, len, 1, zf) != 1) {
			xfree (tag);
			return 0;
		}
		uae_u8 *p = tag;
		if (exthead) {
			int size = (p[4] << 21) | (p[5] << 14) | (p[6] << 7);
			size &= 0x0fffffff;
			p += size;
			len -= size;
		}
		while (len > 0) {
			int size = unsync ? (p[4] << 21) | (p[5] << 14) | (p[6] << 7) | (p[7] << 0) : (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | (p[7] << 0);
			size &= 0x0fffffff;
			if (size > len)
				break;
			int compr = p[9] & 0x80;
			int enc = p[9] & 0x40;
			if (compr == 0 && enc == 0) {
				if (!memcmp (p, "TLEN", 4)) {
					uae_u8 *data = p + 10;
					data[size] = 0;
					if (data[0] ==  0)
						timelen = atol ((char*)(data + 1));
					else
						timelen = _tstol ((wchar_t*)(data + 1));
				}
			}
			size += 10;
			p += size;
			len -= size;
		}
		xfree (tag);
	} else {
		zfile_fseek(zf, -(int)sizeof id3, SEEK_CUR);
	}

	for (;;) {
		struct mpegaudio_header mh;
		mh.firstframe = -1;
		int v = get_header(zf, &mh, true);
		if (v < 0)
			return size;
		zfile_fseek(zf, mh.framesize - 4, SEEK_CUR);
		frames++;
		if (timelen > 0) {
			size = ((uae_u64)timelen * mh.freq * 2 * (mh.isstereo ? 2 : 1)) / 1000;
			break;
		}
		size += mh.samplerate * 2 * (mh.isstereo ? 2 : 1);
		if (mh.bitrate != oldbitrate) {
			oldbitrate = mh.bitrate;
			sameframes++;
		}
		if (sameframes == 0 && frames > 100) {
			// assume this is CBR MP3
			size = mh.samplerate * 2 * (mh.isstereo ? 2 : 1) * ((zfile_size32(zf) - firstframe) / ((mh.samplerate / 8 * mh.bitrate) / mh.freq));
			break;
		}
	}
	return size;
}
