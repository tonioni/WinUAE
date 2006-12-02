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
#include "options.h"
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
#include "win32.h"
#include "win32gfx.h"
#include "win32gui.h"
#include "resource.h"
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

#define AMIGA_WIDTH_MAX 736
#define AMIGA_HEIGHT_MAX 568

#define DM_DX_FULLSCREEN 1
#define DM_W_FULLSCREEN 2
#define DM_OVERLAY 4
#define DM_OPENGL 8
#define DM_DX_DIRECT 16
#define DM_PICASSO96 32
#define DM_DDRAW 64
#define DM_DC 128
#define DM_D3D 256
#define DM_D3D_FULLSCREEN 512
#define DM_SWSCALE 1024

struct uae_filter *usedfilter;

struct winuae_modes {
    int fallback;
    char *name;
    unsigned int aflags;
    unsigned int pflags;
};
struct winuae_currentmode {
    struct winuae_modes *mode;
    struct winuae_modes *pmode[2];
    struct winuae_modes *amode[2];
    unsigned int flags;
    int current_width, current_height, current_depth, real_depth, pitch;
    int amiga_width, amiga_height;
    int frequency;
    int mapping_is_mainscreen;
    int initdone;
    int modeindex;
    LPPALETTEENTRY pal;
};

struct PicassoResolution *DisplayModes;
struct MultiDisplay Displays[MAX_DISPLAYS];
GUID *displayGUID;

static struct winuae_currentmode currentmodestruct;
static int screen_is_initialized;
int display_change_requested, normal_display_change_starting;
int window_led_drives, window_led_drives_end;
extern int console_logging;
int b0rken_ati_overlay;

#define SM_WINDOW 0
#define SM_WINDOW_OVERLAY 1
#define SM_FULLSCREEN_DX 2
#define SM_OPENGL_WINDOW 3
#define SM_OPENGL_FULLSCREEN_DX 4
#define SM_D3D_WINDOW 5
#define SM_D3D_FULLSCREEN_DX 6
#define SM_NONE 7

static struct winuae_modes wmodes[] =
{
    {
	0, "Windowed",
	DM_DDRAW,
	DM_PICASSO96 | DM_DDRAW
    },
    {
	0, "Windowed Overlay",
	DM_OVERLAY | DM_DX_DIRECT | DM_DDRAW,
	DM_OVERLAY | DM_DX_DIRECT | DM_DDRAW | DM_PICASSO96
    },
    {
	1, "Fullscreen",
//	DM_OVERLAY | DM_W_FULLSCREEN | DM_DX_DIRECT | DM_DDRAW,
	DM_DX_FULLSCREEN | DM_DX_DIRECT | DM_DDRAW,
	DM_DX_FULLSCREEN | DM_DX_DIRECT | DM_DDRAW | DM_PICASSO96
    },
    {
	1, "Windowed OpenGL",
	DM_OPENGL | DM_DC,
	0
    },
    {
	3, "DirectDraw Fullscreen OpenGL",
	DM_OPENGL | DM_DX_FULLSCREEN | DM_DC,
	0
    },
    {
	0, "Windowed Direct3D",
	DM_D3D,
	0
    },
    {
	0, "Fullscreen Direct3D",
	DM_D3D | DM_D3D_FULLSCREEN,
	0
    },
    {
	0, "none",
	0,
	0
    }
};

static struct winuae_currentmode *currentmode = &currentmodestruct;
static int gfx_tempbitmap;

static int modefallback (unsigned int mask)
{
    if (mask == DM_OVERLAY) {
	if (currentmode->amode[0] == &wmodes[SM_WINDOW_OVERLAY])
	    currentmode->amode[0] = &wmodes[0];
	if (currentmode->pmode[0] == &wmodes[SM_WINDOW_OVERLAY])
	    currentmode->pmode[0] = &wmodes[0];
	return 1;
    }
    if (!picasso_on) {
	if (currprefs.gfx_afullscreen) {
	    currprefs.gfx_afullscreen = changed_prefs.gfx_afullscreen = 0;
	    updatewinfsmode (&currprefs);
	    return 1;
	} else {
	    if (currentmode->amode[0] == &wmodes[0])
		return 0;
	    currentmode->amode[0] = &wmodes[0];
	    return 1;
	}
    } else {
	if (currprefs.gfx_pfullscreen) {
	    currprefs.gfx_pfullscreen = changed_prefs.gfx_pfullscreen = 0;
	    return 1;
	} else {
	    if (currentmode->pmode[0] == &wmodes[0]) {
		currprefs.gfx_pfullscreen = changed_prefs.gfx_pfullscreen = 1;
		return 1;
	    }
	    currentmode->pmode[0] = &wmodes[0];
	    return 1;
	}
    }
    return 0;
}

int screen_is_picasso = 0;

int WIN32GFX_IsPicassoScreen( void )
{
    return screen_is_picasso;
}

void WIN32GFX_DisablePicasso( void )
{
    picasso_requested_on = 0;
    picasso_on = 0;
}

void WIN32GFX_EnablePicasso( void )
{
    picasso_requested_on = 1;
}

void WIN32GFX_DisplayChangeRequested( void )
{
    display_change_requested = 1;
}

int isscreen (void)
{
    return hMainWnd ? 1 : 0;
}

int isfullscreen (void)
{
    if (screen_is_picasso)
	return currprefs.gfx_pfullscreen;
    else
	return currprefs.gfx_afullscreen;
}

int is3dmode (void)
{
    return currentmode->flags & (DM_D3D | DM_OPENGL);
}

int WIN32GFX_GetDepth (int real)
{
    if (!currentmode->real_depth)
	return currentmode->current_depth;
    return real ? currentmode->real_depth : currentmode->current_depth;
}

int WIN32GFX_GetWidth( void )
{
    return currentmode->current_width;
}

int WIN32GFX_GetHeight( void )
{
    return currentmode->current_height;
}

#include "dxwrap.h"

static BOOL doInit (void);

uae_u32 default_freq = 0;

HWND hStatusWnd = NULL;
HINSTANCE hDDraw = NULL;
uae_u16 picasso96_pixel_format = RGBFF_CHUNKY;

/* For the DX_Invalidate() and gfx_unlock_picasso() functions */
static int p96_double_buffer_first, p96_double_buffer_last, p96_double_buffer_needs_flushing = 0;

static char scrlinebuf[4096 * 4]; /* this is too large, but let's rather play on the safe side here */

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

#if 0
static DEVMODE dmScreenSettings;
static volatile cdsthread_ret;

static void cdsthread (void *dummy)
{
    int ret = ChangeDisplaySettings (&dmScreenSettings, CDS_FULLSCREEN);
    if (ret != DISP_CHANGE_SUCCESSFUL && dmScreenSettings.dmDisplayFrequency > 0) {
	dmScreenSettings.dmFields &= ~DM_DISPLAYFREQUENCY;
	ret = ChangeDisplaySettings (&dmScreenSettings, CDS_FULLSCREEN);
    }
    if (ret != DISP_CHANGE_SUCCESSFUL) {
	cdsthread_ret = 0;
	return;
    }
    cdsthread_ret = 1;
}

#include <process.h>
static int do_changedisplaysettings (int width, int height, int bits, int freq)
{
    memset (&dmScreenSettings, 0, sizeof(dmScreenSettings));
    dmScreenSettings.dmSize = sizeof(dmScreenSettings);
    dmScreenSettings.dmPelsWidth = width;
    dmScreenSettings.dmPelsHeight = height;
    dmScreenSettings.dmBitsPerPel = bits;
    dmScreenSettings.dmDisplayFrequency = freq;
    dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | (freq > 0 ? DM_DISPLAYFREQUENCY : 0);
    cdsthread_ret = -1;
    _beginthread (&cdsthread, 0, 0);
    while (cdsthread_ret < 0)
	Sleep (10);
    return cdsthread_ret;
}
#endif

