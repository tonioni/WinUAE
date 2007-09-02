/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/* Scaler (except Scale2x) code borrowed from ScummVM project */

#include "filter.h"

static uint32 colorMask;
static uint32 lowPixelMask;
static uint32 qcolorMask;
static uint32 qlowpixelMask;
static uint32 redblueMask;
static uint32 redMask;
static uint32 greenMask;
static uint32 blueMask;

int Init_2xSaI (int rb, int gb, int bb, int rs, int gs, int bs)
{
	if (rb + gb + bb == 16) {
		colorMask = 0xF7DEF7DE;
		lowPixelMask = 0x08210821;
		qcolorMask = 0xE79CE79C;
		qlowpixelMask = 0x18631863;
		redblueMask = 0xF81F;
		redMask = 0xF800;
		greenMask = 0x07E0;
		blueMask = 0x001F;
	} else if (rb + gb + bb == 15) {
		colorMask = 0x7BDE7BDE;
		lowPixelMask = 0x04210421;
		qcolorMask = 0x739C739C;
		qlowpixelMask = 0x0C630C63;
		redblueMask = 0x7C1F;
		redMask = 0x7C00;
		greenMask = 0x03E0;
		blueMask = 0x001F;
	} else {
		return 0;
	}

	return 1;
}

static _inline int GetResult(uint32 A, uint32 B, uint32 C, uint32 D)
{
	const bool ac = (A==C);
	const bool bc = (B==C);
	const int x1 = ac;
	const int y1 = (bc & !ac);
	const bool ad = (A==D);
	const bool bd = (B==D);
	const int x2 = ad;
	const int y2 = (bd & !ad);
	const int x = x1+x2;
	const int y = y1+y2;
	static const int rmap[3][3] = {
			{0, 0, -1},
			{0, 0, -1},
			{1, 1,  0}
		};
	return rmap[y][x];
}

static _inline uint32 INTERPOLATE(uint32 A, uint32 B) {
	if (A != B) {
		return (((A & colorMask) >> 1) + ((B & colorMask) >> 1) + (A & B & lowPixelMask));
	} else
		return A;
}

static _inline uint32 Q_INTERPOLATE(uint32 A, uint32 B, uint32 C, uint32 D) {
	register uint32 x = ((A & qcolorMask) >> 2) + ((B & qcolorMask) >> 2) + ((C & qcolorMask) >> 2) + ((D & qcolorMask) >> 2);
	register uint32 y = ((A & qlowpixelMask) + (B & qlowpixelMask) + (C & qlowpixelMask) + (D & qlowpixelMask)) >> 2;

	y &= qlowpixelMask;
	return x + y;
}

void Super2xSaI(const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height) {
	const uint16 *bP;
	uint16 *dP;
	const uint32 nextlineSrc = srcPitch >> 1;

	while (height--) {
		int i;
		bP = (const uint16 *)srcPtr;
		dP = (uint16 *)dstPtr;

		for (i = 0; i < width; ++i) {
			uint32 color4, color5, color6;
			uint32 color1, color2, color3;
			uint32 colorA0, colorA1, colorA2, colorA3;
			uint32 colorB0, colorB1, colorB2, colorB3;
			uint32 colorS1, colorS2;
			uint32 product1a, product1b, product2a, product2b;

//---------------------------------------    B1 B2
//                                         4  5  6 S2
//                                         1  2  3 S1
//                                           A1 A2

			colorB0 = *(bP - nextlineSrc - 1);
			colorB1 = *(bP - nextlineSrc);
			colorB2 = *(bP - nextlineSrc + 1);
			colorB3 = *(bP - nextlineSrc + 2);

			color4 = *(bP - 1);
			color5 = *(bP);
			color6 = *(bP + 1);
			colorS2 = *(bP + 2);

			color1 = *(bP + nextlineSrc - 1);
			color2 = *(bP + nextlineSrc);
			color3 = *(bP + nextlineSrc + 1);
			colorS1 = *(bP + nextlineSrc + 2);

			colorA0 = *(bP + 2 * nextlineSrc - 1);
			colorA1 = *(bP + 2 * nextlineSrc);
			colorA2 = *(bP + 2 * nextlineSrc + 1);
			colorA3 = *(bP + 2 * nextlineSrc + 2);

//--------------------------------------
			if (color2 == color6 && color5 != color3) {
				product2b = product1b = color2;
			} else if (color5 == color3 && color2 != color6) {
				product2b = product1b = color5;
			} else if (color5 == color3 && color2 == color6) {
				register int r = 0;

				r += GetResult(color6, color5, color1, colorA1);
				r += GetResult(color6, color5, color4, colorB1);
				r += GetResult(color6, color5, colorA2, colorS1);
				r += GetResult(color6, color5, colorB2, colorS2);

				if (r > 0)
					product2b = product1b = color6;
				else if (r < 0)
					product2b = product1b = color5;
				else {
					product2b = product1b = INTERPOLATE(color5, color6);
				}
			} else {
				if (color6 == color3 && color3 == colorA1 && color2 != colorA2 && color3 != colorA0)
					product2b = Q_INTERPOLATE(color3, color3, color3, color2);
				else if (color5 == color2 && color2 == colorA2 && colorA1 != color3 && color2 != colorA3)
					product2b = Q_INTERPOLATE(color2, color2, color2, color3);
				else
					product2b = INTERPOLATE(color2, color3);

				if (color6 == color3 && color6 == colorB1 && color5 != colorB2 && color6 != colorB0)
					product1b = Q_INTERPOLATE(color6, color6, color6, color5);
				else if (color5 == color2 && color5 == colorB2 && colorB1 != color6 && color5 != colorB3)
					product1b = Q_INTERPOLATE(color6, color5, color5, color5);
				else
					product1b = INTERPOLATE(color5, color6);
			}

			if (color5 == color3 && color2 != color6 && color4 == color5 && color5 != colorA2)
				product2a = INTERPOLATE(color2, color5);
			else if (color5 == color1 && color6 == color5 && color4 != color2 && color5 != colorA0)
				product2a = INTERPOLATE(color2, color5);
			else
				product2a = color2;

			if (color2 == color6 && color5 != color3 && color1 == color2 && color2 != colorB2)
				product1a = INTERPOLATE(color2, color5);
			else if (color4 == color2 && color3 == color2 && color1 != color5 && color2 != colorB0)
				product1a = INTERPOLATE(color2, color5);
			else
				product1a = color5;

			*(dP + 0) = (uint16) product1a;
			*(dP + 1) = (uint16) product1b;
			*(dP + dstPitch/2 + 0) = (uint16) product2a;
			*(dP + dstPitch/2 + 1) = (uint16) product2b;

			bP += 1;
			dP += 2;
		}

		srcPtr += srcPitch;
		dstPtr += dstPitch * 2;
	}
}

