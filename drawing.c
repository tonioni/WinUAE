//#define XLINECHECK

 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Screen drawing functions
  *
  * Copyright 1995-2000 Bernd Schmidt
  * Copyright 1995 Alessandro Bissacco
  * Copyright 2000,2001 Toni Wilen
  */

/* There are a couple of concepts of "coordinates" in this file.
   - DIW coordinates
   - DDF coordinates (essentially cycles, resolution lower than lores by a factor of 2)
   - Pixel coordinates
     * in the Amiga's resolution as determined by BPLCON0 ("Amiga coordinates")
     * in the window resolution as determined by the preferences ("window coordinates").
     * in the window resolution, and with the origin being the topmost left corner of
       the window ("native coordinates")
   One note about window coordinates.  The visible area depends on the width of the
   window, and the centering code.  The first visible horizontal window coordinate is
   often _not_ 0, but the value of VISIBLE_LEFT_BORDER instead.

   One important thing to remember: DIW coordinates are in the lowest possible
   resolution.

   To prevent extremely bad things (think pixels cut in half by window borders) from
   happening, all ports should restrict window widths to be multiples of 16 pixels.  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <assert.h>

#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "gui.h"
#include "picasso96.h"
#include "drawing.h"
#ifdef JIT
#include "compemu.h"
#endif
#include "savestate.h"

int lores_factor, lores_shift;

/* The shift factor to apply when converting between Amiga coordinates and window
   coordinates.  Zero if the resolution is the same, positive if window coordinates
   have a higher resolution (i.e. we're stretching the image), negative if window
   coordinates have a lower resolution (i.e. we're shrinking the image).  */
static int res_shift;

int interlace_seen = 0;
#define AUTO_LORES_FRAMES 10
static int can_use_lores = 0, frame_res, frame_res_lace, last_max_ypos;

/* Lookup tables for dual playfields.  The dblpf_*1 versions are for the case
   that playfield 1 has the priority, dbplpf_*2 are used if playfield 2 has
   priority.  If we need an array for non-dual playfield mode, it has no number.  */
/* The dbplpf_ms? arrays contain a shift value.  plf_spritemask is initialized
   to contain two 16 bit words, with the appropriate mask if pf1 is in the
   foreground being at bit offset 0, the one used if pf2 is in front being at
   offset 16.  */

static int dblpf_ms1[256], dblpf_ms2[256], dblpf_ms[256];
static int dblpf_ind1[256], dblpf_ind2[256];

static int dblpf_2nd1[256], dblpf_2nd2[256];

static int dblpfofs[] = { 0, 2, 4, 8, 16, 32, 64, 128 };

static int sprite_offs[256];

static uae_u32 clxtab[256];

/* Video buffer description structure. Filled in by the graphics system
 * dependent code. */

struct vidbuf_description gfxvidinfo;

/* OCS/ECS color lookup table. */
xcolnr xcolors[4096];

#ifdef AGA
static uae_u8 spriteagadpfpixels[MAX_PIXELS_PER_LINE * 2]; /* AGA dualplayfield sprite */
/* AGA mode color lookup tables */
unsigned int xredcolors[256], xgreencolors[256], xbluecolors[256];
static int dblpf_ind1_aga[256], dblpf_ind2_aga[256];
#else
static uae_u8 spriteagadpfpixels[1];
static int dblpf_ind1_aga[1], dblpf_ind2_aga[1];
#endif
int xredcolor_s, xredcolor_b, xredcolor_m;
int xgreencolor_s, xgreencolor_b, xgreencolor_m;
int xbluecolor_s, xbluecolor_b, xbluecolor_m;

struct color_entry colors_for_drawing;

/* The size of these arrays is pretty arbitrary; it was chosen to be "more
   than enough".  The coordinates used for indexing into these arrays are
   almost, but not quite, Amiga coordinates (there's a constant offset).  */
union {
    /* Let's try to align this thing. */
    double uupzuq;
    long int cruxmedo;
    uae_u8 apixels[MAX_PIXELS_PER_LINE * 2];
    uae_u16 apixels_w[MAX_PIXELS_PER_LINE * 2 / 2];
    uae_u32 apixels_l[MAX_PIXELS_PER_LINE * 2 / 4];
} pixdata;

#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
uae_u16 *spixels;
#else
uae_u16 spixels[2 * MAX_SPR_PIXELS];
#endif

/* Eight bits for every pixel.  */
union sps_union spixstate;

static uae_u32 ham_linebuf[MAX_PIXELS_PER_LINE * 2];

uae_u8 *xlinebuffer;

static int *amiga2aspect_line_map, *native2amiga_line_map;
static uae_u8 *row_map[MAX_VIDHEIGHT + 1];
static uae_u8 row_tmp[MAX_PIXELS_PER_LINE * 32 / 8];
static int max_drawn_amiga_line;

/* line_draw_funcs: pfield_do_linetoscr, pfield_do_fill_line, decode_ham */
typedef void (*line_draw_func)(int, int);

#define LINE_UNDECIDED 1
#define LINE_DECIDED 2
#define LINE_DECIDED_DOUBLE 3
#define LINE_AS_PREVIOUS 4
#define LINE_BLACK 5
#define LINE_REMEMBERED_AS_BLACK 6
#define LINE_DONE 7
#define LINE_DONE_AS_PREVIOUS 8
#define LINE_REMEMBERED_AS_PREVIOUS 9

static char linestate[(MAXVPOS + 1) * 2 + 1];

uae_u8 line_data[(MAXVPOS + 1) * 2][MAX_PLANES * MAX_WORDS_PER_LINE * 2];

/* Centering variables.  */
static int min_diwstart, max_diwstop;
/* The visible window: VISIBLE_LEFT_BORDER contains the left border of the visible
   area, VISIBLE_RIGHT_BORDER the right border.  These are in window coordinates.  */
static int visible_left_border, visible_right_border;
static int linetoscr_x_adjust_bytes;
static int thisframe_y_adjust;
static int thisframe_y_adjust_real, max_ypos_thisframe, min_ypos_for_screen;
static int extra_y_adjust;
int thisframe_first_drawn_line, thisframe_last_drawn_line;

/* A frame counter that forces a redraw after at least one skipped frame in
   interlace mode.  */
static int last_redraw_point;

static int first_drawn_line, last_drawn_line;
static int first_block_line, last_block_line;

#define NO_BLOCK -3

/* These are generated by the drawing code from the line_decisions array for
   each line that needs to be drawn.  These are basically extracted out of
   bit fields in the hardware registers.  */
static int bplehb, bplham, bpldualpf, bpldualpfpri, bpldualpf2of, bplplanecnt, bplres;
static int plf1pri, plf2pri;
static uae_u32 plf_sprite_mask;
static int sbasecol[2] = { 16, 16 };
static int brdsprt, brdblank, brdblank_changed;

int picasso_requested_on;
int picasso_on;

uae_sem_t gui_sem;
int inhibit_frame;

int framecnt = 0;
static int frame_redraw_necessary;
static int picasso_redraw_necessary;

#ifdef XLINECHECK
static void xlinecheck (unsigned int start, unsigned int end)
{
    unsigned int xstart = (unsigned int)xlinebuffer + start * gfxvidinfo.pixbytes;
    unsigned int xend = (unsigned int)xlinebuffer + end * gfxvidinfo.pixbytes;
    unsigned int end1 = (unsigned int)gfxvidinfo.bufmem + gfxvidinfo.rowbytes * gfxvidinfo.height;
    int min = linetoscr_x_adjust_bytes / gfxvidinfo.pixbytes;
    int ok = 1;

    if (xstart >= gfxvidinfo.emergmem && xstart < gfxvidinfo.emergmem + 4096 * gfxvidinfo.pixbytes &&
	xend >= gfxvidinfo.emergmem && xend < gfxvidinfo.emergmem + 4096 * gfxvidinfo.pixbytes)
	return;

    if (xstart < (unsigned int)gfxvidinfo.bufmem || xend < (unsigned int)gfxvidinfo.bufmem)
	ok = 0;
    if (xend > end1 || xstart >= end1)
	ok = 0;
    xstart -= (unsigned int)gfxvidinfo.bufmem;
    xend -= (unsigned int)gfxvidinfo.bufmem;
    if ((xstart % gfxvidinfo.rowbytes) >= gfxvidinfo.width * gfxvidinfo.pixbytes)
	ok = 0;
    if ((xend % gfxvidinfo.rowbytes) >= gfxvidinfo.width * gfxvidinfo.pixbytes)
	ok = 0;
    if (xstart >= xend)
	ok = 0;
    if (xend - xstart > gfxvidinfo.width * gfxvidinfo.pixbytes)
	ok = 0;

    if (!ok) {
	    write_log ("*** %d-%d (%dx%dx%d %d) %p\n",
		start - min, end - min, gfxvidinfo.width, gfxvidinfo.height,
		gfxvidinfo.pixbytes, gfxvidinfo.rowbytes,
		xlinebuffer);
    }
}
#else
#define xlinecheck
#endif

STATIC_INLINE void count_frame (void)
{
    framecnt++;
    if (framecnt >= currprefs.gfx_framerate)
	framecnt = 0;
    if (inhibit_frame)
        framecnt = 1;
}

int coord_native_to_amiga_x (int x)
{
    x += visible_left_border;
    x <<= (1 - lores_shift);
    return x + 2 * DISPLAY_LEFT_SHIFT - 2 * DIW_DDF_OFFSET;
}

int coord_native_to_amiga_y (int y)
{
    return native2amiga_line_map[y] + thisframe_y_adjust - minfirstline;
}

STATIC_INLINE int res_shift_from_window (int x)
{
    if (res_shift >= 0)
	return x >> res_shift;
    return x << -res_shift;
}

STATIC_INLINE int res_shift_from_amiga (int x)
{
    if (res_shift >= 0)
	return x >> res_shift;
    return x << -res_shift;
}

void notice_screen_contents_lost (void)
{
    picasso_redraw_necessary = 1;
    frame_redraw_necessary = 2;
}

static struct decision *dp_for_drawing;
static struct draw_info *dip_for_drawing;

/* Record DIW of the current line for use by centering code.  */
void record_diw_line (int plfstrt, int first, int last)
{
    if (last > max_diwstop)
	max_diwstop = last;
    if (first < min_diwstart) {
	min_diwstart = first;
/*
	if (plfstrt * 2 > min_diwstart)
	    min_diwstart = plfstrt * 2;
*/
    }
}

/*
 * Screen update macros/functions
 */

/* The important positions in the line: where do we start drawing the left border,
   where do we start drawing the playfield, where do we start drawing the right border.
   All of these are forced into the visible window (VISIBLE_LEFT_BORDER .. VISIBLE_RIGHT_BORDER).
   PLAYFIELD_START and PLAYFIELD_END are in window coordinates.  */
static int playfield_start, playfield_end;
static int real_playfield_start, real_playfield_end;

static int pixels_offset;
static int src_pixel;
/* How many pixels in window coordinates which are to the left of the left border.  */
static int unpainted;

/* Initialize the variables necessary for drawing a line.
 * This involves setting up start/stop positions and display window
 * borders.  */
