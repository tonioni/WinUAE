#if USE_OPTIMIZER
/* Welcome to the magical world of cpp ;-) */

#define USE_REGALLOC optlev>=2

#define opt_op0(dummy) opt_store_op0()
#define opt_op1(a1) opt_store_op1(OPT_##a1)
#define opt_op2(a1,a2) opt_store_op2(OPT_##a1,OPT_##a2)
#define opt_op3(a1,a2,a3) opt_store_op3(OPT_##a1,OPT_##a2,OPT_##a3)
#define opt_op4(a1,a2,a3,a4) opt_store_op4(OPT_##a1,OPT_##a2,OPT_##a3,OPT_##a4)
#define opt_op5(a1,a2,a3,a4,a5) opt_store_op5(OPT_##a1,OPT_##a2,OPT_##a3,OPT_##a4,OPT_##a5)

#define direct0(dummy) ()
#define direct1(a1) (DIR_##a1)
#define direct2(a1,a2) (DIR_##a1,DIR_##a2)
#define direct3(a1,a2,a3) (DIR_##a1,DIR_##a2,DIR_##a3)
#define direct4(a1,a2,a3,a4) (DIR_##a1,DIR_##a2,DIR_##a3,DIR_##a4)
#define direct5(a1,a2,a3,a4,a5) (DIR_##a1,DIR_##a2,DIR_##a3,DIR_##a4,DIR_##a5)

#define OIMM 0
#define OMEMR 0
#define OMEMW 0
#define OMEMRW 0 /* These are all 32 bit immediates */
#define OR1  1
#define OR2  2
#define OR4  3
#define OW1  4
#define OW2  5
#define OW4  6
#define ORW1 7
#define ORW2 8
#define ORW4 9

#define OFW 10
#define OFR 11
#define OFRW 12

#define INST_END 98
#define FLUSH 99

#define OPT_IMM OIMM,
#define OPT_MEMR OMEMR,
#define OPT_MEMW OMEMW,
#define OPT_MEMRW OMEMRW,
#define OPT_R1  OR1 ,
#define OPT_R2  OR2 ,
#define OPT_R4  OR4 ,
#define OPT_W1  OW1 ,
#define OPT_W2  OW2 ,
#define OPT_W4  OW4 ,
#define OPT_RW1 ORW1,
#define OPT_RW2 ORW2,
#define OPT_RW4 ORW4,

#define OPT_FW  OFW ,
#define OPT_FR  OFR ,
#define OPT_FRW OFRW ,

#define DIR_IMM 
#define DIR_MEMR 
#define DIR_MEMW 
#define DIR_MEMRW 
#define DIR_R1  
#define DIR_R2  
#define DIR_R4  
#define DIR_W1  
#define DIR_W2  
#define DIR_W4  
#define DIR_RW1 
#define DIR_RW2 
#define DIR_RW4 

#define DIR_FR
#define DIR_FW
#define DIR_FRW

#undef MIDFUNC
#undef MENDFUNC

#define MIDFUNC(nargs,func,args) \
  __inline__ void do_##func args

#define MENDFUNC(nargs,func,args) \
  void func args \
  { \
    if (reg_alloc_run) { \
      opt_op##nargs##args; \
    } else { \
      do_##func direct##nargs##args; \
    } \
  }

#undef COMPCALL
#define COMPCALL(func) do_##func

int opt_index=0;

static __inline__ void store_any(uae_u8 type, uae_u32 val)
{
    ra[opt_index].type=type;
    ra[opt_index].reg=val;
    opt_index++;
    if (opt_index>=MAXREGOPT) {
	printf("Oops! opt_index overflow....\n");
	abort();
    }
}

static __inline__ void store_arg(uae_u8 type, uae_u32 val)
{
    if (type<OR1 || type>ORW4) 
	return;
    store_any(type,val);
}

static __inline__ void opt_store_op0(void)
{
    /* zilch */
}

static __inline__ void opt_store_op1(uae_u8 t1, uae_u32 a1)
{
    store_arg(t1,a1);
    opt_store_op0();
}
 
static __inline__ void opt_store_op2(uae_u8 t1, uae_u32 a1,
				     uae_u8 t2, uae_u32 a2)
{
    store_arg(t2,a2);
    opt_store_op1(t1,a1);
}

static __inline__ void opt_store_op3(uae_u8 t1, uae_u32 a1,
				     uae_u8 t2, uae_u32 a2,
				     uae_u8 t3, uae_u32 a3)
{
    store_arg(t3,a3);
    opt_store_op2(t1,a1,t2,a2);
}

static __inline__ void opt_store_op4(uae_u8 t1, uae_u32 a1,
				     uae_u8 t2, uae_u32 a2,
				     uae_u8 t3, uae_u32 a3,
				     uae_u8 t4, uae_u32 a4)
{
    store_arg(t4,a4);
    opt_store_op3(t1,a1,t2,a2,t3,a3);
}

static __inline__ void opt_store_op5(uae_u8 t1, uae_u32 a1,
				     uae_u8 t2, uae_u32 a2,
				     uae_u8 t3, uae_u32 a3,
				     uae_u8 t4, uae_u32 a4,
				     uae_u8 t5, uae_u32 a5)
{
    store_arg(t5,a5);
    opt_store_op4(t1,a1,t2,a2,t3,a3,t4,a4);
}

static void opt_assert_empty(int line)
{
}

void empty_optimizer(void) 
{
}

#else
static __inline__ void opt_emit_all(void) {}
static __inline__ void opt_assert_empty(int line) {}
void empty_optimizer(void) {}
#define USE_REGALLOC 0
#endif
