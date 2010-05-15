/* This should eventually end up in machdep/, but for now, x86 is the
only target, and it's easier this way... */

/*************************************************************************
* Some basic information about the the target CPU                       *
*************************************************************************/

#define EAX_INDEX 0
#define ECX_INDEX 1
#define EDX_INDEX 2
#define EBX_INDEX 3
#define ESP_INDEX 4
#define EBP_INDEX 5
#define ESI_INDEX 6
#define EDI_INDEX 7
#if defined(__x86_64__)
#define R8_INDEX  8
#define R9_INDEX  9
#define R10_INDEX 10
#define R11_INDEX 11
#define R12_INDEX 12
#define R13_INDEX 13
#define R14_INDEX 14
#define R15_INDEX 15
#endif
/* XXX this has to match X86_Reg8H_Base + 4 */
#define AH_INDEX (0x10+4+EAX_INDEX)
#define CH_INDEX (0x10+4+ECX_INDEX)
#define DH_INDEX (0x10+4+EDX_INDEX)
#define BH_INDEX (0x10+4+EBX_INDEX)

/* The register in which subroutines return an integer return value */
#define REG_RESULT EAX_INDEX

/* The registers subroutines take their first and second argument in */
#if defined(_WIN32)
/* Handle the _fastcall parameters of ECX and EDX */
#define REG_PAR1 ECX_INDEX
#define REG_PAR2 EDX_INDEX
#elif defined(__x86_64__)
#define REG_PAR1 EDI_INDEX
#define REG_PAR2 ESI_INDEX
#else
#define REG_PAR1 EAX_INDEX
#define REG_PAR2 EDX_INDEX
#endif

#if defined(_WIN32)
#define REG_PC_PRE EAX_INDEX /* The register we use for preloading regs.pc_p */
#define REG_PC_TMP ECX_INDEX
#define SHIFTCOUNT_NREG ECX_INDEX  /* Register that can be used for shiftcount. -1 if any reg will do */
#else
#define REG_PC_PRE EAX_INDEX /* The register we use for preloading regs.pc_p */
#define REG_PC_TMP ECX_INDEX /* Another register that is not the above */
#define SHIFTCOUNT_NREG ECX_INDEX  /* Register that can be used for shiftcount. -1 if any reg will do */
#endif

#define MUL_NREG1 EAX_INDEX /* %eax will hold the low 32 bits after a 32x32 mul */
#define MUL_NREG2 EDX_INDEX /* %edx will hold the high 32 bits */

#define STACK_ALIGN		16
#define STACK_OFFSET	sizeof(void *)

uae_u8 always_used[]={4,0xff};
#if defined(__x86_64__)
uae_s8 can_byte[]={0,1,2,3,5,6,7,8,9,10,11,12,13,14,15,-1};
uae_s8 can_word[]={0,1,2,3,5,6,7,8,9,10,11,12,13,14,15,-1};
#else
uae_u8 can_byte[]={0,1,2,3,0xff};
uae_u8 can_word[]={0,1,2,3,5,6,7,0xff};
#endif

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

//#include "compemu_optimizer_x86.c"

STATIC_INLINE uae_u16 swap16(uae_u16 x)
{
	return ((x&0xff00)>>8)|((x&0x00ff)<<8);
}

STATIC_INLINE uae_u32 swap32(uae_u32 x)
{
	return ((x&0xff00)<<8)|((x&0x00ff)<<24)|((x&0xff0000)>>8)|((x&0xff000000)>>24);
}

STATIC_INLINE int isbyte(uae_s32 x)
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

	LOWFUNC(READ,NONE,3,raw_cmov_b_rr,(RW1 d, R1 s, IMM cc))
{
	/* replacement using branch and mov */
	int uncc=(cc^1);
	emit_byte(0x70+uncc);
	emit_byte(3);  /* skip next 2 bytes if not cc=true */
	emit_byte(0x88);
	emit_byte(0xc0+8*s+d);
}
LENDFUNC(READ,NONE,3,raw_cmov_b_rr,(RW1 d, R1 s, IMM cc))

	LOWFUNC(READ,NONE,3,raw_cmov_w_rr,(RW2 d, R2 s, IMM cc))
{
	if (have_cmov) {
		emit_byte(0x66);
		emit_byte(0x0f);
		emit_byte(0x40+cc);
		emit_byte(0xc0+8*d+s);
	}
	else { /* replacement using branch and mov */
		int uncc=(cc^1);
		emit_byte(0x70+uncc);
		emit_byte(3);  /* skip next 3 bytes if not cc=true */
		emit_byte(0x66);
		emit_byte(0x89);
		emit_byte(0xc0+8*s+d);
	}
}
LENDFUNC(READ,NONE,3,raw_cmov_w_rr,(RW2 d, R2 s, IMM cc))

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
#ifdef JIT_DEBUG
	if (d!=MUL_NREG1 || s!=MUL_NREG2) {
		write_log (L"JIT: Bad register in IMUL: d=%d, s=%d\n",d,s);
		abort();
	}
#endif
	emit_byte(0xf7);
	emit_byte(0xea);
}
LENDFUNC(NONE,NONE,2,raw_imul_64_32,(RW4 d, RW4 s))

	LOWFUNC(NONE,NONE,2,raw_mul_64_32,(RW4 d, RW4 s))
{
#ifdef JIT_DEBUG
	if (d!=MUL_NREG1 || s!=MUL_NREG2) {
		write_log (L"JIT: Bad register in MUL: d=%d, s=%d\n",d,s);
		abort();
	}
#endif
	emit_byte(0xf7);
	emit_byte(0xe2);
}
LENDFUNC(NONE,NONE,2,raw_mul_64_32,(RW4 d, RW4 s))

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

	LOWFUNC(NONE,READ,3,raw_mov_l_rrm_indexed,(W4 d, R4 baser, R4 index))
{
	emit_byte(0x8b);
	if (baser==5) {
		emit_byte(0x44+8*d);
		emit_byte(8*index+baser);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*d);
	emit_byte(8*index+baser);
}
LENDFUNC(NONE,READ,3,raw_mov_l_rrm_indexed,(W4 d, R4 baser, R4 index))

	LOWFUNC(NONE,READ,3,raw_mov_w_rrm_indexed,(W2 d, R4 baser, R4 index))
{
	emit_byte(0x66);
	emit_byte(0x8b);
	if (baser==5) {
		emit_byte(0x44+8*d);
		emit_byte(8*index+baser);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*d);
	emit_byte(8*index+baser);
}
LENDFUNC(NONE,READ,3,raw_mov_w_rrm_indexed,(W2 d, R4 baser, R4 index))

	LOWFUNC(NONE,READ,3,raw_mov_b_rrm_indexed,(W1 d, R4 baser, R4 index))
{
	emit_byte(0x8a);
	if (baser==5) {
		emit_byte(0x44+8*d);
		emit_byte(8*index+baser);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*d);
	emit_byte(8*index+baser);
}
LENDFUNC(NONE,READ,3,raw_mov_b_rrm_indexed,(W1 d, R4 baser, R4 index))

	LOWFUNC(NONE,WRITE,3,raw_mov_l_mrr_indexed,(R4 baser, R4 index, R4 s))
{
	emit_byte(0x89);
	if (baser==5) {
		emit_byte(0x44+8*s);
		emit_byte(8*index+baser);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*s);
	emit_byte(8*index+baser);
}
LENDFUNC(NONE,WRITE,3,raw_mov_l_mrr_indexed,(R4 baser, R4 index, R4 s))

	LOWFUNC(NONE,WRITE,3,raw_mov_w_mrr_indexed,(R4 baser, R4 index, R2 s))
{
	emit_byte(0x66);
	emit_byte(0x89);
	if (baser==5) {
		emit_byte(0x44+8*s);
		emit_byte(8*index+baser);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*s);
	emit_byte(8*index+baser);
}
LENDFUNC(NONE,WRITE,3,raw_mov_w_mrr_indexed,(R4 baser, R4 index, R2 s))

	LOWFUNC(NONE,WRITE,3,raw_mov_b_mrr_indexed,(R4 baser, R4 index, R1 s))
{
	emit_byte(0x88);
	if (baser==5) {
		emit_byte(0x44+8*s);
		emit_byte(8*index+baser);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*s);
	emit_byte(8*index+baser);
}
LENDFUNC(NONE,WRITE,3,raw_mov_b_mrr_indexed,(R4 baser, R4 index, R1 s))

	LOWFUNC(NONE,READ,3,raw_mov_l_rm_indexed,(W4 d, IMM base, R4 index))
{
	emit_byte(0x8b);
	emit_byte(0x04+8*d);
	emit_byte(0x85+8*index);
	emit_long(base);
}
LENDFUNC(NONE,READ,3,raw_mov_l_rm_indexed,(W4 d, IMM base, R4 index))

	LOWFUNC(NONE,READ,4,raw_cmov_l_rm_indexed,(W4 d, IMM base, R4 index, IMM cond))
{
	if (have_cmov) {
		emit_byte(0x0f);
		emit_byte(0x40+cond);
	}
	else { /* replacement using branch and mov */
		int uncc=(cond^1);
		emit_byte(0x70+uncc);
		emit_byte(7);  /* skip next 7 bytes if not cc=true */
		emit_byte(0x8b);
	}
	emit_byte(0x04+8*d);
	emit_byte(0x85+8*index);
	emit_long(base);
}
LENDFUNC(NONE,READ,4,raw_cmov_l_rm_indexed,(W4 d, IMM base, R4 index, IMM cond))

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
	emit_byte(0x8d);
	if (!offset) {
		if (s!=5) {
			emit_byte(0x04+8*d);
			emit_byte(0x40*factor+8*index+s);
			return;
		}
		emit_byte(0x44+8*d);
		emit_byte(0x40*factor+8*index+s);
		emit_byte(0);
		return;
	}
	emit_byte(0x84+8*d);
	emit_byte(0x40*factor+8*index+s);
	emit_long(offset);
}
LENDFUNC(NONE,NONE,5,raw_lea_l_brr_indexed,(W4 d, R4 s, R4 index, IMM factor, IMM offset))

	LOWFUNC(NONE,NONE,3,raw_lea_l_rr_indexed,(W4 d, R4 s, R4 index))
{
	emit_byte(0x8d);
	if (s==5) {
		emit_byte(0x44+8*d);
		emit_byte(8*index+s);
		emit_byte(0);
		return;
	}
	emit_byte(0x04+8*d);
	emit_byte(8*index+s);
}
LENDFUNC(NONE,NONE,3,raw_lea_l_rr_indexed,(W4 d, R4 s, R4 index))

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

	STATIC_INLINE void raw_call_r(R4 r)
{
	lopt_emit_all();
	emit_byte(0xff);
	emit_byte(0xd0+r);
}

