#include "sysconfig.h"
#include "sysdeps.h"

#include "video.h"

bool unix_video_setup(void)
{
    return true;
}

bool unix_video_init(int, int, int)
{
    return false;
}

void unix_video_shutdown(void)
{
}

int unix_video_poll(bool *quit_requested)
{
    if (quit_requested) {
        *quit_requested = false;
    }
    return 0;
}

int unix_video_poll_window_events(bool *quit_requested)
{
    if (quit_requested) {
        *quit_requested = false;
    }
    return 0;
}

void unix_video_present(const struct unix_video_frame *)
{
}

void unix_video_set_title(const TCHAR *)
{
}

bool unix_video_set_window_mode(enum unix_video_window_mode, int, int, int, int)
{
    return false;
}

enum unix_video_window_mode unix_video_get_window_mode(void)
{
    return UNIX_VIDEO_WINDOWED;
}

float unix_video_get_display_refresh_rate(int)
{
    return 0.0f;
}

int unix_video_get_display_modes(int, struct unix_video_display_mode *, int)
{
    return 0;
}

void unix_video_set_mouse_grab(bool)
{
}

bool unix_video_get_mouse_grab(void)
{
    return false;
}

void unix_video_toggle_mouse_grab(void)
{
}

void unix_video_get_desktop(int *dw, int *dh, int *x, int *y, int *w, int *h)
{
    if (dw) {
        *dw = 640;
    }
    if (dh) {
        *dh = 480;
    }
    if (x) {
        *x = 0;
    }
    if (y) {
        *y = 0;
    }
    if (w) {
        *w = 640;
    }
    if (h) {
        *h = 480;
    }
}

int target_get_display(const TCHAR *)
{
    return -1;
}

const TCHAR *target_get_display_name(int, bool)
{
    return NULL;
}
