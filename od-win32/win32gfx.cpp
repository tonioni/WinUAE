/*
* UAE - The Un*x Amiga Emulator
*
* Win32 Drawing and DirectX interface
*
* Copyright 1997-1998 Mathias Ortmann
* Copyright 1997-2000 Brian King
*/

#define FORCE16BIT 0

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>

#include <windows.h>
#include <commctrl.h>
#include <ddraw.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <D3dkmthk.h>
#include <process.h>

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
#include "devices.h"

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

static int deskhz;

struct MultiDisplay Displays[MAX_DISPLAYS + 1];

struct AmigaMonitor AMonitors[MAX_AMIGAMONITORS];
struct AmigaMonitor *amon = NULL;

static int display_change_requested;
int window_led_drives, window_led_drives_end;
int window_led_hd, window_led_hd_end;
int window_led_joys, window_led_joys_end, window_led_joy_start;
int window_led_msg, window_led_msg_end, window_led_msg_start;
extern int console_logging;

static int wasfullwindow_a, wasfullwindow_p;

int vsync_modechangetimeout = 10;

int vsync_activeheight, vsync_totalheight;
float vsync_vblank, vsync_hblank;
bool beamracer_debug;

int reopen(struct AmigaMonitor *, int, bool);

static CRITICAL_SECTION screen_cs;
static bool screen_cs_allocated;

void gfx_lock (void)
{
	EnterCriticalSection (&screen_cs);
}
void gfx_unlock (void)
{
	LeaveCriticalSection (&screen_cs);
}

int WIN32GFX_IsPicassoScreen(struct AmigaMonitor *mon)
{
	return mon->screen_is_picasso ? 1 : 0;
}

static int isscreen(struct AmigaMonitor *mon)
{
	return mon->hMainWnd ? 1 : 0;
}

static void clearscreen (void)
{
	DirectDraw_FillPrimary ();
}    

static int isfullscreen_2(struct uae_prefs *p)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	int idx = mon->screen_is_picasso ? 1 : 0;
	return p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLWINDOW ? -1 : 0);
}
int isfullscreen(void)
{
	return isfullscreen_2(&currprefs);
}

int WIN32GFX_GetDepth(struct AmigaMonitor *mon, int real)
{
	if (!mon->currentmode.native_depth)
		return mon->currentmode.current_depth;
	return real ? mon->currentmode.native_depth : mon->currentmode.current_depth;
}

int WIN32GFX_GetWidth(struct AmigaMonitor *mon)
{
	return mon->currentmode.current_width;
}

int WIN32GFX_GetHeight(struct AmigaMonitor *mon)
{
	return mon->currentmode.current_height;
}

static BOOL doInit (struct AmigaMonitor*);

int default_freq = 60;

static uae_u8 *scrlinebuf;


static struct MultiDisplay *getdisplay2(struct uae_prefs *p, int index)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	static int max;
	int display = index < 0 ? p->gfx_apmode[mon->screen_is_picasso ? APMODE_RTG : APMODE_NATIVE].gfx_display - 1 : index;

	if (!max || (max > 0 && Displays[max].monitorname != NULL)) {
		max = 0;
		while (Displays[max].monitorname)
			max++;
		if (max == 0) {
			gui_message(_T("no display adapters! Exiting"));
			exit(0);
		}
	}
	if (index >= 0 && display >= max)
		return NULL;
	if (display >= max)
		display = 0;
	if (display < 0)
		display = 0;
	return &Displays[display];
}
struct MultiDisplay *getdisplay(struct uae_prefs *p, int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	if (monid > 0 && mon->md)
		return mon->md;
	return getdisplay2(p, -1);
}

void desktop_coords(int monid, int *dw, int *dh, int *ax, int *ay, int *aw, int *ah)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct MultiDisplay *md = getdisplay(&currprefs, monid);

	*dw = md->rect.right - md->rect.left;
	*dh = md->rect.bottom - md->rect.top;
	*ax = mon->amigawin_rect.left;
	*ay = mon->amigawin_rect.top;
	*aw = mon->amigawin_rect.right - *ax;
	*ah = mon->amigawin_rect.bottom - *ay;
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

typedef NTSTATUS(CALLBACK* D3DKMTOPENADAPTERFROMHDC)(D3DKMT_OPENADAPTERFROMHDC*);
static D3DKMTOPENADAPTERFROMHDC pD3DKMTOpenAdapterFromHdc;
typedef NTSTATUS(CALLBACK* D3DKMTGETSCANLINE)(D3DKMT_GETSCANLINE*);
static D3DKMTGETSCANLINE pD3DKMTGetScanLine;
typedef NTSTATUS(CALLBACK* D3DKMTWAITFORVERTICALBLANKEVENT)(const D3DKMT_WAITFORVERTICALBLANKEVENT*);
static D3DKMTWAITFORVERTICALBLANKEVENT pD3DKMTWaitForVerticalBlankEvent;
#define STATUS_SUCCESS ((NTSTATUS)0)

static int target_get_display_scanline2(int displayindex)
{
	if (pD3DKMTGetScanLine) {
		D3DKMT_GETSCANLINE sl = { 0 };
		struct MultiDisplay *md = displayindex < 0 ? getdisplay(&currprefs, 0) : &Displays[displayindex];
		if (!md->HasAdapterData)
			return -11;
		sl.VidPnSourceId = md->VidPnSourceId;
		sl.hAdapter = md->AdapterHandle;
		NTSTATUS status = pD3DKMTGetScanLine(&sl);
		if (status == STATUS_SUCCESS) {
			if (sl.InVerticalBlank)
				return -1;
			return sl.ScanLine;
		} else {
			if ((int)status > 0)
				return -(int)status;
			return status;
		}
		return -12;
	} else if (D3D_getscanline) {
		int scanline;
		bool invblank;
		if (D3D_getscanline(&scanline, &invblank)) {
			if (invblank)
				return -1;
			return scanline;
		}
		return -14;
	}
	return -13;
}

extern uae_u64 spincount;;
int target_get_display_scanline(int displayindex)
{
	static uae_u64 lastrdtsc;
	static int lastvpos;
	if (spincount == 0 || currprefs.m68k_speed >= 0) {
		lastrdtsc = 0;
		lastvpos = target_get_display_scanline2(displayindex);
		return lastvpos;
	}
	uae_u64 v = __rdtsc();
	if (lastrdtsc > v)
		return lastvpos;
	lastvpos = target_get_display_scanline2(displayindex);
	lastrdtsc = __rdtsc() + spincount * 4;
	return lastvpos;
}

typedef LONG(CALLBACK* QUERYDISPLAYCONFIG)(UINT32, UINT32*, DISPLAYCONFIG_PATH_INFO*, UINT32*, DISPLAYCONFIG_MODE_INFO*, DISPLAYCONFIG_TOPOLOGY_ID*);
typedef LONG(CALLBACK* GETDISPLAYCONFIGBUFFERSIZES)(UINT32, UINT32*, UINT32*);
typedef LONG(CALLBACK* DISPLAYCONFIGGETDEVICEINFO)(DISPLAYCONFIG_DEVICE_INFO_HEADER*);

static bool get_display_vblank_params(int displayindex, int *activeheightp, int *totalheightp, float *vblankp, float *hblankp)
{
	static QUERYDISPLAYCONFIG pQueryDisplayConfig;
	static GETDISPLAYCONFIGBUFFERSIZES pGetDisplayConfigBufferSizes;
	static DISPLAYCONFIGGETDEVICEINFO pDisplayConfigGetDeviceInfo;
	if (!pQueryDisplayConfig)
		pQueryDisplayConfig = (QUERYDISPLAYCONFIG)GetProcAddress(GetModuleHandle(_T("user32.dll")), "QueryDisplayConfig");
	if (!pGetDisplayConfigBufferSizes)
		pGetDisplayConfigBufferSizes = (GETDISPLAYCONFIGBUFFERSIZES)GetProcAddress(GetModuleHandle(_T("user32.dll")), "GetDisplayConfigBufferSizes");
	if (!pDisplayConfigGetDeviceInfo)
		pDisplayConfigGetDeviceInfo = (DISPLAYCONFIGGETDEVICEINFO)GetProcAddress(GetModuleHandle(_T("user32.dll")), "DisplayConfigGetDeviceInfo");
	if (!pQueryDisplayConfig || !pGetDisplayConfigBufferSizes || !pDisplayConfigGetDeviceInfo)
		return false;
	struct MultiDisplay *md = displayindex < 0 ? getdisplay(&currprefs, 0) : &Displays[displayindex];
	UINT32 pathCount, modeCount;
	bool ret = false;
	if (pGetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS) {
		DISPLAYCONFIG_PATH_INFO *displayPaths;
		DISPLAYCONFIG_MODE_INFO *displayModes;
		displayPaths = xmalloc(DISPLAYCONFIG_PATH_INFO, pathCount);
		displayModes = xmalloc(DISPLAYCONFIG_MODE_INFO, modeCount);
		if (pQueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, displayPaths, &modeCount, displayModes, NULL) == ERROR_SUCCESS) {
			for (int i = 0; i < pathCount; i++) {
				DISPLAYCONFIG_PATH_INFO *path = &displayPaths[i];
				DISPLAYCONFIG_MODE_INFO *target = &displayModes[path->targetInfo.modeInfoIdx];
				DISPLAYCONFIG_MODE_INFO *source = &displayModes[path->sourceInfo.modeInfoIdx];
				DISPLAYCONFIG_SOURCE_DEVICE_NAME dcsdn;
				DISPLAYCONFIG_DEVICE_INFO_HEADER *dcdih = &dcsdn.header;
				dcdih->size = sizeof dcsdn;
				dcdih->adapterId = source->adapterId;
				dcdih->id = source->id;
				dcdih->type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
				if (pDisplayConfigGetDeviceInfo(dcdih) == ERROR_SUCCESS) {
					if (!_tcscmp(md->adapterid, dcsdn.viewGdiDeviceName)) {
						DISPLAYCONFIG_VIDEO_SIGNAL_INFO *si = &target->targetMode.targetVideoSignalInfo;
						if (activeheightp)
							*activeheightp = si->activeSize.cy;
						if (totalheightp)
							*totalheightp = si->totalSize.cy;
						float vblank = (float)si->vSyncFreq.Numerator / si->vSyncFreq.Denominator;
						float hblank = (float)si->hSyncFreq.Numerator / si->hSyncFreq.Denominator;
						if (vblankp)
							*vblankp = vblank;
						if (hblankp)
							*hblankp = hblank;
						write_log(_T("ActiveHeight: %d TotalHeight: %d VFreq=%d/%d=%.2fHz HFreq=%d/%d=%.3fKHz\n"),
							target->targetMode.targetVideoSignalInfo.activeSize.cy,
							target->targetMode.targetVideoSignalInfo.totalSize.cy,
							target->targetMode.targetVideoSignalInfo.vSyncFreq.Numerator,
							target->targetMode.targetVideoSignalInfo.vSyncFreq.Denominator,
							vblank,
							target->targetMode.targetVideoSignalInfo.hSyncFreq.Numerator,
							target->targetMode.targetVideoSignalInfo.hSyncFreq.Denominator,
							hblank / 1000.0);
						ret = true;
						break;
					}
				}
			}
		}
		xfree(displayModes);
		xfree(displayPaths);
	}
	return ret;
}

static volatile int waitvblankthread_mode;
HANDLE waitvblankevent;
static frame_time_t wait_vblank_timestamp;
static MultiDisplay *wait_vblank_display;
static volatile bool vsync_active;

static unsigned int __stdcall waitvblankthread(void *dummy)
{
	waitvblankthread_mode = 2;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	while (waitvblankthread_mode) {
		D3DKMT_WAITFORVERTICALBLANKEVENT e = { 0 };
		e.hAdapter = wait_vblank_display->AdapterHandle;
		e.VidPnSourceId = wait_vblank_display->VidPnSourceId;
		pD3DKMTWaitForVerticalBlankEvent(&e);
		wait_vblank_timestamp = read_processor_time();
		vsync_active = true;
		SetEvent(waitvblankevent);
	}
	waitvblankthread_mode = -1;
	return 0;
}

static void display_vblank_thread_kill(void)
{
	if (waitvblankthread_mode == 2) {
		waitvblankthread_mode = 0;
		while (waitvblankthread_mode != -1) {
			Sleep(10);
		}
		waitvblankthread_mode = 0;
		CloseHandle(waitvblankevent);
		waitvblankevent = NULL;
	}
}

static void display_vblank_thread(struct AmigaMonitor *mon)
{
	struct amigadisplay *ad = &adisplays[mon->monitor_id];
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];

	if (currprefs.m68k_speed >= 0) {
		display_vblank_thread_kill();
		return;
	}
	if (waitvblankthread_mode > 0)
		return;
	// It seems some Windows 7 drivers stall if D3DKMTWaitForVerticalBlankEvent()
	// and D3DKMTGetScanLine() is used simultaneously.
	if (os_win8 && ap->gfx_vsyncmode && pD3DKMTWaitForVerticalBlankEvent && wait_vblank_display && wait_vblank_display->HasAdapterData) {
		waitvblankevent = CreateEvent(NULL, FALSE, FALSE, NULL);
		waitvblankthread_mode = 1;
		unsigned int th;
		_beginthreadex(NULL, 0, waitvblankthread, 0, 0, &th);
	}
}

void target_cpu_speed(void)
{
	display_vblank_thread(&AMonitors[0]);
}

extern void target_calibrate_spin(void);
static void display_param_init(struct AmigaMonitor *mon)
{
	struct amigadisplay *ad = &adisplays[mon->monitor_id];
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];

	vsync_activeheight = mon->currentmode.current_height;
	vsync_totalheight = vsync_activeheight * 1125 / 1080;
	vsync_vblank = 0;
	vsync_hblank = 0;
	get_display_vblank_params(-1, &vsync_activeheight, &vsync_totalheight, &vsync_vblank, &vsync_hblank);
	if (vsync_vblank <= 0)
		vsync_vblank = mon->currentmode.freq;
	// GPU scaled mode?
	if (vsync_activeheight > mon->currentmode.current_height) {
		float m = (float)vsync_activeheight / mon->currentmode.current_height;
		vsync_hblank = (int)(vsync_hblank / m + 0.5);
		vsync_activeheight = mon->currentmode.current_height;
	}

	wait_vblank_display = getdisplay(&currprefs, mon->monitor_id);
	if (!wait_vblank_display || !wait_vblank_display->HasAdapterData) {
		write_log(_T("Selected display mode does not have adapter data!\n"));
	}
	Sleep(10);
	target_calibrate_spin();
	display_vblank_thread(mon);
}

