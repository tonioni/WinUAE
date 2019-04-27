/*
* UAE - The Un*x Amiga Emulator
*
* Win32 interface
*
* Copyright 1997-1998 Mathias Ortmann
* Copyright 1997-1999 Brian King
*/

//#define MEMDEBUG
#define MOUSECLIP_LOG 0
#define MOUSECLIP_HIDE 1
#define TOUCH_SUPPORT 1
#define TOUCH_DEBUG 1
#define KBHOOK 0

#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include "sysconfig.h"

#define USETHREADCHARACTERICS 1
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <zmouse.h>
#include <ddraw.h>
#include <dbt.h>
#include <math.h>
#include <mmsystem.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dbghelp.h>
#include <float.h>
#include <WtsApi32.h>
#include <Avrt.h>
#include <Cfgmgr32.h>

#include "resource.h"

#include <wintab.h>
#include "wintablet.h"
#include <pktdef.h>

#include "sysdeps.h"
#include "options.h"
#include "audio.h"
#include "sound.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "traps.h"
#include "xwin.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "keybuf.h"
#include "drawing.h"
#include "dxwrap.h"
#include "picasso96_win.h"
#include "bsdsocket.h"
#include "win32.h"
#include "win32gfx.h"
#include "registry.h"
#include "win32gui.h"
#include "autoconf.h"
#include "gui.h"
#include "uae/mman.h"
#include "avioutput.h"
#include "ahidsound.h"
#include "ahidsound_new.h"
#include "zfile.h"
#include "savestate.h"
#include "ioport.h"
#include "parser.h"
#include "scsidev.h"
#include "disk.h"
#include "catweasel.h"
#include "lcd.h"
#include "uaeipc.h"
#include "ar.h"
#include "akiko.h"
#include "cdtv.h"
#include "direct3d.h"
#include "clipboard_win32.h"
#include "blkdev.h"
#include "inputrecord.h"
#include "gfxboard.h"
#include "statusline.h"
#include "devices.h"
#ifdef RETROPLATFORM
#include "rp.h"
#include "cloanto/RetroPlatformIPC.h"
#endif
#include "uae/ppc.h"
#include "fsdb.h"
#include "uae/time.h"
#include "specialmonitors.h"

const static GUID GUID_DEVINTERFACE_HID =  { 0x4D1E55B2L, 0xF16F, 0x11CF,
{ 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
const static GUID GUID_DEVINTERFACE_KEYBOARD = { 0x884b96c3, 0x56ef, 0x11d1,
{ 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };
const static GUID GUID_DEVINTERFACE_MOUSE = { 0x378de44c, 0x56ef, 0x11d1,
{ 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };

extern int harddrive_dangerous, do_rdbdump;
extern int no_rawinput, no_directinput, no_windowsmouse;
extern int force_directsound;
extern int log_a2065, a2065_promiscuous, log_ethernet;
extern int rawinput_enabled_hid, rawinput_log;
extern int log_filesys;
extern int forcedframelatency;
int log_scsi;
int log_net;
int log_vsync, debug_vsync_min_delay, debug_vsync_forced_delay;
static int log_winmouse;
int uaelib_debug;
int pissoff_value = 15000 * CYCLE_UNIT;
unsigned int fpucontrol;
int extraframewait, extraframewait2;
int busywait;
static int noNtDelayExecution;

extern FILE *debugfile;
extern int console_logging;
OSVERSIONINFO osVersion;
static SYSTEM_INFO SystemInfo;
static int logging_started;
static MINIDUMP_TYPE minidumpmode = MiniDumpNormal;
static int doquit;
static int console_started;
void *globalipc;

int cpu_mmx = 1;
int D3DEX = 1;
int shaderon = -1;
int d3ddebug = 0;
int max_uae_width;
int max_uae_height;

HINSTANCE hInst = NULL;
HMODULE hUIDLL = NULL;
HWND (WINAPI *pHtmlHelp)(HWND, LPCWSTR, UINT, LPDWORD);
HWND hHiddenWnd, hGUIWnd;
#if KBHOOK
static HHOOK hhook;
#endif

static UINT TaskbarRestart;
static HWND TaskbarRestartHWND;
static int forceroms;
static int start_data = 0;
static void *tablet;
HCURSOR normalcursor;
static HWND hwndNextViewer;
static bool clipboard_initialized;
HANDLE AVTask;
static int all_events_disabled;
static int mainthreadid;

TCHAR VersionStr[256];
TCHAR BetaStr[64];
extern pathtype path_type;

int toggle_sound;
int paraport_mask;

HKEY hWinUAEKey = NULL;
COLORREF g_dwBackgroundColor;

int pause_emulation;

static int sound_closed;
static int recapture;
static int focus;
static int mouseinside;
int mouseactive;
int minimized;
int monitor_off;

static int mm_timerres;
static int timermode, timeon;
#define MAX_TIMEHANDLES 8
static int timehandlecounter;
static HANDLE timehandle[MAX_TIMEHANDLES];
static bool timehandleinuse[MAX_TIMEHANDLES];
static HANDLE cpu_wakeup_event;
static volatile bool cpu_wakeup_event_triggered;
int sleep_resolution;
static CRITICAL_SECTION cs_time;

TCHAR executable_path[MAX_DPATH];
TCHAR start_path_data[MAX_DPATH];
TCHAR start_path_exe[MAX_DPATH];
TCHAR start_path_plugins[MAX_DPATH];
TCHAR start_path_new1[MAX_DPATH]; /* AF2005 */
TCHAR start_path_new2[MAX_DPATH]; /* AMIGAFOREVERDATA */
TCHAR help_file[MAX_DPATH];
TCHAR bootlogpath[MAX_DPATH];
TCHAR logpath[MAX_DPATH];
bool winuaelog_temporary_enable;
int af_path_2005;
int quickstart = 1, configurationcache = 1, relativepaths = 0, artcache = 0;
int saveimageoriginalpath = 0;
int recursiveromscan = 0;

static TCHAR *inipath = NULL;

static int guijoybutton[MAX_JPORTS];
static int guijoyaxis[MAX_JPORTS][4];
static bool guijoychange;

typedef NTSTATUS(CALLBACK* NTDELAYEXECUTION)(BOOL, PLARGE_INTEGER);
typedef NTSTATUS(CALLBACK* ZWSETTIMERRESOLUTION)(ULONG, BOOLEAN, PULONG);
static NTDELAYEXECUTION pNtDelayExecution;
static ZWSETTIMERRESOLUTION pZwSetTimerResolution;

#if TOUCH_SUPPORT
typedef BOOL (CALLBACK* REGISTERTOUCHWINDOW)(HWND, ULONG);
typedef BOOL (CALLBACK* GETTOUCHINPUTINFO)(HTOUCHINPUT, UINT, PTOUCHINPUT, int);
typedef BOOL (CALLBACK* CLOSETOUCHINPUTHANDLE)(HTOUCHINPUT);

static GETTOUCHINPUTINFO pGetTouchInputInfo;
static CLOSETOUCHINPUTHANDLE pCloseTouchInputHandle;
#endif

static ULONG ActualTimerResolution;

int target_sleep_nanos(int nanos)
{
	static bool init;
	if (!init && !noNtDelayExecution) {
		pNtDelayExecution = (NTDELAYEXECUTION)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "NtDelayExecution");
		pZwSetTimerResolution = (ZWSETTIMERRESOLUTION)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "ZwSetTimerResolution");
		if (pZwSetTimerResolution) {
			// 0.5ms
			NTSTATUS status = pZwSetTimerResolution((500 * 1000) / 100, TRUE, &ActualTimerResolution);
			if (!status) {
				LARGE_INTEGER interval;
				interval.QuadPart = -(int)ActualTimerResolution;
				status = pNtDelayExecution(false, &interval);
				if (!status) {
					write_log(_T("Using NtDelayExecution. ActualTimerResolution=%u\n"), ActualTimerResolution);
				} else {
					write_log(_T("NtDelayExecution returned %08x\n"), status);
					pNtDelayExecution = NULL;
					pZwSetTimerResolution = NULL;
				}
			} else {
				write_log(_T("NtDelayExecution not available\n"));
				pNtDelayExecution = NULL;
				pZwSetTimerResolution = NULL;
			}
		}
		init = true;
	}
	if (pNtDelayExecution) {
		if (nanos < 0)
			return 800;
		LARGE_INTEGER interval;
		int start = read_processor_time();
		nanos *= 10;
		if (nanos < ActualTimerResolution)
			nanos = ActualTimerResolution;
		interval.QuadPart = -nanos;
		pNtDelayExecution(false, &interval);
		idletime += read_processor_time() - start;
	} else {
		if (nanos < 0)
			return 1300;
		sleep_millis_main(1);
	}
	return 0;
}

uae_u64 spincount;

void target_spin(int total)
{
	if (!spincount)
		return;
	if (total > 10)
		total = 10;
	while (total-- >= 0) {
		uae_u64 v1 = __rdtsc();
		v1 += spincount;
		while (v1 > __rdtsc());
	}
}

extern int vsync_activeheight;

void target_calibrate_spin(void)
{
	struct amigadisplay *ad = &adisplays[0];
	struct apmode *ap = ad->picasso_on ? &currprefs.gfx_apmode[1] : &currprefs.gfx_apmode[0];
	int vp;
	const int cntlines = 1;
	uae_u64 sc;

	spincount = 0;
	if (!ap->gfx_vsyncmode)
		return;
	if (busywait) {
		write_log(_T("target_calibrate_spin() skipped\n"));
		return;
	}
	write_log(_T("target_calibrate_spin() start\n"));
	sc = 0x800000000000;
	for (int i = 0; i < 50; i++) {
		for (;;) {
			vp = target_get_display_scanline(-1);
			if (vp <= -10)
				goto fail;
			if (vp >= 1 && vp < vsync_activeheight - 10)
				break;
		}
		uae_u64 v1;
		int vp2;
		for (;;) {
			v1 = __rdtsc();
			vp2 = target_get_display_scanline(-1);
			if (vp2 <= -10)
				goto fail;
			if (vp2 == vp + cntlines)
				break;
			if (vp2 < vp || vp2 > vp + cntlines)
				goto trynext;
		}
		for (;;) {
			int vp2 = target_get_display_scanline(-1);
			if (vp2 <= -10)
				goto fail;
			if (vp2 == vp + cntlines * 2) {
				uae_u64 scd = (__rdtsc() - v1) / cntlines;
				if (sc > scd)
					sc = scd;
			}
			if (vp2 < vp)
				break;
			if (vp2 > vp + cntlines * 2)
				break;
		}
trynext:;
	}
	if (sc == 0x800000000000) {
		write_log(_T("Spincount calculation error, spinloop not used.\n"), sc);
		spincount = 0;
	} else {
		spincount = sc;
		write_log(_T("Spincount = %llu\n"), sc);
	}
	return;
fail:
	write_log(_T("Scanline read failed: %d!\n"), vp);
	spincount = 0;
}

int timeend (void)
{
	if (!timeon)
		return 1;
	timeon = 0;
	if (timeEndPeriod (mm_timerres) == TIMERR_NOERROR)
		return 1;
	write_log (_T("TimeEndPeriod() failed\n"));
	return 0;
}

int timebegin (void)
{
	if (timeon) {
		timeend ();
		return timebegin ();
	}
	timeon = 0;
	if (timeBeginPeriod (mm_timerres) == TIMERR_NOERROR) {
		timeon = 1;
		return 1;
	}
	write_log (_T("TimeBeginPeriod() failed\n"));
	return 0;
}

static int init_mmtimer (void)
{
	TIMECAPS tc;
	int i;

	mm_timerres = 0;
	if (timeGetDevCaps (&tc, sizeof (TIMECAPS)) != TIMERR_NOERROR)
		return 0;
	mm_timerres = min (max (tc.wPeriodMin, 1), tc.wPeriodMax);
	sleep_resolution = 1000 / mm_timerres;
	for (i = 0; i < MAX_TIMEHANDLES; i++)
		timehandle[i] = CreateEvent (NULL, TRUE, FALSE, NULL);
	cpu_wakeup_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection (&cs_time);
	timehandlecounter = 0;
	return 1;
}

void sleep_cpu_wakeup(void)
{
	if (!cpu_wakeup_event_triggered) {
		cpu_wakeup_event_triggered = true;
		SetEvent(cpu_wakeup_event);
	}
}

HANDLE get_sound_event(void);
extern HANDLE waitvblankevent;

static int sleep_millis2 (int ms, bool main)
{
	UINT TimerEvent;
	int start = 0;
	int cnt;
	HANDLE sound_event = get_sound_event();
	bool wasneg = ms < 0;
	bool pullcheck = false;
	int ret = 0;

	if (ms < 0)
		ms = -ms;
	if (main) {
		if (sound_event) {
			bool pullcheck = audio_is_event_frame_possible(ms);
			if (pullcheck) {
				if (WaitForSingleObject(sound_event, 0) == WAIT_OBJECT_0) {
					if (wasneg) {
						write_log(_T("efw %d imm abort\n"), ms);
					}
					return -1;
				}
			}
		}
		if (WaitForSingleObject(cpu_wakeup_event, 0) == WAIT_OBJECT_0) {
			return 0;
		}
		if (waitvblankevent && WaitForSingleObject(waitvblankevent, 0) == WAIT_OBJECT_0) {
			return 0;
		}
		start = read_processor_time ();
	}
	EnterCriticalSection (&cs_time);
	for (;;) {
		timehandlecounter++;
		if (timehandlecounter >= MAX_TIMEHANDLES)
			timehandlecounter = 0;
		if (timehandleinuse[timehandlecounter] == false) {
			cnt = timehandlecounter;
			timehandleinuse[cnt] = true;
			break;
		}
	}
	LeaveCriticalSection (&cs_time);
	TimerEvent = timeSetEvent (ms, 0, (LPTIMECALLBACK)timehandle[cnt], 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
	if (main) {
		int c = 0;
		HANDLE evt[4];
		int sound_event_cnt = -1;
		int vblank_event_cnt = -1;
		evt[c++] = timehandle[cnt];
		evt[c++] = cpu_wakeup_event;
		if (waitvblankevent) {
			vblank_event_cnt = c;
			evt[c++] = waitvblankevent;
		}
		if (sound_event && pullcheck) {
			sound_event_cnt = c;
			evt[c++] = sound_event;
		}
		DWORD status = WaitForMultipleObjects(c, evt, FALSE, ms);
		if (sound_event_cnt >= 0 && status == WAIT_OBJECT_0 + sound_event_cnt)
			ret = -1;
		if (vblank_event_cnt >= 0 && status == WAIT_OBJECT_0 + vblank_event_cnt)
			ret = -1;
		if (wasneg) {
			if (sound_event_cnt >= 0 && status == WAIT_OBJECT_0 + sound_event_cnt) {
				write_log(_T("efw %d delayed abort\n"), ms);
			} else if (status == WAIT_TIMEOUT) {
				write_log(_T("efw %d full wait\n"), ms);
			}
		}
		cpu_wakeup_event_triggered = false;
	} else {
		WaitForSingleObject(timehandle[cnt], ms);
	}
	ResetEvent (timehandle[cnt]);
	timeKillEvent (TimerEvent);
	timehandleinuse[cnt] = false;
	if (main)
		idletime += read_processor_time () - start;
	return ret;
}

int sleep_millis_main (int ms)
{
	return sleep_millis2 (ms, true);
}
int sleep_millis (int ms)
{
	return sleep_millis2 (ms, false);
}

int sleep_millis_amiga(int ms)
{
	int ret = sleep_millis_main(ms);
	return ret;
}

bool quit_ok(void)
{
	if (isfullscreen() > 0)
		return true;
	if (!currprefs.win32_warn_exit)
		return true;
	if (quit_program == -UAE_QUIT)
		return true;
	TCHAR temp[MAX_DPATH];
	WIN32GUI_LoadUIString(IDS_QUIT_WARNING, temp, MAX_DPATH);
	int ret = gui_message_multibutton(1, temp);
	return ret == 1;
}

static void setcursor(struct AmigaMonitor *mon, int oldx, int oldy)
{
	int dx = (mon->amigawinclip_rect.left - mon->amigawin_rect.left) + (mon->amigawinclip_rect.right - mon->amigawinclip_rect.left) / 2;
	int dy = (mon->amigawinclip_rect.top - mon->amigawin_rect.top) + (mon->amigawinclip_rect.bottom - mon->amigawinclip_rect.top) / 2;
	mon->mouseposx = oldx - dx;
	mon->mouseposy = oldy - dy;

	mon->windowmouse_max_w = (mon->amigawinclip_rect.right - mon->amigawinclip_rect.left) / 2 - 50;
	mon->windowmouse_max_h = (mon->amigawinclip_rect.bottom - mon->amigawinclip_rect.top) / 2 - 50;
	if (mon->windowmouse_max_w < 10)
		mon->windowmouse_max_w = 10;
	if (mon->windowmouse_max_h < 10)
		mon->windowmouse_max_h = 10;

	if ((currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC) && currprefs.input_tablet > 0 && mousehack_alive () && isfullscreen () <= 0) {
		mon->mouseposx = mon->mouseposy = 0;
		return;
	}
#if MOUSECLIP_LOG
	write_log (_T("mon=%d %dx%d %dx%d %dx%d %dx%d (%dx%d %dx%d)\n"),
		mon->monitor_id,
		dx, dy,
		mon->mouseposx, mon->mouseposy,
		oldx, oldy,
		oldx + mon->amigawinclip_rect.left, oldy + mon->amigawinclip_rect.top,
		mon->amigawinclip_rect.left, mon->amigawinclip_rect.top,
		mon->amigawinclip_rect.right, mon->amigawinclip_rect.bottom);
#endif
	if (oldx >= 30000 || oldy >= 30000 || oldx <= -30000 || oldy <= -30000) {
		oldx = oldy = 0;
	} else {
		if (abs(mon->mouseposx) < mon->windowmouse_max_w && abs(mon->mouseposy) < mon->windowmouse_max_h)
			return;
	}
	mon->mouseposx = mon->mouseposy = 0;
	if (oldx < 0 || oldy < 0 || oldx > mon->amigawin_rect.right - mon->amigawin_rect.left || oldy > mon->amigawin_rect.bottom - mon->amigawin_rect.top) {
		write_log (_T("Mouse out of range: mon=%d %dx%d (%dx%d %dx%d)\n"), mon->monitor_id, oldx, oldy,
			mon->amigawin_rect.left, mon->amigawin_rect.top, mon->amigawin_rect.right, mon->amigawin_rect.bottom);
		return;
	}
	int cx = (mon->amigawinclip_rect.right - mon->amigawinclip_rect.left) / 2 + mon->amigawin_rect.left + (mon->amigawinclip_rect.left - mon->amigawin_rect.left);
	int cy = (mon->amigawinclip_rect.bottom - mon->amigawinclip_rect.top) / 2 + mon->amigawin_rect.top + (mon->amigawinclip_rect.top - mon->amigawin_rect.top);
#if MOUSECLIP_LOG
	write_log (_T("SetCursorPos(%d,%d) mon=%d\n"), cx, cy, mon->monitor_id);
#endif
	SetCursorPos (cx, cy);
}

static int mon_cursorclipped;

extern TCHAR config_filename[256];

static void setmaintitle(int monid)
{
	TCHAR txt[1000], txt2[500];
	HWND hwnd = AMonitors[monid].hMainWnd;

#ifdef RETROPLATFORM
	if (rp_isactive ())
		return;
#endif
	txt[0] = 0;
	if (!monid) {
		inprec_getstatus(txt);
		if (currprefs.config_window_title[0]) {
			_tcscat(txt, currprefs.config_window_title);
			_tcscat(txt, _T(" - "));
		} else if (config_filename[0]) {
			_tcscat(txt, _T("["));
			_tcscat(txt, config_filename);
			_tcscat(txt, _T("] - "));
		}
	} else {
		if (monid == currprefs.monitoremu_mon && currprefs.monitoremu >= 2) {
			_tcscat(txt, _T("["));
			_tcscat(txt, specialmonitorfriendlynames[currprefs.monitoremu - 2]);
			_tcscat(txt, _T("] - "));
		} else {
			for (int i = 0; i < MAX_RTG_BOARDS; i++) {
				if (monid == currprefs.rtgboards[i].monitor_id) {
					_tcscat(txt, _T("["));
					_tcscat(txt, gfxboard_get_name(currprefs.rtgboards[i].rtgmem_type));
					_tcscat(txt, _T("] - "));
				}
			}
		}
	}
	_tcscat (txt, _T("WinUAE"));
	txt2[0] = 0;
	if (!monid) {
		if (pause_emulation) {
			WIN32GUI_LoadUIString(IDS_WINUAETITLE_PAUSED, txt2, sizeof(txt2) / sizeof(TCHAR));
		} else if (mouseactive > 0) {
			WIN32GUI_LoadUIString((currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) ? IDS_WINUAETITLE_MMB : IDS_WINUAETITLE_NORMAL,
				txt2, sizeof(txt2) / sizeof(TCHAR));
		}
		if (_tcslen(WINUAEBETA) > 0) {
			_tcscat(txt, BetaStr);
			if (_tcslen(WINUAEEXTRA) > 0) {
				_tcscat(txt, _T(" "));
				_tcscat(txt, WINUAEEXTRA);
			}
		}
	}
	if (txt2[0]) {
		_tcscat (txt, _T(" - "));
		_tcscat (txt, txt2);
	}
	SetWindowText (hwnd, txt);
}

static int pausemouseactive;
void resumesoundpaused (void)
{
	resume_sound ();
#ifdef AHI
	ahi_open_sound ();
	ahi2_pause_sound (0);
#endif
}
void setsoundpaused (void)
{
	pause_sound ();
#ifdef AHI
	ahi_close_sound ();
	ahi2_pause_sound (1);
#endif
}
bool resumepaused(int priority)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	//write_log (_T("resume %d (%d)\n"), priority, pause_emulation);
	if (pause_emulation > priority)
		return false;
	if (!pause_emulation)
		return false;
	devices_unpause();
	resumesoundpaused ();
	if (pausemouseactive) {
		pausemouseactive = 0;
		setmouseactive(mon->monitor_id, isfullscreen() > 0 ? 1 : -1);
	}
	pause_emulation = 0;
	setsystime ();
	setmaintitle(mon->monitor_id);
	wait_keyrelease();
	return true;
}
bool setpaused(int priority)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	//write_log (_T("pause %d (%d)\n"), priority, pause_emulation);
	if (pause_emulation > priority)
		return false;
	pause_emulation = priority;
	devices_pause();
	setsoundpaused ();
	pausemouseactive = 1;
	if (isfullscreen () <= 0) {
		pausemouseactive = mouseactive;
		setmouseactive(mon->monitor_id, 0);
	}
	setmaintitle(mon->monitor_id);
	return true;
}

void setminimized(int monid)
{
	if (!minimized)
		minimized = 1;
	set_inhibit_frame(monid, IHF_WINDOWHIDDEN);
	if (isfullscreen() > 0 && D3D_resize) {
		write_log(_T("setminimized\n"));
		D3D_resize(monid, -1);
	}
}

void unsetminimized(int monid)
{
	if (minimized < 0)
		WIN32GFX_DisplayChangeRequested(2);
	else if (minimized > 0)
		full_redraw_all();
	minimized = 0;
	clear_inhibit_frame(monid, IHF_WINDOWHIDDEN);
}

void refreshtitle(void)
{
	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		struct AmigaMonitor *mon = &AMonitors[i];
		if (mon->hMainWnd && isfullscreen() == 0) {
			setmaintitle(mon->monitor_id);
		}
	}
}

#ifndef AVIOUTPUT
static int avioutput_video = 0;
#endif

void setpriority (struct threadpriorities *pri)
{
	int err;
	if (!AVTask) {
		DWORD opri = GetPriorityClass (GetCurrentProcess ());

		if (opri != IDLE_PRIORITY_CLASS && opri != NORMAL_PRIORITY_CLASS && opri != BELOW_NORMAL_PRIORITY_CLASS && opri != ABOVE_NORMAL_PRIORITY_CLASS)
			return;
		err = SetPriorityClass (GetCurrentProcess (), pri->classvalue);
		if (!err)
			write_log (_T("priority set failed, %08X\n"), GetLastError ());
	}
}

static void setcursorshape(int monid)
{
	if (currprefs.input_tablet && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC) && currprefs.input_magic_mouse_cursor == MAGICMOUSE_NATIVE_ONLY) {
		if (GetCursor() != NULL)
			SetCursor(NULL);
	}  else if (!picasso_setwincursor(monid)) {
		if (GetCursor() != normalcursor)
			SetCursor(normalcursor);
	}
}

void releasecapture(struct AmigaMonitor *mon)
{
	//write_log(_T("releasecapture %d\n"), mon_cursorclipped);
#if 0
	CURSORINFO pci;
	pci.cbSize = sizeof pci;
	GetCursorInfo(&pci);
	write_log(_T("PCI %08x %p %d %d\n"), pci.flags, pci.hCursor, pci.ptScreenPos.x, pci.ptScreenPos.y);
#endif
	if (!mon_cursorclipped)
		return;
	ClipCursor(NULL);
	ReleaseCapture();
	ShowCursor(TRUE);
	mon_cursorclipped = 0;
}

void updatemouseclip(struct AmigaMonitor *mon)
{
	if (mon_cursorclipped) {
		mon->amigawinclip_rect = mon->amigawin_rect;
		if (!isfullscreen()) {
			int idx = 0;
			reenumeratemonitors();
			while (Displays[idx].monitorname) {
				RECT out;
				struct MultiDisplay *md = &Displays[idx];
				idx++;
				if (md->rect.left == md->workrect.left && md->rect.right == md->workrect.right
					&& md->rect.top == md->workrect.top && md->rect.bottom == md->workrect.bottom)
					continue;
				// not in this monitor?
				if (!IntersectRect(&out, &md->rect, &mon->amigawin_rect))
					continue;
				for (int e = 0; e < 4; e++) {
					int v1, v2, x, y;
					LONG *lp;
					switch (e)
					{
						case 0:
						default:
						v1 = md->rect.left;
						v2 = md->workrect.left;
						lp = &mon->amigawinclip_rect.left;
						x = v1 - 1;
						y = (md->rect.bottom - md->rect.top) / 2;
						break;
						case 1:
						v1 = md->rect.top;
						v2 = md->workrect.top;
						lp = &mon->amigawinclip_rect.top;
						x = (md->rect.right - md->rect.left) / 2;
						y = v1 - 1;
						break;
						case 2:
						v1 = md->rect.right;
						v2 = md->workrect.right;
						lp = &mon->amigawinclip_rect.right;
						x = v1 + 1;
						y = (md->rect.bottom - md->rect.top) / 2;
						break;
						case 3:
						v1 = md->rect.bottom;
						v2 = md->workrect.bottom;
						lp = &mon->amigawinclip_rect.bottom;
						x = (md->rect.right - md->rect.left) / 2;
						y = v1 + 1;
						break;
					}
					// is there another monitor sharing this edge?
					POINT pt;
					pt.x = x;
					pt.y = y;
					if (MonitorFromPoint(pt, MONITOR_DEFAULTTONULL))
						continue;
					// restrict mouse clip bounding box to this edge
					if (e >= 2) {
						if (*lp > v2) {
							*lp = v2;
						}
					} else {
						if (*lp < v2) {
							*lp = v2;
						}
					}
				}
			}
			// Too small or invalid?
			if (mon->amigawinclip_rect.right <= mon->amigawinclip_rect.left + 7 || mon->amigawinclip_rect.bottom <= mon->amigawinclip_rect.top + 7)
				mon->amigawinclip_rect = mon->amigawin_rect;
		}
		if (mon_cursorclipped == mon->monitor_id + 1) {
#if MOUSECLIP_LOG
			write_log(_T("CLIP mon=%d %dx%d %dx%d %d\n"), mon->monitor_id, mon->amigawin_rect.left, mon->amigawin_rect.top, mon->amigawin_rect.right, mon->amigawin_rect.bottom, isfullscreen());
#endif
			if (!ClipCursor(&mon->amigawinclip_rect))
				write_log(_T("ClipCursor error %d\n"), GetLastError());
		}
	}
}

void updatewinrect(struct AmigaMonitor *mon, bool allowfullscreen)
{
	int f = isfullscreen ();
	if (!allowfullscreen && f > 0)
		return;
	GetWindowRect(mon->hAmigaWnd, &mon->amigawin_rect);
	GetWindowRect(mon->hAmigaWnd, &mon->amigawinclip_rect);
#if MOUSECLIP_LOG
	write_log (_T("GetWindowRect mon=%d %dx%d %dx%d %d\n"), mon->monitor_id, mon->amigawin_rect.left, mon->amigawin_rect.top, mon->amigawin_rect.right, mon->amigawin_rect.bottom, f);
#endif
	if (f == 0 && mon->monitor_id == 0) {
		changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.x = mon->amigawin_rect.left;
		changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.y = mon->amigawin_rect.top;
		currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.x = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.x;
		currprefs.gfx_monitor[mon->monitor_id].gfx_size_win.y = changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.y;
	}
}

#if KBHOOK
static bool HasAltModifier(int flags)
{
	return (flags & 0x20) == 0x20;
}

static LRESULT CALLBACK captureKey(int nCode, WPARAM wp, LPARAM lp)
{
	if (nCode >= 0)
	{
		KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT*)lp;
		DWORD vk = kbd->vkCode;
		DWORD flags = kbd->flags;

		// Disabling Windows keys 

		if (vk == VK_RWIN || vk == VK_LWIN || (vk == VK_TAB && HasAltModifier(flags))) {
			return 1;
		}
	}
	return CallNextHookEx(hhook, nCode, wp, lp);
}
#endif

static bool iswindowfocus(struct AmigaMonitor *mon)
{
	bool donotfocus = false;
	HWND f = GetFocus ();
	HWND fw = GetForegroundWindow ();
	HWND w1 = mon->hAmigaWnd;
	HWND w2 = mon->hMainWnd;

	if (f != w1 && f != w2)
		donotfocus = true;

	
	//write_log(_T("f=%p fw=%p w1=%p w2=%p\n"), f, fw, w1, w2);

#ifdef RETROPLATFORM
	if (rp_isactive()) {
		HWND hGuestParent = rp_getparent();
		DWORD dwHostProcessId;
		GetWindowThreadProcessId(hGuestParent, &dwHostProcessId);
		if (fw) {
			DWORD dwForegroundProcessId = 0;
			GetWindowThreadProcessId(fw, &dwForegroundProcessId);

			//write_log(_T("dwForegroundProcessId=%p dwHostProcessId=%p\n"), dwForegroundProcessId, dwHostProcessId);

			if (dwForegroundProcessId == dwHostProcessId) {
				donotfocus = false;
			}
		}
	}
#endif

	if (isfullscreen () > 0)
		donotfocus = false;
	return donotfocus == false;
}

bool ismouseactive (void)
{
	return mouseactive > 0;
}

void target_inputdevice_unacquire(void)
{
	close_tablet(tablet);
	tablet = NULL;
}
void target_inputdevice_acquire(void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	target_inputdevice_unacquire();
	tablet = open_tablet(mon->hAmigaWnd);
}

int getfocusedmonitor(void)
{
	if (focus > 0) {
		return focus - 1;
	}
	return 0;
}

static void setmouseactive2(struct AmigaMonitor *mon, int active, bool allowpause)
{
#ifdef RETROPLATFORM
	bool isrp = rp_isactive () != 0;
#else
	bool isrp = false;
#endif

	//write_log (_T("setmouseactive %d->%d cursor=%d focus=%d recap=%d\n"), mouseactive, active, mon_cursorclipped, focus, recapture);

	if (active == 0)
		releasecapture (mon);
	if (mouseactive == active && active >= 0)
		return;

	if (!isrp && active == 1 && !(currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC)) {
		HANDLE c = GetCursor ();
		if (c != normalcursor)
			return;
	}
	if (active) {
		if (!isrp && !IsWindowVisible (mon->hAmigaWnd))
			return;
	}

	if (active < 0)
		active = 1;

	mouseactive = active ? mon->monitor_id + 1 : 0;

	mon->mouseposx = mon->mouseposy = 0;
	//write_log (_T("setmouseactive(%d)\n"), active);
	releasecapture (mon);
	recapture = 0;

	if (isfullscreen () <= 0 && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC) && currprefs.input_tablet > 0) {
		if (mousehack_alive ())
			return;
		SetCursor (normalcursor);
	}

	bool gotfocus = false;
	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		HWND h = GetFocus();
		if (h && (h == AMonitors[i].hAmigaWnd || h == AMonitors[i].hMainWnd)) {
			mon = &AMonitors[i];
			break;
		}
	}
	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		if (iswindowfocus(&AMonitors[i])) {
			gotfocus = true;
			break;
		}
	}
	if (!gotfocus) {
		write_log (_T("Tried to capture mouse but window didn't have focus! F=%d A=%d\n"), focus, mouseactive);
		focus = 0;
		mouseactive = 0;
		active = 0;
	}

	if (mouseactive > 0)
		focus = mon->monitor_id + 1;

	//write_log(_T("setcapture %d %d %d\n"), mouseactive, focus, mon_cursorclipped);

	if (mouseactive) {
		if (focus) {
			if (GetActiveWindow() != mon->hMainWnd && GetActiveWindow() != mon->hAmigaWnd)
				SetActiveWindow(mon->hMainWnd);
			if (!mon_cursorclipped) {
				//write_log(_T("setcapture\n"));
#if MOUSECLIP_HIDE
				ShowCursor (FALSE);
#endif
				SetCapture (mon->hAmigaWnd);
				updatewinrect(mon, false);
				mon_cursorclipped = mon->monitor_id + 1;
				updatemouseclip(mon);
			}
			setcursor(mon, -30000, -30000);
		}
		wait_keyrelease();
		inputdevice_acquire(TRUE);
		setpriority (&priorities[currprefs.win32_active_capture_priority]);
		if (currprefs.win32_active_nocapture_pause) {
			resumepaused(2);
		} else if (currprefs.win32_active_nocapture_nosound && sound_closed < 0) {
			resumesoundpaused();
		}
		setmaintitle(mon->monitor_id);
#if KBHOOK
		if (!hhook) {
			hhook = SetWindowsHookEx(WH_KEYBOARD_LL, captureKey, GetModuleHandle(NULL), 0);
		}
#endif

	} else {
#if KBHOOK
		if (hhook) {
			UnhookWindowsHookEx(hhook);
			hhook = NULL;
		}
#endif
		inputdevice_acquire (FALSE);
		inputdevice_releasebuttons();
	}
	if (!active && allowpause) {
		if (currprefs.win32_active_nocapture_pause) {
			setpaused (2);
		} else if (currprefs.win32_active_nocapture_nosound) {
			setsoundpaused ();
			sound_closed = -1;
		}
		setmaintitle(mon->monitor_id);
	}
#ifdef RETROPLATFORM
	rp_mouse_capture(active);
	rp_mouse_magic(magicmouse_alive());
#endif
}

void setmouseactive(int monid, int active)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	monitor_off = 0;
	if (active > 1)
		SetForegroundWindow(mon->hAmigaWnd);
	setmouseactive2(mon, active, true);
}

static int hotkeys[] = { VK_VOLUME_UP, VK_VOLUME_DOWN, VK_VOLUME_MUTE, -1 };