STATIC_INLINE void raw_jmp_r(R4 r)
{
	lopt_emit_all();
	emit_byte(0xff);
	emit_byte(0xe0+r);
}

STATIC_INLINE void raw_jmp_m_indexed(uae_u32 base, uae_u32 r, uae_u32 m)
{
	int sib;

	switch (m) {
	case 1: sib = 0x05; break;
	case 2: sib = 0x45; break;
	case 4: sib = 0x85; break;
	case 8: sib = 0xC5; break;
	default: abort();
	}
	lopt_emit_all();
	emit_byte(0xff);
	emit_byte(0x24);
	emit_byte(8*r+sib);
	emit_long(base);
}

STATIC_INLINE void raw_jmp_m(uae_u32 base)
{
	lopt_emit_all();
	emit_byte(0xff);
	emit_byte(0x25);
	emit_long(base);
}

STATIC_INLINE void raw_call(uae_u32 t)
{
	lopt_emit_all();
	emit_byte(0xe8);
	emit_long(t-(uae_u32)target-4);
}

STATIC_INLINE void raw_jmp(uae_u32 t)
{
	lopt_emit_all();
	emit_byte(0xe9);
	emit_long(t-(uae_u32)target-4);
}

STATIC_INLINE void raw_jl(uae_u32 t)
{
	lopt_emit_all();
	emit_byte(0x0f);
	emit_byte(0x8c);
	emit_long(t-(uae_u32)target-4);
}

STATIC_INLINE void raw_jz(uae_u32 t)
{
	lopt_emit_all();
	emit_byte(0x0f);
	emit_byte(0x84);
	emit_long(t-(uae_u32)target-4);
}

STATIC_INLINE void raw_jnz(uae_u32 t)
{
	lopt_emit_all();
	emit_byte(0x0f);
	emit_byte(0x85);
	emit_long(t-(uae_u32)target-4);
}

STATIC_INLINE void raw_jnz_l_oponly(void)
{
	lopt_emit_all();
	emit_byte(0x0f);
	emit_byte(0x85);
}

STATIC_INLINE void raw_jcc_l_oponly(int cc)
{
	lopt_emit_all();
	emit_byte(0x0f);
	emit_byte(0x80+cc);
}

STATIC_INLINE void raw_jnz_b_oponly(void)
{
	lopt_emit_all();
	emit_byte(0x75);
}

STATIC_INLINE void raw_jz_b_oponly(void)
{
	lopt_emit_all();
	emit_byte(0x74);
}

STATIC_INLINE void raw_jmp_l_oponly(void)
{
	lopt_emit_all();
	emit_byte(0xe9);
}

STATIC_INLINE void raw_jmp_b_oponly(void)
{
	lopt_emit_all();
	emit_byte(0xeb);
}

STATIC_INLINE void raw_ret(void)
{
	lopt_emit_all();
	emit_byte(0xc3);
}

STATIC_INLINE void raw_nop(void)
{
	lopt_emit_all();
	emit_byte(0x90);
}


/*************************************************************************
* Flag handling, to and fro UAE flag register                           *
*************************************************************************/


#define FLAG_NREG1 0  /* Set to -1 if any register will do */

STATIC_INLINE void raw_flags_to_reg(int r)
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
STATIC_INLINE void raw_reg_to_flags(int r)
{
	raw_cmp_b_ri(r,-127); /* set V */
	raw_sahf(0);
}

/* Apparently, there are enough instructions between flag store and
flag reload to avoid the partial memory stall */
STATIC_INLINE void raw_load_flagreg(uae_u32 target, uae_u32 r)
{
#if 1
	raw_mov_l_rm(target,(uae_u32)live.state[r].mem);
#else
	raw_mov_b_rm(target,(uae_u32)live.state[r].mem);
	raw_mov_b_rm(target+4,((uae_u32)live.state[r].mem)+1);
#endif
}

/* FLAGX is word-sized */
STATIC_INLINE void raw_load_flagx(uae_u32 target, uae_u32 r)
{
	if (live.nat[target].canword)
		raw_mov_w_rm(target,(uae_u32)live.state[r].mem);
	else
		raw_mov_l_rm(target,(uae_u32)live.state[r].mem);
}

#define NATIVE_FLAG_Z 0x40
#define NATIVE_CC_EQ  4
STATIC_INLINE void raw_flags_set_zero(int f, int r, int t)
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

STATIC_INLINE void raw_inc_sp(int off)
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

#if defined(CPU_64_BIT)
#define ctxPC (pContext->Rip)
#else
#define ctxPC (pContext->Eip)
#endif

