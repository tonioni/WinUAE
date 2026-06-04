#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WINUAE_UNIX_WITH_SAMPLER

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "options.h"
#include "events.h"
#include "sampler.h"
#include "sound_unix.h"
#include "uae.h"

#define UNIX_SAMPLER_MAX_DEVICES 100
#define UNIX_SAMPLER_FRAME_BYTES 4

float sampler_evtime;

struct unix_sampler_device {
	SDL_AudioDeviceID id;
	TCHAR name[256];
	TCHAR config_name[320];
};

static unix_sampler_device sampler_devices[UNIX_SAMPLER_MAX_DEVICES];
static int sampler_device_count;
static bool sampler_devices_enumerated;
static bool sampler_audio_initialized;
static SDL_AudioStream *sampler_stream;
static SDL_AudioSpec sampler_spec;
static int sampler_inited;
static uae_s16 sampler_last[2];

static bool ensure_sampler_audio(void)
{
	if (sampler_audio_initialized) {
		return true;
	}
	SDL_SetMainReady();
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		write_log(_T("SDL3: sampler audio unavailable: %s\n"), SDL_GetError());
		return false;
	}
	sampler_audio_initialized = true;
	return true;
}

static void reset_sampler_devices(void)
{
	memset(sampler_devices, 0, sizeof sampler_devices);
	sampler_device_count = 0;
	sampler_devices_enumerated = false;
}

static void add_sampler_device(SDL_AudioDeviceID id, const char *name)
{
	if (sampler_device_count >= UNIX_SAMPLER_MAX_DEVICES) {
		return;
	}
	unix_sampler_device *device = &sampler_devices[sampler_device_count++];
	device->id = id;
	const char *display_name = name && name[0] ? name : "Default Recording Device";
	_tcsncpy(device->name, display_name, sizeof device->name / sizeof(TCHAR) - 1);
	device->name[sizeof device->name / sizeof(TCHAR) - 1] = 0;
	_sntprintf(device->config_name, sizeof device->config_name / sizeof(TCHAR),
		_T("SDL:%s"), device->name);
}

static void enumerate_sdl_sampler_devices(void)
{
	int count = 0;
	SDL_AudioDeviceID *devices;

	if (sampler_devices_enumerated) {
		return;
	}
	reset_sampler_devices();
	add_sampler_device(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, "Default Recording Device");
	devices = SDL_GetAudioRecordingDevices(&count);
	if (devices) {
		for (int i = 0; i < count; i++) {
			const char *name = SDL_GetAudioDeviceName(devices[i]);
			add_sampler_device(devices[i], name);
		}
		SDL_free(devices);
	}
	sampler_devices_enumerated = true;
}

static int enumerate_sampler_devices(void)
{
	if (!ensure_sampler_audio()) {
		return 0;
	}
	enumerate_sdl_sampler_devices();
	return sampler_device_count;
}

int unix_sampler_device_count(void)
{
	return enumerate_sampler_devices();
}

const TCHAR *unix_sampler_device_name(int index)
{
	if (index < 0 || index >= enumerate_sampler_devices()) {
		return _T("");
	}
	return sampler_devices[index].name;
}

const TCHAR *unix_sampler_device_config_name(int index)
{
	if (index < 0 || index >= enumerate_sampler_devices()) {
		return _T("");
	}
	return sampler_devices[index].config_name;
}

int unix_sampler_device_index_from_config_name(const TCHAR *name)
{
	TCHAR prefixed[320];

	if (!name || !name[0]) {
		return -1;
	}
	enumerate_sampler_devices();
	for (int i = 0; i < sampler_device_count; i++) {
		if (!_tcsicmp(sampler_devices[i].config_name, name) || !_tcsicmp(sampler_devices[i].name, name)) {
			return i;
		}
	}
	if (_tcsncmp(name, _T("SDL:"), 4) != 0) {
		_sntprintf(prefixed, sizeof prefixed / sizeof(TCHAR), _T("SDL:%s"), name);
		for (int i = 0; i < sampler_device_count; i++) {
			if (!_tcsicmp(sampler_devices[i].config_name, prefixed)) {
				return i;
			}
		}
	}
	return -1;
}

static bool open_sampler_stream(void)
{
	int device_index;
	SDL_AudioDeviceID device_id;

	if (!ensure_sampler_audio()) {
		return false;
	}
	if (enumerate_sampler_devices() <= 0) {
		write_log(_T("SDL3: no recording audio devices available\n"));
		return false;
	}
	device_index = currprefs.win32_samplersoundcard;
	if (device_index < 0 || device_index >= sampler_device_count) {
		write_log(_T("SDL3: sampler input device is not selected\n"));
		return false;
	}
	device_id = sampler_devices[device_index].id;

	memset(&sampler_spec, 0, sizeof sampler_spec);
	sampler_spec.freq = currprefs.sampler_freq > 0 ? currprefs.sampler_freq : 44100;
	sampler_spec.format = SDL_AUDIO_S16;
	sampler_spec.channels = 2;
	sampler_stream = SDL_OpenAudioDeviceStream(device_id, &sampler_spec, NULL, NULL);
	if (!sampler_stream) {
		write_log(_T("SDL3: failed to open sampler device '%s': %s\n"),
			sampler_devices[device_index].config_name, SDL_GetError());
		return false;
	}
	SDL_ResumeAudioStreamDevice(sampler_stream);
	write_log(_T("SDL3: sampler input initialized, '%s'\n"), sampler_devices[device_index].config_name);
	return true;
}

int sampler_init(void)
{
	return currprefs.win32_samplersoundcard >= 0 && enumerate_sampler_devices() > 0;
}

void sampler_free(void)
{
	if (sampler_stream) {
		SDL_DestroyAudioStream(sampler_stream);
		sampler_stream = NULL;
	}
	sampler_inited = 0;
	sampler_last[0] = 0;
	sampler_last[1] = 0;
}

static void update_sampler_frame(void)
{
	uae_s16 frame[2];
	int available;

	if (!sampler_inited) {
		if (!open_sampler_stream()) {
			sampler_free();
			return;
		}
		sampler_inited = 1;
	}
	if (!sampler_stream) {
		return;
	}
	available = SDL_GetAudioStreamAvailable(sampler_stream);
	while (available >= UNIX_SAMPLER_FRAME_BYTES) {
		if (SDL_GetAudioStreamData(sampler_stream, frame, sizeof frame) != sizeof frame) {
			break;
		}
		sampler_last[0] = frame[0];
		sampler_last[1] = frame[1];
		available -= UNIX_SAMPLER_FRAME_BYTES;
	}
}

uae_u8 sampler_getsample(int channel)
{
	int sample;

	if (!currprefs.sampler_stereo) {
		channel = 0;
	}
	channel = channel ? 1 : 0;
	update_sampler_frame();
	sample = sampler_last[channel] / 128;
	if (sample < -128) {
		sample = 0;
	} else if (sample > 127) {
		sample = 127;
	}
	return (uae_u8)(sample - 128);
}

void sampler_vsync(void)
{
}

#endif
