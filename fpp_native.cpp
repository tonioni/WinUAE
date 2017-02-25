/*
* UAE - The Un*x Amiga Emulator
*
* MC68881/68882/68040/68060 FPU emulation
*
* Copyright 1996 Herman ten Brugge
* Modified 2005 Peter Keunecke
* 68040+ exceptions and more by Toni Wilen
*/

#define __USE_ISOC9X  /* We might be able to pick up a NaN */

#include <math.h>
#include <float.h>
#include <fenv.h>

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef _MSC_VER
#pragma fenv_access(on)
#endif

#define USE_HOST_ROUNDING

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "fpp.h"
#include "uae/attributes.h"
#include "uae/vm.h"
#include "newcpu.h"

#ifdef JIT
uae_u32 xhex_exp_1[] ={0xa2bb4a9a, 0xadf85458, 0x4000};
uae_u32 xhex_ln_10[] ={0xaaa8ac17, 0x935d8ddd, 0x4000};
uae_u32 xhex_l10_2[] ={0xfbcff798, 0x9a209a84, 0x3ffd};
uae_u32 xhex_l10_e[] ={0x37287195, 0xde5bd8a9, 0x3ffd};
uae_u32 xhex_1e16[]  ={0x04000000, 0x8e1bc9bf, 0x4034};
uae_u32 xhex_1e32[]  ={0x2b70b59e, 0x9dc5ada8, 0x4069};
uae_u32 xhex_1e64[]  ={0xffcfa6d5, 0xc2781f49, 0x40d3};
uae_u32 xhex_1e128[] ={0x80e98ce0, 0x93ba47c9, 0x41a8};
uae_u32 xhex_1e256[] ={0x9df9de8e, 0xaa7eebfb, 0x4351};
uae_u32 xhex_1e512[] ={0xa60e91c7, 0xe319a0ae, 0x46a3};
uae_u32 xhex_1e1024[]={0x81750c17, 0xc9767586, 0x4d48};
uae_u32 xhex_1e2048[]={0xc53d5de5, 0x9e8b3b5d, 0x5a92};
uae_u32 xhex_1e4096[]={0x8a20979b, 0xc4605202, 0x7525};
double fp_1e8 = 1.0e8;
float  fp_1e0 = 1, fp_1e1 = 10, fp_1e2 = 100, fp_1e4 = 10000;
#endif

