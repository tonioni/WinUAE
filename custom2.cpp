/*
* UAE - The Un*x Amiga Emulator
*
* Custom chip emulation
*
* Copyright 1995-2002 Bernd Schmidt
* Copyright 1995 Alessandro Bissacco
* Copyright 2000-2010 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <assert.h>

#include "options.h"
#include "uae.h"
#include "gensound.h"
#include "audio.h"
#include "sounddep/sound.h"
#include "events.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cia.h"
#include "disk.h"
#include "blitter.h"
#include "xwin.h"
#include "inputdevice.h"
#include "inputrecord.h"
#include "keybuf.h"
#include "serial.h"
#include "autoconf.h"
#include "traps.h"
#include "gui.h"
#include "picasso96.h"
#include "drawing.h"
#include "savestate.h"
#include "ar.h"
#ifdef AVIOUTPUT
#include "avioutput.h"
#endif
#include "debug.h"
#include "akiko.h"
#include "cdtv.h"
#if defined(ENFORCER)
#include "enforcer.h"
#endif
#include "gayle.h"
#include "gfxfilter.h"
#include "a2091.h"
#include "a2065.h"
#include "ncr_scsi.h"
#include "blkdev.h"
#include "sampler.h"

#define CUSTOM_DEBUG 0
#define SPRITE_DEBUG 0
#define SPRITE_DEBUG_MINY 0x0
#define SPRITE_DEBUG_MAXY 0x100
#define SPR0_HPOS 0x15
#define MAX_SPRITES 8
#define SPRITE_COLLISIONS
#define SPEEDUP
#define AUTOSCALE_SPRITES 1

#define SPRBORDER 0

STATIC_INLINE bool nocustom (void)
{
	if (picasso_on && currprefs.picasso96_nocustom)
		return 1;
	return 0;
}

void uae_abort (const TCHAR *format,...)
{
	static int nomore;
	va_list parms;
	TCHAR buffer[1000];

	va_start (parms, format);
	_vsntprintf (buffer, sizeof (buffer) - 1, format, parms );
	va_end (parms);
	if (nomore) {
		write_log (_T("%s\n"), buffer);
		return;
	}
	gui_message (buffer);
	nomore = 1;
}

#if 0
void customhack_put (struct customhack *ch, uae_u16 v, int hpos)
{
	ch->v = v;
	ch->vpos = vpos;
	ch->hpos = hpos;
}

uae_u16 customhack_get (struct customhack *ch, int hpos)
{
	if (ch->vpos == vpos && ch->hpos == hpos) {
		ch->vpos = -1;
		return 0xffff;
	}
	return ch->v;
}
#endif

uae_u16 last_custom_value1;

static unsigned int n_consecutive_skipped = 0;
static unsigned int total_skipped = 0;

STATIC_INLINE void sync_copper (int hpos);


/* Events */

unsigned long int event_cycles, nextevent, is_lastline, currcycle;
long cycles_to_next_event;
long max_cycles_to_next_event;
long cycles_to_hsync_event;
unsigned long int vsync_cycles;
static int extra_cycle;
unsigned long start_cycles;

static int rpt_did_reset;
struct ev eventtab[ev_max];
struct ev2 eventtab2[ev2_max];

volatile frame_time_t vsynctime, vsyncmintime;

int vpos;
static int vpos_count, vpos_count_prev;
static int lof_store; // real bit in custom registers
static int lof_current; // what display device thinks
static int lol;
static int next_lineno, prev_lineno;
static enum nln_how nextline_how;
static int lof_changed = 0;
static int scandoubled_line;

/* Stupid genlock-detection prevention hack.
* We should stop calling vsync_handler() and
* hstop_handler() completely but it is not
* worth the trouble..
*/
static int vpos_previous, hpos_previous;
static int vpos_lpen, hpos_lpen, lightpen_triggered;
int lightpen_x, lightpen_y, lightpen_cx, lightpen_cy;

static uae_u32 sprtaba[256],sprtabb[256];
static uae_u32 sprite_ab_merge[256];
/* Tables for collision detection.  */
static uae_u32 sprclx[16], clxmask[16];

/* AGA T genlock bit in color registers */
static uae_u8 color_regs_aga_genlock[256];

/*
* Hardware registers of all sorts.
*/

static int REGPARAM3 custom_wput_1 (int, uaecptr, uae_u32, int) REGPARAM;

static uae_u16 cregs[256];

uae_u16 intena, intreq;
uae_u16 dmacon;
uae_u16 adkcon; /* used by audio code */

static uae_u32 cop1lc, cop2lc, copcon;

int maxhpos = MAXHPOS_PAL;
int maxhpos_short = MAXHPOS_PAL;
int maxvpos = MAXVPOS_PAL;
int maxvpos_nom = MAXVPOS_PAL; // nominal value (same as maxvpos but "faked" maxvpos in fake 60hz modes)
int hsyncendpos, hsyncstartpos;
static int maxvpos_total = 511;
int minfirstline = VBLANK_ENDLINE_PAL;
int equ_vblank_endline = EQU_ENDLINE_PAL;
double vblank_hz = VBLANK_HZ_PAL, fake_vblank_hz, vblank_hz_stored;
int vblank_skip, doublescan;
frame_time_t syncbase;
static int fmode;
uae_u16 beamcon0, new_beamcon0;
uae_u16 vtotal = MAXVPOS_PAL, htotal = MAXHPOS_PAL;
static int maxvpos_stored, maxhpos_stored;
static uae_u16 hsstop, hbstrt, hbstop, vsstop, vbstrt, vbstop, hsstrt, vsstrt, hcenter;
static int ciavsyncmode;
static int diw_hstrt, diw_hstop;
static int diw_hcounter;

#define HSYNCTIME (maxhpos * CYCLE_UNIT);

/* This is but an educated guess. It seems to be correct, but this stuff
* isn't documented well. */
struct sprite {
	uaecptr pt;
	int xpos;
	int vstart;
	int vstop;
	int dblscan; /* AGA SSCAN2 */
	int armed;
	int dmastate;
	int dmacycle;
	int ptxhpos;
};

static struct sprite spr[MAX_SPRITES];
static int plfstrt_sprite;

uaecptr sprite_0;
int sprite_0_width, sprite_0_height, sprite_0_doubled;
uae_u32 sprite_0_colors[4];
static uae_u8 magic_sprite_mask = 0xff;

static int sprite_vblank_endline = VBLANK_SPRITE_PAL;

static unsigned int sprctl[MAX_SPRITES], sprpos[MAX_SPRITES];
#ifdef AGA
static uae_u16 sprdata[MAX_SPRITES][4], sprdatb[MAX_SPRITES][4];
#else
static uae_u16 sprdata[MAX_SPRITES][1], sprdatb[MAX_SPRITES][1];
#endif
static int sprite_last_drawn_at[MAX_SPRITES];
static int last_sprite_point, nr_armed;
static int sprite_width, sprres;
int sprite_buffer_res;

#ifdef CPUEMU_12
uae_u8 cycle_line[256];
#endif

static uae_u16 bplxdat[8];
static bool bpl1dat_written, bpl1dat_early, bpl1dat_done;
static uae_s16 bpl1mod, bpl2mod;
static uaecptr prevbpl[2][MAXVPOS][8];
static uaecptr bplpt[8], bplptx[8];

/*static int blitcount[256];  blitter debug */

static struct color_entry current_colors;
static unsigned int bplcon0, bplcon1, bplcon2, bplcon3, bplcon4;
static unsigned int bplcon0d, bplcon0dd, bplcon0_res, bplcon0_planes, bplcon0_planes_limit;
static unsigned int diwstrt, diwstop, diwhigh;
static int diwhigh_written;
static unsigned int ddfstrt, ddfstop, ddfstrt_old_hpos;
static int ddf_change, badmode, diw_change;
static int bplcon1_hpos;

/* The display and data fetch windows */

enum diw_states
{
	DIW_waiting_start, DIW_waiting_stop
};

static int plffirstline, plflastline;
int plffirstline_total, plflastline_total;
static int autoscale_bordercolors;
static int plfstrt_start, plfstrt, plfstop;
static int sprite_minx, sprite_maxx;
static int first_bpl_vpos;
static int last_ddf_pix_hpos;
static int last_decide_line_hpos;
static int last_fetch_hpos, last_sprite_hpos;
static int diwfirstword, diwlastword;
static int plfleft_real;
static int last_hdiw;
static enum diw_states diwstate, hdiwstate, ddfstate;
int first_planes_vpos, last_planes_vpos;
int diwfirstword_total, diwlastword_total;
int ddffirstword_total, ddflastword_total;
int firstword_bplcon1;

static int last_copper_hpos;
static int copper_access;

/* Sprite collisions */
static unsigned int clxdat, clxcon, clxcon2, clxcon_bpl_enable, clxcon_bpl_match;

enum copper_states {
	COP_stop,
	COP_read1,
	COP_read2,
	COP_bltwait,
	COP_wait_in2,
	COP_skip_in2,
	COP_wait1,
	COP_wait,
	COP_skip1,
	COP_strobe_delay1,
	COP_strobe_delay2,
	COP_strobe_extra, // just to skip current cycle when CPU wrote to COPJMP
	COP_start_delay
};

struct copper {
	/* The current instruction words.  */
	unsigned int i1, i2;
	unsigned int saved_i1, saved_i2;
	enum copper_states state, state_prev;
	/* Instruction pointer.  */
	uaecptr ip, saved_ip;
	int hpos, vpos;
	unsigned int ignore_next;
	int vcmp, hcmp;

	int strobe; /* COPJMP1 / COPJMP2 accessed */
	int last_write, last_write_hpos;
	int moveaddr, movedata, movedelay;
};

static struct copper cop_state;
static int copper_enabled_thisline;
static int cop_min_waittime;

/*
* Statistics
*/
unsigned long int frametime = 0, lastframetime = 0, timeframes = 0;
unsigned long hsync_counter = 0, vsync_counter = 0;
unsigned long int idletime;
int bogusframe;

/* Recording of custom chip register changes.  */
static int current_change_set;
static struct sprite_entry sprite_entries[2][MAX_SPR_PIXELS / 16];
static struct color_change color_changes[2][MAX_REG_CHANGE];

struct decision line_decisions[2 * (MAXVPOS + 2) + 1];
static struct draw_info line_drawinfo[2][2 * (MAXVPOS + 2) + 1];
#define COLOR_TABLE_SIZE (MAXVPOS + 2) * 2
static struct color_entry color_tables[2][COLOR_TABLE_SIZE];

static int next_sprite_entry = 0;
static int prev_next_sprite_entry;
static int next_sprite_forced = 1;

struct sprite_entry *curr_sprite_entries, *prev_sprite_entries;
struct color_change *curr_color_changes, *prev_color_changes;
struct draw_info *curr_drawinfo, *prev_drawinfo;
struct color_entry *curr_color_tables, *prev_color_tables;

static int next_color_change;
static int next_color_entry, remembered_color_entry;
static int color_src_match, color_dest_match, color_compare_result;

static uae_u32 thisline_changed;

#ifdef SMART_UPDATE
#define MARK_LINE_CHANGED do { thisline_changed = 1; } while (0)
#else
#define MARK_LINE_CHANGED do { ; } while (0)
#endif

static struct decision thisline_decision;
static int fetch_cycle, fetch_modulo_cycle;

enum plfstate
{
	plf_idle,
	plf_start,
	plf_active,
	plf_passed_stop,
	plf_passed_stop2,
	plf_end
} plf_state;

enum fetchstate {
	fetch_not_started,
	fetch_started,
	fetch_was_plane0
} fetch_state;

/*
* helper functions
*/

STATIC_INLINE int ecsshres(void)
{
	return bplcon0_res == RES_SUPERHIRES && (currprefs.chipset_mask & CSMASK_ECS_DENISE) && !(currprefs.chipset_mask & CSMASK_AGA);
}

STATIC_INLINE int nodraw (void)
{
	return !currprefs.cpu_cycle_exact && framecnt != 0;
}

static int doflickerfix (void)
{
	return currprefs.gfx_vresolution && doublescan < 0 && vpos < MAXVPOS;
}

uae_u32 get_copper_address (int copno)
{
	switch (copno) {
	case 1: return cop1lc;
	case 2: return cop2lc;
	case -1: return cop_state.ip;
	default: return 0;
	}
}

int rpt_available = 0;

void reset_frame_rate_hack (void)
{
	if (currprefs.m68k_speed != -1)
		return;

	if (! rpt_available) {
		currprefs.m68k_speed = 0;
		return;
	}

	rpt_did_reset = 1;
	is_lastline = 0;
	vsyncmintime = read_processor_time () + vsynctime;
	write_log (_T("Resetting frame rate hack\n"));
}

STATIC_INLINE void setclr (uae_u16 *p, uae_u16 val)
{
	if (val & 0x8000)
		*p |= val & 0x7FFF;
	else
		*p &= ~val;
}

STATIC_INLINE void alloc_cycle (int hpos, int type)
{
#ifdef CPUEMU_12
#if 0
	if (cycle_line[hpos])
		write_log (_T("hpos=%d, old=%d, new=%d\n"), hpos, cycle_line[hpos], type);
	if ((type == CYCLE_CPU || type == CYCLE_COPPER) && (hpos & 1))
		write_log (_T("odd %d cycle %d\n"), hpos);
	if (!(hpos & 1) && (type == CYCLE_SPRITE || type == CYCLE_REFRESH || type == CYCLE_MISC))
		write_log (_T("even %d cycle %d\n"), type, hpos);
#endif
	cycle_line[hpos] = type;
#endif
}
STATIC_INLINE void alloc_cycle_maybe (int hpos, int type)
{
	if (cycle_line[hpos] == 0)
		alloc_cycle (hpos, type);
}

void alloc_cycle_ext(int hpos, int type)
{
	alloc_cycle (hpos, type);
}

static void hsyncdelay (void)
{
#if 0
	static int prevhpos;
	while (current_hpos () == prevhpos)
		do_cycles(CYCLE_UNIT);
	prevhpos = current_hpos();
#endif
}

static void update_mirrors (void)
{
	aga_mode = (currprefs.chipset_mask & CSMASK_AGA) ? 1 : 0;
	direct_rgb = aga_mode;
}

STATIC_INLINE uae_u8 *pfield_xlateptr (uaecptr plpt, int bytecount)
{
	if (!chipmem_check_indirect (plpt, bytecount)) {
		static int count = 0;
		if (!count)
			count++, write_log (_T("Warning: Bad playfield pointer\n"));
		return NULL;
	}
	return chipmem_xlate_indirect (plpt);
}

STATIC_INLINE void docols (struct color_entry *colentry)
{
	int i;

#ifdef AGA
	if (currprefs.chipset_mask & CSMASK_AGA) {
		for (i = 0; i < 256; i++) {
			int v = color_reg_get (colentry, i);
			if (v < 0 || v > 16777215)
				continue;
			colentry->acolors[i] = getxcolor (v);
		}
	} else {
#endif
		for (i = 0; i < 32; i++) {
			int v = color_reg_get (colentry, i);
			if (v < 0 || v > 4095)
				continue;
			colentry->acolors[i] = getxcolor (v);
		}
#ifdef AGA
	}
#endif
}

extern struct color_entry colors_for_drawing;

void notice_new_xcolors (void)
{
	int i;

	update_mirrors ();
	docols (&current_colors);
	docols (&colors_for_drawing);
	for (i = 0; i < (MAXVPOS + 1) * 2; i++) {
		docols (color_tables[0] + i);
		docols (color_tables[1] + i);
	}
}

static void do_sprites (int currhp);

static void remember_ctable (void)
{
	/* This can happen when program program crashes very badly */
	if (next_color_entry >= COLOR_TABLE_SIZE)
		return;
	if (remembered_color_entry == -1) {
		/* The colors changed since we last recorded a color map. Record a
		* new one. */
		color_reg_cpy (curr_color_tables + next_color_entry, &current_colors);
		remembered_color_entry = next_color_entry++;
	}
	thisline_decision.ctable = remembered_color_entry;
	if (color_src_match == -1 || color_dest_match != remembered_color_entry
		|| line_decisions[next_lineno].ctable != color_src_match)
	{
		/* The remembered comparison didn't help us - need to compare again. */
		int oldctable = line_decisions[next_lineno].ctable;
		int changed = 0;

		if (oldctable == -1) {
			changed = 1;
			color_src_match = color_dest_match = -1;
		} else {
			color_compare_result = color_reg_cmp (&prev_color_tables[oldctable], &current_colors) != 0;
			if (color_compare_result)
				changed = 1;
			color_src_match = oldctable;
			color_dest_match = remembered_color_entry;
		}
		thisline_changed |= changed;
	} else {
		/* We know the result of the comparison */
		if (color_compare_result)
			thisline_changed = 1;
	}
}

static void remember_ctable_for_border (void)
{
	remember_ctable ();
}

/* Called to determine the state of the horizontal display window state
* machine at the current position. It might have changed since we last
* checked.  */
static void decide_diw (int hpos)
{
	/* Last hpos = hpos + 0.5, eg. normal PAL end hpos is 227.5 * 2 = 455
	   OCS Denise: 9 bit hdiw counter does not reset during lines 0 to 9
	   (PAL) or lines 0 to 10 (NTSC). A1000 PAL: 1 to 9, NTSC: 1 to 10.
	   ECS Denise and AGA: no above "features"
	*/

	int hdiw = hpos >= maxhpos ? maxhpos * 2 + 1 : hpos * 2 + 2;
	if (!(currprefs.chipset_mask & CSMASK_ECS_DENISE) && vpos <= equ_vblank_endline) {
		hdiw = diw_hcounter;
		hdiw &= 511;
	}
	for (;;) {
		int lhdiw = hdiw;
		if (last_hdiw > lhdiw)
			lhdiw = 512;

		if (lhdiw >= diw_hstrt && last_hdiw < diw_hstrt && hdiwstate == DIW_waiting_start) {
			if (thisline_decision.diwfirstword == -1)
				thisline_decision.diwfirstword = diwfirstword < 0 ? 0 : diwfirstword;
			hdiwstate = DIW_waiting_stop;
		}
		if (lhdiw >= diw_hstop && last_hdiw < diw_hstop && hdiwstate == DIW_waiting_stop) {
			if (thisline_decision.diwlastword == -1)
				thisline_decision.diwlastword = diwlastword < 0 ? 0 : diwlastword;
			hdiwstate = DIW_waiting_start;
		}
		if (lhdiw != 512)
			break;
		last_hdiw = 0 - 1;
	}
	last_hdiw = hdiw;
}

static int fetchmode;
static int real_bitplane_number[3][3][9];

/* Disable bitplane DMA if planes > available DMA slots. This is needed
e.g. by the Sanity WOC demo (at the "Party Effect").  */
STATIC_INLINE int GET_PLANES_LIMIT (uae_u16 bc0)
{
	int res = GET_RES_AGNUS (bc0);
	int planes = GET_PLANES (bc0);
	return real_bitplane_number[fetchmode][res][planes];
}

/* The HRM says 0xD8, but that can't work... */
#define HARD_DDF_STOP 0xd4
#define HARD_DDF_START 0x18

static void add_modulos (void)
{
	int m1, m2;

	if (fmode & 0x4000) {
		if (((diwstrt >> 8) ^ vpos) & 1)
			m1 = m2 = bpl2mod;
		else
			m1 = m2 = bpl1mod;
	} else {
		m1 = bpl1mod;
		m2 = bpl2mod;
	}

	switch (bplcon0_planes_limit) {
#ifdef AGA
	case 8: bplpt[7] += m2; bplptx[7] += m2;
	case 7: bplpt[6] += m1; bplptx[6] += m1;
#endif
	case 6: bplpt[5] += m2; bplptx[5] += m2;
	case 5: bplpt[4] += m1; bplptx[4] += m1;
	case 4: bplpt[3] += m2; bplptx[3] += m2;
	case 3: bplpt[2] += m1; bplptx[2] += m1;
	case 2: bplpt[1] += m2; bplptx[1] += m2;
	case 1: bplpt[0] += m1; bplptx[0] += m1;
	}
}

static void finish_playfield_line (void)
{
	/* The latter condition might be able to happen in interlaced frames. */
	if (vpos >= minfirstline && (thisframe_first_drawn_line == -1 || vpos < thisframe_first_drawn_line))
		thisframe_first_drawn_line = vpos;
	thisframe_last_drawn_line = vpos;

#ifdef SMART_UPDATE
	if (line_decisions[next_lineno].plflinelen != thisline_decision.plflinelen
		|| line_decisions[next_lineno].plfleft != thisline_decision.plfleft
		|| line_decisions[next_lineno].bplcon0 != thisline_decision.bplcon0
		|| line_decisions[next_lineno].bplcon2 != thisline_decision.bplcon2
#ifdef ECS_DENISE
		|| line_decisions[next_lineno].bplcon3 != thisline_decision.bplcon3
#endif
#ifdef AGA
		|| line_decisions[next_lineno].bplcon4 != thisline_decision.bplcon4
#endif
		)
#endif /* SMART_UPDATE */
		thisline_changed = 1;
}

/* The fetch unit mainly controls ddf stop.  It's the number of cycles that
are contained in an indivisible block during which ddf is active.  E.g.
if DDF starts at 0x30, and fetchunit is 8, then possible DDF stops are
0x30 + n * 8.  */
static int fetchunit, fetchunit_mask;
/* The delay before fetching the same bitplane again.  Can be larger than
the number of bitplanes; in that case there are additional empty cycles
with no data fetch (this happens for high fetchmodes and low
resolutions).  */
static int fetchstart, fetchstart_shift, fetchstart_mask;
/* fm_maxplane holds the maximum number of planes possible with the current
fetch mode.  This selects the cycle diagram:
8 planes: 73516240
4 planes: 3120
2 planes: 10.  */
static int fm_maxplane, fm_maxplane_shift;

/* The corresponding values, by fetchmode and display resolution.  */
static const int fetchunits[] = { 8,8,8,0, 16,8,8,0, 32,16,8,0 };
static const int fetchstarts[] = { 3,2,1,0, 4,3,2,0, 5,4,3,0 };
static const int fm_maxplanes[] = { 3,2,1,0, 3,3,2,0, 3,3,3,0 };

static int cycle_diagram_table[3][3][9][32];
static int cycle_diagram_free_cycles[3][3][9];
static int cycle_diagram_total_cycles[3][3][9];
static int *curr_diagram;
static const int cycle_sequences[3 * 8] = { 2,1,2,1,2,1,2,1, 4,2,3,1,4,2,3,1, 8,4,6,2,7,3,5,1 };

static void debug_cycle_diagram (void)
{
	int fm, res, planes, cycle, v;
	TCHAR aa;

	for (fm = 0; fm <= 2; fm++) {
		write_log (_T("FMODE %d\n=======\n"), fm);
		for (res = 0; res <= 2; res++) {
			for (planes = 0; planes <= 8; planes++) {
				write_log (_T("%d: "),planes);
				for (cycle = 0; cycle < 32; cycle++) {
					v=cycle_diagram_table[fm][res][planes][cycle];
					if (v==0) aa='-'; else if(v>0) aa='1'; else aa='X';
					write_log (_T("%c"),aa);
				}
				write_log (_T(" %d:%d\n"),
					cycle_diagram_free_cycles[fm][res][planes], cycle_diagram_total_cycles[fm][res][planes]);
			}
			write_log (_T("\n"));
		}
	}
	fm=0;
}

static void create_cycle_diagram_table (void)
{
	int fm, res, cycle, planes, rplanes, v;
	int fetch_start, max_planes, freecycles;
	const int *cycle_sequence;

	for (fm = 0; fm <= 2; fm++) {
		for (res = 0; res <= 2; res++) {
			max_planes = fm_maxplanes[fm * 4 + res];
			fetch_start = 1 << fetchstarts[fm * 4 + res];
			cycle_sequence = &cycle_sequences[(max_planes - 1) * 8];
			max_planes = 1 << max_planes;
			for (planes = 0; planes <= 8; planes++) {
				freecycles = 0;
				for (cycle = 0; cycle < 32; cycle++)
					cycle_diagram_table[fm][res][planes][cycle] = -1;
				if (planes <= max_planes) {
					for (cycle = 0; cycle < fetch_start; cycle++) {
						if (cycle < max_planes && planes >= cycle_sequence[cycle & 7]) {
							v = cycle_sequence[cycle & 7];
						} else {
							v = 0;
							freecycles++;
						}
						cycle_diagram_table[fm][res][planes][cycle] = v;
					}
				}
				cycle_diagram_free_cycles[fm][res][planes] = freecycles;
				cycle_diagram_total_cycles[fm][res][planes] = fetch_start;
				rplanes = planes;
				if (rplanes > max_planes)
					rplanes = 0;
				if (rplanes == 7 && fm == 0 && res == 0 && !(currprefs.chipset_mask & CSMASK_AGA))
					rplanes = 4;
				real_bitplane_number[fm][res][planes] = rplanes;
			}
		}
	}
#if 0
	debug_cycle_diagram ();
#endif
}


/* Used by the copper.  */
static int estimated_last_fetch_cycle;
static int cycle_diagram_shift;

static void estimate_last_fetch_cycle (int hpos)
{
	int fetchunit = fetchunits[fetchmode * 4 + bplcon0_res];

	if (plf_state < plf_passed_stop) {
		int stop = plfstop < hpos || plfstop > HARD_DDF_STOP ? HARD_DDF_STOP : plfstop;
		/* We know that fetching is up-to-date up until hpos, so we can use fetch_cycle.  */
		int fetch_cycle_at_stop = fetch_cycle + (stop - hpos);
		int starting_last_block_at = (fetch_cycle_at_stop + fetchunit - 1) & ~(fetchunit - 1);

		estimated_last_fetch_cycle = hpos + (starting_last_block_at - fetch_cycle) + fetchunit;
	} else {
		int starting_last_block_at = (fetch_cycle + fetchunit - 1) & ~(fetchunit - 1);
		if (plf_state == plf_passed_stop2)
			starting_last_block_at -= fetchunit;

		estimated_last_fetch_cycle = hpos + (starting_last_block_at - fetch_cycle) + fetchunit;
	}
}

static uae_u32 outword[MAX_PLANES];
static int out_nbits, out_offs;
static uae_u32 todisplay[MAX_PLANES][4];
static uae_u32 fetched[MAX_PLANES];
#ifdef AGA
static uae_u32 fetched_aga0[MAX_PLANES];
static uae_u32 fetched_aga1[MAX_PLANES];
#endif

/* Expansions from bplcon0/bplcon1.  */
static int toscr_res, toscr_nr_planes, toscr_nr_planes2, fetchwidth;
static int toscr_delay1, toscr_delay2;

/* The number of bits left from the last fetched words.
This is an optimization - conceptually, we have to make sure the result is
the same as if toscr is called in each clock cycle.  However, to speed this
up, we accumulate display data; this variable keeps track of how much.
Thus, once we do call toscr_nbits (which happens at least every 16 bits),
we can do more work at once.  */
static int toscr_nbits;

/* undocumented bitplane delay hardware feature */
static int delayoffset;

STATIC_INLINE void compute_delay_offset (void)
{
	delayoffset = (16 << fetchmode) - (((plfstrt - HARD_DDF_START) & fetchstart_mask) << 1);
#if 0
	/* maybe we can finally get rid of this stupid table.. */
	if (tmp == 4)
		delayoffset = 4; // Loons Docs
	else if (tmp == 8)
		delayoffset = 8;
	else if (tmp == 12) // Loons Docs
		delayoffset = 4;
	else if (tmp == 16) /* Overkill AGA */
		delayoffset = 48;
	else if (tmp == 24) /* AB 2 */
		delayoffset = 8;
	else if (tmp == 32)
		delayoffset = 32;
	else if (tmp == 48) /* Pinball Illusions AGA, ingame */
		delayoffset = 16;
	else /* what about 40 and 56? */
		delayoffset = 0;
	//write_log (_T("%d:%d "), vpos, delayoffset);
#endif
}

static void record_color_change2 (int hpos, int regno, unsigned long value)
{
	int pos = hpos * 2;
	if (regno == 0x1000 + 0x10c)
		pos++; // BPLCON4 change needs 1 lores pixel delay
	curr_color_changes[next_color_change].linepos = pos;
	curr_color_changes[next_color_change].regno = regno;
	curr_color_changes[next_color_change++].value = value;
	curr_color_changes[next_color_change].regno = -1;
}

static int isehb (uae_u16 bplcon0, uae_u16 bplcon2)
{
	int bplehb;
	if (currprefs.chipset_mask & CSMASK_AGA)
		bplehb = (bplcon0 & 0x7010) == 0x6000;
	else if (currprefs.chipset_mask & CSMASK_ECS_DENISE)
		bplehb = ((bplcon0 & 0xFC00) == 0x6000 || (bplcon0 & 0xFC00) == 0x7000);
	else
		bplehb = ((bplcon0 & 0xFC00) == 0x6000 || (bplcon0 & 0xFC00) == 0x7000) && !currprefs.cs_denisenoehb;
	return bplehb;
}

// OCS/ECS, lores, 7 planes = 4 "real" planes + BPL5DAT and BPL6DAT as static 5th and 6th plane
STATIC_INLINE int isocs7planes (void)
{
	return !(currprefs.chipset_mask & CSMASK_AGA) && bplcon0_res == 0 && bplcon0_planes == 7;
}

int is_bitplane_dma (int hpos)
{
	if (fetch_state == fetch_not_started || hpos < plfstrt)
		return 0;
	if ((plf_state == plf_end && hpos >= thisline_decision.plfright)
		|| hpos >= estimated_last_fetch_cycle)
		return 0;
	return curr_diagram[(hpos - cycle_diagram_shift) & fetchstart_mask];
}

STATIC_INLINE int is_bitplane_dma_inline (int hpos)
{
	if (fetch_state == fetch_not_started || hpos < plfstrt)
		return 0;
	if ((plf_state == plf_end && hpos >= thisline_decision.plfright)
		|| hpos >= estimated_last_fetch_cycle)
		return 0;
	return curr_diagram[(hpos - cycle_diagram_shift) & fetchstart_mask];
}

static void update_denise (int hpos)
{
	toscr_res = GET_RES_DENISE (bplcon0d);
	if (bplcon0dd != bplcon0d) {
		record_color_change2 (hpos, 0x100 + 0x1000, bplcon0d);
		bplcon0dd = bplcon0d;
	}
	toscr_nr_planes = GET_PLANES (bplcon0d);
	if (isocs7planes ()) {
		if (toscr_nr_planes2 < 6)
			toscr_nr_planes2 = 6;
	} else {
		toscr_nr_planes2 = toscr_nr_planes;
	}
}

static int bpldmasetuphpos;
static int bpldmasetupphase;

/* set currently active Agnus bitplane DMA sequence */
static void setup_fmodes (int hpos)
{
	switch (fmode & 3)
	{
	case 0:
		fetchmode = 0;
		break;
	case 1:
	case 2:
		fetchmode = 1;
		break;
	case 3:
		fetchmode = 2;
		break;
	}
	badmode = GET_RES_AGNUS (bplcon0) != GET_RES_DENISE (bplcon0);
	bplcon0_res = GET_RES_AGNUS (bplcon0);
	bplcon0_planes = GET_PLANES (bplcon0);
	bplcon0_planes_limit = GET_PLANES_LIMIT (bplcon0);
	fetchunit = fetchunits[fetchmode * 4 + bplcon0_res];
	fetchunit_mask = fetchunit - 1;
	fetchstart_shift = fetchstarts[fetchmode * 4 + bplcon0_res];
	fetchstart = 1 << fetchstart_shift;
	fetchstart_mask = fetchstart - 1;
	fm_maxplane_shift = fm_maxplanes[fetchmode * 4 + bplcon0_res];
	fm_maxplane = 1 << fm_maxplane_shift;
	fetch_modulo_cycle = fetchunit - fetchstart;
	if (is_bitplane_dma (hpos - 1))
		cycle_line[hpos - 1] = 1;
	curr_diagram = cycle_diagram_table[fetchmode][bplcon0_res][bplcon0_planes_limit];
	estimate_last_fetch_cycle (hpos);
	if (bpldmasetuphpos >= 0 && debug_dma)
		record_dma_event (DMA_EVENT_BPLFETCHUPDATE, hpos, vpos);
	bpldmasetuphpos = -1;
	bpldmasetupphase = 0;
	ddf_change = vpos;
}