static void winuae_active(struct AmigaMonitor *mon, HWND hwnd, int minimized)
{
	struct threadpriorities *pri;

	//write_log (_T("winuae_active(%p,%d)\n"), hwnd, minimized);

	monitor_off = 0;
	/* without this returning from hibernate-mode causes wrong timing
	*/
	timeend ();
	sleep_millis (2);
	timebegin ();

	focus = mon->monitor_id + 1;
	pri = &priorities[currprefs.win32_inactive_priority];
	if (!minimized)
		pri = &priorities[currprefs.win32_active_capture_priority];
	setpriority(pri);

	if (sound_closed) {
		if (sound_closed < 0) {
			resumesoundpaused ();
		} else {
			if (currprefs.win32_active_nocapture_pause) {
				if (mouseactive)
					resumepaused (2);
			} else if (currprefs.win32_iconified_pause && !currprefs.win32_inactive_pause)
				resumepaused (1);
			else if (currprefs.win32_inactive_pause)
				resumepaused (2);
		}
		sound_closed = 0;
	}
#if 0
	RegisterHotKey (mon->hAmigaWnd, IDHOT_SNAPDESKTOP, 0, VK_SNAPSHOT);
	for (int i = 0; hotkeys[i] >= 0; i++)
		RegisterHotKey (mon->hAmigaWnd, hotkeys[i], 0, hotkeys[i]);
#endif
	getcapslock ();
	wait_keyrelease ();
	inputdevice_acquire (TRUE);
	if ((isfullscreen () > 0 || currprefs.win32_capture_always) && !gui_active)
		setmouseactive(mon->monitor_id, 1);
#ifdef LOGITECHLCD
	if (!minimized)
		lcd_priority (1);
#endif
	clipboard_active(mon->hAmigaWnd, 1);
#if USETHREADCHARACTERICS
	if (os_vista && AVTask == NULL) {
		typedef HANDLE(WINAPI* AVSETMMTHREADCHARACTERISTICS)(LPCTSTR, LPDWORD);
		DWORD taskIndex = 0;
		AVSETMMTHREADCHARACTERISTICS pAvSetMmThreadCharacteristics =
			(AVSETMMTHREADCHARACTERISTICS)GetProcAddress(GetModuleHandle(_T("Avrt.dll")), "AvSetMmThreadCharacteristicsW");
		if (pAvSetMmThreadCharacteristics) {
			if (!(AVTask = pAvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex))) {
				write_log (_T("AvSetMmThreadCharacteristics failed: %d\n"), GetLastError ());
			}
		}
	}
#endif
}

static void winuae_inactive(struct AmigaMonitor *mon, HWND hWnd, int minimized)
{
	struct threadpriorities *pri;
	int wasfocus = focus;

	//write_log (_T("winuae_inactive(%d)\n"), minimized);
#if USETHREADCHARACTERICS
	if (AVTask) {
		typedef BOOL(WINAPI* AVREVERTMMTHREADCHARACTERISTICS)(HANDLE);
		AVREVERTMMTHREADCHARACTERISTICS pAvRevertMmThreadCharacteristics =
			(AVREVERTMMTHREADCHARACTERISTICS)GetProcAddress(GetModuleHandle(_T("Avrt.dll")), "AvRevertMmThreadCharacteristics");
		if (pAvRevertMmThreadCharacteristics)
			pAvRevertMmThreadCharacteristics(AVTask);
		AVTask = NULL;
	}
#endif
	if (!currprefs.win32_powersavedisabled)
		SetThreadExecutionState (ES_CONTINUOUS);
	if (minimized)
		exit_gui(0);
#if 0
	for (int i = 0; hotkeys[i] >= 0; i++)
		UnregisterHotKey (mon->hAmigaWnd, hotkeys[i]);
	UnregisterHotKey (mon->hAmigaWnd, IDHOT_SNAPDESKTOP);
#endif
	focus = 0;
	recapture = 0;
	wait_keyrelease ();
	setmouseactive(mon->monitor_id, 0);
	clipboard_active(mon->hAmigaWnd, 0);
	pri = &priorities[currprefs.win32_inactive_priority];
	if (!quit_program) {
		if (minimized) {
			pri = &priorities[currprefs.win32_iconified_priority];
			if (currprefs.win32_iconified_pause) {
				inputdevice_unacquire();
				setpaused(1);
				sound_closed = 1;
			} else if (currprefs.win32_iconified_nosound) {
				inputdevice_unacquire(true, currprefs.win32_iconified_input);
				setsoundpaused();
				sound_closed = -1;
			} else {
				inputdevice_unacquire(true, currprefs.win32_iconified_input);
			}
		} else if (mouseactive) {
			inputdevice_unacquire();
			if (currprefs.win32_active_nocapture_pause) {
				setpaused (2);
				sound_closed = 1;
			} else if (currprefs.win32_active_nocapture_nosound) {
				setsoundpaused ();
				sound_closed = -1;
			}
		} else {
			if (currprefs.win32_inactive_pause) {
				inputdevice_unacquire();
				setpaused(2);
				sound_closed = 1;
			} else if (currprefs.win32_inactive_nosound) {
				inputdevice_unacquire(true, currprefs.win32_inactive_input);
				setsoundpaused();
				sound_closed = -1;
			} else {
				inputdevice_unacquire(true, currprefs.win32_inactive_input);
			}
		}
	} else {
		inputdevice_unacquire();
	}
	setpriority (pri);
#ifdef FILESYS
	filesys_flush_cache ();
#endif
#ifdef LOGITECHLCD
	lcd_priority (0);
#endif
}

void minimizewindow(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	if (mon->screen_is_initialized)
		ShowWindow (mon->hMainWnd, SW_MINIMIZE);
}

void enablecapture(int monid)
{
	if (pause_emulation > 2)
		return;
	//write_log(_T("enablecapture\n"));
	setmouseactive(monid, 1);
	if (sound_closed < 0) {
		resumesoundpaused();
		sound_closed = 0;
	}
	if (currprefs.win32_inactive_pause || currprefs.win32_active_nocapture_pause) {
		resumepaused(2);
	}
}

void disablecapture(void)
{
	//write_log(_T("disablecapture\n"));
	setmouseactive(0, 0);
	focus = 0;
	if (currprefs.win32_active_nocapture_pause && sound_closed == 0) {
		setpaused (2);
		sound_closed = 1;
	} else if (currprefs.win32_active_nocapture_nosound && sound_closed == 0) {
		setsoundpaused ();
		sound_closed = -1;
	}
}

void gui_gameport_button_change (int port, int button, int onoff)
{
	//write_log (_T("%d %d %d\n"), port, button, onoff);
#ifdef RETROPLATFORM
	int mask = 0;
	if (button == JOYBUTTON_CD32_PLAY)
		mask = RP_JOYSTICK_BUTTON5;
	if (button == JOYBUTTON_CD32_RWD)
		mask = RP_JOYSTICK_BUTTON6;
	if (button == JOYBUTTON_CD32_FFW)
		mask = RP_JOYSTICK_BUTTON7;
	if (button == JOYBUTTON_CD32_GREEN)
		mask = RP_JOYSTICK_BUTTON4;
	if (button == JOYBUTTON_3 || button == JOYBUTTON_CD32_YELLOW)
		mask = RP_JOYSTICK_BUTTON3;
	if (button == JOYBUTTON_1 || button == JOYBUTTON_CD32_RED)
		mask = RP_JOYSTICK_BUTTON1;
	if (button == JOYBUTTON_2 || button == JOYBUTTON_CD32_BLUE)
		mask = RP_JOYSTICK_BUTTON2;
	rp_update_gameport (port, mask, onoff);
#endif
	if (onoff)
		guijoybutton[port] |= 1 << button;
	else
		guijoybutton[port] &= ~(1 << button);
	guijoychange = true;
}
void gui_gameport_axis_change (int port, int axis, int state, int max)
{
	int onoff = state ? 100 : 0;
	if (axis < 0 || axis > 3)
		return;
	if (max < 0) {
		if (guijoyaxis[port][axis] == 0)
			return;
		if (guijoyaxis[port][axis] > 0)
			guijoyaxis[port][axis]--;
	} else {
		if (state > max)
			state = max;
		if (state < 0)
			state = 0;
		guijoyaxis[port][axis] = max ? state * 127 / max : onoff;
#ifdef RETROPLATFORM
		if (axis == DIR_LEFT_BIT)
			rp_update_gameport (port, RP_JOYSTICK_LEFT, onoff);
		if (axis == DIR_RIGHT_BIT)
			rp_update_gameport (port, RP_JOYSTICK_RIGHT, onoff);
		if (axis == DIR_UP_BIT)
			rp_update_gameport (port, RP_JOYSTICK_UP, onoff);
		if (axis == DIR_DOWN_BIT)
			rp_update_gameport (port, RP_JOYSTICK_DOWN, onoff);
#endif
	}
	guijoychange = true;
}


void setmouseactivexy(int monid, int x, int y, int dir)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	int diff = 8;

	if (isfullscreen () > 0)
		return;
	x += mon->amigawin_rect.left;
	y += mon->amigawin_rect.top;
	if (dir & 1)
		x = mon->amigawin_rect.left - diff;
	if (dir & 2)
		x = mon->amigawin_rect.right + diff;
	if (dir & 4)
		y = mon->amigawin_rect.top - diff;
	if (dir & 8)
		y = mon->amigawin_rect.bottom + diff;
	if (!dir) {
		x += (mon->amigawin_rect.right - mon->amigawin_rect.left) / 2;
		y += (mon->amigawin_rect.bottom - mon->amigawin_rect.top) / 2;
	}
	if (isfullscreen () < 0) {
		POINT pt;
		pt.x = x;
		pt.y = y;
		if (MonitorFromPoint (pt, MONITOR_DEFAULTTONULL) == NULL)
			return;
	}
	if (mouseactive) {
		disablecapture ();
		SetCursorPos (x, y);
		if (dir) {
			recapture = 1;
		}
	}
}

int isfocus(void)
{
	if (isfullscreen () > 0) {
		if (!minimized)
			return 2;
		return 0;
	}
	if (currprefs.input_tablet >= TABLET_MOUSEHACK && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC)) {
		if (mouseinside)
			return 2;
		if (focus)
			return 1;
		return 0;
	}
	if (focus && mouseactive > 0)
		return 2;
	if (focus)
		return -1;
	return 0;
}

static void activationtoggle(int monid, bool inactiveonly)
{
	if (mouseactive) {
		if ((isfullscreen () > 0) || (isfullscreen () < 0 && currprefs.win32_minimize_inactive)) {
			disablecapture();
			minimizewindow(monid);
		} else {
			setmouseactive(monid, 0);
		}
	} else {
		if (!inactiveonly)
			setmouseactive(monid, 1);
	}
}

static void handleXbutton (WPARAM wParam, int updown)
{
	int b = GET_XBUTTON_WPARAM (wParam);
	int num = (b & XBUTTON1) ? 3 : (b & XBUTTON2) ? 4 : -1;
	if (num >= 0)
		setmousebuttonstate (dinput_winmouse (), num, updown);
}

#define MEDIA_INSERT_QUEUE_SIZE 10
static TCHAR *media_insert_queue[MEDIA_INSERT_QUEUE_SIZE];
static int media_insert_queue_type[MEDIA_INSERT_QUEUE_SIZE];
static int media_change_timer;
static int device_change_timer;

static int is_in_media_queue(const TCHAR *drvname)
{
	for (int i = 0; i < MEDIA_INSERT_QUEUE_SIZE; i++) {
		if (media_insert_queue[i] != NULL) {
			if (!_tcsicmp(drvname, media_insert_queue[i]))
				return i;
		}
	}
	return -1;
}

static void start_media_insert_timer(HWND hwnd)
{
	if (!media_change_timer) {
		media_change_timer = 1;
		SetTimer(hwnd, 2, 1000, NULL);
	}
}

static void add_media_insert_queue(HWND hwnd, const TCHAR *drvname, int retrycnt)
{
	int idx = is_in_media_queue(drvname);
	if (idx >= 0) {
		if (retrycnt > media_insert_queue_type[idx])
			media_insert_queue_type[idx] = retrycnt;
		write_log(_T("%s already queued for insertion, cnt=%d.\n"), drvname, retrycnt);
		start_media_insert_timer(hwnd);
		return;
	}
	for (int i = 0; i < MEDIA_INSERT_QUEUE_SIZE; i++) {
		if (media_insert_queue[i] == NULL) {
			media_insert_queue[i] = my_strdup(drvname);
			media_insert_queue_type[i] = retrycnt;
			start_media_insert_timer(hwnd);
			return;
		}
	}
}

#if TOUCH_SUPPORT

static int touch_touched;
static DWORD touch_time;

#define MAX_TOUCHES 10
struct touch_store
{
	bool inuse;
	DWORD id;
	int port;
	int button;
	int axis;
};
static struct touch_store touches[MAX_TOUCHES];

static void touch_release(struct touch_store *ts, const RECT *rcontrol)
{
	if (ts->port == 0) {
		if (ts->button == 0)
			inputdevice_uaelib(_T("JOY1_FIRE_BUTTON"), 0, 1, false);
		if (ts->button == 1)
			inputdevice_uaelib(_T("JOY1_2ND_BUTTON"), 0, 1, false);
	} else if (ts->port == 1) {
		if (ts->button == 0)
			inputdevice_uaelib(_T("JOY2_FIRE_BUTTON"), 0, 1, false);
		if (ts->button == 1)
			inputdevice_uaelib(_T("JOY2_2ND_BUTTON"), 0, 1, false);
	}
	if (ts->axis >= 0) {
		if (ts->port == 0) {
			inputdevice_uaelib(_T("MOUSE1_HORIZ"), 0, -1, false);
			inputdevice_uaelib(_T("MOUSE1_VERT"), 0, -1, false);
		} else {
			inputdevice_uaelib(_T("JOY2_HORIZ"), 0, 1, false);
			inputdevice_uaelib(_T("JOY2_VERT"), 0, 1, false);
		}
	}
	ts->button = -1;
	ts->axis = -1;
}

static void tablet_touch(DWORD id, int pressrel, int x, int y, const RECT *rcontrol)
{
	struct touch_store *ts = NULL;
	int buttony = rcontrol->bottom - (rcontrol->bottom - rcontrol->top) / 4;
	
	int new_slot = -1;
	for (int i = 0; i < MAX_TOUCHES; i++) {
		struct touch_store *tts = &touches[i];
		if (!tts->inuse && new_slot < 0)
			new_slot = i;
		if (tts->inuse && tts->id == id) {
			ts = tts;
#if TOUCH_DEBUG > 1
			write_log(_T("touch_event: old touch event %d\n"), id);
#endif
			break;
		}
	}
	if (!ts) {
		// do not allocate new if release
		if (pressrel == 0)
			return;
		if (new_slot < 0)
			return;
#if TOUCH_DEBUG > 1
		write_log(_T("touch_event: new touch event %d\n"), id);
#endif
		ts = &touches[new_slot];
		ts->inuse = true;
		ts->axis = -1;
		ts->button = -1;
		ts->id = id;

	}

	// Touch release? Release buttons, center stick/mouse.
	if (ts->inuse && ts->id == id && pressrel < 0) {
		touch_release(ts, rcontrol);
		ts->inuse = false;
		return;
	}

	// Check hit boxes if new touch.
	for (int i = 0; i < 2; i++) {
		const RECT *r = &rcontrol[i];
		if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) {

#if TOUCH_DEBUG > 1
			write_log(_T("touch_event: press=%d rect=%d wm=%d\n"), pressrel, i, dinput_winmouse());
#endif
			if (pressrel == 0) {
				// move? port can't change, axis<>button not allowed
				if (ts->port == i) {
					if (y >= buttony && ts->button >= 0) {
						int button = x > r->left + (r->right - r->left) / 2 ? 1 : 0;
						if (button != ts->button) {
							// button change, release old button
							touch_release(ts, rcontrol);
							ts->button = button;
							pressrel = 1;
						}
					}
				}
			} else if (pressrel > 0) {
				// new touch
				ts->port = i;
				if (ts->button < 0 && y >= buttony) {
					ts->button = x > r->left + (r->right - r->left) / 2 ? 1 : 0;
				} else if (ts->axis < 0 && y < buttony) {
					ts->axis = 1;
				}
			}
		}
	}

	// Directions hit?
	if (ts->inuse && ts->id == id && ts->axis >= 0) {
		const RECT *r = &rcontrol[ts->port];
		int xdiff = (r->left + (r->right - r->left) / 2) - x;
		int ydiff = (r->top + (buttony - r->top) / 2) - y;

#if TOUCH_DEBUG > 1
		write_log(_T("touch_event: rect=%d xdiff %03d ydiff %03d\n"), ts->port, xdiff, ydiff);
#endif
		xdiff = -xdiff;
		ydiff = -ydiff;

		if (ts->port == 0) {

			int div = (r->bottom - r->top) / (2 * 5);
			if (div <= 0)
				div = 1;
			int vx = xdiff / div;
			int vy = ydiff / div;

#if TOUCH_DEBUG > 1
			write_log(_T("touch_event: xdiff %03d ydiff %03d div %03d vx %03d vy %03d\n"), xdiff, ydiff, div, vx, vy);
#endif
			inputdevice_uaelib(_T("MOUSE1_HORIZ"), vx, -1, false);
			inputdevice_uaelib(_T("MOUSE1_VERT"), vy, -1, false);

		} else {

			int div = (r->bottom - r->top) / (2 * 3);
			if (div <= 0)
				div = 1;
			if (xdiff <= -div)
				inputdevice_uaelib(_T("JOY2_HORIZ"), -1, 1, false);
			else if (xdiff >= div)
				inputdevice_uaelib(_T("JOY2_HORIZ"), 1, 1, false);
			else
				inputdevice_uaelib(_T("JOY2_HORIZ"), 0, 1, false);
			if (ydiff <= -div)
				inputdevice_uaelib(_T("JOY2_VERT"), -1, 1, false);
			else if (ydiff >= div)
				inputdevice_uaelib(_T("JOY2_VERT"), 1, 1, false);
			else
				inputdevice_uaelib(_T("JOY2_VERT"), 0, 1, false);
		}
	}

	// Buttons hit?
	if (ts->inuse && ts->id == id && pressrel > 0) {
		if (ts->port == 0) {
			if (ts->button == 0)
				inputdevice_uaelib(_T("JOY1_FIRE_BUTTON"), 1, 1, false);
			if (ts->button == 1)
				inputdevice_uaelib(_T("JOY1_2ND_BUTTON"), 1, 1, false);
		} else if (ts->port == 1) {
			if (ts->button == 0)
				inputdevice_uaelib(_T("JOY2_FIRE_BUTTON"), 1, 1, false);
			if (ts->button == 1)
				inputdevice_uaelib(_T("JOY2_2ND_BUTTON"), 1, 1, false);
		}
	}
}

static void touch_event(DWORD id, int pressrel, int x, int y, const RECT *rcontrol)
{
	if (is_touch_lightpen()) {

		tablet_lightpen(x, y, -1, -1, pressrel, pressrel > 0, true, dinput_lightpen(), -1);

	} else {

		tablet_touch(id, pressrel, x, y, rcontrol);

	}
}

static int touch_prev_x, touch_prev_y;
static DWORD touch_prev_flags;

static void processtouch(struct AmigaMonitor *mon, HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	RECT rgui, rcontrol[2];
	int bottom;

	if (currprefs.input_tablet && !is_touch_lightpen())
		return;

	if (isfullscreen()) {
		rgui.left = mon->amigawin_rect.left;
		rgui.top = mon->amigawin_rect.top;
		rgui.right = mon->amigawin_rect.right;
		rgui.bottom = mon->amigawin_rect.top + 30;
		bottom = mon->amigawin_rect.bottom;
	} else {
		rgui.left = mon->mainwin_rect.left;
		rgui.top = mon->mainwin_rect.top;
		rgui.right = mon->mainwin_rect.right;
		rgui.bottom = mon->amigawin_rect.top + GetSystemMetrics(SM_CYMENU) + 2;
		bottom = mon->mainwin_rect.bottom;
	}
	int maxx = rgui.right - rgui.left;
	int maxy = rgui.bottom - rgui.top;
	int max = maxx > maxy ? maxx : maxy;

	// left control region
	rcontrol[0].left = rgui.left;
	rcontrol[0].right = rcontrol[0].left + max / 2;
	rcontrol[0].bottom = bottom;
	rcontrol[0].top = bottom - max / 2;

	// right control region
	rcontrol[1].right = rgui.right;
	rcontrol[1].left = rcontrol[1].right - max / 2;
	rcontrol[1].bottom = bottom;
	rcontrol[1].top = bottom - max / 2;

	if (!pGetTouchInputInfo || !pCloseTouchInputHandle)
		return;

	UINT cInputs = LOWORD(wParam);
	PTOUCHINPUT pInputs = new TOUCHINPUT[cInputs];
	if (NULL != pInputs) {
		if (pGetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT))) {
			for (int i = 0; i < cInputs; i++) {
				PTOUCHINPUT ti = &pInputs[i];
				int x = ti->x / 100;
				int y = ti->y / 100;

				if (x != touch_prev_x || y != touch_prev_y || ti->dwFlags != touch_prev_flags) {
					touch_prev_x = x;
					touch_prev_y = y;
					touch_prev_flags = ti->dwFlags;
#if TOUCH_DEBUG
//					write_log(_T("ID=%08x FLAGS=%08x MASK=%08x X=%d Y=%d \n"), ti->dwID, ti->dwFlags, ti->dwMask, x, y);
#endif
					if (is_touch_lightpen() || (currprefs.input_tablet == TABLET_OFF && dinput_winmouse() < 0)) {
						if (ti->dwFlags & TOUCHEVENTF_DOWN)
							touch_event(ti->dwID, 1,  x, y, rcontrol);
						if (ti->dwFlags & TOUCHEVENTF_UP)
							touch_event(ti->dwID, -1, x, y, rcontrol);
						if (ti->dwFlags & TOUCHEVENTF_MOVE)
							touch_event(ti->dwID, 0, x, y, rcontrol);
					}

					if (ti->dwFlags & TOUCHEVENTF_PRIMARY) {
						if (x < rgui.left || x >= rgui.right || y < rgui.top || y >= rgui.bottom) {
							touch_touched = 0;
						} else {
							if (ti->dwFlags & (TOUCHEVENTF_DOWN | TOUCHEVENTF_MOVE)) {
								if (!touch_touched && (ti->dwFlags & TOUCHEVENTF_DOWN)) {
									touch_touched = 1;
									touch_time = ti->dwTime;
#if TOUCH_DEBUG
									write_log(_T("TOUCHED %d\n"), touch_time);
#endif
								}
							} else if (ti->dwFlags & TOUCHEVENTF_UP) {

								if (touch_touched && ti->dwTime >= touch_time + 2 * 1000) {
#if TOUCH_DEBUG
									write_log(_T("TOUCHED GUI\n"), touch_time);
#endif
									inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
								}
#if TOUCH_DEBUG
								write_log(_T("RELEASED\n"));
#endif
								touch_touched = 0;
							}
						}
					}
				}
			}
			pCloseTouchInputHandle((HTOUCHINPUT)lParam);
		}
		delete [] pInputs;
	}
}
#endif

static void resizing(struct AmigaMonitor *mon, int mode, RECT *r)
{
	int nw = (r->right - r->left) + mon->ratio_adjust_x;
	int nh = (r->bottom - r->top) + mon->ratio_adjust_y;

	if (!mon->ratio_sizing || !mon->ratio_width || !mon->ratio_height)
		return;

	if (mode == WMSZ_BOTTOM || mode == WMSZ_TOP) {
		int w = nh * mon->ratio_width / mon->ratio_height;
		r->right = r->left + w - mon->ratio_adjust_x;
	} else if (mode == WMSZ_LEFT || mode == WMSZ_RIGHT) {
		int h = nw * mon->ratio_height / mon->ratio_width;
		r->bottom = r->top + h - mon->ratio_adjust_y;
	} else if (mode == WMSZ_BOTTOMRIGHT || mode == WMSZ_BOTTOMLEFT || mode == WMSZ_TOPLEFT || mode == WMSZ_TOPRIGHT) {
		int w = r->right - r->left;
		int h = r->bottom - r->top;
		if (nw * mon->ratio_height > nh * mon->ratio_width) {
			h = nw * mon->ratio_height / mon->ratio_width;
		} else {
			w = nh * mon->ratio_width / mon->ratio_height;
		}
		if (mode == WMSZ_BOTTOMRIGHT) {
			r->bottom = r->top + h;
			r->right = r->left + w;
		} else if (mode == WMSZ_BOTTOMLEFT) {
			r->bottom = r->top + h;
			r->left = r->right - w;
		} else if (mode == WMSZ_TOPLEFT) {
			r->top = r->bottom - h;
			r->left = r->right - w;
		} else if (mode == WMSZ_TOPRIGHT) {
			r->top = r->bottom - h;
			r->right = r->left + w;
		}
	}
}

#define MSGDEBUG 1

static LRESULT CALLBACK AmigaWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	struct AmigaMonitor *mon = NULL;
	HDC hDC;
	int mx, my;
	int istablet = (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700;
	static int mm, recursive, ignoremousemove;
	static bool ignorelbutton;

	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		if (hWnd == AMonitors[i].hAmigaWnd) {
			mon = &AMonitors[i];
			break;
		}
		if (hWnd == AMonitors[i].hMainWnd) {
			mon = &AMonitors[i];
			break;
		}
	}
	if (!mon) {
		mon = &AMonitors[0];
	}

#if MSGDEBUG > 1
	write_log (_T("AWP: %p %08x %08x %08x\n"), hWnd, message, wParam, lParam);