void SuperEagle(const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height) {
	const uint16 *bP;
	uint16 *dP;
	const uint32 nextlineSrc = srcPitch >> 1;

	while (height--) {
		int i;
		bP = (const uint16 *)srcPtr;
		dP = (uint16 *)dstPtr;
		for (i = 0; i < width; ++i) {
			uint32 color4, color5, color6;
			uint32 color1, color2, color3;
			uint32 colorA1, colorA2, colorB1, colorB2, colorS1, colorS2;
			uint32 product1a, product1b, product2a, product2b;

			colorB1 = *(bP - nextlineSrc);
			colorB2 = *(bP - nextlineSrc + 1);

			color4 = *(bP - 1);
			color5 = *(bP);
			color6 = *(bP + 1);
			colorS2 = *(bP + 2);

			color1 = *(bP + nextlineSrc - 1);
			color2 = *(bP + nextlineSrc);
			color3 = *(bP + nextlineSrc + 1);
			colorS1 = *(bP + nextlineSrc + 2);

			colorA1 = *(bP + 2 * nextlineSrc);
			colorA2 = *(bP + 2 * nextlineSrc + 1);

			// --------------------------------------
			if (color5 != color3)
			{
				if (color2 == color6)
				{
					product1b = product2a = color2;
					if ((color1 == color2) || (color6 == colorB2)) {
						product1a = INTERPOLATE(color2, color5);
						product1a = INTERPOLATE(color2, product1a);
					} else {
						product1a = INTERPOLATE(color5, color6);
					}

					if ((color6 == colorS2) || (color2 == colorA1)) {
						product2b = INTERPOLATE(color2, color3);
						product2b = INTERPOLATE(color2, product2b);
					} else {
						product2b = INTERPOLATE(color2, color3);
					}
				}
				else
				{
					product2b = product1a = INTERPOLATE(color2, color6);
					product2b = Q_INTERPOLATE(color3, color3, color3, product2b);
					product1a = Q_INTERPOLATE(color5, color5, color5, product1a);

					product2a = product1b = INTERPOLATE(color5, color3);
					product2a = Q_INTERPOLATE(color2, color2, color2, product2a);
					product1b = Q_INTERPOLATE(color6, color6, color6, product1b);
				}
			}
			else //if (color5 == color3)
			{
				if (color2 != color6)
				{
					product2b = product1a = color5;

					if ((colorB1 == color5) || (color3 == colorS1)) {
						product1b = INTERPOLATE(color5, color6);
						product1b = INTERPOLATE(color5, product1b);
					} else {
						product1b = INTERPOLATE(color5, color6);
					}

					if ((color3 == colorA2) || (color4 == color5)) {
						product2a = INTERPOLATE(color5, color2);
						product2a = INTERPOLATE(color5, product2a);
					} else {
						product2a = INTERPOLATE(color2, color3);
					}
				}
				else	//if (color2 != color6)
				{
					register int r = 0;

					r += GetResult(color6, color5, color1, colorA1);
					r += GetResult(color6, color5, color4, colorB1);
					r += GetResult(color6, color5, colorA2, colorS1);
					r += GetResult(color6, color5, colorB2, colorS2);

					if (r > 0) {
						product1b = product2a = color2;
						product1a = product2b = INTERPOLATE(color5, color6);
					} else if (r < 0) {
						product2b = product1a = color5;
						product1b = product2a = INTERPOLATE(color5, color6);
					} else {
						product2b = product1a = color5;
						product1b = product2a = color2;
					}
				}
			}

			*(dP + 0) = (uint16) product1a;
			*(dP + 1) = (uint16) product1b;
			*(dP + dstPitch/2 + 0) = (uint16) product2a;
			*(dP + dstPitch/2 + 1) = (uint16) product2b;

			bP += 1;
			dP += 2;
		}

		srcPtr += srcPitch;
		dstPtr += dstPitch * 2;
	}
}

