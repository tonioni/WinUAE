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
#include "audio.h"
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

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <ks.h>
#include <ksmedia.h>

#include <al.h>
#include <alc.h>

#include <portaudio.h>

#include <math.h>

#define ADJUST_SIZE 30
#define EXP 2.1

#define ADJUST_VSSIZE 15
#define EXPVS 1.7

int sound_debug = 0;
int sound_mode_skip = 0;

static int devicetype;
static int obtainedfreq;
static int have_sound;
static int paused;
static int mute;

#define SND_MAX_BUFFER2 524288
#define SND_MAX_BUFFER 8192

uae_u16 sndbuffer[SND_MAX_BUFFER];
uae_u16 *sndbufpt;
int sndbufsize;

static int max_sndbufsize, snd_configsize, dsoundbuf, samplesize;
static int snd_writeoffset, snd_maxoffset, snd_totalmaxoffset_uf, snd_totalmaxoffset_of;
static int waiting_for_buffer;

static uae_sem_t sound_sem, sound_init_sem;

struct sound_device sound_devices[MAX_SOUND_DEVICES];
struct sound_device record_devices[MAX_SOUND_DEVICES];
static int num_sound_devices, num_record_devices;

// directsound

static LPDIRECTSOUND8 lpDS;
static LPDIRECTSOUNDBUFFER8 lpDSBsecondary;
static DWORD writepos;


// openal

#define AL_BUFFERS 2
static ALCdevice *al_dev;
static ALCcontext *al_ctx;
static ALuint al_Buffers[AL_BUFFERS];
static ALuint al_Source;
static int al_toggle;
static DWORD al_format;
static uae_u8 *al_bigbuffer;
static int al_bufsize, al_offset;


// portaudio

static volatile int patoggle;
static volatile int pacounter;
static uae_u8 *pasoundbuffer[2];
static int pasndbufsize;
static int paframesperbuffer;
static PaStream *pastream;
static HANDLE paevent;


int setup_sound (void)
{
    sound_available = 1;
    return 1;
}

static int isvsync(void)
{
    return currprefs.gfx_avsync && currprefs.gfx_afullscreen;
}

int scaled_sample_evtime_orig;
static int lastfreq;
void update_sound (int freq)
{
    if (freq < 0)
	freq = lastfreq;
    lastfreq = freq;
    if (have_sound) {
	if (isvsync () || currprefs.chipset_refreshrate) {
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

static void clearbuffer_ds (void)
{
    void *buffer;
    DWORD size;

    HRESULT hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, dsoundbuf, &buffer, &size, NULL, NULL, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (lpDSBsecondary);
	hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, dsoundbuf, &buffer, &size, NULL, NULL, 0);
    }
    if (FAILED (hr)) {
	write_log ("SOUND: failed to Lock sound buffer (clear): %s\n", DXError (hr));
	return;
    }
    memset (buffer, 0, size);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, buffer, size, NULL, 0);
}

static void clearbuffer (void)
{
    if (devicetype == SOUND_DEVICE_DS)
	clearbuffer_ds ();
}

static void pause_audio_ds (void)
{
    HRESULT hr;

    waiting_for_buffer = 0;
    hr = IDirectSoundBuffer_Stop (lpDSBsecondary);
    if (FAILED (hr))
	write_log ("SOUND: DirectSoundBuffer_Stop failed, %s\n", DXError(hr));
    hr = IDirectSoundBuffer_SetCurrentPosition (lpDSBsecondary, 0);
    if (FAILED (hr))
	write_log ("SOUND: DirectSoundBuffer_SetCurretPosition failed, %s\n", DXError (hr));
    clearbuffer ();
}
static void resume_audio_ds (void)
{
    paused = 0;
    clearbuffer ();
    waiting_for_buffer = 1;
}
static void pause_audio_pa (void)
{
    PaError err = Pa_StopStream (pastream);
    if (err != paNoError)
	write_log ("SOUND: Pa_StopStream() error %d (%s)\n", err, Pa_GetErrorText (err));
}
static void resume_audio_pa (void)
{
    PaError err = Pa_StartStream (pastream);
    if (err != paNoError)
	write_log ("SOUND: Pa_StartStream() error %d (%s)\n", err, Pa_GetErrorText (err));
    paused = 0;
}
static void pause_audio_al (void)
{
    waiting_for_buffer = 0;
    alSourcePause (al_Source);
}
static void resume_audio_al (void)
{
    waiting_for_buffer = 1;
    al_offset = 0;
}

static int restore_ds (DWORD hr)
{
    if (hr != DSERR_BUFFERLOST)
	return 0;
    if (sound_debug)
	write_log ("SOUND: sound buffer lost\n");
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
    QueryPerformanceCounter (&qpfc);
}
static double getqpf (void)
{
    LARGE_INTEGER qpfc2;
    QueryPerformanceCounter (&qpfc2);
    return (qpfc2.QuadPart - qpfc.QuadPart) / (qpf.QuadPart / 1000.0);
}

static void close_audio_ds (void)
{
    if (lpDSBsecondary)
	IDirectSound_Release (lpDSBsecondary);
    lpDSBsecondary = 0;
#ifdef USE_PRIMARY_BUFFER
    if (lpDSBprimary)
	IDirectSound_Release (lpDSBprimary);
    lpDSBprimary = 0;
#endif
    if (lpDS) {
	IDirectSound_Release (lpDS);
	write_log ("SOUND: DirectSound driver freed\n");
    }
    lpDS = 0;
}

extern HWND hMainWnd;
extern void setvolume_ahi(LONG);
void set_volume (int volume, int mute)
{
    if (devicetype == SOUND_DEVICE_AL) {
	float vol = 0.0;
	if (volume < 100 && !mute)
	    vol = (100 - volume) / 100.0;
	alSourcef (al_Source, AL_GAIN, vol);
    } else if (devicetype == SOUND_DEVICE_DS) {
	HRESULT hr;
	LONG vol = DSBVOLUME_MIN;
	if (volume < 100 && !mute)
	    vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (1 - volume / 100.0)));
	hr = IDirectSoundBuffer_SetVolume (lpDSBsecondary, vol);
	if (FAILED (hr))
	    write_log ("SOUND: SetVolume(%d) failed: %s\n", vol, DXError (hr));
	setvolume_ahi (vol);
    }
}

