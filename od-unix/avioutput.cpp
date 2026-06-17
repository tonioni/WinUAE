#include "sysconfig.h"
#include "sysdeps.h"

#include "audio.h"
#include "avioutput.h"
#include "custom.h"
#include "fsdb.h"
#include "options.h"
#include "uae.h"
#include "xwin.h"

#include "sounddep/sound.h"

#include <algorithm>
#include <vector>

#define AVI_HAS_INDEX 0x10

extern int video_recording_active;

struct UnixAviIndexEntry {
    char id[4];
    uae_u32 flags;
    uae_u32 offset;
    uae_u32 size;
};

static FILE *avi_file;
static FILE *wav_file;
static std::vector<UnixAviIndexEntry> avi_index;
static long riff_size_pos;
static long hdrl_size_pos;
static long movi_size_pos;
static long movi_data_start;
static long avih_total_frames_pos;
static long video_length_pos;
static long audio_length_pos;
static int avi_frame_count;
static uae_u32 avi_audio_bytes;
static int aviout_monid;
static int avioutput_fps = VBLANK_HZ_PAL;
static bool avi_header_written;
static bool avi_failed;

int avioutput_audio, avioutput_video, avioutput_enabled, avioutput_requested;
int avioutput_width, avioutput_height, avioutput_bits;
int avioutput_framelimiter = 0, avioutput_nosoundoutput = 0;
int avioutput_nosoundsync = 1, avioutput_originalsize = 0;

TCHAR avioutput_filename_gui[MAX_DPATH];
TCHAR avioutput_filename_auto[MAX_DPATH];
TCHAR avioutput_filename_inuse[MAX_DPATH];

static void put_u16(FILE *f, uae_u16 value)
{
    fputc(value & 0xff, f);
    fputc((value >> 8) & 0xff, f);
}

static void put_u32(FILE *f, uae_u32 value)
{
    fputc(value & 0xff, f);
    fputc((value >> 8) & 0xff, f);
    fputc((value >> 16) & 0xff, f);
    fputc((value >> 24) & 0xff, f);
}

static void put_fourcc(FILE *f, const char id[4])
{
    fwrite(id, 1, 4, f);
}

static void patch_u32(FILE *f, long pos, uae_u32 value)
{
    long current = ftell(f);
    fseek(f, pos, SEEK_SET);
    put_u32(f, value);
    fseek(f, current, SEEK_SET);
}

static bool unix_avi_ensure_parent_dir(const TCHAR *filename)
{
    TCHAR path[MAX_DPATH];

    if (!filename || !filename[0]) {
        return false;
    }
    _tcsncpy(path, filename, sizeof path / sizeof(TCHAR) - 1);
    path[sizeof path / sizeof(TCHAR) - 1] = 0;

    TCHAR *slash = _tcsrchr(path, '/');
    if (!slash) {
        return true;
    }
    *slash = 0;
    if (!path[0] || my_existsdir(path)) {
        return true;
    }

    for (TCHAR *p = path + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = 0;
        if (path[0] && !my_existsdir(path) && my_mkdir(path) < 0) {
            *p = '/';
            return false;
        }
        *p = '/';
    }
    return my_existsdir(path) || my_mkdir(path) == 0;
}

static void unix_avi_default_filename(void)
{
    if (avioutput_filename_gui[0]) {
        return;
    }
    TCHAR path[MAX_DPATH];
    fetch_videopath(path, sizeof path / sizeof(TCHAR));
    _sntprintf(avioutput_filename_gui, sizeof avioutput_filename_gui / sizeof(TCHAR),
        _T("%soutput.avi"), path);
    avioutput_filename_gui[sizeof avioutput_filename_gui / sizeof(TCHAR) - 1] = 0;
}

static void unix_avi_prepare_filename(void)
{
    unix_avi_default_filename();
    _tcsncpy(avioutput_filename_inuse, avioutput_filename_gui,
        sizeof avioutput_filename_inuse / sizeof(TCHAR) - 1);
    avioutput_filename_inuse[sizeof avioutput_filename_inuse / sizeof(TCHAR) - 1] = 0;

    const TCHAR *wanted = avioutput_audio == AVIAUDIO_WAV ? _T(".wav") : _T(".avi");
    TCHAR *dot = _tcsrchr(avioutput_filename_inuse, '.');
    if (!dot) {
        _tcsncat(avioutput_filename_inuse, wanted,
            sizeof avioutput_filename_inuse / sizeof(TCHAR) - _tcslen(avioutput_filename_inuse) - 1);
    } else if (_tcsicmp(dot, wanted)) {
        _tcscpy(dot, wanted);
    }
    _tcsncpy(avioutput_filename_auto, avioutput_filename_inuse,
        sizeof avioutput_filename_auto / sizeof(TCHAR) - 1);
    avioutput_filename_auto[sizeof avioutput_filename_auto / sizeof(TCHAR) - 1] = 0;
}

