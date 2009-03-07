 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Common code needed by all the various graphics systems.
  *
  * (c) 1996 Bernd Schmidt, Ed Hanway, Samuel Devulder
  */

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "custom.h"
#include "rtgmodes.h"
#include "xwin.h"
#include "gfxfilter.h"

#include <math.h>

#define RED	0
#define GRN	1
#define BLU	2

/*
 * dither matrix
 */
static uae_u8 dither[4][4] =
{
  { 0, 8, 2, 10 },
  { 12, 4, 14, 6 },
  { 3, 11, 1, 9 },
  { 14 /* 15 */, 7, 13, 5 }
};


unsigned int doMask (int p, int bits, int shift)
{
    /* scale to 0..255, shift to align msb with mask, and apply mask */
    unsigned long val = p << 24;
    if (!bits)
	return 0;
    val >>= (32 - bits);
    val <<= shift;

    return val;
}

int bits_in_mask (unsigned long mask)
{
    int n = 0;
    while (mask) {
	n += mask & 1;
	mask >>= 1;
    }
    return n;
}

int mask_shift (unsigned long mask)
{
    int n = 0;
    while (!(mask & 1)) {
	n++;
	mask >>= 1;
    }
    return n;
}

unsigned int doMask256 (int p, int bits, int shift)
{
    /* p is a value from 0 to 255 (Amiga color value)
     * shift to align msb with mask, and apply mask */

    unsigned long val = p * 0x01010101UL;
    val >>= (32 - bits);
    val <<= shift;

    return val;
}

static unsigned int doColor (int i, int bits, int shift)
{
    int shift2;
    if(bits >= 8)
	shift2 = 0;
    else
	shift2 = 8 - bits;
    return (i >> shift2) << shift;
}

static unsigned int doAlpha (int alpha, int bits, int shift)
{
    return (alpha & ((1 << bits) - 1)) << shift;
}

static float video_gamma (float value, float gamma, float bri, float con)
{
    double factor;
    float ret;

    value += bri;
    value *= con;

    if (value <= 0.0f)
	return 0.0f;

    factor = pow(255.0f, 1.0f - gamma);
    ret = (float)(factor * pow(value, gamma));

    if (ret < 0.0f)
	ret = 0.0f;

    return ret;
}

static uae_u32 gamma[256 * 3];
static int lf, hf;

static void video_calc_gammatable (void)
{
    int i;
    float bri, con, gam, v;
    uae_u32 vi;

    bri = ((float)(currprefs.gfx_luminance))
	  * (128.0f / 1000.0f);
    con = ((float)(currprefs.gfx_contrast + 1000)) / 1000.0f;
    gam = ((float)(1000 - currprefs.gfx_gamma)) / 1000.0f;

    lf = 64 * currprefs.gfx_filter_blur / 1000;
    hf = 256 - lf * 2;

    for (i = 0; i < (256 * 3); i++) {
	v = video_gamma((float)(i - 256), gam, bri, con);

	vi = (uae_u32)v;
	if (vi > 255)
	    vi = 255;

	if (currprefs.gfx_luminance == 0 && currprefs.gfx_contrast == 0 && currprefs.gfx_gamma == 0)
	    vi = i & 0xff;

	gamma[i] = vi;
    }
}

static uae_u32 limit256 (double v)
{
    v = v * (double)(currprefs.gfx_filter_contrast + 1000) / 1000.0 + currprefs.gfx_filter_luminance / 10.0;
    if (v < 0)
	v = 0;
    if (v > 255)
	v = 255;
    return ((uae_u32)v) & 0xff;
}
static uae_u32 limit256rb (double v)
{
    v *= (double)(currprefs.gfx_filter_saturation + 1000) / 1000.0;
    if (v < -128)
	v = -128;
    if (v > 127)
	v = 127;
    return ((uae_u32)v) & 0xff;
}
static double get_y (int r, int g, int b)
{
    return 0.2989f * r + 0.5866f * g + 0.1145f * b;
}
static uae_u32 get_yh (int r, int g, int b)
{
    return limit256 (get_y (r, g, b) * hf / 256);
}
static uae_u32 get_yl (int r, int g, int b)
{
    return limit256 (get_y (r, g, b) * lf / 256);
}
static uae_u32 get_cb (int r, int g, int b)
{
    return limit256rb (-0.168736f * r - 0.331264f * g + 0.5f * b);
}
static uae_u32 get_cr (int r, int g, int b)
{
    return limit256rb (0.5f * r - 0.418688f * g - 0.081312f * b);
}

