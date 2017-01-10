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

#ifndef FPP_H
#define FPP_H

#define __USE_ISOC9X  /* We might be able to pick up a NaN */

#include <math.h>
#include <float.h>

#include <softfloat.h>


#define	FPCR_ROUNDING_MODE	0x00000030
#define	FPCR_ROUND_NEAR		0x00000000
#define	FPCR_ROUND_ZERO		0x00000010
#define	FPCR_ROUND_MINF		0x00000020
#define	FPCR_ROUND_PINF		0x00000030

#define	FPCR_ROUNDING_PRECISION	0x000000c0
#define	FPCR_PRECISION_SINGLE	0x00000040
#define	FPCR_PRECISION_DOUBLE	0x00000080
#define FPCR_PRECISION_EXTENDED	0x00000000

extern uae_u32 fpp_get_fpsr (void);
extern void to_single(fptype *fp, uae_u32 wrd1);
extern void to_double(fptype *fp, uae_u32 wrd1, uae_u32 wrd2);
extern void to_exten(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3);
extern void normalize_exten (uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3);


/* Functions for setting host/library modes and getting status */
STATIC_INLINE void set_fp_mode(uae_u32 mode_control)
{
    switch(mode_control & FPCR_ROUNDING_PRECISION) {
        case FPCR_PRECISION_SINGLE:   // S
            //floatx80_rounding_precision = 32;
            break;
        case FPCR_PRECISION_DOUBLE:   // D
            //floatx80_rounding_precision = 64;
            break;
        case FPCR_PRECISION_EXTENDED: // X
        default:                      // undefined
            //floatx80_rounding_precision = 80;
            break;
    }
    floatx80_rounding_precision = 80;
    
    switch(mode_control & FPCR_ROUNDING_MODE) {
        case FPCR_ROUND_NEAR: // to neareset
            float_rounding_mode = float_round_nearest_even;
            break;
        case FPCR_ROUND_ZERO: // to zero
            float_rounding_mode = float_round_to_zero;
            break;
        case FPCR_ROUND_MINF: // to minus
            float_rounding_mode = float_round_down;
            break;
        case FPCR_ROUND_PINF: // to plus
            float_rounding_mode = float_round_up;
            break;
    }
    return;
}
STATIC_INLINE void get_fp_status(uae_u32 *status)
{
    if (float_exception_flags & float_flag_invalid)
        *status |= 0x2000;
    if (float_exception_flags & float_flag_divbyzero)
        *status |= 0x0400;
    if (float_exception_flags & float_flag_overflow)
        *status |= 0x1000;
    if (float_exception_flags & float_flag_underflow)
        *status |= 0x0800;
    if (float_exception_flags & float_flag_inexact)
        *status |= 0x0200;
}
STATIC_INLINE void clear_fp_status(void)
{
    float_exception_flags = 0;
}

/* Helper functions */
STATIC_INLINE const char *fp_print(fptype *fx)
{
    static char fs[32];
    bool n, u, d;
    long double result = 0.0;
    int i;
    
    n = floatx80_is_negative(*fx);
    u = floatx80_is_unnormal(*fx);
    d = floatx80_is_denormal(*fx);
    
    if (floatx80_is_zero(*fx)) {
        sprintf(fs, "%c%#.17Le%s%s", n?'-':'+', (long double) 0.0, u?"U":"", d?"D":"");
    } else if (floatx80_is_infinity(*fx)) {
        sprintf(fs, "%c%s", n?'-':'+', "inf");
    } else if (floatx80_is_signaling_nan(*fx)) {
        sprintf(fs, "%c%s", n?'-':'+', "snan");
    } else if (floatx80_is_nan(*fx)) {
        sprintf(fs, "%c%s", n?'-':'+', "nan");
    } else {
        for (i = 63; i >= 0; i--) {
            if (fx->low & (((uae_u64)1)<<i)) {
                result += (long double) 1.0 / (((uae_u64)1)<<(63-i));
            }
        }
        result *= powl(2.0, (fx->high&0x7FFF) - 0x3FFF);
        sprintf(fs, "%c%#.17Le%s%s", n?'-':'+', result, u?"U":"", d?"D":"");
    }
    
    return fs;
}

STATIC_INLINE void softfloat_set(fptype *fx, uae_u32 *f)
{
    fx->high = (uae_u16)(f[0] >> 16);
    fx->low = ((uae_u64)f[1] << 32) | f[2];
}

STATIC_INLINE void softfloat_get(fptype *fx, uae_u32 *f)
{
    f[0] = (uae_u32)(fx->high << 16);
    f[1] = fx->low >> 32;
    f[2] = (uae_u32)fx->low;
}

