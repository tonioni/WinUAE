#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <assert.h>

#include "options.h"
#include "xwin.h"
#include "custom.h"
#include "drawing.h"
#include "memory.h"
#include "specialmonitors.h"
#include "debug.h"
#include "zfile.h"

static bool automatic;
static int monitor;

extern unsigned int bplcon0;
extern uae_u8 **row_map_genlock;

static uae_u8 graffiti_palette[256 * 4];

STATIC_INLINE bool is_transparent(uae_u8 v)
{
	return v == 0;
}

STATIC_INLINE uae_u8 FVR(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return dataline[2];
	else
		return ((((uae_u16*)dataline)[0] >> 11) & 31) << 3;
}
STATIC_INLINE uae_u8 FVG(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return dataline[1];
	else
		return ((((uae_u16*)dataline)[0] >> 5) & 63) << 2;
}
STATIC_INLINE uae_u8 FVB(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return dataline[0];
	else
		return ((((uae_u16*)dataline)[0] >> 0) & 31) << 2;
}

STATIC_INLINE bool FR(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return (dataline[2] & 0x80) != 0;
	else
		return ((dataline[1] >> 7) & 1) != 0;
}
STATIC_INLINE bool FG(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return (dataline[1] & 0x80) != 0;
	else
		return ((dataline[1] >> 2) & 1) != 0;
}
STATIC_INLINE bool FB(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return (dataline[0] & 0x80) != 0;
	else
		return ((dataline[0] >> 4) & 1) != 0;
}
STATIC_INLINE bool FI(struct vidbuffer *src, uae_u8 *dataline)
{
	if (src->pixbytes == 4)
		return (dataline[0] & 0x10) != 0;
	else
		return ((dataline[0] >> 1) & 1) != 0;
}

STATIC_INLINE uae_u8 FIRGB(struct vidbuffer *src, uae_u8 *dataline)
{
	uae_u8 v = 0;
#if 1
	if (FI(src, dataline))
		v |= 1;
	if (FR(src, dataline))
		v |= 8;
	if (FG(src, dataline))
		v |= 4;
	if (FB(src, dataline))
		v |= 2;
#else
	if (FI(src, dataline))
		v |= 1 << scramble[scramble_counter * 4 + 0];
	if (FR(src, dataline))
		v |= 1 << scramble[scramble_counter * 4 + 1];
	if (FG(src, dataline))
		v |= 1 << scramble[scramble_counter * 4 + 2];
	if (FB(src, dataline))
		v |= 1 << scramble[scramble_counter * 4 + 3];
#endif
	return v;
}

STATIC_INLINE uae_u8 DCTV_FIRBG(struct vidbuffer *src, uae_u8 *dataline)
{
	uae_u8 v = 0;
	if (FI(src, dataline))
		v |= 0x40;
	if (FR(src, dataline))
		v |= 0x10;
	if (FB(src, dataline))
		v |= 0x04;
	if (FG(src, dataline))
		v |= 0x01;
	return v;
}

STATIC_INLINE void PRGB(struct vidbuffer *dst, uae_u8 *dataline, uae_u8 r, uae_u8 g, uae_u8 b)
{
	if (dst->pixbytes == 4) {
		dataline[0] = b;
		dataline[1] = g;
		dataline[2] = r;
	} else {
		r >>= 3;
		g >>= 2;
		b >>= 3;
		((uae_u16*)dataline)[0] = (r << 11) | (g << 5) | b;
	}
}

STATIC_INLINE void PUT_PRGB(uae_u8 *d, uae_u8 *d2, struct vidbuffer *dst, uae_u8 r, uae_u8 g, uae_u8 b, int xadd, int doublelines, bool hdouble)
{
	if (hdouble)
		PRGB(dst, d - dst->pixbytes, r, g, b);
	PRGB(dst, d, r, g, b);
	if (xadd >= 2) {
		PRGB(dst, d + 1 * dst->pixbytes, r, g, b);
		if (hdouble)
			PRGB(dst, d + 2 * dst->pixbytes, r, g, b);
	}
	if (doublelines) {
		if (hdouble)
			PRGB(dst, d2 - dst->pixbytes, r, g, b);
		PRGB(dst, d2, r, g, b);
		if (xadd >= 2) {
			PRGB(dst, d2 + 1 * dst->pixbytes, r, g, b);
			if (hdouble)
				PRGB(dst, d2 + 2 * dst->pixbytes, r, g, b);
		}
	}
}

STATIC_INLINE void PUT_AMIGARGB(uae_u8 *d, uae_u8 *s, uae_u8 *d2, uae_u8 *s2, struct vidbuffer *dst, int xadd, int doublelines, bool hdouble)
{
	if (dst->pixbytes == 4) {
		if (hdouble)
			((uae_u32*)d)[-1] = ((uae_u32*)s)[-1];
		((uae_u32*)d)[0] = ((uae_u32*)s)[0];
	} else {
		if (hdouble)
			((uae_u16*)d)[-1] = ((uae_u16*)s)[-1];
		((uae_u16*)d)[0] = ((uae_u16*)s)[0];
	}
	if (doublelines) {
		if (dst->pixbytes == 4) {
			if (hdouble)
				((uae_u32*)d2)[-1] = ((uae_u32*)s2)[-1];
			((uae_u32*)d2)[0] = ((uae_u32*)s2)[0];
		} else {
			if (hdouble)
				((uae_u16*)d2)[-1] = ((uae_u16*)s2)[-1];
			((uae_u16*)d2)[0] = ((uae_u16*)s2)[0];
		}
	}
}

static void clearmonitor(struct vidbuffer *dst)
{
	uae_u8 *p = dst->bufmem;
	for (int y = 0; y < dst->height_allocated; y++) {
		memset(p, 0, dst->width_allocated * dst->pixbytes);
		p += dst->rowbytes;
	}
}

static void blank_generic(struct vidbuffer *src, struct vidbuffer *dst, int oddlines)
{
	int y, vdbl;
	int ystart, yend, isntsc;

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	vdbl = gfxvidinfo.ychange;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	for (y = ystart; y < yend; y++) {
		int yoff = (((y * 2 + oddlines) - src->yoffset) / vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) / vdbl) * dst->rowbytes;
		memset(dstline, 0, dst->inwidth * dst->pixbytes);
	}
}

static const uae_u8 dctv_signature[] = {
	0x93,0x0e,0x51,0xbc,0x22,0x17,0xdf,0xa4,0x19,0x1d,0x16,0x6a,0xb6,0xeb,0xd9,0x70,
	0x52,0xd6,0x07,0xf2,0x57,0x68,0x69,0xdc,0xce,0x3c,0xf8,0x9e,0xa6,0xc6,0x2a
};

