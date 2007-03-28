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

#include "sysdeps.h"
#include "options.h"
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
#include "win32gui.h"
#include "resource.h"
#include "autoconf.h"
#include "gui.h"
#include "sys/mman.h"
#include "avioutput.h"
#include "ahidsound.h"
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
#include "audio.h"
#include "akiko.h"
#include "cdtv.h"

extern FILE *debugfile;
extern int console_logging;
static OSVERSIONINFO osVersion;
static SYSTEM_INFO SystemInfo;

int useqpc = 0; /* Set to TRUE to use the QueryPerformanceCounter() function instead of rdtsc() */
int qpcdivisor = 0;
int cpu_mmx = 0;
static int no_rdtsc;

HINSTANCE hInst = NULL;
HMODULE hUIDLL = NULL;
HWND (WINAPI *pHtmlHelp)(HWND, LPCSTR, UINT, LPDWORD) = NULL;
HWND hAmigaWnd, hMainWnd, hHiddenWnd;
RECT amigawin_rect;
static UINT TaskbarRestart;
static int TaskbarRestartOk;
static int forceroms;

char VersionStr[256];
char BetaStr[64];

int in_sizemove;
int manual_painting_needed;
int manual_palette_refresh_needed;
int win_x_diff, win_y_diff;

int toggle_sound;
int paraport_mask;

HKEY hWinUAEKey = NULL;
COLORREF g_dwBackgroundColor;

static int emulation_paused;
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

char start_path_data[MAX_DPATH];
char start_path_exe[MAX_DPATH];
char start_path_af[MAX_DPATH];
char help_file[MAX_DPATH];
int af_path_2005, af_path_old, winuae_path;

extern int harddrive_dangerous, do_rdbdump, aspi_allow_all, no_rawinput;
int log_scsi;
DWORD quickstart = 1;

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
	timeend();
	return timebegin();
    }
    timeon = 0;
    if (timeBeginPeriod (mm_timerres) == TIMERR_NOERROR) {
	timeon = 1;
	return 1;
    }
    write_log ("TimeBeginPeriod() failed\n");
    return 0;
}

static void init_mmtimer (void)
{
    TIMECAPS tc;
    mm_timerres = 0;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR)
	return;
    mm_timerres = min(max(tc.wPeriodMin, 1), tc.wPeriodMax);
    timehandle = CreateEvent (NULL, TRUE, FALSE, NULL);
}

