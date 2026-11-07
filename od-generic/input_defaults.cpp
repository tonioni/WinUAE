/* Shared input policy, derived from the WinUAE Windows backend.
 * Copyright 1997 Mathias Ortmann, 1999-2001 Brian King,
 * 2000-2001 Bernd Roesch, 2002 Toni Wilen.
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"
#include "input_defaults.h"

static int nextsub(struct uae_input_device *uid, int i, int slot, int sub)
{
    if (currprefs.input_advancedmultiinput) {
	while (uid[i].eventid[slot][sub] > 0) {
	    sub++;
	    if (sub >= MAX_INPUT_SUB_EVENT) {
		return -1;
	    }
	}
    }
    return sub;
}

static bool isemptyslot(struct uae_input_device *uid, int i, int slot, int sub, int port)
{
    return uid[i].eventid[slot][sub] == 0;
}

static void setid(struct uae_input_device *uid, int i, int slot, int sub, int port, int evt,
		  bool gp)
{
    sub = nextsub(uid, i, slot, sub);
    if (sub < 0) {
	return;
    }
    if (gp && sub == 0) {
	inputdevice_sparecopy(&uid[i], slot, sub);
    }
    uid[i].eventid[slot][sub] = evt;
    uid[i].port[slot][sub] = port + 1;
}
static void setid(struct uae_input_device *uid, int i, int slot, int sub, int port, int evt, int af,
		  bool gp)
{
    sub = nextsub(uid, i, slot, sub);
    if (sub < 0) {
	return;
    }
    setid(uid, i, slot, sub, port, evt, gp);
    uid[i].flags[slot][sub] &= ~ID_FLAG_AUTOFIRE_MASK;
    if (af >= JPORT_AF_NORMAL)
	uid[i].flags[slot][sub] |= ID_FLAG_AUTOFIRE;
    if (af == JPORT_AF_TOGGLE)
	uid[i].flags[slot][sub] |= ID_FLAG_TOGGLE;
    if (af == JPORT_AF_ALWAYS)
	uid[i].flags[slot][sub] |= ID_FLAG_INVERTTOGGLE;
    if (af == JPORT_AF_TOGGLENOAF)
	uid[i].flags[slot][sub] |= ID_FLAG_INVERT;
}

int input_default_mouse(struct uae_input_device *uid, int i, int port, int af, bool gp, bool wheel,
			bool joymouseswap, const host_input_device &device)
{
    setid(uid, i, ID_AXIS_OFFSET + 0, 0, port,
	  port ? INPUTEVENT_MOUSE2_HORIZ : INPUTEVENT_MOUSE1_HORIZ, gp);
    setid(uid, i, ID_AXIS_OFFSET + 1, 0, port,
	  port ? INPUTEVENT_MOUSE2_VERT : INPUTEVENT_MOUSE1_VERT, gp);
    if (wheel && (port == 0 || device.clear_wheel))
	setid(uid, i, ID_AXIS_OFFSET + 2, 0, port, port ? 0 : INPUTEVENT_MOUSE1_WHEEL, gp);
    setid(uid, i, ID_BUTTON_OFFSET + 0, 0, port,
	  port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON, af, gp);
    setid(uid, i, ID_BUTTON_OFFSET + 1, 0, port,
	  port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON, gp);
    setid(uid, i, ID_BUTTON_OFFSET + 2, 0, port,
	  port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON, gp);
    if (wheel && port == 0) { /* map back and forward to ALT+LCUR and ALT+RCUR */
	if (device.button(device.context, 3)) {
	    setid(uid, i, ID_BUTTON_OFFSET + 3, 0, port, INPUTEVENT_KEY_ALT_LEFT, gp);
	    setid(uid, i, ID_BUTTON_OFFSET + 3, 1, port, INPUTEVENT_KEY_CURSOR_LEFT, gp);
	    if (device.button(device.context, 4)) {
		setid(uid, i, ID_BUTTON_OFFSET + 4, 0, port, INPUTEVENT_KEY_ALT_LEFT, gp);
		setid(uid, i, ID_BUTTON_OFFSET + 4, 1, port, INPUTEVENT_KEY_CURSOR_RIGHT, gp);
	    }
	}
    }
    if (i == 0)
	return 1;
    return 0;
}