static const uae_u16 dctv_tables[] = {
	0xF9AF, 0xF9C9, 0xF9E2, 0xF9FC, 0xFA15, 0xFA2F, 0xFA48, 0xFA62,
	0xFA7B, 0xFA95, 0xFAAE, 0xFAC8, 0xFAE1, 0xFAFB, 0xFB14, 0xFB2E,
	0xFB47, 0xFB61, 0xFB7A, 0xFB94, 0xFBAD, 0xFBC7, 0xFBE0, 0xFBFA,
	0xFC13, 0xFC2D, 0xFC46, 0xFC60, 0xFC79, 0xFC93, 0xFCAC, 0xFCC6,
	0xFCDF, 0xFCF9, 0xFD12, 0xFD2C, 0xFD45, 0xFD5F, 0xFD78, 0xFD92,
	0xFDAB, 0xFDC5, 0xFDDE, 0xFDF8, 0xFE11, 0xFE2B, 0xFE44, 0xFE5E,
	0xFE77, 0xFE91, 0xFEAA, 0xFEC4, 0xFEDD, 0xFEF7, 0xFF10, 0xFF2A,
	0xFF43, 0xFF5D, 0xFF76, 0xFF90, 0xFFA9, 0xFFC3, 0xFFDC, 0xFFF6,
	0x000F, 0x0029, 0x0042, 0x005C, 0x0075, 0x008F, 0x00A8, 0x00C2,
	0x00DB, 0x00F5, 0x010E, 0x0128, 0x0141, 0x015B, 0x0174, 0x018E,
	0x01A7, 0x01C1, 0x01DA, 0x01F4, 0x020D, 0x0227, 0x0240, 0x025A,
	0x0273, 0x028D, 0x02A6, 0x02C0, 0x02D9, 0x02F3, 0x030C, 0x0326,
	0x033F, 0x0359, 0x0372, 0x038C, 0x03A5, 0x03BF, 0x03D8, 0x03F2,
	0x040B, 0x0425, 0x043E, 0x0458, 0x0471, 0x048B, 0x04A4, 0x04BE,
	0x04D7, 0x04F1, 0x050A, 0x0524, 0x053D, 0x0557, 0x0570, 0x058A,
	0x05A3, 0x05BD, 0x05D6, 0x05F0, 0x0609, 0x0623, 0x063C, 0x0656,
	0x066F, 0x0689, 0x06A2, 0x06BC, 0x06D5, 0x06EF, 0x0708, 0x0722,
	0x073B, 0x0755, 0x076E, 0x0788, 0x07A1, 0x07BB, 0x07D4, 0x07EE,
	0x0807, 0x0821, 0x083A, 0x0854, 0x086D, 0x0887, 0x08A0, 0x08BA,
	0x08D3, 0x08ED, 0x0906, 0x0920, 0x0939, 0x0953, 0x096C, 0x0986,
	0x099F, 0x09B9, 0x09D2, 0x09EC, 0x0A05, 0x0A1F, 0x0A38, 0x0A52,
	0x0A6B, 0x0A85, 0x0A9E, 0x0AB8, 0x0AD1, 0x0AEB, 0x0B04, 0x0B1E,
	0x0B37, 0x0B51, 0x0B6A, 0x0B84, 0x0B9D, 0x0BB7, 0x0BD0, 0x0BEA,
	0x0C03, 0x0C1D, 0x0C36, 0x0C50, 0x0C69, 0x0C83, 0x0C9C, 0x0CB6,
	0x0CCF, 0x0CE9, 0x0D02, 0x0D1C, 0x0D35, 0x0D4F, 0x0D68, 0x0D82,
	0x0D9B, 0x0DB5, 0x0DCE, 0x0DE8, 0x0E01, 0x0E1B, 0x0E34, 0x0E4E,
	0x0E67, 0x0E81, 0x0E9A, 0x0EB4, 0x0ECD, 0x0EE7, 0x0F00, 0x0F1A,
	0x0F33, 0x0F4D, 0x0F66, 0x0F80, 0x0F99, 0x0FB3, 0x0FCC, 0x0FE6,
	0x0FFF, 0x1019, 0x1032, 0x104C, 0x1065, 0x107F, 0x1098, 0x10B2,
	0x10CB, 0x10E5, 0x10FE, 0x1118, 0x1131, 0x114B, 0x1164, 0x117E,
	0x1197, 0x11B1, 0x11CA, 0x11E4, 0x11FD, 0x1217, 0x1230, 0x124A,
	0x1263, 0x127D, 0x1296, 0x12B0, 0x12C9, 0x12E3, 0x12FC, 0x1316,

	0xF174, 0xF191, 0xF1AE, 0xF1CB, 0xF1E8, 0xF205, 0xF222, 0xF23F,
	0xF25C, 0xF279, 0xF296, 0xF2B4, 0xF2D1, 0xF2EE, 0xF30B, 0xF328,
	0xF345, 0xF362, 0xF37F, 0xF39C, 0xF3B9, 0xF3D6, 0xF3F4, 0xF411,
	0xF42E, 0xF44B, 0xF468, 0xF485, 0xF4A2, 0xF4BF, 0xF4DC, 0xF4F9,
	0xF517, 0xF534, 0xF551, 0xF56E, 0xF58B, 0xF5A8, 0xF5C5, 0xF5E2,
	0xF5FF, 0xF61C, 0xF639, 0xF657, 0xF674, 0xF691, 0xF6AE, 0xF6CB,
	0xF6E8, 0xF705, 0xF722, 0xF73F, 0xF75C, 0xF779, 0xF797, 0xF7B4,
	0xF7D1, 0xF7EE, 0xF80B, 0xF828, 0xF845, 0xF862, 0xF87F, 0xF89C,
	0xF8BA, 0xF8D7, 0xF8F4, 0xF911, 0xF92E, 0xF94B, 0xF968, 0xF985,
	0xF9A2, 0xF9BF, 0xF9DC, 0xF9FA, 0xFA17, 0xFA34, 0xFA51, 0xFA6E,
	0xFA8B, 0xFAA8, 0xFAC5, 0xFAE2, 0xFAFF, 0xFB1C, 0xFB3A, 0xFB57,
	0xFB74, 0xFB91, 0xFBAE, 0xFBCB, 0xFBE8, 0xFC05, 0xFC22, 0xFC3F,
	0xFC5D, 0xFC7A, 0xFC97, 0xFCB4, 0xFCD1, 0xFCEE, 0xFD0B, 0xFD28,
	0xFD45, 0xFD62, 0xFD7F, 0xFD9D, 0xFDBA, 0xFDD7, 0xFDF4, 0xFE11,
	0xFE2E, 0xFE4B, 0xFE68, 0xFE85, 0xFEA2, 0xFEBF, 0xFEDD, 0xFEFA,
	0xFF17, 0xFF34, 0xFF51, 0xFF6E, 0xFF8B, 0xFFA8, 0xFFC5, 0xFFE2,
	//0x180
	0x0000, 0x001D, 0x003A, 0x0057, 0x0074, 0x0091, 0x00AE, 0x00CB,
	0x00E8, 0x0105, 0x0122, 0x0140, 0x015D, 0x017A, 0x0197, 0x01B4,
	0x01D1, 0x01EE, 0x020B, 0x0228, 0x0245, 0x0262, 0x0280, 0x029D,
	0x02BA, 0x02D7, 0x02F4, 0x0311, 0x032E, 0x034B, 0x0368, 0x0385,
	0x03A3, 0x03C0, 0x03DD, 0x03FA, 0x0417, 0x0434, 0x0451, 0x046E,
	0x048B, 0x04A8, 0x04C5, 0x04E3, 0x0500, 0x051D, 0x053A, 0x0557,
	0x0574, 0x0591, 0x05AE, 0x05CB, 0x05E8, 0x0605, 0x0623, 0x0640,
	0x065D, 0x067A, 0x0697, 0x06B4, 0x06D1, 0x06EE, 0x070B, 0x0728,
	0x0746, 0x0763, 0x0780, 0x079D, 0x07BA, 0x07D7, 0x07F4, 0x0811,
	0x082E, 0x084B, 0x0868, 0x0886, 0x08A3, 0x08C0, 0x08DD, 0x08FA,
	0x0917, 0x0934, 0x0951, 0x096E, 0x098B, 0x09A8, 0x09C6, 0x09E3,
	0x0A00, 0x0A1D, 0x0A3A, 0x0A57, 0x0A74, 0x0A91, 0x0AAE, 0x0ACB,
	0x0AE9, 0x0B06, 0x0B23, 0x0B40, 0x0B5D, 0x0B7A, 0x0B97, 0x0BB4,
	0x0BD1, 0x0BEE, 0x0C0B, 0x0C29, 0x0C46, 0x0C63, 0x0C80, 0x0C9D,
	0x0CBA, 0x0CD7, 0x0CF4, 0x0D11, 0x0D2E, 0x0D4B, 0x0D69, 0x0D86,
	0x0DA3, 0x0DC0, 0x0DDD, 0x0DFA, 0x0E17, 0x0E34, 0x0E51, 0x0E6E,

	0xE61B, 0xE64E, 0xE682, 0xE6B6, 0xE6EA, 0xE71E, 0xE751, 0xE785,
	0xE7B9, 0xE7ED, 0xE821, 0xE854, 0xE888, 0xE8BC, 0xE8F0, 0xE924,
	0xE957, 0xE98B, 0xE9BF, 0xE9F3, 0xEA26, 0xEA5A, 0xEA8E, 0xEAC2,
	0xEAF6, 0xEB29, 0xEB5D, 0xEB91, 0xEBC5, 0xEBF9, 0xEC2C, 0xEC60,
	0xEC94, 0xECC8, 0xECFB, 0xED2F, 0xED63, 0xED97, 0xEDCB, 0xEDFE,
	0xEE32, 0xEE66, 0xEE9A, 0xEECE, 0xEF01, 0xEF35, 0xEF69, 0xEF9D,
	0xEFD1, 0xF004, 0xF038, 0xF06C, 0xF0A0, 0xF0D3, 0xF107, 0xF13B,
	0xF16F, 0xF1A3, 0xF1D6, 0xF20A, 0xF23E, 0xF272, 0xF2A6, 0xF2D9,
	0xF30D, 0xF341, 0xF375, 0xF3A8, 0xF3DC, 0xF410, 0xF444, 0xF478,
	0xF4AB, 0xF4DF, 0xF513, 0xF547, 0xF57B, 0xF5AE, 0xF5E2, 0xF616,
	0xF64A, 0xF67D, 0xF6B1, 0xF6E5, 0xF719, 0xF74D, 0xF780, 0xF7B4,
	0xF7E8, 0xF81C, 0xF850, 0xF883, 0xF8B7, 0xF8EB, 0xF91F, 0xF953,
	0xF986, 0xF9BA, 0xF9EE, 0xFA22, 0xFA55, 0xFA89, 0xFABD, 0xFAF1,
	0xFB25, 0xFB58, 0xFB8C, 0xFBC0, 0xFBF4, 0xFC28, 0xFC5B, 0xFC8F,
	0xFCC3, 0xFCF7, 0xFD2A, 0xFD5E, 0xFD92, 0xFDC6, 0xFDFA, 0xFE2D,
	0xFE61, 0xFE95, 0xFEC9, 0xFEFD, 0xFF30, 0xFF64, 0xFF98, 0xFFCC,
	//0x280
	0x0000, 0x0033, 0x0067, 0x009B, 0x00CF, 0x0102, 0x0136, 0x016A,
	0x019E, 0x01D2, 0x0205, 0x0239, 0x026D, 0x02A1, 0x02D5, 0x0308,
	0x033C, 0x0370, 0x03A4, 0x03D7, 0x040B, 0x043F, 0x0473, 0x04A7,
	0x04DA, 0x050E, 0x0542, 0x0576, 0x05AA, 0x05DD, 0x0611, 0x0645,
	0x0679, 0x06AC, 0x06E0, 0x0714, 0x0748, 0x077C, 0x07AF, 0x07E3,
	0x0817, 0x084B, 0x087F, 0x08B2, 0x08E6, 0x091A, 0x094E, 0x0982,
	0x09B5, 0x09E9, 0x0A1D, 0x0A51, 0x0A84, 0x0AB8, 0x0AEC, 0x0B20,
	0x0B54, 0x0B87, 0x0BBB, 0x0BEF, 0x0C23, 0x0C57, 0x0C8A, 0x0CBE,
	0x0CF2, 0x0D26, 0x0D59, 0x0D8D, 0x0DC1, 0x0DF5, 0x0E29, 0x0E5C,
	0x0E90, 0x0EC4, 0x0EF8, 0x0F2C, 0x0F5F, 0x0F93, 0x0FC7, 0x0FFB,
	0x102F, 0x1062, 0x1096, 0x10CA, 0x10FE, 0x1131, 0x1165, 0x1199,
	0x11CD, 0x1201, 0x1234, 0x1268, 0x129C, 0x12D0, 0x1304, 0x1337,
	0x136B, 0x139F, 0x13D3, 0x1406, 0x143A, 0x146E, 0x14A2, 0x14D6,
	0x1509, 0x153D, 0x1571, 0x15A5, 0x15D9, 0x160C, 0x1640, 0x1674,
	0x16A8, 0x16DB, 0x170F, 0x1743, 0x1777, 0x17AB, 0x17DE, 0x1812,
	0x1846, 0x187A, 0x18AE, 0x18E1, 0x1915, 0x1949, 0x197D, 0x19B1,

	0x0769, 0x075A, 0x074B, 0x073D, 0x072E, 0x071F, 0x0710, 0x0701,
	0x06F3, 0x06E4, 0x06D5, 0x06C6, 0x06B7, 0x06A8, 0x069A, 0x068B,
	0x067C, 0x066D, 0x065E, 0x064F, 0x0641, 0x0632, 0x0623, 0x0614,
	0x0605, 0x05F6, 0x05E8, 0x05D9, 0x05CA, 0x05BB, 0x05AC, 0x059E,
	0x058F, 0x0580, 0x0571, 0x0562, 0x0553, 0x0545, 0x0536, 0x0527,
	0x0518, 0x0509, 0x04FA, 0x04EC, 0x04DD, 0x04CE, 0x04BF, 0x04B0,
	0x04A2, 0x0493, 0x0484, 0x0475, 0x0466, 0x0457, 0x0449, 0x043A,
	0x042B, 0x041C, 0x040D, 0x03FE, 0x03F0, 0x03E1, 0x03D2, 0x03C3,
	0x03B4, 0x03A5, 0x0397, 0x0388, 0x0379, 0x036A, 0x035B, 0x034D,
	0x033E, 0x032F, 0x0320, 0x0311, 0x0302, 0x02F4, 0x02E5, 0x02D6,
	0x02C7, 0x02B8, 0x02A9, 0x029B, 0x028C, 0x027D, 0x026E, 0x025F,
	0x0251, 0x0242, 0x0233, 0x0224, 0x0215, 0x0206, 0x01F8, 0x01E9,
	0x01DA, 0x01CB, 0x01BC, 0x01AD, 0x019F, 0x0190, 0x0181, 0x0172,
	0x0163, 0x0154, 0x0146, 0x0137, 0x0128, 0x0119, 0x010A, 0x00FC,
	0x00ED, 0x00DE, 0x00CF, 0x00C0, 0x00B1, 0x00A3, 0x0094, 0x0085,
	0x0076, 0x0067, 0x0058, 0x004A, 0x003B, 0x002C, 0x001D, 0x000E,
	//0x380
	0x0000, 0xFFF1, 0xFFE2, 0xFFD3, 0xFFC4, 0xFFB5, 0xFFA7, 0xFF98,
	0xFF89, 0xFF7A, 0xFF6B, 0xFF5C, 0xFF4E, 0xFF3F, 0xFF30, 0xFF21,
	0xFF12, 0xFF03, 0xFEF5, 0xFEE6, 0xFED7, 0xFEC8, 0xFEB9, 0xFEAB,
	0xFE9C, 0xFE8D, 0xFE7E, 0xFE6F, 0xFE60, 0xFE52, 0xFE43, 0xFE34,
	0xFE25, 0xFE16, 0xFE07, 0xFDF9, 0xFDEA, 0xFDDB, 0xFDCC, 0xFDBD,
	0xFDAF, 0xFDA0, 0xFD91, 0xFD82, 0xFD73, 0xFD64, 0xFD56, 0xFD47,
	0xFD38, 0xFD29, 0xFD1A, 0xFD0B, 0xFCFD, 0xFCEE, 0xFCDF, 0xFCD0,
	0xFCC1, 0xFCB2, 0xFCA4, 0xFC95, 0xFC86, 0xFC77, 0xFC68, 0xFC5A,
	0xFC4B, 0xFC3C, 0xFC2D, 0xFC1E, 0xFC0F, 0xFC01, 0xFBF2, 0xFBE3,
	0xFBD4, 0xFBC5, 0xFBB6, 0xFBA8, 0xFB99, 0xFB8A, 0xFB7B, 0xFB6C,
	0xFB5E, 0xFB4F, 0xFB40, 0xFB31, 0xFB22, 0xFB13, 0xFB05, 0xFAF6,
	0xFAE7, 0xFAD8, 0xFAC9, 0xFABA, 0xFAAC, 0xFA9D, 0xFA8E, 0xFA7F,
	0xFA70, 0xFA61, 0xFA53, 0xFA44, 0xFA35, 0xFA26, 0xFA17, 0xFA09,
	0xF9FA, 0xF9EB, 0xF9DC, 0xF9CD, 0xF9BE, 0xF9B0, 0xF9A1, 0xF992,
	0xF983, 0xF974, 0xF965, 0xF957, 0xF948, 0xF939, 0xF92A, 0xF91B,
	0xF90D, 0xF8FE, 0xF8EF, 0xF8E0, 0xF8D1, 0xF8C2, 0xF8B4, 0xF8A5,

	0x050C, 0x0502, 0x04F8, 0x04EE, 0x04E4, 0x04DA, 0x04D0, 0x04C6,
	0x04BC, 0x04B1, 0x04A7, 0x049D, 0x0493, 0x0489, 0x047F, 0x0475,
	0x046B, 0x0461, 0x0457, 0x044C, 0x0442, 0x0438, 0x042E, 0x0424,
	0x041A, 0x0410, 0x0406, 0x03FC, 0x03F2, 0x03E7, 0x03DD, 0x03D3,
	0x03C9, 0x03BF, 0x03B5, 0x03AB, 0x03A1, 0x0397, 0x038D, 0x0382,
	0x0378, 0x036E, 0x0364, 0x035A, 0x0350, 0x0346, 0x033C, 0x0332,
	0x0328, 0x031D, 0x0313, 0x0309, 0x02FF, 0x02F5, 0x02EB, 0x02E1,
	0x02D7, 0x02CD, 0x02C3, 0x02B8, 0x02AE, 0x02A4, 0x029A, 0x0290,
	0x0286, 0x027C, 0x0272, 0x0268, 0x025E, 0x0253, 0x0249, 0x023F,
	0x0235, 0x022B, 0x0221, 0x0217, 0x020D, 0x0203, 0x01F9, 0x01EE,
	0x01E4, 0x01DA, 0x01D0, 0x01C6, 0x01BC, 0x01B2, 0x01A8, 0x019E,
	0x0194, 0x0189, 0x017F, 0x0175, 0x016B, 0x0161, 0x0157, 0x014D,
	0x0143, 0x0139, 0x012F, 0x0124, 0x011A, 0x0110, 0x0106, 0x00FC,
	0x00F2, 0x00E8, 0x00DE, 0x00D4, 0x00CA, 0x00BF, 0x00B5, 0x00AB,
	0x00A1, 0x0097, 0x008D, 0x0083, 0x0079, 0x006F, 0x0065, 0x005A,
	0x0050, 0x0046, 0x003C, 0x0032, 0x0028, 0x001E, 0x0014, 0x000A,
	//0x480
	0x0000, 0xFFF5, 0xFFEB, 0xFFE1, 0xFFD7, 0xFFCD, 0xFFC3, 0xFFB9,
	0xFFAF, 0xFFA5, 0xFF9B, 0xFF90, 0xFF86, 0xFF7C, 0xFF72, 0xFF68,
	0xFF5E, 0xFF54, 0xFF4A, 0xFF40, 0xFF36, 0xFF2B, 0xFF21, 0xFF17,
	0xFF0D, 0xFF03, 0xFEF9, 0xFEEF, 0xFEE5, 0xFEDB, 0xFED1, 0xFEC6,
	0xFEBC, 0xFEB2, 0xFEA8, 0xFE9E, 0xFE94, 0xFE8A, 0xFE80, 0xFE76,
	0xFE6C, 0xFE61, 0xFE57, 0xFE4D, 0xFE43, 0xFE39, 0xFE2F, 0xFE25,
	0xFE1B, 0xFE11, 0xFE07, 0xFDFC, 0xFDF2, 0xFDE8, 0xFDDE, 0xFDD4,
	0xFDCA, 0xFDC0, 0xFDB6, 0xFDAC, 0xFDA2, 0xFD97, 0xFD8D, 0xFD83,
	0xFD79, 0xFD6F, 0xFD65, 0xFD5B, 0xFD51, 0xFD47, 0xFD3D, 0xFD32,
	0xFD28, 0xFD1E, 0xFD14, 0xFD0A, 0xFD00, 0xFCF6, 0xFCEC, 0xFCE2,
	0xFCD8, 0xFCCD, 0xFCC3, 0xFCB9, 0xFCAF, 0xFCA5, 0xFC9B, 0xFC91,
	0xFC87, 0xFC7D, 0xFC73, 0xFC68, 0xFC5E, 0xFC54, 0xFC4A, 0xFC40,
	0xFC36, 0xFC2C, 0xFC22, 0xFC18, 0xFC0E, 0xFC03, 0xFBF9, 0xFBEF,
	0xFBE5, 0xFBDB, 0xFBD1, 0xFBC7, 0xFBBD, 0xFBB3, 0xFBA9, 0xFB9E,
	0xFB94, 0xFB8A, 0xFB80, 0xFB76, 0xFB6C, 0xFB62, 0xFB58, 0xFB4E,
	0xFB44, 0xFB39, 0xFB2F, 0xFB25, 0xFB1B, 0xFB11, 0xFB07, 0xFAFD
};