const TCHAR *target_get_display_name (int num, bool friendlyname)
{
	if (num <= 0)
		return NULL;
	struct MultiDisplay *md = getdisplay2(NULL, num - 1);
	if (!md)
		return NULL;
	if (friendlyname)
		return md->monitorname;
	return md->monitorid;
}

void centerdstrect(struct AmigaMonitor *mon, RECT *dr)
{
	struct uae_filter *usedfilter = mon->usedfilter;
	if(!(mon->currentmode.flags & (DM_DX_FULLSCREEN | DM_D3D_FULLSCREEN | DM_W_FULLSCREEN)))
		OffsetRect (dr, mon->amigawin_rect.left, mon->amigawin_rect.top);
	if (mon->currentmode.flags & DM_W_FULLSCREEN) {
		if (mon->scalepicasso && mon->screen_is_picasso)
			return;
		if (usedfilter && !mon->screen_is_picasso)
			return;
		if (mon->currentmode.fullfill && (mon->currentmode.current_width > mon->currentmode.native_width || mon->currentmode.current_height > mon->currentmode.native_height))
			return;
		OffsetRect (dr, (mon->currentmode.native_width - mon->currentmode.current_width) / 2,
			(mon->currentmode.native_height - mon->currentmode.current_height) / 2);
	}
}

static int picasso_offset_x, picasso_offset_y;
static float picasso_offset_mx, picasso_offset_my;

void getgfxoffset(int monid, float *dxp, float *dyp, float *mxp, float *myp)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct uae_filter *usedfilter = mon->usedfilter;
	float dx, dy, mx, my;

	getfilteroffset(monid, &dx, &dy, &mx, &my);
	if (ad->picasso_on) {
		dx = picasso_offset_x * picasso_offset_mx;
		dy = picasso_offset_y * picasso_offset_my;
		mx = picasso_offset_mx;
		my = picasso_offset_my;
	}

	//write_log(_T("%.2fx%.2f %.2fx%.2f\n"), dx, dy, mx, my);

	if (mon->currentmode.flags & DM_W_FULLSCREEN) {
		for (;;) {
			if (mon->scalepicasso && mon->screen_is_picasso)
				break;
			if (usedfilter && !mon->screen_is_picasso)
				break;
			if (mon->currentmode.fullfill && (mon->currentmode.current_width > mon->currentmode.native_width || mon->currentmode.current_height > mon->currentmode.native_height))
				break;
			dx += (mon->currentmode.native_width - mon->currentmode.current_width) / 2;
			dy += (mon->currentmode.native_height - mon->currentmode.current_height) / 2;
			break;
		}
	}

	*dxp = dx;
	*dyp = dy;
	*mxp = 1.0 / mx;
	*myp = 1.0 / my;
}

