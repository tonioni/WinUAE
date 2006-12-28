 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Win32 sound interface (DirectSound)
  *
  * Copyright 1997 Mathias Ortmann
  * Copyright 1997-2001 Brian King
  * Copyright 2000-2002 Bernd Roesch
  * Copyright 2002-2003 Toni Wilen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "events.h"
#include "custom.h"
#include "gensound.h"
#include "sounddep/sound.h"
#include "threaddep/thread.h"
#include "avioutput.h"
#include "gui.h"
#include "dxwrap.h"
#include "win32.h"
#include "savestate.h"
#include "driveclick.h"
#include "audio.h"

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <ks.h>
#include <ksmedia.h>

#include <math.h>

#define ADJUST_SIZE 30
#define EXP 2.1

#define ADJUST_VSSIZE 10
#define EXPVS 1.3

int sound_debug = 0;

static int obtainedfreq;
static int have_sound;
static int paused;
static int mute;

#define SND_MAX_BUFFER2 524288
#define SND_MAX_BUFFER 8192

uae_u16 sndbuffer[SND_MAX_BUFFER];
uae_u16 *sndbufpt;
int sndbufsize;

static int max_sndbufsize, snd_configsize, dsoundbuf;
static int snd_writeoffset, snd_maxoffset, snd_totalmaxoffset_uf, snd_totalmaxoffset_of;
static int waiting_for_buffer;

static uae_sem_t sound_sem, sound_init_sem;

#define MAX_SOUND_DEVICES 10

static char *sound_devices[MAX_SOUND_DEVICES];
GUID sound_device_guid[MAX_SOUND_DEVICES];
static int num_sound_devices;

static LPDIRECTSOUNDBUFFER lpDSBprimary;

#define USE_DS8
#ifdef USE_DS8
static LPDIRECTSOUND8 lpDS;
static LPDIRECTSOUNDBUFFER8 lpDSBsecondary;
#else
static LPDIRECTSOUND lpDS;
static LPDIRECTSOUNDBUFFER lpDSBsecondary;
#endif

static DWORD writepos;

int setup_sound (void)
{
    sound_available = 1;
    return 1;
}

static int isvsync(void)
{
    return (currprefs.gfx_vsync && currprefs.gfx_afullscreen) ? 1 : 0;
}

int scaled_sample_evtime_orig;
static int lastfreq;
void update_sound (int freq)
{
    if (freq < 0)
    	freq = lastfreq;
    lastfreq = freq;
    if (have_sound) {
	if (isvsync() || currprefs.chipset_refreshrate) {
	    if (currprefs.ntscmode)
		scaled_sample_evtime_orig = (unsigned long)(MAXHPOS_NTSC * MAXVPOS_NTSC * freq * CYCLE_UNIT + obtainedfreq - 1) / obtainedfreq;
	    else
		scaled_sample_evtime_orig = (unsigned long)(MAXHPOS_PAL * MAXVPOS_PAL * freq * CYCLE_UNIT + obtainedfreq - 1) / obtainedfreq;
	} else {
	    scaled_sample_evtime_orig = (unsigned long)(312.0 * 50 * CYCLE_UNIT / (obtainedfreq  / 227.0));
	}
	scaled_sample_evtime = scaled_sample_evtime_orig;
    }
}

static void cleardsbuffer (void)
{
    void *buffer;
    DWORD size;

    HRESULT hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, dsoundbuf, &buffer, &size, NULL, NULL, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (lpDSBsecondary);
	hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, dsoundbuf, &buffer, &size, NULL, NULL, 0);
    }
    if (FAILED(hr)) {
	write_log ("SOUND: failed to Lock sound buffer (clear): %s\n", DXError (hr));
	return;
    }
    memset (buffer, 0, size);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, buffer, size, NULL, 0);
}

static void pause_audio_ds (void)
{
    waiting_for_buffer = 0;
    IDirectSoundBuffer_Stop (lpDSBsecondary);
    IDirectSoundBuffer_SetCurrentPosition (lpDSBsecondary, 0);
    cleardsbuffer ();
}

static void resume_audio_ds (void)
{
    paused = 0;
    cleardsbuffer ();
    waiting_for_buffer = 1;
}

