
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
#include "filesys.h"

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
static DWORD hd_mask, cd_mask;
static int mousecapture, mousemagic;

static int recursive_device;

static void outhex (const uae_u8 *s)
{
    for (;;) {
	write_log ("%02X%02X ", s[0], s[1]);
	if (s[0] == 0 && s[1] == 0)
	    break;
	s += 2;
    }
    write_log ("\n");
}

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
static WCHAR *au (const char *s)
{
    WCHAR *d;
    int len = MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, s, -1, NULL, 0);
    if (!len)
	return xcalloc (2, 1);
    d = xmalloc ((len + 1) * sizeof (WCHAR));
    MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, s, -1, d, len);
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
	case RPIPCGM_PARENT: return "RPIPCGM_PARENT";
#if 0
	case RPIPCGM_ZORDER: return "RPIPCGM_ZORDER";
	case RPIPCGM_MINIMIZED: return "RPIPCGM_MINIMIZED";
	case RPIPCGM_RESTORED: return "RPIPCGM_RESTORED";
	case RPIPCGM_MOVED: return "RPIPCGM_MOVED";
	case RPIPCGM_DEVICEIMAGE: return "RPIPCGM_DEVICEIMAGE";
	case RPIPCGM_INPUTMODE: return "RPIPCGM_INPUTMODE";
	case RPIPCHM_MINIMIZE: return "RPIPCHM_MINIMIZE";
	case RPIPCHM_DEVICEIMAGE: return "RPIPCHM_DEVICEIMAGE";
	case RPIPCHM_INPUTMODE: return "RPIPCHM_INPUTMODE";
#endif
	case RPIPCGM_SCREENMODE: return "RPIPCGM_SCREENMODE";
	case RPIPCGM_POWERLED: return "RPIPCGM_POWERLED";
	case RPIPCGM_DEVICES: return "RPIPCGM_DEVICES";
	case RPIPCGM_DEVICEACTIVITY: return "RPIPCGM_DEVICEACTIVITY";
	case RPIPCGM_MOUSECAPTURE: return "RPIPCGM_MOUSECAPTURE";
	case RPIPCGM_HOSTAPIVERSION: return "RPIPCGM_HOSTAPIVERSION";
	case RPIPCGM_PAUSE: return "RPIPCGM_PAUSE";
	case RPIPCGM_TURBO: return "RPIPCGM_TURBO";
	case RPIPCGM_VOLUME: return "RPIPCGM_VOLUME";
	case RPIPCGM_DEVICECONTENT: return "RPIPCGM_DEVICECONTENT";

	case RPIPCHM_CLOSE: return "RPIPCHM_CLOSE";
	case RPIPCHM_SCREENMODE: return "RPIPCHM_SCREENMODE";
	case RPIPCHM_SCREENCAPTURE: return "RPIPCHM_SCREENCAPTURE";
	case RPIPCHM_PAUSE: return "RPIPCHM_PAUSE";
	case RPIPCHM_RESET: return "RPIPCHM_RESET";
	case RPIPCHM_TURBO: return "RPIPCHM_TURBO";
	case RPIPCHM_VOLUME: return "RPIPCHM_VOLUME";
	case RPIPCHM_EVENT: return "RPIPCHM_EVENT";
	case RPIPCHM_ESCAPEKEY: return "RPIPCHM_ESCAPEKEY";
	case RPIPCHM_MOUSECAPTURE: return "RPIPCHM_MOUSECAPTURE";
	case RPIPCHM_DEVICECONTENT: return "RPIPCHM_DEVICECONTENT";
	case RPIPCHM_PING: return "RPIPCHM_PING";
	case RPIPCHM_SCREENCLIP: return "RPIPCHM_SCREENCLIP";

	default: return "UNKNOWN";
    }
}

static int port_insert (int num, const char *name)
{
    char tmp1[100], tmp2[200];
    int i, type;

    if (num < 0 || num > 1)
	return FALSE;
    if (strlen (name) == 0) {
	inputdevice_joyport_config (&changed_prefs, "none", num, 0);
	return TRUE;
    }
    if (strlen (name) >= sizeof (tmp2) - 1)
	return FALSE;
    type = 1;
    strcpy (tmp2, name);
    for (i = 1; i <= 4; i++) {
	sprintf (tmp1, "Mouse%d", i);
	if (!strcmp (name, tmp1)) {
	    sprintf (tmp2, "mouse%d", i - 1);
	    type = 0;
	    break;
	}
	sprintf (tmp1, "Joystick%d", i);
	if (!strcmp (name, tmp1)) {
	    sprintf (tmp2, "joy%d", i - 1);
	    type = 0;
	    break;
	}
	sprintf (tmp1, "KeyboardLayout%d", i);
	if (!strcmp (name, tmp1)) {
	    sprintf (tmp2, "kbd%d", i);
	    type = 0;
	    break;
	}
    }
    inputdevice_joyport_config (&changed_prefs, tmp2, num, type);
    return TRUE;
}

