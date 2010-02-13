#if USE_LOW_OPTIMIZER
/* Welcome to the magical world of cpp ;-) */

/* This was broken by the advent of FPU emulation. It also didn't
   provide any useful speedup while it worked. *Sigh* Someone fix my
   software, please ;-) */

#define MAXLOPTINST 100

#define LDECISION currprefs.comp_lowopt

#define lopt_op0(dummy) lopt_store_op0(
#define lopt_op1(a1) lopt_store_op1(LOPT_##a1,
#define lopt_op2(a1,a2) lopt_store_op2(LOPT_##a1,LOPT_##a2,
#define lopt_op3(a1,a2,a3) lopt_store_op3(LOPT_##a1,LOPT_##a2,LOPT_##a3,
#define lopt_op4(a1,a2,a3,a4) lopt_store_op4(LOPT_##a1,LOPT_##a2,LOPT_##a3,LOPT_##a4,
#define lopt_op5(a1,a2,a3,a4,a5) lopt_store_op5(LOPT_##a1,LOPT_##a2,LOPT_##a3,LOPT_##a4,LOPT_##a5,

#define ldirect0(dummy) ()
#define ldirect1(a1) (LDIR_##a1)
#define ldirect2(a1,a2) (LDIR_##a1,LDIR_##a2)
#define ldirect3(a1,a2,a3) (LDIR_##a1,LDIR_##a2,LDIR_##a3)
#define ldirect4(a1,a2,a3,a4) (LDIR_##a1,LDIR_##a2,LDIR_##a3,LDIR_##a4)
#define ldirect5(a1,a2,a3,a4,a5) (LDIR_##a1,LDIR_##a2,LDIR_##a3,LDIR_##a4,LDIR_##a5)

#define NONE 0
#define READ 1
#define WRITE 2
#define RMW (READ|WRITE)

#define SIZE1 4
#define SIZE2 8
#define SIZE4 12
#define FLOAT 16
#define SIZEMASK 12

#define LIMM NONE
#define LR1  (READ | SIZE1)
#define LR2  (READ | SIZE2)
#define LR4  (READ | SIZE4)
#define LW1  (WRITE | SIZE1)
#define LW2  (WRITE | SIZE2)
#define LW4  (WRITE | SIZE4)
#define LRW1 (RMW | SIZE1)
#define LRW2 (RMW | SIZE2)
#define LRW4 (RMW | SIZE4)
#define LFW  (READ | FLOAT)
#define LFR  (WRITE | FLOAT)
#define LFRW (RMW | FLOAT)
#define LMEMR NONE
#define LMEMW NONE
#define LMEMRW NONE

#define LOPT_IMM LIMM,
#define LOPT_R1  LR1 ,
#define LOPT_R2  LR2 ,
#define LOPT_R4  LR4 ,
#define LOPT_W1  LW1 ,
#define LOPT_W2  LW2 ,
#define LOPT_W4  LW4 ,
#define LOPT_RW1 LRW1,
#define LOPT_RW2 LRW2,
#define LOPT_RW4 LRW4,
#define LOPT_FR  LFR,
#define LOPT_FW  LFW,
#define LOPT_FRW LFRW,
#define LOPT_MEMR LMEMR,
#define LOPT_MEMW LMEMW,
#define LOPT_MEMRW LMEMRW,

#define LDIR_IMM
#define LDIR_R1
#define LDIR_R2
#define LDIR_R4
#define LDIR_W1
#define LDIR_W2
#define LDIR_W4
#define LDIR_RW1
#define LDIR_RW2
#define LDIR_RW4
#define LDIR_FW
#define LDIR_FR
#define LDIR_FRW
#define LDIR_MEMR
#define LDIR_MEMW
#define LDIR_MEMRW


#undef LOWFUNC
#undef LENDFUNC

#define LOWFUNC(flags,mem,nargs,func,args) \
  STATIC_INLINE void do_##func args

#define LENDFUNC(flags,mem,nargs,func,args) \
  STATIC_INLINE void func args \
  { \
  if (LDECISION) { \
    lopt_op##nargs##args do_##func, mem, flags); \
  } else { \
    do_##func ldirect##nargs##args; \
  } \
  }

typedef struct lopt_inst_rec {
    void* func;
    uae_u32 args[5];
    uae_u8 argtype[5];
    uae_s8 nargs;
    uae_u8 mem;
    uae_u8 flags;
} lopt_inst;



static lopt_inst linst[MAXLOPTINST];
static int lopt_index=0;

STATIC_INLINE int argsize(int type)
{
    return type&SIZEMASK;
}

STATIC_INLINE int reads_mem(int i) {
    return linst[i].mem & READ;
}


