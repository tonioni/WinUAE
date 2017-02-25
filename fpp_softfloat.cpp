/*
* UAE - The Un*x Amiga Emulator
*
* MC68881/68882/68040/68060 FPU emulation
* Softfloat version
*
* Andreas Grabher and Toni Wilen
*
*/
#define __USE_ISOC9X  /* We might be able to pick up a NaN */

#define SOFTFLOAT_FAST_INT64

#include <math.h>
#include <float.h>
#include <fenv.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "fpp.h"
#include "newcpu.h"

#include "softfloat/softfloat-macros.h"
#include "softfloat/softfloat-specialize.h"

#define	FPCR_ROUNDING_MODE	0x00000030
#define	FPCR_ROUND_NEAR		0x00000000
#define	FPCR_ROUND_ZERO		0x00000010
#define	FPCR_ROUND_MINF		0x00000020
#define	FPCR_ROUND_PINF		0x00000030

#define	FPCR_ROUNDING_PRECISION	0x000000c0
#define	FPCR_PRECISION_SINGLE	0x00000040
#define	FPCR_PRECISION_DOUBLE	0x00000080
#define FPCR_PRECISION_EXTENDED	0x00000000

static struct float_status fs;

/* Functions for setting host/library modes and getting status */
static void fp_set_mode(uae_u32 mode_control)
{
	set_floatx80_rounding_precision(80, &fs);
	set_float_detect_tininess(float_tininess_before_rounding, &fs);
    switch(mode_control & FPCR_ROUNDING_MODE) {
        case FPCR_ROUND_NEAR: // to neareset
            set_float_rounding_mode(float_round_nearest_even, &fs);
            break;
        case FPCR_ROUND_ZERO: // to zero
            set_float_rounding_mode(float_round_to_zero, &fs);
            break;
        case FPCR_ROUND_MINF: // to minus
            set_float_rounding_mode(float_round_down, &fs);
            break;
        case FPCR_ROUND_PINF: // to plus
            set_float_rounding_mode(float_round_up, &fs);
            break;
    }
    return;
}

static void fp_get_status(uae_u32 *status)
{
	if (fs.float_exception_flags & float_flag_signaling)
		*status |= FPSR_SNAN;
	if (fs.float_exception_flags & float_flag_invalid)
		*status |= FPSR_OPERR;
	if (fs.float_exception_flags & float_flag_divbyzero)
		*status |= FPSR_DZ;
	if (fs.float_exception_flags & float_flag_overflow)
		*status |= FPSR_OVFL;
	if (fs.float_exception_flags & float_flag_underflow)
		*status |= FPSR_UNFL;
	if (fs.float_exception_flags & float_flag_inexact)
		*status |= FPSR_INEX2;
}
STATIC_INLINE void fp_clear_status(void)
{
    fs.float_exception_flags = 0;
}


static const TCHAR *fp_print(fpdata *fpd)
{
	static TCHAR fsout[32];
	flag n, u, d;
	fptype result = 0.0;
	int i;
	floatx80 *fx = &fpd->fpx;

	n = floatx80_is_negative(*fx);
	u = floatx80_is_unnormal(*fx);
	d = floatx80_is_denormal(*fx);
    
	if (floatx80_is_zero(*fx)) {
#if USE_LONG_DOUBLE
		_stprintf(fsout, _T("%c%#.17Le%s%s"), n?'-':'+', (fptype) 0.0, u ? _T("U") : _T(""), d ? _T("D") : _T(""));
#else
		_stprintf(fsout, _T("%c%#.17e%s%s"), n?'-':'+', (fptype) 0.0, u ? _T("U") : _T(""), d ? _T("D") : _T(""));
#endif
	} else if (floatx80_is_infinity(*fx)) {
		_stprintf(fsout, _T("%c%s"), n?'-':'+', _T("inf"));
	} else if (floatx80_is_signaling_nan(*fx)) {
		_stprintf(fsout, _T("%c%s"), n?'-':'+', _T("snan"));
	} else if (floatx80_is_any_nan(*fx)) {
		_stprintf(fsout, _T("%c%s"), n?'-':'+', _T("nan"));
	} else {
		for (i = 63; i >= 0; i--) {
			if (fx->low & (((uae_u64)1)<<i)) {
				result += (fptype) 1.0 / (((uae_u64)1)<<(63-i));
			}
		}
#if USE_LONG_DOUBLE
		result *= powl(2.0, (fx->high&0x7FFF) - 0x3FFF);
		_stprintf(fsout, _T("%c%#.17Le%s%s"), n?'-':'+', result, u ? _T("U") : _T(""), d ? _T("D") : _T(""));
#else
		result *= pow(2.0, (fx->high&0x7FFF) - 0x3FFF);
		_stprintf(fsout, _T("%c%#.17e%s%s"), n?'-':'+', result, u ? _T("U") : _T(""), d ? _T("D") : _T(""));
#endif
	}
	return fsout;
}