static void BPLCON0_Denise (int hpos, uae_u16 v);

// writing to BPLCON0 adds 4 cycle delay before Agnus bitplane DMA sequence changes
// (Note that Denise sees the change after 1 cycle)
// AGA needs extra cycle in some specific situations (Brian The Lion "dialog") but not
// in all situations (Superstardust weapon panel)
#define BPLCON_AGNUS_DELAY (4 + (bplcon0_planes == 8 ? 1 : 0))
#define BPLCON_DENISE_DELAY 1

static void maybe_setup_fmodes (int hpos)
{
	switch (bpldmasetupphase)
	{
	case 0:
		BPLCON0_Denise (hpos, bplcon0);
		bpldmasetupphase++;
		bpldmasetuphpos += BPLCON_AGNUS_DELAY - BPLCON_DENISE_DELAY;
		break;
	case 1:
		setup_fmodes (hpos);
		break;
	}
}

STATIC_INLINE void maybe_check (int hpos)
{
	if (bpldmasetuphpos > 0 && hpos >= bpldmasetuphpos)
		maybe_setup_fmodes (hpos);
}

static void bpldmainitdelay (int hpos)
{
	int hposa;

	hposa = hpos + BPLCON_AGNUS_DELAY;
	ddf_change = vpos;
	if (hposa < 0x14) {
		BPLCON0_Denise (hpos, bplcon0);
		setup_fmodes (hpos);
		return;
	}
	if (bpldmasetuphpos < 0) {
		bpldmasetupphase = 0;
		bpldmasetuphpos = hpos + BPLCON_DENISE_DELAY;
	}
}

/* Expand bplcon0/bplcon1 into the toscr_xxx variables.  */
static void compute_toscr_delay_1 (int bplcon1)
{
	int delay1 = (bplcon1 & 0x0f) | ((bplcon1 & 0x0c00) >> 6);
	int delay2 = ((bplcon1 >> 4) & 0x0f) | (((bplcon1 >> 4) & 0x0c00) >> 6);
	int shdelay1 = (bplcon1 >> 12) & 3;
	int shdelay2 = (bplcon1 >> 8) & 3;
	int delaymask;
	int fetchwidth = 16 << fetchmode;

	delay1 += delayoffset;
	delay2 += delayoffset;
	delaymask = (fetchwidth - 1) >> toscr_res;
	toscr_delay1 = (delay1 & delaymask) << toscr_res;
	toscr_delay1 |= shdelay1 >> (RES_MAX - toscr_res);
	toscr_delay2 = (delay2 & delaymask) << toscr_res;
	toscr_delay2 |= shdelay2 >> (RES_MAX - toscr_res);
}

static void compute_toscr_delay (int hpos, int bplcon1)
{
	update_denise (hpos);
	compute_toscr_delay_1 (bplcon1);
}

STATIC_INLINE void clear_fetchbuffer (uae_u32 *ptr, int nwords)
{
	int i;

	if (! thisline_changed) {
		for (i = 0; i < nwords; i++) {
			if (ptr[i]) {
				thisline_changed = 1;
				break;
			}
		}
	}
	memset (ptr, 0, nwords * 4);
}

static void update_toscr_planes (void)
{
	if (toscr_nr_planes2 > thisline_decision.nr_planes) {
		int j;
		for (j = thisline_decision.nr_planes; j < toscr_nr_planes2; j++)
			clear_fetchbuffer ((uae_u32 *)(line_data[next_lineno] + 2 * MAX_WORDS_PER_LINE * j), out_offs);
		thisline_decision.nr_planes = toscr_nr_planes2;
	}
}

STATIC_INLINE void maybe_first_bpl1dat (int hpos)
{
	if (thisline_decision.plfleft != -1) {
		// early bpl1dat crap fix (Sequential engine animation)
		if (plfleft_real == -1) {
			int i;
			for (i = 0; i < thisline_decision.nr_planes; i++) {
				todisplay[i][0] = 0;
#ifdef AGA
				todisplay[i][1] = 0;
				todisplay[i][2] = 0;
				todisplay[i][3] = 0;
#endif
			}
			plfleft_real = hpos;
			bpl1dat_early = 1;
		}
	} else {
		plfleft_real = thisline_decision.plfleft = hpos;
		compute_delay_offset ();
	}
}

STATIC_INLINE void fetch (int nr, int fm, int hpos)
{
	if (nr < bplcon0_planes_limit) {
		uaecptr p = bplpt[nr];
		bplpt[nr] += 2 << fm;
		bplptx[nr] += 2 << fm;
		if (nr == 0)
			bpl1dat_written = 1;
#ifdef DEBUGGER
		if (debug_dma)
			record_dma (0x110 + nr * 2, chipmem_wget_indirect (p), p, hpos, vpos, DMARECORD_BITPLANE);
#endif
		switch (fm)
		{
		case 0:
			fetched[nr] = bplxdat[nr] = last_custom_value1 = chipmem_wget_indirect (p);
			break;
#ifdef AGA
		case 1:
			fetched_aga0[nr] = chipmem_lget_indirect (p);
			last_custom_value1 = (uae_u16)fetched_aga0[nr];
			break;
		case 2:
			fetched_aga1[nr] = chipmem_lget_indirect (p);
			fetched_aga0[nr] = chipmem_lget_indirect (p + 4);
			last_custom_value1 = (uae_u16)fetched_aga0[nr];
			break;
#endif
		}
		if (plf_state == plf_passed_stop2 && fetch_cycle >= (fetch_cycle & ~fetchunit_mask) + fetch_modulo_cycle) {
			int mod;
			if (fmode & 0x4000) {
				if (((diwstrt >> 8) ^ vpos) & 1)
					mod = bpl2mod;
				else
					mod = bpl1mod;
			} else if (nr & 1)
				mod = bpl2mod;
			else
				mod = bpl1mod;
			bplpt[nr] += mod;
			bplptx[nr] += mod;
		}
	} else {
		// use whatever left in BPLxDAT if no DMA
		// normally useless but "7-planes" feature won't work without this
		fetched[nr] = bplxdat[nr];
	}
}

STATIC_INLINE void toscr_3_ecs (int nbits)
{
	int delay1 = toscr_delay1;
	int delay2 = toscr_delay2;
	int i;
	uae_u32 mask = 0xFFFF >> (16 - nbits);

	for (i = 0; i < toscr_nr_planes2; i += 2) {
		outword[i] <<= nbits;
		outword[i] |= (todisplay[i][0] >> (16 - nbits + delay1)) & mask;
		todisplay[i][0] <<= nbits;
	}
	for (i = 1; i < toscr_nr_planes2; i += 2) {
		outword[i] <<= nbits;
		outword[i] |= (todisplay[i][0] >> (16 - nbits + delay2)) & mask;
		todisplay[i][0] <<= nbits;
	}
}

STATIC_INLINE void shift32plus (uae_u32 *p, int n)
{
	uae_u32 t = p[1];
	t <<= n;
	t |= p[0] >> (32 - n);
	p[1] = t;
}

#ifdef AGA
STATIC_INLINE void aga_shift (uae_u32 *p, int n, int fm)
{
	if (fm == 2) {
		shift32plus (p + 2, n);
		shift32plus (p + 1, n);
	}
	shift32plus (p + 0, n);
	p[0] <<= n;
}

STATIC_INLINE void toscr_3_aga (int nbits, int fm)
{
	int delay1 = toscr_delay1;
	int delay2 = toscr_delay2;
	int i;
	uae_u32 mask = 0xFFFF >> (16 - nbits);

	{
		int offs = (16 << fm) - nbits + delay1;
		int off1 = offs >> 5;
		if (off1 == 3)
			off1 = 2;
		offs -= off1 * 32;
		for (i = 0; i < toscr_nr_planes2; i += 2) {
			uae_u32 t0 = todisplay[i][off1];
			uae_u32 t1 = todisplay[i][off1 + 1];
			uae_u64 t = (((uae_u64)t1) << 32) | t0;
			outword[i] <<= nbits;
			outword[i] |= (t >> offs) & mask;
			aga_shift (todisplay[i], nbits, fm);
		}
	}
	{
		int offs = (16 << fm) - nbits + delay2;
		int off1 = offs >> 5;
		if (off1 == 3)
			off1 = 2;
		offs -= off1 * 32;
		for (i = 1; i < toscr_nr_planes2; i += 2) {
			uae_u32 t0 = todisplay[i][off1];
			uae_u32 t1 = todisplay[i][off1 + 1];
			uae_u64 t = (((uae_u64)t1) << 32) | t0;
			outword[i] <<= nbits;
			outword[i] |= (t >> offs) & mask;
			aga_shift (todisplay[i], nbits, fm);
		}
	}
}

#endif

static void toscr_2_0 (int nbits) { toscr_3_ecs (nbits); }
#ifdef AGA
static void toscr_2_1 (int nbits) { toscr_3_aga (nbits, 1); }
static void toscr_2_2 (int nbits) { toscr_3_aga (nbits, 2); }
#endif

STATIC_INLINE void toscr_1 (int nbits, int fm)
{
	switch (fm) {
	case 0:
		toscr_2_0 (nbits);
		break;
#ifdef AGA
	case 1:
		toscr_2_1 (nbits);
		break;
	case 2:
		toscr_2_2 (nbits);
		break;
#endif
	}
	out_nbits += nbits;
	if (out_nbits == 32) {
		int i;
		uae_u8 *dataptr = line_data[next_lineno] + out_offs * 4;
		for (i = 0; i < thisline_decision.nr_planes; i++) {
			uae_u32 *dataptr32 = (uae_u32 *)dataptr;
			if (*dataptr32 != outword[i]) {
				thisline_changed = 1;
				*dataptr32 = outword[i];
			}
			outword[i] = 0;
			dataptr += MAX_WORDS_PER_LINE * 2;
		}
		out_offs++;
		out_nbits = 0;
	}
}

static void toscr_fm0 (int);
static void toscr_fm1 (int);
static void toscr_fm2 (int);

STATIC_INLINE void toscr (int nbits, int fm)
{
	switch (fm) {
	case 0: toscr_fm0 (nbits); break;
#ifdef AGA
	case 1: toscr_fm1 (nbits); break;
	case 2: toscr_fm2 (nbits); break;
#endif
	}
}

STATIC_INLINE void toscr_0 (int nbits, int fm)
{
	int t;

	if (nbits > 16) {
		toscr (16, fm);
		nbits -= 16;
	}

	t = 32 - out_nbits;
	if (t < nbits) {
		toscr_1 (t, fm);
		nbits -= t;
	}
	toscr_1 (nbits, fm);
}

static void toscr_fm0 (int nbits) { toscr_0 (nbits, 0); }
static void toscr_fm1 (int nbits) { toscr_0 (nbits, 1); }
static void toscr_fm2 (int nbits) { toscr_0 (nbits, 2); }

static int flush_plane_data (int fm)
{
	int i = 0;

	if (out_nbits <= 16) {
		i += 16;
		toscr_1 (16, fm);
	}
	if (out_nbits != 0) {
		i += 32 - out_nbits;
		toscr_1 (32 - out_nbits, fm);
	}
	i += 32;

	toscr_1 (16, fm);
	toscr_1 (16, fm);

	if (fm == 2) {
		/* flush AGA full 64-bit shift register */
		i += 32;
		toscr_1 (16, fm);
		toscr_1 (16, fm);
	}

	if (bpl1dat_early) {
		// clear possible crap in right border if
		// bpl1dat was written "out of sync"
		toscr_1 (16, fm);
		toscr_1 (16, fm);
	}

	return i >> (1 + toscr_res);
}

STATIC_INLINE void flush_display (int fm)
{
	if (toscr_nbits > 0 && thisline_decision.plfleft != -1)
		toscr (toscr_nbits, fm);
	toscr_nbits = 0;
}

STATIC_INLINE void fetch_start (int hpos)
{
	fetch_state = fetch_started;
}


/* Called when all planes have been fetched, i.e. when a new block
of data is available to be displayed.  The data in fetched[] is
moved into todisplay[].  */
STATIC_INLINE void beginning_of_plane_block (int hpos, int fm)
{
	int i;
	int oleft = thisline_decision.plfleft;
	static uae_u16 bplcon1t, bplcon1t2;

	flush_display (fm);

	if (fm == 0)
		for (i = 0; i < MAX_PLANES; i++) {
			todisplay[i][0] |= fetched[i];
		}
#ifdef AGA
	else
		for (i = 0; i < MAX_PLANES; i++) {
			if (fm == 2)
				todisplay[i][1] = fetched_aga1[i];
			todisplay[i][0] = fetched_aga0[i];
		}
#endif

		update_denise (hpos);
		maybe_first_bpl1dat (hpos);

		bplcon1t2 = bplcon1t;
		bplcon1t = bplcon1;
		// writing to BPLCON1 1 cycle after BPL1DAT access will
		// not (except first BPL1DAT write) affect the display
		// until next display block
		if (bplcon1_hpos != hpos || oleft < 0)
			bplcon1t2 = bplcon1t;
		compute_toscr_delay (hpos, bplcon1t2);
}

#ifdef SPEEDUP

/* The usual inlining tricks - don't touch unless you know what you are doing. */
STATIC_INLINE void long_fetch_ecs (int plane, int nwords, int weird_number_of_bits, int dma)
{
	uae_u16 *real_pt = (uae_u16 *)pfield_xlateptr (bplpt[plane], nwords * 2);
	int delay = (plane & 1) ? toscr_delay2 : toscr_delay1;
	int tmp_nbits = out_nbits;
	uae_u32 shiftbuffer = todisplay[plane][0];
	uae_u32 outval = outword[plane];
	uae_u32 fetchval = fetched[plane];
	uae_u32 *dataptr = (uae_u32 *)(line_data[next_lineno] + 2 * plane * MAX_WORDS_PER_LINE + 4 * out_offs);

	if (dma) {
		bplpt[plane] += nwords * 2;
		bplptx[plane] += nwords * 2;
	}

	if (real_pt == 0)
		/* @@@ Don't do this, fall back on chipmem_wget instead.  */
		return;

	while (nwords > 0) {
		int bits_left = 32 - tmp_nbits;
		uae_u32 t;

		shiftbuffer |= fetchval;

		t = (shiftbuffer >> delay) & 0xFFFF;

		if (weird_number_of_bits && bits_left < 16) {
			outval <<= bits_left;
			outval |= t >> (16 - bits_left);
			thisline_changed |= *dataptr ^ outval;
			*dataptr++ = outval;

			outval = t;
			tmp_nbits = 16 - bits_left;
			shiftbuffer <<= 16;
		} else {
			outval = (outval << 16) | t;
			shiftbuffer <<= 16;
			tmp_nbits += 16;
			if (tmp_nbits == 32) {
				thisline_changed |= *dataptr ^ outval;
				*dataptr++ = outval;
				tmp_nbits = 0;
			}
		}
		nwords--;
		if (dma) {
			fetchval = do_get_mem_word (real_pt);
			real_pt++;
		}
	}
	fetched[plane] = fetchval;
	todisplay[plane][0] = shiftbuffer;
	outword[plane] = outval;
}

#ifdef AGA
STATIC_INLINE void long_fetch_aga (int plane, int nwords, int weird_number_of_bits, int fm, int dma)
{
	uae_u32 *real_pt = (uae_u32 *)pfield_xlateptr (bplpt[plane], nwords * 2);
	int delay = (plane & 1) ? toscr_delay2 : toscr_delay1;
	int tmp_nbits = out_nbits;
	uae_u32 *shiftbuffer = todisplay[plane];
	uae_u32 outval = outword[plane];
	uae_u32 fetchval0 = fetched_aga0[plane];
	uae_u32 fetchval1 = fetched_aga1[plane];
	uae_u32 *dataptr = (uae_u32 *)(line_data[next_lineno] + 2 * plane * MAX_WORDS_PER_LINE + 4 * out_offs);
	int offs = (16 << fm) - 16 + delay;
	int off1 = offs >> 5;
	if (off1 == 3)
		off1 = 2;
	offs -= off1 * 32;

	if (dma) {
		bplpt[plane] += nwords * 2;
		bplptx[plane] += nwords * 2;
	}

	if (real_pt == 0)
		/* @@@ Don't do this, fall back on chipmem_wget instead.  */
		return;

	while (nwords > 0) {
		int i;

		shiftbuffer[0] = fetchval0;
		if (fm == 2)
			shiftbuffer[1] = fetchval1;

		for (i = 0; i < (1 << fm); i++) {
			int bits_left = 32 - tmp_nbits;

			uae_u32 t0 = shiftbuffer[off1];
			uae_u32 t1 = shiftbuffer[off1 + 1];
			uae_u64 t = (((uae_u64)t1) << 32) | t0;

			t0 = (uae_u32)((t >> offs) & 0xFFFF);

			if (weird_number_of_bits && bits_left < 16) {
				outval <<= bits_left;
				outval |= t0 >> (16 - bits_left);

				thisline_changed |= *dataptr ^ outval;
				*dataptr++ = outval;

				outval = t0;
				tmp_nbits = 16 - bits_left;
				aga_shift (shiftbuffer, 16, fm);
			} else {
				outval = (outval << 16) | t0;
				aga_shift (shiftbuffer, 16, fm);
				tmp_nbits += 16;
				if (tmp_nbits == 32) {
					thisline_changed |= *dataptr ^ outval;
					*dataptr++ = outval;
					tmp_nbits = 0;
				}
			}
		}

		nwords -= 1 << fm;

		if (dma) {
			if (fm == 1)
				fetchval0 = do_get_mem_long (real_pt);
			else {
				fetchval1 = do_get_mem_long (real_pt);
				fetchval0 = do_get_mem_long (real_pt + 1);
			}
			real_pt += fm;
		}
	}
	fetched_aga0[plane] = fetchval0;
	fetched_aga1[plane] = fetchval1;
	outword[plane] = outval;
}
#endif

static void long_fetch_ecs_0 (int hpos, int nwords, int dma) { long_fetch_ecs (hpos, nwords, 0, dma); }
static void long_fetch_ecs_1 (int hpos, int nwords, int dma) { long_fetch_ecs (hpos, nwords, 1, dma); }
#ifdef AGA
static void long_fetch_aga_1_0 (int hpos, int nwords, int dma) { long_fetch_aga (hpos, nwords, 0, 1, dma); }
static void long_fetch_aga_1_1 (int hpos, int nwords, int dma) { long_fetch_aga (hpos, nwords, 1, 1, dma); }
static void long_fetch_aga_2_0 (int hpos, int nwords, int dma) { long_fetch_aga (hpos, nwords, 0, 2, dma); }
static void long_fetch_aga_2_1 (int hpos, int nwords, int dma) { long_fetch_aga (hpos, nwords, 1, 2, dma); }
#endif

static void do_long_fetch (int hpos, int nwords, int dma, int fm)
{
	int i;

	flush_display (fm);
	switch (fm) {
	case 0:
		if (out_nbits & 15) {
			for (i = 0; i < toscr_nr_planes; i++)
				long_fetch_ecs_1 (i, nwords, dma);
		} else {
			for (i = 0; i < toscr_nr_planes; i++)
				long_fetch_ecs_0 (i, nwords, dma);
		}
		break;
#ifdef AGA
	case 1:
		if (out_nbits & 15) {
			for (i = 0; i < toscr_nr_planes; i++)
				long_fetch_aga_1_1 (i, nwords, dma);
		} else {
			for (i = 0; i < toscr_nr_planes; i++)
				long_fetch_aga_1_0 (i, nwords, dma);
		}
		break;
	case 2:
		if (out_nbits & 15) {
			for (i = 0; i < toscr_nr_planes; i++)
				long_fetch_aga_2_1 (i, nwords, dma);
		} else {
			for (i = 0; i < toscr_nr_planes; i++)
				long_fetch_aga_2_0 (i, nwords, dma);
		}
		break;
#endif
	}

	out_nbits += nwords * 16;
	out_offs += out_nbits >> 5;
	out_nbits &= 31;

	if (dma && toscr_nr_planes > 0)
		fetch_state = fetch_was_plane0;
}

#endif

/* make sure fetch that goes beyond maxhpos is finished */
static void finish_final_fetch (int pos, int fm)
{
	if (thisline_decision.plfleft == -1)
		return;
	if (plf_state == plf_end)
		return;
	plf_state = plf_end;
	ddfstate = DIW_waiting_start;
	pos += flush_plane_data (fm);
	thisline_decision.plfright = pos;
	thisline_decision.plflinelen = out_offs;
	finish_playfield_line ();
}

STATIC_INLINE int one_fetch_cycle_0 (int pos, int ddfstop_to_test, int dma, int fm)
{
	if (plf_state < plf_passed_stop && pos == ddfstop_to_test)
		plf_state = plf_passed_stop;

	if ((fetch_cycle & fetchunit_mask) == 0) {
		if (plf_state == plf_passed_stop2) {
			finish_final_fetch (pos, fm);
			return 1;
		}
		if (plf_state == plf_passed_stop) {
			plf_state = plf_passed_stop2;
		} else if (plf_state == plf_passed_stop2) {
			plf_state = plf_end;
		}

	}

	maybe_check (pos);

	if (dma) {
		/* fetchstart_mask can be larger than fm_maxplane if FMODE > 0.  This means
		that the remaining cycles are idle; we'll fall through the whole switch
		without doing anything.  */
		int cycle_start = fetch_cycle & fetchstart_mask;
		switch (fm_maxplane) {
		case 8:
			switch (cycle_start) {
			case 0: fetch (7, fm, pos); break;
			case 1: fetch (3, fm, pos); break;
			case 2: fetch (5, fm, pos); break;
			case 3: fetch (1, fm, pos); break;
			case 4: fetch (6, fm, pos); break;
			case 5: fetch (2, fm, pos); break;
			case 6: fetch (4, fm, pos); break;
			case 7: fetch (0, fm, pos); break;
			}
			break;
		case 4:
			switch (cycle_start) {
			case 0: fetch (3, fm, pos); break;
			case 1: fetch (1, fm, pos); break;
			case 2: fetch (2, fm, pos); break;
			case 3: fetch (0, fm, pos); break;
			}
			break;
		case 2:
			switch (cycle_start) {
			case 0: fetch (1, fm, pos); break;
			case 1: fetch (0, fm, pos); break;
			}
			break;
		}
	}

	if (bpl1dat_written) {
		// do this here because if program plays with BPLCON0 during scanline
		// it is possible that one DMA BPL1DAT write is completely missed
		// and we must not draw anything at all in next dma block if this happens
		// (Disposable Hero titlescreen)
		fetch_state = fetch_was_plane0;
		bpl1dat_written = 0;
	}

	fetch_cycle++;
	toscr_nbits += 2 << toscr_res;

	if (toscr_nbits > 16) {
		uae_abort (_T("toscr_nbits > 16 (%d)"), toscr_nbits);
		toscr_nbits = 0;
	}
	if (toscr_nbits == 16)
		flush_display (fm);

	return 0;
}

static int one_fetch_cycle_fm0 (int pos, int ddfstop_to_test, int dma) { return one_fetch_cycle_0 (pos, ddfstop_to_test, dma, 0); }
static int one_fetch_cycle_fm1 (int pos, int ddfstop_to_test, int dma) { return one_fetch_cycle_0 (pos, ddfstop_to_test, dma, 1); }
static int one_fetch_cycle_fm2 (int pos, int ddfstop_to_test, int dma) { return one_fetch_cycle_0 (pos, ddfstop_to_test, dma, 2); }

STATIC_INLINE int one_fetch_cycle (int pos, int ddfstop_to_test, int dma, int fm)
{
	switch (fm) {
	case 0: return one_fetch_cycle_fm0 (pos, ddfstop_to_test, dma);
#ifdef AGA
	case 1: return one_fetch_cycle_fm1 (pos, ddfstop_to_test, dma);
	case 2: return one_fetch_cycle_fm2 (pos, ddfstop_to_test, dma);
#endif
	default: uae_abort (_T("fm corrupt")); return 0;
	}
}

static void update_fetch_x (int hpos, int fm)
{
	int pos;

	if (nodraw ())
		return;

	if (hpos < 0x18)
		return;
	pos = last_fetch_hpos;
	if (pos < 0x18)
		pos = 0x18;
	update_toscr_planes ();
	for (int i = 0; i < 8; i++) {
		fetched[i] = bplxdat[i];
		bplxdat[i] = 0;
	}
	beginning_of_plane_block (hpos, fm);
	for (; pos < hpos; pos++) {

		toscr_nbits += 2 << toscr_res;

		if (toscr_nbits > 16) {
			uae_abort (_T("toscr_nbits > 16 (%d)"), toscr_nbits);
			toscr_nbits = 0;
		}
		if (toscr_nbits == 16)
			flush_display (fm);

	}
	flush_display (fm);
	bpl1dat_written = 0;
}

STATIC_INLINE void update_fetch (int until, int fm)
{
	int pos;
	int dma = dmaen (DMA_BITPLANE);

	int ddfstop_to_test;

	if (nodraw () || plf_state == plf_end)
		return;

	/* We need an explicit test against HARD_DDF_STOP here to guard against
	programs that move the DDFSTOP before our current position before we
	reach it.  */
	ddfstop_to_test = HARD_DDF_STOP;
	if (ddfstop >= last_fetch_hpos && plfstop < ddfstop_to_test)
		ddfstop_to_test = plfstop;

	update_toscr_planes ();

	pos = last_fetch_hpos;
	cycle_diagram_shift = last_fetch_hpos - fetch_cycle;

	/* First, a loop that prepares us for the speedup code.  We want to enter
	the SPEEDUP case with fetch_state == fetch_was_plane0, and then unroll
	whole blocks, so that we end on the same fetch_state again.  */
	for (; ; pos++) {
		if (pos == until) {
			if (until >= maxhpos) {
				finish_final_fetch (pos, fm);
				return;
			}
			flush_display (fm);
			return;
		}

		if (fetch_state == fetch_was_plane0)
			break;

		fetch_start (pos);
		if (one_fetch_cycle (pos, ddfstop_to_test, dma, fm))
			return;
	}

#ifdef SPEEDUP
	/* Unrolled version of the for loop below.  */
	if (plf_state < plf_passed_stop && ddf_change != vpos && ddf_change + 1 != vpos
		&& dma
		&& (fetch_cycle & fetchstart_mask) == (fm_maxplane & fetchstart_mask)
		&& !badmode && !debug_dma
# if 0
		/* @@@ We handle this case, but the code would be simpler if we
		* disallowed it - it may even be possible to guarantee that
		* this condition never is false.  Later.  */
		&& (out_nbits & 15) == 0
# endif
		&& toscr_nr_planes == thisline_decision.nr_planes)
	{
		int offs = (pos - fetch_cycle) & fetchunit_mask;
		int ddf2 = ((ddfstop_to_test - offs + fetchunit - 1) & ~fetchunit_mask) + offs;
		int ddf3 = ddf2 + fetchunit;
		int stop = until < ddf2 ? until : until < ddf3 ? ddf2 : ddf3;
		int count;

		count = stop - pos;

		if (count >= fetchstart) {
			count &= ~fetchstart_mask;

			if (thisline_decision.plfleft == -1) {
				compute_delay_offset ();
				compute_toscr_delay_1 (bplcon1);
			}

			do_long_fetch (pos, count >> (3 - toscr_res), dma, fm);

			/* This must come _after_ do_long_fetch so as not to confuse flush_display
			into thinking the first fetch has produced any output worth emitting to
			the screen.  But the calculation of delay_offset must happen _before_.  */
			maybe_first_bpl1dat (pos);

			if (pos <= ddfstop_to_test && pos + count > ddfstop_to_test)
				plf_state = plf_passed_stop;
			if (pos <= ddfstop_to_test && pos + count > ddf2)
				plf_state = plf_passed_stop2;
			if (pos <= ddf2 && pos + count >= ddf2 + fm_maxplane)
				add_modulos ();
			pos += count;
			fetch_cycle += count;
		}
	} else {
#endif
#ifdef SPEEDUP
	}
#endif
	for (; pos < until; pos++) {
		if (fetch_state == fetch_was_plane0) {
			beginning_of_plane_block (pos, fm);
			estimate_last_fetch_cycle (pos);
		}
		fetch_start (pos);

		if (one_fetch_cycle (pos, ddfstop_to_test, dma, fm))
			return;
	}
	if (until >= maxhpos) {
		finish_final_fetch (pos, fm);
		return;
	}
	flush_display (fm);
}

static void update_fetch_0 (int hpos) { update_fetch (hpos, 0); }
static void update_fetch_1 (int hpos) { update_fetch (hpos, 1); }
static void update_fetch_2 (int hpos) { update_fetch (hpos, 2); }

STATIC_INLINE void decide_fetch (int hpos)
{
	if (hpos > last_fetch_hpos) {
		if (fetch_state != fetch_not_started) {
			switch (fetchmode) {
			case 0: update_fetch_0 (hpos); break;
#ifdef AGA
			case 1: update_fetch_1 (hpos); break;
			case 2: update_fetch_2 (hpos); break;
#endif
			default: uae_abort (_T("fetchmode corrupt"));
			}
		} else if (bpl1dat_written) {
			// "PIO" mode display
#if 1
			if (!bpl1dat_done) {
				bpl1dat_done = true;
				//plfleft_real = thisline_decision.plfleft;
			}
			update_fetch_x (hpos, fetchmode);
#endif
		}
		maybe_check (hpos);
		last_fetch_hpos = hpos;
	}
}

static void start_bpl_processing (int hpos, int hstart, bool dma)
{
	if (first_bpl_vpos < 0)
		first_bpl_vpos = vpos;

	if (doflickerfix () && interlace_seen && !scandoubled_line) {
		int i;
		for (i = 0; i < 8; i++) {
			prevbpl[lof_current][vpos][i] = bplptx[i];
			if (!lof_current && (bplcon0 & 4))
				bplpt[i] = prevbpl[1 - lof_current][vpos][i];
			if (!(bplcon0 & 4) || interlace_seen < 0)
				prevbpl[1 - lof_current][vpos][i] = prevbpl[lof_current][vpos][i] = 0;
		}
	}

	plfstrt_sprite = plfstrt;
	if (dma)
		fetch_start (hpos);
	fetch_cycle = 0;
	last_fetch_hpos = hstart;
	cycle_diagram_shift = last_fetch_hpos;
	out_nbits = 0;
	out_offs = 0;
	toscr_nbits = 0;
	thisline_decision.bplres = bplcon0_res;

	ddfstate = DIW_waiting_stop;
	compute_toscr_delay (last_fetch_hpos, bplcon1);

	/* If someone already wrote BPL1DAT, clear the area between that point and
	the real fetch start.  */
	if (!nodraw ()) {
		if (thisline_decision.plfleft != -1) {
			out_nbits = (plfstrt - thisline_decision.plfleft) << (1 + toscr_res);
			out_offs = out_nbits >> 5;
			out_nbits &= 31;
		}
		update_toscr_planes ();
	}
}

/* this may turn on datafetch if program turns dma on during the ddf */
static void maybe_start_bpl_dma (int hpos)
{
	/* OCS: BPL DMA never restarts if DMA is turned on during DDF
	* ECS/AGA: BPL DMA restarts but only if DMA was turned off
	outside of DDF or during current line, otherwise display
	processing jumps immediately to "DDFSTOP passed"-condition */
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		return;
	if (fetch_state != fetch_not_started)
		return;
	if (diwstate != DIW_waiting_stop)
		return;
	if (hpos <= plfstrt)
		return;
	if (hpos > plfstop - fetchunit)
		return;
	if (ddfstate != DIW_waiting_start)
		plf_state = plf_passed_stop;
	start_bpl_processing (hpos, hpos, true);
}