static int restore (DWORD hr)
{
    if (hr != DSERR_BUFFERLOST)
	return 0;
    if (sound_debug)
	write_log ("sound buffer lost\n");
    hr = IDirectSoundBuffer_Restore (lpDSBsecondary);
    if (FAILED(hr)) {
	write_log ("SOUND: restore failed %s\n", DXError (hr));
	return 1;
    }
    pause_audio_ds ();
    resume_audio_ds ();
    return 1;
}

static LARGE_INTEGER qpfc, qpf;
static void storeqpf (void)
{
    QueryPerformanceCounter(&qpfc);
}
static double getqpf (void)
{
    LARGE_INTEGER qpfc2;
    QueryPerformanceCounter(&qpfc2);
    return (qpfc2.QuadPart - qpfc.QuadPart) / (qpf.QuadPart / 1000.0);
}

static int getpos (void)
{
    DWORD playpos, safepos;
    HRESULT hr;

    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safepos);
    if (FAILED(hr)) {
	write_log ("SOUND: GetCurrentPosition failed: %s\n", DXError (hr));
	return -1;
    }
    return playpos;
}

static void close_audio_ds (void)
{
    waiting_for_buffer = 0;
    if (lpDSBsecondary)
	IDirectSound_Release (lpDSBsecondary);
    if (lpDSBprimary)
	IDirectSound_Release (lpDSBprimary);
    lpDSBsecondary = 0;
    lpDSBprimary = 0;
    if (lpDS) {
	IDirectSound_Release (lpDS);
	write_log ("SOUND: DirectSound driver freed\n");
    }
    lpDS = 0;
}

extern HWND hMainWnd;
extern void setvolume_ahi(LONG);
static void setvolume (void)
{
    HRESULT hr;
    LONG vol = DSBVOLUME_MIN;

    if (currprefs.sound_volume < 100 && !mute)
        vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (1 - currprefs.sound_volume / 100.0)));
    hr = IDirectSoundBuffer_SetVolume (lpDSBsecondary, vol);
    if (FAILED(hr))
        write_log ("SOUND: SetVolume(%d) failed: %s\n", vol, DXError (hr));
    setvolume_ahi (vol);
}

