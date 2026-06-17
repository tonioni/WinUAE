#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "audio.h"
#ifdef AVIOUTPUT
#include "avioutput.h"
#endif
#include "custom.h"
#include "events.h"
#include "gui.h"
#include "sounddep/sound.h"
#include "gensound.h"
#include "sound_unix.h"
#ifdef DRIVESOUND
#include "driveclick.h"
#endif

#ifdef UAE_UNIX_WITH_SDL3
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#define UNIX_SOUND_MAX_BUFFER_BYTES (65536 * 2 + 1024)
#define UNIX_SOUND_MIN_FRAMES 128
#define UNIX_SOUND_QUEUE_BUFFERS 4
#define UNIX_SOUND_MAX_DEVICES 100

int active_sound_stereo;
uae_u16 paula_sndbuffer[UNIX_SOUND_MAX_BUFFER_BYTES / sizeof(uae_u16)];
uae_u16 *paula_sndbufpt = paula_sndbuffer;
int paula_sndbufsize = DEFAULT_SOUND_MAXB;

static int have_sound;
static int sound_muted;
static int sound_softvolume = -1;
static float sound_sync_multiplier = 1.0f;
static float scaled_sample_evtime_orig;

extern float sampler_evtime;

#ifdef UAE_UNIX_WITH_SDL3
static SDL_AudioStream *audio_stream;
static SDL_AudioSpec audio_spec;
static bool audio_subsystem_initialized;
#endif

struct unix_sound_device {
#ifdef UAE_UNIX_WITH_SDL3
    SDL_AudioDeviceID id;
#endif
    TCHAR name[256];
    TCHAR config_name[320];
};

static unix_sound_device sound_devices[UNIX_SOUND_MAX_DEVICES];
static int sound_device_count;
static bool sound_devices_enumerated;

static void reset_sound_devices(void)
{
    memset(sound_devices, 0, sizeof sound_devices);
    sound_device_count = 0;
    sound_devices_enumerated = false;
}

#ifdef UAE_UNIX_WITH_SDL3
static void add_sound_device(SDL_AudioDeviceID id, const char *name, const TCHAR *config_prefix)
{
    if (sound_device_count >= UNIX_SOUND_MAX_DEVICES) {
        return;
    }

    unix_sound_device *device = &sound_devices[sound_device_count++];
    device->id = id;
    const char *display_name = name && name[0] ? name : "Default Audio Device";
    _tcsncpy(device->name, display_name, sizeof device->name / sizeof(TCHAR) - 1);
    device->name[sizeof device->name / sizeof(TCHAR) - 1] = 0;
    _sntprintf(device->config_name, sizeof device->config_name / sizeof(TCHAR),
        _T("%s%s"), config_prefix, device->name);
}

static void enumerate_sdl_sound_devices(void)
{
    int count = 0;
    SDL_AudioDeviceID *devices;

    if (sound_devices_enumerated) {
        return;
    }
    reset_sound_devices();
    add_sound_device(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, "Default Audio Device", _T("SDL:"));

    devices = SDL_GetAudioPlaybackDevices(&count);
    if (devices) {
        for (int i = 0; i < count; i++) {
            const char *name = SDL_GetAudioDeviceName(devices[i]);
            add_sound_device(devices[i], name, _T("SDL:"));
        }
        SDL_free(devices);
    }
    sound_devices_enumerated = true;
}
#endif

static void clearbuffer(void)
{
    memset(paula_sndbuffer, 0, sizeof paula_sndbuffer);
    paula_sndbufpt = paula_sndbuffer;
#ifdef UAE_UNIX_WITH_SDL3
    if (audio_stream) {
        SDL_ClearAudioStream(audio_stream);
    }
#endif
}

static void channelswap(uae_s16 *sndbuffer, int len)
{
    for (int i = 0; i < len; i += 2) {
        uae_s16 t = sndbuffer[i];
        sndbuffer[i] = sndbuffer[i + 1];
        sndbuffer[i + 1] = t;
    }
}

