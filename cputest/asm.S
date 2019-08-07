
	.text

	.globl _execute_test000
	.globl _execute_test010
	.globl _execute_test020
	.globl _execute_testfpu
	.globl _exception010
	.globl _exception020
	.globl _exceptionfpu
	.globl _exceptiontable000
	.globl _setvbr
	.globl _setcpu
	.globl _flushcache

S_DREG = 0
S_AREG = S_DREG+8*4
S_SSP = S_AREG+8*4
S_MSP = S_SSP+4
S_PC = S_MSP+4
S_SR = S_PC+4
S_EXC = S_SR+4
S_FPU = S_EXC+4
S_FPIAR = S_FPU+8*12
S_FPCR = S_FPIAR+4
S_FPSR = S_FPCR+4

	| set CPU special registers
_setcpu:
	move.l 4(sp),d1 | cpu_lvl
	move.l 8(sp),a1 | new
	move.l 12(sp),a0 | store
	cmp.w #1,d1
	bcs.s .scend1
	move.l a0,d0
	beq.s .scend1
	movec sfc,d0
	move.l d0,(a0)+
	movec dfc,d0
	move.l d0,(a0)+
	cmp.w #2,d1
	bcs.s .scend1
	movec cacr,d0
	move.l d0,(a0)+
	movec caar,d0
	move.l d0,(a0)+
	movec msp,d0
	move.l d0,(a0)+
.scend1:
	move.l a1,d0
	beq.s .scend2
	cmp.w #1,d1
	bcs.s .scend2
	move.l (a1)+,d0
	movec d0,sfc
	move.l (a1)+,d0
	movec d0,dfc	
	cmp.w #2,d1
	bcs.s .scend2
	move.l (a1)+,d0
	movec d0,cacr
	move.l (a1)+,d0
	movec d0,caar
	move.l (a1)+,d0
	move.c d0,msp
.scend2:
	rts

_flushcache:
	movec cacr,d0
	bset #3,d0
	movec d0,cacr
	rts

	| set and return old VBR
_setvbr:
	move.l 4(sp),d1
	movec vbr,d0
	movec d1,vbr
	rts

	| 68000 test entrypoint
_execute_test000:
	movem.l d1-d7/a0-a6,-(sp)
	move.l 14*4+4(sp),a0 | register struct
	move.w sr,-(sp)
	lea datapointer(pc),a1
	move.l a0,(a1)+
	move.l sp,(a1)
	move.l S_SSP(a0),sp
	move.l S_PC(a0),a1
	move.l a1,-(sp)
	move.w (a1)+,d0
	move.w (a1),d1
	eor.w d1,d0
	move.w d0,0xdff180 | opcode^first param = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP
	movem.l (a0),d0-d7/a0-a6
	
	| cmp.l #0x,0x7a0000
	| bne.s .not
	| clr.w 0x100
|.not:
	
	rte

	| 68010+ test entrypoint
_execute_test010:
	movem.l d1-d7/a0-a6,-(sp)
	move.l 14*4+4(sp),a0 | register struct
	move.w sr,-(sp)
	lea datapointer(pc),a1
	move.l a0,(a1)+
	move.l sp,(a1)
	move.l S_SSP(a0),sp
	move.l S_PC(a0),a1
	clr.w -(sp)
	move.l a1,-(sp)
	move.w (a1)+,d0
	move.w (a1),d1
	eor.w d1,d0
	move.w d0,0xdff180 | opcode^first param = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP
	movem.l (a0),d0-d7/a0-a6
	rte

	| 68020 test entrypoint
_execute_test020:
	movem.l d1-d7/a0-a6,-(sp)
	move.l 14*4+4(sp),a0 | register struct
	move.w sr,-(sp)
	lea datapointer(pc),a1
	move.l a0,(a1)+
	move.l sp,(a1)
	move.l S_SSP(a0),sp
	move.l S_PC(a0),a1
	clr.w -(sp)
	move.l a1,-(sp)
	move.w (a1)+,d0
	move.w (a1),d1
	eor.w d1,d0
	move.w d0,0xdff180 | opcode^first param = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP
	move.l S_MSP(a0),a1
	movec a1,MSP
	movem.l (a0),d0-d7/a0-a6
	rte

	| 68020+FPU test entrypoint
