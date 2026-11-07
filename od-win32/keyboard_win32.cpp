/*
* UAE - The Un*x Amiga Emulator
*
* Additional Win32 helper functions not calling any system routines
*
* (c) 1997 Mathias Ortmann
* (c) 1999-2001 Brian King
* (c) 2000-2001 Bernd Roesch
* (c) 2002 Toni Wilen
*/

#include "sysconfig.h"

#include <windows.h>
#include <dinput.h>

#include "sysdeps.h"
#include "uae.h"
#include "gui.h"
#include "options.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "od-generic/keymap.h"
#include "win32.h"
#include "debug.h"

//#define DBG_KEYBD 1
//#define DEBUG_KBD

#ifndef DIK_PREVTRACK
/* Not defined by MinGW */
#define DIK_PREVTRACK 0x90
#endif

static host_keymap keymap;
static const int native_keys[HOST_KEY_COUNT] = {
 -1, // HOST_KEY_NONE
 DIK_ESCAPE, // HOST_KEY_ESCAPE
 DIK_F1, // HOST_KEY_F1
 DIK_F2, // HOST_KEY_F2
 DIK_F3, // HOST_KEY_F3
 DIK_F4, // HOST_KEY_F4
 DIK_F5, // HOST_KEY_F5
 DIK_F6, // HOST_KEY_F6
 DIK_F7, // HOST_KEY_F7
 DIK_F8, // HOST_KEY_F8
 DIK_F9, // HOST_KEY_F9
 DIK_F10, // HOST_KEY_F10
 DIK_1, // HOST_KEY_1
 DIK_2, // HOST_KEY_2
 DIK_3, // HOST_KEY_3
 DIK_4, // HOST_KEY_4
 DIK_5, // HOST_KEY_5
 DIK_6, // HOST_KEY_6
 DIK_7, // HOST_KEY_7
 DIK_8, // HOST_KEY_8
 DIK_9, // HOST_KEY_9
 DIK_0, // HOST_KEY_0
 DIK_TAB, // HOST_KEY_TAB
 DIK_A, // HOST_KEY_A
 DIK_B, // HOST_KEY_B
 DIK_C, // HOST_KEY_C
 DIK_D, // HOST_KEY_D
 DIK_E, // HOST_KEY_E
 DIK_F, // HOST_KEY_F
 DIK_G, // HOST_KEY_G
 DIK_H, // HOST_KEY_H
 DIK_I, // HOST_KEY_I
 DIK_J, // HOST_KEY_J
 DIK_K, // HOST_KEY_K
 DIK_L, // HOST_KEY_L
 DIK_M, // HOST_KEY_M
 DIK_N, // HOST_KEY_N
 DIK_O, // HOST_KEY_O
 DIK_P, // HOST_KEY_P
 DIK_Q, // HOST_KEY_Q
 DIK_R, // HOST_KEY_R
 DIK_S, // HOST_KEY_S
 DIK_T, // HOST_KEY_T
 DIK_U, // HOST_KEY_U
 DIK_W, // HOST_KEY_W
 DIK_V, // HOST_KEY_V
 DIK_X, // HOST_KEY_X
 DIK_Y, // HOST_KEY_Y
 DIK_Z, // HOST_KEY_Z
 DIK_CAPITAL, // HOST_KEY_CAPITAL
 DIK_NUMPAD1, // HOST_KEY_NUMPAD1
 DIK_NUMPAD2, // HOST_KEY_NUMPAD2
 DIK_NUMPAD3, // HOST_KEY_NUMPAD3
 DIK_NUMPAD4, // HOST_KEY_NUMPAD4
 DIK_NUMPAD5, // HOST_KEY_NUMPAD5
 DIK_NUMPAD6, // HOST_KEY_NUMPAD6
 DIK_NUMPAD7, // HOST_KEY_NUMPAD7
 DIK_NUMPAD8, // HOST_KEY_NUMPAD8
 DIK_NUMPAD9, // HOST_KEY_NUMPAD9
 DIK_NUMPAD0, // HOST_KEY_NUMPAD0
 DIK_DECIMAL, // HOST_KEY_DECIMAL
 DIK_ADD, // HOST_KEY_ADD
 DIK_SUBTRACT, // HOST_KEY_SUBTRACT
 DIK_MULTIPLY, // HOST_KEY_MULTIPLY
 DIK_DIVIDE, // HOST_KEY_DIVIDE
 DIK_NUMPADENTER, // HOST_KEY_NUMPADENTER
 DIK_MINUS, // HOST_KEY_MINUS
 DIK_EQUALS, // HOST_KEY_EQUALS
 DIK_BACK, // HOST_KEY_BACK
 DIK_RETURN, // HOST_KEY_RETURN
 DIK_SPACE, // HOST_KEY_SPACE
 DIK_LSHIFT, // HOST_KEY_LSHIFT
 DIK_LCONTROL, // HOST_KEY_LCONTROL
 DIK_LWIN, // HOST_KEY_LWIN
 DIK_LMENU, // HOST_KEY_LMENU
 DIK_RMENU, // HOST_KEY_RMENU
 DIK_RWIN, // HOST_KEY_RWIN
 DIK_APPS, // HOST_KEY_APPS
 DIK_RCONTROL, // HOST_KEY_RCONTROL
 DIK_RSHIFT, // HOST_KEY_RSHIFT
 DIK_UP, // HOST_KEY_UP
 DIK_DOWN, // HOST_KEY_DOWN
 DIK_LEFT, // HOST_KEY_LEFT
 DIK_RIGHT, // HOST_KEY_RIGHT
 DIK_INSERT, // HOST_KEY_INSERT
 DIK_DELETE, // HOST_KEY_DELETE
 DIK_HOME, // HOST_KEY_HOME
 DIK_NEXT, // HOST_KEY_NEXT
 DIK_PRIOR, // HOST_KEY_PRIOR
 DIK_LBRACKET, // HOST_KEY_LBRACKET
 DIK_RBRACKET, // HOST_KEY_RBRACKET
 DIK_SEMICOLON, // HOST_KEY_SEMICOLON
 DIK_APOSTROPHE, // HOST_KEY_APOSTROPHE
 DIK_GRAVE, // HOST_KEY_GRAVE
 DIK_BACKSLASH, // HOST_KEY_BACKSLASH
 DIK_COMMA, // HOST_KEY_COMMA
 DIK_PERIOD, // HOST_KEY_PERIOD
 DIK_SLASH, // HOST_KEY_SLASH
 DIK_OEM_102, // HOST_KEY_OEM_102
 DIK_F11, // HOST_KEY_F11
 DIK_F13, // HOST_KEY_F13
 DIK_F14, // HOST_KEY_F14
 DIK_F15, // HOST_KEY_F15
 DIK_SYSRQ, // HOST_KEY_SYSRQ
 DIK_END, // HOST_KEY_END
 DIK_PAUSE, // HOST_KEY_PAUSE
 DIK_PAUSE+1, // HOST_KEY_PAUSE_RELEASE
 DIK_F12, // HOST_KEY_F12
 DIK_MEDIASTOP, // HOST_KEY_MEDIASTOP
 DIK_PLAYPAUSE, // HOST_KEY_PLAYPAUSE
 DIK_PREVTRACK, // HOST_KEY_PREVTRACK
 DIK_NEXTTRACK, // HOST_KEY_NEXTTRACK
 DIK_SCROLL, // HOST_KEY_SCROLL
 DIK_NUMLOCK, // HOST_KEY_NUMLOCK
 -1, // HOST_KEY_NONUSHASH
};