static void pfield_init_linetoscr (void)
{
    /* First, get data fetch start/stop in DIW coordinates.  */
    int ddf_left = dp_for_drawing->plfleft * 2 + DIW_DDF_OFFSET;
    int ddf_right = dp_for_drawing->plfright * 2 + DIW_DDF_OFFSET;
    /* Compute datafetch start/stop in pixels; native display coordinates.  */
    int native_ddf_left = coord_hw_to_window_x (ddf_left);
    int native_ddf_right = coord_hw_to_window_x (ddf_right);

    int linetoscr_diw_start = dp_for_drawing->diwfirstword;
    int linetoscr_diw_end = dp_for_drawing->diwlastword;

    if (dip_for_drawing->nr_sprites == 0) {
	if (linetoscr_diw_start < native_ddf_left)
	    linetoscr_diw_start = native_ddf_left;
	if (linetoscr_diw_end > native_ddf_right)
	    linetoscr_diw_end = native_ddf_right;
    }

    /* Perverse cases happen. */
    if (linetoscr_diw_end < linetoscr_diw_start)
	linetoscr_diw_end = linetoscr_diw_start;

    playfield_start = linetoscr_diw_start;
    playfield_end = linetoscr_diw_end;

    if (playfield_start < visible_left_border)
	playfield_start = visible_left_border;
    if (playfield_start > visible_right_border)
	playfield_start = visible_right_border;
    if (playfield_end < visible_left_border)
	playfield_end = visible_left_border;
    if (playfield_end > visible_right_border)
	playfield_end = visible_right_border;

    real_playfield_end = playfield_end;
    real_playfield_start = playfield_start;
#ifdef AGA
    if (brdsprt && dip_for_drawing->nr_sprites) {
	int min = visible_right_border, max = visible_left_border, i;
	for (i = 0; i < dip_for_drawing->nr_sprites; i++) {
	    int x;
	    x = curr_sprite_entries[dip_for_drawing->first_sprite_entry + i].pos;
	    if (x < min) min = x;
	    x = curr_sprite_entries[dip_for_drawing->first_sprite_entry + i].max;
	    if (x > max) max = x;
	}
	min += (DIW_DDF_OFFSET - DISPLAY_LEFT_SHIFT) << (2 - lores_shift);
	max += (DIW_DDF_OFFSET - DISPLAY_LEFT_SHIFT) << (2 - lores_shift);
	if (min < playfield_start)
	    playfield_start = min;
	if (playfield_start < visible_left_border)
	    playfield_start = visible_left_border;
	if (max > playfield_end)
	    playfield_end = max;
	if (playfield_end > visible_right_border)
	    playfield_end = visible_right_border;
    }
#endif
    /* Now, compute some offsets.  */

    res_shift = lores_shift - bplres;
    ddf_left -= DISPLAY_LEFT_SHIFT;
    ddf_left <<= bplres;
    pixels_offset = MAX_PIXELS_PER_LINE - ddf_left;

    unpainted = visible_left_border < playfield_start ? 0 : visible_left_border - playfield_start;
    src_pixel = MAX_PIXELS_PER_LINE + res_shift_from_window (playfield_start - native_ddf_left + unpainted);

    if (dip_for_drawing->nr_sprites == 0)
	return;
    /* Must clear parts of apixels.  */
    if (linetoscr_diw_start < native_ddf_left) {
	int size = res_shift_from_window (native_ddf_left - linetoscr_diw_start);
	linetoscr_diw_start = native_ddf_left;
	memset (pixdata.apixels + MAX_PIXELS_PER_LINE - size, 0, size);
    }
    if (linetoscr_diw_end > native_ddf_right) {
	int pos = res_shift_from_window (native_ddf_right - native_ddf_left);
	int size = res_shift_from_window (linetoscr_diw_end - native_ddf_right);
	linetoscr_diw_start = native_ddf_left;
	memset (pixdata.apixels + MAX_PIXELS_PER_LINE + pos, 0, size);
    }
}

void drawing_adjust_mousepos(int *xp, int *yp)
{
}

static uae_u8 merge_2pixel8(uae_u8 p1, uae_u8 p2)
{
    return p1;
}
static uae_u16 merge_2pixel16(uae_u16 p1, uae_u16 p2)
{
    uae_u16 v = ((((p1 >> xredcolor_s) & xredcolor_m) + ((p2 >> xredcolor_s) & xredcolor_m)) / 2) << xredcolor_s;
    v |= ((((p1 >> xbluecolor_s) & xbluecolor_m) + ((p2 >> xbluecolor_s) & xbluecolor_m)) / 2) << xbluecolor_s;
    v |= ((((p1 >> xgreencolor_s) & xgreencolor_m) + ((p2 >> xgreencolor_s) & xgreencolor_m)) / 2) << xgreencolor_s;
    return v;
}
static uae_u32 merge_2pixel32(uae_u32 p1, uae_u32 p2)
{
    uae_u32 v = ((((p1 >> 16) & 0xff) + ((p2 >> 16) & 0xff)) / 2) << 16;
    v |= ((((p1 >> 8) & 0xff) + ((p2 >> 8) & 0xff)) / 2) << 8;
    v |= ((((p1 >> 0) & 0xff) + ((p2 >> 0) & 0xff)) / 2) << 0;
    return v;
}

static void fill_line_8 (uae_u8 *buf, unsigned int start, unsigned int stop)
{
    uae_u8 *b = (uae_u8 *)buf;
    unsigned int i;
    unsigned int rem = 0;
    xcolnr col = brdblank ? 0 : colors_for_drawing.acolors[0];
    while (((long)&b[start]) & 3) {
	b[start++] = (uae_u8)col;
	if (start == stop)
	   return;
    }
    if (((long)&b[stop]) & 3) {
	rem = ((long)&b[stop]) & 3;
	stop -= rem;
    }
    for (i = start; i < stop; i += 4) {
	uae_u32 *b2 = (uae_u32 *)&b[i];
	*b2 = col;
    }
    while (rem--)
	b[stop++] = (uae_u8) col;
}

static void fill_line_16 (uae_u8 *buf, unsigned int start, unsigned int stop)
{
    uae_u16 *b = (uae_u16 *)buf;
    unsigned int i;
    unsigned int rem = 0;
    xcolnr col = brdblank ? 0 : colors_for_drawing.acolors[0];
    if (((long)&b[start]) & 1)
	b[start++] = (uae_u16) col;
    if (start >= stop)
	return;
    if (((long)&b[stop]) & 1) {
	rem++;
	stop--;
    }
    for (i = start; i < stop; i += 2) {
	uae_u32 *b2 = (uae_u32 *)&b[i];
	*b2 = col;
    }
    if (rem)
	b[stop] = (uae_u16)col;
}

static void fill_line_32 (uae_u8 *buf, unsigned int start, unsigned int stop)
{
    uae_u32 *b = (uae_u32 *)buf;
    unsigned int i;
    xcolnr col = brdblank ? 0 : colors_for_drawing.acolors[0];
    for (i = start; i < stop; i++)
	b[i] = col;
}

STATIC_INLINE void fill_line (void)
{
    int shift;
    int nints, nrem;
    int *start;
    xcolnr val;

    shift = 0;
    if (gfxvidinfo.pixbytes == 2)
	shift = 1;
    if (gfxvidinfo.pixbytes == 4)
	shift = 2;

    nints = gfxvidinfo.width >> (2-shift);
    nrem = nints & 7;
    nints &= ~7;
    start = (int *)(((char *)xlinebuffer) + (visible_left_border << shift));
#ifdef AGA
    val = brdblank ? 0 : colors_for_drawing.acolors[0];
#else
    val = colors_for_drawing.acolors[0];
#endif
    for (; nints > 0; nints -= 8, start += 8) {
	*start = val;
	*(start+1) = val;
	*(start+2) = val;
	*(start+3) = val;
	*(start+4) = val;
	*(start+5) = val;
	*(start+6) = val;
	*(start+7) = val;
    }

    switch (nrem) {
     case 7:
	*start++ = val;
     case 6:
	*start++ = val;
     case 5:
	*start++ = val;
     case 4:
	*start++ = val;
     case 3:
	*start++ = val;
     case 2:
	*start++ = val;
     case 1:
	*start = val;
    }
}

static int linetoscr_double_offset;

#include "linetoscr.c"

static void pfield_do_linetoscr (int start, int stop)
{
    xlinecheck(start, stop);
#ifdef AGA
    if (currprefs.chipset_mask & CSMASK_AGA) {
	if (res_shift == 0)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_aga (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_aga (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_aga (src_pixel, start, stop); break;
	    }
	else if (res_shift > 0)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_stretch1_aga (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_stretch1_aga (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_stretch1_aga (src_pixel, start, stop); break;
	    }
	else if (res_shift < 0)
	    if (currprefs.gfx_lores_mode) {
		switch (gfxvidinfo.pixbytes) {
		case 1: src_pixel = linetoscr_8_shrink2_aga (src_pixel, start, stop); break;
		case 2: src_pixel = linetoscr_16_shrink2_aga (src_pixel, start, stop); break;
		case 4: src_pixel = linetoscr_32_shrink2_aga (src_pixel, start, stop); break;
		}
	    } else {
		switch (gfxvidinfo.pixbytes) {
		case 1: src_pixel = linetoscr_8_shrink1_aga (src_pixel, start, stop); break;
		case 2: src_pixel = linetoscr_16_shrink1_aga (src_pixel, start, stop); break;
		case 4: src_pixel = linetoscr_32_shrink1_aga (src_pixel, start, stop); break;
		}
	    }
    } else {
#endif
	if (res_shift == 0)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8 (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16 (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32 (src_pixel, start, stop); break;
	    }
	else if (res_shift > 0)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_stretch1 (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_stretch1 (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_stretch1 (src_pixel, start, stop); break;
	    }
	else if (res_shift < 0)
	    if (currprefs.gfx_lores_mode) {
		switch (gfxvidinfo.pixbytes) {
		case 1: src_pixel = linetoscr_8_shrink2 (src_pixel, start, stop); break;
		case 2: src_pixel = linetoscr_16_shrink2 (src_pixel, start, stop); break;
		case 4: src_pixel = linetoscr_32_shrink2 (src_pixel, start, stop); break;
		}
	    } else {
		switch (gfxvidinfo.pixbytes) {
		case 1: src_pixel = linetoscr_8_shrink1 (src_pixel, start, stop); break;
		case 2: src_pixel = linetoscr_16_shrink1 (src_pixel, start, stop); break;
		case 4: src_pixel = linetoscr_32_shrink1 (src_pixel, start, stop); break;
		}
	    }
#ifdef AGA
    }
#endif
}

static void pfield_do_fill_line (int start, int stop)
{
    xlinecheck(start, stop);
    switch (gfxvidinfo.pixbytes) {
    case 1: fill_line_8 (xlinebuffer, start, stop); break;
    case 2: fill_line_16 (xlinebuffer, start, stop); break;
    case 4: fill_line_32 (xlinebuffer, start, stop); break;
    }
}

static void dummy_worker (int start, int stop)
{
}

static unsigned int ham_lastcolor;

static int ham_decode_pixel;

/* Decode HAM in the invisible portion of the display (left of VISIBLE_LEFT_BORDER),
   but don't draw anything in.  This is done to prepare HAM_LASTCOLOR for later,
   when decode_ham runs.  */
static void init_ham_decoding (void)
{
    int unpainted_amiga = res_shift_from_window (unpainted);
    ham_decode_pixel = src_pixel;
    ham_lastcolor = color_reg_get (&colors_for_drawing, 0);

    if (! bplham || (bplplanecnt != 6 && ((currprefs.chipset_mask & CSMASK_AGA) == 0 || bplplanecnt != 8))) {
	if (unpainted_amiga > 0) {
	    int pv = pixdata.apixels[ham_decode_pixel + unpainted_amiga - 1];
#ifdef AGA
	    if (currprefs.chipset_mask & CSMASK_AGA)
		ham_lastcolor = colors_for_drawing.color_regs_aga[pv];
	    else
#endif
		ham_lastcolor = colors_for_drawing.color_regs_ecs[pv];
	}
#ifdef AGA
    } else if (currprefs.chipset_mask & CSMASK_AGA) {
	if (bplplanecnt == 8) { /* AGA mode HAM8 */
	    while (unpainted_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel++];
		switch (pv & 0x3) {
		case 0x0: ham_lastcolor = colors_for_drawing.color_regs_aga[pv >> 2]; break;
		case 0x1: ham_lastcolor &= 0xFFFF03; ham_lastcolor |= (pv & 0xFC); break;
		case 0x2: ham_lastcolor &= 0x03FFFF; ham_lastcolor |= (pv & 0xFC) << 16; break;
		case 0x3: ham_lastcolor &= 0xFF03FF; ham_lastcolor |= (pv & 0xFC) << 8; break;
		}
	    }
	} else if (bplplanecnt == 6) { /* AGA mode HAM6 */
	    while (unpainted_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel++];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_aga[pv]; break;
		case 0x10: ham_lastcolor &= 0xFFFF00; ham_lastcolor |= (pv & 0xF) << 4; break;
		case 0x20: ham_lastcolor &= 0x00FFFF; ham_lastcolor |= (pv & 0xF) << 20; break;
		case 0x30: ham_lastcolor &= 0xFF00FF; ham_lastcolor |= (pv & 0xF) << 12; break;
		}
	    }
	}
#endif
    } else {
	if (bplplanecnt == 6) { /* OCS/ECS mode HAM6 */
	    while (unpainted_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel++];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_ecs[pv]; break;
		case 0x10: ham_lastcolor &= 0xFF0; ham_lastcolor |= (pv & 0xF); break;
		case 0x20: ham_lastcolor &= 0x0FF; ham_lastcolor |= (pv & 0xF) << 8; break;
		case 0x30: ham_lastcolor &= 0xF0F; ham_lastcolor |= (pv & 0xF) << 4; break;
		}
	    }
	}
    }
}