static int unix_avi_channels(void)
{
    int channels = get_audio_nativechannels(active_sound_stereo);
    if (channels <= 0) {
        channels = 2;
    }
    return channels;
}

static int unix_avi_sample_rate(void)
{
    return currprefs.sound_freq > 0 ? currprefs.sound_freq : 44100;
}

static void write_wave_header(FILE *f, uae_u32 data_size)
{
    int channels = unix_avi_channels();
    int sample_rate = unix_avi_sample_rate();
    int block_align = channels * 2;
    int byte_rate = sample_rate * block_align;

    put_fourcc(f, "RIFF");
    put_u32(f, 36 + data_size);
    put_fourcc(f, "WAVE");
    put_fourcc(f, "fmt ");
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, channels);
    put_u32(f, sample_rate);
    put_u32(f, byte_rate);
    put_u16(f, block_align);
    put_u16(f, 16);
    put_fourcc(f, "data");
    put_u32(f, data_size);
}

static bool start_wav_if_needed(void)
{
    if (wav_file) {
        return true;
    }

    unix_avi_prepare_filename();
    if (!unix_avi_ensure_parent_dir(avioutput_filename_inuse)) {
        write_log(_T("AVIOutput: can't create output directory for '%s'\n"), avioutput_filename_inuse);
        avi_failed = true;
        return false;
    }

    wav_file = _tfopen(avioutput_filename_inuse, _T("wb"));
    if (!wav_file) {
        write_log(_T("AVIOutput: can't open '%s'\n"), avioutput_filename_inuse);
        avi_failed = true;
        return false;
    }
    write_wave_header(wav_file, 0);
    avi_audio_bytes = 0;
    write_log(_T("Wave output to '%s' started\n"), avioutput_filename_inuse);
    return true;
}

static bool prepare_video_frame(int monid, std::vector<uae_u8> *frame, int *widthp, int *heightp)
{
    if (!frame || !widthp || !heightp) {
        return false;
    }
    if (monid < 0 || monid >= MAX_AMIGADISPLAYS) {
        monid = 0;
    }

    struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
    struct vidbuffer *vb = vidinfo->inbuffer ? vidinfo->inbuffer : &vidinfo->drawbuffer;
    if (!vb || !vb->bufmem || vb->outwidth <= 0 || vb->outheight <= 0 || vb->rowbytes <= 0) {
        return false;
    }
    if (vb->pixbytes != 4 && vb->pixbytes != 2) {
        return false;
    }

    int width = avioutput_originalsize || currprefs.aviout_width <= 0 ? vb->outwidth : currprefs.aviout_width;
    int height = avioutput_originalsize || currprefs.aviout_height <= 0 ? vb->outheight : currprefs.aviout_height;
    int src_x = avioutput_originalsize || currprefs.aviout_xoffset < 0 ? 0 : currprefs.aviout_xoffset;
    int src_y = avioutput_originalsize || currprefs.aviout_yoffset < 0 ? 0 : currprefs.aviout_yoffset;
    if (src_x >= vb->outwidth || src_y >= vb->outheight) {
        return false;
    }
    width = std::min(width, vb->outwidth - src_x);
    height = std::min(height, vb->outheight - src_y);
    if (width <= 0 || height <= 0) {
        return false;
    }

    int rowbytes = (width * 3 + 3) & ~3;
    frame->assign((size_t)rowbytes * (size_t)height, 0);
    for (int y = 0; y < height; y++) {
        const uae_u8 *src = vb->bufmem + (size_t)(src_y + height - 1 - y) * (size_t)vb->rowbytes +
            (size_t)src_x * (size_t)vb->pixbytes;
        uae_u8 *dst = frame->data() + (size_t)y * (size_t)rowbytes;
        for (int x = 0; x < width; x++) {
            uae_u8 r, g, b;
            if (vb->pixbytes == 4) {
                const uae_u32 pixel = ((const uae_u32 *)src)[x];
                b = pixel & 0xff;
                g = (pixel >> 8) & 0xff;
                r = (pixel >> 16) & 0xff;
            } else {
                const uae_u16 pixel = (uae_u16)src[x * 2] | ((uae_u16)src[x * 2 + 1] << 8);
                r = (uae_u8)((((pixel >> 11) & 0x1f) * 255) / 31);
                g = (uae_u8)((((pixel >> 5) & 0x3f) * 255) / 63);
                b = (uae_u8)(((pixel & 0x1f) * 255) / 31);
            }
            dst[x * 3 + 0] = b;
            dst[x * 3 + 1] = g;
            dst[x * 3 + 2] = r;
        }
    }
    *widthp = width;
    *heightp = height;
    return true;
}

