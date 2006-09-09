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

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include <windows.h>
#include <dinput.h>

#include "sysdeps.h"
#include "options.h"
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
    int disabled;
    int acquired;
    int priority;
    int superdevice;
    GUID guid;
    char *name;
    char *sortname;

    int connection;
    LPDIRECTINPUTDEVICE8 lpdi;
    HANDLE rawinput;
    int wininput;
    int catweasel;

    int axles;
    int buttons;
    int axismappings[MAX_MAPPINGS];
    char *axisname[MAX_MAPPINGS];
    int axissort[MAX_MAPPINGS];
    int axistype[MAX_MAPPINGS];
    int buttonmappings[MAX_MAPPINGS];
    char *buttonname[MAX_MAPPINGS];
    int buttonsort[MAX_MAPPINGS];
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
static int normalmouse, supermouse, rawmouse, winmouse, winmousenumber, winmousemode;
static int normalkb, superkb, rawkb;

int no_rawinput;

int dinput_winmouse (void)
{
    if (winmouse)
	return winmousenumber;
    return -1;
}
int dinput_winmousemode (void)
{
    if (winmouse)
	return winmousemode;
    return 0;
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

static int rawinput_available, rawinput_registered;

static void unregister_rawinput (void)
{
}

static int register_rawinput (void)
{
    RAWINPUTDEVICE rid[1];

    if (!rawinput_available)
	return 0;
    memset (rid, 0, sizeof (rid));
    /* mouse */
    rid[0].usUsagePage = 1;
    rid[0].usUsage = 2;
    rid[0].dwFlags = 0;
#if 0
    /* keyboard */
    rid[1].usUsagePage = 1;
    rid[1].usUsage = 6;
    rid[1].dwFlags = 0;
#endif

    if (pRegisterRawInputDevices(rid, sizeof (rid) / sizeof (RAWINPUTDEVICE), sizeof (RAWINPUTDEVICE)) == FALSE) {
	write_log ("RAWINPUT registration failed %d\n", GetLastError ());
	return 0;
    }
    rawinput_registered = 1;
    return 1;
}

static void cleardid(struct didata *did)
{
    int i;
    memset (did, 0, sizeof (*did));
    for (i = 0; i < MAX_MAPPINGS; i++) {
	did->axismappings[i] = -1;
	did->buttonmappings[i] = -1;
    }
}

static int initialize_catweasel(void)
{
    int j, i;
    char tmp[MAX_DPATH];
    struct didata *did;

    if (catweasel_ismouse()) {
	for (i = 0; i < 2 && num_mouse < MAX_INPUT_DEVICES; i++) {
	    did = di_mouse;
	    did += num_mouse;
	    cleardid(did);
	    did->connection = DIDC_CAT;
	    did->catweasel = i;
	    did->name = my_strdup ("Catweasel mouse");
	    did->sortname = my_strdup (tmp);
	    did->buttons = 3;
	    did->axles = 2;
	    did->axistype[0] = 1;
	    did->axissort[0] = 0;
	    did->axisname[0] = my_strdup ("X-Axis");
	    did->axistype[1] = 1;
	    did->axissort[1] = 1;
	    did->axisname[1] = my_strdup ("Y-Axis");
	    for (j = 0; j < did->buttons; j++) {
		did->buttonsort[j] = j;
		sprintf (tmp, "Button %d", j + 1);
		did->buttonname[j] = my_strdup (tmp);
	    }
	    did->priority = -1;
	    num_mouse++;
	}
    }
    if (catweasel_isjoystick()) {
	for (i = 0; i < 2 && num_joystick < MAX_INPUT_DEVICES; i++) {
	    did = di_joystick;
	    did += num_joystick;
	    cleardid(did);
	    did->connection = DIDC_CAT;
	    did->catweasel = i;
	    sprintf (tmp, "Catweasel joystick");
	    did->name = my_strdup (tmp);
	    did->sortname = my_strdup (tmp);
	    did->buttons = (catweasel_isjoystick() & 0x80) ? 3 : 1;
	    did->axles = 2;
	    did->axistype[0] = 1;
	    did->axissort[0] = 0;
	    did->axisname[0] = my_strdup ("X-Axis");
	    did->axistype[1] = 1;
	    did->axissort[1] = 1;
	    did->axisname[1] = my_strdup ("Y-Axis");
	    for (j = 0; j < did->buttons; j++) {
		did->buttonsort[j] = j;
		sprintf (tmp, "Button %d", j + 1);
		did->buttonname[j] = my_strdup (tmp);
	    }
	    did->priority = -1;
	    num_joystick++;
	}
    }
    return 1;
}


#define RDP_MOUSE1 "\\??\\Root#RDP_MOU#"
#define RDP_MOUSE2 "\\\\?\\Root#RDP_MOU#"

static int initialize_rawinput (void)
{
    RAWINPUTDEVICELIST *ridl = 0;
    int num = 500, gotnum, i, bufsize, vtmp;
    int rnum_mouse, rnum_kb, rnum_raw;
    uae_u8 *buf = 0;
    int rmouse = 0, rkb = 0;
    char tmp[100];

    if (no_rawinput)
	goto error;
    pRegisterRawInputDevices = (REGISTERRAWINPUTDEVICES)GetProcAddress(
	GetModuleHandle("user32.dll"), "RegisterRawInputDevices");
    pGetRawInputData = (GETRAWINPUTDATA)GetProcAddress(
	GetModuleHandle("user32.dll"), "GetRawInputData");
    pGetRawInputDeviceList = (GETRAWINPUTDEVICELIST)GetProcAddress(
	GetModuleHandle("user32.dll"), "GetRawInputDeviceList");
    pGetRawInputDeviceInfo = (GETRAWINPUTDEVICEINFO)GetProcAddress(
	GetModuleHandle("user32.dll"), "GetRawInputDeviceInfoA");
    pGetRawInputBuffer = (GETRAWINPUTBUFFER)GetProcAddress(
	GetModuleHandle("user32.dll"), "GetRawInputBuffer");
    pDefRawInputProc = (DEFRAWINPUTPROC)GetProcAddress(
	GetModuleHandle("user32.dll"), "DefRawInputProc");

    if (!pRegisterRawInputDevices || !pGetRawInputData || !pGetRawInputDeviceList ||
	!pGetRawInputDeviceInfo || !pGetRawInputBuffer || !pDefRawInputProc)
	goto error;

    ridl = malloc (sizeof(RAWINPUTDEVICELIST) * num);
    memset (ridl, 0, sizeof (RAWINPUTDEVICELIST) * num);
    bufsize = 1000;
    buf = malloc (bufsize);

    gotnum = pGetRawInputDeviceList(ridl, &num, sizeof (RAWINPUTDEVICELIST));
    if (gotnum <= 0) {
	write_log ("RAWINPUT didn't find any devices");
	goto error;
    }
    write_log ("RAWINPUT: found %d devices\n", gotnum);
    rnum_raw = rnum_mouse = rnum_kb = 0;
    for (i = 0; i < gotnum; i++) {
	int type = ridl[i].dwType;
	HANDLE h = ridl[i].hDevice;
	vtmp = bufsize;
	pGetRawInputDeviceInfo (h, RIDI_DEVICENAME, buf, &vtmp);

	if (!memcmp (RDP_MOUSE1, buf, strlen (RDP_MOUSE1)))
	    continue;
	if (!memcmp (RDP_MOUSE2, buf, strlen (RDP_MOUSE2)))
	    continue;
	if (type == RIM_TYPEMOUSE)
	    rnum_mouse++;
	if (type == RIM_TYPEKEYBOARD)
	    rnum_kb++;
    }

    for (i = 0; i < gotnum; i++) {
	HANDLE h = ridl[i].hDevice;
	int type = ridl[i].dwType;

	if (/* type == RIM_TYPEKEYBOARD || */ type == RIM_TYPEMOUSE) {
	    struct didata *did = type == RIM_TYPEMOUSE ? di_mouse : di_keyboard;
	    PRID_DEVICE_INFO rdi;
	    int v, j;

	    vtmp = bufsize;
	    pGetRawInputDeviceInfo (h, RIDI_DEVICENAME, buf, &vtmp);

	    if (did == di_mouse) {
		if (!memcmp (RDP_MOUSE1, buf, strlen (RDP_MOUSE1)))
		    continue;
		if (!memcmp (RDP_MOUSE2, buf, strlen (RDP_MOUSE2)))
		    continue;
		if (rnum_mouse < 2)
		    continue;
		if (num_mouse >= MAX_INPUT_DEVICES - 1) /* leave space for Windows mouse */
		    continue;
		did += num_mouse;
		num_mouse++;
		rmouse++;
		v = rmouse;
	    } else if (did == di_keyboard) {
		if (rnum_kb < 2)
		    continue;
		if (num_keyboard >= MAX_INPUT_DEVICES)
		    continue;
		did += num_keyboard;
		num_keyboard++;
		rkb++;
		v = rkb;
	    }

	    rnum_raw++;
	    cleardid(did);
	    sprintf (tmp, "%s", type == RIM_TYPEMOUSE ? "RAW Mouse" : "RAW Keyboard");
	    did->name = my_strdup (tmp);
	    did->rawinput = h;
	    did->connection = DIDC_RAW;

	    write_log ("%p %s: ", h, type == RIM_TYPEMOUSE ? "mouse" : "keyboard");
	    did->sortname = my_strdup (buf);
	    write_log ("'%s'\n", buf);
	    vtmp = bufsize;
	    rdi = (PRID_DEVICE_INFO)buf;
	    rdi->cbSize = sizeof (RID_DEVICE_INFO);
	    pGetRawInputDeviceInfo (h, RIDI_DEVICEINFO, buf, &vtmp);
	    if (type == RIM_TYPEMOUSE) {
		PRID_DEVICE_INFO_MOUSE rdim = &rdi->mouse;
		write_log ("id=%d buttons=%d rate=%d", 
		    rdim->dwId, rdim->dwNumberOfButtons, rdim->dwSampleRate);
		did->buttons = rdim->dwNumberOfButtons;
		did->axles = 3;
		did->axistype[0] = 1;
		did->axissort[0] = 0;
		did->axisname[0] = my_strdup ("X-Axis");
		did->axistype[1] = 1;
		did->axissort[1] = 1;
		did->axisname[1] = my_strdup ("Y-Axis");
		did->axistype[2] = 1;
		did->axissort[2] = 2;
		did->axisname[2] = my_strdup ("Wheel");
		for (j = 0; j < did->buttons; j++) {
		    did->buttonsort[j] = j;
		    sprintf (tmp, "Button %d", j + 1);
		    did->buttonname[j] = my_strdup (tmp);
		}
		did->priority = -1;
	    } else {
		PRID_DEVICE_INFO_KEYBOARD rdik = &rdi->keyboard;
		write_log ("type=%d sub=%d mode=%d fkeys=%d indicators=%d tkeys=%d",
		    rdik->dwType, rdik->dwSubType, rdik->dwKeyboardMode,
		    rdik->dwNumberOfFunctionKeys, rdik->dwNumberOfIndicators, rdik->dwNumberOfKeysTotal);
	    }
	    write_log("\n");
	}
    }

    free (ridl);
    free (buf);
    if (rnum_raw > 0)
	rawinput_available = 1;
    return 1;

    error:
    write_log ("RAWINPUT not available or failed to initialize\n");
    free (ridl);
    free (buf);
    return 0;
}

static void initialize_windowsmouse (void)
{
    struct didata *did = di_mouse;
    char tmp[100], *name;
    int i, j;

    did += num_mouse;
    for (i = 0; i < 2; i++) {
	if (num_mouse >= MAX_INPUT_DEVICES)
	    return;
	num_mouse++;
	name = (i == 0) ? "Windows mouse" : "Mousehack mouse";
	did->connection = DIDC_WIN;
	did->name = my_strdup (i ? "Mousehack mouse  (Required for tablets)" : "Windows mouse");
	did->sortname = my_strdup (i ? "Windowsmouse2" : "Windowsmouse1");
	did->buttons = GetSystemMetrics (SM_CMOUSEBUTTONS);
	if (did->buttons < 3)
	    did->buttons = 3;
	if (did->buttons > 5)
	    did->buttons = 5; /* no non-direcinput support for >5 buttons */
	if (did->buttons > 3 && !os_winnt)
	    did->buttons = 3; /* Windows 98/ME support max 3 non-DI buttons */
	did->axles = 3;
	did->axistype[0] = 1;
	did->axissort[0] = 0;
	did->axisname[0] = my_strdup ("X-Axis");
	did->axistype[1] = 1;
	did->axissort[1] = 1;
	did->axisname[1] = my_strdup ("Y-Axis");
	if (did->axles > 2) {
	    did->axistype[2] = 1;
	    did->axissort[2] = 2;
	    did->axisname[2] = my_strdup ("Wheel");
	}
	for (j = 0; j < did->buttons; j++) {
	    did->buttonsort[j] = j;
	    sprintf (tmp, "Button %d", j + 1);
	    did->buttonname[j] = my_strdup (tmp);
	}
	did->priority = 2;
	did->wininput = i + 1;
	did++;
    }
}

static void handle_rawinput_2 (RAWINPUT *raw)
{
    int i, num;
    struct didata *did;

    if (raw->header.dwType == RIM_TYPEMOUSE) {
	PRAWMOUSE rm = &raw->data.mouse;
	
	for (num = 0; num < num_mouse; num++) {
	    did = &di_mouse[num];
	    if (!did->disabled && did->rawinput == raw->header.hDevice)
		break;
	}
#ifdef DI_DEBUG2
	write_log ("HANDLE=%08.8x %04.4x %04.4x %04.4x %08.8x %3d %3d %08.8x M=%d\n",
	    raw->header.hDevice,
	    rm->usFlags, 
	    rm->usButtonFlags, 
	    rm->usButtonData, 
	    rm->ulRawButtons, 
	    rm->lLastX, 
	    rm->lLastY, 
	    rm->ulExtraInformation, num < num_mouse ? num + 1 : -1);
#endif
	if (num == num_mouse) {
	    return;
	}

	if (focus) {
	    if (mouseactive || isfullscreen ()) {
		for (i = 0; i < (5 > did->buttons ? did->buttons : 5); i++) {
		    if (rm->usButtonFlags & (3 << (i * 2)))
			setmousebuttonstate (num, i, (rm->usButtonFlags & (1 << (i * 2))) ? 1 : 0);
		}
		if (did->buttons > 5) {
		    for (i = 5; i < did->buttons; i++)
			setmousebuttonstate (num, i, (rm->ulRawButtons & (1 << i)) ? 1 : 0);
		}
		if (did->buttons >= 3 && (rm->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)) {
		    if (currprefs.win32_middle_mouse) {
			if (isfullscreen ())
			    minimizewindow ();
			if (mouseactive)
			    setmouseactive(0);
		    }
		}
	    }
	    if (rm->usButtonFlags & RI_MOUSE_WHEEL)
		setmousestate (num, 2, (int)rm->usButtonData, 0);
	    setmousestate (num, 0, rm->lLastX, (rm->usFlags & MOUSE_MOVE_ABSOLUTE) ? 1 : 0);
	    setmousestate (num, 1, rm->lLastY, (rm->usFlags & MOUSE_MOVE_ABSOLUTE) ? 1 : 0);
	}

    } else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
#ifdef DI_DEBUG2
	write_log ("HANDLE=%x CODE=%x Flags=%x VK=%x MSG=%x EXTRA=%x\n",
	    raw->header.hDevice,
	    raw->data.keyboard.MakeCode,
	    raw->data.keyboard.Flags,
	    raw->data.keyboard.VKey,
	    raw->data.keyboard.Message,
	    raw->data.keyboard.ExtraInformation);
#endif
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
    pGetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
    if (dwSize >= sizeof (lpb))
	return;
    if (pGetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize )
	return;
    raw = (RAWINPUT*)lpb;
    handle_rawinput_2 (raw);
    pDefRawInputProc (&raw, 1, sizeof (RAWINPUTHEADER));
}

static void unacquire (LPDIRECTINPUTDEVICE8 lpdi, char *txt)
{
    if (lpdi) {
	HRESULT hr = IDirectInputDevice8_Unacquire (lpdi);
	if (hr != DI_OK && hr != DI_NOEFFECT)
	    write_log ("unacquire %s failed, %s\n", txt, DXError (hr));
    }
}
static int acquire (LPDIRECTINPUTDEVICE8 lpdi, char *txt)
{
    HRESULT hr = DI_OK;
    if (lpdi) {
	hr = IDirectInputDevice8_Acquire (lpdi);
	if (hr != DI_OK && hr != 0x80070005) {
	    write_log ("acquire %s failed, %s\n", txt, DXError (hr));
	}
    }
    return hr == DI_OK ? 1 : 0;
}

static int setcoop (LPDIRECTINPUTDEVICE8 lpdi, DWORD mode, char *txt)
{
    HRESULT hr = DI_OK;
    if (lpdi && hMainWnd) {
	hr = IDirectInputDevice8_SetCooperativeLevel (lpdi, hMainWnd, mode);
	if (hr != DI_OK && hr != E_NOTIMPL)
	    write_log ("setcooperativelevel %s failed, %s\n", txt, DXError (hr));
    }
    return hr == DI_OK ? 1 : 0;
}

static void sortdd (struct didata *dd, int num, int type)
{
    int i, j;
    struct didata ddtmp;

    for (i = 0; i < num; i++) {
	dd[i].type = type;
	for (j = i + 1; j < num; j++) {
	    dd[j].type = type;
	    if (dd[i].priority < dd[j].priority || (dd[i].priority == dd[j].priority && strcmp (dd[i].sortname, dd[j].sortname) > 0)) {
		memcpy (&ddtmp, &dd[i], sizeof(ddtmp));
		memcpy (&dd[i], &dd[j], sizeof(ddtmp));
		memcpy (&dd[j], &ddtmp, sizeof(ddtmp));
	    }
	}
    }

    /* rename duplicate names */
    for (i = 0; i < num; i++) {
	for (j = i + 1; j < num; j++) {
	    if (!strcmp (dd[i].name, dd[j].name)) {
		int cnt = 1;
		char tmp[MAX_DPATH], tmp2[MAX_DPATH];
		strcpy (tmp2, dd[i].name);
		for (j = i; j < num; j++) {
		    if (!strcmp (tmp2, dd[j].name)) {
			sprintf (tmp, "%s [%d]", dd[j].name, cnt++);
			xfree (dd[j].name);
			dd[j].name = my_strdup (tmp);
		    }
		}
		break;
	    }
	}
    }

}

static void sortobjects (struct didata *did, int *mappings, int *sort, char **names, int *types, int num)
{
    int i, j, tmpi;
    char *tmpc;

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
	write_log ("%s (GUID=%s):\n", did->name, outGUID (&did->guid));
	for (i = 0; i < num; i++)
	    write_log ("%02.2X %0.03d '%s' (%d,%d)\n", mappings[i], mappings[i], names[i], sort[i], types ? types[i] : -1);
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
    int i = 0;
    char tmp[100];

    if (pdidoi->dwType & DIDFT_AXIS) {
	if (did->axles >= MAX_MAPPINGS)
	    return DIENUM_CONTINUE;
	did->axismappings[did->axles] = pdidoi->dwOfs;
	did->axisname[did->axles] = my_strdup (pdidoi->tszName);
	if (did->type == DID_JOYSTICK)
	    did->axissort[did->axles] = makesort_joy (&pdidoi->guidType, &did->axismappings[did->axles]);
	else if (did->type == DID_MOUSE)
	    did->axissort[did->axles] = makesort_mouse (&pdidoi->guidType, &did->axismappings[did->axles]);
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
	    sprintf (tmp, "%s (%d)", pdidoi->tszName, i + 1);
	    did->axisname[did->axles + i] = my_strdup (tmp);
	    did->axissort[did->axles + i] = did->axissort[did->axles];
	    did->axistype[did->axles + i] = 1;
	}
	did->axles += 2;
    }
    if (pdidoi->dwType & DIDFT_BUTTON) {
	if (did->buttons >= MAX_MAPPINGS)
	    return DIENUM_CONTINUE;
	did->buttonname[did->buttons] = my_strdup (pdidoi->tszName);
	if (did->type == DID_JOYSTICK) {
	    did->buttonmappings[did->buttons] = DIJOFS_BUTTON(did->buttons); // pdidoi->dwOfs returns garbage!!
	    did->buttonsort[did->buttons] = makesort_joy (&pdidoi->guidType, &did->buttonmappings[did->buttons]);
	} else if (did->type == DID_MOUSE) {
	    did->buttonmappings[did->buttons] = FIELD_OFFSET(DIMOUSESTATE, rgbButtons) + did->buttons;
	    did->buttonsort[did->buttons] = makesort_mouse (&pdidoi->guidType, &did->buttonmappings[did->buttons]);
	} else {
	    did->buttonmappings[did->buttons] = pdidoi->dwOfs;
	}
	did->buttons++;
    }
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK di_enumcallback (LPCDIDEVICEINSTANCE lpddi, LPVOID *dd)
{
    struct didata *did;
    int i, len, type;
    char *typetxt;

    type = lpddi->dwDevType & 0xff;
    if (type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_SCREENPOINTER) {
	did = di_mouse;
	typetxt = "Mouse";
    } else if (type == DI8DEVTYPE_GAMEPAD  || type == DI8DEVTYPE_JOYSTICK ||
	type == DI8DEVTYPE_FLIGHT || type == DI8DEVTYPE_DRIVING || type == DI8DEVTYPE_1STPERSON) {
	did = di_joystick;
	typetxt = "Game controller";
    } else if (type == DI8DEVTYPE_KEYBOARD) {
	did = di_keyboard;
	typetxt = "Keyboard";
    } else {
	did = NULL;
	typetxt = "Unknown";
    }

#ifdef DI_DEBUG
    write_log ("GUID=%08.8X-%04.8X-%04.8X-%02.2X%02.2X%02.2X%02.2X%02.2X%02.2X%02.2X%02.2X:\n",
	lpddi->guidInstance.Data1, lpddi->guidInstance.Data2, lpddi->guidInstance.Data3,
	lpddi->guidInstance.Data4[0], lpddi->guidInstance.Data4[1], lpddi->guidInstance.Data4[2], lpddi->guidInstance.Data4[3],
	lpddi->guidInstance.Data4[4], lpddi->guidInstance.Data4[5], lpddi->guidInstance.Data4[6], lpddi->guidInstance.Data4[7]);
    write_log ("'%s' '%s' %08.8X [%s]\n", lpddi->tszProductName, lpddi->tszInstanceName, lpddi->dwDevType, typetxt);
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

    memset (did, 0, sizeof (*did));
    for (i = 0; i < MAX_MAPPINGS; i++) {
	did->axismappings[i] = -1;
	did->buttonmappings[i] = -1;
    }
    if (lpddi->tszInstanceName) {
	len = strlen (lpddi->tszInstanceName) + 5 + 1;
	did->name = malloc (len);
	strcpy (did->name, lpddi->tszInstanceName);
    } else {
	did->name = malloc (100);
	sprintf(did->name, "[no name]");
    }
    did->guid = lpddi->guidInstance;
    did->sortname = my_strdup (did->name);
    did->connection = DIDC_DX;

    if (!memcmp (&did->guid, &GUID_SysKeyboard, sizeof (GUID)) || !memcmp (&did->guid, &GUID_SysMouse, sizeof (GUID))) {
	did->priority = 2;
	did->superdevice = 1;
	strcat (did->name, " *");
    }
    return DIENUM_CONTINUE;
}

extern HINSTANCE hInst;
static LPDIRECTINPUT8 g_lpdi;

static int di_do_init (void)
{
    HRESULT hr; 

    num_mouse = num_joystick = num_keyboard = 0;
    memset (&di_mouse, 0, sizeof (di_mouse));
    memset (&di_joystick, 0, sizeof (di_joystick));
    memset (&di_keyboard, 0, sizeof (di_keyboard));

    hr = DirectInput8Create (hInst, DIRECTINPUT_VERSION, &IID_IDirectInput8A, (LPVOID *)&g_lpdi, NULL); 
    if (FAILED(hr)) {
	write_log ("DirectInput8Create failed, %s\n", DXError (hr));
	gui_message ("Failed to initialize DirectInput!");
	return 0;
    }
    write_log("DirectInput enumeration..\n");
    IDirectInput8_EnumDevices (g_lpdi, DI8DEVCLASS_ALL, di_enumcallback, 0, DIEDFL_ATTACHEDONLY);
    write_log("RawInput enumeration..\n");
    initialize_rawinput();
    write_log("Windowsmouse initialization..\n");
    initialize_windowsmouse();
    write_log("Catweasel joymouse initialization..\n");
    initialize_catweasel();
    write_log("end\n");

    sortdd (di_joystick, num_joystick, DID_JOYSTICK);
    sortdd (di_mouse, num_mouse, DID_MOUSE);
    sortdd (di_keyboard, num_keyboard, DID_KEYBOARD);

    return 1;
}

static void di_dev_free (struct didata *did)
{
    free (did->name);
    free (did->sortname);
    memset (did, 0, sizeof (*did));
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
    if (dd_inited == 0)
	return;
    dd_inited--;
    if (dd_inited > 0)
	return;
    if (g_lpdi)
	IDirectInput8_Release (g_lpdi);
    g_lpdi = 0;
    di_dev_free (di_mouse);
    di_dev_free (di_joystick);
    di_dev_free (di_keyboard);
}

static int get_mouse_num (void)
{
    return num_mouse;
}

static char *get_mouse_name (int mouse)
{
    return di_mouse[mouse].name;
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

static int get_mouse_widget_type (int mouse, int num, char *name, uae_u32 *code)
{
    struct didata *did = &di_mouse[mouse];

    int axles = did->axles;
    int buttons = did->buttons;
    if (num >= axles && num < axles + buttons) {
	if (name)
	    strcpy (name, did->buttonname[num - did->axles]);
	return IDEV_WIDGET_BUTTON;
    } else if (num < axles) {
	if (name)
	    strcpy (name, did->axisname[num]);
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
	if (!did->disabled && did->connection == DIDC_DX) {
	    hr = IDirectInput8_CreateDevice (g_lpdi, &did->guid, &lpdi, NULL);
	    if (hr == DI_OK) {
		hr = IDirectInputDevice8_SetDataFormat(lpdi, &c_dfDIMouse); 
		IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, (void*)did, DIDFT_ALL);
		sortobjects (did, did->axismappings, did->axissort, did->axisname, did->axistype, did->axles);
		sortobjects (did, did->buttonmappings, did->buttonsort, did->buttonname, 0, did->buttons);
		did->lpdi = lpdi;
	    } else {
		write_log ("mouse %d CreateDevice failed, %s\n", i, DXError (hr));
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
    for (i = 0; i < num_mouse; i++) {
	if (di_mouse[i].lpdi)
	    IDirectInputDevice8_Release (di_mouse[i].lpdi);
	di_mouse[i].lpdi = 0;
    }
    supermouse = normalmouse = rawmouse = winmouse = 0;
    di_free ();
}

static int acquire_mouse (int num, int flags)
{
    LPDIRECTINPUTDEVICE8 lpdi = di_mouse[num].lpdi;
    struct didata *did = &di_mouse[num];
    DIPROPDWORD dipdw;
    HRESULT hr;

    unacquire (lpdi, "mouse");
    if (did->connection == DIDC_DX && lpdi) {
	setcoop (lpdi, flags ? (DISCL_FOREGROUND | DISCL_EXCLUSIVE) : (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE), "mouse");
	dipdw.diph.dwSize = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = DI_BUFFER;
	hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
	if (hr != DI_OK)
	    write_log ("mouse setpropertry failed, %s\n", DXError (hr));
	di_mouse[num].acquired = acquire (lpdi, "mouse") ? 1 : -1;
    } else {
	di_mouse[num].acquired = 1;
    }
    if (di_mouse[num].acquired) {
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
    if (!supermouse && rawmouse)
	register_rawinput ();
    return di_mouse[num].acquired > 0 ? 1 : 0;
}

static void unacquire_mouse (int num)
{
    unacquire (di_mouse[num].lpdi, "mouse");
    if (di_mouse[num].acquired) {
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
    int fs = isfullscreen();

    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	struct didata *did = &di_mouse[i];
	LPDIRECTINPUTDEVICE8 lpdi = did->lpdi;
	if (!did->acquired)
	    continue;
	if (did->connection == DIDC_CAT) {
	    int cx, cy, cbuttons;
	    catweasel_read_mouse(did->catweasel, &cx, &cy, &cbuttons);
	    if (cx)
		setmousestate(i, 0, cx, 0);
	    if (cy)
		setmousestate(i, 1, cy, 0);
	    setmousebuttonstate(i, 0, cbuttons & 8);
	    setmousebuttonstate(i, 1, cbuttons & 4);
	    setmousebuttonstate(i, 2, cbuttons & 2);
	    continue;
	}
	if (!lpdi || did->connection != DIDC_DX)
	    continue;
	elements = DI_BUFFER;
	hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
	if (hr == DI_OK) {
	    if (supermouse && !did->superdevice)
		continue;
	    for (j = 0; j < elements; j++) {
		int dimofs = didod[j].dwOfs;
		int data = didod[j].dwData;
		int state = (data & 0x80) ? 1 : 0;
#ifdef DI_DEBUG2
		write_log ("MOUSE: %d OFF=%d DATA=%d STATE=%d\n", i, dimofs, data, state);
#endif
		if (focus) {
		    if (mouseactive || fs) {
			for (k = 0; k < did->axles; k++) {
			    if (did->axismappings[k] == dimofs) {
				setmousestate (i, k, data, 0);
				break;
			    }
			}
			for (k = 0; k < did->buttons; k++) {
			    if (did->buttonmappings[k] == dimofs) {
#ifdef SINGLEFILE
				if (k == 0)
				    uae_quit ();
#endif
				if ((currprefs.win32_middle_mouse && k != 2) || !(currprefs.win32_middle_mouse)) {
				    setmousebuttonstate (i, k, state);
				    break;
				}
			    }
			}
		    }
		    if (currprefs.win32_middle_mouse && dimofs == DIMOFS_BUTTON2 && state) {
			if (isfullscreen ())
			    minimizewindow ();
			if (mouseactive)
			    setmouseactive(0);
		    }
		}
	    }
	} else if (hr == DIERR_INPUTLOST) {
	    acquire (lpdi, "mouse");
	} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
	    acquire (lpdi, "mouse");
	}
	IDirectInputDevice8_Poll (lpdi);
    }
}

struct inputdevice_functions inputdevicefunc_mouse = {
    init_mouse, close_mouse, acquire_mouse, unacquire_mouse, read_mouse,
    get_mouse_num, get_mouse_name,
    get_mouse_widget_num, get_mouse_widget_type,
    get_mouse_widget_first
};


static int get_kb_num (void)
{
    return num_keyboard;
}

static char *get_kb_name (int kb)
{
    return di_keyboard[kb].name;
}

static int get_kb_widget_num (int kb)
{
    return di_keyboard[kb].buttons;
}

static int get_kb_widget_first (int kb, int type)
{
    return 0;
}

static int get_kb_widget_type (int kb, int num, char *name, uae_u32 *code)
{
    if (name)
	sprintf (name, "[%02.2X] %s", di_keyboard[kb].buttonmappings[num], di_keyboard[kb].buttonname[num]);
    if (code)
	*code = di_keyboard[kb].buttonmappings[num];
    return IDEV_WIDGET_KEY;
}

static int keyboard_german;

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

    if (currprefs.win32_kbledmode && os_winnt) {
	oldusbleds = led;
    } else if (!currprefs.win32_kbledmode && os_winnt && kbhandle != INVALID_HANDLE_VALUE) {
#ifdef WINDDK
	KEYBOARD_INDICATOR_PARAMETERS InputBuffer;
	KEYBOARD_INDICATOR_PARAMETERS OutputBuffer;
	ULONG DataLength = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
	ULONG ReturnedLength;

	memset (&InputBuffer, 0, sizeof (InputBuffer));
	memset (&OutputBuffer, 0, sizeof (OutputBuffer));
	if (!DeviceIoControl(kbhandle, IOCTL_KEYBOARD_QUERY_INDICATORS,
	    &InputBuffer, DataLength, &OutputBuffer, DataLength, &ReturnedLength, NULL))
	    return 0;
	led = 0;
	if (OutputBuffer.LedFlags & KEYBOARD_NUM_LOCK_ON) led |= KBLED_NUMLOCK;
	if (OutputBuffer.LedFlags & KEYBOARD_CAPS_LOCK_ON) led |= KBLED_CAPSLOCK;
	if (OutputBuffer.LedFlags & KEYBOARD_SCROLL_LOCK_ON) led |= KBLED_SCROLLLOCK;
#endif
    }
    return led;
}

static void set_leds (uae_u32 led)
{
    if (os_winnt && currprefs.win32_kbledmode) {
	if((oldusbleds & KBLED_NUMLOCK) != (led & KBLED_NUMLOCK)) {
	    keybd_event (VK_NUMLOCK, 0, KEYEVENTF_EXTENDEDKEY, 0);
	    keybd_event (VK_NUMLOCK, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
	}
	if((oldusbleds & KBLED_CAPSLOCK) != (led & KBLED_CAPSLOCK)) {
	    keybd_event (VK_CAPITAL, 0, KEYEVENTF_EXTENDEDKEY, 0);
	    keybd_event (VK_CAPITAL, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
	}
	if((oldusbleds & KBLED_SCROLLLOCK) != (led & KBLED_SCROLLLOCK)) {
	    keybd_event (VK_SCROLL, 0, KEYEVENTF_EXTENDEDKEY, 0);
	    keybd_event (VK_SCROLL, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
	}
	oldusbleds = led;
    } else if (os_winnt && kbhandle != INVALID_HANDLE_VALUE) {
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
	if (!DeviceIoControl(kbhandle, IOCTL_KEYBOARD_SET_INDICATORS,
	    &InputBuffer, DataLength, NULL, 0, &ReturnedLength, NULL))
		write_log ("kbleds: DeviceIoControl() failed %d\n", GetLastError());
#endif
    } else if (!os_winnt) {
	ledkeystate[VK_NUMLOCK] &= ~1;
	if (led & KBLED_NUMLOCK)
	    ledkeystate[VK_NUMLOCK] = 1;
	ledkeystate[VK_CAPITAL] &= ~1;
	if (led & KBLED_CAPSLOCK)
	    ledkeystate[VK_CAPITAL] = 1;
	ledkeystate[VK_SCROLL] &= ~1;
	if (led & KBLED_SCROLLLOCK)
	    ledkeystate[VK_SCROLL] = 1;
	SetKeyboardState (ledkeystate);
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
	if (!did->disabled && did->connection == DIDC_DX) {
	    hr = IDirectInput8_CreateDevice (g_lpdi, &did->guid, &lpdi, NULL);
	    if (hr == DI_OK) {
		hr = IDirectInputDevice8_SetDataFormat(lpdi, &c_dfDIKeyboard); 
		if (hr != DI_OK)
		    write_log ("keyboard setdataformat failed, %s\n", DXError (hr));
		memset (&dipdw, 0, sizeof (dipdw));
		dipdw.diph.dwSize = sizeof(DIPROPDWORD);
		dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
		dipdw.diph.dwObj = 0;
		dipdw.diph.dwHow = DIPH_DEVICE;
		dipdw.dwData = DI_KBBUFFER;
		hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
		if (hr != DI_OK)
		    write_log ("keyboard setpropertry failed, %s\n", DXError (hr));
		IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, (void*)did, DIDFT_ALL);
		sortobjects (did, did->axismappings, did->axissort, did->axisname, did->axistype, did->axles);
		sortobjects (did, did->buttonmappings, did->buttonsort, did->buttonname, 0, did->buttons);
		did->lpdi = lpdi;
	    } else
		write_log ("keyboard CreateDevice failed, %s\n", DXError (hr));
	}
    }
    keyboard_german = 0;
    if ((LOWORD(GetKeyboardLayout(0)) & 0x3ff) == 7)
	keyboard_german = 1;
    return 1;
}

static void close_kb (void)
{
    int i;

    if (keyboard_inited == 0)
	return;
    keyboard_inited = 0;

    for (i = 0; i < num_keyboard; i++) {
	if (di_keyboard[i].lpdi)
	    IDirectInputDevice8_Release (di_keyboard[i].lpdi);
	di_keyboard[i].lpdi = 0;
    }
    di_free ();
}

#define	MAX_KEYCODES 256
static uae_u8 di_keycodes[MAX_INPUT_DEVICES][MAX_KEYCODES];
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

static int refresh_kb (LPDIRECTINPUTDEVICE8 lpdi, int num)
{
    HRESULT hr;
    int i;
    uae_u8 kc[256];

    hr = IDirectInputDevice8_GetDeviceState (lpdi, sizeof (kc), kc);
    if (hr == DI_OK) {
	for (i = 0; i < sizeof (kc); i++) {
	    if (i == 0x80) /* USB KB led causes this, better ignore it */
		continue;
	    if (kc[i] & 0x80) kc[i] = 1; else kc[i] = 0;
	    if (kc[i] != di_keycodes[num][i]) {
		write_log ("%02.2X -> %d\n", i, kc[i]);
		di_keycodes[num][i] = kc[i];
		my_kbd_handler (num, i, kc[i]);
	    }
	}
    } else if (hr == DIERR_INPUTLOST) {
	acquire (lpdi, "keyboard");
	IDirectInputDevice8_Poll (lpdi);
	return 0;
    }
    IDirectInputDevice8_Poll (lpdi);
    return 1;
}


static int keyhack (int scancode,int pressed, int num)
{
    static byte backslashstate,apostrophstate;
    
     //check ALT-F4 
    if (pressed && !di_keycodes[num][DIK_F4] && scancode == DIK_F4 && di_keycodes[num][DIK_LALT] && !currprefs.win32_ctrl_F11_is_quit) {
	uae_quit ();
	return -1;
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

static void read_kb (void)
{
    DIDEVICEOBJECTDATA didod[DI_KBBUFFER];
    DWORD elements;
    HRESULT hr;
    LPDIRECTINPUTDEVICE8 lpdi;
    int i, j;

    update_leds ();
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	struct didata *did = &di_keyboard[i];
	if (!did->acquired)
	    continue;
	lpdi = did->lpdi;
	if (!lpdi)
	    continue;
	if (kb_do_refresh & (1 << i)) {
	    if (!refresh_kb (lpdi, i))
		continue;
	    kb_do_refresh &= ~(1 << i);
	}
	elements = DI_KBBUFFER;
	hr = IDirectInputDevice8_GetDeviceData(lpdi, sizeof(DIDEVICEOBJECTDATA), didod, &elements, 0);
	if (hr == DI_OK) {
	    if (did->superdevice && (normalkb || rawkb))
		continue;
	    for (j = 0; j < elements; j++) {
		int scancode = didod[j].dwOfs;
		int pressed = (didod[j].dwData & 0x80) ? 1 : 0;
		scancode = keyhack (scancode, pressed, i);
		if (scancode < 0)
		    continue;
		di_keycodes[i][scancode] = pressed;
		if (stopoutput == 0)
		    my_kbd_handler (i, scancode, pressed);
	    }
	} else if (hr == DIERR_INPUTLOST) {
	    acquire (lpdi, "keyboard");
	    kb_do_refresh |= 1 << i;
	} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
	    acquire (lpdi, "keyboard");
	}
	IDirectInputDevice8_Poll (lpdi);
    }
#ifdef CATWEASEL
    {
	char kc;
	if (stopoutput == 0 && catweasel_read_keyboard (&kc))
	    inputdevice_do_keyboard (kc & 0x7f, kc & 0x80);
    }
#endif
}

void wait_keyrelease (void)
{
    int i, j, maxcount = 200, found;
    stopoutput++;

    while (maxcount-- > 0) {
	sleep_millis (10);
	read_kb ();
	found = 0;
	for (j = 0; j < MAX_INPUT_DEVICES; j++) {
	    for (i = 0; i < MAX_KEYCODES; i++) {
		if (di_keycodes[j][i]) found = 1;
	    }
	}
	if (!found)
	    break;
    }
    release_keys ();

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
	    if (hr == DI_OK) {
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
    stopoutput--;
}

static int acquire_kb (int num, int flags)
{
    DWORD mode = DISCL_NOWINKEY | DISCL_FOREGROUND | DISCL_EXCLUSIVE;
    LPDIRECTINPUTDEVICE8 lpdi = di_keyboard[num].lpdi;

    unacquire (lpdi, "keyboard");
    if (currprefs.keyboard_leds_in_use) {
#ifdef WINDDK
	if (os_winnt && !currprefs.win32_kbledmode) {
	    if (DefineDosDevice (DDD_RAW_TARGET_PATH, "Kbd","\\Device\\KeyboardClass0")) {
		kbhandle = CreateFile("\\\\.\\Kbd", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (kbhandle == INVALID_HANDLE_VALUE) {
		    write_log ("kbled: CreateFile failed, error %d\n", GetLastError());
		    currprefs.win32_kbledmode = 1;
		}
	    } else {
		currprefs.win32_kbledmode = 1;
		write_log ("kbled: DefineDosDevice failed, error %d\n", GetLastError());
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

    setcoop (lpdi, mode, "keyboard");
    kb_do_refresh = ~0;
    di_keyboard[num].acquired = -1;
    if (acquire (lpdi, "keyboard")) {
	if (di_keyboard[num].superdevice)
	    superkb++;
	else
	    normalkb++;
	di_keyboard[num].acquired = 1;
    }
    return di_keyboard[num].acquired > 0 ? 1 : 0;
}

static void unacquire_kb (int num)
{
    LPDIRECTINPUTDEVICE8 lpdi = di_keyboard[num].lpdi;

    unacquire (lpdi, "keyboard");
    if (di_keyboard[num].acquired) {
	if (di_keyboard[num].superdevice)
	    superkb--;
	else
	    normalkb--;
	di_keyboard[num].acquired = 0;
    }
    release_keys ();

    if (currprefs.keyboard_leds_in_use) {
	if (oldusedleds >= 0) {
	    if (!currprefs.win32_kbledmode)
		set_leds (oldleds);
	    oldusedleds = oldleds;
	}
#ifdef WINDDK
	if (kbhandle != INVALID_HANDLE_VALUE) {
	    CloseHandle(kbhandle);
	    DefineDosDevice (DDD_REMOVE_DEFINITION, "Kbd", NULL);
	    kbhandle = INVALID_HANDLE_VALUE;
	}
#endif
    }
}

struct inputdevice_functions inputdevicefunc_keyboard = {
    init_kb, close_kb, acquire_kb, unacquire_kb, read_kb,
    get_kb_num, get_kb_name,
    get_kb_widget_num, get_kb_widget_type,
    get_kb_widget_first
};


static int get_joystick_num (void)
{
    return num_joystick;
}

static int get_joystick_widget_num (int joy)
{
    return di_joystick[joy].axles + di_joystick[joy].buttons;
}

static int get_joystick_widget_type (int joy, int num, char *name, uae_u32 *code)
{
    struct didata *did = &di_joystick[joy];
    if (num >= did->axles && num < did->axles + did->buttons) {
	if (name)
	    strcpy (name, did->buttonname[num - did->axles]);
	return IDEV_WIDGET_BUTTON;
    } else if (num < di_joystick[joy].axles) {
	if (name)
	    strcpy (name, did->axisname[num]);
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

static char *get_joystick_name (int joy)
{
    return di_joystick[joy].name;
}

static void read_joystick (void)
{
    DIDEVICEOBJECTDATA didod[DI_BUFFER];
    LPDIRECTINPUTDEVICE8 lpdi;
    DWORD elements;
    HRESULT hr;
    int i, j, k;

    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	struct didata *did = &di_joystick[i];
	if (!did->acquired)
	    continue;
	if (did->connection == DIDC_CAT) {
	    uae_u8 cdir, cbuttons;
	    catweasel_read_joystick(&cdir, &cbuttons);
	    cdir >>= did->catweasel * 4;
	    cbuttons >>= did->catweasel * 4;
	    setjoystickstate(i, 0, !(cdir & 1) ? 1 : !(cdir & 2) ? -1 : 0, 0);
	    setjoystickstate(i, 1, !(cdir & 4) ? 1 : !(cdir & 8) ? -1 : 0, 0);
	    setjoybuttonstate(i, 0, cbuttons & 8);
	    setjoybuttonstate(i, 1, cbuttons & 4);
	    setjoybuttonstate(i, 2, cbuttons & 2);
	    continue;
	}
	lpdi = did->lpdi;
	if (!lpdi || did->connection != DIDC_DX)
	    continue;
	elements = DI_BUFFER;
	hr = IDirectInputDevice8_GetDeviceData (lpdi, sizeof (DIDEVICEOBJECTDATA), didod, &elements, 0);
	if (hr == DI_OK) {
	    for (j = 0; j < elements; j++) {
		int dimofs = didod[j].dwOfs;
		int data = didod[j].dwData;
		int data2 = data;
		int state = (data & 0x80) ? 1 : 0;
		data -= 32768;
		
		for (k = 0; k < did->buttons; k++) {
		    if (did->buttonmappings[k] == dimofs) {
#ifdef DI_DEBUG2
			write_log ("B:NUM=%d OFF=%d NAME=%s VAL=%d STATE=%d\n",
			    k, dimofs, did->buttonname[k], data, state);
#endif
			setjoybuttonstate (i, k, state);
			break;
		    }
		}
		for (k = 0; k < did->axles; k++) {
		    if (did->axismappings[k] == dimofs) {
			if (did->axistype[k]) {
			    setjoystickstate (i, k, (data2 >= 20250 && data2 <= 33750) ? -1 : (data2 >= 2250 && data2 <= 15750) ? 1 : 0, 1);
			    setjoystickstate (i, k + 1, ((data2 >= 29250 && data2 <= 33750) || (data2 >= 0 && data2 <= 6750)) ? -1 : (data2 >= 11250 && data2 <= 24750) ? 1 : 0, 1);
#ifdef DI_DEBUG2
			    write_log ("P:NUM=%d OFF=%d NAME=%s VAL=%d\n", k, dimofs, did->axisname[k], data2);
#endif
			} else {
#ifdef DI_DEBUG2
			    if (data < -20000 || data > 20000)
				write_log ("A:NUM=%d OFF=%d NAME=%s VAL=%d\n", k, dimofs, did->axisname[k], data);
#endif
			    setjoystickstate (i, k, data, 32768);
			}
			break;
		    }
		}
	    }
	} else if (hr == DIERR_INPUTLOST) {
	    acquire (lpdi, "joystick");
	} else if (did->acquired &&  hr == DIERR_NOTACQUIRED) {
	    acquire (lpdi, "joystick");
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
	if (!did->disabled && did->connection == DIDC_DX) {
	    hr = IDirectInput8_CreateDevice (g_lpdi, &did->guid, &lpdi, NULL);
	    if (hr == DI_OK) {
		hr = IDirectInputDevice8_SetDataFormat(lpdi, &c_dfDIJoystick);
		if (hr == DI_OK) {
		    did->lpdi = lpdi;
		    IDirectInputDevice8_EnumObjects (lpdi, EnumObjectsCallback, (void*)did, DIDFT_ALL);
		    sortobjects (did, did->axismappings, did->axissort, did->axisname, did->axistype, did->axles);
		    sortobjects (did, did->buttonmappings, did->buttonsort, did->buttonname, 0, did->buttons);
		}
	    } else {
		write_log ("joystick createdevice failed, %s\n", DXError (hr));
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
    for (i = 0; i < num_joystick; i++) {
	if (di_joystick[i].lpdi)
	    IDirectInputDevice8_Release (di_joystick[i].lpdi);
	di_joystick[i].lpdi = 0;
    }
    di_free ();
}

static int acquire_joystick (int num, int flags)
{
    LPDIRECTINPUTDEVICE8 lpdi = di_joystick[num].lpdi;
    DIPROPDWORD dipdw;
    HRESULT hr;

    unacquire (lpdi, "joystick");
    if (di_joystick[num].connection == DIDC_DX && lpdi) {
	setcoop (lpdi, flags ? (DISCL_FOREGROUND | DISCL_EXCLUSIVE) : (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE), "joystick");
	memset (&dipdw, 0, sizeof (dipdw));
	dipdw.diph.dwSize = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = DI_BUFFER;
	hr = IDirectInputDevice8_SetProperty (lpdi, DIPROP_BUFFERSIZE, &dipdw.diph);
	if (hr != DI_OK)
	write_log ("joystick setproperty failed, %s\n", DXError (hr));
	di_joystick[num].acquired = acquire (lpdi, "joystick") ? 1 : -1;
    } else {
	di_joystick[num].acquired = 1;
    }
    return di_joystick[num].acquired > 0 ? 1 : 0;
}

static void unacquire_joystick (int num)
{
    unacquire (di_joystick[num].lpdi, "joystick");
    di_joystick[num].acquired = 0;
}

struct inputdevice_functions inputdevicefunc_joystick = {
    init_joystick, close_joystick, acquire_joystick, unacquire_joystick,
    read_joystick, get_joystick_num, get_joystick_name,
    get_joystick_widget_num, get_joystick_widget_type,
    get_joystick_widget_first
};

int dinput_wmkey (uae_u32 key)
{
    if (normalkb || superkb || rawkb)
	return 0;
    if (((key >> 16) & 0xff) == 0x58)
	return 1;
    return 0;
}

void input_get_default_mouse (struct uae_input_device *uid)
{
    int i, port;

    for (i = 0; i < num_mouse; i++) {
	port = i & 1;
	if (di_mouse[i].wininput)
	    port = 0;
	uid[i].eventid[ID_AXIS_OFFSET + 0][0] = port ? INPUTEVENT_MOUSE2_HORIZ : INPUTEVENT_MOUSE1_HORIZ;
	uid[i].eventid[ID_AXIS_OFFSET + 1][0] = port ? INPUTEVENT_MOUSE2_VERT : INPUTEVENT_MOUSE1_VERT;
	uid[i].eventid[ID_AXIS_OFFSET + 2][0] = port ? 0 : INPUTEVENT_MOUSE1_WHEEL;
	uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON;
	uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON;
	uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
	if (port == 0) { /* map back and forward to ALT+LCUR and ALT+RCUR */
	    uid[i].eventid[ID_BUTTON_OFFSET + 3][0] = INPUTEVENT_KEY_ALT_LEFT;
	    uid[i].eventid[ID_BUTTON_OFFSET + 3][1] = INPUTEVENT_KEY_CURSOR_LEFT;
	    uid[i].eventid[ID_BUTTON_OFFSET + 4][0] = INPUTEVENT_KEY_ALT_LEFT;
	    uid[i].eventid[ID_BUTTON_OFFSET + 4][1] = INPUTEVENT_KEY_CURSOR_RIGHT;
	}
    }
    uid[0].enabled = 1;
}

void input_get_default_joystick (struct uae_input_device *uid)
{
    int i, port;

    for (i = 0; i < num_joystick; i++) {
	port = i & 1;
	uid[i].eventid[ID_AXIS_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_HORIZ : INPUTEVENT_JOY1_HORIZ;
	uid[i].eventid[ID_AXIS_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_VERT : INPUTEVENT_JOY1_VERT;
	uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON;
	uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON;
	uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
	if (cd32_enabled) {
	    uid[i].eventid[ID_BUTTON_OFFSET + 0][0] = port ? INPUTEVENT_JOY2_CD32_RED : INPUTEVENT_JOY1_CD32_RED;
	    uid[i].eventid[ID_BUTTON_OFFSET + 1][0] = port ? INPUTEVENT_JOY2_CD32_BLUE : INPUTEVENT_JOY1_CD32_BLUE;
	    uid[i].eventid[ID_BUTTON_OFFSET + 2][0] = port ? INPUTEVENT_JOY2_CD32_YELLOW : INPUTEVENT_JOY1_CD32_YELLOW;
	    uid[i].eventid[ID_BUTTON_OFFSET + 3][0] = port ? INPUTEVENT_JOY2_CD32_GREEN : INPUTEVENT_JOY1_CD32_GREEN;
	    uid[i].eventid[ID_BUTTON_OFFSET + 4][0] = port ? INPUTEVENT_JOY2_CD32_FFW : INPUTEVENT_JOY1_CD32_FFW;
	    uid[i].eventid[ID_BUTTON_OFFSET + 5][0] = port ? INPUTEVENT_JOY2_CD32_RWD : INPUTEVENT_JOY1_CD32_RWD;
	    uid[i].eventid[ID_BUTTON_OFFSET + 6][0] = port ? INPUTEVENT_JOY2_CD32_PLAY :  INPUTEVENT_JOY1_CD32_PLAY;
	}
    }
    uid[0].enabled = 1;
}
