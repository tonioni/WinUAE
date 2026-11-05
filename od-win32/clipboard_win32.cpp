
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>

#include "options.h"
#include "traps.h"
#include "clipboard_win32.h"
#include "clipboard.h"
#include "clipboard_common.h"
#include "keybuf.h"
#include "memory.h"
#include "autoconf.h"
#include "native2amiga_api.h"

static clipboard_transfer transfer;

static HWND chwnd;
static HDC hdc;
static int clipopen;
static int clipactive;
static int clipboard_change;
static void *clipboard_delayed_data;
static int clipboard_delayed_size;
static bool clip_disabled;

static void to_keyboard(const TCHAR *pctxt)
{
	uae_char *txt = ua(pctxt);
	keybuf_inject(txt);
	xfree(txt);
}

static void to_iff_text(TrapContext *, const TCHAR *pctxt)
{
	char *text = ua(pctxt);
	transfer.pending = clipboard_encode_text(text);
	xfree(text);
	transfer.start();
}

static int clipboard_put_text (const TCHAR *txt);
static void from_iff_text(uae_u8 *addr, uae_u32 len)
{
	std::string text;
	if (clipboard_decode_text(addr, len, &text, true)) {
		TCHAR *pctxt = my_strdup_ansi(text.c_str());
		clipboard_put_text(pctxt);
		xfree(pctxt);
	}
}

static void to_iff_ilbm(TrapContext *, HBITMAP hbmp, bool initial)
{
	BITMAP bmp;
	if (!GetObject(hbmp, sizeof bmp, &bmp) || bmp.bmWidth <= 0 || bmp.bmHeight <= 0 ||
		(bmp.bmBitsPixel != 8 && bmp.bmBitsPixel != 32)) {
		return;
	}
	const size_t stride = ((size_t)bmp.bmWidth * bmp.bmBitsPixel / 8 + 3) & ~3;
	if (stride * bmp.bmHeight > (initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT)) {
		return;
	}
	clipboard_bitmap bitmap;
	bitmap.width = bmp.bmWidth;
	bitmap.height = bmp.bmHeight;
	bitmap.pixels.resize((size_t)bitmap.width * bitmap.height);
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof info.bmiHeader;
	info.bmiHeader.biWidth = bitmap.width;
	info.bmiHeader.biHeight = -bitmap.height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	if (!GetDIBits(hdc, hbmp, 0, bitmap.height, bitmap.pixels.data(), &info, DIB_RGB_COLORS)) {
		return;
	}
	for (uae_u32 &pixel : bitmap.pixels) {
		pixel &= 0xffffff;
	}
	if (clipboard_encode_bitmap(bitmap, false, &transfer.pending)) {
		transfer.start();
	}
}

static int clipboard_put_bmp(HBITMAP hbmp);
static void from_iff_ilbm(uae_u8 *data, uae_u32 len)
{
	clipboard_bitmap bitmap;
	if (!clipboard_decode_bitmap(data, len, &bitmap, true, true)) {
		return;
	}
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof info.bmiHeader;
	info.bmiHeader.biWidth = bitmap.width;
	info.bmiHeader.biHeight = -bitmap.height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HBITMAP hbmp = CreateDIBitmap(hdc, &info.bmiHeader, CBM_INIT,
		bitmap.pixels.data(), &info, DIB_RGB_COLORS);
	if (hbmp) {
		clipboard_put_bmp(hbmp);
	}
}

static void from_iff(TrapContext *ctx, uaecptr data, uae_u32 len)
{
	uae_u8 *buf;

	if (len < 18)
		return;
	if (!trap_valid_address(ctx, data, len))
		return;
	buf = xmalloc(uae_u8, len);
	trap_get_bytes(ctx, buf, data, len);

#if 0
	FILE *f = fopen("clipboard.dat", "wb");
	if (f) {
		fwrite(buf, 1, len, f);
		fclose(f);
	}
#endif

	if (clipboard_debug)
		clipboard_debug_dump(ctx, _T("clipboard_a2p"), data, len);
	if (!memcmp ("FORM", buf, 4)) {
		if (!memcmp ("FTXT", buf + 8, 4))
			from_iff_text(buf, len);
		if (!memcmp ("ILBM", buf + 8, 4))
			from_iff_ilbm(buf, len);
	}
	xfree(buf);
}