void _2xSaI(const uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height) {
	const uint16 *bP;
	uint16 *dP;
	const uint32 nextlineSrc = srcPitch >> 1;

	while (height--) {
		int i;
		bP = (const uint16 *)srcPtr;
		dP = (uint16 *)dstPtr;

		for (i = 0; i < width; ++i) {

			register uint32 colorA, colorB;
			uint32 colorC, colorD,
				colorE, colorF, colorG, colorH, colorI, colorJ, colorK, colorL, colorM, colorN, colorO, colorP;
			uint32 product, product1, product2;

//---------------------------------------
// Map of the pixels:                    I|E F|J
//                                       G|A B|K
//                                       H|C D|L
//                                       M|N O|P
			colorI = *(bP - nextlineSrc - 1);
			colorE = *(bP - nextlineSrc);
			colorF = *(bP - nextlineSrc + 1);
			colorJ = *(bP - nextlineSrc + 2);

			colorG = *(bP - 1);
			colorA = *(bP);
			colorB = *(bP + 1);
			colorK = *(bP + 2);

			colorH = *(bP + nextlineSrc - 1);
			colorC = *(bP + nextlineSrc);
			colorD = *(bP + nextlineSrc + 1);
			colorL = *(bP + nextlineSrc + 2);

			colorM = *(bP + 2 * nextlineSrc - 1);
			colorN = *(bP + 2 * nextlineSrc);
			colorO = *(bP + 2 * nextlineSrc + 1);
			colorP = *(bP + 2 * nextlineSrc + 2);

			if ((colorA == colorD) && (colorB != colorC)) {
				if (((colorA == colorE) && (colorB == colorL)) ||
					((colorA == colorC) && (colorA == colorF) && (colorB != colorE) && (colorB == colorJ))) {
					product = colorA;
				} else {
					product = INTERPOLATE(colorA, colorB);
				}

				if (((colorA == colorG) && (colorC == colorO)) ||
					((colorA == colorB) && (colorA == colorH) && (colorG != colorC)  && (colorC == colorM))) {
					product1 = colorA;
				} else {
					product1 = INTERPOLATE(colorA, colorC);
				}
				product2 = colorA;
			} else if ((colorB == colorC) && (colorA != colorD)) {
				if (((colorB == colorF) && (colorA == colorH)) ||
					((colorB == colorE) && (colorB == colorD) && (colorA != colorF) && (colorA == colorI))) {
					product = colorB;
				} else {
					product = INTERPOLATE(colorA, colorB);
				}

				if (((colorC == colorH) && (colorA == colorF)) ||
					((colorC == colorG) && (colorC == colorD) && (colorA != colorH) && (colorA == colorI))) {
					product1 = colorC;
				} else {
					product1 = INTERPOLATE(colorA, colorC);
				}
				product2 = colorB;
			} else if ((colorA == colorD) && (colorB == colorC)) {
				if (colorA == colorB) {
					product = colorA;
					product1 = colorA;
					product2 = colorA;
				} else {
					register int r = 0;

					product1 = INTERPOLATE(colorA, colorC);
					product = INTERPOLATE(colorA, colorB);

					r += GetResult(colorA, colorB, colorG, colorE);
					r -= GetResult(colorB, colorA, colorK, colorF);
					r -= GetResult(colorB, colorA, colorH, colorN);
					r += GetResult(colorA, colorB, colorL, colorO);

					if (r > 0)
						product2 = colorA;
					else if (r < 0)
						product2 = colorB;
					else {
						product2 = Q_INTERPOLATE(colorA, colorB, colorC, colorD);
					}
				}
			} else {
				product2 = Q_INTERPOLATE(colorA, colorB, colorC, colorD);

				if ((colorA == colorC) && (colorA == colorF)
						&& (colorB != colorE) && (colorB == colorJ)) {
					product = colorA;
				} else if ((colorB == colorE) && (colorB == colorD)
									 && (colorA != colorF) && (colorA == colorI)) {
					product = colorB;
				} else {
					product = INTERPOLATE(colorA, colorB);
				}

				if ((colorA == colorB) && (colorA == colorH)
						&& (colorG != colorC) && (colorC == colorM)) {
					product1 = colorA;
				} else if ((colorC == colorG) && (colorC == colorD)
									 && (colorA != colorH) && (colorA == colorI)) {
					product1 = colorC;
				} else {
					product1 = INTERPOLATE(colorA, colorC);
				}
			}

			*(dP + 0) = (uint16) colorA;
			*(dP + 1) = (uint16) product;
			*(dP + dstPitch/2 + 0) = (uint16) product1;
			*(dP + dstPitch/2 + 1) = (uint16) product2;

			bP += 1;
			dP += 2;
		}

		srcPtr += srcPitch;
		dstPtr += dstPitch * 2;
	}
}


/*
 * This file is part of the Advance project.
 *
 * Copyright (C) 1999-2002 Andrea Mazzoleni
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * This file contains a C and MMX implentation of the Scale2x effect.
 *
 * You can found an high level description of the effect at :
 *
 * http://scale2x.sourceforge.net/scale2x.html
 *
 * Alternatively at the previous license terms, you are allowed to use this
 * code in your program with these conditions:
 * - the program is not used in commercial activities.
 * - the whole source code of the program is released with the binary.
 * - derivative works of the program are allowed.
 */

#define MMX
extern int cpu_mmx;

/* Suggested in "Intel Optimization" for Pentium II */
#define ASM_JUMP_ALIGN ".p2align 4\n"

static void internal_scale2x_16_def(u16 *dst0, u16* dst1, const u16* src0, const u16* src1, const u16* src2, unsigned count) {
  /* first pixel */
  dst0[0] = src1[0];
  dst1[0] = src1[0];
  if (src1[1] == src0[0] && src2[0] != src0[0])
    dst0[1] =src0[0];
  else
    dst0[1] =src1[0];
  if (src1[1] == src2[0] && src0[0] != src2[0])
    dst1[1] =src2[0];
  else
    dst1[1] =src1[0];
  ++src0;
  ++src1;
  ++src2;
  dst0 += 2;
  dst1 += 2;

  /* central pixels */
  count -= 2;
  while (count) {
    if (src1[-1] == src0[0] && src2[0] != src0[0] && src1[1] != src0[0])
      dst0[0] = src0[0];
    else
      dst0[0] = src1[0];
    if (src1[1] == src0[0] && src2[0] != src0[0] && src1[-1] != src0[0])
      dst0[1] =src0[0];
    else
      dst0[1] =src1[0];

    if (src1[-1] == src2[0] && src0[0] != src2[0] && src1[1] != src2[0])
      dst1[0] =src2[0];
    else
      dst1[0] =src1[0];
    if (src1[1] == src2[0] && src0[0] != src2[0] && src1[-1] != src2[0])
      dst1[1] =src2[0];
    else
      dst1[1] =src1[0];

    ++src0;
    ++src1;
    ++src2;
    dst0 += 2;
    dst1 += 2;
    --count;
  }

  /* last pixel */
  if (src1[-1] == src0[0] && src2[0] != src0[0])
    dst0[0] =src0[0];
  else
    dst0[0] =src1[0];
  if (src1[-1] == src2[0] && src0[0] != src2[0])
    dst1[0] =src2[0];
  else
    dst1[0] =src1[0];
  dst0[1] =src1[0];
  dst1[1] =src1[0];
}

static void internal_scale2x_32_def(u32* dst0,
				    u32* dst1,
				    const u32* src0,
				    const u32* src1,
				    const u32* src2,
				    unsigned count) {
  /* first pixel */
  dst0[0] = src1[0];
  dst1[0] = src1[0];
  if (src1[1] == src0[0] && src2[0] != src0[0])
    dst0[1] = src0[0];
  else
    dst0[1] = src1[0];
  if (src1[1] == src2[0] && src0[0] != src2[0])
    dst1[1] = src2[0];
  else
    dst1[1] = src1[0];
  ++src0;
  ++src1;
  ++src2;
  dst0 += 2;
  dst1 += 2;

  /* central pixels */
  count -= 2;
  while (count) {
    if (src1[-1] == src0[0] && src2[0] != src0[0] && src1[1] != src0[0])
      dst0[0] = src0[0];
    else
      dst0[0] = src1[0];
    if (src1[1] == src0[0] && src2[0] != src0[0] && src1[-1] != src0[0])
      dst0[1] = src0[0];
    else
      dst0[1] = src1[0];

    if (src1[-1] == src2[0] && src0[0] != src2[0] && src1[1] != src2[0])
      dst1[0] = src2[0];
    else
      dst1[0] = src1[0];
    if (src1[1] == src2[0] && src0[0] != src2[0] && src1[-1] != src2[0])
      dst1[1] = src2[0];
    else
      dst1[1] = src1[0];

    ++src0;
    ++src1;
    ++src2;
    dst0 += 2;
    dst1 += 2;
    --count;
  }

  /* last pixel */
  if (src1[-1] == src0[0] && src2[0] != src0[0])
    dst0[0] = src0[0];
  else
    dst0[0] = src1[0];
  if (src1[-1] == src2[0] && src0[0] != src2[0])
    dst1[0] = src2[0];
  else
    dst1[0] = src1[0];
  dst0[1] = src1[0];
  dst1[1] = src1[0];
}

