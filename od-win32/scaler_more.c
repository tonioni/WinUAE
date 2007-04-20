
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "filter.h"
#include "custom.h"

#include <math.h>

static uae_s32 line_yuv_0[4096 * 3];
static uae_s32 line_yuv_1[4096 * 3];
static uae_u32 randomtable[4096];
static uae_u32 redc[3 * 256], grec[3 * 256], bluc[3 * 256];
static uae_u32 *predc, *pgrec, *pbluc;
static int randomoffset, randomshift;
static int xx1, xx2, xx3, pal_noise_mask, scanlinelevel;

uae_s32 tyhrgb[262144];
uae_s32 tylrgb[262144];
uae_s32 tcbrgb[262144];
uae_s32 tcrrgb[262144];

void PAL_init(void)
{
    int i;
    for (i = 0; i < 4096; i++)
	randomtable[i] = rand() | (rand() << 15);
    for (i = 0; i < 256; i++) {
	redc[0 * 256 + i] = xredcolors[0];
	grec[0 * 256 + i] = xgreencolors[0];
	bluc[0 * 256 + i] = xbluecolors[0];
	redc[1 * 256 + i] = xredcolors[i];
	grec[1 * 256 + i] = xgreencolors[i];
	bluc[1 * 256 + i] = xbluecolors[i];
	redc[2 * 256 + i] = xredcolors[255];
	grec[2 * 256 + i] = xgreencolors[255];
	bluc[2 * 256 + i] = xbluecolors[255];
    }
    predc = &redc[1 * 256];
    pgrec = &grec[1 * 256];
    pbluc = &bluc[1 * 256];

    if (currprefs.gfx_lores) {
	xx1 = 1;
	xx2 = 2;
	xx3 = 3;
    } else {
	xx1 = 2;
	xx2 = 5;
        xx3 = 7;
    }
    pal_noise_mask = (1 << (currprefs.gfx_filter_noise * 7 / 100)) - 1;
    scanlinelevel = 128 - currprefs.gfx_filter_scanlines * 128 / 100;
}

#define mm 256 * 256
#define sat 256
STATIC_INLINE int zyh(uae_u32 v)
{
    return ((v >> 24) & 0xff) * mm;
}
STATIC_INLINE int zyl(uae_u32 v)
{
    return ((v >> 16) & 0xff) * mm;
}
STATIC_INLINE int zcb(uae_u32 v)
{
    return ((int)((uae_s8)(v >> 8))) * sat;
}
STATIC_INLINE int zcr(uae_u32 v)
{
    return ((int)((uae_s8)(v >> 0))) * sat;
}

STATIC_INLINE uae_u32 STRIPRGB(uae_u32 v)
{
    return ((v & 0x0000fc) >> 2) | ((v & 0x00fc00) >> 4) | ((v & 0xfc0000) >> 6);
}
STATIC_INLINE int zyhRGB(uae_u32 v)
{
    return tyhrgb[STRIPRGB(v)];
}
STATIC_INLINE int zylRGB(uae_u32 v)
{
    return tylrgb[STRIPRGB(v)];
}
STATIC_INLINE int zcbRGB(uae_u32 v)
{
    return tcbrgb[STRIPRGB(v)];
}
STATIC_INLINE int zcrRGB(uae_u32 v)
{
    return tcrrgb[STRIPRGB(v)];
}

void PAL_1x1_32(uae_u32 *src, int pitchs, uae_u32 *trg, int pitcht, int width, int height)
{
    const uae_u32 *tmpsrc;
    uae_u32 *tmptrg;
    uae_s32 *lineptr0;
    uae_s32 *lineptr1;
    uae_s32 *line;
    uae_s32 *linepre;
    unsigned int x, y, wstart, wfast, wend, wint;
    int l, u, v;
    int red, grn, blu;
    int xt = 0;
    int yt = 0;
    int xs = 0;
    int ys = 0;

    pitchs /= sizeof (*trg);
    pitcht /= sizeof (*trg);

    src = src + pitchs * ys + xs - 2;
    trg = trg + pitcht * yt + xt;
    if (width < 8) {
        wstart = width;
        wfast = 0;
        wend = 0;
    } else {
        /* alignment: 8 pixels*/
        wstart = (unsigned int)(8 - ((unsigned long)trg & 7));
        wfast = (width - wstart) >> 3; /* fast loop for 8 pixel segments*/
        wend = (width - wstart) & 0x07; /* do not forget the rest*/
    }
    wint = width + 5;
    lineptr0 = line_yuv_0;
    lineptr1 = line_yuv_1;

    tmpsrc = src - pitchs;
    line = lineptr0;
    for (x = 0; x < wint; x++) {
        uae_u32 cl0, cl1, cl2, cl3;

        cl0 = tmpsrc[0];
        cl1 = tmpsrc[xx1];
        cl2 = tmpsrc[xx2];
        cl3 = tmpsrc[xx3];
        line[0] = 0;
        line[1] = zcb(cl0) + zcb(cl1) + zcb(cl2) + zcb(cl3);
        line[2] = zcr(cl0) + zcr(cl1) + zcr(cl2) + zcr(cl3);
        tmpsrc++;
        line += 3;
    }

    for (y = 0; y < height; y++) {
	int scn = (y & 1) ? scanlinelevel : 128;
	randomshift = rand() & 15;
	randomoffset = rand() & 2047;

        tmpsrc = src;
        tmptrg = trg;

        line = lineptr0;
        lineptr0 = lineptr1;
        lineptr1 = line;

        tmpsrc = src;
        line = lineptr0;
        for (x = 0; x < wint; x++) {
	    int r = (randomtable[randomoffset++] >> randomshift) & pal_noise_mask;
            uae_u32 cl0, cl1, cl2, cl3;
            cl0 = tmpsrc[0];
            cl1 = tmpsrc[xx1];
            cl2 = tmpsrc[xx2];
            cl3 = tmpsrc[xx3];
	    line[0] = (zyl(cl1) + zyh(cl2) * (scn - r) / 128 + zyl(cl3)) / 256; /* 1/4 + 1/2 + 1/4 */
            line[1] = zcb(cl0) + zcb(cl1) + zcb(cl2) + zcb(cl3);
            line[2] = zcr(cl0) + zcr(cl1) + zcr(cl2) + zcr(cl3);
            tmpsrc++;
            line += 3;
        }

        line = lineptr0;
        linepre = lineptr1;
        for (x = 0; x < (wfast << 3) + wend + wstart; x++) {

            l = line[0];
            u = (line[1] + linepre[1]) / 8;
            v = (line[2] + linepre[2]) / 8;
            line += 3;
            linepre += 3;

            red = (v + l) / 256;
            blu = (u + l) / 256;
            grn = (l * 256 - 50 * u - 130 * v) / (256 * 256);
            *tmptrg++ = predc[red] | pgrec[grn] | pbluc[blu];
        }

        src += pitchs;
        trg += pitcht;
    }
}


