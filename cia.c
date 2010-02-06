/*
* UAE - The Un*x Amiga Emulator
*
* CIA chip support
*
* Copyright 1995 Bernd Schmidt, Alessandro Bissacco
* Copyright 1996, 1997 Stefan Reinauer, Christian Schmitt
*/


#include "sysconfig.h"
#include "sysdeps.h"
#include <assert.h>

#include "options.h"
#include "events.h"
#include "memory.h"
#include "custom.h"
#include "cia.h"
#include "serial.h"
#include "disk.h"
#include "xwin.h"
#include "keybuf.h"
#include "gui.h"
#include "savestate.h"
#include "inputdevice.h"
#include "zfile.h"
#include "ar.h"
#include "parallel.h"
#include "akiko.h"
#include "cdtv.h"
#include "debug.h"
#include "arcadia.h"
#include "audio.h"
#include "keyboard.h"
#include "uae.h"
#include "amax.h"
#include "ersatz.h"
#include "sampler.h"
#include "dongle.h"

#define CIAA_DEBUG_R 0
#define CIAA_DEBUG_W 0
#define CIAB_DEBUG_R 0
#define CIAB_DEBUG_W 0
#define DONGLE_DEBUG 0

#define TOD_HACK

#define DIV10 (10 * CYCLE_UNIT / 2) /* Yes, a bad identifier. */

static unsigned int ciaaicr, ciaaimask, ciabicr, ciabimask;
static unsigned int ciaacra, ciaacrb, ciabcra, ciabcrb;

/* Values of the CIA timers.  */
static unsigned long ciaata, ciaatb, ciabta, ciabtb;
/* Computed by compute_passed_time.  */
static unsigned long ciaata_passed, ciaatb_passed, ciabta_passed, ciabtb_passed;

static unsigned long ciaatod, ciabtod, ciaatol, ciabtol, ciaaalarm, ciabalarm;
static int ciaatlatch, ciabtlatch;
static int oldled, oldovl, oldcd32mute, led_changed;

unsigned int ciabpra;

static unsigned long ciaala, ciaalb, ciabla, ciablb;
static int ciaatodon, ciabtodon;
static unsigned int ciaapra, ciaaprb, ciaadra, ciaadrb, ciaasdr, ciaasdr_cnt;
static unsigned int ciabprb, ciabdra, ciabdrb, ciabsdr, ciabsdr_cnt;
static int div10;
static int kbstate, kback, ciaasdr_unread;
static unsigned int sleepyhead;

static uae_u8 serbits;
static int warned = 10;

static void setclr (unsigned int *p, unsigned int val)
{
	if (val & 0x80) {
		*p |= val & 0x7F;
	} else {
		*p &= ~val;
	}
}

#include "newcpu.h"

static void RethinkICRA (void)
{
	if (ciaaimask & ciaaicr) {
		ciaaicr |= 0x80;
		send_interrupt (3, 0);
	}
}

static void RethinkICRB (void)
{
	if (ciabimask & ciabicr) {
		ciabicr |= 0x80;
		send_interrupt (13, 0);
	}
}

void rethink_cias (void)
{
	RethinkICRA ();
	RethinkICRB ();
}

/* Figure out how many CIA timer cycles have passed for each timer since the
last call of CIA_calctimers.  */

static void compute_passed_time (void)
{
	unsigned long int ccount = (get_cycles () - eventtab[ev_cia].oldcycles + div10);
	unsigned long int ciaclocks = ccount / DIV10;

	ciaata_passed = ciaatb_passed = ciabta_passed = ciabtb_passed = 0;

	/* CIA A timers */
	if ((ciaacra & 0x21) == 0x01) {
		assert ((ciaata + 1) >= ciaclocks);
		ciaata_passed = ciaclocks;
	}
	if ((ciaacrb & 0x61) == 0x01) {
		assert ((ciaatb + 1) >= ciaclocks);
		ciaatb_passed = ciaclocks;
	}

	/* CIA B timers */
	if ((ciabcra & 0x21) == 0x01) {
		assert ((ciabta + 1) >= ciaclocks);
		ciabta_passed = ciaclocks;
	}
	if ((ciabcrb & 0x61) == 0x01) {
		assert ((ciabtb + 1) >= ciaclocks);
		ciabtb_passed = ciaclocks;
	}
}

/* Called to advance all CIA timers to the current time.  This expects that
one of the timer values will be modified, and CIA_calctimers will be called
in the same cycle.  */

static void CIA_update (void)
{
	unsigned long int ccount = (get_cycles () - eventtab[ev_cia].oldcycles + div10);
	unsigned long int ciaclocks = ccount / DIV10;

	int aovfla = 0, aovflb = 0, asp = 0, bovfla = 0, bovflb = 0, bsp = 0;

	div10 = ccount % DIV10;

	/* CIA A timers */
	if ((ciaacra & 0x21) == 0x01) {
		assert ((ciaata + 1) >= ciaclocks);
		if ((ciaata + 1) == ciaclocks) {
			if ((ciaacra & 0x48) == 0x40 && ciaasdr_cnt > 0 && --ciaasdr_cnt == 0)
				asp = 1;
			aovfla = 1;
			if ((ciaacrb & 0x61) == 0x41 || (ciaacrb & 0x61) == 0x61) {
				if (ciaatb-- == 0)
					aovflb = 1;
			}
		}
		ciaata -= ciaclocks;
	}
	if ((ciaacrb & 0x61) == 0x01) {
		assert ((ciaatb + 1) >= ciaclocks);
		if ((ciaatb + 1) == ciaclocks) aovflb = 1;
		ciaatb -= ciaclocks;
	}

	/* CIA B timers */
	if ((ciabcra & 0x21) == 0x01) {
		assert ((ciabta + 1) >= ciaclocks);
		if ((ciabta + 1) == ciaclocks) {
			if ((ciabcra & 0x48) == 0x40 && ciabsdr_cnt > 0 && --ciabsdr_cnt == 0)
				bsp = 1;
			bovfla = 1;
			if ((ciabcrb & 0x61) == 0x41 || (ciabcrb & 0x61) == 0x61) {
				if (ciabtb-- == 0)
					bovflb = 1;
			}
		}
		ciabta -= ciaclocks;
	}
	if ((ciabcrb & 0x61) == 0x01) {
		assert ((ciabtb + 1) >= ciaclocks);
		if ((ciabtb + 1) == ciaclocks) bovflb = 1;
		ciabtb -= ciaclocks;
	}

	if (aovfla) {
		ciaaicr |= 1; RethinkICRA ();
		ciaata = ciaala;
		if (ciaacra & 0x8) ciaacra &= ~1;
	}
	if (aovflb) {
		ciaaicr |= 2; RethinkICRA ();
		ciaatb = ciaalb;
		if (ciaacrb & 0x8) ciaacrb &= ~1;
	}
	if (asp) {
		ciaaicr |= 8; RethinkICRA ();
	}
	if (bovfla) {
		ciabicr |= 1; RethinkICRB ();
		ciabta = ciabla;
		if (ciabcra & 0x8) ciabcra &= ~1;
	}
	if (bovflb) {
		ciabicr |= 2; RethinkICRB ();
		ciabtb = ciablb;
		if (ciabcrb & 0x8) ciabcrb &= ~1;
	}
	if (bsp) {
		ciabicr |= 8; RethinkICRB ();
	}
}

/* Call this only after CIA_update has been called in the same cycle.  */

