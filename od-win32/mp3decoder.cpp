
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

mp3decoder::~mp3decoder()
{
	if (g_mp3stream)
		acmStreamClose((HACMSTREAM)g_mp3stream, 0);
	g_mp3stream = NULL;
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
		int count = zfile_fread(mp3buf, 1, MP3_BLOCK_SIZE, zf);
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
		int ver, layer, bitrate, freq, padding, bitindex, iscrc;
		int samplerate, framelen, bitrateidx, channelmode;
		int isstereo;
		uae_u8 header[4];

		if (zfile_fread(header, sizeof header, 1, zf) != 1)
			return size;
		if (header[0] != 0xff || ((header[1] & (0x80 | 0x40 | 0x20)) != (0x80 | 0x40 | 0x20))) {
			zfile_fseek (zf, -3, SEEK_CUR);
			continue;
		}
		if (firstframe < 0)
			firstframe = zfile_ftell (zf);

		ver = (header[1] >> 3) & 3;
		if (ver == 1) {
			write_log (_T("MP3: ver==1?!\n"));
			return 0;
		}
		if (ver == 0)
			ver = 2;
		else if (ver == 2)
			ver = 1;
		else if (ver == 3)
			ver = 0;
		layer = 4 - ((header[1] >> 1) & 3);
		if (layer == 4) {
			write_log (_T("MP3: layer==4?!\n"));
			return 0;
		}
		iscrc = ((header[1] >> 0) & 1) ? 0 : 2;
		bitrateidx = (header[2] >> 4) & 15;
		freq = mp3_frequencies[(header[2] >> 2) & 3];
		if (!freq) {
			write_log (_T("MP3: reserved frequency?!\n"));
			return 0;
		}
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
		if (bitrate <= 0) {
			write_log (_T("MP3: reserved bitrate?!\n"));
			return 0;
		}
		padding = (header[2] >> 1) & 1;
		samplerate = mp3_samplesperframe[(layer - 1) * 3 + ver];
		framelen = ((samplerate / 8 * bitrate) / freq) + padding;
		if (framelen <= 4) {
			write_log (_T("MP3: too small frame size?!\n"));
			return 0;
		}
		zfile_fseek(zf, framelen - 4, SEEK_CUR);
		frames++;
		if (timelen > 0) {
			size = ((uae_u64)timelen * freq * 2 * (isstereo ? 2 : 1)) / 1000;
			break;
		}
		size += samplerate * 2 * (isstereo ? 2 : 1);
		if (bitrate != oldbitrate) {
			oldbitrate = bitrate;
			sameframes++;
		}
		if (sameframes == 0 && frames > 100) {
			// assume this is CBR MP3
			size = samplerate * 2 * (isstereo ? 2 : 1) * ((zfile_size (zf) - firstframe) / ((samplerate / 8 * bitrate) / freq));
			break;
		}
	}
	return size;
}
