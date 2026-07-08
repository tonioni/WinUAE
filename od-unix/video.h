#ifndef WINUAE_OD_UNIX_VIDEO_H
#define WINUAE_OD_UNIX_VIDEO_H

#include "sysdeps.h"

struct unix_video_frame
{
    const uae_u8 *pixels;
    int width;
    int height;
    int rowbytes;
    int pixbytes;
    int filter_index;
    int monitor_id;
    int backbuffers;
};

struct unix_video_display_mode
{
    int width;
    int height;
    int refresh_rate;
};

enum unix_video_window_mode
{
    UNIX_VIDEO_WINDOWED = 0,
    UNIX_VIDEO_FULLSCREEN = 1,
    UNIX_VIDEO_FULLWINDOW = 2
};

bool unix_video_setup(void);
bool unix_video_init(int width, int height, int pixbytes);
void unix_video_shutdown(void);
int unix_video_poll(bool *quit_requested);
int unix_video_poll_window_events(bool *quit_requested);
void unix_video_present(const struct unix_video_frame *frame);
void unix_video_set_title(const TCHAR *title);
bool unix_video_set_window_mode(enum unix_video_window_mode mode, int display_index, int width, int height, int refresh_rate);
enum unix_video_window_mode unix_video_get_window_mode(void);
float unix_video_get_display_refresh_rate(int display_index);
int unix_video_get_display_modes(int display_index, struct unix_video_display_mode *modes, int max_modes);
void unix_video_get_desktop(int *dw, int *dh, int *x, int *y, int *w, int *h);
void unix_video_set_mouse_grab(bool grab);
bool unix_video_get_mouse_grab(void);
void unix_video_toggle_mouse_grab(void);
/* Named shader/scaler selected through the unix.gfx_shader config option. */
const TCHAR *unix_gfx_shader_option(void);

#endif /* WINUAE_OD_UNIX_VIDEO_H */
