
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include <windows.h>

#include "cloanto/RetroPlatformGuestIPC.h"
#include "cloanto/RetroPlatformIPC.h"

#include "sysconfig.h"
#include "sysdeps.h"
#include "rp.h"
#include "options.h"
#include "uae.h"
#include "inputdevice.h"
#include "audio.h"
#include "sound.h"
#include "disk.h"
#include "xwin.h"
#include "custom.h"
#include "memory.h"
#include "newcpu.h"
#include "picasso96_win.h"
#include "win32.h"
#include "win32gfx.h"

static int initialized;
static RPGUESTINFO guestinfo;

char *rp_param = NULL;
int rp_rpescapekey = 0x01;
int rp_rpescapeholdtime = 600;
int rp_screenmode = 0;
int rp_inputmode = 0;
int log_rp = 1;

static int default_width, default_height;
static int hwndset;
static int minimized;


static char *ua (const WCHAR *s)
{
    char *d;
    int len = WideCharToMultiByte (CP_ACP, 0, s, -1, NULL, 0, 0, FALSE);
    if (!len)
	return my_strdup ("");
    d = xmalloc (len + 1);
    WideCharToMultiByte (CP_ACP, 0, s, -1, d, len, 0, FALSE);
    return d;
}

static const char *getmsg (int msg)
{
    switch (msg)
    {
	case RPIPCGM_REGISTER: return "RPIPCGM_REGISTER";
	case RPIPCGM_FEATURES: return "RPIPCGM_FEATURES";
	case RPIPCGM_CLOSED: return "RPIPCGM_CLOSED";
	case RPIPCGM_ACTIVATED: return "RPIPCGM_ACTIVATED";
	case RPIPCGM_DEACTIVATED: return "RPIPCGM_DEACTIVATED";
	case RPIPCGM_ZORDER: return "RPIPCGM_ZORDER";
	case RPIPCGM_MINIMIZED: return "RPIPCGM_MINIMIZED";
	case RPIPCGM_RESTORED: return "RPIPCGM_RESTORED";
	case RPIPCGM_MOVED: return "RPIPCGM_MOVED";
	case RPIPCGM_SCREENMODE: return "RPIPCGM_SCREENMODE";
	case RPIPCGM_POWERLED: return "RPIPCGM_POWERLED";
	case RPIPCGM_DEVICES: return "RPIPCGM_DEVICES";
	case RPIPCGM_DEVICEACTIVITY: return "RPIPCGM_DEVICEACTIVITY";
	case RPIPCGM_MOUSECAPTURE: return "RPIPCGM_MOUSECAPTURE";
	case RPIPCGM_HOSTAPIVERSION: return "RPIPCGM_HOSTAPIVERSION";
	case RPIPCGM_PAUSE: return "RPIPCGM_PAUSE";
	case RPIPCGM_DEVICEIMAGE: return "RPIPCGM_DEVICEIMAGE";
	case RPIPCGM_TURBO: return "RPIPCGM_TURBO";
	case RPIPCGM_INPUTMODE: return "RPIPCGM_INPUTMODE";
	case RPIPCGM_VOLUME: return "RPIPCGM_VOLUME";

	case RPIPCHM_CLOSE: return "RPIPCHM_CLOSE";
	case RPIPCHM_MINIMIZE: return "RPIPCHM_MINIMIZE";
	case RPIPCHM_SCREENMODE: return "RPIPCHM_SCREENMODE";
	case RPIPCHM_SCREENCAPTURE: return "RPIPCHM_SCREENCAPTURE";
	case RPIPCHM_PAUSE: return "RPIPCHM_PAUSE";
	case RPIPCHM_DEVICEIMAGE: return "RPIPCHM_DEVICEIMAGE";
	case RPIPCHM_RESET: return "RPIPCHM_RESET";
	case RPIPCHM_TURBO: return "RPIPCHM_TURBO";
	case RPIPCHM_INPUTMODE: return "RPIPCHM_INPUTMODE";
	case RPIPCHM_VOLUME: return "RPIPCHM_VOLUME";
	case RPIPCHM_EVENT: return "RPIPCHM_EVENT";
	case RPIPCHM_ESCAPEKEY: return "RPIPCHM_ESCAPEKEY";
	case RPIPCHM_MOUSECAPTURE: return "RPIPCHM_MOUSECAPTURE";

	default: return "UNKNOWN";
    }
}