static void recalc_offsets (void)
{
    snd_writeoffset = max_sndbufsize * 5 / 8;
    snd_maxoffset = max_sndbufsize;
    snd_totalmaxoffset_of = max_sndbufsize + (dsoundbuf - max_sndbufsize) * 3 / 9;
    snd_totalmaxoffset_uf = max_sndbufsize + (dsoundbuf - max_sndbufsize) * 7 / 9;
}

const static GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,
    {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
#define KSAUDIO_SPEAKER_QUAD_SURROUND   (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
					 SPEAKER_SIDE_LEFT  | SPEAKER_SIDE_RIGHT)

static struct dsaudiomodes supportedmodes[16];

DWORD fillsupportedmodes (LPDIRECTSOUND8 lpDS, int freq, struct dsaudiomodes *dsam)
{
    DWORD speakerconfig;
    DSBUFFERDESC sound_buffer;
    WAVEFORMATEXTENSIBLE wavfmt;
    LPDIRECTSOUNDBUFFER pdsb;
    HRESULT hr;
    int ch, round, mode, skip;
    DWORD rn[4];

    mode = 2;
    dsam[0].ch = 1;
    dsam[0].ksmode = 0;
    dsam[1].ch = 2;
    dsam[1].ksmode = 0;
    if (FAILED (IDirectSound8_GetSpeakerConfig (lpDS, &speakerconfig)))
	speakerconfig = DSSPEAKER_STEREO;

    memset (&wavfmt, 0, sizeof (WAVEFORMATEXTENSIBLE));
    wavfmt.Format.nSamplesPerSec = freq;
    wavfmt.Format.wBitsPerSample = 16;
    wavfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
    wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    wavfmt.Samples.wValidBitsPerSample = 16;
    for (ch = 4; ch <= 6; ch+= 2) {
	wavfmt.Format.nChannels = ch;
	wavfmt.Format.nBlockAlign = wavfmt.Format.wBitsPerSample / 8 * wavfmt.Format.nChannels;
	wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;
	if (ch == 6) {
	    rn[0] = KSAUDIO_SPEAKER_5POINT1;
	    rn[1] = KSAUDIO_SPEAKER_5POINT1_SURROUND;
	    rn[2] = 0;
	} else {
	    rn[0] = KSAUDIO_SPEAKER_QUAD;
	    rn[1] = KSAUDIO_SPEAKER_QUAD_SURROUND;
	    rn[2] = KSAUDIO_SPEAKER_SURROUND;
	    rn[3] = 0;
	}
	skip = sound_mode_skip;
	for (round = 0; rn[round]; round++) {
	    if (skip > 0 && rn[round + 1] != 0) {
		skip--;
		continue;
	    }
	    wavfmt.dwChannelMask = rn[round];
	    memset (&sound_buffer, 0, sizeof (sound_buffer));
	    sound_buffer.dwSize = sizeof (sound_buffer);
	    sound_buffer.dwBufferBytes = dsoundbuf;
	    sound_buffer.lpwfxFormat = &wavfmt.Format;
	    sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
	    sound_buffer.dwFlags |= DSBCAPS_CTRLVOLUME;
	    sound_buffer.guid3DAlgorithm = GUID_NULL;
	    hr = IDirectSound_CreateSoundBuffer (lpDS, &sound_buffer, &pdsb, NULL);
	    if (SUCCEEDED (hr)) {
		IDirectSound_Release (pdsb);
		dsam[mode].ksmode = rn[round];
		dsam[mode].ch = ch;
		mode++;
	    }
	}
    }
    return speakerconfig;
}


static void finish_sound_buffer_pa (void)
{
    static int opacounter;

    while (opacounter == pacounter && pastream && !paused)
	WaitForSingleObject (paevent, 10);
    ResetEvent (paevent);
    opacounter = pacounter;
    memcpy (pasoundbuffer[patoggle], sndbuffer, sndbufsize);
}

static int portAudioCallback (const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData)
{
    memcpy (outputBuffer, pasoundbuffer[patoggle], sndbufsize);
    patoggle ^= 1;
    pacounter++;
    SetEvent (paevent);
    return paContinue;
}

static void close_audio_pa (void)
{
    int i;

    if (pastream)
	Pa_CloseStream (pastream);
    pastream = NULL;
    for (i = 0; i < 2; i++) {
	xfree (pasoundbuffer[i]);
	pasoundbuffer[i] = NULL;
    }
    if (paevent) {
	SetEvent (paevent);
	CloseHandle (paevent);
    }
    paevent = NULL;
}

static int open_audio_pa (int size)
{
    int i;
    int freq = currprefs.sound_freq;
    int ch = get_audio_nativechannels ();
    int dev = sound_devices[currprefs.win32_soundcard].panum;
    const PaDeviceInfo *di;
    PaStreamParameters p;
    PaError err;

    paframesperbuffer = size;
    sndbufsize = size * ch * 2;
    devicetype = SOUND_DEVICE_PA;
    memset (&p, 0, sizeof p);
    p.channelCount = ch;
    p.device = dev;
    p.hostApiSpecificStreamInfo = NULL;
    p.sampleFormat = paInt16;
    p.suggestedLatency = Pa_GetDeviceInfo (dev)->defaultLowOutputLatency;
    p.hostApiSpecificStreamInfo = NULL; 
    for (;;) {
	err = Pa_IsFormatSupported (NULL, &p, freq);
	if (err == paFormatIsSupported)
	    break;
	di = Pa_GetDeviceInfo (dev);
	if (freq < 48000) {
	    freq = 48000;
	    err = Pa_IsFormatSupported (NULL, &p, freq);
	    if (err == paFormatIsSupported) {
		currprefs.sound_freq = changed_prefs.sound_freq = freq;
		break;
	    }
	}
	if (freq != di->defaultSampleRate) {
	    freq = di->defaultSampleRate;
	    err = Pa_IsFormatSupported (NULL, &p, freq);
	    if (err == paFormatIsSupported) {
		currprefs.sound_freq = changed_prefs.sound_freq = freq;
		break;
	    }
	}
	write_log ("SOUND: sound format not supported\n");
	goto end;
    }
    err = Pa_OpenStream (&pastream, NULL, &p, freq, paframesperbuffer, paNoFlag, portAudioCallback, NULL);
    if (err != paNoError) {
	write_log ("SOUND: Pa_OpenStream() error %d (%s)\n", err, Pa_GetErrorText (err));
	goto end;
    }
    paevent = CreateEvent (NULL, FALSE, FALSE, NULL);
    for (i = 0; i < 2; i++)
	pasoundbuffer[i] = xcalloc (sndbufsize, 1);
    return 1;
end:
    pastream = NULL;
    close_audio_pa ();
    return 0;
}