void DX_Fill(struct AmigaMonitor *mon, int dstx, int dsty, int width, int height, uae_u32 color)
{
	RECT dstrect;
	if (width < 0)
		width = mon->currentmode.current_width;
	if (height < 0)
		height = mon->currentmode.current_height;
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

int getrefreshrate(int monid, int width, int height)
{
	struct amigadisplay *ad = &adisplays[monid];
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	int freq = 0;
	
	if (ap->gfx_refreshrate <= 0)
		return 0;
	
	struct MultiDisplay *md = getdisplay(&currprefs, monid);
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

static int set_ddraw_2(struct AmigaMonitor *mon)
{
	struct amigadisplay *ad = &adisplays[mon->monitor_id];
	struct picasso96_state_struct *state = &picasso96_state[mon->monitor_id];

	HRESULT ddrval;
	int bits = (mon->currentmode.current_depth + 7) & ~7;
	int width = mon->currentmode.native_width;
	int height = mon->currentmode.native_height;
	int dxfullscreen, wfullscreen, dd;
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	int freq = ap->gfx_refreshrate;

	dxfullscreen = (mon->currentmode.flags & DM_DX_FULLSCREEN) ? TRUE : FALSE;
	wfullscreen = (mon->currentmode.flags & DM_W_FULLSCREEN) ? TRUE : FALSE;
	dd = (mon->currentmode.flags & DM_DDRAW) ? TRUE : FALSE;

	if (WIN32GFX_IsPicassoScreen(mon) && (state->Width > width || state->Height > height)) {
		width = state->Width;
		height = state->Height;
	}

	DirectDraw_FreeMainSurface ();

	if (!dd && !dxfullscreen)
		return 1;

	ddrval = DirectDraw_SetCooperativeLevel (mon->hAmigaWnd, dxfullscreen, TRUE);
	if (FAILED (ddrval))
		goto oops;

	if (dxfullscreen)  {
		for (;;) {
			HRESULT olderr;
			freq = getrefreshrate(mon->monitor_id, width, height);
			write_log (_T("set_ddraw: trying %dx%d, bits=%d, refreshrate=%d\n"), width, height, bits, freq);
			ddrval = DirectDraw_SetDisplayMode (width, height, bits, freq);
			if (SUCCEEDED (ddrval))
				break;
			olderr = ddrval;
			if (freq) {
				write_log (_T("set_ddraw: failed, trying without forced refresh rate\n"));
				freq = 0;
				DirectDraw_SetCooperativeLevel (mon->hAmigaWnd, dxfullscreen, TRUE);
				ddrval = DirectDraw_SetDisplayMode (width, height, bits, freq);
				if (SUCCEEDED (ddrval))
					break;
			}
			if (olderr != DDERR_INVALIDMODE  && olderr != 0x80004001 && olderr != DDERR_UNSUPPORTEDMODE)
				goto oops;
			return -1;
		}
		mon->currentmode.freq = freq;
		updatewinrect(mon, true);
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
		ddrval = DirectDraw_SetClipper(mon->hAmigaWnd);
		if (FAILED (ddrval))
			goto oops;
		if (DirectDraw_SurfaceLock ()) {
			mon->currentmode.pitch = DirectDraw_GetSurfacePitch ();
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

#ifndef ABM_GETAUTOHIDEBAREX
#define ABM_GETAUTOHIDEBAREX 0x0000000b
#endif

static void adjustappbar(RECT *monitor, RECT *workrect)
{
	if (!os_vista)
		return;
	APPBARDATA abd;
	// Isn't this ugly API?
	for (int i = 0; i < 4; i++) {
		abd.cbSize = sizeof abd;
		abd.rc = *monitor;
		abd.uEdge = i; // ABE_LEFT, TOP, RIGHT, BOTTOM
		HWND hwndAutoHide = (HWND) SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &abd);
		if (hwndAutoHide == NULL)
			continue;
		WINDOWINFO wi;
		wi.cbSize = sizeof wi;
		if (!GetWindowInfo(hwndAutoHide, &wi))
			continue;
		int edge;
		switch (i)
		{
			case ABE_LEFT:
			edge = monitor->left + (wi.rcWindow.right - wi.rcWindow.left);
			if (edge > workrect->left && edge < workrect->right)
				workrect->left = edge;
			break;
			case ABE_RIGHT:
			edge = monitor->right - (wi.rcWindow.right - wi.rcWindow.left);
			if (edge < workrect->right && edge > workrect->left)
				workrect->right = edge;
			break;
			case ABE_TOP:
			edge = monitor->top + (wi.rcWindow.bottom - wi.rcWindow.top);
			if (edge > workrect->top && edge < workrect->bottom)
				workrect->top = edge;
			break;
			case ABE_BOTTOM:
			edge = monitor->bottom - (wi.rcWindow.bottom - wi.rcWindow.top);
			if (edge < workrect->bottom && edge > workrect->top)
				workrect->bottom = edge;
			break;
		}
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
			md->workrect = lpmi.rcWork;
			adjustappbar(&md->rect, &md->workrect);
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

static BOOL CALLBACK monitorEnumProc2(HMONITOR h, HDC hdc, LPRECT rect, LPARAM data)
{
	MONITORINFOEX lpmi;
	lpmi.cbSize = sizeof lpmi;
	GetMonitorInfo(h, (LPMONITORINFO)&lpmi);
	for (int i = 0; i < MAX_DISPLAYS && Displays[i].monitorid; i++) {
		struct MultiDisplay *md = &Displays[i];
		if (!_tcscmp (md->adapterid, lpmi.szDevice) && !memcmp(&md->rect, &lpmi.rcMonitor, sizeof RECT)) {
			md->workrect = lpmi.rcWork;
			adjustappbar(&md->rect, &md->workrect);
			return TRUE;
		}
	}
	return TRUE;
}

void reenumeratemonitors(void)
{
	for (int i = 0; i < MAX_DISPLAYS; i++) {
		struct MultiDisplay *md = &Displays[i];
		memcpy(&md->workrect, &md->rect, sizeof RECT);
	}
	EnumDisplayMonitors (NULL, NULL, monitorEnumProc2, NULL);
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
			if (pD3DKMTOpenAdapterFromHdc) {
				HDC hdc = CreateDC(NULL, add.DeviceName, NULL, NULL);
				if (hdc != NULL) {
					D3DKMT_OPENADAPTERFROMHDC OpenAdapterData = { 0 };
					OpenAdapterData.hDc = hdc;
					NTSTATUS status = pD3DKMTOpenAdapterFromHdc(&OpenAdapterData);
					if (status == STATUS_SUCCESS) {
						md->AdapterLuid = OpenAdapterData.AdapterLuid;
						md->VidPnSourceId = OpenAdapterData.VidPnSourceId;
						md->AdapterHandle = OpenAdapterData.hAdapter;
						md->HasAdapterData = true;
					}
					DeleteDC(hdc);
				}
			}

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
	if (!pD3DKMTWaitForVerticalBlankEvent) {
		pD3DKMTOpenAdapterFromHdc = (D3DKMTOPENADAPTERFROMHDC)GetProcAddress(GetModuleHandle(_T("Gdi32.dll")), "D3DKMTOpenAdapterFromHdc");
		pD3DKMTGetScanLine = (D3DKMTGETSCANLINE)GetProcAddress(GetModuleHandle(_T("Gdi32.dll")), "D3DKMTGetScanLine");
		pD3DKMTWaitForVerticalBlankEvent = (D3DKMTWAITFORVERTICALBLANKEVENT)GetProcAddress(GetModuleHandle(_T("Gdi32.dll")), "D3DKMTWaitForVerticalBlankEvent");
	}
	if (!enumeratedisplays2 (false))
		enumeratedisplays2(true);
}

void sortdisplays (void)
{
	struct MultiDisplay *md;
	int i, idx;

	int w = GetSystemMetrics (SM_CXSCREEN);
	int h = GetSystemMetrics (SM_CYSCREEN);
	int wv = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int hv = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	int b = 0;
	
	deskhz = 0;

	HDC hdc = GetDC (NULL);
	if (hdc) {
		b = GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
		ReleaseDC (NULL, hdc);
	}
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
					if (dm.dmPelsWidth == w && dm.dmPelsHeight == h && dm.dmBitsPerPel == b) {
						if (dm.dmDisplayFrequency > deskhz)
							deskhz = dm.dmDisplayFrequency;
					}
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
	write_log(_T("Desktop: W=%d H=%d B=%d HZ=%d. CXVS=%d CYVS=%d\n"), w, h, b, deskhz, wv, hv);

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

#if 0
static int flushymin, flushymax;
#define FLUSH_DIFF 50

static void flushit (struct vidbuffer *vb, int lineno)
{
	if (!currprefs.gfx_api)
		return;
	if (mon->currentmode.flags & DM_SWSCALE)
		return;
	if (flushymin > lineno) {
		if (flushymin - lineno > FLUSH_DIFF && flushymax != 0) {
			D3D_flushtexture (flushymin, flushymax);
			flushymin = mon->currentmode.amiga_height;
			flushymax = 0;
		} else {
			flushymin = lineno;
		}
	}
	if (flushymax < lineno) {
		if (lineno - flushymax > FLUSH_DIFF && flushymax != 0) {
			D3D_flushtexture (flushymin, flushymax);
			flushymin = mon->currentmode.amiga_height;
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
#endif

bool render_screen(int monid, int mode, bool immediate)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	bool v = false;
	int cnt;

	mon->render_ok = false;
	if (minimized || ad->picasso_on || monitor_off || dx_islost ()) {
		return mon->render_ok;
	}
	cnt = 0;
	while (mon->wait_render) {
		sleep_millis (1);
		cnt++;
		if (cnt > 500) {
			return mon->render_ok;
		}
	}
//	flushymin = 0;
//	flushymax = mon->currentmode.amiga_height;
	gfx_lock();
	if (mon->currentmode.flags & DM_D3D) {
		v = D3D_renderframe(monid, mode, immediate);
	} else if (mon->currentmode.flags & DM_SWSCALE) {
		S2X_render(monid, -1, -1);
		v = true;
	} else if (mon->currentmode.flags & DM_DDRAW) {
		v = true;
	}
	mon->render_ok = v;
	gfx_unlock();
	return mon->render_ok;
}

bool show_screen_maybe(int monid, bool show)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
	if (!ap->gfx_vflip || ap->gfx_vsyncmode == 0 || ap->gfx_vsync <= 0) {
		if (show)
			show_screen(monid, 0);
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
	struct AmigaMonitor *mon = &AMonitors[0];
	if (!mon->screen_is_initialized)
		return;
	if (!D3D_showframe_special)
		return;
	if (mon->currentmode.flags & DM_D3D) {
		gfx_lock();
		D3D_showframe_special(0, 1);
		gfx_unlock();
	}
}
static frame_time_t strobo_time;
static volatile bool strobo_active;
static volatile bool strobo_active2;

static void CALLBACK blackinsertion_cb(
	UINT      uTimerID,
	UINT      uMsg,
	DWORD_PTR dwUser,
	DWORD_PTR dw1,
	DWORD_PTR dw2
	)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	if (mon->screen_is_initialized)  {
		while (strobo_active) {
			frame_time_t ct = read_processor_time();
			int diff = (int)strobo_time - (int)ct;
			if (diff < -vsynctimebase / 2) {
				break;
			}
			if (diff <= 0) {
				if (strobo_active2)
					show_screen_special();
				break;
			}
			if (diff > vsynctimebase / 4) {
				break;
			}
		}
	}
	strobo_active = false;
}

float target_adjust_vblank_hz(int monid, float hz)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	int maxrate;
	if (!currprefs.lightboost_strobo)
		return hz;
	if (isfullscreen() > 0) {
		maxrate = mon->currentmode.freq;
	} else {
		maxrate = deskhz;
	}
	double nhz = hz * 2.0;
	if (nhz >= maxrate - 1 && nhz < maxrate + 1)
		hz -= 0.5;
	return hz;
}

void show_screen(int monid, int mode)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	strobo_active = false;
	strobo_active2 = false;
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];

	gfx_lock();
	if (mode == 2 || mode == 3 || mode == 4) {
		if ((mon->currentmode.flags & DM_D3D) && D3D_showframe_special && ap->gfx_strobo) {
			if (mode == 4) {
				// erase + render
				D3D_showframe_special(0, 2);
				D3D_showframe_special(0, 1);
			} else {
				// erase or render
				D3D_showframe_special(0, mode == 3 ? 2 : 1);
			}
		}
		gfx_unlock();
		return;
	}
	if (mode >= 0 && !mon->render_ok) {
		gfx_unlock();
		return;
	}
	if (mon->currentmode.flags & DM_D3D) {
#if 0
		if (ap->gfx_vsync < 0 && ap->gfx_strobo && currprefs.gfx_api < 2) {
			float vblank = vblank_hz;
			if (WIN32GFX_IsPicassoScreen(mon)) {
				if (currprefs.win32_rtgvblankrate > 0)
					vblank = currprefs.win32_rtgvblankrate;
			}
			bool ok = true;
			int ratio = currprefs.lightboost_strobo_ratio;
			int ms = (int)(1000 / vblank);
			int waitms = ms * ratio / 100 - 1;
			int maxrate;
			if (isfullscreen() > 0) {
				maxrate = mon->currentmode.freq;
			} else {
				maxrate = deskhz;
			}
			if (maxrate > 0) {
				double rate = vblank * 2.0;
				rate *= ratio > 50 ? ratio / 50.0 : 50.0 / ratio;
				if (rate > maxrate + 1.0)
					ok = false;
			}
			if (ok) {
				strobo_time = read_processor_time() + vsynctimebase * ratio / 100;
				strobo_active = true;
				timeSetEvent(waitms, 0, blackinsertion_cb, NULL, TIME_ONESHOT | TIME_CALLBACK_FUNCTION);
			}
		}
#endif
		D3D_showframe(monid);
		if (monid == 0)
			strobo_active2 = true;
#ifdef GFXFILTER
	} else if (mon->currentmode.flags & DM_SWSCALE) {
		if (!dx_islost () && !ad->picasso_on)
			DirectDraw_Flip(1);
#endif
	} else if (mon->currentmode.flags & DM_DDRAW) {
		if (!dx_islost () && !ad->picasso_on)
			DirectDraw_Flip(1);
	}
	gfx_unlock();
	mon->render_ok = false;
}

static uae_u8 *ddraw_dolock (void)
{
	struct vidbuf_description *avidinfo = &adisplays[0].gfxvidinfo;
	if (!DirectDraw_SurfaceLock ()) {
		dx_check ();
		return 0;
	}
	avidinfo->outbuffer->bufmem = DirectDraw_GetSurfacePointer ();
	avidinfo->outbuffer->rowbytes = DirectDraw_GetSurfacePitch ();
	init_row_map ();
	clear_inhibit_frame(0, IHF_WINDOWHIDDEN);
	return avidinfo->outbuffer->bufmem;
}

bool lockscr3d(struct vidbuffer *vb)
{
	struct AmigaMonitor *mon = &AMonitors[vb->monitor_id];
	if (mon->currentmode.flags & DM_D3D) {
		if (!(mon->currentmode.flags & DM_SWSCALE)) {
			vb->bufmem = D3D_locktexture(vb->monitor_id, &vb->rowbytes, NULL, false);
			if (vb->bufmem) 
				return true;
		}
	}
	return false;
}

void unlockscr3d(struct vidbuffer *vb)
{
	struct AmigaMonitor *mon = &AMonitors[vb->monitor_id];
	if (mon->currentmode.flags & DM_D3D) {
		if (!(mon->currentmode.flags & DM_SWSCALE)) {
			D3D_unlocktexture(vb->monitor_id, -1, -1);
		}
	}
}

int lockscr(struct vidbuffer *vb, bool fullupdate, bool first)
{
	struct AmigaMonitor *mon = &AMonitors[vb->monitor_id];
	int ret = 0;

	if (!isscreen(mon))
		return ret;
#if 0
	flushymin = mon->currentmode.amiga_height;
	flushymax = 0;
#endif
	ret = 1;
	if (mon->currentmode.flags & DM_D3D) {
#ifdef D3D
		if (mon->currentmode.flags & DM_SWSCALE) {
			ret = 1;
		} else {
			ret = 0;
			vb->bufmem = D3D_locktexture(vb->monitor_id, &vb->rowbytes, NULL, fullupdate);
			if (vb->bufmem) {
				if (first)
					init_row_map();
				ret = 1;
			}
		}
#endif
	} else if (mon->currentmode.flags & DM_SWSCALE) {
		ret = 1;
	} else if (mon->currentmode.flags & DM_DDRAW) {
		ret = ddraw_dolock() != 0;
	}
	return ret;
}

void unlockscr(struct vidbuffer *vb, int y_start, int y_end)
{
	struct AmigaMonitor *mon = &AMonitors[vb->monitor_id];
	if (mon->currentmode.flags & DM_D3D) {
		if (mon->currentmode.flags & DM_SWSCALE) {
			S2X_render(vb->monitor_id, y_start, y_end);
		} else {
			vb->bufmem = NULL;
		}
		D3D_unlocktexture(vb->monitor_id, y_start, y_end);
	} else if (mon->currentmode.flags & DM_SWSCALE) {
		return;
	} else if (mon->currentmode.flags & DM_DDRAW) {
		DirectDraw_SurfaceUnlock();
		vb->bufmem = NULL;
	}
}

void flush_clear_screen (struct vidbuffer *vb)
{
	if (!vb)
		return;
	if (lockscr(vb, true, true)) {
		int y;
		for (y = 0; y < vb->height_allocated; y++) {
			memset(vb->bufmem + y * vb->rowbytes, 0, vb->width_allocated * vb->pixbytes);
		}
		unlockscr(vb, -1, -1);
	}
}

static void DX_Blit96(struct AmigaMonitor *mon, int x, int y, int w, int h)
{
	struct picasso96_state_struct *state = &picasso96_state[mon->monitor_id];
	RECT dr, sr;

	picasso_offset_x = 0;
	picasso_offset_y = 0;
	picasso_offset_mx = 1.0;
	picasso_offset_my = 1.0;
	if (mon->scalepicasso) {
		int srcratio, dstratio;
		int srcwidth, srcheight;

		if (mon->scalepicasso < 0 || mon->scalepicasso > 1) {
			srcwidth = state->Width;
			srcheight = state->Height;
		} else {
			srcwidth = mon->currentmode.native_width;
			srcheight = mon->currentmode.native_height;
		}

		SetRect (&sr, 0, 0, state->Width, state->Height);
		if (currprefs.win32_rtgscaleaspectratio < 0) {
			// automatic
			srcratio = state->Width * ASPECTMULT / state->Height;
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
		picasso_offset_mx = (float)state->Width / (dr.right - dr.left);
		picasso_offset_my = (float)state->Height / (dr.bottom - dr.top);
		DirectDraw_BlitToPrimaryScale (&dr, &sr);
	} else {
		SetRect (&sr, x, y, x + w, y + h);
		DirectDraw_BlitToPrimary (&sr);
	}
}

void getrtgfilterrect2(int monid, RECT *sr, RECT *dr, RECT *zr, int dst_width, int dst_height)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct picasso96_state_struct *state = &picasso96_state[monid];

	SetRect (sr, 0, 0, mon->currentmode.native_width, mon->currentmode.native_height);
	SetRect (dr, 0, 0, state->Width, state->Height);
	SetRect (zr, 0, 0, 0, 0);
	
	picasso_offset_x = 0;
	picasso_offset_y = 0;
	picasso_offset_mx = 1.0;
	picasso_offset_my = 1.0;

	if (!ad->picasso_on)
		return;
	if (!mon->scalepicasso)
		return;

	int srcratio, dstratio;
	int srcwidth, srcheight;
	srcwidth = state->Width;
	srcheight = state->Height;
	if (!srcwidth || !srcheight)
		return;

	float mx = (float)mon->currentmode.native_width / srcwidth;
	float my = (float)mon->currentmode.native_height / srcheight;
	if (mon->scalepicasso == RTG_MODE_INTEGER_SCALE) {
		int divx = mon->currentmode.native_width / srcwidth;
		int divy = mon->currentmode.native_height / srcheight;
		int mul = divx > divy ? divy : divx;
		int xx = srcwidth * mul;
		int yy = srcheight * mul;
		SetRect (dr, 0, 0, mon->currentmode.native_width / mul, mon->currentmode.native_height / mul);
		//picasso_offset_x = -(state->Width - xx) / 2;
		//picasso_offset_y = -(mon->currentmode.native_height - srcheight) / 2;
		mx = my = 1.0;
	} else if (mon->scalepicasso == RTG_MODE_CENTER) {
		int xx = (mon->currentmode.native_width - srcwidth) / 2;
		int yy = (mon->currentmode.native_height - srcheight) / 2;
		picasso_offset_x = -xx;
		picasso_offset_y = -yy;
		SetRect (dr, 0, 0, mon->currentmode.native_width, mon->currentmode.native_height);
		mx = my = 1.0;
	} else {
		if (currprefs.win32_rtgscaleaspectratio < 0) {
			// automatic
			srcratio = srcwidth * ASPECTMULT / srcheight;
			dstratio = mon->currentmode.native_width * ASPECTMULT / mon->currentmode.native_height;
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
			picasso_offset_y = (state->Height - yy) / 2;
		} else {
			int xx = srcwidth * dstratio / srcratio;
			SetRect (dr, 0, 0, xx, srcheight);
			picasso_offset_x = (state->Width - xx) / 2;
		}
	}

	OffsetRect (zr, picasso_offset_x, picasso_offset_y);
	picasso_offset_mx = (float)srcwidth * mx / (dr->right - dr->left);
	picasso_offset_my = (float)srcheight * my / (dr->bottom - dr->top);
}

static uae_u8 *gfx_lock_picasso2(int monid, bool fullupdate)
{
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
	if (currprefs.gfx_api) {
		int pitch;
		uae_u8 *p = D3D_locktexture(monid, &pitch, NULL, fullupdate);
		vidinfo->rowbytes = pitch;
		return p;
	} else {
		if (!DirectDraw_SurfaceLock ()) {
			dx_check ();
			return 0;
		}
		vidinfo->rowbytes = DirectDraw_GetSurfacePitch ();
		return DirectDraw_GetSurfacePointer ();
	}
}
uae_u8 *gfx_lock_picasso(int monid, bool fullupdate, bool doclear)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
	static uae_u8 *p;
	if (mon->rtg_locked) {
		return p;
	}
	gfx_lock();
	p = gfx_lock_picasso2(monid, fullupdate);
	if (!p) {
		gfx_unlock();
	} else {
		mon->rtg_locked = true;
		if (doclear) {
			uae_u8 *p2 = p;
			for (int h = 0; h < vidinfo->height; h++) {
				memset (p2, 0, vidinfo->width * vidinfo->pixbytes);
				p2 += vidinfo->rowbytes;
			}
		}
	}
	return p;
}

void gfx_unlock_picasso(int monid, bool dorender)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	if (!mon->rtg_locked)
		gfx_lock();
	mon->rtg_locked = false;
	if (currprefs.gfx_api) {
		if (dorender) {
			if (mon->p96_double_buffer_needs_flushing) {
				D3D_flushtexture(monid, mon->p96_double_buffer_first, mon->p96_double_buffer_last);
				mon->p96_double_buffer_needs_flushing = 0;
			}
		}
		D3D_unlocktexture(monid, -1, -1);
		if (dorender) {
			if (D3D_renderframe(monid, true, false)) {
				gfx_unlock();
				mon->render_ok = true;
				show_screen_maybe(monid, true);
			} else {
				gfx_unlock();
			}
		} else {
			gfx_unlock();
		}
	} else {
		DirectDraw_SurfaceUnlock ();
		if (dorender) {
			if (mon->p96_double_buffer_needs_flushing) {
				DX_Blit96(mon, mon->p96_double_buffer_firstx, mon->p96_double_buffer_first,
					mon->p96_double_buffer_lastx - mon->p96_double_buffer_firstx + 1,
					mon->p96_double_buffer_last - mon->p96_double_buffer_first + 1);
				mon->p96_double_buffer_needs_flushing = 0;
			}
		}
		gfx_unlock();
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
	struct MultiDisplay *mdx = getdisplay(&currprefs, 0);
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

static void close_hwnds(struct AmigaMonitor *mon)
{
	if (mon->screen_is_initialized)
		releasecapture(mon);
	mon->screen_is_initialized = 0;
	if (!mon->monitor_id) {
		display_vblank_thread_kill();
#ifdef AVIOUTPUT
		AVIOutput_Restart();
#endif
#ifdef RETROPLATFORM
		rp_set_hwnd(NULL);
#endif
		closeblankwindows();
		rawinput_release();
	}
	if (mon->monitor_id > 0 && mon->hMainWnd)
		setmouseactive(mon->monitor_id, 0);
	deletestatusline(mon->monitor_id);
	if (mon->hStatusWnd) {
		ShowWindow(mon->hStatusWnd, SW_HIDE);
		DestroyWindow(mon->hStatusWnd);
		mon->hStatusWnd = 0;
		if (mon->hStatusBkgB)
			DeleteObject(mon->hStatusBkgB);
		mon->hStatusBkgB = NULL;
	}
	if (mon->hAmigaWnd) {
		addnotifications (mon->hAmigaWnd, TRUE, FALSE);
#ifdef D3D
		D3D_free(mon->monitor_id, true);
#endif
		ShowWindow (mon->hAmigaWnd, SW_HIDE);
		DestroyWindow (mon->hAmigaWnd);
		if (mon->hAmigaWnd == mon->hMainWnd)
			mon->hMainWnd = 0;
		mon->hAmigaWnd = 0;
	}
	if (mon->hMainWnd) {
		ShowWindow(mon->hMainWnd, SW_HIDE);
		DestroyWindow(mon->hMainWnd);
		mon->hMainWnd = 0;
	}
}

static bool canmatchdepth(void)
{
	if (!currprefs.win32_rtgmatchdepth)
		return false;
	if (currprefs.gfx_api >= 2)
		return false;
	return true;
}

static void updatemodes(struct AmigaMonitor *mon)
{
	struct uae_filter *usedfilter = mon->usedfilter;
	DWORD flags;

	mon->currentmode.fullfill = 0;
	flags = DM_DDRAW;
	if (isfullscreen () > 0)
		flags |= DM_DX_FULLSCREEN;
	else if (isfullscreen () < 0)
		flags |= DM_W_FULLSCREEN;
#if defined (GFXFILTER)
	if (usedfilter) {
		flags |= DM_SWSCALE;
		if (mon->currentmode.current_depth < 15)
			mon->currentmode.current_depth = 16;
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
	mon->currentmode.flags = flags;
	if (flags & DM_SWSCALE)
		mon->currentmode.fullfill = 1;
	if (flags & DM_W_FULLSCREEN) {
		RECT rc = getdisplay(&currprefs, mon->monitor_id)->rect;
		mon->currentmode.native_width = rc.right - rc.left;
		mon->currentmode.native_height = rc.bottom - rc.top;
		mon->currentmode.current_width = mon->currentmode.native_width;
		mon->currentmode.current_height = mon->currentmode.native_height;
	} else {
		mon->currentmode.native_width = mon->currentmode.current_width;
		mon->currentmode.native_height = mon->currentmode.current_height;
	}
}

static void update_gfxparams(struct AmigaMonitor *mon)
{
	struct picasso96_state_struct *state = &picasso96_state[mon->monitor_id];

	updatewinfsmode(mon->monitor_id, &currprefs);
#ifdef PICASSO96
	mon->currentmode.vsync = 0;
	if (mon->screen_is_picasso) {
		mon->currentmode.current_width = (int)(state->Width * currprefs.rtg_horiz_zoom_mult);
		mon->currentmode.current_height = (int)(state->Height * currprefs.rtg_vert_zoom_mult);
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
			mon->currentmode.vsync = 1 + currprefs.gfx_apmode[1].gfx_vsyncmode;
	} else {
#endif
		mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
		mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
		if (currprefs.gfx_apmode[0].gfx_vsync)
			mon->currentmode.vsync = 1 + currprefs.gfx_apmode[0].gfx_vsyncmode;
#ifdef PICASSO96
	}
#endif
#if FORCE16BIT
	mon->currentmode.current_depth = 16;
#else
	mon->currentmode.current_depth = currprefs.color_mode < 5 ? 16 : 32;
#endif
	if (mon->screen_is_picasso && canmatchdepth() && isfullscreen () > 0) {
		int pbits = state->BytesPerPixel * 8;
		if (pbits <= 8) {
			if (mon->currentmode.current_depth == 32)
				pbits = 32;
			else
				pbits = 16;
		}
		if (pbits == 24)
			pbits = 32;
		mon->currentmode.current_depth = pbits;
	}
	mon->currentmode.amiga_width = mon->currentmode.current_width;
	mon->currentmode.amiga_height = mon->currentmode.current_height;

	mon->scalepicasso = 0;
	if (mon->screen_is_picasso) {
		if (isfullscreen () < 0) {
			if ((currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER || currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_SCALE || currprefs.win32_rtgallowscaling) && (state->Width != mon->currentmode.native_width || state->Height != mon->currentmode.native_height))
				mon->scalepicasso = 1;
			if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER)
				mon->scalepicasso = currprefs.gf[1].gfx_filter_autoscale;
			if (!mon->scalepicasso && currprefs.win32_rtgscaleaspectratio)
				mon->scalepicasso = -1;
		} else if (isfullscreen () > 0) {
			if (!canmatchdepth()) { // can't scale to different color depth
				if (mon->currentmode.native_width > state->Width && mon->currentmode.native_height > state->Height) {
					if (currprefs.gf[1].gfx_filter_autoscale)
						mon->scalepicasso = 1;
				}
				if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER)
					mon->scalepicasso = currprefs.gf[1].gfx_filter_autoscale;
				if (!mon->scalepicasso && currprefs.win32_rtgscaleaspectratio)
					mon->scalepicasso = -1;
			}
		} else if (isfullscreen () == 0) {
			if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_INTEGER_SCALE) {
				mon->scalepicasso = RTG_MODE_INTEGER_SCALE;
				mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
				mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
			} else if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER) {
				if (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width < state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height < state->Height) {
					if (!currprefs.win32_rtgallowscaling) {
						;
					} else if (currprefs.win32_rtgscaleaspectratio) {
						mon->scalepicasso = -1;
						mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
						mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
					}
				} else {
					mon->scalepicasso = 2;
					mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
					mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
				}
			} else if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_SCALE) {
				if (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width > state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height > state->Height)
					mon->scalepicasso = 1;
				if ((currprefs.gfx_monitor[mon->monitor_id].gfx_size.width != state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height != state->Height) && currprefs.win32_rtgallowscaling) {
					mon->scalepicasso = 1;
				} else if (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width < state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height < state->Height) {
					// no always scaling and smaller? Back to normal size and set new configured max size
					mon->currentmode.current_width = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.width = state->Width;
					mon->currentmode.current_height = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.height = state->Height;
				} else if (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width == state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height == state->Height) {
					;
				} else if (!mon->scalepicasso && currprefs.win32_rtgscaleaspectratio) {
					mon->scalepicasso = -1;
				}
			} else {
				if ((currprefs.gfx_monitor[mon->monitor_id].gfx_size.width != state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height != state->Height) && currprefs.win32_rtgallowscaling)
					mon->scalepicasso = 1;
				if (!mon->scalepicasso && currprefs.win32_rtgscaleaspectratio)
					mon->scalepicasso = -1;
			}
		}

		if (mon->scalepicasso > 0 && (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width != state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height != state->Height)) {
			mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
			mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
		}
	}

}

static int open_windows(struct AmigaMonitor *mon, bool mousecapture, bool started)
{
	bool recapture = false;
	int ret;

	mon->screen_is_initialized = 0;

	if (mon->monitor_id && mouseactive)
		recapture = true;

	inputdevice_unacquire();
	reset_sound();
	if (mon->hAmigaWnd == NULL)
		wait_keyrelease();

	mon->in_sizemove = 0;

	updatewinfsmode(mon->monitor_id, &currprefs);
#ifdef D3D
	gfx_lock();
	D3D_free(mon->monitor_id, false);
	gfx_unlock();
#endif
	if (!DirectDraw_Start())
		return 0;

	int init_round = 0;
	ret = -2;
	do {
		if (ret < -1) {
			updatemodes(mon);
			update_gfxparams(mon);
		}
		ret = doInit(mon);
		init_round++;
		if (ret < -9) {
			DirectDraw_Release();
			if (!DirectDraw_Start())
				return 0;
		}
	} while (ret < 0);

	if (!ret) {
		DirectDraw_Release();
		return ret;
	}

	bool startactive = (started && mouseactive) || (!started && !currprefs.win32_start_uncaptured && !currprefs.win32_start_minimized);
	bool startpaused = !started && ((currprefs.win32_start_minimized && currprefs.win32_iconified_pause) || (currprefs.win32_start_uncaptured && currprefs.win32_inactive_pause && isfullscreen() <= 0));
	bool startminimized = !started && currprefs.win32_start_minimized && isfullscreen() <= 0;
	int input = 0;

	if ((mousecapture && startactive) || recapture)
		setmouseactive(mon->monitor_id, -1);

	int upd = 0;
	if (startactive) {
		setpriority(&priorities[currprefs.win32_active_capture_priority]);
		upd = 2;
	} else if (startminimized) {
		setpriority(&priorities[currprefs.win32_iconified_priority]);
		setminimized(mon->monitor_id);
		input = currprefs.win32_inactive_input;
		upd = 1;
	} else {
		setpriority(&priorities[currprefs.win32_inactive_priority]);
		input = currprefs.win32_inactive_input;
		upd = 2;
	}
	if (upd > 1) {
		for (int i = 0; i < NUM_LEDS; i++)
			gui_flicker_led(i, -1, -1);
		gui_led(LED_POWER, gui_data.powerled, gui_data.powerled_brightness);
		gui_fps(0, 0, 0);
		if (gui_data.md >= 0)
			gui_led(LED_MD, 0, -1);
		for (int i = 0; i < 4; i++) {
			if (currprefs.floppyslots[i].dfxtype >= 0)
				gui_led(LED_DF0 + i, 0, -1);
		}
	}
	if (upd > 0) {
		inputdevice_acquire(TRUE);
		if (!isfocus())
			inputdevice_unacquire(true, input);
	}

	if (startpaused)
		setpaused(1);

	statusline_updated(mon->monitor_id);
	refreshtitle();

	return ret;
}

static void reopen_gfx(struct AmigaMonitor *mon)
{
	open_windows(mon, false, true);
	if (isfullscreen () <= 0)
		DirectDraw_FillPrimary ();
	render_screen(mon->monitor_id, 1, true);
}

static int getstatuswindowheight(int monid)
{
	if (monid > 0)
		return 0;
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
	bool monitors[MAX_AMIGAMONITORS];

	if (!config_changed && !display_change_requested)
		return 0;

	c |= currprefs.win32_statusbar != changed_prefs.win32_statusbar ? 512 : 0;

	for (int i = 0; i < MAX_AMIGADISPLAYS; i++) {
		monitors[i] = false;
		int c2 = 0;
		c2 |= currprefs.gfx_monitor[i].gfx_size_fs.width != changed_prefs.gfx_monitor[i].gfx_size_fs.width ? 16 : 0;
		c2 |= currprefs.gfx_monitor[i].gfx_size_fs.height != changed_prefs.gfx_monitor[i].gfx_size_fs.height ? 16 : 0;
		c2 |= ((currprefs.gfx_monitor[i].gfx_size_win.width + 7) & ~7) != ((changed_prefs.gfx_monitor[i].gfx_size_win.width + 7) & ~7) ? 16 : 0;
		c2 |= currprefs.gfx_monitor[i].gfx_size_win.height != changed_prefs.gfx_monitor[i].gfx_size_win.height ? 16 : 0;
		if (c2) {
			c |= c2;
			monitors[i] = true;
		}
	}
	monitors[0] = true;

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
	c |= currprefs.gfx_autoresolution_vga != changed_prefs.gfx_autoresolution_vga ? (2|8|16) : 0;
	c |= currprefs.gfx_api != changed_prefs.gfx_api ? (1 | 8 | 32) : 0;
	c |= currprefs.gfx_api_options != changed_prefs.gfx_api_options ? (1 | 8 | 32) : 0;
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
		c |= gf->gfx_filter_scanlineratio != gfc->gfx_filter_scanlineratio ? (1 | 8) : 0;
		c |= gf->gfx_filter_scanlineoffset != gfc->gfx_filter_scanlineoffset ? (1 | 8) : 0;

		c |= gf->gfx_filter_horiz_zoom_mult != gfc->gfx_filter_horiz_zoom_mult ? (1) : 0;
		c |= gf->gfx_filter_vert_zoom_mult != gfc->gfx_filter_vert_zoom_mult ? (1) : 0;

		c |= gf->gfx_filter_filtermodeh != gfc->gfx_filter_filtermodeh ? (2 | 8) : 0;
		c |= gf->gfx_filter_filtermodev != gfc->gfx_filter_filtermodev ? (2 | 8) : 0;
		c |= gf->gfx_filter_bilinear != gfc->gfx_filter_bilinear ? (2|8|16) : 0;
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

	c |= currprefs.rtg_horiz_zoom_mult != changed_prefs.rtg_horiz_zoom_mult ? 16 : 0;
	c |= currprefs.rtg_vert_zoom_mult != changed_prefs.rtg_vert_zoom_mult ? 16 : 0;

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
	c |= currprefs.genlock_alpha != changed_prefs.genlock_alpha ? (1 | 8) : 0;
	c |= currprefs.genlock_mix != changed_prefs.genlock_mix ? (1 | 256) : 0;
	c |= currprefs.genlock_aspect != changed_prefs.genlock_aspect ? (1 | 256) : 0;
	c |= currprefs.genlock_scale != changed_prefs.genlock_scale ? (1 | 256) : 0;
	c |= _tcsicmp(currprefs.genlock_image_file, changed_prefs.genlock_image_file) ? (2 | 8) : 0;
	c |= _tcsicmp(currprefs.genlock_video_file, changed_prefs.genlock_video_file) ? (2 | 8) : 0;

	c |= currprefs.gfx_lores_mode != changed_prefs.gfx_lores_mode ? (2 | 8) : 0;
	c |= currprefs.gfx_scandoubler != changed_prefs.gfx_scandoubler ? (2 | 8) : 0;
	c |= currprefs.gfx_threebitcolors != changed_prefs.gfx_threebitcolors ? (256) : 0;
	c |= currprefs.gfx_grayscale != changed_prefs.gfx_grayscale ? (512) : 0;

	c |= currprefs.gfx_display_sections != changed_prefs.gfx_display_sections ? (512) : 0;
	c |= currprefs.gfx_variable_sync != changed_prefs.gfx_variable_sync ? 1 : 0;
	c |= currprefs.gfx_windowed_resize != changed_prefs.gfx_windowed_resize ? 1 : 0;

	c |= currprefs.gfx_apmode[APMODE_NATIVE].gfx_display != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_display ? (2|4|8) : 0;
	c |= currprefs.gfx_apmode[APMODE_RTG].gfx_display != changed_prefs.gfx_apmode[APMODE_RTG].gfx_display ? (2|4|8) : 0;
	c |= currprefs.gfx_blackerthanblack != changed_prefs.gfx_blackerthanblack ? (2 | 8) : 0;
	c |= currprefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers ? (2 | 16) : 0;
	c |= currprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced ? (2 | 8) : 0;
	c |= currprefs.gfx_apmode[APMODE_RTG].gfx_backbuffers != changed_prefs.gfx_apmode[APMODE_RTG].gfx_backbuffers ? (2 | 16) : 0;

	c |= currprefs.win32_main_alwaysontop != changed_prefs.win32_main_alwaysontop ? 32 : 0;
	c |= currprefs.win32_gui_alwaysontop != changed_prefs.win32_gui_alwaysontop ? 2 : 0;
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

		currprefs.gfx_autoresolution = changed_prefs.gfx_autoresolution;
		currprefs.gfx_autoresolution_vga = changed_prefs.gfx_autoresolution_vga;
		currprefs.color_mode = changed_prefs.color_mode;
		currprefs.lightboost_strobo = changed_prefs.lightboost_strobo;

		if (currprefs.gfx_api != changed_prefs.gfx_api) {
			display_change_requested = 1;
		}

		if (c & 128) {
			// hres/vres change
			rp_screenmode_changed();
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
		currprefs.genlock_alpha = changed_prefs.genlock_alpha;
		currprefs.genlock_aspect = changed_prefs.genlock_aspect;
		currprefs.genlock_scale = changed_prefs.genlock_scale;
		_tcscpy(currprefs.genlock_image_file, changed_prefs.genlock_image_file);
		_tcscpy(currprefs.genlock_video_file, changed_prefs.genlock_video_file);

		currprefs.gfx_lores_mode = changed_prefs.gfx_lores_mode;
		currprefs.gfx_scandoubler = changed_prefs.gfx_scandoubler;
		currprefs.gfx_threebitcolors = changed_prefs.gfx_threebitcolors;
		currprefs.gfx_grayscale = changed_prefs.gfx_grayscale;

		currprefs.gfx_display_sections = changed_prefs.gfx_display_sections;
		currprefs.gfx_variable_sync = changed_prefs.gfx_variable_sync;
		currprefs.gfx_windowed_resize = changed_prefs.gfx_windowed_resize;

		currprefs.gfx_apmode[APMODE_NATIVE].gfx_display = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_display;
		currprefs.gfx_apmode[APMODE_RTG].gfx_display = changed_prefs.gfx_apmode[APMODE_RTG].gfx_display;
		currprefs.gfx_blackerthanblack = changed_prefs.gfx_blackerthanblack;
		currprefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers;
		currprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced;
		currprefs.gfx_apmode[APMODE_RTG].gfx_backbuffers = changed_prefs.gfx_apmode[APMODE_RTG].gfx_backbuffers;

		currprefs.win32_main_alwaysontop = changed_prefs.win32_main_alwaysontop;
		currprefs.win32_gui_alwaysontop = changed_prefs.win32_gui_alwaysontop;
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
		for (int monid = MAX_AMIGAMONITORS - 1; monid >= 0; monid--) {
			if (!monitors[monid])
				continue;
			struct AmigaMonitor *mon = &AMonitors[monid];

			if (c & 64) {
				if (!unacquired) {
					inputdevice_unacquire();
					unacquired = true;
				}
				DirectDraw_Fill(NULL, 0);
				DirectDraw_BlitToPrimary(NULL);
			}
			if (c & 256) {
				init_colors(mon->monitor_id);
				reset_drawing();
			}
			if (c & 128) {
				if (currprefs.gfx_autoresolution) {
					c |= 2 | 8;
				} else {
					c |= 16;
					reset_drawing();
					S2X_reset(mon->monitor_id);
				}
			}
			if (c & 1024) {
				target_graphics_buffer_update(mon->monitor_id);
			}
			if (c & 512) {
				reopen_gfx(mon);
			}
			if ((c & 16) || ((c & 8) && keepfsmode)) {
				if (reopen(mon, c & 2, unacquired == false)) {
					c |= 2;
				} else {
					unacquired = true;
				}
			}
			if ((c & 32) || ((c & 2) && !keepfsmode)) {
				if (!unacquired) {
					inputdevice_unacquire();
					unacquired = true;
				}
				close_windows(mon);
				if (currprefs.gfx_api != changed_prefs.gfx_api || currprefs.gfx_api_options != changed_prefs.gfx_api_options) {
					currprefs.gfx_api = changed_prefs.gfx_api;
					currprefs.gfx_api_options = changed_prefs.gfx_api_options;
					d3d_select(&currprefs);
				}
				graphics_init(dontcapture ? false : true);
			}
		}

		init_custom();
		if (c & 4) {
			pause_sound();
			reset_sound();
			resume_sound();
		}

		if (setpause || dontcapture) {
			if (!unacquired)
				inputdevice_unacquire();
			unacquired = false;
		}

		if (unacquired)
			inputdevice_acquire(TRUE);
		
		if (setpause)
			setpaused(1);

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
	currprefs.harddrive_read_only = changed_prefs.harddrive_read_only;

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
		currprefs.input_mouse_untrap != changed_prefs.input_mouse_untrap ||
		currprefs.win32_minimize_inactive != changed_prefs.win32_minimize_inactive ||
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
		currprefs.win32_shutdown_notification != changed_prefs.win32_shutdown_notification ||
		currprefs.win32_warn_exit != changed_prefs.win32_warn_exit ||
		currprefs.right_control_is_right_win_key != changed_prefs.right_control_is_right_win_key)
	{
		currprefs.win32_minimize_inactive = changed_prefs.win32_minimize_inactive;
		currprefs.leds_on_screen = changed_prefs.leds_on_screen;
		currprefs.keyboard_leds[0] = changed_prefs.keyboard_leds[0];
		currprefs.keyboard_leds[1] = changed_prefs.keyboard_leds[1];
		currprefs.keyboard_leds[2] = changed_prefs.keyboard_leds[2];
		currprefs.input_mouse_untrap = changed_prefs.input_mouse_untrap;
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
		currprefs.win32_shutdown_notification = changed_prefs.win32_shutdown_notification;
		currprefs.win32_warn_exit = changed_prefs.win32_warn_exit;
		currprefs.right_control_is_right_win_key = changed_prefs.right_control_is_right_win_key;
		inputdevice_unacquire ();
		currprefs.keyboard_leds_in_use = changed_prefs.keyboard_leds_in_use = (currprefs.keyboard_leds[0] | currprefs.keyboard_leds[1] | currprefs.keyboard_leds[2]) != 0;
		pause_sound ();
		resume_sound ();
		refreshtitle();
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
		currprefs.sound_volume_midi != changed_prefs.sound_volume_midi ||
		currprefs.win32_midirouter != changed_prefs.win32_midirouter)
	{
		currprefs.win32_midiindev = changed_prefs.win32_midiindev;
		currprefs.win32_midioutdev = changed_prefs.win32_midioutdev;
		currprefs.sound_volume_midi = changed_prefs.sound_volume_midi;
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

void init_colors(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	/* init colors */
	if (mon->currentmode.flags & DM_D3D) {
		D3D_getpixelformat (mon->currentmode.current_depth,
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

	if (!(mon->currentmode.flags & (DM_D3D))) {
		if (mon->currentmode.current_depth != mon->currentmode.native_depth) {
			if (mon->currentmode.current_depth == 16) {
				red_bits = 5; green_bits = 6; blue_bits = 5;
				red_shift = 11; green_shift = 5; blue_shift = 0;
			} else {
				red_bits = green_bits = blue_bits = 8;
				red_shift = 16; green_shift = 8; blue_shift = 0;
			}
		}
	}
	alloc_colors64k(monid, red_bits, green_bits, blue_bits, red_shift,green_shift, blue_shift, alpha_bits, alpha_shift, alpha, 0, mon->usedfilter && mon->usedfilter->yuv);
	notice_new_xcolors ();
#ifdef GFXFILTER
	S2X_configure(monid, red_bits, green_bits, blue_bits, red_shift,green_shift, blue_shift);
#endif
#ifdef AVIOUTPUT
	AVIOutput_RGBinfo (red_bits, green_bits, blue_bits, alpha_bits, red_shift, green_shift, blue_shift, alpha_shift);
#endif
	Screenshot_RGBinfo (red_bits, green_bits, blue_bits, alpha_bits, red_shift, green_shift, blue_shift, alpha_shift);
}

#ifdef PICASSO96

int picasso_palette(struct MyCLUTEntry *CLUT, uae_u32 *clut)
{
	int changed = 0;

	for (int i = 0; i < 256; i++) {
		int r = CLUT[i].Red;
		int g = CLUT[i].Green;
		int b = CLUT[i].Blue;
		uae_u32 v = (doMask256 (r, red_bits, red_shift)
			| doMask256 (g, green_bits, green_shift)
			| doMask256 (b, blue_bits, blue_shift))
			| doMask256 (0xff, alpha_bits, alpha_shift);
		if (v != clut[i]) {
			//write_log (_T("%d:%08x\n"), i, v);
			clut[i] = v;
			changed = 1;
		}
	}
	return changed;
}

void DX_Invalidate(struct AmigaMonitor *mon, int x, int y, int width, int height)
{
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[mon->monitor_id];
	int last, lastx;

	if (width == 0 || height == 0)
		return;
	if (y < 0 || height < 0) {
		y = 0;
		height = vidinfo->height;
	}
	if (x < 0 || width < 0) {
		x = 0;
		width = vidinfo->width;
	}
	last = y + height - 1;
	lastx = x + width - 1;
	mon->p96_double_buffer_first = y;
	mon->p96_double_buffer_last  = last;
	mon->p96_double_buffer_firstx = x;
	mon->p96_double_buffer_lastx = lastx;
	mon->p96_double_buffer_needs_flushing = 1;
}

#endif

static void open_screen(struct AmigaMonitor *mon)
{
	close_windows(mon);
	open_windows(mon, true, true);
}

static int ifs(struct AmigaMonitor *mon, struct uae_prefs *p)
{
	int idx = mon->screen_is_picasso ? 1 : 0;
	return p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLSCREEN ? 1 : (p->gfx_apmode[idx].gfx_fullscreen == GFX_FULLWINDOW ? -1 : 0);
}

static int reopen(struct AmigaMonitor *mon, int full, bool unacquire)
{
	struct amigadisplay *ad = &adisplays[mon->monitor_id];
	int quick = 0;
	int idx = mon->screen_is_picasso ? 1 : 0;
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];

	updatewinfsmode(mon->monitor_id, &changed_prefs);

	if (changed_prefs.gfx_apmode[0].gfx_fullscreen != currprefs.gfx_apmode[0].gfx_fullscreen && !mon->screen_is_picasso)
		full = 1;
	if (changed_prefs.gfx_apmode[1].gfx_fullscreen != currprefs.gfx_apmode[1].gfx_fullscreen && mon->screen_is_picasso)
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

	currprefs.gfx_monitor[mon->monitor_id].gfx_size_fs.width = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_fs.width;
	currprefs.gfx_monitor[mon->monitor_id].gfx_size_fs.height = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_fs.height;
	currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.width = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.width;
	currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.height = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.height;
	currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.x = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.x;
	currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.y = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.y;

	currprefs.gfx_apmode[0].gfx_fullscreen = changed_prefs.gfx_apmode[0].gfx_fullscreen;
	currprefs.gfx_apmode[1].gfx_fullscreen = changed_prefs.gfx_apmode[1].gfx_fullscreen;
	currprefs.gfx_apmode[0].gfx_vsync = changed_prefs.gfx_apmode[0].gfx_vsync;
	currprefs.gfx_apmode[1].gfx_vsync = changed_prefs.gfx_apmode[1].gfx_vsync;
	currprefs.gfx_apmode[0].gfx_vsyncmode = changed_prefs.gfx_apmode[0].gfx_vsyncmode;
	currprefs.gfx_apmode[1].gfx_vsyncmode = changed_prefs.gfx_apmode[1].gfx_vsyncmode;
	currprefs.gfx_apmode[0].gfx_refreshrate = changed_prefs.gfx_apmode[0].gfx_refreshrate;

	currprefs.rtg_horiz_zoom_mult = changed_prefs.rtg_horiz_zoom_mult;
	currprefs.rtg_vert_zoom_mult = changed_prefs.rtg_vert_zoom_mult;

#if 0
	currprefs.gfx_apmode[1].gfx_refreshrate = changed_prefs.gfx_apmode[1].gfx_refreshrate;
#endif
	set_config_changed ();

	if (!quick)
		return 1;

	if (unacquire) {
		inputdevice_unacquire ();
	}

	reopen_gfx(mon);

	return 0;
}

bool vsync_switchmode(int monid, int hz)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	static struct PicassoResolution *oldmode;
	static int oldhz;
	int w = mon->currentmode.native_width;
	int h = mon->currentmode.native_height;
	int d = mon->currentmode.native_depth / 8;
	struct MultiDisplay *md = getdisplay(&currprefs, monid);
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
		changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_fs.height = newh;
		changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate = hz;
		changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced = lace;
		if (changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_fs.height != currprefs.gfx_monitor[mon->monitor_id].gfx_size_fs.height ||
			changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate != currprefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate) {
			write_log (_T("refresh rate changed to %d%s, new screenmode %dx%d\n"), hz, lace ? _T("i") : _T("p"), w, newh);
			set_config_changed ();
		}
		return true;
	}
}

void vsync_clear(void)
{
	vsync_active = false;
	if (waitvblankevent)
		ResetEvent(waitvblankevent);
}

int vsync_isdone(frame_time_t *dt)
{
	if (isvsync() == 0)
		return -1;
	if (waitvblankthread_mode <= 0)
		return -2;
	if (dt)
		*dt = wait_vblank_timestamp;
	return vsync_active ? 1 : 0;
}

#ifdef PICASSO96

static int modeswitchneeded(struct AmigaMonitor *mon, struct winuae_currentmode *wc)
{
	struct vidbuf_description *avidinfo = &adisplays[mon->monitor_id].gfxvidinfo;
	struct picasso96_state_struct *state = &picasso96_state[mon->monitor_id];

	if (isfullscreen () > 0) {
		/* fullscreen to fullscreen */
		if (mon->screen_is_picasso) {
			if (state->BytesPerPixel > 1 && state->BytesPerPixel * 8 != wc->current_depth && canmatchdepth())
				return -1;
			if (state->Width < wc->current_width && state->Height < wc->current_height) {
				if ((currprefs.gf[1].gfx_filter_autoscale == 1 || (currprefs.gf[1].gfx_filter_autoscale == 2 && currprefs.win32_rtgallowscaling)) && !canmatchdepth())
					return 0;
			}
			if (state->Width != wc->current_width ||
				state->Height != wc->current_height)
				return 1;
			if (state->Width == wc->current_width &&
				state->Height == wc->current_height) {
					if (state->BytesPerPixel * 8 == wc->current_depth || state->BytesPerPixel == 1)
						return 0;
					if (!canmatchdepth())
						return 0;
			}
			return 1;
		} else {
			if (mon->currentmode.current_width != wc->current_width ||
				mon->currentmode.current_height != wc->current_height ||
				mon->currentmode.current_depth != wc->current_depth)
				return -1;
			if (!avidinfo->outbuffer->bufmem_lockable)
				return -1;
		}
	} else if (isfullscreen () == 0) {
		/* windowed to windowed */
		return -1;
	} else {
		/* fullwindow to fullwindow */
		DirectDraw_Fill (NULL, 0);
		DirectDraw_BlitToPrimary (NULL);
		if (mon->screen_is_picasso) {
			if (currprefs.gf[1].gfx_filter_autoscale && ((wc->native_width > state->Width && wc->native_height >= state->Height) || (wc->native_height > state->Height && wc->native_width >= state->Width)))
				return -1;
			if (currprefs.win32_rtgallowscaling && (state->Width != wc->native_width || state->Height != wc->native_height))
				return -1;
#if 0
			if (wc->native_width < state->Width || wc->native_height < state->Height)
				return 1;
#endif
		}
		return -1;
	}
	return 0;
}

void gfx_set_picasso_state(int monid, int on)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct winuae_currentmode wc;
	struct apmode *newmode, *oldmode;
	struct gfx_filterdata *newf, *oldf;
	int mode;

	if (mon->screen_is_picasso == on)
		return;
	mon->screen_is_picasso = on;
	rp_rtg_switch ();
	memcpy (&wc, &mon->currentmode, sizeof (wc));

	newmode = &currprefs.gfx_apmode[on ? 1 : 0];
	oldmode = &currprefs.gfx_apmode[on ? 0 : 1];

	newf = &currprefs.gf[on ? 1 : 0];
	oldf = &currprefs.gf[on ? 0 : 1];

	updatemodes(mon);
	update_gfxparams(mon);
	clearscreen();

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
		int m = modeswitchneeded(mon, &wc);
		if (m > 0)
			mode = m;
		if (m < 0 && !mode)
			mode = m;
		if (!mode)
			goto end;
	}
	if (mode < 0) {
		open_windows(mon, true, true);
	} else {
		open_screen(mon); // reopen everything
	}
end:
#ifdef RETROPLATFORM
	rp_set_hwnd (mon->hAmigaWnd);
#endif
}

