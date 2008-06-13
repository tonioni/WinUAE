
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
#include "savestate.h"
#include "gfxfilter.h"

static int initialized;
static RPGUESTINFO guestinfo;

char *rp_param = NULL;
int rp_rpescapekey = 0x01;
int rp_rpescapeholdtime = 600;
int rp_screenmode = 0;
int rp_inputmode = 0;
int log_rp = 0;
static int max_horiz_dbl = RES_HIRES;
static int max_vert_dbl = 1;

static int default_width, default_height;
static int hwndset;
static int minimized;
static DWORD hd_mask, cd_mask, floppy_mask;
static int mousecapture, mousemagic;
static int rp_filter, rp_filter_default;
static int recursive_device, recursive;
static int currentpausemode;

static int cando (void)
{
    if (!initialized)
	return 0;
    return 1;
}
static int isrecursive (void)
{
    return recursive_device;
}

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
    int len;

    if (s == NULL)
	return NULL;
    len = WideCharToMultiByte (CP_ACP, 0, s, -1, NULL, 0, 0, FALSE);
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
	case RPIPCGM_CLOSE: return "RPIPCGM_CLOSE";
	case RPIPCGM_ACTIVATED: return "RPIPCGM_ACTIVATED";
	case RPIPCGM_DEACTIVATED: return "RPIPCGM_DEACTIVATED";
	case RPIPCGM_PARENT: return "RPIPCGM_PARENT";
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
	case RPIPCGM_DEVICESEEK: return "RPIPCGM_DEVICESEEK";
	case RPIPCGM_ESCAPED: return "RPIPCGM_ESCAPED";

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
	case RPIPCHM_SAVESTATE: return "RPIPCHM_SAVESTATE";
	case RPIPCHM_LOADSTATE: return "RPIPCHM_LOADSTATE";

	default: return "UNKNOWN";
    }
}

static void trimws (char *s)
{
    /* Delete trailing whitespace.  */
    int len = strlen (s);
    while (len > 0 && strcspn (s + len - 1, "\t \r\n") == 0)
        s[--len] = '\0';
}

