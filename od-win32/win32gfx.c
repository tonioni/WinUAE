/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 Drawing and DirectX interface
 *
 * Copyright 1997-1998 Mathias Ortmann
 * Copyright 1997-2000 Brian King
 */

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>

#include <windows.h>
#include <commctrl.h>
#include <ddraw.h>
#include <shellapi.h>

#include "sysdeps.h"

#include "resource"

#include "options.h"
#include "audio.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "traps.h"
#include "xwin.h"
#include "keyboard.h"
#include "drawing.h"
#include "dxwrap.h"
#include "picasso96_win.h"
#include "registry.h"
#include "win32.h"
#include "win32gfx.h"
#include "win32gui.h"
#include "sound.h"
#include "inputdevice.h"
#include "opengl.h"
#include "direct3d.h"
#include "midi.h"
#include "gui.h"
#include "serial.h"
#include "avioutput.h"
#include "gfxfilter.h"
#include "parser.h"
#include "lcd.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#define AMIGA_WIDTH_MAX 752
#define AMIGA_HEIGHT_MAX 568

#define DM_DX_FULLSCREEN 1
#define DM_W_FULLSCREEN 2
#define DM_OPENGL 8
#define DM_D3D_FULLSCREEN 16
#define DM_PICASSO96 32
#define DM_DDRAW 64
#define DM_DC 128
#define DM_D3D 256
#define DM_SWSCALE 1024

#define SM_WINDOW 0
#define SM_FULLSCREEN_DX 2
#define SM_OPENGL_WINDOW 3
#define SM_OPENGL_FULLWINDOW 9
#define SM_OPENGL_FULLSCREEN_DX 4
#define SM_D3D_WINDOW 5
#define SM_D3D_FULLWINDOW 10
#define SM_D3D_FULLSCREEN_DX 6
#define SM_FULLWINDOW 7
#define SM_NONE 11

struct uae_filter *usedfilter;
int scalepicasso;

struct winuae_currentmode {
    unsigned int flags;
    int native_width, native_height, native_depth, pitch;
    int current_width, current_height, current_depth;
    int amiga_width, amiga_height;
    int frequency;
    int initdone;
    int fullfill;
    int vsync;
    LPPALETTEENTRY pal;
};

struct MultiDisplay Displays[MAX_DISPLAYS];
static GUID *displayGUID;

static struct winuae_currentmode currentmodestruct;
static int screen_is_initialized;
int display_change_requested, normal_display_change_starting;
int window_led_drives, window_led_drives_end;
int window_led_hd, window_led_hd_end;
extern int console_logging;
int window_extra_width, window_extra_height;

static struct winuae_currentmode *currentmode = &currentmodestruct;

int screen_is_picasso = 0;

int WIN32GFX_IsPicassoScreen (void)
{
    return screen_is_picasso;
}

void WIN32GFX_DisablePicasso (void)
{
    picasso_requested_on = 0;
    picasso_on = 0;
}

void WIN32GFX_EnablePicasso (void)
{
    picasso_requested_on = 1;
}

void WIN32GFX_DisplayChangeRequested (void)
{
    display_change_requested = 1;
}

int isscreen (void)
{
    return hMainWnd ? 1 : 0;
}

static void clearscreen (void)
{
    DirectDraw_FillPrimary ();
}    

static int isfullscreen_2 (struct uae_prefs *p)
{
    if (screen_is_picasso)
	return p->gfx_pfullscreen == 1 ? 1 : (p->gfx_pfullscreen == 2 ? -1 : 0);
    else
	return p->gfx_afullscreen == 1 ? 1 : (p->gfx_afullscreen == 2 ? -1 : 0);
}
int isfullscreen (void)
{
    return isfullscreen_2 (&currprefs);
}

int is3dmode (void)
{
    return currentmode->flags & (DM_D3D | DM_OPENGL);
}

int WIN32GFX_GetDepth (int real)
{
    if (!currentmode->native_depth)
	return currentmode->current_depth;
    return real ? currentmode->native_depth : currentmode->current_depth;
}

int WIN32GFX_GetWidth (void)
{
    return currentmode->current_width;
}

int WIN32GFX_GetHeight (void)
{
    return currentmode->current_height;
}

static BOOL doInit (void);

uae_u32 default_freq = 0;

HWND hStatusWnd = NULL;

static uae_u8 scrlinebuf[4096 * 4]; /* this is too large, but let's rather play on the safe side here */


struct MultiDisplay *getdisplay (struct uae_prefs *p)
{
    int i;
    int display = p->gfx_display;

    i = 0;
    while (Displays[i].name) {
	struct MultiDisplay *md = &Displays[i];
	if (p->gfx_display_name[0] && !_tcscmp (md->name, p->gfx_display_name))
	    return md;
	if (p->gfx_display_name[0] && !_tcscmp (md->name2, p->gfx_display_name))
	    return md;
	i++;
    }
    if (i == 0) {
	gui_message (L"no display adapters! Exiting");
	exit (0);
    }
    if (display >= i)
	display = 0;
    return &Displays[display];
}

void desktop_coords (int *dw, int *dh, int *ax, int *ay, int *aw, int *ah)
{
    struct MultiDisplay *md = getdisplay (&currprefs);

    *dw = md->rect.right - md->rect.left;
    *dh = md->rect.bottom - md->rect.top;
    *ax = amigawin_rect.left;
    *ay = amigawin_rect.top;
    *aw = amigawin_rect.right - *ax;
    *ah = amigawin_rect.bottom - *ay;
}

void centerdstrect (RECT *dr)
{
    if(!(currentmode->flags & (DM_DX_FULLSCREEN | DM_D3D_FULLSCREEN | DM_W_FULLSCREEN)))
	OffsetRect (dr, amigawin_rect.left, amigawin_rect.top);
    if (currentmode->flags & DM_W_FULLSCREEN) {
	if (scalepicasso && screen_is_picasso)
	    return;
	if (usedfilter && !screen_is_picasso)
	    return;
	if (currentmode->fullfill && (currentmode->current_width > currentmode->native_width || currentmode->current_height > currentmode->native_height))
	    return;
	OffsetRect (dr, (currentmode->native_width - currentmode->current_width) / 2,
	    (currentmode->native_height - currentmode->current_height) / 2);
    }
}

static int picasso_offset_x, picasso_offset_y, picasso_offset_mx, picasso_offset_my;

void getgfxoffset (int *dxp, int *dyp, int *mxp, int *myp)
{
    int dx, dy;

    getfilteroffset (&dx, &dy, mxp, myp);
    *dxp = dx;
    *dyp = dy;
    if (picasso_on) {
        dx = picasso_offset_x;
        dy = picasso_offset_y;
	*mxp = picasso_offset_mx;
	*myp = picasso_offset_my;
    }
    *dxp = dx;
    *dyp = dy;
    if (currentmode->flags & DM_W_FULLSCREEN) {
	if (scalepicasso && screen_is_picasso)
	    return;
	if (usedfilter && !screen_is_picasso)
	    return;
	if (currentmode->fullfill && (currentmode->current_width > currentmode->native_width || currentmode->current_height > currentmode->native_height))
	    return;
	dx += (currentmode->native_width - currentmode->current_width) / 2;
	dy += (currentmode->native_height - currentmode->current_height) / 2;
    }
    *dxp = dx;
    *dyp = dy;
}

void DX_Fill (int dstx, int dsty, int width, int height, uae_u32 color)
{
    RECT dstrect;
    if (width < 0)
	width = currentmode->current_width;
    if (height < 0)
	height = currentmode->current_height;
    SetRect (&dstrect, dstx, dsty, dstx + width, dsty + height);
    DirectDraw_Fill (&dstrect, color);
}

static int rgbformat_bits (RGBFTYPE t)
{
    unsigned long f = 1 << t;
    return ((f & RGBMASK_8BIT) != 0 ? 8
	    : (f & RGBMASK_15BIT) != 0 ? 15
	    : (f & RGBMASK_16BIT) != 0 ? 16
	    : (f & RGBMASK_24BIT) != 0 ? 24
	    : (f & RGBMASK_32BIT) != 0 ? 32
	    : 0);
}

static int set_ddraw_2 (void)
{
    HRESULT ddrval;
    int bits = (currentmode->current_depth + 7) & ~7;
    int width = currentmode->native_width;
    int height = currentmode->native_height;
    int freq = currentmode->frequency;
    int dxfullscreen, wfullscreen, dd;

    dxfullscreen = (currentmode->flags & DM_DX_FULLSCREEN) ? TRUE : FALSE;
    wfullscreen = (currentmode->flags & DM_W_FULLSCREEN) ? TRUE : FALSE;
    dd = (currentmode->flags & DM_DDRAW) ? TRUE : FALSE;

    if (WIN32GFX_IsPicassoScreen () && (picasso96_state.Width > width || picasso96_state.Height > height)) {
	width = picasso96_state.Width;
	height = picasso96_state.Height;
    }

    DirectDraw_FreeMainSurface ();

    if (!dd && !dxfullscreen)
	return 1;

    ddrval = DirectDraw_SetCooperativeLevel (hAmigaWnd, dxfullscreen, TRUE);
    if (FAILED (ddrval))
	goto oops;

    if (dxfullscreen)  {
	for (;;) {
	    int i, j, got = FALSE;
	    HRESULT olderr;
	    struct MultiDisplay *md = getdisplay (&currprefs);
	    for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
		struct PicassoResolution *pr = &md->DisplayModes[i];
		if (pr->res.width == width && pr->res.height == height) {
		    for (j = 0; pr->refresh[j] > 0; j++) {
			if (pr->refresh[j] == freq)
			    got = TRUE;
		    }
		    break;
		}
	    }
	    if (got == FALSE)
		freq = 0;
	    write_log (L"set_ddraw: trying %dx%d, bits=%d, refreshrate=%d\n", width, height, bits, freq);
	    ddrval = DirectDraw_SetDisplayMode (width, height, bits, freq);
	    if (SUCCEEDED (ddrval))
		break;
	    olderr = ddrval;
	    if (freq) {
		write_log (L"set_ddraw: failed, trying without forced refresh rate\n");
		DirectDraw_SetCooperativeLevel (hAmigaWnd, dxfullscreen, TRUE);
		ddrval = DirectDraw_SetDisplayMode (width, height, bits, 0);
		if (SUCCEEDED (ddrval))
		    break;
	    }
	    if (olderr != DDERR_INVALIDMODE  && olderr != 0x80004001 && olderr != DDERR_UNSUPPORTEDMODE)
		goto oops;
	    return -1;
	}
	GetWindowRect (hAmigaWnd, &amigawin_rect);
    }

    if (dd) {
	ddrval = DirectDraw_CreateClipper ();
	if (FAILED (ddrval))
	    goto oops;
	ddrval = DirectDraw_CreateMainSurface (width, height);
	if (FAILED(ddrval)) {
	    write_log (L"set_ddraw: couldn't CreateSurface() for primary because %s.\n", DXError (ddrval));
	    goto oops;
	}
	ddrval = DirectDraw_SetClipper (hAmigaWnd);
	if (FAILED (ddrval))
	    goto oops;
	if (DirectDraw_SurfaceLock ()) {
	    currentmode->pitch = DirectDraw_GetSurfacePitch ();
	    DirectDraw_SurfaceUnlock ();
	}
	if (bits <= 8)
	    DirectDraw_CreatePalette (currentmode->pal);
    }

    write_log (L"set_ddraw: %dx%d@%d-bytes\n", width, height, bits);
    return 1;
oops:
    return 0;
}