static void decode_ham (int pix, int stoppos)
{
    int todraw_amiga = res_shift_from_window (stoppos - pix);

    if (! bplham || (bplplanecnt != 6 && ((currprefs.chipset_mask & CSMASK_AGA) == 0 || bplplanecnt != 8))) {
	while (todraw_amiga-- > 0) {
	    int pv = pixdata.apixels[ham_decode_pixel];
#ifdef AGA
	    if (currprefs.chipset_mask & CSMASK_AGA)
		ham_lastcolor = colors_for_drawing.color_regs_aga[pv];
	    else
#endif
		ham_lastcolor = colors_for_drawing.color_regs_ecs[pv];

	    ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	}
#ifdef AGA
    } else if (currprefs.chipset_mask & CSMASK_AGA) {
	if (bplplanecnt == 8) { /* AGA mode HAM8 */
	    while (todraw_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel];
		switch (pv & 0x3) {
		case 0x0: ham_lastcolor = colors_for_drawing.color_regs_aga[pv >> 2]; break;
		case 0x1: ham_lastcolor &= 0xFFFF03; ham_lastcolor |= (pv & 0xFC); break;
		case 0x2: ham_lastcolor &= 0x03FFFF; ham_lastcolor |= (pv & 0xFC) << 16; break;
		case 0x3: ham_lastcolor &= 0xFF03FF; ham_lastcolor |= (pv & 0xFC) << 8; break;
		}
		ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	    }
	} else if (bplplanecnt == 6) { /* AGA mode HAM6 */
	    while (todraw_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_aga[pv]; break;
		case 0x10: ham_lastcolor &= 0xFFFF00; ham_lastcolor |= (pv & 0xF) << 4; break;
		case 0x20: ham_lastcolor &= 0x00FFFF; ham_lastcolor |= (pv & 0xF) << 20; break;
		case 0x30: ham_lastcolor &= 0xFF00FF; ham_lastcolor |= (pv & 0xF) << 12; break;
		}
		ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	    }
	}
#endif
    } else {
	if (bplplanecnt == 6) { /* OCS/ECS mode HAM6 */
	    while (todraw_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_ecs[pv]; break;
		case 0x10: ham_lastcolor &= 0xFF0; ham_lastcolor |= (pv & 0xF); break;
		case 0x20: ham_lastcolor &= 0x0FF; ham_lastcolor |= (pv & 0xF) << 8; break;
		case 0x30: ham_lastcolor &= 0xF0F; ham_lastcolor |= (pv & 0xF) << 4; break;
		}
		ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	    }
	}
    }
}

static void gen_pfield_tables (void)
{
    int i;

    /* For now, the AGA stuff is broken in the dual playfield case. We encode
     * sprites in dpf mode by ORing the pixel value with 0x80. To make dual
     * playfield rendering easy, the lookup tables contain are made linear for
     * values >= 128. That only works for OCS/ECS, though. */

    for (i = 0; i < 256; i++) {
	int plane1 = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4) | ((i >> 3) & 8);
	int plane2 = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4) | ((i >> 4) & 8);

	dblpf_2nd1[i] = plane1 == 0 ? (plane2 == 0 ? 0 : 2) : 1;
	dblpf_2nd2[i] = plane2 == 0 ? (plane1 == 0 ? 0 : 1) : 2;

#ifdef AGA
	dblpf_ind1_aga[i] = plane1 == 0 ? plane2 : plane1;
	dblpf_ind2_aga[i] = plane2 == 0 ? plane1 : plane2;
#endif

	dblpf_ms1[i] = plane1 == 0 ? (plane2 == 0 ? 16 : 8) : 0;
	dblpf_ms2[i] = plane2 == 0 ? (plane1 == 0 ? 16 : 0) : 8;
	dblpf_ms[i] = i == 0 ? 16 : 8;
	if (plane2 > 0)
	    plane2 += 8;
	dblpf_ind1[i] = i >= 128 ? i & 0x7F : (plane1 == 0 ? plane2 : plane1);
	dblpf_ind2[i] = i >= 128 ? i & 0x7F : (plane2 == 0 ? plane1 : plane2);

	sprite_offs[i] = (i & 15) ? 0 : 2;

	clxtab[i] = ((((i & 3) && (i & 12)) << 9)
		     | (((i & 3) && (i & 48)) << 10)
		     | (((i & 3) && (i & 192)) << 11)
		     | (((i & 12) && (i & 48)) << 12)
		     | (((i & 12) && (i & 192)) << 13)
		     | (((i & 48) && (i & 192)) << 14));

    }
}

#define SPRITE_DEBUG 0

/* When looking at this function and the ones that inline it, bear in mind
   what an optimizing compiler will do with this code.  All callers of this
   function only pass in constant arguments (except for E).  This means
   that many of the if statements will go away completely after inlining.  */
STATIC_INLINE void draw_sprites_1 (struct sprite_entry *e, int ham, int dualpf,
    int doubling, int skip, int has_attach, int aga)
{
    int *shift_lookup = dualpf ? (bpldualpfpri ? dblpf_ms2 : dblpf_ms1) : dblpf_ms;
    uae_u16 *buf = spixels + e->first_pixel;
    uae_u8 *stbuf = spixstate.bytes + e->first_pixel;
    int pos, window_pos;
#ifdef AGA
    uae_u8 xor_val = (uae_u8)(dp_for_drawing->bplcon4 >> 8);
#endif

    buf -= e->pos;
    stbuf -= e->pos;

    window_pos = e->pos + ((DIW_DDF_OFFSET - DISPLAY_LEFT_SHIFT) << (aga ? 1 : 0));
    if (skip)
	window_pos >>= 1;
    else if (doubling)
	window_pos <<= 1;
    window_pos += pixels_offset;
    for (pos = e->pos; pos < e->max; pos += 1 << skip) {
	int maskshift, plfmask;
	unsigned int v = buf[pos];

	/* The value in the shift lookup table is _half_ the shift count we
	   need.  This is because we can't shift 32 bits at once (undefined
	   behaviour in C).  */
	maskshift = shift_lookup[pixdata.apixels[window_pos]];
	plfmask = (plf_sprite_mask >> maskshift) >> maskshift;
	v &= ~plfmask;
	if (v != 0 || SPRITE_DEBUG) {
	    unsigned int vlo, vhi, col;
	    unsigned int v1 = v & 255;
	    /* OFFS determines the sprite pair with the highest priority that has
	       any bits set.  E.g. if we have 0xFF00 in the buffer, we have sprite
	       pairs 01 and 23 cleared, and pairs 45 and 67 set, so OFFS will
	       have a value of 4.
	       2 * OFFS is the bit number in V of the sprite pair, and it also
	       happens to be the color offset for that pair.  */
	    int offs;
	    if (v1 == 0)
		offs = 4 + sprite_offs[v >> 8];
	    else
		offs = sprite_offs[v1];

	    /* Shift highest priority sprite pair down to bit zero.  */
	    v >>= offs * 2;
	    v &= 15;
#if SPRITE_DEBUG > 0
	    v ^= 8;
#endif

	    if (has_attach && (stbuf[pos] & (3 << offs))) {
		col = v;
		if (aga)
		    col += sbasecol[1];
		else
		    col += 16;
	    } else {
		/* This sequence computes the correct color value.  We have to select
		   either the lower-numbered or the higher-numbered sprite in the pair.
		   We have to select the high one if the low one has all bits zero.
		   If the lower-numbered sprite has any bits nonzero, (VLO - 1) is in
		   the range of 0..2, and with the mask and shift, VHI will be zero.
		   If the lower-numbered sprite is zero, (VLO - 1) is a mask of
		   0xFFFFFFFF, and we select the bits of the higher numbered sprite
		   in VHI.
		   This is _probably_ more efficient than doing it with branches.  */
		vlo = v & 3;
		vhi = (v & (vlo - 1)) >> 2;
		col = (vlo | vhi);
		if (aga) {
		    if (vhi > 0)
			col += sbasecol[1];
		    else
			col += sbasecol[0];
		} else {
		    col += 16;
		}
		col += (offs * 2);
	    }
	    if (dualpf) {
#ifdef AGA
		if (aga) {
		    spriteagadpfpixels[window_pos] = col;
		    if (doubling)
			spriteagadpfpixels[window_pos + 1] = col;
		} else {
#endif
		    col += 128;
		    if (doubling)
			pixdata.apixels_w[window_pos >> 1] = col | (col << 8);
		    else
			pixdata.apixels[window_pos] = col;
#ifdef AGA
		}
#endif
	    } else if (ham) {
		col = color_reg_get (&colors_for_drawing, col);
#ifdef AGA
		if (aga)
		    col ^= xor_val;
#endif
		ham_linebuf[window_pos] = col;
		if (doubling)
		    ham_linebuf[window_pos + 1] = col;
	    } else {
#ifdef AGA
		if (aga)
		    col ^= xor_val;
#endif
		if (doubling)
		    pixdata.apixels_w[window_pos >> 1] = col | (col << 8);
		else
		    pixdata.apixels[window_pos] = col;
	    }
	}
	window_pos += 1 << doubling;
    }
}