static void CIA_calctimers (void)
{
	long int ciaatimea = -1, ciaatimeb = -1, ciabtimea = -1, ciabtimeb = -1;

	eventtab[ev_cia].oldcycles = get_cycles ();
	if ((ciaacra & 0x21) == 0x01) {
		ciaatimea = (DIV10 - div10) + DIV10 * ciaata;
	}
	if ((ciaacrb & 0x61) == 0x41) {
		/* Timer B will not get any pulses if Timer A is off. */
		if (ciaatimea >= 0) {
			/* If Timer A is in one-shot mode, and Timer B needs more than
			* one pulse, it will not underflow. */
			if (ciaatb == 0 || (ciaacra & 0x8) == 0) {
				/* Otherwise, we can determine the time of the underflow. */
				/* This may overflow, however.  So just ignore this timer and
				use the fact that we'll call CIA_handler for the A timer.  */
#if 0
				ciaatimeb = ciaatimea + ciaala * DIV10 * ciaatb;
#endif
			}
		}
	}
	if ((ciaacrb & 0x61) == 0x01) {
		ciaatimeb = (DIV10 - div10) + DIV10 * ciaatb;
	}

	if ((ciabcra & 0x21) == 0x01) {
		ciabtimea = (DIV10 - div10) + DIV10 * ciabta;
	}
	if ((ciabcrb & 0x61) == 0x41) {
		/* Timer B will not get any pulses if Timer A is off. */
		if (ciabtimea >= 0) {
			/* If Timer A is in one-shot mode, and Timer B needs more than
			* one pulse, it will not underflow. */
			if (ciabtb == 0 || (ciabcra & 0x8) == 0) {
				/* Otherwise, we can determine the time of the underflow. */
#if 0
				ciabtimeb = ciabtimea + ciabla * DIV10 * ciabtb;
#endif
			}
		}
	}
	if ((ciabcrb & 0x61) == 0x01) {
		ciabtimeb = (DIV10 - div10) + DIV10 * ciabtb;
	}
	eventtab[ev_cia].active = (ciaatimea != -1 || ciaatimeb != -1
		|| ciabtimea != -1 || ciabtimeb != -1);
	if (eventtab[ev_cia].active) {
		unsigned long int ciatime = ~0L;
		if (ciaatimea != -1) ciatime = ciaatimea;
		if (ciaatimeb != -1 && ciaatimeb < ciatime) ciatime = ciaatimeb;
		if (ciabtimea != -1 && ciabtimea < ciatime) ciatime = ciabtimea;
		if (ciabtimeb != -1 && ciabtimeb < ciatime) ciatime = ciabtimeb;
		eventtab[ev_cia].evtime = ciatime + get_cycles ();
	}
	events_schedule();
}

void CIA_handler (void)
{
	CIA_update ();
	CIA_calctimers ();
}

void cia_diskindex (void)
{
	ciabicr |= 0x10;
	RethinkICRB ();
}
void cia_parallelack (void)
{
	ciaaicr |= 0x10;
	RethinkICRA ();
}

static int checkalarm (unsigned long tod, unsigned long alarm, int inc)
{
	if (tod == alarm)
		return 1;
	if (!inc)
		return 0;
	/* emulate buggy TODMED counter.
	* it counts: .. 29 2A 2B 2C 2D 2E 2F 20 30 31 32 ..
	* (2F->20->30 only takes couple of cycles but it will trigger alarm..
	*/
	if (tod & 0x000fff)
		return 0;
	if (((tod - 1) & 0xfff000) == alarm)
		return 1;
	return 0;
}

STATIC_INLINE void ciab_checkalarm (int inc)
{
	if (checkalarm (ciabtod, ciabalarm, inc)) {
		ciabicr |= 4;
		RethinkICRB ();
	}
}

STATIC_INLINE void ciaa_checkalarm (int inc)
{
	if (checkalarm (ciaatod, ciaaalarm, inc)) {
		ciaaicr |= 4;
		RethinkICRA ();
	}
}


#ifdef TOD_HACK
static uae_u64 tod_hack_tv, tod_hack_tod, tod_hack_tod_last;
static int tod_hack_enabled;
static void tod_hack_reset (void)
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	tod_hack_tv = (uae_u64)tv.tv_sec * 1000000 + tv.tv_usec;
	tod_hack_tod = ciaatod;
	tod_hack_tod_last = tod_hack_tod;
}
#endif

static void do_tod_hack (int dotod)
{
	struct timeval tv;
	static int oldrate;
	uae_u64 t;
	int rate;
	int docount = 0;

	if (tod_hack_enabled == 0)
		return;
	if (tod_hack_enabled > 1) {
		tod_hack_enabled--;
		if (tod_hack_enabled == 1) {
			write_log (L"TOD HACK enabled\n");
			tod_hack_reset ();
		}
		return;
	}

	if (currprefs.cs_ciaatod == 0)
		rate = vblank_hz;
	else if (currprefs.cs_ciaatod == 1)
		rate = 50;
	else
		rate = 60;
	if (rate <= 0)
		return;
	if (rate != oldrate || ciaatod != tod_hack_tod_last) {
		if (ciaatod != 0)
			write_log (L"TOD HACK reset %d,%d %d,%d\n",
			rate, oldrate, ciaatod, tod_hack_tod_last);
		tod_hack_reset ();
		oldrate = rate;
		docount = 1;
	}
	if (!dotod && currprefs.cs_ciaatod == 0)
		return;
	gettimeofday (&tv, NULL);
	t = (uae_u64)tv.tv_sec * 1000000 + tv.tv_usec;
	if (t - tod_hack_tv >= 1000000 / rate) {
		tod_hack_tv += 1000000 / rate;
		docount = 1;
	}
	if (docount) {
		ciaatod++;
		ciaatod &= 0x00ffffff;
		tod_hack_tod_last = ciaatod;
		ciaa_checkalarm (0);
	}
}

static int resetwarning_phase, resetwarning_timer;

static void setcode (uae_u8 keycode)
{
	ciaasdr = ~((keycode << 1) | (keycode >> 7));
}

static void sendrw (void)
{
	setcode (AK_RESETWARNING);
	ciaasdr_unread = 1;
	ciaaicr |= 8;
	RethinkICRA ();
	write_log (L"KB: sent reset warning code (phase=%d)\n", resetwarning_phase);
}

int resetwarning_do (int canreset)
{
	if (resetwarning_phase) {
		/* just force reset if second reset happens during resetwarning */
		if (canreset) {
			resetwarning_phase = 0;
			resetwarning_timer = 0;
		}
		return 0;
	}
	resetwarning_phase = 1;
	resetwarning_timer = maxvpos * 5;
	write_log (L"KB: reset warning triggered\n");
	sendrw ();
	return 1;
}

static void resetwarning_check (void)
{
	if (resetwarning_timer > 0) {
		resetwarning_timer--;
		if (resetwarning_timer <= 0) {
			write_log (L"KB: reset warning forced reset. Phase=%d\n", resetwarning_phase);
			resetwarning_phase = -1;
			uae_reset (0);
		}
	}
	if (resetwarning_phase == 1) {
		if (kback && !(ciaacra & 0x40) && ciaasdr_unread == 2) {
			write_log (L"KB: reset warning second phase..\n");
			resetwarning_phase = 2;
			resetwarning_timer = maxvpos * 5;
			sendrw ();
		}
	} else if (resetwarning_phase == 2) {
		if (ciaacra & 0x40) {
			resetwarning_phase = 3;
			write_log (L"KB: reset warning SP = output\n");
			resetwarning_timer = 10 * maxvpos * vblank_hz; /* wait max 10s */
		}
	} else if (resetwarning_phase == 3) {
		if (!(ciaacra & 0x40)) {
			write_log (L"KB: reset warning end by software. reset.\n");
			resetwarning_phase = -1;
			uae_reset (0);
		}
	}
}

