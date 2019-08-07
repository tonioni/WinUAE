
	.text

	.globl _allocate_absolute
	.globl _free_absolute
	.globl _touser
	.globl _tosuper
	.globl _testexit
	.globl _get_cpu_model

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
	move.w #0x0200,0xdff096
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

	| return CPU model (68000=0, 68010=1, 68020=2)
_get_cpu_model:
	move.l 4.w,a0
	moveq #0,d0
	move.w 0x128(a0),d1
	and.w #3,d1
	beq.s .cpudone
	moveq #2,d0
	btst #1,d1
	bne.s .cpudone
	moveq #1,d0
.cpudone:
	rts

