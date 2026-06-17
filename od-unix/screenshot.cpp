#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "xwin.h"
#include "avioutput.h"
#include "custom.h"
#include "fsdb.h"

#include <algorithm>
#include <errno.h>
#include <unordered_map>
#include <vector>

#ifdef UAE_UNIX_WITH_SDL3
#include <SDL3/SDL.h>
#if SDL_VERSION_ATLEAST(3, 2, 0)
#define UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA 1
#endif
#endif

#ifdef WINUAE_UNIX_WITH_LIBPNG
#include <png.h>
#endif

extern int video_recording_active;
extern int get_custom_limits(int *pw, int *ph, int *pdx, int *pdy, int *prealh,
    int *hres, int *vres);

int screenshotmode =
#ifdef WINUAE_UNIX_WITH_LIBPNG
    1;
#else
    0;
#endif
int screenshot_originalsize = 0;
int screenshot_paletteindexed = 0;
int screenshot_clipmode = 0;
int screenshot_multi = 0;

static int screenshot_multi_start;
static int filenumber;
static int dirnumber = 1;

static void unix_tcslcpy(TCHAR *dst, const TCHAR *src, size_t size)
{
    if (!dst || !size) {
        return;
    }
    if (!src) {
        src = _T("");
    }
    _tcsncpy(dst, src, size - 1);
    dst[size - 1] = 0;
}

static void unix_bmp_append_word(std::vector<uae_u8> *out, uae_u16 value)
{
    out->push_back(value & 0xff);
    out->push_back((value >> 8) & 0xff);
}

static void unix_bmp_append_long(std::vector<uae_u8> *out, uae_u32 value)
{
    out->push_back(value & 0xff);
    out->push_back((value >> 8) & 0xff);
    out->push_back((value >> 16) & 0xff);
    out->push_back((value >> 24) & 0xff);
}

static bool unix_ensure_directory(const TCHAR *path)
{
    TCHAR tmp[MAX_DPATH];

    if (!path || !path[0]) {
        return false;
    }
    if (my_existsdir(path)) {
        return true;
    }

    unix_tcslcpy(tmp, path, sizeof tmp / sizeof(TCHAR));
    int len = (int)_tcslen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = 0;
    }

    for (TCHAR *p = tmp + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = 0;
        if (tmp[0] && !my_existsdir(tmp) && my_mkdir(tmp) < 0 && errno != EEXIST) {
            *p = '/';
            return false;
        }
        *p = '/';
    }

    return my_existsdir(tmp) || my_mkdir(tmp) == 0 || errno == EEXIST;
}

static void unix_screenshot_base_name(TCHAR *out, int out_size)
{
    const TCHAR *name = NULL;

    if (currprefs.floppyslots[0].dfxtype >= 0 && currprefs.floppyslots[0].df[0]) {
        name = currprefs.floppyslots[0].df;
    } else if (currprefs.cdslots[0].inuse && currprefs.cdslots[0].name[0]) {
        name = currprefs.cdslots[0].name;
    }

    if (name) {
        getfilepart(out, out_size, name);
        TCHAR *dot = _tcsrchr(out, '.');
        if (dot) {
            *dot = 0;
        }
    } else {
        unix_tcslcpy(out, _T("WinUAE"), out_size);
    }

    if (!out[0]) {
        unix_tcslcpy(out, _T("WinUAE"), out_size);
    }
    for (TCHAR *p = out; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '?' || *p == '*') {
            *p = '_';
        }
    }
}

