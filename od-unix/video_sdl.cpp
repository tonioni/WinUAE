#include "sysconfig.h"
#include "sysdeps.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "statusline.h"
#include "traps.h"
#include "clipboard.h"
#include "disk.h"
#include "gui.h"
#include "input.h"
#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "video.h"

extern int pause_emulation;
extern void pausemode(int mode);

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static std::vector<SDL_Texture *> s_textures;
static SDL_Texture *s_status_texture;
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
static SDL_GLContext s_gl_context;
static std::vector<GLuint> s_gl_textures;
static GLuint s_gl_texture;
static GLuint s_gl_status_texture;
static GLuint s_gl_program;
static bool s_gl_active;
static bool s_gl_failed;
static bool s_gl_functions_loaded;
static int s_gl_uniform_texture = -1;
static int s_gl_uniform_source_size = -1;
static int s_gl_uniform_adjust = -1;
static int s_gl_uniform_scanline = -1;
static int s_gl_uniform_blur_noise = -1;
static int s_gl_uniform_frame = -1;
static int s_gl_uniform_scaler = -1;
static unsigned int s_gl_frame_counter;
#endif
static bool s_setup_done;
static bool s_available;
static std::atomic<bool> s_event_thread_valid;
static std::atomic<bool> s_wrong_event_thread_logged;
static std::atomic<bool> s_queued_present_logged;
static uae_thread_id s_event_thread;
static bool s_mouse_grabbed;
static enum unix_video_window_mode s_requested_window_mode = UNIX_VIDEO_WINDOWED;
static enum unix_video_window_mode s_active_window_mode = UNIX_VIDEO_WINDOWED;
static int s_requested_display_index;
static int s_active_display_index = -1;
static int s_requested_fullscreen_width;
static int s_requested_fullscreen_height;
static int s_requested_fullscreen_refresh;
static int s_active_fullscreen_width = -1;
static int s_active_fullscreen_height = -1;
static int s_active_fullscreen_refresh = -1;
static int s_auto_window_width;
static int s_auto_window_height;
static int s_texture_width;
static int s_texture_height;
static int s_texture_pixbytes;
static int s_texture_backbuffers;
static int s_texture_index;
static int s_status_width;
static int s_status_height;
static std::vector<uae_u32> s_status_pixels;
static uae_u32 s_status_rc[256];
static uae_u32 s_status_gc[256];
static uae_u32 s_status_bc[256];
static bool s_status_colors_ready;
static Uint8 s_status_click_button;
static SDL_MouseButtonFlags s_suppressed_mouse_buttons;

struct unix_pending_video_frame {
    std::vector<uae_u8> pixels;
    int width;
    int height;
    int rowbytes;
    int pixbytes;
    int filter_index;
    int monitor_id;
    int backbuffers;
    bool valid;
};

struct unix_video_layout {
    float pixel_scale_x;
    float pixel_scale_y;
    SDL_FRect frame_dst;
    SDL_FRect status_dst;
    SDL_Rect frame_clip;
};

static std::mutex s_pending_frame_mutex;
static unix_pending_video_frame s_pending_frame;

static constexpr int UnixStatusScale = 2;
static TCHAR s_display_name[MAX_DPATH];

static void unix_video_present_on_event_thread(const struct unix_video_frame *frame);

#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

typedef GLuint(APIENTRYP UnixGlCreateShaderProc)(GLenum);
typedef void(APIENTRYP UnixGlShaderSourceProc)(GLuint, GLsizei, const GLchar **, const GLint *);
typedef void(APIENTRYP UnixGlCompileShaderProc)(GLuint);
typedef void(APIENTRYP UnixGlGetShaderivProc)(GLuint, GLenum, GLint *);
typedef void(APIENTRYP UnixGlGetShaderInfoLogProc)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint(APIENTRYP UnixGlCreateProgramProc)(void);
typedef void(APIENTRYP UnixGlAttachShaderProc)(GLuint, GLuint);
typedef void(APIENTRYP UnixGlLinkProgramProc)(GLuint);
typedef void(APIENTRYP UnixGlGetProgramivProc)(GLuint, GLenum, GLint *);
typedef void(APIENTRYP UnixGlGetProgramInfoLogProc)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(APIENTRYP UnixGlDeleteShaderProc)(GLuint);
typedef void(APIENTRYP UnixGlDeleteProgramProc)(GLuint);
typedef void(APIENTRYP UnixGlUseProgramProc)(GLuint);
typedef GLint(APIENTRYP UnixGlGetUniformLocationProc)(GLuint, const GLchar *);
typedef void(APIENTRYP UnixGlUniform1iProc)(GLint, GLint);
typedef void(APIENTRYP UnixGlUniform1fProc)(GLint, GLfloat);
typedef void(APIENTRYP UnixGlUniform2fProc)(GLint, GLfloat, GLfloat);
typedef void(APIENTRYP UnixGlUniform4fProc)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);

static UnixGlCreateShaderProc p_glCreateShader;
static UnixGlShaderSourceProc p_glShaderSource;
static UnixGlCompileShaderProc p_glCompileShader;
static UnixGlGetShaderivProc p_glGetShaderiv;
static UnixGlGetShaderInfoLogProc p_glGetShaderInfoLog;
static UnixGlCreateProgramProc p_glCreateProgram;
static UnixGlAttachShaderProc p_glAttachShader;
static UnixGlLinkProgramProc p_glLinkProgram;
static UnixGlGetProgramivProc p_glGetProgramiv;
static UnixGlGetProgramInfoLogProc p_glGetProgramInfoLog;
static UnixGlDeleteShaderProc p_glDeleteShader;
static UnixGlDeleteProgramProc p_glDeleteProgram;
static UnixGlUseProgramProc p_glUseProgram;
static UnixGlGetUniformLocationProc p_glGetUniformLocation;
static UnixGlUniform1iProc p_glUniform1i;
static UnixGlUniform1fProc p_glUniform1f;
static UnixGlUniform2fProc p_glUniform2f;
static UnixGlUniform4fProc p_glUniform4f;

static SDL_FunctionPointer unix_gl_get_proc(const char *name)
{
    SDL_FunctionPointer proc = SDL_GL_GetProcAddress(name);
    if (!proc) {
        write_log(_T("OpenGL shader pipeline: missing %s\n"), name);
    }
    return proc;
}
#endif

static int clamp_window_dimension(int value, int fallback, int maxvalue)
{
    if (value <= 0) {
        value = fallback;
    }
    if (value > maxvalue) {
        value = maxvalue;
    }
    return value;
}

static SDL_PixelFormat texture_format_for_pixbytes(int pixbytes)
{
    if (pixbytes == 2) {
        return SDL_PIXELFORMAT_RGB565;
    }
    return SDL_PIXELFORMAT_ARGB8888;
}

static int clamp_backbuffer_count(int backbuffers)
{
    if (backbuffers < 1) {
        return 1;
    }
    if (backbuffers > 3) {
        return 3;
    }
    return backbuffers;
}

static bool unix_video_on_event_thread(void)
{
    return !s_event_thread_valid.load() ||
        pthread_equal(uae_thread_get_id(), s_event_thread);
}

static void queue_video_frame_for_event_thread(const struct unix_video_frame *frame)
{
    if (!frame || !frame->pixels || frame->width <= 0 || frame->height <= 0 ||
        frame->rowbytes <= 0 || frame->pixbytes <= 0) {
        return;
    }

    if (!s_queued_present_logged.exchange(true)) {
        write_log(_T("SDL3: queueing video present from non-video thread\n"));
    }

    const int rowbytes = frame->width * frame->pixbytes;
    std::vector<uae_u8> pixels((size_t)rowbytes * (size_t)frame->height);
    for (int y = 0; y < frame->height; y++) {
        memcpy(pixels.data() + (size_t)y * (size_t)rowbytes,
            frame->pixels + (size_t)y * (size_t)frame->rowbytes,
            (size_t)rowbytes);
    }

    std::lock_guard<std::mutex> lock(s_pending_frame_mutex);
    s_pending_frame.pixels = std::move(pixels);
    s_pending_frame.width = frame->width;
    s_pending_frame.height = frame->height;
    s_pending_frame.rowbytes = rowbytes;
    s_pending_frame.pixbytes = frame->pixbytes;
    s_pending_frame.filter_index = frame->filter_index;
    s_pending_frame.monitor_id = frame->monitor_id;
    s_pending_frame.backbuffers = frame->backbuffers;
    s_pending_frame.valid = true;
}

