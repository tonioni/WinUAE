 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Custom chip emulation
  *
  * (c) 1995 Bernd Schmidt, Alessandro Bissacco
  * (c) 2002 - 2003 Toni Wilen
  */

//#define BLITTER_DEBUG
//#define BLITTER_SLOWDOWNDEBUG 4

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "blitter.h"
#include "blit.h"
#include "savestate.h"

uae_u16 oldvblts;
uae_u16 bltcon0,bltcon1;
uae_u32 bltapt,bltbpt,bltcpt,bltdpt;

int blinea_shift;
static uae_u16 blinea, blineb;
static uaecptr bltcnxlpt,bltdnxlpt;
static int blitline, blitfc, blitfill, blitife, blitsing, blitdesc;
static int blitonedot,blitsign;
static int blit_add;
static int blit_modadda, blit_modaddb, blit_modaddc, blit_modaddd;
static int blit_ch;
int blit_singlechannel;

#ifdef BLITTER_DEBUG
static int blitter_dontdo;
#endif
#ifdef BLITTER_SLOWDOWNDEBUG
static int blitter_slowdowndebug;
#endif

struct bltinfo blt_info;

static uae_u8 blit_filltable[256][4][2];
uae_u32 blit_masktable[BLITTER_MAX_WORDS];
static uae_u16 blit_trashtable[BLITTER_MAX_WORDS];
enum blitter_states bltstate;

static int blit_cyclecounter, blit_maxcyclecounter, blit_slowdown, blit_cycles_total;
static int blit_linecyclecounter, blit_misscyclecounter;

#ifdef CPUEMU_6
extern uae_u8 cycle_line[];
#endif

static long blit_firstline_cycles;
static long blit_first_cycle;
static int blit_last_cycle, blit_dmacount, blit_dmacount2;
static int blit_linecycles, blit_extracycles;
static uae_u8 *blit_diag;

static uae_u16 ddat1, ddat2;
static int ddat1use, ddat2use;

static uae_u8 blit_cycle_diagram_finald[] =
    { 0, 2, 0,4 };

static uae_u8 blit_cycle_diagram[][10] =
{
    { 0, 2, 0,0 },		/* 0 */
    { 0, 2, 4,0 },		/* 1 */
    { 0, 2, 3,0 },		/* 2 */
    { 2, 3, 0,3,4, 3,0 },	/* 3 */
    { 0, 3, 2,0,0 },		/* 4 */
    { 2, 3, 0,2,4, 2,0 },	/* 5 */
    { 0, 3, 2,3,0 },		/* 6 */
    { 3, 4, 0,2,3,4, 2,3,0 },	/* 7 */
    { 0, 2, 1,0 },		/* 8 */
    { 2, 2, 1,4, 1,0 },		/* 9 */
    { 0, 2, 1,3 },		/* A */
    { 3, 3, 1,3,4, 1,3,0 },	/* B */
    { 2, 3, 0,1,2, 1,2 },	/* C */
    { 3, 3, 1,2,4, 1,2,0 },	/* D */
    { 0, 3, 1,2,3 },		/* E */
    { 4, 4, 1,2,3,4, 1,2,3,0 }	/* F */
};

/* fill mode always adds C-channel to cycle-diagram */
/* Reflect - Sound Vision freezes without this */
static uae_u8 blit_cycle_diagram_fill[][10] =
{
    { 0, 2, 0,0 },		/* 0 */
    { 0, 3, 0,3,4 },		/* 1 */
    { 0, 2, 3,0 },		/* 2 */
    { 2, 3, 0,3,4, 3,0 },	/* 3 */
    { 0, 3, 2,0,0 },		/* 4 */
    { 3, 4, 0,2,0,4, 2,0,0 },	/* 5 */
    { 0, 3, 2,3,0 },		/* 6 */
    { 3, 4, 0,2,3,4, 2,3,0 },	/* 7 */
    { 0, 2, 1,0 },		/* 8 */
    { 3, 3, 1,0,4, 1,0,0},	/* 9 */
    { 0, 2, 1,3 },		/* A */
    { 3, 3, 1,3,4, 1,3,0 },	/* B */
    { 2, 3, 0,1,2, 1,2 },	/* C */
    { 4, 4, 1,2,0,4, 1,2,0,0 },	/* D */
    { 0, 3, 1,2,3 },		/* E */
    { 4, 4, 1,2,3,4, 1,2,3,0 }	/* F */
};

