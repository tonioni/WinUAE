/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997-1998 Mathias Ortmann
 * Copyright 1997-1999 Brian King
 */

/* Uncomment this line if you want the logs time-stamped */
/* #define TIMESTAMP_LOGS */

#include "config.h"
#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

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

#include "sysdeps.h"
#include "options.h"
#include "sound.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "xwin.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "keybuf.h"
#include "drawing.h"
#include "dxwrap.h"
#include "picasso96.h"
#include "bsdsocket.h"
#include "win32.h"
#include "win32gfx.h"
#include "win32gui.h"
#include "resource.h"
#include "autoconf.h"
#include "gui.h"
#include "newcpu.h"
#include "sys/mman.h"
#include "avioutput.h"
#include "ahidsound.h"
#include "zfile.h"
#include "savestate.h"
#include "ioport.h"
#include "parser.h"
#include "scsidev.h"
#include "disk.h"

unsigned long *win32_stackbase; 
unsigned long *win32_freestack[42]; //EXTRA_STACK_SIZE

extern FILE *debugfile;
extern int console_logging;
static OSVERSIONINFO osVersion;

int useqpc = 0; /* Set to TRUE to use the QueryPerformanceCounter() function instead of rdtsc() */
int cpu_mmx = 0;
static int no_rdtsc;

HINSTANCE hInst = NULL;
HMODULE hUIDLL = NULL;
HWND (WINAPI *pHtmlHelp)(HWND, LPCSTR, UINT, LPDWORD ) = NULL;
HWND hAmigaWnd, hMainWnd, hHiddenWnd;
RECT amigawin_rect;
static UINT TaskbarRestart;
static int TaskbarRestartOk;

char VersionStr[256];

int in_sizemove;
int manual_painting_needed;
int manual_palette_refresh_needed;
int win_x_diff, win_y_diff;

int toggle_sound;
int paraport_mask;

HKEY hWinUAEKey = NULL;
COLORREF g_dwBackgroundColor  = RGB(10, 0, 10);

static int emulation_paused;
static int activatemouse = 1;
static int ignore_messages_all;
int pause_emulation;

static int didmousepos;
int mouseactive, focus;

static int mm_timerres;
static int timermode, timeon;
static HANDLE timehandle;

char *start_path;
char help_file[ MAX_DPATH ];

extern int harddrive_dangerous, do_rdbdump, aspi_allow_all, dsound_hardware_mixing, no_rawinput;
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

