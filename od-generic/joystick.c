 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Joystick emulation stubs
  * 
  * Copyright 1997 Bernd Schmidt
  * Copyright 2003 Richard Drummond
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "inputdevice.h"

static int init_joysticks (void)
{
   return 1;
}

static void close_joysticks (void)
{
}

static int acquire_joystick (int num, int flags)
{
    return 0;
}

static void unacquire_joystick (int num)
{
}

static void read_joysticks (void)
{
}

static int get_joystick_num (void)
{
    return 0;
}

static char *get_joystick_name (int joy)
{
    return 0;
}

static int get_joystick_widget_num (int joy)
{
    return 0;
}

static int get_joystick_widget_type (int joy, int num, char *name, uae_u32 *code)
{
    return IDEV_WIDGET_NONE;
}

static int get_joystick_widget_first (int joy, int type)
{
    return -1;
}

struct inputdevice_functions inputdevicefunc_joystick = {
    init_joysticks, close_joysticks, acquire_joystick, unacquire_joystick,
    read_joysticks, get_joystick_num, get_joystick_name,
    get_joystick_widget_num, get_joystick_widget_type,
    get_joystick_widget_first
};