/* See comments above.  Do not touch if you don't know what's going on.
 * (We do _not_ want the following to be inlined themselves).  */
/* lores bitplane, lores sprites */
static void NOINLINE draw_sprites_normal_sp_lo_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 0, 0, 0, 0); }
static void NOINLINE draw_sprites_normal_dp_lo_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 0, 0, 0, 0); }
static void NOINLINE draw_sprites_ham_sp_lo_nat (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 0, 0, 0, 0); }
static void NOINLINE draw_sprites_normal_sp_lo_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 0, 0, 1, 0); }
static void NOINLINE draw_sprites_normal_dp_lo_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 0, 0, 1, 0); }
static void NOINLINE draw_sprites_ham_sp_lo_at (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 0, 0, 1, 0); }
/* hires bitplane, lores sprites */
static void NOINLINE draw_sprites_normal_sp_hi_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 1, 0, 0, 0); }
static void NOINLINE draw_sprites_normal_dp_hi_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 1, 0, 0, 0); }
static void NOINLINE draw_sprites_ham_sp_hi_nat (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 1, 0, 0, 0); }
static void NOINLINE draw_sprites_normal_sp_hi_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 1, 0, 1, 0); }
static void NOINLINE draw_sprites_normal_dp_hi_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 1, 0, 1, 0); }
static void NOINLINE draw_sprites_ham_sp_hi_at (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 1, 0, 1, 0); }

#ifdef AGA
/* not very optimized */
STATIC_INLINE void draw_sprites_aga (struct sprite_entry *e)
{
    int diff = RES_HIRES - bplres;
    if (diff > 0)
	draw_sprites_1 (e, dp_for_drawing->ham_seen, bpldualpf, 0, diff, e->has_attached, 1);
    else
	draw_sprites_1 (e, dp_for_drawing->ham_seen, bpldualpf, -diff, 0, e->has_attached, 1);
}
#endif

STATIC_INLINE void draw_sprites_ecs (struct sprite_entry *e)
{
    if (e->has_attached)
	if (bplres == 1)
	    if (dp_for_drawing->ham_seen)
		draw_sprites_ham_sp_hi_at (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_hi_at (e);
		else
		    draw_sprites_normal_sp_hi_at (e);
	else
	    if (dp_for_drawing->ham_seen)
		draw_sprites_ham_sp_lo_at (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_lo_at (e);
		else
		    draw_sprites_normal_sp_lo_at (e);
    else
	if (bplres == 1)
	    if (dp_for_drawing->ham_seen)
		draw_sprites_ham_sp_hi_nat (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_hi_nat (e);
		else
		    draw_sprites_normal_sp_hi_nat (e);
	else
	    if (dp_for_drawing->ham_seen)
		draw_sprites_ham_sp_lo_nat (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_lo_nat (e);
		else
		    draw_sprites_normal_sp_lo_nat (e);
}

#ifdef AGA
/* clear possible bitplane data outside DIW area */
static void clear_bitplane_border_aga (void)
{
    int len, shift = lores_shift - bplres;
    uae_u8 v = 0;
    if (shift < 0) {
	shift = -shift;
	len = (real_playfield_start - playfield_start) << shift;
	memset (pixdata.apixels + pixels_offset + (playfield_start << shift), v, len);
	len = (playfield_end - real_playfield_end) << shift;
	memset (pixdata.apixels + pixels_offset + (real_playfield_end << shift), v, len);
    } else {
	len = (real_playfield_start - playfield_start) >> shift;
	memset (pixdata.apixels + pixels_offset + (playfield_start >> shift), v, len);
	len = (playfield_end - real_playfield_end) >> shift;
	memset (pixdata.apixels + pixels_offset + (real_playfield_end >> shift), v, len);
    }
}
#endif

/* emulate OCS/ECS only undocumented "SWIV" hardware feature */
static void weird_bitplane_fix (void)
{
    int i, shift = lores_shift - bplres;

    if (shift < 0) {
	shift = -shift;
	for (i = playfield_start << lores_shift; i < playfield_end << lores_shift; i++) {
	    if (pixdata.apixels[pixels_offset + i] > 16)
		pixdata.apixels[pixels_offset + i] = 16;
	}
    } else {
	for (i = playfield_start >> lores_shift; i < playfield_end >> lores_shift; i++) {
	    if (pixdata.apixels[pixels_offset + i] > 16)
		pixdata.apixels[pixels_offset + i] = 16;
	}
    }
}

#define MERGE(a,b,mask,shift) do {\
    uae_u32 tmp = mask & (a ^ (b >> shift)); \
    a ^= tmp; \
    b ^= (tmp << shift); \
} while (0)

#define GETLONG(P) (*(uae_u32 *)P)

/* We use the compiler's inlining ability to ensure that PLANES is in effect a compile time
   constant.  That will cause some unnecessary code to be optimized away.
   Don't touch this if you don't know what you are doing.  */
STATIC_INLINE void pfield_doline_1 (uae_u32 *pixels, int wordcount, int planes)
{
    while (wordcount-- > 0) {
	uae_u32 b0, b1, b2, b3, b4, b5, b6, b7;

	b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0;
	switch (planes) {
#ifdef AGA
	case 8: b0 = GETLONG (real_bplpt[7]); real_bplpt[7] += 4;
	case 7: b1 = GETLONG (real_bplpt[6]); real_bplpt[6] += 4;
#endif
	case 6: b2 = GETLONG (real_bplpt[5]); real_bplpt[5] += 4;
	case 5: b3 = GETLONG (real_bplpt[4]); real_bplpt[4] += 4;
	case 4: b4 = GETLONG (real_bplpt[3]); real_bplpt[3] += 4;
	case 3: b5 = GETLONG (real_bplpt[2]); real_bplpt[2] += 4;
	case 2: b6 = GETLONG (real_bplpt[1]); real_bplpt[1] += 4;
	case 1: b7 = GETLONG (real_bplpt[0]); real_bplpt[0] += 4;
	}

	MERGE (b0, b1, 0x55555555, 1);
	MERGE (b2, b3, 0x55555555, 1);
	MERGE (b4, b5, 0x55555555, 1);
	MERGE (b6, b7, 0x55555555, 1);

	MERGE (b0, b2, 0x33333333, 2);
	MERGE (b1, b3, 0x33333333, 2);
	MERGE (b4, b6, 0x33333333, 2);
	MERGE (b5, b7, 0x33333333, 2);

	MERGE (b0, b4, 0x0f0f0f0f, 4);
	MERGE (b1, b5, 0x0f0f0f0f, 4);
	MERGE (b2, b6, 0x0f0f0f0f, 4);
	MERGE (b3, b7, 0x0f0f0f0f, 4);

	MERGE (b0, b1, 0x00ff00ff, 8);
	MERGE (b2, b3, 0x00ff00ff, 8);
	MERGE (b4, b5, 0x00ff00ff, 8);
	MERGE (b6, b7, 0x00ff00ff, 8);

	MERGE (b0, b2, 0x0000ffff, 16);
	do_put_mem_long (pixels, b0);
	do_put_mem_long (pixels + 4, b2);
	MERGE (b1, b3, 0x0000ffff, 16);
	do_put_mem_long (pixels + 2, b1);
	do_put_mem_long (pixels + 6, b3);
	MERGE (b4, b6, 0x0000ffff, 16);
	do_put_mem_long (pixels + 1, b4);
	do_put_mem_long (pixels + 5, b6);
	MERGE (b5, b7, 0x0000ffff, 16);
	do_put_mem_long (pixels + 3, b5);
	do_put_mem_long (pixels + 7, b7);
	pixels += 8;
    }
}

/* See above for comments on inlining.  These functions should _not_
   be inlined themselves.  */
static void NOINLINE pfield_doline_n1 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 1); }
static void NOINLINE pfield_doline_n2 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 2); }
static void NOINLINE pfield_doline_n3 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 3); }
static void NOINLINE pfield_doline_n4 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 4); }
static void NOINLINE pfield_doline_n5 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 5); }
static void NOINLINE pfield_doline_n6 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 6); }
#ifdef AGA
static void NOINLINE pfield_doline_n7 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 7); }
static void NOINLINE pfield_doline_n8 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 8); }
#endif

static void pfield_doline (int lineno)
{
    int wordcount = dp_for_drawing->plflinelen;
    uae_u32 *data = pixdata.apixels_l + MAX_PIXELS_PER_LINE / 4;

#ifdef SMART_UPDATE
#define DATA_POINTER(n) (line_data[lineno] + (n)*MAX_WORDS_PER_LINE*2)
    real_bplpt[0] = DATA_POINTER (0);
    real_bplpt[1] = DATA_POINTER (1);
    real_bplpt[2] = DATA_POINTER (2);
    real_bplpt[3] = DATA_POINTER (3);
    real_bplpt[4] = DATA_POINTER (4);
    real_bplpt[5] = DATA_POINTER (5);
#ifdef AGA
    real_bplpt[6] = DATA_POINTER (6);
    real_bplpt[7] = DATA_POINTER (7);
#endif
#endif

    switch (bplplanecnt) {
    default: break;
    case 0: memset (data, 0, wordcount * 32); break;
    case 1: pfield_doline_n1 (data, wordcount); break;
    case 2: pfield_doline_n2 (data, wordcount); break;
    case 3: pfield_doline_n3 (data, wordcount); break;
    case 4: pfield_doline_n4 (data, wordcount); break;
    case 5: pfield_doline_n5 (data, wordcount); break;
    case 6: pfield_doline_n6 (data, wordcount); break;
#ifdef AGA
    case 7: pfield_doline_n7 (data, wordcount); break;
    case 8: pfield_doline_n8 (data, wordcount); break;
#endif
    }
}

void init_row_map (void)
{
    int i, j;
    if (gfxvidinfo.height > MAX_VIDHEIGHT) {
	write_log ("Resolution too high, aborting\n");
	abort ();
    }
    j = 0;
    for (i = gfxvidinfo.height; i < MAX_VIDHEIGHT + 1; i++)
	row_map[i] = row_tmp;
    for (i = 0; i < gfxvidinfo.height; i++, j += gfxvidinfo.rowbytes)
	row_map[i] = gfxvidinfo.bufmem + j;
}