static bool pop_queued_video_frame(unix_pending_video_frame *frame)
{
    std::lock_guard<std::mutex> lock(s_pending_frame_mutex);
    if (!s_pending_frame.valid) {
        return false;
    }
    *frame = std::move(s_pending_frame);
    s_pending_frame = unix_pending_video_frame();
    return true;
}

static void destroy_frame_textures(void)
{
    for (SDL_Texture *texture : s_textures) {
        SDL_DestroyTexture(texture);
    }
    s_textures.clear();
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
    if (!s_gl_textures.empty()) {
        glDeleteTextures((GLsizei)s_gl_textures.size(), s_gl_textures.data());
        s_gl_textures.clear();
    }
    s_gl_texture = 0;
#endif
    s_texture = NULL;
    s_texture_width = 0;
    s_texture_height = 0;
    s_texture_pixbytes = 0;
    s_texture_backbuffers = 0;
    s_texture_index = 0;
}

static SDL_DisplayID *get_sdl_displays(int *count)
{
    if (count) {
        *count = 0;
    }
    if (!unix_video_setup()) {
        return NULL;
    }
    return SDL_GetDisplays(count);
}

static SDL_DisplayID get_sdl_display_for_index(int display_index)
{
    int count = 0;
    SDL_DisplayID *displays = get_sdl_displays(&count);
    SDL_DisplayID display = 0;

    if (displays && count > 0) {
        if (display_index > 0 && display_index <= count) {
            display = displays[display_index - 1];
        } else {
            display = displays[0];
        }
    }
    if (displays) {
        SDL_free(displays);
    }
    if (!display) {
        display = SDL_GetPrimaryDisplay();
    }
    return display;
}

static void center_window_on_display(SDL_DisplayID display)
{
    if (!s_window || !display) {
        return;
    }
    SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

static float refresh_rate_from_mode(const SDL_DisplayMode *mode)
{
    if (!mode) {
        return 0.0f;
    }
    if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
        return (float)mode->refresh_rate_numerator / (float)mode->refresh_rate_denominator;
    }
    return mode->refresh_rate;
}

static int rounded_refresh_rate_from_mode(const SDL_DisplayMode *mode)
{
    float refresh = refresh_rate_from_mode(mode);
    return refresh > 0.0f ? (int)(refresh + 0.5f) : 0;
}

static void add_display_mode(struct unix_video_display_mode *modes, int max_modes, int *count,
    int width, int height, int refresh_rate)
{
    if (!modes || !count || width <= 0 || height <= 0) {
        return;
    }

    for (int i = 0; i < *count; i++) {
        if (modes[i].width == width && modes[i].height == height) {
            if (!modes[i].refresh_rate && refresh_rate) {
                modes[i].refresh_rate = refresh_rate;
            }
            return;
        }
    }
    if (*count >= max_modes) {
        return;
    }

    modes[*count].width = width;
    modes[*count].height = height;
    modes[*count].refresh_rate = refresh_rate;
    (*count)++;
}

static void add_sdl_display_modes(SDL_DisplayID display, struct unix_video_display_mode *modes, int max_modes, int *count)
{
    if (!display) {
        return;
    }

    int mode_count = 0;
    SDL_DisplayMode **fullscreen_modes = SDL_GetFullscreenDisplayModes(display, &mode_count);
    if (fullscreen_modes) {
        for (int i = 0; i < mode_count; i++) {
            const SDL_DisplayMode *mode = fullscreen_modes[i];
            if (mode) {
                add_display_mode(modes, max_modes, count, mode->w, mode->h, rounded_refresh_rate_from_mode(mode));
            }
        }
        SDL_free(fullscreen_modes);
    }

    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
    if (desktop) {
        add_display_mode(modes, max_modes, count, desktop->w, desktop->h, rounded_refresh_rate_from_mode(desktop));
    }

    const SDL_DisplayMode *current = SDL_GetCurrentDisplayMode(display);
    if (current) {
        add_display_mode(modes, max_modes, count, current->w, current->h, rounded_refresh_rate_from_mode(current));
    }
}

static bool copy_sdl_display_name(int index, bool friendlyname, TCHAR *dst, size_t dstsize)
{
    int count = 0;
    SDL_DisplayID *displays = get_sdl_displays(&count);
    if (!displays) {
        return false;
    }

    bool found = false;
    if (index >= 0 && index < count) {
        if (friendlyname) {
            const char *name = SDL_GetDisplayName(displays[index]);
            if (name && name[0]) {
                snprintf(dst, dstsize, "%s", name);
                found = true;
            }
        } else {
            snprintf(dst, dstsize, "SDL:%u", (unsigned int)displays[index]);
            found = true;
        }
    }

    SDL_free(displays);
    return found;
}

int target_get_display(const TCHAR *name)
{
    if (!name || !name[0]) {
        return -1;
    }

    int count = 0;
    SDL_DisplayID *displays = get_sdl_displays(&count);
    if (!displays) {
        return -1;
    }

    int found = -1;
    unsigned int displayid = 0;
    if (sscanf(name, "SDL:%u", &displayid) == 1) {
        for (int i = 0; i < count; i++) {
            if (displays[i] == (SDL_DisplayID)displayid) {
                found = i + 1;
                break;
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            const char *displayname = SDL_GetDisplayName(displays[i]);
            if (displayname && !_tcsicmp(displayname, name)) {
                found = i + 1;
                break;
            }
        }
    }

    SDL_free(displays);
    return found;
}

const TCHAR *target_get_display_name(int num, bool friendlyname)
{
    if (num <= 0) {
        return NULL;
    }
    if (!copy_sdl_display_name(num - 1, friendlyname, s_display_name, sizeof s_display_name / sizeof s_display_name[0])) {
        return NULL;
    }
    return s_display_name;
}

static int unix_input_lock_state_from_sdl(SDL_Keymod mod)
{
    int lockstate = 0;
    if (mod & SDL_KMOD_CAPS) {
        lockstate |= UNIX_INPUT_LOCK_CAPS;
    }
    if (mod & SDL_KMOD_NUM) {
        lockstate |= UNIX_INPUT_LOCK_NUM;
    }
    if (mod & SDL_KMOD_SCROLL) {
        lockstate |= UNIX_INPUT_LOCK_SCROLL;
    }
    return lockstate;
}

static int statusbar_source_height(void)
{
    return TD_TOTAL_HEIGHT;
}

static int statusbar_display_height(void)
{
    return statusbar_source_height() * UnixStatusScale;
}

static void init_status_colors(void)
{
    if (s_status_colors_ready) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        s_status_rc[i] = 0xff000000u | (uae_u32(i) << 16);
        s_status_gc[i] = uae_u32(i) << 8;
        s_status_bc[i] = uae_u32(i);
    }
    s_status_colors_ready = true;
}