static bool unix_make_bmp(const struct vidbuffer *vb, std::vector<uae_u8> *bmp)
{
    if (!bmp || !vb || !vb->bufmem || vb->outwidth <= 0 || vb->outheight <= 0 || vb->rowbytes <= 0) {
        return false;
    }
    if (vb->pixbytes != 4 && vb->pixbytes != 2) {
        write_log(_T("Unix screenshot: unsupported pixel size %d\n"), vb->pixbytes);
        return false;
    }

    const int width = vb->outwidth;
    const int height = vb->outheight;
    const int bmp_rowbytes = (width * 3 + 3) & ~3;
    const uae_u32 image_size = (uae_u32)bmp_rowbytes * (uae_u32)height;
    const uae_u32 file_size = 14 + 40 + image_size;

    bmp->clear();
    bmp->reserve(file_size);
    unix_bmp_append_word(bmp, 0x4d42);
    unix_bmp_append_long(bmp, file_size);
    unix_bmp_append_word(bmp, 0);
    unix_bmp_append_word(bmp, 0);
    unix_bmp_append_long(bmp, 14 + 40);
    unix_bmp_append_long(bmp, 40);
    unix_bmp_append_long(bmp, (uae_u32)width);
    unix_bmp_append_long(bmp, (uae_u32)height);
    unix_bmp_append_word(bmp, 1);
    unix_bmp_append_word(bmp, 24);
    unix_bmp_append_long(bmp, 0);
    unix_bmp_append_long(bmp, image_size);
    unix_bmp_append_long(bmp, 0);
    unix_bmp_append_long(bmp, 0);
    unix_bmp_append_long(bmp, 0);
    unix_bmp_append_long(bmp, 0);

    std::vector<uae_u8> row((size_t)bmp_rowbytes);
    for (int y = height - 1; y >= 0; y--) {
        const uae_u8 *src = vb->bufmem + (size_t)y * (size_t)vb->rowbytes;
        memset(row.data(), 0, row.size());
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
            row[(size_t)x * 3 + 0] = b;
            row[(size_t)x * 3 + 1] = g;
            row[(size_t)x * 3 + 2] = r;
        }
        bmp->insert(bmp->end(), row.begin(), row.end());
    }

    return true;
}

static bool unix_write_bmp(const TCHAR *filename, const struct vidbuffer *vb)
{
    std::vector<uae_u8> bmp;
    if (!filename || !unix_make_bmp(vb, &bmp)) {
        return false;
    }

    FILE *fp = _tfopen(filename, _T("wb"));
    if (!fp) {
        write_log(_T("Unix screenshot: can't open '%s'\n"), filename);
        return false;
    }

    if (fwrite(bmp.data(), 1, bmp.size(), fp) != bmp.size()) {
        fclose(fp);
        _tunlink(filename);
        write_log(_T("Unix screenshot: failed writing '%s'\n"), filename);
        return false;
    }
    fclose(fp);
    return true;
}

static uae_u32 unix_screenshot_get_pixel(const struct vidbuffer *vb, int x, int y);

#ifdef WINUAE_UNIX_WITH_LIBPNG
struct UnixPngBuffer {
    std::vector<uae_u8> *bytes;
};

static void unix_png_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    UnixPngBuffer *buffer = (UnixPngBuffer *)png_get_io_ptr(png_ptr);
    if (!buffer || !buffer->bytes) {
        png_error(png_ptr, "missing output buffer");
        return;
    }
    buffer->bytes->insert(buffer->bytes->end(), data, data + length);
}

static void unix_png_flush_data(png_structp)
{
}

static uae_u32 unix_rgb_key_from_pixel(const struct vidbuffer *vb, int x, int y)
{
    return unix_screenshot_get_pixel(vb, x, y) & 0x00ffffff;
}

static bool unix_collect_png_palette(const struct vidbuffer *vb, std::vector<uae_u32> *colors,
    std::vector<uae_u8> *indices)
{
    if (!vb || !colors || !indices || vb->outwidth <= 0 || vb->outheight <= 0) {
        return false;
    }

    colors->clear();
    indices->assign((size_t)vb->outwidth * (size_t)vb->outheight, 0);

    std::unordered_map<uae_u32, uae_u8> palette_index;
    for (int y = 0; y < vb->outheight; y++) {
        for (int x = 0; x < vb->outwidth; x++) {
            uae_u32 color = unix_rgb_key_from_pixel(vb, x, y);
            auto it = palette_index.find(color);
            if (it == palette_index.end()) {
                if (colors->size() >= 256) {
                    colors->clear();
                    indices->clear();
                    return false;
                }
                uae_u8 index = (uae_u8)colors->size();
                colors->push_back(color);
                palette_index[color] = index;
                (*indices)[(size_t)y * (size_t)vb->outwidth + (size_t)x] = index;
            } else {
                (*indices)[(size_t)y * (size_t)vb->outwidth + (size_t)x] = it->second;
            }
        }
    }

    return !colors->empty();
}

