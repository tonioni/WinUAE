#ifndef UAE_UNIX_SNDBOARD_HOST_H
#define UAE_UNIX_SNDBOARD_HOST_H

uae_u8 *unix_sndboard_get_buffer(int *frames);
void unix_sndboard_release_buffer(uae_u8 *buffer, int frames);
void unix_sndboard_free_capture(void);
bool unix_sndboard_init_capture(int freq);

#endif /* UAE_UNIX_SNDBOARD_HOST_H */