static void addmode (struct MultiDisplay *md, int w, int h, int d, int rate, int nondx)
{
    int ct;
    int i, j;

    ct = 0;
    if (d == 8)
	ct = RGBMASK_8BIT;
    if (d == 15)
	ct = RGBMASK_15BIT;
    if (d == 16)
	ct = RGBMASK_16BIT;
    if (d == 24)
	ct = RGBMASK_24BIT;
    if (d == 32)
	ct = RGBMASK_32BIT;
    if (ct == 0)
	return;
    d /= 8;
    i = 0;
    while (md->DisplayModes[i].depth >= 0) {
	if (md->DisplayModes[i].depth == d && md->DisplayModes[i].res.width == w && md->DisplayModes[i].res.height == h) {
	    for (j = 0; j < MAX_REFRESH_RATES; j++) {
		if (md->DisplayModes[i].refresh[j] == 0 || md->DisplayModes[i].refresh[j] == rate)
		    break;
	    }
	    if (j < MAX_REFRESH_RATES) {
		md->DisplayModes[i].refresh[j] = rate;
		md->DisplayModes[i].refresh[j + 1] = 0;
		return;
	    }
	}
	i++;
    }
    i = 0;
    while (md->DisplayModes[i].depth >= 0)
	i++;
    if (i >= MAX_PICASSO_MODES - 1)
	return;
    md->DisplayModes[i].res.width = w;
    md->DisplayModes[i].res.height = h;
    md->DisplayModes[i].depth = d;
    md->DisplayModes[i].refresh[0] = rate;
    md->DisplayModes[i].refresh[1] = 0;
    md->DisplayModes[i].colormodes = ct;
    md->DisplayModes[i + 1].depth = -1;
    _stprintf (md->DisplayModes[i].name, L"%dx%d, %d-bit",
	md->DisplayModes[i].res.width, md->DisplayModes[i].res.height, md->DisplayModes[i].depth * 8);
}

static HRESULT CALLBACK modesCallback (LPDDSURFACEDESC2 modeDesc, LPVOID context)
{
    struct MultiDisplay *md = context;
    RGBFTYPE colortype;
    int depth, ct;

    colortype = DirectDraw_GetSurfacePixelFormat (modeDesc);
    ct = 1 << colortype;
    depth = 0;
    if (ct & RGBMASK_8BIT)
	depth = 8;
    else if (ct & RGBMASK_15BIT)
	depth = 15;
    else if (ct & RGBMASK_16BIT)
	depth = 16;
    else if (ct & RGBMASK_24BIT)
	depth = 24;
    else if (ct & RGBMASK_32BIT)
	depth = 32;
    if (depth == 0)
	return DDENUMRET_OK;
    if (colortype == RGBFB_NONE)
	return DDENUMRET_OK;
    if (modeDesc->dwWidth > 2560 || modeDesc->dwHeight > 2048)
	return DDENUMRET_OK;
    addmode (md, modeDesc->dwWidth, modeDesc->dwHeight, depth, modeDesc->dwRefreshRate, 0);
    return DDENUMRET_OK;
}

static int _cdecl resolution_compare (const void *a, const void *b)
{
    struct PicassoResolution *ma = (struct PicassoResolution *)a;
    struct PicassoResolution *mb = (struct PicassoResolution *)b;
    if (ma->res.width < mb->res.width)
	return -1;
    if (ma->res.width > mb->res.width)
	return 1;
    if (ma->res.height < mb->res.height)
	return -1;
    if (ma->res.height > mb->res.height)
	return 1;
    return ma->depth - mb->depth;
}
static void sortmodes (struct MultiDisplay *md)
{
    int	i = 0, idx = -1;
    int pw = -1, ph = -1;
    while (md->DisplayModes[i].depth >= 0)
	i++;
    qsort (md->DisplayModes, i, sizeof (struct PicassoResolution), resolution_compare);
    for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
	if (md->DisplayModes[i].res.height != ph || md->DisplayModes[i].res.width != pw) {
	    ph = md->DisplayModes[i].res.height;
	    pw = md->DisplayModes[i].res.width;
	    idx++;
	}
	md->DisplayModes[i].residx = idx;
    }
}

static void modesList (struct MultiDisplay *md)
{
    int i, j;

    i = 0;
    while (md->DisplayModes[i].depth >= 0) {
	write_log (L"%d: %s (", i, md->DisplayModes[i].name);
	j = 0;
	while (md->DisplayModes[i].refresh[j] > 0) {
	    if (j > 0)
		write_log (L",");
	    write_log (L"%d", md->DisplayModes[i].refresh[j]);
	    j++;
	}
	write_log (L")\n");
	i++;
    }
}

BOOL CALLBACK displaysCallback (GUID *guid, char *adesc, char *aname, LPVOID ctx, HMONITOR hm)
{
    struct MultiDisplay *md = Displays;
    MONITORINFOEX lpmi;
    TCHAR tmp[200];
    TCHAR *desc = au (adesc);
    TCHAR *name = au (aname);
    int ret = 0;

    while (md->name) {
	if (md - Displays >= MAX_DISPLAYS)
	    goto end;
	md++;
    }
    lpmi.cbSize = sizeof (lpmi);
    if (guid == 0) {
	POINT pt = { 0, 0 };
	md->primary = 1;
	GetMonitorInfo (MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), (LPMONITORINFO)&lpmi);
    } else {
	memcpy (&md->guid,  guid, sizeof (GUID));
	GetMonitorInfo (hm, (LPMONITORINFO)&lpmi);
    }
    md->rect = lpmi.rcMonitor;
    if (md->rect.left == 0 && md->rect.top == 0)
        _stprintf (tmp, L"%s (%d*%d)", desc, md->rect.right - md->rect.left, md->rect.bottom - md->rect.top);
    else
        _stprintf (tmp, L"%s (%d*%d) [%d*%d]", desc, md->rect.right - md->rect.left, md->rect.bottom - md->rect.top, md->rect.left, md->rect.top);
    md->name = my_strdup (tmp);
    md->name2 = my_strdup (desc);
    md->name3 = my_strdup (name);
    write_log (L"'%s' '%s' %s\n", desc, name, outGUID(guid));
    ret = 1;
end:
    xfree (name);
    xfree (desc);
    return ret;
}

static BOOL CALLBACK monitorEnumProc (HMONITOR h, HDC hdc, LPRECT rect, LPARAM data)
{
    MONITORINFOEX lpmi;
    int cnt = *((int*)data);
    if (!Displays[cnt].name)
	return FALSE;
    lpmi.cbSize = sizeof (lpmi);
    GetMonitorInfo(h, (LPMONITORINFO)&lpmi);
    Displays[cnt].rect = *rect;
    Displays[cnt].gdi = TRUE;
    (*((int*)data))++;
    return TRUE;
}

void enumeratedisplays (int multi)
{
    if (multi) {
	int cnt = 1;
	DirectDraw_EnumDisplays (displaysCallback);
	EnumDisplayMonitors (NULL, NULL, monitorEnumProc, (LPARAM)&cnt);
    } else {
	write_log (L"Multimonitor detection disabled\n");
	Displays[0].primary = 1;
	Displays[0].name = L"Display";
	Displays[0].disabled = 0;
    }
}

static int makesort (struct MultiDisplay *md)
{
    int v;

    v = md->rect.top * 65536 + md->rect.left;
    if (md->primary)
	v = 0x80000001;
    if (md->rect.top == 0 && md->rect.left == 0)
	v = 0x80000000;
    return v;
}

