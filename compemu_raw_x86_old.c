/* This should eventually end up in machdep/, but for now, x86 is the
   only target, and it's easier this way... */

/*************************************************************************
 * Some basic information about the the target CPU                       *
 *************************************************************************/

#define EAX 0
#define ECX 1
#define EDX 2
#define EBX 3

/* The register in which subroutines return an integer return value */
#define REG_RESULT 0

/* The registers subroutines take their first and second argument in */
#define REG_PAR1 0
#define REG_PAR2 2

/* Three registers that are not used for any of the above */
#define REG_NOPAR1 6
#define REG_NOPAR2 5
#define REG_NOPAR3 3

#define REG_PC_PRE 0 /* The register we use for preloading regs.pc_p */
#define REG_PC_TMP 1 /* Another register that is not the above */

#define SHIFTCOUNT_NREG 1  /* Register that can be used for shiftcount.
			      -1 if any reg will do */
#define MUL_NREG1 0 /* %eax will hold the low 32 bits after a 32x32 mul */
#define MUL_NREG2 2 /* %edx will hold the high 32 bits */

uae_s8 always_used[]={4,-1};
uae_s8 can_byte[]={0,1,2,3,-1};
uae_s8 can_word[]={0,1,2,3,5,6,7,-1};

uae_u8 call_saved[]={0,0,0,0,1,0,0,0};

/* This *should* be the same as call_saved. But:
   - We might not really know which registers are saved, and which aren't,
     so we need to preserve some, but don't want to rely on everyone else
     also saving those registers
   - Special registers (such like the stack pointer) should not be "preserved"
     by pushing, even though they are "saved" across function calls
*/
uae_u8 need_to_preserve[]={1,1,1,1,0,1,1,1};

/* Whether classes of instructions do or don't clobber the native flags */
#define CLOBBER_MOV
#define CLOBBER_LEA
#define CLOBBER_CMOV
#define CLOBBER_POP
#define CLOBBER_PUSH
#define CLOBBER_SUB  clobber_flags()
#define CLOBBER_SBB  clobber_flags()
#define CLOBBER_CMP  clobber_flags()
#define CLOBBER_ADD  clobber_flags()
#define CLOBBER_ADC  clobber_flags()
#define CLOBBER_AND  clobber_flags()
#define CLOBBER_OR   clobber_flags()
#define CLOBBER_XOR  clobber_flags()

#define CLOBBER_ROL  clobber_flags()
#define CLOBBER_ROR  clobber_flags()
#define CLOBBER_SHLL clobber_flags()
#define CLOBBER_SHRL clobber_flags()
#define CLOBBER_SHRA clobber_flags()
#define CLOBBER_TEST clobber_flags()
#define CLOBBER_CL16 
#define CLOBBER_CL8  
#define CLOBBER_SE16
#define CLOBBER_SE8
#define CLOBBER_ZE16
#define CLOBBER_ZE8
#define CLOBBER_SW16 clobber_flags()
#define CLOBBER_SW32
#define CLOBBER_SETCC
#define CLOBBER_MUL  clobber_flags()
#define CLOBBER_BT   clobber_flags()
#define CLOBBER_BSF  clobber_flags()

/*************************************************************************
 * Actual encoding of the instructions on the target CPU                 *
 *************************************************************************/

static int have_cmov=0;  /* We need to generate different code if 
			    we don't have cmov */

#include "compemu_optimizer_x86.c"

static uae_u16 swap16(uae_u16 x)
{
    return ((x&0xff00)>>8)|((x&0x00ff)<<8);
}

static uae_u32 swap32(uae_u32 x)
{
    return ((x&0xff00)<<8)|((x&0x00ff)<<24)|((x&0xff0000)>>8)|((x&0xff000000)>>24);
}

static __inline__ int isbyte(uae_s32 x)
{
  return (x>=-128 && x<=127);
}

LOWFUNC(NONE,WRITE,1,raw_push_l_r,(R4 r))
{
	emit_byte(0x50+r);
}
LENDFUNC(NONE,WRITE,1,raw_push_l_r,(R4 r))

LOWFUNC(NONE,READ,1,raw_pop_l_r,(R4 r))
{
	emit_byte(0x58+r);
}
LENDFUNC(NONE,READ,1,raw_pop_l_r,(R4 r))