#define DCTV_BUFFER_SIZE 1000
static uae_s8 dctv_chroma[2 * DCTV_BUFFER_SIZE];
static uae_u8 dctv_luma[2 * DCTV_BUFFER_SIZE];


STATIC_INLINE int minmax(int v, int min, int max)
{
	if (v < min)
		v = min;
	if (v > max)
		v = max;
	return v;
}

#define DCTV_SIGNATURE_DEBUG 0
#if DCTV_SIGNATURE_DEBUG
static int signature_test_y = 0x93;
#endif

static bool dctv(struct vidbuffer *src, struct vidbuffer *dst, bool doublelines, int oddlines)
{
	int y, x, vdbl, hdbl;
	int ystart, yend, isntsc;
	int xadd;

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	vdbl = gfxvidinfo.ychange;
	hdbl = gfxvidinfo.xchange;

	xadd = ((1 << 1) / hdbl) * src->pixbytes;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	int signature_detected = -1;
	int signature_cnt = 0;
	bool dctv_enabled = false;
	int ycnt = 0;
	memset(dctv_luma, 64, sizeof DCTV_BUFFER_SIZE);

	for (y = ystart; y < yend; y++) {
		int yoff = (((y * 2 + oddlines) - src->yoffset) / vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;
		uae_u8 *line = src->bufmem + yoff * src->rowbytes;
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) / vdbl) * dst->rowbytes;

		int firstnz = -1;
		bool sign = false;
		int oddeven = 0;
		uae_u8 prev = 0;
		uae_u8 vals[3] = { 0x40, 0x40, 0x40 };
		int zigzagoffset = 0;
		int prevtval = 0;
		uae_s8 *chrbuf_w = NULL, *chrbuf_r1 = NULL, *chrbuf_r2 = NULL;
		uae_u8 *lumabuf1 = dctv_luma, *lumabuf2 = dctv_luma;
	
		ycnt++;

#if DCTV_SIGNATURE_DEBUG
		uae_u8 signx = 0;
		uae_u8 signmask = 0xff;
		if (y == signature_test_y + 1)
			write_log(_T("\n"));
#endif

		for (x = 0; x < src->inwidth; x++) {
			uae_u8 *s = line + ((x << 1) / hdbl) * src->pixbytes;
			uae_u8 *d = dstline + ((x << 1) / hdbl) * dst->pixbytes + zigzagoffset;
			uae_u8 *s2 = s + src->rowbytes;
			uae_u8 *d2 = d + dst->rowbytes;
			uae_u8 newval = DCTV_FIRBG(src, s);

			int mask = 1 << (7 - (signature_cnt & 7));
			int bitval = (newval & 0x40) ? mask : 0;
			if ((dctv_signature[signature_cnt / 8] & mask) == bitval) {
				signature_cnt++;
				if (signature_cnt == sizeof (dctv_signature) * 8) {
					dctv_enabled = true;
					signature_detected = y;
				}
			} else {
				signature_cnt = 0;
			}

#if DCTV_SIGNATURE_DEBUG
			if (y == signature_test_y) {
				if ((newval & 0x40) && signmask == 0xff)
					signmask = 0x80;
				if (signmask != 0xff) {
					if (newval & 0x40)
						signx |= signmask;
					signmask >>= 1;
					if (!signmask) {
						signmask = 0x80;
						write_log(_T("%02x."), signx);
						signx = 0;
					}
				}
			}
#endif
			if (!dctv_enabled || signature_detected == y) {
				PUT_AMIGARGB(d, s, d2, s2, dst, 0, doublelines, false);
				continue;
			}

			uae_u8 val = prev | newval;
			if (firstnz < 0 && newval) {
				firstnz = 0;
				if (ycnt & 1) {
					zigzagoffset = 0;
					oddeven = -1;
					chrbuf_w = dctv_chroma + 8;
					chrbuf_r1 = dctv_chroma + 8;
					chrbuf_r2 = dctv_chroma + DCTV_BUFFER_SIZE + 8;
				} else {
					zigzagoffset = dst->pixbytes;
					oddeven = -1;
					chrbuf_w = dctv_chroma + DCTV_BUFFER_SIZE + 8;
					chrbuf_r2 = dctv_chroma + 8;
					chrbuf_r1 = dctv_chroma + DCTV_BUFFER_SIZE + 8;
				}
				sign = false;
			}

			if (oddeven > 0 && !firstnz) {
				sign = !sign;

				if (val == 0)
					val = 64;

				vals[2] = vals[1];
				vals[1] = vals[0];
				vals[0] = val;

				int v0 = 2 * vals[1] - vals[2] - vals[0] + 2;
				if (v0 < 0)
					v0 += 3;
				v0 /= 4;
				int v1 = -v0;
				if (sign)
					v0 = -v0;
				*chrbuf_w = minmax(v0, -127, 127);
				*lumabuf1 = minmax(vals[2] + v1, 64, 224);

				int ch1 = chrbuf_r1[0] + chrbuf_r1[-1];
				int ch2 = chrbuf_r2[0] + chrbuf_r2[-1];
				ch1 /= 2;
				ch2 /= 2;

				int luma = lumabuf1[-1] * 2 + lumabuf1[-2] + lumabuf1[0];
				luma /= 4;

				int l = (uae_s16)dctv_tables[luma];

				int rr = (uae_s16)dctv_tables[ch1 + 0x180] + l;
				int gg = (uae_s16)dctv_tables[ch1 + 0x380] + (uae_s16)dctv_tables[ch2 + 0x480] + l;
				int bb = (uae_s16)dctv_tables[ch2 + 0x280] + l;

				uae_u8 r = minmax(rr >> 4, 0, 255);
				uae_u8 g = minmax(gg >> 4, 0, 255);
				uae_u8 b = minmax(bb >> 4, 0, 255);

				PRGB(dst, d - dst->pixbytes, r, g, b);
				PRGB(dst, d, r, g, b);
				if (doublelines) {
					PRGB(dst, d2 - dst->pixbytes, r, g, b);
					PRGB(dst, d2, r, g, b);
				}

				chrbuf_r1++;
				chrbuf_r2++;
				chrbuf_w++;
				lumabuf1++;

			} else if (oddeven < 0) {

				uae_u8 r = 0, b = 0, g = 0;
				PRGB(dst, d - dst->pixbytes, r, g, b);
				PRGB(dst, d, r, g, b);
				if (doublelines) {
					PRGB(dst, d2 - dst->pixbytes, r, g, b);
					PRGB(dst, d2, r, g, b);
				}

			}

			if (oddeven >= 0)
				oddeven = oddeven ? 0 : 1;
			else
				oddeven++;
			prev = newval << 1;
		}
	}

	if (dctv_enabled) {
		dst->nativepositioning = true;
		if (monitor != MONITOREMU_DCTV) {
			monitor = MONITOREMU_DCTV;
			write_log(_T("DCTV mode\n"));
		}
	}

	return dctv_enabled;

}

