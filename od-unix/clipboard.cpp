#include "sysconfig.h"
#include "sysdeps.h"

#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

#ifdef WINUAE_UNIX_WITH_IMAGEIO
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#define UAE_UNIX_CLIPBOARD_IMAGEIO 1
#endif

#ifdef UAE_UNIX_WITH_SDL3
#include <SDL3/SDL.h>
#if SDL_VERSION_ATLEAST(3, 2, 0)
#define UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA 1
#endif
#endif

#ifdef WINUAE_UNIX_WITH_LIBPNG
#include <png.h>
#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) && defined(PNG_SIMPLIFIED_WRITE_SUPPORTED)
#define UAE_UNIX_CLIPBOARD_PNG 1
#endif
#endif

#include "traps.h"
#include "clipboard.h"
#include "keybuf.h"
#include "native2amiga_api.h"
#include "uae.h"

extern bool filesys_heartbeat(void);

static constexpr size_t CLIP_SIZE_LIMIT = 10000000;
static constexpr size_t CLIP_SIZE_LIMIT_INIT = 30000;

static uaecptr clipboard_data;
static int vdelay, vdelay2;
static int signaling, initialized;
static std::vector<uae_u8> to_amiga;
static int to_amiga_phase;
static bool clip_disabled;
static int host_poll_delay;
static std::string last_host_clipboard;
static bool last_host_clipboard_valid;
static std::vector<uae_u8> last_host_clipboard_bitmap;
static bool last_host_clipboard_bitmap_valid;

static bool read_command_output(const char *command, std::string *out, size_t max_bytes)
{
	FILE *pipe = popen(command, "r");
	if (!pipe) {
		return false;
	}

	char buffer[4096];
	const size_t read_limit = max_bytes + 1;
	while (out->size() < read_limit) {
		const size_t remaining = read_limit - out->size();
		const size_t wanted = remaining < sizeof buffer ? remaining : sizeof buffer;
		const size_t got = fread(buffer, 1, wanted, pipe);
		if (got > 0) {
			out->append(buffer, got);
		}
		if (got < wanted) {
			if (feof(pipe) || ferror(pipe)) {
				break;
			}
		}
	}

	const bool too_large = out->size() > max_bytes;
	const int status = pclose(pipe);
	if (too_large) {
		out->clear();
		return false;
	}
	return status == 0;
}

static bool write_command_input(const char *command, const std::string &text)
{
	FILE *pipe = popen(command, "w");
	if (!pipe) {
		return false;
	}
	if (!text.empty() && fwrite(text.data(), 1, text.size(), pipe) != text.size()) {
		pclose(pipe);
		return false;
	}
	return pclose(pipe) == 0;
}

#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
struct UnixClipboardData {
	std::vector<uae_u8> bmp;
	std::vector<uae_u8> png;
	std::vector<uae_u8> tiff;
};

static const void *SDLCALL unix_clipboard_data_callback(void *userdata, const char *mime_type, size_t *size)
{
	UnixClipboardData *data = static_cast<UnixClipboardData *>(userdata);
	if (!data) {
		if (size) {
			*size = 0;
		}
		return NULL;
	}
	const std::vector<uae_u8> *payload = NULL;
#ifdef UAE_UNIX_CLIPBOARD_PNG
	if (mime_type && !strcmp(mime_type, "image/png") && !data->png.empty()) {
		payload = &data->png;
	}
#endif
#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
	if (!payload && mime_type &&
		(!strcmp(mime_type, "image/tiff") || !strcmp(mime_type, "image/tif") ||
		 !strcmp(mime_type, "public.tiff")) && !data->tiff.empty()) {
		payload = &data->tiff;
	}
#endif
	if (!payload && mime_type &&
		(!strcmp(mime_type, "image/bmp") || !strcmp(mime_type, "image/x-bmp")) && !data->bmp.empty()) {
		payload = &data->bmp;
	}
	if (!payload) {
		payload = !data->bmp.empty() ? &data->bmp : &data->png;
	}
	if (size) {
		*size = payload ? payload->size() : 0;
	}
	return payload ? payload->data() : NULL;
}

static void SDLCALL unix_clipboard_data_cleanup(void *userdata)
{
	delete static_cast<UnixClipboardData *>(userdata);
}

static bool has_host_clipboard_image(void)
{
	return
#ifdef UAE_UNIX_CLIPBOARD_PNG
		SDL_HasClipboardData("image/png") ||
#endif
#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
		SDL_HasClipboardData("image/tiff") || SDL_HasClipboardData("image/tif") ||
		SDL_HasClipboardData("public.tiff") ||
#endif
		SDL_HasClipboardData("image/bmp") || SDL_HasClipboardData("image/x-bmp");
}

static bool read_host_clipboard_data(const char * const *mime_types, std::vector<uae_u8> *out, size_t max_bytes)
{
	for (int i = 0; mime_types[i]; i++) {
		if (!SDL_HasClipboardData(mime_types[i])) {
			continue;
		}
		size_t size = 0;
		void *data = SDL_GetClipboardData(mime_types[i], &size);
		if (!data) {
			continue;
		}
		if (size > max_bytes) {
			SDL_free(data);
			return false;
		}
		out->assign((const uae_u8 *)data, (const uae_u8 *)data + size);
		SDL_free(data);
		return true;
	}
	return false;
}

#ifdef UAE_UNIX_CLIPBOARD_PNG
static bool read_host_clipboard_png(std::vector<uae_u8> *png, size_t max_bytes)
{
	const char *mime_types[] = { "image/png", NULL };
	return read_host_clipboard_data(mime_types, png, max_bytes);
}
#endif

#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
static bool read_host_clipboard_tiff(std::vector<uae_u8> *tiff, size_t max_bytes)
{
	const char *mime_types[] = { "image/tiff", "image/tif", "public.tiff", NULL };
	return read_host_clipboard_data(mime_types, tiff, max_bytes);
}
#endif

