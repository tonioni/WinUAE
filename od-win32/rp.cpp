
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
#include "blkdev.h"
#include "registry.h"
#include "win32gui.h"
#include <resource>

static int initialized;
static RPGUESTINFO guestinfo;
static int maxjports;

TCHAR *rp_param = NULL;
int rp_rpescapekey = 0x01;
int rp_rpescapeholdtime = 600;
int rp_screenmode = 0;
int rp_inputmode = 0;
int log_rp = 1;
static int rp_revision, rp_version, rp_build;
static int max_horiz_dbl = RES_HIRES;
static int max_vert_dbl = VRES_DOUBLE;

static int default_width, default_height;
static int hwndset;
static int minimized;
static DWORD hd_mask, cd_mask, floppy_mask;
static int mousecapture, mousemagic;
static int rp_filter, rp_filter_default;
static int recursive_device, recursive;
static int currentpausemode;
static int gameportmask[MAX_JPORTS];
static DWORD storeflags;
static bool screenmode_request;
static HWND guestwindow;

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
		write_log (_T("%02X%02X "), s[0], s[1]);
		if (s[0] == 0 && s[1] == 0)
			break;
		s += 2;
	}
	write_log (_T("\n"));
}

static const TCHAR *getmsg (int msg)
{
	switch (msg)
	{
	case RPIPCGM_REGISTER: return _T("RPIPCGM_REGISTER");
	case RPIPCGM_FEATURES: return _T("RPIPCGM_FEATURES");
	case RPIPCGM_CLOSED: return _T("RPIPCGM_CLOSED");
	case RPIPCGM_ACTIVATED: return _T("RPIPCGM_ACTIVATED");
	case RPIPCGM_DEACTIVATED: return _T("RPIPCGM_DEACTIVATED");
	case RPIPCGM_SCREENMODE: return _T("RPIPCGM_SCREENMODE");
	case RPIPCGM_POWERLED: return _T("RPIPCGM_POWERLED");
	case RPIPCGM_DEVICES: return _T("RPIPCGM_DEVICES");
	case RPIPCGM_DEVICEACTIVITY: return _T("RPIPCGM_DEVICEACTIVITY");
	case RPIPCGM_MOUSECAPTURE: return _T("RPIPCGM_MOUSECAPTURE");
	case RPIPCGM_HOSTAPIVERSION: return _T("RPIPCGM_HOSTAPIVERSION");
	case RPIPCGM_PAUSE: return _T("RPIPCGM_PAUSE");
	case RPIPCGM_DEVICECONTENT: return _T("RPIPCGM_DEVICECONTENT");
	case RPIPCGM_TURBO: return _T("RPIPCGM_TURBO");
	case RPIPCGM_PING: return _T("RPIPCGM_PING");
	case RPIPCGM_VOLUME: return _T("RPIPCGM_VOLUME");
	case RPIPCGM_ESCAPED: return _T("RPIPCGM_ESCAPED");
	case RPIPCGM_PARENT: return _T("RPIPCGM_PARENT");
	case RPIPCGM_DEVICESEEK: return _T("RPIPCGM_DEVICESEEK");
	case RPIPCGM_CLOSE: return _T("RPIPCGM_CLOSE");
	case RPIPCGM_DEVICEREADWRITE: return _T("RPIPCGM_DEVICEREADWRITE");
	case RPIPCGM_HOSTVERSION: return _T("RPIPCGM_HOSTVERSION");
	case RPIPCGM_INPUTDEVICE: return _T("RPIPCGM_INPUTDEVICE");

	case RPIPCHM_CLOSE: return _T("RPIPCHM_CLOSE");
	case RPIPCHM_SCREENMODE: return _T("RPIPCHM_SCREENMODE");
	case RPIPCHM_SCREENCAPTURE: return _T("RPIPCHM_SCREENCAPTURE");
	case RPIPCHM_PAUSE: return _T("RPIPCHM_PAUSE");
	case RPIPCHM_DEVICECONTENT: return _T("RPIPCHM_DEVICECONTENT");
	case RPIPCHM_RESET: return _T("RPIPCHM_RESET");
	case RPIPCHM_TURBO: return _T("RPIPCHM_TURBO");
	case RPIPCHM_PING: return _T("RPIPCHM_PING");
	case RPIPCHM_VOLUME: return _T("RPIPCHM_VOLUME");
	case RPIPCHM_ESCAPEKEY: return _T("RPIPCHM_ESCAPEKEY");
	case RPIPCHM_EVENT: return _T("RPIPCHM_EVENT");
	case RPIPCHM_MOUSECAPTURE: return _T("RPIPCHM_MOUSECAPTURE");
	case RPIPCHM_SAVESTATE: return _T("RPIPCHM_SAVESTATE");
	case RPIPCHM_LOADSTATE: return _T("RPIPCHM_LOADSTATE");
	case RPIPCHM_FLUSH: return _T("RPIPCHM_FLUSH");
	case RPIPCHM_DEVICEREADWRITE: return _T("RPIPCHM_DEVICEREADWRITE");
	case RPIPCHM_QUERYSCREENMODE: return _T("RPIPCHM_QUERYSCREENMODE");
	case RPIPCHM_GUESTAPIVERSION : return _T("RPIPCHM_GUESTAPIVERSION");
	default: return _T("UNKNOWN");
	}
}

static void trimws (TCHAR *s)
{
	/* Delete trailing whitespace.  */
	int len = _tcslen (s);
	while (len > 0 && _tcscspn (s + len - 1, _T("\t \r\n")) == 0)
		s[--len] = '\0';
}