static uae_u32 xhex_pi[]    ={0x2168c235, 0xc90fdaa2, 0x4000};
static uae_u32 xhex_l2_e[]  ={0x5c17f0bc, 0xb8aa3b29, 0x3fff};
static uae_u32 xhex_ln_2[]  ={0xd1cf79ac, 0xb17217f7, 0x3ffe};
static uae_u32 xhex_inf[]   ={0x00000000, 0x00000000, 0x7fff};
static uae_u32 xhex_nan[]   ={0xffffffff, 0xffffffff, 0x7fff};
static uae_u32 xhex_snan[]  ={0xffffffff, 0xbfffffff, 0x7fff};
#if USE_LONG_DOUBLE
static long double *fp_pi     = (long double *)xhex_pi;
static long double *fp_exp_1  = (long double *)xhex_exp_1;
static long double *fp_l2_e   = (long double *)xhex_l2_e;
static long double *fp_ln_2   = (long double *)xhex_ln_2;
static long double *fp_ln_10  = (long double *)xhex_ln_10;
static long double *fp_l10_2  = (long double *)xhex_l10_2;
static long double *fp_l10_e  = (long double *)xhex_l10_e;
static long double *fp_1e16   = (long double *)xhex_1e16;
static long double *fp_1e32   = (long double *)xhex_1e32;
static long double *fp_1e64   = (long double *)xhex_1e64;
static long double *fp_1e128  = (long double *)xhex_1e128;
static long double *fp_1e256  = (long double *)xhex_1e256;
static long double *fp_1e512  = (long double *)xhex_1e512;
static long double *fp_1e1024 = (long double *)xhex_1e1024;
static long double *fp_1e2048 = (long double *)xhex_1e2048;
static long double *fp_1e4096 = (long double *)xhex_1e4096;
static long double *fp_inf    = (long double *)xhex_inf;
static long double *fp_nan    = (long double *)xhex_nan;
#else
static uae_u32 dhex_pi[]    ={0x54442D18, 0x400921FB};
static uae_u32 dhex_exp_1[] ={0x8B145769, 0x4005BF0A};
static uae_u32 dhex_l2_e[]  ={0x652B82FE, 0x3FF71547};
static uae_u32 dhex_ln_2[]  ={0xFEFA39EF, 0x3FE62E42};
static uae_u32 dhex_ln_10[] ={0xBBB55516, 0x40026BB1};
static uae_u32 dhex_l10_2[] ={0x509F79FF, 0x3FD34413};
static uae_u32 dhex_l10_e[] ={0x1526E50E, 0x3FDBCB7B};
static uae_u32 dhex_1e16[]  ={0x37E08000, 0x4341C379};
static uae_u32 dhex_1e32[]  ={0xB5056E17, 0x4693B8B5};
static uae_u32 dhex_1e64[]  ={0xE93FF9F5, 0x4D384F03};
static uae_u32 dhex_1e128[] ={0xF9301D32, 0x5A827748};
static uae_u32 dhex_1e256[] ={0x7F73BF3C, 0x75154FDD};
static uae_u32 dhex_inf[]   ={0x00000000, 0x7ff00000};
static uae_u32 dhex_nan[]   ={0xffffffff, 0x7fffffff};
static double *fp_pi     = (double *)dhex_pi;
static double *fp_exp_1  = (double *)dhex_exp_1;
static double *fp_l2_e   = (double *)dhex_l2_e;
static double *fp_ln_2   = (double *)dhex_ln_2;
static double *fp_ln_10  = (double *)dhex_ln_10;
static double *fp_l10_2  = (double *)dhex_l10_2;
static double *fp_l10_e  = (double *)dhex_l10_e;
static double *fp_1e16   = (double *)dhex_1e16;
static double *fp_1e32   = (double *)dhex_1e32;
static double *fp_1e64   = (double *)dhex_1e64;
static double *fp_1e128  = (double *)dhex_1e128;
static double *fp_1e256  = (double *)dhex_1e256;
static double *fp_1e512  = (double *)dhex_inf;
static double *fp_1e1024 = (double *)dhex_inf;
static double *fp_1e2048 = (double *)dhex_inf;
static double *fp_1e4096 = (double *)dhex_inf;
static double *fp_inf    = (double *)dhex_inf;
static double *fp_nan    = (double *)dhex_nan;
#endif
static const double twoto32 = 4294967296.0;

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

#if defined(CPU_i386) || defined(CPU_x86_64)

/* The main motivation for dynamically creating an x86(-64) function in
 * memory is because MSVC (x64) does not allow you to use inline assembly,
 * and the x86-64 versions of _control87/_controlfp functions only modifies
 * SSE2 registers. */

static uae_u16 x87_cw = 0;
static uae_u8 *x87_fldcw_code = NULL;
typedef void (uae_cdecl *x87_fldcw_function)(void);

void init_fpucw_x87(void)
{
	if (x87_fldcw_code) {
		return;
	}
	x87_fldcw_code = (uae_u8 *) uae_vm_alloc(
		uae_vm_page_size(), UAE_VM_32BIT, UAE_VM_READ_WRITE_EXECUTE);
	uae_u8 *c = x87_fldcw_code;
	/* mov eax,0x0 */
	*(c++) = 0xb8;
	*(c++) = 0x00;
	*(c++) = 0x00;
	*(c++) = 0x00;
	*(c++) = 0x00;
#ifdef CPU_x86_64
	/* Address override prefix */
	*(c++) = 0x67;
#endif
	/* fldcw WORD PTR [eax+addr] */
	*(c++) = 0xd9;
	*(c++) = 0xa8;
	*(c++) = (((uintptr_t) &x87_cw)      ) & 0xff;
	*(c++) = (((uintptr_t) &x87_cw) >>  8) & 0xff;
	*(c++) = (((uintptr_t) &x87_cw) >> 16) & 0xff;
	*(c++) = (((uintptr_t) &x87_cw) >> 24) & 0xff;
	/* ret */
	*(c++) = 0xc3;
	/* Write-protect the function */
	uae_vm_protect(x87_fldcw_code, uae_vm_page_size(), UAE_VM_READ_EXECUTE);
}