static BOOL RPSendMessagex (UINT uMessage, WPARAM wParam, LPARAM lParam,
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
	write_log ("RPSEND(%s [%d], %08x, %08x, %08x, %d)\n",
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

    if (WIN32GFX_IsPicassoScreen ()) {
	return currprefs.gfx_pfullscreen ? RP_SCREENMODE_FULLSCREEN : RP_SCREENMODE_1X;
    } else {
	if (currprefs.gfx_afullscreen)
	    return RP_SCREENMODE_FULLSCREEN;
	if (res == 0)
	    return RP_SCREENMODE_1X;
	if (res == 1)
	    return RP_SCREENMODE_2X;
	return RP_SCREENMODE_4X;
    }
}

static LRESULT CALLBACK RPHostMsgFunction2 (UINT uMessage, WPARAM wParam, LPARAM lParam,
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
	case RPIPCHM_PING:
	    return TRUE;
	case RPIPCHM_CLOSE:
	    uae_quit ();
	return TRUE;
	case RPIPCHM_RESET:
	    uae_reset (wParam == RP_RESET_SOFT ? 0 : -1);
	return TRUE;
	case RPIPCHM_TURBO:
	{
	    if (wParam & RP_TURBO_CPU)
		warpmode ((lParam & RP_TURBO_CPU) ? 1 : 0);
	    if (wParam & RP_TURBO_FLOPPY)
		changed_prefs.floppy_speed = (lParam & RP_TURBO_FLOPPY) ? 0 : 100;
	}
	return TRUE;
	case RPIPCHM_PAUSE:
	    pausemode (wParam ? 1 : 0);
	return TRUE;
	case RPIPCHM_VOLUME:
	    currprefs.sound_volume = changed_prefs.sound_volume = 100 - wParam;
	    set_volume (currprefs.sound_volume, 0);
	return TRUE;
	case RPIPCHM_ESCAPEKEY:
	    rp_rpescapekey = wParam;
	    rp_rpescapeholdtime = lParam;
        return TRUE;
	case RPIPCHM_MOUSECAPTURE:
	{
	    if (wParam & RP_MOUSECAPTURE_CAPTURED)
	    	setmouseactive (1);
	    else
		setmouseactive (0);
	}
	return TRUE;
#if 0
	case RPIPCHM_MINIMIZE:
	    minimized = 1;
	    if (ShowWindow (hAmigaWnd, SW_MINIMIZE))
		return TRUE;
	break;
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
#endif
	case RPIPCHM_DEVICECONTENT:
	{
	    struct RPDeviceContent *dc = (struct RPDeviceContent*)pData;
	    char *n = ua (dc->szContent);
	    int num = dc->btDeviceNumber;
	    int ok = FALSE;
	    switch (dc->btDeviceCategory)
	    {
		case RP_DEVICE_FLOPPY:
		disk_insert (num, n);
		ok = TRUE;
		break;
		case RP_DEVICE_INPUTPORT:
		ok = port_insert (num, n);
		break;

	    }
	    xfree (n);
	    return ok;
	}
	case RPIPCHM_SCREENMODE:
	{
	    int res = (BYTE)wParam;
	    minimized = 0;
	    if (WIN32GFX_IsPicassoScreen ()) {
		changed_prefs.gfx_pfullscreen = res >= RP_SCREENMODE_FULLSCREEN ? 1 : 0;
	    } else {
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
	    }
	    updatewinfsmode (&changed_prefs);
	    WIN32GFX_DisplayChangeRequested ();
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
	case RPIPCHM_SCREENCAPTURE:
	{
	    extern int screenshotf (const char *spath, int mode, int doprepare);
	    extern int screenshotmode;
	    int ok;
	    int ossm = screenshotmode;
	    char *s = ua ((WCHAR*)pData);
	    screenshotmode = 0;
	    ok = screenshotf (s, 1, 1);
	    screenshotmode = ossm;
	    xfree (s);
	    return ok ? TRUE : FALSE;
	}
	case RPIPCHM_SCREENCLIP:
	{
	    RPSCREENCLIP *sc = (RPSCREENCLIP*)pData;

	    if (sc->lLeft < 0 || sc->lTop < 0) {
		changed_prefs.gfx_xcenter_pos = -1;
		changed_prefs.gfx_ycenter_pos = -1;
	    } else {
		changed_prefs.gfx_xcenter_pos = sc->lLeft;
		changed_prefs.gfx_ycenter_pos = sc->lTop;
	    }
	    if (sc->lWidth <= 0) {
		changed_prefs.gfx_size_win.width = default_width;
	    } else {
		changed_prefs.gfx_size_win.width = sc->lWidth << (RES_MAX - currprefs.gfx_resolution);
	    }
	    if (sc->lHeight <= 0) {
		changed_prefs.gfx_size_win.height = default_height;
	    } else {
		changed_prefs.gfx_size_win.height = sc->lHeight << (currprefs.gfx_linedbl ? 0 : 1);
	    }
	    fixup_prefs_dimensions (&changed_prefs);
	}
	return TRUE;
    }
    return FALSE;
}

static LRESULT CALLBACK RPHostMsgFunction (UINT uMessage, WPARAM wParam, LPARAM lParam,
                                          LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam)
{
    LRESULT lr;
    recursive_device++;
    lr = RPHostMsgFunction2 (uMessage, wParam, lParam, pData, dwDataSize, lMsgFunctionParam);
    recursive_device--;
    return lr;
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
    mousecapture = 0;
    return hr;
}

void rp_free (void)
{
    if (!initialized)
	return;
    initialized = 0;
    RPSendMessagex (RPIPCGM_CLOSED, 0, 0, NULL, 0, &guestinfo, NULL);
    RPUninitializeGuest (&guestinfo);
}

HWND rp_getparent (void)
{
    LRESULT lr;
    if (!initialized)
	return 0;
    RPSendMessagex (RPIPCGM_PARENT, 0, 0, NULL, 0, &guestinfo, &lr);
    return (HWND)lr;
}

static void sendfeatures (void)
{
    DWORD feat;

    feat = RP_FEATURE_POWERLED | RP_FEATURE_SCREEN1X | RP_FEATURE_FULLSCREEN;
    feat |= RP_FEATURE_PAUSE | RP_FEATURE_TURBO | RP_FEATURE_VOLUME | RP_FEATURE_SCREENCAPTURE;
    if (!WIN32GFX_IsPicassoScreen ())
	feat |= RP_FEATURE_SCREEN2X;
    RPSendMessagex (RPIPCGM_FEATURES, feat, 0, NULL, 0, &guestinfo, NULL);
}

void rp_fixup_options (struct uae_prefs *p)
{
    int i, v;
    int res;
    int p96;

    if (!initialized)
	return;
    write_log ("rp_fixup_options(rpescapekey=%d,rpescapeholdtime=%d,screenmode=%d,inputmode=%d)\n",
	rp_rpescapekey, rp_rpescapeholdtime, rp_screenmode, rp_inputmode);

    p96 = WIN32GFX_IsPicassoScreen ();
    res = 1 << currprefs.gfx_resolution;
    default_width = currprefs.gfx_size_win.width / res;
    default_height = currprefs.gfx_size_win.height / res;

    p->win32_borderless = -1;
    p->gfx_afullscreen = p->gfx_pfullscreen = 0;
    res = rp_screenmode;
    if (res >= RP_SCREENMODE_FULLSCREEN) {
	if (p96)
	    p->gfx_pfullscreen = 1;
	else
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

    sendfeatures ();

    /* floppy drives */
    v = 0;
    for (i = 0; i < 4; i++) {
	if (p->dfxtype[i] >= 0)
	    v |= 1 << i;
    }
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_FLOPPY, v, NULL, 0, &guestinfo, NULL);

    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_INPUTPORT, 3, NULL, 0, &guestinfo, NULL);

    cd_mask = 0;
    for (i = 0; i < currprefs.mountitems; i++) {
        struct uaedev_config_info *uci = &currprefs.mountconfig[i];
	if (uci->controller == HD_CONTROLLER_UAE) {
	    hd_mask |= 1 << i;
        } else if (uci->controller <= HD_CONTROLLER_IDE3 ) {
	    hd_mask |= 1 << (uci->controller -  HD_CONTROLLER_IDE0);
	} else if (uci->controller <= HD_CONTROLLER_SCSI6) {
	    hd_mask |= 1 << (uci->controller -  HD_CONTROLLER_SCSI0);
	}
    }
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_HD, hd_mask, NULL, 0, &guestinfo, NULL);
}

