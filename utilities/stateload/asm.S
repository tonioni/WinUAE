
CPU_CHUNK = 4
CIAA_CHUNK = CPU_CHUNK+4
CIAB_CHUNK = CIAA_CHUNK+4
CUSTOM_CHUNK = CIAB_CHUNK+4
AGA_COLORS_CHUNK = CUSTOM_CHUNK+4
FLOPPY_CHUNK = AGA_COLORS_CHUNK+4
AUDIO_CHUNK = FLOPPY_CHUNK+4*4
SPRITE_CHUNK = AUDIO_CHUNK+4*4
MAP_ROM = SPRITE_CHUNK+4
MAP_ROM_SIZE = MAP_ROM+4
MAP_ROM_TYPE = MAP_ROM_SIZE+4

	.text
	.chip 68020
	.globl _runit
	.globl _killsystem
	.globl _callinflate
	.globl _inflate
	.globl _flushcache

_flushcache:
	move.l a6,-(sp)
	move.l 4.w,a6
	jsr -0x27c(a6) | CacheClearU
	move.l (sp)+,a6
	rts

_callinflate:
	movem.l a4-a5,-(sp)
	move.l 4+2*4(sp),a4
	move.l 8+2*4(sp),a5
	bsr _inflate
	movem.l (sp)+,a4-a5
	rts

	| params: new stack 4, uaestate 8, func(uaestate) 12
_killsystem:
	move.l 8(sp),a0 | uaestate
	move.l a6,a1
	move.l 4,a6
	move.w 0x128(a6),(a0) | AttFlags
	move.l sp,d1
	move.l a5,a0
	lea .super(pc),a5
	jsr -0x1e(a6) | Supervisor
.super:
	move.w #0x2700,sr
	move.l a0,a5
	move.l a1,a6
	lea 0xdff000,a0

	| CIA: stop timers, clear interrupts
	bclr #0,0xbfde00
	bclr #0,0xbfdf00
	bclr #0,0xbfee01
	bclr #0,0xbfef01
	move.b #0x7f,0xbfdd00
	move.b #0x7f,0xbfed01
	tst.b 0xbfdd00
	tst.b 0xbfed01
	
	move.w #0x7fff,0x96(a0)
	move.w #0x7fff,0x9a(a0)
	move.w #0x7fff,0x9c(a0)
	move.w #0x7fff,0x9e(a0)
	move.l d1,a0
	move.l 4(a0),sp | new temp super stack
	move.l 8(a0),-(sp) | uaestate
	move.l 12(a0),a0 | func
	jsr (a0) | func(uaestate)
	| never returns

_runit:
	move.l 4(sp),a4	| get pointer to struct uaestate
	subq.l #8,sp
	lea 0xdff000,a6
	
	| find last line
	moveq #0,d5
	lea 0x6(a6),a2
.wait1:
	cmp.b #250,(a2)
	bne.s .wait1
.wait1a:
	cmp.b #2,(a2)
	bne.s .wait1a
.wait1b:
	move.w 4(a6),d0
	btst #0,d0
	beq.s .wait1c
	move.b (a2),d0
	cmp.b d0,d5
	bcc.s .wait1b
	move.b d0,d5
	bra.s .wait1b
.wait1c:
	cmp.b #160,(a2)
	bne.s .wait1c

	| last line - 14
	sub.b #14,d5
.wait3:
	cmp.b (a2),d5
	bne.s .wait3

	move.w #0x1234,0xfc0004

	move.l CUSTOM_CHUNK(a4),a0
	move.w 4+0x96(a0),d0 | DMACON
	and.w #15,d0
	or.w #0x8200,d0
	| enable audio DMA
	move.w d0,0x96(a6)

	| wait few lines, enough time for audio channels to enter normal 2/3 states
	addq.w #4,d5
.wait4:
	cmp.b (a2),d5
	bne.s .wait4
	| restore latched AUDxLEN and AUDxPT
	move.l a4,(sp)
	bsr _set_audio_final

	| restore possible side-effect causing
	| custom bits as late as possible

	move.l CIAA_CHUNK(a4),(sp)
	clr.l 4(sp)
	bsr _set_cia_final
	move.l CIAB_CHUNK(a4),(sp)
	addq.l #1,4(sp)
	bsr _set_cia_final
	move.l CUSTOM_CHUNK(a4),(sp)
	bsr _set_custom_final

	| restore CPU state
	move.w (a4),d1
	move.l CPU_CHUNK(a4),a0
	move.l 4+4+60+4+2+2+4(a0),SP
	move.l 4+4+60+4+2+2(a0),a1 | USP
	move.l a1,USP
	cmp.l #68020,(a0)
	bcs.s .cpu68010
	btst #1,d1
	beq.s .cpu68010
	lea 4+4+60+4+2+2+4+4+2+4+4+4+4(a0),a1
	move.l (a1)+,d0
	movec d0,CAAR
	move.l (a1)+,d0
	movec d0,CACR
	move.l (a1)+,d0
	movec d0,MSP
.cpu68010:
	cmp.l #68010,(a0)
	bcs.s .cpu68000
	btst #0,d1
	beq.s .cpu68000	
	lea 4+4+60+4+2+2+4+4+2+4(a0),a1
	move.l (a1)+,d0
	movec d0,DFC
	move.l (a1)+,d0
	movec d0,SFC
	move.l (a1)+,d0
	movec d0,VBR
.cpu68000:
	btst #0,d1
	beq.s .nocpu68010
	btst #1,d1
	beq.s .nocpu68020
	btst #0,3(a4) | nocache?
	beq.s .nocpu68020
	moveq #0,d0
	movec d0,CACR
.nocpu68020:
	move.w #0x0020,-(sp) | Format 0, Trap #0
.nocpu68010:

	move.l CIAA_CHUNK(a4),a1
	move.b 15(a1),d6
	swap d6
	move.b 14(a1),d6
	move.l CIAB_CHUNK(a4),a1
	move.b 15(a1),d7
	swap d7
	move.b 14(a1),d7

	move.l 4+4+60(a0),-(sp) | PC
	move.w 4+4+60+4+2+2+4+4(a0),-(sp) | SR
	movem.l 4+4(a0),d0-d5
	movem.l 4+4+8*4+4+4(a0),a2-a6
	add.w #4+4+6*4,a0

	move.w #0x1234,0xfc0000

	lea 0xbfde00,a1

	| wait last line
.wait2:
	tst.b 0xdff006
	bne.s .wait2

	| start timers
	move.b d6,0xbfee01-0xbfde00(a1)
	swap d6
	move.b d6,0xbfef01-0xbfde00(a1)
	move.b d7,(a1)
	swap d7
	move.b d7,0x100(a1)
	
	movem.l (a0),d6-d7/a0-a1
	
	rte | GO! GO! GO!
