
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

static float filteroffsetx, filteroffsety, filterxmult = 1.0, filterymult = 1.0;
static int dst_width, dst_height;
uae_u8 *bufmem_ptr;
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

static bool getmanualpos(int monid, int *cxp, int *cyp, int *cwp, int *chp)
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

	return currprefs.gfx_xcenter_pos >= 0 || currprefs.gfx_ycenter_pos >= 0 || currprefs.gfx_xcenter_size > 0 || currprefs.gfx_ycenter_size > 0;
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
				bool manual = false;

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
						manual = true;
					}
				}
				if (scalemode == AUTOSCALE_INTEGER || ok == false) {
					manual = getmanualpos(monid, &cx, &cy, &cw, &ch);
					crealh = ch;
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

				float m = 1.0f;
				if (manual) {
					if (currprefs.gfx_resolution >= currprefs.gfx_vresolution) {
						m = (float)(1 << (currprefs.gfx_resolution - currprefs.gfx_vresolution));
					} else {
						m = 1.0f / (1 << (currprefs.gfx_vresolution - currprefs.gfx_resolution));
					}
				}

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
						while (((cw2 * (mult + multadd)) / m) - adjw <= maxw && ch2 * (mult + multadd) - adjh <= maxh) {
							mult += multadd;
						}

						float multx = mult, multy = mult;
						// if width is smaller than height, double width (programmed modes)
						if (cw2 * (mult + multadd) - adjw <= maxw && cw2 < ch2) {
							multx *= 2;
						}
						// if width is >=2.4x height, double height (non-doublescanned superhires)
						if (ch2 * (mult * 2) - adjh <= maxh && cw2 > ch2 * 2.4) {
							multy *= 2;
						}
						maxw = (int)((maxw + multx - multadd) / multx);
						maxh = (int)((maxh + multy - multadd) / multy);
					}
				}

				*mode = 1;

				dstratio = 1.0f * cw / ch;

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

			crealh = ch;
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

	if (!vb)
		return;
	unlockscr3d(vb);
}

uae_u8 *getfilterbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->outbuffer;
	int w, h;

	*widthp = 0;
	*heightp = 0;
	*depth = 32;
	if (!vb)
		return NULL;
	if (!lockscr3d(vb)) {
		return NULL;
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
}

#endif
