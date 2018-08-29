
/*
* UAE
*
* Win32 audio player for CDA emulation
*
* Copyright 2010 Toni Wilen
*
*/

#define CDADS 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "dxwrap.h"
#include "audio.h"

#include <dsound.h>
#include <mmreg.h>

#include "win32.h"

#include "cda_play.h"

cda_audio::~cda_audio()
{
	if (active) {
		wait(0);
		wait(1);
#if CDADS
		if (dsnotify)
			dsnotify->Release();
		if (dsbuf)
			dsbuf->Release();
		if (ds)
			ds->Release();
		if (notifyevent[0])
			CloseHandle(notifyevent[0]);
		if (notifyevent[1])
			CloseHandle(notifyevent[1]);
#else
		for (int i = 0; i < 2; i++)
			waveOutUnprepareHeader(wavehandle, &whdr[i], sizeof(WAVEHDR));
		if (wavehandle != NULL)
			waveOutClose(wavehandle);
#endif
	}
	for (int i = 0; i < 2; i++) {
		xfree (buffers[i]);
		buffers[i] = NULL;
	}
}

cda_audio::cda_audio(int num_sectors, int sectorsize, int samplerate, bool internalmode)
{
	active = false;
	playing = false;
	volume[0] = volume[1] = 0;

	bufsize = num_sectors * sectorsize;
	this->sectorsize = sectorsize;
	for (int i = 0; i < 2; i++) {
		buffers[i] = xcalloc (uae_u8, num_sectors * ((bufsize + 4095) & ~4095));
	}
	this->num_sectors = num_sectors;

	if (internalmode)
		return;

	WAVEFORMATEX wav;
	memset (&wav, 0, sizeof (WAVEFORMATEX));

	wav.cbSize = 0;
	wav.nChannels = 2;
	wav.nSamplesPerSec = samplerate;
	wav.wBitsPerSample = 16;
	wav.nBlockAlign = wav.wBitsPerSample / 8 * wav.nChannels;
	wav.nAvgBytesPerSec = wav.nBlockAlign * wav.nSamplesPerSec;
	wav.wFormatTag = WAVE_FORMAT_PCM;

#if CDADS
	LPDIRECTSOUNDBUFFER pdsb;
	WAVEFORMATEXTENSIBLE wavfmt;
	DSBUFFERDESC desc;
	HRESULT hr;

	dsnotify = NULL;
	dsbuf = NULL;
	ds = NULL;
	notifyevent[0] = notifyevent[1] = NULL;

	hr = DirectSoundCreate8 (&sound_devices[currprefs.win32_soundcard].guid, &ds, NULL);
	if (FAILED (hr))  {
		write_log (_T("CDA: DirectSoundCreate8() failure: %s\n"), DXError (hr));
		return;
	}

	hr = ds->SetCooperativeLevel (hMainWnd, DSSCL_PRIORITY);
	if (FAILED (hr)) {
		write_log (_T("CDA: Can't set cooperativelevel: %s\n"), DXError (hr));
		return;
	}

	wavfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wavfmt.Format.nChannels = 2;
	wavfmt.Format.nSamplesPerSec = 44100;
	wavfmt.Format.wBitsPerSample = 16;
	wavfmt.Format.nBlockAlign = wavfmt.Format.wBitsPerSample / 8 * wavfmt.Format.nChannels;
	wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;
	wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	wavfmt.Samples.wValidBitsPerSample = 16;
	wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);

	memset (&desc, 0, sizeof desc);
	desc.dwSize = sizeof desc;
	desc.dwBufferBytes = 2 * bufsize;
	desc.lpwfxFormat = &wavfmt.Format;
	desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
	desc.dwFlags |= DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME;
	desc.guid3DAlgorithm = GUID_NULL;

	hr = ds->CreateSoundBuffer (&desc, &pdsb, NULL);
	if (FAILED (hr)) {
		write_log (_T("CDA: IDirectSound_CreateSoundBuffer %s\n"), DXError (hr));
		return;
	}
	hr = pdsb->QueryInterface (IID_IDirectSoundBuffer8, (LPVOID*)&dsbuf);
	IDirectSound_Release (pdsb);
	if (FAILED (hr))  {
		write_log (_T("CDA: Secondary QueryInterface() failure: %s\n"), DXError (hr));
		return;
	}
	hr = dsbuf->QueryInterface (IID_IDirectSoundNotify, (LPVOID*)&dsnotify);
	if (FAILED (hr))  {
		write_log (_T("CDA: IID_IDirectSoundNotify QueryInterface() failure: %s\n"), DXError (hr));
		return;
	}

	notifyevent[0] = CreateEvent (NULL, TRUE, FALSE, NULL);
	notifyevent[1] = CreateEvent (NULL, TRUE, FALSE, NULL);
	DSBPOSITIONNOTIFY nf[2];
	nf[0].dwOffset = bufsize / num_sectors;
	nf[1].dwOffset = bufsize + bufsize / num_sectors;
	nf[0].hEventNotify = notifyevent[0];
	nf[1].hEventNotify = notifyevent[1];
	hr = dsnotify->SetNotificationPositions(2, nf);

	active = true;
