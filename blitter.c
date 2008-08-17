 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Custom chip emulation
  *
  * (c) 1995 Bernd Schmidt, Alessandro Bissacco
  * (c) 2002 - 2005 Toni Wilen
  */

//#define BLITTER_DEBUG
//#define BLITTER_SLOWDOWNDEBUG 4
//#define BLITTER_DEBUG_NO_D

#define SPEEDUP

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "blitter.h"
#include "blit.h"
#include "savestate.h"
#include "debug.h"

/* we must not change ce-mode while blitter is running.. */
static int blitter_cycle_exact;

uae_u16 bltcon0, bltcon1;
uae_u32 bltapt, bltbpt, bltcpt, bltdpt;

int blinea_shift;
static uae_u16 blinea, blineb;
static int blitline, blitfc, blitfill, blitife, blitsing, blitdesc;
static int blitonedot, blitsign;
static int blit_add;
static int blit_modadda, blit_modaddb, blit_modaddc, blit_modaddd;
static int blit_ch;

#ifdef BLITTER_DEBUG
static int blitter_dontdo;
static int blitter_delayed_debug;
#endif
#ifdef BLITTER_SLOWDOWNDEBUG
static int blitter_slowdowndebug;
#endif

struct bltinfo blt_info;

static uae_u8 blit_filltable[256][4][2];
uae_u32 blit_masktable[BLITTER_MAX_WORDS];
enum blitter_states bltstate;

static int blit_cyclecounter, blit_maxcyclecounter, blit_slowdown;
static int blit_linecyclecounter, blit_misscyclecounter;

#ifdef CPUEMU_12
extern uae_u8 cycle_line[];
#endif

static long blit_firstline_cycles;
static long blit_first_cycle;
static int blit_last_cycle, blit_dmacount, blit_dmacount2;
static int blit_linecycles, blit_extracycles, blit_nod;
static const int *blit_diag;

static uae_u16 ddat1, ddat2;
static int ddat1use, ddat2use;

/*

Confirmed blitter information by Toni Wilen
(order of channels or position of idle cycles are not confirmed)

1=BLTCON0 channel mask
2=total cycles per blitted word
[3=steals all cycles if BLTNASTY=1 (always if A-channel is enabled. this is illogical..)]
4=total cycles per blitted word in fillmode
5=cycle diagram (first cycle)
6=main cycle diagram (ABCD=channels,-=idle cycle,x=idle cycle but bus allocated)

1 234 5    6

F 4*4*ABC- ABCD
E 4*4*ABC- ABC-
D 3*4 AB-  ABD
C 3*3 AB-  AB-
B 3*3*AC-  ACD
A 2*2*AC   AC
9 2*3 A-   AD
8 2*2 A-   A-
7 4 4 -BC- -BCD
6 4 4 -BC- -BC-
5 3 4 -B-  -BD
4 3 3 -B-  -B-
3 3 3 -C-  -CD
2 3 3 -C-  -C-
1 2 3 -D   -D
0 2 3 --   --

NOTES: (BLTNASTY=1)

- Blitter ALWAYS needs free bus cycle, even if it is running an "idle" cycle.
  Exception: possible extra fill mode idle cycle is "real" idle cycle.
  Can someone explain this? Why does idle cycles need bus cycles?
- Fill mode may add one extra real idle cycle.(depends on channel mask)
- All blits with channel A enabled use all available bus cycles
  (stops CPU accesses to Agnus bus if BLTNASTY=1) WTF? I did another test
  and this can't be true... Maybe I am becoming crazy..
- idle cycles (no A-channel enabled) are not "used" by blitter, they are freely
  available for CPU.

BLTNASTY=0 makes things even more interesting..

- even zero channel blits get slower if BLTNASTY=0 depending on the number of
  active bitplanes. ALSO "2 cycle" blits with one real cycle and one idle cycle
  have the exact same speed as zero channel blit in all situations -> only the
  total number of cycles count, number of active channels does not matter.

*/


/* -1 = idle cycle and allocate bus */

static const int blit_cycle_diagram[][10] =
{
    { 0, 2, 0,0 },		/* 0 */
    { 0, 2, 0,4 },		/* 1 */
    { 0, 3, 0,3,0 },		/* 2 */
    { 2, 3, 0,3,4, 3,0 },	/* 3 */
    { 0, 3, 0,2,0 },		/* 4 */
    { 2, 3, 0,2,4, 2,0 },	/* 5 */
    { 0, 4, 0,2,3,0 },		/* 6 */
    { 3, 4, 0,2,3,4, 2,3,0 },	/* 7 */
    { 0, 2, 1,0 },		/* 8 */
    { 2, 2, 1,4, 1,0 },		/* 9 */
    { 0, 2, 1,3 },		/* A */
    { 3, 3, 1,3,4, 1,3,0 },	/* B */
    { 2, 3, 1,2,0, 1,2 },	/* C */
    { 3, 3, 1,2,4, 1,2,0 },	/* D */
    { 0, 3, 1,2,3 },		/* E */
    { 4, 4, 1,2,3,4, 1,2,3,0 }	/* F */
};

/* 5 = fill mode idle cycle ("real" idle cycle) */