extern uae_s32 tyhrgb[65536];
extern uae_s32 tylrgb[65536];
extern uae_s32 tcbrgb[65536];
extern uae_s32 tcrrgb[65536];
extern uae_u32 redc[3 * 256], grec[3 * 256], bluc[3 * 256];

static uae_u32 lowbits (int v, int shift, int lsize)
{
    v >>= shift;
    v &= (1 << lsize) - 1;
    return v;
}

void alloc_colors_picasso (int rw, int gw, int bw, int rs, int gs, int bs, int rgbfmt)
{
    int byte_swap = 0;
    int i;
    int red_bits = 0, green_bits, blue_bits;
    int red_shift, green_shift, blue_shift;
    int bpp = rw + gw + bw;

    switch (rgbfmt)
    {
	case RGBFB_R5G6B5PC:
	red_bits = 5;
	green_bits = 6;
	blue_bits = 5;
	red_shift = 11;
	green_shift = 5;
	blue_shift = 0;
	break;
	case RGBFB_R5G5B5PC:
	red_bits = green_bits = blue_bits = 5;
	red_shift = 10;
	green_shift = 5;
	blue_shift = 0;
	break;
	case RGBFB_R5G6B5:
	red_bits = 5;
	green_bits = 6;
	blue_bits = 5;
	red_shift = 11;
	green_shift = 5;
	blue_shift = 0;
	byte_swap = 1;
	break;
	case RGBFB_R5G5B5:
	red_bits = green_bits = blue_bits = 5;
	red_shift = 10;
	green_shift = 5;
	blue_shift = 0;
	byte_swap = 1;
	break;
	case RGBFB_B5G6R5PC:
	red_bits = 5;
	green_bits = 6;
	blue_bits = 5;
	red_shift = 0;
	green_shift = 5;
	blue_shift = 11;
	break;
	case RGBFB_B5G5R5PC:
	red_bits = 5;
	green_bits = 5;
	blue_bits = 5;
	red_shift = 0;
	green_shift = 5;
	blue_shift = 10;
	break;
	default:
	red_bits = rw;
	green_bits = gw;
	blue_bits = bw;
	red_shift = rs;
	green_shift = gs;
	blue_shift = bs;
	break;
    }

    memset (p96_rgbx16, 0, sizeof p96_rgbx16);

    if (red_bits) {
	int lrbits = 8 - red_bits;
	int lgbits = 8 - green_bits;
	int lbbits = 8 - blue_bits;
	int lrmask = (1 << red_bits) - 1;
	int lgmask = (1 << green_bits) - 1;
	int lbmask = (1 << blue_bits) - 1;
	for (i = 65535; i >= 0; i--) {
	    uae_u32 r, g, b, c;
	    uae_u32 j = byte_swap ? bswap_16 (i) : i;
	    r = (((j >>   red_shift) & lrmask) << lrbits) | lowbits (j,   red_shift, lrbits);
	    g = (((j >> green_shift) & lgmask) << lgbits) | lowbits (j, green_shift, lgbits);
	    b = (((j >>  blue_shift) & lbmask) << lbbits) | lowbits (j,  blue_shift, lbbits);
	    c = doMask(r, rw, rs) | doMask(g, gw, gs) | doMask(b, bw, bs);
	    if (bpp <= 16)
		c *= 0x00010001;
	    p96_rgbx16[i] = c;
	}
    }
}

void alloc_colors_rgb (int rw, int gw, int bw, int rs, int gs, int bs, int aw, int as, int alpha, int byte_swap,
		       uae_u32 *rc, uae_u32 *gc, uae_u32 *bc)
{
    int bpp = rw + gw + bw + aw;
    int i;
    for(i = 0; i < 256; i++) {
	int j;

	if (currprefs.gfx_blackerthanblack) {
	    j = i * 15 / 16 + 16;
	} else {  
	    j = i;
	}
	j += 256;

	rc[i] = doColor (gamma[j], rw, rs) | doAlpha (alpha, aw, as);
	gc[i] = doColor (gamma[j], gw, gs) | doAlpha (alpha, aw, as);
	bc[i] = doColor (gamma[j], bw, bs) | doAlpha (alpha, aw, as);
	if (byte_swap) {
	    if (bpp <= 16) {
		rc[i] = bswap_16 (rc[i]);
		gc[i] = bswap_16 (gc[i]);
		bc[i] = bswap_16 (bc[i]);
	    } else {
		rc[i] = bswap_32 (rc[i]);
		gc[i] = bswap_32 (gc[i]);
		bc[i] = bswap_32 (bc[i]);
	    }
	}
	if (bpp <= 16) {
	    /* Fill upper 16 bits of each colour value with
	     * a copy of the colour. */
	    rc[i] = rc[i] * 0x00010001;
	    gc[i] = gc[i] * 0x00010001;
	    bc[i] = bc[i] * 0x00010001;
	}
    }
}

