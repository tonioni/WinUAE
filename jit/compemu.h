
#include "flags_x86.h"

#ifdef CPU_64_BIT
typedef uae_u64 uintptr;
#else
typedef uae_u32 uintptr;
#endif

/* Flags for Bernie during development/debugging. Should go away eventually */
#define DISTRUST_CONSISTENT_MEM 0
#define TAGMASK 0x000fffff
#define TAGSIZE (TAGMASK+1)
#define MAXRUN 1024

extern uae_u8* start_pc_p;
extern uae_u32 start_pc;

#define cacheline(x) (((uae_u32)x)&TAGMASK)

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

#define USE_OPTIMIZER 0
#define USE_LOW_OPTIMIZER 0
#define USE_ALIAS 1
#define USE_F_ALIAS 1
#define USE_SOFT_FLUSH 1
#define USE_OFFSET 1
#define COMP_DEBUG 1

#if COMP_DEBUG
#define Dif(x) if (x)
#else
#define Dif(x) if (0)
#endif

#define SCALE 2
#define MAXCYCLES (1000 * CYCLE_UNIT)
#define MAXREGOPT 65536

#define BYTES_PER_INST 10240  /* paranoid ;-) */
#define LONGEST_68K_INST 16 /* The number of bytes the longest possible
			       68k instruction takes */
#define MAX_CHECKSUM_LEN 2048 /* The maximum size we calculate checksums
				 for. Anything larger will be flushed
				 unconditionally even with SOFT_FLUSH */
#define MAX_HOLD_BI 3  /* One for the current block, and up to two
			  for jump targets */

#define INDIVIDUAL_INST 0
#define FLAG_C    0x0010
#define FLAG_V    0x0008
#define FLAG_Z    0x0004
#define FLAG_N    0x0002
#define FLAG_X    0x0001
#define FLAG_CZNV (FLAG_C | FLAG_Z | FLAG_N | FLAG_V)
#define FLAG_ZNV  (FLAG_Z | FLAG_N | FLAG_V)

#define KILLTHERAT 1  /* Set to 1 to avoid some partial_rat_stalls */

/* Whether to preserve registers across calls to JIT compiled routines */
#if defined X86_ASSEMBLY
#define USE_PUSH_POP 0
#else
#define USE_PUSH_POP 1
#endif

#define N_REGS 8  /* really only 7, but they are numbered 0,1,2,3,5,6,7 */
#define N_FREGS 6 /* That leaves us two positions on the stack to play with */

/* Functions exposed to newcpu, or to what was moved from newcpu.c to
 * compemu_support.c */
extern void init_comp(void);
extern void flush(int save_regs);
extern void small_flush(int save_regs);
extern void set_target(uae_u8* t);
extern void freescratch(void);
extern void build_comp(void);
extern void set_cache_state(int enabled);
extern int get_cache_state(void);
extern uae_u32 get_jitted_size(void);
#ifdef JIT
extern void flush_icache(uaecptr ptr, int n);
extern void flush_icache_hard(uaecptr ptr, int n);
#endif
extern void alloc_cache(void);
extern void compile_block(cpu_history* pc_hist, int blocklen, int totcyles);
extern int check_for_cache_miss(void);


#define scaled_cycles(x) (currprefs.m68k_speed<0?(((x)/SCALE)?(((x)/SCALE<MAXCYCLES?((x)/SCALE):MAXCYCLES)):1):(x))


extern uae_u32 needed_flags;
extern cacheline cache_tags[];
extern uae_u8* comp_pc_p;
extern void* pushall_call_handler;

#define VREGS 32
#define VFREGS 16

#define INMEM 1
#define CLEAN 2
#define DIRTY 3
#define UNDEF 4
#define ISCONST 5

typedef struct {
  uae_u32* mem;
  uae_u32 val;
  uae_u8 is_swapped;
  uae_u8 status;
  uae_u8 realreg;
  uae_u8 realind; /* The index in the holds[] array */
  uae_u8 needflush;
  uae_u8 validsize;
  uae_u8 dirtysize;
  uae_u8 dummy;
} reg_status;