static const int blit_cycle_diagram_fill[][10] =
{
    { 0, 3, 0,5,0 },		/* 0 */
    { 0, 3, 0,5,4 },		/* 1 */
    { 0, 3, 0,3,0 },		/* 2 */
    { 2, 3, 3,5,4, 3,0 },	/* 3 */
    { 0, 3, 0,2,5 },		/* 4 */
    { 3, 4, 0,2,5,4, 2,0,0 },	/* 5 */
    { 0, 4, 2,3,5,0 },		/* 6 */
    { 3, 4, 2,3,5,4, 2,3,0 },	/* 7 */
    { 0, 2, 1,5 },		/* 8 */
    { 2, 3, 1,5,4, 1,0},	/* 9 */
    { 0, 2, 1,3 },		/* A */
    { 3, 3, 1,3,4, 1,3,0 },	/* B */
    { 2, 3, 1,2,5, 1,2 },	/* C */
    { 3, 4, 1,2,5,4, 1,2,0 },	/* D */
    { 0, 3, 1,2,3 },		/* E */
    { 4, 4, 1,2,3,4, 1,2,3,0 }	/* F */
};

/*

    line draw takes 4 cycles (-X-X)
    it also have real idle cycles and only 2 dma fetches
    (read from C, write to D, but see below)

    Oddities:

    - first word is written to address pointed by BLTDPT
      but all following writes go to address pointed by BLTCPT!
    - BLTDMOD is ignored by blitter (BLTCMOD is used)
    - state of D-channel enable bit does not matter!
    - disabling A-channel freezes the content of BPLAPT

*/

static const int blit_cycle_diagram_line[] =
{
    0, 4, 0,3,0,4, 0,0,0,0,0,0,0,0,0,0
};

static const int blit_cycle_diagram_finald[] =
    { 0, 2, 0,4 };

void build_blitfilltable (void)
{
    unsigned int d, fillmask;
    int i;

    for (i = 0; i < BLITTER_MAX_WORDS; i++)
	blit_masktable[i] = 0xFFFF;

    for (d = 0; d < 256; d++) {
	for (i = 0; i < 4; i++) {
	    int fc = i & 1;
	    uae_u8 data = d;
	    for (fillmask = 1; fillmask != 0x100; fillmask <<= 1) {
		uae_u16 tmp = data;
		if (fc) {
		    if (i & 2)
			data |= fillmask;
		    else
			data ^= fillmask;
		}
		if (tmp & fillmask) fc = !fc;
	    }
	    blit_filltable[d][i][0] = data;
	    blit_filltable[d][i][1] = fc;
	}
    }
}

static void blitter_dump (void)
{
    write_log ("APT=%08X BPT=%08X CPT=%08X DPT=%08X\n", bltapt, bltbpt, bltcpt, bltdpt);
    write_log ("CON0=%04X CON1=%04X ADAT=%04X BDAT=%04X CDAT=%04X\n",
	       bltcon0, bltcon1, blt_info.bltadat, blt_info.bltbdat, blt_info.bltcdat);
    write_log ("AFWM=%04X ALWM=%04X AMOD=%04X BMOD=%04X CMOD=%04X DMOD=%04X\n",
	       blt_info.bltafwm, blt_info.bltalwm,
	       blt_info.bltamod & 0xffff, blt_info.bltbmod & 0xffff, blt_info.bltcmod & 0xffff, blt_info.bltdmod & 0xffff);
}

STATIC_INLINE int channel_state (int cycles)
{
    if (cycles < 0)
	return 0;
    if (cycles < blit_diag[0])
	return blit_diag[blit_diag[1] + 2 + cycles];
    return blit_diag[((cycles - blit_diag[0]) % blit_diag[1]) + 2];
}

extern int is_bitplane_dma (int hpos);
STATIC_INLINE int canblit (int hpos)
{
    if (is_bitplane_dma (hpos))
	return 0;
    if (cycle_line[hpos] == 0)
	return 1;
    if (cycle_line[hpos] & CYCLE_REFRESH)
	return -1;
    return 0;
}

static void blitter_done (void)
{
    ddat1use = ddat2use = 0;
    bltstate = BLT_done;
    blitter_done_notify ();
    INTREQ (0x8040);
    event2_remevent (ev2_blitter);
    unset_special (&regs, SPCFLAG_BLTNASTY);
#ifdef BLITTER_DEBUG
    write_log ("vpos=%d, cycles %d, missed %d, total %d\n",
	vpos, blit_cyclecounter, blit_misscyclecounter, blit_cyclecounter + blit_misscyclecounter);
#endif
}

STATIC_INLINE chipmem_agnus_wput2 (uaecptr addr, uae_u32 w)
{
#ifndef BLITTER_DEBUG_NO_D
    chipmem_agnus_wput (addr, w);
#endif
}

