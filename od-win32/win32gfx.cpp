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
#include "sampler.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#define AMIGA_WIDTH_MAX (752 / 2)
#define AMIGA_HEIGHT_MAX (574 / 2)

#define DM_DX_FULLSCREEN 1
#define DM_W_FULLSCREEN 2
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
static int scalepicasso;
static double remembered_vblank;

struct winuae_currentmode {
	unsigned int flags;
	int native_width, native_height, native_depth, pitch;
	int current_width, current_height, current_depth;
	int amiga_width, amiga_height;
	int frequency;
	int initdone;
	int fullfill;
	int vsync;
};

struct MultiDisplay Displays[MAX_DISPLAYS];
static GUID *displayGUID;

static struct winuae_currentmode currentmodestruct;
static int screen_is_initialized;
static int display_change_requested;
int window_led_drives, window_led_drives_end;
int window_led_hd, window_led_hd_end;
int window_led_joys, window_led_joys_end, window_led_joy_start;
extern int console_logging;
int window_extra_width, window_extra_height;

static struct winuae_currentmode *currentmode = &currentmodestruct;
static int wasfullwindow_a, wasfullwindow_p;
static int vblankbasewait, vblankbasefull;

int screen_is_picasso = 0;

extern int reopen (int);

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
		return p->gfx_pfullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_pfullscreen == GFX_FULLWINDOW ? -1 : 0);
	else
		return p->gfx_afullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_afullscreen == GFX_FULLWINDOW ? -1 : 0);
}
int isfullscreen (void)
{
	return isfullscreen_2 (&currprefs);
}