LOWFUNC(WRITE,NONE,2,raw_bt_l_ri,(R4 r, IMM i))
{
	emit_byte(0x0f);
	emit_byte(0xba);
	emit_byte(0xe0+r);
	emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_bt_l_ri,(R4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_bt_l_rr,(R4 r, R4 b))
{
	emit_byte(0x0f);
	emit_byte(0xa3);
	emit_byte(0xc0+8*b+r);
}
LENDFUNC(WRITE,NONE,2,raw_bt_l_rr,(R4 r, R4 b))

LOWFUNC(WRITE,NONE,2,raw_btc_l_ri,(RW4 r, IMM i))
{
	emit_byte(0x0f);
	emit_byte(0xba);
	emit_byte(0xf8+r);
	emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_btc_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_btc_l_rr,(RW4 r, R4 b))
{
	emit_byte(0x0f);
	emit_byte(0xbb);
	emit_byte(0xc0+8*b+r);
}
LENDFUNC(WRITE,NONE,2,raw_btc_l_rr,(RW4 r, R4 b))


LOWFUNC(WRITE,NONE,2,raw_btr_l_ri,(RW4 r, IMM i))
{
	emit_byte(0x0f);
	emit_byte(0xba);
	emit_byte(0xf0+r);
	emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_btr_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_btr_l_rr,(RW4 r, R4 b))
{
	emit_byte(0x0f);
	emit_byte(0xb3);
	emit_byte(0xc0+8*b+r);
}
LENDFUNC(WRITE,NONE,2,raw_btr_l_rr,(RW4 r, R4 b))

LOWFUNC(WRITE,NONE,2,raw_bts_l_ri,(RW4 r, IMM i))
{
	emit_byte(0x0f);
	emit_byte(0xba);
	emit_byte(0xe8+r);
	emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_bts_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_bts_l_rr,(RW4 r, R4 b))
{
	emit_byte(0x0f);
	emit_byte(0xab);
	emit_byte(0xc0+8*b+r);
}
LENDFUNC(WRITE,NONE,2,raw_bts_l_rr,(RW4 r, R4 b))

LOWFUNC(WRITE,NONE,2,raw_sub_w_ri,(RW2 d, IMM i))
{
    emit_byte(0x66);
    if (isbyte(i)) {
	emit_byte(0x83);
	emit_byte(0xe8+d);
	emit_byte(i);
    }
    else {
	emit_byte(0x81);
	emit_byte(0xe8+d);
	emit_word(i);
    }
}
LENDFUNC(WRITE,NONE,2,raw_sub_w_ri,(RW2 d, IMM i))


LOWFUNC(NONE,WRITE,2,raw_mov_l_mi,(MEMW d, IMM s))
{
    emit_byte(0xc7);
    emit_byte(0x05);
    emit_long(d);
    emit_long(s);
}
LENDFUNC(NONE,WRITE,2,raw_mov_l_mi,(MEMW d, IMM s))

LOWFUNC(NONE,WRITE,2,raw_mov_w_mi,(MEMW d, IMM s))
{
    emit_byte(0x66);
    emit_byte(0xc7);
    emit_byte(0x05);
    emit_long(d);
    emit_word(s);
}
LENDFUNC(NONE,WRITE,2,raw_mov_w_mi,(MEMW d, IMM s))

LOWFUNC(NONE,WRITE,2,raw_mov_b_mi,(MEMW d, IMM s))
{
    emit_byte(0xc6);
    emit_byte(0x05);
    emit_long(d);
    emit_byte(s);
}
LENDFUNC(NONE,WRITE,2,raw_mov_b_mi,(MEMW d, IMM s))

LOWFUNC(WRITE,RMW,2,raw_rol_b_mi,(MEMRW d, IMM i))
{
    emit_byte(0xc0);
    emit_byte(0x05);
    emit_long(d);
    emit_byte(i);
}
LENDFUNC(WRITE,RMW,2,raw_rol_b_mi,(MEMRW d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_rol_b_ri,(RW1 r, IMM i))
{
    emit_byte(0xc0);
    emit_byte(0xc0+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_rol_b_ri,(RW1 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_rol_w_ri,(RW2 r, IMM i))
{
    emit_byte(0x66);
    emit_byte(0xc1);
    emit_byte(0xc0+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_rol_w_ri,(RW2 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_rol_l_ri,(RW4 r, IMM i))
{
    emit_byte(0xc1);
    emit_byte(0xc0+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_rol_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_rol_l_rr,(RW4 d, R1 r))
{
    emit_byte(0xd3);
    emit_byte(0xc0+d);
}
LENDFUNC(WRITE,NONE,2,raw_rol_l_rr,(RW4 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_rol_w_rr,(RW2 d, R1 r))
{
    emit_byte(0x66);
    emit_byte(0xd3);
    emit_byte(0xc0+d);
}
LENDFUNC(WRITE,NONE,2,raw_rol_w_rr,(RW2 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_rol_b_rr,(RW1 d, R1 r))
{
    emit_byte(0xd2);
    emit_byte(0xc0+d);
}
LENDFUNC(WRITE,NONE,2,raw_rol_b_rr,(RW1 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shll_l_rr,(RW4 d, R1 r))
{
    emit_byte(0xd3);
    emit_byte(0xe0+d);
}
LENDFUNC(WRITE,NONE,2,raw_shll_l_rr,(RW4 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shll_w_rr,(RW2 d, R1 r))
{
    emit_byte(0x66);
    emit_byte(0xd3);
    emit_byte(0xe0+d);
}
LENDFUNC(WRITE,NONE,2,raw_shll_w_rr,(RW2 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shll_b_rr,(RW1 d, R1 r))
{
    emit_byte(0xd2);
    emit_byte(0xe0+d);
}
LENDFUNC(WRITE,NONE,2,raw_shll_b_rr,(RW1 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_ror_b_ri,(RW1 r, IMM i))
{
    emit_byte(0xc0);
    emit_byte(0xc8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_ror_b_ri,(RW1 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_ror_w_ri,(RW2 r, IMM i))
{
    emit_byte(0x66);
    emit_byte(0xc1);
    emit_byte(0xc8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_ror_w_ri,(RW2 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_ror_l_ri,(RW4 r, IMM i))
{
    emit_byte(0xc1);
    emit_byte(0xc8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_ror_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_ror_l_rr,(RW4 d, R1 r))
{
    emit_byte(0xd3);
    emit_byte(0xc8+d);
}
LENDFUNC(WRITE,NONE,2,raw_ror_l_rr,(RW4 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_ror_w_rr,(RW2 d, R1 r))
{
    emit_byte(0x66);
    emit_byte(0xd3);
    emit_byte(0xc8+d);
}
LENDFUNC(WRITE,NONE,2,raw_ror_w_rr,(RW2 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_ror_b_rr,(RW1 d, R1 r))
{
    emit_byte(0xd2);
    emit_byte(0xc8+d);
}
LENDFUNC(WRITE,NONE,2,raw_ror_b_rr,(RW1 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shrl_l_rr,(RW4 d, R1 r))
{
    emit_byte(0xd3);
    emit_byte(0xe8+d);
}
LENDFUNC(WRITE,NONE,2,raw_shrl_l_rr,(RW4 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shrl_w_rr,(RW2 d, R1 r))
{
    emit_byte(0x66);
    emit_byte(0xd3);
    emit_byte(0xe8+d);
}
LENDFUNC(WRITE,NONE,2,raw_shrl_w_rr,(RW2 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shrl_b_rr,(RW1 d, R1 r))
{
    emit_byte(0xd2);
    emit_byte(0xe8+d);
}
LENDFUNC(WRITE,NONE,2,raw_shrl_b_rr,(RW1 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shra_l_rr,(RW4 d, R1 r))
{
    emit_byte(0xd3);
    emit_byte(0xf8+d);
}
LENDFUNC(WRITE,NONE,2,raw_shra_l_rr,(RW4 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shra_w_rr,(RW2 d, R1 r))
{
    emit_byte(0x66);
    emit_byte(0xd3);
    emit_byte(0xf8+d);
}
LENDFUNC(WRITE,NONE,2,raw_shra_w_rr,(RW2 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shra_b_rr,(RW1 d, R1 r))
{
    emit_byte(0xd2);
    emit_byte(0xf8+d);
}
LENDFUNC(WRITE,NONE,2,raw_shra_b_rr,(RW1 d, R1 r))

LOWFUNC(WRITE,NONE,2,raw_shll_l_ri,(RW4 r, IMM i))
{
    emit_byte(0xc1);
    emit_byte(0xe0+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shll_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shll_w_ri,(RW2 r, IMM i))
{
    emit_byte(0x66);
    emit_byte(0xc1);
    emit_byte(0xe0+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shll_w_ri,(RW2 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shll_b_ri,(RW1 r, IMM i))
{
    emit_byte(0xc0);
    emit_byte(0xe0+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shll_b_ri,(RW1 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shrl_l_ri,(RW4 r, IMM i))
{
    emit_byte(0xc1);
    emit_byte(0xe8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shrl_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shrl_w_ri,(RW2 r, IMM i))
{
    emit_byte(0x66);
    emit_byte(0xc1);
    emit_byte(0xe8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shrl_w_ri,(RW2 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shrl_b_ri,(RW1 r, IMM i))
{
    emit_byte(0xc0);
    emit_byte(0xe8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shrl_b_ri,(RW1 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shra_l_ri,(RW4 r, IMM i))
{
    emit_byte(0xc1);
    emit_byte(0xf8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shra_l_ri,(RW4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shra_w_ri,(RW2 r, IMM i))
{
    emit_byte(0x66);
    emit_byte(0xc1);
    emit_byte(0xf8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shra_w_ri,(RW2 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_shra_b_ri,(RW1 r, IMM i))
{
    emit_byte(0xc0);
    emit_byte(0xf8+r);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_shra_b_ri,(RW1 r, IMM i))

LOWFUNC(WRITE,NONE,1,raw_sahf,(R2 dummy_ah))
{
    emit_byte(0x9e);
}
LENDFUNC(WRITE,NONE,1,raw_sahf,(R2 dummy_ah))

LOWFUNC(NONE,NONE,1,raw_cpuid,(R4 dummy_eax))
{
    emit_byte(0x0f);
    emit_byte(0xa2);
}
LENDFUNC(NONE,NONE,1,raw_cpuid,(R4 dummy_eax))

LOWFUNC(READ,NONE,1,raw_lahf,(W2 dummy_ah))
{
    emit_byte(0x9f);
}
LENDFUNC(READ,NONE,1,raw_lahf,(W2 dummy_ah))

LOWFUNC(READ,NONE,2,raw_setcc,(W1 d, IMM cc))
{
    emit_byte(0x0f);
    emit_byte(0x90+cc);
    emit_byte(0xc0+d);
}
LENDFUNC(READ,NONE,2,raw_setcc,(W1 d, IMM cc))

LOWFUNC(READ,WRITE,2,raw_setcc_m,(MEMW d, IMM cc))
{
    emit_byte(0x0f);
    emit_byte(0x90+cc);
    emit_byte(0x05);
    emit_long(d);
}
LENDFUNC(READ,WRITE,2,raw_setcc_m,(MEMW d, IMM cc))

LOWFUNC(READ,NONE,3,raw_cmov_l_rr,(RW4 d, R4 s, IMM cc))
{
    if (have_cmov) {
	emit_byte(0x0f);
	emit_byte(0x40+cc);
	emit_byte(0xc0+8*d+s);
    }
    else { /* replacement using branch and mov */
	int uncc=(cc^1);
	emit_byte(0x70+uncc); 
	emit_byte(2);  /* skip next 2 bytes if not cc=true */
	emit_byte(0x89);
	emit_byte(0xc0+8*s+d);
    }
}
LENDFUNC(READ,NONE,3,raw_cmov_l_rr,(RW4 d, R4 s, IMM cc))

LOWFUNC(WRITE,NONE,2,raw_bsf_l_rr,(W4 d, R4 s))
{
    emit_byte(0x0f);
    emit_byte(0xbc);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(WRITE,NONE,2,raw_bsf_l_rr,(W4 d, R4 s))

LOWFUNC(NONE,NONE,2,raw_sign_extend_16_rr,(W4 d, R2 s))
{
    emit_byte(0x0f);
    emit_byte(0xbf);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(NONE,NONE,2,raw_sign_extend_16_rr,(W4 d, R2 s))

LOWFUNC(NONE,NONE,2,raw_sign_extend_8_rr,(W4 d, R1 s))
{
    emit_byte(0x0f);
    emit_byte(0xbe);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(NONE,NONE,2,raw_sign_extend_8_rr,(W4 d, R1 s))

LOWFUNC(NONE,NONE,2,raw_zero_extend_16_rr,(W4 d, R2 s))
{
    emit_byte(0x0f);
    emit_byte(0xb7);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(NONE,NONE,2,raw_zero_extend_16_rr,(W4 d, R2 s))

LOWFUNC(NONE,NONE,2,raw_zero_extend_8_rr,(W4 d, R1 s))
{
    emit_byte(0x0f);
    emit_byte(0xb6);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(NONE,NONE,2,raw_zero_extend_8_rr,(W4 d, R1 s))

LOWFUNC(NONE,NONE,2,raw_imul_32_32,(RW4 d, R4 s))
{
    emit_byte(0x0f);
    emit_byte(0xaf);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(NONE,NONE,2,raw_imul_32_32,(RW4 d, R4 s))

LOWFUNC(NONE,NONE,2,raw_imul_64_32,(RW4 d, RW4 s))
{
    if (d!=MUL_NREG1 || s!=MUL_NREG2)
	abort();
    emit_byte(0xf7);
    emit_byte(0xea);
}
LENDFUNC(NONE,NONE,2,raw_imul_64_32,(RW4 d, RW4 s))

LOWFUNC(NONE,NONE,2,raw_mul_64_32,(RW4 d, RW4 s))
{
    if (d!=MUL_NREG1 || s!=MUL_NREG2) {
	printf("Bad register in MUL: d=%d, s=%d\n",d,s);
	abort();
    }
    emit_byte(0xf7);
    emit_byte(0xe2);
}
LENDFUNC(NONE,NONE,2,raw_mul_64_32,(RW4 d, RW4 s))

LOWFUNC(NONE,NONE,2,raw_mul_32_32,(RW4 d, R4 s))
{
    abort(); /* %^$&%^$%#^ x86! */
    emit_byte(0x0f);
    emit_byte(0xaf);
    emit_byte(0xc0+8*d+s);
}
LENDFUNC(NONE,NONE,2,raw_mul_32_32,(RW4 d, R4 s))

LOWFUNC(NONE,NONE,2,raw_mov_b_rr,(W1 d, R1 s))
{
    emit_byte(0x88);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(NONE,NONE,2,raw_mov_b_rr,(W1 d, R1 s))

LOWFUNC(NONE,NONE,2,raw_mov_w_rr,(W2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x89);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(NONE,NONE,2,raw_mov_w_rr,(W2 d, R2 s))

LOWFUNC(NONE,READ,4,raw_mov_l_rrm_indexed,(W4 d,R4 baser, R4 index, IMM factor))
{
    int isebp=(baser==5)?0x40:0;
    int fi;
    
    switch(factor) {
     case 1: fi=0; break;
     case 2: fi=1; break;
     case 4: fi=2; break;
     case 8: fi=3; break;
     default: abort();
    }


    emit_byte(0x8b);
    emit_byte(0x04+8*d+isebp);
    emit_byte(baser+8*index+0x40*fi);
    if (isebp)
	emit_byte(0x00);
}
LENDFUNC(NONE,READ,4,raw_mov_l_rrm_indexed,(W4 d,R4 baser, R4 index, IMM factor))

LOWFUNC(NONE,READ,4,raw_mov_w_rrm_indexed,(W2 d, R4 baser, R4 index, IMM factor))
{
    int fi;
    int isebp;
    
    switch(factor) {
     case 1: fi=0; break;
     case 2: fi=1; break;
     case 4: fi=2; break;
     case 8: fi=3; break;
     default: abort();
    }
    isebp=(baser==5)?0x40:0;
    
    emit_byte(0x66);
    emit_byte(0x8b);
    emit_byte(0x04+8*d+isebp);
    emit_byte(baser+8*index+0x40*fi);
    if (isebp)
	emit_byte(0x00);
}
LENDFUNC(NONE,READ,4,raw_mov_w_rrm_indexed,(W2 d, R4 baser, R4 index, IMM factor))

LOWFUNC(NONE,READ,4,raw_mov_b_rrm_indexed,(W1 d, R4 baser, R4 index, IMM factor))
{
   int fi;
  int isebp;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }
  isebp=(baser==5)?0x40:0;

   emit_byte(0x8a);
    emit_byte(0x04+8*d+isebp);
    emit_byte(baser+8*index+0x40*fi);
    if (isebp)
	emit_byte(0x00);
}
LENDFUNC(NONE,READ,4,raw_mov_b_rrm_indexed,(W1 d, R4 baser, R4 index, IMM factor))

LOWFUNC(NONE,WRITE,4,raw_mov_l_mrr_indexed,(R4 baser, R4 index, IMM factor, R4 s))
{
  int fi;
  int isebp;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

  
  isebp=(baser==5)?0x40:0;

    emit_byte(0x89);
    emit_byte(0x04+8*s+isebp);
    emit_byte(baser+8*index+0x40*fi);
    if (isebp)
	emit_byte(0x00);
}
LENDFUNC(NONE,WRITE,4,raw_mov_l_mrr_indexed,(R4 baser, R4 index, IMM factor, R4 s))

LOWFUNC(NONE,WRITE,4,raw_mov_w_mrr_indexed,(R4 baser, R4 index, IMM factor, R2 s))
{
  int fi;
  int isebp;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }
  isebp=(baser==5)?0x40:0;

    emit_byte(0x66);
    emit_byte(0x89);
    emit_byte(0x04+8*s+isebp);
    emit_byte(baser+8*index+0x40*fi);
    if (isebp)
	emit_byte(0x00);
}
LENDFUNC(NONE,WRITE,4,raw_mov_w_mrr_indexed,(R4 baser, R4 index, IMM factor, R2 s))

LOWFUNC(NONE,WRITE,4,raw_mov_b_mrr_indexed,(R4 baser, R4 index, IMM factor, R1 s))
{
  int fi;
  int isebp;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }
  isebp=(baser==5)?0x40:0;

    emit_byte(0x88);
    emit_byte(0x04+8*s+isebp);
    emit_byte(baser+8*index+0x40*fi);
    if (isebp)
	emit_byte(0x00);
}
LENDFUNC(NONE,WRITE,4,raw_mov_b_mrr_indexed,(R4 baser, R4 index, IMM factor, R1 s))

LOWFUNC(NONE,WRITE,5,raw_mov_l_bmrr_indexed,(IMM base, R4 baser, R4 index, IMM factor, R4 s))
{
  int fi;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x89);
    emit_byte(0x84+8*s);
    emit_byte(baser+8*index+0x40*fi);
    emit_long(base);
}
LENDFUNC(NONE,WRITE,5,raw_mov_l_bmrr_indexed,(IMM base, R4 baser, R4 index, IMM factor, R4 s))

LOWFUNC(NONE,WRITE,5,raw_mov_w_bmrr_indexed,(IMM base, R4 baser, R4 index, IMM factor, R2 s))
{
  int fi;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x66);
    emit_byte(0x89);
    emit_byte(0x84+8*s);
    emit_byte(baser+8*index+0x40*fi);
    emit_long(base);
}
LENDFUNC(NONE,WRITE,5,raw_mov_w_bmrr_indexed,(IMM base, R4 baser, R4 index, IMM factor, R2 s))

LOWFUNC(NONE,WRITE,5,raw_mov_b_bmrr_indexed,(IMM base, R4 baser, R4 index, IMM factor, R1 s))
{
  int fi;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x88);
    emit_byte(0x84+8*s);
    emit_byte(baser+8*index+0x40*fi);
    emit_long(base);
}
LENDFUNC(NONE,WRITE,5,raw_mov_b_bmrr_indexed,(IMM base, R4 baser, R4 index, IMM factor, R1 s))

LOWFUNC(NONE,READ,5,raw_mov_l_brrm_indexed,(W4 d, IMM base, R4 baser, R4 index, IMM factor))
{
  int fi;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x8b);
    emit_byte(0x84+8*d);
    emit_byte(baser+8*index+0x40*fi);
    emit_long(base);
}
LENDFUNC(NONE,READ,5,raw_mov_l_brrm_indexed,(W4 d, IMM base, R4 baser, R4 index, IMM factor))

LOWFUNC(NONE,READ,5,raw_mov_w_brrm_indexed,(W2 d, IMM base, R4 baser, R4 index, IMM factor))
{
  int fi;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x66);
    emit_byte(0x8b);
    emit_byte(0x84+8*d);
    emit_byte(baser+8*index+0x40*fi);
    emit_long(base);
}
LENDFUNC(NONE,READ,5,raw_mov_w_brrm_indexed,(W2 d, IMM base, R4 baser, R4 index, IMM factor))

LOWFUNC(NONE,READ,5,raw_mov_b_brrm_indexed,(W1 d, IMM base, R4 baser, R4 index, IMM factor))
{
  int fi;

  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x8a);
    emit_byte(0x84+8*d);
    emit_byte(baser+8*index+0x40*fi);
    emit_long(base);
}
LENDFUNC(NONE,READ,5,raw_mov_b_brrm_indexed,(W1 d, IMM base, R4 baser, R4 index, IMM factor))

LOWFUNC(NONE,READ,4,raw_mov_l_rm_indexed,(W4 d, IMM base, R4 index, IMM factor))
{
  int fi;
  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: 
    fprintf(stderr,"Bad factor %d in mov_l_rm_indexed!\n",factor);
    abort();
  }
    emit_byte(0x8b);
    emit_byte(0x04+8*d);
    emit_byte(0x05+8*index+64*fi);
    emit_long(base);
}
LENDFUNC(NONE,READ,4,raw_mov_l_rm_indexed,(W4 d, IMM base, R4 index, IMM factor))

LOWFUNC(NONE,READ,5,raw_cmov_l_rm_indexed,(W4 d, IMM base, R4 index, IMM factor, IMM cond))
{
    int fi;
    switch(factor) {
     case 1: fi=0; break;
     case 2: fi=1; break;
     case 4: fi=2; break;
     case 8: fi=3; break;
     default: 
	fprintf(stderr,"Bad factor %d in mov_l_rm_indexed!\n",factor);
	abort();
    }
    if (have_cmov) {
	emit_byte(0x0f);
	emit_byte(0x40+cond);
	emit_byte(0x04+8*d);
	emit_byte(0x05+8*index+64*fi);
	emit_long(base);
    }
    else { /* replacement using branch and mov */
	int uncc=(cond^1);
	emit_byte(0x70+uncc); 
	emit_byte(7);  /* skip next 7 bytes if not cc=true */
	emit_byte(0x8b);
	emit_byte(0x04+8*d);
	emit_byte(0x05+8*index+64*fi);
	emit_long(base);
    }
}
LENDFUNC(NONE,READ,5,raw_cmov_l_rm_indexed,(W4 d, IMM base, R4 index, IMM factor, IMM cond))

LOWFUNC(NONE,READ,3,raw_cmov_l_rm,(W4 d, IMM mem, IMM cond))
{
    if (have_cmov) {
	emit_byte(0x0f);
	emit_byte(0x40+cond);
	emit_byte(0x05+8*d);
	emit_long(mem);
    }
    else { /* replacement using branch and mov */
	int uncc=(cond^1);
	emit_byte(0x70+uncc); 
	emit_byte(6);  /* skip next 6 bytes if not cc=true */
	emit_byte(0x8b);
	emit_byte(0x05+8*d);
	emit_long(mem);
    }
}
LENDFUNC(NONE,READ,3,raw_cmov_l_rm,(W4 d, IMM mem, IMM cond))

LOWFUNC(NONE,READ,3,raw_mov_l_rR,(W4 d, R4 s, IMM offset))
{
    emit_byte(0x8b);
    emit_byte(0x40+8*d+s);
    emit_byte(offset);
}
LENDFUNC(NONE,READ,3,raw_mov_l_rR,(W4 d, R4 s, IMM offset))

LOWFUNC(NONE,READ,3,raw_mov_w_rR,(W2 d, R4 s, IMM offset))
{
    emit_byte(0x66);
    emit_byte(0x8b);
    emit_byte(0x40+8*d+s);
    emit_byte(offset);
}
LENDFUNC(NONE,READ,3,raw_mov_w_rR,(W2 d, R4 s, IMM offset))

LOWFUNC(NONE,READ,3,raw_mov_b_rR,(W1 d, R4 s, IMM offset))
{
    emit_byte(0x8a);
    emit_byte(0x40+8*d+s);
    emit_byte(offset);
}
LENDFUNC(NONE,READ,3,raw_mov_b_rR,(W1 d, R4 s, IMM offset))

LOWFUNC(NONE,READ,3,raw_mov_l_brR,(W4 d, R4 s, IMM offset))
{
    emit_byte(0x8b);
    emit_byte(0x80+8*d+s);
    emit_long(offset);
}
LENDFUNC(NONE,READ,3,raw_mov_l_brR,(W4 d, R4 s, IMM offset))

LOWFUNC(NONE,READ,3,raw_mov_w_brR,(W2 d, R4 s, IMM offset))
{
    emit_byte(0x66);
    emit_byte(0x8b);
    emit_byte(0x80+8*d+s);
    emit_long(offset);
}
LENDFUNC(NONE,READ,3,raw_mov_w_brR,(W2 d, R4 s, IMM offset))

LOWFUNC(NONE,READ,3,raw_mov_b_brR,(W1 d, R4 s, IMM offset))
{
    emit_byte(0x8a);
    emit_byte(0x80+8*d+s);
    emit_long(offset);
}
LENDFUNC(NONE,READ,3,raw_mov_b_brR,(W1 d, R4 s, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_l_Ri,(R4 d, IMM i, IMM offset))
{
    emit_byte(0xc7);
    emit_byte(0x40+d);
    emit_byte(offset);
    emit_long(i);
}
LENDFUNC(NONE,WRITE,3,raw_mov_l_Ri,(R4 d, IMM i, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_w_Ri,(R4 d, IMM i, IMM offset))
{
    emit_byte(0x66);
    emit_byte(0xc7);
    emit_byte(0x40+d);
    emit_byte(offset);
    emit_word(i);
}
LENDFUNC(NONE,WRITE,3,raw_mov_w_Ri,(R4 d, IMM i, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_b_Ri,(R4 d, IMM i, IMM offset))
{
    emit_byte(0xc6);
    emit_byte(0x40+d);
    emit_byte(offset);
    emit_byte(i);
}
LENDFUNC(NONE,WRITE,3,raw_mov_b_Ri,(R4 d, IMM i, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_l_Rr,(R4 d, R4 s, IMM offset))
{
    emit_byte(0x89);
    emit_byte(0x40+8*s+d);
    emit_byte(offset);
}
LENDFUNC(NONE,WRITE,3,raw_mov_l_Rr,(R4 d, R4 s, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_w_Rr,(R4 d, R2 s, IMM offset))
{
    emit_byte(0x66);
    emit_byte(0x89);
    emit_byte(0x40+8*s+d);
    emit_byte(offset);
}
LENDFUNC(NONE,WRITE,3,raw_mov_w_Rr,(R4 d, R2 s, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_b_Rr,(R4 d, R1 s, IMM offset))
{
    emit_byte(0x88);
    emit_byte(0x40+8*s+d);
    emit_byte(offset);
}
LENDFUNC(NONE,WRITE,3,raw_mov_b_Rr,(R4 d, R1 s, IMM offset))

LOWFUNC(NONE,NONE,3,raw_lea_l_brr,(W4 d, R4 s, IMM offset))
{
    emit_byte(0x8d);
    emit_byte(0x80+8*d+s);
    emit_long(offset);
}
LENDFUNC(NONE,NONE,3,raw_lea_l_brr,(W4 d, R4 s, IMM offset))

LOWFUNC(NONE,NONE,5,raw_lea_l_brr_indexed,(W4 d, R4 s, R4 index, IMM factor, IMM offset))
{
  int fi;
  
  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x8d);
    emit_byte(0x84+8*d);
    emit_byte(0x40*fi+8*index+s);
    emit_long(offset);
}
LENDFUNC(NONE,NONE,5,raw_lea_l_brr_indexed,(W4 d, R4 s, R4 index, IMM factor, IMM offset))

LOWFUNC(NONE,NONE,4,raw_lea_l_rr_indexed,(W4 d, R4 s, R4 index, IMM factor))
{
  int isebp=(s==5)?0x40:0;
  int fi;
  
  switch(factor) {
  case 1: fi=0; break;
  case 2: fi=1; break;
  case 4: fi=2; break;
  case 8: fi=3; break;
  default: abort();
  }

    emit_byte(0x8d);
    emit_byte(0x04+8*d+isebp);
    emit_byte(0x40*fi+8*index+s);
    if (isebp)
      emit_byte(0);
}
LENDFUNC(NONE,NONE,4,raw_lea_l_rr_indexed,(W4 d, R4 s, R4 index, IMM factor))

LOWFUNC(NONE,WRITE,3,raw_mov_l_bRr,(R4 d, R4 s, IMM offset))
{
    emit_byte(0x89);
    emit_byte(0x80+8*s+d);
    emit_long(offset);
}
LENDFUNC(NONE,WRITE,3,raw_mov_l_bRr,(R4 d, R4 s, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_w_bRr,(R4 d, R2 s, IMM offset))
{
    emit_byte(0x66);
    emit_byte(0x89);
    emit_byte(0x80+8*s+d);
    emit_long(offset);
}
LENDFUNC(NONE,WRITE,3,raw_mov_w_bRr,(R4 d, R2 s, IMM offset))

LOWFUNC(NONE,WRITE,3,raw_mov_b_bRr,(R4 d, R1 s, IMM offset))
{
    emit_byte(0x88);
    emit_byte(0x80+8*s+d);
    emit_long(offset);
}
LENDFUNC(NONE,WRITE,3,raw_mov_b_bRr,(R4 d, R1 s, IMM offset))

LOWFUNC(NONE,NONE,1,raw_bswap_32,(RW4 r))
{
    emit_byte(0x0f);
    emit_byte(0xc8+r);
}
LENDFUNC(NONE,NONE,1,raw_bswap_32,(RW4 r))

LOWFUNC(WRITE,NONE,1,raw_bswap_16,(RW2 r))
{
    emit_byte(0x66);
    emit_byte(0xc1);
    emit_byte(0xc0+r);
    emit_byte(0x08);
}
LENDFUNC(WRITE,NONE,1,raw_bswap_16,(RW2 r))

LOWFUNC(NONE,NONE,2,raw_mov_l_rr,(W4 d, R4 s))
{
    emit_byte(0x89);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(NONE,NONE,2,raw_mov_l_rr,(W4 d, R4 s))

LOWFUNC(NONE,WRITE,2,raw_mov_l_mr,(IMM d, R4 s))
{
    emit_byte(0x89);
    emit_byte(0x05+8*s);
    emit_long(d);
}
LENDFUNC(NONE,WRITE,2,raw_mov_l_mr,(IMM d, R4 s))

LOWFUNC(NONE,READ,2,raw_mov_l_rm,(W4 d, MEMR s))
{
    emit_byte(0x8b);
    emit_byte(0x05+8*d);
    emit_long(s);
}
LENDFUNC(NONE,READ,2,raw_mov_l_rm,(W4 d, MEMR s))

LOWFUNC(NONE,WRITE,2,raw_mov_w_mr,(IMM d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x89);
    emit_byte(0x05+8*s);
    emit_long(d);
}
LENDFUNC(NONE,WRITE,2,raw_mov_w_mr,(IMM d, R2 s))

LOWFUNC(NONE,READ,2,raw_mov_w_rm,(W2 d, IMM s))
{
    emit_byte(0x66);
    emit_byte(0x8b);
    emit_byte(0x05+8*d);
    emit_long(s);
}
LENDFUNC(NONE,READ,2,raw_mov_w_rm,(W2 d, IMM s))

LOWFUNC(NONE,WRITE,2,raw_mov_b_mr,(IMM d, R1 s))
{
    emit_byte(0x88);
    emit_byte(0x05+8*s);
    emit_long(d);
}
LENDFUNC(NONE,WRITE,2,raw_mov_b_mr,(IMM d, R1 s))

LOWFUNC(NONE,READ,2,raw_mov_b_rm,(W1 d, IMM s))
{
    emit_byte(0x8a);
    emit_byte(0x05+8*d);
    emit_long(s);
}
LENDFUNC(NONE,READ,2,raw_mov_b_rm,(W1 d, IMM s))

LOWFUNC(NONE,NONE,2,raw_mov_l_ri,(W4 d, IMM s))
{
    emit_byte(0xb8+d);
    emit_long(s);
}
LENDFUNC(NONE,NONE,2,raw_mov_l_ri,(W4 d, IMM s))

LOWFUNC(NONE,NONE,2,raw_mov_w_ri,(W2 d, IMM s))
{
    emit_byte(0x66);
    emit_byte(0xb8+d);
    emit_word(s);
}
LENDFUNC(NONE,NONE,2,raw_mov_w_ri,(W2 d, IMM s))

LOWFUNC(NONE,NONE,2,raw_mov_b_ri,(W1 d, IMM s))
{
    emit_byte(0xb0+d);
    emit_byte(s);
}
LENDFUNC(NONE,NONE,2,raw_mov_b_ri,(W1 d, IMM s))

LOWFUNC(RMW,RMW,2,raw_adc_l_mi,(MEMRW d, IMM s))
{
    emit_byte(0x81);
    emit_byte(0x15);
    emit_long(d);
    emit_long(s);
}
LENDFUNC(RMW,RMW,2,raw_adc_l_mi,(MEMRW d, IMM s))

LOWFUNC(WRITE,RMW,2,raw_add_l_mi,(IMM d, IMM s)) 
{
    emit_byte(0x81);
    emit_byte(0x05);
    emit_long(d);
    emit_long(s);
}
LENDFUNC(WRITE,RMW,2,raw_add_l_mi,(IMM d, IMM s)) 

LOWFUNC(WRITE,RMW,2,raw_add_w_mi,(IMM d, IMM s)) 
{
    emit_byte(0x66);
    emit_byte(0x81);
    emit_byte(0x05);
    emit_long(d);
    emit_word(s);
}
LENDFUNC(WRITE,RMW,2,raw_add_w_mi,(IMM d, IMM s)) 

LOWFUNC(WRITE,RMW,2,raw_add_b_mi,(IMM d, IMM s)) 
{
    emit_byte(0x80);
    emit_byte(0x05);
    emit_long(d);
    emit_byte(s);
}
LENDFUNC(WRITE,RMW,2,raw_add_b_mi,(IMM d, IMM s)) 

LOWFUNC(WRITE,NONE,2,raw_test_l_ri,(R4 d, IMM i))
{
    emit_byte(0xf7);
    emit_byte(0xc0+d);
    emit_long(i);
}
LENDFUNC(WRITE,NONE,2,raw_test_l_ri,(R4 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_test_l_rr,(R4 d, R4 s))
{
    emit_byte(0x85);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_test_l_rr,(R4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_test_w_rr,(R2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x85);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_test_w_rr,(R2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_test_b_rr,(R1 d, R1 s))
{
    emit_byte(0x84);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_test_b_rr,(R1 d, R1 s))

LOWFUNC(WRITE,NONE,2,raw_and_l_ri,(RW4 d, IMM i))
{
    emit_byte(0x81);
    emit_byte(0xe0+d);
    emit_long(i);
}
LENDFUNC(WRITE,NONE,2,raw_and_l_ri,(RW4 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_and_w_ri,(RW2 d, IMM i))
{
    emit_byte(0x66);
    emit_byte(0x81);
    emit_byte(0xe0+d);
    emit_word(i);
}
LENDFUNC(WRITE,NONE,2,raw_and_w_ri,(RW2 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_and_l,(RW4 d, R4 s))
{
    emit_byte(0x21);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_and_l,(RW4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_and_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x21);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_and_w,(RW2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_and_b,(RW1 d, R1 s))
{
    emit_byte(0x20);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_and_b,(RW1 d, R1 s))

LOWFUNC(WRITE,NONE,2,raw_or_l_ri,(RW4 d, IMM i))
{
    emit_byte(0x81);
    emit_byte(0xc8+d);
    emit_long(i);
}
LENDFUNC(WRITE,NONE,2,raw_or_l_ri,(RW4 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_or_l,(RW4 d, R4 s))
{
    emit_byte(0x09);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_or_l,(RW4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_or_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x09);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_or_w,(RW2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_or_b,(RW1 d, R1 s))
{
    emit_byte(0x08);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_or_b,(RW1 d, R1 s))

LOWFUNC(RMW,NONE,2,raw_adc_l,(RW4 d, R4 s))
{
    emit_byte(0x11);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(RMW,NONE,2,raw_adc_l,(RW4 d, R4 s))

LOWFUNC(RMW,NONE,2,raw_adc_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x11);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(RMW,NONE,2,raw_adc_w,(RW2 d, R2 s))

LOWFUNC(RMW,NONE,2,raw_adc_b,(RW1 d, R1 s))
{
    emit_byte(0x10);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(RMW,NONE,2,raw_adc_b,(RW1 d, R1 s))

LOWFUNC(WRITE,NONE,2,raw_add_l,(RW4 d, R4 s))
{
    emit_byte(0x01);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_add_l,(RW4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_add_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x01);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_add_w,(RW2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_add_b,(RW1 d, R1 s))
{
    emit_byte(0x00);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_add_b,(RW1 d, R1 s))

LOWFUNC(WRITE,NONE,2,raw_sub_l_ri,(RW4 d, IMM i))
{
  if (isbyte(i)) {
    emit_byte(0x83);
    emit_byte(0xe8+d);
    emit_byte(i);
  }
  else {
    emit_byte(0x81);
    emit_byte(0xe8+d);
    emit_long(i);
  }
}
LENDFUNC(WRITE,NONE,2,raw_sub_l_ri,(RW4 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_sub_b_ri,(RW1 d, IMM i))
{
    emit_byte(0x80);
    emit_byte(0xe8+d);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_sub_b_ri,(RW1 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_add_l_ri,(RW4 d, IMM i))
{
    if (isbyte(i)) {
	emit_byte(0x83);
	emit_byte(0xc0+d);
	emit_byte(i);
    }
    else {
	emit_byte(0x81);
	emit_byte(0xc0+d);
	emit_long(i);
    }
}
LENDFUNC(WRITE,NONE,2,raw_add_l_ri,(RW4 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_add_w_ri,(RW2 d, IMM i))
{
    if (isbyte(i)) {
	emit_byte(0x66);
	emit_byte(0x83);
	emit_byte(0xc0+d);
	emit_byte(i);
    }
    else {
	emit_byte(0x66);
	emit_byte(0x81);
	emit_byte(0xc0+d);
	emit_word(i);
    }
}
LENDFUNC(WRITE,NONE,2,raw_add_w_ri,(RW2 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_add_b_ri,(RW1 d, IMM i))
{
    emit_byte(0x80);
    emit_byte(0xc0+d);
    emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_add_b_ri,(RW1 d, IMM i))

LOWFUNC(RMW,NONE,2,raw_sbb_l,(RW4 d, R4 s))
{
    emit_byte(0x19);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(RMW,NONE,2,raw_sbb_l,(RW4 d, R4 s))

LOWFUNC(RMW,NONE,2,raw_sbb_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x19);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(RMW,NONE,2,raw_sbb_w,(RW2 d, R2 s))

LOWFUNC(RMW,NONE,2,raw_sbb_b,(RW1 d, R1 s))
{
    emit_byte(0x18);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(RMW,NONE,2,raw_sbb_b,(RW1 d, R1 s))

LOWFUNC(WRITE,NONE,2,raw_sub_l,(RW4 d, R4 s))
{
    emit_byte(0x29);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_sub_l,(RW4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_sub_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x29);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_sub_w,(RW2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_sub_b,(RW1 d, R1 s))
{
    emit_byte(0x28);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_sub_b,(RW1 d, R1 s))

LOWFUNC(WRITE,NONE,2,raw_cmp_l,(R4 d, R4 s))
{
    emit_byte(0x39);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_cmp_l,(R4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_cmp_l_ri,(R4 r, IMM i))
{
    emit_byte(0x81);
    emit_byte(0xf8+r);
    emit_long(i);
}
LENDFUNC(WRITE,NONE,2,raw_cmp_l_ri,(R4 r, IMM i))

LOWFUNC(WRITE,NONE,2,raw_cmp_w,(R2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x39);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_cmp_w,(R2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_cmp_b_ri,(R1 d, IMM i))
{
  emit_byte(0x80);
  emit_byte(0xf8+d);
  emit_byte(i);
}
LENDFUNC(WRITE,NONE,2,raw_cmp_b_ri,(R1 d, IMM i))

LOWFUNC(WRITE,NONE,2,raw_cmp_b,(R1 d, R1 s))
{
    emit_byte(0x38);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_cmp_b,(R1 d, R1 s))

LOWFUNC(WRITE,READ,4,raw_cmp_l_rm_indexed,(R4 d, IMM offset, R4 index, IMM factor))
{
    int fi;
    
    switch(factor) {
     case 1: fi=0; break;
     case 2: fi=1; break;
     case 4: fi=2; break;
     case 8: fi=3; break;
     default: abort();
    }
    emit_byte(0x39);
    emit_byte(0x04+8*d);
    emit_byte(5+8*index+0x40*fi);
    emit_long(offset);
}
LENDFUNC(WRITE,READ,4,raw_cmp_l_rm_indexed,(R4 d, IMM offset, R4 index, IMM factor))

LOWFUNC(WRITE,NONE,2,raw_xor_l,(RW4 d, R4 s))
{
    emit_byte(0x31);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_xor_l,(RW4 d, R4 s))

LOWFUNC(WRITE,NONE,2,raw_xor_w,(RW2 d, R2 s))
{
    emit_byte(0x66);
    emit_byte(0x31);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_xor_w,(RW2 d, R2 s))

LOWFUNC(WRITE,NONE,2,raw_xor_b,(RW1 d, R1 s))
{
    emit_byte(0x30);
    emit_byte(0xc0+8*s+d);
}
LENDFUNC(WRITE,NONE,2,raw_xor_b,(RW1 d, R1 s))

LOWFUNC(WRITE,RMW,2,raw_sub_l_mi,(MEMRW d, IMM s))
{
    emit_byte(0x81);
    emit_byte(0x2d);
    emit_long(d);
    emit_long(s);
}
LENDFUNC(WRITE,RMW,2,raw_sub_l_mi,(MEMRW d, IMM s))

LOWFUNC(WRITE,READ,2,raw_cmp_l_mi,(MEMR d, IMM s))
{
    emit_byte(0x81);
    emit_byte(0x3d);
    emit_long(d);
    emit_long(s);
}
LENDFUNC(WRITE,READ,2,raw_cmp_l_mi,(MEMR d, IMM s))

LOWFUNC(NONE,NONE,2,raw_xchg_l_rr,(RW4 r1, RW4 r2))
{
    emit_byte(0x87);
    emit_byte(0xc0+8*r1+r2);
}
LENDFUNC(NONE,NONE,2,raw_xchg_l_rr,(RW4 r1, RW4 r2))

LOWFUNC(READ,WRITE,0,raw_pushfl,(void))
{
    emit_byte(0x9c);
}
LENDFUNC(READ,WRITE,0,raw_pushfl,(void))

LOWFUNC(WRITE,READ,0,raw_popfl,(void))
{
    emit_byte(0x9d);
}
LENDFUNC(WRITE,READ,0,raw_popfl,(void))

/*************************************************************************
 * Unoptimizable stuff --- jump                                          *
 *************************************************************************/

static __inline__ void raw_call_r(R4 r)
{
    lopt_emit_all();
    emit_byte(0xff);
    emit_byte(0xd0+r);
}

static __inline__ void raw_jmp_r(R4 r)
{
    lopt_emit_all();
    emit_byte(0xff);
    emit_byte(0xe0+r);
}

static __inline__ void raw_jmp_m_indexed(uae_u32 base, uae_u32 r, uae_u32 m)
{
    int mu;
    switch(m) {
     case 1: mu=0; break;
     case 2: mu=1; break;
     case 4: mu=2; break;
     case 8: mu=3; break;
     default: abort();
    }
    lopt_emit_all();
    emit_byte(0xff);
    emit_byte(0x24);
    emit_byte(0x05+8*r+0x40*mu);
    emit_long(base);
}

static __inline__ void raw_jmp_m(uae_u32 base)
{
    lopt_emit_all();
    emit_byte(0xff);
    emit_byte(0x25);
    emit_long(base);
}


static __inline__ void raw_call(uae_u32 t)
{
    lopt_emit_all();
    emit_byte(0xe8);
    emit_long(t-(uae_u32)target-4);
}

static __inline__ void raw_jmp(uae_u32 t)
{
    lopt_emit_all();
    emit_byte(0xe9);
    emit_long(t-(uae_u32)target-4);
}

static __inline__ void raw_jl(uae_u32 t)
{
    lopt_emit_all();
    emit_byte(0x0f);
    emit_byte(0x8c);
    emit_long(t-(uae_u32)target-4);
}

static __inline__ void raw_jz(uae_u32 t)
{
    lopt_emit_all();
    emit_byte(0x0f);
    emit_byte(0x84);
    emit_long(t-(uae_u32)target-4);
}

static __inline__ void raw_jnz(uae_u32 t)
{
    lopt_emit_all();
    emit_byte(0x0f);
    emit_byte(0x85);
    emit_long(t-(uae_u32)target-4);
}

static __inline__ void raw_jnz_l_oponly(void)
{
    lopt_emit_all();
    emit_byte(0x0f); 
    emit_byte(0x85); 
}

static __inline__ void raw_jcc_l_oponly(int cc)
{
    lopt_emit_all();
    emit_byte(0x0f); 
    emit_byte(0x80+cc); 
}

static __inline__ void raw_jnz_b_oponly(void)
{
    lopt_emit_all();
    emit_byte(0x75); 
}

static __inline__ void raw_jz_b_oponly(void)
{
    lopt_emit_all();
    emit_byte(0x74); 
}

static __inline__ void raw_jmp_l_oponly(void)
{
    lopt_emit_all();
    emit_byte(0xe9); 
}

static __inline__ void raw_jmp_b_oponly(void)
{
    lopt_emit_all();
    emit_byte(0xeb); 
}

static __inline__ void raw_ret(void)
{
    lopt_emit_all();
    emit_byte(0xc3);  
}

static __inline__ void raw_nop(void)
{
    lopt_emit_all();
    emit_byte(0x90);
}


/*************************************************************************
 * Flag handling, to and fro UAE flag register                           *
 *************************************************************************/


#define FLAG_NREG1 0  /* Set to -1 if any register will do */

static __inline__ void raw_flags_to_reg(int r)
{
  raw_lahf(0);  /* Most flags in AH */
  //raw_setcc(r,0); /* V flag in AL */
  raw_setcc_m((uae_u32)live.state[FLAGTMP].mem,0); 
  
#if 1   /* Let's avoid those nasty partial register stalls */
  //raw_mov_b_mr((uae_u32)live.state[FLAGTMP].mem,r);
  raw_mov_b_mr(((uae_u32)live.state[FLAGTMP].mem)+1,r+4);
  //live.state[FLAGTMP].status=CLEAN;
  live.state[FLAGTMP].status=INMEM;
  live.state[FLAGTMP].realreg=-1;
  /* We just "evicted" FLAGTMP. */
  if (live.nat[r].nholds!=1) {
      /* Huh? */
      abort();
  }
  live.nat[r].nholds=0;
#endif
}

#define FLAG_NREG2 0  /* Set to -1 if any register will do */
static __inline__ void raw_reg_to_flags(int r)
{
  raw_cmp_b_ri(r,-127); /* set V */
  raw_sahf(0);
}

/* Apparently, there are enough instructions between flag store and
   flag reload to avoid the partial memory stall */
static __inline__ void raw_load_flagreg(uae_u32 target, uae_u32 r)
{
#if 1
    raw_mov_l_rm(target,(uae_u32)live.state[r].mem);
#else
    raw_mov_b_rm(target,(uae_u32)live.state[r].mem);
    raw_mov_b_rm(target+4,((uae_u32)live.state[r].mem)+1);
#endif
}

/* FLAGX is byte sized, and we *do* write it at that size */
static __inline__ void raw_load_flagx(uae_u32 target, uae_u32 r)
{
    if (live.nat[target].canbyte)
	raw_mov_b_rm(target,(uae_u32)live.state[r].mem);
    else if (live.nat[target].canword)
	raw_mov_w_rm(target,(uae_u32)live.state[r].mem);
    else
	raw_mov_l_rm(target,(uae_u32)live.state[r].mem);
}

#define NATIVE_FLAG_Z 0x40
#define NATIVE_CC_EQ  4
static __inline__ void raw_flags_set_zero(int f, int r, int t)
{
    // FIXME: this is really suboptimal
    raw_pushfl();
    raw_pop_l_r(f);
    raw_and_l_ri(f,~NATIVE_FLAG_Z);
    raw_test_l_rr(r,r);
    raw_mov_l_ri(r,0);
    raw_mov_l_ri(t,NATIVE_FLAG_Z);
    raw_cmov_l_rr(r,t,NATIVE_CC_EQ);
    raw_or_l(f,r);
    raw_push_l_r(f);
    raw_popfl();
}

static __inline__ void raw_inc_sp(int off)
{
    raw_add_l_ri(4,off);
}

/*************************************************************************
 * Handling mistaken direct memory access                                *
 *************************************************************************/


#ifdef NATMEM_OFFSET
#ifdef _WIN32 // %%% BRIAN KING WAS HERE %%%
#include <winbase.h>
#else
#include <asm/sigcontext.h>
#endif
#include <signal.h>

#define SIG_READ 1
#define SIG_WRITE 2

static int in_handler=0;
static uae_u8 *veccode;

#ifdef _WIN32
int EvalException ( LPEXCEPTION_POINTERS blah, int n_except )
{
    PEXCEPTION_RECORD pExceptRecord = NULL;
    PCONTEXT          pContext = NULL;

    uae_u8* i = NULL;
    uae_u32 addr = 0;
    int r=-1;
    int size=4;
    int dir=-1;
    int len=0;
    int j;

    if( n_except != STATUS_ACCESS_VIOLATION || !canbang)
        return EXCEPTION_CONTINUE_SEARCH;

    pExceptRecord = blah->ExceptionRecord;
    pContext = blah->ContextRecord;

    if( pContext )
    {
	i = (uae_u8 *)(pContext->Eip);
    }
    if( pExceptRecord )
    {
	addr = (uae_u32)(pExceptRecord->ExceptionInformation[1]);
    }
#ifdef JIT_DEBUG
    write_log("JIT: fault address is 0x%x at 0x%x\n",addr,i);
#endif
    if (!canbang || !currprefs.cachesize) 
    {
#ifdef JIT_DEBUG
	write_log("JIT: Not happy! Canbang or cachesize is 0 in SIGSEGV handler!\n");
#endif
	return EXCEPTION_CONTINUE_SEARCH;
    }

    if (in_handler) 
	write_log("JIT: Argh --- Am already in a handler. Shouldn't happen!\n");
    
    if (canbang && i>=compiled_code && i<=current_compile_p) {
	if (*i==0x66) {
	    i++;
	    size=2;
	    len++;
	}
	
	switch(i[0]) {
	case 0x8a:
	    if ((i[1]&0xc0)==0x80) {
		r=(i[1]>>3)&7;
		dir=SIG_READ;
		size=1;
		len+=6;
		break;
	    }
	    break;
	case 0x88:
	    if ((i[1]&0xc0)==0x80) {
		r=(i[1]>>3)&7;
		dir=SIG_WRITE;
		size=1;
		len+=6;
		break;
	    }
	    break;
	case 0x8b:
	    switch(i[1]&0xc0) {
	    case 0x80:
		r=(i[1]>>3)&7;
		dir=SIG_READ;
		len+=6;
		break;
	    case 0x40:
		r=(i[1]>>3)&7;
		dir=SIG_READ;
		len+=3;
		break;
	    case 0x00:
		r=(i[1]>>3)&7;
		dir=SIG_READ;
		len+=2;
		break;
	    default: 
		break;
	    }
	    break;
	    case 0x89:
		switch(i[1]&0xc0) {
		case 0x80:
		    r=(i[1]>>3)&7;
		    dir=SIG_WRITE;
		    len+=6;
		    break;
		case 0x40:
		    r=(i[1]>>3)&7;
		    dir=SIG_WRITE;
		    len+=3;
		    break;
		case 0x00:
		    r=(i[1]>>3)&7;
		    dir=SIG_WRITE;
		    len+=2;
		    break;
		}
		break;
	}	
    }
    
    if (r!=-1) { 
	void* pr=NULL;
#ifdef JIT_DEBUG
	write_log("register was %d, direction was %d, size was %d\n",r,dir,size);
#endif

	switch(r) {
	case 0: pr=&(pContext->Eax); break;
	case 1: pr=&(pContext->Ecx); break;
	case 2: pr=&(pContext->Edx); break;
	case 3: pr=&(pContext->Ebx); break;
	case 4: pr=(size>1)?NULL:(((uae_u8*)&(pContext->Eax))+1); break;
	case 5: pr=(size>1)?
		    (void*)(&(pContext->Ebp)):
	    (void*)(((uae_u8*)&(pContext->Ecx))+1); break;
	case 6: pr=(size>1)?
		    (void*)(&(pContext->Esi)):
	    (void*)(((uae_u8*)&(pContext->Edx))+1); break;
	case 7: pr=(size>1)?
		    (void*)(&(pContext->Edi)):
	    (void*)(((uae_u8*)&(pContext->Ebx))+1); break;
	default: abort();
	}
	if (pr) {
	    blockinfo* bi;
	    
	    if (currprefs.comp_oldsegv) {
		addr-=NATMEM_OFFSET;
		
		if ((addr>=0x10000000 && addr<0x40000000) ||
		    (addr>=0x50000000)) {
#ifdef JIT_DEBUG
		    write_log("Suspicious address 0x%x in SEGV handler.\n",addr);
#endif
	        }
		if (dir==SIG_READ) {
		    switch(size) {
		    case 1: *((uae_u8*)pr)=get_byte(addr); break;
		    case 2: *((uae_u16*)pr)=swap16(get_word(addr)); break;
		    case 4: *((uae_u32*)pr)=swap32(get_long(addr)); break;
		    default: abort();
		    }
		}
		else { /* write */
		    switch(size) {
		    case 1: put_byte(addr,*((uae_u8*)pr)); break;
		    case 2: put_word(addr,swap16(*((uae_u16*)pr))); break;
		    case 4: put_long(addr,swap32(*((uae_u32*)pr))); break;
		    default: abort();
		    }
		}
#ifdef JIT_DEBUG
		write_log("Handled one access!\n");
#endif
		fflush(stdout);
		segvcount++;
		pContext->Eip+=len;
	    }
	    else {
		void* tmp=target;
		int i;
		uae_u8 vecbuf[5];
		
		addr-=NATMEM_OFFSET;
		
		if ((addr>=0x10000000 && addr<0x40000000) ||
		    (addr>=0x50000000)) {
#ifdef JIT_DEBUG
		    write_log("Suspicious address 0x%x in SEGV handler.\n",addr);
#endif
	        }
	
		target=(uae_u8*)pContext->Eip;
		for (i=0;i<5;i++)
		    vecbuf[i]=target[i];
		emit_byte(0xe9);
		emit_long((uae_u32)veccode-(uae_u32)target-4);
#ifdef JIT_DEBUG

		write_log("Create jump to %p\n",veccode);
		write_log("Handled one access!\n");
#endif
		segvcount++;
		
		target=veccode;
		
		if (dir==SIG_READ) {
		    switch(size) {
		    case 1: raw_mov_b_ri(r,get_byte(addr)); break;
		    case 2: raw_mov_w_ri(r,swap16(get_word(addr))); break;
		    case 4: raw_mov_l_ri(r,swap32(get_long(addr))); break;
		    default: abort();
		    }
		}
		else { /* write */
		    switch(size) {
		    case 1: put_byte(addr,*((uae_u8*)pr)); break;
		    case 2: put_word(addr,swap16(*((uae_u16*)pr))); break;
		    case 4: put_long(addr,swap32(*((uae_u32*)pr))); break;
		    default: abort();
		    }
		}
		for (i=0;i<5;i++)
		    raw_mov_b_mi(pContext->Eip+i,vecbuf[i]);
		raw_mov_l_mi((uae_u32)&in_handler,0);
		emit_byte(0xe9);
		emit_long(pContext->Eip+len-(uae_u32)target-4);
		in_handler=1;
		target=tmp;
	    }
	    bi=active;
	    while (bi) {
		if (bi->handler && 
		    (uae_u8*)bi->direct_handler<=i &&
		    (uae_u8*)bi->nexthandler>i) {
#ifdef JIT_DEBUG
		    write_log("deleted trigger (%p<%p<%p) %p\n",
			bi->handler,
			i,
			bi->nexthandler,
			bi->pc_p);
#endif
		    invalidate_block(bi);
		    raise_in_cl_list(bi);
		    set_special(0);
		    return EXCEPTION_CONTINUE_EXECUTION;
		}
		bi=bi->next;
	    }
	    /* Not found in the active list. Might be a rom routine that
	    is in the dormant list */
	    bi=dormant;
	    while (bi) {
		if (bi->handler && 
		    (uae_u8*)bi->direct_handler<=i &&
		    (uae_u8*)bi->nexthandler>i) {
#ifdef JIT_DEBUG
		    write_log("deleted trigger (%p<%p<%p) %p\n",
			bi->handler,
			i,
			bi->nexthandler,
			bi->pc_p);
#endif
		    invalidate_block(bi);
		    raise_in_cl_list(bi);
		    set_special(0);
		    return EXCEPTION_CONTINUE_EXECUTION;
		}
		bi=bi->next;
	    }
#ifdef JIT_DEBUG
	    write_log("Huh? Could not find trigger!\n");
#endif
	    return EXCEPTION_CONTINUE_EXECUTION;
	}
    }
    write_log("JIT: Can't handle access!\n");
    if( i )
    {
	for (j=0;j<10;j++) {
	    write_log("JIT: instruction byte %2d is 0x%02x\n",j,i[j]);
	}
    }
#if 0
    write_log("Please send the above info (starting at \"fault address\") to\n"
	   "bmeyer@csse.monash.edu.au\n"
	   "This shouldn't happen ;-)\n");
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void vec(int x, struct sigcontext sc)
{
    uae_u8* i=(uae_u8*)sc.eip;
    uae_u32 addr=sc.cr2;
    int r=-1;
    int size=4;
    int dir=-1;
    int len=0;
    int j;
    
    write_log("fault address is %08x at %08x\n",sc.cr2,sc.eip);
    if (!canbang) 
	write_log("Not happy! Canbang is 0 in SIGSEGV handler!\n");
    if (in_handler) 
	write_log("Argh --- Am already in a handler. Shouldn't happen!\n");

    if (canbang && i>=compiled_code && i<=current_compile_p) {
	if (*i==0x66) {
	    i++;
	    size=2;
	    len++;
	}
	
	switch(i[0]) {
	 case 0x8a:
	    if ((i[1]&0xc0)==0x80) {
		r=(i[1]>>3)&7;
		dir=SIG_READ;
		size=1;
		len+=6;
		break;
	    }
	    break;
	 case 0x88:
	    if ((i[1]&0xc0)==0x80) {
		r=(i[1]>>3)&7;
		dir=SIG_WRITE;
		size=1;
		len+=6;
		break;
	    }
	    break;

	 case 0x8b:
	   switch(i[1]&0xc0) {
	   case 0x80:
	     r=(i[1]>>3)&7;
	     dir=SIG_READ;
	     len+=6;
	     break;
	   case 0x40:
	     r=(i[1]>>3)&7;
	     dir=SIG_READ;
	     len+=3;
	     break;
	   case 0x00:
	     r=(i[1]>>3)&7;
	     dir=SIG_READ;
	     len+=2;
	     break;
	   default: 
	     break;
	   }
	   break;
	    
	 case 0x89:
	   switch(i[1]&0xc0) {
	   case 0x80:
	     r=(i[1]>>3)&7;
	     dir=SIG_WRITE;
	     len+=6;
	     break;
	   case 0x40:
	     r=(i[1]>>3)&7;
	     dir=SIG_WRITE;
	     len+=3;
	     break;
	   case 0x00:
	     r=(i[1]>>3)&7;
	     dir=SIG_WRITE;
	     len+=2;
	     break;
	   }
	   break;
	}	
    }

    if (r!=-1) { 
	void* pr=NULL;
	write_log("register was %d, direction was %d, size was %d\n",r,dir,size);
	
	switch(r) {
	 case 0: pr=&(sc.eax); break;
	 case 1: pr=&(sc.ecx); break;
	 case 2: pr=&(sc.edx); break;
	 case 3: pr=&(sc.ebx); break;
	 case 4: pr=(size>1)?NULL:(((uae_u8*)&(sc.eax))+1); break;
	 case 5: pr=(size>1)?
		     (void*)(&(sc.ebp)):
			 (void*)(((uae_u8*)&(sc.ecx))+1); break;
	 case 6: pr=(size>1)?
		     (void*)(&(sc.esi)):
			 (void*)(((uae_u8*)&(sc.edx))+1); break;
	 case 7: pr=(size>1)?
		     (void*)(&(sc.edi)):
			 (void*)(((uae_u8*)&(sc.ebx))+1); break;
	 default: abort();
	}
	if (pr) {
	    blockinfo* bi;

	    if (currprefs.comp_oldsegv) {
	    addr-=NATMEM_OFFSET;
		
	    if ((addr>=0x10000000 && addr<0x40000000) ||
		(addr>=0x50000000)) {
		write_log("Suspicious address in %x SEGV handler.\n",addr);
	    }
	    if (dir==SIG_READ) {
		switch(size) {
		 case 1: *((uae_u8*)pr)=get_byte(addr); break;
		 case 2: *((uae_u16*)pr)=get_word(addr); break;
		 case 4: *((uae_u32*)pr)=get_long(addr); break;
		 default: abort();
		}
	    }
	    else { /* write */
		switch(size) {
		 case 1: put_byte(addr,*((uae_u8*)pr)); break;
		 case 2: put_word(addr,*((uae_u16*)pr)); break;
		 case 4: put_long(addr,*((uae_u32*)pr)); break;
		 default: abort();
		}
	    }
	    write_log("Handled one access!\n");
	    fflush(stdout);
	    segvcount++;
	    sc.eip+=len;
	    }
	    else {
		void* tmp=target;
		int i;
		uae_u8 vecbuf[5];
		
		addr-=NATMEM_OFFSET;
		
		if ((addr>=0x10000000 && addr<0x40000000) ||
		    (addr>=0x50000000)) {
		    write_log("Suspicious address 0x%x in SEGV handler.\n",addr);
		}
		
		target=(uae_u8*)sc.eip;
		for (i=0;i<5;i++)
		    vecbuf[i]=target[i];
		emit_byte(0xe9);
		emit_long((uae_u32)veccode-(uae_u32)target-4);
		write_log("Create jump to %p\n",veccode);

		write_log("Handled one access!\n");
		fflush(stdout);
		segvcount++;
		
		target=veccode;

		if (dir==SIG_READ) {
		    switch(size) {
		     case 1: raw_mov_b_ri(r,get_byte(addr)); break;
		     case 2: raw_mov_w_ri(r,get_word(addr)); break;
		     case 4: raw_mov_l_ri(r,get_long(addr)); break;
		     default: abort();
		    }
		}
		else { /* write */
		    switch(size) {
		     case 1: put_byte(addr,*((uae_u8*)pr)); break;
		     case 2: put_word(addr,*((uae_u16*)pr)); break;
		     case 4: put_long(addr,*((uae_u32*)pr)); break;
		     default: abort();
		    }
		}
		for (i=0;i<5;i++)
		    raw_mov_b_mi(sc.eip+i,vecbuf[i]);
		raw_mov_l_mi((uae_u32)&in_handler,0);
		emit_byte(0xe9);
		emit_long(sc.eip+len-(uae_u32)target-4);
		in_handler=1;
		target=tmp;
	    }
	    bi=active;
	    while (bi) {
		if (bi->handler && 
		    (uae_u8*)bi->direct_handler<=i &&
		    (uae_u8*)bi->nexthandler>i) {
		    write_log("deleted trigger (%p<%p<%p) %p\n",
			      bi->handler,
			      i,
			      bi->nexthandler,
			      bi->pc_p);
		    invalidate_block(bi);
		    raise_in_cl_list(bi);
		    set_special(0);
		    return;
		}
		bi=bi->next;
	    }
	    /* Not found in the active list. Might be a rom routine that
	       is in the dormant list */
	    bi=dormant;
	    while (bi) {
		if (bi->handler && 
		    (uae_u8*)bi->direct_handler<=i &&
		    (uae_u8*)bi->nexthandler>i) {
		    write_log("deleted trigger (%p<%p<%p) %p\n",
			      bi->handler,
			      i,
			      bi->nexthandler,
			      bi->pc_p);
		    invalidate_block(bi);
		    raise_in_cl_list(bi);
		    set_special(0);
		    return;
		}
		bi=bi->next;
	    }
	    write_log("Huh? Could not find trigger!\n");
	    return;
	}
    }
    write_log("Can't handle access!\n");
    for (j=0;j<10;j++) {
	write_log("instruction byte %2d is %02x\n",j,i[j]);
    }
#if 0
    write_log("Please send the above info (starting at \"fault address\") to\n"
	   "bmeyer@csse.monash.edu.au\n"
	   "This shouldn't happen ;-)\n");
    fflush(stdout);
#endif
    signal(SIGSEGV,SIG_DFL);  /* returning here will cause a "real" SEGV */
}
#endif
#endif

/*************************************************************************
 * Checking for CPU features                                             *
 *************************************************************************/

typedef struct {
    uae_u32 eax;
    uae_u32 ecx;
    uae_u32 edx;
    uae_u32 ebx;
} x86_regs;


/* This could be so much easier if it could make assumptions about the
   compiler... */

static uae_u32 cpuid_ptr;
static uae_u32 cpuid_level;

static x86_regs cpuid(uae_u32 level)
{
    x86_regs answer;
    uae_u8 *cpuid_space;
    void* tmp=get_target();

    cpuid_ptr=(uae_u32)&answer;
    cpuid_level=level;

    cpuid_space = cache_alloc (256);
    set_target(cpuid_space);
    raw_push_l_r(0); /* eax */
    raw_push_l_r(1); /* ecx */
    raw_push_l_r(2); /* edx */
    raw_push_l_r(3); /* ebx */
    raw_push_l_r(7); /* edi */
    raw_mov_l_rm(0,(uae_u32)&cpuid_level);
    raw_cpuid(0);
    raw_mov_l_rm(7,(uae_u32)&cpuid_ptr);
    raw_mov_l_Rr(7,0,0);
    raw_mov_l_Rr(7,1,4);
    raw_mov_l_Rr(7,2,8);
    raw_mov_l_Rr(7,3,12);
    raw_pop_l_r(7);
    raw_pop_l_r(3);
    raw_pop_l_r(2);
    raw_pop_l_r(1);
    raw_pop_l_r(0);
    raw_ret();
    set_target(tmp);

    ((cpuop_func*)cpuid_space)(0);
    cache_free (cpuid_space);
    return answer;
}

static void raw_init_cpu(void)
{
    x86_regs x;
    uae_u32 maxlev;
    
    x=cpuid(0);
    maxlev=x.eax;
    write_log("Max CPUID level=%d Processor is %c%c%c%c%c%c%c%c%c%c%c%c\n",
	      maxlev,
	      x.ebx,
	      x.ebx>>8,
	      x.ebx>>16,
	      x.ebx>>24,
	      x.edx,
	      x.edx>>8,
	      x.edx>>16,
	      x.edx>>24,
	      x.ecx,
	      x.ecx>>8,
	      x.ecx>>16,
	      x.ecx>>24
	      );
    have_rat_stall=(x.ecx==0x6c65746e);

    if (maxlev>=1) {
	x=cpuid(1);
	if (x.edx&(1<<15)) 
	    have_cmov=1;
    }
    have_rat_stall=1;
#if 0
    if (!have_cmov)
	have_rat_stall=0;
#endif
#if 0
    write_log ("have_cmov=%d, avoid_cmov=%d, have_rat_stall=%d\n",
	       have_cmov,currprefs.avoid_cmov,have_rat_stall);
    if (currprefs.avoid_cmov) {
	write_log("Disabling cmov use despite processor claiming to support it!\n");
	have_cmov=0;
    }
#else
    /* Dear Bernie, I don't want to keep around options which are useless, and not
       represented in the GUI anymore... Is this okay? */
    write_log ("have_cmov=%d, have_rat_stall=%d\n", have_cmov, have_rat_stall);
#endif
#if 0   /* For testing of non-cmov code! */
    have_cmov=0;
#endif
#if 0 /* It appears that partial register writes are a bad idea even on
	 AMD K7 cores, even though they are not supposed to have the
	 dreaded rat stall. Why? Anyway, that's why we lie about it ;-) */
    if (have_cmov)
      have_rat_stall=1;
#endif
}

/*************************************************************************
 * FPU stuff                                                             *
 *************************************************************************/


static __inline__ void raw_fp_init(void)
{
    int i;
    
    for (i=0;i<N_FREGS;i++)
	live.spos[i]=-2;
    live.tos=-1;  /* Stack is empty */
}

static __inline__ void raw_fp_cleanup_drop(void)
{
#if 0
    /* using FINIT instead of popping all the entries.
       Seems to have side effects --- there is display corruption in
       Quake when this is used */
    if (live.tos>1) {
	emit_byte(0x9b);
	emit_byte(0xdb);
	emit_byte(0xe3);
	live.tos=-1;
    }
#endif
    while (live.tos>=1) {
	emit_byte(0xde);
	emit_byte(0xd9);
	live.tos-=2;
    }
    while (live.tos>=0) {
	emit_byte(0xdd);
	emit_byte(0xd8);
	live.tos--;
    }
    raw_fp_init();
}

static __inline__ void make_tos(int r)
{
    int p,q;

    if (live.spos[r]<0) { /* Register not yet on stack */
	emit_byte(0xd9);
	emit_byte(0xe8);  /* Push '1' on the stack, just to grow it */
	live.tos++;
	live.spos[r]=live.tos;
	live.onstack[live.tos]=r;
	return;
    }
    /* Register is on stack */
    if (live.tos==live.spos[r])
	return;
    p=live.spos[r];
    q=live.onstack[live.tos];

    emit_byte(0xd9);
    emit_byte(0xc8+live.tos-live.spos[r]);  /* exchange it with top of stack */
    live.onstack[live.tos]=r;
    live.spos[r]=live.tos;
    live.onstack[p]=q;
    live.spos[q]=p;
}

static __inline__ void make_tos2(int r, int r2)
{
    int q;

    make_tos(r2); /* Put the reg that's supposed to end up in position2
		     on top */

    if (live.spos[r]<0) { /* Register not yet on stack */
	make_tos(r); /* This will extend the stack */
	return;
    }
    /* Register is on stack */
    emit_byte(0xd9);
    emit_byte(0xc9); /* Move r2 into position 2 */

    q=live.onstack[live.tos-1];
    live.onstack[live.tos]=q;
    live.spos[q]=live.tos;
    live.onstack[live.tos-1]=r2;
    live.spos[r2]=live.tos-1;

    make_tos(r); /* And r into 1 */
}

static __inline__ int stackpos(int r)
{
    if (live.spos[r]<0)
	abort();
    if (live.tos<live.spos[r]) {
	printf("Looking for spos for fnreg %d\n",r);
	abort();
    }
    return live.tos-live.spos[r];
}

static __inline__ void usereg(int r)
{
    if (live.spos[r]<0)
	make_tos(r);
}

/* This is called with one FP value in a reg *above* tos, which it will
   pop off the stack if necessary */
static __inline__ void tos_make(int r)
{
    if (live.spos[r]<0) {
	live.tos++;
	live.spos[r]=live.tos;
	live.onstack[live.tos]=r;
	return;
    }
    emit_byte(0xdd);
    emit_byte(0xd8+(live.tos+1)-live.spos[r]);  /* store top of stack in reg, 
					 and pop it*/
}
    
	
LOWFUNC(NONE,WRITE,2,raw_fmov_mr,(MEMW m, FR r))
{
    make_tos(r);
    emit_byte(0xdd);
    emit_byte(0x15);
    emit_long(m);
}
LENDFUNC(NONE,WRITE,2,raw_fmov_mr,(MEMW m, FR r))

LOWFUNC(NONE,WRITE,2,raw_fmov_mr_drop,(MEMW m, FR r))
{
    make_tos(r);
    emit_byte(0xdd);
    emit_byte(0x1d);
    emit_long(m);
    live.onstack[live.tos]=-1;
    live.tos--;
    live.spos[r]=-2;
}
LENDFUNC(NONE,WRITE,2,raw_fmov_mr,(MEMW m, FR r))

LOWFUNC(NONE,READ,2,raw_fmov_rm,(FW r, MEMR m))
{
    emit_byte(0xdd);
    emit_byte(0x05);
    emit_long(m);
    tos_make(r);
}
LENDFUNC(NONE,READ,2,raw_fmov_rm,(FW r, MEMR m))

LOWFUNC(NONE,READ,2,raw_fmovi_rm,(FW r, MEMR m))
{
    emit_byte(0xdb);
    emit_byte(0x05);
    emit_long(m);
    tos_make(r);
}
LENDFUNC(NONE,READ,2,raw_fmovi_rm,(FW r, MEMR m))

LOWFUNC(NONE,WRITE,2,raw_fmovi_mr,(MEMW m, FR r))
{
    make_tos(r);
    emit_byte(0xdb);
    emit_byte(0x15);
    emit_long(m);
}
LENDFUNC(NONE,WRITE,2,raw_fmovi_mr,(MEMW m, FR r))

LOWFUNC(NONE,READ,2,raw_fmovs_rm,(FW r, MEMR m))
{
    emit_byte(0xd9);
    emit_byte(0x05);
    emit_long(m);
    tos_make(r);
}
LENDFUNC(NONE,READ,2,raw_fmovs_rm,(FW r, MEMR m))

LOWFUNC(NONE,WRITE,2,raw_fmovs_mr,(MEMW m, FR r))
{
    make_tos(r);
    emit_byte(0xd9);
    emit_byte(0x15);
    emit_long(m);
}
LENDFUNC(NONE,WRITE,2,raw_fmovs_mr,(MEMW m, FR r))

LOWFUNC(NONE,WRITE,2,raw_fmov_ext_mr,(MEMW m, FR r))
{
    int rs;

    /* Stupid x87 can't write a long double to mem without popping the 
       stack! */
    usereg(r);
    rs=stackpos(r);
    emit_byte(0xd9);     /* Get a copy to the top of stack */
    emit_byte(0xc0+rs);

    emit_byte(0xdb);  /* store and pop it */
    emit_byte(0x3d);
    emit_long(m);
}
LENDFUNC(NONE,WRITE,2,raw_fmov_ext_mr,(MEMW m, FR r))

LOWFUNC(NONE,WRITE,2,raw_fmov_ext_mr_drop,(MEMW m, FR r))
{
    int rs;

    make_tos(r);
    emit_byte(0xdb);  /* store and pop it */
    emit_byte(0x3d);
    emit_long(m);
    live.onstack[live.tos]=-1;
    live.tos--;
    live.spos[r]=-2;
}
LENDFUNC(NONE,WRITE,2,raw_fmov_ext_mr,(MEMW m, FR r))

LOWFUNC(NONE,READ,2,raw_fmov_ext_rm,(FW r, MEMR m))
{
    emit_byte(0xdb);
    emit_byte(0x2d);
    emit_long(m);
    tos_make(r);
}
LENDFUNC(NONE,READ,2,raw_fmov_ext_rm,(FW r, MEMR m))

LOWFUNC(NONE,NONE,1,raw_fmov_pi,(FW r))
{
    emit_byte(0xd9);
    emit_byte(0xeb);
    tos_make(r);
}
LENDFUNC(NONE,NONE,1,raw_fmov_pi,(FW r))

LOWFUNC(NONE,NONE,1,raw_fmov_log10_2,(FW r))
{
    emit_byte(0xd9);
    emit_byte(0xec);
    tos_make(r);
}
LENDFUNC(NONE,NONE,1,raw_fmov_log10_2,(FW r))

LOWFUNC(NONE,NONE,1,raw_fmov_log2_e,(FW r))
{
    emit_byte(0xd9);
    emit_byte(0xea);
    tos_make(r);
}
LENDFUNC(NONE,NONE,1,raw_fmov_log2_e,(FW r))

LOWFUNC(NONE,NONE,1,raw_fmov_loge_2,(FW r))
{
    emit_byte(0xd9);
    emit_byte(0xed);
    tos_make(r);
}
LENDFUNC(NONE,NONE,1,raw_fmov_loge_2,(FW r))

LOWFUNC(NONE,NONE,1,raw_fmov_1,(FW r))
{
    emit_byte(0xd9);
    emit_byte(0xe8);
    tos_make(r);
}
LENDFUNC(NONE,NONE,1,raw_fmov_1,(FW r))

LOWFUNC(NONE,NONE,1,raw_fmov_0,(FW r))
{
    emit_byte(0xd9);
    emit_byte(0xee);
    tos_make(r);
}
LENDFUNC(NONE,NONE,1,raw_fmov_0,(FW r))

LOWFUNC(NONE,NONE,2,raw_fmov_rr,(FW d, FR s))
{
    int ds;

    usereg(s);
    ds=stackpos(s);
    if (ds==0 && live.spos[d]>=0) {
	/* source is on top of stack, and we already have the dest */
	int dd=stackpos(d);
	emit_byte(0xdd);
	emit_byte(0xd0+dd);
    }
    else {
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source on tos */
	tos_make(d); /* store to destination, pop if necessary */
    }
}
LENDFUNC(NONE,NONE,2,raw_fmov_rr,(FW d, FR s))

LOWFUNC(NONE,READ,4,raw_fldcw_m_indexed,(R4 index, IMM base))
{
    emit_byte(0xd9);
    emit_byte(0xa8+index);
    emit_long(base);
}
LENDFUNC(NONE,READ,4,raw_fldcw_m_indexed,(R4 index, IMM base))


LOWFUNC(NONE,NONE,2,raw_fsqrt_rr,(FW d, FR s))
{
    int ds;

    if (d!=s) {
	usereg(s);
	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source */
	emit_byte(0xd9);
	emit_byte(0xfa); /* take square root */
	tos_make(d); /* store to destination */
    }
    else {
	make_tos(d);
	emit_byte(0xd9);
	emit_byte(0xfa); /* take square root */
    }	
}
LENDFUNC(NONE,NONE,2,raw_fsqrt_rr,(FW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fabs_rr,(FW d, FR s))
{
    int ds;

    if (d!=s) {
	usereg(s);
	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source */
	emit_byte(0xd9);
	emit_byte(0xe1); /* take fabs */
	tos_make(d); /* store to destination */
    }
    else {
	make_tos(d);
	emit_byte(0xd9);
	emit_byte(0xe1); /* take fabs */
    }	
}
LENDFUNC(NONE,NONE,2,raw_fabs_rr,(FW d, FR s))

LOWFUNC(NONE,NONE,2,raw_frndint_rr,(FW d, FR s))
{
    int ds;

    if (d!=s) {
	usereg(s);
	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source */
	emit_byte(0xd9);
	emit_byte(0xfc); /* take frndint */
	tos_make(d); /* store to destination */
    }
    else {
	make_tos(d);
	emit_byte(0xd9);
	emit_byte(0xfc); /* take frndint */
    }	
}
LENDFUNC(NONE,NONE,2,raw_frndint_rr,(FW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fcos_rr,(FW d, FR s))
{
    int ds;

    if (d!=s) {
	usereg(s);
	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source */
	emit_byte(0xd9);
	emit_byte(0xff); /* take cos */
	tos_make(d); /* store to destination */
    }
    else {
	make_tos(d);
	emit_byte(0xd9);
	emit_byte(0xff); /* take cos */
    }	
}
LENDFUNC(NONE,NONE,2,raw_fcos_rr,(FW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fsin_rr,(FW d, FR s))
{
    int ds;

    if (d!=s) {
	usereg(s);
	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source */
	emit_byte(0xd9);
	emit_byte(0xfe); /* take sin */
	tos_make(d); /* store to destination */
    }
    else {
	make_tos(d);
	emit_byte(0xd9);
	emit_byte(0xfe); /* take sin */
    }	
}
LENDFUNC(NONE,NONE,2,raw_fsin_rr,(FW d, FR s))

double one=1;
LOWFUNC(NONE,NONE,2,raw_ftwotox_rr,(FW d, FR s))
{
    int ds;

    usereg(s);
    ds=stackpos(s);
    emit_byte(0xd9);
    emit_byte(0xc0+ds); /* duplicate source */

    emit_byte(0xd9);
    emit_byte(0xc0);  /* duplicate top of stack. Now up to 8 high */
    emit_byte(0xd9);
    emit_byte(0xfc);  /* rndint */
    emit_byte(0xd9);
    emit_byte(0xc9);  /* swap top two elements */
    emit_byte(0xd8);
    emit_byte(0xe1);  /* subtract rounded from original */
    emit_byte(0xd9);
    emit_byte(0xf0);  /* f2xm1 */
    emit_byte(0xdc);
    emit_byte(0x05);
    emit_long((uae_u32)&one);  /* Add '1' without using extra stack space */
    emit_byte(0xd9);
    emit_byte(0xfd);  /* and scale it */
    emit_byte(0xdd);
    emit_byte(0xd9);  /* take he rounded value off */
    tos_make(d); /* store to destination */
}
LENDFUNC(NONE,NONE,2,raw_ftwotox_rr,(FW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fetox_rr,(FW d, FR s))
{
    int ds;

    usereg(s);
    ds=stackpos(s);
    emit_byte(0xd9);
    emit_byte(0xc0+ds); /* duplicate source */
    emit_byte(0xd9);
    emit_byte(0xea);   /* fldl2e */
    emit_byte(0xde);
    emit_byte(0xc9);  /* fmulp --- multiply source by log2(e) */

    emit_byte(0xd9);
    emit_byte(0xc0);  /* duplicate top of stack. Now up to 8 high */
    emit_byte(0xd9);
    emit_byte(0xfc);  /* rndint */
    emit_byte(0xd9);
    emit_byte(0xc9);  /* swap top two elements */
    emit_byte(0xd8);
    emit_byte(0xe1);  /* subtract rounded from original */
    emit_byte(0xd9);
    emit_byte(0xf0);  /* f2xm1 */
    emit_byte(0xdc);
    emit_byte(0x05);
    emit_long((uae_u32)&one);  /* Add '1' without using extra stack space */
    emit_byte(0xd9);
    emit_byte(0xfd);  /* and scale it */
    emit_byte(0xdd);
    emit_byte(0xd9);  /* take he rounded value off */
    tos_make(d); /* store to destination */
}
LENDFUNC(NONE,NONE,2,raw_fetox_rr,(FW d, FR s))
 
LOWFUNC(NONE,NONE,2,raw_flog2_rr,(FW d, FR s))
{
    int ds;

    usereg(s);
    ds=stackpos(s);
    emit_byte(0xd9);
    emit_byte(0xc0+ds); /* duplicate source */
    emit_byte(0xd9);
    emit_byte(0xe8); /* push '1' */
    emit_byte(0xd9);
    emit_byte(0xc9); /* swap top two */
    emit_byte(0xd9);
    emit_byte(0xf1); /* take 1*log2(x) */
    tos_make(d); /* store to destination */
}
LENDFUNC(NONE,NONE,2,raw_flog2_rr,(FW d, FR s))


LOWFUNC(NONE,NONE,2,raw_fneg_rr,(FW d, FR s))
{
    int ds;

    if (d!=s) {
	usereg(s);
	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* duplicate source */
	emit_byte(0xd9);
	emit_byte(0xe0); /* take fchs */
	tos_make(d); /* store to destination */
    }
    else {
	make_tos(d);
	emit_byte(0xd9);
	emit_byte(0xe0); /* take fchs */
    }	
}
LENDFUNC(NONE,NONE,2,raw_fneg_rr,(FW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fadd_rr,(FRW d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    if (live.spos[s]==live.tos) {
	/* Source is on top of stack */
	ds=stackpos(d);
	emit_byte(0xdc);
	emit_byte(0xc0+ds); /* add source to dest*/
    }
    else {
	make_tos(d);
	ds=stackpos(s);
	
	emit_byte(0xd8);
	emit_byte(0xc0+ds); /* add source to dest*/
    }
}
LENDFUNC(NONE,NONE,2,raw_fadd_rr,(FRW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fsub_rr,(FRW d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    if (live.spos[s]==live.tos) {
	/* Source is on top of stack */
	ds=stackpos(d);
	emit_byte(0xdc);
	emit_byte(0xe8+ds); /* sub source from dest*/
    }
    else {
	make_tos(d);
	ds=stackpos(s);
	
	emit_byte(0xd8);
	emit_byte(0xe0+ds); /* sub src from dest */
    }
}
LENDFUNC(NONE,NONE,2,raw_fsub_rr,(FRW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fcmp_rr,(FR d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    make_tos(d);
    ds=stackpos(s);

    emit_byte(0xdd);
    emit_byte(0xe0+ds); /* cmp dest with source*/
}
LENDFUNC(NONE,NONE,2,raw_fcmp_rr,(FR d, FR s))

LOWFUNC(NONE,NONE,2,raw_fmul_rr,(FRW d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    if (live.spos[s]==live.tos) {
	/* Source is on top of stack */
	ds=stackpos(d);
	emit_byte(0xdc);
	emit_byte(0xc8+ds); /* mul dest by source*/
    }
    else {
	make_tos(d);
	ds=stackpos(s);
	
	emit_byte(0xd8);
	emit_byte(0xc8+ds); /* mul dest by source*/
    }
}
LENDFUNC(NONE,NONE,2,raw_fmul_rr,(FRW d, FR s))

LOWFUNC(NONE,NONE,2,raw_fdiv_rr,(FRW d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    if (live.spos[s]==live.tos) {
	/* Source is on top of stack */
	ds=stackpos(d);
	emit_byte(0xdc);
	emit_byte(0xf8+ds); /* div dest by source */
    }
    else {
	make_tos(d);
	ds=stackpos(s);
	
	emit_byte(0xd8);
	emit_byte(0xf0+ds); /* div dest by source*/
    }
}
LENDFUNC(NONE,NONE,2,raw_fdiv_rr,(FRW d, FR s))

LOWFUNC(NONE,NONE,2,raw_frem_rr,(FRW d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    make_tos2(d,s);
    ds=stackpos(s);

    if (ds!=1) {
	printf("Failed horribly in raw_frem_rr! ds is %d\n",ds);
	abort();
    }
    emit_byte(0xd9);
    emit_byte(0xf8); /* take rem from dest by source */
}
LENDFUNC(NONE,NONE,2,raw_frem_rr,(FRW d, FR s))

LOWFUNC(NONE,NONE,2,raw_frem1_rr,(FRW d, FR s))
{
    int ds;

    usereg(s);
    usereg(d);
    
    make_tos2(d,s);
    ds=stackpos(s);

    if (ds!=1) {
	printf("Failed horribly in raw_frem1_rr! ds is %d\n",ds);
	abort();
    }
    emit_byte(0xd9);
    emit_byte(0xf5); /* take rem1 from dest by source */
}
LENDFUNC(NONE,NONE,2,raw_frem1_rr,(FRW d, FR s))


LOWFUNC(NONE,NONE,1,raw_ftst_r,(FR r))
{
    make_tos(r);
    emit_byte(0xd9);  /* ftst */
    emit_byte(0xe4);
}
LENDFUNC(NONE,NONE,1,raw_ftst_r,(FR r))

static __inline__ void raw_fflags_into_flags(int r)
{
    int p;

    usereg(r);
    p=stackpos(r);

    emit_byte(0xd9);
    emit_byte(0xee); /* Push 0 */
    emit_byte(0xd9);
    emit_byte(0xc9+p); /* swap top two around */
    if (have_cmov) {
            // gb-- fucomi is for P6 cores only, not K6-2 then...
    emit_byte(0xdb);
    emit_byte(0xe9+p); /* fucomi them */
    }
    else {
            emit_byte(0xdd);
            emit_byte(0xe1+p); /* fucom them */
            emit_byte(0x9b);
            emit_byte(0xdf);
            emit_byte(0xe0); /* fstsw ax */
            raw_sahf(0); /* sahf */
    }
    emit_byte(0xdd);
    emit_byte(0xd9+p);  /* store value back, and get rid of 0 */
}