static void blitter_dofast (void)
{
    int i,j;
    uaecptr bltadatptr = 0, bltbdatptr = 0, bltcdatptr = 0, bltddatptr = 0;
    uae_u8 mt = bltcon0 & 0xFF;

    blit_masktable[0] = blt_info.bltafwm;
    blit_masktable[blt_info.hblitsize - 1] &= blt_info.bltalwm;

    if (bltcon0 & 0x800) {
	bltadatptr = bltapt;
	bltapt += (blt_info.hblitsize * 2 + blt_info.bltamod) * blt_info.vblitsize;
    }
    if (bltcon0 & 0x400) {
	bltbdatptr = bltbpt;
	bltbpt += (blt_info.hblitsize * 2 + blt_info.bltbmod) * blt_info.vblitsize;
    }
    if (bltcon0 & 0x200) {
	bltcdatptr = bltcpt;
	bltcpt += (blt_info.hblitsize * 2 + blt_info.bltcmod) * blt_info.vblitsize;
    }
    if (bltcon0 & 0x100) {
	bltddatptr = bltdpt;
	bltdpt += (blt_info.hblitsize * 2 + blt_info.bltdmod) * blt_info.vblitsize;
    }

#ifdef SPEEDUP
    if (blitfunc_dofast[mt] && !blitfill) {
	(*blitfunc_dofast[mt])(bltadatptr, bltbdatptr, bltcdatptr, bltddatptr, &blt_info);
    } else
#endif
    {
	uae_u32 blitbhold = blt_info.bltbhold;
	uae_u32 preva = 0, prevb = 0;
	uaecptr dstp = 0;
	int dodst = 0;

	/*if (!blitfill) write_log ("minterm %x not present\n",mt); */
	for (j = 0; j < blt_info.vblitsize; j++) {
	    blitfc = !!(bltcon1 & 0x4);
	    for (i = 0; i < blt_info.hblitsize; i++) {
		uae_u32 bltadat, blitahold;
		uae_u16 bltbdat;
		if (bltadatptr) {
		    blt_info.bltadat = bltadat = chipmem_agnus_wget (bltadatptr);
		    bltadatptr += 2;
		} else
		    bltadat = blt_info.bltadat;
		bltadat &= blit_masktable[i];
		blitahold = (((uae_u32)preva << 16) | bltadat) >> blt_info.blitashift;
		preva = bltadat;

		if (bltbdatptr) {
		    blt_info.bltbdat = bltbdat = chipmem_agnus_wget (bltbdatptr);
		    bltbdatptr += 2;
		    blitbhold = (((uae_u32)prevb << 16) | bltbdat) >> blt_info.blitbshift;
		    prevb = bltbdat;
		}

		if (bltcdatptr) {
		    blt_info.bltcdat = chipmem_agnus_wget (bltcdatptr);
		    bltcdatptr += 2;
		}
		if (dodst)
		    chipmem_agnus_wput2 (dstp, blt_info.bltddat);
		blt_info.bltddat = blit_func (blitahold, blitbhold, blt_info.bltcdat, mt) & 0xFFFF;
		if (blitfill) {
		    uae_u16 d = blt_info.bltddat;
		    int ifemode = blitife ? 2 : 0;
		    int fc1 = blit_filltable[d & 255][ifemode + blitfc][1];
		    blt_info.bltddat = (blit_filltable[d & 255][ifemode + blitfc][0]
			+ (blit_filltable[d >> 8][ifemode + fc1][0] << 8));
		    blitfc = blit_filltable[d >> 8][ifemode + fc1][1];
		}
		if (blt_info.bltddat)
		    blt_info.blitzero = 0;
		if (bltddatptr) {
		    dodst = 1;
		    dstp = bltddatptr;
		    bltddatptr += 2;
		}
	    }
	    if (bltadatptr)
		bltadatptr += blt_info.bltamod;
	    if (bltbdatptr)
		bltbdatptr += blt_info.bltbmod;
	    if (bltcdatptr)
		bltcdatptr += blt_info.bltcmod;
	    if (bltddatptr)
		bltddatptr += blt_info.bltdmod;
	}
	if (dodst)
	    chipmem_agnus_wput2 (dstp, blt_info.bltddat);
	blt_info.bltbhold = blitbhold;
    }
    blit_masktable[0] = 0xFFFF;
    blit_masktable[blt_info.hblitsize - 1] = 0xFFFF;

    bltstate = BLT_done;
}

