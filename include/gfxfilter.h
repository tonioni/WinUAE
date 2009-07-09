
#ifdef GFXFILTER

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

extern void S2X_refresh (void);
extern void S2X_render (void);
extern void S2X_init (int dw, int dh, int aw, int ah, int mult, int ad, int dd);
extern void S2X_free (void);

extern void PAL_init (void);
extern void PAL_1x1_32 (uae_u32 *src, int pitchs, uae_u32 *trg, int pitcht, int width, int height);
extern void PAL_1x1_16 (uae_u16 *src, int pitchs, uae_u16 *trg, int pitcht, int width, int height);

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned long uint32;
#ifndef __cplusplus
typedef int bool;
#endif

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

#define UAE_FILTER_NULL 1
#define UAE_FILTER_DIRECT3D 2
//#define UAE_FILTER_OPENGL 3
#define UAE_FILTER_SCALE2X 3
#define UAE_FILTER_SUPEREAGLE 4
#define UAE_FILTER_SUPER2XSAI 5
#define UAE_FILTER_2XSAI 6
#define UAE_FILTER_PAL 7
#define UAE_FILTER_HQ 8

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
    int x[6];
};

extern struct uae_filter uaefilters[];
extern struct uae_filter *usedfilter;

uae_u8 *getfilterrect1 (RECT *sr, RECT *dr, int dst_depth, int aw, int ah, int scale, int temp_width, int temp_height, uae_u8 *dptr, int pitch);
void getfilterrect2 (RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int temp_width, int temp_height);
void getfilteroffset (int *dx, int *dy, int *mx, int *my);

#endif
