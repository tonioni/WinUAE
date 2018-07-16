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

#include <math.h>

#include "options.h"
#include "audio.h"
#include "memory.h"
#include "events.h"
#include "custom.h"
#include "threaddep/thread.h"
#include "avioutput.h"
#include "gui.h"
#include "dxwrap.h"
#include "win32.h"
#include "savestate.h"
#include "driveclick.h"
#include "gensound.h"
#include "xwin.h"

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <ks.h>
#include <ksmedia.h>
#include <Audioclient.h>
#include <audiopolicy.h>
#include <Mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <al.h>
#include <alc.h>
#include <ntverp.h>

#include <portaudio.h>

#include "sounddep/sound.h"

#define USE_XAUDIO 0
#define WASAPI_SESSION_NOTIFICATION 0

struct sound_dp
{
	// directsound

	LPDIRECTSOUND8 lpDS;
	LPDIRECTSOUNDBUFFER8 lpDSBsecondary;
	DWORD writepos;
	int dsoundbuf;
	DWORD safedist;
	int snd_writeoffset;
	int snd_maxoffset;
	int snd_totalmaxoffset_uf;
	int snd_totalmaxoffset_of;
	int max_sndbufsize;
	int snd_configsize;

	// openal

#define AL_BUFFERS 2

	ALCdevice *al_dev;
	ALCcontext *al_ctx;
	ALuint al_Buffers[AL_BUFFERS];
	ALuint al_Source;
	int al_toggle;
	DWORD al_format;
	uae_u8 *al_bigbuffer;
	int al_bufsize, al_offset;


	// portaudio

#define PA_BUFFERSIZE (262144 * 4)
#define PA_CALLBACKBUFFERS 8

	volatile int pareadoffset, pawriteoffset;
	int pasndbufsize;
	int paframesperbuffer;
	PaStream *pastream;
	int pavolume;

	// wasapi

	IMMDevice *pDevice;
	IAudioClient3 *pAudioClient;
	int AudioClientVersion;
	IAudioRenderClient *pRenderClient;
	ISimpleAudioVolume *pAudioVolume;
	IMMDeviceEnumerator *pEnumerator;
#if WASAPI_SESSION_NOTIFICATION
	IAudioSessionManager *pManager;
	IAudioSessionControl *pControl;
	void *sessionotificationobject;
#endif
	IAudioClock *pAudioClock;
	UINT64 wasapiclock;
	UINT32 bufferFrameCount;
	UINT64 wasapiframes;
	int wasapiexclusive;
	int sndbuf;
	int wasapigoodsize;
	int pullmode;
	HANDLE pullevent, pullevent2;
	uae_u8 *pullbuffer;
	int pullbufferlen;
	int pullbuffermaxlen;

#if USE_XAUDIO
	// xaudio2

#define XA_BUFFERS 8

	IXAudio2 *xaudio2;
	IXAudio2MasteringVoice *xmaster;
	IXAudio2SourceVoice *xsource;
	XAUDIO2_BUFFER xbuffer[XA_BUFFERS];
	uae_u8 *xdata[XA_BUFFERS];
	int xabufcnt;
	int xsamplesplayed;
	int xextrasamples;
#endif

	double avg_correct;
	double cnt_correct;
};

#define SND_STATUSCNT 10

#define ADJUST_SIZE 20
#define EXP 1.9

#define ADJUST_VSSIZE 12
#define EXPVS 1.6

int sound_debug = 0;
int sound_mode_skip = 0;
int sounddrivermask;

static int have_sound;
static int statuscnt;

#define SND_MAX_BUFFER2 524288
#define SND_MAX_BUFFER 65536

uae_u16 paula_sndbuffer[SND_MAX_BUFFER];
uae_u16 *paula_sndbufpt;
int paula_sndbufsize;

static uae_sem_t sound_sem, sound_init_sem;

struct sound_device *sound_devices[MAX_SOUND_DEVICES];
struct sound_device *record_devices[MAX_SOUND_DEVICES];
static int num_sound_devices, num_record_devices;

static struct sound_data sdpaula;
static struct sound_data *sdp = &sdpaula;

static uae_u8 *extrasndbuf;
static int extrasndbufsize;
static int extrasndbuffered;

static int sound_pull = 1;

int setup_sound (void)
{
	sound_available = 1;
	return 1;
}

float sound_sync_multiplier = 1.0;
float scaled_sample_evtime_orig;
extern float sampler_evtime;

void update_sound (double clk)
{
	if (!have_sound)
		return;
	scaled_sample_evtime_orig = clk * CYCLE_UNIT * sound_sync_multiplier / sdp->obtainedfreq;
	scaled_sample_evtime = scaled_sample_evtime_orig;
	sampler_evtime = clk * CYCLE_UNIT * sound_sync_multiplier;
}

extern int vsynctimebase_orig;

#ifndef AVIOUTPUT
static int avioutput_audio;
#endif

#define ADJUST_LIMIT 6
#define ADJUST_LIMIT2 1

void sound_setadjust (double v)
{
	float mult;

	if (v < -ADJUST_LIMIT)
		v = -ADJUST_LIMIT;
	if (v > ADJUST_LIMIT)
		v = ADJUST_LIMIT;

	mult = (1000.0 + v);
	if (avioutput_audio && avioutput_enabled && avioutput_nosoundsync)
		mult = 1000.0;
	if (isvsync_chipset () || (avioutput_audio && avioutput_enabled && !currprefs.cachesize)) {
		vsynctimebase = vsynctimebase_orig;
		scaled_sample_evtime = scaled_sample_evtime_orig * mult / 1000.0;
	} else if (currprefs.cachesize || currprefs.m68k_speed != 0) {
		vsynctimebase = (int)(((double)vsynctimebase_orig) * mult / 1000.0);
		scaled_sample_evtime = scaled_sample_evtime_orig;
	} else {
		vsynctimebase = (int)(((double)vsynctimebase_orig) * mult / 1000.0);
		scaled_sample_evtime = scaled_sample_evtime_orig;
	}
}


static void docorrection (struct sound_dp *s, int sndbuf, double sync, int granulaty)
{
	static int tfprev;

	s->avg_correct += sync;
	s->cnt_correct++;

	if (granulaty < 10)
		granulaty = 10;

	if (tfprev != timeframes) {
		double skipmode, avgskipmode;
		double avg = s->avg_correct / s->cnt_correct;

		skipmode = sync / 100.0;
		avgskipmode = avg / (10000.0 / granulaty);

		if ((0 || sound_debug) && (tfprev % 10) == 0) {
			write_log (_T("%+05d S=%7.1f AVG=%7.1f (IMM=%7.1f + AVG=%7.1f = %7.1f)\n"), sndbuf, sync, avg, skipmode, avgskipmode, skipmode + avgskipmode);
		}
		gui_data.sndbuf = sndbuf;

		if (skipmode > ADJUST_LIMIT2)
			skipmode = ADJUST_LIMIT2;
		if (skipmode < -ADJUST_LIMIT2)
			skipmode = -ADJUST_LIMIT2;

		sound_setadjust (skipmode + avgskipmode);
		tfprev = timeframes;
	}
}


static double sync_sound (double m)
{
	double skipmode;
	if (isvsync ()) {

		skipmode = pow (m < 0 ? -m : m, EXPVS) / 2;
		if (m < 0)
			skipmode = -skipmode;
		if (skipmode < -ADJUST_VSSIZE)
			skipmode = -ADJUST_VSSIZE;
		if (skipmode > ADJUST_VSSIZE)
			skipmode = ADJUST_VSSIZE;

	} else if (1) {

		skipmode = pow (m < 0 ? -m : m, EXP) / 2;
		if (m < 0)
			skipmode = -skipmode;
		if (skipmode < -ADJUST_SIZE)
			skipmode = -ADJUST_SIZE;
		if (skipmode > ADJUST_SIZE)
			skipmode = ADJUST_SIZE;
	}

	return skipmode;
}

static void clearbuffer_ds (struct sound_data *sd)
{
	void *buffer;
	DWORD size;
	struct sound_dp *s = sd->data;

	HRESULT hr = IDirectSoundBuffer_Lock (s->lpDSBsecondary, 0, s->dsoundbuf, &buffer, &size, NULL, NULL, 0);
	if (hr == DSERR_BUFFERLOST) {
		IDirectSoundBuffer_Restore (s->lpDSBsecondary);
		hr = IDirectSoundBuffer_Lock (s->lpDSBsecondary, 0, s->dsoundbuf, &buffer, &size, NULL, NULL, 0);
	}
	if (FAILED (hr)) {
		write_log (_T("DS: failed to Lock sound buffer (clear): %s\n"), DXError (hr));
		return;
	}
	memset (buffer, 0, size);
	IDirectSoundBuffer_Unlock (s->lpDSBsecondary, buffer, size, NULL, 0);
}

static void clearbuffer (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	if (sd->devicetype == SOUND_DEVICE_DS) {
		clearbuffer_ds(sd);
	}
	if (s->pullbuffer) {
		memset(s->pullbuffer, 0, s->pullbuffermaxlen);
	}
}

#if USE_XAUDIO
static void pause_audio_xaudio2 (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;

	s->xsource->Stop ();
	s->xsource->FlushSourceBuffers ();
	s->xaudio2->StopEngine ();
}
static void resume_audio_xaudio2 (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	XAUDIO2_VOICE_STATE state;
	HRESULT hr;
	int i;
	
	hr = s->xaudio2->StartEngine ();
	if (FAILED (hr))
		write_log (_T("XAUDIO2: StartEngine() %08X\n"), hr);
	s->xsource->FlushSourceBuffers ();
	s->xsource->GetState (&state);
	s->xsamplesplayed = state.SamplesPlayed;
	s->xextrasamples = 0;
	for (i = 0; i < XA_BUFFERS / 2; i++) {
		XAUDIO2_BUFFER *buffer = &s->xbuffer[s->xabufcnt];
		buffer->AudioBytes = sd->sndbufsize;
		buffer->pAudioData = (BYTE*)s->xdata[s->xabufcnt];
		hr = s->xsource->SubmitSourceBuffer (buffer);
		if (FAILED (hr)) 
			write_log (_T("XAUDIO2: SubmitSourceBuffer %08X\n"), hr);
		s->xsamplesplayed += sd->sndbufframes;
		s->xabufcnt = (s->xabufcnt + 1) & (XA_BUFFERS - 1);
	}
	hr = s->xsource->Start (0, 0);
	if (FAILED (hr))
		write_log (_T("XAUDIO2: Start() %08X\n"), hr);
}
#endif

static void set_reset(struct sound_data *sd)
{
	sd->reset = true;
	sd->resetcnt = 10;
	sd->resetframecnt = 0;
}

static void wasapi_check_state(struct sound_data *sd, HRESULT hr)
{
	// 0x26 = AUDCLNT_E_RESOURCES_INVALIDATED
	if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_OUT_OF_ORDER || hr == AUDCLNT_E_CPUUSAGE_EXCEEDED || hr == AUDCLNT_E_BUFFER_ERROR || hr == AUDCLNT_ERR(0x026)) {
		set_reset(sd);
	}
}

static void pause_audio_wasapi (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;

	hr = s->pAudioClient->Stop ();
	wasapi_check_state(sd, hr);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: Stop() %08X\n"), hr);
	}
}
static void resume_audio_wasapi (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;
	BYTE *pData;
	int framecnt;

	clearbuffer(sd);
	hr = s->pAudioClient->Reset ();
	wasapi_check_state(sd, hr);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: Reset() %08X\n"), hr);
	}
	if (s->wasapiexclusive) {
		framecnt = s->pullbuffermaxlen / sd->samplesize;
	} else {
		framecnt = s->wasapigoodsize;
	}
	hr = s->pRenderClient->GetBuffer (framecnt, &pData);
	wasapi_check_state(sd, hr);
	if (FAILED (hr)) {
		write_log(_T("WASAPI: Resume GetBuffer() %08X\n"), hr);
		return;
	}
	hr = s->pRenderClient->ReleaseBuffer (framecnt, AUDCLNT_BUFFERFLAGS_SILENT);
	wasapi_check_state(sd, hr);
	if (FAILED(hr)) {
		write_log(_T("WASAPI: Resume ReleaseBuffer() %08X\n"), hr);
	}
	hr = s->pAudioClient->Start ();
	wasapi_check_state(sd, hr);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: Start() %08X\n"), hr);
	}
	if (s->wasapiexclusive) {
		hr = s->pRenderClient->GetBuffer(framecnt, &pData);
		if (SUCCEEDED(hr)) {
			hr = s->pRenderClient->ReleaseBuffer(framecnt, AUDCLNT_BUFFERFLAGS_SILENT);
		}
	}
	s->wasapiframes = 0;
	s->sndbuf = 0;
}

static void pause_audio_ds (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;

	sd->waiting_for_buffer = 0;
	hr = IDirectSoundBuffer_Stop (s->lpDSBsecondary);
	if (FAILED (hr))
		write_log (_T("DS: DirectSoundBuffer_Stop failed, %s\n"), DXError (hr));
	hr = IDirectSoundBuffer_SetCurrentPosition (s->lpDSBsecondary, 0);
	if (FAILED (hr))
		write_log (_T("DS: DirectSoundBuffer_SetCurretPosition failed, %s\n"), DXError (hr));
	clearbuffer (sd);
}
static void resume_audio_ds (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	sd->paused = 0;
	clearbuffer (sd);
	sd->waiting_for_buffer = 1;
	s->avg_correct = 0;
	s->cnt_correct = 0;
}
static void pause_audio_pa (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	PaError err = Pa_StopStream (s->pastream);
	if (err != paNoError)
		write_log (_T("PASOUND: Pa_StopStream() error %d (%s)\n"), err, Pa_GetErrorText (err));
}
static void resume_audio_pa (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	s->pawriteoffset = 0;
	s->pareadoffset = 0;
	clearbuffer(sd);
	PaError err = Pa_StartStream (s->pastream);
	if (err != paNoError)
		write_log (_T("PASOUND: Pa_StartStream() error %d (%s)\n"), err, Pa_GetErrorText (err));
	sd->paused = 0;
}
static void pause_audio_al (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	sd->waiting_for_buffer = 0;
	alSourcePause (s->al_Source);
}
static void resume_audio_al (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	sd->waiting_for_buffer = 1;
	s->al_offset = 0;
}