static void blitter_dofast_desc (void)
{
    int i,j;
    uaecptr bltadatptr = 0, bltbdatptr = 0, bltcdatptr = 0, bltddatptr = 0;
    uae_u8 mt = bltcon0 & 0xFF;

    blit_masktable[0] = blt_info.bltafwm;
    blit_masktable[blt_info.hblitsize - 1] &= blt_info.bltalwm;

    if (bltcon0 & 0x800) {
	bltadatptr = bltapt;
	bltapt -= (blt_info.hblitsize*2 + blt_info.bltamod)*blt_info.vblitsize;
    }
    if (bltcon0 & 0x400) {
	bltbdatptr = bltbpt;
	bltbpt -= (blt_info.hblitsize*2 + blt_info.bltbmod)*blt_info.vblitsize;
    }
    if (bltcon0 & 0x200) {
	bltcdatptr = bltcpt;
	bltcpt -= (blt_info.hblitsize*2 + blt_info.bltcmod)*blt_info.vblitsize;
    }
    if (bltcon0 & 0x100) {
	bltddatptr = bltdpt;
	bltdpt -= (blt_info.hblitsize*2 + blt_info.bltdmod)*blt_info.vblitsize;
    }
#ifdef SPEEDUP
    if (blitfunc_dofast_desc[mt] && !blitfill) {
	(*blitfunc_dofast_desc[mt])(bltadatptr, bltbdatptr, bltcdatptr, bltddatptr, &blt_info);
    } else
#endif
    {
	uae_u32 blitbhold = blt_info.bltbhold;
	uae_u32 preva = 0, prevb = 0;
	uaecptr dstp = 0;
	int dodst = 0;

	for (j = 0; j < blt_info.vblitsize; j++) {
	    blitfc = !!(bltcon1 & 0x4);
	    for (i = 0; i < blt_info.hblitsize; i++) {
		uae_u32 bltadat, blitahold;
		uae_u16 bltbdat;
		if (bltadatptr) {
		    bltadat = blt_info.bltadat = chipmem_agnus_wget (bltadatptr);
		    bltadatptr -= 2;
		} else
		    bltadat = blt_info.bltadat;
		bltadat &= blit_masktable[i];
		blitahold = (((uae_u32)bltadat << 16) | preva) >> blt_info.blitdownashift;
		preva = bltadat;

		if (bltbdatptr) {
		    blt_info.bltbdat = bltbdat = chipmem_agnus_wget (bltbdatptr);
		    bltbdatptr -= 2;
		    blitbhold = (((uae_u32)bltbdat << 16) | prevb) >> blt_info.blitdownbshift;
		    prevb = bltbdat;
		}

		if (bltcdatptr) {
		    blt_info.bltcdat = blt_info.bltbdat = chipmem_agnus_wget (bltcdatptr);
		    bltcdatptr -= 2;
		}
		if (dodst)
		    chipmem_agnus_wput2 (dstp, blt_info.bltddat);
		blt_info.bltddat = blit_func (blitahold, blitbhold, blt_info.bltcdat, mt) & 0xFFFF;
		if (blitfill) {
		    uae_u16 d = blt_info.bltddat;
		    int ifemode = blitife ? 2 : 0;
		    int fc1 = blit_filltable[d & 255][ifemode + blitfc][1];
		    blt_info.bltddat = (blit_filltable[d & 255][ifemode + blitfc][0]
			+ (blit_filltable[d >> 8][ifemode + fc1][0] << 8));
		    blitfc = blit_filltable[d >> 8][ifemode + fc1][1];
		}
		if (blt_info.bltddat)
		    blt_info.blitzero = 0;
		if (bltddatptr) {
		    dstp = bltddatptr;
		    dodst = 1;
		    bltddatptr -= 2;
		}
	    }
	    if (bltadatptr)
		bltadatptr -= blt_info.bltamod;
	    if (bltbdatptr)
		bltbdatptr -= blt_info.bltbmod;
	    if (bltcdatptr)
		bltcdatptr -= blt_info.bltcmod;
	    if (bltddatptr)
		bltddatptr -= blt_info.bltdmod;
	}
	if (dodst)
	    chipmem_agnus_wput2 (dstp, blt_info.bltddat);
	blt_info.bltbhold = blitbhold;
    }
    blit_masktable[0] = 0xFFFF;
    blit_masktable[blt_info.hblitsize - 1] = 0xFFFF;

    bltstate = BLT_done;
}

STATIC_INLINE void blitter_read (void)
{
    if (bltcon0 & 0x200) {
	if (!dmaen (DMA_BLITTER))
	    return;
	blt_info.bltcdat = chipmem_bank.wget(bltcpt);
    }
    bltstate = BLT_work;
}

STATIC_INLINE void blitter_write (void)
{
    if (blt_info.bltddat)
	blt_info.blitzero = 0;
    /* D-channel state has no effect on linedraw, but C must be enabled or nothing is drawn! */
    if (bltcon0 & 0x200) {
	if (!dmaen (DMA_BLITTER))
	    return;
	chipmem_bank.wput(bltdpt, blt_info.bltddat);
    }
    bltstate = BLT_next;
}

STATIC_INLINE void blitter_line_incx (void)
{
    if (++blinea_shift == 16) {
	blinea_shift = 0;
	bltcpt += 2;
    }
}

STATIC_INLINE void blitter_line_decx (void)
{
    if (blinea_shift-- == 0) {
	blinea_shift = 15;
	bltcpt -= 2;
    }
}

STATIC_INLINE void blitter_line_decy (void)
{
    bltcpt -= blt_info.bltcmod;
    blitonedot = 0;
}

STATIC_INLINE void blitter_line_incy (void)
{
    bltcpt += blt_info.bltcmod;
    blitonedot = 0;
}

static void blitter_line (void)
{
    uae_u16 blitahold = (blinea & blt_info.bltafwm) >> blinea_shift;
    uae_u16 blitbhold = blineb & 1 ? 0xFFFF : 0;
    uae_u16 blitchold = blt_info.bltcdat;

    if (blitsing && blitonedot)
	blitahold = 0;
    blitonedot++;
    blt_info.bltddat = blit_func(blitahold, blitbhold, blitchold, bltcon0 & 0xFF);
}

static void blitter_line_proc (void)
{
    if (!blitsign) {
	if (bltcon0 & 0x800)
	    bltapt += (uae_s16)blt_info.bltamod;
	if (bltcon1 & 0x10) {
	    if (bltcon1 & 0x8)
		blitter_line_decy();
	    else
		blitter_line_incy();
	} else {
	    if (bltcon1 & 0x8)
		blitter_line_decx();
	    else
		blitter_line_incx();
	}
    } else {
	if (bltcon0 & 0x800)
	    bltapt += (uae_s16)blt_info.bltbmod;
    }
    if (bltcon1 & 0x10) {
	if (bltcon1 & 0x4)
	    blitter_line_decx();
	else
	    blitter_line_incx();
    } else {
	if (bltcon1 & 0x4)
	    blitter_line_decy();
	else
	    blitter_line_incy();
    }
    blitsign = 0 > (uae_s16)bltapt;
    bltstate = BLT_write;
}

