
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

HWND hMainWnd;
HINSTANCE hInst;
int mouseactive, focus;
int os_winnt, os_winnt_admin, useqpc, cpu_mmx, pause_emulation;
char *start_path;

static int mm_timerres;
static int timermode, timeon;
static HANDLE timehandle;

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

    if (mm_timerres <= 0 || timermode) {
	Sleep (ms);
	return;
    }
    TimerEvent = timeSetEvent (ms, 0, timehandle, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    if (!TimerEvent) {
	Sleep (ms);
    } else {
	WaitForSingleObject (timehandle, ms);
	ResetEvent (timehandle);
	timeKillEvent (TimerEvent);
    }
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
    int i, ratecnt = 6;
    LARGE_INTEGER freq;
    int qpc_avail = 0;
    int mmx = 0;

    rpt_available = 1;
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
	write_log ("CLOCKFREQ: QPF %.2fMHz\n", freq.QuadPart / 1000000.0);
	qpfrate = freq.QuadPart;
	 /* we don't want 32-bit overflow */
	if (qpfrate > 100000000) {
	    qpfrate >>= 6;
	    qpc_avail = -1;
	}
    } else {
	write_log ("CLOCKREQ: QPF not supported\n");
    }

    if (!rpt_available && !qpc_avail)
	return 0;

    init_mmtimer();
    SetThreadPriority ( GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    sleep_millis (100);
    dummythread_die = -1;

    if (qpc_avail || rpt_available)  {

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
	    write_log ("CLOCKFREQ: RDTSC %.2fMHz (busy) / %.2fMHz (idle)\n",
		clockrate / 1000000.0, clockrateidle / 1000000.0);
	    clkdiv = (double)clockrate / (double)clockrateidle;
	    clockrate >>= 6;
	    clockrate1000 = clockrate / 1000.0;
	}
	if (clkdiv <= 0.95 || clkdiv >= 1.05 || !rpt_available) {
	    if (rpt_available)
		write_log ("CLOCKFREQ: CPU throttling detected, using QPF instead of RDTSC\n");
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
	    write_log ("\n");
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
	write_log ("\n");
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

int needmousehack (void)
{
    return 0;
}

void gui_message (const char *format,...)
{
}

void gui_led (int led, int on)
{
}

void gui_fps (int fps)
{
}

void gui_hd_led (int led)
{
}

void gui_cd_led (int led)
{
}

void gui_unlock (void)
{
}

void gui_lock (void)
{
}

int gui_update (void)
{
    return 0;
}

char *DXError (HRESULT ddrval)
{
    return "";
}

void setmouseactive (int active)
{
}

void minimizewindow (void)
{
}

int isfullscreen (void)
{
    return 0;
}

void fullscreentoggle (void)
{
}

void disablecapture (void)
{
}

void WIN32GUI_DisplayGUI (int shortcut)
{
}

void flush_block (int a, int b)
{
}

void flush_screen (int a, int b)
{
}

void flush_line (int lineno)
{
}

void flush_clear_screen (void)
{
}

int lockscr (void)
{
    return 1;
}

void unlockscr (void)
{
}

void machdep_init (void)
{
}

void handle_events (void)
{
}

int debuggable (void)
{
    return 0;
}

int graphics_init (void)
{
    return 1;
}

int graphics_setup (void)
{
    return 1;
}

void graphics_leave (void)
{
}

extern FILE *debugfile;
char VersionStr[256];
static OSVERSIONINFO osVersion;
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
    if( currprefs.win32_logfile ) {
	sprintf( debugfilename, "%s\\winuaelog.txt", start_path );
	if( !debugfile )
	    debugfile = fopen( debugfilename, "wt" );
    } else if (!first) {
	sprintf( debugfilename, "%s\\winuaebootlog.txt", start_path );
	if( !debugfile )
	    debugfile = fopen( debugfilename, "wt" );
    }
    first++;
    write_log ( "%s", VersionStr );
    write_log (" (OS: %s %d.%d%s)", os_winnt ? "NT" : "W9X/ME", osVersion.dwMajorVersion, osVersion.dwMinorVersion, os_winnt_admin ? " Administrator privileges" : "");
    write_log ("\n(c) 1995-2001 Bernd Schmidt   - Core UAE concept and implementation."
	       "\n(c) 1998-2003 Toni Wilen      - Win32 port, core code updates."
	       "\n(c) 1996-2001 Brian King      - Win32 port, Picasso96 RTG, and GUI."
	       "\n(c) 1996-1999 Mathias Ortmann - Win32 port and bsdsocket support."
	       "\n(c) 2000-2001 Bernd Meyer     - JIT engine."
	       "\n(c) 2000-2001 Bernd Roesch    - MIDI input, many fixes."
	       "\nPress F12 to show the Settings Dialog (GUI), Alt-F4 to quit."
	       "\nEnd+F1 changes floppy 0, End+F2 changes floppy 1, etc."
	       "\n");
}

void target_save_options (FILE *f, struct uae_prefs *p)
{
}

int target_parse_option (struct uae_prefs *p, char *option, char *value)
{
    return 0;
}

int gui_init (void)
{
    return 1;
}

void gui_exit (void)
{
}

void gui_filename (int num, const char *name)
{
}

void setup_brkhandler (void)
{
}

int check_prefs_changed_gfx (void)
{
    return 0;
}

void screenshot (int mode)
{
}

int _stdcall WinMain (HINSTANCE hInstance, HINSTANCE prev, LPSTR cmd, int show)
{
    char **argv, *posn;
    int argc;

    hInst = hInstance;
    argc = __argc; argv = __argv;
    start_path = xmalloc( MAX_DPATH );
    GetModuleFileName( NULL, start_path, MAX_DPATH );
    if( ( posn = strrchr( start_path, '\\' ) ) )
	*posn = 0;
    real_main (argc, argv);
    if (mm_timerres && timermode == 0)
	timeend ();
    free (start_path);
    return 0;
}


















