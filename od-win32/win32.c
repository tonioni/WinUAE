/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997-1998 Mathias Ortmann
 * Copyright 1997-1999 Brian King
 */

//#define MEMDEBUG

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#define _WIN32_WINNT 0x600 /* XButtons + MOUSEHWHEEL */

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
#include <dbghelp.h>

#include "sysdeps.h"
#include "options.h"
#include "audio.h"
#include "sound.h"
#include "uae.h"
#include "memory.h"
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
#include "resource.h"
#include "autoconf.h"
#include "gui.h"
#include "sys/mman.h"
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
#ifdef RETROPLATFORM
#include "rp.h"
#endif

extern int harddrive_dangerous, do_rdbdump, aspi_allow_all, no_rawinput, rawkeyboard;
int log_scsi, log_net, uaelib_debug;
int pissoff_value = 25000;

extern FILE *debugfile;
extern int console_logging;
static OSVERSIONINFO osVersion;
static SYSTEM_INFO SystemInfo;
static int logging_started;
static DWORD minidumpmode = MiniDumpNormal;

int qpcdivisor = 0;
int cpu_mmx = 1;
static int userdtsc = 0;

HINSTANCE hInst = NULL;
HMODULE hUIDLL = NULL;
HWND (WINAPI *pHtmlHelp)(HWND, LPCSTR, UINT, LPDWORD) = NULL;
HWND hAmigaWnd, hMainWnd, hHiddenWnd, hGUIWnd;
RECT amigawin_rect;
static int mouseposx, mouseposy;
static UINT TaskbarRestart;
static HWND TaskbarRestartHWND;
static int forceroms;
static int start_data = 0;

char VersionStr[256];
char BetaStr[64];
extern int path_type;

int in_sizemove;
int manual_painting_needed;
int manual_palette_refresh_needed;
int win_x_diff, win_y_diff;

int toggle_sound;
int paraport_mask;

HKEY hWinUAEKey = NULL;
COLORREF g_dwBackgroundColor;

static int activatemouse = 1;
int ignore_messages_all;
int pause_emulation;

static int didmousepos;
static int sound_closed;
static int recapture;
int mouseactive, focus;

static int mm_timerres;
static int timermode, timeon;
static HANDLE timehandle;
int sleep_resolution;

char start_path_data[MAX_DPATH];
char start_path_exe[MAX_DPATH];
char start_path_af[MAX_DPATH]; /* OLD AF */
char start_path_new1[MAX_DPATH]; /* AF2005 */
char start_path_new2[MAX_DPATH]; /* AMIGAFOREVERDATA */
char help_file[MAX_DPATH];
int af_path_2005, af_path_old;
DWORD quickstart = 1, configurationcache = 1;

static int timeend (void)
{
    if (!timeon)
	return 1;
    timeon = 0;
    if (timeEndPeriod (mm_timerres) == TIMERR_NOERROR)
	return 1;
    write_log ("TimeEndPeriod() failed\n");
    return 0;
}

static int timebegin (void)
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
    write_log ("TimeBeginPeriod() failed\n");
    return 0;
}

static int init_mmtimer (void)
{
    TIMECAPS tc;
    mm_timerres = 0;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR)
	return 0;
    mm_timerres = min (max (tc.wPeriodMin, 1), tc.wPeriodMax);
    sleep_resolution = 1000 / mm_timerres;
    timehandle = CreateEvent (NULL, TRUE, FALSE, NULL);
    return 1;
}

void sleep_millis (int ms)
{
    UINT TimerEvent;
    int start;
    
    start = read_processor_time ();
    TimerEvent = timeSetEvent (ms, 0, timehandle, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    WaitForSingleObject (timehandle, ms);
    ResetEvent (timehandle);
    timeKillEvent (TimerEvent);
    idletime += read_processor_time () - start;
}

frame_time_t read_processor_time_qpf (void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter (&counter);
    if (qpcdivisor == 0)
	return (frame_time_t)(counter.LowPart);
    return (frame_time_t)(counter.QuadPart >> qpcdivisor);
}
frame_time_t read_processor_time_rdtsc (void)
{
    frame_time_t foo = 0;
#if defined(X86_MSVC_ASSEMBLY)
    frame_time_t bar;
    __asm
    {
        rdtsc
        mov foo, eax
        mov bar, edx
    }
    /* very high speed CPU's RDTSC might overflow without this.. */
    foo >>= 6;
    foo |= bar << 26;
#endif
    return foo;
}
frame_time_t read_processor_time (void)
{
#if 0
    static int cnt;

    cnt++;
    if (cnt > 1000000) {
	write_log("**************\n");
	cnt = 0;
    }
#endif
    if (userdtsc)
	return read_processor_time_rdtsc ();
    else
	return read_processor_time_qpf ();
}

#include <process.h>
static volatile int dummythread_die;
static void dummythread (void *dummy)
{
    SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_LOWEST);
    while (!dummythread_die);
}
static uae_u64 win32_read_processor_time (void)
{
#if defined(X86_MSVC_ASSEMBLY)
    uae_u32 foo, bar;
    __asm
    {
	cpuid
	rdtsc
	mov foo, eax
	mov bar, edx
    }
    return (((uae_u64)bar) << 32) | foo;
#else
    return 0;
#endif
}
static void figure_processor_speed_rdtsc (void)
{
    static int freqset;
    uae_u64 clockrate;
    int oldpri;
    HANDLE th;

    if (freqset)
	return;
    th = GetCurrentThread ();
    freqset = 1;
    oldpri = GetThreadPriority (th);
    SetThreadPriority (th, THREAD_PRIORITY_HIGHEST);
    dummythread_die = -1;
    CloseHandle((HANDLE)_beginthread (&dummythread, 0, 0));
    sleep_millis (500);
    clockrate = win32_read_processor_time ();
    sleep_millis (500);
    clockrate = (win32_read_processor_time () - clockrate) * 2;
    dummythread_die = 0;
    SetThreadPriority (th, oldpri);
    write_log ("CLOCKFREQ: RDTSC %.2fMHz\n", clockrate / 1000000.0);
    syncbase = clockrate >> 6;
}

static void figure_processor_speed_qpf (void)
{
    LARGE_INTEGER freq;
    static LARGE_INTEGER freq2;
    uae_u64 qpfrate;

    if (!QueryPerformanceFrequency (&freq))
	return;
    if (freq.QuadPart == freq2.QuadPart)
	return;
    freq2.QuadPart = freq.QuadPart;
    qpfrate = freq.QuadPart;
     /* limit to 10MHz */
    qpcdivisor = 0;
    while (qpfrate > 10000000) {
        qpfrate >>= 1;
        qpcdivisor++;
    }
    write_log ("CLOCKFREQ: QPF %.2fMHz (%.2fMHz, DIV=%d)\n", freq.QuadPart / 1000000.0,
	 qpfrate / 1000000.0, 1 << qpcdivisor);
    syncbase = (unsigned long)qpfrate;
}

static void figure_processor_speed (void)
{
    if (SystemInfo.dwNumberOfProcessors > 1)
	userdtsc = 0;
    if (userdtsc)
	figure_processor_speed_rdtsc ();
    if (!userdtsc)
	figure_processor_speed_qpf ();
}

static void setcursor (int oldx, int oldy)
{
    int x = (amigawin_rect.right - amigawin_rect.left) / 2;
    int y = (amigawin_rect.bottom - amigawin_rect.top) / 2;
    mouseposx = oldx - x;
    mouseposy = oldy - y;
    if (abs (mouseposx) < 50 && abs (mouseposy) < 50)
	return;
    mouseposx = 0;
    mouseposy = 0;
//    if (oldx < amigawin_rect.left || oldy < amigawin_rect.top || oldx > amigawin_rect.right || oldy > amigawin_rect.bottom) {
    if (oldx < 0 || oldy < 0 || oldx > amigawin_rect.right - amigawin_rect.left || oldy > amigawin_rect.bottom - amigawin_rect.top) {
	write_log ("Mouse out of range: %dx%d (%dx%d %dx%d)\n", oldx, oldy,
	    amigawin_rect.left, amigawin_rect.top, amigawin_rect.right, amigawin_rect.bottom);
	return;
    }
    SetCursorPos (amigawin_rect.left + x, amigawin_rect.top + y);
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
void resumepaused (void)
{
    resumesoundpaused ();
#ifdef CD32
    akiko_exitgui ();
#endif
#ifdef CDTV
    cdtv_exitgui ();
#endif
    if (pausemouseactive)
	setmouseactive (-1);
    pausemouseactive = 0;
    pause_emulation = FALSE;
#ifdef RETROPLATFORM
    rp_pause (pause_emulation);
#endif
}
void setpaused (void)
{
    pause_emulation = TRUE;
    setsoundpaused ();
#ifdef CD32
    akiko_entergui ();
#endif
#ifdef CDTV
    cdtv_entergui ();
#endif
    pausemouseactive = 1;
    if (isfullscreen () <= 0) {
	pausemouseactive = mouseactive;
	setmouseactive (0);
    }
#ifdef RETROPLATFORM
    rp_pause (pause_emulation);
#endif
}

static void checkpause (void)
{
    if (currprefs.win32_inactive_pause)
	setpaused ();
}

static int showcursor;

extern char config_filename[MAX_DPATH];

static void setmaintitle (HWND hwnd)
{
    char txt[1000], txt2[500];

#ifdef RETROPLATFORM
    if (rp_isactive ())
	return;
#endif
    txt[0] = 0;
    if (config_filename[0]) {
	strcat (txt, "[");
	strcat (txt, config_filename);
	strcat (txt, "] - ");
    }
    strcat (txt, "WinUAE");
    txt2[0] = 0;
    if (mouseactive > 0) {
	WIN32GUI_LoadUIString (currprefs.win32_middle_mouse ? IDS_WINUAETITLE_MMB : IDS_WINUAETITLE_NORMAL,
	    txt2, sizeof (txt2));
    }
    if (WINUAEBETA > 0) {
	strcat (txt, BetaStr);
	if (strlen (WINUAEEXTRA) > 0) {
	    strcat (txt, " ");
	    strcat (txt, WINUAEEXTRA);
	}
    }
    if (txt2[0]) {
	strcat (txt, " - ");
	strcat (txt, txt2);
    }
    SetWindowText (hwnd, txt);
}


#ifndef AVIOUTPUT
static int avioutput_video = 0;
#endif

void setpriority (struct threadpriorities *pri)
{
    int err;
    err = SetPriorityClass (GetCurrentProcess (), pri->classvalue);
    if (!err)
	write_log ("priority set failed, %08X\n", GetLastError ());
}

static void releasecapture (void)
{
    if (!showcursor)
	return;
    ClipCursor (NULL);
    ReleaseCapture ();
    ShowCursor (TRUE);
    showcursor = 0;
}

void setmouseactive (int active)
{
    if (active == 0)
	releasecapture ();
    if (mouseactive == active && active >= 0)
	return;

    if (active < 0)
	active = 1;
    mouseactive = active;

    mouseposx = mouseposy = 0;
    //write_log ("setmouseactive(%d)\n", active);
    releasecapture ();
    recapture = 0;
#if 0
    if (active > 0 && mousehack_allowed () && mousehack_alive ()) {
	if (isfullscreen () <= 0)
	    return;
    }
#endif
    if (mouseactive > 0)
	focus = 1;
    if (focus) {
	int donotfocus = 0;
	HWND fw = GetForegroundWindow ();
	HWND w1 = hAmigaWnd;
	HWND w2 = hMainWnd;
	HWND w3 = NULL;
#ifdef RETROPLATFORM
	if (rp_isactive ())
	    w3 = rp_getparent ();
#endif
	if (!(fw == w1 || fw == w2)) {
	    if (SetForegroundWindow (w2) == FALSE) {
		if (SetForegroundWindow (w1) == FALSE) {
		    if (w3 == NULL || SetForegroundWindow (w3) == FALSE) {
			donotfocus = 1;
			write_log ("wanted focus but SetforegroundWindow() failed\n");
		    }
		}
	    }
	}
#ifdef RETROPLATFORM
	if (rp_isactive () && isfullscreen () == 0)
	    donotfocus = 0;
#endif
	if (donotfocus) {
	    focus = 0;
	    mouseactive = 0;
	}
    }
    if (mouseactive) {
	if (focus) {
	    if (!showcursor) {
		ShowCursor (FALSE);
		SetCapture (hAmigaWnd);
		ClipCursor (&amigawin_rect);
	    }
	    showcursor = 1;
	    setcursor (-1, -1);
	}
	inputdevice_acquire (TRUE);
    } else {
	inputdevice_acquire (FALSE);
    }
    if (!active)
	checkpause ();
    setmaintitle (hMainWnd);
#ifdef RETROPLATFORM
    rp_mouse_capture (active);
    rp_mouse_magic (magicmouse_alive ());
#endif
}

static void winuae_active (HWND hWnd, int minimized)
{
    struct threadpriorities *pri;

    write_log ("winuae_active(%d)\n", minimized);
    /* without this returning from hibernate-mode causes wrong timing
     */
    timeend ();
    sleep_millis (2);
    timebegin ();

    focus = 1;
    pri = &priorities[currprefs.win32_inactive_priority];
#ifndef	_DEBUG
    if (!minimized)
	pri = &priorities[currprefs.win32_active_priority];
#endif
    setpriority (pri);

    if (!avioutput_video) {
        clear_inhibit_frame (IHF_WINDOWHIDDEN);
    }
    if (sound_closed) {
	if (sound_closed < 0)
	    resumesoundpaused ();
	else
	    resumepaused ();
	sound_closed = 0;
    }
    if (WIN32GFX_IsPicassoScreen ())
	WIN32GFX_EnablePicasso ();
    getcapslock ();
    inputdevice_acquire (FALSE);
    wait_keyrelease ();
    inputdevice_acquire (TRUE);
    if (isfullscreen() > 0 && !gui_active)
	setmouseactive (1);
    manual_palette_refresh_needed = 1;
#ifdef LOGITECHLCD
    if (!minimized)
	lcd_priority (1);
#endif
}

static void winuae_inactive (HWND hWnd, int minimized)
{
    struct threadpriorities *pri;
    int wasfocus = focus;

    write_log ("winuae_inactive(%d)\n", minimized);
    if (minimized)
	exit_gui (0);
    focus = 0;
    wait_keyrelease ();
    setmouseactive (0);
    inputdevice_unacquire ();
    pri = &priorities[currprefs.win32_inactive_priority];
    if (!quit_program) {
	if (minimized) {
	    pri = &priorities[currprefs.win32_iconified_priority];
	    if (currprefs.win32_iconified_pause) {
	        setpaused ();
		sound_closed = 1;
	    } else if (currprefs.win32_iconified_nosound) {
		setsoundpaused ();
		sound_closed = -1;
	    }
	    if (!avioutput_video) {
		set_inhibit_frame (IHF_WINDOWHIDDEN);
	    }
	} else {
	    if (currprefs.win32_inactive_pause) {
		setpaused ();
		sound_closed = 1;
	    } else if (currprefs.win32_inactive_nosound) {
		setsoundpaused ();
		sound_closed = -1;
	    }
	}
    }
    setpriority (pri);
#ifdef FILESYS
    filesys_flush_cache ();
#endif
#ifdef LOGITECHLCD
    lcd_priority (0);
#endif
}

void minimizewindow (void)
{
    ShowWindow (hMainWnd, SW_MINIMIZE);
}

void disablecapture (void)
{
    setmouseactive (0);
}

extern void setamigamouse (int,int);
void setmouseactivexy (int x, int y, int dir)
{
    int diff = 8;
    if (isfullscreen () > 0)
	return;
    x += amigawin_rect.left;
    y += amigawin_rect.top;
    if (dir & 1)
	x = amigawin_rect.left - diff;
    if (dir & 2)
	x = amigawin_rect.right + diff;
    if (dir & 4)
	y = amigawin_rect.top - diff;
    if (dir & 8)
	y = amigawin_rect.bottom + diff;
    if (!dir) {
	x += (amigawin_rect.right - amigawin_rect.left) / 2;
	y += (amigawin_rect.bottom - amigawin_rect.top) / 2;
    }
    disablecapture ();
    SetCursorPos (x, y);
    if (dir)
	recapture = 1;
}

static void handleXbutton (WPARAM wParam, int updown)
{
    int b = GET_XBUTTON_WPARAM (wParam);
    int num = (b & XBUTTON1) ? 3 : (b & XBUTTON2) ? 4 : -1;
    if (num >= 0)
	setmousebuttonstate (dinput_winmouse (), num, updown);
}

#define MSGDEBUG 1

static LRESULT CALLBACK AmigaWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HDC hDC;
    int mx, my;
    static int mm, minimized, recursive, ignoremousemove;

#if MSGDEBUG > 1
    write_log ("AWP: %x %d\n", hWnd, message);
#endif
    if (ignore_messages_all)
	return DefWindowProc (hWnd, message, wParam, lParam);

    if (hMainWnd == 0)
	hMainWnd = hWnd;

    switch (message)
    {

    case WM_SETFOCUS:
        winuae_active (hWnd, minimized);
        minimized = 0;
	dx_check ();
	return 0;
    case WM_ACTIVATE:
        if (LOWORD (wParam) == WA_INACTIVE) {
	    minimized = HIWORD (wParam) ? 1 : 0;
	    winuae_inactive (hWnd, minimized);
	}
	dx_check ();
	return 0;
    case WM_ACTIVATEAPP:
#ifdef RETROPLATFORM
	rp_activate (wParam, lParam);
#endif
	dx_check ();
	return 0;

    case WM_PALETTECHANGED:
	if ((HWND)wParam != hWnd)
	    manual_palette_refresh_needed = 1;
    return 0;

    case WM_KEYDOWN:
	if (dinput_wmkey ((uae_u32)lParam))
	    gui_display (-1);
    return 0;

    case WM_LBUTTONUP:
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse (), 0, 0);
    return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
	if (!mouseactive && isfullscreen() <= 0 && !gui_active) {
	    setmouseactive (1);
	} else if (dinput_winmouse () >= 0) {
	    setmousebuttonstate (dinput_winmouse (), 0, 1);
	}
    return 0;
    case WM_RBUTTONUP:
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse (), 1, 0);
    return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse (), 1, 1);
    return 0;
    case WM_MBUTTONUP:
	if (!currprefs.win32_middle_mouse) {
	    if (dinput_winmouse () >= 0)
		setmousebuttonstate (dinput_winmouse (), 2, 0);
	}
    return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
	if (currprefs.win32_middle_mouse) {
#ifndef _DEBUG
	    if (isfullscreen () > 0)
		minimizewindow ();
#endif
	    if (mouseactive)
		setmouseactive(0);
	} else {
	    if (dinput_winmouse () >= 0)
		setmousebuttonstate (dinput_winmouse (), 2, 1);
	}
    return 0;
    case WM_XBUTTONUP:
	if (dinput_winmouse () >= 0) {
	    handleXbutton (wParam, 0);
	    return TRUE;
	}
    return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
	if (dinput_winmouse () >= 0) {
	    handleXbutton (wParam, 1);
	    return TRUE;
	}
    return 0;
    case WM_MOUSEWHEEL:
	if (dinput_winmouse () >= 0) {
	    int val = ((short)HIWORD (wParam));
	    setmousestate (dinput_winmouse (), 2, val, 0);
	    if (val < 0)
		setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 0, -1);
	    else if (val > 0)
		setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 1, -1);
	    return TRUE;
	}
    return 0;
    case WM_MOUSEHWHEEL:
	if (dinput_winmouse () >= 0) {
	    int val = ((short)HIWORD (wParam));
	    setmousestate (dinput_winmouse (), 3, val, 0);
	    if (val < 0)
		setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 2, -1);
	    else if (val > 0)
		setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 3, -1);
	    return TRUE;
	}
    return 0;

    case WM_PAINT:
    {
	static int recursive = 0;
	if (recursive == 0) {
	    PAINTSTRUCT ps;
	    recursive++;
	    notice_screen_contents_lost ();
	    hDC = BeginPaint (hWnd, &ps);
	    /* Check to see if this WM_PAINT is coming while we've got the GUI visible */
	    if (manual_painting_needed)
		updatedisplayarea ();
	    EndPaint (hWnd, &ps);
	    if (manual_palette_refresh_needed) {
		WIN32GFX_SetPalette ();
#ifdef PICASSO96
		DX_SetPalette (0, 256);
#endif
	    }
	    manual_palette_refresh_needed = 0;
	    recursive--;
	}
    }
    return 0;

    case WM_DROPFILES:
	dragdrop (hWnd, (HDROP)wParam, &changed_prefs, -1);
    return 0;

    case WM_TIMER:
