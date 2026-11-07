#include "sysconfig.h"
#include "sysdeps.h"
#include "avsync.h"

int avi_audio_samples(avi_audio_clock *clock, int sample_rate, int video_rate)
{
	if (!clock || sample_rate <= 0 || video_rate <= 0)
		return -1;
	uae_u64 total = clock->remainder + (uae_u64)sample_rate;
	int frames = (int)(total / video_rate);
	clock->remainder = total % video_rate;
	return frames;
}

bool avi_audio_resample(const uae_s16 *input, int input_frames,
	uae_s16 *output, int output_frames, int channels)
{
	if (input_frames < 0 || output_frames < 0 || channels <= 0 ||
		(input_frames && !input) || (output_frames && !output) ||
		(size_t)input_frames > SIZE_MAX / sizeof(uae_s16) / (size_t)channels ||
		(size_t)output_frames > SIZE_MAX / sizeof(uae_s16) / (size_t)channels)
		return false;
	if (!output_frames)
		return true;
	if (!input_frames) {
		memset(output, 0, (size_t)output_frames * channels * sizeof(uae_s16));
		return true;
	}
	// Linear interpolation, as in the original Windows AVI resampler.
	// Integer positions avoid a rate-dependent scratch-buffer limit.
	for (int i = 0; i < output_frames; i++) {
		uae_u64 position = (uae_u64)i * input_frames;
		int index = (int)(position / output_frames);
		uae_s64 fraction = position % output_frames;
		int next = index + 1 < input_frames ? index + 1 : index;
		for (int ch = 0; ch < channels; ch++) {
			uae_s64 value = (uae_s64)input[(size_t)index * channels + ch] *
				(output_frames - fraction) +
				(uae_s64)input[(size_t)next * channels + ch] * fraction;
			// Round to nearest, with the original positive half-way bias.
			value += output_frames / 2;
			value = value < 0 ? -((-value + output_frames - 1) / output_frames) :
				value / output_frames;
			output[(size_t)i * channels + ch] = (uae_s16)value;
		}
	}
	return true;
}
