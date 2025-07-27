
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include <windows.h>

#include "cloanto/RetroPlatformGuestIPC.h"
#include "cloanto/RetroPlatformIPC.h"

#include "sysconfig.h"
#include "sysdeps.h"
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
#include "drawing.h"
#include "resource.h"
#include "gui.h"
#include "keyboard.h"
#include "rp.h"
#include "direct3d.h"
#include "debug.h"
#include "devices.h"

#define RP_SCREENMODE_SCALE_MAX_X 128
#define RP_SCREENMODE_SCALE_MAX_X_MASK 127

static int initialized;
static RPGUESTINFO guestinfo;
static int maxjports;

TCHAR *rp_param = NULL;
int rp_rpescapekey = 0x01;
int rp_rpescapeholdtime = 600;
int rp_screenmode = 0;
int rp_inputmode = 0;
int rp_printer = 0;
int rp_modem = 0;
int log_rp = 2;
static int rp_revision, rp_version, rp_build;
static int rp_printeropen = 0;
static int rp_modemopen = 0;
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
static int screenmode_request;
static HWND guestwindow;
static int hwndset_delay;
static int sendmouseevents;
static int mouseevent_x, mouseevent_y, mouseevent_buttons;
static uae_u64 delayed_refresh;
static bool interpolation_v102;

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

static uae_u64 gett(void)
{
	SYSTEMTIME st;
	FILETIME ft;
	ULARGE_INTEGER li;

	GetSystemTime(&st);
	if (!SystemTimeToFileTime(&st, &ft))
		return 0;
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	return li.QuadPart / 10000;
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
	case RP_IPC_TO_HOST_PRIVATE_REGISTER: return _T("RP_IPC_TO_HOST_PRIVATE_REGISTER");
	case RP_IPC_TO_HOST_FEATURES: return _T("RP_IPC_TO_HOST_FEATURES");
	case RP_IPC_TO_HOST_CLOSED: return _T("RP_IPC_TO_HOST_CLOSED");
	case RP_IPC_TO_HOST_ACTIVATED: return _T("RP_IPC_TO_HOST_ACTIVATED");
	case RP_IPC_TO_HOST_DEACTIVATED: return _T("RP_IPC_TO_HOST_DEACTIVATED");
	case RP_IPC_TO_HOST_SCREENMODE: return _T("RP_IPC_TO_HOST_SCREENMODE");
	case RP_IPC_TO_HOST_POWERLED: return _T("RP_IPC_TO_HOST_POWERLED");
	case RP_IPC_TO_HOST_DEVICES: return _T("RP_IPC_TO_HOST_DEVICES");
	case RP_IPC_TO_HOST_DEVICEACTIVITY: return _T("RP_IPC_TO_HOST_DEVICEACTIVITY");
	case RP_IPC_TO_HOST_MOUSECAPTURE: return _T("RP_IPC_TO_HOST_MOUSECAPTURE");
	case RP_IPC_TO_HOST_HOSTAPIVERSION: return _T("RP_IPC_TO_HOST_HOSTAPIVERSION");
	case RP_IPC_TO_HOST_PAUSE: return _T("RP_IPC_TO_HOST_PAUSE");
	case RP_IPC_TO_HOST_DEVICECONTENT: return _T("RP_IPC_TO_HOST_DEVICECONTENT");
	case RP_IPC_TO_HOST_TURBO: return _T("RP_IPC_TO_HOST_TURBO");
	case RP_IPC_TO_HOST_PING: return _T("RP_IPC_TO_HOST_PING");
	case RP_IPC_TO_HOST_VOLUME: return _T("RP_IPC_TO_HOST_VOLUME");
//	case RP_IPC_TO_HOST_ESCAPED: return _T("RP_IPC_TO_HOST_ESCAPED");
	case RP_IPC_TO_HOST_PARENT: return _T("RP_IPC_TO_HOST_PARENT");
	case RP_IPC_TO_HOST_DEVICESEEK: return _T("RP_IPC_TO_HOST_DEVICESEEK");
	case RP_IPC_TO_HOST_CLOSE: return _T("RP_IPC_TO_HOST_CLOSE");
	case RP_IPC_TO_HOST_DEVICEREADWRITE: return _T("RP_IPC_TO_HOST_DEVICEREADWRITE");
	case RP_IPC_TO_HOST_HOSTVERSION: return _T("RP_IPC_TO_HOST_HOSTVERSION");
	case RP_IPC_TO_HOST_INPUTDEVICE: return _T("RP_IPC_TO_HOST_INPUTDEVICE");
	case RP_IPC_TO_HOST_KEYBOARDLAYOUT: return _T("RP_IPC_TO_HOST_KEYBOARDLAYOUT");
	case RP_IPC_TO_HOST_MOUSEMOVE: return _T("RP_IPC_TO_HOST_MOUSEMOVE");
	case RP_IPC_TO_HOST_MOUSEBUTTON: return _T("RP_IPC_TO_HOST_MOUSEBUTTON");
	case RP_IPC_TO_HOST_EXECUTE_RESULT: return _T("RP_IPC_TO_HOST_EXECUTE_RESULT");

	case RP_IPC_TO_GUEST_CLOSE: return _T("RP_IPC_TO_GUEST_CLOSE");
	case RP_IPC_TO_GUEST_SCREENMODE: return _T("RP_IPC_TO_GUEST_SCREENMODE");
	case RP_IPC_TO_GUEST_SCREENCAPTURE: return _T("RP_IPC_TO_GUEST_SCREENCAPTURE");
	case RP_IPC_TO_GUEST_PAUSE: return _T("RP_IPC_TO_GUEST_PAUSE");
	case RP_IPC_TO_GUEST_DEVICECONTENT: return _T("RP_IPC_TO_GUEST_DEVICECONTENT");
	case RP_IPC_TO_GUEST_RESET: return _T("RP_IPC_TO_GUEST_RESET");
	case RP_IPC_TO_GUEST_TURBO: return _T("RP_IPC_TO_GUEST_TURBO");
	case RP_IPC_TO_GUEST_PING: return _T("RP_IPC_TO_GUEST_PING");
	case RP_IPC_TO_GUEST_VOLUME: return _T("RP_IPC_TO_GUEST_VOLUME");
//	case RP_IPC_TO_GUEST_ESCAPEKEY: return _T("RP_IPC_TO_GUEST_ESCAPEKEY");
	case RP_IPC_TO_GUEST_EVENT: return _T("RP_IPC_TO_GUEST_EVENT");
	case RP_IPC_TO_GUEST_MOUSECAPTURE: return _T("RP_IPC_TO_GUEST_MOUSECAPTURE");
	case RP_IPC_TO_GUEST_SAVESTATE: return _T("RP_IPC_TO_GUEST_SAVESTATE");
	case RP_IPC_TO_GUEST_LOADSTATE: return _T("RP_IPC_TO_GUEST_LOADSTATE");
	case RP_IPC_TO_GUEST_FLUSH: return _T("RP_IPC_TO_GUEST_FLUSH");
	case RP_IPC_TO_GUEST_DEVICEREADWRITE: return _T("RP_IPC_TO_GUEST_DEVICEREADWRITE");
	case RP_IPC_TO_GUEST_QUERYSCREENMODE: return _T("RP_IPC_TO_GUEST_QUERYSCREENMODE");
	case RP_IPC_TO_GUEST_GUESTAPIVERSION : return _T("RP_IPC_TO_GUEST_GUESTAPIVERSION");
	case RP_IPC_TO_GUEST_SHOWOPTIONS: return _T("RP_IPC_TO_GUEST_SHOWOPTIONS");
	case RP_IPC_TO_GUEST_DEVICEACTIVITY: return _T("RP_IPC_TO_GUEST_DEVICEACTIVITY");
	case RP_IPC_TO_GUEST_SCREENOVERLAY: return _T("RP_IPC_TO_GUEST_SCREENOVERLAY");
	case RP_IPC_TO_GUEST_DELETESCREENOVERLAY: return _T("RP_IPC_TO_GUEST_DELETESCREENOVERLAY");
	case RP_IPC_TO_GUEST_MOVESCREENOVERLAY: return _T("RP_IPC_TO_GUEST_MOVESCREENOVERLAY");
	case RP_IPC_TO_GUEST_SENDMOUSEEVENTS: return _T("RP_IPC_TO_GUEST_SENDMOUSEEVENTS");
	case RP_IPC_TO_GUEST_SHOWDEBUGGER: return _T("RP_IPC_TO_GUEST_SHOWDEBUGGER");
	case RP_IPC_TO_GUEST_EXECUTE: return _T("RP_IPC_TO_GUEST_EXECUTE");
	case RP_IPC_TO_GUEST_DEVICEWRITEBYTE: return _T("RP_IPC_TO_GUEST_DEVICEWRITEBYTE");
	case RP_IPC_TO_GUEST_DEVICESETSIGNALS: return _T("RP_IPC_TO_GUEST_DEVICESETSIGNALS");
	default: return _T("UNKNOWN");
	}
}

static void trimws (TCHAR *s)
{
	/* Delete trailing whitespace.  */
	size_t len = _tcslen (s);
	while (len > 0 && _tcscspn (s + len - 1, _T("\t \r\n")) == 0)
		s[--len] = '\0';
}


static uae_u32 dactmask[4];
static uae_u32 dacttype[4];

static const int rp0_joystick[] = {
	INPUTEVENT_JOY1_RIGHT, INPUTEVENT_JOY1_LEFT,
	INPUTEVENT_JOY1_DOWN, INPUTEVENT_JOY1_UP,
	INPUTEVENT_JOY1_FIRE_BUTTON,
	INPUTEVENT_JOY1_2ND_BUTTON,
	-1
};
static const int rp0_pad[] = {
	INPUTEVENT_JOY1_RIGHT, INPUTEVENT_JOY1_LEFT,
	INPUTEVENT_JOY1_DOWN, INPUTEVENT_JOY1_UP,
	INPUTEVENT_JOY1_FIRE_BUTTON,
	INPUTEVENT_JOY1_2ND_BUTTON,
	INPUTEVENT_JOY1_3RD_BUTTON,
	-1
};
static const int rp0_cd32[] = {
	INPUTEVENT_JOY1_RIGHT, INPUTEVENT_JOY1_LEFT,
	INPUTEVENT_JOY1_DOWN, INPUTEVENT_JOY1_UP,
	INPUTEVENT_JOY1_CD32_RED,
	INPUTEVENT_JOY1_CD32_BLUE,
	INPUTEVENT_JOY1_CD32_YELLOW,
	INPUTEVENT_JOY1_CD32_GREEN,
	INPUTEVENT_JOY1_CD32_PLAY,
	INPUTEVENT_JOY1_CD32_RWD,
	INPUTEVENT_JOY1_CD32_FFW,
	-1
};
static const int rp1_joystick[] = {
	INPUTEVENT_JOY2_RIGHT, INPUTEVENT_JOY2_LEFT,
	INPUTEVENT_JOY2_DOWN, INPUTEVENT_JOY2_UP,
	INPUTEVENT_JOY2_FIRE_BUTTON,
	INPUTEVENT_JOY2_2ND_BUTTON,
	-1
};
static const int rp1_pad[] = {
	INPUTEVENT_JOY2_RIGHT, INPUTEVENT_JOY2_LEFT,
	INPUTEVENT_JOY2_DOWN, INPUTEVENT_JOY2_UP,
	INPUTEVENT_JOY2_FIRE_BUTTON,
	INPUTEVENT_JOY2_2ND_BUTTON,
	INPUTEVENT_JOY2_3RD_BUTTON,
	-1
};
static const int rp1_cd32[] = {
	INPUTEVENT_JOY2_RIGHT, INPUTEVENT_JOY2_LEFT,
	INPUTEVENT_JOY2_DOWN, INPUTEVENT_JOY2_UP,
	INPUTEVENT_JOY2_CD32_RED,
	INPUTEVENT_JOY2_CD32_BLUE,
	INPUTEVENT_JOY2_CD32_YELLOW,
	INPUTEVENT_JOY2_CD32_GREEN,
	INPUTEVENT_JOY2_CD32_PLAY,
	INPUTEVENT_JOY2_CD32_RWD,
	INPUTEVENT_JOY2_CD32_FFW,
	-1
};
static const int rp2_joystick[] = {
	INPUTEVENT_PAR_JOY1_RIGHT, INPUTEVENT_PAR_JOY1_LEFT,
	INPUTEVENT_PAR_JOY1_DOWN, INPUTEVENT_PAR_JOY1_UP,
	INPUTEVENT_PAR_JOY1_FIRE_BUTTON,
	-1
};
static const int rp3_joystick[] = {
	INPUTEVENT_PAR_JOY2_RIGHT, INPUTEVENT_PAR_JOY2_LEFT,
	INPUTEVENT_PAR_JOY2_DOWN, INPUTEVENT_PAR_JOY2_UP,
	INPUTEVENT_PAR_JOY2_FIRE_BUTTON,
	-1
};