#ifdef PARALLEL_PORT
	finishjob ();
#endif
    return 0;

    case WM_CREATE:
#ifdef RETROPLATFORM
	rp_set_hwnd (hWnd);
#endif
	DragAcceptFiles (hWnd, TRUE);
    return 0;

    case WM_DESTROY:
	inputdevice_unacquire ();
	dinput_window ();
    return 0;

    case WM_CLOSE:
	uae_quit ();
    return 0;

    case WM_SIZE:
	if (hStatusWnd)
	    SendMessage (hStatusWnd, WM_SIZE, wParam, lParam);
    break;

    case WM_WINDOWPOSCHANGED:
    {
	WINDOWPOS *wp = (WINDOWPOS*)lParam;
	if (isfullscreen () <= 0) {
	    if (!IsIconic (hWnd) && hWnd == hAmigaWnd) {
		GetWindowRect (hWnd, &amigawin_rect);
		if (isfullscreen () == 0) {
		    changed_prefs.gfx_size_win.x = amigawin_rect.left;
		    changed_prefs.gfx_size_win.y = amigawin_rect.top;
		}
	    }
	    notice_screen_contents_lost ();
	}
    }
    break;

    case WM_MOUSEMOVE:
    {
	mx = (signed short) LOWORD (lParam);
	my = (signed short) HIWORD (lParam);
	mx -= mouseposx;
	my -= mouseposy;
	if (recapture && isfullscreen () <= 0) {
	    setmouseactive (1);
	    setamigamouse (mx, my);
	    return 0;
	}
        if (dinput_winmouse () >= 0) {
            if (dinput_winmousemode ()) {
	        /* absolute + mousehack */
	        setmousestate (dinput_winmouse (), 0, mx, 1);
	        setmousestate (dinput_winmouse (), 1, my, 1);
	        return 0;
	    }
	    if (!focus)
		return DefWindowProc (hWnd, message, wParam, lParam);
	}
	if (dinput_winmouse () >= 0) {
	    if (dinput_winmousemode () == 0) {
	        /* relative */
	        int mxx = (amigawin_rect.right - amigawin_rect.left) / 2;
	        int myy = (amigawin_rect.bottom - amigawin_rect.top) / 2;
	        mx = mx - mxx;
	        my = my - myy;
	        setmousestate (dinput_winmouse (), 0, mx, 0);
	        setmousestate (dinput_winmouse (), 1, my, 0);
	    }
	} else if (!mouseactive && isfullscreen () <= 0) {
	    setmousestate (0, 0, mx, 1);
	    setmousestate (0, 1, my, 1);
	}
	if (showcursor || mouseactive)
	    setcursor (LOWORD (lParam), HIWORD (lParam));
	return 0;
    }
    break;

    case WM_MOVING:
    {
	LRESULT lr = DefWindowProc (hWnd, message, wParam, lParam);
	WIN32GFX_WindowMove ();
	return lr;
    }
    case WM_MOVE:
	WIN32GFX_WindowMove ();
    return FALSE;

    case WM_ENABLE:
	rp_set_enabledisable (wParam ? 1 : 0);
    return FALSE;

#ifdef FILESYS
    case WM_USER + 2:
    {
	typedef struct {
	    DWORD dwItem1;    // dwItem1 contains the previous PIDL or name of the folder. 
	    DWORD dwItem2;    // dwItem2 contains the new PIDL or name of the folder. 
	} SHNOTIFYSTRUCT;
	char path[MAX_PATH];

	if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
	    SHNOTIFYSTRUCT *shns = (SHNOTIFYSTRUCT*)wParam;
	    if(SHGetPathFromIDList((struct _ITEMIDLIST *)(shns->dwItem1), path)) {
		int inserted = lParam == SHCNE_MEDIAINSERTED ? 1 : 0;
		write_log("Shell Notification %d '%s'\n", inserted, path);
		if (!win32_hardfile_media_change (path, inserted)) {	
		    if ((inserted && CheckRM (path)) || !inserted) {
			if (inserted) {
			    DWORD type = GetDriveType(path);
			    if (type == DRIVE_CDROM)
				inserted = -1;
			}
			filesys_media_change (path, inserted, NULL);
		    }
		}
	    }
	}
    }
    return TRUE;
    case WM_DEVICECHANGE:
    {
	extern void win32_spti_media_change (char driveletter, int insert);
	extern void win32_ioctl_media_change (char driveletter, int insert);
	extern void win32_aspi_media_change (char driveletter, int insert);
	DEV_BROADCAST_HDR *pBHdr = (DEV_BROADCAST_HDR *)lParam;
	static int waitfornext;

	if (wParam == DBT_DEVNODES_CHANGED && lParam == 0) {
	    if (waitfornext)
		inputdevice_devicechange (&changed_prefs);
	    waitfornext = 0;
	} else if (pBHdr && pBHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
	    DEV_BROADCAST_DEVICEINTERFACE *dbd = (DEV_BROADCAST_DEVICEINTERFACE*)lParam;
	    if (wParam == DBT_DEVICEREMOVECOMPLETE)
		inputdevice_devicechange (&changed_prefs);
	    else if (wParam == DBT_DEVICEARRIVAL)
		waitfornext = 1;
	} else if (pBHdr && pBHdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
	    DEV_BROADCAST_VOLUME *pBVol = (DEV_BROADCAST_VOLUME *)lParam;
	    if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
		if (pBVol->dbcv_unitmask) {
		    int inserted, i;
		    char drive;
		    for (i = 0; i <= 'Z'-'A'; i++) {
			if (pBVol->dbcv_unitmask & (1 << i)) {
			    char drvname[10];
			    int type;

			    drive = 'A' + i;
			    sprintf (drvname, "%c:\\", drive);
			    type = GetDriveType (drvname);
			    if (wParam == DBT_DEVICEARRIVAL)
				inserted = 1;
			    else
				inserted = 0;
			    if (pBVol->dbcv_flags & DBTF_MEDIA) {
    #ifdef WINDDK
				win32_spti_media_change (drive, inserted);
				win32_ioctl_media_change (drive, inserted);
    #endif
				win32_aspi_media_change (drive, inserted);
			    }
			    if (type == DRIVE_REMOVABLE || type == DRIVE_CDROM || !inserted) {
				write_log("WM_DEVICECHANGE '%s' type=%d inserted=%d\n", drvname, type, inserted);
				if (!win32_hardfile_media_change (drvname, inserted)) {
				    if ((inserted && CheckRM (drvname)) || !inserted) {
					if (type == DRIVE_CDROM && inserted)
					    inserted = -1;
					filesys_media_change (drvname, inserted, NULL);
				    }
				}
			    }
			}
		    }
		}
	    }
	}
    }
#endif
    return TRUE;

    case WM_SYSCOMMAND:
	    switch (wParam & 0xfff0) // Check System Calls
	    {
		case SC_SCREENSAVE: // Screensaver Trying To Start?
		case SC_MONITORPOWER: // Monitor Trying To Enter Powersave?
		if (!manual_painting_needed && focus && currprefs.win32_powersavedisabled) {
		    return 0; // Prevent From Happening
		break;
		default:
		{
		    LRESULT lr;
		    
#ifdef RETROPLATFORM
		    if ((wParam & 0xfff0) == SC_CLOSE) {
			if (rp_close ())
			    return 0;
		    }
#endif
		    lr = DefWindowProc (hWnd, message, wParam, lParam);
		    switch (wParam & 0xfff0)
		    {
			case SC_MINIMIZE:
			break;
			case SC_RESTORE:
			break;
			case SC_CLOSE:
			    PostQuitMessage (0);
			break;
		    }
		    return lr;
		}
	    }
	}
    break;

    case WM_SYSKEYDOWN:
	if(currprefs.win32_ctrl_F11_is_quit && wParam == VK_F4)
	    return 0;
    break;

    case WM_INPUT:
	handle_rawinput (lParam);
	DefWindowProc (hWnd, message, wParam, lParam);
    return 0;

    case WM_NOTIFY:
    {
	LPNMHDR nm = (LPNMHDR)lParam;
	if (nm->hwndFrom == hStatusWnd) {
	    switch (nm->code)
	    {
		/* status bar clicks */
		case NM_CLICK:
		case NM_RCLICK:
		{
		    LPNMMOUSE lpnm = (LPNMMOUSE) lParam;
		    int num = (int)lpnm->dwItemSpec;
		    if (num >= 7 && num <= 10) {
			num -= 7;
			if (nm->code == NM_RCLICK) {
			    disk_eject (num);
			} else if (changed_prefs.dfxtype[num] >= 0) {
			    DiskSelection (hWnd, IDC_DF0 + num, 0, &changed_prefs, 0);
			    disk_insert (num, changed_prefs.df[num]);
			}
		    } else if (num == 4) {
			if (nm->code == NM_CLICK)
			    gui_display (-1);
			else
			    uae_reset (0);
		    }
		    return TRUE;
		}
	    }
	}
    }
    break;


     default:
    break;
    }

    return DefWindowProc (hWnd, message, wParam, lParam);
}

