
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
#include "direct3d.h"

#include <float.h>

struct uae_filter uaefilters[] =
{
	{ UAE_FILTER_NULL, 0, 1, L"Null filter", L"null", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32 },

	{ UAE_FILTER_SCALE2X, 0, 2, L"Scale2X", L"scale2x", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32 },

	{ UAE_FILTER_HQ2X, 0, 2, L"hq2x", L"hq2x", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, },

	{ UAE_FILTER_HQ3X, 0, 3, L"hq3x", L"hq3x", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_HQ4X, 0, 4, L"hq4x", L"hq4x", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_SUPEREAGLE, 0, 2, L"SuperEagle", L"supereagle", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_SUPER2XSAI, 0, 2, L"Super2xSaI", L"super2xsai", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_2XSAI, 0, 2, L"2xSaI", L"2xsai", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_PAL, 1, 1, L"PAL", L"pal", UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32 },

	{ 0 }
};

static int filteroffsetx, filteroffsety, filterxmult = 1000, filterymult = 1000;
static int dst_width, dst_height, amiga_width, amiga_height, amiga_depth, dst_depth, scale;
static int dst_width2, dst_height2, amiga_width2, amiga_height2, amiga_depth2, dst_depth2;
static int temp_width, temp_height;
uae_u8 *bufmem_ptr;
static LPDIRECTDRAWSURFACE7 tempsurf;
static uae_u8 *tempsurf2, *tempsurf3;
static int cleartemp;
static uae_u32 rc[256], gc[256], bc[256];
static int deskw, deskh;
static int d3d;
static bool inited;

void getfilteroffset (int *dx, int *dy, int *mx, int *my)
{
	*dx = filteroffsetx;
	*dy = filteroffsety;
	*mx = filterxmult;
	*my = filterymult;
}

static void getinit (void)
{
	if (isfullscreen ()) {
		struct MultiDisplay *md = getdisplay (&currprefs);

		deskw = md->rect.right - md->rect.left;
		deskh = md->rect.bottom - md->rect.top;
	} else {
		deskw = dst_width;
		deskh = dst_height;
	}
}

static int vblscale (int v)
{
	static int o;
	int n, v2;

	n = (beamcon0 & 0x80) + maxvpos_nom;
	if (n != o)
		cleartemp = 1;
	o = n;
	if (beamcon0 & 0x80)
		return v;
	if (currprefs.ntscmode)
		v2 = MAXVPOS_NTSC;
	else
		v2 = MAXVPOS_PAL;
	if (abs (v2 - maxvpos_nom) <= 3)
		return v;
	v = v * maxvpos_nom / v2;
	return v;
}
static int vblscale2 (int v)
{
	static int o;
	int n;

	n = (beamcon0 & 0x80) + maxvpos_nom;
	if (n != o)
		cleartemp = 1;
	o = n;
	if (beamcon0 & 0x80)
		return v;
	v = v * maxvpos_nom / MAXVPOS_PAL;
	return v;
}

static void fixh (int *ah, int *th, int minh)
{
	if (!(beamcon0 & 0x80)) {
		int max = ((572 / 2) * scale) << currprefs.gfx_vresolution;
		if (minh && max < minh)
			max = minh;
		if (*ah > max)
			*ah = max;
		if (*th > max)
			*th = max;
	}
}

static uae_u8 *getfilterrect1 (RECT *sr, RECT *dr, int dst_width, int dst_height, int dst_depth, int aw, int ah, int scale, int temp_width, int temp_height, uae_u8 *dptr, int pitch)
{
	int aws, ahs;

	fixh (&ah, &temp_height, dst_height);

	aws = aw * scale;
	ahs = ah * scale;

	SetRect (sr, 0, 0, 0, 0);
	dr->left = sr->left + (temp_width - aws) /2;
	dr->top = sr->top + (temp_height - ahs) / 2;
	dptr += dr->left * (dst_depth / 8);
	dptr += dr->top * pitch;
	return dptr;
}

static void sizeoffset (RECT *dr, RECT *zr, int w, int h)
{
	dr->right -= w;
	dr->bottom -= h;
	OffsetRect (zr, w / 2, h / 2);
}