void CIA_hsync_handler (int dotod)
{
	if (ciabtodon && dotod) {
		ciabtod++;
		ciabtod &= 0xFFFFFF;
		ciab_checkalarm (1);
	}

	if (currprefs.tod_hack && ciaatodon)
		do_tod_hack (dotod);

	if (resetwarning_phase) {
		resetwarning_check ();
		while (keys_available ())
			get_next_key ();
	} else if ((keys_available() || kbstate < 3) && kback && (ciaacra & 0x40) == 0 && (hsync_counter & 15) == 0) {
		/*
		* This hack lets one possible ciaaicr cycle go by without any key
		* being read, for every cycle in which a key is pulled out of the
		* queue.  If no hack is used, a lot of key events just get lost
		* when you type fast.  With a simple hack that waits for ciaasdr
		* to be read before feeding it another, it will keep up until the
		* queue gets about 14 characters ahead and then lose events, and
		* the mouse pointer will freeze while typing is being taken in.
		* With this hack, you can type 30 or 40 characters ahead with little
		* or no lossage, and the mouse doesn't get stuck.  The tradeoff is
		* that the total slowness of typing appearing on screen is worse.
		*/
		if (ciaasdr_unread == 2) {
			ciaasdr_unread = 0;
		} else if (ciaasdr_unread == 0) {
			switch (kbstate) {
			case 0:
				ciaasdr = 0; /* powerup resync */
				kbstate++;
				ciaasdr_unread = 3;
				break;
			case 1:
				setcode (AK_INIT_POWERUP);
				kbstate++;
				ciaasdr_unread = 3;
				break;
			case 2:
				setcode (AK_TERM_POWERUP);
				kbstate++;
				ciaasdr_unread = 3;
				break;
			case 3:
				ciaasdr = ~get_next_key ();
				ciaasdr_unread = 1;      /* interlock to prevent lost keystrokes */
				break;
			}
			ciaaicr |= 8;
			RethinkICRA ();
			sleepyhead = 0;
		} else if (!(++sleepyhead & 15)) {
			if (ciaasdr_unread == 3)
				ciaaicr |= 8;
			if (ciaasdr_unread < 3)
				ciaasdr_unread = 0;	/* give up on this key event after unread for a long time */
		}
	}
}

static int led_times;
static unsigned long led_on, led_cycle;


static void calc_led (int old_led)
{
	unsigned long c = get_cycles ();
	unsigned long t = c - led_cycle;
	if (old_led)
		led_on += t;
	led_times++;
	led_cycle = c;
}

static void led_vsync (void)
{
	gui_data.powerled_brightness = gui_data.powerled ? 255 : 0;
	calc_led (gui_data.powerled);
	if (led_on > 0 && led_times > 2) {
		int v = led_on / CYCLE_UNIT * 256 / (maxhpos * maxvpos);
		if (v < 0)
			v = 0;
		if (v > 255)
			v = 255;
		gui_data.powerled_brightness = v;
	}
	led_on = 0;
	led_times = 0;
	if (led_changed)
		gui_led (LED_POWER, gui_data.powerled_brightness);
	led_changed = 0;
	led_cycle = get_cycles ();
}

void CIA_vsync_handler (int dotod)
{
	led_vsync ();
#ifdef TOD_HACK
	if (currprefs.tod_hack && tod_hack_enabled == 1)
		return;
#endif
	if (ciaatodon && dotod) {
		ciaatod++;
		ciaatod &= 0xFFFFFF;
		ciaa_checkalarm (1);
	}
}

static void bfe001_change (void)
{
	uae_u8 v = ciaapra;
	int led;

	v |= ~ciaadra; /* output is high when pin's direction is input */
	led = (v & 2) ? 0 : 1;
	if (led != oldled) {
		calc_led (oldled);
		oldled = led;
		gui_data.powerled = led;
		led_changed = 1;
		led_filter_audio ();
	}
	if (currprefs.cs_ciaoverlay && (v & 1) != oldovl) {
		oldovl = v & 1;
		if (!oldovl || ersatzkickfile) {
			map_overlay (1);
		} else {
			//activate_debugger ();
			map_overlay (0);
		}
	} else if (currprefs.cs_cd32cd && (v & 1) != oldcd32mute) {
		oldcd32mute = v & 1;
		akiko_mute (oldcd32mute ? 0 : 1);
	}
}

static uae_u8 ReadCIAA (unsigned int addr)
{
	unsigned int tmp;
	int reg = addr & 15;

	compute_passed_time ();

#if CIAA_DEBUG_R > 0
	write_log (L"R_CIAA: bfe%x01 %08X\n", reg, M68K_GETPC);
#endif

	switch (reg) {
	case 0:
#ifdef ACTION_REPLAY
		action_replay_ciaread ();
#endif
		tmp = DISK_status() & 0x3c;
		tmp |= handle_joystick_buttons (ciaadra);
		tmp |= (ciaapra | (ciaadra ^ 3)) & 0x03;
		if (ciaadra & 0x40)
			tmp = (tmp & ~0x40) | (ciaapra & 0x40);
		if (ciaadra & 0x80)
			tmp = (tmp & ~0x80) | (ciaapra & 0x80);
		tmp = dongle_cia_read (0, reg, tmp);
#if DONGLE_DEBUG > 0
		if (notinrom())
			write_log (L"BFE001 R %02X %s\n", tmp, debuginfo(0));
#endif
		return tmp;
	case 1:
#ifdef PARALLEL_PORT
		if (isprinter () > 0) {
			tmp = ciaaprb;
		} else if (isprinter () < 0) {
			uae_u8 v;
			parallel_direct_read_data (&v);
			tmp = v;
#ifdef ARCADIA
		} else if (arcadia_bios) {
			tmp = arcadia_parport (0, ciaaprb, ciaadrb);
#endif
		} else if (currprefs.parallel_sampler) {

			tmp = sampler_getsample ();

		} else
#endif

		{
			tmp = handle_parport_joystick (0, ciaaprb, ciaadrb);
			tmp = dongle_cia_read (1, reg, tmp);
#if DONGLE_DEBUG > 0
			if (notinrom())
				write_log (L"BFE101 R %02X %s\n", tmp, debuginfo(0));
#endif
		}
		if (ciaacrb & 2) {
			int pb7 = 0;
			if (ciaacrb & 4)
				pb7 = ciaacrb & 1;
			tmp &= ~0x80;
			tmp |= pb7 ? 0x80 : 00;
		}
		if (ciaacra & 2) {
			int pb6 = 0;
			if (ciaacra & 4)
				pb6 = ciaacra & 1;
			tmp &= ~0x40;
			tmp |= pb6 ? 0x40 : 00;
		}
		return tmp;
	case 2:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFE201 R %02X %s\n", ciaadra, debuginfo(0));
#endif
		return ciaadra;
	case 3:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFE301 R %02X %s\n", ciaadrb, debuginfo(0));
#endif
		return ciaadrb;
	case 4:
		return (uae_u8)((ciaata - ciaata_passed) & 0xff);
	case 5:
		return (uae_u8)((ciaata - ciaata_passed) >> 8);
	case 6:
		return (uae_u8)((ciaatb - ciaatb_passed) & 0xff);
	case 7:
		return (uae_u8)((ciaatb - ciaatb_passed) >> 8);
	case 8:
		if (ciaatlatch) {
			ciaatlatch = 0;
			return (uae_u8)ciaatol;
		} else
			return (uae_u8)ciaatod;
	case 9:
		if (ciaatlatch)
			return (uae_u8)(ciaatol >> 8);
		else
			return (uae_u8)(ciaatod >> 8);
	case 10:
		if (!ciaatlatch) { /* only if not already latched. A1200 confirmed. (TW) */
			/* no latching if ALARM is set */
			if (!(ciaacrb & 0x80))
				ciaatlatch = 1;
			ciaatol = ciaatod;
		}
		return (uae_u8)(ciaatol >> 16);
	case 12:
		if (ciaasdr_unread >= 1)
			ciaasdr_unread = 2;
		return ciaasdr;
	case 13:
		tmp = ciaaicr; ciaaicr = 0; RethinkICRA ();
		return tmp;
	case 14:
		return ciaacra;
	case 15:
		return ciaacrb;
	}
	return 0;
}