#else
	MMRESULT mmr;
	mmr = waveOutOpen (&wavehandle, WAVE_MAPPER, &wav, 0, 0, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT);
	if (mmr != MMSYSERR_NOERROR) {
		write_log (_T("IMAGE CDDA: wave open %d\n"), mmr);
		return;
	}
	for (int i = 0; i < 2; i++) {
		memset (&whdr[i], 0, sizeof(WAVEHDR));
		whdr[i].dwBufferLength = sectorsize * num_sectors;
		whdr[i].lpData = (LPSTR)buffers[i];
		mmr = waveOutPrepareHeader (wavehandle, &whdr[i], sizeof (WAVEHDR));
		if (mmr != MMSYSERR_NOERROR) {
			write_log (_T("IMAGE CDDA: waveOutPrepareHeader %d:%d\n"), i, mmr);
			return;
		}
		whdr[i].dwFlags |= WHDR_DONE;
	}
	active = true;
	playing = true;
#endif
}

void cda_audio::setvolume(int left, int right)
{
	for (int j = 0; j < 2; j++) {
		volume[j] = j == 0 ? left : right;
		volume[j] = sound_cd_volume[j] * volume[j] / 32768;
		if (volume[j])
			volume[j]++;
		volume[j] = volume[j] * (100 - currprefs.sound_volume_master) / 100;
		if (volume[j] >= 32768)
			volume[j] = 32768;
	}
#if CDADS
	LONG vol = DSBVOLUME_MIN;
	int volume = master * left / 32768;
	if (volume < 100)
		vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (1 - volume / 100.0)));
	HRESULT hr = dsbuf->SetVolume(vol);
	if (FAILED (hr))
		write_log (_T("CDA: SetVolume(%d) failed: %s\n"), vol, DXError (hr));
#endif
}
bool cda_audio::play(int bufnum)
{
	if (!active)
		return false;
#if CDADS
	DWORD status;
	HRESULT hr = dsbuf->GetStatus (&status);
	if (FAILED (hr)) {
		write_log (_T("CDA: GetStatus() failed: %s\n"), DXError (hr));
		return false;
	}
	if (status & DSBSTATUS_BUFFERLOST) {
		write_log (_T("CDA: bufferlost\n"));
		return false;
	}
	if ((status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) != (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) {
		dsbuf->SetCurrentPosition((1 - bufnum) * bufsize);
		dsbuf->Play(0, 0, DSBPLAY_LOOPING);
		playing = true;
	}
	PVOID ptr;
	DWORD len;
	if (SUCCEEDED(dsbuf->Lock(bufnum * bufsize, bufsize, &ptr, &len, NULL, NULL, 0))) {
		memcpy (ptr, buffers[bufnum], bufsize);
		dsbuf->Unlock(ptr, len, NULL, NULL);
	}
	return true;
#else
	uae_s16 *p = (uae_s16*)(buffers[bufnum]);
	if (volume[0] != 32768 || volume[1] != 32768) {
		for (int i = 0; i < num_sectors * sectorsize / 4; i++) {
			p[i * 2 + 0] = p[i * 2 + 0] * volume[0] / 32768;
			p[i * 2 + 1] = p[i * 2 + 1] * volume[1] / 32768;
		}
	}
	MMRESULT mmr = waveOutWrite (wavehandle, &whdr[bufnum], sizeof (WAVEHDR));
	if (mmr != MMSYSERR_NOERROR) {
		write_log (_T("IMAGE CDDA: waveOutWrite %d\n"), mmr);
		return false;
	}
	return true;
#endif
}
void cda_audio::wait(int bufnum)
{
	if (!active || !playing)
		return;
#if CDADS
	WaitForSingleObject (notifyevent[bufnum], INFINITE);
	ResetEvent (notifyevent[bufnum]);
#else
	while (!(whdr[bufnum].dwFlags & WHDR_DONE))
		Sleep (10);
#endif
}

bool cda_audio::isplaying(int bufnum)
{
	if (!active || !playing)
		return false;
	return (whdr[bufnum].dwFlags & WHDR_DONE) == 0;
}