#endif

	if (all_events_disabled)
		return 0;

	switch (message)
	{
	case WM_QUERYENDSESSION:
	{
		if (hWnd == mon->hMainWnd && currprefs.win32_shutdown_notification && !rp_isactive()) {
			return FALSE;
		}
		return TRUE;
	}
	case WM_ENDSESSION:
		return FALSE;
	case WM_INPUT:
		monitor_off = 0;
		handle_rawinput (lParam);
		DefWindowProc (hWnd, message, wParam, lParam);
		return 0;
	case WM_INPUT_DEVICE_CHANGE:
		if (is_hid_rawinput()) {
			if (handle_rawinput_change(lParam, wParam)) {
				// wait 2 seconds before re-enumerating
				if (device_change_timer)
					KillTimer(hWnd, 4);
				device_change_timer = 1;
				SetTimer(hWnd, 4, 2000, NULL);
			}
			return 0;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	switch (message)
	{

	case WM_SETFOCUS:
		//write_log(_T("WM_SETFOCUS\n"));
		winuae_active(mon, hWnd, minimized);
		unsetminimized(mon->monitor_id);
		dx_check();
		return 0;
	case WM_EXITSIZEMOVE:
		if (wParam == SC_MOVE) {
			if (D3D_resize) {
				if (isfullscreen() > 0 && wParam == SIZE_RESTORED) {
					write_log(_T("WM_EXITSIZEMOVE restored\n"));
					D3D_resize(0, 1);
				}
				write_log(_T("WM_EXITSIZEMOVE\n"));
				D3D_resize(0, 0);
			}
		}
		return 0;
	case WM_SIZE:
		if (mon->hStatusWnd)
			SendMessage(mon->hStatusWnd, WM_SIZE, wParam, lParam);
		if (wParam == SIZE_MINIMIZED && !minimized) {
			write_log(_T("SIZE_MINIMIZED\n"));
			setminimized(mon->monitor_id);
			winuae_inactive(mon, hWnd, minimized);
		}
		return 0;
	case WM_SIZING:
		resizing(mon, wParam, (RECT*)lParam);
		return TRUE;
	case WM_ACTIVATE:
		//write_log(_T("WM_ACTIVATE %p %x\n"), hWnd, wParam);
		if (LOWORD(wParam) == WA_INACTIVE) {
			//write_log(_T("WM_ACTIVATE %x\n"), wParam);
			if (HIWORD(wParam))
				setminimized(mon->monitor_id);
			else
				unsetminimized(mon->monitor_id);
			winuae_inactive(mon, hWnd, minimized);
		}
		dx_check();
		return 0;
	case WM_MOUSEACTIVATE:
		if (isfocus() == 0)
			ignorelbutton = true;
		break;
	case WM_ACTIVATEAPP:
		D3D_restore(0, false);
		//write_log(_T("WM_ACTIVATEAPP %08x\n"), wParam);
		if (!wParam && isfullscreen() > 0 && D3D_resize && !gui_active) {
			write_log(_T("WM_ACTIVATEAPP inactive %p\n"), hWnd);
			D3D_resize(0, -1);
		} else if (wParam && isfullscreen() > 0 && D3D_resize && !gui_active) {
			write_log(_T("WM_ACTIVATEAPP active %p\n"), hWnd);
			D3D_resize(0, 1);
		}
		if (!wParam && isfullscreen() <= 0 && currprefs.win32_minimize_inactive && !gui_active) {
			minimizewindow(mon->monitor_id);
		}
#ifdef RETROPLATFORM
		rp_activate(wParam, lParam);
#endif
		dx_check();
		return 0;

	case WM_KEYDOWN:
		if (dinput_wmkey((uae_u32)lParam))
			inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
		return 0;

	case WM_LBUTTONUP:
		if (!rp_mouseevent(-32768, -32768, 0, 1)) {
			if (dinput_winmouse() >= 0 && isfocus()) {
				if (log_winmouse)
					write_log(_T("WM_LBUTTONUP\n"));
				setmousebuttonstate(dinput_winmouse(), 0, 0);
			}
		}
		return 0;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if (!rp_mouseevent(-32768, -32768, 1, 1)) {
			if (!mouseactive && !gui_active && (!mousehack_alive() || currprefs.input_tablet != TABLET_MOUSEHACK || (currprefs.input_tablet == TABLET_MOUSEHACK && !(currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC)) || isfullscreen() > 0)) {
				// borderless = do not capture with single-click
				if (ignorelbutton) {
					ignorelbutton = 0;
					if (currprefs.win32_borderless)
						return 0;
				}
				if (message == WM_LBUTTONDOWN && isfullscreen() == 0 && currprefs.win32_borderless && !rp_isactive()) {
					// full-window drag
					SendMessage(mon->hAmigaWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
					return 0;
				}
				if (!pause_emulation || currprefs.win32_active_nocapture_pause)
					setmouseactive(mon->monitor_id, (message == WM_LBUTTONDBLCLK || isfullscreen() > 0) ? 2 : 1);
			} else if (dinput_winmouse() >= 0 && isfocus()) {
				if (log_winmouse)
					write_log(_T("WM_LBUTTONDOWN\n"));
				setmousebuttonstate(dinput_winmouse(), 0, 1);
			}
		}
		return 0;
	case WM_RBUTTONUP:
		if (!rp_mouseevent(-32768, -32768, 0, 2)) {
			if (dinput_winmouse() >= 0 && isfocus()) {
				if (log_winmouse)
					write_log(_T("WM_RBUTTONUP\n"));
				setmousebuttonstate(dinput_winmouse(), 1, 0);
			}
		}
		return 0;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONDBLCLK:
		if (!rp_mouseevent(-32768, -32768, 2, 2)) {
			if (dinput_winmouse() >= 0 && isfocus() > 0) {
				if (log_winmouse)
					write_log(_T("WM_RBUTTONDOWN\n"));
				setmousebuttonstate(dinput_winmouse(), 1, 1);
			}
		}
		return 0;
	case WM_MBUTTONUP:
		if (!rp_mouseevent(-32768, -32768, 0, 4)) {
			if (!(currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON)) {
				if (log_winmouse)
					write_log(_T("WM_MBUTTONUP\n"));
				if (dinput_winmouse() >= 0 && isfocus())
					setmousebuttonstate(dinput_winmouse(), 2, 0);
			}
		}
		return 0;
	case WM_MBUTTONDOWN:
	case WM_MBUTTONDBLCLK:
		if (!rp_mouseevent(-32768, -32768, 4, 4)) {
			if (currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) {
				activationtoggle(mon->monitor_id, true);
			} else {
				if (dinput_winmouse() >= 0 && isfocus() > 0) {
					if (log_winmouse)
						write_log(_T("WM_MBUTTONDOWN\n"));
					setmousebuttonstate(dinput_winmouse(), 2, 1);
				}
			}
		}
		return 0;
	case WM_XBUTTONUP:
		if (!rp_ismouseevent() && dinput_winmouse() >= 0 && isfocus()) {
			if (log_winmouse)
				write_log(_T("WM_XBUTTONUP %08x\n"), wParam);
			handleXbutton(wParam, 0);
			return TRUE;
		}
		return 0;
	case WM_XBUTTONDOWN:
	case WM_XBUTTONDBLCLK:
		if (!rp_ismouseevent() && dinput_winmouse() >= 0 && isfocus() > 0) {
			if (log_winmouse)
				write_log(_T("WM_XBUTTONDOWN %08x\n"), wParam);
			handleXbutton(wParam, 1);
			return TRUE;
		}
		return 0;
	case WM_MOUSEWHEEL:
		if (!rp_ismouseevent() && dinput_winmouse() >= 0 && isfocus() > 0) {
			int val = ((short)HIWORD(wParam));
			if (log_winmouse)
				write_log(_T("WM_MOUSEWHEEL %08x\n"), wParam);
			setmousestate(dinput_winmouse(), 2, val, 0);
			if (val < 0)
				setmousebuttonstate(dinput_winmouse(), dinput_wheelbuttonstart() + 0, -1);
			else if (val > 0)
				setmousebuttonstate(dinput_winmouse(), dinput_wheelbuttonstart() + 1, -1);
			return TRUE;
		}
		return 0;
	case WM_MOUSEHWHEEL:
		if (!rp_ismouseevent() && dinput_winmouse() >= 0 && isfocus() > 0) {
			int val = ((short)HIWORD(wParam));
			if (log_winmouse)
				write_log(_T("WM_MOUSEHWHEEL %08x\n"), wParam);
			setmousestate(dinput_winmouse(), 3, val, 0);
			if (val < 0)
				setmousebuttonstate(dinput_winmouse(), dinput_wheelbuttonstart() + 2, -1);
			else if (val > 0)
				setmousebuttonstate(dinput_winmouse(), dinput_wheelbuttonstart() + 3, -1);
			return TRUE;
		}
		return 0;

	case WM_PAINT:
	{
		static int recursive = 0;
		if (recursive == 0) {
			PAINTSTRUCT ps;
			recursive++;
			notice_screen_contents_lost(mon->monitor_id);
			hDC = BeginPaint(hWnd, &ps);
			/* Check to see if this WM_PAINT is coming while we've got the GUI visible */
			if (mon->manual_painting_needed)
				updatedisplayarea(mon->monitor_id);
			EndPaint(hWnd, &ps);
			recursive--;
		}
	}
	return 0;

	case WM_DROPFILES:
		dragdrop(hWnd, (HDROP)wParam, &changed_prefs, -2);
		return 0;

	case WM_TIMER:
		if (wParam == 2) {
			bool restart = false;
			KillTimer(hWnd, 2);
			media_change_timer = 0;
			DWORD r = CMP_WaitNoPendingInstallEvents(0);
			write_log(_T("filesys timer, CMP_WaitNoPendingInstallEvents=%d\n"), r);
			if (r == WAIT_OBJECT_0) {
				for (int i = 0; i < MEDIA_INSERT_QUEUE_SIZE; i++) {
					if (media_insert_queue[i]) {
						TCHAR *drvname = media_insert_queue[i];
						int r = my_getvolumeinfo(drvname);
						if (r < 0) {
							if (media_insert_queue_type[i] > 0) {
								write_log(_T("Mounting %s but drive is not ready, %d.. retrying %d..\n"), drvname, r, media_insert_queue_type[i]);
								media_insert_queue_type[i]--;
								restart = true;
								continue;
							} else {
								write_log(_T("Mounting %s but drive is not ready, %d.. aborting..\n"), drvname, r);
							}
						} else {
							int inserted = 1;
							DWORD type = GetDriveType(drvname);
							if (type == DRIVE_CDROM)
								inserted = -1;
							r = filesys_media_change(drvname, inserted, NULL);
							if (r < 0) {
								write_log(_T("Mounting %s but previous media change is still in progress..\n"), drvname);
								restart = true;
								break;
							} else if (r > 0) {
								write_log(_T("%s mounted\n"), drvname);
							} else {
								write_log(_T("%s mount failed\n"), drvname);
							}
						}
						xfree(media_insert_queue[i]);
						media_insert_queue[i] = NULL;
					}
				}
			} else if (r == WAIT_TIMEOUT) {
				restart = true;
			}
			if (restart)
				start_media_insert_timer(hWnd);
		} else if (wParam == 4) {
			device_change_timer = 0;
			KillTimer(hWnd, 4);
			inputdevice_devicechange(&changed_prefs);
			inputdevice_copyjports(&changed_prefs, &workprefs);
		} else if (wParam == 1) {
#ifdef PARALLEL_PORT
			finishjob();
#endif
		}
		return 0;

	case WM_CREATE:
#ifdef RETROPLATFORM
		rp_set_hwnd(hWnd);
#endif
		DragAcceptFiles(hWnd, TRUE);
		normalcursor = LoadCursor(NULL, IDC_ARROW);
		if (!clipboard_initialized) {
			clipboard_initialized = true;
			hwndNextViewer = SetClipboardViewer(hWnd);
			clipboard_init(hWnd);
		}
		return 0;

	case WM_DESTROY:
		clipboard_initialized = false;
		if (device_change_timer)
			KillTimer(hWnd, 4);
		device_change_timer = 0;
		wait_keyrelease();
		inputdevice_unacquire();
		dinput_window();
		return 0;

	case WM_CLOSE:
		if (quit_ok())
			uae_quit();
		return 0;

	case WM_WINDOWPOSCHANGED:
	{
		WINDOWPOS *wp = (WINDOWPOS*)lParam;
		if (isfullscreen() <= 0) {
			if (!IsIconic(hWnd) && hWnd == mon->hAmigaWnd) {
				updatewinrect(mon, false);
				updatemouseclip(mon);
			}
		}
	}
	break;

	case WM_SETCURSOR:
	{
		if ((HWND)wParam == mon->hAmigaWnd && currprefs.input_tablet > 0 && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC) && isfullscreen() <= 0) {
			if (mousehack_alive()) {
				setcursorshape(mon->monitor_id);
				return 1;
			}
		}
		break;
	}

	case WM_KILLFOCUS:
		//write_log(_T("killfocus\n"));
		focus = 0;
		return 0;

	case WM_MOUSELEAVE:
		mouseinside = false;
		//write_log(_T("mouseoutside\n"));
		return 0;

	case WM_MOUSEMOVE:
	{
		int wm = dinput_winmouse();

		monitor_off = 0;
		if (!mouseinside) {
			//write_log(_T("mouseinside\n"));
			TRACKMOUSEEVENT tme = { 0 };
			mouseinside = true;
			tme.cbSize = sizeof tme;
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = mon->hAmigaWnd;
			TrackMouseEvent(&tme);
		}

		mx = (signed short)LOWORD(lParam);
		my = (signed short)HIWORD(lParam);

		if (log_winmouse)
			write_log (_T("WM_MOUSEMOVE MON=%d NUM=%d ACT=%d FOCUS=%d CLIP=%d FS=%d %dx%d %dx%d\n"),
				mon->monitor_id, wm, mouseactive, focus, mon_cursorclipped, isfullscreen (), mx, my, mon->mouseposx, mon->mouseposy);

		if (rp_mouseevent(mx, my, -1, -1))
			return 0;

		mx -= mon->mouseposx;
		my -= mon->mouseposy;

		if (recapture && isfullscreen() <= 0) {
			enablecapture(mon->monitor_id);
			return 0;
		}

		if (wm < 0 && (istablet || currprefs.input_tablet >= TABLET_MOUSEHACK)) {
			/* absolute */
			setmousestate(0, 0, mx, 1);
			setmousestate(0, 1, my, 1);
			return 0;
		}
		if (wm >= 0) {
			if (istablet || currprefs.input_tablet >= TABLET_MOUSEHACK) {
				/* absolute */
				setmousestate(dinput_winmouse(), 0, mx, 1);
				setmousestate(dinput_winmouse(), 1, my, 1);
				return 0;
			}
			if (!focus || !mouseactive)
				return DefWindowProc(hWnd, message, wParam, lParam);
			/* relative */
			int mxx = (mon->amigawinclip_rect.left - mon->amigawin_rect.left) + (mon->amigawinclip_rect.right - mon->amigawinclip_rect.left) / 2;
			int myy = (mon->amigawinclip_rect.top - mon->amigawin_rect.top) + (mon->amigawinclip_rect.bottom - mon->amigawinclip_rect.top) / 2;
			mx = mx - mxx;
			my = my - myy;
			setmousestate(dinput_winmouse(), 0, mx, 0);
			setmousestate(dinput_winmouse(), 1, my, 0);
		} else if (isfocus() < 0 && (istablet || currprefs.input_tablet >= TABLET_MOUSEHACK)) {
			setmousestate(0, 0, mx, 1);
			setmousestate(0, 1, my, 1);
		}
		if (mon_cursorclipped || mouseactive)
			setcursor(mon, LOWORD(lParam), HIWORD(lParam));
		return 0;
	}
	break;

	case WM_MOVING:
	{
		LRESULT lr = DefWindowProc(hWnd, message, wParam, lParam);
		return lr;
	}
	case WM_MOVE:
		return FALSE;

	case WM_ENABLE:
		rp_set_enabledisable(wParam ? 1 : 0);
		return FALSE;

#ifdef FILESYS
	case WM_USER + 2:
	{
		LONG lEvent;
		PIDLIST_ABSOLUTE *ppidl;
		HANDLE lock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam, &ppidl, &lEvent);
		if (lock) {
			if (lEvent == SHCNE_MEDIAINSERTED || lEvent == SHCNE_DRIVEADD || lEvent == SHCNE_MEDIAREMOVED || lEvent == SHCNE_DRIVEREMOVED) {
				TCHAR drvpath[MAX_DPATH + 1];
				if (SHGetPathFromIDList(ppidl[0], drvpath)) {
					int inserted = (lEvent == SHCNE_MEDIAINSERTED || lEvent == SHCNE_DRIVEADD) ? 1 : 0;
					write_log(_T("Shell Notification %d '%s'\n"), inserted, drvpath);
					if (!win32_hardfile_media_change(drvpath, inserted)) {
						if (inserted) {
							add_media_insert_queue(hWnd, drvpath, 5);
						} else {
							if (is_in_media_queue(drvpath) >= 0) {
								write_log(_T("Insertion queued, removal event dropped\n"));
							} else {
								filesys_media_change(drvpath, inserted, NULL);
							}
						}
					}
				}
			}
			SHChangeNotification_Unlock(lock);
		}
	}
	return TRUE;
	case WM_DEVICECHANGE:
	{
		extern bool win32_spti_media_change(TCHAR driveletter, int insert);
		extern bool win32_ioctl_media_change(TCHAR driveletter, int insert);
		DEV_BROADCAST_HDR *pBHdr = (DEV_BROADCAST_HDR *)lParam;
		int devicechange = 0;
		if (pBHdr && pBHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
			bool ignore = false;
			DEV_BROADCAST_DEVICEINTERFACE *dbd = (DEV_BROADCAST_DEVICEINTERFACE*)lParam;
			GUID *g = &dbd->dbcc_classguid;
			const GUID *ghid = &GUID_DEVINTERFACE_HID;
			// if HID and rawhid active: ignore this event
			if (!memcmp(g, ghid, sizeof(GUID))) {
				if (is_hid_rawinput())
					ignore = true;
			}
			if (!ignore) {
				if (wParam == DBT_DEVICEREMOVECOMPLETE)
					devicechange = 1;
				else if (wParam == DBT_DEVICEARRIVAL)
					devicechange = 1;
			}
		} else if (pBHdr && pBHdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
			DEV_BROADCAST_VOLUME *pBVol = (DEV_BROADCAST_VOLUME *)lParam;
			if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
				if (pBVol->dbcv_unitmask) {
					int inserted, i;
					TCHAR drive;
					UINT errormode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
					for (i = 0; i <= 'Z' - 'A'; i++) {
						if (pBVol->dbcv_unitmask & (1 << i)) {
							TCHAR drvname[10];
							int type;

							drive = 'A' + i;
							_stprintf(drvname, _T("%c:\\"), drive);
							type = GetDriveType(drvname);
							if (wParam == DBT_DEVICEARRIVAL)
								inserted = 1;
							else
								inserted = 0;
							if (pBVol->dbcv_flags & DBTF_MEDIA) {
								bool matched = false;
								matched |= win32_spti_media_change(drive, inserted);
								matched |= win32_ioctl_media_change(drive, inserted);
							}
							if (type == DRIVE_REMOVABLE || type == DRIVE_CDROM || !inserted) {
								write_log(_T("WM_DEVICECHANGE '%s' type=%d inserted=%d\n"), drvname, type, inserted);
								if (!win32_hardfile_media_change(drvname, inserted)) {
									if (inserted) {
										add_media_insert_queue(hWnd, drvname, 0);
									} else {
										if (is_in_media_queue(drvname) >= 0) {
											write_log(_T("Insertion queued, removal event dropped\n"));
										} else {
											filesys_media_change(drvname, inserted, NULL);
										}
									}
								}
							}
						}
					}
					SetErrorMode(errormode);
				}
			}
		}
		if (devicechange) { // && !is_hid_rawinput()) {
			if (device_change_timer)
				KillTimer(hWnd, 4);
			device_change_timer = 1;
			SetTimer(hWnd, 4, 2000, NULL);
		}
	}
#endif
	return TRUE;

	case WM_MENUCHAR:
		return MNC_CLOSE << 16;

	case WM_SYSCOMMAND:
		switch (wParam & 0xfff0) // Check System Calls
		{
			// SetThreadExecutionState handles this now
		case SC_SCREENSAVE: // Screensaver Trying To Start?
			break;
		case SC_MONITORPOWER: // Monitor Trying To Enter Powersave?
			write_log(_T("SC_MONITORPOWER=%d"), lParam);
			if ((int)lParam < 0)
				monitor_off = 0;
			else if ((int)lParam > 0)
				monitor_off = 1;
			break;

		default:
		{
			LRESULT lr;

			if ((wParam & 0xfff0) == SC_CLOSE) {
#ifdef RETROPLATFORM
				if (rp_close())
					return 0;
#endif

				if (!quit_ok())
					return 0;
				uae_quit();
			}
			lr = DefWindowProc(hWnd, message, wParam, lParam);
			switch (wParam & 0xfff0)
			{
			case SC_MINIMIZE:
				winuae_inactive(mon, hWnd, 1);
				break;
			case SC_RESTORE:
				break;
			case SC_CLOSE:
				break;
			}
			return lr;
		}
		}
		break;

	case WM_SYSKEYDOWN:
		if (currprefs.win32_ctrl_F11_is_quit && wParam == VK_F4)
			return 0;
		break;

	case WM_NOTIFY:
	{
		LPNMHDR nm = (LPNMHDR)lParam;
		if (nm->hwndFrom == mon->hStatusWnd) {
			switch (nm->code)
			{
				/* status bar clicks */
			case NM_CLICK:
			case NM_RCLICK:
			{
				LPNMMOUSE lpnm = (LPNMMOUSE)lParam;
				int num = (int)lpnm->dwItemSpec;
				int df0 = 9;
				if (num >= df0 && num <= df0 + 3) { // DF0-DF3
					num -= df0;
					if (nm->code == NM_RCLICK) {
						disk_eject(num);
					} else if (changed_prefs.floppyslots[num].dfxtype >= 0) {
						DiskSelection(hWnd, IDC_DF0 + num, 0, &changed_prefs, NULL, NULL);
						disk_insert(num, changed_prefs.floppyslots[num].df);
					}
				} else if (num == 5) {
					if (nm->code == NM_CLICK) // POWER
						inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
					else
						uae_reset(0, 1);
				} else if (num == 4) {
					if (pause_emulation) {
						resumepaused(9);
						setmouseactive(mon->monitor_id, 1);
					}
				}
				return TRUE;
			}
			}
		}
	}
	break;

	case WM_CHANGECBCHAIN:
		if (clipboard_initialized) {
			if ((HWND)wParam == hwndNextViewer)
				hwndNextViewer = (HWND)lParam;
			else if (hwndNextViewer != NULL)
				SendMessage(hwndNextViewer, message, wParam, lParam);
			return 0;
		}
		break;
	case WM_DRAWCLIPBOARD:
		if (clipboard_initialized) {
			clipboard_changed(hWnd);
			SendMessage(hwndNextViewer, message, wParam, lParam);
			return 0;
		}
		break;

	case WM_WTSSESSION_CHANGE:
	{
		static int wasactive;
		switch (wParam)
		{
		case WTS_CONSOLE_CONNECT:
		case WTS_SESSION_UNLOCK:
			if (wasactive)
				winuae_active(mon, hWnd, 0);
			wasactive = 0;
			break;
		case WTS_CONSOLE_DISCONNECT:
		case WTS_SESSION_LOCK:
			wasactive = mouseactive;
			winuae_inactive(mon, hWnd, 0);
			break;
		}
	}

	case WT_PROXIMITY:
	{
		send_tablet_proximity(LOWORD(lParam) ? 1 : 0);
		return 0;
	}
	case WT_PACKET:
	{
		typedef BOOL(API* WTPACKET)(HCTX, UINT, LPVOID);
		extern WTPACKET pWTPacket;
		PACKET pkt;
		if (inputdevice_is_tablet() <= 0 && !currprefs.tablet_library && !is_touch_lightpen()) {
			close_tablet(tablet);
			tablet = NULL;
			return 0;
		}
		if (pWTPacket((HCTX)lParam, wParam, &pkt)) {
			int x, y, z, pres, proxi;
			DWORD buttons;
			ORIENTATION ori;
			ROTATION rot;

			x = pkt.pkX;
			y = pkt.pkY;
			z = pkt.pkZ;
			pres = pkt.pkNormalPressure;
			ori = pkt.pkOrientation;
			rot = pkt.pkRotation;
			buttons = pkt.pkButtons;
			proxi = pkt.pkStatus;
			send_tablet(x, y, z, pres, buttons, proxi, ori.orAzimuth, ori.orAltitude, ori.orTwist, rot.roPitch, rot.roRoll, rot.roYaw, &mon->amigawin_rect);

		}
		return 0;
	}

#if TOUCH_SUPPORT
	case WM_TOUCH:
		processtouch(mon, hWnd, wParam, lParam);
		break;
#endif

	default:
		break;
	}

	return DefWindowProc (hWnd, message, wParam, lParam);
}

static int canstretch(struct AmigaMonitor *mon)
{
	if (isfullscreen () != 0)
		return 0;
	if (!WIN32GFX_IsPicassoScreen(mon)) {
		if (!currprefs.gfx_windowed_resize)
			return 0;
		if (currprefs.gf[APMODE_NATIVE].gfx_filter_autoscale == AUTOSCALE_RESIZE)
			return 0;
		return 1;
	} else {
		if (currprefs.win32_rtgallowscaling || currprefs.gf[APMODE_RTG].gfx_filter_autoscale)
			return 1;
	}
	return 0;
}

static void plot (LPDRAWITEMSTRUCT lpDIS, int x, int y, int dx, int dy, int idx)
{
	COLORREF rgb;

	x += dx;
	y += dy;
	if (idx == 0)
		rgb = RGB(0x00,0x00,0xff);
	else if (idx == 1)
		rgb = RGB(0xff,0x00,0x00);
	else if (idx == 2)
		rgb = RGB(0xff,0xff,0x00);
	else if (idx == 3)
		rgb = RGB(0x00,0xff,0x00);
	else
		rgb = RGB(0x00,0x00,0x00);

	SetPixel (lpDIS->hDC, x, y, rgb);

	SetPixel (lpDIS->hDC, x + 1, y, rgb);
	SetPixel (lpDIS->hDC, x - 1, y, rgb);

	SetPixel (lpDIS->hDC, x, y + 1, rgb);
	SetPixel (lpDIS->hDC, x, y - 1, rgb);
}

static LRESULT CALLBACK MainWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static RECT myrect;
	struct AmigaMonitor *mon = NULL;
	PAINTSTRUCT ps;
	RECT rc;
	HDC hDC;

	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		if (hWnd == AMonitors[i].hMainWnd) {
			mon = &AMonitors[i];
			break;
		}
	}
	if (!mon) {
		mon = &AMonitors[0];
	}

#if MSGDEBUG > 1
	write_log (_T("MWP: %x %d\n"), hWnd, message);
#endif

	if (all_events_disabled)
		return 0;

	switch (message)
	{
	case WM_SETCURSOR:
	case WM_KILLFOCUS:
	case WM_SETFOCUS:
	case WM_MOUSEMOVE:
	case WM_MOUSELEAVE:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_ACTIVATEAPP:
	case WM_DROPFILES:
	case WM_ACTIVATE:
	case WM_MENUCHAR:
	case WM_SYSCOMMAND:
	case WM_KEYUP:
	case WM_SYSKEYUP:
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MOVING:
	case WM_MOVE:
	case WM_SIZING:
	case WM_SIZE:
	case WM_DESTROY:
	case WM_CLOSE:
	case WM_HELP:
	case WM_DEVICECHANGE:
	case WM_INPUT:
	case WM_INPUT_DEVICE_CHANGE:
	case WM_USER + 1:
	case WM_USER + 2:
	case WM_COMMAND:
	case WM_NOTIFY:
	case WM_ENABLE:
	case WT_PACKET:
	case WM_WTSSESSION_CHANGE:
	case WM_TIMER:
#if TOUCH_SUPPORT
	case WM_TOUCH:
#endif
	case WM_QUERYENDSESSION:
	case WM_ENDSESSION:
		return AmigaWindowProc (hWnd, message, wParam, lParam);
#if 0
	case WM_DISPLAYCHANGE:
		if (isfullscreen() <= 0 && !currprefs.gfx_filter && (wParam + 7) / 8 != DirectDraw_GetBytesPerPixel ())
			WIN32GFX_DisplayChangeRequested ();
		break;
#endif
		case WM_DWMCOMPOSITIONCHANGED:
		case WM_THEMECHANGED:
		WIN32GFX_DisplayChangeRequested (-1);
		return 0;

		case WM_POWERBROADCAST:
		if (wParam == PBT_APMRESUMEAUTOMATIC) {
			setsystime ();
			return TRUE;
		}
		return 0;

	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO lpmmi;
			lpmmi = (LPMINMAXINFO)lParam;
			lpmmi->ptMinTrackSize.x = 160 + mon->window_extra_width;
			lpmmi->ptMinTrackSize.y = 128 + mon->window_extra_height;
			lpmmi->ptMaxTrackSize.x = max_uae_width + mon->window_extra_width;
			lpmmi->ptMaxTrackSize.y = max_uae_height + mon->window_extra_height;
		}
		return 0;

	case WM_ENTERSIZEMOVE:
		mon->in_sizemove++;
		mon->ratio_width = mon->amigawin_rect.right - mon->amigawinclip_rect.left;
		mon->ratio_height = mon->amigawin_rect.bottom - mon->amigawinclip_rect.top;
		mon->ratio_adjust_x = mon->ratio_width - (mon->mainwin_rect.right - mon->mainwin_rect.left);
		mon->ratio_adjust_y = mon->ratio_height - (mon->mainwin_rect.bottom - mon->mainwin_rect.top);
		mon->ratio_sizing = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		break;

	case WM_EXITSIZEMOVE:
		mon->in_sizemove--;
		/* fall through */

	case WM_WINDOWPOSCHANGED:
		{
			if (isfullscreen () > 0)
				break;
			if (mon->in_sizemove > 0)
				break;
			int iconic = IsIconic (hWnd);
			if (mon->hAmigaWnd && hWnd == mon->hMainWnd && !iconic) {
				//write_log (_T("WM_WINDOWPOSCHANGED MAIN\n"));
				GetWindowRect(mon->hMainWnd, &mon->mainwin_rect);
				updatewinrect(mon, false);
				updatemouseclip(mon);
				if (minimized) {
					unsetminimized(mon->monitor_id);
					winuae_active(mon, mon->hAmigaWnd, minimized);
				}
				if (isfullscreen() == 0) {
					static int store_xy;
					RECT rc2;
					if (GetWindowRect (mon->hMainWnd, &rc2)) {
						DWORD left = rc2.left - mon->win_x_diff;
						DWORD top = rc2.top - mon->win_y_diff;
						DWORD width = rc2.right - rc2.left;
						DWORD height = rc2.bottom - rc2.top;
						if (store_xy++ && !mon->monitor_id) {
							regsetint (NULL, _T("MainPosX"), left);
							regsetint (NULL, _T("MainPosY"), top);
						}
						changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.x = left;
						changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.y = top;
						if (canstretch(mon)) {
							int w = mon->mainwin_rect.right - mon->mainwin_rect.left;
							int h = mon->mainwin_rect.bottom - mon->mainwin_rect.top;
							if (w != changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.width + mon->window_extra_width ||
								h != changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.height + mon->window_extra_height) {
									changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.width = w - mon->window_extra_width;
									changed_prefs.gfx_monitor[mon->monitor_id].gfx_size_win.height = h - mon->window_extra_height;
									set_config_changed();
							}
						}
					}
					if (mon->hStatusWnd)
						SendMessage(mon->hStatusWnd, WM_SIZE, wParam, lParam);
					return 0;
				}
			}
		}
		break;

	case WM_WINDOWPOSCHANGING:
		{
			WINDOWPOS *wp = (WINDOWPOS*)lParam;
			if (!canstretch(mon))
				wp->flags |= SWP_NOSIZE;
			break;
		}

	case WM_PAINT:
		hDC = BeginPaint (hWnd, &ps);
		GetClientRect (hWnd, &rc);
		DrawEdge (hDC, &rc, EDGE_SUNKEN, BF_RECT);
		EndPaint (hWnd, &ps);
		return 0;

	case WM_NCLBUTTONDBLCLK:
		if (wParam == HTCAPTION) {
			if (GetKeyState (VK_SHIFT)) {
				toggle_fullscreen(0, 0);
				return 0;
			} else if (GetKeyState (VK_CONTROL)) {
				toggle_fullscreen(0, 2);
				return 0;
			}
		}
		break;

	case WM_DRAWITEM:
	{
		LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;
		if (lpDIS->hwndItem == mon->hStatusWnd) {
			HBRUSH b = (HBRUSH)(COLOR_3DFACE + 1);
			if (mon->hStatusBkgB == NULL) {
				COLORREF c = GetPixel(lpDIS->hDC, lpDIS->rcItem.left + (lpDIS->rcItem.right - lpDIS->rcItem.left) / 2, lpDIS->rcItem.top + (lpDIS->rcItem.bottom - lpDIS->rcItem.top) / 2);
				if (c != CLR_INVALID) {
					mon->hStatusBkgB = CreateSolidBrush(c);
				}
			}
			if (mon->hStatusBkgB != NULL) {
				b = mon->hStatusBkgB;
			}
			if (lpDIS->itemID == window_led_msg_start) {
				COLORREF oc;
				int x = lpDIS->rcItem.left + 1;
				int y = (lpDIS->rcItem.bottom - lpDIS->rcItem.top + 1) / 2 + lpDIS->rcItem.top - 1;
				const TCHAR *txt = statusline_fetch();
				int flags = DT_VCENTER | DT_SINGLELINE | DT_LEFT;

				FillRect(lpDIS->hDC, &lpDIS->rcItem, mon->hStatusBkgB);
				if (txt) {
					SetBkMode(lpDIS->hDC, TRANSPARENT);
					oc = SetTextColor(lpDIS->hDC, RGB(0x00, 0x00, 0x00));
					DrawText(lpDIS->hDC, txt, _tcslen(txt), &lpDIS->rcItem, flags);
					SetTextColor(lpDIS->hDC, oc);
				}
			} else if (lpDIS->itemID > 0 && lpDIS->itemID <= window_led_joy_start) {
				int port = lpDIS->itemID - 1;
				int x = (lpDIS->rcItem.right - lpDIS->rcItem.left + 1) / 2 + lpDIS->rcItem.left - 1;
				int y = (lpDIS->rcItem.bottom - lpDIS->rcItem.top + 1) / 2 + lpDIS->rcItem.top - 1;
				RECT r = lpDIS->rcItem;
				r.left++;
				r.right--;
				r.top++;
				r.bottom--;
				FillRect (lpDIS->hDC, &r, mon->hStatusBkgB);
				for (int i = 0; i < 2; i++) {
					int buttons = guijoybutton[port + i * 2];
					int m = i == 0 ? 1 : 2;
					bool got = false;
					if (buttons & (1 << JOYBUTTON_CD32_BLUE)) {
						plot (lpDIS, x - 1, y,  0,  0, 0);
						got = true;
					}
					if (buttons & (1 << JOYBUTTON_CD32_RED)) {
						plot (lpDIS, x + 1, y,  0,  0, 1);
						got = true;
					}
					if (buttons & (1 << JOYBUTTON_CD32_YELLOW)) {
						plot (lpDIS, x, y - 1,  0,  0, 2);
						got = true;
					}
					if (buttons & (1 << JOYBUTTON_CD32_GREEN)) {
						plot (lpDIS, x, y + 1,  0,  0, 3);
						got = true;
					}
					if (!got) {
						if (buttons & 1)
							plot (lpDIS, x, y,  0,  0, 1);
						if (buttons & 2)
							plot (lpDIS, x, y,  0,  0, 0);
						if (buttons & ~(1 | 2))
							plot (lpDIS, x, y,  0,  0, -1);
					}
					for (int j = 0; j < 4; j++) {
						int dx = 0, dy = 0;
						int axis = guijoyaxis[port + i * 2][j];
						if (j == DIR_LEFT_BIT)
							dx = -1;
						if (j == DIR_RIGHT_BIT)
							dx = +1;
						if (j == DIR_UP_BIT)
							dy = -1;
						if (j == DIR_DOWN_BIT)
							dy = +1;
						if (axis && (dx || dy)) {
							dx *= axis * 8 / 127;
							dy *= axis * 8 / 127;
							plot (lpDIS, x, y,  dx, dy, -1);
						}
					}
				}
			} else {
				DWORD flags, tflags;
				COLORREF oc;
				TCHAR *txt = (TCHAR*)lpDIS->itemData;
				tflags = txt[_tcslen (txt) + 1];
				SetBkMode (lpDIS->hDC, TRANSPARENT);
				if ((tflags & 2) == 0)
					tflags &= ~(4 | 8 | 16);
				if (tflags & 4) {
					oc = SetTextColor (lpDIS->hDC, RGB(0xcc, 0x00, 0x00)); // writing
				} else if (tflags & 8) {
					oc = SetTextColor (lpDIS->hDC, RGB(0x00, 0xcc, 0x00)); // playing
				} else {
					oc = SetTextColor (lpDIS->hDC, GetSysColor ((tflags & 2) ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
				}
				flags = DT_VCENTER | DT_SINGLELINE;
				if (tflags & 1) {
					flags |= DT_CENTER;
					lpDIS->rcItem.left++;
					lpDIS->rcItem.right -= 3;
				} else {
					flags |= DT_LEFT;
					lpDIS->rcItem.right--;
					lpDIS->rcItem.left += 2;
				}
				DrawText (lpDIS->hDC, txt, _tcslen (txt), &lpDIS->rcItem, flags);
				SetTextColor (lpDIS->hDC, oc);
			}
		}
		break;
	}
	
	default:
		break;

	}
	return DefWindowProc (hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK HiddenWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	switch (message)
	{
	case WM_USER + 1: /* Systray icon */
		switch (lParam)
		{
		case WM_LBUTTONDOWN:
			SetForegroundWindow (hGUIWnd ? hGUIWnd : mon->hMainWnd);
			break;
		case WM_LBUTTONDBLCLK:
		case NIN_SELECT:
			if (!gui_active)
				inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
			break;
		case WM_RBUTTONDOWN:
		case WM_CONTEXTMENU:
		case NIN_KEYSELECT:
			if (!gui_active)
				systraymenu (hWnd);
			else
				SetForegroundWindow (hGUIWnd ? hGUIWnd : mon->hMainWnd);
			break;
		}
		break;
	case WM_COMMAND:
		switch (wParam & 0xffff)
		{
		case ID_ST_CONFIGURATION:
			inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
			break;
		case ID_ST_HELP:
			HtmlHelp (NULL, help_file, 0, NULL);
			break;
		case ID_ST_QUIT:
			uae_quit ();
			break;
		case ID_ST_RESET:
			uae_reset (0, 1);
			break;

		case ID_ST_CDEJECTALL:
			changed_prefs.cdslots[0].name[0] = 0;
			changed_prefs.cdslots[0].inuse = false;
			break;
		case ID_ST_CD0:
			DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_CD_SELECT, 17, &changed_prefs, NULL, NULL);
			break;

		case ID_ST_EJECTALL:
			disk_eject (0);
			disk_eject (1);
			disk_eject (2);
			disk_eject (3);
			break;
		case ID_ST_DF0:
			DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF0, 0, &changed_prefs, NULL, NULL);
			disk_insert (0, changed_prefs.floppyslots[0].df);
			break;
		case ID_ST_DF1:
			DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF1, 0, &changed_prefs, NULL, NULL);
			disk_insert (1, changed_prefs.floppyslots[0].df);
			break;
		case ID_ST_DF2:
			DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF2, 0, &changed_prefs, NULL, NULL);
			disk_insert (2, changed_prefs.floppyslots[0].df);
			break;
		case ID_ST_DF3:
			DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF3, 0, &changed_prefs, NULL, NULL);
			disk_insert (3, changed_prefs.floppyslots[0].df);
			break;

		}
		break;
	}
	if (TaskbarRestart != 0 && TaskbarRestartHWND == hWnd && message == TaskbarRestart) {
		//write_log (_T("notif: taskbarrestart\n"));
		systray (TaskbarRestartHWND, FALSE);
	}
	return DefWindowProc (hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK BlankWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc (hWnd, message, wParam, lParam);
}

int handle_msgpump (void)
{
	int got = 0;
	MSG msg;

	while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
		got = 1;
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
	while (checkIPC (globalipc, &currprefs));
	return got;
}

bool handle_events (void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	static int was_paused = 0;
	static int cnt1, cnt2;
	static int pausedelay;

	if (mon->hStatusWnd && guijoychange && window_led_joy_start > 0) {
		guijoychange = false;
		for (int i = 0; i < window_led_joy_start; i++)
			PostMessage(mon->hStatusWnd, SB_SETTEXT, (WPARAM)((i + 1) | SBT_OWNERDRAW), (LPARAM)_T(""));
	}

	pausedelay = 0;
	if (pause_emulation) {
		MSG msg;
		if (was_paused == 0) {
			timeend();
			setpaused (pause_emulation);
			was_paused = pause_emulation;
			mon->manual_painting_needed++;
			gui_fps (0, 0, 0);
			gui_led (LED_SND, 0, -1);
			// we got just paused, report it to caller.
			return 1;
		}
		MsgWaitForMultipleObjects (0, NULL, FALSE, 100, QS_ALLINPUT);
		while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
		if (D3D_run) {
			if (D3D_run(0)) {
				full_redraw_all();
			}
		}
		inputdevicefunc_keyboard.read ();
		inputdevicefunc_mouse.read ();
		inputdevicefunc_joystick.read ();
		inputdevice_handle_inputcode ();
#ifdef RETROPLATFORM
		rp_vsync ();
#endif
		cnt1 = 0;
		while (checkIPC (globalipc, &currprefs));
//		if (quit_program)
//			break;
		cnt2--;
		if (cnt2 <= 0) {
			if (currprefs.win32_powersavedisabled)
				SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
			cnt2 = 10;
		}
	}
	if (was_paused && (!pause_emulation || quit_program)) {
		updatedisplayarea(mon->monitor_id);
		mon->manual_painting_needed--;
		pause_emulation = was_paused;
		resumepaused (was_paused);
		sound_closed = 0;
		was_paused = 0;
		timebegin();
	}
	cnt1--;
	if (cnt1 <= 0) {
		uae_time_calibrate();
		flush_log ();
		cnt1 = 50 * 5;
		cnt2--;
		if (cnt2 <= 0) {
			if (currprefs.win32_powersavedisabled)
				SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
			cnt2 = 5;
		}
	}
	if (D3D_run) {
		if (D3D_run(0))
			full_redraw_all();
	}
	return pause_emulation != 0;
}

/* We're not a console-app anymore! */
void setup_brkhandler (void)
{
}
void remove_brkhandler (void)
{
}

static void WIN32_UnregisterClasses (void)
{
	systray (hHiddenWnd, TRUE);
	DestroyWindow (hHiddenWnd);
}

static int WIN32_RegisterClasses (void)
{
	WNDCLASS wc;
	HDC hDC;
	COLORREF black = RGB(0, 0, 0);

	g_dwBackgroundColor = RGB(10, 0, 10);
	hDC = GetDC (NULL);
	if (GetDeviceCaps (hDC, NUMCOLORS) != -1)
		g_dwBackgroundColor = RGB (255, 0, 255);
	ReleaseDC (NULL, hDC);

	wc.style = CS_DBLCLKS | CS_OWNDC;
	wc.lpfnWndProc = AmigaWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.lpszMenuName = 0;
	wc.lpszClassName = _T("AmigaPowah");
	wc.hbrBackground = CreateSolidBrush (g_dwBackgroundColor);
	if (!RegisterClass (&wc))
		return 0;

	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush (black);
	wc.lpszMenuName = 0;
	wc.lpszClassName = _T("PCsuxRox");
	if (!RegisterClass (&wc))
		return 0;

	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = BoxArtWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(g_dwBackgroundColor);
	wc.lpszMenuName = 0;
	wc.lpszClassName = _T("BoxArt");
	if (!RegisterClass(&wc))
		return 0;

	wc.style = 0;
	wc.lpfnWndProc = HiddenWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	wc.hCursor = NULL;
	wc.hbrBackground = CreateSolidBrush (g_dwBackgroundColor);
	wc.lpszMenuName = 0;
	wc.lpszClassName = _T("Useless");
	if (!RegisterClass (&wc))
		return 0;

	wc.style = 0;
	wc.lpfnWndProc = BlankWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	wc.hCursor = NULL;
	wc.hbrBackground = CreateSolidBrush (g_dwBackgroundColor);
	wc.lpszMenuName = 0;
	wc.lpszClassName = _T("Blank");
	if (!RegisterClass (&wc))
		return 0;

	hHiddenWnd = CreateWindowEx (0,
		_T("Useless"), _T("You don't see me"),
		WS_POPUP,
		0, 0,
		1, 1,
		NULL, NULL, 0, NULL);
	if (!hHiddenWnd)
		return 0;

	return 1;
}