static uae_u8 ReadCIAB (unsigned int addr)
{
	unsigned int tmp;
	int reg = addr & 15;

#if CIAB_DEBUG_R > 0
	if ((addr >= 8 && addr <= 10) || CIAB_DEBUG_R > 1)
		write_log (L"R_CIAB: bfd%x00 %08X\n", reg, M68K_GETPC);
#endif

	compute_passed_time ();

	switch (reg) {
	case 0:
		tmp = 0;
#ifdef SERIAL_PORT
		if (currprefs.use_serial)
			tmp = serial_readstatus(ciabdra);
#endif
#ifdef PARALLEL_PORT
		if (isprinter () > 0) {
			tmp |= ciabpra & (0x04 | 0x02 | 0x01);
		} else if (isprinter () < 0) {
			uae_u8 v;
			parallel_direct_read_status (&v);
			tmp |= v & 7;
		} else {
			tmp |= handle_parport_joystick (1, ciabpra, ciabdra);
		}
#endif
		tmp = dongle_cia_read (1, reg, tmp);
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFD000 R %02X %s\n", tmp, debuginfo(0));
#endif
		return tmp;
	case 1:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFD100 R %02X %s\n", ciabprb, debuginfo(0));
#endif
		tmp = ciabprb;
		tmp = dongle_cia_read (1, reg, tmp);
		if (ciabcrb & 2) {
			int pb7 = 0;
			if (ciabcrb & 4)
				pb7 = ciabcrb & 1;
			tmp &= ~0x80;
			tmp |= pb7 ? 0x80 : 00;
		}
		if (ciabcra & 2) {
			int pb6 = 0;
			if (ciabcra & 4)
				pb6 = ciabcra & 1;
			tmp &= ~0x40;
			tmp |= pb6 ? 0x40 : 00;
		}
		return tmp;
	case 2:
		return ciabdra;
	case 3:
		return ciabdrb;
	case 4:
		return (uae_u8)((ciabta - ciabta_passed) & 0xff);
	case 5:
		return (uae_u8)((ciabta - ciabta_passed) >> 8);
	case 6:
		return (uae_u8)((ciabtb - ciabtb_passed) & 0xff);
	case 7:
		return (uae_u8)((ciabtb - ciabtb_passed) >> 8);
	case 8:
		if (ciabtlatch) {
			ciabtlatch = 0;
			return (uae_u8)ciabtol;
		} else
			return (uae_u8)ciabtod;
	case 9:
		if (ciabtlatch)
			return (uae_u8)(ciabtol >> 8);
		else
			return (uae_u8)(ciabtod >> 8);
	case 10:
		if (!ciabtlatch) {
			/* no latching if ALARM is set */
			if (!(ciabcrb & 0x80))
				ciabtlatch = 1;
			ciabtol = ciabtod;
		}
		return (uae_u8)(ciabtol >> 16);
	case 12:
		return ciabsdr;
	case 13:
		tmp = ciabicr; ciabicr = 0; RethinkICRB ();
		return tmp;
	case 14:
		return ciabcra;
	case 15:
		return ciabcrb;
	}
	return 0;
}

static void WriteCIAA (uae_u16 addr, uae_u8 val)
{
	int reg = addr & 15;

#if CIAA_DEBUG_W > 0
	write_log (L"W_CIAA: bfe%x01 %02X %08X\n", reg, val, M68K_GETPC);
#endif
#ifdef ACTION_REPLAY
	ar_ciaa[reg] = val;
#endif
	if (!currprefs.cs_ciaoverlay && oldovl) {
		map_overlay (1);
		oldovl = 0;
	}
	switch (reg) {
	case 0:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFE001 W %02X %s\n", val, debuginfo(0));
#endif
		ciaapra = (ciaapra & ~0xc3) | (val & 0xc3);
		bfe001_change ();
		handle_cd32_joystick_cia (ciaapra, ciaadra);
		dongle_cia_write (0, reg, val);
#ifdef AMAX
		if (currprefs.amaxromfile[0])
			amax_bfe001_write (val, ciaadra);
#endif
		break;
	case 1:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFE101 W %02X %s\n", val, debuginfo(0));
#endif
		ciaaprb = val;
		dongle_cia_write (0, reg, val);
#ifdef PARALLEL_PORT
		if (isprinter() > 0) {
			doprinter (val);
			cia_parallelack ();
		} else if (isprinter() < 0) {
			parallel_direct_write_data (val, ciaadrb);
			cia_parallelack ();
#ifdef ARCADIA
		} else if (arcadia_bios) {
			arcadia_parport (1, ciaaprb, ciaadrb);
#endif
		}
#endif
		break;
	case 2:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFE201 W %02X %s\n", val, debuginfo(0));
#endif
		ciaadra = val;
		dongle_cia_write (0, reg, val);
		bfe001_change ();
		break;
	case 3:
		ciaadrb = val;
		dongle_cia_write (0, reg, val);
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFE301 W %02X %s\n", val, debuginfo(0));
#endif
#ifdef ARCADIA
		if (arcadia_bios)
			arcadia_parport (1, ciaaprb, ciaadrb);
#endif
		break;
	case 4:
		CIA_update ();
		ciaala = (ciaala & 0xff00) | val;
		CIA_calctimers ();
		break;
	case 5:
		CIA_update ();
		ciaala = (ciaala & 0xff) | (val << 8);
		if ((ciaacra & 1) == 0)
			ciaata = ciaala;
		if (ciaacra & 8) {
			ciaata = ciaala;
			ciaacra |= 1;
		}
		CIA_calctimers ();
		break;
	case 6:
		CIA_update ();
		ciaalb = (ciaalb & 0xff00) | val;
		CIA_calctimers ();
		break;
	case 7:
		CIA_update ();
		ciaalb = (ciaalb & 0xff) | (val << 8);
		if ((ciaacrb & 1) == 0)
			ciaatb = ciaalb;
		if (ciaacrb & 8) {
			ciaatb = ciaalb;
			ciaacrb |= 1;
		}
		CIA_calctimers ();
		break;
	case 8:
		if (ciaacrb & 0x80) {
			ciaaalarm = (ciaaalarm & ~0xff) | val;
		} else {
			ciaatod = (ciaatod & ~0xff) | val;
			ciaatodon = 1;
			ciaa_checkalarm (0);
		}
		break;
	case 9:
		if (ciaacrb & 0x80) {
			ciaaalarm = (ciaaalarm & ~0xff00) | (val << 8);
		} else {
			ciaatod = (ciaatod & ~0xff00) | (val << 8);
		}
		break;
	case 10:
		if (ciaacrb & 0x80) {
			ciaaalarm = (ciaaalarm & ~0xff0000) | (val << 16);
		} else {
			ciaatod = (ciaatod & ~0xff0000) | (val << 16);
			ciaatodon = 0;
		}
		break;
	case 12:
		CIA_update ();
		ciaasdr = val;
		if (ciaacra & 0x40) {
			kback = 1;
		} else {
			ciaasdr_cnt = 0;
		}
		if ((ciaacra & 0x41) == 0x41 && ciaasdr_cnt == 0)
			ciaasdr_cnt = 8 * 2;
		CIA_calctimers ();
		break;
	case 13:
		setclr (&ciaaimask,val);
		break;
	case 14:
		CIA_update ();
		val &= 0x7f; /* bit 7 is unused */
		if (!(ciaacra & 0x40) && (val & 0x40))
			kback = 1;
		ciaacra = val;
		if (ciaacra & 0x10) {
			ciaacra &= ~0x10;
			ciaata = ciaala;
		}
		CIA_calctimers ();
		break;
	case 15:
		CIA_update ();
		ciaacrb = val;
		if (ciaacrb & 0x10) {
			ciaacrb &= ~0x10;
			ciaatb = ciaalb;
		}
		CIA_calctimers ();
		break;
	}
}