STATIC_INLINE int access_reg(int i, int r, int mode)
{
    int k;
    for (k=0;k<linst[i].nargs;k++)
	if (linst[i].args[k]==r &&
	    (linst[i].argtype[k]&mode) &&
	    !(linst[i].argtype[k]&FLOAT))
	    return 1;
    return 0;
}

STATIC_INLINE_ int writes_reg(int i, int r)
{
    return access_reg(i,r,WRITE);
}

STATIC_INLINE int reads_reg(int i, int r)
{
    return access_reg(i,r,READ);
}

STATIC_INLINE int uses_reg(int i, int r)
{
    return access_reg(i,r,RMW);
}


STATIC_INLINE int writes_mem(int i) {
    return linst[i].mem & WRITE;
}

STATIC_INLINE int uses_mem(int i)
{
    return linst[i].mem & RMW;
}

STATIC_INLINE int reads_flags(int i) {
    return linst[i].flags & READ;
}

STATIC_INLINE int writes_flags(int i) {
    return linst[i].flags & WRITE;
}

STATIC_INLINE int uses_flags(int i)
{
    return linst[i].flags & RMW;
}

static void do_raw_mov_l_rm(W4,MEMR);
static void do_raw_fflags_save(void);


/* Whether i depends on j */
STATIC_INLINE int depends_on(int i, int j)
{
    int n;

    /* First, check memory */
    if (writes_mem(i) && uses_mem(j))
	return 1;
    if (reads_mem(i) && writes_mem(j))
	return 1;

    /* Next, check flags */
    if (writes_flags(i) && uses_flags(j))
	return 1;
    if (reads_flags(i) && writes_flags(j))
	return 1;

    for (n=0;n<linst[i].nargs;n++) {
	if (linst[i].argtype[n] & FLOAT)
	    return 1;
    }
    for (n=0;n<linst[j].nargs;n++) {
	if (linst[j].argtype[n] & FLOAT)
	    return 1;
    }

    for (n=0;n<linst[i].nargs;n++) {
	if ((linst[i].argtype[n] & WRITE) &&
	    !(linst[i].argtype[n] & FLOAT)) {
	    if (uses_reg(j,linst[i].args[n]))
		return 1;
	}
	else if ((linst[i].argtype[n] & READ) &&
		 !(linst[i].argtype[n] & FLOAT))  {
	    if (writes_reg(j,linst[i].args[n]))
		return 1;
	}
    }

    /* The need for this indicates a problem somewhere in the
       LOWFUNC definitions --- I think. FIXME! */

    if (uses_flags(j) && uses_flags(i))
	return 1;
    if (linst[i].func==do_raw_fflags_save)
	return 1;
    if (linst[j].func==do_raw_fflags_save)
	return 1;

    return 0;
}

static void do_raw_mov_l_rm(W4 d, MEMR s);

STATIC_INLINE void low_peephole(void)
{
    int i;

    for (i=0;i<lopt_index;i++) {
	if (uses_mem(i)) {
	    int j=i-1;

	    while (j>=i-4 && j>=0 && !depends_on(i,j)) {
		j--;
	    }
	    if (j!=i-1) {
		lopt_inst x=linst[i];
		int k=i;

		j++;
		while (k>j) {
		    linst[k]=linst[k-1];
		    k--;
		}
		linst[j]=x;
	    }
	}
    }
}


typedef void lopt_handler0(void);
typedef void lopt_handler1(uae_u32);
typedef void lopt_handler2(uae_u32,uae_u32);
typedef void lopt_handler3(uae_u32,uae_u32,uae_u32);
typedef void lopt_handler4(uae_u32,uae_u32,uae_u32,uae_u32);
typedef void lopt_handler5(uae_u32,uae_u32,uae_u32,uae_u32,uae_u32);

static void lopt_emit_all(void)
{
    int i;
    lopt_inst* x;
    static int inemit=0;

    if (inemit) {
	printf("WARNING: lopt_emit is not reentrant!\n");
    }
    inemit=1;

    low_peephole();

    for (i=0;i<lopt_index;i++) {
	x=linst+i;
	switch(x->nargs) {
	 case 0: ((lopt_handler0*)x->func)(); break;
	 case 1: ((lopt_handler1*)x->func)(x->args[0]); break;
	 case 2: ((lopt_handler2*)x->func)(x->args[0],x->args[1]); break;
	 case 3: ((lopt_handler3*)x->func)(x->args[0],x->args[1],x->args[2]); break;
	 case 4: ((lopt_handler4*)x->func)(x->args[0],x->args[1],x->args[2],
					   x->args[3]); break;
	 case 5: ((lopt_handler5*)x->func)(x->args[0],x->args[1],x->args[2],
					   x->args[3],x->args[4]); break;
	 default: abort();
	}
    }
    lopt_index=0;
    inemit=0;
}