#ifdef MMX
static void internal_scale2x_16_mmx_single(u16* dst, const u16* src0, const u16* src1, const u16* src2, unsigned count) {
  /* always do the first and last run */
  count -= 2*4;

#ifdef __GNUC__
  __asm__ __volatile__(
		       /* first run */
		       /* set the current, current_pre, current_next registers */
		       "pxor %%mm0,%%mm0\n" /* use a fake black out of screen */
		       "movq 0(%1),%%mm7\n"
		       "movq 8(%1),%%mm1\n"
		       "psrlq $48,%%mm0\n"
		       "psllq $48,%%mm1\n"
		       "movq %%mm7,%%mm2\n"
		       "movq %%mm7,%%mm3\n"
		       "psllq $16,%%mm2\n"
		       "psrlq $16,%%mm3\n"
		       "por %%mm2,%%mm0\n"
		       "por %%mm3,%%mm1\n"

		       /* current_upper */
		       "movq (%0),%%mm6\n"

		       /* compute the upper-left pixel for dst0 on %%mm2 */
		       /* compute the upper-right pixel for dst0 on %%mm4 */
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "movq %%mm0,%%mm3\n"
		       "movq %%mm1,%%mm5\n"
		       "pcmpeqw %%mm6,%%mm2\n"
		       "pcmpeqw %%mm6,%%mm4\n"
		       "pcmpeqw (%2),%%mm3\n"
		       "pcmpeqw (%2),%%mm5\n"
		       "pandn %%mm2,%%mm3\n"
		       "pandn %%mm4,%%mm5\n"
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "pcmpeqw %%mm1,%%mm2\n"
		       "pcmpeqw %%mm0,%%mm4\n"
		       "pandn %%mm3,%%mm2\n"
		       "pandn %%mm5,%%mm4\n"
		       "movq %%mm2,%%mm3\n"
		       "movq %%mm4,%%mm5\n"
		       "pand %%mm6,%%mm2\n"
		       "pand %%mm6,%%mm4\n"
		       "pandn %%mm7,%%mm3\n"
		       "pandn %%mm7,%%mm5\n"
		       "por %%mm3,%%mm2\n"
		       "por %%mm5,%%mm4\n"

		       /* set *dst0 */
		       "movq %%mm2,%%mm3\n"
		       "punpcklwd %%mm4,%%mm2\n"
		       "punpckhwd %%mm4,%%mm3\n"
		       "movq %%mm2,(%3)\n"
		       "movq %%mm3,8(%3)\n"

		       /* next */
		       "addl $8,%0\n"
		       "addl $8,%1\n"
		       "addl $8,%2\n"
		       "addl $16,%3\n"

		       /* central runs */
		       "shrl $2,%4\n"
		       "jz 1f\n"
		       ASM_JUMP_ALIGN
		       "0:\n"

		       /* set the current, current_pre, current_next registers */
		       "movq -8(%1),%%mm0\n"
		       "movq (%1),%%mm7\n"
		       "movq 8(%1),%%mm1\n"
		       "psrlq $48,%%mm0\n"
		       "psllq $48,%%mm1\n"
		       "movq %%mm7,%%mm2\n"
		       "movq %%mm7,%%mm3\n"
		       "psllq $16,%%mm2\n"
		       "psrlq $16,%%mm3\n"
		       "por %%mm2,%%mm0\n"
		       "por %%mm3,%%mm1\n"

		       /* current_upper */
		       "movq (%0),%%mm6\n"

		       /* compute the upper-left pixel for dst0 on %%mm2 */
		       /* compute the upper-right pixel for dst0 on %%mm4 */
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "movq %%mm0,%%mm3\n"
		       "movq %%mm1,%%mm5\n"
		       "pcmpeqw %%mm6,%%mm2\n"
		       "pcmpeqw %%mm6,%%mm4\n"
		       "pcmpeqw (%2),%%mm3\n"
		       "pcmpeqw (%2),%%mm5\n"
		       "pandn %%mm2,%%mm3\n"
		       "pandn %%mm4,%%mm5\n"
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "pcmpeqw %%mm1,%%mm2\n"
		       "pcmpeqw %%mm0,%%mm4\n"
		       "pandn %%mm3,%%mm2\n"
		       "pandn %%mm5,%%mm4\n"
		       "movq %%mm2,%%mm3\n"
		       "movq %%mm4,%%mm5\n"
		       "pand %%mm6,%%mm2\n"
		       "pand %%mm6,%%mm4\n"
		       "pandn %%mm7,%%mm3\n"
		       "pandn %%mm7,%%mm5\n"
		       "por %%mm3,%%mm2\n"
		       "por %%mm5,%%mm4\n"

		       /* set *dst0 */
		       "movq %%mm2,%%mm3\n"
		       "punpcklwd %%mm4,%%mm2\n"
		       "punpckhwd %%mm4,%%mm3\n"
		       "movq %%mm2,(%3)\n"
		       "movq %%mm3,8(%3)\n"

		       /* next */
		       "addl $8,%0\n"
		       "addl $8,%1\n"
		       "addl $8,%2\n"
		       "addl $16,%3\n"

		       "decl %4\n"
		       "jnz 0b\n"
		       "1:\n"

		       /* final run */
		       /* set the current, current_pre, current_next registers */
		       "movq -8(%1),%%mm0\n"
		       "movq (%1),%%mm7\n"
		       "pxor %%mm1,%%mm1\n" /* use a fake black out of screen */
		       "psrlq $48,%%mm0\n"
		       "psllq $48,%%mm1\n"
		       "movq %%mm7,%%mm2\n"
		       "movq %%mm7,%%mm3\n"
		       "psllq $16,%%mm2\n"
		       "psrlq $16,%%mm3\n"
		       "por %%mm2,%%mm0\n"
		       "por %%mm3,%%mm1\n"

		       /* current_upper */
		       "movq (%0),%%mm6\n"

		       /* compute the upper-left pixel for dst0 on %%mm2 */
		       /* compute the upper-right pixel for dst0 on %%mm4 */
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "movq %%mm0,%%mm3\n"
		       "movq %%mm1,%%mm5\n"
		       "pcmpeqw %%mm6,%%mm2\n"
		       "pcmpeqw %%mm6,%%mm4\n"
		       "pcmpeqw (%2),%%mm3\n"
		       "pcmpeqw (%2),%%mm5\n"
		       "pandn %%mm2,%%mm3\n"
		       "pandn %%mm4,%%mm5\n"
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "pcmpeqw %%mm1,%%mm2\n"
		       "pcmpeqw %%mm0,%%mm4\n"
		       "pandn %%mm3,%%mm2\n"
		       "pandn %%mm5,%%mm4\n"
		       "movq %%mm2,%%mm3\n"
		       "movq %%mm4,%%mm5\n"
		       "pand %%mm6,%%mm2\n"
		       "pand %%mm6,%%mm4\n"
		       "pandn %%mm7,%%mm3\n"
		       "pandn %%mm7,%%mm5\n"
		       "por %%mm3,%%mm2\n"
		       "por %%mm5,%%mm4\n"

		       /* set *dst0 */
		       "movq %%mm2,%%mm3\n"
		       "punpcklwd %%mm4,%%mm2\n"
		       "punpckhwd %%mm4,%%mm3\n"
		       "movq %%mm2,(%3)\n"
		       "movq %%mm3,8(%3)\n"
		       "emms\n"

		       : "+r" (src0), "+r" (src1), "+r" (src2), "+r" (dst), "+r" (count)
		       :
		       : "cc"
		       );
#else
  __asm {
    mov eax, src0;
    mov ebx, src1;
    mov ecx, src2;
    mov edx, dst;
    mov esi, count;

    /* first run */
    /* set the current, current_pre, current_next registers */
    pxor mm0,mm0; /* use a fake black out of screen */
    movq mm7, qword ptr [ebx];
    movq mm1, qword ptr [ebx + 8];
    psrlq mm0, 48;
    psllq mm1, 48;
    movq mm2, mm7;
    movq mm3, mm7;
    psllq mm2, 16;
    psrlq mm3, 16;
    por mm0, mm2;
    por mm1, mm3;

    /* current_upper */
    movq mm6, qword ptr [eax];

    /* compute the upper-left pixel for dst0 on %%mm2 */
    /* compute the upper-right pixel for dst0 on %%mm4 */
    movq mm2, mm0;
    movq mm4, mm1;
    movq mm3, mm0;
    movq mm5, mm1;
    pcmpeqw mm2, mm6;
    pcmpeqw mm4, mm6;
    pcmpeqw mm3, qword ptr [ecx];
    pcmpeqw mm5, qword ptr [ecx];
    pandn mm3,mm2;
    pandn mm5,mm4;
    movq mm2,mm0;
    movq mm4,mm1;
    pcmpeqw mm2,mm1;
    pcmpeqw mm4,mm0;
    pandn mm2,mm3;
    pandn mm4,mm5;
    movq mm3,mm2;
    movq mm5,mm4;
    pand mm2,mm6;
    pand mm4,mm6;
    pandn mm3,mm7;
    pandn mm5,mm7;
    por mm2,mm3;
    por mm4,mm5;

    /* set *dst0 */
    movq mm3,mm2;
    punpcklwd mm2,mm4;
    punpckhwd mm3,mm4;
    movq qword ptr [edx], mm2;
    movq qword ptr [edx + 8], mm3;

    /* next */
    add eax, 8;
    add ebx, 8;
    add ecx, 8;
    add edx, 16;

    /* central runs */
    shr esi, 2;
    jz label1;
    align 4;
  label0:

    /* set the current, current_pre, current_next registers */
    movq mm0, qword ptr [ebx-8];
    movq mm7, qword ptr [ebx];
    movq mm1, qword ptr [ebx+8];
    psrlq mm0,48;
    psllq mm1,48;
    movq mm2,mm7;
    movq mm3,mm7;
    psllq mm2,16;
    psrlq mm3,16;
    por mm0,mm2;
    por mm1,mm3;

    /* current_upper */
    movq mm6, qword ptr [eax];

    /* compute the upper-left pixel for dst0 on %%mm2 */
    /* compute the upper-right pixel for dst0 on %%mm4 */
    movq mm2,mm0;
    movq mm4,mm1;
    movq mm3,mm0;
    movq mm5,mm1;
    pcmpeqw mm2,mm6;
    pcmpeqw mm4,mm6;
    pcmpeqw mm3, qword ptr [ecx];
    pcmpeqw mm5, qword ptr [ecx];
    pandn mm3,mm2;
    pandn mm5,mm4;
    movq mm2,mm0;
    movq mm4,mm1;
    pcmpeqw mm2,mm1;
    pcmpeqw mm4,mm0;
    pandn mm2,mm3;
    pandn mm4,mm5;
    movq mm3,mm2;
    movq mm5,mm4;
    pand mm2,mm6;
    pand mm4,mm6;
    pandn mm3,mm7;
    pandn mm5,mm7;
    por mm2,mm3;
    por mm4,mm5;

    /* set *dst0 */
    movq mm3,mm2;
    punpcklwd mm2,mm4;
    punpckhwd mm3,mm4;
    movq qword ptr [edx], mm2;
    movq qword ptr [edx+8], mm3;

    /* next */
    add eax,8;
    add ebx,8;
    add ecx,8;
    add edx,16;

    dec esi;
    jnz label0;
  label1:

    /* final run */
    /* set the current, current_pre, current_next registers */
    movq mm0, qword ptr [ebx-8];
    movq mm7, qword ptr [ebx];
    pxor mm1,mm1; /* use a fake black out of screen */
    psrlq mm0,48;
    psllq mm1,48;
    movq mm2,mm7;
    movq mm3,mm7;
    psllq mm2,16;
    psrlq mm3,16;
    por mm0,mm2;
    por mm1,mm3;

    /* current_upper */
    movq mm6, qword ptr [eax];

    /* compute the upper-left pixel for dst0 on %%mm2 */
    /* compute the upper-right pixel for dst0 on %%mm4 */
    movq mm2,mm0;
    movq mm4,mm1;
    movq mm3,mm0;
    movq mm5,mm1;
    pcmpeqw mm2,mm6;
    pcmpeqw mm4,mm6;
    pcmpeqw mm3, qword ptr [ecx];
    pcmpeqw mm5, qword ptr [ecx];
    pandn mm3,mm2;
    pandn mm5,mm4;
    movq mm2,mm0;
    movq mm4,mm1;
    pcmpeqw mm2,mm1;
    pcmpeqw mm4,mm0;
    pandn mm2,mm3;
    pandn mm4,mm5;
    movq mm3,mm2;
    movq mm5,mm4;
    pand mm2,mm6;
    pand mm4,mm6;
    pandn mm3,mm7;
    pandn mm5,mm7;
    por mm2,mm3;
    por mm4,mm5;

    /* set *dst0 */
    movq mm3,mm2;
    punpcklwd mm2,mm4;
    punpckhwd mm3,mm4;
    movq qword ptr [edx], mm2;
    movq qword ptr [edx+8], mm3;

    mov src0, eax;
    mov src1, ebx;
    mov src2, ecx;
    mov dst, edx;
    mov count, esi;

    emms;
  }
#endif
}

