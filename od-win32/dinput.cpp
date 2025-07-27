/*
* UAE - The Un*x Amiga Emulator
*
* Win32 DirectInput/Windows XP RAWINPUT interface
*
* Copyright 2002 - 2011 Toni Wilen
*/

int rawinput_enabled_hid = -1;
int xinput_enabled = 0;
// 1 = keyboard
// 2 = mouse
// 4 = joystick
int rawinput_log = 0;
int tablet_log = 0;

int no_rawinput = 0;
int no_directinput = 0;
int no_windowsmouse = 0;
int winekeyboard = 0;
int key_swap_hack = 0;

#define _WIN32_WINNT 0x501 /* enable RAWINPUT support */

#define RAWINPUT_DEBUG 0
#define DI_DEBUG 1
#define IGNOREEVERYTHING 0
#define DEBUG_SCANCODE 0
#define OUTPUTDEBUG 0

#define NEGATIVEMINHACK 0

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include <windows.h>
#include <dinput.h>

#include "sysdeps.h"
#include "options.h"
#include "traps.h"
#include "rp.h"
#include "inputdevice.h"
#include "keybuf.h"
#include "xwin.h"
#include "uae.h"
#include "catweasel.h"
#include "keyboard.h"
#include "custom.h"
#include "render.h"
#include "akiko.h"
#include "clipboard.h"
#include "tabletlibrary.h"
#include "gui.h"

#include <winioctl.h>
#include <ntddkbd.h>
#include <ntddpar.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <wbemidl.h>
#include <oleauto.h>


extern "C" 
{
#include <hidsdi.h> 
}

#include <wintab.h>
#include "wintablet.h"

#include "win32.h"

#define MAX_MAPPINGS 256

#define DID_MOUSE 1
#define DID_JOYSTICK 2
#define DID_KEYBOARD 3

#define DIDC_NONE 0
#define DIDC_DX 1
#define DIDC_RAW 2
#define DIDC_WIN 3
#define DIDC_CAT 4
#define DIDC_PARJOY 5
#define DIDC_XINPUT 6

#define AXISTYPE_NORMAL 0
#define AXISTYPE_POV_X 1
#define AXISTYPE_POV_Y 2
#define AXISTYPE_SLIDER 3
#define AXISTYPE_DIAL 4

#define MAX_ACQUIRE_ATTEMPTS 10

struct didata {
	int type;
	int acquired;
	int priority;
	int superdevice;
	GUID iguid;
	GUID pguid;
	TCHAR *name;
	bool fullname;
	TCHAR *sortname;
	TCHAR *configname;
	int vid, pid, mi;

	int connection;
	int acquireattempts;
	LPDIRECTINPUTDEVICE8 lpdi;
	HANDLE rawinput, rawhidhandle;
	HIDP_CAPS hidcaps;
	HIDP_VALUE_CAPS hidvcaps[MAX_MAPPINGS];
	PCHAR hidbuffer, hidbufferprev;
	PHIDP_PREPARSED_DATA hidpreparseddata;
	int maxusagelistlength;
	PUSAGE_AND_PAGE usagelist, prevusagelist;

	int wininput;
	int catweasel;
	int coop;
	int xinput;

	HANDLE parjoy;
	PAR_QUERY_INFORMATION oldparjoystatus;

	uae_s16 axles;
	uae_s16 buttons, buttons_real;
	uae_s16 axismappings[MAX_MAPPINGS];
	TCHAR *axisname[MAX_MAPPINGS];
	uae_s16 axissort[MAX_MAPPINGS];
	uae_s16 axistype[MAX_MAPPINGS];
	bool analogstick;

	uae_s16 buttonmappings[MAX_MAPPINGS];
	TCHAR *buttonname[MAX_MAPPINGS];
	uae_s16 buttonsort[MAX_MAPPINGS];
	uae_s16 buttonaxisparent[MAX_MAPPINGS];
	uae_s16 buttonaxisparentdir[MAX_MAPPINGS];
	uae_s16 buttonaxistype[MAX_MAPPINGS];

};

#define MAX_PARJOYPORTS 2

#define DI_BUFFER 30
#define DI_KBBUFFER 50

static LPDIRECTINPUT8 g_lpdi;

static struct didata di_mouse[MAX_INPUT_DEVICES];
static struct didata di_keyboard[MAX_INPUT_DEVICES];
static struct didata di_joystick[MAX_INPUT_DEVICES];
#define MAX_RAW_HANDLES 500
static int raw_handles;
static HANDLE rawinputhandles[MAX_RAW_HANDLES];
static int num_mouse, num_keyboard, num_joystick;
static int dd_inited, mouse_inited, keyboard_inited, joystick_inited;
static int stopoutput;
static HANDLE kbhandle = INVALID_HANDLE_VALUE;
static int originalleds, oldleds, newleds, disabledleds, ledstate;
static int normalmouse, supermouse, rawmouse, winmouse, winmousenumber, lightpen, lightpennumber, winmousewheelbuttonstart;
static int normalkb, superkb, rawkb;
static bool rawinput_enabled_mouse, rawinput_enabled_keyboard;
static bool rawinput_decided;
static bool rawhid_found;
static int rawinput_enabled_hid_reset;

static uae_s16 axisold[MAX_INPUT_DEVICES][256], buttonold[MAX_INPUT_DEVICES][256];

static int dinput_enum_all;

int dinput_winmouse (void)
{
	if (winmouse)
		return winmousenumber;
	return -1;
}
int dinput_wheelbuttonstart (void)
{
	return winmousewheelbuttonstart;
}
int dinput_lightpen (void)
{
	if (lightpen)
		return lightpennumber;
	return -1;
}

#if 0
static LRESULT CALLBACK LowLevelKeyboardProc (int nCode, WPARAM wParam, LPARAM lParam)
{
	write_log (_T("*"));
	if (nCode >= 0) {
		KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT*)lParam;
		int vk = k->vkCode;
		int sc = k->scanCode;
		write_log (_T("%02x %02x\n"), vk, sc);
	}
	return CallNextHookEx (NULL, nCode, wParam, lParam);
}

static HHOOK kbhook;
static void lock_kb (void)
{
	if (kbhook)
		return;
	kbhook = SetWindowsHookEx (WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);
	if (!kbhook)
		write_log (_T("SetWindowsHookEx %d\n"), GetLastError ());
	else
		write_log (_T("***************************\n"));
}
static void unlock_kb (void)
{
	if (!kbhook)
		return;
	write_log (_T("!!!!!!!!!!!!!!!!!!!!!!!!\n"));
	UnhookWindowsHookEx (kbhook);
	kbhook = NULL;
}
#endif

static BYTE ledkeystate[256];

static uae_u32 get_leds (void)
{
	uae_u32 led = 0;

	GetKeyboardState (ledkeystate);
	if (ledkeystate[VK_NUMLOCK] & 1)
		led |= KBLED_NUMLOCKM;
	if (ledkeystate[VK_CAPITAL] & 1)
		led |= KBLED_CAPSLOCKM;
	if (ledkeystate[VK_SCROLL] & 1)
		led |= KBLED_SCROLLLOCKM;

	if (currprefs.win32_kbledmode) {
		oldleds = led;
	} else if (!currprefs.win32_kbledmode && kbhandle != INVALID_HANDLE_VALUE) {
		KEYBOARD_INDICATOR_PARAMETERS InputBuffer;
		KEYBOARD_INDICATOR_PARAMETERS OutputBuffer;
		ULONG DataLength = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
		ULONG ReturnedLength;

		memset (&InputBuffer, 0, sizeof (InputBuffer));
		memset (&OutputBuffer, 0, sizeof (OutputBuffer));
		if (!DeviceIoControl (kbhandle, IOCTL_KEYBOARD_QUERY_INDICATORS,
			&InputBuffer, DataLength, &OutputBuffer, DataLength, &ReturnedLength, NULL))
			return 0;
		led = 0;
		if (OutputBuffer.LedFlags & KEYBOARD_NUM_LOCK_ON)
			led |= KBLED_NUMLOCKM;
		if (OutputBuffer.LedFlags & KEYBOARD_CAPS_LOCK_ON)
			led |= KBLED_CAPSLOCKM;
		if (OutputBuffer.LedFlags & KEYBOARD_SCROLL_LOCK_ON)
			led |= KBLED_SCROLLLOCKM;
	}
	return led;
}

static void kbevt (uae_u8 vk, uae_u8 sc)
{
	keybd_event (vk, 0, 0, 0);
	keybd_event (vk, 0, KEYEVENTF_KEYUP, 0);
}

static void set_leds (uae_u32 led)
{
	//write_log (_T("setleds %08x\n"), led);
	if (currprefs.win32_kbledmode) {
		if ((oldleds & KBLED_NUMLOCKM) != (led & KBLED_NUMLOCKM) && !(disabledleds & KBLED_NUMLOCKM)) {
			kbevt (VK_NUMLOCK, 0x45);
			oldleds ^= KBLED_NUMLOCKM;
		}
		if ((oldleds & KBLED_CAPSLOCKM) != (led & KBLED_CAPSLOCKM) && !(disabledleds & KBLED_CAPSLOCKM)) {
			kbevt (VK_CAPITAL, 0x3a);
			oldleds ^= KBLED_CAPSLOCKM;
		}
		if ((oldleds & KBLED_SCROLLLOCKM) != (led & KBLED_SCROLLLOCKM) && !(disabledleds & KBLED_SCROLLLOCKM)) {
			kbevt (VK_SCROLL, 0x46);
			oldleds ^= KBLED_SCROLLLOCKM;
		}
	} else if (kbhandle != INVALID_HANDLE_VALUE) {
		KEYBOARD_INDICATOR_PARAMETERS InputBuffer;
		ULONG DataLength = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
		ULONG ReturnedLength;

		memset (&InputBuffer, 0, sizeof (InputBuffer));
		oldleds = 0;
		if (led & KBLED_NUMLOCKM) {
			InputBuffer.LedFlags |= KEYBOARD_NUM_LOCK_ON;
			oldleds |= KBLED_NUMLOCKM;
		}
		if (led & KBLED_CAPSLOCKM) {
			InputBuffer.LedFlags |= KEYBOARD_CAPS_LOCK_ON;
			oldleds |= KBLED_CAPSLOCKM;
		}
		if (led & KBLED_SCROLLLOCKM) {
			InputBuffer.LedFlags |= KEYBOARD_SCROLL_LOCK_ON;
			oldleds |= KBLED_SCROLLLOCKM;
		}
		if (!DeviceIoControl (kbhandle, IOCTL_KEYBOARD_SET_INDICATORS,
			&InputBuffer, DataLength, NULL, 0, &ReturnedLength, NULL))
			write_log (_T("kbleds: DeviceIoControl() failed %d\n"), GetLastError());
	}
}

static void update_leds (void)
{
	if (!currprefs.keyboard_leds_in_use)
		return;
	if (newleds != oldleds)
		set_leds (newleds);
}

void indicator_leds (int num, int state)
{
	if (state == 0)
		ledstate &= ~(1 << num);
	else if (state > 0)
		ledstate |= 1 << num;

	disabledleds = 0;
	for (int i = 0; i < 3; i++) {
		if (state >= 0) {
			int l = currprefs.keyboard_leds[i];
			if (l <= 0) {
				disabledleds |= 1 << i;
			} else {
				newleds &= ~(1 << i);
				if (l - 1 > LED_CD) {
					// all floppies
					if (ledstate & ((1 << LED_DF0) | (1 << LED_DF1) | (1 << LED_DF2) | (1 << LED_DF3)))
						newleds |= 1 << i;
				} else {
					if ((1 << (l - 1)) & ledstate)
						newleds |= 1 << i;
				}
			}
		}
	}
}

static int isrealbutton (struct didata *did, int num)
{
	if (num >= did->buttons)
		return 0;
	if (did->buttonaxisparent[num] >= 0)
		return 0;
	return 1;
}

static void fixbuttons (struct didata *did)
{
	if (did->buttons > 0)
		return;
	write_log (_T("'%s' has no buttons, adding single default button\n"), did->name);
	did->buttonmappings[0] = DIJOFS_BUTTON (0);
	did->buttonsort[0] = 0;
	did->buttonname[0] = my_strdup (_T("Button"));
	did->buttons++;
}

static void addplusminus (struct didata *did, int i)
{
	TCHAR tmp[256];
	int j;

	if (did->buttons + 1 >= ID_BUTTON_TOTAL)
		return;
	for (j = 0; j < 2; j++) {
		_stprintf (tmp, _T("%s [%c]"), did->axisname[i], j ? '+' : '-');
		did->buttonname[did->buttons] = my_strdup (tmp);
		did->buttonmappings[did->buttons] = did->axismappings[i];
		did->buttonsort[did->buttons] = 1000 + (did->axismappings[i] + did->axistype[i]) * 2 + j;
		did->buttonaxisparent[did->buttons] = i;
		did->buttonaxisparentdir[did->buttons] = j;
		did->buttonaxistype[did->buttons] = did->axistype[i];
		did->buttons++;
	}
}

static void fixthings (struct didata *did)
{
	int i;

	did->buttons_real = did->buttons;
	for (i = 0; i < did->axles; i++)
		addplusminus (did, i);
}
static void fixthings_mouse (struct didata *did)
{
	int i;

	if (did == NULL)
		return;
	did->buttons_real = did->buttons;
	for (i = 0; i < did->axles; i++) {
		if (did->axissort[i] == -97)
			addplusminus (did, i);
	}
}

static int rawinput_available;
static bool rawinput_registered;

static int doregister_rawinput(void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	int num;
	RAWINPUTDEVICE rid[2 + 2 + MAX_INPUT_DEVICES] = { 0 };

	if (!rawinput_available)
		return 0;

	memset (rid, 0, sizeof rid);
	num = 0;
	/* mouse */
	rid[num].usUsagePage = 1;
	rid[num].usUsage = 2;
	if (mon->hMainWnd) {
		rid[num].dwFlags = RIDEV_INPUTSINK;
		rid[num].hwndTarget = mon->hMainWnd;
	}
	rid[num].dwFlags |= RIDEV_DEVNOTIFY;
	num++;

	/* keyboard */
	if (!rp_isactive()) {
		rid[num].usUsagePage = 1;
		rid[num].usUsage = 6;
		if (mon->hMainWnd) {
			rid[num].dwFlags = RIDEV_INPUTSINK;
			rid[num].hwndTarget = mon->hMainWnd;
		}
		rid[num].dwFlags |= RIDEV_NOHOTKEYS | RIDEV_DEVNOTIFY;
		num++;

		/* joystick */
		int off = num;

		// game pad
		rid[num].usUsagePage = 1;
		rid[num].usUsage = 4;
		if (mon->hMainWnd) {
			rid[num].dwFlags = RIDEV_INPUTSINK;
			rid[num].hwndTarget = mon->hMainWnd;
		}
		rid[num].dwFlags |= RIDEV_DEVNOTIFY;
		num++;

		// joystick
		rid[num].usUsagePage = 1;
		rid[num].usUsage = 5;
		if (mon->hMainWnd) {
			rid[num].dwFlags = RIDEV_INPUTSINK;
			rid[num].hwndTarget = mon->hMainWnd;
		}
		rid[num].dwFlags |= RIDEV_DEVNOTIFY;
		num++;
	}

#if 0
	for (int i = 0; i < num_joystick; i++) {
		struct didata *did = &di_joystick[i];
		if (did->connection != DIDC_RAW)
			continue;
		int j;
		for (j = off; j < num; j++) {
			if (rid[j].usUsagePage == 1 && (rid[j].usUsage == 4 || rid[j].usUsage == 5))
				break;
			if (rid[j].usUsage == did->hidcaps.Usage && rid[j].usUsagePage == did->hidcaps.UsagePage)
				break;
		}
		if (j == num) {
			rid[num].usUsagePage = did->hidcaps.UsagePage;
			rid[num].usUsage = did->hidcaps.Usage;
			if (!add) {
				rid[num].dwFlags = RIDEV_REMOVE;
			} else {
				if (hMainWnd) {
					rid[num].dwFlags = RIDEV_INPUTSINK;
					rid[num].hwndTarget = hMainWnd;
				}
				rid[num].dwFlags |= (os_vista ? RIDEV_DEVNOTIFY : 0);
			}
#if RAWINPUT_DEBUG
			write_log(_T("RAWHID ADD=%d NUM=%d %04x/%04x\n"), add, num, did->hidcaps.Usage, did->hidcaps.UsagePage);
#endif
			num++;
		}
	}
#endif

#if RAWINPUT_DEBUG
	write_log (_T("RegisterRawInputDevices: NUM=%d HWND=%p\n"), num, hMainWnd);
#endif

	if (RegisterRawInputDevices (rid, num, sizeof(RAWINPUTDEVICE)) == FALSE) {
		write_log (_T("RAWINPUT %sregistration failed %d\n"), GetLastError());
		return 0;
	}

	rawinput_registered = 1;
	return 1;
}

