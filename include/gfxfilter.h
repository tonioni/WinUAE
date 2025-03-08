#ifndef UAE_GFXFILTER_H
#define UAE_GFXFILTER_H

#include "uae/types.h"

#ifdef GFXFILTER

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

void getfilterrect2(int monid, RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int *mode, int temp_width, int temp_height);
void getfilteroffset(int monid, float *dx, float *dy, float *mx, float *my);

uae_u8 *getfilterbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth);
void freefilterbuffer(int monid, uae_u8*);

uae_u8 *uaegfx_getrtgbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth, uae_u8 *palette);
void uaegfx_freertgbuffer(int monid, uae_u8 *dst);

extern void getrtgfilterrect2(int monid, RECT *sr, RECT *dr, RECT *zr, int *mode, int dst_width, int dst_height);
extern float filterrectmult(int v1, float v2, int dmode);

#endif /* GFXFILTER */

#endif /* UAE_GFXFILTER_H */
