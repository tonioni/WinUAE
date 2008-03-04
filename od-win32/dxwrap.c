#include "sysconfig.h"

#include "sysdeps.h"
#include "options.h"

#include "dxwrap.h"
#include "win32gfx.h"

#include <dxerr9.h>


struct ddstuff dxdata;
static int flipinterval_supported = 1;

HRESULT DirectDraw_GetDisplayMode (void)
{
    HRESULT ddrval;

    dxdata.native.dwSize = sizeof (DDSURFACEDESC2);
    ddrval = IDirectDraw7_GetDisplayMode (dxdata.maindd, &dxdata.native);
    if (FAILED (ddrval))
	write_log ("IDirectDraw7_GetDisplayMode: %s\n", DXError (ddrval));
    return ddrval;
}


#define releaser(x, y) if (x) { y (x); x = NULL; }

static void freemainsurface (void)
{
    if (dxdata.dclip) {
	DirectDraw_SetClipper (NULL);
	releaser (dxdata.dclip, IDirectDrawClipper_Release);
    }
    releaser (dxdata.palette, IDirectDrawPalette_Release);
    releaser (dxdata.flipping[1], IDirectDrawSurface7_Release);
    releaser (dxdata.flipping[0], IDirectDrawSurface7_Release);
    releaser (dxdata.primary, IDirectDrawSurface7_Release);
    releaser (dxdata.secondary, IDirectDrawSurface7_Release);
    dxdata.backbuffers = 0;
}

void DirectDraw_Release (void)
{
    if (!dxdata.ddinit)
	return;
    dxdata.ddinit = 0;
    freemainsurface ();
    if (dxdata.fsmodeset)
	IDirectDraw7_RestoreDisplayMode(dxdata.maindd);
    dxdata.fsmodeset = 0;
    IDirectDraw7_SetCooperativeLevel(dxdata.maindd, dxdata.hwnd, DDSCL_NORMAL);
    releaser (dxdata.dclip, IDirectDrawClipper_Release);
    releaser (dxdata.maindd, IDirectDraw_Release);
    memset (&dxdata, 0, sizeof (dxdata));
}

int DirectDraw_Start (GUID *guid)
{
    HRESULT ddrval;

    if (dxdata.ddinit) {
	if (guid == NULL && dxdata.ddzeroguid)
	    return -1;
	if (guid && !memcmp (guid, &dxdata.ddguid, sizeof (GUID)))
	    return -1;
	DirectDraw_Release ();
    }

    ddrval = DirectDrawCreate(guid, &dxdata.olddd, NULL);
    if (FAILED(ddrval)) {
	if (guid != NULL)
	    return 0;
	goto oops;
    }
    ddrval = IDirectDraw_QueryInterface(dxdata.olddd, &IID_IDirectDraw7, (LPVOID*)&dxdata.maindd);
    if(FAILED(ddrval)) {
	gui_message("start_ddraw(): DirectX 7 or newer required");
	DirectDraw_Release();
	return 0;
    }

    if (SUCCEEDED(DirectDraw_GetDisplayMode ())) {
	dxdata.ddinit = 1;
	dxdata.ddzeroguid = 1;
	if (guid) {
	    dxdata.ddzeroguid = 0;
	    memcpy (&dxdata.ddguid, guid, sizeof (GUID));
	}
	return 1;
    }
  oops:
    write_log ("DirectDraw_Start: %s\n", DXError (ddrval));
    DirectDraw_Release();
    return 0;
}

HRESULT restoresurface (LPDIRECTDRAWSURFACE7 surf)
{
    HRESULT ddrval;
    
    if (surf == dxdata.flipping[0] || surf == dxdata.flipping[1])
	surf = dxdata.primary;
    ddrval = IDirectDrawSurface7_Restore (surf);
    if (FAILED (ddrval))
	write_log ("IDirectDrawSurface7_Restore: %s\n", DXError (ddrval));
    if (surf == dxdata.primary && dxdata.palette)
	IDirectDrawSurface7_SetPalette (dxdata.primary, dxdata.palette);
    return ddrval;
}