static bool ensure_texture(int width, int height, int pixbytes, int backbuffers)
{
    if (!s_renderer || width <= 0 || height <= 0) {
        return false;
    }
    backbuffers = clamp_backbuffer_count(backbuffers);
    if (!s_textures.empty() && s_texture_width == width && s_texture_height == height &&
        s_texture_pixbytes == pixbytes && s_texture_backbuffers == backbuffers) {
        return true;
    }

    destroy_frame_textures();

    for (int i = 0; i < backbuffers; i++) {
        SDL_Texture *texture = SDL_CreateTexture(s_renderer, texture_format_for_pixbytes(pixbytes),
            SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!texture) {
            write_log(_T("SDL3: failed to create %dx%d texture: %s\n"), width, height, SDL_GetError());
            destroy_frame_textures();
            return false;
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        s_textures.push_back(texture);
    }
    s_texture = s_textures[0];
    s_texture_width = width;
    s_texture_height = height;
    s_texture_pixbytes = pixbytes;
    s_texture_backbuffers = backbuffers;
    s_texture_index = 0;
    return true;
}

static const struct gfx_filterdata *filterdata_for_frame(const struct unix_video_frame *frame)
{
    int index = frame ? frame->filter_index : GF_NORMAL;
    if (index < 0 || index >= MAX_FILTERDATA) {
        index = GF_NORMAL;
    }
    return &currprefs.gf[index];
}

static float bounded_scale(float value)
{
    if (!std::isfinite(value) || value < 0.01f) {
        return 1.0f;
    }
    if (value > 64.0f) {
        return 64.0f;
    }
    return value;
}

static SDL_FRect filtered_frame_rect(const struct unix_video_frame *frame, const struct gfx_filterdata *filter)
{
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    if (filter) {
        if (filter->gfx_filter_horiz_zoom_mult > 0.0f) {
            scale_x = filter->gfx_filter_horiz_zoom_mult;
        }
        if (filter->gfx_filter_vert_zoom_mult > 0.0f) {
            scale_y = filter->gfx_filter_vert_zoom_mult;
        }
        scale_x += scale_x * (filter->gfx_filter_horiz_zoom / 1000.0f) / 2.0f;
        scale_y += scale_y * (filter->gfx_filter_vert_zoom / 1000.0f) / 2.0f;
        offset_x = -(filter->gfx_filter_horiz_offset / 10000.0f) * frame->width;
        offset_y = -(filter->gfx_filter_vert_offset / 10000.0f) * frame->height;
    }

    scale_x = bounded_scale(scale_x);
    scale_y = bounded_scale(scale_y);

    SDL_FRect rect;
    rect.w = frame->width * scale_x;
    rect.h = frame->height * scale_y;
    rect.x = (frame->width - rect.w) / 2.0f + offset_x;
    rect.y = (frame->height - rect.h) / 2.0f + offset_y;
    return rect;
}

static void get_window_pixel_scale(int output_width, int output_height, float *scale_x, float *scale_y)
{
    int window_width = 0;
    int window_height = 0;

    if (scale_x) {
        *scale_x = 1.0f;
    }
    if (scale_y) {
        *scale_y = 1.0f;
    }
    if (!s_window || output_width <= 0 || output_height <= 0) {
        return;
    }

    SDL_GetWindowSize(s_window, &window_width, &window_height);
    if (window_width > 0 && scale_x) {
        *scale_x = std::max(0.01f, (float)output_width / (float)window_width);
    }
    if (window_height > 0 && scale_y) {
        *scale_y = std::max(0.01f, (float)output_height / (float)window_height);
    }
}

static bool get_window_pixel_size(int *width, int *height)
{
    int window_width = 0;
    int window_height = 0;

    if (!s_window) {
        return false;
    }
    SDL_GetWindowSizeInPixels(s_window, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        SDL_GetWindowSize(s_window, &window_width, &window_height);
    }
    if (window_width <= 0 || window_height <= 0) {
        return false;
    }

    if (width) {
        *width = window_width;
    }
    if (height) {
        *height = window_height;
    }
    return true;
}

static bool get_renderer_output_size(int *width, int *height)
{
    int output_width = 0;
    int output_height = 0;

    if (s_renderer && SDL_GetRenderOutputSize(s_renderer, &output_width, &output_height) &&
        output_width > 0 && output_height > 0) {
        if (width) {
            *width = output_width;
        }
        if (height) {
            *height = output_height;
        }
        return true;
    }
    return get_window_pixel_size(width, height);
}

static bool make_video_layout(const struct unix_video_frame *frame, const struct gfx_filterdata *filter,
    int output_width, int output_height, struct unix_video_layout *layout)
{
    if (!frame || !layout || frame->width <= 0 || frame->height <= 0 ||
        output_width <= 0 || output_height <= 0) {
        return false;
    }

    memset(layout, 0, sizeof(*layout));
    get_window_pixel_scale(output_width, output_height, &layout->pixel_scale_x, &layout->pixel_scale_y);

    int status_height = std::max(1, (int)(statusbar_display_height() * layout->pixel_scale_y + 0.5f));
    if (status_height >= output_height) {
        status_height = std::max(0, output_height - 1);
    }
    const int frame_area_height = std::max(1, output_height - status_height);

    SDL_FRect source_rect = filtered_frame_rect(frame, filter);
    SDL_FRect dst = {};
    int mode = frame->filter_index == GF_RTG && filter ? filter->gfx_filter_autoscale : 0;

    if (frame->filter_index == GF_RTG && mode == 2) { /* center */
        dst.w = source_rect.w * layout->pixel_scale_x;
        dst.h = source_rect.h * layout->pixel_scale_y;
        dst.x = ((float)output_width - dst.w) / 2.0f + source_rect.x * layout->pixel_scale_x;
        dst.y = ((float)frame_area_height - dst.h) / 2.0f + source_rect.y * layout->pixel_scale_y;
    } else if (frame->filter_index == GF_RTG && mode == 3) { /* integer */
        int ix = std::max(1, output_width / frame->width);
        int iy = std::max(1, frame_area_height / frame->height);
        int scale = std::max(1, std::min(ix, iy));
        dst.w = source_rect.w * scale;
        dst.h = source_rect.h * scale;
        dst.x = ((float)output_width - (float)frame->width * scale) / 2.0f + source_rect.x * scale;
        dst.y = ((float)frame_area_height - (float)frame->height * scale) / 2.0f + source_rect.y * scale;
    } else {
        const float sx = (float)output_width / (float)frame->width;
        const float sy = (float)frame_area_height / (float)frame->height;
        float draw_sx = sx;
        float draw_sy = sy;

        if (frame->filter_index == GF_RTG && mode == 1 && currprefs.win32_rtgscaleaspectratio) {
            draw_sx = draw_sy = std::min(sx, sy);
        }
        dst.w = source_rect.w * draw_sx;
        dst.h = source_rect.h * draw_sy;
        dst.x = ((float)output_width - (float)frame->width * draw_sx) / 2.0f + source_rect.x * draw_sx;
        dst.y = ((float)frame_area_height - (float)frame->height * draw_sy) / 2.0f + source_rect.y * draw_sy;
    }

    layout->frame_dst = dst;
    layout->frame_clip = { 0, 0, output_width, frame_area_height };
    layout->status_dst = {
        0.0f,
        (float)frame_area_height,
        (float)output_width,
        (float)status_height
    };
    return true;
}

static int valid_monitor_id(int monitor_id)
{
    if (monitor_id < 0 || monitor_id >= MAX_AMIGADISPLAYS) {
        return 0;
    }
    return monitor_id;
}

static void configured_window_size(int monitor_id, int *width, int *height)
{
    monitor_id = valid_monitor_id(monitor_id);
    int w = currprefs.gfx_monitor[monitor_id].gfx_size.width;
    int h = currprefs.gfx_monitor[monitor_id].gfx_size.height;
    if (w <= 0) {
        w = currprefs.gfx_monitor[monitor_id].gfx_size_win.width;
    }
    if (h <= 0) {
        h = currprefs.gfx_monitor[monitor_id].gfx_size_win.height;
    }
    if (width) {
        *width = w;
    }
    if (height) {
        *height = h;
    }
}

static void auto_resize_window_for_rtg(const struct unix_video_frame *frame,
    const struct gfx_filterdata *filter)
{
    if (!s_window || s_active_window_mode != UNIX_VIDEO_WINDOWED || !frame ||
        frame->filter_index != GF_RTG || frame->width <= 0 || frame->height <= 0) {
        s_auto_window_width = 0;
        s_auto_window_height = 0;
        return;
    }

    SDL_WindowFlags flags = SDL_GetWindowFlags(s_window);
    if (flags & SDL_WINDOW_MAXIMIZED) {
        return;
    }

    SDL_FRect rect = filtered_frame_rect(frame, filter);
    int source_width = std::max(1, (int)std::ceil(rect.w));
    int source_height = std::max(1, (int)std::ceil(rect.h));
    int configured_width = 0;
    int configured_height = 0;
    int desired_width = source_width;
    int desired_height = source_height;

    configured_window_size(frame->monitor_id, &configured_width, &configured_height);
    bool have_configured = configured_width > 0 && configured_height > 0;
    int mode = filter ? filter->gfx_filter_autoscale : 0;

    switch (mode) {
    case 1: /* scale */
        if (have_configured && currprefs.win32_rtgallowscaling) {
            desired_width = configured_width;
            desired_height = configured_height;
        } else if (have_configured) {
            desired_width = std::max(configured_width, source_width);
            desired_height = std::max(configured_height, source_height);
        }
        break;
    case 2: /* center */
        if (have_configured &&
            (currprefs.win32_rtgallowscaling ||
             (configured_width >= source_width && configured_height >= source_height))) {
            desired_width = configured_width;
            desired_height = configured_height;
        }
        break;
    case 3: /* integer */
        if (have_configured) {
            desired_width = configured_width;
            desired_height = configured_height;
        }
        break;
    default: /* resize */
        break;
    }

    desired_height += statusbar_display_height();
    if (desired_width == s_auto_window_width && desired_height == s_auto_window_height) {
        return;
    }

    if (SDL_SetWindowSize(s_window, desired_width, desired_height)) {
        s_auto_window_width = desired_width;
        s_auto_window_height = desired_height;
        write_log(_T("SDL3: RTG window resize %dx%d\n"), desired_width, desired_height);
    } else {
        write_log(_T("SDL3: failed to resize RTG window to %dx%d: %s\n"),
            desired_width, desired_height, SDL_GetError());
    }
}

static int clamp_filter_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static void render_scanline_overlay(const struct gfx_filterdata *filter, const SDL_FRect *frame_dst)
{
    if (!filter || !frame_dst) {
        return;
    }

    int opacity = clamp_filter_percent(filter->gfx_filter_scanlines);
    int level = clamp_filter_percent(filter->gfx_filter_scanlinelevel);
    if (!opacity && !level) {
        return;
    }

    int lit_lines = filter->gfx_filter_scanlineratio & 15;
    int shaded_lines = (filter->gfx_filter_scanlineratio >> 4) & 15;
    int period = lit_lines + shaded_lines;
    if (period <= 0 || shaded_lines <= 0) {
        return;
    }

    int offset = filter->gfx_filter_scanlineoffset % (lit_lines + 1);
    if (offset < 0) {
        offset += lit_lines + 1;
    }

    Uint8 alpha = (Uint8)(opacity * 255 / 100);
    Uint8 color = (Uint8)(level * 255 / 100);
    int height = (int)(frame_dst->h + 0.5f);

    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(s_renderer, color, color, color, alpha);
    for (int y = 0; y < height; y += period) {
        int y2 = y + offset;
        for (int yy = 0; yy < shaded_lines && y2 + yy < height; yy++) {
            SDL_FRect line = {
                frame_dst->x,
                frame_dst->y + (float)(y2 + yy),
                frame_dst->w,
                1.0f
            };
            SDL_RenderFillRect(s_renderer, &line);
        }
    }
    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_NONE);
}

