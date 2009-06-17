/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 DirectInput/Windows XP RAWINPUT interface
 *
 * Copyright 2002 - 2004 Toni Wilen
 */

#define _WIN32_WINNT 0x501 /* enable RAWINPUT support */

#define DI_DEBUG
//#define DI_DEBUG2
//#define DI_DEBUG_RAWINPUT
#define IGNOREEVERYTHING 0

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include <windows.h>
#include <dinput.h>

#include "sysdeps.h"
#include "options.h"
#include "rp.h"
#include "inputdevice.h"
#include "keybuf.h"
#include "xwin.h"
#include "uae.h"
#include "catweasel.h"
#include "keyboard.h"
#include "custom.h"
#include "dxwrap.h"
#include "akiko.h"

#ifdef WINDDK
#include <winioctl.h>
#include <ntddkbd.h>
#endif
#include <setupapi.h>
#include <devguid.h>

#include <wintab.h>
#include "wintablet.h"

#include "win32.h"

#define MAX_MAPPINGS 256

#define DID_MOUSE 1
#define DID_JOYSTICK 2
#define DID_KEYBOARD 3

#define DIDC_DX 1
#define DIDC_RAW 2
#define DIDC_WIN 3
#define DIDC_CAT 4

struct didata {
    int type;
    int acquired;
    int priority;
    int superdevice;
    GUID iguid;
    GUID pguid;
    TCHAR *name;
    TCHAR *sortname;
    TCHAR *configname;

    int connection;
    LPDIRECTINPUTDEVICE8 lpdi;
    HANDLE rawinput;
    int wininput;
    int catweasel;
    int coop;

    int axles;
    int buttons, buttons_real;
    int axismappings[MAX_MAPPINGS];
    TCHAR *axisname[MAX_MAPPINGS];
    int axissort[MAX_MAPPINGS];
    int axistype[MAX_MAPPINGS];
    int buttonmappings[MAX_MAPPINGS];
    TCHAR *buttonname[MAX_MAPPINGS];
    int buttonsort[MAX_MAPPINGS];

    int axisparent[MAX_MAPPINGS];
    int axisparentdir[MAX_MAPPINGS];
};

#define DI_BUFFER 30
#define DI_KBBUFFER 50

static struct didata di_mouse[MAX_INPUT_DEVICES];
static struct didata di_keyboard[MAX_INPUT_DEVICES];
static struct didata di_joystick[MAX_INPUT_DEVICES];
static int num_mouse, num_keyboard, num_joystick;
static int dd_inited, mouse_inited, keyboard_inited, joystick_inited;
static int stopoutput;
static HANDLE kbhandle = INVALID_HANDLE_VALUE;
static int oldleds, oldusedleds, newleds, oldusbleds;
static int normalmouse, supermouse, rawmouse, winmouse, winmousenumber, winmousemode, winmousewheelbuttonstart;
static int normalkb, superkb, rawkb;

int rawkeyboard;
int no_rawinput;
int dinput_enum_all;

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
int dinput_winmousemode (void)
{
    if (winmouse)
	return winmousemode;
    return 0;
}

static isrealbutton (struct didata *did, int num)
{
    if (num >= did->buttons)
	return 0;
    if (did->axisparent[num] >= 0)
	return 0;
    return 1;
}

static void fixbuttons (struct didata *did)
{
    if (did->buttons > 0)
	return;
    write_log (L"'%s' has no buttons, adding single default button\n", did->name);
    did->buttonmappings[0] = DIJOFS_BUTTON (0);
    did->buttonsort[0] = 0;
    did->buttonname[0] = my_strdup (L"Button");
    did->buttons++;
}

static void addplusminus (struct didata *did, int i)
{
    TCHAR tmp[256];
    int j;

    if (did->buttons + 1 >= MAX_MAPPINGS)
	return;
    for (j = 0; j < 2; j++) {
        _stprintf (tmp, L"%s [%c]", did->axisname[i], j ? '+' : '-');
        did->buttonname[did->buttons] = my_strdup (tmp);
        did->buttonmappings[did->buttons] = did->axismappings[i];
        did->buttonsort[did->buttons] = 1000 + (did->axismappings[i] + did->axistype[i]) * 2 + j;
        did->axisparent[did->buttons] = i;
        did->axisparentdir[did->buttons] = j;
        did->buttons++;
    }
}

static void fixthings (struct didata *did)
{
    int i;

    for (i = 0; i < did->axles; i++)
	addplusminus (did, i);
}
static void fixthings_mouse (struct didata *did)
{
    int i;

    for (i = 0; i < did->axles; i++) {
	if (did->axissort[i] == -97)
	    addplusminus (did, i);
    }
}
typedef BOOL (CALLBACK* REGISTERRAWINPUTDEVICES)
    (PCRAWINPUTDEVICE, UINT, UINT);
