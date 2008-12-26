
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef GFXFILTER

#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "gfxfilter.h"
#include "dxwrap.h"
#include "statusline.h"
#include "drawing.h"

struct uae_filter uaefilters[] =
{
    { UAE_FILTER_NULL, 0, 1, "Null filter", "null", 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32, 0, 0, 0 },

    { UAE_FILTER_DIRECT3D, 0, 1, "Direct3D", "direct3d", 1, 0, 0, 0, 0 },

    { UAE_FILTER_OPENGL, 0, 1, "OpenGL", "opengl", 1, 0, 0, 0, 0 },

    { UAE_FILTER_SCALE2X, 0, 2, "Scale2X", "scale2x", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32, 0, 0 },

    { UAE_FILTER_HQ, 0, 2, "hq2x/3x/4x", "hqx", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

    { UAE_FILTER_SUPEREAGLE, 0, 2, "SuperEagle", "supereagle", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, 0, 0 },

    { UAE_FILTER_SUPER2XSAI, 0, 2, "Super2xSaI", "super2xsai", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, 0, 0 },

    { UAE_FILTER_2XSAI, 0, 2, "2xSaI", "2xsai", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, 0, 0 },

    { UAE_FILTER_PAL, 1, 1, "PAL", "pal", 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32, 0, 0, 0 },

    { 0 }
};

static int filteroffsetx, filteroffsety, filterxmult = 1000, filterymult = 1000;
static int dst_width, dst_height, amiga_width, amiga_height, amiga_depth, dst_depth, scale;
static int temp_width, temp_height;
uae_u8 *bufmem_ptr;
static LPDIRECTDRAWSURFACE7 tempsurf;
static uae_u8 *tempsurf2, *tempsurf3;
static int cleartemp;
static uae_u32 rc[256], gc[256], bc[256];

void getfilteroffset (int *dx, int *dy, int *mx, int *my)
{
    *dx = (dst_width - amiga_width * 1000 / filterxmult) / 2;
    *dy = (dst_height - amiga_height * 1000 / filterymult) / 2;
    *mx = filterxmult;
    *my = filterymult;
}

static int vblscale (int v)
{
    static int o;
    int n;

    n = (beamcon0 & 0x80) + maxvpos;
    if (n != o)
	cleartemp = 1;
    o = n;
    if (beamcon0 & 0x80)
	return v;
    v = v * maxvpos / MAXVPOS_PAL;
    return v;
}

uae_u8 *getfilterrect1 (RECT *sr, RECT *dr, int dst_depth, int aw, int ah, int scale, int temp_width, int temp_height, uae_u8 *dptr, int pitch)
{
    int aws, ahs;
    
    ah = vblscale (ah);
    aws = aw * scale;
    ahs = ah * scale;
    
    SetRect (sr, 0, 0, 0, 0);
    dr->left = sr->left + (temp_width - aws) /2;
    dr->top = sr->top + (temp_height - ahs) / 2;
    dptr += dr->left * (dst_depth / 8);
    dptr += dr->top * pitch;
    return dptr;
}

void getfilterrect2 (RECT *sr, RECT *dr, int dst_width, int dst_height, int aw, int ah, int scale, int temp_width, int temp_height)
{
    int aws, ahs;
    int xs, ys;
    int xmult, ymult;
    int v, xy;

    xy = 1;

    ah = vblscale (ah);
    aws = aw * scale;
    ahs = ah * scale;

    SetRect (sr, 0, 0, dst_width, dst_height);
    dr->left = (temp_width - aws) /2;
    dr->top =  (temp_height - ahs) / 2;
    dr->left -= (dst_width - aws) / 2;
    dr->top -= (dst_height - ahs) / 2;
    dr->right = dr->left + dst_width;
    dr->bottom = dr->top + dst_height;


    xmult = currprefs.gfx_filter_horiz_zoom_mult;
    ymult = currprefs.gfx_filter_vert_zoom_mult;
    if (currprefs.gfx_filter_autoscale) {
	int w, h, x, y, v;
	int extraw = currprefs.gfx_filter_horiz_zoom;
	int extrah = currprefs.gfx_filter_vert_zoom;
	static int oxmult, oymult;
	xmult = 1000;
	ymult = 1000;
	v = get_custom_limits (&w, &h, &x, &y);
	if (v) {
	    int diff;
	    dr->left = (temp_width - aws) /2;
	    dr->top =  (temp_height - ahs) / 2;
	    dr->right = dr->left + dst_width * scale;
	    dr->bottom = dr->top + dst_height * scale;
	    OffsetRect (dr, x * scale, y * scale);
	    dr->right -= (dst_width - w) * scale;
	    dr->bottom -= (dst_height - h) * scale;
	    dr->left -= dst_width * extraw / 1000;
	    dr->top -= dst_width * extrah / 1000;
	    dr->right += dst_width * extraw / 1000;
	    dr->bottom += dst_width * extrah / 1000;
	    
	    if (currprefs.gfx_filter_keep_aspect || currprefs.gfx_filter_aspect > 0) {
		int xratio = dst_width * 256 / w;
		int yratio = dst_height * 256 / h;
		int diffx = dr->right - dr->left;
		int diffy = dr->bottom - dr->top;
		
		if (currprefs.gfx_filter_aspect > 0) {
		    int xm = (currprefs.gfx_filter_aspect >> 8) * 256;
		    int ym = (currprefs.gfx_filter_aspect & 0xff) * 256;
		    xratio = xratio * ((dst_width * ym * 256) / (dst_height * xm)) / 256;
		}

		if (xratio > yratio) {
		    diff = diffx - diffx * yratio / xratio;
		    dr->right += diff / 2;
		    dr->left -= diff / 2;
		} else {
		    diff = diffx - diffx * xratio / yratio;
		    dr->bottom += diff / 2;
		    dr->top -= diff / 2;
		}
	    }
	    diff = dr->right - dr->left;
	    filterxmult = diff * 1000 / dst_width;
	    diff = dr->bottom - dr->top;
	    filterymult = diff * 1000 / dst_height;
	    return;
	}
    } else if (currprefs.gfx_filter_keep_aspect && !xmult && !ymult) {
        xmult = aws * 1000 / dst_width;
        ymult = ahs * 1000 / dst_height;
	if (xmult < ymult)
	    xmult = ymult;
	else
	    ymult = xmult;
	ymult = vblscale (ymult);
    } else {
	if (xmult <= 0)
	    xmult = aws * 1000 / dst_width;
	else
	    xmult = xmult + xmult * currprefs.gfx_filter_horiz_zoom / 2000;
	if (ymult <= 0)
	    ymult = ahs * 1000 / dst_height;
	else
	    ymult = ymult + ymult * currprefs.gfx_filter_vert_zoom / 2000;
    }

    if (currprefs.gfx_filter_aspect > 0) {
	int srcratio, dstratio;
	dstratio = (currprefs.gfx_filter_aspect >> 8) * 256 / (currprefs.gfx_filter_aspect & 0xff);
	srcratio = dst_width * 256 / dst_height;
	if (srcratio > dstratio)
	    xmult = xmult * srcratio / dstratio;
	else
	    ymult = ymult * dstratio / srcratio;
    }

    if (xy) {
	v = currprefs.gfx_filter ? currprefs.gfx_filter_horiz_offset : 0;
	OffsetRect (dr, (int)(-v * aws / 1000.0), 0);
	v = currprefs.gfx_filter ? currprefs.gfx_filter_vert_offset : 0;
	OffsetRect (dr, 0, (int)(-v * ahs / 1000.0));
    }

    xs = dst_width - dst_width * xmult / 1000;
    dr->left += xs / 2;
    dr->right -= xs / 2;

    ys = dst_height - dst_height * ymult / 1000;
    dr->top += ys / 2;
    dr->bottom -= ys / 2;

    filterxmult = xmult;
    filterymult = ymult;
}

static void statusline (void)
{
    DDSURFACEDESC2 desc;
    RECT sr, dr;
    int y;

    if (!(currprefs.leds_on_screen & STATUSLINE_CHIPSET) || !tempsurf)
	return;
    SetRect (&sr, 0, 0, dst_width, TD_TOTAL_HEIGHT);
    SetRect (&dr, 0, dst_height - TD_TOTAL_HEIGHT, dst_width, dst_height);
    DirectDraw_BlitRect (tempsurf, &sr, NULL, &dr);
    if (locksurface (tempsurf, &desc)) {
	int yy = 0;
	for (y = dst_height - TD_TOTAL_HEIGHT; y < dst_height; y++) {
	    uae_u8 *buf = (uae_u8*)desc.lpSurface + yy * desc.lPitch;
	    draw_status_line_single (buf, dst_depth / 8, yy, dst_width, rc, gc, bc);
	    yy++;
	}
	unlocksurface (tempsurf);
	DirectDraw_BlitRect (NULL, &dr, tempsurf, &sr);
    }
}

void S2X_configure (int rb, int gb, int bb, int rs, int gs, int bs)
{
    Init_2xSaI (rb, gb, bb, rs, gs, bs);
    hq_init (rb, gb, bb, rs, gs, bs);
    PAL_init ();
    bufmem_ptr = 0;
}

void S2X_free (void)
{
    changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen & ~STATUSLINE_TARGET;

    freesurface (tempsurf);
    tempsurf = 0;
    xfree (tempsurf2);
    tempsurf2 = 0;
    xfree (tempsurf3);
    tempsurf3 = 0;
    filteroffsetx = 0;
    filteroffsety = 0;
    filterxmult = 1000;
    filterymult = 1000;
}

void S2X_init (int dw, int dh, int aw, int ah, int mult, int ad, int dd)
{
    int flags = 0;
    int res_shift;

    S2X_free ();
    changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen | STATUSLINE_TARGET;

    if (dd == 32)
	alloc_colors_rgb (8, 8, 8, 16, 8, 0, 0, 0, 0, 0, rc, gc, bc);
    else
	alloc_colors_rgb (5, 6, 5, 11, 5, 0, 0, 0, 0, 0, rc, gc, bc);


    if (!currprefs.gfx_filter || !usedfilter) {
	usedfilter = &uaefilters[0];
	mult = 1;
    } else if (mult) {
	flags = usedfilter->x[mult];
	if ((ad == 16 && !(flags & UAE_FILTER_MODE_16)) || (ad == 32 && !(flags & UAE_FILTER_MODE_32))) {
	    usedfilter = &uaefilters[0];
	    mult = 1;
	    changed_prefs.gfx_filter = usedfilter->type;
	}
    }

    res_shift = RES_MAX - currprefs.gfx_resolution;
    if (currprefs.gfx_xcenter_size > 0 && (currprefs.gfx_xcenter_size >> res_shift) < aw)
	aw = currprefs.gfx_xcenter_size >> res_shift;
    res_shift = currprefs.gfx_linedbl ? 0 : 1;
    if (currprefs.gfx_ycenter_size > 0 && (currprefs.gfx_ycenter_size >> res_shift) < ah)
	ah = currprefs.gfx_ycenter_size >> res_shift;

    dst_width = dw;
    dst_height = dh;
    dst_depth = dd;
    amiga_width = aw;
    amiga_height = ah;
    amiga_depth = ad;
    scale = mult;

    filteroffsetx = (dst_width - amiga_width) / 2;
    filteroffsety = (dst_height - amiga_height) / 2;

    temp_width = dst_width * 3;
    if (temp_width > dxcaps.maxwidth)
	temp_width = dxcaps.maxwidth;
    temp_height = dst_height * 3;
    if (temp_height > dxcaps.maxheight)
	temp_height = dxcaps.maxheight;
    if (temp_width < dst_width)
	temp_width = dst_width;
    if (temp_height < dst_height)
	temp_height = dst_height;

    if (usedfilter->type == UAE_FILTER_HQ) {
	int w = amiga_width > dst_width ? amiga_width : dst_width;
	int h = amiga_height > dst_height ? amiga_height : dst_height;
	tempsurf2 = xmalloc (w * h * (amiga_depth / 8) * ((scale + 1) / 2));
	tempsurf3 = xmalloc (w * h *(dst_depth / 8) * 4 * scale);
	tempsurf = allocsystemsurface (temp_width, temp_height);
    } else {
        tempsurf = allocsurface (temp_width, temp_height);
    }
    if (!tempsurf)
        write_log ("DDRAW: failed to create temp surface (%dx%d)\n", temp_width, temp_height);

}

void S2X_render (void)
{
    int aw, ah, aws, ahs;
    uae_u8 *dptr, *enddptr, *sptr, *endsptr;
    int ok = 0;
    RECT sr, dr;
    DDSURFACEDESC2 desc;
    DWORD pitch;

    aw = amiga_width;
    ah = amiga_height;
    aws = aw * scale;
    ahs = ah * scale;

    if (ah < 16)
	return;
    if (aw < 16)
	return;
    if (tempsurf == NULL)
	return;

    sptr = gfxvidinfo.bufmem;
    endsptr = gfxvidinfo.bufmemend;
    bufmem_ptr = sptr;

    if (cleartemp) {
	clearsurface (tempsurf);
	cleartemp = 0;
    }
    if (!locksurface (tempsurf, &desc))
	return;
    pitch = desc.lPitch;
    dptr = (uae_u8*)desc.lpSurface;
    enddptr = dptr + pitch * temp_height;
    dptr = getfilterrect1 (&sr, &dr, dst_depth, aw, ah, scale, temp_width, temp_height, dptr, pitch);

    if (!dptr) /* weird things can happen */
	goto end;
    if (dptr < (uae_u8*)desc.lpSurface)
	goto endfail;

    if (usedfilter->type == UAE_FILTER_SCALE2X ) { /* 16+32/2X */

	if (amiga_depth == 16 && dst_depth == 16) {
	    AdMame2x (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	} else if (amiga_depth == 32 && dst_depth == 32) {
	    AdMame2x32 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else if (usedfilter->type == UAE_FILTER_HQ) { /* 32/2X+3X+4X */

	if (tempsurf2 && scale >= 2 && scale <= 4) {
	    /* Aaaaaaaarghhhghgh.. */
	    uae_u8 *sptr2 = tempsurf3;
	    uae_u8 *dptr2 = tempsurf2;
	    int i;
	    for (i = 0; i < ah; i++) {
		int w = aw * (amiga_depth / 8);
		memcpy (dptr2, sptr, w);
		dptr2 += w;
		sptr += gfxvidinfo.rowbytes;
	    }
	    if (amiga_depth == 16 && dst_depth == 32) {
		if (scale == 2)
		    hq2x_32 (tempsurf2, tempsurf3, aw, ah, aws * 4);
		else if (scale == 3)
		    hq3x_32 (tempsurf2, tempsurf3, aw, ah, aws * 4);
		else if (scale == 4)
		    hq4x_32 (tempsurf2, tempsurf3, aw, ah, aws * 4);
	        ok = 1;
	    } else if (amiga_depth == 16 && dst_depth == 16) {
		if (scale == 2)
		    hq2x_16 (tempsurf2, tempsurf3, aw, ah, aws * 2);
		else if (scale == 3)
		    hq3x_16 (tempsurf2, tempsurf3, aw, ah, aws * 2);
		else if (scale == 4)
		    hq4x_16 (tempsurf2, tempsurf3, aw, ah, aws * 2);
	        ok = 1;
	    }
	    for (i = 0; i < ah * scale; i++) {
		int w = aw * scale * (dst_depth / 8);
		memcpy (dptr, sptr2, w);
		sptr2 += w;
		dptr += pitch;
	    }
	}

    } else if (usedfilter->type == UAE_FILTER_SUPEREAGLE) { /* 16/32/2X */

	if (scale == 2 && amiga_depth == 16) {
	    if (dst_depth == 16) {
		SuperEagle_16 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
		ok = 1;
	    } else if (dst_depth == 32) {
		SuperEagle_32 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
		ok = 1;
	    }
	}

    } else if (usedfilter->type == UAE_FILTER_SUPER2XSAI) { /* 16/32/2X */

	if (scale == 2 && amiga_depth == 16) {
	    if (dst_depth == 16) {
		Super2xSaI_16 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
		ok = 1;
	    } else if (dst_depth == 32) {
		Super2xSaI_32 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
		ok = 1;
	    }
	}

    } else if (usedfilter->type == UAE_FILTER_2XSAI) { /* 16/32/2X */

	if (scale == 2 && amiga_depth == 16) {
	    if (dst_depth == 16) {
		_2xSaI_16 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
		ok = 1;
	    } else if (dst_depth == 32) {
		_2xSaI_32 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
		ok = 1;
	    }
	}

    } else if (usedfilter->type == UAE_FILTER_PAL) { /* 16/32/1X */

	if (amiga_depth == 32 && dst_depth == 32) {
	    PAL_1x1_32 ((uae_u32*)sptr, gfxvidinfo.rowbytes, (uae_u32*)dptr, pitch, aw, ah);
	    ok = 1;
	} else if (amiga_depth == 16 && dst_depth == 16) {
	    PAL_1x1_16 ((uae_u16*)sptr, gfxvidinfo.rowbytes, (uae_u16*)dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else { /* null */

	if (amiga_depth == dst_depth) {
	    int y;
	    for (y = 0; y < ah; y++) {
	        memcpy (dptr, sptr, aw * dst_depth / 8);
		sptr += gfxvidinfo.rowbytes;
		dptr += pitch;
	    }
	}
	ok = 1;

    }

endfail:
    if (ok == 0 && currprefs.gfx_filter) {
	usedfilter = &uaefilters[0];
	changed_prefs.gfx_filter = usedfilter->type;
    }

end:
    unlocksurface (tempsurf);

    getfilterrect2 (&sr, &dr, dst_width, dst_height, amiga_width, amiga_height, scale, temp_width, temp_height);
    if (dr.left >= 0 && dr.top >= 0 && dr.right < temp_width && dr.bottom < temp_height) {
	if (dr.left < dr.right && dr.top < dr.bottom)
	    DirectDraw_BlitRect (NULL, &sr, tempsurf, &dr);
    }
    statusline ();
}

void S2X_refresh (void)
{
    clearsurface (NULL);
    S2X_render ();
}

#endif
