
#define PNG_SCREENSHOTS 1

#include <windows.h>
#include <ddraw.h>

#include <stdio.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "custom.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "opengl.h"
#include "registry.h"
#include "gfxfilter.h"

#include "png.h"

int screenshotmode = PNG_SCREENSHOTS;
int screenshot_originalsize = 0;

static void namesplit (TCHAR *s)
{
	int l;

	l = _tcslen (s) - 1;
	while (l >= 0) {
		if (s[l] == '.')
			s[l] = 0;
		if (s[l] == '\\' || s[l] == '/' || s[l] == ':' || s[l] == '?') {
			l++;
			break;
		}
		l--;
	}
	if (l > 0)
		memmove (s, s + l, (_tcslen (s + l) + 1) * sizeof (TCHAR));
}

static int toclipboard (BITMAPINFO *bi, void *bmp)
{
	int v = 0;
	uae_u8 *dib = 0;
	HANDLE hg;

	if (!OpenClipboard (hMainWnd))
		return v;
	EmptyClipboard ();
	hg = GlobalAlloc (GMEM_MOVEABLE | GMEM_DDESHARE, bi->bmiHeader.biSize + bi->bmiHeader.biSizeImage);
	if (hg) {
		dib = (uae_u8*)GlobalLock (hg);
		if (dib) {
			memcpy (dib, &bi->bmiHeader, bi->bmiHeader.biSize);
			memcpy (dib + bi->bmiHeader.biSize, bmp, bi->bmiHeader.biSizeImage);
		}
		GlobalUnlock (hg);
		if (SetClipboardData (CF_DIB, hg))
			v = 1;
	}
	CloseClipboard ();
	if (!v)
		GlobalFree (hg);
	return v;
}

static HDC surface_dc, offscreen_dc;
static BITMAPINFO bi; // bitmap info
static LPVOID lpvBits = NULL; // pointer to bitmap bits array
static HBITMAP offscreen_bitmap;
static int screenshot_prepared;

void screenshot_free (void)
{
	if (surface_dc)
		releasehdc (surface_dc);
	surface_dc = NULL;
	if(offscreen_dc)
		DeleteDC(offscreen_dc);
	offscreen_dc = NULL;
	if(offscreen_bitmap)
		DeleteObject(offscreen_bitmap);
	offscreen_bitmap = NULL;
	if(lpvBits)
		free(lpvBits);
	lpvBits = NULL;
	screenshot_prepared = FALSE;
}

static int rgb_rb, rgb_gb, rgb_bb, rgb_rs, rgb_gs, rgb_bs;