static bool ensure_status_pixels(int width)
{
    const int height = statusbar_source_height();
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (s_status_width == width && s_status_height == height) {
        return true;
    }

    if (s_status_texture) {
        SDL_DestroyTexture(s_status_texture);
        s_status_texture = NULL;
    }
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
    if (s_gl_status_texture) {
        glDeleteTextures(1, &s_gl_status_texture);
        s_gl_status_texture = 0;
    }
#endif
    s_status_width = width;
    s_status_height = height;
    s_status_pixels.resize(size_t(width) * size_t(height));
    return true;
}

static bool ensure_status_texture(int width)
{
    if (!s_renderer || !ensure_status_pixels(width)) {
        return false;
    }
    if (s_status_texture) {
        return true;
    }

    const int height = statusbar_source_height();
    s_status_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!s_status_texture) {
        write_log(_T("SDL3: failed to create %dx%d status texture: %s\n"), width, height, SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(s_status_texture, SDL_BLENDMODE_NONE);
    return true;
}

static bool update_status_pixels(int width)
{
    if (!ensure_status_pixels(width)) {
        return false;
    }

    init_status_colors();
    std::fill(s_status_pixels.begin(), s_status_pixels.end(), 0xffd4d0c8u);
    statusline_set_multiplier(0, width, statusbar_source_height());
    for (int y = 0; y < statusbar_source_height(); y++) {
        draw_status_line_single(
            0,
            reinterpret_cast<uae_u8 *>(s_status_pixels.data() + size_t(y) * size_t(width)),
            y,
            width,
            s_status_rc,
            s_status_gc,
            s_status_bc,
            NULL);
    }
    return true;
}

static bool update_status_texture(int width)
{
    if (!ensure_status_texture(width) || !update_status_pixels(width)) {
        return false;
    }

    SDL_UpdateTexture(s_status_texture, NULL, s_status_pixels.data(), width * int(sizeof(uae_u32)));
    return true;
}

#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
static bool unix_gl_load_functions(void)
{
    if (s_gl_functions_loaded) {
        return true;
    }

    p_glCreateShader = reinterpret_cast<UnixGlCreateShaderProc>(unix_gl_get_proc("glCreateShader"));
    p_glShaderSource = reinterpret_cast<UnixGlShaderSourceProc>(unix_gl_get_proc("glShaderSource"));
    p_glCompileShader = reinterpret_cast<UnixGlCompileShaderProc>(unix_gl_get_proc("glCompileShader"));
    p_glGetShaderiv = reinterpret_cast<UnixGlGetShaderivProc>(unix_gl_get_proc("glGetShaderiv"));
    p_glGetShaderInfoLog = reinterpret_cast<UnixGlGetShaderInfoLogProc>(unix_gl_get_proc("glGetShaderInfoLog"));
    p_glCreateProgram = reinterpret_cast<UnixGlCreateProgramProc>(unix_gl_get_proc("glCreateProgram"));
    p_glAttachShader = reinterpret_cast<UnixGlAttachShaderProc>(unix_gl_get_proc("glAttachShader"));
    p_glLinkProgram = reinterpret_cast<UnixGlLinkProgramProc>(unix_gl_get_proc("glLinkProgram"));
    p_glGetProgramiv = reinterpret_cast<UnixGlGetProgramivProc>(unix_gl_get_proc("glGetProgramiv"));
    p_glGetProgramInfoLog = reinterpret_cast<UnixGlGetProgramInfoLogProc>(unix_gl_get_proc("glGetProgramInfoLog"));
    p_glDeleteShader = reinterpret_cast<UnixGlDeleteShaderProc>(unix_gl_get_proc("glDeleteShader"));
    p_glDeleteProgram = reinterpret_cast<UnixGlDeleteProgramProc>(unix_gl_get_proc("glDeleteProgram"));
    p_glUseProgram = reinterpret_cast<UnixGlUseProgramProc>(unix_gl_get_proc("glUseProgram"));
    p_glGetUniformLocation = reinterpret_cast<UnixGlGetUniformLocationProc>(unix_gl_get_proc("glGetUniformLocation"));
    p_glUniform1i = reinterpret_cast<UnixGlUniform1iProc>(unix_gl_get_proc("glUniform1i"));
    p_glUniform1f = reinterpret_cast<UnixGlUniform1fProc>(unix_gl_get_proc("glUniform1f"));
    p_glUniform2f = reinterpret_cast<UnixGlUniform2fProc>(unix_gl_get_proc("glUniform2f"));
    p_glUniform4f = reinterpret_cast<UnixGlUniform4fProc>(unix_gl_get_proc("glUniform4f"));

    s_gl_functions_loaded = p_glCreateShader && p_glShaderSource && p_glCompileShader &&
        p_glGetShaderiv && p_glGetShaderInfoLog && p_glCreateProgram && p_glAttachShader &&
        p_glLinkProgram && p_glGetProgramiv && p_glGetProgramInfoLog && p_glDeleteShader &&
        p_glDeleteProgram && p_glUseProgram && p_glGetUniformLocation && p_glUniform1i &&
        p_glUniform1f && p_glUniform2f && p_glUniform4f;
    return s_gl_functions_loaded;
}

static bool unix_gl_shader_ok(GLuint shader, const char *label)
{
    GLint ok = 0;
    p_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) {
        return true;
    }

    GLint length = 0;
    p_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(length > 1 ? length : 1, 0);
    if (length > 1) {
        p_glGetShaderInfoLog(shader, length, NULL, log.data());
    }
    write_log(_T("OpenGL shader pipeline: %s compile failed: %s\n"), label, log.data());
    return false;
}

static bool unix_gl_program_ok(GLuint program)
{
    GLint ok = 0;
    p_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) {
        return true;
    }

    GLint length = 0;
    p_glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(length > 1 ? length : 1, 0);
    if (length > 1) {
        p_glGetProgramInfoLog(program, length, NULL, log.data());
    }
    write_log(_T("OpenGL shader pipeline: program link failed: %s\n"), log.data());
    return false;
}