BOOL RPSendMessagex(UINT uMessage, WPARAM wParam, LPARAM lParam,
                   LPCVOID pData, DWORD dwDataSize, const RPGUESTINFO *pInfo, LRESULT *plResult)
{
    BOOL v;
    
    if (!pInfo) {
	write_log ("RPSEND: pInfo == NULL!\n");
        return FALSE;
    }
    if (!pInfo->hHostMessageWindow) {
	write_log ("RPSEND: pInfo->hHostMessageWindow == NULL!\n");
        return FALSE;
    }
    v = RPSendMessage (uMessage, wParam, lParam, pData, dwDataSize, pInfo, plResult);
    if (log_rp) {
	write_log ("RPSEND(%s [%d], %08x, %08x, %08x, %d\n",
	    getmsg (uMessage), uMessage - WM_APP, wParam, lParam, pData, dwDataSize);
	if (v == FALSE)
	    write_log("ERROR %d\n", GetLastError ());
    }
    return v;
}

static int winok(void)
{
    if (!initialized)
	return 0;
    if (!hwndset)
	return 0;
    return 1;
}

static int get_x (void)
{
    int res = currprefs.gfx_resolution;

    if (currprefs.gfx_afullscreen)
	return RP_SCREENMODE_FULLSCREEN;
    if (res == 0)
	return RP_SCREENMODE_1X;
    if (res == 1)
	return RP_SCREENMODE_2X;
    return RP_SCREENMODE_4X;
}

static LRESULT CALLBACK RPHostMsgFunction(UINT uMessage, WPARAM wParam, LPARAM lParam,
                                          LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam)
{
    if (log_rp)
	write_log ("RPFUNC(%s [%d], %08x, %08x, %08x, %d, %08x)\n",
	    getmsg (uMessage), uMessage - WM_APP, wParam, lParam, pData, dwDataSize, lMsgFunctionParam);

    switch (uMessage)
    {
	default:
	    write_log ("RP: Unknown or unsupported command %x\n", uMessage);
	break;
	case RPIPCHM_CLOSE:
	    uae_quit ();
	return TRUE;
	case RPIPCHM_RESET:
	    uae_reset (wParam == RP_RESET_SOFT ? 0 : -1);
	return TRUE;
	case RPIPCHM_TURBO:
	    warpmode (lParam);
	return TRUE;
	case RPIPCHM_PAUSE:
	    pausemode (lParam);
	return TRUE;
	case RPIPCHM_VOLUME:
	    currprefs.sound_volume = changed_prefs.sound_volume = wParam;
	    set_volume (currprefs.sound_volume, 0);
	return TRUE;
	case RPIPCHM_SCREENCAPTURE:
	    screenshot (1, 1);
	return TRUE;
	case RPIPCHM_MINIMIZE:
	    minimized = 1;
	    if (ShowWindow (hAmigaWnd, SW_MINIMIZE))
		return TRUE;
	break;
	case RPIPCHM_ESCAPEKEY:
	    rp_rpescapekey = wParam;
	    rp_rpescapeholdtime = lParam;
        return TRUE;
	case RPIPCHM_MOUSECAPTURE:
	    if (wParam)
		setmouseactive (1);
	    else
		setmouseactive (0);
	return TRUE;
	case RPIPCHM_DEVICEIMAGE:
	{
	    RPDEVICEIMAGE *di = (RPDEVICEIMAGE*)pData;
	    if (di->btDeviceCategory == RP_DEVICE_FLOPPY) {
		char *fn = ua (di->szImageFile);
		disk_insert (di->btDeviceNumber, fn);
		xfree (fn);
	    }
	    return TRUE;
	}
	case RPIPCHM_SCREENMODE:
	{
	    int res = (BYTE)wParam;
	    minimized = 0;
	    changed_prefs.gfx_afullscreen = 0;
	    if (res >= RP_SCREENMODE_FULLSCREEN) {
		res = 1;
		changed_prefs.gfx_afullscreen = 1;
	    }
	    changed_prefs.gfx_resolution = res;
	    if (res == 0)
		changed_prefs.gfx_linedbl = 0;
	    else
		changed_prefs.gfx_linedbl = 1;
	    res = 1 << res;
	    changed_prefs.gfx_size_win.width = default_width * res;
	    changed_prefs.gfx_size_win.height = default_height * res;
	    updatewinfsmode (&changed_prefs);
	    WIN32GFX_DisplayChangeRequested();
	    hwndset = 0;
	    return (LRESULT)INVALID_HANDLE_VALUE;
	}
	case RPIPCHM_EVENT:
	{
	    char out[256];
	    char *s = ua ((WCHAR*)pData);
	    int idx = -1;
	    for (;;) {
		int ret;
		out[0] = 0;
		ret = cfgfile_modify (idx++, s, strlen (s), out, sizeof out);
		if (ret >= 0)
		    break;
	    }
	    xfree (s);
	    return TRUE;
	}


    }
    return FALSE;
}