static HINSTANCE hRichEdit = NULL, hHtmlHelp = NULL;

int WIN32_CleanupLibraries (void)
{
	if (hRichEdit)
		FreeLibrary (hRichEdit);

	if (hHtmlHelp)
		FreeLibrary (hHtmlHelp);

	if (hUIDLL)
		FreeLibrary (hUIDLL);
	CoUninitialize ();
	return 1;
}

/* HtmlHelp Initialization - optional component */
int WIN32_InitHtmlHelp (void)
{
	TCHAR *chm = _T("WinUAE.chm");
	int result = 0;
	_stprintf(help_file, _T("%s%s"), start_path_data, chm);
	if (!zfile_exists (help_file))
		_stprintf(help_file, _T("%s%s"), start_path_exe, chm);
	if (zfile_exists (help_file)) {
		if (hHtmlHelp = LoadLibrary (_T("HHCTRL.OCX"))) {
			pHtmlHelp = (HWND(WINAPI *)(HWND, LPCWSTR, UINT, LPDWORD))GetProcAddress (hHtmlHelp, "HtmlHelpW");
			result = 1;
		}
	}
	return result;
}

const struct winuae_lang langs[] =
{
	{ LANG_AFRIKAANS, _T("Afrikaans") },
	{ LANG_ARABIC, _T("Arabic") },
	{ LANG_ARMENIAN, _T("Armenian") },
	{ LANG_ASSAMESE, _T("Assamese") },
	{ LANG_AZERI, _T("Azeri") },
	{ LANG_BASQUE, _T("Basque") },
	{ LANG_BELARUSIAN, _T("Belarusian") },
	{ LANG_BENGALI, _T("Bengali") },
	{ LANG_BULGARIAN, _T("Bulgarian") },
	{ LANG_CATALAN, _T("Catalan") },
	{ LANG_CHINESE, _T("Chinese") },
	{ LANG_CROATIAN, _T("Croatian") },
	{ LANG_CZECH, _T("Czech") },
	{ LANG_DANISH, _T("Danish") },
	{ LANG_DUTCH, _T("Dutch") },
	{ LANG_ESTONIAN, _T("Estoanian") },
	{ LANG_FAEROESE, _T("Faeroese") },
	{ LANG_FARSI, _T("Farsi") },
	{ LANG_FINNISH, _T("Finnish") },
	{ LANG_FRENCH, _T("French") },
	{ LANG_GEORGIAN, _T("Georgian") },
	{ LANG_GERMAN, _T("German") },
	{ LANG_GREEK, _T("Greek") },
	{ LANG_GUJARATI, _T("Gujarati") },
	{ LANG_HEBREW, _T("Hebrew") },
	{ LANG_HINDI, _T("Hindi") },
	{ LANG_HUNGARIAN, _T("Hungarian") },
	{ LANG_ICELANDIC, _T("Icelandic") },
	{ LANG_INDONESIAN, _T("Indonesian") },
	{ LANG_ITALIAN, _T("Italian") },
	{ LANG_JAPANESE, _T("Japanese") },
	{ LANG_KANNADA, _T("Kannada") },
	{ LANG_KASHMIRI, _T("Kashmiri") },
	{ LANG_KAZAK, _T("Kazak") },
	{ LANG_KONKANI, _T("Konkani") },
	{ LANG_KOREAN, _T("Korean") },
	{ LANG_LATVIAN, _T("Latvian") },
	{ LANG_LITHUANIAN, _T("Lithuanian") },
	{ LANG_MACEDONIAN, _T("Macedonian") },
	{ LANG_MALAY, _T("Malay") },
	{ LANG_MALAYALAM, _T("Malayalam") },
	{ LANG_MANIPURI, _T("Manipuri") },
	{ LANG_MARATHI, _T("Marathi") },
	{ LANG_NEPALI, _T("Nepali") },
	{ LANG_NORWEGIAN, _T("Norwegian") },
	{ LANG_ORIYA, _T("Oriya") },
	{ LANG_POLISH, _T("Polish") },
	{ LANG_PORTUGUESE, _T("Portuguese") },
	{ LANG_PUNJABI, _T("Punjabi") },
	{ LANG_ROMANIAN, _T("Romanian") },
	{ LANG_RUSSIAN, _T("Russian") },
	{ LANG_SANSKRIT, _T("Sanskrit") },
	{ LANG_SINDHI, _T("Sindhi") },
	{ LANG_SLOVAK, _T("Slovak") },
	{ LANG_SLOVENIAN, _T("Slovenian") },
	{ LANG_SPANISH, _T("Spanish") },
	{ LANG_SWAHILI, _T("Swahili") },
	{ LANG_SWEDISH, _T("Swedish") },
	{ LANG_TAMIL, _T("Tamil") },
	{ LANG_TATAR, _T("Tatar") },
	{ LANG_TELUGU, _T("Telugu") },
	{ LANG_THAI, _T("Thai") },
	{ LANG_TURKISH, _T("Turkish") },
	{ LANG_UKRAINIAN, _T("Ukrainian") },
	{ LANG_UZBEK, _T("Uzbek") },
	{ LANG_VIETNAMESE, _T("Vietnamese") },
	{ LANG_ENGLISH, _T("default") },
	{ 0x400, _T("guidll.dll") },
	{ 0, NULL }
};
static TCHAR *getlanguagename(DWORD id)
{
	int i;
	for (i = 0; langs[i].name; i++) {
		if (langs[i].id == id)
			return langs[i].name;
	}
	return NULL;
}

HMODULE language_load (WORD language)
{
	HMODULE result = NULL;

	if (language == 0xffff || language == 0) {
		/* new user-specific Windows ME/2K/XP method to get UI language */
		language = GetUserDefaultUILanguage ();
		language &= 0x3ff; // low 9-bits form the primary-language ID
	}

	TCHAR kblname[KL_NAMELENGTH];
	if (GetKeyboardLayoutName(kblname)) {
		// This is so stupid, function that returns hex number as a string?
		// GetKeyboardLayout() does not work. It seems to return locale, not keyboard layout.
		TCHAR *endptr;
		uae_u32 kbl = _tcstol(kblname, &endptr, 16);
		uae_u32 kblid = kbl & 0x3ff;
		if (kblid == LANG_GERMAN)
			hrtmon_lang = 2;
		if (kblid == LANG_FRENCH)
			hrtmon_lang = 3;
	}

#if LANG_DLL > 0
	TCHAR dllbuf[MAX_DPATH];
	TCHAR *dllname;
	dllname = getlanguagename (language);
	if (dllname) {
		DWORD  dwVersionHandle, dwFileVersionInfoSize;
		LPVOID lpFileVersionData = NULL;
		BOOL   success = FALSE;
		int fail = 1;

		if (language == 0x400)
			_tcscpy (dllbuf, _T("guidll.dll"));
		else
			_stprintf (dllbuf, _T("WinUAE_%s.dll"), dllname);
		result = WIN32_LoadLibrary (dllbuf);
		if (result)  {
			dwFileVersionInfoSize = GetFileVersionInfoSize (dllbuf, &dwVersionHandle);
			if (dwFileVersionInfoSize) {
				if (lpFileVersionData = xcalloc (uae_u8, dwFileVersionInfoSize)) {
					if (GetFileVersionInfo (dllbuf, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData)) {
						VS_FIXEDFILEINFO *vsFileInfo = NULL;
						UINT uLen;
						fail = 0;
						if (VerQueryValue (lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen)) {
							if (vsFileInfo &&
								HIWORD(vsFileInfo->dwProductVersionMS) == UAEMAJOR
								&& LOWORD(vsFileInfo->dwProductVersionMS) == UAEMINOR
								&& (!LANG_DLL_FULL_VERSION_MATCH || (HIWORD(vsFileInfo->dwProductVersionLS) == UAESUBREV))) {
									success = TRUE;
									write_log (_T("Translation DLL '%s' loaded and enabled\n"), dllbuf);
							} else {
								write_log (_T("Translation DLL '%s' version mismatch (%d.%d.%d)\n"), dllbuf,
									HIWORD(vsFileInfo->dwProductVersionMS),
									LOWORD(vsFileInfo->dwProductVersionMS),
									HIWORD(vsFileInfo->dwProductVersionLS));
							}
						}
					}
					xfree (lpFileVersionData);
				}
			}
		}
		if (fail) {
			DWORD err = GetLastError ();
			if (err != ERROR_MOD_NOT_FOUND && err != ERROR_DLL_NOT_FOUND)
				write_log (_T("Translation DLL '%s' failed to load, error %d\n"), dllbuf, GetLastError ());
		}
		if (result && !success) {
			FreeLibrary (result);
			result = NULL;
		}
	}
#endif
	return result;
}

struct threadpriorities priorities[] = {
	{ NULL, THREAD_PRIORITY_ABOVE_NORMAL, ABOVE_NORMAL_PRIORITY_CLASS, IDS_PRI_ABOVENORMAL },
	{ NULL, THREAD_PRIORITY_NORMAL, NORMAL_PRIORITY_CLASS, IDS_PRI_NORMAL },
	{ NULL, THREAD_PRIORITY_BELOW_NORMAL, BELOW_NORMAL_PRIORITY_CLASS, IDS_PRI_BELOWNORMAL },
	{ NULL, THREAD_PRIORITY_LOWEST, IDLE_PRIORITY_CLASS, IDS_PRI_LOW },
	{ 0, 0, 0, 0 }
};

static void pritransla (void)
{
	int i;

	for (i = 0; priorities[i].id; i++) {
		TCHAR tmp[MAX_DPATH];
		WIN32GUI_LoadUIString (priorities[i].id, tmp, sizeof (tmp) / sizeof (TCHAR));
		priorities[i].name = my_strdup (tmp);
	}
}

static void WIN32_InitLang (void)
{
	int lid;
	WORD langid = 0xffff;

	if (regqueryint (NULL, _T("Language"), &lid))
		langid = (WORD)lid;
	hUIDLL = language_load (langid);
	pritransla ();
}

typedef HRESULT (CALLBACK* SETCURRENTPROCESSEXPLICITAPPUSERMODEIDD)(PCWSTR);

/* try to load COMDLG32 and DDRAW, initialize csDraw */
static int WIN32_InitLibraries (void)
{
	LARGE_INTEGER freq;
	SETCURRENTPROCESSEXPLICITAPPUSERMODEIDD pSetCurrentProcessExplicitAppUserModelID; 

	/* Determine our processor speed and capabilities */
	if (!init_mmtimer ()) {
		pre_gui_message (_T("MMTimer initialization failed, exiting.."));
		return 0;
	}
	if (!QueryPerformanceCounter (&freq)) {
		pre_gui_message (_T("No QueryPerformanceFrequency() supported, exiting..\n"));
		return 0;
	}
	uae_time_init();
	if (!timebegin ()) {
		pre_gui_message (_T("MMTimer second initialization failed, exiting.."));
		return 0;
	}
	pSetCurrentProcessExplicitAppUserModelID = (SETCURRENTPROCESSEXPLICITAPPUSERMODEIDD)GetProcAddress (
		GetModuleHandle (_T("shell32.dll")), "SetCurrentProcessExplicitAppUserModelID");
	if (pSetCurrentProcessExplicitAppUserModelID)
		pSetCurrentProcessExplicitAppUserModelID (WINUAEAPPNAME);

	hRichEdit = LoadLibrary (_T("RICHED32.DLL"));
	return 1;
}

int debuggable (void)
{
	return 0;
}

void toggle_mousegrab(void)
{
	activationtoggle(0, false);
}


#define LOG_BOOT _T("winuaebootlog.txt")
#define LOG_NORMAL _T("winuaelog.txt")

static bool createbootlog = true;
static bool logging_disabled = false;

void logging_open (int bootlog, int append)
{
	TCHAR *outpath;
	TCHAR debugfilename[MAX_DPATH];

	if (logging_disabled && !winuaelog_temporary_enable)
		return;

	outpath = logpath;
	debugfilename[0] = 0;
#ifndef	SINGLEFILE
	if (currprefs.win32_logfile || winuaelog_temporary_enable) {
		_stprintf (debugfilename, _T("%s%s"), start_path_data, LOG_NORMAL);
	}
	if (bootlog) {
		_stprintf (debugfilename, _T("%s%s"), start_path_data, LOG_BOOT);
		outpath = bootlogpath;
		if (!createbootlog)
			bootlog = -1;
	}
	if (debugfilename[0]) {
		if (!debugfile)
			debugfile = log_open (debugfilename, append, bootlog, outpath);
	}
#endif
}

typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

void logging_init (void)
{
#ifndef _WIN64
	LPFN_ISWOW64PROCESS fnIsWow64Process;
#endif
	int wow64 = 0;
	static int started;
	static int first;
	TCHAR tmp[MAX_DPATH], filedate[256];

	if (first > 1) {
		write_log (_T("** RESTART **\n"));
		return;
	}
	if (first == 1) {
		write_log (_T("Log (%s): '%s%s'\n"), currprefs.win32_logfile || winuaelog_temporary_enable ? _T("enabled") : _T("disabled"),
			start_path_data, LOG_NORMAL);
		if (debugfile)
			log_close (debugfile);
		debugfile = 0;
	}
	logging_open (first ? 0 : 1, 0);
	logging_started = 1;
	first++;

#ifdef _WIN64
	wow64 = 1;
#else
	fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress (GetModuleHandle (_T("kernel32")), "IsWow64Process");
	if (fnIsWow64Process)
		fnIsWow64Process (GetCurrentProcess (), &wow64);
#endif

	_tcscpy (filedate, _T("?"));
	if (GetModuleFileName (NULL, tmp, sizeof tmp / sizeof (TCHAR))) {
		HANDLE h = CreateFile (tmp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			FILETIME ft;
			if (GetFileTime (h, &ft, NULL, NULL)) {
				SYSTEMTIME st;
				if (FileTimeToSystemTime(&ft, &st)) {
					_stprintf (filedate, _T("%02d:%02d"), st.wHour, st.wMinute);
				}
			}
			CloseHandle (h);
		}
	}

	write_log (_T("\n%s (%d.%d.%d %s%s[%d])"), VersionStr,
		osVersion.dwMajorVersion, osVersion.dwMinorVersion, osVersion.dwBuildNumber, osVersion.szCSDVersion,
		_tcslen (osVersion.szCSDVersion) > 0 ? _T(" ") : _T(""), os_admin);
	write_log (_T(" %d-bit %X.%X.%X %d %s %d"),
		wow64 ? 64 : 32,
		SystemInfo.wProcessorArchitecture, SystemInfo.wProcessorLevel, SystemInfo.wProcessorRevision,
		SystemInfo.dwNumberOfProcessors, filedate, os_touch);
	write_log (_T("\n(c) 1995-2001 Bernd Schmidt   - Core UAE concept and implementation.")
		_T("\n(c) 1998-2019 Toni Wilen      - Win32 port, core code updates.")
		_T("\n(c) 1996-2001 Brian King      - Win32 port, Picasso96 RTG, and GUI.")
		_T("\n(c) 1996-1999 Mathias Ortmann - Win32 port and bsdsocket support.")
		_T("\n(c) 2000-2001 Bernd Meyer     - JIT engine.")
		_T("\n(c) 2000-2005 Bernd Roesch    - MIDI input, many fixes.")
		_T("\nPress F12 to show the Settings Dialog (GUI), Alt-F4 to quit.")
		_T("\nEnd+F1 changes floppy 0, End+F2 changes floppy 1, etc.")
		_T("\n"));
	tmp[0] = 0;
	GetModuleFileName (NULL, tmp, sizeof tmp / sizeof (TCHAR));
	write_log (_T("'%s'\n"), tmp);
	write_log (_T("EXE: '%s', DATA: '%s', PLUGIN: '%s'\n"), start_path_exe, start_path_data, start_path_plugins);
	regstatus ();
}

void logging_cleanup (void)
{
	if (debugfile)
		fclose (debugfile);
	debugfile = 0;
}

uae_u8 *save_log (int bootlog, int *len)
{
	FILE *f;
	uae_u8 *dst = NULL;
	int size;

	if (!logging_started)
		return NULL;
	f = _tfopen (bootlog ? LOG_BOOT : LOG_NORMAL, _T("rb"));
	if (!f)
		return NULL;
	fseek (f, 0, SEEK_END);
	size = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (*len > 0 && size > *len)
		size = *len;
	if (size > 0) {
		dst = xcalloc (uae_u8, size + 1);
		if (dst)
			fread (dst, 1, size, f);
		fclose (f);
		*len = size + 1;
	}
	return dst;
}

void stripslashes (TCHAR *p)
{
	while (_tcslen (p) > 0 && (p[_tcslen (p) - 1] == '\\' || p[_tcslen (p) - 1] == '/'))
		p[_tcslen (p) - 1] = 0;
}
void fixtrailing (TCHAR *p)
{
	if (_tcslen(p) == 0)
		return;
	if (p[_tcslen(p) - 1] == '/' || p[_tcslen(p) - 1] == '\\')
		return;
	_tcscat(p, _T("\\"));
}
static void fixdriveletter(TCHAR *path)
{
	if (_istalpha(path[0]) && path[1] == ':' && path[2] == '\\' && path[3] == '.' && path[4] == 0)
		path[3] = 0;
	if (_istalpha(path[0]) && path[1] == ':' && path[2] == '\\' && path[3] == '.' && path[4] == '.' && path[5] == 0)
		path[3] = 0;
}
// convert path to absolute or relative
void fullpath(TCHAR *path, int size, bool userelative)
{
	if (path[0] == 0 || (path[0] == '\\' && path[1] == '\\') || path[0] == ':')
		return;
	// has one or more environment variables? do nothing.
	if (_tcschr(path, '%'))
		return;
	if (_tcslen(path) >= 2 && path[_tcslen(path) - 1] == '.')
		return;
	/* <drive letter>: is supposed to mean same as <drive letter>:\ */
	if (_istalpha (path[0]) && path[1] == ':' && path[2] == 0)
		_tcscat (path, _T("\\"));
	if (userelative) {
		TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH];
		tmp1[0] = 0;
		GetCurrentDirectory (sizeof tmp1 / sizeof (TCHAR), tmp1);
		fixtrailing (tmp1);
		tmp2[0] = 0;
		int ret = GetFullPathName (path, sizeof tmp2 / sizeof (TCHAR), tmp2, NULL);
		if (ret == 0 || ret >= sizeof tmp2 / sizeof (TCHAR))
			return;
		if (_tcslen(tmp1) > 2 && _tcsnicmp(tmp1, tmp2, 3) == 0 && tmp1[1] == ':' && tmp1[2] == '\\') {
			// same drive letter
			path[0] = 0;
			if (PathRelativePathTo(path, tmp1, FILE_ATTRIBUTE_DIRECTORY, tmp2, tmp2[_tcslen(tmp2) - 1] == '\\' ? FILE_ATTRIBUTE_DIRECTORY : 0)) {
				if (path[0]) {
					if (path[0] == '.' && path[1] == 0) {
						_tcscpy(path, _T(".\\"));
					} else if (path[0] == '\\') {
						_tcscpy(tmp1, path + 1);
						_stprintf(path, _T(".\\%s"), tmp1);
					} else if (path[0] != '.') {
						_tcscpy(tmp1, path);
						_stprintf(path, _T(".\\%s"), tmp1);
					}
				} else {
					_tcscpy (path, tmp2);		
				}
				goto done;
			}
		}
		if (_tcsnicmp (tmp1, tmp2, _tcslen (tmp1)) == 0) {
			// tmp2 is inside tmp1
			_tcscpy (path, _T(".\\"));
			_tcscat (path, tmp2 + _tcslen (tmp1));
		} else {
			_tcscpy (path, tmp2);
		}
done:;
	} else {
		TCHAR tmp[MAX_DPATH];
		_tcscpy(tmp, path);
		DWORD err = GetFullPathName (tmp, size, path, NULL);
	}
}

void fullpath(TCHAR *path, int size)
{
	fullpath(path, size, relativepaths);
}
bool samepath(const TCHAR *p1, const TCHAR *p2)
{
	if (!_tcsicmp(p1, p2))
		return true;
	TCHAR path1[MAX_DPATH], path2[MAX_DPATH];
	_tcscpy(path1, p1);
	_tcscpy(path2, p2);
	fixdriveletter(path1);
	fixdriveletter(path2);
	if (!_tcsicmp(path1, path2))
		return true;
	return false;
}

bool target_isrelativemode(void)
{
	return relativepaths != 0;
}

void getpathpart (TCHAR *outpath, int size, const TCHAR *inpath)
{
	_tcscpy (outpath, inpath);
	TCHAR *p = _tcsrchr (outpath, '\\');
	if (p)
		p[0] = 0;
	fixtrailing (outpath);
}
void getfilepart (TCHAR *out, int size, const TCHAR *path)
{
	out[0] = 0;
	const TCHAR *p = _tcsrchr (path, '\\');
	if (p)
		_tcscpy (out, p + 1);
	else
		_tcscpy (out, path);
}

typedef DWORD (STDAPICALLTYPE *PFN_GetKey)(LPVOID lpvBuffer, DWORD dwSize);
uae_u8 *target_load_keyfile (struct uae_prefs *p, const TCHAR *path, int *sizep, TCHAR *name)
{
	uae_u8 *keybuf = NULL;
	HMODULE h;
	PFN_GetKey pfnGetKey;
	int size;
	TCHAR *libname = _T("amigaforever.dll");

	h = WIN32_LoadLibrary(libname);
	if (!h) {
		TCHAR path[MAX_DPATH];
		_stprintf (path, _T("%s..\\Player\\%s"), start_path_exe, libname);
		h = WIN32_LoadLibrary(path);
		if (!h) {
			TCHAR *afr = _wgetenv (_T("AMIGAFOREVERROOT"));
			if (afr) {
				TCHAR tmp[MAX_DPATH];
				_tcscpy (tmp, afr);
				fixtrailing (tmp);
				_stprintf (path, _T("%sPlayer\\%s"), tmp, libname);
				h = WIN32_LoadLibrary(path);
			}
		}
	}
	if (!h)
		return NULL;
	GetModuleFileName (h, name, MAX_DPATH);
	//write_log (_T("keydll: %s'\n"), name);
	pfnGetKey = (PFN_GetKey)GetProcAddress (h, "GetKey");
	//write_log (_T("addr: %08x\n"), pfnGetKey);
	if (pfnGetKey) {
		size = pfnGetKey (NULL, 0);
		*sizep = size;
		//write_log (_T("size: %d\n"), size);
		if (size > 0) {
			int gotsize;
			keybuf = xmalloc (uae_u8, size);
			gotsize = pfnGetKey (keybuf, size);
			//write_log (_T("gotsize: %d\n"), gotsize);
			if (gotsize != size) {
				xfree (keybuf);
				keybuf = NULL;
			}
		}
	}
	FreeLibrary (h);
	//write_log (_T("keybuf=%08x\n"), keybuf);
	return keybuf;
}

/***
*static void parse_cmdline(cmdstart, argv, args, numargs, numchars)
*
*Purpose:
*       Parses the command line and sets up the argv[] array.
*       On entry, cmdstart should point to the command line,
*       argv should point to memory for the argv array, args
*       points to memory to place the text of the arguments.
*       If these are NULL, then no storing (only counting)
*       is done.  On exit, *numargs has the number of
*       arguments (plus one for a final NULL argument),
*       and *numchars has the number of bytes used in the buffer
*       pointed to by args.
*
*Entry:
*       _TSCHAR *cmdstart - pointer to command line of the form
*           <progname><nul><args><nul>
*       _TSCHAR **argv - where to build argv array; NULL means don't
*                       build array
*       _TSCHAR *args - where to place argument text; NULL means don't
*                       store text
*
*Exit:
*       no return value
*       int *numargs - returns number of argv entries created
*       int *numchars - number of characters used in args buffer
*
*Exceptions:
*
*******************************************************************************/

#define NULCHAR    _T('\0')
#define SPACECHAR  _T(' ')
#define TABCHAR    _T('\t')
#define DQUOTECHAR _T('\"')
#define SLASHCHAR  _T('\\')


static void __cdecl wparse_cmdline (
	const _TSCHAR *cmdstart,
	_TSCHAR **argv,
	_TSCHAR *args,
	int *numargs,
	int *numchars
	)
{
	const _TSCHAR *p;
	_TUCHAR c;
	int inquote;                    /* 1 = inside quotes */
	int copychar;                   /* 1 = copy char to *args */
	unsigned numslash;              /* num of backslashes seen */

	*numchars = 0;
	*numargs = 1;                   /* the program name at least */

	/* first scan the program name, copy it, and count the bytes */
	p = cmdstart;
	if (argv)
		*argv++ = args;

#ifdef WILDCARD
	/* To handle later wild card expansion, we prefix each entry by
	it's first character before quote handling.  This is done
	so _[w]cwild() knows whether to expand an entry or not. */
	if (args)
		*args++ = *p;
	++*numchars;

#endif  /* WILDCARD */

	/* A quoted program name is handled here. The handling is much
	simpler than for other arguments. Basically, whatever lies
	between the leading double-quote and next one, or a terminal null
	character is simply accepted. Fancier handling is not required
	because the program name must be a legal NTFS/HPFS file name.
	Note that the double-quote characters are not copied, nor do they
	contribute to numchars. */
	inquote = FALSE;
	do {
		if (*p == DQUOTECHAR )
		{
			inquote = !inquote;
			c = (_TUCHAR) *p++;
			continue;
		}
		++*numchars;
		if (args)
			*args++ = *p;

		c = (_TUCHAR) *p++;
#ifdef _MBCS
		if (_ismbblead(c)) {
			++*numchars;
			if (args)
				*args++ = *p;   /* copy 2nd byte too */
			p++;  /* skip over trail byte */
		}
#endif  /* _MBCS */

	} while ( (c != NULCHAR && (inquote || (c != SPACECHAR && c != TABCHAR))) );

	if ( c == NULCHAR ) {
		p--;
	} else {
		if (args)
			*(args-1) = NULCHAR;
	}

	inquote = 0;

	/* loop on each argument */
	for(;;) {

		if ( *p ) {
			while (*p == SPACECHAR || *p == TABCHAR)
				++p;
		}

		if (*p == NULCHAR)
			break;              /* end of args */

		/* scan an argument */
		if (argv)
			*argv++ = args;     /* store ptr to arg */
		++*numargs;

#ifdef WILDCARD
		/* To handle later wild card expansion, we prefix each entry by
		it's first character before quote handling.  This is done
		so _[w]cwild() knows whether to expand an entry or not. */
		if (args)
			*args++ = *p;
		++*numchars;

#endif  /* WILDCARD */

		/* loop through scanning one argument */
		for (;;) {
			copychar = 1;
			/* Rules: 2N backslashes + " ==> N backslashes and begin/end quote
			2N+1 backslashes + " ==> N backslashes + literal "
			N backslashes ==> N backslashes */
			numslash = 0;
			while (*p == SLASHCHAR) {
				/* count number of backslashes for use below */
				++p;
				++numslash;
			}
			if (*p == DQUOTECHAR) {
				/* if 2N backslashes before, start/end quote, otherwise
				copy literally */
				if (numslash % 2 == 0) {
					if (inquote && p[1] == DQUOTECHAR) {
						p++;    /* Double quote inside quoted string */
					} else {    /* skip first quote char and copy second */
						copychar = 0;       /* don't copy quote */
						inquote = !inquote;
					}
				}
				numslash /= 2;          /* divide numslash by two */
			}

			/* copy slashes */
			while (numslash--) {
				if (args)
					*args++ = SLASHCHAR;
				++*numchars;
			}

			/* if at end of arg, break loop */
			if (*p == NULCHAR || (!inquote && (*p == SPACECHAR || *p == TABCHAR)))
				break;

			/* copy character into argument */
#ifdef _MBCS
			if (copychar) {
				if (args) {
					if (_ismbblead(*p)) {
						*args++ = *p++;
						++*numchars;
					}
					*args++ = *p;
				} else {
					if (_ismbblead(*p)) {
						++p;
						++*numchars;
					}
				}
				++*numchars;
			}
			++p;
#else  /* _MBCS */
			if (copychar) {
				if (args)
					*args++ = *p;
				++*numchars;
			}
			++p;
#endif  /* _MBCS */
		}

		/* null-terminate the argument */

		if (args)
			*args++ = NULCHAR;          /* terminate string */
		++*numchars;
	}

	/* We put one last argument in -- a null ptr */
	if (argv)
		*argv++ = NULL;
	++*numargs;
}

#define MAX_ARGUMENTS 128

static TCHAR **parseargstring (const TCHAR *s)
{
	TCHAR **p;
	int numa, numc;

	if (_tcslen (s) == 0)
		return NULL;
	wparse_cmdline (s, NULL, NULL, &numa, &numc);
	numa++;
	p = (TCHAR**)xcalloc (uae_u8, numa * sizeof (TCHAR*) + numc * sizeof (TCHAR));
	wparse_cmdline (s, (wchar_t **)p, (wchar_t *)(((char *)p) + numa * sizeof(wchar_t *)), &numa, &numc);
	if (numa > MAX_ARGUMENTS) {
		p[MAX_ARGUMENTS] = NULL;
		numa = MAX_ARGUMENTS;
	}
	TCHAR **dstp = xcalloc (TCHAR*, MAX_ARGUMENTS + 1);
	for (int i = 0; p[i]; i++)
		dstp[i] = my_strdup (p[i]);
	xfree (p);
	return dstp;
}


static void shellexecute (const TCHAR *command)
{
	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	TCHAR **arg;
	int i, j, k, stop;

	if (_tcslen (command) == 0)
		return;
	i = j = 0;
	stop = 0;
	arg = parseargstring (command);
	while (!stop) {
		TCHAR *cmd, *exec;
		int len = 1;
		j = i;
		while (arg[i] && _tcscmp (arg[i], L";")) {
			len += _tcslen (arg[i]) + 3;
			i++;
		}
		exec = NULL;
		cmd = xcalloc (TCHAR, len);
		for (k = j; k < i; k++) {
			int quote = 0;
			if (_tcslen (cmd) > 0)
				_tcscat (cmd, L" ");
			if (_tcschr (arg[k], ' '))
				quote = 1;
			if (quote)
				_tcscat (cmd, L"\"");
			_tcscat (cmd, arg[k]);
			if (quote)
				_tcscat (cmd, _T("\""));
			if (!exec && !_tcsicmp (cmd, _T("cmd.exe"))) {
				int size;
				size = GetEnvironmentVariable (_T("ComSpec"), NULL, 0);
				if (size > 0) {
					exec = xcalloc (TCHAR, size + 1);
					GetEnvironmentVariable (_T("ComSpec"), exec, size);
				}
				cmd[0] = 0;
			}
		}
		if (arg[i++] == 0)
			stop = 1;
		si.cb = sizeof si;
		//si.wShowWindow = SW_HIDE;
		//si.dwFlags = STARTF_USESHOWWINDOW;
		if (CreateProcess (exec,
			cmd,
			NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
				WaitForSingleObject (pi.hProcess, INFINITE);
				CloseHandle (pi.hProcess);
				CloseHandle (pi.hThread);
		} else {
			write_log (_T("CreateProcess('%s' '%s') failed, %d\n"),
				exec, cmd, GetLastError ());
		}
		xfree (exec);
		xfree (cmd);
	}
	for (i = 0; arg && arg[i]; i++)
		xfree (arg[i]);
	xfree (arg);
}

void target_run (void)
{
	shellexecute (currprefs.win32_commandpathstart);
}
void target_quit (void)
{
	shellexecute (currprefs.win32_commandpathend);
}

void target_fixup_options (struct uae_prefs *p)
{
	if (p->win32_automount_cddrives && !p->scsi)
		p->scsi = UAESCSI_SPTI;
	if (p->scsi > UAESCSI_LAST)
		p->scsi = UAESCSI_SPTI;
	bool paused = false;
	bool nosound = false;
	bool nojoy = true;
	if (!paused) {
		paused = p->win32_active_nocapture_pause;
		nosound = p->win32_active_nocapture_nosound;
	} else {
		p->win32_active_nocapture_pause = p->win32_active_nocapture_nosound = true;
		nosound = true;
		nojoy = false;
	}
	if (!paused) {
		paused = p->win32_inactive_pause;
		nosound = p->win32_inactive_nosound;
		nojoy = (p->win32_inactive_input & 4) == 0;
	} else {
		p->win32_inactive_pause = p->win32_inactive_nosound = true;
		p->win32_inactive_input = 0;
		nosound = true;
		nojoy = true;
	}
	
	struct MultiDisplay *md = getdisplay(p, 0);
	for (int j = 0; j < MAX_AMIGADISPLAYS; j++) {
		if (p->gfx_monitor[j].gfx_size_fs.special == WH_NATIVE) {
			int i;
			for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
				if (md->DisplayModes[i].res.width == md->rect.right - md->rect.left &&
					md->DisplayModes[i].res.height == md->rect.bottom - md->rect.top) {
					p->gfx_monitor[j].gfx_size_fs.width = md->DisplayModes[i].res.width;
					p->gfx_monitor[j].gfx_size_fs.height = md->DisplayModes[i].res.height;
					write_log(_T("Native resolution: %dx%d\n"), p->gfx_monitor[j].gfx_size_fs.width, p->gfx_monitor[j].gfx_size_fs.height);
					break;
				}
			}
			if (md->DisplayModes[i].depth < 0) {
				p->gfx_monitor[j].gfx_size_fs.special = 0;
				write_log(_T("Native resolution not found.\n"));
			}
		}
	}
	/* switch from 32 to 16 or vice versa if mode does not exist */
	if (1 || isfullscreen() > 0) {
		int depth = p->color_mode == 5 ? 4 : 2;
		for (int i = 0; md->DisplayModes[i].depth >= 0; i++) {
			if (md->DisplayModes[i].depth == depth) {
				depth = 0;
				break;
			}
		}
		if (depth) {
			p->color_mode = p->color_mode == 5 ? 2 : 5;
		}
	}
	if (p->rtg_hardwaresprite && !p->gfx_api) {
		error_log (_T("DirectDraw is not RTG hardware sprite compatible."));
		p->rtg_hardwaresprite = false;
	}
	if (p->rtgboards[0].rtgmem_type >= GFXBOARD_HARDWARE) {
		p->rtg_hardwareinterrupt = false;
		p->rtg_hardwaresprite = false;
		p->win32_rtgmatchdepth = false;
		if (gfxboard_need_byteswap (&p->rtgboards[0]))
			p->color_mode = 5;
		if (p->ppc_model && !p->gfx_api) {
			error_log (_T("Graphics board and PPC: Direct3D enabled."));
			p->gfx_api = os_win7 ? 2 : 1;
		}

	}
	if ((p->gfx_apmode[0].gfx_vsyncmode || p->gfx_apmode[1].gfx_vsyncmode) ) {
		if (p->produce_sound && sound_devices[p->win32_soundcard]->type == SOUND_DEVICE_DS) {
			p->win32_soundcard = 0;
		}
	}

	d3d_select(p);
}