static const int inputdevmode[] = {
	RP_INPUTDEVICE_MOUSE, JSEM_MODE_MOUSE,
	RP_INPUTDEVICE_JOYSTICK, JSEM_MODE_JOYSTICK,
	RP_INPUTDEVICE_GAMEPAD, JSEM_MODE_GAMEPAD,
	RP_INPUTDEVICE_ANALOGSTICK, JSEM_MODE_JOYSTICK_ANALOG,
	RP_INPUTDEVICE_JOYPAD, JSEM_MODE_JOYSTICK_CD32,
	RP_INPUTDEVICE_LIGHTPEN, JSEM_MODE_LIGHTPEN,
	0, 0,
};

static int port_insert (int num, int devicetype, DWORD flags, const TCHAR *name)
{
	int devicetype2;

	if (num < 0 || num >= maxjports)
		return FALSE;
	if (_tcslen (name) == 0) {
		inputdevice_joyport_config (&changed_prefs, _T("none"), num, 0, 0);
		return TRUE;
	}
	devicetype2 = -1;
	for (int i = 0; inputdevmode[i * 2]; i++) {
		if (inputdevmode[i * 2 + 0] == devicetype) {
			devicetype2 = inputdevmode[i * 2 + 1];
			break;
		}
	}
	if (devicetype2 < 0)
		return FALSE;

	for (int i = 0; i < 10; i++) {
		TCHAR tmp2[100];
		_stprintf (tmp2, _T("KeyboardLayout%d"), i);
		if (!_tcscmp (tmp2, name)) {
			_stprintf (tmp2, _T("kbd%d"), i + 1);
			return inputdevice_joyport_config (&changed_prefs, tmp2, num, devicetype2, 0);
		}
	}
	return inputdevice_joyport_config (&changed_prefs, name, num, devicetype2, 1);
}

static int cd_insert (int num, const TCHAR *name)
{
	_tcscpy (changed_prefs.cdslots[num].name, name);
	changed_prefs.cdslots[num].inuse = true;
	config_changed = 1;
	return 1;
}

