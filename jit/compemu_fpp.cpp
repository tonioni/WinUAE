/*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68881 emulation
  *
  * Copyright 1996 Herman ten Brugge
  * Adapted for JIT compilation (c) Bernd Meyer, 2000
  * Modified 2005 Peter Keunecke
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "ersatz.h"
#include "md-fpp.h"
#include "compemu.h"

#if defined(JIT)
uae_u32 temp_fp[] = { 0, 0, 0 };  /* To convert between FP and <EA> */

/* 128 words, indexed through the low byte of the 68k fpu control word */
static const uae_u16 x86_fpucw[] = {
	0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, /* E-RN */
	0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, /* E-RZ */
	0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, /* E-RD */
	0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, /* E-RU */

	0x107f, 0x107f, 0x107f, 0x107f, 0x107f, 0x107f, 0x107f, 0x107f, /* S-RN */
	0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, /* S-RZ */
	0x147f, 0x147f, 0x147f, 0x147f, 0x147f, 0x147f, 0x147f, 0x147f, /* S-RD */
	0x187f, 0x187f, 0x187f, 0x187f, 0x187f, 0x187f, 0x187f, 0x187f, /* S-RU */

	0x127f, 0x127f, 0x127f, 0x127f, 0x127f, 0x127f, 0x127f, 0x127f, /* D-RN */
	0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, /* D-RZ */
	0x167f, 0x167f, 0x167f, 0x167f, 0x167f, 0x167f, 0x167f, 0x167f, /* D-RD */
	0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, /* D-RU */

	0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, /* ?-RN */
	0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, /* ?-RZ */
	0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, /* ?-RD */
	0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f  /* ?-RU */
};
static const int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
static const int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

static struct {
	double b[2];
	double w[2];
	double l[2];
} clamp_bounds = {
	{ -128.0, 127.0 },
	{ -32768.0, 32767.0 },
	{ -2147483648.0, 2147483647.0 }
};

/* return the required floating point precision or -1 for failure, 0=E, 1=S, 2=D */
STATIC_INLINE int comp_fp_get (uae_u32 opcode, uae_u16 extra, int treg)
{
	int reg = opcode & 7;
	int mode = (opcode >> 3) & 7;
	int size = (extra >> 10) & 7;

	if (size == 3 || size == 7) /* 3 = packed decimal, 7 is not defined */
		return -1;
	switch (mode) {
		case 0: /* Dn */
		switch (size) {
			case 0: /* Long */
			mov_l_mr (uae_p32(temp_fp), reg);
			fmovi_rm (treg, uae_p32(temp_fp));
			return 2;
			case 1: /* Single */
			mov_l_mr (uae_p32(temp_fp), reg);
			fmovs_rm (treg, uae_p32(temp_fp));
			return 1;
			case 4: /* Word */
			sign_extend_16_rr (S1, reg);
			mov_l_mr (uae_p32(temp_fp), S1);
			fmovi_rm (treg, uae_p32(temp_fp));
			return 1;
			case 6: /* Byte */
			sign_extend_8_rr (S1, reg);
			mov_l_mr (uae_p32(temp_fp), S1);
			fmovi_rm (treg, uae_p32(temp_fp));
			return 1;
			default:
			return -1;
		}
		case 1: /* An,  invalid mode */
		return -1;
		case 2: /* (An) */
		mov_l_rr (S1, reg + 8);
		break;
		case 3: /* (An)+ */
		mov_l_rr (S1, reg + 8);
		lea_l_brr (reg + 8, reg + 8, (reg == 7 ? sz2[size] : sz1[size]));
		break;
		case 4: /* -(An) */
		lea_l_brr (reg + 8, reg + 8, -(reg == 7 ? sz2[size] : sz1[size]));
		mov_l_rr (S1, reg + 8);
		break;
		case 5: /* (d16,An)  */
		{
			uae_u32 off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
			mov_l_rr (S1, reg + 8);
			lea_l_brr (S1, S1, off);
			break;
		}
		case 6: /* (d8,An,Xn) or (bd,An,Xn) or ([bd,An,Xn],od) or ([bd,An],Xn,od) */
		{
			uae_u32 dp = comp_get_iword ((m68k_pc_offset += 2) - 2);
			calc_disp_ea_020 (reg + 8, dp, S1, S2);
			break;
		}
		case 7:
		switch (reg) {
			case 0: /* (xxx).W */
			{
				uae_u32 off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
				mov_l_ri (S1, off);
				break;
			}
			case 1: /* (xxx).L */
			{
				uae_u32 off = comp_get_ilong ((m68k_pc_offset += 4) - 4);
				mov_l_ri (S1, off);
				break;
			}
			case 2: /* (d16,PC) */
			{
				uae_u32 address = start_pc + ((uae_char*) comp_pc_p - (uae_char*) start_pc_p) +
					m68k_pc_offset;
				uae_s32 PC16off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
				mov_l_ri (S1, address + PC16off);
				break;
			}
			case 3: /* (d8,PC,Xn) or (bd,PC,Xn) or ([bd,PC,Xn],od) or ([bd,PC],Xn,od) */
			return -1; /* rarely used, fallback to non-JIT */
			case 4: /* # < data >; Constants should be converted just once by the JIT */
			m68k_pc_offset += sz2[size];
			switch (size) {
				case 0:
				{
					uae_s32 li = comp_get_ilong(m68k_pc_offset - 4);
					float si = (float)li;

					if (li == (int)si) {
						//write_log ("converted immediate LONG constant to SINGLE\n");
						mov_l_mi(uae_p32(temp_fp), *(uae_u32 *)&si);
						fmovs_rm(treg, uae_p32(temp_fp));
						return 1;
					}
					//write_log ("immediate LONG constant\n");
					mov_l_mi(uae_p32(temp_fp), *(uae_u32 *)&li);
					fmovi_rm(treg, uae_p32(temp_fp));
					return 2;
				}
				case 1:
				//write_log (_T("immediate SINGLE constant\n"));
				mov_l_mi(uae_p32(temp_fp), comp_get_ilong(m68k_pc_offset - 4));
				fmovs_rm(treg, uae_p32(temp_fp));
				return 1;
				case 2:
				//write_log (_T("immediate LONG DOUBLE constant\n"));
				mov_l_mi(uae_p32(temp_fp), comp_get_ilong(m68k_pc_offset - 4));
				mov_l_mi((uae_p32(temp_fp)) + 4, comp_get_ilong(m68k_pc_offset - 8));
				mov_l_mi((uae_p32(temp_fp)) + 8, (uae_u32)comp_get_iword(m68k_pc_offset - 12));
				fmov_ext_rm(treg, uae_p32(temp_fp));
				return 0;
				case 4:
				{
					float si = (float)(uae_s16)comp_get_iword(m68k_pc_offset-2);

					//write_log (_T("converted immediate WORD constant %f to SINGLE\n"), si);
					mov_l_mi(uae_p32(temp_fp),*(uae_u32 *)&si);
					fmovs_rm(treg,uae_p32(temp_fp));
					return 1;
				}
				case 5:
				{
					uae_u32 longarray[] = { comp_get_ilong(m68k_pc_offset - 4),
						comp_get_ilong(m68k_pc_offset - 8) };
					float si = (float)*(double *)longarray;

					if (*(double *)longarray == (double)si) {
						//write_log (_T("SPEED GAIN: converted a DOUBLE constant to SINGLE\n"));
						mov_l_mi(uae_p32(temp_fp), *(uae_u32 *)&si);
						fmovs_rm(treg, uae_p32(temp_fp));
						return 1;
					}
					//write_log (_T("immediate DOUBLE constant\n"));
					mov_l_mi(uae_p32(temp_fp), longarray[0]);
					mov_l_mi((uae_p32(temp_fp)) + 4, longarray[1]);
					fmov_rm(treg, uae_p32(temp_fp));
					return 2;
				}
				case 6:
				{
					float si = (float)(uae_s8)comp_get_ibyte(m68k_pc_offset - 2);

					//write_log (_T("converted immediate BYTE constant to SINGLE\n"));
					mov_l_mi(uae_p32(temp_fp), *(uae_u32 *)&si);
					fmovs_rm(treg, uae_p32(temp_fp));
					return 1;
				}
				default: /* never reached */
				return -1;
			}
			default: /* never reached */
			return -1;
		}
	}

	switch (size) {
		case 0: /* Long */
		readlong (S1, S2, S3);
		mov_l_mr (uae_p32(temp_fp), S2);
		fmovi_rm (treg, uae_p32(temp_fp));
		return 2;
		case 1: /* Single */
		readlong (S1, S2, S3);
		mov_l_mr (uae_p32(temp_fp), S2);
		fmovs_rm (treg, uae_p32(temp_fp));
		return 1;
		case 2: /* Long Double */
		readword (S1, S2, S3);
		mov_w_mr ((uae_p32(temp_fp)) + 8, S2);
		add_l_ri (S1, 4);
		readlong (S1, S2, S3);
		mov_l_mr ((uae_p32(temp_fp)) + 4, S2);
		add_l_ri (S1, 4);
		readlong (S1, S2, S3);
		mov_l_mr ((uae_p32(temp_fp)), S2);
		fmov_ext_rm (treg, uae_p32(temp_fp));
		return 0;
		case 4: /* Word */
		readword (S1, S2, S3);
		sign_extend_16_rr (S2, S2);
		mov_l_mr (uae_p32(temp_fp), S2);
		fmovi_rm (treg, uae_p32(temp_fp));
		return 1;
		case 5: /* Double */
		readlong (S1, S2, S3);
		mov_l_mr ((uae_p32(temp_fp)) + 4, S2);
		add_l_ri (S1, 4);
		readlong (S1, S2, S3);
		mov_l_mr ((uae_p32(temp_fp)), S2);
		fmov_rm (treg, uae_p32(temp_fp));
		return 2;
		case 6: /* Byte */
		readbyte (S1, S2, S3);
		sign_extend_8_rr (S2, S2);
		mov_l_mr (uae_p32(temp_fp), S2);
		fmovi_rm (treg, uae_p32(temp_fp));
		return 1;
		default:
		return -1;
	}
	return -1;
}