static GLuint unix_gl_compile_shader(GLenum type, const char *source, const char *label)
{
    GLuint shader = p_glCreateShader(type);
    if (!shader) {
        return 0;
    }
    const GLchar *sources[] = { reinterpret_cast<const GLchar *>(source) };
    p_glShaderSource(shader, 1, sources, NULL);
    p_glCompileShader(shader);
    if (!unix_gl_shader_ok(shader, label)) {
        p_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool unix_gl_build_program(void)
{
    if (s_gl_program) {
        return true;
    }
    if (!unix_gl_load_functions()) {
        return false;
    }

    static const char *vertex_shader =
        "#version 120\n"
        "varying vec2 v_tex;\n"
        "void main(void) {\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "    v_tex = gl_MultiTexCoord0.xy;\n"
        "}\n";
    static const char *fragment_shader =
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_source_size;\n"
        "uniform vec4 u_adjust;\n"
        "uniform vec4 u_scanline;\n"
        "uniform vec4 u_blur_noise;\n"
        "uniform float u_frame;\n"
        "uniform float u_scaler;\n"
        "varying vec2 v_tex;\n"
        "float rand(vec2 co) {\n"
        "    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);\n"
        "}\n"
        "bool ceq(vec4 a, vec4 b) {\n"
        "    return all(lessThan(abs(a.rgb - b.rgb), vec3(1.0 / 255.0)));\n"
        "}\n"
        "vec4 scale2x_sample(vec2 uv, vec2 texel) {\n"
        "    vec2 p = uv * u_source_size;\n"
        "    vec2 q = step(vec2(0.5), fract(p));\n"
        "    vec2 base = (floor(p) + vec2(0.5)) * texel;\n"
        "    vec4 e = texture2D(u_tex, base);\n"
        "    vec4 up = texture2D(u_tex, base + vec2(0.0, -texel.y));\n"
        "    vec4 dn = texture2D(u_tex, base + vec2(0.0, texel.y));\n"
        "    vec4 lt = texture2D(u_tex, base + vec2(-texel.x, 0.0));\n"
        "    vec4 rt = texture2D(u_tex, base + vec2(texel.x, 0.0));\n"
        "    vec4 hn = mix(lt, rt, q.x);\n"
        "    vec4 hf = mix(rt, lt, q.x);\n"
        "    vec4 vn = mix(up, dn, q.y);\n"
        "    vec4 vf = mix(dn, up, q.y);\n"
        "    if (ceq(vn, hn) && !ceq(hn, vf) && !ceq(vn, hf)) {\n"
        "        return hn;\n"
        "    }\n"
        "    return e;\n"
        "}\n"
        "void main(void) {\n"
        "    vec2 texel = 1.0 / max(u_source_size, vec2(1.0));\n"
        "    vec4 color;\n"
        "    if (u_scaler > 0.5) {\n"
        "        color = scale2x_sample(v_tex, texel);\n"
        "    } else {\n"
        "        color = texture2D(u_tex, v_tex);\n"
        "    }\n"
        "    float blur = clamp(u_blur_noise.x, 0.0, 1.0);\n"
        "    if (blur > 0.001) {\n"
        "        vec4 sum = color * 4.0;\n"
        "        sum += texture2D(u_tex, v_tex + vec2(texel.x, 0.0));\n"
        "        sum += texture2D(u_tex, v_tex - vec2(texel.x, 0.0));\n"
        "        sum += texture2D(u_tex, v_tex + vec2(0.0, texel.y));\n"
        "        sum += texture2D(u_tex, v_tex - vec2(0.0, texel.y));\n"
        "        color = mix(color, sum / 8.0, blur);\n"
        "    }\n"
        "    float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
        "    color.rgb = mix(vec3(luma), color.rgb, u_adjust.z);\n"
        "    color.rgb = (color.rgb - vec3(0.5)) * u_adjust.y + vec3(0.5 + u_adjust.x);\n"
        "    color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(u_adjust.w));\n"
        "    if (u_scanline.x > 0.001 && u_scanline.z > 0.5) {\n"
        "        float y = mod(gl_FragCoord.y + u_scanline.w, u_scanline.z);\n"
        "        if (y < u_scanline.y) {\n"
        "            color.rgb *= 1.0 - u_scanline.x;\n"
        "        }\n"
        "    }\n"
        "    float noise = clamp(u_blur_noise.y, 0.0, 1.0);\n"
        "    if (noise > 0.001) {\n"
        "        color.rgb += (rand(v_tex * u_source_size + vec2(u_frame, u_frame * 0.37)) - 0.5) * noise;\n"
        "    }\n"
        "    gl_FragColor = clamp(color, 0.0, 1.0);\n"
        "}\n";

    GLuint vs = unix_gl_compile_shader(GL_VERTEX_SHADER, vertex_shader, "vertex");
    GLuint fs = unix_gl_compile_shader(GL_FRAGMENT_SHADER, fragment_shader, "fragment");
    if (!vs || !fs) {
        if (vs) {
            p_glDeleteShader(vs);
        }
        if (fs) {
            p_glDeleteShader(fs);
        }
        return false;
    }

    GLuint program = p_glCreateProgram();
    p_glAttachShader(program, vs);
    p_glAttachShader(program, fs);
    p_glLinkProgram(program);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    if (!unix_gl_program_ok(program)) {
        p_glDeleteProgram(program);
        return false;
    }

    s_gl_program = program;
    s_gl_uniform_texture = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_tex"));
    s_gl_uniform_source_size = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_source_size"));
    s_gl_uniform_adjust = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_adjust"));
    s_gl_uniform_scanline = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_scanline"));
    s_gl_uniform_blur_noise = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_blur_noise"));
    s_gl_uniform_frame = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_frame"));
    s_gl_uniform_scaler = p_glGetUniformLocation(program, reinterpret_cast<const GLchar *>("u_scaler"));
    return true;
}

static bool unix_gl_ensure_context(int width, int height)
{
    if (s_gl_active) {
        return true;
    }
    if (!s_window || s_gl_failed) {
        return false;
    }

    s_gl_context = SDL_GL_CreateContext(s_window);
    if (!s_gl_context) {
        write_log(_T("OpenGL shader pipeline: context creation failed: %s\n"), SDL_GetError());
        s_gl_failed = true;
        return false;
    }
    SDL_GL_MakeCurrent(s_window, s_gl_context);
    SDL_GL_SetSwapInterval(1);
    if (!unix_gl_build_program()) {
        SDL_GL_DestroyContext(s_gl_context);
        s_gl_context = NULL;
        s_gl_failed = true;
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, width, height);
    s_gl_active = true;
    write_log(_T("OpenGL shader pipeline enabled\n"));
    return true;
}

static void unix_gl_destroy_status_texture(void)
{
    if (s_gl_status_texture) {
        glDeleteTextures(1, &s_gl_status_texture);
        s_gl_status_texture = 0;
    }
}

static bool unix_gl_ensure_textures(int width, int height, int pixbytes, int backbuffers)
{
    if (!s_gl_active || width <= 0 || height <= 0) {
        return false;
    }
    backbuffers = clamp_backbuffer_count(backbuffers);
    if (!s_gl_textures.empty() && s_texture_width == width && s_texture_height == height &&
        s_texture_pixbytes == pixbytes && s_texture_backbuffers == backbuffers) {
        return true;
    }

    destroy_frame_textures();
    s_gl_textures.resize(backbuffers);
    glGenTextures(backbuffers, s_gl_textures.data());
    for (GLuint texture : s_gl_textures) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (pixbytes == 2) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        }
    }
    s_gl_texture = s_gl_textures[0];
    s_texture_width = width;
    s_texture_height = height;
    s_texture_pixbytes = pixbytes;
    s_texture_backbuffers = backbuffers;
    s_texture_index = 0;
    return true;
}

static bool unix_gl_ensure_status_texture(int width)
{
    if (!s_gl_active || !update_status_pixels(width)) {
        return false;
    }
    if (!s_gl_status_texture) {
        glGenTextures(1, &s_gl_status_texture);
        glBindTexture(GL_TEXTURE_2D, s_gl_status_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, statusbar_source_height(), 0,
            GL_BGRA, GL_UNSIGNED_BYTE, s_status_pixels.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, s_gl_status_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, statusbar_source_height(),
            GL_BGRA, GL_UNSIGNED_BYTE, s_status_pixels.data());
    }
    return true;
}