void alloc_colors64k (int rw, int gw, int bw, int rs, int gs, int bs, int aw, int as, int alpha, int byte_swap)
{
    int bpp = rw + gw + bw + aw;
    int i, j;

    video_calc_gammatable();
    j = 256;
    for (i = 0; i < 4096; i++) {
	int r = ((i >> 8) << 4) | (i >> 8);
	int g = (((i >> 4) & 0xf) << 4) | ((i >> 4) & 0x0f);
	int b = ((i & 0xf) << 4) | (i & 0x0f);
	r = gamma[r + j];
	g = gamma[g + j];
	b = gamma[b + j];
	xcolors[i] = doMask(r, rw, rs) | doMask(g, gw, gs) | doMask(b, bw, bs) | doAlpha (alpha, aw, as);
	if (byte_swap) {
	    if (bpp <= 16)
		xcolors[i] = bswap_16 (xcolors[i]);
	    else
		xcolors[i] = bswap_32 (xcolors[i]);
	}
	if (bpp <= 16) {
	    /* Fill upper 16 bits of each colour value
	     * with a copy of the colour. */
	    xcolors[i] |= xcolors[i] * 0x00010001;
	}
    }
#if defined(AGA) || defined(GFXFILTER)
    alloc_colors_rgb (rw, gw, bw, rs, gs, bs, aw, as, alpha, byte_swap, xredcolors, xgreencolors, xbluecolors);
    /* copy original color table */
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
    if (usedfilter && usedfilter->yuv) {
	/* create internal 5:6:5 color tables */
	for (i = 0; i < 256; i++) {
	    j = i + 256;
	    xredcolors[i] = doColor (gamma[j], 5, 11);
	    xgreencolors[i] = doColor (gamma[j], 6, 5);
	    xbluecolors[i] = doColor (gamma[j], 5, 0);
	    if (bpp <= 16) {
		/* Fill upper 16 bits of each colour value with
		 * a copy of the colour. */
		xredcolors  [i] = xredcolors  [i] * 0x00010001;
		xgreencolors[i] = xgreencolors[i] * 0x00010001;
		xbluecolors [i] = xbluecolors [i] * 0x00010001;
	    }
	}
	for (i = 0; i < 4096; i++) {
	    int r = ((i >> 8) << 4) | (i >> 8);
	    int g = (((i >> 4) & 0xf) << 4) | ((i >> 4) & 0x0f);
	    int b = ((i & 0xf) << 4) | (i & 0x0f);
	    r = gamma[r + 256];
	    g = gamma[g + 256];
	    b = gamma[b + 256];
	    xcolors[i] = doMask(r, 5, 11) | doMask(g, 6, 5) | doMask(b, 5, 0);
	    if (byte_swap) {
		if (bpp <= 16)
		    xcolors[i] = bswap_16 (xcolors[i]);
		else
		    xcolors[i] = bswap_32 (xcolors[i]);
	    }
	    if (bpp <= 16) {
		/* Fill upper 16 bits of each colour value
		 * with a copy of the colour. */
		xcolors[i] |= xcolors[i] * 0x00010001;
	    }
	}

	/* create RGB 5:6:5 -> YUV tables */
	for (i = 0; i < 65536; i++) {
	    uae_u32 r, g, b;
	    r = (((i >> 11) & 31) << 3) | lowbits (i, 11, 3);
	    r = gamma[r + 256];
	    g = (((i >>  5) & 63) << 2) | lowbits (i,  5, 2);
	    g = gamma[g + 256];
	    b = (((i >>  0) & 31) << 3) | lowbits (i,  0, 3);
	    b = gamma[b + 256];
	    tyhrgb[i] = get_yh (r, g, b) * 256 * 256;
	    tylrgb[i] = get_yl (r, g, b) * 256 * 256;
	    tcbrgb[i] = ((uae_s8)get_cb (r, g, b)) * 256;
	    tcrrgb[i] = ((uae_s8)get_cr (r, g, b)) * 256;
	}
    }

#endif
    xredcolor_b = rw;
    xgreencolor_b = gw;
    xbluecolor_b = bw;
    xredcolor_s = rs;
    xgreencolor_s = gs;
    xbluecolor_s = bs;
    xredcolor_m = (1 << rw) - 1;
    xgreencolor_m = (1 << gw) - 1;
    xbluecolor_m = (1 << bw) - 1;
}