static int port_insert2 (int num, const char *name)
{
    char tmp2[1000];
    int i, type;

    type = 1;
    strcpy (tmp2, name);
    for (i = 1; i <= 4; i++) {
	char tmp1[1000];
	sprintf (tmp1, "Mouse%d", i);
	if (!strcmp (name, tmp1)) {
	    sprintf (tmp2, "mouse%d", i - 1);
	    type = 0;
	    break;
	}
	sprintf (tmp1, "Joystick%d", i);
	if (!strcmp (name, tmp1)) {
	    if (i - 1 == JSEM_XARCADE1LAYOUT)
		sprintf (tmp2, "kbd%d", JSEM_XARCADE1LAYOUT);
	    else if (i - 1 == JSEM_XARCADE2LAYOUT)
		sprintf (tmp2, "kbd%d", JSEM_XARCADE2LAYOUT);
	    else
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
    trimws (tmp2);
    return inputdevice_joyport_config (&changed_prefs, tmp2, num, type);
}

static int port_insert (int num, const char *name)
{
    char tmp1[1000];

    if (num < 0 || num > 1)
	return FALSE;
    if (strlen (name) == 0) {
	inputdevice_joyport_config (&changed_prefs, "none", num, 0);
	return TRUE;
    }
    if (strlen (name) >= sizeof (tmp1) - 1)
	return FALSE;

    strcpy (tmp1, name);
    for (;;) {
	char *p = strrchr (tmp1, '\\');
	if (p) {
	    int v = port_insert2 (num, p + 1);
	    if (v)
		return TRUE;
	    *p = 0;
	    continue;
	}
	return port_insert2 (num, tmp1);
    }
}

static BOOL RPPostMessagex(UINT uMessage, WPARAM wParam, LPARAM lParam, const RPGUESTINFO *pInfo)
{
    BOOL v = FALSE;
    static int cnt;
    int ncnt;
    int dolog = log_rp;

    if (!pInfo) {
	write_log ("RPPOST: pInfo == NULL!\n");
        return FALSE;
    }
    if (uMessage == RPIPCGM_DEVICESEEK)
	dolog = 0;
    recursive++;
    cnt++;
    ncnt = cnt;
    if (dolog)
	write_log ("RPPOST_%d->\n", ncnt);
    v = RPPostMessage (uMessage, wParam, lParam, pInfo);
    recursive--;
    if (dolog) {
	write_log ("RPPOST_%d(%s [%d], %08x, %08x)\n", ncnt,
	    getmsg (uMessage), uMessage - WM_APP, wParam, lParam);
	if (v == FALSE)
	    write_log("ERROR %d\n", GetLastError ());
    }
    return v;
}

static BOOL RPSendMessagex (UINT uMessage, WPARAM wParam, LPARAM lParam,
                   LPCVOID pData, DWORD dwDataSize, const RPGUESTINFO *pInfo, LRESULT *plResult)
{
    BOOL v = FALSE;
    static int cnt;
    int ncnt;
    int dolog = log_rp;

    if (!pInfo) {
	write_log ("RPSEND: pInfo == NULL!\n");
        return FALSE;
    }
    if (!pInfo->hHostMessageWindow) {
	write_log ("RPSEND: pInfo->hHostMessageWindow == NULL!\n");
        return FALSE;
    }
    if (uMessage == RPIPCGM_DEVICESEEK)
	dolog = 0;
    recursive++;
    cnt++;
    ncnt = cnt;
    if (dolog)
	write_log ("RPSEND_%d->\n", ncnt);
    v = RPSendMessage (uMessage, wParam, lParam, pData, dwDataSize, pInfo, plResult);
    recursive--;
    if (dolog) {
	write_log ("RPSEND_%d(%s [%d], %08x, %08x, %08x, %d)\n", ncnt,
	    getmsg (uMessage), uMessage - WM_APP, wParam, lParam, pData, dwDataSize);
	if (v == FALSE)
	    write_log("ERROR %d\n", GetLastError ());
    }
    return v;
}

static int winok (void)
{
    if (!initialized)
	return 0;
    if (!hwndset)
	return 0;
    return 1;
}


static void fixup_size (struct uae_prefs *prefs)
{
    if (prefs->gfx_xcenter_size > 0) {
	int hres = prefs->gfx_resolution;
	if (prefs->gfx_filter) {
	    if (prefs->gfx_filter_horiz_zoom_mult)
		hres += (1000 / prefs->gfx_filter_horiz_zoom_mult) - 1;
	    hres += uaefilters[prefs->gfx_filter].intmul - 1;
	}
	if (hres > RES_MAX)
	    hres = RES_MAX;
        prefs->gfx_size_win.width = prefs->gfx_xcenter_size >> (RES_MAX - hres);
    }
    if (prefs->gfx_ycenter_size > 0) {
	int vres = prefs->gfx_linedbl ? 1 : 0;
	if (prefs->gfx_filter) {
	    if (prefs->gfx_filter_vert_zoom_mult)
		vres += (1000 / prefs->gfx_filter_vert_zoom_mult) - 1;
	    vres += uaefilters[prefs->gfx_filter].intmul - 1;
	}
	if (vres > RES_MAX)
	    vres = RES_MAX;
	prefs->gfx_size_win.height = (prefs->gfx_ycenter_size * 2) >> (RES_MAX - vres);
    }
}

#define LORES_WIDTH 360
#define LORES_HEIGHT 284
static void get_screenmode (struct RPScreenMode *sm, struct uae_prefs *p)
{
    int hres;
    int m;
    int full = 0;
    int vres = 0;
    int totalhdbl, totalvdbl;

    hres = p->gfx_resolution;
    if (p->gfx_filter && p->gfx_filter_horiz_zoom_mult)
	hres += (1000 / p->gfx_filter_horiz_zoom_mult) - 1;
    if (hres > RES_MAX)
	hres = RES_MAX;
    vres = hres;
    m = RP_SCREENMODE_1X;

    if (WIN32GFX_IsPicassoScreen ()) {

	full = p->gfx_pfullscreen;
	sm->lClipTop = 0;
	sm->lClipLeft = 0;
	sm->lClipHeight = picasso96_state.Height;
	sm->lClipWidth = picasso96_state.Width;

    } else {

	full = p->gfx_afullscreen;

	if (hres == RES_HIRES)
	    m = RP_SCREENMODE_2X;
	if (hres == RES_SUPERHIRES)
	    m = RP_SCREENMODE_4X;

	totalhdbl = hres;
        if (hres > max_horiz_dbl)
	    hres = max_horiz_dbl;
	totalvdbl = vres;
	if (vres > max_vert_dbl)
	    vres = max_vert_dbl;

	if (log_rp)
	    write_log ("GET_RPSM: hres=%d (%d) vres=%d (%d) full=%d xcpos=%d ycpos=%d w=%d h=%d\n",
		totalhdbl, hres, totalvdbl, vres, full,
		p->gfx_xcenter_pos,  p->gfx_ycenter_pos,
		p->gfx_size_win.width, p->gfx_size_win.height);
	sm->lClipLeft = p->gfx_xcenter_pos <= 0 ? -1 : p->gfx_xcenter_pos;
	sm->lClipTop = p->gfx_ycenter_pos <= 0 ? -1 : p->gfx_ycenter_pos;
	if (full) {
	    sm->lClipWidth = LORES_WIDTH << RES_MAX;
	    sm->lClipHeight = LORES_HEIGHT << 1;
	} else {
	    sm->lClipWidth = p->gfx_size_win.width << (RES_MAX - totalhdbl);
	    if (totalvdbl == 2)
		sm->lClipHeight = p->gfx_size_win.height >> 1;
	    else
		sm->lClipHeight = p->gfx_size_win.height << (1 - totalvdbl);
	}
	if (full && p->gfx_filter && p->gfx_filter_horiz_zoom_mult == 0)
	    m = RP_SCREENMODE_XX;
    }
    if (full) {
	m &= ~0x0000ff00;
	m |= p->gfx_display << 8;
    }
    if (full > 1)
	m |= RP_SCREENMODE_FULLWINDOW;
    sm->dwScreenMode = m;

    if (log_rp)
	write_log ("GET_RPSM: %08X %dx%d %dx%d hres=%d (%d) vres=%d (%d) disp=%d fs=%d\n",
	    sm->dwScreenMode, sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight,
	    totalhdbl, hres, totalvdbl, vres, p->gfx_display, full);
}

static void set_screenmode (struct RPScreenMode *sm, struct uae_prefs *p)
{
    int smm = RP_SCREENMODE_MODE (sm->dwScreenMode);
    int display = RP_SCREENMODE_DISPLAY (sm->dwScreenMode);
    int fs = 0;
    int hdbl = 1, vdbl = 1;
    int hres, vres;
    struct MultiDisplay *disp;

    minimized = 0;
    if (display) {
	p->gfx_display = display;
	if (sm->dwScreenMode & RP_SCREENMODE_FULLWINDOW)
	    fs = 2;
	else
	    fs = 1;
    }
    disp = getdisplay (p);

    if (log_rp)
	write_log ("SET_RPSM: %08X %dx%d %dx%d hres=%d vres=%d disp=%d fs=%d\n",
	    sm->dwScreenMode, sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight,
	    hdbl, vdbl, display, fs);

    if (!WIN32GFX_IsPicassoScreen ()) {

	hdbl = smm;
	if (smm == RP_SCREENMODE_3X)
	    hdbl = RES_HIRES;
	if (smm == RP_SCREENMODE_4X)
	    hdbl = RES_SUPERHIRES;
	if (smm > RP_SCREENMODE_4X || smm == RP_SCREENMODE_XX) {
	    hdbl = max_horiz_dbl;
	    vdbl = max_vert_dbl;
	}

	if (hdbl == RES_LORES) {
	    vdbl = 0;
	} else {
	    vdbl = 1;
	    if (hdbl == RES_SUPERHIRES)
		vdbl = 2;
	}

	hres = hdbl;
	if (hres > max_horiz_dbl)
	    hres = max_horiz_dbl;
	p->gfx_resolution = hres;
	vres = vdbl;
	if (vres > max_vert_dbl)
	    vres = max_vert_dbl;
	p->gfx_linedbl = vres ? 1 : 0;

	if (sm->lClipWidth <= 0)
	    p->gfx_size_win.width = LORES_WIDTH << hdbl;
	else
	    p->gfx_size_win.width = sm->lClipWidth >> (RES_MAX - hdbl);

	if (sm->lClipHeight <= 0) {
	    p->gfx_size_win.height = LORES_HEIGHT << vdbl;
	} else {
	    if (vdbl == 2)
		p->gfx_size_win.height = sm->lClipHeight * 2;
	    else
		p->gfx_size_win.height = sm->lClipHeight >> (1 - vdbl);
	}

	if (fs == 1) {
	    p->gfx_size_fs.width = p->gfx_size_win.width;
	    p->gfx_size_fs.height = p->gfx_size_win.height;
	} else if (fs == 2) {
	    p->gfx_size_fs.width = disp->rect.right - disp->rect.left;
	    p->gfx_size_fs.height = disp->rect.bottom - disp->rect.top;
	}

	p->gfx_filter = rp_filter_default;
	p->gfx_filter_horiz_zoom_mult = 1000;
	p->gfx_filter_vert_zoom_mult = 1000;
	if (log_rp)
	    write_log ("WW=%d WH=%d FW=%d FH=%d\n",
		p->gfx_size_win.width, p->gfx_size_win.height,
		p->gfx_size_fs.width, p->gfx_size_fs.height);
	if (fs) {
	    if (smm == RP_SCREENMODE_XX) {
	        p->gfx_filter = rp_filter;
		p->gfx_filter_horiz_zoom_mult = 0;
		p->gfx_filter_vert_zoom_mult = 0;
	    } else {
		int mult;
		int prevmult = 1;
		int xmult = uaefilters[p->gfx_filter].intmul;
		int ymult = uaefilters[p->gfx_filter].intmul ;
		for (mult = 2; mult <= 4; mult+=2) {
		    int w = p->gfx_size_win.width;
		    int h = p->gfx_size_win.height;
		    if (p->gfx_size_fs.width * xmult < w * mult || p->gfx_size_fs.height * ymult < h * mult) {
			mult = prevmult;
			break;
		    }
		    prevmult = mult;
		}
		if (mult > 1 || fs == 2) {
		    p->gfx_filter = rp_filter;
		    p->gfx_filter_horiz_zoom_mult = 1000 / mult;
		    p->gfx_filter_vert_zoom_mult = 1000 / mult;
		}
	    }
	} else {
	    if (hdbl != hres || vdbl != vres) {
		p->gfx_filter = rp_filter;
		p->gfx_filter_horiz_zoom_mult = 1000 >> (hdbl - hres);
		p->gfx_filter_vert_zoom_mult = 1000 >> (vdbl - vres);
	    }
	}

    }
    p->gfx_pfullscreen = fs;
    p->gfx_afullscreen = fs;
    p->gfx_xcenter_pos = sm->lClipLeft;
    p->gfx_ycenter_pos = sm->lClipTop;
    p->gfx_xcenter_size = sm->lClipWidth;
    p->gfx_ycenter_size = sm->lClipHeight;

    updatewinfsmode (p);
    hwndset = 0;
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
	    currentpausemode = pause_emulation;
	    if (wParam ? 1 : 0 != pause_emulation ? 1 : 0) {
		pausemode (wParam ? 1 : 0);
		if (wParam) {
		    currentpausemode = -1;
		    return 2;
		}
	    }
	    return 1;
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
	    struct RPScreenMode *sm = (struct RPScreenMode*)pData;
	    set_screenmode (sm, &changed_prefs);
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
	case RPIPCHM_SAVESTATE:
	{
	    char *s = ua ((WCHAR*)pData);
	    DWORD ret = FALSE;
	    if (s == NULL) {
		savestate_initsave (NULL, 0, TRUE);
		return 1;
	    }
	    if (vpos == 0) {
		savestate_initsave ("", 1, TRUE);
		save_state (s, "AF2008");
		ret = 1;
	    } else {
		//savestate_initsave (s, 1, TRUE);
		//ret = -1;
	    }
	    xfree (s);
	    return ret;
	}
	case RPIPCHM_LOADSTATE:
	{
	    char *s = ua ((WCHAR*)pData);
	    DWORD ret = FALSE;
	    DWORD attr = GetFileAttributes (s);
	    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
	        savestate_state = STATE_DORESTORE;
	        strcpy (savestate_fname, s);
	        ret = -1;
	    }
	    xfree (s);
	    return ret;
	}
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
    if (!cando ())
	return;
    if (hwndset)
	rp_set_hwnd (NULL);
    initialized = 0;
    RPSendMessagex (RPIPCGM_CLOSED, 0, 0, NULL, 0, &guestinfo, NULL);
    RPUninitializeGuest (&guestinfo);
}

int rp_close (void)
{
    if (!cando ())
	return 0;
    RPSendMessagex (RPIPCGM_CLOSE, 0, 0, NULL, 0, &guestinfo, NULL);
    return 1;
}

HWND rp_getparent (void)
{
    LRESULT lr;
    if (!initialized)
	return NULL;
    RPSendMessagex (RPIPCGM_PARENT, 0, 0, NULL, 0, &guestinfo, &lr);
    return (HWND)lr;
}

static void sendfeatures (void)
{
    DWORD feat;

    feat = RP_FEATURE_POWERLED | RP_FEATURE_SCREEN1X | RP_FEATURE_FULLSCREEN;
    feat |= RP_FEATURE_PAUSE | RP_FEATURE_TURBO | RP_FEATURE_VOLUME | RP_FEATURE_SCREENCAPTURE;
    feat |= RP_FEATURE_STATE;
    if (!WIN32GFX_IsPicassoScreen ())
	feat |= RP_FEATURE_SCREEN2X | RP_FEATURE_SCREEN4X;
    RPSendMessagex (RPIPCGM_FEATURES, feat, 0, NULL, 0, &guestinfo, NULL);
}

void rp_fixup_options (struct uae_prefs *p)
{
    int i;
    struct RPScreenMode sm;

    if (!initialized)
	return;

    write_log ("rp_fixup_options(escapekey=%d,escapeholdtime=%d,screenmode=%d,inputmode=%d)\n",
	rp_rpescapekey, rp_rpescapeholdtime, rp_screenmode, rp_inputmode);
    write_log ("w=%dx%d fs=%dx%d\n",
	p->gfx_size_win.width, p->gfx_size_win.height,
	p->gfx_size_fs.width, p->gfx_size_fs.height);

    max_horiz_dbl = currprefs.gfx_max_horizontal;
    max_vert_dbl = currprefs.gfx_max_vertical;

    changed_prefs.win32_borderless = currprefs.win32_borderless = 1;
    rp_filter_default = rp_filter = currprefs.gfx_filter;
    if (rp_filter == 0)
	rp_filter = UAE_FILTER_NULL;

    fixup_size (p);
    get_screenmode (&sm, p);
    sm.dwScreenMode = rp_screenmode;
    set_screenmode (&sm, &currprefs);
    set_screenmode (&sm, &changed_prefs);

    sendfeatures ();

    /* floppy drives */
    floppy_mask = 0;
    for (i = 0; i < 4; i++) {
	if (p->dfxtype[i] >= 0)
	    floppy_mask |= 1 << i;
    }
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_FLOPPY, floppy_mask, NULL, 0, &guestinfo, NULL);

    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_INPUTPORT, 3, NULL, 0, &guestinfo, NULL);
    rp_input_change (0);
    rp_input_change (1);

    hd_mask = 0;
    cd_mask = 0;
    for (i = 0; i < currprefs.mountitems; i++) {
        struct uaedev_config_info *uci = &currprefs.mountconfig[i];
	int num = -1;
	if (uci->controller == HD_CONTROLLER_UAE) {
	    num = i;
        } else if (uci->controller <= HD_CONTROLLER_IDE3 ) {
	    num = uci->controller -  HD_CONTROLLER_IDE0;
	} else if (uci->controller <= HD_CONTROLLER_SCSI6) {
	    num = uci->controller -  HD_CONTROLLER_SCSI0;
	}
	if (num >= 0) {
    	    hd_mask |= 1 << num;
	    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_HD, hd_mask, NULL, 0, &guestinfo, NULL);
	    rp_harddrive_image_change (num, uci->rootdir);
	}
    }

    rp_update_volume (&currprefs);
    rp_turbo (turbo_emulation);
    for (i = 0; i <= 4; i++)
	rp_update_leds (i, 0, 0);
}

