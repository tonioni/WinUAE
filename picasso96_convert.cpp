/*
* UAE - The Un*x Amiga Emulator
*
* Picasso96 pixel format conversion shared by the host display code.
*
* getconvert() selects the converter used to copy an RTG pixel format
* to the 32-bit host surface. The same selection is used for the uaegfx
* display, the uaegfx overlay and the Cirrus video window.
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef PICASSO96

#include "options.h"
#include "picasso96.h"

int getconvert(int rgbformat)
{
	int v = 0;

	switch (rgbformat)
	{
	case RGBFB_CLUT:
		v = RGBFB_CLUT_RGBFB_32;
		break;

	case RGBFB_B5G6R5PC:
		v = RGBFB_B5G6R5PC_32;
		break;
	case RGBFB_R5G6B5PC:
		v = RGBFB_R5G6B5PC_32;
		break;

	case RGBFB_R5G5B5PC:
		v = RGBFB_R5G5B5PC_32;
		break;
	case RGBFB_R5G6B5:
		v = RGBFB_R5G6B5_32;
		break;
	case RGBFB_R5G5B5:
		v = RGBFB_R5G5B5_32;
		break;
	case RGBFB_B5G5R5PC:
		v = RGBFB_B5G5R5PC_32;
		break;

	case RGBFB_A8R8G8B8:
		v = RGBFB_A8R8G8B8_32;
		break;
	case RGBFB_R8G8B8:
		v = RGBFB_R8G8B8_32;
		break;
	case RGBFB_B8G8R8:
		v = RGBFB_B8G8R8_32;
		break;
	case RGBFB_A8B8G8R8:
		v = RGBFB_A8B8G8R8_32;
		break;
	case RGBFB_B8G8R8A8:
		v = RGBFB_B8G8R8A8_32;
		break;
	case RGBFB_R8G8B8A8:
		v = RGBFB_R8G8B8A8_32;
		break;

	case RGBFB_Y4U2V2:
		v = RGBFB_Y4U2V2_32;
		break;
	case RGBFB_Y4U1V1:
		v = RGBFB_Y4U1V1_32;
		break;

	}
	return v;
}

// BT.601 limited range YUV to a 15-bit 0RRRRRGGGGGBBBBB value, which the
// callers expand through the R5G5B5 lookup table.
uae_u16 p96_yuvtorgb(uae_u8 yx, uae_u8 ux, uae_u8 vx)
{
	int y = yx - 16;
	int u = ux - 128;
	int v = vx - 128;
	int r = (298 * y + 409 * v + 128) >> (8 + 3);
	int g = (298 * y - 100 * u - 208 * v + 128) >> (8 + 3);
	int b = (298 * y + 516 * u + 128) >> (8 + 3);
	if (r < 0) {
		r = 0;
	}
	if (r > 31) {
		r = 31;
	}
	if (g < 0) {
		g = 0;
	}
	if (g > 31) {
		g = 31;
	}
	if (b < 0) {
		b = 0;
	}
	if (b > 31) {
		b = 31;
	}
	return (r << 10) | (g << 5) | b;
}

#endif /* PICASSO96 */
