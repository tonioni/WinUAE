 /*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68881 emulation
  *
  * Copyright 1996 Herman ten Brugge
  */

#define __USE_ISOC9X  /* We might be able to pick up a NaN */
#include <math.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "ersatz.h"
#include "md-fpp.h"
#include "savestate.h"

#if 1

STATIC_INLINE int isinrom (void)
{
    return (munge24 (m68k_getpc ()) & 0xFFF80000) == 0xF80000;
}

#define	DEBUG_FPP	0

#define FFLAG_Z   0x4000
#define FFLAG_N   0x0100
#define FFLAG_NAN 0x0400 

#define MAKE_FPSR(r)  regs.fp_result=(r)

static __inline__ void native_set_fpucw (uae_u32 m68k_cw)
{
#if USE_X86_FPUCW
  int mprec=(m68k_cw>>6)&3;
  int mround=(m68k_cw>>4)&3;
  uae_u16 x;

  int iprec=0;
  int iround=0;

  switch(mprec) {
  case 0: /* extend */ iprec=3; break;
  case 1: /* single */ iprec=0; break;
  case 2: /* double */ iprec=2; break;
  case 3: /* undef  */ iprec=3; break;
  }

  switch(mround) {
  case 0: /* nearest */ iround=0; break;
  case 1: /* zero    */ iround=3; break;
  case 2: /* down    */ iround=1; break;
  case 3: /* up      */ iround=2; break;
  }
   
  x=0x107f + (iprec<<8) + (iround<<10);
#if defined(X86_MSVC_ASSEMBLY)
  __asm {
  fldcw x
  }
#elif defined(X86_ASSEMBLY)
  __asm__ ("fldcw %0" : : "m" (*&x));
#endif
#endif
}

#if defined(uae_s64) /* Close enough for government work? */
typedef uae_s64 tointtype;
#else
typedef uae_s32 tointtype;
#endif

static __inline__ tointtype toint(fptype src)
{
    switch ((regs.fpcr >> 4) & 0x3) {
    case 0:
#if USE_X86_FPUCW
	return (tointtype) (src);
#else
	return (tointtype) (src + 0.5);
#endif
    case 1:
	return (tointtype) src;
    case 2:
	return (tointtype)floor (src);
    case 3:
	return (tointtype)ceil (src);
    }
    return (tointtype)src; /* Should never be reached */
}

uae_u32 get_fpsr (void) 
{
    uae_u32 answer = regs.fpsr & 0x00ffffff;
#ifdef HAVE_ISNAN
    if (isnan (regs.fp_result))
	answer |= 0x01000000;
    else
#endif
    {
	if (regs.fp_result == 0)
	    answer |= 0x04000000;
	else if (regs.fp_result < 0)
	    answer |= 0x08000000;
#ifdef HAVE_ISINF
	if (isinf (regs.fp_result))
	    answer |= 0x02000000;
#endif
    }
    return answer;
}

STATIC_INLINE void set_fpsr (uae_u32 x) 
{
    regs.fpsr = x;

    if (x & 0x01000000) {
#ifdef NAN
	regs.fp_result = NAN;
#else
	regs.fp_result = pow (1e100, 10) - pow(1e100, 10);  /* Any better way? */
#endif
    }
    else if (x & 0x04000000)
	regs.fp_result = 0;
    else if (x & 0x08000000)
	regs.fp_result = -1;
    else
	regs.fp_result = 1;
}


/* single   : S  8*E 23*F */
/* double   : S 11*E 52*F */
/* extended : S 15*E 64*F */
/* E = 0 & F = 0 -> 0 */
/* E = MAX & F = 0 -> Infin */
/* E = MAX & F # 0 -> NotANumber */
/* E = biased by 127 (single) ,1023 (double) ,16383 (extended) */

STATIC_INLINE fptype to_pack (uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    fptype d;
    char *cp;
    char str[100];

    cp = str;
    if (wrd1 & 0x80000000)
	*cp++ = '-';
    *cp++ = (wrd1 & 0xf) + '0';
    *cp++ = '.';
    *cp++ = ((wrd2 >> 28) & 0xf) + '0';
    *cp++ = ((wrd2 >> 24) & 0xf) + '0';
    *cp++ = ((wrd2 >> 20) & 0xf) + '0';
    *cp++ = ((wrd2 >> 16) & 0xf) + '0';
    *cp++ = ((wrd2 >> 12) & 0xf) + '0';
    *cp++ = ((wrd2 >> 8) & 0xf) + '0';
    *cp++ = ((wrd2 >> 4) & 0xf) + '0';
    *cp++ = ((wrd2 >> 0) & 0xf) + '0';
    *cp++ = ((wrd3 >> 28) & 0xf) + '0';
    *cp++ = ((wrd3 >> 24) & 0xf) + '0';
    *cp++ = ((wrd3 >> 20) & 0xf) + '0';
    *cp++ = ((wrd3 >> 16) & 0xf) + '0';
    *cp++ = ((wrd3 >> 12) & 0xf) + '0';
    *cp++ = ((wrd3 >> 8) & 0xf) + '0';
    *cp++ = ((wrd3 >> 4) & 0xf) + '0';
    *cp++ = ((wrd3 >> 0) & 0xf) + '0';
    *cp++ = 'E';
    if (wrd1 & 0x40000000)
	*cp++ = '-';
    *cp++ = ((wrd1 >> 24) & 0xf) + '0';
    *cp++ = ((wrd1 >> 20) & 0xf) + '0';
    *cp++ = ((wrd1 >> 16) & 0xf) + '0';
    *cp = 0;
    sscanf (str, "%le", &d);
    return d;
}