static void unix_gl_draw_texture(GLuint texture, const SDL_FRect &dst, bool shader,
    const struct gfx_filterdata *filter, int source_width, int source_height,
    int scaler = 0)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    if (shader) {
        p_glUseProgram(s_gl_program);
        p_glUniform1i(s_gl_uniform_texture, 0);
        p_glUniform2f(s_gl_uniform_source_size, (GLfloat)source_width, (GLfloat)source_height);
        p_glUniform1f(s_gl_uniform_scaler, (GLfloat)scaler);

        float luminance = filter ? filter->gfx_filter_luminance / 1000.0f : 0.0f;
        float contrast = filter ? 1.0f + filter->gfx_filter_contrast / 1000.0f : 1.0f;
        float saturation = filter ? 1.0f + filter->gfx_filter_saturation / 1000.0f : 1.0f;
        float gamma = filter ? 1.0f + filter->gfx_filter_gamma / 1000.0f : 1.0f;
        if (contrast < 0.0f) {
            contrast = 0.0f;
        }
        if (saturation < 0.0f) {
            saturation = 0.0f;
        }
        if (gamma <= 0.01f) {
            gamma = 1.0f;
        }
        p_glUniform4f(s_gl_uniform_adjust, luminance, contrast, saturation, 1.0f / gamma);

        int lit_lines = filter ? (filter->gfx_filter_scanlineratio & 15) : 0;
        int shaded_lines = filter ? ((filter->gfx_filter_scanlineratio >> 4) & 15) : 0;
        int period = lit_lines + shaded_lines;
        float opacity = filter ? clamp_filter_percent(filter->gfx_filter_scanlines) / 100.0f : 0.0f;
        if (period <= 0 || shaded_lines <= 0) {
            opacity = 0.0f;
            period = 0;
            shaded_lines = 0;
        }
        float scan_offset = filter ? (float)filter->gfx_filter_scanlineoffset : 0.0f;
        p_glUniform4f(s_gl_uniform_scanline, opacity, (GLfloat)shaded_lines,
            (GLfloat)period, scan_offset);
        float blur = filter ? std::min(1.0f, std::max(0.0f, filter->gfx_filter_blur / 1000.0f)) : 0.0f;
        float noise = filter ? std::min(1.0f, std::max(0.0f, filter->gfx_filter_noise / 1000.0f)) : 0.0f;
        p_glUniform4f(s_gl_uniform_blur_noise, blur, noise, 0.0f, 0.0f);
        p_glUniform1f(s_gl_uniform_frame, (GLfloat)s_gl_frame_counter);
    } else {
        p_glUseProgram(0);
    }

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(dst.x, dst.y);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(dst.x + dst.w, dst.y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(dst.x + dst.w, dst.y + dst.h);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(dst.x, dst.y + dst.h);
    glEnd();
    if (shader) {
        p_glUseProgram(0);
    }
}

static bool unix_gl_upload_frame(const struct unix_video_frame *frame)
{
    if (!unix_gl_ensure_textures(frame->width, frame->height, frame->pixbytes, frame->backbuffers)) {
        return false;
    }
    if (!s_gl_textures.empty()) {
        s_texture_index = (s_texture_index + 1) % (int)s_gl_textures.size();
        s_gl_texture = s_gl_textures[s_texture_index];
    }

    glBindTexture(GL_TEXTURE_2D, s_gl_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (frame->rowbytes == frame->width * frame->pixbytes) {
        if (frame->pixbytes == 2) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
                GL_RGB, GL_UNSIGNED_SHORT_5_6_5, frame->pixels);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
                GL_BGRA, GL_UNSIGNED_BYTE, frame->pixels);
        }
    } else {
        std::vector<uae_u8> packed((size_t)frame->width * (size_t)frame->height * (size_t)frame->pixbytes);
        for (int y = 0; y < frame->height; y++) {
            memcpy(packed.data() + (size_t)y * (size_t)frame->width * (size_t)frame->pixbytes,
                frame->pixels + (size_t)y * (size_t)frame->rowbytes,
                (size_t)frame->width * (size_t)frame->pixbytes);
        }
        if (frame->pixbytes == 2) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
                GL_RGB, GL_UNSIGNED_SHORT_5_6_5, packed.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
                GL_BGRA, GL_UNSIGNED_BYTE, packed.data());
        }
    }
    return true;
}

static void unix_gl_present(const struct unix_video_frame *frame, const struct gfx_filterdata *filter)
{
    if (!unix_gl_upload_frame(frame)) {
        return;
    }
    int output_width = 0;
    int output_height = 0;
    struct unix_video_layout layout;

    if (!get_window_pixel_size(&output_width, &output_height) ||
        !make_video_layout(frame, filter, output_width, output_height, &layout)) {
        return;
    }

    /* Scale2x samples discrete texels; it requires nearest filtering. */
    const TCHAR *shader_name = unix_gfx_shader_option();
    const int scaler = shader_name && !_tcsicmp(shader_name, _T("scale2x")) ? 1 : 0;
    const bool bilinear = !scaler && filter && filter->gfx_filter_bilinear;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        bilinear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
        bilinear ? GL_LINEAR : GL_NEAREST);

    glViewport(0, 0, output_width, output_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, output_width, output_height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    glScissor(layout.frame_clip.x,
        output_height - layout.frame_clip.y - layout.frame_clip.h,
        layout.frame_clip.w, layout.frame_clip.h);
    unix_gl_draw_texture(s_gl_texture, layout.frame_dst, true, filter, frame->width, frame->height, scaler);
    glDisable(GL_SCISSOR_TEST);

    if (unix_gl_ensure_status_texture(frame->width)) {
        unix_gl_draw_texture(s_gl_status_texture, layout.status_dst, true, NULL,
            frame->width, statusbar_source_height());
    }

    SDL_GL_SwapWindow(s_window);
    s_gl_frame_counter++;
}
#endif

static int unix_mouse_button_from_sdl(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    default:
        return -1;
    }
}

static SDL_MouseButtonFlags unix_mouse_button_mask(Uint8 button)
{
    if (button == 0 || button > 32) {
        return 0;
    }
    return SDL_BUTTON_MASK(button);
}

static bool unix_event_for_window(SDL_WindowID window_id)
{
    return s_window && window_id != 0 && window_id == SDL_GetWindowID(s_window);
}

static bool unix_mouse_point_in_client(float x, float y)
{
    int width = 0;
    int height = 0;

    if (!s_window) {
        return false;
    }
    SDL_GetWindowSize(s_window, &width, &height);
    return width > 0 && height > 0 &&
        x >= 0.0f && y >= 0.0f && x < (float)width && y < (float)height;
}

static bool unix_mouse_event_in_client(SDL_WindowID window_id, float x, float y)
{
    return unix_event_for_window(window_id) && unix_mouse_point_in_client(x, y);
}

static bool statusbar_logical_position(int window_x, int window_y, int *logical_x, int *logical_y)
{
    if (!s_window || s_texture_width <= 0 || s_texture_height <= 0) {
        return false;
    }

    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(s_window, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        return false;
    }

    const int logical_width = s_texture_width;
    const int logical_height = s_texture_height + statusbar_display_height();
    const int lx = window_x * logical_width / window_width;
    const int ly = window_y * logical_height / window_height;
    if (ly < s_texture_height || ly >= logical_height) {
        return false;
    }
    if (logical_x) {
        *logical_x = lx;
    }
    if (logical_y) {
        *logical_y = ly;
    }
    return true;
}

static int statusbar_hit_slot(int logical_x)
{
    if (s_status_width <= 0) {
        return -1;
    }

    int mult = statusline_get_multiplier(0) / 100;
    if (mult < 1) {
        mult = 1;
    }
    const int x_start = (td_numbers_pos & TD_RIGHT)
        ? s_status_width - (td_numbers_padx + VISIBLE_LEDS * td_width) * mult
        : td_numbers_padx * mult;
    const int slot_width = td_width * mult;
    if (slot_width <= 0 || logical_x < x_start) {
        return -1;
    }
    const int slot = (logical_x - x_start) / slot_width;
    return slot >= 0 && slot < VISIBLE_LEDS ? slot : -1;
}

static bool handle_statusbar_click(int window_x, int window_y, Uint8 button)
{
    int logical_x = 0;
    if (!statusbar_logical_position(window_x, window_y, &logical_x, NULL)) {
        return false;
    }

    const int slot = statusbar_hit_slot(logical_x);
    if (slot < 0) {
        return true;
    }

    const bool right_click = button == SDL_BUTTON_RIGHT;
    if (slot >= 8 && slot <= 11) {
        const int drive = slot - 8;
        if (right_click) {
            disk_eject(drive);
        } else if (changed_prefs.floppyslots[drive].dfxtype >= 0) {
            gui_display(drive);
        }
        return true;
    }
    if (slot == 6) {
        if (right_click) {
            changed_prefs.cdslots[0].name[0] = 0;
            changed_prefs.cdslots[0].inuse = false;
            set_config_changed();
        } else {
            gui_display(6);
        }
        return true;
    }
    if (slot == 3) {
        if (right_click) {
            uae_reset(0, 1);
        } else {
            gui_display(-1);
        }
        return true;
    }
    if (slot == 2 && !right_click && pause_emulation) {
        pausemode(0);
        return true;
    }

    return true;
}

bool unix_video_setup(void)
{
    if (s_setup_done) {
        return s_available;
    }

    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        write_log(_T("SDL3: video unavailable: %s\n"), SDL_GetError());
        s_setup_done = true;
        s_available = false;
        return false;
    }

    s_event_thread = uae_thread_get_id();
    s_event_thread_valid = true;
    s_wrong_event_thread_logged = false;
    s_queued_present_logged = false;
    s_setup_done = true;
    s_available = true;
    return true;
}

