#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "gfxfilter.h"
#include "scaler.h"
#include "display_modes.h"
uae_prefs currprefs, changed_prefs;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
	if (!(x)) {                                                                                \
	    fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
	    return 1;                                                                              \
	}                                                                                          \
    } while (0)
int main()
{
    PicassoResolution modes[4] = {};
    CHECK(host_display_mode_add(modes, 4, 800, 600, 75, 0, false));
    CHECK(host_display_mode_add(modes, 4, 800, 600, 60, 1, true));
    CHECK(host_display_mode_add(modes, 4, 800, 600, 75, 0, false));
    CHECK(modes[0].refresh[1] == 60); // Revisiting a rate must not truncate later rates.
    CHECK(host_display_mode_add(modes, 4, 640, 480, 60, 0, false));
    CHECK(host_display_mode_add(modes, 4, 1024, 768, 60, 0, false));
    CHECK(!host_display_mode_add(modes, 4, 1280, 720, 60, 0, false));
    CHECK(host_display_modes_sort(modes, 4, true) == 3);
    CHECK(modes[0].res.width == 640 && modes[1].res.width == 800);
    CHECK(modes[1].refresh[0] == 60 && modes[1].refresh[1] == 75);
    CHECK(modes[1].refreshtype[0] == (REFRESH_RATE_RAW | REFRESH_RATE_LACE));
    CHECK(modes[2].residx == 2 && !modes[3].inuse);
    PicassoResolution rates[2] = {};
    for (int i = 1; i < MAX_REFRESH_RATES; i++)
	CHECK(host_display_mode_add(rates, 2, 320, 200, i, 0, false));
    CHECK(!host_display_mode_add(rates, 2, 320, 200, MAX_REFRESH_RATES, 0, false));
    CHECK(!rates[1].inuse && rates[0].refresh[MAX_REFRESH_RATES - 1] == 0);
    CHECK(!host_display_mode_add(rates, 2, 0, 200, 60, 0, false));
    currprefs.host_rtgscaleaspectratio = -1;
    displayscale ds = {};
    host_rtg_offset offset;
    host_rtg_filterdata(&ds, 1920, 1080, 640, 480, 1, 1, 2, true, &offset);
    CHECK(ds.outwidth == 1920 && ds.outheight == 1080 && offset.x == -640 && offset.y == -300);
    ds = {};
    host_rtg_filterdata(&ds, 1920, 1080, 640, 480, 1, 1, 3, true, &offset);
    CHECK(ds.outwidth == 960 && ds.outheight == 540 && ds.mode == 1);
    CHECK(offset.x == -160 && offset.y == -30);
    ds = {};
    host_rtg_filterdata(&ds, 1920, 1080, 640, 480, 1, 1, 1, true, &offset);
    CHECK(ds.outwidth >= 852 && ds.outwidth <= 854 && ds.outheight == 480);
    CHECK(offset.x < 0 && offset.y == 0);
    currprefs.host_rtgscaleaspectratio = 0;
    ds = {};
    host_rtg_filterdata(&ds, 1920, 1080, 640, 480, 1, 1, 1, true, &offset);
    CHECK(ds.outwidth == 640 && ds.outheight == 480);
    for (int w : {320, 640, 1920})
	for (int h : {200, 480, 1080})
	    for (int mode = 1; mode <= 3; mode++) {
		ds = {};
		host_rtg_filterdata(&ds, w, h, 640, 480, 1, 1, mode, true, &offset);
		CHECK(ds.outwidth > 0 && ds.outheight > 0);
	    }
    return 0;
}