/* Functions for detecting float type */
static bool fp_is_snan(fpdata *fpd)
{
    return floatx80_is_signaling_nan(fpd->fpx) != 0;
}
static bool fp_unset_snan(fpdata *fpd)
{
    fpd->fpx.low |= LIT64(0x4000000000000000);
	return 0;
}
static bool fp_is_nan (fpdata *fpd)
{
    return floatx80_is_any_nan(fpd->fpx) != 0;
}
static bool fp_is_infinity (fpdata *fpd)
{
    return floatx80_is_infinity(fpd->fpx) != 0;
}
static bool fp_is_zero(fpdata *fpd)
{
    return floatx80_is_zero(fpd->fpx) != 0;
}
static bool fp_is_neg(fpdata *fpd)
{
    return floatx80_is_negative(fpd->fpx) != 0;
}
static bool fp_is_denormal(fpdata *fpd)
{
    return floatx80_is_denormal(fpd->fpx) != 0;
}
static bool fp_is_unnormal(fpdata *fpd)
{
    return floatx80_is_unnormal(fpd->fpx) != 0;
}

/* Functions for converting between float formats */
static const fptype twoto32 = 4294967296.0;

static void to_native(fptype *fp, fpdata *fpd)
{
    int expon;
    fptype frac;
    
    expon = fpd->fpx.high & 0x7fff;
    
    if (fp_is_zero(fpd)) {
        *fp = fp_is_neg(fpd) ? -0.0 : +0.0;
        return;
    }
    if (fp_is_nan(fpd)) {
#if USE_LONG_DOUBLE
        *fp = sqrtl(-1);
#else
        *fp = sqrt(-1);
#endif
        return;
    }
    if (fp_is_infinity(fpd)) {
		double zero = 0.0;
#if USE_LONG_DOUBLE
		*fp = fp_is_neg(fpd) ? logl(0.0) : (1.0 / zero);
#else
		*fp = fp_is_neg(fpd) ? log(0.0) : (1.0 / zero);
#endif
        return;
    }
    
    frac = (fptype)fpd->fpx.low / (fptype)(twoto32 * 2147483648.0);
    if (fp_is_neg(fpd))
        frac = -frac;
#if USE_LONG_DOUBLE
    *fp = ldexpl (frac, expon - 16383);
#else
    *fp = ldexp (frac, expon - 16383);
#endif
}

static bool to_native_checked(fptype *fp, fpdata *fpd, fpdata *dst)
{
    uint64_t aSig = extractFloatx80Frac(fpd->fpx);
    int32_t aExp = extractFloatx80Exp(fpd->fpx);
	if (aExp == 0x7FFF && (uint64_t)(aSig << 1)) {
		dst->fpx = propagateFloatx80NaN(fpd->fpx, fpd->fpx, &fs);
		return true;
	}	
	to_native(fp, fpd);
	return false;
}

