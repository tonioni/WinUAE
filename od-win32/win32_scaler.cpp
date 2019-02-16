
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

void getfilteroffset(int monid, float *dx, float *dy, float *mx, float *my)
{
	*dx = filteroffsetx;
	*dy = filteroffsety;
	*mx = filterxmult;
	*my = filterymult;
}

static void getinit(int monid)
{
	if (isfullscreen()) {
		struct MultiDisplay *md = getdisplay(&currprefs, monid);

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

static void getmanualpos(int monid, int *cxp, int *cyp, int *cwp, int *chp)
{
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	int v, cx, cy, cw, ch;
	bool native = isnativevidbuf(monid);

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
			cw = avidinfo->outbuffer->outwidth << (RES_MAX - currprefs.gfx_resolution);
		} else {
			cw = native ? AMIGA_WIDTH_MAX << RES_MAX : avidinfo->outbuffer->outwidth;
		}
	} else {
		cw = v;
	}
	cw >>= (RES_MAX - currprefs.gfx_resolution);

	v = currprefs.gfx_ycenter_size;
	if (v <= 0) {
		if (programmedmode && native) {
			ch = avidinfo->outbuffer->outheight << (VRES_MAX - currprefs.gfx_vresolution);
		} else {
			ch = native ? AMIGA_HEIGHT_MAX << VRES_MAX : avidinfo->outbuffer->outheight;
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

static bool get_auto_aspect_ratio(int monid, int cw, int ch, int crealh, int scalemode, float *autoaspectratio)
{
	struct amigadisplay *ad = &adisplays[monid];
	*autoaspectratio = 0;
	if (currprefs.gf[ad->picasso_on].gfx_filter_keep_autoscale_aspect && cw > 0 && ch > 0 && crealh > 0 && (scalemode == AUTOSCALE_NORMAL ||
		scalemode == AUTOSCALE_INTEGER_AUTOSCALE || scalemode == AUTOSCALE_MANUAL)) {
		float cw2 = cw;
		float ch2 = ch;
		int res = currprefs.gfx_resolution - currprefs.gfx_vresolution;

		if (res < 0)
			cw2 *= 1 << (-res);
		else if (res > 0)
			cw2 /= 1 << res;
		*autoaspectratio = (cw2 / ch2) / (4.0 / 3.0);
		return true;
	}
	return false;
}

static bool get_aspect(int monid, float *dstratiop, float *srcratiop, float *xmultp, float *ymultp, bool doautoaspect, float autoaspectratio)
{
	struct amigadisplay *ad = &adisplays[monid];
	bool aspect = false;
	float dstratio = *dstratiop;
	float srcratio = *srcratiop;

	*xmultp = 1.0;
	*ymultp = 1.0;

	if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect || currprefs.gf[ad->picasso_on].gfx_filter_aspect != 0) {

		if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect) {
			if (isvga()) {
				if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 1)
					dstratio = dstratio * 0.93f;
			} else {
				if (currprefs.ntscmode) {
					dstratio = dstratio * 1.21f;
					if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 2 && ispal())
						dstratio = dstratio * 0.93f;
					else if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 1 && !ispal())
						dstratio = dstratio * 0.98f;
				} else {
					if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 2 && ispal())
						dstratio = dstratio * 0.95f;
					else if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 1 && !ispal())
						dstratio = dstratio * 0.95f;
				}
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
			*ymultp = (*ymultp) * srcratio / dstratio;
		} else {
			*xmultp = (*xmultp) * dstratio / srcratio;
		}
	}
	*dstratiop = dstratio;
	*srcratiop = srcratio;
	return aspect;
}