static bool unix_encode_png(std::vector<uae_u8> *png, const struct vidbuffer *vb)
{
    if (!png || !vb || !vb->bufmem || vb->outwidth <= 0 || vb->outheight <= 0 || vb->rowbytes <= 0) {
        return false;
    }
    if (vb->pixbytes != 4 && vb->pixbytes != 2) {
        write_log(_T("Unix screenshot: unsupported pixel size %d\n"), vb->pixbytes);
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        return false;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return false;
    }
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    const int width = vb->outwidth;
    const int height = vb->outheight;
    png->clear();
    UnixPngBuffer buffer = { png };
    png_set_write_fn(png_ptr, &buffer, unix_png_write_data, unix_png_flush_data);

    std::vector<uae_u32> palette_colors;
    std::vector<uae_u8> palette_indices;
    const bool use_palette = screenshot_paletteindexed &&
        unix_collect_png_palette(vb, &palette_colors, &palette_indices);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8,
        use_palette ? PNG_COLOR_TYPE_PALETTE : PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (use_palette) {
        png_color palette[256];
        for (size_t i = 0; i < palette_colors.size(); i++) {
            uae_u32 color = palette_colors[i];
            palette[i].red = (color >> 16) & 0xff;
            palette[i].green = (color >> 8) & 0xff;
            palette[i].blue = color & 0xff;
        }
        png_set_PLTE(png_ptr, info_ptr, palette, (int)palette_colors.size());
    }
    png_write_info(png_ptr, info_ptr);

    std::vector<uae_u8> row((size_t)width * (use_palette ? 1 : 3));
    for (int y = 0; y < height; y++) {
        const uae_u8 *src = vb->bufmem + (size_t)y * (size_t)vb->rowbytes;
        if (use_palette) {
            memcpy(row.data(), palette_indices.data() + (size_t)y * (size_t)width, (size_t)width);
        } else {
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
                row[(size_t)x * 3 + 0] = r;
                row[(size_t)x * 3 + 1] = g;
                row[(size_t)x * 3 + 2] = b;
            }
        }
        png_write_row(png_ptr, row.data());
    }

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return true;
}

static bool unix_write_png(const TCHAR *filename, const struct vidbuffer *vb)
{
    std::vector<uae_u8> png;
    if (!filename || !unix_encode_png(&png, vb)) {
        return false;
    }

    FILE *fp = _tfopen(filename, _T("wb"));
    if (!fp) {
        write_log(_T("Unix screenshot: can't open '%s'\n"), filename);
        return false;
    }
    if (fwrite(png.data(), 1, png.size(), fp) != png.size()) {
        fclose(fp);
        _tunlink(filename);
        write_log(_T("Unix screenshot: failed writing '%s'\n"), filename);
        return false;
    }
    fclose(fp);
    return true;
}
#endif

static int unix_screenshot_clamp_multiplier(int value)
{
    if (value < 1) {
        return 1;
    }
    if (value > 8) {
        return 8;
    }
    return value;
}

static uae_u32 unix_screenshot_get_pixel(const struct vidbuffer *vb, int x, int y)
{
    const uae_u8 *src = vb->bufmem + (size_t)y * (size_t)vb->rowbytes + (size_t)x * (size_t)vb->pixbytes;

    if (vb->pixbytes == 4) {
        return ((const uae_u32 *)src)[0];
    }

    const uae_u16 pixel = (uae_u16)src[0] | ((uae_u16)src[1] << 8);
    const uae_u8 r = (uae_u8)((((pixel >> 11) & 0x1f) * 255) / 31);
    const uae_u8 g = (uae_u8)((((pixel >> 5) & 0x3f) * 255) / 63);
    const uae_u8 b = (uae_u8)(((pixel & 0x1f) * 255) / 31);
    return (uae_u32)b | ((uae_u32)g << 8) | ((uae_u32)r << 16);
}

