/*
* UAE - The Un*x Amiga Emulator
*
* MC68881/68882/68040/68060 FPU emulation
*
* Native FPU, MSVC 80-bit hack
*/

#include <math.h>
#include <float.h>
#include <fenv.h>

#include "sysconfig.h"
#include "sysdeps.h"

#define USE_HOST_ROUNDING 1

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "fpp.h"
#include "uae/attributes.h"
#include "uae/vm.h"
#include "newcpu.h"
#include "softfloat/softfloat-specialize.h"

extern "C"
{
	extern void _cdecl xfp_fldcw(uae_u16*);
	extern void _cdecl xfp_int(void*, void*);
	extern void _cdecl xfp_mov(void*, void*);
	extern void _cdecl xfp_div(void*, void*);
	extern void _cdecl xfp_mul(void*, void*);
	extern void _cdecl xfp_abs(void*, void*);
	extern void _cdecl xfp_neg(void*, void*);
	extern void _cdecl xfp_add(void*, void*);
	extern void _cdecl xfp_sub(void*, void*);
	extern void _cdecl xfp_sqrt(void*, void*);

	extern void _cdecl xfp_sin(void*, void*);
	extern void _cdecl xfp_cos(void*, void*);
	extern void _cdecl xfp_tan(void*, void*);
	extern void _cdecl xfp_atan(void*, void*);
	extern void _cdecl xfp_asin(void*, void*);
	extern void _cdecl xfp_acos(void*, void*);
	extern void _cdecl xfp_atanh(void*, void*);
	extern void _cdecl xfp_sinh(void*, void*);
	extern void _cdecl xfp_cosh(void*, void*);
	extern void _cdecl xfp_tanh(void*, void*);

	extern void _cdecl xfp_rem(void*, void*);
	extern void _cdecl xfp_rem1(void*, void*);
	extern void _cdecl xfp_getexp(void*, void*);
	extern void _cdecl xfp_getman(void*, void*);
	extern void _cdecl xfp_scale(void*, void*);
	extern void _cdecl xfp_twotox(void*, void*);
	extern void _cdecl xfp_etox(void*, void*);
	extern void _cdecl xfp_etoxm1(void*, void*);
	extern void _cdecl xfp_tentox(void*, void*);
	extern void _cdecl xfp_log2(void*, void*);
	extern void _cdecl xfp_log10(void*, void*);
	extern void _cdecl xfp_logn(void*, void*);
	extern void _cdecl xfp_lognp1(void*, void*);

	extern void _cdecl xfp_to_single(void*, uae_u32*);
	extern void _cdecl xfp_from_single(void*, uae_u32*);
	extern void _cdecl xfp_to_double(void*, uae_u32*);
	extern void _cdecl xfp_from_double(void*, uae_u32*);
	extern void _cdecl xfp_from_int(void*, uae_s32*);
	extern void _cdecl xfp_to_int(void*, uae_s64*);
	extern void _cdecl xfp_round_single(void*, void*);
	extern void _cdecl xfp_round_double(void*, void*);
	extern void _cdecl xfp_x_to_double(void*, fptype*);
	extern void _cdecl xfp_x_from_double(void*, fptype*);

	extern uae_u16 _cdecl xfp_get_status(void);
	extern void _cdecl xfp_clear_status(void);
}
static uae_u16 fpx_mode = 0x107f;

static uae_u32 xhex_nan[]   ={0xffffffff, 0xffffffff, 0x7fff};
static long double *fp_nan    = (long double *)xhex_nan;

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
static uae_u32 fpu_mode_control = 0;
static int fpu_prec;
static int temp_prec;
static uae_u16 fp_status;

static void fp_set_mode(uae_u32 m68k_cw)
{
	// RN, RZ, RD, RU
	static const uae_u16 fp87_round[4] = { 0 << 10, 3 << 10, 1 << 10, 2 << 10 };
	static const uae_u16 sw_round[4] = { float_round_nearest_even, float_round_to_zero, float_round_down, float_round_up };
	// Extend X, Single S, Double D, Undefined (Double)
	static const uae_u16 fp87_prec[4] = { 3 << 8, 0 << 8, 2 << 8, 2 << 8 };
	static const uae_u16 sw_prec[4] = { 80, 64, 32, 64 };

	int round = (m68k_cw >> 4) & 3;
	int prec = (m68k_cw >> 6) & 3;

	fpx_mode = fp87_round[round] | fp87_prec[prec] | 0x107f;
	xfp_fldcw(&fpx_mode);
	set_float_rounding_mode(sw_round[round], &fs);
	set_floatx80_rounding_precision(sw_prec[prec], &fs);

}