void rawinput_alloc(void)
{
	if (!rawinput_registered) {
		doregister_rawinput();;
		//write_log("RAWINPUT ALLOC\n");
	}
}
void rawinput_release(void)
{
	if (rp_isactive()) {
		return;
	}

	UINT num = 0;
	int cnt = 0;
	int v = GetRegisteredRawInputDevices(NULL, &num, sizeof(RAWINPUTDEVICE));
	if ((v >= 0 || (v == -1 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) && num > 0) {
		PRAWINPUTDEVICE devs = xcalloc(RAWINPUTDEVICE, num);
		if (devs) {
			int v = GetRegisteredRawInputDevices(devs, &num, sizeof(RAWINPUTDEVICE));
			if (v >= 0) {
				for (int i = 0; i < num; i++) {
					PRAWINPUTDEVICE dev = devs + i;
					dev->dwFlags = RIDEV_REMOVE;
					dev->hwndTarget = NULL;
					cnt++;

				}
				if (!RegisterRawInputDevices(devs, num, sizeof(RAWINPUTDEVICE))) {
					write_log("RegisterRawInputDevices RIDEV_REMOVE error %08x\n", GetLastError());
				}
			}
			xfree(devs);
		}
	}
	rawinput_registered = false;
	//write_log("RAWINPUT FREE %d\n", cnt);
}

static void cleardid (struct didata *did)
{
	memset (did, 0, sizeof (*did));
	for (int i = 0; i < MAX_MAPPINGS; i++) {
		did->axismappings[i] = -1;
		did->buttonmappings[i] = -1;
		did->buttonaxisparent[i] = -1;
	}
	did->parjoy = INVALID_HANDLE_VALUE;
	did->rawhidhandle = INVALID_HANDLE_VALUE;
}


#define	MAX_KEYCODES 256
static uae_u8 di_keycodes[MAX_INPUT_DEVICES][MAX_KEYCODES];
static int keyboard_german;

static int keyhack (int scancode, int pressed, int num)
{
	static byte backslashstate, apostrophstate;

	//check ALT-F4
	if (pressed && !(di_keycodes[num][DIK_F4] & 1) && scancode == DIK_F4) {
		if ((di_keycodes[num][DIK_LALT] & 1) && !currprefs.win32_ctrl_F11_is_quit) {
#ifdef RETROPLATFORM
			if (rp_close ())
				return -1;
#endif
			if (!quit_ok())
				return -1;
			uae_quit ();
			return -1;
		}
	}
#ifdef SINGLEFILE
	if (pressed && scancode == DIK_ESCAPE) {
		uae_quit ();
		return -1;
	}
#endif

	// release mouse if TAB and ALT is pressed
	if (pressed && (di_keycodes[num][DIK_LALT] & 1) && scancode == DIK_TAB) {
		disablecapture ();
		return -1;
	}

#if 0
	if (!keyboard_german)
		return scancode;

	//This code look so ugly because there is no Directinput
	//key for # (called numbersign on win standard message)
	//available
	//so here need to change qulifier state and restore it when key
	//is release
	if (scancode == DIK_BACKSLASH) // The # key
	{
		if ((di_keycodes[num][DIK_LSHIFT] & 1) || (di_keycodes[num][DIK_RSHIFT] & 1) || apostrophstate)
		{
			if (pressed)
			{
				apostrophstate=1;
				inputdevice_translatekeycode (num, DIK_RSHIFT, 0, false);
				inputdevice_translatekeycode (num, DIK_LSHIFT, 0, false);
				return 13;           // the german ' key
			}
			else
			{
				//best is add a real keystatecheck here but it still work so
				apostrophstate = 0;
				inputdevice_translatekeycode (num, DIK_LALT, 0, true);
				inputdevice_translatekeycode (num, DIK_LSHIFT, 0, true);
				inputdevice_translatekeycode (num, 4, 0, true);  // release also the # key
				return 13;
			}

		}
		if (pressed)
		{
			inputdevice_translatekeycode (num, DIK_LALT, 1, false);
			inputdevice_translatekeycode (num, DIK_LSHIFT, 1, false);
			return 4;           // the german # key
		}
		else
		{
			inputdevice_translatekeycode (num, DIK_LALT, 0, true);
			inputdevice_translatekeycode (num, DIK_LSHIFT, 0, true);
			// Here is the same not nice but do the job
			return 4;           // the german # key

		}
	}
	if (((di_keycodes[num][DIK_RALT] & 1)) || (backslashstate)) {
		switch (scancode)
		{
		case 12:
			if (pressed)
			{
				backslashstate=1;
				inputdevice_translatekeycode (num, DIK_RALT, 0, true);
				return DIK_BACKSLASH;
			}
			else
			{
				backslashstate=0;
				return DIK_BACKSLASH;
			}
		}
	}
#endif
	return scancode;
}

static HMODULE wintab;
typedef UINT(API* WTINFOW)(UINT, UINT, LPVOID);
static WTINFOW pWTInfoW;
typedef BOOL(API* WTCLOSE)(HCTX);
static WTCLOSE pWTClose;
typedef HCTX(API* WTOPENW)(HWND, LPLOGCONTEXTW, BOOL);
static WTOPENW pWTOpenW;
typedef BOOL(API* WTPACKET)(HCTX, UINT, LPVOID);
WTPACKET pWTPacket;

static int tablet;
static int axmax, aymax, azmax;
static int xmax, ymax, zmax;
static int xres, yres;
static int maxpres;
static TCHAR *tabletname;
static int tablet_x, tablet_y, tablet_z, tablet_pressure, tablet_buttons, tablet_proximity;
static int tablet_ax, tablet_ay, tablet_az, tablet_flags;
static int tablet_div;

static void tablet_send (void)
{
	static int eraser;

	if ((tablet_flags & TPS_INVERT) && tablet_pressure > 0) {
		tablet_buttons |= 2;
		eraser = 1;
	} else if (eraser) {
		tablet_buttons &= ~2;
		eraser = 0;
	}
	if (tablet_x < 0)
		return;
	inputdevice_tablet (tablet_x, tablet_y, tablet_z, tablet_pressure, tablet_buttons, tablet_proximity,
		tablet_ax, tablet_ay, tablet_az, dinput_lightpen());
	tabletlib_tablet (tablet_x, tablet_y, tablet_z, tablet_pressure, maxpres, tablet_buttons, tablet_proximity,
		tablet_ax, tablet_ay, tablet_az);
}

void send_tablet_proximity (int inproxi)
{
	if (tablet_proximity == inproxi)
		return;
	tablet_proximity = inproxi;
	if (!tablet_proximity) {
		tablet_flags &= ~TPS_INVERT;
	}
	if (tablet_log & 4)
		write_log (_T("TABLET: Proximity=%d\n"), inproxi);
	tablet_send ();
}

void send_tablet (int x, int y, int z, int pres, uae_u32 buttons, int flags, int ax, int ay, int az, int rx, int ry, int rz, RECT *r)
{
	if (tablet_log & 4)
		write_log (_T("TABLET: B=%08X F=%08X X=%d Y=%d P=%d (%d,%d,%d)\n"), buttons, flags, x, y, pres, ax, ay, az);
	if (axmax > 0)
		ax = ax * 255 / axmax;
	else
		ax = 0;
	if (aymax > 0)
		ay = ay * 255 / aymax;
	else
		ay = 0;
	if (azmax > 0)
		az = az * 255 / azmax;
	else
		az = 0;
	pres = pres * 255 / maxpres;

	tablet_x = (x + tablet_div / 2) / tablet_div;
	tablet_y = ymax - (y + tablet_div / 2) / tablet_div;
	tablet_z = z;
	tablet_pressure = pres;
	tablet_buttons = buttons;
	tablet_ax = abs (ax);
	tablet_ay = abs (ay);
	tablet_az = abs (az);
	tablet_flags = flags;

	tablet_send ();
}

static int gettabletres (AXIS *a)
{
	FIX32 r = a->axResolution;
	switch (a->axUnits)
	{
	case TU_INCHES:
		return r >> 16;
	case TU_CENTIMETERS:
		return (int)(((r / 65536.0) / 2.54) + 0.5);
	default:
		return -1;
	}
}

void *open_tablet (HWND hwnd)
{
	static int initialized;
	LOGCONTEXT lc = { 0 };
	AXIS tx = { 0 }, ty = { 0 }, tz = { 0 };
	AXIS pres = { 0 };
	int xm, ym, zm;

	if (!tablet)
		return 0;
	if (inputdevice_is_tablet () <= 0 && !is_touch_lightpen())
		return 0;
	xmax = -1;
	ymax = -1;
	zmax = -1;
	pWTInfoW (WTI_DEFCONTEXT, 0, &lc);
	pWTInfoW (WTI_DEVICES, DVC_X, &tx);
	pWTInfoW (WTI_DEVICES, DVC_Y, &ty);
	pWTInfoW (WTI_DEVICES, DVC_NPRESSURE, &pres);
	pWTInfoW (WTI_DEVICES, DVC_XMARGIN, &xm);
	pWTInfoW (WTI_DEVICES, DVC_YMARGIN, &ym);
	pWTInfoW (WTI_DEVICES, DVC_ZMARGIN, &zm);
	xmax = tx.axMax;
	ymax = ty.axMax;
	if (pWTInfoW (WTI_DEVICES, DVC_Z, &tz))
		zmax = tz.axMax;
	lc.lcOptions |= CXO_MESSAGES;
	lc.lcPktData = PACKETDATA;
	lc.lcPktMode = PACKETMODE;
	lc.lcMoveMask = PACKETDATA;
	lc.lcBtnUpMask = lc.lcBtnDnMask;
	lc.lcInExtX = tx.axMax;
	lc.lcInExtY = ty.axMax;
	if (zmax > 0)
		lc.lcInExtZ = tz.axMax;
	if (!initialized) {
		write_log (_T("Tablet '%s' parameters\n"), tabletname);
		write_log (_T("Xmax=%d,Ymax=%d,Zmax=%d\n"), xmax, ymax, zmax);
		write_log (_T("Xres=%.1f:%d,Yres=%.1f:%d,Zres=%.1f:%d\n"),
			tx.axResolution / 65536.0, tx.axUnits, ty.axResolution / 65536.0, ty.axUnits, tz.axResolution / 65536.0, tz.axUnits);
		write_log (_T("Xrotmax=%d,Yrotmax=%d,Zrotmax=%d\n"), axmax, aymax, azmax);
		write_log (_T("PressureMin=%d,PressureMax=%d\n"), pres.axMin, pres.axMax);
	}
	maxpres = pres.axMax;
	xres = gettabletres (&tx);
	yres = gettabletres (&ty);
	tablet_div = 1;
	while (xmax / tablet_div > 4095 || ymax / tablet_div > 4095) {
		tablet_div *= 2;
	}
	xmax /= tablet_div;
	ymax /= tablet_div;
	xres /= tablet_div;
	yres /= tablet_div;

	if (tablet_div > 1)
		write_log (_T("Divisor: %d (%d,%d)\n"), tablet_div, xmax, ymax);
	tablet_proximity = -1;
	tablet_x = -1;
	inputdevice_tablet_info (xmax, ymax, zmax, axmax, aymax, azmax, xres, yres);
	tabletlib_tablet_info (xmax, ymax, zmax, axmax, aymax, azmax, xres, yres);
	initialized = 1;
	return pWTOpenW (hwnd, &lc, TRUE);
}

int close_tablet (void *ctx)
{
	if (!wintab)
		return 0;
	if (ctx != NULL)
		pWTClose ((HCTX)ctx);
	ctx = NULL;
	if (!tablet)
		return 0;
	return 1;
}

int is_touch_lightpen(void)
{
	return dinput_lightpen() >= 0;
}

int is_tablet (void)
{
	return (tablet || os_touch) ? 1 : 0;
}

static int initialize_tablet (void)
{
	TCHAR name[MAX_DPATH];
	struct tagAXIS ori[3];
	int tilt = 0;

	wintab = WIN32_LoadLibrary(_T("wintab32.dll"));
	if (wintab == NULL) {
		write_log(_T("Tablet: no wintab32.dll\n"));
		return 0;
	}

	pWTOpenW = (WTOPENW)GetProcAddress(wintab, "WTOpenW");
	pWTClose = (WTCLOSE)GetProcAddress(wintab, "WTClose");
	pWTInfoW = (WTINFOW)GetProcAddress(wintab, "WTInfoW");
	pWTPacket = (WTPACKET)GetProcAddress(wintab, "WTPacket");

	if (!pWTOpenW || !pWTClose || !pWTInfoW || !pWTPacket) {
		write_log(_T("Tablet: wintab32.dll has missing functions!\n"));
		FreeModule(wintab);
		wintab = NULL;
		return 0;
	}

	if (!pWTInfoW(0, 0, NULL)) {
		write_log(_T("Tablet: WTInfo() returned failure\n"));
		FreeModule(wintab);
		wintab = NULL;
		return 0;
	}
	name[0] = 0;
	if (!pWTInfoW (WTI_DEVICES, DVC_NAME, name)) {
		write_log(_T("Tablet: WTInfo(DVC_NAME) returned failure\n"));
		FreeModule(wintab);
		wintab = NULL;
		return 0;
	}
	if (name[0] == 0) {
		write_log(_T("Tablet: WTInfo(DVC_NAME) returned NULL name\n"));
		FreeModule(wintab);
		wintab = NULL;
		return 0;
	}
	axmax = aymax = azmax = -1;
	tilt = pWTInfoW (WTI_DEVICES, DVC_ORIENTATION, ori);
	if (tilt) {
		if (ori[0].axMax > 0)
			axmax = ori[0].axMax;
		if (ori[1].axMax > 0)
			aymax = ori[1].axMax;
		if (ori[2].axMax > 0)
			azmax = ori[2].axMax;
	}
	write_log (_T("Tablet '%s' detected\n"), name);
	tabletname = my_strdup (name);
	tablet = TRUE;
	return 1;
}

#if 0
static int initialize_parjoyport (void)
{
	for (int i = 0; i < MAX_PARJOYPORTS && num_joystick < MAX_INPUT_DEVICES; i++) {
		struct didata *did;
		TCHAR *p = i ? currprefs.win32_parjoyport1 : currprefs.win32_parjoyport0;
		if (p[0] == 0)
			continue;
		HANDLE ph = CreateFile (p, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (ph == INVALID_HANDLE_VALUE) {
			write_log (_T("PARJOY: '%s' failed to open: %u\n"), p, GetLastError ());
			continue;
		}
		write_log (_T("PARJOY: '%s' open\n"), p);
		for (int j = 0; j < 2; j++) {
			TCHAR tmp[100];
			did = di_joystick;
			did += num_joystick;
			cleardid(did);
			did->connection = DIDC_PARJOY;
			did->parjoy = ph;
			_stprintf (tmp, _T("Parallel joystick %d.%d"), i + 1, j + 1);
			did->name = my_strdup (tmp);
			did->sortname = my_strdup (tmp);
			_stprintf (tmp, _T("PARJOY%d.%d"), i, j);
			did->configname = my_strdup (tmp);
			did->buttons = did->buttons_real = 1;
			did->axles = 2;
			did->axissort[0] = 0;
			did->axisname[0] = my_strdup (_T("X Axis"));
			did->axissort[1] = 1;
			did->axisname[1] = my_strdup (_T("Y Axis"));
			for (j = 0; j < did->buttons; j++) {
				did->buttonsort[j] = j;
				_stprintf (tmp, _T("Button %d"), j + 1);
				did->buttonname[j] = my_strdup (tmp);
			}
			did->priority = -1;
			fixbuttons (did);
			fixthings (did);
			num_joystick++;
		}
	}
	return 0;
}
#endif

static int initialize_catweasel (void)
{
	int j, i;
	TCHAR tmp[MAX_DPATH];
	struct didata *did;

	if (catweasel_ismouse ()) {
		for (i = 0; i < 2 && num_mouse < MAX_INPUT_DEVICES; i++) {
			did = di_mouse;
			did += num_mouse;
			cleardid(did);
			did->connection = DIDC_CAT;
			did->catweasel = i;
			_stprintf (tmp, _T("Catweasel mouse"));
			did->name = my_strdup (tmp);
			did->sortname = my_strdup (tmp);
			_stprintf (tmp, _T("CWMOUSE%d"), i);
			did->configname = my_strdup (tmp);
			did->buttons = did->buttons_real = 3;
			did->axles = 2;
			did->axissort[0] = 0;
			did->axisname[0] = my_strdup (_T("X Axis"));
			did->axissort[1] = 1;
			did->axisname[1] = my_strdup (_T("Y Axis"));
			for (j = 0; j < did->buttons; j++) {
				did->buttonsort[j] = j;
				_stprintf (tmp, _T("Button %d"), j + 1);
				did->buttonname[j] = my_strdup (tmp);
			}
			did->priority = -1;
			num_mouse++;
		}
	}
	if (catweasel_isjoystick ()) {
		for (i = 0; i < 2 && num_joystick < MAX_INPUT_DEVICES; i++) {
			did = di_joystick;
			did += num_joystick;
			cleardid(did);
			did->connection = DIDC_CAT;
			did->catweasel = i;
			_stprintf (tmp, _T("Catweasel joystick"));
			did->name = my_strdup (tmp);
			did->sortname = my_strdup (tmp);
			_stprintf (tmp, _T("CWJOY%d"), i);
			did->configname = my_strdup (tmp);
			did->buttons = did->buttons_real = (catweasel_isjoystick () & 0x80) ? 3 : 1;
			did->axles = 2;
			did->axissort[0] = 0;
			did->axisname[0] = my_strdup (_T("X Axis"));
			did->axissort[1] = 1;
			did->axisname[1] = my_strdup (_T("Y Axis"));
			for (j = 0; j < did->buttons; j++) {
				did->buttonsort[j] = j;
				_stprintf (tmp, _T("Button %d"), j + 1);
				did->buttonname[j] = my_strdup (tmp);
			}
			did->priority = -1;
			fixbuttons (did);
			fixthings (did);
			num_joystick++;
		}
	}
	return 1;
}


static void sortobjects (struct didata *did)
{
	int i, j;
	uae_s16 tmpi;
	TCHAR *tmpc;

	for (i = 0; i < did->axles; i++) {
		for (j = i + 1; j < did->axles; j++) {
			if (did->axissort[i] > did->axissort[j]) {
				HIDP_VALUE_CAPS tmpvcaps;
				tmpi = did->axismappings[i]; did->axismappings[i] = did->axismappings[j]; did->axismappings[j] = tmpi;
				tmpi = did->axissort[i]; did->axissort[i] = did->axissort[j]; did->axissort[j] = tmpi;
				tmpi = did->axistype[i]; did->axistype[i] = did->axistype[j]; did->axistype[j] = tmpi;
				memcpy (&tmpvcaps, &did->hidvcaps[i], sizeof tmpvcaps);
				memcpy (&did->hidvcaps[i], &did->hidvcaps[j], sizeof tmpvcaps);
				memcpy (&did->hidvcaps[j], &tmpvcaps, sizeof tmpvcaps);
				tmpc = did->axisname[i]; did->axisname[i] = did->axisname[j]; did->axisname[j] = tmpc;
			}
		}
	}
	for (i = 0; i < did->buttons; i++) {
		for (j = i + 1; j < did->buttons; j++) {
			if (did->buttonsort[i] > did->buttonsort[j]) {
				tmpi = did->buttonmappings[i]; did->buttonmappings[i] = did->buttonmappings[j]; did->buttonmappings[j] = tmpi;
				tmpi = did->buttonsort[i]; did->buttonsort[i] = did->buttonsort[j]; did->buttonsort[j] = tmpi;
				tmpc = did->buttonname[i]; did->buttonname[i] = did->buttonname[j]; did->buttonname[j] = tmpc;
				tmpi = did->buttonaxisparent[i]; did->buttonaxisparent[i] = did->buttonaxisparent[j]; did->buttonaxisparent[j] = tmpi;
				tmpi = did->buttonaxisparentdir[i]; did->buttonaxisparentdir[i] = did->buttonaxisparentdir[j]; did->buttonaxisparentdir[j] = tmpi;
				tmpi = did->buttonaxistype[i]; did->buttonaxistype[i] = did->buttonaxistype[j]; did->buttonaxistype[j] = tmpi;
			}
		}
	}

#if DI_DEBUG
	if (did->axles + did->buttons > 0) {
		write_log (_T("%s: [%04X/%04X]\n"), did->name, did->vid, did->pid);
		if (did->connection == DIDC_DX)
			write_log (_T("PGUID=%s\n"), outGUID (&did->pguid));
		for (i = 0; i < did->axles; i++) {
			HIDP_VALUE_CAPS *caps = &did->hidvcaps[i];
			write_log (_T("%02X %03d '%s' (%d, [%d - %d, %d - %d, %d %d %d])\n"),
				did->axismappings[i], did->axismappings[i], did->axisname[i], did->axissort[i],
				caps->LogicalMin, caps->LogicalMax, caps->PhysicalMin, caps->PhysicalMax,
				caps->BitSize, caps->Units, caps->UnitsExp);
		}
		for (i = 0; i < did->buttons; i++) {
			write_log (_T("%02X %03d '%s' (%d)\n"),
				did->buttonmappings[i], did->buttonmappings[i], did->buttonname[i], did->buttonsort[i]);
		}
	}
#endif
}

#define RDP_DEVICE1 _T("\\??\\Root#")
#define RDP_DEVICE2 _T("\\\\?\\Root#")

static int rdpdevice(const TCHAR *buf)
{
	if (!_tcsncmp (RDP_DEVICE1, buf, _tcslen (RDP_DEVICE1)))
		return 1;
	if (!_tcsncmp (RDP_DEVICE2, buf, _tcslen (RDP_DEVICE2)))
		return 1;
	return 0;
}

static void rawinputfixname (const TCHAR *name, const TCHAR *friendlyname)
{
	int i, ii, j;
	TCHAR tmp[MAX_DPATH];

	if (!name[0] || !friendlyname[0])
		return;

	while (*name) {
		if (*name != '\\' && *name != '?')
			break;
		name++;
	}
	_tcscpy (tmp, name);
	for (i = 0; i < _tcslen (tmp); i++) {
		if (tmp[i] == '\\')
			tmp[i] = '#';
		tmp[i] = _totupper (tmp[i]);
	}
	for (ii = 0; ii < 3; ii++) {
		int cnt;
		struct didata *did;
		if (ii == 0) {
			cnt = num_mouse;
			did = di_mouse;
		} else if (ii == 1) {
			cnt = num_keyboard;
			did = di_keyboard;
		} else {
			cnt = num_joystick;
			did = di_joystick;
		}
		for (i = 0; i < cnt; i++, did++) {
			TCHAR tmp2[MAX_DPATH], *p2;
			if (!did->rawinput || did->fullname)
				continue;
			for (j = 0; j < _tcslen (did->configname); j++)
				tmp2[j] = _totupper (did->configname[j]);
			tmp2[j] = 0;
			p2 = tmp2;
			while (*p2) {
				if (*p2 != '\\' && *p2 != '?')
					break;
				p2++;
			}
			if (_tcslen (p2) >= _tcslen (tmp) && !_tcsncmp (p2, tmp, _tcslen (tmp))) {
				write_log(_T("[%04X/%04X] '%s' (%s) -> '%s'\n"), did->vid, did->pid, did->configname, did->name, friendlyname);
				xfree (did->name);
//				if (did->vid > 0 && did->pid > 0)
//					_stprintf (tmp, _T("%s [%04X/%04X]"), friendlyname, did->vid, did->pid);
//				else
				_stprintf (tmp, _T("%s"), friendlyname);
				did->name = my_strdup (tmp);
			}
		}
	}
}

#define FGUIDS 4
static void rawinputfriendlynames (void)
{
	HDEVINFO di;
	int i, ii;
	GUID guid[FGUIDS];

	HidD_GetHidGuid (&guid[0]);
	guid[1] = GUID_DEVCLASS_HIDCLASS;
	guid[2] = GUID_DEVCLASS_MOUSE;
	guid[3] = GUID_DEVCLASS_KEYBOARD;

	for (ii = 0; ii < FGUIDS; ii++) {
		di = SetupDiGetClassDevs (&guid[ii], NULL, NULL, 0);
		if (di != INVALID_HANDLE_VALUE) {
			SP_DEVINFO_DATA dd;
			dd.cbSize = sizeof dd;
			for (i = 0; SetupDiEnumDeviceInfo (di, i, &dd); i++) {
				TCHAR devpath[MAX_DPATH];
				DWORD size = 0;
				if (SetupDiGetDeviceInstanceId (di, &dd, devpath, sizeof devpath / sizeof(TCHAR), &size)) {
					DEVINST devinst = dd.DevInst;

					TCHAR *cg = outGUID (&guid[ii]);
					for (;;) {
						TCHAR devname[MAX_DPATH];
						ULONG size2;
						TCHAR bufguid[MAX_DPATH];
	
						size2 = sizeof bufguid / sizeof(TCHAR);
						if (CM_Get_DevNode_Registry_Property (devinst, CM_DRP_CLASSGUID, NULL, bufguid, &size2, 0) != CR_SUCCESS)
							break;
						if (_tcsicmp (cg, bufguid))
							break;

						size2 = sizeof devname / sizeof(TCHAR);
						if (CM_Get_DevNode_Registry_Property (devinst, CM_DRP_FRIENDLYNAME, NULL, devname, &size2, 0) != CR_SUCCESS) {
							ULONG size2 = sizeof devname / sizeof(TCHAR);
							if (CM_Get_DevNode_Registry_Property (devinst, CM_DRP_DEVICEDESC, NULL, devname, &size2, 0) != CR_SUCCESS)
								devname[0] = 0;
						}

						rawinputfixname (devpath, devname);

						DEVINST parent = devinst;
						if (CM_Get_Parent (&devinst, parent, 0) != CR_SUCCESS)
							break;

					}
				}
			}
			SetupDiDestroyDeviceInfoList (di);
		}
	}
}

static const TCHAR *rawkeyboardlabels[256] =
{
	_T("ESCAPE"),
	_T("1"),_T("2"),_T("3"),_T("4"),_T("5"),_T("6"),_T("7"),_T("8"),_T("9"),_T("0"),
	_T("MINUS"),_T("EQUALS"),_T("BACK"),_T("TAB"),
	_T("Q"),_T("W"),_T("E"),_T("R"),_T("T"),_T("Y"),_T("U"),_T("I"),_T("O"),_T("P"),
	_T("LBRACKET"),_T("RBRACKET"),_T("RETURN"),_T("LCONTROL"),
	_T("A"),_T("S"),_T("D"),_T("F"),_T("G"),_T("H"),_T("J"),_T("K"),_T("L"),
	_T("SEMICOLON"),_T("APOSTROPHE"),_T("GRAVE"),_T("LSHIFT"),_T("BACKSLASH"),
	_T("Z"),_T("X"),_T("C"),_T("V"),_T("B"),_T("N"),_T("M"),
	_T("COMMA"),_T("PERIOD"),_T("SLASH"),_T("RSHIFT"),_T("MULTIPLY"),_T("LMENU"),_T("SPACE"),_T("CAPITAL"),
	_T("F1"),_T("F2"),_T("F3"),_T("F4"),_T("F5"),_T("F6"),_T("F7"),_T("F8"),_T("F9"),_T("F10"),
	_T("NUMLOCK"),_T("SCROLL"),_T("NUMPAD7"),_T("NUMPAD8"),_T("NUMPAD9"),_T("SUBTRACT"),
	_T("NUMPAD4"),_T("NUMPAD5"),_T("NUMPAD6"),_T("ADD"),_T("NUMPAD1"),_T("NUMPAD2"),_T("NUMPAD3"),_T("NUMPAD0"),
	_T("DECIMAL"),NULL,NULL,_T("OEM_102"),_T("F11"),_T("F12"),
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	_T("F13"),_T("F14"),_T("F15"),NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	_T("NUMPADEQUALS"),NULL,NULL,
	_T("PREVTRACK"),NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	_T("NEXTTRACK"),NULL,NULL,_T("NUMPADENTER"),_T("RCONTROL"),NULL,NULL,
	_T("MUTE"),_T("CALCULATOR"),_T("PLAYPAUSE"),NULL,_T("MEDIASTOP"),
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	_T("VOLUMEDOWN"),NULL,_T("VOLUMEUP"),NULL,_T("WEBHOME"),_T("NUMPADCOMMA"),NULL,
	_T("DIVIDE"),NULL,_T("SYSRQ"),_T("RMENU"),
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	_T("PAUSE"),NULL,_T("HOME"),_T("UP"),_T("PREV"),NULL,_T("LEFT"),NULL,_T("RIGHT"),NULL,_T("END"),
	_T("DOWN"),_T("NEXT"),_T("INSERT"),_T("DELETE"),
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	_T("LWIN"),_T("RWIN"),_T("APPS"),_T("POWER"),_T("SLEEP"),
	NULL,NULL,NULL,
	_T("WAKE"),NULL,_T("WEBSEARCH"),_T("WEBFAVORITES"),_T("WEBREFRESH"),_T("WEBSTOP"),
	_T("WEBFORWARD"),_T("WEBBACK"),_T("MYCOMPUTER"),_T("MAIL"),_T("MEDIASELECT"),
	_T("")
};

static void getvidpid2 (const TCHAR *devname, int *id, const TCHAR *str)
{
	TCHAR *dv = my_strdup (devname);
	for (int i = 0; i < _tcslen (dv); i++)
		dv[i] = _totupper (dv[i]);
	TCHAR *s = _tcsstr (dv, str);
	if (s) {
		int val = -1;
		_stscanf (s + _tcslen (str), _T("%X"), &val);
		*id = val;
	}
	xfree (dv);
}

static void getvidpid (const TCHAR *devname, int *vid, int *pid, int *mi)
{
	*vid = *pid = *mi = -1;
	getvidpid2 (devname, vid, _T("VID_"));
	getvidpid2 (devname, pid, _T("PID_"));
	getvidpid2 (devname, mi, _T("MI_"));
}

static void addrkblabels (struct didata *did)
{
	for (int k = 0; k < 254; k++) {
		TCHAR tmp[100];
		tmp[0] = 0;
		if (rawkeyboardlabels[k] != NULL && rawkeyboardlabels[k][0])
			_tcscpy (tmp, rawkeyboardlabels[k]);
		if (!tmp[0])
			_stprintf (tmp, _T("KEY_%02X"), k + 1);
		did->buttonname[k] = my_strdup (tmp);
		did->buttonmappings[k] = k + 1;
		did->buttonsort[k] = k + 1;
		did->buttons++;
	}
}

struct hiddesc
{
	int priority;
	int page, usage;
	TCHAR *name;
	int type;
};
static const struct hiddesc hidtable[] =
{
	{ 0x30, 1, 0x30, _T("X Axis"), 0 },
	{ 0x31, 1, 0x31, _T("Y Axis"), 0 },
	{ 0x32, 1, 0x32, _T("Z Axis"), 0 },
	{ 0x33, 1, 0x33, _T("X Rotation"), 0 },
	{ 0x34, 1, 0x34, _T("Y Rotation"), 0 },
	{ 0x35, 1, 0x35, _T("Z Rotation"), 0 },
	{ 0x36, 1, 0x36, _T("Slider"), AXISTYPE_SLIDER },
	{ 0x37, 1, 0x37, _T("Dial"), AXISTYPE_DIAL },
	{ 0x38, 1, 0x38, _T("Wheel"), 0 },
	{ 0x39, 1, 0x39, _T("Hat Switch"), AXISTYPE_POV_X },
	{ 0x90, 1, 0x90, _T("D-pad Up"), 0 },
	{ 0x91, 1, 0x91, _T("D-pad Down"), 0 },
	{ 0x92, 1, 0x92, _T("D-pad Right"), 0 },
	{ 0x93, 1, 0x93, _T("D-pad Left"), 0 },
	{ 0xbb, 2, 0xbb, _T("Throttle"), AXISTYPE_SLIDER },
	{ 0xba, 2, 0xba, _T("Rudder"), 0 },
	{ 0 }
};

static uae_u32 hidmask (int bits)
{
	 return bits >= 32 ? 0xffffffff : (1 << bits) - 1;
}

static int extractbits (uae_u32 val, int bits, bool issigned)
{
	if (issigned)
		return (val & (bits >= 32 ? 0x80000000 : (1 << (bits - 1)))) ? val | (bits >= 32 ? 0x80000000 : (-1 << bits)) : val;
	else
		return val & hidmask (bits);
}

struct hidquirk
{
	uae_u16 vid, pid;
};

#define USB_VENDOR_ID_LOGITECH		0x046d
#define USB_DEVICE_ID_LOGITECH_G13	0xc2ab
#define USB_VENDOR_ID_AASHIMA		0x06d6
#define USB_DEVICE_ID_AASHIMA_GAMEPAD	0x0025
#define USB_DEVICE_ID_AASHIMA_PREDATOR	0x0026
#define USB_VENDOR_ID_ALPS		0x0433
#define USB_DEVICE_ID_IBM_GAMEPAD	0x1101
#define USB_VENDOR_ID_CHIC		0x05fe
#define USB_DEVICE_ID_CHIC_GAMEPAD	0x0014
#define USB_VENDOR_ID_DWAV		0x0eef
#define USB_DEVICE_ID_EGALAX_TOUCHCONTROLLER	0x0001
#define USB_VENDOR_ID_MOJO		0x8282
#define USB_DEVICE_ID_RETRO_ADAPTER	0x3201
#define USB_VENDOR_ID_HAPP		0x078b
#define USB_DEVICE_ID_UGCI_DRIVING	0x0010
#define USB_DEVICE_ID_UGCI_FLYING	0x0020
#define USB_DEVICE_ID_UGCI_FIGHTING	0x0030
#define USB_VENDOR_ID_NATSU		0x08b7
#define USB_DEVICE_ID_NATSU_GAMEPAD	0x0001
#define USB_VENDOR_ID_NEC		0x073e
#define USB_DEVICE_ID_NEC_USB_GAME_PAD	0x0301
#define USB_VENDOR_ID_NEXTWINDOW	0x1926
#define USB_DEVICE_ID_NEXTWINDOW_TOUCHSCREEN	0x0003
#define USB_VENDOR_ID_SAITEK		0x06a3
#define USB_DEVICE_ID_SAITEK_RUMBLEPAD	0xff17
#define USB_VENDOR_ID_TOPMAX		0x0663
#define USB_DEVICE_ID_TOPMAX_COBRAPAD	0x0103

static const struct hidquirk quirks[] =  {
	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_G13 },
	{ USB_VENDOR_ID_AASHIMA, USB_DEVICE_ID_AASHIMA_GAMEPAD },
	{ USB_VENDOR_ID_AASHIMA, USB_DEVICE_ID_AASHIMA_PREDATOR },
	{ USB_VENDOR_ID_ALPS, USB_DEVICE_ID_IBM_GAMEPAD },
	{ USB_VENDOR_ID_CHIC, USB_DEVICE_ID_CHIC_GAMEPAD },
	{ USB_VENDOR_ID_DWAV, USB_DEVICE_ID_EGALAX_TOUCHCONTROLLER },
//	{ USB_VENDOR_ID_MOJO, USB_DEVICE_ID_RETRO_ADAPTER },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_DRIVING },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_FLYING },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_FIGHTING },
	{ USB_VENDOR_ID_NATSU, USB_DEVICE_ID_NATSU_GAMEPAD },
	{ USB_VENDOR_ID_NEC, USB_DEVICE_ID_NEC_USB_GAME_PAD },
	{ USB_VENDOR_ID_NEXTWINDOW, USB_DEVICE_ID_NEXTWINDOW_TOUCHSCREEN },
	{ USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RUMBLEPAD },
	{ USB_VENDOR_ID_TOPMAX, USB_DEVICE_ID_TOPMAX_COBRAPAD },
	{ 0 }
};

// PC analog joystick to USB adapters
static const struct hidquirk hidnorawinput[] =  {
	{ 0x0583, 0x2030 }, // Rockfire RM-203 1
	{ 0x0583, 0x2031 }, // Rockfire RM-203 2
	{ 0x0583, 0x2032 }, // Rockfire RM-203 3
	{ 0x0583, 0x2033 }, // Rockfire RM-203 4
	{ 0x079d, 0x0201 }, // "USB  ADAPTOR"
	{ 0 }
};

static void fixhidvcaps (RID_DEVICE_INFO_HID *hid, HIDP_VALUE_CAPS *caps)
{
	int pid = hid->dwProductId;
	int vid = hid->dwVendorId;

	caps->LogicalMin = extractbits(caps->LogicalMin, caps->BitSize, caps->LogicalMin < 0);
	caps->LogicalMax = extractbits(caps->LogicalMax, caps->BitSize, caps->LogicalMin < 0);
	caps->PhysicalMin = extractbits(caps->PhysicalMin, caps->BitSize, caps->PhysicalMin < 0);
	caps->PhysicalMax = extractbits(caps->PhysicalMax, caps->BitSize, caps->PhysicalMin < 0);

	for (int i = 0; quirks[i].vid; i++) {
		if (vid == quirks[i].vid && pid == quirks[i].pid) {
			caps->LogicalMin = 0;
			caps->LogicalMax = 255;
			break;
		}
	}
}

static void dumphidvaluecaps (PHIDP_VALUE_CAPS vcaps, int size)
{
	for (int i = 0; i < size; i++) {
		HIDP_VALUE_CAPS caps = vcaps[i];
		write_log (L"******** VALUE_CAPS: %d ********\n", i);
		write_log (L"UsagePage: %u\n", caps.UsagePage);
		write_log (L"ReportID: %u\n", caps.ReportID);
		write_log (L"IsAlias: %u\n", caps.IsAlias);
		write_log (L"BitField: %u\n", caps.BitField);
		write_log (L"LinkCollection: %u\n", caps.LinkCollection);
		write_log (L"LinkUsage: %u\n", caps.LinkUsage);
		write_log (L"LinkUsagePage: %u\n", caps.LinkUsagePage);
		write_log (L"IsRange: %u\n", caps.IsRange);
		write_log (L"IsStringRange: %u\n", caps.IsStringRange);
		write_log (L"IsDesignatorRange: %u\n", caps.IsDesignatorRange);
		write_log (L"IsAbsolute: %u\n", caps.IsAbsolute);
		write_log (L"HasNull: %u\n", caps.HasNull);
		write_log (L"BitSize: %u\n", caps.BitSize);
		write_log (L"ReportCount: %u\n", caps.ReportCount);
		write_log (L"UnitsExp: %u\n", caps.UnitsExp);
		write_log (L"Units: %u\n", caps.Units);
		write_log (L"LogicalMin: %u (%d)\n", caps.LogicalMin, extractbits (caps.LogicalMin, caps.BitSize, caps.LogicalMin < 0));
		write_log (L"LogicalMax: %u (%d)\n", caps.LogicalMax, extractbits (caps.LogicalMax, caps.BitSize, caps.LogicalMin < 0));
		write_log (L"PhysicalMin: %u (%d)\n", caps.PhysicalMin, extractbits (caps.PhysicalMin, caps.BitSize, caps.PhysicalMin < 0));
		write_log (L"PhysicalMax: %u (%d)\n", caps.PhysicalMax, extractbits (caps.PhysicalMax, caps.BitSize, caps.PhysicalMax < 0));
		if (caps.IsRange) {
			write_log (L"UsageMin: %u\n", caps.Range.UsageMin);
			write_log (L"UsageMax: %u\n", caps.Range.UsageMax);
			write_log (L"StringMin: %u\n", caps.Range.StringMin);
			write_log (L"StringMax: %u\n", caps.Range.StringMax);
			write_log (L"DesignatorMin: %u\n", caps.Range.DesignatorMin);
			write_log (L"DesignatorMax: %u\n", caps.Range.DesignatorMax);
			write_log (L"DataIndexMin: %u\n", caps.Range.DataIndexMin);
			write_log (L"DataIndexMax: %u\n", caps.Range.DataIndexMax);
		} else {
			write_log (L"Usage: %u\n", caps.NotRange.Usage);
			write_log (L"StringIndex: %u\n", caps.NotRange.StringIndex);
			write_log (L"DesignatorIndex: %u\n", caps.NotRange.DesignatorIndex);
			write_log (L"DataIndex: %u\n", caps.NotRange.DataIndex);
		}
	}
}

static void dumphidbuttoncaps (PHIDP_BUTTON_CAPS pcaps, int size)
{
	for (int i = 0; i < size; i++) {
		HIDP_BUTTON_CAPS caps = pcaps[i];
		write_log (L"******** BUTTON_CAPS: %d ********\n", i);
		write_log (L"UsagePage: %u\n", caps.UsagePage);
		write_log (L"ReportID: %u\n", caps.ReportID);
		write_log (L"IsAlias: %u\n", caps.IsAlias);
		write_log (L"BitField: %u\n", caps.BitField);
		write_log (L"LinkCollection: %u\n", caps.LinkCollection);
		write_log (L"LinkUsage: %u\n", caps.LinkUsage);
		write_log (L"LinkUsagePage: %u\n", caps.LinkUsagePage);
		write_log (L"IsRange: %u\n", caps.IsRange);
		write_log (L"IsStringRange: %u\n", caps.IsStringRange);
		write_log (L"IsDesignatorRange: %u\n", caps.IsDesignatorRange);
		write_log (L"IsAbsolute: %u\n", caps.IsAbsolute);
		if (caps.IsRange) {
			write_log (L"UsageMin: %u\n", caps.Range.UsageMin);
			write_log (L"UsageMax: %u\n", caps.Range.UsageMax);
			write_log (L"StringMin: %u\n", caps.Range.StringMin);
			write_log (L"StringMax: %u\n", caps.Range.StringMax);
			write_log (L"DesignatorMin: %u\n", caps.Range.DesignatorMin);
			write_log (L"DesignatorMax: %u\n", caps.Range.DesignatorMax);
			write_log (L"DataIndexMin: %u\n", caps.Range.DataIndexMin);
			write_log (L"DataIndexMax: %u\n", caps.Range.DataIndexMax);
		} else {
			write_log (L"Usage: %u\n", caps.NotRange.Usage);
			write_log (L"StringIndex: %u\n", caps.NotRange.StringIndex);
			write_log (L"DesignatorIndex: %u\n", caps.NotRange.DesignatorIndex);
			write_log (L"DataIndex: %u\n", caps.NotRange.DataIndex);
		}
	}
}

static void dumphidcaps (struct didata *did)
{
	HIDP_CAPS caps = did->hidcaps;

	write_log (_T("Usage: %04x\n"), caps.Usage);
	write_log (_T("UsagePage: %04x\n"), caps.UsagePage);
	write_log (_T("InputReportByteLength: %u\n"), caps.InputReportByteLength);
	write_log (_T("OutputReportByteLength: %u\n"), caps.OutputReportByteLength);
	write_log (_T("FeatureReportByteLength: %u\n"), caps.FeatureReportByteLength);
	write_log (_T("NumberLinkCollectionNodes: %u\n"), caps.NumberLinkCollectionNodes);
	write_log (_T("NumberInputButtonCaps: %u\n"), caps.NumberInputButtonCaps);
	write_log (_T("NumberInputValueCaps: %u\n"), caps.NumberInputValueCaps);
	write_log (_T("NumberInputDataIndices: %u\n"), caps.NumberInputDataIndices);
	write_log (_T("NumberOutputButtonCaps: %u\n"), caps.NumberOutputButtonCaps);
	write_log (_T("NumberOutputValueCaps: %u\n"), caps.NumberOutputValueCaps);
	write_log (_T("NumberOutputDataIndices: %u\n"), caps.NumberOutputDataIndices);
	write_log (_T("NumberFeatureButtonCaps: %u\n"), caps.NumberFeatureButtonCaps);
	write_log (_T("NumberFeatureValueCaps: %u\n"), caps.NumberFeatureValueCaps);
	write_log (_T("NumberFeatureDataIndices: %u\n"), caps.NumberFeatureDataIndices);
}

static void dumphidend (void)
{
	write_log (_T("\n"));
}

static const TCHAR *tohex(TCHAR *s)
{
	static TCHAR out[128 * 6];

	int len = 0;
	TCHAR *d = out;
	uae_u8 *ss = (uae_u8*)s;
	while (*s && len < 128 - 1) {
		if (len > 0) {
			*d++ = ' ';
		}
		_stprintf(d, _T("%02X %02X"), ss[0], ss[1]);
		d += _tcslen(d);
		ss += 2;
		s++;
		len++;
	}
	*d = 0;
	return out;
}

static void add_xinput_device(struct didata *did)
{
	TCHAR tmp[256];
	
	did->connection = DIDC_XINPUT;


	int buttoncnt = 0;
	for (int i = 1; i <= 16; i++) {
		did->buttonsort[buttoncnt] = i * 2;
		did->buttonmappings[buttoncnt] = i;
		_stprintf(tmp, _T("Button %d"), i);
		did->buttonname[buttoncnt] = my_strdup(tmp);
		buttoncnt++;
	}
	did->buttons = buttoncnt;



	fixbuttons(did);
	fixthings(did);
}


#define MAX_RAW_KEYBOARD 0

static bool initialize_rawinput (void)
{
	RAWINPUTDEVICELIST *ridl = 0;
	UINT num = 500, vtmp;
	int gotnum, bufsize;
	int rnum_mouse, rnum_kb, rnum_hid, rnum_raw;
	TCHAR *bufp, *buf1, *buf2;
	int rmouse = 0, rkb = 0, rhid = 0;
	TCHAR tmp[100];

	bufsize = 10000 * sizeof (TCHAR);
	bufp = xmalloc (TCHAR, 2 * bufsize / sizeof (TCHAR));
	buf1 = bufp;
	buf2 = buf1 + 10000;

	if (GetRawInputDeviceList (NULL, &num, sizeof (RAWINPUTDEVICELIST)) != 0) {
		write_log (_T("RAWINPUT error %08X\n"), GetLastError());
		goto error2;
	}
	write_log (_T("RAWINPUT: found %d devices\n"), num);
	if (num <= 0)
		goto error2;
	ridl = xcalloc (RAWINPUTDEVICELIST, num);
	gotnum = GetRawInputDeviceList (ridl, &num, sizeof (RAWINPUTDEVICELIST));
	if (gotnum <= 0) {
		write_log (_T("RAWINPUT didn't find any devices\n"));
		goto error2;
	}

	if (rawinput_enabled_hid) {
		for (int rawcnt = 0; rawcnt < gotnum; rawcnt++) {
			int type = ridl[rawcnt].dwType;
			HANDLE h = ridl[rawcnt].hDevice;
			PRID_DEVICE_INFO rdi;

			if (type != RIM_TYPEHID)
				continue;
			rdi = (PRID_DEVICE_INFO)buf2;
			memset (rdi, 0, sizeof (RID_DEVICE_INFO));
			rdi->cbSize = sizeof (RID_DEVICE_INFO);
			if (GetRawInputDeviceInfo (h, RIDI_DEVICEINFO, NULL, &vtmp) == -1)
				continue;
			if (vtmp >= bufsize)
				continue;
			if (GetRawInputDeviceInfo (h, RIDI_DEVICEINFO, buf2, &vtmp) == -1)
				continue;
			for (int i = 0; hidnorawinput[i].vid; i++) {
				if (hidnorawinput[i].vid == rdi->hid.dwVendorId && hidnorawinput[i].pid == rdi->hid.dwProductId) {
					write_log (_T("Found USB HID device that requires calibration (%04X/%04X), disabling HID RAWINPUT support\n"),
						rdi->hid.dwVendorId, rdi->hid.dwProductId);
					rawinput_enabled_hid = 0;
				}
			}
		}
	}

	rnum_raw = rnum_mouse = rnum_kb = rnum_hid = 0;
	raw_handles = 0;
	for (int rawcnt = 0; rawcnt < gotnum; rawcnt++) {
		int type = ridl[rawcnt].dwType;
		HANDLE h = ridl[rawcnt].hDevice;

		if (raw_handles < MAX_RAW_HANDLES)
			rawinputhandles[raw_handles++] = h;
		if (GetRawInputDeviceInfo (h, RIDI_DEVICENAME, NULL, &vtmp) == 1)
			continue;
		if (vtmp >= bufsize)
			continue;
		if (GetRawInputDeviceInfo (h, RIDI_DEVICENAME, buf1, &vtmp) == -1)
			continue;
		if (rdpdevice (buf1))
			continue;
		if (type == RIM_TYPEMOUSE)
			rnum_mouse++;
		else if (type == RIM_TYPEKEYBOARD)
			rnum_kb++;
		else if (type == RIM_TYPEHID)
			rnum_hid++;
	}
	if (MAX_RAW_KEYBOARD > 0 && rnum_kb > MAX_RAW_KEYBOARD)
		rnum_kb = MAX_RAW_KEYBOARD;

	write_log (_T("HID device check:\n"));
	for (int rawcnt = 0; rawcnt < gotnum; rawcnt++) {
		HANDLE h = ridl[rawcnt].hDevice;
		int type = ridl[rawcnt].dwType;

		if (type == RIM_TYPEKEYBOARD || type == RIM_TYPEMOUSE || type == RIM_TYPEHID) {
			TCHAR prodname[128];
			struct didata *did;
			PRID_DEVICE_INFO rdi;
			int v, i, j;

			if (rawinput_decided) {
				// must not enable rawinput later, even if rawinput capable device was plugged in
				if (type == RIM_TYPEKEYBOARD && !rawinput_enabled_keyboard)
					continue;
				if (type == RIM_TYPEMOUSE && !rawinput_enabled_mouse)
					continue;
				if (type == RIM_TYPEHID && !rawinput_enabled_hid)
					continue;
			}
			if (type == RIM_TYPEKEYBOARD) {
				if (num_keyboard >= rnum_kb)
					continue;
				did = di_keyboard;
			} else if (type == RIM_TYPEMOUSE) {
				did = di_mouse;
			} else if (type == RIM_TYPEHID) {
				if (!rawinput_enabled_hid)
					continue;
				did = di_joystick;
			} else
				continue;

			if (GetRawInputDeviceInfo (h, RIDI_DEVICENAME, NULL, &vtmp) == -1) {
				write_log (_T("%p RIDI_DEVICENAME failed\n"), h);
				continue;
			}
			if (vtmp >= bufsize) {
				write_log (_T("%p RIDI_DEVICENAME too big %d\n"), h, vtmp);
				continue;
			}
			if (GetRawInputDeviceInfo (h, RIDI_DEVICENAME, buf1, &vtmp) == -1) {
				write_log (_T("%p RIDI_DEVICENAME %d failed\n"), h, vtmp);
				continue;
			}

			rdi = (PRID_DEVICE_INFO)buf2;
			memset (rdi, 0, sizeof (RID_DEVICE_INFO));
			rdi->cbSize = sizeof (RID_DEVICE_INFO);
			if (GetRawInputDeviceInfo (h, RIDI_DEVICEINFO, NULL, &vtmp) == -1) {
				write_log (_T("%p RIDI_DEVICEINFO failed\n"), h);
				continue;
			}
			if (vtmp >= bufsize) {
				write_log (_T("%p RIDI_DEVICEINFO too big %d\n"), h, vtmp);
				continue;
			}
			if (GetRawInputDeviceInfo (h, RIDI_DEVICEINFO, buf2, &vtmp) == -1) {
				write_log (_T("%p RIDI_DEVICEINFO %d failed\n"), h, vtmp);
				continue;
			}

			write_log (_T("%p %d %04X/%04X (%d/%d)\n"), h, type, rdi->hid.dwVendorId, rdi->hid.dwProductId, rdi->hid.usUsage, rdi->hid.usUsagePage);

			if (type == RIM_TYPEMOUSE) {
				if (rdpdevice (buf1))
					continue;
				if (num_mouse >= MAX_INPUT_DEVICES - (no_windowsmouse ? 0 : 1))  {/* leave space for Windows mouse */
					write_log (_T("Too many mice\n"));
					continue;
				}
				did += num_mouse;
				num_mouse++;
				rmouse++;
				v = rmouse;
			} else if (type == RIM_TYPEKEYBOARD) {
				if (rdpdevice (buf1))
					continue;
				if (num_keyboard >= MAX_INPUT_DEVICES) {
					write_log (_T("Too many keyboards\n"));
					continue;
				}
				did += num_keyboard;
				num_keyboard++;
				rkb++;
				v = rkb;
			} else if (type == RIM_TYPEHID) {
				if (rdpdevice (buf1))
					continue;
				if (rdi->hid.usUsagePage != 0x01) {
					write_log(_T("RAWHID: UsagePage not 1 (%04x)\n"), rdi->hid.usUsagePage);
					continue;
				}
				if (rdi->hid.usUsage != 4 && rdi->hid.usUsage != 5) {
					write_log (_T("RAWHID: Usage not 4 or 5 (%04X)\n"), rdi->hid.usUsage);
					continue;
				}
				for (i = 0; hidnorawinput[i].vid; i++) {
					if (rdi->hid.dwProductId == hidnorawinput[i].pid && rdi->hid.dwVendorId == hidnorawinput[i].vid)
						break;
				}
				if (hidnorawinput[i].vid) {
					write_log (_T("RAWHID: blacklisted\n"));
					continue;
				}
				if (num_joystick >= MAX_INPUT_DEVICES) {
					write_log (_T("RAWHID: too many devices\n"));
					continue;
				}
				did += num_joystick;
				num_joystick++;
				rhid++;
				v = rhid;
			}

			prodname[0] = 0;
			HANDLE hhid = NULL;
			hhid = CreateFile (buf1, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			// mouse and keyboard don't allow READ or WRITE access
			if (hhid == INVALID_HANDLE_VALUE) {
				hhid = CreateFile(buf1, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			}
			if (hhid != INVALID_HANDLE_VALUE) {
				if (!HidD_GetProductString (hhid, prodname, sizeof prodname)) {
					prodname[0] = 0;
				} else {
					while (_tcslen (prodname) > 0 && prodname[_tcslen (prodname) - 1] == ' ') {
						prodname[_tcslen (prodname) - 1] = 0;
					}
					// Corrupted productstrings do exist so lets just ignore it if has non-ASCII characters
					for (int i = 0; i < _tcslen(prodname); i++) {
						if (prodname[i] >= 0x100 || prodname[i] < 32) {
							prodname[0] = 0;
							write_log(_T("HidD_GetProductString ignored!\n"));
							break;
						}
					}
				}
				if (prodname[0])
					write_log(_T("(%s) '%s'\n"), tohex(prodname), prodname);
			} else {
				write_log (_T("HID CreateFile failed %d\n"), GetLastError ());
			}

			if (type == RIM_TYPEMOUSE) {
				prodname[0] = 0;
			}

			rnum_raw++;
			cleardid (did);
			if (_tcsstr(buf1, _T("IG_"))) {
				did->xinput = 1;
			}
			getvidpid (buf1, &did->vid, &did->pid, &did->mi);
			if (prodname[0]) {
				_tcscpy (tmp, prodname);
				did->fullname = true;
			} else {
				TCHAR *st = type == RIM_TYPEHID ? (did->xinput ? _T("RAW HID+XINPUT") : _T("RAW HID")) : (type == RIM_TYPEMOUSE ? _T("RAW Mouse") : _T("RAW Keyboard"));
				if (did->vid > 0 && did->pid > 0)
					_stprintf (tmp, _T("%s (%04X/%04X)"), st, did->vid, did->pid);
				else
					_stprintf (tmp, _T("%s"), st);
			}
			did->name = my_strdup (tmp);

			did->rawinput = h;
			did->rawhidhandle = hhid;
			did->connection = DIDC_RAW;

			write_log (_T("%p %p [%04X/%04X] %s: "), h, hhid, did->vid, did->pid, type == RIM_TYPEHID ? _T("hid") : (type == RIM_TYPEMOUSE ? _T("mouse") : _T("keyboard")));
			did->sortname = my_strdup (buf1);
			write_log (_T("'%s'\n"), buf1);

			did->configname = my_strdup (buf1);
			if (_tcslen(did->configname) >= MAX_JPORT_CONFIG)
				did->configname[MAX_JPORT_CONFIG - 1] = 0;

			if (type == RIM_TYPEMOUSE) {
				PRID_DEVICE_INFO_MOUSE rdim = &rdi->mouse;
				write_log (_T("id=%d buttons=%d hw=%d rate=%d\n"),
					rdim->dwId, rdim->dwNumberOfButtons, rdim->fHasHorizontalWheel, rdim->dwSampleRate);
				int buttons = rdim->dwNumberOfButtons;
				// limit to 20, can only have 32 buttons and it also includes [-][+] axis events.
				if (buttons > 20) {
					write_log(_T("too many buttons (%d > 20)\n"), buttons);
					buttons = 20;
				}
				did->buttons_real = did->buttons = (uae_s16)buttons;
				for (j = 0; j < did->buttons; j++) {
					did->buttonsort[j] = j;
					did->buttonmappings[j] = j;
					_stprintf (tmp, _T("Button %d"), j + 1);
					did->buttonname[j] = my_strdup (tmp);
				}
				did->axles = 3;
				did->axissort[0] = 0;
				did->axismappings[0] = 0;
				did->axisname[0] = my_strdup (_T("X Axis"));
				did->axissort[1] = 1;
				did->axismappings[1] = 1;
				did->axisname[1] = my_strdup (_T("Y Axis"));
				did->axissort[2] = 2;
				did->axismappings[2] = 2;
				did->axisname[2] = my_strdup (_T("Wheel"));
				addplusminus (did, 2);
				if (1 || rdim->fHasHorizontalWheel) { // why is this always false?
					did->axissort[3] = 3;
					did->axisname[3] = my_strdup (_T("HWheel"));
					did->axismappings[3] = 3;
					did->axles++;
					addplusminus (did, 3);
				}
				if (num_mouse == 1)
					did->priority = -1;
				else
					did->priority = -2;
			} else if (type == RIM_TYPEKEYBOARD) {
				PRID_DEVICE_INFO_KEYBOARD rdik = &rdi->keyboard;
				write_log (_T("type=%d sub=%d mode=%d fkeys=%d indicators=%d tkeys=%d\n"),
					rdik->dwType, rdik->dwSubType, rdik->dwKeyboardMode,
					rdik->dwNumberOfFunctionKeys, rdik->dwNumberOfIndicators, rdik->dwNumberOfKeysTotal);
				addrkblabels (did);
				if (num_keyboard == 1)
					did->priority = -1;
				else
					did->priority = -2;
			} else {
				bool ok = false;

#if 0
				if (did->xinput) {
					CloseHandle(did->rawhidhandle);
					did->rawhidhandle = NULL;
					rhid--;
					rnum_raw--;
					add_xinput_device(did);
					continue;
				}
#endif
				if (hhid != INVALID_HANDLE_VALUE && HidD_GetPreparsedData (hhid, &did->hidpreparseddata)) {
					if (HidP_GetCaps (did->hidpreparseddata, &did->hidcaps) == HIDP_STATUS_SUCCESS) {
						PHIDP_BUTTON_CAPS bcaps;
						USHORT size = did->hidcaps.NumberInputButtonCaps;
						write_log(_T("RAWHID: %d/%d %d '%s' ('%s')\n"), rawcnt, gotnum, num_joystick - 1, did->name, did->configname);
						dumphidcaps (did);
						bcaps = xmalloc (HIDP_BUTTON_CAPS, size);
						if (HidP_GetButtonCaps (HidP_Input, bcaps, &size, did->hidpreparseddata) == HIDP_STATUS_SUCCESS) {
							dumphidbuttoncaps (bcaps, size);
							int buttoncnt = 0;
							// limit to 20, can only have 32 buttons and it also includes [-][+] axis events.
							for (i = 0; i < size && buttoncnt < 20; i++) {
								int first, last;
								if (bcaps[i].UsagePage >= 0xff00)
									continue;
								if (bcaps[i].IsRange) {
									first = bcaps[i].Range.UsageMin;
									last = bcaps[i].Range.UsageMax;
								} else {
									first = last = bcaps[i].NotRange.Usage;
								}
								for (j = first; j <= last && buttoncnt < 20; j++) {
									int k;
									for (k = 0; k < buttoncnt; k++) {
										if (did->buttonmappings[k] == j)
											break;
									}
									if (k == buttoncnt) {
										did->buttonsort[buttoncnt] = j * 2;
										did->buttonmappings[buttoncnt] = j;
										_stprintf (tmp, _T("Button %d"), j);
										did->buttonname[buttoncnt] = my_strdup (tmp);
										buttoncnt++;
									}
								}
							}
							if (buttoncnt > 0) {
								did->buttons = buttoncnt;
								ok = true;
							}
						}
						xfree (bcaps);
						PHIDP_VALUE_CAPS vcaps;
						size = did->hidcaps.NumberInputValueCaps;
						vcaps = xmalloc (HIDP_VALUE_CAPS, size);
						if (HidP_GetValueCaps (HidP_Input, vcaps, &size, did->hidpreparseddata) == HIDP_STATUS_SUCCESS) {
#if 0
							for (i = 0; i < size; i++) {
								int usage1;
								if (vcaps[i].IsRange)
									usage1 = vcaps[i].Range.UsageMin;
								else
									usage1 = vcaps[i].NotRange.Usage;
								for (j = i + 1; j < size; j++) {
									int usage2;
									if (vcaps[j].IsRange)
										usage2 = vcaps[j].Range.UsageMin;
									else
										usage2 = vcaps[j].NotRange.Usage;
									if (usage1 < usage2) {
										HIDP_VALUE_CAPS tcaps;
										memcpy(&tcaps, &vcaps[i], sizeof(HIDP_VALUE_CAPS));
										memcpy(&vcaps[i], &vcaps[j], sizeof(HIDP_VALUE_CAPS));
										memcpy(&vcaps[j], &tcaps, sizeof(HIDP_VALUE_CAPS));
									}
								}
							}
#endif
							dumphidvaluecaps (vcaps, size);
							int axiscnt = 0;
							for (i = 0; i < size && axiscnt < ID_AXIS_TOTAL; i++) {
								int first, last;
								if (vcaps[i].IsRange) {
									first = vcaps[i].Range.UsageMin;
									last = vcaps[i].Range.UsageMax;
								} else {
									first = last = vcaps[i].NotRange.Usage;
								}
								for (int acnt = first; acnt <= last && axiscnt < ID_AXIS_TOTAL; acnt++) {
									int ht;
									for (ht = 0; hidtable[ht].name; ht++) {
										if (hidtable[ht].usage == acnt && hidtable[ht].page == vcaps[i].UsagePage) {
											int k;
											for (k = 0; k < axiscnt; k++) {
												if (did->axismappings[k] == acnt)
													break;
											}
											if (k == axiscnt) {	
												if (hidtable[ht].page == 0x01 && acnt == 0x39) { // POV
													if (axiscnt + 1 < ID_AXIS_TOTAL) {
														for (int l = 0; l < 2; l++) {
															TCHAR tmp[256];
															_stprintf (tmp, _T("%s (%c)"), hidtable[ht].name,  l == 0 ? 'X' : 'Y');
															did->axisname[axiscnt] = my_strdup (tmp);
															did->axissort[axiscnt] = hidtable[ht].priority * 2 + l;
															did->axismappings[axiscnt] = acnt;
															memcpy (&did->hidvcaps[axiscnt], &vcaps[i], sizeof(HIDP_VALUE_CAPS));
															did->axistype[axiscnt] = l + 1;
															axiscnt++;
														}
													}
												} else {
													did->axissort[axiscnt] = hidtable[ht].priority * 2;
													did->axisname[axiscnt] = my_strdup (hidtable[ht].name);
													did->axismappings[axiscnt] = acnt;
													memcpy (&did->hidvcaps[axiscnt], &vcaps[i], sizeof(HIDP_VALUE_CAPS));
													fixhidvcaps (&rdi->hid, &did->hidvcaps[axiscnt]);
#if NEGATIVEMINHACK
													did->hidvcaps[axiscnt].LogicalMin -= (did->hidvcaps[axiscnt].LogicalMax / 2);
													did->hidvcaps[axiscnt].LogicalMax -= (did->hidvcaps[axiscnt].LogicalMax / 2);
#endif
													did->axistype[axiscnt] = hidtable[ht].type;
													axiscnt++;
													did->analogstick = true;
												}
												break;
											}
										}
									}
									if (hidtable[ht].name == NULL)
										write_log (_T("unsupported usage %d/%d\n"), vcaps[i].UsagePage, acnt);
								}
							}
							if (axiscnt > 0) {
								did->axles = axiscnt;
								ok = true;
							}
						}
						xfree (vcaps);
						dumphidend ();
					}
				}
				if (ok) {
					did->hidbuffer = xmalloc (CHAR, did->hidcaps.InputReportByteLength + 1);
					did->hidbufferprev = xmalloc (CHAR, did->hidcaps.InputReportByteLength + 1);
					did->maxusagelistlength = HidP_MaxUsageListLength (HidP_Input, 0, did->hidpreparseddata);
					did->usagelist = xmalloc (USAGE_AND_PAGE, did->maxusagelistlength);
					did->prevusagelist = xcalloc (USAGE_AND_PAGE, did->maxusagelistlength);
					fixbuttons (did);
					fixthings (did);
					rawhid_found = true;
				} else {
					if (did->hidpreparseddata)
						HidD_FreePreparsedData (did->hidpreparseddata);
					did->hidpreparseddata = NULL;
					num_joystick--;
					rhid--;
					rnum_raw--;
				}
			}
		}
	}

	if (rnum_kb	&& num_keyboard < MAX_INPUT_DEVICES - 1) {
		struct didata *did = di_keyboard + num_keyboard;
		num_keyboard++;
		rnum_kb++;
		did->name = my_strdup (_T("WinUAE keyboard"));
		did->rawinput = NULL;
		did->connection = DIDC_RAW;
		did->sortname = my_strdup (_T("NULLKEYBOARD"));
		did->priority = 2;
		did->configname = my_strdup (_T("NULLKEYBOARD"));
		addrkblabels (did);
	}

	rawinputfriendlynames ();

	xfree (ridl);
	xfree (bufp);
	if (rnum_raw > 0)
		rawinput_available = 1;

	for (int i = 0; i < num_mouse; i++)
		sortobjects (&di_mouse[i]);
	for (int i = 0; i < num_joystick; i++)
		sortobjects (&di_joystick[i]);

	return 1;

error2:
	xfree (ridl);
	xfree (bufp);
	return 0;
}

static void initialize_windowsmouse (void)
{
	struct didata *did = di_mouse;
	TCHAR tmp[100], *name;
	int i, j;

	did += num_mouse;
	for (i = 0; i < 2; i++) {
		if (num_mouse >= MAX_INPUT_DEVICES)
			return;
		cleardid (did);
		num_mouse++;
		name = (i == 0) ? _T("Windows mouse") : _T("Touchscreen light pen");
		did->connection = DIDC_WIN;
		did->name = my_strdup (i ? _T("Touchscreen light pen") : _T("Windows mouse"));
		did->sortname = my_strdup (i ? _T("Lightpen1") : _T("Windowsmouse1"));
		did->configname = my_strdup (i ? _T("LIGHTPEN1") : _T("WINMOUSE1"));
		did->buttons = GetSystemMetrics (SM_CMOUSEBUTTONS);
		if (did->buttons < 3)
			did->buttons = 3;
		if (did->buttons > 5)
			did->buttons = 5; /* no non-direcinput support for >5 buttons */
		if (i == 1)
			did->buttons = 1;
		did->buttons_real = did->buttons;
		for (j = 0; j < did->buttons; j++) {
			did->buttonsort[j] = j;
			_stprintf (tmp, _T("Button %d"), j + 1);
			did->buttonname[j] = my_strdup (tmp);
		}
		winmousewheelbuttonstart = did->buttons;
		if (i == 0) {
			did->axles = 4;
			did->axissort[0] = 0;
			did->axisname[0] = my_strdup (_T("X Axis"));
			did->axissort[1] = 1;
			did->axisname[1] = my_strdup (_T("Y Axis"));
			if (did->axles > 2) {
				did->axissort[2] = 2;
				did->axisname[2] = my_strdup (_T("Wheel"));
				addplusminus (did, 2);
			}
			if (did->axles > 3) {
				did->axissort[3] = 3;
				did->axisname[3] = my_strdup (_T("HWheel"));
				addplusminus (did, 3);
			}
			did->priority = 2;
		} else {
			did->priority = 1;
		}
		did->wininput = i + 1;
		did++;
		if (!is_tablet())
			break;
	}
}

static uae_u8 rawkeystate[256];
static int rawprevkey;
static int key_lshift, key_lwin;

static void sendscancode(int num, int scancode, int pressed)
{
	scancode = keyhack(scancode, pressed, num);
#if DEBUG_SCANCODE
	write_log(_T("%02X %d %d\n"), scancode, pressed, isfocus());
#endif
	if (scancode < 0)
		return;
	if (!isfocus())
		return;
	if (isfocus() < 2 && currprefs.input_tablet >= TABLET_MOUSEHACK && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MAGIC))
		return;
	if (!mouseactive && !(currprefs.win32_active_input & 1)) {
		if ((currprefs.win32_guikey <= 0 && scancode == DIK_F12) || (scancode == currprefs.win32_guikey)) {
			inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
		}
		return;
	}
	if (pressed) {
		di_keycodes[num][scancode] = 1;
	}
	else {
		if ((di_keycodes[num][scancode] & 1) && pause_emulation) {
			di_keycodes[num][scancode] = 2;
		}
		else {
			di_keycodes[num][scancode] = 0;
		}
	}
	if (stopoutput == 0) {
		my_kbd_handler(num, scancode, pressed, false);
	}
}

static void handle_rawinput_2 (RAWINPUT *raw, LPARAM lParam)
{
	int i, num;
	struct didata *did;
	int istest = inputdevice_istest ();

	if (raw->header.dwType == RIM_TYPEMOUSE) {
		PRAWMOUSE rm = &raw->data.mouse;
		HANDLE h = raw->header.hDevice;

		for (num = 0; num < num_mouse; num++) {
			did = &di_mouse[num];
			if (did->acquired) {
				if (did->rawinput == h)
					break;
			}
		}
		if (rawinput_log & 2) {
			write_log(_T("%p %04x %04x %04x %08x %3d %3d %08x M=%d F=%d\n"),
				raw->header.hDevice,
				rm->usFlags,
				rm->usButtonFlags,
				rm->usButtonData,
				rm->ulRawButtons,
				rm->lLastX,
				rm->lLastY,
				rm->ulExtraInformation, num < num_mouse ? num + 1 : -1,
				isfocus());
		}

#if OUTPUTDEBUG
		TCHAR xx[256];
		_stprintf(xx, _T("%p %d %p %04x %04x %04x %08x %3d %3d %08x M=%d F=%d\n"),
			GetCurrentProcess(), timeframes,
			raw->header.hDevice,
			rm->usFlags,
			rm->usButtonFlags,
			rm->usButtonData,
			rm->ulRawButtons,
			rm->lLastX,
			rm->lLastY,
			rm->ulExtraInformation, num < num_mouse ? num + 1 : -1,
			isfocus());
		OutputDebugString(xx);
#endif

		USHORT usButtonFlags = rm->usButtonFlags;

#ifdef RETROPLATFORM
		if (usButtonFlags && isfocus() != 0) {
			usButtonFlags = rp_rawbuttons(lParam, usButtonFlags);
		}
#endif

		if (num == num_mouse)
			return;
		if (rp_ismouseevent())
			return;

		if (isfocus () > 0 || istest) {
			static int lastx[MAX_INPUT_DEVICES], lasty[MAX_INPUT_DEVICES];
			static int lastmbr[MAX_INPUT_DEVICES];
			for (i = 0; i < (5 > did->buttons ? did->buttons : 5); i++) {
				if (usButtonFlags & (3 << (i * 2))) {
					int state = (usButtonFlags & (1 << (i * 2))) ? 1 : 0;
					if (!istest && i == 2 && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON))
						continue;
					setmousebuttonstate (num, i, state);
				}
			}
			if (did->buttons > 5) {
				for (i = 5; i < did->buttons; i++) {
					if ((lastmbr[num] & (1 << i)) != (rm->ulRawButtons & (1 << i)))
						setmousebuttonstate (num, i, (rm->ulRawButtons & (1 << i)) ? 1 : 0);
				}
				lastmbr[num] = rm->ulRawButtons;
			}
			for (i = 0; i < 2; i++) {
				int bnum = did->buttons_real + (i * 2);
				// RI_MOUSE_WHEEL << 1 = HWHEEL
				if (rm->usButtonFlags & (RI_MOUSE_WHEEL << i)) {
					int val = (short)rm->usButtonData;
					setmousestate (num, 2, val, 0);
					if (istest)
						setmousestate (num, 2, 0, 0);
					if (val < 0)
						setmousebuttonstate (num, bnum + 0, -1);
					else if (val > 0)
						setmousebuttonstate (num, bnum + 1, -1);
				}
			}
			if (!rm->ulButtons) {
				if (istest) {
					static time_t ot;
					time_t t = time (NULL);
					if (t != ot && t != ot + 1) {
						if (abs (rm->lLastX - lastx[num]) > 7) {
							setmousestate (num, 0, rm->lLastX, (rm->usFlags & (MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP)) ? 1 : 0);
							lastx[num] = rm->lLastX;
							lasty[num] = rm->lLastY;
							ot = t;
						}
						if (abs (rm->lLastY - lasty[num]) > 7) {
							setmousestate (num, 1, rm->lLastY, (rm->usFlags & (MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP)) ? 1 : 0);
							lastx[num] = rm->lLastX;
							lasty[num] = rm->lLastY;
							ot = t;
						}
					}
				} else {
					setmousestate (num, 0, rm->lLastX, (rm->usFlags & (MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP)) ? 1 : 0);
					setmousestate (num, 1, rm->lLastY, (rm->usFlags & (MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP)) ? 1 : 0);
					lastx[num] = rm->lLastX;
					lasty[num] = rm->lLastY;
				}
			}
		}
		if (isfocus () && !istest) {
			if (did->buttons >= 3 && (usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)) {
				if (currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) {
					if ((isfullscreen() < 0 && currprefs.win32_minimize_inactive) || isfullscreen() > 0)
						minimizewindow(0);
					if (mouseactive)
						setmouseactive(0, 0);
				}
			}
		}

	} else if (raw->header.dwType == RIM_TYPEHID) {
		int j;
		PRAWHID hid = &raw->data.hid;
		HANDLE h = raw->header.hDevice;
		PCHAR rawdata;

		if ((rawinput_log & 4) || RAWINPUT_DEBUG) {
			static uae_u8 *oldbuf;
			static int oldbufsize;
			if (oldbufsize != hid->dwSizeHid) {
				xfree(oldbuf);
				oldbufsize = hid->dwSizeHid;
				oldbuf = xcalloc(uae_u8, oldbufsize);
			}
			uae_u8 *r = hid->bRawData;
			if (memcmp(r, oldbuf, oldbufsize)) {
				write_log (_T("%d %d "), hid->dwCount, hid->dwSizeHid);
				for (int i = 0; i < hid->dwSizeHid; i++)
					write_log (_T("%02X"), r[i]);
				write_log (_T(" H=%p\n"), h);
				memcpy(oldbuf, r, oldbufsize);
			}
		}
		for (num = 0; num < num_joystick; num++) {
			did = &di_joystick[num];
			if (did->connection != DIDC_RAW)
				continue;
			if (did->acquired && did->rawinput == h)
				break;
		}

#if RAWINPUT_DEBUG
		if (num >= num_joystick) {
			if (!rawinput_enabled_hid)
				return;
			write_log(_T("RAWHID unknown input handle %p\n"), h);
			for (num = 0; num < num_joystick; num++) {
				did = &di_joystick[num];
				if (did->connection != DIDC_RAW)
					continue;
				if (!did->acquired && did->rawinput == h) {
					write_log(_T("RAWHID %d %p was unacquired!\n"), num, did->rawinput);
					return;
				}
			}
		}
#endif

#ifdef RETROPLATFORM
		if (rp_isactive ())
			return;
#endif
		if (!istest && !mouseactive && !(currprefs.win32_active_input & 4)) {
			return;
		}

		if (num < num_joystick) {

			rawdata = (PCHAR)hid->bRawData;
			for (int i = 0; i < hid->dwCount; i++) {
				DWORD usagelength = did->maxusagelistlength;
				if (HidP_GetUsagesEx (HidP_Input, 0, did->usagelist, &usagelength, did->hidpreparseddata, rawdata, hid->dwSizeHid) == HIDP_STATUS_SUCCESS) {
					int k;
					for (k = 0; k < usagelength; k++) {
						if (did->usagelist[k].UsagePage >= 0xff00)
							continue; // ignore vendor specific
						for (j = 0; j < did->maxusagelistlength; j++) {
							if (did->usagelist[k].UsagePage == did->prevusagelist[j].UsagePage &&
								did->usagelist[k].Usage == did->prevusagelist[j].Usage)
								break;
						}
						if (j == did->maxusagelistlength || did->prevusagelist[j].Usage == 0) {
							if (rawinput_log & 4)
								write_log (_T("%d/%d ON\n"), did->usagelist[k].UsagePage, did->usagelist[k].Usage);

							for (int l = 0; l < did->buttons; l++) {
								if (did->buttonmappings[l] == did->usagelist[k].Usage)
									setjoybuttonstate (num, l, 1);
							}
						} else {
							did->prevusagelist[j].Usage = 0;
						}
					}
					for (j = 0; j < did->maxusagelistlength; j++) {
						if (did->prevusagelist[j].UsagePage >= 0xff00)
							continue; // ignore vendor specific
						if (did->prevusagelist[j].Usage) {
							if (rawinput_log & 4)
								write_log (_T("%d/%d OFF\n"), did->prevusagelist[j].UsagePage, did->prevusagelist[j].Usage);

							for (int l = 0; l < did->buttons; l++) {
								if (did->buttonmappings[l] == did->prevusagelist[j].Usage)
									setjoybuttonstate (num, l, 0);
							}
						}
					}
					memcpy (did->prevusagelist, did->usagelist, usagelength * sizeof(USAGE_AND_PAGE));
					memset (did->prevusagelist + usagelength, 0, (did->maxusagelistlength - usagelength) * sizeof(USAGE_AND_PAGE));
					for (int axisnum = 0; axisnum < did->axles; axisnum++) {
						ULONG val;
						int usage = did->axismappings[axisnum];
						NTSTATUS status;
						
						status = HidP_GetUsageValue (HidP_Input, did->hidvcaps[axisnum].UsagePage, 0, usage, &val, did->hidpreparseddata, rawdata, hid->dwSizeHid);
						if (status == HIDP_STATUS_SUCCESS) {
							int data = 0;
							int digitalrange = 0;
							HIDP_VALUE_CAPS *vcaps = &did->hidvcaps[axisnum];
							int type = did->axistype[axisnum];
							int logicalrange = (vcaps->LogicalMax - vcaps->LogicalMin) / 2;
							uae_u32 mask = hidmask (vcaps->BitSize);
							int buttonaxistype;

							if (type == AXISTYPE_POV_X || type == AXISTYPE_POV_Y) {

								int min = vcaps->LogicalMin;
								if (vcaps->LogicalMax - min == 7) {
									if ((val == min + 0 || val == min + 1 || val == min + 7) && type == AXISTYPE_POV_Y)
										data = -127;
									if ((val == min + 2 || val == min + 3 || val == min + 1) && type == AXISTYPE_POV_X)
										data = 127;
									if ((val == min + 4 || val == min + 5 || val == min + 3) && type == AXISTYPE_POV_Y)
										data = 127;
									if ((val == min + 6 || val == min + 7 || val == min + 5) && type == AXISTYPE_POV_X)
										data = -127;
								} else {
									if (val == min + 0 && type == AXISTYPE_POV_Y)
										data = -127;
									if (val == min + 1 && type == AXISTYPE_POV_X)
										data = 127;
									if (val == min + 2 && type == AXISTYPE_POV_Y)
										data = 127;
									if (val == min + 3 && type == AXISTYPE_POV_X)
										data = -127;
								}
								logicalrange = 127;
								digitalrange = logicalrange / 2;
								buttonaxistype = type == AXISTYPE_POV_X ? 1 : 2;

							} else {

								int v;
				
#if NEGATIVEMINHACK
								v = extractbits(val, vcaps->BitSize, 0);
								v -= (vcaps->LogicalMax - vcaps->LogicalMin) / 2;
#else
								v = extractbits(val, vcaps->BitSize, vcaps->LogicalMin < 0);
#endif

								if (v < vcaps->LogicalMin)
									v = vcaps->LogicalMin;
								else if (v > vcaps->LogicalMax)
									v = vcaps->LogicalMax;

								data = v - logicalrange + (0 - vcaps->LogicalMin);

								if (rawinput_log & 4)
									write_log(_T("DEV %d AXIS %d: %d %d (%d - %d) %d %d\n"), num, axisnum, digitalrange, logicalrange, vcaps->LogicalMin, vcaps->LogicalMax, v, data);

								digitalrange = logicalrange * 2 / 3;
								if (istest) {
									if (data < -digitalrange)
										data = -logicalrange;
									else if (data > digitalrange)
										data = logicalrange;
									else
										data = 0;
								}
								buttonaxistype = -1;
							}

							if (data != axisold[num][axisnum] && logicalrange) {
								//write_log (_T("%d %d: %d->%d %d\n"), num, axisnum, axisold[num][axisnum], data, logicalrange);
								axisold[num][axisnum] = data;
								for (j = 0; j < did->buttons; j++) {
									if (did->buttonaxisparent[j] >= 0 && did->buttonmappings[j] == usage && (did->buttonaxistype[j] == buttonaxistype || buttonaxistype < 0)) {
										int bstate = -1;
										int bstate2 = 0;

										int axistype = did->axistype[j];
										if (did->buttonaxisparentdir[j] == 0 && data < -digitalrange) {
											bstate = j;
											bstate2 = 1;
										} else if (did->buttonaxisparentdir[j] != 0 && data > digitalrange) {
											bstate = j;
											bstate2 = 1;
										} else if (data >= -digitalrange && data <= digitalrange) {
											bstate = j;
											bstate2 = 0;
										}

										if (bstate >= 0 && buttonold[num][bstate] != bstate2) {
											if (rawinput_log & 4)
												write_log (_T("[+-] DEV %d AXIS %d: %d %d %d %d (%s)\n"), num, axisnum, digitalrange, data, bstate, bstate2, did->buttonname[bstate]);
											buttonold[num][bstate] = bstate2;
											setjoybuttonstate (num, bstate, bstate2);
										}
									}
								}
								setjoystickstate (num, axisnum, data, logicalrange);
							}
						}
					}
				}
				rawdata += hid->dwSizeHid;
			}
		}

	} else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
		PRAWKEYBOARD rk = &raw->data.keyboard;
		HANDLE h = raw->header.hDevice;
		int scancode = rk->MakeCode & 0x7f;
		int pressed = (rk->Flags & RI_KEY_BREAK) ? 0 : 1;

		if (rawinput_log & 1)
			write_log (_T("HANDLE=%x CODE=%x Flags=%x VK=%x MSG=%x EXTRA=%x SC=%x\n"),
				raw->header.hDevice,
				rk->MakeCode,
				rk->Flags,
				rk->VKey,
				rk->Message,
				rk->ExtraInformation,
				scancode);

#ifdef RETROPLATFORM
		if (rp_isactive ())
			return;
#endif
		if (key_swap_hack == 1) {
			if (scancode == DIK_F11) {
				scancode = DIK_EQUALS;
			} else if (scancode == DIK_EQUALS) {
				scancode = DIK_F11;
			}
		}

		// eat E1 extended keys
		if (rk->Flags & (RI_KEY_E1))
			return;
		if (scancode == 0) {
			scancode = MapVirtualKey (rk->VKey, MAPVK_VK_TO_VSC);
			if (rawinput_log & 1)
				write_log (_T("VK->CODE: %x\n"), scancode);

		}
		if (rk->VKey == 0xff || ((rk->Flags & RI_KEY_E0) && !(winekeyboard && rk->VKey == VK_NUMLOCK)))
			scancode |= 0x80;
		if (winekeyboard && rk->VKey == VK_PAUSE)
			scancode |= 0x80;
		if (rk->MakeCode == KEYBOARD_OVERRUN_MAKE_CODE)
			return;
		if (scancode == 0xaa || scancode == 0)
			return;

		if (currprefs.right_control_is_right_win_key && scancode == DIK_RCONTROL) {
			scancode = DIK_RWIN;
		}

		if (!istest) {
			if (scancode == DIK_SYSRQ)
				clipboard_disable (!!pressed);
			if (h == NULL) {
				// swallow led key fake messages
				if (currprefs.keyboard_leds[KBLED_NUMLOCKB] > 0 && scancode == DIK_NUMLOCK)
					return;
				if (currprefs.keyboard_leds[KBLED_CAPSLOCKB] > 0 && scancode == DIK_CAPITAL)
					return;
				if (currprefs.keyboard_leds[KBLED_SCROLLLOCKB] > 0 && scancode == DIK_SCROLL)
					return;
			} else {
				static bool ch;
				if (pressed) {
					if (currprefs.keyboard_leds[KBLED_NUMLOCKB] > 0 && scancode == DIK_NUMLOCK) {
						oldleds ^= KBLED_NUMLOCKM;
						ch = true;
					}
					if (currprefs.keyboard_leds[KBLED_CAPSLOCKB] > 0 && scancode == DIK_CAPITAL) {
						oldleds ^= KBLED_CAPSLOCKM;
						ch = true;
					}
					if (currprefs.keyboard_leds[KBLED_SCROLLLOCKB] > 0 && scancode == DIK_SCROLL) {
						oldleds ^= KBLED_SCROLLLOCKM;
						ch = true;
					}
				} else if (ch && isfocus ()) {
					ch = false;
					set_leds (newleds);
				}
			}
		}

		for (num = 0; num < num_keyboard; num++) {
			did = &di_keyboard[num];
			if (did->connection != DIDC_RAW)
				continue;
			if (did->acquired) {
				if (did->rawinput == h)
					break;
			}
		}
		if (num == num_keyboard) {
			// find winuae keyboard
			for (num = 0; num < num_keyboard; num++) {
				did = &di_keyboard[num];
				if (did->connection == DIDC_RAW && did->acquired && did->rawinput == NULL)
					break;
			}
			if (num == num_keyboard) {
				if (!istest && scancode == DIK_F12 && pressed && isfocus ())
					inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
				return;
			}
		}

		// More complex press/release check because we want to support
		// keys that never return releases, only presses but also need
		// handle normal keys that can repeat.

#if 0
		if (!pressed)
			return;
#endif
		// quick hack to support copilot+ key as right amiga key
		bool key_lx_repress = false;
		if (scancode == DIK_LWIN) {
			if (pressed) {
				if (key_lwin < 2) {
					key_lwin++;
				}
			} else {
				if (key_lwin > 0) {
					key_lwin--;
				}
			}
		}
		if (scancode == DIK_LSHIFT) {
			if (pressed) {
				if (key_lshift < 2) {
					key_lshift++;
				}
			} else {
				if (key_lshift > 0) {
					key_lshift--;
				}
			}
		}
		if (scancode == 0x6e && key_lwin > 0 && key_lshift > 0) {
			if (pressed) {
				if (key_lwin == 1) {
					sendscancode(num, DIK_LWIN, 0);
				}
				if (key_lshift == 1) {
					sendscancode(num, DIK_LSHIFT, 0);
				}
				scancode = DIK_RWIN;
			} else {
				key_lx_repress = true;
				scancode = DIK_RWIN;
			}
			rawprevkey = -1;
		}

		if (pressed) {
			// previously pressed key and current press is same key? Repeat. Ignore it.
			if (scancode == rawprevkey)
				return;
			rawprevkey = scancode;
			if (rawkeystate[scancode] == 2) {
				// Got press for key that is already pressed.
				// Must be ignored. It is repeat.
				rawkeystate[scancode] = 1;
				return;
			}
			rawkeystate[scancode] = 1;
		} else {
			rawprevkey = -1;
			// release without press: ignore
			if (rawkeystate [scancode] == 0)
				return;
			rawkeystate[scancode] = 0;
			// Got release, if following press is key that
			// is currently pressed: ignore it, it is repeat.
			// Mark all currently pressed keys.
			for (int i = 0; i < sizeof (rawkeystate); i++) {
				if (rawkeystate[i] == 1)
					rawkeystate[i] = 2;
			}
		}

		if (istest) {
			if (pressed && (scancode == DIK_F12))
				return;
			if (scancode == DIK_F12) {
				inputdevice_testrecord (IDTYPE_KEYBOARD, num, IDEV_WIDGET_BUTTON, 0x101, 1, -1);
				inputdevice_testrecord (IDTYPE_KEYBOARD, num, IDEV_WIDGET_BUTTON, 0x101, 0, -1);
			} else {
				inputdevice_testrecord (IDTYPE_KEYBOARD, num, IDEV_WIDGET_BUTTON, scancode, pressed, -1);
			}
		} else {
			sendscancode(num, scancode, pressed);
			if (key_lx_repress) {
				if (key_lwin == 1) {
					sendscancode(num, DIK_LWIN, 1);
				}
				if (key_lshift == 1) {
					sendscancode(num, DIK_LSHIFT, 1);
				}
			}
		}
	}
}