STATIC_INLINE void blitter_nxline (void)
{
    blineb = (blineb << 1) | (blineb >> 15);
    blt_info.vblitsize--;
    bltstate = BLT_read;
}

#ifdef CPUEMU_12

static int blit_last_hpos;

static int blitter_cyclecounter;
static int blitter_hcounter1, blitter_hcounter2;
static int blitter_vcounter1, blitter_vcounter2;

static void decide_blitter_line (int hpos)
{
    hpos++;
    if (dmaen (DMA_BLITTER)) {
	while (blit_last_hpos < hpos) {
	    int c = blit_cyclecounter % 4;
	    for (;;) {
		if (c == 1 || c == 3) {
		    /* onedot mode and no pixel = bus write access is skipped */
		    if (c == 3 && blitsing && blitonedot > 1) {
			blit_cyclecounter++;
			if (blt_info.vblitsize == 0) {
			    bltdpt = bltcpt;
			    blitter_done();
			    return;
			}
			break;
		    }
		    if (canblit (blit_last_hpos) <= 0)
			break;
		}
		blit_cyclecounter++;
		if (c == 1) {
		    blitter_read();
		    alloc_cycle_ext (blit_last_hpos, CYCLE_BLITTER);
		} else if (c == 2) {
		    if (ddat1use) {
			bltdpt = bltcpt;
		    }
		    ddat1use = 1;
		    blitter_line();
		    blitter_line_proc();
		    blitter_nxline();
		} else if (c == 3) {
		    blitter_write();
		    alloc_cycle_ext (blit_last_hpos, CYCLE_BLITTER);
		    if (blt_info.vblitsize == 0) {
			bltdpt = bltcpt;
			blitter_done();
			return;
		    }
		}
		break;
	    }
	    blit_last_hpos++;
	}
    } else {
	blit_last_hpos = hpos;
    }
    if (blit_last_hpos > maxhpos)
	blit_last_hpos = 0;
}

#endif

static void actually_do_blit (void)
{
    if (blitline) {
	do {
	    blitter_read ();
	    blitter_line ();
	    blitter_line_proc ();
	    blitter_write ();
	    bltdpt = bltcpt;
	    blitter_nxline ();
	    if (blt_info.vblitsize == 0)
		bltstate = BLT_done;
	} while (bltstate != BLT_done);
    } else {
	if (blitdesc)
	    blitter_dofast_desc ();
	else
	    blitter_dofast ();
	bltstate = BLT_done;
    }
}

void blitter_handler (uae_u32 data)
{
    static int blitter_stuck;

    if (!dmaen (DMA_BLITTER)) {
	event2_newevent (ev2_blitter, 10);
	blitter_stuck++;
	if (blitter_stuck < 20000 || !currprefs.immediate_blits)
	    return; /* gotta come back later. */
	/* "free" blitter in immediate mode if it has been "stuck" ~3 frames
	 * fixes some JIT game incompatibilities
	 */
	debugtest (DEBUGTEST_BLITTER, "force-unstuck!\n");
    }
    blitter_stuck = 0;
    if (blit_slowdown > 0 && !currprefs.immediate_blits) {
	event2_newevent (ev2_blitter, blit_slowdown);
	blit_misscyclecounter = blit_slowdown;
	blit_slowdown = -1;
	return;
    }
#ifdef BLITTER_DEBUG
    if (!blitter_dontdo)
	actually_do_blit ();
    else
	bltstate = BLT_done;
#else
    actually_do_blit ();
#endif
    blitter_done ();
}

#ifdef CPUEMU_12

static uae_u32 preva, prevb;
STATIC_INLINE uae_u16 blitter_doblit (void)
{
    uae_u32 blitahold;
    uae_u16 bltadat, ddat;
    uae_u8 mt = bltcon0 & 0xFF;

    bltadat = blt_info.bltadat;
    if (blitter_hcounter1 == 0)
	bltadat &= blt_info.bltafwm;
    if (blitter_hcounter1 == blt_info.hblitsize - 1)
	bltadat &= blt_info.bltalwm;
    if (blitdesc)
	blitahold = (((uae_u32)bltadat << 16) | preva) >> blt_info.blitdownashift;
    else
	blitahold = (((uae_u32)preva << 16) | bltadat) >> blt_info.blitashift;
    preva = bltadat;

    ddat = blit_func (blitahold, blt_info.bltbhold, blt_info.bltcdat, mt) & 0xFFFF;

    if (bltcon1 & 0x18) {
	uae_u16 d = ddat;
	int ifemode = blitife ? 2 : 0;
	int fc1 = blit_filltable[d & 255][ifemode + blitfc][1];
	ddat = (blit_filltable[d & 255][ifemode + blitfc][0]
	    + (blit_filltable[d >> 8][ifemode + fc1][0] << 8));
	blitfc = blit_filltable[d >> 8][ifemode + fc1][1];
    }

    if (ddat)
	blt_info.blitzero = 0;

    return ddat;
}