static void close_audio_al (void)
{
    int i;

    alDeleteSources (1, &al_Source);
    al_Source = 0;
    alDeleteBuffers (AL_BUFFERS, al_Buffers);
    alcMakeContextCurrent (NULL);
    if (al_ctx)
	alcDestroyContext (al_ctx);
    al_ctx = NULL;
    if (al_dev)
        alcCloseDevice (al_dev);
    al_dev = NULL;
    for (i = 0; i < AL_BUFFERS; i++) {
	al_Buffers[i] = 0;
    }
    xfree (al_bigbuffer);
    al_bigbuffer = NULL;
}

static int open_audio_al (int size)
{
    int freq = currprefs.sound_freq;
    int ch = get_audio_nativechannels ();

    devicetype = SOUND_DEVICE_AL;
    size *= ch * 2;
    sndbufsize = size / 8;
    if (sndbufsize > SND_MAX_BUFFER)
	sndbufsize = SND_MAX_BUFFER;
    al_bufsize = size;
    al_bigbuffer = xcalloc (al_bufsize, 1);
    al_dev = alcOpenDevice (sound_devices[currprefs.win32_soundcard].alname);
    if (!al_dev)
	goto error;
    al_ctx = alcCreateContext (al_dev, NULL);
    if (!al_ctx)
	goto error;
    alcMakeContextCurrent (al_ctx);
    alGenBuffers (AL_BUFFERS, al_Buffers);
    alGenSources (1, &al_Source);
    al_toggle = 0;
    al_format = 0;
    switch (ch)
    {
	case 1:
	al_format = AL_FORMAT_MONO16;
	break;
	case 2:
	al_format = AL_FORMAT_STEREO16;
	break;
	case 4:
	al_format = alGetEnumValue ("AL_FORMAT_QUAD16");
	break;
	case 6:
	al_format = alGetEnumValue ("AL_FORMAT_51CHN16");
	break;
    }
    if (al_format == 0)
	goto error;

    write_log ("SOUND: %08X,CH=%d,FREQ=%d '%s' buffer %d (%d)\n",
	    al_format, ch, freq, sound_devices[currprefs.win32_soundcard].alname,
	    sndbufsize, al_bufsize);
    return 1;

error:
    close_audio_al ();
    return 0;
}

static int open_audio_ds (int size)
{
    HRESULT hr;
    DSBUFFERDESC sound_buffer;
    DSCAPS DSCaps;
    WAVEFORMATEXTENSIBLE wavfmt;
    LPDIRECTSOUNDBUFFER pdsb;
    int freq = currprefs.sound_freq;
    int ch = get_audio_nativechannels ();
    int round, i;
    DWORD speakerconfig;

    devicetype = SOUND_DEVICE_DS;
    size *= ch * 2;
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

    recalc_offsets ();

    hr = DirectSoundCreate8 (&sound_devices[currprefs.win32_soundcard].guid, &lpDS, NULL);
    if (FAILED (hr))  {
	write_log ("SOUND: DirectSoundCreate8() failure: %s\n", DXError (hr));
	return 0;
    }

    hr = IDirectSound_SetCooperativeLevel (lpDS, hMainWnd, DSSCL_PRIORITY);
    if (FAILED (hr)) {
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
	    write_log ("SOUND: minimum supported frequency: %d\n", minfreq);
	}
	if (maxfreq < freq && freq > 44100) {
	    freq = maxfreq;
	    changed_prefs.sound_freq = currprefs.sound_freq = freq;
	    write_log ("SOUND: maximum supported frequency: %d\n", maxfreq);
	}
    }

    speakerconfig = fillsupportedmodes (lpDS, freq, supportedmodes);
    write_log ("SOUND: %08X ", speakerconfig);
    for (i = 0; supportedmodes[i].ch; i++)
	write_log ("%d:%08X ", supportedmodes[i].ch, supportedmodes[i].ksmode);
    write_log ("\n");

    for (round = 0; supportedmodes[round].ch; round++) {
	DWORD ksmode = 0;

	pdsb = NULL;
	memset (&wavfmt, 0, sizeof (WAVEFORMATEXTENSIBLE));
	wavfmt.Format.nChannels = ch;
	wavfmt.Format.nSamplesPerSec = freq;
	wavfmt.Format.wBitsPerSample = 16;
	if (supportedmodes[round].ch != ch)
	    continue;

	if (ch <= 2) {
	    wavfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
	} else {
	    wavfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	    ksmode = supportedmodes[round].ksmode;
	    wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
	    wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	    wavfmt.Samples.wValidBitsPerSample = 16;
	    wavfmt.dwChannelMask = ksmode;
	}
	wavfmt.Format.nBlockAlign = wavfmt.Format.wBitsPerSample / 8 * wavfmt.Format.nChannels;
	wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;

	samplesize = ch * 2;
	write_log ("SOUND: %08X,CH=%d,FREQ=%d '%s' buffer %d (%d), dist %d\n",
	    ksmode, ch, freq, sound_devices[currprefs.win32_soundcard].name,
	    max_sndbufsize / samplesize, max_sndbufsize, snd_configsize / samplesize);

	memset (&sound_buffer, 0, sizeof (sound_buffer));
	sound_buffer.dwSize = sizeof (sound_buffer);
	sound_buffer.dwBufferBytes = dsoundbuf;
	sound_buffer.lpwfxFormat = &wavfmt.Format;
	sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
	sound_buffer.dwFlags |= DSBCAPS_CTRLVOLUME | (ch >= 4 ? DSBCAPS_LOCHARDWARE : DSBCAPS_LOCSOFTWARE);
	sound_buffer.guid3DAlgorithm = GUID_NULL;

	hr = IDirectSound_CreateSoundBuffer (lpDS, &sound_buffer, &pdsb, NULL);
	if (SUCCEEDED (hr))
	    break;
	if (sound_buffer.dwFlags & DSBCAPS_LOCHARDWARE) {
	    HRESULT hr2 = hr;
	    sound_buffer.dwFlags &= ~DSBCAPS_LOCHARDWARE;
	    sound_buffer.dwFlags |=  DSBCAPS_LOCSOFTWARE;
	    hr = IDirectSound_CreateSoundBuffer (lpDS, &sound_buffer, &pdsb, NULL);
	    if (SUCCEEDED(hr)) {
		//write_log ("SOUND: Couldn't use hardware buffer (switched to software): %s\n", DXError (hr2));
		break;
	    }
	}
	write_log ("SOUND: Secondary CreateSoundBuffer() failure: %s\n", DXError (hr));
    }

    if (pdsb == NULL)
	goto error;
    hr = IDirectSound_QueryInterface (pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&lpDSBsecondary);
    if (FAILED (hr))  {
	write_log ("SOUND: Secondary QueryInterface() failure: %s\n", DXError (hr));
	goto error;
    }
    IDirectSound_Release (pdsb);
    clearbuffer ();

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

    enumerate_sound_devices ();
    if (sound_devices[currprefs.win32_soundcard].type == SOUND_DEVICE_AL)
	ret = open_audio_al (size);
    else if (sound_devices[currprefs.win32_soundcard].type == SOUND_DEVICE_DS)
	ret = open_audio_ds (size);
    else if (sound_devices[currprefs.win32_soundcard].type == SOUND_DEVICE_PA)
	ret = open_audio_pa (size);
    if (!ret)
	return 0;

    set_volume (currprefs.sound_volume, mute);
    init_sound_table16 ();
    if (get_audio_amigachannels() == 4)
	sample_handler = sample16ss_handler;
    else
	sample_handler = get_audio_ismono () ? sample16_handler : sample16s_handler;

    obtainedfreq = currprefs.sound_freq;

    have_sound = 1;
    sound_available = 1;
    update_sound (fake_vblank_hz);
    sndbufpt = sndbuffer;
    driveclick_init ();

    return 1;
}