int is3dmode (void)
{
	return currentmode->flags & (DM_D3D);
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

static int init_round;
static BOOL doInit (void);

int default_freq = 0;

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
{HRESULT ddrval;
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
			if (freq > 0) {
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
				if (got == FALSE) {
					write_log (L"set_ddraw: refresh rate %d not supported\n", freq);
					freq = 0;
				}
			}
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
				md->DisplayModes[i].refreshtype[j] = nondx;
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
	md->DisplayModes[i].nondx = nondx;
	md->DisplayModes[i].res.width = w;
	md->DisplayModes[i].res.height = h;
	md->DisplayModes[i].depth = d;
	md->DisplayModes[i].refresh[0] = rate;
	md->DisplayModes[i].refreshtype[0] = nondx;
	md->DisplayModes[i].refresh[1] = 0;
	md->DisplayModes[i].colormodes = ct;
	md->DisplayModes[i + 1].depth = -1;
	_stprintf (md->DisplayModes[i].name, L"%dx%d, %d-bit",
		md->DisplayModes[i].res.width, md->DisplayModes[i].res.height, md->DisplayModes[i].depth * 8);
}

static HRESULT CALLBACK modesCallback (LPDDSURFACEDESC2 modeDesc, LPVOID context)
{
	struct MultiDisplay *md = (struct MultiDisplay*)context;
	RGBFTYPE colortype;
	int depth, ct;

	colortype = DirectDraw_GetSurfacePixelFormat (modeDesc);
	ct = 1 << colortype;
	depth = 0;
	if (ct & RGBMASK_8BIT)
		return DDENUMRET_OK;
	else if (ct & RGBMASK_15BIT)
		depth = 15;
	else if (ct & RGBMASK_16BIT)
		depth = 16;
	else if (ct & RGBMASK_24BIT)
		return DDENUMRET_OK;
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
	int	i, idx = -1;
	int pw = -1, ph = -1;

	i = 0;
	while (md->DisplayModes[i].depth >= 0)
		i++;
	qsort (md->DisplayModes, i, sizeof (struct PicassoResolution), resolution_compare);
	for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
		int j, k;
		for (j = 0; md->DisplayModes[i].refresh[j]; j++) {
			for (k = j + 1; md->DisplayModes[i].refresh[k]; k++) {
				if (md->DisplayModes[i].refresh[j] > md->DisplayModes[i].refresh[k]) {
					int t = md->DisplayModes[i].refresh[j];
					md->DisplayModes[i].refresh[j] = md->DisplayModes[i].refresh[k];
					md->DisplayModes[i].refresh[k] = t;
					t = md->DisplayModes[i].refreshtype[j];
					md->DisplayModes[i].refreshtype[j] = md->DisplayModes[i].refreshtype[k];
					md->DisplayModes[i].refreshtype[k] = t;
				}
			}
		}
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
		write_log (L"%d: %s%s (", i, md->DisplayModes[i].nondx ? L"!" : L"", md->DisplayModes[i].name);
		j = 0;
		while (md->DisplayModes[i].refresh[j] > 0) {
			if (j > 0)
				write_log (L",");
			if (md->DisplayModes[i].refreshtype[j])
				write_log (L"!%d", md->DisplayModes[i].refresh[j]);
			else
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
		md1->DisplayModes = xmalloc (struct PicassoResolution, MAX_PICASSO_MODES);
		md1->DisplayModes[0].depth = -1;
		md1->disabled = 1;
		int w = GetSystemMetrics (SM_CXSCREEN);
		int h = GetSystemMetrics (SM_CYSCREEN);
		HDC hdc = GetDC (NULL);
		int b = 0;
		
		if (hdc) {
			b = GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
			ReleaseDC (NULL, hdc);
		}

		int maxw = 0, maxh = 0;
		if (DirectDraw_Start (NULL)) {
			DirectDraw_EnumDisplayModes (DDEDM_REFRESHRATES, modesCallback, md1);
			idx2 = 0;
			while (md1->DisplayModes[idx2].depth >= 0) {
				struct PicassoResolution *pr = &md1->DisplayModes[idx2];
				if (pr->res.width > maxw)
					maxw = pr->res.width;
				if (pr->res.height > maxh)
					maxh = pr->res.height;
				idx2++;
			}
			DirectDraw_Release ();
		}
		write_log (L"Desktop: W=%d H=%d B=%d. MaxW=%d MaxH=%d CXVS=%d CYVS=%d\n", w, h, b, maxw, maxh,
			GetSystemMetrics (SM_CXVIRTUALSCREEN), GetSystemMetrics (SM_CYVIRTUALSCREEN));
		idx = 0;
		for (;;) {
			int found;
			DEVMODE dm;
			dm.dmSize = sizeof dm;
			dm.dmDriverExtra = 0;
			if (!EnumDisplaySettingsEx (md1->primary ? NULL : md1->name3, idx, &dm, EDS_RAWMODE))
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
			if (!found && dm.dmBitsPerPel > 8) {
				int freq = 0;
#if 0
				write_log (L"EnumDisplaySettings(%dx%dx%d %dHz %08x)\n",
					dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency, dm.dmFields);
#endif
				if (dm.dmFields & DM_DISPLAYFREQUENCY) {
					freq = dm.dmDisplayFrequency;
					if (freq < 10)
						freq = 0;
				}
				if (freq < 75 && dm.dmPelsWidth <= maxw && dm.dmPelsHeight <= maxh) {
					if ((dm.dmFields & DM_PELSWIDTH) && (dm.dmFields & DM_PELSHEIGHT) && (dm.dmFields & DM_BITSPERPEL))
						addmode (md1, dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, freq, 1);
				}
			}
			idx++;
		}
		//dhack();
		sortmodes (md1);
		modesList (md1);
		if (md1->DisplayModes[0].depth >= 0)
		md1->disabled = 0;
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
int WIN32GFX_AdjustScreenmode (struct MultiDisplay *md, int *pwidth, int *pheight, int *ppixbits)
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

static int flushymin, flushymax;
#define FLUSH_DIFF 50

static void flushit (int lineno)
{
	if (!currprefs.gfx_api)
		return;
	if (currentmode->flags & DM_SWSCALE)
		return;
	if (flushymin > lineno) {
		if (flushymin - lineno > FLUSH_DIFF && flushymax != 0) {
			D3D_flushtexture (flushymin, flushymax);
			flushymin = currentmode->amiga_height;
			flushymax = 0;
		} else {
			flushymin = lineno;
		}
	}
	if (flushymax < lineno) {
		if (lineno - flushymax > FLUSH_DIFF && flushymax != 0) {
			D3D_flushtexture (flushymin, flushymax);
			flushymin = currentmode->amiga_height;
			flushymax = 0;
		} else {
			flushymax = lineno;
		}
	}
}

void flush_line (int lineno)
{
	flushit (lineno);
}

void flush_block (int first, int last)
{
	flushit (first);
	flushit (last);
}

void flush_screen (int a, int b)
{
}

static bool render_ok;

bool render_screen (void)
{
	bool v = false;

	render_ok = false;
	if (dx_islost ())
		return render_ok;
	flushymin = 0;
	flushymax = currentmode->amiga_height;
	if (currentmode->flags & DM_D3D) {
		v = D3D_renderframe ();
#ifdef GFXFILTER
	} else if (currentmode->flags & DM_SWSCALE) {
		S2X_render ();
		v = true;
#endif
	} else if (currentmode->flags & DM_DDRAW) {
		v = true;
	}
	render_ok = v;
	return render_ok;
}

void show_screen (void)
{
	if (dx_islost ())
		return;
	if (!render_ok)
		return;
	if (currentmode->flags & DM_D3D) {
		D3D_showframe ();
#ifdef GFXFILTER
	} else if (currentmode->flags & DM_SWSCALE) {
		DirectDraw_Flip (1);
#endif
	} else if (currentmode->flags & DM_DDRAW) {
		DirectDraw_Flip (1);
	}
}

static uae_u8 *ddraw_dolock (void)
{
	if (!DirectDraw_SurfaceLock ()) {
		dx_check ();
		return 0;
	}
	gfxvidinfo.bufmem = DirectDraw_GetSurfacePointer ();
	gfxvidinfo.rowbytes = DirectDraw_GetSurfacePitch ();
	init_row_map ();
	clear_inhibit_frame (IHF_WINDOWHIDDEN);
	return gfxvidinfo.bufmem;
}

int lockscr (int fullupdate)
{
	int ret = 0;
	if (!isscreen ())
		return ret;
	flushymin = currentmode->amiga_height;
	flushymax = 0;
	ret = 1;
	if (currentmode->flags & DM_D3D) {
#ifdef D3D
		if (currentmode->flags & DM_SWSCALE) {
			ret = 1;
		} else {
			ret = 0;
			gfxvidinfo.bufmem = D3D_locktexture (&gfxvidinfo.rowbytes, fullupdate);
			if (gfxvidinfo.bufmem) {
				init_row_map ();
				ret = 1;
			}
		}
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
		if (currentmode->flags & DM_SWSCALE)
			S2X_render ();
		else
			D3D_flushtexture (flushymin, flushymax);
		D3D_unlocktexture ();
	} else if (currentmode->flags & DM_SWSCALE) {
		return;
	} else if (currentmode->flags & DM_DDRAW) {
		DirectDraw_SurfaceUnlock ();
	}
}

void flush_clear_screen (void)
{
	if (lockscr (true)) {
		int y;
		for (y = 0; y < gfxvidinfo.height; y++) {
			memset (gfxvidinfo.bufmem + y * gfxvidinfo.rowbytes, 0, gfxvidinfo.width * gfxvidinfo.pixbytes);
		}
		unlockscr ();
		flush_screen (0, 0);
	}
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
		int srcwidth, srcheight;

		if (scalepicasso < 0) {
			srcwidth = picasso96_state.Width;
			srcheight = picasso96_state.Height;
		} else {
			srcwidth = currentmode->native_width;
			srcheight = currentmode->native_height;
		}

		SetRect (&sr, 0, 0, picasso96_state.Width, picasso96_state.Height);
		if (currprefs.win32_rtgscaleaspectratio < 0) {
			// automatic
			srcratio = picasso96_state.Width * 256 / picasso96_state.Height;
			dstratio = srcwidth * 256 / srcheight;
		} else if (currprefs.win32_rtgscaleaspectratio == 0) {
			// none
			srcratio = dstratio = 0;
		} else {
			// manual
			srcratio = (currprefs.win32_rtgscaleaspectratio >> 8) * 256 / (currprefs.win32_rtgscaleaspectratio & 0xff);
			dstratio = srcwidth * 256 / srcheight;
		}
		if (srcratio == dstratio) {
			SetRect (&dr, 0, 0, srcwidth, srcheight);
		} else if (srcratio > dstratio) {
			int yy = srcheight - srcheight * dstratio / srcratio;
			SetRect (&dr, 0, yy / 2, srcwidth, srcheight - yy / 2);
			picasso_offset_y = yy / 2;
		} else {
			int xx = srcwidth - srcwidth * srcratio / dstratio;
			SetRect (&dr, xx / 2, 0,srcwidth - xx / 2, srcheight);
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

void getrtgfilterrect2 (RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height)
{
	SetRect (sr, 0, 0, currentmode->native_width, currentmode->native_height);
	SetRect (dr, 0, 0, picasso96_state.Width, picasso96_state.Height);
	SetRect (zr, 0, 0, 0, 0);
	
	picasso_offset_x = 0;
	picasso_offset_y = 0;
	picasso_offset_mx = 1000;
	picasso_offset_my = 1000;

	if (!scalepicasso)
		return;

	int srcratio, dstratio;
	int srcwidth, srcheight;
		srcwidth = picasso96_state.Width;
		srcheight = picasso96_state.Height;
	if (currprefs.win32_rtgscaleaspectratio < 0) {
		// automatic
		srcratio = picasso96_state.Width * 256 / picasso96_state.Height;
		dstratio = currentmode->native_width * 256 / currentmode->native_height;
	} else if (currprefs.win32_rtgscaleaspectratio == 0) {
		// none
		srcratio = dstratio = 0;
	} else {
		// manual
		dstratio = (currprefs.win32_rtgscaleaspectratio >> 8) * 256 / (currprefs.win32_rtgscaleaspectratio & 0xff);
		srcratio = srcwidth * 256 / srcheight;
	}
	if (srcratio == dstratio) {
		SetRect (dr, 0, 0, srcwidth, srcheight);
	} else if (srcratio > dstratio) {
		int yy = picasso96_state.Height * srcratio / dstratio;
		SetRect (dr, 0, 0, picasso96_state.Width, yy);
		picasso_offset_y = (picasso96_state.Height - yy) / 2;
	} else {
		int xx = picasso96_state.Width * dstratio / srcratio;
		SetRect (dr, 0, 0, xx, picasso96_state.Height);
		picasso_offset_x = (picasso96_state.Width - xx) / 2;
	}
	OffsetRect (zr, picasso_offset_x, picasso_offset_y);
	picasso_offset_mx = picasso96_state.Width * 1000 / (dr->right - dr->left);
	picasso_offset_my = picasso96_state.Height * 1000 / (dr->bottom - dr->top);
}

uae_u8 *gfx_lock_picasso (int fullupdate)
{
	if (currprefs.gfx_api) {
		int pitch;
		uae_u8 *p = D3D_locktexture (&pitch, fullupdate);
		picasso_vidinfo.rowbytes = pitch;
		return p;
	} else {
		if (!DirectDraw_SurfaceLock ()) {
			dx_check ();
			return 0;
		}
		picasso_vidinfo.rowbytes = DirectDraw_GetSurfacePitch ();
		return DirectDraw_GetSurfacePointer ();
	}
}

void gfx_unlock_picasso (void)
{
	if (currprefs.gfx_api) {
		if (p96_double_buffer_needs_flushing) {
			D3D_flushtexture (p96_double_buffer_first, p96_double_buffer_last);
			p96_double_buffer_needs_flushing = 0;
		}
		D3D_unlocktexture ();
		if (D3D_renderframe ())
			D3D_showframe ();
	} else {
		DirectDraw_SurfaceUnlock ();
		if (p96_double_buffer_needs_flushing) {
			DX_Blit96 (p96_double_buffer_firstx, p96_double_buffer_first,
				p96_double_buffer_lastx - p96_double_buffer_firstx + 1,
				p96_double_buffer_last - p96_double_buffer_first + 1);
			p96_double_buffer_needs_flushing = 0;
		}
	}
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
		addnotifications (hAmigaWnd, TRUE, FALSE);
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
		flags |= DM_SWSCALE;
		if (currentmode->current_depth < 15)
			currentmode->current_depth = 16;
	}
#endif
	if (currprefs.gfx_api) {
		flags |= DM_D3D;
		if (flags & DM_DX_FULLSCREEN) {
			flags &= ~DM_DX_FULLSCREEN;
			flags |= DM_D3D_FULLSCREEN;
		}
		flags &= ~DM_DDRAW;
	}
	currentmode->flags = flags;
	if (flags & DM_SWSCALE)
		currentmode->fullfill = 1;
	if (flags & DM_W_FULLSCREEN) {
		RECT rc = getdisplay (&currprefs)->rect;
		currentmode->native_width = rc.right - rc.left;
		currentmode->native_height = rc.bottom - rc.top;
		currentmode->current_width = currentmode->native_width;
		currentmode->current_height = currentmode->native_height;
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
			currentmode->vsync = 1 + currprefs.gfx_pvsyncmode;
	} else {
#endif
		currentmode->current_width = currprefs.gfx_size.width;
		currentmode->current_height = currprefs.gfx_size.height;
		currentmode->frequency = abs (currprefs.gfx_refreshrate);
		if (currprefs.gfx_avsync)
			currentmode->vsync = 1 + currprefs.gfx_avsyncmode;
#ifdef PICASSO96
	}
#endif
	currentmode->current_depth = currprefs.color_mode < 5 ? 16 : 32;
	if (screen_is_picasso && currprefs.win32_rtgmatchdepth && isfullscreen () > 0) {
		int pbits = picasso96_state.BytesPerPixel * 8;
		if (pbits <= 8) {
			if (currentmode->current_depth == 32)
				pbits = 32;
			else
				pbits = 16;
		}
		if (pbits == 24)
			pbits = 32;
		currentmode->current_depth = pbits;
	}
	currentmode->amiga_width = currentmode->current_width;
	currentmode->amiga_height = currentmode->current_height;

	scalepicasso = 0;
	if (screen_is_picasso) {
		if (isfullscreen () < 0) {
			if ((currprefs.win32_rtgscaleifsmall || currprefs.win32_rtgallowscaling) && (picasso96_state.Width != currentmode->native_width || picasso96_state.Height != currentmode->native_height))
				scalepicasso = 1;
			if (!scalepicasso && currprefs.win32_rtgscaleaspectratio)
				scalepicasso = -1;
		} else if (isfullscreen () > 0) {
			if (!currprefs.win32_rtgmatchdepth) { // can't scale to different color depth
				if (currentmode->native_width > picasso96_state.Width && currentmode->native_height > picasso96_state.Height) {
					if (currprefs.win32_rtgscaleifsmall)
						scalepicasso = 1;
				}
				if (!scalepicasso && currprefs.win32_rtgscaleaspectratio)
					scalepicasso = -1;
			}
		} else if (isfullscreen () == 0) {
			if ((currprefs.gfx_size.width != picasso96_state.Width || currprefs.gfx_size.height != picasso96_state.Height) && currprefs.win32_rtgallowscaling)
				scalepicasso = 1;
			if ((currprefs.gfx_size.width > picasso96_state.Width || currprefs.gfx_size.height > picasso96_state.Height) && currprefs.win32_rtgscaleifsmall)
				scalepicasso = 1;
			if (!scalepicasso && currprefs.win32_rtgscaleaspectratio)
				scalepicasso = -1;
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
	reset_sound ();
	in_sizemove = 0;

	updatewinfsmode (&currprefs);
#ifdef D3D
	D3D_free ();
#endif
#ifdef OPENGL
	OGL_free ();
#endif
	if (!DirectDraw_Start (displayGUID))
		return 0;
	write_log (L"DirectDraw GUID=%s\n", outGUID (displayGUID));

	init_round = 0;
	ret = -2;
	do {
		if (ret < -1) {
			updatemodes ();
			update_gfxparams ();
		}
		ret = doInit ();
		init_round++;
		if (ret < -9) {
			DirectDraw_Release ();
			if (!DirectDraw_Start (displayGUID))
				return 0;
		}
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

	if (!config_changed)
		return 0;

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
	c |= currprefs.gfx_avsyncmode != changed_prefs.gfx_avsyncmode ? 2 | 16 : 0;
	c |= currprefs.gfx_pvsyncmode != changed_prefs.gfx_pvsyncmode ? 2 | 16 : 0;
	c |= currprefs.gfx_refreshrate != changed_prefs.gfx_refreshrate ? 2 | 16 : 0;
	c |= currprefs.gfx_autoresolution != changed_prefs.gfx_autoresolution ? (2|8) : 0;
	c |= currprefs.gfx_api != changed_prefs.gfx_api ? (1|8|32) : 0;

	c |= currprefs.gfx_filter != changed_prefs.gfx_filter ? (2|8) : 0;
	c |= _tcscmp (currprefs.gfx_filtershader, changed_prefs.gfx_filtershader) ? (2|8) : 0;
	c |= _tcscmp (currprefs.gfx_filtermask, changed_prefs.gfx_filtermask) ? (2|8) : 0;
	c |= _tcscmp (currprefs.gfx_filteroverlay, changed_prefs.gfx_filteroverlay) ? (2|8) : 0;
	c |= currprefs.gfx_filter_filtermode != changed_prefs.gfx_filter_filtermode ? (2|8) : 0;
	c |= currprefs.gfx_filter_bilinear != changed_prefs.gfx_filter_bilinear ? (2|8) : 0;
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

	c |= currprefs.gfx_resolution != changed_prefs.gfx_resolution ? (128) : 0;
	c |= currprefs.gfx_vresolution != changed_prefs.gfx_vresolution ? (128) : 0;
	c |= currprefs.gfx_scanlines != changed_prefs.gfx_scanlines ? (2 | 8) : 0;

	c |= currprefs.gfx_lores_mode != changed_prefs.gfx_lores_mode ? (2 | 8) : 0;
	c |= currprefs.gfx_scandoubler != changed_prefs.gfx_scandoubler ? (2 | 8) : 0;
	c |= currprefs.gfx_display != changed_prefs.gfx_display ? (2|4|8) : 0;
	c |= _tcscmp (currprefs.gfx_display_name, changed_prefs.gfx_display_name) ? (2|4|8) : 0;
	c |= currprefs.gfx_blackerthanblack != changed_prefs.gfx_blackerthanblack ? (2 | 8) : 0;
	c |= currprefs.gfx_backbuffers != changed_prefs.gfx_backbuffers ? (2 | 8) : 0;

	c |= currprefs.win32_alwaysontop != changed_prefs.win32_alwaysontop ? 32 : 0;
	c |= currprefs.win32_notaskbarbutton != changed_prefs.win32_notaskbarbutton ? 32 : 0;
	c |= currprefs.win32_borderless != changed_prefs.win32_borderless ? 32 : 0;
	c |= currprefs.win32_statusbar != changed_prefs.win32_statusbar ? 32 : 0;
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
		currprefs.gfx_api = changed_prefs.gfx_api;

		if (changed_prefs.gfx_afullscreen == GFX_FULLSCREEN) { 
			if (currprefs.gfx_api != changed_prefs.gfx_api)
				display_change_requested = 1;
		}

		if (display_change_requested) {
			c = 2;
			keepfsmode = 0;
			display_change_requested = 0;
		}

		currprefs.gfx_filter = changed_prefs.gfx_filter;
		_tcscpy (currprefs.gfx_filtershader, changed_prefs.gfx_filtershader);
		_tcscpy (currprefs.gfx_filtermask, changed_prefs.gfx_filtermask);
		_tcscpy (currprefs.gfx_filteroverlay, changed_prefs.gfx_filteroverlay);
		currprefs.gfx_filter_filtermode = changed_prefs.gfx_filter_filtermode;
		currprefs.gfx_filter_bilinear = changed_prefs.gfx_filter_bilinear;
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

		currprefs.gfx_resolution = changed_prefs.gfx_resolution;
		currprefs.gfx_vresolution = changed_prefs.gfx_vresolution;
		currprefs.gfx_scanlines = changed_prefs.gfx_scanlines;

		currprefs.gfx_lores_mode = changed_prefs.gfx_lores_mode;
		currprefs.gfx_scandoubler = changed_prefs.gfx_scandoubler;
		currprefs.gfx_display = changed_prefs.gfx_display;
		_tcscpy (currprefs.gfx_display_name, changed_prefs.gfx_display_name);
		currprefs.gfx_blackerthanblack = changed_prefs.gfx_blackerthanblack;
		currprefs.gfx_backbuffers = changed_prefs.gfx_backbuffers;

		currprefs.win32_alwaysontop = changed_prefs.win32_alwaysontop;
		currprefs.win32_notaskbarbutton = changed_prefs.win32_notaskbarbutton;
		currprefs.win32_borderless = changed_prefs.win32_borderless;
		currprefs.win32_statusbar = changed_prefs.win32_statusbar;
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
		if (c & 128) {
			if (currprefs.gfx_autoresolution) {
				c |= 2 | 8;
			} else {
				drawing_init ();
				S2X_reset ();
			}
		}
		if ((c & 16) || ((c & 8) && keepfsmode)) {
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

	bool changed = false;
	for (int i = 0; i < MAX_CHIPSET_REFRESH_TOTAL; i++) {
		if (currprefs.cr[i].rate != changed_prefs.cr[i].rate ||
			currprefs.cr[i].locked != changed_prefs.cr[i].locked) {
				memcpy (&currprefs.cr[i], &changed_prefs.cr[i], sizeof (struct chipset_refresh));
				changed = true;
		}
	}
	if (changed) {
		init_hz_full ();
	}
	if (currprefs.chipset_refreshrate != changed_prefs.chipset_refreshrate) {
		currprefs.chipset_refreshrate = changed_prefs.chipset_refreshrate;
		init_hz_full ();
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
		currprefs.win32_minimize_inactive != changed_prefs.win32_minimize_inactive ||
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
		currprefs.win32_minimize_inactive = changed_prefs.win32_minimize_inactive;
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
		currprefs.keyboard_leds_in_use = (currprefs.keyboard_leds[0] | currprefs.keyboard_leds[1] | currprefs.keyboard_leds[2]) != 0;
		pause_sound ();
		resume_sound ();
		inputdevice_acquire (TRUE);
#ifndef	_DEBUG
		setpriority (&priorities[currprefs.win32_active_priority]);
#endif
		return 1;
	}

	if (currprefs.win32_samplersoundcard != changed_prefs.win32_samplersoundcard) {
		currprefs.win32_samplersoundcard = changed_prefs.win32_samplersoundcard;
		sampler_free ();
	}

	if (_tcscmp (currprefs.prtname, changed_prefs.prtname) ||
		currprefs.parallel_autoflush_time != changed_prefs.parallel_autoflush_time ||
		currprefs.parallel_matrix_emulation != changed_prefs.parallel_matrix_emulation ||
		currprefs.parallel_postscript_emulation != changed_prefs.parallel_postscript_emulation ||
		currprefs.parallel_postscript_detection != changed_prefs.parallel_postscript_detection ||
		_tcscmp (currprefs.ghostscript_parameters, changed_prefs.ghostscript_parameters)) {
			_tcscpy (currprefs.prtname, changed_prefs.prtname);
			currprefs.parallel_autoflush_time = changed_prefs.parallel_autoflush_time;
			currprefs.parallel_matrix_emulation = changed_prefs.parallel_matrix_emulation;
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

static int red_bits, green_bits, blue_bits, alpha_bits;
static int red_shift, green_shift, blue_shift, alpha_shift;
static int alpha;

void init_colors (void)
{
	/* init colors */
	switch(currentmode->current_depth / 8)
	{
	case 1:
		break;
	case 2:
	case 3:
	case 4:
		red_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (red_mask));
		green_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (green_mask));
		blue_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (blue_mask));
		red_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (red_mask));
		green_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (green_mask));
		blue_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (blue_mask));
		alpha_bits = 0;
		alpha_shift = 0;
		break;
	}

	if (currentmode->flags & DM_D3D) {
#ifdef D3D
		D3D_getpixelformat (currentmode->current_depth,
			&red_bits, &green_bits, &blue_bits, &red_shift, &green_shift, &blue_shift, &alpha_bits, &alpha_shift, &alpha);
#endif
	}

	if (!(currentmode->flags & (DM_D3D))) {
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
	Screenshot_RGBinfo (red_bits, green_bits, blue_bits, red_shift, green_shift, blue_shift);
}

#ifdef PICASSO96

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
			| doMask256 (b, blue_bits, blue_shift))
			| doMask256 (0xff, alpha_bits, alpha_shift);
		if (v != picasso_vidinfo.clut[i]) {
			//write_log (L"%d:%08x\n", i, v);
			picasso_vidinfo.clut[i] = v;
			changed = 1;
		}
	}
	return changed;
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
}

static int ifs (struct uae_prefs *p)
{
	if (screen_is_picasso)
		return p->gfx_pfullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_pfullscreen == GFX_FULLWINDOW ? -1 : 0);
	else
		return p->gfx_afullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_afullscreen == GFX_FULLWINDOW ? -1 : 0);
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
		currprefs.gfx_pfullscreen == changed_prefs.gfx_pfullscreen && currprefs.gfx_afullscreen == GFX_FULLSCREEN) {
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
	currprefs.gfx_avsyncmode = changed_prefs.gfx_avsyncmode;
	currprefs.gfx_pvsyncmode = changed_prefs.gfx_pvsyncmode;
	currprefs.gfx_refreshrate = changed_prefs.gfx_refreshrate;
	config_changed = 1;

	if (!quick)
		return 1;

	open_windows (0);

	if (isfullscreen () <= 0)
		DirectDraw_FillPrimary ();

	return 0;
}

