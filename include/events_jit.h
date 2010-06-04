/* Let's see whether hiding this away somewhere where the compiler can't
see it will cure it of its silly urge to mis-optimize the comparison */
extern long int diff32(frame_time_t x, frame_time_t y);
extern int pissoff_value;

STATIC_INLINE void events_schedule (void)
{
	int i;

	unsigned long int mintime = ~0L;
	for (i = 0; i < ev_max; i++) {
		if (eventtab[i].active) {
			unsigned long int eventtime = eventtab[i].evtime - currcycle;
#ifdef EVENT_DEBUG
			if (eventtime == 0) {
				write_log ("event %d bug\n",i);
			}
#endif
			if (eventtime < mintime)
				mintime = eventtime;
		}
	}
	nextevent = currcycle + mintime;
}

extern signed long pissoff;

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
#ifdef EVT_DEBUG
	if (currcycle & (CYCLE_UNIT - 1))
		write_log (L"%x\n", currcycle);
#endif
}

STATIC_INLINE void do_cycles_slow (unsigned long cycles_to_add)
{
	if ((pissoff -= cycles_to_add) >= 0)
		return;

	cycles_to_add = -pissoff;
	pissoff = 0;

	if (is_lastline && eventtab[ev_hsync].evtime - currcycle <= cycles_to_add) {
		int rpt = read_processor_time ();
		int v = rpt - vsyncmintime;
		if (v > (int)syncbase || v < -((int)syncbase))
			vsyncmintime = rpt;
		if (v < 0) {
			pissoff = pissoff_value * CYCLE_UNIT;
			return;
		}
	}
	while ((nextevent - currcycle) <= cycles_to_add) {
		int i;
		cycles_to_add -= (nextevent - currcycle);
		currcycle = nextevent;

		for (i = 0; i < ev_max; i++) {
			if (eventtab[i].active && eventtab[i].evtime == currcycle) {
				(*eventtab[i].handler)();
			}
		}
		events_schedule ();
	}
	currcycle += cycles_to_add;
#ifdef EVT_DEBUG
	if (currcycle & (CYCLE_UNIT - 1))
		write_log (L"%x\n", currcycle);
#endif
}

#define do_cycles do_cycles_slow
#define countdown pissoff

/* This is a special-case function.  Normally, all events should lie in the
future; they should only ever be active at the current cycle during
do_cycles.  However, a snapshot is saved during do_cycles, and so when
restoring it, we may have other events pending.  */

STATIC_INLINE void handle_active_events (void)
{
	int i;
	for (i = 0; i < ev_max; i++) {
		if (eventtab[i].active && eventtab[i].evtime == currcycle) {
			(*eventtab[i].handler)();
		}
	}
}
