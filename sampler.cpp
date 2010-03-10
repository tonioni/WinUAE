/*
 * Parallel port audio digitizer
 *
 * Toni Wilen 2010
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"

#include "dxwrap.h"

#include <dsound.h>

#define RECORDBUFFER (10000 * 4)
#define SAMPLEBUFFER (1024 * 4)

static LPDIRECTSOUNDCAPTURE lpDS2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSBprimary2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSB2r = NULL;
static int inited;
static uae_u8 *samplebuffer;

static int capture_init (void)
{
	HRESULT hr;
	DSCBUFFERDESC sound_buffer_rec;
	WAVEFORMATEX wavfmt;

	wavfmt.wFormatTag = WAVE_FORMAT_PCM;
	wavfmt.nChannels = 2;
	wavfmt.nSamplesPerSec = 44100;
	wavfmt.wBitsPerSample = 16;
	wavfmt.nBlockAlign = wavfmt.wBitsPerSample / 8 * wavfmt.nChannels;
	wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * wavfmt.nSamplesPerSec;
	wavfmt.cbSize = 0;

	hr = DirectSoundCaptureCreate (NULL, &lpDS2r, NULL);
	if (FAILED (hr)) {
		write_log (L"SAMPLER: DirectSoundCaptureCreate() failure: %s\n", DXError (hr));
		return 0;
	}
	memset (&sound_buffer_rec, 0, sizeof (DSCBUFFERDESC));
	sound_buffer_rec.dwSize = sizeof (DSCBUFFERDESC);
	sound_buffer_rec.dwBufferBytes = RECORDBUFFER;
	sound_buffer_rec.lpwfxFormat = &wavfmt;
	sound_buffer_rec.dwFlags = 0 ;

	hr = IDirectSoundCapture_CreateCaptureBuffer (lpDS2r, &sound_buffer_rec, &lpDSB2r, NULL);
	if (FAILED (hr)) {
		write_log (L"SAMPLER: CreateCaptureSoundBuffer() failure: %s\n", DXError(hr));
		return 0;
	}

	hr = IDirectSoundCaptureBuffer_Start (lpDSB2r, DSCBSTART_LOOPING);
	if (FAILED (hr)) {
		write_log (L"SAMPLER: DirectSoundCaptureBuffer_Start failed: %s\n", DXError (hr));
		return 0;
	}
	samplebuffer = xcalloc (uae_u8, SAMPLEBUFFER);
	write_log (L"SAMPLER: Parallel port sampler initialized\n");
	return 1;
}

static void capture_free (void)
{
	if (lpDSB2r)
		IDirectSoundCaptureBuffer_Release (lpDSB2r);
	lpDSB2r = NULL;
	if (lpDS2r)
		IDirectSound_Release (lpDS2r);
	lpDS2r = NULL;
	xfree (samplebuffer);
	samplebuffer = NULL;
}

static evt oldcycles;

uae_u8 sampler_getsample (void)
{
	HRESULT hr;
	DWORD t, cur_pos, cap_pos;
	void *p1, *p2;
	DWORD len1, len2;
	evt cycles;
	int offset;

	if (!inited) {
		if (!capture_init ())
			return 0;
		inited = 1;
	}
	cycles = get_cycles ();
	offset = (cycles - oldcycles) / CYCLE_UNIT;
	if (offset >= SAMPLEBUFFER || offset < 0) {
		oldcycles = cycles;
		offset = 0;
		cap_pos = 0;
		hr = IDirectSoundCaptureBuffer_GetCurrentPosition (lpDSB2r, &t, &cur_pos);
		if (FAILED (hr))
			return 0;
		hr = IDirectSoundCaptureBuffer_Lock (lpDSB2r, cap_pos, SAMPLEBUFFER, &p1, &len1, &p2, &len2, 0);
		if (FAILED (hr))
			return 0;
		memcpy (samplebuffer, p1, len1);
		if (p2)
			memcpy (samplebuffer + len1, p2, len2);
		IDirectSoundCaptureBuffer_Unlock (lpDSB2r, p1, len1, p2, len2);
	}
	return samplebuffer[offset * 4 + 1];
}

int sampler_init (void)
{
	if (!currprefs.parallel_sampler)
		return 0;
	return 1;
}

void sampler_free (void)
{
	inited = 0;
	capture_free ();
}

