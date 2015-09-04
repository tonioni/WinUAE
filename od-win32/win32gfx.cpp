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
#include <dwmapi.h>

#include "sysdeps.h"

#include "resource.h"

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
#include "gfxboard.h"
#include "cpuboard.h"
#include "x86.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif
#include "statusline.h"

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
int scalepicasso;
static double remembered_vblank;
static volatile int vblankthread_mode, vblankthread_counter;

struct winuae_currentmode {
	unsigned int flags;
	int native_width, native_height, native_depth, pitch;
	int current_width, current_height, current_depth;
	int amiga_width, amiga_height;
	int initdone;
	int fullfill;
	int vsync;
	int freq;
};

struct MultiDisplay Displays[MAX_DISPLAYS + 1];

static struct winuae_currentmode currentmodestruct;
static int screen_is_initialized;
static int display_change_requested;
int window_led_drives, window_led_drives_end;
int window_led_hd, window_led_hd_end;
int window_led_joys, window_led_joys_end, window_led_joy_start;
int window_led_msg, window_led_msg_end, window_led_msg_start;
extern int console_logging;
int window_extra_width, window_extra_height;

static struct winuae_currentmode *currentmode = &currentmodestruct;
static int wasfullwindow_a, wasfullwindow_p;

static int vblankbasewait1, vblankbasewait2, vblankbasewait3, vblankbasefull, vblankbaseadjust;
static bool vblankbaselace;
static int vblankbaselace_chipset;
static bool vblankthread_oddeven, vblankthread_oddeven_got;
static int graphics_mode_changed;
int vsync_modechangetimeout = 10;

int screen_is_picasso = 0;

extern int reopen (int, bool);

#define VBLANKTH_KILL 0
#define VBLANKTH_CALIBRATE 1
#define VBLANKTH_IDLE 2
#define VBLANKTH_ACTIVE_WAIT 3
#define VBLANKTH_ACTIVE 4
#define VBLANKTH_ACTIVE_START 5
#define VBLANKTH_ACTIVE_SKIPFRAME 6
#define VBLANKTH_ACTIVE_SKIPFRAME2 7

static volatile bool vblank_found;
static volatile int flipthread_mode;
volatile bool vblank_found_chipset;
volatile bool vblank_found_rtg;
static HANDLE flipevent, flipevent2, vblankwaitevent;
static volatile int flipevent_mode;
static CRITICAL_SECTION screen_cs;

void gfx_lock (void)
{
	EnterCriticalSection (&screen_cs);
}
void gfx_unlock (void)
{
	LeaveCriticalSection (&screen_cs);
}

int vsync_busy_wait_mode;

static void vsync_sleep (bool preferbusy)
{
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
	bool dowait;

	if (vsync_busy_wait_mode == 0) {
		dowait = ap->gfx_vflip || !preferbusy;
		//dowait = !preferbusy;
	} else if (vsync_busy_wait_mode < 0) {
		dowait = true;
	} else {
		dowait = false;
	}
	if (dowait && (currprefs.m68k_speed >= 0 || currprefs.m68k_speed_throttle < 0))
		sleep_millis_main (1);
}

static void changevblankthreadmode_do (int newmode, bool fast)
{
	int t = vblankthread_counter;
	vblank_found = false;
	vblank_found_chipset = false;
	vblank_found_rtg = false;
	if (vblankthread_mode <= 0 || vblankthread_mode == newmode)
		return;
	vblankthread_mode = newmode;
	if (newmode == VBLANKTH_KILL) {
		flipthread_mode = 0;
		SetEvent (flipevent);
		while (flipthread_mode == 0)
			sleep_millis_main (1);
		CloseHandle (flipevent);
		CloseHandle (flipevent2);
		CloseHandle (vblankwaitevent);
		flipevent = NULL;
		flipevent2 = NULL;
		vblankwaitevent = NULL;
	}
	if (!fast) {
		while (t == vblankthread_counter && vblankthread_mode > 0);
	}
}

static void changevblankthreadmode (int newmode)
{
	changevblankthreadmode_do (newmode, false);
}
static void changevblankthreadmode_fast (int newmode)
{
	changevblankthreadmode_do (newmode, true);
}

int WIN32GFX_IsPicassoScreen (void)
{
	return screen_is_picasso ? 1 : 0;
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
	int idx = screen_is_picasso ? 1 : 0;
	return p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLWINDOW ? -1 : 0);
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

int default_freq = 60;

HWND hStatusWnd = NULL;

static uae_u8 *scrlinebuf;

static struct MultiDisplay *getdisplay2 (struct uae_prefs *p, int index)
{
	int max;
	int display = index < 0 ? p->gfx_apmode[screen_is_picasso ? APMODE_RTG : APMODE_NATIVE].gfx_display - 1 : index;