static int restore_ds (struct sound_data *sd, DWORD hr)
{
	struct sound_dp *s = sd->data;
	if (hr != DSERR_BUFFERLOST)
		return 0;
	if (sound_debug)
		write_log (_T("DS: sound buffer lost\n"));
	hr = IDirectSoundBuffer_Restore (s->lpDSBsecondary);
	if (FAILED (hr)) {
		write_log (_T("DS: restore failed %s\n"), DXError (hr));
		return 1;
	}
	pause_audio_ds (sd);
	resume_audio_ds (sd);
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

static void close_audio_ds (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	if (s->lpDSBsecondary)
		IDirectSound_Release (s->lpDSBsecondary);
	s->lpDSBsecondary = 0;
#ifdef USE_PRIMARY_BUFFER
	if (s->lpDSBprimary)
		IDirectSound_Release (s->lpDSBprimary);
	s->lpDSBprimary = 0;
#endif
	if (s->lpDS) {
		IDirectSound_Release (s->lpDS);
		write_log (_T("DS: DirectSound driver freed\n"));
	}
	s->lpDS = 0;
}

extern void setvolume_ahi (LONG);

void set_volume_sound_device (struct sound_data *sd, int volume, int mute)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;
	if (sd->devicetype == SOUND_DEVICE_AL) {
		float vol = 0.0;
		if (volume < 100 && !mute)
			vol = (100 - volume) / 100.0;
		alSourcef (s->al_Source, AL_GAIN, vol);
	} else if (sd->devicetype == SOUND_DEVICE_DS) {
		LONG vol = DSBVOLUME_MIN;
		if (volume < 100 && !mute)
			vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (1 - volume / 100.0)));
		hr = IDirectSoundBuffer_SetVolume (s->lpDSBsecondary, vol);
		if (FAILED (hr))
			write_log (_T("DS: SetVolume(%d) failed: %s\n"), vol, DXError (hr));
#if 0
	} else if (sd->devicetype == SOUND_DEVICE_WASAPI) {
		if (s->pAudioVolume) {
			float vol = 0.0;
			if (volume < 100 && !mute)
				vol = (100 - volume) / 100.0;
			hr = s->pAudioVolume->SetMasterVolume (vol, NULL);
			if (FAILED (hr))
				write_log (_T("AudioVolume->SetMasterVolume(%.2f) failed: %08Xs\n"), vol, hr);
			hr = s->pAudioVolume->SetMute (mute, NULL);
			if (FAILED (hr))
				write_log (_T("pAudioVolume->SetMute(%d) failed: %08Xs\n"), mute, hr);
		}
#endif
	} else if (sd->devicetype == SOUND_DEVICE_PA) {
		s->pavolume = volume;
#if USE_XAUDIO
	} else if (sd->devicetype == SOUND_DEVICE_XAUDIO2) {
		s->xmaster->SetVolume (mute ? 0.0 : (float)(100 - volume) / 100.0);
#endif
	} else if (sd->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE || sd->devicetype == SOUND_DEVICE_WASAPI) {
		sd->softvolume = -1;
		hr = s->pAudioVolume->SetMasterVolume (1.0, NULL);
		wasapi_check_state(sd, hr);
		if (FAILED (hr))
			write_log (_T("AudioVolume->SetMasterVolume(1.0) failed: %08Xs\n"), hr);
		if (volume < 100 && !mute) {
			sd->softvolume = (100 - volume) * 32768 / 100.0;
			if (sd->softvolume >= 32768)
				sd->softvolume = -1;
		}
		if (mute || volume >= 100)
			sd->softvolume = 0;
	}

}

void set_volume (int volume, int mute)
{
	set_volume_sound_device (sdp, volume, mute);
	setvolume_ahi (volume);
	config_changed = 1;
}

static void recalc_offsets (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	s->snd_writeoffset = s->max_sndbufsize * 5 / 8;
	s->snd_maxoffset = s->max_sndbufsize;
	s->snd_totalmaxoffset_of = s->max_sndbufsize + (s->dsoundbuf - s->max_sndbufsize) * 3 / 9;
	s->snd_totalmaxoffset_uf = s->max_sndbufsize + (s->dsoundbuf - s->max_sndbufsize) * 7 / 9;
}

//const static GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,
//{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
#define KSAUDIO_SPEAKER_QUAD_SURROUND   (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
	SPEAKER_SIDE_LEFT  | SPEAKER_SIDE_RIGHT)

#define MAX_SUPPORTEDMODES 16
static struct dsaudiomodes supportedmodes[MAX_SUPPORTEDMODES];

static DWORD fillsupportedmodes (struct sound_data *sd, int freq, struct dsaudiomodes *dsam)
{
	static DWORD speakerconfig;
	DSBUFFERDESC sound_buffer;
	WAVEFORMATEXTENSIBLE wavfmt;
	LPDIRECTSOUNDBUFFER pdsb;
	HRESULT hr;
	int ch, round, mode, skip;
	DWORD rn[4];
	struct sound_dp *s = sd->data;
	LPDIRECTSOUND8 lpDS = s->lpDS;
	static int done;

	if (done)
		return speakerconfig;

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
			sound_buffer.dwBufferBytes = s->dsoundbuf;
			sound_buffer.lpwfxFormat = &wavfmt.Format;
			sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
			sound_buffer.dwFlags |= DSBCAPS_CTRLVOLUME;
			sound_buffer.guid3DAlgorithm = GUID_NULL;
			pdsb = NULL;
			hr = IDirectSound_CreateSoundBuffer (lpDS, &sound_buffer, &pdsb, NULL);
			if (SUCCEEDED (hr)) {
				IDirectSound_Release (pdsb);
				dsam[mode].ksmode = rn[round];
				dsam[mode].ch = ch;
				mode++;
				if (mode >= MAX_SUPPORTEDMODES - 1)
					break;
			}
		}
	}
	dsam[mode].ch = 0;
	dsam[mode].ksmode = 0;
	done = 1;
	return speakerconfig;
}

static void finish_sound_buffer_pull(struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;

	if (s->pullbufferlen + sd->sndbufsize > s->pullbuffermaxlen) {
		write_log(_T("pull overflow! %d %d %d\n"), s->pullbufferlen, sd->sndbufsize, s->pullbuffermaxlen);
		s->pullbufferlen = 0;
	}
	memcpy(s->pullbuffer + s->pullbufferlen, sndbuffer, sd->sndbufsize);
	s->pullbufferlen += sd->sndbufsize;
}

static void finish_sound_buffer_pa (struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;

	if (s->pavolume) {
		int vol = 65536 - s->pavolume * 655;
		for (int i = 0; i < sd->sndbufsize / sizeof (uae_u16); i++) {
			uae_s16 v = (uae_s16)sndbuffer[i];
			sndbuffer[i] = v * vol / 65536;
		}
	}
	finish_sound_buffer_pull(sd, sndbuffer);
}

static int _cdecl portAudioCallback (const void *inputBuffer, void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData)
{
	struct sound_data *sd = (struct sound_data*)userData;
	struct sound_dp *s = sd->data;
	int bytestocopy;

	if (!framesPerBuffer || sdp->deactive)
		return paContinue;

	if (s->pullbufferlen <= 0) {
		ResetEvent(s->pullevent2);
		SetEvent(s->pullevent);
		WaitForSingleObject(s->pullevent2, 1);
		if (s->pullbufferlen <= 0) {
			return paContinue;
		}
	}

	bytestocopy = framesPerBuffer * sd->samplesize;
	if (bytestocopy > 0) {
		memcpy(outputBuffer, s->pullbuffer, bytestocopy);
	}

	if (bytestocopy < s->pullbufferlen) {
		memmove(s->pullbuffer, s->pullbuffer + bytestocopy, s->pullbufferlen - bytestocopy);
	}
	s->pullbufferlen -= bytestocopy;

	return paContinue;
}

static void close_audio_pa (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;

	if (s->pastream)
		Pa_CloseStream (s->pastream);
	if (s->pullevent)
		CloseHandle(s->pullevent);
	s->pullevent = NULL;
	if (s->pullevent2)
		CloseHandle(s->pullevent2);
	s->pullevent2 = NULL;
	s->pastream = NULL;
	xfree(s->pullbuffer);
	s->pullbuffer = NULL;
	s->pullbufferlen = 0;
}

static int open_audio_pa (struct sound_data *sd, int index)
{
	struct sound_dp *s = sd->data;
	int freq = sd->freq;
	int ch = sd->channels;
	int dev = sound_devices[index]->panum;
	const PaDeviceInfo *di;
	PaStreamParameters p;
	PaError err;
	TCHAR *name;
	TCHAR *errtxt;
	int defaultrate = 0;

	if (sd->sndbufsize < 0x80)
		sd->sndbufsize = 0x80;
	s->paframesperbuffer = sd->sndbufsize;
	s->pasndbufsize = s->paframesperbuffer;
	sd->sndbufsize = s->pasndbufsize * ch * 2;
	if (sd->sndbufsize > SND_MAX_BUFFER)
		sd->sndbufsize = SND_MAX_BUFFER;

	sd->devicetype = SOUND_DEVICE_PA;
	memset (&p, 0, sizeof p);
	di = Pa_GetDeviceInfo (dev);
	for (;;) {
		for (;;) {
			int err2;
			p.channelCount = ch;
			p.device = dev;
			p.hostApiSpecificStreamInfo = NULL;
			p.sampleFormat = paInt16;
			p.suggestedLatency = di->defaultLowOutputLatency;
			p.hostApiSpecificStreamInfo = NULL; 

			err = Pa_IsFormatSupported (NULL, &p, freq);
			if (err == paFormatIsSupported)
				break;
fixfreq:
			err2 = err;
			errtxt = au (Pa_GetErrorText (err));
			write_log (_T("PASOUND: sound format not supported, ch=%d, rate=%d. %s\n"), freq, ch, errtxt);
			xfree (errtxt);
			if (err == paInvalidChannelCount) {
				if (ch > 2) {
					ch = sd->channels = 2;
					continue;
				}
				goto end;
			}
			if (freq < 44000 && err == paInvalidSampleRate) {
				freq = 44000;
				sd->freq = freq;
				continue;
			}
			if (freq < 48000 && err == paInvalidSampleRate) {
				freq = 48000;
				sd->freq = freq;
				continue;
			}
			if (freq != di->defaultSampleRate && err == paInvalidSampleRate && !defaultrate) {
				freq = di->defaultSampleRate;
				sd->freq = freq;
				defaultrate = 1;
				continue;
			}
			goto end;
		}

		sd->samplesize = ch * 16 / 8;
		s->pullmode = 1;
		err = Pa_OpenStream (&s->pastream, NULL, &p, freq, s->paframesperbuffer, paNoFlag, portAudioCallback, sd);

		if (err == paInvalidSampleRate || err == paInvalidChannelCount)
			goto fixfreq;
		if (err != paNoError) {
			errtxt = au (Pa_GetErrorText (err));
			write_log (_T("PASOUND: Pa_OpenStream() error %d (%s)\n"), err, errtxt);
			xfree (errtxt);
			goto end;
		}
		break;
	}

	s->pullevent = CreateEvent(NULL, TRUE, FALSE, NULL);
	s->pullevent2 = CreateEvent(NULL, FALSE, FALSE, NULL);
	s->pullbuffermaxlen = sd->sndbufsize * 2;
	s->pullbuffer = xcalloc(uae_u8, s->pullbuffermaxlen);
	s->pullbufferlen = 0;

	name = au (di->name);
	write_log (_T("PASOUND: CH=%d,FREQ=%d (%s) '%s' buffer %d/%d (%s)\n"),
		ch, freq, sound_devices[index]->name, name,
		s->pasndbufsize, s->paframesperbuffer, !s->pullmode ? _T("push") : _T("pull"));
	xfree (name);
	return 1;
end:
	s->pastream = NULL;
	close_audio_pa (sd);
	return 0;
}

static void close_audio_al (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	int i;

	alDeleteSources (1, &s->al_Source);
	s->al_Source = 0;
	alDeleteBuffers (AL_BUFFERS, s->al_Buffers);
	alcMakeContextCurrent (NULL);
	if (s->al_ctx)
		alcDestroyContext (s->al_ctx);
	s->al_ctx = NULL;
	if (s->al_dev)
		alcCloseDevice (s->al_dev);
	s->al_dev = NULL;
	for (i = 0; i < AL_BUFFERS; i++) {
		s->al_Buffers[i] = 0;
	}
	xfree (s->al_bigbuffer);
	s->al_bigbuffer = NULL;
}

