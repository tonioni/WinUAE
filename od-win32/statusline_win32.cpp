#include "sysconfig.h"

#include <windows.h>
#include <commctrl.h>

#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "win32.h"
#include "picasso96_win.h"
#include "win32gfx.h"
#include "statusline.h"
#include "gui.h"
#include "xwin.h"

static HDC statusline_hdc;
static HBITMAP statusline_bitmap;
static void *statusline_bm;
static int statusline_width;
static int statusline_height = TD_TOTAL_HEIGHT;
static HFONT statusline_font;
static HPALETTE statusline_palette;
static bool statusline_was_updated;

bool softstatusline(void)
{
	if (currprefs.gfx_api > 0)
		return false;
	return (currprefs.leds_on_screen & STATUSLINE_TARGET) == 0;
}

void deletestatusline(int monid)
{
	if (monid)
		return;
	struct AmigaMonitor *mon = &AMonitors[monid];
	if (!statusline_hdc)
		return;
	if (!statusline_bitmap)
		DeleteObject(statusline_bitmap);
	if (statusline_hdc)
		ReleaseDC(NULL, statusline_hdc);
	if (statusline_font)
		DeleteObject(statusline_font);
	if (statusline_palette)
		DeleteObject(statusline_palette);
	statusline_bitmap = NULL;
	statusline_hdc = NULL;
	statusline_font = NULL;
	statusline_palette = NULL;
}

bool createstatusline(int monid)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	BITMAPINFO *bi;
	BITMAPINFOHEADER *bih;
	LOGPALETTE *lp;

	if (monid)
		return false;
	deletestatusline(mon->monitor_id);
	statusline_hdc = CreateCompatibleDC(NULL);
	if (!statusline_hdc)
		return false;
	lp = (LOGPALETTE*)xcalloc(uae_u8, sizeof(LOGPALETTE) + 3 * sizeof(PALETTEENTRY));
	lp->palNumEntries = 4;
	lp->palVersion = 0x300;
	lp->palPalEntry[1].peBlue = lp->palPalEntry[1].peGreen = lp->palPalEntry[0].peRed = 0x10;
	lp->palPalEntry[2].peBlue = lp->palPalEntry[2].peGreen = lp->palPalEntry[2].peRed = 0xff;
	lp->palPalEntry[3].peBlue = lp->palPalEntry[3].peGreen = lp->palPalEntry[3].peRed = 0x7f;
	statusline_palette = CreatePalette(lp);
	SelectPalette(statusline_hdc, statusline_palette, FALSE);
	statusline_width = (WIN32GFX_GetWidth(mon) + 31) & ~31;
	bi = (BITMAPINFO*)xcalloc(uae_u8, sizeof(BITMAPINFOHEADER) + 4 * sizeof(RGBQUAD));
	bih = &bi->bmiHeader;
	bih->biSize = sizeof(BITMAPINFOHEADER);
	bih->biWidth = statusline_width;
	bih->biHeight = -statusline_height;
	bih->biPlanes = 1;
	bih->biBitCount = 8;
	bih->biCompression = BI_RGB;
	bih->biClrUsed = 4;
	bih->biClrImportant = 4;
	bi->bmiColors[1].rgbBlue = bi->bmiColors[1].rgbGreen = bi->bmiColors[1].rgbRed = 0x10;
	bi->bmiColors[2].rgbBlue = bi->bmiColors[2].rgbGreen = bi->bmiColors[2].rgbRed = 0xff;
	bi->bmiColors[3].rgbBlue = bi->bmiColors[3].rgbGreen = bi->bmiColors[3].rgbRed = 0x7f;
	statusline_bitmap = CreateDIBSection(statusline_hdc, bi, DIB_RGB_COLORS, &statusline_bm, NULL, 0);
	xfree(bi);
	if (!statusline_bitmap) {
		deletestatusline(mon->monitor_id);
		return false;
	}
	SelectObject(statusline_hdc, statusline_bitmap);
	RealizePalette(statusline_hdc);

	statusline_font = CreateFont(-10, 0,
		0, 0,
		FW_NORMAL,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		PROOF_QUALITY,
		VARIABLE_PITCH | FF_DONTCARE,
		_T("Verdana"));
	SelectObject(statusline_hdc, statusline_font);
	SetTextColor(statusline_hdc, PALETTEINDEX(2));
	SetBkColor(statusline_hdc, PALETTEINDEX(1));
	SetBkMode(statusline_hdc, OPAQUE);
	return true;
}

void statusline_updated(int monid)
{
	if (monid)
		return;
	struct AmigaMonitor *mon = &AMonitors[monid];
	statusline_was_updated = true;
	if (mon->hStatusWnd)
		PostMessage(mon->hStatusWnd, SB_SETTEXT, (WPARAM)((window_led_msg_start) | SBT_OWNERDRAW), (LPARAM)_T(""));
}

void statusline_render(int monid, uae_u8 *buf, int bpp, int pitch, int width, int height, uae_u32 *rc, uae_u32 *gc, uae_u32 *bc, uae_u32 *alpha)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	uae_u32 white = rc[0xff] | gc[0xff] | bc[0xff] | (alpha ? alpha[0xff] : 0);
	uae_u32 back = rc[0x00] | gc[0x00] | bc[0x00] | (alpha ? alpha[0xa0] : 0);
	const TCHAR *text;
	int y = -1, x = 10, textwidth = 0;
	int bar_xstart;

	if (monid)
		return;

	if (currprefs.gf[WIN32GFX_IsPicassoScreen(mon)].gfx_filter == 0 && !currprefs.gfx_api)
		return;
	text = statusline_fetch();
	//text = _T("Testing string 123!");
	if (!text)
		return;
	BitBlt(statusline_hdc, 0, 0, statusline_width, statusline_height, NULL, 0, 0, BLACKNESS);

	SIZE size;
	if (GetTextExtentPoint32(statusline_hdc, text, _tcslen(text), &size)) {
		textwidth = size.cx;
		if (isfullscreen()) {
			if (td_pos & TD_RIGHT) {
				bar_xstart = width - TD_PADX - VISIBLE_LEDS * TD_WIDTH;
				x = bar_xstart - textwidth - TD_LED_WIDTH;
			} else {
				bar_xstart = TD_PADX;
				x = bar_xstart + textwidth + TD_LED_WIDTH;
			}
		}
	}
	if (x < 0)
		x = 0;
	TextOut(statusline_hdc, x, y, text, _tcslen(text));

	for (int y = 0; y < height && y < statusline_height; y++) {
		uae_u8 *src = (uae_u8*)statusline_bm + y * statusline_width;
		uae_u32 *dst2 = (uae_u32*)(buf + pitch * y);
		for (int x = 0; x < width && x < statusline_width; x++) {
			uae_u8 b = *src++;
			if (b) {
				if (b == 2)
					*dst2 = white;
				else if (b == 1)
					*dst2 = back;
			}
			dst2++;
		}
	}
}