static void WriteCIAB (uae_u16 addr, uae_u8 val)
{
	int reg = addr & 15;

#if CIAB_DEBUG_W > 0
	if ((addr >= 8 && addr <= 10) || CIAB_DEBUG_W > 1)
		write_log (L"W_CIAB: bfd%x00 %02X %08X\n", reg, val, M68K_GETPC);
#endif
#ifdef ACTION_REPLAY
	ar_ciab[reg] = val;
#endif
	switch (reg) {
	case 0:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFD000 W %02X %s\n", val, debuginfo(0));
#endif
		dongle_cia_write (1, reg, val);
		ciabpra = val;
#ifdef SERIAL_PORT
		if (currprefs.use_serial)
			serial_writestatus(ciabpra, ciabdra);
#endif
#ifdef PARALLEL_PORT
		if (isprinter () < 0)
			parallel_direct_write_status (val, ciabdra);
#endif
		break;
	case 1:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFD100 W %02X %s\n", val, debuginfo(0));
#endif
		dongle_cia_write (1, reg, val);
		ciabprb = val;
		DISK_select (val);
		break;
	case 2:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFD200 W %02X %s\n", val, debuginfo(0));
#endif
		dongle_cia_write (1, reg, val);
		ciabdra = val;
#ifdef SERIAL_PORT
		if (currprefs.use_serial)
			serial_writestatus (ciabpra, ciabdra);
#endif
		break;
	case 3:
#if DONGLE_DEBUG > 0
		if (notinrom ())
			write_log (L"BFD300 W %02X %s\n", val, debuginfo(0));
#endif
		dongle_cia_write (1, reg, val);
		ciabdrb = val;
		break;
	case 4:
		CIA_update ();
		ciabla = (ciabla & 0xff00) | val;
		CIA_calctimers ();
		break;
	case 5:
		CIA_update ();
		ciabla = (ciabla & 0xff) | (val << 8);
		if ((ciabcra & 1) == 0)
			ciabta = ciabla;
		if (ciabcra & 8) {
			ciabta = ciabla;
			ciabcra |= 1;
		}
		CIA_calctimers ();
		break;
	case 6:
		CIA_update ();
		ciablb = (ciablb & 0xff00) | val;
		CIA_calctimers ();
		break;
	case 7:
		CIA_update ();
		ciablb = (ciablb & 0xff) | (val << 8);
		if ((ciabcrb & 1) == 0)
			ciabtb = ciablb;
		if (ciabcrb & 8) {
			ciabtb = ciablb;
			ciabcrb |= 1;
		}
		CIA_calctimers ();
		break;
	case 8:
		if (ciabcrb & 0x80) {
			ciabalarm = (ciabalarm & ~0xff) | val;
		} else {
			ciabtod = (ciabtod & ~0xff) | val;
			ciabtodon = 1;
			ciab_checkalarm (0);
		}
		break;
	case 9:
		if (ciabcrb & 0x80) {
			ciabalarm = (ciabalarm & ~0xff00) | (val << 8);
		} else {
			ciabtod = (ciabtod & ~0xff00) | (val << 8);
		}
		break;
	case 10:
		if (ciabcrb & 0x80) {
			ciabalarm = (ciabalarm & ~0xff0000) | (val << 16);
		} else {
			ciabtod = (ciabtod & ~0xff0000) | (val << 16);
			ciabtodon = 0;
		}
		break;
	case 12:
		CIA_update ();
		ciabsdr = val;
		if ((ciabcra & 0x40) == 0)
			ciabsdr_cnt = 0;
		if ((ciabcra & 0x41) == 0x41 && ciabsdr_cnt == 0)
			ciabsdr_cnt = 8 * 2;
		CIA_calctimers ();
		break;
	case 13:
		setclr (&ciabimask, val);
		break;
	case 14:
		CIA_update ();
		val &= 0x7f; /* bit 7 is unused */
		ciabcra = val;
		if (ciabcra & 0x10) {
			ciabcra &= ~0x10;
			ciabta = ciabla;
		}
		CIA_calctimers ();
		break;
	case 15:
		CIA_update ();
		ciabcrb = val;
		if (ciabcrb & 0x10) {
			ciabcrb &= ~0x10;
			ciabtb = ciablb;
		}
		CIA_calctimers ();
		break;
	}
}

void CIA_inprec_prepare (void)
{
	sleepyhead = 0;
}

void CIA_reset (void)
{
#ifdef TOD_HACK
	tod_hack_tv = 0;
	tod_hack_tod = 0;
	tod_hack_enabled = 0;
	if (currprefs.tod_hack)
		tod_hack_enabled = 312 * 50 * 10;
#endif
	kback = 1;
	kbstate = 3;
	ciaasdr_unread = 0;
	serbits = 0;
	oldovl = 1;
	oldcd32mute = 1;
	oldled = -1;
	resetwarning_phase = resetwarning_timer = 0;

	if (!savestate_state) {
		kbstate = 0;
		ciaatlatch = ciabtlatch = 0;
		ciaapra = 0; ciaadra = 0;
		ciaatod = ciabtod = 0; ciaatodon = ciabtodon = 0;
		ciaaicr = ciabicr = ciaaimask = ciabimask = 0;
		ciaacra = ciaacrb = ciabcra = ciabcrb = 0x4; /* outmode = toggle; */
		ciaala = ciaalb = ciabla = ciablb = ciaata = ciaatb = ciabta = ciabtb = 0xFFFF;
		ciaaalarm = ciabalarm = 0;
		ciabpra = 0x8C; ciabdra = 0;
		div10 = 0;
		ciaasdr_cnt = 0; ciaasdr = 0;
		ciabsdr_cnt = 0; ciabsdr = 0;
		ciaata_passed = ciaatb_passed = ciabta_passed = ciabtb_passed = 0;
	}
	CIA_calctimers ();
	if (ersatzkickfile)
		ersatz_chipcopy ();
	else
		map_overlay (0);
#ifdef SERIAL_PORT
	if (currprefs.use_serial && !savestate_state)
		serial_dtr_off (); /* Drop DTR at reset */
#endif
	if (savestate_state) {
		bfe001_change ();
		/* select drives */
		DISK_select (ciabprb);
	}
#ifdef CD32
	if (savestate_state != STATE_RESTORE) {
		akiko_reset ();
		if (!akiko_init ())
			currprefs.cs_cd32cd = changed_prefs.cs_cd32cd = 0;
	}
#endif
}

