
	.text

	.globl _callinflate
	.globl _execute_test000
	.globl _execute_test010
	.globl _execute_test020
	.globl _execute_testfpu
	.globl _exceptiontable000
	.globl _exceptiontable010
	.globl _exceptiontable020
	.globl _exceptiontablefpu
	.globl _setvbr
	.globl _setcpu
	.globl _flushcache
	.globl _msp_address1
	.globl _msp_address2
	.globl _msp_address3
	.globl _msp_address4
	.globl _error_vector

| must match main.c
S_DREG = 0
S_AREG = S_DREG+8*4
S_SSP = S_AREG+8*4
S_MSP = S_SSP+4
S_PC = S_MSP+4
S_SR = S_PC+4
S_EXPSR = S_SR+4
S_EXC = S_EXPSR+4
S_EXC010 = S_EXC+4
S_EXCFRAME = S_EXC010+4
S_FPU = S_EXCFRAME+4
S_FPIAR = S_FPU+8*12
S_FPCR = S_FPIAR+4
S_FPSR = S_FPCR+4
S_TRACECNT = S_FPSR+4
S_TRACESTACK = S_TRACECNT+4
S_NEXT = S_TRACESTACK+12

_callinflate:
	movem.l a4-a5,-(sp)
	move.l 4+2*4(sp),a4
	move.l 8+2*4(sp),a5
	bsr _inflate
	movem.l (sp)+,a4-a5
	rts

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
	moveq #0,d0
	cmp.w #4,d1
	bcc.s .scend1b
	| 68020-68030 only
	movec caar,d0
.scend1b:
	move.l d0,(a0)+
	moveq #0,d0
	cmp.w #5,d1
	bcc.s .scend1c
	| 68020-68040 only
	movec msp,d0
.scend1c:
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
	cmp.w #4,d1
	bcc.s .scend2b
	| 68020-68030 only
	movec d0,caar
.scend2b:
	move.l (a1)+,d0
	cmp.w #5,d1
	bcc.s .scend2
	| 68020-68040 only
	movec d0,msp
.scend2:
	rts

_flushcache:
	move.l 4(sp),d1 | cpu_lvl
	cmp.w #4,d1
	bcc flushcache040
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
	move.w (a1),ACTIVITYREG | opcode = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP
	movem.l (a0),d0-d7/a0-a6
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
	move.w (a1),ACTIVITYREG | opcode = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP
	movem.l (a0),d0-d7/a0-a6
	| clear data output buffer.
	| we don't want random DOB contents in bus/address error frame
	move.w #0xf00d,dummy
	rte

	| 68020+ test entrypoint
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
	move.w (a1),ACTIVITYREG | opcode = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP

	move.l S_MSP(a0),a1
_msp_address1:
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
	move.w (a1),ACTIVITYREG | opcode = background color
	move.w S_SR+2(a0),-(sp)
	move.l S_AREG+7*4(a0),a1
	move.l a1,USP

	move.l S_MSP(a0),a1
_msp_address2:
	movec a1,MSP

	fmovem.x S_FPU(a0),fp0-fp7
	lea S_FPIAR(a0),a1
	fmove.l (a1)+,fpiar
	fmove.l (a1)+,fpcr
	fmove.l (a1)+,fpsr
	movem.l (a0),d0-d7/a0-a6
	rte

exception_trace000:
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	tst.l S_TRACECNT(a0)
	bne.s .nexttrace000
	move.l 4(sp),S_TRACESTACK(a0)
	move.l 8(sp),S_TRACESTACK+4(a0)
.nexttrace000:
	addq.l #1,S_TRACECNT(a0)
	move.l (sp)+,a0
	rte

_exceptiontable000:
	bsr.s exception	| 2
	bsr.s exception	| 3
	bsr.s exception | 4
	bsr.s exception | 5
	bsr.s exception | 6
	bsr.s exception | 7
	bsr.s exception | 8
	bra.s exception_trace000 | 9
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
	move.w sr,-(sp)
	move.w #0,ACTIVITYREG
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)
	move.w (sp)+,S_EXPSR+2(a0)
	move.l (sp)+,d0
	lea _exceptiontable000(pc),a1
	sub.l a1,d0
	lsr.w #1,d0
	addq.w #1,d0
	move.w d0,S_EXC+2(a0)

	move.l sp,S_EXCFRAME(a0)
	cmp.w #2,d0
	beq.s .isbuserror
	cmp.w #3,d0
	bne.s .noaddresserror
.isbuserror:
	move.w (sp),S_EXC+0(a0)
	addq.w #8,sp
.noaddresserror:
	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)

	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)

	move.w #0x222,ACTIVITYREG
	move.l superstack(pc),sp
	move.w (sp)+,sr
	movem.l (sp)+,d1-d7/a0-a6
	rts

