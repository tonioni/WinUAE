#ifndef M68K_H
#define M68K_H

 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * MC68000 emulation - machine dependent bits
  *
  * Copyright 1996 Bernd Schmidt
  */

struct flag_struct {
    unsigned int cznv;
    unsigned int x;
};

#define FLAGVAL_Z 0x4000
#define FLAGVAL_N 0x8000

#define SET_ZFLG(y) (regflags.cznv = (regflags.cznv & ~0x4000) | (((y) ? 0x4000 : 0)))
#define SET_CFLG(y) (regflags.cznv = (regflags.cznv & ~0x100) | (((y) ? 0x100 : 0)))
#define SET_VFLG(y) (regflags.cznv = (regflags.cznv & ~0x1) | (((y) ? 1 : 0)))
#define SET_NFLG(y) (regflags.cznv = (regflags.cznv & ~0x8000) | (((y) ? 0x8000 : 0)))
#define SET_XFLG(y) (regflags.x = ((y) ? 1 : 0))

#define GET_ZFLG ((regflags.cznv >> 14) & 1)
#define GET_CFLG ((regflags.cznv >> 8) & 1)
#define GET_VFLG ((regflags.cznv >> 0) & 1)
#define GET_NFLG ((regflags.cznv >> 15) & 1)
#define GET_XFLG (regflags.x & 1)

#define CLEAR_CZNV (regflags.cznv = 0)
#define GET_CZNV (regflags.cznv)
#define IOR_CZNV(X) (regflags.cznv |= (X))
#define SET_CZNV(X) (regflags.cznv = (X))

#define COPY_CARRY (regflags.x = (regflags.cznv) >> 8)

#ifdef X86_ASSEMBLY
extern struct flag_struct regflags __asm__ ("regflags");
#else
extern struct flag_struct regflags;
#endif

static __inline__ int cctrue(int cc)
{
    uae_u32 cznv = regflags.cznv;
    switch(cc){
     case 0: return 1;                       /* T */
     case 1: return 0;                       /* F */
     case 2: return (cznv & 0x4100) == 0; /* !GET_CFLG && !GET_ZFLG;  HI */
     case 3: return (cznv & 0x4100) != 0; /* GET_CFLG || GET_ZFLG;    LS */
     case 4: return (cznv & 0x100) == 0;  /* !GET_CFLG;               CC */
     case 5: return (cznv & 0x100) != 0;  /* GET_CFLG;                CS */
     case 6: return (cznv & 0x4000) == 0; /* !GET_ZFLG;               NE */
     case 7: return (cznv & 0x4000) != 0; /* GET_ZFLG;                EQ */
     case 8: return (cznv & 0x01) == 0;   /* !GET_VFLG;               VC */
     case 9: return (cznv & 0x01) != 0;   /* GET_VFLG;                VS */
     case 10:return (cznv & 0x8000) == 0; /* !GET_NFLG;               PL */
     case 11:return (cznv & 0x8000) != 0; /* GET_NFLG;                MI */
     case 12:return (((cznv << 15) ^ cznv) & 0x8000) == 0; /* GET_NFLG == GET_VFLG;             GE */
     case 13:return (((cznv << 15) ^ cznv) & 0x8000) != 0;/* GET_NFLG != GET_VFLG;             LT */
     case 14:
	cznv &= 0xc001;
	return (((cznv << 15) ^ cznv) & 0xc000) == 0; /* !GET_ZFLG && (GET_NFLG == GET_VFLG);  GT */
     case 15:
	cznv &= 0xc001;
	return (((cznv << 15) ^ cznv) & 0xc000) != 0; /* GET_ZFLG || (GET_NFLG != GET_VFLG);   LE */
    }
    abort();
    return 0;
}

#ifdef X86_ASSEMBLY

#define optflag_testl(v) \
  __asm__ __volatile__ ("andl %0,%0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:: "r" (v) : "%eax","cc","memory")
#define optflag_testw(v) \
  __asm__ __volatile__ ("andw %w0,%w0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:: "r" (v) : "%eax","cc","memory")

#define optflag_testb(v) \
  __asm__ __volatile__ ("andb %b0,%b0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:: "q" (v) : "%eax","cc","memory")

#define optflag_addl(v, s, d) do { \
  __asm__ __volatile__ ("addl %k1,%k0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:"=r" (v) : "rmi" (s), "0" (d) : "%eax","cc","memory"); \
    COPY_CARRY; \
    } while (0)
#define optflag_addw(v, s, d) do { \
  __asm__ __volatile__ ("addw %w1,%w0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			: "=r" (v) : "rmi" (s), "0" (d) : "%eax","cc","memory"); \
    COPY_CARRY; \
    } while (0)

#define optflag_addb(v, s, d) do { \
  __asm__ __volatile__ ("addb %b1,%b0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:"=q" (v) : "qmi" (s), "0" (d) : "%eax","cc","memory"); \
    COPY_CARRY; \
    } while (0)

#define optflag_subl(v, s, d) do { \
  __asm__ __volatile__ ("subl %k1,%k0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			: "=r" (v) : "rmi" (s), "0" (d) : "%eax","cc","memory"); \
    COPY_CARRY; \
    } while (0)

#define optflag_subw(v, s, d) do { \
  __asm__ __volatile__ ("subw %w1,%w0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			: "=r" (v) : "rmi" (s), "0" (d) : "%eax","cc","memory"); \
    COPY_CARRY; \
    } while (0)

#define optflag_subb(v, s, d) do { \
   __asm__ __volatile__ ("subb %b1,%b0\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			: "=q" (v) : "qmi" (s), "0" (d) : "%eax","cc","memory"); \
    COPY_CARRY; \
    } while (0)

#define optflag_cmpl(s, d) \
  __asm__ __volatile__ ("cmpl %k0,%k1\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:: "rmi" (s), "r" (d) : "%eax","cc","memory")

#define optflag_cmpw(s, d) \
  __asm__ __volatile__ ("cmpw %w0,%w1\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:: "rmi" (s), "r" (d) : "%eax","cc","memory");

#define optflag_cmpb(s, d) \
  __asm__ __volatile__ ("cmpb %b0,%b1\n\t" \
			"lahf\n\t" \
			"seto %%al\n\t" \
                        "movb %%al,regflags\n\t" \
			"movb %%ah,regflags+1\n\t" \
			:: "qmi" (s), "q" (d) : "%eax","cc","memory")

#endif

#endif