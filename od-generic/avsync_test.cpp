#include "sysconfig.h"
#include "sysdeps.h"
#include "avsync.h"
#include <vector>
#include <math.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main()
{
	for (int rate : {11025, 44100, 48000, 96000}) {
		for (int fps : {50, 59, 60, 72}) {
			avi_audio_clock clock;
			uae_u64 total = 0;
			for (int frame = 1; frame <= fps * 36000; frame++) {
				total += avi_audio_samples(&clock, rate, fps);
				CHECK(total == (uae_u64)frame * rate / fps);
			}
			CHECK(total == (uae_u64)rate * 36000);
		}
	}
	for (int channels : {1, 2, 4, 6, 8}) {
		for (int input_frames : {0, 1, 2, 735, 882, 4096, 9000}) {
			std::vector<uae_s16> input((size_t)input_frames * channels);
			for (int i = 0; i < input_frames; i++)
				for (int c = 0; c < channels; c++)
					input[(size_t)i * channels + c] = (uae_s16)(sin(i * .031 + c) * 30000);
			for (int output_frames : {1, 2, 736, 883, 10000}) {
				std::vector<uae_s16> output((size_t)output_frames * channels + 2, 12345);
				CHECK(avi_audio_resample(input.data(), input_frames, output.data() + 1,
					output_frames, channels));
				CHECK(output.front() == 12345 && output.back() == 12345);
				for (int i = 0; i < output_frames; i++) {
					double position = (double)i * input_frames / output_frames;
					int lo = (int)position;
					int hi = lo + 1 < input_frames ? lo + 1 : lo;
					for (int c = 0; c < channels; c++) {
						double want = 0;
						if (input_frames)
							want = input[(size_t)lo * channels + c] * (1 - (position - lo)) +
								input[(size_t)hi * channels + c] * (position - lo);
						CHECK(fabs(output[1 + (size_t)i * channels + c] - want) <= .50001);
					}
				}
			}
		}
	}
	uae_s16 extremes[] = {-32768, 32767};
	std::vector<uae_s16> output(20000);
	CHECK(avi_audio_resample(extremes, 2, output.data(), 20000, 1));
	CHECK(output[0] == -32768 && output.back() == 32767);
	CHECK(!avi_audio_resample(NULL, 1, output.data(), 1, 1));
	CHECK(!avi_audio_resample(extremes, 2, output.data(), 1, 0));
	CHECK(!avi_audio_resample(extremes, -1, output.data(), 1, 1));
	CHECK(avi_audio_samples(NULL, 44100, 50) == -1);
	return 0;
}