typedef struct {
  uae_u32* mem;
  double val;
  uae_u8 status;
  uae_u8 realreg;
  uae_u8 realind;
  uae_u8 needflush;
} freg_status;

typedef struct {
    uae_u8 use_flags;
    uae_u8 set_flags;
    uae_u8 is_jump;
    uae_u8 is_addx;
    uae_u8 is_const_jump;
} op_properties;

extern op_properties prop[65536];

STATIC_INLINE int end_block(uae_u16 opcode)
{
    return prop[opcode].is_jump ||
	(prop[opcode].is_const_jump && !currprefs.comp_constjump);
}

#define PC_P 16
#define FLAGX 17
#define FLAGTMP 18
#define NEXT_HANDLER 19
#define S1 20
#define S2 21
#define S3 22
#define S4 23
#define S5 24
#define S6 25
#define S7 26
#define S8 27
#define S9 28
#define S10 29
#define S11 30
#define S12 31

#define FP_RESULT 8
#define FS1 9
#define FS2 10
#define FS3 11

typedef struct {
  uae_u32 touched;
  uae_s8 holds[VREGS];
  uae_u8 nholds;
  uae_u8 canbyte;
  uae_u8 canword;
  uae_u8 locked;
} n_status;

typedef struct {
    uae_s8 holds;
    uae_u8 validsize;
    uae_u8 dirtysize;
} n_smallstatus;

typedef struct {
  uae_u32 touched;
  uae_s8 holds[VFREGS];
  uae_u8 nholds;
  uae_u8 locked;
} fn_status;

/* For flag handling */
#define NADA 1
#define TRASH 2
#define VALID 3

/* needflush values */
#define NF_SCRATCH   0
#define NF_TOMEM     1
#define NF_HANDLER   2

typedef struct {
    /* Integer part */
    reg_status state[VREGS];
    n_status   nat[N_REGS];
    uae_u32 flags_on_stack;
    uae_u32 flags_in_flags;
    uae_u32 flags_are_important;
    /* FPU part */
    freg_status fate[VFREGS];
    fn_status   fat[N_FREGS];

    /* x86 FPU part */
    uae_s8 spos[N_FREGS];
    uae_s8 onstack[6];
    uae_s8 tos;
} bigstate;

typedef struct {
    /* Integer part */
    n_smallstatus  nat[N_REGS];
} smallstate;

extern bigstate live;
extern int touchcnt;


#define IMMS uae_s32
#define IMM uae_u32
#define R1  uae_u32
#define R2  uae_u32
#define R4  uae_u32
#define W1  uae_u32
#define W2  uae_u32
#define W4  uae_u32
#define RW1 uae_u32
#define RW2 uae_u32
#define RW4 uae_u32
#define MEMR uae_u32
#define MEMW uae_u32
#define MEMRW uae_u32

#define FW   uae_u32
#define FR   uae_u32
#define FRW  uae_u32

#define MIDFUNC(nargs,func,args) void func args
#define MENDFUNC(nargs,func,args)
#define COMPCALL(func) func

#define LOWFUNC(flags,mem,nargs,func,args) STATIC_INLINE void func args
#define LENDFUNC(flags,mem,nargs,func,args)

#if USE_OPTIMIZER
#define REGALLOC_O 2
#define PEEPHOLE_O 3 /* Has to be >= REGALLOC */
#define DECLARE(func) extern void func; extern void do_##func
#else
#define REGALLOC_O 2000000
#define PEEPHOLE_O 2000000
#define DECLARE(func) extern void func
#endif