void target_default_options (struct uae_prefs *p, int type)
{
	TCHAR buf[MAX_DPATH];
	if (type == 2 || type == 0 || type == 3) {
		p->win32_logfile = 0;
		p->win32_active_nocapture_pause = 0;
		p->win32_active_nocapture_nosound = 0;
		p->win32_iconified_nosound = 1;
		p->win32_iconified_pause = 1;
		p->win32_iconified_input = 0;
		p->win32_inactive_nosound = 0;
		p->win32_inactive_pause = 0;
		p->win32_inactive_input = 0;
		p->win32_ctrl_F11_is_quit = 0;
		p->win32_soundcard = 0;
		p->win32_samplersoundcard = -1;
		p->win32_minimize_inactive = 0;
		p->win32_capture_always = false;
		p->win32_start_minimized = false;
		p->win32_start_uncaptured = false;
		p->win32_active_capture_priority = 1;
		//p->win32_active_nocapture_priority = 1;
		p->win32_inactive_priority = 2;
		p->win32_iconified_priority = 3;
		p->win32_notaskbarbutton = false;
		p->win32_nonotificationicon = false;
		p->win32_main_alwaysontop = false;
		p->win32_gui_alwaysontop = false;
		p->win32_guikey = -1;
		p->win32_automount_removable = 0;
		p->win32_automount_drives = 0;
		p->win32_automount_removabledrives = 0;
		p->win32_automount_cddrives = 0;
		p->win32_automount_netdrives = 0;
		p->win32_kbledmode = 1;
		p->win32_uaescsimode = UAESCSI_CDEMU;
		p->win32_borderless = 0;
		p->win32_blankmonitors = false;
		p->win32_powersavedisabled = true;
		p->sana2 = 0;
		p->win32_rtgmatchdepth = 1;
		p->gf[APMODE_RTG].gfx_filter_autoscale = RTG_MODE_SCALE;
		p->win32_rtgallowscaling = 0;
		p->win32_rtgscaleaspectratio = -1;
		p->win32_rtgvblankrate = 0;
		p->rtg_hardwaresprite = true;
		p->win32_commandpathstart[0] = 0;
		p->win32_commandpathend[0] = 0;
		p->win32_statusbar = 1;
		p->gfx_api = os_win7 ? 2 : (os_vista ? 1 : 0);
		if (p->gfx_api > 1)
			p->color_mode = 5;
		if (p->gf[APMODE_NATIVE].gfx_filter == 0 && p->gfx_api)
			p->gf[APMODE_NATIVE].gfx_filter = 1;
		if (p->gf[APMODE_RTG].gfx_filter == 0 && p->gfx_api)
			p->gf[APMODE_RTG].gfx_filter = 1;
		WIN32GUI_LoadUIString (IDS_INPUT_CUSTOM, buf, sizeof buf / sizeof (TCHAR));
		for (int i = 0; i < GAMEPORT_INPUT_SETTINGS; i++)
			_stprintf (p->input_config_name[i], buf, i + 1);
		p->aviout_xoffset = -1;
		p->aviout_yoffset = -1;
	}
	if (type == 1 || type == 0 || type == 3) {
		p->win32_uaescsimode = UAESCSI_CDEMU;
		p->win32_midioutdev = -2;
		p->win32_midiindev = 0;
		p->win32_midirouter = false;
		p->win32_automount_removable = 0;
		p->win32_automount_drives = 0;
		p->win32_automount_removabledrives = 0;
		p->win32_automount_cddrives = 0;
		p->win32_automount_netdrives = 0;
		p->picasso96_modeflags = RGBFF_CLUT | RGBFF_R5G6B5PC | RGBFF_B8G8R8A8;
		p->win32_filesystem_mangle_reserved_names = true;
	}
}

static const TCHAR *scsimode[] = { _T("SCSIEMU"), _T("SPTI"), _T("SPTI+SCSISCAN"), NULL };
static const TCHAR *statusbarmode[] = { _T("none"), _T("normal"), _T("extended"), NULL };
static const TCHAR *configmult[] = { _T("1x"), _T("2x"), _T("3x"), _T("4x"), _T("5x"), _T("6x"), _T("7x"), _T("8x"), NULL };

static struct midiportinfo *getmidiport (struct midiportinfo **mi, int devid)
{
	for (int i = 0; i < MAX_MIDI_PORTS; i++) {
		if (mi[i] != NULL && mi[i]->devid == devid)
			return mi[i];
	}
	return NULL;
}

extern int scsiromselected;

void target_save_options (struct zfile *f, struct uae_prefs *p)
{
	struct midiportinfo *midp;

	cfgfile_target_write_bool (f, _T("middle_mouse"), (p->input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) != 0);
	cfgfile_target_dwrite_bool (f, _T("logfile"), p->win32_logfile);
	cfgfile_target_dwrite_bool (f, _T("map_drives"), p->win32_automount_drives);
	cfgfile_target_dwrite_bool (f, _T("map_drives_auto"), p->win32_automount_removable);
	cfgfile_target_dwrite_bool (f, _T("map_cd_drives"), p->win32_automount_cddrives);
	cfgfile_target_dwrite_bool (f, _T("map_net_drives"), p->win32_automount_netdrives);
	cfgfile_target_dwrite_bool (f, _T("map_removable_drives"), p->win32_automount_removabledrives);
	serdevtoname (p->sername);
	cfgfile_target_dwrite_str (f, _T("serial_port"), p->sername[0] ? p->sername : _T("none"));
	sernametodev (p->sername);
	cfgfile_target_dwrite_str (f, _T("parallel_port"), p->prtname[0] ? p->prtname : _T("none"));

	cfgfile_target_dwrite (f, _T("active_priority"), _T("%d"), priorities[p->win32_active_capture_priority].value);
#if 0
	cfgfile_target_dwrite (f, _T("active_not_captured_priority"), _T("%d"), priorities[p->win32_active_nocapture_priority].value);
#endif
	cfgfile_target_dwrite_bool(f, _T("active_not_captured_nosound"), p->win32_active_nocapture_nosound);
	cfgfile_target_dwrite_bool(f, _T("active_not_captured_pause"), p->win32_active_nocapture_pause);
	cfgfile_target_dwrite(f, _T("inactive_priority"), _T("%d"), priorities[p->win32_inactive_priority].value);
	cfgfile_target_dwrite_bool(f, _T("inactive_nosound"), p->win32_inactive_nosound);
	cfgfile_target_dwrite_bool(f, _T("inactive_pause"), p->win32_inactive_pause);
	cfgfile_target_dwrite(f, _T("inactive_input"), _T("%d"), p->win32_inactive_input);
	cfgfile_target_dwrite(f, _T("iconified_priority"), _T("%d"), priorities[p->win32_iconified_priority].value);
	cfgfile_target_dwrite_bool(f, _T("iconified_nosound"), p->win32_iconified_nosound);
	cfgfile_target_dwrite_bool(f, _T("iconified_pause"), p->win32_iconified_pause);
	cfgfile_target_dwrite(f, _T("iconified_input"), _T("%d"), p->win32_iconified_input);
	cfgfile_target_dwrite_bool(f, _T("inactive_iconify"), p->win32_minimize_inactive);
	cfgfile_target_dwrite_bool(f, _T("active_capture_automatically"), p->win32_capture_always);
	cfgfile_target_dwrite_bool(f, _T("start_iconified"), p->win32_start_minimized);
	cfgfile_target_dwrite_bool(f, _T("start_not_captured"), p->win32_start_uncaptured);

	cfgfile_target_dwrite_bool (f, _T("ctrl_f11_is_quit"), p->win32_ctrl_F11_is_quit);

	cfgfile_target_dwrite (f, _T("midiout_device"), _T("%d"), p->win32_midioutdev);
	cfgfile_target_dwrite (f, _T("midiin_device"), _T("%d"), p->win32_midiindev);

	midp = getmidiport (midioutportinfo, p->win32_midioutdev);
	if (p->win32_midioutdev < -1)
		cfgfile_target_dwrite_str (f, _T("midiout_device_name"), _T("none"));
	else if (p->win32_midioutdev == -1 || midp == NULL)
		cfgfile_target_dwrite_str (f, _T("midiout_device_name"), _T("default"));
	else
		cfgfile_target_dwrite_str (f, _T("midiout_device_name"), midp->name);

	midp = getmidiport (midiinportinfo, p->win32_midiindev);
	if (p->win32_midiindev < 0 || midp == NULL)
		cfgfile_target_dwrite_str (f, _T("midiin_device_name"), _T("none"));
	else
		cfgfile_target_dwrite_str (f, _T("midiin_device_name"), midp->name);
	cfgfile_target_dwrite_bool (f, _T("midirouter"), p->win32_midirouter);
			
	cfgfile_target_dwrite_bool (f, _T("rtg_match_depth"), p->win32_rtgmatchdepth);
	cfgfile_target_dwrite_bool(f, _T("rtg_scale_small"), p->gf[1].gfx_filter_autoscale == 1);
	cfgfile_target_dwrite_bool(f, _T("rtg_scale_center"), p->gf[1].gfx_filter_autoscale == 2);
	cfgfile_target_dwrite_bool (f, _T("rtg_scale_allow"), p->win32_rtgallowscaling);
	cfgfile_target_dwrite (f, _T("rtg_scale_aspect_ratio"), _T("%d:%d"),
		p->win32_rtgscaleaspectratio >= 0 ? (p->win32_rtgscaleaspectratio / ASPECTMULT) : -1,
		p->win32_rtgscaleaspectratio >= 0 ? (p->win32_rtgscaleaspectratio & (ASPECTMULT - 1)) : -1);
	if (p->win32_rtgvblankrate <= 0)
		cfgfile_target_dwrite_str (f, _T("rtg_vblank"), p->win32_rtgvblankrate == -1 ? _T("real") : (p->win32_rtgvblankrate == -2 ? _T("disabled") : _T("chipset")));
	else
		cfgfile_target_dwrite (f, _T("rtg_vblank"), _T("%d"), p->win32_rtgvblankrate);
	cfgfile_target_dwrite_bool (f, _T("borderless"), p->win32_borderless);
	cfgfile_target_dwrite_bool (f, _T("blank_monitors"), p->win32_blankmonitors);
	cfgfile_target_dwrite_str (f, _T("uaescsimode"), scsimode[p->win32_uaescsimode]);
	cfgfile_target_dwrite_str (f, _T("statusbar"), statusbarmode[p->win32_statusbar]);
	cfgfile_target_write (f, _T("soundcard"), _T("%d"), p->win32_soundcard);
	if (p->win32_soundcard >= 0 && p->win32_soundcard < MAX_SOUND_DEVICES && sound_devices[p->win32_soundcard])
		cfgfile_target_write_str (f, _T("soundcardname"), sound_devices[p->win32_soundcard]->cfgname);
	if (p->win32_samplersoundcard >= 0 && p->win32_samplersoundcard < MAX_SOUND_DEVICES) {
		cfgfile_target_write (f, _T("samplersoundcard"), _T("%d"), p->win32_samplersoundcard);
		if (record_devices[p->win32_samplersoundcard])
			cfgfile_target_write_str (f, _T("samplersoundcardname"), record_devices[p->win32_samplersoundcard]->cfgname);
	}

	cfgfile_target_dwrite (f, _T("cpu_idle"), _T("%d"), p->cpu_idle);
	cfgfile_target_dwrite_bool (f, _T("notaskbarbutton"), p->win32_notaskbarbutton);
	cfgfile_target_dwrite_bool (f, _T("nonotificationicon"), p->win32_nonotificationicon);
	cfgfile_target_dwrite_bool (f, _T("always_on_top"), p->win32_main_alwaysontop);
	cfgfile_target_dwrite_bool (f, _T("gui_always_on_top"), p->win32_gui_alwaysontop);
	cfgfile_target_dwrite_bool (f, _T("no_recyclebin"), p->win32_norecyclebin);
	if (p->win32_guikey >= 0)
		cfgfile_target_dwrite (f, _T("guikey"), _T("0x%x"), p->win32_guikey);
	cfgfile_target_dwrite (f, _T("kbledmode"), _T("%d"), p->win32_kbledmode);
	cfgfile_target_dwrite_bool (f, _T("powersavedisabled"), p->win32_powersavedisabled);
	cfgfile_target_dwrite_str (f, _T("exec_before"), p->win32_commandpathstart);
	cfgfile_target_dwrite_str (f, _T("exec_after"), p->win32_commandpathend);
	cfgfile_target_dwrite_str (f, _T("parjoyport0"), p->win32_parjoyport0);
	cfgfile_target_dwrite_str (f, _T("parjoyport1"), p->win32_parjoyport1);
	cfgfile_target_dwrite_str (f, _T("gui_page"), p->win32_guipage);
	cfgfile_target_dwrite_str (f, _T("gui_active_page"), p->win32_guiactivepage);
	cfgfile_target_dwrite_bool(f, _T("filesystem_mangle_reserved_names"), p->win32_filesystem_mangle_reserved_names);
	cfgfile_target_dwrite_bool(f, _T("right_control_is_right_win"), p->right_control_is_right_win_key);
	cfgfile_target_dwrite_bool(f, _T("windows_shutdown_notification"), p->win32_shutdown_notification);
	cfgfile_target_dwrite_bool(f, _T("warn_exit"), p->win32_warn_exit);

	cfgfile_target_dwrite(f, _T("extraframewait"), _T("%d"), extraframewait);
	cfgfile_target_dwrite(f, _T("extraframewait_us"), _T("%d"), extraframewait2);
	cfgfile_target_dwrite (f, _T("framelatency"), _T("%d"), forcedframelatency);
	if (scsiromselected > 0)
		cfgfile_target_write(f, _T("expansion_gui_page"), expansionroms[scsiromselected].name);

	cfgfile_target_dwrite(f, _T("recording_width"), _T("%d"), p->aviout_width);
	cfgfile_target_dwrite(f, _T("recording_height"), _T("%d"), p->aviout_height);
	cfgfile_target_dwrite(f, _T("recording_x"), _T("%d"), p->aviout_xoffset);
	cfgfile_target_dwrite(f, _T("recording_y"), _T("%d"), p->aviout_yoffset);
	cfgfile_target_dwrite(f, _T("screenshot_width"), _T("%d"), p->screenshot_width);
	cfgfile_target_dwrite(f, _T("screenshot_height"), _T("%d"), p->screenshot_height);
	cfgfile_target_dwrite(f, _T("screenshot_x"), _T("%d"), p->screenshot_xoffset);
	cfgfile_target_dwrite(f, _T("screenshot_y"), _T("%d"), p->screenshot_yoffset);
	cfgfile_target_dwrite(f, _T("screenshot_min_width"), _T("%d"), p->screenshot_min_width);
	cfgfile_target_dwrite(f, _T("screenshot_min_height"), _T("%d"), p->screenshot_min_height);
	cfgfile_target_dwrite(f, _T("screenshot_max_width"), _T("%d"), p->screenshot_max_width);
	cfgfile_target_dwrite(f, _T("screenshot_max_height"), _T("%d"), p->screenshot_max_height);
	cfgfile_target_dwrite(f, _T("screenshot_output_width_limit"), _T("%d"), p->screenshot_output_width);
	cfgfile_target_dwrite(f, _T("screenshot_output_height_limit"), _T("%d"), p->screenshot_output_height);
	cfgfile_target_dwrite_str(f, _T("screenshot_mult_width"), configmult[p->screenshot_xmult]);
	cfgfile_target_dwrite_str(f, _T("screenshot_mult_height"), configmult[p->screenshot_ymult]);
}

void target_restart (void)
{
	gui_restart ();
}

static int fetchpri (int pri, int defpri)
{
	int i = 0;
	while (priorities[i].name) {
		if (priorities[i].value == pri)
			return i;
		i++;
	}
	return defpri;
}

TCHAR *target_expand_environment (const TCHAR *path, TCHAR *out, int maxlen)
{
	if (!path)
		return NULL;
	if (out == NULL) {
		int len = ExpandEnvironmentStrings (path, NULL, 0);
		if (len <= 0)
			return my_strdup (path);
		TCHAR *s = xmalloc (TCHAR, len + 1);
		ExpandEnvironmentStrings (path, s, len);
		return s;
	} else {
		if (ExpandEnvironmentStrings(path, out, maxlen) <= 0)
			_tcscpy(out, path);
		return out;
	}
}

static const TCHAR *obsolete[] = {
	_T("killwinkeys"), _T("sound_force_primary"), _T("iconified_highpriority"),
	_T("sound_sync"), _T("sound_tweak"), _T("directx6"), _T("sound_style"),
	_T("file_path"), _T("iconified_nospeed"), _T("activepriority"), _T("magic_mouse"),
	_T("filesystem_codepage"), _T("aspi"), _T("no_overlay"), _T("soundcard_exclusive"),
	_T("specialkey"), _T("sound_speed_tweak"), _T("sound_lag"),
	0
};

int target_parse_option (struct uae_prefs *p, const TCHAR *option, const TCHAR *value)
{
	TCHAR tmpbuf[CONFIG_BLEN];
	int i, v;
	bool tbool;

	if (cfgfile_yesno(option, value, _T("middle_mouse"), &tbool)) {
		if (tbool)
			p->input_mouse_untrap |= MOUSEUNTRAP_MIDDLEBUTTON;
		else
			p->input_mouse_untrap &= ~MOUSEUNTRAP_MIDDLEBUTTON;
		return 1;
	}

	if (cfgfile_yesno(option, value, _T("map_drives"), &p->win32_automount_drives)
		|| cfgfile_yesno(option, value, _T("map_drives_auto"), &p->win32_automount_removable)
		|| cfgfile_yesno(option, value, _T("map_cd_drives"), &p->win32_automount_cddrives)
		|| cfgfile_yesno(option, value, _T("map_net_drives"), &p->win32_automount_netdrives)
		|| cfgfile_yesno(option, value, _T("map_removable_drives"), &p->win32_automount_removabledrives)
		|| cfgfile_yesno(option, value, _T("logfile"), &p->win32_logfile)
		|| cfgfile_yesno(option, value, _T("networking"), &p->socket_emu)
		|| cfgfile_yesno(option, value, _T("borderless"), &p->win32_borderless)
		|| cfgfile_yesno(option, value, _T("blank_monitors"), &p->win32_blankmonitors)
		|| cfgfile_yesno(option, value, _T("active_not_captured_pause"), &p->win32_active_nocapture_pause)
		|| cfgfile_yesno(option, value, _T("active_not_captured_nosound"), &p->win32_active_nocapture_nosound)
		|| cfgfile_yesno(option, value, _T("inactive_pause"), &p->win32_inactive_pause)
		|| cfgfile_yesno(option, value, _T("inactive_nosound"), &p->win32_inactive_nosound)
		|| cfgfile_intval(option, value, _T("inactive_input"), &p->win32_inactive_input, 1)
		|| cfgfile_yesno(option, value, _T("iconified_pause"), &p->win32_iconified_pause)
		|| cfgfile_yesno(option, value, _T("iconified_nosound"), &p->win32_iconified_nosound)
		|| cfgfile_intval(option, value, _T("iconified_input"), &p->win32_iconified_input, 1)
		|| cfgfile_yesno(option, value, _T("ctrl_f11_is_quit"), &p->win32_ctrl_F11_is_quit)
		|| cfgfile_yesno(option, value, _T("no_recyclebin"), &p->win32_norecyclebin)
		|| cfgfile_intval(option, value, _T("midi_device"), &p->win32_midioutdev, 1)
		|| cfgfile_intval(option, value, _T("midiout_device"), &p->win32_midioutdev, 1)
		|| cfgfile_intval(option, value, _T("midiin_device"), &p->win32_midiindev, 1)
		|| cfgfile_yesno(option, value, _T("midirouter"), &p->win32_midirouter)
		|| cfgfile_intval(option, value, _T("samplersoundcard"), &p->win32_samplersoundcard, 1)
		|| cfgfile_yesno(option, value, _T("notaskbarbutton"), &p->win32_notaskbarbutton)
		|| cfgfile_yesno(option, value, _T("nonotificationicon"), &p->win32_nonotificationicon)
		|| cfgfile_yesno(option, value, _T("always_on_top"), &p->win32_main_alwaysontop)
		|| cfgfile_yesno(option, value, _T("gui_always_on_top"), &p->win32_gui_alwaysontop)
		|| cfgfile_yesno(option, value, _T("powersavedisabled"), &p->win32_powersavedisabled)
		|| cfgfile_string(option, value, _T("exec_before"), p->win32_commandpathstart, sizeof p->win32_commandpathstart / sizeof (TCHAR))
		|| cfgfile_string(option, value, _T("exec_after"), p->win32_commandpathend, sizeof p->win32_commandpathend / sizeof (TCHAR))
		|| cfgfile_string(option, value, _T("parjoyport0"), p->win32_parjoyport0, sizeof p->win32_parjoyport0 / sizeof (TCHAR))
		|| cfgfile_string(option, value, _T("parjoyport1"), p->win32_parjoyport1, sizeof p->win32_parjoyport1 / sizeof (TCHAR))
		|| cfgfile_string(option, value, _T("gui_page"), p->win32_guipage, sizeof p->win32_guipage / sizeof (TCHAR))
		|| cfgfile_string(option, value, _T("gui_active_page"), p->win32_guiactivepage, sizeof p->win32_guiactivepage / sizeof (TCHAR))
		|| cfgfile_intval(option, value, _T("guikey"), &p->win32_guikey, 1)
		|| cfgfile_intval(option, value, _T("kbledmode"), &p->win32_kbledmode, 1)
		|| cfgfile_yesno(option, value, _T("filesystem_mangle_reserved_names"), &p->win32_filesystem_mangle_reserved_names)
		|| cfgfile_yesno(option, value, _T("right_control_is_right_win"), &p->right_control_is_right_win_key)
		|| cfgfile_yesno(option, value, _T("windows_shutdown_notification"), &p->win32_shutdown_notification)
		|| cfgfile_yesno(option, value, _T("warn_exit"), &p->win32_warn_exit)
		|| cfgfile_intval(option, value, _T("extraframewait"), &extraframewait, 1)
		|| cfgfile_intval(option, value, _T("extraframewait_us"), &extraframewait2, 1)
		|| cfgfile_intval(option, value, _T("framelatency"), &forcedframelatency, 1)
		|| cfgfile_intval(option, value, _T("cpu_idle"), &p->cpu_idle, 1))
		return 1;

	if (cfgfile_intval(option, value, _T("recording_width"), &p->aviout_width, 1)
		|| cfgfile_intval(option, value, _T("recording_height"), &p->aviout_height, 1)
		|| cfgfile_intval(option, value, _T("recording_x"), &p->aviout_xoffset, 1)
		|| cfgfile_intval(option, value, _T("recording_y"), &p->aviout_yoffset, 1)

		|| cfgfile_intval(option, value, _T("screenshot_width"), &p->screenshot_width, 1)
		|| cfgfile_intval(option, value, _T("screenshot_height"), &p->screenshot_height, 1)
		|| cfgfile_intval(option, value, _T("screenshot_x"), &p->screenshot_xoffset, 1)
		|| cfgfile_intval(option, value, _T("screenshot_y"), &p->screenshot_yoffset, 1)
		|| cfgfile_intval(option, value, _T("screenshot_min_width"), &p->screenshot_min_width, 1)
		|| cfgfile_intval(option, value, _T("screenshot_min_height"), &p->screenshot_min_height, 1)
		|| cfgfile_intval(option, value, _T("screenshot_max_width"), &p->screenshot_max_width, 1)
		|| cfgfile_intval(option, value, _T("screenshot_max_height"), &p->screenshot_max_height, 1)
		|| cfgfile_intval(option, value, _T("screenshot_output_width_limit"), &p->screenshot_output_width, 1)
		|| cfgfile_intval(option, value, _T("screenshot_output_height_limit"), &p->screenshot_output_height, 1))

		return 1;

	if (cfgfile_strval(option, value, _T("screenshot_mult_width"), &p->screenshot_xmult, configmult, 0))
		return 1;
	if (cfgfile_strval(option, value, _T("screenshot_mult_height"), &p->screenshot_ymult, configmult, 0))
		return 1;
		
	if (cfgfile_string(option, value, _T("expansion_gui_page"), tmpbuf, sizeof tmpbuf / sizeof(TCHAR))) {
		TCHAR *p = _tcschr(tmpbuf, ',');
		if (p != NULL)
			*p = 0;
		for (int i = 0; expansionroms[i].name; i++) {
			if (!_tcsicmp(tmpbuf, expansionroms[i].name)) {
				scsiromselected = i;
				break;
			}
		}
		return 1;
	}

	if (cfgfile_yesno (option, value, _T("rtg_match_depth"), &p->win32_rtgmatchdepth))
		return 1;
	if (cfgfile_yesno (option, value, _T("rtg_scale_small"), &tbool)) {
		p->gf[1].gfx_filter_autoscale = tbool ? RTG_MODE_SCALE : 0;
		return 1;
	}
	if (cfgfile_yesno (option, value, _T("rtg_scale_center"), &tbool)) {
		if (tbool)
			p->gf[1].gfx_filter_autoscale = RTG_MODE_CENTER;
		return 1;
	}
	if (cfgfile_yesno (option, value, _T("rtg_scale_allow"), &p->win32_rtgallowscaling))
		return 1;

	if (cfgfile_intval (option, value, _T("soundcard"), &p->win32_soundcard, 1)) {
		if (p->win32_soundcard < 0 || p->win32_soundcard >= MAX_SOUND_DEVICES || sound_devices[p->win32_soundcard] == NULL)
			p->win32_soundcard = 0;
		return 1;
	}

	if (cfgfile_string (option, value, _T("soundcardname"), tmpbuf, sizeof tmpbuf / sizeof (TCHAR))) {
		int i, num;

		num = p->win32_soundcard;
		p->win32_soundcard = -1;
		for (i = 0; i < MAX_SOUND_DEVICES && sound_devices[i] ; i++) {
			if (i < num)
				continue;
			if (!_tcscmp (sound_devices[i]->cfgname, tmpbuf)) {
				p->win32_soundcard = i;
				break;
			}
		}
		if (p->win32_soundcard < 0) {
			for (i = 0; i < MAX_SOUND_DEVICES && sound_devices[i]; i++) {
				if (!_tcscmp (sound_devices[i]->cfgname, tmpbuf)) {
					p->win32_soundcard = i;
					break;
				}
			}
		}
		if (p->win32_soundcard < 0) {
			for (i = 0; i < MAX_SOUND_DEVICES && sound_devices[i]; i++) {
				if (!sound_devices[i]->prefix)
					continue;
				int prefixlen = _tcslen(sound_devices[i]->prefix);
				int tmplen = _tcslen(tmpbuf);
				if (prefixlen > 0 && tmplen >= prefixlen &&
					!_tcsncmp(sound_devices[i]->prefix, tmpbuf, prefixlen) &&
					((tmplen > prefixlen && tmpbuf[prefixlen] == ':')
					|| tmplen == prefixlen)) {
					p->win32_soundcard = i;
					break;
				}
			}

		}
		if (p->win32_soundcard < 0)
			p->win32_soundcard = num;
		return 1;
	}
	if (cfgfile_string (option, value, _T("samplersoundcardname"), tmpbuf, sizeof tmpbuf / sizeof (TCHAR))) {
		int i, num;

		num = p->win32_samplersoundcard;
		p->win32_samplersoundcard = -1;
		for (i = 0; i < MAX_SOUND_DEVICES && record_devices[i]; i++) {
			if (i < num)
				continue;
			if (!_tcscmp (record_devices[i]->cfgname, tmpbuf)) {
				p->win32_samplersoundcard = i;
				break;
			}
		}
		if (p->win32_samplersoundcard < 0) {
			for (i = 0; i < MAX_SOUND_DEVICES && record_devices[i]; i++) {
				if (!_tcscmp (record_devices[i]->cfgname, tmpbuf)) {
					p->win32_samplersoundcard = i;
					break;
				}
			}
		}
		return 1;
	}

	if (cfgfile_string (option, value, _T("rtg_vblank"), tmpbuf, sizeof tmpbuf / sizeof (TCHAR))) {
		if (!_tcscmp (tmpbuf, _T("real"))) {
			p->win32_rtgvblankrate = -1;
			return 1;
		}
		if (!_tcscmp (tmpbuf, _T("disabled"))) {
			p->win32_rtgvblankrate = -2;
			return 1;
		}
		if (!_tcscmp (tmpbuf, _T("chipset"))) {
			p->win32_rtgvblankrate = 0;
			return 1;
		}
		p->win32_rtgvblankrate = _tstol (tmpbuf);
		return 1;
	}

	if (cfgfile_string (option, value, _T("rtg_scale_aspect_ratio"), tmpbuf, sizeof tmpbuf / sizeof (TCHAR))) {
		int v1, v2;
		TCHAR *s;

		p->win32_rtgscaleaspectratio = -1;
		v1 = _tstol (tmpbuf);
		s = _tcschr (tmpbuf, ':');
		if (s) {
			v2 = _tstol (s + 1);
			if (v1 < 0 || v2 < 0)
				p->win32_rtgscaleaspectratio = -1;
			else if (v1 == 0 || v2 == 0)
				p->win32_rtgscaleaspectratio = 0;
			else
				p->win32_rtgscaleaspectratio = v1 * ASPECTMULT + v2;
		}
		return 1;
	}

	if (cfgfile_strval (option, value, _T("uaescsimode"), &p->win32_uaescsimode, scsimode, 0)) {
		// force SCSIEMU if pre 2.3 configuration
		if (p->config_version < ((2 << 16) | (3 << 8)))
			p->win32_uaescsimode = UAESCSI_CDEMU;
		return 1;
	}

	if (cfgfile_strval (option, value, _T("statusbar"), &p->win32_statusbar, statusbarmode, 0))
		return 1;

	if (cfgfile_intval (option, value, _T("active_priority"), &v, 1) || cfgfile_intval (option, value, _T("activepriority"), &v, 1)) {
		p->win32_active_capture_priority = fetchpri (v, 1);
		p->win32_active_nocapture_pause = false;
		p->win32_active_nocapture_nosound = false;
		return 1;
	}

#if 0
	if (cfgfile_intval (option, value, _T("active_not_captured_priority"), &v, 1)) {
		p->win32_active_nocapture_priority = fetchpri (v, 1);
		return 1;
	}
#endif

	if (cfgfile_yesno(option, value, _T("active_capture_automatically"), &p->win32_capture_always))
		return 1;

	if (cfgfile_intval (option, value, _T("inactive_priority"), &v, 1)) {
		p->win32_inactive_priority = fetchpri (v, 1);
		return 1;
	}
	if (cfgfile_intval (option, value, _T("iconified_priority"), &v, 1)) {
		p->win32_iconified_priority = fetchpri (v, 2);
		return 1;
	}
	
	if (cfgfile_yesno (option, value, _T("inactive_iconify"), &p->win32_minimize_inactive))
		return 1;

	if (cfgfile_yesno (option, value, _T("start_iconified"), &p->win32_start_minimized))
		return 1;

	if (cfgfile_yesno (option, value, _T("start_not_captured"), &p->win32_start_uncaptured))
		return 1;

	if (cfgfile_string (option, value, _T("serial_port"), &p->sername[0], 256)) {
		sernametodev (p->sername);
		if (p->sername[0])
			p->use_serial = 1;
		else
			p->use_serial = 0;
		return 1;
	}

	if (cfgfile_string (option, value, _T("parallel_port"), &p->prtname[0], 256)) {
		if (!_tcscmp (p->prtname, _T("none")))
			p->prtname[0] = 0;
		if (!_tcscmp (p->prtname, _T("default"))) {
			p->prtname[0] = 0;
			DWORD size = 256;
			GetDefaultPrinter (p->prtname, &size);
		}
		return 1;
	}

	if (cfgfile_string (option, value, _T("midiout_device_name"), tmpbuf, 256)) {
		p->win32_midioutdev = -2;
		if (!_tcsicmp (tmpbuf, _T("default")) || (midioutportinfo[0] && !_tcsicmp (tmpbuf, midioutportinfo[0]->name)))
			p->win32_midioutdev = -1;
		for (int i = 0; i < MAX_MIDI_PORTS && midioutportinfo[i]; i++) {
			if (!_tcsicmp (midioutportinfo[i]->name, tmpbuf)) {
				p->win32_midioutdev = midioutportinfo[i]->devid;
			}
		}
		return 1;
	}
	if (cfgfile_string (option, value, _T("midiin_device_name"), tmpbuf, 256)) {
		p->win32_midiindev = -1;
		for (int i = 0; i < MAX_MIDI_PORTS && midiinportinfo[i]; i++) {
			if (!_tcsicmp (midiinportinfo[i]->name, tmpbuf)) {
				p->win32_midiindev = midiinportinfo[i]->devid;
			}
		}
		return 1;
	}

	i = 0;
	while (obsolete[i]) {
		if (!strcasecmp (obsolete[i], option)) {
			write_log (_T("obsolete config entry '%s'\n"), option);
			return 1;
		}
		i++;
	}

	return 0;
}

static void createdir (const TCHAR *path)
{
	CreateDirectory (path, NULL);
}

void fetch_saveimagepath (TCHAR *out, int size, int dir)
{
	fetch_path (_T("SaveimagePath"), out, size);
	if (dir) {
		out[_tcslen (out) - 1] = 0;
		createdir (out);
		fetch_path (_T("SaveimagePath"), out, size);
	}
}
void fetch_configurationpath (TCHAR *out, int size)
{
	fetch_path (_T("ConfigurationPath"), out, size);
}
void fetch_luapath (TCHAR *out, int size)
{
	fetch_path (_T("LuaPath"), out, size);
}
void fetch_screenshotpath (TCHAR *out, int size)
{
	fetch_path (_T("ScreenshotPath"), out, size);
}
void fetch_ripperpath (TCHAR *out, int size)
{
	fetch_path (_T("RipperPath"), out, size);
}
void fetch_statefilepath (TCHAR *out, int size)
{
	fetch_path (_T("StatefilePath"), out, size);
}
void fetch_inputfilepath (TCHAR *out, int size)
{
	fetch_path (_T("InputPath"), out, size);
}
void fetch_datapath (TCHAR *out, int size)
{
	fetch_path (NULL, out, size);
}
void fetch_rompath (TCHAR *out, int size)
{
	fetch_path (_T("KickstartPath"), out, size);
}
static int isfilesindir (const TCHAR *p)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
	TCHAR path[MAX_DPATH];
	int i = 0;
	DWORD v;

	v = GetFileAttributes (p);
	if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
		return 0;
	_tcscpy (path, p);
	_tcscat (path, _T("\\*.*"));
	h = FindFirstFile (path, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		for (i = 0; i < 3; i++) {
			if (!FindNextFile (h, &fd))
				break;
		}
		FindClose (h);
	}
	if (i == 3)
		return 1;
	return 0;
}

void fetch_path (const TCHAR *name, TCHAR *out, int size)
{
	int size2 = size;

	_tcscpy (out, start_path_data);
	if (!name) {
		fullpath (out, size);
		return;
	}
	if (!_tcscmp (name, _T("FloppyPath")))
		_tcscat (out, _T("..\\shared\\adf\\"));
	if (!_tcscmp (name, _T("CDPath")))
		_tcscat (out, _T("..\\shared\\cd\\"));
	if (!_tcscmp (name, _T("TapePath")))
		_tcscat (out, _T("..\\shared\\tape\\"));
	if (!_tcscmp (name, _T("hdfPath")))
		_tcscat (out, _T("..\\shared\\hdf\\"));
	if (!_tcscmp (name, _T("KickstartPath")))
		_tcscat (out, _T("..\\shared\\rom\\"));
	if (!_tcscmp (name, _T("ConfigurationPath")))
		_tcscat (out, _T("Configurations\\"));
	if (!_tcscmp (name, _T("LuaPath")))
		_tcscat (out, _T("lua\\"));
	if (!_tcscmp (name, _T("StatefilePath")))
		_tcscat (out, _T("Savestates\\"));
	if (!_tcscmp (name, _T("InputPath")))
		_tcscat (out, _T("Inputrecordings\\"));
	if (start_data >= 0)
		regquerystr (NULL, name, out, &size); 
	if (GetFileAttributes (out) == INVALID_FILE_ATTRIBUTES)
		_tcscpy (out, start_path_data);
#if 0
	if (out[0] == '\\' && (_tcslen (out) >= 2 && out[1] != '\\')) { /* relative? */
		_tcscpy (out, start_path_data);
		if (start_data >= 0) {
			size2 -= _tcslen (out);
			regquerystr (NULL, name, out, &size2);
		}
	}
#endif
	stripslashes (out);
	if (!_tcscmp (name, _T("KickstartPath"))) {
		DWORD v = GetFileAttributes (out);
		if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
			_tcscpy (out, start_path_data);
	}
	fixtrailing (out);
	fullpath (out, size2);
}

