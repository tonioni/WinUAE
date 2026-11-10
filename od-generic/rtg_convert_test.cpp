/*
 * Pins the shared RTG host conversion selection and YUV decoding.
 *
 * getconvert() maps each RTG pixel format to the converter that writes the
 * 32-bit host surface; the uaegfx display, overlay and Cirrus video window
 * all depend on these values. p96_yuvtorgb() must stay identical to the
 * copies the Windows and Unix hosts used before it was shared.
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "picasso96.h"

#include <cstdio>

static int failures;

static void expect(bool ok, const char *what, int a, int b, int c, int got, int want)
{
	if (!ok) {
		failures++;
		if (failures <= 20) {
			std::printf("FAIL %s (%d, %d, %d): got %d, want %d\n", what, a, b, c, got, want);
		}
	}
}

// The formula both hosts used before the conversion was shared.
static uae_u16 reference_yuvtorgb(uae_u8 yx, uae_u8 ux, uae_u8 vx)
{
	int y = yx - 16;
	int u = ux - 128;
	int v = vx - 128;
	int r = (298 * y + 409 * v + 128) >> (8 + 3);
	int g = (298 * y - 100 * u - 208 * v + 128) >> (8 + 3);
	int b = (298 * y + 516 * u + 128) >> (8 + 3);
	r = r < 0 ? 0 : (r > 31 ? 31 : r);
	g = g < 0 ? 0 : (g > 31 ? 31 : g);
	b = b < 0 ? 0 : (b > 31 ? 31 : b);
	return (uae_u16)((r << 10) | (g << 5) | b);
}

int main()
{
	static const struct {
		int format;
		int convert;
	} table[] = {
		{ RGBFB_NONE, 0 },
		{ RGBFB_CLUT, RGBFB_CLUT_RGBFB_32 },
		{ RGBFB_R8G8B8, RGBFB_R8G8B8_32 },
		{ RGBFB_B8G8R8, RGBFB_B8G8R8_32 },
		{ RGBFB_R5G6B5PC, RGBFB_R5G6B5PC_32 },
		{ RGBFB_R5G5B5PC, RGBFB_R5G5B5PC_32 },
		{ RGBFB_A8R8G8B8, RGBFB_A8R8G8B8_32 },
		{ RGBFB_A8B8G8R8, RGBFB_A8B8G8R8_32 },
		{ RGBFB_R8G8B8A8, RGBFB_R8G8B8A8_32 },
		{ RGBFB_B8G8R8A8, RGBFB_B8G8R8A8_32 },
		{ RGBFB_R5G6B5, RGBFB_R5G6B5_32 },
		{ RGBFB_R5G5B5, RGBFB_R5G5B5_32 },
		{ RGBFB_B5G6R5PC, RGBFB_B5G6R5PC_32 },
		{ RGBFB_B5G5R5PC, RGBFB_B5G5R5PC_32 },
		{ RGBFB_Y4U2V2, RGBFB_Y4U2V2_32 },
		{ RGBFB_Y4U1V1, RGBFB_Y4U1V1_32 },
		{ RGBFB_MaxFormats, 0 },
		{ -1, 0 },
		{ 255, 0 },
	};
	static_assert(RGBFB_MaxFormats == 16, "RGBFTYPE changed");
	for (const auto &t : table) {
		expect(getconvert(t.format) == t.convert, "getconvert", t.format, 0, 0, getconvert(t.format), t.convert);
	}

	// Converter numbers are stored in state and compared by the hosts.
	static_assert(RGBFB_A8R8G8B8_32 == 1, "converter ids");
	static_assert(RGBFB_B5G5R5PC_32 == 12, "converter ids");
	static_assert(RGBFB_CLUT_RGBFB_32 == 13, "converter ids");
	static_assert(RGBFB_Y4U2V2_32 == 14, "converter ids");
	static_assert(RGBFB_Y4U1V1_32 == 15, "converter ids");

	// BT.601 limited range reference colours, as 0RRRRRGGGGGBBBBB.
	static const struct {
		int y, u, v, rgb;
	} colours[] = {
		{ 16, 128, 128, 0x0000 },
		{ 235, 128, 128, 0x7fff },
		{ 128, 128, 128, 0x4210 },
		{ 81, 90, 240, 0x7c00 },
		{ 145, 54, 34, 0x03e0 },
		{ 41, 240, 110, 0x001f },
		{ 0, 0, 0, 0x0200 },
		{ 255, 255, 255, 0x7dff },
	};
	for (const auto &c : colours) {
		int got = p96_yuvtorgb((uae_u8)c.y, (uae_u8)c.u, (uae_u8)c.v);
		expect(got == c.rgb, "p96_yuvtorgb", c.y, c.u, c.v, got, c.rgb);
	}

	for (int y = 0; y < 256; y++) {
		for (int u = 0; u < 256; u++) {
			for (int v = 0; v < 256; v++) {
				int got = p96_yuvtorgb((uae_u8)y, (uae_u8)u, (uae_u8)v);
				int want = reference_yuvtorgb((uae_u8)y, (uae_u8)u, (uae_u8)v);
				expect(got == want, "p96_yuvtorgb exhaustive", y, u, v, got, want);
			}
		}
	}

	if (failures) {
		std::printf("%d failures\n", failures);
		return 1;
	}
	std::printf("picasso96 conversion: all checks passed\n");
	return 0;
}
