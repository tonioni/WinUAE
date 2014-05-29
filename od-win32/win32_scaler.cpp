
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

#define AUTORESIZE_FRAME_DELAY 10

struct uae_filter uaefilters[] =
{
	{ UAE_FILTER_NULL, 0, 1, _T("Null filter"), _T("null"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32 },

	{ UAE_FILTER_SCALE2X, 0, 2, _T("Scale2X"), _T("scale2x"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32 },

	{ UAE_FILTER_HQ2X, 0, 2, _T("hq2x"), _T("hq2x"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, },

	{ UAE_FILTER_HQ3X, 0, 3, _T("hq3x"), _T("hq3x"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_HQ4X, 0, 4, _T("hq4x"), _T("hq4x"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_SUPEREAGLE, 0, 2, _T("SuperEagle"), _T("supereagle"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_SUPER2XSAI, 0, 2, _T("Super2xSaI"), _T("super2xsai"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_2XSAI, 0, 2, _T("2xSaI"), _T("2xsai"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32 },

	{ UAE_FILTER_PAL, 1, 1, _T("PAL"), _T("pal"), UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32 },

	{ 0 }
};

static float filteroffsetx, filteroffsety, filterxmult = 1.0, filterymult = 1.0;
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

void getfilteroffset (float *dx, float *dy, float *mx, float *my)
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

static uae_u8 *getfilterrect1 (RECT *sr, RECT *dr, int dst_width, int dst_height, int dst_depth, int aw, int ah, int scale, int temp_width, int temp_height, uae_u8 *dptr, int pitch)
{
	int aws, ahs;

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

static void getmanualpos (int *cxp, int *cyp, int *cwp, int *chp)
{
	int v, cx, cy, cw, ch;
	bool native = isnativevidbuf ();

	cx = *cxp;
	cy = *cyp;
	v = currprefs.gfx_xcenter_pos;
	if (v >= 0)
		cx = (v >> (RES_MAX - currprefs.gfx_resolution)) - cx;

	v = currprefs.gfx_ycenter_pos;
	if (v >= 0)
		cy = (v >> (VRES_MAX - currprefs.gfx_vresolution)) - cy;

	v = currprefs.gfx_xcenter_size;
	if (v <= 0) {
		if (programmedmode && native) {
			cw = gfxvidinfo.outbuffer->outwidth << (RES_MAX - currprefs.gfx_resolution);
		} else {
			cw = native ? AMIGA_WIDTH_MAX << RES_MAX : gfxvidinfo.outbuffer->outwidth;
		}
	} else {
		cw = v;
	}
	cw >>=  (RES_MAX - currprefs.gfx_resolution);

	v = currprefs.gfx_ycenter_size;
	if (v <= 0) {
		if (programmedmode && native) {
			ch = gfxvidinfo.outbuffer->outheight << (VRES_MAX - currprefs.gfx_vresolution);
		} else {
			ch = native ? AMIGA_HEIGHT_MAX << VRES_MAX : gfxvidinfo.outbuffer->outheight;
		}
	} else {
		ch = v;
	}
	ch >>= (VRES_MAX - currprefs.gfx_vresolution);

	*cxp = cx;
	*cyp = cy;
	*cwp = cw;
	*chp = ch;
}

extern bool getscalerect (float *mx, float *my, float *sx, float *sy);

void getfilterrect2 (RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int temp_width, int temp_height)
{
	float srcratio, dstratio;
	int aws, ahs;
	int xs, ys;
	int extraw, extrah;
	int fpuv;
	bool specialmode = !isnativevidbuf ();
	float mrmx, mrmy, mrsx, mrsy;
	int extraw2;
	bool doautoaspect = false;
	float autoaspectratio;

	float filter_horiz_zoom = currprefs.gf[picasso_on].gfx_filter_horiz_zoom / 1000.0f;
	float filter_vert_zoom = currprefs.gf[picasso_on].gfx_filter_vert_zoom / 1000.0f;
	float filter_horiz_zoom_mult = currprefs.gf[picasso_on].gfx_filter_horiz_zoom_mult;
	float filter_vert_zoom_mult = currprefs.gf[picasso_on].gfx_filter_vert_zoom_mult;
	float filter_horiz_offset = currprefs.gf[picasso_on].gfx_filter_horiz_offset / 10000.0f;
	float filter_vert_offset = currprefs.gf[picasso_on].gfx_filter_vert_offset / 10000.0f;

	store_custom_limits (-1, -1, -1, -1);

	if (!usedfilter && !currprefs.gfx_api) {
		filter_horiz_zoom = filter_vert_zoom = 0.0;
		filter_horiz_zoom_mult = filter_vert_zoom_mult = 1.0;
		filter_horiz_offset = filter_vert_offset = 0.0;
	}

	if (screen_is_picasso) {
		getrtgfilterrect2 (sr, dr, zr, dst_width, dst_height);
		return;
	}

	fpux_save (&fpuv);

	getinit ();
	aws = aw * scale;
	ahs = ah * scale;
	//write_log (_T("%d %d %d\n"), dst_width, temp_width, aws);
	extraw = -aws * (filter_horiz_zoom - currprefs.gf[picasso_on].gfx_filteroverlay_overscan * 10) / 2.0f;
	extrah = -ahs * (filter_vert_zoom - currprefs.gf[picasso_on].gfx_filteroverlay_overscan * 10) / 2.0f;

	extraw2 = 0;
	if (getscalerect (&mrmx, &mrmy, &mrsx, &mrsy)) {
		extraw2 = mrmx;
		//extrah -= mrmy;
	}

	SetRect (sr, 0, 0, dst_width, dst_height);
	SetRect (zr, 0, 0, 0, 0);
	dr->left = (temp_width - aws) / 2;
	dr->top = (temp_height - ahs) / 2;

	dr->left -= (dst_width - aws) / 2;
	dr->top -= (dst_height - ahs) / 2;

	dr->right = dr->left + dst_width;
	dr->bottom = dr->top + dst_height;

	filteroffsetx = 0;
	filteroffsety = 0;
	float xmult = filter_horiz_zoom_mult;
	float ymult = filter_vert_zoom_mult;

	srcratio = 4.0f / 3.0f;
	if (currprefs.gf[picasso_on].gfx_filter_aspect > 0) {
		dstratio = (currprefs.gf[picasso_on].gfx_filter_aspect / ASPECTMULT) * 1.0f / (currprefs.gf[picasso_on].gfx_filter_aspect & (ASPECTMULT - 1));
	} else if (currprefs.gf[picasso_on].gfx_filter_aspect < 0) {
		if (isfullscreen () && deskw > 0 && deskh > 0)
			dstratio = 1.0f * deskw / deskh;
		else
			dstratio = 1.0f * dst_width / dst_height;
	} else {
		dstratio = srcratio;
	}

	int scalemode = currprefs.gf[picasso_on].gfx_filter_autoscale;
	int oscalemode = changed_prefs.gf[picasso_on].gfx_filter_autoscale;

	if (!specialmode && scalemode == AUTOSCALE_STATIC_AUTO) {
		if (currprefs.gfx_apmode[0].gfx_fullscreen) {
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

	bool scl = false;

	if (scalemode) {
		int cw, ch, cx, cy, cv, crealh = 0;
		static int oxmult, oymult;

		filterxmult = scale;
		filterymult = scale;

		if (scalemode == AUTOSCALE_STATIC_MAX || scalemode == AUTOSCALE_STATIC_NOMINAL || scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {

			if (specialmode) {
				cx = 0;
				cy = 0;
				cw = gfxvidinfo.outbuffer->outwidth;
				ch = gfxvidinfo.outbuffer->outheight;
			} else {
				cx = 0;
				cy = 0;
				cw = gfxvidinfo.drawbuffer.inwidth;
				ch = gfxvidinfo.drawbuffer.inheight;
				cv = 1;
				if (!(beamcon0 & 0x80) && (scalemode == AUTOSCALE_STATIC_NOMINAL)) { // || scalemode == AUTOSCALE_INTEGER)) {
					cx = 28 << currprefs.gfx_resolution;
					cy = 10 << currprefs.gfx_vresolution;
					cw -= 40 << currprefs.gfx_resolution;
					ch -= 25 << currprefs.gfx_vresolution;
				}
				set_custom_limits (cw, ch, cx, cy);
				store_custom_limits (cw, ch, cx, cy);
				scl = true;
			}

			if (scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {
				int maxw = currprefs.gfx_size.width;
				int maxh = currprefs.gfx_size.height;
				int mult = 1;
				bool ok = true;

				if (currprefs.gfx_xcenter_pos >= 0 || currprefs.gfx_ycenter_pos >= 0) {
					changed_prefs.gf[picasso_on].gfx_filter_horiz_offset = currprefs.gf[picasso_on].gfx_filter_horiz_offset = 0.0;
					changed_prefs.gf[picasso_on].gfx_filter_vert_offset = currprefs.gf[picasso_on].gfx_filter_vert_offset = 0.0;
					filter_horiz_offset = 0.0;
					filter_vert_offset = 0.0;
					get_custom_topedge (&cx, &cy, false);
				}

				if (scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {
					ok = get_custom_limits (&cw, &ch, &cx, &cy, &crealh) != 0;
					if (ok)
						store_custom_limits (cw, ch, cx, cy);
				}
				if (scalemode == AUTOSCALE_INTEGER || ok == false) {
					getmanualpos (&cx, &cy, &cw, &ch);
					store_custom_limits (cw, ch, cx, cy);
				}

				int cw2 = cw + cw * filter_horiz_zoom;
				int ch2 = ch + ch * filter_vert_zoom;

				extraw = 0;
				extrah = 0;
				xmult = 1.0;
				ymult = 1.0;
				filter_horiz_zoom = 0;
				filter_vert_zoom = 0;
				filter_horiz_zoom_mult = 1.0;
				filter_vert_zoom_mult = 1.0;

				if (cw2 > maxw || ch2 > maxh) {
					while (cw2 / mult > maxw || ch2 / mult > maxh)
						mult *= 2;
					maxw = maxw * mult;
					maxh = maxh * mult;
				} else {
					while (cw2 * (mult + 1) <= maxw && ch2 * (mult + 1) <= maxh)
						mult++;
					maxw = (maxw + mult - 1) / mult;
					maxh = (maxh + mult - 1) / mult;
				}
				//write_log(_T("(%dx%d) (%dx%d) ww=%d hh=%d w=%d h=%d m=%d\n"), cx, cy, cw, ch, currprefs.gfx_size.width, currprefs.gfx_size.height, maxw, maxh, mult);
				cx -= (maxw - cw) / 2;
				cw = maxw;
				cy -= (maxh - ch) / 2;
				ch = maxh;
			}

		} else if (scalemode == AUTOSCALE_MANUAL) {

			changed_prefs.gf[picasso_on].gfx_filter_horiz_offset = currprefs.gf[picasso_on].gfx_filter_horiz_offset = 0.0;
			changed_prefs.gf[picasso_on].gfx_filter_vert_offset = currprefs.gf[picasso_on].gfx_filter_vert_offset = 0.0;
			filter_horiz_offset = 0.0;
			filter_vert_offset = 0.0;

			get_custom_topedge (&cx, &cy, currprefs.gfx_xcenter_pos < 0 && currprefs.gfx_ycenter_pos < 0);
			//write_log (_T("%dx%d %dx%d\n"), cx, cy, currprefs.gfx_resolution, currprefs.gfx_vresolution);

			getmanualpos (&cx, &cy, &cw, &ch);
			set_custom_limits (cw, ch, cx, cy);
			store_custom_limits (cw, ch, cx, cy);
			scl = true;

			//write_log (_T("%dx%d %dx%d %dx%d\n"), currprefs.gfx_xcenter_pos, currprefs.gfx_ycenter_pos, cx, cy, cw, ch);

			cv = 1;

		} else if (scalemode == AUTOSCALE_CENTER || scalemode == AUTOSCALE_RESIZE) {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh);
			if (cv)
				store_custom_limits (cw, ch, cx, cy);

		} else {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh);
			if (cv) {
				set_custom_limits (cw, ch, cx, cy);
				store_custom_limits (cw, ch, cx, cy);
				scl = true;
			}

		}

		if (!scl) {
			set_custom_limits (-1, -1, -1, -1);
		}
	
		autoaspectratio = 0;
		if (currprefs.gf[picasso_on].gfx_filter_keep_autoscale_aspect && cw > 0 && ch > 0 && crealh > 0 && (scalemode == AUTOSCALE_NORMAL || scalemode == AUTOSCALE_INTEGER_AUTOSCALE || scalemode == AUTOSCALE_MANUAL)) {
			float cw2 = cw;
			float ch2 = ch;
			int res = currprefs.gfx_resolution - currprefs.gfx_vresolution;

			if (res < 0)
				cw2 *= 1 << (-res);
			else if (res > 0)
				cw2 /= 1 << res;
			autoaspectratio = (cw2 / ch2) / (4.0 / 3.0);
			doautoaspect = true;
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

				dr->left = (temp_width - aws) /2;
				dr->top =  (temp_height - ahs) / 2;
				dr->right = dr->left + dst_width * scale;
				dr->bottom = dr->top + dst_height * scale;

				OffsetRect (zr, cx * scale - (dst_width - ww) / 2, cy * scale - (dst_height - hh) / 2);
				goto cont;

			} else if (scalemode == AUTOSCALE_RESIZE && isfullscreen () == 0 && !currprefs.gf[picasso_on].gfx_filteroverlay[0]) {

				static int lastresize = 0;
				static int lastdelay = 1;
				static int ocw, och, ocx, ocy, lcw, lch, lcx, lcy;
				int useold = 0;

				lastresize--;
				if (lastresize > 0) {
					if (cw != lcw || ch != lch || cx != lcx || cy != lcy)
						lastresize = AUTORESIZE_FRAME_DELAY;
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
					lastresize = AUTORESIZE_FRAME_DELAY;
					lastdelay = 0;
				}
				float scalex = currprefs.gf[picasso_on].gfx_filter_horiz_zoom_mult > 0 ? currprefs.gf[picasso_on].gfx_filter_horiz_zoom_mult : 1.0f;
				float scaley = currprefs.gf[picasso_on].gfx_filter_vert_zoom_mult > 0 ? currprefs.gf[picasso_on].gfx_filter_horiz_zoom_mult : 1.0f;
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
				if (scalemode == oscalemode) {
					int oldwinw = currprefs.gfx_size_win.width;
					int oldwinh = currprefs.gfx_size_win.height;
					changed_prefs.gfx_size_win.width = ww;
					changed_prefs.gfx_size_win.height = hh;
					fixup_prefs_dimensions (&changed_prefs);
					if (oldwinw != changed_prefs.gfx_size_win.width || oldwinh != changed_prefs.gfx_size_win.height)
						set_config_changed ();
				}
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

			bool aspect = false;
			int diffx = dr->right - dr->left;
			int diffy = dr->bottom - dr->top;

			xmult = 1.0;
			ymult = 1.0;

			if (currprefs.gf[picasso_on].gfx_filter_keep_aspect || currprefs.gf[picasso_on].gfx_filter_aspect != 0) {

				if (currprefs.gf[picasso_on].gfx_filter_keep_aspect) {
					if (currprefs.ntscmode) {
						dstratio = dstratio * 1.21f;
						if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 2 && ispal ())
							dstratio = dstratio * 0.93f;
						else if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 1 && !ispal ())
							dstratio = dstratio * 0.98f;
					} else {
						if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 2 && ispal ())
							dstratio = dstratio * 0.95f;
						else if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 1 && !ispal ())
							dstratio = dstratio * 0.95f;
					}
				}
				aspect = true;

			} else if (doautoaspect) {

				aspect = true;

			}

			if (aspect) {

				if (doautoaspect) {
					srcratio *= autoaspectratio;
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

			OffsetRect (zr, (int)(-filter_horiz_offset * aws), 0);
			OffsetRect (zr, 0, (int)(-filter_vert_offset * ahs));

			diff = dr->right - dr->left;
			filterxmult = diff / (dst_width * scale);
			diff = dr->bottom - dr->top;
			filterymult = diff / (dst_height * scale);
			goto end;
		}

	}
cont:

	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {

		sizeoffset (dr, zr, extraw, extrah);

		if (currprefs.gf[picasso_on].gfx_filter_keep_aspect) {
			float xm, ym, m;

			xm = aws / dst_width;
			ym = ahs / dst_height;
			if (xm < ym)
				xm = ym;
			else
				ym = xm;
			xmult = ymult = xm;

			m = (aws * dst_width) / (ahs * dst_height);
			dstratio = dstratio * m;
		}

	}

	if (currprefs.ntscmode) {
		if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 2 && ispal ())
			dstratio = dstratio * 0.93f;
		else if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 1 && !ispal ())
			dstratio = dstratio * 0.98f;
	} else {
		if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 2 && ispal ())
			dstratio = dstratio * 0.95f;
		else if (currprefs.gf[picasso_on].gfx_filter_keep_aspect == 1 && !ispal ())
			dstratio = dstratio * 0.95f;
	}

	if (srcratio > dstratio) {
		ymult = ymult * srcratio / dstratio;
	} else {
		xmult = xmult * dstratio / srcratio;
	}

	if (xmult <= 0.01)
		xmult = (float)dst_width / aws;
	else
		xmult = xmult + xmult * filter_horiz_zoom / 2.0f;
	if (ymult <= 0.01)
		ymult = (float)dst_height / ahs;
	else
		ymult = ymult + ymult * filter_vert_zoom / 2.0f;

	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {
		if (currprefs.ntscmode) {
			ymult /= 1.21f;
		}
	}

	OffsetRect (zr, (int)(-filter_horiz_offset * aws), 0);
	OffsetRect (zr, 0, (int)(-filter_vert_offset * ahs));

	xs = dst_width;
	if (xmult)
		xs -= dst_width / xmult;
	ys = dst_height;
	if (ymult)
		ys -= dst_height / ymult;
	sizeoffset (dr, zr, xs, ys);

	filterxmult = xmult;
	filterymult = ymult;
	filteroffsetx += (dst_width - aw / filterxmult) / 2;
	filteroffsety += (dst_height - ah / filterymult) / 2;

end:

	if (getscalerect (&mrmx, &mrmy, &mrsx, &mrsy)) {
		sizeoffset (dr, zr, mrmx, mrmy);
		OffsetRect (dr, mrsx, mrsy);
	}

	fpux_restore (&fpuv);

#if 0
	int rw, rh, rx, ry;
	get_custom_raw_limits (&rw, &rh, &rx, &ry);
	rw <<= RES_MAX - currprefs.gfx_resolution;
	rx <<= RES_MAX - currprefs.gfx_resolution;
	rh <<= VRES_MAX - currprefs.gfx_vresolution;
	ry <<= VRES_MAX - currprefs.gfx_vresolution;
	write_log (_T("%d %d %d %d\n"), rx, rw, ry, rh);
#endif

}

uae_u8 *getfilterbuffer (int *widthp, int *heightp, int *pitch, int *depth)
{
	struct vidbuffer *vb = gfxvidinfo.outbuffer;

	*widthp = 0;
	*heightp = 0;
	*depth = amiga_depth;
	if (usedfilter == NULL)
		return NULL;
	*widthp = vb->outwidth;
	*heightp = vb->outheight;
	if (pitch)
		*pitch = vb->rowbytes;
	*depth = vb->pixbytes * 8;
	return vb->bufmem;
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
	p = vb->bufmem;
	if (pitch)
		*pitch = vb->rowbytes;
	p += (zr.top - (h / 2)) * vb->rowbytes + (zr.left - (w / 2)) * amiga_depth / 8;
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
	int lx, ly;
	int slx, sly;

	if (!(currprefs.leds_on_screen & STATUSLINE_CHIPSET) || !tempsurf)
		return;
	statusline_getpos (&slx, &sly, dst_width, dst_height);
	lx = dst_width;
	ly = dst_height;
	SetRect (&sr, slx, 0, slx + lx, TD_TOTAL_HEIGHT);
	SetRect (&dr, slx, sly, slx + lx, sly + TD_TOTAL_HEIGHT);
	DirectDraw_BlitRect (tempsurf, &sr, NULL, &dr);
	if (DirectDraw_LockSurface (tempsurf, &desc)) {
		for (y = 0; y < TD_TOTAL_HEIGHT; y++) {
			uae_u8 *buf = (uae_u8*)desc.lpSurface + y * desc.lPitch;
			draw_status_line_single (buf, dst_depth / 8, y, lx, rc, gc, bc, NULL);
		}
		DirectDraw_UnlockSurface (tempsurf);
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
	S2X_init (dst_width2, dst_height2, amiga_depth2);
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
	filterxmult = 1.0;
	filterymult = 1.0;
	scale = 1;
	inited = false;
}

bool S2X_init (int dw, int dh, int dd)
{
	int flags = 0;
	struct vidbuffer *vb = gfxvidinfo.outbuffer;

	dst_width2 = dw;
	dst_height2 = dh;
	dst_depth2 = dd;
	amiga_width2 = vb->outwidth;
	amiga_height2 = vb->outheight;
	amiga_depth2 = vb->pixbytes * 8;


	S2X_free ();
	d3d = currprefs.gfx_api;
	changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen | STATUSLINE_TARGET;

	if (d3d)
		dd = amiga_depth2;

	if (dd == 32)
		alloc_colors_rgb (8, 8, 8, 16, 8, 0, 0, 0, 0, 0, rc, gc, bc);
	else
		alloc_colors_rgb (5, 6, 5, 11, 5, 0, 0, 0, 0, 0, rc, gc, bc);

	if (WIN32GFX_IsPicassoScreen ())
		return true;

	if (!currprefs.gf[picasso_on].gfx_filter || !usedfilter) {
		usedfilter = &uaefilters[0];
		scale = 1;
	} else {
		scale = usedfilter->intmul;
		flags = usedfilter->flags;
		if ((amiga_depth2 == 16 && !(flags & UAE_FILTER_MODE_16)) || (amiga_depth2 == 32 && !(flags & UAE_FILTER_MODE_32))) {
			usedfilter = &uaefilters[0];
			scale = 1;
			changed_prefs.gf[picasso_on].gfx_filter = usedfilter->type;
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
	amiga_width = vb->outwidth;
	amiga_height = vb->outheight;
	amiga_depth = vb->pixbytes * 8;

	if (d3d) {
		int m = currprefs.gf[picasso_on].gfx_filter_filtermode + 1;
		if (m < scale)
			m = scale;
		temp_width = dst_width * m;
		temp_height = dst_height * m;
	} else {
		temp_width = dst_width * 2;
		if (temp_width > dxcaps.maxwidth)
			temp_width = dxcaps.maxwidth;
		temp_height = dst_height * 2;
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
	if (!tempsurf && !d3d) {
		write_log (_T("DDRAW: failed to create temp surface (%dx%d)\n"), temp_width, temp_height);
		return false;
	}
	inited = true;
	return true;
}

void S2X_render (void)
{
	struct vidbuffer *vb = gfxvidinfo.outbuffer;
	int aw, ah, aws, ahs;
	uae_u8 *dptr, *enddptr, *sptr, *endsptr;
	int ok = 0;
	RECT sr, dr, zr;
	DDSURFACEDESC2 desc;
	int pitch, surf_height;
	uae_u8 *surfstart;

	aw = amiga_width;
	ah = amiga_height;
	aws = aw * scale;
	ahs = ah * scale;

	if (ah < 16)
		return;
	if (aw < 16)
		return;

	sptr = vb->bufmem;
	endsptr = vb->bufmemend;
	bufmem_ptr = sptr;

	if (d3d) {
		surfstart = D3D_locktexture (&pitch, &surf_height, true);
		if (surfstart == NULL)
			return;
	} else {
		if (tempsurf == NULL)
			return;
		if (cleartemp) {
			DirectDraw_ClearSurface (tempsurf);
			cleartemp = 0;
		}
		if (!DirectDraw_LockSurface (tempsurf, &desc))
			return;
		pitch = desc.lPitch;
		surfstart = (uae_u8*)desc.lpSurface;
		surf_height = desc.dwHeight;
	}
	dptr = surfstart;
	enddptr = dptr + pitch * surf_height;
	if (!d3d) {
		dptr = getfilterrect1 (&sr, &dr, dst_width, dst_height, dst_depth, aw, ah, scale, temp_width, temp_height, dptr, pitch);
	}

	if (!dptr) /* weird things can happen */
		goto end;
	if (dptr < surfstart)
		dptr = surfstart;

	if (usedfilter->type == UAE_FILTER_SCALE2X) { /* 16+32/2X */

		if (dptr + pitch * ah * 2 >= enddptr)
			ah = (enddptr - dptr) / (pitch * 2);

		if (amiga_depth == 16 && dst_depth == 16) {
			AdMame2x (sptr, vb->rowbytes, dptr, pitch, aw, ah);
			ok = 1;
		} else if (amiga_depth == 32 && dst_depth == 32) {
			AdMame2x32 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
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
				sptr += vb->rowbytes;
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

		if (dptr + pitch * ah * 2 >= enddptr)
			ah = (enddptr - dptr) / (pitch * 2);

		if (scale == 2 && amiga_depth == 16) {
			if (dst_depth == 16) {
				SuperEagle_16 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
				ok = 1;
			} else if (dst_depth == 32) {
				SuperEagle_32 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
				ok = 1;
			}
		}

	} else if (usedfilter->type == UAE_FILTER_SUPER2XSAI) { /* 16/32/2X */

		if (dptr + pitch * ah * 2 >= enddptr)
			ah = (enddptr - dptr) / (pitch * 2);

		if (scale == 2 && amiga_depth == 16) {
			if (dst_depth == 16) {
				Super2xSaI_16 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
				ok = 1;
			} else if (dst_depth == 32) {
				Super2xSaI_32 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
				ok = 1;
			}
		}

	} else if (usedfilter->type == UAE_FILTER_2XSAI) { /* 16/32/2X */

		if (dptr + pitch * ah * 2 >= enddptr)
			ah = (enddptr - dptr) / (pitch * 2);

		if (scale == 2 && amiga_depth == 16) {
			if (dst_depth == 16) {
				_2xSaI_16 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
				ok = 1;
			} else if (dst_depth == 32) {
				_2xSaI_32 (sptr, vb->rowbytes, dptr, pitch, aw, ah);
				ok = 1;
			}
		}

	} else if (usedfilter->type == UAE_FILTER_PAL) { /* 16/32/1X */

		if (amiga_depth == 32 && dst_depth == 32) {
			PAL_1x1_32 ((uae_u32*)sptr, vb->rowbytes, (uae_u32*)dptr, pitch, aw, ah);
			ok = 1;
		} else if (amiga_depth == 16 && dst_depth == 16) {
			PAL_1x1_16 ((uae_u16*)sptr, vb->rowbytes, (uae_u16*)dptr, pitch, aw, ah);
			ok = 1;
		}

	} else { /* null */

		if (amiga_depth == dst_depth) {
			int y;
			int w = aw * dst_depth / 8;
			for (y = 0; y < ah && dptr + w <= enddptr; y++) {
				memcpy (dptr, sptr, w);
				sptr += vb->rowbytes;
				dptr += pitch;
			}
		}
		ok = 1;

	}

	if (ok == 0 && currprefs.gf[picasso_on].gfx_filter) {
		usedfilter = &uaefilters[0];
		changed_prefs.gf[picasso_on].gfx_filter = usedfilter->type;
	}

end:
	if (d3d) {
		;//D3D_unlocktexture (); unlock in win32gfx.c
	} else {
		DirectDraw_UnlockSurface (tempsurf);
	
		getfilterrect2 (&dr, &sr, &zr, dst_width, dst_height, aw, ah, scale, temp_width, temp_height);
		//write_log (_T("(%d %d %d %d) - (%d %d %d %d) (%d %d)\n"), dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom, zr.left, zr.top);
		OffsetRect (&sr, zr.left, zr.top);
		if (sr.left < 0)
			sr.left = 0;
		if (sr.top < 0)
			sr.top = 0;
		if (sr.right < temp_width && sr.bottom < temp_height) {
			if (sr.left < sr.right && sr.top < sr.bottom)
				DirectDraw_BlitRect (NULL, &dr, tempsurf, &sr);
		}
		statusline ();
	}
}

void S2X_refresh (void)
{
	DirectDraw_ClearSurface (NULL);
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