static void write_stream_header(FILE *f, const char type[4], const char handler[4],
    uae_u32 scale, uae_u32 rate, long *length_pos, uae_u32 suggested_size,
    uae_u32 sample_size, int width, int height)
{
    put_fourcc(f, "strh");
    put_u32(f, 56);
    put_fourcc(f, type);
    put_fourcc(f, handler);
    put_u32(f, 0);
    put_u16(f, 0);
    put_u16(f, 0);
    put_u32(f, 0);
    put_u32(f, scale);
    put_u32(f, rate);
    put_u32(f, 0);
    *length_pos = ftell(f);
    put_u32(f, 0);
    put_u32(f, suggested_size);
    put_u32(f, 0xffffffffu);
    put_u32(f, sample_size);
    put_u32(f, 0);
    put_u32(f, 0);
    put_u32(f, width);
    put_u32(f, height);
}

static bool start_avi_if_needed(int width, int height, int frame_size)
{
    if (avi_file) {
        return true;
    }
    if (width <= 0 || height <= 0 || frame_size <= 0) {
        return false;
    }

    unix_avi_prepare_filename();
    if (!unix_avi_ensure_parent_dir(avioutput_filename_inuse)) {
        write_log(_T("AVIOutput: can't create output directory for '%s'\n"), avioutput_filename_inuse);
        avi_failed = true;
        return false;
    }

    avi_file = _tfopen(avioutput_filename_inuse, _T("wb"));
    if (!avi_file) {
        write_log(_T("AVIOutput: can't open '%s'\n"), avioutput_filename_inuse);
        avi_failed = true;
        return false;
    }

    avi_index.clear();
    avi_frame_count = 0;
    avi_audio_bytes = 0;
    avioutput_width = width;
    avioutput_height = height;
    avioutput_bits = 24;

    int channels = unix_avi_channels();
    int sample_rate = unix_avi_sample_rate();
    int block_align = channels * 2;

    put_fourcc(avi_file, "RIFF");
    riff_size_pos = ftell(avi_file);
    put_u32(avi_file, 0);
    put_fourcc(avi_file, "AVI ");

    put_fourcc(avi_file, "LIST");
    hdrl_size_pos = ftell(avi_file);
    put_u32(avi_file, 0);
    put_fourcc(avi_file, "hdrl");

    put_fourcc(avi_file, "avih");
    put_u32(avi_file, 56);
    put_u32(avi_file, 1000000 / std::max(1, avioutput_fps));
    put_u32(avi_file, (uae_u32)frame_size * (uae_u32)std::max(1, avioutput_fps));
    put_u32(avi_file, 0);
    put_u32(avi_file, AVI_HAS_INDEX);
    avih_total_frames_pos = ftell(avi_file);
    put_u32(avi_file, 0);
    put_u32(avi_file, 0);
    put_u32(avi_file, avioutput_audio == AVIAUDIO_AVI ? 2 : 1);
    put_u32(avi_file, frame_size);
    put_u32(avi_file, width);
    put_u32(avi_file, height);
    for (int i = 0; i < 4; i++) {
        put_u32(avi_file, 0);
    }

    long video_list_start = ftell(avi_file);
    put_fourcc(avi_file, "LIST");
    long video_list_size_pos = ftell(avi_file);
    put_u32(avi_file, 0);
    put_fourcc(avi_file, "strl");
    write_stream_header(avi_file, "vids", "DIB ", 1, std::max(1, avioutput_fps),
        &video_length_pos, frame_size, 0, width, height);
    put_fourcc(avi_file, "strf");
    put_u32(avi_file, 40);
    put_u32(avi_file, width);
    put_u32(avi_file, height);
    put_u16(avi_file, 1);
    put_u16(avi_file, 24);
    put_u32(avi_file, 0);
    put_u32(avi_file, frame_size);
    put_u32(avi_file, 0);
    put_u32(avi_file, 0);
    put_u32(avi_file, 0);
    put_u32(avi_file, 0);
    patch_u32(avi_file, video_list_size_pos, (uae_u32)(ftell(avi_file) - video_list_start - 8));

    if (avioutput_audio == AVIAUDIO_AVI) {
        long audio_list_start = ftell(avi_file);
        put_fourcc(avi_file, "LIST");
        long audio_list_size_pos = ftell(avi_file);
        put_u32(avi_file, 0);
        put_fourcc(avi_file, "strl");
        write_stream_header(avi_file, "auds", "\0\0\0\0", block_align, sample_rate * block_align,
            &audio_length_pos, 0, block_align, 0, 0);
        put_fourcc(avi_file, "strf");
        put_u32(avi_file, 16);
        put_u16(avi_file, 1);
        put_u16(avi_file, channels);
        put_u32(avi_file, sample_rate);
        put_u32(avi_file, sample_rate * block_align);
        put_u16(avi_file, block_align);
        put_u16(avi_file, 16);
        patch_u32(avi_file, audio_list_size_pos, (uae_u32)(ftell(avi_file) - audio_list_start - 8));
    } else {
        audio_length_pos = 0;
    }

    patch_u32(avi_file, hdrl_size_pos, (uae_u32)(ftell(avi_file) - hdrl_size_pos - 4));

    put_fourcc(avi_file, "LIST");
    movi_size_pos = ftell(avi_file);
    put_u32(avi_file, 0);
    put_fourcc(avi_file, "movi");
    movi_data_start = ftell(avi_file);
    avi_header_written = true;

    write_log(_T("AVI output to '%s' started, %dx%d@%d raw video%s\n"),
        avioutput_filename_inuse, width, height, avioutput_fps,
        avioutput_audio == AVIAUDIO_AVI ? _T(" + PCM audio") : _T(""));
    return true;
}