void gfx_set_picasso_modeinfo(int monid, RGBFTYPE rgbfmt)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	int need;
	if (!mon->screen_is_picasso)
		return;
	clearscreen();
	gfx_set_picasso_colors(monid, rgbfmt);
	updatemodes(mon);
	need = modeswitchneeded(mon, &mon->currentmode);
	update_gfxparams(mon);
	if (need > 0) {
		open_screen(mon);
	} else if (need < 0) {
		open_windows(mon, true, true);
	}
#ifdef RETROPLATFORM
	rp_set_hwnd(mon->hAmigaWnd);
#endif
}
#endif

void gfx_set_picasso_colors(int monid, RGBFTYPE rgbfmt)
{
	alloc_colors_picasso(red_bits, green_bits, blue_bits, red_shift, green_shift, blue_shift, rgbfmt, p96_rgbx16);
}

static void gfxmode_reset(int monid)
{
	struct amigadisplay *ad = &adisplays[monid];
	struct uae_filter **usedfilter = &AMonitors[monid].usedfilter;

#ifdef GFXFILTER
	*usedfilter = NULL;
	if (currprefs.gf[ad->picasso_on].gfx_filter > 0) {
		int i = 0;
		while (uaefilters[i].name) {
			if (uaefilters[i].type == currprefs.gf[ad->picasso_on].gfx_filter) {
				*usedfilter = &uaefilters[i];
				break;
			}
			i++;
		}
	}
#endif
}