void sortdisplays (void)
{
    struct MultiDisplay *md1, *md2, tmp;
    int i, idx, idx2;

    md1 = Displays;
    while (md1->name) {
	int sort1 = makesort (md1);
	md2 = md1 + 1;
	while (md2->name) {
	    int sort2 = makesort (md2);
	    if (sort1 > sort2) {
		memcpy (&tmp, md1, sizeof (tmp));
		memcpy (md1, md2, sizeof (tmp));
		memcpy (md2, &tmp, sizeof (tmp));
	    }
	    md2++;
	}
	md1++;
    }

    md1 = Displays;
    while (md1->name) {
	md1->DisplayModes = xmalloc (sizeof (struct PicassoResolution) * MAX_PICASSO_MODES);
	md1->DisplayModes[0].depth = -1;
	md1->disabled = 1;
	if (DirectDraw_Start (md1->primary ? NULL : &md1->guid)) {
	    if (SUCCEEDED (DirectDraw_GetDisplayMode ())) {
		int w = DirectDraw_CurrentWidth ();
		int h = DirectDraw_CurrentHeight ();
		int b = DirectDraw_GetCurrentDepth ();
		write_log (L"Desktop: W=%d H=%d B=%d. CXVS=%d CYVS=%d\n", w, h, b,
		    GetSystemMetrics (SM_CXVIRTUALSCREEN), GetSystemMetrics (SM_CYVIRTUALSCREEN));
		DirectDraw_EnumDisplayModes (DDEDM_REFRESHRATES, modesCallback, md1);
		idx = 0;
		for (;;) {
		    int found;
		    DEVMODE dm;

		    dm.dmSize = sizeof dm;
		    dm.dmDriverExtra = 0;
		    if (!EnumDisplaySettings  (md1->primary ? NULL : md1->name3, idx, &dm))
			break;
		    idx2 = 0;
		    found = 0;
		    while (md1->DisplayModes[idx2].depth >= 0 && !found) {
			struct PicassoResolution *pr = &md1->DisplayModes[idx2];
			if (pr->res.width == dm.dmPelsWidth && pr->res.height == dm.dmPelsHeight && pr->depth == dm.dmBitsPerPel / 8) {
			    for (i = 0; pr->refresh[i]; i++) {
				if (pr->refresh[i] == dm.dmDisplayFrequency) {
				    found = 1;
				    break;
				}
			    }
			}
			idx2++;
		    }
		    if (!found) {
			write_log (L"EnumDisplaySettings(%dx%dx%d %dHz)\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency);
			addmode (md1, dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency, 1);
		    }
		    idx++;
		}
		//dhack();
		sortmodes (md1);
		modesList (md1);
		DirectDraw_Release ();
		if (md1->DisplayModes[0].depth >= 0)
		    md1->disabled = 0;
	    }
	}
	i = 0;
	while (md1->DisplayModes[i].depth > 0)
	    i++;
	write_log (L"'%s', %d display modes (%s)\n", md1->name, i, md1->disabled ? L"disabled" : L"enabled");
	md1++;
    }
    displayGUID = NULL;
}

/* DirectX will fail with "Mode not supported" if we try to switch to a full
 * screen mode that doesn't match one of the dimensions we got during enumeration.
 * So try to find a best match for the given resolution in our list.  */
int WIN32GFX_AdjustScreenmode (struct MultiDisplay *md, uae_u32 *pwidth, uae_u32 *pheight, uae_u32 *ppixbits)
{
    struct PicassoResolution *best;
    uae_u32 selected_mask = (*ppixbits == 8 ? RGBMASK_8BIT
			     : *ppixbits == 15 ? RGBMASK_15BIT
			     : *ppixbits == 16 ? RGBMASK_16BIT
			     : *ppixbits == 24 ? RGBMASK_24BIT
			     : RGBMASK_32BIT);
    int pass, i = 0, index = 0;

    for (pass = 0; pass < 2; pass++) {
	struct PicassoResolution *dm;
	uae_u32 mask = (pass == 0
			? selected_mask
			: RGBMASK_8BIT | RGBMASK_15BIT | RGBMASK_16BIT | RGBMASK_24BIT | RGBMASK_32BIT); /* %%% - BERND, were you missing 15-bit here??? */
	i = 0;
	index = 0;

	best = &md->DisplayModes[0];
	dm = &md->DisplayModes[1];

	while (dm->depth >= 0)  {

	    /* do we already have supported resolution? */
	    if (dm->res.width == *pwidth && dm->res.height == *pheight && dm->depth == (*ppixbits / 8))
		return i;

	    if ((dm->colormodes & mask) != 0)  {
		if (dm->res.width <= best->res.width && dm->res.height <= best->res.height
		    && dm->res.width >= *pwidth && dm->res.height >= *pheight)
		{
		    best = dm;
		    index = i;
		}
		if (dm->res.width >= best->res.width && dm->res.height >= best->res.height
		    && dm->res.width <= *pwidth && dm->res.height <= *pheight)
		{
		    best = dm;
		    index = i;
		}
	    }
	    dm++;
	    i++;
	}
	if (best->res.width == *pwidth && best->res.height == *pheight) {
	    selected_mask = mask; /* %%% - BERND, I added this - does it make sense?  Otherwise, I'd specify a 16-bit display-mode for my
				     Workbench (using -H 2, but SHOULD have been -H 1), and end up with an 8-bit mode instead*/
	    break;
	}
    }
    *pwidth = best->res.width;
    *pheight = best->res.height;
    if (best->colormodes & selected_mask)
	return index;

    /* Ordering here is done such that 16-bit is preferred, followed by 15-bit, 8-bit, 32-bit and 24-bit */
    if (best->colormodes & RGBMASK_16BIT)
	*ppixbits = 16;
    else if (best->colormodes & RGBMASK_15BIT) /* %%% - BERND, this possibility was missing? */
	*ppixbits = 15;
    else if (best->colormodes & RGBMASK_8BIT)
	*ppixbits = 8;
    else if (best->colormodes & RGBMASK_32BIT)
	*ppixbits = 32;
    else if (best->colormodes & RGBMASK_24BIT)
	*ppixbits = 24;
    else
	index = -1;

    return index;
}

void flush_line (int lineno)
{

}

void flush_block (int a, int b)
{

}

void flush_screen (int a, int b)
{
    if (dx_islost ())
	return;
    if (currentmode->flags & DM_OPENGL) {
#ifdef OPENGL
	OGL_render ();
#endif
    } else if (currentmode->flags & DM_D3D) {
	return;
#ifdef GFXFILTER
    } else if (currentmode->flags & DM_SWSCALE) {
	S2X_render ();
        DirectDraw_Flip (1);
#endif
    } else if (currentmode->flags & DM_DDRAW) {
	DirectDraw_Flip (1);
    }
}

static uae_u8 *ddraw_dolock (void)
{
    if (dx_islost ())
	return 0;
    if (!DirectDraw_SurfaceLock ())
	return 0;
    gfxvidinfo.bufmem = DirectDraw_GetSurfacePointer ();
    gfxvidinfo.rowbytes = DirectDraw_GetSurfacePitch ();
    init_row_map ();
    clear_inhibit_frame (IHF_WINDOWHIDDEN);
    return gfxvidinfo.bufmem;
}

int lockscr (void)
{
    int ret = 0;
    if (!isscreen ())
	return ret;
    ret = 1;
    if (currentmode->flags & DM_D3D) {
#ifdef D3D
	if (D3D_needreset ())
	    WIN32GFX_DisplayChangeRequested ();
	ret = D3D_locktexture ();
#endif
    } else if (currentmode->flags & DM_SWSCALE) {
	ret = 1;
    } else if (currentmode->flags & DM_DDRAW) {
	ret = ddraw_dolock () != 0;
    }
    return ret;
}

void unlockscr (void)
{
    if (currentmode->flags & DM_D3D) {
#ifdef D3D
	D3D_unlocktexture ();
#endif
    } else if (currentmode->flags & DM_SWSCALE) {
	return;
    } else if (currentmode->flags & DM_DDRAW) {
	DirectDraw_SurfaceUnlock ();
    }
}

void flush_clear_screen (void)
{
    if (lockscr ()) {
	int y;
	for (y = 0; y < gfxvidinfo.height; y++) {
	    memset (gfxvidinfo.bufmem + y * gfxvidinfo.rowbytes, 0, gfxvidinfo.width * gfxvidinfo.pixbytes);
	}
	unlockscr ();
	flush_screen (0, 0);
    }
}

uae_u8 *gfx_lock_picasso (void)
{
    if (!DirectDraw_SurfaceLock ())
	return 0;
    picasso_vidinfo.rowbytes = DirectDraw_GetSurfacePitch ();
    return DirectDraw_GetSurfacePointer ();
}

/* For the DX_Invalidate() and gfx_unlock_picasso() functions */
static int p96_double_buffer_firstx, p96_double_buffer_lastx;
static int p96_double_buffer_first, p96_double_buffer_last;
static int p96_double_buffer_needs_flushing = 0;

static void DX_Blit96 (int x, int y, int w, int h)
{
    RECT dr, sr;

    picasso_offset_x = 0;
    picasso_offset_y = 0;
    picasso_offset_mx = 1000;
    picasso_offset_my = 1000;
    if (scalepicasso) {
	int srcratio, dstratio;
	SetRect (&sr, 0, 0, picasso96_state.Width, picasso96_state.Height);
	if (currprefs.win32_rtgscaleaspectratio < 0) {
	    // automatic
	    srcratio = picasso96_state.Width * 256 / picasso96_state.Height;
	    dstratio = currentmode->native_width * 256 / currentmode->native_height;
	} else if (currprefs.win32_rtgscaleaspectratio == 0) {
	    // none
	    srcratio = dstratio = 0;
	} else {
	    // manual
	    if (isfullscreen () == 0) {
		dstratio = currentmode->native_width * 256 / currentmode->native_height;
		srcratio = (currprefs.win32_rtgscaleaspectratio >> 8) * 256 / (currprefs.win32_rtgscaleaspectratio & 0xff);
	    } else {
		srcratio = picasso96_state.Width * 256 / picasso96_state.Height;
		dstratio = (currprefs.win32_rtgscaleaspectratio >> 8) * 256 / (currprefs.win32_rtgscaleaspectratio & 0xff);
	    }
	}
	if (srcratio == dstratio) {
	    SetRect (&dr, 0, 0, currentmode->native_width, currentmode->native_height);
	} else if (srcratio > dstratio) {
	    int yy = currentmode->native_height - currentmode->native_height * dstratio / srcratio;
	    SetRect (&dr, 0, yy / 2, currentmode->native_width, currentmode->native_height - yy / 2);
	    picasso_offset_y = yy / 2;
	} else {
	    int xx = currentmode->native_width - currentmode->native_width * srcratio / dstratio;
	    SetRect (&dr, xx / 2, 0, currentmode->native_width - xx / 2, currentmode->native_height);
	    picasso_offset_x = xx / 2;
	}
	picasso_offset_mx = picasso96_state.Width * 1000 / (dr.right - dr.left);
	picasso_offset_my = picasso96_state.Height * 1000 / (dr.bottom - dr.top);
	DirectDraw_BlitToPrimaryScale (&dr, &sr);
    } else {
        SetRect (&sr, x, y, x + w, y + h);
	DirectDraw_BlitToPrimary (&sr);
    }
}

#include "statusline.h"
extern uae_u32 p96rc[256], p96gc[256], p96bc[256];
static void RTGleds (void)
{
    DDSURFACEDESC2 desc;
    RECT sr, dr;
    int dst_width = currentmode->native_width;
    int dst_height = currentmode->native_height;
    int width;

    if (!(currprefs.leds_on_screen & STATUSLINE_RTG))
	return;

    width = dxdata.statuswidth;
    if (dst_width < width)
	width = dst_width;
    SetRect (&sr, 0, 0, width, dxdata.statusheight);
    SetRect (&dr, dst_width - width, dst_height - dxdata.statusheight, dst_width, dst_height);

    picasso_putcursor (dr.left, dr.top,
	dr.right - dr.left, dr.bottom - dr.top);
    DX_Blit96 (dr.left, dr.top,
	dr.right - dr.left, dr.bottom - dr.top);

    DirectDraw_BlitRect (dxdata.statussurface, &sr, dxdata.secondary, &dr);
    if (locksurface (dxdata.statussurface, &desc)) {
        int sy, yy;
        yy = 0;
        for (sy = dst_height - dxdata.statusheight; sy < dst_height; sy++) {
	    uae_u8 *buf = (uae_u8*)desc.lpSurface + yy * desc.lPitch;
	    draw_status_line_single (buf, currentmode->current_depth / 8, yy, dst_width, p96rc, p96gc, p96bc);
	    yy++;
	}
	unlocksurface (dxdata.statussurface);
    }
    DirectDraw_BlitRect (dxdata.secondary, &dr, dxdata.statussurface, &sr);
    
    picasso_clearcursor ();
    
    DirectDraw_BlitToPrimary (&dr);
}

void gfx_unlock_picasso (void)
{
    DirectDraw_SurfaceUnlock ();
    if (p96_double_buffer_needs_flushing) {
	if (scalepicasso) {
	   p96_double_buffer_firstx = 0;
	   p96_double_buffer_lastx = picasso96_state.Width;
	   p96_double_buffer_first = 0;
	   p96_double_buffer_last = picasso96_state.Height;
	}
	picasso_putcursor (p96_double_buffer_firstx, p96_double_buffer_first,
	    p96_double_buffer_lastx - p96_double_buffer_firstx + 1, p96_double_buffer_last - p96_double_buffer_first + 1);
	DX_Blit96 (p96_double_buffer_firstx, p96_double_buffer_first,
	     p96_double_buffer_lastx - p96_double_buffer_firstx + 1,
	     p96_double_buffer_last - p96_double_buffer_first + 1);
	picasso_clearcursor ();
	p96_double_buffer_needs_flushing = 0;
    }
    if (currprefs.leds_on_screen & STATUSLINE_RTG)
	RTGleds ();
}

static void close_hwnds (void)
{
    screen_is_initialized = 0;
#ifdef AVIOUTPUT
    AVIOutput_Restart ();
#endif
    setmouseactive (0);
#ifdef RETROPLATFORM
    rp_set_hwnd (NULL);
#endif
    if (hStatusWnd) {
	ShowWindow (hStatusWnd, SW_HIDE);
	DestroyWindow (hStatusWnd);
	hStatusWnd = 0;
    }
    if (hAmigaWnd) {
	addnotifications (hAmigaWnd, TRUE);
#ifdef OPENGL
	OGL_free ();
#endif
#ifdef D3D
	D3D_free ();
#endif
	ShowWindow (hAmigaWnd, SW_HIDE);
	DestroyWindow (hAmigaWnd);
	if (hAmigaWnd == hMainWnd)
	    hMainWnd = 0;
	hAmigaWnd = 0;
    }
    if (hMainWnd) {
	ShowWindow (hMainWnd, SW_HIDE);
	DestroyWindow (hMainWnd);
	hMainWnd = 0;
    }
}


static void updatemodes (void)
{
    DWORD flags;

    currentmode->fullfill = 0;
    flags = DM_DDRAW;
    if (isfullscreen () > 0)
	flags |= DM_DX_FULLSCREEN;
    else if (isfullscreen () < 0)
	flags |= DM_W_FULLSCREEN;
#if defined (GFXFILTER)
    if (usedfilter) {
	if (!usedfilter->x[0]) {
	    flags |= DM_SWSCALE;
	    if (currentmode->current_depth < 15)
		currentmode->current_depth = 16;
	}
	if (!screen_is_picasso) {
	    if (usedfilter->type == UAE_FILTER_DIRECT3D) {
		flags |= DM_D3D;
		if (flags & DM_DX_FULLSCREEN) {
		    flags &= ~DM_DX_FULLSCREEN;
		    flags |= DM_D3D_FULLSCREEN;
		}
		flags &= ~DM_DDRAW;
	    }
	    if (usedfilter->type == UAE_FILTER_OPENGL) {
		flags |= DM_OPENGL;
		flags &= ~DM_DDRAW;
	    }
	} 
    }
#endif
    currentmode->flags = flags;
    if (flags & DM_SWSCALE)
	currentmode->fullfill = 1;
    if (useoverlay && currentmode->current_depth > 16)
	currentmode->current_depth = 16;
    if (flags & DM_W_FULLSCREEN) {
        RECT rc = getdisplay (&currprefs)->rect;
        currentmode->native_width = rc.right - rc.left;
        currentmode->native_height = rc.bottom - rc.top;
    } else {
	currentmode->native_width = currentmode->current_width;
	currentmode->native_height = currentmode->current_height;
    }
}

static void update_gfxparams (void)
{
    updatewinfsmode (&currprefs);
#ifdef PICASSO96
    currentmode->vsync = 0;
    if (screen_is_picasso) {
	currentmode->current_width = picasso96_state.Width;
	currentmode->current_height = picasso96_state.Height;
	currentmode->frequency = abs (currprefs.gfx_refreshrate > default_freq ? currprefs.gfx_refreshrate : default_freq);
	if (currprefs.gfx_pvsync)
	    currentmode->vsync = 1;
    } else {
#endif
	currentmode->current_width = currprefs.gfx_size.width;
	currentmode->current_height = currprefs.gfx_size.height;
	currentmode->frequency = abs (currprefs.gfx_refreshrate);
	if (currprefs.gfx_avsync)
	    currentmode->vsync = 1;
#ifdef PICASSO96
    }
#endif
    currentmode->current_depth = (currprefs.color_mode == 0 ? 8
    	: currprefs.color_mode == 1 ? 15
	: currprefs.color_mode == 2 ? 16
	: currprefs.color_mode == 3 ? 8
	: currprefs.color_mode == 4 ? 8 : 32);
    if (screen_is_picasso && currprefs.win32_rtgmatchdepth && isfullscreen () > 0) {
	int pbits = picasso96_state.BytesPerPixel * 8;
	if (pbits == 24)
	    pbits = 32;
	if (pbits >= 8)
	    currentmode->current_depth = pbits;
    }
    if (useoverlay && currentmode->current_depth > 16)
	currentmode->current_depth = 16;
    currentmode->amiga_width = currentmode->current_width;
    currentmode->amiga_height = currentmode->current_height;

    scalepicasso = 0;
    if (screen_is_picasso) {
	if (isfullscreen () < 0) {
	    if ((currprefs.win32_rtgscaleifsmall || currprefs.win32_rtgallowscaling) && (picasso96_state.Width != currentmode->native_width || picasso96_state.Height != currentmode->native_height))
	        scalepicasso = -1;
	} else if (isfullscreen () > 0) {
	    //if (currprefs.gfx_size.width > picasso96_state.Width && currprefs.gfx_size.height > picasso96_state.Height) {
	    if (currentmode->native_width > picasso96_state.Width && currentmode->native_height > picasso96_state.Height) {
		if (currprefs.win32_rtgscaleifsmall && !currprefs.win32_rtgmatchdepth) // can't scale to different color depth
		    scalepicasso = 1;
	    }
	} else if (isfullscreen () == 0) {
	    if ((currprefs.gfx_size.width != picasso96_state.Width || currprefs.gfx_size.height != picasso96_state.Height) && currprefs.win32_rtgallowscaling)
		scalepicasso = 1;
	    if ((currprefs.gfx_size.width > picasso96_state.Width || currprefs.gfx_size.height > picasso96_state.Height) && currprefs.win32_rtgscaleifsmall)
		scalepicasso = 1;
	}

	if (scalepicasso > 0 && (currprefs.gfx_size.width != picasso96_state.Width || currprefs.gfx_size.height != picasso96_state.Height)) {
	    currentmode->current_width = currprefs.gfx_size.width;
	    currentmode->current_height = currprefs.gfx_size.height;
	}
    }

}

static int open_windows (int full)
{
    int ret, i;

    inputdevice_unacquire ();
    reset_sound();
    in_sizemove = 0;

    updatewinfsmode (&currprefs);
    D3D_free ();
    OGL_free ();
    if (!DirectDraw_Start (displayGUID))
	return 0;
    write_log (L"DirectDraw GUID=%s\n", outGUID (displayGUID));

    ret = -2;
    do {
	if (ret < -1) {
	    updatemodes ();
	    update_gfxparams ();
	}
	ret = doInit ();
    } while (ret < 0);

    if (!ret) {
	DirectDraw_Release ();
	return ret;
    }

    setpriority (&priorities[currprefs.win32_active_priority]);
    if (!rp_isactive () && full)
	setmouseactive (-1);
    for (i = 0; i < NUM_LEDS; i++)
	gui_led (i, 0);
    gui_fps (0, 0);
    inputdevice_acquire (TRUE);

    return ret;
}

int check_prefs_changed_gfx (void)
{
    int c = 0;

    c |= currprefs.gfx_size_fs.width != changed_prefs.gfx_size_fs.width ? 16 : 0;
    c |= currprefs.gfx_size_fs.height != changed_prefs.gfx_size_fs.height ? 16 : 0;
    c |= ((currprefs.gfx_size_win.width + 7) & ~7) != ((changed_prefs.gfx_size_win.width + 7) & ~7) ? 16 : 0;
    c |= currprefs.gfx_size_win.height != changed_prefs.gfx_size_win.height ? 16 : 0;
#if 0
    c |= currprefs.gfx_size_win.x != changed_prefs.gfx_size_win.x ? 16 : 0;
    c |= currprefs.gfx_size_win.y != changed_prefs.gfx_size_win.y ? 16 : 0;
#endif
    c |= currprefs.color_mode != changed_prefs.color_mode ? 2 | 16 : 0;
    c |= currprefs.gfx_afullscreen != changed_prefs.gfx_afullscreen ? 16 : 0;
    c |= currprefs.gfx_pfullscreen != changed_prefs.gfx_pfullscreen ? 16 : 0;
    c |= currprefs.gfx_avsync != changed_prefs.gfx_avsync ? 2 | 16 : 0;
    c |= currprefs.gfx_pvsync != changed_prefs.gfx_pvsync ? 2 | 16 : 0;
    c |= currprefs.gfx_refreshrate != changed_prefs.gfx_refreshrate ? 2 | 16 : 0;
    c |= currprefs.gfx_autoresolution != changed_prefs.gfx_autoresolution ? (2|8) : 0;

    c |= currprefs.gfx_filter != changed_prefs.gfx_filter ? (2|8) : 0;
    c |= _tcscmp (currprefs.gfx_filtershader, changed_prefs.gfx_filtershader) ? (2|8|32) : 0;
    c |= currprefs.gfx_filter_filtermode != changed_prefs.gfx_filter_filtermode ? (2|8|32) : 0;
    c |= currprefs.gfx_filter_horiz_zoom_mult != changed_prefs.gfx_filter_horiz_zoom_mult ? (1|8) : 0;
    c |= currprefs.gfx_filter_vert_zoom_mult != changed_prefs.gfx_filter_vert_zoom_mult ? (1|8) : 0;
    c |= currprefs.gfx_filter_noise != changed_prefs.gfx_filter_noise ? (1|8) : 0;
    c |= currprefs.gfx_filter_blur != changed_prefs.gfx_filter_blur ? (1|8) : 0;
    c |= currprefs.gfx_filter_scanlines != changed_prefs.gfx_filter_scanlines ? (1|8) : 0;
    c |= currprefs.gfx_filter_scanlinelevel != changed_prefs.gfx_filter_scanlinelevel ? (1|8) : 0;
    c |= currprefs.gfx_filter_scanlineratio != changed_prefs.gfx_filter_scanlineratio ? (1|8) : 0;
    c |= currprefs.gfx_filter_aspect != changed_prefs.gfx_filter_aspect ? (1|8) : 0;
    c |= currprefs.gfx_filter_luminance != changed_prefs.gfx_filter_luminance ? (1|8) : 0;
    c |= currprefs.gfx_filter_contrast != changed_prefs.gfx_filter_contrast ? (1|8) : 0;
    c |= currprefs.gfx_filter_saturation != changed_prefs.gfx_filter_saturation ? (1|8) : 0;
    c |= currprefs.gfx_filter_gamma != changed_prefs.gfx_filter_gamma ? (1|8) : 0;
    //c |= currprefs.gfx_filter_ != changed_prefs.gfx_filter_ ? (1|8) : 0;
    
    c |= currprefs.gfx_resolution != changed_prefs.gfx_resolution ? (2 | 8) : 0;
    c |= currprefs.gfx_linedbl != changed_prefs.gfx_linedbl ? (2 | 8) : 0;
    c |= currprefs.gfx_lores_mode != changed_prefs.gfx_lores_mode ? (2 | 8) : 0;
    c |= currprefs.gfx_scandoubler != changed_prefs.gfx_scandoubler ? (2 | 8) : 0;
    c |= currprefs.gfx_display != changed_prefs.gfx_display ? (2|4|8) : 0;
    c |= _tcscmp (currprefs.gfx_display_name, changed_prefs.gfx_display_name) ? (2|4|8) : 0;
    c |= currprefs.gfx_blackerthanblack != changed_prefs.gfx_blackerthanblack ? (2 | 8) : 0;

    c |= currprefs.win32_alwaysontop != changed_prefs.win32_alwaysontop ? 32 : 0;
    c |= currprefs.win32_notaskbarbutton != changed_prefs.win32_notaskbarbutton ? 32 : 0;
    c |= currprefs.win32_borderless != changed_prefs.win32_borderless ? 32 : 0;
    c |= currprefs.win32_rtgmatchdepth != changed_prefs.win32_rtgmatchdepth ? 2 : 0;
    c |= currprefs.win32_rtgscaleifsmall != changed_prefs.win32_rtgscaleifsmall ? (2 | 8 | 64) : 0;
    c |= currprefs.win32_rtgallowscaling != changed_prefs.win32_rtgallowscaling ? (2 | 8 | 64) : 0;
    c |= currprefs.win32_rtgscaleaspectratio != changed_prefs.win32_rtgscaleaspectratio ? (8 | 64) : 0;
    c |= currprefs.win32_rtgvblankrate != changed_prefs.win32_rtgvblankrate ? 8 : 0;

    if (display_change_requested || c)
    {
	int keepfsmode = 
	    currprefs.gfx_afullscreen == changed_prefs.gfx_afullscreen && 
	    currprefs.gfx_pfullscreen == changed_prefs.gfx_pfullscreen;
	cfgfile_configuration_change (1);

	currprefs.gfx_autoresolution = changed_prefs.gfx_autoresolution;
	currprefs.color_mode = changed_prefs.color_mode;

        if (changed_prefs.gfx_afullscreen == 1) { 
	    if (currprefs.gfx_filter == UAE_FILTER_DIRECT3D && changed_prefs.gfx_filter != UAE_FILTER_DIRECT3D)
		display_change_requested = 1;
	    if (currprefs.gfx_filter == UAE_FILTER_OPENGL && changed_prefs.gfx_filter != UAE_FILTER_OPENGL)
		display_change_requested = 1;
	    if (changed_prefs.gfx_filter == UAE_FILTER_DIRECT3D && currprefs.gfx_filter != UAE_FILTER_DIRECT3D)
		display_change_requested = 1;
	    if (changed_prefs.gfx_filter == UAE_FILTER_OPENGL && currprefs.gfx_filter != UAE_FILTER_OPENGL)
		display_change_requested = 1;
	}

	if (display_change_requested) {
	    c = 2;
	    keepfsmode = 0;
	    display_change_requested = 0;
	}

	currprefs.gfx_filter = changed_prefs.gfx_filter;
	_tcscpy (currprefs.gfx_filtershader, changed_prefs.gfx_filtershader);
	currprefs.gfx_filter_filtermode = changed_prefs.gfx_filter_filtermode;
	currprefs.gfx_filter_horiz_zoom_mult = changed_prefs.gfx_filter_horiz_zoom_mult;
	currprefs.gfx_filter_vert_zoom_mult = changed_prefs.gfx_filter_vert_zoom_mult;
	currprefs.gfx_filter_noise = changed_prefs.gfx_filter_noise;
	currprefs.gfx_filter_blur = changed_prefs.gfx_filter_blur;
	currprefs.gfx_filter_scanlines = changed_prefs.gfx_filter_scanlines;
	currprefs.gfx_filter_scanlinelevel = changed_prefs.gfx_filter_scanlinelevel;
	currprefs.gfx_filter_scanlineratio = changed_prefs.gfx_filter_scanlineratio;
	currprefs.gfx_filter_aspect = changed_prefs.gfx_filter_aspect;
	currprefs.gfx_filter_luminance = changed_prefs.gfx_filter_luminance;
	currprefs.gfx_filter_contrast = changed_prefs.gfx_filter_contrast;
	currprefs.gfx_filter_saturation = changed_prefs.gfx_filter_saturation;
	currprefs.gfx_filter_gamma = changed_prefs.gfx_filter_gamma;
	currprefs.gfx_filter_autoscale = changed_prefs.gfx_filter_autoscale;
	//currprefs.gfx_filter_ = changed_prefs.gfx_filter_;

	currprefs.gfx_lores_mode = changed_prefs.gfx_lores_mode;
	currprefs.gfx_scandoubler = changed_prefs.gfx_scandoubler;
	currprefs.gfx_resolution = changed_prefs.gfx_resolution;
	currprefs.gfx_linedbl = changed_prefs.gfx_linedbl;
	currprefs.gfx_display = changed_prefs.gfx_display;
	_tcscpy (currprefs.gfx_display_name, changed_prefs.gfx_display_name);
	currprefs.gfx_blackerthanblack = changed_prefs.gfx_blackerthanblack;

	currprefs.win32_alwaysontop = changed_prefs.win32_alwaysontop;
	currprefs.win32_notaskbarbutton = changed_prefs.win32_notaskbarbutton;
	currprefs.win32_borderless = changed_prefs.win32_borderless;
	currprefs.win32_rtgmatchdepth = changed_prefs.win32_rtgmatchdepth;
	currprefs.win32_rtgscaleifsmall = changed_prefs.win32_rtgscaleifsmall;
	currprefs.win32_rtgallowscaling = changed_prefs.win32_rtgallowscaling;
	currprefs.win32_rtgscaleaspectratio = changed_prefs.win32_rtgscaleaspectratio;
	currprefs.win32_rtgvblankrate = changed_prefs.win32_rtgvblankrate;

	inputdevice_unacquire ();
	if (c & 64) {
	    DirectDraw_Fill (NULL, 0);
	    DirectDraw_BlitToPrimary (NULL);
	}
	if ((c & 16) || ((c & 8) && keepfsmode)) {
	    extern int reopen (int);
	    if (reopen (c & 2))
		c |= 2;
	}
	if ((c & 32) || ((c & 2) && !keepfsmode)) {
	    close_windows ();
	    graphics_init ();
	}
	init_custom ();
	if (c & 4) {
	    pause_sound ();
	    resume_sound ();
	}
	inputdevice_acquire (TRUE);
	return 1;
    }

    if (currprefs.chipset_refreshrate != changed_prefs.chipset_refreshrate) {
	currprefs.chipset_refreshrate = changed_prefs.chipset_refreshrate;
	init_hz ();
	return 1;
    }

    if (currprefs.gfx_filter_autoscale != changed_prefs.gfx_filter_autoscale ||
	currprefs.gfx_xcenter_pos != changed_prefs.gfx_xcenter_pos ||
	currprefs.gfx_ycenter_pos != changed_prefs.gfx_ycenter_pos ||
	currprefs.gfx_xcenter_size != changed_prefs.gfx_xcenter_size ||
	currprefs.gfx_ycenter_size != changed_prefs.gfx_ycenter_size ||
	currprefs.gfx_xcenter != changed_prefs.gfx_xcenter ||
	currprefs.gfx_ycenter != changed_prefs.gfx_ycenter)
    {
	currprefs.gfx_xcenter_pos = changed_prefs.gfx_xcenter_pos;
	currprefs.gfx_ycenter_pos = changed_prefs.gfx_ycenter_pos;
	currprefs.gfx_xcenter_size = changed_prefs.gfx_xcenter_size;
	currprefs.gfx_ycenter_size = changed_prefs.gfx_ycenter_size;
	currprefs.gfx_xcenter = changed_prefs.gfx_xcenter;
	currprefs.gfx_ycenter = changed_prefs.gfx_ycenter;
	currprefs.gfx_filter_autoscale = changed_prefs.gfx_filter_autoscale;

	get_custom_limits (NULL, NULL, NULL, NULL);
	fixup_prefs_dimensions (&changed_prefs);

	return 1;
    }

    if (currprefs.win32_norecyclebin != changed_prefs.win32_norecyclebin) {
	currprefs.win32_norecyclebin = changed_prefs.win32_norecyclebin;
    }

    if (currprefs.win32_logfile != changed_prefs.win32_logfile) {
	currprefs.win32_logfile = changed_prefs.win32_logfile;
	if (currprefs.win32_logfile)
	    logging_open (0, 1);
	else
	    logging_cleanup ();
    }

    if (currprefs.leds_on_screen != changed_prefs.leds_on_screen ||
	currprefs.keyboard_leds[0] != changed_prefs.keyboard_leds[0] ||
	currprefs.keyboard_leds[1] != changed_prefs.keyboard_leds[1] ||
	currprefs.keyboard_leds[2] != changed_prefs.keyboard_leds[2] ||
	currprefs.win32_middle_mouse != changed_prefs.win32_middle_mouse ||
	currprefs.win32_active_priority != changed_prefs.win32_active_priority ||
	currprefs.win32_inactive_priority != changed_prefs.win32_inactive_priority ||
	currprefs.win32_iconified_priority != changed_prefs.win32_iconified_priority ||
	currprefs.win32_inactive_nosound != changed_prefs.win32_inactive_nosound ||
	currprefs.win32_inactive_pause != changed_prefs.win32_inactive_pause ||
	currprefs.win32_iconified_nosound != changed_prefs.win32_iconified_nosound ||
	currprefs.win32_iconified_pause != changed_prefs.win32_iconified_pause ||
	currprefs.win32_ctrl_F11_is_quit != changed_prefs.win32_ctrl_F11_is_quit)
    {
	currprefs.leds_on_screen = changed_prefs.leds_on_screen;
	currprefs.keyboard_leds[0] = changed_prefs.keyboard_leds[0];
	currprefs.keyboard_leds[1] = changed_prefs.keyboard_leds[1];
	currprefs.keyboard_leds[2] = changed_prefs.keyboard_leds[2];
	currprefs.win32_middle_mouse = changed_prefs.win32_middle_mouse;
	currprefs.win32_active_priority = changed_prefs.win32_active_priority;
	currprefs.win32_inactive_priority = changed_prefs.win32_inactive_priority;
	currprefs.win32_iconified_priority = changed_prefs.win32_iconified_priority;
	currprefs.win32_inactive_nosound = changed_prefs.win32_inactive_nosound;
	currprefs.win32_inactive_pause = changed_prefs.win32_inactive_pause;
	currprefs.win32_iconified_nosound = changed_prefs.win32_iconified_nosound;
	currprefs.win32_iconified_pause = changed_prefs.win32_iconified_pause;
	currprefs.win32_ctrl_F11_is_quit = changed_prefs.win32_ctrl_F11_is_quit;
	inputdevice_unacquire ();
	currprefs.keyboard_leds_in_use = currprefs.keyboard_leds[0] | currprefs.keyboard_leds[1] | currprefs.keyboard_leds[2];
	pause_sound ();
	resume_sound ();
	inputdevice_acquire (TRUE);
#ifndef	_DEBUG
	setpriority (&priorities[currprefs.win32_active_priority]);
#endif
	return 1;
    }

    if (_tcscmp (currprefs.prtname, changed_prefs.prtname) ||
	currprefs.parallel_autoflush_time != changed_prefs.parallel_autoflush_time ||
	currprefs.parallel_ascii_emulation != changed_prefs.parallel_ascii_emulation ||
	currprefs.parallel_postscript_emulation != changed_prefs.parallel_postscript_emulation ||
	currprefs.parallel_postscript_detection != changed_prefs.parallel_postscript_detection ||
	_tcscmp (currprefs.ghostscript_parameters, changed_prefs.ghostscript_parameters)) {
	_tcscpy (currprefs.prtname, changed_prefs.prtname);
	currprefs.parallel_autoflush_time = changed_prefs.parallel_autoflush_time;
	currprefs.parallel_ascii_emulation = changed_prefs.parallel_ascii_emulation;
	currprefs.parallel_postscript_emulation = changed_prefs.parallel_postscript_emulation;
	currprefs.parallel_postscript_detection = changed_prefs.parallel_postscript_detection;
	_tcscpy (currprefs.ghostscript_parameters, changed_prefs.ghostscript_parameters);
#ifdef PARALLEL_PORT
	closeprinter ();
#endif
    }
    if (_tcscmp (currprefs.sername, changed_prefs.sername) ||
	currprefs.serial_hwctsrts != changed_prefs.serial_hwctsrts ||
	currprefs.serial_direct != changed_prefs.serial_direct ||
	currprefs.serial_demand != changed_prefs.serial_demand) {
	_tcscpy (currprefs.sername, changed_prefs.sername);
	currprefs.serial_hwctsrts = changed_prefs.serial_hwctsrts;
	currprefs.serial_demand = changed_prefs.serial_demand;
	currprefs.serial_direct = changed_prefs.serial_direct;
#ifdef SERIAL_PORT
	serial_exit ();
	serial_init ();
#endif
    }
    if (currprefs.win32_midiindev != changed_prefs.win32_midiindev ||
	currprefs.win32_midioutdev != changed_prefs.win32_midioutdev)
    {
	currprefs.win32_midiindev = changed_prefs.win32_midiindev;
	currprefs.win32_midioutdev = changed_prefs.win32_midioutdev;
#ifdef SERIAL_PORT
	if (midi_ready) {
	    Midi_Close ();
	    Midi_Open ();
	}
#endif
    }

    if (currprefs.win32_powersavedisabled != changed_prefs.win32_powersavedisabled) {

	currprefs.win32_powersavedisabled = changed_prefs.win32_powersavedisabled;
    }
    return 0;
}

/* Color management */

static xcolnr xcol8[4096];
static PALETTEENTRY colors256[256];
static int ncols256 = 0;

static int red_bits, green_bits, blue_bits, alpha_bits;
static int red_shift, green_shift, blue_shift, alpha_shift;
static int x_red_bits, x_green_bits, x_blue_bits, x_alpha_bits;
static int x_red_shift, x_green_shift, x_blue_shift, x_alpha_shift;
static int alpha;

static int get_color (int r, int g, int b, xcolnr * cnp)
{
    if (ncols256 == 256)
	return 0;
    colors256[ncols256].peRed = r * 0x11;
    colors256[ncols256].peGreen = g * 0x11;
    colors256[ncols256].peBlue = b * 0x11;
    colors256[ncols256].peFlags = 0;
    *cnp = ncols256;
    ncols256++;
    return 1;
}

void init_colors (void)
{
    if (ncols256 == 0) {
	alloc_colors256 (get_color);
	memcpy (xcol8, xcolors, sizeof xcol8);
    }

    /* init colors */
    switch(currentmode->current_depth >> 3)
    {
        case 1:
	    memcpy (xcolors, xcol8, sizeof xcolors);
	    DirectDraw_SetPaletteEntries (0, 256, colors256);
	break;
	case 2:
	case 3:
	case 4:
	    x_red_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (red_mask));
	    x_green_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (green_mask));
	    x_blue_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (blue_mask));
	    x_red_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (red_mask));
	    x_green_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (green_mask));
	    x_blue_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (blue_mask));
	    x_alpha_bits = 0;
	    x_alpha_shift = 0;
	break;
    }

    if (currentmode->flags & DM_OPENGL) {
#ifdef OPENGL
	OGL_getpixelformat (currentmode->current_depth,&red_bits,&green_bits,&blue_bits,&red_shift,&green_shift,&blue_shift,&alpha_bits,&alpha_shift,&alpha);
#endif
    } else if (currentmode->flags & DM_D3D) {
#ifdef D3D
	D3D_getpixelformat (currentmode->current_depth,&red_bits,&green_bits,&blue_bits,&red_shift,&green_shift,&blue_shift,&alpha_bits,&alpha_shift,&alpha);
#endif
    } else {
	red_bits = x_red_bits;
	green_bits = x_green_bits;
	blue_bits = x_blue_bits;
	red_shift = x_red_shift;
	green_shift = x_green_shift;
	blue_shift = x_blue_shift;
	alpha_bits = x_alpha_bits;
	alpha_shift = x_alpha_shift;
    }

    if (currentmode->current_depth > 8) {
	if (!(currentmode->flags & (DM_OPENGL|DM_D3D))) {
	    if (currentmode->current_depth != currentmode->native_depth) {
		if (currentmode->current_depth == 16) {
		    red_bits = 5; green_bits = 6; blue_bits = 5;
		    red_shift = 11; green_shift = 5; blue_shift = 0;
		} else {
		    red_bits = green_bits = blue_bits = 8;
		    red_shift = 16; green_shift = 8; blue_shift = 0;
		}
	    }
	}
	alloc_colors64k (red_bits, green_bits, blue_bits, red_shift,green_shift, blue_shift, alpha_bits, alpha_shift, alpha, 0);
	notice_new_xcolors ();
#ifdef GFXFILTER
	S2X_configure (red_bits, green_bits, blue_bits, red_shift,green_shift, blue_shift);
#endif
#ifdef AVIOUTPUT
	AVIOutput_RGBinfo (red_bits, green_bits, blue_bits, red_shift, green_shift, blue_shift);
#endif
    }
}