/* return of -1 means failure, >=0 means OK */
STATIC_INLINE int comp_fp_put (uae_u32 opcode, uae_u16 extra)
{
	int reg = opcode & 7;
	int sreg = (extra >> 7) & 7;
	int mode = (opcode >> 3) & 7;
	int size = (extra >> 10) & 7;

	if (size == 3 || size == 7) /* 3 = packed decimal, 7 is not defined */
		return -1;
	switch (mode) {
		case 0: /* Dn */
		switch (size) {
			case 0: /* FMOVE.L FPx, Dn */
#if USE_X86_FPUCW && 0
			if (!(regs.fpcr & 0xf0)) { /* if extended round to nearest */
				mov_l_ri(S1,0x10); /* use extended round to zero mode */
				fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
				fmovi_mrb(uae_p32(temp_fp),sreg, clamp_bounds.l);
				mov_l_rm(reg,uae_p32(temp_fp));
				mov_l_rm(S1,(uae_u32)&regs.fpcr);
				and_l_ri(S1,0xf0); /* restore control word */
				fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
				return 0;
			}
#endif
			fmovi_mrb (uae_p32(temp_fp), sreg, clamp_bounds.l);
			mov_l_rm (reg, uae_p32(temp_fp));
			return 0;
			case 1: /* FMOVE.S FPx, Dn */
			fmovs_mr (uae_p32(temp_fp), sreg);
			mov_l_rm (reg, uae_p32(temp_fp));
			return 0;
			case 4: /* FMOVE.W FPx, Dn */
#if USE_X86_FPUCW && 0
			if (!(regs.fpcr & 0xf0)) { /* if extended round to nearest */
				mov_l_ri(S1,0x10); /* use extended round to zero mode */
				fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
				fmovi_mrb(uae_p32(temp_fp),sreg, clamp_bounds.w);
				mov_w_rm(reg,uae_p32(temp_fp));
				mov_l_rm(S1,(uae_u32)&regs.fpcr);
				and_l_ri(S1,0xf0); /* restore control word */
				fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
				return 0;
			}
#endif
			fmovi_mrb (uae_p32(temp_fp), sreg, clamp_bounds.w);
			mov_w_rm (reg, uae_p32(temp_fp));
			return 0;
			case 6: /* FMOVE.B FPx, Dn */
#if USE_X86_FPUCW && 0
			if (!(regs.fpcr & 0xf0)) { /* if extended round to nearest */
				mov_l_ri(S1,0x10); /* use extended round to zero mode */
				fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
				fmovi_mrb(uae_p32(temp_fp),sreg, clamp_bounds.b);
				mov_b_rm(reg,uae_p32(temp_fp));
				mov_l_rm(S1,(uae_u32)&regs.fpcr);
				and_l_ri(S1,0xf0); /* restore control word */
				fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
				return 0;
			}
#endif
			fmovi_mrb (uae_p32(temp_fp), sreg, clamp_bounds.b);
			mov_b_rm (reg, uae_p32(temp_fp));
			return 0;
			default:
			return -1;
		}
		case 1: /* An, invalid mode */
		return -1;
		case 2: /* (An) */
		mov_l_rr (S1, reg + 8);
		break;
		case 3: /* (An)+ */
		mov_l_rr (S1, reg + 8);
		lea_l_brr (reg + 8, reg + 8, (reg == 7 ? sz2[size] : sz1[size]));
		break;
		case 4: /* -(An) */
		lea_l_brr (reg + 8, reg + 8, -(reg == 7 ? sz2[size] : sz1[size]));
		mov_l_rr (S1, reg + 8);
		break;
		case 5: /* (d16,An) */
		{
			uae_u32 off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
			mov_l_rr (S1, reg + 8);
			add_l_ri (S1, off);
			break;
		}
		case 6: /* (d8,An,Xn) or (bd,An,Xn) or ([bd,An,Xn],od) or ([bd,An],Xn,od) */
		{
			uae_u32 dp = comp_get_iword ((m68k_pc_offset += 2) - 2);
			calc_disp_ea_020 (reg + 8, dp, S1, S2);
			break;
		}
		case 7:
		switch (reg) {
			case 0: /* (xxx).W */
			{
				uae_u32 off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
				mov_l_ri (S1, off);
				break;
			}
			case 1: /* (xxx).L */
			{
				uae_u32 off = comp_get_ilong ((m68k_pc_offset += 4) - 4);
				mov_l_ri (S1, off);
				break;
			}
			default: /* All other modes are not allowed for FPx to <EA> */
			write_log (_T ("JIT FMOVE FPx,<EA> Mode is not allowed %04x %04x\n"), opcode, extra);
			return -1;
		}
	}
	switch (size) {
		case 0: /* Long */
		fmovi_mrb (uae_p32(temp_fp), sreg, clamp_bounds.l);
		mov_l_rm (S2, uae_p32(temp_fp));
		writelong_clobber (S1, S2, S3);
		return 0;
		case 1: /* Single */
		fmovs_mr (uae_p32(temp_fp), sreg);
		mov_l_rm (S2, uae_p32(temp_fp));
		writelong_clobber (S1, S2, S3);
		return 0;
		case 2:/* Long Double */
		fmov_ext_mr (uae_p32(temp_fp), sreg);
		mov_w_rm (S2, uae_p32(temp_fp) + 8);
		writeword_clobber (S1, S2, S3);
		add_l_ri (S1, 4);
		mov_l_rm (S2, uae_p32(temp_fp) + 4);
		writelong_clobber (S1, S2, S3);
		add_l_ri (S1, 4);
		mov_l_rm (S2, uae_p32(temp_fp));
		writelong_clobber (S1, S2, S3);
		return 0;
		case 4: /* Word */
		fmovi_mrb (uae_p32(temp_fp), sreg, clamp_bounds.w);
		mov_l_rm (S2, uae_p32(temp_fp));
		writeword_clobber (S1, S2, S3);
		return 0;
		case 5: /* Double */
		fmov_mr (uae_p32(temp_fp), sreg);
		mov_l_rm (S2, uae_p32(temp_fp) + 4);
		writelong_clobber (S1, S2, S3);
		add_l_ri (S1, 4);
		mov_l_rm (S2, uae_p32(temp_fp));
		writelong_clobber (S1, S2, S3);
		return 0;
		case 6: /* Byte */
		fmovi_mrb (uae_p32(temp_fp), sreg, clamp_bounds.b);
		mov_l_rm (S2, uae_p32(temp_fp));
		writebyte (S1, S2, S3);
		return 0;
		default:
		return -1;
	}
	return -1;
}