void PAL_1x1_AGA_32(uae_u32 *src, int pitchs, uae_u32 *trg, int pitcht, int width, int height)
{
    const uae_u32 *tmpsrc;
    uae_u32 *tmptrg;
    uae_s32 *lineptr0;
    uae_s32 *lineptr1;
    uae_s32 *line;
    uae_s32 *linepre;
    unsigned int x, y, wstart, wfast, wend, wint;
    int l, u, v;
    int red, grn, blu;
    int xt = 0;
    int yt = 0;
    int xs = 0;
    int ys = 0;

    pitchs /= sizeof (*trg);
    pitcht /= sizeof (*trg);

    src = src + pitchs * ys + xs - 2;
    trg = trg + pitcht * yt + xt;
    if (width < 8) {
        wstart = width;
        wfast = 0;
        wend = 0;
    } else {
        /* alignment: 8 pixels*/
        wstart = (unsigned int)(8 - ((unsigned long)trg & 7));
        wfast = (width - wstart) >> 3; /* fast loop for 8 pixel segments*/
        wend = (width - wstart) & 0x07; /* do not forget the rest*/
    }
    wint = width + 5;
    lineptr0 = line_yuv_0;
    lineptr1 = line_yuv_1;

    tmpsrc = src - pitchs;
    line = lineptr0;
    for (x = 0; x < wint; x++) {
        uae_u32 cl0, cl1, cl2, cl3;

        cl0 = tmpsrc[0];
        cl1 = tmpsrc[xx1];
        cl2 = tmpsrc[xx2];
        cl3 = tmpsrc[xx3];
        line[0] = 0;
        line[1] = zcbRGB(cl0) + zcbRGB(cl1) + zcbRGB(cl2) + zcbRGB(cl3);
        line[2] = zcrRGB(cl0) + zcrRGB(cl1) + zcrRGB(cl2) + zcrRGB(cl3);
        tmpsrc++;
        line += 3;
    }

    for (y = 0; y < height; y++) {
	int scn = (y & 1) ? scanlinelevel : 128;
	randomshift = rand() & 15;
	randomoffset = rand() & 2047;

        tmpsrc = src;
        tmptrg = trg;

        line = lineptr0;
        lineptr0 = lineptr1;
        lineptr1 = line;

        tmpsrc = src;
        line = lineptr0;
        for (x = 0; x < wint; x++) {
	    int r = (randomtable[randomoffset++] >> randomshift) & pal_noise_mask;
            uae_u32 cl0, cl1, cl2, cl3;
            cl0 = tmpsrc[0];
            cl1 = tmpsrc[xx1];
            cl2 = tmpsrc[xx2];
            cl3 = tmpsrc[xx3];
	    line[0] = (zylRGB(cl1) + zyhRGB(cl2) * (scn - r) / 128 + zylRGB(cl3)) / 256; /* 1/4 + 1/2 + 1/4 */
            line[1] = zcbRGB(cl0) + zcbRGB(cl1) + zcbRGB(cl2) + zcbRGB(cl3);
            line[2] = zcrRGB(cl0) + zcrRGB(cl1) + zcrRGB(cl2) + zcrRGB(cl3);
            tmpsrc++;
            line += 3;
        }

        line = lineptr0;
        linepre = lineptr1;
        for (x = 0; x < (wfast << 3) + wend + wstart; x++) {

            l = line[0];
            u = (line[1] + linepre[1]) / 8;
            v = (line[2] + linepre[2]) / 8;
            line += 3;
            linepre += 3;

            red = (v + l) / 256;
            blu = (u + l) / 256;
            grn = (l * 256 - 50 * u - 130 * v) / (256 * 256);
            *tmptrg++ = predc[red] | pgrec[grn] | pbluc[blu];
        }

        src += pitchs;
        trg += pitcht;
    }
}
