
#include "sysconfig.h"
#include "sysdeps.h"

#if defined(LOGITECHLCD)

#include "resource.h"
#include "gui.h"
#include "lcd.h"

#include <lglcd.h>

extern HINSTANCE hInst;

static int inited;
static lgLcdConnectContext cctx;
static lgLcdDeviceDescEx desc;
static int device;
static lgLcdBitmapHeader *lbh;
static uae_u8 *bitmap, *origbitmap;
static uae_u8 *numbers;
static int numbers_width = 7, numbers_height = 10;
static int old_pri;

void lcd_close (void)
{
	lgLcdDeInit ();
	xfree (lbh);
	lbh = NULL;
	bitmap = NULL;
	inited = 0;
}

static int lcd_init (void)
{
	DWORD ret;
	lgLcdOpenContext octx;
	HBITMAP bmp;
	BITMAP binfo;
	HDC dc;
	int x, y;

	old_pri = 0;
	ret = lgLcdInit ();
	if (ret != ERROR_SUCCESS) {
		if (ret == RPC_S_SERVER_UNAVAILABLE || ret == ERROR_OLD_WIN_VERSION) {
			write_log (_T("LCD: Logitech LCD system not detected\n"));
			return 0;
		}
		write_log (_T("LCD: lgLcdInit() returned %d\n"), ret);
		return 0;
	}
	memset (&cctx, 0, sizeof (cctx));
	cctx.appFriendlyName = _T("WinUAE");
	cctx.isPersistent = TRUE;
	cctx.isAutostartable = FALSE;
	ret = lgLcdConnect (&cctx);
	if (ret != ERROR_SUCCESS) {
		write_log (_T("LCD: lgLcdConnect() returned %d\n"), ret);
		lcd_close ();
		return 0;
	}
	ret = lgLcdEnumerateEx (cctx.connection, 0, &desc);
	if (ret != ERROR_SUCCESS) {
		write_log (_T("LCD: lgLcdEnumerateEx() returned %d\n"), ret);
		lcd_close ();
		return 0;
	}
	lbh = (lgLcdBitmapHeader*)xcalloc (uae_u8, sizeof (lgLcdBitmapHeader) + desc.Width * (desc.Height + 20));
	lbh->Format = LGLCD_BMP_FORMAT_160x43x1;
	bitmap = (uae_u8*)lbh + sizeof (lgLcdBitmapHeader);
	origbitmap = xcalloc (uae_u8, desc.Width * desc.Height);
	memset (&octx, 0, sizeof (octx));
	octx.connection = cctx.connection;
	octx.index = 0;
	ret = lgLcdOpen (&octx);
	if (ret != ERROR_SUCCESS) {
		write_log (_T("LCD: lgLcdOpen() returned %d\n"), ret);
		lcd_close ();
		return 0;
	}
	device = octx.device;

	bmp = LoadBitmap (hInst, MAKEINTRESOURCE(IDB_LCD160X43));
	dc = CreateCompatibleDC (NULL);
	SelectObject (dc, bmp);
	GetObject (bmp, sizeof (binfo), &binfo);
	for (y = 0; y < binfo.bmHeight; y++) {
		for (x = 0; x < binfo.bmWidth; x++) {
			bitmap[y * binfo.bmWidth + x] = GetPixel (dc, x, y) == 0 ? 0xff : 0;
		}
	}
	numbers = bitmap + desc.Width * desc.Height;
	memcpy (origbitmap, bitmap, desc.Width * desc.Height);
	DeleteDC (dc);

	write_log (_T("LCD: '%s' enabled\n"), desc.deviceDisplayName);
	return 1;
}

static void dorect (int *crd, int inv)
{
	int yy, xx;
	int x = crd[0], y = crd[1], w = crd[2], h = crd[3];
	for (yy = y; yy < y + h; yy++) {
		for (xx = x; xx < x + w; xx++) {
			uae_u8 b = origbitmap[yy * desc.Width + xx];
			if (inv)
				b = b == 0 ? 0xff : 0;
			bitmap[yy * desc.Width + xx] = b;
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
			dst = bitmap + (yy + y) * desc.Width + (xx + x);
			src = numbers + n * numbers_width + yy * desc.Width + xx;
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
	if (!inited)
		return;
	if (old_pri == priority)
		return;
	if (lgLcdSetAsLCDForegroundApp (device, priority ? LGLCD_LCD_FOREGROUND_APP_YES : LGLCD_LCD_FOREGROUND_APP_NO) == ERROR_SUCCESS)
		old_pri = priority;
}

void lcd_update (int led, int on)
{
	int track, x, y;

	if (!inited)
		return;

	if (led < 0) {
		lgLcdUpdateBitmap (device, lbh, LGLCD_PRIORITY_IDLE_NO_SHOW);
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
	lgLcdUpdateBitmap (device, lbh, LGLCD_ASYNC_UPDATE (LGLCD_PRIORITY_NORMAL + 1));
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