/* return -1 for failure, or register number for success */
STATIC_INLINE int comp_fp_adr (uae_u32 opcode)
{
	uae_s32 off;
	int mode = (opcode >> 3) & 7;
	int reg = opcode & 7;

	switch (mode) {
		case 2:
		case 3:
		case 4:
		mov_l_rr (S1, 8 + reg);
		return S1;
		case 5:
		off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
		mov_l_rr (S1, 8 + reg);
		add_l_ri (S1, off);
		return S1;
		case 7:
		switch (reg) {
			case 0:
			off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
			mov_l_ri (S1, off);
			return S1;
			case 1:
			off = comp_get_ilong ((m68k_pc_offset += 4) - 4);
			mov_l_ri (S1, off);
			return S1;
		}
		default:
		return -1;
	}
}

void comp_fdbcc_opp (uae_u32 opcode, uae_u16 extra)
{
	FAIL (1);
	return;
}

void comp_fscc_opp (uae_u32 opcode, uae_u16 extra)
{
	int reg;

	if (!currprefs.compfpu) {
		FAIL (1);
		return;
	}

#if DEBUG_FPP
	write_log (_T("JIT: fscc_opp at %08lx\n"), M68K_GETPC);
#endif

	if (extra & 0x20) {  /* only cc from 00 to 1f are defined */
		FAIL (1);
		return;
	}
	if ((opcode & 0x38) != 0) { /* We can only do to integer register */
		FAIL (1);
		return;
	}

	fflags_into_flags (S2);
	reg = (opcode & 7);

	mov_l_ri (S1, 255);
	mov_l_ri (S4, 0);
	switch (extra & 0x0f) { /* according to fpp.c, the 0x10 bit is ignored */
		case 0: break;  /* set never */
		case 1: mov_l_rr (S2, S4);
			cmov_l_rr (S4, S1, 4);
			cmov_l_rr (S4, S2, 10); break;
		case 2: cmov_l_rr (S4, S1, 7); break;
		case 3: cmov_l_rr (S4, S1, 3); break;
		case 4: mov_l_rr (S2, S4);
			cmov_l_rr (S4, S1, 2);
			cmov_l_rr (S4, S2, 10); break;
		case 5: mov_l_rr (S2, S4);
			cmov_l_rr (S4, S1, 6);
			cmov_l_rr (S4, S2, 10); break;
		case 6: cmov_l_rr (S4, S1, 5); break;
		case 7: cmov_l_rr (S4, S1, 11); break;
		case 8: cmov_l_rr (S4, S1, 10); break;
		case 9: cmov_l_rr (S4, S1, 4); break;
		case 10: cmov_l_rr (S4, S1, 10); cmov_l_rr (S4, S1, 7); break;
		case 11: cmov_l_rr (S4, S1, 4); cmov_l_rr (S4, S1, 3); break;
		case 12: cmov_l_rr (S4, S1, 2); break;
		case 13: cmov_l_rr (S4, S1, 6); break;
		case 14: cmov_l_rr (S4, S1, 5); cmov_l_rr (S4, S1, 10); break;
		case 15: mov_l_rr (S4, S1); break;
	}

	if (!(opcode & 0x38))
		mov_b_rr (reg, S4);
#if 0
	else {
		abort();
		if (!comp_fp_adr (opcode)) {
			m68k_setpc (m68k_getpc () - 4);
			op_illg (opcode);
		}
		else
			put_byte (ad, cc ? 0xff : 0x00);
	}
#endif
}

void comp_ftrapcc_opp (uae_u32 opcode, uaecptr oldpc)
{
	FAIL (1);
	return;
}

extern unsigned long foink3, oink;