static bool read_host_clipboard_bmp(std::vector<uae_u8> *bmp, size_t max_bytes)
{
	const char *mime_types[] = { "image/bmp", "image/x-bmp", NULL };
	return read_host_clipboard_data(mime_types, bmp, max_bytes);
}

static bool write_host_clipboard_image(const std::vector<uae_u8> &bmp, const std::vector<uae_u8> *png,
	const std::vector<uae_u8> *tiff)
{
	UnixClipboardData *data = new UnixClipboardData;
	data->bmp = bmp;
	if (png) {
		data->png = *png;
	}
	if (tiff) {
		data->tiff = *tiff;
	}
	const bool cache_png = png && !png->empty();
	const bool cache_tiff = tiff && !tiff->empty();

	const char *mime_types[] = {
#ifdef UAE_UNIX_CLIPBOARD_PNG
		"image/png",
#endif
#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
		"image/tiff", "image/tif", "public.tiff",
#endif
		"image/bmp", "image/x-bmp"
	};
	if (!SDL_SetClipboardData(unix_clipboard_data_callback, unix_clipboard_data_cleanup,
		data, mime_types, sizeof mime_types / sizeof mime_types[0])) {
		write_log(_T("clipboard: failed to write host clipboard bitmap: %s\n"), SDL_GetError());
		delete data;
		return false;
	}

	last_host_clipboard.clear();
	last_host_clipboard_valid = false;
	last_host_clipboard_bitmap = cache_png ? *png : (cache_tiff ? *tiff : bmp);
	last_host_clipboard_bitmap_valid = true;
	return true;
}

static bool write_host_clipboard_bmp(const std::vector<uae_u8> &bmp)
{
	return write_host_clipboard_image(bmp, NULL, NULL);
}
#endif

static bool read_host_clipboard_text(std::string *text, size_t max_bytes)
{
#ifdef UAE_UNIX_WITH_SDL3
	if (SDL_HasClipboardText()) {
		char *clipboard = SDL_GetClipboardText();
		if (clipboard) {
			text->assign(clipboard);
			SDL_free(clipboard);
			if (text->size() > max_bytes) {
				text->clear();
				return false;
			}
			return true;
		}
	}
#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
	if (has_host_clipboard_image()) {
		return false;
	}
#endif
#endif

	const char *commands[] = {
#ifdef __APPLE__
		"/usr/bin/pbpaste",
#else
		"wl-paste --no-newline 2>/dev/null",
		"xclip -selection clipboard -o 2>/dev/null",
		"xsel --clipboard --output 2>/dev/null",
#endif
		NULL
	};

	for (int i = 0; commands[i]; i++) {
		text->clear();
		if (read_command_output(commands[i], text, max_bytes)) {
			return true;
		}
	}
	text->clear();
	return false;
}

static bool write_host_clipboard_text(const std::string &text)
{
#ifdef UAE_UNIX_WITH_SDL3
	if (SDL_SetClipboardText(text.c_str())) {
		last_host_clipboard = text;
		last_host_clipboard_valid = true;
		last_host_clipboard_bitmap.clear();
		last_host_clipboard_bitmap_valid = false;
		return true;
	}
#endif

	const char *commands[] = {
#ifdef __APPLE__
		"/usr/bin/pbcopy",
#else
		"wl-copy 2>/dev/null",
		"xclip -selection clipboard 2>/dev/null",
		"xsel --clipboard --input 2>/dev/null",
#endif
		NULL
	};

	for (int i = 0; commands[i]; i++) {
		if (write_command_input(commands[i], text)) {
			last_host_clipboard = text;
			last_host_clipboard_valid = true;
			last_host_clipboard_bitmap.clear();
			last_host_clipboard_bitmap_valid = false;
			return true;
		}
	}
	return false;
}

static void normalize_text_for_keybuf(std::string *text)
{
	std::string normalized;
	normalized.reserve(text->size());
	for (size_t i = 0; i < text->size(); i++) {
		const char ch = (*text)[i];
		if (ch == '\r') {
			if (i + 1 < text->size() && (*text)[i + 1] == '\n') {
				continue;
			}
			normalized.push_back('\n');
		} else {
			normalized.push_back(ch);
		}
	}
	*text = normalized;
}

static std::string host_text_to_amiga_text(const std::string &text)
{
	std::string converted;
	converted.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '\r') {
			converted.push_back(text[i]);
		}
	}
	return converted;
}

static void append_be32(std::vector<uae_u8> *out, uae_u32 value)
{
	out->push_back(value >> 24);
	out->push_back(value >> 16);
	out->push_back(value >> 8);
	out->push_back(value);
}

static void append_be16(std::vector<uae_u8> *out, uae_u16 value)
{
	out->push_back(value >> 8);
	out->push_back(value);
}

static void append_le16(std::vector<uae_u8> *out, uae_u16 value)
{
	out->push_back(value);
	out->push_back(value >> 8);
}

static void append_le32(std::vector<uae_u8> *out, uae_u32 value)
{
	out->push_back(value);
	out->push_back(value >> 8);
	out->push_back(value >> 16);
	out->push_back(value >> 24);
}

static uae_u16 read_le16(const uae_u8 *p)
{
	return (uae_u16)p[0] | ((uae_u16)p[1] << 8);
}

static uae_u32 read_le32(const uae_u8 *p)
{
	return (uae_u32)p[0] | ((uae_u32)p[1] << 8) | ((uae_u32)p[2] << 16) | ((uae_u32)p[3] << 24);
}

static uae_u16 read_be16(const uae_u8 *p)
{
	return ((uae_u16)p[0] << 8) | p[1];
}

static uae_u32 read_be32(const uae_u8 *p)
{
	return ((uae_u32)p[0] << 24) | ((uae_u32)p[1] << 16) | ((uae_u32)p[2] << 8) | p[3];
}

static void append_literal(std::vector<uae_u8> *out, const char *literal)
{
	for (int i = 0; i < 4; i++) {
		out->push_back((uae_u8)literal[i]);
	}
}

