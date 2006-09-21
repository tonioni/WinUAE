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
#include <process.h>

#include <math.h>

#define ADJUST_SIZE 10
#define EXP 1.3

static int sndbuffer_slot_size = 4000;
static int sndbuffer_slot_num = 16;

//#define SOUND_DEBUG

static int obtainedfreq;
static int have_sound;
static int paused;
static int mute;

static int sndbuffer_size;

uae_u16 sndbuffer[262144];
uae_u16 *sndbufpt;
int sndbufsize;

static uae_sem_t sound_sem, sound_init_sem;

#define MAX_SOUND_DEVICES 10

static char *sound_devices[MAX_SOUND_DEVICES];
GUID sound_device_guid[MAX_SOUND_DEVICES];
static int num_sound_devices;

static LPDIRECTSOUND8 lpDS;
static LPDIRECTSOUNDBUFFER lpDSBprimary;
static LPDIRECTSOUNDBUFFER8 lpDSBsecondary;

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
	    scaled_sample_evtime_orig = (unsigned long)(312.0 * 50 * CYCLE_UNIT / (obtainedfreq  / 227.5));
	}
	scaled_sample_evtime = scaled_sample_evtime_orig;
    }
}

static int prevplayslot, writeslot, writeoffset;
uae_sem_t audiosem;

static void clearbuffer (void)
{
    void *buffer;
    DWORD size;
    HRESULT hr;

    uae_sem_wait(&audiosem);
    prevplayslot = 0;
    writeslot = sndbuffer_slot_num / 3 + 2;
    memset (sndbuffer, 0, sndbuffer_size);
    hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, sndbuffer_size, &buffer, &size, NULL, NULL, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (lpDSBsecondary);
	hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, sndbuffer_size, &buffer, &size, NULL, NULL, 0);
    }
    if (FAILED(hr)) {
	write_log ("SOUND: failed to Lock sound buffer (clear): %s\n", DXError (hr));
        uae_sem_post(&audiosem);
	return;
    }
    memset (buffer, 0, size);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, buffer, size, NULL, 0);
    uae_sem_post(&audiosem);
}

static void pause_audio_ds (void)
{
    IDirectSoundBuffer_Stop (lpDSBsecondary);
    clearbuffer ();
}

static void resume_audio_ds (void)
{
    HRESULT hr;
    
    paused = 0;
    clearbuffer ();
    hr = IDirectSoundBuffer_SetCurrentPosition (lpDSBsecondary, 0);
    hr = IDirectSoundBuffer_Play (lpDSBsecondary, 0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr))
	write_log ("SOUND: play failed: %s\n", DXError (hr));
}