void close_sound (void)
{
    gui_data.sndbuf = 0;
    gui_data.sndbuf_status = 3;
    if (! have_sound)
	return;
    pause_sound ();
    if (devicetype == SOUND_DEVICE_AL)
	close_audio_al ();
    else if (devicetype == SOUND_DEVICE_DS)
	close_audio_ds ();
    else if (devicetype == SOUND_DEVICE_PA)
	close_audio_pa ();
    have_sound = 0;
}

int init_sound (void)
{
    gui_data.sndbuf_status = 3;
    gui_data.sndbuf = 0;
    if (!sound_available)
	return 0;
    if (currprefs.produce_sound <= 1)
	return 0;
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
    if (devicetype == SOUND_DEVICE_AL)
	pause_audio_al ();
    else if (devicetype == SOUND_DEVICE_DS)
	pause_audio_ds ();
    else if (devicetype == SOUND_DEVICE_PA)
	pause_audio_pa ();
}

void resume_sound (void)
{
    if (!paused)
	return;
    if (!have_sound)
	return;
    if (devicetype == SOUND_DEVICE_AL)
	resume_audio_al ();
    else if (devicetype == SOUND_DEVICE_DS)
	resume_audio_ds ();
    else if (devicetype == SOUND_DEVICE_PA)
	resume_audio_pa ();
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

    mult = (1000.0 + v);
    if (isvsync () || (avioutput_audio && !compiled_code)) {
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

void restart_sound_buffer (void)
{
    DWORD playpos, safed;
    HRESULT hr;

    if (devicetype != SOUND_DEVICE_DS)
	return;
    if (waiting_for_buffer != -1)
	return;
    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safed);
    if (FAILED (hr)) {
	write_log ("SOUND: DirectSoundBuffer_GetCurrentPosition failed, %s\n", DXError (hr));
	return;
    }
    writepos = safed + snd_writeoffset;
    if (writepos < 0)
	writepos += dsoundbuf;
    cf (writepos);
}

static int alcheck (int v)
{
    int err = alGetError ();
    if (err != AL_NO_ERROR) {
	int v1, v2, v3;
        alGetSourcei (al_Source, AL_BUFFERS_PROCESSED, &v1);
	alGetSourcei (al_Source, AL_BUFFERS_QUEUED, &v2);
	alGetSourcei (al_Source, AL_SOURCE_STATE, &v3);
	write_log ("OpenAL %d: error %d. PROC=%d QUEUE=%d STATE=%d\n", v, err, v1, v2, v3);
	write_log ("           %d %08x %08x %08x %d %d\n",
	    al_toggle, al_Buffers[al_toggle], al_format, al_bigbuffer, al_bufsize, currprefs.sound_freq);
	return 1;
    }
    return 0;
}

static void finish_sound_buffer_al (void)
{
    static int tfprev;
    static int statuscnt;
    int v, v2;
    double m, skipmode;

    if (!waiting_for_buffer)
	return;
    if (savestate_state)
	return;

    if (statuscnt > 0) {
	statuscnt--;
	if (statuscnt == 0)
	    gui_data.sndbuf_status = 0;
    }
    if (gui_data.sndbuf_status == 3)
	gui_data.sndbuf_status = 0;
    alGetError ();

    memcpy (al_bigbuffer + al_offset, sndbuffer, sndbufsize);
    al_offset += sndbufsize;
    if (al_offset >= al_bufsize) {
	ALuint tmp;
	alGetSourcei (al_Source, AL_BUFFERS_PROCESSED, &v);
	while (v == 0 && waiting_for_buffer < 0) {
	    sleep_millis (1);
	    alGetSourcei (al_Source, AL_SOURCE_STATE, &v);
	    if (v != AL_PLAYING)
		break;
	    alGetSourcei (al_Source, AL_BUFFERS_PROCESSED, &v);
	}

        alSourceUnqueueBuffers (al_Source, 1, &tmp);
	alGetError ();

//	write_log ("           %d %08x %08x %08x %d %d\n",
//	    al_toggle, al_Buffers[al_toggle], al_format, al_bigbuffer, al_bufsize, currprefs.sound_freq);

	alBufferData (al_Buffers[al_toggle], al_format, al_bigbuffer, al_bufsize, currprefs.sound_freq);
	alcheck(4);
	alSourceQueueBuffers (al_Source, 1, &al_Buffers[al_toggle]);
	alcheck(2);
	al_toggle++;
	if (al_toggle >= AL_BUFFERS)
	    al_toggle = 0;

	alGetSourcei (al_Source, AL_BUFFERS_QUEUED, &v2);
	alcheck(5);
	alGetSourcei (al_Source, AL_SOURCE_STATE, &v);
	alcheck(3);
	if (v != AL_PLAYING && v2 >= AL_BUFFERS) {
	    if (waiting_for_buffer > 0) {
		write_log ("AL SOUND PLAY!\n");
		alSourcePlay (al_Source);
		waiting_for_buffer = -1;
    		tfprev = timeframes + 10;
		tfprev = (tfprev / 10) * 10;
	    } else {
		gui_data.sndbuf_status = 2;
		statuscnt = SND_STATUSCNT;
		write_log ("AL underflow\n");
		clearbuffer ();
		waiting_for_buffer = 1;
	    }
	}
	al_offset = 0;
    }
    alcheck(1);

    alGetSourcei (al_Source, AL_SOURCE_STATE, &v);
    alcheck(6);
    if (v == AL_PLAYING) {
	alGetSourcei (al_Source, AL_BYTE_OFFSET, &v);
	alcheck(7);
	v -= al_offset;
	gui_data.sndbuf = 100 * v / sndbufsize;
	m = gui_data.sndbuf / 100.0;

	if (isvsync ()) {

	    skipmode = pow (m < 0 ? -m : m, EXP) / 8;
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
	    if ((0 || sound_debug) && !(tfprev % 10))
		write_log ("s=%+02.1f\n", skipmode);
	    tfprev = timeframes;
	    if (!avioutput_audio)
		sound_setadjust (skipmode);
	}
    }

    alcheck (0);
}

static void finish_sound_buffer_ds (void)
{
    static int tfprev;
    DWORD playpos, safepos, status;
    HRESULT hr;
    void *b1, *b2;
    DWORD s1, s2;
    int diff;
    int counter;
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
	if (FAILED (hr)) {
	    write_log ("SOUND: Play failed: %s\n", DXError (hr));
	    restore_ds (DSERR_BUFFERLOST);
	    waiting_for_buffer = 0;
	    return;
	}
	hr = IDirectSoundBuffer_SetCurrentPosition (lpDSBsecondary, 0);
	if (FAILED (hr)) {
	    write_log ("SOUND: 1st SetCurrentPosition failed: %s\n", DXError (hr));
	    restore_ds (DSERR_BUFFERLOST);
	    waiting_for_buffer = 0;
	    return;
	}
	/* there are crappy drivers that return PLAYCURSOR = WRITECURSOR = 0 without this.. */
	counter = 5000;
	for (;;) {
	    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safedist);
	    if (playpos > 0)
		break;
	    sleep_millis (1);
	    counter--;
	    if (counter < 0) {
		write_log ("SOUND: stuck?!?!\n");
		break;
	    }
	}
	write_log ("SOUND: %d = (%d - %d)\n", (safedist - playpos) / samplesize, safedist / samplesize, playpos / samplesize);
	recalc_offsets ();
	safedist -= playpos;
	if (safedist < 64)
	    safedist = 64;
	cf (safedist);
#if 0
	snd_totalmaxoffset_uf += safedist;
	cf (snd_totalmaxoffset_uf);
	snd_totalmaxoffset_of += safedist;
	cf (snd_totalmaxoffset_of);
	snd_maxoffset += safedist;
	cf (snd_maxoffset);
	snd_writeoffset += safedist;
	cf (snd_writeoffset);
#endif
	waiting_for_buffer = -1;
	restart_sound_buffer ();
	write_log ("SOUND: bs=%d w=%d max=%d tof=%d tuf=%d\n",
	    sndbufsize / samplesize, snd_writeoffset / samplesize,
	    snd_maxoffset / samplesize, snd_totalmaxoffset_of / samplesize,
	    snd_totalmaxoffset_uf / samplesize);
	tfprev = timeframes + 10;
	tfprev = (tfprev / 10) * 10;
    }

    counter = 5000;
    hr = IDirectSoundBuffer_GetStatus (lpDSBsecondary, &status);
    if (FAILED (hr)) {
	write_log ("SOUND: GetStatus() failed: %s\n", DXError (hr));
	restore_ds (DSERR_BUFFERLOST);
	return;
    }
    if (status & DSBSTATUS_BUFFERLOST) {
	write_log ("SOUND: buffer lost\n");
	restore_ds (DSERR_BUFFERLOST);
	return;
    }
    if ((status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) != (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) {
	write_log ("SOUND: status = %08X\n", status);
	restore_ds (DSERR_BUFFERLOST);
	return;
    }
    for (;;) {
	hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safepos);
	if (FAILED (hr)) {
	    restore_ds (hr);
	    write_log ("SOUND: GetCurrentPosition failed: %s\n", DXError (hr));
	    return;
	}
	if (writepos >= safepos)
	    diff = writepos - safepos;
	else
	    diff = dsoundbuf - safepos + writepos;

	if (diff < sndbufsize || diff > snd_totalmaxoffset_uf) {
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
	    cf (writepos);
	    diff = safedist;
	    break;
	}

	if (diff > snd_totalmaxoffset_of) {
	    gui_data.sndbuf_status = 2;
	    statuscnt = SND_STATUSCNT;
	    restart_sound_buffer ();
	    diff = snd_writeoffset;
	    write_log ("SOUND: underflow (%d %d)\n", diff / samplesize, snd_totalmaxoffset_of / samplesize);
	    break;
	}

	if (diff > snd_maxoffset) {
	    gui_data.sndbuf_status = 1;
	    statuscnt = SND_STATUSCNT;
	    sleep_millis (1);
	    counter--;
	    if (counter < 0) {
		write_log ("SOUND: sound system got stuck!?\n");
		restore_ds (DSERR_BUFFERLOST);
		return;
	    }
	    continue;
	}
	break;
    }

    hr = IDirectSoundBuffer_Lock (lpDSBsecondary, writepos, sndbufsize, &b1, &s1, &b2, &s2, 0);
    if (restore_ds (hr))
	return;
    if (FAILED (hr)) {
	write_log ("SOUND: lock failed: %s (%d %d)\n", DXError (hr), writepos / samplesize, sndbufsize / samplesize);
	return;
    }
    memcpy (b1, sndbuffer, s1);
    if (b2)
	memcpy (b2, (uae_u8*)sndbuffer + s1, s2);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);

    vdiff = diff - snd_writeoffset;
    m = 100.0 * vdiff / max_sndbufsize;

    if (isvsync ()) {

	skipmode = pow (m < 0 ? -m : m, EXP) / 8;
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
		sndbufsize / samplesize, snd_configsize / samplesize, max_sndbufsize / samplesize,
		dsoundbuf / samplesize, diff / samplesize, vdiff, skipmode);
	tfprev = timeframes;
	if (!avioutput_audio)
	    sound_setadjust (skipmode);
	gui_data.sndbuf = vdiff * 1000 / (snd_maxoffset - snd_writeoffset);
    }

    writepos += sndbufsize;
    cf (writepos);
}