const static GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,
{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static int open_audio_ds (int size)
{
    HRESULT hr;
    DSBUFFERDESC sound_buffer;
    DSCAPS DSCaps;
    DSBCAPS DSBCaps;
    WAVEFORMATEXTENSIBLE wavfmt;
    int freq = currprefs.sound_freq;
    LPDIRECTSOUNDBUFFER pdsb;
    
    enumerate_sound_devices (0);
    if (currprefs.sound_stereo == 3) {
	size <<= 3;
    } else {
	size <<= 1;
	if (currprefs.sound_stereo)
	    size <<= 1;
    }
    snd_configsize = size;
    sndbufsize = size / 32;
    if (sndbufsize > SND_MAX_BUFFER)
        sndbufsize = SND_MAX_BUFFER;

    max_sndbufsize = size * 4;
    if (max_sndbufsize > SND_MAX_BUFFER2)
        max_sndbufsize = SND_MAX_BUFFER2;
    dsoundbuf = max_sndbufsize * 2;

    if (dsoundbuf < DSBSIZE_MIN)
        dsoundbuf = DSBSIZE_MIN;
    if (dsoundbuf > DSBSIZE_MAX)
        dsoundbuf = DSBSIZE_MAX;

    if (max_sndbufsize * 2 > dsoundbuf)
        max_sndbufsize = dsoundbuf / 2;

    snd_writeoffset = max_sndbufsize * 5 / 8;
    snd_maxoffset = max_sndbufsize;
    snd_totalmaxoffset_of = max_sndbufsize + (dsoundbuf - max_sndbufsize) * 3 / 9;
    snd_totalmaxoffset_uf = max_sndbufsize + (dsoundbuf - max_sndbufsize) * 7 / 9;

    memset (&wavfmt, 0, sizeof (WAVEFORMATEXTENSIBLE));
    wavfmt.Format.nChannels = (currprefs.sound_stereo == 3 || currprefs.sound_stereo == 2) ? 4 : (currprefs.sound_stereo ? 2 : 1);
    wavfmt.Format.wFormatTag = wavfmt.Format.nChannels > 2 ? WAVE_FORMAT_EXTENSIBLE : WAVE_FORMAT_PCM;
    wavfmt.Format.nSamplesPerSec = freq;
    wavfmt.Format.wBitsPerSample = 16;
    wavfmt.Format.nBlockAlign = wavfmt.Format.wBitsPerSample / 8 * wavfmt.Format.nChannels;
    wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;
    if (wavfmt.Format.nChannels > 2) {
	wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
	wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	wavfmt.Samples.wValidBitsPerSample = 16;
	wavfmt.dwChannelMask = KSAUDIO_SPEAKER_QUAD;
    }

    write_log ("SOUND: '%s'/%d/%d bits/%d Hz/buffer %d/dist %d\n",
	sound_devices[currprefs.win32_soundcard],
	wavfmt.Format.nChannels, 16, freq, max_sndbufsize, snd_configsize);

#ifdef USE_DS8
    hr = DirectSoundCreate8 (&sound_device_guid[currprefs.win32_soundcard], &lpDS, NULL);
#else
    hr = DirectSoundCreate (&sound_device_guid[currprefs.win32_soundcard], &lpDS, NULL);
#endif
    if (FAILED(hr))  {
        write_log ("SOUND: DirectSoundCreate() failure: %s\n", DXError (hr));
        return 0;
    }

    hr = IDirectSound_SetCooperativeLevel (lpDS, hMainWnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        write_log ("SOUND: Can't set cooperativelevel: %s\n", DXError (hr));
        goto error;
    }

    memset (&DSCaps, 0, sizeof (DSCaps));
    DSCaps.dwSize = sizeof (DSCaps);
    hr = IDirectSound_GetCaps (lpDS, &DSCaps);
    if (FAILED(hr)) {
	write_log ("SOUND: Error getting DirectSound capabilities: %s\n", DXError (hr));
	goto error;
    }
    if (DSCaps.dwFlags & DSCAPS_EMULDRIVER) {
	write_log ("SOUND: Emulated DirectSound driver detected, don't complain if sound quality is crap :)\n");
    }
    if (DSCaps.dwFlags & DSCAPS_CONTINUOUSRATE) {
	int minfreq = DSCaps.dwMinSecondarySampleRate;
	int maxfreq = DSCaps.dwMaxSecondarySampleRate;
	if (minfreq > freq && freq < 22050) {
	    freq = minfreq;
	    changed_prefs.sound_freq = currprefs.sound_freq = freq;
	    write_log("SOUND: minimum supported frequency: %d\n", minfreq);
	}
	if (maxfreq < freq && freq > 44100) {
	    freq = maxfreq;
	    changed_prefs.sound_freq = currprefs.sound_freq = freq;
	    write_log("SOUND: maximum supported frequency: %d\n", maxfreq);
	}
    }
    
    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwFlags = DSBCAPS_PRIMARYBUFFER;
    hr = IDirectSound_CreateSoundBuffer (lpDS, &sound_buffer, &lpDSBprimary, NULL);
    if (FAILED(hr))  {
        write_log ("SOUND: Primary CreateSoundBuffer() failure: %s\n", DXError (hr));
	goto error;
    }

    memset(&DSBCaps, 0, sizeof(DSBCaps));
    DSBCaps.dwSize = sizeof(DSBCaps);
    hr = IDirectSoundBuffer_GetCaps(lpDSBprimary, &DSBCaps);
    if (FAILED(hr)) {
	write_log ("SOUND: Primary GetCaps() failure: %s\n",  DXError (hr));
	goto error;
    }

    hr = IDirectSoundBuffer_SetFormat (lpDSBprimary, &wavfmt.Format);
    if (FAILED(hr))  {
        write_log ("SOUND: Primary SetFormat() failure: %s\n", DXError (hr));
        goto error;
    }

    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwBufferBytes = dsoundbuf;
    sound_buffer.lpwfxFormat = &wavfmt.Format;
    sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    sound_buffer.dwFlags |= DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
    sound_buffer.guid3DAlgorithm = GUID_NULL;

    hr = IDirectSound_CreateSoundBuffer(lpDS, &sound_buffer, &pdsb, NULL);
    if (FAILED(hr)) {
        write_log ("SOUND: Secondary CreateSoundBuffer() failure: %s\n", DXError (hr));
        goto error;
    }
#ifdef USE_DS8
    hr = IDirectSound_QueryInterface(pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&lpDSBsecondary);
    if (FAILED(hr))  {
        write_log ("SOUND: Primary QueryInterface() failure: %s\n", DXError (hr));
	goto error;
    }
    IDirectSound_Release(pdsb);
#else
    lpDSBsecondary = pdsb;
    pdsb = NULL;
#endif

    setvolume ();
    cleardsbuffer ();
    init_sound_table16 ();
    if (currprefs.sound_stereo == 3)
	sample_handler = sample16ss_handler;
    else
	sample_handler = currprefs.sound_stereo ? sample16s_handler : sample16_handler;

    obtainedfreq = currprefs.sound_freq;

    return 1;

error:
    close_audio_ds ();
    return 0;
}

static int open_sound (void)
{
    int ret;
    int size = currprefs.sound_maxbsiz;

    if (!currprefs.produce_sound)
	return 0;
    /* Always interpret buffer size as number of samples, not as actual
       buffer size.  Of course, since 8192 is the default, we'll have to
       scale that to a sane value (assuming that otherwise 16 bits and
       stereo would have been enabled and we'd have done the shift by
       two anyway).  */
    size >>= 2;
    if (size & (size - 1))
	size = DEFAULT_SOUND_MAXB;
    if (size < 512)
	size = 512;

    ret = open_audio_ds (size);
    if (!ret)
	return 0;

    have_sound = 1;
    sound_available = 1;
    update_sound (fake_vblank_hz);
    sndbufpt = sndbuffer;
    driveclick_init ();

    return 1;
}

void close_sound (void)
{
    if (! have_sound)
	return;
    pause_sound ();
    close_audio_ds ();
    have_sound = 0;
    gui_data.sndbuf = 0;
}

int init_sound (void)
{
    if (have_sound)
	return 1;
    if (!open_sound ())
	return 0;
    paused = 1;
    driveclick_reset ();
    resume_sound ();
    return 1;
}

void pause_sound (void)
{
    if (paused)
	return;
    paused = 1;
    if (!have_sound)
	return;
    pause_audio_ds ();
    cleardsbuffer();
}

void resume_sound (void)
{
    if (!paused)
	return;
    if (!have_sound)
	return;
    cleardsbuffer ();
    resume_audio_ds ();
}

void reset_sound (void)
{
    if (!have_sound)
	return;
    cleardsbuffer ();
}

#ifdef JIT
extern uae_u8* compiled_code;
#else
static int compiled_code;
#endif
extern int vsynctime_orig;

#ifndef AVIOUTPUT
static int avioutput_audio;
#endif

void sound_setadjust (double v)
{
    double mult;

    mult = (1000.0 + v);
    if ((currprefs.gfx_vsync && currprefs.gfx_afullscreen) || (avioutput_audio && !compiled_code)) {
	vsynctime = vsynctime_orig;
	scaled_sample_evtime = (long)(((double)scaled_sample_evtime_orig) * mult / 1000.0);
    } else if (compiled_code || currprefs.m68k_speed != 0) {
	vsynctime = (long)(((double)vsynctime_orig) * mult / 1000.0);
	scaled_sample_evtime = scaled_sample_evtime_orig;
    } else {
	vsynctime = (long)(((double)vsynctime_orig) * mult / 1000.0);
	scaled_sample_evtime = scaled_sample_evtime_orig;
    }
}

#define SND_STATUSCNT 10

static int safedist;

#define cf(x) if ((x) >= dsoundbuf) (x) -= dsoundbuf;

void restart_sound_buffer(void)
{
    DWORD playpos, safed;
    HRESULT hr;

    if (waiting_for_buffer != -1)
	return;
    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safed);
    if (FAILED(hr))
	return;
    writepos = playpos + snd_writeoffset - 2 * sndbufsize;
    if (writepos < 0)
	writepos += dsoundbuf;
    cf (writepos);
}