/* This function is responsible for turning on datafetch if necessary. */
STATIC_INLINE void decide_line (int hpos)
{
	/* Take care of the vertical DIW.  */
	if (vpos == plffirstline) {
		diwstate = DIW_waiting_stop;
		ddf_change = vpos;
	}
	if (vpos == plflastline) {
		diwstate = DIW_waiting_start;
		ddf_change = vpos;
	}

	if (hpos <= last_decide_line_hpos)
		return;

	if (fetch_state == fetch_not_started && (diwstate == DIW_waiting_stop || (currprefs.chipset_mask & CSMASK_ECS_AGNUS))) {
		int ok = 0;
		if (last_decide_line_hpos < plfstrt_start && hpos >= plfstrt_start) {
			if (plf_state == plf_idle)
				plf_state = plf_start;
		}
		if (last_decide_line_hpos < plfstrt && hpos >= plfstrt) {
			if (plf_state == plf_start)
				plf_state = plf_active;
			if (plf_state == plf_active)
				ok = 1;
			/* hack warning.. Writing to DDFSTRT when DMA should start must be ignored
			* (correct fix would be emulate this delay for every custom register, but why bother..) */
			if (hpos - 2 == ddfstrt_old_hpos)
				ok = 0;
		}
		if (ok && diwstate == DIW_waiting_stop) {
			if (dmaen (DMA_BITPLANE)) {
				start_bpl_processing (hpos, plfstrt, true);
				estimate_last_fetch_cycle (plfstrt);
			}
			last_decide_line_hpos = hpos;
#ifndef	CUSTOM_SIMPLE
			do_sprites (hpos);
#endif
			return;
		}
	}

#ifndef	CUSTOM_SIMPLE
	if (hpos > last_sprite_hpos && last_sprite_hpos < SPR0_HPOS + 4 * MAX_SPRITES)
		do_sprites (hpos);
#endif

	last_decide_line_hpos = hpos;
}

/* Called when a color is about to be changed (write to a color register),
* but the new color has not been entered into the table yet. */
static void record_color_change (int hpos, int regno, unsigned long value)
{
	if (regno < 0x1000 && nodraw ())
		return;
	/* Early positions don't appear on-screen. */
	if (vpos < minfirstline)
		return;

	decide_diw (hpos);
	decide_line (hpos);

	if (thisline_decision.ctable == -1)
		remember_ctable ();

	if  ((regno < 0x1000 || regno == 0x1000 + 0x10c) && hpos < HBLANK_OFFSET && !(beamcon0 & 0x80) && prev_lineno >= 0) {
		struct draw_info *pdip = curr_drawinfo + prev_lineno;
		int idx = pdip->last_color_change;
		int extrahpos = regno == 0x1000 + 0x10c ? 1 : 0;
		bool lastsync = false;
		/* Move color changes in horizontal cycles 0 to HBLANK_OFFSET to end of previous line.
		* Cycles 0 to HBLANK_OFFSET are visible in right border on real Amigas. (because of late hsync)
		*/
		if (curr_color_changes[idx - 1].regno == 0xffff) {
			idx--;
			lastsync = true;
		}
		pdip->last_color_change++;
		pdip->nr_color_changes++;
		curr_color_changes[idx].linepos = (hpos + maxhpos) * 2 + extrahpos;
		curr_color_changes[idx].regno = regno;
		curr_color_changes[idx].value = value;
		if (lastsync) {
			curr_color_changes[idx + 1].linepos = hsyncstartpos * 2;
			curr_color_changes[idx + 1].regno = 0xffff;
			curr_color_changes[idx + 2].regno = -1;
		} else {
			curr_color_changes[idx + 1].regno = -1;
		}
	}
	record_color_change2 (hpos, regno, value);

	if (regno == 0 && value != 0 && vpos >= 20) {
		// autoscale if COLOR00 changes in top or bottom of screen
		if (vpos >= minfirstline) {
			int vpos2 = autoscale_bordercolors ? minfirstline : vpos;
			if (first_planes_vpos == 0)
				first_planes_vpos = vpos2 - 2;
			if (plffirstline_total == current_maxvpos ())
				plffirstline_total = vpos2 - 2;
			if (vpos2 > last_planes_vpos || vpos2 > plflastline_total)
				plflastline_total = last_planes_vpos = vpos2 + 3;
			autoscale_bordercolors = 0;
		} else {
			autoscale_bordercolors++;
		}
	}
}

static void record_register_change (int hpos, int regno, unsigned long value)
{
	if (regno == 0x100) { // BPLCON0
		if (value & 0x800)
			thisline_decision.ham_seen = 1;
		thisline_decision.ehb_seen = !! isehb (value, bplcon2);
	} else if (regno == 0x104) { // BPLCON2
		thisline_decision.ehb_seen = !! isehb (value, bplcon2);
	}
	record_color_change (hpos, regno + 0x1000, value);
}

typedef int sprbuf_res_t, cclockres_t, hwres_t,	bplres_t;

static int expand_sprres (uae_u16 con0, uae_u16 con3)
{
	int res;

	switch ((con3 >> 6) & 3)
	{
	default:
		res = RES_LORES;
		break;
#ifdef ECS_DENISE
	case 0: /* ECS defaults (LORES,HIRES=LORES sprite,SHRES=HIRES sprite) */
		if ((currprefs.chipset_mask & CSMASK_ECS_DENISE) && GET_RES_DENISE (con0) == RES_SUPERHIRES)
			res = RES_HIRES;
		else
			res = RES_LORES;
		break;
#endif
#ifdef AGA
	case 1:
		res = RES_LORES;
		break;
	case 2:
		res = RES_HIRES;
		break;
	case 3:
		res = RES_SUPERHIRES;
		break;
#endif
	}
	return res;
}

/* handle very rarely needed playfield collision (CLXDAT bit 0) */
/* only known game needing this is Rotor */
static void do_playfield_collisions (void)
{
	int bplres = bplcon0_res;
	hwres_t ddf_left = thisline_decision.plfleft * 2 << bplres;
	hwres_t hw_diwlast = coord_window_to_diw_x (thisline_decision.diwlastword);
	hwres_t hw_diwfirst = coord_window_to_diw_x (thisline_decision.diwfirstword);
	int i, collided, minpos, maxpos;
#ifdef AGA
	int planes = (currprefs.chipset_mask & CSMASK_AGA) ? 8 : 6;
#else
	int planes = 6;
#endif

	if (clxcon_bpl_enable == 0) {
		clxdat |= 1;
		return;
	}
	if (clxdat & 1)
		return;

	collided = 0;
	minpos = thisline_decision.plfleft * 2;
	if (minpos < hw_diwfirst)
		minpos = hw_diwfirst;
	maxpos = thisline_decision.plfright * 2;
	if (maxpos > hw_diwlast)
		maxpos = hw_diwlast;
	for (i = minpos; i < maxpos && !collided; i+= 32) {
		int offs = ((i << bplres) - ddf_left) >> 3;
		int j;
		uae_u32 total = 0xffffffff;
		for (j = 0; j < planes; j++) {
			int ena = (clxcon_bpl_enable >> j) & 1;
			int match = (clxcon_bpl_match >> j) & 1;
			uae_u32 t = 0xffffffff;
			if (ena) {
				if (j < thisline_decision.nr_planes) {
					t = *(uae_u32 *)(line_data[next_lineno] + offs + 2 * j * MAX_WORDS_PER_LINE);
					t ^= (match & 1) - 1;
				} else {
					t = (match & 1) - 1;
				}
			}
			total &= t;
		}
		if (total) {
			collided = 1;
#if 0
			{
				int k;
				for (k = 0; k < 1; k++) {
					uae_u32 *ldata = (uae_u32 *)(line_data[next_lineno] + offs + 2 * k * MAX_WORDS_PER_LINE);
					*ldata ^= 0x5555555555;
				}
			}
#endif

		}
	}
	if (collided)
		clxdat |= 1;
}

/* Sprite-to-sprite collisions are taken care of in record_sprite.  This one does
playfield/sprite collisions. */
static void do_sprite_collisions (void)
{
	int nr_sprites = curr_drawinfo[next_lineno].nr_sprites;
	int first = curr_drawinfo[next_lineno].first_sprite_entry;
	int i;
	unsigned int collision_mask = clxmask[clxcon >> 12];
	int bplres = bplcon0_res;
	hwres_t ddf_left = thisline_decision.plfleft * 2 << bplres;
	hwres_t hw_diwlast = coord_window_to_diw_x (thisline_decision.diwlastword);
	hwres_t hw_diwfirst = coord_window_to_diw_x (thisline_decision.diwfirstword);

	if (clxcon_bpl_enable == 0) {
		clxdat |= 0x1FE;
		return;
	}

	for (i = 0; i < nr_sprites; i++) {
		struct sprite_entry *e = curr_sprite_entries + first + i;
		sprbuf_res_t j;
		sprbuf_res_t minpos = e->pos;
		sprbuf_res_t maxpos = e->max;
		hwres_t minp1 = minpos >> sprite_buffer_res;
		hwres_t maxp1 = maxpos >> sprite_buffer_res;

		if (maxp1 > hw_diwlast)
			maxpos = hw_diwlast << sprite_buffer_res;
		if (maxp1 > thisline_decision.plfright * 2)
			maxpos = thisline_decision.plfright * 2 << sprite_buffer_res;
		if (minp1 < hw_diwfirst)
			minpos = hw_diwfirst << sprite_buffer_res;
		if (minp1 < thisline_decision.plfleft * 2)
			minpos = thisline_decision.plfleft * 2 << sprite_buffer_res;

		for (j = minpos; j < maxpos; j++) {
			int sprpix = spixels[e->first_pixel + j - e->pos] & collision_mask;
			int k, offs, match = 1;

			if (sprpix == 0)
				continue;

			offs = ((j << bplres) >> sprite_buffer_res) - ddf_left;
			sprpix = sprite_ab_merge[sprpix & 255] | (sprite_ab_merge[sprpix >> 8] << 2);
			sprpix <<= 1;

			/* Loop over number of playfields.  */
			for (k = 1; k >= 0; k--) {
				int l;
#ifdef AGA
				int planes = (currprefs.chipset_mask & CSMASK_AGA) ? 8 : 6;
#else
				int planes = 6;
#endif
				if (bplcon0 & 0x400)
					match = 1;
				for (l = k; match && l < planes; l += 2) {
					int t = 0;
					if (l < thisline_decision.nr_planes) {
						uae_u32 *ldata = (uae_u32 *)(line_data[next_lineno] + 2 * l * MAX_WORDS_PER_LINE);
						uae_u32 word = ldata[offs >> 5];
						t = (word >> (31 - (offs & 31))) & 1;
#if 0 /* debug: draw collision mask */
						if (1) {
							int m;
							for (m = 0; m < 5; m++) {
								ldata = (uae_u32 *)(line_data[next_lineno] + 2 * m * MAX_WORDS_PER_LINE);
								ldata[(offs >> 5) + 1] |= 15 << (31 - (offs & 31));
							}
						}
#endif
					}
					if (clxcon_bpl_enable & (1 << l)) {
						if (t != ((clxcon_bpl_match >> l) & 1))
							match = 0;
					}
				}
				if (match) {
#if 0 /* debug: mark lines where collisions are detected */
					if (0) {
						int l;
						for (l = 0; l < 5; l++) {
							uae_u32 *ldata = (uae_u32 *)(line_data[next_lineno] + 2 * l * MAX_WORDS_PER_LINE);
							ldata[(offs >> 5) + 1] |= 15 << (31 - (offs & 31));
						}
					}
#endif
					clxdat |= sprpix << (k * 4);
				}
			}
		}
	}
#if 0
	{
		static int olx;
		if (clxdat != olx)
			write_log (_T("%d: %04X\n"), vpos, clxdat);
		olx = clxdat;
	}
#endif
}

STATIC_INLINE void record_sprite_1 (int sprxp, uae_u16 *buf, uae_u32 datab, int num, int dbl,
	unsigned int mask, int do_collisions, uae_u32 collision_mask)
{
	int j = 0;
	while (datab) {
		unsigned int col = 0;
		unsigned coltmp = 0;

		if ((sprxp >= sprite_minx && sprxp < sprite_maxx) || (bplcon3 & 2))
			col = (datab & 3) << (2 * num);
#if 0
		if (sprxp == sprite_minx || sprxp == sprite_maxx - 1)
			col ^= (rand () << 16) | rand ();
#endif
		if ((j & mask) == 0) {
			unsigned int tmp = (*buf) | col;
			*buf++ = tmp;
			if (do_collisions)
				coltmp |= tmp;
			sprxp++;
		}
		if (dbl > 0) {
			unsigned int tmp = (*buf) | col;
			*buf++ = tmp;
			if (do_collisions)
				coltmp |= tmp;
			sprxp++;
		}
		if (dbl > 1) {
			unsigned int tmp;
			tmp = (*buf) | col;
			*buf++ = tmp;
			if (do_collisions)
				coltmp |= tmp;
			tmp = (*buf) | col;
			*buf++ = tmp;
			if (do_collisions)
				coltmp |= tmp;
			sprxp++;
			sprxp++;
		}
		j++;
		datab >>= 2;
		if (do_collisions) {
			coltmp &= collision_mask;
			if (coltmp) {
				unsigned int shrunk_tmp = sprite_ab_merge[coltmp & 255] | (sprite_ab_merge[coltmp >> 8] << 2);
				clxdat |= sprclx[shrunk_tmp];
			}
		}
	}
}

/* DATAB contains the sprite data; 16 pixels in two-bit packets.  Bits 0/1
determine the color of the leftmost pixel, bits 2/3 the color of the next
etc.
This function assumes that for all sprites in a given line, SPRXP either
stays equal or increases between successive calls.

The data is recorded either in lores pixels (if OCS/ECS), or in hires or
superhires pixels (if AGA).  */

static void record_sprite (int line, int num, int sprxp, uae_u16 *data, uae_u16 *datb, unsigned int ctl)
{
	struct sprite_entry *e = curr_sprite_entries + next_sprite_entry;
	int i;
	int word_offs;
	uae_u32 collision_mask;
	int width, dbl, half;
	unsigned int mask = 0;
	int attachment;

	half = 0;
	dbl = sprite_buffer_res - sprres;
	if (dbl < 0) {
		half = -dbl;
		dbl = 0;
		mask = 1 << half;
	}
	width = (sprite_width << sprite_buffer_res) >> sprres;
	attachment = sprctl[num | 1] & 0x80;

	/* Try to coalesce entries if they aren't too far apart  */
	if (!next_sprite_forced && e[-1].max + sprite_width >= sprxp) {
		e--;
	} else {
		next_sprite_entry++;
		e->pos = sprxp;
		e->has_attached = 0;
	}

	if (sprxp < e->pos)
		uae_abort (_T("sprxp < e->pos"));

	e->max = sprxp + width;
	e[1].first_pixel = e->first_pixel + ((e->max - e->pos + 3) & ~3);
	next_sprite_forced = 0;

	collision_mask = clxmask[clxcon >> 12];
	word_offs = e->first_pixel + sprxp - e->pos;

	for (i = 0; i < sprite_width; i += 16) {
		unsigned int da = *data;
		unsigned int db = *datb;
		uae_u32 datab = ((sprtaba[da & 0xFF] << 16) | sprtaba[da >> 8]
		| (sprtabb[db & 0xFF] << 16) | sprtabb[db >> 8]);
		int off = (i << dbl) >> half;
		uae_u16 *buf = spixels + word_offs + off;
		if (currprefs.collision_level > 0 && collision_mask)
			record_sprite_1 (sprxp + off, buf, datab, num, dbl, mask, 1, collision_mask);
		else
			record_sprite_1 (sprxp + off, buf, datab, num, dbl, mask, 0, collision_mask);
		data++;
		datb++;
	}

	/* We have 8 bits per pixel in spixstate, two for every sprite pair.  The
	low order bit records whether the attach bit was set for this pair.  */
	if (attachment && !ecsshres ()) {
		uae_u32 state = 0x01010101 << (num & ~1);
		uae_u8 *stb1 = spixstate.bytes + word_offs;
		for (i = 0; i < width; i += 8) {
			stb1[0] |= state;
			stb1[1] |= state;
			stb1[2] |= state;
			stb1[3] |= state;
			stb1[4] |= state;
			stb1[5] |= state;
			stb1[6] |= state;
			stb1[7] |= state;
			stb1 += 8;
		}
		e->has_attached = 1;
	}
}

static void add_sprite (int *countp, int num, int sprxp, int posns[], int nrs[])
{
	int count = *countp;
	int j, bestp;

	/* Sort the sprites in order of ascending X position before recording them.  */
	for (bestp = 0; bestp < count; bestp++) {
		if (posns[bestp] > sprxp)
			break;
		if (posns[bestp] == sprxp && nrs[bestp] < num)
			break;
	}
	for (j = count; j > bestp; j--) {
		posns[j] = posns[j - 1];
		nrs[j] = nrs[j - 1];
	}
	posns[j] = sprxp;
	nrs[j] = num;
	count++;
	*countp = count;
}

static int tospritexdiw (int diw)
{
	return  coord_window_to_hw_x (diw - (DIW_DDF_OFFSET << lores_shift)) << sprite_buffer_res;
}
static int tospritexddf (int ddf)
{
	return (ddf * 2) << sprite_buffer_res;
}
static int fromspritexdiw (int ddf)
{
	return coord_hw_to_window_x (ddf >> sprite_buffer_res) + (DIW_DDF_OFFSET << lores_shift);
}

static void calcsprite (void)
{
	sprite_maxx = 0x7fff;
	sprite_minx = 0;
	if (thisline_decision.diwlastword >= 0)
		sprite_maxx = tospritexdiw (thisline_decision.diwlastword);
	if (thisline_decision.diwfirstword >= 0)
		sprite_minx = tospritexdiw (thisline_decision.diwfirstword);
	if (thisline_decision.plfleft >= 0) {
		int min, max;
		min = tospritexddf (thisline_decision.plfleft);
		max = tospritexddf (thisline_decision.plfright);
		if (min > sprite_minx && min < max) /* min < max = full line ddf */
			sprite_minx = min;
		/* sprites are visible from first BPL1DAT write to end of line
		* (undocumented feature)
		*/
	}
}

static void decide_sprites (int hpos)
{
	int nrs[MAX_SPRITES * 2], posns[MAX_SPRITES * 2];
	int count, i;
	/* apparantly writes to custom registers happen in the 3/4th of cycle
	* and sprite xpos comparator sees it immediately */
	int point = hpos * 2 - 3;
	int width = sprite_width;
	int sscanmask = 0x100 << sprite_buffer_res;
	int gotdata = 0;

	if (thisline_decision.plfleft == -1 && !(bplcon3 & 2))
		return;

	if (nodraw () || hpos < 0x14 || nr_armed == 0 || point == last_sprite_point)
		return;

	decide_diw (hpos);
	decide_line (hpos);
	calcsprite ();

	count = 0;
	for (i = 0; i < MAX_SPRITES; i++) {
		int sprxp = (fmode & 0x8000) ? (spr[i].xpos & ~sscanmask) : spr[i].xpos;
		int hw_xp = sprxp >> sprite_buffer_res;

		if (spr[i].xpos < 0)
			continue;

		if (!((debug_sprite_mask & magic_sprite_mask) & (1 << i)))
			continue;

		if (! spr[i].armed)
			continue;

		if (hw_xp > last_sprite_point && hw_xp <= point)
			add_sprite (&count, i, sprxp, posns, nrs);

		/* SSCAN2-bit is fun.. */
		if ((fmode & 0x8000) && !(sprxp & sscanmask)) {
			sprxp |= sscanmask;
			hw_xp = sprxp >> sprite_buffer_res;
			if (hw_xp > last_sprite_point && hw_xp <= point)
				add_sprite (&count, MAX_SPRITES + i, sprxp, posns, nrs);
		}
	}

	for (i = 0; i < count; i++) {
		int nr = nrs[i] & (MAX_SPRITES - 1);
		record_sprite (next_lineno, nr, posns[i], sprdata[nr], sprdatb[nr], sprctl[nr]);
		/* get left and right sprite edge if brdsprt enabled */
#if AUTOSCALE_SPRITES
		if (dmaen (DMA_SPRITE) && (bplcon0 & 1) && (bplcon3 & 0x02) && !(bplcon3 & 0x20)) {
			int j, jj;
			for (j = 0, jj = 0; j < sprite_width; j+= 16, jj++) {
				int nx = fromspritexdiw (posns[i] + j);
				if (sprdata[nr][jj] || sprdatb[nr][jj]) {
					if (diwfirstword_total > nx && nx >= (48 << currprefs.gfx_resolution))
						diwfirstword_total = nx;
					if (diwlastword_total < nx + 16 && nx <= (448 << currprefs.gfx_resolution))
						diwlastword_total = nx + 16;
				}
			}
			gotdata = 1;
		}
#endif
	}
	last_sprite_point = point;

#if AUTOSCALE_SPRITES
	/* get upper and lower sprite position if brdsprt enabled */
	if (gotdata) {
		if (vpos < first_planes_vpos)
			first_planes_vpos = vpos;
		if (vpos < plffirstline_total)
			plffirstline_total = vpos;
		if (vpos > last_planes_vpos)
			last_planes_vpos = vpos;
		if (vpos > plflastline_total)
			plflastline_total = vpos;
	}
#endif
}

STATIC_INLINE int sprites_differ (struct draw_info *dip, struct draw_info *dip_old)
{
	struct sprite_entry *this_first = curr_sprite_entries + dip->first_sprite_entry;
	struct sprite_entry *this_last = curr_sprite_entries + dip->last_sprite_entry;
	struct sprite_entry *prev_first = prev_sprite_entries + dip_old->first_sprite_entry;
	int npixels;
	int i;

	if (dip->nr_sprites != dip_old->nr_sprites)
		return 1;

	if (dip->nr_sprites == 0)
		return 0;

	for (i = 0; i < dip->nr_sprites; i++)
		if (this_first[i].pos != prev_first[i].pos
			|| this_first[i].max != prev_first[i].max
			|| this_first[i].has_attached != prev_first[i].has_attached)
			return 1;

	npixels = this_last->first_pixel + (this_last->max - this_last->pos) - this_first->first_pixel;
	if (memcmp (spixels + this_first->first_pixel, spixels + prev_first->first_pixel,
		npixels * sizeof (uae_u16)) != 0)
		return 1;
	if (memcmp (spixstate.bytes + this_first->first_pixel, spixstate.bytes + prev_first->first_pixel, npixels) != 0)
		return 1;
	return 0;
}

STATIC_INLINE int color_changes_differ (struct draw_info *dip, struct draw_info *dip_old)
{
	if (dip->nr_color_changes != dip_old->nr_color_changes)
		return 1;

	if (dip->nr_color_changes == 0)
		return 0;
	if (memcmp (curr_color_changes + dip->first_color_change,
		prev_color_changes + dip_old->first_color_change,
		dip->nr_color_changes * sizeof *curr_color_changes) != 0)
		return 1;
	return 0;
}

/* End of a horizontal scan line. Finish off all decisions that were not
* made yet. */
static void finish_decisions (void)
{
	struct draw_info *dip;
	struct draw_info *dip_old;
	struct decision *dp;
	int changed;
	int hpos = maxhpos;

	if (nodraw ())
		return;

	decide_diw (hpos);
	decide_line (hpos);
	decide_fetch (hpos);
	if (bpl1dat_done) {
		finish_final_fetch (hpos, fetchmode);
		bpl1dat_done = 0;
	}

	record_color_change2 (hsyncstartpos, 0xffff, 0);
	if (thisline_decision.plfleft != -1 && thisline_decision.plflinelen == -1) {
		if (fetch_state != fetch_not_started) {
			write_log (_T("fetch_state=%d plfleft=%d,len=%d,vpos=%d,hpos=%d\n"),
				fetch_state, thisline_decision.plfleft, thisline_decision.plflinelen,
				vpos, hpos);
			uae_abort (_T("fetch_state != fetch_not_started"));
		}
		thisline_decision.plfright = thisline_decision.plfleft;
		thisline_decision.plflinelen = 0;
		thisline_decision.bplres = RES_LORES;
	}

	/* Large DIWSTOP values can cause the stop position never to be
	* reached, so the state machine always stays in the same state and
	* there's a more-or-less full-screen DIW. */
	if (hdiwstate == DIW_waiting_stop) {
		thisline_decision.diwlastword = max_diwlastword;
		if (thisline_decision.diwfirstword == -1)
			thisline_decision.diwfirstword = 0;
	}

	if (thisline_decision.diwfirstword != line_decisions[next_lineno].diwfirstword)
		MARK_LINE_CHANGED;
	if (thisline_decision.diwlastword != line_decisions[next_lineno].diwlastword)
		MARK_LINE_CHANGED;

	dip = curr_drawinfo + next_lineno;
	dip_old = prev_drawinfo + next_lineno;
	dp = line_decisions + next_lineno;
	changed = thisline_changed;
	if (thisline_decision.plfleft != -1 && thisline_decision.nr_planes > 0)
		record_diw_line (thisline_decision.plfleft, diwfirstword, diwlastword);

	decide_sprites (hpos + 1);

	dip->last_sprite_entry = next_sprite_entry;
	dip->last_color_change = next_color_change;

	if (thisline_decision.ctable == -1) {
		if (thisline_decision.plfleft == -1)
			remember_ctable_for_border ();
		else
			remember_ctable ();
	}

	dip->nr_color_changes = next_color_change - dip->first_color_change;
	dip->nr_sprites = next_sprite_entry - dip->first_sprite_entry;

	if (thisline_decision.plfleft != line_decisions[next_lineno].plfleft)
		changed = 1;
	if (! changed && color_changes_differ (dip, dip_old))
		changed = 1;
	if (!changed && thisline_decision.plfleft != -1 && sprites_differ (dip, dip_old))
		changed = 1;

	if (changed) {
		thisline_changed = 1;
		*dp = thisline_decision;
	} else
		/* The only one that may differ: */
		dp->ctable = thisline_decision.ctable;

	/* leave free space for possible extra color changes at the end of line */
	next_color_change += (HBLANK_OFFSET + 1) / 2;

	diw_hcounter += maxhpos * 2;
	if (!(currprefs.chipset_mask & CSMASK_ECS_DENISE) && vpos == equ_vblank_endline - 1)
		diw_hcounter++;
	if ((currprefs.chipset_mask & CSMASK_ECS_DENISE) || vpos > equ_vblank_endline || (currprefs.cs_dipagnus && vpos == 0)) {
		diw_hcounter = maxhpos * 2;
		last_hdiw = 2 - 1;
	}

	if (next_color_change >= MAX_REG_CHANGE - 30) {
		write_log (_T("color_change buffer overflow!\n"));
		next_color_change = 0;
		dip->nr_color_changes = 0;
		dip->first_color_change = 0;
	}
}

/* Set the state of all decisions to "undecided" for a new scanline. */
static void reset_decisions (void)
{
	if (nodraw ())
		return;

	toscr_nr_planes = toscr_nr_planes2 = 0;
	thisline_decision.bplres = bplcon0_res;
	thisline_decision.nr_planes = 0;
	bpl1dat_written = 0;
	bpl1dat_early = 0;

	plfleft_real = -1;
	thisline_decision.plfleft = -1;
	thisline_decision.plflinelen = -1;
	thisline_decision.ham_seen = !! (bplcon0 & 0x800);
	thisline_decision.ehb_seen = !! isehb (bplcon0, bplcon2);
	thisline_decision.ham_at_start = !! (bplcon0 & 0x800);

	/* decided_res shouldn't be touched before it's initialized by decide_line(). */
	thisline_decision.diwfirstword = -1;
	thisline_decision.diwlastword = -1;
	if (hdiwstate == DIW_waiting_stop) {
		thisline_decision.diwfirstword = 0;
		if (thisline_decision.diwfirstword != line_decisions[next_lineno].diwfirstword)
			MARK_LINE_CHANGED;
	}
	thisline_decision.ctable = -1;

	thisline_changed = 0;
	curr_drawinfo[next_lineno].first_color_change = next_color_change;
	curr_drawinfo[next_lineno].first_sprite_entry = next_sprite_entry;
	next_sprite_forced = 1;

	last_sprite_point = 0;
	fetch_state = fetch_not_started;
	bplcon1_hpos = -1;
	if (bpldmasetuphpos >= 0) {
		// this can happen in "too fast" modes
		BPLCON0_Denise (0, bplcon0);
		setup_fmodes (0);
	}
	bpldmasetuphpos = -1;
	bpldmasetupphase = 0;
	ddfstrt_old_hpos = -1;

	if (plf_state > plf_active)
		plf_state = plf_idle;
	if (plf_state == plf_active && !(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		plf_state = plf_idle;

	memset (todisplay, 0, sizeof todisplay);
	memset (fetched, 0, sizeof fetched);
#ifdef AGA
	if (currprefs.chipset_mask & CSMASK_AGA) {
		memset (fetched_aga0, 0, sizeof fetched_aga0);
		memset (fetched_aga1, 0, sizeof fetched_aga1);
	}
#endif
	memset (outword, 0, sizeof outword);

	last_decide_line_hpos = -1;
	last_ddf_pix_hpos = -1;
	last_sprite_hpos = -1;
	last_fetch_hpos = -1;

	/* These are for comparison. */
	thisline_decision.bplcon0 = bplcon0;
	thisline_decision.bplcon2 = bplcon2;
#ifdef ECS_DENISE
	thisline_decision.bplcon3 = bplcon3;
#endif
#ifdef AGA
	thisline_decision.bplcon4 = bplcon4;
#endif
}

static int islinetoggle (void)
{
	int linetoggle = 0;
	if (!(beamcon0 & 0x0800) && !(beamcon0 & 0x0020) && (currprefs.chipset_mask & CSMASK_ECS_AGNUS)) {
		linetoggle = 1; // NTSC and !LOLDIS -> LOL toggles every line
	} else if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS) && currprefs.ntscmode) {
		linetoggle = 1; // hardwired NTSC Agnus
	}
	return linetoggle;
}

static int isvsync (void)
{
	if (picasso_on || !currprefs.gfx_avsync || (currprefs.gfx_avsync == 0 && !currprefs.gfx_afullscreen))
		return 0;
	return currprefs.gfx_avsyncmode == 0 ? 1 : -1;
}

int vsynctime_orig;

void compute_vsynctime (void)
{
	fake_vblank_hz = 0;
	if (abs (currprefs.chipset_refreshrate) > 0.1) {
		vblank_hz = currprefs.chipset_refreshrate;
		if (isvsync ()) {
			vblank_skip = 1;
			if (!fake_vblank_hz && getvsyncrate (vblank_hz) != vblank_hz) {
				vblank_hz = getvsyncrate (vblank_hz);
				vblank_skip = -1;
			}
		}
	}
	if (!fake_vblank_hz)
		fake_vblank_hz = vblank_hz;
	if (currprefs.turbo_emulation)
		vsynctime = vsynctime_orig = 1;
	else
		vsynctime = vsynctime_orig = syncbase / fake_vblank_hz;
	if (!picasso_on) {
		updatedisplayarea ();
	}
	if (currprefs.produce_sound > 1)
		update_sound (fake_vblank_hz, (bplcon0 & 4) ? -1 : lof_store, islinetoggle ());
}


static void dumpsync (void)
{
	static int cnt = 100;
	if (cnt < 0)
		return;
	cnt--;
	write_log (_T("BEAMCON0=%04X VTOTAL=%04X  HTOTAL=%04X\n"), new_beamcon0, vtotal, htotal);
	write_log (_T("  HSSTOP=%04X HBSTRT=%04X  HBSTOP=%04X\n"), hsstop, hbstrt, hbstop);
	write_log (_T("  VSSTOP=%04X VBSTRT=%04X  VBSTOP=%04X\n"), vsstop, vbstrt, vbstop);
	write_log (_T("  HSSTRT=%04X VSSTRT=%04X HCENTER=%04X\n"), hsstrt, vsstrt, hcenter);
}

int current_maxvpos (void)
{
	return maxvpos + (lof_store ? 1 : 0);
}