static bool apply_window_mode(void)
{
    if (!s_window || !s_setup_done || !s_available) {
        return false;
    }

    if (s_requested_window_mode == s_active_window_mode &&
        s_requested_display_index == s_active_display_index &&
        s_requested_fullscreen_width == s_active_fullscreen_width &&
        s_requested_fullscreen_height == s_active_fullscreen_height &&
        s_requested_fullscreen_refresh == s_active_fullscreen_refresh) {
        return true;
    }

    SDL_DisplayID display = get_sdl_display_for_index(s_requested_display_index);
    center_window_on_display(display);

    if (s_requested_window_mode == UNIX_VIDEO_WINDOWED) {
        if (!SDL_SetWindowFullscreen(s_window, false)) {
            write_log(_T("SDL3: failed to leave fullscreen: %s\n"), SDL_GetError());
            return false;
        }
        s_active_window_mode = UNIX_VIDEO_WINDOWED;
        s_active_display_index = s_requested_display_index;
        s_active_fullscreen_width = s_requested_fullscreen_width;
        s_active_fullscreen_height = s_requested_fullscreen_height;
        s_active_fullscreen_refresh = s_requested_fullscreen_refresh;
        return true;
    }

    SDL_DisplayMode fullscreen_mode;
    const SDL_DisplayMode *mode = NULL;
    if (s_requested_window_mode == UNIX_VIDEO_FULLSCREEN) {
        if (s_requested_fullscreen_width > 0 && s_requested_fullscreen_height > 0) {
            float refresh = s_requested_fullscreen_refresh > 0 ? (float)s_requested_fullscreen_refresh : 0.0f;
            if (SDL_GetClosestFullscreenDisplayMode(display, s_requested_fullscreen_width,
                s_requested_fullscreen_height, refresh, true, &fullscreen_mode)) {
                mode = &fullscreen_mode;
            } else {
                write_log(_T("SDL3: no exclusive fullscreen mode %dx%d@%d on display %d, using desktop fullscreen\n"),
                    s_requested_fullscreen_width, s_requested_fullscreen_height,
                    s_requested_fullscreen_refresh, s_requested_display_index);
            }
        } else {
            const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
            if (desktop) {
                fullscreen_mode = *desktop;
                mode = &fullscreen_mode;
            }
        }
    }

    if (!SDL_SetWindowFullscreenMode(s_window, mode)) {
        write_log(_T("SDL3: failed to set fullscreen mode: %s\n"), SDL_GetError());
        return false;
    }
    if (!SDL_SetWindowFullscreen(s_window, true)) {
        write_log(_T("SDL3: failed to enter fullscreen: %s\n"), SDL_GetError());
        return false;
    }

    s_active_window_mode = s_requested_window_mode;
    s_active_display_index = s_requested_display_index;
    s_active_fullscreen_width = s_requested_fullscreen_width;
    s_active_fullscreen_height = s_requested_fullscreen_height;
    s_active_fullscreen_refresh = s_requested_fullscreen_refresh;
    return true;
}

bool unix_video_init(int width, int height, int pixbytes)
{
    if (!unix_video_setup()) {
        return false;
    }

    width = width > 0 ? width : 768;
    height = height > 0 ? height : 576;
    pixbytes = pixbytes == 2 ? 2 : 4;

    if (!s_window) {
        int window_width = clamp_window_dimension(width, 768, 960);
        int window_height = clamp_window_dimension(height + statusbar_display_height(), 576 + statusbar_display_height(), 720 + statusbar_display_height());
        SDL_WindowFlags base_window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        SDL_WindowFlags window_flags = base_window_flags;
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
        bool tried_gl_window = false;
        if (!s_gl_failed) {
            window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_OPENGL);
            tried_gl_window = true;
        }
#endif
        s_window = SDL_CreateWindow(WINUAE_UNIX_WINDOW_TITLE, window_width, window_height,
            window_flags);
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
        if (!s_window && tried_gl_window) {
            write_log(_T("OpenGL shader pipeline: window creation failed: %s\n"), SDL_GetError());
            s_gl_failed = true;
            s_window = SDL_CreateWindow(WINUAE_UNIX_WINDOW_TITLE, window_width, window_height,
                base_window_flags);
        }
#endif
        if (!s_window) {
            write_log(_T("SDL3: failed to create window: %s\n"), SDL_GetError());
            s_available = false;
            return false;
        }
        center_window_on_display(get_sdl_display_for_index(s_requested_display_index));
    }

#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
    if (!s_gl_failed) {
        int window_width = 0;
        int window_height = 0;
        SDL_GetWindowSizeInPixels(s_window, &window_width, &window_height);
        if (unix_gl_ensure_context(window_width, window_height)) {
            apply_window_mode();
            return unix_gl_ensure_textures(width, height, pixbytes, 1);
        }
        if (!s_renderer && s_window) {
            SDL_DestroyWindow(s_window);
            s_window = NULL;
            return unix_video_init(width, height, pixbytes);
        }
    }
    if (s_gl_active) {
        apply_window_mode();
        return unix_gl_ensure_textures(width, height, pixbytes, 1);
    }
#endif

    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, NULL);
        if (!s_renderer) {
            s_renderer = SDL_CreateRenderer(s_window, "software");
        }
        if (!s_renderer) {
            write_log(_T("SDL3: failed to create renderer: %s\n"), SDL_GetError());
            s_available = false;
            return false;
        }
        SDL_SetRenderVSync(s_renderer, 1);
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
        SDL_RenderClear(s_renderer);
        SDL_RenderPresent(s_renderer);
    }

    apply_window_mode();

    return ensure_texture(width, height, pixbytes, 1);
}

void unix_video_shutdown(void)
{
    unix_input_release_keys();
    unix_video_set_mouse_grab(false);

    destroy_frame_textures();
    if (s_status_texture) {
        SDL_DestroyTexture(s_status_texture);
        s_status_texture = NULL;
    }
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
    unix_gl_destroy_status_texture();
    if (s_gl_program) {
        p_glDeleteProgram(s_gl_program);
        s_gl_program = 0;
    }
    if (s_gl_context) {
        SDL_GL_DestroyContext(s_gl_context);
        s_gl_context = NULL;
    }
    s_gl_active = false;
#endif
    s_status_pixels.clear();
    if (s_renderer) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    s_status_width = 0;
    s_status_height = 0;
    s_auto_window_width = 0;
    s_auto_window_height = 0;
    s_event_thread_valid = false;
    s_wrong_event_thread_logged = false;
    s_queued_present_logged = false;
    s_suppressed_mouse_buttons = 0;
    {
        std::lock_guard<std::mutex> lock(s_pending_frame_mutex);
        s_pending_frame = unix_pending_video_frame();
    }

    if (s_setup_done && s_available) {
        SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO);
    }
    s_setup_done = false;
    s_available = false;
}

