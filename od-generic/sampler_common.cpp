// Parallel-port sampler channel selection and guest sample conversion.
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "sampler.h"

float sampler_evtime;

uae_u8 sampler_getsample(int channel)
{
	channel = currprefs.sampler_stereo && channel ? 1 : 0;
	int sample;
	if (!host_sampler_getsample(channel, &sample)) {
		return 0;
	}
	// Keep the established gain, including its negative-overload response.
	sample /= 128;
	if (sample < -128) {
		sample = 0;
	} else if (sample > 127) {
		sample = 127;
	}
	return (uae_u8)(sample - 128);
}