static void internal_scale2x_32_mmx_single(u32* dst, const u32* src0, const u32* src1, const u32* src2, unsigned count) {
  /* always do the first and last run */
  count -= 2*2;

#ifdef __GNUC__
  __asm__ __volatile__(
		       /* first run */
		       /* set the current, current_pre, current_next registers */
		       "pxor %%mm0,%%mm0\n" /* use a fake black out of screen */
		       "movq 0(%1),%%mm7\n"
		       "movq 8(%1),%%mm1\n"
		       "psrlq $32,%%mm0\n"
		       "psllq $32,%%mm1\n"
		       "movq %%mm7,%%mm2\n"
		       "movq %%mm7,%%mm3\n"
		       "psllq $32,%%mm2\n"
		       "psrlq $32,%%mm3\n"
		       "por %%mm2,%%mm0\n"
		       "por %%mm3,%%mm1\n"

		       /* current_upper */
		       "movq (%0),%%mm6\n"

		       /* compute the upper-left pixel for dst0 on %%mm2 */
		       /* compute the upper-right pixel for dst0 on %%mm4 */
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "movq %%mm0,%%mm3\n"
		       "movq %%mm1,%%mm5\n"
		       "pcmpeqd %%mm6,%%mm2\n"
		       "pcmpeqd %%mm6,%%mm4\n"
		       "pcmpeqd (%2),%%mm3\n"
		       "pcmpeqd (%2),%%mm5\n"
		       "pandn %%mm2,%%mm3\n"
		       "pandn %%mm4,%%mm5\n"
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "pcmpeqd %%mm1,%%mm2\n"
		       "pcmpeqd %%mm0,%%mm4\n"
		       "pandn %%mm3,%%mm2\n"
		       "pandn %%mm5,%%mm4\n"
		       "movq %%mm2,%%mm3\n"
		       "movq %%mm4,%%mm5\n"
		       "pand %%mm6,%%mm2\n"
		       "pand %%mm6,%%mm4\n"
		       "pandn %%mm7,%%mm3\n"
		       "pandn %%mm7,%%mm5\n"
		       "por %%mm3,%%mm2\n"
		       "por %%mm5,%%mm4\n"

		       /* set *dst0 */
		       "movq %%mm2,%%mm3\n"
		       "punpckldq %%mm4,%%mm2\n"
		       "punpckhdq %%mm4,%%mm3\n"
		       "movq %%mm2,(%3)\n"
		       "movq %%mm3,8(%3)\n"

		       /* next */
		       "addl $8,%0\n"
		       "addl $8,%1\n"
		       "addl $8,%2\n"
		       "addl $16,%3\n"

		       /* central runs */
		       "shrl $1,%4\n"
		       "jz 1f\n"
		       ASM_JUMP_ALIGN
		       "0:\n"

		       /* set the current, current_pre, current_next registers */
		       "movq -8(%1),%%mm0\n"
		       "movq (%1),%%mm7\n"
		       "movq 8(%1),%%mm1\n"
		       "psrlq $32,%%mm0\n"
		       "psllq $32,%%mm1\n"
		       "movq %%mm7,%%mm2\n"
		       "movq %%mm7,%%mm3\n"
		       "psllq $32,%%mm2\n"
		       "psrlq $32,%%mm3\n"
		       "por %%mm2,%%mm0\n"
		       "por %%mm3,%%mm1\n"

		       /* current_upper */
		       "movq (%0),%%mm6\n"

		       /* compute the upper-left pixel for dst0 on %%mm2 */
		       /* compute the upper-right pixel for dst0 on %%mm4 */
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "movq %%mm0,%%mm3\n"
		       "movq %%mm1,%%mm5\n"
		       "pcmpeqd %%mm6,%%mm2\n"
		       "pcmpeqd %%mm6,%%mm4\n"
		       "pcmpeqd (%2),%%mm3\n"
		       "pcmpeqd (%2),%%mm5\n"
		       "pandn %%mm2,%%mm3\n"
		       "pandn %%mm4,%%mm5\n"
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "pcmpeqd %%mm1,%%mm2\n"
		       "pcmpeqd %%mm0,%%mm4\n"
		       "pandn %%mm3,%%mm2\n"
		       "pandn %%mm5,%%mm4\n"
		       "movq %%mm2,%%mm3\n"
		       "movq %%mm4,%%mm5\n"
		       "pand %%mm6,%%mm2\n"
		       "pand %%mm6,%%mm4\n"
		       "pandn %%mm7,%%mm3\n"
		       "pandn %%mm7,%%mm5\n"
		       "por %%mm3,%%mm2\n"
		       "por %%mm5,%%mm4\n"

		       /* set *dst0 */
		       "movq %%mm2,%%mm3\n"
		       "punpckldq %%mm4,%%mm2\n"
		       "punpckhdq %%mm4,%%mm3\n"
		       "movq %%mm2,(%3)\n"
		       "movq %%mm3,8(%3)\n"

		       /* next */
		       "addl $8,%0\n"
		       "addl $8,%1\n"
		       "addl $8,%2\n"
		       "addl $16,%3\n"

		       "decl %4\n"
		       "jnz 0b\n"
		       "1:\n"

		       /* final run */
		       /* set the current, current_pre, current_next registers */
		       "movq -8(%1),%%mm0\n"
		       "movq (%1),%%mm7\n"
		       "pxor %%mm1,%%mm1\n" /* use a fake black out of screen */
		       "psrlq $32,%%mm0\n"
		       "psllq $32,%%mm1\n"
		       "movq %%mm7,%%mm2\n"
		       "movq %%mm7,%%mm3\n"
		       "psllq $32,%%mm2\n"
		       "psrlq $32,%%mm3\n"
		       "por %%mm2,%%mm0\n"
		       "por %%mm3,%%mm1\n"

		       /* current_upper */
		       "movq (%0),%%mm6\n"

		       /* compute the upper-left pixel for dst0 on %%mm2 */
		       /* compute the upper-right pixel for dst0 on %%mm4 */
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "movq %%mm0,%%mm3\n"
		       "movq %%mm1,%%mm5\n"
		       "pcmpeqd %%mm6,%%mm2\n"
		       "pcmpeqd %%mm6,%%mm4\n"
		       "pcmpeqd (%2),%%mm3\n"
		       "pcmpeqd (%2),%%mm5\n"
		       "pandn %%mm2,%%mm3\n"
		       "pandn %%mm4,%%mm5\n"
		       "movq %%mm0,%%mm2\n"
		       "movq %%mm1,%%mm4\n"
		       "pcmpeqd %%mm1,%%mm2\n"
		       "pcmpeqd %%mm0,%%mm4\n"
		       "pandn %%mm3,%%mm2\n"
		       "pandn %%mm5,%%mm4\n"
		       "movq %%mm2,%%mm3\n"
		       "movq %%mm4,%%mm5\n"
		       "pand %%mm6,%%mm2\n"
		       "pand %%mm6,%%mm4\n"
		       "pandn %%mm7,%%mm3\n"
		       "pandn %%mm7,%%mm5\n"
		       "por %%mm3,%%mm2\n"
		       "por %%mm5,%%mm4\n"

		       /* set *dst0 */
		       "movq %%mm2,%%mm3\n"
		       "punpckldq %%mm4,%%mm2\n"
		       "punpckhdq %%mm4,%%mm3\n"
		       "movq %%mm2,(%3)\n"
		       "movq %%mm3,8(%3)\n"
		       "emms\n"

		       : "+r" (src0), "+r" (src1), "+r" (src2), "+r" (dst), "+r" (count)
		       :
		       : "cc"
		       );
#else
  __asm {
    mov eax, src0;
    mov ebx, src1;
    mov ecx, src2;
    mov edx, dst;
    mov esi, count;

    /* first run */
    /* set the current, current_pre, current_next registers */
    pxor mm0,mm0;
    movq mm7,qword ptr [ebx];
    movq mm1,qword ptr [ebx + 8];
    psrlq mm0,32;
    psllq mm1,32;
    movq mm2,mm7;
    movq mm3,mm7;
    psllq mm2,32;
    psrlq mm3,32;
    por mm0,mm2;
    por mm1,mm3;

    /* current_upper */
    movq mm6,qword ptr [eax];

    /* compute the upper-left pixel for dst0 on %%mm2 */
    /* compute the upper-right pixel for dst0 on %%mm4 */
    movq mm2,mm0;
    movq mm4,mm1;
    movq mm3,mm0;
    movq mm5,mm1;
    pcmpeqd mm2,mm6;
    pcmpeqd mm4,mm6;
    pcmpeqd mm3,qword ptr [ecx];
    pcmpeqd mm5,qword ptr [ecx];
    pandn mm3,mm2;
    pandn mm5,mm4;
    movq mm2,mm0;
    movq mm4,mm1;
    pcmpeqd mm2,mm1;
    pcmpeqd mm4,mm0;
    pandn mm2,mm3;
    pandn mm4,mm5;
    movq mm3,mm2;
    movq mm5,mm4;
    pand mm2,mm6;
    pand mm4,mm6;
    pandn mm3,mm7;
    pandn mm5,mm7;
    por mm2,mm3;
    por mm4,mm5;

    /* set *dst0 */
    movq mm3,mm2;
    punpckldq mm2,mm4;
    punpckhdq mm3,mm4;
    movq qword ptr [edx],mm2;
    movq qword ptr [edx+8],mm3;

    /* next */
    add eax,8;
    add ebx,8;
    add ecx,8;
    add edx,16;

    /* central runs */
    shr esi,1;
    jz label1;
label0:

  /* set the current, current_pre, current_next registers */
    movq mm0,qword ptr [ebx-8];
    movq mm7,qword ptr [ebx];
    movq mm1,qword ptr [ebx+8];
    psrlq mm0,32;
    psllq mm1,32;
    movq mm2,mm7;
    movq mm3,mm7;
    psllq mm2,32;
    psrlq mm3,32;
    por mm0,mm2;
    por mm1,mm3;

    /* current_upper */
    movq mm6,qword ptr[eax];

    /* compute the upper-left pixel for dst0 on %%mm2 */
    /* compute the upper-right pixel for dst0 on %%mm4 */
    movq mm2,mm0;
    movq mm4,mm1;
    movq mm3,mm0;
    movq mm5,mm1;
    pcmpeqd mm2,mm6;
    pcmpeqd mm4,mm6;
    pcmpeqd mm3,qword ptr[ecx];
    pcmpeqd mm5,qword ptr[ecx];
    pandn mm3,mm2;
    pandn mm5,mm4;
    movq mm2,mm0;
    movq mm4,mm1;
    pcmpeqd mm2,mm1;
    pcmpeqd mm4,mm0;
    pandn mm2,mm3;
    pandn mm4,mm5;
    movq mm3,mm2;
    movq mm5,mm4;
    pand mm2,mm6;
    pand mm4,mm6;
    pandn mm3,mm7;
    pandn mm5,mm7;
    por mm2,mm3;
    por mm4,mm5;

    /* set *dst0 */
    movq mm3,mm2;
    punpckldq mm2,mm4;
    punpckhdq mm3,mm4;
    movq qword ptr [edx],mm2;
    movq qword ptr [edx+8],mm3;

    /* next */
    add eax,8;
    add ebx,8;
    add ecx,8;
    add edx,16;

    dec esi;
    jnz label0;
label1:

    /* final run */
    /* set the current, current_pre, current_next registers */
    movq mm0,qword ptr [ebx-8];
    movq mm7,qword ptr [ebx];
    pxor mm1,mm1;
    psrlq mm0,32;
    psllq mm1,32;
    movq mm2,mm7;
    movq mm3,mm7;
    psllq mm2,32;
    psrlq mm3,32;
    por mm0,mm2;
    por mm1,mm3;

    /* current_upper */
    movq mm6,qword ptr [eax];

    /* compute the upper-left pixel for dst0 on %%mm2 */
    /* compute the upper-right pixel for dst0 on %%mm4 */
    movq mm2,mm0;
    movq mm4,mm1;
    movq mm3,mm0;
    movq mm5,mm1;
    pcmpeqd mm2,mm6;
    pcmpeqd mm4,mm6;
    pcmpeqd mm3,qword ptr [ecx];
    pcmpeqd mm5,qword ptr [ecx];
    pandn mm3,mm2;
    pandn mm5,mm4;
    movq mm2,mm0;
    movq mm4,mm1;
    pcmpeqd mm2,mm1;
    pcmpeqd mm4,mm0;
    pandn mm2,mm3;
    pandn mm4,mm5;
    movq mm3,mm2;
    movq mm5,mm4;
    pand mm2,mm6;
    pand mm4,mm6;
    pandn mm3,mm7;
    pandn mm5,mm7;
    por mm2,mm3;
    por mm4,mm5;

    /* set *dst0 */
    movq mm3,mm2;
    punpckldq mm2,mm4;
    punpckhdq mm3,mm4;
    movq qword ptr [edx],mm2;
    movq qword ptr [edx+8],mm3;

    mov src0, eax;
    mov src1, ebx;
    mov src2, ecx;
    mov dst, edx;
    mov count, esi;

    emms;
  }
#endif
}