static void rp_device_change (int dev, int num, const char *name)
{
    struct RPDeviceContent *dc;
    int dc_size;
    char np[MAX_DPATH];

    if (!cando ())
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
    RPSendMessagex (RPIPCGM_DEVICECONTENT, 0, 0, dc, dc_size, &guestinfo, NULL);
    xfree (dc);
}

void rp_input_change (int num)
{
    int j = jsem_isjoy (num, &currprefs);
    int m = jsem_ismouse (num, &currprefs);
    int k = jsem_iskbdjoy (num, &currprefs);
    char name[MAX_DPATH];
    char *name2 = NULL, *name3 = NULL;

    if (JSEM_ISXARCADE1 (num, &currprefs)) {
	j = 2;
	m = k = -1;
    } else if (JSEM_ISXARCADE2 (num, &currprefs)) {
	j = 3;
	m = k = -1;
    } else if (j >= 1) {
	j = 1;
    }
    if (j >= 0) {
	name2 = inputdevice_get_device_name (IDTYPE_JOYSTICK, j);
	name3 = inputdevice_get_device_unique_name (IDTYPE_JOYSTICK, j);
	sprintf (name, "Joystick%d", j + 1);
    } else if (m >= 0) {
	name2 = inputdevice_get_device_name (IDTYPE_MOUSE, m);
	name3 = inputdevice_get_device_unique_name (IDTYPE_MOUSE, m);
	sprintf (name, "Mouse%d", m + 1);
    } else if (k >= 0) {
	sprintf (name, "KeyboardLayout%d", k + 1);
    }
    if (name3) {
        strcat (name, "\\");
        strcat (name, name3);
	if (name2) {
	    strcat (name, "\\");
	    strcat (name, name2);
	}
    }
    rp_device_change (RP_DEVICE_INPUTPORT, num, name);
}
void rp_disk_image_change (int num, const char *name)
{
    rp_device_change (RP_DEVICE_FLOPPY, num, name);
}
void rp_harddrive_image_change (int num, const char *name)
{
    rp_device_change (RP_DEVICE_HD, num, name);
}