static LRESULT deviceactivity(WPARAM wParam, LPARAM lParam)
{
	int num = HIBYTE(wParam);
	int cat = LOBYTE(wParam);
	uae_u32 mask = (uae_u32)lParam;
	write_log(_T("DEVICEACTIVITY %04x %08x (%d,%d)\n"), wParam, lParam, num, cat);
	if (cat != RP_DEVICECATEGORY_INPUTPORT && cat != RP_DEVICECATEGORY_MULTITAPPORT) {
		write_log(_T("DEVICEACTIVITY Not RP_DEVICECATEGORY_INPUTPORT or RP_DEVICECATEGORY_MULTITAPPORT.\n"));
		return 0;
	}
	if (cat == RP_DEVICECATEGORY_MULTITAPPORT) {
		if (num < 0 || num > 1) {
			write_log(_T("DEVICEACTIVITY invalid RP_DEVICECATEGORY_MULTITAPPORT %d.\n"), num);
			return 0;
		}
		num += 2;
	} else {
		if (num < 0 || num > 1) {
			write_log(_T("DEVICEACTIVITY invalid RP_DEVICECATEGORY_INPUTPORT %d.\n"), num);
			return 0;
		}
	}
	if (dactmask[num] == mask)
		return 1;
	const int *map = NULL;
	int type = dacttype[num];
	switch(num)
	{
	case 0:
		if (type == RP_INPUTDEVICE_JOYSTICK)
			map = rp0_joystick;
		else if (type == RP_INPUTDEVICE_GAMEPAD)
			map = rp0_pad;
		else if (type == RP_INPUTDEVICE_JOYPAD)
			map = rp0_cd32;
	break;
	case 1:
		if (type == RP_INPUTDEVICE_JOYSTICK)
			map = rp1_joystick;
		else if (type == RP_INPUTDEVICE_GAMEPAD)
			map = rp1_pad;
		else if (type == RP_INPUTDEVICE_JOYPAD)
			map = rp1_cd32;
	break;
	case 2:
		if (type == RP_INPUTDEVICE_JOYSTICK)
			map = rp2_joystick;
	break;
	case 3:
		if (type == RP_INPUTDEVICE_JOYSTICK)
			map = rp3_joystick;
	break;
	}
	if (!map) {
		write_log(_T("DEVICEACTIVITY unsupported port (%d)/device (%d) combo.\n"), num, type);
		return 0;
	}
	for (int i = 0; i < 16; i++) {
		uae_u32 mask2 = 1 << i;
		if (map[i] < 0)
			break;
		if ((dactmask[num] ^ mask) & mask2) {
			int state = (mask & mask2) ? 1 : 0;
			send_input_event(map[i], state, 1, 0);
		}
	}
	dactmask[num] = mask;
	return 1;
}



static const int inputdevmode[] = {
	RP_INPUTDEVICE_MOUSE, JSEM_MODE_WHEELMOUSE,
	RP_INPUTDEVICE_JOYSTICK, JSEM_MODE_JOYSTICK,
	RP_INPUTDEVICE_GAMEPAD, JSEM_MODE_GAMEPAD,
	RP_INPUTDEVICE_ANALOGSTICK, JSEM_MODE_JOYSTICK_ANALOG,
	RP_INPUTDEVICE_JOYPAD, JSEM_MODE_JOYSTICK_CD32,
	RP_INPUTDEVICE_LIGHTPEN, JSEM_MODE_LIGHTPEN,
	0, 0,
};

#define KEYBOARDCUSTOM _T("KeyboardCustom ")

struct rp_customevent
{
	const TCHAR *name;
	int evt;
};

static const TCHAR *customeventorder_joy[] = {
	_T("Left"),
	_T("Right"),
	_T("Up"),
	_T("Down"),
	_T("Fire"),
	_T("Fire2"),
	_T("Fire3"),
	NULL
};
static const TCHAR *customeventorder_cd32[] = {
	_T("Left"),
	_T("Right"),
	_T("Up"),
	_T("Down"),
	_T("Red"),
	_T("Blue"),
	_T("Green"),
	_T("Yellow"),
	_T("Rewind"),
	_T("FastForward"),
	_T("Play"),
	NULL
};

static const TCHAR **getcustomeventorder(int *devicetype)
{
	int devicetype2 = -1;
	for (int i = 0; inputdevmode[i * 2]; i++) {
		if (inputdevmode[i * 2 + 0] == *devicetype) {
			devicetype2 = inputdevmode[i * 2 + 1];
			break;
		}
	}
	if (devicetype2 < 0)
		return NULL;

	*devicetype = devicetype2;
	if (devicetype2 == JSEM_MODE_JOYSTICK)
		return customeventorder_joy;
	if (devicetype2 == JSEM_MODE_GAMEPAD)
		return customeventorder_joy;
	if (devicetype2 == JSEM_MODE_JOYSTICK_CD32)
		return customeventorder_cd32;
	return NULL;
}

static bool port_get_custom (int inputmap_port, int sub, TCHAR *out)
{
	int kb;
	bool first = true;
	TCHAR *p = out;
	int mode;
	const int *axistable;
	int events[MAX_COMPA_INPUTLIST];
	int max;
	const TCHAR **eventorder;

	max = inputdevice_get_compatibility_input (&currprefs, inputmap_port, &mode, events, &axistable);
	if (max <= 0)
		return false;

	int devicetype = -1;
	for (int i = 0; inputdevmode[i * 2]; i++) {
		if (inputdevmode[i * 2 + 1] == currprefs.jports[inputmap_port].jd[sub].mode) {
			devicetype = inputdevmode[i * 2 + 0];
			break;
		}
	}
	if (devicetype < 0)
		return false;

	eventorder = getcustomeventorder (&devicetype);
	if (!eventorder)
		return FALSE;

	_tcscpy (p, KEYBOARDCUSTOM);
	p += _tcslen (p);
	kb = inputdevice_get_device_total (IDTYPE_JOYSTICK) + inputdevice_get_device_total (IDTYPE_MOUSE);
	int kbnum = 0;
	for (int i = 0; eventorder[i]; i++) {
		int evtnum = events[i];
		for (int j = 0; j < inputdevicefunc_keyboard.get_widget_num (kbnum); j++) {
			int port;
			uae_u64 flags;
			if (inputdevice_get_mapping (kb + kbnum, j, &flags, &port, NULL, NULL, 0) == evtnum) {
				if (port == inputmap_port + 1) {
					uae_u32 kc = 0;
					inputdevicefunc_keyboard.get_widget_type (kbnum, j, NULL, &kc);
					if (!first)
						*p++ = ' ';
					first = false;
					_tcscpy (p, eventorder[i]);
					p += _tcslen (p);
					if (flags & IDEV_MAPPED_AUTOFIRE_SET) {
						_tcscpy (p, _T(".autorepeat"));
						p += _tcslen (p);
					}
					_stprintf (p, _T("=%02X"), kc);
					p += _tcslen (p);
				}
			}
		}
	}
	write_log (_T("port%d_get_custom: %s\n"), inputmap_port, out);
	return true;
}

int port_insert_custom (int inputmap_port, int devicetype, DWORD flags, const TCHAR *custom)
{
	const TCHAR *p = custom;
	int mode;
	const int *axistable;
	int events[MAX_COMPA_INPUTLIST];
	int max, evtnum;
	int kb;
	const TCHAR **eventorder;

	max = inputdevice_get_compatibility_input (&changed_prefs, inputmap_port, &mode, events, &axistable);

	eventorder = getcustomeventorder (&devicetype);
	if (!eventorder)
		return FALSE;

	kb = inputdevice_get_device_total (IDTYPE_JOYSTICK) + inputdevice_get_device_total (IDTYPE_MOUSE);

	inputdevice_copyconfig (&currprefs, &changed_prefs);
	inputdevice_compa_prepare_custom (&changed_prefs, inputmap_port, 0, devicetype, true);
	inputdevice_updateconfig (NULL, &changed_prefs);
	max = inputdevice_get_compatibility_input (&changed_prefs, inputmap_port, &mode, events, &axistable);
	write_log (_T("custom='%s' max=%d port=%d dt=%d kb=%d kbnum=%d\n"), custom, max, inputmap_port, devicetype, kb, inputdevice_get_device_total (IDTYPE_KEYBOARD));
	if (max <= 0)
		return FALSE;

	while (p && p[0]) {
		int idx = -1, kc = -1;
		int flags = 0;
		int eventlen;

		const TCHAR *p2 = _tcschr (p, '=');
		if (!p2)
			break;
		const TCHAR *p4 = p;
		eventlen = -1;
		for (;;) {
			const TCHAR *p3 = _tcschr (p4, '.');
			if (!p3 || p3 >= p2) {
				p3 = NULL;
				if (eventlen < 0)
					eventlen = (int)(p2 - p);
				break;
			}
			if (eventlen < 0)
				eventlen = (int)(p3 - p);
			if (!_tcsnicmp (p3 + 1, L"autorepeat", 10))
				flags |= IDEV_MAPPED_AUTOFIRE_SET;
			p4 = p3 + 1;
		}
		
		for (int i = 0; eventorder[i]; i++) {
			if (_tcslen (eventorder[i]) == eventlen && !_tcsncmp (p, eventorder[i], eventlen)) {
				idx = i;
				break;
			}
		}
		p2++;
		if (p2[0] == '0' && (p2[1] == 'x' || p2[1] == 'X'))
			kc = _tcstol (p2 + 2, NULL, 16);
		else
			kc = _tstol (p2);
		p = _tcschr (p2, ' ');
		if (p)
			p++;

		if (idx < 0)
			continue;
		if (kc < 0)
			continue;

		evtnum = events[idx];

		write_log (_T("kb=%d evt=%d kc=%02x flags=%08x\n"), kb, evtnum, kc, flags);

		for (int j = 0; j < inputdevice_get_device_total (IDTYPE_KEYBOARD); j++) {
			int wdnum = -1;
			for (int i = 0; i < inputdevicefunc_keyboard.get_widget_num (j); i++) {
				uae_u32 kc2 = 0;
				inputdevicefunc_keyboard.get_widget_type (j, i, NULL, &kc2);
				if (kc == kc2) {
					wdnum = i;
					break;
				}
			}
			if (wdnum >= 0) {
				write_log (_T("kb=%d (%s) wdnum=%d\n"), j, inputdevicefunc_keyboard.get_friendlyname (j), wdnum);
				inputdevice_set_gameports_mapping (&changed_prefs, kb + j, wdnum, evtnum, flags, inputmap_port, GAMEPORT_INPUT_SETTINGS);
			} else {
				write_log (_T("kb=%d (%): keycode %02x not found!\n"), j, inputdevicefunc_keyboard.get_friendlyname (j), kc);
			}
		}
	}
	inputdevice_copyconfig (&changed_prefs, &currprefs);
	return TRUE;
}