struct UnixClipboardBitmap {
	int width = 0;
	int height = 0;
	std::vector<uae_u32> pixels;
};

static bool parse_bmp(const std::vector<uae_u8> &bmp, UnixClipboardBitmap *bitmap)
{
	if (!bitmap || bmp.size() < 54 || bmp[0] != 'B' || bmp[1] != 'M') {
		return false;
	}

	const uae_u32 pixel_offset = read_le32(&bmp[10]);
	const uae_u32 header_size = read_le32(&bmp[14]);
	if (header_size < 40 || 14 + header_size > bmp.size()) {
		return false;
	}

	const int width = (int)read_le32(&bmp[18]);
	const int signed_height = (int)read_le32(&bmp[22]);
	const int height = signed_height < 0 ? -signed_height : signed_height;
	const bool top_down = signed_height < 0;
	const int planes = read_le16(&bmp[26]);
	const int bpp = read_le16(&bmp[28]);
	const uae_u32 compression = read_le32(&bmp[30]);
	if (width <= 0 || height <= 0 || planes != 1 || compression != 0 ||
		(bpp != 8 && bpp != 24 && bpp != 32)) {
		return false;
	}

	const size_t rowbytes = ((size_t)width * (size_t)bpp + 31) / 32 * 4;
	if (pixel_offset > bmp.size() || rowbytes > (bmp.size() - pixel_offset) / (size_t)height) {
		return false;
	}

	std::vector<uae_u32> palette;
	if (bpp == 8) {
		uae_u32 colors = read_le32(&bmp[46]);
		if (!colors) {
			colors = 256;
		}
		const size_t palette_offset = 14 + header_size;
		if (palette_offset + (size_t)colors * 4 > bmp.size()) {
			return false;
		}
		palette.reserve(colors);
		for (uae_u32 i = 0; i < colors; i++) {
			const uae_u8 *entry = &bmp[palette_offset + (size_t)i * 4];
			palette.push_back(((uae_u32)entry[2] << 16) | ((uae_u32)entry[1] << 8) | entry[0]);
		}
	}

	bitmap->width = width;
	bitmap->height = height;
	bitmap->pixels.assign((size_t)width * (size_t)height, 0);
	for (int y = 0; y < height; y++) {
		const int src_y = top_down ? y : height - 1 - y;
		const uae_u8 *src = &bmp[pixel_offset + (size_t)src_y * rowbytes];
		for (int x = 0; x < width; x++) {
			uae_u32 pixel = 0;
			if (bpp == 8) {
				const uae_u8 index = src[x];
				if (index < palette.size()) {
					pixel = palette[index];
				}
			} else if (bpp == 24) {
				const uae_u8 *p = src + (size_t)x * 3;
				pixel = ((uae_u32)p[2] << 16) | ((uae_u32)p[1] << 8) | p[0];
			} else {
				const uae_u8 *p = src + (size_t)x * 4;
				pixel = ((uae_u32)p[2] << 16) | ((uae_u32)p[1] << 8) | p[0];
			}
			bitmap->pixels[(size_t)y * (size_t)width + x] = pixel;
		}
	}
	return true;
}

static bool make_bmp(const UnixClipboardBitmap &bitmap, std::vector<uae_u8> *bmp)
{
	if (!bmp || bitmap.width <= 0 || bitmap.height <= 0 ||
		bitmap.pixels.size() < (size_t)bitmap.width * (size_t)bitmap.height) {
		return false;
	}

	const int width = bitmap.width;
	const int height = bitmap.height;
	const size_t rowbytes = ((size_t)width * 3 + 3) & ~3;
	const uae_u32 image_size = (uae_u32)(rowbytes * (size_t)height);
	const uae_u32 file_size = 14 + 40 + image_size;

	bmp->clear();
	bmp->reserve(file_size);
	bmp->push_back('B');
	bmp->push_back('M');
	append_le32(bmp, file_size);
	append_le16(bmp, 0);
	append_le16(bmp, 0);
	append_le32(bmp, 14 + 40);
	append_le32(bmp, 40);
	append_le32(bmp, (uae_u32)width);
	append_le32(bmp, (uae_u32)height);
	append_le16(bmp, 1);
	append_le16(bmp, 24);
	append_le32(bmp, 0);
	append_le32(bmp, image_size);
	append_le32(bmp, 0);
	append_le32(bmp, 0);
	append_le32(bmp, 0);
	append_le32(bmp, 0);

	std::vector<uae_u8> row(rowbytes);
	for (int y = height - 1; y >= 0; y--) {
		memset(row.data(), 0, row.size());
		for (int x = 0; x < width; x++) {
			const uae_u32 pixel = bitmap.pixels[(size_t)y * (size_t)width + x];
			row[(size_t)x * 3 + 0] = pixel;
			row[(size_t)x * 3 + 1] = pixel >> 8;
			row[(size_t)x * 3 + 2] = pixel >> 16;
		}
		bmp->insert(bmp->end(), row.begin(), row.end());
	}
	return true;
}