/* set PAL/NTSC or custom timing variables */
void init_hz (bool fullinit)
{
	int isntsc;
	int odbl = doublescan, omaxvpos = maxvpos;
	double ovblank = vblank_hz;
	int hzc = 0;

	if (fullinit)
		vpos_count = vpos_count_prev = 0;

	if (vsync_switchmode (-1, 0))
		currprefs.gfx_avsync = changed_prefs.gfx_avsync = vsync_switchmode (-1, 0) ? 2 : 0;

	if (!isvsync () && (DBLEQU (currprefs.chipset_refreshrate, 50) && !currprefs.ntscmode) ||
		(DBLEQU (currprefs.chipset_refreshrate, 60) && currprefs.ntscmode)) {
			currprefs.chipset_refreshrate = changed_prefs.chipset_refreshrate = 0.0;
	}

	doublescan = 0;
	if ((beamcon0 & 0xA0) != (new_beamcon0 & 0xA0))
		hzc = 1;
	if (beamcon0 != new_beamcon0) {
		write_log (_T("BEAMCON0 %04x -> %04x PC%=%08x\n"), beamcon0, new_beamcon0, M68K_GETPC);
		vpos_count = vpos_count_prev = 0;
	}
	beamcon0 = new_beamcon0;
	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;
	if (!isntsc) {
		maxvpos = MAXVPOS_PAL;
		maxhpos = MAXHPOS_PAL;
		minfirstline = VBLANK_ENDLINE_PAL;
		vblank_hz = VBLANK_HZ_PAL;
		sprite_vblank_endline = VBLANK_SPRITE_PAL;
		equ_vblank_endline = EQU_ENDLINE_PAL;
	} else {
		maxvpos = MAXVPOS_NTSC;
		maxhpos = MAXHPOS_NTSC;
		minfirstline = VBLANK_ENDLINE_NTSC;
		vblank_hz = VBLANK_HZ_NTSC;
		sprite_vblank_endline = VBLANK_SPRITE_NTSC;
		equ_vblank_endline = EQU_ENDLINE_NTSC;
	}
	maxvpos_nom = maxvpos;
	if (vpos_count > 0) {
		// we come here if vpos_count != maxvpos (someone poked VPOSW)
		if (vpos_count < 10)
			vpos_count = 10;
		vblank_hz = (isntsc ? 15734 : 15625.0) / vpos_count;
		maxvpos_nom = vpos_count - (lof_current ? 1 : 0);
		reset_drawing ();
	}
	if (beamcon0 & 0x80) {
		// programmable scanrates (ECS Agnus)
		if (vtotal >= MAXVPOS)
			vtotal = MAXVPOS - 1;
		maxvpos = vtotal + 1;
		if (htotal >= MAXHPOS)
			htotal = MAXHPOS - 1;
		maxhpos = htotal + 1;
		vblank_hz = 227.0 * 312.0 * 50.0 / (maxvpos * maxhpos);
		minfirstline = vsstop;
		if (minfirstline < 2)
			minfirstline = 2;
		if (minfirstline >= maxvpos)
			minfirstline = maxvpos - 1;
		sprite_vblank_endline = minfirstline - 2;
		maxvpos_nom = maxvpos;
		equ_vblank_endline = -1;
		doublescan = htotal <= 164 ? 1 : 0;
		dumpsync ();
		hzc = 1;
	}
	if (maxvpos_nom >= MAXVPOS)
		maxvpos_nom = MAXVPOS;
	if (currprefs.gfx_scandoubler && doublescan == 0)
		doublescan = -1;
	if (doublescan != odbl || maxvpos != omaxvpos)
		hzc = 1;
	/* limit to sane values */
	if (vblank_hz < 10)
		vblank_hz = 10;
	if (vblank_hz > 300)
		vblank_hz = 300;
	maxhpos_short = maxhpos;
	if (beamcon0 & 0x80) {
		if (hbstrt > maxhpos)
			hsyncstartpos = hbstrt;
		else
			hsyncstartpos = maxhpos + hbstrt;
		if (hbstop > maxhpos)
			hsyncendpos = hbstop;
		else
			hsyncendpos = maxhpos + hbstop;
	} else {
		hsyncstartpos = maxhpos_short + 13;
		hsyncendpos = 24;
	}
	eventtab[ev_hsync].oldcycles = get_cycles ();
	eventtab[ev_hsync].evtime = get_cycles () + HSYNCTIME;
	events_schedule ();
	if (hzc) {
		interlace_seen = (bplcon0 & 4) ? 1 : 0;
		reset_drawing ();
	}
	if ((DBLEQU (vblank_hz, 50) || DBLEQU (vblank_hz, 60)) && isvsync () && currprefs.gfx_avsync == 2 && currprefs.gfx_afullscreen > 0) {
		if (getvsyncrate (currprefs.gfx_refreshrate) != vblank_hz)
			vsync_switchmode (vblank_hz, currprefs.gfx_refreshrate);
	}
	if (isvsync () > 0) {
		changed_prefs.chipset_refreshrate = currprefs.chipset_refreshrate = abs (currprefs.gfx_refreshrate);
	}
	maxvpos_total = (currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? 2047 : 511;
	if (maxvpos_total > MAXVPOS)
		maxvpos_total = MAXVPOS;
	if (!p96refresh_active) {
		maxvpos_stored = maxvpos;
		maxhpos_stored = maxhpos;
		vblank_hz_stored = vblank_hz;
	}

	compute_vsynctime ();
#ifdef PICASSO96
	init_hz_p96 ();
#endif
	if (vblank_hz != ovblank)
		updatedisplayarea ();
	inputdevice_tablet_strobe ();
	write_log (_T("%s mode%s%s V=%.4fHz H=%0.4fHz (%dx%d+%d)\n"),
		isntsc ? _T("NTSC") : _T("PAL"),
		(bplcon0 & 4) ? _T(" interlaced") : _T(""),
		doublescan > 0 ? _T(" dblscan") : _T(""),
		vblank_hz, vblank_hz * maxvpos_nom,
		maxhpos, maxvpos, lof_store ? 1 : 0);
	config_changed = 1;
}

void init_hz (void)
{
	init_hz (false);
}
void init_hz_full (void)
{
	init_hz (true);
}

static void calcdiw (void)
{
	int hstrt = diwstrt & 0xFF;
	int hstop = diwstop & 0xFF;
	int vstrt = diwstrt >> 8;
	int vstop = diwstop >> 8;

	// vertical in ECS Agnus
	if (diwhigh_written && (currprefs.chipset_mask & CSMASK_ECS_AGNUS)) {
		vstrt |= (diwhigh & 7) << 8;
		vstop |= ((diwhigh >> 8) & 7) << 8;
	} else {
		if ((vstop & 0x80) == 0)
			vstop |= 0x100;
	}
	// horizontal in ECS Denise
	if (diwhigh_written && (currprefs.chipset_mask & CSMASK_ECS_DENISE)) {
		hstrt |= ((diwhigh >> 5) & 1) << 8;
		hstop |= ((diwhigh >> 13) & 1) << 8;
	} else {
		hstop += 0x100;
	}

	diw_hstrt = hstrt;
	diw_hstop = hstop;

	diwfirstword = coord_diw_to_window_x (hstrt);
	diwlastword = coord_diw_to_window_x (hstop);
	if (diwfirstword >= diwlastword) {
		diwfirstword = 0;
		diwlastword = max_diwlastword;
	}
	if (diwfirstword < 0)
		diwfirstword = 0;

	plffirstline = vstrt;
	plflastline = vstop;

	plfstrt = ddfstrt;
	plfstop = ddfstop;
	/* probably not the correct place.. should use plf_state instead */
	if (currprefs.chipset_mask & CSMASK_ECS_AGNUS) {
		/* ECS/AGA and ddfstop > maxhpos == always-on display */
		if (plfstop > maxhpos)
			plfstrt = 0;
		if (plfstrt < HARD_DDF_START)
			plfstrt = HARD_DDF_START;
		plfstrt_start = plfstrt - 4;
	} else {
		/* OCS and ddfstrt >= ddfstop == ddfstop = max */
		if (plfstrt >= plfstop && plfstrt >= HARD_DDF_START)
			plfstop = 0xff;
		plfstrt_start = HARD_DDF_START - 2;
	}
	diw_change = 2;
}

/* display mode changed (lores, doubling etc..), recalculate everything */
void init_custom (void)
{
	update_mirrors();
	create_cycle_diagram_table ();
	reset_drawing ();
	init_hz_full ();
	calcdiw ();
}

static int timehack_alive = 0;

static uae_u32 REGPARAM2 timehack_helper (TrapContext *context)
{
#ifdef HAVE_GETTIMEOFDAY
	struct timeval tv;
	if (m68k_dreg (regs, 0) == 0)
		return timehack_alive;

	timehack_alive = 10;

	gettimeofday (&tv, NULL);
	x_put_long (m68k_areg (regs, 0), tv.tv_sec - (((365 * 8 + 2) * 24) * 60 * 60));
	x_put_long (m68k_areg (regs, 0) + 4, tv.tv_usec);
	return 0;
#else
	return 2;
#endif
}

/*
* register functions
*/
STATIC_INLINE uae_u16 DENISEID (void)
{
	if (currprefs.cs_deniserev >= 0)
		return currprefs.cs_deniserev;
#ifdef AGA
	if (currprefs.chipset_mask & CSMASK_AGA) {
		if (currprefs.cs_ide == IDE_A4000)
			return 0xFCF8;
		return 0x00F8;
	}
#endif
	if (currprefs.chipset_mask & CSMASK_ECS_DENISE)
		return 0xFFFC;
	return 0xFFFF;
}
STATIC_INLINE uae_u16 DMACONR (int hpos)
{
	decide_line (hpos);
	decide_fetch (hpos);
	decide_blitter (hpos);
	dmacon &= ~(0x4000 | 0x2000);
	dmacon |= ((blit_interrupt || (!blit_interrupt && currprefs.cs_agnusbltbusybug && !blt_info.got_cycle)) ? 0 : 0x4000)
		| (blt_info.blitzero ? 0x2000 : 0);
	return dmacon;
}
STATIC_INLINE uae_u16 INTENAR (void)
{
	return intena;
}
uae_u16 INTREQR (void)
{
	return intreq;
}
STATIC_INLINE uae_u16 ADKCONR (void)
{
	return adkcon;
}

STATIC_INLINE int islightpentriggered (void)
{
	if (beamcon0 & 0x2000) // LPENDIS
		return 0;
	return lightpen_triggered > 0;
}
STATIC_INLINE int issyncstopped (void)
{
	return (bplcon0 & 2) && !currprefs.genlock;
}

STATIC_INLINE int GETVPOS (void)
{
	return islightpentriggered () ? vpos_lpen : (issyncstopped () ? vpos_previous : vpos);
}
STATIC_INLINE int GETHPOS (void)
{
	return islightpentriggered () ? hpos_lpen : (issyncstopped () ? hpos_previous : current_hpos ());
}


// DFF006 = 0.W must be valid result but better do this only in 68000 modes (whdload black screen!)

#define HPOS_OFFSET (currprefs.cpu_model < 68020 ? 3 : 0)

STATIC_INLINE uae_u16 VPOSR (void)
{
	unsigned int csbit = 0;
	uae_u16 vp = GETVPOS ();
	uae_u16 hp = GETHPOS ();

	if (hp + HPOS_OFFSET >= maxhpos) {
		vp++;
		if (vp >= maxvpos + lof_store)
			vp = 0;
	}
	vp = (vp >> 8) & 7;

	if (currprefs.cs_agnusrev >= 0) {
		csbit |= currprefs.cs_agnusrev << 8;
	} else {
#ifdef AGA
		csbit |= (currprefs.chipset_mask & CSMASK_AGA) ? 0x2300 : 0;
#endif
		csbit |= (currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? 0x2000 : 0;
		if (currprefs.chipmem_size > 1024 * 1024 && (currprefs.chipset_mask & CSMASK_ECS_AGNUS))
			csbit |= 0x2100;
		if (currprefs.ntscmode)
			csbit |= 0x1000;
	}

	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		vp &= 1;
	vp = vp | (lof_store ? 0x8000 : 0) | csbit;
	if (currprefs.chipset_mask & CSMASK_ECS_AGNUS)
		vp |= lol ? 0x80 : 0;
#if 0
	if (M68K_GETPC < 0x00f00000 || M68K_GETPC >= 0x10000000)
		write_log (_T("VPOSR %04x at %08x\n"), vp, M68K_GETPC);
#endif
	if (currprefs.cpu_model >= 68020)
		hsyncdelay ();
	return vp;
}

static void VPOSW (uae_u16 v)
{
#if 0
	if (M68K_GETPC < 0xf00000 || 1)
		write_log (_T("VPOSW %04X PC=%08x\n"), v, M68K_GETPC);
#endif
	if (lof_store != ((v & 0x8000) ? 1 : 0)) {
		lof_changed = 1;
		lof_store = (v & 0x8000) ? 1 : 0;
	}
	if (currprefs.chipset_mask & CSMASK_ECS_AGNUS) {
		lol = (v & 0x0080) ? 1 : 0;
		if (!islinetoggle ())
			lol = 0;
	}
	if (lof_changed)
		return;
	vpos &= 0x00ff;
	v &= 7;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		v &= 1;
	vpos |= v << 8;
}

static void VHPOSW (uae_u16 v)
{
#if 0
	if (M68K_GETPC < 0xf00000 || 1)
		write_log (_T("VHPOSW %04X PC=%08x\n"), v, M68K_GETPC);
#endif
	v >>= 8; // lets ignore hpos for now
	vpos &= 0xff00;
	vpos |= v;
}

STATIC_INLINE uae_u16 VHPOSR (void)
{
	uae_u16 vp = GETVPOS ();
	uae_u16 hp = GETHPOS ();

	hp += HPOS_OFFSET;
	if (hp >= maxhpos) {
		hp -= maxhpos;
		vp++;
		if (vp >= maxvpos + lof_store)
			vp = 0;
	}
	if (HPOS_OFFSET) {
		hp += 1;
		if (hp >= maxhpos)
			hp -= maxhpos;
	}

	vp <<= 8;
	vp |= hp;
	if (currprefs.cpu_model >= 68020)
		hsyncdelay ();
#if 0
	if (M68K_GETPC < 0x00f00000 || M68K_GETPC >= 0x10000000)
		write_log (_T("VPOS %04x %04x at %08x\n"), VPOSR (), vp, M68K_GETPC);
#endif
	return vp;
}

static int test_copper_dangerous (unsigned int address)
{
	int addr = address & 0x01fe;
	if (addr < ((copcon & 2) ? ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? 0 : 0x40) : 0x80)) {
		cop_state.state = COP_stop;
		copper_enabled_thisline = 0;
		unset_special (SPCFLAG_COPPER);
		return 1;
	}
	return 0;
}

static void immediate_copper (int num)
{
	int pos = 0;
	int oldpos = 0;

	cop_state.state = COP_stop;
	cop_state.vpos = vpos;
	cop_state.hpos = current_hpos () & ~1;
	cop_state.ip = num == 1 ? cop1lc : cop2lc;

	while (pos < (maxvpos << 5)) {
		if (oldpos > pos)
			pos = oldpos;
		if (!dmaen(DMA_COPPER))
			break;
		if (cop_state.ip >= currprefs.chipmem_size)
			break;
		pos++;
		oldpos = pos;
		cop_state.i1 = chipmem_wget_indirect (cop_state.ip);
		cop_state.i2 = chipmem_wget_indirect (cop_state.ip + 2);
		cop_state.ip += 4;
		if (!(cop_state.i1 & 1)) { // move
			cop_state.i1 &= 0x1fe;
			if (cop_state.i1 == 0x88) {
				cop_state.ip = cop1lc;
				continue;
			}
			if (cop_state.i1 == 0x8a) {
				cop_state.ip = cop2lc;
				continue;
			}
			if (test_copper_dangerous (cop_state.i1))
				break;
			custom_wput_1 (0, cop_state.i1, cop_state.i2, 0);
		} else { // wait or skip
			if ((cop_state.i1 >> 8) > ((pos >> 5) & 0xff))
				pos = (((pos >> 5) & 0x100) | ((cop_state.i1 >> 8)) << 5) | ((cop_state.i1 & 0xff) >> 3);
			if (cop_state.i1 >= 0xffdf && cop_state.i2 == 0xfffe)
				break;
		}
	}
	cop_state.state = COP_stop;
	unset_special (SPCFLAG_COPPER);
}

STATIC_INLINE void COP1LCH (uae_u16 v)
{
	cop1lc = (cop1lc & 0xffff) | ((uae_u32)v << 16);
}
STATIC_INLINE void COP1LCL (uae_u16 v)
{
	cop1lc = (cop1lc & ~0xffff) | (v & 0xfffe);
}
STATIC_INLINE void COP2LCH (uae_u16 v)
{
	cop2lc = (cop2lc & 0xffff) | ((uae_u32)v << 16);
}
STATIC_INLINE void COP2LCL (uae_u16 v)
{
	cop2lc = (cop2lc & ~0xffff) | (v & 0xfffe);
}

static void compute_spcflag_copper (int hpos);

// vblank = copper starts at hpos=2
// normal COPJMP write: takes 2 more cycles
static void COPJMP (int num, int vblank)
{
	int oldstrobe = cop_state.strobe;

#if CUSTOM_DEBUG > 0
	if (dmaen (DMA_COPPER) && (cop_state.saved_i1 != 0xffff || cop_state.saved_i2 != 0xfffe))
		write_log (_T("vblank without copper ending %08x (%08x %08x)\n"), cop_state.ip, cop1lc, cop2lc);
#endif

	unset_special (SPCFLAG_COPPER);
	cop_state.ignore_next = 0;
	if (!oldstrobe)
		cop_state.state_prev = cop_state.state;
	cop_state.state = vblank ? COP_start_delay : (copper_access ? COP_strobe_delay1 : COP_strobe_extra);
	cop_state.vpos = vpos;
	cop_state.hpos = current_hpos () & ~1;
	copper_enabled_thisline = 0;
	cop_state.strobe = num;

	if (nocustom ()) {
		immediate_copper (num);
		return;
	}

	if (dmaen (DMA_COPPER)) {
		compute_spcflag_copper (current_hpos ());
	} else if (oldstrobe > 0 && oldstrobe != num && cop_state.state_prev == COP_wait) {
		/* dma disabled, copper idle and accessed both COPxJMPs -> copper stops! */
		cop_state.state = COP_stop;
	}
}

STATIC_INLINE void COPCON (uae_u16 a)
{
	copcon = a;
}

static void DMACON (int hpos, uae_u16 v)
{
	int oldcop, newcop;
	uae_u16 changed;

	uae_u16 oldcon = dmacon;

	decide_line (hpos);
	decide_fetch (hpos);
	decide_blitter (hpos);

	setclr (&dmacon, v);
	dmacon &= 0x1FFF;

	changed = dmacon ^ oldcon;

	oldcop = (oldcon & DMA_COPPER) && (oldcon & DMA_MASTER);
	newcop = (dmacon & DMA_COPPER) && (dmacon & DMA_MASTER);

	if (oldcop != newcop) {
		if (newcop && !oldcop) {
			compute_spcflag_copper (hpos);
		} else if (!newcop) {
			copper_enabled_thisline = 0;
			unset_special (SPCFLAG_COPPER);
		}
	}
	if ((dmacon & DMA_BLITPRI) > (oldcon & DMA_BLITPRI) && bltstate != BLT_done)
		set_special (SPCFLAG_BLTNASTY);

	if (dmaen (DMA_BLITTER) && bltstate == BLT_init)
		bltstate = BLT_work;

	if ((dmacon & (DMA_BLITPRI | DMA_BLITTER | DMA_MASTER)) != (DMA_BLITPRI | DMA_BLITTER | DMA_MASTER))
		unset_special (SPCFLAG_BLTNASTY);

	if (changed & (DMA_MASTER | 0x0f))
		audio_state_machine ();

	if (changed & (DMA_MASTER | DMA_BITPLANE)) {
		ddf_change = vpos;
		if (dmaen (DMA_BITPLANE))
			maybe_start_bpl_dma (hpos);
	}

	events_schedule();
}


void MISC_handler (void)
{
	static bool dorecheck;
	bool recheck;
	int i;
	evt mintime;
	evt ct = get_cycles ();
	static int recursive;

	if (recursive) {
		dorecheck = true;
		return;
	}
	recursive++;
	eventtab[ev_misc].active = 0;
	recheck = true;
	while (recheck) {
		recheck = false;
		mintime = ~0L;
		for (i = 0; i < ev2_max; i++) {
			if (eventtab2[i].active) {
				if (eventtab2[i].evtime == ct) {
					eventtab2[i].active = false;
					eventtab2[i].handler (eventtab2[i].data);
					if (dorecheck || eventtab2[i].active) {
						recheck = true;
						dorecheck = false;
					}
				} else {
					evt eventtime = eventtab2[i].evtime - ct;
					if (eventtime < mintime)
						mintime = eventtime;
				}
			}
		}
	}
	if (mintime != ~0L) {
		eventtab[ev_misc].active = true;
		eventtab[ev_misc].oldcycles = ct;
		eventtab[ev_misc].evtime = ct + mintime;
		events_schedule ();
	}
	recursive--;
}

static int irq_nmi;

void NMI_delayed (void)
{
	irq_nmi = 1;
}

static uae_u16 intreq_internal, intena_internal;

int intlev (void)
{
	uae_u16 imask = intreq_internal & intena_internal;
	if (irq_nmi) {
		irq_nmi = 0;
		return 7;
	}
	if (!(imask && (intena_internal & 0x4000)))
		return -1;
	if (imask & (0x4000 | 0x2000))						// 13 14
		return 6;
	if (imask & (0x1000 | 0x0800))						// 11 12
		return 5;
	if (imask & (0x0400 | 0x0200 | 0x0100 | 0x0080))	// 7 8 9 10
		return 4;
	if (imask & (0x0040 | 0x0020 | 0x0010))				// 4 5 6
		return 3;
	if (imask & 0x0008)									// 3
		return 2;
	if (imask & (0x0001 | 0x0002 | 0x0004))				// 0 1 2
		return 1;
	return -1;
}

#define INT_PROCESSING_DELAY (3 * CYCLE_UNIT)
STATIC_INLINE int use_eventmode (uae_u16 v)
{
	if (!currprefs.cpu_cycle_exact)
		return 0;
	if (currprefs.cpu_cycle_exact && currprefs.cpu_model <= 68020)
		return 1;
	if (v & 0x8000)
		return 1;
	return 0;
}

static void send_interrupt_do (uae_u32 v)
{
	INTREQ_0 (0x8000 | (1 << v));
}

void send_interrupt (int num, int delay)
{
	if (use_eventmode (0x8000) && delay > 0) {
		if (!(intreq & (1 << num)))
			event2_newevent_xx (-1, delay, num, send_interrupt_do);
	} else {
		send_interrupt_do (num);
	}
}

static void send_intena_do (uae_u32 v)
{
	intena_internal = v;
	doint ();
}
static void send_intreq_do (uae_u32 v)
{
	intreq_internal = v;
	doint ();
}

static void INTENA (uae_u16 v)
{
	uae_u16 old = intena;
	setclr (&intena, v);

	if (!(v & 0x8000) && old == intena)
		return;
	if (use_eventmode (v)) {
		event2_newevent_xx (-1, INT_PROCESSING_DELAY, intena, send_intena_do);
	} else {
		intena_internal = intena;
		if (v & 0x8000)
			doint ();
	}
#if 0
	if (v & 0x40)
		write_log (_T("INTENA %04X (%04X) %p\n"), intena, v, M68K_GETPC);
#endif
}

void INTREQ_nodelay (uae_u16 v)
{
	setclr (&intreq, v);
	setclr (&intreq_internal, v);
	doint ();
}

void INTREQ_f (uae_u16 v)
{
	if (use_eventmode (v)) {
		send_intreq_do (v);
	} else {
		setclr (&intreq, v);
		setclr (&intreq_internal, v);
	}
}

void INTREQ_0 (uae_u16 v)
{
#if 0
	if (!(v & 0x8000) && (v & (0x80 | 0x100 | 0x200 | 0x400)))
		write_log (_T("audirq clear %d\n"), v);
#endif

	uae_u16 old = intreq;
	setclr (&intreq, v);

	if (!(v & 0x8000) && old == intreq)
		return;

	if (use_eventmode (v)) {
		event2_newevent_xx (-1, INT_PROCESSING_DELAY, intreq, send_intreq_do);
	} else {
		intreq_internal = intreq;
		if (v & 0x8000)
			doint ();
	}
}

void INTREQ (uae_u16 data)
{
	INTREQ_0 (data);
	serial_check_irq ();
	rethink_cias ();
#ifdef A2065
	rethink_a2065 ();
#endif
#ifdef A2091
	rethink_a2091 ();
#endif
#ifdef CDTV
	rethink_cdtv ();
#endif
#ifdef CD32
	rethink_akiko ();
#endif
	rethink_gayle ();
}

static void ADKCON (int hpos, uae_u16 v)
{
	if (currprefs.produce_sound > 0)
		update_audio ();
	DISK_update (hpos);
	DISK_update_adkcon (hpos, v);
	setclr (&adkcon, v);
	audio_update_adkmasks ();
	if ((v >> 11) & 1)
		serial_uartbreak ((adkcon >> 11) & 1);
}

static void BEAMCON0 (uae_u16 v)
{
	if (currprefs.chipset_mask & CSMASK_ECS_AGNUS) {
		if (!(currprefs.chipset_mask & CSMASK_ECS_DENISE))
			v &= 0x20;
		if (v != new_beamcon0) {
			new_beamcon0 = v;
			if (v & ~0x20)
				write_log (_T("warning: %04X written to BEAMCON0 PC=%08X\n"), v, M68K_GETPC);
		}
	}
}

#ifndef CUSTOM_SIMPLE

static void varsync (void)
{
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		return;
#ifdef PICASSO96
	if (picasso_on && p96refresh_active) {
		vtotal = p96refresh_active;
		return;
	}
#endif
	if (!(beamcon0 & 0x80))
		return;
	vpos_count = 0;
	dumpsync ();
}

#ifdef PICASSO96
void set_picasso_hack_rate (int hz)
{
	if (!picasso_on)
		return;
	vpos_count = 0;
	p96refresh_active = maxvpos_stored * vblank_hz_stored / hz;
	if (!currprefs.cs_ciaatod)
		changed_prefs.cs_ciaatod = currprefs.cs_ciaatod = currprefs.ntscmode ? 2 : 1;
	if (p96refresh_active > 0) {
		new_beamcon0 |= 0x80;
	}
}
#endif

#endif

static void BPLxPTH (int hpos, uae_u16 v, int num)
{
	decide_line (hpos);
	decide_fetch (hpos);
	bplpt[num] = (bplpt[num] & 0x0000ffff) | ((uae_u32)v << 16);
	bplptx[num] = (bplptx[num] & 0x0000ffff) | ((uae_u32)v << 16);
	//write_log (_T("%d:%d:BPL%dPTH %08X COP=%08x\n"), hpos, vpos, num, bplpt[num], cop_state.ip);
}
static void BPLxPTL (int hpos, uae_u16 v, int num)
{
	decide_line (hpos);
	decide_fetch (hpos);
	/* chipset feature: BPLxPTL write and next cycle doing DMA fetch using same pointer register ->
	 * this write goes nowhere (same happens with all DMA channels, not just BPL)
	 * (intro MoreNewStuffy by PlasmaForce)
	 */
	/* only detect copper accesses to prevent too fast CPU mode glitches */
	if (copper_access && is_bitplane_dma (hpos + 1) == num + 1)
		return;
	bplpt[num] = (bplpt[num] & 0xffff0000) | (v & 0x0000fffe);
	bplptx[num] = (bplptx[num] & 0xffff0000) | (v & 0x0000fffe);
	//write_log (_T("%d:%d:BPL%dPTL %08X COP=%08x\n"), hpos, vpos, num, bplpt[num], cop_state.ip);
}

static void BPLCON0_Denise (int hpos, uae_u16 v)
{
	if (! (currprefs.chipset_mask & CSMASK_ECS_DENISE))
		v &= ~0x00F1;
	else if (! (currprefs.chipset_mask & CSMASK_AGA))
		v &= ~0x00B0;
	v &= ~(0x0200 | 0x0100 | 0x0080 | 0x0020);
#if SPRBORDER
	v |= 1;
#endif

	if (bplcon0d == v)
		return;

	bplcon0dd = -1;
	// fake unused 0x0080 bit as an EHB bit (see below)
	if (isehb (bplcon0d, bplcon2))
		v |= 0x80;

	record_register_change (hpos, 0x100, (bplcon0d & ~(0x800 | 0x400 | 0x80)) | (v & (0x0800 | 0x400 | 0x80)));

	bplcon0d = v & ~0x80;

#ifdef ECS_DENISE
	if (currprefs.chipset_mask & CSMASK_ECS_DENISE) {
		decide_sprites (hpos);
		sprres = expand_sprres (v, bplcon3);
	}
#endif
	if (thisline_decision.plfleft == -1)
		update_denise (hpos);
}

static void BPLCON0 (int hpos, uae_u16 v)
{
	if (! (currprefs.chipset_mask & CSMASK_ECS_DENISE))
		v &= ~0x00F1;
	else if (! (currprefs.chipset_mask & CSMASK_AGA))
		v &= ~0x00B0;
	v &= ~(0x0200 | 0x0100 | 0x0080 | 0x0020);

#if SPRBORDER
	v |= 1;
#endif
	if (bplcon0 == v)
		return;

	if (!issyncstopped ()) {
		vpos_previous = vpos;
		hpos_previous = hpos;
	}

	bplcon0 = v;

	bpldmainitdelay (hpos);

	if (thisline_decision.plfleft == -1)
		BPLCON0_Denise (hpos, v);
}

STATIC_INLINE void BPLCON1 (int hpos, uae_u16 v)
{
	if (!(currprefs.chipset_mask & CSMASK_AGA))
		v &= 0xff;
	if (bplcon1 == v)
		return;
	ddf_change = vpos;
	decide_line (hpos);
	decide_fetch (hpos);
	bplcon1_hpos = hpos;
	bplcon1 = v;
}

STATIC_INLINE void BPLCON2 (int hpos, uae_u16 v)
{
	if (!(currprefs.chipset_mask & CSMASK_AGA))
		v &= 0x7f;
	if (bplcon2 == v)
		return;
	decide_line (hpos);
	bplcon2 = v;
	record_register_change (hpos, 0x104, v);
}

#ifdef ECS_DENISE
STATIC_INLINE void BPLCON3 (int hpos, uae_u16 v)
{
	if (!(currprefs.chipset_mask & CSMASK_ECS_DENISE))
		return;
	if (!(currprefs.chipset_mask & CSMASK_AGA)) {
		v &= 0x003f;
		v |= 0x0c00;
	}
#if SPRBORDER
	v |= 2;
#endif
	if (bplcon3 == v)
		return;
	decide_line (hpos);
	decide_sprites (hpos);
	bplcon3 = v;
	sprres = expand_sprres (bplcon0, bplcon3);
	record_register_change (hpos, 0x106, v);
}
#endif
#ifdef AGA
STATIC_INLINE void BPLCON4 (int hpos, uae_u16 v)
{
	if (!(currprefs.chipset_mask & CSMASK_AGA))
		return;
	if (bplcon4 == v)
		return;
	decide_line (hpos);
	bplcon4 = v;
	record_register_change (hpos, 0x10c, v);
}
#endif

static void BPL1MOD (int hpos, uae_u16 v)
{
	v &= ~1;
	if ((uae_s16)bpl1mod == (uae_s16)v)
		return;
	decide_line (hpos);
	decide_fetch (hpos);
	bpl1mod = v;
}

static void BPL2MOD (int hpos, uae_u16 v)
{
	v &= ~1;
	if ((uae_s16)bpl2mod == (uae_s16)v)
		return;
	decide_line (hpos);
	decide_fetch (hpos);
	bpl2mod = v;
}

/* needed in special OCS/ECS "7-plane" mode. */
/* (in reality only BPL0DAT, BPL5DAT and BPL6DAT needed) */
static void BPLxDAT (int hpos, int num, uae_u16 v)
{
	decide_line (hpos);
	decide_fetch (hpos);
	bplxdat[num] = v;
	if (num == 0) {
		bpl1dat_written = 1;
		if (thisline_decision.plfleft == -1) {
			start_bpl_processing (hpos, hpos, false);
			thisline_decision.plfleft = hpos;
			compute_delay_offset ();
		}
	}
}

static void DIWSTRT (int hpos, uae_u16 v)
{
	if (diwstrt == v && ! diwhigh_written)
		return;
	decide_diw (hpos);
	decide_line (hpos);
	diwhigh_written = 0;
	diwstrt = v;
	calcdiw ();
}

static void DIWSTOP (int hpos, uae_u16 v)
{
	if (diwstop == v && ! diwhigh_written)
		return;
	decide_diw (hpos);
	decide_line (hpos);
	diwhigh_written = 0;
	diwstop = v;
	calcdiw ();
}

static void DIWHIGH (int hpos, uae_u16 v)
{
	if (!(currprefs.chipset_mask & (CSMASK_ECS_DENISE | CSMASK_ECS_AGNUS)))
		return;
	if (!(currprefs.chipset_mask & CSMASK_AGA))
		v &= ~(0x0008 | 0x0010 | 0x1000 | 0x0800);
	v &= ~(0x8000 | 0x4000 | 0x0080 | 0x0040);
	if (diwhigh_written && diwhigh == v)
		return;
	decide_line (hpos);
	diwhigh_written = 1;
	diwhigh = v;
	calcdiw ();
}

static void DDFSTRT (int hpos, uae_u16 v)
{
	v &= 0xfe;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		v &= 0xfc;
	if (ddfstrt == v && hpos + 2 != ddfstrt)
		return;
	ddf_change = vpos;
	decide_line (hpos);
	ddfstrt_old_hpos = hpos;
	ddfstrt = v;
	calcdiw ();
	if (ddfstop > 0xD4 && (ddfstrt & 4) == 4) {
		static int last_warned;
		last_warned = (last_warned + 1) & 4095;
		if (last_warned == 0)
			write_log (_T("WARNING! Very strange DDF values (%x %x).\n"), ddfstrt, ddfstop);
	}
}

static void DDFSTOP (int hpos, uae_u16 v)
{
	v &= 0xfe;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		v &= 0xfc;
	if (ddfstop == v && hpos + 2 != ddfstop)
		return;
	ddf_change = vpos;
	decide_line (hpos);
	decide_fetch (hpos);
	decide_blitter (hpos);
	ddfstop = v;
	calcdiw ();
	if (fetch_state != fetch_not_started)
		estimate_last_fetch_cycle (hpos);
	if (ddfstop > 0xD4 && (ddfstrt & 4) == 4) {
		static int last_warned;
		if (last_warned == 0)
			write_log (_T("WARNING! Very strange DDF values (%x).\n"), ddfstop);
		last_warned = (last_warned + 1) & 4095;
	}
}

static void FMODE (int hpos, uae_u16 v)
{
	if (! (currprefs.chipset_mask & CSMASK_AGA))
		v = 0;
	v &= 0xC00F;
	if (fmode == v)
		return;
	ddf_change = vpos;
	fmode = v;
	sprite_width = GET_SPRITEWIDTH (fmode);
	bpldmainitdelay (hpos);
}

static void FNULL (uae_u16 v)
{

}

static void BLTADAT (int hpos, uae_u16 v)
{
	maybe_blit (hpos, 0);

	blt_info.bltadat = v;
}
/*
* "Loading data shifts it immediately" says the HRM. Well, that may
* be true for BLTBDAT, but not for BLTADAT - it appears the A data must be
* loaded for every word so that AFWM and ALWM can be applied.
*/
static void BLTBDAT (int hpos, uae_u16 v)
{
	maybe_blit (hpos, 0);

	if (bltcon1 & 2)
		blt_info.bltbhold = v << (bltcon1 >> 12);
	else
		blt_info.bltbhold = v >> (bltcon1 >> 12);
	blt_info.bltbdat = v;
}
static void BLTCDAT (int hpos, uae_u16 v) { maybe_blit (hpos, 0); blt_info.bltcdat = v; reset_blit (0); }

static void BLTAMOD (int hpos, uae_u16 v) { maybe_blit (hpos, 1); blt_info.bltamod = (uae_s16)(v & 0xFFFE); reset_blit (0); }
static void BLTBMOD (int hpos, uae_u16 v) { maybe_blit (hpos, 1); blt_info.bltbmod = (uae_s16)(v & 0xFFFE); reset_blit (0); }
static void BLTCMOD (int hpos, uae_u16 v) { maybe_blit (hpos, 1); blt_info.bltcmod = (uae_s16)(v & 0xFFFE); reset_blit (0); }
static void BLTDMOD (int hpos, uae_u16 v) { maybe_blit (hpos, 1); blt_info.bltdmod = (uae_s16)(v & 0xFFFE); reset_blit (0); }

static void BLTCON0 (int hpos, uae_u16 v) { maybe_blit (hpos, 2); bltcon0 = v; reset_blit (1); }
/* The next category is "Most useless hardware register".
* And the winner is... */
static void BLTCON0L (int hpos, uae_u16 v)
{
	if (! (currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		return; // ei voittoa.
	maybe_blit (hpos, 2); bltcon0 = (bltcon0 & 0xFF00) | (v & 0xFF);
	reset_blit (1);
}
static void BLTCON1 (int hpos, uae_u16 v) { maybe_blit (hpos, 2); bltcon1 = v; reset_blit (2); }

static void BLTAFWM (int hpos, uae_u16 v) { maybe_blit (hpos, 2); blt_info.bltafwm = v; reset_blit (0); }
static void BLTALWM (int hpos, uae_u16 v) { maybe_blit (hpos, 2); blt_info.bltalwm = v; reset_blit (0); }

static void BLTAPTH (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltapt = (bltapt & 0xffff) | ((uae_u32)v << 16); }
static void BLTAPTL (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltapt = (bltapt & ~0xffff) | (v & 0xFFFE); }
static void BLTBPTH (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltbpt = (bltbpt & 0xffff) | ((uae_u32)v << 16); }
static void BLTBPTL (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltbpt = (bltbpt & ~0xffff) | (v & 0xFFFE); }
static void BLTCPTH (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltcpt = (bltcpt & 0xffff) | ((uae_u32)v << 16); }
static void BLTCPTL (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltcpt = (bltcpt & ~0xffff) | (v & 0xFFFE); }
static void BLTDPTH (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltdpt = (bltdpt & 0xffff) | ((uae_u32)v << 16); }
static void BLTDPTL (int hpos, uae_u16 v) { maybe_blit (hpos, 0); bltdpt = (bltdpt & ~0xffff) | (v & 0xFFFE); }

static void BLTSIZE (int hpos, uae_u16 v)
{
	maybe_blit (hpos, 0);

	blt_info.vblitsize = v >> 6;
	blt_info.hblitsize = v & 0x3F;
	if (!blt_info.vblitsize)
		blt_info.vblitsize = 1024;
	if (!blt_info.hblitsize)
		blt_info.hblitsize = 64;
	do_blitter (hpos, copper_access);
}

static void BLTSIZV (int hpos, uae_u16 v)
{
	if (! (currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		return;
	maybe_blit (hpos, 0);
	blt_info.vblitsize = v & 0x7FFF;
}

static void BLTSIZH (int hpos, uae_u16 v)
{
	if (! (currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		return;
	maybe_blit (hpos, 0);
	blt_info.hblitsize = v & 0x7FF;
	if (!blt_info.vblitsize)
		blt_info.vblitsize = 0x8000;
	if (!blt_info.hblitsize)
		blt_info.hblitsize = 0x0800;
	do_blitter (hpos, copper_access);
}

STATIC_INLINE void spr_arm (int num, int state)
{
	switch (state) {
	case 0:
		nr_armed -= spr[num].armed;
		spr[num].armed = 0;
		break;
	default:
		nr_armed += 1 - spr[num].armed;
		spr[num].armed = 1;
		break;
	}
}

STATIC_INLINE void sprstartstop (struct sprite *s)
{
	if (vpos == s->vstart)
		s->dmastate = 1;
	if (vpos == s->vstop)
		s->dmastate = 0;
}

STATIC_INLINE void SPRxCTLPOS (int num)
{
	int sprxp;
	struct sprite *s = &spr[num];

	sprstartstop (s);
	sprxp = (sprpos[num] & 0xFF) * 2 + (sprctl[num] & 1);
	sprxp <<= sprite_buffer_res;
	/* Quite a bit salad in this register... */
	if (0) {
	}
#ifdef AGA
	else if (currprefs.chipset_mask & CSMASK_AGA) {
		sprxp |= ((sprctl[num] >> 3) & 3) >> (RES_MAX - sprite_buffer_res);
		s->dblscan = sprpos[num] & 0x80;
	}
#endif
#ifdef ECS_DENISE
	else if (currprefs.chipset_mask & CSMASK_ECS_DENISE) {
		sprxp |= ((sprctl[num] >> 3) & 2) >> (RES_MAX - sprite_buffer_res);
	}
#endif
	s->xpos = sprxp;
	s->vstart = (sprpos[num] >> 8) | ((sprctl[num] << 6) & 0x100);
	s->vstop = (sprctl[num] >> 8) | ((sprctl[num] << 7) & 0x100);
	if (currprefs.chipset_mask & CSMASK_ECS_AGNUS) {
		s->vstart |= (sprctl[num] << 3) & 0x200;
		s->vstop |= (sprctl[num] << 4) & 0x200;
	}
	sprstartstop (s);
}

STATIC_INLINE void SPRxCTL_1 (uae_u16 v, int num, int hpos)
{
	struct sprite *s = &spr[num];
	sprctl[num] = v;
	spr_arm (num, 0);
	SPRxCTLPOS (num);
#if SPRITE_DEBUG > 0
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
		write_log (_T("%d:%d:SPR%dCTL %04X P=%06X VSTRT=%d VSTOP=%d HSTRT=%d D=%d A=%d CP=%x PC=%x\n"),
			vpos, hpos, num, v, s->pt, s->vstart, s->vstop, s->xpos, spr[num].dmastate, spr[num].armed, cop_state.ip, M68K_GETPC);
	}
#endif

}
STATIC_INLINE void SPRxPOS_1 (uae_u16 v, int num, int hpos)
{
	struct sprite *s = &spr[num];
	sprpos[num] = v;
	SPRxCTLPOS (num);
#if SPRITE_DEBUG > 0
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
		write_log (_T("%d:%d:SPR%dPOS %04X P=%06X VSTRT=%d VSTOP=%d HSTRT=%d D=%d A=%d CP=%x PC=%x\n"),
			vpos, hpos, num, v, s->pt, s->vstart, s->vstop, s->xpos, spr[num].dmastate, spr[num].armed, cop_state.ip, M68K_GETPC);
	}
#endif
}
STATIC_INLINE void SPRxDATA_1 (uae_u16 v, int num, int hpos)
{
	sprdata[num][0] = v;
#ifdef AGA
	sprdata[num][1] = v;
	sprdata[num][2] = v;
	sprdata[num][3] = v;
#endif
	spr_arm (num, 1);
#if SPRITE_DEBUG > 1
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
		write_log (_T("%d:%d:SPR%dDATA %04X P=%06X D=%d A=%d PC=%x\n"),
			vpos, hpos, num, v, spr[num].pt, spr[num].dmastate, spr[num].armed, M68K_GETPC);
	}
#endif
}
STATIC_INLINE void SPRxDATB_1 (uae_u16 v, int num, int hpos)
{
	sprdatb[num][0] = v;
#ifdef AGA
	sprdatb[num][1] = v;
	sprdatb[num][2] = v;
	sprdatb[num][3] = v;
#endif
#if SPRITE_DEBUG > 1
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
		write_log (_T("%d:%d:SPR%dDATB %04X P=%06X D=%d A=%d PC=%x\n"),
			vpos, hpos, num, v, spr[num].pt, spr[num].dmastate, spr[num].armed, M68K_GETPC);
	}
#endif
}
static void SPRxDATA (int hpos, uae_u16 v, int num) { decide_sprites (hpos); SPRxDATA_1 (v, num, hpos); }
static void SPRxDATB (int hpos, uae_u16 v, int num) { decide_sprites (hpos); SPRxDATB_1 (v, num, hpos); }
static void SPRxCTL (int hpos, uae_u16 v, int num) { decide_sprites (hpos); SPRxCTL_1 (v, num, hpos); }
static void SPRxPOS (int hpos, uae_u16 v, int num) { decide_sprites (hpos); SPRxPOS_1 (v, num, hpos); }
static void SPRxPTH (int hpos, uae_u16 v, int num)
{
	decide_sprites (hpos);
	if (hpos - 1 != spr[num].ptxhpos) {
		spr[num].pt &= 0xffff;
		spr[num].pt |= (uae_u32)v << 16;
	}
#if SPRITE_DEBUG > 0
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
		write_log (_T("%d:%d:SPR%dPTH %06X\n"), vpos, hpos, num, spr[num].pt);
	}
#endif
}
static void SPRxPTL (int hpos, uae_u16 v, int num)
{
	decide_sprites (hpos);
	if (hpos - 1 != spr[num].ptxhpos) {
		spr[num].pt &= ~0xffff;
		spr[num].pt |= v & ~1;
	}
#if SPRITE_DEBUG > 0
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
		write_log (_T("%d:%d:SPR%dPTL %06X\n"), vpos, hpos, num, spr[num].pt);
	}
#endif
}

static void CLXCON (uae_u16 v)
{
	clxcon = v;
	clxcon_bpl_enable = (v >> 6) & 63;
	clxcon_bpl_match = v & 63;
}

static void CLXCON2 (uae_u16 v)
{
	if (!(currprefs.chipset_mask & CSMASK_AGA))
		return;
	clxcon2 = v;
	clxcon_bpl_enable |= v & (0x40 | 0x80);
	clxcon_bpl_match |= (v & (0x01 | 0x02)) << 6;
}

static uae_u16 CLXDAT (void)
{
	uae_u16 v = clxdat | 0x8000;
	clxdat = 0;
	return v;
}

#ifdef AGA

void dump_aga_custom (void)
{
	int c1, c2, c3, c4;
	uae_u32 rgb1, rgb2, rgb3, rgb4;

	for (c1 = 0; c1 < 64; c1++) {
		c2 = c1 + 64;
		c3 = c2 + 64;
		c4 = c3 + 64;
		rgb1 = current_colors.color_regs_aga[c1] | (color_regs_aga_genlock[c1] << 31);
		rgb2 = current_colors.color_regs_aga[c2] | (color_regs_aga_genlock[c2] << 31);
		rgb3 = current_colors.color_regs_aga[c3] | (color_regs_aga_genlock[c3] << 31);
		rgb4 = current_colors.color_regs_aga[c4] | (color_regs_aga_genlock[c4] << 31);
		console_out_f (_T("%3d %08X %3d %08X %3d %08X %3d %08X\n"),
			c1, rgb1, c2, rgb2, c3, rgb3, c4, rgb4);
	}
}

static uae_u16 COLOR_READ (int num)
{
	int cr, cg, cb, colreg;
	uae_u16 cval;

	if (!(currprefs.chipset_mask & CSMASK_AGA) || !(bplcon2 & 0x0100))
		return 0xffff;

	colreg = ((bplcon3 >> 13) & 7) * 32 + num;
	cr = current_colors.color_regs_aga[colreg] >> 16;
	cg = (current_colors.color_regs_aga[colreg] >> 8) & 0xFF;
	cb = current_colors.color_regs_aga[colreg] & 0xFF;
	if (bplcon3 & 0x200) {
		cval = ((cr & 15) << 8) | ((cg & 15) << 4) | ((cb & 15) << 0);
	} else {
		cval = ((cr >> 4) << 8) | ((cg >> 4) << 4) | ((cb >> 4) << 0);
		if (color_regs_aga_genlock[num])
			cval |= 0x8000;
	}
	return cval;
}
#endif

static void COLOR_WRITE (int hpos, uae_u16 v, int num)
{
	v &= 0xFFF;
#ifdef AGA
	if (currprefs.chipset_mask & CSMASK_AGA) {
		int r,g,b;
		int cr,cg,cb;
		int colreg;
		uae_u32 cval;

		/* writing is disabled when RDRAM=1 */
		if (bplcon2 & 0x0100)
			return;

		colreg = ((bplcon3 >> 13) & 7) * 32 + num;
		r = (v & 0xF00) >> 8;
		g = (v & 0xF0) >> 4;
		b = (v & 0xF) >> 0;
		cr = current_colors.color_regs_aga[colreg] >> 16;
		cg = (current_colors.color_regs_aga[colreg] >> 8) & 0xFF;
		cb = current_colors.color_regs_aga[colreg] & 0xFF;

		if (bplcon3 & 0x200) {
			cr &= 0xF0; cr |= r;
			cg &= 0xF0; cg |= g;
			cb &= 0xF0; cb |= b;
		} else {
			cr = r + (r << 4);
			cg = g + (g << 4);
			cb = b + (b << 4);
			color_regs_aga_genlock[colreg] = v >> 15;
		}
		cval = (cr << 16) | (cg << 8) | cb;
		if (cval == current_colors.color_regs_aga[colreg])
			return;

		/* Call this with the old table still intact. */
		record_color_change (hpos, colreg, cval);
		remembered_color_entry = -1;
		current_colors.color_regs_aga[colreg] = cval;
		current_colors.acolors[colreg] = getxcolor (cval);
	} else {
#endif
		if (current_colors.color_regs_ecs[num] == v)
			return;
		/* Call this with the old table still intact. */
		record_color_change (hpos, num, v);
		remembered_color_entry = -1;
		current_colors.color_regs_ecs[num] = v;
		current_colors.acolors[num] = getxcolor (v);
#ifdef AGA
	}
#endif
}

/* The copper code.  The biggest nightmare in the whole emulator.

Alright.  The current theory:
1. Copper moves happen 2 cycles after state READ2 is reached.
It can't happen immediately when we reach READ2, because the
data needs time to get back from the bus.  An additional 2
cycles are needed for non-Agnus registers, to take into account
the delay for moving data from chip to chip.
2. As stated in the HRM, a WAIT really does need an extra cycle
to wake up.  This is implemented by _not_ falling through from
a successful wait to READ1, but by starting the next cycle.
(Note: the extra cycle for the WAIT apparently really needs a
free cycle; i.e. contention with the bitplane fetch can slow
it down).
3. Apparently, to compensate for the extra wake up cycle, a WAIT
will use the _incremented_ horizontal position, so the WAIT
cycle normally finishes two clocks earlier than the position
it was waiting for.  The extra cycle then takes us to the
position that was waited for.
If the earlier cycle is busy with a bitplane, things change a bit.
E.g., waiting for position 0x50 in a 6 plane display: In cycle
0x4e, we fetch BPL5, so the wait wakes up in 0x50, the extra cycle
takes us to 0x54 (since 0x52 is busy), then we have READ1/READ2,
and the next register write is at 0x5c.
4. The last cycle in a line is not usable for the copper.
5. A 4 cycle delay also applies to the WAIT instruction.  This means
that the second of two back-to-back WAITs (or a WAIT whose
condition is immediately true) takes 8 cycles.
6. This also applies to a SKIP instruction.  The copper does not
fetch the next instruction while waiting for the second word of
a WAIT or a SKIP to arrive.
7. A SKIP also seems to need an unexplained additional two cycles
after its second word arrives; this is _not_ a memory cycle (I
think, the documentation is pretty clear on this).
8. Two additional cycles are inserted when writing to COPJMP1/2.  */

/* Determine which cycles are available for the copper in a display
* with a agiven number of planes.  */

STATIC_INLINE int copper_cant_read (int hpos, int alloc)
{
	if (hpos + 1 >= maxhpos) // first refresh slot
		return 1;
	if ((hpos == maxhpos - 3) && (maxhpos & 1)) {
		if (alloc)
			alloc_cycle (hpos, CYCLE_COPPER);
		return -1;
	}
	return is_bitplane_dma_inline (hpos);
}

static int custom_wput_copper (int hpos, uaecptr addr, uae_u32 value, int noget)
{
	int v;

	value = debug_wputpeekdma (0xdff000 + addr, value);
	copper_access = 1;
	v = custom_wput_1 (hpos, addr, value, noget);
	copper_access = 0;
	return v;
}

static void dump_copper (TCHAR *error, int until_hpos)
{
	write_log (_T("%s: vpos=%d until_hpos=%d\n"),
		error, vpos, until_hpos);
	write_log (_T("cvcmp=%d chcmp=%d chpos=%d cvpos=%d ci1=%04X ci2=%04X\n"),
		cop_state.vcmp,cop_state.hcmp,cop_state.hpos,cop_state.vpos,cop_state.saved_i1,cop_state.saved_i2);
	write_log (_T("cstate=%d ip=%x SPCFLAGS=%x\n"),
		cop_state.state, cop_state.ip, regs.spcflags);
}

// "emulate" chip internal delays, not the right place but fast and 99.9% programs
// use only copper to write BPLCON1 etc.. (exception is HulkaMania/TSP..)
// this table should be filled with zeros and done somewhere else..
static int customdelay[]= {
	1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0, /* 32 0x00 - 0x3e */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x40 - 0x5e */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x60 - 0x7e */
	0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0, /* 0x80 - 0x9e */
	1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0, /* 32 0xa0 - 0xde */
	/* BPLxPTH/BPLxPTL */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 16 */
	/* BPLCON0-3,BPLMOD1-2 */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 16 */
	/* SPRxPTH/SPRxPTL */
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 16 */
	/* SPRxPOS/SPRxCTL/SPRxDATA/SPRxDATB */
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	/* COLORxx */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/* RESERVED */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void copper_write (uae_u32 v)
{
	custom_wput_copper (current_hpos (), v >> 16, v & 0xffff, 0);
}

static void update_copper (int until_hpos)
{
	int vp = vpos & (((cop_state.saved_i2 >> 8) & 0x7F) | 0x80);
	int c_hpos = cop_state.hpos;

	if (nocustom ())
		return;

	if (cop_state.state == COP_wait && vp < cop_state.vcmp) {
		dump_copper (_T("error2"), until_hpos);
		copper_enabled_thisline = 0;
		cop_state.state = COP_stop;
		unset_special (SPCFLAG_COPPER);
		return;
	}

	if (until_hpos <= last_copper_hpos)
		return;

	if (until_hpos > (maxhpos & ~1))
		until_hpos = maxhpos & ~1;

	for (;;) {
		int old_hpos = c_hpos;
		int hp;

		if (c_hpos >= until_hpos)
			break;


		/* So we know about the fetch state.  */
		decide_line (c_hpos);
		decide_fetch (c_hpos);

		if (cop_state.movedelay > 0) {
			cop_state.movedelay--;
			if (cop_state.movedelay == 0) {
				custom_wput_copper (c_hpos, cop_state.moveaddr, cop_state.movedata, 0);
			}
		}

		if ((c_hpos == maxhpos - 3) && (maxhpos & 1))
			c_hpos += 1;
		else
			c_hpos += 2;

		switch (cop_state.state)
		{
		case COP_wait_in2:
			if (copper_cant_read (old_hpos, 0))
				continue;
			cop_state.state = COP_wait1;
			break;
		case COP_skip_in2:
			if (copper_cant_read (old_hpos, 0))
				continue;
			cop_state.state = COP_skip1;
			break;
		case COP_strobe_extra:
			// wait 1 copper cycle doing nothing
			cop_state.state = COP_strobe_delay1;
			break;
		case COP_strobe_delay1:
			// first cycle after COPJMP is just like normal first read cycle
			if (copper_cant_read (old_hpos, 1))
				continue;
			cop_state.state = COP_strobe_delay2;
			alloc_cycle (old_hpos, CYCLE_COPPER);
#ifdef DEBUGGER
			if (debug_dma)
				record_dma (0x8c, chipmem_wget_indirect (cop_state.ip), cop_state.ip, old_hpos, vpos, DMARECORD_COPPER);
#endif
			cop_state.ip += 2;
			break;
		case COP_strobe_delay2:
			// second cycle after COPJMP is like second read cycle except
			// there is 0x1FE as a target register
			// (following word is still read normally and tossed away)
			if (copper_cant_read (old_hpos, 1))
				continue;
			cop_state.state = COP_read1;
			alloc_cycle (old_hpos, CYCLE_COPPER);
			if (debug_dma)
				record_dma (0x1fe, chipmem_wget_indirect (cop_state.ip), cop_state.ip, old_hpos, vpos, DMARECORD_COPPER);
			// next cycle finally reads from new pointer
			if (cop_state.strobe == 1)
				cop_state.ip = cop1lc;
			else
				cop_state.ip = cop2lc;
			cop_state.strobe = 0;
			break;
		case COP_start_delay:
			if (copper_cant_read (old_hpos, 1))
				continue;
			cop_state.state = COP_read1;
			alloc_cycle (old_hpos, CYCLE_COPPER);
			if (debug_dma)
				record_dma (0x1fe, 0, 0xffffffff, old_hpos, vpos, DMARECORD_COPPER);
			cop_state.ip = cop1lc;
			break;

		case COP_read1:
			if (copper_cant_read (old_hpos, 1))
				continue;
			cop_state.i1 = last_custom_value1 = chipmem_wget_indirect (cop_state.ip);
			alloc_cycle (old_hpos, CYCLE_COPPER);
#ifdef DEBUGGER
			if (debug_dma)
				record_dma (0x8c, cop_state.i1, cop_state.ip, old_hpos, vpos, DMARECORD_COPPER);
#endif
			cop_state.ip += 2;
			cop_state.state = COP_read2;
			break;

		case COP_read2:
			if (copper_cant_read (old_hpos, 1))
				continue;
			cop_state.i2 = last_custom_value1 = chipmem_wget_indirect (cop_state.ip);
			alloc_cycle (old_hpos, CYCLE_COPPER);
			cop_state.ip += 2;
			cop_state.saved_i1 = cop_state.i1;
			cop_state.saved_i2 = cop_state.i2;
			cop_state.saved_ip = cop_state.ip;

			if (cop_state.i1 & 1) { // WAIT or SKIP
				cop_state.ignore_next = 0;
				if (cop_state.i2 & 1)
					cop_state.state = COP_skip_in2;
				else
					cop_state.state = COP_wait_in2;
#ifdef DEBUGGER
				if (debug_dma)
					record_dma (0x8c, cop_state.i2, cop_state.ip - 2, old_hpos, vpos, DMARECORD_COPPER);
#endif
			} else { // MOVE
#ifdef DEBUGGER
				uaecptr debugip = cop_state.ip;
#endif
				unsigned int reg = cop_state.i1 & 0x1FE;
				uae_u16 data = cop_state.i2;
				cop_state.state = COP_read1;
#ifdef DEBUGGER
				if (debug_dma)
					record_dma (reg, data, cop_state.ip - 2, old_hpos, vpos, DMARECORD_COPPER);
#endif
				test_copper_dangerous (reg);
				if (! copper_enabled_thisline)
					goto out; // was "dangerous" register -> copper stopped
				if (cop_state.ignore_next) {
					reg = 0x1fe;
					cop_state.ignore_next = 0;
				}

				cop_state.last_write = reg;
				cop_state.last_write_hpos = old_hpos;
				if (reg == 0x88) {
					cop_state.strobe = 1;
					cop_state.state = COP_strobe_delay1;
				} else if (reg == 0x8a) {
					cop_state.strobe = 2;
					cop_state.state = COP_strobe_delay1;
				} else {
#if 0
					event2_newevent2 (1, (reg << 16) | data, copper_write);
#else
					// FIX: all copper writes happen 1 cycle later than CPU writes
					if (customdelay[reg / 2]) {
						cop_state.moveaddr = reg;
						cop_state.movedata = data;
						cop_state.movedelay = customdelay[cop_state.moveaddr / 2];
					} else {
						int hpos2 = old_hpos;
						custom_wput_copper (hpos2, reg, data, 0);
						hpos2++;
						if (!nocustom () && reg >= 0x140 && reg < 0x180 && hpos2 >= SPR0_HPOS && hpos2 < SPR0_HPOS + 4 * MAX_SPRITES) {
							do_sprites (hpos2);
						}
					}
#endif
				}
#ifdef DEBUGGER
				if (debug_copper)
					record_copper (debugip - 4, old_hpos, vpos);
#endif
			}
			break;

		case COP_wait1:
			/* There's a nasty case here.  As stated in the "Theory" comment above, we
			test against the incremented copper position.  I believe this means that
			we have to increment the _vertical_ position at the last cycle in the line,
			and set the horizontal position to 0.
			Normally, this isn't going to make a difference, since we consider these
			last cycles unavailable for the copper, so waking up in the last cycle has
			the same effect as waking up at the start of the line.  However, there is
			one possible problem:  If we're at 0xFFE0, any wait for an earlier position
			must _not_ complete (since, in effect, the current position will be back
			at 0/0).  This can be seen in the Superfrog copper list.
			Things get monstrously complicated if we try to handle this "properly" by
			incrementing vpos and setting c_hpos to 0.  Especially the various speedup
			hacks really assume that vpos remains constant during one line.  Hence,
			this hack: defer the entire decision until the next line if necessary.  */
			if (c_hpos >= (maxhpos & ~1) || (c_hpos & 1))
				break;

			cop_state.state = COP_wait;

			cop_state.vcmp = (cop_state.saved_i1 & (cop_state.saved_i2 | 0x8000)) >> 8;
			cop_state.hcmp = (cop_state.saved_i1 & cop_state.saved_i2 & 0xFE);

			vp = vpos & (((cop_state.saved_i2 >> 8) & 0x7F) | 0x80);

			if (cop_state.saved_i1 == 0xFFFF && cop_state.saved_i2 == 0xFFFE) {
				cop_state.state = COP_stop;
				copper_enabled_thisline = 0;
				unset_special (SPCFLAG_COPPER);
				goto out;
			}
			if (vp < cop_state.vcmp) {
				copper_enabled_thisline = 0;
				unset_special (SPCFLAG_COPPER);
				goto out;
			}

			/* fall through */
		case COP_wait:
			if (copper_cant_read (old_hpos, 0))
				continue;

			hp = c_hpos & (cop_state.saved_i2 & 0xFE);
			if (vp <= cop_state.vcmp && hp < cop_state.hcmp)
				break;

			/* Now we know that the comparisons were successful.  We might still
			have to wait for the blitter though.  */
			if ((cop_state.saved_i2 & 0x8000) == 0) {
				decide_blitter (old_hpos);
				if (bltstate != BLT_done) {
					/* We need to wait for the blitter.  */
					cop_state.state = COP_bltwait;
					copper_enabled_thisline = 0;
					unset_special (SPCFLAG_COPPER);
					goto out;
				} else {
					if (debug_dma)
						record_dma_event (DMA_EVENT_COPPERWAKE, old_hpos, vp);
				}
			}

#ifdef DEBUGGER
			if (debug_copper)
				record_copper (cop_state.ip - 4, old_hpos, vpos);
#endif

			cop_state.state = COP_read1;
			break;

		case COP_skip1:
			{
				unsigned int vcmp, hcmp, vp1, hp1;

				if (c_hpos >= (maxhpos & ~1) || (c_hpos & 1))
					break;
				if (copper_cant_read (old_hpos, 0))
					continue;

				vcmp = (cop_state.saved_i1 & (cop_state.saved_i2 | 0x8000)) >> 8;
				hcmp = (cop_state.saved_i1 & cop_state.saved_i2 & 0xFE);
				vp1 = vpos & (((cop_state.saved_i2 >> 8) & 0x7F) | 0x80);
				hp1 = c_hpos & (cop_state.saved_i2 & 0xFE);

				if ((vp1 > vcmp || (vp1 == vcmp && hp1 >= hcmp)) && ((cop_state.saved_i2 & 0x8000) != 0 || bltstate == BLT_done))
					cop_state.ignore_next = 1;

				cop_state.state = COP_read1;

#ifdef DEBUGGER
				if (debug_copper)
					record_copper (cop_state.ip - 4, old_hpos, vpos);
#endif

				break;
			}
		default:
			break;
		}
	}

out:
	cop_state.hpos = c_hpos;
	last_copper_hpos = until_hpos;
}

static void compute_spcflag_copper (int hpos)
{
	int wasenabled = copper_enabled_thisline;

	copper_enabled_thisline = 0;
	unset_special (SPCFLAG_COPPER);
	if (!dmaen (DMA_COPPER) || cop_state.state == COP_stop || cop_state.state == COP_bltwait || nocustom ())
		return;

	if (cop_state.state == COP_wait) {
		int vp = vpos & (((cop_state.saved_i2 >> 8) & 0x7F) | 0x80);

		if (vp < cop_state.vcmp)
			return;
	}
	// do not use past cycles if starting for the first time in this line
	// (write to DMACON for example) hpos+1 for long lines
	if (!wasenabled && cop_state.hpos < hpos && hpos < maxhpos) {
		hpos = (hpos + 2) & ~1;
		if (hpos > (maxhpos_short & ~1))
			hpos = maxhpos_short & ~1;
		cop_state.hpos = hpos;
	}
	copper_enabled_thisline = 1;
	set_special (SPCFLAG_COPPER);
}

/*
Copper writes to BLTSIZE: 3 blitter idle cycles, blitter normal cycle starts
(CPU write to BLTSIZE only have 2 idle cycles at start)

BFD=0 wait: 1 cycle (or 2 if hpos is not aligned) delay before wait ends
*/
void blitter_done_notify (int hpos)
{
	int vp = vpos;

	if (cop_state.state != COP_bltwait)
		return;

	hpos += 3;
	hpos &= ~1;
	if (hpos >= maxhpos) {
		hpos -= maxhpos;
		vp++;
	}
	cop_state.hpos = hpos;
	cop_state.vpos = vp;
	cop_state.state = COP_read1;
	if (debug_dma)
		record_dma_event (DMA_EVENT_COPPERWAKE, hpos, vp);

	if (dmaen (DMA_COPPER) && vp == vpos) {
		copper_enabled_thisline = 1;
		set_special (SPCFLAG_COPPER);
	}
}

void do_copper (void)
{
	int hpos = current_hpos ();
	update_copper (hpos);
}

/* ADDR is the address that is going to be read/written; this access is
the reason why we want to update the copper.  This function is also
used from hsync_handler to finish up the line; for this case, we check
hpos against maxhpos.  */
STATIC_INLINE void sync_copper_with_cpu (int hpos, int do_schedule)
{
	/* Need to let the copper advance to the current position.  */
	if (copper_enabled_thisline)
		update_copper (hpos);
}

static void cursorsprite (void)
{
	if (!dmaen (DMA_SPRITE) || first_planes_vpos == 0)
		return;
	sprite_0 = spr[0].pt;
	sprite_0_height = spr[0].vstop - spr[0].vstart;
	sprite_0_colors[0] = 0;
	sprite_0_doubled = 0;
	if (sprres == 0)
		sprite_0_doubled = 1;
	if (currprefs.chipset_mask & CSMASK_AGA) {
		int sbasecol = ((bplcon4 >> 4) & 15) << 4;
		sprite_0_colors[1] = current_colors.color_regs_aga[sbasecol + 1];
		sprite_0_colors[2] = current_colors.color_regs_aga[sbasecol + 2];
		sprite_0_colors[3] = current_colors.color_regs_aga[sbasecol + 3];
	} else {
		sprite_0_colors[1] = xcolors[current_colors.color_regs_ecs[17]];
		sprite_0_colors[2] = xcolors[current_colors.color_regs_ecs[18]];
		sprite_0_colors[3] = xcolors[current_colors.color_regs_ecs[19]];
	}
	sprite_0_width = sprite_width;
	if (currprefs.input_tablet && currprefs.input_magic_mouse) {
		if (currprefs.input_magic_mouse_cursor == MAGICMOUSE_HOST_ONLY && mousehack_alive ())
			magic_sprite_mask &= ~1;
		else
			magic_sprite_mask |= 1;
	}
}

STATIC_INLINE uae_u16 sprite_fetch (struct sprite *s, int dma, int hpos, int cycle, int mode)
{
	uae_u16 data = last_custom_value1;
	if (dma) {
		if (cycle && currprefs.cpu_cycle_exact)
			s->ptxhpos = hpos;
		data = last_custom_value1 = chipmem_wget_indirect (s->pt);
		alloc_cycle (hpos, CYCLE_SPRITE);
#ifdef DEBUGGER
		if (debug_dma)
			record_dma ((s - &spr[0]) * 8 + 0x140 + mode * 4 + cycle * 2, data, s->pt, hpos, vpos, DMARECORD_SPRITE);
#endif
	}
	s->pt += 2;
	return data;
}
STATIC_INLINE uae_u16 sprite_fetch2 (struct sprite *s, int hpos, int cycle, int mode)
{
	uae_u16 data = last_custom_value1 = chipmem_wget_indirect (s->pt);
	s->pt += 2;
	return data;
}

STATIC_INLINE void do_sprites_1 (int num, int cycle, int hpos)
{
	struct sprite *s = &spr[num];
	int dma, posctl = 0;
	uae_u16 data;
	// fetch both sprite pairs even if DMA was switched off between sprites
	int isdma = dmaen (DMA_SPRITE) || ((num & 1) && spr[num & ~1].dmacycle);

	if (isdma && vpos == sprite_vblank_endline)
		spr_arm (num, 0);

#ifdef AGA
	if (isdma && s->dblscan && (fmode & 0x8000) && (vpos & 1) != (s->vstart & 1) && s->dmastate) {
		spr_arm (num, 1);
		return;
	}
#endif
#if SPRITE_DEBUG > 3
	if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY)
		write_log (_T("%d:%d:slot%d:%d\n"), vpos, hpos, num, cycle);
#endif
	if (vpos == s->vstart) {
#if SPRITE_DEBUG > 0
		if (!s->dmastate && vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY)
			write_log (_T("%d:%d:SPR%d START\n"), vpos, hpos, num);
#endif
		s->dmastate = 1;
		if (num == 0 && cycle == 0)
			cursorsprite ();
	}
	if (vpos == s->vstop || vpos == sprite_vblank_endline) {
#if SPRITE_DEBUG > 0
		if (s->dmastate && vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY)
			write_log (_T("%d:%d:SPR%d STOP\n"), vpos, hpos, num);
#endif
		s->dmastate = 0;
#if 0
		// roots 2.0 flower zoomer bottom part missing if this enabled
		if (vpos == s->vstop) {
			spr_arm (num, 0);
			//return;
		}
#endif
	}

	if (!isdma)
		return;
	if (cycle && !s->dmacycle)
		return; /* Superfrog intro flashing bee fix */

	dma = hpos < plfstrt_sprite || diwstate != DIW_waiting_stop;
	if (vpos == s->vstop || vpos == sprite_vblank_endline) {
		s->dmastate = 0;
		posctl = 1;
		if (dma) {
			data = sprite_fetch (s, dma, hpos, cycle, 0);
			switch (sprite_width)
			{
			case 64:
				sprite_fetch2 (s, hpos, cycle, 0);
				sprite_fetch2 (s, hpos, cycle, 0);
			case 32:
				sprite_fetch2 (s, hpos, cycle, 0);
				break;
			}
			//write_log (_T("%d:%d: %04X=%04X\n"), vpos, hpos, 0x140 + cycle * 2 + num * 8, data);
			if (cycle == 0) {
				SPRxPOS_1 (data, num, hpos);
				s->dmacycle = 1;
			} else {
				SPRxCTL_1 (data, num, hpos);
				s->dmastate = 0;
				sprstartstop (s);
			}
		}
#if SPRITE_DEBUG > 1
		if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
			write_log (_T("%d:%d:dma:P=%06X "), vpos, hpos, s->pt);
		}
#endif
	}
	if (s->dmastate && !posctl && dma) {
		uae_u16 data;

		data = sprite_fetch (s, dma, hpos, cycle, 1);
#if SPRITE_DEBUG > 1
		if (vpos >= SPRITE_DEBUG_MINY && vpos <= SPRITE_DEBUG_MAXY) {
			write_log (_T("%d:%d:dma:P=%06X "), vpos, hpos, s->pt);
		}
#endif
		if (cycle == 0) {
			SPRxDATA_1 (data, num, hpos);
			s->dmacycle = 1;
		} else {
			SPRxDATB_1 (data, num, hpos);
			spr_arm (num, 1);
		}
#ifdef AGA
		switch (sprite_width)
		{
		case 64:
			{
				uae_u16 data32 = sprite_fetch2 (s, hpos, cycle, 1);
				uae_u16 data641 = sprite_fetch2 (s, hpos, cycle, 1);
				uae_u16 data642 = sprite_fetch2 (s, hpos, cycle, 1);
				if (dma) {
					if (cycle == 0) {
						sprdata[num][3] = data642;
						sprdata[num][2] = data641;
						sprdata[num][1] = data32;
					} else {
						sprdatb[num][3] = data642;
						sprdatb[num][2] = data641;
						sprdatb[num][1] = data32;
					}
				}
			}
			break;
		case 32:
			{
				uae_u16 data32 = sprite_fetch2 (s, hpos, cycle, 1);
				if (dma) {
					if (cycle == 0)
						sprdata[num][1] = data32;
					else
						sprdatb[num][1] = data32;
				}
			}
			break;
		}
#endif
	}
}