static int canstretch (void)
{
    if (isfullscreen () != 0)
	return 0;
    if (!WIN32GFX_IsPicassoScreen ())
	return 1;
    if (currprefs.win32_rtgallowscaling)
	return 1;
    return 0;
}

static LRESULT CALLBACK MainWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC hDC;

#if MSGDEBUG > 1
    write_log ("MWP: %x %d\n", hWnd, message);
#endif

    switch (message)
    {
     case WM_KILLFOCUS:
     case WM_SETFOCUS:
     case WM_MOUSEMOVE:
     case WM_MOUSEWHEEL:
     case WM_MOUSEHWHEEL:
     case WM_ACTIVATEAPP:
     case WM_DROPFILES:
     case WM_ACTIVATE:
     case WM_SETCURSOR:
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
     case WM_USER + 1:
     case WM_USER + 2:
     case WM_COMMAND:
     case WM_NOTIFY:
     case WM_ENABLE:
	return AmigaWindowProc (hWnd, message, wParam, lParam);

    case WM_DISPLAYCHANGE:
	if (isfullscreen() <= 0 && !currprefs.gfx_filter && (wParam + 7) / 8 != DirectDraw_GetBytesPerPixel ())
	    WIN32GFX_DisplayChangeRequested();
	break;

    case WM_GETMINMAXINFO:
    {
	LPMINMAXINFO lpmmi;
	lpmmi = (LPMINMAXINFO)lParam;
	lpmmi->ptMinTrackSize.x = 160 + window_extra_width;
	lpmmi->ptMinTrackSize.y = 128 + window_extra_height;
	lpmmi->ptMaxTrackSize.x = 3072 + window_extra_width;
	lpmmi->ptMaxTrackSize.y = 2048 + window_extra_height;
    }
    return 0;

    case WM_ENTERSIZEMOVE:
	in_sizemove++;
	break;

    case WM_EXITSIZEMOVE:
	in_sizemove--;
	/* fall through */

    case WM_WINDOWPOSCHANGED:
	WIN32GFX_WindowMove ();
	if (hAmigaWnd && isfullscreen () <= 0 && GetWindowRect (hAmigaWnd, &amigawin_rect)) {
	    DWORD aw = amigawin_rect.right - amigawin_rect.left;
	    DWORD ah = amigawin_rect.bottom - amigawin_rect.top;
	    if (in_sizemove > 0)
		break;

	    if (isfullscreen() == 0 && hAmigaWnd) {
		static int store_xy;
		RECT rc2;
		if (GetWindowRect (hMainWnd, &rc2)) {
		    DWORD left = rc2.left - win_x_diff;
		    DWORD top = rc2.top - win_y_diff;
		    DWORD width = rc2.right - rc2.left;
		    DWORD height = rc2.bottom - rc2.top;
		    if (amigawin_rect.left & 3) {
			MoveWindow (hMainWnd, rc2.left + 4 - amigawin_rect.left % 4, rc2.top,
				    rc2.right - rc2.left, rc2.bottom - rc2.top, TRUE);

		    }
		    if (store_xy++) {
			regsetint (NULL, "MainPosX", left);
			regsetint (NULL, "MainPosY", top);
		    }
		    changed_prefs.gfx_size_win.x = left;
		    changed_prefs.gfx_size_win.y = top;
		    if (canstretch ()) {
			changed_prefs.gfx_size_win.width = width - window_extra_width;
			changed_prefs.gfx_size_win.height = height - window_extra_height;
		    }
		}
		return 0;
	    }
	}
	break;

    case WM_WINDOWPOSCHANGING:
    {
	WINDOWPOS *wp = (WINDOWPOS*)lParam;
	if (!canstretch ())
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
	    WIN32GFX_ToggleFullScreen ();
	    return 0;
	}
	break;


    default:
	break;

    }

    return DefWindowProc (hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK HiddenWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
	case WM_USER + 1: /* Systray icon */
	    switch (lParam)
	    {
		case WM_LBUTTONDOWN:
		    SetForegroundWindow (hGUIWnd ? hGUIWnd : hMainWnd);
		break;
		case WM_LBUTTONDBLCLK:
		    if (!gui_active)
			inputdevice_add_inputcode (AKS_ENTERGUI, 1);
		break;
		case WM_RBUTTONDOWN:
		    if (!gui_active)
			systraymenu (hWnd);
		    else
			SetForegroundWindow (hGUIWnd ? hGUIWnd : hMainWnd);
		break;
	    }
	break;
	case WM_COMMAND:
	    switch (wParam & 0xffff)
	    {
		case ID_ST_CONFIGURATION:
		    inputdevice_add_inputcode (AKS_ENTERGUI, 1);
		break;
		case ID_ST_HELP:
		    if (pHtmlHelp)
			pHtmlHelp (NULL, help_file, 0, NULL);
		break;
		case ID_ST_QUIT:
		    uae_quit ();
		break;
		case ID_ST_RESET:
		    uae_reset (0);
		break;
		case ID_ST_EJECTALL:
		    disk_eject (0);
		    disk_eject (1);
		    disk_eject (2);
		    disk_eject (3);
		break;
		case ID_ST_DF0:
		    DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF0, 0, &changed_prefs, 0);
		    disk_insert (0, changed_prefs.df[0]);
		break;
		case ID_ST_DF1:
		    DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF1, 0, &changed_prefs, 0);
		    disk_insert (1, changed_prefs.df[0]);
		break;
		case ID_ST_DF2:
		    DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF2, 0, &changed_prefs, 0);
		    disk_insert (2, changed_prefs.df[0]);
		break;
		case ID_ST_DF3:
		    DiskSelection (isfullscreen() > 0 ? NULL : hWnd, IDC_DF3, 0, &changed_prefs, 0);
		    disk_insert (3, changed_prefs.df[0]);
		break;
	    }
	    break;
    }
    if (TaskbarRestart != 0 && TaskbarRestartHWND == hWnd && message == TaskbarRestart) {
         //write_log ("notif: taskbarrestart\n");
         systray (TaskbarRestartHWND, FALSE);
    }
    return DefWindowProc (hWnd, message, wParam, lParam);
}

void handle_events (void)
{
    MSG msg;
    int was_paused = 0;
    static int cnt;

    while (pause_emulation) {
	if (pause_emulation && was_paused == 0) {
	    setpaused ();
	    was_paused = 1;
	    manual_painting_needed++;
	    gui_fps (0, 0);
	}
	while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
	    TranslateMessage (&msg);
	    DispatchMessage (&msg);
	}
	sleep_millis (20);
	inputdevicefunc_keyboard.read ();
	inputdevicefunc_mouse.read ();
	inputdevicefunc_joystick.read ();
	inputdevice_handle_inputcode ();
	check_prefs_changed_gfx ();
#ifdef RETROPLATFORM
	rp_vsync ();
#endif
	cnt = 0;
	while (checkIPC (&currprefs));
	if (quit_program)
	    break;
    }
    while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
	TranslateMessage (&msg);
	DispatchMessage (&msg);
    }
    while (checkIPC (&currprefs));
    if (was_paused) {
	resumepaused ();
	sound_closed = 0;
	manual_painting_needed--;
    }
    cnt--;
    if (cnt <= 0) {
	figure_processor_speed ();
	flush_log ();
	cnt = 50 * 5;
    }
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

    wc.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW | CS_DBLCLKS | CS_OWNDC;
    wc.lpfnWndProc = AmigaWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "AmigaPowah";
    wc.hbrBackground = CreateSolidBrush (g_dwBackgroundColor);
    if (!RegisterClass (&wc))
	return 0;

    wc.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush (black);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "PCsuxRox";
    if (!RegisterClass (&wc))
	return 0;

    wc.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = HiddenWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush (g_dwBackgroundColor);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "Useless";
    if (!RegisterClass (&wc))
	return 0;

    hHiddenWnd = CreateWindowEx (0,
	"Useless", "You don't see me",
	WS_POPUP,
	0, 0,
	1, 1,
	NULL, NULL, 0, NULL);
    if (!hHiddenWnd)
	return 0;

    return 1;
}

#ifdef __GNUC__
#undef WINAPI
#define WINAPI
#endif

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
    char *chm = "WinUAE.chm";
    int result = 0;
    sprintf(help_file, "%s%s", start_path_data, chm);
    if (!zfile_exists (help_file))
	sprintf(help_file, "%s%s", start_path_exe, chm);
    if (zfile_exists (help_file)) {
	if (hHtmlHelp = LoadLibrary ("HHCTRL.OCX")) {
	    pHtmlHelp = (HWND(WINAPI *)(HWND, LPCSTR, UINT, LPDWORD))GetProcAddress (hHtmlHelp, "HtmlHelpA");
	    result = 1;
	}
    }
    return result;
}

struct winuae_lang langs[] =
{
    { LANG_AFRIKAANS, "Afrikaans" },
    { LANG_ARABIC, "Arabic" },
    { LANG_ARMENIAN, "Armenian" },
    { LANG_ASSAMESE, "Assamese" },
    { LANG_AZERI, "Azeri" },
    { LANG_BASQUE, "Basque" },
    { LANG_BELARUSIAN, "Belarusian" },
    { LANG_BENGALI, "Bengali" },
    { LANG_BULGARIAN, "Bulgarian" },
    { LANG_CATALAN, "Catalan" },
    { LANG_CHINESE, "Chinese" },
    { LANG_CROATIAN, "Croatian" },
    { LANG_CZECH, "Czech" },
    { LANG_DANISH, "Danish" },
    { LANG_DUTCH, "Dutch" },
    { LANG_ESTONIAN, "Estoanian" },
    { LANG_FAEROESE, "Faeroese" },
    { LANG_FARSI, "Farsi" },
    { LANG_FINNISH, "Finnish" },
    { LANG_FRENCH, "French" },
    { LANG_GEORGIAN, "Georgian" },
    { LANG_GERMAN, "German" },
    { LANG_GREEK, "Greek" },
    { LANG_GUJARATI, "Gujarati" },
    { LANG_HEBREW, "Hebrew" },
    { LANG_HINDI, "Hindi" },
    { LANG_HUNGARIAN, "Hungarian" },
    { LANG_ICELANDIC, "Icelandic" },
    { LANG_INDONESIAN, "Indonesian" },
    { LANG_ITALIAN, "Italian" },
    { LANG_JAPANESE, "Japanese" },
    { LANG_KANNADA, "Kannada" },
    { LANG_KASHMIRI, "Kashmiri" },
    { LANG_KAZAK, "Kazak" },
    { LANG_KONKANI, "Konkani" },
    { LANG_KOREAN, "Korean" },
    { LANG_LATVIAN, "Latvian" },
    { LANG_LITHUANIAN, "Lithuanian" },
    { LANG_MACEDONIAN, "Macedonian" },
    { LANG_MALAY, "Malay" },
    { LANG_MALAYALAM, "Malayalam" },
    { LANG_MANIPURI, "Manipuri" },
    { LANG_MARATHI, "Marathi" },
    { LANG_NEPALI, "Nepali" },
    { LANG_NORWEGIAN, "Norwegian" },
    { LANG_ORIYA, "Oriya" },
    { LANG_POLISH, "Polish" },
    { LANG_PORTUGUESE, "Portuguese" },
    { LANG_PUNJABI, "Punjabi" },
    { LANG_ROMANIAN, "Romanian" },
    { LANG_RUSSIAN, "Russian" },
    { LANG_SANSKRIT, "Sanskrit" },
    { LANG_SINDHI, "Sindhi" },
    { LANG_SLOVAK, "Slovak" },
    { LANG_SLOVENIAN, "Slovenian" },
    { LANG_SPANISH, "Spanish" },
    { LANG_SWAHILI, "Swahili" },
    { LANG_SWEDISH, "Swedish" },
    { LANG_TAMIL, "Tamil" },
    { LANG_TATAR, "Tatar" },
    { LANG_TELUGU, "Telugu" },
    { LANG_THAI, "Thai" },
    { LANG_TURKISH, "Turkish" },
    { LANG_UKRAINIAN, "Ukrainian" },
    { LANG_UZBEK, "Uzbek" },
    { LANG_VIETNAMESE, "Vietnamese" },
    { LANG_ENGLISH, "default" },
    { 0x400, "guidll.dll"},
    { 0, NULL }
};
static char *getlanguagename(DWORD id)
{
    int i;
    for (i = 0; langs[i].name; i++) {
	if (langs[i].id == id)
	    return langs[i].name;
    }
    return NULL;
}

typedef LANGID (CALLBACK* PGETUSERDEFAULTUILANGUAGE)(void);
static PGETUSERDEFAULTUILANGUAGE pGetUserDefaultUILanguage;