static int open_audio_al (struct sound_data *sd, int index)
{
	struct sound_dp *s = sd->data;
	int freq = sd->freq;
	int ch = sd->channels;
	char *name;
	int size;

	if (sd->sndbufsize < 0x80)
		sd->sndbufsize = 0x80;
	size = sd->sndbufsize;
	sd->devicetype = SOUND_DEVICE_AL;
	size *= ch * 2;
	sd->sndbufsize = size / 8;
	if (sd->sndbufsize > SND_MAX_BUFFER)
		sd->sndbufsize = SND_MAX_BUFFER;
	if (size < 512)
		size = 512;
	s->al_bufsize = size;
	s->al_bigbuffer = xcalloc (uae_u8, s->al_bufsize);
	name = ua (sound_devices[index]->alname);
	s->al_dev = alcOpenDevice (name);
	xfree (name);
	if (!s->al_dev)
		goto error;
	s->al_ctx = alcCreateContext (s->al_dev, NULL);
	if (!s->al_ctx)
		goto error;
	alcMakeContextCurrent (s->al_ctx);
	alGenBuffers (AL_BUFFERS, s->al_Buffers);
	alGenSources (1, &s->al_Source);
	s->al_toggle = 0;
	s->al_format = 0;
	switch (ch)
	{
	case 1:
		s->al_format = AL_FORMAT_MONO16;
		break;
	case 2:
		s->al_format = AL_FORMAT_STEREO16;
		break;
	case 4:
		s->al_format = alGetEnumValue ("AL_FORMAT_QUAD16");
		break;
	case 6:
		s->al_format = alGetEnumValue ("AL_FORMAT_51CHN16");
		break;
	}
	if (s->al_format == 0)
		goto error;

	write_log (_T("ALSOUND: %08X,CH=%d,FREQ=%d '%s' buffer %d (%d)\n"),
		s->al_format, ch, freq, sound_devices[index]->alname,
		sd->sndbufsize, s->al_bufsize);
	return 1;

error:
	close_audio_al (sd);
	return 0;
}

static void setwavfmt (WAVEFORMATEXTENSIBLE *wavfmt, struct sound_data *sd, DWORD channelmask)
{
	memset (wavfmt, 0, sizeof WAVEFORMATEXTENSIBLE);
	wavfmt->Format.nChannels = sd->channels;
	wavfmt->Format.nSamplesPerSec = sd->freq;
	wavfmt->Format.wBitsPerSample = 16;
	wavfmt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wavfmt->Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
	wavfmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	wavfmt->Samples.wValidBitsPerSample = 16;
	wavfmt->dwChannelMask = channelmask;
	wavfmt->Format.nBlockAlign = wavfmt->Format.wBitsPerSample / 8 * wavfmt->Format.nChannels;
	wavfmt->Format.nAvgBytesPerSec = wavfmt->Format.nBlockAlign * wavfmt->Format.nSamplesPerSec;
}

#if USE_XAUDIO
static void close_audio_xaudio2 (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	int i;

	if (s->xsource)
		s->xsource->DestroyVoice ();
	s->xsource = NULL;
	if (s->xmaster)
		s->xmaster->DestroyVoice();
	s->xmaster = NULL;
	if (s->xaudio2)
		s->xaudio2->Release();
	s->xaudio2 = NULL;
	for (i = 0; i < XA_BUFFERS; i++) {
		xfree (s->xdata[i]);
		s->xdata[i] = NULL;
	}
}

static int open_audio_xaudio2 (struct sound_data *sd, int index)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;
	WAVEFORMATEXTENSIBLE wavfmt;
	int rncnt, i;

	hr = XAudio2Create (&s->xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED (hr)) {
		write_log (_T("XAudio2 enumeration failed, %08x\n"), hr);
		return 0;
	}

	sd->devicetype = SOUND_DEVICE_XAUDIO2;

	rncnt = 0;
	for (;;) {
		DWORD rn[4];
		if (sd->channels == 6) {
			rn[0] = KSAUDIO_SPEAKER_5POINT1;
			rn[1] = KSAUDIO_SPEAKER_5POINT1_SURROUND;
			rn[2] = 0;
		} else if (sd->channels == 4) {
			rn[0] = KSAUDIO_SPEAKER_QUAD;
			rn[1] = KSAUDIO_SPEAKER_QUAD_SURROUND;
			rn[2] = KSAUDIO_SPEAKER_SURROUND;
			rn[3] = 0;
		} else if (sd->channels == 2) {
			rn[0] = KSAUDIO_SPEAKER_STEREO;
			rn[1] = 0;
		} else {
			rn[0] = KSAUDIO_SPEAKER_MONO;
			rn[1] = 0;
		}
		setwavfmt (&wavfmt, sd, rn[rncnt]);
			
		hr = s->xaudio2->CreateMasteringVoice (&s->xmaster, sd->channels, sd->freq, 0, sound_devices[index]->panum, NULL);
		if (SUCCEEDED (hr)) {
			hr = s->xaudio2->CreateSourceVoice (&s->xsource, &wavfmt.Format, XAUDIO2_VOICE_NOPITCH | XAUDIO2_VOICE_NOSRC, 1.0f, NULL, NULL, NULL);
 			if (SUCCEEDED (hr))
				break;
			s->xmaster->DestroyVoice ();
			s->xmaster = NULL;
		}

		rncnt++;
		if (rn[rncnt])
			continue;

		if (sd->freq < 44100) {
			sd->freq = 44100;
			continue;
		}
		if (sd->freq < 48000) {
			sd->freq = 48000;
			continue;
		}
		if (sd->channels != 2) {
			sd->channels = 2;
			continue;
		}

		write_log (_T("XAUDIO2: open failed %08X\n"), hr);
		return 0;

	}

	sd->samplesize = sd->channels * 16 / 8;
	sd->sndbufsize = sd->sndbufsize * sd->samplesize / 32;

	write_log(_T("XAUDIO2: '%s' CH=%d FREQ=%d BUF=%d\n"),
		sound_devices[index]->cfgname, sd->channels, sd->freq, sd->sndbufsize);
	
	sd->sndbufsize = sd->sndbufsize * sd->samplesize;

	for (i = 0; i < XA_BUFFERS; i++)
		s->xdata[i] = xcalloc (uae_u8, sd->sndbufsize);
	
	return 1;

}
#endif

class UAEIMMNotificationClient : public IMMNotificationClient
{
	LONG _cRef;
	struct sound_data *s;
	TCHAR current[MAX_DPATH];
public:
	UAEIMMNotificationClient(struct sound_data *s) : _cRef(1)
	{
		this->s = s;
		current[0] = 0;
	}
	~UAEIMMNotificationClient()
	{
	}
	ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&_cRef);
	}
	ULONG STDMETHODCALLTYPE Release()
	{
		ULONG ulRef = InterlockedDecrement(&_cRef);
		if (!ulRef) {
			delete this;
		}
		return ulRef;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid, VOID **ppvInterface)
	{
		if (IID_IUnknown == riid) {
			AddRef();
			*ppvInterface = (IUnknown*)this;
		} else if (__uuidof(IMMNotificationClient) == riid) {
			AddRef();
			*ppvInterface = (IMMNotificationClient*)this;
		} else {
			*ppvInterface = NULL;
			return E_NOINTERFACE;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
	{
		if (flow != eRender)
			return S_OK;
		if (flow != eConsole && flow != eMultimedia)
			return S_OK;
		if (s->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE) {
			write_log(_T("WASAPI EX OnDefaultDeviceChanged '%s'\n"), pwstrDeviceId);
			return S_OK;
		}
		if (s->devicetype != SOUND_DEVICE_WASAPI)
			return S_OK;
		if (s->index < 0)
			return S_OK;
		// default wasapi device selected?
		if (sound_devices[s->index]->alname != NULL)
			return S_OK;
		if (!_tcscmp(current, pwstrDeviceId))
			return S_OK;
		_tcscpy(current, pwstrDeviceId);
		write_log(_T("WASAPI OnDefaultDeviceChanged '%s'\n"), current);
		set_reset(s);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId)
	{
		return S_OK;
	};
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId)
	{
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
	{
		if (s->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE || s->devicetype == SOUND_DEVICE_WASAPI) {
			;// write_log(_T("WASAPI OnDeviceStateChanged '%s' %08x\n"), pwstrDeviceId, dwNewState);
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
	{
		if (s->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE || s->devicetype == SOUND_DEVICE_WASAPI) {
			;// write_log(_T("WASAPI OnPropertyValueChanged '%s'\n"), pwstrDeviceId);
		}
		return S_OK;
	}
};

static void add_wasapi_endpointnotification(struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	static bool notificationcallback;
	if (notificationcallback)
		return;
	UAEIMMNotificationClient *imm = new UAEIMMNotificationClient(sd);
	HRESULT hr = s->pEnumerator->RegisterEndpointNotificationCallback(imm);
	notificationcallback = true;
	if (FAILED(hr))
		write_log(_T("RegisterEndpointNotificationCallback failed %08x\n"), hr);
}

#if WASAPI_SESSION_NOTIFICATION

class UAEAudioSessionControl : public IAudioSessionEvents
{
	LONG _cRef;
	struct sound_data *s;
public:
	UAEAudioSessionControl(struct sound_data *s) : _cRef(1)
	{
		this->s = s;
	}
	~UAEAudioSessionControl()
	{
	}
	ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&_cRef);
	}
	ULONG STDMETHODCALLTYPE Release()
	{
		ULONG ulRef = InterlockedDecrement(&_cRef);
		if (!ulRef) {
			delete this;
		}
		return ulRef;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface)
	{
		if (IID_IUnknown == riid) {
			AddRef();
			*ppvInterface = (IUnknown*)this;
		}
		else if (__uuidof(IAudioSessionEvents) == riid) {
			AddRef();
			*ppvInterface = (IAudioSessionEvents*)this;
		}
		else {
			*ppvInterface = NULL;
			return E_NOINTERFACE;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext)
	{
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnIconPathChanged(PCWSTR NewIconPath, LPCGUID EventContext)
	{
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext)
	{
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD ChannelCount, float NewChannelVolumeArray[], DWORD ChangedChannel, LPCGUID EventContext)
	{
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext)
	{
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState NewState)
	{
		write_log(_T("WASAPI OnStateChanged %08x\n"), NewState);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason)
	{
		write_log(_T("WASAPI OnSessionDisconnected %08x\n"), DisconnectReason);
		return S_OK;
	}
};

static void add_wasapi_sessionnotification(struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	UAEAudioSessionControl *asc = new UAEAudioSessionControl(sd);
	s->sessionotificationobject = NULL;
	if (s->pControl) {
		HRESULT hr = s->pControl->RegisterAudioSessionNotification(asc);
		if (FAILED(hr)) {
			write_log(_T("RegisterAudioSessionNotification failed %08x\n"), hr);
			delete asc;
		} else {
			write_log(_T("RegisterAudioSessionNotification\n"));
			s->sessionotificationobject = asc;
		}
	}
}

#endif

static void close_audio_wasapi(struct sound_data *sd)
{
	struct sound_dp *s = sd->data;

	if (s->pRenderClient)
		s->pRenderClient->Release();
	if (s->pAudioVolume)
		s->pAudioVolume->Release();
	if (s->pAudioClock)
		s->pAudioClock->Release();
	if (s->pAudioClient)
		s->pAudioClient->Release();
#if WASAPI_SESSION_NOTIFICATION
	if (s->pControl) {
		if (s->sessionotificationobject) {
			s->pControl->UnregisterAudioSessionNotification((UAEAudioSessionControl*)s->sessionotificationobject);
			delete s->sessionotificationobject;
		}
		s->pControl->Release();
	}
	if (s->pManager)
		s->pManager->Release();
#endif
	if (s->pDevice)
		s->pDevice->Release ();
	if (s->pEnumerator)
		s->pEnumerator->Release ();
	if (s->pullevent) {
		CloseHandle(s->pullevent);
		s->pullevent = NULL;
	}
	xfree(s->pullbuffer);
	s->pullbuffer = NULL;
	s->pullbufferlen = 0;
}

static int open_audio_wasapi (struct sound_data *sd, int index, int exclusive)
{
	static const GUID audioguid = { 0x6a009123, 0xa0d5, 0x4d06, { 0xad, 0xac, 0x20, 0x1b, 0xe7, 0x31, 0x31, 0xd7 } };
	HRESULT hr;
	struct sound_dp *s = sd->data;
	WAVEFORMATEX *pwfx = NULL;
	WAVEFORMATEX *pwfx_saved = NULL;
	WAVEFORMATEXTENSIBLE wavfmt;
	int final;
	LPWSTR name = NULL;
	int rn[4], rncnt;
	AUDCLNT_SHAREMODE sharemode;
	REFERENCE_TIME phnsMinimumDevicePeriod, hnsDefaultDevicePeriod;
	int v;

retry:
	sd->devicetype = exclusive ? SOUND_DEVICE_WASAPI_EXCLUSIVE : SOUND_DEVICE_WASAPI;
	s->wasapiexclusive = exclusive;
	s->pullmode = sound_pull;

	if (s->wasapiexclusive)
		sharemode = AUDCLNT_SHAREMODE_EXCLUSIVE;
	else
		sharemode = AUDCLNT_SHAREMODE_SHARED;

	if (s->pullmode) {
		s->pullevent = CreateEvent(NULL, TRUE, FALSE, NULL);
	}

	hr = CoCreateInstance (__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&s->pEnumerator);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: %08X\n"), hr);
		goto error;
	}

	if (sound_devices[index]->alname == NULL)
		hr = s->pEnumerator->GetDefaultAudioEndpoint (eRender, eMultimedia, &s->pDevice);
	else
		hr = s->pEnumerator->GetDevice (sound_devices[index]->alname, &s->pDevice);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetDevice(%s) %08X\n"), sound_devices[index]->alname ? sound_devices[index]->alname : _T("NULL"), hr);
		goto error;
	}

	hr = s->pDevice->GetId (&name);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetId() %08X\n"), hr);
		goto error;
	}

	s->AudioClientVersion = 3;
	hr = s->pDevice->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, NULL, (void**)&s->pAudioClient);
	if (FAILED(hr)) {
		s->AudioClientVersion = 1;
		hr = s->pDevice->Activate (__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&s->pAudioClient);
		if (FAILED (hr)) {
			write_log (_T("WASAPI: Activate(IAudioClient) %08X\n"), hr);
			goto error;
		}
	}