int get_rom_path (TCHAR *out, pathtype mode)
{
	TCHAR tmp[MAX_DPATH];

	tmp[0] = 0;
	switch (mode)
	{
	case PATH_TYPE_DEFAULT:
		{
			if (!_tcscmp (start_path_data, start_path_exe))
				_tcscpy (tmp, _T(".\\"));
			else
				_tcscpy (tmp, start_path_data);
			if (GetFileAttributes (tmp) != INVALID_FILE_ATTRIBUTES) {
				TCHAR tmp2[MAX_DPATH];
				_tcscpy (tmp2, tmp);
				_tcscat (tmp2, _T("rom"));
				if (GetFileAttributes (tmp2) != INVALID_FILE_ATTRIBUTES) {
					_tcscpy (tmp, tmp2);
				} else {
					_tcscpy (tmp2, tmp);
					_tcscpy (tmp2, _T("roms"));
					if (GetFileAttributes (tmp2) != INVALID_FILE_ATTRIBUTES) {
						_tcscpy (tmp, tmp2);
					} else {
						if (!get_rom_path (tmp, PATH_TYPE_NEWAF)) {
							if (!get_rom_path (tmp, PATH_TYPE_AMIGAFOREVERDATA)) {
								_tcscpy (tmp, start_path_data);
							}
						}
					}
				}
			}
		}
		break;
	case PATH_TYPE_NEWAF:
		{
			TCHAR tmp2[MAX_DPATH];
			_tcscpy (tmp2, start_path_new1);
			_tcscat (tmp2, _T("..\\system\\rom"));
			if (isfilesindir (tmp2)) {
				_tcscpy (tmp, tmp2);
				break;
			}
			_tcscpy (tmp2, start_path_new1);
			_tcscat (tmp2, _T("..\\shared\\rom"));
			if (isfilesindir (tmp2)) {
				_tcscpy (tmp, tmp2);
				break;
			}
		}
		break;
	case PATH_TYPE_AMIGAFOREVERDATA:
		{
			TCHAR tmp2[MAX_DPATH];
			_tcscpy (tmp2, start_path_new2);
			_tcscat (tmp2, _T("system\\rom"));
			if (isfilesindir (tmp2)) {
				_tcscpy (tmp, tmp2);
				break;
			}
			_tcscpy (tmp2, start_path_new2);
			_tcscat (tmp2, _T("shared\\rom"));
			if (isfilesindir (tmp2)) {
				_tcscpy (tmp, tmp2);
				break;
			}
		}
		break;
	default:
		return -1;
	}
	if (isfilesindir (tmp)) {
		_tcscpy (out, tmp);
		fixtrailing (out);
	}
	if (out[0]) {
		fullpath (out, MAX_DPATH);
	}
	return out[0] ? 1 : 0;
}

void set_path (const TCHAR *name, TCHAR *path, pathtype mode)
{
	TCHAR tmp[MAX_DPATH];

	if (!path) {
		if (!_tcscmp (start_path_data, start_path_exe))
			_tcscpy (tmp, _T(".\\"));
		else
			_tcscpy (tmp, start_path_data);
		if (!_tcscmp (name, _T("KickstartPath")))
			_tcscat (tmp, _T("Roms"));
		if (!_tcscmp (name, _T("ConfigurationPath")))
			_tcscat (tmp, _T("Configurations"));
		if (!_tcscmp (name, _T("LuaPath")))
			_tcscat (tmp, _T("lua"));
		if (!_tcscmp (name, _T("ScreenshotPath")))
			_tcscat (tmp, _T("Screenshots"));
		if (!_tcscmp (name, _T("StatefilePath")))
			_tcscat (tmp, _T("Savestates"));
		if (!_tcscmp (name, _T("SaveimagePath")))
			_tcscat (tmp, _T("SaveImages"));
		if (!_tcscmp (name, _T("InputPath")))
			_tcscat (tmp, _T("Inputrecordings"));
	} else {
		_tcscpy (tmp, path);
	}
	stripslashes (tmp);
	if (!_tcscmp (name, _T("KickstartPath"))) {
		DWORD v = GetFileAttributes (tmp);
		if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
			get_rom_path (tmp, PATH_TYPE_DEFAULT);
		if (mode == PATH_TYPE_NEWAF) {
			get_rom_path (tmp, PATH_TYPE_NEWAF);
		} else if (mode == PATH_TYPE_AMIGAFOREVERDATA) {
			get_rom_path (tmp, PATH_TYPE_AMIGAFOREVERDATA);
		}
	}
	fixtrailing (tmp);
	fullpath (tmp, sizeof tmp / sizeof (TCHAR));
	regsetstr (NULL, name, tmp);
}
void set_path (const TCHAR *name, TCHAR *path)
{
	set_path (name, path, PATH_TYPE_DEFAULT);
}

static void initpath (const TCHAR *name, TCHAR *path)
{
	if (regexists (NULL, name))
		return;
	set_path (name, NULL);
}

static void romlist_add2 (const TCHAR *path, struct romdata *rd)
{
	if (getregmode ()) {
		int ok = 0;
		TCHAR tmp[MAX_DPATH];
		if (path[0] == '/' || path[0] == '\\')
			ok = 1;
		if (_tcslen (path) > 1 && path[1] == ':')
			ok = 1;
		if (!ok) {
			_tcscpy (tmp, start_path_exe);
			_tcscat (tmp, path);
			romlist_add (tmp, rd);
			return;
		}
	}
	romlist_add (path, rd);
}

extern int scan_roms (HWND, int);
void read_rom_list (void)
{
	TCHAR tmp2[1000];
	int idx, idx2;
	UAEREG *fkey;
	TCHAR tmp[1000];
	int size, size2, exists;

	romlist_clear ();
	exists = regexiststree (NULL, _T("DetectedROMs"));
	fkey = regcreatetree (NULL, _T("DetectedROMs"));
	if (fkey == NULL)
		return;
	if (!exists || forceroms) {
		load_keyring (NULL, NULL);
		scan_roms (NULL, forceroms ? 0 : 1);
	}
	forceroms = 0;
	idx = 0;
	for (;;) {
		size = sizeof (tmp) / sizeof (TCHAR);
		size2 = sizeof (tmp2) / sizeof (TCHAR);
		if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
			break;
		if (_tcslen (tmp) == 7 || _tcslen (tmp) == 13) {
			int group = 0;
			int subitem = 0;
			idx2 = _tstol (tmp + 4);
			if (_tcslen (tmp) == 13) {
				group = _tstol (tmp + 8);
				subitem = _tstol (tmp + 11);
			}
			if (idx2 >= 0 && _tcslen (tmp2) > 0) {
				struct romdata *rd = getromdatabyidgroup (idx2, group, subitem);
				if (rd) {
					TCHAR *s = _tcschr (tmp2, '\"');
					if (s && _tcslen (s) > 1) {
						TCHAR *s2 = my_strdup (s + 1);
						s = _tcschr (s2, '\"');
						if (s)
							*s = 0;
						romlist_add2 (s2, rd);
						xfree (s2);
					} else {
						romlist_add2 (tmp2, rd);
					}
				}
			}
		}
		idx++;
	}
	romlist_add (NULL, NULL);
	regclosetree (fkey);
}

static int parseversion (TCHAR **vs)
{
	TCHAR tmp[10];
	int i;

	i = 0;
	while (**vs >= '0' && **vs <= '9') {
		if (i >= sizeof (tmp) / sizeof (TCHAR))
			return 0;
		tmp[i++] = **vs;
		(*vs)++;
	}
	if (**vs == '.')
		(*vs)++;
	tmp[i] = 0;
	return _tstol (tmp);
}

static int checkversion (TCHAR *vs)
{
	int ver;
	if (_tcslen (vs) < 10)
		return 0;
	if (_tcsncmp (vs, _T("WinUAE "), 7))
		return 0;
	vs += 7;
	ver = parseversion (&vs) << 16;
	ver |= parseversion (&vs) << 8;
	ver |= parseversion (&vs);
	if (ver >= ((UAEMAJOR << 16) | (UAEMINOR << 8) | UAESUBREV))
		return 0;
	return 1;
}

static int shell_deassociate (const TCHAR *extension)
{
	HKEY rkey;
	const TCHAR *progid = _T("WinUAE");
	int def = !_tcscmp (extension, _T(".uae"));
	TCHAR rpath1[MAX_DPATH], rpath2[MAX_DPATH], progid2[MAX_DPATH];
	UAEREG *fkey;

	if (extension == NULL || _tcslen (extension) < 1 || extension[0] != '.')
		return 0;
	_tcscpy (progid2, progid);
	_tcscat (progid2, extension);
	if (os_admin > 1)
		rkey = HKEY_LOCAL_MACHINE;
	else
		rkey = HKEY_CURRENT_USER;

	_tcscpy (rpath1, _T("Software\\Classes\\"));
	_tcscpy (rpath2, rpath1);
	_tcscat (rpath2, extension);
	RegDeleteKey (rkey, rpath2);
	_tcscpy (rpath2, rpath1);
	_tcscat (rpath2, progid);
	if (!def)
		_tcscat (rpath2, extension);
	SHDeleteKey (rkey, rpath2);
	fkey = regcreatetree (NULL, _T("FileAssociations"));
	regdelete (fkey, extension);
	regclosetree (fkey);
	return 1;
}

static int shell_associate_2 (const TCHAR *extension, TCHAR *shellcommand, TCHAR *command, struct contextcommand *cc, const TCHAR *perceivedtype,
	const TCHAR *description, const TCHAR *ext2, int icon)
{
	TCHAR rpath1[MAX_DPATH], rpath2[MAX_DPATH], progid2[MAX_DPATH];
	HKEY rkey, key1, key2;
	DWORD disposition;
	const TCHAR *progid = _T("WinUAE");
	int def = !_tcscmp (extension, _T(".uae"));
	const TCHAR *defprogid;
	UAEREG *fkey;

	if (!icon)
		icon = IDI_APPICON;

	_tcscpy (progid2, progid);
	_tcscat (progid2, ext2 ? ext2 : extension);
	if (os_admin > 1)
		rkey = HKEY_LOCAL_MACHINE;
	else
		rkey = HKEY_CURRENT_USER;
	defprogid = def ? progid : progid2;

	_tcscpy (rpath1, _T("Software\\Classes\\"));
	_tcscpy (rpath2, rpath1);
	_tcscat (rpath2, extension);
	if (RegCreateKeyEx (rkey, rpath2, 0, NULL, REG_OPTION_NON_VOLATILE,
		KEY_WRITE | KEY_READ, NULL, &key1, &disposition) == ERROR_SUCCESS) {
			RegSetValueEx (key1, _T(""), 0, REG_SZ, (CONST BYTE *)defprogid, (_tcslen (defprogid) + 1) * sizeof (TCHAR));
			if (perceivedtype)
				RegSetValueEx (key1, _T("PerceivedType"), 0, REG_SZ, (CONST BYTE *)perceivedtype, (_tcslen (perceivedtype) + 1) * sizeof (TCHAR));
			RegCloseKey (key1);
	}
	_tcscpy (rpath2, rpath1);
	_tcscat (rpath2, progid);
	if (!def)
		_tcscat (rpath2, ext2 ? ext2 : extension);
	if (description) {
		if (RegCreateKeyEx (rkey, rpath2, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ, NULL, &key1, &disposition) == ERROR_SUCCESS) {
			TCHAR tmp[MAX_DPATH];
			RegSetValueEx (key1, _T(""), 0, REG_SZ, (CONST BYTE *)description, (_tcslen (description) + 1) * sizeof (TCHAR));
			RegSetValueEx (key1, _T("AppUserModelID"), 0, REG_SZ, (CONST BYTE *)WINUAEAPPNAME, (_tcslen (WINUAEAPPNAME) + 1) * sizeof (TCHAR));
			_tcscpy (tmp, rpath2);
			_tcscat (tmp, _T("\\CurVer"));
			if (RegCreateKeyEx (rkey, tmp, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ, NULL, &key2, &disposition) == ERROR_SUCCESS) {
				RegSetValueEx (key2, _T(""), 0, REG_SZ, (CONST BYTE *)defprogid, (_tcslen (defprogid) + 1) * sizeof (TCHAR));
				RegCloseKey (key2);
			}
			if (icon) {
				_tcscpy (tmp, rpath2);
				_tcscat (tmp, _T("\\DefaultIcon"));
				if (RegCreateKeyEx (rkey, tmp, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ, NULL, &key2, &disposition) == ERROR_SUCCESS) {
					_stprintf (tmp, _T("%s,%d"), executable_path, -icon);
					RegSetValueEx (key2, _T(""), 0, REG_SZ, (CONST BYTE *)tmp, (_tcslen (tmp) + 1) * sizeof (TCHAR));
					RegCloseKey (key2);
				}
			}
			RegCloseKey (key1);
		}
	}
	cc = NULL;
	struct contextcommand ccs[2];
	memset (ccs, 0, sizeof ccs);
	if ((command || shellcommand)) {
		ccs[0].command = command;
		ccs[0].shellcommand = shellcommand;
		ccs[0].icon = IDI_APPICON;
		cc = &ccs[0];
	}
	if (cc) {
		TCHAR path2[MAX_DPATH];
		for (int i = 0; cc[i].command; i++) {
			_tcscpy (path2, rpath2);
			_tcscat (path2, _T("\\shell\\"));
			if (cc[i].shellcommand)
				_tcscat (path2, cc[i].shellcommand);
			else
				_tcscat (path2, _T("open"));
			if (cc[i].icon) {
				if (RegCreateKeyEx (rkey, path2, 0, NULL, REG_OPTION_NON_VOLATILE,
					KEY_WRITE | KEY_READ, NULL, &key1, &disposition) == ERROR_SUCCESS) {
						TCHAR tmp[MAX_DPATH];
						_stprintf (tmp, _T("%s,%d"), executable_path, -cc[i].icon);
						RegSetValueEx (key1, _T("Icon"), 0, REG_SZ, (CONST BYTE *)tmp, (_tcslen (tmp) + 1) * sizeof (TCHAR));
						RegCloseKey (key1);
				}
			}
			_tcscat (path2, _T("\\command"));
			if (RegCreateKeyEx (rkey, path2, 0, NULL, REG_OPTION_NON_VOLATILE,
				KEY_WRITE | KEY_READ, NULL, &key1, &disposition) == ERROR_SUCCESS) {
					TCHAR path[MAX_DPATH];
					_stprintf (path, _T("\"%sWinUAE.exe\" %s"), start_path_exe, cc[i].command);
					RegSetValueEx (key1, _T(""), 0, REG_SZ, (CONST BYTE *)path, (_tcslen (path) + 1) * sizeof (TCHAR));
					RegCloseKey (key1);
			}
		}
	}
	fkey = regcreatetree (NULL, _T("FileAssociations"));
	regsetstr (fkey, extension, _T(""));
	regclosetree (fkey);
	return 1;
}
static int shell_associate (const TCHAR *extension, TCHAR *command, struct contextcommand *cc, const TCHAR *perceivedtype, const TCHAR *description, const TCHAR *ext2, int icon)
{
	int v = shell_associate_2 (extension, NULL, command, cc, perceivedtype, description, ext2, icon);
	if (!_tcscmp (extension, _T(".uae")))
		shell_associate_2 (extension, _T("edit"), _T("-f \"%1\" -s use_gui=yes"), NULL, _T("text"), description, NULL, 0);
	return v;
}

static int shell_associate_is (const TCHAR *extension)
{
	TCHAR rpath1[MAX_DPATH], rpath2[MAX_DPATH];
	TCHAR progid2[MAX_DPATH], tmp[MAX_DPATH];
	DWORD size;
	HKEY rkey, key1;
	const TCHAR *progid = _T("WinUAE");
	int def = !_tcscmp (extension, _T(".uae"));

	_tcscpy (progid2, progid);
	_tcscat (progid2, extension);
	if (os_admin > 1)
		rkey = HKEY_LOCAL_MACHINE;
	else
		rkey = HKEY_CURRENT_USER;

	_tcscpy (rpath1, _T("Software\\Classes\\"));
	_tcscpy (rpath2, rpath1);
	_tcscat (rpath2, extension);
	size = sizeof tmp / sizeof (TCHAR);
	if (RegOpenKeyEx (rkey, rpath2, 0, KEY_READ, &key1) == ERROR_SUCCESS) {
		if (RegQueryValueEx (key1, NULL, NULL, NULL, (LPBYTE)tmp, &size) == ERROR_SUCCESS) {
			if (_tcscmp (tmp, def ? progid : progid2)) {
				RegCloseKey (key1);
				return 0;
			}
		}
		RegCloseKey (key1);
	}
	_tcscpy (rpath2, rpath1);
	_tcscat (rpath2, progid);
	if (!def)
		_tcscat (rpath2, extension);
	if (RegOpenKeyEx (rkey, rpath2, 0, KEY_READ, &key1) == ERROR_SUCCESS) {
		RegCloseKey (key1);
		return 1;
	}
	return 0;
}
static struct contextcommand cc_cd[] = {
	{ _T("CDTV"), _T("-cdimage=\"%1\" -s use_gui=no -cfgparam=quickstart=CDTV,0"), IDI_APPICON },
	{ _T("CD32"), _T("-cdimage=\"%1\" -s use_gui=no -cfgparam=quickstart=CD32,0"), IDI_APPICON },
	{ NULL }
};
static struct  contextcommand cc_disk[] = {
	{ _T("A500"), _T("-0 \"%1\" -s use_gui=no -cfgparam=quickstart=A500,0"), IDI_DISKIMAGE },
	{ _T("A1200"), _T("-0 \"%1\" -s use_gui=no -cfgparam=quickstart=A1200,0"), IDI_DISKIMAGE },
	{ NULL }
};
struct assext exts[] = {
//	{ _T(".cue"), _T("-cdimage=\"%1\" -s use_gui=no"), _T("WinUAE CD image"), IDI_DISKIMAGE, cc_cd },
//	{ _T(".iso"), _T("-cdimage=\"%1\" -s use_gui=no"), _T("WinUAE CD image"), IDI_DISKIMAGE, cc_cd },
//	{ _T(".ccd"), _T("-cdimage=\"%1\" -s use_gui=no"), _T("WinUAE CD image"), IDI_DISKIMAGE, cc_cd },
	{ _T(".uae"), _T("-f \"%1\""), _T("WinUAE configuration file"), IDI_CONFIGFILE, NULL },
	{ _T(".adf"), _T("-0 \"%1\" -s use_gui=no"), _T("WinUAE floppy disk image"), IDI_DISKIMAGE, cc_disk },
	{ _T(".adz"), _T("-0 \"%1\" -s use_gui=no"), _T("WinUAE floppy disk image"), IDI_DISKIMAGE, cc_disk },
	{ _T(".dms"), _T("-0 \"%1\" -s use_gui=no"), _T("WinUAE floppy disk image"), IDI_DISKIMAGE, cc_disk },
	{ _T(".fdi"), _T("-0 \"%1\" -s use_gui=no"), _T("WinUAE floppy disk image"), IDI_DISKIMAGE, cc_disk },
	{ _T(".ipf"), _T("-0 \"%1\" -s use_gui=no"), _T("WinUAE floppy disk image"), IDI_DISKIMAGE, cc_disk },
	{ _T(".uss"), _T("-s statefile=\"%1\" -s use_gui=no"), _T("WinUAE statefile"), IDI_APPICON, NULL },
	{ NULL }
};

static void associate_init_extensions (void)
{
	int i;

	for (i = 0; exts[i].ext; i++) {
		exts[i].enabled = 0;
		if (shell_associate_is (exts[i].ext))
			exts[i].enabled = 1;
	}
	if (rp_param || inipath)
		return;
	// associate .uae by default when running for the first time
	if (!regexiststree (NULL, _T("FileAssociations"))) {
		UAEREG *fkey;
		if (exts[0].enabled == 0) {
			shell_associate (exts[0].ext, exts[0].cmd, exts[0].cc, NULL, exts[0].desc, NULL, exts[0].icon);
			exts[0].enabled = shell_associate_is (exts[0].ext);
		}
		fkey = regcreatetree (NULL, _T("FileAssociations"));
		regsetstr (fkey, exts[0].ext, _T(""));
		regclosetree (fkey);
	}
	if (os_admin > 1) {
		DWORD disposition;
		TCHAR rpath[MAX_DPATH];
		HKEY rkey = HKEY_LOCAL_MACHINE;
		HKEY key1;
		bool setit = true;

		_tcscpy (rpath, _T("Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\winuae.exe"));
		if (RegOpenKeyEx (rkey, rpath, 0, KEY_READ, &key1) == ERROR_SUCCESS) {
			TCHAR tmp[MAX_DPATH];
			DWORD size = sizeof tmp / sizeof (TCHAR);
			if (RegQueryValueEx (key1, NULL, NULL, NULL, (LPBYTE)tmp, &size) == ERROR_SUCCESS) {
				if (!_tcsicmp (tmp, executable_path))
					setit = false;
			}
			RegCloseKey (key1);
		}
		if (setit) {
			if (RegCreateKeyEx (rkey, rpath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &key1, &disposition) == ERROR_SUCCESS) {
				DWORD val = 1;
				RegSetValueEx (key1, _T(""), 0, REG_SZ, (CONST BYTE *)executable_path, (_tcslen (executable_path) + 1) * sizeof (TCHAR));
				RegSetValueEx (key1, _T("UseUrl"), 0, REG_DWORD, (LPBYTE)&val, sizeof val);
				_tcscpy (rpath, start_path_exe);
				rpath[_tcslen (rpath) - 1] = 0;
				RegSetValueEx (key1, _T("Path"), 0, REG_SZ, (CONST BYTE *)rpath, (_tcslen (rpath) + 1) * sizeof (TCHAR));
				RegCloseKey (key1);
				SHChangeNotify (SHCNE_ASSOCCHANGED, 0, 0, 0); 
			}
		}
	}

#if 0
	UAEREG *fkey;
	fkey = regcreatetree (NULL, _T("FileAssociations"));
	if (fkey) {
		int ok = 1;
		TCHAR tmp[MAX_DPATH];
		_tcscpy (tmp, _T("Following file associations:\n"));
		for (i = 0; exts[i].ext; i++) {
			TCHAR tmp2[10];
			int size = sizeof tmp;
			int is1 = exts[i].enabled;
			int is2 = regquerystr (fkey, exts[i].ext, tmp2, &size);
			if (is1 == 0 && is2 != 0) {
				_tcscat (tmp, exts[i].ext);
				_tcscat (tmp, _T("\n"));
				ok = 0;
			}
		}
		if (!ok) {
			TCHAR szTitle[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);
			_tcscat (szTitle, BetaStr);
			if (MessageBox (NULL, tmp, szTitle, MB_YESNO | MB_TASKMODAL) == IDOK) {
				for (i = 0; exts[i].ext; i++) {
					TCHAR tmp2[10];
					int size = sizeof tmp;
					int is1 = exts[i].enabled;
					int is2 = regquerystr (fkey, exts[i].ext, tmp2, &size);
					if (is1 == 0 && is2 != 0) {
						regdelete (fkey, exts[i].ext);
						shell_associate (exts[i].ext, exts[i].cmd, NULL, exts[i].desc, NULL);
						exts[i].enabled = shell_associate_is (exts[i].ext);
					}
				}
			} else {
				for (i = 0; exts[i].ext; i++) {
					if (!exts[i].enabled)
						regdelete (fkey, exts[i].ext);
				}
			}
		}
	}
#endif
}

void associate_file_extensions (void)
{
	int i;
	int modified = 0;

	if (rp_param)
		return;
	for (i = 0; exts[i].ext; i++) {
		int already = shell_associate_is (exts[i].ext);
		if (exts[i].enabled == 0 && already) {
			shell_deassociate (exts[i].ext);
			exts[i].enabled = shell_associate_is (exts[i].ext);
			if (exts[i].enabled) {
				modified = 1;
				shell_associate (exts[i].ext, exts[i].cmd, exts[i].cc, NULL, exts[i].desc, NULL, exts[i].icon);
			}
		} else if (exts[i].enabled) {
			shell_associate (exts[i].ext, exts[i].cmd, exts[i].cc, NULL, exts[i].desc, NULL, exts[i].icon);
			exts[i].enabled = shell_associate_is (exts[i].ext);
			if (exts[i].enabled != already)
				modified = 1;
		}
	}
	if (modified)
		SHChangeNotify (SHCNE_ASSOCCHANGED, 0, 0, 0); 
}

static void WIN32_HandleRegistryStuff (void)
{
	RGBFTYPE colortype = RGBFB_NONE;
	DWORD dwType = REG_DWORD;
	DWORD dwDisplayInfoSize = sizeof (colortype);
	int size;
	TCHAR path[MAX_DPATH] = _T("");
	TCHAR version[100];

	initpath (_T("FloppyPath"), start_path_data);
	initpath (_T("KickstartPath"), start_path_data);
	initpath (_T("hdfPath"), start_path_data);
	initpath (_T("ConfigurationPath"), start_path_data);
	initpath (_T("LuaPath"), start_path_data);
	initpath (_T("ScreenshotPath"), start_path_data);
	initpath (_T("StatefilePath"), start_path_data);
	initpath (_T("SaveimagePath"), start_path_data);
	initpath (_T("VideoPath"), start_path_data);
	initpath (_T("InputPath"), start_path_data);
	if (!regexists (NULL, _T("MainPosX")) || !regexists (NULL, _T("GUIPosX"))) {
		int x = GetSystemMetrics (SM_CXSCREEN);
		int y = GetSystemMetrics (SM_CYSCREEN);
		x = (x - 800) / 2;
		y = (y - 600) / 2;
		if (x < 10)
			x = 10;
		if (y < 10)
			y = 10;
		/* Create and initialize all our sub-keys to the default values */
		regsetint (NULL, _T("MainPosX"), x);
		regsetint (NULL, _T("MainPosY"), y);
		regsetint (NULL, _T("GUIPosX"), x);
		regsetint (NULL, _T("GUIPosY"), y);
	}
	size = sizeof (version) / sizeof (TCHAR);
	if (regquerystr (NULL, _T("Version"), version, &size)) {
		if (checkversion (version))
			regsetstr (NULL, _T("Version"), VersionStr);
	} else {
		regsetstr (NULL, _T("Version"), VersionStr);
	}
	size = sizeof (version) / sizeof (TCHAR);
	if (regquerystr (NULL, _T("ROMCheckVersion"), version, &size)) {
		if (checkversion (version)) {
			if (regsetstr (NULL, _T("ROMCheckVersion"), VersionStr))
				forceroms = 1;
		}
	} else {
		if (regsetstr (NULL, _T("ROMCheckVersion"), VersionStr))
			forceroms = 1;
	}

	regqueryint (NULL, _T("DirectDraw_Secondary"), &ddforceram);
	if (regexists (NULL, _T("SoundDriverMask"))) {
		regqueryint (NULL, _T("SoundDriverMask"), &sounddrivermask);
	} else {
		sounddrivermask = 3;
		regsetint (NULL, _T("SoundDriverMask"), sounddrivermask);
	}

	if (regexists (NULL, _T("RecursiveROMScan")))
		regqueryint (NULL, _T("RecursiveROMScan"), &recursiveromscan);
	else
		regsetint (NULL, _T("RecursiveROMScan"), recursiveromscan);

	if (regexists (NULL, _T("ConfigurationCache")))
		regqueryint (NULL, _T("ConfigurationCache"), &configurationcache);
	else
		regsetint (NULL, _T("ConfigurationCache"), configurationcache);

	if (regexists(NULL, _T("ArtCache")))
		regqueryint(NULL, _T("ArtCache"), &artcache);
	else
		regsetint(NULL, _T("ArtCache"), artcache);

	if (regexists(NULL, _T("ArtImageCount")))
		regqueryint(NULL, _T("ArtImageCount"), &max_visible_boxart_images);
	else
		regsetint(NULL, _T("ArtImageCount"), max_visible_boxart_images);

	if (regexists(NULL, _T("ArtImageWidth")))
		regqueryint(NULL, _T("ArtImageWidth"), &stored_boxart_window_width);
	else
		regsetint(NULL, _T("ArtImageWidth"), stored_boxart_window_width);

	if (regexists (NULL, _T("SaveImageOriginalPath")))
		regqueryint (NULL, _T("SaveImageOriginalPath"), &saveimageoriginalpath);
	else
		regsetint (NULL, _T("SaveImageOriginalPath"), saveimageoriginalpath);


	if (regexists (NULL, _T("RelativePaths")))
		regqueryint (NULL, _T("RelativePaths"), &relativepaths);
	else
		regsetint (NULL, _T("RelativePaths"), relativepaths);

	if (!regqueryint (NULL, _T("QuickStartMode"), &quickstart))
		quickstart = 1;
	reopen_console ();
	fetch_path (_T("ConfigurationPath"), path, sizeof (path) / sizeof (TCHAR));
	if (path[0])
		path[_tcslen (path) - 1] = 0;
	if (GetFileAttributes (path) == 0xffffffff) {
		TCHAR path2[MAX_DPATH];
		_tcscpy (path2, path);
		createdir (path);
		_tcscat (path, _T("\\Host"));
		createdir (path);
		_tcscpy (path, path2);
		_tcscat (path, _T("\\Hardware"));
		createdir (path);
	}
	fetch_path (_T("StatefilePath"), path, sizeof (path) / sizeof (TCHAR));
	createdir (path);
	_tcscat (path, _T("default.uss"));
	_tcscpy (savestate_fname, path);
	fetch_path (_T("InputPath"), path, sizeof (path) / sizeof (TCHAR));
	createdir (path);
	regclosetree (read_disk_history (HISTORY_FLOPPY));
	regclosetree (read_disk_history (HISTORY_CD));
	associate_init_extensions ();
	read_rom_list ();
	load_keyring (NULL, NULL);
}

#if WINUAEPUBLICBETA > 0
static TCHAR *BETAMESSAGE = {
	_T("This is unstable beta software. Click cancel if you are not comfortable using software that is incomplete and can have serious programming errors.")
};
#endif

static int betamessage (void)
{
#if WINUAEPUBLICBETA > 0
	int showmsg = TRUE;
	HANDLE h = INVALID_HANDLE_VALUE;
	ULONGLONG regft64;
	ULARGE_INTEGER ft64;
	ULARGE_INTEGER sft64;
	struct tm *t;
	__int64 ltime;
	DWORD dwType, size;
#ifdef _WIN64
	const TCHAR *tokenname = _T("BetaToken64");
#else
	const TCHAR *tokenname = _T("BetaToken");
#endif

	ft64.QuadPart = 0;
	for (;;) {
		FILETIME ft, sft;
		SYSTEMTIME st;
		TCHAR tmp1[MAX_DPATH];

		if (!hWinUAEKey)
			break;
		if (GetModuleFileName (NULL, tmp1, sizeof tmp1 / sizeof (TCHAR)) == 0)
			break;
		h = CreateFile (tmp1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE)
			break;
		if (GetFileTime (h, &ft, NULL, NULL) == 0)
			break;
		ft64.LowPart = ft.dwLowDateTime;
		ft64.HighPart = ft.dwHighDateTime;
		dwType = REG_QWORD;
		size = sizeof regft64;
		if (!regquerylonglong (NULL, tokenname, &regft64))
			break;
		GetSystemTime(&st);
		SystemTimeToFileTime(&st, &sft);
		sft64.LowPart = sft.dwLowDateTime;
		sft64.HighPart = sft.dwHighDateTime;
		if (ft64.QuadPart == regft64)
			showmsg = FALSE;
		/* complain again in 7 days */
		if (sft64.QuadPart > regft64 + (ULONGLONG)1000000000 * 60 * 60 * 24 * 7)
			showmsg = TRUE;
		break;
	}
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle (h);
	if (showmsg) {
		int r, data;
		TCHAR title[MAX_DPATH];

		dwType = REG_DWORD;
		size = sizeof data;
		if (regqueryint (NULL, _T("Beta_Just_Shut_Up"), &data)) {
			if (data == 68000 + 10) {
				write_log (_T("I was told to shut up :(\n"));
				return 1;
			}
		}
		_time64 (&ltime);
		t = _gmtime64 (&ltime);
		/* "expire" in 1 month */
		if (MAKEBD(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday) > WINUAEDATE + 100)
			pre_gui_message (_T("This beta build of WinUAE is obsolete.\nPlease download newer version."));

		_tcscpy (title, _T("WinUAE Public Beta Disclaimer"));
		_tcscat (title, BetaStr);
		r = MessageBox (NULL, BETAMESSAGE, title, MB_OKCANCEL | MB_TASKMODAL | MB_SETFOREGROUND | MB_ICONWARNING | MB_DEFBUTTON2);
		if (r == IDABORT || r == IDCANCEL)
			return 0;
		if (ft64.QuadPart > 0) {
			regft64 = ft64.QuadPart;
			regsetlonglong (NULL, tokenname, regft64);
		}
	}
#endif
	return 1;
}

int os_admin, os_64bit, os_win7, os_win8, os_win10, os_vista, cpu_number, os_touch;
BOOL os_dwm_enabled;

static int isadminpriv (void)
{
	DWORD i, dwSize = 0, dwResult = 0;
	HANDLE hToken;
	PTOKEN_GROUPS pGroupInfo;
	BYTE sidBuffer[100];
	PSID pSID = (PSID)&sidBuffer;
	SID_IDENTIFIER_AUTHORITY SIDAuth = SECURITY_NT_AUTHORITY;
	int isadmin = 0;

	// Open a handle to the access token for the calling process.
	if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &hToken)) {
		write_log (_T("OpenProcessToken Error %u\n"), GetLastError ());
		return FALSE;
	}

	// Call GetTokenInformation to get the buffer size.
	if(!GetTokenInformation (hToken, TokenGroups, NULL, dwSize, &dwSize)) {
		dwResult = GetLastError ();
		if(dwResult != ERROR_INSUFFICIENT_BUFFER) {
			write_log (_T("GetTokenInformation Error %u\n"), dwResult);
			return FALSE;
		}
	}

	// Allocate the buffer.
	pGroupInfo = (PTOKEN_GROUPS)GlobalAlloc (GPTR, dwSize);

	// Call GetTokenInformation again to get the group information.
	if (!GetTokenInformation (hToken, TokenGroups, pGroupInfo, dwSize, &dwSize)) {
		write_log (_T("GetTokenInformation Error %u\n"), GetLastError ());
		return FALSE;
	}

	// Create a SID for the BUILTIN\Administrators group.
	if (!AllocateAndInitializeSid (&SIDAuth, 2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&pSID)) {
			write_log (_T("AllocateAndInitializeSid Error %u\n"), GetLastError ());
			return FALSE;
	}

	// Loop through the group SIDs looking for the administrator SID.
	for(i = 0; i < pGroupInfo->GroupCount; i++) {
		if (EqualSid (pSID, pGroupInfo->Groups[i].Sid))
			isadmin = 1;
	}

	if (pSID)
		FreeSid (pSID);
	if (pGroupInfo)
		GlobalFree (pGroupInfo);
	return isadmin;
}

