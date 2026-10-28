#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "drawing.h"

#include "od-generic/screenshot_common.h"

#include <stdio.h>

struct uae_prefs currprefs;
int maxhpos_display, maxvsize_display;

int uaetcslen(const TCHAR *s)
{
	return (int)_tcslen(s);
}

static bool limits_valid;
static int limit_w, limit_h, limit_x, limit_y;

int get_custom_limits(int *pw, int *ph, int *pdx, int *pdy, int *prealh, int *hres, int *vres)
{
	*pw = limit_w;
	*ph = limit_h;
	*pdx = limit_x;
	*pdy = limit_y;
	*prealh = limit_h;
	*hres = 0;
	*vres = 0;
	return limits_valid ? 1 : 0;
}

static bool check_crop(const char *label, const struct screenshot_crop &sc,
	int width, int height, int xoffset, int yoffset, int xmult, int ymult)
{
	if (sc.width == width && sc.height == height && sc.xoffset == xoffset
		&& sc.yoffset == yoffset && sc.xmult == xmult && sc.ymult == ymult) {
		return true;
	}
	fprintf(stderr, "%s: expected %dx%d off %d,%d mult %dx%d, got %dx%d off %d,%d mult %dx%d\n",
		label, width, height, xoffset, yoffset, xmult, ymult,
		sc.width, sc.height, sc.xoffset, sc.yoffset, sc.xmult, sc.ymult);
	return false;
}

static bool check_name(const char *label, const TCHAR *got, const TCHAR *expected)
{
	if (!_tcscmp(got, expected)) {
		return true;
	}
	fprintf(stderr, "%s: expected '%s', got '%s'\n", label, expected, got);
	return false;
}

int main()
{
	bool ok = true;
	struct screenshot_crop sc;

	maxhpos_display = 400;
	maxvsize_display = 313;
	currprefs.gfx_resolution = 1;
	currprefs.gfx_vresolution = 1;

	// No settings: the whole source at 1x.
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, false);
	ok = check_crop("plain", sc, 720, 568, 0, 0, 1, 1) && ok;

	// The visible-area offset is kept when not clipping.
	screenshot_get_crop(&sc, 720, 568, 3, 5, false, false);
	ok = check_crop("offset", sc, 720, 568, 3, 5, 1, 1) && ok;

	// Size and scaling settings, ignored in standard mode.
	currprefs.screenshot_width = 640;
	currprefs.screenshot_height = 512;
	currprefs.screenshot_xmult = 1;
	currprefs.screenshot_ymult = 2;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, false);
	ok = check_crop("settings", sc, 640, 512, 0, 0, 2, 3) && ok;
	screenshot_get_crop(&sc, 720, 568, 0, 0, true, false);
	ok = check_crop("standard", sc, 720, 568, 0, 0, 1, 1) && ok;
	currprefs.screenshot_width = 0;
	currprefs.screenshot_height = 0;
	currprefs.screenshot_xmult = 0;
	currprefs.screenshot_ymult = 0;

	// Output size limit raises the multiplier, up to 8.
	currprefs.screenshot_output_width = 1500;
	currprefs.screenshot_output_height = 100000;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, false);
	ok = check_crop("output limit", sc, 720, 568, 0, 0, 3, 8) && ok;
	currprefs.screenshot_output_width = 0;
	currprefs.screenshot_output_height = 0;

	// Clip without valid limits keeps the source size.
	limits_valid = false;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, true);
	ok = check_crop("clip, no limits", sc, 720, 568, 0, 0, 1, 1) && ok;

	// Clip to the displayed area.
	limits_valid = true;
	limit_w = 640;
	limit_h = 512;
	limit_x = 40;
	limit_y = 28;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, true);
	ok = check_crop("clip", sc, 640, 512, -40, -28, 1, 1) && ok;

	// Minimum size grows the area around its centre.
	currprefs.screenshot_min_width = 336;
	currprefs.screenshot_min_height = 270;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, true);
	ok = check_crop("clip min", sc, 672, 540, -24, -14, 1, 1) && ok;
	currprefs.screenshot_min_width = 0;
	currprefs.screenshot_min_height = 0;

	// Maximum size shrinks it around its centre.
	currprefs.screenshot_max_width = 300;
	currprefs.screenshot_max_height = 200;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, true);
	ok = check_crop("clip max", sc, 600, 400, -60, -84, 1, 1) && ok;
	currprefs.screenshot_max_width = 0;
	currprefs.screenshot_max_height = 0;

	// Minimum is limited to the display size; a negative offset centres.
	currprefs.screenshot_min_width = 1000;
	limit_x = 10;
	screenshot_get_crop(&sc, 720, 568, 0, 0, false, true);
	ok = check_crop("clip min limit", sc, 802, 512, 41, -28, 1, 1) && ok;
	currprefs.screenshot_min_width = 0;

	TCHAR name[MAX_DPATH];
	TCHAR filename[MAX_DPATH];

	screenshot_get_name(name);
	ok = check_name("no disk", name, _T("")) && ok;
	ok = screenshot_get_filename(filename, MAX_DPATH, _T("/shots/"), name, 1, false, _T("png")) && ok;
	ok = check_name("no disk file", filename, _T("/shots/001.png")) && ok;

	currprefs.floppyslots[0].dfxtype = 0;
	_tcscpy(currprefs.floppyslots[0].df, _T("/games/Turrican II.v2.adf"));
	screenshot_get_name(name);
	ok = check_name("disk", name, _T("Turrican II")) && ok;
	ok = screenshot_get_filename(filename, MAX_DPATH, _T("/shots/"), name, 12, false, _T("png")) && ok;
	ok = check_name("disk file", filename, _T("/shots/Turrican II_012.png")) && ok;
	ok = screenshot_get_filename(filename, MAX_DPATH, _T("/shots/001/"), name, 7, true, _T("bmp")) && ok;
	ok = check_name("multi file", filename, _T("/shots/001/Turrican II_00007.bmp")) && ok;

	if (screenshot_get_filename(filename, MAX_DPATH, _T(""), name, 1000, false, _T("png"))) {
		fprintf(stderr, "number 1000 accepted\n");
		ok = false;
	}
	if (!screenshot_get_filename(filename, MAX_DPATH, _T(""), name, 99999, true, _T("png"))) {
		fprintf(stderr, "multi number 99999 rejected\n");
		ok = false;
	}

	currprefs.floppyslots[0].dfxtype = -1;
	currprefs.cdslots[0].inuse = true;
	_tcscpy(currprefs.cdslots[0].name, _T("C:\\cd\\demo.cue"));
	screenshot_get_name(name);
	ok = check_name("cd", name, _T("demo")) && ok;

	return ok ? 0 : 1;
}