void dumpcia (void)
{
	console_out_f (L"A: CRA %02x CRB %02x ICR %02x IM %02x TA %04x (%04x) TB %04x (%04x)\n",
		ciaacra, ciaacrb, ciaaicr, ciaaimask, ciaata, ciaala, ciaatb, ciaalb);
	console_out_f (L"TOD %06x (%06x) ALARM %06x %c%c\n",
		ciaatod, ciaatol, ciaaalarm, ciaatlatch ? 'L' : ' ', ciaatodon ? ' ' : 'S');
	console_out_f (L"B: CRA %02x CRB %02x ICR %02x IM %02x TA %04x (%04x) TB %04x (%04x)\n",
		ciabcra, ciabcrb, ciaaicr, ciabimask, ciabta, ciabla, ciabtb, ciablb);
	console_out_f (L"TOD %06x (%06x) ALARM %06x %c%c\n",
		ciabtod, ciabtol, ciabalarm, ciabtlatch ? 'L' : ' ', ciabtodon ? ' ' : 'S');
}

/* CIA memory access */

static uae_u32 REGPARAM3 cia_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 cia_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 cia_bget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 cia_lgeti (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 cia_wgeti (uaecptr) REGPARAM;
static void REGPARAM3 cia_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 cia_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 cia_bput (uaecptr, uae_u32) REGPARAM;

addrbank cia_bank = {
	cia_lget, cia_wget, cia_bget,
	cia_lput, cia_wput, cia_bput,
	default_xlate, default_check, NULL, L"CIA",
	cia_lgeti, cia_wgeti, ABFLAG_IO
};


STATIC_INLINE isgayle (void)
{
	return (currprefs.cs_ide == IDE_A600A1200 || currprefs.cs_pcmcia);
}

/* e-clock is 10 CPU cycles, 4 cycles high, 6 low
* data transfer happens during 4 high cycles
*/
#define ECLOCK_DATA_CYCLE 4
#define ECLOCK_WAIT_CYCLE 6

static void cia_wait_pre (void)
{
#ifndef CUSTOM_SIMPLE
	int div10 = (get_cycles () - eventtab[ev_cia].oldcycles) % DIV10;
	int cycles;

	if (div10 > DIV10 * ECLOCK_DATA_CYCLE / 10) {
		cycles = DIV10 - div10;
		cycles += DIV10 * ECLOCK_DATA_CYCLE / 10;
	} else {
		cycles = DIV10 * ECLOCK_DATA_CYCLE / 10 - div10;
	}
	do_cycles (cycles);
#endif
}

static void cia_wait_post (void)
{
	do_cycles (6 * CYCLE_UNIT / 2);
}

static uae_u32 REGPARAM2 cia_bget (uaecptr addr)
{
	int r = (addr & 0xf00) >> 8;
	uae_u8 v;

#ifdef JIT
	special_mem |= S_READ;
#endif
	cia_wait_pre ();
	v = 0xff;
	switch ((addr >> 12) & 3) {
	case 0:
		if (!isgayle ())
			v = (addr & 1) ? ReadCIAA (r) : ReadCIAB (r);
		break;
	case 1:
		v = (addr & 1) ? 0xff : ReadCIAB (r);
		break;
	case 2:
		v = (addr & 1) ? ReadCIAA (r) : 0xff;
		break;
	case 3:
		if (currprefs.cpu_model == 68000 && currprefs.cpu_compatible)
			v = (addr & 1) ? regs.irc : regs.irc >> 8;
		if (warned > 0) {
			write_log (L"cia_bget: unknown CIA address %x PC=%x\n", addr, M68K_GETPC);
			warned--;
		}
		break;
	}
	cia_wait_post ();
	return v;
}

static uae_u32 REGPARAM2 cia_wget (uaecptr addr)
{
	int r = (addr & 0xf00) >> 8;
	uae_u16 v;

#ifdef JIT
	special_mem |= S_READ;
#endif
	cia_wait_pre ();
	v = 0xffff;
	switch ((addr >> 12) & 3)
	{
	case 0:
		if (!isgayle ())
			v = (ReadCIAB (r) << 8) | ReadCIAA (r);
		break;
	case 1:
		v = (ReadCIAB (r) << 8) | 0xff;
		break;
	case 2:
		v = (0xff << 8) | ReadCIAA (r);
		break;
	case 3:
		if (currprefs.cpu_model == 68000 && currprefs.cpu_compatible)
			v = regs.irc;
		if (warned > 0) {
			write_log (L"cia_wget: unknown CIA address %x PC=%x\n", addr, M68K_GETPC);
			warned--;
		}
		break;
	}
	cia_wait_post ();
	return v;
}

static uae_u32 REGPARAM2 cia_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = cia_wget (addr) << 16;
	v |= cia_wget (addr + 2);
	return v;
}

static uae_u32 REGPARAM2 cia_wgeti (uaecptr addr)
{
	if (currprefs.cpu_model >= 68020)
		return dummy_wgeti(addr);
	return cia_wget(addr);
}
static uae_u32 REGPARAM2 cia_lgeti (uaecptr addr)
{
	if (currprefs.cpu_model >= 68020)
		return dummy_lgeti(addr);
	return cia_lget(addr);
}

static void REGPARAM2 cia_bput (uaecptr addr, uae_u32 value)
{
	int r = (addr & 0xf00) >> 8;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	cia_wait_pre ();
	if (!isgayle () || (addr & 0x3000) != 0) {
		if ((addr & 0x2000) == 0)
			WriteCIAB (r, value);
		if ((addr & 0x1000) == 0)
			WriteCIAA (r, value);
		if (((addr & 0x3000) == 0x3000) && warned > 0) {
			write_log (L"cia_bput: unknown CIA address %x %x\n", addr, value);
			warned--;
		}
	}
	cia_wait_post ();
}

static void REGPARAM2 cia_wput (uaecptr addr, uae_u32 value)
{
	int r = (addr & 0xf00) >> 8;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	cia_wait_pre ();
	if (!isgayle () || (addr & 0x3000) != 0) {
		if ((addr & 0x2000) == 0)
			WriteCIAB (r, value >> 8);
		if ((addr & 0x1000) == 0)
			WriteCIAA (r, value & 0xff);
		if (((addr & 0x3000) == 0x3000) && warned > 0) {
			write_log (L"cia_wput: unknown CIA address %x %x\n", addr, value);
			warned--;
		}
	}
	cia_wait_post ();
}

static void REGPARAM2 cia_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	cia_wput (addr, value >> 16);
	cia_wput (addr + 2, value & 0xffff);
}

/* battclock memory access */

static uae_u32 REGPARAM3 clock_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 clock_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 clock_bget (uaecptr) REGPARAM;
static void REGPARAM3 clock_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 clock_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 clock_bput (uaecptr, uae_u32) REGPARAM;