static int port_insert (int inputmap_port, int devicetype, DWORD flags, const TCHAR *name)
{
	int ret = 0;
	int devicetype2;

	write_log (_T("port%d_insert type=%d flags=%d '%s'\n"), inputmap_port, devicetype, flags, name);

	if (devicetype == RP_INPUTDEVICE_JOYSTICK || devicetype == RP_INPUTDEVICE_GAMEPAD || devicetype == RP_INPUTDEVICE_JOYPAD) {
		if (inputmap_port >= 0 && inputmap_port < 4) {
			dacttype[inputmap_port] = devicetype;
			inputdevice_compa_clear(&changed_prefs, inputmap_port, -1);
			inputdevice_joyport_config(&changed_prefs, _T("none"), NULL, inputmap_port, 0, 0, 0, 0, true);
			return 1;
		}
		return 0;
	}

	if (inputmap_port < 0 || inputmap_port >= maxjports)
		return FALSE;
	
	inputdevice_compa_clear (&changed_prefs, inputmap_port, -1);
	
	if (name[0] == '\0') {
		inputdevice_joyport_config (&changed_prefs, _T("none"), NULL, inputmap_port, 0, 0, 0, 0, true);
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

	if (!_tcsncmp (name, KEYBOARDCUSTOM, _tcslen (KEYBOARDCUSTOM))) {
		return port_insert_custom (inputmap_port, devicetype, flags, name + _tcslen (KEYBOARDCUSTOM));
	}

	for (int i = 0; i < 10; i++) {
		TCHAR tmp2[100];
		_stprintf (tmp2, _T("KeyboardLayout%d"), i);
		if (!_tcscmp (tmp2, name)) {
			_stprintf (tmp2, _T("kbd%d"), i + 1);
			ret = inputdevice_joyport_config (&changed_prefs, tmp2, NULL, inputmap_port, devicetype2, 0, 0, 0, true);
			return ret;
		}
	}
	ret = inputdevice_joyport_config (&changed_prefs, name, name, inputmap_port, devicetype2, 0, 1, 0, true);
	return ret;
}

static int cd_insert (int num, const TCHAR *name)
{
	_tcscpy (changed_prefs.cdslots[num].name, name);
	changed_prefs.cdslots[num].inuse = true;
	set_config_changed ();
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
	if (uMessage == RP_IPC_TO_HOST_DEVICESEEK || uMessage == RP_IPC_TO_HOST_DEVICEACTIVITY)
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
	if (uMessage == RP_IPC_TO_HOST_DEVICESEEK)
		dolog = 0;
	recursive++;
	cnt++;
	ncnt = cnt;
	if (dolog & 1)
		write_log (_T("RPSEND_%d->\n"), ncnt);
	v = RPSendMessage (uMessage, wParam, lParam, pData, dwDataSize, pInfo, plResult);
	recursive--;
	if (dolog & 1) {
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
	struct monconfig *gm = &prefs->gfx_monitor[0];
	static int done;

	if (done)
		return;
	done = 1;
	if (log_rp)
		write_log(_T("fixup_size(%d,%d)\n"), prefs->gfx_xcenter_size, prefs->gfx_ycenter_size);
	if (prefs->gfx_xcenter_size > 0) {
		int hres = prefs->gfx_resolution;
		if (prefs->gf[0].gfx_filter) {
			if (prefs->gf[0].gfx_filter_horiz_zoom_mult)
				hres += (int)prefs->gf[0].gfx_filter_horiz_zoom_mult - 1;
		}
		if (hres > max_horiz_dbl)
			hres = max_horiz_dbl;
		gm->gfx_size_win.width = prefs->gfx_xcenter_size >> (RES_MAX - hres);
		prefs->gf[0].gfx_filter_autoscale = 0;
	}
	if (prefs->gfx_ycenter_size > 0) {
		int vres = prefs->gfx_vresolution;
		if (prefs->gf[0].gfx_filter) {
			if (prefs->gf[0].gfx_filter_vert_zoom_mult)
				vres += (int)prefs->gf[0].gfx_filter_vert_zoom_mult - 1;
		}
		if (vres > max_vert_dbl)
			vres = max_vert_dbl;
		gm->gfx_size_win.height = (prefs->gfx_ycenter_size * 2) >> (VRES_QUAD - vres);
		prefs->gf[0].gfx_filter_autoscale = 0;
	}
	write_log(_T("-> %dx%d\n"), gm->gfx_size_win.width, gm->gfx_size_win.height);
}

static float getmult(float mult)
{
	if (mult >= 3.5f)
		return mult / 2.0f; // 4x+
	if (mult >= 2.5f)
		return 1.5f; // 3x
	if (mult >= 1.5f)
		return 1.0f; // 2x
	if (mult >= 0.8f)
		return 0.0f; // 1x
	if (mult >= 0.4f)
		return -1.0f; // 1/2x
	if (mult >= 0.1f)
		return -2.0f; // 1/4x
	return 0.0f;
}

static int shift (int val, int shift)
{
	if (shift > 0)
		val >>= shift;
	else if (shift < 0)
		val <<= -shift;
	return val;
}

static void get_screenmode (struct RPScreenMode *sm, struct uae_prefs *p, bool getclip)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	struct monconfig *gm = &p->gfx_monitor[mon->monitor_id];
	int m, cf;
	int full = 0;
	int hres, vres;
	int totalhdbl = -1, totalvdbl = -1;
	float hmult, vmult;
	bool half;
	bool rtg;
	int scalemode = p->gf[0].gfx_filter_autoscale;

	hres = p->gfx_resolution;
	vres = p->gfx_vresolution;

	sm->hGuestWindow = guestwindow;
	m = RP_SCREENMODE_SCALE_1X;
	cf = 0;
	half = false;
	rtg = WIN32GFX_IsPicassoScreen(mon) != 0;

	if (rtg) {

		hmult = p->rtg_horiz_zoom_mult;
		vmult = p->rtg_vert_zoom_mult;

		full = p->gfx_apmode[1].gfx_fullscreen;
		sm->lClipTop = -1;
		sm->lClipLeft = -1;
		sm->lClipWidth = -1;//picasso96_state.Width;
		sm->lClipHeight = -1;//picasso96_state.Height;

		for (int i = RP_SCREENMODE_SCALE_MAX_X; i >= RP_SCREENMODE_SCALE_2X; i--) {
			if (hmult >= (float)i + 0.5) {
				m |= i;
				break;
			}
		}

	} else {

		int rw, rh, rx, ry;
		int xp, yp;

		get_custom_raw_limits (&rw, &rh, &rx, &ry);
		if (rx >= 0 && ry >= 0) {
			get_custom_topedge (&xp, &yp, false);
			rx += xp;
			ry += yp;
			rw <<= RES_MAX - currprefs.gfx_resolution;
			rx <<= RES_MAX - currprefs.gfx_resolution;
			rh <<= VRES_MAX - currprefs.gfx_vresolution;
			ry <<= VRES_MAX - currprefs.gfx_vresolution;
		}
		write_log (_T("GET_RPSM: %d %d %d %d\n"), rx, ry, rw, rh);

		hmult = p->gf[0].gfx_filter_horiz_zoom_mult;
		vmult = p->gf[0].gfx_filter_vert_zoom_mult;

		full = p->gfx_apmode[0].gfx_fullscreen;

		totalhdbl = hres;
		if (hres > max_horiz_dbl)
			hres = max_horiz_dbl;
		float hresm = getmult(hmult);
		hres += (int)hresm;

		totalvdbl = vres;
		if (vres > max_vert_dbl)
			vres = max_vert_dbl;
		float vresm = getmult(vmult);
		vres += (int)vresm;

		if (hres > RES_SUPERHIRES) {
			m = (int)(vresm * 2.0f + 0.5f) - 1;
		} else if (hres == RES_SUPERHIRES) {
			m = vresm > 1.0f && vresm < 2.0f ? RP_SCREENMODE_SCALE_3X : RP_SCREENMODE_SCALE_2X;
		} else if (hres >= RES_SUPERHIRES + 1) {
			m = vresm > 1.0f && vresm < 2.0f ? RP_SCREENMODE_SCALE_3X : RP_SCREENMODE_SCALE_4X;
		} else {
			m = RP_SCREENMODE_SCALE_1X;
		}

		if (rx > 0 && ry > 0) {
			sm->lClipLeft = rx;
			sm->lClipTop = ry;
			sm->lClipWidth = rw;
			sm->lClipHeight = rh;
		} else {
			sm->lClipLeft = p->gfx_xcenter_pos < 0 ? -1 : p->gfx_xcenter_pos;
			sm->lClipTop = p->gfx_ycenter_pos < 0 ? -1 : p->gfx_ycenter_pos;
			sm->lClipWidth = p->gfx_xcenter_size <= 0 ? -1 : p->gfx_xcenter_size;
			sm->lClipHeight = p->gfx_ycenter_size <= 0 ? -1 : p->gfx_ycenter_size;
		}

		if (p->gf[0].gfx_filter_scanlines || p->gfx_pscanlines)
			m |= RP_SCREENMODE_SCANLINES;

		if (scalemode == AUTOSCALE_RESIZE || scalemode == AUTOSCALE_NORMAL || scalemode == AUTOSCALE_INTEGER_AUTOSCALE)
			cf |= RP_CLIPFLAGS_AUTOCLIP;
		else if (p->gfx_xcenter_pos == 0 && p->gfx_ycenter_pos == 0)
			cf |= RP_CLIPFLAGS_NOCLIP;
	}

	if (full) {
		m &= ~RP_SCREENMODE_DISPLAYMASK;
		m |= p->gfx_apmode[rtg ? APMODE_RTG : APMODE_NATIVE].gfx_display << 8;
	}
	if (full > 1) {
		m |= RP_SCREENMODE_FULLSCREEN_SHARED;
	}
	if (p->gf[rtg].gfx_filter_bilinear) {
		m |= RP_SCREENMODE_INTERPOLATION;
	}
	if (!rtg && p->gfx_ntscpixels) {
		m |= RP_SCREENMODE_PIXEL_ORIGINAL_RATIO;
	}

	sm->dwScreenMode = m | (storeflags & (RP_SCREENMODE_SCALING_STRETCH | RP_SCREENMODE_SCALING_SUBPIXEL));
	sm->lTargetHeight = 0;
	sm->lTargetWidth = 0;
	if ((storeflags & RP_SCREENMODE_SCALEMASK) == RP_SCREENMODE_SCALE_MAX) {
		sm->dwScreenMode &= ~RP_SCREENMODE_SCALEMASK;
		sm->dwScreenMode |= RP_SCREENMODE_SCALE_MAX;
	} else if ((storeflags & RP_SCREENMODE_SCALEMASK) == RP_SCREENMODE_SCALE_TARGET) {
		sm->dwScreenMode &= ~RP_SCREENMODE_SCALEMASK;
		sm->dwScreenMode = RP_SCREENMODE_SCALE_TARGET;
		sm->lTargetWidth = gm->gfx_size_win.width;
		sm->lTargetHeight = gm->gfx_size_win.height;
	}
	sm->dwClipFlags = cf;

	if (log_rp & 2) {
		write_log (_T("%sGET_RPSM: hres=%d (%d) vres=%d (%d) full=%d xcpos=%d ycpos=%d w=%d h=%d vm=%.2f hm=%.2f half=%d\n"),
			rtg ? _T("RTG ") : _T(""),
			totalhdbl, hres, totalvdbl, vres, full,
			p->gfx_xcenter_pos,  p->gfx_ycenter_pos,
			gm->gfx_size_win.width, gm->gfx_size_win.height,
			hmult, vmult, half);
		write_log (_T("GET_RPSM: %08X %dx%d %dx%d hres=%d (%d) vres=%d (%d) disp=%d fs=%d\n"),
			sm->dwScreenMode, sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight,
			totalhdbl, hres, totalvdbl, vres, p->gfx_apmode[APMODE_NATIVE].gfx_display, full);
	}
}

static bool interpolation_old(DWORD sm)
{
	return ((sm & (RP_SCREENMODE_PIXEL_ORIGINAL_RATIO | RP_SCREENMODE_SCALING_SUBPIXEL | RP_SCREENMODE_SCANLINES)) == 0 &&
		RP_SCREENMODE_DISPLAY(sm) == 0) ? 0 : 1;
}

static void set_screenmode (struct RPScreenMode *sm, struct uae_prefs *p)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[0];
	struct monconfig *gm = &p->gfx_monitor[mon->monitor_id];
	struct monconfig *gmc = &currprefs.gfx_monitor[mon->monitor_id];
	int smm = RP_SCREENMODE_SCALE (sm->dwScreenMode);
	int smm_m = smm & RP_SCREENMODE_SCALE_MAX_X_MASK;
	int display = RP_SCREENMODE_DISPLAY (sm->dwScreenMode);
	int fs = 0;
	int hdbl = RES_HIRES, vdbl = VRES_DOUBLE;
	int hres, vres;
	float hmult = 1, vmult = 1;
	struct MultiDisplay *disp;
	bool keepaspect = !(sm->dwScreenMode & RP_SCREENMODE_SCALING_STRETCH);
	bool stretch = (sm->dwScreenMode & RP_SCREENMODE_SCALING_STRETCH) != 0;
	bool forcesize = smm == RP_SCREENMODE_SCALE_TARGET && sm->lTargetWidth > 0 && sm->lTargetHeight > 0;
	bool integerscale = !(sm->dwScreenMode & RP_SCREENMODE_SCALING_SUBPIXEL) && !(sm->dwScreenMode & RP_SCREENMODE_SCALING_STRETCH) && smm >= RP_SCREENMODE_SCALE_TARGET;
	int width, height;
	bool half;

	storeflags = sm->dwScreenMode;
	minimized = 0;
	if (display) {
		p->gfx_apmode[APMODE_NATIVE].gfx_display = display;
		p->gfx_apmode[APMODE_RTG].gfx_display = display;
		if (sm->dwScreenMode & RP_SCREENMODE_FULLSCREEN_SHARED)
			fs = 2;
		else
			fs = 1;
	}
	p->gf[0].gfx_filter_left_border = 0;
	p->gf[0].gfx_filter_top_border = 0;
	p->gf[0].gfx_filter_right_border = 0;
	p->gf[0].gfx_filter_bottom_border = 0;
	p->gf[0].gfx_filter_autoscale = AUTOSCALE_CENTER;
	p->gfx_ntscpixels = false;

	disp = getdisplay(p, 0);

	if (log_rp & 2) {
		write_log (_T("SET_RPSM: %08X %dx%d %dx%d hres=%d vres=%d disp=%d fs=%d smm=%d\n"),
			sm->dwScreenMode, sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight,
			hdbl, vdbl, display, fs, smm);
		write_log (_T("aspect=%d stretch=%d force=%d integer=%d\n"),
			keepaspect, stretch, forcesize, integerscale);
	}

	if (!WIN32GFX_IsPicassoScreen(mon)) {

		float xtramult = 1.0f;

		if (smm == RP_SCREENMODE_SCALE_3X) {

			hdbl = RES_SUPERHIRES;
			vdbl = VRES_QUAD;
			hmult = 1.5;
			vmult = 1.5;
			half = true;

			hres = hdbl;
			if (hres > max_horiz_dbl) {
				hmult *= 1 << (hres - max_horiz_dbl);
				hres = max_horiz_dbl;
			}

			vres = vdbl;
			if (vres > max_vert_dbl) {
				vmult *= 1 << (vres - max_vert_dbl);
				vres = max_vert_dbl;
			}

		} else {

			half = false;
			if (smm == RP_SCREENMODE_SCALE_2X) {
				// 2X
				hdbl = RES_SUPERHIRES;
				vdbl = VRES_QUAD;
			} else if (smm_m >= RP_SCREENMODE_SCALE_4X) {
				// 4X
				hdbl = RES_SUPERHIRES + 1;
				vdbl = VRES_QUAD + 1;
			} else {
				// 1X
				hdbl = RES_HIRES;
				vdbl = VRES_DOUBLE;
			}

			if (smm == RP_SCREENMODE_SCALE_MAX) {
				hdbl = max_horiz_dbl;
				vdbl = max_vert_dbl;
			}

			hres = hdbl;
			if (hres > max_horiz_dbl) {
				hmult = (float)(1 << (hres - max_horiz_dbl));
				hres = max_horiz_dbl;
			}

			vres = vdbl;
			if (vres > max_vert_dbl) {
				vmult = (float)(1 << (vres - max_vert_dbl));
				vres = max_vert_dbl;
			}

			if (smm_m > RP_SCREENMODE_SCALE_4X && smm < RP_SCREENMODE_SCALE_TARGET) {
				xtramult = (smm_m + 1.0f) / 4.0f;
				hmult *= xtramult;
				vmult *= xtramult;
			}
		}
		if (hres == RES_LORES && vres > VRES_NONDOUBLE)
			vres = VRES_NONDOUBLE;

		p->gfx_resolution = hres;
		p->gfx_vresolution = vres;

		if (sm->lClipWidth <= 0) {
			gm->gfx_size_win.width = shift (AMIGA_WIDTH_MAX, -hdbl);
		} else {
			if (hdbl > RES_MAX)
				gm->gfx_size_win.width = sm->lClipWidth << (hdbl - RES_MAX);
			else
				gm->gfx_size_win.width = sm->lClipWidth >> (RES_MAX - hdbl);
		}

		if (sm->lClipHeight <= 0) {
			gm->gfx_size_win.height = shift (AMIGA_HEIGHT_MAX, -vdbl);
		} else {
			if (vdbl > VRES_MAX)
				gm->gfx_size_win.height = sm->lClipHeight << (vdbl - VRES_MAX);
			else
				gm->gfx_size_win.height = sm->lClipHeight >> (VRES_MAX - vdbl);
		}
		
		gm->gfx_size_win.width = (int)(gm->gfx_size_win.width * xtramult);
		gm->gfx_size_win.height = (int)(gm->gfx_size_win.height * xtramult);

		if (half) {
			gm->gfx_size_win.width = gm->gfx_size_win.width * 3 / 2;
			gm->gfx_size_win.height = gm->gfx_size_win.height * 3 / 2;
		}

		if (forcesize) {
			width = gm->gfx_size_win.width = sm->lTargetWidth;
			height = gm->gfx_size_win.height = sm->lTargetHeight;
		}

		if (fs == 1) {
			width = gm->gfx_size_fs.width = gm->gfx_size_win.width;
			height = gm->gfx_size_fs.height = gm->gfx_size_win.height;
		} else if (fs == 2) {
			width = gm->gfx_size_fs.width = disp->rect.right - disp->rect.left;
			height = gm->gfx_size_fs.height = disp->rect.bottom - disp->rect.top;
		}
	}

	p->gfx_apmode[1].gfx_fullscreen = fs;
	p->gfx_apmode[0].gfx_fullscreen = fs;
	p->gfx_xcenter_pos = sm->lClipLeft;
	p->gfx_ycenter_pos = sm->lClipTop;
	p->gfx_xcenter_size = -1;
	p->gfx_ycenter_size = -1;

	int m = 1;
	if (fs < 2) {
		if (smm_m >= RP_SCREENMODE_SCALE_2X && smm < RP_SCREENMODE_SCALE_TARGET) {
			m = smm_m - RP_SCREENMODE_SCALE_2X + 2;
		}
	}
	p->rtg_horiz_zoom_mult = p->rtg_vert_zoom_mult = (float)m;

	if (WIN32GFX_IsPicassoScreen(mon)) {

		int m = 1;
		if (fs == 2) {
			p->gf[GF_RTG].gfx_filter_autoscale = RTG_MODE_SCALE;
		} else {
			p->gf[GF_RTG].gfx_filter_autoscale = 0;
			if (smm_m >= RP_SCREENMODE_SCALE_2X && smm < RP_SCREENMODE_SCALE_TARGET) {
				m = smm_m - RP_SCREENMODE_SCALE_2X + 2;
			}
		}

		p->win32_rtgallowscaling = false;
		p->win32_rtgscaleaspectratio = keepaspect ? -1 : 0;
		if (interpolation_v102) {
			p->gf[GF_RTG].gfx_filter_bilinear = (sm->dwScreenMode & RP_SCREENMODE_INTERPOLATION) != 0;
		} else {
			p->gf[GF_RTG].gfx_filter_bilinear = interpolation_old(sm->dwScreenMode);
		}

		if (integerscale) {
			p->gf[GF_RTG].gfx_filter_autoscale = RTG_MODE_INTEGER_SCALE;
		}
		gm->gfx_size_win.width = vidinfo->width * m;
		gm->gfx_size_win.height = vidinfo->height * m;
		if (forcesize && !integerscale) {
			gm->gfx_size_win.width = sm->lTargetWidth;
			gm->gfx_size_win.height = sm->lTargetHeight;
			p->gf[GF_RTG].gfx_filter_autoscale = RTG_MODE_SCALE;
			p->win32_rtgallowscaling = true;
		}

		hmult = (float)m;
		vmult = (float)m;

	} else {

		if (stretch) {
			hmult = vmult = 0;
		} else if (integerscale) {
			hmult = vmult = 1;
			p->gf[0].gfx_filter_integerscalelimit = 0;
			if (sm->dwClipFlags & RP_CLIPFLAGS_AUTOCLIP) {
				p->gf[0].gfx_filter_autoscale = AUTOSCALE_INTEGER_AUTOSCALE;
				p->gfx_xcenter_pos = -1;
				p->gfx_ycenter_pos = -1;
				p->gfx_xcenter_size = -1;
				p->gfx_ycenter_size = -1;
			} else {
				p->gf[0].gfx_filter_autoscale = AUTOSCALE_INTEGER;
				if (sm->lClipWidth > 0)
					p->gfx_xcenter_size = sm->lClipWidth;
				if (sm->lClipHeight > 0)
					p->gfx_ycenter_size = sm->lClipHeight;
				p->gf[0].gfx_filter_top_border = sm->lClipTop;
				p->gf[0].gfx_filter_bottom_border = sm->lClipTop + sm->lClipHeight;
				p->gf[0].gfx_filter_left_border = sm->lClipLeft;
				p->gf[0].gfx_filter_right_border = p->gf[0].gfx_filter_left_border + sm->lClipWidth;
				// backwards compatibility fix
				p->gf[0].gfx_filter_left_border -= 0x38 * 4;
				p->gf[0].gfx_filter_right_border -= 0x38 * 4;
			}
			
		}

		if (keepaspect) {
			p->gf[0].gfx_filter_aspect = -1;
			p->gf[0].gfx_filter_keep_autoscale_aspect = integerscale ? 0 : 1;
			p->gf[0].gfx_filter_keep_aspect = 1;
		} else {
			p->gf[0].gfx_filter_aspect = 0;
			p->gf[0].gfx_filter_keep_autoscale_aspect = 0;
			p->gf[0].gfx_filter_keep_aspect = 0;
		}

		if (!integerscale) {
			if (sm->dwClipFlags & RP_CLIPFLAGS_AUTOCLIP) {
				if (!forcesize)
					p->gf[0].gfx_filter_autoscale = AUTOSCALE_RESIZE;
				else
					p->gf[0].gfx_filter_autoscale = AUTOSCALE_NORMAL;
				p->gfx_xcenter_pos = -1;
				p->gfx_ycenter_pos = -1;
				p->gfx_xcenter_size = -1;
				p->gfx_ycenter_size = -1;
			} else if (sm->dwClipFlags & RP_CLIPFLAGS_NOCLIP) {
				p->gf[0].gfx_filter_autoscale = AUTOSCALE_STATIC_MAX;
				p->gfx_xcenter_pos = -1;
				p->gfx_ycenter_pos = -1;
				p->gfx_xcenter_size = -1;
				p->gfx_ycenter_size = -1;
				if (!forcesize) {
					gm->gfx_size_win.width = AMIGA_WIDTH_MAX << currprefs.gfx_resolution;
					gm->gfx_size_win.height = AMIGA_HEIGHT_MAX << currprefs.gfx_vresolution;;
				}
			}

			if (sm->lClipWidth > 0)
				p->gfx_xcenter_size = sm->lClipWidth;
			if (sm->lClipHeight > 0)
				p->gfx_ycenter_size = sm->lClipHeight;

			if ((p->gfx_xcenter_pos >= 0 && p->gfx_ycenter_pos >= 0) || (p->gfx_xcenter_size > 0 && p->gfx_ycenter_size > 0)) {
				p->gf[0].gfx_filter_autoscale = AUTOSCALE_MANUAL;
			}
		}

		p->gf[0].gfx_filter_horiz_zoom_mult = hmult;
		p->gf[0].gfx_filter_vert_zoom_mult = vmult;

		p->gf[0].gfx_filter_scanlines = 0;
		p->gfx_iscanlines = 0;
		p->gfx_pscanlines = 0;
		if (sm->dwScreenMode & RP_SCREENMODE_SCANLINES) {
			p->gfx_pscanlines = 1;
			p->gf[0].gfx_filter_scanlines = 8;
			p->gf[0].gfx_filter_scanlinelevel = 8;
			p->gf[0].gfx_filter_scanlineratio = (1 << 4) | 1;
		}

		if (sm->dwClipFlags & RP_CLIPFLAGS_AUTOCLIP) {
			p->gf[0].gfx_filter_left_border = -1;
			p->gf[0].gfx_filter_top_border = -1;
			p->gf[0].gfx_filter_right_border = -1;
			p->gf[0].gfx_filter_bottom_border = -1;
		}
		if (interpolation_v102) {
			p->gf[0].gfx_filter_bilinear = (sm->dwScreenMode & RP_SCREENMODE_INTERPOLATION) != 0;
		} else {
			p->gf[0].gfx_filter_bilinear = interpolation_old(sm->dwScreenMode);
		}

		if (sm->dwScreenMode & RP_SCREENMODE_PIXEL_ORIGINAL_RATIO) {
			p->gfx_ntscpixels = true;
		}
	}

	if (log_rp & 2) {
		write_log(_T("%dx%d %dx%d %dx%d %08x HM=%.1f VM=%.1f\n"),
			sm->lClipLeft, sm->lClipTop, sm->lClipWidth, sm->lClipHeight, sm->lTargetWidth, sm->lTargetHeight, sm->dwClipFlags, hmult, vmult);
		if (WIN32GFX_IsPicassoScreen(mon)) {
			write_log (_T("RTG WW=%d WH=%d FW=%d FH=%d HM=%.1f VM=%.1f\n"),
				gm->gfx_size_win.width, gm->gfx_size_win.height,
				gm->gfx_size_fs.width, gm->gfx_size_fs.height,
				p->rtg_horiz_zoom_mult, p->rtg_vert_zoom_mult);
		} else {
			write_log (_T("WW=%d (%d) WH=%d (%d) FW=%d (%d) FH=%d (%d) HM=%.1f VM=%.1f XP=%d YP=%d XS=%d YS=%d AS=%d AR=%d,%d\n"),
				gm->gfx_size_win.width, gmc->gfx_size_win.width, gm->gfx_size_win.height, gmc->gfx_size.height,
				gm->gfx_size_fs.width, gmc->gfx_size_fs.width, gm->gfx_size_fs.height, gmc->gfx_size_fs.height,
				p->gf[0].gfx_filter_horiz_zoom_mult, p->gf[0].gfx_filter_vert_zoom_mult,
				p->gfx_xcenter_pos, p->gfx_ycenter_pos,
				p->gfx_xcenter_size, p->gfx_ycenter_size,
				p->gf[0].gfx_filter_autoscale, p->gf[0].gfx_filter_aspect, p->gf[0].gfx_filter_keep_aspect);
		}
	}

	updatewinfsmode(0, p);
	hwndset = 0;
	set_config_changed ();
#if 0
	write_log (_T("AFTER WW=%d (%d) WH=%d (%d) FW=%d (%d) FH=%d (%d) HM=%.1f VM=%.1f XP=%d YP=%d XS=%d YS=%d AS=%d AR=%d,%d\n"),
		gm->gfx_size_win.width, currprefs.gfx_size_win.width, gm->gfx_size_win.height, currprefs.gfx_size.height,
		gm->gfx_size_fs.width, currprefs.gfx_size_fs.width, gm->gfx_size_fs.height, currprefs.gfx_size_fs.height,
		p->gf[0].gfx_filter_horiz_zoom_mult, p->gf[0].gfx_filter_vert_zoom_mult,
		p->gfx_xcenter_pos, p->gfx_ycenter_pos,
		p->gfx_xcenter_size, p->gfx_ycenter_size,
		p->gf[0].gfx_filter_autoscale, p->gf[0].gfx_filter_aspect, p->gf[0].gfx_filter_keep_aspect);
	write_log (_T("AFTER W=%d (%d) H=%d (%d)\n"), gm->gfx_size.width, currprefs.gfx_size.width, gm->gfx_size.height, currprefs.gfx_size.height);
#endif
}