static uae_u8 blit_cycle_diagram_line[] =
{
    0, 4, 0,0,0,4 /* total guess.. */
};

void build_blitfilltable(void)
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
    write_log ("APT=%08.8X BPT=%08.8X CPT=%08.8X DPT=%08.8X\n", bltapt, bltbpt, bltcpt, bltdpt);
    write_log ("CON0=%04.4X CON1=%04.4X ADAT=%04.4X BDAT=%04.4X CDAT=%04.4X\n",
	bltcon0, bltcon1, blt_info.bltadat, blt_info.bltbdat, blt_info.bltcdat);
    write_log ("AFWM=%04.4X ALWM=%04.4X AMOD=%04.4X BMOD=%04.4X CMOD=%04.4X DMOD=%04.4X\n",
	blt_info.bltafwm, blt_info.bltalwm,
	blt_info.bltamod & 0xffff, blt_info.bltbmod & 0xffff, blt_info.bltcmod & 0xffff, blt_info.bltdmod & 0xffff);
}

STATIC_INLINE uae_u8 *blit_xlateptr(uaecptr bltpt, int bytecount)
{
    if (!chipmem_bank.check(bltpt,bytecount)) return NULL;
    return chipmem_bank.xlateaddr(bltpt);
}

STATIC_INLINE uae_u8 *blit_xlateptr_desc(uaecptr bltpt, int bytecount)
{
    if (!chipmem_bank.check(bltpt-bytecount, bytecount)) return NULL;
    return chipmem_bank.xlateaddr(bltpt);
}

static void blitter_done (void)
{
    ddat1use = ddat2use = 0;
    bltstate = BLT_done;
    blitter_done_notify ();
    INTREQ(0x8040);
    eventtab[ev_blitter].active = 0;
    unset_special (SPCFLAG_BLTNASTY);
#ifdef BLITTER_DEBUG
    write_log ("vpos=%d, cycles %d, missed %d, total %d\n",
	vpos, blit_cyclecounter, blit_misscyclecounter, blit_cyclecounter + blit_misscyclecounter);
#endif
}

static void blitter_dofast(void)
{
    int i,j;
    uaecptr bltadatptr = 0, bltbdatptr = 0, bltcdatptr = 0, bltddatptr = 0;
    uae_u8 mt = bltcon0 & 0xFF;

    blit_masktable[0] = blt_info.bltafwm;
    blit_masktable[blt_info.hblitsize - 1] &= blt_info.bltalwm;

    if (bltcon0 & 0x800) {
	bltadatptr = bltapt;
	bltapt += (blt_info.hblitsize*2 + blt_info.bltamod)*blt_info.vblitsize;
    }
    if (bltcon0 & 0x400) {
	bltbdatptr = bltbpt;
	bltbpt += (blt_info.hblitsize*2 + blt_info.bltbmod)*blt_info.vblitsize;
    }
    if (bltcon0 & 0x200) {
	bltcdatptr = bltcpt;
	bltcpt += (blt_info.hblitsize*2 + blt_info.bltcmod)*blt_info.vblitsize;
    }
    if (bltcon0 & 0x100) {
	bltddatptr = bltdpt;
	bltdpt += (blt_info.hblitsize*2 + blt_info.bltdmod)*blt_info.vblitsize;
    }

    if (blitfunc_dofast[mt] && !blitfill)
	(*blitfunc_dofast[mt])(bltadatptr, bltbdatptr, bltcdatptr, bltddatptr, &blt_info);
    else {
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
		    blt_info.bltadat = bltadat = chipmem_wget (bltadatptr);
		    bltadatptr += 2;
		} else
		    bltadat = blt_info.bltadat;
		bltadat &= blit_masktable[i];
		blitahold = (((uae_u32)preva << 16) | bltadat) >> blt_info.blitashift;
		preva = bltadat;

		if (bltbdatptr) {
		    blt_info.bltbdat = bltbdat = chipmem_wget (bltbdatptr);
		    bltbdatptr += 2;
		    blitbhold = (((uae_u32)prevb << 16) | bltbdat) >> blt_info.blitbshift;
		    prevb = bltbdat;
		}

		if (bltcdatptr) {
		    blt_info.bltcdat = chipmem_wget (bltcdatptr);
		    bltcdatptr += 2;
		}
		if (dodst) chipmem_wput (dstp, blt_info.bltddat);
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
	    if (bltadatptr) bltadatptr += blt_info.bltamod;
	    if (bltbdatptr) bltbdatptr += blt_info.bltbmod;
	    if (bltcdatptr) bltcdatptr += blt_info.bltcmod;
	    if (bltddatptr) bltddatptr += blt_info.bltdmod;
	}
	if (dodst) chipmem_wput (dstp, blt_info.bltddat);
	blt_info.bltbhold = blitbhold;
    }
    blit_masktable[0] = 0xFFFF;
    blit_masktable[blt_info.hblitsize - 1] = 0xFFFF;

    bltstate = BLT_done;
}