#ifdef PICASSO96
void DX_SetPalette_vsync (void)
{
}

int picasso_palette (void)
{
    int i, changed;

    changed = 0;
    for (i = 0; i < 256; i++) {
        int r = picasso96_state.CLUT[i].Red;
        int g = picasso96_state.CLUT[i].Green;
        int b = picasso96_state.CLUT[i].Blue;
	uae_u32 v = (doMask256 (r, red_bits, red_shift)
	    | doMask256 (g, green_bits, green_shift)
	    | doMask256 (b, blue_bits, blue_shift));
	if (v !=  picasso_vidinfo.clut[i]) {
	     picasso_vidinfo.clut[i] = v;
	     changed = 1;
	}
    }
    if (changed)
	DX_SetPalette (0,256);
    return changed;
}

void DX_SetPalette (int start, int count)
{
    if (!screen_is_picasso)
	return;
    if (picasso_vidinfo.pixbytes != 1)
	return;
    if(currentmode->current_depth > 8)
	return;
    if (SUCCEEDED (DirectDraw_SetPalette (0)))
        DirectDraw_SetPaletteEntries (start, count, (LPPALETTEENTRY)&(picasso96_state.CLUT[start]));
}

void DX_Invalidate (int x, int y, int width, int height)
{
    int last, lastx;

    if (width == 0 || height == 0)
	return;
    if (y < 0 || height < 0) {
	y = 0;
	height = picasso_vidinfo.height;
    }
    if (x < 0 || width < 0) {
	x = 0;
	width = picasso_vidinfo.width;
    }
    last = y + height - 1;
    lastx = x + width - 1;
    p96_double_buffer_first = y;
    p96_double_buffer_last  = last;
    p96_double_buffer_firstx = x;
    p96_double_buffer_lastx = lastx;
    p96_double_buffer_needs_flushing = 1;
}