STATIC_INLINE void from_pack (fptype src, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
    int i;
    int t;
    char *cp;
    char str[100];

    sprintf (str, "%.16e", src);
    cp = str;
    *wrd1 = *wrd2 = *wrd3 = 0;
    if (*cp == '-') {
	cp++;
	*wrd1 = 0x80000000;
    }
    if (*cp == '+')
	cp++;
    *wrd1 |= (*cp++ - '0');
    if (*cp == '.')
	cp++;
    for (i = 0; i < 8; i++) {
	*wrd2 <<= 4;
	if (*cp >= '0' && *cp <= '9')
	    *wrd2 |= *cp++ - '0';
    }
    for (i = 0; i < 8; i++) {
	*wrd3 <<= 4;
	if (*cp >= '0' && *cp <= '9')
	    *wrd3 |= *cp++ - '0';
    }
    if (*cp == 'e' || *cp == 'E') {
	cp++;
	if (*cp == '-') {
	    cp++;
	    *wrd1 |= 0x40000000;
	}
	if (*cp == '+')
	    cp++;
	t = 0;
	for (i = 0; i < 3; i++) {
	    if (*cp >= '0' && *cp <= '9')
		t = (t << 4) | (*cp++ - '0');
	}
	*wrd1 |= t << 16;
    }
}

STATIC_INLINE int get_fp_value (uae_u32 opcode, uae_u16 extra, fptype *src)
{
    uaecptr tmppc;
    uae_u16 tmp;
    int size;
    int mode;
    int reg;
    uae_u32 ad = 0;
    static int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
    static int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

    if ((extra & 0x4000) == 0) {
	*src = regs.fp[(extra >> 10) & 7];
	return 1;
    }
    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    size = (extra >> 10) & 7;
    switch (mode) {
    case 0:
	switch (size) {
	case 6:
	    *src = (fptype) (uae_s8) m68k_dreg (regs, reg);
	    break;
	case 4:
	    *src = (fptype) (uae_s16) m68k_dreg (regs, reg);
	    break;
	case 0:
	    *src = (fptype) (uae_s32) m68k_dreg (regs, reg);
	    break;
	case 1:
	    *src = to_single (m68k_dreg (regs, reg));
	    break;
	default:
	    return 0;
	}
	return 1;
    case 1:
	return 0;
    case 2:
	ad = m68k_areg (regs, reg);
	break;
    case 3:
	ad = m68k_areg (regs, reg);
	m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
	break;
    case 4:
	m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
	ad = m68k_areg (regs, reg);
	break;
    case 5:
	ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) next_iword ();
	break;
    case 6:
	ad = get_disp_ea_020 (m68k_areg (regs, reg), next_iword ());
	break;
    case 7:
	switch (reg) {
	case 0:
	    ad = (uae_s32) (uae_s16) next_iword ();
	    break;
	case 1:
	    ad = next_ilong ();
	    break;
	case 2:
	    ad = m68k_getpc ();
	    ad += (uae_s32) (uae_s16) next_iword ();
	    break;
	case 3:
	    tmppc = m68k_getpc ();
	    tmp = next_iword ();
	    ad = get_disp_ea_020 (tmppc, tmp);
	    break;
	case 4:
	    ad = m68k_getpc ();
	    m68k_setpc (ad + sz2[size]);
	    if (size == 6)
		ad++;
	    break;
	default:
	    return 0;
	}
    }
    switch (size) {
    case 0:
	*src = (fptype) (uae_s32) get_long (ad);
	break;
    case 1:
	*src = to_single (get_long (ad));
	break;
    case 2:{
	    uae_u32 wrd1, wrd2, wrd3;
	    wrd1 = get_long (ad);
	    ad += 4;
	    wrd2 = get_long (ad);
	    ad += 4;
	    wrd3 = get_long (ad);
	    *src = to_exten (wrd1, wrd2, wrd3);
	}
	break;
    case 3:{
	    uae_u32 wrd1, wrd2, wrd3;
	    wrd1 = get_long (ad);
	    ad += 4;
	    wrd2 = get_long (ad);
	    ad += 4;
	    wrd3 = get_long (ad);
	    *src = to_pack (wrd1, wrd2, wrd3);
	}
	break;
    case 4:
	*src = (fptype) (uae_s16) get_word (ad);
	break;
    case 5:{
	    uae_u32 wrd1, wrd2;
	    wrd1 = get_long (ad);
	    ad += 4;
	    wrd2 = get_long (ad);
	    *src = to_double (wrd1, wrd2);
	}
	break;
    case 6:
	*src = (fptype) (uae_s8) get_byte (ad);
	break;
    default:
	return 0;
    }
    return 1;
}

