
#include "sysconfig.h"
#include "sysdeps.h"

#if defined(LOGITECHLCD)

#include "gui.h"
#include "lcd.h"

#include <lglcd.h>

static int inited;
static lgLcdConnectContext cctx;
static lgLcdDeviceDesc desc;
static int device;
static lgLcdBitmapHeader *lbh;
static uae_u8 *bitmap;

#define TD_NUM_WIDTH 7
#define TD_NUM_HEIGHT 7
#define NUMBERS_NUM 16
#define TD_LED_WIDTH 24

static char *numbers = { /* ugly  0123456789CHD%x */
"+++++++--++++-+++++++++++++++++-++++++++++++++++++++++++++++++++++++++++++++-++++++-++++----++---+++++++++++++++"
"+xxxxx+--+xx+-+xxxxx++xxxxx++x+-+x++xxxxx++xxxxx++xxxxx++xxxxx++xxxxx++xxxx+-+x++x+-+xxx++-+xx+-+x+xxxxxxxxxxxx+"
"+x+++x+--++x+-+++++x++++++x++x+++x++x++++++x++++++++++x++x+++x++x+++x++x++++-+x++x+-+x++x+--+x++x++xxxxxxxxxxxx+"
"+x+-+x+---+x+-+xxxxx++xxxxx++xxxxx++xxxxx++xxxxx+--++x+-+xxxxx++xxxxx++x+----+xxxx+-+x++x+----+x+++xxxxxxxxxxxx+"
"+x+++x+---+x+-+x++++++++++x++++++x++++++x++x+++x+--+x+--+x+++x++++++x++x++++-+x++x+-+x++x+---+x+x++xxxxxxxxxxxx+"
"+xxxxx+---+x+-+xxxxx++xxxxx+----+x++xxxxx++xxxxx+--+x+--+xxxxx++xxxxx++xxxx+-+x++x+-+xxx+---+x++xx+xxxxxxxxxxxx+"
"+++++++---+++-++++++++++++++----+++++++++++++++++--+++--++++++++++++++++++++-++++++-++++----------++++++++++++++"
};
static char *one = { "x" };

void lcd_close(void)
{
    lgLcdDeInit();
    xfree(lbh);
    lbh = NULL;
    bitmap = NULL;
    inited = 0;
}

static int lcd_init(void)
{
    DWORD ret;
    lgLcdOpenContext octx;

    ret = lgLcdInit();
    if (ret != ERROR_SUCCESS) {
	if (ret == RPC_S_SERVER_UNAVAILABLE || ret == ERROR_OLD_WIN_VERSION) {
	    write_log ("LCD: Logitech LCD system not detected\n");
	    return 0;
	}
	write_log ("LCD: lgLcdInit() returned %d\n", ret);
	return 0;
    }
    memset (&cctx, 0, sizeof (cctx));
    cctx.appFriendlyName = "WinUAE";
    cctx.isPersistent = TRUE;
    cctx.isAutostartable = FALSE;
    ret = lgLcdConnect(&cctx);
    if (ret != ERROR_SUCCESS) {
	write_log ("LCD: lgLcdConnect() returned %d\n", ret);
	lcd_close();
	return 0;
    }
    ret = lgLcdEnumerate(cctx.connection, 0, &desc);
    if (ret != ERROR_SUCCESS) {
	write_log ("LCD: lgLcdEnumerate() returned %d\n", ret);
	lcd_close();
	return 0;
    }
    lbh = xcalloc (1, sizeof (lgLcdBitmapHeader) + desc.Width * desc.Height);
    lbh->Format = LGLCD_BMP_FORMAT_160x43x1;
    bitmap = (uae_u8*)lbh + sizeof (lgLcdBitmapHeader);
    memset (&octx, 0, sizeof (octx));
    octx.connection = cctx.connection;
    octx.index = 0;
    ret = lgLcdOpen(&octx);
    if (ret != ERROR_SUCCESS) {
	write_log("LCD: lgLcdOpen() returned %d\n", ret);
	lcd_close();
	return 0;
    }
    device = octx.device;
    write_log("LCD: Logitech LCD system initialized\n");
    return 1;
}

static void putnumber(int x, int y, int n, int inv)
{
    int xx, yy;
    uae_u8 *dst, *src;
    for (yy = 0; yy < TD_NUM_HEIGHT; yy++) {
	for (xx = 0; xx < TD_NUM_WIDTH; xx++) {
	    dst = bitmap + (yy + y) * desc.Width + (xx + x);
	    src = numbers + n * TD_NUM_WIDTH + yy * TD_NUM_WIDTH * NUMBERS_NUM + xx;
	    *dst = 0;
	    if (*src == 'x')
		*dst = 0xff;
	    if (inv)
		*dst ^= 0xff;
	}
    }
}

static void putnumbers(int x, int y, int num, int inv)
{
    putnumber(x, y, num / 10, inv);
    putnumber(x + TD_NUM_WIDTH, y, num % 10, inv);
}

void lcd_update(int led, int on)
{
    int track, x, y, y1, y2;

    if (!inited)
	return;

    x = 10;
    y1 = desc.Height - TD_NUM_HEIGHT - 2;
    y2 = y1 - TD_NUM_HEIGHT - 4;
    if (led >= 1 && led <= 4) {
	x += (led - 1) * TD_LED_WIDTH;
	y = y1;
        track = gui_data.drive_track[led - 1];
	putnumbers(x, y, track, on);
    } else if (led == 0) {
	y = y2;
	x += 2 * TD_LED_WIDTH;
	putnumber(x, y, 14, on);
	putnumber(x + TD_NUM_WIDTH, y, 15, on);
    } else if (led == 5) {
	y = y2;
	x += 3 * TD_LED_WIDTH;
	putnumber(x, y, 11, on);
	putnumber(x + TD_NUM_WIDTH, y, 12, on);
    } else if (led == 6) {
	y = y2;
	x += 4 * TD_LED_WIDTH;
	putnumber(x, y, 10, on);
	putnumber(x + TD_NUM_WIDTH, y, 12, on);
    } else if (led == 7) {
	y = y2;
	x += 1 * TD_LED_WIDTH;
	putnumbers(x, y, gui_data.fps <= 999 ? gui_data.fps / 10 : 99, 0);
    } else if (led == 8) {
	y = y2;
	x += 0 * TD_LED_WIDTH;
	putnumbers(x, y, gui_data.idle <= 999 ? gui_data.idle / 10 : 99, 0);
    }
    lgLcdUpdateBitmap(device, lbh, LGLCD_ASYNC_UPDATE(LGLCD_PRIORITY_NORMAL));
}

int lcd_open(void)
{
    if (!inited) {
	if (!lcd_init())
	    return 0;
	inited = 1;
    }
    return 1;
}

#endif