int sleep_resolution;
void sleep_millis (int ms)
{
    UINT TimerEvent;
    int start = read_processor_time();
    if (mm_timerres <= 0 || timermode) {
	Sleep (ms);
    } else {
	TimerEvent = timeSetEvent (ms, 0, timehandle, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
	if (!TimerEvent) {
	    Sleep (ms);
	} else {
	    WaitForSingleObject (timehandle, ms);
	    ResetEvent (timehandle);
	    timeKillEvent (TimerEvent);
	}
    }
    idletime += read_processor_time() - start;
}

void sleep_millis_busy (int ms)
{
    if (timermode < 0)
	return;
    sleep_millis (ms);
}

#include <process.h>
static volatile int dummythread_die;
static void dummythread (void *dummy)
{
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    while (!dummythread_die);
}


STATIC_INLINE frame_time_t read_processor_time_qpc (void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    if (qpcdivisor == 0)
	return (frame_time_t)(counter.LowPart);
    return (frame_time_t)(counter.QuadPart >> qpcdivisor);
}

frame_time_t read_processor_time (void)
{
    frame_time_t foo;

    if (useqpc) /* No RDTSC or RDTSC is not stable */
	return read_processor_time_qpc();

#if defined(X86_MSVC_ASSEMBLY)
    {
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
    }
#else
    foo = 0;
#endif
#ifdef HIBERNATE_TEST
    if (rpt_skip_trigger) {
	foo += rpt_skip_trigger;
	rpt_skip_trigger = 0;
    }
#endif
    return foo;
}


static uae_u64 win32_read_processor_time (void)
{
#if defined(X86_MSVC_ASSEMBLY)
    uae_u32 foo, bar;
    __asm
    {
	rdtsc
	mov foo, eax
	mov bar, edx
    }
    return ((uae_u64)bar << 32) | foo;
#else
    return 0;
#endif
}

static int figure_processor_speed (void)
{
    extern volatile frame_time_t vsynctime;
    extern unsigned long syncbase;
    uae_u64 clockrate, clockrateidle, qpfrate, ratea1, ratea2;
    uae_u32 rate1, rate2;
    double limit, clkdiv = 1, clockrate1000 = 0;
    int i, ratecnt = 5;
    LARGE_INTEGER freq;
    int qpc_avail = 0;
    int mmx = 0; 

    rpt_available = no_rdtsc > 0 ? 0 : 1;
#ifdef X86_MSVC_ASSEMBLY
    __try
    {
	__asm 
	{
	    rdtsc
	}
    } __except(GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION) {
	rpt_available = 0;
	write_log ("CLOCKFREQ: RDTSC not supported\n");
    }
    __try
    {
	__asm 
	{
	    mov eax,1
	    cpuid
	    and edx,0x800000
	    mov mmx,edx
	}
	if (mmx)
	    cpu_mmx = 1;
    } __except(GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION) {
    }
#endif
#ifdef WIN64
    cpu_mmx = 1;
    rpt_available = 0;
#endif

    if (QueryPerformanceFrequency(&freq)) {
	qpc_avail = 1;
	qpfrate = freq.QuadPart;
	 /* limit to 10MHz */
	qpcdivisor = 0;
	while (qpfrate > 10000000) {
	    qpfrate >>= 1;
	    qpcdivisor++;
	    qpc_avail = -1;
	}
	write_log("CLOCKFREQ: QPF %.2fMHz (%.2fMHz, DIV=%d)\n", freq.QuadPart / 1000000.0,
	    qpfrate / 1000000.0, 1 << qpcdivisor);
	if (SystemInfo.dwNumberOfProcessors > 1 && no_rdtsc >= 0)
	    rpt_available = 0; /* RDTSC can be weird in SMP-systems */
    } else {
	write_log("CLOCKREQ: QPF not supported\n");
    }

    if (!rpt_available && !qpc_avail) {
	pre_gui_message ("No timing reference found\n(no RDTSC or QPF support detected)\nWinUAE will exit\n");
	return 0;
    }

    init_mmtimer();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    sleep_millis (50);
    dummythread_die = -1;

    if (qpc_avail || rpt_available)  {
	int qpfinit = 0;

	if (rpt_available) {
	    clockrateidle = win32_read_processor_time();
	    sleep_millis (500);
	    clockrateidle = (win32_read_processor_time() - clockrateidle) * 2;
	    dummythread_die = 0;
	    _beginthread(&dummythread, 0, 0);
	    sleep_millis (50);
	    clockrate = win32_read_processor_time();
	    sleep_millis (500);
	    clockrate = (win32_read_processor_time() - clockrate) * 2;
	    write_log("CLOCKFREQ: RDTSC %.2fMHz (busy) / %.2fMHz (idle)\n",
		clockrate / 1000000.0, clockrateidle / 1000000.0);
	    clkdiv = (double)clockrate / (double)clockrateidle;
	    clockrate >>= 6;
	    clockrate1000 = clockrate / 1000.0;
	}
	if (rpt_available && qpc_avail && qpfrate / 950.0 >= clockrate1000) {
	    write_log ("CLOCKFREQ: Using QPF (QPF ~>= RDTSC)\n");
	    qpfinit = 1;
	} else	if (((clkdiv <= 0.95 || clkdiv >= 1.05) && no_rdtsc == 0) || !rpt_available) {
	    if (rpt_available)
		write_log ("CLOCKFREQ: CPU throttling detected, using QPF instead of RDTSC\n");
	    qpfinit = 1;
	} else if (qpc_avail && freq.QuadPart >= 999000000) {
	    write_log ("CLOCKFREQ: Using QPF (QPF >= 1GHz)\n");
	    qpfinit = 1;
	}
	if (qpfinit) {
	    useqpc = qpc_avail;
	    rpt_available = 1;
	    clkdiv = 1.0;
	    clockrate = qpfrate;
	    clockrate1000 = clockrate / 1000.0;
	    if (dummythread_die < 0) {
		dummythread_die = 0;
		_beginthread(&dummythread, 0, 0);
	    }
	    if (!qpc_avail)
		write_log ("No working timing reference detected\n");
	}
	timermode = 0;
	if (mm_timerres) {
	    sleep_millis (50);
	    timebegin ();
	    sleep_millis (50);
	    ratea1 = 0;
	    write_log ("Testing MM-timer resolution:\n");
	    for (i = 0; i < ratecnt; i++) {
		rate1 = read_processor_time();
		sleep_millis (1);
		rate1 = read_processor_time() - rate1;
		write_log ("%1.2fms ", rate1 / clockrate1000);
		ratea1 += rate1;
	    }
	    write_log("\n");
	    timeend ();
	    sleep_millis (50);
	}
	timermode = 1;
	ratea2 = 0;
	write_log ("Testing Sleep() resolution:\n");
	for (i = 0; i < ratecnt; i++) {
	    rate2 = read_processor_time();
	    sleep_millis (1);
	    rate2 = read_processor_time() - rate2;
	    write_log ("%1.2fms ", rate2 / clockrate1000);
	    ratea2 += rate2;
	}
	write_log("\n");
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    dummythread_die = 1;

    sleep_resolution = 1;
    if (clkdiv >= 0.90 && clkdiv <= 1.10 && rpt_available) {
	limit = 2.5;
	if (mm_timerres && (ratea1 / ratecnt) < limit * clockrate1000) { /* MM-timer is ok */
	    timermode = 0;
	    sleep_resolution = (int)((1000 * ratea1) / (clockrate1000 * ratecnt));
	    timebegin ();
	    write_log ("Using MultiMedia timers (resolution < %.1fms) SR=%d\n", limit, sleep_resolution);
	} else if ((ratea2 / ratecnt) < limit * clockrate1000) { /* regular Sleep() is ok */
	    timermode = 1;
	    sleep_resolution = (int)((1000 * ratea2) / (clockrate1000 * ratecnt));
	    write_log ("Using Sleep() (resolution < %.1fms) SR=%d\n", limit, sleep_resolution);
	} else {
	    timermode = -1; /* both timers are bad, fall back to busy-wait */
	    write_log ("falling back to busy-loop waiting (timer resolution > %.1fms)\n", limit);
	}
    } else {
	timermode = -1;
	write_log ("forcing busy-loop wait mode\n");
    }
    if (sleep_resolution < 1000)
	sleep_resolution = 1000;
    syncbase = (unsigned long)clockrate;
    return 1;
}

static void setcursor(int oldx, int oldy)
{
    int x = (amigawin_rect.right - amigawin_rect.left) / 2;
    int y = (amigawin_rect.bottom - amigawin_rect.top) / 2;
    if (oldx == x && oldy == y)
	return;
    SetCursorPos (amigawin_rect.left + x, amigawin_rect.top + y);
}

void resumepaused(void)
{
    resume_sound ();
#ifdef AHI
    ahi_open_sound ();
#endif
#ifdef CD32
    akiko_exitgui ();
#endif
#ifdef CDTV
    cdtv_exitgui ();
#endif
}

void setpaused(void)
{
    pause_sound ();
#ifdef AHI
    ahi_close_sound ();
#endif
#ifdef CD32
    akiko_entergui ();
#endif
#ifdef CDTV
    cdtv_entergui ();
#endif
}

static WPARAM activateapp;

static void checkpause (void)
{
    if (activateapp)
	return;
    if (currprefs.win32_inactive_pause) {
	setpaused ();
	emulation_paused = 1;
    }
}

static int showcursor;

void setmouseactive (int active)
{
    int oldactive = mouseactive;
    char txt[400], txt2[200];

    if (showcursor) {
	ClipCursor(NULL);
	ReleaseCapture();
	ShowCursor (TRUE);
	showcursor = 0;
    }
    recapture = 0;
#if 0
    if (active > 0 && mousehack_allowed () && mousehack_alive ()) {
	if (isfullscreen () <= 0)
	    return;
    }
#endif
    inputdevice_unacquire ();

    mouseactive = active;
    strcpy (txt, "WinUAE");
    txt2[0] = 0;
    if (mouseactive > 0) {
	focus = 1;
	WIN32GUI_LoadUIString (currprefs.win32_middle_mouse ? IDS_WINUAETITLE_MMB : IDS_WINUAETITLE_NORMAL,
	    txt2, sizeof (txt2));
    }
    if (WINUAEBETA > 0)
	strcat (txt, BetaStr);
    if (txt2[0]) {
	strcat (txt, " - ");
	strcat (txt, txt2);
    }
    SetWindowText (hMainWnd, txt);
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
	inputdevice_acquire ();
    }
    if (!active)
	checkpause ();
}

#ifndef AVIOUTPUT
static int avioutput_video = 0;
#endif

void setpriority (struct threadpriorities *pri)
{
    int err;
    if (os_winnt)
	err = SetPriorityClass (GetCurrentProcess (), pri->classvalue);
    else
	err = SetThreadPriority (GetCurrentThread(), pri->value);
    if (!err)
	write_log ("priority set failed, %08.8X\n", GetLastError ());
}

static void winuae_active (HWND hWnd, int minimized)
{
    int ot;
    struct threadpriorities *pri;

    /* without this returning from hibernate-mode causes wrong timing
     */
    ot = timermode;
    timermode = 0;
    timebegin();
    sleep_millis (2);
    timermode = ot;
    if (timermode != 0)
	timeend();  

    focus = 1;
    pri = &priorities[currprefs.win32_inactive_priority];
#ifndef	_DEBUG
    if (!minimized)
	pri = &priorities[currprefs.win32_active_priority];
#endif
    setpriority (pri);

    if (!minimized) {
	if (!avioutput_video) {
	    clear_inhibit_frame (IHF_WINDOWHIDDEN);
	}
    }
    if (emulation_paused > 0)
	emulation_paused = -1;
    ShowWindow (hWnd, SW_RESTORE);
    if (sound_closed) {
	resumepaused();
	sound_closed = 0;
    }
    if (WIN32GFX_IsPicassoScreen ())
	WIN32GFX_EnablePicasso();
    getcapslock ();
    inputdevice_acquire ();
    wait_keyrelease ();
    inputdevice_acquire ();
    if (isfullscreen() > 0)
	setmouseactive (1);
    manual_palette_refresh_needed = 1;

}

static void winuae_inactive (HWND hWnd, int minimized)
{
    struct threadpriorities *pri;

    if (minimized)
	exit_gui (0);
    focus = 0;
    wait_keyrelease ();
    setmouseactive (0);
    pri = &priorities[currprefs.win32_inactive_priority];
    if (!quit_program) {
	if (minimized) {
	    inputdevice_unacquire ();
	    pri = &priorities[currprefs.win32_iconified_priority];
	    if (currprefs.win32_iconified_nosound) {
		setpaused();
		sound_closed = 1;
	    }
	    if (!avioutput_video) {
		set_inhibit_frame (IHF_WINDOWHIDDEN);
	    }
	    if (currprefs.win32_iconified_pause) {
		if (!sound_closed)
		    setpaused();
		emulation_paused = 1;
		sound_closed = 1;
	    }
	} else {
	    if (currprefs.win32_inactive_nosound) {
		setpaused();
		sound_closed = 1;
	    }
	}
    }
    setpriority (pri);
#ifdef FILESYS
    filesys_flush_cache ();
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

extern void setamigamouse(int,int);
void setmouseactivexy(int x, int y, int dir)
{
    int diff = 8;
    if (isfullscreen() > 0)
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
    disablecapture();
    SetCursorPos(x, y);
    if (dir)
	recapture = 1;
}

static void handleXbutton (WPARAM wParam, int updown)
{
    int b = GET_XBUTTON_WPARAM (wParam);
    int num = (b & XBUTTON1) ? 3 : (b & XBUTTON2) ? 4 : -1;
    if (num >= 0)
	setmousebuttonstate (dinput_winmouse(), num, updown);
}

#define MSGDEBUG 0

static LRESULT CALLBACK AmigaWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hDC;
    int mx, my, v;
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
    case WM_SETCURSOR:
	return TRUE;
    case WM_SIZE:
    {
	if (recursive)
	    return 0;
	recursive++;
#if MSGDEBUG
	write_log ("WM_SIZE %x %d %d\n", hWnd, wParam, minimized);
#endif
	if (isfullscreen () > 0) {
	    v = minimized;
	    switch (wParam)
	    {
		case SIZE_MAXIMIZED:
		case SIZE_RESTORED:
		v = FALSE;
		break;
		default:
		v = TRUE;
		break;
	    }
	    exit_gui (0);
	    if (v != minimized) {
		minimized = v;
		if (v) {
		    winuae_inactive (hWnd, wParam == SIZE_MINIMIZED);
		} else {
		    pausemode (0);
		    winuae_active (hWnd, minimized);
		}
	    }
	}
	recursive--;
	return 0;
    }
    break;    
    case WM_ACTIVATE:
	if (recursive)
	    return 0;
	recursive++;
#if MSGDEBUG
	write_log ("WM_ACTIVATE %x %d %d %d\n", hWnd, HIWORD (wParam), LOWORD (wParam), minimized);
#endif
	if (isfullscreen () <= 0) {
	    minimized = HIWORD (wParam);
	    if (LOWORD (wParam) != WA_INACTIVE) {
		winuae_active (hWnd, minimized);
	    } else {
		winuae_inactive (hWnd, minimized);
	    }
	} else {
	    if (LOWORD (wParam) == WA_INACTIVE) {
		minimized = HIWORD (wParam);
		winuae_inactive (hWnd, minimized);
	    } else {
		if (!minimized)
		    winuae_active (hWnd, minimized);
		if (is3dmode() && normal_display_change_starting == 0)
		    normal_display_change_starting = 1;
	    }
	}
	recursive--;
        return 0;

    case WM_ACTIVATEAPP:
    {
	if (recursive)
	    return 0;
	recursive++;
#if MSGDEBUG
	write_log ("WM_ACTIVATEAPP %x %d %d\n", hWnd, wParam, minimized);
#endif
	activateapp = wParam;
	if (!wParam) {
	    setmouseactive (0);
	    normal_display_change_starting = 0;
	} else {
	    if (minimized)
		minimized = 0;
	    winuae_active (hWnd, minimized);
	    if (is3dmode () && normal_display_change_starting == 1) {
		WIN32GFX_DisplayChangeRequested ();
		normal_display_change_starting = -1;
	    }
	}
	manual_palette_refresh_needed = 1;
	recursive--;
        return 0;
    }

    case WM_PALETTECHANGED:
	if ((HWND)wParam != hWnd)
	    manual_palette_refresh_needed = 1;
    break;

    case WM_KEYDOWN:
	if (dinput_wmkey ((uae_u32)lParam))
	    gui_display (-1);
    return 0;

    case WM_LBUTTONUP:
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse(), 0, 0);
    return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
	if (!mouseactive && isfullscreen() <= 0 && !gui_active) {
	    setmouseactive (1);
	}
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse(), 0, 1);
    return 0;
    case WM_RBUTTONUP:
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse(), 1, 0);
    return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
	if (dinput_winmouse () >= 0)
	    setmousebuttonstate (dinput_winmouse(), 1, 1);
    return 0;
    case WM_MBUTTONUP:
	if (!currprefs.win32_middle_mouse) {
	    if (dinput_winmouse () >= 0)
		setmousebuttonstate (dinput_winmouse(), 2, 0);
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
		setmousebuttonstate (dinput_winmouse(), 2, 1);
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
	if (dinput_winmouse () >= 0)
	    setmousestate (dinput_winmouse(), 2, ((short)HIWORD(wParam)), 0);
    return 0;
    case WM_MOUSEHWHEEL:
	if (dinput_winmouse () >= 0)
	    setmousestate (dinput_winmouse(), 3, ((short)HIWORD(wParam)), 0);
    return 0;

    case WM_PAINT:
    {
	static int recursive = 0;
	if (recursive == 0) {
	    recursive++;
	    notice_screen_contents_lost ();
	    hDC = BeginPaint (hWnd, &ps);
	    /* Check to see if this WM_PAINT is coming while we've got the GUI visible */
	    if (manual_painting_needed)
		updatedisplayarea ();
	    EndPaint (hWnd, &ps);
	    if (manual_palette_refresh_needed) {
		WIN32GFX_SetPalette();
#ifdef PICASSO96
		DX_SetPalette (0, 256);
#endif
	    }
	    manual_palette_refresh_needed = 0;
	    recursive--;
	}
    }
    break;

    case WM_DROPFILES:
	dragdrop (hWnd, (HDROP)wParam, &changed_prefs, -1);
    break;
     
    case WM_TIMER:
#ifdef PARALLEL_PORT
	finishjob ();
#endif
    break;

    case WM_CREATE:
	DragAcceptFiles (hWnd, TRUE);
    break;

    case WM_CLOSE:
	uae_quit ();
    return 0;

    case WM_WINDOWPOSCHANGED:
	GetWindowRect (hWnd, &amigawin_rect);
	if (isfullscreen() == 0) {
	    changed_prefs.gfx_size_win.x = amigawin_rect.left;
	    changed_prefs.gfx_size_win.y = amigawin_rect.top;
	}
    break;

    case WM_MOUSEMOVE:
    {
	mx = (signed short) LOWORD (lParam);
	my = (signed short) HIWORD (lParam);
	if (recapture && isfullscreen() <= 0) {
	    setmouseactive(1);
	    setamigamouse(mx, my);
	    return 0;
	}
	if (normal_display_change_starting)
	    return 0;
	if (dinput_winmouse () >= 0) {
	    if (dinput_winmousemode ()) {
		/* absolete + mousehack */
		setmousestate (dinput_winmouse (), 0, mx, 1);
		setmousestate (dinput_winmouse (), 1, my, 1);
		return 0;
	    } else {
		/* relative */
		int mxx = (amigawin_rect.right - amigawin_rect.left) / 2;
		int myy = (amigawin_rect.bottom - amigawin_rect.top) / 2;
		mx = mx - mxx;
		my = my - myy;
		setmousestate (dinput_winmouse (), 0, mx, 0);
		setmousestate (dinput_winmouse (), 1, my, 0);
	    }
	} else if ((!mouseactive && isfullscreen() <= 0)) {
	    setmousestate (0, 0, mx, 1);
	    setmousestate (0, 1, my, 1);
	}
	if (showcursor || mouseactive)
	    setcursor (LOWORD (lParam), HIWORD (lParam));
    }
    return 0;

    case WM_MOVING:
    case WM_MOVE:
	WIN32GFX_WindowMove();
    return TRUE;

#if 0
    case WM_GETMINMAXINFO:
    {
	LPMINMAXINFO lpmmi;
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	lpmmi = (LPMINMAXINFO)lParam;
	rect.right = 320;
	rect.bottom = 256;
	//AdjustWindowRectEx(&rect,WSTYLE,0,0);
	lpmmi->ptMinTrackSize.x = rect.right-rect.left;
	lpmmi->ptMinTrackSize.y = rect.bottom-rect.top;
    }
    return 0;
#endif

#ifdef FILESYS
    case WM_DEVICECHANGE:
    {
	extern void win32_spti_media_change (char driveletter, int insert);
	extern void win32_ioctl_media_change (char driveletter, int insert);
	extern void win32_aspi_media_change (char driveletter, int insert);
	DEV_BROADCAST_HDR *pBHdr = (DEV_BROADCAST_HDR *)lParam;
	if( pBHdr && ( pBHdr->dbch_devicetype == DBT_DEVTYP_VOLUME ) ) {
	    DEV_BROADCAST_VOLUME *pBVol = (DEV_BROADCAST_VOLUME *)lParam;
	    if( pBVol->dbcv_flags & DBTF_MEDIA ) {
		if( pBVol->dbcv_unitmask ) {
		    int inserted, i;
		    char drive;
		    for (i = 0; i <= 'Z'-'A'; i++) {
			if (pBVol->dbcv_unitmask & (1 << i)) {
			    drive = 'A' + i;
			    inserted = -1;
			    if (wParam == DBT_DEVICEARRIVAL)
				inserted = 1;
			    else if (wParam == DBT_DEVICEREMOVECOMPLETE)
				inserted = 0;
#ifdef WINDDK
			    win32_spti_media_change (drive, inserted);
			    win32_ioctl_media_change (drive, inserted);
#endif
			    win32_aspi_media_change (drive, inserted);
			}
		    }
		}
	    }
	}
    }
#endif
    return TRUE;

    case WM_SYSCOMMAND:
	if (!manual_painting_needed && focus && currprefs.win32_powersavedisabled) {
	    switch (wParam) // Check System Calls
	    {
		case SC_SCREENSAVE: // Screensaver Trying To Start?
		case SC_MONITORPOWER: // Monitor Trying To Enter Powersave?
		return 0; // Prevent From Happening
	    }
	}
    break;

    case WM_SYSKEYDOWN:
	if(currprefs.win32_ctrl_F11_is_quit && wParam == VK_F4)
	    return 0;
    break;

    case 0xff: // WM_INPUT:
    handle_rawinput (lParam);
    break;
    
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
    
    case WM_USER + 1: /* Systray icon */
	 switch (lParam)
	 {
	    case WM_LBUTTONDOWN:
	    SetForegroundWindow (hWnd);
	    break;
	    case WM_LBUTTONDBLCLK:
	    gui_display (-1);
	    break;
	    case WM_RBUTTONDOWN:
	    if (!gui_active)
		systraymenu (hWnd);
	    else
		SetForegroundWindow (hWnd);
	    break;
	 }
	break;

     case WM_COMMAND:
	switch (wParam & 0xffff)
	{
	    case ID_ST_CONFIGURATION:
		gui_display (-1);
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

     default:
	 if (TaskbarRestartOk && message == TaskbarRestart)
	     systray (hWnd, 0);
    break;
    }

    return DefWindowProc (hWnd, message, wParam, lParam);
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
     case WM_MOUSEMOVE:
     case WM_MOUSEWHEEL:
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
     case WM_GETMINMAXINFO:
     case WM_CREATE:
     case WM_DESTROY:
     case WM_CLOSE:
     case WM_HELP:
     case WM_DEVICECHANGE:
     case 0xff: // WM_INPUT
     case WM_USER + 1:
     case WM_COMMAND:
     case WM_NOTIFY:
	return AmigaWindowProc (hWnd, message, wParam, lParam);

     case WM_DISPLAYCHANGE:
	if (isfullscreen() <= 0 && !currprefs.gfx_filter && (wParam + 7) / 8 != DirectDraw_GetBytesPerPixel() )
	    WIN32GFX_DisplayChangeRequested();
	break;

     case WM_ENTERSIZEMOVE:
	in_sizemove++;
	break;

     case WM_EXITSIZEMOVE:
	in_sizemove--;
	/* fall through */

     case WM_WINDOWPOSCHANGED:
	WIN32GFX_WindowMove();
	if (hAmigaWnd && GetWindowRect(hAmigaWnd, &amigawin_rect)) {
	    if (in_sizemove > 0)
		break;

	    if (isfullscreen() == 0 && hAmigaWnd) {
		static int store_xy;
		RECT rc2;
		if (GetWindowRect(hMainWnd, &rc2)) {
		    DWORD left = rc2.left - win_x_diff;
	    	    DWORD top = rc2.top - win_y_diff;
		    if (amigawin_rect.left & 3) {
			MoveWindow (hMainWnd, rc2.left + 4 - amigawin_rect.left % 4, rc2.top,
				    rc2.right - rc2.left, rc2.bottom - rc2.top, TRUE);

		    }
		    if (hWinUAEKey && store_xy++) {
			RegSetValueEx(hWinUAEKey, "MainPosX", 0, REG_DWORD, (LPBYTE)&left, sizeof(LONG));
			RegSetValueEx(hWinUAEKey, "MainPosY", 0, REG_DWORD, (LPBYTE)&top, sizeof(LONG));
		    }
		    changed_prefs.gfx_size_win.x = left;
		    changed_prefs.gfx_size_win.y = top;
		}
		return 0;
	    }
	}
	break;

     case WM_PAINT:
	hDC = BeginPaint (hWnd, &ps);
	GetClientRect (hWnd, &rc);
	DrawEdge (hDC, &rc, EDGE_SUNKEN, BF_RECT);
	EndPaint (hWnd, &ps);
	break;

     case WM_NCLBUTTONDBLCLK:
	if (wParam == HTCAPTION) {
	    WIN32GFX_ToggleFullScreen();
	    return 0;
	}
	break;


    default:
	if (TaskbarRestartOk && message == TaskbarRestart)
	    return AmigaWindowProc (hWnd, message, wParam, lParam);
	break;

    }

    return DefWindowProc (hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK HiddenWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc (hWnd, message, wParam, lParam);
}