addrbank clock_bank = {
	clock_lget, clock_wget, clock_bget,
	clock_lput, clock_wput, clock_bput,
	default_xlate, default_check, NULL, L"Battery backed up clock (none)",
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static unsigned int clock_control_d;
static unsigned int clock_control_e;
static unsigned int clock_control_f;

#define RF5C01A_RAM_SIZE 16
static uae_u8 rtc_memory[RF5C01A_RAM_SIZE], rtc_alarm[RF5C01A_RAM_SIZE];

static void write_battclock (void)
{
	struct zfile *f = zfile_fopen (currprefs.flashfile, L"rb+", ZFD_NORMAL);
	if (!f) {
		f = zfile_fopen (currprefs.flashfile, L"wb", 0);
		if (f) {
			zfile_fwrite (rtc_memory, RF5C01A_RAM_SIZE, 1, f);
			zfile_fwrite (rtc_alarm, RF5C01A_RAM_SIZE, 1, f);
			zfile_fclose (f);
		}
		return;
	}
	zfile_fseek (f, 0, SEEK_END);
	if (zfile_ftell (f) <= 2 * RF5C01A_RAM_SIZE) {
		zfile_fseek (f, 0, SEEK_SET);
		zfile_fwrite (rtc_memory, RF5C01A_RAM_SIZE, 1, f);
		zfile_fwrite (rtc_alarm, RF5C01A_RAM_SIZE, 1, f);
	}
	zfile_fclose (f);
}

void rtc_hardreset (void)
{
	if (currprefs.cs_rtc == 1) { /* MSM6242B */
		clock_bank.name = L"Battery backed up clock (MSM6242B)";
		clock_control_d = 0x1;
		clock_control_e = 0;
		clock_control_f = 0x4; /* 24/12 */
	} else if (currprefs.cs_rtc == 2) { /* RF5C01A */
		clock_bank.name = L"Battery backed up clock (RF5C01A)";
		clock_control_d = 0x4; /* Timer EN */
		clock_control_e = 0;
		clock_control_f = 0;
		memset (rtc_memory, 0, RF5C01A_RAM_SIZE);
		memset (rtc_alarm, 0, RF5C01A_RAM_SIZE);
#if 0
		struct zfile *f;
		f = zfile_fopen (currprefs.flashfile, "rb", ZFD_NORMAL);
		if (f) {
			zfile_fread (rtc_memory, RF5C01A_RAM_SIZE, 1, f);
			zfile_fread (rtc_alarm, RF5C01A_RAM_SIZE, 1, f);
			zfile_fclose (f);
		}
#endif
	}
}

static uae_u32 REGPARAM2 clock_lget (uaecptr addr)
{
	return (clock_wget (addr) << 16) | clock_wget (addr + 2);
}

static uae_u32 REGPARAM2 clock_wget (uaecptr addr)
{
	return (clock_bget (addr) << 8) | clock_bget (addr + 1);
}

static uae_u32 REGPARAM2 clock_bget (uaecptr addr)
{
	time_t t;
	struct tm *ct;

#ifdef JIT
	special_mem |= S_READ;
#endif
	//write_log("R: %x (%x), PC=%08x\n", addr, addr >> 2, M68K_GETPC);
#ifdef CDTV
	if (currprefs.cs_cdtvram && addr >= 0xdc8000)
		return cdtv_battram_read (addr);
#endif
	addr &= 0x3f;
	if ((addr & 3) == 2 || (addr & 3) == 0 || currprefs.cs_rtc == 0) {
		int v = 0;
		if (currprefs.cpu_model == 68000 && currprefs.cpu_compatible)
			v = regs.irc >> 8;
		return v;
	}
	t = time (0);
	t += currprefs.cs_rtc_adjust;
	ct = localtime (&t);
	addr >>= 2;
	if (currprefs.cs_rtc == 1) { /* MSM6242B */
		switch (addr) {
		case 0x0: return ct->tm_sec % 10;
		case 0x1: return ct->tm_sec / 10;
		case 0x2: return ct->tm_min % 10;
		case 0x3: return ct->tm_min / 10;
		case 0x4: return ct->tm_hour % 10;
		case 0x5: return ct->tm_hour / 10;
		case 0x6: return ct->tm_mday % 10;
		case 0x7: return ct->tm_mday / 10;
		case 0x8: return (ct->tm_mon + 1) % 10;
		case 0x9: return (ct->tm_mon + 1) / 10;
		case 0xA: return ct->tm_year % 10;
		case 0xB: return ct->tm_year / 10;
		case 0xC: return ct->tm_wday;
		case 0xD: return clock_control_d;
		case 0xE: return clock_control_e;
		case 0xF: return clock_control_f;
		}
	} else if (currprefs.cs_rtc == 2) { /* RF5C01A */
		int bank = clock_control_d & 3;
		/* memory access */
		if (bank >= 2 && addr < 0x0d)
			return (rtc_memory[addr] >> ((bank == 2) ? 0 : 4)) & 0x0f;
		/* alarm */
		if (bank == 1 && addr < 0x0d)
			return rtc_alarm[addr];
		switch (addr) {
		case 0x0: return ct->tm_sec % 10;
		case 0x1: return ct->tm_sec / 10;
		case 0x2: return ct->tm_min % 10;
		case 0x3: return ct->tm_min / 10;
		case 0x4: return ct->tm_hour % 10;
		case 0x5: return ct->tm_hour / 10;
		case 0x6: return ct->tm_wday;
		case 0x7: return ct->tm_mday % 10;
		case 0x8: return ct->tm_mday / 10;
		case 0x9: return (ct->tm_mon+1) % 10;
		case 0xA: return (ct->tm_mon+1) / 10;
		case 0xB: return ct->tm_year % 10;
		case 0xC: return ct->tm_year / 10;
		case 0xD: return clock_control_d;
			/* E and F = write-only */
		}
	}
	return 0;
}

static void REGPARAM2 clock_lput (uaecptr addr, uae_u32 value)
{
	clock_wput (addr, value >> 16);
	clock_wput (addr + 2, value);
}

static void REGPARAM2 clock_wput (uaecptr addr, uae_u32 value)
{
	clock_bput (addr, value >> 8);
	clock_bput (addr + 1, value);
}

static void REGPARAM2 clock_bput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	//write_log("W: %x: %x, PC=%08x\n", addr, value & 0xff, M68K_GETPC);
#ifdef CDTV
	if (currprefs.cs_cdtvram && addr >= 0xdc8000) {
		cdtv_battram_write (addr, value);
		return;
	}
#endif
	addr &= 0x3f;
	if ((addr & 1) != 1 || currprefs.cs_rtc == 0)
		return;
	addr >>= 2;
	value &= 0x0f;
	if (currprefs.cs_rtc == 1) { /* MSM6242B */
		switch (addr)
		{
		case 0xD: clock_control_d = value & (1|8); break;
		case 0xE: clock_control_e = value; break;
		case 0xF: clock_control_f = value; break;
		}
	} else if (currprefs.cs_rtc == 2) { /* RF5C01A */
		int bank = clock_control_d & 3;
		/* memory access */
		if (bank >= 2 && addr < 0x0d) {
			rtc_memory[addr] &= ((bank == 2) ? 0xf0 : 0x0f);
			rtc_memory[addr] |= value << ((bank == 2) ? 0 : 4);
#if 0
			uae_u8 ov = rtc_memory[addr];
			if (rtc_memory[addr] != ov)
				write_battclock ();
#endif
			return;
		}
		/* alarm */
		if (bank == 1 && addr < 0x0d) {
			rtc_alarm[addr] = value;
			rtc_alarm[0] = rtc_alarm[1] = rtc_alarm[9] = rtc_alarm[12] = 0;
			rtc_alarm[3] &= ~0x8;
			rtc_alarm[5] &= ~0xc;
			rtc_alarm[6] &= ~0x8;
			rtc_alarm[8] &= ~0xc;
			rtc_alarm[10] &= ~0xe;
			rtc_alarm[11] &= ~0xc;
#if 0
			uae_u8 ov = rtc_alarm[addr];
			if (rtc_alarm[addr] != value)
				write_battclock ();
#endif
			return;
		}
		switch (addr)
		{
		case 0xD: clock_control_d = value; break;
		case 0xE: clock_control_e = value; break;
		case 0xF: clock_control_f = value; break;
		}
	}

}

