// Sound-board recording through WASAPI.
#include "sysconfig.h"
#include "sysdeps.h"
#include "sndboard_host.h"

static int capture_owner = SNDBOARD_CAPTURE_OWNER_NONE;

#include <mmdeviceapi.h>
#include <Audioclient.h>

#define REFTIMES_PER_SEC  10000000

static const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
static const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
static const IID IID_IAudioClient = __uuidof(IAudioClient);
static const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

#define EXIT_ON_ERROR(hres) if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  if ((punk) != NULL) { (punk)->Release(); (punk) = NULL; }

static IMMDeviceEnumerator *pEnumerator = NULL;
static IMMDevice *pDevice = NULL;
static IAudioClient *pAudioClient = NULL;
static IAudioCaptureClient *pCaptureClient = NULL;
static bool capture_started;
static int capture_read_count;
static bool capture_nonzero_seen;

uae_u8 *host_sndboard_get_buffer(int *frames)
{
	HRESULT hr;
	UINT32 numFramesAvailable;
	BYTE *pData;
	DWORD flags = 0;

	*frames = -1;
	if (!capture_started)
		return NULL;
	hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
	if (FAILED(hr)) {
		write_log(_T("GetBuffer failed %08x\n"), hr);
		return NULL;
	}
	*frames = numFramesAvailable;
	if (numFramesAvailable > 0) {
		capture_read_count++;
		if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
			if (capture_read_count <= 5)
				write_log(_T("sndboard capture read %u silent frames from host input\n"), numFramesAvailable);
		} else if (!capture_nonzero_seen) {
			bool has_signal = false;
			int bytes = numFramesAvailable * 4;
			for (int i = 0; i < bytes; i++) {
				if (pData[i] != 0) {
					has_signal = true;
					break;
				}
			}
			if (has_signal) {
				capture_nonzero_seen = true;
				write_log(_T("sndboard capture received first non-silent buffer after %d reads\n"), capture_read_count);
			} else if (capture_read_count <= 5) {
				write_log(_T("sndboard capture read %u silent frames from host input\n"), numFramesAvailable);
			}
		}
	}
	return pData;
}

void host_sndboard_release_buffer(uae_u8 *buffer, int frames)
{
	HRESULT hr;
	if (!capture_started || frames < 0)
		return;
	hr = pCaptureClient->ReleaseBuffer(frames);
	if (FAILED(hr)) {
		write_log(_T("ReleaseBuffer failed %08x\n"), hr);
	}
}

void host_sndboard_free_capture(int owner)
{
	if (capture_owner != owner)
		return;
	if (capture_started)
		pAudioClient->Stop();
	capture_started = false;
	capture_read_count = 0;
	capture_nonzero_seen = false;
	capture_owner = SNDBOARD_CAPTURE_OWNER_NONE;
	SAFE_RELEASE(pEnumerator)
	SAFE_RELEASE(pDevice)
	SAFE_RELEASE(pAudioClient)
	SAFE_RELEASE(pCaptureClient)
}

bool host_sndboard_init_capture(int freq, int owner)
{
	HRESULT hr;
	WAVEFORMATEX wavfmtsrc;
	WAVEFORMATEX *wavfmt2;
	WAVEFORMATEX *wavfmt;
	bool init = false;

	if (capture_started && capture_owner != owner)
		return false;
	if (capture_started)
		host_sndboard_free_capture(owner);
	capture_owner = owner;

	wavfmt2 = NULL;

	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_ERROR(hr)

	hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
	EXIT_ON_ERROR(hr)

	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr)

	memset (&wavfmtsrc, 0, sizeof wavfmtsrc);
	wavfmtsrc.nChannels = 2;
	wavfmtsrc.nSamplesPerSec = freq;
	wavfmtsrc.wBitsPerSample = 16;
	wavfmtsrc.wFormatTag = WAVE_FORMAT_PCM;
	wavfmtsrc.cbSize = 0;
	wavfmtsrc.nBlockAlign = wavfmtsrc.wBitsPerSample / 8 * wavfmtsrc.nChannels;
	wavfmtsrc.nAvgBytesPerSec = wavfmtsrc.nBlockAlign * wavfmtsrc.nSamplesPerSec;

	AUDCLNT_SHAREMODE exc;
	for (int mode = 0; mode < 2; mode++) {
		exc = mode == 0 ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
		int time = mode == 0 ? 0 : REFTIMES_PER_SEC / 50;

		wavfmt = &wavfmtsrc;
		hr = pAudioClient->IsFormatSupported(exc, &wavfmtsrc, &wavfmt2);
		if (SUCCEEDED(hr)) {
			hr = pAudioClient->Initialize(exc, 0, time, 0, wavfmt, NULL);
			if (SUCCEEDED(hr)) {
				init = true;
				break;
			}
		}

		if (hr == S_FALSE && wavfmt2) {
			wavfmt = wavfmt2;
			if (wavfmt->wFormatTag == WAVE_FORMAT_PCM && wavfmt->nChannels == 2 && wavfmt->wBitsPerSample == 16) {
				hr = pAudioClient->Initialize(exc, 0, time, 0, wavfmt, NULL);
				if (SUCCEEDED(hr)) {
					init = true;
					break;
				}
			}
		}
	}

	if (!init) {
		write_log(_T("sndboard capture init, freq=%d, failed\n"), freq);
		goto Exit;
	}


	hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->Start();
	EXIT_ON_ERROR(hr)
	capture_started = true;
	capture_read_count = 0;
	capture_nonzero_seen = false;

	CoTaskMemFree(wavfmt2);

	write_log(_T("sndboard capture started: freq=%d mode=%s\n"), freq, exc == AUDCLNT_SHAREMODE_EXCLUSIVE ? _T("exclusive") : _T("shared"));

	return true;
Exit:;
	CoTaskMemFree(wavfmt2);
	write_log(_T("sndboard capture init failed %08x\n"), hr);
	host_sndboard_free_capture(owner);
	return false;
}