/* What we expose to the outside */
DECLARE(bt_l_ri(R4 r, IMM i));
DECLARE(bt_l_rr(R4 r, R4 b));
DECLARE(btc_l_ri(RW4 r, IMM i));
DECLARE(btc_l_rr(RW4 r, R4 b));
DECLARE(bts_l_ri(RW4 r, IMM i));
DECLARE(bts_l_rr(RW4 r, R4 b));
DECLARE(btr_l_ri(RW4 r, IMM i));
DECLARE(btr_l_rr(RW4 r, R4 b));
DECLARE(mov_l_rm(W4 d, IMM s));
DECLARE(call_r(R4 r));
DECLARE(sub_l_mi(IMM d, IMM s));
DECLARE(mov_l_mi(IMM d, IMM s));
DECLARE(mov_w_mi(IMM d, IMM s));
DECLARE(mov_b_mi(IMM d, IMM s));
DECLARE(rol_b_ri(RW1 r, IMM i));
DECLARE(rol_w_ri(RW2 r, IMM i));
DECLARE(rol_l_ri(RW4 r, IMM i));
DECLARE(rol_l_rr(RW4 d, R1 r));
DECLARE(rol_w_rr(RW2 d, R1 r));
DECLARE(rol_b_rr(RW1 d, R1 r));
DECLARE(shll_l_rr(RW4 d, R1 r));
DECLARE(shll_w_rr(RW2 d, R1 r));
DECLARE(shll_b_rr(RW1 d, R1 r));
DECLARE(ror_b_ri(R1 r, IMM i));
DECLARE(ror_w_ri(R2 r, IMM i));
DECLARE(ror_l_ri(R4 r, IMM i));
DECLARE(ror_l_rr(R4 d, R1 r));
DECLARE(ror_w_rr(R2 d, R1 r));
DECLARE(ror_b_rr(R1 d, R1 r));
DECLARE(shrl_l_rr(RW4 d, R1 r));
DECLARE(shrl_w_rr(RW2 d, R1 r));
DECLARE(shrl_b_rr(RW1 d, R1 r));
DECLARE(shra_l_rr(RW4 d, R1 r));
DECLARE(shra_w_rr(RW2 d, R1 r));
DECLARE(shra_b_rr(RW1 d, R1 r));
DECLARE(shll_l_ri(RW4 r, IMM i));
DECLARE(shll_w_ri(RW2 r, IMM i));
DECLARE(shll_b_ri(RW1 r, IMM i));
DECLARE(shrl_l_ri(RW4 r, IMM i));
DECLARE(shrl_w_ri(RW2 r, IMM i));
DECLARE(shrl_b_ri(RW1 r, IMM i));
DECLARE(shra_l_ri(RW4 r, IMM i));
DECLARE(shra_w_ri(RW2 r, IMM i));
DECLARE(shra_b_ri(RW1 r, IMM i));
DECLARE(setcc(W1 d, IMM cc));
DECLARE(setcc_m(IMM d, IMM cc));
DECLARE(cmov_b_rr(RW1 d, R1 s, IMM cc));
DECLARE(cmov_w_rr(RW2 d, R2 s, IMM cc));
DECLARE(cmov_l_rr(RW4 d, R4 s, IMM cc));
DECLARE(cmov_l_rm(RW4 d, IMM s, IMM cc));
DECLARE(bsf_l_rr(W4 d, R4 s));
DECLARE(pop_m(IMM d));
DECLARE(push_m(IMM d));
DECLARE(pop_l(W4 d));
DECLARE(push_l_i(IMM i));
DECLARE(push_l(R4 s));
DECLARE(clear_16(RW4 r));
DECLARE(clear_8(RW4 r));
DECLARE(sign_extend_16_rr(W4 d, R2 s));
DECLARE(sign_extend_8_rr(W4 d, R1 s));
DECLARE(zero_extend_16_rr(W4 d, R2 s));
DECLARE(zero_extend_8_rr(W4 d, R1 s));
DECLARE(imul_64_32(RW4 d, RW4 s));
DECLARE(mul_64_32(RW4 d, RW4 s));
DECLARE(imul_32_32(RW4 d, R4 s));
DECLARE(mov_b_rr(W1 d, R1 s));
DECLARE(mov_w_rr(W2 d, R2 s));
DECLARE(mov_l_rrm_indexed(W4 d, R4 baser, R4 index));
DECLARE(mov_w_rrm_indexed(W2 d, R4 baser, R4 index));
DECLARE(mov_b_rrm_indexed(W1 d, R4 baser, R4 index));
DECLARE(mov_l_mrr_indexed(R4 baser, R4 index, R4 s));
DECLARE(mov_w_mrr_indexed(R4 baser, R4 index, R2 s));
DECLARE(mov_b_mrr_indexed(R4 baser, R4 index, R1 s));
DECLARE(mov_l_rm_indexed(W4 d, IMM base, R4 index));
DECLARE(mov_l_rR(W4 d, R4 s, IMM offset));
DECLARE(mov_w_rR(W2 d, R4 s, IMM offset));
DECLARE(mov_b_rR(W1 d, R4 s, IMM offset));
DECLARE(mov_l_brR(W4 d, R4 s, IMM offset));
DECLARE(mov_w_brR(W2 d, R4 s, IMM offset));
DECLARE(mov_b_brR(W1 d, R4 s, IMM offset));
DECLARE(mov_l_Ri(R4 d, IMM i, IMM offset));
DECLARE(mov_w_Ri(R4 d, IMM i, IMM offset));
DECLARE(mov_b_Ri(R4 d, IMM i, IMM offset));
DECLARE(mov_l_Rr(R4 d, R4 s, IMM offset));
DECLARE(mov_w_Rr(R4 d, R2 s, IMM offset));
DECLARE(mov_b_Rr(R4 d, R1 s, IMM offset));
DECLARE(lea_l_brr(W4 d, R4 s, IMM offset));
DECLARE(lea_l_brr_indexed(W4 d, R4 s, R4 index, IMM factor, IMM offset));
DECLARE(mov_l_bRr(R4 d, R4 s, IMM offset));
DECLARE(mov_w_bRr(R4 d, R2 s, IMM offset));
DECLARE(mov_b_bRr(R4 d, R1 s, IMM offset));
DECLARE(gen_bswap_32(RW4 r));
DECLARE(gen_bswap_16(RW2 r));
DECLARE(mov_l_rr(W4 d, R4 s));
DECLARE(mov_l_mr(IMM d, R4 s));
DECLARE(mov_w_mr(IMM d, R2 s));
DECLARE(mov_w_rm(W2 d, IMM s));
DECLARE(mov_b_mr(IMM d, R1 s));
DECLARE(mov_b_rm(W1 d, IMM s));
DECLARE(mov_l_ri(W4 d, IMM s));
DECLARE(mov_w_ri(W2 d, IMM s));
DECLARE(mov_b_ri(W1 d, IMM s));
DECLARE(add_l_mi(IMM d, IMM s) );
DECLARE(add_w_mi(IMM d, IMM s) );
DECLARE(add_b_mi(IMM d, IMM s) );
DECLARE(test_l_ri(R4 d, IMM i));
DECLARE(test_l_rr(R4 d, R4 s));
DECLARE(test_w_rr(R2 d, R2 s));
DECLARE(test_b_rr(R1 d, R1 s));
DECLARE(and_l_ri(RW4 d, IMM i));
DECLARE(and_l(RW4 d, R4 s));
DECLARE(and_w(RW2 d, R2 s));
DECLARE(and_b(RW1 d, R1 s));
DECLARE(or_l_ri(RW4 d, IMM i));
DECLARE(or_l(RW4 d, R4 s));
DECLARE(or_w(RW2 d, R2 s));
DECLARE(or_b(RW1 d, R1 s));
DECLARE(adc_l(RW4 d, R4 s));
DECLARE(adc_w(RW2 d, R2 s));
DECLARE(adc_b(RW1 d, R1 s));
DECLARE(add_l(RW4 d, R4 s));
DECLARE(add_w(RW2 d, R2 s));
DECLARE(add_b(RW1 d, R1 s));
DECLARE(sub_l_ri(RW4 d, IMM i));
DECLARE(sub_w_ri(RW2 d, IMM i));
DECLARE(sub_b_ri(RW1 d, IMM i));
DECLARE(add_l_ri(RW4 d, IMM i));
DECLARE(add_w_ri(RW2 d, IMM i));
DECLARE(add_b_ri(RW1 d, IMM i));
DECLARE(sbb_l(RW4 d, R4 s));
DECLARE(sbb_w(RW2 d, R2 s));
DECLARE(sbb_b(RW1 d, R1 s));
DECLARE(sub_l(RW4 d, R4 s));
DECLARE(sub_w(RW2 d, R2 s));
DECLARE(sub_b(RW1 d, R1 s));
DECLARE(cmp_l(R4 d, R4 s));
DECLARE(cmp_l_ri(R4 r, IMM i));
DECLARE(cmp_w(R2 d, R2 s));
DECLARE(cmp_b(R1 d, R1 s));
DECLARE(xor_l(RW4 d, R4 s));
DECLARE(xor_w(RW2 d, R2 s));
DECLARE(xor_b(RW1 d, R1 s));
DECLARE(live_flags(void));
DECLARE(dont_care_flags(void));
DECLARE(duplicate_carry(void));
DECLARE(restore_carry(void));
DECLARE(start_needflags(void));
DECLARE(end_needflags(void));
DECLARE(make_flags_live(void));
DECLARE(call_r_11(R4 r, W4 out1, R4 in1, IMM osize, IMM isize));
DECLARE(call_r_02(R4 r, R4 in1, R4 in2, IMM isize1, IMM isize2));
DECLARE(readmem_new(R4 address, W4 dest, IMM offset, IMM size, W4 tmp));
DECLARE(writemem_new(R4 address, R4 source, IMM offset, IMM size, W4 tmp));
DECLARE(forget_about(W4 r));
DECLARE(nop(void));