static void set_fpucw_x87(uae_u32 m68k_cw)
{
#ifdef _MSC_VER
	static int ex = 0;
	// RN, RZ, RM, RP
	static const unsigned int fp87_round[4] = { _RC_NEAR, _RC_CHOP, _RC_DOWN, _RC_UP };
	// Extend X, Single S, Double D, Undefined
	static const unsigned int fp87_prec[4] = { _PC_64, _PC_24, _PC_53, 0 };
	int round = (m68k_cw >> 4) & 3;
#ifdef WIN64
	// x64 only sets SSE2, must also call x87_fldcw_code() to set FPU rounding mode.
	_controlfp(ex | fp87_round[round], _MCW_RC);
#else
	int prec = (m68k_cw >> 6) & 3;
	// x86 sets both FPU and SSE2 rounding mode, don't need x87_fldcw_code()
	_control87(ex | fp87_round[round] | fp87_prec[prec], _MCW_RC | _MCW_PC);
	return;
#endif
#endif
	static const uae_u16 x87_cw_tab[] = {
		0x137f, 0x1f7f, 0x177f, 0x1b7f,	/* Extended */
		0x107f, 0x1c7f, 0x147f, 0x187f,	/* Single */
		0x127f, 0x1e7f, 0x167f, 0x1a7f,	/* Double */
		0x137f, 0x1f7f, 0x177f, 0x1b7f	/* undefined */
	};
	x87_cw = x87_cw_tab[(m68k_cw >> 4) & 0xf];
#if defined(X86_MSVC_ASSEMBLY) && 0
	__asm { fldcw word ptr x87_cw }
#elif defined(__GNUC__) && 0
	__asm__("fldcw %0" : : "m" (*&x87_cw));
#else
	((x87_fldcw_function) x87_fldcw_code)();
#endif
}

#endif /* defined(CPU_i386) || defined(CPU_x86_64) */

static void native_set_fpucw(uae_u32 m68k_cw)
{
#if defined(CPU_i386) || defined(CPU_x86_64)
	set_fpucw_x87(m68k_cw);
#endif
}

/* Functions for setting host/library modes and getting status */
static void fp_set_mode(uae_u32 mode_control)
{
    switch(mode_control & FPCR_ROUNDING_PRECISION) {
        case FPCR_PRECISION_EXTENDED: // X
            break;
        case FPCR_PRECISION_SINGLE:   // S
            break;
        case FPCR_PRECISION_DOUBLE:   // D
        default:                      // undefined
            break;
    }
#ifdef USE_HOST_ROUNDING
    switch(mode_control & FPCR_ROUNDING_MODE) {
        case FPCR_ROUND_NEAR: // to neareset
            fesetround(FE_TONEAREST);
            break;
        case FPCR_ROUND_ZERO: // to zero
            fesetround(FE_TOWARDZERO);
            break;
        case FPCR_ROUND_MINF: // to minus
            fesetround(FE_DOWNWARD);
            break;
        case FPCR_ROUND_PINF: // to plus
            fesetround(FE_UPWARD);
            break;
    }
	native_set_fpucw(mode_control);
#endif
}


