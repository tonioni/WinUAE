#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "videograb.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef UAE_UNIX_WITH_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_surface.h>
#endif

#define BI_RGB 0

struct AviFrame {
    char id[4];
    long data_pos;
    uae_u32 size;
};

struct AviIndexEntry {
    char id[4];
    uae_u32 offset;
    uae_u32 size;
};

struct AviInfo {
    int stream_count = 0;
    int video_stream = -1;
    int width = 0;
    int height = 0;
    bool top_down = false;
    uae_u32 microsec_per_frame = 40000;
    uae_u32 stream_scale = 0;
    uae_u32 stream_rate = 0;
    long movi_data_start = 0;
    long movi_data_end = 0;
    std::vector<AviFrame> frames;
    std::vector<AviIndexEntry> index;
};

enum VideoGrabMode {
    VIDEOGRAB_NONE,
    VIDEOGRAB_AVI,
    VIDEOGRAB_CAMERA
};

static VideoGrabMode videograb_mode = VIDEOGRAB_NONE;
static FILE *avi_file;
static std::vector<AviFrame> avi_frames;
static std::vector<long> frame_buffer;
static int video_width;
static int video_height;
static int video_paused = -1;
static int audio_chflags;
static int audio_volume;
static uae_s64 current_frame;
static uae_s64 play_base_frame;
static uae_u32 frame_usec = 40000;
static std::chrono::steady_clock::time_point play_base_time;
static bool video_initialized;
static bool avi_top_down;
static uae_s64 loaded_frame = -1;

#ifdef UAE_UNIX_WITH_SDL3
static SDL_Camera *camera;
static bool camera_sdl_initialized;
static bool camera_permission_denied_logged;
#endif

static bool fourcc_equals(const char id[4], const char *value)
{
    return std::memcmp(id, value, 4) == 0;
}

static uae_u32 read_le32(const uae_u8 *p)
{
    return (uae_u32)p[0] |
        ((uae_u32)p[1] << 8) |
        ((uae_u32)p[2] << 16) |
        ((uae_u32)p[3] << 24);
}

static uae_s32 read_le_s32(const uae_u8 *p)
{
    return (uae_s32)read_le32(p);
}

static uae_u16 read_le16(const uae_u8 *p)
{
    return (uae_u16)(p[0] | (p[1] << 8));
}

static bool read_exact(FILE *f, void *data, size_t size)
{
    return size == 0 || std::fread(data, 1, size, f) == size;
}

static bool read_chunk_header(FILE *f, char id[4], uae_u32 *size)
{
    uae_u8 bytes[4];

    if (!read_exact(f, id, 4) || !read_exact(f, bytes, sizeof bytes)) {
        return false;
    }
    *size = read_le32(bytes);
    return true;
}

static long file_size(FILE *f)
{
    const long pos = std::ftell(f);
    if (pos < 0 || std::fseek(f, 0, SEEK_END) != 0) {
        return 0;
    }
    const long size = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);
    return size;
}

static bool seek_file(FILE *f, long pos)
{
    return pos >= 0 && std::fseek(f, pos, SEEK_SET) == 0;
}

static bool read_chunk_data(FILE *f, uae_u32 size, std::vector<uae_u8> *data)
{
    data->resize(size);
    return read_exact(f, data->data(), size);
}

static bool valid_video_chunk_id(const AviInfo &info, const char id[4])
{
    if (info.video_stream < 0) {
        return false;
    }
    if (id[0] < '0' || id[0] > '9' || id[1] < '0' || id[1] > '9') {
        return false;
    }
    const int stream = (id[0] - '0') * 10 + (id[1] - '0');
    return stream == info.video_stream && id[2] == 'd' &&
        (id[3] == 'b' || id[3] == 'c');
}

static void parse_avih(const std::vector<uae_u8> &data, AviInfo *info)
{
    if (data.size() < 20) {
        return;
    }
    const uae_u32 usec = read_le32(data.data());
    if (usec > 0) {
        info->microsec_per_frame = usec;
    }
}