static void channelswap (uae_s16 *sndbuffer, int len)
{
    int i;
    for (i = 0; i < len; i += 2) {
	uae_s16 t;
	t = sndbuffer[i];
	sndbuffer[i] = sndbuffer[i + 1];
	sndbuffer[i + 1] = t;
    }
}
static void channelswap6 (uae_s16 *sndbuffer, int len)
{
    int i;
    for (i = 0; i < len; i += 6) {
	uae_s16 t;
	t = sndbuffer[i + 0];
	sndbuffer[i + 0] = sndbuffer[i + 1];
	sndbuffer[i + 1] = t;
	t = sndbuffer[i + 4];
	sndbuffer[i + 4] = sndbuffer[i + 5];
	sndbuffer[i + 5] = t;
    }
}

void finish_sound_buffer (void)
{
    if (turbo_emulation)
	return;
    if (currprefs.sound_stereo_swap_paula) {
	if (get_audio_nativechannels () == 2 || get_audio_nativechannels () == 4)
	    channelswap ((uae_s16*)sndbuffer, sndbufsize / 2);
	else if (get_audio_nativechannels() == 6)
	    channelswap6 ((uae_s16*)sndbuffer, sndbufsize / 2);
    }
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

    if (devicetype == SOUND_DEVICE_AL)
	finish_sound_buffer_al ();
    else if (devicetype == SOUND_DEVICE_DS)
	finish_sound_buffer_ds ();
    else if (devicetype == SOUND_DEVICE_PA)
	finish_sound_buffer_pa ();
}

