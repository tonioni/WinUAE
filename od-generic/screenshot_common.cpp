/*
* UAE - The Un*x Amiga Emulator
*
* Screenshot cropping and file naming shared by the host screenshot code
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "drawing.h"

#include "od-generic/screenshot_common.h"

void screenshot_get_crop(struct screenshot_crop *sc, int width, int height, int dx, int dy, bool standard, bool clip)
{
	int screenshot_width = width;
	int screenshot_height = height;
	int screenshot_xmult = 1;
	int screenshot_ymult = 1;
	int xoffset = dx, yoffset = dy;

	if (!standard) {
		screenshot_xmult = currprefs.screenshot_xmult + 1;
		screenshot_ymult = currprefs.screenshot_ymult + 1;
		if (currprefs.screenshot_width > 0) {
			screenshot_width = currprefs.screenshot_width;
		}
		if (currprefs.screenshot_height > 0) {
			screenshot_height = currprefs.screenshot_height;
		}
	}

	if (clip) {
		int cw, ch, cx, cy, crealh = 0, hres, vres;
		if (get_custom_limits(&cw, &ch, &cx, &cy, &crealh, &hres, &vres)) {
			int maxw = currprefs.screenshot_max_width << currprefs.gfx_resolution;
			int maxh = currprefs.screenshot_max_height << currprefs.gfx_vresolution;
			int minw = currprefs.screenshot_min_width << currprefs.gfx_resolution;
			int minh = currprefs.screenshot_min_height << currprefs.gfx_vresolution;
			if (minw > (maxhpos_display + 1) << currprefs.gfx_resolution) {
				minw = (maxhpos_display + 1) << currprefs.gfx_resolution;
			}
			if (minh > (maxvsize_display + 1) << currprefs.gfx_vresolution) {
				minh = (maxvsize_display + 1) << currprefs.gfx_vresolution;
			}
			if (maxw < minw) {
				maxw = minw;
			}
			if (maxh < minh) {
				maxh = minh;
			}
			int screenshot_xoffset = cx;
			int screenshot_yoffset = cy;
			screenshot_width = cw;
			screenshot_height = ch;
			if (screenshot_width < minw && minw > 0) {
				screenshot_xoffset -= (minw - screenshot_width) / 2;
				screenshot_width = minw;
			}
			if (screenshot_height < minh && minh > 0) {
				screenshot_yoffset -= (minh - screenshot_height) / 2;
				screenshot_height = minh;
			}
			if (screenshot_width > maxw && maxw > 0) {
				screenshot_xoffset += (screenshot_width - maxw) / 2;
				screenshot_width = maxw;
			}
			if (screenshot_height > maxh && maxh > 0) {
				screenshot_yoffset += (screenshot_height - maxh) / 2;
				screenshot_height = maxh;
			}
			xoffset = screenshot_xoffset < 0 ? (screenshot_width - width) / 2 : -screenshot_xoffset;
			yoffset = screenshot_yoffset < 0 ? (screenshot_height - height) / 2 : -screenshot_yoffset;
		}
	}

	if (screenshot_xmult < 1) {
		screenshot_xmult = 1;
	}
	if (screenshot_ymult < 1) {
		screenshot_ymult = 1;
	}

	int maxw_output = standard ? 0 : currprefs.screenshot_output_width;
	int maxh_output = standard ? 0 : currprefs.screenshot_output_height;
	while (maxw_output > screenshot_width * screenshot_xmult && screenshot_xmult < 8) {
		screenshot_xmult++;
	}
	while (maxh_output > screenshot_height * screenshot_ymult && screenshot_ymult < 8) {
		screenshot_ymult++;
	}

	sc->width = screenshot_width;
	sc->height = screenshot_height;
	sc->xoffset = xoffset;
	sc->yoffset = yoffset;
	sc->xmult = screenshot_xmult;
	sc->ymult = screenshot_ymult;
}

static void namesplit(TCHAR *s)
{
	int l;

	l = uaetcslen(s) - 1;
	while (l >= 0) {
		if (s[l] == '.') {
			s[l] = 0;
		}
		if (s[l] == '\\' || s[l] == '/' || s[l] == ':' || s[l] == '?') {
			l++;
			break;
		}
		l--;
	}
	if (l > 0) {
		memmove(s, s + l, (uaetcslen(s + l) + 1) * sizeof(TCHAR));
	}
}

void screenshot_get_name(TCHAR *name)
{
	name[0] = 0;
	if (currprefs.floppyslots[0].dfxtype >= 0 && currprefs.floppyslots[0].df[0]) {
		_tcscpy(name, currprefs.floppyslots[0].df);
	} else if (currprefs.cdslots[0].inuse) {
		_tcscpy(name, currprefs.cdslots[0].name);
	}
	namesplit(name);
}

bool screenshot_get_filename(TCHAR *out, int size, const TCHAR *path, const TCHAR *name, int number, bool multi, const TCHAR *ext)
{
	// limit 999 screenshots, or 99999 in continuous mode
	if (number >= (multi ? 100000 : 1000)) {
		return false;
	}
	_sntprintf(out, size, multi ? _T("%s%s%s%05d.%s") : _T("%s%s%s%03d.%s"), path, name, name[0] ? _T("_") : _T(""), number, ext);
	out[size - 1] = 0;
	return true;
}