HRESULT rp_init (void)
{
    HRESULT hr;

    hr = RPInitializeGuest(&guestinfo, hInst, rp_param, RPHostMsgFunction, 0);
    if (SUCCEEDED (hr)) {
	initialized = TRUE;
	write_log ("rp_init('%s') succeeded\n", rp_param);
    } else {
	write_log ("rp_init('%s') failed, error code %08x\n", rp_param, hr);
    }
    xfree (rp_param);
    rp_param = NULL;
    return hr;
}

void rp_free (void)
{
    if (!initialized)
	return;
    initialized = 0;
    RPSendMessagex(RPIPCGM_CLOSED, 0, 0, NULL, 0, &guestinfo, NULL);
    RPUninitializeGuest (&guestinfo);
}

void rp_fixup_options (struct uae_prefs *p)
{
    int i, v;
    int res;

    if (!initialized)
	return;
    write_log ("rp_fixup_options(rpescapekey=%d,rpescapeholdtime=%d,screenmode=%d,inputmode=%d)\n",
	rp_rpescapekey, rp_rpescapeholdtime, rp_screenmode, rp_inputmode);

    res = 1 << currprefs.gfx_resolution;
    default_width = currprefs.gfx_size_win.width / res;
    default_height = currprefs.gfx_size_win.height / res;

    p->win32_borderless = 1;
    p->gfx_afullscreen = p->gfx_pfullscreen = 0;
    res = rp_screenmode;
    if (res >= RP_SCREENMODE_FULLSCREEN) {
	p->gfx_afullscreen = 1;
	res = 1;
    } else {
        int xres = 1 << res;
	p->gfx_size_win.width = default_width * xres;
	p->gfx_size_win.height = default_height * xres;
    }
    p->gfx_resolution = res;
    if (res == 0)
	p->gfx_linedbl = 0;
    else
	p->gfx_linedbl = 1;

    RPSendMessagex(RPIPCGM_FEATURES,
	RP_FEATURE_POWERLED | RP_FEATURE_SCREEN1X | RP_FEATURE_SCREEN2X | RP_FEATURE_FULLSCREEN |
	RP_FEATURE_PAUSE | RP_FEATURE_TURBO | RP_FEATURE_INPUTMODE | RP_FEATURE_VOLUME,
	0, NULL, 0, &guestinfo, NULL);
    /* floppy drives */
    v = 0;
    for (i = 0; i < 4; i++) {
	if (p->dfxtype[i] >= 0)
	    v |= 1 << i;
    }
    RPSendMessagex(RPIPCGM_DEVICES, RP_DEVICE_FLOPPY, v, NULL, 0, &guestinfo, NULL);
}

