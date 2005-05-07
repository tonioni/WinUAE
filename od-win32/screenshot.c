#include <windows.h>
#include <ddraw.h>

#include <stdio.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "custom.h"
#include "picasso96.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "opengl.h"

static void namesplit (char *s)
{
    int l;
    
    l = strlen (s) - 1;
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
	memmove (s, s + l, strlen (s + l) + 1);
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
        dib = GlobalLock (hg);
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

void screenshot_free(void)
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


int screenshot_prepare(void)
{
    unsigned int width, height;
    HGDIOBJ hgdiobj;

    screenshot_free();

    width = WIN32GFX_GetWidth();
    height = WIN32GFX_GetHeight();

    surface_dc = gethdc ();
    if (surface_dc == NULL)
	goto oops;

    // need a HBITMAP to convert it to a DIB
    if((offscreen_bitmap = CreateCompatibleBitmap(surface_dc, width, height)) == NULL)
    	goto oops; // error
	
    // The bitmap is empty, so let's copy the contents of the surface to it.
    // For that we need to select it into a device context.
    if((offscreen_dc = CreateCompatibleDC(surface_dc)) == NULL)
    	goto oops; // error

    // select offscreen_bitmap into offscreen_dc
    hgdiobj = SelectObject(offscreen_dc, offscreen_bitmap);

    // now we can copy the contents of the surface to the offscreen bitmap
    BitBlt(offscreen_dc, 0, 0, width, height, surface_dc, 0, 0, SRCCOPY);

    // de-select offscreen_bitmap
    SelectObject(offscreen_dc, hgdiobj);
	
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
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
    if(!(lpvBits = malloc(bi.bmiHeader.biSizeImage)))
	goto oops; // out of memory

    // Have GetDIBits convert offscreen_bitmap to a DIB (device-independent bitmap):
    if(!GetDIBits(offscreen_dc, offscreen_bitmap, 0, bi.bmiHeader.biHeight, lpvBits, &bi, DIB_RGB_COLORS))
	goto oops; // GetDIBits FAILED

    releasehdc (surface_dc);
    surface_dc = NULL;
    screenshot_prepared = TRUE;
    return 1;

oops:
    screenshot_free();
    return 0;
}


/*
Captures the Amiga display (DirectDraw, D3D or OpenGL) surface and saves it to file as a 24bit bitmap.
*/
void screenshot(int mode, int doprepare)
{
	static int recursive;
	FILE *fp = NULL;
	
	HBITMAP offscreen_bitmap = NULL; // bitmap that is converted to a DIB
	HDC offscreen_dc = NULL; // offscreen DC that we can select offscreen bitmap into

	if(recursive)
		return;
	
	recursive++;

	if (!screenshot_prepared || doprepare) {
	    if (!screenshot_prepare())
		goto oops;
	}

	if (mode == 0) {
	    toclipboard (&bi, lpvBits);
	} else {
		char filename[MAX_DPATH];
		char extension[] = "bmp";
		char path[MAX_DPATH];
		char name[MAX_DPATH];
		char underline[] = "_";
		int number = 0;
		
		fetch_path ("ScreenshotPath", path, sizeof (path));
		CreateDirectory (path, NULL);
		name[0] = 0;
		if (currprefs.dfxtype[0] >= 0)
		    strcpy (name, currprefs.df[0]);
		if (!name[0])
		    underline[0] = 0;
		namesplit (name);
		
		while(++number < 1000) // limit 999 iterations / screenshots
		{
			sprintf(filename, "%s%s%s%03d.%s", path, name, underline, number, extension);
			
			if((fp = fopen(filename, "r")) == NULL) // does file not exist?
			{
				BITMAPFILEHEADER bfh;
				
				if((fp = fopen(filename, "wb")) == NULL)
					goto oops; // error
				
				// write the file header, bitmap information and pixel data
				bfh.bfType = 19778;
				bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bi.bmiHeader.biSizeImage;
				bfh.bfReserved1 = 0;
				bfh.bfReserved2 = 0;
				bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
				
				if(fwrite(&bfh, 1, sizeof(BITMAPFILEHEADER), fp) < sizeof(BITMAPFILEHEADER))
					goto oops; // failed to write bitmap file header
				
				if(fwrite(&bi, 1, sizeof(BITMAPINFOHEADER), fp) < sizeof(BITMAPINFOHEADER))
					goto oops; // failed to write bitmap infomation header
				
				if(fwrite(lpvBits, 1, bi.bmiHeader.biSizeImage, fp) < bi.bmiHeader.biSizeImage)
					goto oops; // failed to write the bitmap
				
				fclose(fp);
				
				write_log("Screenshot saved as \"%s\"\n", filename);
				
				break;
			}
			
			fclose(fp);
			fp = NULL;
		}
	}
	
oops:
	if(fp)
		fclose(fp);
	
	if (doprepare)
	    screenshot_free();

	recursive--;

	return;
}