HMODULE language_load (WORD language)
{
    HMODULE result = NULL;
    char dllbuf[MAX_DPATH];
    char *dllname;

    if (language <= 0) {
	/* new user-specific Windows ME/2K/XP method to get UI language */
	pGetUserDefaultUILanguage = (PGETUSERDEFAULTUILANGUAGE)GetProcAddress (
	    GetModuleHandle ("kernel32.dll"), "GetUserDefaultUILanguage");
	language = GetUserDefaultLangID ();
	if (pGetUserDefaultUILanguage)
	    language = pGetUserDefaultUILanguage ();
	language &= 0x3ff; // low 9-bits form the primary-language ID
    }
    if (language == LANG_GERMAN)
	hrtmon_lang = 2;
    if (language == LANG_FRENCH)
	hrtmon_lang = 3;
    dllname = getlanguagename (language);
    if (dllname) {
	DWORD  dwVersionHandle, dwFileVersionInfoSize;
	LPVOID lpFileVersionData = NULL;
	BOOL   success = FALSE;
	int fail = 1;

	if (language == 0x400)
	    strcpy (dllbuf, "guidll.dll");
	else
	    sprintf (dllbuf, "WinUAE_%s.dll", dllname);
	result = WIN32_LoadLibrary (dllbuf);
	if (result)  {
	    dwFileVersionInfoSize = GetFileVersionInfoSize (dllbuf, &dwVersionHandle);
	    if (dwFileVersionInfoSize) {
		if (lpFileVersionData = xcalloc (1, dwFileVersionInfoSize)) {
		    if (GetFileVersionInfo (dllbuf, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData)) {
			VS_FIXEDFILEINFO *vsFileInfo = NULL;
			UINT uLen;
			fail = 0;
			if (VerQueryValue (lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen)) {
			    if (vsFileInfo &&
				HIWORD(vsFileInfo->dwProductVersionMS) == UAEMAJOR
				&& LOWORD(vsFileInfo->dwProductVersionMS) == UAEMINOR
				&& (HIWORD(vsFileInfo->dwProductVersionLS) == UAESUBREV)) {
				success = TRUE;
				write_log ("Translation DLL '%s' loaded and enabled\n", dllbuf);
			    } else {
				write_log ("Translation DLL '%s' version mismatch (%d.%d.%d)\n", dllbuf,
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
		write_log ("Translation DLL '%s' failed to load, error %d\n", dllbuf, GetLastError ());
	}
	if (result && !success) {
	    FreeLibrary (result);
	    result = NULL;
	}
    }
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
	char tmp[MAX_DPATH];
	WIN32GUI_LoadUIString (priorities[i].id, tmp, sizeof (tmp));
	priorities[i].name = my_strdup (tmp);
    }
}

static void WIN32_InitLang(void)
{
    int lid;
    WORD langid = -1;

    if (regqueryint (NULL, "Language", &lid))
	langid = (WORD)lid;
    hUIDLL = language_load (langid);
    pritransla ();
}

 /* try to load COMDLG32 and DDRAW, initialize csDraw */
static int WIN32_InitLibraries( void )
{
    LARGE_INTEGER freq;

    CoInitialize (0);
    /* Determine our processor speed and capabilities */
    if (!init_mmtimer ()) {
	pre_gui_message ("MMTimer initialization failed, exiting..");
	return 0;
    }
    if (!QueryPerformanceCounter (&freq)) {
	pre_gui_message ("No QueryPerformanceFrequency() supported, exiting..\n");
	return 0;
    }
    rpt_available = 1;
    figure_processor_speed ();
    if (!timebegin ()) {
	pre_gui_message ("MMTimer second initialization failed, exiting..");
	return 0;
    }

    /* Make sure we do an InitCommonControls() to get some advanced controls */
    InitCommonControls ();

    hRichEdit = LoadLibrary ("RICHED32.DLL");

    return 1;
}

int debuggable (void)
{
    return 0;
}

int mousehack_allowed (void)
{
    return dinput_winmouse () > 0 && dinput_winmousemode ();
}

void toggle_mousegrab (void)
{
}
#define LOG_BOOT "winuaebootlog.txt"
#define LOG_NORMAL "winuaelog.txt"

void logging_open(int bootlog, int append)
{
    char debugfilename[MAX_DPATH];

    debugfilename[0] = 0;
#ifndef	SINGLEFILE
    if (currprefs.win32_logfile)
	sprintf (debugfilename, "%s%s", start_path_data, LOG_NORMAL);
    if (bootlog)
	sprintf (debugfilename, "%s%s", start_path_data, LOG_BOOT);
    if (debugfilename[0]) {
	if (!debugfile)
	    debugfile = log_open (debugfilename, append, bootlog);
    }
#endif
}

typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

void logging_init(void)
{
    LPFN_ISWOW64PROCESS fnIsWow64Process;
    int wow64 = 0;
    static int started;
    static int first;

    if (first > 1) {
	write_log ("** RESTART **\n");
	return;
    }
    if (first == 1) {
	write_log ("Log (%s): '%s%s'\n", currprefs.win32_logfile ? "enabled" : "disabled",
	    start_path_data, LOG_NORMAL);
	if (debugfile)
	    log_close (debugfile);
	debugfile = 0;
    }
    logging_open (first ? 0 : 1, 0);
    logging_started = 1;
    first++;
    fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress (GetModuleHandle ("kernel32"), "IsWow64Process");
    if (fnIsWow64Process)
	fnIsWow64Process (GetCurrentProcess (), &wow64);
    write_log ("%s (%d.%d %s%s[%d])", VersionStr,
	osVersion.dwMajorVersion, osVersion.dwMinorVersion, osVersion.szCSDVersion,
	strlen(osVersion.szCSDVersion) > 0 ? " " : "", os_winnt_admin);
    write_log (" %d-bit %X.%X %d", wow64 ? 64 : 32,
	SystemInfo.wProcessorLevel, SystemInfo.wProcessorRevision,
	SystemInfo.dwNumberOfProcessors);
    write_log ("\n(c) 1995-2001 Bernd Schmidt   - Core UAE concept and implementation."
	       "\n(c) 1998-2008 Toni Wilen      - Win32 port, core code updates."
	       "\n(c) 1996-2001 Brian King      - Win32 port, Picasso96 RTG, and GUI."
	       "\n(c) 1996-1999 Mathias Ortmann - Win32 port and bsdsocket support."
	       "\n(c) 2000-2001 Bernd Meyer     - JIT engine."
	       "\n(c) 2000-2005 Bernd Roesch    - MIDI input, many fixes."
	       "\nPress F12 to show the Settings Dialog (GUI), Alt-F4 to quit."
	       "\nEnd+F1 changes floppy 0, End+F2 changes floppy 1, etc."
	       "\n");
    write_log ("EXE: '%s', DATA: '%s'\n", start_path_exe, start_path_data);
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
    f = fopen (bootlog ? LOG_BOOT : LOG_NORMAL, "rb");
    if (!f)
	return NULL;
    fseek (f, 0, SEEK_END);
    size = ftell (f);
    fseek (f, 0, SEEK_SET);
    if (size > 30000)
	size = 30000;
    if (size > 0) {
	dst = xcalloc (1, size + 1);
	if (dst)
	    fread (dst, 1, size, f);
	fclose (f);
	*len = size + 1;
    }
    return dst;
}

static void strip_slashes (char *p)
{
    while (strlen (p) > 0 && (p[strlen (p) - 1] == '\\' || p[strlen (p) - 1] == '/'))
	p[strlen (p) - 1] = 0;
}
static void fixtrailing(char *p)
{
    if (strlen(p) == 0)
	return;
    if (p[strlen(p) - 1] == '/' || p[strlen(p) - 1] == '\\')
	return;
    strcat(p, "\\");
}

typedef DWORD (STDAPICALLTYPE *PFN_GetKey)(LPVOID lpvBuffer, DWORD dwSize);
uae_u8 *target_load_keyfile (struct uae_prefs *p, char *path, int *sizep, char *name)
{
    uae_u8 *keybuf = NULL;
    HMODULE h;
    PFN_GetKey pfnGetKey;
    int size;
    char *libname = "amigaforever.dll";

    h = WIN32_LoadLibrary (libname);
    if (!h) {
	char path[MAX_DPATH];
	sprintf (path, "%s..\\Player\\%s", start_path_exe, libname);
	h = LoadLibrary (path);
	if (!h) {
	    char *afr = getenv ("AMIGAFOREVERROOT");
	    if (afr) {
		char tmp[MAX_DPATH];
		strcpy (tmp, afr);
		fixtrailing (tmp);
		sprintf (path, "%sPlayer\\%s", tmp, libname);
		h = LoadLibrary (path);
		if (!h)
		    return NULL;
	    }
	}
    }
    GetModuleFileName (h, name, MAX_DPATH);
    pfnGetKey = (PFN_GetKey)GetProcAddress (h, "GetKey");
    if (pfnGetKey) {
	size = pfnGetKey (NULL, 0);
	*sizep = size;
	if (size > 0) {
	    keybuf = xmalloc (size);
	    if (pfnGetKey (keybuf, size) != size) {
		xfree (keybuf);
		keybuf = NULL;
	    }
	}
    }
    FreeLibrary (h);
    return keybuf;
}


extern char *get_aspi_path(int);

static get_aspi (int old)
{
    if ((old == UAESCSI_SPTI || old == UAESCSI_SPTISCAN) && os_winnt_admin)
	return old;
    if (old == UAESCSI_NEROASPI && get_aspi_path (1))
	return old;
    if (old == UAESCSI_FROGASPI && get_aspi_path (2))
	return old;
    if (old == UAESCSI_ADAPTECASPI && get_aspi_path (0))
	return old;
    if (os_winnt_admin)
	return UAESCSI_SPTI;
    else if (get_aspi_path (1))
	return UAESCSI_NEROASPI;
    else if (get_aspi_path (2))
	return UAESCSI_FROGASPI;
    else if (get_aspi_path (0))
	return UAESCSI_ADAPTECASPI;
    else
	return UAESCSI_SPTI;
}

void target_quit (void)
{
}

void target_fixup_options (struct uae_prefs *p)
{
#ifdef RETROPLATFORM
    rp_fixup_options (p);
#endif
}

void target_default_options (struct uae_prefs *p, int type)
{
    if (type == 2 || type == 0) {
	p->win32_middle_mouse = 1;
	p->win32_logfile = 0;
	p->win32_iconified_nosound = 1;
	p->win32_iconified_pause = 1;
	p->win32_inactive_nosound = 0;
	p->win32_inactive_pause = 0;
	p->win32_ctrl_F11_is_quit = 0;
	p->win32_soundcard = 0;
	p->win32_active_priority = 1;
	p->win32_inactive_priority = 2;
	p->win32_iconified_priority = 3;
	p->win32_notaskbarbutton = 0;
	p->win32_alwaysontop = 0;
	p->win32_specialkey = 0xcf; // DIK_END
	p->win32_automount_removable = 0;
	p->win32_automount_drives = 0;
	p->win32_automount_cddrives = 0;
	p->win32_automount_netdrives = 0;
	p->win32_kbledmode = 0;
	p->win32_uaescsimode = get_aspi (p->win32_uaescsimode);
	p->win32_borderless = 0;
	p->win32_powersavedisabled = 1;
	p->win32_outsidemouse = 0;
	p->sana2 = 0;
	p->win32_rtgmatchdepth = 1;
	p->win32_rtgscaleifsmall = 1;
	p->win32_rtgallowscaling = 0;
	p->win32_rtgscaleaspectratio = -1;
    }
    if (type == 1 || type == 0) {
	p->win32_uaescsimode = get_aspi (p->win32_uaescsimode);
	p->win32_midioutdev = -2;
	p->win32_midiindev = 0;
	p->win32_automount_removable = 0;
	p->win32_automount_drives = 0;
	p->win32_automount_cddrives = 0;
	p->win32_automount_netdrives = 0;
	p->picasso96_modeflags = RGBFF_CLUT | RGBFF_R5G6B5PC | RGBFF_B8G8R8A8;
    }
}

static const char *scsimode[] = { "none", "SPTI", "SPTI+SCSISCAN", "AdaptecASPI", "NeroASPI", "FrogASPI", 0 };

void target_save_options (struct zfile *f, struct uae_prefs *p)
{
    cfgfile_target_dwrite (f, "middle_mouse=%s\n", p->win32_middle_mouse ? "true" : "false");
    cfgfile_target_dwrite (f, "magic_mouse=%s\n", p->win32_outsidemouse ? "true" : "false");
    cfgfile_target_dwrite (f, "logfile=%s\n", p->win32_logfile ? "true" : "false");
    cfgfile_target_dwrite (f, "map_drives=%s\n", p->win32_automount_drives ? "true" : "false");
    cfgfile_target_dwrite (f, "map_drives_auto=%s\n", p->win32_automount_removable ? "true" : "false");
    cfgfile_target_dwrite (f, "map_cd_drives=%s\n", p->win32_automount_cddrives ? "true" : "false");
    cfgfile_target_dwrite (f, "map_net_drives=%s\n", p->win32_automount_netdrives ? "true" : "false");
    serdevtoname (p->sername);
    cfgfile_target_dwrite (f, "serial_port=%s\n", p->sername[0] ? p->sername : "none" );
    sernametodev (p->sername);
    cfgfile_target_dwrite (f, "parallel_port=%s\n", p->prtname[0] ? p->prtname : "none" );

    cfgfile_target_dwrite (f, "active_priority=%d\n", priorities[p->win32_active_priority].value);
    cfgfile_target_dwrite (f, "inactive_priority=%d\n", priorities[p->win32_inactive_priority].value);
    cfgfile_target_dwrite (f, "inactive_nosound=%s\n", p->win32_inactive_nosound ? "true" : "false");
    cfgfile_target_dwrite (f, "inactive_pause=%s\n", p->win32_inactive_pause ? "true" : "false");
    cfgfile_target_dwrite (f, "iconified_priority=%d\n", priorities[p->win32_iconified_priority].value);
    cfgfile_target_dwrite (f, "iconified_nosound=%s\n", p->win32_iconified_nosound ? "true" : "false");
    cfgfile_target_dwrite (f, "iconified_pause=%s\n", p->win32_iconified_pause ? "true" : "false");

    cfgfile_target_dwrite (f, "ctrl_f11_is_quit=%s\n", p->win32_ctrl_F11_is_quit ? "true" : "false");
    cfgfile_target_dwrite (f, "midiout_device=%d\n", p->win32_midioutdev );
    cfgfile_target_dwrite (f, "midiin_device=%d\n", p->win32_midiindev );
    cfgfile_target_dwrite (f, "rtg_match_depth=%s\n", p->win32_rtgmatchdepth ? "true" : "false");
    cfgfile_target_dwrite (f, "rtg_scale_small=%s\n", p->win32_rtgscaleifsmall ? "true" : "false");
    cfgfile_target_dwrite (f, "rtg_scale_allow=%s\n", p->win32_rtgallowscaling ? "true" : "false");
    cfgfile_target_dwrite (f, "rtg_scale_aspect_ratio=%d:%d\n",
	p->win32_rtgscaleaspectratio >= 0 ? (p->win32_rtgscaleaspectratio >> 8) : -1,
	p->win32_rtgscaleaspectratio >= 0 ? (p->win32_rtgscaleaspectratio & 0xff) : -1);
    cfgfile_target_dwrite (f, "borderless=%s\n", p->win32_borderless ? "true" : "false");
    cfgfile_target_dwrite (f, "uaescsimode=%s\n", scsimode[p->win32_uaescsimode]);
    cfgfile_target_dwrite (f, "soundcard=%d\n", p->win32_soundcard);
    cfgfile_target_dwrite (f, "cpu_idle=%d\n", p->cpu_idle);
    cfgfile_target_dwrite (f, "notaskbarbutton=%s\n", p->win32_notaskbarbutton ? "true" : "false");
    cfgfile_target_dwrite (f, "always_on_top=%s\n", p->win32_alwaysontop ? "true" : "false");
    cfgfile_target_dwrite (f, "no_recyclebin=%s\n", p->win32_norecyclebin ? "true" : "false");
    cfgfile_target_dwrite (f, "specialkey=0x%x\n", p->win32_specialkey);
    cfgfile_target_dwrite (f, "kbledmode=%d\n", p->win32_kbledmode);
    cfgfile_target_dwrite (f, "powersavedisabled=%s\n", p->win32_powersavedisabled ? "true" : "false");

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

static const char *obsolete[] = {
    "killwinkeys", "sound_force_primary", "iconified_highpriority",
    "sound_sync", "sound_tweak", "directx6", "sound_style",
    "file_path", "iconified_nospeed", "activepriority",
    0
};

int target_parse_option (struct uae_prefs *p, char *option, char *value)
{
    char tmpbuf[CONFIG_BLEN];
    int i, v;
    int result = (cfgfile_yesno (option, value, "middle_mouse", &p->win32_middle_mouse)
	    || cfgfile_yesno (option, value, "map_drives", &p->win32_automount_drives)
	    || cfgfile_yesno (option, value, "map_drives_auto", &p->win32_automount_removable)
	    || cfgfile_yesno (option, value, "map_cd_drives", &p->win32_automount_cddrives)
	    || cfgfile_yesno (option, value, "map_net_drives", &p->win32_automount_netdrives)
	    || cfgfile_yesno (option, value, "logfile", &p->win32_logfile)
	    || cfgfile_yesno (option, value, "networking", &p->socket_emu)
	    || cfgfile_yesno (option, value, "borderless", &p->win32_borderless)
	    || cfgfile_yesno (option, value, "inactive_pause", &p->win32_inactive_pause)
	    || cfgfile_yesno (option, value, "inactive_nosound", &p->win32_inactive_nosound)
	    || cfgfile_yesno (option, value, "iconified_pause", &p->win32_iconified_pause)
	    || cfgfile_yesno (option, value, "iconified_nosound", &p->win32_iconified_nosound)
	    || cfgfile_yesno (option, value, "ctrl_f11_is_quit", &p->win32_ctrl_F11_is_quit)
	    || cfgfile_yesno (option, value, "no_recyclebin", &p->win32_norecyclebin)
	    || cfgfile_intval (option, value, "midi_device", &p->win32_midioutdev, 1)
	    || cfgfile_intval (option, value, "midiout_device", &p->win32_midioutdev, 1)
	    || cfgfile_intval (option, value, "midiin_device", &p->win32_midiindev, 1)
	    || cfgfile_intval (option, value, "soundcard", &p->win32_soundcard, 1)
	    || cfgfile_yesno (option, value, "notaskbarbutton", &p->win32_notaskbarbutton)
	    || cfgfile_yesno (option, value, "always_on_top", &p->win32_alwaysontop)
	    || cfgfile_yesno (option, value, "powersavedisabled", &p->win32_powersavedisabled)
	    || cfgfile_yesno (option, value, "magic_mouse", &p->win32_outsidemouse)
	    || cfgfile_intval (option, value, "specialkey", &p->win32_specialkey, 1)
	    || cfgfile_intval (option, value, "kbledmode", &p->win32_kbledmode, 1)
	    || cfgfile_intval (option, value, "cpu_idle", &p->cpu_idle, 1));

    if (cfgfile_yesno (option, value, "rtg_match_depth", &p->win32_rtgmatchdepth))
	return 1;
    if (cfgfile_yesno (option, value, "rtg_scale_small", &p->win32_rtgscaleifsmall))
	return 1;
    if (cfgfile_yesno (option, value, "rtg_scale_allow", &p->win32_rtgallowscaling))
	return 1;

    if (cfgfile_yesno (option, value, "aspi", &v)) {
	p->win32_uaescsimode = 0;
	if (v)
	    p->win32_uaescsimode = get_aspi(0);
	if (p->win32_uaescsimode < UAESCSI_ASPI_FIRST)
	    p->win32_uaescsimode = UAESCSI_ADAPTECASPI;
	return 1;
    }

    if (cfgfile_string (option, value, "rtg_scale_aspect_ratio", tmpbuf, sizeof tmpbuf)) {
	int v1, v2;
	char *s;
	
	p->gfx_filter_aspect = -1;
	v1 = atol (tmpbuf);
	s = strchr (tmpbuf, ':');
	if (s) {
	    v2 = atol (s + 1);
	    if (v1 < 0 || v2 < 0)
		p->gfx_filter_aspect = -1;
	    else if (v1 == 0 || v2 == 0)
		p->gfx_filter_aspect = 0;
	    else
		p->gfx_filter_aspect = (v1 << 8) | v2;
	}
	return 1;
    }

    if (cfgfile_strval (option, value, "uaescsimode", &p->win32_uaescsimode, scsimode, 0))
	return 1;


    if (cfgfile_intval (option, value, "active_priority", &v, 1)) {
	p->win32_active_priority = fetchpri (v, 1);
	return 1;
    }
    if (cfgfile_intval (option, value, "activepriority", &v, 1)) {
	p->win32_active_priority = fetchpri (v, 1);
	return 1;
    }
    if (cfgfile_intval (option, value, "inactive_priority", &v, 1)) {
	p->win32_inactive_priority = fetchpri (v, 1);
	return 1;
    }
    if (cfgfile_intval (option, value, "iconified_priority", &v, 1)) {
	p->win32_iconified_priority = fetchpri (v, 2);
	return 1;
    }

    if (cfgfile_string (option, value, "serial_port", &p->sername[0], 256)) {
	sernametodev(p->sername);
	if (p->sername[0])
	    p->use_serial = 1;
	else
	    p->use_serial = 0;
	return 1;
    }

    if (cfgfile_string (option, value, "parallel_port", &p->prtname[0], 256)) {
	if (!strcmp(p->prtname, "none"))
	    p->prtname[0] = 0;
	return 1;
    }

    i = 0;
    while (obsolete[i]) {
	if (!strcasecmp (obsolete[i], option)) {
	    write_log ("obsolete config entry '%s'\n", option);
	    return 1;
	}
	i++;
    }

    return result;
}

static void createdir (const char *path)
{
    CreateDirectory (path, NULL);
}

void fetch_saveimagepath (char *out, int size, int dir)
{
    fetch_path ("SaveimagePath", out, size);
    if (dir) {
	out[strlen (out) - 1] = 0;
	createdir (out);
	fetch_path ("SaveimagePath", out, size);
    }
}
void fetch_configurationpath (char *out, int size)
{
    fetch_path ("ConfigurationPath", out, size);
}
void fetch_screenshotpath (char *out, int size)
{
    fetch_path ("ScreenshotPath", out, size);
}
void fetch_datapath (char *out, int size)
{
    fetch_path (NULL, out, size);
}

static int isfilesindir(char *p)
{
    WIN32_FIND_DATA fd;
    HANDLE h;
    char path[MAX_DPATH];
    int i = 0;
    DWORD v;

    v = GetFileAttributes (p);
    if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
	return 0;
    strcpy (path, p);
    strcat (path, "\\*.*");
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

void fetch_path (char *name, char *out, int size)
{
    int size2 = size;

    strcpy (out, start_path_data);
    if (!name)
	return;
    if (!strcmp (name, "FloppyPath"))
	strcat (out, "..\\shared\\adf\\");
    if (!strcmp (name, "hdfPath"))
	strcat (out, "..\\shared\\hdf\\");
    if (!strcmp (name, "KickstartPath"))
	strcat (out, "..\\shared\\rom\\");
    if (!strcmp (name, "ConfigurationPath"))
	strcat (out, "Configurations\\");
    if (start_data >= 0)
	regquerystr (NULL, name, out, &size); 
    if (out[0] == '\\' && (strlen(out) >= 2 && out[1] != '\\')) { /* relative? */
	strcpy (out, start_path_data);
	if (start_data >= 0) {
	    size2 -= strlen (out);
	    regquerystr (NULL, name, out, &size2);
	}
    }
    strip_slashes (out);
    if (!strcmp (name, "KickstartPath")) {
	DWORD v = GetFileAttributes (out);
	if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
	    strcpy (out, start_path_data);
    }
    fixtrailing (out);
}

int get_rom_path(char *out, int mode)
{
    char tmp[MAX_DPATH];

    tmp[0] = 0;
    switch (mode)
    {
	case 0:
	{
	    if (!strcmp (start_path_data, start_path_exe))
		strcpy (tmp, ".\\");
	    else
		strcpy (tmp, start_path_data);
	    if (GetFileAttributes (tmp) != INVALID_FILE_ATTRIBUTES) {
		char tmp2[MAX_DPATH];
		strcpy (tmp2, tmp);
		strcat (tmp2, "rom");
		if (GetFileAttributes (tmp2) != INVALID_FILE_ATTRIBUTES) {
		    strcpy (tmp, tmp2);
		} else {
		    strcpy (tmp2, tmp);
		    strcpy (tmp2, "roms");
		    if (GetFileAttributes (tmp2) != INVALID_FILE_ATTRIBUTES)
			strcpy (tmp, tmp2);
		}
	    }
	}
	break;
	case 1:
	{
	    char tmp2[MAX_DPATH];
	    strcpy (tmp2, start_path_new1);
	    strcat (tmp2, "..\\system\\rom");
	    if (isfilesindir (tmp2))
		strcpy (tmp, tmp2);
	}
	break;
	case 2:
	{
	    char tmp2[MAX_DPATH];
	    strcpy (tmp2, start_path_new2);
	    strcat (tmp2, "system\\rom");
	    if (isfilesindir (tmp2))
		strcpy (tmp, tmp2);
	}
	break;
	case 3:
	{
	    char tmp2[MAX_DPATH];
	    strcpy (tmp2, start_path_af);
	    strcat (tmp2, "..\\shared\\rom");
	    if (isfilesindir (tmp2))
		strcpy (tmp, tmp2);
	}
	break;
	default:
	return -1;
    }
    if (isfilesindir (tmp)) {
	strcpy (out, tmp);
	fixtrailing (out);
    }
    return out[0] ? 1 : 0;
}



void set_path (char *name, char *path)
{
    char tmp[MAX_DPATH];

    if (!path) {
	if (!strcmp (start_path_data, start_path_exe))
	    strcpy (tmp, ".\\");
	else
	    strcpy (tmp, start_path_data);
	if (!strcmp (name, "KickstartPath"))
	    strcat (tmp, "Roms");
	if (!strcmp (name, "ConfigurationPath"))
	    strcat (tmp, "Configurations");
	if (!strcmp (name, "ScreenshotPath"))
	    strcat (tmp, "Screenshots");
	if (!strcmp (name, "StatefilePath"))
	    strcat (tmp, "Savestates");
	if (!strcmp (name, "SaveimagePath"))
	    strcat (tmp, "SaveImages");
	if (!strcmp (name, "InputPath"))
	    strcat (tmp, "Inputrecordings");
    } else {
	strcpy (tmp, path);
    }
    strip_slashes (tmp);
    if (!strcmp (name, "KickstartPath")) {
	DWORD v = GetFileAttributes (tmp);
	if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY))
	    get_rom_path (tmp, 0);
	if ((af_path_2005 & 1) && path_type == PATH_TYPE_NEWAF) {
	    get_rom_path (tmp, 1);
	} else if ((af_path_2005 & 2) && path_type == PATH_TYPE_AMIGAFOREVERDATA) {
	    get_rom_path (tmp, 2);
	} else if (af_path_old && path_type == PATH_TYPE_OLDAF) {
	    get_rom_path (tmp, 3);
	}
    }
    fixtrailing (tmp);
    regsetstr (NULL, name, tmp);
}

static void initpath (char *name, char *path)
{
    if (regexists (NULL, name))
	return;
    set_path (name, NULL);
}

extern int scan_roms (int);
void read_rom_list (void)
{
    char tmp2[1000];
    int idx, idx2;
    UAEREG *fkey;
    char tmp[1000];
    int size, size2, exists;

    romlist_clear ();
    exists = regexiststree (NULL, "DetectedROMs");
    fkey = regcreatetree (NULL, "DetectedROMs");
    if (fkey == NULL)
	return;
    if (!exists || forceroms) {
	load_keyring (NULL, NULL);
	scan_roms (forceroms ? 0 : 1);
    }
    forceroms = 0;
    idx = 0;
    for (;;) {
	size = sizeof (tmp);
	size2 = sizeof (tmp2);
	if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
	    break;
	if (strlen (tmp) == 7 || strlen (tmp) == 13) {
	    int group = 0;
	    int subitem = 0;
	    idx2 = atol (tmp + 4);
	    if (strlen (tmp) == 13) {
		group = atol (tmp + 8);
		subitem = atol (tmp + 11);
	    }
	    if (idx2 >= 0 && strlen (tmp2) > 0) {
		struct romdata *rd = getromdatabyidgroup (idx2, group, subitem);
		if (rd) {
		    char *s = strchr (tmp2, '\"');
		    if (s && strlen (s) > 1) {
			char *s2 = my_strdup (s + 1);
			s = strchr (s2, '\"');
			if (s)
			    *s = 0;
			romlist_add (s2, rd);
			xfree (s2);
		    } else {
			romlist_add (tmp2, rd);
		    }
		}
	    }
	}
	idx++;
    }
    romlist_add (NULL, NULL);
    regclosetree (fkey);
}

static int parseversion (char **vs)
{
    char tmp[10];
    int i;

    i = 0;
    while (**vs >= '0' && **vs <= '9') {
	if (i >= sizeof (tmp))
	    return 0;
	tmp[i++] = **vs;
	(*vs)++;
    }
    if (**vs == '.')
	(*vs)++;
    tmp[i] = 0;
    return atol (tmp);
}

static int checkversion (char *vs)
{
    if (strlen (vs) < 10)
	return 0;
    if (memcmp (vs, "WinUAE ", 7))
	return 0;
    vs += 7;
    if (parseversion (&vs) > UAEMAJOR)
	return 0;
    if (parseversion (&vs) > UAEMINOR)
	return 0;
    if (parseversion (&vs) >= UAESUBREV)
	return 0;
    return 1;
}

static void WIN32_HandleRegistryStuff(void)
{
    RGBFTYPE colortype = RGBFB_NONE;
    DWORD dwType = REG_DWORD;
    DWORD dwDisplayInfoSize = sizeof (colortype);
    DWORD size;
    DWORD disposition;
    char path[MAX_DPATH] = "";
    char version[100];
    HKEY hWinUAEKeyLocal = NULL;
    HKEY rkey;
    char rpath1[MAX_DPATH], rpath2[MAX_DPATH], rpath3[MAX_DPATH];

    rpath1[0] = rpath2[0] = rpath3[0] = 0;
    rkey = HKEY_CLASSES_ROOT;
    if (os_winnt_admin)
        rkey = HKEY_LOCAL_MACHINE;
    else
        rkey = HKEY_CURRENT_USER;
    strcpy (rpath1, "Software\\Classes\\");
    strcpy (rpath2, rpath1);
    strcpy (rpath3, rpath1);
    strcat (rpath1, ".uae");
    strcat (rpath2, "WinUAE\\shell\\Edit\\command");
    strcat (rpath3, "WinUAE\\shell\\Open\\command");

    /* Create/Open the hWinUAEKey which points to our config-info */
    if (RegCreateKeyEx (rkey, rpath1, 0, "", REG_OPTION_NON_VOLATILE,
	KEY_WRITE | KEY_READ, NULL, &hWinUAEKey, &disposition) == ERROR_SUCCESS)
    {
	// Regardless of opening the existing key, or creating a new key, we will write the .uae filename-extension
	// commands in.  This way, we're always up to date.

	/* Set our (default) sub-key to point to the "WinUAE" key, which we then create */
	RegSetValueEx (hWinUAEKey, "", 0, REG_SZ, (CONST BYTE *)"WinUAE", strlen("WinUAE") + 1);

	if (RegCreateKeyEx (rkey, rpath2, 0, "", REG_OPTION_NON_VOLATILE,
			      KEY_WRITE | KEY_READ, NULL, &hWinUAEKeyLocal, &disposition) == ERROR_SUCCESS)
	{
	    /* Set our (default) sub-key to BE the "WinUAE" command for editing a configuration */
	    sprintf (path, "\"%sWinUAE.exe\" -f \"%%1\" -s use_gui=yes", start_path_exe);
	    RegSetValueEx (hWinUAEKeyLocal, "", 0, REG_SZ, (CONST BYTE *)path, strlen (path) + 1);
	    RegCloseKey (hWinUAEKeyLocal);
	}

	if(RegCreateKeyEx(rkey, rpath3, 0, "", REG_OPTION_NON_VOLATILE,
			      KEY_WRITE | KEY_READ, NULL, &hWinUAEKeyLocal, &disposition) == ERROR_SUCCESS)
	{
	    /* Set our (default) sub-key to BE the "WinUAE" command for launching a configuration */
	    sprintf (path, "\"%sWinUAE.exe\" -f \"%%1\"", start_path_exe);
	    RegSetValueEx (hWinUAEKeyLocal, "", 0, REG_SZ, (CONST BYTE *)path, strlen (path) + 1);
	    RegCloseKey (hWinUAEKeyLocal);
	}
	RegCloseKey (hWinUAEKey);
    }
    hWinUAEKey = NULL;

    /* Create/Open the hWinUAEKey which points our config-info */
    RegCreateKeyEx (HKEY_CURRENT_USER, "Software\\Arabuusimiehet\\WinUAE", 0, "", REG_OPTION_NON_VOLATILE,
	  KEY_WRITE | KEY_READ, NULL, &hWinUAEKey, &disposition);
    initpath ("FloppyPath", start_path_data);
    initpath ("KickstartPath", start_path_data);
    initpath ("hdfPath", start_path_data);
    initpath ("ConfigurationPath", start_path_data);
    initpath ("ScreenshotPath", start_path_data);
    initpath ("StatefilePath", start_path_data);
    initpath ("SaveimagePath", start_path_data);
    initpath ("VideoPath", start_path_data);
    initpath ("InputPath", start_path_data);
    if (!regexists (NULL, "MainPosX") || !regexists (NULL, "GUIPosX")) {
	int x = GetSystemMetrics (SM_CXSCREEN);
	int y = GetSystemMetrics (SM_CYSCREEN);
	x = (x - 800) / 2;
	y = (y - 600) / 2;
	if (x < 10)
	    x = 10;
	if (y < 10)
	    y = 10;
        /* Create and initialize all our sub-keys to the default values */
        regsetint (NULL, "MainPosX", x);
        regsetint (NULL, "MainPosY", y);
        regsetint (NULL, "GUIPosX", x);
        regsetint (NULL, "GUIPosY", y);
    }
    size = sizeof (version);
    if (regquerystr (NULL, "Version", version, &size)) {
        if (checkversion (version))
    	regsetstr (NULL, "Version", VersionStr);
    } else {
        regsetstr (NULL, "Version", VersionStr);
    }
    size = sizeof (version);
    if (regquerystr (NULL, "ROMCheckVersion", version, &size)) {
        if (checkversion (version)) {
    	    if (regsetstr (NULL, "ROMCheckVersion", VersionStr))
    		forceroms = 1;
	}
    } else {
        if (regsetstr (NULL, "ROMCheckVersion", VersionStr))
    	forceroms = 1;
    }

    regqueryint (NULL, "DirectDraw_Secondary", &ddforceram);
    if (regexists (NULL, "ConfigurationCache"))
	regqueryint (NULL, "ConfigurationCache", &configurationcache);
    else
	regsetint (NULL, "ConfigurationCache", configurationcache);
    regqueryint (NULL, "QuickStartMode", &quickstart);
    reopen_console ();
    fetch_path ("ConfigurationPath", path, sizeof (path));
    path[strlen (path) - 1] = 0;
    createdir (path);
    strcat (path, "\\Host");
    createdir (path);
    fetch_path ("ConfigurationPath", path, sizeof (path));
    strcat (path, "Hardware");
    createdir (path);
    fetch_path ("StatefilePath", path, sizeof (path));
    createdir (path);
    strcat (path, "default.uss");
    strcpy (savestate_fname, path);
    fetch_path ("InputPath", path, sizeof (path));
    createdir (path);
    regclosetree (read_disk_history ());
    read_rom_list ();
    load_keyring (NULL, NULL);
}

#if WINUAEPUBLICBETA > 0
static char *BETAMESSAGE = {
    "This is unstable beta software. Click cancel if you are not comfortable using software that is incomplete and can have serious programming errors."
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
    DWORD dwType, size, data;

    ft64.QuadPart = 0;
    for (;;) {
	FILETIME ft, sft;
	SYSTEMTIME st;
	char tmp1[MAX_DPATH];

	if (!hWinUAEKey)
	    break;
	if (GetModuleFileName (NULL, tmp1, sizeof tmp1) == 0)
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
	if (RegQueryValueEx (hWinUAEKey, "BetaToken", 0, &dwType, (LPBYTE)&regft64, &size) != ERROR_SUCCESS)
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
	int r;
	char title[MAX_DPATH];

	dwType = REG_DWORD;
	size = sizeof data;
	if (hWinUAEKey && RegQueryValueEx (hWinUAEKey, "Beta_Just_Shut_Up", 0, &dwType, (LPBYTE)&data, &size) == ERROR_SUCCESS) {
	    if (data == 68000) {
		write_log ("I was told to shut up :(\n");
		return 1;
	    }
	}

	_time64 (&ltime);
	t = _gmtime64 (&ltime);
	/* "expire" in 1 month */
	if (MAKEBD(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday) > WINUAEDATE + 100)
	    pre_gui_message ("This beta build of WinUAE is obsolete.\nPlease download newer version.");

	strcpy (title, "WinUAE Public Beta Disclaimer");
	strcat (title, BetaStr);
	r = MessageBox (NULL, BETAMESSAGE, title, MB_OKCANCEL | MB_TASKMODAL | MB_SETFOREGROUND | MB_ICONWARNING | MB_DEFBUTTON2);
	if (r == IDABORT || r == IDCANCEL)
	    return 0;
	if (ft64.QuadPart > 0) {
	    regft64 = ft64.QuadPart;
	    RegSetValueEx (hWinUAEKey, "BetaToken", 0, REG_QWORD, (LPBYTE)&regft64, sizeof regft64);
	}
    }
#endif
    return 1;
}

static int dxdetect (void)
{
#if !defined(WIN64)
    /* believe or not but this is MS supported way of detecting DX8+ */
    HMODULE h = LoadLibrary ("D3D8.DLL");
    char szWrongDXVersion[MAX_DPATH];
    if (h) {
	FreeLibrary (h);
	return 1;
    }
    WIN32GUI_LoadUIString (IDS_WRONGDXVERSION, szWrongDXVersion, MAX_DPATH);
    pre_gui_message (szWrongDXVersion);
    return 0;
#else
    return 1;
#endif
}

int os_winnt, os_winnt_admin, os_64bit, os_vista, os_winxp;

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
	write_log ("OpenProcessToken Error %u\n", GetLastError ());
	return FALSE;
    }

    // Call GetTokenInformation to get the buffer size.
    if(!GetTokenInformation (hToken, TokenGroups, NULL, dwSize, &dwSize)) {
	dwResult = GetLastError ();
	if(dwResult != ERROR_INSUFFICIENT_BUFFER) {
	    write_log ("GetTokenInformation Error %u\n", dwResult);
	    return FALSE;
	}
    }

    // Allocate the buffer.
    pGroupInfo = (PTOKEN_GROUPS)GlobalAlloc (GPTR, dwSize);

    // Call GetTokenInformation again to get the group information.
    if (!GetTokenInformation (hToken, TokenGroups, pGroupInfo, dwSize, &dwSize)) {
	write_log ("GetTokenInformation Error %u\n", GetLastError ());
	return FALSE;
    }

    // Create a SID for the BUILTIN\Administrators group.
    if (!AllocateAndInitializeSid (&SIDAuth, 2,
		 SECURITY_BUILTIN_DOMAIN_RID,
		 DOMAIN_ALIAS_RID_ADMINS,
		 0, 0, 0, 0, 0, 0,
		 &pSID)) {
	write_log ("AllocateAndInitializeSid Error %u\n", GetLastError ());
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
    os_winnt = 0;
    os_winnt_admin = 0;
    os_vista = 0;
    os_64bit = 0;

    pGetNativeSystemInfo = (PGETNATIVESYSTEMINFO)GetProcAddress (
	GetModuleHandle ("kernel32.dll"), "GetNativeSystemInfo");
    pIsUserAnAdmin = (PISUSERANADMIN)GetProcAddress (
	GetModuleHandle ("shell32.dll"), "IsUserAnAdmin");

    GetSystemInfo (&SystemInfo);
    if (pGetNativeSystemInfo)
	pGetNativeSystemInfo (&SystemInfo);
    osVersion.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
    if (GetVersionEx (&osVersion)) {
	if ((osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT) &&
	    (osVersion.dwMajorVersion <= 4))
	{
	    /* WinUAE not supported on this version of Windows... */
	    char szWrongOSVersion[MAX_DPATH];
	    WIN32GUI_LoadUIString (IDS_WRONGOSVERSION, szWrongOSVersion, MAX_DPATH);
	    pre_gui_message (szWrongOSVersion);
	    return FALSE;
	}
	if (osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT)
	    os_winnt = 1;
	if (osVersion.dwMajorVersion > 5 || (osVersion.dwMajorVersion == 5 && osVersion.dwMinorVersion >= 1))
	    os_winxp = 1;
	if (osVersion.dwMajorVersion >= 6)
	    os_vista = 1;
	if (SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
	    os_64bit = 1;
    }
    if (!os_winnt)
	return 0;
    os_winnt_admin = isadminpriv ();
    if (os_winnt_admin) {
	if (pIsUserAnAdmin) {
	    if (pIsUserAnAdmin ())
		os_winnt_admin++;
	} else {
	    os_winnt_admin++;
	}
    }

    return 1;
}

void create_afnewdir (int remove)
{
    char tmp[MAX_DPATH], tmp2[MAX_DPATH];

    if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_COMMON_DOCUMENTS, NULL, 0, tmp))) {
	fixtrailing (tmp);
	strcpy (tmp2, tmp);
	strcat (tmp2, "Amiga Files");
	strcpy (tmp, tmp2);
	strcat (tmp, "\\WinUAE");
	if (remove) {
	    if (GetFileAttributes (tmp) != INVALID_FILE_ATTRIBUTES) {
		RemoveDirectory (tmp);
		RemoveDirectory (tmp2);
	    }
	} else {
	    CreateDirectory (tmp2, NULL);
	    CreateDirectory (tmp, NULL);
	}
    }
}

static void getstartpaths (void)
{
    char *posn, *p;
    char tmp[MAX_DPATH], tmp2[MAX_DPATH], prevpath[MAX_DPATH];
    DWORD v;
    UAEREG *key;
    char xstart_path_uae[MAX_DPATH], xstart_path_old[MAX_DPATH];
    char xstart_path_new1[MAX_DPATH], xstart_path_new2[MAX_DPATH];

    path_type = -1;
    xstart_path_uae[0] = xstart_path_old[0] = xstart_path_new1[0] = xstart_path_new2[0] = 0;
    key = regcreatetree (NULL, NULL);
    if (key)  {
	DWORD size = sizeof (prevpath);
	if (!regquerystr (key, "PathMode", prevpath, &size))
	    prevpath[0] = 0;
	regclosetree (key);
    }
    if (!strcmp (prevpath, "WinUAE"))
	path_type = PATH_TYPE_WINUAE;
    if (!strcmp (prevpath, "WinUAE_2"))
	path_type = PATH_TYPE_NEWWINUAE;
    if (!strcmp (prevpath, "AF"))
	path_type = PATH_TYPE_OLDAF;
    if (!strcmp (prevpath, "AF2005"))
	path_type = PATH_TYPE_NEWAF;
    if (!strcmp (prevpath, "AMIGAFOREVERDATA"))
	path_type = PATH_TYPE_AMIGAFOREVERDATA;

    strcpy (start_path_exe, _pgmptr);
    if((posn = strrchr (start_path_exe, '\\')))
	posn[1] = 0;

    strcpy (tmp, start_path_exe);
    strcat (tmp, "roms");
    if (isfilesindir (tmp)) {
	strcpy (xstart_path_uae, start_path_exe);
    }
    strcpy (tmp, start_path_exe);
    strcat (tmp, "configurations");
    if (isfilesindir (tmp)) {
	strcpy (xstart_path_uae, start_path_exe);
    }

    strcpy (tmp, start_path_exe);
    strcat (tmp, "..\\system\\rom\\rom.key");
    v = GetFileAttributes (tmp);
    if (v != INVALID_FILE_ATTRIBUTES) {
	af_path_old = 1;
	strcpy (xstart_path_old, start_path_exe);
	strcat (xstart_path_old, "..\\system\\");
	strcpy (start_path_af, xstart_path_old);
    } else {
	strcpy (tmp, start_path_exe);
	strcat (tmp, "..\\shared\\rom\\rom.key");
	v = GetFileAttributes (tmp);
	if (v != INVALID_FILE_ATTRIBUTES) {
	    af_path_old = 1;
	    strcpy (xstart_path_old, start_path_exe);
	    strcat (xstart_path_old, "..\\shared\\");
	    strcpy (start_path_af, xstart_path_old);
	}
    }

    p = getenv ("AMIGAFOREVERDATA");
    if (p) {
	strcpy (tmp, p);
	fixtrailing (tmp);
	strcpy (start_path_new2, p);
	fixtrailing (start_path_af);
	v = GetFileAttributes (tmp);
	if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
	    strcpy (xstart_path_new2, start_path_af);
	    strcat (xstart_path_new2, "WinUAE\\");
	    af_path_2005 |= 2;
	}
    }

    {
	if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_COMMON_DOCUMENTS, NULL, 0, tmp))) {
	    fixtrailing (tmp);
	    strcpy (tmp2, tmp);
	    strcat (tmp2, "Amiga Files\\");
	    strcpy (tmp, tmp2);
	    strcat (tmp, "WinUAE");
	    v = GetFileAttributes (tmp);
	    if (v == INVALID_FILE_ATTRIBUTES || (v & FILE_ATTRIBUTE_DIRECTORY)) {
		char *p;
		strcpy (xstart_path_new1, tmp2);
		strcat (xstart_path_new1, "WinUAE\\");
		strcpy (xstart_path_uae, start_path_exe);
		strcpy (start_path_new1, xstart_path_new1);
		p = tmp2 + strlen (tmp2);
		strcpy (p, "System");
		if (isfilesindir (tmp2)) {
		    af_path_2005 |= 1;
		} else {
		    strcpy (p, "Shared");
		    if (isfilesindir (tmp2)) {
			af_path_2005 |= 1;
		    }
		}
	    }
	}
    }

    if (start_data == 0) {
	start_data = 1;
	if (path_type == 0 && xstart_path_uae[0]) {
	    strcpy (start_path_data, xstart_path_uae);
	} else if (path_type == PATH_TYPE_OLDAF && af_path_old && xstart_path_old[0]) {
	    strcpy (start_path_data, xstart_path_old);
	} else if (path_type == PATH_TYPE_NEWWINUAE && xstart_path_new1[0]) {
	    strcpy (start_path_data, xstart_path_new1);
	    create_afnewdir(0);
	} else if (path_type == PATH_TYPE_NEWAF && (af_path_2005 & 1) && xstart_path_new1[0]) {
	    strcpy (start_path_data, xstart_path_new1);
	    create_afnewdir(0);
	} else if (path_type == PATH_TYPE_AMIGAFOREVERDATA && (af_path_2005 & 2) && xstart_path_new2[0]) {
	    strcpy (start_path_data, xstart_path_new2);
	} else if (path_type < 0) {
	    path_type = 0;
	    strcpy (start_path_data, xstart_path_uae);
	    if (af_path_old) {
		path_type = PATH_TYPE_OLDAF;
		strcpy (start_path_data, xstart_path_old);
	    }
	    if (af_path_2005 & 1) {
		path_type = PATH_TYPE_NEWAF;
		create_afnewdir (1);
		strcpy (start_path_data, xstart_path_new1);
	    }
	    if (af_path_2005 & 2) {
		strcpy (tmp, xstart_path_new2);
		strcat (tmp, "system\\rom");
		if (isfilesindir (tmp)) {
		    path_type = PATH_TYPE_AMIGAFOREVERDATA;
		} else {
		    strcpy (tmp, xstart_path_new2);
		    strcat (tmp, "shared\\rom");
		    if (isfilesindir (tmp)) {
			path_type = PATH_TYPE_AMIGAFOREVERDATA;
		    } else {
			path_type = PATH_TYPE_NEWWINUAE;
		    }
		}
		strcpy (start_path_data, xstart_path_new2);
	    }
	}
    }

    v = GetFileAttributes (start_path_data);
    if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY) || start_data == 0 || start_data == -2) {
	strcpy (start_path_data, start_path_exe);
    }
    fixtrailing (start_path_data);
    GetFullPathName (start_path_data, sizeof tmp, tmp, NULL);
    strcpy (start_path_data, tmp);
    SetCurrentDirectory (start_path_data);
}