#ifdef UAE_UNIX_CLIPBOARD_PNG
static bool parse_png(const std::vector<uae_u8> &png, UnixClipboardBitmap *bitmap)
{
	if (!bitmap || png.empty()) {
		return false;
	}

	png_image image;
	memset(&image, 0, sizeof image);
	image.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&image, png.data(), png.size())) {
		return false;
	}
	if (image.width == 0 || image.height == 0 ||
		(uae_u64)image.width * (uae_u64)image.height > CLIP_SIZE_LIMIT / 4) {
		png_image_free(&image);
		return false;
	}

	image.format = PNG_FORMAT_RGBA;
	std::vector<uae_u8> rgba(PNG_IMAGE_SIZE(image));
	if (!png_image_finish_read(&image, NULL, rgba.data(), 0, NULL)) {
		png_image_free(&image);
		return false;
	}

	bitmap->width = (int)image.width;
	bitmap->height = (int)image.height;
	bitmap->pixels.assign((size_t)image.width * (size_t)image.height, 0);
	for (png_uint_32 y = 0; y < image.height; y++) {
		const uae_u8 *row = rgba.data() + (size_t)y * image.width * 4;
		for (png_uint_32 x = 0; x < image.width; x++) {
			const uae_u8 *p = row + (size_t)x * 4;
			const uae_u32 alpha = p[3];
			const uae_u32 red = (p[0] * alpha + 255 * (255 - alpha) + 127) / 255;
			const uae_u32 green = (p[1] * alpha + 255 * (255 - alpha) + 127) / 255;
			const uae_u32 blue = (p[2] * alpha + 255 * (255 - alpha) + 127) / 255;
			bitmap->pixels[(size_t)y * image.width + x] = (red << 16) | (green << 8) | blue;
		}
	}
	png_image_free(&image);
	return true;
}

static bool make_png(const UnixClipboardBitmap &bitmap, std::vector<uae_u8> *png)
{
	if (!png || bitmap.width <= 0 || bitmap.height <= 0 ||
		bitmap.pixels.size() < (size_t)bitmap.width * (size_t)bitmap.height) {
		return false;
	}

	std::vector<uae_u8> rgb((size_t)bitmap.width * (size_t)bitmap.height * 3);
	for (int y = 0; y < bitmap.height; y++) {
		uae_u8 *row = rgb.data() + (size_t)y * bitmap.width * 3;
		for (int x = 0; x < bitmap.width; x++) {
			const uae_u32 pixel = bitmap.pixels[(size_t)y * bitmap.width + x];
			row[(size_t)x * 3 + 0] = pixel >> 16;
			row[(size_t)x * 3 + 1] = pixel >> 8;
			row[(size_t)x * 3 + 2] = pixel;
		}
	}

	png_image image;
	memset(&image, 0, sizeof image);
	image.version = PNG_IMAGE_VERSION;
	image.width = bitmap.width;
	image.height = bitmap.height;
	image.format = PNG_FORMAT_RGB;

	png_alloc_size_t size = 0;
	if (!png_image_write_to_memory(&image, NULL, &size, 0, rgb.data(), 0, NULL) || !size) {
		png_image_free(&image);
		return false;
	}
	png->resize(size);
	if (!png_image_write_to_memory(&image, png->data(), &size, 0, rgb.data(), 0, NULL)) {
		png_image_free(&image);
		png->clear();
		return false;
	}
	png->resize(size);
	png_image_free(&image);
	return true;
}
#endif

#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
static bool parse_imageio_bitmap(const std::vector<uae_u8> &data, UnixClipboardBitmap *bitmap)
{
    if (!bitmap || data.empty()) {
        return false;
    }

    CFDataRef image_data = CFDataCreate(kCFAllocatorDefault, data.data(), data.size());
    if (!image_data) {
        return false;
    }
    CGImageSourceRef source = CGImageSourceCreateWithData(image_data, NULL);
    CFRelease(image_data);
    if (!source) {
        return false;
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
    CFRelease(source);
    if (!image) {
        return false;
    }

    const size_t width = CGImageGetWidth(image);
    const size_t height = CGImageGetHeight(image);
    if (!width || !height || width > INT_MAX || height > INT_MAX ||
        (uae_u64)width * (uae_u64)height > CLIP_SIZE_LIMIT / 4) {
        CGImageRelease(image);
        return false;
    }

    std::vector<uae_u8> rgba(width * height * 4);
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(rgba.data(), width, height, 8, width * 4,
        colorspace, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(colorspace);
    if (!context) {
        CGImageRelease(image);
        return false;
    }

    CGContextTranslateCTM(context, 0, (CGFloat)height);
    CGContextScaleCTM(context, 1, -1);
    CGContextDrawImage(context, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height), image);
    CGContextRelease(context);
    CGImageRelease(image);

    bitmap->width = (int)width;
    bitmap->height = (int)height;
    bitmap->pixels.assign(width * height, 0);
    for (size_t y = 0; y < height; y++) {
        const uae_u8 *row = rgba.data() + y * width * 4;
        for (size_t x = 0; x < width; x++) {
            const uae_u8 *p = row + x * 4;
            const uae_u32 alpha = p[3];
            const uae_u32 red = p[0] + 255 - alpha;
            const uae_u32 green = p[1] + 255 - alpha;
            const uae_u32 blue = p[2] + 255 - alpha;
            bitmap->pixels[y * width + x] =
                ((red > 255 ? 255 : red) << 16) |
                ((green > 255 ? 255 : green) << 8) |
                (blue > 255 ? 255 : blue);
        }
    }
    return true;
}

static bool make_tiff(const UnixClipboardBitmap &bitmap, std::vector<uae_u8> *tiff)
{
    if (!tiff || bitmap.width <= 0 || bitmap.height <= 0 ||
        bitmap.pixels.size() < (size_t)bitmap.width * (size_t)bitmap.height) {
        return false;
    }

    const size_t width = bitmap.width;
    const size_t height = bitmap.height;
    std::vector<uae_u8> rgba(width * height * 4);
    for (size_t y = 0; y < height; y++) {
        uae_u8 *row = rgba.data() + y * width * 4;
        for (size_t x = 0; x < width; x++) {
            const uae_u32 pixel = bitmap.pixels[y * width + x];
            uae_u8 *p = row + x * 4;
            p[0] = pixel >> 16;
            p[1] = pixel >> 8;
            p[2] = pixel;
            p[3] = 255;
        }
    }

    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, rgba.data(), rgba.size(), NULL);
    if (!provider) {
        return false;
    }
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(width, height, 8, 32, width * 4, colorspace,
        kCGBitmapByteOrder32Big | kCGImageAlphaNoneSkipLast, provider, NULL, false,
        kCGRenderingIntentDefault);
    CGColorSpaceRelease(colorspace);
    CGDataProviderRelease(provider);
    if (!image) {
        return false;
    }

    CFMutableDataRef image_data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    if (!image_data) {
        CGImageRelease(image);
        return false;
    }
    CGImageDestinationRef destination = CGImageDestinationCreateWithData(image_data, CFSTR("public.tiff"), 1, NULL);
    if (!destination) {
        CFRelease(image_data);
        CGImageRelease(image);
        return false;
    }
    CGImageDestinationAddImage(destination, image, NULL);
    const bool ok = CGImageDestinationFinalize(destination);
    CFRelease(destination);
    CGImageRelease(image);
    if (!ok) {
        CFRelease(image_data);
        return false;
    }

    const UInt8 *bytes = CFDataGetBytePtr(image_data);
    const CFIndex length = CFDataGetLength(image_data);
    tiff->assign(bytes, bytes + length);
    CFRelease(image_data);
    return !tiff->empty();
}
#endif