STATIC_INLINE int put_fp_value (fptype value, uae_u32 opcode, uae_u16 extra)
{
    uae_u16 tmp;
    uaecptr tmppc;
    int size;
    int mode;
    int reg;
    uae_u32 ad;
    static int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
    static int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("PUTFP: %f %04.4X %04.4X\n", value, opcode, extra);
#endif
    if ((extra & 0x4000) == 0) {
	regs.fp[(extra >> 10) & 7] = value;
	return 1;
    }
    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    size = (extra >> 10) & 7;
    ad = -1;
    
    switch (mode) {
    case 0:
	switch (size) {
	case 6:
	    m68k_dreg (regs, reg) = (uae_u32)(((toint(value) & 0xff)
				     | (m68k_dreg (regs, reg) & ~0xff)));
	    break;
	case 4:
	    m68k_dreg (regs, reg) = (uae_u32)(((toint(value) & 0xffff)
				     | (m68k_dreg (regs, reg) & ~0xffff)));
	    break;
	case 0:
	    m68k_dreg (regs, reg) = (uae_u32)toint(value);
	    break;
	case 1:
	    m68k_dreg (regs, reg) = from_single (value);
	    break;
	default:
	    return 0;
	}
	return 1;
    case 1:
	return 0;
    case 2:
	ad = m68k_areg (regs, reg);
	break;
    case 3:
	ad = m68k_areg (regs, reg);
	m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
	break;
    case 4:
	m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
	ad = m68k_areg (regs, reg);
	break;
    case 5:
	ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) next_iword ();
	break;
    case 6:
	ad = get_disp_ea_020 (m68k_areg (regs, reg), next_iword ());
	break;
    case 7:
	switch (reg) {
	case 0:
	    ad = (uae_s32) (uae_s16) next_iword ();
	    break;
	case 1:
	    ad = next_ilong ();
	    break;
	case 2:
	    ad = m68k_getpc ();
	    ad += (uae_s32) (uae_s16) next_iword ();
	    break;
	case 3:
	    tmppc = m68k_getpc ();
	    tmp = next_iword ();
	    ad = get_disp_ea_020 (tmppc, tmp);
	    break;
	case 4:
	    ad = m68k_getpc ();
	    m68k_setpc (ad + sz2[size]);
	    break;
	default:
	    return 0;
	}
    }
    switch (size) {
    case 0:
	put_long (ad, (uae_u32)toint(value));
	break;
    case 1:
	put_long (ad, from_single (value));
	break;
    case 2:
	{
	    uae_u32 wrd1, wrd2, wrd3;
	    from_exten (value, &wrd1, &wrd2, &wrd3);
	    put_long (ad, wrd1);
	    ad += 4;
	    put_long (ad, wrd2);
	    ad += 4;
	    put_long (ad, wrd3);
	}
	break;
    case 3:
	{
	    uae_u32 wrd1, wrd2, wrd3;
	    from_pack (value, &wrd1, &wrd2, &wrd3);
	    put_long (ad, wrd1);
	    ad += 4;
	    put_long (ad, wrd2);
	    ad += 4;
	    put_long (ad, wrd3);
	}
	break;
    case 4:
	put_word (ad, (uae_s16) toint(value));
	break;
    case 5:{
	    uae_u32 wrd1, wrd2;
	    from_double (value, &wrd1, &wrd2);
	    put_long (ad, wrd1);
	    ad += 4;
	    put_long (ad, wrd2);
	}
	break;
    case 6:
	put_byte (ad, (uae_s8)toint(value));
	break;
    default:
	return 0;
    }
    return 1;
}

STATIC_INLINE int get_fp_ad (uae_u32 opcode, uae_u32 * ad)
{
    uae_u16 tmp;
    uaecptr tmppc;
    int mode;
    int reg;

    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    switch (mode) {
    case 0:
    case 1:
	return 0;
    case 2:
	*ad = m68k_areg (regs, reg);
	break;
    case 3:
	*ad = m68k_areg (regs, reg);
	break;
    case 4:
	*ad = m68k_areg (regs, reg);
	break;
    case 5:
	*ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) next_iword ();
	break;
    case 6:
	*ad = get_disp_ea_020 (m68k_areg (regs, reg), next_iword ());
	break;
    case 7:
	switch (reg) {
	case 0:
	    *ad = (uae_s32) (uae_s16) next_iword ();
	    break;
	case 1:
	    *ad = next_ilong ();
	    break;
	case 2:
	    *ad = m68k_getpc ();
	    *ad += (uae_s32) (uae_s16) next_iword ();
	    break;
	case 3:
	    tmppc = m68k_getpc ();
	    tmp = next_iword ();
	    *ad = get_disp_ea_020 (tmppc, tmp);
	    break;
	default:
	    return 0;
	}
    }
    return 1;
}

