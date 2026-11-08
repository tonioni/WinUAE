#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "gfxfilter.h"
#include "scaler.h"
#ifdef GFXFILTER
void host_rtg_filterdata(displayscale *ds, int native_width, int native_height, int src_width,
			 int src_height, int hdouble, int vdouble, int mode, bool enabled,
			 host_rtg_offset *offset)
{
    offset->x = 0;
    offset->y = 0;
    offset->mx = 1.0;
    offset->my = 1.0;

    ds->mode = 0;
    ds->outwidth = native_width;
    ds->outheight = native_height;

    if (!enabled)
	return;

    if (currprefs.gf[GF_RTG].gfx_filter_horiz_zoom_mult > 0) {
	offset->mx *= currprefs.gf[GF_RTG].gfx_filter_horiz_zoom_mult;
    }
    if (currprefs.gf[GF_RTG].gfx_filter_vert_zoom_mult > 0) {
	offset->my *= currprefs.gf[GF_RTG].gfx_filter_vert_zoom_mult;
    }

    if (!mode || currprefs.host_rtgnonsquarepixels)
	return;

    int srcratio, dstratio;
    int srcwidth, srcheight;
    int outwidth, outheight;

    srcwidth = src_width;
    srcheight = src_height;
    if (srcwidth <= 0 || srcheight <= 0 || native_width <= 0 || native_height <= 0 ||
	hdouble <= 0 || vdouble <= 0)
	return;

    float mx = (float)native_width / srcwidth;
    float my = (float)native_height / srcheight;

    if (mode == 3) {
	float fdivx = (float)native_width / srcwidth;
	float fdivy = (float)native_height / srcheight;
	float fmul = (float)(fdivx < 1.0 || fdivy < 1.0 ? 1 : (fdivx > fdivy ? fdivy : fdivx));
	int mul = (int)(fmul * (1 << (currprefs.gf[GF_RTG].gfx_filter_integerscalelimit)));
	fmul = (float)mul / (1 << (currprefs.gf[GF_RTG].gfx_filter_integerscalelimit));
	if (fdivx < 1.0 || fdivy < 1.0) {
	    float adjust = 0.5f / (1 << (currprefs.gf[GF_RTG].gfx_filter_integerscalelimit));
	    fmul = 1.0f;
	    float prevfmul = fmul;
	    for (;;) {
		if (fmul <= adjust) {
		    adjust /= 2.0f;
		    if (adjust < 0.10f) {
			break;
		    }
		}
		fmul -= adjust;
		if (fmul < 0.10f) {
		    fmul = 0.10f;
		    break;
		}
		if ((float)native_width / srcwidth > 2 * fmul ||
		    ((float)native_height / srcheight > 2 * fmul)) {
		    fmul = prevfmul;
		    break;
		}
		prevfmul = fmul;
	    }
#if 0
			if ((float)native_width / srcwidth <= 0.95f || ((float)native_height / srcheight <= 0.95f)) {
				fmul = 1.0f / 2.0f;
			}
			if ((float)native_width / srcwidth <= 0.45f || ((float)native_height / srcheight <= 0.45f)) {
				fmul = 1.0f / 4.0f;
			}
#endif
	}
	ds->outwidth = (int)(native_width / fmul);
	ds->outheight = (int)(native_height / fmul);
	int xx = (int)((native_width / fmul - srcwidth) / 2);
	int yy = (int)((native_height / fmul - srcheight) / 2);
	offset->x = -xx;
	offset->y = -yy;
	mx = fmul;
	my = fmul;
	outwidth = srcwidth;
	outheight = srcheight;
	ds->mode = 1;
    } else if (mode == 2) {
	int xx = (native_width - srcwidth) / 2;
	int yy = (native_height - srcheight) / 2;
	offset->x = -xx;
	offset->y = -yy;
	ds->outwidth = native_width;
	ds->outheight = native_height;
	outwidth = ds->outwidth;
	outheight = ds->outheight;
	mx = my = 1.0;
    } else {
	if (currprefs.host_rtgscaleaspectratio < 0) {
	    // automatic
	    srcratio = srcwidth * ASPECTMULT / srcheight;
	    dstratio = native_width * ASPECTMULT / native_height;
	} else if (currprefs.host_rtgscaleaspectratio == 0) {
	    // none
	    srcratio = dstratio = 0;
	} else if (currprefs.host_rtgscaleaspectratio & (ASPECTMULT - 1)) {
	    // manual
	    dstratio = (currprefs.host_rtgscaleaspectratio / ASPECTMULT) * ASPECTMULT /
		       (currprefs.host_rtgscaleaspectratio & (ASPECTMULT - 1));
	    srcratio = srcwidth * ASPECTMULT / srcheight;
	} else {
	    srcratio = dstratio = 0;
	}

	if (srcratio == dstratio) {
	    ds->outwidth = srcwidth;
	    ds->outheight = srcheight;
	} else if (srcratio > dstratio) {
	    int yy = srcheight * srcratio / dstratio;
	    ds->outwidth = srcwidth;
	    ds->outheight = yy;
	    offset->y = (src_height - yy) / 2;
	} else {
	    int xx = srcwidth * dstratio / srcratio;
	    ds->outwidth = xx;
	    ds->outheight = srcheight;
	    offset->x = (src_width - xx) / 2;
	}
	outwidth = ds->outwidth;
	outheight = ds->outheight;
    }

    ds->xoffset += offset->x;
    ds->yoffset += offset->y;

    offset->x /= hdouble;
    offset->y /= vdouble;

    offset->mx = (float)(srcwidth * mx * hdouble) / outwidth;
    offset->my = (float)(srcheight * my * vdouble) / outheight;
}

#endif