static int set_ddraw (void)
{
    HRESULT ddrval;
    int bits = (currentmode->current_depth + 7) & ~7;
    int width = currentmode->current_width;
    int height = currentmode->current_height;
    int freq = currentmode->frequency;
    int dxfullscreen, wfullscreen, dd, overlay;

    dxfullscreen = (currentmode->flags & DM_DX_FULLSCREEN) ? TRUE : FALSE;
    wfullscreen = (currentmode->flags & DM_W_FULLSCREEN) ? TRUE : FALSE;
    dd = (currentmode->flags & DM_DDRAW) ? TRUE : FALSE;
    overlay = (currentmode->flags & DM_OVERLAY) ? TRUE : FALSE;

    ddrval = DirectDraw_SetCooperativeLevel(hAmigaWnd, dxfullscreen);
    if (FAILED(ddrval))
	goto oops;

    if (dxfullscreen)  {
	write_log( "set_ddraw: Trying %dx%d, bits=%d, refreshrate=%d\n", width, height, bits, freq );
	ddrval = DirectDraw_SetDisplayMode (width, height, bits, freq);
	if (FAILED(ddrval)) {
	    write_log ("set_ddraw: failed, trying without forced refresh rate\n");
	    ddrval = DirectDraw_SetDisplayMode (width, height, bits, 0);
	    if (FAILED(ddrval)) {
		write_log( "set_ddraw: Couldn't SetDisplayMode()\n" );
		goto oops;
	    }
	}

	ddrval = DirectDraw_GetDisplayMode();
	if (FAILED(ddrval)) {
	    write_log( "set_ddraw: Couldn't GetDisplayMode()\n" );
	    goto oops;
	}
    }

    if (dd) {
	ddrval = DirectDraw_CreateClipper();
	if (FAILED(ddrval)) {
	    write_log( "set_ddraw: No clipping support\n" );
	    goto oops;
	}
	ddrval = DirectDraw_CreateSurface (width, height);
	if (FAILED(ddrval)) {
	    write_log( "set_ddraw: Couldn't CreateSurface() for primary because %s.\n", DXError( ddrval ) );
	    goto oops;
	}
	if (DirectDraw_GetPrimaryBitCount() != (unsigned)bits && overlay) {
	    ddrval = DirectDraw_CreateOverlaySurface (width, height, bits, 0);
	    if(FAILED(ddrval))
	    {
		write_log( "set_ddraw: Couldn't CreateOverlaySurface(%d,%d,%d) because %s.\n", width, height, bits, DXError( ddrval ) );
		goto oops2;
	    }
	} else {
	    overlay = 0;
	}

	DirectDraw_ClearSurfaces();

	if (!DirectDraw_DetermineLocking (dxfullscreen))
	{
	    write_log( "set_ddraw: Couldn't determine locking.\n" );
	    goto oops;
	}

	ddrval = DirectDraw_SetClipper (hAmigaWnd);

	if (FAILED(ddrval)) {
	    write_log( "set_ddraw: Couldn't SetHWnd()\n" );
	    goto oops;
	}

	if (bits == 8) {
	    ddrval = DirectDraw_CreatePalette (currentmode->pal);
	    if (FAILED(ddrval))
	    {
		write_log( "set_ddraw: Couldn't CreatePalette()\n" );
		goto oops;
	    }
	}
	currentmode->pitch = DirectDraw_GetSurfacePitch ();
    }

    write_log ("set_ddraw() called, and is %dx%d@%d-bytes\n", width, height, bits);
    return 1;

oops:
    write_log ("set_ddraw(): DirectDraw initialization failed with\n%s\n", DXError (ddrval));
oops2:
    return 0;
}

/*
static void dhack(void)
{
    int i = 0;
    while (DisplayModes[i].depth >= 0)
	i++;
    if (i >= MAX_PICASSO_MODES - 1)
	return;
    DisplayModes[i].res.width = 480;
    DisplayModes[i].res.height = 640;
    DisplayModes[i].depth = DisplayModes[i - 1].depth;
    DisplayModes[i].refresh[0] = 0;
    DisplayModes[i].refresh[1] = 0;
    DisplayModes[i].colormodes = DisplayModes[i - 1].colormodes;
    DisplayModes[i + 1].depth = -1;
    sprintf(DisplayModes[i].name, "%dx%d, %d-bit",
	DisplayModes[i].res.width, DisplayModes[i].res.height, DisplayModes[i].depth * 8);
}
*/

static HRESULT CALLBACK modesCallback(LPDDSURFACEDESC2 modeDesc, LPVOID context)
{
    RGBFTYPE colortype;
    int i, j, ct, depth;

    colortype = DirectDraw_GetSurfacePixelFormat(modeDesc);
    if (colortype == RGBFB_NONE || colortype == RGBFB_R8G8B8 || colortype == RGBFB_B8G8R8 )
	return DDENUMRET_OK;
    if (modeDesc->dwWidth > 2048 || modeDesc->dwHeight > 2048)
	return DDENUMRET_OK;
    ct = 1 << colortype;
    depth = 0;
    if (ct & RGBMASK_8BIT)
	depth = 1;
    else if (ct & (RGBMASK_15BIT | RGBMASK_16BIT))
	depth = 2;
    else if (ct & RGBMASK_24BIT)
	depth = 3;
    else if (ct & RGBMASK_32BIT)
	depth = 4;
    if (depth == 0)
	return DDENUMRET_OK;
    i = 0;
    while (DisplayModes[i].depth >= 0) {
	if (DisplayModes[i].depth == depth && DisplayModes[i].res.width == modeDesc->dwWidth && DisplayModes[i].res.height == modeDesc->dwHeight) {
	    for (j = 0; j < MAX_REFRESH_RATES; j++) {
		if (DisplayModes[i].refresh[j] == 0 || DisplayModes[i].refresh[j] == modeDesc->dwRefreshRate)
		    break;
	    }
	    if (j < MAX_REFRESH_RATES) {
		DisplayModes[i].refresh[j] = modeDesc->dwRefreshRate;
		DisplayModes[i].refresh[j + 1] = 0;
		return DDENUMRET_OK;
	    }
	}
	i++;
    }
    picasso96_pixel_format |= ct;
    i = 0;
    while (DisplayModes[i].depth >= 0)
	i++;
    if (i >= MAX_PICASSO_MODES - 1)
	return DDENUMRET_OK;
    DisplayModes[i].res.width = modeDesc->dwWidth;
    DisplayModes[i].res.height = modeDesc->dwHeight;
    DisplayModes[i].depth = depth;
    DisplayModes[i].refresh[0] = modeDesc->dwRefreshRate;
    DisplayModes[i].refresh[1] = 0;
    DisplayModes[i].colormodes = ct;
    DisplayModes[i + 1].depth = -1;
    sprintf(DisplayModes[i].name, "%dx%d, %d-bit",
	DisplayModes[i].res.width, DisplayModes[i].res.height, DisplayModes[i].depth * 8);
    return DDENUMRET_OK;
}

static int resolution_compare (const void *a, const void *b)
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
static void sortmodes (void)
{
    int	i = 0, idx = -1;
    int pw = -1, ph = -1;
    while (DisplayModes[i].depth >= 0)
	i++;
    qsort (DisplayModes, i, sizeof (struct PicassoResolution), resolution_compare);
    for (i = 0; DisplayModes[i].depth >= 0; i++) {
	if (DisplayModes[i].res.height != ph || DisplayModes[i].res.width != pw) {
	    ph = DisplayModes[i].res.height;
	    pw = DisplayModes[i].res.width;
	    idx++;
	}
        DisplayModes[i].residx = idx;
    }
}

static void modesList (void)
{
    int i, j;

    i = 0;
    while (DisplayModes[i].depth >= 0) {
	write_log ("%d: %s (", i, DisplayModes[i].name);
	j = 0;
	while (DisplayModes[i].refresh[j] > 0) {
	    if (j > 0)
		write_log(",");
	    write_log ("%d", DisplayModes[i].refresh[j]);
	    j++;
	}
	write_log(")\n");
	i++;
    }
}

BOOL CALLBACK displaysCallback (GUID *guid, LPSTR desc, LPSTR name, LPVOID ctx, HMONITOR hm)
{
    struct MultiDisplay *md = Displays;
    MONITORINFOEX lpmi;

    while (md->name) {
	if (md - Displays >= MAX_DISPLAYS)
	    return 0;
	md++;
    }
    md->name = my_strdup (desc);
    if (guid == 0) {
	POINT pt = { 0, 0 };
	md->primary = 1;
        lpmi.cbSize = sizeof (lpmi);
	GetMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), (LPMONITORINFO)&lpmi);
	md->rect = lpmi.rcMonitor;
    } else {
	memcpy (&md->guid,  guid, sizeof (GUID));
    }
    write_log ("'%s' '%s' %s\n", desc, name, outGUID(guid));
    if ((strstr(desc, "X1900") || strstr(desc, "X1800") || strstr(desc, "X1600")) && !b0rken_ati_overlay) {
	b0rken_ati_overlay = 1;
	if (!os_vista) {
	    write_log ("** Radeon X1x00 series display card detected, enabling overlay workaround.\n");
	    write_log ("** (blank display with Catalyst 6.1 and newer). Use -disableowr to disable workaround.\n");
	}
    }
    return 1;
}

static BOOL CALLBACK monitorEnumProc(HMONITOR h, HDC hdc, LPRECT rect, LPARAM data)
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
	EnumDisplayMonitors(NULL, NULL, monitorEnumProc, (LPARAM)&cnt);
    } else {
	write_log ("Multimonitor detection disabled\n");
	Displays[0].primary = 1;
	Displays[0].name = "Display";
	Displays[0].disabled = 0;
    }
}

