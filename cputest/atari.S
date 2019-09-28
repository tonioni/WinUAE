| cputester's machine specific support for Atari's STF/STE/TT/Falcon
|
| 2019/09	Nicolas Pomarede

	.text

	.equ ACTIVITYREG,0xff8240

	.globl _allocate_absolute
	.globl _free_absolute
	.globl _touser
	.globl _tosuper
	.globl _testexit
	.globl _get_cpu_model

	| check "space" key is pressed
_testexit:
	moveq	#0,d0
	cmp.b	#0x39,0xfffc02
	bne.s	.done
	moveq	#1,d0
.done:
	| d0 != 0: -> exit
	rts

	| super mode, disable MFP interrupts
	| save hbl and vbl vectors and set them to a 'RTE'
_tosuper:
 	move.l	#0,-(sp)
	move.w	#0x20,-(sp)
	trap	#1
	addq.l	#6,sp
	move.l	d0,old_ssp

	move.l	0x68,save_hbl
	move.l	0x70,save_vbl
	move.l	#int_rte,0x68
	move.l	#int_rte,0x70

	move.b	0xfffa13,save_imra
	move.b	0xfffa15,save_imrb
	clr.b	0xfffa13
	clr.b	0xfffa15

	move.w	0xff8240,save_color0

	move.w	#0x2700,sr		| disable HBL/VBL
	rts

	| back to user mode, enable MFP interrupts
	| and restore hbl/vbl vectors
	| keep the value of A7/SSP into USP
_touser:
	move.w	save_color0,0xff8240

	move.b	save_imra,0xfffa13
	move.b	save_imrb,0xfffa15

	move.l	save_hbl,0x68
	move.l	save_vbl,0x70

	move.l	old_ssp(pc),-(sp)
	move.w	#0x20,-(sp)
        | we must keep the same value of A7 when going back to user mode
	| else the main C program will crash later
	move.l a7,usp
	trap	#1
	addq.l	#6,sp
	rts

	| free absolute allocated test memory
	| -> not supported for now
_free_absolute:
	rts

	| allocate absolute memory
	| -> not supported by TOS, assume memory can always be used
	| (requires 4 MB RAM)
	| in  : 4(sp) absolute memory address to allocate
	| in  : 8(sp) size of absolute memory address to allocate
	| out : d0=absolute memory address if success, or 0 if error
_allocate_absolute:
	move.l	4(sp),d0		| success
	rts

	| return CPU model (68000=0, 68010=1, 68020=2, 68030=3, 68040=4, 68060=5)
	| we read TOS' cookie jar at $5A0, or return 0 it cpu is not found
_get_cpu_model:
	move.l	#0,-(sp)
	move.w	#0x20,-(sp)
	trap	#1
	addq.l	#6,sp
	move.l	d0,old_ssp

	move.q	#0,d0
	move.l	0x5a0,d1
	beq.s	.cpudone		| no cookie jar, assume 68000

	move.l	d1,a0
.loop:
	move.l	(a0),d0
	beq.s	.cpudone		| end of cookie list, assume 68000
	cmp.l	#0x5f435055,d0		| '_CPU'
	beq.s	.found
	addq.w	#8,a0			| next cookie
	bra.s	.loop
.found:
	move.l	4(a0),d0		| _CPU value : 0, 10, 20, 30, 40 or 60
	divu	#10,d0
	ext.l	d0
	cmp.w	#6,d0
	bne.s	.cpudone
	moveq	#5,d0			| 68060 -> d0=5

.cpudone:
	move.l	d0,cpu_level

	move.l	old_ssp(pc),-(sp)
	move.w	#0x20,-(sp)
	trap	#1
	addq.l	#6,sp

	move.l	cpu_level,d0
	rts


	| empty interrupt function to replace TOS' HBL/VBL
int_rte:
	rte


old_ssp:
	dc.l	0
save_hbl:
	dc.l	0
save_vbl:
	dc.l	0
save_imra:
	dc.b	0
save_imrb:
	dc.b	0
save_color0:
	dc.w	0
cpu_level:
	dc.l	0

	.include "asm.S"