int EvalException (LPEXCEPTION_POINTERS blah, int n_except)
{
	PEXCEPTION_RECORD pExceptRecord = NULL;
	PCONTEXT          pContext = NULL;

	uae_u8* i = NULL;
	uae_u32 addr = 0;
	int r=-1;
	int size=4;
	int dir=-1;
	int len=0;

	if (n_except != STATUS_ACCESS_VIOLATION || !canbang || currprefs.cachesize == 0)
		return EXCEPTION_CONTINUE_SEARCH;

	pExceptRecord = blah->ExceptionRecord;
	pContext = blah->ContextRecord;

	if (pContext)
		i = (uae_u8 *)ctxPC;
	if (pExceptRecord)
		addr = (uae_u32)(pExceptRecord->ExceptionInformation[1]);
#ifdef JIT_DEBUG
	write_log (L"JIT: fault address is 0x%x at 0x%x\n",addr,i);
#endif
	if (!canbang || !currprefs.cachesize)
		return EXCEPTION_CONTINUE_SEARCH;

	if (in_handler)
		write_log (L"JIT: Argh --- Am already in a handler. Shouldn't happen!\n");

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
		write_log (L"JIT: register was %d, direction was %d, size was %d\n",r,dir,size);
#endif

		switch(r) {
#if defined(CPU_64_BIT)
		case 0: pr=&(pContext->Rax); break;
		case 1: pr=&(pContext->Rcx); break;
		case 2: pr=&(pContext->Rdx); break;
		case 3: pr=&(pContext->Rbx); break;
		case 4: pr=(size>1)?NULL:(((uae_u8*)&(pContext->Rax))+1); break;
		case 5: pr=(size>1)?
					(void*)(&(pContext->Rbp)):
			(void*)(((uae_u8*)&(pContext->Rcx))+1); break;
		case 6: pr=(size>1)?
					(void*)(&(pContext->Rsi)):
			(void*)(((uae_u8*)&(pContext->Rdx))+1); break;
		case 7: pr=(size>1)?
					(void*)(&(pContext->Rdi)):
			(void*)(((uae_u8*)&(pContext->Rbx))+1); break;
#else
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
#endif
		default: abort();
		}
		if (pr) {
			blockinfo* bi;

			if (currprefs.comp_oldsegv) {
				addr-=(uae_u32)NATMEM_OFFSET;

#ifdef JIT_DEBUG
				if ((addr>=0x10000000 && addr<0x40000000) ||
					(addr>=0x50000000)) {
						write_log (L"JIT: Suspicious address 0x%x in SEGV handler.\n",addr);
				}
#endif
				if (dir==SIG_READ) {
					switch (size) {
					case 1: *((uae_u8*)pr)=get_byte (addr); break;
					case 2: *((uae_u16*)pr)=swap16(get_word (addr)); break;
					case 4: *((uae_u32*)pr)=swap32(get_long (addr)); break;
					default: abort();
					}
				}
				else { /* write */
					switch (size) {
					case 1: put_byte (addr,*((uae_u8*)pr)); break;
					case 2: put_word (addr,swap16(*((uae_u16*)pr))); break;
					case 4: put_long (addr,swap32(*((uae_u32*)pr))); break;
					default: abort();
					}
				}
#ifdef JIT_DEBUG
				write_log (L"JIT: Handled one access!\n");
#endif
				fflush(stdout);
				segvcount++;
				ctxPC+=len;
			}
			else {
				void* tmp=target;
				int i;
				uae_u8 vecbuf[5];

				addr-=(uae_u32)NATMEM_OFFSET;

#ifdef JIT_DEBUG
				if ((addr>=0x10000000 && addr<0x40000000) ||
					(addr>=0x50000000)) {
						write_log (L"JIT: Suspicious address 0x%x in SEGV handler.\n",addr);
				}
#endif

				target=(uae_u8*)ctxPC;
				for (i=0;i<5;i++)
					vecbuf[i]=target[i];
				emit_byte(0xe9);
				emit_long((uae_u32)veccode-(uae_u32)target-4);
#ifdef JIT_DEBUG

				write_log (L"JIT: Create jump to %p\n",veccode);
				write_log (L"JIT: Handled one access!\n");
#endif
				segvcount++;

				target=veccode;

				if (dir==SIG_READ) {
					switch(size) {
					case 1: raw_mov_b_ri(r,get_byte (addr)); break;
					case 2: raw_mov_w_ri(r,swap16(get_word (addr))); break;
					case 4: raw_mov_l_ri(r,swap32(get_long (addr))); break;
					default: abort();
					}
				}
				else { /* write */
					switch(size) {
					case 1: put_byte (addr,*((uae_u8*)pr)); break;
					case 2: put_word (addr,swap16(*((uae_u16*)pr))); break;
					case 4: put_long (addr,swap32(*((uae_u32*)pr))); break;
					default: abort();
					}
				}
				for (i=0;i<5;i++)
					raw_mov_b_mi(ctxPC+i,vecbuf[i]);
				raw_mov_l_mi((uae_u32)&in_handler,0);
				emit_byte(0xe9);
				emit_long(ctxPC+len-(uae_u32)target-4);
				in_handler=1;
				target=(uae_u8*)tmp;
			}
			bi=active;
			while (bi) {
				if (bi->handler &&
					(uae_u8*)bi->direct_handler<=i &&
					(uae_u8*)bi->nexthandler>i) {
#ifdef JIT_DEBUG
						write_log (L"JIT: deleted trigger (%p<%p<%p) %p\n",
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
						write_log (L"JIT: deleted trigger (%p<%p<%p) %p\n",
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
			write_log (L"JIT: Huh? Could not find trigger!\n");
#endif
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}
	write_log (L"JIT: Can't handle access %08X!\n", i);
#if 0
	if (i)
	{
		int j;

		for (j=0;j<10;j++) {
			write_log (L"JIT: instruction byte %2d is 0x%02x\n",j,i[j]);
		}
	}
	write_log (L"Please send the above info (starting at \"fault address\") to\n"
		L"bmeyer@csse.monash.edu.au\n"
		L"This shouldn't happen ;-)\n");
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

	write_log (L"JIT: fault address is %08x at %08x\n",sc.cr2,sc.eip);
	if (!canbang)
		write_log (L"JIT: Not happy! Canbang is 0 in SIGSEGV handler!\n");
	if (in_handler)
		write_log (L"JIT: Argh --- Am already in a handler. Shouldn't happen!\n");

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
		write_log (L"JIT: register was %d, direction was %d, size was %d\n",r,dir,size);

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
						write_log (L"JIT: Suspicious address in %x SEGV handler.\n",addr);
				}
				if (dir==SIG_READ) {
					switch(size) {
					case 1: *((uae_u8*)pr)=get_byte (addr); break;
					case 2: *((uae_u16*)pr)=get_word (addr); break;
					case 4: *((uae_u32*)pr)=get_long (addr); break;
					default: abort();
					}
				}
				else { /* write */
					switch(size) {
					case 1: put_byte (addr,*((uae_u8*)pr)); break;
					case 2: put_word (addr,*((uae_u16*)pr)); break;
					case 4: put_long (addr,*((uae_u32*)pr)); break;
					default: abort();
					}
				}
				write_log (L"JIT: Handled one access!\n");
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
						write_log (L"JIT: Suspicious address 0x%x in SEGV handler.\n",addr);
				}

				target=(uae_u8*)sc.eip;
				for (i=0;i<5;i++)
					vecbuf[i]=target[i];
				emit_byte(0xe9);
				emit_long((uae_u32)veccode-(uae_u32)target-4);
				write_log (L"JIT: Create jump to %p\n",veccode);

				write_log (L"JIT: Handled one access!\n");
				segvcount++;

				target=veccode;

				if (dir==SIG_READ) {
					switch(size) {
					case 1: raw_mov_b_ri(r,get_byte (addr)); break;
					case 2: raw_mov_w_ri(r,get_word (addr)); break;
					case 4: raw_mov_l_ri(r,get_long (addr)); break;
					default: abort();
					}
				}
				else { /* write */
					switch(size) {
					case 1: put_byte (addr,*((uae_u8*)pr)); break;
					case 2: put_word (addr,*((uae_u16*)pr)); break;
					case 4: put_long (addr,*((uae_u32*)pr)); break;
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
						write_log (L"JIT: deleted trigger (%p<%p<%p) %p\n",
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
						write_log (L"JIT: deleted trigger (%p<%p<%p) %p\n",
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
			write_log (L"JIT: Huh? Could not find trigger!\n");
			return;
		}
	}
	write_log (L"JIT: Can't handle access!\n");
	for (j=0;j<10;j++) {
		write_log (L"JIT: instruction byte %2d is %02x\n",j,i[j]);
	}
#if 0
	write_log (L"Please send the above info (starting at \"fault address\") to\n"
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

struct cpuinfo_x86 {
	uae_u8	x86;			// CPU family
	uae_u8	x86_vendor;		// CPU vendor
	uae_u8	x86_processor;	// CPU canonical processor type
	uae_u8	x86_brand_id;	// CPU BrandID if supported, yield 0 otherwise
	uae_u32	x86_hwcap;
	uae_u8	x86_model;
	uae_u8	x86_mask;
	int		cpuid_level;    // Maximum supported CPUID level, -1=no CPUID
	char		x86_vendor_id[16];
};
struct cpuinfo_x86 cpuinfo;

enum {
	X86_VENDOR_INTEL		= 0,
	X86_VENDOR_CYRIX		= 1,
	X86_VENDOR_AMD		= 2,
	X86_VENDOR_UMC		= 3,
	X86_VENDOR_NEXGEN		= 4,
	X86_VENDOR_CENTAUR	= 5,
	X86_VENDOR_RISE		= 6,
	X86_VENDOR_TRANSMETA	= 7,
	X86_VENDOR_NSC		= 8,
	X86_VENDOR_UNKNOWN	= 0xff
};

enum {
	X86_PROCESSOR_I386,                       /* 80386 */
	X86_PROCESSOR_I486,                       /* 80486DX, 80486SX, 80486DX[24] */
	X86_PROCESSOR_PENTIUM,
	X86_PROCESSOR_PENTIUMPRO,
	X86_PROCESSOR_K6,
	X86_PROCESSOR_ATHLON,
	X86_PROCESSOR_PENTIUM4,
	X86_PROCESSOR_K8,
	X86_PROCESSOR_max
};

static struct ptt {
	const int align_loop;
	const int align_loop_max_skip;
	const int align_jump;
	const int align_jump_max_skip;
	const int align_func;
}
x86_alignments[X86_PROCESSOR_max + 1] = {
	{  4,  3,  4,  3,  4 },
	{ 16, 15, 16, 15, 16 },
	{ 16,  7, 16,  7, 16 },
	{ 16, 15, 16,  7, 16 },
	{ 32,  7, 32,  7, 32 },
	{ 16,  7, 16,  7, 16 },
	{  0,  0,  0,  0,  0 },
	{ 16,  7, 16,  7, 16 },
	{  0,  0,  0,  0,  0 }
};

static void
	x86_get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "GenuineIntel"))
		c->x86_vendor = X86_VENDOR_INTEL;
	else if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else if (!strcmp(v, "CyrixInstead"))
		c->x86_vendor = X86_VENDOR_CYRIX;
	else if (!strcmp(v, "Geode by NSC"))
		c->x86_vendor = X86_VENDOR_NSC;
	else if (!strcmp(v, "UMC UMC UMC "))
		c->x86_vendor = X86_VENDOR_UMC;
	else if (!strcmp(v, "CentaurHauls"))
		c->x86_vendor = X86_VENDOR_CENTAUR;
	else if (!strcmp(v, "NexGenDriven"))
		c->x86_vendor = X86_VENDOR_NEXGEN;
	else if (!strcmp(v, "RiseRiseRise"))
		c->x86_vendor = X86_VENDOR_RISE;
	else if (!strcmp(v, "GenuineTMx86") ||
		!strcmp(v, "TransmetaCPU"))
		c->x86_vendor = X86_VENDOR_TRANSMETA;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

static void cpuid(uae_u32 op, uae_u32 *eax, uae_u32 *ebx, uae_u32 *ecx, uae_u32 *edx)
{
	const int CPUID_SPACE = 4096;
	uae_u8* cpuid_space = (uae_u8*)cache_alloc(CPUID_SPACE);
	static uae_u32 s_op, s_eax, s_ebx, s_ecx, s_edx;
	uae_u8* tmp=get_target();

	s_op = op;
	set_target(cpuid_space);
	raw_push_l_r(0); /* eax */
	raw_push_l_r(1); /* ecx */
	raw_push_l_r(2); /* edx */
	raw_push_l_r(3); /* ebx */
	raw_mov_l_rm(0,(uintptr)&s_op);
	raw_cpuid(0);
	raw_mov_l_mr((uintptr)&s_eax,0);
	raw_mov_l_mr((uintptr)&s_ebx,3);
	raw_mov_l_mr((uintptr)&s_ecx,1);
	raw_mov_l_mr((uintptr)&s_edx,2);
	raw_pop_l_r(3);
	raw_pop_l_r(2);
	raw_pop_l_r(1);
	raw_pop_l_r(0);
	raw_ret();
	set_target(tmp);

	((compop_func*)cpuid_space)(0);
	if (eax != NULL) *eax = s_eax;
	if (ebx != NULL) *ebx = s_ebx;
	if (ecx != NULL) *ecx = s_ecx;
	if (edx != NULL) *edx = s_edx;

	cache_free (cpuid_space);
}

static void raw_init_cpu(void)
{
	struct cpuinfo_x86 *c = &cpuinfo;
	uae_u32 xlvl;

	/* Defaults */
	c->x86_processor = X86_PROCESSOR_max;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->cpuid_level = -1;				/* CPUID not detected */
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0';		/* Unset */
	c->x86_hwcap = 0;

	/* Get vendor name */
	c->x86_vendor_id[12] = '\0';
	cpuid(0x00000000,
		(uae_u32 *)&c->cpuid_level,
		(uae_u32 *)&c->x86_vendor_id[0],
		(uae_u32 *)&c->x86_vendor_id[8],
		(uae_u32 *)&c->x86_vendor_id[4]);
	x86_get_cpu_vendor(c);

	/* Intel-defined flags: level 0x00000001 */
	c->x86_brand_id = 0;
	if ( c->cpuid_level >= 0x00000001 ) {
		uae_u32 tfms, brand_id;
		cpuid(0x00000001, &tfms, &brand_id, NULL, &c->x86_hwcap);
		c->x86 = (tfms >> 8) & 15;
		c->x86_model = (tfms >> 4) & 15;
		c->x86_brand_id = brand_id & 0xff;
		if ( (c->x86_vendor == X86_VENDOR_AMD) &&
			(c->x86 == 0xf)) {
				/* AMD Extended Family and Model Values */
				c->x86 += (tfms >> 20) & 0xff;
				c->x86_model += (tfms >> 12) & 0xf0;
		}
		c->x86_mask = tfms & 15;
	} else {
		/* Have CPUID level 0 only - unheard of */
		c->x86 = 4;
	}

	/* AMD-defined flags: level 0x80000001 */
	cpuid(0x80000000, &xlvl, NULL, NULL, NULL);
	if ( (xlvl & 0xffff0000) == 0x80000000 ) {
		if ( xlvl >= 0x80000001 ) {
			uae_u32 features;
			cpuid(0x80000001, NULL, NULL, NULL, &features);
			if (features & (1 << 29)) {
				/* Assume x86-64 if long mode is supported */
				c->x86_processor = X86_PROCESSOR_K8;
			}
		}
	}

	/* Canonicalize processor ID */
	switch (c->x86) {
	case 3:
		c->x86_processor = X86_PROCESSOR_I386;
		break;
	case 4:
		c->x86_processor = X86_PROCESSOR_I486;
		break;
	case 5:
		if (c->x86_vendor == X86_VENDOR_AMD)
			c->x86_processor = X86_PROCESSOR_K6;
		else
			c->x86_processor = X86_PROCESSOR_PENTIUM;
		break;
	case 6:
		if (c->x86_vendor == X86_VENDOR_AMD)
			c->x86_processor = X86_PROCESSOR_ATHLON;
		else
			c->x86_processor = X86_PROCESSOR_PENTIUMPRO;
		break;
	case 15:
		if (c->x86_vendor == X86_VENDOR_INTEL) {
			/* Assume any BrandID >= 8 and family == 15 yields a Pentium 4 */
			if (c->x86_brand_id >= 8)
				c->x86_processor = X86_PROCESSOR_PENTIUM4;
		}
		if (c->x86_vendor == X86_VENDOR_AMD) {
			/* Assume an Athlon processor if family == 15 and it was not
			detected as an x86-64 so far */
			if (c->x86_processor == X86_PROCESSOR_max)
				c->x86_processor = X86_PROCESSOR_ATHLON;
		}
		break;
	}

	/* Have CMOV support? */
	have_cmov = c->x86_hwcap & (1 << 15);

#if 0
	/* Can the host CPU suffer from partial register stalls? */
	have_rat_stall = (c->x86_vendor == X86_VENDOR_INTEL);
	/* It appears that partial register writes are a bad idea even on
	AMD K7 cores, even though they are not supposed to have the
	dreaded rat stall. Why? Anyway, that's why we lie about it ;-) */
	if (c->x86_processor == X86_PROCESSOR_ATHLON)
		have_rat_stall = 1;
#endif
	have_rat_stall = 1;

	/* Alignments */
	if (tune_alignment) {
		align_loops = x86_alignments[c->x86_processor].align_loop;
		align_jumps = x86_alignments[c->x86_processor].align_jump;
	}
	{ 
		TCHAR *s = au (c->x86_vendor_id);
		write_log (L"CPUID level=%d, Family=%d, Model=%d, Mask=%d, Vendor=%s [%d]\n",
			c->cpuid_level, c->x86, c->x86_model, c->x86_mask, s, c->x86_vendor);
		xfree (s);
	}
}

#if 0
static int target_check_bsf(void)
{
	int mismatch = 0;
	for (int g_ZF = 0; g_ZF <= 1; g_ZF++) {
		for (int g_CF = 0; g_CF <= 1; g_CF++) {
			for (int g_OF = 0; g_OF <= 1; g_OF++) {
				for (int g_SF = 0; g_SF <= 1; g_SF++) {
					for (int value = -1; value <= 1; value++) {
						unsigned long flags = (g_SF << 7) | (g_OF << 11) | (g_ZF << 6) | g_CF;
						unsigned long tmp = value;
						__asm__ __volatile__ ("push %0; popf; bsf %1,%1; pushf; pop %0"
							: "+r" (flags), "+r" (tmp) : : "cc");
						int OF = (flags >> 11) & 1;
						int SF = (flags >>  7) & 1;
						int ZF = (flags >>  6) & 1;
						int CF = flags & 1;
						tmp = (value == 0);
						if (ZF != tmp || SF != g_SF || OF != g_OF || CF != g_CF)
							mismatch = true;
					}
				}}}}
	if (mismatch)
		write_log (L"Target CPU defines all flags on BSF instruction\n");
	return !mismatch;
}
#endif

#if 0

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
	write_log (L"Max CPUID level=%d Processor is %c%c%c%c%c%c%c%c%c%c%c%c\n",
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
	write_log (L"have_cmov=%d, avoid_cmov=%d, have_rat_stall=%d\n",
		have_cmov,currprefs.avoid_cmov,have_rat_stall);
	if (currprefs.avoid_cmov) {
		write_log (L"Disabling cmov use despite processor claiming to support it!\n");
		have_cmov=0;
	}
#else
	/* Dear Bernie, I don't want to keep around options which are useless, and not
	represented in the GUI anymore... Is this okay? */
	write_log (L"have_cmov=%d, have_rat_stall=%d\n", have_cmov, have_rat_stall);
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
#endif

/*************************************************************************
* FPU stuff                                                             *
*************************************************************************/


STATIC_INLINE void raw_fp_init(void)
{
	int i;

	for (i=0;i<N_FREGS;i++)
		live.spos[i]=-2;
	live.tos=-1;  /* Stack is empty */
}

STATIC_INLINE void raw_fp_cleanup_drop(void)
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

STATIC_INLINE void make_tos(int r)
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

STATIC_INLINE int stackpos(int r)
{
	if (live.spos[r]<0)
		abort();
	if (live.tos<live.spos[r]) {
		write_log (L"JIT: Looking for spos for fnreg %d\n",r);
		abort();
	}
	return live.tos-live.spos[r];
}

/* IMO, calling usereg(r) makes no sense, if the register r should supply our function with
an argument, because I would expect all arguments to be on the stack already, won't they?
Thus, usereg(s) is always useless and also for every FRW d it's too late here now. PeterK
*/
STATIC_INLINE void usereg(int r)
{

	if (live.spos[r]<0) {
		// write_log (L"usereg wants to push reg %d onto the x87 stack calling make_tos\n", r);
		make_tos(r);
	}
}

/* This is called with one FP value in a reg *above* tos,
which it will pop off the stack if necessary */
STATIC_INLINE void tos_make(int r)
{
	if (live.spos[r]<0) {
		live.tos++;
		live.spos[r]=live.tos;
		live.onstack[live.tos]=r;
		return;
	}
	emit_byte(0xdd);
	emit_byte(0xd8+(live.tos+1)-live.spos[r]);
	/* store top of stack in reg and pop it*/
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

	LOWFUNC(NONE,WRITE,3,raw_fmovi_mrb,(MEMW m, FR r, double *bounds))
{
	/* Clamp value to the given range and convert to integer.
	ideally, the clamping should be done using two FCMOVs, but
	this requires two free fp registers, and we can only be sure
	of having one. Using a jump for the lower bound and an FCMOV
	for the upper bound, we can do it with one scratch register.
	*/

	int rs;
	usereg(r);
	rs = stackpos(r)+1;

	/* Lower bound onto stack */
	emit_byte(0xdd);
	emit_byte(0x05);
	emit_long((uae_u32)&bounds[0]); /* fld double from lower */

	/* Clamp to lower */
	emit_byte(0xdb);
	emit_byte(0xf0+rs); /* fcomi lower,r */
	emit_byte(0x73);
	emit_byte(12);      /* jae to writeback */

	/* Upper bound onto stack */
	emit_byte(0xdd);
	emit_byte(0xd8);	/* fstp st(0) */
	emit_byte(0xdd);
	emit_byte(0x05);
	emit_long((uae_u32)&bounds[1]); /* fld double from upper */

	/* Clamp to upper */
	emit_byte(0xdb);
	emit_byte(0xf0+rs); /* fcomi upper,r */
	emit_byte(0xdb);
	emit_byte(0xd0+rs); /* fcmovnbe upper,r */

	/* Store to destination */
	emit_byte(0xdb);
	emit_byte(0x1d);
	emit_long(m);
}
LENDFUNC(NONE,WRITE,3,raw_fmovi_mrb,(MEMW m, FR r, double *bounds))

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

	LOWFUNC(NONE,NONE,1,raw_fcuts_r,(FRW r))
{
	make_tos(r);     /* TOS = r */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0xfc); /* add -4 to esp */
	emit_byte(0xd9);
	emit_byte(0x1c);
	emit_byte(0x24); /* fstp store r as SINGLE to [esp] and pop */
	emit_byte(0xd9);
	emit_byte(0x04);
	emit_byte(0x24); /* fld load r as SINGLE from [esp] */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0x04); /* add +4 to esp */
}
LENDFUNC(NONE,NONE,1,raw_fcuts_r,(FRW r))

	LOWFUNC(NONE,NONE,1,raw_fcut_r,(FRW r))
{
	make_tos(r);     /* TOS = r */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0xf8); /* add -8 to esp */
	emit_byte(0xdd);
	emit_byte(0x1c);
	emit_byte(0x24); /* fstp store r as DOUBLE to [esp] and pop */
	emit_byte(0xdd);
	emit_byte(0x04);
	emit_byte(0x24); /* fld load r as DOUBLE from [esp] */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0x08); /* add +8 to esp */
}
LENDFUNC(NONE,NONE,1,raw_fcut_r,(FRW r))

	LOWFUNC(NONE,READ,2,raw_fmovl_ri,(FW r, IMMS i))
{
	emit_byte(0x68);
	emit_long(i);    /* push immediate32 onto [esp] */
	emit_byte(0xdb);
	emit_byte(0x04);
	emit_byte(0x24); /* fild load m32int from [esp] */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0x04); /* add +4 to esp */
	tos_make(r);
}
LENDFUNC(NONE,READ,2,raw_fmovl_ri,(FW r, IMMS i))

	LOWFUNC(NONE,READ,2,raw_fmovs_ri,(FW r, IMM i))
{
	emit_byte(0x68);
	emit_long(i);    /* push immediate32 onto [esp] */
	emit_byte(0xd9);
	emit_byte(0x04);
	emit_byte(0x24); /* fld load m32real from [esp] */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0x04); /* add +4 to esp */
	tos_make(r);
}
LENDFUNC(NONE,READ,2,raw_fmovs_ri,(FW r, IMM i))

	LOWFUNC(NONE,READ,3,raw_fmov_ri,(FW r, IMM i1, IMM i2))
{
	emit_byte(0x68);
	emit_long(i2);   /* push immediate32 onto [esp] */
	emit_byte(0x68);
	emit_long(i1);   /* push immediate32 onto [esp] */
	emit_byte(0xdd);
	emit_byte(0x04);
	emit_byte(0x24); /* fld load m64real from [esp] */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0x08); /* add +8 to esp */
	tos_make(r);
}
LENDFUNC(NONE,READ,3,raw_fmov_ri,(FW r, IMM i1, IMM i2))

	LOWFUNC(NONE,READ,4,raw_fmov_ext_ri,(FW r, IMM i1, IMM i2, IMM i3))
{
	emit_byte(0x68);
	emit_long(i3);   /* push immediate32 onto [esp] */
	emit_byte(0x68);
	emit_long(i2);   /* push immediate32 onto [esp] */
	emit_byte(0x68);
	emit_long(i1);   /* push immediate32 onto [esp] */
	emit_byte(0xdb);
	emit_byte(0x2c);
	emit_byte(0x24); /* fld load m80real from [esp] */
	emit_byte(0x83);
	emit_byte(0xc4);
	emit_byte(0x0c); /* add +12 to esp */
	tos_make(r);
}
LENDFUNC(NONE,READ,4,raw_fmov_ext_ri,(FW r, IMM i1, IMM i2, IMMi3))

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

	LOWFUNC(NONE,READ,2,raw_fldcw_m_indexed,(R4 index, IMM base))
{
	emit_byte(0xd9);
	emit_byte(0xa8+index);
	emit_long(base);
}
LENDFUNC(NONE,READ,2,raw_fldcw_m_indexed,(R4 index, IMM base))

	LOWFUNC(NONE,NONE,2,raw_fsqrt_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xfa);    /* fsqrt sqrt(x) */
		tos_make(d);        /* store to destination */
	}
	else {
		make_tos(d);
		emit_byte(0xd9);
		emit_byte(0xfa);    /* fsqrt y=sqrt(x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_fsqrt_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fabs_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xe1);    /* fabs abs(x) */
		tos_make(d);        /* store to destination */
	}
	else {
		make_tos(d);
		emit_byte(0xd9);
		emit_byte(0xe1);    /* fabs y=abs(x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_fabs_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_frndint_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xfc);    /* frndint int(x) */
		tos_make(d);        /* store to destination */
	}
	else {
		make_tos(d);
		emit_byte(0xd9);
		emit_byte(0xfc);    /* frndint y=int(x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_frndint_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fgetexp_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xf4);    /* fxtract exp push man */
		emit_byte(0xdd);
		emit_byte(0xd8);    /* fstp just pop man */
		tos_make(d);        /* store exp to destination */
	}
	else {
		make_tos(d);        /* tos=x=y */
		emit_byte(0xd9);
		emit_byte(0xf4);    /* fxtract exp push man */
		emit_byte(0xdd);
		emit_byte(0xd8);    /* fstp just pop man */
	}
}
LENDFUNC(NONE,NONE,2,raw_fgetexp_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fgetman_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xf4);    /* fxtract exp push man */
		emit_byte(0xdd);
		emit_byte(0xd9);    /* fstp copy man up & pop */
		tos_make(d);        /* store man to destination */
	}
	else {
		make_tos(d);        /* tos=x=y */
		emit_byte(0xd9);
		emit_byte(0xf4);    /* fxtract exp push man */
		emit_byte(0xdd);
		emit_byte(0xd9);    /* fstp copy man up & pop */
	}
}
LENDFUNC(NONE,NONE,2,raw_fgetman_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fsin_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xfe);    /* fsin sin(x) */
		tos_make(d);        /* store to destination */
	}
	else {
		make_tos(d);
		emit_byte(0xd9);
		emit_byte(0xfe);    /* fsin y=sin(x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_fsin_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fcos_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xff);    /* fcos cos(x) */
		tos_make(d);        /* store to destination */
	}
	else {
		make_tos(d);
		emit_byte(0xd9);
		emit_byte(0xff);    /* fcos y=cos(x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_fcos_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_ftan_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
		emit_byte(0xd9);
		emit_byte(0xf2);    /* fptan tan(x)=y/1.0 */
		emit_byte(0xdd);
		emit_byte(0xd8);    /* fstp pop 1.0 */
		tos_make(d);        /* store to destination */
	}
	else {
		make_tos(d);
		emit_byte(0xd9);
		emit_byte(0xf2);    /* fptan tan(x)=y/1.0 */
		emit_byte(0xdd);
		emit_byte(0xd8);    /* fstp pop 1.0 */
	}
}
LENDFUNC(NONE,NONE,2,raw_ftan_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,3,raw_fsincos_rr,(FW d, FW c, FR s))
{
	int ds;

	if (s==d) {
		//write_log (L"FSINCOS src = dest\n");
		make_tos(s);
		emit_byte(0xd9);
		emit_byte(0xfb); /* fsincos sin(x) push cos(x) */
		tos_make(c);     /* store cos(x) to c */
		return;
	}

	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds);  /* fld x */
	emit_byte(0xd9);
	emit_byte(0xfb);     /* fsincos sin(x) push cos(x) */
	if (live.spos[c]<0) {
		if (live.spos[d]<0) { /* occupy both regs directly */
			live.tos++;
			live.spos[d]=live.tos;
			live.onstack[live.tos]=d; /* sin(x) comes first */
			live.tos++;
			live.spos[c]=live.tos;
			live.onstack[live.tos]=c;
		}
		else {
			emit_byte(0xd9);
			emit_byte(0xc9); /* fxch swap cos(x) with sin(x) */
			emit_byte(0xdd); /* store sin(x) to d & pop */
			emit_byte(0xd8+(live.tos+2)-live.spos[d]);
			live.tos++;      /* occupy a reg for cos(x) here */
			live.spos[c]=live.tos;
			live.onstack[live.tos]=c;
		}
	}
	else {
		emit_byte(0xdd); /* store cos(x) to c & pop */
		emit_byte(0xd8+(live.tos+2)-live.spos[c]);
		tos_make(d);     /* store sin(x) to destination */
	}
}
LENDFUNC(NONE,NONE,3,raw_fsincos_rr,(FW d, FW c, FR s))

	float one=1;