static void channelswap6(uae_s16 *sndbuffer, int len)
{
    for (int i = 0; i < len; i += 6) {
        uae_s16 t = sndbuffer[i + 0];
        sndbuffer[i + 0] = sndbuffer[i + 1];
        sndbuffer[i + 1] = t;
        t = sndbuffer[i + 4];
        sndbuffer[i + 4] = sndbuffer[i + 5];
        sndbuffer[i + 5] = t;
    }
}

static int get_sound_channels(void)
{
    int channels = get_audio_nativechannels(active_sound_stereo);
    if (channels < 1) {
        channels = 2;
    }
    return channels;
}

static int get_sound_buffer_frames(int channels)
{
    int frames = currprefs.sound_maxbsiz > 0 ? currprefs.sound_maxbsiz : DEFAULT_SOUND_MAXB;

    frames >>= 2;
    frames &= ~63;
    if (frames < UNIX_SOUND_MIN_FRAMES) {
        frames = UNIX_SOUND_MIN_FRAMES;
    }
    while (frames * channels * (int)sizeof(uae_s16) > UNIX_SOUND_MAX_BUFFER_BYTES) {
        frames >>= 1;
    }
    if (frames < UNIX_SOUND_MIN_FRAMES) {
        frames = UNIX_SOUND_MIN_FRAMES;
    }
    return frames;
}

static void update_softvolume(void)
{
    int volume = currprefs.sound_volume_master;

    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }

    if (sound_muted || volume >= 100) {
        sound_softvolume = 0;
    } else {
        sound_softvolume = (100 - volume) * 32768 / 100;
        if (sound_softvolume >= 32768) {
            sound_softvolume = -1;
        }
    }
}

int setup_sound(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    sound_available = 1;
    return 1;
#else
    sound_available = 0;
    return 0;
#endif
}

void update_sound(float clk)
{
#ifdef UAE_UNIX_WITH_SDL3
    if (!have_sound || audio_spec.freq <= 0) {
        return;
    }
    scaled_sample_evtime_orig = clk * (float)CYCLE_UNIT * sound_sync_multiplier / audio_spec.freq;
    scaled_sample_evtime = scaled_sample_evtime_orig;
    sampler_evtime = clk * CYCLE_UNIT * sound_sync_multiplier;
#endif
}

#ifdef UAE_UNIX_WITH_SDL3
static bool ensure_audio_subsystem(void)
{
    if (audio_subsystem_initialized) {
        return true;
    }

    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        write_log(_T("SDL3: audio unavailable: %s\n"), SDL_GetError());
        return false;
    }
    audio_subsystem_initialized = true;
    return true;
}

static int selected_sound_device(void)
{
    int devices = enumerate_sound_devices();
    if (devices <= 0) {
        return -1;
    }
    if (currprefs.win32_soundcard < 0 || currprefs.win32_soundcard >= devices) {
        currprefs.win32_soundcard = changed_prefs.win32_soundcard = 0;
    }
    return currprefs.win32_soundcard;
}

static bool open_sound_device(void)
{
    SDL_AudioSpec desired;
    int channels = get_sound_channels();
    int freq = currprefs.sound_freq > 0 ? currprefs.sound_freq : DEFAULT_SOUND_FREQ;
    int device_index;
    SDL_AudioDeviceID device_id;

    if (!ensure_audio_subsystem()) {
        return false;
    }
    device_index = selected_sound_device();
    if (device_index < 0) {
        write_log(_T("SDL3: no playback audio devices available\n"));
        return false;
    }
    device_id = sound_devices[device_index].id;

    memset(&desired, 0, sizeof desired);
    desired.freq = freq;
    desired.format = SDL_AUDIO_S16;
    desired.channels = channels;

    audio_stream = SDL_OpenAudioDeviceStream(device_id, &desired, NULL, NULL);
    if (!audio_stream) {
        write_log(_T("SDL3: failed to open audio device '%s': %s\n"),
            sound_devices[device_index].config_name, SDL_GetError());
        return false;
    }
    audio_spec = desired;
    SDL_GetAudioStreamFormat(audio_stream, &audio_spec, NULL);

    if (audio_spec.channels != channels) {
        active_sound_stereo = get_audio_stereomode(audio_spec.channels);
        channels = audio_spec.channels;
    }

    currprefs.sound_freq = changed_prefs.sound_freq = audio_spec.freq;
    paula_sndbufsize = get_sound_buffer_frames(channels) * channels * (int)sizeof(uae_s16);
    if (paula_sndbufsize > UNIX_SOUND_MAX_BUFFER_BYTES) {
        paula_sndbufsize = UNIX_SOUND_MAX_BUFFER_BYTES;
    }
    paula_sndbufpt = paula_sndbuffer;

    if (get_audio_amigachannels(active_sound_stereo) == 4) {
        sample_handler = sample16ss_handler;
    } else {
        sample_handler = get_audio_ismono(active_sound_stereo) ? sample16_handler : sample16s_handler;
    }

    update_softvolume();
    clearbuffer();
    SDL_ResumeAudioStreamDevice(audio_stream);
    gui_data.sndbuf_avail = true;
    write_log(_T("SDL3: audio initialized: %s, %d Hz, %d channels, %d byte buffer\n"),
        sound_devices[device_index].config_name, audio_spec.freq, audio_spec.channels, paula_sndbufsize);
    return true;
}
#endif