static int events_added;

void parse_guest_event(const TCHAR *ss)
{
	TCHAR *s = my_strdup(ss);
	if (s[0] != '\'') {
		TCHAR *start = s;
		for (;;) {
			TCHAR *next;
			TCHAR *space1 = _tcschr(start, ' ');
			if (!space1)
				break;
			TCHAR *space2 = _tcschr(space1 + 1, ' ');
			if (space2) {
				*space2 = 0;
				next = space2 + 1;
			} else {
				next = NULL;
			}
			events_added++;
			if (events_added > 4) {
				handle_custom_event(_T("hdelay 120"), 1);
				events_added = 0;
			}
			handle_custom_event(start, 1);
			if (!next)
				break;
			start = next;
		}
	} else {
		handle_custom_event(s, 1);
	}
	xfree(s);
}

static int movescreenoverlay(WPARAM wParam, LPARAM lParam)
{
	struct extoverlay eo = { 0 };
	if (!D3D_extoverlay)
		return 0;
	eo.idx = (int)wParam;
	eo.xpos = LOWORD(lParam);
	eo.ypos = HIWORD(lParam);
	int ret = D3D_extoverlay(&eo, 0);
	if (pause_emulation && D3D_refresh) {
		D3D_refresh(0);
		delayed_refresh = 0;
	}
	return ret;
}