STATIC_INLINE int blitter_doddma (void)
{
    int wd;
    uae_u16 d;

    wd = 0;
    if (blit_dmacount2 == 0) {
	d =  blitter_doblit ();
	wd = -1;
    } else if (ddat2use) {
	d = ddat2;
	ddat2use = 0;
	wd = 2;
    } else if (ddat1use) {
	d = ddat1;
	ddat1use = 0;
	wd = 1;
    }
    if (wd) {
	chipmem_agnus_wput2 (bltdpt, d);
	bltdpt += blit_add;
	blitter_hcounter2++;
	if (blitter_hcounter2 == blt_info.hblitsize) {
	    blitter_hcounter2 = 0;
	    bltdpt += blit_modaddd;
	    blitter_vcounter2++;
	    if (blitter_vcounter2 > blitter_vcounter1)
		blitter_vcounter1 = blitter_vcounter2;
	}
	if (blit_ch == 1)
	    blitter_hcounter1 = blitter_hcounter2;
    }
    return wd;
}

STATIC_INLINE void blitter_dodma (int ch)
{

    switch (ch)
    {
	case 1:
	blt_info.bltadat = chipmem_agnus_wget (bltapt);
	bltapt += blit_add;
	break;
	case 2:
	blt_info.bltbdat = chipmem_agnus_wget (bltbpt);
	bltbpt += blit_add;
	if (blitdesc)
	    blt_info.bltbhold = (((uae_u32)blt_info.bltbdat << 16) | prevb) >> blt_info.blitdownbshift;
	else
	    blt_info.bltbhold = (((uae_u32)prevb << 16) | blt_info.bltbdat) >> blt_info.blitbshift;
	prevb = blt_info.bltbdat;
	break;
	case 3:
	blt_info.bltcdat = chipmem_agnus_wget (bltcpt);
	bltcpt += blit_add;
	break;
    }

    blitter_cyclecounter++;
    if (blitter_cyclecounter >= blit_dmacount2) {
	blitter_cyclecounter = 0;
	ddat2 = ddat1;
	ddat2use = ddat1use;
	ddat1use = 0;
	ddat1 = blitter_doblit ();
	if (bltcon0 & 0x100)
	    ddat1use = 1;
	blitter_hcounter1++;
	if (blitter_hcounter1 == blt_info.hblitsize) {
	    blitter_hcounter1 = 0;
	    if (bltcon0 & 0x800)
		bltapt += blit_modadda;
	    if (bltcon0 & 0x400)
		bltbpt += blit_modaddb;
	    if (bltcon0 & 0x200)
		bltcpt += blit_modaddc;
	    blitter_vcounter1++;
	    blitfc = !!(bltcon1 & 0x4);
	}
    }
}

void decide_blitter (int hpos)
{
    if (bltstate == BLT_done)
	return;
#ifdef BLITTER_DEBUG
    if (blitter_delayed_debug) {
	blitter_delayed_debug = 0;
	blitter_dump ();
    }
#endif
    if (!blitter_cycle_exact)
	return;

    if (blit_linecyclecounter > 0) {
	while (blit_linecyclecounter > 0 && blit_last_hpos < hpos) {
	    blit_linecyclecounter--;
	    blit_last_hpos++;
	}
	if (blit_last_hpos > maxhpos)
	    blit_last_hpos = 0;
    }
    if (blit_linecyclecounter > 0) {
	blit_last_hpos = hpos;
	return;
    }

    if (blitline) {
	blt_info.got_cycle = 1;
	decide_blitter_line (hpos);
	return;
    }
    hpos++;
    if (dmaen (DMA_BLITTER)) {
	while (blit_last_hpos < hpos) {
	    int c = channel_state (blit_cyclecounter);
#ifdef BLITTER_SLOWDOWNDEBUG
	    blitter_slowdowndebug--;
	    if (blitter_slowdowndebug < 0) {
		cycle_line[blit_last_hpos] |= CYCLE_BLITTER;
		blitter_slowdowndebug = BLITTER_SLOWDOWNDEBUG;
	    }
#endif
	    for (;;) {
		int v;

		if (c == 5) { /* real idle cycle */
		    blit_cyclecounter++;
		    break;
		}

		/* all cycles need free bus, even idle cycles (except fillmode idle) */
		v = canblit (blit_last_hpos);
		if (v < 0 && c == 0) {
		    blit_cyclecounter++;
		    break;
		}
		if (v <= 0) {
		    blit_misscyclecounter++;
		    break;
		}

		blt_info.got_cycle = 1;
		if (c < 0) { /* no channel but bus still needs to be allocated.. */
		    alloc_cycle_ext (blit_last_hpos, CYCLE_BLITTER);
		    blit_cyclecounter++;
		} else if (c == 4) {
		    if (blitter_doddma ()) {
			alloc_cycle_ext (blit_last_hpos, CYCLE_BLITTER);
			blit_cyclecounter++;
		    }
		} else if (c) {
		    if (blitter_vcounter1 < blt_info.vblitsize) {
			alloc_cycle_ext (blit_last_hpos, CYCLE_BLITTER);
			blitter_dodma (c);
		    }
		    blit_cyclecounter++;
		} else {
		    blit_cyclecounter++;
		    /* check if blit with zero channels has ended  */
		    if (blit_cyclecounter >= blit_maxcyclecounter) {
			blitter_done ();
			return;
		    }
		}
		if (blitter_vcounter1 >= blt_info.vblitsize && blitter_vcounter2 >= blt_info.vblitsize) {
		    if (!ddat1use && !ddat2use) {
			blitter_done ();
			return;
		    }
		    if (blit_diag != blit_cycle_diagram_finald) {
			blit_cyclecounter = 0;
			blit_diag = blit_cycle_diagram_finald;
		    }
		}
		break;
	    }
	    blit_last_hpos++;
	}
    } else {
	blit_last_hpos = hpos;
    }
    if (blit_last_hpos > maxhpos)
	blit_last_hpos = 0;
}
#else
void decide_blitter (int hpos) { }
#endif

