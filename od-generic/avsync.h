#ifndef UAE_AVSYNC_H
#define UAE_AVSYNC_H

#include "uae/types.h"

struct avi_audio_clock
{
	uae_u64 remainder = 0;
};

// Number of PCM sample frames for the next video frame. Reset the clock
// when starting a stream or changing its rate. No cumulative float drift.
int avi_audio_samples(avi_audio_clock *clock, int sample_rate, int video_rate);

// Interleaved signed 16-bit PCM. Empty input produces silence. All channels
// use the same interpolation position, and every output sample is written.
bool avi_audio_resample(const uae_s16 *input, int input_frames,
	uae_s16 *output, int output_frames, int channels);

#endif