static void parse_strh(const std::vector<uae_u8> &data, int stream_index, AviInfo *info)
{
    if (stream_index < 0 || data.size() < 36 || !fourcc_equals((const char *)data.data(), "vids")) {
        return;
    }
    if (info->video_stream >= 0) {
        return;
    }
    info->video_stream = stream_index;
    info->stream_scale = read_le32(data.data() + 20);
    info->stream_rate = read_le32(data.data() + 24);
}

static void parse_strf(const std::vector<uae_u8> &data, int stream_index, AviInfo *info)
{
    if (stream_index != info->video_stream || data.size() < 40) {
        return;
    }

    const uae_s32 width = read_le_s32(data.data() + 4);
    const uae_s32 signed_height = read_le_s32(data.data() + 8);
    const uae_u16 planes = read_le16(data.data() + 12);
    const uae_u16 bit_count = read_le16(data.data() + 14);
    const uae_u32 compression = read_le32(data.data() + 16);
    if (width <= 0 || signed_height == 0 || planes != 1 ||
        bit_count != 24 || compression != BI_RGB) {
        return;
    }

    info->width = width;
    info->height = signed_height < 0 ? -signed_height : signed_height;
    info->top_down = signed_height < 0;
}

static void parse_idx1(const std::vector<uae_u8> &data, AviInfo *info)
{
    const size_t count = data.size() / 16;
    for (size_t i = 0; i < count; i++) {
        const uae_u8 *entry = data.data() + i * 16;
        AviIndexEntry idx;
        std::memcpy(idx.id, entry, 4);
        idx.offset = read_le32(entry + 8);
        idx.size = read_le32(entry + 12);
        info->index.push_back(idx);
    }
}

static bool parse_avi_range(FILE *f, long end, int stream_index, AviInfo *info)
{
    while (std::ftell(f) >= 0 && std::ftell(f) + 8 <= end) {
        char id[4];
        uae_u32 size;
        const long chunk_start = std::ftell(f);
        if (!read_chunk_header(f, id, &size)) {
            return false;
        }

        const long data_start = std::ftell(f);
        const long chunk_end = data_start + (long)size + (size & 1);
        if (chunk_end < data_start || chunk_end > end + 1) {
            return false;
        }

        if (fourcc_equals(id, "LIST") && size >= 4) {
            char list_type[4];
            if (!read_exact(f, list_type, sizeof list_type)) {
                return false;
            }
            const long list_data_start = std::ftell(f);
            const long list_data_end = data_start + (long)size;
            if (fourcc_equals(list_type, "strl")) {
                const int child_stream = info->stream_count++;
                if (!parse_avi_range(f, list_data_end, child_stream, info)) {
                    return false;
                }
            } else if (fourcc_equals(list_type, "movi")) {
                info->movi_data_start = list_data_start;
                info->movi_data_end = list_data_end;
            } else if (!parse_avi_range(f, list_data_end, stream_index, info)) {
                return false;
            }
        } else {
            std::vector<uae_u8> data;
            if (fourcc_equals(id, "avih")) {
                if (!read_chunk_data(f, size, &data)) {
                    return false;
                }
                parse_avih(data, info);
            } else if (fourcc_equals(id, "strh")) {
                if (!read_chunk_data(f, size, &data)) {
                    return false;
                }
                parse_strh(data, stream_index, info);
            } else if (fourcc_equals(id, "strf")) {
                if (!read_chunk_data(f, size, &data)) {
                    return false;
                }
                parse_strf(data, stream_index, info);
            } else if (fourcc_equals(id, "idx1")) {
                if (!read_chunk_data(f, size, &data)) {
                    return false;
                }
                parse_idx1(data, info);
            }
        }

        if (!seek_file(f, chunk_end)) {
            return false;
        }
        if (std::ftell(f) <= chunk_start) {
            return false;
        }
    }
    return true;
}