static void finish_sound_buffer_ds (void)
{
    static int tfprev;
    DWORD playpos, safepos, status;
    HRESULT hr;
    void *b1, *b2;
    DWORD s1, s2;
    int diff;
    int counter = 1000;
    double vdiff, m, skipmode;
    static int statuscnt;

    if (statuscnt > 0) {
	statuscnt--;
	if (statuscnt == 0)
	    gui_data.sndbuf_status = 0;
    }
    if (gui_data.sndbuf_status == 3)
	gui_data.sndbuf_status = 0;

    if (!waiting_for_buffer)
	return;
    if (savestate_state)
        return;

    if (waiting_for_buffer == 1) {
	hr = IDirectSoundBuffer_Play (lpDSBsecondary, 0, 0, DSBPLAY_LOOPING);
	if (FAILED(hr)) {
	    write_log ("SOUND: Play failed: %s\n", DXError (hr));
	    restore (DSERR_BUFFERLOST);
	    waiting_for_buffer = 0;
	    return;
	}
	hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safedist);
	if (FAILED(hr)) {
	    write_log ("SOUND: 1st GetCurrentPosition failed: %s\n", DXError (hr));
	    restore (DSERR_BUFFERLOST);
	    waiting_for_buffer = 0;
	    return;
	}
	safedist -= playpos;
	if (safedist < 64)
	    safedist = 64;
	safedist += sndbufsize;
	if (safedist < 0)
	    safedist += dsoundbuf;
	cf(safedist);
	snd_totalmaxoffset_uf += safedist;
	cf (snd_totalmaxoffset_uf);
	snd_totalmaxoffset_of += safedist;
	cf (snd_totalmaxoffset_of);
	snd_maxoffset += safedist;
	cf (snd_maxoffset);
	snd_writeoffset += safedist;
	cf (snd_writeoffset);
	waiting_for_buffer = -1;
	restart_sound_buffer();
	write_log("SOUND: safe=%d bs=%d w=%d max=%d tof=%d tuf=%d\n",
	    safedist - sndbufsize, sndbufsize, snd_writeoffset,
	    snd_maxoffset, snd_totalmaxoffset_of, snd_totalmaxoffset_uf);
	tfprev = timeframes + 10;
	tfprev = (tfprev / 10) * 10;
    }

    hr = IDirectSoundBuffer_GetStatus (lpDSBsecondary, &status);
    if (FAILED(hr)) {
	write_log ("SOUND: GetStatus() failed: %s\n", DXError(hr));
	restore (DSERR_BUFFERLOST);
	return;
    }
    if (status & DSBSTATUS_BUFFERLOST) {
	write_log ("SOUND: buffer lost\n");
	restore (DSERR_BUFFERLOST);
	return;
    }
    if ((status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) != (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) {
	write_log ("SOUND: status = %08.8X\n", status);
	restore (DSERR_BUFFERLOST);
	return;
    }
    for (;;) {
	hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safepos);
	if (FAILED(hr)) {
	    restore (hr);
	    write_log ("SOUND: GetCurrentPosition failed: %s\n", DXError (hr));
	    return;
	}

	if (writepos >= playpos)
	    diff = writepos - playpos;
	else
	    diff = dsoundbuf - playpos + writepos;

	if (diff < safedist || diff > snd_totalmaxoffset_uf) {
#if 0
	    hr = IDirectSoundBuffer_Lock (lpDSBsecondary, writepos, sndbufsize, &b1, &s1, &b2, &s2, 0);
	    if (SUCCEEDED(hr)) {
		memset (b1, 0, s1);
		if (b2)
		    memset (b2, 0, s2);
		IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);
	    }
#endif
	    gui_data.sndbuf_status = -1;
	    statuscnt = SND_STATUSCNT;
	    if (diff > snd_totalmaxoffset_uf)
		writepos += dsoundbuf - diff;
	    writepos += sndbufsize;
	    cf(writepos);
	    diff = safedist;
	    break;
	}

	if (diff > snd_totalmaxoffset_of) {
	    gui_data.sndbuf_status = 2;
	    statuscnt = SND_STATUSCNT;
	    restart_sound_buffer();
	    diff = snd_writeoffset;
	    write_log("SOUND: underflow (%d %d)\n", diff, snd_totalmaxoffset_of);
	    break;
	}

	if (diff > snd_maxoffset) {
	    gui_data.sndbuf_status = 1;
	    statuscnt = SND_STATUSCNT;
	    sleep_millis(1);
	    counter--;
	    if (counter < 0) {
		write_log ("SOUND: sound system got stuck!?\n");
		restore (DSERR_BUFFERLOST);
		return;
	    }
	    continue;
	}
	break;
    }

    hr = IDirectSoundBuffer_Lock (lpDSBsecondary, writepos, sndbufsize, &b1, &s1, &b2, &s2, 0);
    if (restore (hr))
	return;
    if (FAILED(hr)) {
	write_log ("SOUND: lock failed: %s (%d %d)\n", DXError (hr), writepos, sndbufsize);
        return;
    }
    memcpy (b1, sndbuffer, s1);
    if (b2)
        memcpy (b2, (uae_u8*)sndbuffer + s1, s2);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);

    vdiff = diff - snd_writeoffset;
    m = 100.0 * vdiff / max_sndbufsize;

    if (isvsync()) {

	skipmode = pow (m < 0 ? -m : m, EXP) / 10;
	if (m < 0)
	    skipmode = -skipmode;
	if (skipmode < -ADJUST_VSSIZE)
	    skipmode = -ADJUST_VSSIZE;
	if (skipmode > ADJUST_VSSIZE)
	    skipmode = ADJUST_VSSIZE;

    } else {

	skipmode = pow (m < 0 ? -m : m, EXP) / 2;
	if (m < 0)
	    skipmode = -skipmode;
	if (skipmode < -ADJUST_SIZE)
	    skipmode = -ADJUST_SIZE;
	if (skipmode > ADJUST_SIZE)
	    skipmode = ADJUST_SIZE;

    }

    if (tfprev != timeframes) {
	if (sound_debug && !(tfprev % 10))
	    write_log ("b=%4d,%5d,%5d,%5d d=%5d vd=%5.0f s=%+02.1f\n",
		sndbufsize, snd_configsize, max_sndbufsize, dsoundbuf, diff, vdiff, skipmode);
	tfprev = timeframes;
        if (!avioutput_audio)
	    sound_setadjust (skipmode);
        gui_data.sndbuf = vdiff * 1000 / (snd_maxoffset - snd_writeoffset);
    }

    writepos += sndbufsize;
    cf(writepos);
}