extern void test (void);
extern int screenshotmode, postscript_print_debugging, sound_debug, log_uaeserial;
extern int force_direct_catweasel, sound_mode_skip, maxmem;

extern DWORD_PTR cpu_affinity, cpu_paffinity;
static DWORD_PTR original_affinity = -1;

static int getval(char *s)
{
    int base = 10;
    int v;
    char *endptr;

    if (s[0] == '0' && toupper(s[1]) == 'X')
	s += 2, base = 16;
    v = strtol (s, &endptr, base);
    if (*endptr != '\0' || *s == '\0')
	return 0;
    return v;
}

static void makeverstr(char *s)
{
#if WINUAEBETA > 0
    sprintf (BetaStr, " (%sBeta %d, %d.%02d.%02d)", WINUAEPUBLICBETA > 0 ? "Public " : "", WINUAEBETA,
	GETBDY(WINUAEDATE), GETBDM(WINUAEDATE), GETBDD(WINUAEDATE));
    sprintf (s, "WinUAE %d.%d.%d%s%s",
	UAEMAJOR, UAEMINOR, UAESUBREV, WINUAEREV, BetaStr);
#else
    sprintf(s, "WinUAE %d.%d.%d%s (%d.%02d.%02d)",
	UAEMAJOR, UAEMINOR, UAESUBREV, WINUAEREV, GETBDY(WINUAEDATE), GETBDM(WINUAEDATE), GETBDD(WINUAEDATE));
#endif
    if (strlen (WINUAEEXTRA) > 0) {
	strcat (s, " ");
	strcat (s, WINUAEEXTRA);
    }
}

