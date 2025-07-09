/*
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
#include "render.h"
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
#include "keybuf.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif
#include "statusline.h"
#include "devices.h"
#ifdef WITH_MIDIEMU
#include "midiemu.h"
#endif

#include "darkmode.h"

#define DM_DX_FULLSCREEN 1
#define DM_W_FULLSCREEN 2
#define DM_D3D_FULLSCREEN 16
#define DM_PICASSO96 32
#define DM_D3D 256

#define SM_WINDOW 0
#define SM_FULLSCREEN_DX 2
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

static int wasfs[2];
static const TCHAR *wasfsname[2] = { _T("FullScreenMode"), _T("FullScreenModeRTG") };

int vsync_modechangetimeout = 10;

int vsync_activeheight, vsync_totalheight;
float vsync_vblank, vsync_hblank;
bool beamracer_debug;
bool gfx_hdr;

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

static volatile int waitvblankthread_mode;
HANDLE waitvblankevent;
static frame_time_t wait_vblank_timestamp;
static MultiDisplay *wait_vblank_display;
static volatile bool vsync_active;
static bool scanlinecalibrating;

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

extern uae_s64 spincount;
bool calculated_scanline = true;

int target_get_display_scanline(int displayindex)
{
	if (!scanlinecalibrating && calculated_scanline) {
		static int lastline;
		float diff = (float)(read_processor_time() - wait_vblank_timestamp);
		if (diff < 0)
			return -1;
		int sl = (int)(diff * (vsync_activeheight + (vsync_totalheight - vsync_activeheight) / 10) * vsync_vblank / syncbase);
		if (sl < 0)
			sl = -1;
		return sl;
	} else {
		static uae_s64 lastrdtsc;
		static int lastvpos;
		if (spincount == 0 || currprefs.m68k_speed >= 0) {
			lastrdtsc = 0;
			lastvpos = target_get_display_scanline2(displayindex);
			return lastvpos;
		}
		uae_s64 v = read_processor_time_rdtsc();
		if (lastrdtsc > v)
			return lastvpos;
		lastvpos = target_get_display_scanline2(displayindex);
		lastrdtsc = read_processor_time_rdtsc() + spincount * 4;
		return lastvpos;
	}
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
		pQueryDisplayConfig = (QUERYDISPLAYCONFIG)GetProcAddress(userdll, "QueryDisplayConfig");
	if (!pGetDisplayConfigBufferSizes)
		pGetDisplayConfigBufferSizes = (GETDISPLAYCONFIGBUFFERSIZES)GetProcAddress(userdll, "GetDisplayConfigBufferSizes");
	if (!pDisplayConfigGetDeviceInfo)
		pDisplayConfigGetDeviceInfo = (DISPLAYCONFIGGETDEVICEINFO)GetProcAddress(userdll, "DisplayConfigGetDeviceInfo");
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

static unsigned int __stdcall waitvblankthread(void *dummy)
{
	waitvblankthread_mode = 2;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
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

	if (waitvblankthread_mode > 0)
		return;
	// It seems some Windows 7 drivers stall if D3DKMTWaitForVerticalBlankEvent()
	// and D3DKMTGetScanLine() is used simultaneously.
	if ((calculated_scanline || os_win8) && ap->gfx_vsyncmode && pD3DKMTWaitForVerticalBlankEvent && wait_vblank_display && wait_vblank_display->HasAdapterData) {
		waitvblankevent = CreateEvent(NULL, FALSE, FALSE, NULL);
		waitvblankthread_mode = 1;
		unsigned int th;
		_beginthreadex(NULL, 0, waitvblankthread, 0, 0, &th);
	} else {
		calculated_scanline = false;
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
		vsync_vblank = (float)mon->currentmode.freq;
	// GPU scaled mode?
	if (vsync_activeheight > mon->currentmode.current_height) {
		float m = (float)vsync_activeheight / mon->currentmode.current_height;
		vsync_hblank = vsync_hblank / m + 0.5f;
		vsync_activeheight = mon->currentmode.current_height;
	}

	wait_vblank_display = getdisplay(&currprefs, mon->monitor_id);
	if (!wait_vblank_display || !wait_vblank_display->HasAdapterData) {
		write_log(_T("Selected display mode does not have adapter data!\n"));
	}
	scanlinecalibrating = true;
	target_calibrate_spin();
	scanlinecalibrating = false;
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
	if(!(mon->currentmode.flags & (DM_DX_FULLSCREEN | DM_D3D_FULLSCREEN | DM_W_FULLSCREEN)))
		OffsetRect (dr, mon->amigawin_rect.left, mon->amigawin_rect.top);
	if (mon->currentmode.flags & DM_W_FULLSCREEN) {
		if (mon->scalepicasso && mon->screen_is_picasso)
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
			if (mon->currentmode.fullfill && (mon->currentmode.current_width > mon->currentmode.native_width || mon->currentmode.current_height > mon->currentmode.native_height))
				break;
			dx += (mon->currentmode.native_width - mon->currentmode.current_width) / 2;
			dy += (mon->currentmode.native_height - mon->currentmode.current_height) / 2;
			break;
		}
	}

	*dxp = dx;
	*dyp = dy;
	*mxp = 1.0f / mx;
	*myp = 1.0f / my;
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
	for (int i = 0; md->DisplayModes[i].inuse; i++) {
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

static void addmode (struct MultiDisplay *md, DEVMODE *dm, int rawmode)
{
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
	if (d != 32) {
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

	i = 0;
	while (md->DisplayModes[i].inuse) {
		if (md->DisplayModes[i].res.width == w && md->DisplayModes[i].res.height == h) {
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
	while (md->DisplayModes[i].inuse) {
		i++;
	}
	if (i >= MAX_PICASSO_MODES - 1) {
		return;
	}
	md->DisplayModes[i].inuse = true;
	md->DisplayModes[i].rawmode = rawmode;
	md->DisplayModes[i].lace = lace;
	md->DisplayModes[i].res.width = w;
	md->DisplayModes[i].res.height = h;
	md->DisplayModes[i].refresh[0] = freq;
	md->DisplayModes[i].refreshtype[0] = (lace ? REFRESH_RATE_LACE : 0) | (rawmode ? REFRESH_RATE_RAW : 0);
	md->DisplayModes[i].refresh[1] = 0;
	_stprintf (md->DisplayModes[i].name, _T("%dx%d%s"),
		md->DisplayModes[i].res.width, md->DisplayModes[i].res.height,
		lace ? _T("i") : _T(""));
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
	return 0;
}

static void sortmodes (struct MultiDisplay *md)
{
	int	i, idx = -1;
	int pw = -1, ph = -1;

	i = 0;
	while (md->DisplayModes[i].inuse) {
		i++;
	}
	qsort (md->DisplayModes, i, sizeof (struct PicassoResolution), resolution_compare);
	for (i = 0; md->DisplayModes[i].inuse; i++) {
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
	while (md->DisplayModes[i].inuse) {
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

static void adjustappbar(RECT *monitor, RECT *workrect)
{
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
			md->monitor = h;
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
			md->monitor = h;
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
	// XP does not support hybrid displays, don't load Direct3D
	// Windows 10+ seems to use same names by default
	if (os_win10)
		return;
	IDirect3D9 *d3d = Direct3DCreate9 (D3D_SDK_VERSION);
	if (!d3d)
		return;
	int max = d3d->GetAdapterCount ();
	struct MultiDisplay* md = Displays;
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
		md->DisplayModes = xcalloc(struct PicassoResolution, MAX_PICASSO_MODES);

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
				while (md->DisplayModes[idx2].inuse && !found) {
					struct PicassoResolution *pr = &md->DisplayModes[idx2];
					if (dm.dmPelsWidth == w && dm.dmPelsHeight == h && dm.dmBitsPerPel == b) {
						if (dm.dmDisplayFrequency > deskhz)
							deskhz = dm.dmDisplayFrequency;
					}
					if (pr->res.width == dm.dmPelsWidth && pr->res.height == dm.dmPelsHeight) {
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
		while (md->DisplayModes[i].inuse) {
			i++;
		}
		write_log (_T("%d display modes.\n"), i);
		md++;
	}
	write_log(_T("Desktop: W=%d H=%d B=%d HZ=%d. CXVS=%d CYVS=%d\n"), w, h, b, deskhz, wv, hv);

}

/* DirectX will fail with "Mode not supported" if we try to switch to a full
* screen mode that doesn't match one of the dimensions we got during enumeration.
* So try to find a best match for the given resolution in our list.  */
int WIN32GFX_AdjustScreenmode (struct MultiDisplay *md, int *pwidth, int *pheight)
{
	struct PicassoResolution *best;
	int pass, i = 0, index = 0;

	for (pass = 0; pass < 2; pass++) {
		struct PicassoResolution *dm;
		i = 0;
		index = 0;

		best = &md->DisplayModes[0];
		dm = &md->DisplayModes[1];

		while (dm->inuse)  {

			/* do we already have supported resolution? */
			if (dm->res.width == *pwidth && dm->res.height == *pheight)
				return i;

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
			dm++;
			i++;
		}
		if (best->res.width == *pwidth && best->res.height == *pheight) {
			break;
		}
	}
	*pwidth = best->res.width;
	*pheight = best->res.height;

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
	if (ad->picasso_on || monitor_off) {
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
	gfx_lock();
	if (mon->currentmode.flags & DM_D3D) {
		v = D3D_renderframe(monid, mode, immediate);
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
static volatile int strobo_active;
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
		if (strobo_active) {
		}

		while (strobo_active) {
			frame_time_t ct = read_processor_time();
			frame_time_t diff = strobo_time - ct;
			if (diff < -vsynctimebase / 2) {
				break;
			}
			if (diff <= 0) {
				if (strobo_active) {
					gfx_lock();
					D3D_showframe_special(0, 1);
					gfx_unlock();
				}
				break;
			}
			if (diff > vsynctimebase / 4) {
				break;
			}
		}
	}
	strobo_active = 0;
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
		if (ap->gfx_strobo && currprefs.gfx_variable_sync) {
			float vblank = vblank_hz;
			int ratio = currprefs.lightboost_strobo_ratio;
			int ms = (int)(1000 / vblank);
			int waitms = ms * ratio / 100 - 1;
			strobo_active = -1;
			strobo_time = read_processor_time() + vsynctimebase * ratio / 100;
			timeSetEvent(waitms, 0, blackinsertion_cb, NULL, TIME_ONESHOT | TIME_CALLBACK_FUNCTION);
		}
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
		if (monid == 0) {
			strobo_active2 = true;
			if (strobo_active < 0) {
				D3D_showframe_special(0, 2);
			}
		}
	}
	gfx_unlock();
	mon->render_ok = false;
}

