
#include "sysconfig.h"
#include "sysdeps.h"
#include "m68k.h"

/*
 * Test CCR condition
 */
int cctrue (int cc)
{
    uae_u32 cznv = regflags.cznv;

    switch (cc) {
	case 0:  return 1;											/*					T  */
	case 1:  return 0;											/*					F  */
	case 2:  return (cznv & (FLAGVAL_C | FLAGVAL_Z)) == 0;		/* !CFLG && !ZFLG	HI */
	case 3:  return (cznv & (FLAGVAL_C | FLAGVAL_Z)) != 0;		/*  CFLG || ZFLG	LS */
	case 4:  return (cznv & FLAGVAL_C) == 0;					/* !CFLG			CC */
	case 5:  return (cznv & FLAGVAL_C) != 0;					/*  CFLG			CS */
	case 6:  return (cznv & FLAGVAL_Z) == 0;					/* !ZFLG			NE */
	case 7:  return (cznv & FLAGVAL_Z) != 0;					/*  ZFLG			EQ */
	case 8:  return (cznv & FLAGVAL_V) == 0;					/* !VFLG			VC */
	case 9:  return (cznv & FLAGVAL_V) != 0;					/*  VFLG			VS */
	case 10: return (cznv & FLAGVAL_N) == 0;					/* !NFLG			PL */
	case 11: return (cznv & FLAGVAL_N) != 0;					/*  NFLG			MI */

	case 12: /*  NFLG == VFLG		GE */
		return ((cznv >> FLAGBIT_N) & 1) == ((cznv >> FLAGBIT_V) & 1);
	case 13: /*  NFLG != VFLG		LT */
		return ((cznv >> FLAGBIT_N) & 1) != ((cznv >> FLAGBIT_V) & 1);
	case 14: /* !GET_ZFLG && (GET_NFLG == GET_VFLG);  GT */
		return !(cznv & FLAGVAL_Z) && (((cznv >> FLAGBIT_N) & 1) == ((cznv >> FLAGBIT_V) & 1));
	case 15: /* GET_ZFLG || (GET_NFLG != GET_VFLG);   LE */
		return (cznv & FLAGVAL_Z) || (((cznv >> FLAGBIT_N) & 1) != ((cznv >> FLAGBIT_V) & 1));
	}
    return 0;
}