int screenshot_prepare (void)
{
	int width, height;
	HGDIOBJ hgdiobj;
	int bits;

	screenshot_free ();

	regqueryint (NULL, _T("Screenshot_Original"), &screenshot_originalsize);

	if (screenshot_originalsize && !WIN32GFX_IsPicassoScreen ()) {
		int spitch, dpitch, x, y;
		uae_u8 *src, *dst;
		
		src = getfilterbuffer (&width, &height, &spitch, &bits);
		if (src == NULL || width == 0 || height == 0)
			goto donormal;
		ZeroMemory (&bi, sizeof(bi));
		bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = width;
		bi.bmiHeader.biHeight = height;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 24;
		bi.bmiHeader.biCompression = BI_RGB;
		dpitch = ((bi.bmiHeader.biWidth * bi.bmiHeader.biBitCount + 31) & ~31) / 8;
		bi.bmiHeader.biSizeImage = dpitch * bi.bmiHeader.biHeight;
		bi.bmiHeader.biXPelsPerMeter = 0;
		bi.bmiHeader.biYPelsPerMeter = 0;
		bi.bmiHeader.biClrUsed = 0;
		bi.bmiHeader.biClrImportant = 0;
		if (!(lpvBits = xmalloc (uae_u8, bi.bmiHeader.biSizeImage)))
			goto oops;
		dst = (uae_u8*)lpvBits + (height - 1) * dpitch;
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				int shift;
				uae_u32 v = 0;
				uae_u32 v2;

				if (bits == 16)
					v = ((uae_u16*)src)[x];
				else if (bits == 32)
					v = ((uae_u32*)src)[x];

				shift = 8 - rgb_bb;
				v2 = (v >> rgb_bs) & ((1 << rgb_bb) - 1);
				v2 <<= shift;
				if (rgb_bb < 8)
					v2 |= (v2 >> shift) & ((1 < shift) - 1);
				dst[x * 3 + 0] = v2;

				shift = 8 - rgb_gb;
				v2 = (v >> rgb_gs) & ((1 << rgb_gb) - 1);
				v2 <<= (8 - rgb_gb);
				if (rgb_gb < 8)
					v2 |= (v2 >> shift) & ((1 < shift) - 1);
				dst[x * 3 + 1] = v2;

				shift = 8 - rgb_rb;
				v2 = (v >> rgb_rs) & ((1 << rgb_rb) - 1);
				v2 <<= (8 - rgb_rb);
				if (rgb_rb < 8)
					v2 |= (v2 >> shift) & ((1 < shift) - 1);
				dst[x * 3 + 2] = v2;

			}
			src += spitch;
			dst -= dpitch;
		}

	} else {
donormal:
		width = WIN32GFX_GetWidth ();
		height = WIN32GFX_GetHeight ();

		surface_dc = gethdc ();
		if (surface_dc == NULL)
			goto oops;

		// need a HBITMAP to convert it to a DIB
		if ((offscreen_bitmap = CreateCompatibleBitmap (surface_dc, width, height)) == NULL)
			goto oops; // error

		// The bitmap is empty, so let's copy the contents of the surface to it.
		// For that we need to select it into a device context.
		if ((offscreen_dc = CreateCompatibleDC (surface_dc)) == NULL)
			goto oops; // error

		// select offscreen_bitmap into offscreen_dc
		hgdiobj = SelectObject (offscreen_dc, offscreen_bitmap);

		// now we can copy the contents of the surface to the offscreen bitmap
		BitBlt (offscreen_dc, 0, 0, width, height, surface_dc, 0, 0, SRCCOPY);

		// de-select offscreen_bitmap
		SelectObject (offscreen_dc, hgdiobj);

		ZeroMemory (&bi, sizeof(bi));
		bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = width;
		bi.bmiHeader.biHeight = height;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 24;
		bi.bmiHeader.biCompression = BI_RGB;
		bi.bmiHeader.biSizeImage = (((bi.bmiHeader.biWidth * bi.bmiHeader.biBitCount + 31) & ~31) / 8) * bi.bmiHeader.biHeight;
		bi.bmiHeader.biXPelsPerMeter = 0;
		bi.bmiHeader.biYPelsPerMeter = 0;
		bi.bmiHeader.biClrUsed = 0;
		bi.bmiHeader.biClrImportant = 0;

		// Reserve memory for bitmap bits
		if (!(lpvBits = xmalloc (uae_u8, bi.bmiHeader.biSizeImage)))
			goto oops; // out of memory

		// Have GetDIBits convert offscreen_bitmap to a DIB (device-independent bitmap):
		if (!GetDIBits (offscreen_dc, offscreen_bitmap, 0, bi.bmiHeader.biHeight, lpvBits, &bi, DIB_RGB_COLORS))
			goto oops; // GetDIBits FAILED

		releasehdc (surface_dc);
		surface_dc = NULL;
	}
	screenshot_prepared = TRUE;
	return 1;

oops:
	screenshot_free ();
	return 0;
}

void Screenshot_RGBinfo (int rb, int gb, int bb, int rs, int gs, int bs)
{
	rgb_rb = rb;
	rgb_gb = gb;
	rgb_bb = rb;
	rgb_rs = rs;
	rgb_gs = gs;
	rgb_bs = bs;
}

#if PNG_SCREENSHOTS > 0

static void _cdecl pngtest_blah (png_structp png_ptr, png_const_charp message)
{
	TCHAR *name = au ("unknown");
	if (png_ptr != NULL && png_ptr->error_ptr != NULL)
		name = au ((char*)png_ptr->error_ptr);
	write_log (_T("%s: libpng warning: %s\n"), name, message);
	xfree (name);
}