static BOOL CALLBACK DSEnumProc (LPGUID lpGUID, LPCTSTR lpszDesc, LPCTSTR lpszDrvName, LPVOID lpContext)
{
    struct sound_device *sd = lpContext;
    int i;
    
    for (i = 0; i < MAX_SOUND_DEVICES; i++) {
	if (sd[i].name == NULL)
	    break;
    }
    if (i >= MAX_SOUND_DEVICES)
	return TRUE;
    if (lpGUID != NULL)
	memcpy (&sd[i].guid, lpGUID, sizeof (GUID));
    sd[i].name = my_strdup (lpszDesc);
    sd[i].type = SOUND_DEVICE_DS;
    sd[i].cfgname = my_strdup (sd[i].name);
    return TRUE;
}

static void OpenALEnumerate (struct sound_device *sds, const char *pDeviceNames, const char *ppDefaultDevice, int skipdetect)
{
    struct sound_device *sd;
    while (pDeviceNames && *pDeviceNames) {
	ALCdevice *pDevice;
	const char *devname;
	int i, ok;
	for (i = 0; i < MAX_SOUND_DEVICES; i++) {
	    sd = &sds[i];
	    if (sd->name == NULL)
	        break;
	}
	if (i >= MAX_SOUND_DEVICES)
	    return;
        devname = pDeviceNames;
	if (ppDefaultDevice)
	    devname = ppDefaultDevice;
	ok = 0;
	if (!skipdetect) {
	    pDevice = alcOpenDevice (devname);
	    if (pDevice) {
		ALCcontext *context = alcCreateContext (pDevice, NULL);
		if (context) {
		    ALint iMajorVersion = 0, iMinorVersion = 0;
		    alcMakeContextCurrent (context);
		    alcGetIntegerv (pDevice, ALC_MAJOR_VERSION, sizeof (ALint), &iMajorVersion);
		    alcGetIntegerv (pDevice, ALC_MINOR_VERSION, sizeof (ALint), &iMinorVersion);
		    if (iMajorVersion > 1 || (iMajorVersion == 1 && iMinorVersion > 0)) {
			ok = 1;
		    }
		}
		alcMakeContextCurrent (NULL);
		alcDestroyContext (context);
	    }
	    alcCloseDevice (pDevice);
	} else {
	    ok = 1;
	}
	if (ok) {
	    sd->type = SOUND_DEVICE_AL;
	    if (ppDefaultDevice) {
	        char tmp[MAX_DPATH];
	        sprintf (tmp, "Default [%s]", devname);
	        sd->alname = my_strdup (ppDefaultDevice);
	        sd->name = my_strdup (tmp);
	    } else {
	        sd->alname = my_strdup (pDeviceNames);
	        sd->name = my_strdup (pDeviceNames);
	    }
	    sd->cfgname = my_strdup (sd->alname);
	}
	if (ppDefaultDevice)
	    ppDefaultDevice = NULL;
	else
	    pDeviceNames += strlen (pDeviceNames) + 1;
   }
}