void handle_events (void)
{
    MSG msg;
    int was_paused = 0;

    while (emulation_paused > 0 || pause_emulation) {
	if ((emulation_paused > 0 || pause_emulation) && was_paused == 0) {
	    setpaused();
	    was_paused = 1;
	    manual_painting_needed++;
	    gui_fps (0, 0);
	}
	if (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
	    TranslateMessage (&msg);
	    DispatchMessage (&msg);
	}
	sleep_millis (50);
	inputdevicefunc_keyboard.read();
	inputdevicefunc_mouse.read();
	inputdevicefunc_joystick.read();
	inputdevice_handle_inputcode ();
	check_prefs_changed_gfx ();
	while (checkIPC(&currprefs));
	if (quit_program)
	    break;
    }
    while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
	TranslateMessage (&msg);
	DispatchMessage (&msg);
    }
    while (checkIPC(&currprefs));
    if (was_paused) {
	resumepaused();
	emulation_paused = 0;
	sound_closed = 0;
	manual_painting_needed--;
    }
}

/* We're not a console-app anymore! */
void setup_brkhandler (void)
{
}
void remove_brkhandler (void)
{
}

int WIN32_RegisterClasses( void )
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

int WIN32_CleanupLibraries( void )
{
    if (hRichEdit)
	FreeLibrary(hRichEdit);
    
    if (hHtmlHelp)
	FreeLibrary(hHtmlHelp);

    if (hUIDLL)
	FreeLibrary(hUIDLL);

    return 1;
}

