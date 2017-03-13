/*============================================================================

This C source file is an extension to the SoftFloat IEC/IEEE Floating-point 
Arithmetic Package, Release 2a.

=============================================================================*/

#include <stdint.h>

#include "softfloat.h"
#include "softfloat-macros.h"
#include "softfloat/softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Methods for converting decimal floats to binary extended precision floats.
*----------------------------------------------------------------------------*/

void mul128by128(int32_t *aExp, uint64_t *aSig0, uint64_t *aSig1, int32_t bExp, uint64_t bSig0, uint64_t bSig1)
{
    int32_t zExp;
    uint64_t zSig0, zSig1, zSig2, zSig3;
    
    zExp = *aExp;
    zSig0 = *aSig0;
    zSig1 = *aSig1;

    zExp += bExp - 0x3FFE;
    mul128To256(zSig0, zSig1, bSig0, bSig1, &zSig0, &zSig1, &zSig2, &zSig3);
    zSig1 |= (zSig2 | zSig3) != 0;
    if ( 0 < (int64_t) zSig0 ) {
        shortShift128Left( zSig0, zSig1, 1, &zSig0, &zSig1 );
        --zExp;
    }
    *aExp = zExp;
    *aSig0 = zSig0;
    *aSig1 = zSig1;
}

void div128by128(int32_t *aExp, uint64_t *aSig0, uint64_t *aSig1, int32_t bExp, uint64_t bSig0, uint64_t bSig1)
{
    int32_t zExp;
    uint64_t zSig0, zSig1;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;
    
    zExp = *aExp;
    zSig0 = *aSig0;
    zSig1 = *aSig1;
    
    zExp -= bExp - 0x3FFE;
    rem1 = 0;
    if ( le128( bSig0, bSig1, zSig0, zSig1 ) ) {
        shift128Right( zSig0, zSig1, 1, &zSig0, &zSig1 );
        ++zExp;
    }
    zSig0 = estimateDiv128To64( zSig0, zSig1, bSig0 );
    mul128By64To192( bSig0, bSig1, zSig0, &term0, &term1, &term2 );
    sub192( zSig0, zSig1, 0, term0, term1, term2, &rem0, &rem1, &rem2 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        add192( rem0, rem1, rem2, 0, bSig0, bSig1, &rem0, &rem1, &rem2 );
    }
    zSig1 = estimateDiv128To64( rem1, rem2, bSig0 );
    if ( ( zSig1 & 0x3FFF ) <= 4 ) {
        mul128By64To192( bSig0, bSig1, zSig1, &term1, &term2, &term3 );
        sub192( rem1, rem2, 0, term1, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            add192( rem1, rem2, rem3, 0, bSig0, bSig1, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }

    *aExp = zExp;
    *aSig0 = zSig0;
    *aSig1 = zSig1;
}

/*----------------------------------------------------------------------------
| Decimal to binary
*----------------------------------------------------------------------------*/

floatx80 floatdecimal_to_floatx80(floatx80 a, float_status *status)
{
    flag decSign, zSign, decExpSign, increment;
    int32_t decExp, zExp, mExp, xExp, shiftCount;
    uint64_t decSig, zSig0, zSig1, mSig0, mSig1, xSig0, xSig1;
    
    decSign = extractFloatx80Sign(a);
    decExp = extractFloatx80Exp(a);
    decSig = extractFloatx80Frac(a);
    
    if (decExp == 0x7FFF) return a;
    
    if (decExp == 0 && decSig == 0) return a;
    
    decExpSign = (decExp >> 14) & 1;
    decExp &= 0x3FFF;
    
    shiftCount = countLeadingZeros64( decSig );
    zExp = 0x403E - shiftCount;
    zSig0 = decSig << shiftCount;
    zSig1 = 0;
    zSign = decSign;
    
    mExp = 0x4002;
    mSig0 = LIT64(0xA000000000000000);
	mSig1 = 0;
    xExp = 0x3FFF;
    xSig0 = LIT64(0x8000000000000000);
    
    while (decExp) {
        if (decExp & 1) {
            mul128by128(&xExp, &xSig0, &xSig1, mExp, mSig0, mSig1);
        }
        mul128by128(&mExp, &mSig0, &mSig1, mExp, mSig0, mSig1);
        decExp >>= 1;
    }
    
    if (decExpSign) {
        div128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
    } else {
        mul128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
    }
    
    increment = ( (int64_t) zSig1 < 0 );
    if (status->float_rounding_mode != float_round_nearest_even) {
        if (status->float_rounding_mode == float_round_to_zero) {
            increment = 0;
        } else {
            if (zSign) {
                increment = (status->float_rounding_mode == float_round_down) && zSig1;
            } else {
                increment = (status->float_rounding_mode == float_round_up) && zSig1;
            }
        }
    }
    if (zSig1) float_raise(float_flag_decimal, status);
    
    if (increment) {
        ++zSig0;
        if (zSig0 == 0) {
            ++zExp;
            zSig0 = LIT64(0x8000000000000000);
        } else {
            zSig0 &= ~ (((uint64_t) (zSig1<<1) == 0) & (status->float_rounding_mode == float_round_nearest_even));
        }
    } else {
        if ( zSig0 == 0 ) zExp = 0;
    }
    return packFloatx80( zSign, zExp, zSig0 );
    
}

/*----------------------------------------------------------------------------
 | Binary to decimal
 *----------------------------------------------------------------------------*/

floatx80 floatx80_to_floatdecimal(floatx80 a, int32_t *k, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;
    
    flag decSign;
    int32_t decExp, zExp, mExp, xExp;
    uint64_t decSig, zSig0, zSig1, mSig0, mSig1, xSig0, xSig1;
    
    aSign = extractFloatx80Sign(a);
    aExp = extractFloatx80Exp(a);
    aSig = extractFloatx80Frac(a);
    
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        return a;
    }
    
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    int32_t kfactor = *k;
    
    int32_t ilog, len;
    
    flag bSign;
    

    
    floatx80 b;
    floatx80 c;
    floatx80 one = int32_to_floatx80(1);
    floatx80 log2 = packFloatx80(0, 0x3FFD, LIT64(0x9A209A84FBCFF798));
    floatx80 log2up1 = packFloatx80(0, 0x3FFD, LIT64(0x9A209A84FBCFF799));
    
    
    if (aExp < 0) {
        ilog = -4933;
    } else {
        b = packFloatx80(0, 0x3FFF, aSig);
        c = int32_to_floatx80(aExp - 0x3FFF);
        b = floatx80_add(b, c, status);
        b = floatx80_sub(b, one, status);
        bSign = extractFloatx80Sign(b);
        if (bSign) {
            b = floatx80_mul(b, log2up1, status);
        } else {
            b = floatx80_mul(b, log2, status);
        }
        ilog = floatx80_to_int32(b, status);
    }
    
    bool ictr = false;
    
try_again:
    //printf("ILOG = %i\n",ilog);
    
    if (kfactor > 0) {
        if (kfactor > 17) {
            kfactor = 17;
            float_raise(float_flag_invalid, status);
        }
        len = kfactor;
    } else {
        len = ilog + 1 - kfactor;
        if (len > 17) {
            len = 17;
        }
        if (len < 1) {
            len = 1;
        }
    }
    
    //printf("LEN = %i\n",len);
    
    if (kfactor <= 0 && kfactor > ilog) {
        ilog = kfactor;
        //printf("ILOG is kfactor = %i\n",ilog);
    }
    
    flag lambda = 0;
    int iscale = ilog + 1 - len;
    
    if (iscale < 0) {
        lambda = 1;
#if 0
        if (iscale <= -4908) { // do we need this?
            iscale += 24;
            temp_for_a9 = 24;
        }
#endif
        iscale = -iscale;
    }
    
    //printf("ISCALE = %i, LAMDA = %i\n",iscale,lambda);
    
    mExp = 0x4002;
    mSig0 = LIT64(0xA000000000000000);
    mSig1 = 0;
    xExp = 0x3FFF;
    xSig0 = LIT64(0x8000000000000000);
    xSig1 = 0;
    
    while (iscale) {
        if (iscale & 1) {
            mul128by128(&xExp, &xSig0, &xSig1, mExp, mSig0, mSig1);
        }
        mul128by128(&mExp, &mSig0, &mSig1, mExp, mSig0, mSig1);
        iscale >>= 1;
    }
    
    zExp = aExp;
    zSig0 = aSig;
    zSig1 = 0;
    
    if (lambda) {
        mul128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
    } else {
        div128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
    }
    if (zSig1) zSig0 |= 1;
    
    floatx80 z = packFloatx80(aSign, zExp, zSig0);
    z = floatx80_round_to_int(z, status);
    zSig0 = extractFloatx80Frac(z);
    zExp = extractFloatx80Exp(z);
    zSig1 = 0;
    
    if (ictr == false) {
        int lentemp = len - 1;
        
        mExp = 0x4002;
        mSig0 = LIT64(0xA000000000000000);
        mSig1 = 0;
        xExp = 0x3FFF;
        xSig0 = LIT64(0x8000000000000000);
        xSig1 = 0;
        
        while (lentemp) {
            if (lentemp & 1) {
                mul128by128(&xExp, &xSig0, &xSig1, mExp, mSig0, mSig1);
            }
            mul128by128(&mExp, &mSig0, &mSig1, mExp, mSig0, mSig1);
            lentemp >>= 1;
        }
        
        zSig0 = extractFloatx80Frac(z);
        zExp = extractFloatx80Exp(z);
        
        if (zExp < xExp || ((zExp == xExp) && lt128(zSig0, 0, xSig0, xSig1))) { // z < x
            ilog -= 1;
            ictr = true;
            mul128by128(&xExp, &xSig0, &xSig1, 0x4002, LIT64(0xA000000000000000), 0);
            goto try_again;
        }
        
        mul128by128(&xExp, &xSig0, &xSig1, 0x4002, LIT64(0xA000000000000000), 0);
        
        if (zExp > xExp || ((zExp == xExp) && lt128(xSig0, xSig1, zSig0, 0))) { // z > x
            ilog += 1;
            ictr = true;
            goto try_again;
        }
    } else {
        int lentemp = len;

        mExp = 0x4002;
        mSig0 = LIT64(0xA000000000000000);
        mSig1 = 0;
        xExp = 0x3FFF;
        xSig0 = LIT64(0x8000000000000000);
        xSig1 = 0;
        
        while (lentemp) {
            if (lentemp & 1) {
                mul128by128(&xExp, &xSig0, &xSig1, mExp, mSig0, mSig1);
            }
            mul128by128(&mExp, &mSig0, &mSig1, mExp, mSig0, mSig1);
            lentemp >>= 1;
        }

        if (eq128(zSig0, 0, xSig0, xSig1)) {
            div128by128(&zExp, &zSig0, &zSig1, 0x4002, LIT64(0xA000000000000000), 0);
            ilog += 1;
            len += 1;
            mul128by128(&xExp, &xSig0, &xSig1, 0x4002, LIT64(0xA000000000000000), 0);
        }
    }
    
    if (zSig1) zSig0 |= 1;
    
    z = packFloatx80(0, zExp, zSig0);
    
    decSign = aSign;
    decSig = floatx80_to_int64(z, status);
    decExp = (ilog < 0) ? -ilog : ilog;
    if (decExp > 999) {
        float_raise(float_flag_invalid, status);
    }
    if (ilog < 0) decExp |= 0x4000;
    
    *k = len;
    
    return packFloatx80(decSign, decExp, decSig);
#if 0
    printf("abs(Yint) = %s\n",fp_print(&zint));
    
    uae_u64 significand = floatx80_to_int64(zint);
    
    printf("Mantissa = %lli\n",significand);
    
    printf("Exponent = %i\n",ilog);
    
    uae_s32 exp = ilog;
    
    uae_u32 pack_exp = 0;   // packed exponent
    uae_u32 pack_exp4 = 0;
    uae_u32 pack_int = 0;   // packed integer part
    uae_u64 pack_frac = 0;  // packed fraction
    uae_u32 pack_se = 0;    // sign of packed exponent
    uae_u32 pack_sm = 0;    // sign of packed significand
    
    if (exp < 0) {
        exp = -exp;
        pack_se = 1;
    }
    
    uae_u64 digit;
    pack_frac = 0;
    while (len > 0) {
        len--;
        digit = significand % 10;
        significand /= 10;
        if (len == 0) {
            pack_int = digit;
        } else {
            pack_frac |= digit << (64 - len * 4);
        }
    }
    printf("PACKED FRACTION = %02x.%16llx\n",pack_int, pack_frac);
    
    if (exp > 999) {
        digit = exp / 1000;
        exp -= digit * 1000;
        pack_exp4 = digit;
        // OPERR
    }
    digit = exp / 100;
    exp -= digit * 100;
    pack_exp = digit << 8;
    digit = exp / 10;
    exp -= digit * 10;
    pack_exp |= digit << 4;
    pack_exp |= exp;
    
    pack_sm = aSign;
    
    wrd[0] = pack_exp << 16;
    wrd[0] |= pack_exp4 << 12;
    wrd[0] |= pack_int;
    wrd[0] |= pack_se ? 0x40000000 : 0;
    wrd[0] |= pack_sm ? 0x80000000 : 0;
    
    wrd[1] = pack_frac >> 32;
    wrd[2] = pack_frac & 0xffffffff;
    
    printf("PACKED = %08x %08x %08x\n",wrd[0],wrd[1],wrd[2]);
#endif
}