static void internal_scale2x_16_mmx(u16* dst0, u16* dst1, const u16* src0, const u16* src1, const u16* src2, unsigned count) {
  //	assert( count >= 2*4 );
  internal_scale2x_16_mmx_single(dst0, src0, src1, src2, count);
  internal_scale2x_16_mmx_single(dst1, src2, src1, src0, count);
}

static void internal_scale2x_32_mmx(u32* dst0, u32* dst1, const u32* src0, const u32* src1, const u32* src2, unsigned count) {
  //	assert( count >= 2*2 );
  internal_scale2x_32_mmx_single(dst0, src0, src1, src2, count);
  internal_scale2x_32_mmx_single(dst1, src2, src1, src0, count);
}
#endif

void AdMame2x(u8 *srcPtr, u32 srcPitch, /* u8 deltaPtr, */
	      u8 *dstPtr, u32 dstPitch, int width, int height)
{
  u16 *dst0 = (u16 *)dstPtr;
  u16 *dst1 = dst0 + (dstPitch/2);

  u16 *src0 = (u16 *)srcPtr;
  u16 *src1 = src0 + (srcPitch/2);
  u16 *src2 = src1 + (srcPitch/2);
#ifdef MMX
  if(cpu_mmx) {
    internal_scale2x_16_mmx(dst0, dst1, src0, src0, src1, width);

    {
    int count = height;

    count -= 2;
    while(count) {
      dst0 += dstPitch;
      dst1 += dstPitch;
      internal_scale2x_16_mmx(dst0, dst1, src0, src1, src2, width);
      src0 = src1;
      src1 = src2;
      src2 += srcPitch/2;
      --count;
    }
    }
    dst0 += dstPitch;
    dst1 += dstPitch;
    internal_scale2x_16_mmx(dst0, dst1, src0, src1, src1, width);
  } else {
#endif
    internal_scale2x_16_def(dst0, dst1, src0, src0, src1, width);

    {
    int count = height;

    count -= 2;
    while(count) {
      dst0 += dstPitch;
      dst1 += dstPitch;
      internal_scale2x_16_def(dst0, dst1, src0, src1, src2, width);
      src0 = src1;
      src1 = src2;
      src2 += srcPitch/2;
      --count;
    }
    }
    dst0 += dstPitch;
    dst1 += dstPitch;
    internal_scale2x_16_def(dst0, dst1, src0, src1, src1, width);
#ifdef MMX
  }
#endif
}

