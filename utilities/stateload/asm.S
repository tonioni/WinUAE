
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
	
	| wait for first line
.wait1:
	move.l 4(a6),d0
	lsr.l #8,d0
	and.w #511,d0
	cmp.w #258,d0
	bne .wait1
.wait2:
	move.l 4(a6),d0
	lsr.l #8,d0
	and.w #511,d0
	bne .wait2

	move.w #0x1234,0xfc0004

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
	move.l CPU_CHUNK(a4),a0
	move.l 4+4+60+4+2+2+4(a0),SP
	move.l 4+4+60+4+2+2(a0),a1 | USP
	move.l a1,USP
	cmp.l #68020,(a0)
	bcs .cpu68010
	lea 4+4+60+4+2+2+4+4+2+4+4+4+4(a0),a1
	move.l (a1)+,d0
	movec d0,CAAR
	move.l (a1)+,d0
	movec d0,CACR
	move.l (a1)+,d0
	movec d0,MSP
.cpu68010:
	cmp.l #68010,(a0)
	bcs .cpu68000
	lea 4+4+60+4+2+2+4+4+2+4(a0),a1
	move.l (a1)+,d0
	movec d0,DFC
	move.l (a1)+,d0
	movec d0,SFC
	move.l (a1)+,d0
	movec d0,VBR
	move.w #0x0020,-(sp) | Format 0, Trap #0
.cpu68000:
	move.l 4+4+60(a0),-(sp) | PC
	move.w 4+4+60+4+2+2+4+4(a0),-(sp) | SR
	movem.l 4+4(a0),d0-d7/a0-a6
	
	move.w #0x1234,0xfc0000
	
	rte | GO! GO! GO!