void getfilterrect2(int monid, RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int temp_width, int temp_height)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct uae_filter *usedfilter = mon->usedfilter;
	struct monconfig *gmc = &currprefs.gfx_monitor[mon->monitor_id];
	struct monconfig *gmh = &changed_prefs.gfx_monitor[mon->monitor_id];
	float srcratio, dstratio;
	int aws, ahs;
	int xs, ys;
	int extraw, extrah;
	int fpuv;
	bool specialmode = !isnativevidbuf(monid);
	float mrmx, mrmy, mrsx, mrsy;
	int extraw2;
	bool doautoaspect = false;
	float autoaspectratio;

	float filter_horiz_zoom = currprefs.gf[ad->picasso_on].gfx_filter_horiz_zoom / 1000.0f;
	float filter_vert_zoom = currprefs.gf[ad->picasso_on].gfx_filter_vert_zoom / 1000.0f;
	float filter_horiz_zoom_mult = currprefs.gf[ad->picasso_on].gfx_filter_horiz_zoom_mult;
	float filter_vert_zoom_mult = currprefs.gf[ad->picasso_on].gfx_filter_vert_zoom_mult;
	float filter_horiz_offset = currprefs.gf[ad->picasso_on].gfx_filter_horiz_offset / 10000.0f;
	float filter_vert_offset = currprefs.gf[ad->picasso_on].gfx_filter_vert_offset / 10000.0f;

	store_custom_limits (-1, -1, -1, -1);

	if (!usedfilter && !currprefs.gfx_api) {
		filter_horiz_zoom = filter_vert_zoom = 0.0;
		filter_horiz_zoom_mult = filter_vert_zoom_mult = 1.0;
		filter_horiz_offset = filter_vert_offset = 0.0;
	}

	if (mon->screen_is_picasso) {
		getrtgfilterrect2(monid, sr, dr, zr, dst_width, dst_height);
		return;
	}

	fpux_save (&fpuv);

	getinit(monid);
	aws = aw * scale;
	ahs = ah * scale;
	//write_log (_T("%d %d %d\n"), dst_width, temp_width, aws);
	extraw = -aws * (filter_horiz_zoom - currprefs.gf[ad->picasso_on].gfx_filteroverlay_overscan * 10) / 2.0f;
	extrah = -ahs * (filter_vert_zoom - currprefs.gf[ad->picasso_on].gfx_filteroverlay_overscan * 10) / 2.0f;

	extraw2 = 0;
	if (D3D_getscalerect && D3D_getscalerect(0, &mrmx, &mrmy, &mrsx, &mrsy)) {
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
	if (currprefs.gf[ad->picasso_on].gfx_filter_aspect > 0) {
		dstratio = (currprefs.gf[ad->picasso_on].gfx_filter_aspect / ASPECTMULT) * 1.0f / (currprefs.gf[ad->picasso_on].gfx_filter_aspect & (ASPECTMULT - 1));
	} else if (currprefs.gf[ad->picasso_on].gfx_filter_aspect < 0) {
		if (isfullscreen () && deskw > 0 && deskh > 0)
			dstratio = 1.0f * deskw / deskh;
		else
			dstratio = 1.0f * dst_width / dst_height;
	} else {
		dstratio = srcratio;
	}

	int scalemode = currprefs.gf[ad->picasso_on].gfx_filter_autoscale;
	int oscalemode = changed_prefs.gf[ad->picasso_on].gfx_filter_autoscale;
	if (scalemode == AUTOSCALE_OVERSCAN_BLANK) {
		oscalemode = scalemode = AUTOSCALE_NONE;
	}

	if (!specialmode && scalemode == AUTOSCALE_STATIC_AUTO) {
		if (currprefs.gfx_apmode[0].gfx_fullscreen) {
			scalemode = AUTOSCALE_STATIC_NOMINAL;
		} else {
			int w1 = (800 / 2) << currprefs.gfx_resolution;
			int w2 = (640 / 2) << currprefs.gfx_resolution;
			int h1 = (600 / 2) << currprefs.gfx_vresolution;
			int h2 = (400 / 2) << currprefs.gfx_vresolution;
			int w = gmc->gfx_size_win.width;
			int h = gmc->gfx_size_win.height;
			if (w <= w1 && h <= h1 && w >= w2 && h >= h2)
				scalemode = AUTOSCALE_NONE;
			else
				scalemode = AUTOSCALE_STATIC_NOMINAL;
		}
	}

	bool scl = false;
	int width_aspect = -1;
	int height_aspect = -1;
	bool autoaspect_done = false;

	if (scalemode) {
		int cw, ch, cx, cy, cv, crealh = 0;
		static int oxmult, oymult;

		filterxmult = scale;
		filterymult = scale;

		if (scalemode == AUTOSCALE_STATIC_MAX || scalemode == AUTOSCALE_STATIC_NOMINAL ||
			scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {

			if (specialmode) {
				cx = 0;
				cy = 0;
				cw = avidinfo->outbuffer->outwidth;
				ch = avidinfo->outbuffer->outheight;
			} else {
				cx = 0;
				cy = 0;
				cw = avidinfo->drawbuffer.inwidth;
				ch = avidinfo->drawbuffer.inheight;
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
				int maxw = gmc->gfx_size.width;
				int maxh = gmc->gfx_size.height;
				double mult = 1;
				bool ok = true;

				if (currprefs.gfx_xcenter_pos >= 0 || currprefs.gfx_ycenter_pos >= 0) {
					changed_prefs.gf[ad->picasso_on].gfx_filter_horiz_offset = currprefs.gf[ad->picasso_on].gfx_filter_horiz_offset = 0.0;
					changed_prefs.gf[ad->picasso_on].gfx_filter_vert_offset = currprefs.gf[ad->picasso_on].gfx_filter_vert_offset = 0.0;
					filter_horiz_offset = 0.0;
					filter_vert_offset = 0.0;
					get_custom_topedge (&cx, &cy, false);
				}

				if (scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {
					ok = get_custom_limits (&cw, &ch, &cx, &cy, &crealh) != 0;
					if (ok) {
						set_custom_limits(cw, ch, cx, cy);
						store_custom_limits(cw, ch, cx, cy);
					}
				}
				if (scalemode == AUTOSCALE_INTEGER || ok == false) {
					getmanualpos(monid, &cx, &cy, &cw, &ch);
					set_custom_limits(cw, ch, cx, cy);
					store_custom_limits(cw, ch, cx, cy);
				}

#if 0
				doautoaspect = get_auto_aspect_ratio(cw, ch, crealh, scalemode, &autoaspectratio);
				autoaspect_done = true;

				if (get_aspect(&dstratio, &srcratio, &xmult, &ymult, doautoaspect, autoaspectratio)) {
					cw += cw - cw * xmult;
					ch += ch - ch * ymult;
				}
#endif

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

				double multadd = 1.0 / (1 << currprefs.gf[ad->picasso_on].gfx_filter_integerscalelimit);
				if (cw2 > maxw || ch2 > maxh) {
					while (cw2 / mult > maxw || ch2 / mult > maxh)
						mult += multadd;
					maxw = maxw * mult;
					maxh = maxh * mult;
				} else {
					while (cw2 * (mult + multadd) <= maxw && ch2 * (mult + multadd) <= maxh)
						mult += multadd;
					maxw = (maxw + mult - multadd) / mult;
					maxh = (maxh + mult - multadd) / mult;
				}

				width_aspect = cw;
				height_aspect = ch;

				//write_log(_T("(%dx%d) (%dx%d) ww=%d hh=%d w=%d h=%d m=%d\n"), cx, cy, cw, ch, currprefs.gfx_size.width, currprefs.gfx_size.height, maxw, maxh, mult);
				cx -= (maxw - cw) / 2;
				cw = maxw;
				cy -= (maxh - ch) / 2;
				ch = maxh;
			}

		} else if (scalemode == AUTOSCALE_MANUAL) {

			changed_prefs.gf[ad->picasso_on].gfx_filter_horiz_offset = currprefs.gf[ad->picasso_on].gfx_filter_horiz_offset = 0.0;
			changed_prefs.gf[ad->picasso_on].gfx_filter_vert_offset = currprefs.gf[ad->picasso_on].gfx_filter_vert_offset = 0.0;
			filter_horiz_offset = 0.0;
			filter_vert_offset = 0.0;

			get_custom_topedge (&cx, &cy, currprefs.gfx_xcenter_pos < 0 && currprefs.gfx_ycenter_pos < 0);
			//write_log (_T("%dx%d %dx%d\n"), cx, cy, currprefs.gfx_resolution, currprefs.gfx_vresolution);

			getmanualpos(monid, &cx, &cy, &cw, &ch);
			set_custom_limits(cw, ch, cx, cy);
			store_custom_limits(cw, ch, cx, cy);
			scl = true;

			//write_log (_T("%dx%d %dx%d %dx%d\n"), currprefs.gfx_xcenter_pos, currprefs.gfx_ycenter_pos, cx, cy, cw, ch);

			cv = 1;

		} else if (scalemode == AUTOSCALE_CENTER || scalemode == AUTOSCALE_RESIZE) {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh);
			if (cv) {
				set_custom_limits(cw, ch, cx, cy);
				store_custom_limits(cw, ch, cx, cy);
				scl = true;
			}

		} else {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh);
			if (cv) {
				set_custom_limits (cw, ch, cx, cy);
				store_custom_limits (cw, ch, cx, cy);
				scl = true;
			}

		}

		if (!scl) {
			set_custom_limits (0, 0, 0, 0);
		}
	
		if (!autoaspect_done) {
			doautoaspect = get_auto_aspect_ratio(monid, cw, ch, crealh, scalemode, &autoaspectratio);
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

			} else if (scalemode == AUTOSCALE_RESIZE && isfullscreen() == 0 && !currprefs.gf[ad->picasso_on].gfx_filteroverlay[0]) {

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
				float scalex = currprefs.gf[ad->picasso_on].gfx_filter_horiz_zoom_mult > 0 ? currprefs.gf[ad->picasso_on].gfx_filter_horiz_zoom_mult : 1.0f;
				float scaley = currprefs.gf[ad->picasso_on].gfx_filter_vert_zoom_mult > 0 ? currprefs.gf[ad->picasso_on].gfx_filter_vert_zoom_mult : 1.0f;
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
					int oldwinw = gmc->gfx_size_win.width;
					int oldwinh = gmc->gfx_size_win.height;
					gmh->gfx_size_win.width = ww;
					gmh->gfx_size_win.height = hh;
					fixup_prefs_dimensions (&changed_prefs);
					if (oldwinw != gmh->gfx_size_win.width || oldwinh != gmh->gfx_size_win.height)
						set_config_changed ();
				}
				OffsetRect (zr, -(gmh->gfx_size_win.width - ww + 1) / 2, -(gmh->gfx_size_win.height - hh + 1) / 2);
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

			int diffx, diffy;

			if (width_aspect > 0) {
				diffx = width_aspect;
			} else {
				diffx = dr->right - dr->left;
			}
			if (height_aspect > 0) {
				diffy = height_aspect;
			} else {
				diffy = dr->bottom - dr->top;
			}

			if (get_aspect(monid, &dstratio, &srcratio, &xmult, &ymult, doautoaspect, autoaspectratio)) {
				diff = diffx - diffx * xmult;
				sizeoffset(dr, zr, diff, 0);
				filteroffsetx += -diff / 2;

				diff = diffy - diffy * ymult;
				sizeoffset(dr, zr, 0, diff);
				filteroffsety += -diff / 2;
			}

			OffsetRect (zr, (int)(-filter_horiz_offset * aws), 0);
			OffsetRect (zr, 0, (int)(-filter_vert_offset * ahs));

			diff = dr->right - dr->left;
			filterxmult = ((float)dst_width * scale) / diff;
			diff = dr->bottom - dr->top;
			filterymult = ((float)dst_height * scale) / diff;

			goto end;
		}

	}
