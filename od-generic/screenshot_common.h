#ifndef OD_GENERIC_SCREENSHOT_COMMON_H
#define OD_GENERIC_SCREENSHOT_COMMON_H

/* Screenshot logic shared by the host screenshot writers. */

struct screenshot_crop
{
	/* Captured area, in source pixels. */
	int width, height;
	/* Where the source goes in the captured area: a positive value
	 * offsets the destination, a negative value skips source pixels. */
	int xoffset, yoffset;
	/* Output scaling, 1 to 8. */
	int xmult, ymult;
};

/* Computes the captured area for a width x height source image whose
 * visible part starts at dx/dy. 'standard' ignores the screenshot_*
 * size and scaling settings. 'clip' crops to the displayed chipset area
 * (screenshot clip mode 1). */
void screenshot_get_crop(struct screenshot_crop *sc, int width, int height, int dx, int dy, bool standard, bool clip);

/* File name prefix: the DF0: or CD image name without path and
 * extension, or an empty string. 'name' must hold MAX_DPATH characters. */
void screenshot_get_name(TCHAR *name);

/* Builds path + name + "_" + number + "." + ext. Returns false when
 * 'number' is past the last screenshot number. */
bool screenshot_get_filename(TCHAR *out, int size, const TCHAR *path, const TCHAR *name, int number, bool multi, const TCHAR *ext);


#include <vector>

struct screenshot_palette
{
	uae_u32 colors[256];
	int count, depth;
	std::vector<uae_u8> indices;
};

typedef uae_u32 (*screenshot_read_pixel)(void *context, int x, int y);

// Colors are opaque 24-bit keys; each writer retains its channel order.
// Preferred colors retain their register slots. Unmatched colors fill from
// reserved onward, wrapping to unused lower slots when necessary.
// With no preferred colors or reservation, use first-encounter order.
bool screenshot_build_palette(screenshot_palette *out, int width, int height,
	screenshot_read_pixel read_pixel, void *context,
	const uae_u32 *preferred, int preferred_count, int reserved);

#endif /* OD_GENERIC_SCREENSHOT_COMMON_H */