bool vsync_switchmode (int hz)
{
	static struct PicassoResolution *oldmode;
	static int oldhz;
	int w = currentmode->native_width;
	int h = currentmode->native_height;
	int d = currentmode->native_depth / 8;
	struct MultiDisplay *md = getdisplay (&currprefs);
	struct PicassoResolution *found;
	int newh, i, cnt;

	newh = h * (currprefs.ntscmode ? 60 : 50) / hz;

	found = NULL;
	for (cnt = 0; cnt <= abs (newh - h) + 1 && !found; cnt++) {
		for (i = 0; md->DisplayModes[i].depth >= 0 && !found; i++) {
			struct PicassoResolution *r = &md->DisplayModes[i];
			if (r->res.width == w && (r->res.height == newh + cnt || r->res.height == newh - cnt) && r->depth == d) {
				int j;
				for (j = 0; r->refresh[j] > 0; j++) {
					if (r->refresh[j] == hz || r->refresh[j] == hz * 2) {
						found = r;
						hz = r->refresh[j];
						break;
					}
				}
			}
		}
	}
	if (found == oldmode && hz == oldhz)
		return true;
	oldmode = found;
	oldhz = hz;
	if (!found) {
		changed_prefs.gfx_avsync = 0;
		if (currprefs.gfx_avsync != changed_prefs.gfx_avsync) {
			config_changed = 1;
		}
		write_log (L"refresh rate changed to %d but no matching screenmode found, vsync disabled\n", hz);
		return false;
	} else {
		newh = found->res.height;
		changed_prefs.gfx_size_fs.height = newh;
		changed_prefs.gfx_refreshrate = hz;
		if (changed_prefs.gfx_size_fs.height != currprefs.gfx_size_fs.height ||
			changed_prefs.gfx_refreshrate != currprefs.gfx_refreshrate) {
			write_log (L"refresh rate changed to %d, new screenmode %dx%d\n", hz, w, newh);
			config_changed = 1;
		}
		return true;
	}
}