DECLARE(f_forget_about(FW r));
DECLARE(fmov_pi(FW r));
DECLARE(fmov_log10_2(FW r));
DECLARE(fmov_log2_e(FW r));
DECLARE(fmov_loge_2(FW r));
DECLARE(fmov_1(FW r));
DECLARE(fmov_0(FW r));
DECLARE(fmov_rm(FW r, MEMR m));
DECLARE(fmov_mr(MEMW m, FR r));
DECLARE(fmovi_rm(FW r, MEMR m));
DECLARE(fmovi_mrb(MEMW m, FR r, double *bounds));
DECLARE(fmovs_rm(FW r, MEMR m));
DECLARE(fmovs_mr(MEMW m, FR r));
DECLARE(fcuts_r(FRW r));
DECLARE(fcut_r(FRW r));
DECLARE(fmov_ext_mr(MEMW m, FR r));
DECLARE(fmov_ext_rm(FW r, MEMR m));
DECLARE(fmov_rr(FW d, FR s));
DECLARE(fldcw_m_indexed(R4 index, IMM base));
DECLARE(ftst_r(FR r));
DECLARE(dont_care_fflags(void));
DECLARE(fsqrt_rr(FW d, FR s));
DECLARE(fabs_rr(FW d, FR s));
DECLARE(frndint_rr(FW d, FR s));
DECLARE(fgetexp_rr(FW d, FR s));
DECLARE(fgetman_rr(FW d, FR s));
DECLARE(fsin_rr(FW d, FR s));
DECLARE(fcos_rr(FW d, FR s));
DECLARE(ftan_rr(FW d, FR s));
DECLARE(fsincos_rr(FW d, FW c, FR s));
DECLARE(fscale_rr(FRW d, FR s));
DECLARE(ftwotox_rr(FW d, FR s));
DECLARE(fetox_rr(FW d, FR s));
DECLARE(fetoxM1_rr(FW d, FR s));
DECLARE(ftentox_rr(FW d, FR s));
DECLARE(flog2_rr(FW d, FR s));
DECLARE(flogN_rr(FW d, FR s));
DECLARE(flogNP1_rr(FW d, FR s));
DECLARE(flog10_rr(FW d, FR s));
DECLARE(fasin_rr(FW d, FR s));
DECLARE(facos_rr(FW d, FR s));
DECLARE(fatan_rr(FW d, FR s));
DECLARE(fatanh_rr(FW d, FR s));
DECLARE(fsinh_rr(FW d, FR s));
DECLARE(fcosh_rr(FW d, FR s));
DECLARE(ftanh_rr(FW d, FR s));
DECLARE(fneg_rr(FW d, FR s));
DECLARE(fadd_rr(FRW d, FR s));
DECLARE(fsub_rr(FRW d, FR s));
DECLARE(fmul_rr(FRW d, FR s));
DECLARE(frem_rr(FRW d, FR s));
DECLARE(frem1_rr(FRW d, FR s));
DECLARE(fdiv_rr(FRW d, FR s));
DECLARE(fcmp_rr(FR d, FR s));
DECLARE(fflags_into_flags(W2 tmp));

