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

#define	FPCR_ROUNDING_MODE	0x00000030
#define	FPCR_ROUND_NEAR		0x00000000
#define	FPCR_ROUND_ZERO		0x00000010
#define	FPCR_ROUND_MINF		0x00000020
#define	FPCR_ROUND_PINF		0x00000030

#define	FPCR_ROUNDING_PRECISION	0x000000c0
#define	FPCR_PRECISION_SINGLE	0x00000040
#define	FPCR_PRECISION_DOUBLE	0x00000080
#define FPCR_PRECISION_EXTENDED	0x00000000

#if USE_LONG_DOUBLE
STATIC_INLINE long double to_exten(uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    uae_u32 longarray[] = {wrd3,wrd2,((wrd1>>16)&0xffff)}; // little endian
    register long double *longdoublewords = (long double *)longarray;

    return(*longdoublewords);
}
#define HAVE_to_exten

STATIC_INLINE void from_exten(long double src, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
    register uae_u32 *longarray = (uae_u32 *)&src;
    register uae_u16 *finalword = (uae_u16 *)(&src + 8);

    *wrd1 = ((uae_u32)*finalword)<<16;
    *wrd2 = longarray[1];
    *wrd3 = longarray[0]; // little endian
}
#define HAVE_from_exten
#endif /* USE_LONG_DOUBLE */

#if defined(X86_MSVC_ASSEMBLY)
#ifndef HAVE_to_single
#define HAVE_to_single
STATIC_INLINE double to_single (uae_u32 longvalue)
{
    double floatfake;

    __asm {
    fld     dword ptr   longvalue;
    fstp    qword ptr   floatfake;
    }
    return(floatfake);
}
#endif

#ifndef HAVE_from_single
#define HAVE_from_single
STATIC_INLINE uae_u32 from_single (double floatfake)
{
    uae_u32 longvalue;

    __asm {
    fld     qword ptr   floatfake;
    fstp    dword ptr   longvalue;
    }
    return(longvalue);
}
#endif

#ifndef HAVE_to_exten
#define HAVE_to_exten
STATIC_INLINE double to_exten(uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    uae_u32 longarray[] = {wrd3,wrd2,((wrd1>>16)&0xffff)}; // little endian
    double  extenfake;

    __asm {
    fld     tbyte ptr   longarray;
    fstp    qword ptr   extenfake;
    }
    return(extenfake);
}
#endif

#ifndef HAVE_from_exten
#define HAVE_from_exten
STATIC_INLINE void from_exten(double src, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
    uae_u32 longarray[3], *srcarray = (uae_u32 *)&src;

    __asm {
    fld     qword ptr   src;
    fstp    tbyte ptr   longarray;
    }
    *wrd1 = (longarray[2] & 0xffff) <<16;
    *wrd2 =  longarray[1];
    *wrd3 =  longarray[0]; // little endian
    if (!srcarray[0] && (srcarray[1]==0x7ff00000 || srcarray[1]==0xfff00000))
	*wrd2 = 0; // The MSB of the mantissa was set wrongly for infinity, causing a NaN
}
#endif
#endif /* X86_MSVC_ASSEMBLY */

#ifndef HAVE_to_single
#define HAVE_to_single
STATIC_INLINE double to_single (uae_u32 value)
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
STATIC_INLINE uae_u32 from_single (double src)
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
STATIC_INLINE double to_double(uae_u32 wrd1, uae_u32 wrd2)
{
    union {
    double d;
    uae_u32 u[2];
    } val;

    val.u[0] = wrd2; // little endian
    val.u[1] = wrd1;
    return val.d;
}
#endif

#ifndef HAVE_from_double
#define HAVE_from_double
STATIC_INLINE void from_double(double src, uae_u32 * wrd1, uae_u32 * wrd2)
{
    register uae_u32 *longarray = (uae_u32 *)&src;

    *wrd1 = longarray[1]; // little endian
    *wrd2 = longarray[0];
}
#endif

static double twoto32 = 4294967296.0;
#ifndef HAVE_to_exten
#define HAVE_to_exten
STATIC_INLINE double to_exten(uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    double frac;

    if ((wrd1 & 0x7fff0000) == 0 && wrd2 == 0 && wrd3 == 0)
	return 0.0;
    frac = ((double)wrd2 + ((double)wrd3 / twoto32)) / 2147483648.0;
    if (wrd1 & 0x80000000)
	frac = -frac;
    return ldexp (frac, ((wrd1 >> 16) & 0x7fff) - 16383);
}
#endif

#ifndef HAVE_from_exten
#define HAVE_from_exten
STATIC_INLINE void from_exten(double src, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
    int expon;
    double frac;

    if (src == 0.0) {
	*wrd1 = 0;
	*wrd2 = 0;
	*wrd3 = 0;
	return;
    }
    if (src < 0) {
	*wrd1 = 0x80000000;
	src = -src;
    } else {
	*wrd1 = 0;
    }
    frac = frexp (src, &expon);
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