cont:

	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {

		sizeoffset (dr, zr, extraw, extrah);

		if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect) {
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
		if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 2 && ispal ())
			dstratio = dstratio * 0.93f;
		else if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 1 && !ispal ())
			dstratio = dstratio * 0.98f;
	} else {
		if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 2 && ispal ())
			dstratio = dstratio * 0.95f;
		else if (currprefs.gf[ad->picasso_on].gfx_filter_keep_aspect == 1 && !ispal ())
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
	filteroffsetx += (dst_width - aw * filterxmult) / 2;
	filteroffsety += (dst_height - ah * filterymult) / 2;

end:

	if (D3D_getscalerect && D3D_getscalerect(0, &mrmx, &mrmy, &mrsx, &mrsy)) {
		sizeoffset (dr, zr, mrmx, mrmy);
		OffsetRect (dr, mrsx, mrsy);
	}

	check_custom_limits();

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

void freefilterbuffer(int monid, uae_u8 *buf)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->outbuffer;
	struct uae_filter *usedfilter = mon->usedfilter;

	if (!vb)
		return;
	if (usedfilter == NULL) {
		unlockscr3d(vb);
	}
}

uae_u8 *getfilterbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->outbuffer;
	struct uae_filter *usedfilter = mon->usedfilter;

	*widthp = 0;
	*heightp = 0;
	*depth = amiga_depth;
	if (!vb)
		return NULL;
	if (usedfilter == NULL) {
		if (!lockscr3d(vb)) {
			return NULL;
		}
	}
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