#if WASAPI_SESSION_NOTIFICATION
	hr = s->pDevice->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, NULL, (void**)&s->pManager);
	if (FAILED(hr)) {
		write_log(_T("WASAPI: Activate(IAudioSessionManager) %08X\n"), hr);
	} else {
		hr = s->pManager->GetAudioSessionControl(NULL, 0, &s->pControl);
		if (FAILED(hr))
			write_log(_T("WASAPI: GetAudioSessionControl() %08X\n"), hr);
	}
#endif

	hr = s->pAudioClient->GetMixFormat (&pwfx);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetMixFormat() %08X\n"), hr);
		goto error;
	}

	hr = s->pAudioClient->GetDevicePeriod (&hnsDefaultDevicePeriod, &phnsMinimumDevicePeriod);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetDevicePeriod() %08X\n"), hr);
		goto error;
	}

	final = 0;
	rncnt = 0;
	for (;;) {

		if (sd->channels == 6) {
			rn[0] = KSAUDIO_SPEAKER_5POINT1;
			rn[1] = KSAUDIO_SPEAKER_5POINT1_SURROUND;
			rn[2] = 0;
		} else if (sd->channels == 4) {
			rn[0] = KSAUDIO_SPEAKER_QUAD;
			rn[1] = KSAUDIO_SPEAKER_QUAD_SURROUND;
			rn[2] = KSAUDIO_SPEAKER_SURROUND;
			rn[3] = 0;
		} else if (sd->channels == 2) {
			rn[0] = KSAUDIO_SPEAKER_STEREO;
			rn[1] = 0;
		} else {
			rn[0] = KSAUDIO_SPEAKER_MONO;
			rn[1] = 0;
		}

		setwavfmt (&wavfmt, sd, rn[rncnt]);

		CoTaskMemFree (pwfx);
		pwfx = NULL;
		hr = s->pAudioClient->IsFormatSupported (sharemode, &wavfmt.Format, &pwfx);
		if (SUCCEEDED (hr) && hr != S_FALSE)
			break;
		write_log (_T("WASAPI: IsFormatSupported(%d,%08X,%d) (%d,%d) %08X\n"),
			sd->channels, rn[rncnt], sd->freq,
			pwfx ? pwfx->nChannels : -1, pwfx ? pwfx->nSamplesPerSec : -1,
			hr);
		if (final && SUCCEEDED (hr)) {
			if (pwfx_saved) {
				sd->channels = pwfx_saved->nChannels;
				sd->freq = pwfx_saved->nSamplesPerSec;
				CoTaskMemFree (pwfx);
				pwfx = pwfx_saved;
				pwfx_saved = NULL;
			}
			break;
		}
		if (hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) {
			if (exclusive) {
				exclusive = 0;
				goto retry;
			}
		}
		if (hr != AUDCLNT_E_UNSUPPORTED_FORMAT && hr != S_FALSE)
			goto error;
		if (hr == S_FALSE && pwfx_saved == NULL) {
			pwfx_saved = pwfx;
			pwfx = NULL;
		}
		rncnt++;
		if (rn[rncnt])
			continue;
		if (final)
			goto error;
		rncnt = 0;
		if (sd->freq < 44100) {
			sd->freq = 44100;
			continue;
		}
		if (sd->freq < 48000) {
			sd->freq = 48000;
			continue;
		}
		if (sd->channels != 2) {
			sd->channels = 2;
			continue;
		}
		final = 1;
		if (pwfx_saved == NULL)
			goto error;
		sd->channels = pwfx_saved->nChannels;
		sd->freq = pwfx_saved->nSamplesPerSec;
	}

	UINT32 DefaultDevicePeriod = (UINT32)( // frames =
		1.0 * hnsDefaultDevicePeriod * // hns *
		wavfmt.Format.nSamplesPerSec / // (frames / s) /
		1000 / // (ms / s) /
		10000 // (hns / s) /
		+ 0.5); // rounding
	UINT32  RequestedDuration = (UINT32)( // frames =
		1.0 * phnsMinimumDevicePeriod * // hns *
		wavfmt.Format.nSamplesPerSec / // (frames / s) /
		1000 / // (ms / s) /
		10000 // (hns / s) /
		+ 0.5); // rounding

	UINT32 DefaultPeriodInFrames = 0, FundamentalPeriodInFrames = 0, MinPeriodInFrames = 0, MaxPeriodInFrames = 0;
	if (s->AudioClientVersion >= 3 && sharemode == AUDCLNT_SHAREMODE_SHARED) {
		UINT32 CurrentPeriodInFrames;
		WAVEFORMATEX *outwfx;
		hr = s->pAudioClient->GetCurrentSharedModeEnginePeriod(&outwfx, &CurrentPeriodInFrames);
		if (SUCCEEDED(hr)) {
			write_log(_T("WASAPI: GetCurrentSharedModeEnginePeriod() CH=%d FREQ=%d BITS=%d CurrentPeriodInFrames=%u\n"),
				outwfx->nChannels, outwfx->nSamplesPerSec, outwfx->wBitsPerSample, CurrentPeriodInFrames);
		}
		hr = s->pAudioClient->GetSharedModeEnginePeriod(pwfx ? pwfx : &wavfmt.Format, &DefaultPeriodInFrames, &FundamentalPeriodInFrames, &MinPeriodInFrames, &MaxPeriodInFrames);
		if (SUCCEEDED(hr)) {
			write_log(_T("WASAPI: GetSharedModeEnginePeriod() DPIF=%u FPIF=%u MinPIF=%u MaxPIF=%d\n"),
				DefaultPeriodInFrames, FundamentalPeriodInFrames, MinPeriodInFrames, MaxPeriodInFrames);
		}
	}


	// magic adjustment
	sd->sndbufsize = sd->sndbufsize * 2 / 3;

	sd->samplesize = sd->channels * 16 / 8;

	// must be divisible by frame size
	sd->sndbufsize += sd->samplesize - 1;
	sd->sndbufsize = (sd->sndbufsize / sd->samplesize) * sd->samplesize;

	if (sd->sndbufsize > SND_MAX_BUFFER)
		sd->sndbufsize = SND_MAX_BUFFER;

	s->snd_configsize = sd->sndbufsize * sd->samplesize;

	s->bufferFrameCount = sd->sndbufsize;
	REFERENCE_TIME hnsRequestedDuration = // hns =
		(REFERENCE_TIME)(
		10000.0 * // (hns / ms) *
		1000 * // (ms / s) *
		s->bufferFrameCount / // frames /
		wavfmt.Format.nSamplesPerSec  // (frames / s)
		+ 0.5 // rounding
		);


	if (s->AudioClientVersion >= 3) {
		BOOL cap = FALSE;
		hr = s->pAudioClient->IsOffloadCapable((AUDIO_STREAM_CATEGORY)AudioCategory_Media, &cap);
		write_log(_T("WASAPI: IsOffloadCapable() returned %d %08x\n"), cap, hr);
	}

	int loop = 0;
	for (;;) {
		loop++;

		if (s->AudioClientVersion >= 3) {
			AudioClientProperties props;
			props.bIsOffload = FALSE;
			props.cbSize = sizeof props;
			props.eCategory = (AUDIO_STREAM_CATEGORY)AudioCategory_Media;
			props.Options = (AUDCLNT_STREAMOPTIONS)AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;;
			hr = s->pAudioClient->SetClientProperties(&props);
			if (FAILED(hr)) {
				write_log(_T("WASAPI: SetClientProperties() %08X\n"), hr);
			}
		}

		hr = E_FAIL;
		if (s->AudioClientVersion >= 3 && sharemode == AUDCLNT_SHAREMODE_SHARED && DefaultPeriodInFrames) {
			// this InitializeSharedAudioStream when requesting shared mode buffer that is smaller than 10ms.
			// default wasapi shared mode is min 10ms.
			int p10ms = sd->freq / 100;
			if (s->bufferFrameCount <= p10ms) {
				UINT32 period = s->bufferFrameCount;
				period /= FundamentalPeriodInFrames;
				period *= FundamentalPeriodInFrames;
				if (period < MinPeriodInFrames)
					period = MinPeriodInFrames;
				if (period > MaxPeriodInFrames)
					period = MaxPeriodInFrames;
				hr = s->pAudioClient->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, pwfx ? pwfx : &wavfmt.Format, &audioguid);
				write_log(_T("WASAPI: InitializeSharedAudioStream() Period=%u. HRESULT=%08x\n"), period, hr);
				if (SUCCEEDED(hr)) {
					UINT32 CurrentPeriodInFrames;
					WAVEFORMATEX *outwfx;
					HRESULT hr2 = s->pAudioClient->GetCurrentSharedModeEnginePeriod(&outwfx, &CurrentPeriodInFrames);
					if (SUCCEEDED(hr2)) {
						write_log(_T("WASAPI: GetCurrentSharedModeEnginePeriod() CH=%d FREQ=%d BITS=%d CurrentPeriodInFrames=%u\n"),
							outwfx->nChannels, outwfx->nSamplesPerSec, outwfx->wBitsPerSample, CurrentPeriodInFrames);
					}
				}
			}
		} else if (s->bufferFrameCount == 0) {
			hnsRequestedDuration = phnsMinimumDevicePeriod;
		}
		if (FAILED(hr)) {
			hr = s->pAudioClient->Initialize (sharemode, AUDCLNT_STREAMFLAGS_NOPERSIST | (s->pullmode ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0),
				hnsRequestedDuration, s->wasapiexclusive ? hnsRequestedDuration : 0, pwfx ? pwfx : &wavfmt.Format, &audioguid);
		}
		if (SUCCEEDED(hr))
			break;
		if (loop > 1)
			goto error;
		if (hr == AUDCLNT_E_BUFFER_SIZE_ERROR || hr == AUDCLNT_E_INVALID_DEVICE_PERIOD) {
			write_log(_T("WASAPI: AUDCLNT_E_BUFFER_SIZE_ERROR: %d\n"), s->bufferFrameCount);
			hr = s->pAudioClient->GetDevicePeriod(NULL, &hnsRequestedDuration);
			if (FAILED(hr)) {
				write_log(_T("WASAPI: GetDevicePeriod() %08X\n"), hr);
				goto error;
			}
			s->bufferFrameCount = (UINT32)( // frames =
				1.0 * hnsRequestedDuration * // hns *
				wavfmt.Format.nSamplesPerSec / // (frames / s) /
				1000 / // (ms / s) /
				10000 // (hns / s) /
				+ 0.5); // rounding
		} else if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
			UINT32 obfc = s->bufferFrameCount;
			hr = s->pAudioClient->GetBufferSize (&s->bufferFrameCount);
			if (FAILED (hr)) {
				write_log (_T("WASAPI: GetBufferSize() %08X\n"), hr);
				goto error;
			}
			write_log(_T("WASAPI: AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED: %d -> %d\n"), obfc, s->bufferFrameCount);
			hnsRequestedDuration = // hns =
				(REFERENCE_TIME)(
				10000.0 * // (hns / ms) *
				1000 * // (ms / s) *
				s->bufferFrameCount / // frames /
				wavfmt.Format.nSamplesPerSec  // (frames / s)
				+ 0.5 // rounding
				);
		} else {
			break;
		}
		s->pAudioClient->Release();
		hr = s->pDevice->Activate (s->AudioClientVersion >= 3 ?__uuidof(IAudioClient3) : __uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&s->pAudioClient);
		if (FAILED (hr)) {
			write_log (_T("WASAPI: Activate() %08X\n"), hr);
			goto error;
		}
	}
	if (FAILED (hr)) {
		write_log (_T("WASAPI: Initialize() %08X\n"), hr);
		goto error;
	}

	if (s->pullmode) {
		hr = s->pAudioClient->SetEventHandle(s->pullevent);
		if (FAILED(hr)) {
			write_log(_T("WASAPI: SetEventHandle() %08X\n"), hr);
			goto error;
		}
	}

	hr = s->pAudioClient->GetBufferSize (&s->bufferFrameCount);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetBufferSize() %08X\n"), hr);
		goto error;
	}

	hr = s->pAudioClient->GetService (__uuidof(IAudioRenderClient), (void**)&s->pRenderClient);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetService(IAudioRenderClient) %08X\n"), hr);
		goto error;
	}
	hr = s->pAudioClient->GetService (__uuidof(ISimpleAudioVolume), (void**)&s->pAudioVolume );
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetService(ISimpleAudioVolume) %08X\n"), hr);
		goto error;
	}
	hr = s->pAudioClient->GetService (__uuidof(IAudioClock), (void**)&s->pAudioClock);
	if (FAILED (hr)) {
		write_log (_T("WASAPI: GetService(IAudioClock) %08X\n"), hr);
	} else {
		hr = s->pAudioClock->GetFrequency (&s->wasapiclock);
		if (FAILED (hr)) {
			write_log (_T("WASAPI: GetFrequency() %08X\n"), hr);
		}
	}
	if (s->pullmode) {
		if (s->wasapiexclusive) {
			sd->sndbufsize = s->bufferFrameCount * sd->samplesize;
			s->pullbuffermaxlen = sd->sndbufsize;
		} else {
			sd->sndbufsize = s->bufferFrameCount * sd->samplesize / 2;
			s->pullbuffermaxlen = sd->sndbufsize * 2;
		}
		s->wasapigoodsize = s->bufferFrameCount;
		s->pullbuffer = xcalloc(uae_u8, s->pullbuffermaxlen);
		s->pullbufferlen = 0;
	} else {
		sd->sndbufsize = (s->bufferFrameCount / 8) * sd->samplesize;
		v = s->bufferFrameCount * sd->samplesize;
		v /= 2;
		if (sd->sndbufsize > v)
			sd->sndbufsize = v;
		s->wasapigoodsize = s->bufferFrameCount / 2;
	}

	write_log(_T("WASAPI: '%s'\nWASAPI: %s %s CH=%d FREQ=%d BUF=%d (%d)\n"),
		name,
		s->wasapiexclusive ? _T("Exclusive") : _T("Shared"),
		s->pullmode ? _T("Pull") : _T("Push"),
		sd->channels, sd->freq, sd->sndbufsize / sd->samplesize, s->bufferFrameCount);

	add_wasapi_endpointnotification(sd);