static std::vector<uae_u8> make_iff_text(const std::string &host_text)
{
	const std::string amiga_text = host_text_to_amiga_text(host_text);
	std::vector<uae_u8> iff;
	const uae_u32 text_size = (uae_u32)amiga_text.size();
	const uae_u32 form_size = 4 + 8 + text_size + (text_size & 1);

	iff.reserve(form_size + 8);
	append_literal(&iff, "FORM");
	append_be32(&iff, form_size);
	append_literal(&iff, "FTXT");
	append_literal(&iff, "CHRS");
	append_be32(&iff, text_size);
	iff.insert(iff.end(), amiga_text.begin(), amiga_text.end());
	if (text_size & 1) {
		iff.push_back(0);
	}
	return iff;
}

static bool make_iff_ilbm(const UnixClipboardBitmap &bitmap, bool initial, std::vector<uae_u8> *iff)
{
	if (!iff || bitmap.width <= 0 || bitmap.height <= 0 ||
		bitmap.pixels.size() < (size_t)bitmap.width * (size_t)bitmap.height) {
		return false;
	}

	const size_t pixel_bytes = (size_t)bitmap.width * (size_t)bitmap.height * 4;
	if (pixel_bytes > (initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT)) {
		return false;
	}

	std::vector<uae_u32> colors;
	colors.reserve(256);
	for (uae_u32 pixel : bitmap.pixels) {
		size_t i = 0;
		for (; i < colors.size(); i++) {
			if (colors[i] == pixel) {
				break;
			}
		}
		if (i == colors.size()) {
			if (colors.size() >= 256) {
				colors.clear();
				break;
			}
			colors.push_back(pixel);
		}
	}

	int planes = 24;
	if (colors.size() >= 256) {
		colors.clear();
	}
	if (!colors.empty()) {
		planes = 0;
		for (size_t count = colors.size(); count > 0; count >>= 1) {
			planes++;
		}
		if (planes < 1) {
			planes = 1;
		}
	}

	const int width = bitmap.width;
	const int height = bitmap.height;
	const int plane_rowbytes = ((width + 15) & ~15) / 8;
	const uae_u32 body_size = (uae_u32)(plane_rowbytes * height * planes);

	iff->clear();
	append_literal(iff, "FORM");
	append_be32(iff, 0);
	append_literal(iff, "ILBM");
	append_literal(iff, "BMHD");
	append_be32(iff, 20);
	append_be16(iff, (uae_u16)width);
	append_be16(iff, (uae_u16)height);
	append_be16(iff, 0);
	append_be16(iff, 0);
	iff->push_back((uae_u8)planes);
	iff->push_back(0);
	iff->push_back(0);
	iff->push_back(0);
	append_be16(iff, 0);
	iff->push_back(1);
	iff->push_back(1);
	append_be16(iff, (uae_u16)width);
	append_be16(iff, (uae_u16)height);
	append_literal(iff, "CAMG");
	append_be32(iff, 4);
	uae_u32 camg = 0;
	if (width > 400) {
		camg |= 0x8000;
	}
	if (height > 300) {
		camg |= 0x0004;
	}
	append_be32(iff, camg);

	if (planes <= 8) {
		const int color_count = 1 << planes;
		append_literal(iff, "CMAP");
		append_be32(iff, color_count * 3);
		for (int i = 0; i < color_count; i++) {
			const uae_u32 color = i < (int)colors.size() ? colors[i] : 0;
			iff->push_back(color >> 16);
			iff->push_back(color >> 8);
			iff->push_back(color);
		}
		if ((color_count * 3) & 1) {
			iff->push_back(0);
		}
	}

	append_literal(iff, "BODY");
	append_be32(iff, body_size);
	const size_t body_offset = iff->size();
	iff->resize(body_offset + body_size, 0);
	uae_u8 *body = iff->data() + body_offset;

	for (int y = 0; y < height; y++) {
		if (planes <= 8) {
			for (int b = 0; b < planes; b++) {
				for (int x = 0; x < width; x++) {
					const uae_u32 pixel = bitmap.pixels[(size_t)y * (size_t)width + x];
					int color_index = 0;
					for (int i = 0; i < (int)colors.size(); i++) {
						if (colors[i] == pixel) {
							color_index = i;
							break;
						}
					}
					if (color_index & (1 << b)) {
						body[x / 8] |= 1 << (7 - (x & 7));
					}
				}
				body += plane_rowbytes;
			}
		} else {
			for (int component = 0; component < 3; component++) {
				for (int b = 0; b < 8; b++) {
					const int mask = 1 << (((2 - component) * 8) + b);
					for (int x = 0; x < width; x++) {
						const uae_u32 pixel = bitmap.pixels[(size_t)y * (size_t)width + x];
						if (pixel & mask) {
							body[x / 8] |= 1 << (7 - (x & 7));
						}
					}
					body += plane_rowbytes;
				}
			}
		}
	}

	const uae_u32 form_size = (uae_u32)iff->size() - 8;
	(*iff)[4] = form_size >> 24;
	(*iff)[5] = form_size >> 16;
	(*iff)[6] = form_size >> 8;
	(*iff)[7] = form_size;
	if (form_size & 1) {
		iff->push_back(0);
	}
	return true;
}