static int multi_display = 1;
static char *inipath = NULL;

static int process_arg(char **xargv)
{
    int i, argc, xargc;
    char **argv;

    xargc = 0;
    argc = __argc; argv = __argv;
    xargv[xargc++] = my_strdup(argv[0]);
    for (i = 1; i < argc; i++) {
	char *arg = argv[i];
	if (!strcmp (arg, "-log")) {
	    console_logging = 1;
	    continue;
	}
#ifdef FILESYS
	if (!strcmp (arg, "-rdbdump")) {
	    do_rdbdump = 1;
	    continue;
	}
	if (!strcmp (arg, "-disableharddrivesafetycheck")) {
	    harddrive_dangerous = 0x1234dead;
	    continue;
	}
	if (!strcmp (arg, "-noaspifiltering")) {
	    aspi_allow_all = 1;
	    continue;
	}
#endif
	if (!strcmp (arg, "-norawinput")) {
	    no_rawinput = 1;
	    continue;
	}
	if (!strcmp (arg, "-rawkeyboard")) {
	    rawkeyboard = 1;
	    continue;
	}
	if (!strcmp (arg, "-scsilog")) {
	    log_scsi = 1;
	    continue;
	}
	if (!strcmp (arg, "-netlog")) {
	    log_net = 1;
	    continue;
	}
	if (!strcmp (arg, "-seriallog")) {
	    log_uaeserial = 1;
	    continue;
	}
	if (!strcmp (arg, "-rplog")) {
	    log_rp = 1;
	    continue;
	}
	if (!strcmp (arg, "-nomultidisplay")) {
	    multi_display = 0;
	    continue;
	}
	if (!strcmp (arg, "-legacypaths")) {
	    start_data = -2;
	    continue;
	}
	if (!strcmp (arg, "-screenshotbmp")) {
	    screenshotmode = 0;
	    continue;
	}
	if (!strcmp (arg, "-psprintdebug")) {
	    postscript_print_debugging = 1;
	    continue;
	}
	if (!strcmp (arg, "-sounddebug")) {
	    sound_debug = 1;
	    continue;
	}
	if (!strcmp (arg, "-directcatweasel")) {
	    force_direct_catweasel = 1;
	    if (i + 1 < argc)
		force_direct_catweasel = getval (argv[++i]);
	    continue;
	}
        if (!strcmp (arg, "-forcerdtsc")) {
	    userdtsc = 1;
	    continue;
	}
	if (!strcmp (arg, "-ddsoftwarecolorkey")) {
	    extern int ddsoftwarecolorkey;
	    ddsoftwarecolorkey = 1;
	    continue;
	}

	if (i + 1 < argc) {
	    char *np = argv[i + 1];

	    if (!strcmp (arg, "-ddforcemode")) {
		extern int ddforceram;
		ddforceram = getval (np);
		if (ddforceram < 0 || ddforceram > 3)
		    ddforceram = 0;
		i++;
		continue;
	    }
	    if (!strcmp (arg, "-affinity")) {
		cpu_affinity = getval (np);
		i++;
		if (cpu_affinity == 0)
		    cpu_affinity = original_affinity;
		SetThreadAffinityMask (GetCurrentThread (), cpu_affinity);
		continue;
	    }
	    if (!strcmp (arg, "-paffinity")) {
		cpu_paffinity = getval (np);
		i++;
		if (cpu_paffinity == 0)
		    cpu_paffinity = original_affinity;
		SetProcessAffinityMask (GetCurrentProcess (), cpu_paffinity);
		continue;
	    }
	    if (!strcmp (arg, "-datapath")) {
		i++;
		strcpy(start_path_data, np);
		start_data = -1;
		continue;
	    }
	    if (!strcmp (arg, "-maxmem")) {
		i++;
		maxmem = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-soundmodeskip")) {
		i++;
		sound_mode_skip = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-ini")) {
		i++;
		inipath = my_strdup (np);
		continue;
	    }
	    if (!strcmp (arg, "-p96skipmode")) {
		extern int p96skipmode;
		i++;
		p96skipmode = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-minidumpmode")) {
		i++;
		minidumpmode = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-jitevent")) {
		i++;
		pissoff_value = getval (np);
		continue;
	    }
#ifdef RETROPLATFORM
	    if (!strcmp (arg, "-rphost")) {
		i++;
		rp_param = my_strdup (np);
		continue;
	    }
	    if (!strcmp (arg, "-rpescapekey")) {
		i++;
		rp_rpescapekey = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-rpescapeholdtime")) {
		i++;
		rp_rpescapeholdtime = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-rpscreenmode")) {
		i++;
		rp_screenmode = getval (np);
		continue;
	    }
	    if (!strcmp (arg, "-rpinputmode")) {
		i++;
		rp_inputmode = getval (np);
		continue;
	    }
#endif
	}
	xargv[xargc++] = my_strdup (arg);
    }
#if 0
    argv = 0;
    argv[0] = 0;
#endif
    return xargc;
}