void sortdisplays (void)
{
    struct MultiDisplay *md1, *md2, tmp;
    int i;

    md1 = Displays;
    while (md1->name) {
	md2 = md1 + 1;
	while (md2->name) {
	    if (md1->primary < md2->primary || (md1->primary == md2->primary && strcmp (md1->name, md2->name) > 0)) {
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
	DisplayModes = md1->DisplayModes = xmalloc (sizeof (struct PicassoResolution) * MAX_PICASSO_MODES);
	DisplayModes[0].depth = -1;
	md1->disabled = 1;
	if (DirectDraw_Start (md1->primary ? NULL : &md1->guid)) {
	    if (SUCCEEDED(DirectDraw_GetDisplayMode())) {
		int w = DirectDraw_CurrentWidth ();
		int h = DirectDraw_CurrentHeight ();
		int b = DirectDraw_GetSurfaceBitCount ();
		write_log ("Desktop: W=%d H=%d B=%d\n", w, h, b);
		DirectDraw_EnumDisplayModes (DDEDM_REFRESHRATES , modesCallback);
		//dhack();
		sortmodes ();
		modesList ();
		DirectDraw_Release ();
		if (DisplayModes[0].depth >= 0)
		    md1->disabled = 0;
	    }
	}
	i = 0;
	while (DisplayModes[i].depth > 0)
	    i++;
	write_log ("'%s', %d display modes (%s)\n", md1->name, i, md1->disabled ? "disabled" : "enabled");
	md1++;
    }
    DisplayModes = Displays[0].DisplayModes;
    displayGUID = NULL;
}

static int our_possible_depths[] = { 8, 15, 16, 24, 32 };

RGBFTYPE WIN32GFX_FigurePixelFormats( RGBFTYPE colortype )
{
    HRESULT ddrval;
    int got_16bit_mode = 0;
    int window_created = 0;
    struct PicassoResolution *dm;
    int i;

    ignore_messages_all++;
    DirectDraw_Start (NULL);
    if(colortype == 0) /* Need to query a 16-bit display mode for its pixel-format.  Do this by opening such a screen */
    {
	hAmigaWnd = CreateWindowEx (WS_EX_TOPMOST,
			       "AmigaPowah", VersionStr,
			       WS_VISIBLE | WS_POPUP,
			       CW_USEDEFAULT, CW_USEDEFAULT,
			       1,//GetSystemMetrics (SM_CXSCREEN),
			       1,//GetSystemMetrics (SM_CYSCREEN),
			       hHiddenWnd, NULL, 0, NULL);
	if(hAmigaWnd)
	{
	    window_created = 1;
	    ddrval = DirectDraw_SetCooperativeLevel( hAmigaWnd, TRUE ); /* TRUE indicates full-screen */
	    if(FAILED(ddrval))
	    {
		gui_message("WIN32GFX_FigurePixelFormats: ERROR - %s\n", DXError(ddrval));
		goto out;
	    }
	}
	else
	{
	    gui_message("WIN32GFX_FigurePixelFormats: ERROR - test-window could not be created.\n");
	}
    }
    else
    {
	got_16bit_mode = 1;
    }

    i = 0;
    while (DisplayModes[i].depth >= 0) {
	dm = &DisplayModes[i++];
	if (!got_16bit_mode) {
	    write_log ("figure_pixel_formats: Attempting %dx%d..\n", dm->res.width, dm->res.height);

	    ddrval = DirectDraw_SetDisplayMode (dm->res.width, dm->res.height, 16, 0); /* 0 for default freq */
	    if (FAILED(ddrval))
		continue;

	    ddrval = DirectDraw_GetDisplayMode();
	    if (FAILED(ddrval))
		continue;

	    colortype = DirectDraw_GetPixelFormat();
	    if (colortype != RGBFB_NONE)  {
		/* Clear the 16-bit information, and get the real stuff! */
		dm->colormodes &= ~(RGBFF_R5G6B5PC|RGBFF_R5G5B5PC|RGBFF_R5G6B5|RGBFF_R5G5B5|RGBFF_B5G6R5PC|RGBFF_B5G5R5PC);
		dm->colormodes |= 1 << colortype;
		got_16bit_mode = 1;
		write_log( "Got real 16-bit colour-depth information: 0x%x\n", colortype );
	    }
	} else if (dm->colormodes & (RGBFF_R5G6B5PC|RGBFF_R5G5B5PC|RGBFF_R5G6B5|RGBFF_R5G5B5|RGBFF_B5G6R5PC|RGBFF_B5G5R5PC) )  {
	    /* Clear the 16-bit information, and set the real stuff! */
	    dm->colormodes &= ~(RGBFF_R5G6B5PC|RGBFF_R5G5B5PC|RGBFF_R5G6B5|RGBFF_R5G5B5|RGBFF_B5G6R5PC|RGBFF_B5G5R5PC);
	    dm->colormodes |= 1 << colortype;
	}
    }

    out:
    if (window_created)
    {
	Sleep (1000);
	DestroyWindow (hAmigaWnd);
	hAmigaWnd = NULL;
    }
    DirectDraw_Release ();
    ignore_messages_all--;
    return colortype;
}

/* DirectX will fail with "Mode not supported" if we try to switch to a full
 * screen mode that doesn't match one of the dimensions we got during enumeration.
 * So try to find a best match for the given resolution in our list.  */
int WIN32GFX_AdjustScreenmode(uae_u32 *pwidth, uae_u32 *pheight, uae_u32 *ppixbits)
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

	best = &DisplayModes[0];
	dm = &DisplayModes[1];

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

void setoverlay(int quick)
{
    static RECT sr, dr;
    RECT statusr;
    POINT p = {0,0};
    int maxwidth, maxheight, w, h;
    HMONITOR hm;
    MONITORINFO mi;

    if (quick) {
	if (!(currentmode->flags & DM_OVERLAY) || b0rken_ati_overlay <= 0)
	    return;
	goto end;
    }

    hm = MonitorFromWindow (hMainWnd, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof (mi);
    if (!GetMonitorInfo (hm, &mi))
	return;

    GetClientRect (hMainWnd, &dr);
    // adjust the dest-rect to avoid the status-bar
    if (hStatusWnd) {
	if (GetWindowRect (hStatusWnd, &statusr))
	    dr.bottom = dr.bottom - ( statusr.bottom - statusr.top );
    }

    ClientToScreen(hMainWnd, &p);
    if (!currprefs.win32_borderless) {
	p.x += 2;
	p.y += 2;
    }
    dr.left = p.x;
    dr.top = p.y;
    dr.right += p.x + 1;
    dr.bottom += p.y + 1;
    /* overlay's coordinates are relative to monitor's top/left-corner */
    dr.left -= mi.rcMonitor.left;
    dr.top -= mi.rcMonitor.top;
    dr.right -= mi.rcMonitor.left;
    dr.bottom -= mi.rcMonitor.top;

    w = currentmode->current_width;
    h = currentmode->current_height;

    sr.left = 0;
    sr.top = 0;
    sr.right = w;
    sr.bottom = h;

    // Adjust our dst-rect to match the dimensions of our src-rect
    if (dr.right - dr.left > sr.right - sr.left)
	dr.right = dr.left + sr.right - sr.left;
    if (dr.bottom - dr.top > sr.bottom - sr.top)
	dr.bottom = dr.top + sr.bottom - sr.top;

    maxwidth = mi.rcMonitor.right - mi.rcMonitor.left;
    if (dr.right > maxwidth) {
	sr.right = w - (dr.right - maxwidth);
	dr.right = maxwidth;
    }
    maxheight = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (dr.bottom > maxheight) {
	sr.bottom = h - (dr.bottom - maxheight);
	dr.bottom = maxheight;
    }
    if (dr.left < 0) {
	sr.left = -dr.left;
	dr.left = 0;
    }
    if (dr.top < 0) {
	sr.top = -dr.top;
	dr.top = 0;
    }
end:
    DirectDraw_UpdateOverlay(sr, dr);
}

// This function is only called for full-screen Amiga screen-modes, and simply flips
// the front and back buffers. Additionally, because the emulation is not always drawing
// complete frames, we also need to update the back-buffer with the new contents we just
// flipped to. Thus, after our flip, we blit.
static int DX_Flip(void)
{
    int result = 0;
#if 0
    static frame_time_t end;
    frame_time_t start, used;

    start = read_processor_time();
    used = start - end;
    if (used > 0 && used < vsynctime * 2) {
	int pct = used * 100 / vsynctime;
	write_log ("%d\n", pct);
	if (pct < 95)
	    sleep_millis_busy (2 + (95 - pct) / 10);
    }
#endif
    result = DirectDraw_Flip(0);
    if( result )
    {
//	result = DirectDraw_BltFast(primary_surface, 0, 0, secondary_surface, NULL);
//	result = DirectDraw_BltFast(primary_surface, 0, 0, tertiary_surface, NULL);
//	result = DirectDraw_BltFast(secondary_surface, 0, 0, primary_surface, NULL);
//	result = DirectDraw_BltFast(secondary_surface, 0, 0, tertiary_surface, NULL);
	result = DirectDraw_BltFast(tertiary_surface, 0, 0, primary_surface, NULL);
//	result = DirectDraw_BltFast(tertiary_surface, 0, 0, secondary_surface, NULL);
    }
#if 0
    end = read_processor_time();
#endif
    return result;
}

void flush_line( int lineno )
{

}

void flush_block (int a, int b)
{

}

void flush_screen (int a, int b)
{
    if (currentmode->flags & DM_OPENGL) {
#ifdef OPENGL
	OGL_render ();
#endif
    } else if (currentmode->flags & DM_D3D) {
	return;
#ifdef GFXFILTER
    } else if (currentmode->flags & DM_SWSCALE) {
	S2X_render ();
	if(currentmode->flags & DM_DX_FULLSCREEN )
	    DX_Flip ();
	else if (DirectDraw_GetLockableType() != overlay_surface)
	    DX_Blit (0, 0, 0, 0, WIN32GFX_GetWidth(), WIN32GFX_GetHeight(), BLIT_SRC);
#endif
    } else if ((currentmode->flags & DM_DDRAW) && DirectDraw_GetLockableType() == secondary_surface ) {
	if (currentmode->flags & DM_DX_FULLSCREEN) {
	    if(turbo_emulation || DX_Flip() == 0)
		DX_Blit (0, a, 0, a, currentmode->current_width, b - a + 1, BLIT_SRC);
	} else if(DirectDraw_GetLockableType() != overlay_surface)
	    DX_Blit (0, a, 0, a, currentmode->current_width, b - a + 1, BLIT_SRC);
    }
    setoverlay(1);
}

static uae_u8 *ddraw_dolock (void)
{
    if (!DirectDraw_SurfaceLock(lockable_surface))
	return 0;
    gfxvidinfo.bufmem = DirectDraw_GetSurfacePointer();
    init_row_map ();
    clear_inhibit_frame (IHF_WINDOWHIDDEN);
    return gfxvidinfo.bufmem;
}

int lockscr (void)
{
    if (!isscreen ())
	return 0;
    if (currentmode->flags & DM_D3D) {
#ifdef D3D
	return D3D_locktexture ();
#endif
    } else if (currentmode->flags & DM_SWSCALE) {
	return 1;
    } else if (currentmode->flags & DM_DDRAW) {
	return ddraw_dolock() != 0;
    }
    return 1;
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
	ddraw_unlockscr ();
    }
}

void flush_clear_screen (void)
{
    if (WIN32GFX_IsPicassoScreen())
	return;
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
    uae_u8 *p = ddraw_dolock ();
    picasso_vidinfo.rowbytes = DirectDraw_GetSurfacePitch();
    return p;
}

void gfx_unlock_picasso (void)
{
    DirectDraw_SurfaceUnlock();
    if (p96_double_buffer_needs_flushing) {
	/* Here, our flush_block() will deal with a offscreen-plain (back-buffer) to visible-surface (front-buffer) */
	if (DirectDraw_GetLockableType() == secondary_surface) {
	    BOOL relock = FALSE;
	    if (DirectDraw_IsLocked()) {
		relock = TRUE;
		unlockscr();
	    }
	    DX_Blit (0, p96_double_buffer_first, 
		     0, p96_double_buffer_first, 
		     currentmode->current_width, p96_double_buffer_last - p96_double_buffer_first + 1, 
		     BLIT_SRC);
	    if (relock) {
		lockscr();
	    }
	}
	p96_double_buffer_needs_flushing = 0;
    }
}

static void close_hwnds( void )
{
    screen_is_initialized = 0;
#ifdef AVIOUTPUT
    AVIOutput_Restart ();
#endif
    setmouseactive (0);
    if (hMainWnd)
	systray (hMainWnd, TRUE);
    if (hStatusWnd) {
	ShowWindow (hStatusWnd, SW_HIDE);
	DestroyWindow (hStatusWnd);
    }
    if (hAmigaWnd) {
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
    }
    hMainWnd = 0;
    hStatusWnd = 0;
}

static int open_windows (void)
{
    int ret, i;

    in_sizemove = 0;
    updatewinfsmode (&currprefs);

    if( !DirectDraw_Start(displayGUID) )
	return 0;
    write_log ("DirectDraw GUID=%s\n", outGUID (displayGUID));

    ret = -2;
    do {
	if (ret < -1) {
#ifdef PICASSO96
	    if (screen_is_picasso) {
		currentmode->current_width = picasso_vidinfo.width;
		currentmode->current_height = picasso_vidinfo.height;
		currentmode->current_depth = rgbformat_bits (picasso_vidinfo.selected_rgbformat);
		currentmode->frequency = abs (currprefs.gfx_refreshrate > default_freq ? currprefs.gfx_refreshrate : default_freq);
	    } else {
#endif
		currentmode->current_width = currprefs.gfx_size.width;
		currentmode->current_height = currprefs.gfx_size.height;
		currentmode->current_depth = (currprefs.color_mode == 0 ? 8
				: currprefs.color_mode == 1 ? 15
				: currprefs.color_mode == 2 ? 16
				: currprefs.color_mode == 3 ? 8
				: currprefs.color_mode == 4 ? 8 : 32);
		currentmode->frequency = abs (currprefs.gfx_refreshrate);
#ifdef PICASSO96
	    }
#endif
	    currentmode->amiga_width = currentmode->current_width;
	    currentmode->amiga_height = currentmode->current_height;
	}
	ret = doInit ();
    } while (ret < 0);

    setpriority(&priorities[currprefs.win32_active_priority]);
    setmouseactive (1);
    for (i = 0; i < NUM_LEDS; i++)
	gui_led (i, 0);
    gui_fps (0, 0);

    return ret;
}

int check_prefs_changed_gfx (void)
{
    int c = 0;

    c |= currprefs.gfx_size_fs.width != changed_prefs.gfx_size_fs.width ? (2|8) : 0;
    c |= currprefs.gfx_size_fs.height != changed_prefs.gfx_size_fs.height ? (2|8) : 0;
    c |= currprefs.gfx_size_win.width != changed_prefs.gfx_size_win.width ? (2|8) : 0;
    c |= currprefs.gfx_size_win.height != changed_prefs.gfx_size_win.height ? (2|8) : 0;
    c |= currprefs.gfx_size_win.x != changed_prefs.gfx_size_win.x ? 16 : 0;
    c |= currprefs.gfx_size_win.y != changed_prefs.gfx_size_win.y ? 16 : 0;
    c |= currprefs.color_mode != changed_prefs.color_mode ? (2|8) : 0;
    c |= currprefs.gfx_afullscreen != changed_prefs.gfx_afullscreen ? (2|8) : 0;
    c |= currprefs.gfx_pfullscreen != changed_prefs.gfx_pfullscreen ? (2|8) : 0;
    c |= currprefs.gfx_vsync != changed_prefs.gfx_vsync ? (2|4|8) : 0;
    c |= currprefs.gfx_refreshrate != changed_prefs.gfx_refreshrate ? (2|4|8) : 0;
    c |= currprefs.gfx_autoresolution != changed_prefs.gfx_autoresolution ? (2|8) : 0;

    c |= currprefs.gfx_filter != changed_prefs.gfx_filter ? (2|8) : 0;
    c |= currprefs.gfx_filter_filtermode != changed_prefs.gfx_filter_filtermode ? (2|8) : 0;
    c |= currprefs.gfx_filter_horiz_zoom_mult != changed_prefs.gfx_filter_horiz_zoom_mult ? (1|8) : 0;
    c |= currprefs.gfx_filter_vert_zoom_mult != changed_prefs.gfx_filter_vert_zoom_mult ? (1|8) : 0;

    c |= currprefs.gfx_lores != changed_prefs.gfx_lores ? 1 : 0;
    c |= currprefs.gfx_linedbl != changed_prefs.gfx_linedbl ? 1 : 0;
    c |= currprefs.gfx_lores_mode != changed_prefs.gfx_lores_mode ? 1 : 0;
    c |= currprefs.gfx_display != changed_prefs.gfx_display ? (2|4|8) : 0;
    c |= currprefs.win32_alwaysontop != changed_prefs.win32_alwaysontop ? 1 : 0;
    c |= currprefs.win32_borderless != changed_prefs.win32_borderless ? 1 : 0;
    c |= currprefs.win32_no_overlay != changed_prefs.win32_no_overlay ? 2 : 0;

    if (display_change_requested || c) 
    {
	cfgfile_configuration_change(1);
	if (display_change_requested)
	    c |= 2;
	display_change_requested = 0;
	fixup_prefs_dimensions (&changed_prefs);
	currprefs.gfx_size_fs.width = changed_prefs.gfx_size_fs.width;
	currprefs.gfx_size_fs.height = changed_prefs.gfx_size_fs.height;
	currprefs.gfx_size_win.width = changed_prefs.gfx_size_win.width;
	currprefs.gfx_size_win.height = changed_prefs.gfx_size_win.height;
	currprefs.gfx_size_win.x = changed_prefs.gfx_size_win.x;
	currprefs.gfx_size_win.y = changed_prefs.gfx_size_win.y;
	currprefs.color_mode = changed_prefs.color_mode;
	currprefs.gfx_afullscreen = changed_prefs.gfx_afullscreen;
	currprefs.gfx_pfullscreen = changed_prefs.gfx_pfullscreen;
	updatewinfsmode (&currprefs);
	currprefs.gfx_vsync = changed_prefs.gfx_vsync;
	currprefs.gfx_refreshrate = changed_prefs.gfx_refreshrate;
	currprefs.gfx_autoresolution = changed_prefs.gfx_autoresolution;

	currprefs.gfx_filter = changed_prefs.gfx_filter;
	currprefs.gfx_filter_filtermode = changed_prefs.gfx_filter_filtermode;
	currprefs.gfx_filter_horiz_zoom_mult = changed_prefs.gfx_filter_horiz_zoom_mult;
	currprefs.gfx_filter_vert_zoom_mult = changed_prefs.gfx_filter_vert_zoom_mult;

	currprefs.gfx_lores_mode = changed_prefs.gfx_lores_mode;
	currprefs.gfx_lores = changed_prefs.gfx_lores;
	currprefs.gfx_linedbl = changed_prefs.gfx_linedbl;
	currprefs.gfx_display = changed_prefs.gfx_display;
	currprefs.win32_alwaysontop = changed_prefs.win32_alwaysontop;
	currprefs.win32_borderless = changed_prefs.win32_borderless;
	currprefs.win32_no_overlay = changed_prefs.win32_no_overlay;

        inputdevice_unacquire ();
	if (c & 2) {
	    close_windows ();
	    graphics_init ();
	}
        init_custom ();
	if (c & 4) {
	    pause_sound ();
	    resume_sound ();
	}
        inputdevice_acquire ();
	return 1;
    }

    if (currprefs.chipset_refreshrate != changed_prefs.chipset_refreshrate) {
	currprefs.chipset_refreshrate = changed_prefs.chipset_refreshrate;
	init_hz ();
	return 1;
    }

    if (currprefs.gfx_correct_aspect != changed_prefs.gfx_correct_aspect ||
	currprefs.gfx_xcenter != changed_prefs.gfx_xcenter ||
	currprefs.gfx_ycenter != changed_prefs.gfx_ycenter)
    {
	currprefs.gfx_correct_aspect = changed_prefs.gfx_correct_aspect;
	currprefs.gfx_xcenter = changed_prefs.gfx_xcenter;
	currprefs.gfx_ycenter = changed_prefs.gfx_ycenter;
	return 1;
    }

    if (currprefs.win32_norecyclebin != changed_prefs.win32_norecyclebin) {
	currprefs.win32_norecyclebin = changed_prefs.win32_norecyclebin;
    }

    if (currprefs.win32_logfile != changed_prefs.win32_logfile) {
	currprefs.win32_logfile = changed_prefs.win32_logfile;
	if (currprefs.win32_logfile)
	    logging_open(0, 1);
	else
	    logging_cleanup();
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
	inputdevice_acquire ();
#ifndef	_DEBUG
	setpriority (&priorities[currprefs.win32_active_priority]);
#endif
	return 1;
    }

    if (strcmp (currprefs.prtname, changed_prefs.prtname) ||
	currprefs.parallel_autoflush_time != changed_prefs.parallel_autoflush_time ||
	currprefs.parallel_postscript_emulation != changed_prefs.parallel_postscript_emulation ||
	currprefs.parallel_postscript_detection != changed_prefs.parallel_postscript_detection ||
	strcmp (currprefs.ghostscript_parameters, changed_prefs.ghostscript_parameters)) {
	strcpy (currprefs.prtname, changed_prefs.prtname);
	currprefs.parallel_autoflush_time = changed_prefs.parallel_autoflush_time;
	currprefs.parallel_postscript_emulation = changed_prefs.parallel_postscript_emulation;
	currprefs.parallel_postscript_detection = changed_prefs.parallel_postscript_detection;
	strcpy (currprefs.ghostscript_parameters, changed_prefs.ghostscript_parameters);
#ifdef PARALLEL_PORT
	closeprinter ();
#endif
    }
    if (strcmp (currprefs.sername, changed_prefs.sername) || 
	currprefs.serial_hwctsrts != changed_prefs.serial_hwctsrts ||
	currprefs.serial_direct != changed_prefs.serial_direct ||
	currprefs.serial_demand != changed_prefs.serial_demand) {
	strcpy (currprefs.sername, changed_prefs.sername);
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

    if (currprefs.win32_automount_drives != changed_prefs.win32_automount_drives ||
	currprefs.win32_powersavedisabled != changed_prefs.win32_powersavedisabled) {
	currprefs.win32_automount_drives = changed_prefs.win32_automount_drives;
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
    HRESULT ddrval;

    if (ncols256 == 0) {
	alloc_colors256 (get_color);
	memcpy (xcol8, xcolors, sizeof xcol8);
    }

    /* init colors */
    if (currentmode->flags & DM_OPENGL) {
#ifdef OPENGL
	OGL_getpixelformat (currentmode->current_depth,&red_bits,&green_bits,&blue_bits,&red_shift,&green_shift,&blue_shift,&alpha_bits,&alpha_shift,&alpha);
#endif
    } else if (currentmode->flags & DM_D3D) {
#ifdef D3D
	D3D_getpixelformat (currentmode->current_depth,&red_bits,&green_bits,&blue_bits,&red_shift,&green_shift,&blue_shift,&alpha_bits,&alpha_shift,&alpha);
#endif
    } else {
	switch( currentmode->current_depth >> 3)
	{
	    case 1:
		memcpy (xcolors, xcol8, sizeof xcolors);
		ddrval = DirectDraw_SetPaletteEntries( 0, 256, colors256 );
		if (FAILED(ddrval))
		    write_log ("DX_SetPalette() failed with %s/%d\n", DXError (ddrval), ddrval);
	    break;

	    case 2:
	    case 3:
	    case 4:
		red_bits = bits_in_mask( DirectDraw_GetPixelFormatBitMask( red_mask ) );
		green_bits = bits_in_mask( DirectDraw_GetPixelFormatBitMask( green_mask ) );
		blue_bits = bits_in_mask( DirectDraw_GetPixelFormatBitMask( blue_mask ) );
		red_shift = mask_shift( DirectDraw_GetPixelFormatBitMask( red_mask ) );
		green_shift = mask_shift( DirectDraw_GetPixelFormatBitMask( green_mask ) );
		blue_shift = mask_shift( DirectDraw_GetPixelFormatBitMask( blue_mask ) );
		alpha_bits = 0;
		alpha_shift = 0;
	    break;
	}
    }
    if (currentmode->current_depth > 8) {
	if (!(currentmode->flags & DM_OPENGL|DM_D3D)) {
	    if (currentmode->current_depth != currentmode->real_depth) {
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

void DX_SetPalette (int start, int count)
{
    HRESULT ddrval;

    if (!screen_is_picasso)
	return;

    if( picasso96_state.RGBFormat != RGBFB_CHUNKY )
	return;

    if (picasso_vidinfo.pixbytes != 1) 
    {
	/* write_log ("DX Setpalette emulation\n"); */
	/* This is the case when we're emulating a 256 color display.  */
	while (count-- > 0) 
	{
	    int r = picasso96_state.CLUT[start].Red;
	    int g = picasso96_state.CLUT[start].Green;
	    int b = picasso96_state.CLUT[start].Blue;
	    picasso_vidinfo.clut[start++] = (doMask256 (r, red_bits, red_shift)
		| doMask256 (g, green_bits, green_shift)
		| doMask256 (b, blue_bits, blue_shift));
	}
	notice_screen_contents_lost();
	return;
    }

    /* Set our DirectX palette here */
    if( currentmode->current_depth == 8 )
    {
	if (SUCCEEDED(DirectDraw_SetPalette(0))) {
	    ddrval = DirectDraw_SetPaletteEntries( start, count, (LPPALETTEENTRY)&(picasso96_state.CLUT[start] ) );
	    if (FAILED(ddrval))
		gui_message("DX_SetPalette() failed with %s/%d\n", DXError (ddrval), ddrval);
	}
    }
    else
    {
	write_log ("ERROR - DX_SetPalette() pixbytes %d\n", currentmode->current_depth >> 3 );
    }
}

void DX_Invalidate (int first, int last)
{
    p96_double_buffer_first = first;
    if(last >= picasso_vidinfo.height )
	last = picasso_vidinfo.height - 1;
    p96_double_buffer_last  = last;
    p96_double_buffer_needs_flushing = 1;
}

#endif

int DX_BitsPerCannon (void)
{
    return 8;
}

static COLORREF BuildColorRef( int color, RGBFTYPE pixelformat )
{
    COLORREF result;

    /* Do special case first */
    if( pixelformat == RGBFB_CHUNKY )
	result = color;
    else
	result = do_get_mem_long( &color );
    return result;
#if 0
    int r,g,b;
    write_log( "DX_Blit() called to fill with color of 0x%x, rgbtype of 0x%x\n", color, pixelformat );

    switch( pixelformat )
    {
	case RGBFB_R5G6B5PC:
	    r = color & 0xF800 >> 11;
	    g = color & 0x07E0 >> 5;
	    b = color & 0x001F;
	break;
	case RGBFB_R5G5B5PC:
	    r = color & 0x7C00 >> 10;
	    g = color & 0x03E0 >> 5;
	    b = color & 0x001F;
	break;
	case RGBFB_B5G6R5PC:
	    r = color & 0x001F;
	    g = color & 0x07E0 >> 5;
	    b = color & 0xF800 >> 11;
	break;
	case RGBFB_B5G5R5PC:
	    r = color & 0x001F;
	    g = color & 0x03E0 >> 5;
	    b = color & 0x7C00 >> 10;
	break;
	case RGBFB_B8G8R8:
	    r = color & 0x00FF0000 >> 16;
	    g = color & 0x0000FF00 >> 8;
	    b = color & 0x000000FF;
	break;
	case RGBFB_A8B8G8R8:
	    r = color & 0xFF000000 >> 24;
	    g = color & 0x00FF0000 >> 16;
	    b = color & 0x0000FF00 >> 8;
	break;
	case RGBFB_R8G8B8:
	    r = color & 0x000000FF;
	    g = color & 0x0000FF00 >> 8;
	    b = color & 0x00FF0000 >> 16;
	break;
	case RGBFB_A8R8G8B8:
	    r = color & 0x0000FF00 >> 8;
	    g = color & 0x00FF0000 >> 16;
	    b = color & 0xFF000000 >> 24;
	break;
	default:
	    write_log( "Uknown 0x%x pixel-format\n", pixelformat );
	break;
    }
    result = RGB(r,g,b);
    write_log( "R = 0x%02x, G = 0x%02x, B = 0x%02x - result = 0x%08x\n", r, g, b, result );
    return result;
#endif
}

/* This is a general purpose DirectDrawSurface filling routine.  It can fill within primary surface.
 * Definitions:
 * - primary is the displayed (visible) surface in VRAM, which may have an associated offscreen surface (or back-buffer)
 */
int DX_Fill(int dstx, int dsty, int width, int height, uae_u32 color, RGBFTYPE rgbtype)
{
    int result = 0;
    RECT dstrect;
    RECT srcrect;
    DDBLTFX ddbltfx;
    memset(&ddbltfx, 0, sizeof(ddbltfx));
    ddbltfx.dwFillColor = BuildColorRef(color, rgbtype);
    ddbltfx.dwSize = sizeof(ddbltfx);

    /* Set up our source rectangle.  This NEVER needs to be adjusted for windowed display, since the
     * source is ALWAYS in an offscreen buffer, or we're in full-screen mode. */
    SetRect(&srcrect, dstx, dsty, dstx+width, dsty+height);

    /* Set up our destination rectangle, and adjust for blit to windowed display (if necessary ) */
    SetRect(&dstrect, dstx, dsty, dstx+width, dsty+height);
    if(!(currentmode->flags & (DM_DX_FULLSCREEN | DM_OVERLAY)))
	OffsetRect(&dstrect, amigawin_rect.left, amigawin_rect.top);

    /* Render our fill to the visible (primary) surface */
    if((result = DirectDraw_Blt(primary_surface, &dstrect, invalid_surface, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx)))
    {
	if(DirectDraw_GetLockableType() == secondary_surface)
	{
	    /* We've colour-filled the visible, but still need to colour-fill the offscreen */
	    result = DirectDraw_Blt(secondary_surface, &srcrect, invalid_surface, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx);
	}
    }
    return result;
}

/* This is a general purpose DirectDrawSurface blitting routine.  It can blit within primary surface
 * Definitions:
 * - primary is the displayed (visible) surface in VRAM, which may have an associated offscreen surface (or back-buffer)
 */

static DDBLTFX fx = { sizeof(DDBLTFX) };

static DWORD BLIT_OPCODE_TRANSLATION[BLIT_LAST] =
{
    BLACKNESS,  /* BLIT_FALSE */
    NOTSRCERASE,/* BLIT_NOR */
    -1,         /* BLIT_ONLYDST NOT SUPPORTED */
    NOTSRCCOPY, /* BLIT_NOTSRC */
    SRCERASE,   /* BLIT_ONLYSRC */
    DSTINVERT,  /* BLIT_NOTDST */
    SRCINVERT,  /* BLIT_EOR */
    -1,         /* BLIT_NAND NOT SUPPORTED */
    SRCAND,     /* BLIT_AND */
    -1,         /* BLIT_NEOR NOT SUPPORTED */
    -1,         /* NO-OP */
    MERGEPAINT, /* BLIT_NOTONLYSRC */
    SRCCOPY,    /* BLIT_SRC */
    -1,         /* BLIT_NOTONLYDST NOT SUPPORTED */
    SRCPAINT,   /* BLIT_OR */
    WHITENESS   /* BLIT_TRUE */
};

int DX_Blit(int srcx, int srcy, int dstx, int dsty, int width, int height, BLIT_OPCODE opcode)
{
    HRESULT result;
    RECT dstrect, srcrect;
    DWORD dwROP = BLIT_OPCODE_TRANSLATION[opcode];

    if(dwROP == -1) {
	/* Unsupported blit opcode! */
	return 0;
    }
    fx.dwROP = dwROP;

    /* Set up our source rectangle.  This NEVER needs to be adjusted for windowed display, since the
     * source is ALWAYS in an offscreen buffer, or we're in full-screen mode. */
    SetRect(&srcrect, srcx, srcy, srcx+width, srcy+height);

    /* Set up our destination rectangle, and adjust for blit to windowed display (if necessary ) */
    SetRect(&dstrect, dstx, dsty, dstx+width, dsty+height);
    
    if(!(currentmode->flags & (DM_DX_FULLSCREEN | DM_OVERLAY)))
	OffsetRect(&dstrect, amigawin_rect.left, amigawin_rect.top);

    /* Render our blit within the primary surface */
    result = DirectDraw_Blt(primary_surface, &dstrect, DirectDraw_GetLockableType(), &srcrect, DDBLT_WAIT | DDBLT_ROP, &fx);
    if (FAILED(result)) {
	write_log("DX_Blit1() failed %s\n", DXError(result));
	return 0;
    } else if(DirectDraw_GetLockableType() == secondary_surface) {
	/* We've just blitted from the offscreen to the visible, but still need to blit from offscreen to offscreen
	 * NOTE: reset our destination rectangle again if its been modified above... */
	if((srcx != dstx) || (srcy != dsty)) {
		    SetRect(&dstrect, dstx, dsty, dstx+width, dsty+height);
	    result = DirectDraw_Blt(secondary_surface, &dstrect, secondary_surface, &srcrect, DDBLT_WAIT | DDBLT_ROP, &fx);
	    if (FAILED(result)) {
		write_log("DX_Blit2() failed %s\n", DXError(result));
	    }
	}
    }

    return 1;
}

void DX_WaitVerticalSync( void )
{
    DirectDraw_WaitForVerticalBlank (DDWAITVB_BLOCKBEGIN);
}

#if 0
uae_u32 DX_ShowCursor( uae_u32 activate )
{
    uae_u32 result = 0;
    if( ShowCursor( activate ) > 0 )
	result = 1;
    return result;
}
uae_u32 DX_MoveCursor( uae_u32 x, uae_u32 y )
{
    uae_u32 result = 0;

    // We may need to adjust the x,y values for our window-offset
    if(!(currentmode->flags & DM_DX_FULLSCREEN))
    {
	RECT rect;
	if( GetWindowRect( hAmigaWnd, &rect ) )
	{
	    x = rect.left + x;
	    y = rect.top + y;
	}
    }
    if( SetCursorPos( x, y ) )
	result = 1;
    return result;
}
#endif

static void open_screen( void )
{
    close_windows ();
    open_windows();
#ifdef PICASSO96
    DX_SetPalette (0, 256);
#endif
}

#ifdef PICASSO96
void gfx_set_picasso_state( int on )
{
    if (screen_is_picasso == on)
	return;
    screen_is_picasso = on;
    open_screen();
}

void gfx_set_picasso_modeinfo( uae_u32 w, uae_u32 h, uae_u32 depth, RGBFTYPE rgbfmt )
{
    depth >>= 3;
    if( ((unsigned)picasso_vidinfo.width == w ) &&
	    ( (unsigned)picasso_vidinfo.height == h ) &&
	    ( (unsigned)picasso_vidinfo.depth == depth ) &&
	    ( picasso_vidinfo.selected_rgbformat == rgbfmt) )
	return;

    picasso_vidinfo.selected_rgbformat = rgbfmt;
    picasso_vidinfo.width = w;
    picasso_vidinfo.height = h;
    picasso_vidinfo.depth = depth;
    picasso_vidinfo.extra_mem = 1;

    if( screen_is_picasso ) 
    {
	open_screen();
    }
}
#endif

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
    currentmode->amode[0] = &wmodes[currprefs.win32_no_overlay ? SM_WINDOW : SM_WINDOW_OVERLAY];
    currentmode->amode[1] = &wmodes[SM_FULLSCREEN_DX];
    currentmode->pmode[0] = &wmodes[currprefs.win32_no_overlay ? SM_WINDOW : SM_WINDOW_OVERLAY];
    currentmode->pmode[1] = &wmodes[SM_FULLSCREEN_DX];
#if defined (OPENGL) &&	defined	(GFXFILTER)
    if (usedfilter && usedfilter->type == UAE_FILTER_OPENGL) {
	currentmode->amode[0] = &wmodes[SM_OPENGL_WINDOW];
	currentmode->amode[1] = &wmodes[SM_OPENGL_FULLSCREEN_DX];
    }
#endif
#if defined (D3D) && defined (GFXFILTER)
    if (usedfilter && usedfilter->type == UAE_FILTER_DIRECT3D) {
	currentmode->amode[0] = &wmodes[SM_D3D_WINDOW];
	currentmode->amode[1] = &wmodes[SM_D3D_FULLSCREEN_DX];
    }
#endif
}

void machdep_init (void)
{
    picasso_requested_on = 0;
    picasso_on = 0;
    screen_is_picasso = 0;
    memset (currentmode, 0, sizeof (*currentmode));
#ifdef LOGITECHLCD
    lcd_open();
#endif
}

void machdep_free (void)
{
#ifdef LOGITECHLCD
    lcd_close();
#endif
}

int graphics_init (void)
{
    gfxmode_reset ();
    return open_windows ();
}

int graphics_setup (void)
{
    if (!DirectDraw_Start (NULL))
	return 0;
    DirectDraw_Release();
#ifdef PICASSO96
    InitPicasso96();
#endif
    return 1;
}

void graphics_leave (void)
{
    close_windows ();
}

uae_u32 OSDEP_minimize_uae( void )
{
    return ShowWindow (hAmigaWnd, SW_MINIMIZE);
}

void close_windows (void)
{
#if defined (GFXFILTER)
    S2X_free ();
#endif
    free (gfxvidinfo.realbufmem);
    gfxvidinfo.realbufmem = 0;
    DirectDraw_Release();
    close_hwnds();
}

void WIN32GFX_ToggleFullScreen( void )
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

    hStatusWnd = CreateWindowEx(
	0, STATUSCLASSNAME, (LPCTSTR) NULL, SBT_TOOLTIPS | WS_CHILD | WS_VISIBLE,
	0, 0, 0, 0, hMainWnd, (HMENU) 1, hInst, NULL);
    if (!hStatusWnd)
	return;

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
	lpParts[0] = rc.right - (drive_width * 4) - power_width - idle_width - fps_width - cd_width - hd_width - snd_width - 2;
	lpParts[1] = lpParts[0] + snd_width;
	lpParts[2] = lpParts[1] + idle_width;
	lpParts[3] = lpParts[2] + fps_width;
	lpParts[4] = lpParts[3] + power_width;
	lpParts[5] = lpParts[4] + cd_width;
	lpParts[6] = lpParts[5] + hd_width;
	lpParts[7] = lpParts[6] + drive_width;
	lpParts[8] = lpParts[7] + drive_width;
	lpParts[9] = lpParts[8] + drive_width;
	lpParts[10] = lpParts[9] + drive_width;
	window_led_drives = lpParts[6];
	window_led_drives_end = lpParts[10];

	/* Create the parts */
	SendMessage (hStatusWnd, SB_SETPARTS, (WPARAM) num_parts, (LPARAM) lpParts);
	LocalUnlock (hloc);
	LocalFree (hloc);
    }
}

static int create_windows (void)
{
    int dxfs = currentmode->flags & (DM_DX_FULLSCREEN | DM_D3D_FULLSCREEN);
    int fsw = currentmode->flags & (DM_W_FULLSCREEN);
    DWORD exstyle = currprefs.win32_notaskbarbutton ? 0 : WS_EX_APPWINDOW;
    DWORD flags = 0;
    HWND hhWnd = currprefs.win32_notaskbarbutton ? hHiddenWnd : NULL;
    int borderless = currprefs.win32_borderless;
    DWORD style = NORMAL_WINDOW_STYLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    int cymenu = GetSystemMetrics (SM_CYMENU);
    int cyborder = GetSystemMetrics (SM_CYBORDER);
    int cxborder = GetSystemMetrics(SM_CXBORDER);
    int gap = 3;
    int x, y;

    window_led_drives = 0;
    window_led_drives_end = 0;
    hMainWnd = NULL;
    x = 2; y = 2;
    if (borderless)
	cymenu = cyborder = cxborder = 0;

    if (!dxfs)  {
	RECT rc;
	LONG stored_x = 1, stored_y = cymenu + cyborder;
	DWORD regkeytype;
	DWORD regkeysize = sizeof (LONG);
	int oldx, oldy;
	int first = 2;

	RegQueryValueEx(hWinUAEKey, "xPos", 0, &regkeytype, (LPBYTE)&stored_x, &regkeysize);
	RegQueryValueEx(hWinUAEKey, "yPos", 0, &regkeytype, (LPBYTE)&stored_y, &regkeysize);

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
		write_log ("window coordinates are not visible on any monitor, reseting..\n");
		stored_x = stored_y = 0;
		continue;
	    }
	    break;
	}

	if (fsw) {
	    rc = Displays[currprefs.gfx_display].rect;
	    flags |= WS_EX_TOPMOST;
	    style = WS_POPUP;
	    currentmode->current_width = rc.right - rc.left;
	    currentmode->current_height = rc.bottom - rc.top;
	}

	flags |= (currprefs.win32_alwaysontop ? WS_EX_TOPMOST : 0);

	if (!borderless) {
	    hMainWnd = CreateWindowEx (WS_EX_ACCEPTFILES | exstyle | flags,
		"PCsuxRox", "WinUAE",
		style,
		rc.left, rc.top,
		rc.right - rc.left + 1, rc.bottom - rc.top + 1,
		hhWnd, NULL, 0, NULL);

	    if (!hMainWnd) {
		write_log ("main window creation failed\n");
		return 0;
	    }
	    if (!(currentmode->flags & DM_W_FULLSCREEN))
		createstatuswindow ();
	} else {
	    x = rc.left;
	    y = rc.top;
	}

    }

    hAmigaWnd = CreateWindowEx (dxfs ? WS_EX_ACCEPTFILES | WS_EX_TOPMOST : WS_EX_ACCEPTFILES | exstyle | (currprefs.win32_alwaysontop ? WS_EX_TOPMOST : 0),
				"AmigaPowah", "WinUAE",
				WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (hMainWnd ? WS_VISIBLE | WS_CHILD : WS_VISIBLE | WS_POPUP),
				x, y,
				currentmode->current_width, currentmode->current_height,
				hMainWnd ? hMainWnd : hhWnd, NULL, 0, NULL);

    if (!hAmigaWnd) {
	write_log ("creation of amiga window failed\n");
	close_hwnds();
	return 0;
    }

    systray (hMainWnd, FALSE);
    if (hMainWnd != hAmigaWnd) {
	ShowWindow (hMainWnd, SW_SHOWNORMAL);
	UpdateWindow (hMainWnd);
    }
    UpdateWindow (hAmigaWnd);
    ShowWindow (hAmigaWnd, SW_SHOWNORMAL);

    return 1;
}

static void updatemodes (void)
{
    if (screen_is_picasso) {
	currentmode->mode = currentmode->pmode[currprefs.gfx_pfullscreen];
	currentmode->flags = currentmode->mode->pflags;
    } else {
	currentmode->mode = currentmode->amode[currprefs.gfx_afullscreen];
	currentmode->flags = currentmode->mode->aflags;
    }
    currentmode->modeindex = currentmode->mode - &wmodes[0];

    currentmode->flags &= ~DM_SWSCALE;
#if defined (GFXFILTER)
    if (usedfilter && !usedfilter->x[0]) {
	currentmode->flags |= DM_SWSCALE;
	if (currentmode->current_depth < 15)
	    currentmode->current_depth = 16;
    }
#endif
}

static BOOL doInit (void)
{
    int fs_warning = -1;
    char tmpstr[300];
    RGBFTYPE colortype;
    int tmp_depth;
    int ret = 0;
    int mult = 0;

    colortype = DirectDraw_GetPixelFormat();
    gfxmode_reset ();

    for (;;) {
	updatemodes ();
	currentmode->real_depth = 0;
	tmp_depth = currentmode->current_depth;

	write_log("W=%d H=%d B=%d CT=%d\n",
	    DirectDraw_CurrentWidth (), DirectDraw_CurrentHeight (), DirectDraw_GetSurfaceBitCount (), colortype);

	if (currentmode->current_depth < 15 && (currprefs.chipset_mask & CSMASK_AGA) && isfullscreen () && !WIN32GFX_IsPicassoScreen()) {
	    static int warned;
	    if (!warned) {
		char szMessage[MAX_DPATH];
		currentmode->current_depth = 16;
		WIN32GUI_LoadUIString(IDS_AGA8BIT, szMessage, MAX_DPATH);
		gui_message(szMessage);
	    }
	    warned = 1;
	}

	if (!(currentmode->flags & DM_OVERLAY) && !isfullscreen() && !(currentmode->flags & (DM_OPENGL | DM_D3D))) {
	    write_log ("using desktop depth (%d -> %d) because not using overlay or opengl mode\n",
		currentmode->current_depth, DirectDraw_GetSurfaceBitCount());
	    currentmode->current_depth = DirectDraw_GetSurfaceBitCount();
	    updatemodes ();
	}

	//If screen depth is equal to the desired window_depth then no overlay is needed.
	if (!(currentmode->flags & (DM_OPENGL | DM_D3D)) && DirectDraw_GetSurfaceBitCount() == (unsigned)currentmode->current_depth) {
	    write_log ("ignored overlay because desktop depth == requested depth (%d)\n", currentmode->current_depth);
	    modefallback (DM_OVERLAY);
	    updatemodes ();
	}
    
	if (colortype == RGBFB_NONE && !(currentmode->flags & DM_OVERLAY)) {
	    fs_warning = IDS_UNSUPPORTEDSCREENMODE_1;
	} else if (colortype == RGBFB_CLUT && !(currentmode->flags & DM_OVERLAY)) {
	    fs_warning = IDS_UNSUPPORTEDSCREENMODE_2;
	} else if (currentmode->current_width >= GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
	    currentmode->current_height >= GetSystemMetrics(SM_CYVIRTUALSCREEN)) {
	    if (!console_logging)
		fs_warning = IDS_UNSUPPORTEDSCREENMODE_3;
#ifdef PICASSO96
	} else if (screen_is_picasso && !currprefs.gfx_pfullscreen &&
		  (picasso_vidinfo.selected_rgbformat != RGBFB_CHUNKY) &&
		  (picasso_vidinfo.selected_rgbformat != colortype) &&
		    !(currentmode->flags & DM_OVERLAY) )
	{
	    fs_warning = IDS_UNSUPPORTEDSCREENMODE_4;
#endif
	}
	if (fs_warning >= 0 && !isfullscreen ()) {
	    char szMessage[MAX_DPATH], szMessage2[MAX_DPATH];
	    WIN32GUI_LoadUIString(IDS_UNSUPPORTEDSCREENMODE, szMessage, MAX_DPATH);
	    WIN32GUI_LoadUIString(fs_warning, szMessage2, MAX_DPATH);
	    // Temporarily drop the DirectDraw stuff
	    DirectDraw_Release();
	    sprintf (tmpstr, szMessage, szMessage2);
	    gui_message (tmpstr);
	    DirectDraw_Start(displayGUID);
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
	    currentmode->pal = (LPPALETTEENTRY) & picasso96_state.CLUT;
	    if (! set_ddraw ()) {
		if (!modefallback (0))
		    goto oops;
		close_windows ();
		if (!DirectDraw_Start (displayGUID)) break;
		continue;
	    }
	    picasso_vidinfo.rowbytes = DirectDraw_GetSurfacePitch();
	    picasso_vidinfo.pixbytes = DirectDraw_GetBytesPerPixel();
	    picasso_vidinfo.rgbformat = DirectDraw_GetPixelFormat();
	    break;
	} else {
#endif
	    currentmode->pal = colors256;
	    if (! set_ddraw ()) {
		if (!modefallback (0))
		    goto oops;
		close_windows ();
		if (!DirectDraw_Start (displayGUID)) break;
		continue;
	    }
	    currentmode->real_depth = currentmode->current_depth;
#if defined (GFXFILTER)
	    if (currentmode->flags & (DM_OPENGL | DM_D3D | DM_SWSCALE)) {
		currentmode->amiga_width = AMIGA_WIDTH_MAX >> (currprefs.gfx_lores ? 1 : 0);
		currentmode->amiga_height = AMIGA_HEIGHT_MAX >> (currprefs.gfx_linedbl ? 0 : 1);
		if (usedfilter) {
		    if (usedfilter->x[0]) {
			currentmode->current_depth = (currprefs.gfx_filter_filtermode / 2) ? 32 : 16;
		    } else {
			int j = 0, i = currprefs.gfx_filter_filtermode;
			while (i >= 0) {
			    while (!usedfilter->x[j]) j++;
			    if(i-- > 0)
				j++;
			}
			if ((usedfilter->x[j] & (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) == (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) {
			    currentmode->current_depth = currentmode->real_depth;
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

    if ((currentmode->flags & DM_DDRAW) && !(currentmode->flags & (DM_D3D | DM_SWSCALE))) {
	int flags;
	if(!DirectDraw_SurfaceLock (lockable_surface))
	    goto oops;
	flags = DirectDraw_GetPixelFormatFlags();
	DirectDraw_SurfaceUnlock();
	if (flags  & (DDPF_RGB | DDPF_PALETTEINDEXED8 | DDPF_RGBTOYUV )) {
	    write_log( "%s mode (bits: %d, pixbytes: %d)\n", currentmode->flags & DM_DX_FULLSCREEN ? "Full screen" : "Window",
		   DirectDraw_GetSurfaceBitCount(), currentmode->current_depth >> 3 );
	} else {
	    char szMessage[MAX_DPATH];
	    WIN32GUI_LoadUIString (IDS_UNSUPPORTEDPIXELFORMAT, szMessage, MAX_DPATH);
	    gui_message( szMessage);
	    goto oops;
	}
    } else if (!(currentmode->flags & DM_SWSCALE)) {
	int size = currentmode->amiga_width * currentmode->amiga_height * gfxvidinfo.pixbytes;
	gfxvidinfo.realbufmem = xmalloc (size);
	gfxvidinfo.bufmem = gfxvidinfo.realbufmem;
	gfxvidinfo.rowbytes = currentmode->amiga_width * gfxvidinfo.pixbytes;
    } else if (!(currentmode->flags & DM_D3D)) {
	int size = (currentmode->amiga_width * 2) * (currentmode->amiga_height * 3) * gfxvidinfo.pixbytes;
	gfxvidinfo.realbufmem = xmalloc (size);
	memset (gfxvidinfo.realbufmem, 0, size);
	gfxvidinfo.bufmem = gfxvidinfo.realbufmem + (currentmode->amiga_width + (currentmode->amiga_width * 2) * currentmode->amiga_height) * gfxvidinfo.pixbytes;
	gfxvidinfo.rowbytes = currentmode->amiga_width * 2 * gfxvidinfo.pixbytes;
    }

    init_row_map ();
    init_colors ();

    if (currentmode->flags & DM_OVERLAY)
	setoverlay (0);

#if defined (GFXFILTER)
    if (currentmode->flags & DM_SWSCALE) {
	S2X_init (currentmode->current_width, currentmode->current_height,
	    currentmode->amiga_width, currentmode->amiga_height,
	    mult, currentmode->current_depth, currentmode->real_depth);
    }
#if defined OPENGL
    if (currentmode->flags & DM_OPENGL) {
	const char *err = OGL_init (hAmigaWnd, currentmode->current_width, currentmode->current_height,
	    currentmode->amiga_width, currentmode->amiga_height, currentmode->current_depth);
	if (err) {
	    OGL_free ();
	    if (err[0] != '*') {
		gui_message (err);
		changed_prefs.gfx_filter = currprefs.gfx_filter = 0;
	    }
	    currentmode->current_depth = currentmode->real_depth;
	    gfxmode_reset ();
	    ret = -1;
	    goto oops;
	}
    }
#endif
#ifdef D3D
    if (currentmode->flags & DM_D3D) {
	const char *err = D3D_init (hAmigaWnd, currentmode->current_width, currentmode->current_height,
	    currentmode->amiga_width, currentmode->amiga_height, currentmode->current_depth);
	if (err) {
	    D3D_free ();
	    gui_message (err);
	    changed_prefs.gfx_filter = currprefs.gfx_filter = 0;
	    currentmode->current_depth = currentmode->real_depth;
	    gfxmode_reset ();
	    ret = -1;
	    goto oops;
	}
    }
#endif
#endif
    screen_is_initialized = 1;
    return 1;

oops:
    close_hwnds();
    return ret;
}


void WIN32GFX_PaletteChange( void )
{
    HRESULT hr;

    if (!(currentmode->flags & DM_DDRAW) || (currentmode->flags & DM_D3D)) return;
    if (currentmode->current_depth > 8)
	return;
    hr = DirectDraw_SetPalette (1); /* Remove current palette */
    if (FAILED(hr))
	write_log ("SetPalette(1) failed, %s\n", DXError (hr));
    hr = DirectDraw_SetPalette (0); /* Set our real palette */
    if (FAILED(hr))
	write_log ("SetPalette(0) failed, %s\n", DXError (hr));
}

int WIN32GFX_ClearPalette( void )
{
    HRESULT hr;
    if (currentmode->current_depth > 8)
	return 1;
    if (!(currentmode->flags & DM_DDRAW) || (currentmode->flags & DM_D3D)) return 1;
    hr = DirectDraw_SetPalette (1); /* Remove palette */
    if (FAILED(hr))
	write_log ("SetPalette(1) failed, %s\n", DXError (hr));
    return SUCCEEDED(hr);
}

int WIN32GFX_SetPalette( void )
{
    HRESULT hr;
    if (!(currentmode->flags & DM_DDRAW) || (currentmode->flags & DM_D3D)) return 1;
    if (currentmode->current_depth > 8)
	return 1;
    hr = DirectDraw_SetPalette (0); /* Set palette */
    if (FAILED(hr))
	write_log ("SetPalette(0) failed, %s\n", DXError (hr));
    return SUCCEEDED(hr);
}
void WIN32GFX_WindowMove ( void	)
{
    if (currentmode->flags & DM_OVERLAY)
	setoverlay(0);
}

void updatedisplayarea (void)
{
    if (!screen_is_initialized)
	return;
    if (picasso_on)
	return;
    /* Update the display area */
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
	if (currentmode->flags & DM_SWSCALE) {
	    S2X_refresh ();
	    if(!isfullscreen()) {
		if(DirectDraw_GetLockableType() != overlay_surface)
		    DX_Blit(0, 0, 0, 0, WIN32GFX_GetWidth(), WIN32GFX_GetHeight(), BLIT_SRC);
	    } else {
		DirectDraw_Blt(primary_surface, NULL, secondary_surface, NULL, DDBLT_WAIT, NULL);
	    }
	}
	    else
#endif
	{
	    if (!isfullscreen()) {
		surface_type_e s;
		s = DirectDraw_GetLockableType();
		if (s != overlay_surface && s != invalid_surface)
		    DX_Blit(0, 0, 0, 0, WIN32GFX_GetWidth(), WIN32GFX_GetHeight(), BLIT_SRC);
	    } else {
		DirectDraw_Blt(primary_surface, NULL, secondary_surface, NULL, DDBLT_WAIT, NULL);
	    }
	}
    }
}

void updatewinfsmode (struct uae_prefs *p)
{
    int i;

    fixup_prefs_dimensions (p);
    if (p->gfx_afullscreen) {
	p->gfx_size = p->gfx_size_fs;
    } else {
	p->gfx_size = p->gfx_size_win;
    }
    displayGUID = NULL;
    i = 0;
    while (Displays[i].name) i++;
    if (p->gfx_display >= i)
	p->gfx_display = 0;
    if (Displays[p->gfx_display].disabled)
	p->gfx_display = 0;
    if (i == 0) {
	gui_message ("no display adapters! Exiting");
	exit (0);
    }
    if (!Displays[p->gfx_display].primary)
	displayGUID = &Displays[p->gfx_display].guid;
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
    if (OGL_isenabled())
	return OGL_getDC (0);
#endif
#ifdef D3D
    if (D3D_isenabled())
	return D3D_getDC (0);
#endif
    if(FAILED(DirectDraw_GetDC(&hdc, DirectDraw_GetLockableType())))
	hdc = 0;
    return hdc;
}

void releasehdc (HDC hdc)
{
#ifdef OPENGL
    if (OGL_isenabled()) {
	OGL_getDC (hdc);
	return;
    }
#endif
#ifdef D3D
    if (D3D_isenabled()) {
	D3D_getDC (hdc);
	return;
    }
#endif
    DirectDraw_ReleaseDC(hdc, DirectDraw_GetLockableType());
}