void comp_fbcc_opp (uae_u32 opcode)
{
	uae_u32 start_68k_offset = m68k_pc_offset;
	uae_u32 off, v1, v2;
	int cc;

	if (!currprefs.compfpu) {
		FAIL (1);
		return;
	}

	// comp_pc_p is expected to be bound to 32-bit addresses
	assert((uintptr) comp_pc_p <= 0xffffffffUL);

	if (opcode & 0x20) {  /* only cc from 00 to 1f are defined */
		FAIL (1);
		return;
	}
	if (!(opcode & 0x40)) {
		off = (uae_s32) (uae_s16) comp_get_iword ((m68k_pc_offset += 2) - 2);
	}
	else {
		off = comp_get_ilong ((m68k_pc_offset += 4) - 4);
	}

	/* Note, "off" will sometimes be (unsigned) "negative", so the following
         * uintptr can be > 0xffffffff, but the result will be correct due to
         * wraparound when truncated to 32 bit in the call to mov_l_ri. */
	mov_l_ri(S1, (uintptr)
		(comp_pc_p + off - (m68k_pc_offset - start_68k_offset)));
	mov_l_ri(PC_P, (uintptr) comp_pc_p);

	/* Now they are both constant. Might as well fold in m68k_pc_offset */
	add_l_ri (S1, m68k_pc_offset);
	add_l_ri (PC_P, m68k_pc_offset);
	m68k_pc_offset = 0;

	/* according to fpp.c, the 0x10 bit is ignored
	   (it handles exception handling, which we don't
	   do, anyway ;-) */
	cc = opcode & 0x0f;
	v1 = get_const (PC_P);
	v2 = get_const (S1);
	fflags_into_flags (S2);

	// mov_l_mi((uae_u32)&foink3,cc);
	switch (cc) {
		case 0: break;  /* jump never */
		case 1:
		mov_l_rr (S2, PC_P);
		cmov_l_rr (PC_P, S1, 4);
		cmov_l_rr (PC_P, S2, 10); break;
		case 2: register_branch (v1, v2, 7); break;
		case 3: register_branch (v1, v2, 3); break;
		case 4:
		mov_l_rr (S2, PC_P);
		cmov_l_rr (PC_P, S1, 2);
		cmov_l_rr (PC_P, S2, 10); break;
		case 5:
		mov_l_rr (S2, PC_P);
		cmov_l_rr (PC_P, S1, 6);
		cmov_l_rr (PC_P, S2, 10); break;
		case 6: register_branch (v1, v2, 5); break;
		case 7: register_branch (v1, v2, 11); break;
		case 8: register_branch (v1, v2, 10); break;
		case 9: register_branch (v1, v2, 4); break;
		case 10:
		cmov_l_rr (PC_P, S1, 10);
		cmov_l_rr (PC_P, S1, 7); break;
		case 11:
		cmov_l_rr (PC_P, S1, 4);
		cmov_l_rr (PC_P, S1, 3); break;
		case 12: register_branch (v1, v2, 2); break;
		case 13: register_branch (v1, v2, 6); break;
		case 14:
		cmov_l_rr (PC_P, S1, 5);
		cmov_l_rr (PC_P, S1, 10); break;
		case 15: mov_l_rr (PC_P, S1); break;
	}
}

/* Floating point conditions
   The "NotANumber" part could be problematic; Howver, when NaN is
   encountered, the ftst instruction sets bot N and Z to 1 on the x87,
   so quite often things just fall into place. This is probably not
   accurate wrt the 68k FPU, but it is *as* accurate as this was before.
   However, some more thought should go into fixing this stuff up so
   it accurately emulates the 68k FPU.
   >=<U
   0000    0x00: 0                        ---   Never jump
   0101    0x01: Z                        ---   jump if zero (x86: 4)
   1000    0x02: !(NotANumber || Z || N)  --- Neither Z nor N set (x86: 7)
   1101    0x03: Z || !(NotANumber || N); --- Z or !N (x86: 4 and 3)
   0010    0x04: N && !(NotANumber || Z); --- N and !Z (x86: hard!)
   0111    0x05: Z || (N && !NotANumber); --- Z or N (x86: 6)
   1010    0x06: !(NotANumber || Z);      --- not Z (x86: 5)
   1110    0x07: !NotANumber;             --- not NaN (x86: 11, not parity)
   0001    0x08: NotANumber;              --- NaN (x86: 10)
   0101    0x09: NotANumber || Z;         --- Z (x86: 4)
   1001    0x0a: NotANumber || !(N || Z); --- NaN or neither N nor Z (x86: 10 and 7)
   1101    0x0b: NotANumber || Z || !N;   --- Z or !N (x86: 4 and 3)
   0011    0x0c: NotANumber || (N && !Z); --- N (x86: 2)
   0111    0x0d: NotANumber || Z || N;    --- Z or N (x86: 6)
   1010    0x0e: !Z;                      --- not Z (x86: 5)
   1111    0x0f: 1;                       --- always

   This is not how the 68k handles things, though --- it sets Z to 0 and N
   to the NaN's sign.... ('o' and 'i' denote differences from the above
   table)

   >=<U
   0000    0x00: 0                        ---   Never jump
   010o    0x01: Z                        ---   jump if zero (x86: 4, not 10)
   1000    0x02: !(NotANumber || Z || N)  --- Neither Z nor N set (x86: 7)
   110o    0x03: Z || !(NotANumber || N); --- Z or !N (x86: 3)
   0010    0x04: N && !(NotANumber || Z); --- N and !Z (x86: 2, not 10)
   011o    0x05: Z || (N && !NotANumber); --- Z or N (x86: 6, not 10)
   1010    0x06: !(NotANumber || Z);      --- not Z (x86: 5)
   1110    0x07: !NotANumber;             --- not NaN (x86: 11, not parity)
   0001    0x08: NotANumber;              --- NaN (x86: 10)
   0101    0x09: NotANumber || Z;         --- Z (x86: 4)
   1001    0x0a: NotANumber || !(N || Z); --- NaN or neither N nor Z (x86: 10 and 7)
   1101    0x0b: NotANumber || Z || !N;   --- Z or !N (x86: 4 and 3)
   0011    0x0c: NotANumber || (N && !Z); --- N (x86: 2)
   0111    0x0d: NotANumber || Z || N;    --- Z or N (x86: 6)
   101i    0x0e: !Z;                      --- not Z (x86: 5 and 10)
   1111    0x0f: 1;                       --- always

   Of course, this *still* doesn't mean that the x86 and 68k conditions are
   equivalent --- the handling of infinities is different, for one thing.
   On the 68k, +infinity minus +infinity is NotANumber (as it should be). On
   the x86, it is +infinity, and some exception is raised (which I suspect
   is promptly ignored) STUPID!
   The more I learn about their CPUs, the more I detest Intel....

   You can see this in action if you have "Benoit" (see Aminet) and
   set the exponent to 16. Wait for a long time, and marvel at the extra black
   areas outside the center one. That's where Benoit expects NaN, and the x86
   gives +infinity. [Ooops --- that must have been some kind of bug in my code.
   it no longer happens, and the resulting graphic looks much better, too]

   x86 conditions
   0011    : 2
   1100    : 3
   0101    : 4
   1010    : 5
   0111    : 6
   1000    : 7
   0001    : 10
   1110    : 11
   */
