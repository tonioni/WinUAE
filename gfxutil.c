 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Common code needed by all the various graphics systems.
  *
  * (c) 1996 Bernd Schmidt, Ed Hanway, Samuel Devulder
  */

#include "sysconfig.h"
#include "sysdeps.h"
#include "custom.h"
#include "xwin.h"

#define	RED 	0
#define	GRN	1
#define	BLU	2

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


unsigned long doMask (int p, int bits, int shift)
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

unsigned long doMask256 (int p, int bits, int shift)
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
    if(bits >= 8) shift2 = 0; else shift2 = 8 - bits;
    return (i >> shift2) << shift;
}

static unsigned int doAlpha (int alpha, int bits, int shift)
{
    return (alpha & ((1 << bits) - 1)) << shift;
}

#if 0
static void colormodify (int *r, int *g, int *b)
{
    double h, l, s;

    RGBToHLS (*r, *g, *b, &h, &l, &s);

    h = h + currprefs.gfx_hue / 10.0;
    if (h > 359) h = 359;
    if (h < 0) h = 0;
    s = s + currprefs.gfx_saturation / 30.0;
    if (s > 99) s = 99;
    if (s < 0) s = 0;
    l = l + currprefs.gfx_luminance / 30.0;
    l = (l - currprefs.gfx_contrast / 30.0) / (100 - 2 * currprefs.gfx_contrast / 30.0) * 100;
    l = pow (l / 100.0, (currprefs.gfx_gamma + 1000) / 1000.0) * 100.0;
    if (l > 99) l = 99;
    if (l < 0) l = 0;
    HLSToRGB (h, l, s, r, g, b);
}
#endif

void alloc_colors64k (int rw, int gw, int bw, int rs, int gs, int bs, int aw, int as, int alpha)
{
    int i;

    for (i = 0; i < 4096; i++) {
	int r = (i >> 8) << 4;
	int g = ((i >> 4) & 0xF) << 4;
	int b = (i & 0xF) << 4;
	//colormodify (&r, &g, &b);
	xcolors[i] = doMask(r, rw, rs) | doMask(g, gw, gs) | doMask(b, bw, bs) | doAlpha (alpha, aw, as);
    }
#ifdef AGA
    /* create AGA color tables */
    for(i = 0; i < 256; i++) {
	xredcolors[i] = doColor (i, rw, rs) | doAlpha (alpha, aw, as);
	xgreencolors[i] = doColor (i, gw, gs) | doAlpha (alpha, aw, as);
	xbluecolors[i] = doColor (i, bw, bs) | doAlpha (alpha, aw, as);;
    }
#endif
}

static int allocated[4096];
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
	write_log ("Not enough mem for colormap!\n");
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
	xcolors[i] = i;

    map = (xcolnr *)malloc (sizeof(xcolnr) * maxcol);
    if (!map) {
	write_log ("Not enough mem for colormap!\n");
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
/* sam:                      ^^^^^^^ */
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
	write_log ("Not enough mem for colormap!\n");
	abort();
    }

    for (i = 0; i < 4096; i++)
	xcolors[i] = i;

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
/*    write_log ("%d color(s) lost\n",maxcol - l);*/

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
