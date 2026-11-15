#include "sysconfig.h"
#include "sysdeps.h"
#include "statusbar.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main()
{
    statusbar_state state = {};
    state.gui.fps = 500;
    state.gui.lines = 312;
    state.gui.sndbuf_avail = true;
    statusbar_item item;
    CHECK(statusbar_format(state, LED_FPS, 1, &item));
    CHECK(!_tcscmp(item.text, _T("FPS: 50.0")));
    state.paused = true;
    CHECK(statusbar_format(state, LED_FPS, 1, &item));
    CHECK(!_tcscmp(item.text, _T("PAUSED")));
    state.paused = false;
    state.rtg = true;
    state.refresh_rate = 60;
    CHECK(statusbar_format(state, LED_FPS, 1, &item));
    CHECK(!_tcscmp(item.text, _T("60.0 [50.0]")));
    state.gui.drives[0].drive_track = 42;
    state.gui.drives[0].drive_writing = true;
    _tcscpy(state.gui.drives[0].df, _T("/tmp/Workbench.adf"));
    CHECK(statusbar_format(state, LED_DF0, 1, &item));
    CHECK(!_tcscmp(item.text, _T("42")));
    CHECK(item.flags & STATUSBAR_WRITING);
    CHECK(!_tcscmp(item.tooltip, _T("Workbench.adf [CRC=00000000]")));
    CHECK(statusbar_action_for(LED_HD, false) == STATUSBAR_NONE);
    CHECK(statusbar_action_for(LED_CAPS, false) == STATUSBAR_NONE);
    CHECK(statusbar_action_for(LED_POWER, false) == STATUSBAR_SETTINGS);
    CHECK(statusbar_action_for(LED_POWER, true) == STATUSBAR_RESET);
    CHECK(statusbar_action_for(LED_CD, false) == STATUSBAR_CD);
    CHECK(statusbar_action_for(LED_CD, true) == STATUSBAR_EJECT_CD);
    CHECK(statusbar_action_for(LED_FPS, false) == STATUSBAR_RESUME);
    for (int led = LED_DF0; led <= LED_DF3; ++led) {
        CHECK(statusbar_led(statusbar_position(led)) == led);
        CHECK(statusbar_action_for(led, false) == STATUSBAR_DISK);
        CHECK(statusbar_action_for(led, true) == STATUSBAR_EJECT);
    }
    // A resized 1004-point window still has a 22-point bar, at either DPI.
    for (int scale = 1; scale <= 2; ++scale) {
        statusbar_rect bar = {0, float(982 * scale), float(800 * scale), float(22 * scale)};
        float x, y;
        CHECK(!statusbar_point(600, 981.5f, 800, 1004, 800*scale, 1004*scale, bar, &x, &y));
        CHECK(statusbar_point(600, 982, 800, 1004, 800*scale, 1004*scale, bar, &x, &y));
        CHECK(x == 600*scale && y == 0);
        CHECK(!statusbar_point(800, 990, 800, 1004, 800*scale, 1004*scale, bar, &x, &y));
        CHECK(!statusbar_point(10, 1004, 800, 1004, 800*scale, 1004*scale, bar, &x, &y));
    }
    statusbar_rect cells[] = {{0,0,24,22},{24,0,24,22}};
    CHECK(statusbar_hit(cells, 2, 23.99f, 10) == 0);
    CHECK(statusbar_hit(cells, 2, 24, 10) == 1);
    CHECK(statusbar_hit(cells, 2, 48, 10) == -1);
    puts("statusbar model and coordinate checks passed");
    return 0;
}