static int color_diff[4096];
static int newmaxcol = 0;

void setup_maxcol (int max)
{
    newmaxcol = max;
}

void alloc_colors256 (allocfunc_type allocfunc)
{
    int nb_cols[3]; /* r,g,b */
    int maxcol = newmaxcol == 0 ? 256 : newmaxcol;
    int i,j,k,l;
    xcolnr *map;

    map = (xcolnr *)malloc (sizeof(xcolnr) * maxcol);
    if (!map) {
	write_log (L"Not enough mem for colormap!\n");
	abort ();
    }

    /*
     * compute #cols per components
     */
    for (i = 1; i*i*i <= maxcol; ++i)
	;
    --i;

    nb_cols[RED] = i;
    nb_cols[GRN] = i;
    nb_cols[BLU] = i;

    /*
     * set the colormap
     */
    l = 0;
    for (i = 0; i < nb_cols[RED]; ++i) {
	int r = (i * 15) / (nb_cols[RED] - 1);
	for (j = 0; j < nb_cols[GRN]; ++j) {
	    int g = (j * 15) / (nb_cols[GRN] - 1);
	    for (k = 0; k < nb_cols[BLU]; ++k) {
		int b = (k * 15) / (nb_cols[BLU] - 1);
		int result;
		result = allocfunc (r, g, b, map + l);
		l++;
	    }
	}
    }
/*    printf("%d color(s) lost\n",maxcol - l);*/

    /*
     * for each component compute the mapping
     */
    {
	int diffr, diffg, diffb, maxdiff = 0, won = 0, lost;
	int r, d = 8;
	for (r = 0; r < 16; ++r) {
	    int cr, g, q;

	    k = nb_cols[RED]-1;
	    cr = (r * k) / 15;
	    q = (r * k) % 15;
	    if (q > d && cr < k) ++cr;
	    diffr = abs (cr*k - r);
	    for (g = 0; g < 16; ++g) {
		int cg, b;

		k = nb_cols[GRN]-1;
		cg = (g * k) / 15;
		q  = (g * k) % 15;
		if (q > d && cg < k) ++cg;
		diffg = abs (cg*k - g);
		for (b = 0; b < 16; ++b) {
		    int cb, rgb = (r << 8) | (g << 4) | b;

		    k = nb_cols[BLU]-1;
		    cb = (b * k) / 15;
		    q = (b * k) % 15;
		    if (q > d && cb < k) ++cb;
		    diffb = abs (cb*k - b);
		    xcolors[rgb] = map[(cr * nb_cols[GRN] + cg) * nb_cols[BLU] + cb];
		    color_diff[rgb] = diffr + diffg + diffb;
		    if (color_diff[rgb] > maxdiff)
			maxdiff = color_diff[rgb];
		}
	    }
	}
	while (maxdiff > 0 && l < maxcol) {
	    int newmaxdiff = 0;
	    lost = 0; won++;
	    for (r = 15; r >= 0; r--) {
		int g;

		for (g = 15; g >= 0; g--) {
		    int b;

		    for (b = 15; b >= 0; b--) {
			int rgb = (r << 8) | (g << 4) | b;

			if (color_diff[rgb] == maxdiff) {
			    int result;

			    if (l >= maxcol)
				lost++;
			    else {
				result = allocfunc (r, g, b, xcolors + rgb);
				l++;
			    }
			    color_diff[rgb] = 0;
			} else if (color_diff[rgb] > newmaxdiff)
				newmaxdiff = color_diff[rgb];

		    }
		}
	    }
	    maxdiff = newmaxdiff;
	}
/*	printf("%d color(s) lost, %d stages won\n",lost, won);*/
    }
    free (map);
}