static void fp_get_status(uae_u32 *status)
{
    int exp_flags = fetestexcept(FE_ALL_EXCEPT);
    if (exp_flags) {
        if (exp_flags & FE_INEXACT)
            *status |= FPSR_INEX2;
        if (exp_flags & FE_DIVBYZERO)
            *status |= FPSR_DZ;
        if (exp_flags & FE_UNDERFLOW)
            *status |= FPSR_UNFL;
        if (exp_flags & FE_OVERFLOW)
            *status |= FPSR_OVFL;
        if (exp_flags & FE_INVALID)
            *status |= FPSR_OPERR;
    }
	/* FIXME: how to detect SNAN? */
}

static void fp_clear_status(void)
{
    feclearexcept (FE_ALL_EXCEPT);
}

static const TCHAR *fp_print(fpdata *fpd)
{
	static TCHAR fs[32];
	bool n, d;

	n = signbit(fpd->fp) ? 1 : 0;
	d = isnormal(fpd->fp) ? 0 : 1;

	if(isinf(fpd->fp)) {
		_stprintf(fs, _T("%c%s"), n ? '-' : '+', _T("inf"));
	} else if(isnan(fpd->fp)) {
		_stprintf(fs, _T("%c%s"), n ? '-' : '+', _T("nan"));
	} else {
		if(n)
			fpd->fp *= -1.0;
#if USE_LONG_DOUBLE
		_stprintf(fs, _T("#%Le"), fpd->fp);
#else
		_stprintf(fs, _T("#%e"), fpd->fp);
#endif
	}
	return fs;
}

/* Functions for detecting float type */
static bool fp_is_snan(fpdata *fpd)
{
    return 0; /* FIXME: how to detect SNAN */
}
static bool fp_unset_snan(fpdata *fpd)
{
    /* FIXME: how to unset SNAN */
	return 0;
}
static bool fp_is_nan (fpdata *fpd)
{
    return isnan(fpd->fp) != 0;
}
static bool fp_is_infinity (fpdata *fpd)
{
    return isinf(fpd->fp) != 0;
}
static bool fp_is_zero(fpdata *fpd)
{
    return fpd->fp == 0.0;
}
static bool fp_is_neg(fpdata *fpd)
{
    return signbit(fpd->fp) != 0;
}
static bool fp_is_denormal(fpdata *fpd)
{
    return false;
	//return (isnormal(fpd->fp) == 0); /* FIXME: how to differ denormal/unnormal? */
}
static bool fp_is_unnormal(fpdata *fpd)
{
	return false;
    //return (isnormal(fpd->fp) == 0); /* FIXME: how to differ denormal/unnormal? */
}

/* Functions for converting between float formats */
/* FIXME: how to preserve/fix denormals and unnormals? */

static void fp_to_native(fptype *fp, fpdata *fpd)
{
    *fp = fpd->fp;
}
static void fp_from_native(fptype fp, fpdata *fpd)
{
    fpd->fp = fp;
}

static void fp_to_single(fpdata *fpd, uae_u32 wrd1)
{
    union {
        float f;
        uae_u32 u;
    } val;
    
    val.u = wrd1;
    fpd->fp = (fptype) val.f;
}
static uae_u32 fp_from_single(fpdata *fpd)
{
    union {
        float f;
        uae_u32 u;
    } val;
    
    val.f = (float) fpd->fp;
    return val.u;
}

