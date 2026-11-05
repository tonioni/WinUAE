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
#include "clipboard_common.h"
#include "keybuf.h"
#include "native2amiga_api.h"
#include "uae.h"

extern bool filesys_heartbeat(void);

static clipboard_transfer transfer;
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

static bool parse_bmp(const std::vector<uae_u8> &bmp, clipboard_bitmap *bitmap)
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

static bool make_bmp(const clipboard_bitmap &bitmap, std::vector<uae_u8> *bmp)
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
static bool parse_png(const std::vector<uae_u8> &png, clipboard_bitmap *bitmap)
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

static bool make_png(const clipboard_bitmap &bitmap, std::vector<uae_u8> *png)
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
static bool parse_imageio_bitmap(const std::vector<uae_u8> &data, clipboard_bitmap *bitmap)
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

static bool make_tiff(const clipboard_bitmap &bitmap, std::vector<uae_u8> *tiff)
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

static void queue_host_text_to_amiga(const std::string &text)
{
	transfer.pending = clipboard_encode_text(text);
	transfer.start();
}

static void queue_host_bitmap_to_amiga(const clipboard_bitmap &bitmap, bool initial)
{
	if (clipboard_encode_bitmap(bitmap, initial, &transfer.pending)) {
		transfer.start();
	}
}

static void clipboard_put_text(const std::string &text)
{
	if (!write_host_clipboard_text(text)) {
		write_log(_T("clipboard: failed to write host clipboard text\n"));
	}
}

static void from_iff_text(const uae_u8 *addr, uae_u32 len)
{
	std::string text;
	if (clipboard_decode_text(addr, len, &text, false)) {
		clipboard_put_text(text);
	}
}

static void from_iff(TrapContext *ctx, uaecptr data, uae_u32 len)
{
	if (len < 12 || !trap_valid_address(ctx, data, len)) {
		return;
	}

	std::vector<uae_u8> buffer(len);
	trap_get_bytes(ctx, buffer.data(), data, len);
	if (!memcmp(buffer.data(), "FORM", 4) && !memcmp(buffer.data() + 8, "FTXT", 4)) {
		from_iff_text(buffer.data(), len);
	} else if (!memcmp(buffer.data(), "FORM", 4) && !memcmp(buffer.data() + 8, "ILBM", 4)) {
#ifdef UAE_UNIX_WITH_SDL3_CLIPBOARD_DATA
		clipboard_bitmap bitmap;
		std::vector<uae_u8> bmp;
		if (clipboard_decode_bitmap(buffer.data(), len, &bitmap) && make_bmp(bitmap, &bmp)) {
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
	if (clip_disabled || (!keyboardinject && transfer.phase)) {
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
		clipboard_bitmap bitmap;
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

void clipboard_vsync(void)
{
	if (!filesys_heartbeat() || !transfer.guest_data) {
		return;
	}

	// The guest waits for this signal before opening clipboard.device.
	transfer.vsync(false);
	if (!transfer.initialized) {
		return;
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
	transfer.unsafe_period();
}

void clipboard_disable(bool disabled)
{
	clip_disabled = disabled;
}

uaecptr amiga_clipboard_proc_start(TrapContext *)
{
	transfer.signaling = 1;
	transfer.start();
	return transfer.guest_data;
}

void amiga_clipboard_task_start(TrapContext *, uaecptr data)
{
	transfer.guest_data = data;
	transfer.signaling = 1;
	write_log(_T("clipboard task init: %08x\n"), transfer.guest_data);
	transfer.start();
}

int amiga_clipboard_want_data(TrapContext *ctx)
{
	return transfer.want_data(ctx);
}

void amiga_clipboard_got_data(TrapContext *ctx, uaecptr data, uae_u32, uae_u32 actual)
{
	if (!transfer.initialized) {
		return;
	}
	from_iff(ctx, data, actual);
}

void amiga_clipboard_die(TrapContext *)
{
	transfer.signaling = 0;
	write_log(_T("clipboard not initialized\n"));
}

void amiga_clipboard_init(TrapContext *ctx)
{
	transfer.signaling = 0;
	transfer.initialized = 1;
	host_poll_delay = 50;
	write_log(_T("clipboard initialized\n"));
	clipboard_read_host(ctx, false, true);
}

void target_paste_to_keyboard(void)
{
	clipboard_read_host(NULL, true, false);
}