typedef void (CALLBACK* PGETNATIVESYSTEMINFO)(LPSYSTEM_INFO);
typedef BOOL (CALLBACK* PISUSERANADMIN)(VOID);

static int osdetect (void)
{
	PGETNATIVESYSTEMINFO pGetNativeSystemInfo;
	PISUSERANADMIN pIsUserAnAdmin;

	pGetNativeSystemInfo = (PGETNATIVESYSTEMINFO)GetProcAddress (
		GetModuleHandle (_T("kernel32.dll")), "GetNativeSystemInfo");
	pIsUserAnAdmin = (PISUSERANADMIN)GetProcAddress (
		GetModuleHandle (_T("shell32.dll")), "IsUserAnAdmin");

	GetSystemInfo (&SystemInfo);
	if (pGetNativeSystemInfo)
		pGetNativeSystemInfo (&SystemInfo);
	osVersion.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
	if (GetVersionEx (&osVersion)) {
		if (osVersion.dwMajorVersion >= 6)
			os_vista = 1;
		if (osVersion.dwMajorVersion >= 7 || (osVersion.dwMajorVersion == 6 && osVersion.dwMinorVersion >= 1))
			os_win7 = 1;
		if (osVersion.dwMajorVersion >= 7 || (osVersion.dwMajorVersion == 6 && osVersion.dwMinorVersion >= 2))
			os_win8 = 1;
		if (osVersion.dwMajorVersion >= 7 || (osVersion.dwMajorVersion == 6 && osVersion.dwMinorVersion >= 3))
			os_win8 = 2;
		if (osVersion.dwMajorVersion >= 10)
			os_win10 = 1;
		if (SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
			os_64bit = 1;
	}
	cpu_number = SystemInfo.dwNumberOfProcessors;
	os_admin = isadminpriv ();
	if (os_admin) {
		if (pIsUserAnAdmin) {
			if (pIsUserAnAdmin ())
				os_admin++;
		} else {
			os_admin++;
		}
	}

	if (os_win7) {
		int v = GetSystemMetrics(SM_DIGITIZER);
		if (v & NID_READY) {
			if (v & (NID_INTEGRATED_TOUCH | NID_INTEGRATED_PEN | NID_EXTERNAL_TOUCH | NID_EXTERNAL_PEN))
				os_touch = 1;
		}
	}

	if (os_vista) {
		typedef HRESULT(CALLBACK* DWMISCOMPOSITIONENABLED)(BOOL*);
		HMODULE dwmapihandle;
		DWMISCOMPOSITIONENABLED pDwmIsCompositionEnabled;
		dwmapihandle = LoadLibrary(_T("dwmapi.dll"));
		if (dwmapihandle) {
			pDwmIsCompositionEnabled = (DWMISCOMPOSITIONENABLED)GetProcAddress(dwmapihandle, "DwmIsCompositionEnabled");
			if (pDwmIsCompositionEnabled) {
				pDwmIsCompositionEnabled(&os_dwm_enabled);
			}
			FreeLibrary(dwmapihandle);
		}
	}


	return 1;
}

void create_afnewdir (int remove)
{
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];

	if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_COMMON_DOCUMENTS, NULL, 0, tmp))) {
		fixtrailing (tmp);
		_tcscpy (tmp2, tmp);
		_tcscat (tmp2, _T("Amiga Files"));
		_tcscpy (tmp, tmp2);
		_tcscat (tmp, _T("\\WinUAE"));
		if (remove) {
			DWORD attrs = GetFileAttributes(tmp);
			if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
				RemoveDirectory (tmp);
				RemoveDirectory (tmp2);
			}
		} else {
			CreateDirectory (tmp2, NULL);
			CreateDirectory (tmp, NULL);
		}
	}
}

static bool isdir (const TCHAR *path)
{
	DWORD a = GetFileAttributes (path);
	if (a == INVALID_FILE_ATTRIBUTES)
		return false;
	if (a & FILE_ATTRIBUTE_DIRECTORY)
		return true;
	return false;
}

bool get_plugin_path (TCHAR *out, int len, const TCHAR *path)
{
	TCHAR tmp[MAX_DPATH];
	_tcscpy (tmp, start_path_plugins);
	if (path != NULL)
		_tcscat (tmp, path);
	if (isdir (tmp)) {
		_tcscpy (out, tmp);
		fixtrailing (out);
		return true;
	}
	if (!_tcsicmp (path, _T("floppysounds"))) {
		_tcscpy (tmp, start_path_data);
		_tcscpy (tmp, _T("uae_data"));
		if (isdir (tmp)) {
			_tcscpy (out, tmp);
			fixtrailing (out);
			return true;
		}
		_tcscpy (tmp, start_path_exe);
		_tcscpy (tmp, _T("uae_data"));
		if (isdir (tmp)) {
			_tcscpy (out, tmp);
			fixtrailing (out);
			return true;
		}
	}
	_tcscpy (tmp, start_path_data);
	_tcscpy (tmp, WIN32_PLUGINDIR);
	if (path != NULL)
		_tcscat (tmp, path);
	if (isdir (tmp)) {
		_tcscpy (out, tmp);
		fixtrailing (out);
		return true;
	}
	_tcscpy (tmp, start_path_exe);
	_tcscpy (tmp, WIN32_PLUGINDIR);
	if (path != NULL)
		_tcscat (tmp, path);
	if (isdir (tmp)) {
		_tcscpy (out, tmp);
		fixtrailing (out);
		return true;
	}
	_tcscpy (out, start_path_plugins);
	if (path != NULL)
		_tcscat (tmp, path);
	fixtrailing (out);
	return false;
}

void setpathmode (pathtype pt)
{
	TCHAR pathmode[32] = { 0 };
	if (pt == PATH_TYPE_WINUAE)
		_tcscpy (pathmode, _T("WinUAE"));
	if (pt == PATH_TYPE_NEWWINUAE)
		_tcscpy (pathmode, _T("WinUAE_2"));
	if (pt == PATH_TYPE_NEWAF)
		_tcscpy (pathmode, _T("AmigaForever"));
	if (pt == PATH_TYPE_AMIGAFOREVERDATA)
		_tcscpy (pathmode, _T("AMIGAFOREVERDATA"));
	regsetstr (NULL, _T("PathMode"), pathmode);
}

static void getstartpaths (void)
{
	TCHAR *posn, *p;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH], prevpath[MAX_DPATH];
	DWORD v;
	UAEREG *key;
	TCHAR xstart_path_uae[MAX_DPATH], xstart_path_old[MAX_DPATH];
	TCHAR xstart_path_new1[MAX_DPATH], xstart_path_new2[MAX_DPATH];

	path_type = PATH_TYPE_DEFAULT;
	prevpath[0] = 0;
	xstart_path_uae[0] = xstart_path_old[0] = xstart_path_new1[0] = xstart_path_new2[0] = 0;
	key = regcreatetree (NULL, NULL);
	if (key)  {
		int size = sizeof (prevpath) / sizeof (TCHAR);
		if (!regquerystr (key, _T("PathMode"), prevpath, &size))
			prevpath[0] = 0;
		regclosetree (key);
	}
	if (!_tcscmp (prevpath, _T("WinUAE")))
		path_type = PATH_TYPE_WINUAE;
	if (!_tcscmp (prevpath, _T("WinUAE_2")))
		path_type = PATH_TYPE_NEWWINUAE;
	if (!_tcscmp (prevpath, _T("AF2005")) || !_tcscmp (prevpath, _T("AmigaForever")))
		path_type = PATH_TYPE_NEWAF;
	if (!_tcscmp (prevpath, _T("AMIGAFOREVERDATA")))
		path_type = PATH_TYPE_AMIGAFOREVERDATA;

	_tcscpy(start_path_exe, executable_path);
	if((posn = _tcsrchr (start_path_exe, '\\')))
		posn[1] = 0;

	if (path_type == PATH_TYPE_DEFAULT && inipath) {
		path_type = PATH_TYPE_WINUAE;
		_tcscpy (xstart_path_uae, start_path_exe);
		relativepaths = 1;
	} else if (path_type == PATH_TYPE_DEFAULT && start_data == 0 && key) {
		bool ispath = false;
		_tcscpy (tmp2, start_path_exe);
		_tcscat (tmp2, _T("configurations\\configuration.cache"));
		v = GetFileAttributes (tmp2);
		if (v != INVALID_FILE_ATTRIBUTES && !(v & FILE_ATTRIBUTE_DIRECTORY))
			ispath = true;
		_tcscpy (tmp2, start_path_exe);
		_tcscat (tmp2, _T("roms"));
		v = GetFileAttributes (tmp2);
		if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY))
			ispath = true;
		if (!ispath) {
			if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, tmp))) {
				GetFullPathName (tmp, sizeof tmp / sizeof (TCHAR), tmp2, NULL);
				// installed in Program Files?
				if (_tcsnicmp (tmp, tmp2, _tcslen (tmp)) == 0) {
					if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_COMMON_DOCUMENTS, NULL, SHGFP_TYPE_CURRENT, tmp))) {
						fixtrailing (tmp);
						_tcscpy (tmp2, tmp);
						_tcscat (tmp2, _T("Amiga Files"));
						CreateDirectory (tmp2, NULL);
						_tcscat (tmp2, _T("\\WinUAE"));
						CreateDirectory (tmp2, NULL);
						v = GetFileAttributes (tmp2);
						if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
							_tcscat (tmp2, _T("\\"));
							path_type = PATH_TYPE_NEWWINUAE;
							_tcscpy (tmp, tmp2);
							_tcscat (tmp, _T("Configurations"));
							CreateDirectory (tmp, NULL);
							_tcscpy (tmp, tmp2);
							_tcscat (tmp, _T("Screenshots"));
							CreateDirectory (tmp, NULL);
							_tcscpy (tmp, tmp2);
							_tcscat (tmp, _T("Savestates"));
							CreateDirectory (tmp, NULL);
							_tcscpy (tmp, tmp2);
							_tcscat (tmp, _T("Screenshots"));
							CreateDirectory (tmp, NULL);
						}
					}
				}
			}
		}
	}

	_tcscpy (tmp, start_path_exe);
	_tcscat (tmp, _T("roms"));
	if (isfilesindir (tmp)) {
		_tcscpy (xstart_path_uae, start_path_exe);
	}
	_tcscpy (tmp, start_path_exe);
	_tcscat (tmp, _T("configurations"));
	if (isfilesindir (tmp)) {
		_tcscpy (xstart_path_uae, start_path_exe);
	}

	p = _wgetenv (_T("AMIGAFOREVERDATA"));
	if (p) {
		_tcscpy (tmp, p);
		fixtrailing (tmp);
		_tcscpy (start_path_new2, p);
		v = GetFileAttributes (tmp);
		if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
			_tcscpy (xstart_path_new2, start_path_new2);
			_tcscpy (xstart_path_new2, _T("WinUAE\\"));
			af_path_2005 |= 2;
		}
	}

	if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_COMMON_DOCUMENTS, NULL, SHGFP_TYPE_CURRENT, tmp))) {
		fixtrailing (tmp);
		_tcscpy (tmp2, tmp);
		_tcscat (tmp2, _T("Amiga Files\\"));
		_tcscpy (tmp, tmp2);
		_tcscat (tmp, _T("WinUAE"));
		v = GetFileAttributes (tmp);
		if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
			TCHAR *p;
			_tcscpy (xstart_path_new1, tmp2);
			_tcscat (xstart_path_new1, _T("WinUAE\\"));
			_tcscpy (xstart_path_uae, start_path_exe);
			_tcscpy (start_path_new1, xstart_path_new1);
			p = tmp2 + _tcslen (tmp2);
			_tcscpy (p, _T("System"));
			if (isfilesindir (tmp2)) {
				af_path_2005 |= 1;
			} else {
				_tcscpy (p, _T("Shared"));
				if (isfilesindir (tmp2)) {
					af_path_2005 |= 1;
				}
			}
		}
	}

	if (start_data == 0) {
		start_data = 1;
		if (path_type == PATH_TYPE_WINUAE && xstart_path_uae[0]) {
			_tcscpy (start_path_data, xstart_path_uae);
		} else if (path_type == PATH_TYPE_NEWWINUAE && xstart_path_new1[0]) {
			_tcscpy (start_path_data, xstart_path_new1);
			create_afnewdir (0);
		} else if (path_type == PATH_TYPE_NEWAF && (af_path_2005 & 1) && xstart_path_new1[0]) {
			_tcscpy (start_path_data, xstart_path_new1);
			create_afnewdir (0);
		} else if (path_type == PATH_TYPE_AMIGAFOREVERDATA && (af_path_2005 & 2) && xstart_path_new2[0]) {
			_tcscpy (start_path_data, xstart_path_new2);
		} else if (path_type == PATH_TYPE_DEFAULT) {
			_tcscpy (start_path_data, xstart_path_uae);
			if (af_path_2005 & 1) {
				path_type = PATH_TYPE_NEWAF;
				create_afnewdir (1);
				_tcscpy (start_path_data, xstart_path_new1);
			}
			if (af_path_2005 & 2) {
				_tcscpy (tmp, xstart_path_new2);
				_tcscat (tmp, _T("system\\rom"));
				if (isfilesindir (tmp)) {
					path_type = PATH_TYPE_AMIGAFOREVERDATA;
				} else {
					_tcscpy (tmp, xstart_path_new2);
					_tcscat (tmp, _T("shared\\rom"));
					if (isfilesindir (tmp)) {
						path_type = PATH_TYPE_AMIGAFOREVERDATA;
					} else {
						path_type = PATH_TYPE_NEWWINUAE;
					}
				}
				_tcscpy (start_path_data, xstart_path_new2);
			}
		}
	}

	v = GetFileAttributes (start_path_data);
	if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY) || start_data == 0 || start_data == -2) {
		_tcscpy (start_path_data, start_path_exe);
	}
	fixtrailing (start_path_data);
	fullpath (start_path_data, sizeof start_path_data / sizeof (TCHAR));
	SetCurrentDirectory (start_path_data);

	// use path via command line?
	if (!start_path_plugins[0]) {
		// default path
		_tcscpy (start_path_plugins, start_path_data);
		_tcscat (start_path_plugins, WIN32_PLUGINDIR);
	}
	v = GetFileAttributes (start_path_plugins);
	if (!start_path_plugins[0] || v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY)) {
		// not found, exe path?
		_tcscpy (start_path_plugins, start_path_exe);
		_tcscat (start_path_plugins, WIN32_PLUGINDIR);
		v = GetFileAttributes (start_path_plugins);
		if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
			_tcscpy (start_path_plugins, start_path_data); // not found, very default path
	}
	fixtrailing (start_path_plugins);
	fullpath (start_path_plugins, sizeof start_path_plugins / sizeof (TCHAR));
	setpathmode (path_type);
}

extern void test (void);
extern int screenshotmode, postscript_print_debugging, sound_debug, log_uaeserial, clipboard_debug;
extern int force_direct_catweasel, sound_mode_skip, maxmem;
extern int pngprint, log_sercon, midi_inbuflen;
extern int debug_rtg_blitter;
extern int log_bsd;
extern int inputdevice_logging;
extern int vsync_modechangetimeout;
extern int tablet_log;
extern int log_blitter;
extern int slirp_debug;
extern int fakemodewaitms;
extern float sound_sync_multiplier;
extern int log_cd32;
extern int log_ld;
extern int logitech_lcd;
extern uae_s64 max_avi_size;
extern int floppy_writemode;

extern DWORD_PTR cpu_affinity, cpu_paffinity;
static DWORD_PTR original_affinity = -1;

static int getval (const TCHAR *s)
{
	int base = 10;
	int v;
	TCHAR *endptr;

	if (s[0] == '0' && _totupper(s[1]) == 'X')
		s += 2, base = 16;
	v = _tcstol (s, &endptr, base);
	if (*endptr != '\0' || *s == '\0')
		return 0;
	return v;
}

static uae_s64 getval64(const TCHAR *s)
{
	int base = 10;
	uae_s64 v;
	TCHAR *endptr;

	if (s[0] == '0' && _totupper(s[1]) == 'X')
		s += 2, base = 16;
	v = _wcstoui64(s, &endptr, base);
	if (*endptr != '\0' || *s == '\0')
		return 0;
	return v;
}

#if 0
static float getvalf (const TCHAR *s)
{
	TCHAR *endptr;
	double v;

	v = _tcstof (s, &endptr);
	if (*endptr != '\0' || *s == '\0')
		return 0;
	return v;
}
#endif

static void makeverstr (TCHAR *s)
{
	if (_tcslen (WINUAEBETA) > 0) {
		_stprintf (BetaStr, _T(" (%sBeta %s, %d.%02d.%02d)"), WINUAEPUBLICBETA > 0 ? _T("Public ") : _T(""), WINUAEBETA,
			GETBDY(WINUAEDATE), GETBDM(WINUAEDATE), GETBDD(WINUAEDATE));
#ifdef _WIN64
		_tcscat (BetaStr, _T(" 64-bit"));
#endif
		_stprintf (s, _T("WinUAE %d.%d.%d%s%s"),
			UAEMAJOR, UAEMINOR, UAESUBREV, WINUAEREV, BetaStr);
	} else {
		_stprintf (s, _T("WinUAE %d.%d.%d%s (%d.%02d.%02d)"),
			UAEMAJOR, UAEMINOR, UAESUBREV, WINUAEREV, GETBDY(WINUAEDATE), GETBDM(WINUAEDATE), GETBDD(WINUAEDATE));
#ifdef _WIN64
		_tcscat (s, _T(" 64-bit"));
#endif
	}
	if (_tcslen (WINUAEEXTRA) > 0) {
		_tcscat (s, _T(" "));
		_tcscat (s, WINUAEEXTRA);
	}
}

static TCHAR *getdefaultini (int *tempfile)
{
	FILE *f;
	TCHAR path[MAX_DPATH], orgpath[MAX_DPATH];
	
	*tempfile = 0;
	path[0] = 0;
	if (!GetFullPathName (executable_path, sizeof path / sizeof (TCHAR), path, NULL))
		_tcscpy (path, executable_path);
	TCHAR *posn;
	if((posn = _tcsrchr (path, '\\')))
		posn[1] = 0;
	_tcscat (path, _T("winuae.ini"));
	_tcscpy (orgpath, path);
#if 1
	f = _tfopen (path, _T("r+"));
	if (f) {
		fclose (f);
		return my_strdup (path);
	}
	f = _tfopen (path, _T("w"));
	if (f) {
		fclose (f);
		return my_strdup (path);
	}
#endif
	*tempfile = 1;
	int v = GetTempPath (sizeof path / sizeof (TCHAR), path);
	if (v == 0 || v > sizeof path / sizeof (TCHAR))
		return my_strdup (orgpath);
	_tcsncat (path, _T("winuae.ini"), sizeof path / sizeof (TCHAR) - _tcslen(path));
	f = _tfopen (path, _T("w"));
	if (f) {
		fclose (f);
		return my_strdup (path);
	}
	return my_strdup (orgpath);
}

static int parseargs(const TCHAR *argx, const TCHAR *np, const TCHAR *np2)
{
	const TCHAR *arg = argx + 1;

	if (argx[0] != '-' && argx[0] != '/')
		return 0;

	if (!_tcscmp(arg, _T("convert")) && np && np2) {
		zfile_convertimage(np, np2);
		return -1;
	}
	if (!_tcscmp(arg, _T("console"))) {
		console_started = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("cli"))) {
		console_emulation = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("log"))) {
		console_logging = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("logfile"))) {
		winuaelog_temporary_enable = true;
		return 1;
	}
#ifdef FILESYS
	if (!_tcscmp(arg, _T("rdbdump"))) {
		do_rdbdump = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("hddump"))) {
		do_rdbdump = 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("disableharddrivesafetycheck"))) {
		//harddrive_dangerous = 0x1234dead;
		return 1;
	}
	if (!_tcscmp(arg, _T("noaspifiltering"))) {
		//aspi_allow_all = 1;
		return 1;
	}
#endif
	if (!_tcscmp(arg, _T("pngprint"))) {
		pngprint = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("norawinput"))) {
		no_rawinput |= 4;
		return 1;
	}
	if (!_tcscmp(arg, _T("norawinput_all"))) {
		no_rawinput = 7;
		return 1;
	}
	if (!_tcscmp(arg, _T("norawinput_keyboard"))) {
		no_rawinput |= 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("norawinput_mouse"))) {
		no_rawinput |= 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("nodirectinput"))) {
		no_directinput = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("nowindowsmouse"))) {
		no_windowsmouse = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("rawhid"))) {
		rawinput_enabled_hid = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("norawhid"))) {
		rawinput_enabled_hid = 0;
		return 1;
	}
	if (!_tcscmp(arg, _T("rawkeyboard"))) {
		// obsolete
		return 1;
	}
	if (!_tcscmp(arg, _T("winmouselog"))) {
		log_winmouse = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("directsound"))) {
		force_directsound = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("scsilog"))) {
		log_scsi = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("scsiemulog"))) {
		extern int log_scsiemu;
		log_scsiemu = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("ds_partition_hdf"))) {
		extern int enable_ds_partition_hdf;
		enable_ds_partition_hdf = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("filesyslog"))) {
		log_filesys = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("filesyslog2"))) {
		log_filesys = 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("netlog"))) {
		log_net = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("serlog"))) {
		log_sercon = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("serlog2"))) {
		log_sercon = 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("a2065log"))) {
		log_a2065 = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("a2065log2"))) {
		log_a2065 = 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("a2065log3"))) {
		log_a2065 = 3;
		return 1;
	}
	if (!_tcscmp(arg, _T("a2065_promiscuous"))) {
		a2065_promiscuous = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("seriallog"))) {
		log_uaeserial = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("vsynclog")) || !_tcscmp(arg, _T("vsynclog1"))) {
		log_vsync |= 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("vsynclog2"))) {
		log_vsync |= 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("bsdlog"))) {
		log_bsd = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("clipboarddebug"))) {
		clipboard_debug = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("rplog"))) {
		log_rp = 3;
		return 1;
	}
	if (!_tcscmp(arg, _T("nomultidisplay"))) {
		return 1;
	}
	if (!_tcscmp(arg, _T("legacypaths"))) {
		start_data = -2;
		return 1;
	}
	if (!_tcscmp(arg, _T("screenshotbmp"))) {
		screenshotmode = 0;
		return 1;
	}
	if (!_tcscmp(arg, _T("psprintdebug"))) {
		postscript_print_debugging = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("sounddebug"))) {
		sound_debug = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("directcatweasel"))) {
		force_direct_catweasel = 1;
		if (np) {
			force_direct_catweasel = getval(np);
			return 2;
		}
		return 1;
	}
	if (!_tcscmp(arg, _T("forcerdtsc"))) {
		uae_time_use_rdtsc(true);
		return 1;
	}
	if (!_tcscmp(arg, _T("ddsoftwarecolorkey"))) {
		// obsolete
		return 1;
	}
	if (!_tcscmp(arg, _T("nod3d9ex"))) {
		D3DEX = 0;
		return 1;
	}
	if (!_tcscmp(arg, _T("nod3d9shader"))) {
		shaderon = 0;
		return 1;
	}
	if (!_tcscmp(arg, _T("d3ddebug"))) {
		d3ddebug = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("logflush"))) {
		extern int always_flush_log;
		always_flush_log = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("ahidebug"))) {
		extern int ahi_debug;
		ahi_debug = 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("ahidebug2"))) {
		extern int ahi_debug;
		ahi_debug = 3;
		return 1;
	}
	if (!_tcscmp(arg, _T("quittogui"))) {
		quit_to_gui = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("ini")) && np) {
		inipath = my_strdup(np);
		return 2;
	}
	if (!_tcscmp(arg, _T("portable"))) {
		int temp;
		inipath = getdefaultini(&temp);
		createbootlog = false;
		return 1;
	}
	if (!_tcscmp(arg, _T("bootlog"))) {
		createbootlog = true;
		return 1;
	}
	if (!_tcscmp(arg, _T("nobootlog"))) {
		createbootlog = false;
		return 1;
	}
	if (!_tcscmp(arg, _T("cd32log"))) {
		if (log_cd32 < 1)
			log_cd32 = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("cd32log2"))) {
		if (log_cd32 < 2)
			log_cd32 = 2;
		return 1;
	}
	if (!_tcscmp(arg, _T("nolcd"))) {
		logitech_lcd = 0;
		return 1;
	}
	if (!_tcscmp(arg, _T("romlist"))) {
		void dumpromlist(void);
		dumpromlist();
		return -1;
	}
	if (!_tcscmp(arg, _T("rawextadf"))) {
		floppy_writemode = -1;
		return 1;
	}
	if (!_tcscmp(arg, _T("busywait"))) {
		busywait = 1;
		return 1;
	}
	if (!_tcscmp(arg, _T("nontdelayexecution"))) {
		noNtDelayExecution = 1;
		return 1;
	}
	if (!np)
		return 0;

#if 0
	if (!_tcscmp (arg, _T("sound_adjust"))) {
		sound_sync_multiplier = getvalf (np);
		return 2;
	}
#endif

	if (!_tcscmp(arg, _T("max_avi_size"))) {
		max_avi_size = getval64(np);
		return 2;
	}

	if (!_tcscmp(arg, _T("ethlog"))) {
		log_ethernet = getval(np);
		return 2;
	}
	if (!_tcscmp (arg, _T("vsync_modechangetimeout"))) {
		vsync_modechangetimeout = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("rtg_blitter"))) {
		debug_rtg_blitter = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("vsync_min_delay"))) {
		debug_vsync_min_delay = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("vsync_forced_delay"))) {
		debug_vsync_forced_delay = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("threadedd3d"))) {
		fakemodewaitms = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("tabletlog"))) {
		tablet_log = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("blitterdebug"))) {
		log_blitter = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("inputlog"))) {
		rawinput_log = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("inputdevicelog"))) {
		inputdevice_logging = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("slirplog"))) {
		slirp_debug = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("ldlog"))) {
		log_ld = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("midiinbuffer"))) {
		midi_inbuflen = getval (np);
		if (midi_inbuflen < 16000)
			midi_inbuflen = 16000;
		return 2;
	}
	if (!_tcscmp (arg, _T("ddforcemode"))) {
		extern int ddforceram;
		ddforceram = getval (np);
		if (ddforceram < 0 || ddforceram > 3)
			ddforceram = 0;
		return 2;
	}
	if (!_tcscmp (arg, _T("affinity"))) {
		cpu_affinity = getval (np);
		if (cpu_affinity == 0)
			cpu_affinity = original_affinity;
		SetThreadAffinityMask (GetCurrentThread (), cpu_affinity);
		return 2;
	}
	if (!_tcscmp (arg, _T("paffinity"))) {
		cpu_paffinity = getval (np);
		if (cpu_paffinity == 0)
			cpu_paffinity = original_affinity;
		SetProcessAffinityMask (GetCurrentProcess (), cpu_paffinity);
		return 2;
	}
	if (!_tcscmp (arg, _T("datapath"))) {
		ExpandEnvironmentStrings (np, start_path_data, sizeof start_path_data / sizeof (TCHAR));
		start_data = -1;
		return 2;
	}
	if (!_tcscmp (arg, _T("pluginpath"))) {
		ExpandEnvironmentStrings (np, start_path_plugins, sizeof start_path_plugins / sizeof (TCHAR));
		return 2;
	}
	if (!_tcscmp (arg, _T("maxmem"))) {
		maxmem = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("soundmodeskip"))) {
		sound_mode_skip = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("p96skipmode"))) {
		extern int p96skipmode;
		p96skipmode = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("minidumpmode"))) {
		minidumpmode = (MINIDUMP_TYPE)getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("jitevent"))) {
		pissoff_value = getval (np) * CYCLE_UNIT;
		return 2;
	}
	if (!_tcscmp (arg, _T("inputrecorddebug"))) {
		inputrecord_debug = getval (np);
		return 2;
	}
	if (!_tcscmp(arg, _T("extraframewait"))) {
		extraframewait = getval(np);
		return 2;
	}
	if (!_tcscmp(arg, _T("extraframewait_us"))) {
		extraframewait2 = getval(np);
		return 2;
	}
	if (!_tcscmp (arg, _T("framelatency"))) {
		forcedframelatency = getval (np);
		return 2;
	}
#ifdef RETROPLATFORM
	if (!_tcscmp (arg, _T("rphost"))) {
		rp_param = my_strdup (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("rpescapekey"))) {
		rp_rpescapekey = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("rpescapeholdtime"))) {
		rp_rpescapeholdtime = getval (np);
		return 2;
	}
	if (!_tcscmp (arg, _T("rpscreenmode"))) {
		rp_screenmode = getval (np);
		return 2;
	}
	if (!_tcscmp(arg, _T("rpinputmode"))) {
		rp_inputmode = getval(np);
		return 2;
	}
	if (!_tcscmp(arg, _T("hrtmon_keyboard"))) {
		hrtmon_lang = getval(np);
		return 2;
	}
#endif
	return 0;
}

static TCHAR **parseargstrings (TCHAR *s, TCHAR **xargv)
{
	int cnt, i, xargc;
	TCHAR **args;

	args = parseargstring (s);
	for (cnt = 0; args[cnt]; cnt++);
	for (xargc = 0; xargv[xargc]; xargc++);
	for (i = 0; i < cnt; i++) {
		TCHAR *arg = args[i];
		TCHAR *next = i + 1 < cnt ? args[i + 1] : NULL;
		TCHAR *next2 = i + 2 < cnt ? args[i + 2] : NULL;
		int v = parseargs (arg, next, next2);
		if (!v) {
			xargv[xargc++] = my_strdup (arg);
		} else if (v == 2) {
			i++;
		} else if (v < 0) {
			doquit = 1;
			return NULL;
		}
	}
	return args;
}

static int process_arg (TCHAR *cmdline, TCHAR **xargv, TCHAR ***xargv3)
{
	int i, xargc;
	TCHAR **argv;
	TCHAR tmp[MAX_DPATH];
	int fd, ok, added;

	*xargv3 = NULL;
	argv = parseargstring (cmdline);
	if (argv == NULL)
		return 0;
	added = 0;
	xargc = 0;
	xargv[xargc++] = my_strdup (executable_path);
	fd = 0;
	for (i = 0; argv[i]; i++) {
		// resolve .lnk paths
		const TCHAR *arg = argv[i];
		if (_tcslen(arg) > 4 && !_tcsicmp(arg + _tcslen(arg) - 4, _T(".lnk"))) {
			if (my_existsfile(arg)) {
				TCHAR s[MAX_DPATH];
				_tcscpy(s, arg);
				if (my_resolveshortcut(s, MAX_DPATH)) {
					xfree(argv[i]);
					argv[i] = my_strdup(s);
				}
			}
		}
	}

	for (i = 0; argv[i]; i++) {
		TCHAR *f = argv[i];
		ok = 0;
		if (f[0] != '-' && f[0] != '/') {
			int type = -1;
			struct zfile *z = zfile_fopen (f, _T("rb"), ZFD_NORMAL);
			if (z) {
				type = zfile_gettype (z);
				zfile_fclose (z);
			}
			tmp[0] = 0;
			switch (type)
			{
			case ZFILE_CONFIGURATION:
				_stprintf (tmp, _T("-config=%s"), f);
				break;
			case ZFILE_STATEFILE:
				_stprintf (tmp, _T("-statefile=%s"), f);
				break;
			case ZFILE_CDIMAGE:
				_stprintf (tmp, _T("-cdimage=%s"), f);
				break;
			case ZFILE_DISKIMAGE:
				if (fd < 4)
					_stprintf (tmp, _T("-cfgparam=floppy%d=%s"), fd++, f);
				break;
			}
			if (tmp[0]) {
				xfree (argv[i]);
				argv[i] = my_strdup (tmp);
				ok = 1;
				added = 1;
			}
		}
		if (!ok)
			break;
	}
	if (added) {
		for (i = 0; argv[i]; i++);
		argv[i++] = my_strdup (_T("-s"));
		argv[i++] = my_strdup (_T("use_gui=no"));
		argv[i] = NULL;
	}
	for (i = 0; argv[i]; i++) {
		TCHAR *arg = argv[i];
		TCHAR *next = argv[i + 1];
		TCHAR *next2 = next != NULL ? argv[i + 2] : NULL;
		int v = parseargs (arg, next, next2);
		if (!v) {
			xargv[xargc++] = my_strdup (arg);
		} else if (v == 2) {
			i++;
		} else if (v < 0) {
			doquit = 1;
			return 0;
		}
	}
#if 0
	argv = 0;
	argv[0] = 0;
#endif
	*xargv3 = argv;
	return xargc;
}

static TCHAR **WIN32_InitRegistry (TCHAR **argv)
{
	DWORD disposition;
	TCHAR tmp[MAX_DPATH];
	int size = sizeof tmp / sizeof (TCHAR);

	reginitializeinit (&inipath);
	hWinUAEKey = NULL;
	if (getregmode () == NULL || WINUAEPUBLICBETA > 0) {
		/* Create/Open the hWinUAEKey which points our config-info */
		RegCreateKeyEx (HKEY_CURRENT_USER, _T("Software\\Arabuusimiehet\\WinUAE"), 0, _T(""), REG_OPTION_NON_VOLATILE,
			KEY_WRITE | KEY_READ, NULL, &hWinUAEKey, &disposition);
		if (hWinUAEKey == NULL) {
			FILE *f;
			TCHAR *path;
			int tempfile;

			path = getdefaultini (&tempfile);
			f = _tfopen (path, _T("r"));
			if (!f)
				f = _tfopen (path, _T("w"));
			if (f) {
				fclose (f);
				reginitializeinit (&path);
			}
			xfree (path);
		}
	}
	if (regquerystr (NULL, _T("Commandline"), tmp, &size))
		return parseargstrings (tmp, argv);
	return NULL;
}

bool switchreginimode(void)
{
	TCHAR *path;
	const TCHAR *inipath = getregmode();
	if (inipath == NULL) {
		// reg -> ini
		FILE *f;
		int tempfile;

		path = getdefaultini(&tempfile);
		if (tempfile)
			return false;
		f = _tfopen (path, _T("w"));
		if (f) {
			fclose(f);
			return reginitializeinit(&path) != 0;
		}
	} else {
		// ini -> reg
		DeleteFile(inipath);
		path = NULL;
		reginitializeinit(&path);
		return true;
	}
	return false;
}

static const TCHAR *pipename = _T("\\\\.\\pipe\\WinUAE");

static bool singleprocess (void)
{
    DWORD mode, ret, avail;
	bool ok = false;
    TCHAR buf[1000];

	HANDLE p = CreateFile(
		pipename,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	if (p == INVALID_HANDLE_VALUE)
		return false;
	mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(p, &mode, NULL, NULL))
		goto end;
	buf[0] = 0xfeff;
	_tcscpy (buf + 1, _T("IPC_QUIT"));
	if (!WriteFile(p, (void*)buf, (_tcslen (buf) + 1) * sizeof (TCHAR), &ret, NULL))
		goto end;
	if (!PeekNamedPipe(p, NULL, 0, NULL, &avail, NULL))
		goto end;
    if (!ReadFile(p, buf, sizeof buf, &ret, NULL))
		goto end;
	ok = true;
end:
	CloseHandle(p);
	return ok;
}