void getfilterrect2 (RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int temp_width, int temp_height)
{
	float srcratio, dstratio;
	int aws, ahs, ahs2;
	int xs, ys;
	int v;
	int extraw, extrah;
	int fpuv;

	int filter_horiz_zoom = currprefs.gfx_filter_horiz_zoom;
	int filter_vert_zoom = currprefs.gfx_filter_vert_zoom;
	int filter_horiz_zoom_mult = currprefs.gfx_filter_horiz_zoom_mult;
	int filter_vert_zoom_mult = currprefs.gfx_filter_vert_zoom_mult;
	int filter_horiz_offset = currprefs.gfx_filter_horiz_offset;
	int filter_vert_offset = currprefs.gfx_filter_vert_offset;

	if (!usedfilter && !currprefs.gfx_api) {
		filter_horiz_zoom = filter_vert_zoom = 0;
		filter_horiz_zoom_mult = filter_vert_zoom_mult = 1000;
		filter_horiz_offset = filter_vert_offset = 0;
	}

	if (screen_is_picasso) {
		getrtgfilterrect2 (sr, dr, zr, dst_width, dst_height);
		return;
	}

	fpux_save (&fpuv);

	fixh (&ah, &temp_height, dst_height);

	getinit ();
	ahs2 = vblscale (ah) * scale;
	aws = aw * scale;
	ahs = ah * scale;
	//write_log (L"%d %d %d\n", dst_width, temp_width, aws);
	extraw = -aws * (filter_horiz_zoom - currprefs.gfx_filteroverlay_overscan * 10) / 2000;
	extrah = -ahs * (filter_vert_zoom - currprefs.gfx_filteroverlay_overscan * 10) / 2000;

	SetRect (sr, 0, 0, dst_width, dst_height);
	SetRect (zr, 0, 0, 0, 0);
	dr->left = (temp_width - aws) / 2;
	dr->top = (temp_height - ahs) / 2;

	filteroffsetx = 0;
	filteroffsety = 0;
	float xmult = filter_horiz_zoom_mult;
	float ymult = filter_vert_zoom_mult;

	dr->left -= (dst_width - aws) / 2;
	dr->top -= (dst_height - ahs) / 2;
	dr->right = dr->left + dst_width;
	dr->bottom = dr->top + dst_height;

	srcratio = 4.0 / 3.0;
	if (currprefs.gfx_filter_aspect > 0) {
		dstratio = (currprefs.gfx_filter_aspect >> 8) * 1.0 / (currprefs.gfx_filter_aspect & 0xff);
	} else if (currprefs.gfx_filter_aspect < 0) {
		if (isfullscreen () && deskw > 0 && deskh > 0)
			dstratio = 1.0 * deskw / deskh;
		else
			dstratio = 1.0 * dst_width / dst_height;
	} else {
		dstratio = srcratio;
	}

	int scalemode = currprefs.gfx_filter_autoscale;

	if (scalemode == AUTOSCALE_STATIC_AUTO) {
		if (currprefs.gfx_afullscreen) {
			scalemode = AUTOSCALE_STATIC_NOMINAL;
		} else {
			int w1 = (800 / 2) << currprefs.gfx_resolution;
			int w2 = (640 / 2) << currprefs.gfx_resolution;
			int h1 = (600 / 2) << currprefs.gfx_vresolution;
			int h2 = (400 / 2) << currprefs.gfx_vresolution;
			int w = currprefs.gfx_size_win.width;
			int h = currprefs.gfx_size_win.height;
			if (w <= w1 && h <= h1 && w >= w2 && h >= h2)
				scalemode = AUTOSCALE_NONE;
			else
				scalemode = AUTOSCALE_STATIC_NOMINAL;
		}
	}

	if (scalemode) {
		int cw, ch, cx, cy, cv;
		static int oxmult, oymult;

		filterxmult = 1000 / scale;
		filterymult = 1000 / scale;

		if (scalemode == AUTOSCALE_STATIC_MAX || scalemode == AUTOSCALE_STATIC_NOMINAL) {
			cw = (752 / 2) << currprefs.gfx_resolution;
			ch = (572 / 2) << currprefs.gfx_vresolution;
			cx = 0;
			cy = 0;
			cv = 1;
			if (scalemode == AUTOSCALE_STATIC_NOMINAL) {
				cw -= 40 << currprefs.gfx_resolution;
				ch -= 25 << currprefs.gfx_vresolution;
				cx = 28 << currprefs.gfx_resolution;
				cy = 10 << currprefs.gfx_vresolution;
			}
		} else if (scalemode == AUTOSCALE_MANUAL) {
			int v;

			changed_prefs.gfx_filter_horiz_offset = currprefs.gfx_filter_horiz_offset = 0;
			changed_prefs.gfx_filter_vert_offset = currprefs.gfx_filter_vert_offset = 0;
			filter_horiz_offset = 0;
			filter_vert_offset = 0;

			get_custom_topedge (&cx, &cy);
			//write_log (L"%dx%d %dx%d\n", cx, cy, currprefs.gfx_resolution, currprefs.gfx_vresolution);

			v = currprefs.gfx_xcenter_pos;
			if (v >= 0)
				cx = (v >> (RES_MAX - currprefs.gfx_resolution)) - cx;

			v = currprefs.gfx_ycenter_pos;
			if (v >= 0)
				cy = (v >> (VRES_MAX - currprefs.gfx_vresolution)) - cy;

			v = currprefs.gfx_xcenter_size;
			if (v <= 0)
				cw = 752 * 2;
			else
				cw = v;
			cw >>=  (RES_MAX - currprefs.gfx_resolution);

			v = currprefs.gfx_ycenter_size;
			if (v <= 0)
				ch = 572;
			else
				ch = v;
			ch >>= (VRES_MAX - currprefs.gfx_vresolution);

			//write_log (L"%dx%d %dx%d %dx%d\n", currprefs.gfx_xcenter_pos, currprefs.gfx_ycenter_pos, cx, cy, cw, ch);

			cv = 1;

		} else {

			cv = get_custom_limits (&cw, &ch, &cx, &cy);

		}

		if (currprefs.gfx_api == 0) {
			if (cx < 0)
				cx = 0;
			if (cy < 0)
				cy = 0;
		}

		if (cv) {
			int diff;

			if (scalemode == AUTOSCALE_CENTER) {

				int ww = cw * scale;
				int hh = ch * scale;
				SetRect (sr, 0, 0, dst_width, dst_height);
				SetRect (zr, 0, 0, 0, 0);
				dr->left = dr->top = 0;
				dr->right = dr->left + dst_width;
				dr->bottom = dr->top + dst_height;
				OffsetRect (zr, cx * scale - (dst_width - ww) / 2, cy * scale - (dst_height - hh) / 2);
				goto cont;

			} else if (scalemode == AUTOSCALE_RESIZE && isfullscreen () == 0 && !currprefs.gfx_filteroverlay[0]) {

				static int lastresize = 0;
				static int lastdelay = 1;
				static int ocw, och, ocx, ocy, lcw, lch, lcx, lcy;
				int useold = 0;

				lastresize--;
				if (lastresize > 0) {
					if (cw != lcw || ch != lch || cx != lcx || cy != lcy)
						lastresize = 10;
					useold = 1;
				} else if (lastdelay == 0) {
					lastdelay = 2;
					useold = 1;
				} else if (lastdelay > 0) {
					lastdelay--;
					useold = 1;
					if (lastdelay == 0) {
						lastdelay = -1;
						useold = 0;
					}
				}

				lcw = cw;
				lch = ch;
				lcx = cx;
				lcy = cy;
				if (useold && !config_changed) {
					cw = ocw;
					ch = och;
					cx = ocx;
					cy = ocy;
				} else {
					ocw = cw;
					och = ch;
					ocx = cx;
					ocy = cy;
					lastresize = 10;
					lastdelay = 0;
				}
				double scalex = currprefs.gfx_filter_horiz_zoom_mult ? 1000.0 / currprefs.gfx_filter_horiz_zoom_mult : 1.0;
				double scaley = currprefs.gfx_filter_vert_zoom_mult ? 1000.0 / currprefs.gfx_filter_vert_zoom_mult : 1.0;
				SetRect (sr, 0, 0, cw * scale * scalex, ch * scale * scaley);
				dr->left = (temp_width - aws) /2;
				dr->top = (temp_height - ahs) / 2;
				dr->right = dr->left + cw * scale;
				dr->bottom = dr->top + ch * scale;
				OffsetRect (zr, cx * scale, cy * scale);
				int ww = (dr->right - dr->left) * scalex;
				int hh = (dr->bottom - dr->top) * scaley;
				if (currprefs.gfx_xcenter_size >= 0)
					ww = currprefs.gfx_xcenter_size;
				if (currprefs.gfx_ycenter_size >= 0)
					hh = currprefs.gfx_ycenter_size;
				int oldwinw = currprefs.gfx_size_win.width;
				int oldwinh = currprefs.gfx_size_win.width;
				changed_prefs.gfx_size_win.width = ww;
				changed_prefs.gfx_size_win.height = hh;
				fixup_prefs_dimensions (&changed_prefs);
				if (oldwinw != changed_prefs.gfx_size_win.width || oldwinh != changed_prefs.gfx_size_win.height)
					config_changed = 1;
				OffsetRect (zr, -(changed_prefs.gfx_size_win.width - ww + 1) / 2, -(changed_prefs.gfx_size_win.height - hh + 1) / 2);
				filteroffsetx = -zr->left / scale;
				filteroffsety = -zr->top / scale;
				goto end;
			}

			dr->left = (temp_width - aws) /2;
			dr->top =  (temp_height - ahs) / 2;
			dr->right = dr->left + dst_width * scale;
			dr->bottom = dr->top + dst_height * scale;

			OffsetRect (zr, cx * scale, cy * scale);

			sizeoffset (dr, zr, extraw, extrah);

			dr->right -= (dst_width - cw) * scale;
			dr->bottom -= (dst_height - ch) * scale;

			filteroffsetx = -zr->left / scale;
			filteroffsety = -zr->top / scale;

			if (currprefs.gfx_filter_keep_aspect || currprefs.gfx_filter_aspect != 0) {
				int diffx = dr->right - dr->left;
				int diffy = dr->bottom - dr->top;
				float xmult = 1.0;
				float ymult = 1.0;

				if (currprefs.gfx_filter_keep_aspect) {
					dstratio = dstratio * (aws * 1.0 / ahs2) / (cw * 1.0 / ch);
					if (currprefs.ntscmode) {
						dstratio = dstratio * 1.21;
						if (currprefs.gfx_filter_keep_aspect == 2 && ispal ())
							dstratio = dstratio * 0.93;
						else if (currprefs.gfx_filter_keep_aspect == 1 && !ispal ())
							dstratio = dstratio * 0.98;
					} else {
						if (currprefs.gfx_filter_keep_aspect == 2 && ispal ())
							dstratio = dstratio * 0.95;
						else if (currprefs.gfx_filter_keep_aspect == 1 && !ispal ())
							dstratio = dstratio * 0.95;
					}
				}

				if (srcratio > dstratio) {
					ymult = ymult * srcratio / dstratio;
				} else {
					xmult = xmult * dstratio / srcratio;
				}

				diff = diffx - diffx * xmult;
				sizeoffset (dr, zr, diff, 0);
				filteroffsetx += diff / 2;

				diff = diffy - diffy * ymult;
				sizeoffset (dr, zr, 0, diff);
				filteroffsety += diff / 2;
			}

			v = filter_horiz_offset;
			OffsetRect (zr, (int)(-v * aws / 1000.0), 0);
			v = filter_vert_offset;
			OffsetRect (zr, 0, (int)(-v * ahs / 1000.0));

			diff = dr->right - dr->left;
			filterxmult = diff * 1000 / (dst_width * scale);
			diff = dr->bottom - dr->top;
			filterymult = diff * 1000 / (dst_height * scale);
			goto end;
		}

	}
cont:
	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {

		sizeoffset (dr, zr, extraw, extrah);

		if (currprefs.gfx_filter_keep_aspect) {
			float xm, ym, m;

			xm = 1.0 * aws / dst_width;
			ym = 1.0 * ahs / dst_height;
			if (xm < ym)
				xm = ym;
			else
				ym = xm;
			xmult = ymult = xm * 1000.0;

			m = (aws * 1.0 / dst_width) / (ahs * 1.0 / dst_height);
			dstratio = dstratio * m;
		}

	}

	if (currprefs.ntscmode) {
		if (currprefs.gfx_filter_keep_aspect == 2 && ispal ())
			dstratio = dstratio * 0.93;
		else if (currprefs.gfx_filter_keep_aspect == 1 && !ispal ())
			dstratio = dstratio * 0.98;
	} else {
		if (currprefs.gfx_filter_keep_aspect == 2 && ispal ())
			dstratio = dstratio * 0.95;
		else if (currprefs.gfx_filter_keep_aspect == 1 && !ispal ())
			dstratio = dstratio * 0.95;
	}

	if (srcratio > dstratio) {
		ymult = ymult * srcratio / dstratio;
	} else {
		xmult = xmult * dstratio / srcratio;
	}

	if (xmult <= 0.01)
		xmult = aws * 1000 / dst_width;
	else
		xmult = xmult + xmult * filter_horiz_zoom / 2000;
	if (ymult <= 0.01)
		ymult = ahs * 1000 / dst_height;
	else
		ymult = ymult + ymult * filter_vert_zoom / 2000;

	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {
		if (currprefs.ntscmode) {
			int v = vblscale2 (ahs);
			ymult /= 1.21;
			OffsetRect (dr, 0, (v - ahs2) / 2);
		}
	}

	ymult = vblscale (ymult);
	OffsetRect (dr, 0, (ahs2 - ahs) / 2);

	v = filter_horiz_offset;
	OffsetRect (zr, (int)(-v * aws / 1000.0), 0);
	v = filter_vert_offset;
	OffsetRect (zr, 0, (int)(-v * ahs / 1000.0));

	xs = dst_width - dst_width * xmult / 1000;
	ys = dst_height - dst_height * ymult / 1000;
	sizeoffset (dr, zr, xs, ys);

	filterxmult = xmult;
	filterymult = ymult;
	filteroffsetx += (dst_width - aw * 1000 / filterxmult) / 2;
	filteroffsety += (dst_height - ah * 1000 / filterymult) / 2;

end:
	fpux_restore (&fpuv);

}

