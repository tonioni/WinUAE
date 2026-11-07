/* Shared input policy, derived from the WinUAE Windows backend.
 * Copyright 1997 Mathias Ortmann, 1999-2001 Brian King,
 * 2000-2001 Bernd Roesch, 2002 Toni Wilen.
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "keymap.h"

static const int np[] = {HOST_KEY_NUMPAD0,
			 0,
			 HOST_KEY_DECIMAL,
			 0,
			 HOST_KEY_NUMPAD1,
			 1,
			 HOST_KEY_NUMPAD2,
			 2,
			 HOST_KEY_NUMPAD3,
			 3,
			 HOST_KEY_NUMPAD4,
			 4,
			 HOST_KEY_NUMPAD5,
			 5,
			 HOST_KEY_NUMPAD6,
			 6,
			 HOST_KEY_NUMPAD7,
			 7,
			 HOST_KEY_NUMPAD8,
			 8,
			 HOST_KEY_NUMPAD9,
			 9,
			 -1};

bool host_keyboard_hotkey(int keyboard, int native_code, int key, int default_gui, int f12,
			  int newstate)
{
    int code = 0;
    bool amode = currprefs.input_keyboard_type == 0;
    bool special = false;
    static int swapperdrive;
    int scancode = key;
    if (!key_specialpressed() && inputdevice_iskeymapped(keyboard, native_code))
	scancode = 0;

    if (newstate) {
	int defaultguikey = default_gui;
	if (currprefs.host_guikey >= 0x100) {
	    if (native_code == f12)
		return true;
	} else if (currprefs.host_guikey > 0) {
	    if (native_code == defaultguikey && currprefs.host_guikey != native_code) {
		scancode = 0;
		if (key_specialpressed() && key_ctrlpressed() && key_shiftpressed() &&
		    key_altpressed())
		    inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
	    } else if (native_code == currprefs.host_guikey) {
		inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
		scancode = 0;
	    }
	} else if (currprefs.host_guikey != 0 && !key_specialpressed() && !key_ctrlpressed() &&
		   !key_shiftpressed() && !key_altpressed() && native_code == defaultguikey) {
	    inputdevice_add_inputcode(AKS_ENTERGUI, 1, NULL);
	    scancode = 0;
	}
    }

    // write_log (_T("keyboard = %d scancode = 0x%02x state = %d\n"), keyboard, scancode, newstate
    // );

    if (newstate && code == 0 && amode) {

	switch (scancode) {

	case HOST_KEY_1:
	case HOST_KEY_2:
	case HOST_KEY_3:
	case HOST_KEY_4:
	case HOST_KEY_5:
	case HOST_KEY_6:
	case HOST_KEY_7:
	case HOST_KEY_8:
	case HOST_KEY_9:
	case HOST_KEY_0:
	    if (key_specialpressed()) {
		int num = scancode - HOST_KEY_1;
		if (key_shiftpressed())
		    num += 10;
		if (key_ctrlpressed()) {
		    swapperdrive = num;
		    if (swapperdrive > 3)
			swapperdrive = 0;
		} else {
		    int i;
		    for (i = 0; i < 4; i++) {
			if (!_tcscmp(currprefs.floppyslots[i].df, currprefs.dfxlist[num]))
			    changed_prefs.floppyslots[i].df[0] = 0;
		    }
		    _tcscpy(changed_prefs.floppyslots[swapperdrive].df, currprefs.dfxlist[num]);
		    set_config_changed();
		}
		special = true;
	    }
	    break;
	case HOST_KEY_NUMPAD0:
	case HOST_KEY_NUMPAD1:
	case HOST_KEY_NUMPAD2:
	case HOST_KEY_NUMPAD3:
	case HOST_KEY_NUMPAD4:
	case HOST_KEY_NUMPAD5:
	case HOST_KEY_NUMPAD6:
	case HOST_KEY_NUMPAD7:
	case HOST_KEY_NUMPAD8:
	case HOST_KEY_NUMPAD9:
	case HOST_KEY_DECIMAL:
	    if (key_specialpressed()) {
		int i = 0, v = -1;
		while (np[i] >= 0) {
		    v = np[i + 1];
		    if (np[i] == scancode)
			break;
		    i += 2;
		}
		if (v >= 0)
		    code = AKS_STATESAVEQUICK + v * 2 +
			   ((key_shiftpressed() || key_ctrlpressed()) ? 0 : 1);
		special = true;
	    }
	    break;
	}
    }

    if (code) {
	inputdevice_add_inputcode(code, 1, NULL);
	return true;
    }

    if (special) {
	inputdevice_checkqualifierkeycode(keyboard, native_code, newstate);
	return true;
    }
    return false;
}