static void init_aspect_maps (void)
{
    int i, maxl;
    double native_lines_per_amiga_line;

    if (gfxvidinfo.height == 0)
	/* Do nothing if the gfx driver hasn't initialized the screen yet */
	return;

    if (native2amiga_line_map)
	free (native2amiga_line_map);
    if (amiga2aspect_line_map)
	free (amiga2aspect_line_map);

    /* At least for this array the +1 is necessary. */
    amiga2aspect_line_map = malloc (sizeof (int) * (MAXVPOS + 1) * 2 + 1);
    native2amiga_line_map = malloc (sizeof (int) * gfxvidinfo.height);

    if (currprefs.gfx_correct_aspect)
	native_lines_per_amiga_line = ((double)gfxvidinfo.height
				       * (currprefs.gfx_lores ? 320 : 640)
				       / (currprefs.gfx_linedbl ? 512 : 256)
				       / gfxvidinfo.width);
    else
	native_lines_per_amiga_line = 1;

    maxl = (MAXVPOS + 1) * (currprefs.gfx_linedbl ? 2 : 1);
    min_ypos_for_screen = minfirstline << (currprefs.gfx_linedbl ? 1 : 0);
    max_drawn_amiga_line = -1;
    for (i = 0; i < maxl; i++) {
	int v = (int) ((i - min_ypos_for_screen) * native_lines_per_amiga_line);
	if (v >= gfxvidinfo.height && max_drawn_amiga_line == -1)
	    max_drawn_amiga_line = i - min_ypos_for_screen;
	if (i < min_ypos_for_screen || v >= gfxvidinfo.height)
	    v = -1;
	amiga2aspect_line_map[i] = v;
    }
    if (currprefs.gfx_linedbl)
	max_drawn_amiga_line >>= 1;

    if (currprefs.gfx_ycenter && !(currprefs.gfx_correct_aspect)) {
	/* @@@ verify maxvpos vs. MAXVPOS */
	extra_y_adjust = (gfxvidinfo.height - (maxvpos_max << (currprefs.gfx_linedbl ? 1 : 0))) >> 1;
	if (extra_y_adjust < 0)
	    extra_y_adjust = 0;
    }

    for (i = 0; i < gfxvidinfo.height; i++)
	native2amiga_line_map[i] = -1;

    if (native_lines_per_amiga_line < 1) {
	/* Must omit drawing some lines. */
	for (i = maxl - 1; i > min_ypos_for_screen; i--) {
	    if (amiga2aspect_line_map[i] == amiga2aspect_line_map[i-1]) {
		if (currprefs.gfx_linedbl && (i & 1) == 0 && amiga2aspect_line_map[i+1] != -1) {
		    /* If only the first line of a line pair would be omitted,
		     * omit the second one instead to avoid problems with line
		     * doubling. */
		    amiga2aspect_line_map[i] = amiga2aspect_line_map[i+1];
		    amiga2aspect_line_map[i+1] = -1;
		} else
		    amiga2aspect_line_map[i] = -1;
	    }
	}
    }

    for (i = maxl-1; i >= min_ypos_for_screen; i--) {
	int j;
	if (amiga2aspect_line_map[i] == -1)
	    continue;
	for (j = amiga2aspect_line_map[i]; j < gfxvidinfo.height && native2amiga_line_map[j] == -1; j++)
	    native2amiga_line_map[j] = i >> (currprefs.gfx_linedbl ? 1 : 0);
    }
}

/*
 * A raster line has been built in the graphics buffer. Tell the graphics code
 * to do anything necessary to display it.
 */
static void do_flush_line_1 (int lineno)
{
    if (lineno < first_drawn_line)
	first_drawn_line = lineno;
    if (lineno > last_drawn_line)
	last_drawn_line = lineno;

    if (gfxvidinfo.maxblocklines == 0)
	flush_line (lineno);
    else {
	if ((last_block_line + 2) < lineno) {
	    if (first_block_line != NO_BLOCK)
		flush_block (first_block_line, last_block_line);
	    first_block_line = lineno;
	}
	last_block_line = lineno;
	if (last_block_line - first_block_line >= gfxvidinfo.maxblocklines) {
	    flush_block (first_block_line, last_block_line);
	    first_block_line = last_block_line = NO_BLOCK;
	}
    }
}

STATIC_INLINE void do_flush_line (int lineno)
{
    do_flush_line_1 (lineno);
}

/*
 * One drawing frame has been finished. Tell the graphics code about it.
 * Note that the actual flush_screen() call is a no-op for all reasonable
 * systems.
 */

STATIC_INLINE void do_flush_screen (int start, int stop)
{
    /* TODO: this flush operation is executed outside locked state!
       Should be corrected.
       (sjo 26.9.99) */

    xlinecheck(start, stop);
    if (gfxvidinfo.maxblocklines != 0 && first_block_line != NO_BLOCK) {
	flush_block (first_block_line, last_block_line);
    }
    unlockscr ();
    if (start <= stop)
	flush_screen (start, stop);
    else if (currprefs.gfx_afullscreen && currprefs.gfx_vsync)
	flush_screen (0, 0); /* vsync mode */
}

static int drawing_color_matches;
static enum { color_match_acolors, color_match_full } color_match_type;

/* Set up colors_for_drawing to the state at the beginning of the currently drawn
   line.  Try to avoid copying color tables around whenever possible.  */
static void adjust_drawing_colors (int ctable, int need_full)
{
    if (drawing_color_matches != ctable) {
	if (need_full) {
	    color_reg_cpy (&colors_for_drawing, curr_color_tables + ctable);
	    color_match_type = color_match_full;
	} else {
	    memcpy (colors_for_drawing.acolors, curr_color_tables[ctable].acolors,
		sizeof colors_for_drawing.acolors);
	    color_match_type = color_match_acolors;
	}
	drawing_color_matches = ctable;
    } else if (need_full && color_match_type != color_match_full) {
	color_reg_cpy (&colors_for_drawing, &curr_color_tables[ctable]);
	color_match_type = color_match_full;
    }
}

/* We only save hardware registers during the hardware frame. Now, when
 * drawing the frame, we expand the data into a slightly more useful
 * form. */
static void pfield_expand_dp_bplcon (void)
{
    int brdblank_2;

    bplres = dp_for_drawing->bplres;
    bplplanecnt = dp_for_drawing->nr_planes;
    bplham = dp_for_drawing->ham_seen;

    if (bplres > 0)
	frame_res = 1;
    if (bplres > 0)
	can_use_lores = 0;
    if (currprefs.chipset_mask & CSMASK_AGA) {
	/* The KILLEHB bit exists in ECS, but is apparently meant for Genlock
	 * stuff, and it's set by some demos (e.g. Andromeda Seven Seas) */
	bplehb = ((dp_for_drawing->bplcon0 & 0x7010) == 0x6000 && !(dp_for_drawing->bplcon2 & 0x200));
    } else {
	bplehb = (dp_for_drawing->bplcon0 & 0xFC00) == 0x6000;
    }
    plf1pri = dp_for_drawing->bplcon2 & 7;
    plf2pri = (dp_for_drawing->bplcon2 >> 3) & 7;
    plf_sprite_mask = 0xFFFF0000 << (4 * plf2pri);
    plf_sprite_mask |= (0xFFFF << (4 * plf1pri)) & 0xFFFF;
    bpldualpf = (dp_for_drawing->bplcon0 & 0x400) == 0x400;
    bpldualpfpri = (dp_for_drawing->bplcon2 & 0x40) == 0x40;
    brdblank_2 = (currprefs.chipset_mask & CSMASK_ECS_DENISE) && (dp_for_drawing->bplcon0 & 1) && (dp_for_drawing->bplcon3 & 0x20);
    if (brdblank_2 != brdblank)
	brdblank_changed = 1;
    brdblank = brdblank_2;
#ifdef AGA
    bpldualpf2of = (dp_for_drawing->bplcon3 >> 10) & 7;
    sbasecol[0] = ((dp_for_drawing->bplcon4 >> 4) & 15) << 4;
    sbasecol[1] = ((dp_for_drawing->bplcon4 >> 0) & 15) << 4;
    brdsprt = !brdblank && (currprefs.chipset_mask & CSMASK_AGA) && (dp_for_drawing->bplcon0 & 1) && (dp_for_drawing->bplcon3 & 0x02);
#endif
}
static void pfield_expand_dp_bplcon2(int regno, int v)
{
    regno -= 0x1000;
    switch (regno)
    {
        case 0x100:
        dp_for_drawing->bplcon0 = v;
        dp_for_drawing->bplres = GET_RES(v);
        dp_for_drawing->nr_planes = GET_PLANES(v);
	dp_for_drawing->ham_seen = !! (v & 0x800);
        break;
        case 0x104:
        dp_for_drawing->bplcon2 = v;
        break;
#ifdef AGA
	case 0x106:
        dp_for_drawing->bplcon3 = v;
        break;
        case 0x108:
        dp_for_drawing->bplcon4 = v;
        break;
#endif
    }
    pfield_expand_dp_bplcon();
    res_shift = lores_shift - bplres;
}

STATIC_INLINE void do_color_changes (line_draw_func worker_border, line_draw_func worker_pfield)
{
    int i;
    int lastpos = visible_left_border;
    int endpos = visible_left_border + gfxvidinfo.width;

    for (i = dip_for_drawing->first_color_change; i <= dip_for_drawing->last_color_change; i++) {
	int regno = curr_color_changes[i].regno;
	unsigned int value = curr_color_changes[i].value;
	int nextpos, nextpos_in_range;
	if (i == dip_for_drawing->last_color_change)
	    nextpos = endpos;
	else
	    nextpos = coord_hw_to_window_x (curr_color_changes[i].linepos * 2);

	nextpos_in_range = nextpos;
	if (nextpos > endpos)
	    nextpos_in_range = endpos;

	if (nextpos_in_range > lastpos) {
	    if (lastpos < playfield_start) {
		int t = nextpos_in_range <= playfield_start ? nextpos_in_range : playfield_start;
		(*worker_border) (lastpos, t);
		lastpos = t;
	    }
	}
	if (nextpos_in_range > lastpos) {
	    if (lastpos >= playfield_start && lastpos < playfield_end) {
		int t = nextpos_in_range <= playfield_end ? nextpos_in_range : playfield_end;
		(*worker_pfield) (lastpos, t);
		lastpos = t;
	    }
	}
	if (nextpos_in_range > lastpos) {
	    if (lastpos >= playfield_end)
		(*worker_border) (lastpos, nextpos_in_range);
	    lastpos = nextpos_in_range;
	}
	if (i != dip_for_drawing->last_color_change) {
	    if (regno >= 0x1000) {
		pfield_expand_dp_bplcon2(regno, value);
	    } else {
		color_reg_set (&colors_for_drawing, regno, value);
		colors_for_drawing.acolors[regno] = getxcolor (value);
	    }
	}
	if (lastpos >= endpos)
	    break;
    }
}

/* Move color changes in horizontal cycles 0 to HBLANK_OFFSET - 1 to previous line.
 * Cycles 0 to HBLANK_OFFSET are visible in right border on real Amigas.
 */
static void mungedip(int lineno, int next)
{
    int i = dip_for_drawing->last_color_change;
    struct draw_info *dip_for_drawing_next = curr_drawinfo + (lineno + next);

    if (dip_for_drawing_next->first_color_change == 0)
	dip_for_drawing_next = curr_drawinfo + (lineno + 2);
    while (i < dip_for_drawing_next->last_color_change) {
	int regno = curr_color_changes[i].regno;
	int hpos = curr_color_changes[i].linepos;
	if (regno < 0)
	    break;
	if (hpos >= HBLANK_OFFSET)
	    break;
	curr_color_changes[i].linepos += maxhpos + 2;
	dip_for_drawing->last_color_change++;
	dip_for_drawing->nr_color_changes++;
	dip_for_drawing_next->first_color_change++;
	dip_for_drawing_next->nr_color_changes--;
	i++;
    }
}

enum double_how {
    dh_buf,
    dh_line,
    dh_emerg
};