#endif

static void open_screen (void)
{
    close_windows ();
    open_windows (1);
#ifdef PICASSO96
    DX_SetPalette (0, 256);
#endif
}

static int ifs (struct uae_prefs *p)
{
    if (screen_is_picasso)
	return p->gfx_pfullscreen == 1 ? 1 : (p->gfx_pfullscreen == 2 ? -1 : 0);
    else
	return p->gfx_afullscreen == 1 ? 1 : (p->gfx_afullscreen == 2 ? -1 : 0);
}

static int reopen (int full)
{
    int quick = 0;

    updatewinfsmode (&changed_prefs);

    if (changed_prefs.gfx_afullscreen != currprefs.gfx_afullscreen && !screen_is_picasso)
	full = 1;
    if (changed_prefs.gfx_pfullscreen != currprefs.gfx_pfullscreen && screen_is_picasso)
	full = 1;

    /* fullscreen to fullscreen? */
    if (isfullscreen () > 0 && currprefs.gfx_afullscreen == changed_prefs.gfx_afullscreen &&
	currprefs.gfx_pfullscreen == changed_prefs.gfx_pfullscreen && currprefs.gfx_afullscreen == 1) {
	    quick = 1;
    }
    /* windowed to windowed */
    if (isfullscreen () <= 0 && currprefs.gfx_afullscreen == changed_prefs.gfx_afullscreen &&
	currprefs.gfx_pfullscreen == changed_prefs.gfx_pfullscreen) {
	quick = 1;
    }

    currprefs.gfx_size_fs.width = changed_prefs.gfx_size_fs.width;
    currprefs.gfx_size_fs.height = changed_prefs.gfx_size_fs.height;
    currprefs.gfx_size_win.width = changed_prefs.gfx_size_win.width;
    currprefs.gfx_size_win.height = changed_prefs.gfx_size_win.height;
    currprefs.gfx_size_win.x = changed_prefs.gfx_size_win.x;
    currprefs.gfx_size_win.y = changed_prefs.gfx_size_win.y;
    currprefs.gfx_afullscreen = changed_prefs.gfx_afullscreen;
    currprefs.gfx_pfullscreen = changed_prefs.gfx_pfullscreen;
    currprefs.gfx_avsync = changed_prefs.gfx_avsync;
    currprefs.gfx_pvsync = changed_prefs.gfx_pvsync;
    currprefs.gfx_refreshrate = changed_prefs.gfx_refreshrate;

    if (!quick)
	return 1;
    
    open_windows (0);

    if (isfullscreen () <= 0)
	DirectDraw_FillPrimary ();

    return 0;
}