static void fp_to_double(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2)
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
    fpd->fp = (fptype) val.d;
}
static void fp_from_double(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2)
{
    union {
        double d;
        uae_u32 u[2];
    } val;
    
    val.d = (double) fpd->fp;
#ifdef WORDS_BIGENDIAN
    *wrd1 = val.u[0];
    *wrd2 = val.u[1];
#else
    *wrd1 = val.u[1];
    *wrd2 = val.u[0];
#endif
}
#ifdef USE_LONG_DOUBLE
static void fp_to_exten(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    union {
        long double ld;
        uae_u32 u[3];
    } val;

#if WORDS_BIGENDIAN
    val.u[0] = (wrd1 & 0xffff0000) | ((wrd2 & 0xffff0000) >> 16);
    val.u[1] = (wrd2 & 0x0000ffff) | ((wrd3 & 0xffff0000) >> 16);
    val.u[2] = (wrd3 & 0x0000ffff) << 16;
#else
    val.u[0] = wrd3;
    val.u[1] = wrd2;
    val.u[2] = wrd1 >> 16;
#endif
    fpd->fp = val.ld;
}
static void fp_from_exten(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
    union {
        long double ld;
        uae_u32 u[3];
    } val;
    
    val.ld = fpd->fp;
#if WORDS_BIGENDIAN
    *wrd1 = val.u[0] & 0xffff0000;
    *wrd2 = ((val.u[0] & 0x0000ffff) << 16) | ((val.u[1] & 0xffff0000) >> 16);
    *wrd3 = ((val.u[1] & 0x0000ffff) << 16) | ((val.u[2] & 0xffff0000) >> 16);
#else
    *wrd3 = val.u[0];
    *wrd2 = val.u[1];
    *wrd1 = val.u[2] << 16;
#endif
}
#else // if !USE_LONG_DOUBLE
static void fp_to_exten(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
#if 1
	floatx80 fx80;
	fx80.high = wrd1 >> 16;
	fx80.low = (((uae_u64)wrd2) << 32) | wrd3;
	float64 f = floatx80_to_float64(fx80, &fs);
	fp_to_double(fpd, f >> 32, (uae_u32)f);
#else
    double frac;
    if ((wrd1 & 0x7fff0000) == 0 && wrd2 == 0 && wrd3 == 0) {
        fpd->fp = (wrd1 & 0x80000000) ? -0.0 : +0.0;
        return;
    }
    frac = ((double)wrd2 + ((double)wrd3 / twoto32)) / 2147483648.0;
    if (wrd1 & 0x80000000)
        frac = -frac;
    fpd->fp = ldexp (frac, ((wrd1 >> 16) & 0x7fff) - 16383);
#endif
}
static void fp_from_exten(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
#if 1
	uae_u32 w1, w2;
	fp_from_double(fpd, &w1, &w2);
	floatx80 f = float64_to_floatx80(((uae_u64)w1 << 32) | w2, &fs);
	*wrd1 = f.high << 16;
	*wrd2 = f.low >> 32;
	*wrd3 = (uae_u32)f.low;
#else
    int expon;
    double frac;
    fptype v;
    
    v = fpd->fp;
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
#endif
}
#endif // !USE_LONG_DOUBLE

static uae_s64 fp_to_int(fpdata *src, int size)
{
    static const fptype fxsizes[6] =
    {
               -128.0,        127.0,
             -32768.0,      32767.0,
        -2147483648.0, 2147483647.0
    };

	fptype fp = src->fp;
	if (fp_is_nan(src)) {
		uae_u32 w1, w2, w3;
		fp_from_exten(src, &w1, &w2, &w3);
		uae_s64 v = 0;
		fpsr_set_exception(FPSR_OPERR);
		// return mantissa
		switch (size)
		{
			case 0:
			v = w2 >> 24;
			break;
			case 1:
			v = w2 >> 16;
			break;
			case 2:
			v = w2 >> 0;
			break;
		}
		return v;
	}
	if (fp < fxsizes[size * 2 + 0]) {
		fp = fxsizes[size * 2 + 0];
		fpsr_set_exception(FPSR_OPERR);
	}
	if (fp > fxsizes[size * 2 + 1]) {
		fp = fxsizes[size * 2 + 1];
		fpsr_set_exception(FPSR_OPERR);
	}
#ifdef USE_HOST_ROUNDING
#ifdef USE_LONG_DOUBLE
	return lrintl(fp);
#else
	return lrint(fp);
#endif
#else
	tointtype result = (int)fp;
	switch (regs.fpcr & 0x30)
	{
		case FPCR_ROUND_ZERO:
			result = (int)fp_round_to_zero (fp);
			break;
		case FPCR_ROUND_MINF:
			result = (int)fp_round_to_minus_infinity (fp);
			break;
		case FPCR_ROUND_NEAR:
			result = fp_round_to_nearest (fp);
			break;
		case FPCR_ROUND_PINF:
			result = (int)fp_round_to_plus_infinity (fp);
			break;
	}
	return result;
#endif
}
static void fp_from_int(fpdata *fpd, uae_s32 src)
{
    fpd->fp = (fptype) src;
}