static int restore (DWORD hr)
{
    if (hr != DSERR_BUFFERLOST)
	return 0;
#ifdef SOUND_DEBUG
    write_log ("sound buffer lost\n");
#endif
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

    if (!QueryPerformanceFrequency(&qpf)) {
	write_log ("SOUND: no QPF, can't calibrate\n");
	return 100 * 10;
    }
    pos = 1000;
    pause_audio_ds ();
    resume_audio_ds ();
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
	    tpos += sndbuffer_size - lastpos + pos;
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

static volatile int notificationthread_mode;
static HANDLE notifyevent, notifyevent2;

static unsigned __stdcall notifythread(void *data)
{
    notificationthread_mode = 2;
    write_log("SOUND: notificationthread running\n");
    while (notificationthread_mode == 2) {
	HRESULT hr;
	DWORD status, playpos, safepos;
	int slot, cnt, incoming, usedslot;
	void *b1, *b2;
	DWORD s1, s2;
	double skipmode;

	WaitForSingleObject(notifyevent, INFINITE);
	if (!have_sound)
	    continue;

	uae_sem_wait (&audiosem);
	hr = IDirectSoundBuffer_GetStatus (lpDSBsecondary, &status);
	if (FAILED(hr)) {
	    write_log ("SOUND: GetStatus() failed: %s\n", DXError(hr));
	    goto nreset;
	}
	if (status & DSBSTATUS_BUFFERLOST) {
	    write_log ("SOUND: buffer lost\n");
	    restore (DSERR_BUFFERLOST);
	    goto nreset;
	}
	if ((status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) != (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) {
	    write_log ("SOUND: status = %08.8X\n", status);
	    restore (DSERR_BUFFERLOST);
	    goto nreset;
	}
	hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safepos);
	if (FAILED(hr)) {
	    write_log ("SOUND: GetCurrentPosition failed: %s\n", DXError (hr));
	    restore (hr);
	    goto nreset;
	}
	slot = playpos / sndbuffer_slot_size;

	usedslot = prevplayslot;
	while (slot != prevplayslot) {
	    if (writeslot == prevplayslot) {
		write_log ("UF: missed\n");
		writeslot = prevplayslot + 1;
		writeslot %= sndbuffer_slot_num;
		writeoffset = 0;
	    }
	    prevplayslot++;
	    prevplayslot %= sndbuffer_slot_num;
	}

	incoming = (uae_u8*)sndbufpt - (uae_u8*)sndbuffer;
	//write_log ("%d.%d.%d.%d ", playslot, slot, writeslot, incoming);
	//write_log ("%d ", incoming);

	// handle possible buffer underflow
	if (slot == writeslot) {
	    writeslot++;
	    writeslot %= sndbuffer_slot_num;
	    if (incoming <= sndbuffer_slot_size) {
		write_log ("UF: skipped\n");
		writeslot++;
		writeslot %= sndbuffer_slot_num;
		writeoffset = 0;
	    } else if (incoming < sndbuffer_slot_size + (sndbuffer_slot_size - writeoffset)) {
		write_log ("UF: miniskip\n");
		writeoffset = sndbuffer_slot_size - (incoming - sndbuffer_slot_size);
	    }
	}

	// copy new data if available
	if (incoming > 0) {
	    IDirectSoundBuffer_Lock (lpDSBsecondary, writeslot * sndbuffer_slot_size + writeoffset, incoming,
		&b1, &s1, &b2, &s2, 0);
	    memcpy (b1, sndbuffer, s1);
	    if (b2)
		memcpy (b2, (uae_u8*)sndbuffer + s1, s2);
	    IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);
	    writeslot += (writeoffset + incoming) / sndbuffer_slot_size;
	    writeslot %= sndbuffer_slot_num;
	    writeoffset = (writeoffset + incoming) % sndbuffer_slot_size;
	}

	// clear already played slot(s)
	cnt = (usedslot >= slot ? sndbuffer_slot_num - usedslot + slot : slot - usedslot) - 1;
	if (0 && cnt > 0) {
	    IDirectSoundBuffer_Lock (lpDSBsecondary, usedslot * sndbuffer_slot_size, cnt * sndbuffer_slot_size,
	        &b1, &s1, &b2, &s2, 0);
	    if (FAILED(hr)) {
	        write_log ("SOUND: lock failed: %s (%d %d)\n", DXError (hr), usedslot, cnt);
	        goto nreset;
	    }
	    memset (b1, 0, s1);
	    if (b2)
	        memset (b2, 0, s2);
	    IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);
	}

	cnt = (writeslot > slot ? sndbuffer_slot_num - writeslot + slot : slot - writeslot) - 1;
	write_log ("%d ", cnt);
	if (cnt <= 0)
	    cnt = 0;
	sndbufsize = cnt * sndbuffer_slot_size;
	sndbufpt = sndbuffer;
	uae_sem_post(&audiosem);
	SetEvent(notifyevent2);
        //sound_setadjust (-50);
	continue;
nreset:
	write_log ("SOUND: reset\n");
	sndbufpt = sndbuffer;
	sndbufsize = sndbuffer_size;
	clearbuffer();
	uae_sem_post(&audiosem);
	SetEvent(notifyevent2);
	continue;
    }
    write_log("SOUND: notificationthread exiting\n");
    notificationthread_mode = 4;
    return 0;
}

#if 0

    DWORD playpos, safepos, status;
    HRESULT hr;
    void *b1, *b2;
    DWORD s1, s2;
    int diff;
    int counter = 1000;
    double vdiff, m, skipmode;

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

	if (savestate_state)
	    return;

	if (writepos >= playpos)
	    diff = writepos - playpos;
	else
	    diff = dsoundbuf - playpos + writepos;

	if (diff >= max_sndbufsize) {
	    writepos = safepos + snd_configsize;
	    if (writepos >= dsoundbuf)
		writepos -= dsoundbuf;
	    diff = snd_configsize;
	    break;
	}

	if (diff > max_sndbufsize * 6 / 8) {
	    sleep_millis_busy (1);
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
    memcpy (b1, sndbuffer, sndbufsize >= s1 ? s1 : sndbufsize);
    if (b2)
        memcpy (b2, (uae_u8*)sndbuffer + s1, sndbufsize - s1);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);

    vdiff = diff - snd_configsize;
    m = 100.0 * vdiff / max_sndbufsize;
    skipmode = pow (m < 0 ? -m : m, EXP)/ 10.0;

    if (m < 0) skipmode = -skipmode;
    if (skipmode < -ADJUST_SIZE) skipmode = -ADJUST_SIZE;
    if (skipmode > ADJUST_SIZE) skipmode = ADJUST_SIZE;

#ifdef SOUND_DEBUG
    if (!(timeframes % 10)) {
	write_log ("b=%5d,%5d,%5d,%5d diff=%5d vdiff=%5.0f vdiff2=%5d skip=%+02.1f\n",
	    sndbufsize, snd_configsize, max_sndbufsize, dsoundbuf, diff, vdiff, diff - snd_configsize, skipmode);
    }
#endif

    writepos += sndbufsize;
    if (writepos >= dsoundbuf)
	writepos -= dsoundbuf;

    sound_setadjust (skipmode);
}