int input_default_lightpen(struct uae_input_device *uid, int i, int port, int af, bool gp,
			   bool joymouseswap, int submode, const host_input_device &device)
{
    setid(uid, i, ID_AXIS_OFFSET + 0, 0, port, INPUTEVENT_LIGHTPEN_HORIZ, gp);
    setid(uid, i, ID_AXIS_OFFSET + 1, 0, port, INPUTEVENT_LIGHTPEN_VERT, gp);
    int button = port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON;
    switch (submode) {
    case 1:
	button = port ? INPUTEVENT_JOY2_LEFT : INPUTEVENT_JOY1_LEFT;
	break;
    }
    setid(uid, i, ID_BUTTON_OFFSET + 0, 0, port, button, gp);
    if (i == 0)
	return 1;
    return 0;
}

int input_default_joystick(struct uae_input_device *uid, int i, int port, int af, int mode, bool gp,
			   bool joymouseswap, bool default_osk, const host_input_device &device)
{
    int j;
    int h, v;
    if (mode == JSEM_MODE_MOUSE_CDTV) {
	h = INPUTEVENT_MOUSE_CDTV_HORIZ;
	v = INPUTEVENT_MOUSE_CDTV_VERT;
    } else if (port >= 2) {
	h = port == 3 ? INPUTEVENT_PAR_JOY2_HORIZ : INPUTEVENT_PAR_JOY1_HORIZ;
	v = port == 3 ? INPUTEVENT_PAR_JOY2_VERT : INPUTEVENT_PAR_JOY1_VERT;
    } else {
	h = port ? INPUTEVENT_JOY2_HORIZ : INPUTEVENT_JOY1_HORIZ;
	;
	v = port ? INPUTEVENT_JOY2_VERT : INPUTEVENT_JOY1_VERT;
    }
    setid(uid, i, ID_AXIS_OFFSET + 0, 0, port, h, gp);
    setid(uid, i, ID_AXIS_OFFSET + 1, 0, port, v, gp);

    if (port >= 2) {
	setid(uid, i, ID_BUTTON_OFFSET + 0, 0, port,
	      port == 3 ? INPUTEVENT_PAR_JOY2_FIRE_BUTTON : INPUTEVENT_PAR_JOY1_FIRE_BUTTON, af,
	      gp);
    } else {
	setid(uid, i, ID_BUTTON_OFFSET + 0, 0, port,
	      port ? INPUTEVENT_JOY2_FIRE_BUTTON : INPUTEVENT_JOY1_FIRE_BUTTON, af, gp);
	if (device.button(device.context, 1))
	    setid(uid, i, ID_BUTTON_OFFSET + 1, 0, port,
		  port ? INPUTEVENT_JOY2_2ND_BUTTON : INPUTEVENT_JOY1_2ND_BUTTON, gp);
	if (mode != JSEM_MODE_JOYSTICK) {
	    if (device.button(device.context, 2))
		setid(uid, i, ID_BUTTON_OFFSET + 2, 0, port,
		      port ? INPUTEVENT_JOY2_3RD_BUTTON : INPUTEVENT_JOY1_3RD_BUTTON, gp);
	}
	if (device.button(device.context, 3) &&
	    (device.replace_osk || isemptyslot(uid, i, ID_BUTTON_OFFSET + 3, 0, port)) &&
	    default_osk) {
	    setid(uid, i, ID_BUTTON_OFFSET + 3, 0, port, INPUTEVENT_SPC_OSK, gp);
	}
    }

    for (j = 2; j < device.axes - 1; j++) {
	if (device.hat(device.context, j)) {
	    setid(uid, i, ID_AXIS_OFFSET + j + 0, 0, port, h, gp);
	    setid(uid, i, ID_AXIS_OFFSET + j + 1, 0, port, v, gp);
	    j++;
	}
    }

    if (mode == JSEM_MODE_JOYSTICK_CD32) {
	setid(uid, i, ID_BUTTON_OFFSET + 0, 0, port,
	      port ? INPUTEVENT_JOY2_CD32_RED : INPUTEVENT_JOY1_CD32_RED, af, gp);
	if (device.button(device.context, 1)) {
	    setid(uid, i, ID_BUTTON_OFFSET + 1, 0, port,
		  port ? INPUTEVENT_JOY2_CD32_BLUE : INPUTEVENT_JOY1_CD32_BLUE, gp);
	}
	if (device.button(device.context, 2))
	    setid(uid, i, ID_BUTTON_OFFSET + 2, 0, port,
		  port ? INPUTEVENT_JOY2_CD32_GREEN : INPUTEVENT_JOY1_CD32_GREEN, gp);
	if (device.button(device.context, 3))
	    setid(uid, i, ID_BUTTON_OFFSET + 3, 0, port,
		  port ? INPUTEVENT_JOY2_CD32_YELLOW : INPUTEVENT_JOY1_CD32_YELLOW, gp);
	if (device.button(device.context, 4))
	    setid(uid, i, ID_BUTTON_OFFSET + 4, 0, port,
		  port ? INPUTEVENT_JOY2_CD32_RWD : INPUTEVENT_JOY1_CD32_RWD, gp);
	if (device.button(device.context, 5))
	    setid(uid, i, ID_BUTTON_OFFSET + 5, 0, port,
		  port ? INPUTEVENT_JOY2_CD32_FFW : INPUTEVENT_JOY1_CD32_FFW, gp);
	if (device.button(device.context, 6))
	    setid(uid, i, ID_BUTTON_OFFSET + 6, 0, port,
		  port ? INPUTEVENT_JOY2_CD32_PLAY : INPUTEVENT_JOY1_CD32_PLAY, gp);
    }
    if (i == 0)
	return 1;
    return 0;
}