static int deletescreenoverlay(WPARAM wParam)
{
	struct extoverlay eo = { 0 };
	if (!D3D_extoverlay)
		return 0;
	delayed_refresh = gett();
	eo.idx = (int)wParam;
	eo.width = -1;
	eo.height = -1;
	return D3D_extoverlay(&eo, 0);
}

static int screenoverlay(LPCVOID pData)
{
	struct RPScreenOverlay *rpo = (struct RPScreenOverlay*)pData;
	struct extoverlay eo = { 0 };
	if (!D3D_extoverlay)
		return 0;
	if (rpo->dwFormat != RPSOPF_32BIT_BGRA)
		return 0;
	delayed_refresh = gett();
	eo.idx = rpo->dwIndex;
	eo.xpos = rpo->lLeft;
	eo.ypos = rpo->lTop;
	eo.width = rpo->lWidth;
	eo.height = rpo->lHeight;
	eo.data = rpo->btData;
	return D3D_extoverlay(&eo, 0);
}

static void dos_execute_callback(uae_u32 id, uae_u32 status, uae_u32 flags, const char *outbuf, void *userdata)
{
	RPExecuteResult *er;
	int size = sizeof(RPExecuteResult);

	if (flags & (RP_EXECUTE_RETURN_EXIT_CODE | RP_EXECUTE_RETURN_OUTPUT)) {
		int outsize = 0;
		if (outbuf) {
			outsize = uaestrlen(outbuf);
		}
		size += (outsize + 1) * sizeof(TCHAR);
		er = (RPExecuteResult *)xcalloc(uae_u8, size);
		if (er) {
			er->cbSize = sizeof(RPExecuteResult);
			er->dwExecuteID = id;
			er->dwExitCode = status;
			er->dwOutputLength = outsize;
			er->hrExecuteResult = S_OK;
			if (outbuf) {
				au_copy(er->szOutput, outsize, outbuf);
			}
			RPSendMessagex(RP_IPC_TO_HOST_EXECUTE_RESULT, 0, 0, er, size, &guestinfo, NULL);
			xfree(er);
		}
	}
}

extern bool serreceive_external(uae_u16);
static int rp_readmodemint(WPARAM wp, LPARAM lp)
{
	if (wp != MAKEWORD(RP_DEVICECATEGORY_MODEM, 0)) {
		return 0;
	}
	uae_u8 b = (uae_u8)lp;
	bool v = serreceive_external(b | 0x100);
	return v ? 1 : -1;
}

static bool modem_cts, modem_cd, modem_ri, modem_dsr;
void rp_readmodemstatus(bool *dsr, bool *cd, bool *cts, bool *ri)
{
	*dsr = modem_dsr;
	*cd = modem_cd;
	*cts = modem_cts;
	*ri = modem_ri;
}
static int rp_readmodemstatusint(WPARAM wp, LPARAM lp)
{
	if (wp != MAKEWORD(RP_DEVICECATEGORY_MODEM, 0)) {
		return 0;
	}
	uae_u16 mask = LOWORD(lp);
	uae_u16 val = HIWORD(lp);
	if (mask & RP_SIGNAL_CTS) {
		modem_cts = (val & RP_SIGNAL_CTS) != 0;
	}
	if (mask & RP_SIGNAL_RI) {
		modem_ri = (val & RP_SIGNAL_RI) != 0;
	}
	if (mask & RP_SIGNAL_DSR) {
		modem_dsr = (val & RP_SIGNAL_DSR) != 0;
	}
	if (mask & RP_SIGNAL_CD) {
		modem_cd = (val & RP_SIGNAL_CD) != 0;
	}
	return 1;
}

static int dosexecute(TCHAR *file, TCHAR *currentdir, TCHAR *parms, uae_u32 stack, uae_s32 priority, uae_u32 id, uae_u32 flags, uae_u8 *bin, uae_u32 binsize)
{
	int ret = filesys_shellexecute2(file, currentdir, parms, stack, priority, id, flags, bin, binsize, dos_execute_callback, NULL);
	return ret;
}

static int execute(LPCVOID pData)
{
	struct RPExecuteInfo *ei = (struct RPExecuteInfo*)pData;
	int v = dosexecute(ei->szFile, ei->szDirectory, ei->szParameters, ei->dwStackSize, ei->lPriority, ei->dwExecuteID, ei->dwFlags, ei->dwFileDataOffset ? (uae_u8*)pData + ei->dwFileDataOffset : NULL , ei->dwFileDataSize);
	return v;
}

extern int screenshotf(int monid, const TCHAR *spath, int mode, int doprepare, int imagemode, struct vidbuffer *vb);
extern int screenshotmode;
static int screencap(LPCVOID pData, struct AmigaMonitor *mon)
{
	struct RPScreenCapture *rpsc = (struct RPScreenCapture *)pData;
	if (rpsc->szScreenFiltered[0] || rpsc->szScreenRaw[0]) {
		int ossm = screenshotmode;
		DWORD ret = 0;
		int ok = 0;
		screenshotmode = 0;
		if (log_rp & 2)
			write_log(_T("'%s' '%s'\n"), rpsc->szScreenFiltered, rpsc->szScreenRaw);
		if (rpsc->szScreenFiltered[0])
			ok = screenshotf(0, rpsc->szScreenFiltered, 1, 1, 0, NULL);
		if (rpsc->szScreenRaw[0]) {
			ok |= screenshotf(0, rpsc->szScreenRaw, 1, 1, 1, NULL);
		}
		screenshotmode = ossm;
		if (log_rp & 2)
			write_log(_T("->%d\n"), ok);
		if (!ok)
			return RP_SCREENCAPTURE_ERROR;
		if (WIN32GFX_IsPicassoScreen(mon)) {
			ret |= RP_GUESTSCREENFLAGS_MODE_DIGITAL;
		} else {
			ret |= currprefs.gfx_resolution == RES_LORES ? RP_GUESTSCREENFLAGS_HORIZONTAL_LORES : ((currprefs.gfx_resolution == RES_SUPERHIRES) ? RP_GUESTSCREENFLAGS_HORIZONTAL_SUPERHIRES : 0);
			ret |= currprefs.ntscmode ? RP_GUESTSCREENFLAGS_MODE_NTSC : RP_GUESTSCREENFLAGS_MODE_PAL;
			ret |= currprefs.gfx_vresolution ? RP_GUESTSCREENFLAGS_VERTICAL_INTERLACED : 0;
		}
		return ret;
	}
	return RP_SCREENCAPTURE_ERROR;
}