int init_sound(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    gui_data.sndbuf = 0;
    gui_data.sndbuf_status = 3;
    gui_data.sndbuf_avail = false;

    if (!sound_available || currprefs.produce_sound <= 1) {
        return 0;
    }
    if (have_sound) {
        return 1;
    }
    if (!open_sound_device()) {
        return 0;
    }

    have_sound = 1;
#ifdef DRIVESOUND
    driveclick_init();
#endif
    return 1;
#else
    return 0;
#endif
}

void close_sound(void)
{
    gui_data.sndbuf = 0;
    gui_data.sndbuf_status = 3;
    gui_data.sndbuf_avail = false;
    if (!have_sound) {
        return;
    }

#ifdef UAE_UNIX_WITH_SDL3
    if (audio_stream) {
        SDL_PauseAudioStreamDevice(audio_stream);
        SDL_ClearAudioStream(audio_stream);
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
    }
#endif
    have_sound = 0;
#ifdef DRIVESOUND
    driveclick_reset();
#endif
    clearbuffer();
}

void finish_sound_buffer(void)
{
    int bufsize = (int)((uae_u8 *)paula_sndbufpt - (uae_u8 *)paula_sndbuffer);

    if (currprefs.turbo_emulation) {
        paula_sndbufpt = paula_sndbuffer;
        return;
    }

    if (bufsize <= 0) {
        paula_sndbufpt = paula_sndbuffer;
        return;
    }

    if (currprefs.sound_stereo_swap_paula) {
        int channels = get_audio_nativechannels(active_sound_stereo);
        if (channels == 2 || channels == 4) {
            channelswap((uae_s16 *)paula_sndbuffer, bufsize / (int)sizeof(uae_s16));
        } else if (channels >= 6) {
            channelswap6((uae_s16 *)paula_sndbuffer, bufsize / (int)sizeof(uae_s16));
        }
    }

    paula_sndbufpt = paula_sndbuffer;

#ifdef AVIOUTPUT
    if (avioutput_audio) {
        if (AVIOutput_WriteAudio((uae_u8 *)paula_sndbuffer, bufsize)) {
            if (avioutput_nosoundsync) {
                sound_setadjust(0);
            }
        }
    }
    if (avioutput_enabled && (!avioutput_framelimiter || avioutput_nosoundoutput)) {
        return;
    }
#endif

#ifdef UAE_UNIX_WITH_SDL3
    if (!have_sound || !audio_stream) {
        return;
    }

    if (sound_softvolume >= 0) {
        uae_s16 *p = (uae_s16 *)paula_sndbuffer;
        for (int i = 0; i < bufsize / (int)sizeof(uae_s16); i++) {
            p[i] = (uae_s16)(p[i] * sound_softvolume / 32768);
        }
    }

    if (!SDL_PutAudioStreamData(audio_stream, paula_sndbuffer, bufsize)) {
        write_log(_T("SDL3: SDL_PutAudioStreamData failed: %s\n"), SDL_GetError());
        gui_data.sndbuf_status = -1;
        return;
    }

    int queued = SDL_GetAudioStreamQueued(audio_stream);
    if (queued < 0) {
        gui_data.sndbuf_status = -1;
        return;
    }
    int target = paula_sndbufsize * UNIX_SOUND_QUEUE_BUFFERS;
    if (queued > target * 3) {
        SDL_ClearAudioStream(audio_stream);
        gui_data.sndbuf_status = 2;
    } else {
        gui_data.sndbuf_status = 0;
        gui_data.sndbuf = target ? (int)(queued * 1000 / target) : 0;
    }
#endif
}

