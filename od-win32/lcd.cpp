
#include "sysconfig.h"
#include "sysdeps.h"

#if defined(LOGITECHLCD)

#include "resource.h"
#include "gui.h"
#include "lcd.h"

#include <LogitechLCDLib.h>

extern HINSTANCE hInst;

static int inited;
static uae_u8 *bitmap, *origbitmap;
static uae_u8 *numbers;
static int bm_width = LOGI_LCD_MONO_WIDTH;
static int bm_height = LOGI_LCD_MONO_HEIGHT;
static const int numbers_width = 7, numbers_height = 10;
static HMODULE lcdlib;

extern unsigned long timeframes;

// Do it this way because stupid LogitechLCDLib.lib LogiLcdInit() refuses to link.

typedef bool(__cdecl *LOGILCDINIT)(wchar_t*, int);
static LOGILCDINIT pLogiLcdInit;
typedef bool(__cdecl *LOGILCDISCONNECTED)(int);
static LOGILCDISCONNECTED pLogiLcdIsConnected;
typedef bool(__cdecl *LOGILCDSHUTDOWN)(void);
static LOGILCDSHUTDOWN pLogiLcdShutdown;
typedef bool(__cdecl *LOGILCDUPDATE)(void);
static LOGILCDUPDATE pLogiLcdUpdate;
typedef bool(__cdecl *LOGILCDMONOSETBACKGROUND)(BYTE[]);
static LOGILCDMONOSETBACKGROUND pLogiLcdMonoSetBackground;

#define LOGITECH_LCD_DLL _T("SOFTWARE\\Classes\\CLSID\\{d0e790a5-01a7-49ae-ae0b-e986bdd0c21b}\\ServerBinary")

void lcd_close (void)
{
	if (!lcdlib)
		return;
	pLogiLcdShutdown();
	xfree(bitmap);
	bitmap = NULL;
	xfree(origbitmap);
	origbitmap = NULL;
	inited = 0;
	FreeLibrary(lcdlib);
	lcdlib = NULL;
}

static int lcd_init (void)
{
	HBITMAP bmp;
	BITMAP binfo;
	HDC dc;
	TCHAR path[MAX_DPATH];
	DWORD ret;
	DWORD type = REG_SZ;
	DWORD size = sizeof(path) / sizeof(TCHAR);
	HKEY key;

	lcdlib = NULL;
	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, LOGITECH_LCD_DLL, 0, KEY_READ, &key);
	if (ret != ERROR_SUCCESS)
		return 0;
	if (RegQueryValueEx(key, NULL, 0, &type, (LPBYTE)path, &size) == ERROR_SUCCESS) {
		lcdlib = LoadLibrary(path);
	}
	RegCloseKey(key);
	if (!lcdlib)
		return 0;

	pLogiLcdInit = (LOGILCDINIT)GetProcAddress(lcdlib, "LogiLcdInit");
	pLogiLcdIsConnected = (LOGILCDISCONNECTED)GetProcAddress(lcdlib, "LogiLcdIsConnected");
	pLogiLcdShutdown = (LOGILCDSHUTDOWN)GetProcAddress(lcdlib, "LogiLcdShutdown");
	pLogiLcdUpdate = (LOGILCDUPDATE)GetProcAddress(lcdlib, "LogiLcdUpdate");
	pLogiLcdMonoSetBackground = (LOGILCDMONOSETBACKGROUND)GetProcAddress(lcdlib, "LogiLcdMonoSetBackground");
	if (!pLogiLcdInit || !pLogiLcdIsConnected || !pLogiLcdShutdown || !pLogiLcdUpdate || !pLogiLcdMonoSetBackground)
		goto err;

	if (!pLogiLcdInit(_T("WinUAE"), LOGI_LCD_TYPE_MONO))
		goto err;

	bmp = LoadBitmap (hInst, MAKEINTRESOURCE(IDB_LCD160X43));
	dc = CreateCompatibleDC (NULL);
	SelectObject (dc, bmp);
	GetObject (bmp, sizeof (binfo), &binfo);

	bitmap = xcalloc(uae_u8, binfo.bmWidth * binfo.bmHeight);
	origbitmap = xcalloc(uae_u8, binfo.bmWidth * binfo.bmHeight);

	for (int y = 0; y < binfo.bmHeight; y++) {
		for (int x = 0; x < binfo.bmWidth; x++) {
			bitmap[y * binfo.bmWidth + x] = GetPixel (dc, x, y) == 0 ? 0xff : 0;
		}
	}
	numbers = bitmap + bm_width * bm_height;
	memcpy (origbitmap, bitmap, bm_width * bm_height);
	DeleteDC (dc);

	write_log (_T("LCD enabled\n"));
	return 1;