static int unix_video_poll_internal(bool *quit_requested, bool input_events)
{
    SDL_Event event;
    int got = 0;

    if (quit_requested) {
        *quit_requested = false;
    }
    if (!s_setup_done || !s_available) {
        return 0;
    }
    if (s_event_thread_valid.load() && !pthread_equal(uae_thread_get_id(), s_event_thread)) {
        if (!s_wrong_event_thread_logged.exchange(true)) {
            write_log(_T("SDL3: ignoring event poll from non-video thread\n"));
        }
        return 0;
    }

    unix_pending_video_frame pending;
    if (pop_queued_video_frame(&pending)) {
        struct unix_video_frame frame;
        frame.pixels = pending.pixels.data();
        frame.width = pending.width;
        frame.height = pending.height;
        frame.rowbytes = pending.rowbytes;
        frame.pixbytes = pending.pixbytes;
        frame.filter_index = pending.filter_index;
        frame.monitor_id = pending.monitor_id;
        frame.backbuffers = pending.backbuffers;
        unix_video_present_on_event_thread(&frame);
        got = 1;
    }

    while (SDL_PollEvent(&event)) {
        got = 1;
        switch (event.type) {
        case SDL_EVENT_QUIT:
            if (quit_requested) {
                *quit_requested = true;
            }
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (quit_requested) {
                *quit_requested = true;
            }
            break;
        case SDL_EVENT_WINDOW_MOVED:
            if (s_mouse_grabbed && unix_event_for_window(event.window.windowID)) {
                unix_input_release_keys();
                unix_video_set_mouse_grab(false);
                s_suppressed_mouse_buttons = 0;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            unix_input_release_keys();
            unix_video_set_mouse_grab(false);
            s_suppressed_mouse_buttons = 0;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!input_events) {
                break;
            }
            if (event.key.repeat) {
                break;
            }
            if (event.key.key == SDLK_Q && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                if (quit_requested) {
                    *quit_requested = true;
                }
                break;
            }
            if (event.key.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_G &&
                (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                unix_video_set_mouse_grab(false);
                break;
            }
            if (event.key.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && s_mouse_grabbed) {
                unix_video_set_mouse_grab(false);
            }
            unix_input_keyboard_key((int)event.key.scancode, event.key.type == SDL_EVENT_KEY_DOWN,
                unix_input_lock_state_from_sdl(event.key.mod));
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (!input_events) {
                break;
            }
            if (s_suppressed_mouse_buttons) {
                s_suppressed_mouse_buttons &= event.motion.state;
                break;
            }
            if (s_mouse_grabbed) {
                unix_input_mouse_motion((int)event.motion.xrel, (int)event.motion.yrel);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (!input_events) {
                break;
            }
            SDL_MouseButtonFlags button_mask = unix_mouse_button_mask(event.button.button);
            if (button_mask && (s_suppressed_mouse_buttons & button_mask)) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    s_suppressed_mouse_buttons &= ~button_mask;
                }
                break;
            }
            if (!s_mouse_grabbed &&
                !unix_mouse_event_in_client(event.button.windowID, event.button.x, event.button.y)) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && button_mask) {
                    s_suppressed_mouse_buttons |= button_mask;
                }
                break;
            }
            int button = unix_mouse_button_from_sdl(event.button.button);
            if (button >= 0) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && handle_statusbar_click((int)event.button.x, (int)event.button.y, event.button.button)) {
                    s_status_click_button = event.button.button;
                    break;
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && s_status_click_button == event.button.button) {
                    s_status_click_button = 0;
                    break;
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !s_mouse_grabbed) {
                    unix_video_set_mouse_grab(true);
                }
                unix_input_mouse_button(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (!input_events) {
                break;
            }
            if (!s_mouse_grabbed &&
                !unix_mouse_event_in_client(event.wheel.windowID, event.wheel.mouse_x, event.wheel.mouse_y)) {
                break;
            }
            if (s_mouse_grabbed) {
                unix_input_mouse_wheel(event.wheel.integer_x, event.wheel.integer_y);
            }
            break;
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        case SDL_EVENT_GAMEPAD_REMAPPED:
            unix_input_joystick_device_changed();
            break;
        case SDL_EVENT_CLIPBOARD_UPDATE:
            clipboard_host_changed();
            break;
        default:
            break;
        }
    }

    return got;
}

int unix_video_poll(bool *quit_requested)
{
    return unix_video_poll_internal(quit_requested, true);
}

int unix_video_poll_window_events(bool *quit_requested)
{
    return unix_video_poll_internal(quit_requested, false);
}

static void unix_video_present_on_event_thread(const struct unix_video_frame *frame)
{
    if (!frame || !frame->pixels || frame->width <= 0 || frame->height <= 0 || frame->rowbytes <= 0) {
        return;
    }
    if (!unix_video_init(frame->width, frame->height, frame->pixbytes)) {
        return;
    }
    const struct gfx_filterdata *filter = filterdata_for_frame(frame);
    auto_resize_window_for_rtg(frame, filter);
#ifdef WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE
    if (s_gl_active) {
        unix_gl_present(frame, filter);
        return;
    }
#endif
    if (!ensure_texture(frame->width, frame->height, frame->pixbytes, frame->backbuffers)) {
        return;
    }
    if (!s_textures.empty()) {
        s_texture_index = (s_texture_index + 1) % (int)s_textures.size();
        s_texture = s_textures[s_texture_index];
    }

    SDL_SetTextureScaleMode(s_texture,
        filter && filter->gfx_filter_bilinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    SDL_UpdateTexture(s_texture, NULL, frame->pixels, frame->rowbytes);
    SDL_SetRenderLogicalPresentation(s_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    int output_width = 0;
    int output_height = 0;
    struct unix_video_layout layout;
    if (!get_renderer_output_size(&output_width, &output_height) ||
        !make_video_layout(frame, filter, output_width, output_height, &layout)) {
        return;
    }
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);

    SDL_SetRenderClipRect(s_renderer, &layout.frame_clip);
    SDL_RenderTexture(s_renderer, s_texture, NULL, &layout.frame_dst);
    render_scanline_overlay(filter, &layout.frame_dst);
    SDL_SetRenderClipRect(s_renderer, NULL);

    if (update_status_texture(frame->width)) {
        SDL_RenderTexture(s_renderer, s_status_texture, NULL, &layout.status_dst);
    }
    SDL_RenderPresent(s_renderer);
}

void unix_video_present(const struct unix_video_frame *frame)
{
    if (!unix_video_on_event_thread()) {
        queue_video_frame_for_event_thread(frame);
        return;
    }
    unix_video_present_on_event_thread(frame);
}

void unix_video_set_title(const TCHAR *title)
{
    if (s_window && title) {
        SDL_SetWindowTitle(s_window, title);
    }
}

bool unix_video_set_window_mode(enum unix_video_window_mode mode, int display_index, int width, int height, int refresh_rate)
{
    s_requested_window_mode = mode;
    s_requested_display_index = display_index;
    s_requested_fullscreen_width = width;
    s_requested_fullscreen_height = height;
    s_requested_fullscreen_refresh = refresh_rate;

    if (!s_window) {
        return true;
    }
    return apply_window_mode();
}

enum unix_video_window_mode unix_video_get_window_mode(void)
{
    return s_active_window_mode;
}

float unix_video_get_display_refresh_rate(int display_index)
{
    if (!unix_video_setup()) {
        return 0.0f;
    }

    SDL_DisplayID display = 0;
    if (display_index < 0 && s_window) {
        display = SDL_GetDisplayForWindow(s_window);
    }
    if (!display) {
        display = get_sdl_display_for_index(display_index);
    }

    const SDL_DisplayMode *mode = display ? SDL_GetCurrentDisplayMode(display) : NULL;
    float refresh = refresh_rate_from_mode(mode);
    if (refresh <= 0.0f && display) {
        refresh = refresh_rate_from_mode(SDL_GetDesktopDisplayMode(display));
    }
    return refresh;
}

int unix_video_get_display_modes(int display_index, struct unix_video_display_mode *modes, int max_modes)
{
    if (!modes || max_modes <= 0 || !unix_video_setup()) {
        return 0;
    }

    int count = 0;
    int display_count = 0;
    SDL_DisplayID *displays = get_sdl_displays(&display_count);
    if (displays && display_count > 0) {
        if (display_index > 0 && display_index <= display_count) {
            add_sdl_display_modes(displays[display_index - 1], modes, max_modes, &count);
        } else {
            for (int i = 0; i < display_count; i++) {
                add_sdl_display_modes(displays[i], modes, max_modes, &count);
            }
        }
    }
    if (displays) {
        SDL_free(displays);
    }
    if (!count) {
        add_sdl_display_modes(SDL_GetPrimaryDisplay(), modes, max_modes, &count);
    }
    return count;
}

void unix_video_set_mouse_grab(bool grab)
{
    s_mouse_grabbed = grab;
    unix_input_set_mouse_active(grab);
    if (grab) {
        s_suppressed_mouse_buttons = 0;
    }

    if (!s_setup_done || !s_available) {
        return;
    }

    if (s_window) {
        SDL_SetWindowRelativeMouseMode(s_window, grab);
        SDL_SetWindowMouseGrab(s_window, grab);
        SDL_CaptureMouse(grab);
    }
}

bool unix_video_get_mouse_grab(void)
{
    return s_mouse_grabbed;
}

void unix_video_toggle_mouse_grab(void)
{
    unix_video_set_mouse_grab(!unix_video_get_mouse_grab());
}

void unix_video_get_desktop(int *dw, int *dh, int *x, int *y, int *w, int *h)
{
    SDL_Rect usable;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();

    if (x) {
        *x = 0;
    }
    if (y) {
        *y = 0;
    }
    const SDL_DisplayMode *mode = display ? SDL_GetCurrentDisplayMode(display) : NULL;
    if (mode) {
        if (dw) {
            *dw = mode->w;
        }
        if (dh) {
            *dh = mode->h;
        }
    } else {
        if (dw) {
            *dw = 640;
        }
        if (dh) {
            *dh = 480;
        }
    }

    if (display && SDL_GetDisplayUsableBounds(display, &usable)) {
        if (x) {
            *x = usable.x;
        }
        if (y) {
            *y = usable.y;
        }
        if (w) {
            *w = usable.w;
        }
        if (h) {
            *h = usable.h;
        }
    } else {
        if (w) {
            *w = dw ? *dw : 640;
        }
        if (h) {
            *h = dh ? *dh : 480;
        }
    }
}