static int PASCAL WinMain2 (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    HANDLE hMutex;
    char **argv;
    int argc, i;

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

    if (!osdetect ())
	return 0;
    if (!dxdetect ())
	return 0;

    hInst = hInstance;
    hMutex = CreateMutex (NULL, FALSE, "WinUAE Instantiated"); // To tell the installer we're running
#ifdef AVIOUTPUT
    AVIOutput_Initialize ();
#endif

    argv = xcalloc (sizeof (char*),  __argc);
    argc = process_arg (argv);

    reginitializeinit (inipath);
    getstartpaths ();
    makeverstr (VersionStr);

    logging_init ();
    write_log ("params:\n");
    for (i = 1; i < __argc; i++)
	write_log ("%d: '%s'\n", i, __argv[i]);

    if (WIN32_RegisterClasses () && WIN32_InitLibraries () && DirectDraw_Start (NULL)) {
	DEVMODE devmode;
	DWORD i;

	DirectDraw_Release ();
	write_log ("Enumerating display devices.. \n");
	enumeratedisplays (multi_display);
	write_log ("Sorting devices and modes..\n");
	sortdisplays ();
	write_log ("Display buffer mode = %d\n", ddforceram);
	write_log ("Enumerating sound devices:\n");
	for (i = 0; i < enumerate_sound_devices (); i++) {
	    write_log ("%d:%s: %s\n", i, sound_devices[i].type == SOUND_DEVICE_DS ? "DS" : "AL", sound_devices[i].name);
	}
	write_log ("done\n");
	memset (&devmode, 0, sizeof(devmode));
	devmode.dmSize = sizeof (DEVMODE);
	if (EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &devmode)) {
	    default_freq = devmode.dmDisplayFrequency;
	    if (default_freq >= 70)
		default_freq = 70;
	    else
		default_freq = 60;
	}
	WIN32_HandleRegistryStuff ();
	WIN32_InitLang ();
	WIN32_InitHtmlHelp ();
	DirectDraw_Release ();
	if (betamessage ()) {
	    keyboard_settrans ();
#ifdef CATWEASEL
	    catweasel_init ();
#endif
#ifdef PARALLEL_PORT
	    paraport_mask = paraport_init ();
#endif
	    createIPC ();
	    enumserialports ();
	    real_main (argc, argv);
	}
    }

    closeIPC ();
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
    close_console ();
    _fcloseall ();
#ifdef RETROPLATFORM
    rp_free ();
#endif
    if(hWinUAEKey)
	RegCloseKey (hWinUAEKey);
    CloseHandle (hMutex);
    WIN32_CleanupLibraries ();
    WIN32_UnregisterClasses ();
#ifdef _DEBUG
    // show memory leaks
    //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    for (i = 0; i < argc; i++)
	xfree (argv[i]);
    xfree (argv);
    return FALSE;
}

#if 0
int execute_command (char *cmd)
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
static int drvsampleres[] = {
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
	HRSRC res = FindResource (NULL, MAKEINTRESOURCE (drvsampleres[i + 0]), "WAVE");
	if (res != 0) {
	    HANDLE h = LoadResource (NULL, res);
	    int len = SizeofResource (NULL, res);
	    uae_u8 *p = LockResource (h);
	    s->p = decodewav (p, &len);
	    s->len = len;
	} else {
	    ok = 0;
	}
    }
    return ok;
}

