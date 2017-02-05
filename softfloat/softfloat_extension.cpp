

#define SOFTFLOAT_68K

#include <stdint.h>
#include <stdlib.h>

/*============================================================================

This C source file is an extension to the SoftFloat IEC/IEEE Floating-point 
Arithmetic Package, Release 2a.

=============================================================================*/

#include "softfloat/softfloat.h"
#include "softfloat/softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Methods for detecting special conditions for mathematical functions
| supported by MC68881 and MC68862 mathematical coprocessor.
*----------------------------------------------------------------------------*/

#define pi_sig0     LIT64(0xc90fdaa22168c234)
#define pi_sig1     LIT64(0xc4c6628b80dc1cd1)

#define pi_exp      0x4000
#define piby2_exp   0x3FFF
#define piby4_exp   0x3FFE

#define one_exp     0x3FFF
#define one_sig     LIT64(0x8000000000000000)

/*----------------------------------------------------------------------------
 | Arc cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_acos_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }
    
    if (aExp > one_exp || (aExp == one_exp && aSig > one_sig)) {
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                                   0, piby2_exp, pi_sig0, pi_sig1, status);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Arc sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_asin_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }
    
    if (aExp > one_exp || (aExp == one_exp && aSig > one_sig)) {
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Arc tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_atan_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                    aSign, piby2_exp, pi_sig0, pi_sig1, status);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Hyperbolic arc tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_atanh_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }
    
    if (aExp >= one_exp) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_divbyzero, status);
            packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
        }
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_cos_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(0, one_exp, one_sig);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Hyperbolic cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_cosh_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(0, one_exp, one_sig);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | e to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etox_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        if (aSign) return packFloatx80(0, 0, 0);
        return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(0, one_exp, one_sig);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | e to x minus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etoxm1_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        if (aSign) return packFloatx80(aSign, one_exp, one_sig);
        return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Log base 10
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log10_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) propagateFloatx80NaNOneArg(a, status);
        if (aSign == 0)
            return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    if (aSign) {
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Log base 2
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log2_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) propagateFloatx80NaNOneArg(a, status);
        if (aSign == 0)
            return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    if (aSign) {
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Log base e
 *----------------------------------------------------------------------------*/

floatx80 floatx80_logn_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) propagateFloatx80NaNOneArg(a, status);
        if (aSign == 0)
            return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    if (aSign) {
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Log base e of x plus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_lognp1_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) propagateFloatx80NaNOneArg(a, status);
        if (aSign) {
            float_raise(float_flag_invalid, status);
            a.low = floatx80_default_nan_low;
            a.high = floatx80_default_nan_high;
            return a;
        }
        return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    if (aSign && aExp >= one_exp) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_divbyzero, status);
            packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low); /* NaN? */
        }
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sin_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Hyperbolic sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sinh_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tan_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        float_raise(float_flag_invalid, status);
        a.low = floatx80_default_nan_low;
        a.high = floatx80_default_nan_high;
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | Hyperbolic tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tanh_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        return packFloatx80(aSign, one_exp, one_sig);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | 10 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tentox_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        if (aSign) return packFloatx80(0, 0, 0);
        return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(0, one_exp, one_sig);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}

/*----------------------------------------------------------------------------
 | 2 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_twotox_check(floatx80 a, flag *e, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    *e = 1;
    
    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        if (aSign) return packFloatx80(0, 0, 0);
        return packFloatx80(0, 0x7FFF, floatx80_default_infinity_low);
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(0, one_exp, one_sig);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    
    *e = 0;
    return a;
}