/* The main motivation for dynamically creating an x86(-64) function in
* memory is because MSVC (x64) does not allow you to use inline assembly,
* and the x86-64 versions of _control87/_controlfp functions only modifies
* SSE2 registers. */

static uae_u16 x87_cw = 0;
static uae_u8 *x87_fldcw_code = NULL;
typedef void (uae_cdecl *x87_fldcw_function)(void);

void init_fpucw_x87_80(void)
{
	if (x87_fldcw_code) {
		return;
	}
	x87_fldcw_code = (uae_u8 *)uae_vm_alloc(uae_vm_page_size(), UAE_VM_32BIT, UAE_VM_READ_WRITE_EXECUTE);
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
	*(c++) = (((uintptr_t)&x87_cw)) & 0xff;
	*(c++) = (((uintptr_t)&x87_cw) >> 8) & 0xff;
	*(c++) = (((uintptr_t)&x87_cw) >> 16) & 0xff;
	*(c++) = (((uintptr_t)&x87_cw) >> 24) & 0xff;
	/* ret */
	*(c++) = 0xc3;
	/* Write-protect the function */
	uae_vm_protect(x87_fldcw_code, uae_vm_page_size(), UAE_VM_READ_EXECUTE);
}

static void native_set_fpucw(uae_u32 m68k_cw)
{
	static int ex = 0;
	// RN, RZ, RD, RU
	static const unsigned int fp87_round[4] = { _RC_NEAR, _RC_CHOP, _RC_DOWN, _RC_UP };
	// Extend X, Single S, Double D, Undefined
	static const unsigned int fp87_prec[4] = { _PC_53, _PC_24, _PC_53, _PC_53 };
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
	static const uae_u16 x87_cw_tab[] = {
		0x127f, 0x1e7f, 0x167f, 0x1a7f,	/* Double */
		0x107f, 0x1c7f, 0x147f, 0x187f,	/* Single */
		0x127f, 0x1e7f, 0x167f, 0x1a7f,	/* Double */
		0x127f, 0x1e7f, 0x167f, 0x1a7f,	/* undefined (Double) */
	};
	x87_cw = x87_cw_tab[(m68k_cw >> 4) & 0xf];
	((x87_fldcw_function)x87_fldcw_code)();
}