#if defined(WIN64)

static LONG WINAPI WIN32_ExceptionFilter( struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
    write_log ("EVALEXCEPTION!\n");
    return EXCEPTION_EXECUTE_HANDLER;
}
#else

#if 0
#include <errorrep.h>
#endif
typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType,
    CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

/* Gah, don't look at this crap, please.. */
static void efix (DWORD *regp, void *p, void *ps, int *got)
{
    DWORD reg = *regp;
    if (p >= (void*)reg && p < (void*)(reg + 32)) {
	*regp = (DWORD)ps;
	*got = 1;
    }
}

static void savedump (MINIDUMPWRITEDUMP dump, HANDLE f, struct _EXCEPTION_POINTERS *pExceptionPointers)
{
    MINIDUMP_EXCEPTION_INFORMATION exinfo;
    MINIDUMP_USER_STREAM_INFORMATION musi, *musip;
    MINIDUMP_USER_STREAM mus[2], *musp;
    char *log;
    int loglen;

    musip = NULL;
    log = save_log (TRUE, &loglen);
    if (log) {
	musi.UserStreamArray = mus;
	musi.UserStreamCount = 1;
	musp = &mus[0];
	musp->Type = LastReservedStream + 1;
	musp->Buffer = log;
	musp->BufferSize = loglen;
	musip = &musi;
	log = save_log (FALSE, &loglen);
	if (log) {
	    musi.UserStreamCount++;
	    musp = &mus[1];
	    musp->Type = LastReservedStream + 2;
	    musp->Buffer = log;
	    musp->BufferSize = loglen;
	}
    }
    exinfo.ThreadId = GetCurrentThreadId ();
    exinfo.ExceptionPointers = pExceptionPointers;
    exinfo.ClientPointers = 0;
    dump (GetCurrentProcess (), GetCurrentProcessId (), f, minidumpmode, &exinfo, musip, NULL);
}

LONG WINAPI WIN32_ExceptionFilter (struct _EXCEPTION_POINTERS *pExceptionPointers, DWORD ec)
{
    static uae_u8 *prevpc;
    LONG lRet = EXCEPTION_CONTINUE_SEARCH;
    PEXCEPTION_RECORD er = pExceptionPointers->ExceptionRecord;
    PCONTEXT ctx = pExceptionPointers->ContextRecord;

    /* Check possible access violation in 68010+/compatible mode disabled if PC points to non-existing memory */
    if (ec == EXCEPTION_ACCESS_VIOLATION && !er->ExceptionFlags &&
	er->NumberParameters >= 2 && !er->ExceptionInformation[0] && regs.pc_p) {
	    void *p = (void*)er->ExceptionInformation[1];
	    write_log ("ExceptionFilter Trap: %p %p %p\n", p, regs.pc_p, prevpc);
	    if ((p >= (void*)regs.pc_p && p < (void*)(regs.pc_p + 32))
		|| (p >= (void*)prevpc && p < (void*)(prevpc + 32))) {
		int got = 0;
		uaecptr opc = m68k_getpc (&regs);
		void *ps = get_real_address (0);
		m68k_dumpstate (0, 0);
		efix (&ctx->Eax, p, ps, &got);
		efix (&ctx->Ebx, p, ps, &got);
		efix (&ctx->Ecx, p, ps, &got);
		efix (&ctx->Edx, p, ps, &got);
		efix (&ctx->Esi, p, ps, &got);
		efix (&ctx->Edi, p, ps, &got);
		write_log ("Access violation! (68KPC=%08X HOSTADDR=%p)\n", M68K_GETPC, p);
		if (got == 0) {
		    write_log ("failed to find and fix the problem (%p). crashing..\n", p);
		} else {
		    void *ppc = regs.pc_p;
		    m68k_setpc (&regs, 0);
		    if (ppc != regs.pc_p) {
			prevpc = (uae_u8*)ppc;
		    }
		    exception2 (opc, (uaecptr)p);
		    lRet = EXCEPTION_CONTINUE_EXECUTION;
		}
	    }
    }
#ifndef	_DEBUG
    if (lRet == EXCEPTION_CONTINUE_SEARCH) {
	char path[MAX_DPATH];
	char path2[MAX_DPATH];
	char msg[1024];
	char *p;
	HMODULE dll = NULL;
	struct tm when;
	__time64_t now;

	if (os_winnt && GetModuleFileName (NULL, path, MAX_DPATH)) {
	    char *slash = strrchr (path, '\\');
	    _time64 (&now);
	    when = *_localtime64 (&now);
	    strcpy (path2, path);
	    if (slash) {
		strcpy (slash + 1, "DBGHELP.DLL");
		dll = WIN32_LoadLibrary (path);
	    }
	    slash = strrchr (path2, '\\');
	    if (slash)
		p = slash + 1;
	    else
		p = path2;
	    sprintf (p, "winuae_%d%d%d%d_%d%02d%02d_%02d%02d%02d.dmp",
		UAEMAJOR, UAEMINOR, UAESUBREV, WINUAEBETA,
		when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);
	    if (dll == NULL)
		dll = WIN32_LoadLibrary ("DBGHELP.DLL");
	    if (dll) {
		MINIDUMPWRITEDUMP dump = (MINIDUMPWRITEDUMP)GetProcAddress (dll, "MiniDumpWriteDump");
		if (dump) {
		    HANDLE f = CreateFile (path2, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		    if (f != INVALID_HANDLE_VALUE) {
			flush_log ();
			savedump (dump, f, pExceptionPointers);
			CloseHandle (f);
			if (isfullscreen () <= 0) {
			    sprintf (msg, "Crash detected. MiniDump saved as:\n%s\n", path2);
			    MessageBox (NULL, msg, "Crash", MB_OK | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND);
			}
		    }
		}
	    }
	}
    }
#endif
#if 0
	HMODULE hFaultRepDll = LoadLibrary ("FaultRep.dll") ;
	if (hFaultRepDll) {
	    pfn_REPORTFAULT pfn = (pfn_REPORTFAULT)GetProcAddress (hFaultRepDll, "ReportFault");
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

const static GUID GUID_DEVINTERFACE_HID =  { 0x4D1E55B2L, 0xF16F, 0x11CF,
    { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

typedef ULONG (CALLBACK *SHCHANGENOTIFYREGISTER)
    (HWND hwnd,
    int fSources,
    LONG fEvents,
    UINT wMsg,
    int cEntries,
    const SHChangeNotifyEntry *pshcne);
typedef BOOL (CALLBACK *SHCHANGENOTIFYDEREGISTER)(ULONG ulID);

void addnotifications (HWND hwnd, int remove)
{
    static ULONG ret;
    static HDEVNOTIFY hdn;
    LPITEMIDLIST ppidl;
    SHCHANGENOTIFYREGISTER pSHChangeNotifyRegister;
    SHCHANGENOTIFYDEREGISTER pSHChangeNotifyDeregister;

    pSHChangeNotifyRegister = (SHCHANGENOTIFYREGISTER)GetProcAddress (
	GetModuleHandle ("shell32.dll"), "SHChangeNotifyRegister");
    pSHChangeNotifyDeregister = (SHCHANGENOTIFYDEREGISTER)GetProcAddress (
	GetModuleHandle ("shell32.dll"), "SHChangeNotifyDeregister");

    if (remove) {
	if (ret > 0 && pSHChangeNotifyDeregister)
	    pSHChangeNotifyDeregister (ret);
	ret = 0;
	if (hdn)
	    UnregisterDeviceNotification (hdn);
	hdn = 0;
    } else {
	DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = { 0 };
	if(pSHChangeNotifyRegister && SHGetSpecialFolderLocation (hwnd, CSIDL_DESKTOP, &ppidl) == NOERROR) {
	    SHChangeNotifyEntry shCNE;
	    shCNE.pidl = ppidl;
	    shCNE.fRecursive = TRUE;
	    ret = pSHChangeNotifyRegister (hwnd, SHCNE_DISKEVENTS, SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED,
		WM_USER + 2, 1, &shCNE);
	}
	NotificationFilter.dbcc_size = 
	    sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
	hdn = RegisterDeviceNotification (hwnd,  &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
}

void systray (HWND hwnd, int remove)
{
    NOTIFYICONDATA nid;
    BOOL v;

#ifdef RETROPLATFORM
    if (rp_isactive ())
	return;
#endif
    //write_log ("notif: systray(%x,%d)\n", hwnd, remove);
    if (!remove) {
	TaskbarRestart = RegisterWindowMessage (TEXT ("TaskbarCreated"));
	TaskbarRestartHWND = hwnd;
	//write_log ("notif: taskbarrestart = %d\n", TaskbarRestart);
    } else {
	TaskbarRestart = 0;
	hwnd = TaskbarRestartHWND;
    }
    if (!hwnd)
	return;
    memset (&nid, 0, sizeof (nid));
    nid.cbSize = sizeof (nid);
    nid.hWnd = hwnd;
    nid.hIcon = LoadIcon (hInst, (LPCSTR)MAKEINTRESOURCE(IDI_APPICON));
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    nid.uCallbackMessage = WM_USER + 1;
    v = Shell_NotifyIcon (remove ? NIM_DELETE : NIM_ADD, &nid);
    //write_log ("notif: Shell_NotifyIcon returned %d\n", v);
    if (v) {
	if (remove)
	    TaskbarRestartHWND = NULL;
    } else {
	DWORD err = GetLastError ();
	write_log ("Notify error code = %x (%d)\n",  err, err);
    }
}

static void systraymenu (HWND hwnd)
{
    POINT pt;
    HMENU menu, menu2, drvmenu;
    int drvs[] = { ID_ST_DF0, ID_ST_DF1, ID_ST_DF2, ID_ST_DF3, -1 };
    int i;
    char text[100];

    WIN32GUI_LoadUIString (IDS_STMENUNOFLOPPY, text, sizeof (text));
    GetCursorPos (&pt);
    menu = LoadMenu (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDM_SYSTRAY));
    if (!menu)
	return;
    menu2 = GetSubMenu (menu, 0);
    drvmenu = GetSubMenu (menu2, 1);
    EnableMenuItem (menu2, ID_ST_HELP, pHtmlHelp ? MF_ENABLED : MF_GRAYED);
    i = 0;
    while (drvs[i] >= 0) {
	char s[MAX_DPATH];
	if (currprefs.df[i][0])
	    sprintf (s, "DF%d: [%s]", i, currprefs.df[i]);
	else
	    sprintf (s, "DF%d: [%s]", i, text);
	ModifyMenu (drvmenu, drvs[i], MF_BYCOMMAND | MF_STRING, drvs[i], s);
	EnableMenuItem (menu2, drvs[i], currprefs.dfxtype[i] < 0 ? MF_GRAYED : MF_ENABLED);
	i++;
    }
    if (isfullscreen () <= 0)
	SetForegroundWindow (hwnd);
    TrackPopupMenu (menu2, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
	pt.x, pt.y, 0, hwnd, NULL);
    PostMessage (hwnd, WM_NULL, 0, 0);
    DestroyMenu (menu);
}

static void LLError(const char *s)
{
    DWORD err = GetLastError ();

    if (err == ERROR_MOD_NOT_FOUND || err == ERROR_DLL_NOT_FOUND)
	return;
    write_log ("%s failed to open %d\n", s, err);
}

HMODULE WIN32_LoadLibrary (const char *name)
{
    HMODULE m = NULL;
    char *newname;
    DWORD err = -1;
#ifdef CPU_64_BIT
    char *p;
#endif
    int round;

    newname = xmalloc (strlen (name) + 1 + 10);
    if (!newname)
	return NULL;
    for (round = 0; round < 4; round++) {
	char *s;
	strcpy (newname, name);
#ifdef CPU_64_BIT
	switch(round)
	{
	    case 0:
	    p = strstr (newname,"32");
	    if (p) {
		p[0] = '6';
		p[1] = '4';
	    }
	    break;
	    case 1:
	    p = strchr (newname,'.');
	    strcpy(p,"_64");
	    strcat(p, strchr (name,'.'));
	    break;
	    case 2:
	    p = strchr (newname,'.');
	    strcpy (p,"64");
	    strcat (p, strchr (name,'.'));
	    break;
	}
#endif
	s = xmalloc (strlen (start_path_exe) + strlen (WIN32_PLUGINDIR) + strlen (newname) + 1);
	if (s) {
	    sprintf (s, "%s%s%s", start_path_exe, WIN32_PLUGINDIR, newname);
	    m = LoadLibrary (s);
	    if (m)
		goto end;
	    sprintf (s, "%s%s", start_path_exe, newname);
	    m = LoadLibrary (s);
	    if (m)
		goto end;
	    sprintf (s, "%s%s%s", start_path_exe, WIN32_PLUGINDIR, newname);
	    LLError(s);
	    xfree (s);
	}
	m = LoadLibrary (newname);
	if (m)
	    goto end;
	LLError (newname);
#ifndef CPU_64_BIT
	break;
#endif
    }
end:
    xfree (newname);
    return m;
}

typedef BOOL (CALLBACK* SETPROCESSDPIAWARE)(void);
typedef BOOL (CALLBACK* CHANGEWINDOWMESSAGEFILTER)(UINT, DWORD);

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SETPROCESSDPIAWARE pSetProcessDPIAware;
    DWORD_PTR sys_aff;
    HANDLE thread;

    original_affinity = 1;
    GetProcessAffinityMask (GetCurrentProcess(), &original_affinity, &sys_aff);

    thread = GetCurrentThread ();
    //original_affinity = SetThreadAffinityMask(thread, 1);

#if 0
#define MSGFLT_ADD 1
    CHANGEWINDOWMESSAGEFILTER pChangeWindowMessageFilter;
    pChangeWindowMessageFilter = (CHANGEWINDOWMESSAGEFILTER)GetProcAddress(
	GetModuleHandle("user32.dll"), "ChangeWindowMessageFilter");
    if (pChangeWindowMessageFilter)
	pChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
#endif

    pSetProcessDPIAware = (SETPROCESSDPIAWARE)GetProcAddress (
	GetModuleHandle ("user32.dll"), "SetProcessDPIAware");
    if (pSetProcessDPIAware)
	pSetProcessDPIAware ();

    __try {
	WinMain2 (hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    } __except(WIN32_ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
    }
    //SetThreadAffinityMask(thread, original_affinity);
    return FALSE;
}