static bool do_dctv(struct vidbuffer *src, struct vidbuffer *dst)
{
	bool v;
	if (interlace_seen) {
		if (currprefs.gfx_iscanlines) {
			v = dctv(src, dst, false, lof_store ? 0 : 1);
			if (v && currprefs.gfx_iscanlines > 1)
				blank_generic(src, dst, lof_store ? 1 : 0);
		} else {
			v = dctv(src, dst, false, 0);
			v |= dctv(src, dst, false, 1);
		}
	} else {
		v = dctv(src, dst, true, 0);
	}
	return v;
}

static uae_u8 *sm_frame_buffer;
#define SM_VRAM_WIDTH 1024
#define SM_VRAM_HEIGHT 800
#define SM_VRAM_BYTES 4
static int sm_configured;
static uae_u8 sm_acmemory[128];

static void sm_alloc_fb(void)
{
	if (!sm_frame_buffer) {
		sm_frame_buffer = xcalloc(uae_u8, SM_VRAM_WIDTH * SM_VRAM_HEIGHT * SM_VRAM_BYTES);
	}
}
static void sm_free(void)
{
	xfree(sm_frame_buffer);
	sm_frame_buffer = NULL;
	sm_configured = 0;
}

#define FC24_MAXHEIGHT 482
static uae_u8 fc24_mode, fc24_cr0, fc24_cr1;
static uae_u16 fc24_hpos, fc24_vpos, fc24_width;
static int fc24_offset;

STATIC_INLINE uae_u8 MAKEFCOVERLAY(uae_u8 v)
{
	v &= 3;
	v |= v << 2;
	v |= v << 4;
	v |= v << 6;
	return v;
}

static bool firecracker24(struct vidbuffer *src, struct vidbuffer *dst, bool doublelines, int oddlines)
{
	int y, x, vdbl, hdbl;
	int fc24_y, fc24_x, fc24_dx, fc24_xadd, fc24_xmult, fc24_xoffset;
	int ystart, yend, isntsc;
	int xadd, xaddfc;
	int bufferoffset;

	// FC disabled and Amiga enabled?
	if (!(fc24_cr1 & 1) && !(fc24_cr0 & 1))
		return false;

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	vdbl = gfxvidinfo.ychange;
	hdbl = gfxvidinfo.xchange; // 4=lores,2=hires,1=shres

	xaddfc = (1 << 1) / hdbl; // 0=lores,1=hires,2=shres
	xadd = xaddfc * src->pixbytes;
	

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	switch (fc24_width)
	{
		case 384:
		fc24_xmult = 0;
		break;
		case 512:
		fc24_xmult = 1;
		break;
		case 768:
		fc24_xmult = 1;
		break;
		case 1024:
		fc24_xmult = 2;
		break;
		default:
		return false;
	}
	
	if (fc24_xmult >= xaddfc) {
		fc24_xadd = fc24_xmult - xaddfc;
		fc24_dx = 0;
	} else {
		fc24_xadd = 0;
		fc24_dx = xaddfc - fc24_xmult;
	}

	fc24_xoffset = ((src->inwidth - ((fc24_width << fc24_dx) >> fc24_xadd)) / 2);
	fc24_xadd = 1 << fc24_xadd;

	bufferoffset = (fc24_cr0 & 2) ? 512 * SM_VRAM_BYTES: 0;

	fc24_y = 0;
	for (y = ystart; y < yend; y++) {
		int oddeven = 0;
		uae_u8 prev = 0;
		int yoff = (((y * 2 + oddlines) - src->yoffset) / vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;
		uae_u8 *line = src->bufmem + yoff * src->rowbytes;
		uae_u8 *line_genlock = row_map_genlock[yoff];
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) / vdbl) * dst->rowbytes;
		uae_u8 *vramline = sm_frame_buffer + (fc24_y + oddlines) * SM_VRAM_WIDTH * SM_VRAM_BYTES + bufferoffset;
		fc24_x = 0;
		for (x = 0; x < src->inwidth; x++) {
			uae_u8 r = 0, g = 0, b = 0;
			uae_u8 *s = line + ((x << 1) / hdbl) * src->pixbytes;
			uae_u8 *s_genlock = line_genlock + ((x << 1) / hdbl);
			uae_u8 *d = dstline + ((x << 1) / hdbl) * dst->pixbytes;
			int fc24_xx = (fc24_x >> fc24_dx) - fc24_xoffset;
			uae_u8 *vramptr = NULL;
			if (fc24_xx >= 0 && fc24_xx < fc24_width && fc24_y >= 0 && fc24_y < FC24_MAXHEIGHT) {
				vramptr = vramline + fc24_xx * SM_VRAM_BYTES;
				uae_u8 ax = vramptr[0];
				if (ax & 0x40) {
					r = MAKEFCOVERLAY(ax >> 4);
					g = MAKEFCOVERLAY(ax >> 2);
					b = MAKEFCOVERLAY(ax >> 0);
				} else {
					r = vramptr[1];
					g = vramptr[2];
					b = vramptr[3];
				}
			}
			if (!(fc24_cr0 & 1) && (!(fc24_cr1 & 1) || (!is_transparent(s_genlock[0])))) {
				uae_u8 *s2 = s + src->rowbytes;
				uae_u8 *d2 = d + dst->rowbytes;
				PUT_AMIGARGB(d, s, d2, s2, dst, xadd, doublelines, false);
			} else {
				PUT_PRGB(d, NULL, dst, r, g, b, 0, false, false);
				if (doublelines) {
					if (vramptr) {
						vramptr += SM_VRAM_WIDTH * SM_VRAM_BYTES;
						uae_u8 ax = vramptr[0];
						if (ax & 0x40) {
							r = MAKEFCOVERLAY(ax >> 4);
							g = MAKEFCOVERLAY(ax >> 2);
							b = MAKEFCOVERLAY(ax >> 0);
						} else {
							r = vramptr[1];
							g = vramptr[2];
							b = vramptr[3];
						}
					}
					PUT_PRGB(d + dst->rowbytes, NULL, dst, r, g, b, 0, false, false);
				}
			}
			fc24_x += fc24_xadd;
		}
		fc24_y += 2;
	}

	dst->nativepositioning = true;
	if (monitor != MONITOREMU_FIRECRACKER24) {
		monitor = MONITOREMU_FIRECRACKER24;
		write_log(_T("FireCracker mode\n"));
	}

	return true;
}

static void fc24_setoffset(void)
{
	fc24_vpos &= 511;
	fc24_hpos &= 1023;
	fc24_offset = fc24_vpos * SM_VRAM_WIDTH * SM_VRAM_BYTES + fc24_hpos * SM_VRAM_BYTES;
	sm_alloc_fb();
}

static void fc24_inc(uaecptr addr, bool write)
{
	addr &= 65535;
	if (addr >= 6) {
		fc24_hpos++;
		if (fc24_hpos >= 1024) {
			fc24_hpos = 0;
			fc24_vpos++;
			fc24_vpos &= 511;
		}
		fc24_setoffset();
	}
}

static void fc24_setmode(void)
{
	switch (fc24_cr0 >> 6)
	{
		case 0:
		fc24_width = 384;
		break;
		case 1:
		fc24_width = 512;
		break;
		case 2:
		fc24_width = 768;
		break;
		case 3:
		fc24_width = 1024;
		break;
	}
	sm_alloc_fb();
}

static void fc24_reset(void)
{
	if (currprefs.monitoremu != MONITOREMU_FIRECRACKER24)
		return;
	fc24_cr0 = 0;
	fc24_cr1 = 0;
	fc24_setmode();
	fc24_setoffset();
}

static void firecracker24_write_byte(uaecptr addr, uae_u8 v)
{
	addr &= 65535;
	switch (addr)
	{
		default:
		if (!sm_frame_buffer)
			return;
		sm_frame_buffer[fc24_offset + (addr & 3)] = v;
		break;
		case 10:
		fc24_cr0 = v;
		fc24_setmode();
		write_log(_T("FC24_CR0 = %02x\n"), fc24_cr0);
		break;
		case 11:
		fc24_cr1 = v;
		sm_alloc_fb();
		write_log(_T("FC24_CR1 = %02x\n"), fc24_cr1);
		break;
		case 12:
		fc24_vpos &= 0x00ff;
		fc24_vpos |= v << 8;
		fc24_setoffset();
		break;
		case 13:
		fc24_vpos &= 0xff00;
		fc24_vpos |= v;
		fc24_setoffset();
		//write_log(_T("V=%d "), fc24_vpos);
		break;
		case 14:
		fc24_hpos &= 0x00ff;
		fc24_hpos |= v << 8;
		fc24_setoffset();
		break;
		case 15:
		fc24_hpos &= 0xff00;
		fc24_hpos |= v;
		fc24_setoffset();
		//write_log(_T("H=%d "), fc24_hpos);
		break;
	}
}

static uae_u8 firecracker24_read_byte(uaecptr addr)
{
	uae_u8 v = 0;
	addr &= 65535;
	switch (addr)
	{
		default:
		if (!sm_frame_buffer)
			return v;
		v = sm_frame_buffer[fc24_offset + (addr & 3)];
		break;
		case 10:
		v = fc24_cr0;
		break;
		case 11:
		v = fc24_cr1;
		break;
		case 12:
		v = fc24_vpos >> 8;
		break;
		case 13:
		v = fc24_vpos >> 0;
		break;
		case 14:
		v = fc24_hpos >> 8;
		break;
		case 15:
		v = fc24_hpos >> 0;
		break;
	}
	return v;
}

static void firecracker24_write(uaecptr addr, uae_u32 v, int size)
{
	int offset = addr & 3;
	uaecptr oaddr = addr;
	int shift = 8 * (size - 1);
	while (size > 0 && addr < 10) {
		int off = fc24_offset + offset;
		if ((offset & 3) == 0) {
			if (!(fc24_cr1 & 0x80))
				sm_frame_buffer[off] = v >> shift;
		} else {
			sm_frame_buffer[off] = v >> shift;
		}
		offset++;
		size--;
		shift -= 8;
		addr++;
	}
	fc24_inc(oaddr, true);
}

static uae_u32 firecracker24_read(uaecptr addr, int size)
{
	uae_u32 v = 0;
	uaecptr oaddr = addr;
	int offset = addr & 3;
	while (size > 0 && addr < 10) {
		v <<= 8;
		v |= sm_frame_buffer[fc24_offset + offset];
		offset++;
		size--;
		addr++;
	}
	fc24_inc(oaddr, false);
	return v;
}

extern addrbank specialmonitors_bank;

static void REGPARAM2 sm_bput(uaecptr addr, uae_u32 b)
{
	b &= 0xff;
	addr &= 65535;
	if (!sm_configured) {
		switch (addr) {
			case 0x48:
			map_banks_z2(&specialmonitors_bank, expamem_z2_pointer >> 16, 65536 >> 16);
			sm_configured = 1;
			expamem_next(&specialmonitors_bank, NULL);
			break;
			case 0x4c:
			sm_configured = -1;
			expamem_shutup(&specialmonitors_bank);
			break;
		}
		return;
	}
	if (sm_configured > 0) {
		if (addr < 10) {
			firecracker24_write(addr, b, 1);
		} else {
			firecracker24_write_byte(addr, b);
		}
	}
}
static void REGPARAM2 sm_wput(uaecptr addr, uae_u32 b)
{
	addr &= 65535;
	if (addr < 10) {
		firecracker24_write(addr, b, 2);
	} else {
		firecracker24_write_byte(addr + 0, b >> 8);
		firecracker24_write_byte(addr + 1, b >> 0);
	}
}