static void blitter_dofast_desc(void)
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
    if (blitfunc_dofast_desc[mt] && !blitfill)
	(*blitfunc_dofast_desc[mt])(bltadatptr, bltbdatptr, bltcdatptr, bltddatptr, &blt_info);
    else {
	uae_u32 blitbhold = blt_info.bltbhold;
	uae_u32 preva = 0, prevb = 0;
	uaecptr dstp = 0;
	int dodst = 0;

/*	if (!blitfill) write_log ("minterm %x not present\n",mt);*/
	for (j = 0; j < blt_info.vblitsize; j++) {
	    blitfc = !!(bltcon1 & 0x4);
	    for (i = 0; i < blt_info.hblitsize; i++) {
		uae_u32 bltadat, blitahold;
		uae_u16 bltbdat;
		if (bltadatptr) {
		    bltadat = blt_info.bltadat = chipmem_wget (bltadatptr);
		    bltadatptr -= 2;
		} else
		    bltadat = blt_info.bltadat;
		bltadat &= blit_masktable[i];
		blitahold = (((uae_u32)bltadat << 16) | preva) >> blt_info.blitdownashift;
		preva = bltadat;

		if (bltbdatptr) {
		    blt_info.bltbdat = bltbdat = chipmem_wget (bltbdatptr);
		    bltbdatptr -= 2;
		    blitbhold = (((uae_u32)bltbdat << 16) | prevb) >> blt_info.blitdownbshift;
		    prevb = bltbdat;
		}

		if (bltcdatptr) {
		    blt_info.bltcdat = blt_info.bltbdat = chipmem_wget (bltcdatptr);
		    bltcdatptr -= 2;
		}
		if (dodst) chipmem_wput (dstp, blt_info.bltddat);
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
	    if (bltadatptr) bltadatptr -= blt_info.bltamod;
	    if (bltbdatptr) bltbdatptr -= blt_info.bltbmod;
	    if (bltcdatptr) bltcdatptr -= blt_info.bltcmod;
	    if (bltddatptr) bltddatptr -= blt_info.bltdmod;
	}
	if (dodst) chipmem_wput (dstp, blt_info.bltddat);
	blt_info.bltbhold = blitbhold;
    }
    blit_masktable[0] = 0xFFFF;
    blit_masktable[blt_info.hblitsize - 1] = 0xFFFF;

    bltstate = BLT_done;
}

STATIC_INLINE int blitter_read(void)
{
    if (bltcon0 & 0x200) {
	if (!dmaen(DMA_BLITTER))
	    return 1;
        blt_info.bltcdat = chipmem_bank.wget(bltcpt);
    }
    bltstate = BLT_work;
    return (bltcon0 & 0x200) != 0;
}

STATIC_INLINE int blitter_write(void)
{
    if (blt_info.bltddat)
	blt_info.blitzero = 0;
    /* D-channel state has no effect on linedraw, but C must be enabled or nothing is drawn! */
    if (bltcon0 & 0x200) {
	if (!dmaen(DMA_BLITTER)) return 1;
	chipmem_bank.wput(bltdpt, blt_info.bltddat);
	bltdpt = bltcpt; /* believe it or not but try Cardamon or Cardamom without this.. */
    }
    bltstate = BLT_next;
    return (bltcon0 & 0x200) != 0;
}

STATIC_INLINE void blitter_line_incx(void)
{
    if (++blinea_shift == 16) {
	blinea_shift = 0;
	bltcnxlpt += 2;
    }
}

STATIC_INLINE void blitter_line_decx(void)
{
    if (blinea_shift-- == 0) {
	blinea_shift = 15;
	bltcnxlpt -= 2;
    }
}

STATIC_INLINE void blitter_line_decy(void)
{
    bltcnxlpt -= blt_info.bltcmod;
    blitonedot = 0;
}

STATIC_INLINE void blitter_line_incy(void)
{
    bltcnxlpt += blt_info.bltcmod;
    blitonedot = 0;
}