/* Functions for setting host/library modes and getting status */
static void fp_set_mode_native(uae_u32 mode_control)
{
	if (mode_control == fpu_mode_control)
		return;
	switch (mode_control & FPCR_ROUNDING_PRECISION) {
	case FPCR_PRECISION_EXTENDED: // X
		fpu_prec = 80;
		break;
	case FPCR_PRECISION_SINGLE:   // S
		fpu_prec = 32;
		break;
	case FPCR_PRECISION_DOUBLE:   // D
	default:                      // undefined
		fpu_prec = 64;
		break;
	}
#if USE_HOST_ROUNDING
	if ((mode_control & FPCR_ROUNDING_MODE) != (fpu_mode_control & FPCR_ROUNDING_MODE)) {
		switch (mode_control & FPCR_ROUNDING_MODE) {
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
	}
	native_set_fpucw(mode_control);
#endif
	fpu_mode_control = mode_control;
}

static void fp_get_status(uae_u32 *status)
{
	uae_u16 st = xfp_get_status();

	if (st & (1 << 5)) // P
		*status |= FPSR_INEX2;
	if (st & (1 << 4)) // U
		*status |= FPSR_UNFL;
	if (st & (1 << 3)) // O
		*status |= FPSR_OVFL;
	if (st & (1 << 2)) // Z
		*status |= FPSR_DZ;
	*status |= fp_status;
}

static void fp_clear_status(void)
{
	xfp_clear_status();
	fp_status = 0;
}

static void toxnative(fpdata *fpd, fptype *fp)
{
	xfp_x_to_double(&fpd->rfp, fp);
}
static void fromxnative(fptype *fp, fpdata *fpd)
{
	xfp_x_from_double(&fpd->rfp, fp);
	fp_clear_status();
}

static void xfp_to_softfloat(fpdata *fpd)
{
	fpd->fpx.high = fpd->rfp.e;
	fpd->fpx.low = fpd->rfp.m;
}
static void xfp_from_softfloat(fpdata *fpd)
{
	fpd->rfp.e = fpd->fpx.high;
	fpd->rfp.m = fpd->fpx.low;
}

/* Functions for rounding */

// round to float with extended precision exponent
static void fp_round32(fpdata *fpd)
{
	xfp_to_softfloat(fpd);
	fpd->fpx = floatx80_round32(fpd->fpx, &fs);
	xfp_from_softfloat(fpd);
}

// round to double with extended precision exponent
static void fp_round64(fpdata *fpd)
{
	xfp_to_softfloat(fpd);
	fpd->fpx = floatx80_round64(fpd->fpx, &fs);
	xfp_from_softfloat(fpd);
}

// round to float
static void fp_round_single(fpdata *fpd)
{
	xfp_round_single(&fpd->rfp, &fpd->rfp);
}

// round to double
static void fp_round_double(fpdata *fpd)
{
	xfp_round_double(&fpd->rfp, &fpd->rfp);
}


static bool xfp_changed;
static bool native_changed;
static uint8_t xfp_swprec;

static void xfp_resetprec(void)
{
	if (xfp_changed) {
		xfp_fldcw(&fpx_mode);
		set_floatx80_rounding_precision(xfp_swprec, &fs);
		xfp_changed = false;
	}
}

static void xfp_setprec(int prec)
{
	// normal, float, double, extended
	static const uae_u16 prectable[] = { 0, 0 << 8, 2 << 8, 3 << 8 };
	static const uint8_t sfprectable[] = { 32, 64, 0 };
	if (prec == PREC_NORMAL)
		return;
	uae_u16 v = fpx_mode;
	// clear precision fields
	v &= ~(3 << 8);
	v |= prectable[prec];
	if (v != fpx_mode) {
		xfp_fldcw(&v);
		xfp_swprec = fs.floatx80_rounding_precision;
		set_floatx80_rounding_precision(sfprectable[prec], &fs);
		xfp_changed = true;
	} else {
		xfp_changed = false;
	}
}

static void xfp_resetnormal(fpdata *fp)
{
	if (xfp_changed) {
		xfp_fldcw(&fpx_mode);
		set_floatx80_rounding_precision(xfp_swprec, &fs);
		xfp_changed = false;
	}
	xfp_clear_status();
	if (!currprefs.fpu_strict)
		return;
	if (fs.floatx80_rounding_precision == 32)
		fp_round_single(fp);
	else if (fs.floatx80_rounding_precision == 64)
		fp_round_double(fp);
}

// precision bits 8,9
//
// 24-bit: 00
// 32-bit: 10
// 64-bit: 11

// rounding bits 10,11
//
// nearest even: 00
// down toward infinity: 01
// up toward infinity 10
// toward zero: 11

static void xfp_setnormal(void)
{
	uae_u16 v = fpx_mode;
	v |= 3 << 8; // extended
	v &= ~(3 << 10); // round nearest
	if (v != fpx_mode) {
		xfp_fldcw(&v);
		xfp_swprec = fs.floatx80_rounding_precision;
		set_floatx80_rounding_precision(80, &fs);
		xfp_changed = true;
	} else {
		xfp_changed = false;
	}
}

// Must use default precision/rounding mode when calling C-library math functions.
static void fp_normal_prec(void)
{
	if ((fpu_mode_control & FPCR_ROUNDING_PRECISION) != FPCR_PRECISION_DOUBLE || (fpu_mode_control & FPCR_ROUNDING_MODE) != FPCR_ROUND_NEAR) {
		fp_set_mode_native(FPCR_PRECISION_DOUBLE | FPCR_ROUND_NEAR);
		native_changed = true;
	} else {
		native_changed = false;
	}
}

static void fp_reset_normal_prec(void)
{
	if (native_changed) {
		fp_set_mode_native(temp_prec);
		xfp_fldcw(&fpx_mode);
	}
}

static uae_u32 fp_get_support_flags(void)
{
	return FPU_FEATURE_EXCEPTIONS;
}

/* Functions for detecting float type */
static bool fp_is_init(fpdata *fpd)
{
	xfp_to_softfloat(fpd);
	return 0;
}
static bool fp_is_snan(fpdata *fpd)
{
	return floatx80_is_signaling_nan(fpd->fpx) != 0;
}
static bool fp_unset_snan(fpdata *fpd)
{
	fpd->rfp.m |= LIT64(0x4000000000000000);
	return 0;
}
static bool fp_is_nan(fpdata *fpd)
{
	return floatx80_is_nan(fpd->fpx);
}
static bool fp_is_infinity(fpdata *fpd)
{
	return floatx80_is_infinity(fpd->fpx);
}
static bool fp_is_zero(fpdata *fpd)
{
	return floatx80_is_zero(fpd->fpx);
}
static bool fp_is_neg(fpdata *fpd)
{
	return floatx80_is_negative(fpd->fpx);
}
static bool fp_is_denormal(fpdata *fpd)
{
	return floatx80_is_denormal(fpd->fpx);
}
static bool fp_is_unnormal(fpdata *fpd)
{
	return floatx80_is_unnormal(fpd->fpx);
}

/* Functions for converting between float formats */
/* FIXME: how to preserve/fix denormals and unnormals? */

static void fp_to_native(fptype *fp, fpdata *fpd)
{
	toxnative(fpd, fp);
}
static void fp_from_native(fptype fp, fpdata *fpd)
{
	fromxnative(&fp, fpd);
}

static void fp_to_single(fpdata *fpd, uae_u32 wrd1)
{
	xfp_to_single(&fpd->rfp, &wrd1);
}
static uae_u32 fp_from_single(fpdata *fpd)
{
	uae_u32 v;
	xfp_from_single(&fpd->rfp, &v);
	return v;
}

static void fp_to_double(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2)
{
	uae_u32 v[2] = { wrd2, wrd1 };
	xfp_to_double(&fpd->rfp, v);
}
static void fp_from_double(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2)
{
	uae_u32 v[2];
	xfp_from_double(&fpd->rfp, v);
	*wrd1 = v[1];
	*wrd2 = v[0];
}
static void fp_to_exten(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
	fpd->rfp.m = ((uae_u64)wrd2 << 32) | wrd3;
	fpd->rfp.e = wrd1 >> 16;
}
static void fp_from_exten(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
	*wrd3 = (uae_u32)fpd->rfp.m;
	*wrd2 = fpd->rfp.m >> 32;
	*wrd1 = (uae_u32)fpd->rfp.e << 16;
}

static uae_s64 fp_to_int(fpdata *src, int size)
{
	static const fptype fxsizes1[6] =
	{
		-128.0, 127.0,
		-32768.0, 32767.0,
		-2147483648.0, 2147483647.0
	};
	static const uae_s64 fxsizes2[6] =
	{
		-128, 127,
		-32768, 32767,
		-2147483648LL, 2147483647
	};
	// x86 FPU returns infinity if conversion to integer is out
	// of range so convert to double first, then do range check
	fptype d;
	xfp_x_to_double(&src->rfp, &d);
	if (d < fxsizes1[size * 2 + 0]) {
		return fxsizes2[size * 2 + 0];
	} if (d > fxsizes1[size * 2 + 1]) {
		return fxsizes2[size * 2 + 1];
	}
	uae_s64 v;
	xfp_to_int(&src->rfp, &v);
	return v;
}
static void fp_from_int(fpdata *fpd, uae_s32 src)
{
	xfp_from_int(&fpd->rfp, &src);
}

static const TCHAR *fp_print(fpdata *fpd, int mode)
{
	static TCHAR fsout[32];
	bool n;
	fptype fp;

	if (mode < 0) {
		uae_u32 w1, w2, w3;
		fp_from_exten(fpd, &w1, &w2, &w3);
		_stprintf(fsout, _T("%04X-%08X-%08X"), w1 >> 16, w2, w3);
		return fsout;
	}
	toxnative(fpd, &fp);
	fp_normal_prec();

	n = signbit(fp) ? 1 : 0;

	if(isinf(fp)) {
		_stprintf(fsout, _T("%c%s"), n ? '-' : '+', _T("inf"));
	} else if(isnan(fp)) {
		_stprintf(fsout, _T("%c%s"), n ? '-' : '+', _T("nan"));
	} else {
		if(n)
			fpd->fp *= -1.0;
		_stprintf(fsout, _T("#%e"), fp);
	}
	fp_reset_normal_prec();
	if (mode == 0 || mode > _tcslen(fsout))
		return fsout;
	fsout[mode] = 0;
	return fsout;
}

/* Arithmetic functions */

static void fp_move(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_mov(&a->rfp, &b->rfp);
	xfp_resetprec();
}

static void fp_int(fpdata *a, fpdata *b)
{
	xfp_int(&a->rfp, &b->rfp);
}

static void fp_getexp(fpdata *a, fpdata *b)
{
	xfp_getexp(&a->rfp, &b->rfp);
}
static void fp_getman(fpdata *a, fpdata *b)
{
	xfp_getman(&a->rfp, &b->rfp);
}
static void fp_div(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_div(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_mod(fpdata *a, fpdata *b, uae_u64 *q, uae_u8 *s)
{
	xfp_rem(&a->rfp, &b->rfp);
}

static void fp_rem(fpdata *a, fpdata *b, uae_u64 *q, uae_u8 *s)
{
	xfp_rem1(&a->rfp, &b->rfp);
}

static void fp_scale(fpdata *a, fpdata *b)
{
	xfp_scale(&a->rfp, &b->rfp);
}

static void fp_sinh(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_sinh(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_lognp1(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_lognp1(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_etoxm1(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_etoxm1(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_tanh(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_tanh(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_asin(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_asin(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_atanh(fpdata *a, fpdata *b)
{
	xfp_to_softfloat(b);
	a->fpx = floatx80_atanh(b->fpx, &fs);
	xfp_from_softfloat(a);
}
static void fp_etox(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_etox(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_twotox(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_twotox(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_tentox(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_tentox(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_logn(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_logn(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_log10(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_log10(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_log2(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_log2(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_cosh(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_cosh(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}
static void fp_acos(fpdata *a, fpdata *b)
{
	xfp_setnormal();
	xfp_acos(&a->rfp, &b->rfp);
	xfp_resetnormal(a);
}

static void fp_intrz(fpdata *a, fpdata *b)
{
	if ((fpx_mode & (3 << 10)) == (3 << 10)) {
		xfp_int(&a->rfp, &b->rfp);
	} else {
		uae_u16 old = fpx_mode;
		uae_u16 s = fpx_mode | (3 << 10);
		xfp_fldcw(&s);
		xfp_int(&a->rfp, &b->rfp);
		xfp_fldcw(&old);
	}
}
static void fp_sqrt(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_sqrt(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_atan(fpdata *a, fpdata *b)
{
	xfp_atan(&a->rfp, &b->rfp);
}
static void fp_sin(fpdata *a, fpdata *b)
{
	xfp_sin(&a->rfp, &b->rfp);
}
static void fp_tan(fpdata *a, fpdata *b)
{
	xfp_tan(&a->rfp, &b->rfp);
}

static void fp_abs(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_abs(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_neg(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_neg(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_cos(fpdata *a, fpdata *b)
{
	xfp_cos(&a->rfp, &b->rfp);
}
static void fp_sub(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_sub(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_add(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_add(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_mul(fpdata *a, fpdata *b, int prec)
{
	xfp_setprec(prec);
	xfp_mul(&a->rfp, &b->rfp);
	xfp_resetprec();
}
static void fp_sglmul(fpdata *a, fpdata *b)
{
	xfp_setprec(PREC_EXTENDED);
	a->rfp.m &= 0xFFFFFF0000000000;
	b->rfp.m &= 0xFFFFFF0000000000;
	xfp_mul(&a->rfp, &b->rfp);
	fpdata fpx = *a;
	xfp_resetprec();
	fp_round32(a);
	if (fpx.rfp.m != a->rfp.m)
		fp_status |= FPSR_INEX2;
}
static void fp_sgldiv(fpdata *a, fpdata *b)
{
	xfp_setprec(PREC_FLOAT);
	xfp_div(&a->rfp, &b->rfp);
	xfp_resetprec();
}

static void fp_normalize(fpdata *a)
{
}

static void fp_cmp(fpdata *a, fpdata *b)
{
	if (currprefs.fpu_strict) {
		xfp_to_softfloat(a);
		xfp_to_softfloat(b);
		a->fpx = floatx80_cmp(a->fpx, b->fpx, &fs);
		xfp_from_softfloat(a);
	} else {
		xfp_setprec(64);
		xfp_sub(&a->rfp, &b->rfp);
		xfp_resetprec();
	}
	fp_clear_status();
}

static void fp_tst(fpdata *a, fpdata *b)
{
	a->rfp.m = b->rfp.m;
	a->rfp.e = b->rfp.e;
}

/* Functions for returning exception state data */

static void fp_get_internal_overflow(fpdata *fpd)
{
	fpd->rfp.m = 0;
	fpd->rfp.e = 0;
}
static void fp_get_internal_underflow(fpdata *fpd)
{
	fpd->rfp.m = 0;
	fpd->rfp.e = 0;
}
static void fp_get_internal_round_all(fpdata *fpd)
{
	fpd->rfp.m = 0;
	fpd->rfp.e = 0;
}
static void fp_get_internal_round(fpdata *fpd)
{
	fpd->rfp.m = 0;
	fpd->rfp.e = 0;
}
static void fp_get_internal_round_exten(fpdata *fpd)
{
	fpd->rfp.m = 0;
	fpd->rfp.e = 0;
}
static void fp_get_internal(fpdata *fpd)
{
	fpd->rfp.m = 0;
	fpd->rfp.e = 0;
}
static uae_u32 fp_get_internal_grs(void)
{
	return 0;
}

/* Function for denormalizing */
static void fp_denormalize(fpdata *fpd, int esign)
{
}

static void fp_from_pack (fpdata *src, uae_u32 *wrd, int kfactor)
{
	int i, j, t;
	int exp;
	int ndigits;
	char *cp, *strp;
	char str[100];
	fptype fp;

	fp_is_init(src);
   if (fp_is_nan(src)) {
        // copy bit by bit, handle signaling nan
        fpp_from_exten(src, &wrd[0], &wrd[1], &wrd[2]);
        return;
    }
    if (fp_is_infinity(src)) {
        // extended exponent and all 0 packed fraction
        fpp_from_exten(src, &wrd[0], &wrd[1], &wrd[2]);
        wrd[1] = wrd[2] = 0;
        return;
    }

	wrd[0] = wrd[1] = wrd[2] = 0;

	fp_to_native(&fp, src);
	fp_normal_prec();

	sprintf (str, "%#.17e", fp);
	
	// get exponent
	cp = str;
	while (*cp != 'e') {
		if (*cp == 0)
			return;
		cp++;
	}
	cp++;
	if (*cp == '+')
		cp++;
	exp = atoi (cp);

	// remove trailing zeros
	cp = str;
	while (*cp != 'e') {
		cp++;
	}
	cp[0] = 0;
	cp--;
	while (cp > str && *cp == '0') {
		*cp = 0;
		cp--;
	}

	cp = str;
	// get sign
	if (*cp == '-') {
		cp++;
		wrd[0] = 0x80000000;
	} else if (*cp == '+') {
		cp++;
	}
	strp = cp;

	if (kfactor <= 0) {
		ndigits = abs (exp) + (-kfactor) + 1;
	} else {
		if (kfactor > 17) {
			kfactor = 17;
			fpsr_set_exception(FPSR_OPERR);
		}
		ndigits = kfactor;
	}

	if (ndigits < 0)
		ndigits = 0;
	if (ndigits > 16)
		ndigits = 16;

	// remove decimal point
	strp[1] = strp[0];
	strp++;
	// add trailing zeros
	i = strlen (strp);
	cp = strp + i;
	while (i < ndigits) {
		*cp++ = '0';
		i++;
	}
	i = ndigits + 1;
	while (i < 17) {
		strp[i] = 0;
		i++;
	}
	*cp = 0;
	i = ndigits - 1;
	// need to round?
	if (i >= 0 && strp[i + 1] >= '5') {
		while (i >= 0) {
			strp[i]++;
			if (strp[i] <= '9')
				break;
			if (i == 0) {
				strp[i] = '1';
				exp++;
			} else {
				strp[i] = '0';
			}
			i--;
		}
	}
	strp[ndigits] = 0;

	// store first digit of mantissa
	cp = strp;
	wrd[0] |= *cp++ - '0';

	// store rest of mantissa
	for (j = 1; j < 3; j++) {
		for (i = 0; i < 8; i++) {
			wrd[j] <<= 4;
			if (*cp >= '0' && *cp <= '9')
				wrd[j] |= *cp++ - '0';
		}
	}

	// exponent
	if (exp < 0) {
		wrd[0] |= 0x40000000;
		exp = -exp;
	}
	if (exp > 9999) // ??
		exp = 9999;
	if (exp > 999) {
		int d = exp / 1000;
		wrd[0] |= d << 12;
		exp -= d * 1000;
		fpsr_set_exception(FPSR_OPERR);
	}
	i = 100;
	t = 0;
	while (i >= 1) {
		int d = exp / i;
		t <<= 4;
		t |= d;
		exp -= d * i;
		i /= 10;
	}
	wrd[0] |= t << 16;
	fp_reset_normal_prec();
}

static void fp_to_pack (fpdata *fpd, uae_u32 *wrd, int dummy)
{
	fptype d;
	char *cp;
	char str[100];

    if (((wrd[0] >> 16) & 0x7fff) == 0x7fff) {
        // infinity has extended exponent and all 0 packed fraction
        // nans are copies bit by bit
        fpp_to_exten(fpd, wrd[0], wrd[1], wrd[2]);
        return;
    }
    if (!(wrd[0] & 0xf) && !wrd[1] && !wrd[2]) {
        // exponent is not cared about, if mantissa is zero
        wrd[0] &= 0x80000000;
        fpp_to_exten(fpd, wrd[0], wrd[1], wrd[2]);
        return;
    }

	fp_normal_prec();
	cp = str;
	if (wrd[0] & 0x80000000)
		*cp++ = '-';
	*cp++ = (wrd[0] & 0xf) + '0';
	*cp++ = '.';
	*cp++ = ((wrd[1] >> 28) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 24) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 20) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 16) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 12) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 8) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 4) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 0) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 28) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 24) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 20) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 16) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 12) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 8) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 4) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 0) & 0xf) + '0';
	*cp++ = 'E';
	if (wrd[0] & 0x40000000)
		*cp++ = '-';
	*cp++ = ((wrd[0] >> 24) & 0xf) + '0';
	*cp++ = ((wrd[0] >> 20) & 0xf) + '0';
	*cp++ = ((wrd[0] >> 16) & 0xf) + '0';
	*cp = 0;
	sscanf (str, "%le", &d);
	fp_reset_normal_prec();
	fp_from_native(d, fpd);
}


void fp_init_native_80(void)
{
	set_floatx80_rounding_precision(80, &fs);
	set_float_rounding_mode(float_round_to_zero, &fs);

	fpp_print = fp_print;
	fpp_unset_snan = fp_unset_snan;

	fpp_is_init = fp_is_init;
	fpp_is_snan = fp_is_snan;
	fpp_is_nan = fp_is_nan;
	fpp_is_infinity = fp_is_infinity;
	fpp_is_zero = fp_is_zero;
	fpp_is_neg = fp_is_neg;
	fpp_is_denormal = fp_is_denormal;
	fpp_is_unnormal = fp_is_unnormal;
	fpp_fix_infinity = NULL;

	fpp_get_status = fp_get_status;
	fpp_clear_status = fp_clear_status;
	fpp_set_mode = fp_set_mode;
	fpp_get_support_flags = fp_get_support_flags;

	fpp_to_int = fp_to_int;
	fpp_from_int = fp_from_int;

	fpp_to_pack = fp_to_pack;
	fpp_from_pack = fp_from_pack;

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
	fpp_denormalize = fp_denormalize;
	fpp_get_internal_overflow = fp_get_internal_overflow;
	fpp_get_internal_underflow = fp_get_internal_underflow;
	fpp_get_internal_round_all = fp_get_internal_round_all;
	fpp_get_internal_round = fp_get_internal_round;
	fpp_get_internal_round_exten = fp_get_internal_round_exten;
	fpp_get_internal = fp_get_internal;
	fpp_get_internal_grs = fp_get_internal_grs;

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
