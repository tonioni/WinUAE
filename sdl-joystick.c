 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * SDL Joystick code
  * 
  * Copyright 1997 Bernd Schmidt
  * Copyright 1998 Krister Walfridsson
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "inputdevice.h"
#include "SDL.h"

static int nr_joysticks;

struct joyinfo {
    SDL_Joystick *joy;
    int axles;
    int buttons;
};

static struct joyinfo joys[MAX_INPUT_DEVICES];

static int isjoy (int pcport, int amigaport)
{
    if (pcport == 0)
	return JSEM_ISJOY0 (amigaport, &currprefs);
    else
	return JSEM_ISJOY1 (amigaport, &currprefs);
}

static void read_joy(int nr)
{
    int num, i, axes, axis;
    SDL_Joystick *joy;

    if (currprefs.input_selected_setting == 0) {
	if (nr >= 2)
	    return;
	if (isjoy (nr, 0)) {
	    if (JSEM_ISNUMPAD (0, &currprefs) || JSEM_ISCURSOR (0, &currprefs) || JSEM_ISSOMEWHEREELSE (0, &currprefs))
		return;
	} else if (isjoy (nr, 1)) {
	    if (JSEM_ISNUMPAD (1, &currprefs) || JSEM_ISCURSOR (1, &currprefs) || JSEM_ISSOMEWHEREELSE (1, &currprefs))
	        return;
	} else
	    return;
    }
    joy = joys[nr].joy;
    axes = SDL_JoystickNumAxes (joy);
    for (i = 0; i < axes; i++) {
	axis = SDL_JoystickGetAxis (joy, i);
	setjoystickstate (nr, i, axis, 32767);
    }

    num = SDL_JoystickNumButtons (joy);
    for (i = 0; i < num; i++) {
	int bs = SDL_JoystickGetButton (joy, i) ? 1 : 0;
        setjoybuttonstate (nr, i, bs);
    }
}

static int get_joystick_num (void)
{
    return nr_joysticks;
}

static int get_joystick_widget_num (int joy)
{
    return joys[joy].axles + joys[joy].buttons;
}

static int get_joystick_widget_type (int joy, int num, char *name)
{
    if (num >= joys[joy].axles && num < joys[joy].axles + joys[joy].buttons) {
	if (name)
	    sprintf (name, "Button %d", num + 1 - joys[joy].axles);
	return IDEV_WIDGET_BUTTON;
    } else if (num < joys[joy].axles) {
	if (name)
	    sprintf (name, "Axis %d", num + 1);
	return IDEV_WIDGET_AXIS;
    }
    return IDEV_WIDGET_NONE;
}

static int get_joystick_widget_first (int joy, int type)
{
    switch (type)
    {
	case IDEV_WIDGET_BUTTON:
	return joys[joy].axles;
	case IDEV_WIDGET_AXIS:
	return 0;
    }
    return -1;
}

static char *get_joystick_name (int joy)
{
    static char name[100];
    sprintf (name, "%d: %s", joy + 1, SDL_JoystickName (joy));
    return name;
}

static void read_joysticks (void)
{
    int i;
    SDL_JoystickUpdate ();
    for (i = 0; i < get_joystick_num(); i++)
	read_joy (i);
}

static int init_joysticks (void)
{
    int i;
    nr_joysticks = SDL_NumJoysticks ();
    if (nr_joysticks > MAX_INPUT_DEVICES)
	nr_joysticks = MAX_INPUT_DEVICES;
    for (i = 0; i < get_joystick_num(); i++) {
	joys[i].joy = SDL_JoystickOpen (i);
	joys[i].axles = SDL_JoystickNumAxes (joys[i].joy);
	joys[i].buttons = SDL_JoystickNumButtons (joys[i].joy);
    }
    return 1;
}

static void close_joysticks (void)
{
    int i;
    for (i = 0; i < get_joystick_num(); i++) {
	SDL_JoystickClose (joys[i].joy);
	joys[i].joy = 0;
    }
}

static int acquire_joy (int num, int flags)
{
    return 1;
}

static void unacquire_joy (int num)
{
}

struct inputdevice_functions inputdevicefunc_joystick = {
    init_joysticks, close_joysticks, acquire_joy, unacquire_joy,
    read_joysticks, get_joystick_num, get_joystick_name,
    get_joystick_widget_num, get_joystick_widget_type,
    get_joystick_widget_first
};