static void do_sprites (int hpos)
{
	int maxspr, minspr;
	int i;

	if (vpos < sprite_vblank_endline)
		return;

	if (doflickerfix () && interlace_seen && (next_lineno & 1))
		return;

#ifndef CUSTOM_SIMPLE
	maxspr = hpos;
	minspr = last_sprite_hpos + 1;

	if (minspr >= maxspr || last_sprite_hpos == hpos)
		return;

	if (maxspr >= SPR0_HPOS + MAX_SPRITES * 4)
		maxspr = SPR0_HPOS + MAX_SPRITES * 4 - 1;
	if (minspr < SPR0_HPOS)
		minspr = SPR0_HPOS;

	if (minspr == maxspr)
		return;

	for (i = minspr; i <= maxspr; i++) {
		int cycle = -1;
		int num = (i - SPR0_HPOS) / 4;
		switch ((i - SPR0_HPOS) & 3)
		{
		case 0:
			cycle = 0;
			spr[num].dmacycle = 0;
			break;
		case 2:
			cycle = 1;
			break;
		}
		if (cycle >= 0) {
			spr[num].ptxhpos = MAXHPOS;
			do_sprites_1 (num, cycle, i);
		}
	}

	last_sprite_hpos = hpos;
#else
	for (i = 0; i < MAX_SPRITES * 2; i++) {
		spr[i / 2].dmacycle = 1;
		do_sprites_1 (i / 2, i & 1, 0);
	}
#endif
}