static void REGPARAM2 sm_lput(uaecptr addr, uae_u32 b)
{
	addr &= 65535;
	if (addr < 10) {
		firecracker24_write(addr + 0, b >> 16, 2);
		firecracker24_write(addr + 2, b >>  0, 2);
	} else {
		firecracker24_write_byte(addr + 0, b >> 24);
		firecracker24_write_byte(addr + 1, b >> 16);
		firecracker24_write_byte(addr + 2, b >> 8);
		firecracker24_write_byte(addr + 3, b >> 0);
	}
}
static uae_u32 REGPARAM2 sm_bget(uaecptr addr)
{
	uae_u8 v = 0;
	addr &= 65535;
	if (!sm_configured) {
		if (addr >= sizeof sm_acmemory)
			return 0;
		return sm_acmemory[addr];
	}
	if (sm_configured > 0) {
		if (addr < 10) {
			v = firecracker24_read(addr, 1);
		} else {
			v = firecracker24_read_byte(addr);
		}
	}
	return v;
}
static uae_u32 REGPARAM2 sm_wget(uaecptr addr)
{
	uae_u16 v;
	addr &= 65535;
	if (addr < 10) {
		v = firecracker24_read(addr, 2);
	} else {
		v = firecracker24_read_byte(addr) << 8;
		v |= firecracker24_read_byte(addr + 1) << 0;
	}
	return v;
}
static uae_u32 REGPARAM2 sm_lget(uaecptr addr)
{
	uae_u32 v;
	addr &= 65535;
	if (addr < 10) {
		v  = firecracker24_read(addr + 0, 2) << 16;
		v |= firecracker24_read(addr + 2, 2) <<  0;
	} else {
		v = firecracker24_read_byte(addr) << 24;
		v |= firecracker24_read_byte(addr + 1) << 16;
		v |= firecracker24_read_byte(addr + 2) << 8;
		v |= firecracker24_read_byte(addr + 3) << 0;
	}
	return v;
}

addrbank specialmonitors_bank = {
	sm_lget, sm_wget, sm_bget,
	sm_lput, sm_wput, sm_bput,
	default_xlate, default_check, NULL, NULL, _T("DisplayAdapter"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO, S_READ, S_WRITE
};

static void ew(int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		sm_acmemory[addr] = (value & 0xf0);
		sm_acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		sm_acmemory[addr] = ~(value & 0xf0);
		sm_acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static const uae_u8 firecracker24_autoconfig[16] = { 0xc1, 0, 0, 0, 2104 >> 8, 2104 & 255 };

addrbank *specialmonitor_autoconfig_init(int devnum)
{
	sm_configured = 0;
	memset(sm_acmemory, 0xff, sizeof sm_acmemory);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = firecracker24_autoconfig[i];
		ew(i * 4, b);
	}
	return &specialmonitors_bank;
}

static bool do_firecracker24(struct vidbuffer *src, struct vidbuffer *dst)
{
	bool v;
	if (interlace_seen) {
		if (currprefs.gfx_iscanlines) {
			v = firecracker24(src, dst, false, lof_store ? 0 : 1);
			if (v && currprefs.gfx_iscanlines > 1)
				blank_generic(src, dst, lof_store ? 1 : 0);
		} else {
			v = firecracker24(src, dst, false, 0);
			v |= firecracker24(src, dst, false, 1);
		}
	} else {
		v = firecracker24(src, dst, true, 0);
	}
	return v;
}

static uae_u16 avideo_previous_fmode[2];
static int av24_offset[2];
static int av24_doublebuffer[2];
static int av24_writetovram[2];
static int av24_mode[2];
static int avideo_allowed;

static bool avideo(struct vidbuffer *src, struct vidbuffer *dst, bool doublelines, int oddlines, int lof)
{
	int y, x, vdbl, hdbl;
	int ystart, yend, isntsc;
	int xadd, xaddpix;
	int mode;
	int offset = -1;
	bool writetovram;
	bool av24;
	int doublebuffer = -1;
	uae_u16 fmode;
	
	fmode = avideo_previous_fmode[lof];

	if (currprefs.monitoremu == MONITOREMU_AUTO) {
		if (!avideo_allowed)
			return false;
		av24 = avideo_allowed == 24;
	} else {
		av24 = currprefs.monitoremu == MONITOREMU_AVIDEO24;
	}

	if (currprefs.chipset_mask & CSMASK_AGA)
		return false;

	if (av24) {
		writetovram = av24_writetovram[lof] != 0;
		mode = av24_mode[lof];
	} else {
		mode = fmode & 7;
		if (mode == 1)
			offset = 0;
		else if (mode == 3)
			offset = 1;
		else if (mode == 2)
			offset = 2;
		writetovram = offset >= 0;
	}

	if (!mode)
		return false;

	sm_alloc_fb();

	//write_log(_T("%04x %d %d %d\n"), avideo_previous_fmode[oddlines], mode, offset, writetovram);

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	vdbl = gfxvidinfo.ychange;
	hdbl = gfxvidinfo.xchange;

	xaddpix = (1 << 1) / hdbl;
	xadd = ((1 << 1) / hdbl) * src->pixbytes;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	for (y = ystart; y < yend; y++) {
		int oddeven = 0;
		uae_u8 prev = 0;
		int yoff = (((y * 2 + oddlines) - src->yoffset) / vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;
		uae_u8 *line = src->bufmem + yoff * src->rowbytes;
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) / vdbl) * dst->rowbytes;
		uae_u8 *vramline = sm_frame_buffer + y * 2 * SM_VRAM_WIDTH * SM_VRAM_BYTES;

		if (av24) {
			vramline += av24_offset[lof];
		} else {
			if (fmode & 0x10)
				vramline += SM_VRAM_WIDTH * SM_VRAM_BYTES;
		}

		for (x = 0; x < src->inwidth; x++) {
			uae_u8 *s = line + ((x << 1) / hdbl) * src->pixbytes;
			uae_u8 *d = dstline + ((x << 1) / hdbl) * dst->pixbytes;
			uae_u8 *vramptr = vramline + ((x << 1) / hdbl) * SM_VRAM_BYTES;
			uae_u8 *s2 = s + src->rowbytes;
			uae_u8 *d2 = d + dst->rowbytes;
			if (writetovram) {
				uae_u8 val[3];
				val[0] = FVR(src, s) >> 4;
				val[1] = FVG(src, s) >> 4;
				val[2] = FVB(src, s) >> 4;
				if (av24) {
					uae_u8 v;

					/*
					R3 = 20 08 08 08
					R2 = 20 01 01 01
					R1 = 10 02 02 02
					R0 = 08 04 04 04

					G3 = 20 04 04 04
					G2 = 10 08 08 08
					G1 = 10 01 01 01
					G0 = 08 02 02 02

					B3 = 20 02 02 02
					B2 = 10 04 04 04
					B1 = 08 08 08 08
					B0 = 08 01 01 01
					*/

					uae_u8 rval = vramptr[0];
					uae_u8 gval = vramptr[1];
					uae_u8 bval = vramptr[2];

					/* This probably should use lookup tables.. */

					if (fmode & 0x01) { // Red (low)

						v = (((val[0] >> 2) & 1) << 0);
						rval &= ~(0x01);
						rval |= v;

						v = (((val[1] >> 1) & 1) << 0);
						gval &= ~(0x01);
						gval |= v;

						v = (((val[2] >> 3) & 1) << 1) | (((val[2] >> 0) & 1) << 0);
						bval &= ~(0x02 | 0x01);
						bval |= v;
					}

					if (fmode & 0x02) { // Green (low)

						v = (((val[0] >> 1) & 1) << 1);
						rval &= ~(0x02);
						rval |= v;

						v = (((val[1] >> 0) & 1) << 1) | (((val[1] >> 3) & 1) << 2);
						gval &= ~(0x02 | 0x04);
						gval |= v;

						v = (((val[2] >> 2) & 1) << 2);
						bval &= ~(0x04);
						bval |= v;
					}

					if (fmode & 0x04) { // Blue (low)

						v = (((val[0] >> 0) & 1) << 2) | (((val[0] >> 3) & 1) << 3);
						rval &= ~(0x04 | 0x08);
						rval |= v;

						v = (((val[1] >> 2) & 1) << 3);
						gval &= ~(0x08);
						gval |= v;

						v = (((val[2] >> 1) & 1) << 3);
						bval &= ~(0x08);
						bval |= v;
					}

					if (fmode & 0x08) { // Red (high)
						
						v = (((val[0] >> 2) & 1) << 4);
						rval &= ~(0x10);
						rval |= v;
						
						v = (((val[1] >> 1) & 1) << 4);
						gval &= ~(0x10);
						gval |= v;

						v = (((val[2] >> 3) & 1) << 5) | (((val[2] >> 0) & 1) << 4);
						bval &= ~(0x20 | 0x10);
						bval |= v;
					}

					if (fmode & 0x10) { // Green (high)

						v = (((val[0] >> 1) & 1) << 5);
						rval &= ~(0x20);
						rval |= v;

						v = (((val[1] >> 0) & 1) << 5) | (((val[1] >> 3) & 1) << 6);
						gval &= ~(0x20 | 0x40);
						gval |= v;

						v = (((val[2] >> 2) & 1) << 6);
						bval &= ~(0x40);
						bval |= v;
					}

					if (fmode & 0x20) { // Blue (high)

						v = (((val[0] >> 0) & 1) << 6) | (((val[0] >> 3) & 1) << 7);
						rval &= ~(0x40 | 0x80);
						rval |= v;

						v = (((val[1] >> 2) & 1) << 7);
						gval &= ~(0x80);
						gval |= v;

						v = (((val[2] >> 1) & 1) << 7);
						bval &= ~(0x80);
						bval |= v;
					}

					if (av24_doublebuffer[lof] == 0 && (fmode & (0x08 | 0x10 | 0x20))) {
						rval = (rval & 0xf0) | (rval >> 4);
						gval = (gval & 0xf0) | (gval >> 4);
						bval = (bval & 0xf0) | (bval >> 4);
					}

					vramptr[0] = rval;
					vramptr[1] = gval;
					vramptr[2] = bval;

				} else {

					uae_u8 v = val[offset];
					vramptr[offset] = (v << 4) | (v & 15);

				}
			}
			if (writetovram || mode == 7 || (mode == 6 && FVR(src, s) == 0 && FVG(src, s) == 0 && FVB(src, s) == 0)) {
				uae_u8 r, g, b;
				r = vramptr[0];
				g = vramptr[1];
				b = vramptr[2];
				if (av24_doublebuffer[lof] == 1) {
					r = (r << 4) | (r & 0x0f);
					g = (g << 4) | (g & 0x0f);
					b = (b << 4) | (b & 0x0f);
				} else if (av24_doublebuffer[lof] == 2) {
					r = (r >> 4) | (r & 0xf0);
					g = (g >> 4) | (g & 0xf0);
					b = (b >> 4) | (b & 0xf0);
				}
				PUT_PRGB(d, d2, dst, r, g, b, xaddpix, doublelines, false);
			} else {
				PUT_AMIGARGB(d, s, d2, s2, dst, xaddpix, doublelines, false);
			}
		}
	}

	dst->nativepositioning = true;
	if (monitor != MONITOREMU_AVIDEO12 && monitor != MONITOREMU_AVIDEO24) {
		monitor = av24 ? MONITOREMU_AVIDEO24 : MONITOREMU_AVIDEO12;
		write_log (_T("AVIDEO%d mode\n"), av24 ? 24 : 12);
	}

	return true;
}

