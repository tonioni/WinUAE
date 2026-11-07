#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "audio.h"
#include "gensound.h"
#include "custom.h"
#include "xwin.h"
#include "events.h"
#include "sound_common.h"
#ifdef AVIOUTPUT
#include "avioutput.h"
#endif
#include <cmath>

float scaled_sample_evtime_orig;
extern float sampler_evtime;
extern frame_time_t vsynctimebase_orig;

void host_sound_update_clock(float clock, int rate, float multiplier)
{
    if (rate <= 0)
	return;
    scaled_sample_evtime_orig = clock * (float)CYCLE_UNIT * multiplier / rate;
    scaled_sample_evtime = scaled_sample_evtime_orig;
    sampler_evtime = clock * CYCLE_UNIT * multiplier;
}

void sound_setadjust(float value)
{
    if (value < -6)
	value = -6;
    if (value > 6)
	value = 6;
    float multiplier = 1000.0f + value;
    bool recording = false;
#ifdef AVIOUTPUT
    recording = avioutput_audio && avioutput_enabled;
    if (recording && avioutput_nosoundsync)
	multiplier = 1000.0f;
#endif
    if (isvsync_chipset() || (recording && !currprefs.cachesize)) {
	vsynctimebase = vsynctimebase_orig;
	scaled_sample_evtime = scaled_sample_evtime_orig * multiplier / 1000.0f;
    } else {
	vsynctimebase = (int)(vsynctimebase_orig * multiplier / 1000.0f);
	scaled_sample_evtime = scaled_sample_evtime_orig;
    }
}

int host_sound_prepare(uae_s16 *buffer, int bytes, int channels, bool swap, bool turbo)
{
    if (turbo || !buffer || bytes <= 0 || channels <= 0 || channels > 8 ||
	bytes % (channels * (int)sizeof(uae_s16)))
	return 0;
    if (!swap || channels == 1)
	return bytes;
    int samples = bytes / (int)sizeof(uae_s16);
    for (int i = 0; i < samples; i += channels) {
	for (int pair = 0; pair + 1 < channels; pair += 2) {
	    // Center and LFE are not a left/right pair in surround layouts.
	    if (channels >= 6 && pair == 2)
		continue;
	    uae_s16 tmp = buffer[i + pair];
	    buffer[i + pair] = buffer[i + pair + 1];
	    buffer[i + pair + 1] = tmp;
	}
    }
    return bytes;
}

int host_sound_gain(int volume, bool muted)
{
    if (muted || volume >= 100)
	return 0;
    if (volume <= 0)
	return -1;
    return (100 - volume) * 32768 / 100;
}

void host_sound_apply_gain(uae_s16 *buffer, int samples, int gain)
{
    if (!buffer || samples <= 0 || gain < 0)
	return;
    if (gain > 32768)
	gain = 32768;
    for (int i = 0; i < samples; i++)
	buffer[i] = (uae_s16)(buffer[i] * gain / 32768);
}

int host_sound_volume_step(int volume, int direction)
{
    uae_s64 next = (uae_s64)volume - (uae_s64)direction * 10;
    return next < 0 ? 0 : next > 100 ? 100 : (int)next;
}

bool host_sound_correct(float *sum, float *count, unsigned long *previous_frame,
			unsigned long frame, float error, int granularity, float *adjustment)
{
    *sum += error;
    (*count)++;
    if (*previous_frame == frame)
	return false;
    if (granularity < 10)
	granularity = 10;
    float immediate = error / 100.0f;
    float average = (*sum / *count) / (10000.0f / granularity);
    if (immediate > 1)
	immediate = 1;
    if (immediate < -1)
	immediate = -1;
    *adjustment = immediate + average;
    *previous_frame = frame;
    return true;
}

float host_sound_sync(float error, bool vsync)
{
    float value = (float)pow(error < 0 ? -error : error, vsync ? 1.6 : 1.9) / 2.0f;
    if (error < 0)
	value = -value;
    float limit = vsync ? 12.0f : 20.0f;
    return value < -limit ? -limit : value > limit ? limit : value;
}