	max = 0;
	while (Displays[max].monitorname)
		max++;
	if (max == 0) {
		gui_message (_T("no display adapters! Exiting"));
		exit (0);
	}
	if (index >= 0 && display >= max)
		return NULL;
	if (display >= max)
		display = 0;
	if (display < 0)
		display = 0;
	return &Displays[display];
}
struct MultiDisplay *getdisplay (struct uae_prefs *p)
{
	return getdisplay2 (p, -1);
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

static int target_get_display2(const TCHAR *name, int mode)
{
	int found, found2;

	found = -1;
	found2 = -1;
	for (int i = 0; Displays[i].monitorname; i++) {
		struct MultiDisplay *md = &Displays[i];
		if (mode == 1 && md->monitorid[0] == '\\')
			continue;
		if (mode == 2 && md->monitorid[0] != '\\')
			continue;
		if (!_tcscmp (md->monitorid, name)) {
			if (found < 0) {
				found = i + 1;
			} else {
				found2 = found;
				found = -1;
				break;
			}
		}
	}
	if (found >= 0)
		return found;

	found = -1;
	for (int i = 0; Displays[i].monitorname; i++) {
		struct MultiDisplay *md = &Displays[i];
		if (mode == 1 && md->adapterid[0] == '\\')
			continue;
		if (mode == 2 && md->adapterid[0] != '\\')
			continue;
		if (!_tcscmp (md->adapterid, name)) {
			if (found < 0) {
				found = i + 1;
			} else {
				if (found2 < 0)
					found2 = found;
				found = -1;
				break;
			}
		}
	}
	if (found >= 0)
		return found;

	for (int i = 0; Displays[i].monitorname; i++) {
		struct MultiDisplay *md = &Displays[i];
		if (mode == 1 && md->adaptername[0] == '\\')
			continue;
		if (mode == 2 && md->adaptername[0] != '\\')
			continue;
		if (!_tcscmp (md->adaptername, name)) {
			if (found < 0) {
				found = i + 1;
			} else {
				if (found2 < 0)
					found2 = found;
				found = -1;
				break;
			}
		}
	}
	if (found >= 0)
		return found;

	for (int i = 0; Displays[i].monitorname; i++) {
		struct MultiDisplay *md = &Displays[i];
		if (mode == 1 && md->monitorname[0] == '\\')
			continue;
		if (mode == 2 && md->monitorname[0] != '\\')
			continue;
		if (!_tcscmp (md->monitorname, name)) {
			if (found < 0) {
				found = i + 1;
			} else {
				if (found2 < 0)
					found2 = found;
				found = -1;
				break;
			}
		}
	}
	if (found >= 0)
		return found;
	if (mode == 3) {
		if (found2 >= 0)
			return found2;
	}

	return -1;
}

int target_get_display(const TCHAR *name)
{
	int disp;

	//write_log(_T("target_get_display '%s'\n"), name);
	disp = target_get_display2(name, 0);
	//write_log(_T("Scan 0: %d\n"), disp);
	if (disp >= 0)
		return disp;
	disp = target_get_display2(name, 1);
	//write_log(_T("Scan 1: %d\n"), disp);
	if (disp >= 0)
		return disp;
	disp = target_get_display2(name, 2);
	//write_log(_T("Scan 2: %d\n"), disp);
	if (disp >= 0)
		return disp;
	disp = target_get_display2(name, 3);
	//write_log(_T("Scan 3: %d\n"), disp);
	if (disp >= 0)
		return disp;
	return -1;
}

const TCHAR *target_get_display_name (int num, bool friendlyname)
{
	if (num <= 0)
		return NULL;
	struct MultiDisplay *md = getdisplay2 (NULL, num - 1);
	if (!md)
		return NULL;
	if (friendlyname)
		return md->monitorname;
	return md->monitorid;
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

static int picasso_offset_x, picasso_offset_y;
static float picasso_offset_mx, picasso_offset_my;

void getgfxoffset (float *dxp, float *dyp, float *mxp, float *myp)
{
	float dx, dy;

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

int getrefreshrate (int width, int height)
{
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	int freq = 0;
	
	if (ap->gfx_refreshrate <= 0)
		return 0;
	
	struct MultiDisplay *md = getdisplay (&currprefs);
	for (int i = 0; md->DisplayModes[i].depth >= 0; i++) {
		struct PicassoResolution *pr = &md->DisplayModes[i];
		if (pr->res.width == width && pr->res.height == height) {
			for (int j = 0; pr->refresh[j] > 0; j++) {
				if (pr->refresh[j] == ap->gfx_refreshrate)
					return ap->gfx_refreshrate;
				if (pr->refresh[j] > freq && pr->refresh[j] < ap->gfx_refreshrate)
					freq = pr->refresh[j];
			}
		}
	}
	write_log (_T("Refresh rate %d not supported, using %d\n"), ap->gfx_refreshrate, freq);
	return freq;
}

static int set_ddraw_2 (void)
{
	HRESULT ddrval;
	int bits = (currentmode->current_depth + 7) & ~7;
	int width = currentmode->native_width;
	int height = currentmode->native_height;
	int dxfullscreen, wfullscreen, dd;
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	int freq = ap->gfx_refreshrate;

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
			HRESULT olderr;
			freq = getrefreshrate (width, height);
			write_log (_T("set_ddraw: trying %dx%d, bits=%d, refreshrate=%d\n"), width, height, bits, freq);
			ddrval = DirectDraw_SetDisplayMode (width, height, bits, freq);
			if (SUCCEEDED (ddrval))
				break;
			olderr = ddrval;
			if (freq) {
				write_log (_T("set_ddraw: failed, trying without forced refresh rate\n"));
				freq = 0;
				DirectDraw_SetCooperativeLevel (hAmigaWnd, dxfullscreen, TRUE);
				ddrval = DirectDraw_SetDisplayMode (width, height, bits, freq);
				if (SUCCEEDED (ddrval))
					break;
			}
			if (olderr != DDERR_INVALIDMODE  && olderr != 0x80004001 && olderr != DDERR_UNSUPPORTEDMODE)
				goto oops;
			return -1;
		}
		currentmode->freq = freq;
		updatewinrect (true);
	}

	if (dd) {
		ddrval = DirectDraw_CreateClipper ();
		if (FAILED (ddrval))
			goto oops;
		ddrval = DirectDraw_CreateMainSurface (width, height);
		if (FAILED(ddrval)) {
			write_log (_T("set_ddraw: couldn't CreateSurface() for primary because %s.\n"), DXError (ddrval));
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

	write_log (_T("set_ddraw: %dx%d@%d-bytes\n"), width, height, bits);
	return 1;
oops:
	return 0;
}

static void addmode (struct MultiDisplay *md, DEVMODE *dm, int rawmode)
{
	int ct;
	int i, j;
	int w = dm->dmPelsWidth;
	int h = dm->dmPelsHeight;
	int d = dm->dmBitsPerPel;
	bool lace = false;
	int freq = 0;

	if (w > max_uae_width || h > max_uae_height) {
		write_log (_T("Ignored mode %d*%d\n"), w, h);
		return;
	}

	if (dm->dmFields & DM_DISPLAYFREQUENCY) {
		freq = dm->dmDisplayFrequency;
		if (freq < 10)
			freq = 0;
	}
	if (dm->dmFields & DM_DISPLAYFLAGS) {
		lace = (dm->dmDisplayFlags & DM_INTERLACED) != 0;
	}

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
				if (md->DisplayModes[i].refresh[j] == 0 || md->DisplayModes[i].refresh[j] == freq)
					break;
			}
			if (j < MAX_REFRESH_RATES) {
				md->DisplayModes[i].refresh[j] = freq;
				md->DisplayModes[i].refreshtype[j] = (lace ? REFRESH_RATE_LACE : 0) | (rawmode ? REFRESH_RATE_RAW : 0);
				md->DisplayModes[i].refresh[j + 1] = 0;
				if (!lace)
					md->DisplayModes[i].lace = false;
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
	md->DisplayModes[i].rawmode = rawmode;
	md->DisplayModes[i].lace = lace;
	md->DisplayModes[i].res.width = w;
	md->DisplayModes[i].res.height = h;
	md->DisplayModes[i].depth = d;
	md->DisplayModes[i].refresh[0] = freq;
	md->DisplayModes[i].refreshtype[0] = (lace ? REFRESH_RATE_LACE : 0) | (rawmode ? REFRESH_RATE_RAW : 0);
	md->DisplayModes[i].refresh[1] = 0;
	md->DisplayModes[i].colormodes = ct;
	md->DisplayModes[i + 1].depth = -1;
	_stprintf (md->DisplayModes[i].name, _T("%dx%d%s, %d-bit"),
		md->DisplayModes[i].res.width, md->DisplayModes[i].res.height,
		lace ? _T("i") : _T(""),
		md->DisplayModes[i].depth * 8);
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

#if 0
static void sortmonitors (void)
{
	for (int i = 0; Displays[i].monitorid; i++) {
		for (int j = i + 1; Displays[j].monitorid; j++) {
			int comp = (Displays[j].primary ? 1 : 0) - (Displays[i].primary ? 1 : 0);
			if (!comp)
				comp = _tcsicmp (Displays[i].adapterid, Displays[j].adapterid);
			if (comp > 0) {
				struct MultiDisplay md;
				memcpy (&md, &Displays[i], sizeof MultiDisplay);
				memcpy (&Displays[i], &Displays[j], sizeof MultiDisplay);
				memcpy (&Displays[j], &md, sizeof MultiDisplay);
			}
		}
	}
}
#endif

static void modesList (struct MultiDisplay *md)
{
	int i, j;

	i = 0;
	while (md->DisplayModes[i].depth >= 0) {
		write_log (_T("%d: %s%s ("), i, md->DisplayModes[i].rawmode ? _T("!") : _T(""), md->DisplayModes[i].name);
		j = 0;
		while (md->DisplayModes[i].refresh[j] > 0) {
			if (j > 0)
				write_log (_T(","));
			if (md->DisplayModes[i].refreshtype[j] & REFRESH_RATE_RAW)
				write_log (_T("!"));
			write_log (_T("%d"),  md->DisplayModes[i].refresh[j]);
			if (md->DisplayModes[i].refreshtype[j] & REFRESH_RATE_LACE)
				write_log (_T("i"));
			j++;
		}
		write_log (_T(")\n"));
		i++;
	}
}

static BOOL CALLBACK monitorEnumProc (HMONITOR h, HDC hdc, LPRECT rect, LPARAM data)
{
	struct MultiDisplay *md = Displays;
	MONITORINFOEX lpmi;
	lpmi.cbSize = sizeof lpmi;
	GetMonitorInfo(h, (LPMONITORINFO)&lpmi);
	while (md - Displays < MAX_DISPLAYS && md->monitorid) {
		if (!_tcscmp (md->adapterid, lpmi.szDevice)) {
			TCHAR tmp[1000];
			md->rect = lpmi.rcMonitor;
			if (md->rect.left == 0 && md->rect.top == 0)
				_stprintf (tmp, _T("%s (%d*%d)"), md->monitorname, md->rect.right - md->rect.left, md->rect.bottom - md->rect.top);
			else
				_stprintf (tmp, _T("%s (%d*%d) [%d*%d]"), md->monitorname, md->rect.right - md->rect.left, md->rect.bottom - md->rect.top, md->rect.left, md->rect.top);
			if (md->primary)
				_tcscat (tmp, _T(" *"));
			xfree (md->fullname);
			md->fullname = my_strdup (tmp);
			return TRUE;
		}
		md++;
	}
	return TRUE;
}

static void getd3dmonitornames (void)
{
	struct MultiDisplay *md = Displays;
	IDirect3D9 *d3d;
	int max;

	// XP does not support hybrid displays, don't load Direct3D
	if (!os_vista)
		return;
	d3d = Direct3DCreate9 (D3D_SDK_VERSION);
	if (!d3d)
		return;
	max = d3d->GetAdapterCount ();
	while (md - Displays < MAX_DISPLAYS && md->monitorid) {
		POINT pt;
		HMONITOR winmon;
		pt.x = (md->rect.right - md->rect.left) / 2 + md->rect.left;
		pt.y = (md->rect.bottom - md->rect.top) / 2 + md->rect.top;
		winmon = MonitorFromPoint (pt, MONITOR_DEFAULTTONEAREST);
		for (int i = 0; i < max; i++) {
			D3DADAPTER_IDENTIFIER9 did;
			HMONITOR d3dmon = d3d->GetAdapterMonitor (i);
			if (d3dmon != winmon)
				continue;
			if (SUCCEEDED (d3d->GetAdapterIdentifier (i, 0, &did))) {
				TCHAR *name = au (did.Description);
				my_trim (name);
				if (_tcsicmp (name, md->adaptername)) {
					write_log (_T("%d: '%s' -> '%s'\n"), i, md->adaptername, name);
					xfree (md->adaptername);
					md->adaptername = name;
					name = NULL;
				}
				xfree (name);
			}
			break;
		}
		md++;
	}
	d3d->Release ();
}

static bool enumeratedisplays2 (bool selectall)
{
	struct MultiDisplay *md = Displays;
	int adapterindex = 0;
	DISPLAY_DEVICE add;
	add.cb = sizeof add;
	while (EnumDisplayDevices (NULL, adapterindex, &add, 0)) {

		adapterindex++;
		if (!selectall) {
			if (!(add.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
				continue;
			if (add.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
				continue;
		}
		if (md - Displays >= MAX_DISPLAYS)
			break;

		int monitorindex = 0;
		DISPLAY_DEVICE mdd;
		mdd.cb = sizeof mdd;
		while (EnumDisplayDevices (add.DeviceName, monitorindex, &mdd, 0)) {
			monitorindex++;
			if (md - Displays >= MAX_DISPLAYS)
				break;
			if (!selectall) {
				if (!(mdd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
					continue;
				if (mdd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
					continue;
			}
			md->adaptername = my_strdup_trim (add.DeviceString);
			md->adapterid = my_strdup (add.DeviceName);
			md->adapterkey = my_strdup (add.DeviceID);
			md->monitorname = my_strdup_trim (mdd.DeviceString);
			md->monitorid = my_strdup (mdd.DeviceKey);
			if (add.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
				md->primary = true;
			md++;
		}
		if (md - Displays >= MAX_DISPLAYS)
			return true;
		if (monitorindex == 0) {
			md->adaptername = my_strdup_trim (add.DeviceString);
			md->adapterid = my_strdup (add.DeviceName);
			md->adapterkey = my_strdup (add.DeviceID);
			md->monitorname = my_strdup_trim (add.DeviceString);
			md->monitorid = my_strdup (add.DeviceKey);
			md->primary = true;
			md++;
		}
	}
	if (md == Displays)
		return false;
	EnumDisplayMonitors (NULL, NULL, monitorEnumProc, NULL);
	md = Displays;
	while (md->monitorname) {
		if (!md->fullname)
			md->fullname = my_strdup (md->adapterid);
		md++;
	}
	getd3dmonitornames ();
	//sortmonitors ();
	return true;
}
void enumeratedisplays (void)
{
	if (!enumeratedisplays2 (false))
		enumeratedisplays2(true);
}

void sortdisplays (void)
{
	struct MultiDisplay *md;
	int i, idx;

	int w = GetSystemMetrics (SM_CXSCREEN);
	int h = GetSystemMetrics (SM_CYSCREEN);
	int b = 0;
	HDC hdc = GetDC (NULL);
	if (hdc) {
		b = GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
		ReleaseDC (NULL, hdc);
	}
	write_log (_T("Desktop: W=%d H=%d B=%d. CXVS=%d CYVS=%d\n"), w, h, b,
		GetSystemMetrics (SM_CXVIRTUALSCREEN), GetSystemMetrics (SM_CYVIRTUALSCREEN));

	md = Displays;
	while (md->monitorname) {
		md->DisplayModes = xmalloc (struct PicassoResolution, MAX_PICASSO_MODES);
		md->DisplayModes[0].depth = -1;

		write_log (_T("%s '%s' [%s]\n"), md->adaptername, md->adapterid, md->adapterkey);
		write_log (_T("-: %s [%s]\n"), md->fullname, md->monitorid);
		for (int mode = 0; mode < 2; mode++) {
			DEVMODE dm;
			dm.dmSize = sizeof dm;
			dm.dmDriverExtra = 0;
			idx = 0;
			while (EnumDisplaySettingsEx (md->adapterid, idx, &dm, mode ? EDS_RAWMODE : 0)) {
				int found = 0;
				int idx2 = 0;
				while (md->DisplayModes[idx2].depth >= 0 && !found) {
					struct PicassoResolution *pr = &md->DisplayModes[idx2];
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
					write_log (_T("EnumDisplaySettings(%dx%dx%d %dHz %08x)\n"),
						dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency, dm.dmFields);
#endif
					if ((dm.dmFields & DM_PELSWIDTH) && (dm.dmFields & DM_PELSHEIGHT) && (dm.dmFields & DM_BITSPERPEL)) {
						addmode (md, &dm, mode);
					}
				}
				idx++;
			}
		}
		//dhack();
		sortmodes (md);
		modesList (md);
		i = 0;
		while (md->DisplayModes[i].depth > 0)
			i++;
		write_log (_T("%d display modes.\n"), i);
		md++;
	}
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

static void flushit (struct vidbuffer *vb, int lineno)
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

void flush_line (struct vidbuffer *vb, int lineno)
{
	flushit (vb, lineno);
}

void flush_block (struct vidbuffer *vb, int first, int last)
{
	flushit (vb, first);
	flushit (vb, last);
}

void flush_screen (struct vidbuffer *vb, int a, int b)
{
}

static volatile bool render_ok, wait_render;

bool render_screen (bool immediate)
{
	bool v = false;
	int cnt;

	render_ok = false;
	if (minimized || picasso_on || monitor_off || dx_islost ())
		return render_ok;
	cnt = 0;
	while (wait_render) {
		sleep_millis (1);
		cnt++;
		if (cnt > 500)
			return render_ok;
	}
	flushymin = 0;
	flushymax = currentmode->amiga_height;
	EnterCriticalSection (&screen_cs);
	if (currentmode->flags & DM_D3D) {
		v = D3D_renderframe (immediate);
	} else if (currentmode->flags & DM_SWSCALE) {
		S2X_render ();
		v = true;
	} else if (currentmode->flags & DM_DDRAW) {
		v = true;
	}
	render_ok = v;
	LeaveCriticalSection (&screen_cs);
	return render_ok;
}

static void waitflipevent (void)
{
	while (flipevent_mode) {
		if (WaitForSingleObject (flipevent2, 10) == WAIT_ABANDONED)
			break;
	}
}
static void doflipevent (int mode)
{
	if (flipevent == NULL)
		return;
	waitflipevent ();
	flipevent_mode = mode;
	SetEvent (flipevent);
}

bool show_screen_maybe (bool show)
{
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
	if (!ap->gfx_vflip || ap->gfx_vsyncmode == 0 || !ap->gfx_vsync) {
		if (show)
			show_screen (0);
		return false;
	}
#if 0
	if (ap->gfx_vflip < 0) {
		doflipevent ();
		return true;
	}
#endif
	return false;
}

void show_screen_special (void)
{
	EnterCriticalSection (&screen_cs);
	if (currentmode->flags & DM_D3D) {
		D3D_showframe_special (1);
	}
	LeaveCriticalSection (&screen_cs);
}

void show_screen (int mode)
{
	EnterCriticalSection (&screen_cs);
	if (mode == 2) {
		if (currentmode->flags & DM_D3D) {
			D3D_showframe_special (1);
		}
		LeaveCriticalSection (&screen_cs);
		return;
	}
	if (!render_ok) {
		LeaveCriticalSection (&screen_cs);
		return;
	}
	if (currentmode->flags & DM_D3D) {
		D3D_showframe ();

#ifdef GFXFILTER
	} else if (currentmode->flags & DM_SWSCALE) {
		if (!dx_islost () && !picasso_on)
			DirectDraw_Flip (1);
#endif
	} else if (currentmode->flags & DM_DDRAW) {
		if (!dx_islost () && !picasso_on)
			DirectDraw_Flip (1);
	}
	LeaveCriticalSection (&screen_cs);
	render_ok = false;
}

static uae_u8 *ddraw_dolock (void)
{
	if (!DirectDraw_SurfaceLock ()) {
		dx_check ();
		return 0;
	}
	gfxvidinfo.outbuffer->bufmem = DirectDraw_GetSurfacePointer ();
	gfxvidinfo.outbuffer->rowbytes = DirectDraw_GetSurfacePitch ();
	init_row_map ();
	clear_inhibit_frame (IHF_WINDOWHIDDEN);
	return gfxvidinfo.outbuffer->bufmem;
}

bool lockscr3d(struct vidbuffer *vb)
{
	if (currentmode->flags & DM_D3D) {
		if (!(currentmode->flags & DM_SWSCALE)) {
			vb->bufmem = D3D_locktexture(&vb->rowbytes, NULL, false);
			if (vb->bufmem) 
				return true;
		}
	}
	return false;
}

void unlockscr3d(struct vidbuffer *vb)
{
	if (currentmode->flags & DM_D3D) {
		if (!(currentmode->flags & DM_SWSCALE)) {
			D3D_unlocktexture();
		}
	}
}

int lockscr (struct vidbuffer *vb, bool fullupdate)
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
			vb->bufmem = D3D_locktexture (&vb->rowbytes, NULL, fullupdate);
			if (vb->bufmem) {
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

void unlockscr (struct vidbuffer *vb)
{
	if (currentmode->flags & DM_D3D) {
		if (currentmode->flags & DM_SWSCALE) {
			S2X_render ();
		} else {
			D3D_flushtexture (flushymin, flushymax);
			vb->bufmem = NULL;
		}
		D3D_unlocktexture ();
	} else if (currentmode->flags & DM_SWSCALE) {
		return;
	} else if (currentmode->flags & DM_DDRAW) {
		DirectDraw_SurfaceUnlock ();
		vb->bufmem = NULL;
	}
}

void flush_clear_screen (struct vidbuffer *vb)
{
	if (!vb)
		return;
	if (lockscr (vb, true)) {
		int y;
		for (y = 0; y < vb->height_allocated; y++) {
			memset (vb->bufmem + y * vb->rowbytes, 0, vb->width_allocated * vb->pixbytes);
		}
		unlockscr (vb);
		flush_screen (vb, 0, 0);
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
	picasso_offset_mx = 1.0;
	picasso_offset_my = 1.0;
	if (scalepicasso) {
		int srcratio, dstratio;
		int srcwidth, srcheight;

		if (scalepicasso < 0 || scalepicasso > 1) {
			srcwidth = picasso96_state.Width;
			srcheight = picasso96_state.Height;
		} else {
			srcwidth = currentmode->native_width;
			srcheight = currentmode->native_height;
		}

		SetRect (&sr, 0, 0, picasso96_state.Width, picasso96_state.Height);
		if (currprefs.win32_rtgscaleaspectratio < 0) {
			// automatic
			srcratio = picasso96_state.Width * ASPECTMULT / picasso96_state.Height;
			dstratio = srcwidth * ASPECTMULT / srcheight;
		} else if (currprefs.win32_rtgscaleaspectratio == 0) {
			// none
			srcratio = dstratio = 0;
		} else {
			// manual
			srcratio = currprefs.win32_rtgscaleaspectratio;
			dstratio = srcwidth * ASPECTMULT / srcheight;
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
		picasso_offset_mx = (float)picasso96_state.Width / (dr.right - dr.left);
		picasso_offset_my = (float)picasso96_state.Height / (dr.bottom - dr.top);
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
	picasso_offset_mx = 1.0;
	picasso_offset_my = 1.0;

	if (!picasso_on)
		return;
	if (!scalepicasso)
		return;

	int srcratio, dstratio;
	int srcwidth, srcheight;
	srcwidth = picasso96_state.Width;
	srcheight = picasso96_state.Height;
	if (!srcwidth || !srcheight)
		return;

	if (scalepicasso == RTG_MODE_INTEGER_SCALE) {
		int divx = currentmode->native_width / srcwidth;
		int divy = currentmode->native_height / srcheight;
		int mul = divx > divy ? divy : divx;
		int xx = srcwidth * mul;
		int yy = srcheight * mul;
		SetRect (dr, 0, 0, currentmode->native_width / mul, currentmode->native_height / mul);
		//picasso_offset_x = -(picasso96_state.Width - xx) / 2;
		//picasso_offset_y = -(currentmode->native_height - srcheight) / 2;
	} else if (scalepicasso == RTG_MODE_CENTER) {
		int xx = (currentmode->native_width - srcwidth) / 2;
		int yy = (currentmode->native_height - srcheight) / 2;
		picasso_offset_x = -xx;
		picasso_offset_y = -yy;
		SetRect (dr, 0, 0, currentmode->native_width, currentmode->native_height);
	} else {
		if (currprefs.win32_rtgscaleaspectratio < 0) {
			// automatic
			srcratio = srcwidth * ASPECTMULT / srcheight;
			dstratio = currentmode->native_width * ASPECTMULT / currentmode->native_height;
		} else if (currprefs.win32_rtgscaleaspectratio == 0) {
			// none
			srcratio = dstratio = 0;
		} else {
			// manual
			dstratio = (currprefs.win32_rtgscaleaspectratio / ASPECTMULT) * ASPECTMULT / (currprefs.win32_rtgscaleaspectratio & (ASPECTMULT - 1));
			srcratio = srcwidth * ASPECTMULT / srcheight;
		}

		if (srcratio == dstratio) {
			SetRect (dr, 0, 0, srcwidth, srcheight);
		} else if (srcratio > dstratio) {
			int yy = srcheight * srcratio / dstratio;
			SetRect (dr, 0, 0, srcwidth, yy);
			picasso_offset_y = (picasso96_state.Height - yy) / 2;
		} else {
			int xx = srcwidth * dstratio / srcratio;
			SetRect (dr, 0, 0, xx, srcheight);
			picasso_offset_x = (picasso96_state.Width - xx) / 2;
		}
	}

	OffsetRect (zr, picasso_offset_x, picasso_offset_y);
	picasso_offset_mx = (float)srcwidth / (dr->right - dr->left);
	picasso_offset_my = (float)srcheight / (dr->bottom - dr->top);
}

static bool rtg_locked;

static uae_u8 *gfx_lock_picasso2 (bool fullupdate)
{
	if (currprefs.gfx_api) {
		int pitch;
		uae_u8 *p = D3D_locktexture (&pitch, NULL, fullupdate);
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
uae_u8 *gfx_lock_picasso (bool fullupdate, bool doclear)
{
	static uae_u8 *p;
	if (rtg_locked) {
		return p;
	}
	EnterCriticalSection (&screen_cs);
	p = gfx_lock_picasso2 (fullupdate);
	if (!p) {
		LeaveCriticalSection (&screen_cs);
	} else {
		rtg_locked = true;
		if (doclear) {
			uae_u8 *p2 = p;
			for (int h = 0; h < picasso_vidinfo.height; h++) {
				memset (p2, 0, picasso_vidinfo.width * picasso_vidinfo.pixbytes);
				p2 += picasso_vidinfo.rowbytes;
			}
		}
	}
	return p;
}

void gfx_unlock_picasso (bool dorender)
{
	if (!rtg_locked)
		EnterCriticalSection (&screen_cs);
	rtg_locked = false;
	if (currprefs.gfx_api) {
		if (dorender) {
			if (p96_double_buffer_needs_flushing) {
				D3D_flushtexture (p96_double_buffer_first, p96_double_buffer_last);
				p96_double_buffer_needs_flushing = 0;
			}
		}
		D3D_unlocktexture ();
		if (dorender) {
			if (D3D_renderframe (false)) {
				LeaveCriticalSection (&screen_cs);
				render_ok = true;
				show_screen_maybe (true);
			} else {
				LeaveCriticalSection (&screen_cs);
			}
		} else {
			LeaveCriticalSection (&screen_cs);
		}
	} else {
		DirectDraw_SurfaceUnlock ();
		if (dorender) {
			if (p96_double_buffer_needs_flushing) {
				DX_Blit96 (p96_double_buffer_firstx, p96_double_buffer_first,
					p96_double_buffer_lastx - p96_double_buffer_firstx + 1,
					p96_double_buffer_last - p96_double_buffer_first + 1);
				p96_double_buffer_needs_flushing = 0;
			}
		}
		LeaveCriticalSection (&screen_cs);
	}
}

static HWND blankwindows[MAX_DISPLAYS];
static void closeblankwindows (void)
{
	for (int i = 0; i < MAX_DISPLAYS; i++) {
		HWND h = blankwindows[i];
		if (h) {
			ShowWindow (h, SW_HIDE);
			DestroyWindow (h);
			blankwindows[i] = NULL;
		}
	}
}
static void createblankwindows (void)
{
	struct MultiDisplay *mdx = getdisplay (&currprefs);
	int i;

	if (!currprefs.win32_blankmonitors)
		return;

	for (i = 0; Displays[i].monitorname; i++) {
		struct MultiDisplay *md = &Displays[i];
		TCHAR name[100];
		if (mdx == md)
			continue;
		_stprintf (name, _T("WinUAE_Blank_%d"), i);
		blankwindows[i] = CreateWindowEx (
			WS_EX_TOPMOST,
			_T("Blank"), name,
			WS_POPUP | WS_VISIBLE,
			md->rect.left, md->rect.top, md->rect.right - md->rect.left, md->rect.bottom - md->rect.top,
			NULL,
			NULL, hInst, NULL);
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
	closeblankwindows ();
	deletestatusline();
	if (hStatusWnd) {
		ShowWindow (hStatusWnd, SW_HIDE);
		DestroyWindow (hStatusWnd);
		hStatusWnd = 0;
	}
	if (hAmigaWnd) {
		addnotifications (hAmigaWnd, TRUE, FALSE);
#ifdef D3D
		D3D_free (true);
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
		currentmode->current_width = (int)(picasso96_state.Width * currprefs.rtg_horiz_zoom_mult);
		currentmode->current_height = (int)(picasso96_state.Height * currprefs.rtg_vert_zoom_mult);
		currprefs.gfx_apmode[1].gfx_interlaced = false;
		if (currprefs.win32_rtgvblankrate == 0) {
			currprefs.gfx_apmode[1].gfx_refreshrate = currprefs.gfx_apmode[0].gfx_refreshrate;
			if (currprefs.gfx_apmode[0].gfx_interlaced) {
				currprefs.gfx_apmode[1].gfx_refreshrate *= 2;
			}
		} else if (currprefs.win32_rtgvblankrate < 0) {
			currprefs.gfx_apmode[1].gfx_refreshrate = 0;
		} else {
			currprefs.gfx_apmode[1].gfx_refreshrate = currprefs.win32_rtgvblankrate;
		}
		if (currprefs.gfx_apmode[1].gfx_vsync)
			currentmode->vsync = 1 + currprefs.gfx_apmode[1].gfx_vsyncmode;
	} else {
#endif
		currentmode->current_width = currprefs.gfx_size.width;
		currentmode->current_height = currprefs.gfx_size.height;
		if (currprefs.gfx_apmode[0].gfx_vsync)
			currentmode->vsync = 1 + currprefs.gfx_apmode[0].gfx_vsyncmode;
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
			if ((currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER || currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_SCALE || currprefs.win32_rtgallowscaling) && (picasso96_state.Width != currentmode->native_width || picasso96_state.Height != currentmode->native_height))
				scalepicasso = 1;
			if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER)
				scalepicasso = currprefs.gf[1].gfx_filter_autoscale;
			if (!scalepicasso && currprefs.win32_rtgscaleaspectratio)
				scalepicasso = -1;
		} else if (isfullscreen () > 0) {
			if (!currprefs.win32_rtgmatchdepth) { // can't scale to different color depth
				if (currentmode->native_width > picasso96_state.Width && currentmode->native_height > picasso96_state.Height) {
					if (currprefs.gf[1].gfx_filter_autoscale)
						scalepicasso = 1;
				}
				if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER)
					scalepicasso = currprefs.gf[1].gfx_filter_autoscale;
				if (!scalepicasso && currprefs.win32_rtgscaleaspectratio)
					scalepicasso = -1;
			}
		} else if (isfullscreen () == 0) {
			if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_INTEGER_SCALE) {
				scalepicasso = RTG_MODE_INTEGER_SCALE;
				currentmode->current_width = currprefs.gfx_size.width;
				currentmode->current_height = currprefs.gfx_size.height;
			} else if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER) {
				if (currprefs.gfx_size.width < picasso96_state.Width || currprefs.gfx_size.height < picasso96_state.Height) {
					if (!currprefs.win32_rtgallowscaling) {
						;
					} else if (currprefs.win32_rtgscaleaspectratio) {
						scalepicasso = -1;
						currentmode->current_width = currprefs.gfx_size.width;
						currentmode->current_height = currprefs.gfx_size.height;
					}
				} else {
					scalepicasso = 2;
					currentmode->current_width = currprefs.gfx_size.width;
					currentmode->current_height = currprefs.gfx_size.height;
				}
			} else if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_SCALE) {
				if (currprefs.gfx_size.width > picasso96_state.Width || currprefs.gfx_size.height > picasso96_state.Height)
					scalepicasso = 1;
				if ((currprefs.gfx_size.width != picasso96_state.Width || currprefs.gfx_size.height != picasso96_state.Height) && currprefs.win32_rtgallowscaling) {
					scalepicasso = 1;
				} else if (currprefs.gfx_size.width < picasso96_state.Width || currprefs.gfx_size.height < picasso96_state.Height) {
					// no always scaling and smaller? Back to normal size
					currentmode->current_width = changed_prefs.gfx_size_win.width = picasso96_state.Width;
					currentmode->current_height = changed_prefs.gfx_size_win.height = picasso96_state.Height;
				} else if (currprefs.gfx_size.width == picasso96_state.Width || currprefs.gfx_size.height == picasso96_state.Height) {
					;
				} else if (!scalepicasso && currprefs.win32_rtgscaleaspectratio) {
					scalepicasso = -1;
				}
			} else {
				if ((currprefs.gfx_size.width != picasso96_state.Width || currprefs.gfx_size.height != picasso96_state.Height) && currprefs.win32_rtgallowscaling)
					scalepicasso = 1;
				if (!scalepicasso && currprefs.win32_rtgscaleaspectratio)
					scalepicasso = -1;
			}
		}

		if (scalepicasso > 0 && (currprefs.gfx_size.width != picasso96_state.Width || currprefs.gfx_size.height != picasso96_state.Height)) {
			currentmode->current_width = currprefs.gfx_size.width;
			currentmode->current_height = currprefs.gfx_size.height;
		}
	}

}

static int open_windows (bool mousecapture)
{
	static bool started = false;
	int ret, i;

	changevblankthreadmode (VBLANKTH_IDLE);

	inputdevice_unacquire ();
	wait_keyrelease ();
	reset_sound ();
	in_sizemove = 0;

	updatewinfsmode (&currprefs);
#ifdef D3D
	D3D_free (false);
#endif
#ifdef OPENGL
	OGL_free ();
#endif
	if (!DirectDraw_Start ())
		return 0;

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
			if (!DirectDraw_Start ())
				return 0;
		}
	} while (ret < 0);

	if (!ret) {
		DirectDraw_Release ();
		return ret;
	}

	bool startactive = (started && mouseactive) || (!started && !currprefs.win32_start_uncaptured && !currprefs.win32_start_minimized);
	bool startpaused = !started && ((currprefs.win32_start_minimized && currprefs.win32_iconified_pause) || (currprefs.win32_start_uncaptured && currprefs.win32_inactive_pause && isfullscreen () <= 0));
	bool startminimized = !started && currprefs.win32_start_minimized && isfullscreen () <= 0;
	int input = 0;

	if (mousecapture && startactive)
		setmouseactive (-1);

	int upd = 0;
	if (startactive) {
		setpriority (&priorities[currprefs.win32_active_capture_priority]);
		upd = 2;
	} else if (startminimized) {
		setpriority (&priorities[currprefs.win32_iconified_priority]);
		setminimized ();
		input = currprefs.win32_inactive_input;
		upd = 1;
	} else {
		setpriority (&priorities[currprefs.win32_inactive_priority]);
		input = currprefs.win32_inactive_input;
		upd = 2;
	}
	if (upd > 1) {
		for (i = 0; i < NUM_LEDS; i++)
			gui_flicker_led (i, -1, -1);
		gui_led (LED_POWER, gui_data.powerled, gui_data.powerled_brightness);
		gui_fps (0, 0, 0);
		if (gui_data.md >= 0)
			gui_led (LED_MD, 0, -1);
		for (i = 0; i < 4; i++) {
			if (currprefs.floppyslots[i].dfxtype >= 0)
				gui_led (LED_DF0 + i, 0, -1);
		}
	}
	if (upd > 0) {
		inputdevice_acquire(TRUE);
		if (!isfocus())
			inputdevice_unacquire(true, input);
	}

	if (startpaused)
		setpaused (1);

	started = true;
	return ret;
}

static void reopen_gfx (void)
{
	open_windows (false);

	if (isvsync () < 0)
		vblank_calibrate (0, false);

	if (isfullscreen () <= 0)
		DirectDraw_FillPrimary ();
}

static int getstatuswindowheight (void)
{
	int def = GetSystemMetrics (SM_CYMENU) + 3;
	WINDOWINFO wi;
	HWND h = CreateWindowEx (
		0, STATUSCLASSNAME, (LPCTSTR) NULL, SBARS_TOOLTIPS | WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, hHiddenWnd, (HMENU) 1, hInst, NULL);
	if (!h)
		return def;
	wi.cbSize = sizeof wi;
	if (!GetWindowInfo (h, &wi))
		return def;
	DestroyWindow (h);
	return wi.rcWindow.bottom - wi.rcWindow.top;
}

void graphics_reset(bool forced)
{
	if (forced) {
		display_change_requested = 2;
	} else {
		// full reset if display size can't changed.
		if (currprefs.gfx_api) {
			display_change_requested = 3;
		} else {
			display_change_requested = 2;
		}
	}
}

void WIN32GFX_DisplayChangeRequested (int mode)
{
	display_change_requested = mode;
}

int check_prefs_changed_gfx (void)
{
	int c = 0;

	if (!config_changed && !display_change_requested)
		return 0;

	c |= currprefs.win32_statusbar != changed_prefs.win32_statusbar ? 512 : 0;
	c |= currprefs.gfx_size_fs.width != changed_prefs.gfx_size_fs.width ? 16 : 0;
	c |= currprefs.gfx_size_fs.height != changed_prefs.gfx_size_fs.height ? 16 : 0;
	c |= ((currprefs.gfx_size_win.width + 7) & ~7) != ((changed_prefs.gfx_size_win.width + 7) & ~7) ? 16 : 0;
	c |= currprefs.gfx_size_win.height != changed_prefs.gfx_size_win.height ? 16 : 0;
#if 0
	c |= currprefs.gfx_size_win.x != changed_prefs.gfx_size_win.x ? 16 : 0;
	c |= currprefs.gfx_size_win.y != changed_prefs.gfx_size_win.y ? 16 : 0;
#endif
	c |= currprefs.color_mode != changed_prefs.color_mode ? 2 | 16 : 0;
	c |= currprefs.gfx_apmode[0].gfx_fullscreen != changed_prefs.gfx_apmode[0].gfx_fullscreen ? 16 : 0;
	c |= currprefs.gfx_apmode[1].gfx_fullscreen != changed_prefs.gfx_apmode[1].gfx_fullscreen ? 16 : 0;
	c |= currprefs.gfx_apmode[0].gfx_vsync != changed_prefs.gfx_apmode[0].gfx_vsync ? 2 | 16 : 0;
	c |= currprefs.gfx_apmode[1].gfx_vsync != changed_prefs.gfx_apmode[1].gfx_vsync ? 2 | 16 : 0;
	c |= currprefs.gfx_apmode[0].gfx_vsyncmode != changed_prefs.gfx_apmode[0].gfx_vsyncmode ? 2 | 16 : 0;
	c |= currprefs.gfx_apmode[1].gfx_vsyncmode != changed_prefs.gfx_apmode[1].gfx_vsyncmode ? 2 | 16 : 0;
	c |= currprefs.gfx_apmode[0].gfx_refreshrate != changed_prefs.gfx_apmode[0].gfx_refreshrate ? 2 | 16 : 0;
#if 0
	c |= currprefs.gfx_apmode[1].gfx_refreshrate != changed_prefs.gfx_apmode[1].gfx_refreshrate ? 2 | 16 : 0;
#endif
	c |= currprefs.gfx_autoresolution != changed_prefs.gfx_autoresolution ? (2|8|16) : 0;
	c |= currprefs.gfx_api != changed_prefs.gfx_api ? (1|8|32) : 0;
	c |= currprefs.lightboost_strobo != changed_prefs.lightboost_strobo ? (2|16) : 0;

	for (int j = 0; j < 2; j++) {
		struct gfx_filterdata *gf = &currprefs.gf[j];
		struct gfx_filterdata *gfc = &changed_prefs.gf[j];

		c |= gf->gfx_filter != gfc->gfx_filter ? (2|8) : 0;

		for (int i = 0; i <= 2 * MAX_FILTERSHADERS; i++) {
			c |= _tcscmp (gf->gfx_filtershader[i], gfc->gfx_filtershader[i]) ? (2|8) : 0;
			c |= _tcscmp (gf->gfx_filtermask[i], gfc->gfx_filtermask[i]) ? (2|8) : 0;
		}
		c |= _tcscmp (gf->gfx_filteroverlay, gfc->gfx_filteroverlay) ? (2|8) : 0;

		c |= gf->gfx_filter_scanlines != gfc->gfx_filter_scanlines ? (1|8) : 0;
		c |= gf->gfx_filter_scanlinelevel != gfc->gfx_filter_scanlinelevel ? (1|8) : 0;
		c |= gf->gfx_filter_scanlineratio != gfc->gfx_filter_scanlineratio ? (1|8) : 0;

		c |= gf->gfx_filter_horiz_zoom_mult != gfc->gfx_filter_horiz_zoom_mult ? (1) : 0;
		c |= gf->gfx_filter_vert_zoom_mult != gfc->gfx_filter_vert_zoom_mult ? (1) : 0;

		c |= gf->gfx_filter_filtermode != gfc->gfx_filter_filtermode ? (2|8) : 0;
		c |= gf->gfx_filter_bilinear != gfc->gfx_filter_bilinear ? (2|8) : 0;
		c |= gf->gfx_filter_noise != gfc->gfx_filter_noise ? (1) : 0;
		c |= gf->gfx_filter_blur != gfc->gfx_filter_blur ? (1) : 0;

		c |= gf->gfx_filter_aspect != gfc->gfx_filter_aspect ? (1) : 0;
		c |= gf->gfx_filter_keep_aspect != gfc->gfx_filter_keep_aspect ? (1) : 0;
		c |= gf->gfx_filter_keep_autoscale_aspect != gfc->gfx_filter_keep_autoscale_aspect ? (1) : 0;
		c |= gf->gfx_filter_luminance != gfc->gfx_filter_luminance ? (1) : 0;
		c |= gf->gfx_filter_contrast != gfc->gfx_filter_contrast ? (1) : 0;
		c |= gf->gfx_filter_saturation != gfc->gfx_filter_saturation ? (1) : 0;
		c |= gf->gfx_filter_gamma != gfc->gfx_filter_gamma ? (1) : 0;
		c |= gf->gfx_filter_integerscalelimit != gfc->gfx_filter_integerscalelimit ? (1) : 0;
		if (j && gf->gfx_filter_autoscale != gfc->gfx_filter_autoscale)
			c |= 8 | 64;
		//c |= gf->gfx_filter_ != gfc->gfx_filter_ ? (1|8) : 0;
	}

	c |= currprefs.rtg_horiz_zoom_mult != changed_prefs.rtg_horiz_zoom_mult ? (1) : 0;
	c |= currprefs.rtg_vert_zoom_mult != changed_prefs.rtg_vert_zoom_mult ? (1) : 0;

	c |= currprefs.gfx_luminance != changed_prefs.gfx_luminance ? (1 | 256) : 0;
	c |= currprefs.gfx_contrast != changed_prefs.gfx_contrast ? (1 | 256) : 0;
	c |= currprefs.gfx_gamma != changed_prefs.gfx_gamma ? (1 | 256) : 0;

	c |= currprefs.gfx_resolution != changed_prefs.gfx_resolution ? (128) : 0;
	c |= currprefs.gfx_vresolution != changed_prefs.gfx_vresolution ? (128) : 0;
	c |= currprefs.gfx_autoresolution_minh != changed_prefs.gfx_autoresolution_minh ? (128) : 0;
	c |= currprefs.gfx_autoresolution_minv != changed_prefs.gfx_autoresolution_minv ? (128) : 0;
	c |= currprefs.gfx_iscanlines != changed_prefs.gfx_iscanlines ? (2 | 8) : 0;
	c |= currprefs.gfx_pscanlines != changed_prefs.gfx_pscanlines ? (2 | 8) : 0;
	c |= currprefs.monitoremu != changed_prefs.monitoremu ? (2 | 8) : 0;
	c |= currprefs.genlock_image != changed_prefs.genlock_image ? (2 | 8) : 0;
	c |= currprefs.genlock != changed_prefs.genlock ? (2 | 8) : 0;
	c |= currprefs.genlock_mix != changed_prefs.genlock_mix ? (1 | 256) : 0;

	c |= currprefs.gfx_lores_mode != changed_prefs.gfx_lores_mode ? (2 | 8) : 0;
	c |= currprefs.gfx_scandoubler != changed_prefs.gfx_scandoubler ? (2 | 8) : 0;
	c |= currprefs.gfx_apmode[APMODE_NATIVE].gfx_display != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_display ? (2|4|8) : 0;
	c |= currprefs.gfx_apmode[APMODE_RTG].gfx_display != changed_prefs.gfx_apmode[APMODE_RTG].gfx_display ? (2|4|8) : 0;
	c |= currprefs.gfx_blackerthanblack != changed_prefs.gfx_blackerthanblack ? (2 | 8) : 0;
	c |= currprefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers ? (2 | 8) : 0;
	c |= currprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced ? (2 | 8) : 0;
	c |= currprefs.gfx_apmode[APMODE_RTG].gfx_backbuffers != changed_prefs.gfx_apmode[APMODE_RTG].gfx_backbuffers ? (2 | 8) : 0;

	c |= currprefs.win32_alwaysontop != changed_prefs.win32_alwaysontop ? 32 : 0;
	c |= currprefs.win32_notaskbarbutton != changed_prefs.win32_notaskbarbutton ? 32 : 0;
	c |= currprefs.win32_nonotificationicon != changed_prefs.win32_nonotificationicon ? 32 : 0;
	c |= currprefs.win32_borderless != changed_prefs.win32_borderless ? 32 : 0;
	c |= currprefs.win32_blankmonitors != changed_prefs.win32_blankmonitors ? 32 : 0;
	c |= currprefs.win32_rtgmatchdepth != changed_prefs.win32_rtgmatchdepth ? 2 : 0;
//	c |= currprefs.win32_rtgscalemode != changed_prefs.win32_rtgscalemode ? (2 | 8 | 64) : 0;
	c |= currprefs.win32_rtgallowscaling != changed_prefs.win32_rtgallowscaling ? (2 | 8 | 64) : 0;
	c |= currprefs.win32_rtgscaleaspectratio != changed_prefs.win32_rtgscaleaspectratio ? (8 | 64) : 0;
	c |= currprefs.win32_rtgvblankrate != changed_prefs.win32_rtgvblankrate ? 8 : 0;


	if (display_change_requested || c)
	{
		bool setpause = false;
		bool dontcapture = false;
		int keepfsmode = 
			currprefs.gfx_apmode[0].gfx_fullscreen == changed_prefs.gfx_apmode[0].gfx_fullscreen && 
			currprefs.gfx_apmode[1].gfx_fullscreen == changed_prefs.gfx_apmode[1].gfx_fullscreen;
		cfgfile_configuration_change (1);

		currprefs.gfx_autoresolution = changed_prefs.gfx_autoresolution;
		currprefs.color_mode = changed_prefs.color_mode;
		currprefs.gfx_api = changed_prefs.gfx_api;
		currprefs.lightboost_strobo = changed_prefs.lightboost_strobo;

		if (changed_prefs.gfx_apmode[0].gfx_fullscreen == GFX_FULLSCREEN) { 
			if (currprefs.gfx_api != changed_prefs.gfx_api)
				display_change_requested = 1;
		}

		if (display_change_requested) {
			if (display_change_requested == 3) {
				c = 1024;
			} else if (display_change_requested == 2) {
				c = 512;
			} else {
				c = 2;
				keepfsmode = 0;
				if (display_change_requested <= -1) {
					dontcapture = true;
					if (display_change_requested == -2)
						setpause = true;
					if (pause_emulation)
						setpause = true;
				}
			}
			display_change_requested = 0;
		}

		for (int j = 0; j < 2; j++) {
			struct gfx_filterdata *gf = &currprefs.gf[j];
			struct gfx_filterdata *gfc = &changed_prefs.gf[j];
			memcpy(gf, gfc, sizeof(struct gfx_filterdata));
		}

		currprefs.rtg_horiz_zoom_mult = changed_prefs.rtg_horiz_zoom_mult;
		currprefs.rtg_vert_zoom_mult = changed_prefs.rtg_vert_zoom_mult;

		currprefs.gfx_luminance = changed_prefs.gfx_luminance;
		currprefs.gfx_contrast = changed_prefs.gfx_contrast;
		currprefs.gfx_gamma = changed_prefs.gfx_gamma;

		currprefs.gfx_resolution = changed_prefs.gfx_resolution;
		currprefs.gfx_vresolution = changed_prefs.gfx_vresolution;
		currprefs.gfx_autoresolution_minh = changed_prefs.gfx_autoresolution_minh;
		currprefs.gfx_autoresolution_minv = changed_prefs.gfx_autoresolution_minv;
		currprefs.gfx_iscanlines = changed_prefs.gfx_iscanlines;
		currprefs.gfx_pscanlines = changed_prefs.gfx_pscanlines;
		currprefs.monitoremu = changed_prefs.monitoremu;
		currprefs.genlock_image = changed_prefs.genlock_image;
		currprefs.genlock = changed_prefs.genlock;
		currprefs.genlock_mix = changed_prefs.genlock_mix;

		currprefs.gfx_lores_mode = changed_prefs.gfx_lores_mode;
		currprefs.gfx_scandoubler = changed_prefs.gfx_scandoubler;
		currprefs.gfx_apmode[APMODE_NATIVE].gfx_display = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_display;
		currprefs.gfx_apmode[APMODE_RTG].gfx_display = changed_prefs.gfx_apmode[APMODE_RTG].gfx_display;
		currprefs.gfx_blackerthanblack = changed_prefs.gfx_blackerthanblack;
		currprefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers;
		currprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced;
		currprefs.gfx_apmode[APMODE_RTG].gfx_backbuffers = changed_prefs.gfx_apmode[APMODE_RTG].gfx_backbuffers;

		currprefs.win32_alwaysontop = changed_prefs.win32_alwaysontop;
		currprefs.win32_nonotificationicon = changed_prefs.win32_nonotificationicon;
		currprefs.win32_notaskbarbutton = changed_prefs.win32_notaskbarbutton;
		currprefs.win32_borderless = changed_prefs.win32_borderless;
		currprefs.win32_blankmonitors = changed_prefs.win32_blankmonitors;
		currprefs.win32_statusbar = changed_prefs.win32_statusbar;
		currprefs.win32_rtgmatchdepth = changed_prefs.win32_rtgmatchdepth;
//		currprefs.win32_rtgscalemode = changed_prefs.win32_rtgscalemode;
		currprefs.win32_rtgallowscaling = changed_prefs.win32_rtgallowscaling;
		currprefs.win32_rtgscaleaspectratio = changed_prefs.win32_rtgscaleaspectratio;
		currprefs.win32_rtgvblankrate = changed_prefs.win32_rtgvblankrate;

		bool unacquired = false;
		if (c & 64) {
			if (!unacquired) {
				inputdevice_unacquire ();
				unacquired = true;
			}
			DirectDraw_Fill (NULL, 0);
			DirectDraw_BlitToPrimary (NULL);
		}
		if (c & 256) {
			init_colors ();
			reset_drawing ();
		}
		if (c & 128) {
			if (currprefs.gfx_autoresolution) {
				c |= 2 | 8;
			} else {
				c |= 16;
				reset_drawing ();
				S2X_reset ();
			}
		}
		if (c & 1024) {
			target_graphics_buffer_update();
		}
		if (c & 512) {
			reopen_gfx ();
			graphics_mode_changed = 1;
		}
		if ((c & 16) || ((c & 8) && keepfsmode)) {
			if (reopen (c & 2, unacquired == false)) {
				c |= 2;
			} else {
				unacquired = true;
			}
			graphics_mode_changed = 1;
		}
		if ((c & 32) || ((c & 2) && !keepfsmode)) {
			if (!unacquired) {
				inputdevice_unacquire ();
				unacquired = true;
			}
			close_windows ();
			graphics_init (dontcapture ? false : true);
			graphics_mode_changed = 1;
		}
		init_custom ();
		if (c & 4) {
			pause_sound ();
			reset_sound ();
			resume_sound ();
		}
		
		if (setpause || dontcapture) {
			if (!unacquired)
				inputdevice_unacquire ();
			unacquired = false;
		}

		if (unacquired)
			inputdevice_acquire (TRUE);

		if (setpause)
			setpaused (1);

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
		init_hz_normal();
	}
	if (currprefs.chipset_refreshrate != changed_prefs.chipset_refreshrate) {
		currprefs.chipset_refreshrate = changed_prefs.chipset_refreshrate;
		init_hz_normal();
		return 1;
	}

	if (currprefs.gf[0].gfx_filter_autoscale != changed_prefs.gf[0].gfx_filter_autoscale ||
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
		currprefs.gf[0].gfx_filter_autoscale = changed_prefs.gf[0].gfx_filter_autoscale;

		get_custom_limits (NULL, NULL, NULL, NULL, NULL);
		fixup_prefs_dimensions (&changed_prefs);

		return 1;
	}

	currprefs.win32_norecyclebin = changed_prefs.win32_norecyclebin;
	currprefs.filesys_limit = changed_prefs.filesys_limit;

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
		currprefs.win32_active_capture_priority != changed_prefs.win32_active_capture_priority ||
		currprefs.win32_inactive_priority != changed_prefs.win32_inactive_priority ||
		currprefs.win32_iconified_priority != changed_prefs.win32_iconified_priority ||
		currprefs.win32_active_nocapture_nosound != changed_prefs.win32_active_nocapture_nosound ||
		currprefs.win32_active_nocapture_pause != changed_prefs.win32_active_nocapture_pause ||
		currprefs.win32_inactive_nosound != changed_prefs.win32_inactive_nosound ||
		currprefs.win32_inactive_pause != changed_prefs.win32_inactive_pause ||
		currprefs.win32_inactive_input != changed_prefs.win32_inactive_input ||
		currprefs.win32_iconified_nosound != changed_prefs.win32_iconified_nosound ||
		currprefs.win32_iconified_pause != changed_prefs.win32_iconified_pause ||
		currprefs.win32_iconified_input != changed_prefs.win32_iconified_input ||
		currprefs.win32_ctrl_F11_is_quit != changed_prefs.win32_ctrl_F11_is_quit ||
		currprefs.right_control_is_right_win_key != changed_prefs.right_control_is_right_win_key)
	{
		currprefs.win32_minimize_inactive = changed_prefs.win32_minimize_inactive;
		currprefs.leds_on_screen = changed_prefs.leds_on_screen;
		currprefs.keyboard_leds[0] = changed_prefs.keyboard_leds[0];
		currprefs.keyboard_leds[1] = changed_prefs.keyboard_leds[1];
		currprefs.keyboard_leds[2] = changed_prefs.keyboard_leds[2];
		currprefs.win32_middle_mouse = changed_prefs.win32_middle_mouse;
		currprefs.win32_active_capture_priority = changed_prefs.win32_active_capture_priority;
		currprefs.win32_inactive_priority = changed_prefs.win32_inactive_priority;
		currprefs.win32_iconified_priority = changed_prefs.win32_iconified_priority;
		currprefs.win32_active_nocapture_nosound = changed_prefs.win32_active_nocapture_nosound;
		currprefs.win32_active_nocapture_pause = changed_prefs.win32_active_nocapture_pause;
		currprefs.win32_inactive_nosound = changed_prefs.win32_inactive_nosound;
		currprefs.win32_inactive_pause = changed_prefs.win32_inactive_pause;
		currprefs.win32_inactive_input = changed_prefs.win32_inactive_input;
		currprefs.win32_iconified_nosound = changed_prefs.win32_iconified_nosound;
		currprefs.win32_iconified_pause = changed_prefs.win32_iconified_pause;
		currprefs.win32_iconified_input = changed_prefs.win32_iconified_input;
		currprefs.win32_ctrl_F11_is_quit = changed_prefs.win32_ctrl_F11_is_quit;
		currprefs.right_control_is_right_win_key = changed_prefs.right_control_is_right_win_key;
		inputdevice_unacquire ();
		currprefs.keyboard_leds_in_use = changed_prefs.keyboard_leds_in_use = (currprefs.keyboard_leds[0] | currprefs.keyboard_leds[1] | currprefs.keyboard_leds[2]) != 0;
		pause_sound ();
		resume_sound ();
		inputdevice_acquire (TRUE);
#ifndef	_DEBUG
		setpriority (&priorities[currprefs.win32_active_capture_priority]);
#endif
		return 1;
	}

	if (currprefs.win32_samplersoundcard != changed_prefs.win32_samplersoundcard ||
		currprefs.sampler_stereo != changed_prefs.sampler_stereo) {
		currprefs.win32_samplersoundcard = changed_prefs.win32_samplersoundcard;
		currprefs.sampler_stereo = changed_prefs.sampler_stereo;
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
		currprefs.win32_midioutdev != changed_prefs.win32_midioutdev ||
		currprefs.win32_midirouter != changed_prefs.win32_midirouter)
	{
		currprefs.win32_midiindev = changed_prefs.win32_midiindev;
		currprefs.win32_midioutdev = changed_prefs.win32_midioutdev;
		currprefs.win32_midirouter = changed_prefs.win32_midirouter;
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
	if (currentmode->flags & DM_D3D) {
		D3D_getpixelformat (currentmode->current_depth,
			&red_bits, &green_bits, &blue_bits, &red_shift, &green_shift, &blue_shift, &alpha_bits, &alpha_shift, &alpha);
	} else {
		red_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (red_mask));
		green_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (green_mask));
		blue_bits = bits_in_mask (DirectDraw_GetPixelFormatBitMask (blue_mask));
		red_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (red_mask));
		green_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (green_mask));
		blue_shift = mask_shift (DirectDraw_GetPixelFormatBitMask (blue_mask));
		alpha_bits = 0;
		alpha_shift = 0;
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
			//write_log (_T("%d:%08x\n"), i, v);
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
	open_windows (true);
}

static int ifs (struct uae_prefs *p)
{
	int idx = screen_is_picasso ? 1 : 0;
	return p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLWINDOW ? -1 : 0);
}

static int reopen (int full, bool unacquire)
{
	int quick = 0;
	int idx = screen_is_picasso ? 1 : 0;
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];

	updatewinfsmode (&changed_prefs);

	if (changed_prefs.gfx_apmode[0].gfx_fullscreen != currprefs.gfx_apmode[0].gfx_fullscreen && !screen_is_picasso)
		full = 1;
	if (changed_prefs.gfx_apmode[1].gfx_fullscreen != currprefs.gfx_apmode[1].gfx_fullscreen && screen_is_picasso)
		full = 1;

	/* fullscreen to fullscreen? */
	if (isfullscreen () > 0 && currprefs.gfx_apmode[0].gfx_fullscreen == changed_prefs.gfx_apmode[0].gfx_fullscreen &&
		currprefs.gfx_apmode[1].gfx_fullscreen == changed_prefs.gfx_apmode[1].gfx_fullscreen && currprefs.gfx_apmode[0].gfx_fullscreen == GFX_FULLSCREEN) {
			quick = 1;
	}
	/* windowed to windowed */
	if (isfullscreen () <= 0 && currprefs.gfx_apmode[0].gfx_fullscreen == changed_prefs.gfx_apmode[0].gfx_fullscreen &&
		currprefs.gfx_apmode[1].gfx_fullscreen == changed_prefs.gfx_apmode[1].gfx_fullscreen) {
			quick = 1;
	}

	currprefs.gfx_size_fs.width = changed_prefs.gfx_size_fs.width;
	currprefs.gfx_size_fs.height = changed_prefs.gfx_size_fs.height;
	currprefs.gfx_size_win.width = changed_prefs.gfx_size_win.width;
	currprefs.gfx_size_win.height = changed_prefs.gfx_size_win.height;
	currprefs.gfx_size_win.x = changed_prefs.gfx_size_win.x;
	currprefs.gfx_size_win.y = changed_prefs.gfx_size_win.y;
	currprefs.gfx_apmode[0].gfx_fullscreen = changed_prefs.gfx_apmode[0].gfx_fullscreen;
	currprefs.gfx_apmode[1].gfx_fullscreen = changed_prefs.gfx_apmode[1].gfx_fullscreen;
	currprefs.gfx_apmode[0].gfx_vsync = changed_prefs.gfx_apmode[0].gfx_vsync;
	currprefs.gfx_apmode[1].gfx_vsync = changed_prefs.gfx_apmode[1].gfx_vsync;
	currprefs.gfx_apmode[0].gfx_vsyncmode = changed_prefs.gfx_apmode[0].gfx_vsyncmode;
	currprefs.gfx_apmode[1].gfx_vsyncmode = changed_prefs.gfx_apmode[1].gfx_vsyncmode;
	currprefs.gfx_apmode[0].gfx_refreshrate = changed_prefs.gfx_apmode[0].gfx_refreshrate;
#if 0
	currprefs.gfx_apmode[1].gfx_refreshrate = changed_prefs.gfx_apmode[1].gfx_refreshrate;
#endif
	set_config_changed ();

	if (!quick)
		return 1;

	if (unacquire) {
		inputdevice_unacquire ();
	}

	reopen_gfx ();

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
	bool preferdouble = 0, preferlace = 0;
	bool lace = false;

	if (currprefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate > 85) {
		preferdouble = 1;
	} else if (currprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced) {
		preferlace = 1;
	}

	if (hz >= 55)
		hz = 60;
	else
		hz = 50;

	newh = h * (currprefs.ntscmode ? 60 : 50) / hz;

	found = NULL;
	for (cnt = 0; cnt <= abs (newh - h) + 1 && !found; cnt++) {
		for (int dbl = 0; dbl < 2 && !found; dbl++) {
			bool doublecheck = false;
			bool lacecheck = false;
			if (preferdouble && dbl == 0)
				doublecheck = true;
			else if (preferlace && dbl == 0)
				lacecheck = true;

			for (int extra = 1; extra >= -1 && !found; extra--) {
				for (i = 0; md->DisplayModes[i].depth >= 0 && !found; i++) {
					struct PicassoResolution *r = &md->DisplayModes[i];
					if (r->res.width == w && (r->res.height == newh + cnt || r->res.height == newh - cnt) && r->depth == d) {
						int j;
						for (j = 0; r->refresh[j] > 0; j++) {
							if (doublecheck) {
								if (r->refreshtype[j] & REFRESH_RATE_LACE)
									continue;
								if (r->refresh[j] == hz * 2 + extra) {
									found = r;
									hz = r->refresh[j];
									break;
								}
							} else if (lacecheck) {
								if (!(r->refreshtype[j] & REFRESH_RATE_LACE))
									continue;
								if (r->refresh[j] * 2 == hz + extra) {
									found = r;
									lace = true;
									hz = r->refresh[j];
									break;
								}
							} else {
								if (r->refresh[j] == hz + extra) {
									found = r;
									hz = r->refresh[j];
									break;
								}
							}
						}
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
		changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_vsync = 0;
		if (currprefs.gfx_apmode[APMODE_NATIVE].gfx_vsync != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_vsync) {
			set_config_changed ();
		}
		write_log (_T("refresh rate changed to %d%s but no matching screenmode found, vsync disabled\n"), hz, lace ? _T("i") : _T("p"));
		return false;
	} else {
		newh = found->res.height;
		changed_prefs.gfx_size_fs.height = newh;
		changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate = hz;
		changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced = lace;
		if (changed_prefs.gfx_size_fs.height != currprefs.gfx_size_fs.height ||
			changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate != currprefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate) {
			write_log (_T("refresh rate changed to %d%s, new screenmode %dx%d\n"), hz, lace ? _T("i") : _T("p"), w, newh);
			set_config_changed ();
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
				if ((currprefs.gf[1].gfx_filter_autoscale == 1 || (currprefs.gf[1].gfx_filter_autoscale == 2 && currprefs.win32_rtgallowscaling)) && !currprefs.win32_rtgmatchdepth)
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
			if (!gfxvidinfo.outbuffer->bufmem_lockable)
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
			if (currprefs.gf[1].gfx_filter_autoscale && ((wc->native_width > picasso96_state.Width && wc->native_height >= picasso96_state.Height) || (wc->native_height > picasso96_state.Height && wc->native_width >= picasso96_state.Width)))
				return -1;
			if (currprefs.win32_rtgallowscaling && (picasso96_state.Width != wc->native_width || picasso96_state.Height != wc->native_height))
				return -1;
#if 0
			if (wc->native_width < picasso96_state.Width || wc->native_height < picasso96_state.Height)
				return 1;
#endif
		}
		return -1;
	}
	return 0;
}

void gfx_set_picasso_state (int on)
{
	struct winuae_currentmode wc;
	struct apmode *newmode, *oldmode;
	struct gfx_filterdata *newf, *oldf;
	int mode;

	if (screen_is_picasso == on)
		return;
	screen_is_picasso = on;
	rp_rtg_switch ();
	memcpy (&wc, currentmode, sizeof (wc));

	newmode = &currprefs.gfx_apmode[on ? 1 : 0];
	oldmode = &currprefs.gfx_apmode[on ? 0 : 1];

	newf = &currprefs.gf[on ? 1 : 0];
	oldf = &currprefs.gf[on ? 0 : 1];

	updatemodes ();
	update_gfxparams ();
	clearscreen ();

	// if filter changes, need to reset
	mode = 0;
	if (newf->gfx_filter != oldf->gfx_filter)
		mode = -1;
	for (int i = 0; i <= 2 * MAX_FILTERSHADERS; i++) {
		if (_tcscmp(newf->gfx_filtershader[i], oldf->gfx_filtershader[i]))
			mode = -1;
		if (_tcscmp(newf->gfx_filtermask[i], oldf->gfx_filtermask[i]))
			mode = -1;
	}
	// if screen parameter changes, need to reopen window
	if (newmode->gfx_fullscreen != oldmode->gfx_fullscreen ||
		(newmode->gfx_fullscreen && (
			newmode->gfx_backbuffers != oldmode->gfx_backbuffers ||
			newmode->gfx_display != oldmode->gfx_display ||
			newmode->gfx_refreshrate != oldmode->gfx_refreshrate ||
			newmode->gfx_strobo != oldmode->gfx_strobo ||
			newmode->gfx_vflip != oldmode->gfx_vflip ||
			newmode->gfx_vsync != oldmode->gfx_vsync))) {
		mode = 1;
	}
	if (mode <= 0) {
		int m = modeswitchneeded (&wc);
		if (m > 0)
			mode = m;
		if (m < 0 && !mode)
			mode = m;
		if (!mode)
			goto end;
	}
	if (mode < 0) {
		open_windows (true);
	} else {
		open_screen (); // reopen everything
	}
	if (on && isvsync_rtg () < 0)
		vblank_calibrate (0, false);
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
		open_windows (true);
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
	if (currprefs.gf[picasso_on].gfx_filter > 0) {
		int i = 0;
		while (uaefilters[i].name) {
			if (uaefilters[i].type == currprefs.gf[picasso_on].gfx_filter) {
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
	systray (hHiddenWnd, FALSE);
	return 1;
}

void machdep_free (void)
{
#ifdef LOGITECHLCD
	lcd_close ();
#endif
}

int graphics_init (bool mousecapture)
{
	systray (hHiddenWnd, TRUE);
	systray (hHiddenWnd, FALSE);
	gfxmode_reset ();
	graphics_mode_changed = 1;
	return open_windows (mousecapture);
}

int graphics_setup (void)
{
	InitializeCriticalSection (&screen_cs);
#ifdef PICASSO96
	InitPicasso96 ();
#endif
	return 1;
}

void graphics_leave (void)
{
	changevblankthreadmode (VBLANKTH_KILL);
	close_windows ();
}

uae_u32 OSDEP_minimize_uae (void)
{
	return ShowWindow (hAmigaWnd, SW_MINIMIZE);
}

typedef HRESULT (CALLBACK* DWMENABLEMMCSS)(BOOL);
static void setDwmEnableMMCSS (bool state)
{
	if (!os_vista)
		return;
	DWMENABLEMMCSS pDwmEnableMMCSS;
	pDwmEnableMMCSS = (DWMENABLEMMCSS)GetProcAddress (
		GetModuleHandle (_T("dwmapi.dll")), "DwmEnableMMCSS");
	if (pDwmEnableMMCSS)
		pDwmEnableMMCSS (state);
}

void close_windows (void)
{
	changevblankthreadmode (VBLANKTH_IDLE);
	waitflipevent ();
	setDwmEnableMMCSS (FALSE);
	reset_sound ();
#if defined (GFXFILTER)
	S2X_free ();
#endif
	freevidbuffer (&gfxvidinfo.drawbuffer);
	freevidbuffer (&gfxvidinfo.tempbuffer);
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
	int num_parts = 11 + joys + 1;
	double scaleX, scaleY;
	WINDOWINFO wi;
	int extra;

	if (hStatusWnd) {
		ShowWindow (hStatusWnd, SW_HIDE);
		DestroyWindow (hStatusWnd);
	}
	if (currprefs.win32_statusbar == 0)
		return;
	if (isfullscreen () != 0)
		return;
	if (currprefs.win32_borderless)
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
	if (is_ppc_cpu(&currprefs))
		idle_width += (int)(68 * scaleX);
	if (is_x86_cpu(&currprefs))
		idle_width += (int)(68 * scaleX);
	snd_width = (int)(72 * scaleX);
	joy_width = (int)(24 * scaleX);
	GetClientRect (hMainWnd, &rc);
	/* Allocate an array for holding the right edge coordinates. */
	hloc = LocalAlloc (LHND, sizeof (int) * (num_parts + 1));
	if (hloc) {
		int i = 0, i1, j;
		lpParts = (LPINT)LocalLock (hloc);
		// left side, msg area
		lpParts[i] = rc.left + 2;
		i++;
		window_led_msg_start = i;
		/* Calculate the right edge coordinate for each part, and copy the coords to the array.  */
		int startx = rc.right - (drive_width * 4) - power_width - idle_width - fps_width - cd_width - hd_width - snd_width - joys * joy_width - extra;
		for (j = 0; j < joys; j++) {
			lpParts[i] = startx;
			i++;
			startx += joy_width;
		}
		window_led_joy_start = i;
		if (lpParts[0] >= startx)
			lpParts[0] = startx - 1;
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

		window_led_msg = lpParts[window_led_msg_start - 1];
		window_led_msg_end = lpParts[window_led_msg_start - 1 + 1];
		window_led_joys = lpParts[window_led_joy_start - joys];
		window_led_joys_end = lpParts[window_led_joy_start - joys + 1];
		window_led_hd = lpParts[i1];
		window_led_hd_end = lpParts[i1 + 1];
		window_led_drives = lpParts[i1 + 2];
		window_led_drives_end = lpParts[i1 + 6];

		/* Create the parts */
		SendMessage (hStatusWnd, SB_SETPARTS, (WPARAM)num_parts, (LPARAM)lpParts);
		LocalUnlock (hloc);
		LocalFree (hloc);
	}
	registertouch(hStatusWnd);
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
		write_log (_T("RegisterDeviceNotification failed: %d\n"), GetLastError());
		return FALSE;
	}

	return TRUE;
}
#endif

static int getbestmode (int nextbest)
{
	int i, startidx;
	struct MultiDisplay *md;
	int ratio;
	int index = -1;

	for(;;) {
		md = getdisplay2 (&currprefs, index);
		if (!md)
			return 0;
		ratio = currentmode->native_width > currentmode->native_height ? 1 : 0;
		for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
			struct PicassoResolution *pr = &md->DisplayModes[i];
			if (pr->res.width == currentmode->native_width && pr->res.height == currentmode->native_height)
				break;
		}
		if (md->DisplayModes[i].depth >= 0) {
			if (!nextbest)
				break;
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
				write_log (_T("FS: %dx%d -> %dx%d %d %d\n"), currentmode->native_width, currentmode->native_height,
					pr->res.width, pr->res.height, ratio, index);
				currentmode->native_width = pr->res.width;
				currentmode->native_height = pr->res.height;
				currentmode->current_width = currentmode->native_width;
				currentmode->current_height = currentmode->native_height;
				goto end;
			}
		}
		// still not match? check all modes
		i = startidx;
		for (; md->DisplayModes[i].depth >= 0; i++) {
			struct PicassoResolution *pr = &md->DisplayModes[i];
			int r = pr->res.width > pr->res.height ? 1 : 0;
			if (pr->res.width >= currentmode->native_width && pr->res.height >= currentmode->native_height) {
				write_log (_T("FS: %dx%d -> %dx%d\n"), currentmode->native_width, currentmode->native_height,
					pr->res.width, pr->res.height);
				currentmode->native_width = pr->res.width;
				currentmode->native_height = pr->res.height;
				currentmode->current_width = currentmode->native_width;
				currentmode->current_height = currentmode->native_height;
				goto end;
			}
		}
		index++;
	}
end:
	if (index >= 0) {
		currprefs.gfx_apmode[screen_is_picasso ? APMODE_RTG : APMODE_NATIVE].gfx_display = 
			changed_prefs.gfx_apmode[screen_is_picasso ? APMODE_RTG : APMODE_NATIVE].gfx_display = index;
		write_log (L"Can't find mode %dx%d ->\n", currentmode->native_width, currentmode->native_height);
		write_log (L"Monitor switched to '%s'\n", md->adaptername);
	}
	return 1;
}

static volatile frame_time_t vblank_prev_time, vblank_real_prev_time, thread_vblank_time;
static volatile int vblank_found_flipdelay;

#include <process.h>

double getcurrentvblankrate (void)
{
	if (remembered_vblank)
		return remembered_vblank;
	if (currprefs.gfx_api)
		return D3D_getrefreshrate ();
	else
		return DirectDraw_CurrentRefreshRate ();
}

static int maxscanline, minscanline, prevvblankpos;

static bool getvblankpos (int *vp, bool updateprev)
{
	int sl;
#if 0
	frame_time_t t = read_processor_time ();
#endif
	*vp = -2;
	if (currprefs.gfx_api) {
		if (!D3D_getvblankpos (&sl))
			return false;
	} else {
		if (!DD_getvblankpos (&sl))
			return false;
	}
#if 0
	t = read_processor_time () - t;
	write_log (_T("(%d:%d)"), t, sl);
#endif
	if (updateprev && sl > prevvblankpos)
		prevvblankpos = sl;
	if (sl > maxscanline)
		maxscanline = sl;
	if (sl > 0) {
		if (sl < minscanline || minscanline < 0)
			minscanline = sl;
	}
	*vp = sl;
	return true;
}

static bool getvblankpos2 (int *vp, int *flags, bool updateprev)
{
	if (!getvblankpos (vp, updateprev))
		return false;
	if (*vp > 100 && flags) {
		if ((*vp) & 1)
			*flags |= 2;
		else
			*flags |= 1;
	}
	return true;
}

static bool waitvblankstate (bool state, int *maxvpos, int *flags)
{
	int vp;
	int count = 0;
	if (flags)
		*flags = 0;
	uae_u32 t = getlocaltime () + 5;
	for (;;) {
		int omax = maxscanline;
		if (!getvblankpos2 (&vp, flags, true))
			return false;
		while (omax != maxscanline) {
			omax = maxscanline;
			if (!getvblankpos2 (&vp, flags, true))
				return false;
		}
		if (maxvpos)
			*maxvpos = maxscanline;
		if (vp < 0) {
			if (state)
				return true;
		} else {
			if (!state)
				return true;
		}
		count--;
		if (count < 0) {
			if (getlocaltime () > t)
				return false;
			count = 1000;
		}
	}
}

static int timezeroonevblank (int startline, int endline)
{
	int vp;
	for (;;) {
		if (!getvblankpos (&vp, false))
			return -1;
		if (vp > endline)
			break;
	}
	for (;;) {
		if (!getvblankpos (&vp, false))
			return -1;
		if (vp == startline)
			break;
	}
	frame_time_t start = read_processor_time ();
	for (;;) {
		if (!getvblankpos (&vp, false))
			return -1;
		if (vp >= endline)
			break;
	}
	frame_time_t end = read_processor_time ();
	return end - start;
}

static int vblank_wait (void)
{
	int vp;

	for (;;) {
		int opos = prevvblankpos;
		if (!getvblankpos (&vp, true))
			return -2;
		if (opos >= 0 && opos > (maxscanline + minscanline) / 2 && vp < (maxscanline + minscanline) / 3)
			return vp;
		if (vp <= 0)
			return vp;
		vsync_sleep (true);
	}
}

static bool vblank_getstate (bool *state, int *pvp)
{
	int vp, opos;

	*state = false;
	opos = prevvblankpos;
	if (!getvblankpos (&vp, true))
		return false;
	if (pvp)
		*pvp = vp;
	if (opos >= 0 && opos > (maxscanline + minscanline) / 2 && vp < (maxscanline + minscanline) / 3) {
		*state = true;
		return true;
	}
	if (opos > vp && vp <= 0) {
		*state = true;
		return true;
	}
	*state = false;
	return true;
}
static bool vblank_getstate (bool *state)
{
	return vblank_getstate (state, NULL);
}

void vblank_reset (double freq)
{
	if (currprefs.gfx_api)
		D3D_vblank_reset (freq);
	else
		DD_vblank_reset (freq);
}

static unsigned int __stdcall flipthread (void *dummy)
{
	SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_HIGHEST);
	while (flipthread_mode) {
		WaitForSingleObject (flipevent, INFINITE);
		if (flipthread_mode == 0)
			break;
		frame_time_t t = read_processor_time ();
		while ((flipevent_mode & 1) && !render_ok) {
			sleep_millis (1);
			if (read_processor_time () - t > vblankbasefull)
				break;
		}
		if (flipevent_mode & 1) {
			show_screen (0);
			render_ok = false;
		}
		if (flipevent_mode & 2) {
			show_screen_special ();
			wait_render = false;
		}
		flipevent_mode = 0;
		SetEvent (flipevent2);
	}
	flipevent_mode = 0;
	flipthread_mode = -1;
	return 0;
}

static int frame_missed, frame_counted, frame_errors;
static int frame_usage, frame_usage_avg, frame_usage_total;
extern int log_vsync;
static int dooddevenskip;
static volatile int vblank_skipeveryother;
static int vblank_flip_delay;

static int lacemismatch_post_frames = 5;
static int lacemismatch_pre_frames = 5;

static bool vblanklaceskip (void)
{
	if (graphics_mode_changed)
		return false;
	if (vblankbaselace_chipset >= 0 && vblankbaselace) {
		if ((vblankbaselace_chipset && !vblankthread_oddeven) || (!vblankbaselace_chipset && vblankthread_oddeven))
			return true;
	}
	return false;
}

static bool vblanklaceskip_check (void)
{
	int vp = -2;
	if (!vblanklaceskip ()) {
//		if (vblankbaselace_chipset >= 0)
//			write_log (_T("%d == %d\n"), vblankbaselace_chipset, vblankthread_oddeven);
		return false;
	}
	getvblankpos (&vp, false);
	write_log (_T("Interlaced frame type mismatch %d<>%d (%d,%d)\n"), vblankbaselace_chipset, vblankthread_oddeven, vp, prevvblankpos);
	return true;
}

static unsigned int __stdcall vblankthread (void *dummy)
{
	bool firstvblankbasewait2;
	frame_time_t vblank_prev_time2;
	bool doflipped;

	while (vblankthread_mode > VBLANKTH_KILL) {
		struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
		vblankthread_counter++;
		int mode = vblankthread_mode;
		if (mode == VBLANKTH_CALIBRATE) {
			// calibrate mode, try to keep CPU power saving inactive
			SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_LOWEST);
			while (vblankthread_mode == 0)
				vblankthread_counter++;
			SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_HIGHEST);
		} else if (mode == VBLANKTH_IDLE) {
			// idle mode
			Sleep (100);
		} else if (mode == VBLANKTH_ACTIVE_WAIT) {
			sleep_millis (1);
		} else if (mode == VBLANKTH_ACTIVE_START) {
			// do not start until vblank has passed
			int vp;
			if (!getvblankpos (&vp, false)) {
				// bad things happening
				vblankthread_mode = VBLANKTH_ACTIVE;
				continue;
			}
			if (vp <= 0) {
				sleep_millis (1);
				continue;
			}
			ResetEvent (vblankwaitevent);
			if (dooddevenskip == 1) {
				frame_time_t rpt = read_processor_time ();
				for (;;) {
					sleep_millis (1);
					if (!getvblankpos (&vp, false))
						break;
					if (read_processor_time () - rpt > 2 * vblankbasefull)
						break;
					if (vp >= (maxscanline + minscanline) / 2)
						break;
				}
				for (;;) {
					sleep_millis (1);
					if (!getvblankpos (&vp, false))
						break;
					if (read_processor_time () - rpt > 2 * vblankbasefull)
						break;
					if (vp < (maxscanline + minscanline) / 2)
						break;
				}
			}
			if (dooddevenskip > 1) {
				dooddevenskip++;
				if (dooddevenskip > lacemismatch_post_frames)
					dooddevenskip = 0;
			}
			if (vp > maxscanline / 2)
				vp = maxscanline / 2;
			frame_time_t rpt = read_processor_time ();
			vblank_prev_time2 = rpt - (vblankbaseadjust + (vblankbasefull * vp / maxscanline) / (vblank_skipeveryother > 0 ? 2 : 1));
			vblank_prev_time = vblank_prev_time2;
			firstvblankbasewait2 = false;
			prevvblankpos = -1;
			vblank_found_flipdelay = 0;
			doflipped = false;
			if (vblank_skipeveryother > 0) // wait for first vblank in skip frame mode (100Hz+)
				vblankthread_mode = VBLANKTH_ACTIVE_SKIPFRAME;
			else
				vblankthread_mode = VBLANKTH_ACTIVE;
		} else if (mode == VBLANKTH_ACTIVE_SKIPFRAME) {
			int vp;
			sleep_millis (1);
			getvblankpos (&vp, true);
			if (vp >= (maxscanline + minscanline) / 2)
				vblankthread_mode = VBLANKTH_ACTIVE_SKIPFRAME2;
			// something is wrong?
			if (read_processor_time () - vblank_prev_time2 > vblankbasefull * 2)
				vblankthread_mode = VBLANKTH_ACTIVE;
		} else if (mode == VBLANKTH_ACTIVE_SKIPFRAME2) {
			int vp;
			sleep_millis (1);
			getvblankpos (&vp, true);
			if (vp > 0 && vp < (maxscanline + minscanline) / 2) {
				prevvblankpos = 0;
				vblankthread_mode = VBLANKTH_ACTIVE;
			}
			if (read_processor_time () - vblank_prev_time2 > vblankbasefull * 2)
				vblankthread_mode = VBLANKTH_ACTIVE;
		} else if (mode == VBLANKTH_ACTIVE) {
			// busy wait mode
			frame_time_t t = read_processor_time ();
			bool donotwait = false;
			bool end = false;
			bool vblank_found_rtg2 = false;
			bool vblank_found2 = false;
			frame_time_t thread_vblank_time2 = 0;

			if (t - vblank_prev_time2 > vblankbasewait2) {
				int vp = 0;
				bool vb = false;
				bool ok;
				if (firstvblankbasewait2 == false) {
					firstvblankbasewait2 = true;
					vblank_getstate (&vb, &vp);
					vblankthread_oddeven = (vp & 1) != 0;
				}
				if (!doflipped && ap->gfx_vflip > 0) {
					int flag = 1;
					if (ap->gfx_strobo && vblank_skipeveryother > 0)
						flag |= 2;
					doflipevent (flag);
					doflipped = true;
				}
				ok = vblank_getstate (&vb, &vp);
				if (!ok || vb) {
					thread_vblank_time2 = t;
					vblank_found_chipset = true;
					if (!ap->gfx_vflip) {
						if (vblank_skipeveryother >= 0) {
							while (!render_ok) {
								if (read_processor_time () - t > vblankbasefull)
									break;
							}
							show_screen (0);
							render_ok = false;
						} else if (vblank_skipeveryother == -1) {
							while (!render_ok) {
								if (read_processor_time () - t > vblankbasefull)
									break;
							}
							show_screen (0);
							render_ok = false;
							wait_render = true;
							vblank_skipeveryother = -2;
						} else { // == -2
							show_screen (2);
							wait_render = false;
							vblank_skipeveryother = -1;
						}
						int delay = read_processor_time () - t;
						if (delay < 0)
							delay = 0;
						else if (delay >= vblankbasefull)
							delay = 0;
						else if (delay > vblankbasefull * 2 / 3)
							delay = vblankbasefull * 2 / 3;
						vblank_found_flipdelay = delay;
					}
					vblank_found_rtg2 = true;
					vblank_found2 = true;
					if (!dooddevenskip && vblanklaceskip_check ())
						dooddevenskip = 1;
					end = true;
				}
				if (t - vblank_prev_time2 > vblankbasewait3)
					donotwait = true;
			}
			if (!end && t - vblank_prev_time2 > vblankbasefull * 2) {
				thread_vblank_time2 = t;
				vblank_found2 = true;
				vblank_found_rtg2 = true;
				vblank_found_chipset = true;
				end = true;
			}
			if (end) {
				if (ap->gfx_vflip > 0 && !doflipped) {
					doflipevent (1);
					doflipped = true;
				}
				thread_vblank_time = thread_vblank_time2;
				vblank_found_rtg = vblank_found_rtg2;
				vblank_found = vblank_found2;
				if (vblank_skipeveryother == -2)
					vblankthread_mode = VBLANKTH_ACTIVE_START;
				else
					vblankthread_mode = VBLANKTH_ACTIVE_WAIT;
				SetEvent (vblankwaitevent);
			} else if (!donotwait || ap->gfx_vflip || picasso_on) {
				sleep_millis (1);
			}
		} else {
			break;
		}
	}
	vblankthread_mode = -1;
	return 0;
}


static bool isthreadedvsync (void)
{
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
	return isvsync_chipset () <= -2 || isvsync_rtg () < 0 || ap->gfx_strobo;
}

frame_time_t vsync_busywait_end (int *flipdelay)
{
	if (graphics_mode_changed > 0) {
		graphics_mode_changed++;
		if (graphics_mode_changed >= vsync_modechangetimeout)
			graphics_mode_changed = 0;
	}

	if (isthreadedvsync ()) {
		frame_time_t prev;

		if (!currprefs.turbo_emulation) {
			for (;;) {
				int v = vblankthread_mode;
				if (v != VBLANKTH_ACTIVE_START && v != VBLANKTH_ACTIVE_SKIPFRAME && v != VBLANKTH_ACTIVE_SKIPFRAME2)
					break;
				sleep_millis_main (1);
			}
			prev = vblank_prev_time;
			if (!vblanklaceskip ()) {
				int delay = 10;
				frame_time_t t = read_processor_time ();
				while (delay-- > 0) {
					if (WaitForSingleObject (vblankwaitevent, 10) != WAIT_TIMEOUT)
						break;
				}
				idletime += read_processor_time () - t;
			}
			if (flipdelay)
				*flipdelay = vblank_found_flipdelay;
		} else {
			show_screen (0);
			prev = read_processor_time ();
		}
		changevblankthreadmode_fast (VBLANKTH_ACTIVE_WAIT);
		return prev + vblankbasefull;
	} else {
		if (flipdelay)
			*flipdelay = vblank_flip_delay;
		return vblank_prev_time;
	}
}

static bool vblank_sync_started;

bool vsync_isdone (void)
{
	if (isvsync () == 0)
		return false;
	if (!isthreadedvsync ()) {
		int vp = -2;
		getvblankpos (&vp, true);
		if (!vblankthread_oddeven_got) {
			// need to get odd/even state early
			while (vp < 0) {
				if (!getvblankpos (&vp, true))
					break;
			}
			vblankthread_oddeven = (vp & 1) != 0;
			vblankthread_oddeven_got = true;
		}
	}
	if (dooddevenskip)
		return true;
	if (vblank_found_chipset)
		return true;
	return false;
}

void vsync_busywait_start (void)
{
	if (isthreadedvsync ()) {
		if (vblankthread_mode < 0)
			write_log (L"low latency threaded mode but thread is not running!?\n");
		else if (vblankthread_mode != VBLANKTH_ACTIVE_WAIT)
			write_log (L"low latency vsync state mismatch %d\n", vblankthread_mode);
		changevblankthreadmode_fast (VBLANKTH_ACTIVE_START);
	} else {
		vblank_found_chipset = false;
	}
}

int vsync_busywait_do (int *freetime, bool lace, bool oddeven)
{
	int v;
	static bool framelost;
	int ti;
	frame_time_t t;
	frame_time_t prevtime = vblank_prev_time;
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];

	vblank_sync_started = true;
	if (lace)
		vblankbaselace_chipset = oddeven == true ? 1 : 0;
	else
		vblankbaselace_chipset = -1;

	t = read_processor_time ();
	ti = t - prevtime;
	if (ti > 2 * vblankbasefull || ti < -2 * vblankbasefull) {
		changevblankthreadmode_fast (VBLANKTH_ACTIVE_WAIT);
		waitvblankstate (false, NULL, NULL);
		vblank_prev_time = t;
		thread_vblank_time = t;
		frame_missed++;
		return 0;
	}

	if (0 || (log_vsync & 1)) {
		console_out_f(_T("F:%8d M:%8d E:%8d %3d%% (%3d%%) %10d\r"), frame_counted, frame_missed, frame_errors, frame_usage, frame_usage_avg, (t - vblank_prev_time) - vblankbasefull);
		//write_log(_T("F:%8d M:%8d E:%8d %3d%% (%3d%%) %10d\n"), frame_counted, frame_missed, frame_errors, frame_usage, frame_usage_avg, (t - vblank_prev_time) - vblankbasefull);
	}

	if (freetime)
		*freetime = 0;

	frame_usage = (t - prevtime) * 100 / vblankbasefull;
	if (frame_usage > 99)
		frame_usage = 99;
	else if (frame_usage < 0)
		frame_usage = 0;
	frame_usage_total += frame_usage;
	if (freetime)
		*freetime = frame_usage;
	if (frame_counted)
		frame_usage_avg = frame_usage_total / frame_counted;

	v = 0;

	if (isthreadedvsync ()) {

		framelost = false;
		v = 1;

	} else {
		int vp;

		vblank_flip_delay = 0;

		vblankthread_oddeven_got = false;

		if (currprefs.turbo_emulation) {

			show_screen (0);
			dooddevenskip = 0;
			vblank_prev_time = read_processor_time ();
			framelost = true;
			v = -1;
			prevvblankpos = -1;

		} else {

			//write_log (L"%d\n", prevvblankpos);

			if (dooddevenskip > 0 && dooddevenskip != lacemismatch_pre_frames) {
				dooddevenskip++;
				if (dooddevenskip > lacemismatch_pre_frames + lacemismatch_post_frames)
					dooddevenskip = 0;
			}

			if (!dooddevenskip && vblanklaceskip_check ()) {
				dooddevenskip = 1;
			}

			if (ap->gfx_vflip == 0 && vblank_skipeveryother) {
				// make sure that we really did skip one field
				while (!framelost && read_processor_time () - vblank_real_prev_time < vblankbasewait1) {
					vsync_sleep (false);
				}
			}

			if (ap->gfx_vflip != 0) {
				show_screen (0);
				if (ap->gfx_strobo && vblank_skipeveryother) {
					wait_render = true;
					doflipevent (2);
				}
			}
			while (!framelost && read_processor_time () - prevtime < vblankbasewait1) {
				vsync_sleep (false);
			}
			vp = vblank_wait ();

			if (dooddevenskip == lacemismatch_pre_frames) {
				if (vblanklaceskip_check ()) {
					for (;;) {
						if (!getvblankpos (&vp, true))
							break;
						if (vp > maxscanline * 2 / 3)
							break;
					}
					vp = vblank_wait ();
					dooddevenskip++;
				} else {
					dooddevenskip = 0;
				}
			}

			if (vp >= -1) {
				vblank_real_prev_time = vblank_prev_time = read_processor_time ();
				if (ap->gfx_vflip == 0) {
					show_screen (0);
					vblank_flip_delay = (read_processor_time () - vblank_prev_time) / (vblank_skipeveryother ? 2 : 1);
					if (vblank_flip_delay < 0)
						vblank_flip_delay = 0;
					else if (vblank_flip_delay > vblankbasefull * 2 / 3)
						vblank_flip_delay = vblankbasefull * 2 / 3;
				}

				vblank_prev_time -= vblankbaseadjust;
				if (vp > 0) {
					vblank_prev_time -= (vblankbasefull * vp / maxscanline) / (vblank_skipeveryother ? 2 : 1 );
					vblank_sync_started = false;
				}

				v = dooddevenskip || framelost ? -1 : 1;
			}

			prevvblankpos = -1;
			framelost = false;
		}
	}

	if (v) {
		frame_counted++;
		return v;
	}
	frame_errors++;
	return 0;
}

static struct remembered_vsync *vsyncmemory;

struct remembered_vsync
{
	struct remembered_vsync *next;
	int width, height, depth, rate, mode;
	bool rtg, lace;
	double remembered_rate, remembered_rate2;
	int remembered_adjust;
	int maxscanline, minscanline, maxvpos;
};

double vblank_calibrate (double approx_vblank, bool waitonly)
{
	frame_time_t t1, t2;
	double tsum, tsum2, tval, tfirst, div;
	int maxcnt, maxtotal, total, cnt, tcnt2;
	HANDLE th;
	int maxvpos, mult;
	int width, height, depth, rate, mode;
	struct remembered_vsync *rv;
	double rval = -1;
	struct apmode *ap = picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
	struct apmode *apc = picasso_on ? &changed_prefs.gfx_apmode[1] : &changed_prefs.gfx_apmode[0];
	bool remembered = false;
	bool lace = false;

	if (picasso_on) {
		width = picasso96_state.Width;
		height = picasso96_state.Height;
		depth = picasso96_state.BytesPerPixel;
	} else {
		width = currentmode->native_width;
		height = currentmode->native_height;
		depth = (currentmode->native_depth + 7) / 8;
	}

	rate = ap->gfx_refreshrate;
	mode = isfullscreen ();

	// clear remembered modes if restarting and start thread again.
	if (vblankthread_mode <= 0) {
		rv = vsyncmemory;
		while (rv) {
			struct remembered_vsync *rvo = rv->next;
			xfree (rv);
			rv = rvo;
		}
		vsyncmemory = NULL;
	}

	rv = vsyncmemory;
	while (rv) {
		if (rv->width == width && rv->height == height && rv->depth == depth && rv->rate == rate && rv->mode == mode && rv->rtg == picasso_on) {
			approx_vblank = rv->remembered_rate2;
			tsum = rval = rv->remembered_rate;
			maxscanline = rv->maxscanline;
			minscanline = rv->minscanline;
			vblankbaseadjust = rv->remembered_adjust;
			maxvpos = rv->maxvpos;
			lace = rv->lace;
			waitonly = true;
			remembered = true;
			goto skip;
		}
		rv = rv->next;
	}
	
	th = GetCurrentThread ();
	int oldpri = GetThreadPriority (th);
	SetThreadPriority (th, THREAD_PRIORITY_HIGHEST);
	if (vblankthread_mode <= VBLANKTH_KILL) {
		unsigned th;
		vblankthread_mode = VBLANKTH_CALIBRATE;
		_beginthreadex (NULL, 0, vblankthread, 0, 0, &th);
		flipthread_mode = 1;
		flipevent_mode = 0;
		flipevent = CreateEvent (NULL, FALSE, FALSE, NULL);
		flipevent2 = CreateEvent (NULL, FALSE, FALSE, NULL);
		vblankwaitevent = CreateEvent (NULL, FALSE, FALSE, NULL);
		_beginthreadex (NULL, 0, flipthread, 0, 0, &th);
	} else {
		changevblankthreadmode (VBLANKTH_CALIBRATE);
	}
	sleep_millis (100);

	maxtotal = 10;
	maxcnt = maxtotal;
	maxscanline = 0;
	minscanline = -1;
	tsum2 = 0;
	tcnt2 = 0;
	for (maxcnt = 0; maxcnt < maxtotal; maxcnt++) {
		total = 5;
		tsum = 0;
		cnt = total;
		for (cnt = 0; cnt < total; cnt++) {
			int maxvpos1, maxvpos2;
			int flags1, flags2;
			if (!waitvblankstate (true, NULL, NULL))
				goto fail;
			if (!waitvblankstate (false, NULL, NULL))
				goto fail;
			if (!waitvblankstate (true, NULL, NULL))
				goto fail;
			t1 = read_processor_time ();
			if (!waitvblankstate (false, NULL, NULL))
				goto fail;
			maxscanline = 0;
			if (!waitvblankstate (true, &maxvpos1, &flags1))
				goto fail;
			if (!waitvblankstate (false, NULL, NULL))
				goto fail;
			maxscanline = 0;
			if (!waitvblankstate (true, &maxvpos2, &flags2))
				goto fail;
			t2 = read_processor_time ();
			maxvpos = maxvpos1 > maxvpos2 ? maxvpos1 : maxvpos2;
			// count two fields: works with interlaced modes too.
			tval = (double)syncbase * 2.0 / (t2 - t1);
			if (cnt == 0)
				tfirst = tval;
			if (abs (tval - tfirst) > 1) {
				write_log (_T("Very unstable vsync! %.6f vs %.6f, retrying..\n"), tval, tfirst);
				break;
			}
			tsum2 += tval;
			tcnt2++;
			if (abs (tval - tfirst) > 0.1) {
				write_log (_T("Unstable vsync! %.6f vs %.6f\n"), tval, tfirst);
				break;
			}
			tsum += tval;
			if ((flags1 > 0 && flags1 < 3) && (flags2 > 0 && flags2 < 3) && (flags1 != flags2)) {
				lace = true;
			}
		}
		if (cnt >= total)
			break;
	}
	vblankbaseadjust = timezeroonevblank (-1, 1);

	changevblankthreadmode (VBLANKTH_IDLE);

	if (maxcnt >= maxtotal) {
		tsum = tsum2 / tcnt2;
		write_log (_T("Unstable vsync reporting, using average value\n"));
	} else {
		tsum /= total;
	}

	if (ap->gfx_vflip == 0) {
		int vsdetect = 0;
		int detectcnt = 6;
		for (cnt = 0; cnt < detectcnt; cnt++) {
			render_screen (true);
			show_screen (0);
			sleep_millis (1);
			frame_time_t t = read_processor_time () + 1 * (syncbase / tsum);
			for (int cnt2 = 0; cnt2 < 4; cnt2++) {
				render_ok = true;
				show_screen (0);
			}
			int diff = (int)read_processor_time () - (int)t;
			if (diff >= 0)
				vsdetect++;
		}
		if (vsdetect >= detectcnt / 2) {
			write_log (L"Forced vsync detected, switching to double buffered\n");
			changed_prefs.gfx_apmode[0].gfx_backbuffers = 1;
		}
	}

	SetThreadPriority (th, oldpri);

	if (waitonly)
		tsum = approx_vblank;
skip:

	vblank_skipeveryother = 0;
	getvsyncrate (tsum, &mult);
	if (mult < 0) {
		div = 2.0;
		vblank_skipeveryother = 1;
		if (ap->gfx_strobo && ap->gfx_vflip == 0)  {
			vblank_skipeveryother = -1;
			div = 1.0;
		}
	} else if (mult > 0) {
		div = 0.5;
	} else {
		div = 1.0;
	}
	tsum2 = tsum / div;

	vblankbasefull = (syncbase / tsum2);
	vblankbasewait1 = (syncbase / tsum2) * 70 / 100;
	vblankbasewait2 = (syncbase / tsum2) * 55 / 100;
	vblankbasewait3 = (syncbase / tsum2) * 99 / 100 - syncbase / (250 * (vblank_skipeveryother > 0 ? 1 : 2)); // at least 2ms before vblank
	vblankbaselace = lace;

	write_log (_T("VSync %s: %.6fHz/%.1f=%.6fHz. MinV=%d MaxV=%d%s Adj=%d Units=%d %.1f%%\n"),
		waitonly ? _T("remembered") : _T("calibrated"), tsum, div, tsum2,
		minscanline, maxvpos, lace ? _T("i") : _T(""), vblankbaseadjust, vblankbasefull,
		vblankbasewait3 * 100 / (syncbase / tsum2));

	if (minscanline == 1) {
		if (vblankbaseadjust < 0)
			vblankbaseadjust = 0;
		else if (vblankbaseadjust > vblankbasefull / 10)
			vblankbaseadjust = vblankbasefull / 10;
	} else {
		vblankbaseadjust = 0;
	}

	remembered_vblank = tsum;
	vblank_prev_time = read_processor_time ();
	
	if (!remembered) {
		rv = xcalloc (struct remembered_vsync, 1);
		rv->width = width;
		rv->height = height;
		rv->depth = depth;
		rv->rate = rate;
		rv->mode = isfullscreen ();
		rv->rtg = picasso_on;
		rv->remembered_rate = tsum;
		rv->remembered_rate2 = tsum2;
		rv->remembered_adjust = vblankbaseadjust;
		rv->maxscanline = maxscanline;
		rv->minscanline = minscanline;
		rv->maxvpos = maxvpos;
		rv->lace = lace;
		if (vsyncmemory == NULL) {
			vsyncmemory = rv;
		} else {
			rv->next = vsyncmemory;
			vsyncmemory = rv;
		}
	}
	
	vblank_reset (tsum);
	return tsum;
fail:
	write_log (_T("VSync calibration failed\n"));
	ap->gfx_vsync = 0;
	apc->gfx_vsync = 0;
	return -1;
}

static void movecursor (int x, int y)
{
	write_log (_T("SetCursorPos %dx%d\n"), x, y);
	SetCursorPos (x, y);
}

static int create_windows_2 (void)
{
	static bool firstwindow = true;
	static int prevsbheight;
	int dxfs = currentmode->flags & (DM_DX_FULLSCREEN);
	int d3dfs = currentmode->flags & (DM_D3D_FULLSCREEN);
	int fsw = currentmode->flags & (DM_W_FULLSCREEN);
	DWORD exstyle = (currprefs.win32_notaskbarbutton ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW) | 0;
	DWORD flags = 0;
	int borderless = currprefs.win32_borderless;
	DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	int cyborder = GetSystemMetrics (SM_CYFRAME);
	int gap = 0;
	int x, y, w, h;
	struct MultiDisplay *md = getdisplay (&currprefs);
	int sbheight;

	sbheight = currprefs.win32_statusbar ? getstatuswindowheight () : 0;

	if (hAmigaWnd) {
		RECT r;
		int w, h, x, y;
		int nw, nh, nx, ny;

		if (minimized) {
			minimized = -1;
			return 1;
		}
#if 0
		if (minimized && hMainWnd) {
			unsetminimized ();
			ShowWindow (hMainWnd, SW_SHOW);
			ShowWindow (hMainWnd, SW_RESTORE);
		}
#endif
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
			RECT rc = md->rect;
			nx = rc.left;
			ny = rc.top;
			nw = rc.right - rc.left;
			nh = rc.bottom - rc.top;
		} else if (d3dfs) {
			RECT rc = md->rect;
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
		if (w != nw || h != nh || x != nx || y != ny || sbheight != prevsbheight) {
			w = nw;
			h = nh;
			x = nx;
			y = ny;
			in_sizemove++;
			if (hMainWnd && !fsw && !dxfs && !d3dfs && !rp_isactive ()) {
				window_extra_height += (sbheight - prevsbheight);
				GetWindowRect (hMainWnd, &r);
				x = r.left;
				y = r.top;
				SetWindowPos (hMainWnd, HWND_TOP, x, y, w + window_extra_width, h + window_extra_height,
					SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
				x = gap;
				y = gap;
			}
			SetWindowPos (hAmigaWnd, HWND_TOP, x, y, w, h,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
			in_sizemove--;
		} else {
			w = nw;
			h = nh;
			x = nx;
			y = ny;
		}
		createstatuswindow();
		createstatusline();
		updatewinrect (false);
		GetWindowRect (hMainWnd, &mainwin_rect);
		if (d3dfs || dxfs)
			movecursor (x + w / 2, y + h / 2);
		write_log (_T("window already open (%dx%d %dx%d)\n"),
			amigawin_rect.left, amigawin_rect.top, amigawin_rect.right - amigawin_rect.left, amigawin_rect.bottom - amigawin_rect.top);
		updatemouseclip ();
		rp_screenmode_changed ();
		prevsbheight = sbheight;
		return 1;
	}

	if (fsw && !borderless)
		borderless = 1;
	window_led_drives = 0;
	window_led_drives_end = 0;
	hMainWnd = NULL;
	x = 0; y = 0;
	if (borderless)
		sbheight = cyborder = 0;

	if (!dxfs && !d3dfs)  {
		RECT rc;
		int stored_x = 1, stored_y = sbheight + cyborder;
		int oldx, oldy;
		int first = 2;

		regqueryint (NULL, _T("MainPosX"), &stored_x);
		regqueryint (NULL, _T("MainPosY"), &stored_y);

		if (borderless) {
			stored_x = currprefs.gfx_size_win.x;
			stored_y = currprefs.gfx_size_win.y;
		}

		while (first) {
			first--;
			if (stored_x < GetSystemMetrics (SM_XVIRTUALSCREEN))
				stored_x = GetSystemMetrics (SM_XVIRTUALSCREEN);
			if (stored_y < GetSystemMetrics (SM_YVIRTUALSCREEN) + sbheight + cyborder)
				stored_y = GetSystemMetrics (SM_YVIRTUALSCREEN) + sbheight + cyborder;

			if (stored_x > GetSystemMetrics (SM_CXVIRTUALSCREEN))
				rc.left = 1;
			else
				rc.left = stored_x;

			if (stored_y > GetSystemMetrics (SM_CYVIRTUALSCREEN))
				rc.top = 1;
			else
				rc.top = stored_y;

			rc.right = rc.left + gap + currentmode->current_width + gap;
			rc.bottom = rc.top + gap + currentmode->current_height + gap + sbheight;

			oldx = rc.left;
			oldy = rc.top;
			AdjustWindowRect (&rc, borderless ? WS_POPUP : style, FALSE);
			win_x_diff = rc.left - oldx;
			win_y_diff = rc.top - oldy;

			if (MonitorFromRect (&rc, MONITOR_DEFAULTTONULL) == NULL) {
				write_log (_T("window coordinates are not visible on any monitor, reseting..\n"));
				stored_x = stored_y = 0;
				continue;
			}
			break;
		}

		if (fsw) {
			rc = md->rect;
			flags |= WS_EX_TOPMOST;
			style = WS_POPUP;
			currentmode->native_width = rc.right - rc.left;
			currentmode->native_height = rc.bottom - rc.top;
		}
		flags |= (currprefs.win32_alwaysontop ? WS_EX_TOPMOST : 0);

		if (!borderless) {
			RECT rc2;
			hMainWnd = CreateWindowEx (WS_EX_ACCEPTFILES | exstyle | flags,
				_T("PCsuxRox"), _T("WinUAE"),
				style,
				rc.left, rc.top,
				rc.right - rc.left, rc.bottom - rc.top,
				NULL, NULL, hInst, NULL);
			if (!hMainWnd) {
				write_log (_T("main window creation failed\n"));
				return 0;
			}
			GetWindowRect (hMainWnd, &rc2);
			window_extra_width = rc2.right - rc2.left - currentmode->current_width;
			window_extra_height = rc2.bottom - rc2.top - currentmode->current_height;
			createstatuswindow();
			createstatusline();
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
		rc = md->rect;
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
			_T("AmigaPowah"), _T("WinUAE"),
			WS_POPUP,
			0, 0, w, h,
			parent, NULL, hInst, NULL);
	} else {
		hAmigaWnd = CreateWindowEx (
			((dxfs || d3dfs || currprefs.win32_alwaysontop) ? WS_EX_TOPMOST : WS_EX_ACCEPTFILES) | exstyle,
			_T("AmigaPowah"), _T("WinUAE"),
			((dxfs || d3dfs || currprefs.headless) ? WS_POPUP : (WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (hMainWnd ? WS_VISIBLE | WS_CHILD : WS_VISIBLE | WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX))),
			x, y, w, h,
			borderless ? NULL : (hMainWnd ? hMainWnd : NULL),
			NULL, hInst, NULL);
	}
	if (!hAmigaWnd) {
		write_log (_T("creation of amiga window failed\n"));
		close_hwnds ();
		return 0;
	}
	if (hMainWnd == NULL) {
		hMainWnd = hAmigaWnd;
		registertouch(hAmigaWnd);
	} else {
		registertouch(hMainWnd);
		registertouch(hAmigaWnd);
	}

	updatewinrect (true);
	GetWindowRect (hMainWnd, &mainwin_rect);
	if (dxfs || d3dfs)
		movecursor (x + w / 2, y + h / 2);
	addnotifications (hAmigaWnd, FALSE, FALSE);
	createblankwindows ();

	if (hMainWnd != hAmigaWnd) {
		if (!currprefs.headless && !rp_isactive ())
			ShowWindow (hMainWnd, firstwindow ? (currprefs.win32_start_minimized ? SW_SHOWMINIMIZED : SW_SHOWDEFAULT) : SW_SHOWNORMAL);
		UpdateWindow (hMainWnd);
	}
	if (!currprefs.headless && !rp_isactive ())
		ShowWindow (hAmigaWnd, SW_SHOWNORMAL);
	UpdateWindow (hAmigaWnd);
	firstwindow = false;
	setDwmEnableMMCSS (true);
	prevsbheight = sbheight;
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

static void allocsoftbuffer (const TCHAR *name, struct vidbuffer *buf, int flags, int width, int height, int depth)
{
	buf->pixbytes = (depth + 7) / 8;
	buf->width_allocated = (width + 7) & ~7;
	buf->height_allocated = height;

	if (!(flags & DM_SWSCALE)) {

		if (buf != &gfxvidinfo.drawbuffer)
			return;

		buf->bufmem = NULL;
		buf->bufmemend = NULL;
		buf->realbufmem = NULL;
		buf->bufmem_allocated = NULL;
		buf->bufmem_lockable = true;

		write_log (_T("Reserved %s temp buffer (%d*%d*%d)\n"), name, width, height, depth);

	} else if (flags & DM_SWSCALE) {

		int w = buf->width_allocated;
		int h = buf->height_allocated;
		int size = (w * 2) * (h * 2) * buf->pixbytes;
		buf->rowbytes = w * 2 * buf->pixbytes;
		buf->realbufmem = xcalloc (uae_u8, size);
		buf->bufmem_allocated = buf->bufmem = buf->realbufmem + (h / 2) * buf->rowbytes + (w / 2) * buf->pixbytes;
		buf->bufmemend = buf->realbufmem + size - buf->rowbytes;
		buf->bufmem_lockable = true;

		write_log (_T("Allocated %s temp buffer (%d*%d*%d) = %p\n"), name, width, height, depth, buf->realbufmem);
	}
}

static int create_windows (void)
{
	if (!create_windows_2 ())
		return 0;

	return set_ddraw ();
}

static int oldtex_w, oldtex_h, oldtex_rtg;

static BOOL doInit (void)
{
	int fs_warning = -1;
	TCHAR tmpstr[300];
	int tmp_depth;
	int ret = 0;

	remembered_vblank = -1;
	if (wasfullwindow_a == 0)
		wasfullwindow_a = currprefs.gfx_apmode[0].gfx_fullscreen == GFX_FULLWINDOW ? 1 : -1;
	if (wasfullwindow_p == 0)
		wasfullwindow_p = currprefs.gfx_apmode[1].gfx_fullscreen == GFX_FULLWINDOW ? 1 : -1;
	gfxmode_reset ();
	freevidbuffer (&gfxvidinfo.drawbuffer);
	freevidbuffer (&gfxvidinfo.tempbuffer);

	for (;;) {
		updatemodes ();
		currentmode->native_depth = 0;
		tmp_depth = currentmode->current_depth;

		if (currentmode->flags & DM_W_FULLSCREEN) {
			RECT rc = getdisplay (&currprefs)->rect;
			currentmode->native_width = rc.right - rc.left;
			currentmode->native_height = rc.bottom - rc.top;
		}

		if (isfullscreen() <= 0 && !(currentmode->flags & (DM_D3D))) {
			currentmode->current_depth = DirectDraw_GetCurrentDepth ();
			updatemodes ();
		}
		if (!(currentmode->flags & (DM_D3D)) && DirectDraw_GetCurrentDepth () == currentmode->current_depth) {
			updatemodes ();
		}

		if (!rp_isactive () && (currentmode->current_width > GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			currentmode->current_height > GetSystemMetrics(SM_CYVIRTUALSCREEN))) {
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
			DirectDraw_Start ();
			if (screen_is_picasso)
				changed_prefs.gfx_apmode[1].gfx_fullscreen = currprefs.gfx_apmode[1].gfx_fullscreen = GFX_FULLSCREEN;
			else
				changed_prefs.gfx_apmode[0].gfx_fullscreen = currprefs.gfx_apmode[0].gfx_fullscreen = GFX_FULLSCREEN;
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

			if (currprefs.gfx_resolution > gfxvidinfo.gfx_resolution_reserved)
				gfxvidinfo.gfx_resolution_reserved = currprefs.gfx_resolution;
			if (currprefs.gfx_vresolution > gfxvidinfo.gfx_vresolution_reserved)
				gfxvidinfo.gfx_vresolution_reserved = currprefs.gfx_vresolution;

			//gfxvidinfo.drawbuffer.gfx_resolution_reserved = RES_SUPERHIRES;

#if defined (GFXFILTER)
			if (currentmode->flags & (DM_D3D | DM_SWSCALE)) {
				if (!currprefs.gfx_autoresolution) {
					currentmode->amiga_width = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
					currentmode->amiga_height = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;
				} else {
					currentmode->amiga_width = AMIGA_WIDTH_MAX << gfxvidinfo.gfx_resolution_reserved;
					currentmode->amiga_height = AMIGA_HEIGHT_MAX << gfxvidinfo.gfx_vresolution_reserved;
				}
				if (gfxvidinfo.gfx_resolution_reserved == RES_SUPERHIRES)
					currentmode->amiga_height *= 2;
				if (currentmode->amiga_height > 1280)
					currentmode->amiga_height = 1280;

				gfxvidinfo.drawbuffer.inwidth = gfxvidinfo.drawbuffer.outwidth = currentmode->amiga_width;
				gfxvidinfo.drawbuffer.inheight = gfxvidinfo.drawbuffer.outheight = currentmode->amiga_height;

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
			gfxvidinfo.drawbuffer.pixbytes = currentmode->current_depth >> 3;
			gfxvidinfo.drawbuffer.bufmem = NULL;
			gfxvidinfo.drawbuffer.linemem = NULL;
			gfxvidinfo.maxblocklines = 0; // flush_screen actually does everything
			gfxvidinfo.drawbuffer.rowbytes = currentmode->pitch;
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
	if (!scrlinebuf)
		scrlinebuf = xmalloc (uae_u8, max_uae_width * 4);

	gfxvidinfo.drawbuffer.emergmem = scrlinebuf; // memcpy from system-memory to video-memory

	gfxvidinfo.drawbuffer.realbufmem = NULL;
	gfxvidinfo.drawbuffer.bufmem = NULL;
	gfxvidinfo.drawbuffer.bufmem_allocated = NULL;
	gfxvidinfo.drawbuffer.bufmem_lockable = false;

	gfxvidinfo.outbuffer = &gfxvidinfo.drawbuffer;
	gfxvidinfo.inbuffer = &gfxvidinfo.drawbuffer;

	if (!screen_is_picasso) {

		if (currprefs.gfx_api == 0 && currprefs.gf[0].gfx_filter == 0) {
			allocsoftbuffer (_T("draw"), &gfxvidinfo.drawbuffer, currentmode->flags,
				currentmode->native_width, currentmode->native_height, currentmode->current_depth);
		} else {
			allocsoftbuffer (_T("draw"), &gfxvidinfo.drawbuffer, currentmode->flags,
				1600, 1280, currentmode->current_depth);
		}
		if (currprefs.monitoremu || currprefs.cs_cd32fmv || (currprefs.genlock && currprefs.genlock_image)) {
			allocsoftbuffer (_T("monemu"), &gfxvidinfo.tempbuffer, currentmode->flags,
				currentmode->amiga_width > 1024 ? currentmode->amiga_width : 1024,
				currentmode->amiga_height > 1024 ? currentmode->amiga_height : 1024,
				currentmode->current_depth);
		}

		init_row_map ();
	}
	init_colors ();

	S2X_free ();
	oldtex_w = oldtex_h = -1;
	if (currentmode->flags & DM_D3D) {
		const TCHAR *err = D3D_init (hAmigaWnd, currentmode->native_width, currentmode->native_height, currentmode->current_depth, &currentmode->freq, screen_is_picasso ? 1 : currprefs.gf[picasso_on].gfx_filter_filtermode + 1);
		if (err) {
			D3D_free (true);
			gui_message (err);
			changed_prefs.gfx_api = currprefs.gfx_api = 0;
			changed_prefs.gf[picasso_on].gfx_filter = currprefs.gf[picasso_on].gfx_filter = 0;
			currentmode->current_depth = currentmode->native_depth;
			gfxmode_reset ();
			DirectDraw_Start ();
			ret = -1;
			goto oops;
		}
		target_graphics_buffer_update ();
		updatewinrect (true);
	}

	screen_is_initialized = 1;
	createstatusline();
	picasso_refresh ();
#ifdef RETROPLATFORM
	rp_set_hwnd_delayed ();
#endif

	if (isfullscreen () != 0)
		setmouseactive (-1);

	return 1;

oops:
	close_hwnds ();
	return ret;
}

bool target_graphics_buffer_update (void)
{
	static bool	graphicsbuffer_retry;
	int w, h;
	
	graphicsbuffer_retry = false;
	if (screen_is_picasso) {
		w = picasso96_state.Width > picasso_vidinfo.width ? picasso96_state.Width : picasso_vidinfo.width;
		h = picasso96_state.Height > picasso_vidinfo.height ? picasso96_state.Height : picasso_vidinfo.height;
	} else {
		struct vidbuffer *vb = gfxvidinfo.drawbuffer.tempbufferinuse ? &gfxvidinfo.tempbuffer : &gfxvidinfo.drawbuffer;
		gfxvidinfo.outbuffer = vb;
		w = vb->outwidth;
		h = vb->outheight;
	}
	
	if (oldtex_w == w && oldtex_h == h && oldtex_rtg == screen_is_picasso)
		return false;

	if (!w || !h) {
		oldtex_w = w;
		oldtex_h = h;
		oldtex_rtg = screen_is_picasso;
		return false;
	}

	S2X_free ();
	if (currentmode->flags & DM_D3D) {
		if (!D3D_alloctexture (w, h)) {
			graphicsbuffer_retry = true;
			return false;
		}
	} else {
		DirectDraw_ClearSurface (NULL);
	}

	oldtex_w = w;
	oldtex_h = h;
	oldtex_rtg = screen_is_picasso;

	write_log (_T("Buffer size (%d*%d) %s\n"), w, h, screen_is_picasso ? _T("RTG") : _T("Native"));

	if ((currentmode->flags & DM_SWSCALE) && !screen_is_picasso) {
		if (!S2X_init (currentmode->native_width, currentmode->native_height, currentmode->native_depth))
			return false;
	}
	return true;
}

void updatedisplayarea (void)
{
	if (!screen_is_initialized)
		return;
	if (dx_islost ())
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
			if (!picasso_on) {
				if (currentmode->flags & DM_SWSCALE)
					S2X_refresh ();
			}
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
	md = getdisplay (p);
	set_config_changed ();
}

bool toggle_rtg (int mode)
{
	if (mode == 0) {
		if (!picasso_on)
			return false;
	} else if (mode > 0) {
		if (picasso_on)
			return false;
	}
	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE) {
		return gfxboard_toggle (mode);
	} else {
		// can always switch from RTG to custom
		if (picasso_requested_on && picasso_on) {
			picasso_requested_on = false;
			return true;
		}
		if (picasso_on)
			return false;
		// can only switch from custom to RTG if there is some mode active
		if (picasso_is_active ()) {
			picasso_requested_on = true;
			return true;
		}
	}
	return false;
}

void toggle_fullscreen (int mode)
{
	int *p = picasso_on ? &changed_prefs.gfx_apmode[1].gfx_fullscreen : &changed_prefs.gfx_apmode[0].gfx_fullscreen;
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

	frame_missed = frame_counted = frame_errors = 0;
	frame_usage = frame_usage_avg = frame_usage_total = 0;

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



