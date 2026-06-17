#include "sysconfig.h"
#include "sysdeps.h"

#include "sndboard_host.h"
#include "uae.h"

#ifdef UAE_UNIX_WITH_SDL3

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_AudioStream *capture_stream;
static SDL_AudioSpec capture_spec;
static bool audio_initialized;
static uae_u8 capture_buffer[8192];

static bool ensure_audio(void)
{
	if (audio_initialized) {
		return true;
	}
	SDL_SetMainReady();
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		write_log(_T("SDL3: sound-board capture audio unavailable: %s\n"), SDL_GetError());
		return false;
	}
	audio_initialized = true;
	return true;
}

uae_u8 *unix_sndboard_get_buffer(int *frames)
{
	int available;
	int bytes;
	int got;

	if (frames) {
		*frames = 0;
	}
	if (!capture_stream) {
		return NULL;
	}
	available = SDL_GetAudioStreamAvailable(capture_stream);
	bytes = available - (available % 4);
	if (bytes <= 0) {
		return NULL;
	}
	if (bytes > (int)sizeof capture_buffer) {
		bytes = sizeof capture_buffer;
		bytes -= bytes % 4;
	}
	got = SDL_GetAudioStreamData(capture_stream, capture_buffer, bytes);
	if (got <= 0) {
		return NULL;
	}
	got -= got % 4;
	if (got <= 0) {
		return NULL;
	}
	if (frames) {
		*frames = got / 4;
	}
	return capture_buffer;
}

void unix_sndboard_release_buffer(uae_u8 *buffer, int frames)
{
}

void unix_sndboard_free_capture(void)
{
	if (capture_stream) {
		SDL_DestroyAudioStream(capture_stream);
		capture_stream = NULL;
	}
	memset(&capture_spec, 0, sizeof capture_spec);
}

bool unix_sndboard_init_capture(int freq)
{
	unix_sndboard_free_capture();
	if (!ensure_audio()) {
		return false;
	}
	memset(&capture_spec, 0, sizeof capture_spec);
	capture_spec.freq = freq > 0 ? freq : 44100;
	capture_spec.format = SDL_AUDIO_S16;
	capture_spec.channels = 2;
	capture_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &capture_spec, NULL, NULL);
	if (!capture_stream) {
		write_log(_T("SDL3: failed to open default sound-board capture device: %s\n"), SDL_GetError());
		return false;
	}
	SDL_ResumeAudioStreamDevice(capture_stream);
	write_log(_T("SDL3: sound-board capture initialized, freq=%d\n"), capture_spec.freq);
	return true;
}

#else

uae_u8 *unix_sndboard_get_buffer(int *frames)
{
	if (frames) {
		*frames = 0;
	}
	return NULL;
}

void unix_sndboard_release_buffer(uae_u8 *buffer, int frames)
{
}

void unix_sndboard_free_capture(void)
{
}

bool unix_sndboard_init_capture(int freq)
{
	return false;
}

#endif