bool is_hid_rawinput(void)
{
	if (no_rawinput & 4)
		return false;
	if (!rawinput_enabled_hid && !rawinput_enabled_hid_reset)
		return false;
	return true;
}

static bool match_rawinput(struct didata *didp, const TCHAR *name, HANDLE h)
{
	for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
		struct didata *did = &didp[i];
		if (did->connection != DIDC_RAW)
			continue;
		if (!_tcscmp(name, did->configname)) {
#if RAWINPUT_DEBUG
			write_log(_T("- matched: %p -> %p (%s)\n"), did->rawinput, h, name);
#endif
			for (int j = 0; j < raw_handles; j++) {
				if (rawinputhandles[j] == did->rawinput) {
					rawinputhandles[j] = h;
				}
			}
			did->rawinput = h;
			return true;
		}
	}
	return false;
}

bool handle_rawinput_change(LPARAM lParam, WPARAM wParam)
{
	HANDLE h = (HANDLE)lParam;
	UINT vtmp;
	uae_u8 buf[10000];
	bool ret = true;

#if RAWINPUT_DEBUG
	PRID_DEVICE_INFO rdi;
	write_log(_T("WM_INPUT_DEVICE_CHANGE  %p %08x\n"), lParam, wParam);
#endif

	for (int i = 0; i < raw_handles; i++) {
		if (rawinputhandles[i] == h) {
#if RAWINPUT_DEBUG
			write_log(_T("- Already existing\n"));
#endif
			ret = false;
		}
	}
	if (ret) {
#if RAWINPUT_DEBUG
		write_log(_T("- Not found, re-enumerating if no match..\n"));
#endif
	}

	// existing removed
	if (wParam == GIDC_REMOVAL && !ret) {
#if RAWINPUT_DEBUG
		write_log(_T("- Existing device removed, re-enumerating..\n"));
#endif
		return true;
	}

	if (wParam != GIDC_ARRIVAL)
		return false;

	if (GetRawInputDeviceInfo(h, RIDI_DEVICENAME, NULL, &vtmp) == -1)
		return false;
	if (vtmp >= sizeof buf)
		return false;
	if (GetRawInputDeviceInfo(h, RIDI_DEVICENAME, buf, &vtmp) == -1)
		return false;

	TCHAR *dn = (TCHAR*)buf;
	// name matches? Replace handle, do not re-enumerate
	if (match_rawinput(di_mouse, dn, h))
		return false;
	if (match_rawinput(di_keyboard, dn, h))
		return false;
	if (match_rawinput(di_joystick, dn, h))
		return false;

	// new device
#if RAWINPUT_DEBUG
	rdi = (PRID_DEVICE_INFO)buf;
	memset(rdi, 0, sizeof(RID_DEVICE_INFO));
	rdi->cbSize = sizeof(RID_DEVICE_INFO);
	if (GetRawInputDeviceInfo(h, RIDI_DEVICEINFO, NULL, &vtmp) == -1)
		return false;
	if (vtmp >= sizeof buf)
		return false;
	if (GetRawInputDeviceInfo(h, RIDI_DEVICEINFO, buf, &vtmp) == -1)
		return false;

	write_log(_T("Type = %d\n"), rdi->dwType);
	if (rdi->dwType == RIM_TYPEHID) {
		RID_DEVICE_INFO_HID *hid = &rdi->hid;
		write_log(_T("%04x %04x %04x %04x %04x\n"),
			hid->dwVendorId, hid->dwProductId, hid->dwVersionNumber, hid->usUsage, hid->usUsagePage);
	}
#endif

	return ret;
}