/* Functions for detecting float type */
STATIC_INLINE bool fp_is_snan(fptype *fp)
{
    return floatx80_is_signaling_nan(*fp) != 0;
}
STATIC_INLINE void fp_unset_snan(fptype *fp)
{
    fp->low |= LIT64(0x4000000000000000);
}
STATIC_INLINE bool fp_is_nan (fptype *fp)
{
    return floatx80_is_nan(*fp) != 0;
}
STATIC_INLINE bool fp_is_infinity (fptype *fp)
{
    return floatx80_is_infinity(*fp) != 0;
}
STATIC_INLINE bool fp_is_zero(fptype *fp)
{
    return floatx80_is_zero(*fp) != 0;
}
STATIC_INLINE bool fp_is_neg(fptype *fp)
{
    return floatx80_is_negative(*fp) != 0;
}
STATIC_INLINE bool fp_is_denormal(fptype *fp)
{
    return floatx80_is_denormal(*fp) != 0;
}
STATIC_INLINE bool fp_is_unnormal(fptype *fp)
{
    return floatx80_is_unnormal(*fp) != 0;
}

/* Functions for converting between float formats */
static const long double twoto32 = 4294967296.0;

STATIC_INLINE void to_native(long double *fp, fptype fpx)
{
    int expon;
    long double frac;
    
    expon = fpx.high & 0x7fff;
    
    if (floatx80_is_zero(fpx)) {
        *fp = floatx80_is_negative(fpx) ? -0.0 : +0.0;
        return;
    }
    if (floatx80_is_nan(fpx)) {
        *fp = sqrtl(-1);
        return;
    }
    if (floatx80_is_infinity(fpx)) {
        *fp = floatx80_is_negative(fpx) ? logl(0.0) : (1.0/0.0);
        return;
    }
    
    frac = (long double)fpx.low / (long double)(twoto32 * 2147483648.0);
    if (floatx80_is_negative(fpx))
        frac = -frac;
    *fp = ldexpl (frac, expon - 16383);
}

STATIC_INLINE void from_native(long double fp, fptype *fpx)
{
    int expon;
    long double frac;
    
    if (signbit(fp))
        fpx->high = 0x8000;
    else
        fpx->high = 0x0000;
    
    if (isnan(fp)) {
        fpx->high |= 0x7fff;
        fpx->low = LIT64(0xffffffffffffffff);
        return;
    }
    if (isinf(fp)) {
        fpx->high |= 0x7fff;
        fpx->low = LIT64(0x0000000000000000);
        return;
    }
    if (fp == 0.0) {
        fpx->low = LIT64(0x0000000000000000);
        return;
    }
    if (fp < 0.0)
        fp = -fp;
    
    frac = frexpl (fp, &expon);
    frac += 0.5 / (twoto32 * twoto32);
    if (frac >= 1.0) {
        frac /= 2.0;
        expon++;
    }
    fpx->high |= (expon + 16383 - 1) & 0x7fff;
    fpx->low = (bits64)(frac * (long double)(twoto32 * twoto32));
    
    while (!(fpx->low & LIT64( 0x8000000000000000))) {
        if (fpx->high == 0) {
            float_raise( float_flag_denormal );
            break;
        }
        fpx->low <<= 1;
        fpx->high--;
    }
}

STATIC_INLINE void to_single_xn(fptype *fp, uae_u32 wrd1)
{
    float32 f = wrd1;
    *fp = float32_to_floatx80(f); // automatically fix denormals
}
STATIC_INLINE void to_single_x(fptype *fp, uae_u32 wrd1)
{
    float32 f = wrd1;
    *fp = float32_to_floatx80_allowunnormal(f);
}
STATIC_INLINE uae_u32 from_single_x(fptype *fp)
{
    float32 f = floatx80_to_float32(*fp);
    return f;
}

STATIC_INLINE void to_double_xn(fptype *fp, uae_u32 wrd1, uae_u32 wrd2)
{
    float64 f = ((float64)wrd1 << 32) | wrd2;
    *fp = float64_to_floatx80(f); // automatically fix denormals
}
STATIC_INLINE void to_double_x(fptype *fp, uae_u32 wrd1, uae_u32 wrd2)
{
    float64 f = ((float64)wrd1 << 32) | wrd2;
    *fp = float64_to_floatx80_allowunnormal(f);
}
STATIC_INLINE void from_double_x(fptype *fp, uae_u32 *wrd1, uae_u32 *wrd2)
{
    float64 f = floatx80_to_float64(*fp);
    *wrd1 = f >> 32;
    *wrd2 = (uae_u32)f;
}

STATIC_INLINE void to_exten_x(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    uae_u32 wrd[3] = { wrd1, wrd2, wrd3 };
    softfloat_set(fp, wrd);
}
STATIC_INLINE void from_exten_x(fptype *fp, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
    uae_u32 wrd[3];
    softfloat_get(fp, wrd);
    *wrd1 = wrd[0];
    *wrd2 = wrd[1];
    *wrd3 = wrd[2];
}

