 /*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68881 emulation
  *
  * Conversion routines for hosts knowing floating point format.
  *
  * Copyright 1996 Herman ten Brugge
  * Modified 2005 Peter Keunecke
  */

#include <math.h>

#define	FPCR_ROUNDING_MODE	0x00000030
#define	FPCR_ROUND_NEAR		0x00000000
#define	FPCR_ROUND_ZERO		0x00000010
#define	FPCR_ROUND_MINF		0x00000020
#define	FPCR_ROUND_PINF		0x00000030

#define	FPCR_ROUNDING_PRECISION	0x000000c0
#define	FPCR_PRECISION_SINGLE	0x00000040
#define	FPCR_PRECISION_DOUBLE	0x00000080
#define FPCR_PRECISION_EXTENDED	0x00000000

extern void to_single(fpdata *fpd, uae_u32 value);
extern void to_double(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2);
extern void to_exten(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3);
extern const TCHAR *fp_print(fpdata *fpd);

#if 0
STATIC_INLINE void exten_zeronormalize(uae_u32 *pwrd1, uae_u32 *pwrd2, uae_u32 *pwrd3)
{
	uae_u32 wrd1 = *pwrd1;
	uae_u32 wrd2 = *pwrd2;
	uae_u32 wrd3 = *pwrd3;
	int exp = (wrd1 >> 16) & 0x7fff;
	// Force zero if mantissa is zero but exponent is non-zero
	// M68k FPU automatically convert them to plain zeros.
	// x86 FPU considers them invalid values
	if (exp != 0 && exp != 0x7fff && !wrd2 && !wrd3) {
		*pwrd1 = (wrd1 & 0x80000000);
	}
}

#if USE_LONG_DOUBLE
STATIC_INLINE void to_exten_x(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	// force correct long double alignment
	union
	{
		long double lf;
		uae_u32 longarray[3];
	} uld;
	exten_zeronormalize(&wrd1, &wrd2, &wrd3);
	// little endian order
	uld.longarray[0] = wrd3;
	uld.longarray[1] = wrd2;
	uld.longarray[2] = wrd1 >> 16;
	long double *longdoublewords = (long double *)uld.longarray;
	*fp = *longdoublewords;
}
#define HAVE_to_exten

STATIC_INLINE void from_exten_x(fptype fp, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
	uae_u32 *longarray = (uae_u32 *)&fp;
	uae_u16 *finalword = (uae_u16 *)(((uae_u8*)&fp) + 8);

	*wrd1 = finalword[0] << 16;
	*wrd2 = longarray[1];
	*wrd3 = longarray[0]; // little endian
}
#define HAVE_from_exten
#endif /* USE_LONG_DOUBLE */

#if defined(X86_MSVC_ASSEMBLY_FPU)
#ifndef HAVE_to_single
#define HAVE_to_single
STATIC_INLINE double to_single_x (uae_u32 longvalue)
{
	double floatfake;

	__asm {
		fld dword ptr longvalue;
		fstp qword ptr floatfake;
	}
	return floatfake;
}
#endif

#ifndef HAVE_from_single
#define HAVE_from_single
STATIC_INLINE uae_u32 from_single_x (double floatfake)
{
	uae_u32 longvalue;

	__asm {
		fld qword ptr floatfake;
		fstp dword ptr longvalue;
	}
	return longvalue;
}
#endif

#ifndef HAVE_to_exten
#define HAVE_to_exten
STATIC_INLINE void to_exten_x(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	uae_u32 longarray[3];
	double  extenfake;

	exten_normalize(&wrd1, &wrd2, &wrd3);
	longarray[0] = wrd3; // littlen endian
	longarray[1] = wrd2;
	longarray[2] = wrd2 >> 16;

	__asm {
		fld tbyte ptr longarray;
		fstp qword ptr extenfake;
	}
	*fp = extenfake;
}
#endif

#ifndef HAVE_from_exten
#define HAVE_from_exten
STATIC_INLINE void from_exten_x(fptype fp, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
	fptype src = fp;
	uae_u32 longarray[3], *srcarray = (uae_u32 *)&src;
	__asm {
		fld qword ptr src;
		fstp tbyte ptr longarray;
	}
	*wrd1 = (longarray[2] & 0xffff) <<16;
	*wrd2 =  longarray[1];
	*wrd3 =  longarray[0]; // little endian
	if (!srcarray[0] && (srcarray[1] == 0x7ff00000 || srcarray[1] == 0xfff00000))
		*wrd2 = 0; // The MSB of the mantissa was set wrongly for infinity, causing a NaN
}
#endif
#endif /* X86_MSVC_ASSEMBLY */

#ifndef HAVE_to_single
#define HAVE_to_single
STATIC_INLINE double to_single_x (uae_u32 value)
{
	union {
		float f;
		uae_u32 u;
	} val;

	val.u = value;
	return val.f;
}
#endif

#ifndef HAVE_from_single
#define HAVE_from_single
STATIC_INLINE uae_u32 from_single_x (double src)
{
	union {
		float f;
		uae_u32 u;
	} val;

	val.f = (float) src;
	return val.u;
}
#endif

#ifndef HAVE_to_double
#define HAVE_to_double
STATIC_INLINE double to_double_x(uae_u32 wrd1, uae_u32 wrd2)
{
	union {
		double d;
		uae_u32 u[2];
	} val;

#ifdef WORDS_BIGENDIAN
	val.u[0] = wrd1;
	val.u[1] = wrd2;
#else
	val.u[1] = wrd1;
	val.u[0] = wrd2;
#endif
	return val.d;
}
#endif

#ifndef HAVE_from_double
#define HAVE_from_double
STATIC_INLINE void from_double_x(double src, uae_u32 * wrd1, uae_u32 * wrd2)
{
	uae_u32 *longarray = (uae_u32 *)&src;

	*wrd1 = longarray[1]; // little endian
	*wrd2 = longarray[0];
}
#endif

#ifndef HAVE_to_exten
#define HAVE_to_exten
STATIC_INLINE void to_exten_x(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	double frac;
	exten_zeronormalize(&wrd1, &wrd2, &wrd3);
	if ((wrd1 & 0x7fff0000) == 0 && wrd2 == 0 && wrd3 == 0) {
		*fp = (wrd1 & 0x80000000) ? -0.0 : +0.0;
		return;
	}
	frac = ((double)wrd2 + ((double)wrd3 / twoto32)) / 2147483648.0;
	if (wrd1 & 0x80000000)
		frac = -frac;
	*fp = ldexp (frac, ((wrd1 >> 16) & 0x7fff) - 16383);
}
#endif

#ifndef HAVE_from_exten
#define HAVE_from_exten
STATIC_INLINE void from_exten_x(fptype fp, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
	int expon;
	double frac;
	fptype v;

	v = fp;
	if (v == 0.0) {
		*wrd1 = signbit(v) ? 0x80000000 : 0;
		*wrd2 = 0;
		*wrd3 = 0;
		return;
	}
	if (v < 0) {
		*wrd1 = 0x80000000;
		v = -v;
	} else {
		*wrd1 = 0;
	}
	frac = frexp (v, &expon);
	frac += 0.5 / (twoto32 * twoto32);
	if (frac >= 1.0) {
		frac /= 2.0;
		expon++;
	}
	*wrd1 |= (((expon + 16383 - 1) & 0x7fff) << 16);
	*wrd2 = (uae_u32) (frac * twoto32);
	*wrd3 = (uae_u32) ((frac * twoto32 - *wrd2) * twoto32);
}
#endif
#endif
