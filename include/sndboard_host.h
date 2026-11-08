#ifndef UAE_SNDBOARD_HOST_H
#define UAE_SNDBOARD_HOST_H

#include "uae/types.h"

#define SNDBOARD_CAPTURE_OWNER_NONE 0
#define SNDBOARD_CAPTURE_OWNER_TOCCATA 1
#define SNDBOARD_CAPTURE_OWNER_UAESND 2

// Capture returns interleaved native-endian signed 16-bit stereo frames.
// Release each returned buffer before requesting another. Initialization
// reserves the input for one board; another owner cannot stop its capture.
uae_u8 *host_sndboard_get_buffer(int *frames);
void host_sndboard_release_buffer(uae_u8 *buffer, int frames);
void host_sndboard_free_capture(int owner);
bool host_sndboard_init_capture(int freq, int owner);

#endif /* UAE_SNDBOARD_HOST_H */