void rp_floppydrive_change (int num, int removed)
{
    if (!cando ())
	return;
    if (removed)
	floppy_mask &= ~(1 << num);
    else
	floppy_mask |= 1 << num;
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_FLOPPY, floppy_mask, NULL, 0, &guestinfo, NULL);
}

void rp_hd_change (int num, int removed)
{
    if (!cando ())
	return;
    if (removed)
	hd_mask &= ~(1 << num);
    else
	hd_mask |= 1 << num;
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_HD, hd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_cd_change (int num, int removed)
{
    if (!cando ())
	return;
    if (removed)
	cd_mask &= ~(1 << num);
    else
	cd_mask |= 1 << num;
    RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_CD, cd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_floppy_track (int floppy, int track)
{
    if (!cando ())
	return;
    RPPostMessagex (RPIPCGM_DEVICESEEK, MAKEWORD (RP_DEVICE_FLOPPY, floppy), track, &guestinfo);
}

void rp_update_leds (int led, int onoff, int write)
{
    if (!cando ())
	return;
    if (led < 0 || led > 4)
	return;
    switch (led)
    {
	case 0:
	RPSendMessage (RPIPCGM_POWERLED, onoff >= 250 ? 100 : onoff * 10 / 26, 0, NULL, 0, &guestinfo, NULL);
	break;
	case 1:
	case 2:
	case 3:
	case 4:
        RPPostMessagex (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_FLOPPY, led - 1),
	    MAKELONG (onoff ? -1 : 0, write ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ) , &guestinfo);
	break;
    }
}

void rp_hd_activity (int num, int onoff, int write)
{
    if (!cando ())
	return;
    if (num < 0)
	return;
    if (onoff)
	RPPostMessagex (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_HD, num),
	    MAKELONG (200, write ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ), &guestinfo);
}