void rp_update_leds (int led, int onoff)
{
    if (!initialized)
	return;
    if (led < 0 || led > 4)
	return;
    switch (led)
    {
	case 0:
        RPSendMessage(RPIPCGM_POWERLED, onoff ? 100 : 0, 0, NULL, 0, &guestinfo, NULL);
	break;
	case 1:
	case 2:
	case 3:
	case 4:
        RPSendMessage(RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_FLOPPY, led - 1), onoff ? -1 : 0, NULL, 0, &guestinfo, NULL);
	break;
    }
}

void rp_update_status (struct uae_prefs *p)
{
    if (!initialized)
	return;
    RPSendMessagex(RPIPCGM_VOLUME, (WPARAM)p->sound_volume, 0, NULL, 0, &guestinfo, NULL);
}

void rp_mousecapture (int captured)
{
    if (!winok())
	return;
    RPSendMessagex(RPIPCGM_MOUSECAPTURE, captured, 0, NULL, 0, &guestinfo, NULL);
}

void rp_activate (int active, LPARAM lParam)
{
    if (!initialized)
	return;
    RPSendMessagex(active ? RPIPCGM_ACTIVATED : RPIPCGM_DEACTIVATED, 0, lParam, NULL, 0, &guestinfo, NULL);
}

void rp_minimize (int minimize)
{
    if (!initialized)
	return;
    RPSendMessagex(minimize ? RPIPCGM_MINIMIZED : RPIPCGM_RESTORED, 0, 0, NULL, 0, &guestinfo, NULL);
}

void rp_turbo (int active)
{
    if (!initialized)
	return;
    RPSendMessagex(RPIPCGM_TURBO, RP_TURBO_CPU, active, NULL, 0, &guestinfo, NULL);
}

void rp_set_hwnd (HWND hWnd)
{
    int rx;
    if (!initialized)
	return;
    rx = get_x ();
    hwndset = 1;
    RPSendMessagex(RPIPCGM_SCREENMODE, rx, (LPARAM)hWnd, NULL, 0, &guestinfo, NULL); 
}

void rp_moved (int zorder)
{
    if (!winok())
	return;
    RPSendMessagex(zorder ? RPIPCGM_ZORDER : RPIPCGM_MOVED, 0, 0, NULL, 0, &guestinfo, NULL);
}

static uae_u64 esctime;
static int ignorerelease;

static uae_u64 gett (void)
{
    SYSTEMTIME st;
    FILETIME ft;
    ULARGE_INTEGER li;

    GetSystemTime (&st);
    if (!SystemTimeToFileTime (&st, &ft))
	return 0;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart / 10000;
}

void rp_hsync (void)
{
    uae_u64 t;
    static int cnt;

    if (!initialized)
	return;
    cnt--;
    if (cnt > 0)
	return;
    cnt = 100;
    if (!esctime)
	return;
    t = gett ();
    if (t >= esctime) {
	RPSendMessagex(RPIPCGM_ESCAPED, 0, 0, NULL, 0, &guestinfo, NULL);
	ignorerelease = 1;
	esctime = 0;
    }
}

int rp_checkesc (int scancode, uae_u8 *codes, int pressed, int num)
{
    uae_u64 t;

    if (!initialized)
	goto end;
    if (scancode != rp_rpescapekey)
	goto end;
    if (ignorerelease && !pressed) {
	ignorerelease = 0;
	goto end;
    }
    t = gett();
    if (!t)
	goto end;
    if (pressed) {
	esctime = t + rp_rpescapeholdtime;
	return 1;
    }
    my_kbd_handler (num, scancode, 1);
    my_kbd_handler (num, scancode, 0);
    ignorerelease = 0;
    esctime = 0;
    return 1;
end:
    esctime = 0;
    return 0;
}

int rp_isactive (void)
{
    return initialized;
}
