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
#include <dsound.h>

#include <math.h>

#define ADJUST_SIZE 20
#define EXP 1.3

int sound_debug;

static int obtainedfreq;
static int have_sound;
static int paused;
static int mute;

#define SND_MAX_BUFFER2 262144
#define SND_MAX_BUFFER 512

uae_u16 sndbuffer[SND_MAX_BUFFER];
uae_u16 *sndbufpt;
int sndbufsize;

static int max_sndbufsize, snd_configsize, dsoundbuf;
static int snd_writeoffset, snd_maxoffset, snd_totalmaxoffset;
static int waiting_for_buffer;

static uae_sem_t sound_sem, sound_init_sem;

#define MAX_SOUND_DEVICES 10

static char *sound_devices[MAX_SOUND_DEVICES];
GUID sound_device_guid[MAX_SOUND_DEVICES];
static int num_sound_devices;

static LPDIRECTSOUND8 lpDS;
static LPDIRECTSOUNDBUFFER lpDSBprimary;
static LPDIRECTSOUNDBUFFER8 lpDSBsecondary;

static DWORD writepos;

int setup_sound (void)
{
    sound_available = 1;
    return 1;
}

int scaled_sample_evtime_orig;
static int lastfreq;
void update_sound (int freq)
{
    if (freq < 0)
    	freq = lastfreq;
    lastfreq = freq;
    if (have_sound) {
	if ((currprefs.gfx_vsync && currprefs.gfx_afullscreen) || currprefs.chipset_refreshrate) {
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

static void clearbuffer (void)
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
    memset (sndbuffer, 0, sizeof (sndbuffer));
}

static void pause_audio_ds (void)
{
    waiting_for_buffer = 0;
    IDirectSoundBuffer_Stop (lpDSBsecondary);
    IDirectSoundBuffer_SetCurrentPosition (lpDSBsecondary, 0);
    clearbuffer ();
}

static void resume_audio_ds (void)
{
    paused = 0;
    clearbuffer ();
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

static int calibrate (void)
{
    int len = 1000;
    int pos, lastpos, tpos, expected, diff;
    int mult = (currprefs.sound_stereo == 3) ? 8 : (currprefs.sound_stereo ? 4 : 2);
    double qv, pct;
    HRESULT hr;

    if (!QueryPerformanceFrequency(&qpf)) {
	write_log ("SOUND: no QPF, can't calibrate\n");
	return 100 * 10;
    }
    pos = 1000;
    pause_audio_ds ();
    resume_audio_ds ();
    hr = IDirectSoundBuffer_Play (lpDSBsecondary, 0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) 
	return 100 * 10;
    while (pos >= 1000)
        pos = getpos();
    while (pos < 1000)
        pos = getpos();
    lastpos = getpos();
    storeqpf ();
    tpos = 0;
    do {
	pos = getpos();
	if (pos < lastpos) {
	    tpos += dsoundbuf - lastpos + pos;
	} else {
	    tpos += pos - lastpos;
	}
	lastpos = pos;
        qv = getqpf();
    } while (qv < len);
    expected = (int)(len / 1000.0 * currprefs.sound_freq);
    tpos /= mult;
    diff = tpos - expected;
    pct = tpos * 100.0 / expected;
    write_log ("SOUND: calibration: %d %d (%d %.2f%%)\n", tpos, expected, diff, pct);
    return (int)(pct * 10);
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

static void setvolume (void)
{
    HRESULT hr;
    LONG vol = DSBVOLUME_MIN;

    if (currprefs.sound_volume < 100 && !mute)
        vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (1 - currprefs.sound_volume / 100.0)));
    hr = IDirectSoundBuffer_SetVolume (lpDSBsecondary, vol);
    if (FAILED(hr))
        write_log ("SOUND: SetVolume(%d) failed: %s\n", vol, DXError (hr));
}

static int open_audio_ds (int size)
{
    HRESULT hr;
    DSBUFFERDESC sound_buffer;
    DSCAPS DSCaps;
    DSBCAPS DSBCaps;
    WAVEFORMATEX wavfmt;
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

    hr = DirectSoundCreate8 (&sound_device_guid[currprefs.win32_soundcard], &lpDS, NULL);
    if (FAILED(hr))  {
        write_log ("SOUND: DirectSoundCreate() failure: %s\n", DXError (hr));
        return 0;
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
    sound_buffer.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_GETCURRENTPOSITION2;
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

    wavfmt.wFormatTag = WAVE_FORMAT_PCM;
    wavfmt.nChannels = (currprefs.sound_stereo == 3 || currprefs.sound_stereo == 2) ? 4 : (currprefs.sound_stereo ? 2 : 1);
    wavfmt.nSamplesPerSec = freq;
    wavfmt.wBitsPerSample = 16;
    wavfmt.nBlockAlign = 16 / 8 * wavfmt.nChannels;
    wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * freq;
    wavfmt.cbSize = 0;

    max_sndbufsize = size * 4;
    if (max_sndbufsize > SND_MAX_BUFFER2)
        max_sndbufsize = SND_MAX_BUFFER2;
    dsoundbuf = max_sndbufsize * 2;

    hr = IDirectSound_SetCooperativeLevel (lpDS, hMainWnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        write_log ("SOUND: Can't set cooperativelevel: %s\n", DXError (hr));
        goto error;
    }
    if (dsoundbuf < DSBSIZE_MIN)
        dsoundbuf = DSBSIZE_MIN;
    if (dsoundbuf > DSBSIZE_MAX)
        dsoundbuf = DSBSIZE_MAX;
    if (max_sndbufsize > dsoundbuf)
        max_sndbufsize = dsoundbuf;

    snd_writeoffset = max_sndbufsize * 3 / 4;
    snd_maxoffset = max_sndbufsize;
    snd_totalmaxoffset = max_sndbufsize + (dsoundbuf - max_sndbufsize) / 2;

    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwBufferBytes = dsoundbuf;
    sound_buffer.lpwfxFormat = &wavfmt;
    sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    sound_buffer.dwFlags |= DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY;

    hr = IDirectSound_CreateSoundBuffer(lpDS, &sound_buffer, &pdsb, NULL);
    if (FAILED(hr)) {
        write_log ("SOUND: Secondary CreateSoundBuffer() failure: %s\n", DXError (hr));
        goto error;
    }
    hr = IDirectSound_QueryInterface(pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&lpDSBsecondary);
    if (FAILED(hr))  {
        write_log ("SOUND: Primary QueryInterface() failure: %s\n", DXError (hr));
	goto error;
    }
    IDirectSound_Release(pdsb);

    hr = IDirectSoundBuffer_SetFormat (lpDSBprimary, &wavfmt);
    if (FAILED(hr))  {
        write_log ("SOUND: Primary SetFormat() failure: %s\n", DXError (hr));
        goto error;
    }
    setvolume ();
    clearbuffer ();
    init_sound_table16 ();
    if (currprefs.sound_stereo == 3)
	sample_handler = sample16ss_handler;
    else
	sample_handler = currprefs.sound_stereo ? sample16s_handler : sample16_handler;

    write_log ("DS driver '%s'/%d/%d bits/%d Hz/buffer %d/dist %d\n",
	sound_devices[currprefs.win32_soundcard],
	currprefs.sound_stereo,
	16, freq, max_sndbufsize, snd_configsize);
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
    clearbuffer();
}

void resume_sound (void)
{
    if (!paused)
	return;
    if (!have_sound)
	return;
    clearbuffer ();
    resume_audio_ds ();
}

void reset_sound (void)
{
    if (!have_sound)
	return;
    clearbuffer ();
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

    mult = (1000.0 + currprefs.sound_adjust + v);
    if ((currprefs.gfx_vsync && currprefs.gfx_afullscreen) || (avioutput_audio && !compiled_code)) {
	vsynctime = vsynctime_orig;
	scaled_sample_evtime = (long)(((double)scaled_sample_evtime_orig) * mult / 1000.0);
    } else if (compiled_code || currprefs.m68k_speed != 0) {
	vsynctime = (long)(((double)vsynctime_orig) * mult / 1000.0);
	scaled_sample_evtime = scaled_sample_evtime_orig;
    } else {
	vsynctime = vsynctime_orig * mult / 1000.0;
	scaled_sample_evtime = scaled_sample_evtime_orig;
    }
}

#define SND_STATUSCNT 10

static int safedist;

void restart_sound_buffer(void)
{
    DWORD playpos, safed;
    HRESULT hr;

    if (waiting_for_buffer != -1)
	return;
    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safed);
    if (FAILED(hr))
	return;
    writepos = snd_writeoffset + safedist + playpos;
    if (writepos >= dsoundbuf)
        writepos -= dsoundbuf;
}

static void finish_sound_buffer_ds (void)
{
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
	if (safedist < 0)
	    safedist += dsoundbuf;
	safedist += sndbufsize;
	snd_totalmaxoffset += safedist;
	snd_maxoffset += safedist;
	snd_writeoffset += safedist;
	waiting_for_buffer = -1;
	restart_sound_buffer();
	write_log("SOUND: safedist=%d snd_total=%d snd_max=%d snd_write=%d\n",
	    safedist, snd_totalmaxoffset, snd_maxoffset, snd_writeoffset);
    }

    hr = IDirectSoundBuffer_GetStatus (lpDSBsecondary, &status);
    if (FAILED(hr)) {
	write_log ("SOUND: GetStatus() failed: %s\n", DXError(hr));
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

	if (diff < safedist) {
	    hr = IDirectSoundBuffer_Lock (lpDSBsecondary, writepos, safedist, &b1, &s1, &b2, &s2, 0);
	    if (SUCCEEDED(hr)) {
		memset (b1, 0, s1);
		if (b2)
		    memset (b2, 0, s2);
		IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);
	    }
	    gui_data.sndbuf_status = -1;
	    statuscnt = SND_STATUSCNT;
	    writepos += safedist;
	    if (writepos >= dsoundbuf)
		writepos -= dsoundbuf;
	    break;
	}

	if (diff > snd_totalmaxoffset) {
	    gui_data.sndbuf_status = 2;
	    statuscnt = SND_STATUSCNT;
	    writepos = safepos + snd_writeoffset;
	    if (writepos >= dsoundbuf)
		writepos -= dsoundbuf;
	    diff = snd_writeoffset;
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
    skipmode = pow (m < 0 ? -m : m, EXP) / 2.0;

    if (m < 0)
	skipmode = -skipmode;
    if (skipmode < -ADJUST_SIZE)
	skipmode = -ADJUST_SIZE;
    if (skipmode > ADJUST_SIZE)
	skipmode = ADJUST_SIZE;

    if (sound_debug) {
	static int tfprev;
	if (tfprev != timeframes && !(timeframes % 10)) {
	    write_log ("b=%5d,%5d,%5d,%5d d=%5d vd=%5.0f s=%+02.1f\n",
		sndbufsize, snd_configsize, max_sndbufsize, dsoundbuf, diff, vdiff, skipmode);
	}
	tfprev = timeframes;
    }

    writepos += sndbufsize;
    if (writepos >= dsoundbuf)
	writepos -= dsoundbuf;

    sound_setadjust (skipmode);

    gui_data.sndbuf = vdiff * 1000 / snd_maxoffset;
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

int sound_calibrate (HWND hwnd, struct uae_prefs *p)
{
    HWND old = hMainWnd;
    int pct = 100 * 10;

    hMainWnd = hwnd;
    currprefs.sound_freq = p->sound_freq;
    currprefs.sound_stereo = p->sound_stereo;
    if (open_sound ()) {
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	pct = calibrate ();
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	close_sound ();
    }
    if (pct > 995 && pct < 1005)
	pct = 1000;
    hMainWnd = old;
    return pct;
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