STATIC_INLINE void low_advance(void)
{
  lopt_index++;
  if (lopt_index==MAXLOPTINST)
    lopt_emit_all();
}

STATIC_INLINE void lopt_store_op0(void* lfuncptr, uae_u32 lmem,
				      uae_u32 lflags)
{
  linst[lopt_index].func=lfuncptr;
  linst[lopt_index].mem=lmem;
  linst[lopt_index].flags=lflags;
  linst[lopt_index].nargs=0;
  low_advance();
}

STATIC_INLINE void lopt_store_op1(uae_u8 t1, uae_u32 a1,
				      void* lfuncptr, uae_u32 lmem,
				      uae_u32 lflags)
{
  linst[lopt_index].func=lfuncptr;
  linst[lopt_index].mem=lmem;
  linst[lopt_index].flags=lflags;
  linst[lopt_index].nargs=1;
  linst[lopt_index].argtype[0]=t1;
  linst[lopt_index].args[0]=a1;
  low_advance();
}

STATIC_INLINE void lopt_store_op2(uae_u8 t1, uae_u32 a1,
				      uae_u8 t2, uae_u32 a2,
				      void* lfuncptr, uae_u32 lmem,
				      uae_u32 lflags)
{
  linst[lopt_index].func=lfuncptr;
  linst[lopt_index].mem=lmem;
  linst[lopt_index].flags=lflags;
  linst[lopt_index].nargs=2;
  linst[lopt_index].argtype[0]=t1;
  linst[lopt_index].args[0]=a1;
  linst[lopt_index].argtype[1]=t2;
  linst[lopt_index].args[1]=a2;
  low_advance();
}

STATIC_INLINE void lopt_store_op3(uae_u8 t1, uae_u32 a1,
				      uae_u8 t2, uae_u32 a2,
				      uae_u8 t3, uae_u32 a3,
				      void* lfuncptr, uae_u32 lmem,
				      uae_u32 lflags)
{
  linst[lopt_index].func=lfuncptr;
  linst[lopt_index].mem=lmem;
  linst[lopt_index].flags=lflags;
  linst[lopt_index].nargs=3;
  linst[lopt_index].argtype[0]=t1;
  linst[lopt_index].args[0]=a1;
  linst[lopt_index].argtype[1]=t2;
  linst[lopt_index].args[1]=a2;
  linst[lopt_index].argtype[2]=t3;
  linst[lopt_index].args[2]=a3;
  low_advance();
}

STATIC_INLINE void lopt_store_op4(uae_u8 t1, uae_u32 a1,
				      uae_u8 t2, uae_u32 a2,
				      uae_u8 t3, uae_u32 a3,
				      uae_u8 t4, uae_u32 a4,
				      void* lfuncptr, uae_u32 lmem,
				      uae_u32 lflags)
{
  linst[lopt_index].func=lfuncptr;
  linst[lopt_index].mem=lmem;
  linst[lopt_index].flags=lflags;
  linst[lopt_index].nargs=4;
  linst[lopt_index].argtype[0]=t1;
  linst[lopt_index].args[0]=a1;
  linst[lopt_index].argtype[1]=t2;
  linst[lopt_index].args[1]=a2;
  linst[lopt_index].argtype[2]=t3;
  linst[lopt_index].args[2]=a3;
  linst[lopt_index].argtype[3]=t4;
  linst[lopt_index].args[3]=a4;
  low_advance();
}

STATIC_INLINE void lopt_store_op5(uae_u8 t1, uae_u32 a1,
				      uae_u8 t2, uae_u32 a2,
				      uae_u8 t3, uae_u32 a3,
				      uae_u8 t4, uae_u32 a4,
				      uae_u8 t5, uae_u32 a5,
				      void* lfuncptr, uae_u32 lmem,
				      uae_u32 lflags)
{
  linst[lopt_index].func=lfuncptr;
  linst[lopt_index].mem=lmem;
  linst[lopt_index].flags=lflags;
  linst[lopt_index].nargs=5;
  linst[lopt_index].argtype[0]=t1;
  linst[lopt_index].args[0]=a1;
  linst[lopt_index].argtype[1]=t2;
  linst[lopt_index].args[1]=a2;
  linst[lopt_index].argtype[2]=t3;
  linst[lopt_index].args[2]=a3;
  linst[lopt_index].argtype[3]=t4;
  linst[lopt_index].args[3]=a4;
  linst[lopt_index].argtype[4]=t5;
  linst[lopt_index].args[4]=a5;
  low_advance();
}

STATIC_INLINE void empty_low_optimizer(void)
{
  lopt_emit_all();
}

#else
#define lopt_emit_all()
#define empty_low_optimizer()
#endif