int machdep_init(void)
{
	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		struct AmigaMonitor *mon = &AMonitors[i];
		struct amigadisplay *ad = &adisplays[i];
		mon->monitor_id = i;
		ad->picasso_requested_on = 0;
		ad->picasso_on = 0;
		mon->screen_is_picasso = 0;
		memset(&mon->currentmode, 0, sizeof(*&mon->currentmode));
	}
#ifdef LOGITECHLCD
	lcd_open ();
#endif
	systray (hHiddenWnd, FALSE);
	return 1;
}

void machdep_free(void)
{
#ifdef LOGITECHLCD
	lcd_close ();
#endif
}

int graphics_init(bool mousecapture)
{
	systray (hHiddenWnd, TRUE);
	systray (hHiddenWnd, FALSE);
	d3d_select(&currprefs);
	gfxmode_reset(0);
	if (open_windows(&AMonitors[0], mousecapture, false)) {
		if (currprefs.monitoremu_mon > 0 && currprefs.monitoremu) {
			gfxmode_reset(currprefs.monitoremu_mon);
			open_windows(&AMonitors[currprefs.monitoremu_mon], mousecapture, false);
		}
		return true;
	}
	return false;
}

int graphics_setup(void)
{
	if (!screen_cs_allocated) {
		InitializeCriticalSection(&screen_cs);
		screen_cs_allocated = true;
	}
#ifdef PICASSO96
	InitPicasso96(0);
#endif
	return 1;
}