LOWFUNC(NONE,NONE,2,raw_fscale_rr,(FRW d, FR s))
{
	int ds;

	if (live.spos[d]==live.tos && live.spos[s]==live.tos-1) {
		//write_log (L"fscale found x in TOS-1 and y in TOS\n");
		emit_byte(0xd9);
		emit_byte(0xfd);    /* fscale y*(2^x) */
	}
	else {
		make_tos(s);        /* tos=x */
		ds=stackpos(d);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld y */
		emit_byte(0xd9);
		emit_byte(0xfd);    /* fscale y*(2^x) */
		tos_make(d);        /* store y=y*(2^x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_fscale_rr,(FRW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_ftwotox_rr,(FW d, FR s))
{
	int ds;

	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* fld x */
	emit_byte(0xd9);
	emit_byte(0xfc);    /* frndint int(x) */
	emit_byte(0xd9);
	emit_byte(0xc1+ds); /* fld x again */
	emit_byte(0xd8);
	emit_byte(0xe1);    /* fsub frac(x) = x - int(x) */
	emit_byte(0xd9);
	emit_byte(0xf0);    /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one); /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);    /* fscale (2^frac(x))*2^int(x) */
	emit_byte(0xdd);
	emit_byte(0xd9);    /* fstp copy & pop */
	tos_make(d);        /* store y=2^x */
}
LENDFUNC(NONE,NONE,2,raw_ftwotox_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fetox_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xea);    /* fldl2e log2(e) */
	emit_byte(0xd8);
	emit_byte(0xc9);    /* fmul x*log2(e) */
	emit_byte(0xdd);
	emit_byte(0xd1);    /* fst copy up */
	emit_byte(0xd9);
	emit_byte(0xfc);    /* frndint int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap top two elements */
	emit_byte(0xd8);
	emit_byte(0xe1);    /* fsub x*log2(e) - int(x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);    /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);    /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xdd);
	emit_byte(0xd9);    /* fstp copy & pop */
	if (s!=d)
		tos_make(d);    /* store y=e^x */
}
LENDFUNC(NONE,NONE,2,raw_fetox_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fetoxM1_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xea);    /* fldl2e log2(e) */
	emit_byte(0xd8);
	emit_byte(0xc9);    /* fmul x*log2(e) */
	emit_byte(0xdd);
	emit_byte(0xd1);    /* fst copy up */
	emit_byte(0xd9);
	emit_byte(0xfc);    /* frndint int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap top two elements */
	emit_byte(0xd8);
	emit_byte(0xe1);    /* fsub x*log2(e) - int(x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);    /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd9);
	emit_byte(0xfd);    /* fscale ((2^frac(x))-1)*2^int(x*log2(e)) */
	emit_byte(0xdd);
	emit_byte(0xd9);    /* fstp copy & pop */
	if (s!=d)
		tos_make(d);    /* store y=(e^x)-1 */
}
LENDFUNC(NONE,NONE,2,raw_fetoxM1_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_ftentox_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xe9);    /* fldl2t log2(10) */
	emit_byte(0xd8);
	emit_byte(0xc9);    /* fmul x*log2(10) */
	emit_byte(0xdd);
	emit_byte(0xd1);    /* fst copy up */
	emit_byte(0xd9);
	emit_byte(0xfc);    /* frndint int(x*log2(10)) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap top two elements */
	emit_byte(0xd8);
	emit_byte(0xe1);    /* fsub x*log2(10) - int(x*log2(10))  */
	emit_byte(0xd9);
	emit_byte(0xf0);    /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);    /* fscale (2^frac(x))*2^int(x*log2(10)) */
	emit_byte(0xdd);
	emit_byte(0xd9);    /* fstp copy & pop */
	if (s!=d)
		tos_make(d);    /* store y=10^x */
}
LENDFUNC(NONE,NONE,2,raw_ftentox_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_flog2_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xe8);    /* fld1 1 */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap 1 with x */
	emit_byte(0xd9);
	emit_byte(0xf1);    /* fyl2x 1*log2(x) */
	if (s!=d)
		tos_make(d);    /* store y=log2(x) */
}
LENDFUNC(NONE,NONE,2,raw_flog2_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_flogN_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xed);    /* fldln2 logN(2) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap logN(2) with x */
	emit_byte(0xd9);
	emit_byte(0xf1);    /* fyl2x logN(2)*log2(x) */
	if (s!=d)
		tos_make(d);    /* store y=logN(x) */
}
LENDFUNC(NONE,NONE,2,raw_flogN_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_flogNP1_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xed);    /* fldln2 logN(2) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap logN(2) with x */
	emit_byte(0xd9);
	emit_byte(0xf9);    /* fyl2xp1 logN(2)*log2(x+1) */
	if (s!=d)
		tos_make(d);    /* store y=logN(x+1) */
}
LENDFUNC(NONE,NONE,2,raw_flogNP1_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_flog10_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xec);    /* fldlg2 log10(2) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap log10(2) with x */
	emit_byte(0xd9);
	emit_byte(0xf1);    /* fyl2x log10(2)*log2(x) */
	if (s!=d)
		tos_make(d);    /* store y=log10(x) */
}
LENDFUNC(NONE,NONE,2,raw_flog10_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fasin_rr,(FW d, FR s))
{
	int ds;

	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* fld x */
	emit_byte(0xd8);
	emit_byte(0xc8);    /* fmul x*x */
	emit_byte(0xd9);
	emit_byte(0xe8);    /* fld 1.0 */
	emit_byte(0xde);
	emit_byte(0xe1);    /* fsubrp 1 - (x^2) */
	emit_byte(0xd9);
	emit_byte(0xfa);    /* fsqrt sqrt(1-(x^2)) */
	emit_byte(0xd9);
	emit_byte(0xc1+ds); /* fld x again */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap x with sqrt(1-(x^2))  */
	emit_byte(0xd9);
	emit_byte(0xf3);    /* fpatan atan(x/sqrt(1-(x^2))) & pop */
	tos_make(d);        /* store y=asin(x) */
}
LENDFUNC(NONE,NONE,2,raw_fasin_rr,(FW d, FR s))

	static uae_u32 pihalf[] = {0x2168c234, 0xc90fdaa2, 0x3fff}; // LSB=0 to get acos(1)=0
