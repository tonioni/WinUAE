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
static char *td_new_numbers;

void deletestatusline(int monid)
{
	if (monid)
		return;
	struct AmigaMonitor *mon = &AMonitors[monid];
	if (!statusline_hdc)
		return;
	if (statusline_bitmap)
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

static void create_led_font(HWND parent, int monid)
{
	HDC hdc;
	LPLOGPALETTE lp;
	HPALETTE hpal;
	BITMAPINFO *bi;
	BITMAPINFOHEADER *bih;
	HBITMAP bitmap = NULL;
	int width = 128;
	int height = 128;
	void *bm;

	statusline_set_font(NULL, 0, 0);

	xfree(td_new_numbers);

	hdc = CreateCompatibleDC(NULL);
	if (hdc) {
		int y = getdpiforwindow(parent);
		int fontsize = -MulDiv(6, y, 72);
		fontsize = fontsize * statusline_get_multiplier(monid) / 100;
		lp = (LOGPALETTE *)xcalloc(uae_u8, sizeof(LOGPALETTE) + 3 * sizeof(PALETTEENTRY));
		if (lp) {
			lp->palNumEntries = 4;
			lp->palVersion = 0x300;
			lp->palPalEntry[1].peBlue = lp->palPalEntry[1].peGreen = lp->palPalEntry[0].peRed = 0x10;
			lp->palPalEntry[2].peBlue = lp->palPalEntry[2].peGreen = lp->palPalEntry[2].peRed = 0xff;
			lp->palPalEntry[3].peBlue = lp->palPalEntry[3].peGreen = lp->palPalEntry[3].peRed = 0x7f;
			hpal = CreatePalette(lp);
			if (hpal) {
				SelectPalette(hdc, hpal, FALSE);
				bi = (BITMAPINFO*)xcalloc(uae_u8, sizeof(BITMAPINFOHEADER) + 4 * sizeof(RGBQUAD));
				if (bi) {
					bih = &bi->bmiHeader;
					bih->biSize = sizeof(BITMAPINFOHEADER);
					bih->biWidth = width;
					bih->biHeight = -height;
					bih->biPlanes = 1;
					bih->biBitCount = 8;
					bih->biCompression = BI_RGB;
					bih->biClrUsed = 4;
					bih->biClrImportant = 4;
					bi->bmiColors[1].rgbBlue = bi->bmiColors[1].rgbGreen = bi->bmiColors[1].rgbRed = 0x10;
					bi->bmiColors[2].rgbBlue = bi->bmiColors[2].rgbGreen = bi->bmiColors[2].rgbRed = 0xff;
					bi->bmiColors[3].rgbBlue = bi->bmiColors[3].rgbGreen = bi->bmiColors[3].rgbRed = 0x7f;
					bitmap = CreateDIBSection(hdc, bi, DIB_RGB_COLORS, &bm, NULL, 0);
					if (bitmap) {
						SelectObject(hdc, bitmap);
						RealizePalette(hdc);
						HFONT font = CreateFont(fontsize, 0,
							0, 0,
							FW_NORMAL,
							FALSE,
							FALSE,
							FALSE,
							DEFAULT_CHARSET,
							OUT_TT_PRECIS,
							CLIP_DEFAULT_PRECIS,
							PROOF_QUALITY,
							FIXED_PITCH | FF_DONTCARE,
							_T("Lucida Console"));
						if (font) {
							SelectObject(hdc, font);
							SetTextColor(hdc, PALETTEINDEX(2));
							SetBkColor(hdc, PALETTEINDEX(1));
							TEXTMETRIC tm;
							GetTextMetrics(hdc, &tm);
							int w = 0;
							int h = tm.tmAscent + 2;
							for (int i = 0; i < td_characters[i]; i++) {
								SIZE sz;
								if (GetTextExtentPoint32(hdc, &td_characters[i], 1, &sz)) {
									if (sz.cx > w)
										w = sz.cx;
								}
							}
							int offsetx = 10;
							int offsety = 10 - 1;
							w += 1;
							td_new_numbers = xcalloc(char, w * h * NUMBERS_NUM);
							if (td_new_numbers) {
								for (int i = 0; i < td_characters[i]; i++) {
									SetBkMode(hdc, OPAQUE);
									BitBlt(hdc, 0, 0, width, height, NULL, 0, 0, BLACKNESS);
									TextOut(hdc, 10, 10, &td_characters[i], 1);
									for (int y = 0; y < h; y++) {
										uae_u8 *src = (uae_u8 *)bm + (y + offsety) * width + offsetx;
										char *dst = td_new_numbers + i * w + y * w * NUMBERS_NUM;
										for (int x = 0; x < w; x++) {
											uae_u8 b = *src++;
											if (b == 2) {
												*dst = 'x';
											} else {
												*dst = '-';
											}
											dst++;
										}
									}

									BitBlt(hdc, 0, 0, width, height, NULL, 0, 0, BLACKNESS);
									SetBkMode(hdc, TRANSPARENT);
									TextOut(hdc, 9, 9, &td_characters[i], 1);
									TextOut(hdc, 11, 9, &td_characters[i], 1);
									TextOut(hdc, 11, 11, &td_characters[i], 1);
									TextOut(hdc, 9, 11, &td_characters[i], 1);
									for (int y = 0; y < h; y++) {
										uae_u8 *src = (uae_u8 *)bm + (y + offsety) * width + offsetx;
										char *dst = td_new_numbers + i * w + y * w * NUMBERS_NUM;
										for (int x = 0; x < w; x++) {
											uae_u8 b = *src++;
											if (b == 2 && dst[0] == '-') {
												*dst = '+';
											}
											dst++;
										}
									}

								}
							}
							statusline_set_font(td_new_numbers, w, h);
							DeleteObject(font);
						}
						DeleteObject(bitmap);
					}
					xfree(bi);
				}
				DeleteObject(hpal);
			}
			xfree(lp);
		}
		ReleaseDC(NULL, hdc);
	}
}

bool createstatusline(HWND parentHwnd, int monid)
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
	if (!lp)
		return false;
	lp->palNumEntries = 4;
	lp->palVersion = 0x300;
	lp->palPalEntry[1].peBlue = lp->palPalEntry[1].peGreen = lp->palPalEntry[0].peRed = 0x10;
	lp->palPalEntry[2].peBlue = lp->palPalEntry[2].peGreen = lp->palPalEntry[2].peRed = 0xff;
	lp->palPalEntry[3].peBlue = lp->palPalEntry[3].peGreen = lp->palPalEntry[3].peRed = 0x7f;
	statusline_palette = CreatePalette(lp);
	xfree(lp);
	SelectPalette(statusline_hdc, statusline_palette, FALSE);
	statusline_width = (WIN32GFX_GetWidth(mon) + 31) & ~31;
	bi = (BITMAPINFO*)xcalloc(uae_u8, sizeof(BITMAPINFOHEADER) + 4 * sizeof(RGBQUAD));
	if (bi) {
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
	}
	if (!statusline_bitmap) {
		deletestatusline(mon->monitor_id);
		return false;
	}
	SelectObject(statusline_hdc, statusline_bitmap);
	RealizePalette(statusline_hdc);

	create_led_font(parentHwnd, monid);

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

	text = statusline_fetch();
	//text = _T("Testing string 123!");
	if (!text)
		return;
	BitBlt(statusline_hdc, 0, 0, statusline_width, statusline_height, NULL, 0, 0, BLACKNESS);

	SIZE size;
	if (GetTextExtentPoint32(statusline_hdc, text, uaetcslen(text), &size)) {
		textwidth = size.cx;
		if (isfullscreen()) {
			if (td_numbers_pos & TD_RIGHT) {
				bar_xstart = width - td_numbers_padx - VISIBLE_LEDS * td_width;
				x = bar_xstart - textwidth - td_led_width;
			} else {
				bar_xstart = td_numbers_padx;
				x = bar_xstart + textwidth + td_led_width;
			}
		}
	}
	if (x < 0)
		x = 0;
	TextOut(statusline_hdc, x, y, text, uaetcslen(text));

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