void handle_rawinput(LPARAM lParam)
{
	UINT dwSize = 0;
	BYTE lpb[1000];
	RAWINPUT *raw;

	if (!rawinput_available)
		return;
	if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) >= 0) {
		if (dwSize <= sizeof(lpb)) {
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
				raw = (RAWINPUT*)lpb;
				if (!isguiactive() || (inputdevice_istest() && isguiactive())) {
					handle_rawinput_2 (raw, lParam);
				}
				DefRawInputProc(&raw, 1, sizeof(RAWINPUTHEADER));
			} else {
				write_log(_T("GetRawInputData(%d) failed, %d\n"), dwSize, GetLastError ());
			}
		} else {
			write_log(_T("GetRawInputData() too large buffer %d\n"), dwSize);
		}
	}  else {
		write_log(_T("GetRawInputData(-1) failed, %d\n"), GetLastError ());
	}
}

static void unacquire (LPDIRECTINPUTDEVICE8 lpdi, TCHAR *txt)
{
	HRESULT hr;
	if (lpdi) {
		hr = IDirectInputDevice8_Unacquire (lpdi);
		if (FAILED (hr) && hr != DI_NOEFFECT)
			write_log (_T("unacquire %s failed, %s\n"), txt, DXError (hr));
	}
}

static int acquire (LPDIRECTINPUTDEVICE8 lpdi, TCHAR *txt)
{
	HRESULT hr = DI_OK;
	if (lpdi) {
		hr = IDirectInputDevice8_Acquire (lpdi);
		if (FAILED (hr) && hr != 0x80070005) {
			write_log (_T("acquire %s failed, %s\n"), txt, DXError (hr));
		}
	}
	return SUCCEEDED (hr) ? 1 : 0;
}