static void init_sprites (void)
{
	memset (sprpos, 0, sizeof sprpos);
	memset (sprctl, 0, sizeof sprctl);
}

static void init_hardware_frame (void)
{
	int i;

	first_bpl_vpos = -1;
	next_lineno = 0;
	prev_lineno = -1;
	nextline_how = nln_normal;
	diwstate = DIW_waiting_start;
	ddfstate = DIW_waiting_start;
	first_planes_vpos = 0;
	last_planes_vpos = 0;
	diwfirstword_total = max_diwlastword;
	diwlastword_total = 0;
	ddffirstword_total = max_diwlastword;
	ddflastword_total = 0;
	plflastline_total = 0;
	plffirstline_total = current_maxvpos ();
	autoscale_bordercolors = 0;
	for (i = 0; i < MAX_SPRITES; i++)
		spr[i].ptxhpos = MAXHPOS;
}

void init_hardware_for_drawing_frame (void)
{
	/* Avoid this code in the first frame after a customreset.  */
	if (prev_sprite_entries) {
		int first_pixel = prev_sprite_entries[0].first_pixel;
		int npixels = prev_sprite_entries[prev_next_sprite_entry].first_pixel - first_pixel;
		memset (spixels + first_pixel, 0, npixels * sizeof *spixels);
		memset (spixstate.bytes + first_pixel, 0, npixels * sizeof *spixstate.bytes);
	}
	prev_next_sprite_entry = next_sprite_entry;

	next_color_change = 0;
	next_sprite_entry = 0;
	next_color_entry = 0;
	remembered_color_entry = -1;

	prev_sprite_entries = sprite_entries[current_change_set];
	curr_sprite_entries = sprite_entries[current_change_set ^ 1];
	prev_color_changes = color_changes[current_change_set];
	curr_color_changes = color_changes[current_change_set ^ 1];
	prev_color_tables = color_tables[current_change_set];
	curr_color_tables = color_tables[current_change_set ^ 1];

	prev_drawinfo = line_drawinfo[current_change_set];
	curr_drawinfo = line_drawinfo[current_change_set ^ 1];
	current_change_set ^= 1;

	color_src_match = color_dest_match = -1;

	/* Use both halves of the array in alternating fashion.  */
	curr_sprite_entries[0].first_pixel = current_change_set * MAX_SPR_PIXELS;
	next_sprite_forced = 1;
}

static void do_savestate(void);

static int rpt_vsync (void)
{
	int v = read_processor_time () - vsyncmintime;
	if (v > (int)syncbase || v < -((int)syncbase)) {
		vsyncmintime = read_processor_time ();
		v = 0;
	}
	return v;
}

static void framewait (void)
{
	frame_time_t curr_time;
	frame_time_t start;
	int vs = isvsync ();

	if (vs > 0) {
		vsyncmintime = vsynctime;
		update_screen ();
		return;
	} else if (vs < 0) {
		vsyncmintime = vsynctime;
		vsync_busywait ();
		update_screen ();
		return;
	}
	for (;;) {
		double v = rpt_vsync () / (syncbase / 1000.0);
		if (v >= -4)
			break;
		sleep_millis (2);
	}
	update_screen ();
	curr_time = start = read_processor_time ();
	while (rpt_vsync () < 0);
	curr_time = read_processor_time ();
	vsyncmintime = curr_time + vsynctime;
	idletime += read_processor_time() - start;
}

static frame_time_t frametime2;

void fpscounter_reset (void)
{
	timeframes = 0;
	frametime2 = 0;
	bogusframe = 2;
	lastframetime = read_processor_time ();
	idletime = 0;
}

static void fpscounter (void)
{
	frame_time_t now, last;
	int mcnt = 10;

	now = read_processor_time ();
	last = now - lastframetime;
	lastframetime = now;

	if (bogusframe)
		return;

	frametime += last;
	frametime2 += last;
	timeframes++;
	if ((timeframes % mcnt) == 0) {
		double idle = 1000 - (idletime == 0 ? 0.0 : (double)idletime * 1000.0 / (vsynctime * mcnt));
		int fps = frametime2 == 0 ? 0 : (syncbase * mcnt) / (frametime2 / 10);
		if (fps > 9999)
			fps = 9999;
		if (idle < 0)
			idle = 0;
		if (idle > 100 * 10)
			idle = 100 * 10;
		if (fake_vblank_hz * 10 > fps) {
			double mult = (double)fake_vblank_hz * 10.0 / fps;
			idle *= mult;
		}
		if (currprefs.turbo_emulation && idle < 100 * 10)
			idle = 100 * 10;
		gui_fps (fps, (int)idle);
		frametime2 = 0;
		idletime = 0;
	}
}

// vsync functions that are not hardware timing related
static void vsync_handler_pre (void)
{
	if (bogusframe > 0)
		bogusframe--;

	handle_events ();

#ifdef PICASSO96
	picasso_handle_vsync ();
#endif
	audio_vsync ();
	blkdev_vsync ();
	CIA_vsync_prehandler ();

	if (quit_program > 0) {
		/* prevent possible infinite loop at wait_cycles().. */
		framecnt = 0;
		reset_decisions ();
		return;
	}

	config_check_vsync ();
	if (timehack_alive > 0)
		timehack_alive--;

	inputdevice_vsync ();

	filesys_vsync ();

	sampler_vsync ();

	vsync_handle_redraw (lof_store, lof_changed);
}

// emulated hardware vsync
static void vsync_handler_post (void)
{
	fpscounter ();

	if (!isvsync ()
#ifdef AVIOUTPUT
		&& ((avioutput_framelimiter && avioutput_enabled) || !avioutput_enabled)
#endif
		) {
#ifdef JIT
			if (!currprefs.cachesize) {
#endif
				if (currprefs.m68k_speed == -1) {
					frame_time_t curr_time = read_processor_time ();
					vsyncmintime += vsynctime;
					/* @@@ Mathias? How do you think we should do this? */
					/* If we are too far behind, or we just did a reset, adjust the
					* needed time. */
					if ((long int)(curr_time - vsyncmintime) > 0 || rpt_did_reset)
						vsyncmintime = curr_time + vsynctime;
					rpt_did_reset = 0;
					update_screen ();
				} else if (rpt_available) {
					framewait ();
				}
#ifdef JIT
			} else {
				if (rpt_available && currprefs.m68k_speed == 0) {
					framewait ();
				} else {
					update_screen ();
				}
			}
#endif
	} else if (currprefs.m68k_speed == 0) {
		framewait ();
	} else {
		update_screen ();
	}

#if CUSTOM_DEBUG > 1
	if ((intreq & 0x0020) && (intena & 0x0020))
		write_log (_T("vblank interrupt not cleared\n"));
#endif
	DISK_vsync ();
	if (bplcon0 & 4)
		lof_store = lof_store ? 0 : 1;
	lof_current = lof_store;

	if (debug_copper)
		record_copper_reset ();
	if (debug_dma)
		record_dma_reset ();

	if (p96refresh_active) {
		vpos_count = p96refresh_active;
		vtotal = vpos_count;
	}
	if ((beamcon0 & (0x20 | 0x80)) != (new_beamcon0 & (0x20 | 0x80)) || (abs (vpos_count - vpos_count_prev)  > 1))
		init_hz ();
	if (lof_changed)
		compute_vsynctime ();
	vpos_count_prev = vpos_count;

	lof_changed = 0;

	COPJMP (1, 1);

	init_hardware_frame ();

	vsync_cycles = get_cycles ();
}

#ifdef JIT

#define N_LINES 8

STATIC_INLINE int trigger_frh (int v)
{
	return (v & (N_LINES - 1)) == 0;
}

static long int diff32 (frame_time_t x, frame_time_t y)
{
	return (long int)(x - y);
}
static void frh_handler (void)
{
	if (currprefs.m68k_speed == -1) {
		frame_time_t curr_time = read_processor_time ();
		vsyncmintime += vsynctime * N_LINES / maxvpos_nom;
		/* @@@ Mathias? How do you think we should do this? */
		/* If we are too far behind, or we just did a reset, adjust the
		* needed time. */
		if (rpt_did_reset) {
			vsyncmintime = curr_time + vsynctime;
			rpt_did_reset = 0;
		}
		/* Allow this to be one frame's worth of cycles out */
		while (diff32 (curr_time, vsyncmintime + vsynctime) > 0) {
			vsyncmintime += vsynctime * N_LINES / maxvpos_nom;
			if (currprefs.turbo_emulation)
				break;
		}
	}
}
#endif

static void copper_check (int n)
{
	if (cop_state.state == COP_wait) {
		int vp = vpos & (((cop_state.saved_i2 >> 8) & 0x7F) | 0x80);
		if (vp < cop_state.vcmp) {
			if (copper_enabled_thisline)
				write_log (_T("COPPER BUG %d: vp=%d vpos=%d vcmp=%d act=%d thisline=%d\n"), n, vp, vpos, cop_state.vcmp, copper_enabled_thisline);
		}
	}
}

/*

0 0 -
1 1 --
2 2 -
3 3 --
4 4 -
5 5 --

0 x -
1 0 --
2 1 -
3 2 --
4 3 -
5 4 --

*/

static void hsync_scandoubler (void)
{
	int i, idx1;
	struct draw_info *dip1;
	uaecptr bpltmp[8], bpltmpx[8];

	next_lineno++;
	scandoubled_line = 1;
	debug_dma = 0;

	for (i = 0; i < 8; i++) {
		int diff;
		bpltmp[i] = bplpt[i];
		bpltmpx[i] = bplptx[i];
		if (prevbpl[lof_store][vpos][i] && prevbpl[1 - lof_store][vpos][i]) {
			diff = prevbpl[lof_store][vpos][i] - prevbpl[1 - lof_store][vpos][i];
			if (lof_store) {
				if (bplcon0 & 4)
					bplpt[i] = prevbpl[lof_store][vpos][i] - diff;
			} else {
				if (bplcon0 & 4)
					bplpt[i] = prevbpl[lof_store][vpos][i];
				else
					bplpt[i] = bplpt[i] - diff;

			}
		}
	}

	reset_decisions ();
	plf_state = plf_idle;

	// copy color changes
	dip1 = curr_drawinfo + next_lineno - 1;
	for (idx1 = dip1->first_color_change; idx1 < dip1->last_color_change; idx1++) {
		struct color_change *cs2 = &curr_color_changes[idx1];
		int regno = cs2->regno;
		int hpos = cs2->linepos;
		if (regno < 0x1000 && hpos < HBLANK_OFFSET && !(beamcon0 & 0x80) && prev_lineno >= 0) {
			struct draw_info *pdip = curr_drawinfo + next_lineno - 1;
			int idx = pdip->last_color_change;
			pdip->last_color_change++;
			pdip->nr_color_changes++;
			curr_color_changes[idx].linepos = hpos + maxhpos + 1;
			curr_color_changes[idx].regno = regno;
			curr_color_changes[idx].value = cs2->value;
			curr_color_changes[idx + 1].regno = -1;
		} else {
			struct color_change *cs1 = &curr_color_changes[next_color_change];
			memcpy (cs1, cs2, sizeof (struct color_change));
			next_color_change++;
		}
	}
	curr_color_changes[next_color_change].regno = -1;

	finish_decisions ();
	hsync_record_line_state (next_lineno, nln_normal, thisline_changed);
	hardware_line_completed (next_lineno);
	scandoubled_line = 0;

	for (i = 0; i < 8; i++) {
		bplpt[i] = bpltmp[i];
		bplptx[i] = bpltmpx[i];
	}
}

static void events_dmal (int);
static uae_u16 dmal, dmal_hpos;

static void dmal_emu (uae_u32 v)
{
	// Disk and Audio DMA bits are ignored by Agnus, Agnus only checks DMAL and master bit
	if (!(dmacon & DMA_MASTER))
		return;
	int hpos = current_hpos ();
	if (v >= 6) {
		v -= 6;
		int nr = v / 2;
		uaecptr pt = audio_getpt (nr, (v & 1) != 0);
		uae_u16 dat = chipmem_wget_indirect (pt);
#ifdef DEBUGGER
		if (debug_dma)
			record_dma (0xaa + nr * 16, dat, pt, hpos, vpos, DMARECORD_AUDIO);
#endif
		last_custom_value1 = dat;
		AUDxDAT (nr, dat, pt);
	} else {
		uae_u16 dat;
		int w = v & 1;
		uaecptr pt = disk_getpt ();
		// disk_fifostatus() needed in >100% disk speed modes
		if (w) {
			if (disk_fifostatus () <= 0) {
				dat = chipmem_wget_indirect (pt);
				last_custom_value1 = dat;
				DSKDAT (dat);
			}
		} else {
			if (disk_fifostatus () >= 0) {
				dat = DSKDATR ();
				chipmem_wput_indirect (pt, dat);
			}
		}
#ifdef DEBUGGER
		if (debug_dma)
			record_dma (w ? 0x26 : 0x08, dat, pt, hpos, vpos, DMARECORD_DISK);
#endif
	}
}

static void dmal_func (uae_u32 v)
{
	dmal_emu (v);
	events_dmal (0);
}

static void dmal_func2 (uae_u32 v)
{
	while (dmal) {
		if (dmal & 3)
			dmal_emu (dmal_hpos + ((dmal & 2) ? 1 : 0));
		dmal_hpos += 2;
		dmal >>= 2;
	}
}

static void events_dmal (int hp)
{
	if (!dmal)
		return;
	if (currprefs.cpu_cycle_exact) {
		while (dmal) {
			if (dmal & 3)
				break;
			hp += 2;
			dmal >>= 2;
			dmal_hpos += 2;
		}
		event2_newevent2 (hp, dmal_hpos + ((dmal & 2) ? 1 : 0), dmal_func);
		dmal &= ~3;
	} else if (currprefs.cachesize) {
		dmal_func2 (0);
	} else {
		event2_newevent2 (hp, 13, dmal_func2);
	}
}

static void events_dmal_hsync (void)
{
	if (dmal)
		write_log (_T("DMAL error!? %04x\n"), dmal);
	dmal = audio_dmal ();
	dmal <<= 6;
	dmal |= disk_dmal ();
	if (!dmal)
		return;
	dmal_hpos = 0;
	if (currprefs.cpu_cycle_exact) {
		for (int i = 0; i < 6 + 8; i += 2) {
			if (dmal & (3 << i)) {
				alloc_cycle_ext (i + 7, CYCLE_MISC);
			}
		}
	}
	events_dmal (7);
}

static bool is_vsync (void)
{
	int vp = vpos + 1;
	int vpc = vpos_count + 1;
	/* Agnus vpos counter keeps counting until it wraps around if VPOSW writes put it past maxvpos */
	if (vp >= maxvpos_total)
		vp = 0;
	if (vp == maxvpos + lof_store || vp == maxvpos + lof_store + 1 || vpc >= MAXVPOS) {
		// vpos_count >= MAXVPOS just to not crash if VPOSW writes prevent vsync completely
		return true;
	}
	return false;
}

static void set_hpos (void)
{
	maxhpos = maxhpos_short + lol;
	eventtab[ev_hsync].evtime = get_cycles () + HSYNCTIME;
	eventtab[ev_hsync].oldcycles = get_cycles ();
}

// this finishes current line
static void hsync_handler_pre (bool isvsync)
{
	int hpos = current_hpos ();

	if (!nocustom ()) {
		sync_copper_with_cpu (maxhpos, 0);
		finish_decisions ();
		if (thisline_decision.plfleft != -1) {
			if (currprefs.collision_level > 1)
				do_sprite_collisions ();
			if (currprefs.collision_level > 2)
				do_playfield_collisions ();
		}
		hsync_record_line_state (next_lineno, nextline_how, thisline_changed);
		/* reset light pen latch */
		if (vpos == sprite_vblank_endline) {
			lightpen_triggered = 0;
			sprite_0 = 0;
		}
		if (lightpen_cx > 0 && (bplcon0 & 8) && !lightpen_triggered && lightpen_cy == vpos) {
			vpos_lpen = vpos;
			hpos_lpen = lightpen_cx;
			lightpen_triggered = 1;
		}
		hardware_line_completed (next_lineno);
		if (doflickerfix () && interlace_seen)
			hsync_scandoubler ();
	}

#ifdef A2065
	a2065_hsync_handler ();
#endif
#ifdef CD32
	AKIKO_hsync_handler ();
#endif
#ifdef CDTV
	CDTV_hsync_handler ();
#endif
	decide_blitter (-1);

#ifdef PICASSO96
	picasso_handle_hsync ();
#endif
	{
		void ahi_hsync (void);
		ahi_hsync ();
	}

	DISK_hsync ();
	if (currprefs.produce_sound)
		audio_hsync ();
	CIA_hsync_prehandler ();

	hsync_counter++;

	if (islinetoggle ())
		lol ^= 1;
	else
		lol = 0;

	vpos++;
	vpos_count++;
	if (vpos >= maxvpos_total)
		vpos = 0;
	if (isvsync) {
		vpos = 0;
		vsync_counter++;
	}
	set_hpos ();
#if 0
	static int ppp = 2;
	if (input_record && hsync_counter == 100 * 313 + 1) {
		ppp--;
		if (ppp == 0)
			activate_debugger ();
	}
#endif
}

// this prepares for new line
static void hsync_handler_post (bool isvsync)
{
	last_copper_hpos = 0;
#ifdef CPUEMU_12
	if (currprefs.cpu_cycle_exact || currprefs.blitter_cycle_exact) {
		memset (cycle_line, 0, sizeof cycle_line);
	}
#endif

	bool ciasyncs = !(bplcon0 & 2) || ((bplcon0 & 2) && currprefs.genlock);
	CIA_hsync_posthandler (ciasyncs);
	if (currprefs.cs_ciaatod > 0) {
		static int cia_hsync;
		cia_hsync -= 256;
		if (cia_hsync <= 0) {
			CIA_vsync_posthandler (1);
			cia_hsync += ((MAXVPOS_PAL * MAXHPOS_PAL * 50 * 256) / (maxhpos * (currprefs.cs_ciaatod == 2 ? 60 : 50)));
		}
	} else if (currprefs.cs_ciaatod == 0 && isvsync) {
		CIA_vsync_posthandler (ciasyncs);
	}

	if (vpos == equ_vblank_endline + 1 && lof_current != lof_store) {
		// argh, line=0 field decision was wrong, someone did
		// something stupid and changed LOF
		// lof_current = lof_store;
		// don't really know what to do here exactly without corrupt display
	}

	inputdevice_hsync ();

	last_custom_value1 = 0xffff; // refresh slots should set this to 0xffff

	if (!nocustom ()) {
		if (!currprefs.blitter_cycle_exact && bltstate != BLT_done && dmaen (DMA_BITPLANE) && diwstate == DIW_waiting_stop) {
			blitter_slowdown (thisline_decision.plfleft, thisline_decision.plfright - (16 << fetchmode),
				cycle_diagram_total_cycles[fetchmode][GET_RES_AGNUS (bplcon0)][GET_PLANES_LIMIT (bplcon0)],
				cycle_diagram_free_cycles[fetchmode][GET_RES_AGNUS (bplcon0)][GET_PLANES_LIMIT (bplcon0)]);
		}
	}

	if (isvsync) {
		// vpos_count >= MAXVPOS just to not crash if VPOSW writes prevent vsync completely
		if ((bplcon0 & 8) && !lightpen_triggered) {
			vpos_lpen = vpos - 1;
			hpos_lpen = maxhpos;
			lightpen_triggered = 1;
		}
		vpos = 0;
		vsync_handler_post ();
		vpos_count = 0;
	}
	// DIP Agnus (8361): vblank interrupt is triggered on line 1!
	if (currprefs.cs_dipagnus) {
		if (vpos == 1)
			send_interrupt (5, 1 * CYCLE_UNIT);
	} else {
		if (vpos == 0)
			send_interrupt (5, 1 * CYCLE_UNIT);
	}

#ifdef CPUEMU_12
	if (currprefs.cpu_cycle_exact || currprefs.blitter_cycle_exact) {
		int hp = maxhpos - 1, i;
		for (i = 0; i < 4; i++) {
			alloc_cycle (hp, i == 0 ? CYCLE_STROBE : CYCLE_REFRESH); /* strobe */
#ifdef DEBUGGER
			if (debug_dma) {
				uae_u16 strobe = 0x3c;
				if (vpos < equ_vblank_endline)
					strobe = 0x38;
				else if (vpos < minfirstline)
					strobe = 0x3a;
				else if (vpos + 1 == maxvpos + lof_store)
					strobe = 0x38;
				else if ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) && lol)
					strobe = 0x3e;
				record_dma (i == 0 ? strobe : 0x1fe, 0xffff, 0xffffffff, hp, vpos, DMARECORD_REFRESH);
			}
#endif
			hp += 2;
			if (hp >= maxhpos)
				hp -= maxhpos;
		}
	}
#endif


	events_dmal_hsync ();