static void statusline(int monid)
{
	DDSURFACEDESC2 desc;
	RECT sr, dr;
	int y;
	int lx, ly;
	int slx, sly;

	if (!(currprefs.leds_on_screen & STATUSLINE_CHIPSET) || !tempsurf)
		return;
	statusline_getpos(monid, &slx, &sly, dst_width, dst_height, 1, 1);
	lx = dst_width;
	ly = dst_height;
	SetRect(&sr, slx, 0, slx + lx, TD_TOTAL_HEIGHT);
	SetRect(&dr, slx, sly, slx + lx, sly + TD_TOTAL_HEIGHT);
	DirectDraw_BlitRect(tempsurf, &sr, NULL, &dr);
	if (DirectDraw_LockSurface(tempsurf, &desc)) {
		statusline_render(0, (uae_u8*)desc.lpSurface, dst_depth / 8, desc.lPitch, lx, ly, rc, gc, bc, NULL);
		for (y = 0; y < TD_TOTAL_HEIGHT; y++) {
			uae_u8 *buf = (uae_u8*)desc.lpSurface + y * desc.lPitch;
			draw_status_line_single(monid, buf, dst_depth / 8, y, lx, rc, gc, bc, NULL);
		}
		DirectDraw_UnlockSurface(tempsurf);
		DirectDraw_BlitRect(NULL, &dr, tempsurf, &sr);
	}
}

