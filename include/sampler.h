#ifndef UAE_SAMPLER_H
#define UAE_SAMPLER_H

#include "uae/types.h"

extern float sampler_evtime;
// Host sampling produces a signed 16-bit level for channel zero or one.
// False preserves the sampler's failure response rather than an idle sample.
bool host_sampler_getsample(int channel, int *value);

extern uae_u8 sampler_getsample (int);
extern int sampler_init (void);
extern void sampler_free (void);
extern void sampler_vsync (void);

#endif /* UAE_SAMPLER_H */
