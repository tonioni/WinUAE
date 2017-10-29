
/*
* UAE - The Un*x Amiga Emulator
*
* Emulates simple protection dongles
*
* Copyright 2009 Toni Wilen
*/


#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "dongle.h"
#include "events.h"
#include "uae.h"

#define ROBOCOP3 1
#define LEADERBOARD 2
#define BAT2 3
#define ITALY90 4
#define DAMESGRANDMAITRE 5
#define RUGBYCOACH 6
#define CRICKETCAPTAIN 7
#define LEVIATHAN 8
#define MUSICMASTER 9
#define LOGISTIX 10

static int flag;
static unsigned int cycles;

/*
RoboCop 3
- set firebutton as output
- read JOY1DAT
- pulse firebutton (high->low)
- read JOY1DAT
- JOY1DAT bit 8 must toggle

Leader Board
- JOY1DAT, both up and down active (0x0101)

B.A.T. II
- set all serial pins as output except CTS
- game pulses DTR (high->low)
- CTS must be one
- delay
- CTS must be zero

Italy '90 Soccer
- 220k resistor between pins 5 (+5v) and 7 (POTX)
- POT1DAT POTX must be between 0x32 and 0x60

Dames Grand Maitre
- read POT1
- POT1X != POT1Y
- POT1Y * 256 / POT1X must be between 450 and 500

Rugby Coach
- JOY1DAT, left, up and down active (0x0301)

Cricket Captain
- JOY0DAT bits 0 and 1:
- 10 01 11 allowed
- must continuously change state

Leviathan
- same as Leaderboard but in mouse port

Logistix/SuperBase
- second button must be high
- POT1X = 150k
- POT1Y = 100k
- POT1X * 10 / POT1Y must be between 12 and 33

Music Master
- sets joystick port 2 fire button output + low
- first JOY1DAT AND 0x0303 must be zero.
- following JOY1DAT AND 0x0303 reads must be nonzero.

*/

static uae_u8 oldcia[2][16];

void dongle_reset (void)
{
	flag = 0;
	memset (oldcia, 0, sizeof oldcia);
}

uae_u8 dongle_cia_read (int cia, int reg, uae_u8 extra, uae_u8 val)
{
	if (!currprefs.dongle)
		return val;
	switch (currprefs.dongle)
	{
	case BAT2:
		if (cia == 1 && reg == 0) {
			if (!flag || get_cycles () > cycles + CYCLE_UNIT * 200) {
				val &= ~0x10;
				flag = 0;
			} else {
				val |= 0x10;
			}
		}
		break;
	}
	return val;
}

void dongle_cia_write (int cia, int reg, uae_u8 extra, uae_u8 val)
{
	if (!currprefs.dongle)
		return;
	switch (currprefs.dongle)
	{
	case ROBOCOP3:
		if (cia == 0 && reg == 0 && (val & 0x80))
			flag ^= 1;
		break;
	case BAT2:
		if (cia == 1 && reg == 0 && !(val & 0x80)) {
			flag = 1;
			cycles = get_cycles ();
		}
		break;
	case MUSICMASTER:
		if (cia == 0 && reg == 0) {
			if (!(val & 0x80) && (extra & 0x80)) {
				flag = 1;
			} else {
				flag = 0;
			}
		}
		break;
	}
	oldcia[cia][reg] = val;
}

void dongle_joytest (uae_u16 val)
{
}

uae_u16 dongle_joydat (int port, uae_u16 val)
{
	if (!currprefs.dongle)
		return val;
	switch (currprefs.dongle)
	{
	case ROBOCOP3:
		if (port == 1 && flag)
			val += 0x100;
		break;
	case LEADERBOARD:
		if (port == 1) {
			val &= ~0x0303;
			val |= 0x0101;
		}
		break;
	case LEVIATHAN:
		if (port == 0) {
			val &= ~0x0303;
			val |= 0x0101;
		}
		break;
	case RUGBYCOACH:
		if (port == 1) {
			val &= ~0x0303;
			val|= 0x0301;
		}
		break;
	case CRICKETCAPTAIN:
		if (port == 0) {
			val &= ~0x0003;
			if (flag == 0)
				val |= 0x0001;
			else
				val |= 0x0002;
		}
		flag ^= 1;
		break;
	case MUSICMASTER:
		if (port == 1 && !flag) {
			val = 0;
		} else if (port == 1 && flag == 1) {
			val = 0;
			flag++;
		} else if (port == 1 && flag == 2) {
			val = 0x0303;
		}
		break;
	}
	return val;
}

void dongle_potgo (uae_u16 val)
{
	if (!currprefs.dongle)
		return;
	switch (currprefs.dongle)
	{
	case ITALY90:
	case LOGISTIX:
	case DAMESGRANDMAITRE:
		flag = (uaerand () & 7) - 3;
		break;
	}

}

uae_u16 dongle_potgor (uae_u16 val)
{
	if (!currprefs.dongle)
		return val;
	switch (currprefs.dongle)
	{
	case LOGISTIX:
		val |= 1 << 14;
		break;
	}
	return val;
}

int dongle_analogjoy (int joy, int axis)
{
	int v = -1;
	if (!currprefs.dongle)
		return -1;
	switch (currprefs.dongle)
	{
	case ITALY90:
		if (joy == 1 && axis == 0)
			v = 73;
		break;
	case LOGISTIX:
		if (joy == 1) {
			if (axis == 0)
				v = 21;
			if (axis == 1)
				v = 10;
		}
		break;
	case DAMESGRANDMAITRE:
		if (joy == 1) {
			if (axis == 1)
				v = 80;
			if (axis == 0)
				v = 43;
		}
		break;

	}
	if (v >= 0) {
		v += flag;
		if (v < 0)
			v = 0;
	}
	return v;
}