int lockscr(struct vidbuffer *vb, bool fullupdate, bool skip)
{
	struct AmigaMonitor *mon = &AMonitors[vb->monitor_id];
	int ret = 0;

	if (!isscreen(mon)) {
		return ret;
	}
	gfx_lock();
	if (vb->vram_buffer) {
		vb->bufmem = D3D_locktexture(vb->monitor_id, &vb->rowbytes, &vb->width_allocated, &vb->height_allocated, skip ? -1 : (fullupdate ? 1 : 0));
		if (vb->bufmem) {
			ret = 1;
		}
	} else {
		ret = 1;
	}
	if (ret) {
		vb->locked = true;
	}
	gfx_unlock();
	return ret;
}

void unlockscr(struct vidbuffer *vb, int y_start, int y_end)
{
	struct AmigaMonitor *mon = &AMonitors[vb->monitor_id];
	gfx_lock();
	vb->locked = false;
	if (vb->vram_buffer) {
		vb->bufmem = NULL;
		D3D_unlocktexture(vb->monitor_id, y_start, y_end);
	}
	gfx_unlock();
}

void flush_clear_screen (struct vidbuffer *vb)
{
	if (!vb)
		return;
	if (lockscr(vb, true, false)) {
		int y;
		for (y = 0; y < vb->height_allocated; y++) {
			memset(vb->bufmem + y * vb->rowbytes, 0, vb->width_allocated * vb->pixbytes);
		}
		unlockscr(vb, -1, -1);
	}
}

float filterrectmult(int v1, float v2, int dmode)
{
	float v = v1 / v2;
	int vv = (int)(v + 0.5f);
	if (v > 1.5f && vv * v2 <= v1 && vv * (v2 + vv - 1) >= v1) {
		return (float)vv;
	}
	if (!dmode) {
		return v;
	}
	if (v > 0.2f && v < 0.3f) {
		return 0.25f;
	}
	if (v > 0.4f && v < 0.6f) {
		return 0.5f;
	}
	return (float)(int)(v + 0.5f);
}

void getrtgfilterdata(int monid, struct displayscale *ds)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct picasso96_state_struct *state = &picasso96_state[monid];

	picasso_offset_x = 0;
	picasso_offset_y = 0;
	picasso_offset_mx = 1.0;
	picasso_offset_my = 1.0;

	ds->mode = 0;
	ds->outwidth = mon->currentmode.native_width;
	ds->outheight = mon->currentmode.native_height;

	if (!ad->picasso_on)
		return;

	if (currprefs.gf[GF_RTG].gfx_filter_horiz_zoom_mult > 0) {
		picasso_offset_mx *= currprefs.gf[GF_RTG].gfx_filter_horiz_zoom_mult;
	}
	if (currprefs.gf[GF_RTG].gfx_filter_vert_zoom_mult > 0) {
		picasso_offset_my *= currprefs.gf[GF_RTG].gfx_filter_vert_zoom_mult;
	}

	if (!mon->scalepicasso)
		return;

	int srcratio, dstratio;
	int srcwidth, srcheight;
	int outwidth, outheight;

	srcwidth = state->Width;
	srcheight = state->Height;
	if (!srcwidth || !srcheight)
		return;

	float mx = (float)mon->currentmode.native_width / srcwidth;
	float my = (float)mon->currentmode.native_height / srcheight;

	if (mon->scalepicasso == RTG_MODE_INTEGER_SCALE) {
		int divx = mon->currentmode.native_width / srcwidth;
		int divy = mon->currentmode.native_height / srcheight;
		float mul = (float)(!divx || !divy ? 1 : (divx > divy ? divy : divx));
		if (!divx || !divy) {
			if ((float)mon->currentmode.native_width / srcwidth <= 0.95f || ((float)mon->currentmode.native_height / srcheight <= 0.95f)) {
				mul = 0.5f;
			}
			if ((float)mon->currentmode.native_width / srcwidth <= 0.45f || ((float)mon->currentmode.native_height / srcheight <= 0.45f)) {
				mul = 0.25f;
			}
		}
		ds->outwidth = (int)(mon->currentmode.native_width / mul);
		ds->outheight = (int)(mon->currentmode.native_height / mul);
		int xx = (int)((mon->currentmode.native_width / mul - srcwidth) / 2);
		int yy = (int)((mon->currentmode.native_height / mul - srcheight) / 2);
		picasso_offset_x = -xx;
		picasso_offset_y = -yy;
		mx = mul;
		my = mul;
		outwidth = srcwidth;
		outheight = srcheight;
		ds->mode = 1;
	} else if (mon->scalepicasso == RTG_MODE_CENTER) {
		int xx = (mon->currentmode.native_width - srcwidth) / 2;
		int yy = (mon->currentmode.native_height - srcheight) / 2;
		picasso_offset_x = -xx;
		picasso_offset_y = -yy;
		ds->outwidth = mon->currentmode.native_width;
		ds->outheight = mon->currentmode.native_height;
		outwidth = ds->outwidth;
		outheight = ds->outheight;
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
			ds->outwidth = srcwidth;
			ds->outheight = srcheight;
		} else if (srcratio > dstratio) {
			int yy = srcheight * srcratio / dstratio;
			ds->outwidth = srcwidth;
			ds->outheight = yy;
			picasso_offset_y = (state->Height - yy) / 2;
		} else {
			int xx = srcwidth * dstratio / srcratio;
			ds->outwidth = xx;
			ds->outheight = srcheight;
			picasso_offset_x = (state->Width - xx) / 2;
		}
		outwidth = ds->outwidth;
		outheight = ds->outheight;
	}

	ds->xoffset += picasso_offset_x;
	ds->yoffset += picasso_offset_y;

	picasso_offset_x /= state->HLineDBL;
	picasso_offset_y /= state->VLineDBL;

	picasso_offset_mx = (float)(srcwidth * mx * state->HLineDBL) / outwidth;
	picasso_offset_my = (float)(srcheight * my * state->VLineDBL) / outheight;
}

static uae_u8 *gfx_lock_picasso2(int monid, bool fullupdate)
{
	struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
	struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
	struct vidbuffer *vb = &vidinfo->drawbuffer;
	if (vb->locked) {
		unlockscr(vb, -1, -1);
		vb->locked = false;
	}
	uae_u8 *p = D3D_locktexture(monid, &pvidinfo->rowbytes, &pvidinfo->maxwidth, &pvidinfo->maxheight, fullupdate);
	return p;
}
uae_u8 *gfx_lock_picasso(int monid, bool fullupdate)
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
	}
	return p;
}

void gfx_unlock_picasso(int monid, bool dorender)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	if (!mon->rtg_locked) {
		gfx_lock();
	}
	mon->rtg_locked = false;
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
		AVIOutput_Restart(true);
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
	gfx_hdr = false;
}

static void updatemodes(struct AmigaMonitor *mon)
{
	DWORD flags = 0;

	mon->currentmode.fullfill = 0;
	if (isfullscreen () > 0)
		flags |= DM_DX_FULLSCREEN;
	else if (isfullscreen () < 0)
		flags |= DM_W_FULLSCREEN;
	flags |= DM_D3D;
	if (flags & DM_DX_FULLSCREEN) {
		flags &= ~DM_DX_FULLSCREEN;
		flags |= DM_D3D_FULLSCREEN;
	}
	mon->currentmode.flags = flags;
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
		float mx = 1.0;
		float my = 1.0;
		if (currprefs.gf[GF_RTG].gfx_filter_horiz_zoom_mult > 0) {
			mx *= currprefs.gf[GF_RTG].gfx_filter_horiz_zoom_mult;
		}
		if (currprefs.gf[GF_RTG].gfx_filter_vert_zoom_mult > 0) {
			my *= currprefs.gf[GF_RTG].gfx_filter_vert_zoom_mult;
		}
		mon->currentmode.current_width = (int)(state->Width * currprefs.rtg_horiz_zoom_mult * mx);
		mon->currentmode.current_height = (int)(state->Height * currprefs.rtg_vert_zoom_mult * my);
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

	mon->currentmode.amiga_width = mon->currentmode.current_width;
	mon->currentmode.amiga_height = mon->currentmode.current_height;

	mon->scalepicasso = 0;
	if (mon->screen_is_picasso) {
		bool diff = state->Width != mon->currentmode.native_width || state->Height != mon->currentmode.native_height;
		if (isfullscreen () < 0) {
			if ((currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_CENTER || currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_SCALE || currprefs.win32_rtgallowscaling) && diff) {
				mon->scalepicasso = RTG_MODE_SCALE;
			}
			if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_INTEGER_SCALE && diff) {
				mon->scalepicasso = RTG_MODE_INTEGER_SCALE;
			}
			if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_CENTER && diff) {
				mon->scalepicasso = currprefs.gf[GF_RTG].gfx_filter_autoscale;
			}
			if (!mon->scalepicasso && currprefs.win32_rtgscaleaspectratio) {
				mon->scalepicasso = -1;
			}
		} else if (isfullscreen () > 0) {
			if (mon->currentmode.native_width > state->Width && mon->currentmode.native_height > state->Height) {
				if (currprefs.gf[GF_RTG].gfx_filter_autoscale)
					mon->scalepicasso = RTG_MODE_SCALE;
				if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_INTEGER_SCALE) {
					mon->scalepicasso = RTG_MODE_INTEGER_SCALE;
				}
			}
			if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_CENTER)
				mon->scalepicasso = currprefs.gf[GF_RTG].gfx_filter_autoscale;
			if (!mon->scalepicasso && currprefs.win32_rtgscaleaspectratio)
				mon->scalepicasso = -1;
		} else if (isfullscreen () == 0) {
			if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_INTEGER_SCALE) {
				mon->scalepicasso = RTG_MODE_INTEGER_SCALE;
				mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
				mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
			} else if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_CENTER) {
				if (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width < state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height < state->Height) {
					if (!currprefs.win32_rtgallowscaling) {
						;
					} else if (currprefs.win32_rtgscaleaspectratio) {
						mon->scalepicasso = -1;
						mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
						mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
					}
				} else {
					mon->scalepicasso = RTG_MODE_CENTER;
					mon->currentmode.current_width = currprefs.gfx_monitor[mon->monitor_id].gfx_size.width;
					mon->currentmode.current_height = currprefs.gfx_monitor[mon->monitor_id].gfx_size.height;
				}
			} else if (currprefs.gf[GF_RTG].gfx_filter_autoscale == RTG_MODE_SCALE) {
				if (currprefs.gfx_monitor[mon->monitor_id].gfx_size.width > state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height > state->Height)
					mon->scalepicasso = RTG_MODE_SCALE;
				if ((currprefs.gfx_monitor[mon->monitor_id].gfx_size.width != state->Width || currprefs.gfx_monitor[mon->monitor_id].gfx_size.height != state->Height) && currprefs.win32_rtgallowscaling) {
					mon->scalepicasso = RTG_MODE_SCALE;
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
					mon->scalepicasso = RTG_MODE_SCALE;
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
			return 0;
		}
	} while (ret < 0);

	if (!ret) {
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
		gui_fps(0, 0, 0, 0, 0);
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
			inputdevice_unacquire(input);
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
	render_screen(mon->monitor_id, 1, true);
}