#ifdef PICASSO96

static int modeswitchneeded (struct winuae_currentmode *wc)
{
    if (isfullscreen () > 0) {
	/* fullscreen to fullscreen */
	if (screen_is_picasso) {
	    if (picasso96_state.BytesPerPixel * 8 != wc->current_depth && currprefs.win32_rtgmatchdepth)
		return -1;
	    if (picasso96_state.Width < wc->current_width && picasso96_state.Height < wc->current_height) {
		if (currprefs.win32_rtgscaleifsmall && !currprefs.win32_rtgmatchdepth)
		    return 0;
	    }
	    if (picasso96_state.Width != wc->current_width ||
		picasso96_state.Height != wc->current_height)
		return 1;
	    if (picasso96_state.Width == wc->current_width &&
		picasso96_state.Height == wc->current_height) {
		if (picasso96_state.BytesPerPixel * 8 == wc->current_depth)
		    return 0;
		if (!currprefs.win32_rtgmatchdepth && wc->current_depth >= 16)
		    return 0;
	    }
	    return 1;
	} else {
	    if (currentmode->current_width != wc->current_width ||
		currentmode->current_height != wc->current_height ||
		currentmode->current_depth != wc->current_depth)
		    return -1;
	}
    } else if (isfullscreen () == 0) {
	/* windowed to windowed */
	return -1;
    } else {
	/* fullwindow to fullwindow */
	DirectDraw_Fill (NULL, 0);
        DirectDraw_BlitToPrimary (NULL);
	if (screen_is_picasso) {
	    if (currprefs.win32_rtgscaleifsmall && (wc->native_width > picasso96_state.Width || wc->native_height > picasso96_state.Height))
		return -1;
	    if (currprefs.win32_rtgallowscaling && (picasso96_state.Width != wc->native_width || picasso96_state.Height != wc->native_height))
		return -1;
	}
        return -1;
    }
    return 0;
}

void gfx_set_picasso_state (int on)
{
    struct winuae_currentmode wc;
    int mode;

    if (screen_is_picasso == on)
	return;
    screen_is_picasso = on;
    rp_rtg_switch ();
    memcpy (&wc, currentmode, sizeof (wc));

    updatemodes ();
    update_gfxparams ();
    clearscreen ();
    if (currprefs.gfx_afullscreen != currprefs.gfx_pfullscreen ||
	(currprefs.gfx_afullscreen == 1 && (currprefs.gfx_filter == UAE_FILTER_DIRECT3D || currprefs.gfx_filter == UAE_FILTER_OPENGL))) {
	mode = 1;
    } else {
	mode = modeswitchneeded (&wc);
	if (!mode)
	    goto end;
    }
    if (mode < 0) {
	open_windows (0);
    } else {
	open_screen (); // reopen everything
    }
end:
#ifdef RETROPLATFORM
    rp_set_hwnd (hAmigaWnd);
#endif
}

void gfx_set_picasso_modeinfo (uae_u32 w, uae_u32 h, uae_u32 depth, RGBFTYPE rgbfmt)
{
    int need;
    if (!screen_is_picasso)
	return;
    clearscreen ();
    gfx_set_picasso_colors (rgbfmt);
    updatemodes ();
    need = modeswitchneeded (currentmode);
    update_gfxparams ();
    if (need > 0) {
        open_screen ();
    } else if (need < 0) {
	open_windows (0);
    }
#ifdef RETROPLATFORM
    rp_set_hwnd (hAmigaWnd);
#endif
}
#endif

void gfx_set_picasso_colors (RGBFTYPE rgbfmt)
{
    alloc_colors_picasso (x_red_bits, x_green_bits, x_blue_bits, x_red_shift, x_green_shift, x_blue_shift, rgbfmt);
}

static void gfxmode_reset (void)
{
#ifdef GFXFILTER
    usedfilter = 0;
    if (currprefs.gfx_filter > 0) {
	int i = 0;
	while (uaefilters[i].name) {
	    if (uaefilters[i].type == currprefs.gfx_filter) {
		usedfilter = &uaefilters[i];
		break;
	    }
	    i++;
	}
    }
#endif
}

int machdep_init (void)
{
    picasso_requested_on = 0;
    picasso_on = 0;
    screen_is_picasso = 0;
    memset (currentmode, 0, sizeof (*currentmode));
#ifdef LOGITECHLCD
    lcd_open();
#endif
#ifdef RETROPLATFORM
    if (rp_param != NULL) {
        if (FAILED (rp_init ()))
	    return 0;
    }
#endif
    systray (hHiddenWnd, FALSE);
    return 1;
}

void machdep_free (void)
{
#ifdef LOGITECHLCD
    lcd_close ();
#endif
}

int graphics_init (void)
{
    gfxmode_reset ();
    return open_windows (1);
}

int graphics_setup (void)
{
    if (!DirectDraw_Start (NULL))
	return 0;
    DirectDraw_Release ();
#ifdef PICASSO96
    InitPicasso96 ();
#endif
    return 1;
}

void graphics_leave (void)
{
    close_windows ();
}

uae_u32 OSDEP_minimize_uae (void)
{
    return ShowWindow (hAmigaWnd, SW_MINIMIZE);
}

void close_windows (void)
{
    reset_sound();
#if defined (GFXFILTER)
    S2X_free ();
#endif
    xfree (gfxvidinfo.realbufmem);
    gfxvidinfo.realbufmem = 0;
    DirectDraw_Release ();
    close_hwnds ();
}

void WIN32GFX_ToggleFullScreen (void)
{
    display_change_requested = 1;
    if (screen_is_picasso)
	currprefs.gfx_pfullscreen ^= 1;
    else
	currprefs.gfx_afullscreen ^= 1;
}

static void createstatuswindow (void)
{
    HDC hdc;
    RECT rc;
    HLOCAL hloc;
    LPINT lpParts;
    int drive_width, hd_width, cd_width, power_width, fps_width, idle_width, snd_width;
    int num_parts = 11;
    double scaleX, scaleY;
    WINDOWINFO wi;
    int extra;

    if (hStatusWnd) {
	ShowWindow (hStatusWnd, SW_HIDE);
	DestroyWindow (hStatusWnd);
    }
    hStatusWnd = CreateWindowEx (
	0, STATUSCLASSNAME, (LPCTSTR) NULL, SBT_TOOLTIPS | WS_CHILD | WS_VISIBLE,
	0, 0, 0, 0, hMainWnd, (HMENU) 1, hInst, NULL);
    if (!hStatusWnd)
	return;
    wi.cbSize = sizeof wi;
    GetWindowInfo (hMainWnd, &wi);
    extra = wi.rcClient.top - wi.rcWindow.top;

    hdc = GetDC (hStatusWnd);
    scaleX = GetDeviceCaps (hdc, LOGPIXELSX) / 96.0;
    scaleY = GetDeviceCaps (hdc, LOGPIXELSY) / 96.0;
    ReleaseDC (hStatusWnd, hdc);
    drive_width = (int)(24 * scaleX);
    hd_width = (int)(24 * scaleX);
    cd_width = (int)(24 * scaleX);
    power_width = (int)(42 * scaleX);
    fps_width = (int)(64 * scaleX);
    idle_width = (int)(64 * scaleX);
    snd_width = (int)(64 * scaleX);
    GetClientRect (hMainWnd, &rc);
    /* Allocate an array for holding the right edge coordinates. */
    hloc = LocalAlloc (LHND, sizeof (int) * num_parts);
    if (hloc) {
	lpParts = LocalLock (hloc);
	/* Calculate the right edge coordinate for each part, and copy the coords
	 * to the array.  */
	lpParts[0] = rc.right - (drive_width * 4) - power_width - idle_width - fps_width - cd_width - hd_width - snd_width - extra;
	lpParts[1] = lpParts[0] + snd_width;
	lpParts[2] = lpParts[1] + idle_width;
	lpParts[3] = lpParts[2] + fps_width;
	lpParts[4] = lpParts[3] + power_width;
	lpParts[5] = lpParts[4] + hd_width;
	lpParts[6] = lpParts[5] + cd_width;
	lpParts[7] = lpParts[6] + drive_width;
	lpParts[8] = lpParts[7] + drive_width;
	lpParts[9] = lpParts[8] + drive_width;
	lpParts[10] = lpParts[9] + drive_width;
	window_led_drives = lpParts[6];
	window_led_drives_end = lpParts[10];
	window_led_hd = lpParts[4];
	window_led_hd_end = lpParts[5];

	/* Create the parts */
	SendMessage (hStatusWnd, SB_SETPARTS, (WPARAM)num_parts, (LPARAM)lpParts);
	LocalUnlock (hloc);
	LocalFree (hloc);
    }
}