static int capslockstate;
static int host_capslockstate, host_numlockstate, host_scrolllockstate;

int getcapslockstate (void)
{
	return capslockstate;
}
void setcapslockstate (int state)
{
	capslockstate = state;
}

int getcapslock (void)
{
	int capstable[7];

	// this returns bogus state if caps change when in exclusive mode..
	host_capslockstate = GetKeyState (VK_CAPITAL) & 1;
	host_numlockstate = GetKeyState (VK_NUMLOCK) & 1;
	host_scrolllockstate = GetKeyState (VK_SCROLL) & 1;
	capstable[0] = DIK_CAPITAL;
	capstable[1] = host_capslockstate;
	capstable[2] = DIK_NUMLOCK;
	capstable[3] = host_numlockstate;
	capstable[4] = DIK_SCROLL;
	capstable[5] = host_scrolllockstate;
	capstable[6] = 0;
	capslockstate = inputdevice_synccapslock (capslockstate, capstable);
	if (currprefs.keyboard_mode == 0) {
		gui_data.capslock = host_capslockstate;
		gui_led(LED_CAPS, gui_data.capslock, -1);
	}
	return capslockstate;
}

void clearallkeys (void)
{
	inputdevice_updateconfig (&changed_prefs, &currprefs);
}

bool my_kbd_handler(int keyboard, int scancode, int newstate, bool alwaysrelease)
{
 bool amode = currprefs.input_keyboard_type == 0;
	if (amode && scancode == DIK_F11 && currprefs.win32_ctrl_F11_is_quit && key_ctrlpressed()) {
		if (!quit_ok())
			return true;
		uae_quit();
		return true;
	}

	if (scancode == DIK_F9 && key_specialpressed ()) {
		extern bool toggle_3d_debug(void);
		if (newstate) {
			if (toggle_3d_debug()) {
				return true;
			}
		}
	}

 if (host_keyboard_hotkey(keyboard, scancode,
   host_key_from_native(native_keys, scancode),
   amode ? DIK_F12 : DIK_NUMLOCK, DIK_F12, newstate))
  return true;
	if (!key_specialpressed () && newstate) {
		if (scancode == DIK_CAPITAL) {
			host_capslockstate = host_capslockstate ? 0 : 1;
			capslockstate = host_capslockstate;
		}
		if (scancode == DIK_NUMLOCK) {
			host_numlockstate = host_numlockstate ? 0 : 1;
			capslockstate = host_numlockstate;
		}
		if (scancode == DIK_SCROLL) {
			host_scrolllockstate = host_scrolllockstate ? 0 : 1;
			capslockstate = host_scrolllockstate;
		}
	}

	return inputdevice_translatekeycode (keyboard, scancode, newstate, alwaysrelease) != 0;
}

void keyboard_settrans (void)
{
	host_keymap_init(&keymap, native_keys, false);
	inputdevice_setkeytranslation(keymap.translations, keymap.mappings);
}


int target_checkcapslock (int scancode, int *state)
{
	if (scancode != DIK_CAPITAL && scancode != DIK_NUMLOCK && scancode != DIK_SCROLL) {
		return 0;
	}
	if (currprefs.keyboard_mode > 0) {
		return 1;
	}
	if (*state == 0) {
		return -1;
	}
	if (scancode == DIK_CAPITAL) {
		*state = host_capslockstate;
		if (gui_data.capslock != (host_capslockstate != 0)) {
			gui_data.capslock = host_capslockstate;
			gui_led(LED_CAPS, gui_data.capslock, -1);
		}
	}
	if (scancode == DIK_NUMLOCK) {
		*state = host_numlockstate;
	}
	if (scancode == DIK_SCROLL) {
		*state = host_scrolllockstate;
	}
	return 1;
}