void graphics_leave(void)
{
	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		close_windows(&AMonitors[i]);
	}
}

uae_u32 OSDEP_minimize_uae (void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	return ShowWindow(mon->hAmigaWnd, SW_MINIMIZE);
}

typedef HRESULT (CALLBACK* DWMENABLEMMCSS)(BOOL);
static void setDwmEnableMMCSS (bool state)
{
	if (!os_vista)
		return;
	DWMENABLEMMCSS pDwmEnableMMCSS;
	pDwmEnableMMCSS = (DWMENABLEMMCSS)GetProcAddress(GetModuleHandle(_T("dwmapi.dll")), "DwmEnableMMCSS");
	if (pDwmEnableMMCSS)
		pDwmEnableMMCSS (state);
}

void close_windows(struct AmigaMonitor *mon)
{
	struct vidbuf_description *avidinfo = &adisplays[mon->monitor_id].gfxvidinfo;

	setDwmEnableMMCSS (FALSE);
	reset_sound ();
#if defined (GFXFILTER)
	S2X_free(mon->monitor_id);
#endif
	freevidbuffer(mon->monitor_id, &avidinfo->drawbuffer);
	freevidbuffer(mon->monitor_id, &avidinfo->tempbuffer);
	DirectDraw_Release();
	close_hwnds(mon);
}

static void createstatuswindow(struct AmigaMonitor *mon)
{
	HDC hdc;
	RECT rc;
	HLOCAL hloc;
	LPINT lpParts;
	int drive_width, hd_width, cd_width, power_width;
	int fps_width, idle_width, snd_width, joy_width, net_width;
	int joys = currprefs.win32_statusbar > 1 ? 2 : 0;
	int num_parts = 12 + joys + 1;
	double scaleX, scaleY;
	WINDOWINFO wi;
	int extra;

	if (mon->hStatusWnd) {
		ShowWindow(mon->hStatusWnd, SW_HIDE);
		DestroyWindow(mon->hStatusWnd);
	}
	if (currprefs.win32_statusbar == 0 || mon->monitor_id > 0)
		return;
	if (isfullscreen () != 0)
		return;
	if (currprefs.win32_borderless)
		return;

	mon->hStatusWnd = CreateWindowEx (
		WS_EX_COMPOSITED, STATUSCLASSNAME, (LPCTSTR) NULL, SBARS_TOOLTIPS | WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, mon->hMainWnd, (HMENU) 1, hInst, NULL);
	if (!mon->hStatusWnd)
		return;
	wi.cbSize = sizeof wi;
	GetWindowInfo(mon->hMainWnd, &wi);
	extra = wi.rcClient.top - wi.rcWindow.top;

	hdc = GetDC(mon->hStatusWnd);
	scaleX = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0;
	scaleY = GetDeviceCaps(hdc, LOGPIXELSY) / 96.0;
	ReleaseDC(mon->hStatusWnd, hdc);
	drive_width = (int)(24 * scaleX);
	hd_width = (int)(24 * scaleX);
	cd_width = (int)(24 * scaleX);
	power_width = (int)(42 * scaleX);
	fps_width = (int)(64 * scaleX);
	idle_width = (int)(64 * scaleX);
	net_width = (int)(24 * scaleX);
	if (is_ppc_cpu(&currprefs))
		idle_width += (int)(68 * scaleX);
	if (is_x86_cpu(&currprefs))
		idle_width += (int)(68 * scaleX);
	snd_width = (int)(72 * scaleX);
	joy_width = (int)(24 * scaleX);
	GetClientRect(mon->hMainWnd, &rc);
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
		int startx = rc.right - (drive_width * 4) - power_width - idle_width - fps_width - cd_width - hd_width - snd_width - net_width - joys * joy_width - extra;
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
		// net
		lpParts[i] = lpParts[i - 1] + cd_width;
		i++;
		// df0
		lpParts[i] = lpParts[i - 1] + net_width;
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
		window_led_drives = lpParts[i1 + 3];
		window_led_drives_end = lpParts[i1 + 3 + 4];

		/* Create the parts */
		SendMessage(mon->hStatusWnd, SB_SETPARTS, (WPARAM)num_parts, (LPARAM)lpParts);
		LocalUnlock(hloc);
		LocalFree(hloc);
	}
	registertouch(mon->hStatusWnd);
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

static int getbestmode(struct AmigaMonitor *mon, int nextbest)
{
	int i, startidx;
	struct MultiDisplay *md;
	int ratio;
	int index = -1;

	for(;;) {
		md = getdisplay2(&currprefs, index);
		if (!md)
			return 0;
		ratio = mon->currentmode.native_width > mon->currentmode.native_height ? 1 : 0;
		for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
			struct PicassoResolution *pr = &md->DisplayModes[i];
			if (pr->res.width == mon->currentmode.native_width && pr->res.height == mon->currentmode.native_height)
				break;
		}
		if (md->DisplayModes[i].depth >= 0) {
			if (!nextbest)
				break;
			while (md->DisplayModes[i].res.width == mon->currentmode.native_width && md->DisplayModes[i].res.height == mon->currentmode.native_height)
				i++;
		} else {
			i = 0;
		}
		// first iterate only modes that have similar aspect ratio
		startidx = i;
		for (; md->DisplayModes[i].depth >= 0; i++) {
			struct PicassoResolution *pr = &md->DisplayModes[i];
			int r = pr->res.width > pr->res.height ? 1 : 0;
			if (pr->res.width >= mon->currentmode.native_width && pr->res.height >= mon->currentmode.native_height && r == ratio) {
				write_log (_T("FS: %dx%d -> %dx%d %d %d\n"), mon->currentmode.native_width, mon->currentmode.native_height,
					pr->res.width, pr->res.height, ratio, index);
				mon->currentmode.native_width = pr->res.width;
				mon->currentmode.native_height = pr->res.height;
				mon->currentmode.current_width = mon->currentmode.native_width;
				mon->currentmode.current_height = mon->currentmode.native_height;
				goto end;
			}
		}
		// still not match? check all modes
		i = startidx;
		for (; md->DisplayModes[i].depth >= 0; i++) {
			struct PicassoResolution *pr = &md->DisplayModes[i];
			int r = pr->res.width > pr->res.height ? 1 : 0;
			if (pr->res.width >= mon->currentmode.native_width && pr->res.height >= mon->currentmode.native_height) {
				write_log (_T("FS: %dx%d -> %dx%d\n"), mon->currentmode.native_width, mon->currentmode.native_height,
					pr->res.width, pr->res.height);
				mon->currentmode.native_width = pr->res.width;
				mon->currentmode.native_height = pr->res.height;
				mon->currentmode.current_width = mon->currentmode.native_width;
				mon->currentmode.current_height = mon->currentmode.native_height;
				goto end;
			}
		}
		index++;
	}
end:
	if (index >= 0) {
		currprefs.gfx_apmode[mon->screen_is_picasso ? APMODE_RTG : APMODE_NATIVE].gfx_display = 
			changed_prefs.gfx_apmode[mon->screen_is_picasso ? APMODE_RTG : APMODE_NATIVE].gfx_display = index;
		write_log (L"Can't find mode %dx%d ->\n", mon->currentmode.native_width, mon->currentmode.native_height);
		write_log (L"Monitor switched to '%s'\n", md->adaptername);
	}
	return 1;
}

float target_getcurrentvblankrate(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	float vb;
	if (currprefs.gfx_variable_sync)
		return (float)mon->currentmode.freq;
	if (get_display_vblank_params(-1, NULL, NULL, &vb, NULL)) {
		return vb;
	} else if (currprefs.gfx_api) {
		return D3D_getrefreshrate(0);
	} else {
		return (float)DirectDraw_CurrentRefreshRate();
	}
}

static void movecursor (int x, int y)
{
	write_log (_T("SetCursorPos %dx%d\n"), x, y);
	SetCursorPos (x, y);
}

static void getextramonitorpos(struct AmigaMonitor *mon, RECT *r)
{
	typedef HRESULT(CALLBACK* DWMGETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute);
	static DWMGETWINDOWATTRIBUTE pDwmGetWindowAttribute;
	static HMODULE dwmapihandle;

	RECT r1, r2;
	if (!pDwmGetWindowAttribute && !dwmapihandle && os_vista) {
		dwmapihandle = LoadLibrary(_T("dwmapi.dll"));
		if (dwmapihandle)
			pDwmGetWindowAttribute = (DWMGETWINDOWATTRIBUTE)GetProcAddress(dwmapihandle, "DwmGetWindowAttribute");
	}

	// find rightmost window edge
	int monid = MAX_AMIGAMONITORS - 1;
	int rightmon = -1;
	int rightedge = 0;
	HWND hwnd = NULL;
	for (;;) {
		if (monid < 1)
			break;
		monid--;
		hwnd = AMonitors[monid].hMainWnd;
		if (!hwnd)
			continue;
		GetWindowRect(hwnd, &r1);
		if (r1.right > rightedge) {
			rightedge = r1.right;
			rightmon = monid;
		}
	}
	if (rightmon < 0)
		return;
	hwnd = AMonitors[rightmon].hMainWnd;
	GetWindowRect(hwnd, &r1);
	r2 = r1;

	if (pDwmGetWindowAttribute) {
		pDwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r2, sizeof(r2));
	}
	int width = r->right - r->left;
	int height = r->bottom - r->top;

	r->left = r1.right - ((r2.left - r1.left) + (r1.right - r2.right));
	r->top = r1.top;
	r->bottom = r->top + height;
	r->right = r->left + width;
}

