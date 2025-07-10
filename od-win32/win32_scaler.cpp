
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

void getfilteroffset(int monid, float *dx, float *dy, float *mx, float *my)
{
	*dx = filteroffsetx;
	*dy = filteroffsety;
	*mx = filterxmult;
	*my = filterymult;
}

static void sizeoffset(struct displayscale *ds, int w, int h)
{
	ds->outwidth -= w;
	ds->outheight -= h;
	ds->xoffset += w / 2;
	ds->yoffset += h / 2;
}

static bool getmanualpos(int monid, int *cxp, int *cyp, int *cwp, int *chp)
{
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	int v, cx, cy, cw, ch;
	bool native = isnativevidbuf(monid);
	int hres = gethresolution();

	cx = *cxp;
	cy = *cyp;
	v = currprefs.gfx_xcenter_pos;
	if (v >= 0) {
		cx = (v >> (RES_MAX - currprefs.gfx_resolution)) - cx;
	}

	v = currprefs.gfx_ycenter_pos;
	if (v >= 0) {
		cy = (v >> (VRES_MAX - currprefs.gfx_vresolution)) - cy;
	}

	v = currprefs.gfx_xcenter_size;
	if (v <= 0) {
		if (programmedmode && native) {
			cw = maxhpos_display << (RES_MAX + (doublescan == 1));
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
			ch = (current_linear_vpos - minfirstline) << (VRES_MAX - (doublescan == 1 && !interlace_seen));
		} else if (currprefs.gfx_overscanmode <= OVERSCANMODE_OVERSCAN) {
			// keep old version compatiblity
			ch = native ? (ispal(NULL) ? AMIGA_HEIGHT_MAX_PAL : AMIGA_HEIGHT_MAX_NTSC) << VRES_MAX : avidinfo->outbuffer->outheight;
		} else {
			ch = native ? (current_linear_vpos - minfirstline) << VRES_MAX : avidinfo->outbuffer->outheight;
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
					if (keep_aspect == 2 && !isp)
						dstratio = dstratio * 0.93f;
					else if (keep_aspect == 1 && isp)
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

void getfilterdata(int monid, struct displayscale *ds)
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
	ds->mode = 0;

	if (mon->screen_is_picasso) {
		getrtgfilterdata(monid, ds);
		if (D3D_getscalerect && D3D_getscalerect(monid, &mrmx, &mrmy, &mrsx, &mrsy, ds->srcwidth, ds->srcheight)) {
			ds->xoffset += (int)mrsx;
			ds->yoffset += (int)mrsy;
			sizeoffset(ds, (int)mrmx, (int)mrmy);
		}
		return;
	}

	fpux_save (&fpuv);

	aws = ds->srcwidth * ds->scale;
	ahs = ds->srcheight * ds->scale;
	//write_log (_T("%d %d %d\n"), dst_width, temp_width, aws);
	extraw = (int)(-aws * (filter_horiz_zoom - currprefs.gf[idx].gfx_filteroverlay_overscan * 10) / 2.0f);
	extrah = (int)(-ahs * (filter_vert_zoom - currprefs.gf[idx].gfx_filteroverlay_overscan * 10) / 2.0f);

	extraw2 = 0;
	if (D3D_getscalerect && D3D_getscalerect(monid, &mrmx, &mrmy, &mrsx, &mrsy, ds->srcwidth, ds->srcheight)) {
		extraw2 = (int)mrmx;
		//extrah -= mrmy;
	}

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
		if (ds->dstwidth >= 640 && ds->dstwidth <= 800 && ds->dstheight >= 480 && ds->dstheight <= 600 && !programmedmode) {
			autoselect = 1;
			scalemode = AUTOSCALE_NONE;
			int m = 1;
			int w = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
			int h = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;
			for (;;) {
				if (w * (m * 2) > ds->dstwidth || h * (m * 2) > ds->dstheight) {
					break;
				}
				m *= 2;
			}
			autoselect = m;
		} else {
			float dstratio = 1.0f * ds->dstwidth / ds->dstheight;
			scalemode = AUTOSCALE_STATIC_NOMINAL;
			if (dstratio > srcratio + 0.1 || dstratio < srcratio - 0.1) {
				filter_aspect = -1;
			}
		}
	}

	if (filter_aspect > 0) {
		dstratio = (filter_aspect / ASPECTMULT) * 1.0f / (filter_aspect & (ASPECTMULT - 1));
	} else if (filter_aspect < 0) {
		dstratio = 1.0f * ds->dstwidth / ds->dstheight;
	} else {
		dstratio = srcratio;
	}

	bool scl = false;
	int width_aspect = -1;
	int height_aspect = -1;
	bool autoaspect_done = false;

	if (scalemode) {
		int cw, ch, cx, cy, cv = 0, crealh = 0;
		int hres = currprefs.gfx_resolution;
		int vres = currprefs.gfx_vresolution;
		static int oxmult, oymult;

		filterxmult = (float)ds->scale;
		filterymult = (float)ds->scale;

		ds->xoffset = 0;
		ds->yoffset = 0;

		if (scalemode == AUTOSCALE_STATIC_MAX || scalemode == AUTOSCALE_STATIC_NOMINAL ||
			scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {

			if (scalemode == AUTOSCALE_STATIC_NOMINAL || scalemode == AUTOSCALE_STATIC_NOMINAL || scalemode == AUTOSCALE_STATIC_MAX) {
				// do not default/TV scale programmed modes
				if (programmedmode) {
					goto skipcont;
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
					if (currprefs.gfx_overscanmode < OVERSCANMODE_ULTRA) {
						cx = 28 << currprefs.gfx_resolution;
						cy = 10 << currprefs.gfx_vresolution;
						cw -= 40 << currprefs.gfx_resolution;
						ch -= 20 << currprefs.gfx_vresolution;
						if (currprefs.gfx_overscanmode == OVERSCANMODE_BROADCAST) {
							cx -= 4 << currprefs.gfx_resolution;
							cy -= 2 << currprefs.gfx_vresolution;
							cw += 8 << currprefs.gfx_resolution;
							ch += 4 << currprefs.gfx_vresolution;
						} else if (currprefs.gfx_overscanmode == OVERSCANMODE_EXTREME) {
							cx -= 7 << currprefs.gfx_resolution;
							cy -= 10 << currprefs.gfx_vresolution;
							cw += 14 << currprefs.gfx_resolution;
							ch += 20 << currprefs.gfx_vresolution;
						}
					}
				}
				if (scalemode != AUTOSCALE_INTEGER && scalemode != AUTOSCALE_INTEGER_AUTOSCALE) {
					set_custom_limits (cw, ch, cx, cy, true);
					store_custom_limits (cw, ch, cx, cy);
					scl = true;
				}
			}

			if (scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE) {
				int maxw = isfullscreen() < 0 ? ds->dstwidth : gmc->gfx_size.width;
				int maxh = isfullscreen() < 0 ? ds->dstheight : gmc->gfx_size.height;
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
					ok = get_custom_limits (&cw, &ch, &cx, &cy, &crealh, &hres, &vres) != 0;
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
				mult = 1.0f;
				xmult = 1.0f;
				ymult = 1.0f;
				filter_horiz_zoom = 0;
				filter_vert_zoom = 0;
				filter_horiz_zoom_mult = 1.0;
				filter_vert_zoom_mult = 1.0;

				if (hres >= vres) {
					ymult = (float)(1 << (hres - vres));
				} else {
					xmult = (float)(1 << (vres - hres));
				}

				float multadd = 1.0f / (1 << currprefs.gf[idx].gfx_filter_integerscalelimit);
				if (cw2 > 0 && ch2 > 0) {
					if (cw2 * xmult > maxw || ch2 * ymult > maxh) {
						while (cw2 * xmult / mult - adjw > maxw || ch2 * ymult / mult - adjh > maxh) {
							mult += multadd;
						}
						maxw = (int)((maxw / xmult) * mult);
						maxh = (int)((maxh / ymult) * mult);
					} else {
						while ((cw2 * (xmult * mult + multadd)) - adjw <= maxw && ch2 * (ymult * mult + multadd) - adjh <= maxh) {
							mult += multadd;
						}
						maxw = (int)(maxw / (xmult * mult));
						maxh = (int)(maxh / (ymult * mult));
					}
				}

				ds->mode = 1;
				cv = 2;

				dstratio = 1.0f * (cw * xmult) / (ch * ymult);

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

			cv = get_custom_limits(&cw, &ch, &cx, &cy, &crealh, &hres, &vres);
			if (cv) {
				store_custom_limits(cw, ch, cx, cy);
			}

		} else if (scalemode == AUTOSCALE_RESIZE) {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh, &hres, &vres);
			if (cv) {
				set_custom_limits(cw, ch, cx, cy, false);
				store_custom_limits(cw, ch, cx, cy);
				scl = true;
			}

		} else {

			cv = get_custom_limits (&cw, &ch, &cx, &cy, &crealh, &hres, &vres);
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

				int ww = cw * ds->scale;
				int hh = ch * ds->scale;

				ds->outwidth = ds->dstwidth * ds->scale;
				ds->outheight = ds->dstheight * ds->scale;
				ds->xoffset += cx * ds->scale - (ds->dstwidth - ww) / 2;
				ds->yoffset += cy * ds->scale - (ds->dstheight - hh) / 2;

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

				ds->outwidth = (int)(cw * ds->scale);
				ds->outheight = (int)(ch * ds->scale);
				ds->xoffset += cx * ds->scale;
				ds->yoffset += cy * ds->scale;

				int ww = (int)(ds->outwidth * scalex);
				int hh = (int)(ds->outheight * scaley);
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
				ds->xoffset += -(gmh->gfx_size_win.width - ww + 1) / 2;
				ds->yoffset += -(gmh->gfx_size_win.height - hh + 1) / 2;

				filteroffsetx = (float)-ds->xoffset / ds->scale;
				filteroffsety = (float)-ds->yoffset / ds->scale;
				goto end;
			}

			ds->outwidth = cw * ds->scale;
			ds->outheight = ch * ds->scale;
			ds->xoffset = cx * ds->scale;
			ds->yoffset = cy * ds->scale;

			sizeoffset(ds, extraw, extrah);

			filteroffsetx = (float)-ds->xoffset / ds->scale;
			filteroffsety = (float)-ds->yoffset / ds->scale;

			bool aspect = false;

			int diffx, diffy;

			if (width_aspect > 0) {
				diffx = width_aspect;
			} else {
				diffx = ds->outwidth;
			}
			if (height_aspect > 0) {
				diffy = height_aspect;
			} else {
				diffy = ds->outheight;
			}

			if (get_aspect(monid, &dstratio, &srcratio, &xmult, &ymult, doautoaspect, autoaspectratio, keep_aspect, filter_aspect)) {
				diff = diffx - (int)(diffx * xmult);
				sizeoffset(ds, diff, 0);
				filteroffsetx += -diff / 2;

				diff = diffy - (int)(diffy * ymult);
				sizeoffset(ds, 0, diff);
				filteroffsety += -diff / 2;
			}

			ds->xoffset += (int)(-filter_horiz_offset * aws);
			ds->yoffset += (int)(-filter_vert_offset * ahs);

			diff = ds->outwidth;
			filterxmult = ((float)ds->dstwidth * ds->scale) / diff;
			diff = ds->outheight;
			filterymult = ((float)ds->dstheight * ds->scale) / diff;

			goto end;
		}

	} else {
skipcont:
		int cw = avidinfo->drawbuffer.inwidth;
		int ch = avidinfo->drawbuffer.inheight;
		set_custom_limits(cw, ch, 0, 0, true);

		ds->outwidth = ds->dstwidth;
		ds->outheight = ds->dstheight;
		ds->xoffset = (ds->srcwidth - ds->dstwidth) / 2;
		ds->yoffset = (ds->srcheight - ds->dstheight) / 2;

	}
cont:
	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {

		sizeoffset(ds, extraw, extrah);

		if (keep_aspect) {
			float xm, ym, m;

			xm = (float)aws / ds->dstwidth;
			ym = (float)ahs / ds->dstheight;
			if (xm < ym)
				xm = ym;
			else
				ym = xm;
			xmult = ymult = xm;

			m = (float)(aws * ds->dstwidth) / (ahs * ds->dstheight);
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
		xmult = (float)ds->dstwidth / aws;
	else
		xmult = xmult + xmult * filter_horiz_zoom / 2.0f;
	if (ymult <= 0.01)
		ymult = (float)ds->dstheight / ahs;
	else
		ymult = ymult + ymult * filter_vert_zoom / 2.0f;

	if (!filter_horiz_zoom_mult && !filter_vert_zoom_mult) {
		if (currprefs.ntscmode) {
			ymult /= 1.21f;
		}
	}

	ds->xoffset += (int)(-filter_horiz_offset * aws);
	ds->yoffset += (int)(-filter_vert_offset * ahs);

	xs = ds->dstwidth;
	if (xmult) {
		xs -= (int)(ds->dstwidth / xmult);
	}
	ys = ds->dstheight;
	if (ymult) {
		ys -= (int)(ds->dstheight / ymult);
	}
	sizeoffset(ds, xs, ys);

	filterxmult = xmult;
	filterymult = ymult;
	filteroffsetx += (ds->dstwidth - ds->srcwidth * filterxmult) / 2;
	filteroffsety += (ds->dstheight - ds->srcheight * filterymult) / 2;

end:

	if (D3D_getscalerect && D3D_getscalerect(monid, &mrmx, &mrmy, &mrsx, &mrsy, ds->srcwidth, ds->srcheight)) {
		ds->xoffset += (int)mrsx;
		ds->yoffset += (int)mrsy;
		sizeoffset(ds, (int)mrmx, (int)mrmy);
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

void freefilterbuffer(int monid, uae_u8 *buf, bool unlock)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->outbuffer;

	if (!vb)
		return;
	if (unlock) {
		unlockscr(vb, -1, -1);
	}
}

uae_u8 *getfilterbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth, bool *locked)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->outbuffer;
	int w, h;

	*widthp = 0;
	*heightp = 0;
	*depth = 32;
	*locked = false;
	if (!vb || mon->screen_is_picasso)
		return NULL;
	if (!vb->locked) {
		if (!lockscr(vb, false, false)) {
			return NULL;
		}
		*locked = true;
	}
	w = vb->outwidth;
	h = vb->outheight;
	if (!monid && currprefs.gfx_overscanmode <= OVERSCANMODE_BROADCAST) {
		// if native screen: do not include vertical blank
		h = get_vertical_visible_height(false);
		if (h > vb->outheight) {
			h = vb->outheight;
		}
	}
	if (pitch) {
		*pitch = vb->rowbytes;
	}
	// remove short/long line reserved areas
	int extra = 1 << gethresolution();
	w -= 2 * extra;
	*widthp = w;
	*heightp = h;
	*depth = vb->pixbytes * 8;
	return vb->bufmem ? vb->bufmem + extra * sizeof(uae_u32) : NULL;
}

#endif
