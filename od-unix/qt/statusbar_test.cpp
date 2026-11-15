#include <QApplication>
#include <QImage>
#include <QPalette>
#include <vector>
#include <cstdio>
#include "sysconfig.h"
#include "sysdeps.h"
#include "od-unix/qt/statusbar.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    statusbar_state state = {};
    state.gui.fps = 500;
    state.gui.lines = 312;
    state.gui.powerled = true;
    state.gui.sndbuf_avail = true;
    state.gui.hd = 1;
    state.gui.cd = 0;
    state.gui.net = state.gui.md = -1;
    state.gui.drives[0].drive_motor = true;
    state.gui.drives[0].drive_track = 42;
    _tcscpy(state.gui.drives[0].df, _T("/tmp/Workbench.adf"));
    const int height = winUaeQtStatusbarHeight();
    for (int scale = 1; scale <= 2; ++scale) {
        for (int width : {480, 800, 1200}) {
            std::vector<unsigned int> pixels(width * scale * height * scale);
            statusbar_rect rects[STATUSBAR_ITEMS];
            statusbar_item items[STATUSBAR_ITEMS];
            winUaeQtPaintStatusbar(pixels.data(), width*scale, height*scale, scale,
                state, _T("Workbench.adf"), rects, items);
            for (int i = 0; i < STATUSBAR_ITEMS; ++i) {
                auto &r = rects[i];
                float cx = r.x + r.w / 2;
                if (cx < 0) continue; // narrow windows clip, never compress fields
                CHECK(statusbar_hit(rects, STATUSBAR_ITEMS, cx, r.h / 2) == i);
                float x, y;
                statusbar_rect bar = {0, float(600*scale), float(width*scale), float(height*scale)};
                CHECK(statusbar_point(cx/scale, 600 + height/2.0f, width, 600+height,
                    width*scale, (600+height)*scale, bar, &x, &y));
                CHECK(statusbar_hit(rects, STATUSBAR_ITEMS, x, y) == i);
                if (statusbar_led(i) == LED_HD || statusbar_led(i) == LED_CAPS)
                    CHECK(statusbar_action_for(statusbar_led(i), false) == STATUSBAR_NONE);
            }
            // Device visibility and OSD settings cannot move the window controls.
            statusbar_rect previous[STATUSBAR_ITEMS];
            memcpy(previous, rects, sizeof rects);
            state.gui.hd = state.gui.cd = -1;
            winUaeQtPaintStatusbar(pixels.data(), width*scale, height*scale, scale,
                state, nullptr, rects, items);
            CHECK(!memcmp(previous, rects, sizeof rects));
            state.gui.hd = 1; state.gui.cd = 0;
            if (argc > 1 && width == 1200) {
                winUaeQtPaintStatusbar(pixels.data(), width*scale, height*scale, scale,
                    state, _T("Workbench.adf"), rects, items);
                QImage image(reinterpret_cast<uchar *>(pixels.data()), width*scale,
                    height*scale, width*scale*4, QImage::Format_ARGB32);
                CHECK(image.save(QString::fromLocal8Bit(argv[1]) + QString::number(scale) + ".png"));
            }
        }
    }
    puts("Qt statusbar geometry and rendering checks passed");
    return 0;
}