static bool unix_prepare_screenshot_buffer(const struct vidbuffer *src, struct vidbuffer *dst,
    std::vector<uae_u8> &storage, bool standard)
{
    if (!src || !dst || !src->bufmem || src->outwidth <= 0 || src->outheight <= 0 || src->rowbytes <= 0) {
        return false;
    }
    if (src->pixbytes != 4 && src->pixbytes != 2) {
        write_log(_T("Unix screenshot: unsupported pixel size %d\n"), src->pixbytes);
        return false;
    }

    int width = standard || currprefs.screenshot_width <= 0 ? src->outwidth : currprefs.screenshot_width;
    int height = standard || currprefs.screenshot_height <= 0 ? src->outheight : currprefs.screenshot_height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    int xoffset = standard ? -1 : currprefs.screenshot_xoffset;
    int yoffset = standard ? -1 : currprefs.screenshot_yoffset;

    if (!standard && screenshot_originalsize && screenshot_clipmode == 1) {
        int cw, ch, cx, cy, crealh = 0, hres, vres;
        if (get_custom_limits(&cw, &ch, &cx, &cy, &crealh, &hres, &vres)) {
            int maxw = currprefs.screenshot_max_width << currprefs.gfx_resolution;
            int maxh = currprefs.screenshot_max_height << currprefs.gfx_vresolution;
            int minw = currprefs.screenshot_min_width << currprefs.gfx_resolution;
            int minh = currprefs.screenshot_min_height << currprefs.gfx_vresolution;
            if (minw > (maxhpos_display + 1) << currprefs.gfx_resolution) {
                minw = (maxhpos_display + 1) << currprefs.gfx_resolution;
            }
            if (minh > (maxvsize_display + 1) << currprefs.gfx_vresolution) {
                minh = (maxvsize_display + 1) << currprefs.gfx_vresolution;
            }
            if (maxw < minw) {
                maxw = minw;
            }
            if (maxh < minh) {
                maxh = minh;
            }
            width = cw;
            height = ch;
            xoffset = cx;
            yoffset = cy;
            if (width < minw && minw > 0) {
                xoffset -= (minw - width) / 2;
                width = minw;
            }
            if (height < minh && minh > 0) {
                yoffset -= (minh - height) / 2;
                height = minh;
            }
            if (width > maxw && maxw > 0) {
                xoffset += (width - maxw) / 2;
                width = maxw;
            }
            if (height > maxh && maxh > 0) {
                yoffset += (height - maxh) / 2;
                height = maxh;
            }
        }
    }

    int xmult = standard ? 1 : unix_screenshot_clamp_multiplier(currprefs.screenshot_xmult + 1);
    int ymult = standard ? 1 : unix_screenshot_clamp_multiplier(currprefs.screenshot_ymult + 1);
    while (!standard && currprefs.screenshot_output_width > width * xmult && xmult < 8) {
        xmult++;
    }
    while (!standard && currprefs.screenshot_output_height > height * ymult && ymult < 8) {
        ymult++;
    }

    const int output_width = width * xmult;
    const int output_height = height * ymult;
    const int output_rowbytes = output_width * 4;
    storage.assign((size_t)output_rowbytes * (size_t)output_height, 0);

    xoffset = xoffset < 0 ? (width - src->outwidth) / 2 : -xoffset;
    yoffset = yoffset < 0 ? (height - src->outheight) / 2 : -yoffset;
    int dst_x = 0;
    int dst_y = 0;
    int src_x = 0;
    int src_y = 0;

    if (xoffset > 0) {
        dst_x = std::min(xoffset, std::max(0, width - src->outwidth));
    } else if (xoffset < 0) {
        src_x = std::min(-xoffset, std::max(0, src->outwidth - width));
    }
    if (yoffset > 0) {
        dst_y = std::min(yoffset, std::max(0, height - src->outheight));
    } else if (yoffset < 0) {
        src_y = std::min(-yoffset, std::max(0, src->outheight - height));
    }

    const int copy_width = std::min(width - dst_x, src->outwidth - src_x);
    const int copy_height = std::min(height - dst_y, src->outheight - src_y);
    if (copy_width > 0 && copy_height > 0) {
        for (int y = 0; y < copy_height; y++) {
            for (int x = 0; x < copy_width; x++) {
                const uae_u32 pixel = unix_screenshot_get_pixel(src, src_x + x, src_y + y);
                for (int yy = 0; yy < ymult; yy++) {
                    uae_u32 *out = (uae_u32 *)(storage.data()
                        + (size_t)((dst_y + y) * ymult + yy) * (size_t)output_rowbytes)
                        + (size_t)(dst_x + x) * (size_t)xmult;
                    for (int xx = 0; xx < xmult; xx++) {
                        out[xx] = pixel;
                    }
                }
            }
        }
    }

    memset(dst, 0, sizeof *dst);
    dst->bufmem = storage.data();
    dst->outwidth = output_width;
    dst->outheight = output_height;
    dst->rowbytes = output_rowbytes;
    dst->pixbytes = 4;
    return true;
}