STATIC_INLINE uae_s64 to_int(fptype *src, int size)
{
    static fptype fxsizes[6];
    static bool setup = false;
    if (!setup) {
        fxsizes[0] = int32_to_floatx80(-128);
        fxsizes[1] = int32_to_floatx80(127);
        fxsizes[2] = int32_to_floatx80(-32768);
        fxsizes[3] = int32_to_floatx80(32767);
        fxsizes[4] = int32_to_floatx80(-2147483648);
        fxsizes[5] = int32_to_floatx80(2147483647);
        setup = true;
    }
    
    if (floatx80_lt(*src, fxsizes[size * 2 + 0])) {
        return floatx80_to_int32(fxsizes[size * 2 + 0]);
    }
    if (floatx80_le(fxsizes[size * 2 + 1], *src)) {
        return floatx80_to_int32(fxsizes[size * 2 + 1]);
    }
    return floatx80_to_int32(*src);
}
STATIC_INLINE fptype from_int(uae_s32 src)
{
    return int32_to_floatx80(src);
}

/* Functions for rounding */

// round to float with extended precision exponent
STATIC_INLINE void fp_roundsgl(fptype *fp)
{
    *fp = floatx80_round32(*fp);
}

// round to float
STATIC_INLINE void fp_round32(fptype *fp)
{
    float32 f = floatx80_to_float32(*fp);
    *fp = float32_to_floatx80(f);
}

// round to double
STATIC_INLINE void fp_round64(fptype *fp)
{
    float64 f = floatx80_to_float64(*fp);
    *fp = float64_to_floatx80(f);
}

/* Arithmetic functions */

STATIC_INLINE fptype fp_int(fptype a)
{
    return floatx80_round_to_int(a);
}
STATIC_INLINE fptype fp_intrz(fptype a)
{
    int8 save_rm;
    fptype b;
    save_rm = float_rounding_mode;
    float_rounding_mode = float_round_to_zero;
    b = floatx80_round_to_int(a);
    float_rounding_mode = save_rm;
    return b;
}
STATIC_INLINE fptype fp_sqrt(fptype a)
{
    return floatx80_sqrt(a);
}
STATIC_INLINE fptype fp_lognp1(fptype a)
{
    return floatx80_flognp1(a);
}
STATIC_INLINE fptype fp_sin(fptype a)
{
    floatx80_fsin(&a);
    return a;
}
STATIC_INLINE fptype fp_tan(fptype a)
{
    floatx80_ftan(&a);
    return a;
}
STATIC_INLINE fptype fp_logn(fptype a)
{
    return floatx80_flogn(a);
}
STATIC_INLINE fptype fp_log10(fptype a)
{
    return floatx80_flog10(a);
}
STATIC_INLINE fptype fp_log2(fptype a)
{
    return floatx80_flog2(a);
}
STATIC_INLINE fptype fp_abs(fptype a)
{
    return floatx80_abs(a);
}
STATIC_INLINE fptype fp_neg(fptype a)
{
    return floatx80_chs(a);
}
STATIC_INLINE fptype fp_cos(fptype a)
{
    floatx80_fcos(&a);
    return a;
}
STATIC_INLINE fptype fp_getexp(fptype a)
{
    return floatx80_getexp(a);
}
STATIC_INLINE fptype fp_getman(fptype a)
{
    return floatx80_getman(a);
}
STATIC_INLINE fptype fp_div(fptype a, fptype b)
{
    return floatx80_div(a, b);
}
STATIC_INLINE fptype fp_add(fptype a, fptype b)
{
    return floatx80_add(a, b);
}
STATIC_INLINE fptype fp_mul(fptype a, fptype b)
{
    return floatx80_mul(a, b);
}
STATIC_INLINE fptype fp_rem(fptype a, fptype b)
{
    return floatx80_rem(a, b);
}
STATIC_INLINE fptype fp_scale(fptype a, fptype b)
{
    return floatx80_scale(a, b);
}
STATIC_INLINE fptype fp_sub(fptype a, fptype b)
{
    return floatx80_sub(a, b);
}

/* FIXME: create softfloat functions for following arithmetics */
#define fp_round_to_minus_infinity(x) floor(x)
#define fp_round_to_plus_infinity(x) ceil(x)
#define fp_round_to_zero(x)	((x) >= 0.0 ? floor(x) : ceil(x))
#define fp_round_to_nearest(x) ((x) >= 0.0 ? (int)((x) + 0.5) : (int)((x) - 0.5))

STATIC_INLINE fptype fp_sinh(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = sinhl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_etoxm1(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = expl(fpa) - 1.0;
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_tanh(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = tanhl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_atan(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = atanl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_asin(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = asinl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_atanh(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = atanhl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_etox(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = expl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_twotox(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = powl(2.0, fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_tentox(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = powl(10.0, fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_cosh(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = coshl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_acos(fptype a)
{
    long double fpa;
    to_native(&fpa, a);
    fpa = acosl(fpa);
    from_native(fpa, &a);
    return a;
}
STATIC_INLINE fptype fp_mod(fptype a, fptype b)
{
    long double fpa, fpb;
    long double quot;
    to_native(&fpa, a);
    to_native(&fpb, a);
    quot = fp_round_to_zero(fpa / fpb);
    fpa -= quot * fpb;
    from_native(fpa, &a);
    return a;
}

#endif