#if WASAPI_SESSION_NOTIFICATION
	add_wasapi_sessionnotification(sd);
#endif

	CoTaskMemFree (pwfx);
	CoTaskMemFree (pwfx_saved);
	CoTaskMemFree (name);

	return 1;

error:
	CoTaskMemFree (pwfx);
	CoTaskMemFree (pwfx_saved);
	CoTaskMemFree (name);
	close_audio_wasapi (sd);
	return 0;
}

static int open_audio_ds (struct sound_data *sd, int index)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	struct sound_dp *s = sd->data;
	HRESULT hr;
	DSBUFFERDESC sound_buffer;
	DSCAPS DSCaps;
	WAVEFORMATEXTENSIBLE wavfmt;
	LPDIRECTSOUNDBUFFER pdsb = NULL;
	int freq = sd->freq;
	int ch = sd->channels;
	int round, i;
	DWORD speakerconfig;
	int size;

	sd->devicetype = SOUND_DEVICE_DS;
	if (sd->sndbufsize < 0x80)
		sd->sndbufsize = 0x80;
	size = sd->sndbufsize * ch * 2;
	s->snd_configsize = size;
	sd->sndbufsize = size / 32;
	if (sd->sndbufsize > SND_MAX_BUFFER)
		sd->sndbufsize = SND_MAX_BUFFER;

	s->max_sndbufsize = size * 4;
	if (s->max_sndbufsize > SND_MAX_BUFFER2)
		s->max_sndbufsize = SND_MAX_BUFFER2;

	s->dsoundbuf = s->max_sndbufsize * 2;

	if (s->dsoundbuf > DSBSIZE_MAX)
		s->dsoundbuf = DSBSIZE_MAX;

	if (s->max_sndbufsize * 2 > s->dsoundbuf)
		s->max_sndbufsize = s->dsoundbuf / 2;
	sd->samplesize = sd->channels * 2;

	recalc_offsets (sd);

	hr = DirectSoundCreate8 (&sound_devices[index]->guid, &s->lpDS, NULL);
	if (FAILED (hr))  {
		write_log (_T("DS: DirectSoundCreate8() failure: %s\n"), DXError (hr));
		return 0;
	}

	hr = IDirectSound_SetCooperativeLevel (s->lpDS, mon->hMainWnd, DSSCL_PRIORITY);
	if (FAILED (hr)) {
		write_log (_T("DS: Can't set cooperativelevel: %s\n"), DXError (hr));
		goto error;
	}

	memset (&DSCaps, 0, sizeof (DSCaps));
	DSCaps.dwSize = sizeof (DSCaps);
	hr = IDirectSound_GetCaps (s->lpDS, &DSCaps);
	if (FAILED(hr)) {
		write_log (_T("DS: Error getting DirectSound capabilities: %s\n"), DXError (hr));
		goto error;
	}
	if (DSCaps.dwFlags & DSCAPS_EMULDRIVER) {
		write_log (_T("DS: Emulated DirectSound driver detected, don't complain if sound quality is crap :)\n"));
	}
	if (DSCaps.dwFlags & DSCAPS_CONTINUOUSRATE) {
		int minfreq = DSCaps.dwMinSecondarySampleRate;
		int maxfreq = DSCaps.dwMaxSecondarySampleRate;
		if (minfreq > freq && freq < 22050) {
			freq = minfreq;
			sd->freq = freq;
			write_log (_T("DS: minimum supported frequency: %d\n"), minfreq);
		}
		if (maxfreq < freq && freq > 44100) {
			freq = maxfreq;
			sd->freq = freq;
			write_log (_T("DS: maximum supported frequency: %d\n"), maxfreq);
		}
	}

	speakerconfig = fillsupportedmodes (sd, freq, supportedmodes);
	write_log (_T("DS: %08X "), speakerconfig);
	for (i = 0; supportedmodes[i].ch; i++)
		write_log (_T("%d:%08X "), supportedmodes[i].ch, supportedmodes[i].ksmode);
	write_log (_T("\n"));

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

		write_log (_T("DS: %08X,CH=%d,FREQ=%d '%s' buffer %d, dist %d\n"),
			ksmode, ch, freq, sound_devices[index]->name,
			s->max_sndbufsize / sd->samplesize, s->snd_configsize / sd->samplesize);

		memset (&sound_buffer, 0, sizeof (sound_buffer));
		sound_buffer.dwSize = sizeof (sound_buffer);
		sound_buffer.dwBufferBytes = s->dsoundbuf;
		sound_buffer.lpwfxFormat = &wavfmt.Format;
		sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
		sound_buffer.dwFlags |= DSBCAPS_CTRLVOLUME | (ch >= 4 ? DSBCAPS_LOCHARDWARE : DSBCAPS_LOCSOFTWARE);
		sound_buffer.guid3DAlgorithm = GUID_NULL;

		hr = IDirectSound_CreateSoundBuffer (s->lpDS, &sound_buffer, &pdsb, NULL);
		if (SUCCEEDED (hr))
			break;
		if (sound_buffer.dwFlags & DSBCAPS_LOCHARDWARE) {
			HRESULT hr2 = hr;
			sound_buffer.dwFlags &= ~DSBCAPS_LOCHARDWARE;
			sound_buffer.dwFlags |=  DSBCAPS_LOCSOFTWARE;
			hr = IDirectSound_CreateSoundBuffer (s->lpDS, &sound_buffer, &pdsb, NULL);
			if (SUCCEEDED(hr)) {
				//write_log (_T("DS: Couldn't use hardware buffer (switched to software): %s\n"), DXError (hr2));
				break;
			}
		}
		write_log (_T("DS: Secondary CreateSoundBuffer() failure: %s\n"), DXError (hr));
	}

	if (pdsb == NULL)
		goto error;
	hr = pdsb->QueryInterface (IID_IDirectSoundBuffer8, (LPVOID*)&s->lpDSBsecondary);
	IDirectSound_Release (pdsb);
	if (FAILED (hr))  {
		write_log (_T("DS: Secondary QueryInterface() failure: %s\n"), DXError (hr));
		goto error;
	}
	clearbuffer (sd);

	return 1;

error:
	close_audio_ds (sd);
	return 0;
}

int open_sound_device (struct sound_data *sd, int index, int bufsize, int freq, int channels)
{
	int ret = 0;
	struct sound_dp *sdp = xcalloc (struct sound_dp, 1);
	int type = sound_devices[index]->type;
	
	sd->data = sdp;
	sd->sndbufsize = bufsize;
	sd->freq = freq;
	sd->channels = channels;
	sd->paused = 1;
	sd->index = index;
	if (type == SOUND_DEVICE_AL)
		ret = open_audio_al (sd, index);
	else if (type == SOUND_DEVICE_DS)
		ret = open_audio_ds (sd, index);
	else if (type == SOUND_DEVICE_PA)
		ret = open_audio_pa (sd, index);
	else if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE)
		ret = open_audio_wasapi (sd, index, type == SOUND_DEVICE_WASAPI_EXCLUSIVE);
#if USE_XAUDIO
	else if (type == SOUND_DEVICE_XAUDIO2)
		ret = open_audio_xaudio2 (sd, index);
#endif
	sd->samplesize = sd->channels * 2;
	sd->sndbufframes = sd->sndbufsize / sd->samplesize;
	return ret;
}
void close_sound_device (struct sound_data *sd)
{
	pause_sound_device (sd);
	if (sd->devicetype == SOUND_DEVICE_AL)
		close_audio_al (sd);
	else if (sd->devicetype == SOUND_DEVICE_DS)
		close_audio_ds (sd);
	else if (sd->devicetype == SOUND_DEVICE_PA)
		close_audio_pa (sd);
	else if (sd->devicetype == SOUND_DEVICE_WASAPI || sd->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE)
		close_audio_wasapi (sd);
#if USE_XAUDIO
	else if (sd->devicetype == SOUND_DEVICE_XAUDIO2)
		close_audio_xaudio2 (sd);
#endif
	xfree (sd->data);
	sd->data = NULL;
	sd->index = -1;
}
void pause_sound_device (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	sd->paused = 1;
	gui_data.sndbuf_status = 0;
	gui_data.sndbuf = 0;
	if (sd->devicetype == SOUND_DEVICE_AL)
		pause_audio_al (sd);
	else if (sd->devicetype == SOUND_DEVICE_DS)
		pause_audio_ds (sd);
	else if (sd->devicetype == SOUND_DEVICE_PA)
		pause_audio_pa (sd);
	else if (sd->devicetype == SOUND_DEVICE_WASAPI || sd->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE)
		pause_audio_wasapi (sd);
#if USE_XAUDIO
	else if (sd->devicetype == SOUND_DEVICE_XAUDIO2)
		pause_audio_xaudio2 (sd);
#endif
	if (s->pullevent)
		ResetEvent(s->pullevent);
	if (s->pullevent2)
		ResetEvent(s->pullevent2);
}
void resume_sound_device (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	if (sd->devicetype == SOUND_DEVICE_AL)
		resume_audio_al (sd);
	else if (sd->devicetype == SOUND_DEVICE_DS)
		resume_audio_ds (sd);
	else if (sd->devicetype == SOUND_DEVICE_PA)
		resume_audio_pa (sd);
	else if (sd->devicetype == SOUND_DEVICE_WASAPI || sd->devicetype == SOUND_DEVICE_WASAPI_EXCLUSIVE)
		resume_audio_wasapi (sd);
#if USE_XAUDIO
	else if (sd->devicetype == SOUND_DEVICE_XAUDIO2)
		resume_audio_xaudio2 (sd);
#endif
	if (s->pullevent)
		ResetEvent(s->pullevent);
	if (s->pullevent2)
		ResetEvent(s->pullevent2);
	sd->paused = 0;
}


static int open_sound (void)
{
	int ret = 0, num, ch;
	int size = currprefs.sound_maxbsiz;

	if (!currprefs.produce_sound)
		return 0;
	config_changed = 1;
	/* Always interpret buffer size as number of samples, not as actual
	buffer size.  Of course, since 8192 is the default, we'll have to
	scale that to a sane value (assuming that otherwise 16 bits and
	stereo would have been enabled and we'd have done the shift by
	two anyway).  */
	size >>= 2;
	size &= ~63;

	sdp->softvolume = -1;
	num = enumerate_sound_devices ();
	if (currprefs.win32_soundcard >= num)
		currprefs.win32_soundcard = changed_prefs.win32_soundcard = 0;
	if (num == 0)
		return 0;
	ch = get_audio_nativechannels (currprefs.sound_stereo);
	ret = open_sound_device (sdp, currprefs.win32_soundcard, size, currprefs.sound_freq, ch);
	if (!ret)
		return 0;
	currprefs.sound_freq = changed_prefs.sound_freq = sdp->freq;
	if (ch != sdp->channels)
		currprefs.sound_stereo = changed_prefs.sound_stereo = get_audio_stereomode (sdp->channels);

	set_volume (currprefs.sound_volume_master, sdp->mute);
	if (get_audio_amigachannels (currprefs.sound_stereo) == 4)
		sample_handler = sample16ss_handler;
	else
		sample_handler = get_audio_ismono (currprefs.sound_stereo) ? sample16_handler : sample16s_handler;

	sdp->obtainedfreq = currprefs.sound_freq;

	have_sound = 1;
	sound_available = 1;
	gui_data.sndbuf_avail = audio_is_pull() == 0;

	paula_sndbufsize = sdp->sndbufsize;
	paula_sndbufpt = paula_sndbuffer;
	driveclick_init ();

	return 1;
}

void close_sound (void)
{
	config_changed = 1;
	gui_data.sndbuf = 0;
	gui_data.sndbuf_status = 3;
	gui_data.sndbuf_avail = false;
	if (! have_sound)
		return;
	close_sound_device (sdp);
	have_sound = 0;
	extrasndbufsize = 0;
	extrasndbuffered = 0;
	xfree(extrasndbuf);
	extrasndbuf = NULL;
}

bool sound_paused(void)
{
	return sdp->paused != 0;
}

void pause_sound (void)
{
	if (sdp->paused)
		return;
	if (!have_sound)
		return;
	pause_sound_device (sdp);
}

void resume_sound (void)
{
	if (!sdp->paused)
		return;
	if (!have_sound)
		return;
	resume_sound_device (sdp);
}

void reset_sound (void)
{
	if (!have_sound)
		return;
	clearbuffer (sdp);
}

int init_sound (void)
{
	bool started = false;
	gui_data.sndbuf_status = 3;
	gui_data.sndbuf = 0;
	gui_data.sndbuf_avail = false;
	if (!sound_available)
		return 0;
	if (currprefs.produce_sound <= 1)
		return 0;
	if (have_sound)
		return 1;
	if (!open_sound ())
		return 0;
	sdp->paused = 1;
	driveclick_reset ();
	reset_sound ();
	resume_sound ();
	if (!started &&
		(currprefs.win32_start_minimized && currprefs.win32_iconified_nosound ||
		currprefs.win32_start_uncaptured && currprefs.win32_inactive_nosound))
		pause_sound ();
	started = true;
	return 1;
}

static void disable_sound (void)
{
	close_sound ();
	currprefs.produce_sound = changed_prefs.produce_sound = 1;
}

static int reopen_sound (void)
{
	bool paused = sdp->paused != 0;
	close_sound ();
	int v = open_sound ();
	if (v && !paused)
		resume_sound_device(sdp);
	return v;
}

#define cf(x) if ((x) >= s->dsoundbuf) (x) -= s->dsoundbuf;