static BOOL RPPostMessagex(UINT uMessage, WPARAM wParam, LPARAM lParam, const RPGUESTINFO *pInfo)
{
	BOOL v = FALSE;
	static int cnt;
	int ncnt;
	int dolog = log_rp;

	if (!pInfo) {
		write_log (_T("RPPOST: pInfo == NULL!\n"));
		return FALSE;
	}
	if (uMessage == RPIPCGM_DEVICESEEK || uMessage == RPIPCGM_DEVICEACTIVITY)
		dolog = 0;
	recursive++;
	cnt++;
	ncnt = cnt;
	if (dolog)
		write_log (_T("RPPOST_%d->\n"), ncnt);
	v = RPPostMessage (uMessage, wParam, lParam, pInfo);
	recursive--;
	if (dolog) {
		write_log (_T("RPPOST_%d(%s [%d], %08x, %08x)\n"), ncnt,
			getmsg (uMessage), uMessage - WM_APP, wParam, lParam);
		if (v == FALSE)
			write_log (_T("ERROR %d\n"), GetLastError ());
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
		write_log (_T("RPSEND: pInfo == NULL!\n"));
		return FALSE;
	}
	if (!pInfo->hHostMessageWindow) {
		write_log (_T("RPSEND: pInfo->hHostMessageWindow == NULL!\n"));
		return FALSE;
	}
	if (uMessage == RPIPCGM_DEVICESEEK)
		dolog = 0;
	recursive++;
	cnt++;
	ncnt = cnt;
	if (dolog)
		write_log (_T("RPSEND_%d->\n"), ncnt);
	v = RPSendMessage (uMessage, wParam, lParam, pData, dwDataSize, pInfo, plResult);
	recursive--;
	if (dolog) {
		write_log (_T("RPSEND_%d(%s [%d], %08x, %08x, %08x, %d)\n"), ncnt,
			getmsg (uMessage), uMessage - WM_APP, wParam, lParam, pData, dwDataSize);
		if (v == FALSE)
			write_log (_T("ERROR %d\n"), GetLastError ());
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
	static int done;

	if (done)
		return;
	done = 1;
	write_log(_T("fixup_size(%d,%d)\n"), prefs->gfx_xcenter_size, prefs->gfx_ycenter_size);
	if (prefs->gfx_xcenter_size > 0) {
		int hres = prefs->gfx_resolution;
		if (prefs->gfx_filter) {
			if (prefs->gfx_filter_horiz_zoom_mult)
				hres += (1000 / prefs->gfx_filter_horiz_zoom_mult) - 1;
			hres += uaefilters[prefs->gfx_filter].intmul - 1;
		}
		if (hres > max_horiz_dbl)
			hres = max_horiz_dbl;
		prefs->gfx_size_win.width = prefs->gfx_xcenter_size >> (RES_MAX - hres);
		prefs->gfx_filter_autoscale = 0;
	}
	if (prefs->gfx_ycenter_size > 0) {
		int vres = prefs->gfx_vresolution;
		if (prefs->gfx_filter) {
			if (prefs->gfx_filter_vert_zoom_mult)
				vres += (1000 / prefs->gfx_filter_vert_zoom_mult) - 1;
			vres += uaefilters[prefs->gfx_filter].intmul - 1;
		}
		if (vres > max_vert_dbl)
			vres = max_vert_dbl;
		prefs->gfx_size_win.height = (prefs->gfx_ycenter_size * 2) >> (VRES_QUAD - vres);
		prefs->gfx_filter_autoscale = 0;
	}
	write_log(_T("-> %dx%d\n"), prefs->gfx_size_win.width, prefs->gfx_size_win.height);
}

static int getmult (int mult)
{
	if (mult >= 4 * 256)
		return 2;
	if (mult == 2 * 256)
		return 1;
	if (mult >= 1 * 256)
		return 0;
	if (mult >= 256 / 2)
		return -1;
	if (mult >= 256 / 4)
		return -2;
	return 0;
}

static int shift (int val, int shift)
{
	if (shift > 0)
		val >>= shift;
	else if (shift < 0)
		val <<= -shift;
	return val;
}

static void get_screenmode (struct RPScreenMode *sm, struct uae_prefs *p)
{
	int m, cf;
	int full = 0;
	int hres, vres;
	int totalhdbl = -1, totalvdbl = -1;
	int hmult, vmult;

	hres = p->gfx_resolution;
	vres = p->gfx_vresolution;
	hmult = p->gfx_filter_horiz_zoom_mult > 0 ? 1000 * 256 / p->gfx_filter_horiz_zoom_mult : 256;
	vmult = p->gfx_filter_vert_zoom_mult > 0 ? 1000 * 256 / p->gfx_filter_vert_zoom_mult : 256;

	sm->hGuestWindow = guestwindow;
	m = RP_SCREENMODE_1X;
	cf = 0;

	if (WIN32GFX_IsPicassoScreen ()) {

		full = p->gfx_apmode[1].gfx_fullscreen;
		sm->lClipTop = -1;
		sm->lClipLeft = -1;
		sm->lClipWidth = -1;//picasso96_state.Width;
		sm->lClipHeight = -1;//picasso96_state.Height;

	} else {

		full = p->gfx_apmode[0].gfx_fullscreen;

		totalhdbl = hres;
		if (hres > max_horiz_dbl)
			hres = max_horiz_dbl;
		hres += getmult (hmult);

		totalvdbl = vres;
		if (vres > max_vert_dbl)
			vres = max_vert_dbl;
		vres += getmult (vmult);

		if (hres > RES_SUPERHIRES)
			hres = RES_SUPERHIRES;
		if (vres > VRES_QUAD)
			vres = VRES_QUAD;

		if (hres == RES_HIRES)
			m = RP_SCREENMODE_2X;
		else if (hres >= RES_SUPERHIRES)
			m = RP_SCREENMODE_4X;

		if (log_rp)
			write_log (_T("GET_RPSM: hres=%d (%d) vres=%d (%d) full=%d xcpos=%d ycpos=%d w=%d h=%d vm=%d hm=%d\n"),
				totalhdbl, hres, totalvdbl, vres, full,
				p->gfx_xcenter_pos,  p->gfx_ycenter_pos,
				p->gfx_size_win.width, p->gfx_size_win.height,
				hmult, vmult);

		sm->lClipLeft = p->gfx_xcenter_pos < 0 ? -1 : p->gfx_xcenter_pos;
		sm->lClipTop = p->gfx_ycenter_pos < 0 ? -1 : p->gfx_ycenter_pos;
		sm->lClipWidth = p->gfx_xcenter_size <= 0 ? -1 : p->gfx_xcenter_size;
		sm->lClipHeight = p->gfx_ycenter_size <= 0 ? -1 : p->gfx_ycenter_size;

		if (p->gfx_filter_scanlines || p->gfx_scanlines)
			m |= RP_SCREENMODE_SCANLINES;

		if (p->gfx_xcenter_pos == 0 && p->gfx_ycenter_pos == 0)
			cf |= RP_CLIPFLAGS_NOCLIP;
		else if (p->gfx_filter_autoscale == AUTOSCALE_RESIZE || p->gfx_filter_autoscale == AUTOSCALE_NORMAL)
			cf |= RP_CLIPFLAGS_AUTOCLIP;
	}
	if (full) {
		m &= ~RP_SCREENMODE_DISPLAYMASK;
		m |= p->gfx_display << 8;
	}
	if (full > 1)
		m |= RP_SCREENMODE_FULLWINDOW;

	sm->dwScreenMode = m  | (storeflags & (RP_SCREENMODE_STRETCH | RP_SCREENMODE_SUBPIXEL));
	sm->lTargetHeight = 0;
	sm->lTargetWidth = 0;
	if ((storeflags & RP_SCREENMODE_MODEMASK) == RP_SCREENMODE_XX) {
		sm->dwScreenMode &= ~RP_SCREENMODE_MODEMASK;
		sm->dwScreenMode |= RP_SCREENMODE_XX;
	} else if ((storeflags & RP_SCREENMODE_MODEMASK) == RP_SCREENMODE_WW) {
		sm->dwScreenMode &= ~RP_SCREENMODE_MODEMASK;
		sm->dwScreenMode = RP_SCREENMODE_WW;
		sm->lTargetWidth = p->gfx_size_win.width;
		sm->lTargetHeight = p->gfx_size_win.height;
	}
	sm->dwClipFlags = cf;

	if (log_rp)
		write_log (_T("GET_RPSM: %08X %dx%d %dx%d hres=%d (%d) vres=%d (%d) disp=%d fs=%d\n"),
			sm->dwScreenMode, sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight,
			totalhdbl, hres, totalvdbl, vres, p->gfx_display, full);
}

static void set_screenmode (struct RPScreenMode *sm, struct uae_prefs *p)
{
	int smm = RP_SCREENMODE_SCALE (sm->dwScreenMode);
	int display = RP_SCREENMODE_DISPLAY (sm->dwScreenMode);
	int fs = 0;
	int hdbl = RES_HIRES, vdbl = VRES_DOUBLE;
	int hres, vres;
	int hmult = 1, vmult = 1;
	struct MultiDisplay *disp;
	bool keepaspect = (sm->dwScreenMode & RP_SCREENMODE_SCALING_SUBPIXEL) && !(sm->dwScreenMode & RP_SCREENMODE_SCALING_STRETCH);
	bool stretch = (sm->dwScreenMode & RP_SCREENMODE_SCALING_STRETCH) != 0;
	bool forcesize = smm == RP_SCREENMODE_WW && sm->lTargetWidth > 0 && sm->lTargetHeight > 0;
	bool integerscale = !(sm->dwScreenMode & RP_SCREENMODE_SCALING_SUBPIXEL) && !(sm->dwScreenMode & RP_SCREENMODE_SCALING_STRETCH) && smm >= RP_SCREENMODE_SCALE_TARGET;
	int width, height;

	storeflags = sm->dwScreenMode;
	minimized = 0;
	if (display) {
		p->gfx_display = display;
		p->gfx_display_name[0] = 0;
		if (sm->dwScreenMode & RP_SCREENMODE_FULLWINDOW)
			fs = 2;
		else
			fs = 1;
	}
	p->gfx_filter_autoscale = AUTOSCALE_CENTER;
	disp = getdisplay (p);

	if (log_rp) {
		write_log (_T("SET_RPSM: %08X %dx%d %dx%d hres=%d vres=%d disp=%d fs=%d smm=%d\n"),
			sm->dwScreenMode, sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight,
			hdbl, vdbl, display, fs, smm);
		write_log (_T("aspect=%d stretch=%d force=%d integer=%d\n"),
			keepaspect, stretch, forcesize, integerscale);
	}

	if (!WIN32GFX_IsPicassoScreen ()) {

		hdbl = RES_LORES;
		vdbl = VRES_NONDOUBLE;
		if (smm == RP_SCREENMODE_2X || smm == RP_SCREENMODE_3X) {
			hdbl = RES_HIRES;
			vdbl = VRES_DOUBLE;
		} else if (smm == RP_SCREENMODE_4X) {
			hdbl = RES_SUPERHIRES;
			vdbl = VRES_DOUBLE;
		}
		if (smm > RP_SCREENMODE_4X || smm == RP_SCREENMODE_XX) {
			hdbl = max_horiz_dbl;
			vdbl = max_vert_dbl;
		}

		hres = hdbl;
		if (hres > max_horiz_dbl) {
			hmult = 1 << (hres - max_horiz_dbl);
			hres = max_horiz_dbl;
		}
		p->gfx_resolution = hres;

		vres = vdbl;
		if (vres > max_vert_dbl) {
			vmult = 1 << (vres - max_vert_dbl);
			vres = max_vert_dbl;
		}
		p->gfx_vresolution = vres;

		if (sm->lClipWidth <= 0)
			p->gfx_size_win.width = shift (AMIGA_WIDTH_MAX, -hdbl);
		else
			p->gfx_size_win.width = sm->lClipWidth >> (RES_MAX - hdbl);

		if (sm->lClipHeight <= 0) {
			p->gfx_size_win.height = shift (AMIGA_HEIGHT_MAX, -vdbl);
		} else {
			p->gfx_size_win.height = sm->lClipHeight >> (VRES_MAX - vdbl);
		}

		if (forcesize) {
			width = p->gfx_size_win.width = sm->lTargetWidth;
			height = p->gfx_size_win.height = sm->lTargetHeight;
		}

		if (fs == 1) {
			width = p->gfx_size_fs.width = p->gfx_size_win.width;
			height = p->gfx_size_fs.height = p->gfx_size_win.height;
		} else if (fs == 2) {
			width = p->gfx_size_fs.width = disp->rect.right - disp->rect.left;
			height = p->gfx_size_fs.height = disp->rect.bottom - disp->rect.top;
		}
	}

	p->gfx_apmode[1].gfx_fullscreen = fs;
	p->gfx_apmode[0].gfx_fullscreen = fs;
	p->win32_rtgscaleifsmall = fs == 2;
	p->gfx_xcenter_pos = sm->lClipLeft;
	p->gfx_ycenter_pos = sm->lClipTop;
	p->gfx_xcenter_size = -1;
	p->gfx_ycenter_size = -1;

	if (stretch) {
		hmult = vmult = 0;
	} else if (integerscale) {
		hmult = vmult = 1;
		p->gfx_filter_autoscale = AUTOSCALE_INTEGER;
		if (sm->lClipWidth > 0)
			p->gfx_xcenter_size = sm->lClipWidth;
		if (sm->lClipHeight > 0)
			p->gfx_ycenter_size = sm->lClipHeight;

	}

	if (keepaspect) {
		p->gfx_filter_aspect = -1;
		p->gfx_filter_keep_aspect = 1;
	} else {
		p->gfx_filter_aspect = 0;
		p->gfx_filter_keep_aspect = 0;
	}

	if (log_rp)
		write_log(_T("%dx%d %dx%d %dx%d %08x HM=%d VM=%d\n"),
			sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight, sm->lTargetWidth, sm->lTargetHeight, sm->dwClipFlags, hmult, vmult);

	if (!integerscale) {
		if (sm->dwClipFlags & RP_CLIPFLAGS_AUTOCLIP) {
			if (!forcesize)
				p->gfx_filter_autoscale = AUTOSCALE_RESIZE;
			else
				p->gfx_filter_autoscale = AUTOSCALE_NORMAL;
			p->gfx_xcenter_pos = -1;
			p->gfx_ycenter_pos = -1;
			p->gfx_xcenter_size = -1;
			p->gfx_ycenter_size = -1;
		} else if (sm->dwClipFlags & RP_CLIPFLAGS_NOCLIP) {
			p->gfx_filter_autoscale = AUTOSCALE_STATIC_MAX;
			p->gfx_xcenter_pos = -1;
			p->gfx_ycenter_pos = -1;
			p->gfx_xcenter_size = -1;
			p->gfx_ycenter_size = -1;
			if (!forcesize) {
				p->gfx_size_win.width = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
				p->gfx_size_win.height = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;;
			}
		}

		if (sm->lClipWidth > 0)
			p->gfx_xcenter_size = sm->lClipWidth;
		if (sm->lClipHeight > 0)
			p->gfx_ycenter_size = sm->lClipHeight;

		if ((p->gfx_xcenter_pos >= 0 && p->gfx_ycenter_pos >= 0) || (p->gfx_xcenter_size > 0 && p->gfx_ycenter_size > 0)) {
			p->gfx_filter_autoscale = AUTOSCALE_MANUAL;
		}
	}

	p->gfx_filter_horiz_zoom_mult = hmult > 0 ? 1000 / hmult : hmult;
	p->gfx_filter_vert_zoom_mult = vmult > 0 ? 1000 / vmult : vmult;

	p->gfx_filter_scanlines = 0;
	p->gfx_scanlines = 0;
	if (sm->dwScreenMode & RP_SCREENMODE_SCANLINES) {
		p->gfx_scanlines = 1;
		p->gfx_filter_scanlines = 8;
		p->gfx_filter_scanlinelevel = 8;
		p->gfx_filter_scanlineratio = (1 << 4) | 1;
	}

	if (log_rp)
		write_log (_T("WW=%d WH=%d FW=%d FH=%d HM=%d VM=%d XP=%d YP=%d XS=%d YS=%d AS=%d AR=%d,%d\n"),
			p->gfx_size_win.width, p->gfx_size_win.height,
			p->gfx_size_fs.width, p->gfx_size_fs.height,
			p->gfx_filter_horiz_zoom_mult, p->gfx_filter_vert_zoom_mult,
			p->gfx_xcenter_pos, p->gfx_ycenter_pos,
			p->gfx_xcenter_size, p->gfx_ycenter_size,
			p->gfx_filter_autoscale, p->gfx_filter_aspect, p->gfx_filter_keep_aspect);


	updatewinfsmode (p);
	hwndset = 0;
	config_changed = 1;
}

static LRESULT CALLBACK RPHostMsgFunction2 (UINT uMessage, WPARAM wParam, LPARAM lParam,
	LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam)
{
	if (log_rp) {
		write_log (_T("RPFUNC(%s [%d], %08x, %08x, %08x, %d, %08x)\n"),
		getmsg (uMessage), uMessage - WM_APP, wParam, lParam, pData, dwDataSize, lMsgFunctionParam);
		if (uMessage == RPIPCHM_DEVICECONTENT) {
			struct RPDeviceContent *dc = (struct RPDeviceContent*)pData;
			write_log (_T(" Cat=%d Num=%d Flags=%08x '%s'\n"),
				dc->btDeviceCategory, dc->btDeviceNumber, dc->dwFlags, dc->szContent);
		}
	}

	switch (uMessage)
	{
	default:
		write_log (_T("RP: Unknown or unsupported command %x\n"), uMessage);
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
			config_changed = 1;
		}
		return TRUE;
	case RPIPCHM_PAUSE:
		currentpausemode = pause_emulation;
		if (wParam ? 1 : 0 != pause_emulation ? 1 : 0) {
			pausemode (wParam ? -1 : 0);
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
			TCHAR *n = dc->szContent;
			int num = dc->btDeviceNumber;
			int ok = FALSE;
			switch (dc->btDeviceCategory)
			{
			case RP_DEVICE_FLOPPY:
				if (n == NULL || n[0] == 0)
					disk_eject (num);
				else
					disk_insert (num, n);
				ok = TRUE;
				break;
			case RP_DEVICE_INPUTPORT:
				ok = port_insert (num, dc->dwInputDevice, dc->dwFlags, n);
				if (ok)
					inputdevice_updateconfig (&currprefs);
				break;
			case RP_DEVICE_CD:
				ok = cd_insert (num, n);
				break;
			}
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
			TCHAR out[256];
			TCHAR *s = (WCHAR*)pData;
			int idx = -1;
			for (;;) {
				int ret;
				out[0] = 0;
				ret = cfgfile_modify (idx++, s, _tcslen (s), out, sizeof out / sizeof (TCHAR));
				if (ret >= 0)
					break;
			}
			return TRUE;
		}
	case RPIPCHM_SCREENCAPTURE:
		{
			extern int screenshotf (const TCHAR *spath, int mode, int doprepare);
			extern int screenshotmode;
			int ok;
			int ossm = screenshotmode;
			TCHAR *s = (TCHAR*)pData;
			screenshotmode = 0;
			ok = screenshotf (s, 1, 1);
			screenshotmode = ossm;
			return ok ? TRUE : FALSE;
		}
	case RPIPCHM_SAVESTATE:
		{
			TCHAR *s = (TCHAR*)pData;
			DWORD ret = FALSE;
			if (s == NULL) {
				savestate_initsave (NULL, 0, TRUE, true);
				return 1;
			}
			if (vpos == 0) {
				savestate_initsave (_T(""), 1, TRUE, true);
				save_state (s, _T("AmigaForever"));
				ret = 1;
			} else {
				//savestate_initsave (s, 1, TRUE);
				//ret = -1;
			}
			return ret;
		}
	case RPIPCHM_LOADSTATE:
		{
			TCHAR *s = (WCHAR*)pData;
			DWORD ret = FALSE;
			DWORD attr = GetFileAttributes (s);
			if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
				savestate_state = STATE_DORESTORE;
				_tcscpy (savestate_fname, s);
				ret = -1;
			}
			return ret;
		}
	case RPIPCHM_DEVICEREADWRITE:
		{
			DWORD ret = FALSE;
			int device = LOBYTE(wParam);
			if (device == RP_DEVICE_FLOPPY) {
				int num = HIBYTE(wParam);
				if (lParam == RP_DEVICE_READONLY || lParam == RP_DEVICE_READWRITE) {
					ret = disk_setwriteprotect (&currprefs, num, currprefs.floppyslots[num].df, lParam == RP_DEVICE_READONLY);
				}
			}
			return ret ? (LPARAM)1 : 0;
		}
	case RPIPCHM_FLUSH:
		return 1;
	case RPIPCHM_QUERYSCREENMODE:
		{
			screenmode_request = true;
			return 1;
		}
	case RPIPCHM_GUESTAPIVERSION:
		{
			return MAKELONG(3, 0);
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

static int rp_hostversion (int *ver, int *rev, int *build)
{
	LRESULT lr = 0;
	if (!RPSendMessagex (RPIPCGM_HOSTVERSION, 0, 0, NULL, 0, &guestinfo, &lr))
		return 0;
	*ver = RP_HOSTVERSION_MAJOR(lr);
	*rev = RP_HOSTVERSION_MINOR(lr);
	*build = RP_HOSTVERSION_BUILD(lr);
	return 1;
}

HRESULT rp_init (void)
{
	HRESULT hr;

	write_log (_T("rp_init()\n"));
	hr = RPInitializeGuest (&guestinfo, hInst, rp_param, RPHostMsgFunction, 0);
	if (SUCCEEDED (hr)) {
		initialized = TRUE;
		rp_version = rp_revision = rp_build = -1;
		rp_hostversion (&rp_version, &rp_revision, &rp_build);
		write_log (_T("rp_init('%s') succeeded. Version: %d.%d.%d\n"), rp_param, rp_version, rp_revision, rp_build);
	} else {
		write_log (_T("rp_init('%s') failed, error code %08x\n"), rp_param, hr);
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
	RPPostMessagex (RPIPCGM_CLOSED, 0, 0, &guestinfo);
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

extern int rp_input_enum (struct RPInputDeviceDescription *desc, int);

static void sendenum (void)
{
	TCHAR tmp[MAX_DPATH];
	TCHAR *p1, *p2;
	struct RPInputDeviceDescription desc;
	int cnt;

	WIN32GUI_LoadUIString (IDS_KEYJOY, tmp, sizeof tmp / sizeof (TCHAR));
	_tcscat (tmp, _T("\n"));
	p1 = tmp;
	cnt = 0;
	for (;;) {
		p2 = _tcschr (p1, '\n');
		if (p2 && _tcslen (p2) > 0) {
			TCHAR tmp2[100];
			*p2++ = 0;
			memset (&desc, 0, sizeof desc);
			_stprintf (tmp2, _T("KeyboardLayout%d"), cnt);
			_tcscpy (desc.szHostInputID, tmp2);
			_tcscpy (desc.szHostInputName, p1);
			desc.dwHostInputType= RP_HOSTINPUT_KEYJOY_MAP1 + cnt;
			desc.dwInputDeviceFeatures = RP_FEATURE_INPUTDEVICE_JOYSTICK;
			if (cnt == 0)
				desc.dwInputDeviceFeatures |= RP_FEATURE_INPUTDEVICE_JOYPAD;
			if (log_rp)
				write_log(_T("Enum%d: '%s' '%s'\n"), cnt, desc.szHostInputName, desc.szHostInputID);
			RPSendMessagex (RPIPCGM_INPUTDEVICE, 0, 0, &desc, sizeof desc, &guestinfo, NULL);
			cnt++;
			p1 = p2;
		} else {
			break;
		}
	}
	cnt = 0;
	while ((cnt = rp_input_enum (&desc, cnt)) >= 0) {
		if (log_rp)
			write_log(_T("Enum%d: '%s' '%s' (%x/%x)\n"),
				cnt, desc.szHostInputName, desc.szHostInputID, desc.dwHostInputVendorID, desc.dwHostInputProductID);
		RPSendMessagex (RPIPCGM_INPUTDEVICE, 0, 0, &desc, sizeof desc, &guestinfo, NULL);
	}
}

static void sendfeatures (void)
{
	DWORD feat;

	feat = RP_FEATURE_POWERLED | RP_FEATURE_SCREEN1X | RP_FEATURE_FULLSCREEN;
	feat |= RP_FEATURE_PAUSE | RP_FEATURE_TURBO | RP_FEATURE_VOLUME | RP_FEATURE_SCREENCAPTURE;
	feat |= RP_FEATURE_STATE | RP_FEATURE_SCANLINES | RP_FEATURE_DEVICEREADWRITE;
	feat |= RP_FEATURE_RESIZE_SUBPIXEL | RP_FEATURE_RESIZE_STRETCH;
	if (!WIN32GFX_IsPicassoScreen ())
		feat |= RP_FEATURE_SCREEN2X | RP_FEATURE_SCREEN4X;
	feat |= RP_FEATURE_INPUTDEVICE_MOUSE;
	feat |= RP_FEATURE_INPUTDEVICE_JOYSTICK;
	feat |= RP_FEATURE_INPUTDEVICE_GAMEPAD;
	feat |= RP_FEATURE_INPUTDEVICE_JOYPAD;
	feat |= RP_FEATURE_INPUTDEVICE_ANALOGSTICK;
	feat |= RP_FEATURE_INPUTDEVICE_LIGHTPEN;
	RPSendMessagex (RPIPCGM_FEATURES, feat, 0, NULL, 0, &guestinfo, NULL);
}

static int gethdnum (int n)
{
	struct uaedev_config_info *uci = &currprefs.mountconfig[n];
	int num = -1;
	if (uci->controller == HD_CONTROLLER_UAE) {
		num = n;
	} else if (uci->controller <= HD_CONTROLLER_IDE3 ) {
		num = uci->controller - HD_CONTROLLER_IDE0;
	} else if (uci->controller <= HD_CONTROLLER_SCSI6) {
		num = uci->controller - HD_CONTROLLER_SCSI0;
	}
	return num;
}

void rp_fixup_options (struct uae_prefs *p)
{
	int i;
	struct RPScreenMode sm;

	if (!initialized)
		return;

	write_log (_T("rp_fixup_options(escapekey=%d,escapeholdtime=%d,screenmode=%d,inputmode=%d)\n"),
		rp_rpescapekey, rp_rpescapeholdtime, rp_screenmode, rp_inputmode);

	max_horiz_dbl = currprefs.gfx_max_horizontal;
	max_vert_dbl = currprefs.gfx_max_vertical;
	maxjports = (rp_version * 256 + rp_revision) >= 2 * 256 + 3 ? MAX_JPORTS : 2;

	write_log (_T("w=%dx%d fs=%dx%d pos=%dx%d %dx%d HV=%d,%d J=%d\n"),
		p->gfx_size_win.width, p->gfx_size_win.height,
		p->gfx_size_fs.width, p->gfx_size_fs.height,
		p->gfx_xcenter_pos, p->gfx_ycenter_pos,
		p->gfx_xcenter_size, p->gfx_ycenter_size,
		max_horiz_dbl, max_vert_dbl, maxjports);

	sendfeatures ();
	sendenum ();

	changed_prefs.win32_borderless = currprefs.win32_borderless = 1;
	rp_filter_default = rp_filter = currprefs.gfx_filter;
	if (rp_filter == 0)
		rp_filter = UAE_FILTER_NULL;

	fixup_size (p);
	get_screenmode (&sm, p);
	sm.dwScreenMode = rp_screenmode;
	set_screenmode (&sm, &currprefs);
	set_screenmode (&sm, &changed_prefs);

	/* floppy drives */
	floppy_mask = 0;
	for (i = 0; i < 4; i++) {
		if (p->floppyslots[i].dfxtype >= 0)
			floppy_mask |= 1 << i;
	}
	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_FLOPPY, floppy_mask, NULL, 0, &guestinfo, NULL);

	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_INPUTPORT, (1 << maxjports) - 1, NULL, 0, &guestinfo, NULL);
	rp_input_change (0);
	rp_input_change (1);
	rp_input_change (2);
	rp_input_change (3);
	gameportmask[0] = gameportmask[1] = gameportmask[2] = gameportmask[3] = 0;

	hd_mask = 0;
	cd_mask = 0;
	for (i = 0; i < currprefs.mountitems; i++) {
		int num = gethdnum (i);
		if (num >= 0)
			hd_mask |= 1 << num;
	}
	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_HD, hd_mask, NULL, 0, &guestinfo, NULL);
	if (hd_mask) {
		for (i = 0; i < currprefs.mountitems; i++) {
			struct uaedev_config_info *uci = &currprefs.mountconfig[i];
			int num = gethdnum (i);
			if (num >= 0 && ((1 << num) & hd_mask))
				rp_harddrive_image_change (num, uci->readonly, uci->rootdir);
		}
	}

	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (p->cdslots[i].inuse)
			cd_mask |= 1 << i;
	}
	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICE_CD, cd_mask, NULL, 0, &guestinfo, NULL);
	if (cd_mask) {
		for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
			if (p->cdslots[i].inuse)
				rp_cd_image_change (i, p->cdslots[i].name);
		}
	}

	rp_update_volume (&currprefs);
	rp_turbo_cpu (currprefs.turbo_emulation);
	rp_turbo_floppy (currprefs.floppy_speed == 0);
	for (i = 0; i <= 4; i++)
		rp_update_leds (i, 0, 0);
	config_changed = 1;
}