static void from_native(fptype fp, fpdata *fpd)
{
    int expon;
    fptype frac;
    
    if (signbit(fp))
        fpd->fpx.high = 0x8000;
    else
        fpd->fpx.high = 0x0000;
    
    if (isnan(fp)) {
        fpd->fpx.high |= 0x7fff;
        fpd->fpx.low = LIT64(0xffffffffffffffff);
        return;
    }
    if (isinf(fp)) {
        fpd->fpx.high |= 0x7fff;
        fpd->fpx.low = LIT64(0x0000000000000000);
        return;
    }
    if (fp == 0.0) {
        fpd->fpx.low = LIT64(0x0000000000000000);
        return;
    }
    if (fp < 0.0)
        fp = -fp;
    
#if USE_LONG_DOUBLE
     frac = frexpl (fp, &expon);
#else
     frac = frexp (fp, &expon);
#endif
    frac += 0.5 / (twoto32 * twoto32);
    if (frac >= 1.0) {
        frac /= 2.0;
        expon++;
    }
    fpd->fpx.high |= (expon + 16383 - 1) & 0x7fff;
    fpd->fpx.low = (uint64_t)(frac * (fptype)(twoto32 * twoto32));
    
    while (!(fpd->fpx.low & LIT64( 0x8000000000000000))) {
        if (fpd->fpx.high == 0) {
            float_raise(float_flag_denormal, &fs);
            break;
        }
        fpd->fpx.low <<= 1;
        fpd->fpx.high--;
    }
}

static void to_single(fpdata *fpd, uae_u32 wrd1)
{
	float32 f = wrd1;
    fpd->fpx = float32_to_floatx80_allowunnormal(f, &fs);
}
static uae_u32 from_single(fpdata *fpd)
{
    float32 f = floatx80_to_float32(fpd->fpx, &fs);
    return f;
}

static void to_double(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2)
{
    float64 f = ((float64)wrd1 << 32) | wrd2;
    fpd->fpx = float64_to_floatx80_allowunnormal(f, &fs);
}
static void from_double(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2)
{
    float64 f = floatx80_to_float64(fpd->fpx, &fs);
    *wrd1 = f >> 32;
    *wrd2 = (uae_u32)f;
}

static void to_exten(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	fpd->fpx.high = (uae_u16)(wrd1 >> 16);
	fpd->fpx.low = ((uae_u64)wrd2 << 32) | wrd3;
}
static void from_exten(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
	floatx80 f = floatx80_to_floatx80(fpd->fpx, &fs);
	*wrd1 = (uae_u32)(f.high << 16);
	*wrd2 = f.low >> 32;
	*wrd3 = (uae_u32)f.low;
}