static void restart_sound_buffer2 (struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	DWORD playpos, safed;
	HRESULT hr;

	if (sd->devicetype != SOUND_DEVICE_DS)
		return;
	if (sdp->waiting_for_buffer != -1)
		return;
	hr = IDirectSoundBuffer_GetCurrentPosition (s->lpDSBsecondary, &playpos, &safed);
	if (FAILED (hr)) {
		write_log (_T("DS: DirectSoundBuffer_GetCurrentPosition failed, %s\n"), DXError (hr));
		return;
	}
	s->writepos = safed + s->snd_writeoffset;
	if (s->writepos < 0)
		s->writepos += s->dsoundbuf;
	cf (s->writepos);
}

void pause_sound_buffer (void)
{
	sdp->deactive = true;
	reset_sound ();
}

void restart_sound_buffer (void)
{
	sdp->deactive = false;
	restart_sound_buffer2 (sdp);
}

static int alcheck (struct sound_data *sd, int v)
{
	struct sound_dp *s = sd->data;
	int err = alGetError ();
	if (err != AL_NO_ERROR) {
		int v1, v2, v3;
		alGetSourcei (s->al_Source, AL_BUFFERS_PROCESSED, &v1);
		alGetSourcei (s->al_Source, AL_BUFFERS_QUEUED, &v2);
		alGetSourcei (s->al_Source, AL_SOURCE_STATE, &v3);
		write_log (_T("OpenAL %d: error %d. PROC=%d QUEUE=%d STATE=%d\n"), v, err, v1, v2, v3);
		write_log (_T("           %d %08x %08x %08x %d %d\n"),
			s->al_toggle, s->al_Buffers[s->al_toggle], s->al_format, s->al_bigbuffer, s->al_bufsize, sd->freq);
		return 1;
	}
	return 0;
}

static void finish_sound_buffer_al (struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;
	static int tfprev;
	int v, v2;

	if (!sd->waiting_for_buffer)
		return;
	if (savestate_state)
		return;

	alGetError ();

	memcpy (s->al_bigbuffer + s->al_offset, sndbuffer, sd->sndbufsize);
	s->al_offset += sd->sndbufsize;
	if (s->al_offset >= s->al_bufsize) {
		ALuint tmp;
		alGetSourcei (s->al_Source, AL_BUFFERS_PROCESSED, &v);
		while (v == 0 && sd->waiting_for_buffer < 0) {
			sleep_millis (1);
			alGetSourcei (s->al_Source, AL_SOURCE_STATE, &v);
			if (v != AL_PLAYING)
				break;
			alGetSourcei (s->al_Source, AL_BUFFERS_PROCESSED, &v);
		}

		alSourceUnqueueBuffers (s->al_Source, 1, &tmp);
		alGetError ();

		//	write_log (_T("           %d %08x %08x %08x %d %d\n"),
		//	    al_toggle, al_Buffers[al_toggle], al_format, al_bigbuffer, al_bufsize, currprefs.sound_freq);

		alBufferData (s->al_Buffers[s->al_toggle], s->al_format, s->al_bigbuffer, s->al_bufsize, sd->freq);
		alcheck (sd, 4);
		alSourceQueueBuffers (s->al_Source, 1, &s->al_Buffers[s->al_toggle]);
		alcheck (sd, 2);
		s->al_toggle++;
		if (s->al_toggle >= AL_BUFFERS)
			s->al_toggle = 0;

		alGetSourcei (s->al_Source, AL_BUFFERS_QUEUED, &v2);
		alcheck (sd, 5);
		alGetSourcei (s->al_Source, AL_SOURCE_STATE, &v);
		alcheck (sd, 3);
		if (v != AL_PLAYING && v2 >= AL_BUFFERS) {
			if (sd->waiting_for_buffer > 0) {
				write_log (_T("AL SOUND PLAY!\n"));
				alSourcePlay (s->al_Source);
				sd->waiting_for_buffer = -1;
				tfprev = timeframes + 10;
				tfprev = (tfprev / 10) * 10;
			} else {
				gui_data.sndbuf_status = 2;
				statuscnt = SND_STATUSCNT;
				write_log (_T("AL underflow\n"));
				clearbuffer (sd);
				sd->waiting_for_buffer = 1;
			}
		}
		s->al_offset = 0;
	}
	alcheck (sd, 1);

	alGetSourcei (s->al_Source, AL_SOURCE_STATE, &v);
	alcheck (sd, 6);

	if (v == AL_PLAYING && sd == sdp) {
		alGetSourcei (s->al_Source, AL_BYTE_OFFSET, &v);
		alcheck (sd, 7);
		v -= s->al_offset;

		docorrection (s, 100 * v / sd->sndbufsize, v / sd->samplesize, 100);

#if 0
		gui_data.sndbuf = 100 * v / sd->sndbufsize;
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
				write_log (_T("s=%+02.1f\n"), skipmode);
			tfprev = timeframes;
			sound_setadjust (skipmode);
		}
#endif
	}

	alcheck (sd, 0);
}

#if USE_XAUDIO
static void finish_sound_buffer_xaudio2 (struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;
	int stuck = 2000;
	int oldpadding = 0;
	int avail;
	XAUDIO2_BUFFER *buffer;
	int goodsize = XA_BUFFERS * sd->sndbufframes / 2;

	for (;;) {
		XAUDIO2_VOICE_STATE state;
		s->xsource->GetState (&state);
		avail = s->xsamplesplayed - state.SamplesPlayed;
		//write_log (_T("%d %d\n"), state.BuffersQueued,  goodsize - avail);
		if (state.BuffersQueued == 0 || (avail >= sd->sndbufframes && state.BuffersQueued < XA_BUFFERS)) {
			if (avail >= 2 * XA_BUFFERS * sd->sndbufframes) {
				statuscnt = SND_STATUSCNT;
				gui_data.sndbuf_status = 2;
			}
			break;
		}
		gui_data.sndbuf_status = 1;
		statuscnt = SND_STATUSCNT;
		sleep_millis (1);
		if (oldpadding == avail) {
			if (stuck-- < 0) {
				write_log (_T("XAUDIO2: sound stuck %d !?\n"), avail, sd->sndbufframes);
				reopen_sound ();
				return;
			}
		}
		oldpadding = avail;
	}

	docorrection (s, (goodsize - avail) * 1000 / goodsize, goodsize - avail, 100);

	memcpy (s->xdata[s->xabufcnt], sndbuffer, sd->sndbufsize);
	buffer = &s->xbuffer[s->xabufcnt];
	buffer->AudioBytes = sd->sndbufsize;
	buffer->pAudioData = (BYTE*)s->xdata[s->xabufcnt];
	hr = s->xsource->SubmitSourceBuffer (buffer);
	if (FAILED (hr)) {
		write_log (_T("XAUDIO2: SubmitSourceBuffer %08X\n"), hr);
		return;
	}
	s->xsamplesplayed += sd->sndbufframes;
	s->xabufcnt = (s->xabufcnt + 1) & (XA_BUFFERS - 1);
}
#endif

static void finish_sound_buffer_wasapi_push(struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;
	BYTE *pData;
	UINT32 numFramesPadding;
	int avail;
	int stuck = 2000;
	int oldpadding = 0;
	bool silence = false;

	for (;;) {
		hr = s->pAudioClient->GetCurrentPadding (&numFramesPadding);
		wasapi_check_state(sd, hr);
		if (FAILED (hr)) {
			write_log (_T("WASAPI: GetCurrentPadding() %08X\n"), hr);
			return;
		}
		avail = s->bufferFrameCount - numFramesPadding;
		if (avail >= sd->sndbufframes) {
			if (avail >= s->wasapigoodsize * 2 - sd->sndbufframes * 1) {
				statuscnt = SND_STATUSCNT;
				gui_data.sndbuf_status = 2;
			}
			break;
		}
		gui_data.sndbuf_status = 1;
		statuscnt = SND_STATUSCNT;
		sleep_millis (1);
		if (oldpadding == numFramesPadding) {
			if (stuck-- < 0) {
				write_log (_T("WASAPI: sound stuck %d %d %d !?\n"), s->bufferFrameCount, numFramesPadding, sd->sndbufframes);
				set_reset(sd);
				return;
			}
		}
		oldpadding = numFramesPadding;
	}

	docorrection (s, (s->wasapigoodsize - avail) * 1000 / s->wasapigoodsize, s->wasapigoodsize - avail, 100);

	hr = s->pRenderClient->GetBuffer (sd->sndbufframes, &pData);
	wasapi_check_state(sd, hr);
	if (SUCCEEDED (hr)) {
		if (silence)
			memset(pData, 0, sd->sndbufsize);
		else
			memcpy(pData, sndbuffer, sd->sndbufsize);
		s->pRenderClient->ReleaseBuffer (sd->sndbufframes, 0);
	}

}

static bool finish_sound_buffer_wasapi_pull_do(struct sound_data *sd)
{
	struct sound_dp *s = sd->data;
	HRESULT hr;
	BYTE *pData;

	if (s->pullbufferlen <= 0)
		return false;

	int frames = s->pullbufferlen / sd->samplesize;
	int avail = frames;

	if (!s->wasapiexclusive) {
		UINT32 numFramesPadding;
		hr = s->pAudioClient->GetCurrentPadding(&numFramesPadding);
		wasapi_check_state(sd, hr);
		if (FAILED(hr)) {
			write_log(_T("WASAPI: GetCurrentPadding() %08X\n"), hr);
			s->pullbufferlen = 0;
			return false;
		}
		avail = s->bufferFrameCount - numFramesPadding;
		if (!avail)
			return false;
		if (avail > frames)
			avail = frames;
	}

	ResetEvent(s->pullevent);

	hr = s->pRenderClient->GetBuffer(avail, &pData);
	wasapi_check_state(sd, hr);
	if (SUCCEEDED(hr)) {
		memcpy(pData, s->pullbuffer, avail * sd->samplesize);
		hr = s->pRenderClient->ReleaseBuffer(avail, 0);
		if (FAILED(hr)) {
			write_log(_T("WASAPI: ReleaseBuffer() %08X\n"), hr);
		}
	} else {
		write_log(_T("WASAPI: GetBuffer() %08X\n"), hr);
	}
	
	if (avail < frames) {
		memmove(s->pullbuffer, s->pullbuffer + avail * sd->samplesize, s->pullbufferlen - (avail  * sd->samplesize));
	}
	s->pullbufferlen -= avail * sd->samplesize;

	return true;
}

static void finish_sound_buffer_wasapi(struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;
	if (s->pullmode)
		finish_sound_buffer_pull(sd, sndbuffer);
	else
		finish_sound_buffer_wasapi_push(sd, sndbuffer);
}

