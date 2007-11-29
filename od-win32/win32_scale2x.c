
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

struct uae_filter uaefilters[] =
{
    { UAE_FILTER_NULL, 0, "Null filter", "null", 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32, 0, 0, 0 },

    { UAE_FILTER_DIRECT3D, 0, "Direct3D", "direct3d", 1, 0, 0, 0, 0 },

    { UAE_FILTER_OPENGL, 0, "OpenGL", "opengl", 1, 0, 0, 0, 0 },

    { UAE_FILTER_SCALE2X, 0, "Scale2X", "scale2x", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32, 0, 0 },

    { UAE_FILTER_HQ, 0, "hq2x", "hqx", 0, 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_16_32, 0, 0 },

    { UAE_FILTER_SUPEREAGLE, 0, "SuperEagle", "supereagle", 0, 0, UAE_FILTER_MODE_16_16, 0, 0 },

    { UAE_FILTER_SUPER2XSAI, 0, "Super2xSaI", "super2xsai", 0, 0, UAE_FILTER_MODE_16_16, 0, 0 },

    { UAE_FILTER_2XSAI, 0, "2xSaI", "2xsai", 0, 0, UAE_FILTER_MODE_16_16, 0, 0 },

    { UAE_FILTER_PAL, 1, "PAL", "pal", 0, UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32, 0, 0, 0 },

    { 0 }
};


static int dst_width, dst_height, amiga_width, amiga_height, amiga_depth, dst_depth, scale;
uae_u8 *bufmem_ptr;
int bufmem_width, bufmem_height;
static int tempsurf;
static uae_u8 *tempsurf2, *tempsurf3;

void S2X_configure (int rb, int gb, int bb, int rs, int gs, int bs)
{
    Init_2xSaI (rb, gb, bb, rs, gs, bs);
    hq_init (rb, gb, bb, rs, gs, bs);
    PAL_init ();
    bufmem_ptr = 0;
}

void S2X_free (void)
{
    if (tempsurf)
	IDirectDrawSurface7_Release (DirectDrawState.temporary.surface);
    tempsurf = 0;
    xfree (tempsurf2);
    tempsurf2 = 0;
    xfree (tempsurf3);
    tempsurf3 = 0;
}

void S2X_init (int dw, int dh, int aw, int ah, int mult, int ad, int dd)
{
    int flags = 0;
    HRESULT ddrval;

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
    dst_width = dw;
    dst_height = dh;
    dst_depth = dd;
    amiga_width = aw;
    amiga_height = ah;
    amiga_depth = ad;
    scale = mult;

    tempsurf = 1;
    ZeroMemory (&DirectDrawState.temporary.desc, sizeof (DDSURFACEDESC2));
    DirectDrawState.temporary.desc.dwSize = sizeof (DDSURFACEDESC2);
    DirectDrawState.temporary.desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    DirectDrawState.temporary.desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    DirectDrawState.temporary.desc.dwWidth = dst_width;
    DirectDrawState.temporary.desc.dwHeight = dst_height;
    if (DirectDraw_GetPrimaryPixelFormat (&DirectDrawState.temporary.desc.ddpfPixelFormat))
	DirectDrawState.temporary.desc.dwFlags |= DDSD_PIXELFORMAT;
    ddrval = IDirectDraw7_CreateSurface (DirectDrawState.directdraw.dd,
					    &DirectDrawState.temporary.desc,
					    &DirectDrawState.temporary.surface,
					    NULL);
    if (FAILED(ddrval)) {
	write_log ("DDRAW: failed to create temp surface\n%s\n", DXError (ddrval));
	tempsurf = 0;
    }

    if (usedfilter->type == UAE_FILTER_HQ) {
	int w = amiga_width > dst_width ? amiga_width : dst_width;
	int h = amiga_height > dst_height ? amiga_height : dst_height;
	tempsurf2 = xmalloc (w * h * (amiga_depth / 8));
	tempsurf3 = xmalloc (w * h *(dst_depth / 8) * 4);
    }
}