err:
	write_log(_T("LCD: Logitech LCD failed to initialize\n"));
	if (lcdlib) {
		FreeLibrary(lcdlib);
		lcdlib = NULL;
	}
	return 0;
}

static void dorect (int *crd, int inv)
{
	int yy, xx;
	int x = crd[0], y = crd[1], w = crd[2], h = crd[3];
	for (yy = y; yy < y + h; yy++) {
		for (xx = x; xx < x + w; xx++) {
			uae_u8 b = origbitmap[yy * bm_width + xx];
			if (inv)
				b = b == 0 ? 0xff : 0;
			bitmap[yy * bm_width + xx] = b;
		}
	}
}

static void putnumber (int x, int y, int n, int inv)
{
	int xx, yy;
	uae_u8 *dst, *src;
	if (n == 0)
		n = 9;
	else
		n--;
	if (n < 0)
		n = 10;
	for (yy = 0; yy < numbers_height; yy++) {
		for (xx = 0; xx < numbers_width; xx++) {
			dst = bitmap + (yy + y) * bm_width + (xx + x);
			src = numbers + n * numbers_width + yy * bm_width + xx;
			*dst = 0;
			if (*src == 0)
				*dst = 0xff;
			if (inv)
				*dst ^= 0xff;
		}
	}
}

static void putnumbers (int x, int y, int num, int inv)
{
	putnumber (x, y, num < 0 ? num : ((num / 10) > 0 ? num / 10 : -1), inv);
	putnumber (x + numbers_width, y, num < 0 ? num : num % 10, inv);
}

static int coords[] = {
	53, 2, 13, 10, // CD
	36, 2, 13, 10, // HD
	2, 2, 30, 10 // POWER
};

void lcd_priority (int priority)
{
}

void lcd_update (int led, int on)
{
	bool c;
	int track, x, y;
	static unsigned long otimeframes;

	if (!inited)
		return;

	if (led < 0) {
		c = pLogiLcdUpdate();
		return;
	}
	if (on < 0)
		return;

	if (led >= 1 && led <= 4) {
		x = 23 + (led - 1) * 40;
		y = 17;
		track = gui_data.drive_track[led - 1];
		if (gui_data.drive_disabled[led - 1]) {
			track = -1;
			on = 0;
		}
		putnumbers (x, y, track, on);
	} else if (led == 0) {
		dorect (&coords[4 * 2], on);
	} else if (led == 5) {
		dorect (&coords[4 * 1], on);
	} else if (led == 6) {
		dorect (&coords[4 * 0], on);
	} else if (led == 7) {
		y = 2;
		x = 125;
		putnumbers (x, y, gui_data.fps <= 999 ? (gui_data.fps + 5) / 10 : 99, 0);
	} else if (led == 8) {
		y = 2;
		x = 98;
		putnumbers (x, y, gui_data.idle <= 999 ? gui_data.idle / 10 : 99, 0);
	}

	if (otimeframes != timeframes) {
		c = pLogiLcdMonoSetBackground(bitmap);
		c = pLogiLcdUpdate();
		otimeframes = timeframes;
	}
}

int lcd_open (void)
{
	if (!inited) {
		if (!lcd_init ())
			return 0;
		inited = 1;
	}
	return 1;
}

#endif