static bool unix_prepare_active_screenshot(int monid, struct vidbuffer *prepared,
    std::vector<uae_u8> &prepared_storage, bool standard = false)
{
    if (monid < 0 || monid >= MAX_AMIGADISPLAYS) {
        monid = 0;
    }
    struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
    struct vidbuffer *vb = vidinfo->inbuffer ? vidinfo->inbuffer : &vidinfo->drawbuffer;
    if (!vb || !vb->bufmem || vb->outwidth <= 0 || vb->outheight <= 0) {
        write_log(_T("Unix screenshot: no active video buffer\n"));
        return false;
    }
    if (!unix_prepare_screenshot_buffer(vb, prepared, prepared_storage, standard)) {
        return false;
    }
    return true;
}

#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
struct UnixScreenshotClipboardData {
    std::vector<uae_u8> bmp;
};

static const void *SDLCALL unix_screenshot_clipboard_data(void *userdata, const char *, size_t *size)
{
    UnixScreenshotClipboardData *data = static_cast<UnixScreenshotClipboardData *>(userdata);
    if (!data) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    if (size) {
        *size = data->bmp.size();
    }
    return data->bmp.data();
}

static void SDLCALL unix_screenshot_clipboard_cleanup(void *userdata)
{
    delete static_cast<UnixScreenshotClipboardData *>(userdata);
}
#endif

static bool unix_save_screenshot_clipboard(const struct vidbuffer *prepared)
{
#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
    UnixScreenshotClipboardData *data = new UnixScreenshotClipboardData;
    if (!unix_make_bmp(prepared, &data->bmp)) {
        delete data;
        return false;
    }

    const char *mime_types[] = { "image/bmp", "image/x-bmp" };
    if (!SDL_SetClipboardData(unix_screenshot_clipboard_data, unix_screenshot_clipboard_cleanup,
        data, mime_types, sizeof mime_types / sizeof mime_types[0])) {
        write_log(_T("Unix screenshot: failed to copy BMP to clipboard: %s\n"), SDL_GetError());
        delete data;
        return false;
    }

    write_log(_T("Screenshot copied to clipboard\n"));
    return true;
#else
    write_log(_T("Unix screenshot: clipboard screenshots require SDL3 clipboard data support\n"));
    return false;
#endif
}