static REGISTERRAWINPUTDEVICES pRegisterRawInputDevices;
typedef UINT (CALLBACK* GETRAWINPUTDATA)
    (HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static GETRAWINPUTDATA pGetRawInputData;
typedef UINT (CALLBACK* GETRAWINPUTDEVICELIST)
    (PRAWINPUTDEVICEKUST, PUINT, UINT);
static GETRAWINPUTDEVICELIST pGetRawInputDeviceList;
typedef UINT (CALLBACK* GETRAWINPUTDEVICEINFO)
    (HANDLE, UINT, LPVOID, PUINT);
static GETRAWINPUTDEVICEINFO pGetRawInputDeviceInfo;
typedef UINT (CALLBACK* GETRAWINPUTBUFFER)
    (PRAWINPUT, PUINT, UINT);
static GETRAWINPUTBUFFER pGetRawInputBuffer;
typedef LRESULT (CALLBACK* DEFRAWINPUTPROC)
    (PRAWINPUT*, INT, UINT);
static DEFRAWINPUTPROC pDefRawInputProc;

static int rawinput_available, rawinput_registered_mouse, rawinput_registered_kb;

static int register_rawinput (void)
{
    int num, rm, rkb;
    RAWINPUTDEVICE rid[2];

    if (!rawinput_available)
	return 0;

    rm = rawmouse ? 1 : 0;
    if (supermouse)
	rm = 0;
    rkb = rawkb ? 1 : 0;
    if (!rawkeyboard)
	rkb = 0;
    if (rawinput_registered_mouse == rm && rawinput_registered_kb == rkb)
	return 1;

    memset (rid, 0, sizeof (rid));
    num = 0;
    if (rawinput_registered_mouse != rm) {
	/* mouse */
	rid[num].usUsagePage = 1;
	rid[num].usUsage = 2;
	if (!rawmouse)
	    rid[num].dwFlags = RIDEV_REMOVE;
	num++;
    }
    if (rawinput_registered_kb != rkb) {
	/* keyboard */
	rid[num].usUsagePage = 1;
	rid[num].usUsage = 6;
	if (!rawkb)
	    rid[num].dwFlags = RIDEV_REMOVE;
	num++;
    }
    if (num == 0)
	return 1;
    if (pRegisterRawInputDevices (rid, num, sizeof (RAWINPUTDEVICE)) == FALSE) {
	write_log (L"RAWINPUT registration failed %d (%d,%d->%d,%d->%d)\n",
	    GetLastError (), num,
	    rawinput_registered_mouse, rm,
	    rawinput_registered_kb, rkb);
	return 0;
    }
    rawinput_registered_mouse = rm;
    rawinput_registered_kb = rkb;
    return 1;
}

static void cleardid (struct didata *did)
{
    int i;
    memset (did, 0, sizeof (*did));
    for (i = 0; i < MAX_MAPPINGS; i++) {
	did->axismappings[i] = -1;
	did->buttonmappings[i] = -1;
	did->axisparent[i] = -1;
    }
}


#define	MAX_KEYCODES 256
static uae_u8 di_keycodes[MAX_INPUT_DEVICES][MAX_KEYCODES];
static int keyboard_german;

static int keyhack (int scancode, int pressed, int num)
{
    static byte backslashstate,apostrophstate;

#ifdef RETROPLATFORM
    if (rp_checkesc (scancode, di_keycodes[num], pressed, num))
	return -1;
#endif

     //check ALT-F4
    if (pressed && !di_keycodes[num][DIK_F4] && scancode == DIK_F4) {
	if (di_keycodes[num][DIK_LALT] && !currprefs.win32_ctrl_F11_is_quit) {
#ifdef RETROPLATFORM
	    if (rp_close ())
		return -1;
#endif
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
    if (pressed && di_keycodes[num][DIK_LALT] && scancode == DIK_TAB) {
	disablecapture ();
	return -1;
    }

    if (!keyboard_german || currprefs.input_selected_setting > 0)
	return scancode;

 //This code look so ugly because there is no Directinput
 //key for # (called numbersign on win standard message)
 //available
 //so here need to change qulifier state and restore it when key
 //is release
    if (scancode == DIK_BACKSLASH) // The # key
    {
	if (di_keycodes[num][DIK_LSHIFT] || di_keycodes[num][DIK_RSHIFT] || apostrophstate)
	{
	    if (pressed)
	    {   apostrophstate=1;
		inputdevice_translatekeycode (num, DIK_RSHIFT, 0);
		inputdevice_translatekeycode (num, DIK_LSHIFT, 0);
		return 13;           // the german ' key
	    }
	    else
	    {
		//best is add a real keystatecheck here but it still work so
		apostrophstate = 0;
		inputdevice_translatekeycode (num, DIK_LALT,0);
		inputdevice_translatekeycode (num, DIK_LSHIFT,0);
		inputdevice_translatekeycode (num, 4, 0);  // release also the # key
		return 13;
	    }

	}
	if (pressed)
	{
	    inputdevice_translatekeycode (num, DIK_LALT, 1);
	    inputdevice_translatekeycode (num, DIK_LSHIFT,1);
	    return 4;           // the german # key
	}
	    else
	{
	    inputdevice_translatekeycode (num, DIK_LALT, 0);
	    inputdevice_translatekeycode (num, DIK_LSHIFT, 0);
	    // Here is the same not nice but do the job
	    return 4;           // the german # key

	}
    }
    if ((di_keycodes[num][DIK_RALT]) || (backslashstate)) {
	switch (scancode)
	{
	    case 12:
	    if (pressed)
	    {
		backslashstate=1;
		inputdevice_translatekeycode (num, DIK_RALT, 0);
		return DIK_BACKSLASH;
	    }
	    else
	    {
		backslashstate=0;
		return DIK_BACKSLASH;
	    }
	}
    }
    return scancode;
}

static int tablet;
static int axmax, aymax, azmax;
static int xmax, ymax, zmax;
static int xres, yres;
static int maxpres;
static TCHAR *tabletname;
static int tablet_x, tablet_y, tablet_z, tablet_pressure, tablet_buttons, tablet_proximity;
static int tablet_ax, tablet_ay, tablet_az, tablet_flags;

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
    tablet_send ();
}

void send_tablet (int x, int y, int z, int pres, uae_u32 buttons, int flags, int ax, int ay, int az, int rx, int ry, int rz, RECT *r)
{
    //write_log (L"%d %d %d (%d,%d,%d), %08X %d\n", x, y, pres, ax, ay, az, buttons, proxi);
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

    tablet_x = x;
    tablet_y = ymax - y;
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
    LOGCONTEXT lc;
    AXIS tx = { 0 }, ty = { 0 }, tz = { 0 };
    AXIS pres = { 0 };
    int xm, ym, zm;

    if (!tablet)
	return 0;
    xmax = -1;
    ymax = -1;
    zmax = -1;
    WTInfo (WTI_DEFCONTEXT, 0, &lc);
    WTInfo (WTI_DEVICES, DVC_X, &tx);
    WTInfo (WTI_DEVICES, DVC_Y, &ty);
    WTInfo (WTI_DEVICES, DVC_NPRESSURE, &pres);
    WTInfo (WTI_DEVICES, DVC_XMARGIN, &xm);
    WTInfo (WTI_DEVICES, DVC_YMARGIN, &ym);
    WTInfo (WTI_DEVICES, DVC_ZMARGIN, &zm);
    xmax = tx.axMax;
    ymax = ty.axMax;
    if (WTInfo (WTI_DEVICES, DVC_Z, &tz))
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
    write_log (L"Tablet '%s' parameters\n", tabletname);
    write_log (L"Xmax=%d,Ymax=%d,Zmax=%d\n", xmax, ymax, zmax);
    write_log (L"Xres=%.1f:%d,Yres=%.1f:%d,Zres=%.1f:%d\n",
	tx.axResolution / 65536.0, tx.axUnits, ty.axResolution / 65536.0, ty.axUnits, tz.axResolution / 65536.0, tz.axUnits);
    write_log (L"Xrotmax=%d,Yrotmax=%d,Zrotmax=%d\n", axmax, aymax, azmax);
    write_log (L"PressureMin=%d,PressureMax=%d\n", pres.axMin, pres.axMax);
    maxpres = pres.axMax;
    xres = gettabletres (&tx);
    yres = gettabletres (&ty);
    tablet_proximity = -1;
    tablet_x = -1;
    inputdevice_tablet_info (xmax, ymax, zmax, axmax, aymax, azmax, xres, yres);
    return WTOpen (hwnd, &lc, TRUE);
}

int close_tablet (void *ctx)
{
    if (ctx != NULL)
	WTClose (ctx);
    ctx = NULL;
    if (!tablet)
	return 0;
    return 1;
}

int is_tablet (void)
{
    return tablet ? 1 : 0;
}

static int initialize_tablet (void)
{
    HANDLE h;
    TCHAR name[MAX_DPATH];
    struct tagAXIS ori[3];
    int tilt = 0;

    h = LoadLibrary (L"wintab32.dll");
    if (h == NULL) {
	write_log (L"Tablet: no wintab32.dll\n");
	return 0;
    }
    FreeLibrary (h);
    if (!WTInfo (0, 0, NULL)) {
	write_log (L"Tablet: WTInfo() returned failure\n");
	return 0;
    }
    name[0] = 0;
    WTInfo (WTI_DEVICES, DVC_NAME, name);
    axmax = aymax = azmax = -1;
    tilt = WTInfo (WTI_DEVICES, DVC_ORIENTATION, &ori);
    if (tilt) {
	if (ori[0].axMax > 0)
	    axmax = ori[0].axMax;
	if (ori[1].axMax > 0)
	    aymax = ori[1].axMax;
	if (ori[2].axMax > 0)
	    azmax = ori[2].axMax;
    }
    write_log (L"Tablet '%s' detected\n", name);
    tabletname = my_strdup (name);
    tablet = TRUE;
    return 1;
}

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
	    _stprintf (tmp, L"Catweasel mouse");
	    did->name = my_strdup (tmp);
	    did->sortname = my_strdup (tmp);
	    _stprintf (tmp, L"CWMOUSE%d", i);
	    did->configname = my_strdup (tmp);
	    did->buttons = did->buttons_real = 3;
	    did->axles = 2;
	    did->axissort[0] = 0;
	    did->axisname[0] = my_strdup (L"X-Axis");
	    did->axissort[1] = 1;
	    did->axisname[1] = my_strdup (L"Y-Axis");
	    for (j = 0; j < did->buttons; j++) {
		did->buttonsort[j] = j;
		_stprintf (tmp, L"Button %d", j + 1);
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
	    _stprintf (tmp, L"Catweasel joystick");
	    did->name = my_strdup (tmp);
	    did->sortname = my_strdup (tmp);
	    _stprintf (tmp, L"CWJOY%d", i);
	    did->configname = my_strdup (tmp);
	    did->buttons = did->buttons_real =(catweasel_isjoystick() & 0x80) ? 3 : 1;
	    did->axles = 2;
	    did->axissort[0] = 0;
	    did->axisname[0] = my_strdup (L"X-Axis");
	    did->axissort[1] = 1;
	    did->axisname[1] = my_strdup (L"Y-Axis");
	    for (j = 0; j < did->buttons; j++) {
		did->buttonsort[j] = j;
		_stprintf (tmp, L"Button %d", j + 1);
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


#define RDP_DEVICE1 L"\\??\\Root#RDP_"
#define RDP_DEVICE2 L"\\\\?\\Root#RDP_"

static int rdpdevice(TCHAR *buf)
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

    _stprintf (tmp, L"\\\\?\\%s", name);
    for (i = 4; i < _tcslen (tmp); i++) {
	if (tmp[i] == '\\')
	    tmp[i] = '#';
	tmp[i] = _totupper (tmp[i]);
    }
    for (ii = 0; ii < 2; ii++) {
	for (i = 0; i < (ii == 0 ? num_mouse : num_keyboard); i++) {
	    struct didata *did = ii == 0 ? &di_mouse[i] : &di_keyboard[i];
	    TCHAR tmp2[MAX_DPATH];
	    if (!did->rawinput)
		continue;
	    for (j = 0; j < _tcslen (did->configname); j++)
		tmp2[j] = _totupper (did->configname[j]);
	    tmp2[j] = 0;
	    if (_tcslen (tmp2) >= _tcslen (tmp) && !_tcsncmp (tmp2, tmp, _tcslen (tmp))) {
		xfree (did->name);
		xfree (did->sortname);
		did->name = my_strdup (friendlyname);
		did->sortname = my_strdup (friendlyname);
		write_log (L"'%s' ('%s')\n", did->name, did->configname);
	    }
	}
    }
}

static void rawinputfriendlynames (void)
{
    HDEVINFO di;
    int i, ii;

    for (ii = 0; ii < 2; ii++) {
	di = SetupDiGetClassDevs (ii == 0 ? &GUID_DEVCLASS_MOUSE : &GUID_DEVCLASS_KEYBOARD, NULL, NULL, DIGCF_PRESENT);
	if (di != INVALID_HANDLE_VALUE) {
	    SP_DEVINFO_DATA dd;
	    dd.cbSize = sizeof dd;
	    for (i = 0; SetupDiEnumDeviceInfo (di, i, &dd); i++) {
		TCHAR buf[MAX_DPATH];
		DWORD size = 0;
		if (SetupDiGetDeviceInstanceId (di, &dd, buf, sizeof buf , &size)) {
		    TCHAR fname[MAX_DPATH];
		    DWORD dt;
		    fname[0] = 0;
		    size = 0;
		    if (!SetupDiGetDeviceRegistryProperty (di, &dd,
			SPDRP_FRIENDLYNAME, &dt, (PBYTE)fname, sizeof fname, &size)) {
			size = 0;
			SetupDiGetDeviceRegistryProperty (di, &dd,
			    SPDRP_DEVICEDESC, &dt, (PBYTE)fname, sizeof fname, &size);
		    }
		    if (size > 0 && fname[0])
			rawinputfixname (buf, fname);
 		}
	    }
	    SetupDiDestroyDeviceInfoList (di);
	}
    }
}

static TCHAR *rawkeyboardlabels[256] =
{
    L"ESCAPE",
    L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"0",
    L"MINUS",L"EQUALS",L"BACK",L"TAB",
    L"Q",L"W",L"E",L"R",L"T",L"Y",L"U",L"I",L"O",L"P",
    L"LBRACKET",L"RBRACKET",L"RETURN",L"LCONTROL",
    L"A",L"S",L"D",L"F",L"G",L"H",L"J",L"K",L"L",
    L"SEMICOLON",L"APOSTROPHE",L"GRAVE",L"LSHIFT",L"BACKSLASH",
    L"Z",L"X",L"C",L"V",L"B",L"N",L"M",
    L"COMMA",L"PERIOD",L"SLASH",L"RSHIFT",L"MULTIPLY",L"LMENU",L"SPACE",L"CAPITAL",
    L"F1",L"F2",L"F3",L"F4",L"F5",L"F6",L"F7",L"F8",L"F9",L"F10",
    L"NULOCK",L"SCROLL",L"NUMPAD7",L"NUMPAD8",L"NUMPAD9",L"SUBTRACT",
    L"NUMPAD4",L"NUMPAD5",L"NUMPAD6",L"ADD",L"NUMPAD1",L"NUMPAD2",L"NUMPAD3",L"NUMPAD0",
    L"DECIMAL",NULL,NULL,L"OEM_102",L"F11",L"F12",
    L"F13",L"F14",L"F15",L"F16",NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,
    L"NUMPADEQUALS",NULL,NULL,
    L"PREVTRACK",NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    L"NEXTTRACK",NULL,NULL,L"NUMPADENTER",L"RCONTROL",NULL,NULL,
    L"MUTE",L"CALCULATOR",L"PLAYPAUSE",NULL,L"MEDIASTOP",
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    L"VOLUMEDOWN",NULL,L"VOLUMEUP",NULL,L"WEBHOME",L"NUMPADCOMMA",NULL,
    L"DIVIDE",NULL,L"SYSRQ",L"RMENU",
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    L"PAUSE",NULL,L"HOME",L"UP",L"PRIOR",NULL,L"LEFT",NULL,L"RIGHT",NULL,L"END",
    L"DOWN",L"NEXT",L"INSERT",L"DELETE",
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    L"LWIN",L"RWIN",L"APPS",L"POWER",L"SLEEP",
    NULL,NULL,NULL,
    L"WAKE",NULL,L"WEBSEARCH",L"WEBFAVORITES",L"WEBREFRESH",L"WEBSTOP",
    L"WEBFORWARD",L"WEBBACK",L"MYCOMPUTER",L"MAIL",L"MEDIASELECT",
    L""
};

static int initialize_rawinput (void)
{
    RAWINPUTDEVICELIST *ridl = 0;
    int num = 500, gotnum, i, bufsize, vtmp;
    int rnum_mouse, rnum_kb, rnum_raw;
    TCHAR *buf = NULL;
    int rmouse = 0, rkb = 0;
    TCHAR tmp[100];

    if (no_rawinput)
	goto error;
    pRegisterRawInputDevices = (REGISTERRAWINPUTDEVICES)GetProcAddress (
	GetModuleHandle (L"user32.dll"), "RegisterRawInputDevices");
    pGetRawInputData = (GETRAWINPUTDATA)GetProcAddress (
	GetModuleHandle (L"user32.dll"), "GetRawInputData");
    pGetRawInputDeviceList = (GETRAWINPUTDEVICELIST)GetProcAddress (
	GetModuleHandle (L"user32.dll"), "GetRawInputDeviceList");
    pGetRawInputDeviceInfo = (GETRAWINPUTDEVICEINFO)GetProcAddress (
	GetModuleHandle (L"user32.dll"), "GetRawInputDeviceInfoW");
    pGetRawInputBuffer = (GETRAWINPUTBUFFER)GetProcAddress (
	GetModuleHandle (L"user32.dll"), "GetRawInputBuffer");
    pDefRawInputProc = (DEFRAWINPUTPROC)GetProcAddress (
	GetModuleHandle (L"user32.dll"), "DefRawInputProc");

    if (!pRegisterRawInputDevices || !pGetRawInputData || !pGetRawInputDeviceList ||
	!pGetRawInputDeviceInfo || !pGetRawInputBuffer || !pDefRawInputProc)
	goto error;

    bufsize = 10000 * sizeof (TCHAR);
    buf = xmalloc (bufsize);

    register_rawinput ();
    if (pGetRawInputDeviceList (NULL, &num, sizeof (RAWINPUTDEVICELIST)) != 0) {
	write_log (L"RAWINPUT error %08X\n", GetLastError());
	goto error2;
    }
    write_log (L"RAWINPUT: found %d devices\n", num);
    if (num <= 0)
	goto error2;
    ridl = xcalloc (sizeof (RAWINPUTDEVICELIST), num);
    gotnum = pGetRawInputDeviceList (ridl, &num, sizeof (RAWINPUTDEVICELIST));
    if (gotnum <= 0) {
	write_log (L"RAWINPUT didn't find any devices\n");
	goto error2;
    }
    rnum_raw = rnum_mouse = rnum_kb = 0;
    for (i = 0; i < gotnum; i++) {
	int type = ridl[i].dwType;
	HANDLE h = ridl[i].hDevice;

	if (pGetRawInputDeviceInfo (h, RIDI_DEVICENAME, NULL, &vtmp) == 1)
	    continue;
	if (vtmp >= bufsize)
	    continue;
	if (pGetRawInputDeviceInfo (h, RIDI_DEVICENAME, buf, &vtmp) == -1)
	    continue;
	if (rdpdevice (buf))
	    continue;
	if (type == RIM_TYPEMOUSE)
	    rnum_mouse++;
	if (type == RIM_TYPEKEYBOARD)
	    rnum_kb++;
    }

    for (i = 0; i < gotnum; i++) {
	HANDLE h = ridl[i].hDevice;
	int type = ridl[i].dwType;

	if (type == RIM_TYPEKEYBOARD && !rawkeyboard)
	    continue;

	if (type == RIM_TYPEKEYBOARD || type == RIM_TYPEMOUSE) {
	    struct didata *did = type == RIM_TYPEMOUSE ? di_mouse : di_keyboard;
	    PRID_DEVICE_INFO rdi;
	    int v, j;

	    if (pGetRawInputDeviceInfo (h, RIDI_DEVICENAME, NULL, &vtmp) == -1)
		continue;
	    if (vtmp >= bufsize)
		continue;
	    if (pGetRawInputDeviceInfo (h, RIDI_DEVICENAME, buf, &vtmp) == -1)
		continue;

	    if (did == di_mouse) {
		if (rdpdevice (buf))
		    continue;
		if (num_mouse >= MAX_INPUT_DEVICES - 1) /* leave space for Windows mouse */
		    continue;
		did += num_mouse;
		num_mouse++;
		rmouse++;
		v = rmouse;
	    } else if (did == di_keyboard) {
		if (rdpdevice (buf))
		    continue;
		if (rnum_kb < 2)
		    continue;
		if (num_keyboard >= MAX_INPUT_DEVICES - 1)
		    continue;
		did += num_keyboard;
		num_keyboard++;
		rkb++;
		v = rkb;
	    }

	    rnum_raw++;
	    cleardid (did);
	    _stprintf (tmp, L"%s", type == RIM_TYPEMOUSE ? L"RAW Mouse" : L"RAW Keyboard");
	    did->name = my_strdup (tmp);
	    did->rawinput = h;
	    did->connection = DIDC_RAW;

	    write_log (L"%p %s: ", h, type == RIM_TYPEMOUSE ? L"mouse" : L"keyboard");
	    did->sortname = my_strdup (buf);
	    write_log (L"'%s'\n", buf);
	    did->configname = my_strdup (buf);
	    rdi = (PRID_DEVICE_INFO)buf;
	    memset (rdi, 0, sizeof (RID_DEVICE_INFO));
	    rdi->cbSize = sizeof (RID_DEVICE_INFO);
	    if (pGetRawInputDeviceInfo (h, RIDI_DEVICEINFO, NULL, &vtmp) == -1)
		continue;
	    if (vtmp >= bufsize)
		continue;
	    if (pGetRawInputDeviceInfo (h, RIDI_DEVICEINFO, buf, &vtmp) == -1)
		continue;

	    if (type == RIM_TYPEMOUSE) {
		PRID_DEVICE_INFO_MOUSE rdim = &rdi->mouse;
		write_log (L"id=%d buttons=%d hw=%d rate=%d\n",
		    rdim->dwId, rdim->dwNumberOfButtons, rdim->fHasHorizontalWheel, rdim->dwSampleRate);
		if (rdim->dwNumberOfButtons >= MAX_MAPPINGS) {
		    write_log (L"bogus number of buttons, ignored\n");
		    continue;
		}
		did->buttons_real = did->buttons = rdim->dwNumberOfButtons;
		for (j = 0; j < did->buttons; j++) {
		    did->buttonsort[j] = j;
		    _stprintf (tmp, L"Button %d", j + 1);
		    did->buttonname[j] = my_strdup (tmp);
		}
		did->axles = 3;
		did->axissort[0] = 0;
		did->axisname[0] = my_strdup (L"X-Axis");
		did->axissort[1] = 1;
		did->axisname[1] = my_strdup (L"Y-Axis");
		did->axissort[2] = 2;
		did->axisname[2] = my_strdup (L"Wheel");
		addplusminus (did, 2);
		if (rdim->fHasHorizontalWheel) {
		    did->axissort[3] = 3;
		    did->axisname[3] = my_strdup (L"HWheel");
		    did->axles++;
		    addplusminus (did, 3);
		}
		did->priority = -1;
	    } else {
		int j;
		PRID_DEVICE_INFO_KEYBOARD rdik = &rdi->keyboard;
		write_log (L"type=%d sub=%d mode=%d fkeys=%d indicators=%d tkeys=%d",
		    rdik->dwType, rdik->dwSubType, rdik->dwKeyboardMode,
		    rdik->dwNumberOfFunctionKeys, rdik->dwNumberOfIndicators, rdik->dwNumberOfKeysTotal);
		j = 0;
		for (i = 0; i < 254; i++) {
		    TCHAR tmp[100];
		    tmp[0] = 0;
		    if (rawkeyboardlabels[j] != NULL) {
			if (rawkeyboardlabels[j][0]) {
			    _tcscpy (tmp, rawkeyboardlabels[j]);
			    j++;
			}
		    } else {
			j++;
		    }
		    if (!tmp[0])
			_stprintf (tmp, L"Key %02X", i + 1);
		    did->buttonname[i] = my_strdup (tmp);
		    did->buttonmappings[i] = i + 1;
		    did->buttonsort[i] = i + 1;
		    did->buttons++;
		}
	    }
	}
    }

    rawinputfriendlynames ();

    xfree (ridl);
    xfree (buf);
    if (rnum_raw > 0)
	rawinput_available = 1;
    return 1;

error:
    write_log (L"RAWINPUT not available or failed to initialize\n");
error2:
    xfree (ridl);
    xfree (buf);
    return 0;
}

static void initialize_windowsmouse (void)
{
    struct didata *did = di_mouse;
    TCHAR tmp[100], *name;
    int i, j;

    did += num_mouse;
    for (i = 0; i < 1; i++) {
	if (num_mouse >= MAX_INPUT_DEVICES)
	    return;
	num_mouse++;
	name = (i == 0) ? L"Windows mouse" : L"Mousehack mouse";
	did->connection = DIDC_WIN;
	did->name = my_strdup (i ? L"Mousehack mouse (Required for tablets)" : L"Windows mouse");
	did->sortname = my_strdup (i ? L"Windowsmouse2" : L"Windowsmouse1");
	did->configname = my_strdup (i ? L"WINMOUSE2" : L"WINMOUSE1");
	did->buttons = GetSystemMetrics (SM_CMOUSEBUTTONS);
	if (did->buttons < 3)
	    did->buttons = 3;
	if (did->buttons > 5)
	    did->buttons = 5; /* no non-direcinput support for >5 buttons */
	did->buttons_real = did->buttons;
	for (j = 0; j < did->buttons; j++) {
	    did->buttonsort[j] = j;
	    _stprintf (tmp, L"Button %d", j + 1);
	    did->buttonname[j] = my_strdup (tmp);
	}
	winmousewheelbuttonstart = did->buttons;
	did->axles = os_vista ? 4 : 3;
	did->axissort[0] = 0;
	did->axisname[0] = my_strdup (L"X-Axis");
	did->axissort[1] = 1;
	did->axisname[1] = my_strdup (L"Y-Axis");
	if (did->axles > 2) {
	    did->axissort[2] = 2;
	    did->axisname[2] = my_strdup (L"Wheel");
	    addplusminus (did, 2);
	}
	if (did->axles > 3) {
	    did->axissort[3] = 3;
	    did->axisname[3] = my_strdup (L"HWheel");
	    addplusminus (did, 3);
	}
	did->priority = 2;
	did->wininput = i + 1;
	did++;
    }
}

static uae_u8 rawkeystate[256];
static void handle_rawinput_2 (RAWINPUT *raw)
{
    int i, num;
    struct didata *did;

    if (raw->header.dwType == RIM_TYPEMOUSE) {
	PRAWMOUSE rm = &raw->data.mouse;

	for (num = 0; num < num_mouse; num++) {
	    did = &di_mouse[num];
	    if (did->rawinput == raw->header.hDevice)
		break;
	}
#ifdef DI_DEBUG_RAWINPUT
	write_log (L"HANDLE=%08x %04x %04x %04x %08x %3d %3d %08x M=%d\n",
	    raw->header.hDevice,
	    rm->usFlags,
	    rm->usButtonFlags,
	    rm->usButtonData,
	    rm->ulRawButtons,
	    rm->lLastX,
	    rm->lLastY,
	    rm->ulExtraInformation, num < num_mouse ? num + 1 : -1);
#endif
	if (num == num_mouse)
	    return;

	if (isfocus () > 0) {
	    for (i = 0; i < (5 > did->buttons ? did->buttons : 5); i++) {
	        if (rm->usButtonFlags & (3 << (i * 2)))
		    setmousebuttonstate (num, i, (rm->usButtonFlags & (1 << (i * 2))) ? 1 : 0);
	    }
	    if (did->buttons > 5) {
	        for (i = 5; i < did->buttons; i++)
		    setmousebuttonstate (num, i, (rm->ulRawButtons & (1 << i)) ? 1 : 0);
	    }
	    if (rm->usButtonFlags & RI_MOUSE_WHEEL) {
		int val = (short)rm->usButtonData;
		int bnum = did->buttons_real;
		setmousestate (num, 2, val, 0);
		if (val < 0)
		    setmousebuttonstate (num, bnum + 0, -1);
		else if (val > 0)
		    setmousebuttonstate (num, bnum + 1, -1);
	    }
	    setmousestate (num, 0, rm->lLastX, (rm->usFlags & MOUSE_MOVE_ABSOLUTE) ? 1 : 0);
	    setmousestate (num, 1, rm->lLastY, (rm->usFlags & MOUSE_MOVE_ABSOLUTE) ? 1 : 0);
	}
	if (isfocus ()) {
	    if (did->buttons >= 3 && (rm->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)) {
		if (currprefs.win32_middle_mouse) {
		    if (isfullscreen () > 0)
		        minimizewindow ();
		    if (mouseactive)
		        setmouseactive(0);
		}
	    }
	}

    } else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
	int istest = inputdevice_istest ();
	PRAWKEYBOARD rk = &raw->data.keyboard;
	uae_u8 scancode = (rk->MakeCode & 0x7f) | ((rk->Flags & RI_KEY_E0) ? 0x80 : 0x00);
	int pressed = (rk->Flags & RI_KEY_BREAK) ? 0 : 1;

#ifdef DI_DEBUG_RAWINPUT
	write_log (L"HANDLE=%x CODE=%x Flags=%x VK=%x MSG=%x EXTRA=%x\n",
	    raw->header.hDevice,
	    raw->data.keyboard.MakeCode,
	    raw->data.keyboard.Flags,
	    raw->data.keyboard.VKey,
	    raw->data.keyboard.Message,
	    raw->data.keyboard.ExtraInformation);
#endif
	if (rk->MakeCode == KEYBOARD_OVERRUN_MAKE_CODE)
	    return;
	if (scancode == 0xaa)
	    return;

#if 0
	for (num = 0; num < num_keyboard; num++) {
	    did = &di_keyboard[num];
	    if ((did->acquired || rawkeyboard > 0) && did->rawinput == raw->header.hDevice)
		break;
	}
	if (num == num_keyboard) {
	    if (scancode == DIK_F12 && pressed) {
		inputdevice_add_inputcode (AKS_ENTERGUI, 1);
		return;
	    }
	    return;
	}
#endif
	num = 0;
	if (rawkeystate[scancode] == pressed)
	    return;
	rawkeystate[scancode] = pressed;
	if (istest) {
	    inputdevice_do_keyboard (scancode, pressed);
	} else {
	    scancode = keyhack (scancode, pressed, num);
	    if (scancode < 0 || isfocus () == 0)
		return;
	    di_keycodes[num][scancode] = pressed;
	    if (stopoutput == 0)
		my_kbd_handler (num, scancode, pressed);
	}
    }
}

#if 0
static void read_rawinput (void)
{
    RAWINPUT raw;
    int size, ret;

    for(;;) {
	size = sizeof (RAWINPUT);
	ret = pGetRawInputBuffer (&raw, &size, sizeof (RAWINPUTHEADER));
	if (ret <= 0)
	    return;
	handle_rawinput_2 (&raw);
    }
}
#endif

void handle_rawinput (LPARAM lParam)
{
    UINT dwSize;
    BYTE lpb[1000];
    RAWINPUT *raw;

    if (!rawinput_available)
	return;
    pGetRawInputData ((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof (RAWINPUTHEADER));
    if (dwSize <= sizeof (lpb)) {
	if (pGetRawInputData ((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof (RAWINPUTHEADER)) == dwSize) {
	    raw = (RAWINPUT*)lpb;
	    handle_rawinput_2 (raw);
	    pDefRawInputProc (&raw, 1, sizeof (RAWINPUTHEADER));
	}
    }
}

static void unacquire (LPDIRECTINPUTDEVICE8 lpdi, TCHAR *txt)
{
    HRESULT hr;
    if (lpdi) {
	hr = IDirectInputDevice8_Unacquire (lpdi);
	if (FAILED (hr) && hr != DI_NOEFFECT)
	    write_log (L"unacquire %s failed, %s\n", txt, DXError (hr));
    }
}

static int acquire (LPDIRECTINPUTDEVICE8 lpdi, TCHAR *txt)
{
    HRESULT hr = DI_OK;
    if (lpdi) {
	hr = IDirectInputDevice8_Acquire (lpdi);
	if (FAILED (hr) && hr != 0x80070005) {
	    write_log (L"acquire %s failed, %s\n", txt, DXError (hr));
	}
    }
    return SUCCEEDED (hr) ? 1 : 0;
}

static int setcoop (struct didata *did, DWORD mode, TCHAR *txt)
{
    HRESULT hr = DI_OK;
    if (did->lpdi) {
	did->coop = 0;
	if (!did->coop && hMainWnd) {
	    hr = IDirectInputDevice8_SetCooperativeLevel (did->lpdi, hMainWnd, mode);
	    if (FAILED (hr) && hr != E_NOTIMPL) {
		write_log (L"setcooperativelevel %s failed, %s\n", txt, DXError (hr));
	    } else {
		did->coop = 1;
		//write_log (L"cooperativelevel %s set\n", txt);
	    }
	}
    }
    return SUCCEEDED (hr) ? 1 : 0;
}

static void sortdd (struct didata *dd, int num, int type)
{
    int i, j;
    struct didata ddtmp;

    for (i = 0; i < num; i++) {
	dd[i].type = type;
	for (j = i + 1; j < num; j++) {
	    dd[j].type = type;
	    if (dd[i].priority < dd[j].priority || (dd[i].priority == dd[j].priority && _tcscmp (dd[i].sortname, dd[j].sortname) > 0)) {
		memcpy (&ddtmp, &dd[i], sizeof (ddtmp));
		memcpy (&dd[i], &dd[j], sizeof (ddtmp));
		memcpy (&dd[j], &ddtmp, sizeof (ddtmp));
	    }
	}
    }

    /* rename duplicate names */
    for (i = 0; i < num; i++) {
	for (j = i + 1; j < num; j++) {
	    if (!_tcscmp (dd[i].name, dd[j].name)) {
		int cnt = 1;
		TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
		_tcscpy (tmp2, dd[i].name);
		for (j = i; j < num; j++) {
		    if (!_tcscmp (tmp2, dd[j].name)) {
			_stprintf (tmp, L"%s [%d]", dd[j].name, cnt++);
			xfree (dd[j].name);
			dd[j].name = my_strdup (tmp);
		    }
		}
		break;
	    }
	}
    }

}

static void sortobjects (struct didata *did, int *mappings, int *sort, TCHAR **names, int *types, int num)
{
    int i, j, tmpi;
    TCHAR *tmpc;

    for (i = 0; i < num; i++) {
	for (j = i + 1; j < num; j++) {
	    if (sort[i] > sort[j]) {
		tmpi = mappings[i]; mappings[i] = mappings[j]; mappings[j] = tmpi;
		tmpi = sort[i]; sort[i] = sort[j]; sort[j] = tmpi;
		if (types) {
		    tmpi = types[i]; types[i] = types[j]; types[j] = tmpi;
		}
		tmpc = names[i]; names[i] = names[j]; names[j] = tmpc;
	    }
	}
    }
#ifdef DI_DEBUG
    if (num > 0) {
	write_log (L"%s (PGUID=%s):\n", did->name, outGUID (&did->pguid));
	for (i = 0; i < num; i++)
	    write_log (L"%02X %03d '%s' (%d,%d)\n", mappings[i], mappings[i], names[i], sort[i], types ? types[i] : -1);
    }
#endif
}

static int isg (const GUID *g, const GUID *g2, int *dwofs, int v)
{
    if (!memcmp (g, g2, sizeof (GUID))) {
	*dwofs = v;
	return 1;
    }
    return 0;
}

static int makesort_joy (const GUID *g, int *dwofs)
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

static int makesort_mouse (const GUID *g, int *dwofs)
{
    if (isg (g, &GUID_XAxis, dwofs, DIMOFS_X)) return -99;
    if (isg (g, &GUID_YAxis, dwofs, DIMOFS_Y)) return -98;
    if (isg (g, &GUID_ZAxis, dwofs, DIMOFS_Z)) return -97;
    return *dwofs;
}

static BOOL CALLBACK EnumObjectsCallback (const DIDEVICEOBJECTINSTANCE* pdidoi, VOID *pContext)
{
    struct didata *did = pContext;
    int i;
    TCHAR tmp[100];

#if 0
    if (pdidoi->dwOfs != DIDFT_GETINSTANCE (pdidoi->dwType))
	write_log (L"%x-%s: %x <> %x\n", pdidoi->dwType & 0xff, pdidoi->tszName,
	    pdidoi->dwOfs, DIDFT_GETINSTANCE (pdidoi->dwType));
#endif
    if (pdidoi->dwType & DIDFT_AXIS) {
	int sort = 0;
	if (did->axles >= MAX_MAPPINGS)
	    return DIENUM_CONTINUE;
	did->axismappings[did->axles] = DIDFT_GETINSTANCE (pdidoi->dwType);
	did->axisname[did->axles] = my_strdup (pdidoi->tszName);
	if (did->type == DID_JOYSTICK)
	    sort = makesort_joy (&pdidoi->guidType, &did->axismappings[did->axles]);
	else if (did->type == DID_MOUSE)
	    sort = makesort_mouse (&pdidoi->guidType, &did->axismappings[did->axles]);
	if (sort < 0) {
	    for (i = 0; i < did->axles; i++) {
		if (did->axissort[i] == sort) {
		    write_log (L"ignored duplicate '%s'\n", pdidoi->tszName);
		    return DIENUM_CONTINUE;
		}
	    }
	}
	did->axissort[did->axles] = sort;
	did->axles++;
    }
    if (pdidoi->dwType & DIDFT_POV) {
	int numpov = 0;
	if (did->axles + 1 >= MAX_MAPPINGS)
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
	    did->axismappings[did->axles + i] = DIJOFS_POV(numpov);
	    _stprintf (tmp, L"%s (%d)", pdidoi->tszName, i + 1);
	    did->axisname[did->axles + i] = my_strdup (tmp);
	    did->axissort[did->axles + i] = did->axissort[did->axles];
	    did->axistype[did->axles + i] = i + 1;
	}
	did->axles += 2;
    }

    if (pdidoi->dwType & DIDFT_BUTTON) {
	if (did->buttons >= MAX_MAPPINGS)
	    return DIENUM_CONTINUE;
	did->buttonname[did->buttons] = my_strdup (pdidoi->tszName);
	if (did->type == DID_JOYSTICK) {
	    //did->buttonmappings[did->buttons] = DIJOFS_BUTTON(DIDFT_GETINSTANCE (pdidoi->dwType));
	    did->buttonmappings[did->buttons] = DIJOFS_BUTTON(did->buttons);
	    did->buttonsort[did->buttons] = makesort_joy (&pdidoi->guidType, &did->buttonmappings[did->buttons]);
	} else if (did->type == DID_MOUSE) {
	    //did->buttonmappings[did->buttons] = FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + DIDFT_GETINSTANCE (pdidoi->dwType);
	    did->buttonmappings[did->buttons] = FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + did->buttons;
	    did->buttonsort[did->buttons] = makesort_mouse (&pdidoi->guidType, &did->buttonmappings[did->buttons]);
	} else {
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
    int len = _tcslen (s);
    while (len > 0 && _tcscspn (s + len - 1, L"\t \r\n") == 0)
        s[--len] = '\0';
}

static BOOL CALLBACK di_enumcallback (LPCDIDEVICEINSTANCE lpddi, LPVOID *dd)
{
    struct didata *did;
    int len, type;
    TCHAR *typetxt;
    TCHAR tmp[100];

    type = lpddi->dwDevType & 0xff;
    if (type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_SCREENPOINTER) {
	did = di_mouse;
	typetxt = L"Mouse";
    } else if (type == DI8DEVTYPE_GAMEPAD  || type == DI8DEVTYPE_JOYSTICK ||
	type == DI8DEVTYPE_FLIGHT || type == DI8DEVTYPE_DRIVING || type == DI8DEVTYPE_1STPERSON) {
	did = di_joystick;
	typetxt = L"Game controller";
    } else if (type == DI8DEVTYPE_KEYBOARD) {
	did = di_keyboard;
	typetxt = L"Keyboard";
    } else {
	did = NULL;
	typetxt = L"Unknown";
    }

#ifdef DI_DEBUG
    write_log (L"I=%s ", outGUID (&lpddi->guidInstance));
    write_log (L"P=%s\n", outGUID (&lpddi->guidProduct));
    write_log (L"'%s' '%s' %08X [%s]\n", lpddi->tszProductName, lpddi->tszInstanceName, lpddi->dwDevType, typetxt);
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
	len = _tcslen (lpddi->tszInstanceName) + 5 + 1;
	did->name = malloc (len * sizeof (TCHAR));
	_tcscpy (did->name, lpddi->tszInstanceName);
    } else {
	did->name = malloc (100);
	_stprintf (did->name, L"[no name]");
    }
    trimws (did->name);
    _stprintf (tmp, L"%08X-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X %08X-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X",
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
	_tcscat (did->name, L" *");
    }
    return DIENUM_CONTINUE;
}

extern HINSTANCE hInst;
static LPDIRECTINPUT8 g_lpdi;


static void di_dev_free (struct didata *did)
{
    if (did->lpdi)
	IDirectInputDevice8_Release (did->lpdi);
    xfree (did->name);
    xfree (did->sortname);
    xfree (did->configname);
    memset (did, 0, sizeof (struct didata));
}

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
    hr = DirectInput8Create (hInst, DIRECTINPUT_VERSION, &IID_IDirectInput8, (LPVOID *)&g_lpdi, NULL);
    if (FAILED(hr)) {
	write_log (L"DirectInput8Create failed, %s\n", DXError (hr));
	gui_message (L"Failed to initialize DirectInput!");
	return 0;
    }
    if (dinput_enum_all) {
	write_log (L"DirectInput enumeration..\n");
	IDirectInput8_EnumDevices (g_lpdi, DI8DEVCLASS_ALL, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
    } else {
	if (rawkeyboard <= 0) {
	    write_log (L"DirectInput enumeration.. Keyboards..\n");
	    IDirectInput8_EnumDevices (g_lpdi, DI8DEVCLASS_KEYBOARD, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
	}
	write_log (L"DirectInput enumeration.. Pointing devices..\n");
	IDirectInput8_EnumDevices (g_lpdi, DI8DEVCLASS_POINTER, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
	write_log (L"DirectInput enumeration.. Game controllers..\n");
	IDirectInput8_EnumDevices (g_lpdi, DI8DEVCLASS_GAMECTRL, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
    }
    write_log (L"RawInput enumeration..\n");
    initialize_rawinput ();
    write_log (L"Windowsmouse initialization..\n");
    initialize_windowsmouse ();
    write_log (L"Catweasel joymouse initialization..\n");
    initialize_catweasel ();
    write_log (L"wintab tablet initialization..\n");
    initialize_tablet ();
    write_log (L"end\n");

    sortdd (di_joystick, num_joystick, DID_JOYSTICK);
    sortdd (di_mouse, num_mouse, DID_MOUSE);
    sortdd (di_keyboard, num_keyboard, DID_KEYBOARD);

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
    }
    return -1;
}

static int get_mouse_widget_type (int mouse, int num, TCHAR *name, uae_u32 *code)
{
    struct didata *did = &di_mouse[mouse];

    int axles = did->axles;
    int buttons = did->buttons;
    if (num >= axles && num < axles + buttons) {
	if (name)
	    _tcscpy (name, did->buttonname[num - did->axles]);
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
	    hr = IDirectInput8_CreateDevice (g_lpdi, &did->iguid, &lpdi, NULL);
	    if (SUCCEEDED (hr)) {
		hr = IDirectInputDevice8_SetDataFormat (lpdi, &c_dfDIMouse2);
		IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, (void*)did, DIDFT_ALL);
		fixbuttons (did);
		fixthings_mouse (did);
		sortobjects (did, did->axismappings, did->axissort, did->axisname, did->axistype, did->axles);
		sortobjects (did, did->buttonmappings, did->buttonsort, did->buttonname, 0, did->buttons);
		did->lpdi = lpdi;
	    } else {
		write_log (L"mouse %d CreateDevice failed, %s\n", i, DXError (hr));
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
    LPDIRECTINPUTDEVICE8 lpdi = di_mouse[num].lpdi;
    struct didata *did = &di_mouse[num];
    DIPROPDWORD dipdw;
    HRESULT hr;

    unacquire (lpdi, L"mouse");
    if (did->connection == DIDC_DX && lpdi) {
	setcoop (&di_mouse[num], flags ? (DISCL_FOREGROUND | DISCL_EXCLUSIVE) : (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE), L"mouse");
	dipdw.diph.dwSize = sizeof (DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = DI_BUFFER;
	hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
	if (FAILED (hr))
	    write_log (L"mouse setpropertry failed, %s\n", DXError (hr));
	di_mouse[num].acquired = acquire (lpdi, L"mouse") ? 1 : -1;
    } else {
	di_mouse[num].acquired = 1;
    }
    if (di_mouse[num].acquired > 0) {
	if (di_mouse[num].rawinput)
	    rawmouse++;
	else if (di_mouse[num].superdevice)
	    supermouse++;
	else if (di_mouse[num].wininput) {
	    winmouse++;
	    winmousenumber = num;
	    winmousemode = di_mouse[num].wininput == 2;
	} else
	    normalmouse++;
    }
    register_rawinput ();
    return di_mouse[num].acquired > 0 ? 1 : 0;
}

static void unacquire_mouse (int num)
{
    unacquire (di_mouse[num].lpdi, L"mouse");
    if (di_mouse[num].acquired > 0) {
	if (di_mouse[num].rawinput)
	    rawmouse--;
	else if (di_mouse[num].superdevice)
	    supermouse--;
	else if (di_mouse[num].wininput)
	    winmouse--;
	else
	    normalmouse--;
	di_mouse[num].acquired = 0;
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
	elements = DI_BUFFER;
	hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
	if (SUCCEEDED (hr) || hr == DI_BUFFEROVERFLOW) {
	    if (supermouse && !did->superdevice)
		continue;
	    for (j = 0; j < elements; j++) {
		int dimofs = didod[j].dwOfs;
		int data = didod[j].dwData;
		int state = (data & 0x80) ? 1 : 0;
#ifdef DI_DEBUG2
		write_log (L"MOUSE: %d OFF=%d DATA=%d STATE=%d\n", i, dimofs, data, state);
#endif
		if (istest || isfocus () > 0) {
		    for (k = 0; k < did->axles; k++) {
		        if (did->axismappings[k] == dimofs)
			    setmousestate (i, k, data, 0);
		    }
		    for (k = 0; k < did->buttons; k++) {
			if (did->buttonmappings[k] == dimofs) {
			    if (did->axisparent[k] >= 0) {
				int dir = did->axisparentdir[k];
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
				if ((currprefs.win32_middle_mouse && k != 2) || !(currprefs.win32_middle_mouse))
				    setmousebuttonstate (i, k, state);
			    }
			}
		    }
		}
		if (!istest && isfocus () && currprefs.win32_middle_mouse && dimofs == DIMOFS_BUTTON2 && state) {
		    if (isfullscreen () > 0)
		        minimizewindow ();
		    if (mouseactive)
		        setmouseactive (0);
		}
	    }
	} else if (hr == DIERR_INPUTLOST) {
	    acquire (lpdi, L"mouse");
	} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
	    acquire (lpdi, L"mouse");
	}
	IDirectInputDevice8_Poll (lpdi);
    }
}

static int get_mouse_flags (int num)
{
    if (di_mouse[num].rawinput)
	return 0;
    if (di_mouse[num].catweasel)
	return 0;
    if (di_mouse[num].wininput == 1 && !rawinput_available)
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
    if (name)
	_stprintf (name, L"[%02X] %s", di_keyboard[kb].buttonmappings[num], di_keyboard[kb].buttonname[num]);
    if (code)
	*code = di_keyboard[kb].buttonmappings[num];
    return IDEV_WIDGET_KEY;
}

static BYTE ledkeystate[256];

static uae_u32 get_leds (void)
{
    uae_u32 led = 0;

    GetKeyboardState (ledkeystate);
    if (ledkeystate[VK_NUMLOCK] & 1)
	led |= KBLED_NUMLOCK;
    if (ledkeystate[VK_CAPITAL] & 1)
	led |= KBLED_CAPSLOCK;
    if (ledkeystate[VK_SCROLL] & 1)
	led |= KBLED_SCROLLLOCK;

    if (currprefs.win32_kbledmode) {
	oldusbleds = led;
    } else if (!currprefs.win32_kbledmode && kbhandle != INVALID_HANDLE_VALUE) {
#ifdef WINDDK
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
	    led |= KBLED_NUMLOCK;
	if (OutputBuffer.LedFlags & KEYBOARD_CAPS_LOCK_ON)
	    led |= KBLED_CAPSLOCK;
	if (OutputBuffer.LedFlags & KEYBOARD_SCROLL_LOCK_ON) led
	    |= KBLED_SCROLLLOCK;
#endif
    }
    return led;
}

static void kbevt (uae_u8 vk, uae_u8 sc)
{
    if (0) {
	INPUT inp[2] = { 0 };
	int i;

	for (i = 0; i < 2; i++) {
	    inp[i].type = INPUT_KEYBOARD;
	    inp[i].ki.wVk = vk;
	}
	inp[1].ki.dwFlags |= KEYEVENTF_KEYUP;
	SendInput (1, inp, sizeof (INPUT));
	SendInput (1, inp + 1, sizeof (INPUT));
    } else {
	keybd_event (vk, 0, 0, 0);
	keybd_event (vk, 0, KEYEVENTF_KEYUP, 0);
    }
}

static void set_leds (uae_u32 led)
{
    if (currprefs.win32_kbledmode) {
	if ((oldusbleds & KBLED_NUMLOCK) != (led & KBLED_NUMLOCK))
	    kbevt (VK_NUMLOCK, 0x45);
	if ((oldusbleds & KBLED_CAPSLOCK) != (led & KBLED_CAPSLOCK))
	    kbevt (VK_CAPITAL, 0x3a);
	if ((oldusbleds & KBLED_SCROLLLOCK) != (led & KBLED_SCROLLLOCK))
	    kbevt (VK_SCROLL, 0x46);
	oldusbleds = led;
    } else if (kbhandle != INVALID_HANDLE_VALUE) {
#ifdef WINDDK
	KEYBOARD_INDICATOR_PARAMETERS InputBuffer;
	ULONG DataLength = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
	ULONG ReturnedLength;

	memset (&InputBuffer, 0, sizeof (InputBuffer));
	if (led & KBLED_NUMLOCK)
	    InputBuffer.LedFlags |= KEYBOARD_NUM_LOCK_ON;
	if (led & KBLED_CAPSLOCK)
	    InputBuffer.LedFlags |= KEYBOARD_CAPS_LOCK_ON;
	if (led & KBLED_SCROLLLOCK)
	    InputBuffer.LedFlags |= KEYBOARD_SCROLL_LOCK_ON;
	if (!DeviceIoControl (kbhandle, IOCTL_KEYBOARD_SET_INDICATORS,
	    &InputBuffer, DataLength, NULL, 0, &ReturnedLength, NULL))
		write_log (L"kbleds: DeviceIoControl() failed %d\n", GetLastError());
#endif
    }
}

static void update_leds (void)
{
    if (!currprefs.keyboard_leds_in_use)
	return;
    if (newleds != oldusedleds) {
	oldusedleds = newleds;
	set_leds (newleds);
    }
}

void indicator_leds (int num, int state)
{
    int i;

    if (!currprefs.keyboard_leds_in_use)
	return;
    for (i = 0; i < 3; i++) {
	if (currprefs.keyboard_leds[i] == num + 1) {
	    newleds &= ~(1 << i);
	    if (state)
		newleds |= (1 << i);
	}
    }
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
    oldusedleds = -1;
    keyboard_inited = 1;
    for (i = 0; i < num_keyboard; i++) {
	struct didata *did = &di_keyboard[i];
	if (did->connection == DIDC_DX) {
	    hr = IDirectInput8_CreateDevice (g_lpdi, &did->iguid, &lpdi, NULL);
	    if (SUCCEEDED (hr)) {
		hr = IDirectInputDevice8_SetDataFormat (lpdi, &c_dfDIKeyboard);
		if (FAILED (hr))
		    write_log (L"keyboard setdataformat failed, %s\n", DXError (hr));
		memset (&dipdw, 0, sizeof (dipdw));
		dipdw.diph.dwSize = sizeof (DIPROPDWORD);
		dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
		dipdw.diph.dwObj = 0;
		dipdw.diph.dwHow = DIPH_DEVICE;
		dipdw.dwData = DI_KBBUFFER;
		hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
		if (FAILED (hr))
		    write_log (L"keyboard setpropertry failed, %s\n", DXError (hr));
		IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, did, DIDFT_ALL);
		sortobjects (did, did->axismappings, did->axissort, did->axisname, did->axistype, did->axles);
		sortobjects (did, did->buttonmappings, did->buttonsort, did->buttonname, 0, did->buttons);
		did->lpdi = lpdi;
	    } else
		write_log (L"keyboard CreateDevice failed, %s\n", DXError (hr));
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
    int i;
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (di_keycodes[i][key])
	    return 1;
    }
    return 0;
}

void release_keys (void)
{
    int i, j;

    for (j = 0; j < MAX_INPUT_DEVICES; j++) {
	for (i = 0; i < MAX_KEYCODES; i++) {
	    if (di_keycodes[j][i]) {
		my_kbd_handler (j, i, 0);
		di_keycodes[j][i] = 0;
	    }
	}
    }
}


static int acquire_kb (int num, int flags)
{
    LPDIRECTINPUTDEVICE8 lpdi = di_keyboard[num].lpdi;

    unacquire (lpdi, L"keyboard");
    if (currprefs.keyboard_leds_in_use) {
#ifdef WINDDK
	if (!currprefs.win32_kbledmode) {
	    if (DefineDosDevice (DDD_RAW_TARGET_PATH, L"Kbd", L"\\Device\\KeyboardClass0")) {
		kbhandle = CreateFile (L"\\\\.\\Kbd", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (kbhandle == INVALID_HANDLE_VALUE) {
		    write_log (L"kbled: CreateFile failed, error %d\n", GetLastError());
		    currprefs.win32_kbledmode = 1;
		}
	    } else {
		currprefs.win32_kbledmode = 1;
		write_log (L"kbled: DefineDosDevice failed, error %d\n", GetLastError());
	    }
	}
#else
	currprefs.kbledmode = 1;
#endif
	oldleds = get_leds ();
	if (oldusedleds < 0)
	    oldusedleds = newleds = oldleds;
	set_leds (oldusedleds);
    }

    setcoop (&di_keyboard[num], DISCL_NOWINKEY | DISCL_FOREGROUND | DISCL_EXCLUSIVE, L"keyboard");
    kb_do_refresh = ~0;
    di_keyboard[num].acquired = -1;
    if (acquire (lpdi, L"keyboard")) {
	if (di_keyboard[num].rawinput)
	    rawkb++;
	else if (di_keyboard[num].superdevice)
	    superkb++;
	else
	    normalkb++;
	di_keyboard[num].acquired = 1;
    }
    register_rawinput ();
    return di_keyboard[num].acquired > 0 ? 1 : 0;
}

static void unacquire_kb (int num)
{
    LPDIRECTINPUTDEVICE8 lpdi = di_keyboard[num].lpdi;

    unacquire (lpdi, L"keyboard");
    if (di_keyboard[num].acquired > 0) {
	if (di_keyboard[num].rawinput)
	    rawkb--;
	else if (di_keyboard[num].superdevice)
	    superkb--;
	else
	    normalkb--;
	di_keyboard[num].acquired = 0;
    }
    release_keys ();

    if (currprefs.keyboard_leds_in_use) {
	if (oldusedleds >= 0) {
	    set_leds (oldleds);
	    oldusedleds = oldleds;
	}
#ifdef WINDDK
	if (kbhandle != INVALID_HANDLE_VALUE) {
	    CloseHandle (kbhandle);
	    DefineDosDevice (DDD_REMOVE_DEFINITION, L"Kbd", NULL);
	    kbhandle = INVALID_HANDLE_VALUE;
	}
#endif
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
	    if (kc[i] != di_keycodes[num][i]) {
		write_log (L"%02X -> %d\n", i, kc[i]);
		di_keycodes[num][i] = kc[i];
		my_kbd_handler (num, i, kc[i]);
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

    update_leds ();
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	struct didata *did = &di_keyboard[i];
	if (!did->acquired)
	    continue;
	if (isfocus () == 0) {
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
	elements = DI_KBBUFFER;
	hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof(DIDEVICEOBJECTDATA), didod, &elements, 0);
	if ((SUCCEEDED (hr) || hr == DI_BUFFEROVERFLOW) && isfocus ()) {
	    if (did->superdevice && (normalkb || rawkb))
		continue;
	    for (j = 0; j < elements; j++) {
		int scancode = didod[j].dwOfs;
		int pressed = (didod[j].dwData & 0x80) ? 1 : 0;
		//write_log (L"%d: %02X %d\n", j, scancode, pressed);
		if (!istest)
		    scancode = keyhack (scancode, pressed, i);
		if (scancode < 0)
		    continue;
		di_keycodes[i][scancode] = pressed;
		if (istest) {
		    inputdevice_do_keyboard (scancode, pressed);
		} else {
		    if (stopoutput == 0)
			my_kbd_handler (i, scancode, pressed);
		}
	    }
	} else if (hr == DIERR_INPUTLOST) {
	    acquire_kb (i, 0);
	    kb_do_refresh |= 1 << i;
	} else if (did->acquired && hr == DIERR_NOTACQUIRED) {
	    acquire_kb (i, 0);
	}
	IDirectInputDevice8_Poll (lpdi);
    }
#ifdef CATWEASEL
    if (isfocus ()) {
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

static int get_kb_flags (int kb)
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
    if (num >= did->axles && num < did->axles + did->buttons) {
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

    if (IGNOREEVERYTHING)
	return;

    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	struct didata *did = &di_joystick[i];
	if (!did->acquired)
	    continue;
	if (did->connection == DIDC_CAT) {
	    if (getjoystickstate (i) && isfocus ()) {
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
	}
	lpdi = did->lpdi;
	if (!lpdi || did->connection != DIDC_DX)
	    continue;
	elements = DI_BUFFER;
	hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
	if ((SUCCEEDED (hr) || hr == DI_BUFFEROVERFLOW) && isfocus ()) {
	    for (j = 0; j < elements; j++) {
		int dimofs = didod[j].dwOfs;
		int data = didod[j].dwData;
		int data2 = data;
		int state = (data & 0x80) ? 1 : 0;
		data -= 32768;

		for (k = 0; k < did->buttons; k++) {

		    if (did->axisparent[k] >= 0 && did->buttonmappings[k] == dimofs) {

			int bstate = -1;
			int axis = did->axisparent[k];
			int dir = did->axisparentdir[k];

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
			if (bstate >= 0)
			    setjoybuttonstate (i, k, bstate);
#ifdef DI_DEBUG2
			write_log (L"AB:NUM=%d OFF=%d AXIS=%d DIR=%d NAME=%s VAL=%d STATE=%d\n",
			    k, dimofs, axis, dir, did->buttonname[k], data, state);
#endif

		    } else if (did->axisparent[k] < 0 && did->buttonmappings[k] == dimofs) {
#ifdef DI_DEBUG2
			write_log (L"B:NUM=%d OFF=%d NAME=%s VAL=%d STATE=%d\n",
			    k, dimofs, did->buttonname[k], data, state);
#endif
			setjoybuttonstate (i, k, state);
		    }
		}

		for (k = 0; k < did->axles; k++) {
		    if (did->axismappings[k] == dimofs) {
			if (did->axistype[k] == 1) {
			    setjoystickstate (i, k, (data2 >= 20250 && data2 <= 33750) ? -1 : (data2 >= 2250 && data2 <= 15750) ? 1 : 0, 1);
			} else if (did->axistype[k] == 2) {
			    setjoystickstate (i, k, ((data2 >= 29250 && data2 <= 33750) || (data2 >= 0 && data2 <= 6750)) ? -1 : (data2 >= 11250 && data2 <= 24750) ? 1 : 0, 1);
#ifdef DI_DEBUG2
			    write_log (L"P:NUM=%d OFF=%d NAME=%s VAL=%d\n", k, dimofs, did->axisname[k], data2);
#endif
			} else if (did->axistype[k] == 0) {
#ifdef DI_DEBUG2
			    if (data < -20000 || data > 20000)
				write_log (L"A:NUM=%d OFF=%d NAME=%s VAL=%d\n", k, dimofs, did->axisname[k], data);
#endif
			    setjoystickstate (i, k, data, 32768);
			}
		    }
		}

	    }

	} else if (hr == DIERR_INPUTLOST) {
	    acquire (lpdi, L"joystick");
	} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
	    acquire (lpdi, L"joystick");
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
	    hr = IDirectInput8_CreateDevice (g_lpdi, &did->iguid, &lpdi, NULL);
	    if (SUCCEEDED (hr)) {
		hr = IDirectInputDevice8_SetDataFormat (lpdi, &c_dfDIJoystick);
		if (SUCCEEDED (hr)) {
		    did->lpdi = lpdi;
		    IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, (void*)did, DIDFT_ALL);
		    fixbuttons (did);
		    fixthings (did);
		    sortobjects (did, did->axismappings, did->axissort, did->axisname, did->axistype, did->axles);
		    sortobjects (did, did->buttonmappings, did->buttonsort, did->buttonname, 0, did->buttons);
		}
	    } else {
		write_log (L"joystick createdevice failed, %s\n", DXError (hr));
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
    LPDIRECTINPUTDEVICE8 lpdi = di_joystick[num].lpdi;
    DIPROPDWORD dipdw;
    HRESULT hr;

    unacquire (lpdi, L"joystick");
    if (di_joystick[num].connection == DIDC_DX && lpdi) {
	setcoop (&di_joystick[num], flags ? (DISCL_FOREGROUND | DISCL_EXCLUSIVE) : (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE), L"joystick");
	memset (&dipdw, 0, sizeof (dipdw));
	dipdw.diph.dwSize = sizeof (DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = DI_BUFFER;
	hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
	if (FAILED (hr))
	    write_log (L"joystick setproperty failed, %s\n", DXError (hr));
	di_joystick[num].acquired = acquire (lpdi, L"joystick") ? 1 : -1;
    } else {
	di_joystick[num].acquired = 1;
    }
    return di_joystick[num].acquired > 0 ? 1 : 0;
}

static void unacquire_joystick (int num)
{
    unacquire (di_joystick[num].lpdi, L"joystick");
    di_joystick[num].acquired = 0;
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

int input_get_default_mouse (struct uae_input_device *uid, int i, int port)
{
    struct didata *did;

    if (i >= num_mouse)
	return 0;
    did = &di_mouse[i];
    if (did->wininput)
	port = 0;
    uid[i].eventid[ID_AXIS_OFFSET + 0][0] = port ? INPUTEVENT_MOUSE2_HORIZ : INPUTEVENT_MOUSE1_HORIZ;
    uid[i].eventid[ID_AXIS_OFFSET + 1][0] = port ? INPUTEVENT_MOUSE2_VERT : INPUTEVENT_MOUSE1_VERT;
    uid[i].eventid[ID_AXIS_OFFSET + 2][0] = port ? 0 : INPUTEVENT_MOUSE1_WHEEL;
    uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON;
    uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON;
    uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
    if (port == 0) { /* map back and forward to ALT+LCUR and ALT+RCUR */
	if (isrealbutton (did, 3)) {
	    uid[i].eventid[ID_BUTTON_OFFSET + 3][0] = INPUTEVENT_KEY_ALT_LEFT;
	    uid[i].eventid[ID_BUTTON_OFFSET + 3][1] = INPUTEVENT_KEY_CURSOR_LEFT;
	    if (isrealbutton (did, 4)) {
		uid[i].eventid[ID_BUTTON_OFFSET + 4][0] = INPUTEVENT_KEY_ALT_LEFT;
		uid[i].eventid[ID_BUTTON_OFFSET + 4][1] = INPUTEVENT_KEY_CURSOR_RIGHT;
	    }
	}
    }
    if (i == 0)
	return 1;
    return 0;
}

int input_get_default_lightpen (struct uae_input_device *uid, int i, int port)
{
    struct didata *did;

    if (i >= num_mouse)
	return 0;
    did = &di_mouse[i];
    if (did->wininput)
	port = 0;
    uid[i].eventid[ID_AXIS_OFFSET + 0][0] = INPUTEVENT_LIGHTPEN_HORIZ;
    uid[i].eventid[ID_AXIS_OFFSET + 1][0] = INPUTEVENT_LIGHTPEN_VERT;
    uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
    if (i == 0)
	return 1;
    return 0;
}

int input_get_default_joystick (struct uae_input_device *uid, int i, int port, int cd32)
{
    int j;
    struct didata *did;
    int h, v;

    if (i >= num_joystick)
	return 0;
    did = &di_joystick[i];
    if (port >= 2) {
	h = port == 3 ? INPUTEVENT_PAR_JOY2_HORIZ : INPUTEVENT_PAR_JOY1_HORIZ;
	v = port == 3 ? INPUTEVENT_PAR_JOY2_VERT : INPUTEVENT_PAR_JOY1_VERT;
    } else {
	h = port ? INPUTEVENT_JOY2_HORIZ : INPUTEVENT_JOY1_HORIZ;;
	v = port ? INPUTEVENT_JOY2_VERT : INPUTEVENT_JOY1_VERT;
    }
    uid[i].eventid[ID_AXIS_OFFSET + 0][0] = h;
    uid[i].eventid[ID_AXIS_OFFSET + 1][0] = v;

    if (port >= 2) {
	uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port == 3 ? INPUTEVENT_PAR_JOY2_FIRE_BUTTON : INPUTEVENT_PAR_JOY1_FIRE_BUTTON;
    } else {
	uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON;
	if (isrealbutton (did, 1))
	    uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON;
	if (isrealbutton (did, 2))
	    uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
    }

    for (j = 2; j < MAX_MAPPINGS - 1; j++) {
	int am = did->axismappings[j];
	if (am == DIJOFS_POV(0) || am == DIJOFS_POV(1) || am == DIJOFS_POV(2) || am == DIJOFS_POV(3)) {
	    uid[i].eventid[ID_AXIS_OFFSET + j + 0][0] = h;
	    uid[i].eventid[ID_AXIS_OFFSET + j + 1][0] = v;
	    j++;
	}
    }
    if (cd32) {
	uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_CD32_RED : INPUTEVENT_JOY1_CD32_RED;
	if (isrealbutton (did, 1))
	    uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_CD32_BLUE : INPUTEVENT_JOY1_CD32_BLUE;
	if (isrealbutton (did, 2))
	    uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_CD32_YELLOW : INPUTEVENT_JOY1_CD32_YELLOW;
	if (isrealbutton (did, 3))
	    uid[i].eventid[ID_BUTTON_OFFSET + 3][0] = port ? INPUTEVENT_JOY2_CD32_GREEN : INPUTEVENT_JOY1_CD32_GREEN;
	if (isrealbutton (did, 4))
	    uid[i].eventid[ID_BUTTON_OFFSET + 4][0] = port ? INPUTEVENT_JOY2_CD32_FFW : INPUTEVENT_JOY1_CD32_FFW;
	if (isrealbutton (did, 5))
	    uid[i].eventid[ID_BUTTON_OFFSET + 5][0] = port ? INPUTEVENT_JOY2_CD32_RWD : INPUTEVENT_JOY1_CD32_RWD;
	if (isrealbutton (did, 6))
	    uid[i].eventid[ID_BUTTON_OFFSET + 6][0] = port ? INPUTEVENT_JOY2_CD32_PLAY :  INPUTEVENT_JOY1_CD32_PLAY;
    }
    if (i == 0)
	return 1;
    return 0;
}

int input_get_default_joystick_analog (struct uae_input_device *uid, int i, int port)
{
    int j;
    struct didata *did;

    if (i >= num_joystick)
	return 0;
    did = &di_joystick[i];
    uid[i].eventid[ID_AXIS_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_HORIZ_POT : INPUTEVENT_JOY1_HORIZ_POT;
    uid[i].eventid[ID_AXIS_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_VERT_POT : INPUTEVENT_JOY1_VERT_POT;
    uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_LEFT : INPUTEVENT_JOY1_LEFT;
    if (isrealbutton (did, 1))
	uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_RIGHT : INPUTEVENT_JOY1_RIGHT;
    if (isrealbutton (did, 2))
	uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_UP : INPUTEVENT_JOY1_UP;
    if (isrealbutton (did, 3))
	uid[i].eventid[ID_BUTTON_OFFSET + 3][0] = port ? INPUTEVENT_JOY2_DOWN : INPUTEVENT_JOY1_DOWN;
    for (j = 2; j < MAX_MAPPINGS - 1; j++) {
	int am = did->axismappings[j];
	if (am == DIJOFS_POV(0) || am == DIJOFS_POV(1) || am == DIJOFS_POV(2) || am == DIJOFS_POV(3)) {
	    uid[i].eventid[ID_AXIS_OFFSET + j + 0][0] = port ? INPUTEVENT_JOY2_HORIZ_POT : INPUTEVENT_JOY1_HORIZ_POT;
	    uid[i].eventid[ID_AXIS_OFFSET + j + 1][0] = port ? INPUTEVENT_JOY2_VERT_POT : INPUTEVENT_JOY1_VERT_POT;
	    j++;
	}
    }
    if (i == 0)
	return 1;
    return 0;
}