static void rp_device_change (int dev, int num, const char *name)
{
    struct RPDeviceContent *dc;
    int dc_size;
    char np[MAX_DPATH];

    if (!initialized)
	return;
    if (recursive_device)
	return;
    np[0] = 0;
    if (name != NULL)
	strcpy (np, name);
    dc_size = sizeof (struct RPDeviceContent) + (strlen (np) + 1) * sizeof (WCHAR);
    dc = xcalloc (dc_size, 1);
    dc->btDeviceCategory = dev;
    dc->btDeviceNumber = num;
    wcscpy (dc->szContent, au (np));
    RPSendMessagex(RPIPCGM_DEVICECONTENT, 0, 0, dc, dc_size, &guestinfo, NULL);
    xfree (dc);
}

void rp_disk_change (int num, const char *name)
{
    rp_device_change (RP_DEVICE_FLOPPY, num, name);
}
void rp_input_change (int num, const char *name)
{
    rp_device_change (RP_DEVICE_INPUTPORT, num, name);
}

void rp_hd_change (int num, int removed)
{
    if (removed)
	hd_mask &= ~(1 << num);
    else
	hd_mask |= 1 << num;
    RPSendMessagex(RPIPCGM_DEVICES, RP_DEVICE_HD, hd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_cd_change (int num, int removed)
{
    if (removed)
	cd_mask &= ~(1 << num);
    else
	cd_mask |= 1 << num;
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_CD, cd_mask, NULL, 0, &guestinfo, NULL);
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
        RPSendMessage (RPIPCGM_POWERLED, onoff ? 100 : 0, 0, NULL, 0, &guestinfo, NULL);
	break;
	case 1:
	case 2:
	case 3:
	case 4:
        RPSendMessage (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_FLOPPY, led - 1), onoff ? -1 : 0, NULL, 0, &guestinfo, NULL);
	break;
    }
}

