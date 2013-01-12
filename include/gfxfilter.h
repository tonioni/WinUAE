
#ifdef GFXFILTER

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

extern void S2X_refresh (void);
extern void S2X_render (void);
extern bool S2X_init (int dw, int dh, int dd);
extern void S2X_reset (void);
extern void S2X_free (void);
extern int S2X_getmult (void);

extern void PAL_init (void);
extern void PAL_1x1_32 (uae_u32 *src, int pitchs, uae_u32 *trg, int pitcht, int width, int height);
extern void PAL_1x1_16 (uae_u16 *src, int pitchs, uae_u16 *trg, int pitcht, int width, int height);

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned long uint32;
#ifndef __cplusplus
typedef int bool;
#endif

extern "C"
{
	extern void S2X_configure (int rb, int gb, int bb, int rs, int gs, int bs);
	extern int Init_2xSaI (int rb, int gb, int bb, int rs, int gs, int bs);
	extern void Super2xSaI_16 (const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
	extern void Super2xSaI_32 (const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
	extern void SuperEagle_16 (const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
	extern void SuperEagle_32 (const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
	extern void _2xSaI_16 (const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
	extern void _2xSaI_32 (const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
	extern void AdMame2x (u8 *srcPtr, u32 srcPitch, /* u8 deltaPtr, */
		      u8 *dstPtr, u32 dstPitch, int width, int height);
	extern void AdMame2x32 (u8 *srcPtr, u32 srcPitch, /* u8 deltaPtr, */
		      u8 *dstPtr, u32 dstPitch, int width, int height);

	extern void hq_init (int rb, int gb, int bb, int rs, int gs, int bs);

	extern void _cdecl hq2x_16 (unsigned char*, unsigned char*, DWORD, DWORD, DWORD);
	extern void _cdecl hq2x_32 (unsigned char*, unsigned char*, DWORD, DWORD, DWORD);
	extern void _cdecl hq3x_16 (unsigned char*, unsigned char*, DWORD, DWORD, DWORD);
	extern void _cdecl hq3x_32 (unsigned char*, unsigned char*, DWORD, DWORD, DWORD);
	extern void _cdecl hq4x_16 (unsigned char*, unsigned char*, DWORD, DWORD, DWORD);
	extern void _cdecl hq4x_32 (unsigned char*, unsigned char*, DWORD, DWORD, DWORD);
}

#define UAE_FILTER_NULL 1
#define UAE_FILTER_SCALE2X 2
#define UAE_FILTER_HQ2X 3
#define UAE_FILTER_HQ3X 4
#define UAE_FILTER_HQ4X 5
#define UAE_FILTER_SUPEREAGLE 6
#define UAE_FILTER_SUPER2XSAI 7
#define UAE_FILTER_2XSAI 8
#define UAE_FILTER_PAL 9
#define UAE_FILTER_LAST 9

#define UAE_FILTER_MODE_16 16
#define UAE_FILTER_MODE_16_16 16
#define UAE_FILTER_MODE_16_32 (16 | 8)
#define UAE_FILTER_MODE_32 32
#define UAE_FILTER_MODE_32_32 32
#define UAE_FILTER_MODE_32_16 (32 | 8)


struct uae_filter
{
    int type, yuv, intmul;
    TCHAR *name, *cfgname;
    int flags;
};

extern struct uae_filter uaefilters[];
extern struct uae_filter *usedfilter;

void getfilterrect2 (RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int temp_width, int temp_height);
void getfilteroffset (float *dx, float *dy, float *mx, float *my);
uae_u8 *getfilterbuffer (int *widthp, int *heightp, int *pitch, int *depth);
uae_u8 *getfilterbuffer3d (int *widthp, int *heightp, int *pitch, int *depth);
uae_u8 *getrtgbuffer (int *widthp, int *heightp, int *pitch, int *depth, uae_u8 *palette);
void freertgbuffer (uae_u8 *dst);

extern void getrtgfilterrect2 (RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height);
#endif