static LRESULT CALLBACK RPHostMsgFunction2 (UINT uMessage, WPARAM wParam, LPARAM lParam,
	LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	if (log_rp & 1) {
		write_log (_T("RPFUNC(%s [%d], %08x, %08x, %p, %d, %08x)\n"),
		getmsg (uMessage), uMessage - WM_APP, wParam, lParam, pData, dwDataSize, lMsgFunctionParam);
		if (uMessage == RP_IPC_TO_GUEST_DEVICECONTENT) {
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
	case RP_IPC_TO_GUEST_PING:
		return TRUE;
	case RP_IPC_TO_GUEST_CLOSE:
		uae_quit ();
		return TRUE;
	case RP_IPC_TO_GUEST_RESET:
		uae_reset (wParam == RP_RESET_SOFT ? 0 : -1, 1);
		return TRUE;
	case RP_IPC_TO_GUEST_TURBO:
		{
			if (wParam & RP_TURBO_CPU)
				warpmode ((lParam & RP_TURBO_CPU) ? 1 : 0);
			if (wParam & RP_TURBO_FLOPPY)
				changed_prefs.floppy_speed = (lParam & RP_TURBO_FLOPPY) ? 0 : 100;
			set_config_changed ();
		}
		return TRUE;
	case RP_IPC_TO_GUEST_PAUSE:
		currentpausemode = pause_emulation;
		if (wParam ? 1 : 0 != pause_emulation ? 1 : 0) {
			pausemode (wParam ? -1 : 0);
			if (wParam) {
				currentpausemode = -1;
				return 2;
			}
		}
		return 1;
	case RP_IPC_TO_GUEST_VOLUME:
		currprefs.sound_volume_master = changed_prefs.sound_volume_master = 100 - (int)wParam;
		currprefs.sound_volume_cd = changed_prefs.sound_volume_cd = 100 - (int)wParam;
		set_volume (currprefs.sound_volume_master, 0);
		return TRUE;
#if 0
	case RP_IPC_TO_GUEST_ESCAPEKEY:
		rp_rpescapekey = wParam;
		rp_rpescapeholdtime = lParam;
		return TRUE;
#endif
	case RP_IPC_TO_GUEST_MOUSECAPTURE:
		{
			if (wParam & RP_MOUSECAPTURE_CAPTURED)
				setmouseactive(0, 1);
			else
				setmouseactive(0, 0);
		}
		return TRUE;
	case RP_IPC_TO_GUEST_DEVICECONTENT:
		{
			struct RPDeviceContent *dc = (struct RPDeviceContent*)pData;
			TCHAR *n = dc->szContent;
			int num = dc->btDeviceNumber;
			int ok = FALSE;
			switch (dc->btDeviceCategory)
			{
			case RP_DEVICECATEGORY_FLOPPY:
				if (n == NULL || n[0] == 0)
					disk_eject (num);
				else
					disk_insert (num, n, (dc->dwFlags & RP_DEVICEFLAGS_RW_READWRITE) == 0);
				ok = TRUE;
				break;
			case RP_DEVICECATEGORY_INPUTPORT:
				ok = port_insert (num, dc->dwInputDevice, dc->dwFlags, n);
				if (ok)
					inputdevice_updateconfig (&changed_prefs, &currprefs);
				break;
			case RP_DEVICECATEGORY_MULTITAPPORT:
				ok = port_insert (num + 2, dc->dwInputDevice, dc->dwFlags, n);
				if (ok)
					inputdevice_updateconfig (&changed_prefs, &currprefs);
				break;
			case RP_DEVICECATEGORY_CD:
				ok = cd_insert (num, n);
				break;
			}
			return ok;
		}
	case RP_IPC_TO_GUEST_SCREENMODE:
		{
			struct RPScreenMode *sm = (struct RPScreenMode*)pData;
			set_screenmode (sm, &changed_prefs);
			rp_set_hwnd_delayed ();
			return (LRESULT)INVALID_HANDLE_VALUE;
		}
	case RP_IPC_TO_GUEST_EVENT:
		{
			TCHAR *s = (WCHAR*)pData;
			parse_guest_event(s);
			return TRUE;
		}
	case RP_IPC_TO_GUEST_SCREENCAPTURE:
		{
			return screencap(pData, mon);
		}
	case RP_IPC_TO_GUEST_SAVESTATE:
		{
			TCHAR *s = (TCHAR*)pData;
			DWORD ret = FALSE;
			if (s == NULL) {
				savestate_initsave (NULL, 0, TRUE, true);
				return 1;
			}
			if (vpos == vsync_startline + 1) {
				savestate_initsave (_T(""), 1, TRUE, true);
				save_state (s, _T("AmigaForever"));
				ret = 1;
			} else {
				write_log(_T("RP_IPC_TO_GUEST_SAVESTATE unsupported emulation state\n"));
				//savestate_initsave (s, 1, TRUE);
				//ret = -1;
			}
			return ret;
		}
	case RP_IPC_TO_GUEST_LOADSTATE:
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
	case RP_IPC_TO_GUEST_DEVICEREADWRITE:
		{
			DWORD ret = FALSE;
			int device = LOBYTE(wParam);
			if (device == RP_DEVICECATEGORY_FLOPPY) {
				int num = HIBYTE(wParam);
				if ((lParam == RP_DEVICE_READONLY || lParam == RP_DEVICE_READWRITE) && num >= 0 && num <= 4) {
					ret = disk_setwriteprotect (&currprefs, num, currprefs.floppyslots[num].df, lParam == RP_DEVICE_READONLY);
					if (ret)
						DISK_reinsert(num);
				}
			}
			return ret ? (LPARAM)1 : 0;
		}
	case RP_IPC_TO_GUEST_FLUSH:
		return 1;
	case RP_IPC_TO_GUEST_QUERYSCREENMODE:
		{
			screenmode_request = 1;
			write_log (_T("RP_IPC_TO_GUEST_QUERYSCREENMODE -> RP_IPC_TO_HOST_SCREENMODE screenmode_request started\n"));
			return 1;
		}
	case RP_IPC_TO_GUEST_GUESTAPIVERSION:
		{
			return MAKELONG(7, 6);
		}
	case RP_IPC_TO_GUEST_SHOWOPTIONS:
		inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
		return 1;
	case RP_IPC_TO_GUEST_DEVICEACTIVITY:
		return deviceactivity(wParam, lParam);
	case RP_IPC_TO_GUEST_SCREENOVERLAY:
		return screenoverlay(pData);
	case RP_IPC_TO_GUEST_DELETESCREENOVERLAY:
		return deletescreenoverlay(wParam);
	case RP_IPC_TO_GUEST_MOVESCREENOVERLAY:
		return movescreenoverlay(wParam, lParam);
	case RP_IPC_TO_GUEST_SENDMOUSEEVENTS:
		sendmouseevents = (int)wParam;
		if (sendmouseevents) {
			LPARAM lp = MAKELONG(mouseevent_x, mouseevent_y);
			RPPostMessagex(RP_IPC_TO_HOST_MOUSEMOVE, 0, lp, &guestinfo);
		}
		return 1;
	case RP_IPC_TO_GUEST_SHOWDEBUGGER:
		activate_debugger();
		return 1;
	case RP_IPC_TO_GUEST_EXECUTE:
		return execute(pData);
	case RP_IPC_TO_GUEST_DEVICEWRITEBYTE:
		return rp_readmodemint(wParam, lParam);
	case RP_IPC_TO_GUEST_DEVICESETSIGNALS:
		return rp_readmodemstatusint(wParam, lParam);
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

void rp_keymap(TrapContext *ctx, uaecptr ptr, uae_u32 size)
{
	uae_u8 *p = xmalloc(uae_u8, size);
	if (p && size) {
		trap_get_bytes(ctx, p, ptr, size);
		RPSendMessagex(RP_IPC_TO_HOST_KEYBOARDLAYOUT, 0, 0, p, size, &guestinfo, NULL);
	}
	xfree(p);
}

static int rp_hostversion (int *ver, int *rev, int *build)
{
	LRESULT lr = 0;
	if (!RPSendMessagex (RP_IPC_TO_HOST_HOSTVERSION, 0, 0, NULL, 0, &guestinfo, &lr))
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
	for (int i = 0; i < 4; i++) {
		dacttype[i] = -1;
		dactmask[i] = 0;
	}
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
	RPPostMessagex (RP_IPC_TO_HOST_CLOSED, 0, 0, &guestinfo);
	RPUninitializeGuest (&guestinfo);
}


int rp_close (void)
{
	if (!cando ())
		return 0;
	RPSendMessagex (RP_IPC_TO_HOST_CLOSE, 0, 0, NULL, 0, &guestinfo, NULL);
	return 1;
}

HWND rp_getparent (void)
{
	LRESULT lr;
	if (!initialized)
		return NULL;
	RPSendMessagex (RP_IPC_TO_HOST_PARENT, 0, 0, NULL, 0, &guestinfo, &lr);
	return (HWND)lr;
}

extern int rp_input_enum (struct RPInputDeviceDescription *desc, int);

static void sendenum (void)
{
	TCHAR tmp[MAX_DPATH];
	TCHAR *p1, *p2;
	struct RPInputDeviceDescription desc;
	int cnt, max = 3;

	WIN32GUI_LoadUIString (IDS_KEYJOY, tmp, sizeof tmp / sizeof (TCHAR));
	_tcscat (tmp, _T("\n"));
	p1 = tmp;
	cnt = 0;
	while (max--) {
		p2 = _tcschr (p1, '\n');
		if (p2 && p2[0] != '\0') {
			TCHAR tmp2[100];
			*p2++ = 0;
			memset (&desc, 0, sizeof desc);
			_stprintf (tmp2, _T("KeyboardLayout%d"), cnt);
			_tcscpy (desc.szHostInputID, tmp2);
			_tcscpy (desc.szHostInputName, p1);
			desc.dwHostInputType= (RP_HOSTINPUT_KEYJOY_MAP1 + cnt);
			desc.dwInputDeviceFeatures = RP_FEATURE_INPUTDEVICE_JOYSTICK;
			if (cnt == 0)
				desc.dwInputDeviceFeatures |= RP_FEATURE_INPUTDEVICE_JOYPAD;
			if (log_rp & 2)
				write_log(_T("Enum%d: '%s' '%s'\n"), cnt, desc.szHostInputName, desc.szHostInputID);
			RPSendMessagex (RP_IPC_TO_HOST_INPUTDEVICE, 0, 0, &desc, sizeof desc, &guestinfo, NULL);
			cnt++;
			p1 = p2;
		} else {
			break;
		}
	}
	memset (&desc, 0, sizeof desc);
	_tcscpy (desc.szHostInputID, _T("KeyboardCustom"));
	_tcscpy (desc.szHostInputName, _T("KeyboardCustom"));
	desc.dwHostInputType = RP_HOSTINPUT_KEYBOARD;
	desc.dwInputDeviceFeatures = RP_FEATURE_INPUTDEVICE_JOYSTICK | RP_FEATURE_INPUTDEVICE_JOYPAD;
	RPSendMessagex (RP_IPC_TO_HOST_INPUTDEVICE, 0, 0, &desc, sizeof desc, &guestinfo, NULL);
	cnt = 0;
	while ((cnt = rp_input_enum (&desc, cnt)) >= 0) {
		if (log_rp & 2)
			write_log(_T("Enum%d: '%s' '%s' (%x/%x)\n"),
				cnt, desc.szHostInputName, desc.szHostInputID, desc.dwHostInputVendorID, desc.dwHostInputProductID);
		RPSendMessagex (RP_IPC_TO_HOST_INPUTDEVICE, 0, 0, &desc, sizeof desc, &guestinfo, NULL);
	}
	memset (&desc, 0, sizeof desc);
	desc.dwHostInputType = RP_HOSTINPUT_END;
	RPSendMessagex (RP_IPC_TO_HOST_INPUTDEVICE, 0, 0, &desc, sizeof desc, &guestinfo, NULL);
}

void rp_enumdevices (void)
{
	if (!cando ())
		return;
	sendenum ();
	rp_input_change (0);
	rp_input_change (1);
	rp_input_change (2);
	rp_input_change (3);
}

static void sendfeatures (void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	DWORD feat;

	feat = RP_FEATURE_POWERLED | RP_FEATURE_SCREEN1X | RP_FEATURE_FULLSCREEN;
	feat |= RP_FEATURE_PAUSE | RP_FEATURE_TURBO_CPU | RP_FEATURE_TURBO_FLOPPY | RP_FEATURE_VOLUME | RP_FEATURE_SCREENCAPTURE;
	feat |= RP_FEATURE_DEVICEREADWRITE;
	feat |= RP_FEATURE_SCREENOVERLAY;
	if (WIN32GFX_IsPicassoScreen(mon)) {
		feat |= RP_FEATURE_SCREEN2X | RP_FEATURE_SCREEN3X | RP_FEATURE_SCREEN4X;
	} else {
		feat |= RP_FEATURE_SCREEN2X | RP_FEATURE_SCREEN3X | RP_FEATURE_SCREEN4X;
		feat |= RP_FEATURE_SCALING_SUBPIXEL | RP_FEATURE_SCALING_STRETCH | RP_FEATURE_SCANLINES;
	}
	feat |= RP_FEATURE_INPUTDEVICE_MOUSE;
	feat |= RP_FEATURE_INPUTDEVICE_JOYSTICK;
	feat |= RP_FEATURE_INPUTDEVICE_GAMEPAD;
	feat |= RP_FEATURE_INPUTDEVICE_JOYPAD;
	feat |= RP_FEATURE_INPUTDEVICE_ANALOGSTICK;
	feat |= RP_FEATURE_INPUTDEVICE_LIGHTPEN;
	feat |= RP_FEATURE_RAWINPUT_EVENT;
	if (!is_savestate_incompatible()) {
		feat |= RP_FEATURE_STATE;
	}
	write_log (_T("RP_IPC_TO_HOST_FEATURES=%08x %d\n"), feat, WIN32GFX_IsPicassoScreen(mon));
	RPSendMessagex (RP_IPC_TO_HOST_FEATURES, feat, 0, NULL, 0, &guestinfo, NULL);
}

static bool ishd(int n)
{
	struct uaedev_config_data *uci = &currprefs.mountconfig[n];
	int num = -1;
	if (uci->ci.controller_type == HD_CONTROLLER_TYPE_UAE) {
		if (uci->ci.type == UAEDEV_DIR)
			return true;
		num = uci->ci.controller_unit;
	} else if (uci->ci.controller_type >= HD_CONTROLLER_TYPE_IDE_FIRST && uci->ci.controller_type <= HD_CONTROLLER_TYPE_IDE_LAST) {
		num = uci->ci.controller_unit;
	} else if (uci->ci.controller_type >= HD_CONTROLLER_TYPE_SCSI_FIRST && uci->ci.controller_type <= HD_CONTROLLER_TYPE_SCSI_LAST) {
		num = uci->ci.controller_unit;
	}
	return num >= 0 && uci->ci.type == UAEDEV_HDF;
}

void rp_fixup_options (struct uae_prefs *p)
{
	struct monconfig *gm = &p->gfx_monitor[0];
	struct RPScreenMode sm;
	LRESULT lr;

	if (!initialized)
		return;

	write_log (_T("rp_fixup_options(escapekey=%d,escapeholdtime=%d,screenmode=%d,inputmode=%d)\n"),
		rp_rpescapekey, rp_rpescapeholdtime, rp_screenmode, rp_inputmode);

	if (RPSendMessage(RP_IPC_TO_HOST_HOSTAPIVERSION, 0, 0, NULL, 0, &guestinfo, &lr)) {
		WORD major = LOWORD(lr);
		WORD minor = HIWORD(lr);
		interpolation_v102 = major > 10 || (major == 10 && minor >= 2);
	}

	sendmouseevents = 0;
	mouseevent_x = mouseevent_y = 0;
	mouseevent_buttons = 0;
	max_horiz_dbl = currprefs.gfx_max_horizontal;
	max_vert_dbl = currprefs.gfx_max_vertical;
	maxjports = (rp_version * 256 + rp_revision) >= 2 * 256 + 3 ? MAX_JPORTS : 2;

	write_log (_T("w=%dx%d fs=%dx%d pos=%dx%d %dx%d HV=%d,%d J=%d\n"),
		gm->gfx_size_win.width, gm->gfx_size_win.height,
		gm->gfx_size_fs.width, gm->gfx_size_fs.height,
		p->gfx_xcenter_pos, p->gfx_ycenter_pos,
		p->gfx_xcenter_size, p->gfx_ycenter_size,
		max_horiz_dbl, max_vert_dbl, maxjports);

	sendfeatures ();
	sendenum ();

	changed_prefs.win32_borderless = currprefs.win32_borderless = 1;
	rp_filter_default = rp_filter = currprefs.gf[0].gfx_filter;

	fixup_size (p);
	get_screenmode (&sm, p, false);
	sm.dwScreenMode &= ~RP_SCREENMODE_SCALEMASK;
	sm.dwScreenMode |= rp_screenmode;
	set_screenmode (&sm, &currprefs);
	set_screenmode (&sm, &changed_prefs);

	/* floppy drives */
	floppy_mask = 0;
	for (int i = 0; i < 4; i++) {
		if (p->floppyslots[i].dfxtype >= 0)
			floppy_mask |= 1 << i;
	}
	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_FLOPPY, floppy_mask, NULL, 0, &guestinfo, NULL);

	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_INPUTPORT, (1 << 2) - 1, NULL, 0, &guestinfo, NULL);
	rp_input_change (0);
	rp_input_change (1);
	gameportmask[0] = gameportmask[1] = gameportmask[2] = gameportmask[3] = 0;

	int parportmask = 0;
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < MAX_JPORT_DEVS; j++) {
			if (p->jports[i + 2].jd[j].idc.configname[0] || p->jports[i + 2].jd[j].idc.name[0] || p->jports[i + 2].jd[j].idc.shortid[0])
				parportmask |= 1 << i;
		}
	}
	if (parportmask) {
		RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_MULTITAPPORT, parportmask, NULL, 0, &guestinfo, NULL);
		rp_input_change (2);
		rp_input_change (3);
	}

	hd_mask = 0;
	cd_mask = 0;
	for (int i = 0; i < currprefs.mountitems && i < 32; i++) {
		if (ishd(i))
			hd_mask |= 1 << i;
	}
	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_HD, hd_mask, NULL, 0, &guestinfo, NULL);
	if (hd_mask) {
		for (int i = 0; i < currprefs.mountitems && i < 32; i++) {
			struct uaedev_config_data *uci = &currprefs.mountconfig[i];
			if (ishd(i) && ((1 << i) & hd_mask))
				rp_harddrive_image_change (i, uci->ci.readonly, uci->ci.rootdir);
		}
	}

	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (p->cdslots[i].inuse)
			cd_mask |= 1 << i;
	}
	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_CD, cd_mask, NULL, 0, &guestinfo, NULL);
	if (cd_mask) {
		for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
			if (p->cdslots[i].inuse)
				rp_cd_image_change (i, p->cdslots[i].name);
		}
	}

	rp_update_volume (&currprefs);
	rp_turbo_cpu (currprefs.turbo_emulation);
	rp_turbo_floppy (currprefs.floppy_speed == 0);
	for (int i = 0; i <= 4; i++)
		rp_update_leds (i, 0, -1, 0);
	set_config_changed ();

	write_log(_T("rp_fixup_options end\n"));

}