void clearsurface (LPDIRECTDRAWSURFACE7 surf)
{
    HRESULT ddrval;
    DDBLTFX ddbltfx;

    if (surf == NULL)
	return;
    memset(&ddbltfx, 0, sizeof (ddbltfx));
    ddbltfx.dwFillColor = 0;
    ddbltfx.dwSize = sizeof (ddbltfx);
    while (FAILED(ddrval = IDirectDrawSurface7_Blt (surf, NULL, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx))) {
	if (ddrval == DDERR_SURFACELOST) {
	    ddrval = restoresurface (dxdata.primary);
	    if (FAILED (ddrval))
		break;
	} else if (ddrval != DDERR_SURFACEBUSY) {
	    break;
	}
    }

}

int locksurface (LPDIRECTDRAWSURFACE7 surf, LPDDSURFACEDESC2 desc)
{
    HRESULT ddrval;
    desc->dwSize = sizeof (*desc);
    while (FAILED(ddrval = IDirectDrawSurface7_Lock (surf, NULL, desc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL))) {
	if (ddrval == DDERR_SURFACELOST) {
	    ddrval = restoresurface (surf);
	    if (FAILED(ddrval))
	        return 0;
	} else if (ddrval != DDERR_SURFACEBUSY) {
	    write_log ("locksurface: %s\n", DXError (ddrval));
	    return 0;
	}
    }
    return 1;
}
void unlocksurface (LPDIRECTDRAWSURFACE7 surf)
{
    HRESULT ddrval;

    ddrval = IDirectDrawSurface7_Unlock (surf, NULL);
    if (FAILED (ddrval))
	write_log ("IDirectDrawSurface7_Unlock: %s\n", DXError (ddrval));
}