/*
 * This dithering process works by letting UAE run internaly in 12bit
 * mode and doing the dithering on the fly when rendering to the display.
 * The dithering algorithm is quite fast but uses lot of memory (4*8*2^12 =
 * 128Kb). I don't think that is a trouble right now, but when UAE will
 * emulate AGA and work internaly in 24bit mode, that dithering algorithm
 * will need 4*8*2^24 = 512Mb. Obviously that fast algorithm will not be
 * tractable. However, we could then use an other algorithm, slower, but
 * far more reasonable (I am thinking about the one that is used in DJPEG).
 */

uae_u8 cidx[4][8*4096]; /* fast, but memory hungry =:-( */

/*
 * Compute dithering structures
 */
void setup_greydither_maxcol (int maxcol, allocfunc_type allocfunc)
{
    int i,j,k;
    xcolnr *map;

    for (i = 0; i < 4096; i++)
	xcolors[i] = i << 16 | i;

    map = (xcolnr *)malloc (sizeof(xcolnr) * maxcol);
    if (!map) {
	write_log (L"Not enough mem for colormap!\n");
	abort();
    }

    /*
     * set the colormap
     */
    for (i = 0; i < maxcol; ++i) {
	int c, result;
	c = (15 * i + (maxcol-1)/2) / (maxcol - 1);
	result = allocfunc(c, c, c, map + i);
	/* @@@ check for errors */
    }

    /*
     * for each componant compute the mapping
     */
    for (i = 0; i < 4; ++i) {
	for (j = 0; j < 4; ++j) {
	    int r, d = dither[i][j]*17;
	    for (r = 0; r<16; ++r) {
		int g;
		for (g = 0; g < 16; ++g) {
		    int  b;
		    for (b = 0; b < 16; ++b) {
			int rgb = (r << 8) | (g << 4) | b;
			int c,p,q;

			c = (77  * r +
			     151 * g +
			     28  * b) / 15; /* c in 0..256 */

			k = maxcol-1;
			p = (c * k) / 256;
			q = (c * k) % 256;
			if (q /*/ k*/> d /*/ k*/ && p < k) ++p;
/* sam:			     ^^^^^^^ */
/*  It seems that produces better output */
			cidx[i][rgb + (j+4)*4096] =
			    cidx[i][rgb + j*4096] = (uae_u8)map[p];
		    }
		}
	    }
	}
    }
    free (map);
}

void setup_greydither (int bits, allocfunc_type allocfunc)
{
    setup_greydither_maxcol(1 << bits, allocfunc);
}