static int create_windows_2(struct AmigaMonitor *mon)
{
	static bool firstwindow = true;
	int dxfs = mon->currentmode.flags & (DM_DX_FULLSCREEN);
	int d3dfs = mon->currentmode.flags & (DM_D3D_FULLSCREEN);
	int fsw = mon->currentmode.flags & (DM_W_FULLSCREEN);
	DWORD exstyle = (currprefs.win32_notaskbarbutton ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW) | 0;
	DWORD flags = 0;
	int borderless = currprefs.win32_borderless;
	DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	int cyborder = GetSystemMetrics (SM_CYFRAME);
	int gap = 0;
	int x, y, w, h;
	struct MultiDisplay *md;
	int sbheight;

	md = getdisplay(&currprefs, mon->monitor_id);
	if (mon->monitor_id && fsw) {
		struct MultiDisplay *md2 = NULL;
		int idx = 0;
		for (;;) {
			md2 = getdisplay2(&currprefs, idx);
			if (md2 == md)
				break;
			if (!md2)
				break;
			idx++;
		}
		for (int i = 0; i <= mon->monitor_id; i++) {
			md2 = getdisplay2(&currprefs, idx);
			if (!md2)
				idx = 0;
			else
				idx++;
		}
		if (md2)
			md = md2;
	}
	mon->md = md;

	sbheight = currprefs.win32_statusbar ? getstatuswindowheight(mon->monitor_id) : 0;

	if (mon->hAmigaWnd) {
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
		GetWindowRect (mon->hAmigaWnd, &r);
		x = r.left;
		y = r.top;
		w = r.right - r.left;
		h = r.bottom - r.top;
		nx = x;
		ny = y;

		if (mon->screen_is_picasso) {
			nw = mon->currentmode.current_width;
			nh = mon->currentmode.current_height;
		} else {
			nw = currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.width;
			nh = currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.height;
		}

		if (fsw || dxfs) {
			RECT rc = md->rect;
			nx = rc.left;
			ny = rc.top;
			nw = rc.right - rc.left;
			nh = rc.bottom - rc.top;
		} else if (d3dfs) {
			RECT rc = md->rect;
			nw = mon->currentmode.native_width;
			nh = mon->currentmode.native_height;
			if (rc.left >= 0)
				nx = rc.left;
			else
				nx = rc.left + (rc.right - rc.left - nw);
			if (rc.top >= 0)
				ny = rc.top;
			else
				ny = rc.top + (rc.bottom - rc.top - nh);
		}
		if (w != nw || h != nh || x != nx || y != ny || sbheight != mon->prevsbheight) {
			w = nw;
			h = nh;
			x = nx;
			y = ny;
			mon->in_sizemove++;
			if (mon->hMainWnd && !fsw && !dxfs && !d3dfs && !rp_isactive ()) {
				mon->window_extra_height += (sbheight - mon->prevsbheight);
				GetWindowRect(mon->hMainWnd, &r);
				x = r.left;
				y = r.top;
				SetWindowPos(mon->hMainWnd, HWND_TOP, x, y, w + mon->window_extra_width, h + mon->window_extra_height,
					SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
				x = gap;
				y = gap;
			}
			SetWindowPos(mon->hAmigaWnd, HWND_TOP, x, y, w, h,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
			mon->in_sizemove--;
		} else {
			w = nw;
			h = nh;
			x = nx;
			y = ny;
		}
		createstatuswindow(mon);
		createstatusline(mon->monitor_id);
		updatewinrect(mon, false);
		GetWindowRect (mon->hMainWnd, &mon->mainwin_rect);
		if (d3dfs || dxfs)
			movecursor (x + w / 2, y + h / 2);
		write_log (_T("window already open (%dx%d %dx%d)\n"),
			mon->amigawin_rect.left, mon->amigawin_rect.top, mon->amigawin_rect.right - mon->amigawin_rect.left, mon->amigawin_rect.bottom - mon->amigawin_rect.top);
		updatemouseclip(mon);
		rp_screenmode_changed ();
		mon->prevsbheight = sbheight;
		return 1;
	}

	if (fsw && !borderless)
		borderless = 1;
	window_led_drives = 0;
	window_led_drives_end = 0;
	mon->hMainWnd = NULL;
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
			stored_x = currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.x;
			stored_y = currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.y;
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

			rc.right = rc.left + gap + mon->currentmode.current_width + gap;
			rc.bottom = rc.top + gap + mon->currentmode.current_height + gap + sbheight;

			oldx = rc.left;
			oldy = rc.top;
			AdjustWindowRect (&rc, borderless ? WS_POPUP : style, FALSE);
			mon->win_x_diff = rc.left - oldx;
			mon->win_y_diff = rc.top - oldy;

			if (MonitorFromRect (&rc, MONITOR_DEFAULTTONULL) == NULL) {
				write_log (_T("window coordinates are not visible on any monitor, reseting..\n"));
				stored_x = stored_y = 0;
				continue;
			}

			if (mon->monitor_id > 0) {
				getextramonitorpos(mon, &rc);
			}
			break;
		}

		if (fsw) {
			rc = md->rect;
			flags |= WS_EX_TOPMOST;
			style = WS_POPUP;
			mon->currentmode.native_width = rc.right - rc.left;
			mon->currentmode.native_height = rc.bottom - rc.top;
		}
		flags |= (currprefs.win32_main_alwaysontop ? WS_EX_TOPMOST : 0);

		if (!borderless) {
			RECT rc2;
			mon->hMainWnd = CreateWindowEx(WS_EX_ACCEPTFILES | exstyle | flags,
				_T("PCsuxRox"), _T("WinUAE"),
				style,
				rc.left, rc.top,
				rc.right - rc.left, rc.bottom - rc.top,
				NULL, NULL, hInst, NULL);
			if (!mon->hMainWnd) {
				write_log (_T("main window creation failed\n"));
				return 0;
			}
			GetWindowRect(mon->hMainWnd, &rc2);
			mon->window_extra_width = rc2.right - rc2.left - mon->currentmode.current_width;
			mon->window_extra_height = rc2.bottom - rc2.top - mon->currentmode.current_height;
			createstatuswindow(mon);
			createstatusline(mon->monitor_id);
		} else {
			x = rc.left;
			y = rc.top;
		}
		w = mon->currentmode.native_width;
		h = mon->currentmode.native_height;

	} else {

		RECT rc;
		getbestmode(mon, 0);
		w = mon->currentmode.native_width;
		h = mon->currentmode.native_height;
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
		mon->hAmigaWnd = CreateWindowEx (dxfs || d3dfs ? WS_EX_ACCEPTFILES | WS_EX_TOPMOST : WS_EX_ACCEPTFILES | WS_EX_TOOLWINDOW | (currprefs.win32_main_alwaysontop ? WS_EX_TOPMOST : 0),
			_T("AmigaPowah"), _T("WinUAE"),
			WS_POPUP,
			0, 0, w, h,
			parent, NULL, hInst, NULL);
	} else {
		mon->hAmigaWnd = CreateWindowEx (
			((dxfs || d3dfs || currprefs.win32_main_alwaysontop) ? WS_EX_TOPMOST : WS_EX_ACCEPTFILES) | exstyle,
			_T("AmigaPowah"), _T("WinUAE"),
			((dxfs || d3dfs || currprefs.headless) ? WS_POPUP : (WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (mon->hMainWnd ? WS_VISIBLE | WS_CHILD : WS_VISIBLE | WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX))),
			x, y, w, h,
			borderless ? NULL : (mon->hMainWnd ? mon->hMainWnd : NULL),
			NULL, hInst, NULL);
	}
	if (!mon->hAmigaWnd) {
		write_log (_T("creation of amiga window failed\n"));
		close_hwnds(mon);
		return 0;
	}
	if (mon->hMainWnd == NULL) {
		mon->hMainWnd = mon->hAmigaWnd;
		registertouch(mon->hAmigaWnd);
	} else {
		registertouch(mon->hMainWnd);
		registertouch(mon->hAmigaWnd);
	}

	updatewinrect(mon, true);
	GetWindowRect(mon->hMainWnd, &mon->mainwin_rect);
	if (dxfs || d3dfs)
		movecursor (x + w / 2, y + h / 2);
	addnotifications (mon->hAmigaWnd, FALSE, FALSE);
	mon->prevsbheight = sbheight;

	if (mon->monitor_id) {
		ShowWindow(mon->hMainWnd, SW_SHOWNOACTIVATE);
		UpdateWindow(mon->hMainWnd);
		ShowWindow(mon->hAmigaWnd, SW_SHOWNOACTIVATE);
		UpdateWindow(mon->hAmigaWnd);
	} else {
		createblankwindows();
		if (mon->hMainWnd != mon->hAmigaWnd) {
			if (!currprefs.headless && !rp_isactive())
				ShowWindow(mon->hMainWnd, firstwindow ? (currprefs.win32_start_minimized ? SW_SHOWMINIMIZED : SW_SHOWDEFAULT) : SW_SHOWNORMAL);
			UpdateWindow(mon->hMainWnd);
		}
		if (!currprefs.headless && !rp_isactive())
			ShowWindow(mon->hAmigaWnd, SW_SHOWNORMAL);
		UpdateWindow(mon->hAmigaWnd);
		firstwindow = false;
		setDwmEnableMMCSS(true);
		rawinput_alloc();

		if (currprefs.win32_shutdown_notification && !rp_isactive()) {
			typedef BOOL(WINAPI *SHUTDOWNBLOCKREASONCREATE)(HWND, LPCWSTR);
			SHUTDOWNBLOCKREASONCREATE pShutdownBlockReasonCreate;
			pShutdownBlockReasonCreate = (SHUTDOWNBLOCKREASONCREATE)GetProcAddress(GetModuleHandle(_T("user32.dll")), "ShutdownBlockReasonCreate");
			if (pShutdownBlockReasonCreate) {
				TCHAR tmp[MAX_DPATH];
				WIN32GUI_LoadUIString(IDS_SHUTDOWN_NOTIFICATION, tmp, MAX_DPATH);
				if (!pShutdownBlockReasonCreate(mon->hMainWnd, tmp)) {
					write_log(_T("ShutdownBlockReasonCreate %08x\n"), GetLastError());
				}
			}
		}
	}

	return 1;
}

static int set_ddraw(struct AmigaMonitor *mon)
{
	int cnt, ret;

	cnt = 3;
	for (;;) {
		ret = set_ddraw_2(mon);
		if (cnt-- <= 0)
			return 0;
		if (ret < 0) {
			getbestmode(mon, 1);
			continue;
		}
		if (ret == 0)
			return 0;
		break;
	}
	return 1;
}

static void allocsoftbuffer(int monid, const TCHAR *name, struct vidbuffer *buf, int flags, int width, int height, int depth)
{
	struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;

	buf->monitor_id = monid;
	buf->pixbytes = (depth + 7) / 8;
	buf->width_allocated = (width + 7) & ~7;
	buf->height_allocated = height;

	if (!(flags & DM_SWSCALE)) {

		if (buf != &vidinfo->drawbuffer)
			return;

		buf->bufmem = NULL;
		buf->bufmemend = NULL;
		buf->realbufmem = NULL;
		buf->bufmem_allocated = NULL;
		buf->bufmem_lockable = true;

		write_log (_T("Mon %d reserved %s temp buffer (%d*%d*%d)\n"), monid, name, width, height, depth);

	} else if (flags & DM_SWSCALE) {

		int w = buf->width_allocated;
		int h = buf->height_allocated;
		int size = (w * 2) * (h * 2) * buf->pixbytes;
		buf->rowbytes = w * 2 * buf->pixbytes;
		buf->realbufmem = xcalloc(uae_u8, size);
		buf->bufmem_allocated = buf->bufmem = buf->realbufmem + (h / 2) * buf->rowbytes + (w / 2) * buf->pixbytes;
		buf->bufmemend = buf->realbufmem + size - buf->rowbytes;
		buf->bufmem_lockable = true;

		write_log (_T("Mon %d allocated %s temp buffer (%d*%d*%d) = %p\n"), monid, name, width, height, depth, buf->realbufmem);
	}
}

static int create_windows(struct AmigaMonitor *mon)
{
	if (!create_windows_2(mon))
		return 0;

	return set_ddraw(mon);
}

static int oldtex_w, oldtex_h, oldtex_rtg;

