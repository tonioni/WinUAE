#include "sysconfig.h"

#include <windows.h>
#include <commctrl.h>

#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "win32.h"
#include "picasso96_win.h"
#include "win32gfx.h"
#include "registry.h"
#include "win32gui.h"
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
static char *td_new_numbers;
static int statusline_fontsize, statusline_fontstyle, statusline_fontweight;
static bool statusline_customfont;
static TCHAR statusline_fontname[256];

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


static char *ldp_font_bitmap;
static int ldp_font_width, ldp_font_height;

static void create_ldp_font(HWND parent)
{
	HDC hdc;
	LPLOGPALETTE lp;
	HPALETTE hpal;
	BITMAPINFO *bi;
	BITMAPINFOHEADER *bih;
	HBITMAP bitmap = NULL;
	int width = 128;
	int height = 128;
	int fontsize, fontweight;
	void *bm;
	const TCHAR *fn;

	xfree(ldp_font_bitmap);

	if (currprefs.genlock_image != 6 && currprefs.genlock_image != 7 && currprefs.genlock_image != 8) {
		return;
	}

	fontsize = 20;
	fontweight = FW_NORMAL;
	fn = statusline_fontname;
	if (currprefs.genlock_font[0]) {
		fn = currprefs.genlock_font;
	}

	hdc = CreateCompatibleDC(NULL);
	if (hdc) {
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
				bi = (BITMAPINFO *)xcalloc(uae_u8, sizeof(BITMAPINFOHEADER) + 4 * sizeof(RGBQUAD));
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
						HFONT font = CreateFont(-fontsize, 0,
							0, 0,
							fontweight,
							0,
							FALSE,
							FALSE,
							DEFAULT_CHARSET,
							OUT_TT_PRECIS,
							CLIP_DEFAULT_PRECIS,
							PROOF_QUALITY,
							FIXED_PITCH | FF_DONTCARE,
							statusline_fontname);
						if (font) {
							SelectObject(hdc, font);
							SetTextColor(hdc, PALETTEINDEX(2));
							SetBkColor(hdc, PALETTEINDEX(1));
							TEXTMETRIC tm;
							GetTextMetrics(hdc, &tm);
							int w = 0;
							int h = tm.tmAscent + 2;
							int total = 128 - 32;
							for (int i = 32; i < 128; i++) {
								SIZE sz;
								TCHAR ch = i;
								if (GetTextExtentPoint32(hdc,&ch, 1, &sz)) {
									if (sz.cx > w)
										w = sz.cx;
								}
							}
							int offsetx = 10;
							int offsety = 10 - 1;
							int fxd = fontsize - w;
							if (fxd >= 1) {
								fxd -= 1;
								offsetx -= fxd / 2;
								w += fxd / 2;
								if (offsetx < 0) {
									offsetx = 0;
								}
							}
							ldp_font_bitmap = xcalloc(char, w * h * total);
							if (ldp_font_bitmap) {
								for (int i = 32; i < 128; i++) {
									TCHAR ch = i;
									SetBkMode(hdc, OPAQUE);
									BitBlt(hdc, 0, 0, width, height, NULL, 0, 0, BLACKNESS);
									TextOut(hdc, 10, 10, &ch, 1);
									char *dst = ldp_font_bitmap + (i - 32) * w * h;
									for (int y = 0; y < h; y++) {
										uae_u8 *src = (uae_u8 *)bm + (y + offsety) * width + offsetx;
										for (int x = 0; x < w; x++) {
											uae_u8 b = *src++;
											if (b == 2) {
												*dst = 'x';
											}
											dst++;
										}
									}
								}
								ldp_font_width = w;
								ldp_font_height = h;
							}
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

	statusline_fontsize = 8;
	_tcscpy(statusline_fontname, _T("Lucida Console"));
	statusline_fontweight = FW_NORMAL;
	statusline_customfont = regqueryfont(NULL, NULL, _T("OSDFont"), statusline_fontname, &statusline_fontsize, &statusline_fontstyle, &statusline_fontweight);

	hdc = CreateCompatibleDC(NULL);
	if (hdc) {
		int y = getdpiforwindow(parent);
		if (isfullscreen() <= 0) {
			statusline_fontsize = -MulDiv(statusline_fontsize, y, 72);
		}
		statusline_fontsize = statusline_fontsize * statusline_get_multiplier(monid) / 100;
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
						HFONT font = CreateFont(statusline_fontsize, 0,
							0, 0,
							statusline_fontweight,
							(statusline_fontstyle & ITALIC_FONTTYPE) != 0,
							FALSE,
							FALSE,
							DEFAULT_CHARSET,
							OUT_TT_PRECIS,
							CLIP_DEFAULT_PRECIS,
							PROOF_QUALITY,
							FIXED_PITCH | FF_DONTCARE,
							statusline_fontname);
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
							if (h < -statusline_fontsize) {
								h = -statusline_fontsize;
							}
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

	create_led_font(parentHwnd, monid);
	create_ldp_font(parentHwnd);

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
	statusline_fontsize -= (2 * TD_DEFAULT_PADY - 1);
	statusline_height = -statusline_fontsize;
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

	statusline_font = CreateFont(statusline_fontsize, 0,
		0, 0,
		statusline_fontweight,
		(statusline_fontstyle & ITALIC_FONTTYPE) != 0,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		PROOF_QUALITY,
		VARIABLE_PITCH | FF_DONTCARE,
		statusline_fontname);
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
	if (mon->hStatusWnd)
		PostMessage(mon->hStatusWnd, SB_SETTEXT, (WPARAM)((window_led_msg_start) | SBT_OWNERDRAW), (LPARAM)_T(""));
}

void statusline_render(int monid, uae_u8 *buf, int bpp, int pitch, int maxwidth, int maxheight, uae_u32 *rc, uae_u32 *gc, uae_u32 *bc, uae_u32 *alpha)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	uae_u32 white = rc[0xff] | gc[0xff] | bc[0xff] | (alpha ? alpha[0xff] : 0);
	uae_u32 black = rc[0x00] | gc[0x00] | bc[0x00] | (alpha ? alpha[0xa0] : 0);
	const TCHAR *text;
	int y = 0, x = 10, textwidth = 0;
	int bar_xstart;

	if (monid || !statusline_hdc) {
		return;
	}

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
				bar_xstart = td_numbers_width - td_numbers_padx - VISIBLE_LEDS * td_width;
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

	for (int y = 0; y < maxheight && y < statusline_height; y++) {
		uae_u8 *src = (uae_u8*)statusline_bm + y * statusline_width;
		uae_u32 *dst2 = (uae_u32*)(buf + pitch * y);
		for (int x = 0; x < maxwidth && x < statusline_width; x++) {
			uae_u8 b = *src++;
			if (b) {
				if (b == 2)
					*dst2 = white;
				else if (b == 1)
					*dst2 = black;
			}
			dst2++;
		}
	}
}

void ldp_render(const char *txt, int len, uae_u8 *buf, struct vidbuffer *vd, int dx, int dy, int mx, int my)
{
	if (!ldp_font_bitmap) {
		return;
	}
	int bpp = vd->pixbytes;
	uae_u32 white = 0xffffff;
	uae_u8 *dbuf2 = buf + dy * vd->rowbytes + dx * bpp;
	for (int i = 0; i < len; i++) {
		uae_u8 *dbuf = dbuf2 + i * ldp_font_width * mx * bpp;
		char ch = *txt++;
		if (ch >= 128 || ch < 32) {
			ch = 0;
		} else {
			ch -= 32;
		}
		char *font = ldp_font_bitmap + ch * ldp_font_width * ldp_font_height;
		for (int y = 0; y < ldp_font_height; y++) {
			for (int mmy = 0; mmy < my; mmy++) {
				char *font2 = font;
				for (int x = 0; x < ldp_font_width; x++) {
					if (*font2) {
						for (int mmx = 0; mmx < mx; mmx++) {
							int xx = x * mx + mmx;
							if (dy + y * my + mmy >= 0 && dy + y * my + mmy < vd->inheight) {
								if (dx + xx >= 0 && dx + xx < vd->inwidth) {
									dbuf[xx * bpp + 0] = 0xff;
									dbuf[xx * bpp + 1] = 0xff;
									if (bpp == 4) {
										dbuf[xx * bpp + 2] = 0xff;
										dbuf[xx * bpp + 3] = 0xff;
									}
								}
							}
						}
					}
					font2++;
				}
				dbuf += vd->rowbytes;
			}
			font += ldp_font_width;
		}
		dx += ldp_font_width;
	}
}