static int isdllversion (const char *name, int version, int revision, int subver, int subrev)
{
    DWORD  dwVersionHandle, dwFileVersionInfoSize;
    LPVOID lpFileVersionData = NULL;
    int ok = 0;

    dwFileVersionInfoSize = GetFileVersionInfoSize (name, &dwVersionHandle);
    if (dwFileVersionInfoSize) {
	if (lpFileVersionData = xcalloc (1, dwFileVersionInfoSize)) {
	    if (GetFileVersionInfo (name, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData)) {
		VS_FIXEDFILEINFO *vsFileInfo = NULL;
		UINT uLen;
		if (VerQueryValue (lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen)) {
		    if (vsFileInfo) {
			uae_u64 v1 = ((uae_u64)vsFileInfo->dwProductVersionMS << 32) | vsFileInfo->dwProductVersionLS;
			uae_u64 v2 = ((uae_u64)version << 48) | ((uae_u64)revision << 32) | (subver << 16) | (subrev << 0);
			write_log ("%s %d.%d.%d.%d\n", name,
			    HIWORD (vsFileInfo->dwProductVersionMS), LOWORD (vsFileInfo->dwProductVersionMS),
			    HIWORD (vsFileInfo->dwProductVersionLS), LOWORD (vsFileInfo->dwProductVersionLS));
			if (v1 >= v2)
			    ok = 1;
		    }
		}
	    }
	    xfree (lpFileVersionData);
	}
    }
    return ok;
}
#define PORTAUDIO 1
#if PORTAUDIO
static void PortAudioEnumerate (struct sound_device *sds)
{
    struct sound_device *sd;
    int num;
    int i, j;
    char tmp[MAX_DPATH];

    num = Pa_GetDeviceCount ();
    for (j = 0; j < num; j++) {
	const PaDeviceInfo *di;
	const PaHostApiInfo *hai;
	di = Pa_GetDeviceInfo (j);
	if (di->maxOutputChannels == 0)
	    continue;
	hai = Pa_GetHostApiInfo (di->hostApi);
	if (!hai)
	    continue;
	if (hai->type == paDirectSound || hai->type == paMME)
	    continue;
	for (i = 0; i < MAX_SOUND_DEVICES; i++) {
	    sd = &sds[i];
	    if (sd->name == NULL)
	        break;
	}
	if (i >= MAX_SOUND_DEVICES)
	    return;
	sprintf (tmp, "[%s] %s", hai->name, di->name);
	sd->type = SOUND_DEVICE_PA;
	sd->name = my_strdup (tmp);
	sd->cfgname = my_strdup (tmp);
	sd->panum = j;
    }
}
#endif
int enumerate_sound_devices (void)
{
    if (!num_sound_devices) {
	HMODULE l = NULL;
	write_log ("Enumerating DirectSound devices..\n");
	DirectSoundEnumerate ((LPDSENUMCALLBACK)DSEnumProc, sound_devices);
	DirectSoundCaptureEnumerate ((LPDSENUMCALLBACK)DSEnumProc, record_devices);
	if (isdllversion ("openal32.dll", 6, 14, 357, 22)) {
	    write_log ("Enumerating OpenAL devices..\n");
	    if (alcIsExtensionPresent (NULL, "ALC_ENUMERATION_EXT")) {
		const ALchar* ppDefaultDevice = alcGetString (NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
		const ALchar* pDeviceNames = alcGetString (NULL, ALC_DEVICE_SPECIFIER);
		if (alcIsExtensionPresent (NULL, "ALC_ENUMERATE_ALL_EXT"))
		    pDeviceNames = alcGetString (NULL, ALC_ALL_DEVICES_SPECIFIER);
		OpenALEnumerate (sound_devices, pDeviceNames, ppDefaultDevice, FALSE);
		ppDefaultDevice = alcGetString (NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
		pDeviceNames = alcGetString (NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
		OpenALEnumerate (record_devices, pDeviceNames, ppDefaultDevice, TRUE);
	    }
	}
#if PORTAUDIO
	{
	    HMODULE hm = WIN32_LoadLibrary ("portaudio_x86.dll");
	    if (hm) {
		PaError err;
		write_log ("Enumerating PortAudio devices..\n");
		write_log ("%s (%d)\n", Pa_GetVersionText (), Pa_GetVersion ());
		err = Pa_Initialize ();
		if (err == paNoError) {
		    PortAudioEnumerate (sound_devices);
		} else {
		    write_log ("Portaudio initializiation failed: %d (%s)\n",
			err, Pa_GetErrorText (err));
		    FreeLibrary (hm);
		}
	    }
	}
#endif
	write_log("Enumeration end\n");
	for (num_sound_devices = 0; num_sound_devices < MAX_SOUND_DEVICES; num_sound_devices++) {
	    if (sound_devices[num_sound_devices].name == NULL)
		break;
	}
	for (num_record_devices = 0; num_record_devices < MAX_SOUND_DEVICES; num_record_devices++) {
	    if (record_devices[num_record_devices].name == NULL)
		break;
	}
    }
    if (currprefs.win32_soundcard >= num_sound_devices)
	currprefs.win32_soundcard = 0;
    return num_sound_devices;
}

#include <mmdeviceapi.h>
#include <endpointvolume.h>

/*
    Based on
    http://blogs.msdn.com/larryosterman/archive/2007/03/06/how-do-i-change-the-master-volume-in-windows-vista.aspx

*/
const static GUID XIID_MMDeviceEnumerator = {0xBCDE0395,0xE52F,0x467C,
    {0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
const static GUID XIID_IMMDeviceEnumerator = {0xA95664D2,0x9614,0x4F35,
    {0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6}};
const static GUID XIID_IAudioEndpointVolume = {0x5CDF2C82, 0x841E,0x4546,
    {0x97,0x22,0x0C,0xF7,0x40,0x78,0x22,0x9A}};

static int setget_master_volume_vista (int setvolume, int *volume, int *mute)
{
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    IMMDevice *defaultDevice = NULL;
    IAudioEndpointVolume *endpointVolume = NULL;
    HRESULT hr;
    int ok = 0;

    hr = CoInitialize (NULL);
    if (FAILED (hr))
	return 0;
    hr = CoCreateInstance (&XIID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &XIID_IMMDeviceEnumerator, (LPVOID *)&deviceEnumerator);
    if (FAILED (hr))
	return 0;
    hr = deviceEnumerator->lpVtbl->GetDefaultAudioEndpoint (deviceEnumerator, eRender, eConsole, &defaultDevice);
    if (SUCCEEDED (hr)) {
	hr = defaultDevice->lpVtbl->Activate (defaultDevice, &XIID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, NULL, (LPVOID *)&endpointVolume);
	if (SUCCEEDED (hr)) {
	    if (setvolume) {
		if (SUCCEEDED (endpointVolume->lpVtbl->SetMasterVolumeLevelScalar (endpointVolume, (float)(*volume) / (float)65535.0, NULL)))
		    ok++;
		if (SUCCEEDED (endpointVolume->lpVtbl->SetMute (endpointVolume, *mute, NULL)))
		    ok++;
	    } else {
		float vol;
		if (SUCCEEDED (endpointVolume->lpVtbl->GetMasterVolumeLevelScalar (endpointVolume, &vol))) {
		    ok++;
		    *volume = vol * 65535.0;
		}
		if (SUCCEEDED (endpointVolume->lpVtbl->GetMute (endpointVolume, mute))) {
		    ok++;
		}
	    }
	    endpointVolume->lpVtbl->Release (endpointVolume);
	}
	defaultDevice->lpVtbl->Release (defaultDevice);
    }
    deviceEnumerator->lpVtbl->Release (deviceEnumerator);
    CoUninitialize ();
    return ok == 2;
}

static void mcierr (char *str, DWORD err)
{
    char es[1000];
    if (err == MMSYSERR_NOERROR)
	return;
    if (mciGetErrorString (err, es, sizeof es))
	write_log ("MCIErr: %s: %d = '%s'\n", str, err, es);
    else
	write_log ("%s, errcode=%d\n", str, err);
}
/* from http://www.codeproject.com/audio/mixerSetControlDetails.asp */
static int setget_master_volume_xp (int setvolume, int *volume, int *mute)
{
    MMRESULT result;
    HMIXER hMixer;
    MIXERLINE ml = {0};
    MIXERLINECONTROLS mlc = {0};
    MIXERCONTROL mc = {0};
    MIXERCONTROLDETAILS mcd = {0};
    MIXERCONTROLDETAILS_UNSIGNED mcdu = {0};
    MIXERCONTROLDETAILS_BOOLEAN mcb = {0};
    int ok = 0;

    result = mixerOpen (&hMixer, 0, 0, 0, MIXER_OBJECTF_MIXER);
    if (result == MMSYSERR_NOERROR) {
	ml.cbStruct = sizeof (MIXERLINE);
	ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
	result = mixerGetLineInfo ((HMIXEROBJ) hMixer, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE);
	if (result == MIXERR_INVALLINE) {
	    ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_DIGITAL;
	    result = mixerGetLineInfo ((HMIXEROBJ) hMixer, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE);
	}
	if (result == MMSYSERR_NOERROR) {
	    mlc.cbStruct = sizeof (MIXERLINECONTROLS);
	    mlc.dwLineID = ml.dwLineID;
	    mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
	    mlc.cControls = 1;
	    mlc.pamxctrl = &mc;
	    mlc.cbmxctrl = sizeof(MIXERCONTROL);
	    result = mixerGetLineControls ((HMIXEROBJ) hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);
	    if (result == MMSYSERR_NOERROR) {
		mcd.cbStruct = sizeof (MIXERCONTROLDETAILS);
		mcd.hwndOwner = 0;
		mcd.dwControlID = mc.dwControlID;
		mcd.paDetails = &mcdu;
		mcd.cbDetails = sizeof (MIXERCONTROLDETAILS_UNSIGNED);
		mcd.cChannels = 1;
		mcdu.dwValue = 0;
		if (setvolume) {
		    mcdu.dwValue = *volume;
		    result = mixerSetControlDetails ((HMIXEROBJ) hMixer, &mcd, MIXER_SETCONTROLDETAILSF_VALUE);
		} else {
		    result = mixerGetControlDetails ((HMIXEROBJ) hMixer, &mcd, MIXER_GETCONTROLDETAILSF_VALUE);
		    *volume = mcdu.dwValue;
		}
		mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
		result = mixerGetLineControls ((HMIXEROBJ) hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);
		if (result == MMSYSERR_NOERROR) {
		    mcd.paDetails = &mcb;
		    mcd.dwControlID = mc.dwControlID;
		    mcb.fValue = 0;
		    mcd.cbDetails = sizeof (MIXERCONTROLDETAILS_BOOLEAN);
		    if (setvolume) {
			mcb.fValue = *mute;
			result = mixerSetControlDetails ((HMIXEROBJ) hMixer, &mcd, MIXER_SETCONTROLDETAILSF_VALUE);
		    } else {
			result = mixerGetControlDetails ((HMIXEROBJ) hMixer, &mcd, MIXER_GETCONTROLDETAILSF_VALUE);
			*mute = mcb.fValue;
		    }
		    if (result == MMSYSERR_NOERROR)
			ok = 1;
		} else
		    mcierr ("mixerGetLineControls Mute", result);
	    } else
		mcierr ("mixerGetLineControls Volume", result);
	} else
	    mcierr ("mixerGetLineInfo", result);
	mixerClose (hMixer);
    } else
	mcierr ("mixerOpen", result);
    return ok;
}

static int set_master_volume (int volume, int mute)
{
    if (os_vista)
	return setget_master_volume_vista (1, &volume, &mute);
    else
	return setget_master_volume_xp (1, &volume, &mute);
}
static int get_master_volume (int *volume, int *mute)
{
    *volume = 0;
    *mute = 0;
    if (os_vista)
	return setget_master_volume_vista (0, volume, mute);
    else
	return setget_master_volume_xp (0, volume, mute);
}

void sound_mute (int newmute)
{
    if (newmute < 0)
	mute = mute ? 0 : 1;
    else
	mute = newmute;
    set_volume (currprefs.sound_volume, mute);
}

void sound_volume (int dir)
{
    currprefs.sound_volume -= dir * 10;
    if (currprefs.sound_volume < 0)
	currprefs.sound_volume = 0;
    if (currprefs.sound_volume > 100)
	currprefs.sound_volume = 100;
    changed_prefs.sound_volume = currprefs.sound_volume;
    set_volume (currprefs.sound_volume, mute);
}
void master_sound_volume (int dir)
{
    int vol, mute, r;

    r = get_master_volume (&vol, &mute);
    if (!r)
	return;
    if (dir == 0)
	mute = mute ? 0 : 1;
    vol += dir * (65536 / 10);
    if (vol < 0)
	vol = 0;
    if (vol > 65535)
	vol = 65535;
    set_master_volume (vol, mute);
}