uae_u8 *getfilterbuffer (int *widthp, int *heightp, int *pitch, int *depth)
{

	*widthp = 0;
	*heightp = 0;
	*depth = amiga_depth;
	if (usedfilter == NULL)
		return NULL;
	*widthp = gfxvidinfo.width;
	*heightp = gfxvidinfo.height;
	if (pitch)
		*pitch = gfxvidinfo.rowbytes;
	*depth = gfxvidinfo.pixbytes * 8;
	return gfxvidinfo.bufmem;
#if 0
	RECT dr, sr, zr;
	uae_u8 *p;
	int w, h;
	if (usedfilter->type == UAE_FILTER_DIRECT3D) {
		return getfilterbuffer3d (widthp, heightp, pitch, depth);
	} else {
		getfilterrect2 (&dr, &sr, &zr, dst_width, dst_height, amiga_width, amiga_height, scale, temp_width, temp_height);
	}
	w = sr.right - sr.left;
	h = sr.bottom - sr.top;
	p = gfxvidinfo.bufmem;
	if (pitch)
		*pitch = gfxvidinfo.rowbytes;
	p += (zr.top - (h / 2)) * gfxvidinfo.rowbytes + (zr.left - (w / 2)) * amiga_depth / 8;
	*widthp = w;
	*heightp = h;
	return p;
#endif
}