exception_trace010:
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	tst.l S_TRACECNT(a0)
	bne.s .nexttrace010
	move.l 4(sp),S_TRACESTACK(a0)
	move.l 8(sp),S_TRACESTACK+4(a0)
	move.l 12(sp),S_TRACESTACK+8(a0)
.nexttrace010:
	addq.l #1,S_TRACECNT(a0)
	move.l (sp)+,a0
	rte

_exceptiontable010:
	bsr.s exception010	| 2
	bsr.s exception010	| 3
	bsr.s exception010 | 4
	bsr.s exception010 | 5
	bsr.s exception010 | 6
	bsr.s exception010 | 7
	bsr.s exception010 | 8
	bra.s exception_trace010 | 9
	bsr.s exception010 | 10
	bsr.s exception010 | 11
	bsr.s exception010 | 12
	bsr.s exception010 | 13
	bsr.s exception010 | 14
	bsr.s exception010 | 15
	bsr.s exception010 | 16
	bsr.s exception010 | 17
	bsr.s exception010 | 18
	bsr.s exception010 | 19
	bsr.s exception010 | 20
	bsr.s exception010 | 21
	bsr.s exception010 | 22
	bsr.s exception010 | 23
	bsr.s exception010 | 24
	bsr.s exception010 | 25
	bsr.s exception010 | 26
	bsr.s exception010 | 27
	bsr.s exception010 | 28
	bsr.s exception010 | 29
	bsr.s exception010 | 30
	bsr.s exception010 | 31
	bsr.s exception010 | 32
	bsr.s exception010 | 33
	bsr.s exception010 | 34
	bsr.s exception010 | 35
	bsr.s exception010 | 36
	bsr.s exception010 | 37
	bsr.s exception010 | 38
	bsr.s exception010 | 30
	bsr.s exception010 | 40
	bsr.s exception010 | 41
	bsr.s exception010 | 42
	bsr.s exception010 | 43
	bsr.s exception010 | 44
	bsr.s exception010 | 45
	bsr.s exception010 | 46
	bsr.s exception010 | 47
	nop
exception010:
	move.w sr,-(sp)
	move.w #0,ACTIVITYREG
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)
	move.w (sp)+,S_EXPSR+2(a0)

	move.l (sp)+,d0
	lea _exceptiontable010(pc),a1
	sub.l a1,d0
	lsr.w #1,d0
	addq.w #1,d0
	move.w d0,S_EXC+2(a0)

	move.l sp,S_EXCFRAME(a0)
	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)
	move.w (sp),d0
	and.w #0xfff,d0
	lsr.w #2,d0
	move.w d0,S_EXC010+2(a0)

	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)
		
	move.w #0x222,ACTIVITYREG
	move.l superstack(pc),sp
	move.w (sp)+,sr
	movem.l (sp)+,d1-d7/a0-a6
	rts

exception_trace010t1:
	bra exception_trace010

_exceptiontable020:
	bsr.s exception020	| 2
	bsr.s exception020	| 3
	bsr.s exception020 | 4
	bsr.s exception020 | 5
	bsr.s exception020 | 6
	bsr.s exception020 | 7
	bsr.s exception020 | 8
	bra.s exception_trace010t1 | 9
	bsr.s exception020 | 10
	bsr.s exception020 | 11
	bsr.s exception020 | 12
	bsr.s exception020 | 13
	bsr.s exception020 | 14
	bsr.s exception020 | 15
	bsr.s exception020 | 16
	bsr.s exception020 | 17
	bsr.s exception020 | 18
	bsr.s exception020 | 19
	bsr.s exception020 | 20
	bsr.s exception020 | 21
	bsr.s exception020 | 22
	bsr.s exception020 | 23
	bsr.s exception020 | 24
	bsr.s exception020 | 25
	bsr.s exception020 | 26
	bsr.s exception020 | 27
	bsr.s exception020 | 28
	bsr.s exception020 | 29
	bsr.s exception020 | 30
	bsr.s exception020 | 31
	bsr.s exception020 | 32
	bsr.s exception020 | 33
	bsr.s exception020 | 34
	bsr.s exception020 | 35
	bsr.s exception020 | 36
	bsr.s exception020 | 37
	bsr.s exception020 | 38
	bsr.s exception020 | 30
	bsr.s exception020 | 40
	bsr.s exception020 | 41
	bsr.s exception020 | 42
	bsr.s exception020 | 43
	bsr.s exception020 | 44
	bsr.s exception020 | 45
	bsr.s exception020 | 46
	bsr.s exception020 | 47
	nop