void restart_sound_buffer(void)
{
    clearbuffer();
}

void pause_sound_buffer(void)
{
    reset_sound();
}

void resume_sound(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    if (have_sound && audio_stream) {
        SDL_ResumeAudioStreamDevice(audio_stream);
    }
#endif
}

void pause_sound(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    if (have_sound && audio_stream) {
        SDL_PauseAudioStreamDevice(audio_stream);
    }
#endif
}

void reset_sound(void)
{
    clearbuffer();
}

bool sound_paused(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    return !have_sound || !audio_stream || SDL_AudioStreamDevicePaused(audio_stream);
#else
    return true;
#endif
}

void sound_setadjust(float v)
{
    if (v < -6.0f) {
        v = -6.0f;
    } else if (v > 6.0f) {
        v = 6.0f;
    }
    if (scaled_sample_evtime_orig > 0) {
        scaled_sample_evtime = scaled_sample_evtime_orig * (1000.0f + v) / 1000.0f;
    }
}

int enumerate_sound_devices(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    if (!ensure_audio_subsystem()) {
        return 0;
    }
    enumerate_sdl_sound_devices();
    return sound_device_count;
#else
    return 0;
#endif
}

int unix_sound_device_count(void)
{
    return enumerate_sound_devices();
}

const TCHAR *unix_sound_device_name(int index)
{
    if (index < 0 || index >= enumerate_sound_devices()) {
        return _T("");
    }
    return sound_devices[index].name;
}

const TCHAR *unix_sound_device_config_name(int index)
{
    if (index < 0 || index >= enumerate_sound_devices()) {
        return _T("");
    }
    return sound_devices[index].config_name;
}

int unix_sound_device_index_from_config_name(const TCHAR *name)
{
    TCHAR prefixed[320];

    if (!name || !name[0]) {
        return -1;
    }
    enumerate_sound_devices();
    for (int i = 0; i < sound_device_count; i++) {
        if (!_tcsicmp(sound_devices[i].config_name, name) || !_tcsicmp(sound_devices[i].name, name)) {
            return i;
        }
    }
    if (_tcsncmp(name, _T("SDL:"), 4) != 0) {
        _sntprintf(prefixed, sizeof prefixed / sizeof(TCHAR), _T("SDL:%s"), name);
        for (int i = 0; i < sound_device_count; i++) {
            if (!_tcsicmp(sound_devices[i].config_name, prefixed)) {
                return i;
            }
        }
    }
    return -1;
}

void sound_mute(int newmute)
{
    if (newmute < 0) {
        sound_muted = !sound_muted;
    } else {
        sound_muted = newmute != 0;
    }
    update_softvolume();
}

void sound_volume(int dir)
{
    currprefs.sound_volume_master -= dir * 10;
    if (currprefs.sound_volume_master < 0) {
        currprefs.sound_volume_master = 0;
    } else if (currprefs.sound_volume_master > 100) {
        currprefs.sound_volume_master = 100;
    }
    changed_prefs.sound_volume_master = currprefs.sound_volume_master;
    update_softvolume();
    config_changed = 1;
}

void set_volume(int volume, int mute)
{
    currprefs.sound_volume_master = volume;
    changed_prefs.sound_volume_master = volume;
    sound_muted = mute != 0;
    update_softvolume();
}

void master_sound_volume(int dir)
{
    if (dir == 0) {
        sound_mute(-1);
    } else {
        sound_volume(dir);
    }
}