/* Functions for rounding */

// round to float with extended precision exponent
static void fp_round32(fpdata *fpd)
{
    int expon;
    float mant;
#ifdef USE_LONG_DOUBLE
    mant = (float)(frexpl(fpd->fp, &expon) * 2.0);
    fpd->fp = ldexpl((fptype)mant, expon - 1);
#else
    mant = (float)(frexp(fpd->fp, &expon) * 2.0);
    fpd->fp = ldexp((fptype)mant, expon - 1);
#endif
}

// round to double with extended precision exponent
static void fp_round64(fpdata *fpd)
{
    int expon;
    double mant;
#ifdef USE_LONG_DOUBLE
    mant = (double)(frexpl(fpd->fp, &expon) * 2.0);
    fpd->fp = ldexpl((fptype)mant, expon - 1);
#else
    mant = (double)(frexp(fpd->fp, &expon) * 2.0);
    fpd->fp = ldexp((fptype)mant, expon - 1);
#endif
}

// round to float
static void fp_round_single(fpdata *fpd)
{
    fpd->fp = (float) fpd->fp;
}

// round to double
static void fp_round_double(fpdata *fpd)
{
#ifdef USE_LONG_DOUBLE
	fpd->fp = (double) fpd->fp;
#endif
}

/* Arithmetic functions */

static void fp_move(fpdata *src, fpdata *dst)
{
	dst->fp = src->fp;
}

#ifdef USE_LONG_DOUBLE

STATIC_INLINE fptype fp_int(fpdata *a, fpdata *dst)
{
#ifdef USE_HOST_ROUNDING
    dst->fp = rintl(a->dst);
#else
    switch (regs.fpcr & FPCR_ROUNDING_MODE)
    {
        case FPCR_ROUND_NEAR:
            return fp_round_to_nearest(a);
        case FPCR_ROUND_ZERO:
            return fp_round_to_zero(a);
        case FPCR_ROUND_MINF:
            return fp_round_to_minus_infinity(a);
        case FPCR_ROUND_PINF:
            return fp_round_to_plus_infinity(a);
        default: /* never reached */
            return a;
    }
#endif
}
STATIC_INLINE fptype fp_mod(fptype a, fptype b, uae_u64 *q, uae_s8 *s)
{
    fptype quot;
#ifdef USE_HOST_ROUNDING
    quot = truncl(a / b);
#else
    quot = fp_round_to_zero(a / b);
#endif
    if (quot < 0.0) {
        *s = 1;
        quot = -quot;
    } else {
        *s = 0;
    }
    *q = (uae_u64)quot;
    return fmodl(a, b);
}
STATIC_INLINE fptype fp_rem(fptype a, fptype b, uae_u64 *q, uae_s8 *s)
{
    fptype quot;
#ifdef USE_HOST_ROUNDING
    quot = roundl(a / b);
#else
    quot = fp_round_to_nearest(a / b);
#endif
    if (quot < 0.0) {
        *s = 1;
        quot = -quot;
    } else {
        *s = 0;
    }
    *q = (uae_u64)quot;
    return remainderl(a, b);
}

#else // if !USE_LONG_DOUBLE

static void fp_int(fpdata *fpd, fpdata *dst)
{
	fptype a = fpd->fp;
#ifdef USE_HOST_ROUNDING
	dst->fp = rintl(a);
#else
    switch (regs.fpcr & FPCR_ROUNDING_MODE)
    {
        case FPCR_ROUND_NEAR:
            dst->fp = fp_round_to_nearest(a);
        case FPCR_ROUND_ZERO:
            dst->fp = fp_round_to_zero(a);
        case FPCR_ROUND_MINF:
            dst->fp = fp_round_to_minus_infinity(a);
        case FPCR_ROUND_PINF:
            dst->fp = fp_round_to_plus_infinity(a);
        default: /* never reached */
		break;
    }
#endif
}