#if 0
#include <dbt.h>

static int createnotification (HWND hwnd)
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    HDEVNOTIFY hDevNotify;

    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    hDevNotify = RegisterDeviceNotification(hMainWnd, 
        &NotificationFilter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

    if(!hDevNotify) 
    {
        write_log (L"RegisterDeviceNotification failed: %d\n", GetLastError());
        return FALSE;
    }

    return TRUE;
}
#endif

static int getbestmode (int nextbest)
{
    int i, disp;
    struct MultiDisplay *md = getdisplay (&currprefs);

    disp = currprefs.gfx_display;
    for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
        struct PicassoResolution *pr = &md->DisplayModes[i];
        if (pr->res.width == currentmode->native_width && pr->res.height == currentmode->native_height)
	    break;
    }
    if (md->DisplayModes[i].depth >= 0) {
	if (!nextbest)
	    return 1;
	while (md->DisplayModes[i].res.width == currentmode->native_width && md->DisplayModes[i].res.height == currentmode->native_height)
	    i++;
    } else {
	i = 0;
    }
    for (; md->DisplayModes[i].depth >= 0; i++) {
	struct PicassoResolution *pr = &md->DisplayModes[i];
	if (pr->res.width >= currentmode->native_width && pr->res.height >= currentmode->native_height) {
	    write_log (L"FS: %dx%d -> %dx%d\n", currentmode->native_width, currentmode->native_height,
		pr->res.width, pr->res.height);
	    currentmode->native_width = pr->res.width;
	    currentmode->native_height = pr->res.height;
	    currentmode->current_width = currentmode->native_width;
	    currentmode->current_height = currentmode->native_height;
	    return 1;
	}
    }
    return 0;
}