static void to_amiga_start(void)
{
	to_amiga_phase = 0;
	if (!initialized || !clipboard_data || to_amiga.empty()) {
		return;
	}
	to_amiga_phase = 1;
}

static void queue_host_text_to_amiga(const std::string &text)
{
	to_amiga = make_iff_text(text);
	to_amiga_start();
}

static void queue_host_bitmap_to_amiga(const UnixClipboardBitmap &bitmap, bool initial)
{
	if (make_iff_ilbm(bitmap, initial, &to_amiga)) {
		to_amiga_start();
	}
}

static int parse_csi(const std::string &text, size_t offset)
{
	while (offset < text.size()) {
		if ((uae_u8)text[offset] >= 0x40) {
			break;
		}
		offset++;
	}
	return (int)offset;
}

static std::string amiga_text_to_host_text(const std::string &text)
{
	std::string converted;
	converted.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		uae_u8 c = (uae_u8)text[i];
		if (c == 0 && i + 1 < text.size()) {
			continue;
		}
		if (c == 0x9b) {
			i = parse_csi(text, i + 1);
			continue;
		}
		if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
			i = parse_csi(text, i + 2);
			continue;
		}
		if (c == '\r') {
			converted.push_back('\n');
			if (i + 1 < text.size() && text[i + 1] == '\n') {
				i++;
			}
			continue;
		}
		converted.push_back((char)c);
	}
	return converted;
}

static void clipboard_put_text(const std::string &text)
{
	if (!write_host_clipboard_text(text)) {
		write_log(_T("clipboard: failed to write host clipboard text\n"));
	}
}

static void from_iff_text(const uae_u8 *addr, uae_u32 len)
{
	if (len < 12 || memcmp(addr, "FORM", 4) || memcmp(addr + 8, "FTXT", 4)) {
		return;
	}

	std::string text;
	uae_u32 offset = 12;
	while (offset + 8 <= len) {
		const uae_u8 *chunk = addr + offset;
		uae_u32 csize = ((uae_u32)chunk[4] << 24) | ((uae_u32)chunk[5] << 16) | ((uae_u32)chunk[6] << 8) | chunk[7];
		offset += 8;
		if (csize > len - offset) {
			break;
		}
		if (!memcmp(chunk, "CHRS", 4)) {
			text.append((const char *)(addr + offset), csize);
		}
		offset += csize + (csize & 1);
	}

	clipboard_put_text(amiga_text_to_host_text(text));
}

static bool decompress_byterun1(const uae_u8 *src, const uae_u8 *end, std::vector<uae_u8> *out, size_t expected)
{
	out->clear();
	out->reserve(expected);
	while (src < end && out->size() < expected) {
		const int8_t control = (int8_t)*src++;
		if (control >= 0) {
			const int count = control + 1;
			if (src + count > end) {
				return false;
			}
			const size_t writable = std::min((size_t)count, expected - out->size());
			out->insert(out->end(), src, src + writable);
			src += count;
		} else if (control >= -127) {
			if (src >= end) {
				return false;
			}
			const int count = -control + 1;
			const uae_u8 value = *src++;
			const size_t writable = std::min((size_t)count, expected - out->size());
			out->insert(out->end(), writable, value);
		}
	}
	return out->size() == expected;
}