#endif

static void close_audio_ds (void)
{
    uae_sem_wait (&audiosem);
    if (notificationthread_mode == 2) {
	notificationthread_mode = 3;
	SetEvent(notifyevent);
	while (notificationthread_mode == 3)
	    Sleep(10);
	notificationthread_mode = 0;
    }
    if (notifyevent)
	CloseHandle(notifyevent);
    notifyevent = NULL;
    if (notifyevent2)
	CloseHandle(notifyevent2);
    notifyevent2 = NULL;

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
    uae_sem_post (&audiosem);
}

static int open_audio_ds (int size)
{
    int i;
    HRESULT hr;
    DSBUFFERDESC sound_buffer;
    DSCAPS DSCaps;
    DSBCAPS DSBCaps;
    WAVEFORMATEX wavfmt;
    int freq = currprefs.sound_freq;
    LPDIRECTSOUNDBUFFER pdsb;
    DSBPOSITIONNOTIFY *dsbpn;
    LPDIRECTSOUNDNOTIFY8 lpDsNotify;
    HANDLE thread;
    unsigned ttid;
    
    enumerate_sound_devices (0);
    if (currprefs.sound_stereo == 3) {
	size <<= 3;
    } else {
	size <<= 1;
	if (currprefs.sound_stereo)
	    size <<= 1;
    }
    sndbuffer_size = sndbuffer_slot_size * sndbuffer_slot_num;
    sndbufsize = sndbuffer_size;

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

    hr = IDirectSound_SetCooperativeLevel (lpDS, hMainWnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        write_log ("SOUND: Can't set cooperativelevel: %s\n", DXError (hr));
        goto error;
    }

    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwBufferBytes = sndbuffer_size;
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
        write_log ("SOUND: QueryInterface(DirectSoundBuffer8) failure: %s\n", DXError (hr));
	goto error;
    }
    IDirectSound_Release(pdsb);

    hr = IDirectSoundBuffer_SetFormat (lpDSBprimary, &wavfmt);
    if (FAILED(hr))  {
        write_log ("SOUND: Primary SetFormat() failure: %s\n", DXError (hr));
        goto error;
    }

    hr = IDirectSound_QueryInterface(lpDSBsecondary, &IID_IDirectSoundNotify8, (LPVOID*)&lpDsNotify);
    if (FAILED(hr)) {
	write_log ("SOUND: QueryInterface(DirectSoundNotify8) failed: %s\n", DXError (hr));
	goto error;
    }
    uae_sem_init(&audiosem, 0, 1);
    notifyevent = CreateEvent(NULL, FALSE, FALSE, NULL);
    notifyevent2 = CreateEvent(NULL, FALSE, FALSE, NULL);
    dsbpn = xmalloc (sizeof (DSBPOSITIONNOTIFY) * sndbuffer_slot_num);
    for (i = 0; i < sndbuffer_slot_num; i++) {
	dsbpn[i].dwOffset = i * sndbuffer_slot_size;
	dsbpn[i].hEventNotify = notifyevent;
    }
    IDirectSoundNotify_SetNotificationPositions(lpDsNotify, sndbuffer_slot_num, dsbpn);
    IDirectSound_Release(lpDsNotify);

    notificationthread_mode = 1;
    thread = (HANDLE)_beginthreadex(NULL, 0, notifythread, NULL, 0, &ttid);
    if (thread == NULL) {
	write_log(" SOUND: NotificationThread failed to start: %d\n", GetLastError());
	goto error;
    }
    SetThreadPriority (thread, THREAD_PRIORITY_HIGHEST);
    SetThreadPriority (thread, THREAD_PRIORITY_TIME_CRITICAL);

    setvolume ();
    clearbuffer ();

    init_sound_table16 ();
    if (currprefs.sound_stereo == 3)
	sample_handler = sample16ss_handler;
    else
	sample_handler = currprefs.sound_stereo ? sample16s_handler : sample16_handler;

    write_log ("DS driver '%s'/%d/%d bits/%d Hz/buffer %d*%d=%d\n",
	sound_devices[currprefs.win32_soundcard],
	currprefs.sound_stereo,
	16, freq, sndbuffer_slot_num, sndbuffer_slot_size, sndbuffer_size);
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
    have_sound = 0;
    pause_sound ();
    close_audio_ds ();
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
	vsynctime = (long)(((double)vsynctime_orig) * mult / 1000.0);
	scaled_sample_evtime = scaled_sample_evtime_orig;
    }
}

static void finish_sound_buffer_ds (void)
{
    uae_sem_post(&audiosem);
    ResetEvent(notifyevent2);
    for (;;) {
	SetEvent(notifyevent);
	WaitForSingleObject(notifyevent2, INFINITE);
	if (sndbufpt == sndbuffer && sndbufsize > 0)
	    break;
    }
    uae_sem_wait(&audiosem);
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