static int getstatuswindowheight(int monid, HWND hwnd)
{
	if (monid > 0)
		return 0;
	int def = GetSystemMetrics (SM_CYMENU) + 3;
	WINDOWINFO wi;
	HWND h = CreateWindowEx (
		0, STATUSCLASSNAME, (LPCTSTR) NULL, SBARS_TOOLTIPS | WS_CHILD,
		0, 0, 0, 0, hwnd ? hwnd : hHiddenWnd, (HMENU) 1, hInst, NULL);
	if (!h)
		return def;
	wi.cbSize = sizeof wi;
	if (GetWindowInfo(h, &wi)) {
		def = wi.rcWindow.bottom - wi.rcWindow.top;
	}
	DestroyWindow(h);
	return def;
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

void WIN32GFX_DisplayChangeRequested(int mode)
{
	display_change_requested = mode;
}

int check_prefs_changed_gfx(void)
{
	int c = 0;
	bool monitors[MAX_AMIGAMONITORS];

	if (!config_changed && !display_change_requested)
		return 0;

	c |= config_changed_flags;
	config_changed_flags = 0;

	c |= currprefs.win32_statusbar != changed_prefs.win32_statusbar ? 512 : 0;

	for (int i = 0; i < MAX_AMIGADISPLAYS; i++) {
		monitors[i] = false;
		int c2 = 0;
		c2 |= currprefs.gfx_monitor[i].gfx_size_fs.width != changed_prefs.gfx_monitor[i].gfx_size_fs.width ? 16 : 0;
		c2 |= currprefs.gfx_monitor[i].gfx_size_fs.height != changed_prefs.gfx_monitor[i].gfx_size_fs.height ? 16 : 0;
		c2 |= ((currprefs.gfx_monitor[i].gfx_size_win.width + 7) & ~7) != ((changed_prefs.gfx_monitor[i].gfx_size_win.width + 7) & ~7) ? 16 : 0;
		c2 |= currprefs.gfx_monitor[i].gfx_size_win.height != changed_prefs.gfx_monitor[i].gfx_size_win.height ? 16 : 0;
		if (c2) {
			if (i > 0) {
				for (int j = 0; j < MAX_AMIGAMONITORS; j++) {
					struct rtgboardconfig *rbc = &changed_prefs.rtgboards[j];
					if (rbc->monitor_id == i) {
						c |= c2;
						monitors[i] = true;
					}
				}
				if (!monitors[i]) {
					currprefs.gfx_monitor[i].gfx_size_fs.width = changed_prefs.gfx_monitor[i].gfx_size_fs.width;
					currprefs.gfx_monitor[i].gfx_size_fs.height = changed_prefs.gfx_monitor[i].gfx_size_fs.height;
					currprefs.gfx_monitor[i].gfx_size_win.width = changed_prefs.gfx_monitor[i].gfx_size_win.width;
					currprefs.gfx_monitor[i].gfx_size_win.height = changed_prefs.gfx_monitor[i].gfx_size_win.height;
				}
			} else {
				c |= c2;
				monitors[i] = true;
			}
		}
		if (WIN32GFX_IsPicassoScreen(&AMonitors[i])) {
			struct gfx_filterdata *gfc = &changed_prefs.gf[1];
			if (gfc->changed) {
				gfc->changed = false;
				c |= 16;
			}
		} else {
			struct gfx_filterdata *gfc1 = &changed_prefs.gf[0];
			struct gfx_filterdata *gfc2 = &changed_prefs.gf[2];
			if (gfc1->changed || gfc2->changed) {
				gfc1->changed = false;
				gfc2->changed = false;
				c |= 16;
			}
		}
	}
	if (currprefs.gf[2].enable != changed_prefs.gf[2].enable) {
		currprefs.gf[2].enable = changed_prefs.gf[2].enable;
		c |= 512;
	}

	monitors[0] = true;

#if 0
	c |= currprefs.gfx_size_win.x != changed_prefs.gfx_size_win.x ? 16 : 0;
	c |= currprefs.gfx_size_win.y != changed_prefs.gfx_size_win.y ? 16 : 0;
#endif
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

	for (int j = 0; j < MAX_FILTERDATA; j++) {
		struct gfx_filterdata *gf = &currprefs.gf[j];
		struct gfx_filterdata *gfc = &changed_prefs.gf[j];

		c |= gf->gfx_filter != gfc->gfx_filter ? (2 | 8) : 0;
		c |= gf->gfx_filter != gfc->gfx_filter ? (2 | 8) : 0;

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
		c |= gf->gfx_filter_rotation != gfc->gfx_filter_rotation ? (1) : 0;
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
	c |= currprefs.genlock_offset_x != changed_prefs.genlock_offset_x ? (1 | 256) : 0;
	c |= currprefs.genlock_offset_y != changed_prefs.genlock_offset_y ? (1 | 256) : 0;
	c |= _tcsicmp(currprefs.genlock_image_file, changed_prefs.genlock_image_file) ? (2 | 8) : 0;
	c |= _tcsicmp(currprefs.genlock_video_file, changed_prefs.genlock_video_file) ? (2 | 8) : 0;

	c |= currprefs.gfx_lores_mode != changed_prefs.gfx_lores_mode ? (2 | 8) : 0;
	c |= currprefs.gfx_overscanmode != changed_prefs.gfx_overscanmode ? (2 | 8) : 0;
	c |= currprefs.gfx_scandoubler != changed_prefs.gfx_scandoubler ? (2 | 8) : 0;
	c |= currprefs.gfx_threebitcolors != changed_prefs.gfx_threebitcolors ? (256) : 0;
	c |= currprefs.gfx_grayscale != changed_prefs.gfx_grayscale ? (512) : 0;
	c |= currprefs.gfx_monitorblankdelay != changed_prefs.gfx_monitorblankdelay ? (512) : 0;

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
			} else if (display_change_requested == 4) {
				c = 32;
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

		for (int j = 0; j < MAX_FILTERDATA; j++) {
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
		currprefs.genlock_offset_x = changed_prefs.genlock_offset_x;
		currprefs.genlock_offset_y = changed_prefs.genlock_offset_y;
		_tcscpy(currprefs.genlock_image_file, changed_prefs.genlock_image_file);
		_tcscpy(currprefs.genlock_video_file, changed_prefs.genlock_video_file);

		currprefs.gfx_lores_mode = changed_prefs.gfx_lores_mode;
		currprefs.gfx_overscanmode = changed_prefs.gfx_overscanmode;
		currprefs.gfx_scandoubler = changed_prefs.gfx_scandoubler;
		currprefs.gfx_threebitcolors = changed_prefs.gfx_threebitcolors;
		currprefs.gfx_grayscale = changed_prefs.gfx_grayscale;
		currprefs.gfx_monitorblankdelay = changed_prefs.gfx_monitorblankdelay;

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
#if 0
					S2X_reset(mon->monitor_id);
#endif
				}
			}
			if (c & 1024) {
				target_graphics_buffer_update(mon->monitor_id, true);
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
		init_hz();
	}
	if (currprefs.chipset_refreshrate != changed_prefs.chipset_refreshrate) {
		currprefs.chipset_refreshrate = changed_prefs.chipset_refreshrate;
		init_hz();
		return 1;
	}

	if (
		currprefs.gf[0].gfx_filter_autoscale != changed_prefs.gf[0].gfx_filter_autoscale ||
		currprefs.gf[2].gfx_filter_autoscale != changed_prefs.gf[2].gfx_filter_autoscale ||
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
		currprefs.gf[2].gfx_filter_autoscale = changed_prefs.gf[2].gfx_filter_autoscale;

		get_custom_limits (NULL, NULL, NULL, NULL, NULL, NULL, NULL);
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
		currprefs.win32_active_input != changed_prefs.win32_active_input ||
		currprefs.win32_inactive_nosound != changed_prefs.win32_inactive_nosound ||
		currprefs.win32_inactive_pause != changed_prefs.win32_inactive_pause ||
		currprefs.win32_inactive_input != changed_prefs.win32_inactive_input ||
		currprefs.win32_iconified_nosound != changed_prefs.win32_iconified_nosound ||
		currprefs.win32_iconified_pause != changed_prefs.win32_iconified_pause ||
		currprefs.win32_iconified_input != changed_prefs.win32_iconified_input ||
		currprefs.win32_capture_always != changed_prefs.win32_capture_always ||
		currprefs.win32_ctrl_F11_is_quit != changed_prefs.win32_ctrl_F11_is_quit ||
		currprefs.win32_shutdown_notification != changed_prefs.win32_shutdown_notification ||
		currprefs.win32_warn_exit != changed_prefs.win32_warn_exit ||
		currprefs.win32_gui_control != changed_prefs.win32_gui_control ||
		currprefs.turbo_boot != changed_prefs.turbo_boot ||
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
		currprefs.win32_active_input = changed_prefs.win32_active_input;
		currprefs.win32_active_nocapture_pause = changed_prefs.win32_active_nocapture_pause;
		currprefs.win32_inactive_nosound = changed_prefs.win32_inactive_nosound;
		currprefs.win32_inactive_pause = changed_prefs.win32_inactive_pause;
		currprefs.win32_inactive_input = changed_prefs.win32_inactive_input;
		currprefs.win32_iconified_nosound = changed_prefs.win32_iconified_nosound;
		currprefs.win32_iconified_pause = changed_prefs.win32_iconified_pause;
		currprefs.win32_iconified_input = changed_prefs.win32_iconified_input;
		currprefs.win32_capture_always = changed_prefs.win32_capture_always;
		currprefs.win32_ctrl_F11_is_quit = changed_prefs.win32_ctrl_F11_is_quit;
		currprefs.win32_shutdown_notification = changed_prefs.win32_shutdown_notification;
		currprefs.win32_warn_exit = changed_prefs.win32_warn_exit;
		currprefs.win32_gui_control = changed_prefs.win32_gui_control;
		currprefs.turbo_boot = changed_prefs.turbo_boot;
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
		currprefs.win32_midirouter != changed_prefs.win32_midirouter)
	{
		currprefs.win32_midiindev = changed_prefs.win32_midiindev;
		currprefs.win32_midioutdev = changed_prefs.win32_midioutdev;
		currprefs.win32_midirouter = changed_prefs.win32_midirouter;

#ifdef SERIAL_PORT
		serial_exit();
		serial_init();
		Midi_Reopen();
#endif
#ifdef WITH_MIDIEMU
		midi_emu_reopen();
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
	D3D_getpixelformat(&red_bits, &green_bits, &blue_bits, &red_shift, &green_shift, &blue_shift, &alpha_bits, &alpha_shift, &alpha);

	red_bits = green_bits = blue_bits = 8;
	red_shift = 16; green_shift = 8; blue_shift = 0;

	alloc_colors64k(monid, red_bits, green_bits, blue_bits, red_shift,green_shift, blue_shift, alpha_bits, alpha_shift, alpha, 0);
	notice_new_xcolors ();
#ifdef AVIOUTPUT
	AVIOutput_RGBinfo (red_bits, green_bits, blue_bits, alpha_bits, red_shift, green_shift, blue_shift, alpha_shift);
#endif
	Screenshot_RGBinfo (red_bits, green_bits, blue_bits, alpha_bits, red_shift, green_shift, blue_shift, alpha_shift);
}

#ifdef PICASSO96

int picasso_palette(struct MyCLUTEntry *CLUT, uae_u32 *clut)
{
	int changed = 0;

	for (int i = 0; i < 256 * 2; i++) {
		int r = CLUT[i].Red;
		int g = CLUT[i].Green;
		int b = CLUT[i].Blue;
		uae_u32 v = (doMask256 (r, red_bits, red_shift)
			| doMask256 (g, green_bits, green_shift)
			| doMask256 (b, blue_bits, blue_shift))
			| doMask256 ((1 << alpha_bits) - 1, alpha_bits, alpha_shift);
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
				for (i = 0; md->DisplayModes[i].inuse && !found; i++) {
					struct PicassoResolution *r = &md->DisplayModes[i];
					if (r->res.width == w && (r->res.height == newh + cnt || r->res.height == newh - cnt)) {
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
			if (state->Width < wc->current_width && state->Height < wc->current_height) {
				if (currprefs.gf[GF_RTG].gfx_filter_autoscale == 1 || (currprefs.gf[GF_RTG].gfx_filter_autoscale == 2 && currprefs.win32_rtgallowscaling))
					return 0;
			}
			if (state->Width != wc->current_width ||
				state->Height != wc->current_height)
				return 1;
			if (state->Width == wc->current_width &&
				state->Height == wc->current_height) {
				return 0;
			}
			return 1;
		} else {
			if (mon->currentmode.current_width != wc->current_width ||
				mon->currentmode.current_height != wc->current_height)
				return -1;
		}
	} else if (isfullscreen () == 0) {
		/* windowed to windowed */
		return -2;
	} else {
		/* fullwindow to fullwindow */
		if (mon->screen_is_picasso) {
			if (currprefs.gf[GF_RTG].gfx_filter_autoscale && ((wc->native_width > state->Width && wc->native_height >= state->Height) || (wc->native_height > state->Height && wc->native_width >= state->Width)))
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

	if (mon->screen_is_picasso == on) {
		return;
	}
	mon->screen_is_picasso = on;

	rp_rtg_switch ();
	memcpy (&wc, &mon->currentmode, sizeof (wc));

	newmode = &currprefs.gfx_apmode[on ? 1 : 0];
	oldmode = &currprefs.gfx_apmode[on ? 0 : 1];

	newf = &currprefs.gf[on ? 1 : 0];
	oldf = &currprefs.gf[on ? 0 : 1];

	updatemodes(mon);
	update_gfxparams(mon);

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
	bool differentmonitor = getdisplay(&currprefs, newmode->gfx_display) != getdisplay(&currprefs, oldmode->gfx_display);
	// if screen parameter changes, need to reopen window
	if (newmode->gfx_fullscreen != oldmode->gfx_fullscreen ||
		(newmode->gfx_fullscreen && (
			newmode->gfx_backbuffers != oldmode->gfx_backbuffers ||
			differentmonitor ||
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
	rp_set_hwnd_delayed();
#endif
}

static void updatepicasso96(struct AmigaMonitor *mon)
{
#ifdef PICASSO96
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[mon->monitor_id];
	vidinfo->rowbytes = 0;
	vidinfo->pixbytes = 32 / 8;
	vidinfo->rgbformat = 0;
	vidinfo->extra_mem = 1;
	vidinfo->height = mon->currentmode.current_height;
	vidinfo->width = mon->currentmode.current_width;
	vidinfo->offset = 0;
	vidinfo->splitypos = -1;
#endif
}

void gfx_set_picasso_modeinfo(int monid, RGBFTYPE rgbfmt)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct picasso96_state_struct *state = &picasso96_state[mon->monitor_id];
	int need;
	if (!mon->screen_is_picasso)
		return;
	gfx_set_picasso_colors(monid, rgbfmt);
	need = modeswitchneeded(mon, &mon->currentmode);
	update_gfxparams(mon);
	updatemodes(mon);
	if (need > 0) {
		open_screen(mon);
	} else if (need < 0) {
		open_windows(mon, true, true);
	}
	state->ModeChanged = false;
#ifdef RETROPLATFORM
	rp_set_hwnd_delayed();
#endif
	target_graphics_buffer_update(monid, false);
}
#endif

void gfx_set_picasso_colors(int monid, RGBFTYPE rgbfmt)
{
	alloc_colors_picasso(red_bits, green_bits, blue_bits, red_shift, green_shift, blue_shift, rgbfmt, p96_rgbx16);
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
	InitializeDarkMode();
	systray (hHiddenWnd, TRUE);
	systray (hHiddenWnd, FALSE);
	d3d_select(&currprefs);
	if (open_windows(&AMonitors[0], mousecapture, false)) {
		if (currprefs.monitoremu_mon > 0 && currprefs.monitoremu) {
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
	HMODULE hm = GetModuleHandle(_T("dwmapi.dll"));
	if (hm) {
		DWMENABLEMMCSS pDwmEnableMMCSS;
		pDwmEnableMMCSS = (DWMENABLEMMCSS)GetProcAddress(hm, "DwmEnableMMCSS");
		if (pDwmEnableMMCSS)
			pDwmEnableMMCSS(state);
	}
}

void close_windows(struct AmigaMonitor *mon)
{
	struct vidbuf_description *avidinfo = &adisplays[mon->monitor_id].gfxvidinfo;

	setDwmEnableMMCSS (FALSE);
	reset_sound ();
#if 0
	S2X_free(mon->monitor_id);
#endif
	freevidbuffer(mon->monitor_id, &avidinfo->drawbuffer);
	freevidbuffer(mon->monitor_id, &avidinfo->tempbuffer);
	close_hwnds(mon);
}

static void createstatuswindow(struct AmigaMonitor *mon)
{
	RECT rc;
	HLOCAL hloc;
	LPINT lpParts;
	int drive_width, hd_width, cd_width, power_width, caps_width;
	int fps_width, lines_width, idle_width, snd_width, joy_width, net_width;
	int joys = currprefs.win32_statusbar > 1 ? 2 : 0;
	int num_parts = LED_MAX + joys + 1;
	float scale = 1.0;
	WINDOWINFO wi;
	int extra;

	if (mon->hStatusWnd) {
		ShowWindow(mon->hStatusWnd, SW_HIDE);
		DestroyWindow(mon->hStatusWnd);
		mon->hStatusWnd = NULL;
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
	if (g_darkModeSupported) {
		BOOL value = g_darkModeEnabled;
		DwmSetWindowAttribute(mon->hStatusWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
		if (g_darkModeEnabled) {
			SubClassStatusBar(mon->hStatusWnd);
		}
	}
	wi.cbSize = sizeof wi;
	GetWindowInfo(mon->hMainWnd, &wi);
	extra = wi.rcClient.top - wi.rcWindow.top;
	scale = getdpiforwindow(mon->hStatusWnd) / 96.0f;
	drive_width = (int)(24 * scale);
	hd_width = (int)(24 * scale);
	cd_width = (int)(24 * scale);
	power_width = (int)(42 * scale);
	caps_width = (int)(42 * scale);
	lines_width = (int)(42 * scale);
	fps_width = (int)(64 * scale);
	idle_width = (int)(64 * scale);
	net_width = (int)(24 * scale);
	if (is_ppc_cpu(&currprefs))
		idle_width += (int)(68 * scale);
	if (is_x86_cpu(&currprefs))
		idle_width += (int)(68 * scale);
	snd_width = (int)(72 * scale);
	joy_width = (int)(24 * scale);
	GetClientRect(mon->hMainWnd, &rc);
	/* Allocate an array for holding the right edge coordinates. */
	hloc = LocalAlloc (LHND, sizeof (int) * (num_parts + 1));
	if (hloc) {
		lpParts = (LPINT)LocalLock(hloc);
		if (lpParts) {
			int i = 0, i1;
			// left side, msg area
			lpParts[i] = rc.left;
			i++;
			window_led_msg_start = i;
			/* Calculate the right edge coordinate for each part, and copy the coords to the array.  */
			int startx = rc.right - (drive_width * 4) - power_width - caps_width - idle_width - fps_width - lines_width - cd_width - hd_width - snd_width - net_width - joys * joy_width - extra;
			for (int j = 0; j < joys; j++) {
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
			// lines
			lpParts[i] = lpParts[i - 1] + fps_width;
			i++;
			// power
			lpParts[i] = lpParts[i - 1] + lines_width;
			i++;
			// caps
			lpParts[i] = lpParts[i - 1] + power_width;
			i++;
			i1 = i;
			// hd
			lpParts[i] = lpParts[i - 1] + caps_width;
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
		for (i = 0; md->DisplayModes[i].inuse; i++) {
			struct PicassoResolution *pr = &md->DisplayModes[i];
			if (pr->res.width == mon->currentmode.native_width && pr->res.height == mon->currentmode.native_height)
				break;
		}
		if (md->DisplayModes[i].inuse) {
			if (!nextbest)
				break;
			while (md->DisplayModes[i].res.width == mon->currentmode.native_width && md->DisplayModes[i].res.height == mon->currentmode.native_height)
				i++;
		} else {
			i = 0;
		}
		// first iterate only modes that have similar aspect ratio
		startidx = i;
		for (; md->DisplayModes[i].inuse; i++) {
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
		for (; md->DisplayModes[i].inuse; i++) {
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
	} else {
		return D3D_getrefreshrate(0);
	}
}

static void movecursor (int x, int y)
{
	write_log (_T("SetCursorPos %dx%d\n"), x, y);
	SetCursorPos (x, y);
}

static void getextramonitorpos(struct AmigaMonitor *mon, RECT *r)
{
	TCHAR buf[100];
	RECT r1, r2;
	int x, y;
	bool got = true;

	_stprintf(buf, _T("MainPosX_%d"), mon->monitor_id);
	if (!regqueryint(NULL, buf, &x)) {
		got = false;
	}
	_stprintf(buf, _T("MainPosY_%d"), mon->monitor_id);
	if (!regqueryint(NULL, buf, &y)) {
		got = false;
	}
	if (got) {
		POINT pt;
		pt.x = x;
		pt.y = y;
		if (!MonitorFromPoint(pt, MONITOR_DEFAULTTONULL)) {
			got = false;
		}
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
	if (rightmon < 0 && !got)
		return;
	hwnd = AMonitors[rightmon].hMainWnd;
	GetWindowRect(hwnd, &r1);
	r2 = r1;

	getextendedframebounds(hwnd, &r2);
	int width = r->right - r->left;
	int height = r->bottom - r->top;

	if (got) {
		r->left = x;
		r->top = y;
	} else {
		r->left = r1.right - ((r2.left - r1.left) + (r1.right - r2.right));
		r->top = r1.top;
	}
	r->bottom = r->top + height;
	r->right = r->left + width;
}

static int create_windows(struct AmigaMonitor *mon)
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

		int sbheight = currprefs.win32_statusbar && !currprefs.win32_borderless ? getstatuswindowheight(mon->monitor_id, mon->hAmigaWnd) : 0;
		int dpi = getdpiforwindow(mon->hAmigaWnd);

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

		if (w != nw || h != nh || x != nx || y != ny || sbheight != mon->window_extra_height_bar || dpi != mon->dpi) {
			w = nw;
			h = nh;
			x = nx;
			y = ny;
			mon->in_sizemove++;
			if (mon->hMainWnd && !fsw && !dxfs && !d3dfs && !rp_isactive()) {
				if (dpi != mon->dpi) {
					mon->window_extra_height -= mon->window_extra_height_bar;
					mon->window_extra_height += sbheight;
				} else {
					mon->window_extra_height += (sbheight - mon->window_extra_height_bar);
				}

				GetWindowRect(mon->hMainWnd, &r);
#if 0
				RECT r2;
				GetClientRect(mon->hMainWnd, &r2);
				if (pAdjustWindowRectExForDpi) {
					HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
					pAdjustWindowRectExForDpi(&r, borderless ? WS_POPUP : style, FALSE, exstyle, getdpiformonitor(mon));
				} else {
					AdjustWindowRectEx(&r, borderless ? WS_POPUP : style, FALSE, exstyle);
				}
#endif
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
			mon->dpi = dpi;
		} else {
			w = nw;
			h = nh;
			x = nx;
			y = ny;
		}
		createstatuswindow(mon);
		createstatusline(mon->hAmigaWnd, mon->monitor_id);
		updatewinrect(mon, false);
		GetWindowRect (mon->hMainWnd, &mon->mainwin_rect);
		if (d3dfs || dxfs)
			movecursor (x + w / 2, y + h / 2);
		write_log (_T("window already open (%dx%d %dx%d)\n"),
			mon->amigawin_rect.left, mon->amigawin_rect.top, mon->amigawin_rect.right - mon->amigawin_rect.left, mon->amigawin_rect.bottom - mon->amigawin_rect.top);
		updatemouseclip(mon);
		rp_screenmode_changed ();
		mon->window_extra_height_bar = sbheight;
		return 1;
	}

	rawinput_release();
	gfx_lock();
	D3D_free(mon->monitor_id, false);
	gfx_unlock();

	if (fsw && !borderless)
		borderless = 1;
	window_led_drives = 0;
	window_led_drives_end = 0;
	mon->hMainWnd = NULL;
	x = 0; y = 0;

	int sbheight = currprefs.win32_statusbar && !currprefs.win32_borderless ? getstatuswindowheight(mon->monitor_id, NULL) : 0;

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
			if (pAdjustWindowRectExForDpi) {
				HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
				pAdjustWindowRectExForDpi(&rc, borderless ? WS_POPUP : style, FALSE, exstyle, getdpiformonitor(mon));
			} else {
				AdjustWindowRectEx(&rc, borderless ? WS_POPUP : style, FALSE, exstyle);
			}
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
		if (currprefs.gfx_api < 2) {
			flags |= currprefs.win32_main_alwaysontop ? WS_EX_TOPMOST : 0;
		}

		if (!borderless) {
			RECT rc2;
			int sbheight2 = -1;
			for (;;) {
				mon->hMainWnd = CreateWindowEx(WS_EX_ACCEPTFILES | exstyle | flags,
					_T("PCsuxRox"), _T("WinUAE"),
					style,
					rc.left, rc.top,
					rc.right - rc.left, rc.bottom - rc.top,
					NULL, NULL, hInst, NULL);
				if (!mon->hMainWnd) {
					write_log(_T("main window creation failed\n"));
					return 0;
				}
				if (sbheight && sbheight2 < 0 && !fsw) {
					// recheck, system could have multiple monitors with different DPI
					sbheight2 = getstatuswindowheight(mon->monitor_id, mon->hMainWnd);
					if (sbheight2 != sbheight) {
						rc.bottom -= sbheight;
						rc.bottom += sbheight2;
						sbheight = sbheight2;
						DestroyWindow(mon->hMainWnd);
						mon->hMainWnd = NULL;
						continue;
					}
				}
				break;
			}
			if (g_darkModeSupported) {
				BOOL value = g_darkModeEnabled;
				DwmSetWindowAttribute(mon->hMainWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
			}
			GetWindowRect(mon->hMainWnd, &rc2);
			mon->window_extra_width = rc2.right - rc2.left - mon->currentmode.current_width;
			mon->window_extra_height = rc2.bottom - rc2.top - mon->currentmode.current_height;
			createstatuswindow(mon);
			createstatusline(mon->hMainWnd, mon->monitor_id);
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
	if (g_darkModeSupported) {
		BOOL value = g_darkModeEnabled;
		DwmSetWindowAttribute(mon->hAmigaWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
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
	mon->window_extra_height_bar = sbheight;
	mon->dpi = getdpiforwindow(mon->hAmigaWnd);
	createstatusline(mon->hMainWnd, mon->monitor_id);

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

		if (currprefs.win32_shutdown_notification && !rp_isactive()) {
			typedef BOOL(WINAPI *SHUTDOWNBLOCKREASONCREATE)(HWND, LPCWSTR);
			SHUTDOWNBLOCKREASONCREATE pShutdownBlockReasonCreate;
			pShutdownBlockReasonCreate = (SHUTDOWNBLOCKREASONCREATE)GetProcAddress(userdll, "ShutdownBlockReasonCreate");
			if (pShutdownBlockReasonCreate) {
				TCHAR tmp[MAX_DPATH];
				WIN32GUI_LoadUIString(IDS_SHUTDOWN_NOTIFICATION, tmp, MAX_DPATH);
				if (!pShutdownBlockReasonCreate(mon->hMainWnd, tmp)) {
					write_log(_T("ShutdownBlockReasonCreate %08x\n"), GetLastError());
				}
			}
		}
	}

	rawinput_alloc();
	return 1;
}

static void allocsoftbuffer(int monid, const TCHAR *name, struct vidbuffer *buf, int flags, int width, int height)
{
	struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
	int depth = 32;

	if (buf->initialized && buf->vram_buffer) {
		return;
	}

	buf->monitor_id = monid;
	buf->pixbytes = (depth + 7) / 8;
	buf->width_allocated = (width + 7) & ~7;
	buf->height_allocated = height;
	buf->initialized = true;
	buf->hardwiredpositioning = false;

	if (buf == &vidinfo->drawbuffer) {

		buf->bufmem = NULL;
		buf->bufmemend = NULL;
		buf->realbufmem = NULL;
		buf->bufmem_allocated = NULL;
		buf->vram_buffer = true;

	} else {

		xfree(buf->realbufmem);
		int w = buf->width_allocated;
		int h = buf->height_allocated;
		int size = (w * 2) * (h * 2) * buf->pixbytes;
		buf->rowbytes = w * 2 * buf->pixbytes;
		buf->realbufmem = xcalloc(uae_u8, size);
		buf->bufmem_allocated = buf->bufmem = buf->realbufmem + (h / 2) * buf->rowbytes + (w / 2) * buf->pixbytes;
		buf->bufmemend = buf->realbufmem + size - buf->rowbytes;

	}

}

static int oldtex_w[MAX_AMIGAMONITORS], oldtex_h[MAX_AMIGAMONITORS], oldtex_rtg[MAX_AMIGAMONITORS];

static BOOL doInit(struct AmigaMonitor *mon)
{
	int ret = 0;
	bool modechanged;
	int retrymask = 0;
	int original_api = currprefs.gfx_api;

retry:
	struct vidbuf_description *avidinfo = &adisplays[mon->monitor_id].gfxvidinfo;
	struct amigadisplay *ad = &adisplays[mon->monitor_id];
	// v6: max is always available
	avidinfo->gfx_resolution_reserved = RES_MAX;
	avidinfo->gfx_vresolution_reserved = VRES_MAX;

	modechanged = true;
	if (wasfs[0] == 0)
		regqueryint(NULL, wasfsname[0], &wasfs[0]);
	if (wasfs[1] == 0)
		regqueryint(NULL, wasfsname[1], &wasfs[1]);

	d3d_select(&currprefs);

	for (;;) {
		updatemodes(mon);

		if (mon->currentmode.flags & DM_W_FULLSCREEN) {
			RECT rc = getdisplay(&currprefs, mon->monitor_id)->rect;
			mon->currentmode.native_width = rc.right - rc.left;
			mon->currentmode.native_height = rc.bottom - rc.top;
		}
		if (!create_windows(mon))
			goto oops;
#ifdef PICASSO96
		if (mon->screen_is_picasso) {
			break;
		} else {
#endif
			if (currprefs.gfx_resolution > avidinfo->gfx_resolution_reserved)
				avidinfo->gfx_resolution_reserved = currprefs.gfx_resolution;
			if (currprefs.gfx_vresolution > avidinfo->gfx_vresolution_reserved)
				avidinfo->gfx_vresolution_reserved = currprefs.gfx_vresolution;

			mon->currentmode.amiga_width = mon->currentmode.current_width;
			mon->currentmode.amiga_height = mon->currentmode.current_height;
			break;
#ifdef PICASSO96
		}
#endif
	}

	updatepicasso96(mon);

	if (!mon->screen_is_picasso) {

		allocsoftbuffer(mon->monitor_id, _T("draw"), &avidinfo->drawbuffer, mon->currentmode.flags,
			1920, 1280);

		allocsoftbuffer(mon->monitor_id, _T("monemu"), &avidinfo->tempbuffer, mon->currentmode.flags,
			mon->currentmode.amiga_width > 2048 ? mon->currentmode.amiga_width : 2048,
			mon->currentmode.amiga_height > 2048 ? mon->currentmode.amiga_height : 2048);

	}

	avidinfo->outbuffer = &avidinfo->drawbuffer;
	avidinfo->inbuffer = &avidinfo->tempbuffer;

	if (!D3D_isenabled(mon->monitor_id)) {
		for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
			oldtex_w[i] = oldtex_h[i] = -1;
		}
	}
	if (mon->currentmode.flags & DM_D3D) {
		int fmh = mon->screen_is_picasso ? 1 : currprefs.gf[ad->gf_index].gfx_filter_filtermodeh + 1;
		int fmv = mon->screen_is_picasso ? 1 : currprefs.gf[ad->gf_index].gfx_filter_filtermodev + 1 - 1;
		if (currprefs.gf[ad->gf_index].gfx_filter_filtermodev == 0) {
			fmv = fmh;
		}
		int errv = 0;
		const TCHAR *err = D3D_init(mon->hAmigaWnd, mon->monitor_id, mon->currentmode.native_width, mon->currentmode.native_height,
			&mon->currentmode.freq, fmh, fmv, &errv);
		if (errv > 0) {
			// GDI but fullscreen/not supported
			if (errv == 2 && currprefs.gfx_api == 0) {
				if (!(retrymask & (1 << 2))) {
					retrymask |= 1 << 2;
					write_log(_T("Retrying: D3D11, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 2;
					update_gfxparams(mon);
					goto retry;
				}
			}
			// D3D9 but not supported
			if (errv == 1 && currprefs.gfx_api == 1) {
				if (!(retrymask & (1 << 2))) {
					retrymask |= 1 << 2;
					write_log(_T("Retrying: D3D11, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 2;
					update_gfxparams(mon);
					goto retry;
				} else if (!(retrymask & (1 << 0))) {
					retrymask |= 1 << 0;
					write_log(_T("Retrying: GDI, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 0;
					update_gfxparams(mon);
					goto retry;
				}
			}
			// D3D11 but not supported
			if (errv == 1 && currprefs.gfx_api == 2) {
				if (!(retrymask & (1 << 1))) {
					retrymask |= 1 << 1;
					write_log(_T("Retrying: D3D9, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 1;
					update_gfxparams(mon);
					goto retry;
				}
			}
			if (errv == 1 && currprefs.gfx_api == 3) {
				if (!(retrymask & (1 << 2))) {
					retrymask |= 1 << 2;
					write_log(_T("Retrying: D3D11 NOHDR, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 2;
					update_gfxparams(mon);
					goto retry;
				} else if (!(retrymask & (1 << 1))) {
					retrymask |= 1 << 1;
					write_log(_T("Retrying: D3D9, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 1;
					update_gfxparams(mon);
					goto retry;
				} else if (!(retrymask & (1 << 0))) {
					retrymask |= 1 << 0;
					write_log(_T("Retrying: GDI, ERR=%s\n"), err);
					changed_prefs.gfx_api = currprefs.gfx_api = 0;
					update_gfxparams(mon);
					goto retry;
				}
			}
			if (errv > 0) {
				if (isfullscreen() > 0) {
					int idx = mon->screen_is_picasso ? 1 : 0;
					changed_prefs.gfx_apmode[idx].gfx_fullscreen = currprefs.gfx_apmode[idx].gfx_fullscreen = GFX_FULLWINDOW;
					retrymask = 0;
					changed_prefs.gfx_api = currprefs.gfx_api = original_api;
					update_gfxparams(mon);
					goto retry;
				}
				if ((retrymask & (1 | 2 | 4)) == (1 | 2 | 4)) { // don't care about D3D11 HDR
					error_log(_T("Failed to initialize any rendering modes."));
				}
				ret = -1;
				goto oops;
			}
		} else if (errv < 0) {
			modechanged = false;
		}
		updatewinrect(mon, true);
	}

	mon->screen_is_initialized = 1;

	if (modechanged) {
		init_colors(mon->monitor_id);
		display_param_init(mon);
		createstatusline(mon->hAmigaWnd, mon->monitor_id);
	}
	target_graphics_buffer_update(mon->monitor_id, false);

	picasso_refresh(mon->monitor_id);
#ifdef RETROPLATFORM
	rp_set_hwnd_delayed();
#endif

	if (isfullscreen () != 0) {
		setmouseactive(mon->monitor_id, -1);
	}

	osk_setup(mon->monitor_id, -2);

	return 1;

oops:
	osk_setup(mon->monitor_id, 0);
	close_hwnds(mon);
	return ret;
}

bool target_graphics_buffer_update(int monid, bool force)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
	struct vidbuf_description *avidinfo = &adisplays[monid].gfxvidinfo;
	struct picasso96_state_struct *state = &picasso96_state[monid];
	struct vidbuffer *vb = NULL, *vbout = NULL;

	gfx_lock();

	static bool	graphicsbuffer_retry;
	int w, h;
	
	graphicsbuffer_retry = false;
	if (mon->screen_is_picasso) {
		w = state->Width;
		h = state->Height;
	} else {
		vb = avidinfo->inbuffer;
		vbout = avidinfo->outbuffer;
		if (!vb) {
			gfx_unlock();
			return false;
		}
		w = vb->outwidth;
		h = vb->outheight;
	}
	
	if (!force && oldtex_w[monid] == w && oldtex_h[monid] == h && oldtex_rtg[monid] == mon->screen_is_picasso) {
		if (D3D_alloctexture(mon->monitor_id, -w, -h)) {
			osk_setup(monid, -2);
			if (vbout) {
				vbout->width_allocated = w;
				vbout->height_allocated = h;
			}
			gfx_unlock();
			return false;
		}
	}

	if (!w || !h) {
		oldtex_w[monid] = w;
		oldtex_h[monid] = h;
		oldtex_rtg[monid] = mon->screen_is_picasso;
		gfx_unlock();
		return false;
	}

	if (!D3D_alloctexture(mon->monitor_id, w, h)) {
		graphicsbuffer_retry = true;
		gfx_unlock();
		return false;
	}

	if (vbout) {
		vbout->width_allocated = w;
		vbout->height_allocated = h;
	}

	if (avidinfo->inbuffer != avidinfo->outbuffer) {
		avidinfo->outbuffer->inwidth = w;
		avidinfo->outbuffer->inheight = h;
		avidinfo->outbuffer->width_allocated = w;
		avidinfo->outbuffer->height_allocated = h;
	}

	oldtex_w[monid] = w;
	oldtex_h[monid] = h;
	oldtex_rtg[monid] = mon->screen_is_picasso;
	osk_setup(monid, -2);

	write_log (_T("Buffer %d size (%d*%d) %s\n"), monid, w, h, mon->screen_is_picasso ? _T("RTG") : _T("Native"));

	gfx_unlock();

	return true;
}

static void updatedisplayarea2(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	if (!mon->screen_is_initialized)
		return;
	D3D_refresh(monid);
}

void updatedisplayarea(int monid)
{
	set_custom_limits(-1, -1, -1, -1, false);
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
	struct amigadisplay *ad = &adisplays[monid];

	fixup_prefs_dimensions (p);
	int fs = isfullscreen_2(p);
	if (fs != 0) {
		p->gfx_monitor[monid].gfx_size = p->gfx_monitor[monid].gfx_size_fs;
	} else {
		p->gfx_monitor[monid].gfx_size = p->gfx_monitor[monid].gfx_size_win;
	}

	int *wfw = &wasfs[ad->picasso_on ? 1 : 0];
	const TCHAR *wfwname = wasfsname[ad->picasso_on ? 1 : 0];
	if (fs != *wfw && fs != 0) {
		*wfw = fs;
		regsetint(NULL, wfwname, *wfw);
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

void close_rtg(int monid, bool reset)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	close_windows(mon);
	if (reset) {
		struct amigadisplay *ad = &adisplays[monid];
		mon->screen_is_picasso = false;
		ad->picasso_on = false;
		ad->picasso_requested_on = false;
		ad->picasso_requested_forced_on = false;
	}
}

void toggle_fullscreen(int monid, int mode)
{
	struct amigadisplay *ad = &adisplays[monid];
	int *p = ad->picasso_on ? &changed_prefs.gfx_apmode[1].gfx_fullscreen : &changed_prefs.gfx_apmode[0].gfx_fullscreen;
	int *wfw = &wasfs[ad->picasso_on ? 1 : 0];
	int v = *p;
	static int prevmode = -1;

	if (mode < 0) {
		// fullwindow->window->fullwindow.
		// fullscreen->window->fullscreen.
		// window->fullscreen->window.
		if (v == GFX_FULLWINDOW) {
			prevmode = v;
			*wfw = -1;
			v = GFX_WINDOW;
		} else if (v == GFX_WINDOW) {
			if (*wfw >= 0) {
				v = GFX_FULLSCREEN;
			} else {
				v = GFX_FULLWINDOW;
			}
		} else if (v == GFX_FULLSCREEN) {
			prevmode = v;
			*wfw = 1;
			v = GFX_WINDOW;
		}
	} else if (mode == 0) {
		prevmode = v;
		// fullscreen <> window
		if (v == GFX_FULLSCREEN)
			v = GFX_WINDOW;
		else
			v = GFX_FULLSCREEN;
	} else if (mode == 1) {
		prevmode = v;
		// fullscreen <> fullwindow
		if (v == GFX_FULLSCREEN)
			v = GFX_FULLWINDOW;
		else
			v = GFX_FULLSCREEN;
	} else if (mode == 2) {
		prevmode = v;
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

	if (D3D_isenabled(0))
		return D3D_getDC(monid, 0);
	return hdc;
}

void releasehdc(int monid, HDC hdc)
{
	if (D3D_isenabled(0)) {
		D3D_getDC(monid, hdc);
		return;
	}
}

TCHAR *outGUID(const GUID *guid)
{
	static TCHAR gb[64];
	if (guid == NULL)
		return _T("NULL");
	_stprintf(gb, _T("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}"),
		guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
	return gb;
}

const TCHAR *DXError(HRESULT ddrval)
{
	static TCHAR dderr[1000];
	_stprintf(dderr, _T("%08X S=%d F=%04X C=%04X (%d)"),
		ddrval, (ddrval & 0x80000000) ? 1 : 0,
		HRESULT_FACILITY(ddrval),
		HRESULT_CODE(ddrval),
		HRESULT_CODE(ddrval));
	return dderr;
}

struct osd_kb
{
	uae_s16 x, y;
	uae_s16 w, h;
	uae_u16 code;
	uae_s8 pressed;
	uae_s8 inverted;
};

static struct osd_kb *osd_kb_data;
static int osd_kb_selected = 11, osd_kb_x, osd_kb_y;
struct extoverlay osd_kb_eo = { 0 };

#define OSD_KB_TRANSPARENCY 0xaa
#define OSD_KB_ACTIVE_TRANSPARENCY 0xaa
#define OSD_KB_PRESSED_TRANSPARENCY 0xff
#define OSD_KB_COLOR 0xeeeeee
#define OSD_KB_ACTIVE_COLOR 0x44cc44
#define OSD_KB_PRESSED_COLOR 0x222222
#define OSD_KB_PRESSED_COLOR2 0x444444

static void drawpixel(uae_u8 *p, uae_u32 c, uae_u8 trans)
{
	((uae_u32*)p)[0] = (c & 0x00ffffff) | (trans << 24);
}

static void drawline(struct extoverlay *eo, int x1, int y1, int x2, int y2)
{
	uae_u8 *p = eo->data + y1 * eo->width * 4 + x1 * 4;
	if (x1 != x2) {
		x2++;
	}
	if (y1 != y2) {
		y2++;
	}
	while (x1 != x2) {
		drawpixel(p, OSD_KB_COLOR, OSD_KB_TRANSPARENCY);
		if (x1 < x2) {
			x1++;
			p += 4;
		} else {
			x1--;
			p -= 4;
		}
	}
	while (y1 != y2) {
		drawpixel(p, OSD_KB_COLOR, OSD_KB_TRANSPARENCY);
		if (y1 < y2) {
			y1++;
			p += eo->width * 4;
		} else {
			y1--;
			p -= eo->width * 4;
		}
	}
}

static void highlight(struct extoverlay *eo, struct osd_kb *kb, bool mode)
{
	if (kb->inverted) {
		kb->inverted = false;
		highlight(eo, kb, true);
		kb->inverted = false;
	}
	int x1 = kb->x;
	int x2 = kb->x + kb->w;
	int y1 = kb->y;
	int y2 = kb->y + kb->h;
	uae_u8 *p = eo->data + y1 * eo->width * 4;
	uae_u32 color = 0;
	uae_u8 tr = 0;
	if (mode) {
		kb->inverted = true;
		color = OSD_KB_COLOR;
		tr = OSD_KB_TRANSPARENCY;
	} else {
		color = !mode ? OSD_KB_COLOR : OSD_KB_ACTIVE_COLOR;
		tr = !mode ? OSD_KB_TRANSPARENCY : OSD_KB_ACTIVE_TRANSPARENCY;
		if (kb->pressed) {
			color = OSD_KB_PRESSED_COLOR;
			tr = OSD_KB_PRESSED_TRANSPARENCY;
		}
	}
	while (y1 <= y2) {
		uae_u8 *pp = p + x1 * 4;
		for (int x = x1; x <= x2; x++) {
			if (kb->inverted) {
				uae_u32 *p32 = (uae_u32*)pp;
				*p32 ^= 0xffffff;
			} else {
				if (pp[0] || pp[1] || pp[2]) {
					drawpixel(pp, color, tr);
				} else {
					pp[3] = tr;
				}
			}
			pp += 4;
		}
		p += eo->width * 4;
		y1++;
	}
	if (mode) {
		if (osd_kb_x < kb->x || osd_kb_x > kb->x + kb->w) {
			osd_kb_x = kb->x + kb->w / 2;
		}
		if (osd_kb_y < kb->y || osd_kb_y > kb->y + kb->h) {
			osd_kb_y = kb->y + kb->h / 2;
		}
	}
}

static const uae_s16 layout[] = {
	10,-5,13,13,13,13,13,-5,13,13,13,13,13,-5,13,13,13,13,0,
	15,10,10,10,10,10,10,10,10,10,10,10,10,10,10,-5,15,15,-5,10,10,10,10,0,
	23,10,10,10,10,10,10,10,10,10,10,10,10,12 + 256,-40,10,10,10,10,0,
	13,10,10,10,10,10,10,10,10,10,10,10,10,10,-27,10,-15,10,10,10,10,0,
	18,10,10,10,10,10,10,10,10,10,10,10,27,-5,10,10,10,-5,10,10,10,10 + 256,0,
	-8,13,13,90,13,13,-45,20,10,0,
	0
};
static const TCHAR *key_labels[] = {
	L"Esc",L"F1",L"F2",L"F3",L"F4",L"F5",L"F6",L"F7",L"F8",L"F9",L"F10",L"GUI",L"J<>M",L"↑↓",L"X",
	L"~|´",L"!|1",L"@|2",L"#|3",L"$|4",L"%|5",L"^|6",L"&|7",L"*|8",L"(|9",L")|0",L"_|-",L"+|=",L"\\||\\\\",L"←",L"Del",L"Help",L"(",L")",L"/",L"*",
	L"←|→",L"Q",L"W",L"E",L"R",L"T",L"Y",L"U",L"I",L"O",L"P",L"{|[",L"}|]",L"Ret",L"7",L"8",L"9",L"-",
	L"Ctrl",L"Caps|Lock",L"A",L"S",L"D",L"F",L"G",L"H",L"J",L"K",L"L",L":|;",L"*|'",L"",L"↑",L"4",L"5",L"6",L"+",
	L"Shift",L"",L"Z",L"X",L"C",L"V",L"B",L"N",L"M",L"<|,",L">|.",L"?|/",L"Shift",L"←",L"↓",L"→",L"1",L"2",L"3",L"E|n|t|e|r",
	L"Alt",L"\\iA",L"",L"\\iA",L"Alt",L"0",L".",
	0,0
};
static const uae_u16 key_codes[] = {
	0x45, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, AKS_ENTERGUI | 0x8000, AKS_SWAPJOYPORTS | 0x8000, 0xf001, AKS_OSK | 0x8000,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x41, 0x46, 0x5f, 0x5a, 0x5b, 0x5c, 0x5d,
	0x42, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x44, 0x3d, 0x3e, 0x3f, 0x4a,
	0x63, 0x62, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x4c, 0x2d, 0x2e, 0x2f, 0x5e,
	0x60, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x61, 0x4f, 0x4d, 0x4e, 0x1d, 0x1e, 0x1f, 0x43,
	0x64, 0x66, 0x40, 0x67, 0x64, 0x0f, 0x3c,
	0
};

static void draw_key(HDC hdc, void *bm, int bmw, int bmh, struct extoverlay *eo, float sx, float sy, const TCHAR *key, int len, int fw, int fh)
{
	SetBkMode(hdc, OPAQUE);
	BitBlt(hdc, 0, 0, bmw, bmh, NULL, 0, 0, BLACKNESS);
	TextOut(hdc, 10, 10, key, len);
	int offsetx = 10;
	int offsety = 10 - 1;
	for (int y = 0; y < fh + 2 && y < bmh; y++) {
		int eox = (int)(sx + 0.5f);
		int eoy = (int)(sy + 0.5f) + y;
		uae_u8 *src = (uae_u8 *)bm + (y + offsety) * bmw + offsetx;
		uae_u8 *dst = eo->data + eox * 4 + eoy * eo->width * 4;
		for (int x = 0; x < bmw; x++) {
			uae_u8 b = *src++;
			if (b == 2 && eox >= 0 && eox < eo->width && eoy >= 0 && eoy < eo->height) {
				drawpixel(dst, OSD_KB_COLOR, OSD_KB_TRANSPARENCY);
			}
			dst += 4;
		}
	}
}

static bool draw_keyboard(HDC hdc, HFONT *fonts, void *bm, int bmw, int bmh, struct extoverlay *eo, int fw, int fh)
{
	int num = 0, knum = 0;
	int maxcols = 240;

	float space = (float)eo->width / maxcols;
	float colwidth = (float)eo->width / maxcols;
	float rowheight = colwidth * 7.5f;

	int theight = (int)((rowheight + space) * 6 + 2 * space);
	if (eo->height < theight) {
		eo->height = theight;
	}
	eo->data = xcalloc(uae_u8, eo->width * eo->height * 4);
	if (!eo->data) {
		return false;
	}

	for (int y = 0; y < eo->height; y++) {
		uae_u8 *p = eo->data + y * eo->width * 4;
		for (int x = 0; x < eo->width; x++) {
			p[3] = OSD_KB_TRANSPARENCY;
			p += 4;
		}
	}

	float y = 2.0f * space;
	float x = 2.0f * space;
	const uae_s16 *lp = layout;
	const TCHAR **ll = key_labels;
	while (*lp) {
		uae_s16 v = *lp++;
		float w = (abs(v) & 255) * colwidth - space;
		float h = rowheight;
		if ((abs(v) >> 8) > 0) {
			h += rowheight;
			h += space;
		}
		if (v > 0) {
			const TCHAR *lab = key_labels[knum];
			if (lab) {
				float kx = x;
				float ky = y;
				int i = 0;
				bool esc = false;
				TCHAR out[10];
				TCHAR *outp = out;
				bool italic = false;
				for (;;) {
					bool skip = false;
					TCHAR c = lab[i];
					TCHAR cn = lab[i + 1];
					if (c == '\\' && cn != '\\' && !esc) {
						esc = true;
					} else if (c == 'i' && esc) {
						italic = true;
						esc = false;
					} else if (c == '|' && esc) {
						*outp++ = c;
						esc = false;
						skip = true;
					} else if (c != '|' && c) {
						*outp++ = c;
						esc = false;
					}
					if ((c == '|' && !esc && !skip) || c == 0) {
						SelectObject(hdc, italic ? fonts[1] : fonts[0]);
						draw_key(hdc, bm, bmw, bmh, eo, kx + space, ky + space, out, addrdiff(outp, out), fw, fh);
						ky += fh + 2;
						outp = out;
						italic = false;
					}
					if (!c) {
						break;
					}
					i++;
				}
			}
			if (key_labels[knum] != NULL || key_labels[knum + 1] != NULL) {
				knum++;
			}

			struct osd_kb *kb = &osd_kb_data[num];
			kb->x = (int)(x + 0.5f);
			kb->y = (int)(y + 0.5f);
			kb->w = (int)(w + 0.5f);
			kb->h = (int)(h + 0.5f);
			kb->code = key_codes[num];

			drawline(eo, kb->x, kb->y, kb->x + kb->w, kb->y);
			drawline(eo, kb->x, kb->y + 1, kb->x + kb->w, kb->y + 1);

			drawline(eo, kb->x, kb->y + kb->h, kb->x + kb->w, kb->y + kb->h);
			drawline(eo, kb->x, kb->y + kb->h - 1, kb->x + kb->w, kb->y + kb->h - 1);

			drawline(eo, kb->x, kb->y, kb->x, kb->y + kb->h);
			drawline(eo, kb->x + 1, kb->y, kb->x + 1, kb->y + kb->h);

			drawline(eo, kb->x + kb->w, kb->y, kb->x + kb->w, kb->y + kb->h);
			drawline(eo, kb->x + kb->w - 1, kb->y, kb->x + kb->w - 1, kb->y + kb->h);

			if (num == osd_kb_selected) {
				highlight(eo, kb, true);
			}
			num++;
		}
		x += w + space;

		if (*lp == 0) {
			y += rowheight + space;
			x = 2 * space;
			lp++;
		}
	}

	return true;
}

static const TCHAR *ab = _T("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

static bool drawkeys(HWND parent, struct extoverlay *eo)
{
	bool ret = false;
	HDC hdc;
	LPLOGPALETTE lp;
	HPALETTE hpal;
	BITMAPINFO *bi;
	BITMAPINFOHEADER *bih;
	HBITMAP bitmap = NULL;
	int width = 128;
	int height = 128;
	void *bm;
	int fsize = eo->height / 30;
	
	if (fsize < 4) {
		return false;
	}

	hdc = CreateCompatibleDC(NULL);
	if (hdc) {
		int y = getdpiforwindow(parent);
		int fontsize = -MulDiv(fsize, y, 72);
		fontsize = fontsize * statusline_get_multiplier(0) / 100;
		lp = (LOGPALETTE *)xcalloc(uae_u8, sizeof(LOGPALETTE) + 3 * sizeof(PALETTEENTRY));
		if (lp) {
			lp->palNumEntries = 4;
			lp->palVersion = 0x300;
			lp->palPalEntry[1].peBlue = lp->palPalEntry[1].peGreen = lp->palPalEntry[0].peRed = 0x10;
			lp->palPalEntry[2].peBlue = lp->palPalEntry[2].peGreen = lp->palPalEntry[2].peRed = 0xff;
			lp->palPalEntry[3].peBlue = lp->palPalEntry[3].peGreen = lp->palPalEntry[3].peRed = 0x7f;
			hpal = CreatePalette(lp);
			if (hpal) {
				SelectPalette(hdc, hpal, FALSE);
				bi = (BITMAPINFO *)xcalloc(uae_u8, sizeof(BITMAPINFOHEADER) + 4 * sizeof(RGBQUAD));
				if (bi) {
					bih = &bi->bmiHeader;
					bih->biSize = sizeof(BITMAPINFOHEADER);
					bih->biWidth = width;
					bih->biHeight = -height;
					bih->biPlanes = 1;
					bih->biBitCount = 8;
					bih->biCompression = BI_RGB;
					bih->biClrUsed = 4;
					bih->biClrImportant = 4;
					bi->bmiColors[1].rgbBlue = bi->bmiColors[1].rgbGreen = bi->bmiColors[1].rgbRed = 0x10;
					bi->bmiColors[2].rgbBlue = bi->bmiColors[2].rgbGreen = bi->bmiColors[2].rgbRed = 0xff;
					bi->bmiColors[3].rgbBlue = bi->bmiColors[3].rgbGreen = bi->bmiColors[3].rgbRed = 0x7f;
					bitmap = CreateDIBSection(hdc, bi, DIB_RGB_COLORS, &bm, NULL, 0);
					if (bitmap) {
						SelectObject(hdc, bitmap);
						RealizePalette(hdc);
						HFONT fonts[3] = { 0, 0, 0 };
						fonts[0] = CreateFont(fontsize, 0,
							0, 0,
							FW_BOLD,
							FALSE,
							FALSE,
							FALSE,
							DEFAULT_CHARSET,
							OUT_TT_PRECIS,
							CLIP_DEFAULT_PRECIS,
							PROOF_QUALITY,
							FIXED_PITCH | FF_DONTCARE,
							_T("Lucida Console"));
						fonts[1] = CreateFont(fontsize, 0,
							0, 0,
							FW_BOLD,
							TRUE,
							FALSE,
							FALSE,
							DEFAULT_CHARSET,
							OUT_TT_PRECIS,
							CLIP_DEFAULT_PRECIS,
							PROOF_QUALITY,
							FIXED_PITCH | FF_DONTCARE,
							_T("Lucida Console"));
						if (fonts[0] && fonts[1]) {
							SelectObject(hdc, fonts[0]);
							SetTextColor(hdc, PALETTEINDEX(2));
							SetBkColor(hdc, PALETTEINDEX(1));

							TEXTMETRIC tm;
							GetTextMetrics(hdc, &tm);
							int w = 0;
							int h = tm.tmAscent + 2;
							for (int i = 0; i < ab[i]; i++) {
								SIZE sz;
								if (GetTextExtentPoint32(hdc, &ab[i], 1, &sz)) {
									if (sz.cx > w)
										w = sz.cx;
								}
							}
							w += 1;

							if (draw_keyboard(hdc, fonts, bm, width, height, eo, w, h)) {
								ret = true;
							}
						}
						if (fonts[0]) {
							DeleteObject(fonts[0]);
						}
						if (fonts[1]) {
							DeleteObject(fonts[1]);
						}
						DeleteObject(bitmap);
					}
					xfree(bi);
				}
				DeleteObject(hpal);
			}
			xfree(lp);
		}
		ReleaseDC(NULL, hdc);
	}
	return ret;
}

void target_osk_control(int x, int y, int button, int buttonstate)
{
	if (button == 2) {
		if (buttonstate) {
			struct osd_kb *kb = &osd_kb_data[osd_kb_selected];
			kb->pressed = kb->pressed ? 0 : 1;
			highlight(&osd_kb_eo, kb, false);
			D3D_extoverlay(&osd_kb_eo, 0);
			if ((kb->code & 0xf000) != 0xf000) {
				if (kb->pressed) {
					record_key((kb->code << 1) | 0, true);
				} else {
					record_key((kb->code << 1) | 1, true);
				}
			}
		}
		return;
	}
	if (button == 1) {
		struct osd_kb *kb = &osd_kb_data[osd_kb_selected];
		if (kb->pressed) {
			kb->pressed = 0;
			highlight(&osd_kb_eo, kb, true);
			D3D_extoverlay(&osd_kb_eo, 0);
			if (kb->code != 0x62) {
				record_key((kb->code << 1) | 1, true);
			}
		}
		if (buttonstate) {
			kb->pressed = -1;
			highlight(&osd_kb_eo, kb, false);
			D3D_extoverlay(&osd_kb_eo, 0);
		}

		if (kb->code & 0x8000) {
			if ((kb->code & 0xf000) != 0xf000) {
				inputdevice_add_inputcode(kb->code & 0x7fff, buttonstate, NULL);
			} else {
				int c = kb->code & 0xfff;
				if (c == 1) {
					if (buttonstate) {
						struct AmigaMonitor *amon = &AMonitors[0];
						if (osd_kb_eo.ypos == 0) {
							osd_kb_eo.ypos = (amon->amigawin_rect.bottom - amon->amigawin_rect.top) - osd_kb_eo.height;
						} else {
							osd_kb_eo.ypos = 0;
						}
						D3D_extoverlay(&osd_kb_eo, 0);
					}
				}
			}
		} else {
			// capslock?
			if (kb->code == 0x62) {
				if (buttonstate) {
					int capsstate = getcapslockstate();
					capsstate = capsstate ? 0 : 1;
					record_key((kb->code << 1) | capsstate, true);
					setcapslockstate(capsstate);
				}
			} else {
				if (buttonstate) {
					record_key((kb->code << 1) | 0, true);
				} else {
					record_key((kb->code << 1) | 1, true);
				}
			}
		}
		return;
	}

	if (x > 1) {
		x = 1;
	}
	if (x < -1) {
		x = -1;
	}
	if (y > 1) {
		y = 1;
	}
	if (y < -1) {
		y = -1;
	}

	if (x || y) {
		for (int i = 0; osd_kb_data[i].w; i++) {
			struct osd_kb *kb = &osd_kb_data[i];
			if (kb->pressed < 0) {
				return;
			}
		}
	}

	int max = 1000;
	while (x || y) {
		osd_kb_x += x * 5;
		osd_kb_y += y * 5;
		if (osd_kb_x < 0) {
			for (int i = 0; osd_kb_data[i].w; i++) {
				struct osd_kb *kb = &osd_kb_data[i];
				if (kb->x + kb->w > osd_kb_x) {
					osd_kb_x = kb->x + kb->w;
				}
			}
		}
		if (osd_kb_y < 0) {
			for (int i = 0; osd_kb_data[i].w; i++) {
				struct osd_kb *kb = &osd_kb_data[i];
				if (kb->y + kb->h > osd_kb_y) {
					osd_kb_y = kb->y + kb->h;
				}
			}
		}
		int xmax = 0, ymax = 0;
		int i;
		for (i = 0; osd_kb_data[i].w; i++) {
			struct osd_kb *kb = &osd_kb_data[i];
			if (osd_kb_x > kb->x + kb->w) {
				xmax++;
			}
			if (osd_kb_y > kb->y + kb->h) {
				ymax++;
			}
			if (i != osd_kb_selected) {
				if (kb->x <= osd_kb_x && kb->x + kb->w >= osd_kb_x && kb->y <= osd_kb_y && kb->y + kb->h >= osd_kb_y) {
					highlight(&osd_kb_eo, &osd_kb_data[osd_kb_selected], false);
					highlight(&osd_kb_eo, kb, true);
					osd_kb_selected = i;
					D3D_extoverlay(&osd_kb_eo, 0);
					return;
				}
			}
		}
		if (xmax >= i) {
			osd_kb_x = 0;
		}
		if (ymax >= i) {
			osd_kb_y = 0;
		}
		max--;
		if (max < 0) {
			highlight(&osd_kb_eo, &osd_kb_data[osd_kb_selected], false);
			osd_kb_selected = 0;
			highlight(&osd_kb_eo, &osd_kb_data[osd_kb_selected], true);
			D3D_extoverlay(&osd_kb_eo, 0);
			return;
		}
	}

}

int on_screen_keyboard;

bool target_osd_keyboard(int show)
{
	struct AmigaMonitor *amon = &AMonitors[0];
	static bool first;

#ifdef RETROPLATFORM
	if (rp_isactive() && !on_screen_keyboard) {
		return false;
	}
#endif

	xfree(osd_kb_data);
	osd_kb_data = NULL;
	osd_kb_eo.idx = 0x7f7f0000;
	if (!show) {
		osd_kb_eo.width = -1;
		osd_kb_eo.height = -1;
		D3D_extoverlay(&osd_kb_eo, 0);
		return true;
	}

	int w = amon->amigawin_rect.right - amon->amigawinclip_rect.left;
	int h = amon->amigawin_rect.bottom - amon->amigawinclip_rect.top;

	if (w > h * 4 / 3) {
		w = h * 4 / 3;
	}
	osd_kb_eo.width = w;
	osd_kb_eo.height = w * 10 / 44;
	osd_kb_data = xcalloc(struct osd_kb, 120);
	if (osd_kb_data) {
		if (!drawkeys(amon->hAmigaWnd, &osd_kb_eo)) {
			return false;
		}
	}
	if (!first) {
		osd_kb_eo.ypos = (amon->amigawin_rect.bottom - amon->amigawin_rect.top) - osd_kb_eo.height;
	}
	if (osd_kb_eo.ypos) {
		osd_kb_eo.ypos = (amon->amigawin_rect.bottom - amon->amigawin_rect.top) - osd_kb_eo.height;
	}
	osd_kb_eo.xpos = ((amon->amigawin_rect.right - amon->amigawinclip_rect.left) - w) / 2;
	if (!osd_kb_eo.data) {
		return false;
	}
	if (!D3D_extoverlay(&osd_kb_eo, 0)) {
		return false;
	}

	first = true;
	return true;
}
