#ifndef UAE_SOUND_COMMON_H
#define UAE_SOUND_COMMON_H
#include "uae/types.h"
extern float scaled_sample_evtime_orig;
void host_sound_update_clock(float clock, int rate, float multiplier);
int host_sound_prepare(uae_s16 *buffer, int bytes, int channels, bool swap, bool turbo);
int host_sound_gain(int volume, bool muted);
void host_sound_apply_gain(uae_s16 *buffer, int samples, int gain);
int host_sound_volume_step(int volume, int direction);
bool host_sound_correct(float *sum, float *count, unsigned long *previous_frame,
			unsigned long frame, float error, int granularity, float *adjustment);
float host_sound_sync(float error, bool vsync);
#endif
