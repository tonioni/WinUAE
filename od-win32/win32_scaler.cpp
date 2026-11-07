
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef GFXFILTER

#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "win32.h"
#include "gfxfilter.h"
#include "render.h"
#include "drawing.h"
#include "direct3d.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#include "od-generic/scaler.h"

void getfilterdata(int monid, displayscale *ds)
{
 AmigaMonitor *mon = &AMonitors[monid];
 monconfig *gmc = &currprefs.gfx_monitor[mon->monitor_id];
 host_scaler host = {};
 host.monitor_id = mon->monitor_id;
 host.rtg = mon->screen_is_picasso;
 host.integer_width = isfullscreen() < 0 ? ds->dstwidth : gmc->gfx_size.width;
 host.integer_height = isfullscreen() < 0 ? ds->dstheight : gmc->gfx_size.height;
 host.shader_rect = D3D_getscalerect;
 host.rtg_scale = getrtgfilterdata;
#ifdef RETROPLATFORM
 host.mode_changed = rp_screenmode_changed;
#endif
 int precision;
 if (!host.rtg) fpux_save(&precision);
 host_getfilterdata(monid, ds, host);
 if (!host.rtg) fpux_restore(&precision);
}

void freefilterbuffer(int monid, uae_u8 *buf, bool unlock)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->inbuffer;

	if (!vb)
		return;
	if (unlock) {
		unlockscr(vb, -1, -1);
	}
}

uae_u8 *getfilterbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth, bool *locked, int *dxp, int *dyp)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = avidinfo->inbuffer;
	int w, h;

	*widthp = 0;
	*heightp = 0;
	*depth = 32;
	*locked = false;
	*dxp = 0;
	*dyp = 0;
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
		h = get_vertical_visible_height(currprefs.gfx_overscanmode <= OVERSCANMODE_OVERSCAN);
		if (h > vb->outheight) {
			h = vb->outheight;
		}
	}
	if (pitch) {
		*pitch = vb->rowbytes;
	}
	get_filter_compatibility_offset(dxp, dyp, false);
	if (currprefs.gfx_overscanmode <= OVERSCANMODE_OVERSCAN) {
		*dyp -= 1 << currprefs.gfx_vresolution;
		if (agnusa1000) {
			*dyp -= 1 << currprefs.gfx_vresolution;
		}
		if (ecs_denise && ecs_agnus) {
			int hres = gethresolution();
			*dxp -= 1 << hres;
		}
	}
	*widthp = w;
	*heightp = h;
	*depth = vb->pixbytes * 8;
	return vb->bufmem;
}

#endif