void S2X_configure(int monid, int rb, int gb, int bb, int rs, int gs, int bs)
{
	if (monid)
		return;
	Init_2xSaI(rb, gb, bb, rs, gs, bs);
	hq_init(rb, gb, bb, rs, gs, bs);
	PAL_init(monid);
	bufmem_ptr = 0;
}

void S2X_reset(int monid)
{
	if (monid)
		return;
	if (!inited)
		return;
	S2X_init(monid, dst_width2, dst_height2, amiga_depth2);
}

void S2X_free(int monid)
{
	if (monid)
		return;

	changed_prefs.leds_on_screen &= ~STATUSLINE_TARGET;
	currprefs.leds_on_screen &= ~STATUSLINE_TARGET;

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

bool S2X_init(int monid, int dw, int dh, int dd)
{
	if (monid)
		return false;

	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct amigadisplay *ad = &adisplays[monid];
	struct vidbuffer *vb = avidinfo->outbuffer;
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct uae_filter *usedfilter = mon->usedfilter;
	int flags = 0;

	dst_width2 = dw;
	dst_height2 = dh;
	dst_depth2 = dd;
	amiga_width2 = vb->outwidth;
	amiga_height2 = vb->outheight;
	amiga_depth2 = vb->pixbytes * 8;

	S2X_free(monid);
	d3d = currprefs.gfx_api;
	changed_prefs.leds_on_screen |= STATUSLINE_TARGET;
	currprefs.leds_on_screen |= STATUSLINE_TARGET;

	if (d3d)
		dd = amiga_depth2;

	if (dd == 32)
		alloc_colors_rgb (8, 8, 8, 16, 8, 0, 0, 0, 0, 0, rc, gc, bc);
	else
		alloc_colors_rgb (5, 6, 5, 11, 5, 0, 0, 0, 0, 0, rc, gc, bc);

	if (WIN32GFX_IsPicassoScreen(mon))
		return true;

	if (!currprefs.gf[ad->picasso_on].gfx_filter || !usedfilter) {
		usedfilter = &uaefilters[0];
		scale = 1;
	} else {
		scale = usedfilter->intmul;
		flags = usedfilter->flags;
		if ((amiga_depth2 == 16 && !(flags & UAE_FILTER_MODE_16)) || (amiga_depth2 == 32 && !(flags & UAE_FILTER_MODE_32))) {
			usedfilter = &uaefilters[0];
			scale = 1;
			changed_prefs.gf[ad->picasso_on].gfx_filter = usedfilter->type;
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
		int mh = currprefs.gf[ad->picasso_on].gfx_filter_filtermodeh + 1;
		if (mh < scale)
			mh = scale;
		temp_width = dst_width * mh;
		int mv = currprefs.gf[ad->picasso_on].gfx_filter_filtermodev + 1;
		if (mv < scale)
			mv = scale;
		temp_height = dst_height * mv;
	} else {
		temp_width = dst_width * 2;
		temp_height = dst_height * 2;
	}

	if (usedfilter->type == UAE_FILTER_HQ2X || usedfilter->type == UAE_FILTER_HQ3X || usedfilter->type == UAE_FILTER_HQ4X) {
		int w = amiga_width > dst_width ? amiga_width : dst_width;
		int h = amiga_height > dst_height ? amiga_height : dst_height;
		tempsurf2 = xmalloc (uae_u8, w * h * (amiga_depth / 8) * ((scale + 1) / 2));
		tempsurf3 = xmalloc (uae_u8, w * h *(dst_depth / 8) * 4 * scale);
	}
	if (!d3d) {
		for (;;) {
			if (temp_width > dxcaps.maxwidth)
				temp_width = dxcaps.maxwidth;
			if (temp_height > dxcaps.maxheight)
				temp_height = dxcaps.maxheight;
			if (temp_width < dst_width)
				temp_width = dst_width;
			if (temp_height < dst_height)
				temp_height = dst_height;
			tempsurf = allocsurface(temp_width, temp_height);
			if (tempsurf)
				break;
			if (temp_width >= 2 * dst_width || temp_height >= 2 * dst_height) {
				temp_width = dst_width * 3 / 2;
				temp_height = dst_height * 3 / 2;
				continue;
			}
			if (temp_width == dst_width * 3 / 2 || temp_height == dst_height * 2) {
				temp_width = dst_width * 4 / 3;
				temp_height = dst_height * 4 / 3;
				continue;
			}
			if (temp_width > dst_width || temp_height > dst_height) {
				temp_width = dst_width;
				temp_height = dst_height;
				continue;
			}
			break;
		}
	}
	if (!tempsurf && !d3d) {
		write_log (_T("DDRAW: failed to create temp surface (%dx%d)\n"), temp_width, temp_height);
		return false;
	}
	inited = true;
	return true;
}

void S2X_render(int monid, int y_start, int y_end)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct uae_filter *usedfilter = mon->usedfilter;
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->outbuffer;
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
	if (!vb->bufmem)
		return;

	sptr = vb->bufmem;
	endsptr = vb->bufmemend;
	bufmem_ptr = sptr;

	if (d3d) {
		if (D3D_restore)
			D3D_restore(monid, true);
		surfstart = D3D_locktexture(monid, &pitch, &surf_height, y_start < 0);
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
			int w = aw * dst_depth / 8;
			if (y_start < 0) {
				uae_u8 *d = dptr;
				uae_u8 *s = sptr;
				for (int y = 0; y < ah && d + w <= enddptr; y++) {
					memcpy(d, s, w);
					s += vb->rowbytes;
					d += pitch;
				}
			} else {
				uae_u8 *d = dptr + y_start * pitch;
				uae_u8 *s = sptr + y_start * vb->rowbytes;
				for (int y = y_start; y < ah && y < y_end && d + w <= enddptr; y++) {
					memcpy(d, s, w);
					s += vb->rowbytes;
					d += pitch;
				}
			}
		}
		ok = 1;

	}

	if (ok == 0 && currprefs.gf[ad->picasso_on].gfx_filter) {
		usedfilter = &uaefilters[0];
		changed_prefs.gf[ad->picasso_on].gfx_filter = usedfilter->type;
	}

end:
	if (d3d) {
		;//D3D_unlocktexture (); unlock in win32gfx.c
	} else {
		DirectDraw_UnlockSurface (tempsurf);
	
		getfilterrect2(monid, &dr, &sr, &zr, dst_width, dst_height, aw, ah, scale, temp_width, temp_height);
		//write_log (_T("(%d %d %d %d) - (%d %d %d %d) (%d %d)\n"), dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom, zr.left, zr.top);
		OffsetRect (&sr, zr.left, zr.top);
		if (sr.left < 0)
			sr.left = 0;
		if (sr.top < 0)
			sr.top = 0;
		if (sr.right <= temp_width && sr.bottom <= temp_height) {
			if (sr.left < sr.right && sr.top < sr.bottom)
				DirectDraw_BlitRect (NULL, &dr, tempsurf, &sr);
		}
		statusline(monid);
	}
}

void S2X_refresh(int monid)
{
	if (monid)
		return;
	DirectDraw_ClearSurface(NULL);
	S2X_render(monid, -1, -1);
}

int S2X_getmult(int monid)
{
	if (monid)
		return 1;
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct uae_filter *usedfilter = mon->usedfilter;
	if (!usedfilter)
		return 1;
	if (mon->screen_is_picasso)
		return 1;
	return usedfilter->intmul;
}

#endif
