 /*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68000 emulation
  *
  * Copyright 1995 Bernd Schmidt
  */

#include "readcpu.h"
#include "machdep/m68k.h"
#include "events.h"

#ifndef SET_CFLG

#define SET_CFLG(x) (CFLG = (x))
#define SET_NFLG(x) (NFLG = (x))
#define SET_VFLG(x) (VFLG = (x))
#define SET_ZFLG(x) (ZFLG = (x))
#define SET_XFLG(x) (XFLG = (x))

#define GET_CFLG CFLG
#define GET_NFLG NFLG
#define GET_VFLG VFLG
#define GET_ZFLG ZFLG
#define GET_XFLG XFLG

#define CLEAR_CZNV do { \
 SET_CFLG (0); \
 SET_ZFLG (0); \
 SET_NFLG (0); \
 SET_VFLG (0); \
} while (0)

#define COPY_CARRY (SET_XFLG (GET_CFLG))
#endif

extern int areg_byteinc[];
extern int imm8_table[];

extern int movem_index1[256];
extern int movem_index2[256];
extern int movem_next[256];

#ifdef FPUEMU
extern int fpp_movem_index1[256];
extern int fpp_movem_index2[256];
extern int fpp_movem_next[256];
#endif

typedef unsigned long cpuop_func (uae_u32) REGPARAM;
typedef void cpuop_func_ce (uae_u32) REGPARAM;

struct cputbl {
    cpuop_func *handler;
#ifndef JIT
    uae_u16 opcode;
#else
    uae_u32 opcode;
    int specific;
#endif
};

extern unsigned long op_illg (uae_u32) REGPARAM;

typedef char flagtype;

#ifdef FPUEMU
/* You can set this to long double to be more accurate. However, the
   resulting alignment issues will cost a lot of performance in some
   apps */
#define USE_LONG_DOUBLE 0

#if USE_LONG_DOUBLE
typedef long double fptype;
#define LDPTR tbyte ptr
#else
typedef double fptype;
#define LDPTR qword ptr
#endif
#endif

extern struct regstruct
{
    uae_u32 regs[16];
    uaecptr usp,isp,msp;
    uae_u16 sr;
    flagtype t1;
    flagtype t0;
    flagtype s;
    flagtype m;
    flagtype x;
    flagtype stopped;
    int intmask;

    uae_u32 pc;
    uae_u8 *pc_p;
    uae_u8 *pc_oldp;

    uae_u32 vbr,sfc,dfc;

#ifdef FPUEMU
    fptype fp[8];
    fptype fp_result;

    uae_u32 fpcr,fpsr,fpiar;
    uae_u32 fpsr_highbyte;
#endif

    uae_u32 spcflags;
    uae_u32 kick_mask;
    uae_u32 address_space_mask;

    uae_u16 irc, ir;

    uae_u8 panic;
    uae_u32 panic_pc, panic_addr;

} regs, lastint_regs;

typedef struct {
  uae_u16* location;
  uae_u8  cycles;
  uae_u8  specmem;
  uae_u8  dummy2;
  uae_u8  dummy3;
} cpu_history;

struct blockinfo_t;

typedef union {
    cpuop_func* handler;
    struct blockinfo_t* bi;
} cacheline;

extern signed long pissoff;

STATIC_INLINE uae_u32 munge24(uae_u32 x)
{
    return x & regs.address_space_mask;
}

extern unsigned long irqcycles[15];
extern int irqdelay[15];

STATIC_INLINE void set_special (uae_u32 x)
{
    regs.spcflags |= x;
    cycles_do_special();
}

STATIC_INLINE void unset_special (uae_u32 x)
{
    regs.spcflags &= ~x;
}

#define m68k_dreg(r,num) ((r).regs[(num)])
#define m68k_areg(r,num) (((r).regs + 8)[(num)])

STATIC_INLINE void m68k_setpc (uaecptr newpc)
{
    regs.pc_p = regs.pc_oldp = get_real_address (newpc);
    regs.pc = newpc;
}

STATIC_INLINE uaecptr m68k_getpc (void)
{
    return regs.pc + (uaecptr)(((char *)regs.pc_p - (char *)regs.pc_oldp));
}

STATIC_INLINE uaecptr m68k_getpc_p (uae_u8 *p)
{
    return regs.pc + (uaecptr)(((char *)p - (char *)regs.pc_oldp));
}

#define get_ibyte(o) do_get_mem_byte((uae_u8 *)(regs.pc_p + (o) + 1))
#define get_iword(o) do_get_mem_word((uae_u16 *)(regs.pc_p + (o)))
#define get_ilong(o) do_get_mem_long((uae_u32 *)(regs.pc_p + (o)))

#define m68k_incpc(o) (regs.pc_p += (o))

/* These are only used by the 68020/68881 code, and therefore don't
 * need to handle prefetch.  */
STATIC_INLINE uae_u32 next_ibyte (void)
{
    uae_u32 r = get_ibyte (0);
    m68k_incpc (2);
    return r;
}