static bool indexed_chunk_data_pos(FILE *f, const AviInfo &info,
    const AviIndexEntry &idx, long *data_pos)
{
    const long bases[] = {
        info.movi_data_start,
        info.movi_data_start - 4,
        0
    };

    for (long base : bases) {
        const long chunk_pos = base + (long)idx.offset;
        if (!seek_file(f, chunk_pos)) {
            continue;
        }
        char id[4];
        uae_u32 size;
        if (read_chunk_header(f, id, &size) &&
            std::memcmp(id, idx.id, 4) == 0 && size >= idx.size) {
            *data_pos = chunk_pos + 8;
            return true;
        }
    }
    return false;
}

static void build_indexed_frames(FILE *f, AviInfo *info)
{
    for (const AviIndexEntry &idx : info->index) {
        if (!valid_video_chunk_id(*info, idx.id) || idx.size == 0) {
            continue;
        }
        long data_pos;
        if (!indexed_chunk_data_pos(f, *info, idx, &data_pos)) {
            continue;
        }
        AviFrame frame;
        std::memcpy(frame.id, idx.id, 4);
        frame.data_pos = data_pos;
        frame.size = idx.size;
        info->frames.push_back(frame);
    }
}

static bool scan_movi_frames(FILE *f, AviInfo *info, long start, long end)
{
    if (!seek_file(f, start)) {
        return false;
    }

    while (std::ftell(f) >= 0 && std::ftell(f) + 8 <= end) {
        char id[4];
        uae_u32 size;
        const long chunk_start = std::ftell(f);
        if (!read_chunk_header(f, id, &size)) {
            return false;
        }
        const long data_start = std::ftell(f);
        const long chunk_end = data_start + (long)size + (size & 1);
        if (chunk_end < data_start || chunk_end > end + 1) {
            return false;
        }
        if (fourcc_equals(id, "LIST") && size >= 4) {
            char list_type[4];
            if (!read_exact(f, list_type, sizeof list_type)) {
                return false;
            }
            if (fourcc_equals(list_type, "rec ")) {
                const long list_data_start = std::ftell(f);
                const long list_data_end = data_start + (long)size;
                if (!scan_movi_frames(f, info, list_data_start, list_data_end)) {
                    return false;
                }
            }
        } else if (valid_video_chunk_id(*info, id) && size > 0) {
            AviFrame frame;
            std::memcpy(frame.id, id, 4);
            frame.data_pos = data_start;
            frame.size = size;
            info->frames.push_back(frame);
        }
        if (!seek_file(f, chunk_end)) {
            return false;
        }
        if (std::ftell(f) <= chunk_start) {
            return false;
        }
    }
    return true;
}

static bool parse_avi_file(FILE *f, AviInfo *info)
{
    char id[4];
    uae_u32 riff_size;
    char type[4];

    if (!seek_file(f, 0) || !read_chunk_header(f, id, &riff_size) ||
        !fourcc_equals(id, "RIFF") || !read_exact(f, type, sizeof type) ||
        !fourcc_equals(type, "AVI ")) {
        return false;
    }

    const long end = std::min(file_size(f), (long)riff_size + 8);
    if (end <= 12 || !parse_avi_range(f, end, -1, info)) {
        return false;
    }

    if (info->stream_rate > 0 && info->stream_scale > 0) {
        const double usec = 1000000.0 * (double)info->stream_scale /
            (double)info->stream_rate;
        if (usec >= 1.0) {
            info->microsec_per_frame = (uae_u32)usec;
        }
    }

    if (!info->index.empty()) {
        build_indexed_frames(f, info);
    }
    if (info->frames.empty() && info->movi_data_start > 0 &&
        info->movi_data_end > info->movi_data_start) {
        scan_movi_frames(f, info, info->movi_data_start, info->movi_data_end);
    }

    return info->width > 0 && info->height > 0 && !info->frames.empty();
}

static uae_u8 *frame_bytes(void)
{
    return reinterpret_cast<uae_u8 *>(frame_buffer.data());
}