void clipboard_disable (bool disabled)
{
	clip_disabled = disabled;
}

static void clipboard_read(TrapContext *ctx, HWND hwnd, bool keyboardinject, bool initial)
{
	HGLOBAL hglb;
	UINT f;
	int text = FALSE, bmp = FALSE;

	if (clip_disabled || !hwnd)
		return;
	if (!transfer.pending.empty()) {
		if (clipboard_log) {
			write_log (_T("clipboard: read windows clipboard but ignored because previous clip transfer still active\n"));
		}
		return;
	}
	clipboard_change = 0;
	if (clipboard_log) {
		write_log (_T("clipboard: read windows clipboard\n"));
	}
	if (!filesys_heartbeat())
		return;
	if (!OpenClipboard (hwnd))
		return;
	f = 0;
	while (f = EnumClipboardFormats (f)) {
		if (f == CF_UNICODETEXT)
			text = TRUE;
		if (f == CF_BITMAP && !keyboardinject)
			bmp = TRUE;
	}
	if (text) {
		hglb = GetClipboardData (CF_UNICODETEXT); 
		if (hglb != NULL) { 
			TCHAR *lptstr = (TCHAR*)GlobalLock (hglb); 
			if (lptstr != NULL) {
				size_t len = _tcslen(lptstr);
				if (len > (initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT)) {
					if (clipboard_log) {
						write_log(_T("clipboard: initial size too large, ignored.\n"));
					}
				} else {
					if (clipboard_log) {
						write_log (_T("clipboard: CF_UNICODETEXT '%s'\n"), lptstr);
					}
					if (keyboardinject) {
						to_keyboard(lptstr);
					} else {
						to_iff_text(ctx, lptstr);
					}
				}
				GlobalUnlock (hglb);
			}
		}
	} else if (bmp) {
		HBITMAP hbmp = (HBITMAP)GetClipboardData (CF_BITMAP);
		if (hbmp != NULL) {
			if (clipboard_log) {
				write_log (_T("clipboard: CF_BITMAP\n"));
			}
			to_iff_ilbm(ctx, hbmp, initial);
		}
	}
	CloseClipboard ();
}

static void clipboard_free_delayed (void)
{
	if (clipboard_delayed_data == 0)
		return;
	if (clipboard_delayed_size < 0)
		xfree (clipboard_delayed_data);
	else
		DeleteObject (clipboard_delayed_data);
	clipboard_delayed_data = 0;
	clipboard_delayed_size = 0;
}

void clipboard_changed (HWND hwnd)
{
	if (clipboard_log) {
		write_log (_T("clipboard: windows clipboard changed message\n"));
	}
	if (!transfer.initialized)
		return;
	if (!transfer.guest_data)
		return;
	if (clipopen)
		return;
	if (!clipactive) {
		clipboard_change = 1;
		return;
	}
	clipboard_read(NULL, hwnd, false, false);
}

static int clipboard_put_bmp_real (HBITMAP hbmp)
{
	int ret = FALSE;

	if (!OpenClipboard (chwnd)) 
		return ret;
	clipopen++;
	EmptyClipboard ();
	SetClipboardData (CF_BITMAP, hbmp); 
	ret = TRUE;
	CloseClipboard ();
	clipopen--;
	if (clipboard_log) {
		write_log (_T("clipboard: BMP written to windows clipboard\n"));
	}
	return ret;
}