extern int failure;
#define FAIL(x) do { failure|=x; } while (0)

/* Convenience functions exposed to gencomp */
extern uae_u32 m68k_pc_offset;
extern void readbyte(int address, int dest, int tmp);
extern void readword(int address, int dest, int tmp);
extern void readlong(int address, int dest, int tmp);
extern void writebyte(int address, int source, int tmp);
extern void writeword(int address, int source, int tmp);
extern void writelong(int address, int source, int tmp);
extern void writeword_clobber(int address, int source, int tmp);
extern void writelong_clobber(int address, int source, int tmp);
extern void get_n_addr(int address, int dest, int tmp);
extern void get_n_addr_jmp(int address, int dest, int tmp);
extern void calc_disp_ea_020(int base, uae_u32 dp, int target, int tmp);
extern int kill_rodent(int r);
extern void sync_m68k_pc(void);
extern uae_u32 get_const(int r);
extern int  is_const(int r);
extern void register_branch(uae_u32 not_taken, uae_u32 taken, uae_u8 cond);
extern void empty_optimizer(void);

#define comp_get_ibyte(o) do_get_mem_byte((uae_u8 *)(comp_pc_p + (o) + 1))
#define comp_get_iword(o) do_get_mem_word((uae_u16 *)(comp_pc_p + (o)))
#define comp_get_ilong(o) do_get_mem_long((uae_u32 *)(comp_pc_p + (o)))