static int savepng (FILE *fp)
{
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep *row_pointers;
	int h = bi.bmiHeader.biHeight;
	int w = bi.bmiHeader.biWidth;
	int i;

	png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, pngtest_blah, pngtest_blah, pngtest_blah);
	if (!png_ptr)
		return 0;
	info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct (&png_ptr, NULL);
		return 0;
	}
	if (setjmp(png_jmpbuf (png_ptr))) {
		png_destroy_write_struct (&png_ptr, &info_ptr);
		return 0;
	}

	png_init_io (png_ptr, fp);
	png_set_filter (png_ptr, 0, PNG_FILTER_NONE);
	png_set_IHDR (png_ptr, info_ptr,
		w, h, 8, PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	row_pointers = xmalloc (png_bytep, h);
	for (i = 0; i < h; i++) {
		int j = h - i - 1;
		row_pointers[i] = (uae_u8*)lpvBits + j * (((w * 24 + 31) & ~31) / 8);
	}
	png_set_rows (png_ptr, info_ptr, row_pointers);
	png_write_png (png_ptr,info_ptr, PNG_TRANSFORM_BGR, NULL);
	png_destroy_write_struct (&png_ptr, &info_ptr);
	return 1;
}
#endif

static int savebmp (FILE *fp)
{
	BITMAPFILEHEADER bfh;
	// write the file header, bitmap information and pixel data
	bfh.bfType = 19778;
	bfh.bfSize = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + bi.bmiHeader.biSizeImage;
	bfh.bfReserved1 = 0;
	bfh.bfReserved2 = 0;
	bfh.bfOffBits = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER);
	if (fwrite (&bfh, 1, sizeof (BITMAPFILEHEADER), fp) < sizeof (BITMAPFILEHEADER))
		return 0; // failed to write bitmap file header
	if (fwrite (&bi, 1, sizeof (BITMAPINFOHEADER), fp) < sizeof (BITMAPINFOHEADER))
		return 0; // failed to write bitmap infomation header
	if (fwrite (lpvBits, 1, bi.bmiHeader.biSizeImage, fp) < bi.bmiHeader.biSizeImage)
		return 0; // failed to write the bitmap
	return 1;
}

/*
Captures the Amiga display (DirectDraw, D3D or OpenGL) surface and saves it to file as a 24bit bitmap.
*/
int screenshotf (const TCHAR *spath, int mode, int doprepare)
{
	static int recursive;
	FILE *fp = NULL;
	int allok = 0;

	HBITMAP offscreen_bitmap = NULL; // bitmap that is converted to a DIB
	HDC offscreen_dc = NULL; // offscreen DC that we can select offscreen bitmap into

	if(recursive)
		return 0;

	recursive++;

	if (!screenshot_prepared || doprepare) {
		if (!screenshot_prepare ())
			goto oops;
	}

	if (mode == 0) {
		toclipboard (&bi, lpvBits);
	} else {
		TCHAR filename[MAX_DPATH];
		TCHAR path[MAX_DPATH];
		TCHAR name[MAX_DPATH];
		TCHAR underline[] = _T("_");
		int number = 0;

		if (spath) {
			fp = _tfopen (spath, _T("wb"));
			if (fp) {
#if PNG_SCREENSHOTS > 0
				if (screenshotmode)
					allok = savepng (fp);
				else
#endif
					allok = savebmp (fp);
				fclose (fp);
				fp = NULL;
				goto oops;
			}
		}
		fetch_path (_T("ScreenshotPath"), path, sizeof (path) / sizeof (TCHAR));
		CreateDirectory (path, NULL);
		name[0] = 0;
		if (currprefs.floppyslots[0].dfxtype >= 0)
			_tcscpy (name, currprefs.floppyslots[0].df);
		else if (currprefs.cdslots[0].inuse)
			_tcscpy (name, currprefs.cdslots[0].name);
		if (!name[0])
			underline[0] = 0;
		namesplit (name);

		while(++number < 1000) // limit 999 iterations / screenshots
		{
			_stprintf (filename, _T("%s%s%s%03d.%s"), path, name, underline, number, screenshotmode ? _T("png") : _T("bmp"));
			if ((fp = _tfopen (filename, _T("rb"))) == NULL) // does file not exist?
			{
				int ok = 0;
				if ((fp = _tfopen (filename, _T("wb"))) == NULL)
					goto oops; // error
#if PNG_SCREENSHOTS > 0
				if (screenshotmode)
					ok = savepng (fp);
				else
#endif
					ok = savebmp (fp);
				fclose(fp);
				fp = NULL;
				if (!ok)
					goto oops;
				write_log (_T("Screenshot saved as \"%s\"\n"), filename);
				allok = 1;
				break;
			}
			fclose (fp);
			fp = NULL;
		}
	}

oops:
	if(fp)
		fclose (fp);

	if (doprepare)
		screenshot_free();

	recursive--;

	return allok;
}

void screenshot (int mode, int doprepare)
{
	screenshotf (NULL, mode, doprepare);
}