static int create_windows_2 (void)
{
    int dxfs = currentmode->flags & (DM_DX_FULLSCREEN);
    int d3dfs = currentmode->flags & (DM_D3D_FULLSCREEN);
    int fsw = currentmode->flags & (DM_W_FULLSCREEN);
    DWORD exstyle = currprefs.win32_notaskbarbutton ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
    DWORD flags = 0;
    HWND hhWnd = NULL;//currprefs.win32_notaskbarbutton ? hHiddenWnd : NULL;
    int borderless = currprefs.win32_borderless;
    DWORD style = NORMAL_WINDOW_STYLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    int cymenu = GetSystemMetrics (SM_CYMENU);
    int cyborder = GetSystemMetrics (SM_CYBORDER);
    int cxborder = GetSystemMetrics (SM_CXBORDER);
    int gap = 3;
    int x, y, w, h;

    if (hAmigaWnd) {
	RECT r;
	int w, h, x, y;
	int nw, nh, nx, ny;
	GetWindowRect (hAmigaWnd, &r);
	x = r.left;
	y = r.top;
	w = r.right - r.left;
	h = r.bottom - r.top;
        nx = x;
        ny = y;

	if (screen_is_picasso) {
	    nw = currentmode->current_width;
	    nh = currentmode->current_height;
	} else {
	    nw = currprefs.gfx_size_win.width;
	    nh = currprefs.gfx_size_win.height;
	}

	if (fsw || dxfs) {
	    RECT rc = getdisplay (&currprefs)->rect;
	    nx = rc.left;
	    ny = rc.top;
	    nw = rc.right - rc.left;
	    nh = rc.bottom - rc.top;
	} else if (d3dfs) {
	    RECT rc = getdisplay (&currprefs)->rect;
	    nw = currentmode->native_width;
	    nh = currentmode->native_height;
	    if (rc.left >= 0)
	        nx = rc.left;
	    else
	        nx = rc.left + (rc.right - rc.left - nw);
	    if (rc.top >= 0)
	        ny = rc.top;
	    else
	        ny = rc.top + (rc.bottom - rc.top - nh);
	}
	if (w != nw || h != nh || x != nx || y != ny) {
	    w = nw;
	    h = nh;
	    x = nx;
	    y = ny;
	    in_sizemove++;
	    if (hMainWnd && !fsw && !dxfs && !d3dfs && !rp_isactive ()) {
    		GetWindowRect (hMainWnd, &r);
		x = r.left;
		y = r.top;
		SetWindowPos (hMainWnd, HWND_TOP, x, y, w + window_extra_width, h + window_extra_height,
		    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
		x = gap - 1;
		y = gap - 2;
	    }
	    SetWindowPos (hAmigaWnd, HWND_TOP, x, y, w, h,
	        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
	    if (hStatusWnd)
		createstatuswindow ();
	    in_sizemove--;
	} else {
	    w = nw;
	    h = nh;
	    x = nx;
	    y = ny;
	}
	GetWindowRect (hAmigaWnd, &amigawin_rect);
	if (d3dfs || dxfs)
	    SetCursorPos (x + w / 2, y + h / 2);
	write_log (L"window already open\n");
#ifdef RETROPLATFORM
	rp_set_hwnd (hAmigaWnd);
#endif
	return 1;
    }

    if (fsw && !borderless)
	borderless = 1;
    window_led_drives = 0;
    window_led_drives_end = 0;
    hMainWnd = NULL;
    x = 2; y = 2;
    if (borderless)
	cymenu = cyborder = cxborder = 0;

    if (!dxfs && !d3dfs)  {
	RECT rc;
	LONG stored_x = 1, stored_y = cymenu + cyborder;
	int oldx, oldy;
	int first = 2;

	regqueryint (NULL, L"MainPosX", &stored_x);
	regqueryint (NULL, L"MainPosY", &stored_y);

	while (first) {
	    first--;
	    if (stored_x < GetSystemMetrics (SM_XVIRTUALSCREEN))
		stored_x = GetSystemMetrics (SM_XVIRTUALSCREEN);
	    if (stored_y < GetSystemMetrics (SM_YVIRTUALSCREEN) + cymenu + cyborder)
		stored_y = GetSystemMetrics (SM_YVIRTUALSCREEN) + cymenu + cyborder;

	    if (stored_x > GetSystemMetrics (SM_CXVIRTUALSCREEN))
		rc.left = 1;
	    else
		rc.left = stored_x;

	    if (stored_y > GetSystemMetrics (SM_CYVIRTUALSCREEN))
		rc.top = 1;
	    else
		rc.top = stored_y;

	    rc.right = rc.left + gap + currentmode->current_width + gap - 2;
	    rc.bottom = rc.top + gap + currentmode->current_height + gap + cymenu - 1 - 2;

	    oldx = rc.left;
	    oldy = rc.top;
	    AdjustWindowRect (&rc, borderless ? WS_POPUP : style, FALSE);
	    win_x_diff = rc.left - oldx;
	    win_y_diff = rc.top - oldy;

	    if (MonitorFromRect (&rc, MONITOR_DEFAULTTONULL) == NULL) {
		write_log (L"window coordinates are not visible on any monitor, reseting..\n");
		stored_x = stored_y = 0;
		continue;
	    }
	    break;
	}

	if (fsw) {
	    rc = getdisplay (&currprefs)->rect;
	    flags |= WS_EX_TOPMOST;
	    style = WS_POPUP;
	    currentmode->native_width = rc.right - rc.left;
	    currentmode->native_height = rc.bottom - rc.top;
	}
	flags |= (currprefs.win32_alwaysontop ? WS_EX_TOPMOST : 0);

	if (!borderless) {
	    RECT rc2;
	    hMainWnd = CreateWindowEx (WS_EX_ACCEPTFILES | exstyle | flags,
		L"PCsuxRox", L"WinUAE",
		style,
		rc.left, rc.top,
		rc.right - rc.left + 1, rc.bottom - rc.top + 1,
		hhWnd, NULL, hInst, NULL);
	    if (!hMainWnd) {
		write_log (L"main window creation failed\n");
		return 0;
	    }
	    GetWindowRect (hMainWnd, &rc2);
	    window_extra_width = rc2.right - rc2.left - currentmode->current_width;
	    window_extra_height = rc2.bottom - rc2.top - currentmode->current_height;
	    if (!(currentmode->flags & DM_W_FULLSCREEN))
		createstatuswindow ();
	} else {
	    x = rc.left;
	    y = rc.top;
	}
	w = currentmode->native_width;
	h = currentmode->native_height;

    } else {

	RECT rc;
	getbestmode (0);
	w = currentmode->native_width;
	h = currentmode->native_height;
	rc = getdisplay (&currprefs)->rect;
	if (rc.left >= 0)
	    x = rc.left;
	else
	    x = rc.left + (rc.right - rc.left - w);
	if (rc.top >= 0)
	    y = rc.top;
	else
	    y = rc.top + (rc.bottom - rc.top - h);
    }

    if (rp_isactive () && !dxfs && !d3dfs && !fsw) {
	HWND parent = rp_getparent ();
	hAmigaWnd = CreateWindowEx (dxfs || d3dfs ? WS_EX_ACCEPTFILES | WS_EX_TOPMOST : WS_EX_ACCEPTFILES | WS_EX_TOOLWINDOW | (currprefs.win32_alwaysontop ? WS_EX_TOPMOST : 0),
	    L"AmigaPowah", L"WinUAE",
	    WS_POPUP,
	    x, y, w, h,
	    parent, NULL, hInst, NULL);
    } else {
	hAmigaWnd = CreateWindowEx (dxfs || d3dfs ?
	    WS_EX_TOPMOST :
	    WS_EX_ACCEPTFILES | exstyle | (currprefs.win32_alwaysontop ? WS_EX_TOPMOST : 0),
	    L"AmigaPowah", L"WinUAE",
	    (dxfs || d3dfs ? WS_POPUP : (WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (hMainWnd ? WS_VISIBLE | WS_CHILD : WS_VISIBLE | WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX))),
	    x, y, w, h,
	    borderless ? NULL : (hMainWnd ? hMainWnd : hhWnd), NULL, hInst, NULL);
    }
    if (!hAmigaWnd) {
	write_log (L"creation of amiga window failed\n");
	close_hwnds();
	return 0;
    }
    if (hMainWnd == NULL)
	hMainWnd = hAmigaWnd;
    GetWindowRect (hAmigaWnd, &amigawin_rect);
    if (dxfs || d3dfs)
	SetCursorPos (x + w / 2, y + h / 2);
    addnotifications (hAmigaWnd, FALSE);
    if (hMainWnd != hAmigaWnd) {
	ShowWindow (hMainWnd, SW_SHOWNORMAL);
	UpdateWindow (hMainWnd);
    }
    ShowWindow (hAmigaWnd, SW_SHOWNORMAL);
    UpdateWindow (hAmigaWnd);

    return 1;
}

static int set_ddraw (void)
{
    int cnt, ret;

    if (picasso_on)
	currentmode->pal = (LPPALETTEENTRY) & picasso96_state.CLUT;
    else
	currentmode->pal = colors256;

    cnt = 3;
    for (;;) {
	ret = set_ddraw_2 ();
	if (cnt-- <= 0)
	    return 0;
	if (ret < 0) {
    	    getbestmode (1);
	    continue;
	}
	if (ret == 0)
	    return 0;
	break;
    }
    return 1;
}

static int create_windows (void)
{
    if (!create_windows_2 ())
	return 0;
    return set_ddraw ();
}

static BOOL doInit (void)
{
    int fs_warning = -1;
    TCHAR tmpstr[300];
    RGBFTYPE colortype;
    int tmp_depth;
    int ret = 0;
    int mult = 0;

    colortype = DirectDraw_GetPixelFormat();
    gfxmode_reset ();

    for (;;) {
	updatemodes ();
	currentmode->native_depth = 0;
	tmp_depth = currentmode->current_depth;

	if (currentmode->flags & DM_W_FULLSCREEN) {
	    RECT rc = getdisplay (&currprefs)->rect;
	    currentmode->native_width = rc.right - rc.left;
	    currentmode->native_height = rc.bottom - rc.top;
	}

	write_log (L"W=%d H=%d B=%d CT=%d\n",
	    DirectDraw_CurrentWidth (), DirectDraw_CurrentHeight (), DirectDraw_GetCurrentDepth (), colortype);

	if (currentmode->current_depth < 15 && (currprefs.chipset_mask & CSMASK_AGA) && isfullscreen () > 0 && !WIN32GFX_IsPicassoScreen()) {
	    static int warned;
	    if (!warned) {
		TCHAR szMessage[MAX_DPATH];
		currentmode->current_depth = 16;
		WIN32GUI_LoadUIString(IDS_AGA8BIT, szMessage, MAX_DPATH);
		gui_message(szMessage);
	    }
	    warned = 1;
	}

	if (isfullscreen() <= 0 && !(currentmode->flags & (DM_OPENGL | DM_D3D))) {
	    currentmode->current_depth = DirectDraw_GetCurrentDepth ();
	    updatemodes ();
	}
	if (!(currentmode->flags & (DM_OPENGL | DM_D3D)) && DirectDraw_GetCurrentDepth () == currentmode->current_depth) {
	    updatemodes ();
	}

	if (colortype == RGBFB_NONE) {
	    fs_warning = IDS_UNSUPPORTEDSCREENMODE_1;
	} else if (colortype == RGBFB_CLUT && DirectDraw_GetCurrentDepth () != 8) {
	    fs_warning = IDS_UNSUPPORTEDSCREENMODE_2;
	} else if (currentmode->current_width > GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
	    currentmode->current_height > GetSystemMetrics(SM_CYVIRTUALSCREEN)) {
	    if (!console_logging)
		fs_warning = IDS_UNSUPPORTEDSCREENMODE_3;
	}
	if (fs_warning >= 0 && isfullscreen () <= 0) {
	    TCHAR szMessage[MAX_DPATH], szMessage2[MAX_DPATH];
	    WIN32GUI_LoadUIString(IDS_UNSUPPORTEDSCREENMODE, szMessage, MAX_DPATH);
	    WIN32GUI_LoadUIString(fs_warning, szMessage2, MAX_DPATH);
	    // Temporarily drop the DirectDraw stuff
	    DirectDraw_Release ();
	    _stprintf (tmpstr, szMessage, szMessage2);
	    gui_message (tmpstr);
	    DirectDraw_Start (displayGUID);
	    if (screen_is_picasso)
		changed_prefs.gfx_pfullscreen = currprefs.gfx_pfullscreen = 1;
	    else
		changed_prefs.gfx_afullscreen = currprefs.gfx_afullscreen = 1;
	    updatewinfsmode (&currprefs);
	    updatewinfsmode (&changed_prefs);
	    currentmode->current_depth = tmp_depth;
	    updatemodes ();
	    ret = -2;
	    goto oops;
	}
	if (! create_windows ())
	    goto oops;
#ifdef PICASSO96
	if (screen_is_picasso) {
	    break;
	} else {
#endif
	    currentmode->native_depth = currentmode->current_depth;
#if defined (GFXFILTER)
	    if (currentmode->flags & (DM_OPENGL | DM_D3D | DM_SWSCALE)) {
		currentmode->amiga_width = AMIGA_WIDTH_MAX;
		currentmode->amiga_height = AMIGA_HEIGHT_MAX >> (currprefs.gfx_linedbl ? 0 : 1);
		if (currprefs.gfx_resolution == 0)
		    currentmode->amiga_width >>= 1;
		else if (currprefs.gfx_resolution > 1)
		    currentmode->amiga_width <<= 1;
		if (usedfilter) {
		    if (usedfilter->x[0]) {
			currentmode->current_depth = (currprefs.gfx_filter_filtermode / 2) ? 32 : 16;
		    } else {
			int j, i;
			j = 0;
			for (i = 1; i <= 4; i++) {
			    if (usedfilter->x[i])
				j++;
			}
			i = currprefs.gfx_filter_filtermode;
			if (i >= j)
			    i = 0;
			j = 0;
			while (i >= 0) {
			    while (!usedfilter->x[j])
				j++;
			    if(i-- > 0)
				j++;
			}
			if ((usedfilter->x[j] & (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) == (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) {
			    currentmode->current_depth = currentmode->native_depth;
			} else {
			    currentmode->current_depth = (usedfilter->x[j] & UAE_FILTER_MODE_16) ? 16 : 32;
			}
			mult = j;
		    }
		}
		currentmode->pitch = currentmode->amiga_width * currentmode->current_depth >> 3;
	    }
		else
#endif
	    {
		currentmode->amiga_width = currentmode->current_width;
		currentmode->amiga_height = currentmode->current_height;
	    }
	    gfxvidinfo.pixbytes = currentmode->current_depth >> 3;
	    gfxvidinfo.bufmem = 0;
	    gfxvidinfo.linemem = 0;
	    gfxvidinfo.emergmem = scrlinebuf; // memcpy from system-memory to video-memory
	    gfxvidinfo.width = currentmode->amiga_width;
	    gfxvidinfo.height = currentmode->amiga_height;
	    gfxvidinfo.maxblocklines = 0; // flush_screen actually does everything
	    gfxvidinfo.rowbytes = currentmode->pitch;
	    break;
#ifdef PICASSO96
	}
#endif
    }

#ifdef PICASSO96
    picasso_vidinfo.rowbytes = DirectDraw_GetSurfacePitch ();
    picasso_vidinfo.pixbytes = DirectDraw_GetBytesPerPixel ();
    picasso_vidinfo.rgbformat = DirectDraw_GetPixelFormat ();
    picasso_vidinfo.extra_mem = 1;
    picasso_vidinfo.height = currentmode->current_height;
    picasso_vidinfo.width = currentmode->current_width;
    picasso_vidinfo.depth = currentmode->current_depth;
    picasso_vidinfo.offset = 0;
#endif

    xfree (gfxvidinfo.realbufmem);
    gfxvidinfo.realbufmem = NULL;
    gfxvidinfo.bufmem = NULL;

    if ((currentmode->flags & DM_DDRAW) && !(currentmode->flags & (DM_D3D | DM_SWSCALE | DM_OPENGL))) {

	;

    } else if (!(currentmode->flags & DM_SWSCALE)) {

	int size = currentmode->amiga_width * currentmode->amiga_height * gfxvidinfo.pixbytes;
	gfxvidinfo.realbufmem = xmalloc (size);
	gfxvidinfo.bufmem = gfxvidinfo.realbufmem;
	gfxvidinfo.rowbytes = currentmode->amiga_width * gfxvidinfo.pixbytes;
	gfxvidinfo.bufmemend = gfxvidinfo.bufmem + size;

    } else if (!(currentmode->flags & DM_D3D)) {

	int w = currentmode->amiga_width * 2;
	int h = currentmode->amiga_height * 2;
	int size = (w * 2) * (h * 3) * gfxvidinfo.pixbytes;
	gfxvidinfo.realbufmem = xmalloc (size);
	memset (gfxvidinfo.realbufmem, 0, size);
	gfxvidinfo.bufmem = gfxvidinfo.realbufmem + (w + (w * 2) * h) * gfxvidinfo.pixbytes;
	gfxvidinfo.rowbytes = w * 2 * gfxvidinfo.pixbytes;
	gfxvidinfo.bufmemend = gfxvidinfo.realbufmem + size - gfxvidinfo.rowbytes;

    }

    init_row_map ();
    init_colors ();

#if defined (GFXFILTER)
    S2X_free ();
    if ((currentmode->flags & DM_SWSCALE) && !WIN32GFX_IsPicassoScreen ()) {
	S2X_init (currentmode->native_width, currentmode->native_height,
	    currentmode->amiga_width, currentmode->amiga_height,
	    mult, currentmode->current_depth, currentmode->native_depth);
    }
#if defined OPENGL
    if (currentmode->flags & DM_OPENGL) {
	const TCHAR *err = OGL_init (hAmigaWnd, currentmode->native_width, currentmode->native_height,
	    currentmode->amiga_width, currentmode->amiga_height, currentmode->current_depth);
	if (err) {
	    OGL_free ();
	    if (err[0] != '*') {
		gui_message (err);
		changed_prefs.gfx_filter = currprefs.gfx_filter = 0;
	    }
	    currentmode->current_depth = currentmode->native_depth;
	    gfxmode_reset ();
	    ret = -1;
	    goto oops;
	}
    }
#endif
#ifdef D3D
    if (currentmode->flags & DM_D3D) {
	const TCHAR *err = D3D_init (hAmigaWnd, currentmode->native_width, currentmode->native_height,
	    currentmode->amiga_width, currentmode->amiga_height, currentmode->current_depth);
	if (err) {
	    D3D_free ();
	    gui_message (err);
	    changed_prefs.gfx_filter = currprefs.gfx_filter = 0;
	    currentmode->current_depth = currentmode->native_depth;
	    gfxmode_reset ();
	    ret = -1;
	    goto oops;
	}
    }
#endif
#endif
    screen_is_initialized = 1;
    WIN32GFX_SetPalette ();
#ifdef PICASSO96
    DX_SetPalette (0, 256);
#endif
    picasso_refresh ();

    if (isfullscreen () > 0)
	setmouseactive (-1);

    return 1;

oops:
    close_hwnds();
    return ret;
}


void WIN32GFX_PaletteChange(void)
{
    if (!(currentmode->flags & DM_DDRAW) || (currentmode->flags & DM_D3D))
	return;
    if (currentmode->current_depth > 8)
	return;
    DirectDraw_SetPalette (1); /* Remove current palette */
    DirectDraw_SetPalette (0); /* Set our real palette */
}

int WIN32GFX_ClearPalette(void)
{
    if (currentmode->current_depth > 8)
	return 1;
    if (!(currentmode->flags & DM_DDRAW) || (currentmode->flags & DM_D3D))
	return 1;
    DirectDraw_SetPalette (1); /* Remove palette */
    return 1;
}

int WIN32GFX_SetPalette(void)
{
    if (!(currentmode->flags & DM_DDRAW) || (currentmode->flags & DM_D3D))
	return 1;
    if (currentmode->current_depth > 8)
	return 1;
    DirectDraw_SetPalette (0); /* Set palette */
    return 1;
}
void WIN32GFX_WindowMove (void)
{
}

void updatedisplayarea (void)
{
    if (!screen_is_initialized)
	return;
    if (picasso_on)
	return;
    if (dx_islost ())
	return;
#if defined (GFXFILTER)
    if (currentmode->flags & DM_OPENGL) {
#if defined (OPENGL)
	OGL_refresh ();
#endif
    } else if (currentmode->flags & DM_D3D) {
#if defined (D3D)
	D3D_refresh ();
#endif
    } else
#endif
    if (currentmode->flags & DM_DDRAW) {
#if defined (GFXFILTER)
	if (currentmode->flags & DM_SWSCALE)
	    S2X_refresh ();
#endif
	DirectDraw_Flip (0);
    }
}

void updatewinfsmode (struct uae_prefs *p)
{
    struct MultiDisplay *md;

    fixup_prefs_dimensions (p);
    if (isfullscreen_2 (p) != 0) {
	p->gfx_size = p->gfx_size_fs;
    } else {
	p->gfx_size = p->gfx_size_win;
    }
    displayGUID = NULL;
    md = getdisplay (p);
    if (md->disabled) {
	p->gfx_display = 0;
	md = getdisplay (p);
    }
    if (!md->primary)
	displayGUID = &md->guid;
    if (isfullscreen () == 0)
	displayGUID = NULL;
}

void toggle_fullscreen (void)
{
    if(picasso_on)
	changed_prefs.gfx_pfullscreen = !changed_prefs.gfx_pfullscreen;
    else
	changed_prefs.gfx_afullscreen = !changed_prefs.gfx_afullscreen;
    updatewinfsmode (&changed_prefs);
}

HDC gethdc (void)
{
    HDC hdc = 0;
#ifdef OPENGL
    if (OGL_isenabled ())
	return OGL_getDC (0);
#endif
#ifdef D3D
    if (D3D_isenabled ())
	return D3D_getDC (0);
#endif
    if(FAILED (DirectDraw_GetDC (&hdc)))
	hdc = 0;
    return hdc;
}

void releasehdc (HDC hdc)
{
#ifdef OPENGL
    if (OGL_isenabled ()) {
	OGL_getDC (hdc);
	return;
    }
#endif
#ifdef D3D
    if (D3D_isenabled ()) {
	D3D_getDC (hdc);
	return;
    }
#endif
    DirectDraw_ReleaseDC (hdc);
}
