
	.text

	.equ ACTIVITYREG,0xdff180
	.equ CYCLEREG,0xdff006
	
	.globl _allocate_absolute
	.globl _free_absolute
	.globl _touser
	.globl _tosuper
	.globl _testexit
	.globl _get_cpu_model
	.globl sync
	.globl _uae_command

_uae_command:
	move.l 4(sp),a0
	movem.l d2-d3/a2-a3/a6,-(sp)
	move.l 4.w,a6
	move.l a0,a2
	lea uaeres(pc),a1
	jsr -0x1f2(a6) | OpenResource
	tst.l d0
	beq.s .nores
	move.l d0,a6
	lea uaefuncname(pc),a0
	jsr -6(a6)
	move.l d0,a0
	clr.l -(sp)
	clr.l -(sp)
	move.l #100,-(sp)
	move.l a2,-(sp)
	move.l #-1,-(sp)
	move.l #82,-(sp)
	jsr (a0)
	add.w #6*4,sp
.nores:
	movem.l (sp)+,d2-d3/a2-a3/a6
	rts

uaeres:
	.asciz "uae.resource"
uaefuncname:
	.asciz "uaelib_demux"

sync:
	move.b 0xdff006,d0
	beq.s sync
	cmp.b #0xff,d0
	beq.s sync
	cmp.b #0x38,d0
	beq.s sync
	| above prevent wraparound between
	| dff004.w and dff006.w reads
	move.l 0xdff004,d0
	rts

	| check left mouse button/joystick fire
_testexit:
	moveq #0,d0
	move.b 0xbfe001,d0
	and.b #0xc0,d0
	eor.b #0xc0,d0
	| d0 != 0: -> exit
	rts

	| super mode, disable interrupts
_tosuper:
	move.l a6,-(sp)
	move.l 4.w,a6
	jsr -0x78(a6) | Disable
	jsr -0x96(a6) | SuperState
	tst.l 8+1*4(sp)
	beq.s .keepdisplay
	move.w #0x0200,0xdff096
.keepdisplay:
	move.w #0x2700,sr
	move.l (sp)+,a6
	rts
	
	| back to user mode, enable interrupts
_touser:
	move.l a6,-(sp)
	move.l 4.w,a6
	move.l 8(sp),d0
	jsr -0x9c(a6) | UserState
	move.w #0x8200,0xdff096
	jsr -0x7e(a6) | Enable
	move.l (sp)+,a6
	rts

	| free absolute allocated test memory
_free_absolute:
	move.l a6,-(sp)
	move.l 8(sp),a1
	move.l 12(sp),d0
	move.l 4.w,a6
	jsr -0xd2(a6) | FreeMem
	move.l (sp)+,a6
	rts

	| allocate absolute memory
_allocate_absolute:
	move.l a6,-(sp)
	move.l 8(sp),a1
	move.l 12(sp),d0
	move.l 4.w,a6
	jsr -0xcc(a6) | AllocAbs
	move.l (sp)+,a6
	rts

	| return CPU model (68000=0, 68010=1, 68020=2, 68030=3, 68040=4, 68060=5)
_get_cpu_model:
	move.l 4.w,a0
	move.w 0x128(a0),d1
	cmp.w #15,d1
	bne.s .cpucheck2
	| 68040 but could be also 68060
	movem.l a5/a6,-(sp)
	move.l a0,a6
	lea scpucheck(pc),a5
	jsr -0x1e(a6) | Supervisor
	movem.l (sp)+,a5/a6
	bra.s .cpudone2
.cpucheck2:	
	moveq #5,d0
	tst.b d1
	bmi.s .cpudone2
	moveq #3,d0
.cpucheck:
	btst d0,d1
	bne.s .cpudone
	dbf d0,.cpucheck
.cpudone:
	addq.w #1,d0
.cpudone2:
	rts

scpucheck:
	or.w #0x0700,sr
	movec vbr,a0
	move.l 0x10(a0),d1
	lea illg(pc),a1
	move.l a1,0x10(a0)
	move.l sp,a1
	moveq #4,d0
	dc.l 0x4e7a0808 | movec pcr,d0
	moveq #5,d0
illg:
	move.l d1,0x10(a0)
	move.l a1,sp
	rte

	.include "asm.S"