static uae_u64 win32_read_processor_time (void)
{
    uae_u32 foo, bar;
     __asm
    {
        rdtsc
        mov foo, eax
        mov bar, edx
    }
    return ((uae_u64)bar << 32) | foo;
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
    __try
    {
	__asm 
	{
	    rdtsc
	}
    } __except( GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ) {
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
    } __except( GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ) {
    }

    if (QueryPerformanceFrequency(&freq)) {
	qpc_avail = 1;
	write_log("CLOCKFREQ: QPF %.2fMHz\n", freq.QuadPart / 1000000.0);
	qpfrate = freq.QuadPart;
	 /* we don't want 32-bit overflow */
	if (qpfrate > 100000000) {
	    qpfrate >>= 6;
	    qpc_avail = -1;
	}
    } else {
	write_log("CLOCKREQ: QPF not supported\n");
    }

    if (!rpt_available && !qpc_avail) {
	pre_gui_message ("No timing reference found\n(no RDTSC or QPF support detected)\nWinUAE will exit\n");
	return 0;
    }

    init_mmtimer();
    SetThreadPriority ( GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    sleep_millis (100);
    dummythread_die = -1;

    if (qpc_avail || rpt_available)  {
	int qpfinit = 0;

	if (rpt_available) {
	    clockrateidle = win32_read_processor_time();
	    sleep_millis (500);
	    clockrateidle = (win32_read_processor_time() - clockrateidle) * 2;
	    dummythread_die = 0;
	    _beginthread(&dummythread, 0, 0);
	    sleep_millis (100);
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

    SetThreadPriority ( GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    dummythread_die = 1;
   
    if (clkdiv >= 0.90 && clkdiv <= 1.10 && rpt_available) {
	limit = 2.5;
	if ((ratea2 / ratecnt) < limit * clockrate1000) { /* regular Sleep() is ok */
	    timermode = 1;
	    write_log ("Using Sleep() (resolution < %.1fms)\n", limit);
	} else if (mm_timerres && (ratea1 / ratecnt) < limit * clockrate1000) { /* MM-timer is ok */
	    timermode = 0;
	    timebegin ();
	    write_log ("Using MultiMedia timers (resolution < %.1fms)\n", limit);
	} else {
	    timermode = -1; /* both timers are bad, fall back to busy-wait */
	    write_log ("falling back to busy-loop waiting (timer resolution > %.1fms)\n", limit);
	}
    } else {
	timermode = -1;
	write_log ("forcing busy-loop wait mode\n");
    }
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

void setmouseactive (int active)
{
    int oldactive = mouseactive;
    static int mousecapture, showcursor;
    char txt[100], txt2[110];

    if (active > 0 && ievent_alive > 0) {
	mousehack_set (mousehack_follow);
	return;
    }
    mousehack_set (mousehack_dontcare);
    inputdevice_unacquire ();
    mouseactive = active;
    strcpy (txt, "WinUAE");
    if (mouseactive > 0) {
	focus = 1;
        WIN32GUI_LoadUIString (currprefs.win32_middle_mouse ? IDS_WINUAETITLE_MMB : IDS_WINUAETITLE_NORMAL,
	    txt2, sizeof (txt2));
	strcat (txt, " - ");
	strcat (txt, txt2);
    }
    SetWindowText (hMainWnd, txt);
    if (mousecapture) {
	ClipCursor (0);
	ReleaseCapture ();
	mousecapture = 0;
    }
    if (showcursor) {
	ShowCursor (TRUE);
	showcursor = 0;
    }
    if (mouseactive) {
	if (focus) {
	    if (!showcursor)
		ShowCursor (FALSE);
	    showcursor = 1;
	    if (!isfullscreen()) {
		if (!mousecapture) {
		    SetCapture (hAmigaWnd);
		    ClipCursor (&amigawin_rect);
		}
		mousecapture = 1;
	    }
	    setcursor (-1, -1);
	}
	inputdevice_acquire (mouseactive);
    }
}

#ifndef AVIOUTPUT
static int avioutput_video = 0;
#endif

void setpriority (struct threadpriorities *pri)
{
    int err;
    write_log ("changing priority to %s\n", pri->name);
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
    write_log ("WinUAE now active via WM_ACTIVATE\n");
    pri = &priorities[currprefs.win32_inactive_priority];
#ifndef _DEBUG
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
#ifdef AHI
    ahi_close_sound ();
#endif
    close_sound ();
#ifdef AHI
    ahi_open_sound ();
#endif
    init_sound ();
    if (WIN32GFX_IsPicassoScreen ())
	WIN32GFX_EnablePicasso();
    getcapslock ();
    inputdevice_acquire (mouseactive);
    wait_keyrelease ();
    inputdevice_acquire (mouseactive);
    if (isfullscreen())
	setmouseactive (1);
    manual_palette_refresh_needed = 1;

}

static void winuae_inactive (HWND hWnd, int minimized)
{
    struct threadpriorities *pri;

    focus = 0;
    write_log( "WinUAE now inactive via WM_ACTIVATE\n" );
    wait_keyrelease ();
    setmouseactive (0);
    close_sound ();
#ifdef AHI
    ahi_close_sound ();
#endif
    init_sound ();
#ifdef AHI
    ahi_open_sound ();
#endif
    pri = &priorities[currprefs.win32_inactive_priority];
    if (!quit_program) {
	if (minimized) {
	    inputdevice_unacquire ();
	    pri = &priorities[currprefs.win32_iconified_priority];
	    if (currprefs.win32_iconified_nosound) {
		close_sound ();
    #ifdef AHI
		ahi_close_sound ();
    #endif
	    }
	    if (!avioutput_video) {
		set_inhibit_frame (IHF_WINDOWHIDDEN);
	    }
	    if (currprefs.win32_iconified_pause) {
		close_sound ();
    #ifdef AHI
		ahi_close_sound ();
    #endif
		emulation_paused = 1;
	    }
	} else {
	    if (currprefs.win32_inactive_nosound) {
		close_sound ();
    #ifdef AHI
		ahi_close_sound ();
    #endif
	    }
	    if (currprefs.win32_inactive_pause) {
		close_sound ();
    #ifdef AHI
		ahi_close_sound ();
    #endif
		emulation_paused = 1;
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
    close_sound ();
#ifdef AHI
    ahi_close_sound ();
#endif
}

static long FAR PASCAL AmigaWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int ignorenextactivateapp;
    PAINTSTRUCT ps;
    HDC hDC;
    LPMINMAXINFO lpmmi;
    RECT rect;
    int mx, my, v;
    static int mm;
    static int minimized;

    if (ignore_messages_all)
	return DefWindowProc (hWnd, message, wParam, lParam);

    if (hMainWnd == 0)
	hMainWnd = hWnd;

    switch( message ) 
    {
    case WM_SIZE:
    {
	if (isfullscreen ()) {
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
	    if (v != minimized) {
		if (v)
		    winuae_inactive (hWnd, wParam == SIZE_MINIMIZED);
		else
		    winuae_active (hWnd, minimized);
	    }
	    minimized = v;
	    return 0;
	}
    }
	    
    case WM_ACTIVATE:
	if (!isfullscreen ()) {
    	    minimized = HIWORD (wParam);
	    if (LOWORD (wParam) != WA_INACTIVE) {
      		winuae_active (hWnd, minimized);
		if (ignorenextactivateapp > 0)
		    ignorenextactivateapp--;
	    } else {
		winuae_inactive (hWnd, minimized);
	    }
	    return 0;
	}

    case WM_ACTIVATEAPP:
	if (!wParam) {
	    setmouseactive (0);
	} else {
	    if (!ignorenextactivateapp && isfullscreen () && is3dmode ()) {
	        WIN32GFX_DisplayChangeRequested ();
	        ignorenextactivateapp = 2;
	    }
	    if (gui_active && isfullscreen())
	        exit_gui (0);
	}
        manual_palette_refresh_needed = 1;
    return 0;

    case WM_PALETTECHANGED:
        if ((HWND)wParam != hWnd)
	    manual_palette_refresh_needed = 1;
    break;

    case WM_KEYDOWN:
	if (dinput_wmkey ((uae_u32)lParam))
	    gui_display (-1);
    return 0;

    case WM_LBUTTONUP:
	if (dinput_winmouse () > 0)
	    setmousebuttonstate (dinput_winmouse(), 0, 0);
    return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
	if (!mouseactive && !isfullscreen()) {
	    setmouseactive (1);
	}
	if (dinput_winmouse () > 0)
	    setmousebuttonstate (dinput_winmouse(), 0, 1);
    return 0;
    case WM_RBUTTONUP:
	if (dinput_winmouse () > 0)
	    setmousebuttonstate (dinput_winmouse(), 1, 0);
    return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
	if (dinput_winmouse () > 0)
	    setmousebuttonstate (dinput_winmouse(), 1, 1);
    return 0;
    case WM_MBUTTONUP:
	if (dinput_winmouse () > 0)
	    setmousebuttonstate (dinput_winmouse(), 2, 0);
    return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
	if (dinput_winmouse () > 0)
	    setmousebuttonstate (dinput_winmouse(), 2, 1);
    return 0;
    case WM_MOUSEWHEEL:
	if (dinput_winmouse () > 0)
	    setmousestate (dinput_winmouse(), 2, ((short)HIWORD(wParam)), 0);
    return 0;

    case WM_PAINT:
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
	    manual_palette_refresh_needed = 0;
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
	if( !currprefs.win32_ctrl_F11_is_quit )
	    uae_quit ();
    return 0;

    case WM_WINDOWPOSCHANGED:
	GetWindowRect( hWnd, &amigawin_rect);
    break;

    case WM_MOUSEMOVE:
        mx = (signed short) LOWORD (lParam);
        my = (signed short) HIWORD (lParam);
	if (dinput_winmouse () > 0) {
	    int mxx = (amigawin_rect.right - amigawin_rect.left) / 2;
	    int myy = (amigawin_rect.bottom - amigawin_rect.top) / 2;
	    mx = mx - mxx;
	    my = my - myy;
	    setmousestate (dinput_winmouse (), 0, mx, 0);
	    setmousestate (dinput_winmouse (), 1, my, 0);
        } else if ((!mouseactive && !isfullscreen())) {
	    setmousestate (0, 0, mx, 1);
	    setmousestate (0, 1, my, 1);
	} else {
#if 0
	    int mxx = (amigawin_rect.right - amigawin_rect.left) / 2;
	    int myy = (amigawin_rect.bottom - amigawin_rect.top) / 2;
	    mx = mx - mxx;
	    my = my - myy;
	    setmousestate (0, 0, mx, 0);
	    setmousestate (0, 1, my, 0);
#endif
	}
	if (mouseactive)
	    setcursor (LOWORD (lParam), HIWORD (lParam));
    return 0;

    case WM_MOVING:
    case WM_MOVE:
	WIN32GFX_WindowMove();
    return TRUE;

    case WM_GETMINMAXINFO:
	rect.left=0;
	rect.top=0;
	lpmmi=(LPMINMAXINFO)lParam;
	rect.right=320;
	rect.bottom=256;
	//AdjustWindowRectEx(&rect,WSTYLE,0,0);
	lpmmi->ptMinTrackSize.x=rect.right-rect.left;
	lpmmi->ptMinTrackSize.y=rect.bottom-rect.top;
    return 0;

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
	if (!manual_painting_needed && focus) {
	    switch (wParam) // Check System Calls
	    {
		case SC_SCREENSAVE: // Screensaver Trying To Start?
		case SC_MONITORPOWER: // Monitor Trying To Enter Powersave?
		return 0; // Prevent From Happening
	    }
	}
    break;

    case 0xff: // WM_INPUT:
    handle_rawinput (lParam);
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
		DiskSelection (isfullscreen() ? NULL : hWnd, IDC_DF0, 0, &changed_prefs, 0);
		disk_insert (0, changed_prefs.df[0]);
	    break;
	    case ID_ST_DF1:
		DiskSelection (isfullscreen() ? NULL : hWnd, IDC_DF1, 0, &changed_prefs, 0);
		disk_insert (1, changed_prefs.df[0]);
	    break;
	    case ID_ST_DF2:
		DiskSelection (isfullscreen() ? NULL : hWnd, IDC_DF2, 0, &changed_prefs, 0);
		disk_insert (2, changed_prefs.df[0]);
	    break;
	    case ID_ST_DF3:
		DiskSelection (isfullscreen() ? NULL : hWnd, IDC_DF3, 0, &changed_prefs, 0);
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

static long FAR PASCAL MainWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC hDC;

    switch (message) {

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
	return AmigaWindowProc (hWnd, message, wParam, lParam);

     case WM_DISPLAYCHANGE:
	if (!isfullscreen() && !currprefs.gfx_filter && (wParam + 7) / 8 != DirectDraw_GetBytesPerPixel() )
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
	if( hAmigaWnd && GetWindowRect (hAmigaWnd, &amigawin_rect) )
	{
	    if (in_sizemove > 0)
		break;

	    if( !isfullscreen() && hAmigaWnd )
	    {
	        static int store_xy;
	        RECT rc2;
		if( GetWindowRect( hMainWnd, &rc2 )) {
		    if (amigawin_rect.left & 3)
		    {
			MoveWindow (hMainWnd, rc2.left+ 4 - amigawin_rect.left % 4, rc2.top,
				    rc2.right - rc2.left, rc2.bottom - rc2.top, TRUE);

		    }
		    if( hWinUAEKey && store_xy++)
		    {
			DWORD left = rc2.left - win_x_diff;
			DWORD top = rc2.top - win_y_diff;
			RegSetValueEx( hWinUAEKey, "xPos", 0, REG_DWORD, (LPBYTE)&left, sizeof( LONG ) );
			RegSetValueEx( hWinUAEKey, "yPos", 0, REG_DWORD, (LPBYTE)&top, sizeof( LONG ) );
		    }
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

static long FAR PASCAL HiddenWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc (hWnd, message, wParam, lParam);
}

void handle_events (void)
{
    MSG msg;
    int was_paused = 0;

    while (emulation_paused > 0 || pause_emulation) {
	if ((emulation_paused > 0 || pause_emulation) && was_paused == 0) {
	    close_sound ();
#ifdef AHI
	    ahi_close_sound ();
#endif
	    was_paused = 1;
	    manual_painting_needed++;
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
    }
    while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }
    if (was_paused) {
	init_sound ();
#ifdef AHI
        ahi_open_sound ();
#endif
	emulation_paused = 0;
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
    HDC hDC = GetDC( NULL );

    if (GetDeviceCaps (hDC, NUMCOLORS) != -1) 
        g_dwBackgroundColor = RGB (255, 0, 255);
    ReleaseDC (NULL, hDC);

    wc.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW | CS_DBLCLKS | CS_OWNDC;
    wc.lpfnWndProc = AmigaWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
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
    wc.cbWndExtra = 0;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush (g_dwBackgroundColor);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "PCsuxRox";
    if (!RegisterClass (&wc))
	return 0;
    
    wc.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = HiddenWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
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
	FreeLibrary (hRichEdit);
    
    if( hHtmlHelp )
        FreeLibrary( hHtmlHelp );

    if( hUIDLL )
	FreeLibrary( hUIDLL );

    return 1;
}

/* HtmlHelp Initialization - optional component */
int WIN32_InitHtmlHelp( void )
{
    int result = 0;
    if (zfile_exists (help_file)) {
        if( hHtmlHelp = LoadLibrary( "HHCTRL.OCX" ) )
	{
	    pHtmlHelp = ( HWND(WINAPI *)(HWND, LPCSTR, UINT, LPDWORD ) )GetProcAddress( hHtmlHelp, "HtmlHelpA" );
	    result = 1;
	}
    }
    return result;
}

static HMODULE LoadGUI( void )
{
    HMODULE result = NULL;
    LPCTSTR dllname = NULL;
    LANGID language = GetUserDefaultLangID() & 0x3FF; // low 9-bits form the primary-language ID

    switch( language )
    {
    case LANG_AFRIKAANS:
	dllname = "WinUAE_Afrikaans.dll";
	break;
    case LANG_ARABIC:
	dllname = "WinUAE_Arabic.dll";
	break;
    case LANG_ARMENIAN:
	dllname = "WinUAE_Armenian.dll";
	break;
    case LANG_ASSAMESE:
	dllname = "WinUAE_Assamese.dll";
	break;
    case LANG_AZERI:
	dllname = "WinUAE_Azeri.dll";
	break;
    case LANG_BASQUE:
	dllname = "WinUAE_Basque.dll";
	break;
    case LANG_BELARUSIAN:
	dllname = "WinUAE_Belarusian.dll";
	break;
    case LANG_BENGALI:
	dllname = "WinUAE_Bengali.dll";
	break;
    case LANG_BULGARIAN:
	dllname = "WinUAE_Bulgarian.dll";
	break;
    case LANG_CATALAN:
	dllname = "WinUAE_Catalan.dll";
	break;
    case LANG_CHINESE:
	dllname = "WinUAE_Chinese.dll";
	break;
    case LANG_CROATIAN:
	dllname = "WinUAE_CroatianSerbian.dll";
	break;
    case LANG_CZECH:
	dllname = "WinUAE_Czech.dll";
	break;
    case LANG_DANISH:
	dllname = "WinUAE_Danish.dll";
	break;
    case LANG_DUTCH:
	dllname = "WinUAE_Dutch.dll";
	break;
    case LANG_ESTONIAN:
	dllname = "WinUAE_Estonian.dll";
	break;
    case LANG_FAEROESE:
	dllname = "WinUAE_Faeroese.dll";
	break;
    case LANG_FARSI:
	dllname = "WinUAE_Farsi.dll";
	break;
    case LANG_FINNISH:
	dllname = "WinUAE_Finnish.dll";
	break;
    case LANG_FRENCH:
	dllname = "WinUAE_French.dll";
	break;
    case LANG_GEORGIAN:
	dllname = "WinUAE_Georgian.dll";
	break;
    case LANG_GERMAN:
	dllname = "WinUAE_German.dll";
	break;
    case LANG_GREEK:
	dllname = "WinUAE_Greek.dll";
	break;
    case LANG_GUJARATI:
	dllname = "WinUAE_Gujarati.dll";
	break;
    case LANG_HEBREW:
	dllname = "WinUAE_Hebrew.dll";
	break;
    case LANG_HINDI:
	dllname = "WinUAE_Hindi.dll";
	break;
    case LANG_HUNGARIAN:
	dllname = "WinUAE_Hungarian.dll";
	break;
    case LANG_ICELANDIC:
	dllname = "WinUAE_Icelandic.dll";
	break;
    case LANG_INDONESIAN:
	dllname = "WinUAE_Indonesian.dll";
	break;
    case LANG_ITALIAN:
	dllname = "WinUAE_Italian.dll";
	break;
    case LANG_JAPANESE:
	dllname = "WinUAE_Japanese.dll";
	break;
    case LANG_KANNADA:
	dllname = "WinUAE_Kannada.dll";
	break;
    case LANG_KASHMIRI:
	dllname = "WinUAE_Kashmiri.dll";
	break;
    case LANG_KAZAK:
	dllname = "WinUAE_Kazak.dll";
	break;
    case LANG_KONKANI:
	dllname = "WinUAE_Konkani.dll";
	break;
    case LANG_KOREAN:
	dllname = "WinUAE_Korean.dll";
	break;
    case LANG_LATVIAN:
	dllname = "WinUAE_Latvian.dll";
	break;
    case LANG_LITHUANIAN:
	dllname = "WinUAE_Lithuanian.dll";
	break;
    case LANG_MACEDONIAN:
	dllname = "WinUAE_Macedonian.dll";
	break;
    case LANG_MALAY:
	dllname = "WinUAE_Malay.dll";
	break;
    case LANG_MALAYALAM:
	dllname = "WinUAE_Malayalam.dll";
	break;
    case LANG_MANIPURI:
	dllname = "WinUAE_Manipuri.dll";
	break;
    case LANG_MARATHI:
	dllname = "WinUAE_Marathi.dll";
	break;
    case LANG_NEPALI:
	dllname = "WinUAE_Nepali.dll";
	break;
    case LANG_NORWEGIAN:
	dllname = "WinUAE_Norwegian.dll";
	break;
    case LANG_ORIYA:
	dllname = "WinUAE_Oriya.dll";
	break;
    case LANG_POLISH:
	dllname = "WinUAE_Polish.dll";
	break;
    case LANG_PORTUGUESE:
	dllname = "WinUAE_Portuguese.dll";
	break;
    case LANG_PUNJABI:
	dllname = "WinUAE_Punjabi.dll";
	break;
    case LANG_ROMANIAN:
	dllname = "WinUAE_Romanian.dll";
	break;
    case LANG_RUSSIAN:
	dllname = "WinUAE_Russian.dll";
	break;
    case LANG_SANSKRIT:
	dllname = "WinUAE_Sanskrit.dll";
	break;
    case LANG_SINDHI:
	dllname = "WinUAE_Sindhi.dll";
	break;
    case LANG_SLOVAK:
	dllname = "WinUAE_Slovak.dll";
	break;
    case LANG_SLOVENIAN:
	dllname = "WinUAE_Slovenian.dll";
	break;
    case LANG_SPANISH:
	dllname = "WinUAE_Spanish.dll";
	break;
    case LANG_SWAHILI:
	dllname = "WinUAE_Swahili.dll";
	break;
    case LANG_SWEDISH:
	dllname = "WinUAE_Swedish.dll";
	break;
    case LANG_TAMIL:
	dllname = "WinUAE_Tamil.dll";
	break;
    case LANG_TATAR:
	dllname = "WinUAE_Tatar.dll";
	break;
    case LANG_TELUGU:
	dllname = "WinUAE_Telugu.dll";
	break;
    case LANG_THAI:
	dllname = "WinUAE_Thai.dll";
	break;
    case LANG_TURKISH:
	dllname = "WinUAE_Turkish.dll";
	break;
    case LANG_UKRAINIAN:
	dllname = "WinUAE_Ukrainian.dll";
	break;
    case LANG_URDU:
	dllname = "WinUAE_Urdu.dll";
	break;
    case LANG_UZBEK:
	dllname = "WinUAE_Uzbek.dll";
	break;
    case LANG_VIETNAMESE:
	dllname = "WinUAE_Vietnamese.dll";
	break;
    case 0x400:
	dllname = "guidll.dll";
	break;
    }

    if( dllname )
    {
	TCHAR  szFilename[ MAX_DPATH ];
	DWORD  dwVersionHandle, dwFileVersionInfoSize;
	LPVOID lpFileVersionData = NULL;
	BOOL   success = FALSE;
	result = LoadLibrary( dllname );
	if( result && GetModuleFileName( result, (LPTSTR)&szFilename, MAX_DPATH ) )
	{
	    dwFileVersionInfoSize = GetFileVersionInfoSize( szFilename, &dwVersionHandle );
	    if( dwFileVersionInfoSize )
	    {
		if( lpFileVersionData = calloc( 1, dwFileVersionInfoSize ) )
		{
		    if( GetFileVersionInfo( szFilename, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData ) )
		    {
			VS_FIXEDFILEINFO *vsFileInfo = NULL;
			UINT uLen;
			if( VerQueryValue( lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen ) )
			{
			    if( vsFileInfo &&
				HIWORD(vsFileInfo->dwProductVersionMS) == UAEMAJOR
				&& LOWORD(vsFileInfo->dwProductVersionMS) == UAEMINOR
				&& HIWORD(vsFileInfo->dwProductVersionLS) == UAESUBREV)
			    {
				success = TRUE;
				write_log ("Translation DLL '%s' loaded and used\n", dllname);
			    } else {
				write_log ("Translation DLL '%s' version mismatch (%d.%d.%d)\n", dllname,
				    HIWORD(vsFileInfo->dwProductVersionMS),
				    LOWORD(vsFileInfo->dwProductVersionMS),
				    HIWORD(vsFileInfo->dwProductVersionLS));
			    }
			}
		    }
		    free( lpFileVersionData );
		}
	    }
	}
	if( result && !success )
	{
	    FreeLibrary( result );
	    result = NULL;
	}
    }

    return result;
}


/* try to load COMDLG32 and DDRAW, initialize csDraw */
int WIN32_InitLibraries( void )
{
    int result = 1;
    /* Determine our processor speed and capabilities */
    if (!figure_processor_speed())
	return 0;
    
    /* Make sure we do an InitCommonControls() to get some advanced controls */
    InitCommonControls();
    
    hRichEdit = LoadLibrary( "RICHED32.DLL" );
    
    hUIDLL = LoadGUI();

    return result;
}

int debuggable (void)
{
    return 0;
}

int needmousehack (void)
{
    return 1;
}

void logging_init( void )
{
    static int started;
    static int first;
    char debugfilename[MAX_DPATH];

    if (first > 1) {
	write_log ("** RESTART **\n");
	return;
    }
    if (first == 1) {
	if (debugfile)
	    fclose (debugfile);
        debugfile = 0;
    }
#ifndef SINGLEFILE
    if( currprefs.win32_logfile ) {
	sprintf( debugfilename, "%swinuaelog.txt", start_path );
	if( !debugfile )
	    debugfile = fopen( debugfilename, "wt" );
    } else if (!first) {
	sprintf( debugfilename, "%swinuaebootlog.txt", start_path );
	if( !debugfile )
	    debugfile = fopen( debugfilename, "wt" );
    }
#endif
    first++;
    write_log ( "%s", VersionStr );
    write_log (" (%s %d.%d %s%s)", os_winnt ? "NT" : "W9X/ME",
	osVersion.dwMajorVersion, osVersion.dwMinorVersion, osVersion.szCSDVersion,
	os_winnt_admin ? " Admin" : "");
    write_log ("\n(c) 1995-2001 Bernd Schmidt   - Core UAE concept and implementation."
	       "\n(c) 1998-2004 Toni Wilen      - Win32 port, core code updates."
	       "\n(c) 1996-2001 Brian King      - Win32 port, Picasso96 RTG, and GUI."
	       "\n(c) 1996-1999 Mathias Ortmann - Win32 port and bsdsocket support."
	       "\n(c) 2000-2001 Bernd Meyer     - JIT engine."
	       "\n(c) 2000-2001 Bernd Roesch    - MIDI input, many fixes."
	       "\nPress F12 to show the Settings Dialog (GUI), Alt-F4 to quit."
	       "\nEnd+F1 changes floppy 0, End+F2 changes floppy 1, etc."
	       "\n");
}

void logging_cleanup( void )
{
    if( debugfile )
        fclose( debugfile );
    debugfile = 0;
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
	p->win32_no_overlay = 0;
	p->win32_ctrl_F11_is_quit = 0;
	p->win32_soundcard = 0;
	p->win32_active_priority = 0;
	p->win32_inactive_priority = 2;
	p->win32_iconified_priority = 3;
	p->win32_notaskbarbutton = 0;
    }
    if (type == 1 || type == 0) {
        p->win32_midioutdev = -2;
	p->win32_midiindev = 0;
    }
}

void target_save_options (FILE *f, struct uae_prefs *p)
{
    cfgfile_write (f, "win32.middle_mouse=%s\n", p->win32_middle_mouse ? "true" : "false");
    cfgfile_write (f, "win32.logfile=%s\n", p->win32_logfile ? "true" : "false");
    cfgfile_write (f, "win32.map_drives=%s\n", p->win32_automount_drives ? "true" : "false" );
    cfgfile_write (f, "win32.serial_port=%s\n", p->use_serial ? p->sername : "none" );
    cfgfile_write (f, "win32.parallel_port=%s\n", p->prtname[0] ? p->prtname : "none" );

    cfgfile_write (f, "win32.active_priority=%d\n", priorities[p->win32_active_priority].value);
    cfgfile_write (f, "win32.inactive_priority=%d\n", priorities[p->win32_inactive_priority].value);
    cfgfile_write (f, "win32.inactive_nosound=%s\n", p->win32_inactive_nosound ? "true" : "false");
    cfgfile_write (f, "win32.inactive_pause=%s\n", p->win32_inactive_pause ? "true" : "false");
    cfgfile_write (f, "win32.iconified_priority=%d\n", priorities[p->win32_iconified_priority].value);
    cfgfile_write (f, "win32.iconified_nosound=%s\n", p->win32_iconified_nosound ? "true" : "false");
    cfgfile_write (f, "win32.iconified_pause=%s\n", p->win32_iconified_pause ? "true" : "false");

    cfgfile_write (f, "win32.ctrl_f11_is_quit=%s\n", p->win32_ctrl_F11_is_quit ? "true" : "false");
    cfgfile_write (f, "win32.midiout_device=%d\n", p->win32_midioutdev );
    cfgfile_write (f, "win32.midiin_device=%d\n", p->win32_midiindev );
    cfgfile_write (f, "win32.no_overlay=%s\n", p->win32_no_overlay ? "true" : "false" );
    cfgfile_write (f, "win32.aspi=%s\n", p->win32_aspi ? "true" : "false" );
    cfgfile_write (f, "win32.soundcard=%d\n", p->win32_soundcard );
    cfgfile_write (f, "win32.cpu_idle=%d\n", p->cpu_idle);
    cfgfile_write (f, "win32.notaskbarbutton=%s\n", p->win32_notaskbarbutton ? "true" : "false");
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
	    || cfgfile_yesno (option, value, "logfile", &p->win32_logfile)
	    || cfgfile_yesno  (option, value, "networking", &p->socket_emu)
	    || cfgfile_yesno (option, value, "no_overlay", &p->win32_no_overlay)
	    || cfgfile_yesno (option, value, "aspi", &p->win32_aspi)
	    || cfgfile_yesno  (option, value, "map_drives", &p->win32_automount_drives)
	    || cfgfile_yesno (option, value, "inactive_pause", &p->win32_inactive_pause)
	    || cfgfile_yesno (option, value, "inactive_nosound", &p->win32_inactive_nosound)
	    || cfgfile_yesno (option, value, "iconified_pause", &p->win32_iconified_pause)
	    || cfgfile_yesno (option, value, "iconified_nosound", &p->win32_iconified_nosound)
	    || cfgfile_yesno  (option, value, "ctrl_f11_is_quit", &p->win32_ctrl_F11_is_quit)
	    || cfgfile_intval (option, value, "midi_device", &p->win32_midioutdev, 1)
	    || cfgfile_intval (option, value, "midiout_device", &p->win32_midioutdev, 1)
	    || cfgfile_intval (option, value, "midiin_device", &p->win32_midiindev, 1)
	    || cfgfile_intval (option, value, "soundcard", &p->win32_soundcard, 1)
	    || cfgfile_string (option, value, "serial_port", &p->sername[0], 256)
	    || cfgfile_string (option, value, "parallel_port", &p->prtname[0], 256)
	    || cfgfile_yesno  (option, value, "notaskbarbutton", &p->win32_notaskbarbutton)
	    || cfgfile_intval  (option, value, "cpu_idle", &p->cpu_idle, 1));

    if (cfgfile_intval (option, value, "active_priority", &v, 1)) {
	p->win32_active_priority = fetchpri (v, 0);
	return 1;
    }
    if (cfgfile_intval (option, value, "activepriority", &v, 1)) {
	p->win32_active_priority = fetchpri (v, 0);
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

    if (p->sername[0] == 'n')
	p->use_serial = 0;
    else
	p->use_serial = 1;

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

void fetch_path (char *name, char *out, int size)
{
    int size2 = size;
    strcpy (out, start_path);
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
    if (out[0] == '\\') { /* relative? */
	strcpy (out, start_path);
        if (hWinUAEKey) {
	    size2 -= strlen (out);
	    RegQueryValueEx (hWinUAEKey, name, 0, NULL, out + strlen (out) - 1, &size2);
	}
    }
}
void set_path (char *name, char *path)
{
    char tmp[MAX_DPATH];
    if (!path) {
	strcpy (tmp, start_path);
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
    } else {
	strcpy (tmp, path);
    }
    if (tmp[strlen (tmp) - 1] != '\\' && tmp[strlen (tmp) - 1] != '/')
	strcat (tmp, "\\");
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
void read_rom_list (int force)
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
	KEY_ALL_ACCESS, NULL, &fkey, &disp);
    if (fkey == NULL)
	return;
    if (disp == REG_CREATED_NEW_KEY || force)
	scan_roms (NULL);
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
    if (parseversion (&vs) < UAEMAJOR)
	return 1;
    if (parseversion (&vs) < UAEMINOR)
	return 1;
    if (parseversion (&vs) < UAESUBREV)
	return 1;
    return 0;
}

static void WIN32_HandleRegistryStuff( void )
{
    RGBFTYPE colortype = RGBFB_NONE;
    DWORD dwType = REG_DWORD;
    DWORD dwDisplayInfoSize = sizeof( colortype );
    DWORD size;
    DWORD disposition;
    char path[MAX_DPATH] = "";
    char version[100];
    HKEY hWinUAEKeyLocal = NULL;
    HKEY fkey;
    int forceroms = 0;
    int updateversion = 1;

    /* Create/Open the hWinUAEKey which points to our config-info */
    if( RegCreateKeyEx( HKEY_CLASSES_ROOT, ".uae", 0, "", REG_OPTION_NON_VOLATILE,
                          KEY_ALL_ACCESS, NULL, &hWinUAEKey, &disposition ) == ERROR_SUCCESS )
    {
	// Regardless of opening the existing key, or creating a new key, we will write the .uae filename-extension
	// commands in.  This way, we're always up to date.

        /* Set our (default) sub-key to point to the "WinUAE" key, which we then create */
        RegSetValueEx( hWinUAEKey, "", 0, REG_SZ, (CONST BYTE *)"WinUAE", strlen( "WinUAE" ) + 1 );

        if( ( RegCreateKeyEx( HKEY_CLASSES_ROOT, "WinUAE\\shell\\Edit\\command", 0, "", REG_OPTION_NON_VOLATILE,
                              KEY_ALL_ACCESS, NULL, &hWinUAEKeyLocal, &disposition ) == ERROR_SUCCESS ) )
        {
            /* Set our (default) sub-key to BE the "WinUAE" command for editing a configuration */
            sprintf( path, "%sWinUAE.exe -f \"%%1\" -s use_gui=yes", start_path );
            RegSetValueEx( hWinUAEKeyLocal, "", 0, REG_SZ, (CONST BYTE *)path, strlen( path ) + 1 );
        }
	RegCloseKey( hWinUAEKeyLocal );

        if( ( RegCreateKeyEx( HKEY_CLASSES_ROOT, "WinUAE\\shell\\Open\\command", 0, "", REG_OPTION_NON_VOLATILE,
                              KEY_ALL_ACCESS, NULL, &hWinUAEKeyLocal, &disposition ) == ERROR_SUCCESS ) )
        {
            /* Set our (default) sub-key to BE the "WinUAE" command for launching a configuration */
            sprintf( path, "%sWinUAE.exe -f \"%%1\"", start_path );
            RegSetValueEx( hWinUAEKeyLocal, "", 0, REG_SZ, (CONST BYTE *)path, strlen( path ) + 1 );
        }
	RegCloseKey( hWinUAEKeyLocal );
    }
    RegCloseKey( hWinUAEKey );
    hWinUAEKey = NULL;

    /* Create/Open the hWinUAEKey which points our config-info */
    if( RegCreateKeyEx( HKEY_CURRENT_USER, "Software\\Arabuusimiehet\\WinUAE", 0, "", REG_OPTION_NON_VOLATILE,
                          KEY_ALL_ACCESS, NULL, &hWinUAEKey, &disposition ) == ERROR_SUCCESS )
    {
        initpath ("FloppyPath", start_path);
        initpath ("KickstartPath", start_path);
        initpath ("hdfPath", start_path);
        initpath ("ConfigurationPath", start_path);
        initpath ("ScreenshotPath", start_path);
        initpath ("StatefilePath", start_path);
        initpath ("SaveimagePath", start_path);
        initpath ("VideoPath", start_path);
        if( disposition == REG_CREATED_NEW_KEY )
        {
            /* Create and initialize all our sub-keys to the default values */
            colortype = 0;
            RegSetValueEx( hWinUAEKey, "DisplayInfo", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
            RegSetValueEx( hWinUAEKey, "xPos", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
            RegSetValueEx( hWinUAEKey, "yPos", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
            RegSetValueEx( hWinUAEKey, "xPosGUI", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
            RegSetValueEx( hWinUAEKey, "yPosGUI", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
        }
	size = sizeof (version);
	if (RegQueryValueEx( hWinUAEKey, "Version", 0, &dwType, (LPBYTE)&version, &size) == ERROR_SUCCESS) {
	    if (checkversion (version))
		forceroms = 1;
	    else
		updateversion = 0;
	} else {
	    forceroms = 1;
	}
	if (updateversion) {
	    // Set this even when we're opening an existing key, so that the version info is always up to date.
	    if (RegSetValueEx( hWinUAEKey, "Version", 0, REG_SZ, (CONST BYTE *)VersionStr, strlen( VersionStr ) + 1 ) != ERROR_SUCCESS)
		forceroms = 0;
	}
        
	RegQueryValueEx( hWinUAEKey, "DisplayInfo", 0, &dwType, (LPBYTE)&colortype, &dwDisplayInfoSize );
	if( colortype == 0 ) /* No color information stored in the registry yet */
	{
	    char szMessage[ 4096 ];
	    char szTitle[ MAX_DPATH ];
	    WIN32GUI_LoadUIString( IDS_GFXCARDCHECK, szMessage, 4096 );
	    WIN32GUI_LoadUIString( IDS_GFXCARDTITLE, szTitle, MAX_DPATH );
		    
	    if( MessageBox( NULL, szMessage, szTitle, MB_YESNO | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND ) == IDYES )
	    {
	        ignore_messages_all++;
	        colortype = WIN32GFX_FigurePixelFormats(0);
	        ignore_messages_all--;
	        RegSetValueEx( hWinUAEKey, "DisplayInfo", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
	    }
	}
	if( colortype ) {
	    /* Set the 16-bit pixel format for the appropriate modes */
	    WIN32GFX_FigurePixelFormats( colortype );
	}
	size = sizeof (quickstart);
 	RegQueryValueEx( hWinUAEKey, "QuickStartMode", 0, &dwType, (LPBYTE)&quickstart, &size );
    }
    fetch_path ("ConfigurationPath", path, sizeof (path));
    path[strlen (path) - 1] = 0;
    CreateDirectory (path, NULL);
    strcat (path, "\\Host");
    CreateDirectory (path, NULL);
    fetch_path ("ConfigurationPath", path, sizeof (path));
    strcat (path, "Hardware");
    CreateDirectory (path, NULL);
    fetch_path ("StatefilePath", path, sizeof (path));
    strcat (path, "default.uss");
    strcpy (savestate_fname, path);
    fkey = read_disk_history ();
    if (fkey)
	RegCloseKey (fkey);
    read_rom_list (forceroms);
}

static void betamessage (void)
{
}

static void init_zlib (void)
{
    HMODULE h = LoadLibrary ("zlib1.dll");
    if (h) {
	is_zlib = 1;
	FreeLibrary(h);
    } else {
	write_log ("zlib1.dll not found, gzip/zip support disabled\n");
    }
}

static int dxdetect (void)
{
    /* believe or not but this is MS supported way of detecting DX8+ */
    HMODULE h = LoadLibrary("D3D8.DLL");
    char szWrongDXVersion[ MAX_DPATH ];
    if (h) {
	FreeLibrary (h);
	return 1;
    }
    WIN32GUI_LoadUIString( IDS_WRONGDXVERSION, szWrongDXVersion, MAX_DPATH );
    pre_gui_message( szWrongDXVersion );
    return 0;
}

int os_winnt, os_winnt_admin;

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
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &hToken )) {
	write_log ( "OpenProcessToken Error %u\n", GetLastError() );
	return FALSE;
    }

    // Call GetTokenInformation to get the buffer size.
    if(!GetTokenInformation(hToken, TokenGroups, NULL, dwSize, &dwSize)) {
	dwResult = GetLastError();
	if( dwResult != ERROR_INSUFFICIENT_BUFFER ) {
	    write_log( "GetTokenInformation Error %u\n", dwResult );
	    return FALSE;
	}
    }

    // Allocate the buffer.
    pGroupInfo = (PTOKEN_GROUPS) GlobalAlloc( GPTR, dwSize );

    // Call GetTokenInformation again to get the group information.
    if(! GetTokenInformation(hToken, TokenGroups, pGroupInfo, dwSize, &dwSize ) ) {
	write_log ( "GetTokenInformation Error %u\n", GetLastError() );
	return FALSE;
    }

    // Create a SID for the BUILTIN\Administrators group.
    if(! AllocateAndInitializeSid( &SIDAuth, 2,
                 SECURITY_BUILTIN_DOMAIN_RID,
                 DOMAIN_ALIAS_RID_ADMINS,
                 0, 0, 0, 0, 0, 0,
                 &pSID) ) {
	write_log( "AllocateAndInitializeSid Error %u\n", GetLastError() );
	return FALSE;
   }

    // Loop through the group SIDs looking for the administrator SID.
    for(i=0; i<pGroupInfo->GroupCount; i++) {
	if ( EqualSid(pSID, pGroupInfo->Groups[i].Sid) )
	    isadmin = 1;
    }
    
    if (pSID)
	FreeSid(pSID);
    if ( pGroupInfo )
	GlobalFree( pGroupInfo );
    return isadmin;
}

static int osdetect (void)
{
    os_winnt = 0;
    os_winnt_admin = 0;

    osVersion.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );
    if( GetVersionEx( &osVersion ) )
    {
	if( ( osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT ) &&
	    ( osVersion.dwMajorVersion <= 4 ) )
	{
	    /* WinUAE not supported on this version of Windows... */
	    char szWrongOSVersion[ MAX_DPATH ];
	    WIN32GUI_LoadUIString( IDS_WRONGOSVERSION, szWrongOSVersion, MAX_DPATH );
	    pre_gui_message( szWrongOSVersion );
	    return FALSE;
	}
	if (osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT)
	    os_winnt = 1;
    }

    if (!os_winnt) {
	return 1;
    }
    os_winnt_admin = isadminpriv ();
    return 1;
}

static int PASCAL WinMain2 (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
		    int nCmdShow)
{
    char *posn;
    HANDLE hMutex;
    char **argv;
    int argc;
    int i;
    int multi_display = 1;

#ifdef __GNUC__
    __asm__ ("leal -2300*1024(%%esp),%0" : "=r" (win32_stackbase) :);
#else
__asm{
    mov eax,esp
    sub eax,2300*1024
    mov win32_stackbase,eax
 }
#endif

#ifdef _DEBUG
    {
	int tmp = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
	tmp |= _CRTDBG_CHECK_ALWAYS_DF;
	tmp |= _CRTDBG_CHECK_CRT_DF;
	//tmp |= _CRTDBG_DELAY_FREE_MEM_DF;
	_CrtSetDbgFlag(tmp);
    }
#endif

    if (!osdetect())
	return 0;
    if (!dxdetect())
	return 0;

    hInst = hInstance;
    hMutex = CreateMutex( NULL, FALSE, "WinUAE Instantiated" ); // To tell the installer we're running
#ifdef AVIOUTPUT
    AVIOutput_Initialize();
#endif
    init_zlib ();

#ifdef __MINGW32__
    argc = _argc; argv = _argv;
#else
    argc = __argc; argv = __argv;
#endif
    for (i = 1; i < argc; i++) {
	char *arg = argv[i];
	if (!strcmp (arg, "-log")) console_logging = 1;
#ifdef FILESYS
	if (!strcmp (arg, "-rdbdump")) do_rdbdump = 1;
	if (!strcmp (arg, "-disableharddrivesafetycheck")) harddrive_dangerous = 0x1234dead;
	if (!strcmp (arg, "-noaspifiltering")) aspi_allow_all = 1;
#endif
	if (!strcmp (arg, "-dsaudiomix")) dsound_hardware_mixing = 1;
	if (!strcmp (arg, "-nordtsc")) no_rdtsc = 1;
	if (!strcmp (arg, "-forcerdtsc")) no_rdtsc = -1;
	if (!strcmp (arg, "-norawinput")) no_rawinput = 1;
	if (!strcmp (arg, "-scsilog")) log_scsi = 1;
	if (!strcmp (arg, "-nomultidisplay")) multi_display = 0;
    }
#if 0
    argv = 0;
    argv[0] = 0;
#endif
    /* Get our executable's root-path */
    if ((start_path = xmalloc (MAX_DPATH)))
    {
	GetModuleFileName( NULL, start_path, MAX_DPATH );
	if((posn = strrchr (start_path, '\\')))
	    posn[1] = 0;
	sprintf (help_file, "%sWinUAE.chm", start_path );
	sprintf( VersionStr, "WinUAE %d.%d.%d%s", UAEMAJOR, UAEMINOR, UAESUBREV, WINUAEBETA ? WINUAEBETASTR : "" );

	logging_init ();

	if( WIN32_RegisterClasses() && WIN32_InitLibraries() && DirectDraw_Start(NULL) )
	{
	    struct foo {
		DEVMODE actual_devmode;
		char overrun[8];
	    } devmode;

	    DWORD i = 0;

	    DirectDraw_Release ();
	    write_log ("Enumerating display devices.. \n");
	    enumeratedisplays (multi_display);
	    write_log ("Sorting devices and modes..\n");
	    sortdisplays ();
	    write_log ("done\n");
	    
	    memset( &devmode, 0, sizeof(DEVMODE) + 8 );
	    devmode.actual_devmode.dmSize = sizeof(DEVMODE);
	    devmode.actual_devmode.dmDriverExtra = 8;
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
	    if( EnumDisplaySettings( NULL, ENUM_CURRENT_SETTINGS, (LPDEVMODE)&devmode ) )
	    {
		default_freq = devmode.actual_devmode.dmDisplayFrequency;
		if( default_freq >= 70 )
		    default_freq = 70;
		else
		    default_freq = 60;
	    }

	    WIN32_HandleRegistryStuff();
	    WIN32_InitHtmlHelp();
	    DirectDraw_Release();
	    betamessage ();
	    keyboard_settrans ();
#ifdef PARALLEL_PORT
	    paraport_mask = paraport_init ();
#endif
	    real_main (argc, argv);
	}
	free (start_path);
    }
	
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
    flushprinter ();
    closeprinter ();
#endif
    WIN32_CleanupLibraries();
    _fcloseall();
    if( hWinUAEKey )
	RegCloseKey( hWinUAEKey );
    CloseHandle( hMutex );
#ifdef _DEBUG
    // show memory leaks
    //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    return FALSE;
}

int execute_command (char *cmd)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    memset (&si, 0, sizeof (si));
    si.cb = sizeof (si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if( CreateProcess( NULL, cmd, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi ) )  {
	WaitForSingleObject( pi.hProcess, INFINITE );
	return 1;
    }
    return 0;
}

struct threadpriorities priorities[] = {
    { "Above Normal", THREAD_PRIORITY_ABOVE_NORMAL, ABOVE_NORMAL_PRIORITY_CLASS },
    { "Normal", THREAD_PRIORITY_NORMAL, NORMAL_PRIORITY_CLASS },
    { "Below Normal", THREAD_PRIORITY_BELOW_NORMAL, BELOW_NORMAL_PRIORITY_CLASS },
    { "Low", THREAD_PRIORITY_LOWEST, IDLE_PRIORITY_CLASS },
    { 0 }
};

static int drvsampleres[] = {
    IDR_DRIVE_CLICK_A500_1, IDR_DRIVE_SPIN_A500_1, IDR_DRIVE_SPINND_A500_1,
    IDR_DRIVE_STARTUP_A500_1, IDR_DRIVE_SNATCH_A500_1
};
#include "driveclick.h"
int driveclick_loadresource (struct drvsample *s, int drivetype)
{
    int i, ok;

    ok = 1;
    for (i = 0; i < DS_END; i++) {
        HRSRC res = FindResource(NULL, MAKEINTRESOURCE(drvsampleres[i]), "WAVE");
	if (res != 0) {
	    HANDLE h = LoadResource(NULL, res);
	    int len = SizeofResource(NULL, res);
	    uae_u8 *p = LockResource(h);
	    s->p = decodewav (p, &len);
	    s->len = len;
	} else {
	    ok = 0;
	}
	s++;
    }
    return ok;
}

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

static LONG WINAPI ExceptionFilter( struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
    LONG lRet = EXCEPTION_CONTINUE_SEARCH;
    PEXCEPTION_RECORD er = pExceptionPointers->ExceptionRecord;
    PCONTEXT ctx = pExceptionPointers->ContextRecord;

    /* Check possible access violation in 68010+/compatible mode disabled if PC points to non-existing memory */
    if (ec == EXCEPTION_ACCESS_VIOLATION && !er->ExceptionFlags &&
	er->NumberParameters >= 2 && !er->ExceptionInformation[0] && regs.pc_p) {
	    void *p = (void*)er->ExceptionInformation[1];
	    if (p >= (void*)regs.pc_p && p < (void*)(regs.pc_p + 32)) {
		int got = 0;
		uaecptr opc = m68k_getpc();
		void *ps = get_real_address (0);
		m68k_dumpstate (0, 0);
		efix (&ctx->Eax, p, ps, &got);
		efix (&ctx->Ebx, p, ps, &got);
		efix (&ctx->Ecx, p, ps, &got);
		efix (&ctx->Edx, p, ps, &got);
		efix (&ctx->Esi, p, ps, &got);
		efix (&ctx->Edi, p, ps, &got);
#if 0
		gui_message ("Experimental access violation trap code activated\n"
		    "Trying to prevent WinUAE crash.\nFix %s (68KPC=%08.8X HOSTADDR=%p)\n",
			got ? "ok" : "failed", m68k_getpc(), p);
#endif
		write_log ("Access violation! (68KPC=%08.8X HOSTADDR=%p)\n", m68k_getpc(), p);
		if (got == 0) {
		    write_log ("failed to find and fix the problem (%p). crashing..\n", p);
		} else {
		    m68k_setpc (0);
		    exception2 (opc, (uaecptr)p);
		    lRet = EXCEPTION_CONTINUE_EXECUTION;
		}
	    }
    }
#ifndef _DEBUG
    if (lRet == EXCEPTION_CONTINUE_SEARCH) {
	char path[MAX_DPATH];
	char path2[MAX_DPATH];
	char msg[1024];
	char *p;
	HMODULE dll = NULL;
	struct tm when;
	__time64_t now;
	
	_time64(&now);
	when = *_localtime64(&now);
	if (GetModuleFileName(NULL, path, MAX_DPATH)) {
	    char *slash = strrchr (path, '\\');
	    strcpy (path2, path);
	    if (slash) {
		strcpy (slash + 1, "DBGHELP.DLL");
		dll = LoadLibrary (path);
	    }
	    slash = strrchr (path2, '\\');
	    if (slash)
		p = slash + 1;
	    else
		p = path2;
	    sprintf (p, "winuae_%d%02d%02d_%02d%02d%02d.dmp",
		when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);
	    if (dll == NULL)
		dll = LoadLibrary("DBGHELP.DLL");
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
			if (!isfullscreen ()) {
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
    menu = LoadMenu (hInst, MAKEINTRESOURCE (IDM_SYSTRAY));
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
    if (!isfullscreen ())
	SetForegroundWindow (hwnd);
    TrackPopupMenu (menu2, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, NULL);
    PostMessage (hwnd, WM_NULL, 0, 0);
    DestroyMenu (menu);
    winuae_active (hwnd, FALSE);
}

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
		    int nCmdShow)
{
    __try {
	WinMain2 (hInstance, hPrevInstance, lpCmdLine, nCmdShow);
        } __except(ExceptionFilter(GetExceptionInformation(), GetExceptionCode()))
    {
    }
    return FALSE;
}