exception020:
	move.w sr,-(sp)
	move.w #0,ACTIVITYREG
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)
	move.w (sp)+,S_EXPSR+2(a0)

	move.l (sp)+,d0
	lea _exceptiontable020(pc),a1
	sub.l a1,d0
	lsr.w #1,d0
	addq.w #1,d0
	move.w d0,S_EXC+2(a0)

	move.l sp,S_EXCFRAME(a0)
	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)
	move.w (sp),d0
	and.w #0xfff,d0
	lsr.w #2,d0
	move.w d0,S_EXC010+2(a0)
	
_msp_address3:
	movec MSP,a1
	move.l a1,S_MSP(a0)

	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)
		
	move.w #0x222,ACTIVITYREG
	| restore SR first, then stack
	| M-bit may have been set.
	move.l superstack(pc),a0
	move.w (a0)+,sr
	move.l a0,sp
	movem.l (sp)+,d1-d7/a0-a6
	rts

exception_trace010t2:
	bra exception_trace010

_exceptiontablefpu:
	bsr.s exceptionfpu	| 2
	bsr.s exceptionfpu	| 3
	bsr.s exceptionfpu | 4
	bsr.s exceptionfpu | 5
	bsr.s exceptionfpu | 6
	bsr.s exceptionfpu | 7
	bsr.s exceptionfpu | 8
	bsr.s exception_trace010t2 | 9
	bsr.s exceptionfpu | 10
	bsr.s exceptionfpu | 11
	bsr.s exceptionfpu | 12
	bsr.s exceptionfpu | 13
	bsr.s exceptionfpu | 14
	bsr.s exceptionfpu | 15
	bsr.s exceptionfpu | 16
	bsr.s exceptionfpu | 17
	bsr.s exceptionfpu | 18
	bsr.s exceptionfpu | 19
	bsr.s exceptionfpu | 20
	bsr.s exceptionfpu | 21
	bsr.s exceptionfpu | 22
	bsr.s exceptionfpu | 23
	bsr.s exceptionfpu | 24
	bsr.s exceptionfpu | 25
	bsr.s exceptionfpu | 26
	bsr.s exceptionfpu | 27
	bsr.s exceptionfpu | 28
	bsr.s exceptionfpu | 29
	bsr.s exceptionfpu | 30
	bsr.s exceptionfpu | 31
	bsr.s exceptionfpu | 32
	bsr.s exceptionfpu | 33
	bsr.s exceptionfpu | 34
	bsr.s exceptionfpu | 35
	bsr.s exceptionfpu | 36
	bsr.s exceptionfpu | 37
	bsr.s exceptionfpu | 38
	bsr.s exceptionfpu | 30
	bsr.s exceptionfpu | 40
	bsr.s exceptionfpu | 41
	bsr.s exceptionfpu | 42
	bsr.s exceptionfpu | 43
	bsr.s exceptionfpu | 44
	bsr.s exceptionfpu | 45
	bsr.s exceptionfpu | 46
	bsr.s exceptionfpu | 47
	nop
exceptionfpu:
	move.w sr,-(sp)
	move.w #0,ACTIVITYREG
	move.l a0,-(sp)
	move.l datapointer(pc),a0
	movem.l d0-d7/a0-a6,(a0)
	move.l (sp)+,8*4(a0)
	move.w (sp)+,S_EXPSR+2(a0)

	move.l (sp)+,d0
	lea _exceptiontable020(pc),a1
	sub.l a1,d0
	lsr.w #1,d0
	addq.w #1,d0
	move.w d0,S_EXC+2(a0)

	move.l sp,S_EXCFRAME(a0)
	move.w (sp)+,S_SR+2(a0)
	move.l (sp)+,S_PC(a0)
	move.w (sp),d0
	and.w #0xfff,d0
	lsr.w #2,d0
	move.w d0,S_EXC010+2(a0)
	
_msp_address4:
	movec MSP,a1
	move.l a1,S_MSP(a0)

	move.l USP,a1
	move.l a1,S_AREG+7*4(a0)

	fmovem.x fp0-fp7,S_FPU(a0)
	lea S_FPIAR(a0),a1
	fmove.l fpiar,(a1)+
	fmove.l fpcr,(a1)+
	fmove.l fpsr,(a1)+
		
	move.w #0x222,ACTIVITYREG
	move.l superstack(pc),a0
	move.w (a0)+,sr
	move.l a0,sp
	movem.l (sp)+,d1-d7/a0-a6
	rts

_error_vector:
	or.w #0x700,sr
waiterr:
	move.w #0x400,ACTIVITYREG
	move.w #0x004,ACTIVITYREG
	bra.s waiterr

datapointer:
	dc.l 0
superstack:
	dc.l 0
dummy:
	dc.l 0