static void fp_getexp(fpdata *a, fpdata *dst)
{
    int expon;
    frexpl(a->fp, &expon);
    dst->fp = (double) (expon - 1);
}
static void fp_getman(fpdata *a, fpdata *dst)
{
    int expon;
    dst->fp = frexpl(a->fp, &expon) * 2.0;
}
static void fp_div(fpdata *a, fpdata *b)
{
	a->fp = a->fp / b->fp;
}
static void fp_mod(fpdata *a, fpdata *b, uae_u64 *q, uae_u8 *s)
{
    fptype quot;
#ifdef USE_HOST_ROUNDING
    quot = truncl(a->fp / b->fp);
#else
    quot = fp_round_to_zero(a->fp / b->fp);
#endif
    if (quot < 0.0) {
        *s = 1;
        quot = -quot;
    } else {
        *s = 0;
    }
    *q = (uae_u64)quot;
    a->fp = fmodl(a->fp, b->fp);
}
static void fp_rem(fpdata *a, fpdata *b, uae_u64 *q, uae_u8 *s)
{
    fptype quot;
#ifdef USE_HOST_ROUNDING
    quot = roundl(a->fp / b->fp);
#else
    quot = fp_round_to_nearest(a->fp / b->fp);
#endif
    if (quot < 0.0) {
        *s = 1;
        quot = -quot;
    } else {
        *s = 0;
    }
    *q = (uae_u64)quot;
    a->fp = remainderl(a->fp, b->fp);
}

static void fp_scale(fpdata *a, fpdata *b)
{
	a->fp = ldexpl(a->fp, (int)b->fp);
}

#endif // !USE_LONG_DOUBLE

static void fp_sinh(fpdata *a, fpdata *dst)
{
	dst->fp = sinhl(a->fp);
}
static void fp_intrz(fpdata *fpd, fpdata *dst)
{
#ifdef USE_HOST_ROUNDING
    dst->fp = truncl(fpd->fp);
#else
    dst->fp = fp_round_to_zero (fpd->fp);
#endif
}
static void fp_sqrt(fpdata *a, fpdata *dst)
{
	dst->fp = sqrtl(a->fp);
}
static void fp_lognp1(fpdata *a, fpdata *dst)
{
	dst->fp = log1pl(a->fp);
}
static void fp_etoxm1(fpdata *a, fpdata *dst)
{
	dst->fp = expm1l(a->fp);
}
static void fp_tanh(fpdata *a, fpdata *dst)
{
	dst->fp = tanhl(a->fp);
}
static void fp_atan(fpdata *a, fpdata *dst)
{
	dst->fp = atanl(a->fp);
}
static void fp_atanh(fpdata *a, fpdata *dst)
{
	dst->fp = atanhl(a->fp);
}
static void fp_sin(fpdata *a, fpdata *dst)
{
	dst->fp = sinl(a->fp);
}
static void fp_asin(fpdata *a, fpdata *dst)
{
	dst->fp = asinl(a->fp);
}
static void fp_tan(fpdata *a, fpdata *dst)
{
	dst->fp = tanl(a->fp);
}
static void fp_etox(fpdata *a, fpdata *dst)
{
	dst->fp = expl(a->fp);
}
static void fp_twotox(fpdata *a, fpdata *dst)
{
	dst->fp = powl(2.0, a->fp);
}
static void fp_tentox(fpdata *a, fpdata *dst)
{
	dst->fp = powl(10.0, a->fp);
}
static void fp_logn(fpdata *a, fpdata *dst)
{
	dst->fp = logl(a->fp);
}
static void fp_log10(fpdata *a, fpdata *dst)
{
	dst->fp = log10l(a->fp);
}
static void fp_log2(fpdata *a, fpdata *dst)
{
	dst->fp = log2l(a->fp);
}
static void fp_abs(fpdata *a, fpdata *dst)
{
	dst->fp = a->fp < 0.0 ? -a->fp : a->fp;
}
static void fp_cosh(fpdata *a, fpdata *dst)
{
	dst->fp = coshl(a->fp);
}
static void fp_neg(fpdata *a, fpdata *dst)
{
	dst->fp = -a->fp;
}
static void fp_acos(fpdata *a, fpdata *dst)
{
	dst->fp = acosl(a->fp);
}
static void fp_cos(fpdata *a, fpdata *dst)
{
	dst->fp = cosl(a->fp);
}
static void fp_sub(fpdata *a, fpdata *b)
{
	a->fp = a->fp - b->fp;
}
static void fp_add(fpdata *a, fpdata *b)
{
	a->fp = a->fp + b->fp;
}
static void fp_mul(fpdata *a, fpdata *b)
{
	a->fp = a->fp * b->fp;
}
static void fp_sglmul(fpdata *a, fpdata *b)
{
	// not exact
	a->fp = a->fp * b->fp;
	fpp_round32(a);
}
static void fp_sgldiv(fpdata *a, fpdata *b)
{
	// not exact
	a->fp = a->fp / b->fp;
	fpp_round32(a);
}