_execute_testfpu:
	movem.l d1-d7/a0-a6,-(sp)
	move.l 14*4+4(sp),a0 | register struct
	move.w sr,-(sp)
	lea datapointer(pc),a1
	move.l a0,(a1)+
	move.l sp,(a1)
	move.l S_SSP(a0),sp
	move.l S_PC(a0),a1
	clr.w -(sp)
	move.l a1,-(sp)
	move.w (a1)+,d0
	move.w (a1),d1
	eor.w d1,d0
	move.w d0,0xdff180 | opcode^first param = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP
	move.l S_MSP(a0),a1
	movec a1,MSP
	fmovem.x S_FPU(a0),fp0-fp7
	lea S_FPIAR(a0),a1
	fmove.l (a1)+,fpiar
	fmove.l (a1)+,fpcr
	fmove.l (a1)+,fpsr
	movem.l (a0),d0-d7/a0-a6
	rte


_exceptiontable000:
	bsr.s exception	| 3
	bsr.s exception | 4
	bsr.s exception | 5
	bsr.s exception | 6
	bsr.s exception | 7
	bsr.s exception | 8
	bsr.s exception | 9
	bsr.s exception | 10
	bsr.s exception | 11
	bsr.s exception | 12
	bsr.s exception | 13
	bsr.s exception | 14
	bsr.s exception | 15
	bsr.s exception | 16
	bsr.s exception | 17
	bsr.s exception | 18
	bsr.s exception | 19
	bsr.s exception | 20
	bsr.s exception | 21
	bsr.s exception | 22
	bsr.s exception | 23
	bsr.s exception | 24
	bsr.s exception | 25
	bsr.s exception | 26
	bsr.s exception | 27
	bsr.s exception | 28
	bsr.s exception | 29
	bsr.s exception | 30
	bsr.s exception | 31
	bsr.s exception | 32
	bsr.s exception | 33
	bsr.s exception | 34
	bsr.s exception | 35
	bsr.s exception | 36
	bsr.s exception | 37
	bsr.s exception | 38
	bsr.s exception | 30
	bsr.s exception | 40
	bsr.s exception | 41
	bsr.s exception | 42
	bsr.s exception | 43
	bsr.s exception | 44
	bsr.s exception | 45
	bsr.s exception | 46
	bsr.s exception | 47
	nop
exception:
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)
	move.l (sp)+,d0
	lea _exceptiontable000(pc),a1
	sub.l a1,d0
	lsr.w #1,d0
	addq.w #2,d0
	move.w d0,S_EXC+2(a0)

	cmp.w #3,d0
	bne.s .noaddresserror
	move.w (sp),S_EXC+0(a0)
	addq.w #8,sp
.noaddresserror:
	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)

	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)

	move.l superstack(pc),sp
	move.w (sp)+,sr
	movem.l (sp)+,d1-d7/a0-a6
	rts

_exception010:
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)

	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)
	move.w (sp),d0
	and.w #0xfff,d0
	lsr.w #2,d0
	move.w d0,S_EXC+2(a0)
	
	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)
		
	move.l superstack(pc),sp
	move.w (sp)+,sr
	movem.l (sp)+,d1-d7/a0-a6
	rts
	
_exception020:
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)

	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)
	move.w (sp),d0
	and.w #0xfff,d0
	lsr.w #2,d0
	move.w d0,S_EXC+2(a0)
	
	movec MSP,a1
	move.l a1,S_MSP(a0)
	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)
		
	| restore SR first, then stack
	| M-bit may have been set.
	move.l superstack(pc),a0
	move.w (a0)+,sr
	move.l a0,sp
	movem.l (sp)+,d1-d7/a0-a6
	rts

_exceptionfpu:
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)

	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)
	move.w (sp),d0
	and.w #0xfff,d0
	lsr.w #2,d0
	move.w d0,S_EXC+2(a0)
	
	movec MSP,a1
	move.l a1,S_MSP(a0)
	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)

	fmovem.x fp0-fp7,S_FPU(a0)
	lea S_FPIAR(a0),a1
	fmove.l fpiar,(a1)+
	fmove.l fpcr,(a1)+
	fmove.l fpsr,(a1)+
		
	move.l superstack(pc),a0
	move.w (a0)+,sr
	move.l a0,sp
	movem.l (sp)+,d1-d7/a0-a6
	rts

datapointer:
	dc.l 0
superstack:
	dc.l 0