static void pfield_draw_line (int lineno, int gfx_ypos, int follow_ypos)
{
    static int warned = 0;
    int border = 0;
    int do_double = 0;
    enum double_how dh;

    dp_for_drawing = line_decisions + lineno;
    dip_for_drawing = curr_drawinfo + lineno;
    mungedip(lineno, (dp_for_drawing->bplcon0 & 4) ? 2 : 1);
    switch (linestate[lineno]) {
    case LINE_REMEMBERED_AS_PREVIOUS:
	if (!warned)
	    write_log ("Shouldn't get here... this is a bug.\n"), warned++;
	return;

    case LINE_BLACK:
	linestate[lineno] = LINE_REMEMBERED_AS_BLACK;
	border = 2;
	break;

    case LINE_REMEMBERED_AS_BLACK:
	return;

    case LINE_AS_PREVIOUS:
	dp_for_drawing--;
	dip_for_drawing--;
	linestate[lineno] = LINE_DONE_AS_PREVIOUS;
	if (!dp_for_drawing->valid)
	    return;
	if (dp_for_drawing->plfleft == -1)
	    border = 1;
	break;

    case LINE_DONE_AS_PREVIOUS:
	/* fall through */
    case LINE_DONE:
	return;

    case LINE_DECIDED_DOUBLE:
	if (follow_ypos != -1) {
	    do_double = 1;
	    linetoscr_double_offset = gfxvidinfo.rowbytes * (follow_ypos - gfx_ypos);
	    linestate[lineno + 1] = LINE_DONE_AS_PREVIOUS;
	}

	/* fall through */
    default:
	if (dp_for_drawing->plfleft == -1)
	    border = 1;
	linestate[lineno] = LINE_DONE;
	break;
    }

    dh = dh_line;
    xlinebuffer = gfxvidinfo.linemem;
    if (xlinebuffer == 0 && do_double
	&& (border == 0 || dip_for_drawing->nr_color_changes > 0))
	xlinebuffer = gfxvidinfo.emergmem, dh = dh_emerg;
    if (xlinebuffer == 0)
	xlinebuffer = row_map[gfx_ypos], dh = dh_buf;
    xlinebuffer -= linetoscr_x_adjust_bytes;

    if (border == 0) {

	pfield_expand_dp_bplcon ();

	if (bplres == RES_LORES && ! currprefs.gfx_lores)
	    currprefs.gfx_lores = 2;

	pfield_init_linetoscr ();
	pfield_doline (lineno);

	adjust_drawing_colors (dp_for_drawing->ctable, dp_for_drawing->ham_seen || bplehb);

	/* The problem is that we must call decode_ham() BEFORE we do the
	   sprites. */
	if (! border && dp_for_drawing->ham_seen) {
	    init_ham_decoding ();
	    if (dip_for_drawing->nr_color_changes == 0) {
		/* The easy case: need to do HAM decoding only once for the
		 * full line. */
		decode_ham (visible_left_border, visible_right_border);
	    } else /* Argh. */ {
		do_color_changes (dummy_worker, decode_ham);
		adjust_drawing_colors (dp_for_drawing->ctable, dp_for_drawing->ham_seen || bplehb);
	    }
	    bplham = dp_for_drawing->ham_at_start;
	}
	if (plf2pri > 5 && bplplanecnt == 5 && !(currprefs.chipset_mask & CSMASK_AGA))
	    weird_bitplane_fix ();

	{
	    if (dip_for_drawing->nr_sprites) {
		int i;
#ifdef AGA
		if (brdsprt)
		    clear_bitplane_border_aga ();
#endif
		for (i = 0; i < dip_for_drawing->nr_sprites; i++) {
#ifdef AGA
		    if (currprefs.chipset_mask & CSMASK_AGA)
			draw_sprites_aga (curr_sprite_entries + dip_for_drawing->first_sprite_entry + i);
		    else
#endif
			draw_sprites_ecs (curr_sprite_entries + dip_for_drawing->first_sprite_entry + i);
		}
	    }
	}

	do_color_changes (pfield_do_fill_line, pfield_do_linetoscr);

	if (dh == dh_emerg)
	    memcpy (row_map[gfx_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.pixbytes * gfxvidinfo.width);

	do_flush_line (gfx_ypos);
	if (do_double) {
	    if (dh == dh_emerg)
		memcpy (row_map[follow_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.pixbytes * gfxvidinfo.width);
	    else if (dh == dh_buf)
		memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.pixbytes * gfxvidinfo.width);
	    do_flush_line (follow_ypos);
	}
	if (currprefs.gfx_lores == 2)
	    currprefs.gfx_lores = 0;

    } else if (border == 1) {
	int dosprites = 0;

	adjust_drawing_colors (dp_for_drawing->ctable, 0);

#ifdef AGA /* this makes things complex.. */
	if (brdsprt && (currprefs.chipset_mask & CSMASK_AGA) && dip_for_drawing->nr_sprites > 0) {
	    dosprites = 1;
	    pfield_expand_dp_bplcon ();
	    pfield_init_linetoscr ();
	    memset (pixdata.apixels + MAX_PIXELS_PER_LINE, brdblank ? 0 : colors_for_drawing.acolors[0], MAX_PIXELS_PER_LINE);
	}
#endif

	if (!dosprites && dip_for_drawing->nr_color_changes == 0) {
	    fill_line ();
	    do_flush_line (gfx_ypos);

	    if (do_double) {
		if (dh == dh_buf) {
		    xlinebuffer = row_map[follow_ypos] - linetoscr_x_adjust_bytes;
		    fill_line ();
		}
		/* If dh == dh_line, do_flush_line will re-use the rendered line
		 * from linemem.  */
		do_flush_line (follow_ypos);
	    }
	    return;
	}


	if (dosprites) {

	    int i;
	    for (i = 0; i < dip_for_drawing->nr_sprites; i++) {
		draw_sprites_aga (curr_sprite_entries + dip_for_drawing->first_sprite_entry + i);
	    }
	    do_color_changes (pfield_do_fill_line, pfield_do_linetoscr);

	} else {

	    playfield_start = visible_right_border;
	    playfield_end = visible_right_border;
	    do_color_changes (pfield_do_fill_line, pfield_do_fill_line);

	}

	if (dh == dh_emerg)
	    memcpy (row_map[gfx_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.pixbytes * gfxvidinfo.width);

	do_flush_line (gfx_ypos);
	if (do_double) {
	    if (dh == dh_emerg)
		memcpy (row_map[follow_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.pixbytes * gfxvidinfo.width);
	    else if (dh == dh_buf)
		memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.pixbytes * gfxvidinfo.width);
	    do_flush_line (follow_ypos);
	}

    } else {

	xcolnr tmp = colors_for_drawing.acolors[0];
	colors_for_drawing.acolors[0] = getxcolor (0);
	fill_line ();
	do_flush_line (gfx_ypos);
	colors_for_drawing.acolors[0] = tmp;

    }
}

static void center_image (void)
{
    int prev_x_adjust = visible_left_border;
    int prev_y_adjust = thisframe_y_adjust;
    int tmp;

    if (currprefs.gfx_xcenter) {
	int w = gfxvidinfo.width;

	if (max_diwstop - min_diwstart < w && currprefs.gfx_xcenter == 2)
	    /* Try to center. */
	    visible_left_border = (max_diwstop - min_diwstart - w) / 2 + min_diwstart;
	else
	    visible_left_border = max_diwstop - w - (max_diwstop - min_diwstart - w) / 2;
	visible_left_border &= ~((1 << lores_shift) - 1);

	/* Would the old value be good enough? If so, leave it as it is if we want to
	 * be clever. */
	if (currprefs.gfx_xcenter == 2) {
	    if (visible_left_border < prev_x_adjust && prev_x_adjust < min_diwstart && min_diwstart - visible_left_border <= 32)
		visible_left_border = prev_x_adjust;
	}
    } else
	visible_left_border = max_diwlastword - gfxvidinfo.width;
    if (visible_left_border > max_diwlastword - 32)
	visible_left_border = max_diwlastword - 32;
    if (visible_left_border < 0)
	visible_left_border = 0;

    linetoscr_x_adjust_bytes = visible_left_border * gfxvidinfo.pixbytes;

    visible_right_border = visible_left_border + gfxvidinfo.width;
    if (visible_right_border > max_diwlastword)
	visible_right_border = max_diwlastword;

    thisframe_y_adjust = minfirstline;
    if (currprefs.gfx_ycenter && thisframe_first_drawn_line != -1) {

	if (thisframe_last_drawn_line - thisframe_first_drawn_line < max_drawn_amiga_line && currprefs.gfx_ycenter == 2)
	    thisframe_y_adjust = (thisframe_last_drawn_line - thisframe_first_drawn_line - max_drawn_amiga_line) / 2 + thisframe_first_drawn_line;
	else
	    thisframe_y_adjust = thisframe_first_drawn_line + ((thisframe_last_drawn_line - thisframe_first_drawn_line) - max_drawn_amiga_line) / 2;

	/* Would the old value be good enough? If so, leave it as it is if we want to
	 * be clever. */
	if (currprefs.gfx_ycenter == 2) {
	    if (thisframe_y_adjust != prev_y_adjust
		&& prev_y_adjust <= thisframe_first_drawn_line
		&& prev_y_adjust + max_drawn_amiga_line > thisframe_last_drawn_line)
		thisframe_y_adjust = prev_y_adjust;
	}
	/* Make sure the value makes sense */
	if (thisframe_y_adjust + max_drawn_amiga_line > maxvpos_max)
	    thisframe_y_adjust = maxvpos_max - max_drawn_amiga_line;
	if (thisframe_y_adjust < minfirstline)
	    thisframe_y_adjust = minfirstline;
    }
    thisframe_y_adjust_real = thisframe_y_adjust << (currprefs.gfx_linedbl ? 1 : 0);
    tmp = (maxvpos_max - thisframe_y_adjust) << (currprefs.gfx_linedbl ? 1 : 0);
    if (tmp != max_ypos_thisframe) {
	last_max_ypos = tmp;
	if (last_max_ypos < 0)
	    last_max_ypos = 0;
    }
    max_ypos_thisframe = tmp;

    /* @@@ interlace_seen used to be (bplcon0 & 4), but this is probably
     * better.  */
    if (prev_x_adjust != visible_left_border || prev_y_adjust != thisframe_y_adjust)
	frame_redraw_necessary |= interlace_seen && currprefs.gfx_linedbl ? 2 : 1;

    max_diwstop = 0;
    min_diwstart = 10000;
}

static void lores_reset (void)
{
    lores_factor = currprefs.gfx_lores ? 1 : 2;
    lores_shift = currprefs.gfx_lores ? 0 : 1;
}

#define FRAMES_UNTIL_RES_SWITCH 5
static int frame_res_cnt;
static void init_drawing_frame (void)
{
    int i, maxline;
    static int frame_res_old;

    if (FRAMES_UNTIL_RES_SWITCH > 0 && frame_res_old == frame_res * 2 + frame_res_lace) {
	frame_res_cnt--;
	if (frame_res_cnt == 0) {
	    int m = frame_res * 2 + frame_res_lace;
	    struct wh *dst = currprefs.gfx_afullscreen ? &changed_prefs.gfx_size_fs : &changed_prefs.gfx_size_win;
	    while (m < 4) {
		struct wh *src = currprefs.gfx_afullscreen ? &currprefs.gfx_size_fs_xtra[m] : &currprefs.gfx_size_win_xtra[m];
		if ((src->width > 0 && src->height > 0) || (currprefs.gfx_autoresolution && currprefs.gfx_filter > 0)) {
		    changed_prefs.gfx_lores = (m & 2) == 0 ? 1 : 0;
		    changed_prefs.gfx_linedbl = (m & 1) == 0 ? 0 : 1;
		    if (currprefs.gfx_autoresolution) {
			changed_prefs.gfx_filter_horiz_zoom_mult = 1000 / (changed_prefs.gfx_lores + 1);
			changed_prefs.gfx_filter_vert_zoom_mult = (changed_prefs.gfx_linedbl + 1) * 500;
		    } else {
    			*dst = *src;
		    }
		    break;
		}
		m++;
	    }
	    frame_res_cnt = FRAMES_UNTIL_RES_SWITCH;
	}
    } else {
	frame_res_old = frame_res * 2 + frame_res_lace;
	frame_res_cnt = FRAMES_UNTIL_RES_SWITCH;
    }
    frame_res = 0;
    frame_res_lace = 0;

    if (can_use_lores > AUTO_LORES_FRAMES && 0) {
	lores_factor = 1;
	lores_shift = 0;
    } else {
	can_use_lores++;
	lores_reset ();
    }

    init_hardware_for_drawing_frame ();

    if (thisframe_first_drawn_line == -1)
	thisframe_first_drawn_line = minfirstline;
    if (thisframe_first_drawn_line > thisframe_last_drawn_line)
	thisframe_last_drawn_line = thisframe_first_drawn_line;

    maxline = currprefs.gfx_linedbl ? (maxvpos_max + 1) * 2 + 1 : (maxvpos_max + 1) + 1;
    maxline++;
#ifdef SMART_UPDATE
    for (i = 0; i < maxline; i++) {
	switch (linestate[i]) {
	 case LINE_DONE_AS_PREVIOUS:
	    linestate[i] = LINE_REMEMBERED_AS_PREVIOUS;
	    break;
	 case LINE_REMEMBERED_AS_BLACK:
	    break;
	 default:
	    linestate[i] = LINE_UNDECIDED;
	    break;
	}
    }
#else
    memset (linestate, LINE_UNDECIDED, maxline);
#endif
    last_drawn_line = 0;
    first_drawn_line = 32767;

    first_block_line = last_block_line = NO_BLOCK;
    if (currprefs.test_drawing_speed)
	frame_redraw_necessary = 1;
    else if (frame_redraw_necessary)
	frame_redraw_necessary--;

    center_image ();

    thisframe_first_drawn_line = -1;
    thisframe_last_drawn_line = -1;

    drawing_color_matches = -1;
}

/*
 * Some code to put status information on the screen.
 */

#define TD_PADX 10
#define TD_PADY 2
#define TD_WIDTH 32
#define TD_LED_WIDTH 24
#define TD_LED_HEIGHT 4

#define TD_RIGHT 1
#define TD_BOTTOM 2

static int td_pos = (TD_RIGHT|TD_BOTTOM);

#define TD_NUM_WIDTH 7
#define TD_NUM_HEIGHT 7

#define TD_TOTAL_HEIGHT (TD_PADY * 2 + TD_NUM_HEIGHT)

#define NUMBERS_NUM 16

#define TD_BORDER 0x333

static const char *numbers = { /* ugly  0123456789CHD%+- */
"+++++++--++++-+++++++++++++++++-++++++++++++++++++++++++++++++++++++++++++++-++++++-++++----++---+--------------"
"+xxxxx+--+xx+-+xxxxx++xxxxx++x+-+x++xxxxx++xxxxx++xxxxx++xxxxx++xxxxx++xxxx+-+x++x+-+xxx++-+xx+-+x---+----------"
"+x+++x+--++x+-+++++x++++++x++x+++x++x++++++x++++++++++x++x+++x++x+++x++x++++-+x++x+-+x++x+--+x++x+--+x+----+++--"
"+x+-+x+---+x+-+xxxxx++xxxxx++xxxxx++xxxxx++xxxxx+--++x+-+xxxxx++xxxxx++x+----+xxxx+-+x++x+----+x+--+xxx+--+xxx+-"
"+x+++x+---+x+-+x++++++++++x++++++x++++++x++x+++x+--+x+--+x+++x++++++x++x++++-+x++x+-+x++x+---+x+x+--+x+----+++--"
"+xxxxx+---+x+-+xxxxx++xxxxx+----+x++xxxxx++xxxxx+--+x+--+xxxxx++xxxxx++xxxx+-+x++x+-+xxx+---+x++xx--------------"
"+++++++---+++-++++++++++++++----+++++++++++++++++--+++--++++++++++++++++++++-++++++-++++------------------------"
};

STATIC_INLINE void putpixel (int x, xcolnr c8)
{
    if (x <= 0)
	return;

    switch(gfxvidinfo.pixbytes)
    {
    case 1:
	xlinebuffer[x] = (uae_u8)c8;
	break;
    case 2:
    {
	uae_u16 *p = (uae_u16 *)xlinebuffer + x;
	*p = (uae_u16)c8;
	break;
    }
    case 3:
	/* no 24 bit yet */
	break;
    case 4:
    {
	uae_u32 *p = (uae_u32 *)xlinebuffer + x;
	*p = c8;
	break;
    }
    }
}

static void write_tdnumber (int x, int y, int num)
{
    int j;
    const char *numptr;

    numptr = numbers + num * TD_NUM_WIDTH + NUMBERS_NUM * TD_NUM_WIDTH * y;
    for (j = 0; j < TD_NUM_WIDTH; j++) {
	if (*numptr == 'x')
	    putpixel (x + j, xcolors[0xfff]);
	else if (*numptr == '+')
	    putpixel (x + j, xcolors[0x000]);
	numptr++;
    }
}

static void draw_status_line (int line)
{
    int x_start, y, j, led;

    if (td_pos & TD_RIGHT)
	x_start = gfxvidinfo.width - TD_PADX - NUM_LEDS * TD_WIDTH;
    else
	x_start = TD_PADX;

    y = line - (gfxvidinfo.height - TD_TOTAL_HEIGHT);
    xlinebuffer = gfxvidinfo.linemem;
    if (xlinebuffer == 0)
	xlinebuffer = row_map[line];

    for (led = 0; led < NUM_LEDS; led++) {
	int side, pos, num1 = -1, num2 = -1, num3 = -1, num4 = -1;
	int x, off_rgb, on_rgb, c, on = 0, am = 2;
	if (led >= 1 && led <= 4) {
	    int pled = led - 1;
	    int track = gui_data.drive_track[pled];
	    pos = 6 + pled;
	    on_rgb = 0x0c0;
	    off_rgb = 0x030;
	    if (!gui_data.drive_disabled[pled]) {
		num1 = -1;
		num2 = track / 10;
		num3 = track % 10;
		on = gui_data.drive_motor[pled];
		if (gui_data.drive_writing[pled])
		    on_rgb = 0xc00;
	    }
	    side = gui_data.drive_side;
	} else if (led == 0) {
	    pos = 3;
	    on = gui_data.powerled;
	    on_rgb = 0xc00;
	    off_rgb = 0x300;
	} else if (led == 5) {
	    pos = 5;
	    on = gui_data.cd;
	    on_rgb = 0x00c;
	    off_rgb = 0x003;
	    num1 = -1;
	    num2 = 10;
	    num3 = 12;
	} else if (led == 6) {
	    pos = 4;
	    on = gui_data.hd;
	    on_rgb = on == 2 ? 0xc00 : 0x00c;
	    off_rgb = 0x003;
	    num1 = -1;
	    num2 = 11;
	    num3 = 12;
	} else if (led == 7) {
	    int fps = (gui_data.fps + 5) / 10;
	    pos = 2;
	    on_rgb = 0x000;
	    off_rgb = 0x000;
	    num1 = fps / 100;
	    num2 = (fps - num1 * 100) / 10;
	    num3 = fps % 10;
	    am = 3;
	    if (num1 == 0)
		am = 2;
	} else if (led == 8) {
	    int idle = (gui_data.idle + 5) / 10;
	    pos = 1;
	    on = framecnt;
	    on_rgb = 0xc00;
	    off_rgb = 0x000;
	    num1 = idle / 100;
	    num2 = (idle - num1 * 100) / 10;
	    num3 = idle % 10;
	    num4 = num1 == 0 ? 13 : -1;
	    am = 3;
	} else if (led == 9) {
	    int snd = abs(gui_data.sndbuf + 5) / 10;
	    if (snd > 99)
		snd = 99;
	    pos = 0;
	    on = gui_data.sndbuf_status;
	    if (on < 3) {
		num1 = gui_data.sndbuf < 0 ? 15 : 14;
		num2 = snd / 10;
		num3 = snd % 10;
	    }
	    on_rgb = 0x000;
	    if (on < 0)
		on_rgb = 0xcc0; // underflow
	    else if (on == 2)
		on_rgb = 0xc00; // really big overflow
	    else if (on == 1)
		on_rgb = 0x00c; // "normal" overflow
	    off_rgb = 0x000;
	    am = 3;
	}
	c = xcolors[on ? on_rgb : off_rgb];
	if (y == 0 || y == TD_TOTAL_HEIGHT - 1)
	    c = xcolors[TD_BORDER];

	x = x_start + pos * TD_WIDTH;
	putpixel (x - 1, xcolors[TD_BORDER]);
	for (j = 0; j < TD_LED_WIDTH; j++)
	    putpixel (x + j, c);
	putpixel (x + j, xcolors[TD_BORDER]);

	if (y >= TD_PADY && y - TD_PADY < TD_NUM_HEIGHT) {
	    if (num3 >= 0) {
		x += (TD_LED_WIDTH - am * TD_NUM_WIDTH) / 2;
		if (num1 > 0) {
		    write_tdnumber (x, y - TD_PADY, num1);
		    x += TD_NUM_WIDTH;
		}
		write_tdnumber (x, y - TD_PADY, num2);
		x += TD_NUM_WIDTH;
		write_tdnumber (x, y - TD_PADY, num3);
		x += TD_NUM_WIDTH;
		if (num4 > 0)
		    write_tdnumber (x, y - TD_PADY, num4);
	    }
	}
    }
}

#define LIGHTPEN_HEIGHT 12
#define LIGHTPEN_WIDTH 17

static const char *lightpen_cursor = {
    "------.....------"
    "------.xxx.------"
    "------.xxx.------"
    "------.xxx.------"
    ".......xxx......."
    ".xxxxxxxxxxxxxxx."
    ".xxxxxxxxxxxxxxx."
    ".......xxx......."
    "------.xxx.------"
    "------.xxx.------"
    "------.xxx.------"
    "------.....------"
};

static void draw_lightpen_cursor (int x, int y, int line, int onscreen)
{
    int i;
    const char *p;
    int color1 = onscreen ? 0xff0 : 0xf00;
    int color2 = 0x000;

    xlinebuffer = gfxvidinfo.linemem;
    if (xlinebuffer == 0)
	xlinebuffer = row_map[line];
    
    p = lightpen_cursor + y * LIGHTPEN_WIDTH;
    for (i = 0; i < LIGHTPEN_WIDTH; i++) {
	int xx = x + i - LIGHTPEN_WIDTH / 2;
	if (*p != '-' && xx >= 0 && xx < gfxvidinfo.width)
	    putpixel(xx, *p == 'x' ? xcolors[color1] : xcolors[color2]);
	p++;
    }
}

static int lightpen_y1, lightpen_y2;

static void lightpen_update (void)
{
    int i;

    if (lightpen_x < LIGHTPEN_WIDTH + 1)
	lightpen_x = LIGHTPEN_WIDTH + 1;
    if (lightpen_x >= gfxvidinfo.width - LIGHTPEN_WIDTH - 1)
	lightpen_x = gfxvidinfo.width - LIGHTPEN_WIDTH - 2;
    if (lightpen_y < LIGHTPEN_HEIGHT + 1)
	lightpen_y = LIGHTPEN_HEIGHT + 1;
    if (lightpen_y >= gfxvidinfo.height - LIGHTPEN_HEIGHT - 1)
	lightpen_y = gfxvidinfo.height - LIGHTPEN_HEIGHT - 2;
    if (lightpen_y >= max_ypos_thisframe - LIGHTPEN_HEIGHT - 1)
	lightpen_y = max_ypos_thisframe - LIGHTPEN_HEIGHT - 2;

    lightpen_cx = (((lightpen_x + visible_left_border) >> lores_shift) >> 1) + DISPLAY_LEFT_SHIFT - DIW_DDF_OFFSET;

    lightpen_cy = lightpen_y;
    if (currprefs.gfx_linedbl)
	lightpen_cy >>= 1;
    lightpen_cy += minfirstline;

    if (lightpen_cx < 0x18)
	lightpen_cx = 0x18;
    if (lightpen_cx >= maxhpos)
	lightpen_cx -= maxhpos;
    if (lightpen_cy < minfirstline)
	lightpen_cy = minfirstline;
    if (lightpen_cy >= maxvpos_max)
	lightpen_cy = maxvpos_max - 1;

    for (i = 0; i < LIGHTPEN_HEIGHT; i++) {
        int line = lightpen_y + i - LIGHTPEN_HEIGHT / 2;
        if (line >= 0 || line < max_ypos_thisframe) {
	    draw_lightpen_cursor(lightpen_x, i, line, lightpen_cx > 0);
	    flush_line (line);
	}
    }
    lightpen_y1 = lightpen_y - LIGHTPEN_HEIGHT / 2 - 1 + min_ypos_for_screen;
    lightpen_y2 = lightpen_y1 + LIGHTPEN_HEIGHT + 2;
}

void finish_drawing_frame (void)
{
    int i;

    if (! lockscr ()) {
	notice_screen_contents_lost ();
	return;
    }

#ifndef SMART_UPDATE
    /* @@@ This isn't exactly right yet. FIXME */
    if (!interlace_seen)
	do_flush_screen (first_drawn_line, last_drawn_line);
    else
	unlockscr ();
    return;
#endif
    for (i = 0; i < max_ypos_thisframe; i++) {
	int i1 = i + min_ypos_for_screen;
	int line = i + thisframe_y_adjust_real;
	int where;

	if (linestate[line] == LINE_UNDECIDED)
	    break;

	where = amiga2aspect_line_map[i1];
	if (where >= gfxvidinfo.height)
	    break;
	if (where < 0)
	    continue;

	pfield_draw_line (line, where, amiga2aspect_line_map[i1 + 1]);
    }

    /* clear possible old garbage at the bottom if emulated area become smaller */
    for (i = last_max_ypos; i < gfxvidinfo.height; i++) {
	int i1 = i + min_ypos_for_screen;
	int line = i + thisframe_y_adjust_real;
	int where = amiga2aspect_line_map[i1];
	xcolnr tmp;

	if (where >= gfxvidinfo.height)
	    break;
	if (where < 0)
	    continue;
	tmp = colors_for_drawing.acolors[0];
	colors_for_drawing.acolors[0] = getxcolor (0);
	xlinebuffer = gfxvidinfo.linemem;
	if (xlinebuffer == 0)
	    xlinebuffer = row_map[where];
	xlinebuffer -= linetoscr_x_adjust_bytes;
	fill_line ();
	linestate[line] = LINE_UNDECIDED;
	do_flush_line (where);
	colors_for_drawing.acolors[0] = tmp;
    }

    if (currprefs.leds_on_screen) {
	for (i = 0; i < TD_TOTAL_HEIGHT; i++) {
	    int line = gfxvidinfo.height - TD_TOTAL_HEIGHT + i;
	    draw_status_line (line);
	    do_flush_line (line);
	}
    }

    if (lightpen_x > 0 || lightpen_y > 0)
	lightpen_update ();

    do_flush_screen (first_drawn_line, last_drawn_line);

    if (brdblank_changed) {
	last_max_ypos = max_ypos_thisframe;
	last_redraw_point = 10;
	notice_screen_contents_lost();
	brdblank_changed = 0;
    }
}

void hardware_line_completed (int lineno)
{
#ifndef SMART_UPDATE
    {
	int i, where;
	/* l is the line that has been finished for drawing. */
	i = lineno - thisframe_y_adjust_real;
	if (i >= 0 && i < max_ypos_thisframe) {
	    where = amiga2aspect_line_map[i+min_ypos_for_screen];
	    if (where < gfxvidinfo.height && where != -1)
		pfield_draw_line (lineno, where, amiga2aspect_line_map[i+min_ypos_for_screen+1]);
	}
    }
#endif
}

STATIC_INLINE void check_picasso (void)
{
#ifdef PICASSO96
    if (picasso_on && picasso_redraw_necessary)
	picasso_refresh (1);
    picasso_redraw_necessary = 0;

    if (picasso_requested_on == picasso_on)
	return;

    picasso_on = picasso_requested_on;

    if (!picasso_on)
	clear_inhibit_frame (IHF_PICASSO);
    else
	set_inhibit_frame (IHF_PICASSO);

    gfx_set_picasso_state (picasso_on);
    picasso_enablescreen (picasso_requested_on);

    notice_screen_contents_lost ();
    notice_new_xcolors ();
    count_frame ();
#endif
}

void redraw_frame (void)
{
    last_drawn_line = 0;
    first_drawn_line = 32767;
    finish_drawing_frame ();
    flush_screen (0, 0);
}

void vsync_handle_redraw (int long_frame, int lof_changed)
{
    last_redraw_point++;
    if (lof_changed || ! interlace_seen || last_redraw_point >= 2 || long_frame) {
	last_redraw_point = 0;

	if (framecnt == 0)
	    finish_drawing_frame ();

	interlace_seen = 0;

	/* At this point, we have finished both the hardware and the
	 * drawing frame. Essentially, we are outside of all loops and
	 * can do some things which would cause confusion if they were
	 * done at other times.
	 */

#ifdef SAVESTATE
	if (savestate_state == STATE_DORESTORE) {
	    savestate_state = STATE_RESTORE;
	    reset_drawing ();
	    uae_reset (0);
	} else if (savestate_state == STATE_DOREWIND) {
	    savestate_state = STATE_REWIND;
	    reset_drawing ();
	    uae_reset (0);
	}
#endif

	if (quit_program < 0) {
	    quit_program = -quit_program;
	    set_inhibit_frame (IHF_QUIT_PROGRAM);
	    set_special (&regs, SPCFLAG_BRK);
#ifdef FILESYS
	    filesys_prepare_reset ();
#endif
	    return;
	}

#ifdef SAVESTATE
	savestate_capture (0);
#endif
	count_frame ();
	check_picasso ();

	if (check_prefs_changed_gfx ()) {
	    reset_drawing ();
	    init_row_map ();
	    init_aspect_maps ();
	    notice_screen_contents_lost ();
	    notice_new_xcolors ();
	}

	check_prefs_changed_audio ();
#ifdef JIT
	check_prefs_changed_comp ();
#endif
	check_prefs_changed_custom ();
	check_prefs_changed_cpu ();

	if (framecnt == 0)
	    init_drawing_frame ();
    } else {
	if (currprefs.gfx_afullscreen && currprefs.gfx_vsync)
	    flush_screen (0, 0); /* vsync mode */
    }
    gui_hd_led (0);
    gui_cd_led (0);
#ifdef AVIOUTPUT
    frame_drawn ();
#endif
}

void hsync_record_line_state (int lineno, enum nln_how how, int changed)
{
    char *state;

    if (framecnt != 0)
	return;

    state = linestate + lineno;
    changed += frame_redraw_necessary + ((lineno >= lightpen_y1 && lineno <= lightpen_y2) ? 1 : 0);

    switch (how) {
     case nln_normal:
	*state = changed ? LINE_DECIDED : LINE_DONE;
	break;
     case nln_doubled:
	*state = changed ? LINE_DECIDED_DOUBLE : LINE_DONE;
	changed += state[1] != LINE_REMEMBERED_AS_PREVIOUS;
	state[1] = changed ? LINE_AS_PREVIOUS : LINE_DONE_AS_PREVIOUS;
	break;
     case nln_nblack:
	*state = changed ? LINE_DECIDED : LINE_DONE;
	if (state[1] != LINE_REMEMBERED_AS_BLACK)
	    state[1] = LINE_BLACK;
	break;
     case nln_lower:
	if (state[-1] == LINE_UNDECIDED)
	    state[-1] = LINE_BLACK;
	*state = changed ? LINE_DECIDED : LINE_DONE;
	break;
     case nln_upper:
	*state = changed ? LINE_DECIDED : LINE_DONE;
	if (state[1] == LINE_UNDECIDED
	    || state[1] == LINE_REMEMBERED_AS_PREVIOUS
	    || state[1] == LINE_AS_PREVIOUS)
	    state[1] = LINE_BLACK;
	break;
    }
}

static void dummy_flush_line (struct vidbuf_description *gfxinfo, int line_no)
{
}

static void dummy_flush_block (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
}

static void dummy_flush_screen (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
}

static void dummy_flush_clear_screen (struct vidbuf_description *gfxinfo)
{
}

static int  dummy_lock (struct vidbuf_description *gfxinfo)
{
    return 1;
}

static void dummy_unlock (struct vidbuf_description *gfxinfo)
{
}

static void gfxbuffer_reset (void)
{
    gfxvidinfo.flush_line         = dummy_flush_line;
    gfxvidinfo.flush_block        = dummy_flush_block;
    gfxvidinfo.flush_screen       = dummy_flush_screen;
    gfxvidinfo.flush_clear_screen = dummy_flush_clear_screen;
    gfxvidinfo.lockscr            = dummy_lock;
    gfxvidinfo.unlockscr          = dummy_unlock;
}

void notice_interlace_seen (void)
{
    interlace_seen = 1;
    frame_res_lace = 1;
}

void reset_drawing (void)
{
    unsigned int i;

    max_diwstop = 0;

    lores_reset ();

    for (i = 0; i < sizeof linestate / sizeof *linestate; i++)
	linestate[i] = LINE_UNDECIDED;

    init_aspect_maps ();

    init_row_map();

    last_redraw_point = 0;

    memset (spixels, 0, sizeof spixels);
    memset (&spixstate, 0, sizeof spixstate);

    init_drawing_frame ();

    notice_screen_contents_lost ();
    frame_res_cnt = FRAMES_UNTIL_RES_SWITCH;
    lightpen_y1 = lightpen_y2 = -1;
}

void drawing_init (void)
{
    gen_pfield_tables();

    uae_sem_init (&gui_sem, 0, 1);
#ifdef PICASSO96
    InitPicasso96 ();
    picasso_on = 0;
    picasso_requested_on = 0;
    gfx_set_picasso_state (0);
#endif
    xlinebuffer = gfxvidinfo.bufmem;
    inhibit_frame = 0;

    gfxbuffer_reset ();
    reset_drawing ();
}