static bool parse_iff_ilbm(const uae_u8 *addr, uae_u32 len, UnixClipboardBitmap *bitmap)
{
	if (!bitmap || len < 12 || memcmp(addr, "FORM", 4) || memcmp(addr + 8, "ILBM", 4)) {
		return false;
	}
	if (read_be32(addr + 4) > 0xffffff) {
		return false;
	}

	int width = 0;
	int height = 0;
	int planes = 0;
	int masking = 0;
	int compression = 0;
	uae_u32 camg = 0;
	uae_u32 palette[256];
	for (int i = 0; i < 256; i++) {
		palette[i] = ((uae_u32)i << 16) | ((uae_u32)i << 8) | i;
	}
	bool have_bmhd = false;
	const uae_u8 *body = NULL;
	uae_u32 body_size = 0;

	uae_u32 offset = 12;
	while (offset + 8 <= len) {
		const uae_u8 *chunk = addr + offset;
		const uae_u32 csize = read_be32(chunk + 4);
		offset += 8;
		if (csize > len - offset) {
			return false;
		}
		const uae_u8 *data = addr + offset;
		if (!memcmp(chunk, "BMHD", 4) && csize >= 20) {
			width = read_be16(data);
			height = read_be16(data + 2);
			planes = data[8];
			masking = data[9];
			compression = data[10];
			have_bmhd = true;
		} else if (!memcmp(chunk, "CAMG", 4) && csize >= 4) {
			camg = read_be32(data);
			if ((camg & 0xffff0000) && !(camg & 0x1000)) {
				camg = 0;
			}
		} else if (!memcmp(chunk, "CMAP", 4)) {
			const int colors = std::min<int>(256, csize / 3);
			bool zero4 = true;
			for (int i = 0; i < colors; i++) {
				const uae_u8 r = data[(size_t)i * 3 + 0];
				const uae_u8 g = data[(size_t)i * 3 + 1];
				const uae_u8 b = data[(size_t)i * 3 + 2];
				if ((r & 0x0f) || (g & 0x0f) || (b & 0x0f)) {
					zero4 = false;
				}
				palette[i] = ((uae_u32)r << 16) | ((uae_u32)g << 8) | b;
			}
			if (zero4) {
				for (int i = 0; i < colors; i++) {
					const uae_u8 r = ((palette[i] >> 16) & 0xff) | ((palette[i] >> 20) & 0x0f);
					const uae_u8 g = ((palette[i] >> 8) & 0xff) | ((palette[i] >> 12) & 0x0f);
					const uae_u8 b = (palette[i] & 0xff) | ((palette[i] >> 4) & 0x0f);
					palette[i] = ((uae_u32)r << 16) | ((uae_u32)g << 8) | b;
				}
			}
		} else if (!memcmp(chunk, "BODY", 4)) {
			body = data;
			body_size = csize;
		}
		offset += csize + (csize & 1);
	}

	if (!have_bmhd || !body || width <= 0 || height <= 0 || planes <= 0 || planes > 24) {
		return false;
	}
	if (planes > 8 && planes != 24 && !((camg & 0x0800) && planes > 4)) {
		return false;
	}

	const int plane_rowbytes = ((width + 15) & ~15) / 8;
	const int stored_planes = planes + (masking == 1 ? 1 : 0);
	const size_t decoded_size = (size_t)plane_rowbytes * (size_t)height * (size_t)stored_planes;
	std::vector<uae_u8> decoded;
	if (compression) {
		if (!decompress_byterun1(body, body + body_size, &decoded, decoded_size)) {
			return false;
		}
		body = decoded.data();
		body_size = (uae_u32)decoded.size();
	} else if (body_size < decoded_size) {
		return false;
	}

	const bool ham = (camg & 0x0800) && planes > 4;
	const bool ehb = !(camg & (0x0800 | 0x0400 | 0x8000 | 0x0040)) && (camg & 0x0080) && planes == 6;
	bitmap->width = width;
	bitmap->height = height;
	bitmap->pixels.assign((size_t)width * (size_t)height, 0);

	for (int y = 0; y < height; y++) {
		const uae_u8 *row = body + (size_t)y * (size_t)stored_planes * (size_t)plane_rowbytes;
		uae_u32 ham_lastcolor = 0;
		for (int x = 0; x < width; x++) {
			int value = 0;
			for (int b = 0; b < planes; b++) {
				const int off = x / 8;
				const int mask = 1 << (7 - (x & 7));
				if (row[(size_t)b * plane_rowbytes + off] & mask) {
					value |= 1 << b;
				}
			}
			uae_u32 pixel = 0;
			if (ham) {
				if (planes >= 7) {
					const uae_u32 c = value & 0x3f;
					switch (value >> 6)
					{
						case 0: ham_lastcolor = palette[value & 0x3f]; break;
						case 1: ham_lastcolor = (ham_lastcolor & 0xffff00) | (c << 2); break;
						case 2: ham_lastcolor = (ham_lastcolor & 0x00ffff) | (c << 18); break;
						case 3: ham_lastcolor = (ham_lastcolor & 0xff00ff) | (c << 10); break;
					}
				} else {
					const uae_u32 c = value & 0x0f;
					switch (value >> 4)
					{
						case 0: ham_lastcolor = palette[value & 0x0f]; break;
						case 1: ham_lastcolor = (ham_lastcolor & 0xffff00) | (c << 4); break;
						case 2: ham_lastcolor = (ham_lastcolor & 0x00ffff) | (c << 20); break;
						case 3: ham_lastcolor = (ham_lastcolor & 0xff00ff) | (c << 12); break;
					}
				}
				pixel = ham_lastcolor;
			} else if (planes <= 8) {
				if (ehb && value >= 32) {
					const uae_u32 base = palette[value - 32];
					pixel = (((base >> 17) & 0x7f) << 16) | (((base >> 9) & 0x7f) << 8) | ((base >> 1) & 0x7f);
				} else {
					pixel = palette[value & 0xff];
				}
			} else {
				uae_u8 r = 0, g = 0, b = 0;
				for (int bit = 0; bit < 8; bit++) {
					const int off = x / 8;
					const int mask = 1 << (7 - (x & 7));
					if (row[(size_t)bit * plane_rowbytes + off] & mask) {
						r |= 1 << bit;
					}
					if (row[(size_t)(8 + bit) * plane_rowbytes + off] & mask) {
						g |= 1 << bit;
					}
					if (row[(size_t)(16 + bit) * plane_rowbytes + off] & mask) {
						b |= 1 << bit;
					}
				}
				pixel = ((uae_u32)r << 16) | ((uae_u32)g << 8) | b;
			}
			bitmap->pixels[(size_t)y * (size_t)width + x] = pixel;
		}
	}
	return true;
}

static void from_iff(TrapContext *ctx, uaecptr data, uae_u32 len)
{
	if (len < 12 || !trap_valid_address(ctx, data, len)) {
		return;
	}

	std::vector<uae_u8> buffer((len + 3) & ~3);
	trap_get_bytes(ctx, buffer.data(), data, (len + 3) & ~3);
	if (!memcmp(buffer.data(), "FORM", 4) && !memcmp(buffer.data() + 8, "FTXT", 4)) {
		from_iff_text(buffer.data(), len);
	} else if (!memcmp(buffer.data(), "FORM", 4) && !memcmp(buffer.data() + 8, "ILBM", 4)) {
#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
		UnixClipboardBitmap bitmap;
		std::vector<uae_u8> bmp;
		if (parse_iff_ilbm(buffer.data(), len, &bitmap) && make_bmp(bitmap, &bmp)) {
			std::vector<uae_u8> png;
#ifdef UAE_UNIX_CLIPBOARD_PNG
			make_png(bitmap, &png);
#endif
			std::vector<uae_u8> tiff;
#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
			make_tiff(bitmap, &tiff);
#endif
			write_host_clipboard_image(bmp, png.empty() ? NULL : &png, tiff.empty() ? NULL : &tiff);
		}
#endif
	}
}