static void rp_device_writeprotect (int dev, int num, bool writeprotected)
{
	if (!cando ())
		return;
	if (rp_version * 256 + rp_revision < 2 * 256 + 3)
		return;
	RPSendMessagex (RPIPCGM_DEVICEREADWRITE, MAKEWORD(dev, num), writeprotected ? RP_DEVICE_READONLY : RP_DEVICE_READWRITE, NULL, 0, &guestinfo, NULL);
}

static void rp_device_change (int dev, int num, int mode, bool readonly, const TCHAR *content)
{
	struct RPDeviceContent dc = { 0 };

	if (!cando ())
		return;
	if (recursive_device)
		return;
	dc.btDeviceCategory = dev;
	dc.btDeviceNumber = num;
	dc.dwInputDevice = mode;
	if (content)
		_tcscpy (dc.szContent, content);
	if (log_rp)
		write_log (_T("RPIPCGM_DEVICECONTENT cat=%d num=%d type=%d '%s'\n"),
		dc.btDeviceCategory, dc.btDeviceNumber, dc.dwInputDevice, dc.szContent);
	RPSendMessagex (RPIPCGM_DEVICECONTENT, 0, 0, &dc, sizeof(struct RPDeviceContent), &guestinfo, NULL);
}

void rp_input_change (int num)
{
	int j = jsem_isjoy (num, &currprefs);
	int m = jsem_ismouse (num, &currprefs);
	int k = jsem_iskbdjoy (num, &currprefs);
	TCHAR name[MAX_DPATH];
	int mode;

	if (num >= maxjports)
		return;

	name[0] = 0;
	if (k >= 0) {
		_stprintf (name, _T("KeyboardLayout%d"), k);
	} else if (j >= 0) {
		_tcscpy (name, inputdevice_get_device_unique_name (IDTYPE_JOYSTICK, j));
	} else if (m >= 0) {
		_tcscpy (name, inputdevice_get_device_unique_name (IDTYPE_MOUSE, m));
	}
	mode = RP_INPUTDEVICE_EMPTY;
	for (int i = 0; inputdevmode[i * 2]; i++) {
		if (inputdevmode[i * 2 + 1] == currprefs.jports[num].mode) {
			mode = inputdevmode[i * 2 + 0];
			break;
		}
	}
	if (log_rp)
		write_log(_T("PORT%d: '%s':%d\n"), num, name, mode);
	rp_device_change (RP_DEVICECATEGORY_INPUTPORT, num, mode, false, name);
}
void rp_disk_image_change (int num, const TCHAR *name, bool writeprotected)
{
	rp_device_change (RP_DEVICE_FLOPPY, num, 0, writeprotected == false, name);
	rp_device_writeprotect (RP_DEVICECATEGORY_FLOPPY, num, writeprotected);
}
void rp_harddrive_image_change (int num, bool readonly, const TCHAR *name)
{
	rp_device_change (RP_DEVICECATEGORY_HD, num, 0, readonly, name);
}
void rp_cd_image_change (int num, const TCHAR *name)
{
	rp_device_change (RP_DEVICECATEGORY_CD, num, 0, 0, name);
}