static void rp_device_writeprotect (int dev, int num, bool writeprotected)
{
	if (!cando ())
		return;
	if (rp_version * 256 + rp_revision < 2 * 256 + 3)
		return;
	if (log_rp & 1)
		write_log(_T("RP_IPC_TO_HOST_DEVICEREADWRITE %d %d %d\n"), dev, num, writeprotected);
	RPSendMessagex (RP_IPC_TO_HOST_DEVICEREADWRITE, MAKEWORD(dev, num), writeprotected ? RP_DEVICE_READONLY : RP_DEVICE_READWRITE, NULL, 0, &guestinfo, NULL);
}

static void rp_device_change (int dev, int num, int mode, bool readonly, const TCHAR *content, bool preventrecursive)
{
	struct RPDeviceContent dc = { 0 };

	if (!cando ())
		return;
	if (preventrecursive && recursive_device)
		return;
	dc.btDeviceCategory = dev;
	dc.btDeviceNumber = num;
	dc.dwInputDevice = mode;
	dc.dwFlags = readonly ? RP_DEVICEFLAGS_RW_READONLY : RP_DEVICEFLAGS_RW_READWRITE;
	if (content)
		_tcscpy (dc.szContent, content);
	if (log_rp & 1)
		write_log (_T("RP_IPC_TO_HOST_DEVICECONTENT cat=%d num=%d type=%d flags=%x '%s'\n"),
		dc.btDeviceCategory, dc.btDeviceNumber, dc.dwInputDevice, dc.dwFlags, dc.szContent);
	RPSendMessagex (RP_IPC_TO_HOST_DEVICECONTENT, 0, 0, &dc, sizeof(struct RPDeviceContent), &guestinfo, NULL);
}

void rp_input_change (int num)
{
	int sub = 0;
	int j = jsem_isjoy(num, sub, &currprefs);
	int m = jsem_ismouse(num, sub, &currprefs);
	int k = jsem_iskbdjoy(num, &currprefs);
	TCHAR name[MAX_DPATH];
	int mode;

	if (num >= maxjports)
		return;

	name[0] = 0;
	if (JSEM_ISCUSTOM(num, sub, &currprefs)) {
		port_get_custom (num, sub, name);
	} else if (k >= 0) {
		_stprintf (name, _T("KeyboardLayout%d"), k);
	} else if (j >= 0) {
		_tcscpy (name, inputdevice_get_device_unique_name (IDTYPE_JOYSTICK, j));
	} else if (m >= 0) {
		_tcscpy (name, inputdevice_get_device_unique_name (IDTYPE_MOUSE, m));
	}
	mode = RP_INPUTDEVICE_EMPTY;
	for (int i = 0; inputdevmode[i * 2]; i++) {
		if (inputdevmode[i * 2 + 1] == currprefs.jports[num].jd[sub].mode) {
			mode = inputdevmode[i * 2 + 0];
			break;
		}
	}
	if (log_rp & 1)
		write_log(_T("PORT%d: '%s':%d\n"), num, name, mode);
	if (num >= 2) {
		rp_device_change (RP_DEVICECATEGORY_MULTITAPPORT, num - 2, mode, true, name, true);
	} else {
		rp_device_change (RP_DEVICECATEGORY_INPUTPORT, num, mode, true, name, true);
	}
}
void rp_disk_image_change (int num, const TCHAR *name, bool writeprotected)
{
	if (!cando())
		return;
	TCHAR tmp[MAX_DPATH], *p = NULL;
	if (name && name[0]) {
		cfgfile_resolve_path_out_load(name, tmp, MAX_DPATH, PATH_FLOPPY);
		p = tmp;
	}
	rp_device_change (RP_DEVICECATEGORY_FLOPPY, num, 0, writeprotected, p, false);
	rp_device_writeprotect (RP_DEVICECATEGORY_FLOPPY, num, writeprotected);
}
void rp_harddrive_image_change (int num, bool readonly, const TCHAR *name)
{
	if (!cando())
		return;
	TCHAR tmp[MAX_DPATH], *p = NULL;
	if (name && name[0]) {
		cfgfile_resolve_path_out_load(name, tmp, MAX_DPATH, PATH_HDF);
		p = tmp;
	}
	rp_device_change (RP_DEVICECATEGORY_HD, num, 0, readonly, p, false);
}
void rp_cd_image_change (int num, const TCHAR *name)
{
	if (!cando())
		return;
	TCHAR tmp[MAX_DPATH], *p = NULL;
	if (name && name[0]) {
		cfgfile_resolve_path_out_load(name, tmp, MAX_DPATH, PATH_CD);
		p = tmp;
	}
	rp_device_change (RP_DEVICECATEGORY_CD, num, 0, true, p, false);
}

void rp_floppy_device_enable (int num, bool enabled)
{
	if (!cando ())
		return;
	if (enabled)
		floppy_mask |= 1 << num;
	else
		floppy_mask &= ~(1 << num);
	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_FLOPPY, floppy_mask, NULL, 0, &guestinfo, NULL);
}

void rp_hd_device_enable (int num, bool enabled)
{
	if (!cando ())
		return;
	if (enabled)
		hd_mask |= 1 << num;
	else
		hd_mask &= ~(1 << num);
	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_HD, hd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_cd_device_enable (int num, bool enabled)
{
	if (!cando ())
		return;
	if (enabled)
		cd_mask |= 1 << num;
	else
		cd_mask &= ~(1 << num);
	RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_CD, cd_mask, NULL, 0, &guestinfo, NULL);
}

void rp_floppy_track (int floppy, int track)
{
	static int oldtrack[4];
	if (!cando ())
		return;
	if (oldtrack[floppy] == track)
		return;
	oldtrack[floppy] = track;
	RPPostMessagex (RP_IPC_TO_HOST_DEVICESEEK, MAKEWORD (RP_DEVICECATEGORY_FLOPPY, floppy), track, &guestinfo);
}

void rp_update_leds (int led, int onoff, int brightness, int write)
{
	static int oldled[5];
	int ledstate;

	if (!cando ())
		return;
	if (led < 0 || led > 4)
		return;
	if (onoff < 0)
		return;
	if (brightness < 0)
		brightness = onoff ? 250 : 0;
	switch (led)
	{
	case LED_POWER:
		ledstate = brightness >= 250 ? 100 : brightness * 5 / 26 + 49;
		if (ledstate == oldled[led])
			return;
		oldled[led] = ledstate;
		RPPostMessagex(RP_IPC_TO_HOST_POWERLED, ledstate, 0, &guestinfo);
		break;
	case LED_DF0:
	case LED_DF1:
	case LED_DF2:
	case LED_DF3:
		ledstate = onoff ? 1 : 0;
		ledstate |= write ? 2 : 0;
		if (ledstate == oldled[led])
			return;
		oldled[led] = ledstate;
		RPPostMessagex(RP_IPC_TO_HOST_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_FLOPPY, led - LED_DF0),
			MAKELONG ((ledstate & 1) ? -1 : 0, (ledstate & 2) ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ) , &guestinfo);
		break;
	}
}

void rp_update_gameport (int port, int mask, int onoff)
{
#if 0
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
		RPPostMessagex (RP_IPC_TO_HOST_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_INPUTPORT, port),
			gameportmask[port], &guestinfo);
	}
#endif
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
		RPPostMessagex (RP_IPC_TO_HOST_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_HD, num),
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
		RPSendMessagex (RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_CD, cd_mask, NULL, 0, &guestinfo, NULL);
	}
	if (onoff) {
		RPPostMessage (RP_IPC_TO_HOST_DEVICEACTIVITY, MAKEWORD (RP_DEVICECATEGORY_CD, num),
			MAKELONG (200, RP_DEVICEACTIVITY_READ), &guestinfo);
	}
}

void rp_update_volume (struct uae_prefs *p)
{
	if (!cando ())
		return;
	RPSendMessagex (RP_IPC_TO_HOST_VOLUME, (WPARAM)(100 - p->sound_volume_master), 0, NULL, 0, &guestinfo, NULL);
}

void rp_pause (int paused)
{
	if (!cando ())
		return;
	if (isrecursive ())
		return;
	if (currentpausemode != paused)
		RPSendMessagex (RP_IPC_TO_HOST_PAUSE, (WPARAM)paused, 0, NULL, 0, &guestinfo, NULL);
	currentpausemode = paused;
}