void rp_hd_activity (int num, int onoff)
{
    if (!initialized)
	return;
    if (num < 0)
	return;
    if (onoff)
	RPSendMessage (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_HD, num), 200, NULL, 0, &guestinfo, NULL);
}

void rp_cd_activity (int num, int onoff)
{
    if (!initialized)
	return;
    if (num < 0)
	return;
    if ((cd_mask & (1 << num)) != ((onoff ? 1 : 0) << num)) {
        cd_mask ^= 1 << num;
        RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_CD, cd_mask, NULL, 0, &guestinfo, NULL);
    }
    if (onoff) {
	RPSendMessage (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_CD, num), 200, NULL, 0, &guestinfo, NULL);
    }
}

void rp_update_status (struct uae_prefs *p)
{
    if (!initialized)
	return;
    RPSendMessagex (RPIPCGM_VOLUME, (WPARAM)(100 - p->sound_volume), 0, NULL, 0, &guestinfo, NULL);
}

static void rp_mouse (void)
{
    int flags = 0;

    if (!initialized)
	return;
    if (mousemagic)
	flags |= RP_MOUSECAPTURE_MAGICMOUSE;
    if (mousecapture)
	flags |= RP_MOUSECAPTURE_CAPTURED;
    RPSendMessagex (RPIPCGM_MOUSECAPTURE, flags, 0, NULL, 0, &guestinfo, NULL);
}

void rp_mouse_capture (int captured)
{
    mousecapture = captured;
    rp_mouse ();
}

void rp_mouse_magic (int magic)
{
    mousemagic = magic;
    rp_mouse ();
}

void rp_activate (int active, LPARAM lParam)
{
    if (!initialized)
	return;
    RPSendMessagex (active ? RPIPCGM_ACTIVATED : RPIPCGM_DEACTIVATED, 0, lParam, NULL, 0, &guestinfo, NULL);
}

#if 0
void rp_minimize (int minimize)
{
    if (!initialized)
	return;
    RPSendMessagex(minimize ? RPIPCGM_MINIMIZED : RPIPCGM_RESTORED, 0, 0, NULL, 0, &guestinfo, NULL);
}
#endif

void rp_turbo (int active)
{
    if (!initialized)
	return;
    RPSendMessagex (RPIPCGM_TURBO, RP_TURBO_CPU, active, NULL, 0, &guestinfo, NULL);
}

void rp_set_hwnd (HWND hWnd)
{
    int rx;
    if (!initialized)
	return;
    rx = get_x ();
    hwndset = 1;
    RPSendMessagex (RPIPCGM_SCREENMODE, rx, (LPARAM)hWnd, NULL, 0, &guestinfo, NULL); 
}

void rp_rtg_switch (void)
{
    if (!initialized)
	return;
    sendfeatures ();
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

void rp_vsync (void)
{
    uae_u64 t;

    if (!initialized)
	return;
    if (magicmouse_alive () != mousemagic)
	rp_mouse_magic (magicmouse_alive ());
    if (!esctime)
	return;
    t = gett ();
    if (t >= esctime) {
	RPSendMessagex (RPIPCGM_ESCAPED, 0, 0, NULL, 0, &guestinfo, NULL);
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