static void finish_sound_buffer_ds (struct sound_data *sd, uae_u16 *sndbuffer)
{
	struct sound_dp *s = sd->data;
	static int tfprev;
	DWORD playpos, safepos, status;
	HRESULT hr;
	void *b1, *b2;
	DWORD s1, s2;
	int diff;
	int counter;

	if (!sd->waiting_for_buffer)
		return;

	if (sd->waiting_for_buffer == 1) {
		hr = s->lpDSBsecondary->Play (0, 0, DSBPLAY_LOOPING);
		if (FAILED (hr)) {
			write_log (_T("DS: Play failed: %s\n"), DXError (hr));
			restore_ds (sd, DSERR_BUFFERLOST);
			sd->waiting_for_buffer = 0;
			return;
		}
		hr = s->lpDSBsecondary->SetCurrentPosition (0);
		if (FAILED (hr)) {
			write_log (_T("DS: 1st SetCurrentPosition failed: %s\n"), DXError (hr));
			restore_ds (sd, DSERR_BUFFERLOST);
			sd->waiting_for_buffer = 0;
			return;
		}
		/* there are crappy drivers that return PLAYCURSOR = WRITECURSOR = 0 without this.. */
		counter = 5000;
		for (;;) {
			hr = s->lpDSBsecondary->GetCurrentPosition (&playpos, &s->safedist);
			if (playpos > 0)
				break;
			sleep_millis (1);
			counter--;
			if (counter < 0) {
				write_log (_T("DS: stuck?!?!\n"));
				disable_sound ();
				return;
			}
		}
		write_log (_T("DS: %d = (%d - %d)\n"), (s->safedist - playpos) / sd->samplesize, s->safedist / sd->samplesize, playpos / sd->samplesize);
		recalc_offsets (sd);
		s->safedist -= playpos;
		if (s->safedist < 64)
			s->safedist = 64;
		cf (s->safedist);
#if 0
		snd_totalmaxoffset_uf += s->safedist;
		cf (snd_totalmaxoffset_uf);
		snd_totalmaxoffset_of += s->safedist;
		cf (snd_totalmaxoffset_of);
		snd_maxoffset += s->safedist;
		cf (snd_maxoffset);
		snd_writeoffset += s->safedist;
		cf (snd_writeoffset);
#endif
		sd->waiting_for_buffer = -1;
		restart_sound_buffer2 (sd);
		write_log (_T("DS: bs=%d w=%d max=%d tof=%d tuf=%d\n"),
			sd->sndbufsize / sd->samplesize, s->snd_writeoffset / sd->samplesize,
			s->snd_maxoffset / sd->samplesize, s->snd_totalmaxoffset_of / sd->samplesize,
			s->snd_totalmaxoffset_uf / sd->samplesize);
		tfprev = timeframes + 10;
		tfprev = (tfprev / 10) * 10;
	}

	counter = 5000;
	hr = s->lpDSBsecondary->GetStatus (&status);
	if (FAILED (hr)) {
		write_log (_T("DS: GetStatus() failed: %s\n"), DXError (hr));
		restore_ds (sd, DSERR_BUFFERLOST);
		return;
	}
	if (status & DSBSTATUS_BUFFERLOST) {
		write_log (_T("DS: buffer lost\n"));
		restore_ds (sd, DSERR_BUFFERLOST);
		return;
	}
	if ((status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) != (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) {
		write_log (_T("DS: status = %08X\n"), status);
		restore_ds (sd, DSERR_BUFFERLOST);
		return;
	}
	for (;;) {
		hr = s->lpDSBsecondary->GetCurrentPosition (&playpos, &safepos);
		if (FAILED (hr)) {
			restore_ds (sd, hr);
			write_log (_T("DS: GetCurrentPosition failed: %s\n"), DXError (hr));
			return;
		}
		if (s->writepos >= safepos)
			diff = s->writepos - safepos;
		else
			diff = s->dsoundbuf - safepos + s->writepos;

		if (diff < sd->sndbufsize || diff > s->snd_totalmaxoffset_uf) {
#if 0
			hr = IDirectSoundBuffer_Lock (lpDSBsecondary, s->writepos, sndbufsize, &b1, &s1, &b2, &s2, 0);
			if (SUCCEEDED(hr)) {
				memset (b1, 0, s1);
				if (b2)
					memset (b2, 0, s2);
				IDirectSoundBuffer_Unlock (lpDSBsecondary, b1, s1, b2, s2);
			}
#endif
			gui_data.sndbuf_status = -1;
			statuscnt = SND_STATUSCNT;
			if (diff > s->snd_totalmaxoffset_uf)
				s->writepos += s->dsoundbuf - diff;
			s->writepos += sd->sndbufsize;
			cf (s->writepos);
			diff = s->safedist;
			break;
		}

		if (diff > s->snd_totalmaxoffset_of) {
			gui_data.sndbuf_status = 2;
			statuscnt = SND_STATUSCNT;
			restart_sound_buffer2 (sd);
			diff = s->snd_writeoffset;
			write_log (_T("DS: underflow (%d %d)\n"), diff / sd->samplesize, s->snd_totalmaxoffset_of / sd->samplesize);
			break;
		}

		if (diff > s->snd_maxoffset) {
			gui_data.sndbuf_status = 1;
			statuscnt = SND_STATUSCNT;
			sleep_millis (1);
			counter--;
			if (counter < 0) {
				write_log (_T("DS: sound system got stuck!?\n"));
				restore_ds (sd, DSERR_BUFFERLOST);
				reopen_sound ();
				return;
			}
			continue;
		}
		break;
	}

	hr = s->lpDSBsecondary->Lock (s->writepos, sd->sndbufsize, &b1, &s1, &b2, &s2, 0);
	if (restore_ds (sd, hr))
		return;
	if (FAILED (hr)) {
		write_log (_T("DS: lock failed: %s (%d %d)\n"), DXError (hr), s->writepos / sd->samplesize, sd->sndbufsize / sd->samplesize);
		return;
	}
	memcpy (b1, sndbuffer, s1);
	if (b2)
		memcpy (b2, (uae_u8*)sndbuffer + s1, s2);
	s->lpDSBsecondary->Unlock (b1, s1, b2, s2);

	if (sd == sdp) {
		double vdiff, m, skipmode;

		vdiff = (diff - s->snd_writeoffset) / sd->samplesize;
		m = 100.0 * vdiff / (s->max_sndbufsize / sd->samplesize);
		skipmode = sync_sound (m);

		if (tfprev != timeframes) {
			gui_data.sndbuf = vdiff * 1000 / (s->snd_maxoffset - s->snd_writeoffset);
			s->avg_correct += vdiff;
			s->cnt_correct++;
			double adj = (s->avg_correct / s->cnt_correct) / 50.0;
			if ((0 || sound_debug) && !(tfprev % 10))
				write_log (_T("%d,%d,%d,%d d%5d vd%5.0f s%+02.1f %.0f %+02.1f\n"),
				sd->sndbufsize / sd->samplesize, s->snd_configsize / sd->samplesize, s->max_sndbufsize / sd->samplesize,
				s->dsoundbuf / sd->samplesize, diff / sd->samplesize, vdiff, skipmode, s->avg_correct / s->cnt_correct, adj);
			tfprev = timeframes;
			if (skipmode > ADJUST_LIMIT2)
				skipmode = ADJUST_LIMIT2;
			if (skipmode < -ADJUST_LIMIT2)
				skipmode = -ADJUST_LIMIT2;
			sound_setadjust (skipmode + adj);
		}
	}

	s->writepos += sd->sndbufsize;
	cf (s->writepos);
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

static bool send_sound_do(struct sound_data *sd)
{
	int type = sd->devicetype;
	if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE) {
		return finish_sound_buffer_wasapi_pull_do(sd);
	} else if (type == SOUND_DEVICE_PA) {
		struct sound_dp *s = sd->data;
		ResetEvent(s->pullevent);
		SetEvent(s->pullevent2);
	}
	return false;
}

static void send_sound (struct sound_data *sd, uae_u16 *sndbuffer)
{
	int type = sd->devicetype;
	if (savestate_state)
		return;
	if (sd->paused)
		return;
	if (sd->softvolume >= 0) {
		uae_s16 *p = (uae_s16*)sndbuffer;
		for (int i = 0; i < sd->sndbufsize / 2; i++) {
			p[i] = p[i] * sd->softvolume / 32768;
		}
	}
	if (type == SOUND_DEVICE_AL)
		finish_sound_buffer_al (sd, sndbuffer);
	else if (type == SOUND_DEVICE_DS)
		finish_sound_buffer_ds (sd, sndbuffer);
	else if (type == SOUND_DEVICE_PA)
		finish_sound_buffer_pa (sd, sndbuffer);
	else if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE)
		finish_sound_buffer_wasapi (sd, sndbuffer);
#if USE_XAUDIO
	else if (type == SOUND_DEVICE_XAUDIO2)
		finish_sound_buffer_xaudio2 (sd, sndbuffer);
#endif
}

HANDLE get_sound_event(void)
{
	int type = sdp->devicetype;
	if (sdp->paused || sdp->deactive)
		return 0;
	if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE  || type == SOUND_DEVICE_PA) {
		struct sound_dp *s = sdp->data;
		if (s && s->pullmode) {
			return s->pullevent;
		}
	}
	return 0;
}

bool audio_is_event_frame_possible(int ms)
{
	int type = sdp->devicetype;
	if (sdp->paused || sdp->deactive || sdp->reset)
		return false;
	if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE || type == SOUND_DEVICE_PA) {
		struct sound_dp *s = sdp->data;
		int bufsize = (uae_u8*)paula_sndbufpt - (uae_u8*)paula_sndbuffer;
		bufsize /= sdp->samplesize;
		int todo = s->bufferFrameCount - bufsize;
		int samplesperframe = sdp->obtainedfreq / vblank_hz;
		return samplesperframe >= todo - samplesperframe;
	}
	return false;
}

int audio_is_pull(void)
{
	int type = sdp->devicetype;
	if (sdp->reset)
		return 0;
	if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE || type == SOUND_DEVICE_PA) {
		struct sound_dp *s = sdp->data;
		if (s && s->pullmode) {
			return sdp->paused || sdp->deactive ? -1 : 1;
		}
	}
	return 0;
}

int audio_pull_buffer(void)
{
	int cnt = 0;
	int type = sdp->devicetype;
	
	if (sdp->paused || sdp->deactive || sdp->reset)
		return 0;
	if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE || type == SOUND_DEVICE_PA) {
		struct sound_dp *s = sdp->data;
		if (s->pullbufferlen > 0) {
			cnt++;
			int size = (uae_u8*)paula_sndbufpt - (uae_u8*)paula_sndbuffer;
			if (size > sdp->sndbufsize * 2 / 3)
				cnt++;
		}
	}
	return cnt;
}

bool audio_is_pull_event(void)
{
	int type = sdp->devicetype;
	if (sdp->paused || sdp->deactive || sdp->reset)
		return false;
	if (type == SOUND_DEVICE_WASAPI || type == SOUND_DEVICE_WASAPI_EXCLUSIVE || type == SOUND_DEVICE_PA) {
		struct sound_dp *s = sdp->data;
		if (s->pullmode) {
			return WaitForSingleObject(s->pullevent, 0) == WAIT_OBJECT_0;
		}
	}
	return false;
}

bool audio_finish_pull(void)
{
	int type = sdp->devicetype;
	if (sdp->paused || sdp->deactive || sdp->reset)
		return false;
	if (type != SOUND_DEVICE_WASAPI && type != SOUND_DEVICE_WASAPI_EXCLUSIVE && type != SOUND_DEVICE_PA)
		return false;
	if (audio_pull_buffer() && audio_is_pull_event()) {
		return send_sound_do(sdp);
	}
	return false;
}

static void handle_reset(void)
{
	if (sdp->resetframe == timeframes)
		return;
	sdp->resetframe = timeframes;
	sdp->resetframecnt--;
	if (sdp->resetframecnt > 0)
		return;
	sdp->resetframecnt = 20;

	sdp->reset = false;
	if (!reopen_sound() || sdp->reset) {
		if (sdp->resetcnt <= 0) {
			write_log(_T("Reopen sound failed. Retrying with default device.\n"));
			close_sound();
			int type = sound_devices[currprefs.win32_soundcard]->type;
			int max = enumerate_sound_devices();
			for (int i = 0; i < max; i++) {
				if (sound_devices[i]->alname == NULL && sound_devices[i]->type == type) {
					currprefs.win32_soundcard = changed_prefs.win32_soundcard = i;
					if (open_sound())
						return;
					break;
				}
			}
			currprefs.produce_sound = changed_prefs.produce_sound = 1;
		} else {
			write_log(_T("Retrying sound.. %d..\n"), sdp->resetcnt);
			sdp->resetcnt--;
			sdp->reset = true;
		}
	} else {
		resume_sound_device(sdp);
	}
}


void finish_sound_buffer (void)
{
	static unsigned long tframe;
	int bufsize = (uae_u8*)paula_sndbufpt - (uae_u8*)paula_sndbuffer;

	if (sdp->reset) {
		handle_reset();
		paula_sndbufpt = paula_sndbuffer;
		return;
	}

	if (currprefs.turbo_emulation) {
		paula_sndbufpt = paula_sndbuffer;
		return;
	}
	if (currprefs.sound_stereo_swap_paula) {
		if (get_audio_nativechannels (currprefs.sound_stereo) == 2 || get_audio_nativechannels (currprefs.sound_stereo) == 4)
			channelswap((uae_s16*)paula_sndbuffer, bufsize / 2);
		else if (get_audio_nativechannels (currprefs.sound_stereo) == 6)
			channelswap6((uae_s16*)paula_sndbuffer, bufsize / 2);
	}
#ifdef DRIVESOUND
	driveclick_mix((uae_s16*)paula_sndbuffer, bufsize / 2, currprefs.dfxclickchannelmask);
#endif
	// must be after driveclick_mix
	paula_sndbufpt = paula_sndbuffer;
#ifdef AVIOUTPUT
	if (avioutput_audio) {
		if (AVIOutput_WriteAudio((uae_u8*)paula_sndbuffer, bufsize)) {
			if (avioutput_nosoundsync)
				sound_setadjust(0);
		}
	}
	if (avioutput_enabled && (!avioutput_framelimiter || avioutput_nosoundoutput))
		return;
#endif
	if (!have_sound)
		return;

	// we got buffer that was not full (recording active). Need special handling.
	if (bufsize < sdp->sndbufsize && !extrasndbuf) {
		extrasndbufsize = sdp->sndbufsize;
		extrasndbuf = xcalloc(uae_u8, sdp->sndbufsize);
		extrasndbuffered = 0;
	}

	if (statuscnt > 0 && tframe != timeframes) {
		tframe = timeframes;
		statuscnt--;
		if (statuscnt == 0)
			gui_data.sndbuf_status = 0;
	}
	if (gui_data.sndbuf_status == 3)
		gui_data.sndbuf_status = 0;

	if (extrasndbuf) {
		int size = extrasndbuffered + bufsize;
		int copied = 0;
		if (size > extrasndbufsize) {
			copied = extrasndbufsize - extrasndbuffered;
			memcpy(extrasndbuf + extrasndbuffered, paula_sndbuffer, copied);
			send_sound(sdp, (uae_u16*)extrasndbuf);
			extrasndbuffered = 0;
		}
		memcpy(extrasndbuf + extrasndbuffered, (uae_u8*)paula_sndbuffer + copied, bufsize - copied);
		extrasndbuffered += bufsize - copied;
	} else {
		send_sound(sdp, paula_sndbuffer);
	}
}

static BOOL CALLBACK DSEnumProc (LPGUID lpGUID, LPCTSTR lpszDesc, LPCTSTR lpszDrvName, LPVOID lpContext)
{
	struct sound_device **sd = (struct sound_device**)lpContext;
	int i;

	for (i = 0; i < MAX_SOUND_DEVICES; i++) {
		if (sd[i] == NULL)
			break;
	}
	if (i >= MAX_SOUND_DEVICES)
		return TRUE;

	sd[i] = xcalloc (struct sound_device, 1);
	if (lpGUID != NULL)
		memcpy (&sd[i]->guid, lpGUID, sizeof (GUID));
	sd[i]->name = my_strdup (lpszDesc);
	sd[i]->type = SOUND_DEVICE_DS;
	sd[i]->cfgname = my_strdup (sd[i]->name);
	sd[i]->prefix = my_strdup(_T("DS"));
	return TRUE;
}