static void blitter_line(void)
{
    uae_u16 blitahold = blinea >> blinea_shift;
    uae_u16 blitbhold = blineb & 1 ? 0xFFFF : 0;
    uae_u16 blitchold = blt_info.bltcdat;
    blt_info.bltddat = 0;

    if (blitsing && blitonedot)
	blitahold = 0;
    blitonedot = 1;
    blt_info.bltddat = blit_func(blitahold, blitbhold, blitchold, bltcon0 & 0xFF);
    if (!blitsign){
	if (bltcon0 & 0x800)
	    bltapt += (uae_s16)blt_info.bltamod;
	if (bltcon1 & 0x10){
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
    if (bltcon1 & 0x10){
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

STATIC_INLINE void blitter_nxline(void)
{
    bltdpt = bltcpt = bltcnxlpt;
    blineb = (blineb << 1) | (blineb >> 15);
    if (--blt_info.vblitsize == 0) {
	bltstate = BLT_done;
    } else {
	bltstate = BLT_read;
    }
}

static void actually_do_blit(void)
{
    if (blitline) {
	do {
	    blitter_read();
	    blitter_line();
	    blitter_write();
	    blitter_nxline();
	} while (bltstate != BLT_done);
    } else {
	if (blitdesc)
	    blitter_dofast_desc();
	else
	    blitter_dofast();
        bltstate = BLT_done;
    }
}

void blitter_handler(void)
{
    if (!dmaen(DMA_BLITTER)) {
        eventtab[ev_blitter].active = 1;
        eventtab[ev_blitter].oldcycles = get_cycles ();
        eventtab[ev_blitter].evtime = 10 * CYCLE_UNIT + get_cycles (); /* wait a little */
        return; /* gotta come back later. */
    }
    if (blit_slowdown > 0) {
	eventtab[ev_blitter].active = 1;
        eventtab[ev_blitter].oldcycles = get_cycles ();
        eventtab[ev_blitter].evtime = blit_slowdown * CYCLE_UNIT + get_cycles ();
        blit_slowdown = -1;
        return;
    }
#ifdef BLITTER_DEBUG
    if (!blitter_dontdo) 
	actually_do_blit();
    else
	bltstate = BLT_done;
#else
    actually_do_blit ();
#endif
    blitter_done ();
}

STATIC_INLINE int channel_state (int cycles)
{
    if (cycles < blit_diag[0])
        return blit_diag[blit_diag[1] + 2 + cycles];
    return blit_diag[((cycles - blit_diag[0]) % blit_diag[1]) + 2];
}

#ifdef CPUEMU_6

static int blit_last_hpos;

static int blitter_dma_cycles_line, blitter_dma_cycles_line_count;
static int blitter_cyclecounter;
static int blitter_hcounter1, blitter_hcounter2;
static int blitter_vcounter1, blitter_vcounter2;


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


int is_bitplane_dma (int hpos);

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
	chipmem_wput (bltdpt, d);
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
        blt_info.bltadat = chipmem_wget (bltapt);
	bltapt += blit_add;
	break;
	case 2:
	blt_info.bltbdat = chipmem_wget (bltbpt);
	bltbpt += blit_add;
	if (blitdesc)
	    blt_info.bltbhold = (((uae_u32)blt_info.bltbdat << 16) | prevb) >> blt_info.blitdownbshift;
	else
	    blt_info.bltbhold = (((uae_u32)prevb << 16) | blt_info.bltbdat) >> blt_info.blitbshift;
	prevb = blt_info.bltbdat;
	break;
	case 3:
	blt_info.bltcdat = chipmem_wget (bltcpt);
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

static void decide_blitter_line (int hpos)
{
    hpos++;
    if (dmaen (DMA_BLITTER)) {
	while (blit_last_hpos < hpos) {
	    int c = channel_state (blit_cyclecounter);
	    for (;;) {
	        if (c && (cycle_line[blit_last_hpos] || is_bitplane_dma (blit_last_hpos)))
		    break;
		if (c)
		    cycle_line[blit_last_hpos] |= CYCLE_BLITTER;
		blit_cyclecounter++;
		blit_linecyclecounter++;
		if (blit_linecyclecounter >= blit_diag[1]) {
		    blit_linecyclecounter = 0;
		    blitter_read();
		    blitter_line();
		    blitter_write();
		    blitter_nxline();
		    if (bltstate == BLT_done) {
			blitter_done ();
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

void decide_blitter (int hpos)
{
    if (bltstate == BLT_done)
	return;
    if (!currprefs.blitter_cycle_exact) {
        blitter_handler ();
	return;
    }
    if (blitline) {
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
	        if (c && (cycle_line[blit_last_hpos] || is_bitplane_dma (blit_last_hpos))) {
		    blit_misscyclecounter++;
		    break;
		}
		if (c == 4) {
		    if (blitter_doddma ()) {
			cycle_line[blit_last_hpos] |= CYCLE_BLITTER;
		        blit_cyclecounter++;
		    }
		} else if (c) {
		    if (blitter_vcounter1 < blt_info.vblitsize) {
			cycle_line[blit_last_hpos] |= CYCLE_BLITTER;
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
	if (currprefs.blitter_cycle_exact) {
	    while (bltstate != BLT_done) {
	        memset (cycle_line, 0, maxhpos);
		decide_blitter (maxhpos);
	    }
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

    blit_singlechannel = 0;
    if (blit_ch == 0 || blit_ch == 1 || blit_ch == 2 || blit_ch == 4 || blit_ch == 8)
	blit_singlechannel = 1;
    if (blitline) {
	if (blt_info.hblitsize != 2)
	    write_log ("weird hblitsize in linemode: %d vsize=%d PC%=%x\n", blt_info.hblitsize, blt_info.vblitsize, m68k_getpc());
        blit_diag = blit_cycle_diagram_line;
    } else {
	if (con & 2) {
	    blitfc = !!(bltcon1 & 0x4);
	    blitife = bltcon1 & 0x8;
	    if ((bltcon1 & 0x18) == 0x18) {
		/* Digital "Trash" demo does this; others too. Apparently, no
		* negative effects. */
		static int warn = 1;
		if (warn)
		    write_log ("warning: weird fill mode (further messages suppressed)\n");
		warn = 0;
		blitife = 0;
	    }
	}
	if (blitfill && !blitdesc) {
	    static int warn = 1;
	    if (warn)
	        write_log ("warning: blitter fill without desc (further messages suppressed)\n");
	    warn = 0;
	}
	blit_diag = blitfill ? blit_cycle_diagram_fill[blit_ch] : blit_cycle_diagram[blit_ch];
    }
    if ((bltcon1 & 0x80) && (currprefs.chipset_mask & CSMASK_ECS_AGNUS))
	write_log("warning: BLTCON1 DOFF-bit set\n");

    blit_dmacount = blit_dmacount2 = 0;
    for (i = 0; i < blit_diag[1]; i++) {
	int v = blit_diag[2 + i];
	if (v)
	    blit_dmacount++;
	if (v > 0 && v < 4)
	    blit_dmacount2++;
    }

    blt_info.blitashift = bltcon0 >> 12;
    blt_info.blitdownashift = 16 - blt_info.blitashift;
    blt_info.blitbshift = bltcon1 >> 12;
    blt_info.blitdownbshift = 16 - blt_info.blitbshift;
}

void blit_modset (void)
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

    blt_info.blitzero = 1;
    bltstate = BLT_init;
    preva = 0;
    prevb = 0;

    blit_firstline_cycles = blit_first_cycle = get_cycles ();
    blit_cyclecounter = blit_misscyclecounter = 0;
    blit_last_cycle = 0;
    blit_maxcyclecounter = 0;
    blit_last_hpos = hpos;

    reset_blit (1|2); 

    if (blitline) {
	bltcnxlpt = bltcpt;
	bltdnxlpt = bltdpt;
	blitsing = bltcon1 & 0x2;
	blinea = blt_info.bltadat;
	blineb = (blt_info.bltbdat >> blt_info.blitbshift) | (blt_info.bltbdat << (16 - blt_info.blitbshift));
	blitsign = bltcon1 & 0x40;
	blitonedot = 0;
        cycles = blt_info.vblitsize;
    } else {
        blit_firstline_cycles = blit_first_cycle + blit_diag[1] * blt_info.hblitsize * CYCLE_UNIT;
        cycles = blt_info.vblitsize * blt_info.hblitsize;
    }
    blit_cycles_total = cycles;

#ifdef BLITTER_DEBUG
    blitter_dontdo = 0;
    if (1) {
	write_log("blitstart: v=%03.3d h=%03.3d %dx%d %d (%d) d=%d f=%02.2X n=%d pc=%p l=%d dma=%d\n",
	    vpos, hpos, blt_info.hblitsize, blt_info.vblitsize, cycles, blit_ch,
	    blitdesc ? 1 : 0, blitfill,
	    dmaen(DMA_BLITPRI) ? 1 : 0, m68k_getpc(), blitline, dmaen(DMA_BLITTER));
	blitter_dump ();
    }
#endif

    blit_slowdown = 0;

    unset_special (SPCFLAG_BLTNASTY);
    if (dmaen(DMA_BLITPRI))
        set_special (SPCFLAG_BLTNASTY);

    if (blt_info.vblitsize == 0 || (blitline && blt_info.hblitsize != 2)) {
        blitter_done ();
        return;
    }

    if (dmaen(DMA_BLITTER))
	bltstate = BLT_work;

    blit_maxcyclecounter = 0x7fffffff;
    if (currprefs.blitter_cycle_exact) {
	blitter_dma_cycles_line_count = 0;
	blitter_hcounter1 = blitter_hcounter2 = 0;
	blitter_vcounter1 = blitter_vcounter2 = 0;
	if (blit_dmacount2 == blit_dmacount)
	    blitter_vcounter2 = blt_info.vblitsize;
	blit_linecyclecounter = 0;
	blitter_dma_cycles_line = blt_info.hblitsize * blit_dmacount2;
	if (blit_ch == 0)
	    blit_maxcyclecounter = blt_info.hblitsize * blt_info.vblitsize;
	return;
    }

    if (currprefs.immediate_blits)
        cycles = 1;

    eventtab[ev_blitter].active = 1;
    eventtab[ev_blitter].oldcycles = get_cycles ();
    eventtab[ev_blitter].evtime = cycles * blit_diag[1] * CYCLE_UNIT + get_cycles ();
    events_schedule();
}


void maybe_blit (int hpos, int hack)
{
    static int warned;

    if (bltstate == BLT_done)
	return;

    if (!warned && dmaen (DMA_BLITTER)) {
#ifndef BLITTER_DEBUG
	warned = 1;
#endif
	write_log ("warning: Program does not wait for blitter %p\n", m68k_getpc());
    }

    if (currprefs.blitter_cycle_exact) {
	decide_blitter (hpos);
	return;
    }

    if (!eventtab[ev_blitter].active)
	write_log ("FOO!!?\n");
    if (hack == 1 && get_cycles() < blit_firstline_cycles)
	return;

    blitter_handler ();
}

int blitnasty (void)
{
    int cycles, ccnt;
    if (bltstate == BLT_done)
	return 0;
    if (!dmaen(DMA_BLITTER))
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
    static int cycles_used;
    int slow;
    
    if (ddfstrt != oddfstrt || ddfstop != oddfstop || totalcycles != ototal || ofree != freecycles) {
	int linecycles = ddfstop - ddfstrt;
        cycles_used = 0;
        if (linecycles > blit_cycles_total)
	    linecycles = blit_cycles_total;
	if (linecycles < 0)
	    linecycles = 0;
	if (totalcycles == 0)
	    return;
	cycles_used = linecycles * (totalcycles - freecycles) / totalcycles;
	oddfstrt = ddfstrt;
	oddfstop = ddfstop;
	ototal = totalcycles;
	ofree = freecycles;
    }
    if (blit_slowdown < 0 || blitline || totalcycles == 0)
	return;
    slow = cycles_used * blit_dmacount / blit_diag[1];
    slow = slow * 7 / 10;
    if (slow <= 0)
	return;
    blit_slowdown += slow;
    blit_misscyclecounter += slow;
    blit_cycles_total -= maxhpos;
}

uae_u8 *restore_blitter (uae_u8 *src)
{
    uae_u32 flags = restore_u32();

    bltstate = (flags & 1) ? BLT_init : BLT_done;
    if (bltstate == BLT_init) {
	write_log ("blitter was started but DMA was inactive during save\n");
	do_blitter (0);
    }
    return src;
}

uae_u8 *save_blitter (int *len, uae_u8 *dstptr)
{
    uae_u8 *dstbak,*dst;

    if (bltstate != BLT_done && bltstate != BLT_init) {
	write_log ("blitter was running, forcing immediate finish\n");
	 /* blitter is active just now but we don't have blitter state support yet */
	blitter_force_finish ();
    }
    if (dstptr)
	dstbak = dst = dstptr;
    else
        dstbak = dst = malloc (16);
    save_u32((bltstate != BLT_done) ? 0 : 1);
    *len = dst - dstbak;
    return dstbak;

}