static bool write_avi_chunk(const char id[4], const uae_u8 *data, size_t size, uae_u32 flags)
{
    if (!avi_file || !data) {
        return false;
    }

    long chunk_start = ftell(avi_file);
    put_fourcc(avi_file, id);
    put_u32(avi_file, (uae_u32)size);
    if (size && fwrite(data, 1, size, avi_file) != size) {
        avi_failed = true;
        return false;
    }
    if (size & 1) {
        fputc(0, avi_file);
    }

    UnixAviIndexEntry entry;
    memcpy(entry.id, id, 4);
    entry.flags = flags;
    entry.offset = (uae_u32)(chunk_start - movi_data_start);
    entry.size = (uae_u32)size;
    avi_index.push_back(entry);
    return true;
}

static void finish_avi(void)
{
    if (!avi_file) {
        return;
    }

    long movi_end = ftell(avi_file);
    patch_u32(avi_file, movi_size_pos, (uae_u32)(movi_end - movi_size_pos - 4));

    put_fourcc(avi_file, "idx1");
    put_u32(avi_file, (uae_u32)avi_index.size() * 16);
    for (const UnixAviIndexEntry &entry : avi_index) {
        put_fourcc(avi_file, entry.id);
        put_u32(avi_file, entry.flags);
        put_u32(avi_file, entry.offset);
        put_u32(avi_file, entry.size);
    }

    long end = ftell(avi_file);
    patch_u32(avi_file, riff_size_pos, (uae_u32)(end - 8));
    patch_u32(avi_file, avih_total_frames_pos, avi_frame_count);
    patch_u32(avi_file, video_length_pos, avi_frame_count);
    if (audio_length_pos) {
        int block_align = unix_avi_channels() * 2;
        patch_u32(avi_file, audio_length_pos, block_align ? avi_audio_bytes / block_align : 0);
    }

    fclose(avi_file);
    avi_file = NULL;
    avi_header_written = false;
    avi_index.clear();
    write_log(_T("AVI output stopped: %d frames, %u audio bytes\n"), avi_frame_count, avi_audio_bytes);
}

static void finish_wav(void)
{
    if (!wav_file) {
        return;
    }
    fseek(wav_file, 0, SEEK_SET);
    write_wave_header(wav_file, avi_audio_bytes);
    fclose(wav_file);
    wav_file = NULL;
    write_log(_T("Wave output stopped: %u audio bytes\n"), avi_audio_bytes);
}