static void statusline (void)
{
	DDSURFACEDESC2 desc;
	RECT sr, dr;
	int y;
	int lx, ly, sx;

	if (!(currprefs.leds_on_screen & STATUSLINE_CHIPSET) || !tempsurf)
		return;
	lx = dst_width;
	ly = dst_height;
	sx = lx;
	if (sx > dst_width)
		sx = dst_width;
	SetRect (&sr, 0, 0, sx, TD_TOTAL_HEIGHT);
	SetRect (&dr, lx - sx, ly - TD_TOTAL_HEIGHT, lx, ly);
	DirectDraw_BlitRect (tempsurf, &sr, NULL, &dr);
	if (locksurface (tempsurf, &desc)) {
		for (y = 0; y < TD_TOTAL_HEIGHT; y++) {
			uae_u8 *buf = (uae_u8*)desc.lpSurface + y * desc.lPitch;
			draw_status_line_single (buf, dst_depth / 8, y, sx, rc, gc, bc, NULL);
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

void S2X_reset (void)
{
	if (!inited)
		return;
	S2X_init (dst_width2, dst_height2, amiga_width2, amiga_height2, dst_depth2, amiga_depth2);
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
	scale = 1;
	inited = false;
}

void S2X_init (int dw, int dh, int aw, int ah, int ad, int dd)
{
	int flags = 0;

	dst_width2 = dw;
	dst_height2 = dh;
	dst_depth2 = dd;
	amiga_width2 = aw;
	amiga_height2 = ah;
	amiga_depth2 = ad;


	S2X_free ();
	d3d = currprefs.gfx_api;
	changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen | STATUSLINE_TARGET;

	if (d3d)
		dd = ad;

	if (dd == 32)
		alloc_colors_rgb (8, 8, 8, 16, 8, 0, 0, 0, 0, 0, rc, gc, bc);
	else
		alloc_colors_rgb (5, 6, 5, 11, 5, 0, 0, 0, 0, 0, rc, gc, bc);


	if (!currprefs.gfx_filter || !usedfilter) {
		usedfilter = &uaefilters[0];
		scale = 1;
	} else {
		scale = usedfilter->intmul;
		flags = usedfilter->flags;
		if ((ad == 16 && !(flags & UAE_FILTER_MODE_16)) || (ad == 32 && !(flags & UAE_FILTER_MODE_32))) {
			usedfilter = &uaefilters[0];
			scale = 1;
			changed_prefs.gfx_filter = usedfilter->type;
		}
	}
#if 0
	int res_shift;
	res_shift = RES_MAX - currprefs.gfx_resolution;
	if (currprefs.gfx_xcenter_size > 0 && (currprefs.gfx_xcenter_size >> res_shift) < aw)
		aw = currprefs.gfx_xcenter_size >> res_shift;
	res_shift = VRES_MAX - currprefs.gfx_vresolution;
	if (currprefs.gfx_ycenter_size > 0 && (currprefs.gfx_ycenter_size >> res_shift) < ah)
		ah = currprefs.gfx_ycenter_size >> res_shift;
#endif
	dst_width = dw;
	dst_height = dh;
	dst_depth = dd;
	amiga_width = aw;
	amiga_height = ah;
	amiga_depth = ad;

	if (d3d) {
		int m = currprefs.gfx_filter_filtermode + 1;
		if (m < scale)
			m = scale;
		temp_width = dst_width * m;
		temp_height = dst_height * m;
	} else {
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
	}

	if (usedfilter->type == UAE_FILTER_HQ2X || usedfilter->type == UAE_FILTER_HQ3X || usedfilter->type == UAE_FILTER_HQ4X) {
		int w = amiga_width > dst_width ? amiga_width : dst_width;
		int h = amiga_height > dst_height ? amiga_height : dst_height;
		tempsurf2 = xmalloc (uae_u8, w * h * (amiga_depth / 8) * ((scale + 1) / 2));
		tempsurf3 = xmalloc (uae_u8, w * h *(dst_depth / 8) * 4 * scale);
		if (!d3d)
			tempsurf = allocsystemsurface (temp_width, temp_height);
	} else {
		if (!d3d)
			tempsurf = allocsurface (temp_width, temp_height);
	}
	if (!tempsurf && !d3d)
		write_log (L"DDRAW: failed to create temp surface (%dx%d)\n", temp_width, temp_height);
	inited = true;
}

void S2X_render (void)
{
	int aw, ah, aws, ahs;
	uae_u8 *dptr, *enddptr, *sptr, *endsptr;
	int ok = 0;
	RECT sr, dr, zr;
	DDSURFACEDESC2 desc;
	int pitch;
	uae_u8 *surfstart;

	aw = amiga_width;
	ah = amiga_height;
	aws = aw * scale;
	ahs = ah * scale;

	if (ah < 16)
		return;
	if (aw < 16)
		return;

	sptr = gfxvidinfo.bufmem;
	endsptr = gfxvidinfo.bufmemend;
	bufmem_ptr = sptr;

	if (d3d) {
		surfstart = D3D_locktexture (&pitch, true);
		if (surfstart == NULL)
			return;
	} else {
		if (tempsurf == NULL)
			return;
		if (cleartemp) {
			clearsurface (tempsurf);
			cleartemp = 0;
		}
		if (!locksurface (tempsurf, &desc))
			return;
		pitch = desc.lPitch;
		surfstart = (uae_u8*)desc.lpSurface;
	}
	dptr = surfstart;
	enddptr = dptr + pitch * temp_height;
	if (!d3d) {
		dptr = getfilterrect1 (&sr, &dr, dst_width, dst_height, dst_depth, aw, ah, scale, temp_width, temp_height, dptr, pitch);
	}

	if (!dptr) /* weird things can happen */
		goto end;
	if (dptr < surfstart)
		dptr = surfstart;

	if (usedfilter->type == UAE_FILTER_SCALE2X) { /* 16+32/2X */

		if (amiga_depth == 16 && dst_depth == 16) {
			AdMame2x (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
			ok = 1;
		} else if (amiga_depth == 32 && dst_depth == 32) {
			AdMame2x32 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
			ok = 1;
		}

	} else if (usedfilter->type == UAE_FILTER_HQ2X || usedfilter->type == UAE_FILTER_HQ3X || usedfilter->type == UAE_FILTER_HQ4X) { /* 32/2X+3X+4X */

#ifndef CPU_64_BIT

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
				if (usedfilter->type == UAE_FILTER_HQ2X)
					hq2x_32 (tempsurf2, tempsurf3, aw, ah, aws * 4);
				else if (usedfilter->type == UAE_FILTER_HQ3X)
					hq3x_32 (tempsurf2, tempsurf3, aw, ah, aws * 4);
				else if (usedfilter->type == UAE_FILTER_HQ4X)
					hq4x_32 (tempsurf2, tempsurf3, aw, ah, aws * 4);
				ok = 1;
			} else if (amiga_depth == 16 && dst_depth == 16) {
				if (usedfilter->type == UAE_FILTER_HQ2X)
					hq2x_16 (tempsurf2, tempsurf3, aw, ah, aws * 2);
				else if (usedfilter->type == UAE_FILTER_HQ3X)
					hq3x_16 (tempsurf2, tempsurf3, aw, ah, aws * 2);
				else if (usedfilter->type == UAE_FILTER_HQ4X)
					hq4x_16 (tempsurf2, tempsurf3, aw, ah, aws * 2);
				ok = 1;
			}
			for (i = 0; i < ah * scale; i++) {
				int w = aw * scale * (dst_depth / 8);
				if (dptr + w > enddptr)
					break;
				memcpy (dptr, sptr2, w);
				sptr2 += w;
				dptr += pitch;
			}
		}
#endif

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

	if (ok == 0 && currprefs.gfx_filter) {
		usedfilter = &uaefilters[0];
		changed_prefs.gfx_filter = usedfilter->type;
	}

end:
	if (d3d) {
		;//D3D_unlocktexture (); unlock in win32gfx.c
	} else {
		unlocksurface (tempsurf);
	
		getfilterrect2 (&dr, &sr, &zr, dst_width, dst_height, aw, ah, scale, temp_width, temp_height);
		//write_log (L"(%d %d %d %d) - (%d %d %d %d) (%d %d)\n", dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom, zr.left, zr.top);
		OffsetRect (&sr, zr.left, zr.top);
		if (sr.left >= 0 && sr.top >= 0 && sr.right < temp_width && sr.bottom < temp_height) {
			if (sr.left < sr.right && sr.top < sr.bottom)
				DirectDraw_BlitRect (NULL, &dr, tempsurf, &sr);
		}
		statusline ();
	}
}

void S2X_refresh (void)
{
	clearsurface (NULL);
	S2X_render ();
}

int S2X_getmult (void)
{
	if (!usedfilter)
		return 1;
	if (screen_is_picasso)
		return 1;
	return usedfilter->intmul;
}

#endif