static void to_exten_fmovem(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	fpd->fpx.high = (uae_u16)(wrd1 >> 16);
	fpd->fpx.low = ((uae_u64)wrd2 << 32) | wrd3;
}
static void from_exten_fmovem(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
 {
	*wrd1 = (uae_u32)(fpd->fpx.high << 16);
	*wrd2 = fpd->fpx.low >> 32;
	*wrd3 = (uae_u32)fpd->fpx.low;
 }

static uae_s64 to_int(fpdata *src, int size)
{
	switch (size) {
		case 0: return floatx80_to_int8(src->fpx, &fs);
		case 1: return floatx80_to_int16(src->fpx, &fs);
		case 2: return floatx80_to_int32(src->fpx, &fs);
		default: return 0;
     }
}
static void from_int(fpdata *fpd, uae_s32 src)
{
    fpd->fpx = int32_to_floatx80(src, &fs);
}

/* Functions for returning exception state data */
static void fp_get_exceptional_operand(uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
}
static void fp_get_exceptional_operand_grs(uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3, uae_u32 *grs)
{
}

/* Functions for rounding */

static floatx80 fp_to_sgl(floatx80 a)
{
    floatx80 v = floatx80_round32(a, &fs);
	v.high &= 0x7fff;
	v.high |= a.high & 0x7fff;
	return v;
}

// round to float with extended precision exponent
static void fp_round32(fpdata *fpd)
{
    fpd->fpx = floatx80_round32(fpd->fpx, &fs);
}

// round to double with extended precision exponent
static void fp_round64(fpdata *fpd)
{
    fpd->fpx = floatx80_round64(fpd->fpx, &fs);
}

// round to float
static void fp_round_single(fpdata *fpd)
{
	fpd->fpx = floatx80_round_to_float32(fpd->fpx, &fs);
}

// round to double
static void fp_round_double(fpdata *fpd)
{
	fpd->fpx = floatx80_round_to_float64(fpd->fpx, &fs);
}

/* Arithmetic functions */

static void fp_move(fpdata *src, fpdata *dst)
{
    dst->fpx = floatx80_move(src->fpx, &fs);
}

static void fp_int(fpdata *a, fpdata *dst)
{
    dst->fpx = floatx80_round_to_int(a->fpx, &fs);
}

static void fp_intrz(fpdata *a, fpdata *dst)
{
    dst->fpx = floatx80_round_to_int_toward_zero(a->fpx, &fs);
}
static void fp_sqrt(fpdata *a, fpdata *dst)
{
    dst->fpx = floatx80_sqrt(a->fpx, &fs);
}
static void fp_lognp1(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_lognp1_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = log(a->fp + 1.0);
    from_native(fpa, dst);
}
static void fp_sin(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_sin_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = sin(fpa);
    from_native(fpa, dst);
}
static void fp_tan(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_tan_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = tan(fpa);
    from_native(fpa, dst);
}
static void fp_logn(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_logn_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = log(fpa);
    from_native(fpa, dst);
}
static void fp_log10(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_log10_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = log10(fpa);
    from_native(fpa, dst);
}
static void fp_log2(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_log2_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = log2(fpa);
    from_native(fpa, dst);
}

static void fp_abs(fpdata *a, fpdata *dst)
{
	dst->fpx = floatx80_abs(a->fpx, &fs);
}
static void fp_neg(fpdata *a, fpdata *dst)
{
	dst->fpx = floatx80_neg(a->fpx, &fs);
}
static void fp_cos(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_sin_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = cos(fpa);
    from_native(fpa, dst);
}
static void fp_getexp(fpdata *a, fpdata *dst)
{
    dst->fpx = floatx80_getexp(a->fpx, &fs);
}
static void fp_getman(fpdata *a, fpdata *dst)
{
    dst->fpx = floatx80_getman(a->fpx, &fs);
}
static void fp_div(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_div(a->fpx, b->fpx, &fs);
}
static void fp_mod(fpdata *a, fpdata *b, uae_u64 *q, uae_u8 *s)
{
    a->fpx = floatx80_mod(a->fpx, b->fpx, q, s, &fs);
}
static void fp_add(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_add(a->fpx, b->fpx, &fs);
}
static void fp_mul(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_mul(a->fpx, b->fpx, &fs);
}
static void fp_sgldiv(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_sgldiv(a->fpx, b->fpx, &fs);
}
static void fp_sglmul(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_sglmul(a->fpx, b->fpx, &fs);
}
static void fp_rem(fpdata *a, fpdata *b, uae_u64 *q, uae_u8 *s)
{
    a->fpx = floatx80_rem(a->fpx, b->fpx, q, s, &fs);
}
static void fp_scale(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_scale(a->fpx, b->fpx, &fs);
}
static void fp_sub(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_sub(a->fpx, b->fpx, &fs);
}
static void fp_cmp(fpdata *a, fpdata *b)
{
    a->fpx = floatx80_cmp(a->fpx, b->fpx, &fs);
}
static void fp_tst(fpdata *a, fpdata *b)
{
	a->fpx = floatx80_tst(b->fpx, &fs);
}

/* FIXME: create softfloat functions for following arithmetics */

static void fp_sinh(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_sinh_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = sinhl(fpa);
    from_native(fpa, dst);
}
static void fp_etoxm1(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_etoxm1_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = expl(fpa) - 1.0;
    from_native(fpa, dst);
}
static void fp_tanh(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_tanh_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = tanhl(fpa);
    from_native(fpa, dst);
}
static void fp_atan(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_atan_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = atanl(fpa);
    from_native(fpa, dst);
}
static void fp_asin(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_asin_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = asinl(fpa);
    from_native(fpa, dst);
}
static void fp_atanh(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_atanh_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = atanhl(fpa);
    from_native(fpa, dst);
}
static void fp_etox(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_etox_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = expl(fpa);
    from_native(fpa, dst);
}
static void fp_twotox(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_twotox_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = powl(2.0, fpa);
    from_native(fpa, dst);
}
static void fp_tentox(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_tentox_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = powl(10.0, fpa);
    from_native(fpa, dst);
}
static void fp_cosh(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_cosh_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = coshl(fpa);
    from_native(fpa, dst);
}
static void fp_acos(fpdata *a, fpdata *dst)
{
    fptype fpa;
	flag e = 0;
	dst->fpx = floatx80_acos_check(a->fpx, &e, &fs);
	if (e)
		return;
    to_native(&fpa, a);
    fpa = acosl(fpa);
    from_native(fpa, dst);
}

static void fp_normalize(fpdata *a)
{
	a->fpx = floatx80_normalize(a->fpx);
}

void fp_init_softfloat(void)
{
	float_status fsx = { 0 };
	set_floatx80_rounding_precision(80, &fsx);
	set_float_rounding_mode(float_round_to_zero, &fsx);

	fpp_print = fp_print;
	fpp_is_snan = fp_is_snan;
	fpp_unset_snan = fp_unset_snan;
	fpp_is_nan = fp_is_nan;
	fpp_is_infinity = fp_is_infinity;
	fpp_is_zero = fp_is_zero;
	fpp_is_neg = fp_is_neg;
	fpp_is_denormal = fp_is_denormal;
	fpp_is_unnormal = fp_is_unnormal;

	fpp_get_status = fp_get_status;
	fpp_clear_status = fp_clear_status;
	fpp_set_mode = fp_set_mode;

	fpp_from_native = from_native;
	fpp_to_native = to_native;

	fpp_to_int = to_int;
	fpp_from_int = from_int;

	fpp_to_single = to_single;
	fpp_from_single = from_single;
	fpp_to_double = to_double;
	fpp_from_double = from_double;
	fpp_to_exten = to_exten;
	fpp_from_exten = from_exten;
	fpp_to_exten_fmovem = to_exten_fmovem;
	fpp_from_exten_fmovem = from_exten_fmovem;

	fpp_round_single = fp_round_single;
	fpp_round_double = fp_round_double;
	fpp_round32 = fp_round32;
	fpp_round64 = fp_round64;

	fpp_normalize = fp_normalize;
	fpp_get_exceptional_operand = fp_get_exceptional_operand;
	fpp_get_exceptional_operand_grs = fp_get_exceptional_operand_grs;

	fpp_int = fp_int;
	fpp_sinh = fp_sinh;
	fpp_intrz = fp_intrz;
	fpp_sqrt = fp_sqrt;
	fpp_lognp1 = fp_lognp1;
	fpp_etoxm1 = fp_etoxm1;
	fpp_tanh = fp_tanh;
	fpp_atan = fp_atan;
	fpp_atanh = fp_atanh;
	fpp_sin = fp_sin;
	fpp_asin = fp_asin;
	fpp_tan = fp_tan;
	fpp_etox = fp_etox;
	fpp_twotox = fp_twotox;
	fpp_tentox = fp_tentox;
	fpp_logn = fp_logn;
	fpp_log10 = fp_log10;
	fpp_log2 = fp_log2;
	fpp_abs = fp_abs;
	fpp_cosh = fp_cosh;
	fpp_neg = fp_neg;
	fpp_acos = fp_acos;
	fpp_cos = fp_cos;
	fpp_getexp = fp_getexp;
	fpp_getman = fp_getman;
	fpp_div = fp_div;
	fpp_mod = fp_mod;
	fpp_add = fp_add;
	fpp_mul = fp_mul;
	fpp_rem = fp_rem;
	fpp_scale = fp_scale;
	fpp_sub = fp_sub;
	fpp_sgldiv = fp_sgldiv;
	fpp_sglmul = fp_sglmul;
	fpp_cmp = fp_cmp;
	fpp_tst = fp_tst;
	fpp_move = fp_move;
}