static void clipboard_read_host(TrapContext *, bool keyboardinject, bool initial)
{
	if (clip_disabled || (!keyboardinject && to_amiga_phase)) {
		return;
	}

	std::string text;
	if (read_host_clipboard_text(&text, initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT)) {
		if (keyboardinject) {
			normalize_text_for_keybuf(&text);
			if (!text.empty()) {
				keybuf_inject(text.c_str());
			}
			return;
		}

		if (last_host_clipboard_valid && text == last_host_clipboard) {
			return;
		}
		last_host_clipboard = text;
		last_host_clipboard_valid = true;
		last_host_clipboard_bitmap.clear();
		last_host_clipboard_bitmap_valid = false;
		queue_host_text_to_amiga(text);
		return;
	}

#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
	if (!keyboardinject) {
		UnixClipboardBitmap bitmap;
		std::vector<uae_u8> image;
#ifdef UAE_UNIX_CLIPBOARD_PNG
		if (read_host_clipboard_png(&image, initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT) && parse_png(image, &bitmap)) {
			if (last_host_clipboard_bitmap_valid && image == last_host_clipboard_bitmap) {
				return;
			}
			last_host_clipboard.clear();
			last_host_clipboard_valid = false;
			last_host_clipboard_bitmap = image;
			last_host_clipboard_bitmap_valid = true;
			queue_host_bitmap_to_amiga(bitmap, initial);
			return;
		}
#endif
#ifdef UAE_UNIX_CLIPBOARD_IMAGEIO
		if (read_host_clipboard_tiff(&image, initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT) && parse_imageio_bitmap(image, &bitmap)) {
			if (last_host_clipboard_bitmap_valid && image == last_host_clipboard_bitmap) {
				return;
			}
			last_host_clipboard.clear();
			last_host_clipboard_valid = false;
			last_host_clipboard_bitmap = image;
			last_host_clipboard_bitmap_valid = true;
			queue_host_bitmap_to_amiga(bitmap, initial);
			return;
		}
#endif
		if (read_host_clipboard_bmp(&image, initial ? CLIP_SIZE_LIMIT_INIT : CLIP_SIZE_LIMIT) && parse_bmp(image, &bitmap)) {
			if (last_host_clipboard_bitmap_valid && image == last_host_clipboard_bitmap) {
				return;
			}
			last_host_clipboard.clear();
			last_host_clipboard_valid = false;
			last_host_clipboard_bitmap = image;
			last_host_clipboard_bitmap_valid = true;
			queue_host_bitmap_to_amiga(bitmap, initial);
		}
	}
#endif
}

static uae_u32 to_amiga_start_cb(TrapContext *ctx, void *)
{
	if (!clipboard_data || to_amiga.empty() || trap_get_long(ctx, clipboard_data) != 0) {
		return 0;
	}
	trap_put_long(ctx, clipboard_data, (uae_u32)to_amiga.size());
	uae_Signal(trap_get_long(ctx, clipboard_data + 8), 1 << 13);
	to_amiga_phase = 2;
	return 1;
}

static uae_u32 clipboard_vsync_cb(TrapContext *ctx, void *)
{
	if (clipboard_data) {
		uaecptr task = trap_get_long(ctx, clipboard_data + 8);
		if (task && native2amiga_isfree()) {
			uae_Signal(task, 1 << 13);
		}
	}
	return 0;
}

void clipboard_vsync(void)
{
	if (!filesys_heartbeat() || !clipboard_data || !initialized) {
		return;
	}

	if (signaling) {
		vdelay--;
		if (vdelay <= 0) {
			trap_callback(clipboard_vsync_cb, NULL);
			vdelay = 50;
		}
	}

	if (vdelay2 > 0) {
		vdelay2--;
	}

	if (to_amiga_phase == 1 && vdelay2 <= 0) {
		trap_callback(to_amiga_start_cb, NULL);
	}

	if (host_poll_delay > 0) {
		host_poll_delay--;
	} else {
		host_poll_delay = 100;
		clipboard_read_host(NULL, false, false);
	}
}

void clipboard_host_changed(void)
{
	host_poll_delay = 0;
}

void clipboard_unsafeperiod(void)
{
	vdelay2 = 100;
	if (vdelay < 60) {
		vdelay = 60;
	}
}

void clipboard_disable(bool disabled)
{
	clip_disabled = disabled;
}

uaecptr amiga_clipboard_proc_start(TrapContext *)
{
	signaling = 1;
	to_amiga_start();
	return clipboard_data;
}

void amiga_clipboard_task_start(TrapContext *, uaecptr data)
{
	clipboard_data = data;
	signaling = 1;
	write_log(_T("clipboard task init: %08x\n"), clipboard_data);
	to_amiga_start();
}

int amiga_clipboard_want_data(TrapContext *ctx)
{
	if (!clipboard_data) {
		return 0;
	}

	uae_u32 addr = trap_get_long(ctx, clipboard_data + 4);
	uae_u32 size = trap_get_long(ctx, clipboard_data);

	if (!initialized || to_amiga.empty()) {
		to_amiga.clear();
		to_amiga_phase = 0;
		return 0;
	}
	if (size != to_amiga.size()) {
		write_log(_T("clipboard: size %d <> %d mismatch\n"), size, (int)to_amiga.size());
		to_amiga.clear();
		to_amiga_phase = 0;
		return 0;
	}
	if (addr && size) {
		trap_put_bytes(ctx, to_amiga.data(), addr, size);
	}
	to_amiga.clear();
	to_amiga_phase = 0;
	return 1;
}

void amiga_clipboard_got_data(TrapContext *ctx, uaecptr data, uae_u32, uae_u32 actual)
{
	if (!initialized) {
		return;
	}
	from_iff(ctx, data, actual);
}

void amiga_clipboard_die(TrapContext *)
{
	signaling = 0;
	write_log(_T("clipboard not initialized\n"));
}

void amiga_clipboard_init(TrapContext *ctx)
{
	signaling = 0;
	initialized = 1;
	host_poll_delay = 50;
	write_log(_T("clipboard initialized\n"));
	clipboard_read_host(ctx, false, true);
}

void target_paste_to_keyboard(void)
{
	clipboard_read_host(NULL, true, false);
}