static int setcoop (struct didata *did, DWORD mode, TCHAR *txt)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	HRESULT hr = DI_OK;
	HWND hwnd;
	int test = inputdevice_istest ();
	
	if (!test)
		hwnd = mon->hMainWnd;
	else
		hwnd = hGUIWnd;

	if (did->lpdi) {
		did->coop = 0;
		if (!did->coop && hwnd) {
			hr = IDirectInputDevice8_SetCooperativeLevel (did->lpdi, hwnd, mode);
			if (FAILED (hr) && hr != E_NOTIMPL) {
				write_log (_T("setcooperativelevel %s failed, %s\n"), txt, DXError (hr));
			} else {
				did->coop = 1;
				//write_log (_T("cooperativelevel %s set\n"), txt);
			}
		}
	}
	return SUCCEEDED (hr) ? 1 : 0;
}

static void sortdd2 (struct didata *dd, int num, int type)
{
	for (int i = 0; i < num; i++) {
		struct didata *did1 = &dd[i];
		did1->type = type;
		for (int j = i + 1; j < num; j++) {
			struct didata *did2 = &dd[j];
			did2->type = type;
			if (did1->priority < did2->priority || (did1->priority == did2->priority && _tcscmp (did1->sortname, did2->sortname) > 0)) {
				struct didata ddtmp;
				memcpy (&ddtmp, did1, sizeof ddtmp);
				memcpy (did1, did2, sizeof ddtmp);
				memcpy (did2, &ddtmp, sizeof ddtmp);
			}
		}
	}
}
static void sortdd (struct didata *dd, int num, int type)
{
	sortdd2 (dd, num, type);
	/* rename duplicate names */
	for (int i = 0; i < num; i++) {
		struct didata *did1 = &dd[i];
		for (int j = i + 1; j < num; j++) {
			struct didata *did2 = &dd[j];
			if (!_tcscmp (did1->name, did2->name)) {
				int cnt = 1;
				TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
				_tcscpy (tmp2, did1->name);
				for (j = i; j < num; j++) {
					did2 = &dd[j];
					if (!_tcscmp (tmp2, did2->name)) {
						_stprintf (tmp, _T("%s [%d]"), did2->name, cnt);
						xfree (did2->name);
						did2->name = my_strdup (tmp);
						_stprintf (tmp, _T("%s [%d]"), did2->sortname, cnt);
						xfree (did2->sortname);
						did2->sortname = my_strdup (tmp);
						cnt++;
					}
				}
				break;
			}
		}
	}
	sortdd2 (dd, num, type);
}

static int isg (const GUID *g, const GUID *g2, short *dwofs, int v)
{
	if (!memcmp (g, g2, sizeof (GUID))) {
		*dwofs = v;
		return 1;
	}
	return 0;
}