static bool resize_frame_buffer(size_t bytes)
{
    const size_t longs = (bytes + sizeof(long) - 1) / sizeof(long);
    try {
        frame_buffer.resize(longs);
    } catch (...) {
        frame_buffer.clear();
        return false;
    }
    return true;
}

static uae_s64 normalized_frame(uae_s64 frame)
{
    const uae_s64 count = (uae_s64)avi_frames.size();
    if (count <= 0) {
        return 0;
    }
    frame %= count;
    if (frame < 0) {
        frame += count;
    }
    return frame;
}

static uae_s64 current_avi_frame(void)
{
    if (video_paused > 0) {
        return normalized_frame(current_frame);
    }

    const auto now = std::chrono::steady_clock::now();
    const uae_s64 elapsed = (uae_s64)std::chrono::duration_cast
        <std::chrono::microseconds>(now - play_base_time).count();
    const uae_s64 delta = frame_usec > 0 ? elapsed / frame_usec : 0;
    current_frame = normalized_frame(play_base_frame + delta);
    return current_frame;
}

static bool read_avi_frame(uae_s64 frame)
{
    if (!avi_file || avi_frames.empty() || video_width <= 0 || video_height <= 0) {
        return false;
    }

    frame = normalized_frame(frame);
    if (loaded_frame == frame && !frame_buffer.empty()) {
        return true;
    }

    const AviFrame &avi_frame = avi_frames[(size_t)frame];
    std::vector<uae_u8> data(avi_frame.size);
    if (!seek_file(avi_file, avi_frame.data_pos) ||
        !read_exact(avi_file, data.data(), data.size())) {
        write_log(_T("VIDEOGRAB: failed reading AVI frame %lld\n"), frame);
        return false;
    }

    const size_t row_bytes = (size_t)video_width * 3;
    const size_t padded_row_bytes = (row_bytes + 3) & ~(size_t)3;
    size_t source_pitch = padded_row_bytes;
    if (data.size() < source_pitch * (size_t)video_height) {
        if (data.size() < row_bytes * (size_t)video_height) {
            write_log(_T("VIDEOGRAB: short AVI frame %lld\n"), frame);
            return false;
        }
        source_pitch = row_bytes;
    }

    if (!resize_frame_buffer(row_bytes * (size_t)video_height)) {
        return false;
    }

    uae_u8 *dst = frame_bytes();
    for (int y = 0; y < video_height; y++) {
        const int source_y = avi_top_down ? video_height - 1 - y : y;
        const uae_u8 *src = data.data() + (size_t)source_y * source_pitch;
        std::memcpy(dst + (size_t)y * row_bytes, src, row_bytes);
    }

    loaded_frame = frame;
    return true;
}

static bool init_avi_videograb(const TCHAR *filename)
{
    AviInfo info;

    avi_file = std::fopen(filename, "rb");
    if (!avi_file) {
        write_log(_T("VIDEOGRAB: can't open '%s'\n"), filename);
        return false;
    }

    if (!parse_avi_file(avi_file, &info)) {
        write_log(_T("VIDEOGRAB: unsupported AVI '%s' (need 24-bit uncompressed video)\n"),
            filename);
        std::fclose(avi_file);
        avi_file = NULL;
        return false;
    }

    avi_frames = info.frames;
    video_width = info.width;
    video_height = info.height;
    avi_top_down = info.top_down;
    frame_usec = info.microsec_per_frame ? info.microsec_per_frame : 40000;
    current_frame = 0;
    play_base_frame = 0;
    play_base_time = std::chrono::steady_clock::now();
    video_paused = 0;
    loaded_frame = -1;

    write_log(_T("VIDEOGRAB: playing '%s', %dx%d, %d frames, %.3f fps\n"),
        filename, video_width, video_height, (int)avi_frames.size(),
        1000000.0 / (double)frame_usec);
    return true;
}