LOWFUNC(NONE,NONE,2,raw_facos_rr,(FW d, FR s))
{
	int ds;

	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* fld x */
	emit_byte(0xd8);
	emit_byte(0xc8);    /* fmul x*x */
	emit_byte(0xd9);
	emit_byte(0xe8);    /* fld 1.0 */
	emit_byte(0xde);
	emit_byte(0xe1);    /* fsubrp 1 - (x^2) */
	emit_byte(0xd9);
	emit_byte(0xfa);    /* fsqrt sqrt(1-(x^2)) */
	emit_byte(0xd9);
	emit_byte(0xc1+ds); /* fld x again */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap x with sqrt(1-(x^2))  */
	emit_byte(0xd9);
	emit_byte(0xf3);    /* fpatan atan(x/sqrt(1-(x^2))) & pop */
	emit_byte(0xdb);
	emit_byte(0x2d);
	emit_long((uae_u32)&pihalf); /* fld load pi/2 from pihalf */
	emit_byte(0xde);
	emit_byte(0xe1);    /* fsubrp pi/2 - asin(x) & pop */
	tos_make(d);        /* store y=acos(x) */
}
LENDFUNC(NONE,NONE,2,raw_facos_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fatan_rr,(FW d, FR s))
{
	int ds;

	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xe8);    /* fld 1.0 */
	emit_byte(0xd9);
	emit_byte(0xf3);    /* fpatan atan(x)/1  & pop*/
	if (s!=d)
		tos_make(d);    /* store y=atan(x) */
}
LENDFUNC(NONE,NONE,2,raw_fatan_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fatanh_rr,(FW d, FR s))
{
	int ds;

	ds=stackpos(s);
	emit_byte(0xd9);
	emit_byte(0xc0+ds); /* fld x */
	emit_byte(0xd9);
	emit_byte(0xe8);    /* fld 1.0 */
	emit_byte(0xdc);
	emit_byte(0xc1);    /* fadd 1 + x */
	emit_byte(0xd8);
	emit_byte(0xe2+ds); /* fsub 1 - x */
	emit_byte(0xde);
	emit_byte(0xf9);    /* fdivp (1+x)/(1-x) */
	emit_byte(0xd9);
	emit_byte(0xed);    /* fldl2e logN(2) */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap logN(2) with (1+x)/(1-x) */
	emit_byte(0xd9);
	emit_byte(0xf1);    /* fyl2x logN(2)*log2((1+x)/(1-x)) pop */
	emit_byte(0xd9);
	emit_byte(0xe8);    /* fld 1.0 */
	emit_byte(0xd9);
	emit_byte(0xe0);    /* fchs -1.0 */
	emit_byte(0xd9);
	emit_byte(0xc9);    /* fxch swap */
	emit_byte(0xd9);
	emit_byte(0xfd);    /* fscale logN((1+x)/(1-x)) * 2^(-1) */
	emit_byte(0xdd);
	emit_byte(0xd9);    /* fstp copy & pop */
	tos_make(d);        /* store y=atanh(x) */
}
LENDFUNC(NONE,NONE,2,raw_fatanh_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fsinh_rr,(FW d, FR s))
{
	int ds,tr;

	tr=live.onstack[live.tos+3];
	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xea);     /* fldl2e log2(e) */
	emit_byte(0xd8);
	emit_byte(0xc9);     /* fmul x*log2(e) */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy x*log2(e) */
	if (tr>=0) {
		emit_byte(0xd9);
		emit_byte(0xca); /* fxch swap with temp-reg */
		emit_byte(0x83);
		emit_byte(0xc4);
		emit_byte(0xf4); /* add -12 to esp */
		emit_byte(0xdb);
		emit_byte(0x3c);
		emit_byte(0x24); /* fstp store temp-reg to [esp] & pop */
	}
	emit_byte(0xd9);
	emit_byte(0xe0);     /* fchs -x*log2(e) */
	emit_byte(0xd9);
	emit_byte(0xc0);     /* fld -x*log2(e) again */
	emit_byte(0xd9);
	emit_byte(0xfc);     /* frndint int(-x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd8);
	emit_byte(0xe1);     /* fsub -x*log2(e) - int(-x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);     /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xca);     /* fxch swap e^-x with x*log2(e) in tr */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy x*log2(e) */
	emit_byte(0xd9);
	emit_byte(0xfc);     /* frndint int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd8);
	emit_byte(0xe1);     /* fsub x*log2(e) - int(x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);     /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xdd);
	emit_byte(0xd9);     /* fstp copy e^x & pop */
	if (tr>=0) {
		emit_byte(0xdb);
		emit_byte(0x2c);
		emit_byte(0x24); /* fld load temp-reg from [esp] */
		emit_byte(0x83);
		emit_byte(0xc4);
		emit_byte(0x0c); /* add +12 to esp */
		emit_byte(0xd9);
		emit_byte(0xca); /* fxch swap temp-reg with e^-x in tr */
		emit_byte(0xde);
		emit_byte(0xe9); /* fsubp (e^x)-(e^-x) */
	}
	else {
		emit_byte(0xde);
		emit_byte(0xe1); /* fsubrp (e^x)-(e^-x) */
	}
	emit_byte(0xd9);
	emit_byte(0xe8);     /* fld 1.0 */
	emit_byte(0xd9);
	emit_byte(0xe0);     /* fchs -1.0 */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale ((e^x)-(e^-x))/2 */
	emit_byte(0xdd);
	emit_byte(0xd9);     /* fstp copy & pop */
	if (s!=d)
		tos_make(d);     /* store y=sinh(x) */
}
LENDFUNC(NONE,NONE,2,raw_fsinh_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fcosh_rr,(FW d, FR s))
{
	int ds,tr;

	tr=live.onstack[live.tos+3];
	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xea);     /* fldl2e log2(e) */
	emit_byte(0xd8);
	emit_byte(0xc9);     /* fmul x*log2(e) */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy x*log2(e) */
	if (tr>=0) {
		emit_byte(0xd9);
		emit_byte(0xca); /* fxch swap with temp-reg */
		emit_byte(0x83);
		emit_byte(0xc4);
		emit_byte(0xf4); /* add -12 to esp */
		emit_byte(0xdb);
		emit_byte(0x3c);
		emit_byte(0x24); /* fstp store temp-reg to [esp] & pop */
	}
	emit_byte(0xd9);
	emit_byte(0xe0);     /* fchs -x*log2(e) */
	emit_byte(0xd9);
	emit_byte(0xc0);     /* fld -x*log2(e) again */
	emit_byte(0xd9);
	emit_byte(0xfc);     /* frndint int(-x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd8);
	emit_byte(0xe1);     /* fsub -x*log2(e) - int(-x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);     /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xca);     /* fxch swap e^-x with x*log2(e) in tr */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy x*log2(e) */
	emit_byte(0xd9);
	emit_byte(0xfc);     /* frndint int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd8);
	emit_byte(0xe1);     /* fsub x*log2(e) - int(x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);     /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xdd);
	emit_byte(0xd9);     /* fstp copy e^x & pop */
	if (tr>=0) {
		emit_byte(0xdb);
		emit_byte(0x2c);
		emit_byte(0x24); /* fld load temp-reg from [esp] */
		emit_byte(0x83);
		emit_byte(0xc4);
		emit_byte(0x0c); /* add +12 to esp */
		emit_byte(0xd9);
		emit_byte(0xca); /* fxch swap temp-reg with e^-x in tr */
	}
	emit_byte(0xde);
	emit_byte(0xc1);     /* faddp (e^x)+(e^-x) */
	emit_byte(0xd9);
	emit_byte(0xe8);     /* fld 1.0 */
	emit_byte(0xd9);
	emit_byte(0xe0);     /* fchs -1.0 */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale ((e^x)+(e^-x))/2 */
	emit_byte(0xdd);
	emit_byte(0xd9);     /* fstp copy & pop */
	if (s!=d)
		tos_make(d);     /* store y=cosh(x) */
}
LENDFUNC(NONE,NONE,2,raw_fcosh_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_ftanh_rr,(FW d, FR s))
{
	int ds,tr;

	tr=live.onstack[live.tos+3];
	if (s==d)
		make_tos(s);
	else {
		ds=stackpos(s);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld x */
	}
	emit_byte(0xd9);
	emit_byte(0xea);     /* fldl2e log2(e) */
	emit_byte(0xd8);
	emit_byte(0xc9);     /* fmul x*log2(e) */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy x*log2(e) */
	if (tr>=0) {
		emit_byte(0xd9);
		emit_byte(0xca); /* fxch swap with temp-reg */
		emit_byte(0x83);
		emit_byte(0xc4);
		emit_byte(0xf4); /* add -12 to esp */
		emit_byte(0xdb);
		emit_byte(0x3c);
		emit_byte(0x24); /* fstp store temp-reg to [esp] & pop */
	}
	emit_byte(0xd9);
	emit_byte(0xe0);     /* fchs -x*log2(e) */
	emit_byte(0xd9);
	emit_byte(0xc0);     /* fld -x*log2(e) again */
	emit_byte(0xd9);
	emit_byte(0xfc);     /* frndint int(-x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd8);
	emit_byte(0xe1);     /* fsub -x*log2(e) - int(-x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);     /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xca);     /* fxch swap e^-x with x*log2(e) */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy x*log2(e) */
	emit_byte(0xd9);
	emit_byte(0xfc);     /* frndint int(x*log2(e)) */
	emit_byte(0xd9);
	emit_byte(0xc9);     /* fxch swap */
	emit_byte(0xd8);
	emit_byte(0xe1);     /* fsub x*log2(e) - int(x*log2(e))  */
	emit_byte(0xd9);
	emit_byte(0xf0);     /* f2xm1 (2^frac(x))-1 */
	emit_byte(0xd8);
	emit_byte(0x05);
	emit_long((uae_u32)&one);  /* fadd (2^frac(x))-1 + 1 */
	emit_byte(0xd9);
	emit_byte(0xfd);     /* fscale (2^frac(x))*2^int(x*log2(e)) */
	emit_byte(0xdd);
	emit_byte(0xd1);     /* fst copy e^x */
	emit_byte(0xd8);
	emit_byte(0xc2);     /* fadd (e^x)+(e^-x) */
	emit_byte(0xd9);
	emit_byte(0xca);     /* fxch swap with e^-x */
	emit_byte(0xde);
	emit_byte(0xe9);     /* fsubp (e^x)-(e^-x) */
	if (tr>=0) {
		emit_byte(0xdb);
		emit_byte(0x2c);
		emit_byte(0x24); /* fld load temp-reg from [esp] */
		emit_byte(0x83);
		emit_byte(0xc4);
		emit_byte(0x0c); /* add +12 to esp */
		emit_byte(0xd9);
		emit_byte(0xca); /* fxch swap temp-reg with e^-x in tr */
		emit_byte(0xde);
		emit_byte(0xf9); /* fdivp ((e^x)-(e^-x))/((e^x)+(e^-x)) */
	}
	else {
		emit_byte(0xde);
		emit_byte(0xf1); /* fdivrp ((e^x)-(e^-x))/((e^x)+(e^-x)) */
	}
	if (s!=d)
		tos_make(d);     /* store y=tanh(x) */
}
LENDFUNC(NONE,NONE,2,raw_ftanh_rr,(FW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fneg_rr,(FW d, FR s))
{
	int ds;

	if (d!=s) {
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

	make_tos(d);
	ds=stackpos(s);

	emit_byte(0xdd);
	emit_byte(0xe0+ds); /* cmp dest with source*/
}
LENDFUNC(NONE,NONE,2,raw_fcmp_rr,(FR d, FR s))

	LOWFUNC(NONE,NONE,2,raw_fmul_rr,(FRW d, FR s))
{
	int ds;

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

	if (live.spos[d]==live.tos && live.spos[s]==live.tos-1) {
		//write_log (L"frem found x in TOS-1 and y in TOS\n");
		emit_byte(0xd9);
		emit_byte(0xf8);    /* fprem rem(y/x) */
	}
	else {
		make_tos(s);        /* tos=x */
		ds=stackpos(d);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld y */
		emit_byte(0xd9);
		emit_byte(0xf8);    /* fprem rem(y/x) */
		tos_make(d);        /* store y=rem(y/x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_frem_rr,(FRW d, FR s))

	LOWFUNC(NONE,NONE,2,raw_frem1_rr,(FRW d, FR s))
{
	int ds;

	if (live.spos[d]==live.tos && live.spos[s]==live.tos-1) {
		//write_log (L"frem1 found x in TOS-1 and y in TOS\n");
		emit_byte(0xd9);
		emit_byte(0xf5);    /* fprem1 rem1(y/x) */
	}
	else {
		make_tos(s);        /* tos=x */
		ds=stackpos(d);
		emit_byte(0xd9);
		emit_byte(0xc0+ds); /* fld y */
		emit_byte(0xd9);
		emit_byte(0xf5);    /* fprem1 rem1(y/x) */
		tos_make(d);        /* store y=rem(y/x) */
	}
}
LENDFUNC(NONE,NONE,2,raw_frem1_rr,(FRW d, FR s))

	LOWFUNC(NONE,NONE,1,raw_ftst_r,(FR r))
{
	make_tos(r);
	emit_byte(0xd9);  /* ftst */
	emit_byte(0xe4);
}
LENDFUNC(NONE,NONE,1,raw_ftst_r,(FR r))

	STATIC_INLINE void raw_fflags_into_flags(int r)
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