static void wasapi_enum (struct sound_device **sdp)
{
	HRESULT hr;
	IMMDeviceEnumerator *enumerator;
	IMMDeviceCollection *col;
	int i, cnt, cnt2, start;

	write_log (_T("Enumerating WASAPI devices...\n"));
	for (cnt = 0; cnt < MAX_SOUND_DEVICES; cnt++) {
		if (sdp[cnt] == NULL)
			break;
	}
	if (cnt >= MAX_SOUND_DEVICES)
		return;
	start = cnt;

	hr = CoCreateInstance (__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (SUCCEEDED (hr)) {
		hr = enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &col);
		if (SUCCEEDED (hr)) {
			UINT num;
			hr = col->GetCount (&num);
			if (SUCCEEDED (hr)) {
				for (i = 0; i < num && cnt < MAX_SOUND_DEVICES; i++) {
					IMMDevice *dev;
					LPWSTR devid = NULL;
					LPWSTR devname = NULL;
					hr = col->Item (i, &dev);
					if (SUCCEEDED (hr)) {
						IPropertyStore *prop;
						dev->GetId (&devid);
						hr = dev->OpenPropertyStore (STGM_READ, &prop);
						if (SUCCEEDED (hr)) {
							PROPVARIANT pv;
							PropVariantInit (&pv);
							hr = prop->GetValue (PKEY_Device_FriendlyName, &pv);
							if (SUCCEEDED (hr)) {
								devname = my_strdup (pv.pwszVal);
							}
							PropVariantClear (&pv);
							prop->Release ();
						}
						dev->Release ();
					}
					if (devid && devname) {
						if (i == 0) {
							sdp[cnt] = xcalloc (struct sound_device, 1);
							sdp[cnt]->cfgname = my_strdup (_T("Default Audio Device"));
							sdp[cnt]->type = SOUND_DEVICE_WASAPI;
							sdp[cnt]->name = my_strdup (_T("Default Audio Device"));
							sdp[cnt]->alname = NULL;
							cnt++;
						}
						if (cnt < MAX_SOUND_DEVICES) {
							sdp[cnt] = xcalloc (struct sound_device, 1);
							sdp[cnt]->cfgname = my_strdup (devname);
							sdp[cnt]->type = SOUND_DEVICE_WASAPI;
							sdp[cnt]->name = my_strdup (devname);
							sdp[cnt]->alname = my_strdup (devid);
							cnt++;
						}
					}
					xfree (devname);
					CoTaskMemFree (devid);
				}
			}
			col->Release ();
		}
		enumerator->Release ();
	}
	cnt2 = cnt;
	for (i = start; i < cnt2; i++) {
		TCHAR buf[1000];
		_stprintf (buf, _T("WASAPIX:%s"), sdp[i]->cfgname);
		sdp[cnt] = xcalloc (struct sound_device, 1);
		sdp[cnt]->cfgname = my_strdup(buf);
		sdp[cnt]->prefix = my_strdup(_T("WASAPIX"));
		sdp[cnt]->name = my_strdup (sdp[i]->name);
		sdp[cnt]->alname = sdp[i]->alname ? my_strdup (sdp[i]->alname) : NULL;
		_stprintf (buf, _T("WASAPI:%s"), sdp[i]->cfgname);
		sdp[cnt]->type = SOUND_DEVICE_WASAPI_EXCLUSIVE;
		xfree (sdp[i]->cfgname);
		sdp[i]->cfgname = my_strdup (buf);
		sdp[i]->prefix = my_strdup(_T("WASAPI"));
		cnt++;
	}

}

static void OpenALEnumerate (struct sound_device **sds, const char *pDeviceNames, const char *ppDefaultDevice, int skipdetect)
{
	while (pDeviceNames && *pDeviceNames) {
		ALCdevice *pDevice;
		const char *devname;
		int i, ok;

		for (i = 0; i < MAX_SOUND_DEVICES; i++) {
			if (sds[i] == NULL)
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
					alcMakeContextCurrent (NULL);
					alcDestroyContext (context);
				}
				alcCloseDevice (pDevice);
			}
		} else {
			ok = 1;
		}
		if (ok) {
			TCHAR tmp[MAX_DPATH];
			sds[i] = xcalloc (struct sound_device, 1);
			sds[i]->type = SOUND_DEVICE_AL;
			if (ppDefaultDevice) {
				TCHAR *tdevname = au (devname);
				_stprintf (tmp, _T("Default [%s]"), tdevname);
				xfree (tdevname);
				sds[i]->alname = my_strdup_ansi (ppDefaultDevice);
				sds[i]->name = my_strdup (tmp);
			} else {
				sds[i]->alname = my_strdup_ansi (pDeviceNames);
				sds[i]->name = my_strdup_ansi (pDeviceNames);
			}
			_stprintf (tmp, _T("OPENAL:%s"), sds[i]->alname);
			sds[i]->cfgname = my_strdup (tmp);
			sds[i]->prefix = my_strdup(_T("OPENAL"));
		}
		if (ppDefaultDevice)
			ppDefaultDevice = NULL;
		else
			pDeviceNames += strlen (pDeviceNames) + 1;
	}
}

#if USE_XAUDIO
static void xaudioenumerate (struct sound_device **sds)
{
	IXAudio2 *xaudio2 = NULL;
	HRESULT hr;
	int i, j;
	UINT32 num;

	hr = XAudio2Create (&xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED (hr)) {
		write_log (_T("XAudio2 enumeration failed, %08x\n"), hr);
		return;
	}
	hr = xaudio2->GetDeviceCount (&num);
	if (SUCCEEDED (hr)) {
		for (i = 0; i < num; i++) {
			XAUDIO2_DEVICE_DETAILS dd;
			hr = xaudio2->GetDeviceDetails (i, &dd);
			if (FAILED (hr))
				continue;
			for (j = 0; j < MAX_SOUND_DEVICES; j++) {
				if (sds[j] == NULL)
					break;
			}
			if (j < MAX_SOUND_DEVICES) {
				sds[j] = xcalloc (struct sound_device, 1);
				sds[j]->type = SOUND_DEVICE_XAUDIO2;
				sds[j]->panum = i;
				sds[j]->name = my_strdup (dd.DisplayName);
				sds[j]->cfgname = my_strdup (dd.DeviceID);
			}
		}
	}
	xaudio2->Release();
}
#endif

#define PORTAUDIO 1
#if PORTAUDIO
static void PortAudioEnumerate (struct sound_device **sds)
{
	int num;
	int i, j;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH], *s1, *s2;

	num = Pa_GetDeviceCount ();
	if (num < 0) {
		TCHAR *errtxt = au (Pa_GetErrorText (num));
		write_log (_T("PA: Pa_GetDeviceCount() failed: %08x (%s)\n"), num, errtxt);
		xfree (errtxt);
		return;
	}
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
			if (sds[i] == NULL)
				break;
		}
		if (i >= MAX_SOUND_DEVICES)
			return;
		sds[i] = xcalloc (struct sound_device, 1);
		s1 = au (hai->name);
		s2 = au (di->name);
		_stprintf (tmp, _T("[%s] %s"), s1, s2);
		xfree (s2);
		xfree (s1);
		sds[i]->type = SOUND_DEVICE_PA;
		sds[i]->name = my_strdup (tmp);
		sds[i]->prefix = my_strdup(_T("PORTAUDIO"));
		_stprintf (tmp2, _T("PORTAUDIO:%s"), tmp);
		sds[i]->cfgname = my_strdup (tmp2);
		sds[i]->panum = j;
	}
}
#endif

static LONG WINAPI ExceptionFilter (struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

int force_directsound;
int enumerate_sound_devices (void)
{
	if (!num_sound_devices) {
		HMODULE l = NULL;
		if (os_vista && (sounddrivermask & SOUNDDRIVER_WASAPI)) {
			wasapi_enum(sound_devices);
		}
		if ((1 || force_directsound || !os_vista) && (sounddrivermask & SOUNDDRIVER_DS)) {
			write_log(_T("Enumerating DirectSound devices..\n"));
			DirectSoundEnumerate ((LPDSENUMCALLBACK)DSEnumProc, sound_devices);
		}
		DirectSoundCaptureEnumerate ((LPDSENUMCALLBACK)DSEnumProc, record_devices);
#if USE_XAUDIO
		if (sounddrivermask & SOUNDDRIVE_XAUDIO2)
			xaudioenumerate (sound_devices);
#endif
		if (sounddrivermask & SOUNDDRIVER_OPENAL) {
			__try {
				if (isdllversion (_T("openal32.dll"), 6, 14, 357, 22)) {
					write_log (_T("Enumerating OpenAL devices..\n"));
					if (alcIsExtensionPresent (NULL, "ALC_ENUMERATION_EXT")) {
						const char* ppDefaultDevice = alcGetString (NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
						const char* pDeviceNames = alcGetString (NULL, ALC_DEVICE_SPECIFIER);
						if (alcIsExtensionPresent (NULL, "ALC_ENUMERATE_ALL_EXT"))
							pDeviceNames = alcGetString (NULL, ALC_ALL_DEVICES_SPECIFIER);
						OpenALEnumerate (sound_devices, pDeviceNames, ppDefaultDevice, FALSE);
#if 0
						ppDefaultDevice = alcGetString (NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
						pDeviceNames = alcGetString (NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
						OpenALEnumerate (record_devices, pDeviceNames, ppDefaultDevice, TRUE);
#endif
					}
				}
			} __except(ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
				write_log (_T("OpenAL enumeration crashed!\n"));
				flush_log ();
			}
		}
#if PORTAUDIO
		if (sounddrivermask & SOUNDDRIVER_PORTAUDIO) {
			__try {
#ifdef CPU_64_BIT
				HMODULE hm = WIN32_LoadLibrary (_T("portaudio.dll"));
#else
				HMODULE hm = WIN32_LoadLibrary (_T("portaudio_x86.dll"));
#endif
				if (hm) {
					TCHAR *s;
					PaError err;
					write_log (_T("Enumerating PortAudio devices..\n"));
					s = au (Pa_GetVersionText ());
					write_log (_T("%s (%d)\n"), s, Pa_GetVersion ());
					xfree (s);
					if (Pa_GetVersion () >= 1899) {
						err = Pa_Initialize ();
						if (err == paNoError) {
							PortAudioEnumerate (sound_devices);
						} else {
							s = au (Pa_GetErrorText (err));
							write_log (_T("Portaudio initialization failed: %d (%s)\n"),
								err, s);
							xfree (s);
							FreeLibrary (hm);
						}
					} else {
						write_log (_T("Too old PortAudio library\n"));
						flush_log ();
						FreeLibrary (hm);
					}
				}
			} __except(ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
				write_log (_T("Portaudio enumeration crashed!\n"));
			}
		}
#endif
		write_log (_T("Enumeration end\n"));
		for (num_sound_devices = 0; num_sound_devices < MAX_SOUND_DEVICES; num_sound_devices++) {
			if (sound_devices[num_sound_devices] == NULL)
				break;
		}
		for (num_record_devices = 0; num_record_devices < MAX_SOUND_DEVICES; num_record_devices++) {
			if (record_devices[num_record_devices] == NULL)
				break;
		}
	}
	return num_sound_devices;
}

#include <mmdeviceapi.h>
#include <endpointvolume.h>

/*
Based on
http://blogs.msdn.com/larryosterman/archive/2007/03/06/how-do-i-change-the-master-volume-in-windows-vista.aspx
*/

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
	hr = CoCreateInstance (__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
	if (FAILED (hr))
		return 0;
	hr = deviceEnumerator->GetDefaultAudioEndpoint (eRender, eConsole, &defaultDevice);
	if (SUCCEEDED (hr)) {
		hr = defaultDevice->Activate (__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID *)&endpointVolume);
		if (SUCCEEDED (hr)) {
			if (setvolume) {
				if (SUCCEEDED (endpointVolume->SetMasterVolumeLevelScalar ((float)(*volume) / (float)65535.0, NULL)))
					ok++;
				if (SUCCEEDED (endpointVolume->SetMute (*mute, NULL)))
					ok++;
			} else {
				float vol;
				if (SUCCEEDED (endpointVolume->GetMasterVolumeLevelScalar (&vol))) {
					ok++;
					*volume = vol * 65535.0;
				}
				if (SUCCEEDED (endpointVolume->GetMute (mute))) {
					ok++;
				}
			}
			endpointVolume->Release ();
		}
		defaultDevice->Release ();
	}
	deviceEnumerator->Release ();
	CoUninitialize ();
	return ok == 2;
}

static void mcierr (const TCHAR *str, DWORD err)
{
	TCHAR es[1000];
	if (err == MMSYSERR_NOERROR)
		return;
	if (mciGetErrorString (err, es, sizeof es / sizeof (TCHAR)))
		write_log (_T("MCIErr: %s: %d = '%s'\n"), str, err, es);
	else
		write_log (_T("%s, errcode=%d\n"), str, err);
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
					mcierr (_T("mixerGetLineControls Mute"), result);
			} else
				mcierr (_T("mixerGetLineControls Volume"), result);
		} else
			mcierr (_T("mixerGetLineInfo"), result);
		mixerClose (hMixer);
	} else
		mcierr (_T("mixerOpen"), result);
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
		sdp->mute = sdp->mute ? 0 : 1;
	else
		sdp->mute = newmute;
	set_volume (currprefs.sound_volume_master, sdp->mute);
	config_changed = 1;
}

void sound_volume (int dir)
{
	currprefs.sound_volume_master -= dir * 10;
	currprefs.sound_volume_cd -= dir * 10;
	if (currprefs.sound_volume_master < 0)
		currprefs.sound_volume_master = 0;
	if (currprefs.sound_volume_master > 100)
		currprefs.sound_volume_master = 100;
	changed_prefs.sound_volume_master = currprefs.sound_volume_master;
	if (currprefs.sound_volume_cd < 0)
		currprefs.sound_volume_cd = 0;
	if (currprefs.sound_volume_cd > 100)
		currprefs.sound_volume_cd = 100;
	changed_prefs.sound_volume_cd = currprefs.sound_volume_cd;
	set_volume (currprefs.sound_volume_master, sdp->mute);
	config_changed = 1;
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
	config_changed = 1;
}