STATIC_INLINE uae_u32 next_iword (void)
{
    uae_u32 r = get_iword (0);
    m68k_incpc (2);
    return r;
}

STATIC_INLINE uae_u32 next_ilong (void)
{
    uae_u32 r = get_ilong (0);
    m68k_incpc (4);
    return r;
}

STATIC_INLINE void m68k_do_rts(void)
{
    m68k_setpc(get_long(m68k_areg(regs, 7)));
    m68k_areg(regs, 7) += 4;
}

STATIC_INLINE void m68k_do_bsr(uaecptr oldpc, uae_s32 offset)
{
    m68k_areg(regs, 7) -= 4;
    put_long(m68k_areg(regs, 7), oldpc);
    m68k_incpc(offset);
}

STATIC_INLINE void m68k_do_jsr(uaecptr oldpc, uaecptr dest)
{
    m68k_areg(regs, 7) -= 4;
    put_long(m68k_areg(regs, 7), oldpc);
    m68k_setpc(dest);
}

#define m68k_setpc_fast m68k_setpc
#define m68k_setpc_bcc  m68k_setpc
#define m68k_setpc_rte  m68k_setpc

STATIC_INLINE void m68k_setstopped (int stop)
{
    regs.stopped = stop;
    /* A traced STOP instruction drops through immediately without
       actually stopping.  */
    if (stop && (regs.spcflags & SPCFLAG_DOTRACE) == 0)
	set_special (SPCFLAG_STOP);
}

extern uae_u32 get_disp_ea_020 (uae_u32 base, uae_u32 dp);
extern uae_u32 get_disp_ea_000 (uae_u32 base, uae_u32 dp);

extern uae_s32 ShowEA (void *, uae_u16 opcode, int reg, amodes mode, wordsizes size, char *buf);

extern void MakeSR (void);
extern void MakeFromSR (void);
extern void Exception (int, uaecptr);
extern void Interrupt (int nr);
extern void dump_counts (void);
extern int m68k_move2c (int, uae_u32 *);
extern int m68k_movec2 (int, uae_u32 *);
extern void m68k_divl (uae_u32, uae_u32, uae_u16, uaecptr);
extern void m68k_mull (uae_u32, uae_u32, uae_u16);
extern void init_m68k (void);
extern void init_m68k_full (void);
extern void m68k_go (int);
extern void m68k_dumpstate (void *, uaecptr *);
extern void m68k_disasm (void *, uaecptr, uaecptr *, int);
extern void sm68k_disasm(char *, char *, uaecptr addr, uaecptr *nextpc);
extern void m68k_reset (void);
extern int getDivu68kCycles(uae_u32 dividend, uae_u16 divisor);
extern int getDivs68kCycles(uae_s32 dividend, uae_s16 divisor);

extern void mmu_op (uae_u32, uae_u16);

extern void fpp_opp (uae_u32, uae_u16);
extern void fdbcc_opp (uae_u32, uae_u16);
extern void fscc_opp (uae_u32, uae_u16);
extern void ftrapcc_opp (uae_u32,uaecptr);
extern void fbcc_opp (uae_u32, uaecptr, uae_u32);
extern void fsave_opp (uae_u32);
extern void frestore_opp (uae_u32);

extern void exception3 (uae_u32 opcode, uaecptr addr, uaecptr fault);
extern void exception3i (uae_u32 opcode, uaecptr addr, uaecptr fault);
extern void exception2 (uaecptr addr, uaecptr fault);
extern void cpureset (void);

extern void fill_prefetch_slow (void);

#define CPU_OP_NAME(a) op ## a

/* 68040 */
extern struct cputbl op_smalltbl_0_ff[];
/* 68020 + 68881 */
extern struct cputbl op_smalltbl_1_ff[];
/* 68020 */
extern struct cputbl op_smalltbl_2_ff[];
/* 68010 */
extern struct cputbl op_smalltbl_3_ff[];
/* 68000 */
extern struct cputbl op_smalltbl_4_ff[];
/* 68000 slow but compatible.  */
extern struct cputbl op_smalltbl_5_ff[];
/* 68000 slow but compatible and cycle-exact.  */
extern struct cputbl op_smalltbl_6_ff[];

extern cpuop_func *cpufunctbl[65536] ASM_SYM_FOR_FUNC ("cpufunctbl");


/* Flags for Bernie during development/debugging. Should go away eventually */
#define DISTRUST_CONSISTENT_MEM 0
#define TAGMASK 0x000fffff
#define TAGSIZE (TAGMASK+1)
#define MAXRUN 1024

extern uae_u8* start_pc_p;
extern uae_u32 start_pc;

#define cacheline(x) (((uae_u32)x)&TAGMASK)

void newcpu_showstate(void);

#ifdef JIT
extern void flush_icache(int n);
extern void compemu_reset(void);
#else
#define flush_icache(X) do {} while (0)
#endif