void specialmonitor_store_fmode(int vpos, int hpos, uae_u16 fmode)
{
	int lof = lof_store ? 0 : 1;
	if (vpos < 0) {
		for (int i = 0; i < 2; i++) {
			avideo_previous_fmode[i] = 0;
			av24_offset[i] = 0;
			av24_doublebuffer[i] = 0;
			av24_mode[i] = 0;
			av24_writetovram[i] = 0;
		}
		avideo_allowed = 0;
		return;
	}
	if (currprefs.monitoremu == MONITOREMU_AUTO) {
		if ((fmode & 0x0080))
			avideo_allowed = 24;
		if ((fmode & 0x00f0) < 0x40 && !avideo_allowed)
			avideo_allowed = 12;
		if (!avideo_allowed)
			return;
	}
	//write_log(_T("%04x\n"), fmode);

	if (fmode == 0x91) {
		av24_offset[lof] = SM_VRAM_WIDTH * SM_VRAM_BYTES;
	}
	if (fmode == 0x92) {
		av24_offset[lof] = 0;
	}

	if (fmode & 0x8000) {
		av24_doublebuffer[lof] = (fmode & 0x4000) ? 1 : 2;
		av24_mode[lof] = 6;
	}

	if ((fmode & 0xc0) == 0x40) {
		av24_writetovram[lof] = (fmode & 0x3f) != 0;
	}

	if (fmode == 0x80) {
		av24_mode[lof] = 0;
	}

	if (fmode == 0x13) {
		av24_mode[lof] = 6;
		av24_doublebuffer[lof] = 0;
	}

	avideo_previous_fmode[lof] = fmode;
}

static bool do_avideo(struct vidbuffer *src, struct vidbuffer *dst)
{
	bool v;
	int lof = lof_store ? 0 : 1;
	if (interlace_seen) {
		if (currprefs.gfx_iscanlines) {
			v = avideo(src, dst, false, lof, lof);
			if (v && currprefs.gfx_iscanlines > 1)
				blank_generic(src, dst, !lof);
		} else {
			v = avideo(src, dst, false, 0, 0);
			v |= avideo(src, dst, false, 1, 1);
		}
	} else {
		v = avideo(src, dst, true, 0, lof);
	}
	return v;
}


static bool videodac18(struct vidbuffer *src, struct vidbuffer *dst, bool doublelines, int oddlines)
{
	int y, x, vdbl, hdbl;
	int ystart, yend, isntsc;
	int xadd, xaddpix;
	uae_u16 hsstrt, hsstop, vsstrt, vsstop;
	int xstart, xstop;

	if ((beamcon0 & (0x80 | 0x100 | 0x200 | 0x10)) != 0x300)
		return false;
	getsyncregisters(&hsstrt, &hsstop, &vsstrt, &vsstop);

	if (hsstop >= (maxhpos & ~1))
		hsstrt = 0;
	xstart = ((hsstrt * 2) << RES_MAX) - src->xoffset;
	xstop = ((hsstop * 2) << RES_MAX) - src->xoffset;

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	vdbl = gfxvidinfo.ychange;
	hdbl = gfxvidinfo.xchange;

	xaddpix = (1 << 1) / hdbl;
	xadd = ((1 << 1) / hdbl) * src->pixbytes;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	uae_u8 r = 0, g = 0, b = 0;
	for (y = ystart; y < yend; y++) {
		int oddeven = 0;
		uae_u8 prev = 0;
		int yoff = (((y * 2 + oddlines) - src->yoffset) / vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;
		uae_u8 *line = src->bufmem + yoff * src->rowbytes;
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) / vdbl) * dst->rowbytes;
		r = g = b = 0;
		for (x = 0; x < src->inwidth; x++) {
			uae_u8 *s = line + ((x << 1) / hdbl) * src->pixbytes;
			uae_u8 *d = dstline + ((x << 1) / hdbl) * dst->pixbytes;
			uae_u8 *s2 = s + src->rowbytes;
			uae_u8 *d2 = d + dst->rowbytes;
			uae_u8 newval = FIRGB(src, s);
			uae_u8 val = prev | (newval << 4);
			if (oddeven) {
				int mode = val >> 6;
				int data = (val & 63) << 2;
				if (mode == 0) {
					r = data;
					g = data;
					b = data;
				} else if (mode == 1) {
					b = data;
				} else if (mode == 2) {
					r = data;
				} else {
					g = data;
				}
				if (y >= vsstrt && y < vsstop && x >= xstart && y < xstop) {
					PUT_PRGB(d, d2, dst, r, g, b, xaddpix, doublelines, true);
				} else {
					PUT_AMIGARGB(d, s, d2, s2, dst, xaddpix, doublelines, true);
				}
			}
			oddeven = oddeven ? 0 : 1;
			prev = val >> 4;
		}
	}

	dst->nativepositioning = true;
	if (monitor != MONITOREMU_VIDEODAC18) {
		monitor = MONITOREMU_VIDEODAC18;
		write_log (_T("Video DAC 18 mode\n"));
	}

	return true;
}

static bool do_videodac18(struct vidbuffer *src, struct vidbuffer *dst)
{
	bool v;
	if (interlace_seen) {
		if (currprefs.gfx_iscanlines) {
			v = videodac18(src, dst, false, lof_store ? 0 : 1);
			if (v && currprefs.gfx_iscanlines > 1)
				blank_generic(src, dst, lof_store ? 1 : 0);
		} else {
			v = videodac18(src, dst, false, 0);
			v |= videodac18(src, dst, false, 1);
		}
	} else {
		v = videodac18(src, dst, true, 0);
	}
	return v;
}

static const uae_u8 ham_e_magic_cookie[] = { 0xa2, 0xf5, 0x84, 0xdc, 0x6d, 0xb0, 0x7f  };
static const uae_u8 ham_e_magic_cookie_reg = 0x14;
static const uae_u8 ham_e_magic_cookie_ham = 0x18;

static bool ham_e(struct vidbuffer *src, struct vidbuffer *dst, bool doublelines, int oddlines)
{
	int y, x, vdbl, hdbl;
	int ystart, yend, isntsc;
	int xadd, xaddpix;
	bool hameplus = currprefs.monitoremu == MONITOREMU_HAM_E_PLUS;

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	vdbl = gfxvidinfo.ychange;
	hdbl = gfxvidinfo.xchange;

	xaddpix = (1 << 1) / hdbl;
	xadd = ((1 << 1) / hdbl) * src->pixbytes;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	uae_u8 r, g, b;
	/* or is an alternative operator and cannot be used as an identifier */
	uae_u8 or_, og, ob;
	int pcnt = 0;
	int bank = 0;
	int mode_active = 0;
	bool prevzeroline = false;
	int was_active = 0;
	bool cookie_line = false;
	int cookiestartx = 10000;
	for (y = ystart; y < yend; y++) {
		int yoff = (((y * 2 + oddlines) - src->yoffset) / vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;
		uae_u8 *line = src->bufmem + yoff * src->rowbytes;
		uae_u8 *line_genlock = row_map_genlock[yoff];
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) / vdbl) * dst->rowbytes;

		bool getpalette = false;
		uae_u8 prev = 0;
		bool zeroline = true;
		int oddeven = 0;
		for (x = 0; x < src->inwidth; x++) {
			uae_u8 *s = line + ((x << 1) / hdbl) * src->pixbytes;
			uae_u8 *s_genlock = line_genlock + ((x << 1) / hdbl);
			uae_u8 *d = dstline + ((x << 1) / hdbl) * dst->pixbytes;
			uae_u8 *s2 = s + src->rowbytes;
			uae_u8 *d2 = d + dst->rowbytes;
			uae_u8 newval = FIRGB(src, s);
			uae_u8 val = prev | newval;

			if (s_genlock[0])
				zeroline = false;

			if (val == ham_e_magic_cookie[0] && x + sizeof ham_e_magic_cookie + 1 < src->inwidth) {
				int i;
				for (i = 1; i <= sizeof ham_e_magic_cookie; i++) {
					uae_u8 val2 = (FIRGB(src, s + (i * 2 - 1) * xadd) << 4) | FIRGB(src, s + (i * 2 + 0) * xadd);
					if (i < sizeof ham_e_magic_cookie) {
						if (val2 != ham_e_magic_cookie[i])
							break;
					} else if (val2 == ham_e_magic_cookie_reg || val2 == ham_e_magic_cookie_ham) {
						mode_active = val2;
						getpalette = true;
						prevzeroline = false;
						cookiestartx = x - 1;
						x += i * 2;
						oddeven = 0;
						cookie_line = true;
					}
				}
				if (i == sizeof ham_e_magic_cookie + 1)
					continue;
			}

			if (!cookie_line && x == cookiestartx)
				oddeven = 0;

			if (oddeven) {
				if (getpalette) {
					graffiti_palette[pcnt] = val;
					pcnt++;
					if ((pcnt & 3) == 3)
						pcnt++;
					// 64 colors/line
					if ((pcnt & ((4 * 64) - 1)) == 0)
						getpalette = false;
					pcnt &= (4 * 256) - 1;
				}
				if (mode_active) {
					if (cookie_line || x < cookiestartx) {
						r = g = b = 0;
						or_ = og = ob = 0;
					} else {
						if (mode_active == ham_e_magic_cookie_reg) {
							uae_u8 *pal = &graffiti_palette[val * 4];
							r = pal[0];
							g = pal[1];
							b = pal[2];
						} else if (mode_active == ham_e_magic_cookie_ham) {
							int mode = val >> 6;
							int color = val & 63;
							if (mode == 0 && color <= 59) {
								uae_u8 *pal = &graffiti_palette[(bank + color) * 4];
								r = pal[0];
								g = pal[1];
								b = pal[2];
							} else if (mode == 0) {
								bank = (color & 3) * 64;
							} else if (mode == 1) {
								b = color << 2;
							} else if (mode == 2) {
								r = color << 2;
							} else if (mode == 3) {
								g = color << 2;
							}
						}
					}

					if (hameplus) {
						uae_u8 ar, ag, ab;

						ar = (r + or_) / 2;
						ag = (g + og) / 2;
						ab = (b + ob) / 2;

						if (xaddpix >= 2) {
							PRGB(dst, d - dst->pixbytes, ar, ag, ab);
							PRGB(dst, d, ar, ag, ab);
							PRGB(dst, d + 1 * dst->pixbytes, r, g, b);
							PRGB(dst, d + 2 * dst->pixbytes, r, g, b);
							if (doublelines) {
								PRGB(dst, d2 - dst->pixbytes, ar, ag, ab);
								PRGB(dst, d2, ar, ag, ab);
								PRGB(dst, d2 + 1 * dst->pixbytes, r, g, b);
								PRGB(dst, d2 + 2 * dst->pixbytes, r, g, b);
							}
						} else {
							PRGB(dst, d - dst->pixbytes, ar, ag, ab);
							PRGB(dst, d, r, g, b);
							if (doublelines) {
								PRGB(dst, d2 - dst->pixbytes, ar, ag, ab);
								PRGB(dst, d2, r, g, b);
							}
						}
						or_ = r;
						og = g;
						ob = b;
					} else {
						PUT_PRGB(d, d2, dst, r, g, b, xaddpix, doublelines, true);
					}
				} else {
					PUT_AMIGARGB(d, s, d2, s2, dst, xaddpix, doublelines, true);
				}
			}

			oddeven = oddeven ? 0 : 1;
			prev = val << 4;
		}

		if (cookie_line) {
			// Erase magic cookie. I assume real HAM-E would erase it
			// because not erasing it would look really ugly.
			memset(dstline, 0, dst->outwidth * dst->pixbytes);
			if (doublelines)
				memset(dstline + dst->rowbytes, 0, dst->outwidth * dst->pixbytes);
		}

		cookie_line = false;
		if (mode_active)
			was_active = mode_active;
		if (zeroline) {
			if (prevzeroline) {
				mode_active = 0;
				pcnt = 0;
				cookiestartx = 10000;
			}
			prevzeroline = true;
		} else {
			prevzeroline = false;
		}

	}

	if (was_active) {
		dst->nativepositioning = true;
		if (monitor != MONITOREMU_HAM_E) {
			monitor = MONITOREMU_HAM_E;
			write_log (_T("HAM-E mode, %s\n"), was_active == ham_e_magic_cookie_reg ? _T("REG") : _T("HAM"));
		}
	}

	return was_active != 0;
}