STATIC_INLINE int fpp_cond (uae_u32 opcode, int contition)
{
    int N = (regs.fp_result<0);
    int Z = (regs.fp_result==0);
    /* int I = (regs.fpsr & 0x2000000) != 0; */
    int NotANumber = 0;

#ifdef HAVE_ISNAN
    NotANumber = isnan (regs.fp_result);
#endif

    if (NotANumber)
	N=Z=0;

    switch (contition) {
    case 0x00:
	return 0;
    case 0x01:
	return Z;
    case 0x02:
	return !(NotANumber || Z || N);
    case 0x03:
	return Z || !(NotANumber || N);
    case 0x04:
	return N && !(NotANumber || Z);
    case 0x05:
	return Z || (N && !NotANumber);
    case 0x06:
	return !(NotANumber || Z);
    case 0x07:
	return !NotANumber;
    case 0x08:
	return NotANumber;
    case 0x09:
	return NotANumber || Z;
    case 0x0a:
	return NotANumber || !(N || Z);
    case 0x0b:
	return NotANumber || Z || !N;
    case 0x0c:
	return NotANumber || (N && !Z);
    case 0x0d:
	return NotANumber || Z || N;
    case 0x0e:
	return !Z;
    case 0x0f:
	return 1;
    case 0x10:
	return 0;
    case 0x11:
	return Z;
    case 0x12:
	return !(NotANumber || Z || N);
    case 0x13:
	return Z || !(NotANumber || N);
    case 0x14:
	return N && !(NotANumber || Z);
    case 0x15:
	return Z || (N && !NotANumber);
    case 0x16:
	return !(NotANumber || Z);
    case 0x17:
	return !NotANumber;
    case 0x18:
	return NotANumber;
    case 0x19:
	return NotANumber || Z;
    case 0x1a:
	return NotANumber || !(N || Z);
    case 0x1b:
	return NotANumber || Z || !N;
    case 0x1c:
#if 0
        return NotANumber || (Z && N); /* This is wrong, compare 0x0c */
#else
        return NotANumber || (N && !Z);  
#endif
    case 0x1d:
	return NotANumber || Z || N;
    case 0x1e:
	return !Z;
    case 0x1f:
	return 1;
    }
    return -1;
}

void fdbcc_opp (uae_u32 opcode, uae_u16 extra)
{
    uaecptr pc = (uae_u32) m68k_getpc ();
    uae_s32 disp = (uae_s32) (uae_s16) next_iword ();
    int cc;

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("fdbcc_opp at %08lx\n", m68k_getpc ());
#endif
    cc = fpp_cond (opcode, extra & 0x3f);
    if (cc == -1) {
	m68k_setpc (pc - 4);
	op_illg (opcode);
    } else if (!cc) {
	int reg = opcode & 0x7;

	m68k_dreg (regs, reg) = ((m68k_dreg (regs, reg) & ~0xffff)
				 | ((m68k_dreg (regs, reg) - 1) & 0xffff));
	if ((m68k_dreg (regs, reg) & 0xffff) == 0xffff)
	    m68k_setpc (pc + disp);
    }
}

void fscc_opp (uae_u32 opcode, uae_u16 extra)
{
    uae_u32 ad;
    int cc;

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("fscc_opp at %08lx\n", m68k_getpc ());
#endif
    cc = fpp_cond (opcode, extra & 0x3f);
    if (cc == -1) {
	m68k_setpc (m68k_getpc () - 4);
	op_illg (opcode);
    } else if ((opcode & 0x38) == 0) {
	m68k_dreg (regs, opcode & 7) = (m68k_dreg (regs, opcode & 7) & ~0xff) | (cc ? 0xff : 0x00);
    } else {
	if (get_fp_ad (opcode, &ad) == 0) {
	    m68k_setpc (m68k_getpc () - 4);
	    op_illg (opcode);
	} else
	    put_byte (ad, cc ? 0xff : 0x00);
    }
}

void ftrapcc_opp (uae_u32 opcode, uaecptr oldpc)
{
    int cc;

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("ftrapcc_opp at %08lx\n", m68k_getpc ());
#endif
    cc = fpp_cond (opcode, opcode & 0x3f);
    if (cc == -1) {
	m68k_setpc (oldpc);
	op_illg (opcode);
    }
    if (cc)
	Exception (7, oldpc - 2);
}

void fbcc_opp (uae_u32 opcode, uaecptr pc, uae_u32 extra)
{
    int cc;

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("fbcc_opp at %08lx\n", m68k_getpc ());
#endif
    cc = fpp_cond (opcode, opcode & 0x3f);
    if (cc == -1) {
	m68k_setpc (pc);
	op_illg (opcode);
    } else if (cc) {
	if ((opcode & 0x40) == 0)
	    extra = (uae_s32) (uae_s16) extra;
	m68k_setpc (pc + extra);
    }
}

