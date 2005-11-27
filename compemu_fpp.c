/*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68881 emulation
  *
  * Copyright 1996 Herman ten Brugge
  * Adapted for JIT compilation (c) Bernd Meyer, 2000
  * Modified 2005 Peter Keunecke
 */

#include <math.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "ersatz.h"
#include "md-fpp.h"
#include "compemu.h"

#if defined(JIT)

#define MAKE_FPSR(r) do { fmov_rr(FP_RESULT,r); } while (0)

#define delay   //nop() ;nop() /* Who tried this with the JIT ? */
#define delay2  //nop() ;nop() /* the delays will have to emit( ;-) */

uae_s32 temp_fp[3];  /* To convert between FP/integer */

/* 128 words, indexed through the low byte of the 68k fpu control word */
static uae_u16 x86_fpucw[]={
    0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, /* p0r0 */
    0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, /* p0r1 */
    0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, /* p0r2 */
    0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, /* p0r3 */

    0x107f, 0x107f, 0x107f, 0x107f, 0x107f, 0x107f, 0x107f, 0x107f, /* p1r0 */
    0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, 0x1c7f, /* p1r1 */
    0x147f, 0x147f, 0x147f, 0x147f, 0x147f, 0x147f, 0x147f, 0x147f, /* p1r2 */
    0x187f, 0x187f, 0x187f, 0x187f, 0x187f, 0x187f, 0x187f, 0x187f, /* p1r3 */

    0x127f, 0x127f, 0x127f, 0x127f, 0x127f, 0x127f, 0x127f, 0x127f, /* p2r0 */
    0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, 0x1e7f, /* p2r1 */
    0x167f, 0x167f, 0x167f, 0x167f, 0x167f, 0x167f, 0x167f, 0x167f, /* p2r2 */
    0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, 0x1a7f, /* p2r3 */

    0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, 0x137f, /* p3r0 */
    0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, 0x1f7f, /* p3r1 */
    0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, 0x177f, /* p3r2 */
    0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f, 0x1b7f  /* p3r3 */
};