void comp_fsave_opp (uae_u32 opcode)
{
	FAIL (1);
	return;
}

void comp_frestore_opp (uae_u32 opcode)
{
	FAIL (1);
	return;
}

extern uae_u32 xhex_pi[], xhex_exp_1[], xhex_l2_e[], xhex_ln_2[], xhex_ln_10[];
extern uae_u32 xhex_l10_2[], xhex_l10_e[], xhex_1e16[], xhex_1e32[], xhex_1e64[];
extern uae_u32 xhex_1e128[], xhex_1e256[], xhex_1e512[], xhex_1e1024[];
extern uae_u32 xhex_1e2048[], xhex_1e4096[];
extern double fp_1e8;
extern float  fp_1e1, fp_1e2, fp_1e4;

void comp_fpp_opp (uae_u32 opcode, uae_u16 extra)
{
	int reg;
	int sreg, prec = 0;
	int	dreg = (extra >> 7) & 7;
	int source = (extra >> 13) & 7;
	int	opmode = extra & 0x7f;

	if (!currprefs.compfpu) {
		FAIL (1);
		return;
	}
	switch (source) {
		case 3: /* FMOVE FPx, <EA> */
		if (comp_fp_put (opcode, extra) < 0)
			FAIL (1);
		return;
		case 4: /* FMOVE.L  <EA>, ControlReg */
		if (!(opcode & 0x30)) { /* Dn or An */
			if (extra & 0x1000) { /* FPCR */
				mov_l_mr (uae_p32(&regs.fpcr), opcode & 15);
#if USE_X86_FPUCW
				mov_l_rr (S1, opcode & 15);
				and_l_ri (S1, 0xf0);
				fldcw_m_indexed (S1, uae_p32(x86_fpucw));
#endif
				return;
			}
			if (extra & 0x0800) { /* FPSR */
				FAIL (1);
				return;
				// set_fpsr(m68k_dreg (regs, opcode & 15));
			}
			if (extra & 0x0400) { /* FPIAR */
				mov_l_mr (uae_p32(&regs.fpiar), opcode & 15); return;
			}
		}
		else if ((opcode & 0x3f) == 0x3c) {
			if (extra & 0x1000) { /* FPCR */
				uae_u32 val = comp_get_ilong ((m68k_pc_offset += 4) - 4);
				mov_l_mi (uae_p32(&regs.fpcr), val);
#if USE_X86_FPUCW
				mov_l_ri (S1, val & 0xf0);
				fldcw_m_indexed (S1, uae_p32(x86_fpucw));
#endif
				return;
			}
			if (extra & 0x0800) { /* FPSR */
				FAIL (1);
				return;
			}
			if (extra & 0x0400) { /* FPIAR */
				uae_u32 val = comp_get_ilong ((m68k_pc_offset += 4) - 4);
				mov_l_mi (uae_p32(&regs.fpiar), val);
				return;
			}
		}
		FAIL (1);
		return;
		case 5: /* FMOVE.L  ControlReg, <EA> */
		if (!(opcode & 0x30)) { /* Dn or An */
			if (extra & 0x1000) { /* FPCR */
				mov_l_rm (opcode & 15, uae_p32(&regs.fpcr)); return;
			}
			if (extra & 0x0800) { /* FPSR */
				FAIL (1);
				return;
			}
			if (extra & 0x0400) { /* FPIAR */
				mov_l_rm (opcode & 15, uae_p32(&regs.fpiar)); return;
			}
		}
		FAIL (1);
		return;
		case 6:
		case 7:
		{
			uae_u32 list = 0;
			int incr = 0;
			if (extra & 0x2000) {
				int ad;

				/* FMOVEM FPP->memory */
				switch ((extra >> 11) & 3) { /* Get out early if failure */
					case 0:
					case 2:
					break;
					case 1:
					case 3:
					default:
					FAIL (1); return;
				}
				ad = comp_fp_adr (opcode);
				if (ad < 0) {
					m68k_setpc (m68k_getpc () - 4);
					op_illg (opcode);
					return;
				}
				switch ((extra >> 11) & 3) {
					case 0:	/* static pred */
					list = extra & 0xff;
					incr = -1;
					break;
					case 2:	/* static postinc */
					list = extra & 0xff;
					incr = 1;
					break;
					case 1:	/* dynamic pred */
					case 3:	/* dynamic postinc */
					abort ();
				}
				if (incr < 0) { /* Predecrement */
					for (reg = 7; reg >= 0; reg--) {
						if (list & 0x80) {
							fmov_ext_mr ((uintptr) temp_fp, reg);
							sub_l_ri (ad, 4);
							mov_l_rm (S2, (uintptr) temp_fp);
							writelong_clobber (ad, S2, S3);
							sub_l_ri (ad, 4);
							mov_l_rm (S2, (uintptr) temp_fp + 4);
							writelong_clobber (ad, S2, S3);
							sub_l_ri (ad, 4);
							mov_w_rm (S2, (uintptr) temp_fp + 8);
							writeword_clobber (ad, S2, S3);
						}
						list <<= 1;
					}
				} else { /* Postincrement */
					for (reg = 0; reg <= 7; reg++) {
						if (list & 0x80) {
							fmov_ext_mr ((uintptr) temp_fp, reg);
							mov_w_rm (S2, (uintptr) temp_fp + 8);
							writeword_clobber (ad, S2, S3);
							add_l_ri (ad, 4);
							mov_l_rm (S2, (uintptr) temp_fp + 4);
							writelong_clobber (ad, S2, S3);
							add_l_ri (ad, 4);
							mov_l_rm (S2, (uintptr) temp_fp);
							writelong_clobber (ad, S2, S3);
							add_l_ri (ad, 4);
						}
						list <<= 1;
					}
				}
				if ((opcode & 0x38) == 0x18)
					mov_l_rr ((opcode & 7) + 8, ad);
				if ((opcode & 0x38) == 0x20)
					mov_l_rr ((opcode & 7) + 8, ad);
			} else {
				/* FMOVEM memory->FPP */

				int ad;
				switch ((extra >> 11) & 3) { /* Get out early if failure */
					case 0:
					case 2:
					break;
					case 1:
					case 3:
					default:
					FAIL (1); return;
				}
				ad = comp_fp_adr (opcode);
				if (ad < 0) {
					m68k_setpc (m68k_getpc () - 4);
					op_illg (opcode);
					return;
				}
				switch ((extra >> 11) & 3) {
					case 0:	/* static pred */
					list = extra & 0xff;
					incr = -1;
					break;
					case 2:	/* static postinc */
					list = extra & 0xff;
					incr = 1;
					break;
					case 1:	/* dynamic pred */
					case 3:	/* dynamic postinc */
					abort ();
				}

				if (incr < 0) {
					// not reached
					for (reg = 7; reg >= 0; reg--) {
						if (list & 0x80) {
							sub_l_ri (ad, 4);
							readlong (ad, S2, S3);
							mov_l_mr ((uintptr) (temp_fp), S2);
							sub_l_ri (ad, 4);
							readlong (ad, S2, S3);
							mov_l_mr ((uintptr) (temp_fp) +4, S2);
							sub_l_ri (ad, 4);
							readword (ad, S2, S3);
							mov_w_mr (((uintptr) temp_fp) + 8, S2);
							fmov_ext_rm (reg, (uintptr) (temp_fp));
						}
						list <<= 1;
					}
				} else {
					for (reg = 0; reg <= 7; reg++) {
						if (list & 0x80) {
							readword (ad, S2, S3);
							mov_w_mr (((uintptr) temp_fp) + 8, S2);
							add_l_ri (ad, 4);
							readlong (ad, S2, S3);
							mov_l_mr ((uintptr) (temp_fp) +4, S2);
							add_l_ri (ad, 4);
							readlong (ad, S2, S3);
							mov_l_mr ((uintptr) (temp_fp), S2);
							add_l_ri (ad, 4);
							fmov_ext_rm (reg, (uintptr) (temp_fp));
						}
						list <<= 1;
					}
				}
				if ((opcode & 0x38) == 0x18)
					mov_l_rr ((opcode & 7) + 8, ad);
				if ((opcode & 0x38) == 0x20)
					mov_l_rr ((opcode & 7) + 8, ad);
			}
		}
		return;
#if 0
		case 6: /* FMOVEM  <EA>, FPx-FPz */
		if (!(extra & 0x0800)) {
			uae_u32 list = extra & 0xff;
			int ad;
			if ((ad = comp_fp_adr(opcode)) < 0) {FAIL(1);return;}
			while (list) {
				if  (extra & 0x1000) { /* postincrement */
					readword(ad,S2,S3);
					mov_w_mr((uae_p32(temp_fp))+8,S2);
					add_l_ri(ad,4);
					readlong(ad,S2,S3);
					mov_l_mr((uae_u32)(temp_fp)+4,S2);
					add_l_ri(ad,4);
					readlong(ad,S2,S3);
					mov_l_mr((uae_u32)(temp_fp),S2);
					add_l_ri(ad,4);
					fmov_ext_rm(fpp_movem_index1[list],(uae_u32)(temp_fp));
				} else { /* predecrement */
					sub_l_ri(ad,4);
					readlong(ad,S2,S3);
					mov_l_mr((uae_u32)(temp_fp),S2);
					sub_l_ri(ad,4);
					readlong(ad,S2,S3);
					mov_l_mr((uae_u32)(temp_fp)+4,S2);
					sub_l_ri(ad,4);
					readword(ad,S2,S3);
					mov_w_mr((uae_p32(temp_fp))+8,S2);
					fmov_ext_rm(fpp_movem_index2[list],(uae_u32)(temp_fp));
				}
				list = fpp_movem_next[list];
			}
			if ((opcode & 0x38) == 0x18)
				mov_l_rr((opcode & 7)+8,ad);
			return;
		} /* no break for dynamic register list */
		case 7: /* FMOVEM  FPx-FPz, <EA> */
		if (!(extra & 0x0800)) {
			uae_u32 list = extra & 0xff;
			int ad;
			if ((ad = comp_fp_adr(opcode)) < 0) {FAIL(1);return;}
			while (list) {
				if (extra & 0x1000) { /* postincrement */
					fmov_ext_mr(uae_p32(temp_fp),fpp_movem_index2[list]);
					mov_w_rm(S2,uae_p32(temp_fp)+8);
					writeword_clobber(ad,S2,S3);
					add_l_ri(ad,4);
					mov_l_rm(S2,uae_p32(temp_fp)+4);
					writelong_clobber(ad,S2,S3);
					add_l_ri(ad,4);
					mov_l_rm(S2,uae_p32(temp_fp));
					writelong_clobber(ad,S2,S3);
					add_l_ri(ad,4);
				} else { /* predecrement */
					fmov_ext_mr(uae_p32(temp_fp),fpp_movem_index2[list]);
					sub_l_ri(ad,4);
					mov_l_rm(S2,uae_p32(temp_fp));
					writelong_clobber(ad,S2,S3);
					sub_l_ri(ad,4);
					mov_l_rm(S2,uae_p32(temp_fp)+4);
					writelong_clobber(ad,S2,S3);
					sub_l_ri(ad,4);
					mov_w_rm(S2,uae_p32(temp_fp)+8);
					writeword_clobber(ad,S2,S3);
				}
				list = fpp_movem_next[list];
			}
			if ((opcode & 0x38) == 0x20)
				mov_l_rr((opcode & 7)+8,ad);
			return;
		} /* no break */
		write_log (_T("fallback from JIT FMOVEM dynamic register list\n"));
		FAIL(1);
		return;
#endif
		case 2: /* from <EA> to FPx */
		dont_care_fflags ();
		if ((extra & 0xfc00) == 0x5c00) { /* FMOVECR */
			//write_log (_T("JIT FMOVECR %x\n"), opmode);
			switch (opmode) {
				case 0x00:
				fmov_pi (dreg);
				break;
				case 0x0b:
				fmov_ext_rm (dreg, uae_p32(&xhex_l10_2));
				break;
				case 0x0c:
				fmov_ext_rm (dreg, uae_p32(&xhex_exp_1));
				break;
				case 0x0d:
				fmov_log2_e (dreg);
				break;
				case 0x0e:
				fmov_ext_rm (dreg, uae_p32(&xhex_l10_e));
				break;
				case 0x0f:
				fmov_0 (dreg);
				break;
				case 0x30:
				fmov_loge_2 (dreg);
				break;
				case 0x31:
				fmov_ext_rm (dreg, uae_p32(&xhex_ln_10));
				break;
				case 0x32:
				fmov_1 (dreg);
				break;
				case 0x33:
				fmovs_rm (dreg, uae_p32(&fp_1e1));
				break;
				case 0x34:
				fmovs_rm (dreg, uae_p32(&fp_1e2));
				break;
				case 0x35:
				fmovs_rm (dreg, uae_p32(&fp_1e4));
				break;
				case 0x36:
				fmov_rm (dreg, uae_p32(&fp_1e8));
				break;
				case 0x37:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e16));
				break;
				case 0x38:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e32));
				break;
				case 0x39:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e64));
				break;
				case 0x3a:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e128));
				break;
				case 0x3b:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e256));
				break;
				case 0x3c:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e512));
				break;
				case 0x3d:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e1024));
				break;
				case 0x3e:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e2048));
				break;
				case 0x3f:
				fmov_ext_rm (dreg, uae_p32(&xhex_1e4096));
				break;
				default:
				FAIL (1);
				return;
			}
			fmov_rr (FP_RESULT, dreg);
			return;
		}
		if (opmode & 0x20) /* two operands, so we need a scratch reg */
			sreg = FS1;
		else /* one operand only, thus we can load the argument into dreg */
			sreg = dreg;
		if ((prec = comp_fp_get (opcode, extra, sreg)) < 0) {
			FAIL (1);
			return;
		}
		if (!opmode) { /* FMOVE  <EA>,FPx */
			fmov_rr (FP_RESULT, dreg);
			return;
		}
		/* no break here for <EA> to dreg */
		case 0: /* directly from sreg to dreg */
		if (!source) { /* no <EA> */
			dont_care_fflags ();
			sreg = (extra >> 10) & 7;
		}
		switch (opmode) {
			case 0x00: /* FMOVE */
			fmov_rr (dreg, sreg);
			break;
			case 0x01: /* FINT */
			frndint_rr (dreg, sreg);
			break;
			case 0x02: /* FSINH */
			fsinh_rr (dreg, sreg);
			break;
			case 0x03: /* FINTRZ */
#if USE_X86_FPUCW /* if we have control over the CW, we can do this */
			if (0 && (regs.fpcr & 0xf0) == 0x10) /* maybe unsafe, because this test is done */
				frndint_rr (dreg, sreg); /* during the JIT compilation and not at runtime */
			else {
				mov_l_ri (S1, 0x10); /* extended round to zero */
				fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				frndint_rr (dreg, sreg);
				mov_l_rm (S1, uae_p32(&regs.fpcr));
				and_l_ri (S1, 0xf0); /* restore control word */
				fldcw_m_indexed (S1, uae_p32(x86_fpucw));
			}
			break;
#endif
			FAIL (1);
			return;
			case 0x04: /* FSQRT */
			fsqrt_rr (dreg, sreg);
			break;
			case 0x06: /* FLOGNP1 */
			flogNP1_rr (dreg, sreg);
			break;
			case 0x08: /* FETOXM1 */
			fetoxM1_rr (dreg, sreg);
			break;
			case 0x09: /* FTANH */
			ftanh_rr (dreg, sreg);
			break;
			case 0x0a: /* FATAN */
			fatan_rr (dreg, sreg);
			break;
			case 0x0c: /* FASIN */
			fasin_rr (dreg, sreg);
			break;
			case 0x0d: /* FATANH */
			fatanh_rr (dreg, sreg);
			break;
			case 0x0e: /* FSIN */
			fsin_rr (dreg, sreg);
			break;
			case 0x0f: /* FTAN */
			ftan_rr (dreg, sreg);
			break;
			case 0x10: /* FETOX */
			fetox_rr (dreg, sreg);
			break;
			case 0x11: /* FTWOTOX */
			ftwotox_rr (dreg, sreg);
			break;
			case 0x12: /* FTENTOX */
			ftentox_rr (dreg, sreg);
			break;
			case 0x14: /* FLOGN */
			flogN_rr (dreg, sreg);
			break;
			case 0x15: /* FLOG10 */
			flog10_rr (dreg, sreg);
			break;
			case 0x16: /* FLOG2 */
			flog2_rr (dreg, sreg);
			break;
			case 0x18: /* FABS */
			fabs_rr (dreg, sreg);
			break;
			case 0x19: /* FCOSH */
			fcosh_rr (dreg, sreg);
			break;
			case 0x1a: /* FNEG */
			fneg_rr (dreg, sreg);
			break;
			case 0x1c: /* FACOS */
#if USE_X86_FPUCW
			if ((regs.fpcr & 0x30) != 0x10) { /* use round to zero */
				mov_l_ri (S1, (regs.fpcr & 0xC0) | 0x10);
				fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				facos_rr (dreg, sreg);
				mov_l_rm (S1, uae_p32(&regs.fpcr));
				and_l_ri (S1, 0xf0); /* restore control word */
				fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				break;
			}
#endif
			facos_rr (dreg, sreg);
			break;
			case 0x1d: /* FCOS */
			fcos_rr (dreg, sreg);
			break;
			case 0x1e: /* FGETEXP */
			fgetexp_rr (dreg, sreg);
			break;
			case 0x1f: /* FGETMAN */
			fgetman_rr (dreg, sreg);
			break;
			case 0x20: /* FDIV */
			fdiv_rr (dreg, sreg);
			break;
			case 0x21: /* FMOD */
			frem_rr (dreg, sreg);
			break;
			case 0x22: /* FADD */
			fadd_rr (dreg, sreg);
			break;
			case 0x23: /* FMUL */
			fmul_rr (dreg, sreg);
			break;
			case 0x24: /* FSGLDIV  is not exactly the same as FSDIV, */
			/* because both operands should be SINGLE precision, too */
			case 0x60: /* FSDIV */
			fdiv_rr (dreg, sreg);
			if (!currprefs.fpu_strict) /* faster, but less strict rounding */
				break;
#if USE_X86_FPUCW
			if ((regs.fpcr & 0xC0) == 0x40) /* if SINGLE precision */
				break;
#endif
			fcuts_r (dreg);
			break;
			case 0x25: /* FREM */
			frem1_rr (dreg, sreg);
			break;
			case 0x26: /* FSCALE */
			fscale_rr (dreg, sreg);
			break;
			case 0x27: /* FSGLMUL is not exactly the same as FSMUL, */
			/* because both operands should be SINGLE precision, too */
			case 0x63: /* FSMUL */
			fmul_rr (dreg, sreg);
			if (!currprefs.fpu_strict) /* faster, but less strict rounding */
				break;
#if USE_X86_FPUCW
			if ((regs.fpcr & 0xC0) == 0x40) /* if SINGLE precision */
				break;
#endif
			fcuts_r (dreg);
			break;
			case 0x28: /* FSUB */
			fsub_rr (dreg, sreg);
			break;
			case 0x30: /* FSINCOS */
			case 0x31:
			case 0x32:
			case 0x33:
			case 0x34:
			case 0x35:
			case 0x36:
			case 0x37:
			if (dreg == (extra & 7))
				fsin_rr (dreg, sreg);
			else
				fsincos_rr (dreg, extra & 7, sreg);
			break;
			case 0x38: /* FCMP */
			fmov_rr (FP_RESULT, dreg);
			fsub_rr (FP_RESULT, sreg);
			return;
			case 0x3a: /* FTST */
			fmov_rr (FP_RESULT, sreg);
			return;
			case 0x40: /* FSMOVE */
			if (prec == 1 || !currprefs.fpu_strict) {
				if (sreg != dreg) /* no <EA> */
					fmov_rr (dreg, sreg);
			}
			else {
				fmovs_mr (uae_p32(temp_fp), sreg);
				fmovs_rm (dreg, uae_p32(temp_fp));
			}
			break;
			case 0x44: /* FDMOVE */
			if (prec || !currprefs.fpu_strict) {
				if (sreg != dreg) /* no <EA> */
					fmov_rr (dreg, sreg);
			}
			else {
				fmov_mr (uae_p32(temp_fp), sreg);
				fmov_rm (dreg, uae_p32(temp_fp));
			}
			break;
			case 0x41: /* FSSQRT */
			fsqrt_rr (dreg, sreg);
			if (!currprefs.fpu_strict) /* faster, but less strict rounding */
				break;
#if USE_X86_FPUCW
			if ((regs.fpcr & 0xC0) == 0x40) /* if SINGLE precision */
				break;
#endif
			fcuts_r (dreg);
			break;
			case 0x45: /* FDSQRT */
			if (!currprefs.fpu_strict) { /* faster, but less strict rounding */
				fsqrt_rr (dreg, sreg);
				break;
			}
#if USE_X86_FPUCW
			if (regs.fpcr & 0xC0) { /* if we don't have EXTENDED precision */
				if ((regs.fpcr & 0xC0) == 0x80) /* if we have DOUBLE */
					fsqrt_rr (dreg, sreg);
				else { /* if we have SINGLE presision, force DOUBLE */
					mov_l_ri (S1, (regs.fpcr & 0x30) | 0x80);
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
					fsqrt_rr (dreg, sreg);
					mov_l_rm (S1, uae_p32(&regs.fpcr));
					and_l_ri (S1, 0xf0); /* restore control word */
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				}
				break;
			}
#endif		/* in case of EXTENDED precision, just reduce the result to DOUBLE */
			fsqrt_rr (dreg, sreg);
			fcut_r (dreg);
			break;
			case 0x58: /* FSABS */
			fabs_rr (dreg, sreg);
			if (prec != 1 && currprefs.fpu_strict)
				fcuts_r (dreg);
			break;
			case 0x5a: /* FSNEG */
			fneg_rr (dreg, sreg);
			if (prec != 1 && currprefs.fpu_strict)
				fcuts_r (dreg);
			break;
			case 0x5c: /* FDABS */
			fabs_rr (dreg, sreg);
			if (!prec && currprefs.fpu_strict)
				fcut_r (dreg);
			break;
			case 0x5e: /* FDNEG */
			fneg_rr (dreg, sreg);
			if (!prec && currprefs.fpu_strict)
				fcut_r (dreg);
			break;
			case 0x62: /* FSADD */
			fadd_rr (dreg, sreg);
			if (!currprefs.fpu_strict) /* faster, but less strict rounding */
				break;
#if USE_X86_FPUCW
			if ((regs.fpcr & 0xC0) == 0x40) /* if SINGLE precision */
				break;
#endif
			fcuts_r (dreg);
			break;
			case 0x64: /* FDDIV */
			if (!currprefs.fpu_strict) { /* faster, but less strict rounding */
				fdiv_rr (dreg, sreg);
				break;
			}
#if USE_X86_FPUCW
			if (regs.fpcr & 0xC0) { /* if we don't have EXTENDED precision */
				if ((regs.fpcr & 0xC0) == 0x80) /* if we have DOUBLE */
					fdiv_rr (dreg, sreg);
				else { /* if we have SINGLE presision, force DOUBLE */
					mov_l_ri (S1, (regs.fpcr & 0x30) | 0x80);
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
					fdiv_rr (dreg, sreg);
					mov_l_rm (S1, uae_p32(&regs.fpcr));
					and_l_ri (S1, 0xf0); /* restore control word */
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				}
				break;
			}
#endif		/* in case of EXTENDED precision, just reduce the result to DOUBLE */
			fdiv_rr (dreg, sreg);
			fcut_r (dreg);
			break;
			case 0x66: /* FDADD */
			if (!currprefs.fpu_strict) { /* faster, but less strict rounding */
				fadd_rr (dreg, sreg);
				break;
			}
#if USE_X86_FPUCW
			if (regs.fpcr & 0xC0) { /* if we don't have EXTENDED precision */
				if ((regs.fpcr & 0xC0) == 0x80) /* if we have DOUBLE */
					fadd_rr (dreg, sreg);
				else { /* if we have SINGLE presision, force DOUBLE */
					mov_l_ri (S1, (regs.fpcr & 0x30) | 0x80);
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
					fadd_rr (dreg, sreg);
					mov_l_rm (S1, uae_p32(&regs.fpcr));
					and_l_ri (S1, 0xf0); /* restore control word */
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				}
				break;
			}
#endif		/* in case of EXTENDED precision, just reduce the result to DOUBLE */
			fadd_rr (dreg, sreg);
			fcut_r (dreg);
			break;
			case 0x67: /* FDMUL */
			if (!currprefs.fpu_strict) { /* faster, but less strict rounding */
				fmul_rr (dreg, sreg);
				break;
			}
#if USE_X86_FPUCW
			if (regs.fpcr & 0xC0) { /* if we don't have EXTENDED precision */
				if ((regs.fpcr & 0xC0) == 0x80) /* if we have DOUBLE */
					fmul_rr (dreg, sreg);
				else { /* if we have SINGLE presision, force DOUBLE */
					mov_l_ri (S1, (regs.fpcr & 0x30) | 0x80);
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
					fmul_rr (dreg, sreg);
					mov_l_rm (S1, uae_p32(&regs.fpcr));
					and_l_ri (S1, 0xf0); /* restore control word */
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				}
				break;
			}
#endif		/* in case of EXTENDED precision, just reduce the result to DOUBLE */
			fmul_rr (dreg, sreg);
			fcut_r (dreg);
			break;
			case 0x68: /* FSSUB */
			fsub_rr (dreg, sreg);
			if (!currprefs.fpu_strict) /* faster, but less strict rounding */
				break;
#if USE_X86_FPUCW
			if ((regs.fpcr & 0xC0) == 0x40) /* if SINGLE precision */
				break;
#endif
			fcuts_r (dreg);
			break;
			case 0x6c: /* FDSUB */
			if (!currprefs.fpu_strict) { /* faster, but less strict rounding */
				fsub_rr (dreg, sreg);
				break;
			}
#if USE_X86_FPUCW
			if (regs.fpcr & 0xC0) { /* if we don't have EXTENDED precision */
				if ((regs.fpcr & 0xC0) == 0x80) /* if we have DOUBLE */
					fsub_rr (dreg, sreg);
				else { /* if we have SINGLE presision, force DOUBLE */
					mov_l_ri (S1, (regs.fpcr & 0x30) | 0x80);
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
					fsub_rr (dreg, sreg);
					mov_l_rm (S1, uae_p32(&regs.fpcr));
					and_l_ri (S1, 0xf0); /* restore control word */
					fldcw_m_indexed (S1, uae_p32(x86_fpucw));
				}
				break;
			}
#endif		/* in case of EXTENDED precision, just reduce the result to DOUBLE */
			fsub_rr (dreg, sreg);
			fcut_r (dreg);
			break;
			default:
			FAIL (1);
			return;
		}
		fmov_rr (FP_RESULT, dreg);
		return;
		default:
		write_log (_T ("Unsupported JIT-FPU instruction: 0x%04x %04x\n"), opcode, extra);
		FAIL (1);
		return;
	}
}
#endif