static void rp_mouse (void)
{
	int flags = 0;

	if (!cando ())
		return;
	if (mousemagic)
		flags |= RP_MOUSECAPTURE_INTEGRATED;
	if (mousecapture)
		flags |= RP_MOUSECAPTURE_CAPTURED;
	RPSendMessagex (RP_IPC_TO_HOST_MOUSECAPTURE, flags, 0, NULL, 0, &guestinfo, NULL);
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

void rp_activate (WPARAM active, LPARAM lParam)
{
	if (!cando ())
		return;
	//RPSendMessagex (active ? RP_IPC_TO_HOST_ACTIVATED : RP_IPC_TO_HOST_DEACTIVATED, 0, lParam, NULL, 0, &guestinfo, NULL);
	RPPostMessagex (active ? RP_IPC_TO_HOST_ACTIVATED : RP_IPC_TO_HOST_DEACTIVATED, 0, lParam, &guestinfo);
}

void rp_turbo_cpu (int active)
{
	if (!cando ())
		return;
	if (recursive_device)
		return;
	RPSendMessagex (RP_IPC_TO_HOST_TURBO, RP_TURBO_CPU, active ? RP_TURBO_CPU : 0, NULL, 0, &guestinfo, NULL);
}

void rp_turbo_floppy (int active)
{
	if (!cando ())
		return;
	if (recursive_device)
		return;
	RPSendMessagex (RP_IPC_TO_HOST_TURBO, RP_TURBO_FLOPPY, active ? RP_TURBO_FLOPPY : 0, NULL, 0, &guestinfo, NULL);
}

void rp_set_hwnd_delayed (void)
{
	struct amigadisplay *ad = &adisplays[0];
	hwndset_delay = 3;
	int idx = ad->gf_index;
	if (currprefs.gf[idx].gfx_filter_autoscale == AUTOSCALE_RESIZE) {
		hwndset_delay += 10;
	}
	if (log_rp && initialized)
		write_log (_T("RP_IPC_TO_HOST_SCREENMODE delay started (%d)\n"), hwndset_delay);

	screenmode_request = 0;
}

void rp_set_hwnd (HWND hWnd)
{
	struct RPScreenMode sm = { 0 };

	if (!initialized)
		return;
	if (hwndset_delay) {
		if (hWnd) {
			if (log_rp)
				write_log (_T("RP_IPC_TO_HOST_SCREENMODE, delay=%d\n"), hwndset_delay);
			return;
		}
		hwndset_delay = 0;
	}
	if (log_rp)
		write_log (_T("RP_IPC_TO_HOST_SCREENMODE\n"));
	guestwindow = hWnd;
	get_screenmode (&sm, &currprefs, false);
	if (hWnd != NULL)
		hwndset = 1;
	RPSendMessagex (RP_IPC_TO_HOST_SCREENMODE, 0, 0, &sm, sizeof sm, &guestinfo, NULL); 
}

void rp_screenmode_changed (void)
{
	if (log_rp)
		write_log (_T("rp_screenmode_changed\n"));
	if (!screenmode_request && !hwndset_delay) {
		screenmode_request = 6;
		if (log_rp)
			write_log (_T("rp_screenmode_changed -> screenmode_request started\n"));
	}
}

void rp_set_enabledisable (int enabled)
{
	if (!cando ())
		return;
	//RPSendMessagex (enabled ? RP_IPC_TO_HOST_ENABLED : RP_IPC_TO_HOST_DISABLED, 0, 0, NULL, 0, &guestinfo, NULL);
	RPPostMessagex (enabled ? RP_IPC_TO_HOST_ENABLED : RP_IPC_TO_HOST_DISABLED, 0, 0, &guestinfo);
}

void rp_rtg_switch (void)
{
	if (!cando ())
		return;
	sendfeatures ();
}

static uae_u64 esctime;
static int releasetime, releasenum;
static int rp_prev_w, rp_prev_h, rp_prev_x, rp_prev_y;

void rp_vsync(void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	if (!initialized)
		return;
	if (!hwndset_delay && !delayed_refresh && !screenmode_request && isfullscreen()) {
		int w, h, x, y;
		get_custom_raw_limits(&w, &h, &x, &y);
		if (x != rp_prev_x || y != rp_prev_y ||
			w != rp_prev_w || h != rp_prev_h) {
			screenmode_request = 6;
			if (log_rp)
				write_log(_T("screenmode_request = %d\n"), screenmode_request);
			rp_prev_x = x;
			rp_prev_y = y;
			rp_prev_w = w;
			rp_prev_h = h;
		}
	}
	if (delayed_refresh) {
		if (gett() >= delayed_refresh + 50) {
			if (pause_emulation && D3D_refresh) {
				D3D_refresh(0);
			}
			delayed_refresh = 0;
		}
	}
	if (hwndset_delay > 0) {
		hwndset_delay--;
		if (hwndset_delay == 0) {
			if (log_rp)
				write_log(_T("rp_set_hwnd delay expired\n"));
			rp_set_hwnd(mon->hAmigaWnd);
		}
	}

	if (screenmode_request) {
		screenmode_request--;
		if (screenmode_request == 0) {
			if (log_rp)
				write_log (_T("RP_IPC_TO_HOST_SCREENMODE screenmode_request expired\n"));
			struct RPScreenMode sm = { 0 };
			get_screenmode (&sm, &currprefs, true);
			RPSendMessagex (RP_IPC_TO_HOST_SCREENMODE, 0, 0, &sm, sizeof sm, &guestinfo, NULL);
		}
	}
	if (magicmouse_alive () != mousemagic)
		rp_mouse_magic (magicmouse_alive ());
#if 0
	uae_u64 t;
	if (!esctime && !releasetime)
		return;
	t = gett ();
	if (releasetime > 0) {
		releasetime--;
		if (!releasetime)
			my_kbd_handler (releasenum, rp_rpescapekey, 0);
	}
	if (esctime && t >= esctime) {
		RPSendMessagex (RP_IPC_TO_HOST_ESCAPED, 0, 0, NULL, 0, &guestinfo, NULL);
		releasetime = -1;
		esctime = 0;
	}
#endif
}

int rp_checkesc (int scancode, int pressed, int num)
{
	uae_u64 t;

	if (!initialized)
		goto end;
	if (scancode != rp_rpescapekey)
		goto end;
	if (releasetime < 0 && !pressed) {
		releasetime = 0;
		goto end;
	}
	t = gett ();
	if (!t)
		goto end;
	if (pressed) {
		esctime = t + rp_rpescapeholdtime;
		return 1;
	}
	my_kbd_handler (num, scancode, 1, false);
	releasetime = 10;
	releasenum = num;
	esctime = 0;
	return 1;
end:
	esctime = 0;
	return 0;
}

USHORT rp_rawbuttons(LPARAM lParam, USHORT usButtonFlags)
{
	LRESULT lr;
	if (!initialized)
		return usButtonFlags;
	if (RPSendMessage(RP_IPC_TO_HOST_RAWINPUT_EVENT, 0, lParam, NULL, 0, &guestinfo, &lr))
		usButtonFlags = (USHORT)lr;
	return usButtonFlags;
}

bool rp_ismouseevent(void)
{
	return sendmouseevents != 0;
}

bool rp_mouseevent(int x, int y, int buttons, int buttonmask)
{
#if 0
	uae_u8 *data;
	static int ovl_idx = 10;
	static int ovl_add;
	if (buttons > 0 && (buttons & 1)) {
		data = xcalloc(uae_u8, 10 * 10 * 4);
		for (int i = 0; i < 10 * 10; i++) {
			data[i * 4 + 0] = 0xff;
			data[i * 4 + 1] = 0xff;
			data[i * 4 + 2] = 0x00;
			data[i * 4 + 3] = 0xff;
		}

		struct extoverlay eo = { 0 };
		eo.idx = ovl_idx;
		eo.xpos = 100 + ovl_idx * 50;
		eo.ypos = 100;
		eo.width = 10;
		eo.height = 10;
		eo.data = data;
		int ret = D3D_extoverlay(&eo);
		ovl_idx--;
	}
	if (buttons > 0 && (buttons & 2)) {
		struct extoverlay eo = { 0 };
		ovl_idx++;
		eo.idx = ovl_idx;
		eo.width = -1;
		eo.height = -1;
		int ret = D3D_extoverlay(&eo);
	}

	for (int i = 0; i < ovl_idx; i++) {
		struct extoverlay eo = { 0 };
		eo.idx = i;
		eo.xpos = 100 + i * 50;
		eo.ypos = 100 + ovl_add * (i + 1);
		int ret = D3D_extoverlay(&eo);
	}
	ovl_add++;
#endif

	if (!sendmouseevents) {
		if (x > -30000 && y > -30000) {
			mouseevent_x = x;
			mouseevent_y = y;
		}
		return false;
	}
	if (x > -30000 && y > -30000 && (x != mouseevent_x || y != mouseevent_y)) {
		LPARAM lParam = MAKELONG(x, y);
		RPPostMessagex(RP_IPC_TO_HOST_MOUSEMOVE, 0, lParam, &guestinfo);
		mouseevent_x = x;
		mouseevent_y = y;
	}
	if (buttons >= 0 && (buttons & buttonmask) != (mouseevent_buttons & buttonmask)) {
		LPARAM lParam = MAKELONG(mouseevent_x, mouseevent_y);
		mouseevent_buttons &= ~buttonmask;
		mouseevent_buttons |= buttons & buttonmask;
		RPPostMessagex(RP_IPC_TO_HOST_MOUSEBUTTON, mouseevent_buttons, lParam, &guestinfo);
	}
	return true;
}

int rp_isactive (void)
{
	return initialized;
}

void rp_reset(void)
{
	if (!initialized)
		return;
	device_add_vsync_pre(rp_vsync);
}

bool rp_ismodem(void)
{
	return rp_modem != 0;
}
void rp_writemodem(uae_u8 v)
{
	if (!initialized) {
		return;
	}
	if (!rp_modemopen) {
		rp_modemstate(1);
	}
	WPARAM unit = MAKEWORD(RP_DEVICECATEGORY_MODEM, 0);
	RPSendMessagex(RP_IPC_TO_HOST_DEVICEWRITEBYTE, unit, v, NULL, 0, &guestinfo, NULL);
}
void rp_modemstate(int state)
{
	if (!initialized) {
		return;
	}
	if (state == rp_modemopen) {
		return;
	}
	WPARAM unit = MAKEWORD(RP_DEVICECATEGORY_MODEM, 0);
	if (state) {
		if (log_rp)
			write_log(_T("RP: modem open\n"));
		RPSendMessagex(RP_IPC_TO_HOST_DEVICEOPEN, unit, 0, NULL, 0, &guestinfo, NULL);
	} else {
		if (log_rp)
			write_log(_T("RP: modem close\n"));
		RPSendMessagex(RP_IPC_TO_HOST_DEVICECLOSE, unit, 0, NULL, 0, &guestinfo, NULL);
	}
	rp_modemopen = state;
}
void rp_writemodemstatus(bool rts, bool rtschanged, bool dtr, bool dtrchanged)
{
	if (!initialized) {
		return;
	}
	WPARAM unit = MAKEWORD(RP_DEVICECATEGORY_MODEM, 0);
	LPARAM l = MAKELONG((rtschanged ? RP_SIGNAL_RTS :0) | (dtrchanged ? RP_SIGNAL_DTR : 0), (rts ? RP_SIGNAL_RTS : 0) | (dtr ? RP_SIGNAL_DTR : 0));
	RPSendMessagex(RP_IPC_TO_HOST_DEVICESETSIGNALS, unit, l, NULL, 0, &guestinfo, NULL);

}

bool rp_isprinter(void)
{
	return rp_printer != 0;
}
bool rp_isprinteropen(void)
{
	return rp_printer != 0 && rp_printeropen != 0;
}
void rp_writeprinter(uae_char *b, int len)
{
	if (!initialized) {
		return;
	}
	WPARAM unit = MAKEWORD(RP_DEVICECATEGORY_PRINTER, 0);
	if (!b) {
		if (rp_printeropen) {
			if (log_rp)
				write_log(_T("RP: printer close\n"));
			RPSendMessagex(RP_IPC_TO_HOST_DEVICECLOSE, unit, 0, NULL, 0, &guestinfo, NULL);
		}
		rp_printeropen = 0;
		return;
	}
	if (!rp_printeropen) {
		if (log_rp)
			write_log(_T("RP: printer open\n"));
		RPSendMessagex(RP_IPC_TO_HOST_DEVICEOPEN, unit, 0, NULL, 0, &guestinfo, NULL);
		rp_printeropen = 1;
	}
	if (len == 1)
		RPSendMessagex(RP_IPC_TO_HOST_DEVICEWRITEBYTE, unit, (LPARAM)*b & 0xFF, 0, 0, &guestinfo, NULL);
	else
		RPSendMessagex(RP_IPC_TO_HOST_DEVICEWRITEBYTES, unit, 0, b, len, &guestinfo, NULL);
}

void rp_drive_access_error(int error)
{
	if (!initialized)
		return;
	RPSendMessage(RP_IPC_TO_HOST_STATUS, RP_STATUS_PARTITION_TABLE_WRITE_ERROR, error, NULL, 0, &guestinfo, NULL);
}

void rp_test(void)
{
#if 0
	struct AmigaMonitor *mon = &AMonitors[0];
	struct RPScreenCapture rpsc = { 0 };

	_tcscpy(rpsc.szScreenRaw, _T("c:\\temp\\test_r.png"));
	_tcscpy(rpsc.szScreenFiltered, _T("c:\\temp\\test_f.png"));

	screencap((void*)&rpsc, mon);
#endif
#if 0
	FILE *f = fopen("c:\\temp\\amiga.bin", "rb");
	uae_u8 buf[100000];
	int size = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	dosexecute(_T("c:list"), _T("sys:"), _T(""), 4000, 3, 0x12345678, 3, buf, size);

#endif
}