/* return register number, or -1 for failure */
STATIC_INLINE int get_fp_value (uae_u32 opcode, uae_u16 extra)
{
    uaecptr tmppc;
    uae_u16 tmp;
    int size;
    int mode;
    int reg;
    double* src;
    uae_u32 ad = 0;
    static int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
    static int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

    if ((extra & 0x4000) == 0) {
	return (extra >> 10) & 7;
    }

    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    size = (extra >> 10) & 7;
    switch (mode) {
     case 0:
	switch (size) {
	 case 6:
	    sign_extend_8_rr(S1,reg);
	    mov_l_mr((uae_u32)temp_fp,S1);
	    delay2;
	    fmovi_rm(FS1,(uae_u32)temp_fp);
	    return FS1;
	 case 4:
	    sign_extend_16_rr(S1,reg);
	    mov_l_mr((uae_u32)temp_fp,S1);
	    delay2;
	    fmovi_rm(FS1,(uae_u32)temp_fp);
	    return FS1;
	 case 0:
	    mov_l_mr((uae_u32)temp_fp,reg);
	    delay2;
	    fmovi_rm(FS1,(uae_u32)temp_fp);
	    return FS1;
	 case 1:
	    mov_l_mr((uae_u32)temp_fp,reg);
	    delay2;
	    fmovs_rm(FS1,(uae_u32)temp_fp);
	    return FS1;
	 default:
	    return -1;
	}
	return -1; /* Should be unreachable */
     case 1:
	return -1; /* Genuine invalid instruction */
     default:
	break;
    }
    /* OK, we *will* have to load something from an address. Let's make
       sure we know how to handle that, or quit early --- i.e. *before*
       we do any postincrement/predecrement that we may regret */

    switch (size) {
     case 3:
	return -1;
     case 0:
     case 1:
     case 2:
     case 4:
     case 5:
     case 6:
	break;
     default:
	return -1;
    }

    switch (mode) {
     case 2:
	ad=S1;  /* We will change it, anyway ;-) */
	mov_l_rr(ad,reg+8);
	break;
     case 3:
	ad=S1;
	mov_l_rr(ad,reg+8);
	lea_l_brr(reg+8,reg+8,(reg == 7?sz2[size]:sz1[size]));
	break;
     case 4:
	ad=S1;

	lea_l_brr(reg+8,reg+8,-(reg == 7?sz2[size]:sz1[size]));
	mov_l_rr(ad,reg+8);
	break;
     case 5:
     {
	 uae_u32 off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	 ad=S1;
	 mov_l_rr(ad,reg+8);
	 lea_l_brr(ad,ad,off);
	 break;
     }
     case 6:
     {
	uae_u32 dp=comp_get_iword((m68k_pc_offset+=2)-2);
	ad=S1;
	calc_disp_ea_020(reg+8,dp,ad,S2);
	break;
     }
     case 7:
	switch (reg) {
	 case 0:
	 {
	     uae_u32 off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	     ad=S1;
	     mov_l_ri(ad,off);
	     break;
	 }
	 case 1:
	 {
	     uae_u32 off=comp_get_ilong((m68k_pc_offset+=4)-4);
	     ad=S1;
	     mov_l_ri(ad,off);
	     break;
	 }
	 case 2:
	 {
	     uae_u32 address=start_pc+((char *)comp_pc_p-(char *)start_pc_p)+
		 m68k_pc_offset;
	     uae_s32 PC16off =(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	     ad=S1;
	     mov_l_ri(ad,address+PC16off);
	     break;
	 }
	 case 3:
	    return -1;
	    tmppc = m68k_getpc ();
	    tmp = next_iword ();
	    ad = get_disp_ea_020 (tmppc, tmp);
	    break;
	 case 4:
	 {
	     uae_u32 address=start_pc+((char *)comp_pc_p-(char *)start_pc_p)+
		 m68k_pc_offset;
	     ad=S1;
	     if (size == 6)
		 address++;
	     mov_l_ri(ad,address);
	     m68k_pc_offset+=sz2[size];
	     break;
	 }
	 default:
	    return -1;
	}
    }

    switch (size) {
     case 0:
	readlong(ad,S2,S3);
	mov_l_mr((uae_u32)temp_fp,S2);
	delay2;
	fmovi_rm(FS1,(uae_u32)temp_fp);
	break;
     case 1:
	readlong(ad,S2,S3);
	mov_l_mr((uae_u32)temp_fp,S2);
	delay2;
	fmovs_rm(FS1,(uae_u32)temp_fp);
	break;
     case 2:
	readword(ad,S2,S3);
	mov_w_mr(((uae_u32)temp_fp)+8,S2);
	add_l_ri(ad,4);
	readlong(ad,S2,S3);
	mov_l_mr((uae_u32)(temp_fp)+4,S2);
	add_l_ri(ad,4);
	readlong(ad,S2,S3);
	mov_l_mr((uae_u32)(temp_fp),S2);
	delay2;
	fmov_ext_rm(FS1,(uae_u32)(temp_fp));
	break;
     case 3:
	return -1; /* Some silly "packed" stuff */
     case 4:
	readword(ad,S2,S3);
	sign_extend_16_rr(S2,S2);
	mov_l_mr((uae_u32)temp_fp,S2);
	delay2;
	fmovi_rm(FS1,(uae_u32)temp_fp);
	break;
     case 5:
	readlong(ad,S2,S3);
	mov_l_mr(((uae_u32)temp_fp)+4,S2);
	add_l_ri(ad,4);
	readlong(ad,S2,S3);
	mov_l_mr((uae_u32)(temp_fp),S2);
	delay2;
	fmov_rm(FS1,(uae_u32)(temp_fp));
	break;
     case 6:
	readbyte(ad,S2,S3);
	sign_extend_8_rr(S2,S2);
	mov_l_mr((uae_u32)temp_fp,S2);
	delay2;
	fmovi_rm(FS1,(uae_u32)temp_fp);
	break;
     default:
	return -1;
    }
    return FS1;
}

/* return of -1 means failure, >=0 means OK */
STATIC_INLINE int put_fp_value (int val, uae_u32 opcode, uae_u16 extra)
{
    uae_u16 tmp;
    uaecptr tmppc;
    int size;
    int mode;
    int reg;
    uae_u32 ad;
    static int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
    static int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

    if ((extra & 0x4000) == 0) {
	fmov_rr((extra>>10)&7,val);
	return 0;
    }

    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    size = (extra >> 10) & 7;
    ad = -1;
    switch (mode) {
     case 0:
	switch (size) {
	 case 6: /* FMOVE.B FPx, reg */
#if USE_X86_FPUCW
	    if (!(regs.fpcr & 0xf0)) { /* if extended round to nearest */
		mov_l_ri(S1,0x10); /* use extended round to zero mode */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		fmovi_mr((uae_u32)temp_fp,val);
		mov_b_rm(reg,(uae_u32)temp_fp);
		mov_l_rm(S1,(uae_u32)&regs.fpcr);
		and_l_ri(S1,0xf0); /* restore control word */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		return 0;
	    }
#endif
	    fmovi_mr((uae_u32)temp_fp,val);
	    mov_b_rm(reg,(uae_u32)temp_fp);
	    return 0;
	 case 4: /* FMOVE.W FPx, reg */
#if USE_X86_FPUCW
	    if (!(regs.fpcr & 0xf0)) { /* if extended round to nearest */
		mov_l_ri(S1,0x10); /* use extended round to zero mode */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		fmovi_mr((uae_u32)temp_fp,val);
		mov_w_rm(reg,(uae_u32)temp_fp);
		mov_l_rm(S1,(uae_u32)&regs.fpcr);
		and_l_ri(S1,0xf0); /* restore control word */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		return 0;
	    }
#endif
	    fmovi_mr((uae_u32)temp_fp,val);
	    mov_w_rm(reg,(uae_u32)temp_fp);
	    return 0;
	 case 0: /* FMOVE.L FPx, reg */
#if USE_X86_FPUCW
	    if (!(regs.fpcr & 0xf0)) { /* if extended round to nearest */
		mov_l_ri(S1,0x10); /* use extended round to zero mode */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		fmovi_mr((uae_u32)temp_fp,val);
		mov_l_rm(reg,(uae_u32)temp_fp);
		mov_l_rm(S1,(uae_u32)&regs.fpcr);
		and_l_ri(S1,0xf0); /* restore control word */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		return 0;
	    }
#endif
	    fmovi_mr((uae_u32)temp_fp,val);
	    mov_l_rm(reg,(uae_u32)temp_fp);
	    return 0;
	 case 1:
	    fmovs_mr((uae_u32)temp_fp,val);
	    mov_l_rm(reg,(uae_u32)temp_fp);
	    return 0;
	 default:
	    return -1;
	}
     case 1:
	return -1; /* genuine invalid instruction */
     default: break;
    }

    /* Let's make sure we get out *before* doing something silly if
       we can't handle the size */
    switch (size) {
     case 0:
     case 4:
     case 5:
     case 6:
     case 2:
     case 1:
	break;
     case 3:
     default:
	return -1;
    }

    switch (mode) {
     case 2:
	ad=S1;
	mov_l_rr(ad,reg+8);
	break;
     case 3:
	ad=S1;
	mov_l_rr(ad,reg+8);
	lea_l_brr(reg+8,reg+8,(reg == 7?sz2[size]:sz1[size]));
	break;
     case 4:
	ad=S1;
	lea_l_brr(reg+8,reg+8,-(reg == 7?sz2[size]:sz1[size]));
	mov_l_rr(ad,reg+8);
	break;
     case 5:
     {
	 uae_u32 off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	 ad=S1;
	 mov_l_rr(ad,reg+8);
	 add_l_ri(ad,off);
	 break;
     }
     case 6:
     {
	uae_u32 dp=comp_get_iword((m68k_pc_offset+=2)-2);
	ad=S1;
	calc_disp_ea_020(reg+8,dp,ad,S2);
	break;
     }
     case 7:
	switch (reg) {
	 case 0:
	 {
	     uae_u32 off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	     ad=S1;
	     mov_l_ri(ad,off);
	     break;
	 }
	 case 1:
	 {
	     uae_u32 off=comp_get_ilong((m68k_pc_offset+=4)-4);
	     ad=S1;
	     mov_l_ri(ad,off);
	     break;
	 }
	 case 2:
	 {
	     uae_u32 address=start_pc+((char *)comp_pc_p-(char *)start_pc_p)+
		 m68k_pc_offset;
	     uae_s32 PC16off =(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	     ad=S1;
	     mov_l_ri(ad,address+PC16off);
	     break;
	 }
	 case 3:
	    return -1;
	    tmppc = m68k_getpc ();
	    tmp = next_iword ();
	    ad = get_disp_ea_020 (tmppc, tmp);
	    break;
	 case 4:
	 {
	     uae_u32 address=start_pc+((char *)comp_pc_p-(char *)start_pc_p)+
		 m68k_pc_offset;
	     ad=S1;
	     mov_l_ri(ad,address);
	     m68k_pc_offset+=sz2[size];
	     break;
	 }
	 default:
	    return -1;
	}
    }
    switch (size) {
     case 0:
	fmovi_mr((uae_u32)temp_fp,val);
	delay;
	mov_l_rm(S2,(uae_u32)temp_fp);
	writelong_clobber(ad,S2,S3);
	break;
     case 1:
	fmovs_mr((uae_u32)temp_fp,val);
	delay;
	mov_l_rm(S2,(uae_u32)temp_fp);
	writelong_clobber(ad,S2,S3);
	break;
     case 2:
	fmov_ext_mr((uae_u32)temp_fp,val);
	delay;
	mov_w_rm(S2,(uae_u32)temp_fp+8);
	writeword_clobber(ad,S2,S3);
	add_l_ri(ad,4);
	mov_l_rm(S2,(uae_u32)temp_fp+4);
	writelong_clobber(ad,S2,S3);
	add_l_ri(ad,4);
	mov_l_rm(S2,(uae_u32)temp_fp);
	writelong_clobber(ad,S2,S3);
	break;
     case 3: return -1; /* Packed */

     case 4:
	fmovi_mr((uae_u32)temp_fp,val);
	delay;
	mov_l_rm(S2,(uae_u32)temp_fp);
	writeword_clobber(ad,S2,S3);
	break;
     case 5:
	fmov_mr((uae_u32)temp_fp,val);
	delay;
	mov_l_rm(S2,(uae_u32)temp_fp+4);
	writelong_clobber(ad,S2,S3);
	add_l_ri(ad,4);
	mov_l_rm(S2,(uae_u32)temp_fp);
	writelong_clobber(ad,S2,S3);
	break;
     case 6:
	fmovi_mr((uae_u32)temp_fp,val);
	delay;
	mov_l_rm(S2,(uae_u32)temp_fp);
	writebyte(ad,S2,S3);
	break;
     default:
	return -1;
    }
    return 0;
}

/* return -1 for failure, or register number for success */
STATIC_INLINE int get_fp_ad (uae_u32 opcode, uae_u32 * ad)
{
    uae_u16 tmp;
    uaecptr tmppc;
    int mode;
    int reg;
    uae_s32 off;

    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    switch (mode) {
     case 0:
     case 1:
	return -1;
     case 2:
     case 3:
     case 4:
	mov_l_rr(S1,8+reg);
	return S1;
	*ad = m68k_areg (regs, reg);
	break;
     case 5:
	off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);

	mov_l_rr(S1,8+reg);
	add_l_ri(S1,off);
	return S1;
     case 6:
	return -1;
	break;
     case 7:
	switch (reg) {
	 case 0:
	    off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
	    mov_l_ri(S1,off);
	    return S1;
	 case 1:
	    off=comp_get_ilong((m68k_pc_offset+=4)-4);
	    mov_l_ri(S1,off);
	    return S1;
	 case 2:
	    return -1;
	    *ad = m68k_getpc ();
	    *ad += (uae_s32) (uae_s16) next_iword ();
	    break;
	 case 3:
	    return -1;
	    tmppc = m68k_getpc ();
	    tmp = next_iword ();
	    *ad = get_disp_ea_020 (tmppc, tmp);
	    break;
	 default:
	    return -1;
	}
    }
    abort();
}

void comp_fdbcc_opp (uae_u32 opcode, uae_u16 extra)
{
    FAIL(1);
    return;

    if (!currprefs.compfpu) {
	FAIL(1);
	return;
    }
}

void comp_fscc_opp (uae_u32 opcode, uae_u16 extra)
{
    uae_u32 ad;
    int cc;
    int reg;

    if (!currprefs.compfpu) {
	FAIL(1);
	return;
    }

#if DEBUG_FPP
    printf ("fscc_opp at %08lx\n", m68k_getpc ());
    fflush (stdout);
#endif


    if (extra&0x20) {  /* only cc from 00 to 1f are defined */
	FAIL(1);
	return;
    }
    if ((opcode & 0x38) != 0) { /* We can only do to integer register */
	FAIL(1);
	return;
    }

    fflags_into_flags(S2);
    reg=(opcode&7);

    mov_l_ri(S1,255);
    mov_l_ri(S4,0);
    switch(extra&0x0f) { /* according to fpp.c, the 0x10 bit is ignored */
     case 0: break;  /* set never */
     case 1: mov_l_rr(S2,S4);
	cmov_l_rr(S4,S1,4);
	cmov_l_rr(S4,S2,10); break;
     case 2: cmov_l_rr(S4,S1,7); break;
     case 3: cmov_l_rr(S4,S1,3); break;
     case 4: mov_l_rr(S2,S4);
	cmov_l_rr(S4,S1,2);
	cmov_l_rr(S4,S2,10); break;
     case 5: mov_l_rr(S2,S4);
	cmov_l_rr(S4,S1,6);
	cmov_l_rr(S4,S2,10); break;
     case 6: cmov_l_rr(S4,S1,5); break;
     case 7: cmov_l_rr(S4,S1,11); break;
     case 8: cmov_l_rr(S4,S1,10); break;
     case 9: cmov_l_rr(S4,S1,4); break;
     case 10: cmov_l_rr(S4,S1,10); cmov_l_rr(S4,S1,7); break;
     case 11: cmov_l_rr(S4,S1,4); cmov_l_rr(S4,S1,3); break;
     case 12: cmov_l_rr(S4,S1,2); break;
     case 13: cmov_l_rr(S4,S1,6); break;
     case 14: cmov_l_rr(S4,S1,5); cmov_l_rr(S4,S1,10); break;
     case 15: mov_l_rr(S4,S1); break;
    }

    if ((opcode & 0x38) == 0) {
	mov_b_rr(reg,S4);
    } else {
	abort();
	if (get_fp_ad (opcode, &ad) == 0) {
	    m68k_setpc (m68k_getpc () - 4);
	    op_illg (opcode);
	} else
	    put_byte (ad, cc ? 0xff : 0x00);
    }
}

void comp_ftrapcc_opp (uae_u32 opcode, uaecptr oldpc)
{
    int cc;

    FAIL(1);
    return;
}

extern unsigned long foink3, oink;

void comp_fbcc_opp (uae_u32 opcode)
{
    uae_u32 start_68k_offset=m68k_pc_offset;
    uae_u32 off;
    uae_u32 v1;
    uae_u32 v2;
    uae_u32 nh;
    int cc;

    if (!currprefs.compfpu) {
	FAIL(1);
	return;
    }

    if (opcode&0x20) {  /* only cc from 00 to 1f are defined */
	FAIL(1);
	return;
    }
    if ((opcode&0x40)==0) {
	off=(uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
    }
    else {
	off=comp_get_ilong((m68k_pc_offset+=4)-4);
    }
    mov_l_ri(S1,(uae_u32)
	     (comp_pc_p+off-(m68k_pc_offset-start_68k_offset)));
    mov_l_ri(PC_P,(uae_u32)comp_pc_p);

    /* Now they are both constant. Might as well fold in m68k_pc_offset */
    add_l_ri(S1,m68k_pc_offset);
    add_l_ri(PC_P,m68k_pc_offset);
    m68k_pc_offset=0;

    /* according to fpp.c, the 0x10 bit is ignored
       (it handles exception handling, which we don't
       do, anyway ;-) */
    cc=opcode&0x0f;
    v1=get_const(PC_P);
    v2=get_const(S1);
    fflags_into_flags(S2);

    // mov_l_mi((uae_u32)&foink3,cc);
    switch(cc) {
     case 0: break;  /* jump never */
     case 1:
	mov_l_rr(S2,PC_P);
	cmov_l_rr(PC_P,S1,4);
	cmov_l_rr(PC_P,S2,10); break;
     case 2: register_branch(v1,v2,7); break;
     case 3: register_branch(v1,v2,3); break;
     case 4:
	mov_l_rr(S2,PC_P);
	cmov_l_rr(PC_P,S1,2);
	cmov_l_rr(PC_P,S2,10); break;
     case 5:
	mov_l_rr(S2,PC_P);
	cmov_l_rr(PC_P,S1,6);
	cmov_l_rr(PC_P,S2,10); break;
     case 6: register_branch(v1,v2,5); break;
     case 7: register_branch(v1,v2,11); break;
     case 8: register_branch(v1,v2,10); break;
     case 9: register_branch(v1,v2,4); break;
     case 10:
	cmov_l_rr(PC_P,S1,10);
	cmov_l_rr(PC_P,S1,7); break;
     case 11:
	cmov_l_rr(PC_P,S1,4);
	cmov_l_rr(PC_P,S1,3); break;
     case 12: register_branch(v1,v2,2); break;
     case 13: register_branch(v1,v2,6); break;
     case 14:
	cmov_l_rr(PC_P,S1,5);
	cmov_l_rr(PC_P,S1,10); break;
     case 15: mov_l_rr(PC_P,S1); break;
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
    uae_u32 ad;
    int incr = (opcode & 0x38) == 0x20 ? -1 : 1;
    int i;

    FAIL(1);
    return;

    if (!currprefs.compfpu) {
	FAIL(1);
	return;
    }

#if DEBUG_FPP
    printf ("fsave_opp at %08lx\n", m68k_getpc ());
    fflush (stdout);
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
	    put_long (ad, 0x1f180000);
	} else {
	    put_long (ad, 0x1f180000);
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

void comp_frestore_opp (uae_u32 opcode)
{
    uae_u32 ad;
    uae_u32 d;
    int incr = (opcode & 0x38) == 0x20 ? -1 : 1;

    FAIL(1);
    return;

    if (!currprefs.compfpu) {
	FAIL(1);
	return;
    }

#if DEBUG_FPP
    printf ("frestore_opp at %08lx\n", m68k_getpc ());
    fflush (stdout);
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

extern uae_u32 *xhex_pi, *xhex_exp_1, *xhex_l2_e, *xhex_ln_2, *xhex_ln_10;
extern uae_u32 *xhex_l10_2, *xhex_l10_e, *xhex_1e16, *xhex_1e32, *xhex_1e64;
extern uae_u32 *xhex_1e128, *xhex_1e256, *xhex_1e512, *xhex_1e1024;
extern uae_u32 *xhex_1e2048, *xhex_1e4096;
extern double fp_1e8;
extern float  fp_1e1, fp_1e2, fp_1e4;

void comp_fpp_opp (uae_u32 opcode, uae_u16 extra)
{
    int dreg;
    int sreg;

    if (!currprefs.compfpu) {
	FAIL(1);
	return;
    }
    switch ((extra >> 13) & 0x7) {
     case 3: /* 2nd most common */
	if (put_fp_value ((extra >> 7)&7 , opcode, extra) < 0) {
	    FAIL(1);
	    return;
	}
	return;
     case 6:
     case 7:
	{
	    uae_u32 ad, list = 0;
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
		    FAIL(1); return;
		}
		ad=get_fp_ad (opcode, &ad);
		if (ad<0) {
		    FAIL(1);
#if 0
		    m68k_setpc (m68k_getpc () - 4);
		    op_illg (opcode);
#endif
		    return;
		}
		switch ((extra >> 11) & 3) {
		case 0: /* static pred */
		    list = extra & 0xff;
		    incr = -1;
		    break;
		case 2: /* static postinc */
		    list = extra & 0xff;
		    incr = 1;
		    break;
		case 1: /* dynamic pred */
		case 3: /* dynamic postinc */
		   abort();
		}
		while (list) {
		    uae_u32 wrd1, wrd2, wrd3;
		    if (incr < 0) { /* Predecrement */
			fmov_ext_mr((uae_u32)temp_fp,fpp_movem_index2[list]);
			delay;
			sub_l_ri(ad,4);
			mov_l_rm(S2,(uae_u32)temp_fp);
			writelong_clobber(ad,S2,S3);
			sub_l_ri(ad,4);
			mov_l_rm(S2,(uae_u32)temp_fp+4);
			writelong_clobber(ad,S2,S3);
			sub_l_ri(ad,4);
			mov_w_rm(S2,(uae_u32)temp_fp+8);
			writeword_clobber(ad,S2,S3);
		    } else { /* postinc */
			fmov_ext_mr((uae_u32)temp_fp,fpp_movem_index2[list]);
			delay;
			mov_w_rm(S2,(uae_u32)temp_fp+8);
			writeword_clobber(ad,S2,S3);
			add_l_ri(ad,4);
			mov_l_rm(S2,(uae_u32)temp_fp+4);
			writelong_clobber(ad,S2,S3);
			add_l_ri(ad,4);
			mov_l_rm(S2,(uae_u32)temp_fp);
			writelong_clobber(ad,S2,S3);
			add_l_ri(ad,4);
		    }
		    list = fpp_movem_next[list];
		}
		if ((opcode & 0x38) == 0x18)
		    mov_l_rr((opcode & 7)+8,ad);
		if ((opcode & 0x38) == 0x20)
		    mov_l_rr((opcode & 7)+8,ad);
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
		    FAIL(1); return;
		}
		ad=get_fp_ad (opcode, &ad);
		if (ad<0) {
		    FAIL(1);
#if 0
		    m68k_setpc (m68k_getpc () - 4);
		    op_illg (opcode);
#endif
		    return;
		}
		switch ((extra >> 11) & 3) {
		case 0: /* static pred */
		    list = extra & 0xff;
		    incr = -1;
		    break;
		case 2: /* static postinc */
		    list = extra & 0xff;
		    incr = 1;
		    break;
		case 1: /* dynamic pred */
		case 3: /* dynamic postinc */
		   abort();
		}

		while (list) {
		    uae_u32 wrd1, wrd2, wrd3;
		    if (incr < 0) {
			sub_l_ri(ad,4);
			readlong(ad,S2,S3);
			mov_l_mr((uae_u32)(temp_fp),S2);
			sub_l_ri(ad,4);
			readlong(ad,S2,S3);
			mov_l_mr((uae_u32)(temp_fp)+4,S2);
			sub_l_ri(ad,4);
			readword(ad,S2,S3);
			mov_w_mr(((uae_u32)temp_fp)+8,S2);
			delay2;
			fmov_ext_rm(fpp_movem_index2[list],(uae_u32)(temp_fp));
		    } else {
			readword(ad,S2,S3);
			mov_w_mr(((uae_u32)temp_fp)+8,S2);
			add_l_ri(ad,4);
			readlong(ad,S2,S3);
			mov_l_mr((uae_u32)(temp_fp)+4,S2);
			add_l_ri(ad,4);
			readlong(ad,S2,S3);
			mov_l_mr((uae_u32)(temp_fp),S2);
			add_l_ri(ad,4);
			delay2;
			fmov_ext_rm(fpp_movem_index1[list],(uae_u32)(temp_fp));
		    }
		    list = fpp_movem_next[list];
		}
		if ((opcode & 0x38) == 0x18)
		    mov_l_rr((opcode & 7)+8,ad);
		if ((opcode & 0x38) == 0x20)
		    mov_l_rr((opcode & 7)+8,ad);
	    }
	}
	return;

     case 4:
     case 5:  /* rare */
	if ((opcode & 0x30) == 0) {
	    if (extra & 0x2000) {
		if (extra & 0x1000) {
		    mov_l_rm(opcode & 15,(uae_u32)&regs.fpcr); return;
		}
		if (extra & 0x0800) {
		    FAIL(1);
		    return;
		}
		if (extra & 0x0400) {
		    mov_l_rm(opcode & 15,(uae_u32)&regs.fpiar); return;
		}
	    } else {
		if (extra & 0x1000) {
		    mov_l_mr((uae_u32)&regs.fpcr,opcode & 15);
#if USE_X86_FPUCW
		    mov_l_rr(S1,opcode & 15);
		    and_l_ri(S1,0xf0);
		    fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
#endif
		    return;
		}
		if (extra & 0x0800) {
		    FAIL(1);
		    return;
		    // set_fpsr(m68k_dreg (regs, opcode & 15));
		}
		if (extra & 0x0400) {
		    mov_l_mr((uae_u32)&regs.fpiar,opcode & 15); return;
		}
	    }
	} else if ((opcode & 0x3f) == 0x3c) {
	    if ((extra & 0x2000) == 0) {
		if (extra & 0x1000) {
		    uae_u32 val=comp_get_ilong((m68k_pc_offset+=4)-4);
		    mov_l_mi((uae_u32)&regs.fpcr,val);
#if USE_X86_FPUCW
		    mov_l_ri(S1,val&0xf0);
		    fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
#endif
		    return;
		}
		if (extra & 0x0800) {
		    FAIL(1);
		    return;
		}
		if (extra & 0x0400) {
		    uae_u32 val=comp_get_ilong((m68k_pc_offset+=4)-4);
		    mov_l_mi((uae_u32)&regs.fpiar,val);
		    return;
		}
	    }
	    FAIL(1);
	    return;
	} else if (extra & 0x2000) {
	    FAIL(1);
	    return;
	} else {
	    FAIL(1);
	    return;
	}
	FAIL(1);
	return;

     case 0:
     case 2: /* Extremely common */
	dreg = (extra >> 7) & 7;
	if ((extra & 0xfc00) == 0x5c00) {
	   //write_log ("JIT FMOVECR %x\n", extra & 0x7f);
	   dont_care_fflags();
	   switch (extra & 0x7f) {
	     case 0x00:
		fmov_pi(dreg);
		break;
	     case 0x0b:
		fmov_log10_2(dreg);
		break;
	     case 0x0c:
		fmov_ext_rm(dreg,&xhex_exp_1);
		break;
	     case 0x0d:
		fmov_log2_e(dreg);
		break;
	     case 0x0e:
		fmov_ext_rm(dreg,&xhex_l10_e);
		break;
	     case 0x0f:
		fmov_0(dreg);
		break;
	     case 0x30:
		fmov_loge_2(dreg);
		break;
	     case 0x31:
		fmov_ext_rm(dreg,&xhex_ln_10);
		break;
	     case 0x32:
		fmov_1(dreg);
		break;
	     case 0x33:
		fmovs_rm(dreg,&fp_1e1);
		break;
	     case 0x34:
		fmovs_rm(dreg,&fp_1e2);
		break;
	     case 0x35:
		fmovs_rm(dreg,&fp_1e4);
		break;
	     case 0x36:
		fmov_rm(dreg,&fp_1e8);
		break;
	     case 0x37:
		fmov_ext_rm(dreg,&xhex_1e16);
		break;
	     case 0x38:
		fmov_ext_rm(dreg,&xhex_1e32);
		break;
	     case 0x39:
		fmov_ext_rm(dreg,&xhex_1e64);
		break;
	     case 0x3a:
		fmov_ext_rm(dreg,&xhex_1e128);
		break;
	     case 0x3b:
		fmov_ext_rm(dreg,&xhex_1e256);
		break;
	     case 0x3c:
		fmov_ext_rm(dreg,&xhex_1e512);
		break;
	     case 0x3d:
		fmov_ext_rm(dreg,&xhex_1e1024);
		break;
	     case 0x3e:
		fmov_ext_rm(dreg,&xhex_1e2048);
		break;
	     case 0x3f:
		fmov_ext_rm(dreg,&xhex_1e4096);
		break;
	     default:
		FAIL(1);
		return;
	    }
	    MAKE_FPSR (dreg);
	    return;
	}

	switch (extra & 0x7f) {
	 case 0x00: /* FMOVE */
	 case 0x40:
	 case 0x44:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fmov_rr(dreg,sreg);
	    break;
	 case 0x01: /* FINT */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    frndint_rr(dreg,sreg);
	    break;
	 case 0x02: /* FSINH */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fsinh_rr(dreg,sreg);
	    break;
	 case 0x03: /* FINTRZ */
#if USE_X86_FPUCW
	    /* If we have control over the CW, we can do this */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    if ((regs.fpcr & 0x30) == 0x10) /* maybe unsafe, because this test is done */
		frndint_rr(dreg,sreg); /* during the JIT compilation and not at runtime */
	    else {
		mov_l_ri(S1,(regs.fpcr & 0xC0) | 0x10); /* use round to zero */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		frndint_rr(dreg,sreg);
		mov_l_rm(S1,(uae_u32)&regs.fpcr);
		and_l_ri(S1,0xf0); /* restore control word */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
	    }
	    break;
#endif
	    FAIL(1);
	    return;
	 case 0x04: /* FSQRT */
	 case 0x41:
	 case 0x45:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fsqrt_rr(dreg,sreg);
	    break;
	 case 0x06: /* FLOGNP1 */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    flogNP1_rr(dreg,sreg);
	    break;
	 case 0x08: /* FETOXM1 */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fetoxM1_rr(dreg,sreg);
	    break;
	 case 0x09: /* FTANH */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    ftanh_rr(dreg,sreg);
	    break;
	 case 0x0a: /* FATAN */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fatan_rr(dreg,sreg);
	    break;
	 case 0x0c: /* FASIN */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fasin_rr(dreg,sreg);
	    break;
	 case 0x0d: /* FATANH */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fatanh_rr(dreg,sreg);
	    break;
	 case 0x0e: /* FSIN */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fsin_rr(dreg,sreg);
	    break;
	 case 0x0f: /* FTAN */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    ftan_rr(dreg,sreg);
	    break;
	 case 0x10: /* FETOX */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fetox_rr(dreg,sreg);
	    break;
	 case 0x11: /* FTWOTOX */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    ftwotox_rr(dreg,sreg);
	    break;
	 case 0x12: /* FTENTOX */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    ftentox_rr(dreg,sreg);
	    break;
	 case 0x14: /* FLOGN */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    flogN_rr(dreg,sreg);
	    break;
	 case 0x15: /* FLOG10 */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    flog10_rr(dreg,sreg);
	    break;
	 case 0x16: /* FLOG2 */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    flog2_rr(dreg,sreg);
	    break;
	 case 0x18: /* FABS */
	 case 0x58:
	 case 0x5c:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fabs_rr(dreg,sreg);
	    break;
	 case 0x19: /* FCOSH */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fcosh_rr(dreg,sreg);
	    break;
	 case 0x1a: /* FNEG */
	 case 0x5a:
	 case 0x5e:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fneg_rr(dreg,sreg);
	    break;
	 case 0x1c: /* FACOS */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
#if USE_X86_FPUCW
	    if ((regs.fpcr & 0x30) != 0x10) {
		mov_l_ri(S1,(regs.fpcr & 0xC0) | 0x10); /* use round to zero */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		facos_rr(dreg,sreg);
		mov_l_rm(S1,(uae_u32)&regs.fpcr);
		and_l_ri(S1,0xf0); /* restore control word */
		fldcw_m_indexed(S1,(uae_u32)x86_fpucw);
		break;
	    }
#endif
	    facos_rr(dreg,sreg);
	    break;
	 case 0x1d: /* FCOS */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fcos_rr(dreg,sreg);
	    break;
	 case 0x1e: /* FGETEXP */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fgetexp_rr(dreg,sreg);
	    break;
	 case 0x1f: /* FGETMAN */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fgetman_rr(dreg,sreg);
	    break;
	 case 0x20: /* FDIV */
	 case 0x60:
	 case 0x64:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fdiv_rr(dreg,sreg);
	    break;
	 case 0x21: /* FMOD */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    frem_rr(dreg,sreg);
	    break;
	 case 0x22: /* FADD */
	 case 0x62:
	 case 0x66:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fadd_rr(dreg,sreg);
	    break;
	 case 0x23: /* FMUL */
	 case 0x63:
	 case 0x67:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fmul_rr(dreg,sreg);
	    break;
	 case 0x24: /* FSGLDIV */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fdiv_rr(dreg,sreg);
	    break;
	 case 0x25: /* FREM */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    frem1_rr(dreg,sreg);
	    break;
	 case 0x26: /* FSCALE */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fscale_rr(dreg,sreg);
	    break;
	 case 0x27: /* FSGLMUL */
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fmul_rr(dreg,sreg);
	    break;
	 case 0x28: /* FSUB */
	 case 0x68:
	 case 0x6c:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fsub_rr(dreg,sreg);
	    break;
	 case 0x30: /* FSINCOS */
	 case 0x31:
	 case 0x32:
	 case 0x33:
	 case 0x34:
	 case 0x35:
	 case 0x36:
	 case 0x37:
	    dont_care_fflags();
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    if (dreg == (extra & 7))
		fsin_rr(dreg, sreg);
	    else
		fsincos_rr(dreg, extra & 7, sreg);
	    break;
	 case 0x38: /* FCMP */
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fmov_rr(FP_RESULT,dreg);
	    fsub_rr(FP_RESULT,sreg); /* Right way? */
	    return;
	 case 0x3a: /* FTST */
	    if ((sreg=get_fp_value(opcode,extra)) < 0) {FAIL(1);return;}
	    fmov_rr(FP_RESULT,sreg);
	    return;
	 default:
	    FAIL(1);
	    return;
	}
	MAKE_FPSR (dreg);
	return;
    }
    m68k_setpc (m68k_getpc () - 4);
    op_illg (opcode);
}
#endif
