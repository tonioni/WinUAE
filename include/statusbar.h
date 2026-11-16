#ifndef UAE_STATUSBAR_H
#define UAE_STATUSBAR_H

#include "gui.h"

// Window status bar, separate from the bitmap on-screen display.
#define STATUSBAR_ITEMS 13
#define STATUSBAR_TEXT_SIZE 160
#define STATUSBAR_TOOLTIP_SIZE 300

enum statusbar_flags {
    STATUSBAR_CENTER = 1, STATUSBAR_ON = 2, STATUSBAR_WRITING = 4,
    STATUSBAR_PLAYING = 8, STATUSBAR_ACTIVE2 = 16, STATUSBAR_PROTECTED = 32
};
struct statusbar_state {
    gui_info gui;
    bool paused, sound_paused, recording, rtg, ppc, ppc_active;
    int x86; // 0: absent, 1: active, 2: stopped
    float refresh_rate;
};
struct statusbar_item {
    TCHAR text[STATUSBAR_TEXT_SIZE];
    TCHAR tooltip[STATUSBAR_TOOLTIP_SIZE];
    int flags;
};
struct statusbar_rect { float x, y, w, h; };
enum statusbar_action {
    STATUSBAR_NONE, STATUSBAR_DISK, STATUSBAR_EJECT, STATUSBAR_SETTINGS,
    STATUSBAR_RESET, STATUSBAR_RESUME, STATUSBAR_CD, STATUSBAR_EJECT_CD
};
int statusbar_led(int position);
int statusbar_position(int led);
int statusbar_width(int position, bool ppc, bool x86);
bool statusbar_format(const statusbar_state &state, int led, int on, statusbar_item *item);
statusbar_action statusbar_action_for(int led, bool right_button);
int statusbar_hit(const statusbar_rect *rects, int count, float x, float y);
bool statusbar_point(float window_x, float window_y, int window_w, int window_h,
    int output_w, int output_h, const statusbar_rect &bar, float *x, float *y);
void statusbar_get_state(statusbar_state *state);
int statusbar_led_state(const statusbar_state &state, int led);

#endif