static void start_if_requested(int monid, int width, int height, int frame_size)
{
    if (!avioutput_requested || avi_failed || avioutput_audio == AVIAUDIO_WAV) {
        return;
    }
    avioutput_enabled = avioutput_audio || avioutput_video;
    if (!avioutput_enabled) {
        return;
    }
    if (avioutput_video && !avi_file && width > 0 && height > 0) {
        aviout_monid = monid < 0 ? 0 : monid;
        start_avi_if_needed(width, height, frame_size);
    }
}

void AVIOutput_GetSettings(void)
{
}

void AVIOutput_SetSettings(void)
{
}

int AVIOutput_GetAudioCodec(TCHAR *name, int len)
{
    if (name && len > 0) {
        _tcsncpy(name, avioutput_audio == AVIAUDIO_WAV ? _T("Wave (internal)") : _T("PCM (internal)"), len - 1);
        name[len - 1] = 0;
    }
    return avioutput_audio ? avioutput_audio : AVIAUDIO_AVI;
}

int AVIOutput_ChooseAudioCodec(void *, TCHAR *name, int len)
{
    avioutput_audio = AVIAUDIO_AVI;
    return AVIOutput_GetAudioCodec(name, len);
}

int AVIOutput_GetVideoCodec(TCHAR *name, int len)
{
    if (name && len > 0) {
        _tcsncpy(name, _T("DIB RGB (internal)"), len - 1);
        name[len - 1] = 0;
    }
    return avioutput_video ? avioutput_video : 1;
}

int AVIOutput_ChooseVideoCodec(void *, TCHAR *name, int len)
{
    avioutput_video = 1;
    return AVIOutput_GetVideoCodec(name, len);
}

void AVIOutput_RGBinfo(int, int, int, int, int, int, int, int)
{
}

void Screenshot_RGBinfo(int, int, int, int, int, int, int, int)
{
}

void AVIOutput_End(void)
{
    finish_avi();
    finish_wav();
    avioutput_enabled = 0;
    avioutput_requested = 0;
    avi_failed = false;
    video_recording_active &= ~1;
}

void AVIOutput_Begin(bool)
{
    if (!avioutput_audio && !avioutput_video) {
        avioutput_video = 1;
    }
    avioutput_requested = 1;
    avioutput_enabled = avioutput_audio || avioutput_video;
    video_recording_active |= 1;
}

void AVIOutput_Restart(bool)
{
    bool requested = avioutput_requested != 0;
    AVIOutput_End();
    if (requested) {
        AVIOutput_Begin(false);
    }
}

void AVIOutput_Release(void)
{
    AVIOutput_End();
}

void AVIOutput_Initialize(void)
{
}

void AVIOutput_Toggle(int mode, bool immediate)
{
    if (mode) {
        if (!avioutput_requested) {
            AVIOutput_Begin(immediate);
        }
    } else {
        AVIOutput_End();
    }
}

bool AVIOutput_WriteAudio(uae_u8 *sndbuffer, int sndbufsize)
{
    if (!sndbuffer || sndbufsize <= 0 || !avioutput_requested || !avioutput_audio || avi_failed) {
        return false;
    }

    if (avioutput_audio == AVIAUDIO_WAV) {
        avioutput_enabled = 1;
        video_recording_active |= 1;
        if (!start_wav_if_needed()) {
            return false;
        }
        if (fwrite(sndbuffer, 1, sndbufsize, wav_file) != (size_t)sndbufsize) {
            avi_failed = true;
            return false;
        }
        avi_audio_bytes += sndbufsize;
        return true;
    }

    if (!avioutput_video && !avi_file) {
        start_avi_if_needed(1, 1, 4);
    }
    if (!avi_file || !avi_header_written) {
        return true;
    }
    if (!write_avi_chunk("01wb", sndbuffer, sndbufsize, 0)) {
        return false;
    }
    avi_audio_bytes += sndbufsize;
    return true;
}

bool frame_drawn(int monid)
{
    if (screenshot_multi) {
        screenshot(monid, 1, 1);
        if (screenshot_multi > 0) {
            screenshot_multi--;
        }
    }

    if (!avioutput_requested || !avioutput_video || avi_failed) {
        return false;
    }

    std::vector<uae_u8> frame;
    int width = 0;
    int height = 0;
    if (!prepare_video_frame(monid, &frame, &width, &height)) {
        return false;
    }
    start_if_requested(monid, width, height, (int)frame.size());
    if (!avi_file || !avi_header_written || monid != aviout_monid) {
        return false;
    }
    if (!write_avi_chunk("00db", frame.data(), frame.size(), 0x10)) {
        AVIOutput_End();
        return false;
    }
    avi_frame_count++;
    return true;
}