static int makesort_joy (const GUID *g, short *dwofs)
{

	if (isg (g, &GUID_XAxis, dwofs, DIJOFS_X)) return -99;
	if (isg (g, &GUID_YAxis, dwofs, DIJOFS_Y)) return -98;
	if (isg (g, &GUID_ZAxis, dwofs, DIJOFS_Z)) return -97;
	if (isg (g, &GUID_RxAxis, dwofs, DIJOFS_RX)) return -89;
	if (isg (g, &GUID_RyAxis, dwofs, DIJOFS_RY)) return -88;
	if (isg (g, &GUID_RzAxis, dwofs, DIJOFS_RZ)) return -87;
	if (isg (g, &GUID_Slider, dwofs, DIJOFS_SLIDER(0))) return -79;
	if (isg (g, &GUID_Slider, dwofs, DIJOFS_SLIDER(1))) return -78;
	if (isg (g, &GUID_POV, dwofs, DIJOFS_POV(0))) return -69;
	if (isg (g, &GUID_POV, dwofs, DIJOFS_POV(1))) return -68;
	if (isg (g, &GUID_POV, dwofs, DIJOFS_POV(2))) return -67;
	if (isg (g, &GUID_POV, dwofs, DIJOFS_POV(3))) return -66;
	return *dwofs;
}

static int makesort_mouse (const GUID *g, short *dwofs)
{
	if (isg (g, &GUID_XAxis, dwofs, DIMOFS_X)) return -99;
	if (isg (g, &GUID_YAxis, dwofs, DIMOFS_Y)) return -98;
	if (isg (g, &GUID_ZAxis, dwofs, DIMOFS_Z)) return -97;
	return *dwofs;
}

static BOOL CALLBACK EnumObjectsCallback (const DIDEVICEOBJECTINSTANCE* pdidoi, VOID *pContext)
{
	struct didata *did = (struct didata*)pContext;
	int i;
	TCHAR tmp[100];

#if 0
	if (pdidoi->dwOfs != DIDFT_GETINSTANCE (pdidoi->dwType))
		write_log (_T("%x-%s: %x <> %x\n"), pdidoi->dwType & 0xff, pdidoi->tszName,
		pdidoi->dwOfs, DIDFT_GETINSTANCE (pdidoi->dwType));
#endif
	if (pdidoi->dwType & DIDFT_AXIS) {
		int sort = 0;
		if (did->axles >= ID_AXIS_TOTAL)
			return DIENUM_CONTINUE;
		did->axismappings[did->axles] = DIDFT_GETINSTANCE (pdidoi->dwType);
		did->axisname[did->axles] = my_strdup (pdidoi->tszName);
		if (pdidoi->wUsagePage == 1 && (pdidoi->wUsage == 0x30 || pdidoi->wUsage == 0x31))
			did->analogstick = true;
		if (did->type == DID_JOYSTICK)
			sort = makesort_joy (&pdidoi->guidType, &did->axismappings[did->axles]);
		else if (did->type == DID_MOUSE)
			sort = makesort_mouse (&pdidoi->guidType, &did->axismappings[did->axles]);
		if (sort < 0) {
			for (i = 0; i < did->axles; i++) {
				if (did->axissort[i] == sort) {
					write_log (_T("ignored duplicate '%s'\n"), pdidoi->tszName);
					return DIENUM_CONTINUE;
				}
			}
		}
		did->axissort[did->axles] = sort;
		did->axles++;
	}
	if (pdidoi->dwType & DIDFT_POV) {
		int numpov = 0;
		if (did->axles + 1 >= ID_AXIS_TOTAL)
			return DIENUM_CONTINUE;
		for (i = 0; i < did->axles; i++) {
			if (did->axistype[i]) {
				numpov++;
				i++;
			}
		}
		if (did->type == DID_JOYSTICK)
			did->axissort[did->axles] = makesort_joy (&pdidoi->guidType, &did->axismappings[did->axles]);
		else if (did->type == DID_MOUSE)
			did->axissort[did->axles] = makesort_mouse (&pdidoi->guidType, &did->axismappings[did->axles]);
		for (i = 0; i < 2; i++) {
			did->axismappings[did->axles + i] = (uae_s16)DIJOFS_POV(numpov);
			_stprintf (tmp, _T("%s (%c)"), pdidoi->tszName, i ? 'Y' : 'X');
			did->axisname[did->axles + i] = my_strdup (tmp);
			did->axissort[did->axles + i] = did->axissort[did->axles];
			did->axistype[did->axles + i] = i + 1;
		}
		did->axles += 2;
	}

	if (pdidoi->dwType & DIDFT_BUTTON) {
		if (did->buttons >= ID_BUTTON_TOTAL && did->type != DID_KEYBOARD)
			return DIENUM_CONTINUE;
		TCHAR *bname = did->buttonname[did->buttons] = my_strdup (pdidoi->tszName);
		if (did->type == DID_JOYSTICK) {
			//did->buttonmappings[did->buttons] = DIJOFS_BUTTON(DIDFT_GETINSTANCE (pdidoi->dwType));
			did->buttonmappings[did->buttons] = DIJOFS_BUTTON(did->buttons);
			did->buttonsort[did->buttons] = makesort_joy (&pdidoi->guidType, &did->buttonmappings[did->buttons]);
		} else if (did->type == DID_MOUSE) {
			//did->buttonmappings[did->buttons] = FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + DIDFT_GETINSTANCE (pdidoi->dwType);
			did->buttonmappings[did->buttons] = FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + did->buttons;
			did->buttonsort[did->buttons] = makesort_mouse (&pdidoi->guidType, &did->buttonmappings[did->buttons]);
		} else if (did->type == DID_KEYBOARD) {
			//did->buttonmappings[did->buttons] = pdidoi->dwOfs;
			did->buttonmappings[did->buttons] = DIDFT_GETINSTANCE (pdidoi->dwType);
		}
		did->buttons++;
	}

	return DIENUM_CONTINUE;
}

static void trimws (TCHAR *s)
{
	/* Delete trailing whitespace.  */
	int len = uaetcslen(s);
	while (len > 0 && _tcscspn(s + len - 1, _T("\t \r\n")) == 0)
		s[--len] = '\0';
}

static BOOL di_enumcallback2 (LPCDIDEVICEINSTANCE lpddi, int joy)
{
	struct didata *did;
	int len, type;
	TCHAR *typetxt;
	TCHAR tmp[100];

	if (rawinput_enabled_hid && (lpddi->dwDevType & DIDEVTYPE_HID)) {
		return 1;
	}

	type = lpddi->dwDevType & 0xff;
	if (type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_SCREENPOINTER) {
		did = di_mouse;
		typetxt = _T("Mouse");
	} else if ((type == DI8DEVTYPE_GAMEPAD  || type == DI8DEVTYPE_JOYSTICK || type == DI8DEVTYPE_SUPPLEMENTAL ||
		type == DI8DEVTYPE_FLIGHT || type == DI8DEVTYPE_DRIVING || type == DI8DEVTYPE_1STPERSON) || joy) {
			did = di_joystick;
			typetxt = _T("Game controller");
	} else if (type == DI8DEVTYPE_KEYBOARD) {
		did = di_keyboard;
		typetxt = _T("Keyboard");
	} else {
		did = NULL;
		typetxt = _T("Unknown");
	}

#if DI_DEBUG
	write_log (_T("I=%s "), outGUID (&lpddi->guidInstance));
	write_log (_T("P=%s\n"), outGUID (&lpddi->guidProduct));
	write_log (_T("'%s' '%s' %08X [%s]\n"), lpddi->tszProductName, lpddi->tszInstanceName, lpddi->dwDevType, typetxt);
#endif

	if (did == di_mouse) {
		if (num_mouse >= MAX_INPUT_DEVICES)
			return DIENUM_CONTINUE;
		did += num_mouse;
		num_mouse++;
	} else if (did == di_joystick) {
		if (num_joystick >= MAX_INPUT_DEVICES)
			return DIENUM_CONTINUE;
		did += num_joystick;
		num_joystick++;
	} else if (did == di_keyboard) {
		if (num_keyboard >= MAX_INPUT_DEVICES)
			return DIENUM_CONTINUE;
		did += num_keyboard;
		num_keyboard++;
	} else
		return DIENUM_CONTINUE;

	cleardid (did);
	if (lpddi->tszInstanceName) {
		len = uaetcslen(lpddi->tszInstanceName) + 5 + 1;
		did->name = xmalloc (TCHAR, len);
		_tcscpy (did->name, lpddi->tszInstanceName);
	} else {
		did->name = xmalloc (TCHAR, 100);
		_tcscpy (did->name, _T("[no name]"));
	}
	trimws (did->name);
	_stprintf (tmp, _T("%08X-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X %08X-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X"),
		lpddi->guidProduct.Data1, lpddi->guidProduct.Data2, lpddi->guidProduct.Data3,
		lpddi->guidProduct.Data4[0], lpddi->guidProduct.Data4[1], lpddi->guidProduct.Data4[2], lpddi->guidProduct.Data4[3],
		lpddi->guidProduct.Data4[4], lpddi->guidProduct.Data4[5], lpddi->guidProduct.Data4[6], lpddi->guidProduct.Data4[7],
		lpddi->guidInstance.Data1, lpddi->guidInstance.Data2, lpddi->guidInstance.Data3,
		lpddi->guidInstance.Data4[0], lpddi->guidInstance.Data4[1], lpddi->guidInstance.Data4[2], lpddi->guidInstance.Data4[3],
		lpddi->guidInstance.Data4[4], lpddi->guidInstance.Data4[5], lpddi->guidInstance.Data4[6], lpddi->guidInstance.Data4[7]);
	did->configname = my_strdup (tmp);
	trimws (did->configname);
	did->iguid = lpddi->guidInstance;
	did->pguid = lpddi->guidProduct;
	did->sortname = my_strdup (did->name);
	did->connection = DIDC_DX;

	if (!memcmp (&did->iguid, &GUID_SysKeyboard, sizeof (GUID)) || !memcmp (&did->iguid, &GUID_SysMouse, sizeof (GUID))) {
		did->priority = 2;
		did->superdevice = 1;
	}
	return DIENUM_CONTINUE;
}

static BOOL CALLBACK di_enumcallback (LPCDIDEVICEINSTANCE lpddi, LPVOID dd)
{
	return di_enumcallback2 (lpddi, 0);
}
static BOOL CALLBACK di_enumcallbackj (LPCDIDEVICEINSTANCE lpddi, LPVOID dd)
{
	return di_enumcallback2 (lpddi, 1);
}

static void di_dev_free (struct didata *did)
{
	if (did->lpdi)
		IDirectInputDevice8_Release (did->lpdi);
	if (did->parjoy != INVALID_HANDLE_VALUE)
		CloseHandle (did->parjoy);
	xfree (did->name);
	xfree (did->sortname);
	xfree (did->configname);
	if (did->hidpreparseddata)
		HidD_FreePreparsedData (did->hidpreparseddata);
	if (did->rawhidhandle != INVALID_HANDLE_VALUE)
		CloseHandle(did->rawhidhandle);
	xfree (did->hidbuffer);
	xfree (did->hidbufferprev);
	xfree (did->usagelist);
	xfree (did->prevusagelist);
	cleardid(did);
}

#if 0
#define SAFE_RELEASE(x) if(x) x->Release();

// believe it or not, this is MS example code!
BOOL IsXInputDevice(const GUID* pGuidProductFromDirectInput)
{
	IWbemLocator*           pIWbemLocator = NULL;
	IEnumWbemClassObject*   pEnumDevices = NULL;
	IWbemClassObject*       pDevices[20] = { 0 };
	IWbemServices*          pIWbemServices = NULL;
	BSTR                    bstrNamespace = NULL;
	BSTR                    bstrDeviceID = NULL;
	BSTR                    bstrClassName = NULL;
	DWORD                   uReturned = 0;
	bool                    bIsXinputDevice = false;
	UINT                    iDevice = 0;
	VARIANT                 var;
	HRESULT                 hr;

	// CoInit if needed
	hr = CoInitialize(NULL);
	bool bCleanupCOM = SUCCEEDED(hr);

	// Create WMI
	hr = CoCreateInstance(__uuidof(WbemLocator),
		NULL,
		CLSCTX_INPROC_SERVER,
		__uuidof(IWbemLocator),
		(LPVOID*)&pIWbemLocator);
	if (FAILED(hr) || pIWbemLocator == NULL)
		goto LCleanup;

	bstrNamespace = SysAllocString(L"\\\\.\\root\\cimv2"); if (bstrNamespace == NULL) goto LCleanup;
	bstrClassName = SysAllocString(L"Win32_PNPEntity");   if (bstrClassName == NULL) goto LCleanup;
	bstrDeviceID = SysAllocString(L"DeviceID");          if (bstrDeviceID == NULL)  goto LCleanup;

	// Connect to WMI 
	hr = pIWbemLocator->ConnectServer(bstrNamespace, NULL, NULL, 0L,
		0L, NULL, NULL, &pIWbemServices);
	if (FAILED(hr) || pIWbemServices == NULL)
		goto LCleanup;

	// Switch security level to IMPERSONATE. 
	CoSetProxyBlanket(pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
		RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

	hr = pIWbemServices->CreateInstanceEnum(bstrClassName, 0, NULL, &pEnumDevices);
	if (FAILED(hr) || pEnumDevices == NULL)
		goto LCleanup;

	// Loop over all devices
	for (;; )
	{
		// Get 20 at a time
		hr = pEnumDevices->Next(10000, 20, pDevices, &uReturned);
		if (FAILED(hr))
			goto LCleanup;
		if (uReturned == 0)
			break;

		for (iDevice = 0; iDevice<uReturned; iDevice++)
		{
			// For each device, get its device ID
			hr = pDevices[iDevice]->Get(bstrDeviceID, 0L, &var, NULL, NULL);
			if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal != NULL)
			{
				// Check if the device ID contains "IG_".  If it does, then it's an XInput device
				// This information can not be found from DirectInput 
				if (wcsstr(var.bstrVal, L"IG_"))
				{
					// If it does, then get the VID/PID from var.bstrVal
					DWORD dwPid = 0, dwVid = 0;
					WCHAR* strVid = wcsstr(var.bstrVal, L"VID_");
					if (strVid && swscanf(strVid, L"VID_%4X", &dwVid) != 1)
						dwVid = 0;
					WCHAR* strPid = wcsstr(var.bstrVal, L"PID_");
					if (strPid && swscanf(strPid, L"PID_%4X", &dwPid) != 1)
						dwPid = 0;

					// Compare the VID/PID to the DInput device
					DWORD dwVidPid = MAKELONG(dwVid, dwPid);
					if (pGuidProductFromDirectInput && dwVidPid == pGuidProductFromDirectInput->Data1)
					{
						bIsXinputDevice = true;
						goto LCleanup;
					}
				}
			}
			SAFE_RELEASE(pDevices[iDevice]);
		}
	}

LCleanup:
	if (bstrNamespace)
		SysFreeString(bstrNamespace);
	if (bstrDeviceID)
		SysFreeString(bstrDeviceID);
	if (bstrClassName)
		SysFreeString(bstrClassName);
	for (iDevice = 0; iDevice<20; iDevice++)
		SAFE_RELEASE(pDevices[iDevice]);
	SAFE_RELEASE(pEnumDevices);
	SAFE_RELEASE(pIWbemLocator);
	SAFE_RELEASE(pIWbemServices);

	if (bCleanupCOM)
		CoUninitialize();

	return bIsXinputDevice;
}
#endif

static int di_do_init (void)
{
	HRESULT hr;
	int i;

	num_mouse = num_joystick = num_keyboard = 0;

	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		di_dev_free (&di_joystick[i]);
		di_dev_free (&di_mouse[i]);
		di_dev_free (&di_keyboard[i]);
	}

	if (rawinput_enabled_hid_reset) {
		rawinput_enabled_hid = rawinput_enabled_hid_reset;
		rawinput_enabled_hid_reset = 0;
	}

#if 0
	IsXInputDevice(NULL);
#endif

	write_log (_T("RawInput enumeration..\n"));
	if (!initialize_rawinput ())
		rawinput_enabled_hid = 0;

	if (!rawinput_decided) {
		if (!(no_rawinput & 1))
			rawinput_enabled_keyboard = true;
		if (!(no_rawinput & 2))
			rawinput_enabled_mouse = true;
		rawinput_decided = true;
	}
	if (!rawhid_found) {
		// didn't find anything but was enabled? Try again next time.
		rawinput_enabled_hid_reset = rawinput_enabled_hid;
		rawinput_enabled_hid = 0;
	}

	if (!no_directinput || !rawinput_enabled_keyboard || !rawinput_enabled_mouse) {
		hr = DirectInput8Create (hInst, DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID *)&g_lpdi, NULL);
		if (FAILED (hr)) {
			write_log (_T("DirectInput8Create failed, %s\n"), DXError (hr));
		} else {
			if (dinput_enum_all) {
				write_log (_T("DirectInput enumeration..\n"));
				g_lpdi->EnumDevices (DI8DEVCLASS_ALL, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
			} else {
				if (!rawinput_enabled_keyboard) {
					write_log (_T("DirectInput enumeration.. Keyboards..\n"));
					g_lpdi->EnumDevices (DI8DEVCLASS_KEYBOARD, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
				}
				if (!rawinput_enabled_mouse) {
					write_log (_T("DirectInput enumeration.. Pointing devices..\n"));
					g_lpdi->EnumDevices (DI8DEVCLASS_POINTER, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
				}
				if (!no_directinput) {
					write_log (_T("DirectInput enumeration.. Game controllers..\n"));
					g_lpdi->EnumDevices (DI8DEVCLASS_GAMECTRL, di_enumcallbackj, 0, DIEDFL_ATTACHEDONLY);
				}
			}
		}
	}

	if (!no_windowsmouse) {
		write_log (_T("Windowsmouse initialization..\n"));
		initialize_windowsmouse ();
	}
	write_log (_T("Catweasel joymouse initialization..\n"));
	initialize_catweasel ();
//	write_log (_T("Parallel joystick port initialization..\n"));
//	initialize_parjoyport ();
	write_log (_T("wintab tablet initialization..\n"));
	initialize_tablet ();
	write_log (_T("end\n"));

	sortdd (di_joystick, num_joystick, DID_JOYSTICK);
	sortdd (di_mouse, num_mouse, DID_MOUSE);
	sortdd (di_keyboard, num_keyboard, DID_KEYBOARD);

	for (int i = 0; i < num_joystick; i++) {
		write_log(_T("M %02d: '%s' (%s)\n"), i, di_mouse[i].name, di_mouse[i].configname);
	}
	for (int i = 0; i < num_joystick; i++) {
		write_log(_T("J %02d: '%s' (%s)\n"), i, di_joystick[i].name, di_joystick[i].configname);
	}
	for (int i = 0; i < num_keyboard; i++) {
		write_log(_T("K %02d: '%s' (%s)\n"), i, di_keyboard[i].name, di_keyboard[i].configname);
	}

	return 1;
}

static int di_init (void)
{
	if (dd_inited++ > 0)
		return 1;
	if (!di_do_init ())
		return 0;
	return 1;
}

static void di_free (void)
{
	int i;
	if (dd_inited == 0)
		return;
	dd_inited--;
	if (dd_inited > 0)
		return;
	if (g_lpdi)
		IDirectInput8_Release (g_lpdi);
	g_lpdi = 0;
	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		di_dev_free (&di_joystick[i]);
		di_dev_free (&di_mouse[i]);
		di_dev_free (&di_keyboard[i]);
	}
}

static int get_mouse_num (void)
{
	return num_mouse;
}

static TCHAR *get_mouse_friendlyname (int mouse)
{
	return di_mouse[mouse].name;
}

static TCHAR *get_mouse_uniquename (int mouse)
{
	return di_mouse[mouse].configname;
}

static int get_mouse_widget_num (int mouse)
{
	return di_mouse[mouse].axles + di_mouse[mouse].buttons;
}

static int get_mouse_widget_first (int mouse, int type)
{
	switch (type)
	{
	case IDEV_WIDGET_BUTTON:
		return di_mouse[mouse].axles;
	case IDEV_WIDGET_AXIS:
		return 0;
	case IDEV_WIDGET_BUTTONAXIS:
		return di_mouse[mouse].axles + di_mouse[mouse].buttons_real;
	}
	return -1;
}

static int get_mouse_widget_type (int mouse, int num, TCHAR *name, uae_u32 *code)
{
	struct didata *did = &di_mouse[mouse];

	int axles = did->axles;
	int buttons = did->buttons;
	int realbuttons = did->buttons_real;
	if (num >= axles + realbuttons && num < axles + buttons) {
		if (name)
			_tcscpy (name, did->buttonname[num - axles]);
		return IDEV_WIDGET_BUTTONAXIS;
	} else if (num >= axles && num < axles + realbuttons) {
		if (name)
			_tcscpy (name, did->buttonname[num - axles]);
		return IDEV_WIDGET_BUTTON;
	} else if (num < axles) {
		if (name)
			_tcscpy (name, did->axisname[num]);
		return IDEV_WIDGET_AXIS;
	}
	return IDEV_WIDGET_NONE;
}

static int init_mouse (void)
{
	int i;
	LPDIRECTINPUTDEVICE8 lpdi;
	HRESULT hr;
	struct didata *did;

	if (mouse_inited)
		return 1;
	di_init ();
	mouse_inited = 1;
	for (i = 0; i < num_mouse; i++) {
		did = &di_mouse[i];
		if (did->connection == DIDC_DX) {
			hr = g_lpdi->CreateDevice (did->iguid, &lpdi, NULL);
			if (SUCCEEDED (hr)) {
				hr = IDirectInputDevice8_SetDataFormat (lpdi, &c_dfDIMouse2);
				IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, (void*)did, DIDFT_ALL);
				fixbuttons (did);
				fixthings_mouse (did);
				sortobjects (did);
				did->lpdi = lpdi;
			} else {
				write_log (_T("mouse %d CreateDevice failed, %s\n"), i, DXError (hr));
			}
		}
	}
	return 1;
}

static void close_mouse (void)
{
	int i;

	if (!mouse_inited)
		return;
	mouse_inited = 0;
	for (i = 0; i < num_mouse; i++)
		di_dev_free (&di_mouse[i]);
	supermouse = normalmouse = rawmouse = winmouse = 0;
	di_free ();
}

static int acquire_mouse (int num, int flags)
{
	DIPROPDWORD dipdw;
	HRESULT hr;

	if (num < 0) {
		return 1;
	}

	struct didata *did = &di_mouse[num];

	if (did->connection == DIDC_NONE)
		return 0;

	LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;

	unacquire (lpdi, _T("mouse"));
	did->acquireattempts = MAX_ACQUIRE_ATTEMPTS;
	if (did->connection == DIDC_DX && lpdi) {
		setcoop (&di_mouse[num], flags ? (DISCL_FOREGROUND | DISCL_EXCLUSIVE) : (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE), _T("mouse"));
		dipdw.diph.dwSize = sizeof (DIPROPDWORD);
		dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
		dipdw.diph.dwObj = 0;
		dipdw.diph.dwHow = DIPH_DEVICE;
		dipdw.dwData = DI_BUFFER;
		hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
		if (FAILED (hr))
			write_log (_T("mouse setpropertry failed, %s\n"), DXError (hr));
		did->acquired = acquire (lpdi, _T("mouse")) ? 1 : -1;
	} else {
		did->acquired = 1;
	}
	if (did->acquired > 0) {
		if (did->rawinput) {
			rawmouse++;
		} else if (did->superdevice) {
			supermouse++;
		} else if (did->wininput == 1) {
			winmouse++;
			winmousenumber = num;
		} else if (did->wininput == 2) {
			lightpen++;
			lightpennumber = num;
		} else {
			normalmouse++;
		}
	}
	return did->acquired > 0 ? 1 : 0;
}