static int PASCAL WinMain2 (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	HANDLE hMutex;
	TCHAR **argv = NULL, **argv2 = NULL, **argv3;
	int argc, i;

	if (!osdetect ())
		return 0;

	if (os_vista) {
		max_uae_width = 8192;
		max_uae_height = 8192;
	} else {
		max_uae_width = 3072;
		max_uae_height = 2048;
	}

	hInst = hInstance;
	hMutex = CreateMutex (NULL, FALSE, _T("WinUAE Instantiated")); // To tell the installer we're running

	//singleprocess ();

	argv = xcalloc (TCHAR*, MAX_ARGUMENTS);
	argv3 = NULL;
	argc = process_arg (lpCmdLine, argv, &argv3);
	if (doquit)
		return 0;

	argv2 = WIN32_InitRegistry (argv);

	if (regqueryint (NULL, _T("log_disabled"), &i)) {
		if (i)
			logging_disabled = true;
	}

	getstartpaths ();
	makeverstr (VersionStr);

	logging_init ();
	if (_tcslen (lpCmdLine) > 0)
		write_log (_T("'%s'\n"), lpCmdLine);
	if (argv3 && argv3[0]) {
		write_log (_T("params:\n"));
		for (i = 0; argv3[i]; i++)
			write_log (_T("%d: '%s'\n"), i + 1, argv3[i]);
	}
	if (argv2) {
		write_log (_T("extra params:\n"));
		for (i = 0; argv2[i]; i++)
			write_log (_T("%d: '%s'\n"), i + 1, argv2[i]);
	}
	if (preinit_shm () && WIN32_RegisterClasses () && WIN32_InitLibraries ()) {
		DWORD i;

#ifdef RETROPLATFORM
		if (rp_param != NULL) {
			if (FAILED (rp_init ()))
				goto end;
		}
#endif
		WIN32_HandleRegistryStuff ();
		write_log (_T("Enumerating display devices.. \n"));
		enumeratedisplays ();
		write_log (_T("Sorting devices and modes..\n"));
		sortdisplays ();
		enumerate_sound_devices ();
		for (i = 0; i < MAX_SOUND_DEVICES && sound_devices[i]; i++) {
			int type = sound_devices[i]->type;
			write_log (_T("%d:%s: %s\n"), i, type == SOUND_DEVICE_XAUDIO2 ? _T("XA") : (type == SOUND_DEVICE_DS ? _T("DS") : (type == SOUND_DEVICE_AL ? _T("AL") : (type == SOUND_DEVICE_WASAPI ? _T("WA") : (type == SOUND_DEVICE_WASAPI_EXCLUSIVE ? _T("WX") : _T("PA"))))), sound_devices[i]->name);
		}
		write_log (_T("Enumerating recording devices:\n"));
		for (i = 0; i < MAX_SOUND_DEVICES && record_devices[i]; i++) {
			int type = record_devices[i]->type;
			write_log (_T("%d:%s: %s\n"), i,  type == SOUND_DEVICE_XAUDIO2 ? _T("XA") : (type == SOUND_DEVICE_DS ? _T("DS") : (type == SOUND_DEVICE_AL ? _T("AL") : (type == SOUND_DEVICE_WASAPI ? _T("WA") : (type == SOUND_DEVICE_WASAPI_EXCLUSIVE ? _T("WX") : _T("PA"))))), record_devices[i]->name);
		}
		write_log (_T("done\n"));
#if 0
		DEVMODE devmode;
		memset (&devmode, 0, sizeof (devmode));
		devmode.dmSize = sizeof (DEVMODE);
		if (EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &devmode)) {
			default_freq = devmode.dmDisplayFrequency;
			if (default_freq >= 70)
				default_freq = 70;
			else
				default_freq = 60;
		}
#endif
		WIN32_InitLang ();
		WIN32_InitHtmlHelp ();
		DirectDraw_Release ();
		unicode_init ();
		can_D3D11(false);
		if (betamessage ()) {
			keyboard_settrans ();
#ifdef CATWEASEL
			catweasel_init ();
#endif
#ifdef PARALLEL_PORT
			paraport_mask = paraport_init ();
#endif
			globalipc = createIPC (_T("WinUAE"), 0);
			shmem_serial_create();
			enumserialports ();
			enummidiports ();
			real_main (argc, argv);
		}
	}
end:
	closeIPC (globalipc);
	shmem_serial_delete();
	write_disk_history ();
	timeend ();
#ifdef AVIOUTPUT
	AVIOutput_Release ();
#endif
#ifdef AHI
	ahi_close_sound ();
#endif
#ifdef PARALLEL_PORT
	paraport_free ();
	closeprinter ();
#endif
	create_afnewdir (1);
#ifdef RETROPLATFORM
	rp_free ();
#endif
	CloseHandle (hMutex);
	WIN32_CleanupLibraries ();
	WIN32_UnregisterClasses ();
#ifdef _DEBUG
	// show memory leaks
	//_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
	close_console();
	if (hWinUAEKey)
		RegCloseKey(hWinUAEKey);
	_fcloseall();
	for (i = 0; i < argc; i++)
		xfree (argv[i]);
	xfree (argv);
	if (argv2) {
		for (i = 0; argv2[i]; i++)
			xfree (argv2[i]);
		xfree (argv2);
	}
	for (i = 0; argv3 && argv3[i]; i++)
		xfree (argv3[i]);
	xfree (argv3);
	return FALSE;
}

#if 0
int execute_command (TCHAR *cmd)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	memset (&si, 0, sizeof (si));
	si.cb = sizeof (si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	if(CreateProcess(NULL, cmd, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))  {
		WaitForSingleObject(pi.hProcess, INFINITE);
		return 1;
	}
	return 0;
}
#endif

#include "driveclick.h"
static const int drvsampleres[] = {
	IDR_DRIVE_CLICK_A500_1, DS_CLICK,
	IDR_DRIVE_SPIN_A500_1, DS_SPIN,
	IDR_DRIVE_SPINND_A500_1, DS_SPINND,
	IDR_DRIVE_STARTUP_A500_1, DS_START,
	IDR_DRIVE_SNATCH_A500_1, DS_SNATCH,
	-1
};
int driveclick_loadresource (struct drvsample *sp, int drivetype)
{
	int i, ok;

	ok = 1;
	for (i = 0; drvsampleres[i] >= 0; i += 2) {
		struct drvsample *s = sp + drvsampleres[i + 1];
		HRSRC res = FindResource (NULL, MAKEINTRESOURCE (drvsampleres[i + 0]), _T("WAVE"));
		if (res != 0) {
			HANDLE h = LoadResource (NULL, res);
			int len = SizeofResource (NULL, res);
			uae_u8 *p = (uae_u8*)LockResource (h);
			s->p = decodewav (p, &len);
			s->len = len;
		} else {
			ok = 0;
		}
	}
	return ok;
}

typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType,
	CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

static void savedump (MINIDUMPWRITEDUMP dump, HANDLE f, struct _EXCEPTION_POINTERS *pExceptionPointers)
{
	MINIDUMP_EXCEPTION_INFORMATION exinfo;
	MINIDUMP_USER_STREAM_INFORMATION musi, *musip;
	MINIDUMP_USER_STREAM mus[3], *musp;
	uae_u8 *log;
	int len;

	musip = NULL;
	musi.UserStreamArray = mus;
	musi.UserStreamCount = 0;

	len = 30000;
	log = save_log (TRUE, &len);
	if (log) {
		musp = &mus[musi.UserStreamCount];
		musi.UserStreamCount++;
		musp->Type = LastReservedStream + musi.UserStreamCount;
		musp->Buffer = log;
		musp->BufferSize = len;
		len = 30000;
		log = save_log (FALSE, &len);
		if (log) {
			musp = &mus[musi.UserStreamCount];
			musi.UserStreamCount++;
			musp->Type = LastReservedStream + musi.UserStreamCount;
			musp->Buffer = log;
			musp->BufferSize = len;
		}
	}

	const TCHAR *config = cfgfile_getconfigdata(&len);
	if (config && len > 0) {
		musp = &mus[musi.UserStreamCount];
		musi.UserStreamCount++;
		musp->Type = LastReservedStream + musi.UserStreamCount;
		musp->Buffer = (void*)config;
		musp->BufferSize = len;
	}

	if (musi.UserStreamCount > 0)
		musip = &musi;

	exinfo.ThreadId = GetCurrentThreadId ();
	exinfo.ExceptionPointers = pExceptionPointers;
	exinfo.ClientPointers = 0;
	dump (GetCurrentProcess (), GetCurrentProcessId (), f, minidumpmode, &exinfo, musip, NULL);
}

static void create_dump (struct _EXCEPTION_POINTERS *pExceptionPointers)
{
	TCHAR path[MAX_DPATH];
	TCHAR path2[MAX_DPATH];
	TCHAR msg[1024];
	TCHAR *p;
	HMODULE dll = NULL;
	struct tm when;
	__time64_t now;

	if (GetModuleFileName (NULL, path, MAX_DPATH)) {
		TCHAR dumpfilename[100];
		TCHAR beta[100];
		TCHAR path3[MAX_DPATH];
		TCHAR *slash = _tcsrchr (path, '\\');
		_time64 (&now);
		when = *_localtime64 (&now);
		_tcscpy (path2, path);
		if (slash) {
			_tcscpy (slash + 1, _T("DBGHELP.DLL"));
			dll = WIN32_LoadLibrary (path);
		}
		slash = _tcsrchr (path2, '\\');
		if (slash)
			p = slash + 1;
		else
			p = path2;
		p[0] = 0;
		beta[0] = 0;
		if (WINUAEPUBLICBETA > 0)
			_stprintf (beta, _T("b%s"), WINUAEBETA);
		_stprintf (dumpfilename, _T("winuae%s_%d.%d.%d_%s_%d.%02d.%02d_%02d.%02d.%02d.dmp"),
#ifdef _WIN64
			_T("_x64"),
#else
			_T(""),
#endif
			UAEMAJOR, UAEMINOR, UAESUBREV, beta[0] ? beta : _T("R"),
			when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);
		if (dll == NULL)
			dll = WIN32_LoadLibrary (_T("DBGHELP.DLL"));
		if (dll) {
			all_events_disabled = 1;
			MINIDUMPWRITEDUMP dump = (MINIDUMPWRITEDUMP)GetProcAddress (dll, "MiniDumpWriteDump");
			if (dump) {
				_tcscpy (path3, path2);
				_tcscat (path3, dumpfilename);
				HANDLE f = CreateFile (path3, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (f == INVALID_HANDLE_VALUE) {
					_tcscpy (path3, start_path_data);
					_tcscat (path3, dumpfilename);
					f = CreateFile (path3, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				}
				if (f == INVALID_HANDLE_VALUE) {
					if (GetTempPath (MAX_DPATH, path3) > 0) {
						_tcscat (path3, dumpfilename);
						f = CreateFile (path3, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
					}
				}
				if (f != INVALID_HANDLE_VALUE) {
					flush_log ();
					savedump (dump, f, pExceptionPointers);
					CloseHandle (f);
					ClipCursor(NULL);
					ReleaseCapture();
					ShowCursor(TRUE);
					if (debugfile)
						log_close(debugfile);
					if (isfullscreen () <= 0) {
						_stprintf (msg, _T("Crash detected. MiniDump saved as:\n%s\n"), path3);
						MessageBox (NULL, msg, _T("Crash"), MB_OK | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND);
					}
					ExitProcess(0);
				}
			}
			all_events_disabled = 0;
		}
	}
}

#if defined(_WIN64)

LONG WINAPI WIN32_ExceptionFilter (struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
	write_log (_T("EVALEXCEPTION %08x!\n"), ec);
	create_dump  (pExceptionPointers);
	return EXCEPTION_CONTINUE_SEARCH;
}
#else

#if 0
#include <errorrep.h>
#endif

/* Gah, don't look at this crap, please.. */
static void efix (DWORD *regp, void *p, void *ps, int *got)
{
	DWORD reg = *regp;
	if (p >= (void*)reg && p < (void*)(reg + 32)) {
		*regp = (DWORD)ps;
		*got = 1;
	}
}


LONG WINAPI WIN32_ExceptionFilter (struct _EXCEPTION_POINTERS *pExceptionPointers, DWORD ec)
{
	static uae_u8 *prevpc;
	LONG lRet = EXCEPTION_CONTINUE_SEARCH;
	PEXCEPTION_RECORD er = pExceptionPointers->ExceptionRecord;
	PCONTEXT ctx = pExceptionPointers->ContextRecord;

#if 0
	if (ec >= EXCEPTION_FLT_DENORMAL_OPERAND && ec <= EXCEPTION_FLT_UNDERFLOW) {
		extern void fpp_setexcept (uae_u16);
		if (ec == EXCEPTION_FLT_INEXACT_RESULT)
			fpp_setexcept (0x0100 | 0x0200);
		else if (ec == EXCEPTION_FLT_OVERFLOW)
			fpp_setexcept (0x1000);
		else if (ec == EXCEPTION_FLT_UNDERFLOW)
			fpp_setexcept (0x0800);
		else if (ec == EXCEPTION_FLT_DIVIDE_BY_ZERO)
			fpp_setexcept (0x0400);
		return EXCEPTION_CONTINUE_EXECUTION;
	}
#endif
	/* Check possible access violation in 68010+/compatible mode disabled if PC points to non-existing memory */
#if 1
	if (ec == EXCEPTION_ACCESS_VIOLATION && !er->ExceptionFlags &&
		er->NumberParameters >= 2 && !er->ExceptionInformation[0] && regs.pc_p) {
			void *p = (void*)er->ExceptionInformation[1];
			write_log (_T("ExceptionFilter Trap: %p %p %p\n"), p, regs.pc_p, prevpc);
			if ((p >= (void*)regs.pc_p && p < (void*)(regs.pc_p + 32))
				|| (p >= (void*)prevpc && p < (void*)(prevpc + 32))) {
					int got = 0;
					uaecptr opc = m68k_getpc ();
					void *ps = get_real_address (0);
					m68k_dumpstate(NULL, 0xffffffff);
					efix (&ctx->Eax, p, ps, &got);
					efix (&ctx->Ebx, p, ps, &got);
					efix (&ctx->Ecx, p, ps, &got);
					efix (&ctx->Edx, p, ps, &got);
					efix (&ctx->Esi, p, ps, &got);
					efix (&ctx->Edi, p, ps, &got);
					write_log (_T("Access violation! (68KPC=%08X HOSTADDR=%p)\n"), M68K_GETPC, p);
					if (got == 0) {
						write_log (_T("failed to find and fix the problem (%p). crashing..\n"), p);
					} else {
						void *ppc = regs.pc_p;
						m68k_setpc (0);
						if (ppc != regs.pc_p) {
							prevpc = (uae_u8*)ppc;
						}
						m68k_setpc ((uaecptr)p);
						exception2(opc, er->ExceptionInformation[0] == 0, 4, regs.s ? 4 : 0);
						lRet = EXCEPTION_CONTINUE_EXECUTION;
					}
			}
	}
#endif
#ifndef	_DEBUG
	if (lRet == EXCEPTION_CONTINUE_SEARCH)
		create_dump  (pExceptionPointers);
#endif
#if 0
	HMODULE hFaultRepDll = LoadLibrary (_T("FaultRep.dll")) ;
	if (hFaultRepDll) {
		pfn_REPORTFAULT pfn = (pfn_REPORTFAULT)GetProcAddress (hFaultRepDll, _T("ReportFault"));
		if (pfn) {
			EFaultRepRetVal rc = pfn (pExceptionPointers, 0);
			lRet = EXCEPTION_EXECUTE_HANDLER;
		}
		FreeLibrary (hFaultRepDll );
	}
#endif
	return lRet ;
}

#endif

void addnotifications (HWND hwnd, int remove, int isgui)
{
	static ULONG ret;
	static HDEVNOTIFY hdn1, hdn2, hdn3;
	static int wtson;

	if (remove) {
		if (ret > 0)
			SHChangeNotifyDeregister (ret);
		ret = 0;
		if (hdn1)
			UnregisterDeviceNotification (hdn1);
		if (hdn2)
			UnregisterDeviceNotification (hdn2);
		if (hdn3)
			UnregisterDeviceNotification (hdn3);
		hdn1 = 0;
		hdn2 = 0;
		hdn3 = 0;
		if (wtson && !isgui)
			WTSUnRegisterSessionNotification (hwnd);
		wtson = 0;
	} else {
		DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = { 0 };
		SHChangeNotifyEntry shCNE = { 0 };
		shCNE.pidl = NULL;
		shCNE.fRecursive = TRUE;
		ret = SHChangeNotifyRegister (hwnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
			SHCNE_MEDIAREMOVED | SHCNE_MEDIAINSERTED | SHCNE_DRIVEREMOVED | SHCNE_DRIVEADD,
			WM_USER + 2, 1, &shCNE);
		NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
		NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_KEYBOARD;
		hdn1 = RegisterDeviceNotification (hwnd,  &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
		NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_MOUSE;
		hdn2 = RegisterDeviceNotification (hwnd,  &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
		NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
		hdn3 = RegisterDeviceNotification (hwnd,  &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
		if (!isgui)
			wtson = WTSRegisterSessionNotification (hwnd, NOTIFY_FOR_THIS_SESSION);
	}
}

void registertouch(HWND hwnd)
{
#if TOUCH_SUPPORT
	REGISTERTOUCHWINDOW pRegisterTouchWindow;

	if (!os_touch)
		return;
	pRegisterTouchWindow = (REGISTERTOUCHWINDOW)GetProcAddress(
		GetModuleHandle (_T("user32.dll")), "RegisterTouchWindow");
	pGetTouchInputInfo = (GETTOUCHINPUTINFO)GetProcAddress(
		GetModuleHandle (_T("user32.dll")), "GetTouchInputInfo");
	pCloseTouchInputHandle = (CLOSETOUCHINPUTHANDLE)GetProcAddress(
		GetModuleHandle (_T("user32.dll")), "CloseTouchInputHandle");
	if (!pRegisterTouchWindow || !pGetTouchInputInfo || !pCloseTouchInputHandle)
		return;
	if (!pRegisterTouchWindow(hwnd, 0)) {
		write_log(_T("RegisterTouchWindow error: %d\n"), GetLastError());
	}
#endif
}

void systray (HWND hwnd, int remove)
{
	static const GUID iconguid = { 0xdac2e99b, 0xe8f6, 0x4150, { 0x98, 0x46, 0xd, 0x4a, 0x61, 0xfb, 0xdd, 0x03 } };
	NOTIFYICONDATA nid;
	BOOL v;
	static bool noguid;

	if (!remove && currprefs.win32_nonotificationicon)
		return;

#ifdef RETROPLATFORM
	if (rp_isactive ())
		return;
#endif
	bool canguid = !noguid && os_win7;
	//write_log (_T("notif: systray(%x,%d)\n"), hwnd, remove);
	if (!remove) {
		if (!TaskbarRestart)
			TaskbarRestart = RegisterWindowMessage (_T("TaskbarCreated"));
		TaskbarRestartHWND = hwnd;
		//write_log (_T("notif: taskbarrestart = %d\n"), TaskbarRestart);
	} else {
		TaskbarRestart = 0;
		hwnd = TaskbarRestartHWND;
	}
	if (!hwnd)
		return;
	memset (&nid, 0, sizeof (nid));
	nid.cbSize = sizeof (nid);
	nid.hWnd = hwnd;
	nid.hIcon = LoadIcon (hInst, (LPCWSTR)MAKEINTRESOURCE (IDI_APPICON));
	nid.uFlags = NIF_ICON | NIF_MESSAGE | (canguid ? NIF_GUID : 0);
	nid.uCallbackMessage = WM_USER + 1;
	nid.uVersion = os_win7 ? NOTIFYICON_VERSION_4 : NOTIFYICON_VERSION;
	nid.dwInfoFlags = NIIF_USER;
	_tcscpy(nid.szInfo, _T("WinUAE"));
	_tcscpy(nid.szInfoTitle, _T("WinUAE"));
	nid.hBalloonIcon = nid.hIcon;
	if (canguid) {
		nid.guidItem = iconguid;
		if (!remove) {
			// if guid identifier: always remove first.
			// old icon may not have been removed due to crash etc
			Shell_NotifyIcon(NIM_DELETE, &nid);
		}
	}
	v = Shell_NotifyIcon (remove ? NIM_DELETE : NIM_ADD, &nid);
	if (!remove && !v && !noguid) {
		noguid = true;
		write_log(_T("Notify error2 = %x %d\n"), GetLastError(), remove);
		return systray(hwnd, remove);
	}

	//write_log (_T("notif: Shell_NotifyIcon returned %d\n"), v);
	if (v) {
		if (remove) {
			TaskbarRestartHWND = NULL;
		} else {
			v = Shell_NotifyIcon(NIM_SETVERSION, &nid);
		}
	} else {
		write_log (_T("Notify error = %x %d\n"), GetLastError(), remove);
	}
}

static void systraymenu (HWND hwnd)
{
	POINT pt;
	HMENU menu, menu2, drvmenu, cdmenu;
	int drvs[] = { ID_ST_DF0, ID_ST_DF1, ID_ST_DF2, ID_ST_DF3, -1 };
	int i;
	TCHAR text[100], text2[100];

	WIN32GUI_LoadUIString (IDS_STMENUNOFLOPPY, text, sizeof (text) / sizeof (TCHAR));
	WIN32GUI_LoadUIString (IDS_STMENUNOCD, text2, sizeof (text2) / sizeof (TCHAR));
	GetCursorPos (&pt);
	menu = LoadMenu (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDM_SYSTRAY));
	if (!menu)
		return;
	menu2 = GetSubMenu (menu, 0);
	drvmenu = GetSubMenu (menu2, 1);
	cdmenu = GetSubMenu (menu2, 2);
	EnableMenuItem (menu2, ID_ST_HELP, pHtmlHelp ? MF_ENABLED : MF_GRAYED);
	i = 0;
	while (drvs[i] >= 0) {
		TCHAR s[MAX_DPATH];
		if (currprefs.floppyslots[i].df[0])
			_stprintf (s, _T("DF%d: [%s]"), i, currprefs.floppyslots[i].df);
		else
			_stprintf (s, _T("DF%d: [%s]"), i, text);
		ModifyMenu (drvmenu, drvs[i], MF_BYCOMMAND | MF_STRING, drvs[i], s);
		EnableMenuItem (menu2, drvs[i], currprefs.floppyslots[i].dfxtype < 0 ? MF_GRAYED : MF_ENABLED);
		i++;
	}
	{
		TCHAR s[MAX_DPATH];
		if (currprefs.cdslots[0].inuse && currprefs.cdslots[0].name[0])
			_stprintf (s, _T("CD: [%s]"), currprefs.cdslots[0].name);
		else
			_stprintf (s, _T("CD: [%s]"), text2);
		ModifyMenu (cdmenu, ID_ST_CD0, MF_BYCOMMAND | MF_STRING, ID_ST_CD0, s);
		int open = 0;
		struct device_info di;
		if (sys_command_info (0, &di, 1) && di.open)
			open = 1;
		EnableMenuItem (menu2, ID_ST_CD0, open == 0 ? MF_GRAYED : MF_ENABLED);
	}


	if (isfullscreen () <= 0)
		SetForegroundWindow (hwnd);
	TrackPopupMenu (menu2, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
		pt.x, pt.y, 0, hwnd, NULL);
	PostMessage (hwnd, WM_NULL, 0, 0);
	DestroyMenu (menu);
}

static void LLError (HMODULE m, const TCHAR *s)
{
	DWORD err;

	if (m) {
		//	write_log (_T("'%s' opened\n"), s);
		return;
	}
	err = GetLastError ();
	if (err == ERROR_MOD_NOT_FOUND || err == ERROR_DLL_NOT_FOUND)
		return;
	write_log (_T("%s failed to open %d\n"), s, err);
}

HMODULE WIN32_LoadLibrary_2 (const TCHAR *name, int expand)
{
	HMODULE m = NULL;
	TCHAR *newname;
	DWORD err = -1;
	int round;

	newname = xmalloc (TCHAR, _tcslen (name) + 1 + 10);
	if (!newname)
		return NULL;
	for (round = 0; round < 6; round++) {
		TCHAR s[MAX_DPATH], dir[MAX_DPATH], dir2[MAX_DPATH];
		_tcscpy (newname, name);
#ifdef CPU_64_BIT
		TCHAR *p = NULL;
		switch(round)
		{
		case 0:
			p = _tcsstr (newname, _T("32"));
			if (p) {
				p[0] = '6';
				p[1] = '4';
			}
			break;
		case 1:
			p = _tcschr (newname, '.');
			if (p) {
				_tcscpy(p, _T("_x64"));
				_tcscat(p, _tcschr(name, '.'));
			}
			break;
		case 2:
			p = _tcschr (newname, '.');
			if (p) {
				_tcscpy(p, _T("x64"));
				_tcscat(p, _tcschr(name, '.'));
			}
			break;
		case 3:
			p = _tcschr (newname, '.');
			if (p) {
				_tcscpy(p, _T("_64"));
				_tcscat(p, _tcschr(name, '.'));
			}
			break;
		case 4:
			p = _tcschr (newname, '.');
			if (p) {
				_tcscpy(p, _T("64"));
				_tcscat(p, _tcschr(name, '.'));
			}
			break;
		case 5:
			p = newname;
			break;
		}
		if (!p)
			continue;
#endif
		get_plugin_path (s, sizeof s / sizeof (TCHAR), NULL);
		_tcscat (s, newname);
		GetDllDirectory(sizeof(dir2) / sizeof TCHAR, dir2);
		getpathpart(dir, sizeof(dir) / sizeof TCHAR, s);
		stripslashes(dir);
		if (dir[0])
			SetDllDirectory(dir);
		m = LoadLibrary (s);
		LLError (m ,s);
		if (m) {
			if (dir2[0])
				SetDllDirectory(dir2);
			goto end;
		}
		m = LoadLibrary (newname);
		LLError (m, newname);
		if (dir2[0])
			SetDllDirectory(dir2);
		if (m)
			goto end;
#ifndef CPU_64_BIT
		break;
#endif
	}
end:
	xfree (newname);
	return m;
}
HMODULE WIN32_LoadLibrary (const TCHAR *name)
{
	return WIN32_LoadLibrary_2 (name, TRUE);
}

int isdllversion (const TCHAR *name, int version, int revision, int subver, int subrev)
{
	DWORD  dwVersionHandle, dwFileVersionInfoSize;
	LPVOID lpFileVersionData = NULL;
	int ok = 0;
	
	dwFileVersionInfoSize = GetFileVersionInfoSize (name, &dwVersionHandle);
	if (dwFileVersionInfoSize) {
		if (lpFileVersionData = xcalloc (uae_u8, dwFileVersionInfoSize)) {
			if (GetFileVersionInfo (name, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData)) {
				VS_FIXEDFILEINFO *vsFileInfo = NULL;
				UINT uLen;
				if (VerQueryValue (lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen)) {
					if (vsFileInfo) {
						uae_u64 v1 = ((uae_u64)vsFileInfo->dwProductVersionMS << 32) | vsFileInfo->dwProductVersionLS;
						uae_u64 v2 = ((uae_u64)version << 48) | ((uae_u64)revision << 32) | (subver << 16) | (subrev << 0);
						write_log (_T("%s %d.%d.%d.%d\n"), name,
							HIWORD (vsFileInfo->dwProductVersionMS), LOWORD (vsFileInfo->dwProductVersionMS),
							HIWORD (vsFileInfo->dwProductVersionLS), LOWORD (vsFileInfo->dwProductVersionLS));
						if (v1 >= v2)
							ok = 1;
					}
				}
			}
			xfree (lpFileVersionData);
		}
	}
	return ok;
}

int get_guid_target (uae_u8 *out)
{
	GUID guid;

	if (CoCreateGuid (&guid) != S_OK)
		return 0;
	out[0] = guid.Data1 >> 24;
	out[1] = (uae_u8)(guid.Data1 >> 16);
	out[2] = (uae_u8)(guid.Data1 >>  8);
	out[3] = (uae_u8)(guid.Data1 >>  0);
	out[4] = guid.Data2 >>  8;
	out[5] = guid.Data2 >>  0;
	out[6] = guid.Data3 >>  8;
	out[7] = guid.Data3 >>  0;
	memcpy (out + 8, guid.Data4, 8);
	return 1;
}

typedef HRESULT (CALLBACK* SHCREATEITEMFROMPARSINGNAME)
	(PCWSTR,IBindCtx*,REFIID,void**); // Vista+ only

void target_getdate(int *y, int *m, int *d)
{
	*y = GETBDY(WINUAEDATE);
	*m = GETBDM(WINUAEDATE);
	*d = GETBDD(WINUAEDATE);
}

void target_addtorecent (const TCHAR *name, int t)
{
	TCHAR tmp[MAX_DPATH];

	if (name == NULL || name[0] == 0)
		return;
	tmp[0] = 0;
	GetFullPathName (name, sizeof tmp / sizeof (TCHAR), tmp, NULL);
	if (os_win7) {
		SHCREATEITEMFROMPARSINGNAME pSHCreateItemFromParsingName;
		SHARDAPPIDINFO shard;
		pSHCreateItemFromParsingName = (SHCREATEITEMFROMPARSINGNAME)GetProcAddress (
			GetModuleHandle (_T("shell32.dll")), "SHCreateItemFromParsingName");
		if (!pSHCreateItemFromParsingName)
			return;
		shard.pszAppID = WINUAEAPPNAME;
		if (SUCCEEDED (pSHCreateItemFromParsingName (tmp, NULL, IID_IShellItem, (void**)&shard.psi))) {
			SHAddToRecentDocs (SHARD_APPIDINFO, &shard);
			shard.psi->Release();
		}
	} else {
		SHAddToRecentDocs (SHARD_PATH, tmp);
	}
}


void target_reset (void)
{
	clipboard_reset ();
}

uae_u32 emulib_target_getcpurate (uae_u32 v, uae_u32 *low)
{
	*low = 0;
	if (v == 1) {
		LARGE_INTEGER pf;
		pf.QuadPart = 0;
		QueryPerformanceFrequency (&pf);
		*low = pf.LowPart;
		return pf.HighPart;
	} else if (v == 2) {
		LARGE_INTEGER pf;
		pf.QuadPart = 0;
		QueryPerformanceCounter (&pf);
		*low = pf.LowPart;
		return pf.HighPart;
	}
	return 0;
}

bool target_can_autoswitchdevice(void)
{
#ifdef RETROPLATFORM
	if (rp_isactive ())
		return false;
#endif
	if (!ismouseactive())
		return false;
	return true;
}

void fpux_save (int *v)
{
#ifndef _WIN64
	*v = _controlfp (0, 0);
	_controlfp (fpucontrol, _MCW_IC | _MCW_RC | _MCW_PC);
#endif
}
void fpux_restore (int *v)
{
#ifndef _WIN64
	if (v)
		_controlfp (*v, _MCW_IC | _MCW_RC | _MCW_PC);
#endif
}

struct winuae	//this struct is put in a6 if you call
	//execute native function
{
	HWND amigawnd;    //address of amiga Window Windows Handle
	unsigned int changenum;   //number to detect screen close/open
	unsigned int z3offset;    //the offset to add to acsess Z3 mem from Dll side
};

void *uaenative_get_uaevar (void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	static struct winuae uaevar;
#ifdef _WIN32
    uaevar.amigawnd = mon->hAmigaWnd;
#endif
    uaevar.z3offset = (uae_u32)get_real_address (z3fastmem_bank[0].start) - z3fastmem_bank[0].start;
    return &uaevar;
}

const TCHAR **uaenative_get_library_dirs (void)
{
	static const TCHAR **nats;
	static TCHAR *path;

	if (nats == NULL)
		nats = xcalloc (const TCHAR*, 3);
	if (path == NULL) {
		path = xcalloc (TCHAR, MAX_DPATH);
		_tcscpy (path, start_path_data);
		_tcscat (path, _T("plugins"));
	}
	nats[0] = start_path_data;
	nats[1] = path;
	return nats;
}

bool is_mainthread(void)
{
	return GetCurrentThreadId() == mainthreadid;
}

typedef BOOL (CALLBACK* CHANGEWINDOWMESSAGEFILTER)(UINT, DWORD);

#ifndef NDEBUG
typedef BOOL(WINAPI* SETPROCESSMITIGATIONPOLICY)(DWORD, PVOID, SIZE_T);
static SETPROCESSMITIGATIONPOLICY pSetProcessMitigationPolicy;
#endif

int PASCAL wWinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	DWORD_PTR sys_aff;
	HANDLE thread;

#if 0
#ifdef _DEBUG
	{
		int tmp = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
		//tmp &= 0xffff;
		tmp |= _CRTDBG_CHECK_ALWAYS_DF;
		tmp |= _CRTDBG_CHECK_CRT_DF;
#ifdef MEMDEBUG
		tmp |=_CRTDBG_CHECK_EVERY_16_DF;
		tmp |= _CRTDBG_DELAY_FREE_MEM_DF;
#endif
		_CrtSetDbgFlag(tmp);
	}
#endif
#endif

#ifndef NDEBUG
	PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY p = { 0 };
	p.HandleExceptionsPermanentlyEnabled = 1;
	p.RaiseExceptionOnInvalidHandleReference = 1;
	//ProcessStrictHandleCheckPolicy = 3
	pSetProcessMitigationPolicy = (SETPROCESSMITIGATIONPOLICY)GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "SetProcessMitigationPolicy");
	pSetProcessMitigationPolicy(3, &p, sizeof p);
#endif

	executable_path[0] = 0;
	GetModuleFileName(NULL, executable_path, sizeof executable_path / sizeof(TCHAR));

	SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
	currprefs.win32_filesystem_mangle_reserved_names = true;
	SetDllDirectory (_T(""));
	/* Make sure we do an InitCommonControls() to get some advanced controls */
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	InitCommonControls ();

	original_affinity = 1;
	GetProcessAffinityMask (GetCurrentProcess (), &original_affinity, &sys_aff);

	thread = GetCurrentThread ();
	mainthreadid = GetCurrentThreadId();
	//original_affinity = SetThreadAffinityMask(thread, 1);
	fpucontrol = _controlfp (0, 0) & (_MCW_IC | _MCW_RC | _MCW_PC);
	_tzset ();

#if 0
#define MSGFLT_ADD 1
	CHANGEWINDOWMESSAGEFILTER pChangeWindowMessageFilter;
	pChangeWindowMessageFilter = (CHANGEWINDOWMESSAGEFILTER)GetProcAddress(
		GetModuleHandle(_T("user32.dll")), _T("ChangeWindowMessageFilter"));
	if (pChangeWindowMessageFilter)
		pChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
#endif

	log_open (NULL, 0, -1, NULL);

#ifdef NDEBUG
	__try {
#endif
		WinMain2 (hInstance, hPrevInstance, lpCmdLine, nCmdShow);
#ifdef NDEBUG
	} __except(WIN32_ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
	}
#endif
	//SetThreadAffinityMask (thread, original_affinity);
	return FALSE;
}