#ifdef UAE_UNIX_WITH_SDL3
static bool init_camera_videograb(void)
{
    if (!(SDL_WasInit(SDL_INIT_CAMERA) & SDL_INIT_CAMERA)) {
        if (!SDL_InitSubSystem(SDL_INIT_CAMERA)) {
            write_log(_T("VIDEOGRAB: SDL camera init failed: %s\n"), SDL_GetError());
            return false;
        }
        camera_sdl_initialized = true;
    }

    int count = 0;
    SDL_CameraID *cameras = SDL_GetCameras(&count);
    if (!cameras || count <= 0) {
        write_log(_T("VIDEOGRAB: no SDL camera devices found: %s\n"), SDL_GetError());
        if (cameras) {
            SDL_free(cameras);
        }
        return false;
    }

    const SDL_CameraID camera_id = cameras[0];
    const char *camera_name = SDL_GetCameraName(camera_id);
    camera = SDL_OpenCamera(camera_id, NULL);
    SDL_free(cameras);
    if (!camera) {
        write_log(_T("VIDEOGRAB: SDL_OpenCamera failed: %s\n"), SDL_GetError());
        return false;
    }

    video_width = 0;
    video_height = 0;
    video_paused = 0;
    current_frame = 0;
    loaded_frame = -1;
    camera_permission_denied_logged = false;
    write_log(_T("VIDEOGRAB: opened camera '%s'\n"),
        camera_name ? camera_name : _T("<unknown>"));
    return true;
}

static bool copy_camera_surface(SDL_Surface *surface)
{
    if (!surface || surface->w <= 0 || surface->h <= 0 || !surface->pixels) {
        return false;
    }

    SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_BGR24);
    if (!converted) {
        write_log(_T("VIDEOGRAB: SDL_ConvertSurface failed: %s\n"), SDL_GetError());
        return false;
    }

    const size_t row_bytes = (size_t)converted->w * 3;
    if (!resize_frame_buffer(row_bytes * (size_t)converted->h)) {
        SDL_DestroySurface(converted);
        return false;
    }

    const uae_u8 *src_pixels = (const uae_u8 *)converted->pixels;
    uae_u8 *dst_pixels = frame_bytes();
    for (int y = 0; y < converted->h; y++) {
        const uae_u8 *src = src_pixels + (size_t)y * converted->pitch;
        uae_u8 *dst = dst_pixels + (size_t)(converted->h - 1 - y) * row_bytes;
        std::memcpy(dst, src, row_bytes);
    }

    video_width = converted->w;
    video_height = converted->h;
    SDL_DestroySurface(converted);
    return true;
}

static bool get_camera_videograb(long **buffer, int *width, int *height)
{
    if (!camera) {
        return false;
    }
    if (video_paused > 0 && !frame_buffer.empty()) {
        *buffer = frame_buffer.data();
        *width = video_width;
        *height = video_height;
        return true;
    }

    const SDL_CameraPermissionState permission =
        SDL_GetCameraPermissionState(camera);
    if (permission == SDL_CAMERA_PERMISSION_STATE_DENIED) {
        if (!camera_permission_denied_logged) {
            write_log(_T("VIDEOGRAB: camera permission denied\n"));
            camera_permission_denied_logged = true;
        }
        return false;
    }

    Uint64 timestamp = 0;
    SDL_Surface *surface = SDL_AcquireCameraFrame(camera, &timestamp);
    if (surface) {
        const bool copied = copy_camera_surface(surface);
        SDL_ReleaseCameraFrame(camera, surface);
        if (!copied) {
            return false;
        }
        current_frame++;
    }

    if (frame_buffer.empty()) {
        return false;
    }

    *buffer = frame_buffer.data();
    *width = video_width;
    *height = video_height;
    return true;
}
#else
static bool init_camera_videograb(void)
{
    write_log(_T("VIDEOGRAB: capture device support requires SDL3\n"));
    return false;
}

static bool get_camera_videograb(long **, int *, int *)
{
    return false;
}
#endif