static void fp_normalize(fpdata *a)
{
}

static void fp_cmp(fpdata *a, fpdata *b)
{
	bool a_neg = fpp_is_neg(a);
	bool b_neg = fpp_is_neg(b);
	bool a_inf = fpp_is_infinity(a);
	bool b_inf = fpp_is_infinity(b);
	bool a_zero = fpp_is_zero(a);
	bool b_zero = fpp_is_zero(b);
	bool a_nan = fpp_is_nan(a);
	bool b_nan = fpp_is_nan(b);
	fptype v = 1.0;

	if (a_nan || b_nan) {
		// FCMP never returns N + NaN
		v = *fp_nan;
	} else if (a_zero && b_zero) {
		if ((a_neg && b_neg) || (a_neg && !b_neg))
			v = -0.0;
		else
			v = 0.0;
	} else if (a_zero && b_inf) {
		if (!b_neg)
			v = -1.0;
		else
			v = 1.0;
	} else if (a_inf && b_zero) {
		if (!a_neg)
			v = -1.0;
		else
			v = 1.0;
	} else if (a_inf && b_inf) {
		if (a_neg == b_neg)
			v = 0.0;
		if ((a_neg && b_neg) || (a_neg && !b_neg))
			v = -v;
	} else if (a_inf) {
		if (a_neg)
			v = -1.0;
	} else if (b_inf) {
		if (!b_neg)
			v = -1.0;
	} else {
		fpp_sub(a, b);
		v = a->fp;
		fp_clear_status();
	}
	a->fp = v;
}

static void fp_tst(fpdata *a, fpdata *b)
{
	a->fp = b->fp;
}

/* Functions for returning exception state data */
static void fp_get_exceptional_operand(uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
}
static void fp_get_exceptional_operand_grs(uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3, uae_u32 *grs)
{
}

void fp_init_native(void)
{
	set_floatx80_rounding_precision(80, &fs);
	set_float_rounding_mode(float_round_to_zero, &fs);

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

	fpp_from_native = fp_from_native;
	fpp_to_native = fp_to_native;

	fpp_to_int = fp_to_int;
	fpp_from_int = fp_from_int;

	fpp_to_single = fp_to_single;
	fpp_from_single = fp_from_single;
	fpp_to_double = fp_to_double;
	fpp_from_double = fp_from_double;
	fpp_to_exten = fp_to_exten;
	fpp_from_exten = fp_from_exten;
	fpp_to_exten_fmovem = fp_to_exten;
	fpp_from_exten_fmovem = fp_from_exten;

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