struct blockinfo_t;

typedef struct dep_t {
  uae_u32*            jmp_off;
  struct blockinfo_t* target;
  struct dep_t**      prev_p;
  struct dep_t*       next;
} dependency;

typedef struct blockinfo_t {
    uae_s32 count;
    cpuop_func* direct_handler_to_use;
    cpuop_func* handler_to_use;
    /* The direct handler does not check for the correct address */

    cpuop_func* handler;
    cpuop_func* direct_handler;

    cpuop_func* direct_pen;
    cpuop_func* direct_pcc;

    uae_u8* nexthandler;
    uae_u8* pc_p;

    uae_u32 c1;
    uae_u32 c2;
    uae_u32 len;

    struct blockinfo_t* next_same_cl;
    struct blockinfo_t** prev_same_cl_p;
    struct blockinfo_t* next;
    struct blockinfo_t** prev_p;

    uae_u32 min_pcp;
    uae_u8 optlevel;
    uae_u8 needed_flags;
    uae_u8 status;
    uae_u8 havestate;

    dependency  dep[2];  /* Holds things we depend on */
    dependency* deplist; /* List of things that depend on this */
    smallstate  env;
} blockinfo;

#define BI_NEW 0
#define BI_COUNTING 1
#define BI_TARGETTED 2

typedef struct {
    uae_u8 type;
    uae_u8 reg;
    uae_u32 next;
} regacc;

void execute_normal(void);
void exec_nostats(void);
void do_nothing(void);

void comp_fdbcc_opp (uae_u32 opcode, uae_u16 extra);
void comp_fscc_opp (uae_u32 opcode, uae_u16 extra);
void comp_ftrapcc_opp (uae_u32 opcode, uaecptr oldpc);
void comp_fbcc_opp (uae_u32 opcode);
void comp_fsave_opp (uae_u32 opcode);
void comp_frestore_opp (uae_u32 opcode);
void comp_fpp_opp (uae_u32 opcode, uae_u16 extra);