void fsave_opp (uae_u32 opcode)
{
    uae_u32 ad;
    int incr = (opcode & 0x38) == 0x20 ? -1 : 1;
    int fpu_version = 0x18; /* 68881 */
//    int fpu_version = 0x38; /* 68882 */
    int i;


#if DEBUG_FPP
    if (!isinrom ())
	write_log ("fsave_opp at %08lx\n", m68k_getpc ());
#endif
    if (get_fp_ad (opcode, &ad) == 0) {
	m68k_setpc (m68k_getpc () - 2);
	op_illg (opcode);
	return;
    }

    if (currprefs.cpu_level >= 4) {
	/* 4 byte 68040 IDLE frame.  */
	if (incr < 0) {
	    ad -= 4;
	    put_long (ad, 0x41000000);
	} else {
	    put_long (ad, 0x41000000);
	    ad += 4;
	}
    } else {
	if (incr < 0) {
	    ad -= 4;
	    put_long (ad, 0x70000000);
	    for (i = 0; i < 5; i++) {
		ad -= 4;
		put_long (ad, 0x00000000);
	    }
	    ad -= 4;
	    put_long (ad, 0x1f000000 | (fpu_version << 16));
	} else {
	    put_long (ad, 0x1f000000 | (fpu_version << 16));
	    ad += 4;
	    for (i = 0; i < 5; i++) {
		put_long (ad, 0x00000000);
		ad += 4;
	    }
	    put_long (ad, 0x70000000);
	    ad += 4;
	}
    }
    if ((opcode & 0x38) == 0x18)
	m68k_areg (regs, opcode & 7) = ad;
    if ((opcode & 0x38) == 0x20)
	m68k_areg (regs, opcode & 7) = ad;
}

void frestore_opp (uae_u32 opcode)
{
    uae_u32 ad;
    uae_u32 d;
    int incr = (opcode & 0x38) == 0x20 ? -1 : 1;

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("frestore_opp at %08lx\n", m68k_getpc ());
#endif
    if (get_fp_ad (opcode, &ad) == 0) {
	m68k_setpc (m68k_getpc () - 2);
	op_illg (opcode);
	return;
    }
    if (currprefs.cpu_level >= 4) {
	/* 68040 */
	if (incr < 0) {
	    /* @@@ This may be wrong.  */
	    ad -= 4;
	    d = get_long (ad);
	    if ((d & 0xff000000) != 0) { /* Not a NULL frame? */
		if ((d & 0x00ff0000) == 0) { /* IDLE */
		} else if ((d & 0x00ff0000) == 0x00300000) { /* UNIMP */
		    ad -= 44;
		} else if ((d & 0x00ff0000) == 0x00600000) { /* BUSY */
		    ad -= 92;
		}
	    }
	} else {
	    d = get_long (ad);
	    ad += 4;
	    if ((d & 0xff000000) != 0) { /* Not a NULL frame? */
		if ((d & 0x00ff0000) == 0) { /* IDLE */
		} else if ((d & 0x00ff0000) == 0x00300000) { /* UNIMP */
		    ad += 44;
		} else if ((d & 0x00ff0000) == 0x00600000) { /* BUSY */
		    ad += 92;
		}
	    }
	}
    } else {
	if (incr < 0) {
	    ad -= 4;
	    d = get_long (ad);
	    if ((d & 0xff000000) != 0) {
		if ((d & 0x00ff0000) == 0x00180000)
		    ad -= 6 * 4;
		else if ((d & 0x00ff0000) == 0x00380000)
		    ad -= 14 * 4;
		else if ((d & 0x00ff0000) == 0x00b40000)
		    ad -= 45 * 4;
	    }
	} else {
	    d = get_long (ad);
	    ad += 4;
	    if ((d & 0xff000000) != 0) {
		if ((d & 0x00ff0000) == 0x00180000)
		    ad += 6 * 4;
		else if ((d & 0x00ff0000) == 0x00380000)
		    ad += 14 * 4;
		else if ((d & 0x00ff0000) == 0x00b40000)
		    ad += 45 * 4;
	    }
	}
    }
    if ((opcode & 0x38) == 0x18)
	m68k_areg (regs, opcode & 7) = ad;
    if ((opcode & 0x38) == 0x20)
	m68k_areg (regs, opcode & 7) = ad;
}

static void fround (int reg)
{
    regs.fp[reg] = (float)regs.fp[reg];
}

