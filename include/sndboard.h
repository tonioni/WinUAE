#ifndef UAE_SNDBOARD_H
#define UAE_SNDBOARD_H

#include "uae/types.h"

addrbank *sndboard_init(int devnum);
void sndboard_free(void);
void sndboard_hsync(void);
void sndboard_vsync(void);
void sndboard_rethink(void);
void update_sndboard_sound(double);
void sndboard_reset(void);
void sndboard_ext_volume(void);

#endif /* UAE_SNDBOARD_H */
