#include "gui.h"
#include "threaddep/thread.h"
#include <stdio.h>

int gui_init (void) {
    return 0;
}

int gui_update (void) {
    return 0;
}

void gui_exit (void) {

}
void gui_led (int, int, int) {

}

void gui_handle_events (void) {

}

void gui_filename (int, const TCHAR *) {

}

void gui_fps (int fps, int idle, int color) {

}

void gui_changesettings (void) {

}

void gui_lock (void) {

}

void gui_unlock (void) {

}

void gui_flicker_led (int, int, int) {

}

void gui_disk_image_change (int, const TCHAR *, bool writeprotected) {

}

void gui_display (int shortcut) {

}

void gui_gameport_button_change (int port, int button, int onoff) {

}

void gui_gameport_axis_change (int port, int axis, int state, int max) {

}

void notify_user (int msg) {
    printf("notify_user: %d\n", msg);
}