void rp_floppy_device_enable (int num, bool enabled)
{
	if (!cando ())
		return;
	if (enabled)
		floppy_mask |= 1 << num;
	else
		floppy_mask &= ~(1 << num);
	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICECATEGORY_FLOPPY, floppy_mask, NULL, 0, &guestinfo, NULL);
}

void rp_hd_device_enable (int num, bool enabled)
{
	if (!cando ())
		return;
	if (enabled)
		hd_mask |= 1 << num;
	else
		hd_mask &= ~(1 << num);
	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICECATEGORY_HD, hd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_cd_device_enable (int num, bool enabled)
{
	if (!cando ())
		return;
	if (enabled)
		cd_mask |= 1 << num;
	else
		cd_mask &= ~(1 << num);
	RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICECATEGORY_CD, cd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_floppy_track (int floppy, int track)
{
	static int oldtrack[4];
	if (!cando ())
		return;
	if (oldtrack[floppy] == track)
		return;
	oldtrack[floppy] = track;
	RPPostMessagex (RPIPCGM_DEVICESEEK, MAKEWORD (RP_DEVICECATEGORY_FLOPPY, floppy), track, &guestinfo);
}

void rp_update_leds (int led, int onoff, int write)
{
	static int oldled[5];
	int ledstate;

	if (!cando ())
		return;
	if (led < 0 || led > 4)
		return;
	switch (led)
	{
	case 0:
		ledstate = onoff >= 250 ? 100 : onoff * 10 / 26;
		if (ledstate == oldled[led])
			return;
		oldled[led] = ledstate;
		RPSendMessage (RPIPCGM_POWERLED, ledstate, 0, NULL, 0, &guestinfo, NULL);
		break;
	case 1:
	case 2:
	case 3:
	case 4:
		ledstate = onoff ? 1 : 0;
		ledstate |= write ? 2 : 0;
		if (ledstate == oldled[led])
			return;
		oldled[led] = ledstate;
		RPPostMessagex (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_FLOPPY, led - 1),
			MAKELONG ((ledstate & 1) ? -1 : 0, (ledstate & 2) ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ) , &guestinfo);
		break;
	}
}

void rp_update_gameport (int port, int mask, int onoff)
{
	if (!cando ())
		return;
	if (port < 0 || port >= maxjports)
		return;
	if (rp_version * 256 + rp_revision < 2 * 256 + 3)
		return;
	int old = gameportmask[port];
	if (onoff)
		gameportmask[port] |= mask;
	else
		gameportmask[port] &= ~mask;
	if (old != gameportmask[port]) {
		RPPostMessagex (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_INPUTPORT, port),
			gameportmask[port], &guestinfo);
	}
}