static bool unix_save_screenshot_file(int monid)
{
    TCHAR path[MAX_DPATH];
    TCHAR base[MAX_DPATH];
    TCHAR filename[MAX_DPATH];
    std::vector<uae_u8> prepared_storage;
    struct vidbuffer prepared;

    if (!unix_prepare_active_screenshot(monid, &prepared, prepared_storage)) {
        return false;
    }
    fetch_screenshotpath(path, sizeof path / sizeof(TCHAR));
    if (!unix_ensure_directory(path)) {
        write_log(_T("Unix screenshot: can't create screenshot directory '%s'\n"), path);
        return false;
    }
    if (screenshot_multi) {
        TCHAR *p = path + _tcslen(path);
        while (dirnumber < 1000) {
            _sntprintf(p, (sizeof path / sizeof(TCHAR)) - (p - path), _T("%03d/"), dirnumber);
            if (!screenshot_multi_start) {
                break;
            }
            filenumber = 0;
            if (!my_existsdir(path) && !my_existsfile(path)) {
                break;
            }
            dirnumber++;
        }
        screenshot_multi_start = 0;
        if (dirnumber == 1000 || !unix_ensure_directory(path)) {
            write_log(_T("Unix screenshot: can't create continuous screenshot directory '%s'\n"), path);
            screenshot_multi = 0;
            video_recording_active &= ~2;
            return false;
        }
    }
    unix_screenshot_base_name(base, sizeof base / sizeof(TCHAR));

    for (int i = filenumber + 1; i < 100000; i++) {
#ifdef WINUAE_UNIX_WITH_LIBPNG
        if (screenshotmode == 1) {
            _sntprintf(filename, sizeof filename / sizeof(TCHAR), _T("%s%s_%05d.png"), path, base, i);
        } else {
            _sntprintf(filename, sizeof filename / sizeof(TCHAR), _T("%s%s_%05d.bmp"), path, base, i);
        }
#else
        _sntprintf(filename, sizeof filename / sizeof(TCHAR), _T("%s%s_%05d.bmp"), path, base, i);
#endif
        FILE *existing = _tfopen(filename, _T("rb"));
        if (existing) {
            fclose(existing);
            continue;
        }
#ifdef WINUAE_UNIX_WITH_LIBPNG
        if (screenshotmode == 1) {
            if (!unix_write_png(filename, &prepared)) {
                return false;
            }
        } else {
            if (!unix_write_bmp(filename, &prepared)) {
                return false;
            }
        }
#else
        if (!unix_write_bmp(filename, &prepared)) {
            return false;
        }
#endif
        filenumber = i;
        write_log(_T("Screenshot saved as \"%s\"\n"), filename);
        return true;
    }

    write_log(_T("Unix screenshot: no free filename in '%s'\n"), path);
    return false;
}

void screenshot(int monid, int mode, int)
{
    if (monid < 0) {
        monid = 0;
    }
    if (mode == 0) {
        std::vector<uae_u8> prepared_storage;
        struct vidbuffer prepared;
        if (unix_prepare_active_screenshot(monid, &prepared, prepared_storage)) {
            unix_save_screenshot_clipboard(&prepared);
        }
        return;
    }
    if (mode == 2) {
        screenshot_multi = 10;
        video_recording_active |= 2;
        screenshot_multi_start = 1;
        return;
    }
    if (mode == 3) {
        screenshot_multi = -1;
        screenshot_multi_start = 1;
        video_recording_active &= ~2;
        return;
    }
    if (mode == 4) {
        screenshot_multi = 0;
        filenumber = 0;
        video_recording_active &= ~2;
        return;
    }
    unix_save_screenshot_file(monid);
}

void screenshot_reset(void)
{
}

uae_u8 *save_screenshot(int monid, size_t *len)
{
    std::vector<uae_u8> prepared_storage;
    struct vidbuffer prepared;

    if (len) {
        *len = 0;
    }
    if (!unix_prepare_active_screenshot(monid, &prepared, prepared_storage, true)) {
        return NULL;
    }

    std::vector<uae_u8> bytes;
#ifdef WINUAE_UNIX_WITH_LIBPNG
    if (!unix_encode_png(&bytes, &prepared)) {
        return NULL;
    }
#else
    if (!unix_make_bmp(&prepared, &bytes)) {
        return NULL;
    }
#endif
    if (bytes.empty()) {
        return NULL;
    }

    uae_u8 *out = xmalloc(uae_u8, bytes.size());
    memcpy(out, bytes.data(), bytes.size());
    if (len) {
        *len = bytes.size();
    }
    return out;
}