#ifdef SAVESTATE

/* CIA-A and CIA-B save/restore code */

uae_u8 *restore_cia (int num, uae_u8 *src)
{
	uae_u8 b;
	uae_u16 w;
	uae_u32 l;

	/* CIA registers */
	b = restore_u8 ();					/* 0 PRA */
	if (num) ciabpra = b; else ciaapra = b;
	b = restore_u8 ();					/* 1 PRB */
	if (num) ciabprb = b; else ciaaprb = b;
	b = restore_u8 ();					/* 2 DDRA */
	if (num) ciabdra = b; else ciaadra = b;
	b = restore_u8 ();					/* 3 DDRB */
	if (num) ciabdrb = b; else ciaadrb = b;
	w = restore_u16 ();					/* 4 TA */
	if (num) ciabta = w; else ciaata = w;
	w = restore_u16 ();					/* 6 TB */
	if (num) ciabtb = w; else ciaatb = w;
	l = restore_u8 ();					/* 8/9/A TOD */
	l |= restore_u8 () << 8;
	l |= restore_u8 () << 16;
	if (num) ciabtod = l; else ciaatod = l;
	restore_u8 ();					/* B unused */
	b = restore_u8 ();					/* C SDR */
	if (num) ciabsdr = b; else ciaasdr = b;
	b = restore_u8 ();					/* D ICR INFORMATION (not mask!) */
	if (num) ciabicr = b; else ciaaicr = b;
	b = restore_u8 ();					/* E CRA */
	if (num) ciabcra = b; else ciaacra = b;
	b = restore_u8 ();					/* F CRB */
	if (num) ciabcrb = b; else ciaacrb = b;

	/* CIA internal data */

	b = restore_u8 ();					/* ICR MASK */
	if (num) ciabimask = b; else ciaaimask = b;
	w = restore_u8 ();					/* timer A latch */
	w |= restore_u8 () << 8;
	if (num) ciabla = w; else ciaala = w;
	w = restore_u8 ();					/* timer B latch */
	w |= restore_u8 () << 8;
	if (num) ciablb = w; else ciaalb = w;
	w = restore_u8 ();					/* TOD latched value */
	w |= restore_u8 () << 8;
	w |= restore_u8 () << 16;
	if (num) ciabtol = w; else ciaatol = w;
	l = restore_u8 ();					/* alarm */
	l |= restore_u8 () << 8;
	l |= restore_u8 () << 16;
	if (num) ciabalarm = l; else ciaaalarm = l;
	b = restore_u8 ();
	if (num) ciabtlatch = b & 1; else ciaatlatch = b & 1;	/* is TOD latched? */
	if (num) ciabtodon = b & 2; else ciaatodon = b & 2;		/* is TOD stopped? */
	b = restore_u8 ();
	if (num)
		div10 = CYCLE_UNIT * b;
	b = restore_u8 ();
	if (num) ciabsdr_cnt = b; else ciaasdr_cnt = b;
	return src;
}

uae_u8 *save_cia (int num, int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak,*dst, b;
	uae_u16 t;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = (uae_u8*)malloc (16 + 12 + 1 + 1);

	compute_passed_time ();

	/* CIA registers */

	b = num ? ciabpra : ciaapra;				/* 0 PRA */
	save_u8 (b);
	b = num ? ciabprb : ciaaprb;				/* 1 PRB */
	save_u8 (b);
	b = num ? ciabdra : ciaadra;				/* 2 DDRA */
	save_u8 (b);
	b = num ? ciabdrb : ciaadrb;				/* 3 DDRB */
	save_u8 (b);
	t = (num ? ciabta - ciabta_passed : ciaata - ciaata_passed);/* 4 TA */
	save_u16 (t);
	t = (num ? ciabtb - ciabtb_passed : ciaatb - ciaatb_passed);/* 6 TB */
	save_u16 (t);
	b = (num ? ciabtod : ciaatod);			/* 8 TODL */
	save_u8 (b);
	b = (num ? ciabtod >> 8 : ciaatod >> 8);		/* 9 TODM */
	save_u8 (b);
	b = (num ? ciabtod >> 16 : ciaatod >> 16);		/* A TODH */
	save_u8 (b);
	save_u8 (0);						/* B unused */
	b = num ? ciabsdr : ciaasdr;				/* C SDR */
	save_u8 (b);
	b = num ? ciabicr : ciaaicr;				/* D ICR INFORMATION (not mask!) */
	save_u8 (b);
	b = num ? ciabcra : ciaacra;				/* E CRA */
	save_u8 (b);
	b = num ? ciabcrb : ciaacrb;				/* F CRB */
	save_u8 (b);

	/* CIA internal data */

	save_u8 (num ? ciabimask : ciaaimask);			/* ICR */
	b = (num ? ciabla : ciaala);			/* timer A latch LO */
	save_u8 (b);
	b = (num ? ciabla >> 8 : ciaala >> 8);		/* timer A latch HI */
	save_u8 (b);
	b = (num ? ciablb : ciaalb);			/* timer B latch LO */
	save_u8 (b);
	b = (num ? ciablb >> 8 : ciaalb >> 8);		/* timer B latch HI */
	save_u8 (b);
	b = (num ? ciabtol : ciaatol);			/* latched TOD LO */
	save_u8 (b);
	b = (num ? ciabtol >> 8 : ciaatol >> 8);		/* latched TOD MED */
	save_u8 (b);
	b = (num ? ciabtol >> 16 : ciaatol >> 16);		/* latched TOD HI */
	save_u8 (b);
	b = (num ? ciabalarm : ciaaalarm);			/* alarm LO */
	save_u8 (b);
	b = (num ? ciabalarm >> 8 : ciaaalarm >> 8);	/* alarm MED */
	save_u8 (b);
	b = (num ? ciabalarm >> 16 : ciaaalarm >> 16);	/* alarm HI */
	save_u8 (b);
	b = 0;
	if (num)
		b |= ciabtlatch ? 1 : 0;
	else
		b |= ciaatlatch ? 1 : 0; /* is TOD latched? */
	if (num)
		b |= ciabtodon ? 2 : 0;
	else
		b |= ciaatodon ? 2 : 0;   /* TOD stopped? */
	save_u8 (b);
	save_u8 (div10 / CYCLE_UNIT);
	save_u8 (num ? ciabsdr_cnt : ciaasdr_cnt);
	*len = dst - dstbak;
	return dstbak;
}

#endif /* SAVESTATE */