#ifdef PICASSO96

static int modeswitchneeded (struct winuae_currentmode *wc)
{
	if (isfullscreen () > 0) {
		/* fullscreen to fullscreen */
		if (screen_is_picasso) {
			if (picasso96_state.BytesPerPixel > 1 && picasso96_state.BytesPerPixel * 8 != wc->current_depth && currprefs.win32_rtgmatchdepth)
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
					if (picasso96_state.BytesPerPixel * 8 == wc->current_depth || picasso96_state.BytesPerPixel == 1)
						return 0;
					if (!currprefs.win32_rtgmatchdepth)
						return 0;
			}
			return 1;
		} else {
			if (currentmode->current_width != wc->current_width ||
				currentmode->current_height != wc->current_height ||
				currentmode->current_depth != wc->current_depth)
				return -1;
			if (!gfxvidinfo.bufmem_allocated)
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
	if (currprefs.gfx_afullscreen != currprefs.gfx_pfullscreen || (currprefs.gfx_afullscreen == GFX_FULLSCREEN && currprefs.gfx_api)) {
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
	alloc_colors_picasso (red_bits, green_bits, blue_bits, red_shift, green_shift, blue_shift, rgbfmt);
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
	lcd_open ();
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
	gfxvidinfo.bufmem_allocated = false;
	gfxvidinfo.realbufmem = NULL;
	DirectDraw_Release ();
	close_hwnds ();
}

static void createstatuswindow (void)
{
	HDC hdc;
	RECT rc;
	HLOCAL hloc;
	LPINT lpParts;
	int drive_width, hd_width, cd_width, power_width, fps_width, idle_width, snd_width, joy_width;
	int joys = currprefs.win32_statusbar > 1 ? 2 : 0;
	int num_parts = 11 + joys;
	double scaleX, scaleY;
	WINDOWINFO wi;
	int extra;

	if (hStatusWnd) {
		ShowWindow (hStatusWnd, SW_HIDE);
		DestroyWindow (hStatusWnd);
	}
	if (currprefs.win32_statusbar == 0)
		return;
	hStatusWnd = CreateWindowEx (
		0, STATUSCLASSNAME, (LPCTSTR) NULL, SBARS_TOOLTIPS | WS_CHILD | WS_VISIBLE,
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
	snd_width = (int)(72 * scaleX);
	joy_width = (int)(24 * scaleX);
	GetClientRect (hMainWnd, &rc);
	/* Allocate an array for holding the right edge coordinates. */
	hloc = LocalAlloc (LHND, sizeof (int) * (num_parts + 1));
	if (hloc) {
		int i = 0, i1, j;
		lpParts = (LPINT)LocalLock (hloc);
		/* Calculate the right edge coordinate for each part, and copy the coords to the array.  */
		int startx = rc.right - (drive_width * 4) - power_width - idle_width - fps_width - cd_width - hd_width - snd_width - joys * joy_width - extra;
		for (j = 0; j < joys; j++) {
			lpParts[i] = startx;
			i++;
			startx += joy_width;
		}
		window_led_joy_start = i;
		// snd
		lpParts[i] = startx;
		i++;
		// cpu
		lpParts[i] = lpParts[i - 1] + snd_width;
		i++;
		// fps
		lpParts[i] = lpParts[i - 1] + idle_width;
		i++;
		// power
		lpParts[i] = lpParts[i - 1] + fps_width;
		i++;
		i1 = i;
		// hd
		lpParts[i] = lpParts[i - 1] + power_width;
		i++;
		// cd
		lpParts[i] = lpParts[i - 1] + hd_width;
		i++;
		// df0
		lpParts[i] = lpParts[i - 1] + cd_width;
		i++;
		// df1
		lpParts[i] = lpParts[i - 1] + drive_width;
		i++;
		// df2
		lpParts[i] = lpParts[i - 1] + drive_width;
		i++;
		// df3
		lpParts[i] = lpParts[i - 1] + drive_width;
		i++;
		// edge
		lpParts[i] = lpParts[i - 1] + drive_width;

		window_led_joys = lpParts[0];
		window_led_joys_end = lpParts[1];
		window_led_hd = lpParts[i1];
		window_led_hd_end = lpParts[i1 + 1];
		window_led_drives = lpParts[i1 + 2];
		window_led_drives_end = lpParts[i1 + 6];

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
	int i, startidx, disp;
	struct MultiDisplay *md = getdisplay (&currprefs);
	int ratio;

	ratio = currentmode->native_width > currentmode->native_height ? 1 : 0;
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
	// first iterate only modes that have similar aspect ratio
	startidx = i;
	for (; md->DisplayModes[i].depth >= 0; i++) {
		struct PicassoResolution *pr = &md->DisplayModes[i];
		int r = pr->res.width > pr->res.height ? 1 : 0;
		if (pr->res.width >= currentmode->native_width && pr->res.height >= currentmode->native_height && r == ratio) {
			write_log (L"FS: %dx%d -> %dx%d (%d)\n", currentmode->native_width, currentmode->native_height,
				pr->res.width, pr->res.height, ratio);
			currentmode->native_width = pr->res.width;
			currentmode->native_height = pr->res.height;
			currentmode->current_width = currentmode->native_width;
			currentmode->current_height = currentmode->native_height;
			return 1;
		}
	}
	// still not match? check all modes
	i = startidx;
	for (; md->DisplayModes[i].depth >= 0; i++) {
		struct PicassoResolution *pr = &md->DisplayModes[i];
		int r = pr->res.width > pr->res.height ? 1 : 0;
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


bool waitvblankstate (bool state)
{
	if (currprefs.gfx_api) {
		return D3D_waitvblankstate (state);
	} else {
		return DirectDraw_waitvblankstate (state);
	}
}

double getcurrentvblankrate (void)
{
	if (remembered_vblank)
		return remembered_vblank;
	if (currprefs.gfx_api)
		return D3D_getrefreshrate ();
	else
		return DirectDraw_CurrentRefreshRate ();
}

#include <process.h>
static volatile int dummythread_die;
int dummy_counter;
static void _cdecl dummythread (void *dummy)
{
	SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_LOWEST);
	while (!dummythread_die)
		dummy_counter++;
}

double vblank_calibrate (double approx_vblank, bool waitonly)
{
	frame_time_t t1, t2;
	double tsum, tsum2, tval, tfirst;
	int maxcnt, maxtotal, total, cnt, tcnt2;
	HANDLE th;
	
	if (remembered_vblank > 0)
		return remembered_vblank;
	if (waitonly) {
		vblankbasefull = syncbase / approx_vblank;
		vblankbasewait = (syncbase / approx_vblank) * 3 / 4;
		remembered_vblank = -1;
		return -1;
	}
	th = GetCurrentThread ();
	int oldpri = GetThreadPriority (th);
	SetThreadPriority (th, THREAD_PRIORITY_HIGHEST);
	dummythread_die = -1;
	dummy_counter = 0;
	_beginthread (&dummythread, 0, 0);
	sleep_millis (100);
	maxtotal = 10;
	maxcnt = maxtotal;
	tsum2 = 0;
	tcnt2 = 0;
	for (maxcnt = 0; maxcnt < maxtotal; maxcnt++) {
		total = 10;
		tsum = 0;
		cnt = total;
		for (cnt = 0; cnt < total; cnt++) {
			if (!waitvblankstate (true))
				return -1;
			if (!waitvblankstate (false))
				return -1;
			if (!waitvblankstate (true))
				return -1;
			t1 = read_processor_time ();
			if (!waitvblankstate (false))
				return -1;
			if (!waitvblankstate (true))
				return -1;
			t2 = read_processor_time ();
			tval = (double)syncbase / (t2 - t1);
			if (cnt == 0)
				tfirst = tval;
			if (abs (tval - tfirst) > 1) {
				write_log (L"very unstable vsync! %.6f vs %.6f, retrying..\n", tval, tfirst);
				break;
			}
			tsum2 += tval;
			tcnt2++;
			if (abs (tval - tfirst) > 0.1) {
				write_log (L"unstable vsync! %.6f vs %.6f\n", tval, tfirst);
				break;
			}
			tsum += tval;
		}
		if (cnt >= total)
			break;
	}
	dummythread_die = 0;
	SetThreadPriority (th, oldpri);
	if (maxcnt >= maxtotal) {
		tsum = tsum2 / tcnt2;
		write_log (L"unstable vsync reporting, using average value\n");
	} else {
		tsum /= total;
	}
	if (tsum >= 85)
		tsum /= 2;
	vblankbasefull = (syncbase / tsum);
	vblankbasewait = (syncbase / tsum) * 3 / 4;
	write_log (L"VSync calibration: %.6fHz\n", tsum);
	remembered_vblank = tsum;
	return tsum;
}

bool vsync_busywait (void)
{
	bool v;
	static frame_time_t prevtime;
	static bool framelost;

	if (currprefs.turbo_emulation)
		return true;

	if (!framelost && read_processor_time () - prevtime > vblankbasefull) {
		framelost = true;
		prevtime = read_processor_time ();
		return true;
	}
	if (framelost) {
		framelost = false;
		prevtime = read_processor_time ();
		return true;
	}

	while (read_processor_time () - prevtime < vblankbasewait)
		sleep_millis (1);
	v = false;
	if (currprefs.gfx_api) {
		v = D3D_vblank_busywait ();
	} else {
		v = DirectDraw_vblank_busywait ();
	}
	if (v) {
		prevtime = read_processor_time ();
		return true;
	}
	return false;
}

static int create_windows_2 (void)
{
	static int firstwindow = 1;
	int dxfs = currentmode->flags & (DM_DX_FULLSCREEN);
	int d3dfs = currentmode->flags & (DM_D3D_FULLSCREEN);
	int fsw = currentmode->flags & (DM_W_FULLSCREEN);
	DWORD exstyle = currprefs.win32_notaskbarbutton ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
	DWORD flags = 0;
	int borderless = currprefs.win32_borderless;
	DWORD style = NORMAL_WINDOW_STYLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	int cymenu = currprefs.win32_statusbar == 0 ? 0 : GetSystemMetrics (SM_CYMENU);
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
		GetWindowRect (hMainWnd, &mainwin_rect);
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
		int stored_x = 1, stored_y = cymenu + cyborder;
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
				NULL, NULL, hInst, NULL);
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
		hAmigaWnd = CreateWindowEx (
			((dxfs || d3dfs || currprefs.win32_alwaysontop) ? WS_EX_TOPMOST : WS_EX_ACCEPTFILES) | exstyle,
			L"AmigaPowah", L"WinUAE",
			((dxfs || d3dfs || currprefs.headless) ? WS_POPUP : (WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (hMainWnd ? WS_VISIBLE | WS_CHILD : WS_VISIBLE | WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX))),
			x, y, w, h,
			borderless ? NULL : (hMainWnd ? hMainWnd : NULL),
			NULL, hInst, NULL);
	}
	if (!hAmigaWnd) {
		write_log (L"creation of amiga window failed\n");
		close_hwnds ();
		return 0;
	}
	if (hMainWnd == NULL)
		hMainWnd = hAmigaWnd;
	GetWindowRect (hAmigaWnd, &amigawin_rect);
	GetWindowRect (hMainWnd, &mainwin_rect);
	if (dxfs || d3dfs)
		SetCursorPos (x + w / 2, y + h / 2);
	addnotifications (hAmigaWnd, FALSE, FALSE);
	if (hMainWnd != hAmigaWnd) {
		if (!currprefs.headless && !rp_isactive ())
			ShowWindow (hMainWnd, firstwindow ? SW_SHOWDEFAULT : SW_SHOWNORMAL);
		UpdateWindow (hMainWnd);
	}
	if (!currprefs.headless && !rp_isactive ())
		ShowWindow (hAmigaWnd, firstwindow ? SW_SHOWDEFAULT : SW_SHOWNORMAL);
	UpdateWindow (hAmigaWnd);
	firstwindow = 0;

	return 1;
}

static int set_ddraw (void)
{
	int cnt, ret;

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

	remembered_vblank = 0;
	if (wasfullwindow_a == 0)
		wasfullwindow_a = currprefs.gfx_afullscreen == GFX_FULLWINDOW ? 1 : -1;
	if (wasfullwindow_p == 0)
		wasfullwindow_p = currprefs.gfx_pfullscreen == GFX_FULLWINDOW ? 1 : -1;
	colortype = DirectDraw_GetPixelFormat ();
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

		if (isfullscreen() <= 0 && !(currentmode->flags & (DM_D3D))) {
			currentmode->current_depth = DirectDraw_GetCurrentDepth ();
			updatemodes ();
		}
		if (!(currentmode->flags & (DM_D3D)) && DirectDraw_GetCurrentDepth () == currentmode->current_depth) {
			updatemodes ();
		}

		if (colortype == RGBFB_NONE) {
			fs_warning = IDS_UNSUPPORTEDSCREENMODE_1;
		} else if (colortype == RGBFB_CLUT) {
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
				changed_prefs.gfx_pfullscreen = currprefs.gfx_pfullscreen = GFX_FULLSCREEN;
			else
				changed_prefs.gfx_afullscreen = currprefs.gfx_afullscreen = GFX_FULLSCREEN;
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
			gfxvidinfo.gfx_resolution_reserved = currprefs.gfx_resolution;
			gfxvidinfo.gfx_vresolution_reserved = currprefs.gfx_vresolution;
#if defined (GFXFILTER)
			if (currentmode->flags & (DM_D3D | DM_SWSCALE)) {
				if (!currprefs.gfx_autoresolution) {
					currentmode->amiga_width = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
					currentmode->amiga_height = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;
				} else {
					gfxvidinfo.gfx_resolution_reserved = currprefs.gfx_resolution == RES_SUPERHIRES ? RES_SUPERHIRES : RES_HIRES;
					gfxvidinfo.gfx_vresolution_reserved = VRES_DOUBLE;
					currentmode->amiga_width = AMIGA_WIDTH_MAX << gfxvidinfo.gfx_resolution_reserved;
					currentmode->amiga_height = AMIGA_HEIGHT_MAX << gfxvidinfo.gfx_vresolution_reserved;
				}
				if (currprefs.gfx_resolution == RES_SUPERHIRES)
					currentmode->amiga_height *= 2;
				if (currentmode->amiga_height > 960)
					currentmode->amiga_height = 960;
				if (usedfilter) {
					if ((usedfilter->flags & (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) == (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) {
						currentmode->current_depth = currentmode->native_depth;
					} else {
						currentmode->current_depth = (usedfilter->flags & UAE_FILTER_MODE_32) ? 32 : 16;
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
			gfxvidinfo.bufmem = NULL;
			gfxvidinfo.linemem = NULL;
			gfxvidinfo.width = (currentmode->amiga_width + 7) & ~7;
			gfxvidinfo.height = currentmode->amiga_height;
			gfxvidinfo.maxblocklines = 0; // flush_screen actually does everything
			gfxvidinfo.rowbytes = currentmode->pitch;
			break;
#ifdef PICASSO96
		}
#endif
	}

#ifdef PICASSO96
	picasso_vidinfo.rowbytes = 0;
	picasso_vidinfo.pixbytes = currentmode->current_depth / 8;
	picasso_vidinfo.rgbformat = 0;
	picasso_vidinfo.extra_mem = 1;
	picasso_vidinfo.height = currentmode->current_height;
	picasso_vidinfo.width = currentmode->current_width;
	picasso_vidinfo.depth = currentmode->current_depth;
	picasso_vidinfo.offset = 0;
#endif
	gfxvidinfo.emergmem = scrlinebuf; // memcpy from system-memory to video-memory

	xfree (gfxvidinfo.realbufmem);
	gfxvidinfo.realbufmem = NULL;
	gfxvidinfo.bufmem = NULL;
	gfxvidinfo.bufmem_allocated = false;

	if (!screen_is_picasso) {
		if ((currentmode->flags & DM_DDRAW) && !(currentmode->flags & (DM_D3D | DM_SWSCALE))) {

			gfxvidinfo.bufmem_allocated = true;

		} else if (currentmode->flags & DM_SWSCALE) {

			int w = currentmode->amiga_width * 2;
			int h = currentmode->amiga_height * 2;
			int size = (w * 2) * (h * 3) * gfxvidinfo.pixbytes;
			gfxvidinfo.realbufmem = xmalloc (uae_u8, size);
			memset (gfxvidinfo.realbufmem, 0, size);
			gfxvidinfo.bufmem = gfxvidinfo.realbufmem + (w + (w * 2) * h) * gfxvidinfo.pixbytes;
			gfxvidinfo.rowbytes = w * 2 * gfxvidinfo.pixbytes;
			gfxvidinfo.bufmemend = gfxvidinfo.realbufmem + size - gfxvidinfo.rowbytes;
			gfxvidinfo.bufmem_allocated = true;

		} else if (currentmode->flags & DM_D3D) {

			int size = currentmode->amiga_width * currentmode->amiga_height * gfxvidinfo.pixbytes;
			gfxvidinfo.realbufmem = xmalloc (uae_u8, size);
			gfxvidinfo.bufmem = gfxvidinfo.realbufmem;
			gfxvidinfo.rowbytes = currentmode->amiga_width * gfxvidinfo.pixbytes;
			gfxvidinfo.bufmemend = gfxvidinfo.bufmem + size;
			gfxvidinfo.bufmem_allocated = true;

		}
		init_row_map ();
	}
	init_colors ();


#if defined (GFXFILTER)
	S2X_free ();
	if (currentmode->flags & DM_SWSCALE) {
		S2X_init (currentmode->native_width, currentmode->native_height,
			currentmode->amiga_width, currentmode->amiga_height,
			currentmode->current_depth, currentmode->native_depth);
	}
#ifdef D3D
	if (currentmode->flags & DM_D3D) {
		const TCHAR *err = D3D_init (hAmigaWnd, currentmode->native_width, currentmode->native_height,
			currentmode->amiga_width, currentmode->amiga_height, currentmode->current_depth, screen_is_picasso ? 1 : currprefs.gfx_filter_filtermode + 1);
		if (err) {
			D3D_free ();
			gui_message (err);
			changed_prefs.gfx_api = currprefs.gfx_api = 0;
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
	picasso_refresh ();

	if (isfullscreen () > 0)
		setmouseactive (-1);

	return 1;

oops:
	close_hwnds ();
	return ret;
}

void WIN32GFX_WindowMove (void)
{
}

void updatedisplayarea (void)
{
	if (!screen_is_initialized)
		return;
	if (dx_islost ())
		return;
	if (picasso_on)
		return;
#if defined (GFXFILTER)
	if (currentmode->flags & DM_D3D) {
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
	config_changed = 1;
}

void toggle_fullscreen (int mode)
{
	int *p = picasso_on ? &changed_prefs.gfx_pfullscreen : &changed_prefs.gfx_afullscreen;
	int wfw = picasso_on ? wasfullwindow_p : wasfullwindow_a;
	int v = *p;

	if (mode < 0) {
		// fullscreen <> window (if in fullwindow: fullwindow <> fullscreen)
		if (v == GFX_FULLWINDOW)
			v = GFX_FULLSCREEN;
		else if (v == GFX_WINDOW)
			v = GFX_FULLSCREEN;
		else if (v == GFX_FULLSCREEN)
			if (wfw > 0)
				v = GFX_FULLWINDOW;
			else
				v = GFX_WINDOW;
	} else if (mode == 0) {
		// fullscreen <> window
		if (v == GFX_FULLSCREEN)
			v = GFX_WINDOW;
		else
			v = GFX_FULLSCREEN;
	} else if (mode == 1) {
		// fullscreen <> fullwindow
		if (v == GFX_FULLSCREEN)
			v = GFX_FULLWINDOW;
		else
			v = GFX_FULLSCREEN;
	} else if (mode == 2) {
		// window <> fullwindow
		if (v == GFX_FULLWINDOW)
			v = GFX_WINDOW;
		else
			v = GFX_FULLWINDOW;
	}
	*p = v;
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