#ifdef JIT
	if (currprefs.cachesize) {
		if (currprefs.m68k_speed == -1) {
			static int count = 0;
			count++;
			if (trigger_frh (count)) {
				frh_handler ();
			}
			is_lastline = trigger_frh (count + 1) && ! rpt_did_reset;
		} else {
			is_lastline = 0;
		}
	} else {
#endif
		is_lastline = vpos + 1 == maxvpos + lof_store && currprefs.m68k_speed == -1;
#ifdef JIT
	}
#endif

	if (!nocustom ()) {
		int lineno = vpos;
		if (lineno >= MAXVPOS)
			lineno %= MAXVPOS;
		if ((bplcon0 & 4) && currprefs.gfx_vresolution)
			notice_interlace_seen ();
		nextline_how = nln_normal;
		if (doflickerfix () && interlace_seen) {
			lineno *= 2;
		} else if (currprefs.gfx_vresolution && (doublescan <= 0 || interlace_seen > 0)) {
			lineno *= 2;
			nextline_how = currprefs.gfx_vresolution > VRES_NONDOUBLE && currprefs.gfx_scanlines == false ? nln_doubled : nln_nblack;
			if ((bplcon0 & 4) || (interlace_seen > 0 && !lof_current)) {
				if (!lof_current) {
					lineno++;
					nextline_how = nln_lower;
				} else {
					nextline_how = nln_upper;
				}
			}
		}
		prev_lineno = next_lineno;
		next_lineno = lineno;
		reset_decisions ();
	}

	if (uae_int_requested) {
		INTREQ (0x8000 | 0x0008);
	}

	{
		extern int volatile uaenet_int_requested;
		extern int volatile uaenet_vsync_requested;
		if (uaenet_int_requested || (uaenet_vsync_requested && vpos == 10)) {
			INTREQ (0x8000 | 0x2000);
		}
	}

	{
		extern void bsdsock_fake_int_handler (void);
		extern int volatile bsd_int_requested;
		if (bsd_int_requested)
			bsdsock_fake_int_handler ();
	}

	plfstrt_sprite = plfstrt;
	/* See if there's a chance of a copper wait ending this line.  */
	cop_state.hpos = 0;
	cop_state.last_write = 0;
	compute_spcflag_copper (maxhpos);

	serial_hsynchandler ();
#ifdef CUSTOM_SIMPLE
	do_sprites (0);
#endif

	gayle_hsync ();
	scsi_hsync ();

	//copper_check (2);

	if (GET_PLANES (bplcon0) > 0 && dmaen (DMA_BITPLANE)) {
		if (vpos > last_planes_vpos)
			last_planes_vpos = vpos;
		if (vpos >= minfirstline && first_planes_vpos == 0) {
			first_planes_vpos = vpos > minfirstline ? vpos - 1 : vpos;
		} else if (vpos == current_maxvpos () - 1) {
			last_planes_vpos = vpos - 1;
		}
	}
	if (diw_change == 0) {
		if (vpos >= first_planes_vpos && vpos <= last_planes_vpos) {
			if (diwlastword > diwlastword_total)
				diwlastword_total = diwlastword;
			if (diwfirstword < diwfirstword_total) {
				diwfirstword_total = diwfirstword;
				firstword_bplcon1 = bplcon1;
			}
		}
		if (diwstate == DIW_waiting_stop) {
			int f = 8 << fetchmode;
			if (plfstrt + f < ddffirstword_total + f)
				ddffirstword_total = plfstrt + f;
			if (plfstop + 2 * f > ddflastword_total + 2 * f)
				ddflastword_total = plfstop + 2 * f;
		}
		if ((plffirstline < plffirstline_total || (plffirstline_total == minfirstline && vpos > minfirstline)) && plffirstline < vpos / 2) {
			firstword_bplcon1 = bplcon1;
			if (plffirstline < minfirstline)
				plffirstline_total = minfirstline;
			else
				plffirstline_total = plffirstline;
		}
		if (plflastline > plflastline_total && plflastline > plffirstline_total && plflastline > maxvpos / 2)
			plflastline_total = plflastline;
	}
	if (diw_change > 0)
		diw_change--;


#if 0
	{
		static int skip;
		if (M68K_GETPC >= 0x0C0D7A2 && M68K_GETPC < 0x00C0D7B2 && vpos == 0xf3) {
			if (!skip)
				activate_debugger ();
			skip = 1;
		}
		if (vpos != 0xf3)
			skip = 0;
	}
#endif
}

static void hsync_handler (void)
{
	bool vs = is_vsync ();
	hsync_handler_pre (vs);
	if (vs) {
		vsync_handler_pre ();
		if (savestate_check ()) {
			uae_reset (0);
			return;
		}
	}
	hsync_handler_post (vs);
}

void init_eventtab (void)
{
	int i;

	nextevent = 0;
	for (i = 0; i < ev_max; i++) {
		eventtab[i].active = 0;
		eventtab[i].oldcycles = get_cycles ();
	}
	for (i = 0; i < ev2_max; i++) {
		eventtab2[i].active = 0;
	}

	eventtab[ev_cia].handler = CIA_handler;
	eventtab[ev_hsync].handler = hsync_handler;
	eventtab[ev_hsync].evtime = get_cycles () + HSYNCTIME;
	eventtab[ev_hsync].active = 1;
	eventtab[ev_misc].handler = MISC_handler;
	eventtab[ev_audio].handler = audio_evhandler;

	eventtab2[ev2_blitter].handler = blitter_handler;
	eventtab2[ev2_disk].handler = DISK_handler;

	events_schedule ();
}

void custom_prepare (void)
{
	set_hpos ();
	hsync_handler_post (true);
}

void custom_reset (int hardreset)
{
	int i;
	int zero = 0;

	target_reset ();
	reset_all_systems ();
	write_log (_T("Reset at %08X\n"), M68K_GETPC);
	memory_map_dump ();

	lightpen_x = lightpen_y = -1;
	lightpen_triggered = 0;
	lightpen_cx = lightpen_cy = -1;
	if (!savestate_state) {
		extra_cycle = 0;
		hsync_counter = 0;
		vsync_counter = 0;
		currprefs.chipset_mask = changed_prefs.chipset_mask;
		update_mirrors ();
		if (!aga_mode) {
			for (i = 0; i < 32; i++) {
				current_colors.color_regs_ecs[i] = 0;
				current_colors.acolors[i] = getxcolor (0);
			}
#ifdef AGA
		} else {
			for (i = 0; i < 256; i++) {
				current_colors.color_regs_aga[i] = 0;
				current_colors.acolors[i] = getxcolor (0);
			}
#endif
		}

		clxdat = 0;

		/* Clear the armed flags of all sprites.  */
		memset (spr, 0, sizeof spr);
		nr_armed = 0;

		dmacon = 0;
		intreq_internal = 0;
		intena = intena_internal = 0;

		copcon = 0;
		DSKLEN (0, 0);

		bplcon0 = 0;
		bplcon4 = 0x0011; /* Get AGA chipset into ECS compatibility mode */
		bplcon3 = 0x0C00;

		diwhigh = 0;
		diwhigh_written = 0;
		hdiwstate = DIW_waiting_start; // this does not reset at vblank

		FMODE (0, 0);
		CLXCON (0);
		setup_fmodes (0);
		sprite_width = GET_SPRITEWIDTH (fmode);
		beamcon0 = new_beamcon0 = currprefs.ntscmode ? 0x00 : 0x20;
		bltstate = BLT_done;
		blit_interrupt = 1;
		lof_store = lof_current = 1;
	}

	gayle_reset (hardreset);
#ifdef AUTOCONFIG
	expamem_reset ();
#endif
	a1000_reset ();
	DISK_reset ();
	CIA_reset ();
	gayle_reset (0);
#ifdef A2091
	a2091_reset ();
#endif
#ifdef NCR
	ncr_reset ();
#endif
#ifdef JIT
	compemu_reset ();
#endif
	unset_special (~(SPCFLAG_BRK | SPCFLAG_MODE_CHANGE));

	vpos = 0;
	vpos_count = vpos_count_prev = 0;

	inputdevice_reset ();
	timehack_alive = 0;

	curr_sprite_entries = 0;
	prev_sprite_entries = 0;
	sprite_entries[0][0].first_pixel = 0;
	sprite_entries[1][0].first_pixel = MAX_SPR_PIXELS;
	sprite_entries[0][1].first_pixel = 0;
	sprite_entries[1][1].first_pixel = MAX_SPR_PIXELS;
	memset (spixels, 0, 2 * MAX_SPR_PIXELS * sizeof *spixels);
	memset (&spixstate, 0, sizeof spixstate);

	cop_state.state = COP_stop;
	diwstate = DIW_waiting_start;

	dmal = 0;
	init_hz_full ();
	vpos_lpen = -1;

	audio_reset ();
	if (!isrestore ()) {
		/* must be called after audio_reset */
		adkcon = 0;
		serial_uartbreak (0);
		audio_update_adkmasks ();
	}

	init_sprites ();

	init_hardware_frame ();
	drawing_init ();

	reset_decisions ();

	bogusframe = 1;

	if (isrestore ()) {
		uae_u16 v;
		uae_u32 vv;

		audio_update_adkmasks ();
		INTENA (0);
		INTREQ (0);
		COPJMP (1, 1);
		v = bplcon0;
		BPLCON0 (0, 0);
		BPLCON0 (0, v);
		FMODE (0, fmode);
		if (!(currprefs.chipset_mask & CSMASK_AGA)) {
			for(i = 0 ; i < 32 ; i++)  {
				vv = current_colors.color_regs_ecs[i];
				current_colors.color_regs_ecs[i] = -1;
				record_color_change (0, i, vv);
				remembered_color_entry = -1;
				current_colors.color_regs_ecs[i] = vv;
				current_colors.acolors[i] = xcolors[vv];
			}
#ifdef AGA
		} else {
			for(i = 0 ; i < 256 ; i++)  {
				vv = current_colors.color_regs_aga[i];
				current_colors.color_regs_aga[i] = -1;
				record_color_change (0, i, vv);
				remembered_color_entry = -1;
				current_colors.color_regs_aga[i] = vv;
				current_colors.acolors[i] = CONVERT_RGB (vv);
			}
#endif
		}
		CLXCON (clxcon);
		CLXCON2 (clxcon2);
		calcdiw ();
		write_log (_T("CPU=%d Chipset=%s %s\n"),
			currprefs.cpu_model,
			(currprefs.chipset_mask & CSMASK_AGA) ? _T("AGA") :
			(currprefs.chipset_mask & CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE) == (CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE) ? _T("Full ECS") :
			(currprefs.chipset_mask & CSMASK_ECS_DENISE) ? _T("ECS Denise") :
			(currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? _T("ECS") :
			_T("OCS"), currprefs.ntscmode ? _T("NTSC") : _T("PAL"));
		write_log (_T("State restored\n"));
		for (i = 0; i < 8; i++)
			nr_armed += spr[i].armed != 0;
		if (! currprefs.produce_sound) {
			eventtab[ev_audio].active = 0;
			events_schedule ();
		}
	}
	sprres = expand_sprres (bplcon0, bplcon3);
	sprite_width = GET_SPRITEWIDTH (fmode);
	setup_fmodes (0);

#ifdef ACTION_REPLAY
	/* Doing this here ensures we can use the 'reset' command from within AR */
	action_replay_reset ();
#endif
#if defined(ENFORCER)
	enforcer_disable ();
#endif

	if (hardreset)
		rtc_hardreset();

	picasso_reset ();
}

void dumpcustom (void)
{
	console_out_f (_T("DMACON: %04x INTENA: %04x (%04x) INTREQ: %04x (%04x) VPOS: %x HPOS: %x\n"), DMACONR (current_hpos ()),
		intena, intena_internal, intreq, intreq_internal, vpos, current_hpos ());
	console_out_f (_T("COP1LC: %08lx, COP2LC: %08lx COPPTR: %08lx\n"), (unsigned long)cop1lc, (unsigned long)cop2lc, cop_state.ip);
	console_out_f (_T("DIWSTRT: %04x DIWSTOP: %04x DDFSTRT: %04x DDFSTOP: %04x\n"),
		(unsigned int)diwstrt, (unsigned int)diwstop, (unsigned int)ddfstrt, (unsigned int)ddfstop);
	console_out_f (_T("BPLCON 0: %04x 1: %04x 2: %04x 3: %04x 4: %04x LOF=%d/%d HDIW=%d VDIW=%d\n"),
		bplcon0, bplcon1, bplcon2, bplcon3, bplcon4,
		lof_current, lof_store,
		hdiwstate == DIW_waiting_start ? 0 : 1, diwstate == DIW_waiting_start ? 0 : 1);
	if (timeframes) {
		console_out_f (_T("Average frame time: %.2f ms [frames: %d time: %d]\n"),
			(double)frametime / timeframes, timeframes, frametime);
		if (total_skipped)
			console_out_f (_T("Skipped frames: %d\n"), total_skipped);
	}
}

static void gen_custom_tables (void)
{
	int i;
	for (i = 0; i < 256; i++) {
		sprtaba[i] = ((((i >> 7) & 1) << 0)
			| (((i >> 6) & 1) << 2)
			| (((i >> 5) & 1) << 4)
			| (((i >> 4) & 1) << 6)
			| (((i >> 3) & 1) << 8)
			| (((i >> 2) & 1) << 10)
			| (((i >> 1) & 1) << 12)
			| (((i >> 0) & 1) << 14));
		sprtabb[i] = sprtaba[i] * 2;
		sprite_ab_merge[i] = (((i & 15) ? 1 : 0)
			| ((i & 240) ? 2 : 0));
	}
	for (i = 0; i < 16; i++) {
		clxmask[i] = (((i & 1) ? 0xF : 0x3)
			| ((i & 2) ? 0xF0 : 0x30)
			| ((i & 4) ? 0xF00 : 0x300)
			| ((i & 8) ? 0xF000 : 0x3000));
		sprclx[i] = (((i & 0x3) == 0x3 ? 1 : 0)
			| ((i & 0x5) == 0x5 ? 2 : 0)
			| ((i & 0x9) == 0x9 ? 4 : 0)
			| ((i & 0x6) == 0x6 ? 8 : 0)
			| ((i & 0xA) == 0xA ? 16 : 0)
			| ((i & 0xC) == 0xC ? 32 : 0)) << 9;
	}
}

/* mousehack is now in "filesys boot rom" */
static uae_u32 REGPARAM2 mousehack_helper_old (struct TrapContext *ctx)
{
	return 0;
}

int custom_init (void)
{

#ifdef AUTOCONFIG
	if (uae_boot_rom) {
		uaecptr pos;
		pos = here ();

		org (rtarea_base + 0xFF70);
		calltrap (deftrap (mousehack_helper_old));
		dw (RTS);

		org (rtarea_base + 0xFFA0);
		calltrap (deftrap (timehack_helper));
		dw (RTS);

		org (pos);
	}
#endif

	gen_custom_tables ();
	build_blitfilltable ();

	drawing_init ();

	create_cycle_diagram_table ();

	return 1;
}

/* Custom chip memory bank */

static uae_u32 REGPARAM3 custom_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custom_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custom_bget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custom_lgeti (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custom_wgeti (uaecptr) REGPARAM;
static void REGPARAM3 custom_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 custom_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 custom_bput (uaecptr, uae_u32) REGPARAM;

addrbank custom_bank = {
	custom_lget, custom_wget, custom_bget,
	custom_lput, custom_wput, custom_bput,
	default_xlate, default_check, NULL, _T("Custom chipset"),
	custom_lgeti, custom_wgeti, ABFLAG_IO
};

static uae_u32 REGPARAM2 custom_wgeti (uaecptr addr)
{
	if (currprefs.cpu_model >= 68020)
		return dummy_wgeti (addr);
	return custom_wget (addr);
}
static uae_u32 REGPARAM2 custom_lgeti (uaecptr addr)
{
	if (currprefs.cpu_model >= 68020)
		return dummy_lgeti (addr);
	return custom_lget (addr);
}

STATIC_INLINE uae_u32 REGPARAM2 custom_wget_1 (int hpos, uaecptr addr, int noput)
{
	uae_u16 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 0xfff;
#if CUSTOM_DEBUG > 2
	write_log (_T("%d:%d:wget: %04X=%04X pc=%p\n"), current_hpos(), vpos, addr, addr & 0x1fe, m68k_getpc ());
#endif
	switch (addr & 0x1fe) {
	case 0x002: v = DMACONR (hpos); break;
	case 0x004: v = VPOSR (); break;
	case 0x006: v = VHPOSR (); break;

	case 0x00A: v = JOY0DAT (); break;
	case 0x00C: v = JOY1DAT (); break;
	case 0x00E: v = CLXDAT (); break;
	case 0x010: v = ADKCONR (); break;

	case 0x012: v = POT0DAT (); break;
	case 0x014: v = POT1DAT (); break;
	case 0x016: v = POTGOR (); break;
	case 0x018: v = SERDATR (); break;
	case 0x01A: v = DSKBYTR (hpos); break;
	case 0x01C: v = INTENAR (); break;
	case 0x01E: v = INTREQR (); break;
	case 0x07C: v = DENISEID (); break;

#ifdef AGA
	case 0x180: case 0x182: case 0x184: case 0x186: case 0x188: case 0x18A:
	case 0x18C: case 0x18E: case 0x190: case 0x192: case 0x194: case 0x196:
	case 0x198: case 0x19A: case 0x19C: case 0x19E: case 0x1A0: case 0x1A2:
	case 0x1A4: case 0x1A6: case 0x1A8: case 0x1AA: case 0x1AC: case 0x1AE:
	case 0x1B0: case 0x1B2: case 0x1B4: case 0x1B6: case 0x1B8: case 0x1BA:
	case 0x1BC: case 0x1BE:
		v = COLOR_READ ((addr & 0x3E) / 2);
		break;
#endif

	default:
		/* OCS/ECS:
		* reading write-only register causes write with last value in chip
		* bus (custom registers, chipram, slowram)
		* and finally returns either all ones or something weird if DMA happens
		* in next (or previous) cycle.. FIXME.
		*
		* OCS-only special case: DFF000 (BLTDDAT) will always return whatever was left in bus
		*
		* AGA:
		* only writes to custom registers change last value, read returns
		* last value which then changes to all ones (following read will return
		* all ones)
		*/
		v = last_custom_value1;
		if (!noput) {
			int r;
			uae_u16 old = last_custom_value1;
			uae_u16 l = currprefs.cpu_compatible && currprefs.cpu_model == 68000 ? regs.irc : 0xffff;
			decide_line (hpos);
			decide_fetch (hpos);
			decide_blitter (hpos);
			debug_wputpeek (0xdff000 + addr, l);
			r = custom_wput_1 (hpos, addr, l, 1);
			if (r) { // register don't exist
				if (currprefs.chipset_mask & CSMASK_ECS_AGNUS) {
					v = l;
				} else {
					if ((addr & 0x1fe) == 0) {
						if (is_cycle_ce ())
							v = last_custom_value1;
						else
							v = l;
					}
				}
			} else {
				v = 0xffff;
			}
#if CUSTOM_DEBUG > 0
			write_log (_T("%08X read = %04X. Value written=%04X PC=%08x\n"), 0xdff000 | addr, v, l, M68K_GETPC);
#endif
			return v;
		}
	}
	last_custom_value1 = v;
	return v;
}

STATIC_INLINE uae_u32 custom_wget2 (uaecptr addr)
{
	uae_u32 v;
	int hpos = current_hpos ();

	sync_copper_with_cpu (hpos, 1);
	v = custom_wget_1 (hpos, addr, 0);
#ifdef ACTION_REPLAY
#ifdef ACTION_REPLAY_COMMON
	addr &= 0x1ff;
	ar_custom[addr + 0] = (uae_u8)(v >> 8);
	ar_custom[addr + 1] = (uae_u8)(v);
#endif
#endif
	return v;
}

static uae_u32 REGPARAM2 custom_wget (uaecptr addr)
{
	uae_u32 v;

	if (addr & 1) {
		/* think about move.w $dff005,d0.. (68020+ only) */
		addr &= ~1;
		v = custom_wget2 (addr) << 8;
		v |= custom_wget2 (addr + 2) >> 8;
		return v;
	}
	return custom_wget2 (addr);
}

static uae_u32 REGPARAM2 custom_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = custom_wget2 (addr & ~1);
	v >>= (addr & 1 ? 0 : 8);
	return v;
}

static uae_u32 REGPARAM2 custom_lget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return ((uae_u32)custom_wget (addr) << 16) | custom_wget (addr + 2);
}
static int REGPARAM2 custom_wput_1 (int hpos, uaecptr addr, uae_u32 value, int noget)
{
	if (!noget)
		last_custom_value1 = value;
	addr &= 0x1FE;
	value &= 0xffff;
#ifdef ACTION_REPLAY
#ifdef ACTION_REPLAY_COMMON
	ar_custom[addr+0]=(uae_u8)(value>>8);
	ar_custom[addr+1]=(uae_u8)(value);
#endif
#endif
	switch (addr) {
	case 0x00E: CLXDAT (); break;

	case 0x020: DSKPTH (value); break;
	case 0x022: DSKPTL (value); break;
	case 0x024: DSKLEN (value, hpos); break;
	case 0x026: DSKDAT (value); break;

	case 0x02A: VPOSW (value); break;
	case 0x02C: VHPOSW (value); break;
	case 0x02E: COPCON (value); break;
	case 0x030: SERDAT (value); break;
	case 0x032: SERPER (value); break;
	case 0x034: POTGO (value); break;

	case 0x040: BLTCON0 (hpos, value); break;
	case 0x042: BLTCON1 (hpos, value); break;

	case 0x044: BLTAFWM (hpos, value); break;
	case 0x046: BLTALWM (hpos, value); break;

	case 0x050: BLTAPTH (hpos, value); break;
	case 0x052: BLTAPTL (hpos, value); break;
	case 0x04C: BLTBPTH (hpos, value); break;
	case 0x04E: BLTBPTL (hpos, value); break;
	case 0x048: BLTCPTH (hpos, value); break;
	case 0x04A: BLTCPTL (hpos, value); break;
	case 0x054: BLTDPTH (hpos, value); break;
	case 0x056: BLTDPTL (hpos, value); break;

	case 0x058: BLTSIZE (hpos, value); break;

	case 0x064: BLTAMOD (hpos, value); break;
	case 0x062: BLTBMOD (hpos, value); break;
	case 0x060: BLTCMOD (hpos, value); break;
	case 0x066: BLTDMOD (hpos, value); break;

	case 0x070: BLTCDAT (hpos, value); break;
	case 0x072: BLTBDAT (hpos, value); break;
	case 0x074: BLTADAT (hpos, value); break;

	case 0x07E: DSKSYNC (hpos, value); break;

	case 0x080: COP1LCH (value); break;
	case 0x082: COP1LCL (value); break;
	case 0x084: COP2LCH (value); break;
	case 0x086: COP2LCL (value); break;

	case 0x088: COPJMP (1, 0); break;
	case 0x08A: COPJMP (2, 0); break;

	case 0x08E: DIWSTRT (hpos, value); break;
	case 0x090: DIWSTOP (hpos, value); break;
	case 0x092: DDFSTRT (hpos, value); break;
	case 0x094: DDFSTOP (hpos, value); break;

	case 0x096: DMACON (hpos, value); break;
	case 0x098: CLXCON (value); break;
	case 0x09A: INTENA (value); break;
	case 0x09C: INTREQ (value); break;
	case 0x09E: ADKCON (hpos, value); break;

	case 0x0A0: AUDxLCH (0, value); break;
	case 0x0A2: AUDxLCL (0, value); break;
	case 0x0A4: AUDxLEN (0, value); break;
	case 0x0A6: AUDxPER (0, value); break;
	case 0x0A8: AUDxVOL (0, value); break;
	case 0x0AA: AUDxDAT (0, value); break;

	case 0x0B0: AUDxLCH (1, value); break;
	case 0x0B2: AUDxLCL (1, value); break;
	case 0x0B4: AUDxLEN (1, value); break;
	case 0x0B6: AUDxPER (1, value); break;
	case 0x0B8: AUDxVOL (1, value); break;
	case 0x0BA: AUDxDAT (1, value); break;

	case 0x0C0: AUDxLCH (2, value); break;
	case 0x0C2: AUDxLCL (2, value); break;
	case 0x0C4: AUDxLEN (2, value); break;
	case 0x0C6: AUDxPER (2, value); break;
	case 0x0C8: AUDxVOL (2, value); break;
	case 0x0CA: AUDxDAT (2, value); break;

	case 0x0D0: AUDxLCH (3, value); break;
	case 0x0D2: AUDxLCL (3, value); break;
	case 0x0D4: AUDxLEN (3, value); break;
	case 0x0D6: AUDxPER (3, value); break;
	case 0x0D8: AUDxVOL (3, value); break;
	case 0x0DA: AUDxDAT (3, value); break;

	case 0x0E0: BPLxPTH (hpos, value, 0); break;
	case 0x0E2: BPLxPTL (hpos, value, 0); break;
	case 0x0E4: BPLxPTH (hpos, value, 1); break;
	case 0x0E6: BPLxPTL (hpos, value, 1); break;
	case 0x0E8: BPLxPTH (hpos, value, 2); break;
	case 0x0EA: BPLxPTL (hpos, value, 2); break;
	case 0x0EC: BPLxPTH (hpos, value, 3); break;
	case 0x0EE: BPLxPTL (hpos, value, 3); break;
	case 0x0F0: BPLxPTH (hpos, value, 4); break;
	case 0x0F2: BPLxPTL (hpos, value, 4); break;
	case 0x0F4: BPLxPTH (hpos, value, 5); break;
	case 0x0F6: BPLxPTL (hpos, value, 5); break;
	case 0x0F8: BPLxPTH (hpos, value, 6); break;
	case 0x0FA: BPLxPTL (hpos, value, 6); break;
	case 0x0FC: BPLxPTH (hpos, value, 7); break;
	case 0x0FE: BPLxPTL (hpos, value, 7); break;

	case 0x100: BPLCON0 (hpos, value); break;
	case 0x102: BPLCON1 (hpos, value); break;
	case 0x104: BPLCON2 (hpos, value); break;
#ifdef ECS_DENISE
	case 0x106: BPLCON3 (hpos, value); break;
#endif

	case 0x108: BPL1MOD (hpos, value); break;
	case 0x10A: BPL2MOD (hpos, value); break;
#ifdef AGA
	case 0x10E: CLXCON2 (value); break;
#endif

	case 0x110: BPLxDAT (hpos, 0, value); break;
	case 0x112: BPLxDAT (hpos, 1, value); break;
	case 0x114: BPLxDAT (hpos, 2, value); break;
	case 0x116: BPLxDAT (hpos, 3, value); break;
	case 0x118: BPLxDAT (hpos, 4, value); break;
	case 0x11A: BPLxDAT (hpos, 5, value); break;
	case 0x11C: BPLxDAT (hpos, 6, value); break;
	case 0x11E: BPLxDAT (hpos, 7, value); break;

	case 0x180: case 0x182: case 0x184: case 0x186: case 0x188: case 0x18A:
	case 0x18C: case 0x18E: case 0x190: case 0x192: case 0x194: case 0x196:
	case 0x198: case 0x19A: case 0x19C: case 0x19E: case 0x1A0: case 0x1A2:
	case 0x1A4: case 0x1A6: case 0x1A8: case 0x1AA: case 0x1AC: case 0x1AE:
	case 0x1B0: case 0x1B2: case 0x1B4: case 0x1B6: case 0x1B8: case 0x1BA:
	case 0x1BC: case 0x1BE:
		COLOR_WRITE (hpos, value & 0xFFF, (addr & 0x3E) / 2);
		break;
	case 0x120: case 0x124: case 0x128: case 0x12C:
	case 0x130: case 0x134: case 0x138: case 0x13C:
		SPRxPTH (hpos, value, (addr - 0x120) / 4);
		break;
	case 0x122: case 0x126: case 0x12A: case 0x12E:
	case 0x132: case 0x136: case 0x13A: case 0x13E:
		SPRxPTL (hpos, value, (addr - 0x122) / 4);
		break;
	case 0x140: case 0x148: case 0x150: case 0x158:
	case 0x160: case 0x168: case 0x170: case 0x178:
		SPRxPOS (hpos, value, (addr - 0x140) / 8);
		break;
	case 0x142: case 0x14A: case 0x152: case 0x15A:
	case 0x162: case 0x16A: case 0x172: case 0x17A:
		SPRxCTL (hpos, value, (addr - 0x142) / 8);
		break;
	case 0x144: case 0x14C: case 0x154: case 0x15C:
	case 0x164: case 0x16C: case 0x174: case 0x17C:
		SPRxDATA (hpos, value, (addr - 0x144) / 8);
		break;
	case 0x146: case 0x14E: case 0x156: case 0x15E:
	case 0x166: case 0x16E: case 0x176: case 0x17E:
		SPRxDATB (hpos, value, (addr - 0x146) / 8);
		break;

	case 0x36: JOYTEST (value); break;
	case 0x5A: BLTCON0L (hpos, value); break;
	case 0x5C: BLTSIZV (hpos, value); break;
	case 0x5E: BLTSIZH (hpos, value); break;
	case 0x1E4: DIWHIGH (hpos, value); break;
#ifdef AGA
	case 0x10C: BPLCON4 (hpos, value); break;
#endif

#ifndef CUSTOM_SIMPLE
	case 0x1DC: BEAMCON0 (value); break;
#ifdef ECS_DENISE
	case 0x1C0: if (htotal != value) { htotal = value; varsync (); } break;
	case 0x1C2: if (hsstop != value) { hsstop = value; varsync (); } break;
	case 0x1C4: if (hbstrt != value) { hbstrt = value; varsync (); } break;
	case 0x1C6: if (hbstop != value) { hbstop = value; varsync (); } break;
	case 0x1C8: if (vtotal != value) { vtotal = value; varsync (); } break;
	case 0x1CA: if (vsstop != value) { vsstop = value; varsync (); } break;
	case 0x1CC: if (vbstrt < value || vbstrt > value + 1) { vbstrt = value; varsync (); } break;
	case 0x1CE: if (vbstop < value || vbstop > value + 1) { vbstop = value; varsync (); } break;
	case 0x1DE: if (hsstrt != value) { hsstrt = value; varsync (); } break;
	case 0x1E0: if (vsstrt != value) { vsstrt = value; varsync (); } break;
	case 0x1E2: if (hcenter != value) { hcenter = value; varsync (); } break;
#endif
#endif

#ifdef AGA
	case 0x1FC: FMODE (hpos, value); break;
#endif
	case 0x1FE: FNULL (value); break;

		/* writing to read-only register causes read access */
	default:
		if (!noget) {
#if CUSTOM_DEBUG > 0
			write_log (_T("%04X written %08x\n"), addr, M68K_GETPC);
#endif
			custom_wget_1 (hpos, addr, 1);
		}
		return 1;
	}
	return 0;
}

static void REGPARAM2 custom_wput (uaecptr addr, uae_u32 value)
{
	int hpos = current_hpos ();
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CUSTOM_DEBUG > 2
	write_log (_T("%d:%d:wput: %04X %04X pc=%p\n"), hpos, vpos, addr & 0x01fe, value & 0xffff, m68k_getpc ());
#endif
	sync_copper_with_cpu (hpos, 1);
	if (addr & 1) {
		addr &= ~1;
		custom_wput_1 (hpos, addr, (value >> 8) | (value & 0xff00), 0);
		custom_wput_1 (hpos, addr + 2, (value << 8) | (value & 0x00ff), 0);
		return;
	}
	custom_wput_1 (hpos, addr, value, 0);
}

static void REGPARAM2 custom_bput (uaecptr addr, uae_u32 value)
{
	static int warned;
	uae_u16 rval;

	if (currprefs.chipset_mask & CSMASK_AGA) {
		if (addr & 1) {
			rval = value & 0xff;
		} else {
			rval = (value << 8) | (value & 0xFF);
		}
	} else {
		rval = (value << 8) | (value & 0xff);
	}

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.cpu_model == 68060) {
		if (addr & 1)
			custom_wput (addr & ~1, rval);
		else
			custom_wput (addr, value << 8);
	} else {
		custom_wput (addr & ~1, rval);
	}
}