void AdMame2x32(u8 *srcPtr, u32 srcPitch, /* u8 deltaPtr, */
		u8 *dstPtr, u32 dstPitch, int width, int height)
{
  u32 *dst0 = (u32 *)dstPtr;
  u32 *dst1 = dst0 + (dstPitch/4);

  u32 *src0 = (u32 *)srcPtr;
  u32 *src1 = src0 + (srcPitch/4);
  u32 *src2 = src1 + (srcPitch/4);
#ifdef MMX
  if(cpu_mmx) {
    internal_scale2x_32_mmx(dst0, dst1, src0, src0, src1, width);

    {
    int count = height;

    count -= 2;
    while(count) {
      dst0 += dstPitch/2;
      dst1 += dstPitch/2;
      internal_scale2x_32_mmx(dst0, dst1, src0, src1, src2, width);
      src0 = src1;
      src1 = src2;
      src2 += srcPitch/4;
      --count;
    }
    }
    dst0 += dstPitch/2;
    dst1 += dstPitch/2;
    internal_scale2x_32_mmx(dst0, dst1, src0, src1, src1, width);
  } else {
#endif
    internal_scale2x_32_def(dst0, dst1, src0, src0, src1, width);
    {
    int count = height;

    count -= 2;
    while(count) {
      dst0 += dstPitch/2;
      dst1 += dstPitch/2;
      internal_scale2x_32_def(dst0, dst1, src0, src1, src2, width);
      src0 = src1;
      src1 = src2;
      src2 += srcPitch/4;
      --count;
    }
    }
    dst0 += dstPitch/2;
    dst1 += dstPitch/2;
    internal_scale2x_32_def(dst0, dst1, src0, src1, src1, width);
#ifdef MMX
  }
#endif
}



unsigned int LUT16to32[65536];
unsigned int RGBtoYUV[65536];

void hq_init(void)
{
    int i, j, k, r, g, b, Y, u, v;

    for (i=0; i<65536; i++)
	LUT16to32[i] = ((i & 0xF800) << 8) + ((i & 0x07E0) << 5) + ((i & 0x001F) << 3);

    for (i=0; i<32; i++) {
	for (j=0; j<64; j++) {
	    for (k=0; k<32; k++) {
		r = i << 3;
		g = j << 2;
		b = k << 3;
		Y = (r + g + b) >> 2;
		u = 128 + ((r - b) >> 2);
		v = 128 + ((-r + 2*g -b)>>3);
		RGBtoYUV[ (i << 11) + (j << 5) + k ] = (Y<<16) + (u<<8) + v;
	    }
	}
    }
}