void S2X_render (void)
{
    int aw = amiga_width, ah = amiga_height, v, pitch;
    uae_u8 *dptr, *sptr, *endsptr;
    int ok = 0, temp_needed = 0;
    RECT sr, dr;
    HRESULT ddrval;
    LPDIRECTDRAWSURFACE7 dds;
    DDSURFACEDESC2 desc;

    sptr = gfxvidinfo.bufmem;
    v = (dst_width - amiga_width) / 2;
    aw += v;
    sptr -= v * (amiga_depth / 8);
    v = (dst_height - amiga_height) / 2;
    ah += v;
    sptr -= v * gfxvidinfo.rowbytes;

    endsptr = gfxvidinfo.bufmemend;

    v = currprefs.gfx_filter ? currprefs.gfx_filter_horiz_offset : 0;
    v += (dst_width / scale - amiga_width) / 8;
    sptr += (int)(-v * 4.0 / 10.0) * (amiga_depth / 8);
    aw -= (int)(-v * 4.0 / 10);

    v = currprefs.gfx_filter ? currprefs.gfx_filter_vert_offset : 0;
    v += (dst_height / scale - amiga_height) / 8;
    sptr += (int)(-v * 4.0 / 10.0) * gfxvidinfo.rowbytes;
    ah -= (int)(-v * 4.0 / 10);

    if (aw * scale > dst_width)
	aw = dst_width / scale;
    if (ah * scale > dst_height)
	ah = dst_height / scale;

    if (ah < 16)
	return;
    if (aw < 16)
	return;

    if (currprefs.gfx_filter && (currprefs.gfx_filter_horiz_zoom || currprefs.gfx_filter_vert_zoom ||
	    currprefs.gfx_filter_horiz_zoom_mult != 1000 ||
	    currprefs.gfx_filter_vert_zoom_mult != 1000)) {
	int wz = dst_width * currprefs.gfx_filter_horiz_zoom_mult / 1000;
	int hz = dst_height * currprefs.gfx_filter_vert_zoom_mult / 1000;
	wz += currprefs.gfx_filter_horiz_zoom / 4;
	hz += currprefs.gfx_filter_vert_zoom / 4;
	sr.left = (dst_width - wz) / 2;
	sr.top = (dst_height - hz) / 2;
	sr.right = sr.left + wz;
	sr.bottom = sr.top + hz;
	dr.left = dr.top = 0;
	dr.right = dst_width;
	dr.bottom = dst_height;
	if (sr.left >= sr.right) {
	    sr.left = dst_width / 2 - 1;
	    sr.right = dst_width / 2 + 1;
	}
	if (sr.left < 0) {
	    dr.left = -sr.left;
	    sr.left = 0;
	}
	if (sr.right - sr.left > dst_width) {
	    dr.right = dst_width - (sr.right - dst_width);
	    sr.right = sr.left + dst_width;
	}
	if (sr.top >= sr.bottom) {
	    sr.top = dst_height / 2 - 1;
	    sr.bottom = dst_height / 2 + 1;
	}
	if (sr.top < 0) {
	    dr.top = -sr.top;
	    sr.top = 0;
	}
	if (sr.bottom - sr.top > dst_height) {
	    dr.bottom = dst_height - (sr.bottom - dst_height);
	    sr.bottom = sr.top + dst_height;
	}

	if (tempsurf && sr.left != 0 || sr.top != 0 || sr.right != dst_width || sr.bottom != dst_height ||
	    dr.top != 0 || dr.right != dst_width || dr.left != 0 || dr.bottom != dst_height) {
		dds = DirectDrawState.temporary.surface;
		temp_needed = 1;
	}
    }

    bufmem_ptr = sptr;
    bufmem_width = aw;
    bufmem_height = ah;

    if (temp_needed) {
	desc.dwSize = sizeof (desc);
	while (FAILED(ddrval = IDirectDrawSurface7_Lock (dds, NULL, &desc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL))) {
	    if (ddrval == DDERR_SURFACELOST) {
		ddrval = IDirectDrawSurface7_Restore (dds);
		if (FAILED(ddrval))
		    return;
	    } else if (ddrval != DDERR_SURFACEBUSY) {
		return;
	    }
	}
	dptr = (uae_u8*)desc.lpSurface;
	pitch = desc.lPitch;
    } else {
	if (!DirectDraw_SurfaceLock (lockable_surface))
	    return;
	dptr = DirectDraw_GetSurfacePointer ();
	pitch = DirectDraw_GetSurfacePitch ();
    }

    if (usedfilter->type == UAE_FILTER_SCALE2X ) { /* 16+32/2X */

	if (amiga_depth == 16 && dst_depth == 16) {
	    AdMame2x (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	} else if (amiga_depth == 32 && dst_depth == 32) {
	    AdMame2x32 (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else if (usedfilter->type == UAE_FILTER_HQ) { /* 32/2X+3X+4X */

	if (tempsurf2 && scale == 2) {
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
	        hq2x_32 (tempsurf2, tempsurf3, aw, ah, aw * scale * 4);
	        ok = 1;
	    } else if (amiga_depth == 16 && dst_depth == 16) {
	        hq2x_16 (tempsurf2, tempsurf3, aw, ah, aw * scale * 2);
	        ok = 1;
	    }
	    for (i = 0; i < ah * scale; i++) {
		int w = aw * scale * (dst_depth / 8);
		memcpy (dptr, sptr2, w);
		sptr2 += w;
		dptr += pitch;
	    }
	}

    } else if (usedfilter->type == UAE_FILTER_SUPEREAGLE) { /* 16/2X */

	if (scale == 2 && amiga_depth == 16 && dst_depth == 16) {
	    SuperEagle (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else if (usedfilter->type == UAE_FILTER_SUPER2XSAI) { /* 16/2X */

	if (scale == 2 && amiga_depth == 16 && dst_depth == 16) {
	    Super2xSaI (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else if (usedfilter->type == UAE_FILTER_2XSAI) { /* 16/2X */

	if (scale == 2 && amiga_depth == 16 && dst_depth == 16) {
	    _2xSaI (sptr, gfxvidinfo.rowbytes, dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else if (usedfilter->type == UAE_FILTER_PAL) { /* 16/32/1X */

	if (amiga_depth == 32 && dst_depth == 32) {
	    PAL_1x1_32((uae_u32*)sptr, gfxvidinfo.rowbytes, (uae_u32*)dptr, pitch, aw, ah);
	    ok = 1;
	} else if (amiga_depth == 16 && dst_depth == 16) {
	    PAL_1x1_16((uae_u16*)sptr, gfxvidinfo.rowbytes, (uae_u16*)dptr, pitch, aw, ah);
	    ok = 1;
	}

    } else { /* null */

	if (amiga_depth == dst_depth) {
	    int y;
	    for (y = 0; y < dst_height; y++) {
		if (sptr < endsptr && sptr >= gfxvidinfo.bufmem)
		    memcpy (dptr, sptr, dst_width * dst_depth / 8);
		else
		    memset (dptr, 0, dst_width * dst_depth / 8);
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

    if (temp_needed) {
	IDirectDrawSurface7_Unlock (dds, NULL);
	DirectDraw_Blt (DirectDraw_GetLockableType(), &dr, temporary_surface, &sr, 0, NULL);
    } else {
	DirectDraw_SurfaceUnlock ();
    }
}

void S2X_refresh (void)
{
    int y, pitch;
    uae_u8 *dptr;

    if (!DirectDraw_SurfaceLock (lockable_surface))
	return;
    dptr = DirectDraw_GetSurfacePointer ();
    pitch = DirectDraw_GetSurfacePitch();
    for (y = 0; y < dst_height; y++)
	memset (dptr + y * pitch, 0, dst_width * dst_depth / 8);
    DirectDraw_SurfaceUnlock ();
    S2X_render ();
}

#endif