/* HtmlHelp Initialization - optional component */
int WIN32_InitHtmlHelp( void )
{
    char *chm = "WinUAE.chm";
    int result = 0;
    sprintf(help_file, "%s%s", start_path_data, chm);
    if (!zfile_exists (help_file))
	sprintf(help_file, "%s%s", start_path_exe, chm);
    if (zfile_exists (help_file)) {
	if (hHtmlHelp = LoadLibrary("HHCTRL.OCX")) {
	    pHtmlHelp = (HWND(WINAPI *)(HWND, LPCSTR, UINT, LPDWORD))GetProcAddress(hHtmlHelp, "HtmlHelpA");
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

HMODULE language_load(WORD language)
{
    HMODULE result = NULL;
    char dllbuf[MAX_DPATH];
    char *dllname;

    if (language <= 0) {
        /* new user-specific Windows ME/2K/XP method to get UI language */
	pGetUserDefaultUILanguage = (PGETUSERDEFAULTUILANGUAGE)GetProcAddress(
	    GetModuleHandle("kernel32.dll"), "GetUserDefaultUILanguage");
	language = GetUserDefaultLangID();
	if (pGetUserDefaultUILanguage)
	    language = pGetUserDefaultUILanguage();
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
	    sprintf (dllbuf, "%sguidll.dll", start_path_exe);
	else
	    sprintf (dllbuf, "%sWinUAE_%s.dll", start_path_exe, dllname);
	result = WIN32_LoadLibrary (dllbuf);
	if (result)  {
	    dwFileVersionInfoSize = GetFileVersionInfoSize(dllbuf, &dwVersionHandle);
	    if (dwFileVersionInfoSize) {
		if (lpFileVersionData = calloc(1, dwFileVersionInfoSize)) {
		    if (GetFileVersionInfo(dllbuf, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData)) {
			VS_FIXEDFILEINFO *vsFileInfo = NULL;
			UINT uLen;
			fail = 0;
			if (VerQueryValue(lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen)) {
			    if (vsFileInfo &&
				HIWORD(vsFileInfo->dwProductVersionMS) == UAEMAJOR
				&& LOWORD(vsFileInfo->dwProductVersionMS) == UAEMINOR
				&& HIWORD(vsFileInfo->dwProductVersionLS) == UAESUBREV) {
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
		    free(lpFileVersionData);
		}
	    }
	}
	if (fail) {
	    DWORD err = GetLastError();
	    if (err != ERROR_MOD_NOT_FOUND && err != ERROR_DLL_NOT_FOUND)
		write_log ("Translation DLL '%s' failed to load, error %d\n", dllbuf, GetLastError ());
	}
	if (result && !success) {
	    FreeLibrary(result);
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
    WORD langid = -1;
    if (hWinUAEKey) {
	DWORD regkeytype;
	DWORD regkeysize = sizeof(langid);
        RegQueryValueEx (hWinUAEKey, "Language", 0, &regkeytype, (LPBYTE)&langid, &regkeysize);
    }
    hUIDLL = language_load(langid);
    pritransla ();
}

 /* try to load COMDLG32 and DDRAW, initialize csDraw */
static int WIN32_InitLibraries( void )
{
    int result = 1;
    /* Determine our processor speed and capabilities */
    if (!figure_processor_speed())
	return 0;
    
    /* Make sure we do an InitCommonControls() to get some advanced controls */
    InitCommonControls();
    
    hRichEdit = LoadLibrary ("RICHED32.DLL");
    
    return result;
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

void logging_open(int bootlog, int append)
{
    char debugfilename[MAX_DPATH];

    debugfilename[0] = 0;
#ifndef	SINGLEFILE
    if (currprefs.win32_logfile)
	sprintf (debugfilename, "%swinuaelog.txt", start_path_data);
    if (bootlog)
	sprintf (debugfilename, "%swinuaebootlog.txt", start_path_data);
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
	if (debugfile)
	    log_close (debugfile);
	debugfile = 0;
    }
    logging_open(first ? 0 : 1, 0);
    first++;
    fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle("kernel32"),"IsWow64Process");
    if (fnIsWow64Process)
	fnIsWow64Process(GetCurrentProcess(), &wow64);
    write_log ("%s (%s %d.%d %s%s[%d])", VersionStr, os_winnt ? "NT" : "W9X/ME",
	osVersion.dwMajorVersion, osVersion.dwMinorVersion, osVersion.szCSDVersion,
	strlen(osVersion.szCSDVersion) > 0 ? " " : "", os_winnt_admin);
    write_log (" %d-bit %X.%X %d", wow64 ? 64 : 32,
	SystemInfo.wProcessorLevel, SystemInfo.wProcessorRevision,
	SystemInfo.dwNumberOfProcessors);
    write_log ("\n(c) 1995-2001 Bernd Schmidt   - Core UAE concept and implementation."
	       "\n(c) 1998-2007 Toni Wilen      - Win32 port, core code updates."
	       "\n(c) 1996-2001 Brian King      - Win32 port, Picasso96 RTG, and GUI."
	       "\n(c) 1996-1999 Mathias Ortmann - Win32 port and bsdsocket support."
	       "\n(c) 2000-2001 Bernd Meyer     - JIT engine."
	       "\n(c) 2000-2005 Bernd Roesch    - MIDI input, many fixes."
	       "\nPress F12 to show the Settings Dialog (GUI), Alt-F4 to quit."
	       "\nEnd+F1 changes floppy 0, End+F2 changes floppy 1, etc."
	       "\n");
    write_log ("EXE: '%s', DATA: '%s'\n", start_path_exe, start_path_data);
}

void logging_cleanup( void )
{
    if (debugfile)
	fclose (debugfile);
    debugfile = 0;
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
    if (!h)
	return NULL;
    GetModuleFileName(h, name, MAX_DPATH);
    pfnGetKey = (PFN_GetKey)GetProcAddress(h, "GetKey");
    if (pfnGetKey) {
	size = pfnGetKey(NULL, 0);
	*sizep = size;
	if (size > 0) {
	    keybuf = xmalloc (size);
	    if (pfnGetKey(keybuf, size) != size) {
		xfree (keybuf);
		keybuf = NULL;
	    }
	}
    }
    FreeLibrary (h);
    return keybuf;
}


extern char *get_aspi_path(int);

static get_aspi(int old)
{
    if ((old == UAESCSI_SPTI || old == UAESCSI_SPTISCAN) && os_winnt && os_winnt_admin)
	return old;
    if (old == UAESCSI_NEROASPI && get_aspi_path(1))
	return old;
    if (old == UAESCSI_FROGASPI && get_aspi_path(2))
	return old;
    if (old == UAESCSI_ADAPTECASPI && get_aspi_path(0))
	return old;
    if (os_winnt && os_winnt_admin)
	return UAESCSI_SPTI;
    else if (get_aspi_path(1))
	return UAESCSI_NEROASPI;
    else if (get_aspi_path(2))
	return UAESCSI_FROGASPI;
    else if (get_aspi_path(0))
	return UAESCSI_ADAPTECASPI;
    else if (os_winnt)
	return UAESCSI_SPTI;
    return UAESCSI_ADAPTECASPI;
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
	p->win32_no_overlay = os_vista ? 1 : 0;
	p->win32_ctrl_F11_is_quit = 0;
	p->win32_soundcard = 0;
	p->win32_active_priority = 1;
	p->win32_inactive_priority = 2;
	p->win32_iconified_priority = 3;
	p->win32_notaskbarbutton = 0;
	p->win32_alwaysontop = 0;
	p->win32_specialkey = 0xcf; // DIK_END
	p->win32_automount_drives = 0;
	p->win32_automount_netdrives = 0;
	p->win32_kbledmode = 0;
	p->win32_uaescsimode = get_aspi(p->win32_uaescsimode);
	p->win32_borderless = 0;
	p->win32_powersavedisabled = 1;
	p->win32_outsidemouse = 0;
    }
    if (type == 1 || type == 0) {
	p->win32_uaescsimode = get_aspi(p->win32_uaescsimode);
	p->win32_midioutdev = -2;
	p->win32_midiindev = 0;
    }
}

static const char *scsimode[] = { "none", "SPTI", "SPTI+SCSISCAN", "AdaptecASPI", "NeroASPI", "FrogASPI", 0 };

void target_save_options (struct zfile *f, struct uae_prefs *p)
{
    cfgfile_target_write (f, "middle_mouse=%s\n", p->win32_middle_mouse ? "true" : "false");
    cfgfile_target_write (f, "magic_mouse=%s\n", p->win32_outsidemouse ? "true" : "false");
    cfgfile_target_write (f, "logfile=%s\n", p->win32_logfile ? "true" : "false");
    cfgfile_target_write (f, "map_drives=%s\n", p->win32_automount_drives ? "true" : "false");
    cfgfile_target_write (f, "map_net_drives=%s\n", p->win32_automount_netdrives ? "true" : "false");
    serdevtoname(p->sername);
    cfgfile_target_write (f, "serial_port=%s\n", p->sername[0] ? p->sername : "none" );
    sernametodev(p->sername);
    cfgfile_target_write (f, "parallel_port=%s\n", p->prtname[0] ? p->prtname : "none" );

    cfgfile_target_write (f, "active_priority=%d\n", priorities[p->win32_active_priority].value);
    cfgfile_target_write (f, "inactive_priority=%d\n", priorities[p->win32_inactive_priority].value);
    cfgfile_target_write (f, "inactive_nosound=%s\n", p->win32_inactive_nosound ? "true" : "false");
    cfgfile_target_write (f, "inactive_pause=%s\n", p->win32_inactive_pause ? "true" : "false");
    cfgfile_target_write (f, "iconified_priority=%d\n", priorities[p->win32_iconified_priority].value);
    cfgfile_target_write (f, "iconified_nosound=%s\n", p->win32_iconified_nosound ? "true" : "false");
    cfgfile_target_write (f, "iconified_pause=%s\n", p->win32_iconified_pause ? "true" : "false");

    cfgfile_target_write (f, "ctrl_f11_is_quit=%s\n", p->win32_ctrl_F11_is_quit ? "true" : "false");
    cfgfile_target_write (f, "midiout_device=%d\n", p->win32_midioutdev );
    cfgfile_target_write (f, "midiin_device=%d\n", p->win32_midiindev );
    cfgfile_target_write (f, "no_overlay=%s\n", p->win32_no_overlay ? "true" : "false" );
    cfgfile_target_write (f, "borderless=%s\n", p->win32_borderless ? "true" : "false" );
    cfgfile_target_write (f, "uaescsimode=%s\n", scsimode[p->win32_uaescsimode]);
    cfgfile_target_write (f, "soundcard=%d\n", p->win32_soundcard );
    cfgfile_target_write (f, "cpu_idle=%d\n", p->cpu_idle);
    cfgfile_target_write (f, "notaskbarbutton=%s\n", p->win32_notaskbarbutton ? "true" : "false");
    cfgfile_target_write (f, "always_on_top=%s\n", p->win32_alwaysontop ? "true" : "false");
    cfgfile_target_write (f, "no_recyclebin=%s\n", p->win32_norecyclebin ? "true" : "false");
    cfgfile_target_write (f, "specialkey=0x%x\n", p->win32_specialkey);
    cfgfile_target_write (f, "kbledmode=%d\n", p->win32_kbledmode);
    cfgfile_target_write (f, "powersavedisabled=%s\n", p->win32_powersavedisabled ? "true" : "false");

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
    int i, v;
    int result = (cfgfile_yesno (option, value, "middle_mouse", &p->win32_middle_mouse)
	    || cfgfile_yesno (option, value, "map_drives", &p->win32_automount_drives)
	    || cfgfile_yesno (option, value, "map_net_drives", &p->win32_automount_netdrives)
	    || cfgfile_yesno (option, value, "logfile", &p->win32_logfile)
	    || cfgfile_yesno (option, value, "networking", &p->socket_emu)
	    || cfgfile_yesno (option, value, "no_overlay", &p->win32_no_overlay)
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


    if (cfgfile_yesno (option, value, "aspi", &v)) {
	p->win32_uaescsimode = 0;
	if (v)
	    p->win32_uaescsimode = get_aspi(0);
	if (p->win32_uaescsimode < UAESCSI_ASPI_FIRST)
	    p->win32_uaescsimode = UAESCSI_ADAPTECASPI;
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

void fetch_saveimagepath (char *out, int size, int dir)
{
    fetch_path ("SaveimagePath", out, size);
    if (dir) {
	out[strlen (out) - 1] = 0;
	CreateDirectory (out, NULL);
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
    if (hWinUAEKey)
	RegQueryValueEx (hWinUAEKey, name, 0, NULL, out, &size);
    if (out[0] == '\\' && (strlen(out) >= 2 && out[1] != '\\')) { /* relative? */
	strcpy (out, start_path_data);
	if (hWinUAEKey) {
	    size2 -= strlen (out);
	    RegQueryValueEx (hWinUAEKey, name, 0, NULL, out + strlen (out) - 1, &size2);
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
	if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY)) {
	    if (!strcmp (start_path_data, start_path_exe))
		strcpy (tmp, ".\\");
	    else
		strcpy (tmp, start_path_data);
	}
	if (af_path_2005) {
	    strcpy (tmp, start_path_af);
	    strcat (tmp, "System\\rom");
	} else if (af_path_old) {
	    strcpy (tmp, start_path_exe);
	    strcat (tmp, "..\\shared\\rom");
	}
    }
    fixtrailing (tmp);

    if (hWinUAEKey)
	RegSetValueEx (hWinUAEKey, name, 0, REG_SZ, (CONST BYTE *)tmp, strlen (tmp) + 1);
}

static void initpath (char *name, char *path)
{
    if (!hWinUAEKey)
	return;
    if (RegQueryValueEx(hWinUAEKey, name, 0, NULL, NULL, NULL) == ERROR_SUCCESS)
	return;
    set_path (name, NULL);
}

extern int scan_roms (char*);
void read_rom_list (void)
{
    char tmp2[1000];
    DWORD size2;
    int idx, idx2;
    HKEY fkey;
    char tmp[1000];
    DWORD size, disp;

    romlist_clear ();
    if (!hWinUAEKey)
	return;
    RegCreateKeyEx(hWinUAEKey , "DetectedROMs", 0, NULL, REG_OPTION_NON_VOLATILE,
	KEY_READ | KEY_WRITE, NULL, &fkey, &disp);
    if (fkey == NULL)
	return;
    if (disp == REG_CREATED_NEW_KEY || forceroms) {
	load_keyring (NULL, NULL);
	scan_roms (NULL);
    }
    forceroms = 0;
    idx = 0;
    for (;;) {
	int err;
	size = sizeof (tmp);
	size2 = sizeof (tmp2);
	err = RegEnumValue(fkey, idx, tmp, &size, NULL, NULL, tmp2, &size2);
	if (err != ERROR_SUCCESS)
	    break;
	if (strlen (tmp) == 5) {
	    idx2 = atol (tmp + 3);
	    if (idx2 >= 0 && strlen (tmp2) > 0) {
		struct romdata *rd = getromdatabyid (idx2);
		if (rd)
		    romlist_add (tmp2, rd);
	    }
	}
	idx++;
    }
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
    DWORD dwDisplayInfoSize = sizeof(colortype);
    DWORD size;
    DWORD disposition;
    char path[MAX_DPATH] = "";
    char version[100];
    HKEY hWinUAEKeyLocal = NULL;
    HKEY fkey;
    HKEY rkey;
    char rpath1[MAX_DPATH], rpath2[MAX_DPATH], rpath3[MAX_DPATH];

    rpath1[0] = rpath2[0] = rpath3[0] = 0;
    rkey = HKEY_CLASSES_ROOT;
    if (os_winnt) {
	if (os_winnt_admin)
	    rkey = HKEY_LOCAL_MACHINE;
	else
	    rkey = HKEY_CURRENT_USER;
	strcpy(rpath1, "Software\\Classes\\");
	strcpy(rpath2, rpath1);
	strcpy(rpath3, rpath1);
    }
    strcat(rpath1, ".uae");
    strcat(rpath2, "WinUAE\\shell\\Edit\\command");
    strcat(rpath3, "WinUAE\\shell\\Open\\command");

    /* Create/Open the hWinUAEKey which points to our config-info */
    if (RegCreateKeyEx(rkey, rpath1, 0, "", REG_OPTION_NON_VOLATILE,
	KEY_WRITE | KEY_READ, NULL, &hWinUAEKey, &disposition) == ERROR_SUCCESS)
    {
	// Regardless of opening the existing key, or creating a new key, we will write the .uae filename-extension
	// commands in.  This way, we're always up to date.

	/* Set our (default) sub-key to point to the "WinUAE" key, which we then create */
	RegSetValueEx(hWinUAEKey, "", 0, REG_SZ, (CONST BYTE *)"WinUAE", strlen( "WinUAE" ) + 1);

	if((RegCreateKeyEx(rkey, rpath2, 0, "", REG_OPTION_NON_VOLATILE,
			      KEY_WRITE | KEY_READ, NULL, &hWinUAEKeyLocal, &disposition) == ERROR_SUCCESS))
	{
	    /* Set our (default) sub-key to BE the "WinUAE" command for editing a configuration */
	    sprintf(path, "\"%sWinUAE.exe\" -f \"%%1\" -s use_gui=yes", start_path_exe);
	    RegSetValueEx(hWinUAEKeyLocal, "", 0, REG_SZ, (CONST BYTE *)path, strlen(path) + 1);
	    RegCloseKey(hWinUAEKeyLocal);
	}

	if((RegCreateKeyEx(rkey, rpath3, 0, "", REG_OPTION_NON_VOLATILE,
			      KEY_WRITE | KEY_READ, NULL, &hWinUAEKeyLocal, &disposition) == ERROR_SUCCESS))
	{
	    /* Set our (default) sub-key to BE the "WinUAE" command for launching a configuration */
	    sprintf(path, "\"%sWinUAE.exe\" -f \"%%1\"", start_path_exe);
	    RegSetValueEx(hWinUAEKeyLocal, "", 0, REG_SZ, (CONST BYTE *)path, strlen( path ) + 1);
	    RegCloseKey(hWinUAEKeyLocal);
	}
	RegCloseKey(hWinUAEKey);
    }
    hWinUAEKey = NULL;

    /* Create/Open the hWinUAEKey which points our config-info */
    if (RegCreateKeyEx(HKEY_CURRENT_USER, "Software\\Arabuusimiehet\\WinUAE", 0, "", REG_OPTION_NON_VOLATILE,
			  KEY_WRITE | KEY_READ, NULL, &hWinUAEKey, &disposition) == ERROR_SUCCESS)
    {
	initpath ("FloppyPath", start_path_data);
	initpath ("KickstartPath", start_path_data);
	initpath ("hdfPath", start_path_data);
	initpath ("ConfigurationPath", start_path_data);
	initpath ("ScreenshotPath", start_path_data);
	initpath ("StatefilePath", start_path_data);
	initpath ("SaveimagePath", start_path_data);
	initpath ("VideoPath", start_path_data);
	initpath ("InputPath", start_path_data);
	if (disposition == REG_CREATED_NEW_KEY) {
	    /* Create and initialize all our sub-keys to the default values */
	    RegSetValueEx(hWinUAEKey, "MainPosX", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof(colortype));
	    RegSetValueEx(hWinUAEKey, "MainPosY", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof(colortype));
	    RegSetValueEx(hWinUAEKey, "GUIPosX", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof(colortype));
	    RegSetValueEx(hWinUAEKey, "GUIPosY", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof(colortype));
	}
	size = sizeof (version);
	dwType = REG_SZ;
	if (RegQueryValueEx (hWinUAEKey, "Version", 0, &dwType, (LPBYTE)&version, &size) == ERROR_SUCCESS) {
	    if (checkversion (version))
		RegSetValueEx (hWinUAEKey, "Version", 0, REG_SZ, (CONST BYTE *)VersionStr, strlen (VersionStr) + 1);
	} else {
	    RegSetValueEx (hWinUAEKey, "Version", 0, REG_SZ, (CONST BYTE *)VersionStr, strlen (VersionStr) + 1);
	}
	size = sizeof (version);
	dwType = REG_SZ;
	if (RegQueryValueEx (hWinUAEKey, "ROMCheckVersion", 0, &dwType, (LPBYTE)&version, &size) == ERROR_SUCCESS) {
	    if (checkversion (version)) {
		if (RegSetValueEx (hWinUAEKey, "ROMCheckVersion", 0, REG_SZ, (CONST BYTE *)VersionStr, strlen (VersionStr) + 1) == ERROR_SUCCESS)
		    forceroms = 1;
	    }
	} else {
	    if (RegSetValueEx (hWinUAEKey, "ROMCheckVersion", 0, REG_SZ, (CONST BYTE *)VersionStr, strlen (VersionStr) + 1) == ERROR_SUCCESS)
		forceroms = 1;
	}
	
	size = sizeof (quickstart);
	dwType = REG_DWORD;
	RegQueryValueEx(hWinUAEKey, "QuickStartMode", 0, &dwType, (LPBYTE)&quickstart, &size);
    }
    reopen_console();
    fetch_path ("ConfigurationPath", path, sizeof (path));
    path[strlen (path) - 1] = 0;
    CreateDirectory (path, NULL);
    strcat (path, "\\Host");
    CreateDirectory (path, NULL);
    fetch_path ("ConfigurationPath", path, sizeof (path));
    strcat (path, "Hardware");
    CreateDirectory (path, NULL);
    fetch_path ("StatefilePath", path, sizeof (path));
    CreateDirectory (path, NULL);
    strcat (path, "default.uss");
    strcpy (savestate_fname, path);
    fetch_path ("InputPath", path, sizeof (path));
    CreateDirectory (path, NULL);
    fkey = read_disk_history ();
    if (fkey)
	RegCloseKey (fkey);
    read_rom_list ();
    load_keyring(NULL, NULL);
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
        if (GetModuleFileName(NULL, tmp1, sizeof tmp1) == 0)
	    break;
	h = CreateFile(tmp1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	    break;
	if (GetFileTime(h, &ft, NULL, NULL) == 0)
	    break;
	ft64.LowPart = ft.dwLowDateTime;
	ft64.HighPart = ft.dwHighDateTime;
	dwType = REG_QWORD;
	size = sizeof regft64;
	if (RegQueryValueEx(hWinUAEKey, "BetaToken", 0, &dwType, (LPBYTE)&regft64, &size) != ERROR_SUCCESS)
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
	CloseHandle(h);
    if (showmsg) {
	int r;
	char title[MAX_DPATH];

	dwType = REG_DWORD;
	size = sizeof data;
	if (hWinUAEKey && RegQueryValueEx(hWinUAEKey, "Beta_Just_Shut_Up", 0, &dwType, (LPBYTE)&data, &size) == ERROR_SUCCESS) {
	    if (data == 68000) {
		write_log("I was told to shut up :(\n");
		return 1;
	    }
	}

	_time64(&ltime);
	t = _gmtime64 (&ltime);
	/* "expire" in 1 month */
	if (MAKEBD(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday) > WINUAEDATE + 100)
	    pre_gui_message("This beta build of WinUAE is obsolete.\nPlease download newer version.");

	strcpy (title, "WinUAE Public Beta Disclaimer");
	strcat (title, BetaStr);
	r = MessageBox (NULL, BETAMESSAGE, title, MB_OKCANCEL | MB_TASKMODAL | MB_SETFOREGROUND | MB_ICONWARNING | MB_DEFBUTTON2);
	if (r == IDABORT || r == IDCANCEL)
	    return 0;
	if (ft64.QuadPart > 0) {
	    regft64 = ft64.QuadPart;
	    RegSetValueEx(hWinUAEKey, "BetaToken", 0, REG_QWORD, (LPBYTE)&regft64, sizeof regft64);
	}
    }
#endif
    return 1;
}

static int dxdetect (void)
{
#if !defined(WIN64)
    /* believe or not but this is MS supported way of detecting DX8+ */
    HMODULE h = LoadLibrary("D3D8.DLL");
    char szWrongDXVersion[MAX_DPATH];
    if (h) {
	FreeLibrary (h);
	return 1;
    }
    WIN32GUI_LoadUIString(IDS_WRONGDXVERSION, szWrongDXVersion, MAX_DPATH);
    pre_gui_message(szWrongDXVersion);
    return 0;
#else
    return 1;
#endif
}

int os_winnt, os_winnt_admin, os_64bit, os_vista;

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
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
	write_log ("OpenProcessToken Error %u\n", GetLastError());
	return FALSE;
    }

    // Call GetTokenInformation to get the buffer size.
    if(!GetTokenInformation(hToken, TokenGroups, NULL, dwSize, &dwSize)) {
	dwResult = GetLastError();
	if(dwResult != ERROR_INSUFFICIENT_BUFFER) {
	    write_log("GetTokenInformation Error %u\n", dwResult);
	    return FALSE;
	}
    }

    // Allocate the buffer.
    pGroupInfo = (PTOKEN_GROUPS)GlobalAlloc(GPTR, dwSize);

    // Call GetTokenInformation again to get the group information.
    if(!GetTokenInformation(hToken, TokenGroups, pGroupInfo, dwSize, &dwSize)) {
	write_log ("GetTokenInformation Error %u\n", GetLastError());
	return FALSE;
    }

    // Create a SID for the BUILTIN\Administrators group.
    if(!AllocateAndInitializeSid(&SIDAuth, 2,
		 SECURITY_BUILTIN_DOMAIN_RID,
		 DOMAIN_ALIAS_RID_ADMINS,
		 0, 0, 0, 0, 0, 0,
		 &pSID)) {
	write_log( "AllocateAndInitializeSid Error %u\n", GetLastError() );
	return FALSE;
   }

    // Loop through the group SIDs looking for the administrator SID.
    for(i = 0; i < pGroupInfo->GroupCount; i++) {
	if (EqualSid(pSID, pGroupInfo->Groups[i].Sid))
	    isadmin = 1;
    }
    
    if (pSID)
	FreeSid(pSID);
    if (pGroupInfo)
	GlobalFree(pGroupInfo);
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

    pGetNativeSystemInfo = (PGETNATIVESYSTEMINFO)GetProcAddress(
	GetModuleHandle("kernel32.dll"), "GetNativeSystemInfo");
    pIsUserAnAdmin = (PISUSERANADMIN)GetProcAddress(
	GetModuleHandle("shell32.dll"), "IsUserAnAdmin");

    GetSystemInfo(&SystemInfo);
    if (pGetNativeSystemInfo)
	pGetNativeSystemInfo(&SystemInfo);
    osVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (GetVersionEx(&osVersion)) {
	if ((osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT) &&
	    (osVersion.dwMajorVersion <= 4))
	{
	    /* WinUAE not supported on this version of Windows... */
	    char szWrongOSVersion[MAX_DPATH];
	    WIN32GUI_LoadUIString(IDS_WRONGOSVERSION, szWrongOSVersion, MAX_DPATH);
	    pre_gui_message(szWrongOSVersion);
	    return FALSE;
	}
	if (osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT)
	    os_winnt = 1;
	if (osVersion.dwMajorVersion >= 6)
	    os_vista = 1;
	if (SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
	    os_64bit = 1;
    }
    if (!os_winnt)
	return 1;
    os_winnt_admin = isadminpriv ();
    if (os_winnt_admin && pIsUserAnAdmin) {
	if (pIsUserAnAdmin())
	    os_winnt_admin++;
    }

    return 1;
}

typedef HRESULT (CALLBACK* SHGETFOLDERPATH)(HWND,int,HANDLE,DWORD,LPTSTR);
typedef BOOL (CALLBACK* SHGETSPECIALFOLDERPATH)(HWND,LPTSTR,int,BOOL);
extern int path_type;
static void getstartpaths(int start_data)
{
    SHGETFOLDERPATH pSHGetFolderPath;
    SHGETSPECIALFOLDERPATH pSHGetSpecialFolderPath;
    char *posn, *p;
    char tmp[MAX_DPATH], tmp2[MAX_DPATH], prevpath[MAX_DPATH];
    DWORD v;
    HKEY key;
    DWORD dispo;
    int path_done;

    path_done = 0;
    path_type = 2005;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, "Software\\Arabuusimiehet\\WinUAE", 0, "", REG_OPTION_NON_VOLATILE,
			  KEY_WRITE | KEY_READ, NULL, &key, &dispo) == ERROR_SUCCESS)
    {
	DWORD size = sizeof (prevpath);
        DWORD dwType = REG_SZ;
	if (RegQueryValueEx (key, "PathMode", 0, &dwType, (LPBYTE)&prevpath, &size) != ERROR_SUCCESS)
	    prevpath[0] = 0;
	RegCloseKey(key);
    }
    pSHGetFolderPath = (SHGETFOLDERPATH)GetProcAddress(
	GetModuleHandle("shell32.dll"), "SHGetFolderPathA");
    pSHGetSpecialFolderPath = (SHGETSPECIALFOLDERPATH)GetProcAddress(
	GetModuleHandle("shell32.dll"), "SHGetSpecialFolderPathA");
    strcpy (start_path_exe, _pgmptr );
   if((posn = strrchr (start_path_exe, '\\')))
	posn[1] = 0;

    strcpy (tmp, start_path_exe);
    strcat (tmp, "..\\shared\\rom\\rom.key");
    v = GetFileAttributes(tmp);
    if (v != INVALID_FILE_ATTRIBUTES) {
	af_path_old = 1;
	if (!strcmp (prevpath, "AF")) {
	    path_done = 1;
	    path_type = 1;
	}
	
    }

    strcpy (tmp, start_path_exe);
    strcat (tmp, "roms");
    v = GetFileAttributes(tmp);
    if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
	WIN32_FIND_DATA fd;
	HANDLE h;
	int i;
	strcat (tmp, "\\*.*");
	h = FindFirstFile(tmp, &fd);
	if (h != INVALID_HANDLE_VALUE) {
	    for (i = 0; i < 3; i++) {
		if (!FindNextFile (h, &fd))
		    break;
	    }
	    if (i == 3) {
		winuae_path = 1;
		if (!strcmp (prevpath, "WinUAE")) {
		    path_done = 1;
		    path_type = 0;
		}
	    }
	    FindClose(h);
	}
    }

    p = getenv("AMIGAFOREVERDATA");
    if (p) {
	strcpy (tmp, p);
	fixtrailing(tmp);
	strcpy (start_path_af, p);
	fixtrailing(start_path_af);
	v = GetFileAttributes(tmp);
	if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
	    if (start_data == 0) {
		if (path_done == 0) {
		    strcpy (start_path_data, start_path_af);
		    strcat (start_path_data, "WinUAE");
		    path_done = 1;
		}
		start_data = 1;
	    }
	    af_path_2005 = 1;
	}
    }

    {
	BOOL ok = FALSE;
	if (pSHGetFolderPath)
	    ok = SUCCEEDED(pSHGetFolderPath(NULL, CSIDL_COMMON_DOCUMENTS, NULL, 0, tmp));
	else if (pSHGetSpecialFolderPath)
	    ok = pSHGetSpecialFolderPath(NULL, tmp, CSIDL_COMMON_DOCUMENTS, 0);
	if (ok) {
	    fixtrailing(tmp);
	    strcpy (tmp2, tmp);
	    strcat (tmp2, "Amiga Files\\");
	    strcpy (tmp, tmp2);
	    strcat(tmp, "WinUAE");
	    v = GetFileAttributes(tmp);
	    if (v != INVALID_FILE_ATTRIBUTES && (v & FILE_ATTRIBUTE_DIRECTORY)) {
		if (start_data == 0) {
		    if (path_done == 0) {
			strcpy (start_path_af, tmp2);
			strcpy (start_path_data, start_path_af);
			strcat (start_path_data, "WinUAE");
			path_done = 1;
		    }
		    start_data = 1;
		}
		af_path_2005 = 1;
	    }
	}
    }

    v = GetFileAttributes(start_path_data);
    if (v == INVALID_FILE_ATTRIBUTES || !(v & FILE_ATTRIBUTE_DIRECTORY) || start_data <= 0)
	strcpy(start_path_data, start_path_exe);

    fixtrailing(start_path_data);
}

extern void test (void);
extern int screenshotmode, b0rken_ati_overlay, postscript_print_debugging, sound_debug, log_uaeserial;
extern int force_direct_catweasel, max_allowed_mman;

extern DWORD_PTR cpu_affinity;
static DWORD_PTR original_affinity;

static int getval(char *s)
{
    int base = 10;
    int v;
    char *endptr;

    if (s[0] == '0' && s[1] == 'x')
	s += 2, base = 16;
    v = strtol (s, &endptr, base);
    if (*endptr != '\0' || *s == '\0')
	return 0;
    return v;
}

static void makeverstr(char *s)
{
#if WINUAEBETA > 0
    sprintf(BetaStr, " (%sBeta %d, %d.%02d.%02d)", WINUAEPUBLICBETA > 0 ? "Public " : "", WINUAEBETA,
	GETBDY(WINUAEDATE), GETBDM(WINUAEDATE), GETBDD(WINUAEDATE));
    sprintf(s, "WinUAE %d.%d.%d%s",
	UAEMAJOR, UAEMINOR, UAESUBREV, BetaStr);
#else
    sprintf(s, "WinUAE %d.%d.%d (%d.%02d.%02d)",
	UAEMAJOR, UAEMINOR, UAESUBREV, GETBDY(WINUAEDATE), GETBDM(WINUAEDATE), GETBDD(WINUAEDATE));
#endif
}

static int multi_display = 1;
static int start_data = 0;

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
	if (!strcmp (arg, "-disableowr")) {
	    b0rken_ati_overlay = -1;
	    continue;
	}
	if (!strcmp (arg, "-enableowr")) {
	    b0rken_ati_overlay = 1;
	    continue;
	}
	if (!strcmp (arg, "-nordtsc")) {
	    no_rdtsc = 1;
	    continue;
	}
	if (!strcmp (arg, "-forcerdtsc")) {
	    no_rdtsc = -1;
	    continue;
	}
	if (!strcmp (arg, "-norawinput")) {
	    no_rawinput = 1;
	    continue;
	}
	if (!strcmp (arg, "-scsilog")) {
	    log_scsi = 1;
	    continue;
	}
	if (!strcmp (arg, "-seriallog")) {
	    log_uaeserial = 1;
	    continue;
	}
	if (!strcmp (arg, "-nomultidisplay")) {
	    multi_display = 0;
	    continue;
	}
	if (!strcmp (arg, "-legacypaths")) {
	    start_data = -1;
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
	if (!strcmp (arg, "-affinity") && i + 1 < argc) {
	    cpu_affinity = getval (argv[++i]);
	    if (cpu_affinity == 0)
		cpu_affinity = original_affinity;
	    SetThreadAffinityMask(GetCurrentThread(), cpu_affinity);
	    continue;
	}
	if (!strcmp (arg, "-datapath") && i + 1 < argc) {
	    strcpy(start_path_data, argv[++i]);
	    start_data = 1;
	    continue;
	}
	if (!strcmp (arg, "-maxmem") && i + 1 < argc) {
	    max_allowed_mman = getval (argv[++i]);
	    continue;
	}
	xargv[xargc++] = my_strdup(arg);
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

    if (!osdetect())
	return 0;
    if (!dxdetect())
	return 0;
    if (!os_winnt && max_allowed_mman > 256)
	max_allowed_mman = 256;

    hInst = hInstance;
    hMutex = CreateMutex( NULL, FALSE, "WinUAE Instantiated" ); // To tell the installer we're running
#ifdef AVIOUTPUT
    AVIOutput_Initialize();
#endif

    argv = xcalloc (sizeof (char*),  __argc);
    argc = process_arg(argv);

    getstartpaths(start_data);
    makeverstr(VersionStr);
    SetCurrentDirectory (start_path_data);

    logging_init ();

    if(WIN32_RegisterClasses() && WIN32_InitLibraries() && DirectDraw_Start(NULL)) {
	DEVMODE devmode;
	DWORD i = 0;

	DirectDraw_Release ();
	write_log ("Enumerating display devices.. \n");
	enumeratedisplays (multi_display);
	write_log ("Sorting devices and modes..\n");
	sortdisplays ();
	write_log ("done\n");

	memset (&devmode, 0, sizeof(devmode));
	devmode.dmSize = sizeof(DEVMODE);
	if (EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &devmode)) {
	    default_freq = devmode.dmDisplayFrequency;
	    if(default_freq >= 70)
		default_freq = 70;
	    else
		default_freq = 60;
	}
	WIN32_HandleRegistryStuff();
	WIN32_InitLang();
	WIN32_InitHtmlHelp();
	DirectDraw_Release();
	if (betamessage ()) {
	    keyboard_settrans ();
#ifdef CATWEASEL
	    catweasel_init();
#endif
#ifdef PARALLEL_PORT
	    paraport_mask = paraport_init ();
#endif
	    createIPC();
	    enumserialports();
	    real_main (argc, argv);
	}
    }

    closeIPC();
    write_disk_history ();
    if (mm_timerres && timermode == 0)
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
    WIN32_CleanupLibraries();
    close_console();
    _fcloseall();
    if(hWinUAEKey)
	RegCloseKey(hWinUAEKey);
    CloseHandle(hMutex);
#ifdef _DEBUG
    // show memory leaks
    //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    for (i = 0; i < argc; i++)
	free (argv[i]);
    free(argv);
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
	HRSRC res = FindResource(NULL, MAKEINTRESOURCE(drvsampleres[i + 0]), "WAVE");
	if (res != 0) {
	    HANDLE h = LoadResource(NULL, res);
	    int len = SizeofResource(NULL, res);
	    uae_u8 *p = LockResource(h);
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
    write_log("EVALEXCEPTION!\n");
    return EXCEPTION_EXECUTE_HANDLER;
}
#else

#if 0
#include <errorrep.h>
#endif
#include <dbghelp.h>
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

LONG WINAPI WIN32_ExceptionFilter(struct _EXCEPTION_POINTERS *pExceptionPointers, DWORD ec)
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
		uaecptr opc = m68k_getpc(&regs);
		void *ps = get_real_address (0);
		m68k_dumpstate (0, 0);
		efix (&ctx->Eax, p, ps, &got);
		efix (&ctx->Ebx, p, ps, &got);
		efix (&ctx->Ecx, p, ps, &got);
		efix (&ctx->Edx, p, ps, &got);
		efix (&ctx->Esi, p, ps, &got);
		efix (&ctx->Edi, p, ps, &got);
		write_log ("Access violation! (68KPC=%08.8X HOSTADDR=%p)\n", M68K_GETPC, p);
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

	if (os_winnt && GetModuleFileName(NULL, path, MAX_DPATH)) {
	    char *slash = strrchr (path, '\\');
	    _time64(&now);
	    when = *_localtime64(&now);
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
		MINIDUMPWRITEDUMP dump = (MINIDUMPWRITEDUMP)GetProcAddress(dll, "MiniDumpWriteDump");
		if (dump) {
		    HANDLE f = CreateFile(path2, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		    if (f != INVALID_HANDLE_VALUE) {
			MINIDUMP_EXCEPTION_INFORMATION exinfo;
			exinfo.ThreadId = GetCurrentThreadId();
			exinfo.ExceptionPointers = pExceptionPointers;
			exinfo.ClientPointers = 0;
			dump (GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpNormal, &exinfo, NULL, NULL);
			CloseHandle (f);
			if (isfullscreen () <= 0) {
			    sprintf (msg, "Crash detected. MiniDump saved as:\n%s\n", path2);
			    MessageBox( NULL, msg, "Crash", MB_OK | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND );
			}
		    }
		}
	    }
	}
    }
#endif
#if 0
	HMODULE hFaultRepDll = LoadLibrary("FaultRep.dll") ;
	if ( hFaultRepDll )
	{
	    pfn_REPORTFAULT pfn = (pfn_REPORTFAULT)GetProcAddress( hFaultRepDll, "ReportFault");
	    if ( pfn )
	    {
		EFaultRepRetVal rc = pfn( pExceptionPointers, 0) ;
		lRet = EXCEPTION_EXECUTE_HANDLER;
	    }
	    FreeLibrary(hFaultRepDll );
	}
#endif
    return lRet ;
}

#endif

void systray (HWND hwnd, int remove)
{
    NOTIFYICONDATA nid;

    if (hwnd == NULL)
	return;
    if (!TaskbarRestartOk) {
	TaskbarRestart = RegisterWindowMessage(TEXT("TaskbarCreated"));
	TaskbarRestartOk = 1;
    }
    memset (&nid, 0, sizeof (nid));
    nid.cbSize = sizeof (nid);
    nid.hWnd = hwnd;
    nid.hIcon = LoadIcon (hInst, (LPCSTR)MAKEINTRESOURCE(IDI_APPICON));
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    nid.uCallbackMessage = WM_USER + 1;
    Shell_NotifyIcon (remove ? NIM_DELETE : NIM_ADD, &nid);
}

void systraymenu (HWND hwnd)
{
    POINT pt;
    HMENU menu, menu2, drvmenu;
    int drvs[] = { ID_ST_DF0, ID_ST_DF1, ID_ST_DF2, ID_ST_DF3, -1 };
    int i;
    char text[100];

    winuae_inactive (hwnd, FALSE);
    WIN32GUI_LoadUIString( IDS_STMENUNOFLOPPY, text, sizeof (text));
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
    winuae_active (hwnd, FALSE);
}

static void LLError(const char *s)
{
    DWORD err = GetLastError();

    if (err == ERROR_MOD_NOT_FOUND || err == ERROR_DLL_NOT_FOUND)
	return;
    write_log("%s failed to open %d\n", s, err);
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

    newname = xmalloc(strlen(name) + 1 + 10);
    if (!newname)
	return NULL;
    for (round = 0; round < 4; round++) {
	char *s;
	strcpy(newname, name);
#ifdef CPU_64_BIT
	switch(round)
	{
	    case 0:
	    p = strstr(newname,"32");
	    if (p) {
		p[0] = '6';
		p[1] = '4';
	    }
	    break;
	    case 1:
	    p = strchr(newname,'.');
	    strcpy(p,"_64");
	    strcat(p, strchr(name,'.'));
	    break;
	    case 2:
	    p = strchr(newname,'.');
	    strcpy(p,"64");
	    strcat(p, strchr(name,'.'));
	    break;
	}
#endif
	s = xmalloc (strlen (start_path_exe) + strlen (WIN32_PLUGINDIR) + strlen (newname) + 1);
	if (s) {
	    sprintf (s, "%s%s%s", start_path_exe, WIN32_PLUGINDIR, newname);
	    m = LoadLibrary (s);
	    if (m)
		goto end;
	    LLError(s);
	    xfree (s);
	}
	m = LoadLibrary (newname);
	if (m)
	    goto end;
	LLError(newname);
#ifndef CPU_64_BIT
	break;
#endif
    }
end:
    xfree(newname);
    return m;
}

typedef BOOL (CALLBACK* SETPROCESSDPIAWARE)(void);
typedef BOOL (CALLBACK* CHANGEWINDOWMESSAGEFILTER)(UINT, DWORD);
#define MSGFLT_ADD 1

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SETPROCESSDPIAWARE pSetProcessDPIAware;
    HANDLE thread;

    thread = GetCurrentThread();
    original_affinity = SetThreadAffinityMask(thread, 1); 
#if 0
    CHANGEWINDOWMESSAGEFILTER pChangeWindowMessageFilter;
    pChangeWindowMessageFilter = (CHANGEWINDOWMESSAGEFILTER)GetProcAddress(
	GetModuleHandle("user32.dll"), "ChangeWindowMessageFilter");
    if (pChangeWindowMessageFilter)
	pChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
#endif
    pSetProcessDPIAware = (SETPROCESSDPIAWARE)GetProcAddress(
	GetModuleHandle("user32.dll"), "SetProcessDPIAware");
    if (pSetProcessDPIAware)
	pSetProcessDPIAware();

    __try {
	WinMain2 (hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    } __except(WIN32_ExceptionFilter(GetExceptionInformation(), GetExceptionCode())) {
    }
    SetThreadAffinityMask(thread, original_affinity);
    return FALSE;
}