static int clipboard_put_text_real (const TCHAR *txt)
{
	HGLOBAL hglb;
	int ret = FALSE;

	if (!OpenClipboard (chwnd)) 
		return ret;
	clipopen++;
	EmptyClipboard (); 
	hglb = GlobalAlloc (GMEM_MOVEABLE, (_tcslen (txt) + 1) * sizeof (TCHAR));
	if (hglb) {
		TCHAR *lptstr = (TCHAR*)GlobalLock (hglb);
		_tcscpy (lptstr, txt);
		GlobalUnlock (hglb);
		SetClipboardData (CF_UNICODETEXT, hglb); 
		ret = TRUE;
	}
	CloseClipboard ();
	clipopen--;
	if (clipboard_log) {
		write_log (_T("clipboard: text written to windows clipboard\n"));
	}
	return ret;
}

static int clipboard_put_text (const TCHAR *txt)
{
	if (!clipactive)
		return clipboard_put_text_real (txt);
	clipboard_free_delayed ();
	clipboard_delayed_data = my_strdup (txt);
	clipboard_delayed_size = -1;
	return 1;
}

static int clipboard_put_bmp (HBITMAP hbmp)
{
	if (!clipactive)
		return clipboard_put_bmp_real (hbmp);
	clipboard_delayed_data = (void*)hbmp;
	clipboard_delayed_size = 1;
	return 1;
}

void amiga_clipboard_die(TrapContext *ctx)
{
	transfer.signaling = 0;
	write_log (_T("clipboard not initialized\n"));
}

void amiga_clipboard_init(TrapContext *ctx)
{
	transfer.signaling = 0;
	write_log (_T("clipboard initialized\n"));
	transfer.initialized = 1;
	clipboard_read(ctx, chwnd, false, true);
}

void amiga_clipboard_task_start(TrapContext *ctx, uaecptr data)
{
	transfer.guest_data = data;
	transfer.signaling = 1;
	write_log (_T("clipboard task init: %08x\n"), transfer.guest_data);
}

uae_u32 amiga_clipboard_proc_start(TrapContext *ctx)
{
	write_log (_T("clipboard process init: %08x\n"), transfer.guest_data);
	transfer.signaling = 1;
	return transfer.guest_data;
}

void amiga_clipboard_got_data(TrapContext *ctx, uaecptr data, uae_u32 size, uae_u32 actual)
{
	if (!transfer.initialized) {
		write_log (_T("clipboard: got_data() before initialized!?\n"));
		return;
	}
	if (clipboard_log) {
		write_log (_T("clipboard: <-amiga, %08x, %08x %d %d\n"), transfer.guest_data, data, size, actual);
	}
	from_iff(ctx, data, actual);
}

int amiga_clipboard_want_data(TrapContext *ctx)
{
	return transfer.want_data(ctx);
}

void clipboard_active(HWND hwnd, int active)
{
	clipactive = active;
	if (!transfer.initialized || !hwnd)
		return;
	if (clipactive && clipboard_change) {
		clipboard_read(NULL, hwnd, false, false);
	}
	if (!clipactive && clipboard_delayed_data) {
		if (clipboard_delayed_size < 0) {
			clipboard_put_text_real ((TCHAR*)clipboard_delayed_data);
			xfree (clipboard_delayed_data);
		} else {
			clipboard_put_bmp_real ((HBITMAP)clipboard_delayed_data);
		}
		clipboard_delayed_data = NULL;
		clipboard_delayed_size = 0;
	}
}

void clipboard_vsync(void)
{
	if (filesys_heartbeat()) {
		transfer.vsync(true);
	}
}

void clipboard_reset(void)
{
	if (clipboard_log) {
		write_log(_T("clipboard: reset (%08x)\n"), transfer.guest_data);
	}
	clipboard_unsafeperiod();
	clipboard_free_delayed ();
	transfer.reset();
	clip_disabled = false;
	ReleaseDC (chwnd, hdc);
}

void clipboard_init (HWND hwnd)
{
	chwnd = hwnd;
	hdc = GetDC (chwnd);
}

void target_paste_to_keyboard(void)
{
	clipboard_read(NULL, chwnd, true, false);
}

// force 2 second delay before accepting new data
void clipboard_unsafeperiod(void)
{
	transfer.unsafe_period();
}