LPDIRECTDRAWSURFACE7 allocsurface (int width, int height)
{
    HRESULT ddrval;
    DDSURFACEDESC2 desc = { 0 };
    LPDIRECTDRAWSURFACE7 surf;

    desc.dwSize = sizeof (desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = width;
    desc.dwHeight = height;
    memcpy (&desc.ddpfPixelFormat, &dxdata.native.ddpfPixelFormat, sizeof (DDPIXELFORMAT));
    ddrval = IDirectDraw7_CreateSurface (dxdata.maindd, &desc, &surf, NULL);
    if (FAILED (ddrval)) {
	write_log ("IDirectDraw7_CreateSurface: %s\n", DXError (ddrval));
    } else {
	clearsurface (surf);
    }
    return surf;
}
void freesurface (LPDIRECTDRAWSURFACE7 surf)
{
    if (surf)
	IDirectDrawSurface7_Release (surf);
}

void DirectDraw_FreeMainSurface (void)
{
    freemainsurface ();
}

HRESULT DirectDraw_CreateMainSurface (int width, int height)
{
    HRESULT ddrval;
    DDSURFACEDESC2 desc = { 0 };
    LPDIRECTDRAWSURFACE7 surf;

    desc.dwSize = sizeof (desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (dxdata.fsmodeset) {
	DWORD oldcaps = desc.ddsCaps.dwCaps;
	DWORD oldflags = desc.dwFlags;
	desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
	desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX | DDSCAPS_FLIP;
	desc.dwBackBufferCount = 1;
	ddrval = IDirectDraw7_CreateSurface (dxdata.maindd, &desc, &dxdata.primary, NULL);
	if (FAILED (ddrval)) {
	    desc.dwBackBufferCount = 1;
	    ddrval = IDirectDraw7_CreateSurface (dxdata.maindd, &desc, &dxdata.primary, NULL);
	}
	if (SUCCEEDED (ddrval)) {
	    DDSCAPS2 ddscaps;
	    memset (&ddscaps, 0, sizeof (ddscaps));
	    ddscaps.dwCaps = DDSCAPS_BACKBUFFER;
	    ddrval = IDirectDrawSurface7_GetAttachedSurface (dxdata.primary, &ddscaps, &dxdata.flipping[0]);
	    if(SUCCEEDED (ddrval) && desc.dwBackBufferCount > 1) {
		memset (&ddscaps, 0, sizeof (ddscaps));
		ddscaps.dwCaps = DDSCAPS_FLIP;
		ddrval = IDirectDrawSurface7_GetAttachedSurface (dxdata.flipping[0], &ddscaps, &dxdata.flipping[1]);
	    }
	    if (FAILED (ddrval))
		write_log ("IDirectDrawSurface7_GetAttachedSurface: %s\n", DXError (ddrval));
	} else {
	    desc.dwBackBufferCount = 0;
	    desc.ddsCaps.dwCaps = oldcaps;
	    desc.dwFlags = oldflags;
	    ddrval = IDirectDraw7_CreateSurface (dxdata.maindd, &desc, &dxdata.primary, NULL);
	}
    } else {
	ddrval = IDirectDraw7_CreateSurface (dxdata.maindd, &desc, &dxdata.primary, NULL);
    }
    if (FAILED (ddrval)) {
        write_log ("IDirectDraw7_CreateSurface: %s\n", DXError (ddrval));
        return ddrval;
    }
    ddrval = IDirectDrawSurface7_GetSurfaceDesc (dxdata.primary, &dxdata.native);
    if (FAILED (ddrval))
	write_log ("IDirectDrawSurface7_GetSurfaceDesc: %s\n", DXError (ddrval));
    if (dxdata.fsmodeset)
        clearsurface (dxdata.primary);
    dxdata.backbuffers = desc.dwBackBufferCount;
    clearsurface (dxdata.flipping[0]);
    clearsurface (dxdata.flipping[1]);
    surf = allocsurface (width, height);
    if (surf) {
	dxdata.secondary = surf;
	dxdata.swidth = width;
	dxdata.sheight = height;
	if (locksurface (surf, &desc)) {
	    dxdata.pitch = desc.lPitch;
	    unlocksurface (surf);
	}
    } else {
	ddrval = DD_FALSE;
    }
    return ddrval;
}

HRESULT DirectDraw_SetDisplayMode(int width, int height, int bits, int freq)
{
    HRESULT ddrval;

    if (dxdata.fsmodeset && dxdata.width == width && dxdata.height == height &&
	dxdata.depth == bits && dxdata.freq == freq)
	return DD_OK;
    ddrval = IDirectDraw7_SetDisplayMode(dxdata.maindd, width, height, bits, freq, 0);
    if (FAILED (ddrval)) {
	write_log ("IDirectDraw7_SetDisplayMode: %s\n", DXError (ddrval));
    } else {
	dxdata.fsmodeset = 1;
	dxdata.width = width;
	dxdata.height = height;
	dxdata.depth = bits;
	dxdata.freq = freq;
    }
    return ddrval;
}

HRESULT DirectDraw_SetCooperativeLevel (HWND window, int fullscreen)
{
    HRESULT ddrval;
    
    dxdata.hwnd = window;
    ddrval = IDirectDraw7_SetCooperativeLevel(dxdata.maindd, window, fullscreen ?
	DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN : DDSCL_NORMAL);
    if (FAILED (ddrval))
	write_log ("IDirectDraw7_SetCooperativeLevel: %s\n", DXError (ddrval));
    return ddrval;
}

HRESULT DirectDraw_CreateClipper (void)
{
    HRESULT ddrval;

    ddrval = IDirectDraw7_CreateClipper (dxdata.maindd, 0, &dxdata.dclip, NULL);
    if (FAILED (ddrval))
	write_log ("IDirectDraw7_CreateClipper: %s\n", DXError (ddrval));
    return ddrval;
}

HRESULT DirectDraw_SetClipper(HWND hWnd)
{
    HRESULT ddrval;

    ddrval = IDirectDrawSurface7_SetClipper (dxdata.primary, hWnd ? dxdata.dclip : NULL);
    if (FAILED (ddrval))
	write_log ("IDirectDrawSurface7_SetClipper: %s\n", DXError (ddrval));
    if(hWnd && SUCCEEDED(ddrval)) {
	ddrval = IDirectDrawClipper_SetHWnd (dxdata.dclip, 0, hWnd);
	if (FAILED (ddrval))
	    write_log ("IDirectDrawClipper_SetHWnd: %s\n", DXError (ddrval));
    }
    return ddrval;
}


char *outGUID (const GUID *guid)
{
    static char gb[64];
    if (guid == NULL)
	return "NULL";
    sprintf(gb, "%08X-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X",
	guid->Data1, guid->Data2, guid->Data3,
	guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
	guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return gb;
}

const char *DXError (HRESULT ddrval)
{
    static char dderr[1000];
    sprintf(dderr, "%08.8X S=%d F=%04.4X C=%04.4X (%d) (%s)",
	ddrval, (ddrval & 0x80000000) ? 1 : 0,
	HRESULT_FACILITY(ddrval),
	HRESULT_CODE(ddrval),
	HRESULT_CODE(ddrval),
	DXGetErrorDescription9 (ddrval));
    return dderr;
}

RGBFTYPE DirectDraw_GetSurfacePixelFormat(LPDDSURFACEDESC2 surface)
{
    int surface_is = 0;
    DDPIXELFORMAT *pfp = NULL;
    DWORD r, g, b;
    DWORD surf_flags;

    surf_flags = surface->dwFlags;
    pfp = &surface->ddpfPixelFormat;

    if((surf_flags & DDSD_PIXELFORMAT) == 0x0)
	return RGBFB_NONE;

    if ((pfp->dwFlags & DDPF_RGB) == 0)
	return RGBFB_NONE;

    r = pfp->dwRBitMask;
    g = pfp->dwGBitMask;
    b = pfp->dwBBitMask;
    switch (pfp->dwRGBBitCount) {
     case 8:
	if ((pfp->dwFlags & DDPF_PALETTEINDEXED8) != 0)
	    return RGBFB_CHUNKY;
	break;

     case 16:
	if (r == 0xF800 && g == 0x07E0 && b == 0x001F)
	    return RGBFB_R5G6B5PC;
	if (r == 0x7C00 && g == 0x03E0 && b == 0x001F)
	    return RGBFB_R5G5B5PC;
	if (b == 0xF800 && g == 0x07E0 && r == 0x001F)
	    return RGBFB_B5G6R5PC;
	if (b == 0x7C00 && g == 0x03E0 && r == 0x001F)
	    return RGBFB_B5G5R5PC;
	break;

     case 24:
	if (r == 0xFF0000 && g == 0x00FF00 && b == 0x0000FF)
	    return RGBFB_B8G8R8;
	if (r == 0x0000FF && g == 0x00FF00 && b == 0xFF0000)
	    return RGBFB_R8G8B8;
	break;

     case 32:
	if (r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF)
	    return RGBFB_B8G8R8A8;
	if (r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000)
	    return RGBFB_R8G8B8A8;
	if (r == 0xFF000000 && g == 0x00FF0000 && b == 0x0000FF00)
	    return RGBFB_A8B8G8R8;
	if (r == 0x0000FF00 && g == 0x00FF0000 && b == 0xFF000000)
	    return RGBFB_A8R8G8B8;
	break;

     default:
	write_log ("Unknown %d bit format %d %d %d\n", pfp->dwRGBBitCount, r, g, b);
	break;
    }
    return RGBFB_NONE;
}

HRESULT DirectDraw_EnumDisplayModes(DWORD flags, LPDDENUMMODESCALLBACK2 callback)
{
    HRESULT result;
    result = IDirectDraw7_EnumDisplayModes(dxdata.maindd, flags, NULL, NULL, callback);
    return result;
}

HRESULT DirectDraw_EnumDisplays(LPDDENUMCALLBACKEX callback)
{
    HRESULT result;
    result = DirectDrawEnumerateEx (callback, 0, DDENUM_DETACHEDSECONDARYDEVICES | DDENUM_ATTACHEDSECONDARYDEVICES);
    return result;
}

DWORD DirectDraw_CurrentWidth (void)
{
    return dxdata.native.dwWidth;
}
DWORD DirectDraw_CurrentHeight (void)
{
    return dxdata.native.dwHeight;
}
DWORD DirectDraw_GetCurrentDepth (void)
{
    return dxdata.native.ddpfPixelFormat.dwRGBBitCount;
}

static LPDIRECTDRAWSURFACE7 getlocksurface (void)
{
    if (dxdata.backbuffers > 0 && currprefs.gfx_afullscreen > 0 && !WIN32GFX_IsPicassoScreen ())
	return dxdata.flipping[0];
    return dxdata.secondary;
}

int DirectDraw_SurfaceLock (void)
{
    int ok;
    if (getlocksurface () == NULL)
	return 0;
    if (dxdata.lockcnt > 0)
	return 1;
    ok = locksurface (getlocksurface (), &dxdata.locksurface);
    if (ok)
	dxdata.lockcnt++;
    return ok;
}
void DirectDraw_SurfaceUnlock (void)
{
    if (dxdata.lockcnt == 0)
	return;
    dxdata.lockcnt--;
    unlocksurface (getlocksurface ());
}

void *DirectDraw_GetSurfacePointer (void)
{
    return dxdata.locksurface.lpSurface;
}
DWORD DirectDraw_GetSurfacePitch (void)
{
    return dxdata.locksurface.lPitch;
}
int DirectDraw_IsLocked (void)
{
    return dxdata.lockcnt;
}
DWORD DirectDraw_GetPixelFormatBitMask(DirectDraw_Mask_e mask)
{
    DWORD result = 0;
    switch(mask)
    {
	case red_mask:
	    result = dxdata.native.ddpfPixelFormat.dwRBitMask;
	break;
	case green_mask:
	    result = dxdata.native.ddpfPixelFormat.dwGBitMask;
	break;
	case blue_mask:
	    result = dxdata.native.ddpfPixelFormat.dwBBitMask;
	break;
    }
    return result;
}
DWORD DirectDraw_GetPixelFormat (void)
{
    return DirectDraw_GetSurfacePixelFormat (&dxdata.native);
}
DWORD DirectDraw_GetBytesPerPixel (void)
{
    return (dxdata.native.ddpfPixelFormat.dwRGBBitCount + 7) >> 3;
}

HRESULT DirectDraw_GetDC(HDC *hdc)
{
    HRESULT result;
    result = IDirectDrawSurface7_GetDC (getlocksurface (), hdc);
    return result;
}
HRESULT DirectDraw_ReleaseDC(HDC hdc)
{
    HRESULT result;
    result = IDirectDrawSurface7_ReleaseDC (getlocksurface (), hdc);
    return result;
}
int DirectDraw_GetVerticalBlankStatus (void)
{
    BOOL status;
    if (FAILED(IDirectDraw7_GetVerticalBlankStatus (dxdata.maindd, &status)))
	return -1;
    return status;
}
void DirectDraw_GetPrimaryPixelFormat (DDSURFACEDESC2 *desc)
{
    memcpy (&desc->ddpfPixelFormat, &dxdata.native.ddpfPixelFormat, sizeof (DDPIXELFORMAT));
    desc->dwFlags |= DDSD_PIXELFORMAT;
}
DWORD DirectDraw_CurrentRefreshRate (void)
{
    return dxdata.native.dwRefreshRate;
}

HRESULT DirectDraw_FlipToGDISurface (void)
{
    return IDirectDraw7_FlipToGDISurface (dxdata.maindd);
}

int DirectDraw_BlitToPrimary (RECT *rect)
{
    int result = 0;
    HRESULT ddrval;
    RECT srcrect, dstrect;
    int x = 0, y = 0, w = dxdata.swidth, h = dxdata.sheight;

    if (rect) {
	x = rect->left;
	y = rect->top;
	w = rect->right - rect->left;
	h = rect->bottom - rect->top;
    }
    if (w > dxdata.swidth - x)
	w = dxdata.swidth - x;
    if (h > dxdata.sheight - y)
	h = dxdata.sheight - y;
    SetRect (&srcrect, x, y, x + w, y + h);
    SetRect (&dstrect, x, y, x + w, y + h);
    centerdstrect (&dstrect, &srcrect);
    while (FAILED(ddrval = IDirectDrawSurface7_Blt (dxdata.primary, &dstrect, dxdata.secondary, &srcrect, DDBLT_WAIT, NULL))) {
	if (ddrval == DDERR_SURFACELOST) {
	    ddrval = restoresurface (dxdata.primary);
	    if (FAILED (ddrval))
		return 0;
	} else if (ddrval != DDERR_SURFACEBUSY) {
	    write_log ("DirectDraw_BlitToPrimary: %s\n", DXError (ddrval));
	    break;
	}
    }
    if (SUCCEEDED(ddrval))
	result = 1;
    return result;
}

static void DirectDraw_Blt (LPDIRECTDRAWSURFACE7 dst, RECT *dstrect, LPDIRECTDRAWSURFACE7 src, RECT *srcrect)
{
    HRESULT ddrval;
    if (dst == NULL)
	dst = getlocksurface ();
    while (FAILED(ddrval = IDirectDrawSurface7_Blt (dst, dstrect, src, srcrect, DDBLT_WAIT, NULL))) {
	if (ddrval == DDERR_SURFACELOST) {
	    ddrval = restoresurface (dst);
	    if (FAILED (ddrval))
		break;
	} else if (ddrval != DDERR_SURFACEBUSY) {
	    write_log ("DirectDraw_Blit: %s\n", DXError (ddrval));
	    break;
	}
    }
}

void DirectDraw_Blit (LPDIRECTDRAWSURFACE7 dst, LPDIRECTDRAWSURFACE7 src)
{
    DirectDraw_Blt (dst, NULL, src, NULL);
}

void DirectDraw_BlitRect (LPDIRECTDRAWSURFACE7 dst, RECT *dstrect, LPDIRECTDRAWSURFACE7 src, RECT *scrrect)
{
    DirectDraw_Blt (dst, dstrect, src, scrrect);
}


extern int vblank_skip;
static void flip (void)
{
    int result = 0;
    HRESULT ddrval = DD_OK;
    DWORD flags = DDFLIP_WAIT;
    static int skip;

    if (dxdata.backbuffers == 2) {
	if (currprefs.gfx_avsync) {
	    if (vblank_skip >= 0) {
		skip++;
		if (vblank_skip > skip) {
		    ddrval = IDirectDrawSurface7_Flip (dxdata.primary, NULL, flags | DDFLIP_NOVSYNC);
		} else {
		    skip = 0;
		    ddrval = IDirectDrawSurface7_Flip (dxdata.primary, NULL, flags);
		}
	    } else {
		if (flipinterval_supported) {
		    ddrval = IDirectDrawSurface7_Flip (dxdata.primary, NULL, flags | DDFLIP_INTERVAL2);
		} else {
		    ddrval = IDirectDrawSurface7_Flip (dxdata.primary, NULL, flags);
		    DirectDraw_Blit (dxdata.flipping[1], dxdata.primary);
		    ddrval = IDirectDrawSurface7_Flip (dxdata.primary, NULL, flags);
		}
	    }
	} else {
	    ddrval = IDirectDrawSurface7_Flip (dxdata.primary, NULL, flags);
	}
        DirectDraw_Blit (dxdata.flipping[1], dxdata.primary);
    } else if(dxdata.backbuffers == 1) {
        ddrval = IDirectDrawSurface7_Flip(dxdata.primary, NULL, flags);
        DirectDraw_Blit (dxdata.flipping[0], dxdata.primary);
    }
    if (ddrval == DDERR_SURFACELOST) {
        static int recurse;
	restoresurface (dxdata.primary);
	if (!recurse) {
	    recurse++;
	    flip ();
	    recurse--;
	}
    } else if(FAILED (ddrval)) {
	write_log ("IDirectDrawSurface7_Flip: %s\n", DXError (ddrval));
    }
}

int DirectDraw_Flip (int wait)
{
    if (getlocksurface () != dxdata.secondary) {
	flip ();
    } else {
	DirectDraw_BlitToPrimary (NULL);
    }
    return 1;
}

HRESULT DirectDraw_SetPaletteEntries (int start, int count, PALETTEENTRY *palette)
{
    HRESULT ddrval = DDERR_NOPALETTEATTACHED;
    if (dxdata.palette)
	ddrval = IDirectDrawPalette_SetEntries(dxdata.palette, 0, start, count, palette);
    return ddrval;
}
HRESULT DirectDraw_SetPalette (int remove)
{
    HRESULT ddrval;
    if (!dxdata.fsmodeset || (!dxdata.palette && !remove))
	return DD_FALSE;
    ddrval = IDirectDrawSurface7_SetPalette (dxdata.primary, remove ? NULL : dxdata.palette);
    if (FAILED (ddrval))
	write_log ("IDirectDrawSurface7_SetPalette: %s\n", DXError (ddrval));
    return ddrval;
}
HRESULT DirectDraw_CreatePalette (LPPALETTEENTRY pal)
{
    HRESULT ddrval;
    ddrval = IDirectDraw_CreatePalette (dxdata.maindd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, pal, &dxdata.palette, NULL);
    if (FAILED (ddrval))
	write_log ("IDirectDraw_CreatePalette: %s\n", DXError (ddrval));
    return ddrval;
}