static void channelswap(uae_s16 *sndbuffer, int len)
{
    int i;
    for (i = 0; i < len; i += 2) {
	uae_s16 t = sndbuffer[i];
	sndbuffer[i] = sndbuffer[i + 1];
	sndbuffer[i + 1] = t;
    }
}

void finish_sound_buffer (void)
{
    if (turbo_emulation)
	return;
    if (ISSTEREO(currprefs) && currprefs.sound_stereo_swap_paula)
        channelswap((uae_s16*)sndbuffer, sndbufsize / 2);
#ifdef DRIVESOUND
    driveclick_mix ((uae_s16*)sndbuffer, sndbufsize / 2);
#endif
#ifdef AVIOUTPUT
    if (avioutput_audio)
        AVIOutput_WriteAudio ((uae_u8*)sndbuffer, sndbufsize);
    if (avioutput_enabled && (!avioutput_framelimiter || avioutput_nosoundoutput))
	return;
#endif
    if (!have_sound)
	return;
    finish_sound_buffer_ds ();
}

static BOOL CALLBACK DSEnumProc(LPGUID lpGUID, LPCTSTR lpszDesc, LPCTSTR lpszDrvName,  LPVOID lpContext)
{
    int i = num_sound_devices;
    if (i == MAX_SOUND_DEVICES)
	return TRUE;
    if (lpGUID != NULL)
	memcpy (&sound_device_guid[i], lpGUID, sizeof (GUID));
    sound_devices[i] = my_strdup (lpszDesc);
    num_sound_devices++;
    return TRUE;
}

char **enumerate_sound_devices (int *total)
{
    if (!num_sound_devices)
        DirectSoundEnumerate ((LPDSENUMCALLBACK)DSEnumProc, 0);
    if (total)
	*total = num_sound_devices;
    if (currprefs.win32_soundcard >= num_sound_devices)
	currprefs.win32_soundcard = 0;
    if (num_sound_devices)
	return sound_devices;
    return 0;
}

void sound_volume (int dir)
{
    if (dir == 0)
	mute = mute ? 0 : 1;
    currprefs.sound_volume -= dir * 10;
    if (currprefs.sound_volume < 0)
	currprefs.sound_volume = 0;
    if (currprefs.sound_volume > 100)
	currprefs.sound_volume = 100;
    changed_prefs.sound_volume = currprefs.sound_volume;
    setvolume ();
}
