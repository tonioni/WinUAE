#ifndef WINUAE_OD_UNIX_MACHDEP_M68K_H
#define WINUAE_OD_UNIX_MACHDEP_M68K_H

#include <stdlib.h>
#include "uae/types.h"

struct flag_struct {
#if defined(CPU_AARCH64)
    union {
        uae_u64 cznv;
        uae_u64 nzcv;
    };
    uae_u64 x;
#else
    uae_u32 cznv;
    uae_u32 x;
#endif
};

extern struct flag_struct regflags;

#if defined(CPU_AARCH64)
#define FLAGBIT_N 31
#define FLAGBIT_Z 30
#define FLAGBIT_C 29
#define FLAGBIT_V 28
#define FLAGBIT_X 0

#define FLAGVAL_N (1UL << FLAGBIT_N)
#define FLAGVAL_Z (1UL << FLAGBIT_Z)
#define FLAGVAL_C (1UL << FLAGBIT_C)
#define FLAGVAL_V (1UL << FLAGBIT_V)
#define FLAGVAL_X (1UL << FLAGBIT_X)

#define SET_ZFLG(y) (regflags.nzcv = (regflags.nzcv & ~FLAGVAL_Z) | (((y) ? 1UL : 0UL) << FLAGBIT_Z))
#define SET_CFLG(y) (regflags.nzcv = (regflags.nzcv & ~FLAGVAL_C) | (((y) ? 1UL : 0UL) << FLAGBIT_C))
#define SET_VFLG(y) (regflags.nzcv = (regflags.nzcv & ~FLAGVAL_V) | (((y) ? 1UL : 0UL) << FLAGBIT_V))
#define SET_NFLG(y) (regflags.nzcv = (regflags.nzcv & ~FLAGVAL_N) | (((y) ? 1UL : 0UL) << FLAGBIT_N))
#define SET_XFLG(y) (regflags.x = ((y) ? 1UL : 0UL))

#define GET_ZFLG() ((regflags.nzcv >> FLAGBIT_Z) & 1)
#define GET_CFLG() ((regflags.nzcv >> FLAGBIT_C) & 1)
#define GET_VFLG() ((regflags.nzcv >> FLAGBIT_V) & 1)
#define GET_NFLG() ((regflags.nzcv >> FLAGBIT_N) & 1)
#define GET_XFLG() (regflags.x & 1)

#define CLEAR_CZNV() (regflags.nzcv = 0)
#define GET_CZNV() (regflags.nzcv)
#define IOR_CZNV(X) (regflags.nzcv |= (X))
#define SET_CZNV(X) (regflags.nzcv = (X))
#define COPY_CARRY() (regflags.x = (regflags.nzcv >> FLAGBIT_C) & 1)
#else
#define FLAGBIT_N 15
#define FLAGBIT_Z 14
#define FLAGBIT_C 8
#define FLAGBIT_V 0
#define FLAGBIT_X 8

#define FLAGVAL_N (1 << FLAGBIT_N)
#define FLAGVAL_Z (1 << FLAGBIT_Z)
#define FLAGVAL_C (1 << FLAGBIT_C)
#define FLAGVAL_V (1 << FLAGBIT_V)
#define FLAGVAL_X (1 << FLAGBIT_X)

#define SET_ZFLG(y) (regflags.cznv = (regflags.cznv & ~FLAGVAL_Z) | (((y) ? 1 : 0) << FLAGBIT_Z))
#define SET_CFLG(y) (regflags.cznv = (regflags.cznv & ~FLAGVAL_C) | (((y) ? 1 : 0) << FLAGBIT_C))
#define SET_VFLG(y) (regflags.cznv = (regflags.cznv & ~FLAGVAL_V) | (((y) ? 1 : 0) << FLAGBIT_V))
#define SET_NFLG(y) (regflags.cznv = (regflags.cznv & ~FLAGVAL_N) | (((y) ? 1 : 0) << FLAGBIT_N))
#define SET_XFLG(y) (regflags.x = ((y) ? 1 : 0) << FLAGBIT_X)

#define GET_ZFLG() ((regflags.cznv >> FLAGBIT_Z) & 1)
#define GET_CFLG() ((regflags.cznv >> FLAGBIT_C) & 1)
#define GET_VFLG() ((regflags.cznv >> FLAGBIT_V) & 1)
#define GET_NFLG() ((regflags.cznv >> FLAGBIT_N) & 1)
#define GET_XFLG() ((regflags.x >> FLAGBIT_X) & 1)

#define CLEAR_CZNV() (regflags.cznv = 0)
#define GET_CZNV() (regflags.cznv)
#define IOR_CZNV(X) (regflags.cznv |= (X))
#define SET_CZNV(X) (regflags.cznv = (X))
#define COPY_CARRY() (regflags.x = regflags.cznv)
#endif

static inline int cctrue(int cc)
{
    uae_u64 cznv = GET_CZNV();
    switch (cc) {
    case 0: return 1;
    case 1: return 0;
    case 2: return (cznv & (FLAGVAL_C | FLAGVAL_Z)) == 0;
    case 3: return (cznv & (FLAGVAL_C | FLAGVAL_Z)) != 0;
    case 4: return (cznv & FLAGVAL_C) == 0;
    case 5: return (cznv & FLAGVAL_C) != 0;
    case 6: return (cznv & FLAGVAL_Z) == 0;
    case 7: return (cznv & FLAGVAL_Z) != 0;
    case 8: return (cznv & FLAGVAL_V) == 0;
    case 9: return (cznv & FLAGVAL_V) != 0;
    case 10: return (cznv & FLAGVAL_N) == 0;
    case 11: return (cznv & FLAGVAL_N) != 0;
    case 12: return (((cznv << (FLAGBIT_N - FLAGBIT_V)) ^ cznv) & FLAGVAL_N) == 0;
    case 13: return (((cznv << (FLAGBIT_N - FLAGBIT_V)) ^ cznv) & FLAGVAL_N) != 0;
    case 14:
        cznv &= (FLAGVAL_N | FLAGVAL_Z | FLAGVAL_V);
        return (((cznv << (FLAGBIT_N - FLAGBIT_V)) ^ cznv) & (FLAGVAL_N | FLAGVAL_Z)) == 0;
    case 15:
        cznv &= (FLAGVAL_N | FLAGVAL_Z | FLAGVAL_V);
        return (((cznv << (FLAGBIT_N - FLAGBIT_V)) ^ cznv) & (FLAGVAL_N | FLAGVAL_Z)) != 0;
    }
    abort();
}

#endif /* WINUAE_OD_UNIX_MACHDEP_M68K_H */