void rp_hd_activity (int num, int onoff, int write)
{
	static int oldleds[MAX_TOTAL_SCSI_DEVICES];
	static int state;

	if (!cando ())
		return;
	if (num < 0)
		return;
	state = onoff ? 1 : 0;
	state |= write ? 2 : 0;
	if (state == oldleds[num])
		return;
	oldleds[num] = state;
	if (state & 1) {
		RPPostMessagex (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_HD, num),
			MAKELONG (200, (state & 2) ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ), &guestinfo);
	}
}

void rp_cd_activity (int num, int onoff)
{
	if (!cando ())
		return;
	if (num < 0)
		return;
	if (onoff && !(cd_mask & (1 << num))) {
		cd_mask |= 1 << num;
		RPSendMessagex (RPIPCGM_DEVICES, RP_DEVICECATEGORY_CD, cd_mask, NULL, 0, &guestinfo, NULL);
	}
	if (onoff) {
		RPPostMessage (RPIPCGM_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_CD, num),
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

void rp_turbo_cpu (int active)
{
	if (!cando ())
		return;
	if (recursive_device)
		return;
	RPSendMessagex (RPIPCGM_TURBO, RP_TURBO_CPU, active ? RP_TURBO_CPU : 0, NULL, 0, &guestinfo, NULL);
}

void rp_turbo_floppy (int active)
{
	if (!cando ())
		return;
	if (recursive_device)
		return;
	RPSendMessagex (RPIPCGM_TURBO, RP_TURBO_FLOPPY, active ? RP_TURBO_FLOPPY : 0, NULL, 0, &guestinfo, NULL);
}

void rp_set_hwnd (HWND hWnd)
{
	struct RPScreenMode sm = { 0 };

	if (!initialized)
		return;
	guestwindow = hWnd;
	get_screenmode (&sm, &currprefs);
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
	if (screenmode_request) {
		struct RPScreenMode sm = { 0 };
		get_screenmode (&sm, &currprefs);
		RPSendMessagex (RPIPCGM_SCREENMODE, 0, 0, &sm, sizeof sm, &guestinfo, NULL);
		screenmode_request = false;
	}
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