void setup_dither (int bits, allocfunc_type allocfunc)
{
    int nb_cols[3]; /* r,g,b */
    int maxcol = 1 << bits;
    int i,j,k,l;

    xcolnr *map;
    int *redvals, *grnvals, *bluvals;

    map = (xcolnr *)malloc (sizeof(xcolnr) * maxcol);
    if (!map) {
	write_log (L"Not enough mem for colormap!\n");
	abort();
    }

    for (i = 0; i < 4096; i++)
	xcolors[i] = i << 16 | i;

    /*
     * compute #cols per components
     */
    for (i = 1; i*i*i <= maxcol; ++i)
	;
    --i;

    nb_cols[RED] = i;
    nb_cols[GRN] = i;
    nb_cols[BLU] = i;

    if (nb_cols[RED]*(++i)*nb_cols[BLU] <= maxcol) {
	nb_cols[GRN] = i;
	if ((i)*nb_cols[GRN]*nb_cols[BLU] <= maxcol)
	    nb_cols[RED] = i;
    }

    redvals = (int *)malloc (sizeof(int) * maxcol);
    grnvals = redvals + nb_cols[RED];
    bluvals = grnvals + nb_cols[GRN];
    /*
     * set the colormap
     */
    l = 0;
    for (i = 0; i < nb_cols[RED]; ++i) {
	int r = (i * 15) / (nb_cols[RED] - 1);
	redvals[i] = r;
	for (j = 0; j < nb_cols[GRN]; ++j) {
	    int g = (j * 15) / (nb_cols[GRN] - 1);
	    grnvals[j] = g;
	    for (k = 0; k < nb_cols[BLU]; ++k) {
		int b = (k * 15) / (nb_cols[BLU] - 1);
		int result;
		bluvals[k] = b;
		result = allocfunc(r, g, b, map + l);
		l++;
	    }
	}
    }
/*    write_log (L"%d color(s) lost\n",maxcol - l);*/

    /*
     * for each component compute the mapping
     */
    {
	int r;
	for (r = 0; r < 16; ++r) {
	    int g;
	    for (g = 0; g < 16; ++g) {
		int b;
		for (b = 0; b < 16; ++b) {
		    int rgb = (r << 8) | (g << 4) | b;

		    for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j) {
			int d = dither[i][j];
			int cr, cg, cb, k, q;
#if 0 /* Slightly different algorithm. Needs some tuning. */
			int rederr = 0, grnerr = 0, bluerr = 0;

			k  = nb_cols[RED]-1;
			cr = r * k / 15;
			q  = r * k - 15*cr;
			if (cr < 0)
			    cr = 0;
			else if (q / k > d / k && rederr <= 0)
			    ++cr;
			if (cr > k) cr = k;
			rederr += redvals[cr]-r;

			k  = nb_cols[GRN]-1;
			cg = g * k / 15;
			q  = g * k - 15*cg;
			if (cg < 0)
			    cg = 0;
			else if (q / k > d / k && grnerr <= 0)
			    ++cg;
			if (cg > k) cg = k;
			grnerr += grnvals[cg]-g;

			k  = nb_cols[BLU]-1;
			cb = b * k / 15;
			q  = b * k - 15*cb;
			if (cb < 0)
			    cb = 0;
			else if (q / k > d / k && bluerr <= 0)
			    ++cb;
			if (cb > k) cb = k;
			bluerr += bluvals[cb]-b;
#else
			k  = nb_cols[RED]-1;
			cr = r * k / 15;
			q  = r * k - 15*cr;
			if (cr < 0)
			    cr = 0;
			else if (q /*/ k*/ > d /*/ k*/)
			    ++cr;
			if (cr > k) cr = k;

			k  = nb_cols[GRN]-1;
			cg = g * k / 15;
			q  = g * k - 15*cg;
			if (cg < 0)
			    cg = 0;
			else if (q /*/ k*/ > d /*/ k*/)
			    ++cg;
			if (cg > k) cg = k;

			k  = nb_cols[BLU]-1;
			cb = b * k / 15;
			q  = b * k - 15*cb;
			if (cb < 0)
			    cb = 0;
			else if (q /*/ k*/ > d /*/ k*/)
			    ++cb;
			if (cb > k) cb = k;
#endif
			cidx[i][rgb + (j+4)*4096] = cidx[i][rgb + j*4096] = (uae_u8)map[(cr*nb_cols[GRN]+cg)*nb_cols[BLU]+cb];
		    }
		}
	    }
	}
    }
    free (redvals);
    free (map);
}

#if !defined X86_ASSEMBLY
/*
 * Dither the line.
 * Make sure you call this only with (len & 3) == 0, or you'll just make
 * yourself unhappy.
 */

void DitherLine (uae_u8 *l, uae_u16 *r4g4b4, int x, int y, uae_s16 len, int bits)
{
    uae_u8 *dith = cidx[y&3]+(x&3)*4096;
    uae_u8 d = 0;
    int bitsleft = 8;

    if (bits == 8) {
	while (len > 0) {
	    *l++ = dith[0*4096 + *r4g4b4++];
	    *l++ = dith[1*4096 + *r4g4b4++];
	    *l++ = dith[2*4096 + *r4g4b4++];
	    *l++ = dith[3*4096 + *r4g4b4++];
	    len -= 4;
	}
	return;
    }

    while (len) {
	int v;
	v = dith[0*4096 + *r4g4b4++];
	bitsleft -= bits;
	d |= (v << bitsleft);
	if (!bitsleft)
	    *l++ = d, bitsleft = 8, d = 0;

	v = dith[1*4096 + *r4g4b4++];
	bitsleft -= bits;
	d |= (v << bitsleft);
	if (!bitsleft)
	    *l++ = d, bitsleft = 8, d = 0;

	v = dith[2*4096 + *r4g4b4++];
	bitsleft -= bits;
	d |= (v << bitsleft);
	if (!bitsleft)
	    *l++ = d, bitsleft = 8, d = 0;

	v = dith[3*4096 + *r4g4b4++];
	bitsleft -= bits;
	d |= (v << bitsleft);
	if (!bitsleft)
	    *l++ = d, bitsleft = 8, d = 0;
	len -= 4;
    }
}
#endif