static void REGPARAM2 custom_lput(uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	custom_wput (addr & 0xfffe, value >> 16);
	custom_wput ((addr + 2) & 0xfffe, (uae_u16)value);
}

#ifdef SAVESTATE

void custom_prepare_savestate (void)
{
	int i;

	for (i = 0; i < ev2_max; i++) {
		if (eventtab2[i].active) {
			eventtab2[i].active = 0;
			eventtab2[i].handler (eventtab2[i].data);
		}
	}
}

#define RB restore_u8 ()
#define SRB (uae_s8)restore_u8 ()
#define RBB restore_u8 () != 0
#define RW restore_u16 ()
#define RL restore_u32 ()

uae_u8 *restore_custom (uae_u8 *src)
{
	uae_u16 dsklen, dskbytr;
	int dskpt;
	int i;

	audio_reset ();

	changed_prefs.chipset_mask = currprefs.chipset_mask = RL & CSMASK_MASK;
	update_mirrors ();
	RW;						/* 000 BLTDDAT */
	RW;						/* 002 DMACONR */
	RW;						/* 004 VPOSR */
	RW;						/* 006 VHPOSR */
	RW;						/* 008 DSKDATR (dummy register) */
	JOYSET(0, RW);			/* 00A JOY0DAT */
	JOYSET(1, RW);			/* 00C JOY1DAT */
	clxdat = RW;			/* 00E CLXDAT */
	RW;						/* 010 ADKCONR */
	RW;						/* 012 POT0DAT* */
	RW;						/* 014 POT1DAT* */
	RW;						/* 016 POTINP* */
	RW;						/* 018 SERDATR* */
	dskbytr = RW;			/* 01A DSKBYTR */
	RW;						/* 01C INTENAR */
	RW;						/* 01E INTREQR */
	dskpt = RL;				/* 020-022 DSKPT */
	dsklen = RW;			/* 024 DSKLEN */
	RW;						/* 026 DSKDAT */
	RW;						/* 028 REFPTR */
	i = RW; lof_store = lof_current = (i & 0x8000) ? 1 : 0; lol = (i & 0x0080) ? 1 : 0; /* 02A VPOSW */
	RW;						/* 02C VHPOSW */
	COPCON (RW);			/* 02E COPCON */
	RW;						/* 030 SERDAT* */
	RW;						/* 032 SERPER* */
	POTGO (RW);				/* 034 POTGO */
	RW;						/* 036 JOYTEST* */
	RW;						/* 038 STREQU */
	RW;						/* 03A STRVHBL */
	RW;						/* 03C STRHOR */
	RW;						/* 03E STRLONG */
	BLTCON0 (0, RW);		/* 040 BLTCON0 */
	BLTCON1 (0, RW);		/* 042 BLTCON1 */
	BLTAFWM (0, RW);		/* 044 BLTAFWM */
	BLTALWM (0, RW);		/* 046 BLTALWM */
	BLTCPTH (0, RW);BLTCPTL(0, RW);	/* 048-04B BLTCPT */
	BLTBPTH (0, RW);BLTBPTL(0, RW);	/* 04C-04F BLTBPT */
	BLTAPTH (0, RW);BLTAPTL(0, RW);	/* 050-053 BLTAPT */
	BLTDPTH (0, RW);BLTDPTL(0, RW);	/* 054-057 BLTDPT */
	RW;						/* 058 BLTSIZE */
	RW;						/* 05A BLTCON0L */
	blt_info.vblitsize = RW;/* 05C BLTSIZV */
	blt_info.hblitsize = RW;/* 05E BLTSIZH */
	BLTCMOD (0, RW);		/* 060 BLTCMOD */
	BLTBMOD (0, RW);		/* 062 BLTBMOD */
	BLTAMOD (0, RW);		/* 064 BLTAMOD */
	BLTDMOD (0, RW);		/* 066 BLTDMOD */
	RW;						/* 068 ? */
	RW;						/* 06A ? */
	RW;						/* 06C ? */
	RW;						/* 06E ? */
	BLTCDAT (0, RW);		/* 070 BLTCDAT */
	BLTBDAT (0, RW);		/* 072 BLTBDAT */
	BLTADAT (0, RW);		/* 074 BLTADAT */
	RW;						/* 076 ? */
	RW;						/* 078 ? */
	RW;						/* 07A ? */
	RW;						/* 07C LISAID */
	DSKSYNC (-1, RW);		/* 07E DSKSYNC */
	cop1lc = RL;			/* 080/082 COP1LC */
	cop2lc = RL;			/* 084/086 COP2LC */
	RW;						/* 088 ? */
	RW;						/* 08A ? */
	RW;						/* 08C ? */
	diwstrt = RW;			/* 08E DIWSTRT */
	diwstop = RW;			/* 090 DIWSTOP */
	ddfstrt = RW;			/* 092 DDFSTRT */
	ddfstop = RW;			/* 094 DDFSTOP */
	dmacon = RW & ~(0x2000|0x4000); /* 096 DMACON */
	CLXCON (RW);			/* 098 CLXCON */
	intena = intena_internal = RW;	/* 09A INTENA */
	intreq = RW;			/* 09C INTREQ */
	intreq_internal = intreq;
	adkcon = RW;			/* 09E ADKCON */
	for (i = 0; i < 8; i++)
		bplptx[i] = bplpt[i] = RL;
	bplcon0 = RW;			/* 100 BPLCON0 */
	bplcon1 = RW;			/* 102 BPLCON1 */
	bplcon2 = RW;			/* 104 BPLCON2 */
	bplcon3 = RW;			/* 106 BPLCON3 */
	bpl1mod = RW;			/* 108 BPL1MOD */
	bpl2mod = RW;			/* 10A BPL2MOD */
	bplcon4 = RW;			/* 10C BPLCON4 */
	clxcon2 = RW;			/* 10E CLXCON2* */
	for(i = 0; i < 8; i++)
		bplxdat[i] = RW;	/*     BPLXDAT */
	for(i = 0; i < 32; i++)
		current_colors.color_regs_ecs[i] = RW; /* 180 COLORxx */
	htotal = RW;			/* 1C0 HTOTAL */
	hsstop = RW;			/* 1C2 HSTOP ? */
	hbstrt = RW;			/* 1C4 HBSTRT ? */
	hbstop = RW;			/* 1C6 HBSTOP ? */
	vtotal = RW;			/* 1C8 VTOTAL */
	vsstop = RW;			/* 1CA VSSTOP */
	vbstrt = RW;			/* 1CC VBSTRT */
	vbstop = RW;			/* 1CE VBSTOP */
	RW;						/* 1D0 ? */
	RW;						/* 1D2 ? */
	RW;						/* 1D4 ? */
	RW;						/* 1D6 ? */
	RW;						/* 1D8 ? */
	RW;						/* 1DA ? */
	new_beamcon0 = RW;		/* 1DC BEAMCON0 */
	hsstrt = RW;			/* 1DE HSSTRT */
	vsstrt = RW;			/* 1E0 VSSTT  */
	hcenter = RW;			/* 1E2 HCENTER */
	diwhigh = RW;			/* 1E4 DIWHIGH */
	diwhigh_written = (diwhigh & 0x8000) ? 1 : 0;
	hdiwstate = (diwhigh & 0x4000) ? DIW_waiting_stop : DIW_waiting_start;
	diwhigh &= 0x3fff;
	RW;						/* 1E6 ? */
	RW;						/* 1E8 ? */
	RW;						/* 1EA ? */
	RW;						/* 1EC ? */
	RW;						/* 1EE ? */
	RW;						/* 1F0 ? */
	RW;						/* 1F2 ? */
	RW;						/* 1F4 ? */
	RW;						/* 1F6 ? */
	RW;						/* 1F8 ? */
	i = RW;					/* 1FA ? */
	if (i & 0x8000)
		currprefs.ntscmode = changed_prefs.ntscmode = i & 1;
	fmode = RW;				/* 1FC FMODE */
	last_custom_value1 = RW;/* 1FE ? */

	DISK_restore_custom (dskpt, dsklen, dskbytr);

	return src;
}

#endif /* SAVESTATE */

#if defined SAVESTATE || defined DEBUGGER

#define SB save_u8
#define SW save_u16
#define SL save_u32

extern uae_u16 serper;

uae_u8 *save_custom (int *len, uae_u8 *dstptr, int full)
{
	uae_u8 *dstbak, *dst;
	int i;
	uae_u32 dskpt;
	uae_u16 dsklen, dsksync, dskbytr;

	DISK_save_custom (&dskpt, &dsklen, &dsksync, &dskbytr);

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 8 + 256 * 2);

	SL (currprefs.chipset_mask);
	SW (0);					/* 000 BLTDDAT */
	SW (dmacon);			/* 002 DMACONR */
	SW (VPOSR ());			/* 004 VPOSR */
	SW (VHPOSR ());			/* 006 VHPOSR */
	SW (0);					/* 008 DSKDATR */
	SW (JOYGET (0));		/* 00A JOY0DAT */
	SW (JOYGET (1));		/* 00C JOY1DAT */
	SW (clxdat | 0x8000);	/* 00E CLXDAT */
	SW (ADKCONR ());		/* 010 ADKCONR */
	SW (POT0DAT ());		/* 012 POT0DAT */
	SW (POT1DAT ());		/* 014 POT1DAT */
	SW (0)	;				/* 016 POTINP * */
	SW (0);					/* 018 SERDATR * */
	SW (dskbytr);			/* 01A DSKBYTR */
	SW (INTENAR ());		/* 01C INTENAR */
	SW (INTREQR ());		/* 01E INTREQR */
	SL (dskpt);				/* 020-023 DSKPT */
	SW (dsklen);			/* 024 DSKLEN */
	SW (0);					/* 026 DSKDAT */
	SW (0);					/* 028 REFPTR */
	SW ((lof_store ? 0x8001 : 0) | (lol ? 0x0080 : 0));/* 02A VPOSW */
	SW (0);					/* 02C VHPOSW */
	SW (copcon);			/* 02E COPCON */
	SW (serper);			/* 030 SERDAT * */
	SW (serdat);			/* 032 SERPER * */
	SW (potgo_value);		/* 034 POTGO */
	SW (0);					/* 036 JOYTEST * */
	SW (0);					/* 038 STREQU */
	SW (0);					/* 03A STRVBL */
	SW (0);					/* 03C STRHOR */
	SW (0);					/* 03E STRLONG */
	SW (bltcon0);			/* 040 BLTCON0 */
	SW (bltcon1);			/* 042 BLTCON1 */
	SW (blt_info.bltafwm);	/* 044 BLTAFWM */
	SW (blt_info.bltalwm);	/* 046 BLTALWM */
	SL (bltcpt);			/* 048-04B BLTCPT */
	SL (bltbpt);			/* 04C-04F BLTCPT */
	SL (bltapt);			/* 050-053 BLTCPT */
	SL (bltdpt);			/* 054-057 BLTCPT */
	SW (0);					/* 058 BLTSIZE */
	SW (0);					/* 05A BLTCON0L (use BLTCON0 instead) */
	SW (blt_info.vblitsize);/* 05C BLTSIZV */
	SW (blt_info.hblitsize);/* 05E BLTSIZH */
	SW (blt_info.bltcmod);	/* 060 BLTCMOD */
	SW (blt_info.bltbmod);	/* 062 BLTBMOD */
	SW (blt_info.bltamod);	/* 064 BLTAMOD */
	SW (blt_info.bltdmod);	/* 066 BLTDMOD */
	SW (0);					/* 068 ? */
	SW (0);					/* 06A ? */
	SW (0);					/* 06C ? */
	SW (0);					/* 06E ? */
	SW (blt_info.bltcdat);	/* 070 BLTCDAT */
	SW (blt_info.bltbdat);	/* 072 BLTBDAT */
	SW (blt_info.bltadat);	/* 074 BLTADAT */
	SW (0);					/* 076 ? */
	SW (0);					/* 078 ? */
	SW (0);					/* 07A ? */
	SW (DENISEID ());		/* 07C DENISEID/LISAID */
	SW (dsksync);			/* 07E DSKSYNC */
	SL (cop1lc);			/* 080-083 COP1LC */
	SL (cop2lc);			/* 084-087 COP2LC */
	SW (0);					/* 088 ? */
	SW (0);					/* 08A ? */
	SW (0);					/* 08C ? */
	SW (diwstrt);			/* 08E DIWSTRT */
	SW (diwstop);			/* 090 DIWSTOP */
	SW (ddfstrt);			/* 092 DDFSTRT */
	SW (ddfstop);			/* 094 DDFSTOP */
	SW (dmacon);			/* 096 DMACON */
	SW (clxcon);			/* 098 CLXCON */
	SW (intena);			/* 09A INTENA */
	SW (intreq);			/* 09C INTREQ */
	SW (adkcon);			/* 09E ADKCON */
	for (i = 0; full && i < 32; i++)
		SW (0);
	for (i = 0; i < 8; i++)
		SL (bplpt[i]);		/* 0E0-0FE BPLxPT */
	SW (bplcon0);			/* 100 BPLCON0 */
	SW (bplcon1);			/* 102 BPLCON1 */
	SW (bplcon2);			/* 104 BPLCON2 */
	SW (bplcon3);			/* 106 BPLCON3 */
	SW (bpl1mod);			/* 108 BPL1MOD */
	SW (bpl2mod);			/* 10A BPL2MOD */
	SW (bplcon4);			/* 10C BPLCON4 */
	SW (clxcon2);			/* 10E CLXCON2 */
	for (i = 0;i < 8; i++)
		SW (bplxdat[i]);	/* 110 BPLxDAT */
	if (full) {
		for (i = 0; i < 8; i++) {
			SL (spr[i].pt);	/* 120-13E SPRxPT */
			SW (sprpos[i]);	/* 1x0 SPRxPOS */
			SW (sprctl[i]);	/* 1x2 SPRxPOS */
			SW (sprdata[i][0]);	/* 1x4 SPRxDATA */
			SW (sprdatb[i][0]);	/* 1x6 SPRxDATB */
		}
	}
	for ( i = 0; i < 32; i++) {
		if (currprefs.chipset_mask & CSMASK_AGA) {
			uae_u32 v = current_colors.color_regs_aga[i];
			uae_u16 v2;
			v &= 0x00f0f0f0;
			v2 = (v >> 4) & 15;
			v2 |= ((v >> 12) & 15) << 4;
			v2 |= ((v >> 20) & 15) << 8;
			SW (v2);
		} else {
			SW (current_colors.color_regs_ecs[i]); /* 180-1BE COLORxx */
		}
	}
	SW (htotal);		/* 1C0 HTOTAL */
	SW (hsstop);		/* 1C2 HSTOP*/
	SW (hbstrt);		/* 1C4 HBSTRT */
	SW (hbstop);		/* 1C6 HBSTOP */
	SW (vtotal);		/* 1C8 VTOTAL */
	SW (vsstop);		/* 1CA VSSTOP */
	SW (vbstrt);		/* 1CC VBSTRT */
	SW (vbstop);		/* 1CE VBSTOP */
	SW (0);			/* 1D0 */
	SW (0);			/* 1D2 */
	SW (0);			/* 1D4 */
	SW (0);			/* 1D6 */
	SW (0);			/* 1D8 */
	SW (0);			/* 1DA */
	SW (beamcon0);		/* 1DC BEAMCON0 */
	SW (hsstrt);		/* 1DE HSSTRT */
	SW (vsstrt);		/* 1E0 VSSTRT */
	SW (hcenter);		/* 1E2 HCENTER */
	SW (diwhigh | (diwhigh_written ? 0x8000 : 0) | (hdiwstate == DIW_waiting_stop ? 0x4000 : 0)); /* 1E4 DIWHIGH */
	SW (0);			/* 1E6 */
	SW (0);			/* 1E8 */
	SW (0);			/* 1EA */
	SW (0);			/* 1EC */
	SW (0);			/* 1EE */
	SW (0);			/* 1F0 */
	SW (0);			/* 1F2 */
	SW (0);			/* 1F4 */
	SW (0);			/* 1F6 */
	SW (0);			/* 1F8 */
	SW (0x8000 | (currprefs.ntscmode ? 1 : 0));			/* 1FA (re-used for NTSC) */
	SW (fmode);			/* 1FC FMODE */
	SW (last_custom_value1);	/* 1FE */

	*len = dst - dstbak;
	return dstbak;
}

#endif /* SAVESTATE || DEBUGGER */

#ifdef SAVESTATE

uae_u8 *restore_custom_agacolors (uae_u8 *src)
{
	int i;

	for (i = 0; i < 256; i++) {
#ifdef AGA
		uae_u32 v = RL;
		color_regs_aga_genlock[i] = 0;
		if (v & 0x80000000)
			color_regs_aga_genlock[i] = 1;
		v &= 0x00ffffff;
		current_colors.color_regs_aga[i] = v;
#else
		RL;
#endif
	}
	return src;
}

uae_u8 *save_custom_agacolors (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int i;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 256 * 4);
	for (i = 0; i < 256; i++)
#ifdef AGA
		SL (current_colors.color_regs_aga[i] | (color_regs_aga_genlock[i] ? 0x80000000 : 0));
#else
		SL (0);
#endif
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_custom_sprite (int num, uae_u8 *src)
{
	memset (&spr[num], 0, sizeof (struct sprite));
	spr[num].pt = RL;		/* 120-13E SPRxPT */
	sprpos[num] = RW;		/* 1x0 SPRxPOS */
	sprctl[num] = RW;		/* 1x2 SPRxPOS */
	sprdata[num][0] = RW;	/* 1x4 SPRxDATA */
	sprdatb[num][0] = RW;	/* 1x6 SPRxDATB */
	sprdata[num][1] = RW;
	sprdatb[num][1] = RW;
	sprdata[num][2] = RW;
	sprdatb[num][2] = RW;
	sprdata[num][3] = RW;
	sprdatb[num][3] = RW;
	spr[num].armed = RB;
	return src;
}

uae_u8 *save_custom_sprite (int num, int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 30);
	SL (spr[num].pt);		/* 120-13E SPRxPT */
	SW (sprpos[num]);		/* 1x0 SPRxPOS */
	SW (sprctl[num]);		/* 1x2 SPRxPOS */
	SW (sprdata[num][0]);	/* 1x4 SPRxDATA */
	SW (sprdatb[num][0]);	/* 1x6 SPRxDATB */
	SW (sprdata[num][1]);
	SW (sprdatb[num][1]);
	SW (sprdata[num][2]);
	SW (sprdatb[num][2]);
	SW (sprdata[num][3]);
	SW (sprdatb[num][3]);
	SB (spr[num].armed ? 1 : 0);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_custom_extra (uae_u8 *src)
{
	uae_u32 v = restore_u32 ();

	if (!(v & 1))
		v = 0;
	currprefs.cs_compatible = changed_prefs.cs_compatible = v >> 24;

	currprefs.genlock = changed_prefs.genlock = RBB;
	currprefs.cs_rtc = changed_prefs.cs_rtc = RB;
	currprefs.cs_rtc_adjust = changed_prefs.cs_rtc_adjust = RL;

	currprefs.cs_a1000ram = changed_prefs.cs_a1000ram = RBB;
	currprefs.cs_slowmemisfast = changed_prefs.cs_slowmemisfast = RBB;

	currprefs.cs_a2091 = changed_prefs.cs_a2091 = RBB;
	currprefs.cs_a4091 = changed_prefs.cs_a4091 = RBB;
	currprefs.cs_cdtvscsi = changed_prefs.cs_cdtvscsi = RBB;

	currprefs.cs_pcmcia = changed_prefs.cs_pcmcia = RBB;
	currprefs.cs_ciaatod = changed_prefs.cs_ciaatod = RB;
	currprefs.cs_ciaoverlay = changed_prefs.cs_ciaoverlay = RBB;

	currprefs.cs_agnusbltbusybug = changed_prefs.cs_agnusbltbusybug = RBB;
	currprefs.cs_denisenoehb = changed_prefs.cs_denisenoehb = RBB;

	currprefs.cs_agnusrev = changed_prefs.cs_agnusrev = SRB;
	currprefs.cs_deniserev = changed_prefs.cs_deniserev = SRB;
	currprefs.cs_fatgaryrev = changed_prefs.cs_fatgaryrev = SRB;
	currprefs.cs_ramseyrev = changed_prefs.cs_ramseyrev = SRB;

	currprefs.cs_cd32c2p = changed_prefs.cs_cd32c2p = RBB;
	currprefs.cs_cd32cd = changed_prefs.cs_cd32cd = RBB;
	currprefs.cs_cd32nvram = changed_prefs.cs_cd32nvram = RBB;
	currprefs.cs_cdtvcd = changed_prefs.cs_cdtvcd = RBB;
	currprefs.cs_cdtvram = changed_prefs.cs_cdtvram = RBB;
	currprefs.cs_cdtvcard = changed_prefs.cs_cdtvcard = RB;

	currprefs.cs_df0idhw = changed_prefs.cs_df0idhw = RBB;
	currprefs.cs_dipagnus = changed_prefs.cs_dipagnus = RBB;
	currprefs.cs_ide = changed_prefs.cs_ide = RB;
	currprefs.cs_mbdmac = changed_prefs.cs_mbdmac = RB;
	currprefs.cs_ksmirror_a8 = changed_prefs.cs_ksmirror_a8 = RBB;
	currprefs.cs_ksmirror_e0 = changed_prefs.cs_ksmirror_e0 = RBB;
	currprefs.cs_resetwarning = changed_prefs.cs_resetwarning = RBB;

	return src;
}

uae_u8 *save_custom_extra (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);

	SL ((currprefs.cs_compatible << 24) | 1);
	SB (currprefs.genlock ? 1 : 0);
	SB (currprefs.cs_rtc);
	SL (currprefs.cs_rtc_adjust);

	SB (currprefs.cs_a1000ram ? 1 : 0);
	SB (currprefs.cs_slowmemisfast ? 1 : 0);

	SB (currprefs.cs_a2091 ? 1 : 0);
	SB (currprefs.cs_a4091 ? 1 : 0);
	SB (currprefs.cs_cdtvscsi ? 1 : 0);

	SB (currprefs.cs_pcmcia ? 1 : 0);
	SB (currprefs.cs_ciaatod);
	SB (currprefs.cs_ciaoverlay ? 1 : 0);

	SB (currprefs.cs_agnusbltbusybug ? 1 : 0);
	SB (currprefs.cs_denisenoehb ? 1 : 0);

	SB (currprefs.cs_agnusrev);
	SB (currprefs.cs_deniserev);
	SB (currprefs.cs_fatgaryrev);
	SB (currprefs.cs_ramseyrev);

	SB (currprefs.cs_cd32c2p);
	SB (currprefs.cs_cd32cd);
	SB (currprefs.cs_cd32nvram);
	SB (currprefs.cs_cdtvcd ? 1 : 0);
	SB (currprefs.cs_cdtvram ? 1 : 0);
	SB (currprefs.cs_cdtvcard);

	SB (currprefs.cs_df0idhw ? 1 : 0);
	SB (currprefs.cs_dipagnus ? 1 : 0);
	SB (currprefs.cs_ide);
	SB (currprefs.cs_mbdmac);
	SB (currprefs.cs_ksmirror_a8 ? 1 : 0);
	SB (currprefs.cs_ksmirror_e0 ? 1 : 0);
	SB (currprefs.cs_resetwarning ? 1 : 0);

	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_custom_event_delay (uae_u8 *src)
{
	if (restore_u32 () != 1)
		return src;
	int cnt = restore_u8 ();
	for (int i = 0; i < cnt; i++) {
		uae_u8 type = restore_u8 ();
		evt e = restore_u64 ();
		uae_u32 data = restore_u32 ();
		if (type == 1)
			event2_newevent_xx (-1, e, data, send_interrupt_do);
	}
	return src;
}
uae_u8 *save_custom_event_delay (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int cnt = 0;

	for (int i = ev2_misc;  i < ev2_max; i++) {
		struct ev2 *e = &eventtab2[i];
		if (e->active && e->handler == send_interrupt_do) {
			cnt++;
		}
	}
	if (cnt == 0)
		return NULL;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);

	save_u32 (1);
	save_u8 (cnt);
	for (int i = ev2_misc;  i < ev2_max; i++) {
		struct ev2 *e = &eventtab2[i];
		if (e->active && e->handler == send_interrupt_do) {
			save_u8 (1);
			save_u64 (e->evtime - get_cycles ());
			save_u32 (e->data);
		
		}
	}

	*len = dst - dstbak;
	return dstbak;
}


uae_u8 *save_cycles (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (1);
	save_u32 (CYCLE_UNIT);
	save_u64 (get_cycles ());
	save_u32 (extra_cycle);
	write_log (_T("SAVECYCLES %08X\n"), get_cycles ());
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_cycles (uae_u8 *src)
{
	if (restore_u32 () != 1)
		return src;
	restore_u32 ();
	start_cycles = restore_u64 ();
	extra_cycle = restore_u32 ();
	write_log (_T("RESTORECYCLES %08X\n"), start_cycles);
	return src;
}

#endif /* SAVESTATE */

void check_prefs_changed_custom (void)
{
	if (!config_changed)
		return;
	currprefs.gfx_framerate = changed_prefs.gfx_framerate;
	if (currprefs.turbo_emulation != changed_prefs.turbo_emulation)
		warpmode (changed_prefs.turbo_emulation);
	if (inputdevice_config_change_test ()) 
		inputdevice_copyconfig (&changed_prefs, &currprefs);
	currprefs.immediate_blits = changed_prefs.immediate_blits;
	currprefs.collision_level = changed_prefs.collision_level;

	currprefs.cs_ciaatod = changed_prefs.cs_ciaatod;
	currprefs.cs_rtc = changed_prefs.cs_rtc;
	currprefs.cs_cd32cd = changed_prefs.cs_cd32cd;
	currprefs.cs_cd32c2p = changed_prefs.cs_cd32c2p;
	currprefs.cs_cd32nvram = changed_prefs.cs_cd32nvram;
	currprefs.cs_cdtvcd = changed_prefs.cs_cdtvcd;
	currprefs.cs_ide = changed_prefs.cs_ide;
	currprefs.cs_pcmcia = changed_prefs.cs_pcmcia;
	currprefs.cs_fatgaryrev = changed_prefs.cs_fatgaryrev;
	currprefs.cs_ramseyrev = changed_prefs.cs_ramseyrev;
	currprefs.cs_agnusrev = changed_prefs.cs_agnusrev;
	currprefs.cs_deniserev = changed_prefs.cs_deniserev;
	currprefs.cs_mbdmac = changed_prefs.cs_mbdmac;
	currprefs.cs_df0idhw = changed_prefs.cs_df0idhw;
	currprefs.cs_slowmemisfast = changed_prefs.cs_slowmemisfast;

	if (currprefs.chipset_mask != changed_prefs.chipset_mask ||
		currprefs.picasso96_nocustom != changed_prefs.picasso96_nocustom ||
		currprefs.ntscmode != changed_prefs.ntscmode) {
			currprefs.picasso96_nocustom = changed_prefs.picasso96_nocustom;
			currprefs.chipset_mask = changed_prefs.chipset_mask;
			if (currprefs.ntscmode != changed_prefs.ntscmode) {
				currprefs.ntscmode = changed_prefs.ntscmode;
				new_beamcon0 = currprefs.ntscmode ? 0x00 : 0x20;
			}
			init_custom ();
	}
#ifdef GFXFILTER
	currprefs.gfx_filter_horiz_zoom = changed_prefs.gfx_filter_horiz_zoom;
	currprefs.gfx_filter_vert_zoom = changed_prefs.gfx_filter_vert_zoom;
	currprefs.gfx_filter_horiz_offset = changed_prefs.gfx_filter_horiz_offset;
	currprefs.gfx_filter_vert_offset = changed_prefs.gfx_filter_vert_offset;
	currprefs.gfx_filter_scanlines = changed_prefs.gfx_filter_scanlines;
#endif
}

#ifdef CPUEMU_12

STATIC_INLINE void sync_copper (int hpos)
{
	if (copper_enabled_thisline)
		update_copper (hpos);
}

STATIC_INLINE void decide_fetch_ce (int hpos)
{
	if ((ddf_change == vpos || ddf_change + 1 == vpos) && vpos < current_maxvpos ())
		decide_fetch (hpos);
}

#define BLIT_NASTY 4

// blitter not in nasty mode = CPU gets one cycle if it has been waiting
// at least 4 cycles (all DMA cycles count, not just blitter cycles, even
// blitter idle cycles do count!)

extern int cpu_tracer;
STATIC_INLINE int dma_cycle (void)
{
	int hpos, hpos_old;

	blitter_nasty = 1;
	if (cpu_tracer == -1)
		return current_hpos ();
	for (;;) {
		int bpldma;
		int blitpri = dmacon & DMA_BLITPRI;
		hpos_old = current_hpos ();
		hpos = hpos_old + 1;
		sync_copper (hpos);
		decide_line (hpos);
		decide_fetch_ce (hpos);
		bpldma = is_bitplane_dma (hpos_old);
		if (bltstate != BLT_done) {
			if (!blitpri && blitter_nasty >= BLIT_NASTY && cycle_line[hpos_old] == 0 && !bpldma) {
				alloc_cycle (hpos_old, CYCLE_CPUNASTY);
				break;
			}
			decide_blitter (hpos);
			// copper may have been waiting for the blitter
			sync_copper (hpos);
		}
		if (cycle_line[hpos_old] == 0 && !bpldma) {
			alloc_cycle (hpos_old, CYCLE_CPU);
			break;
		}
		regs.ce020memcycles -= CYCLE_UNIT;
		do_cycles (1 * CYCLE_UNIT);
		/* bus was allocated to dma channel, wait for next cycle.. */
	}
	return hpos_old;
}

STATIC_INLINE void checknasty (int hpos, int vpos)
{
	if (blitter_nasty >= BLIT_NASTY && !(dmacon & DMA_BLITPRI))
		record_dma_event (DMA_EVENT_BLITNASTY, hpos, vpos);
}

uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode)
{
	uae_u32 v = 0;
	int hpos;
	struct dma_rec *dr;

	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1000;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		dr = record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif
	if (mode < 0)
		v = get_long (addr);
	else if (mode > 0)
		v = get_word (addr);
	else if (mode == 0)
		v = get_byte (addr);

#ifdef DEBUGGER
	if (debug_dma)
		dr->dat = v;
#endif

	x_do_cycles_post (CYCLE_UNIT, v);
	return v;
}

uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode)
{
	uae_u32 v = 0;
	int hpos;
	struct dma_rec *dr;

	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1000;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		dr = record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif
	if (mode < 0)
		v = get_long (addr);
	else if (mode > 0)
		v = get_word (addr);
	else if (mode == 0)
		v = get_byte (addr);

#ifdef DEBUGGER
	if (debug_dma)
		dr->dat = v;
#endif

	regs.ce020memcycles -= CYCLE_UNIT;
	return v;
}

void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v)
{
	int hpos;

	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1100;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);
	x_do_cycles_post (CYCLE_UNIT, v);

}

void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v)
{
	int hpos;

	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1100;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);

	regs.ce020memcycles -= CYCLE_UNIT;
}

void do_cycles_ce (unsigned long cycles)
{
	unsigned long c;

	c = cycles + extra_cycle;
	while (c >= CYCLE_UNIT) {
		int hpos = current_hpos () + 1;
		sync_copper (hpos);
		decide_line (hpos);
		decide_fetch_ce (hpos);
		if (bltstate != BLT_done)
			decide_blitter (hpos);
		do_cycles (1 * CYCLE_UNIT);
		c -= CYCLE_UNIT;
	}
	extra_cycle = c;
}

int is_cycle_ce (void)
{
	int hpos = current_hpos ();
	return cycle_line[hpos];
}

#endif

bool ispal (void)
{
	if (beamcon0 & 0x80)
		return currprefs.ntscmode == 0;
	return maxvpos_nom >= MAXVPOS_NTSC + (MAXVPOS_PAL - MAXVPOS_NTSC) / 2;
}