static void blitter_force_finish (void)
{
    uae_u16 odmacon;
    if (bltstate == BLT_done)
	return;
    if (bltstate != BLT_done) {
	 /* blitter is currently running
	  * force finish (no blitter state support yet)
	  */
	odmacon = dmacon;
	dmacon |= DMA_MASTER | DMA_BLITTER;
	write_log ("forcing blitter finish\n");
	if (blitter_cycle_exact) {
	    int rounds = 10000;
	    while (bltstate != BLT_done && rounds > 0) {
		memset (cycle_line, 0, maxhpos);
		decide_blitter (maxhpos);
		rounds--;
	    }
	    if (rounds == 0)
		write_log ("blitter froze!?\n");
	} else {
	    actually_do_blit ();
	}
	blitter_done ();
	dmacon = odmacon;
    }
}

static void blit_bltset (int con)
{
    int i;

    blitline = bltcon1 & 1;
    blitfill = bltcon1 & 0x18;
    blitdesc = bltcon1 & 2;
    blit_ch = (bltcon0 & 0x0f00) >> 8;

    if (blitline) {
	if (blt_info.hblitsize != 2)
	    debugtest (DEBUGTEST_BLITTER, "weird hblitsize in linemode: %d vsize=%d\n",
		blt_info.hblitsize, blt_info.vblitsize);
	blit_diag = blit_cycle_diagram_line;
    } else {
	if (con & 2) {
	    blitfc = !!(bltcon1 & 0x4);
	    blitife = bltcon1 & 0x8;
	    if ((bltcon1 & 0x18) == 0x18) {
		debugtest (DEBUGTEST_BLITTER, "weird fill mode\n");
		blitife = 0;
	    }
	}
	if (blitfill && !blitdesc)
	    debugtest (DEBUGTEST_BLITTER, "fill without desc\n");
	blit_diag = blitfill ? blit_cycle_diagram_fill[blit_ch] : blit_cycle_diagram[blit_ch];
    }
    if ((bltcon1 & 0x80) && (currprefs.chipset_mask & CSMASK_ECS_AGNUS))
	debugtest (DEBUGTEST_BLITTER, "ECS BLTCON1 DOFF-bit set\n");

    blit_dmacount = blit_dmacount2 = 0;
    blit_nod = 1;
    for (i = 0; i < blit_diag[1]; i++) {
	int v = blit_diag[2 + i];
	if (v)
	    blit_dmacount++;
	if (v > 0 && v < 4)
	    blit_dmacount2++;
	if (v == 4)
	    blit_nod = 0;
    }

    blt_info.blitashift = bltcon0 >> 12;
    blt_info.blitdownashift = 16 - blt_info.blitashift;
    blt_info.blitbshift = bltcon1 >> 12;
    blt_info.blitdownbshift = 16 - blt_info.blitbshift;
}

static void blit_modset (void)
{
    int mult;

    blit_add = blitdesc ? -2 : 2;
    mult = blitdesc ? -1 : 1;
    blit_modadda = mult * blt_info.bltamod;
    blit_modaddb = mult * blt_info.bltbmod;
    blit_modaddc = mult * blt_info.bltcmod;
    blit_modaddd = mult * blt_info.bltdmod;
}

void reset_blit (int bltcon)
{
    if (bltstate == BLT_done)
	return;
    if (bltcon)
	blit_bltset (bltcon);
    blit_modset ();
}

void do_blitter (int hpos)
{
    int cycles;
#ifdef BLITTER_DEBUG
    int oldstate = bltstate;
#endif

    blitter_cycle_exact = currprefs.blitter_cycle_exact;
    blt_info.blitzero = 1;
    bltstate = BLT_init;
    preva = 0;
    prevb = 0;
    blt_info.got_cycle = 0;

    blit_firstline_cycles = blit_first_cycle = get_cycles ();
    blit_misscyclecounter = 0;
    blit_last_cycle = 0;
    blit_maxcyclecounter = 0;
    blit_last_hpos = hpos;
    blit_cyclecounter = 0;

    blit_bltset (1|2);
    blit_modset ();
    ddat1use = ddat2use = 0;

    if (blitline) {
	blitsing = bltcon1 & 0x2;
	blinea = blt_info.bltadat;
	blineb = (blt_info.bltbdat >> blt_info.blitbshift) | (blt_info.bltbdat << (16 - blt_info.blitbshift));
	blitsign = bltcon1 & 0x40;
	blitonedot = 0;
	cycles = blt_info.vblitsize;
    } else {
	blit_firstline_cycles = blit_first_cycle + (blit_diag[1] * blt_info.hblitsize + cpu_cycles) * CYCLE_UNIT;
	cycles = blt_info.vblitsize * blt_info.hblitsize;
    }

#ifdef BLITTER_DEBUG
    blitter_dontdo = 0;
    if (1) {
	int ch = 0;
	if (oldstate != BLT_done)
	    write_log ("blitter was already active!\n");
	if (blit_ch & 1)
	    ch++;
	if (blit_ch & 2)
	    ch++;
	if (blit_ch & 4)
	    ch++;
	if (blit_ch & 8)
	    ch++;
	write_log ("blitstart: v=%03d h=%03d %dx%d ch=%d %d*%d=%d d=%d f=%02X n=%d pc=%p l=%d dma=%04X\n",
	    vpos, hpos, blt_info.hblitsize, blt_info.vblitsize, ch, blit_diag[1], cycles, blit_diag[1] * cycles,
	    blitdesc ? 1 : 0, blitfill, dmaen (DMA_BLITPRI) ? 1 : 0, M68K_GETPC, blitline, dmacon);
	blitter_dump ();
    }
#endif
    blit_slowdown = 0;

    unset_special (&regs, SPCFLAG_BLTNASTY);
    if (dmaen (DMA_BLITPRI))
	set_special (&regs, SPCFLAG_BLTNASTY);

    if (blt_info.vblitsize == 0 || (blitline && blt_info.hblitsize != 2)) {
	blitter_done ();
	return;
    }

    if (dmaen (DMA_BLITTER))
	bltstate = BLT_work;

    blit_maxcyclecounter = 0x7fffffff;
    if (blitter_cycle_exact) {
	blitter_hcounter1 = blitter_hcounter2 = 0;
	blitter_vcounter1 = blitter_vcounter2 = 0;
	if (blit_nod)
	    blitter_vcounter2 = blt_info.vblitsize;
	blit_linecyclecounter = 2;
	if (blit_ch == 0)
	    blit_maxcyclecounter = blt_info.hblitsize * blt_info.vblitsize;
	return;
    }

    blt_info.got_cycle = 1;
    if (currprefs.immediate_blits)
	cycles = 1;

    blit_cyclecounter = cycles * blit_diag[1]; 
    event2_newevent (ev2_blitter, blit_cyclecounter);
}

