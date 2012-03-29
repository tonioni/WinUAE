#ifndef EVENTS_H
#define EVENTS_H

 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Events
  * These are best for low-frequency events. Having too many of them,
  * or using them for events that occur too frequently, can cause massive
  * slowdown.
  *
  * Copyright 1995-1998 Bernd Schmidt
  */

#undef EVENT_DEBUG

#include "machdep/rpt.h"

extern frame_time_t vsynctimebase, vsyncmintime, vsyncmaxtime;
extern void reset_frame_rate_hack (void);
extern frame_time_t syncbase;
extern unsigned long int vsync_cycles;
extern unsigned long start_cycles;

extern void compute_vsynctime (void);
extern void init_eventtab (void);
extern void do_cycles_ce (unsigned long cycles);
extern void events_schedule (void);
extern void do_cycles_slow (unsigned long cycles_to_add);
extern void do_cycles_fast (unsigned long cycles_to_add);

extern int is_cycle_ce (void);

extern unsigned long currcycle, nextevent;
extern int is_syncline;
typedef void (*evfunc)(void);
typedef void (*evfunc2)(uae_u32);

typedef unsigned long int evt;

struct ev
{
    bool active;
    evt evtime, oldcycles;
    evfunc handler;
};

struct ev2
{
    bool active;
    evt evtime;
    uae_u32 data;
    evfunc2 handler;
};

enum {
    ev_cia, ev_audio, ev_misc, ev_hsync,
    ev_max
};

enum {
    ev2_blitter, ev2_disk, ev2_misc,
    ev2_max = 12
};

extern int pissoff_value;
extern signed long pissoff;

#define countdown pissoff
#define do_cycles do_cycles_slow

extern struct ev eventtab[ev_max];
extern struct ev2 eventtab2[ev2_max];

extern volatile bool vblank_found_chipset;
extern volatile bool vblank_found_rtg;

STATIC_INLINE void cycles_do_special (void)
{
#ifdef JIT
	if (currprefs.cachesize) {
		if (pissoff >= 0)
			pissoff = -1;
	} else
#endif
	{
		pissoff = 0;
	}
}

STATIC_INLINE void do_extra_cycles (unsigned long cycles_to_add)
{
	pissoff -= cycles_to_add;
}

STATIC_INLINE unsigned long int get_cycles (void)
{
	return currcycle;
}

STATIC_INLINE void set_cycles (unsigned long int x)
{
	currcycle = x;
	eventtab[ev_hsync].oldcycles = x;
#ifdef EVT_DEBUG
	if (currcycle & (CYCLE_UNIT - 1))
		write_log (_T("%x\n"), currcycle);
#endif
}

STATIC_INLINE int current_hpos (void)
{
    return (get_cycles () - eventtab[ev_hsync].oldcycles) / CYCLE_UNIT;
}

STATIC_INLINE bool cycles_in_range (unsigned long endcycles)
{
	signed long c = get_cycles ();
	return (signed long)endcycles - c > 0;
}

extern void MISC_handler (void);
extern void event2_newevent_xx (int no, evt t, uae_u32 data, evfunc2 func);

STATIC_INLINE void event2_newevent_x (int no, evt t, uae_u32 data, evfunc2 func)
{
	if (((int)t) <= 0) {
		func (data);
		return;
	}
	event2_newevent_xx (no, t * CYCLE_UNIT, data, func);
}

STATIC_INLINE void event2_newevent (int no, evt t, uae_u32 data)
{
	event2_newevent_x (no, t, data, eventtab2[no].handler);
}
STATIC_INLINE void event2_newevent2 (evt t, uae_u32 data, evfunc2 func)
{
	event2_newevent_x (-1, t, data, func);
}

STATIC_INLINE void event2_remevent (int no)
{
	eventtab2[no].active = 0;
}


#endif
