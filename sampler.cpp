/*
 * Parallel port audio digitizer
 *
 * Toni Wilen 2010
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"
#include "custom.h"
#include "sampler.h"

#include "dxwrap.h"

#include <dsound.h>

#include <math.h>

#include "win32.h"

#define SAMPLESIZE 4
#define RECORDBUFFER 10000
#define SAMPLEBUFFER 2000

static LPDIRECTSOUNDCAPTURE lpDS2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSBprimary2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSB2r = NULL;
static int inited;
static uae_u8 *samplebuffer;
static int samplerate = 44100;
static float clockspersample;
static int vsynccnt;
static int safepos;
float sampler_evtime;

static int capture_init (void)
{
	HRESULT hr;
	DSCBUFFERDESC sound_buffer_rec;
	WAVEFORMATEX wavfmt;

	wavfmt.wFormatTag = WAVE_FORMAT_PCM;
	wavfmt.nChannels = 2;
	wavfmt.nSamplesPerSec = samplerate;
	wavfmt.wBitsPerSample = 16;
	wavfmt.nBlockAlign = wavfmt.wBitsPerSample / 8 * wavfmt.nChannels;
	wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * wavfmt.nSamplesPerSec;
	wavfmt.cbSize = 0;

	hr = DirectSoundCaptureCreate (&record_devices[currprefs.win32_samplersoundcard]->guid, &lpDS2r, NULL);
	if (FAILED (hr)) {
		write_log (_T("SAMPLER: DirectSoundCaptureCreate() failure: %s\n"), DXError (hr));
		return 0;
	}
	memset (&sound_buffer_rec, 0, sizeof (DSCBUFFERDESC));
	sound_buffer_rec.dwSize = sizeof (DSCBUFFERDESC);
	sound_buffer_rec.dwBufferBytes = RECORDBUFFER * SAMPLESIZE;
	sound_buffer_rec.lpwfxFormat = &wavfmt;
	sound_buffer_rec.dwFlags = 0 ;

	hr = lpDS2r->CreateCaptureBuffer (&sound_buffer_rec, &lpDSB2r, NULL);
	if (FAILED (hr)) {
		write_log (_T("SAMPLER: CreateCaptureSoundBuffer() failure: %s\n"), DXError(hr));
		return 0;
	}

	hr = lpDSB2r->Start (DSCBSTART_LOOPING);
	if (FAILED (hr)) {
		write_log (_T("SAMPLER: DirectSoundCaptureBuffer_Start failed: %s\n"), DXError (hr));
		return 0;
	}
	samplebuffer = xcalloc (uae_u8, SAMPLEBUFFER * SAMPLESIZE);
	write_log (_T("SAMPLER: Parallel port sampler initialized\n"));
	return 1;
}

static void capture_free (void)
{
	if (lpDSB2r) {
		lpDSB2r->Stop ();
		lpDSB2r->Release ();
		write_log (_T("SAMPLER: Parallel port sampler freed\n"));
	}
	lpDSB2r = NULL;
	if (lpDS2r)
		lpDS2r->Release ();
	lpDS2r = NULL;
	xfree (samplebuffer);
	samplebuffer = NULL;
}

static evt oldcycles;
static int oldoffset;

uae_u8 sampler_getsample (int channel)
{
	HRESULT hr;
	static DWORD cap_pos;
	static float diffsample;
	DWORD t, cur_pos;
	void *p1, *p2;
	DWORD len1, len2;
	evt cycles;
	int offset;
	int sample, samplecnt, diff;

//	if (channel)
//		return 0;
	channel = 0;

	if (!inited) {
		if (!capture_init ()) {
			capture_free ();
			return 0;
		}
		inited = 1;
		oldcycles = get_cycles ();
		oldoffset = -1;
		safepos = -RECORDBUFFER / 10 * SAMPLESIZE;
		hr = lpDSB2r->GetCurrentPosition (&t, &cap_pos);
		cap_pos += safepos;
		if (cap_pos > 10 * RECORDBUFFER * SAMPLESIZE)
			cap_pos += RECORDBUFFER * SAMPLESIZE;
		if (cap_pos >= RECORDBUFFER * SAMPLESIZE)
			cap_pos -= RECORDBUFFER * SAMPLESIZE;
		if (FAILED (hr)) {
			sampler_free ();
			return 0;
		}
		clockspersample = sampler_evtime / samplerate + 41000;
	}
	if (clockspersample < 1)
		return 0;
	uae_s16 *sbuf = (uae_s16*)samplebuffer;

	vsynccnt = 0;
	sample = 0;
	samplecnt = 0;
	cycles = get_cycles () - oldcycles;
	float cps = clockspersample + diffsample;
	offset = (cycles + cps - 1) / cps;
	if (oldoffset < 0 || offset >= SAMPLEBUFFER || offset < 0) {
		if (oldoffset >= 0 && offset >= SAMPLEBUFFER) {
			while (oldoffset < SAMPLEBUFFER) {
				sample += sbuf[oldoffset * SAMPLESIZE / 2 + channel];
				oldoffset++;
				samplecnt++;
			}
		}
		hr = lpDSB2r->Lock (cap_pos, SAMPLEBUFFER * SAMPLESIZE, &p1, &len1, &p2, &len2, 0);
		if (FAILED (hr))
			return 0;
		memcpy (samplebuffer, p1, len1);
		if (p2)
			memcpy (samplebuffer + len1, p2, len2);
		lpDSB2r->Unlock (p1, len1, p2, len2);
		cap_pos += SAMPLEBUFFER * SAMPLESIZE;

		hr = lpDSB2r->GetCurrentPosition (&t, &cur_pos);
		if (FAILED (hr))
			return 0;
		cur_pos += safepos;
		if (cur_pos >= 10 * RECORDBUFFER * SAMPLESIZE)
			cur_pos += RECORDBUFFER * SAMPLESIZE;
		if (cur_pos >= RECORDBUFFER * SAMPLESIZE)
			cur_pos -= RECORDBUFFER * SAMPLESIZE;
		if (cur_pos >= cap_pos)
			diff = cur_pos - cap_pos;
		else
			diff = RECORDBUFFER * SAMPLESIZE - cap_pos + cur_pos;
		if (diff > RECORDBUFFER * SAMPLESIZE / 2)
			diff -= RECORDBUFFER * SAMPLESIZE; 
		diff /= SAMPLESIZE;

		int diff2 = 100 * diff / (RECORDBUFFER / 2);
#if 0
		diffsample = -pow (diff2 < 0 ? -diff2 : diff2, 3.1);
		if (diff2 < 0)
			diffsample = -diffsample;
#endif	
//		write_log (_T("%d:%.1f\n"), diff, diffsample);

		cap_pos += SAMPLEBUFFER * SAMPLESIZE;
		if (cap_pos < 0)
			cap_pos += RECORDBUFFER * SAMPLESIZE;
		if (cap_pos >= RECORDBUFFER * SAMPLESIZE)
			cap_pos -= RECORDBUFFER * SAMPLESIZE;

		if (offset < 0)
			offset = 0;
		if (offset >= SAMPLEBUFFER)
			offset -= SAMPLEBUFFER;
		oldoffset = 0;
		oldcycles = get_cycles ();
	}
	while (oldoffset <= offset) {
		sample += sbuf[oldoffset * SAMPLESIZE / 2 + channel];
		samplecnt++;
		oldoffset++;
	}
	oldoffset = offset;
	if (samplecnt > 0)
		sample /= samplecnt;
	return (sample / 256) - 128;
}

int sampler_init (void)
{
	if (currprefs.win32_samplersoundcard < 0)
		return 0;
	return 1;
}

void sampler_free (void)
{
	inited = 0;
	vsynccnt = 0;
	capture_free ();
}

void sampler_vsync (void)
{
	if (!inited)
		return;
	vsynccnt++;
	if (vsynccnt > 50) {
		sampler_free ();
		return;
	}
}