void maybe_blit (int hpos, int hack)
{
    static int warned;

    if (bltstate == BLT_done)
	return;
    if (savestate_state)
	return;

    if (!warned && dmaen (DMA_BLITTER)) {
#ifndef BLITTER_DEBUG
	warned = 1;
#endif
        debugtest (DEBUGTEST_BLITTER, "program does not wait for blitter vpos=%d tc=%d\n",
	    vpos, blit_cyclecounter);
    }

    if (blitter_cycle_exact) {
	decide_blitter (hpos);
	goto end;
    }

    if (hack == 1 && get_cycles() < blit_firstline_cycles)
	goto end;

    blitter_handler (0);
end:;
#ifdef BLITTER_DEBUG
    blitter_delayed_debug = 1;
#endif
}

int blitnasty (void)
{
    int cycles, ccnt;
    if (bltstate == BLT_done)
	return 0;
    if (!dmaen (DMA_BLITTER))
	return 0;
    if (blit_last_cycle >= blit_diag[0] && blit_dmacount == blit_diag[1])
	return 0;
    cycles = (get_cycles () - blit_first_cycle) / CYCLE_UNIT;
    ccnt = 0;
    while (blit_last_cycle < cycles) {
	int c = channel_state (blit_last_cycle++);
	if (!c)
	    ccnt++;
    }
    return ccnt;
}

/* very approximate emulation of blitter slowdown caused by bitplane DMA */
void blitter_slowdown (int ddfstrt, int ddfstop, int totalcycles, int freecycles)
{
    static int oddfstrt, oddfstop, ototal, ofree;
    static int slow;

    if (!totalcycles || ddfstrt < 0 || ddfstop < 0)
	return;
    if (ddfstrt != oddfstrt || ddfstop != oddfstop || totalcycles != ototal || ofree != freecycles) {
	int linecycles = ((ddfstop - ddfstrt + totalcycles - 1) / totalcycles) * totalcycles;
	int freelinecycles = ((ddfstop - ddfstrt + totalcycles - 1) / totalcycles) * freecycles;
	int dmacycles = (linecycles * blit_dmacount) / blit_diag[1];
	oddfstrt = ddfstrt;
	oddfstop = ddfstop;
	ototal = totalcycles;
	ofree = freecycles;
	slow = 0;
	if (dmacycles > freelinecycles)
	    slow = dmacycles - freelinecycles;
    }
    if (blit_slowdown < 0 || blitline)
	return;
    blit_slowdown += slow;
    blit_misscyclecounter += slow;
}

#ifdef SAVESTATE

uae_u8 *restore_blitter (uae_u8 *src)
{
    uae_u32 flags = restore_u32();

    bltstate = (flags & 1) ? BLT_init : BLT_done;
    if (flags & 2) {
	write_log ("blitter was force-finished when this statefile was saved\n");
	write_log ("contact the author if restored program freezes\n");
    }
    return src;
}

void restore_blitter_finish (void)
{
    if (bltstate == BLT_init) {
	write_log ("blitter was started but DMA was inactive during save\n");
	do_blitter (0);
    }
}

uae_u8 *save_blitter (int *len, uae_u8 *dstptr)
{
    uae_u8 *dstbak,*dst;
    int forced;

    forced = 0;
    if (bltstate != BLT_done && bltstate != BLT_init) {
	write_log ("blitter is active, forcing immediate finish\n");
	 /* blitter is active just now but we don't have blitter state support yet */
	blitter_force_finish ();
	forced = 2;
    }
    if (dstptr)
	dstbak = dst = dstptr;
    else
	dstbak = dst = (uae_u8*)malloc (16);
    save_u32(((bltstate != BLT_done) ? 0 : 1) | forced);
    *len = dst - dstbak;
    return dstbak;

}

#endif /* SAVESTATE */
