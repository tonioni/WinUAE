
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef GFXFILTER

#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "win32.h"
#include "win32gfx.h"
#include "gfxfilter.h"
#include "render.h"
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
static uae_u8 *tempsurf2, *tempsurf3;
static int cleartemp;
static uae_u32 rc[256], gc[256], bc[256];
static int deskw, deskh;
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
			if (currprefs.gfx_overscanmode <= OVERSCANMODE_OVERSCAN) {
				// keep old version compatibility
				cw = native ? AMIGA_WIDTH_MAX << RES_MAX : avidinfo->outbuffer->outwidth << 1;
			} else {
				cw = native ? maxhpos_display << RES_MAX : avidinfo->outbuffer->outwidth << 1;
			}
		}
	} else {
		cw = v;
	}
	cw >>= (RES_MAX - currprefs.gfx_resolution);

	v = currprefs.gfx_ycenter_size;
	if (v <= 0) {
		if (programmedmode && native) {
			ch = avidinfo->outbuffer->outheight << (VRES_MAX - currprefs.gfx_vresolution);
		} else if (currprefs.gfx_overscanmode <= OVERSCANMODE_OVERSCAN) {
			// keep old version compatiblity
			ch = native ? (ispal(NULL) ? AMIGA_HEIGHT_MAX_PAL : AMIGA_HEIGHT_MAX_NTSC) << VRES_MAX : avidinfo->outbuffer->outheight;
		} else {
			ch = native ? (maxvpos_display + maxvpos_display_vsync - minfirstline) << VRES_MAX : avidinfo->outbuffer->outheight;
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

static bool get_auto_aspect_ratio(int monid, int cw, int ch, int crealh, int scalemode, float *autoaspectratio, int idx)
{
	*autoaspectratio = 0;
	if (currprefs.gf[idx].gfx_filter_keep_autoscale_aspect && cw > 0 && ch > 0 && crealh > 0 && (scalemode == AUTOSCALE_NORMAL ||
		scalemode == AUTOSCALE_INTEGER_AUTOSCALE || scalemode == AUTOSCALE_MANUAL)) {
		float cw2 = (float)cw;
		float ch2 = (float)ch;
		int res = currprefs.gfx_resolution - currprefs.gfx_vresolution;

		if (res < 0)
			cw2 *= 1 << (-res);
		else if (res > 0)
			cw2 /= 1 << res;
		*autoaspectratio = (cw2 / ch2) / (4.0f / 3.0f);
		return true;
	}
	return false;
}

static bool get_aspect(int monid, float *dstratiop, float *srcratiop, float *xmultp, float *ymultp, bool doautoaspect, float autoaspectratio, int keep_aspect, int filter_aspect)
{
	struct amigadisplay *ad = &adisplays[monid];
	bool aspect = false;
	float dstratio = *dstratiop;
	float srcratio = *srcratiop;

	*xmultp = 1.0;
	*ymultp = 1.0;

	if (keep_aspect || filter_aspect != 0) {

		if (keep_aspect) {
			if (isvga()) {
				if (keep_aspect == 1)
					dstratio = dstratio * 0.93f;
			} else {
				bool isp = ispal(NULL);
				if (currprefs.ntscmode) {
					dstratio = dstratio * 1.21f;
					if (keep_aspect == 2 && isp)
						dstratio = dstratio * 0.93f;
					else if (keep_aspect == 1 && !isp)
						dstratio = dstratio * 0.98f;
				} else {
					if (keep_aspect == 2 && isp)
						dstratio = dstratio * 0.95f;
					else if (keep_aspect == 1 && !isp)
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

void getfilterrect2(int monid, RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height, int aw, int ah, int scale, int *mode, int temp_width, int temp_height)
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
	int idx = ad->gf_index;
	int keep_aspect = currprefs.gf[idx].gfx_filter_keep_aspect;
	int filter_aspect = currprefs.gf[idx].gfx_filter_aspect;
	int palntscadjust = 1;
	int autoselect = 0;

	float filter_horiz_zoom = currprefs.gf[idx].gfx_filter_horiz_zoom / 1000.0f;
	float filter_vert_zoom = currprefs.gf[idx].gfx_filter_vert_zoom / 1000.0f;
	float filter_horiz_zoom_mult = currprefs.gf[idx].gfx_filter_horiz_zoom_mult;
	float filter_vert_zoom_mult = currprefs.gf[idx].gfx_filter_vert_zoom_mult;
	float filter_horiz_offset = currprefs.gf[idx].gfx_filter_horiz_offset / 10000.0f;
	float filter_vert_offset = currprefs.gf[idx].gfx_filter_vert_offset / 10000.0f;

	store_custom_limits (-1, -1, -1, -1);
	*mode = 0;

	if (mon->screen_is_picasso) {
		getrtgfilterrect2(monid, sr, dr, zr, mode, dst_width, dst_height);
		if (D3D_getscalerect && D3D_getscalerect(monid, &mrmx, &mrmy, &mrsx, &mrsy, dst_width, dst_height)) {
			sizeoffset(dr, zr, (int)mrmx, (int)mrmy);
			OffsetRect(dr, (int)mrsx, (int)mrsy);
		}
		return;
	}

	fpux_save (&fpuv);

	getinit(monid);
	aws = aw * scale;
	ahs = ah * scale;
	//write_log (_T("%d %d %d\n"), dst_width, temp_width, aws);
	extraw = (int)(-aws * (filter_horiz_zoom - currprefs.gf[idx].gfx_filteroverlay_overscan * 10) / 2.0f);
	extrah = (int)(-ahs * (filter_vert_zoom - currprefs.gf[idx].gfx_filteroverlay_overscan * 10) / 2.0f);

	extraw2 = 0;
	if (D3D_getscalerect && D3D_getscalerect(monid, &mrmx, &mrmy, &mrsx, &mrsy, avidinfo->outbuffer->inwidth2, avidinfo->outbuffer->inheight2)) {
		extraw2 = (int)mrmx;
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

	int scalemode = currprefs.gf[idx].gfx_filter_autoscale;
	int oscalemode = changed_prefs.gf[idx].gfx_filter_autoscale;
	if (scalemode == AUTOSCALE_OVERSCAN_BLANK) {
		oscalemode = scalemode = AUTOSCALE_NONE;
	}

	srcratio = 4.0f / 3.0f;

	if (!specialmode && scalemode == AUTOSCALE_STATIC_AUTO) {
		filter_aspect = 0;
		keep_aspect = 0;
		palntscadjust = 1;
		if (dst_width >= 640 && dst_width <= 800 && dst_height >= 480 && dst_height <= 600 && !programmedmode) {
			autoselect = 1;
			scalemode = AUTOSCALE_NONE;
			int m = 1;
			int w = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
			int h = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;
			for (;;) {
				if (w * (m * 2) > dst_width || h * (m * 2) > dst_height) {
					break;
				}
				m *= 2;
			}
			autoselect = m;
		} else {
			float dstratio = 1.0f * dst_width / dst_height;
			scalemode = AUTOSCALE_STATIC_NOMINAL;
			if (dstratio > srcratio + 0.1 || dstratio < srcratio - 0.1) {
				filter_aspect = -1;
			}
		}
	}

	if (filter_aspect > 0) {
		dstratio = (filter_aspect / ASPECTMULT) * 1.0f / (filter_aspect & (ASPECTMULT - 1));
	} else if (filter_aspect < 0) {
		if (isfullscreen() && deskw > 0 && deskh > 0)
			dstratio = 1.0f * deskw / deskh;
		else
			dstratio = 1.0f * dst_width / dst_height;
	} else {
		dstratio = srcratio;
	}

	bool scl = false;
	int width_aspect = -1;
	int height_aspect = -1;
	bool autoaspect_done = false;

	if (scalemode) {
		int cw, ch, cx, cy, cv = 0, crealh = 0;
		static int oxmult, oymult;

		filterxmult = (float)scale;
		filterymult = (float)scale;

		if (scalemode == AUTOSCALE_STATIC_MAX || scalemode == AUTOSCALE_STATIC_NOMINAL ||
			scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {

			if (scalemode == AUTOSCALE_STATIC_NOMINAL || scalemode == AUTOSCALE_STATIC_NOMINAL || scalemode == AUTOSCALE_STATIC_MAX) {
				// do not default/TV scale programmed modes
				if (beamcon0 & BEAMCON0_VARBEAMEN) {
					goto cont;
				}
			}

			if (specialmode) {
				cx = 0;
				cy = 0;
				cw = avidinfo->outbuffer->outwidth;
				ch = avidinfo->outbuffer->outheight;
				cv = 1;
			} else {
				cx = 0;
				cy = 0;
				cw = avidinfo->drawbuffer.inwidth;
				ch = avidinfo->drawbuffer.inheight;
				cv = 1;
				if (scalemode == AUTOSCALE_STATIC_NOMINAL) { // || scalemode == AUTOSCALE_INTEGER)) {
					cx = 28 << currprefs.gfx_resolution;
					cy = 10 << currprefs.gfx_vresolution;
					cw -= 40 << currprefs.gfx_resolution;
					ch -= 20 << currprefs.gfx_vresolution;
				}
				if (scalemode != AUTOSCALE_INTEGER && scalemode != AUTOSCALE_INTEGER_AUTOSCALE) {
					set_custom_limits (cw, ch, cx, cy, true);
					store_custom_limits (cw, ch, cx, cy);
					scl = true;
				}
			}

			if (scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {
				int maxw = isfullscreen() < 0 ? deskw : gmc->gfx_size.width;
				int maxh = isfullscreen() < 0 ? deskh : gmc->gfx_size.height;
				float mult = 1.0f;
				bool ok = true;

				if (currprefs.gfx_xcenter_pos >= 0 || currprefs.gfx_ycenter_pos >= 0) {
					changed_prefs.gf[idx].gfx_filter_horiz_offset = currprefs.gf[idx].gfx_filter_horiz_offset = 0.0;
					changed_prefs.gf[idx].gfx_filter_vert_offset = currprefs.gf[idx].gfx_filter_vert_offset = 0.0;
					filter_horiz_offset = 0.0;
					filter_vert_offset = 0.0;
					get_custom_topedge (&cx, &cy, false);
				}

				if (scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {
					ok = get_custom_limits (&cw, &ch, &cx, &cy, &crealh) != 0;
					if (ok) {
						set_custom_limits(cw, ch, cx, cy, true);
						store_custom_limits(cw, ch, cx, cy);
						scl = true;
					}
				}
				if (scalemode == AUTOSCALE_INTEGER || ok == false) {
					getmanualpos(monid, &cx, &cy, &cw, &ch);
					set_custom_limits(cw, ch, cx, cy, true);
					store_custom_limits(cw, ch, cx, cy);
					scl = true;
				}

#if 0
				doautoaspect = get_auto_aspect_ratio(cw, ch, crealh, scalemode, &autoaspectratio);
				autoaspect_done = true;

				if (get_aspect(&dstratio, &srcratio, &xmult, &ymult, doautoaspect, autoaspectratio, keep_aspect, filter_aspect)) {
					cw += cw - cw * xmult;
					ch += ch - ch * ymult;
				}
#endif

				int cw2 = cw + (int)(cw * filter_horiz_zoom);
				int ch2 = ch + (int)(ch * filter_vert_zoom);
				int adjw = cw2 * 5 / 100;
				int adjh = ch2 * 5 / 100;

				extraw = 0;
				extrah = 0;
				xmult = 1.0;
				ymult = 1.0;
				filter_horiz_zoom = 0;
				filter_vert_zoom = 0;
				filter_horiz_zoom_mult = 1.0;
				filter_vert_zoom_mult = 1.0;

				float multadd = 1.0f / (1 << currprefs.gf[idx].gfx_filter_integerscalelimit);
				if (cw2 > 0 && ch2 > 0) {
					if (cw2 > maxw || ch2 > maxh) {
						while (cw2 / mult - adjw > maxw || ch2 / mult - adjh > maxh) {
							mult += multadd;
						}
						float multx = mult, multy = mult;
						maxw = (int)(maxw * multx);
						maxh = (int)(maxh * multy);
					} else {
						while (cw2 * (mult + multadd) - adjw <= maxw && ch2 * (mult + multadd) - adjh <= maxh) {
							mult += multadd;
						}

						float multx = mult, multy = mult;
						// if width is smaller than height, double width (programmed modes)
						if (cw2 * (mult + multadd) - adjw <= maxw && cw2 < ch2) {
							multx += multadd;
						}
						// if width is >2.5x height, double height (non-doublescanned superhires)
						if (ch2 * (mult + multadd) - adjh <= maxh && cw2 > ch2 * 2.5) {
							multy += multadd;
						}
						maxw = (int)((maxw + multx - multadd) / multx);
						maxh = (int)((maxh + multy - multadd) / multy);
					}
				}

				*mode = 1;

				width_aspect = cw;
				height_aspect = ch;

				//write_log(_T("(%dx%d) (%dx%d) ww=%d hh=%d w=%d h=%d m=%d\n"), cx, cy, cw, ch, currprefs.gfx_size.width, currprefs.gfx_size.height, maxw, maxh, mult);
				cx -= (maxw - cw) / 2;
				cw = maxw;
				cy -= (maxh - ch) / 2;
				ch = maxh;
			}

		} else if (scalemode == AUTOSCALE_MANUAL) {

			changed_prefs.gf[idx].gfx_filter_horiz_offset = currprefs.gf[idx].gfx_filter_horiz_offset = 0.0;
			changed_prefs.gf[idx].gfx_filter_vert_offset = currprefs.gf[idx].gfx_filter_vert_offset = 0.0;
			filter_horiz_offset = 0.0;
			filter_vert_offset = 0.0;

			get_custom_topedge (&cx, &cy, currprefs.gfx_xcenter_pos < 0 && currprefs.gfx_ycenter_pos < 0);
			//write_log (_T("%dx%d %dx%d\n"), cx, cy, currprefs.gfx_resolution, currprefs.gfx_vresolution);

			getmanualpos(monid, &cx, &cy, &cw, &ch);
			set_custom_limits(cw, ch, cx, cy, false);
			store_custom_limits(cw, ch, cx, cy);
			scl = true;

			//write_log (_T("%dx%d %dx%d %dx%d\n"), currprefs.gfx_xcenter_pos, currprefs.gfx_ycenter_pos, cx, cy, cw, ch);

			cv = 1;

		} else if (scalemode == AUTOSCALE_CENTER) {

			cv = get_custom_limits(&cw, &ch, &cx, &cy, &crealh);
			if (cv) {
				store_custom_limits(cw, ch, cx, cy);
			}

		} else if (scalemode == AUTOSCALE_RESIZE) {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh);
			if (cv) {
				set_custom_limits(cw, ch, cx, cy, false);
				store_custom_limits(cw, ch, cx, cy);
				scl = true;
			}

		} else {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh);
			if (cv) {
				set_custom_limits (cw, ch, cx, cy, true);
				store_custom_limits (cw, ch, cx, cy);
				scl = true;
			}

		}

		if (!scl) {
			set_custom_limits (0, 0, 0, 0, false);
		}
	
		if (!autoaspect_done) {
			doautoaspect = get_auto_aspect_ratio(monid, cw, ch, crealh, scalemode, &autoaspectratio, idx);
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

			} else if (scalemode == AUTOSCALE_RESIZE && isfullscreen() == 0 && !currprefs.gf[idx].gfx_filteroverlay[0]) {

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
				float scalex = currprefs.gf[idx].gfx_filter_horiz_zoom_mult > 0 ? currprefs.gf[idx].gfx_filter_horiz_zoom_mult : 1.0f;
				float scaley = currprefs.gf[idx].gfx_filter_vert_zoom_mult > 0 ? currprefs.gf[idx].gfx_filter_vert_zoom_mult : 1.0f;
				SetRect (sr, 0, 0, (int)(cw * scale * scalex), (int)(ch * scale * scaley));
				dr->left = (temp_width - aws) /2;
				dr->top = (temp_height - ahs) / 2;
				dr->right = dr->left + cw * scale;
				dr->bottom = dr->top + ch * scale;
				OffsetRect (zr, cx * scale, cy * scale);
				int ww = (int)((dr->right - dr->left) * scalex);
				int hh = (int)((dr->bottom - dr->top) * scaley);
				if (currprefs.gfx_xcenter_size >= 0)
					ww = currprefs.gfx_xcenter_size;
				if (currprefs.gfx_ycenter_size >= 0)
					hh = currprefs.gfx_ycenter_size;
				if (scalemode == oscalemode && !useold) {
					int oldwinw = gmc->gfx_size_win.width;
					int oldwinh = gmc->gfx_size_win.height;
					gmh->gfx_size_win.width = ww;
					gmh->gfx_size_win.height = hh;
					fixup_prefs_dimensions (&changed_prefs);
					if (oldwinw != gmh->gfx_size_win.width || oldwinh != gmh->gfx_size_win.height)
						set_config_changed ();
				}
				OffsetRect (zr, -(gmh->gfx_size_win.width - ww + 1) / 2, -(gmh->gfx_size_win.height - hh + 1) / 2);
				filteroffsetx = (float)-zr->left / scale;
				filteroffsety = (float)-zr->top / scale;
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

			filteroffsetx = (float)-zr->left / scale;
			filteroffsety = (float)-zr->top / scale;

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

			if (get_aspect(monid, &dstratio, &srcratio, &xmult, &ymult, doautoaspect, autoaspectratio, keep_aspect, filter_aspect)) {
				diff = diffx - (int)(diffx * xmult);
				sizeoffset(dr, zr, diff, 0);
				filteroffsetx += -diff / 2;

				diff = diffy - (int)(diffy * ymult);
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

		if (keep_aspect) {
			float xm, ym, m;

			xm = (float)aws / dst_width;
			ym = (float)ahs / dst_height;
			if (xm < ym)
				xm = ym;
			else
				ym = xm;
			xmult = ymult = xm;

			m = (float)(aws * dst_width) / (ahs * dst_height);
			dstratio = dstratio * m;
		}

	}

	if (autoselect) {
		xmult *= autoselect;
		ymult *= autoselect;
		if (currprefs.gfx_vresolution == VRES_NONDOUBLE) {
			if (currprefs.gfx_resolution == RES_HIRES) {
				ymult *= 2;
			} else if (currprefs.gfx_resolution == RES_SUPERHIRES) {
				xmult /= 2;
			}
		} else {
			if (currprefs.gfx_resolution == RES_LORES) {
				xmult *= 2;
			} else if (currprefs.gfx_resolution == RES_SUPERHIRES) {
				xmult /= 2;
			}
		}
	}

	{
		float palntscratio = dstratio;
		int l = 0;
		bool isp = ispal(&l);
		if (abs(l - 262) <= 25) {
			l = 262;
		}
		if (abs(l - 312) <= 25) {
			l = 312;
		}
		float ll = l * 2.0f + 1.0f;
		if (currprefs.ntscmode) {
			if (palntscadjust && isp) {
				palntscratio = palntscratio * (525.0f / ll);
			}
			if (keep_aspect == 2 && isp) {
				palntscratio = palntscratio * 0.93f;
			} else if (keep_aspect == 1 && !isp) {
				palntscratio = palntscratio * 0.98f;
			}
		} else {
			if (palntscadjust && !isp) {
				palntscratio = palntscratio * (625.0f / ll);
			}
			if (keep_aspect == 2 && isp) {
				palntscratio = palntscratio * 0.95f;
			} else if (keep_aspect == 1 && !isp) {
				palntscratio = palntscratio * 0.95f;
			}
		}
		if (palntscratio != dstratio) {
			ymult = ymult * palntscratio / dstratio;
		}
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
		xs -= (int)(dst_width / xmult);
	ys = dst_height;
	if (ymult)
		ys -= (int)(dst_height / ymult);
	sizeoffset (dr, zr, xs, ys);

	filterxmult = xmult;
	filterymult = ymult;
	filteroffsetx += (dst_width - aw * filterxmult) / 2;
	filteroffsety += (dst_height - ah * filterymult) / 2;

end:

	if (D3D_getscalerect && D3D_getscalerect(monid, &mrmx, &mrmy, &mrsx, &mrsy, avidinfo->outbuffer->inwidth2, avidinfo->outbuffer->inheight2)) {
		sizeoffset(dr, zr, (int)mrmx, (int)mrmy);
		OffsetRect(dr, (int)mrsx, (int)mrsy);
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
	int w, h;

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
	w = vb->outwidth;
	h = vb->outheight;
	if (!monid) {
		// if native screen: do not include vertical blank
		h = get_vertical_visible_height(false);
	}
	if (pitch)
		*pitch = vb->rowbytes;
	*widthp = w;
	*heightp = h;
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
	int idx = ad->gf_index;
	int flags = 0;

	dst_width2 = dw;
	dst_height2 = dh;
	dst_depth2 = dd;
	amiga_width2 = vb->outwidth;
	amiga_height2 = vb->outheight;
	amiga_depth2 = vb->pixbytes * 8;

	S2X_free(monid);
	changed_prefs.leds_on_screen |= STATUSLINE_TARGET;
	currprefs.leds_on_screen |= STATUSLINE_TARGET;
	statusline_set_multiplier(monid, dw, dh);

	dd = amiga_depth2;

	if (dd == 32)
		alloc_colors_rgb (8, 8, 8, 16, 8, 0, 0, 0, 0, 0, rc, gc, bc);
	else
		alloc_colors_rgb (5, 6, 5, 11, 5, 0, 0, 0, 0, 0, rc, gc, bc);

	if (WIN32GFX_IsPicassoScreen(mon))
		return true;

	if (!currprefs.gf[idx].gfx_filter || !usedfilter) {
		usedfilter = &uaefilters[0];
		scale = 1;
	} else {
		scale = usedfilter->intmul;
		flags = usedfilter->flags;
		if ((amiga_depth2 == 16 && !(flags & UAE_FILTER_MODE_16)) || (amiga_depth2 == 32 && !(flags & UAE_FILTER_MODE_32))) {
			usedfilter = &uaefilters[0];
			scale = 1;
			changed_prefs.gf[idx].gfx_filter = usedfilter->type;
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

	int mh = currprefs.gf[idx].gfx_filter_filtermodeh + 1;
	if (mh < scale)
		mh = scale;
	temp_width = dst_width * mh;
	int mv = currprefs.gf[idx].gfx_filter_filtermodev + 1;
	if (mv < scale)
		mv = scale;
	temp_height = dst_height * mv;

	if (usedfilter->type == UAE_FILTER_HQ2X || usedfilter->type == UAE_FILTER_HQ3X || usedfilter->type == UAE_FILTER_HQ4X) {
		int w = amiga_width > dst_width ? amiga_width : dst_width;
		int h = amiga_height > dst_height ? amiga_height : dst_height;
		tempsurf2 = xmalloc (uae_u8, w * h * (amiga_depth / 8) * ((scale + 1) / 2));
		tempsurf3 = xmalloc (uae_u8, w * h *(dst_depth / 8) * 4 * scale);
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
	int idx = ad->gf_index;
	int aw, ah, aws, ahs;
	uae_u8 *dptr, *enddptr, *sptr, *endsptr;
	int ok = 0;
	int pitch, surf_height, surf_width;
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

	if (D3D_restore)
		D3D_restore(monid, true);
	surfstart = D3D_locktexture(monid, &pitch, &surf_width, &surf_height, y_start < -1 ? -1 : (y_start < 0 ? 1 : 0));
	if (surfstart == NULL) {
		return;
	}
	dptr = surfstart;
	enddptr = dptr + pitch * surf_height;

	if (!dptr) /* weird things can happen */
		goto end;
	if (dptr < surfstart)
		dptr = surfstart;

	if (usedfilter->type == UAE_FILTER_SCALE2X) { /* 16+32/2X */

		if (dptr + pitch * ah * 2 >= enddptr)
			ah = (int)((enddptr - dptr) / (pitch * 2));

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
			ah = (int)((enddptr - dptr) / (pitch * 2));

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
			ah = (int)((enddptr - dptr) / (pitch * 2));

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
			ah = (int)((enddptr - dptr) / (pitch * 2));

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

	if (ok == 0 && currprefs.gf[idx].gfx_filter) {
		usedfilter = &uaefilters[0];
		changed_prefs.gf[idx].gfx_filter = usedfilter->type;
	}
end:;
}

void S2X_refresh(int monid)
{
	if (monid)
		return;
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