int input_default_joystick_analog(struct uae_input_device *uid, int i, int port, int af, bool gp,
				  bool joymouseswap, bool default_osk,
				  const host_input_device &device)
{
    int j;
    setid(uid, i, ID_AXIS_OFFSET + 0, 0, port,
	  port ? INPUTEVENT_JOY2_HORIZ_POT : INPUTEVENT_JOY1_HORIZ_POT, gp);
    setid(uid, i, ID_AXIS_OFFSET + 1, 0, port,
	  port ? INPUTEVENT_JOY2_VERT_POT : INPUTEVENT_JOY1_VERT_POT, gp);
    setid(uid, i, ID_BUTTON_OFFSET + 0, 0, port, port ? INPUTEVENT_JOY2_LEFT : INPUTEVENT_JOY1_LEFT,
	  af, gp);
    if (device.button(device.context, 1))
	setid(uid, i, ID_BUTTON_OFFSET + 1, 0, port,
	      port ? INPUTEVENT_JOY2_RIGHT : INPUTEVENT_JOY1_RIGHT, gp);
    if (device.button(device.context, 2))
	setid(uid, i, ID_BUTTON_OFFSET + 2, 0, port, port ? INPUTEVENT_JOY2_UP : INPUTEVENT_JOY1_UP,
	      gp);
    if (device.button(device.context, 3))
	setid(uid, i, ID_BUTTON_OFFSET + 3, 0, port,
	      port ? INPUTEVENT_JOY2_DOWN : INPUTEVENT_JOY1_DOWN, gp);
    if (device.button(device.context, 4) &&
	(device.replace_osk || isemptyslot(uid, i, ID_BUTTON_OFFSET + 4, 0, port)) && default_osk)
	setid(uid, i, ID_BUTTON_OFFSET + 4, 0, port, INPUTEVENT_SPC_OSK, gp);

    for (j = 2; j < device.axes - 1; j++) {
	if (device.hat(device.context, j)) {
	    setid(uid, i, ID_AXIS_OFFSET + j + 0, 0, port,
		  port ? INPUTEVENT_JOY2_HORIZ_POT : INPUTEVENT_JOY1_HORIZ_POT, gp);
	    setid(uid, i, ID_AXIS_OFFSET + j + 1, 0, port,
		  port ? INPUTEVENT_JOY2_VERT_POT : INPUTEVENT_JOY1_VERT_POT, gp);
	    j++;
	}
    }

    if (i == 0)
	return 1;
    return 0;
}