void rp_cd_activity (int num, int onoff)
{
    if (!cando ())
	return;
    if (num < 0)
	return;
    if ((cd_mask & (1 << num)) != ((onoff ? 1 : 0) << num)) {
        cd_mask ^= 1 << num;
        RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_CD, cd_mask, NULL, 0, &guestinfo, NULL);
    }
    if (onoff) {
	RPPostMessage (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICE_CD, num),
	    MAKELONG (200, RP_DEVICEACTIVITY_READ), &guestinfo);
    }
}

void rp_update_volume (struct uae_prefs *p)
{
    if (!cando ())
	return;
    RPSendMessagex (RPIPCGM_VOLUME, (WPARAM)(100 - p->sound_volume), 0, NULL, 0, &guestinfo, NULL);
}

void rp_pause (int paused)
{
    if (!cando ())
	return;
    if (isrecursive ())
	return;
    if (currentpausemode != paused)
	RPSendMessagex (RPIPCGM_PAUSE, (WPARAM)paused, 0, NULL, 0, &guestinfo, NULL);
    currentpausemode = paused;
}

static void rp_mouse (void)
{
    int flags = 0;

    if (!cando ())
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
    if (!cando ())
	return;
    RPSendMessagex (active ? RPIPCGM_ACTIVATED : RPIPCGM_DEACTIVATED, 0, lParam, NULL, 0, &guestinfo, NULL);
}

void rp_turbo (int active)
{
    if (!cando ())
	return;
    if (recursive_device)
	return;
    RPSendMessagex (RPIPCGM_TURBO, RP_TURBO_CPU, active ? RP_TURBO_CPU : 0, NULL, 0, &guestinfo, NULL);
}

void rp_set_hwnd (HWND hWnd)
{
    struct RPScreenMode sm = { 0 };

    if (!initialized)
	return;
    get_screenmode (&sm, &currprefs);
    sm.hGuestWindow = hWnd;
    if (hWnd != NULL)
	hwndset = 1;
    RPSendMessagex (RPIPCGM_SCREENMODE, 0, 0, &sm, sizeof sm, &guestinfo, NULL); 
}

void rp_set_enabledisable (int enabled)
{
    if (!cando ())
	return;
    RPSendMessagex (enabled ? RPIPCGM_ENABLED : RPIPCGM_DISABLED, 0, 0, NULL, 0, &guestinfo, NULL);
}

void rp_rtg_switch (void)
{
    if (!cando ())
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