static void unacquire_mouse (int num)
{
	if (num < 0) {
		return;
	}

	struct didata *did = &di_mouse[num];

	if (did->connection == DIDC_NONE)
		return;

	unacquire (did->lpdi, _T("mouse"));
	if (did->acquired > 0) {
		if (did->rawinput) {
			rawmouse--;
		} else if (did->superdevice) {
			supermouse--;
		} else if (did->wininput == 1) {
			winmouse--;
		} else if (did->wininput == 2) {
			lightpen--;
		} else {
			normalmouse--;
		}
		did->acquired = 0;
	}
}

static void read_mouse (void)
{
	DIDEVICEOBJECTDATA didod[DI_BUFFER];
	DWORD elements;
	HRESULT hr;
	int i, j, k;
	int fs = isfullscreen () > 0 ? 1 : 0;
	int istest = inputdevice_istest ();

	if (IGNOREEVERYTHING)
		return;

	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		struct didata *did = &di_mouse[i];
		LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;
		if (!did->acquired)
			continue;
		if (did->connection == DIDC_CAT) {
			if (getmousestate (i)) {
				int cx, cy, cbuttons;
				if (catweasel_read_mouse (did->catweasel, &cx, &cy, &cbuttons)) {
					if (cx)
						setmousestate (i, 0, cx, 0);
					if (cy)
						setmousestate (i, 1, cy, 0);
					setmousebuttonstate (i, 0, cbuttons & 8);
					setmousebuttonstate (i, 1, cbuttons & 4);
					setmousebuttonstate (i, 2, cbuttons & 2);
				}
			}
			continue;
		}
		if (!lpdi || did->connection != DIDC_DX)
			continue;
		if (did->acquireattempts <= 0)
			continue;
		elements = DI_BUFFER;
		hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
		if (SUCCEEDED (hr) || hr == DI_BUFFEROVERFLOW) {
			if (supermouse && !did->superdevice)
				continue;
			for (j = 0; j < elements; j++) {
				int dimofs = didod[j].dwOfs;
				int data = didod[j].dwData;
				int state = (data & 0x80) ? 1 : 0;
				if (rawinput_log & 8)
					write_log (_T("MOUSE: %d OFF=%d DATA=%d STATE=%d\n"), i, dimofs, data, state);

				if (istest || isfocus () > 0) {
					for (k = 0; k < did->axles; k++) {
						if (did->axismappings[k] == dimofs) {
							if (istest) {
								static time_t ot;
								time_t t = time (NULL);
								if (t != ot && t != ot + 1) {
									if (data > 7 || data < -7)
										setmousestate (i, k, data, 0);
								}
							} else {
								setmousestate (i, k, data, 0);
							}
						}
					}
					for (k = 0; k < did->buttons; k++) {
						if (did->buttonmappings[k] == dimofs) {
							if (did->buttonaxisparent[k] >= 0) {
								int dir = did->buttonaxisparentdir[k];
								int bstate = 0;
								if (dir)
									bstate = data > 0 ? 1 : 0;
								else
									bstate = data < 0 ? 1 : 0;
								if (bstate)
									setmousebuttonstate (i, k, -1);
							} else {
#ifdef SINGLEFILE
								if (k == 0)
									uae_quit ();
#endif
								if (((currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) && k != 2) || !(currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) || istest)
									setmousebuttonstate (i, k, state);
							}
						}
					}
				}
				if (!istest && isfocus () && (currprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) && dimofs == DIMOFS_BUTTON2 && state) {
					if ((isfullscreen() < 0 && currprefs.win32_minimize_inactive) || isfullscreen() > 0)
						minimizewindow(0);
					if (mouseactive)
						setmouseactive(0, 0);
				}
			}
		} else if (hr == DIERR_INPUTLOST) {
			if (!acquire (lpdi, _T("mouse")))
				did->acquireattempts--;
		} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
			if (!acquire (lpdi, _T("mouse")))
				did->acquireattempts--;
		}
		IDirectInputDevice8_Poll (lpdi);
	}
}

static int get_mouse_flags (int num)
{
	if (!rawinput_enabled_mouse && !num)
		return 1;
	if (di_mouse[num].rawinput || !rawinput_enabled_mouse)
		return 0;
	if (di_mouse[num].catweasel)
		return 0;
	return 1;
}
struct inputdevice_functions inputdevicefunc_mouse = {
	init_mouse, close_mouse, acquire_mouse, unacquire_mouse, read_mouse,
	get_mouse_num, get_mouse_friendlyname, get_mouse_uniquename,
	get_mouse_widget_num, get_mouse_widget_type,
	get_mouse_widget_first,
	get_mouse_flags
};


static int get_kb_num (void)
{
	return num_keyboard;
}

static TCHAR *get_kb_friendlyname (int kb)
{
	return di_keyboard[kb].name;
}

static TCHAR *get_kb_uniquename (int kb)
{
	return di_keyboard[kb].configname;
}

static int get_kb_widget_num (int kb)
{
	return di_keyboard[kb].buttons;
}

static int get_kb_widget_first (int kb, int type)
{
	return 0;
}

static int get_kb_widget_type (int kb, int num, TCHAR *name, uae_u32 *code)
{
	if (name) {
		if (di_keyboard[kb].buttonname[num])
			_tcscpy (name, di_keyboard[kb].buttonname[num]);
		else
			name[0] = 0;
	}
	if (code) {
		*code = di_keyboard[kb].buttonmappings[num];
	}
	return IDEV_WIDGET_KEY;
}

static int init_kb (void)
{
	int i;
	LPDIRECTINPUTDEVICE8 lpdi;
	DIPROPDWORD dipdw;
	HRESULT hr;

	if (keyboard_inited)
		return 1;
	di_init ();
	originalleds = -1;
	keyboard_inited = 1;
	for (i = 0; i < num_keyboard; i++) {
		struct didata *did = &di_keyboard[i];
		if (did->connection == DIDC_DX) {
			hr = g_lpdi->CreateDevice (did->iguid, &lpdi, NULL);
			if (SUCCEEDED (hr)) {
				hr = lpdi->SetDataFormat (&c_dfDIKeyboard);
				if (FAILED (hr))
					write_log (_T("keyboard setdataformat failed, %s\n"), DXError (hr));
				memset (&dipdw, 0, sizeof (dipdw));
				dipdw.diph.dwSize = sizeof (DIPROPDWORD);
				dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
				dipdw.diph.dwObj = 0;
				dipdw.diph.dwHow = DIPH_DEVICE;
				dipdw.dwData = DI_KBBUFFER;
				hr = lpdi->SetProperty (DIPROP_BUFFERSIZE, &dipdw.diph);
				if (FAILED (hr))
					write_log (_T("keyboard setpropertry failed, %s\n"), DXError (hr));
				lpdi->EnumObjects (EnumObjectsCallback, did, DIDFT_ALL);
				sortobjects (did);
				did->lpdi = lpdi;
			} else
				write_log (_T("keyboard CreateDevice failed, %s\n"), DXError (hr));
		}
	}
	keyboard_german = 0;
	if ((LOWORD(GetKeyboardLayout (0)) & 0x3ff) == 7)
		keyboard_german = 1;
	return 1;
}

static void close_kb (void)
{
	int i;

	if (keyboard_inited == 0)
		return;
	keyboard_inited = 0;

	for (i = 0; i < num_keyboard; i++)
		di_dev_free (&di_keyboard[i]);
	superkb = normalkb = rawkb = 0;
	di_free ();
}

static uae_u32 kb_do_refresh;

int ispressed (int key)
{
	if (key < 0 || key > 255)
		return 0;
	int i;
	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		if (di_keycodes[i][key] & 1)
			return 1;
	}
	return 0;
}

void release_keys(void)
{
	for (int j = 0; j < MAX_INPUT_DEVICES; j++) {
		for (int i = 0; i < MAX_KEYCODES; i++) {
			if (di_keycodes[j][i]) {
#if DEBUG_SCANCODE
				write_log(_T("release %d:%02x:%02x\n"), j, di_keycodes[j][i], i);
#endif
				di_keycodes[j][i] = 0;
				my_kbd_handler(j, i, 0, true);
			}
		}
	}
	memset (rawkeystate, 0, sizeof rawkeystate);
	rawprevkey = -1;
	key_lwin = key_lshift = 0;
}

static void flushmsgpump (void)
{
	MSG msg;
	while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
}

static int acquire_kb (int num, int flags)
{
	if (num < 0) {
		flushmsgpump();
		if (currprefs.keyboard_leds_in_use) {
			//write_log (_T("***********************acquire_kb_led\n"));
			if (!currprefs.win32_kbledmode) {
				if (DefineDosDevice (DDD_RAW_TARGET_PATH, _T("Kbd"), _T("\\Device\\KeyboardClass0"))) {
					kbhandle = CreateFile (_T("\\\\.\\Kbd"), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
					if (kbhandle == INVALID_HANDLE_VALUE) {
						write_log (_T("kbled: CreateFile failed, error %d\n"), GetLastError());
						currprefs.win32_kbledmode = 1;
					}
				} else {
					currprefs.win32_kbledmode = 1;
					write_log (_T("kbled: DefineDosDevice failed, error %d\n"), GetLastError());
				}
			}
			oldleds = get_leds ();
			if (originalleds == -1) {
				originalleds = oldleds;
				//write_log (_T("stored %08x -> %08x\n"), originalleds, newleds);
			}
			set_leds (newleds);
		}
		return 1;
	}

	struct didata *did = &di_keyboard[num];

	if (did->connection == DIDC_NONE)
		return 0;

	LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;

	unacquire (lpdi, _T("keyboard"));
	did->acquireattempts = MAX_ACQUIRE_ATTEMPTS;

	//lock_kb ();
	setcoop (did, DISCL_NOWINKEY | DISCL_FOREGROUND | DISCL_EXCLUSIVE, _T("keyboard"));
	kb_do_refresh = ~0;
	did->acquired = -1;
	if (acquire (lpdi, _T("keyboard"))) {
		if (did->rawinput)
			rawkb++;
		else if (did->superdevice)
			superkb++;
		else
			normalkb++;
		did->acquired = 1;
	}
	return did->acquired > 0 ? 1 : 0;
}

static void unacquire_kb (int num)
{
	if (num < 0) {
		if (currprefs.keyboard_leds_in_use) {
			//write_log (_T("*********************** unacquire_kb_led\n"));
			if (originalleds != -1) {
				//write_log (_T("restored %08x -> %08x\n"), oldleds, originalleds);
				set_leds (originalleds);
				originalleds = -1;
				flushmsgpump ();
			}
			if (kbhandle != INVALID_HANDLE_VALUE) {
				CloseHandle (kbhandle);
				DefineDosDevice (DDD_REMOVE_DEFINITION, _T("Kbd"), NULL);
				kbhandle = INVALID_HANDLE_VALUE;
			}
		}
		return;
	}

	struct didata *did = &di_keyboard[num];

	if (did->connection == DIDC_NONE)
		return;

	LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;

	unacquire (lpdi, _T("keyboard"));
	if (did->acquired > 0) {
		if (did->rawinput)
			rawkb--;
		else if (did->superdevice)
			superkb--;
		else
			normalkb--;
		did->acquired = 0;
	}
}

static int refresh_kb (LPDIRECTINPUTDEVICE8 lpdi, int num)
{
	HRESULT hr;
	int i;
	uae_u8 kc[256];

	hr = IDirectInputDevice8_GetDeviceState (lpdi, sizeof (kc), kc);
	if (SUCCEEDED (hr)) {
		for (i = 0; i < sizeof (kc); i++) {
			if (i == 0x80) /* USB KB led causes this, better ignore it */
				continue;
			if (kc[i] & 0x80)
				kc[i] = 1;
			else
				kc[i] = 0;
			if (kc[i] != (di_keycodes[num][i] & 1)) {
				write_log (_T("%d: %02X -> %d\n"), num, i, kc[i]);
				di_keycodes[num][i] = kc[i];
				my_kbd_handler (num, i, kc[i], true);
			}
		}
	} else if (hr == DIERR_INPUTLOST) {
		acquire_kb (num, 0);
		IDirectInputDevice8_Poll (lpdi);
		return 0;
	}
	IDirectInputDevice8_Poll (lpdi);
	return 1;
}

static void read_kb (void)
{
	DIDEVICEOBJECTDATA didod[DI_KBBUFFER];
	DWORD elements;
	HRESULT hr;
	LPDIRECTINPUTDEVICE8 lpdi;
	int i, j;
	int istest = inputdevice_istest ();

	if (IGNOREEVERYTHING)
		return;

	if (originalleds != -1)
		update_leds ();
	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		struct didata *did = &di_keyboard[i];
		if (!did->acquired)
			continue;
		if (istest == 0 && isfocus () == 0) {
			if (did->acquired > 0)
				unacquire_kb (i);
			continue;
		}
		lpdi = did->lpdi;
		if (!lpdi)
			continue;
		if (kb_do_refresh & (1 << i)) {
			if (!refresh_kb (lpdi, i))
				continue;
			kb_do_refresh &= ~(1 << i);
		}
		if (did->acquireattempts <= 0)
			continue;
		elements = DI_KBBUFFER;
		hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
		if ((SUCCEEDED (hr) || hr == DI_BUFFEROVERFLOW) && (isfocus () || istest)) {
			for (j = 0; j < elements; j++) {
				int scancode = didod[j].dwOfs;
				int pressed = (didod[j].dwData & 0x80) ? 1 : 0;

				//write_log (_T("%d: %02X %d\n"), j, scancode, pressed);
				if (currprefs.right_control_is_right_win_key && scancode == DIK_RCONTROL)
					scancode = DIK_RWIN;
				if (!istest)
					scancode = keyhack (scancode, pressed, i);
				if (scancode < 0)
					continue;
				if (pressed) {
					di_keycodes[i][scancode] = 1;
				} else {
					if ((di_keycodes[i][scancode] & 1) && pause_emulation) {
						di_keycodes[i][scancode] = 2;
					} else {
						di_keycodes[i][scancode] = 0;
					}
				}
				if (istest) {
					if (pressed && (scancode == DIK_F12))
						return;
					if (scancode == DIK_F12) {
						inputdevice_testrecord (IDTYPE_KEYBOARD, i, IDEV_WIDGET_BUTTON, 0x101, 1, -1);
						inputdevice_testrecord (IDTYPE_KEYBOARD, i, IDEV_WIDGET_BUTTON, 0x101, 0, -1);
					} else {
						inputdevice_testrecord (IDTYPE_KEYBOARD, i, IDEV_WIDGET_BUTTON, scancode, pressed, -1);
					}
				} else {
					if (stopoutput == 0)
						my_kbd_handler (i, scancode, pressed, false);
				}
			}
		} else if (hr == DIERR_INPUTLOST) {
			if (!acquire_kb (i, 0))
				did->acquireattempts--;
			kb_do_refresh |= 1 << i;
		} else if (did->acquired && hr == DIERR_NOTACQUIRED) {
			if (!acquire_kb (i, 0))
				did->acquireattempts--;
		}
		IDirectInputDevice8_Poll (lpdi);
	}
#ifdef CATWEASEL
	if (isfocus() || istest) {
		uae_u8 kc;
		if (stopoutput == 0 && catweasel_read_keyboard (&kc))
			inputdevice_do_keyboard (kc & 0x7f, kc & 0x80);
	}
#endif
}

void wait_keyrelease (void)
{
	stopoutput++;

#if 0
	int i, j, maxcount = 10, found;
	while (maxcount-- > 0) {
		read_kb ();
		found = 0;
		for (j = 0; j < MAX_INPUT_DEVICES; j++) {
			for (i = 0; i < MAX_KEYCODES; i++) {
				if (di_keycodes[j][i])
					found = 1;
			}
		}
		if (!found)
			break;
		sleep_millis (10);
	}
#endif
	release_keys ();
#if 0
	for (;;) {
		int ok = 0, nok = 0;
		for (i = 0; i < MAX_INPUT_DEVICES; i++) {
			struct didata *did = &di_mouse[i];
			DIMOUSESTATE dis;
			LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;
			HRESULT hr;
			int j;

			if (!lpdi)
				continue;
			nok++;
			hr = IDirectInputDevice8_GetDeviceState (lpdi, sizeof (dis), &dis);
			if (SUCCEEDED (hr)) {
				for (j = 0; j < 4; j++) {
					if (dis.rgbButtons[j] & 0x80) break;
				}
				if (j == 4) ok++;
			} else {
				ok++;
			}
		}
		if (ok == nok) break;
		sleep_millis (10);
	}
#endif
	stopoutput--;
}

static int get_kb_flags (int num)
{
	return 0;
}

struct inputdevice_functions inputdevicefunc_keyboard = {
	init_kb, close_kb, acquire_kb, unacquire_kb, read_kb,
	get_kb_num, get_kb_friendlyname, get_kb_uniquename,
	get_kb_widget_num, get_kb_widget_type,
	get_kb_widget_first,
	get_kb_flags
};


static int get_joystick_num (void)
{
	return num_joystick;
}

static int get_joystick_widget_num (int joy)
{
	return di_joystick[joy].axles + di_joystick[joy].buttons;
}

static int get_joystick_widget_type (int joy, int num, TCHAR *name, uae_u32 *code)
{
	struct didata *did = &di_joystick[joy];
	if (num >= did->axles + did->buttons_real && num < did->axles + did->buttons) {
		if (name)
			_tcscpy (name, did->buttonname[num - did->axles]);
		return IDEV_WIDGET_BUTTONAXIS;
	} else if (num >= did->axles && num < did->axles + did->buttons_real) {
		if (name)
			_tcscpy (name, did->buttonname[num - did->axles]);
		return IDEV_WIDGET_BUTTON;
	} else if (num < di_joystick[joy].axles) {
		if (name)
			_tcscpy (name, did->axisname[num]);
		return IDEV_WIDGET_AXIS;
	}
	return IDEV_WIDGET_NONE;
}

static int get_joystick_widget_first (int joy, int type)
{
	switch (type)
	{
	case IDEV_WIDGET_BUTTON:
		return di_joystick[joy].axles;
	case IDEV_WIDGET_AXIS:
		return 0;
	case IDEV_WIDGET_BUTTONAXIS:
		return di_joystick[joy].axles + di_joystick[joy].buttons_real;
	}
	return -1;
}

static TCHAR *get_joystick_friendlyname (int joy)
{
	return di_joystick[joy].name;
}


static TCHAR *get_joystick_uniquename (int joy)
{
	return di_joystick[joy].configname;
}