static bool do_hame(struct vidbuffer *src, struct vidbuffer *dst)
{
	bool v;
	if (interlace_seen) {
		if (currprefs.gfx_iscanlines) {
			v = ham_e(src, dst, false, lof_store ? 0 : 1);
			if (v && currprefs.gfx_iscanlines > 1)
				blank_generic(src, dst, lof_store ? 1 : 0);
		} else {
			v = ham_e(src, dst, false, 0);
			v |= ham_e(src, dst, false, 1);
		}
	} else {
		v = ham_e(src, dst, true, 0);
	}
	return v;
}

static bool graffiti(struct vidbuffer *src, struct vidbuffer *dst)
{
	int y, x;
	int ystart, yend, isntsc;
	int xstart, xend;
	uae_u8 *srcbuf, *srcend;
	uae_u8 *dstbuf;
	bool command, hires, found;
	int xadd, xpixadd, extrapix;
	int waitline = 0, dbl;
	uae_u8 read_mask = 0xff, color = 0, color2 = 0;

	if (!(bplcon0 & 0x0100)) // GAUD
		return false;

	command = true;
	found = false;
	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	dbl = gfxvidinfo.ychange == 1 ? 2 : 1;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;
	if (src->yoffset >= (ystart << VRES_MAX))
		ystart = src->yoffset >> VRES_MAX;

	xadd = gfxvidinfo.xchange == 1 ? src->pixbytes * 2 : src->pixbytes;
	xpixadd = gfxvidinfo.xchange == 1 ? 4 : 2;

	xstart = 0x1c * 2 + 1;
	xend = 0xf0 * 2 + 1;
	if (!(currprefs.chipset_mask & CSMASK_AGA)) {
		xstart++;
		xend++;
	}

	srcbuf = src->bufmem + (((ystart << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes + (((xstart << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * src->pixbytes;
	srcend = src->bufmem + (((yend << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes;
	extrapix = 0;

	dstbuf = dst->bufmem + (((ystart << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * dst->rowbytes + (((xstart << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * dst->pixbytes;

	y = 0;
	while (srcend > srcbuf && dst->bufmemend > dstbuf) {
		uae_u8 *srcp = srcbuf + extrapix;
		uae_u8 *dstp = dstbuf;

		x = xstart;
		while (x < xend) {
			
			uae_u8 mask = 0x80;
			uae_u8 chunky[4] = { 0, 0, 0, 0 };
			while (mask) {
				if (FR(src, srcp)) // R
					chunky[3] |= mask;
				if (FG(src, srcp)) // G
					chunky[2] |= mask;
				if (FB(src, srcp)) // B
					chunky[1] |= mask;
				if (FI(src, srcp)) // I
					chunky[0] |= mask;
				srcp += xadd;
				mask >>= 1;
			}

			if (command) {
				if (chunky[0] || chunky[1] || chunky[2] || chunky[3] || found) {
					for (int pix = 0; pix < 2; pix++) {
						uae_u8 cmd = chunky[pix * 2 + 0];
						uae_u8 parm = chunky[pix * 2 + 1];

						if (automatic && cmd >= 0x40)
							return false;
						if (cmd != 0)
							found = true;
						if (cmd & 8) {
							command = false;
							dbl = 1;
							waitline = 2;
							if (0 && (cmd & 16)) {
								hires = true;
								xadd /= 2;
								xpixadd /= 2;
								extrapix = -4 * src->pixbytes;
							} else {
								hires = false;
							}
							if (xpixadd == 0) // shres needed
								return false;
							if (monitor != MONITOREMU_GRAFFITI)
								clearmonitor(dst);
						} else if (cmd & 4) {
							if ((cmd & 3) == 1) {
								read_mask = parm;
							} else if ((cmd & 3) == 2) {
								graffiti_palette[color * 4 + color2] = (parm << 2) | (parm & 3);
								color2++;
								if (color2 == 3) {
									color2 = 0;
									color++;
								}
							} else if ((cmd & 3) == 0) {
								color = parm;
								color2 = 0;
							}
						}
					}
				}

				memset(dstp, 0, dst->pixbytes * 4 * 2);
				dstp += dst->pixbytes * 4 * 2;

			} else if (waitline) {
			
				memset(dstp, 0, dst->pixbytes * 4 * 2);
				dstp += dst->pixbytes * 4 * 2;
			
			} else {

				for (int pix = 0; pix < 4; pix++) {
					uae_u8 r, g, b, c;
					
					c = chunky[pix] & read_mask;
					r = graffiti_palette[c * 4 + 0];
					g = graffiti_palette[c * 4 + 1];
					b = graffiti_palette[c * 4 + 2];
					PRGB(dst, dstp, r, g, b);
					dstp += dst->pixbytes;
					PRGB(dst, dstp, r, g, b);
					dstp += dst->pixbytes;
					
					if (gfxvidinfo.xchange == 1 && !hires) {
						PRGB(dst, dstp, r, g, b);
						dstp += dst->pixbytes;
						PRGB(dst, dstp, r, g, b);
						dstp += dst->pixbytes;
					}
				}

			}

			x += xpixadd;
		}

		y++;
		srcbuf += src->rowbytes * dbl;
		dstbuf += dst->rowbytes * dbl;
		if (waitline > 0)
			waitline--;
	}

	dst->nativepositioning = true;

	if (monitor != MONITOREMU_GRAFFITI) {
		monitor = MONITOREMU_GRAFFITI;
		write_log (_T("GRAFFITI %s mode\n"), hires ? _T("hires") : _T("lores"));
	}

	return true;
}

/* A2024 information comes from US patent 4851826 */

static bool a2024(struct vidbuffer *src, struct vidbuffer *dst)
{
	int y;
	uae_u8 *srcbuf, *dstbuf;
	uae_u8 *dataline;
	int px, py, doff, pxcnt, dbl;
	int panel_width, panel_width_draw, panel_height, srcxoffset;
	bool f64, interlace, expand, wpb, less16;
	uae_u8 enp, dpl;
	bool hires, ntsc, found;
	int idline;
	int total_width, total_height;
	
	dbl = gfxvidinfo.ychange == 1 ? 2 : 1;
	doff = (128 * 2 / gfxvidinfo.xchange) * src->pixbytes;
	found = false;

	for (idline = 21; idline <= 29; idline += 8) {
		if (src->yoffset > (idline << VRES_MAX))
			continue;
		// min 178 max 234
		dataline = src->bufmem + (((idline << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes + (((200 << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * src->pixbytes;

#if 0
		write_log (_T("%02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x\n"),
			dataline[0 * doff + 0], dataline[0 * doff + 1], dataline[0 * doff + 2],
			dataline[1 * doff + 0], dataline[1 * doff + 1], dataline[1 * doff + 2],
			dataline[2 * doff + 0], dataline[2 * doff + 1], dataline[2 * doff + 2],
			dataline[3 * doff + 0], dataline[3 * doff + 1], dataline[3 * doff + 2]);
#endif

		if (FB(src, &dataline[0 * doff]))			// 0:B = 0
			continue;
		if (!FI(src, &dataline[0 * doff]))			// 0:I = 1
			continue;
		if (FI(src, &dataline[2 * doff]))			// 2:I = 0
			continue;
		if (!FI(src, &dataline[3 * doff]))			// 3:I = 1
			continue;

		ntsc = idline < 26;
		found = true;
		break;
	}

	if (!found)
		return false;

	px = py = 0;
	if (FB(src, &dataline[1 * doff])) // 1:B FN2
		px |= 2;
	if (FG(src, &dataline[1 * doff])) // 1:G FN1
		px |= 1;
	if (FR(src, &dataline[1 * doff])) // 1:R FN0
		py |= 1;

	f64 = FR(src, &dataline[0 * doff]) != 0;		// 0:R
	interlace = FG(src, &dataline[0 * doff]) != 0;	// 0:G (*Always zero)
	expand = FI(src, &dataline[1 * doff]) != 0;		// 1:I (*Always set)
	enp = FR(src, &dataline[2 * doff]) ? 1 : 0;		// 2:R (*ENP=3)
	enp |= FG(src, &dataline[2 * doff]) ? 2 : 0;	// 2:G
	wpb = FB(src, &dataline[2 * doff]) != 0;		// 2:B (*Always zero)
	dpl = FR(src, &dataline[3 * doff]) ? 1 : 0;		// 3:R (*DPL=3)
	dpl |= FG(src, &dataline[3 * doff]) ? 2 : 0;	// 3:G
	less16 = FB(src, &dataline[3 * doff]) != 0;		// 3:B

	/* (*) = AOS A2024 driver static bits. Not yet implemented in emulation. */

	if (f64) {
		panel_width = 336;
		panel_width_draw = px == 2 ? 352 : 336;
		pxcnt = 3;
		hires = false;
		srcxoffset = 113;
		if (px > 2)
			return false;
		total_width = 336 + 336 + 352;
	} else {
		panel_width = 512;
		panel_width_draw = 512;
		pxcnt = 2;
		hires = true;
		srcxoffset = 129;
		if (px > 1)
			return false;
		total_width = 512 + 512;
	}
	panel_height = ntsc ? 400 : 512;

	if (monitor != MONITOREMU_A2024) {
		clearmonitor(dst);
	}

#if 0
	write_log (_T("0 = F6-4:%d INTERLACE:%d\n"), f64, interlace);
	write_log (_T("1 = FN:%d EXPAND:%d\n"), py + px *2, expand);
	write_log (_T("2 = ENP:%d WPB=%d\n"), enp, wpb);
	write_log (_T("3 = DPL:%d LESS16=%d\n"), dpl, less16);
#endif
#if 0
	write_log (_T("%02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %dx%d\n"),
		dataline[0 * doff + 0], dataline[0 * doff + 1], dataline[0 * doff + 2],
		dataline[1 * doff + 0], dataline[1 * doff + 1], dataline[1 * doff + 2],
		dataline[2 * doff + 0], dataline[2 * doff + 1], dataline[2 * doff + 2],
		dataline[3 * doff + 0], dataline[3 * doff + 1], dataline[3 * doff + 2],
		px, py);
#endif

	if (less16) {
		total_width -= 16;
		if (px == pxcnt - 1)
			panel_width_draw -= 16;
	}
	total_height = panel_height * dbl;
	
	srcbuf = src->bufmem + (((44 << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes + (((srcxoffset << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * src->pixbytes;
	dstbuf = dst->bufmem + py * (panel_height / gfxvidinfo.ychange) * dst->rowbytes + px * ((panel_width * 2) / gfxvidinfo.xchange) * dst->pixbytes;

	for (y = 0; y < (panel_height / (dbl == 1 ? 1 : 2)) / gfxvidinfo.ychange; y++) {
#if 0
		memcpy (dstbuf, srcbuf, ((panel_width * 2) / gfxvidinfo.xchange) * dst->pixbytes);
#else
		uae_u8 *srcp = srcbuf;
		uae_u8 *dstp1 = dstbuf;
		uae_u8 *dstp2 = dstbuf + dst->rowbytes;
		int x;
		for (x = 0; x < (panel_width_draw * 2) / gfxvidinfo.xchange; x++) {
			uae_u8 c1 = 0, c2 = 0;
			if (FR(src, srcp)) // R
				c1 |= 2;
			if (FG(src, srcp)) // G
				c2 |= 2;
			if (FB(src, srcp)) // B
				c1 |= 1;
			if (FI(src, srcp)) // I
				c2 |= 1;
			if (dpl == 0) {
				c1 = c2 = 0;
			} else if (dpl == 1) {
				c1 &= 1;
				c1 |= c1 << 1;
				c2 &= 1;
				c2 |= c2 << 1;
			} else if (dpl == 2) {
				c1 &= 2;
				c1 |= c1 >> 1;
				c2 &= 2;
				c2 |= c2 >> 1;
			}
			if (dbl == 1) {
				c1 = (c1 + c2 + 1) / 2;
				c1 = (c1 << 6) | (c1 << 4) | (c1 << 2) | (c1 << 0);
				PRGB(dst, dstp1, c1, c1, c1);
			} else {
				c1 = (c1 << 6) | (c1 << 4) | (c1 << 2) | (c1 << 0);
				c2 = (c2 << 6) | (c2 << 4) | (c2 << 2) | (c2 << 0);
				PRGB(dst, dstp1, c1, c1, c1);
				PRGB(dst, dstp2, c2, c2, c2);
				dstp2 += dst->pixbytes;
			}
			srcp += src->pixbytes;
			if (!hires)
				srcp += src->pixbytes;
			dstp1 += dst->pixbytes;
		}
#endif
		srcbuf += src->rowbytes * dbl;
		dstbuf += dst->rowbytes * dbl;
	}

	total_width /= 2;
	total_width <<= currprefs.gfx_resolution;

	dst->outwidth = total_width;
	dst->outheight = total_height;
	dst->inwidth = total_width;
	dst->inheight = total_height;
	dst->inwidth2 = total_width;
	dst->inheight2 = total_height;
	dst->nativepositioning = false;

	if (monitor != MONITOREMU_A2024) {
		monitor = MONITOREMU_A2024;
		write_log (_T("A2024 %dHz %s mode\n"), hires ? 10 : 15, ntsc ? _T("NTSC") : _T("PAL"));
	}

	return true;
}

static bool emulate_specialmonitors2(struct vidbuffer *src, struct vidbuffer *dst)
{
	automatic = false;
	if (currprefs.monitoremu == MONITOREMU_AUTO) {
		automatic = true;
		bool v = a2024(src, dst);
		if (!v)
			v = graffiti(src, dst);
		if (!v)
			v = do_videodac18(src, dst);
		if (!v) {
			if (avideo_allowed)
				v = do_avideo(src, dst);
		}
		return v;
	} else if (currprefs.monitoremu == MONITOREMU_A2024) {
		return a2024(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_GRAFFITI) {
		return graffiti(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_DCTV) {
		return do_dctv(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_HAM_E || currprefs.monitoremu == MONITOREMU_HAM_E_PLUS) {
		return do_hame(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_VIDEODAC18) {
		return do_videodac18(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_AVIDEO12 || currprefs.monitoremu == MONITOREMU_AVIDEO24) {
		avideo_allowed = -1;
		return do_avideo(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_FIRECRACKER24) {
		return do_firecracker24(src, dst);
	}
	return false;
}


bool emulate_specialmonitors(struct vidbuffer *src, struct vidbuffer *dst)
{
	if (!emulate_specialmonitors2(src, dst)) {
		if (monitor) {
			clearmonitor(dst);
			monitor = 0;
			write_log (_T("Native mode\n"));
		}
		return false;
	}
	return true;
}

void specialmonitor_reset(void)
{
	if (!currprefs.monitoremu)
		return;
	specialmonitor_store_fmode(-1, -1, 0);
	fc24_reset();
}

bool specialmonitor_need_genlock(void)
{
	switch (currprefs.monitoremu)
	{
		case MONITOREMU_FIRECRACKER24:
		case MONITOREMU_HAM_E:
		case MONITOREMU_HAM_E_PLUS:
		return true;
	}
	if (currprefs.genlock_image && currprefs.genlock)
		return true;
	return false;
}

static uae_u8 *genlock_image;
static int genlock_image_width, genlock_image_height, genlock_image_pitch;
static uae_u8 noise_buffer[1024];
static uae_u32 noise_seed, noise_add, noise_index;

static uae_u32 quickrand(void)
{
	noise_seed = (noise_seed >> 1) ^ (0 - (noise_seed & 1) & 0xd0000001);
	return noise_seed;
}

static void init_noise(void)
{
	noise_seed++;
	for (int i = 0; i < sizeof noise_buffer; i++) {
		noise_buffer[i] = quickrand();
	}
}

static uae_u8 get_noise(void)
{
	noise_index += noise_add;
	noise_index &= 1023;
	return noise_buffer[noise_index];
}

#include "png.h"

struct png_cb
{
	uae_u8 *ptr;
	int size;
};

static void __cdecl readcallback(png_structp png_ptr, png_bytep out, png_size_t count)
{
	png_voidp io_ptr = png_get_io_ptr(png_ptr);

	if (!io_ptr)
		return;
	struct png_cb *cb = (struct png_cb*)io_ptr;
	if (count > cb->size)
		count = cb->size;
	memcpy(out, cb->ptr, count);
	cb->ptr += count;
	cb->size -= count;
}

static void load_genlock_image(void)
{
	extern unsigned char test_card_png[];
	extern unsigned int test_card_png_len;
	uae_u8 *b = test_card_png;
	uae_u8 *bfree = NULL;
	int file_size = test_card_png_len;
	png_structp png_ptr;
	png_infop info_ptr;
	png_uint_32 width, height;
	int depth, color_type;
	struct png_cb cb;
	png_bytepp row_pp;
	png_size_t cols;

	xfree(genlock_image);
	genlock_image = NULL;

	if (currprefs.genlock_image == 3) {
		int size;
		uae_u8 *bb = zfile_load_file(currprefs.genlock_image_file, &size);
		if (bb) {
			file_size = size;
			b = bb;
			bfree = bb;
		}
	}

	if (!png_check_sig(b, 8))
		goto end;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
	if (!png_ptr)
		goto end;
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, 0, 0);
		goto end;
	}
	cb.ptr = b;
	cb.size = file_size;
	png_set_read_fn(png_ptr, &cb, readcallback);

	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &depth, &color_type, 0, 0, 0);

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_expand(png_ptr);
	if (color_type == PNG_COLOR_TYPE_GRAY && depth < 8)
		png_set_expand(png_ptr);

	if (depth > 8)
		png_set_strip_16(png_ptr);
	if (depth < 8)
		png_set_packing(png_ptr);
	if (!(color_type & PNG_COLOR_MASK_ALPHA))
		png_set_add_alpha(png_ptr, 0, PNG_FILLER_AFTER);

	cols = png_get_rowbytes(png_ptr, info_ptr);

	genlock_image_pitch = width * 4;
	genlock_image_width = width;
	genlock_image_height = height;

	row_pp = new png_bytep[height];
	
	genlock_image = xcalloc(uae_u8, width * height * 4);
	
	for (int i = 0; i < height; i++) {
		row_pp[i] = (png_bytep) &genlock_image[i * genlock_image_pitch];
	}

	png_read_image(png_ptr, row_pp);
	png_read_end(png_ptr, info_ptr);

	png_destroy_read_struct(&png_ptr, &info_ptr, 0);

	delete[] row_pp;
end:
	xfree(bfree);
}

static bool do_genlock(struct vidbuffer *src, struct vidbuffer *dst, bool doublelines, int oddlines)
{
	int y, x, vdbl, hdbl;
	int ystart, yend, isntsc;
	int gl_vdbl_l, gl_vdbl_r;
	int gl_hdbl_l, gl_hdbl_r, gl_hdbl;
	int gl_hcenter, gl_vcenter;
	int mix1 = 0, mix2 = 0;

	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	if (!genlock_image && currprefs.genlock_image == 2) {
		load_genlock_image();
	}
	if (genlock_image && currprefs.genlock_image != 2) {
		xfree(genlock_image);
		genlock_image = NULL;
	}

	if (gfxvidinfo.xchange == 1)
		hdbl = 0;
	else if (gfxvidinfo.xchange == 2)
		hdbl = 1;
	else
		hdbl = 2;

	gl_hdbl_l = gl_hdbl_r = 0;
	if (genlock_image_width < 600) {
		gl_hdbl = 0;
	} else if (genlock_image_width < 1000) {
		gl_hdbl = 1;
	} else {
		gl_hdbl = 2;
	}
	if (hdbl >= gl_hdbl) {
		gl_hdbl_l = hdbl - gl_hdbl;
	} else {
		gl_hdbl_r = gl_hdbl - hdbl;
	}

	if (gfxvidinfo.ychange == 1)
		vdbl = 0;
	else
		vdbl = 1;

	gl_vdbl_l = gl_vdbl_r = 0;

	gl_hcenter = (genlock_image_width - ((src->inwidth << hdbl) >> gl_hdbl)) / 2;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;

	gl_vcenter = (((genlock_image_height << gl_vdbl_l) >> gl_vdbl_r) - (((yend - ystart) * 2))) / 2;

	init_noise();

	if(currprefs.genlock_mix) {
		mix1 = 256 - currprefs.genlock_mix;
		mix2 = currprefs.genlock_mix;
	}

	uae_u8 r = 0, g = 0, b = 0;
	for (y = ystart; y < yend; y++) {
		int yoff = (((y * 2 + oddlines) - src->yoffset) >> vdbl);
		if (yoff < 0)
			continue;
		if (yoff >= src->inheight)
			continue;

		uae_u8 *line = src->bufmem + yoff * src->rowbytes;
		uae_u8 *dstline = dst->bufmem + (((y * 2 + oddlines) - dst->yoffset) >> vdbl) * dst->rowbytes;
		uae_u8 *line_genlock = row_map_genlock[yoff];
		int gy = ((((y * 2 + oddlines) - dst->yoffset) << gl_vdbl_l) >> gl_vdbl_r) + gl_vcenter;
		uae_u8 *image_genlock = genlock_image + gy * genlock_image_pitch;
		r = g = b = 0;
		noise_add = (quickrand() & 15) | 1;
		for (x = 0; x < src->inwidth; x++) {
			uae_u8 *s = line + x * src->pixbytes;
			uae_u8 *d = dstline + x * dst->pixbytes;
			uae_u8 *s_genlock = line_genlock + x;
			uae_u8 *s2 = s + src->rowbytes;
			uae_u8 *d2 = d + dst->rowbytes;

			if (is_transparent(*s_genlock)) {
				if (genlock_image) {
					int gx = (((x + gl_hcenter) << gl_hdbl_l) >> gl_hdbl_r);
					if (gx >= 0 && gx < genlock_image_width && gy >= 0 && gy < genlock_image_height) {
						uae_u8 *s_genlock_image = image_genlock + gx * 4;
						r = s_genlock_image[0];
						g = s_genlock_image[1];
						b = s_genlock_image[2];
					} else {
						r = g = b = 0;
					}
				} else {
					r = g = b = get_noise();
				}
				if (mix2) {
					r = (mix1 * r + mix2 * FVR(src, s)) / 256;
					g = (mix1 * g + mix2 * FVG(src, s)) / 256;
					b = (mix1 * b + mix2 * FVB(src, s)) / 256;
				}
				PUT_PRGB(d, d2, dst, r, g, b, 0, doublelines, false);
			} else {
				PUT_AMIGARGB(d, s, d2, s2, dst, 0, doublelines, false);
			}
		}
	}

	dst->nativepositioning = true;
	return true;
}

bool emulate_genlock(struct vidbuffer *src, struct vidbuffer *dst)
{
	bool v;
	if (interlace_seen) {
		if (currprefs.gfx_iscanlines) {
			v = do_genlock(src, dst, false, lof_store ? 0 : 1);
			if (v && currprefs.gfx_iscanlines > 1)
				blank_generic(src, dst, lof_store ? 1 : 0);
		} else {
			v = do_genlock(src, dst, false, 0);
			v |= do_genlock(src, dst, false, 1);
		}
	} else {
		if (currprefs.gfx_pscanlines) {
			v = do_genlock(src, dst, false, lof_store ? 0 : 1);
			if (v && currprefs.gfx_pscanlines > 1)
				blank_generic(src, dst, lof_store ? 1 : 0);
		} else {
			v = do_genlock(src, dst, true, 0);
		}
	}
	return v;
}