void fpp_opp (uae_u32 opcode, uae_u16 extra)
{
    int reg;
    fptype src;

#if DEBUG_FPP
    if (!isinrom ())
	write_log ("FPP %04lx %04x at %08lx\n", opcode & 0xffff, extra, m68k_getpc () - 4);
#endif
    switch ((extra >> 13) & 0x7) {
    case 3:
	if (put_fp_value (regs.fp[(extra >> 7) & 7], opcode, extra) == 0) {
	    m68k_setpc (m68k_getpc () - 4);
	    op_illg (opcode);
	}
	return;
    case 4:
    case 5:
	if ((opcode & 0x38) == 0) {
	    if (extra & 0x2000) {
		if (extra & 0x1000)
		    m68k_dreg (regs, opcode & 7) = regs.fpcr;
		if (extra & 0x0800)
		    m68k_dreg (regs, opcode & 7) = get_fpsr ();
		if (extra & 0x0400)
		    m68k_dreg (regs, opcode & 7) = regs.fpiar;
	    } else {
		if (extra & 0x1000) {
		    regs.fpcr = m68k_dreg (regs, opcode & 7);
		    native_set_fpucw (regs.fpcr);
		}
		if (extra & 0x0800)
		    set_fpsr (m68k_dreg (regs, opcode & 7));
		if (extra & 0x0400)
		    regs.fpiar = m68k_dreg (regs, opcode & 7);
	    }
	} else if ((opcode & 0x38) == 0x08) {
	    if (extra & 0x2000) {
		if (extra & 0x1000)
		    m68k_areg (regs, opcode & 7) = regs.fpcr;
		if (extra & 0x0800)
		    m68k_areg (regs, opcode & 7) = get_fpsr ();
		if (extra & 0x0400)
		    m68k_areg (regs, opcode & 7) = regs.fpiar;
	    } else {
		if (extra & 0x1000) {
		    regs.fpcr = m68k_areg (regs, opcode & 7);
		    native_set_fpucw (regs.fpcr);
		}
		if (extra & 0x0800)
		    set_fpsr (m68k_areg (regs, opcode & 7));
		if (extra & 0x0400)
		    regs.fpiar = m68k_areg (regs, opcode & 7);
	    }
	} else if ((opcode & 0x3f) == 0x3c) {
	    if ((extra & 0x2000) == 0) {
		if (extra & 0x1000) {
		    regs.fpcr = next_ilong ();
		    native_set_fpucw (regs.fpcr);
		}
		if (extra & 0x0800)
		    set_fpsr (next_ilong ());
		if (extra & 0x0400)
		    regs.fpiar = next_ilong ();
	    }
	} else if (extra & 0x2000) {
	    /* FMOVEM FPP->memory */
	    uae_u32 ad;
	    int incr = 0;

	    if (get_fp_ad (opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
		return;
	    }
	    if ((opcode & 0x38) == 0x20) {
		if (extra & 0x1000)
		    incr += 4;
		if (extra & 0x0800)
		    incr += 4;
		if (extra & 0x0400)
		    incr += 4;
	    }
	    ad -= incr;
	    if (extra & 0x1000) {
		put_long (ad, regs.fpcr);
		ad += 4;
	    }
	    if (extra & 0x0800) {
		put_long (ad, get_fpsr());
		ad += 4;
	    }
	    if (extra & 0x0400) {
		put_long (ad, regs.fpiar);
		ad += 4;
	    }
	    ad -= incr;
	    if ((opcode & 0x38) == 0x18)
		m68k_areg (regs, opcode & 7) = ad;
	    if ((opcode & 0x38) == 0x20)
		m68k_areg (regs, opcode & 7) = ad;
	} else {
	    /* FMOVEM memory->FPP */
	    uae_u32 ad;

	    if (get_fp_ad (opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
		return;
	    }
	    ad = (opcode & 0x38) == 0x20 ? ad - 12 : ad;
	    if (extra & 0x1000) {
		regs.fpcr = get_long (ad);
		native_set_fpucw(regs.fpcr);
		ad += 4;
	    }
	    if (extra & 0x0800) {
		set_fpsr(get_long (ad));
		ad += 4;
	    }
	    if (extra & 0x0400) {
		regs.fpiar = get_long (ad);
		ad += 4;
	    }
	    if ((opcode & 0x38) == 0x18)
		m68k_areg (regs, opcode & 7) = ad;
	    if ((opcode & 0x38) == 0x20)
		m68k_areg (regs, opcode & 7) = ad - 12;
	}
	return;
    case 6:
    case 7:
    {
	uae_u32 ad, list = 0;
	int incr = 0;
	if (extra & 0x2000) {
	    /* FMOVEM FPP->memory */
	    if (get_fp_ad (opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
		return;
	    }
	    switch ((extra >> 11) & 3) {
	    case 0:	/* static pred */
		list = extra & 0xff;
		incr = -1;
		break;
	    case 1:	/* dynamic pred */
		list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
		incr = -1;
		break;
	    case 2:	/* static postinc */
		list = extra & 0xff;
		incr = 1;
		break;
	    case 3:	/* dynamic postinc */
		list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
		incr = 1;
		break;
	    }
	    while (list) {
		uae_u32 wrd1, wrd2, wrd3;
		if (incr < 0) {
		    from_exten (regs.fp[fpp_movem_index2[list]], &wrd1, &wrd2, &wrd3);
		    ad -= 4;
		    put_long (ad, wrd3);
		    ad -= 4;
		    put_long (ad, wrd2);
		    ad -= 4;
		    put_long (ad, wrd1);
		} else {
		    from_exten (regs.fp[fpp_movem_index1[list]], &wrd1, &wrd2, &wrd3);
		    put_long (ad, wrd1);
		    ad += 4;
		    put_long (ad, wrd2);
		    ad += 4;
		    put_long (ad, wrd3);
		    ad += 4;
		}
		list = fpp_movem_next[list];
	    }
	    if ((opcode & 0x38) == 0x18)
		m68k_areg (regs, opcode & 7) = ad;
	    if ((opcode & 0x38) == 0x20)
		m68k_areg (regs, opcode & 7) = ad;
	} else {
	    /* FMOVEM memory->FPP */
	    if (get_fp_ad (opcode, &ad) == 0) {
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
		return;
	    }
	    switch ((extra >> 11) & 3) {
	    case 0:	/* static pred */
		list = extra & 0xff;
		incr = -1;
		break;
	    case 1:	/* dynamic pred */
		list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
		incr = -1;
		break;
	    case 2:	/* static postinc */
		list = extra & 0xff;
		incr = 1;
		break;
	    case 3:	/* dynamic postinc */
		list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
		incr = 1;
		break;
	    }
	    while (list) {
		uae_u32 wrd1, wrd2, wrd3;
		if (incr < 0) {
		    ad -= 4;
		    wrd3 = get_long (ad);
		    ad -= 4;
		    wrd2 = get_long (ad);
		    ad -= 4;
		    wrd1 = get_long (ad);
		    regs.fp[fpp_movem_index2[list]] = to_exten (wrd1, wrd2, wrd3);
		} else {
		    wrd1 = get_long (ad);
		    ad += 4;
		    wrd2 = get_long (ad);
		    ad += 4;
		    wrd3 = get_long (ad);
		    ad += 4;
		    regs.fp[fpp_movem_index1[list]] = to_exten (wrd1, wrd2, wrd3);
		}
		list = fpp_movem_next[list];
	    }
	    if ((opcode & 0x38) == 0x18)
		m68k_areg (regs, opcode & 7) = ad;
	    if ((opcode & 0x38) == 0x20)
		m68k_areg (regs, opcode & 7) = ad;
	}
    }
    return;
    case 0:
    case 2: /* Extremely common */
	reg = (extra >> 7) & 7;
	if ((extra & 0xfc00) == 0x5c00) {
	    switch (extra & 0x7f) {
	    case 0x00:
		regs.fp[reg] = 4.0 * atan (1.0);
		break;
	    case 0x0b:
		regs.fp[reg] = log10 (2.0);
		break;
	    case 0x0c:
		regs.fp[reg] = exp (1.0);
		break;
	    case 0x0d:
		regs.fp[reg] = log (exp (1.0)) / log (2.0);
		break;
	    case 0x0e:
		regs.fp[reg] = log (exp (1.0)) / log (10.0);
		break;
	    case 0x0f:
		regs.fp[reg] = 0.0;
		break;
	    case 0x30:
		regs.fp[reg] = log (2.0);
		break;
	    case 0x31:
		regs.fp[reg] = log (10.0);
		break;
	    case 0x32:
		regs.fp[reg] = 1.0e0;
		break;
	    case 0x33:
		regs.fp[reg] = 1.0e1;
		break;
	    case 0x34:
		regs.fp[reg] = 1.0e2;
		break;
	    case 0x35:
		regs.fp[reg] = 1.0e4;
		break;
	    case 0x36:
		regs.fp[reg] = 1.0e8;
		break;
	    case 0x37:
		regs.fp[reg] = 1.0e16;
		break;
	    case 0x38:
		regs.fp[reg] = 1.0e32;
		break;
	    case 0x39:
		regs.fp[reg] = 1.0e64;
		break;
	    case 0x3a:
		regs.fp[reg] = 1.0e128;
		break;
	    case 0x3b:
		regs.fp[reg] = 1.0e256;
		break;
#if 0
	    case 0x3c:
		regs.fp[reg] = 1.0e512;
		break;
	    case 0x3d:
		regs.fp[reg] = 1.0e1024;
		break;
	    case 0x3e:
		regs.fp[reg] = 1.0e2048;
		break;
	    case 0x3f:
		regs.fp[reg] = 1.0e4096;
		break;
#endif
	    default:
		m68k_setpc (m68k_getpc () - 4);
		op_illg (opcode);
		break;
	    }
	    return;
	}
	if (get_fp_value (opcode, extra, &src) == 0) {
	    m68k_setpc (m68k_getpc () - 4);
	    op_illg (opcode);
	    return;
	}
	switch (extra & 0x7f) {
	case 0x00:		/* FMOVE */
	case 0x40:  /* Explicit rounding. This is just a quick fix. Same
		     * for all other cases that have three choices */
	case 0x44:   
	    regs.fp[reg] = src;
	    /* Brian King was here.  <ea> to register needs FPSR updated.
	     * See page 3-73 in Motorola 68K programmers reference manual.
	     * %%%FPU */
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x01:		/* FINT */
	    /* need to take the current rounding mode into account */
 	    regs.fp[reg] = (fptype)toint(src);
	    break;
	case 0x02:		/* FSINH */
	    regs.fp[reg] = sinh (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x03:		/* FINTRZ */
	    regs.fp[reg] = (int) src;
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x04:		/* FSQRT */
	case 0x41:
	case 0x45:
	    regs.fp[reg] = sqrt (src);
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x06:		/* FLOGNP1 */
	    regs.fp[reg] = log (src + 1.0);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x08:		/* FETOXM1 */
	    regs.fp[reg] = exp (src) - 1.0;
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x09:		/* FTANH */
	    regs.fp[reg] = tanh (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x0a:		/* FATAN */
	    regs.fp[reg] = atan (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x0c:		/* FASIN */
	    regs.fp[reg] = asin (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x0d:		/* FATANH */
#if 1				/* The BeBox doesn't have atanh, and it isn't in the HPUX libm either */
	    regs.fp[reg] = log ((1 + src) / (1 - src)) / 2;
#else
	    regs.fp[reg] = atanh (src);
#endif
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x0e:		/* FSIN */
	    regs.fp[reg] = sin (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x0f:		/* FTAN */
	    regs.fp[reg] = tan (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x10:		/* FETOX */
	    regs.fp[reg] = exp (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x11:		/* FTWOTOX */
	    regs.fp[reg] = pow (2.0, src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x12:		/* FTENTOX */
	    regs.fp[reg] = pow (10.0, src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x14:		/* FLOGN */
	    regs.fp[reg] = log (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x15:		/* FLOG10 */
	    regs.fp[reg] = log10 (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x16:		/* FLOG2 */
	    regs.fp[reg] = log (src) / log (2.0);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x18:		/* FABS */
	case 0x58:
	case 0x5c:
	    regs.fp[reg] = src < 0 ? -src : src;
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x19:		/* FCOSH */
	    regs.fp[reg] = cosh (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x1a:		/* FNEG */
	case 0x5a:
	case 0x5e:
	    regs.fp[reg] = -src;
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x1c:		/* FACOS */
	    regs.fp[reg] = acos (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x1d:		/* FCOS */
	    regs.fp[reg] = cos (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x1e:		/* FGETEXP */
	{
	    int expon;
	    frexp (src, &expon);
	    regs.fp[reg] = (double) (expon - 1);
	    MAKE_FPSR (regs.fp[reg]);
	}
	break;
	case 0x1f:		/* FGETMAN */
	{
	    int expon;
	    regs.fp[reg] = frexp (src, &expon) * 2.0;
	    MAKE_FPSR (regs.fp[reg]);
	}
	break;
	case 0x20:		/* FDIV */
	case 0x60:
	case 0x64:
	    regs.fp[reg] /= src;
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x21:		/* FMOD */
	    regs.fp[reg] = regs.fp[reg] - (double) ((int) (regs.fp[reg] / src)) * src;
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x22:		/* FADD */
	case 0x62:
	case 0x66:
	    regs.fp[reg] += src;
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x23:		/* FMUL */
	case 0x63:
	case 0x67:
	    regs.fp[reg] *= src;
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x24:		/* FSGLDIV */
	    regs.fp[reg] /= src;
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x25:		/* FREM */
	    regs.fp[reg] = regs.fp[reg] - (double) ((int) (regs.fp[reg] / src + 0.5)) * src;
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x26:		/* FSCALE */
	    regs.fp[reg] *= exp (log (2.0) * src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x27:		/* FSGLMUL */
	    regs.fp[reg] *= src;
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x28:		/* FSUB */
	case 0x68:
	case 0x6c:
	    regs.fp[reg] -= src;
	    if ((extra & 0x44) == 0x40)
		fround (reg);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x30:		/* FSINCOS */
	case 0x31:
	case 0x32:
	case 0x33:
	case 0x34:
	case 0x35:
	case 0x36:
	case 0x37:
	    regs.fp[extra & 7] = cos (src);
	    regs.fp[reg] = sin (src);
	    MAKE_FPSR (regs.fp[reg]);
	    break;
	case 0x38:		/* FCMP */
	{
	    fptype tmp = regs.fp[reg] - src;
	    regs.fpsr = 0;
	    MAKE_FPSR (tmp);
	}
	break;
	case 0x3a:		/* FTST */
	    regs.fpsr = 0;
	    MAKE_FPSR (src);
	    break;
	default:
	    m68k_setpc (m68k_getpc () - 4);
	    op_illg (opcode);
	    break;
	}
	return;
    }
    m68k_setpc (m68k_getpc () - 4);
    op_illg (opcode);
}

#endif

uae_u8 *restore_fpu (uae_u8 *src)
{
    int model, i;

    model = restore_u32();
    restore_u32 ();
    if (currprefs.cpu_level == 2) {
	currprefs.cpu_level++;
        init_m68k_full ();
    }
    changed_prefs.cpu_level = currprefs.cpu_level;
    for (i = 0; i < 8; i++) {
	uae_u32 w1 = restore_u32 ();
	uae_u32 w2 = restore_u32 ();
	uae_u32 w3 = restore_u16 ();
	regs.fp[i] = to_exten (w1, w2, w3);
    }
    regs.fpcr = restore_u32 ();
    regs.fpsr = restore_u32 ();
    regs.fpiar = restore_u32 ();
    return src;
}

uae_u8 *save_fpu (int *len, uae_u8 *dstptr)
{
    uae_u8 *dstbak,*dst;
    int model,i;

    *len = 0;
    switch (currprefs.cpu_level)
    {
	case 3:
	model = 68881;
	break;
	case 4:
	model = 68040;
	break;
	case 6:
	model = 68060;
	break;
	default:
	return 0;
    }
    if (dstptr)
	dstbak = dst = dstptr;
    else
        dstbak = dst = malloc(4+4+8*10+4+4+4);
    save_u32 (model);
    save_u32 (0);
    for (i = 0; i < 8; i++) {
	uae_u32 w1, w2, w3;
	from_exten (regs.fp[i], &w1, &w2, &w3);
	save_u32 (w1);
	save_u32 (w2);
	save_u16 (w3);
    }
    save_u32 (regs.fpcr);
    save_u32 (regs.fpsr);
    save_u32 (regs.fpiar);
    *len = dst - dstbak;
    return dstbak;
}