static void read_joystick (void)
{
	DIDEVICEOBJECTDATA didod[DI_BUFFER];
	LPDIRECTINPUTDEVICE8 lpdi;
	DWORD elements;
	HRESULT hr;
	int i, j, k;
	int istest = inputdevice_istest ();

	if (IGNOREEVERYTHING)
		return;
#ifdef RETROPLATFORM
	if (rp_isactive ())
		return;
#endif
	if (!istest && !mouseactive && !(currprefs.win32_active_input & 4)) {
		return;
	}

	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		struct didata *did = &di_joystick[i];
		if (!did->acquired)
			continue;
		if (did->connection == DIDC_CAT) {
			if (getjoystickstate (i) && (isfocus () || istest)) {
				/* only read CW state if it is really needed */
				uae_u8 cdir, cbuttons;
				if (catweasel_read_joystick (&cdir, &cbuttons)) {
					cdir >>= did->catweasel * 4;
					cbuttons >>= did->catweasel * 4;
					setjoystickstate (i, 0, !(cdir & 1) ? 1 : !(cdir & 2) ? -1 : 0, 0);
					setjoystickstate (i, 1, !(cdir & 4) ? 1 : !(cdir & 8) ? -1 : 0, 0);
					setjoybuttonstate (i, 0, cbuttons & 8);
					setjoybuttonstate (i, 1, cbuttons & 4);
					setjoybuttonstate (i, 2, cbuttons & 2);
				}
			}
			continue;
		} else if (did->connection == DIDC_PARJOY) {
			DWORD ret;
			PAR_QUERY_INFORMATION inf;
			ret = 0;
			if (DeviceIoControl (did->parjoy, IOCTL_PAR_QUERY_INFORMATION, NULL, 0, &inf, sizeof inf, &ret, NULL)) {
				write_log (_T("PARJOY: IOCTL_PAR_QUERY_INFORMATION = %u\n"), GetLastError ());
			} else {
				if (inf.Status != did->oldparjoystatus.Status) {
					write_log (_T("PARJOY: %08x\n"), inf.Status);
					did->oldparjoystatus.Status = inf.Status;
				}
			}
			continue;
#if 0
		} else if (did->connection == DIDC_RAW) {
			bool changedreport = true;
			while ((isfocus () || istest) && changedreport) {
				DWORD readbytes;
				changedreport = false;
				if (!did->hidread) {
					DWORD ret;
					DWORD readlen = did->hidcaps.InputReportByteLength;
					did->hidbuffer[0] = 0;
					ret = ReadFile (did->hidhandle, did->hidbuffer, readlen, NULL, &did->hidol);
					if (ret || (ret == 0 && GetLastError () == ERROR_IO_PENDING))
						did->hidread = true;
				}
				if (did->hidread && GetOverlappedResult (did->hidhandle, &did->hidol, &readbytes, FALSE)) {
					did->hidread = false;
					changedreport = memcmp (did->hidbuffer, did->hidbufferprev, readbytes) != 0;
					memcpy (did->hidbufferprev, did->hidbuffer, readbytes);
					DWORD usagelength = did->maxusagelistlength;
					if (changedreport)
						write_log (_T("%02x%02x%02x%02x%02x%02x%02x\n"),
							(uae_u8)did->hidbuffer[0], (uae_u8)did->hidbuffer[1], (uae_u8)did->hidbuffer[2], (uae_u8)did->hidbuffer[3], (uae_u8)did->hidbuffer[4], (uae_u8)did->hidbuffer[5], (uae_u8)did->hidbuffer[6]);
					if (HidP_GetUsagesEx (HidP_Input, 0, did->usagelist, &usagelength, did->hidpreparseddata, did->hidbuffer, readbytes) == HIDP_STATUS_SUCCESS) {
						for (k = 0; k < usagelength; k++) {
							for (j = 0; j < did->maxusagelistlength; j++) {
								if (did->usagelist[k].UsagePage == did->prevusagelist[j].UsagePage &&
									did->usagelist[k].Usage == did->prevusagelist[j].Usage)
									break;
							}
							if (j == did->maxusagelistlength || did->prevusagelist[j].Usage == 0) {
								write_log (_T("%d/%d ON\n"), did->usagelist[k].UsagePage, did->usagelist[k].Usage);
							} else {
								did->prevusagelist[j].Usage = 0;
							}
						}
						for (j = 0; j < did->maxusagelistlength; j++) {
							if (did->prevusagelist[j].Usage) {
								write_log (_T("%d/%d OFF\n"), did->prevusagelist[j].UsagePage, did->prevusagelist[j].Usage);
							}
						}
						memcpy (did->prevusagelist, did->usagelist, usagelength * sizeof(USAGE_AND_PAGE));
						memset (did->prevusagelist + usagelength, 0, (did->maxusagelistlength - usagelength) * sizeof(USAGE_AND_PAGE));
					}
				}
			}
#endif
		}

		lpdi = did->lpdi;
		if (!lpdi || did->connection != DIDC_DX)
			continue;
		if (did->acquireattempts <= 0)
			continue;
		elements = DI_BUFFER;
		hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
		if ((SUCCEEDED (hr) || hr == DI_BUFFEROVERFLOW) && (isfocus () || istest || (currprefs.win32_inactive_input & 4))) {
			for (j = 0; j < elements; j++) {
				int dimofs = didod[j].dwOfs;
				int data = didod[j].dwData;
				int data2 = data;
				int state = (data & 0x80) ? 1 : 0;
				data -= 32768;

				for (k = 0; k < did->buttons; k++) {

					if (did->buttonaxisparent[k] >= 0 && did->buttonmappings[k] == dimofs) {

						int bstate = -1;
						int axis = did->buttonaxisparent[k];
						int dir = did->buttonaxisparentdir[k];

						if (did->axistype[axis] == 1) {
							if (!dir)
								bstate = (data2 >= 20250 && data2 <= 33750) ? 1 : 0;
							else
								bstate = (data2 >= 2250 && data2 <= 15750) ? 1 : 0;
						} else if (did->axistype[axis] == 2) {
							if (!dir)
								bstate = ((data2 >= 29250 && data2 <= 33750) || (data2 >= 0 && data2 <= 6750))  ? 1 : 0;
							else
								bstate = (data2 >= 11250 && data2 <= 24750) ? 1 : 0;
						} else if (did->axistype[axis] == 0) {
							if (dir)
								bstate = data > 20000 ? 1 : 0;
							else
								bstate = data < -20000 ? 1 : 0;
						}
						if (bstate >= 0 && axisold[i][k] != bstate) {
							setjoybuttonstate (i, k, bstate);
							axisold[i][k] = bstate;
							if (rawinput_log & 8)
								write_log (_T("AB:NUM=%d OFF=%d AXIS=%d DIR=%d NAME=%s VAL=%d STATE=%d BS=%d\n"),
									k, dimofs, axis, dir, did->buttonname[k], data, state, bstate);

						}


					} else if (did->buttonaxisparent[k] < 0 && did->buttonmappings[k] == dimofs) {
						if (rawinput_log & 8)
							write_log (_T("B:NUM=%d OFF=%d NAME=%s VAL=%d STATE=%d\n"),
								k, dimofs, did->buttonname[k], data, state);
						setjoybuttonstate (i, k, state);
					}
				}

				for (k = 0; k < did->axles; k++) {
					if (did->axismappings[k] == dimofs) {
						if (did->axistype[k] == 1) {
							setjoystickstate (i, k, (data2 >= 20250 && data2 <= 33750) ? -1 : (data2 >= 2250 && data2 <= 15750) ? 1 : 0, 1);
						} else if (did->axistype[k] == 2) {
							setjoystickstate (i, k, ((data2 >= 29250 && data2 <= 33750) || (data2 >= 0 && data2 <= 6750)) ? -1 : (data2 >= 11250 && data2 <= 24750) ? 1 : 0, 1);
							if (rawinput_log & 8)
								write_log (_T("P:NUM=%d OFF=%d NAME=%s VAL=%d\n"), k, dimofs, did->axisname[k], data2);
						} else if (did->axistype[k] == 0) {
							if (rawinput_log & 8) {
								if (data < -20000 || data > 20000)
									write_log (_T("A:NUM=%d OFF=%d NAME=%s VAL=%d\n"), k, dimofs, did->axisname[k], data);
							}
							if (istest) {
								if (data < -20000)
									data = -20000;
								else if (data > 20000)
									data = 20000;
								else
									data = 0;
							}
							if (axisold[i][k] != data) {
								setjoystickstate (i, k, data, 32767);
								axisold[i][k] = data;
							}
						}
					}
				}

			}

		} else if (hr == DIERR_INPUTLOST) {
			if (!acquire (lpdi, _T("joystick")))
				did->acquireattempts--;
		} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
			if (!acquire (lpdi, _T("joystick")))
				did->acquireattempts--;
		}
		IDirectInputDevice8_Poll (lpdi);
	}
}

static int init_joystick (void)
{
	int i;
	LPDIRECTINPUTDEVICE8 lpdi;
	HRESULT hr;
	struct didata *did;

	if (joystick_inited)
		return 1;
	di_init ();
	joystick_inited = 1;
	for (i = 0; i < num_joystick; i++) {
		did = &di_joystick[i];
		if (did->connection == DIDC_DX) {
			hr = g_lpdi->CreateDevice (did->iguid, &lpdi, NULL);
			if (SUCCEEDED (hr)) {
				hr = lpdi->SetDataFormat (&c_dfDIJoystick);
				if (SUCCEEDED (hr)) {
					did->lpdi = lpdi;
					lpdi->EnumObjects (EnumObjectsCallback, (void*)did, DIDFT_ALL);
					fixbuttons (did);
					fixthings (did);
					sortobjects (did);
				}
			} else {
				write_log (_T("joystick createdevice failed, %s\n"), DXError (hr));
			}
		}
	}
	return 1;
}

static void close_joystick (void)
{
	int i;

	if (!joystick_inited)
		return;
	joystick_inited = 0;
	for (i = 0; i < num_joystick; i++)
		di_dev_free (&di_joystick[i]);
	di_free ();
}

void dinput_window (void)
{
	int i;
	for (i = 0; i < num_joystick; i++)
		di_joystick[i].coop = 0;
	for (i = 0; i < num_mouse; i++)
		di_mouse[i].coop = 0;
	for (i = 0; i < num_keyboard; i++)
		di_keyboard[i].coop = 0;
}

static int acquire_joystick (int num, int flags)
{
	if (num < 0) {
		return 1;
	}

	struct didata *did = &di_joystick[num];

	if (did->connection == DIDC_NONE)
		return 0;

	LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;
	unacquire (lpdi, _T("joystick"));
	did->acquireattempts = MAX_ACQUIRE_ATTEMPTS;
	if (did->connection == DIDC_DX && lpdi) {
		DIPROPDWORD dipdw;
		HRESULT hr;

		setcoop (did, flags ? (DISCL_FOREGROUND | DISCL_EXCLUSIVE) : (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE), _T("joystick"));
		memset (&dipdw, 0, sizeof (dipdw));
		dipdw.diph.dwSize = sizeof (DIPROPDWORD);
		dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
		dipdw.diph.dwObj = 0;
		dipdw.diph.dwHow = DIPH_DEVICE;
		dipdw.dwData = DI_BUFFER;
		hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
		if (FAILED (hr))
			write_log (_T("joystick setproperty failed, %s\n"), DXError (hr));
		did->acquired = acquire (lpdi, _T("joystick")) ? 1 : -1;
	} else if (did->connection == DIDC_RAW) {
		did->acquired = 1;
	} else {
		did->acquired = 1;
	}
	return did->acquired > 0 ? 1 : 0;
}

static void unacquire_joystick (int num)
{
	if (num < 0) {
		return;
	}

	struct didata *did = &di_joystick[num];

	if (did->connection == DIDC_NONE)
		return;

	unacquire (did->lpdi, _T("joystick"));
	did->acquired = 0;
}

static int get_joystick_flags (int num)
{
	return 0;
}

struct inputdevice_functions inputdevicefunc_joystick = {
	init_joystick, close_joystick, acquire_joystick, unacquire_joystick,
	read_joystick, get_joystick_num, get_joystick_friendlyname, get_joystick_uniquename,
	get_joystick_widget_num, get_joystick_widget_type,
	get_joystick_widget_first,
	get_joystick_flags
};

int dinput_wmkey (uae_u32 key)
{
	if (normalkb || superkb || rawkb)
		return 0;
	if (((key >> 16) & 0xff) == 0x58)
		return 1;
	return 0;
}

int input_get_default_keyboard (int i)
{
	if (rawinput_enabled_keyboard) {
		if (i < 0)
			return 0;
		if (i >= num_keyboard)
			return 0;
		struct didata *did = &di_keyboard[i];
		if (did->connection == DIDC_RAW && !did->rawinput)
			return 1;
	} else {
		if (i < 0)
			return 0;
		if (i == 0)
			return 1;
	}
	return 0;
}

static int nextsub(struct uae_input_device *uid, int i, int slot, int sub)
{
	if (currprefs.input_advancedmultiinput) {
		while (uid[i].eventid[slot][sub] > 0) {
			sub++;
			if (sub >= MAX_INPUT_SUB_EVENT) {
				return -1;
			}
		}
	}
	return sub;
}

static bool isemptyslot(struct uae_input_device *uid, int i, int slot, int sub, int port)
{
	return uid[i].eventid[slot][sub] == 0;
}

static void setid (struct uae_input_device *uid, int i, int slot, int sub, int port, int evt, bool gp)
{
	sub = nextsub(uid, i, slot, sub);
	if (sub < 0) {
		return;
	}
	if (gp && sub == 0) {
		inputdevice_sparecopy (&uid[i], slot, sub);
	}
	uid[i].eventid[slot][sub] = evt;
	uid[i].port[slot][sub] = port + 1;
}
static void setid (struct uae_input_device *uid, int i, int slot, int sub, int port, int evt, int af, bool gp)
{
	sub = nextsub(uid, i, slot, sub);
	if (sub < 0) {
		return;
	}
	setid (uid, i, slot, sub, port, evt, gp);
	uid[i].flags[slot][sub] &= ~ID_FLAG_AUTOFIRE_MASK;
	if (af >= JPORT_AF_NORMAL)
		uid[i].flags[slot][sub] |= ID_FLAG_AUTOFIRE;
	if (af == JPORT_AF_TOGGLE)
		uid[i].flags[slot][sub] |= ID_FLAG_TOGGLE;
	if (af == JPORT_AF_ALWAYS)
		uid[i].flags[slot][sub] |= ID_FLAG_INVERTTOGGLE;
	if (af == JPORT_AF_TOGGLENOAF)
		uid[i].flags[slot][sub] |= ID_FLAG_INVERT;
}

int input_get_default_mouse (struct uae_input_device *uid, int i, int port, int af, bool gp, bool wheel, bool joymouseswap)
{
	struct didata *did = NULL;

	if (!joymouseswap) {
		if (i >= num_mouse)
			return 0;
		did = &di_mouse[i];
	} else {
		if (i >= num_joystick)
			return 0;
		did = &di_joystick[i];
	}
	setid (uid, i, ID_AXIS_OFFSET + 0, 0, port, port ? INPUTEVENT_MOUSE2_HORIZ : INPUTEVENT_MOUSE1_HORIZ, gp);
	setid (uid, i, ID_AXIS_OFFSET + 1, 0, port, port ? INPUTEVENT_MOUSE2_VERT : INPUTEVENT_MOUSE1_VERT, gp);
	if (wheel)
		setid (uid, i, ID_AXIS_OFFSET + 2, 0, port, port ? 0 : INPUTEVENT_MOUSE1_WHEEL, gp);
	setid (uid, i, ID_BUTTON_OFFSET + 0, 0, port, port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON, af, gp);
	setid (uid, i, ID_BUTTON_OFFSET + 1, 0, port, port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON, gp);
	setid (uid, i, ID_BUTTON_OFFSET + 2, 0, port, port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON, gp);
	if (wheel && port == 0) { /* map back and forward to ALT+LCUR and ALT+RCUR */
		if (isrealbutton (did, 3)) {
			setid (uid, i, ID_BUTTON_OFFSET + 3, 0, port, INPUTEVENT_KEY_ALT_LEFT, gp);
			setid (uid, i, ID_BUTTON_OFFSET + 3, 1, port, INPUTEVENT_KEY_CURSOR_LEFT, gp);
			if (isrealbutton (did, 4)) {
				setid (uid, i, ID_BUTTON_OFFSET + 4, 0, port, INPUTEVENT_KEY_ALT_LEFT, gp);
				setid (uid, i, ID_BUTTON_OFFSET + 4, 1, port, INPUTEVENT_KEY_CURSOR_RIGHT, gp);
			}
		}
	}
	if (i == 0)
		return 1;
	return 0;
}

int input_get_default_lightpen (struct uae_input_device *uid, int i, int port, int af, bool gp, bool joymouseswap, int submode)
{
	struct didata *did = NULL;

	if (!joymouseswap) {
		if (i >= num_mouse)
			return 0;
		did = &di_mouse[i];
	} else {
		if (i >= num_joystick)
			return 0;
		did = &di_joystick[i];
	}
	setid (uid, i, ID_AXIS_OFFSET + 0, 0, port, INPUTEVENT_LIGHTPEN_HORIZ, gp);
	setid (uid, i, ID_AXIS_OFFSET + 1, 0, port, INPUTEVENT_LIGHTPEN_VERT, gp);
	int button = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
	switch (submode)
	{
	case 1:
		button = port ? INPUTEVENT_JOY2_LEFT : INPUTEVENT_JOY1_LEFT;
		break;
	}
	setid (uid, i, ID_BUTTON_OFFSET + 0, 0, port, button, gp);
	if (i == 0)
		return 1;
	return 0;
}

int input_get_default_joystick (struct uae_input_device *uid, int i, int port, int af, int mode, bool gp, bool joymouseswap, bool default_osk)
{
	int j;
	struct didata *did = NULL;
	int h, v;

	if (joymouseswap) {
		if (i >= num_mouse)
			return 0;
		did = &di_mouse[i];
	} else {
		if (i >= num_joystick)
			return 0;
		did = &di_joystick[i];
	}
	if (mode == JSEM_MODE_MOUSE_CDTV) {
		h = INPUTEVENT_MOUSE_CDTV_HORIZ;
		v = INPUTEVENT_MOUSE_CDTV_VERT;
	} else if (port >= 2) {
		h = port == 3 ? INPUTEVENT_PAR_JOY2_HORIZ : INPUTEVENT_PAR_JOY1_HORIZ;
		v = port == 3 ? INPUTEVENT_PAR_JOY2_VERT : INPUTEVENT_PAR_JOY1_VERT;
	} else {
		h = port ? INPUTEVENT_JOY2_HORIZ : INPUTEVENT_JOY1_HORIZ;;
		v = port ? INPUTEVENT_JOY2_VERT : INPUTEVENT_JOY1_VERT;
	}
	setid (uid, i, ID_AXIS_OFFSET + 0, 0, port, h, gp);
	setid (uid, i, ID_AXIS_OFFSET + 1, 0, port, v, gp);

	if (port >= 2) {
		setid (uid, i, ID_BUTTON_OFFSET + 0, 0, port, port == 3 ? INPUTEVENT_PAR_JOY2_FIRE_BUTTON : INPUTEVENT_PAR_JOY1_FIRE_BUTTON, af, gp);
	} else {
		setid (uid, i, ID_BUTTON_OFFSET + 0, 0, port, port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON, af, gp);
		if (isrealbutton (did, 1))
			setid (uid, i, ID_BUTTON_OFFSET + 1, 0, port, port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON, gp);
		if (mode != JSEM_MODE_JOYSTICK) {
			if (isrealbutton (did, 2))
				setid (uid, i, ID_BUTTON_OFFSET + 2, 0, port, port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON, gp);
		}
		if (isrealbutton(did, 3) && isemptyslot(uid, i, ID_BUTTON_OFFSET + 3, 0, port) && default_osk) {
			setid(uid, i, ID_BUTTON_OFFSET + 3, 0, port, INPUTEVENT_SPC_OSK, gp);
		}
	}

	for (j = 2; j < MAX_MAPPINGS - 1; j++) {
		int type = did->axistype[j];
		if (type == AXISTYPE_POV_X) {
			setid (uid, i, ID_AXIS_OFFSET + j + 0, 0, port, h, gp);
			setid (uid, i, ID_AXIS_OFFSET + j + 1, 0, port, v, gp);
			j++;
		}
	}

	if (mode == JSEM_MODE_JOYSTICK_CD32) {
		setid (uid, i, ID_BUTTON_OFFSET + 0, 0, port, port ? INPUTEVENT_JOY2_CD32_RED : INPUTEVENT_JOY1_CD32_RED, af, gp);
		if (isrealbutton (did, 1)) {
			setid (uid, i, ID_BUTTON_OFFSET + 1, 0, port, port ? INPUTEVENT_JOY2_CD32_BLUE : INPUTEVENT_JOY1_CD32_BLUE, gp);
		}
		if (isrealbutton (did, 2))
			setid (uid, i, ID_BUTTON_OFFSET + 2, 0, port, port ? INPUTEVENT_JOY2_CD32_GREEN : INPUTEVENT_JOY1_CD32_GREEN, gp);
		if (isrealbutton (did, 3))
			setid (uid, i, ID_BUTTON_OFFSET + 3, 0, port, port ? INPUTEVENT_JOY2_CD32_YELLOW : INPUTEVENT_JOY1_CD32_YELLOW, gp);
		if (isrealbutton (did, 4))
			setid (uid, i, ID_BUTTON_OFFSET + 4, 0, port, port ? INPUTEVENT_JOY2_CD32_RWD : INPUTEVENT_JOY1_CD32_RWD, gp);
		if (isrealbutton (did, 5))
			setid (uid, i, ID_BUTTON_OFFSET + 5, 0, port, port ? INPUTEVENT_JOY2_CD32_FFW : INPUTEVENT_JOY1_CD32_FFW, gp);
		if (isrealbutton (did, 6))
			setid (uid, i, ID_BUTTON_OFFSET + 6, 0, port, port ? INPUTEVENT_JOY2_CD32_PLAY :  INPUTEVENT_JOY1_CD32_PLAY, gp);
	}
	if (i == 0)
		return 1;
	return 0;
}

int input_get_default_joystick_analog (struct uae_input_device *uid, int i, int port, int af, bool gp, bool joymouseswap, bool default_osk)
{
	int j;
	struct didata *did;

	if (joymouseswap) {
		if (i >= num_mouse)
			return 0;
		did = &di_mouse[i];
	} else {
		if (i >= num_joystick)
			return 0;
		did = &di_joystick[i];
	}
	setid (uid, i, ID_AXIS_OFFSET + 0, 0, port, port ? INPUTEVENT_JOY2_HORIZ_POT : INPUTEVENT_JOY1_HORIZ_POT, gp);
	setid (uid, i, ID_AXIS_OFFSET + 1, 0, port, port ? INPUTEVENT_JOY2_VERT_POT : INPUTEVENT_JOY1_VERT_POT, gp);
	setid (uid, i, ID_BUTTON_OFFSET + 0, 0, port, port ? INPUTEVENT_JOY2_LEFT : INPUTEVENT_JOY1_LEFT, af, gp);
	if (isrealbutton(did, 1))
		setid(uid, i, ID_BUTTON_OFFSET + 1, 0, port, port ? INPUTEVENT_JOY2_RIGHT : INPUTEVENT_JOY1_RIGHT, gp);
	if (isrealbutton(did, 2))
		setid(uid, i, ID_BUTTON_OFFSET + 2, 0, port, port ? INPUTEVENT_JOY2_UP : INPUTEVENT_JOY1_UP, gp);
	if (isrealbutton(did, 3))
		setid(uid, i, ID_BUTTON_OFFSET + 3, 0, port, port ? INPUTEVENT_JOY2_DOWN : INPUTEVENT_JOY1_DOWN, gp);
	if (isrealbutton(did, 4) && isemptyslot(uid, i, ID_BUTTON_OFFSET + 4, 0, port) && default_osk)
		setid(uid, i, ID_BUTTON_OFFSET + 4, 0, port, INPUTEVENT_SPC_OSK, gp);

	for (j = 2; j < MAX_MAPPINGS - 1; j++) {
		int type = did->axistype[j];
		if (type == AXISTYPE_POV_X) {
			setid (uid, i, ID_AXIS_OFFSET + j + 0, 0, port, port ? INPUTEVENT_JOY2_HORIZ_POT : INPUTEVENT_JOY1_HORIZ_POT, gp);
			setid (uid, i, ID_AXIS_OFFSET + j + 1, 0, port, port ? INPUTEVENT_JOY2_VERT_POT : INPUTEVENT_JOY1_VERT_POT, gp);
			j++;
		}
	}

	if (i == 0)
		return 1;
	return 0;
}

#ifdef RETROPLATFORM
#include "cloanto/RetroPlatformIPC.h"
int rp_input_enum (struct RPInputDeviceDescription *desc, int index)
{
	memset (desc, 0, sizeof (struct RPInputDeviceDescription));
	if (index < num_joystick) {
		struct didata *did = &di_joystick[index];
		_tcscpy (desc->szHostInputName, did->name);
		_tcscpy (desc->szHostInputID, did->configname);
		desc->dwHostInputType = RP_HOSTINPUT_JOYSTICK;
		desc->dwInputDeviceFeatures = RP_FEATURE_INPUTDEVICE_JOYSTICK;
		if (did->buttons > 2)
			desc->dwInputDeviceFeatures |= RP_FEATURE_INPUTDEVICE_GAMEPAD;
		if (did->buttons >= 7)
			desc->dwInputDeviceFeatures |= RP_FEATURE_INPUTDEVICE_JOYPAD;
		desc->dwHostInputProductID = did->pid;
		desc->dwHostInputVendorID = did->vid;
		if (did->analogstick)
			desc->dwInputDeviceFeatures |= RP_FEATURE_INPUTDEVICE_ANALOGSTICK;
		return index + 1;
	} else if (index < num_joystick + num_mouse) {
		struct didata *did = &di_mouse[index - num_joystick];
		_tcscpy (desc->szHostInputName, did->name);
		_tcscpy (desc->szHostInputID, did->configname);
		desc->dwHostInputType = RP_HOSTINPUT_MOUSE;
		desc->dwInputDeviceFeatures = RP_FEATURE_INPUTDEVICE_MOUSE;
		desc->dwInputDeviceFeatures |= RP_FEATURE_INPUTDEVICE_LIGHTPEN;
		desc->dwHostInputProductID = did->pid;
		desc->dwHostInputVendorID = did->vid;
		if (did->wininput)
			desc->dwFlags |= RP_HOSTINPUTFLAGS_MOUSE_SMART;
		return index + 1;
	}
	return -1;
}
#endif