static BOOL doInit(struct AmigaMonitor *mon)
{
	int tmp_depth;
	int ret = 0;

retry:
	struct vidbuf_description *avidinfo = &adisplays[mon->monitor_id].gfxvidinfo;
	struct amigadisplay *ad = &adisplays[mon->monitor_id];

	if (wasfullwindow_a == 0)
		wasfullwindow_a = currprefs.gfx_apmode[0].gfx_fullscreen == GFX_FULLWINDOW ? 1 : -1;
	if (wasfullwindow_p == 0)
		wasfullwindow_p = currprefs.gfx_apmode[1].gfx_fullscreen == GFX_FULLWINDOW ? 1 : -1;
	gfxmode_reset(mon->monitor_id);
	freevidbuffer(mon->monitor_id, &avidinfo->drawbuffer);
	freevidbuffer(mon->monitor_id, &avidinfo->tempbuffer);

	for (;;) {
		updatemodes(mon);
		mon->currentmode.native_depth = 0;
		tmp_depth = mon->currentmode.current_depth;

		if (mon->currentmode.flags & DM_W_FULLSCREEN) {
			RECT rc = getdisplay(&currprefs, mon->monitor_id)->rect;
			mon->currentmode.native_width = rc.right - rc.left;
			mon->currentmode.native_height = rc.bottom - rc.top;
		}

		if (isfullscreen() <= 0 && !(mon->currentmode.flags & (DM_D3D))) {
			mon->currentmode.current_depth = DirectDraw_GetCurrentDepth ();
			updatemodes(mon);
		}
		if (!(mon->currentmode.flags & (DM_D3D)) && DirectDraw_GetCurrentDepth () == mon->currentmode.current_depth) {
			updatemodes(mon);
		}
#if 0
		TCHAR tmpstr[300];
		int fs_warning = -1;
		if (!rp_isactive () && (mon->currentmode.current_width > GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			mon->currentmode.current_height > GetSystemMetrics(SM_CYVIRTUALSCREEN))) {
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
			if (mon->screen_is_picasso)
				changed_prefs.gfx_apmode[1].gfx_fullscreen = currprefs.gfx_apmode[1].gfx_fullscreen = GFX_FULLSCREEN;
			else
				changed_prefs.gfx_apmode[0].gfx_fullscreen = currprefs.gfx_apmode[0].gfx_fullscreen = GFX_FULLSCREEN;
			updatewinfsmode(mon->monitor_id, &currprefs);
			updatewinfsmode(mon->monitor_id, &changed_prefs);
			mon->currentmode.current_depth = tmp_depth;
			updatemodes(mon);
			ret = -2;
			goto oops;
		}
#endif
		if (!create_windows(mon))
			goto oops;
#ifdef PICASSO96
		if (mon->screen_is_picasso) {
			break;
		} else {
#endif
			struct uae_filter *usedfilter = mon->usedfilter;
			mon->currentmode.native_depth = mon->currentmode.current_depth;

			if (currprefs.gfx_resolution > avidinfo->gfx_resolution_reserved)
				avidinfo->gfx_resolution_reserved = currprefs.gfx_resolution;
			if (currprefs.gfx_vresolution > avidinfo->gfx_vresolution_reserved)
				avidinfo->gfx_vresolution_reserved = currprefs.gfx_vresolution;

			//gfxvidinfo.drawbuffer.gfx_resolution_reserved = RES_SUPERHIRES;

#if defined (GFXFILTER)
			if (mon->currentmode.flags & (DM_D3D | DM_SWSCALE)) {
				if (!currprefs.gfx_autoresolution) {
					mon->currentmode.amiga_width = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
					mon->currentmode.amiga_height = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;
				} else {
					mon->currentmode.amiga_width = AMIGA_WIDTH_MAX << avidinfo->gfx_resolution_reserved;
					mon->currentmode.amiga_height = AMIGA_HEIGHT_MAX << avidinfo->gfx_vresolution_reserved;
				}
				if (avidinfo->gfx_resolution_reserved == RES_SUPERHIRES)
					mon->currentmode.amiga_height *= 2;
				if (mon->currentmode.amiga_height > 1280)
					mon->currentmode.amiga_height = 1280;

				avidinfo->drawbuffer.inwidth = avidinfo->drawbuffer.outwidth = mon->currentmode.amiga_width;
				avidinfo->drawbuffer.inheight = avidinfo->drawbuffer.outheight = mon->currentmode.amiga_height;

				if (usedfilter) {
					if ((usedfilter->flags & (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) == (UAE_FILTER_MODE_16 | UAE_FILTER_MODE_32)) {
						mon->currentmode.current_depth = mon->currentmode.native_depth;
					} else {
						mon->currentmode.current_depth = (usedfilter->flags & UAE_FILTER_MODE_32) ? 32 : 16;
					}
				}
				mon->currentmode.pitch = mon->currentmode.amiga_width * mon->currentmode.current_depth >> 3;
			}
			else
#endif
			{
				mon->currentmode.amiga_width = mon->currentmode.current_width;
				mon->currentmode.amiga_height = mon->currentmode.current_height;
			}
			avidinfo->drawbuffer.pixbytes = mon->currentmode.current_depth >> 3;
			avidinfo->drawbuffer.bufmem = NULL;
			avidinfo->drawbuffer.linemem = NULL;
			avidinfo->drawbuffer.rowbytes = mon->currentmode.pitch;
			break;
#ifdef PICASSO96
		}
#endif
	}

#ifdef PICASSO96
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[mon->monitor_id];
	vidinfo->rowbytes = 0;
	vidinfo->pixbytes = mon->currentmode.current_depth / 8;
	vidinfo->rgbformat = 0;
	vidinfo->extra_mem = 1;
	vidinfo->height = mon->currentmode.current_height;
	vidinfo->width = mon->currentmode.current_width;
	vidinfo->depth = mon->currentmode.current_depth;
	vidinfo->offset = 0;
#endif
	if (!scrlinebuf)
		scrlinebuf = xmalloc (uae_u8, max_uae_width * 4);

	avidinfo->drawbuffer.emergmem = scrlinebuf; // memcpy from system-memory to video-memory

	avidinfo->drawbuffer.realbufmem = NULL;
	avidinfo->drawbuffer.bufmem = NULL;
	avidinfo->drawbuffer.bufmem_allocated = NULL;
	avidinfo->drawbuffer.bufmem_lockable = false;

	avidinfo->outbuffer = &avidinfo->drawbuffer;
	avidinfo->inbuffer = &avidinfo->drawbuffer;

	if (!mon->screen_is_picasso) {

		if (currprefs.gfx_api == 0 && currprefs.gf[0].gfx_filter == 0) {
			allocsoftbuffer(mon->monitor_id, _T("draw"), &avidinfo->drawbuffer, mon->currentmode.flags,
				mon->currentmode.native_width, mon->currentmode.native_height, mon->currentmode.current_depth);
		} else {
			allocsoftbuffer(mon->monitor_id, _T("draw"), &avidinfo->drawbuffer, mon->currentmode.flags,
				1600, 1280, mon->currentmode.current_depth);
		}
		if (currprefs.monitoremu || currprefs.cs_cd32fmv || (currprefs.genlock && currprefs.genlock_image) || currprefs.cs_color_burst || currprefs.gfx_grayscale) {
			allocsoftbuffer(mon->monitor_id, _T("monemu"), &avidinfo->tempbuffer, mon->currentmode.flags,
				mon->currentmode.amiga_width > 1024 ? mon->currentmode.amiga_width : 1024,
				mon->currentmode.amiga_height > 1024 ? mon->currentmode.amiga_height : 1024,
				mon->currentmode.current_depth);
		}

		init_row_map ();
	}
	init_colors(mon->monitor_id);

	S2X_free(mon->monitor_id);
	oldtex_w = oldtex_h = -1;
	if (mon->currentmode.flags & DM_D3D) {
		int fmh = mon->screen_is_picasso ? 1 : currprefs.gf[ad->picasso_on].gfx_filter_filtermodeh + 1;
		int fmv = mon->screen_is_picasso ? 1 : currprefs.gf[ad->picasso_on].gfx_filter_filtermodev + 1 - 1;
		if (currprefs.gf[ad->picasso_on].gfx_filter_filtermodev == 0) {
			fmv = fmh;
		}
		const TCHAR *err = D3D_init(mon->hAmigaWnd, mon->monitor_id, mon->currentmode.native_width, mon->currentmode.native_height,
			mon->currentmode.current_depth, &mon->currentmode.freq, fmh, fmv);
			if (err) {
			if (currprefs.gfx_api == 2) {
				D3D_free(0, true);
				if (err[0] == 0 && currprefs.color_mode != 5) {
					changed_prefs.color_mode = currprefs.color_mode = 5;
					update_gfxparams(mon);
					goto retry;
				}
				changed_prefs.gfx_api = currprefs.gfx_api = 1;
				d3d_select(&currprefs);
				error_log(_T("Direct3D11 failed to initialize ('%s'), falling back to Direct3D9."), err);
				err = D3D_init(mon->hAmigaWnd, mon->monitor_id, mon->currentmode.native_width, mon->currentmode.native_height,
					mon->currentmode.current_depth, &mon->currentmode.freq, fmh, fmv);
			}
			if (err) {
				D3D_free(0, true);
				error_log(_T("Direct3D9 failed to initialize ('%s'), falling back to DirectDraw."), err);
				changed_prefs.gfx_api = currprefs.gfx_api = 0;
				changed_prefs.gf[ad->picasso_on].gfx_filter = currprefs.gf[ad->picasso_on].gfx_filter = 1;
				mon->currentmode.current_depth = mon->currentmode.native_depth;
				gfxmode_reset(mon->monitor_id);
				DirectDraw_Start();
				ret = -1;
				goto oops;
			}
		}
		target_graphics_buffer_update(mon->monitor_id);
		updatewinrect(mon, true);
	}

	mon->screen_is_initialized = 1;

	display_param_init(mon);

	createstatusline(mon->monitor_id);
	picasso_refresh(mon->monitor_id);
#ifdef RETROPLATFORM
	rp_set_hwnd_delayed ();
#endif

	if (isfullscreen () != 0)
		setmouseactive(mon->monitor_id, -1);

	return 1;

oops:
	close_hwnds(mon);
	return ret;
}

bool target_graphics_buffer_update(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
	struct vidbuf_description *avidinfo = &adisplays[0].gfxvidinfo;
	struct picasso96_state_struct *state = &picasso96_state[monid];

	static bool	graphicsbuffer_retry;
	int w, h;
	
	graphicsbuffer_retry = false;
	if (mon->screen_is_picasso) {
		w = state->Width > vidinfo->width ? state->Width : vidinfo->width;
		h = state->Height > vidinfo->height ? state->Height : vidinfo->height;
	} else {
		struct vidbuffer *vb = avidinfo->drawbuffer.tempbufferinuse ? &avidinfo->tempbuffer : &avidinfo->drawbuffer;
		avidinfo->outbuffer = vb;
		w = vb->outwidth;
		h = vb->outheight;
	}
	
	if (oldtex_w == w && oldtex_h == h && oldtex_rtg == mon->screen_is_picasso)
		return false;

	if (!w || !h) {
		oldtex_w = w;
		oldtex_h = h;
		oldtex_rtg = mon->screen_is_picasso;
		return false;
	}

	S2X_free(mon->monitor_id);
	if (mon->currentmode.flags & DM_D3D) {
		if (!D3D_alloctexture(mon->monitor_id, w, h)) {
			graphicsbuffer_retry = true;
			return false;
		}
	} else {
		DirectDraw_ClearSurface (NULL);
	}

	oldtex_w = w;
	oldtex_h = h;
	oldtex_rtg = mon->screen_is_picasso;

	write_log (_T("Buffer size (%d*%d) %s\n"), w, h, mon->screen_is_picasso ? _T("RTG") : _T("Native"));

	if ((mon->currentmode.flags & DM_SWSCALE) && !mon->screen_is_picasso) {
		if (!S2X_init(mon->monitor_id, mon->currentmode.native_width, mon->currentmode.native_height, mon->currentmode.native_depth))
			return false;
	}
	return true;
}

static void updatedisplayarea2(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	if (!mon->screen_is_initialized)
		return;
	if (dx_islost())
		return;
#if defined (GFXFILTER)
	if (mon->currentmode.flags & DM_D3D) {
#if defined (D3D)
		D3D_refresh(monid);
#endif
	} else
#endif
		if (mon->currentmode.flags & DM_DDRAW) {
#if defined (GFXFILTER)
			if (!ad->picasso_on) {
				if (mon->currentmode.flags & DM_SWSCALE)
					S2X_refresh(monid);
			}
#endif
			DirectDraw_Flip(0);
		}
}

void updatedisplayarea(int monid)
{
	if (monid >= 0) {
		updatedisplayarea2(monid);
	} else {
		for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
			updatedisplayarea2(i);
		}
	}

}

void updatewinfsmode(int monid, struct uae_prefs *p)
{
	struct MultiDisplay *md;

	fixup_prefs_dimensions (p);
	if (isfullscreen_2 (p) != 0) {
		p->gfx_monitor[monid].gfx_size = p->gfx_monitor[monid].gfx_size_fs;
	} else {
		p->gfx_monitor[monid].gfx_size = p->gfx_monitor[monid].gfx_size_win;
	}
	md = getdisplay(p, monid);
	set_config_changed ();
}

bool toggle_3d_debug(void)
{
	if (isvsync_chipset() < 0) {
		beamracer_debug = !beamracer_debug;
		if (D3D_debug) {
			D3D_debug(0, beamracer_debug);
		}
		reset_drawing();
		return true;
	}
	return false;
}



int rtg_index = -1;

// -2 = default
// -1 = prev
// 0 = chipset
// 1..4 = rtg
// 5 = next
bool toggle_rtg (int monid, int mode)
{
	struct amigadisplay *ad = &adisplays[monid];

	int old_index = rtg_index;

	if (monid > 0) {
		return true;
	}

	if (mode < -1 && rtg_index >= 0)
		return true;

	for (;;) {
		if (mode == -1) {
			rtg_index--;
		} else if (mode >= 0 && mode <= MAX_RTG_BOARDS) {
			rtg_index = mode - 1;
		} else {
			rtg_index++;
		}
		if (rtg_index >= MAX_RTG_BOARDS) {
			rtg_index = -1;
		} else if (rtg_index < -1) {
			rtg_index = MAX_RTG_BOARDS - 1;
		}
		if (rtg_index < 0) {
			if (ad->picasso_on) {
				gfxboard_rtg_disable(monid, old_index);
				ad->picasso_requested_on = false;
				statusline_add_message(STATUSTYPE_DISPLAY, _T("Chipset display"));
				set_config_changed();
				return false;
			}
			return false;
		}
		struct rtgboardconfig *r = &currprefs.rtgboards[rtg_index];
		if (r->rtgmem_size > 0 && r->monitor_id == monid) {
			if (r->rtgmem_type >= GFXBOARD_HARDWARE) {
				int idx = gfxboard_toggle(r->monitor_id, rtg_index, mode >= -1);
				if (idx >= 0) {
					rtg_index = idx;
					return true;
				}
				if (idx < -1) {
					rtg_index = -1;
					return false;
				}
			} else {
				gfxboard_toggle(r->monitor_id, -1, -1);
				if (mode < -1)
					return true;
				devices_unsafeperiod();
				gfxboard_rtg_disable(monid, old_index);
				// can always switch from RTG to custom
				if (ad->picasso_requested_on && ad->picasso_on) {
					ad->picasso_requested_on = false;
					rtg_index = -1;
					set_config_changed();
					return true;
				}
				if (ad->picasso_on)
					return false;
				// can only switch from custom to RTG if there is some mode active
				if (picasso_is_active(r->monitor_id)) {
					picasso_enablescreen(r->monitor_id, 1);
					ad->picasso_requested_on = true;
					statusline_add_message(STATUSTYPE_DISPLAY, _T("RTG %d: %s"), rtg_index + 1, _T("UAEGFX"));
					set_config_changed();
					return true;
				}
			}
		}
		if (mode >= 0 && mode <= MAX_RTG_BOARDS) {
			rtg_index = old_index;
			return false;
		}
	}
	return false;
}

void close_rtg(int monid)
{
	close_windows(&AMonitors[monid]);
}

void toggle_fullscreen(int monid, int mode)
{
	struct amigadisplay *ad = &adisplays[monid];
	int *p = ad->picasso_on ? &changed_prefs.gfx_apmode[1].gfx_fullscreen : &changed_prefs.gfx_apmode[0].gfx_fullscreen;
	int wfw = ad->picasso_on ? wasfullwindow_p : wasfullwindow_a;
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
	} else if (mode == 10) {
		v = GFX_WINDOW;
	}
	*p = v;
	devices_unsafeperiod();
	updatewinfsmode(monid, &changed_prefs);
}

HDC gethdc(int monid)
{
	HDC hdc = 0;

#ifdef D3D
	if (D3D_isenabled(0))
		return D3D_getDC(monid, 0);
#endif
	if(FAILED(DirectDraw_GetDC(&hdc)))
		hdc = 0;
	return hdc;
}

void releasehdc(int monid, HDC hdc)
{
#ifdef D3D
	if (D3D_isenabled(0)) {
		D3D_getDC(monid, hdc);
		return;
	}
#endif
	DirectDraw_ReleaseDC(hdc);
}