void uninitvideograb(void)
{
#ifdef UAE_UNIX_WITH_SDL3
    if (camera) {
        SDL_CloseCamera(camera);
        camera = NULL;
    }
    if (camera_sdl_initialized) {
        SDL_QuitSubSystem(SDL_INIT_CAMERA);
        camera_sdl_initialized = false;
    }
#endif
    if (avi_file) {
        std::fclose(avi_file);
        avi_file = NULL;
    }
    avi_frames.clear();
    frame_buffer.clear();
    videograb_mode = VIDEOGRAB_NONE;
    video_initialized = false;
    video_paused = -1;
    video_width = 0;
    video_height = 0;
    current_frame = 0;
    play_base_frame = 0;
    frame_usec = 40000;
    loaded_frame = -1;
    audio_chflags = 0;
    audio_volume = 0;
}

bool initvideograb(const TCHAR *filename)
{
    uninitvideograb();

    if (!filename || !filename[0]) {
        if (!init_camera_videograb()) {
            uninitvideograb();
            return false;
        }
        videograb_mode = VIDEOGRAB_CAMERA;
    } else {
        if (!init_avi_videograb(filename)) {
            uninitvideograb();
            return false;
        }
        videograb_mode = VIDEOGRAB_AVI;
    }

    video_initialized = true;
    return true;
}

bool getvideograb(long **buffer, int *width, int *height)
{
    if (!video_initialized || !buffer || !width || !height) {
        return false;
    }

    if (videograb_mode == VIDEOGRAB_CAMERA) {
        return get_camera_videograb(buffer, width, height);
    }

    if (videograb_mode != VIDEOGRAB_AVI) {
        return false;
    }

    const uae_s64 frame = current_avi_frame();
    if (!read_avi_frame(frame)) {
        return false;
    }

    *buffer = frame_buffer.data();
    *width = video_width;
    *height = video_height;
    return true;
}

void pausevideograb(int pause)
{
    if (!video_initialized) {
        return;
    }
    if (pause < 0) {
        pause = video_paused > 0 ? 0 : 1;
    }
    if (video_paused == pause) {
        return;
    }

    if (videograb_mode == VIDEOGRAB_AVI) {
        if (pause > 0) {
            current_frame = current_avi_frame();
        } else {
            play_base_frame = current_frame;
            play_base_time = std::chrono::steady_clock::now();
        }
    }
    video_paused = pause > 0 ? 1 : 0;
}

uae_s64 getsetpositionvideograb(uae_s64 framepos)
{
    if (!video_initialized) {
        return 0;
    }

    if (framepos < 0) {
        if (videograb_mode == VIDEOGRAB_AVI) {
            return current_avi_frame();
        }
        return current_frame;
    }

    if (videograb_mode == VIDEOGRAB_AVI) {
        current_frame = normalized_frame(framepos);
        play_base_frame = current_frame;
        play_base_time = std::chrono::steady_clock::now();
        loaded_frame = -1;
        return current_frame;
    }

    current_frame = framepos;
    return current_frame;
}

uae_s64 getdurationvideograb(void)
{
    if (!video_initialized) {
        return 0;
    }
    if (videograb_mode == VIDEOGRAB_AVI) {
        return (uae_s64)avi_frames.size();
    }
    return 0;
}

bool isvideograb(void)
{
    return video_initialized;
}

bool getpausevideograb(void)
{
    return video_paused > 0;
}

void setvolumevideograb(int volume)
{
    audio_volume = volume;
}

void setchflagsvideograb(int chflags, bool)
{
    audio_chflags = chflags;
}

void isvideograb_status(void)
{
    if (!video_initialized) {
        return;
    }
    if (currprefs.sound_volume_genlock != changed_prefs.sound_volume_genlock) {
        currprefs.sound_volume_genlock = changed_prefs.sound_volume_genlock;
        setvolumevideograb(100 - currprefs.sound_volume_genlock);
        setchflagsvideograb(audio_chflags, false);
    }
}
