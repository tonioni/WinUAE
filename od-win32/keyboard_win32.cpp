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

#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <windows.h>
#include <dinput.h>

#include "sysdeps.h"
#include "uae.h"
#include "gui.h"
#include "options.h"
#include "audio.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "xwin.h"
#include "drawing.h"
#include "disk.h"
#include "keybuf.h"
#include "win32.h"
#include "debug.h"
#include "ar.h"
#include "savestate.h"
#include "sound.h"
#include "akiko.h"
#include "arcadia.h"

//#define DBG_KEYBD 1
//#define DEBUG_KBD

#ifndef DIK_PREVTRACK
/* Not defined by MinGW */
#define DIK_PREVTRACK 0x90
#endif

static struct uae_input_device_kbr_default keytrans_amiga[] = {

	{ DIK_ESCAPE, INPUTEVENT_KEY_ESC },

	{  DIK_F1,  INPUTEVENT_KEY_F1, 0, INPUTEVENT_SPC_FLOPPY0, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_EFLOPPY0, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },
	{  DIK_F2,  INPUTEVENT_KEY_F2, 0, INPUTEVENT_SPC_FLOPPY1, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_EFLOPPY1, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },
	{  DIK_F3,  INPUTEVENT_KEY_F3, 0, INPUTEVENT_SPC_FLOPPY2, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_EFLOPPY2, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },
	{  DIK_F4,  INPUTEVENT_KEY_F4, 0, INPUTEVENT_SPC_FLOPPY3, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_EFLOPPY3, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },

	{  DIK_F5,  INPUTEVENT_KEY_F5, 0, INPUTEVENT_SPC_CD0, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_ECD0, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },
	{  DIK_F6,  INPUTEVENT_KEY_F6, 0, INPUTEVENT_SPC_STATERESTOREDIALOG, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_STATESAVEDIALOG, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },
	{  DIK_F7,  INPUTEVENT_KEY_F7 },
	{  DIK_F8,  INPUTEVENT_KEY_F8 },
	{  DIK_F9,  INPUTEVENT_KEY_F9, 0, INPUTEVENT_SPC_TOGGLERTG, ID_FLAG_QUALIFIER_SPECIAL },
	{ DIK_F10, INPUTEVENT_KEY_F10 },

	{ DIK_1, INPUTEVENT_KEY_1 },
	{ DIK_2, INPUTEVENT_KEY_2 },
	{ DIK_3, INPUTEVENT_KEY_3 },
	{ DIK_4, INPUTEVENT_KEY_4 },
	{ DIK_5, INPUTEVENT_KEY_5 },
	{ DIK_6, INPUTEVENT_KEY_6 },
	{ DIK_7, INPUTEVENT_KEY_7 },
	{ DIK_8, INPUTEVENT_KEY_8 },
	{ DIK_9, INPUTEVENT_KEY_9 },
	{ DIK_0, INPUTEVENT_KEY_0 },

	{ DIK_TAB, INPUTEVENT_KEY_TAB },

	{ DIK_A, INPUTEVENT_KEY_A },
	{ DIK_B, INPUTEVENT_KEY_B },
	{ DIK_C, INPUTEVENT_KEY_C },
	{ DIK_D, INPUTEVENT_KEY_D },
	{ DIK_E, INPUTEVENT_KEY_E },
	{ DIK_F, INPUTEVENT_KEY_F },
	{ DIK_G, INPUTEVENT_KEY_G },
	{ DIK_H, INPUTEVENT_KEY_H },
	{ DIK_I, INPUTEVENT_KEY_I },
	{ DIK_J, INPUTEVENT_KEY_J, 0, INPUTEVENT_SPC_SWAPJOYPORTS, ID_FLAG_QUALIFIER_SPECIAL },
	{ DIK_K, INPUTEVENT_KEY_K },
	{ DIK_L, INPUTEVENT_KEY_L },
	{ DIK_M, INPUTEVENT_KEY_M },
	{ DIK_N, INPUTEVENT_KEY_N },
	{ DIK_O, INPUTEVENT_KEY_O },
	{ DIK_P, INPUTEVENT_KEY_P },
	{ DIK_Q, INPUTEVENT_KEY_Q },
	{ DIK_R, INPUTEVENT_KEY_R },
	{ DIK_S, INPUTEVENT_KEY_S },
	{ DIK_T, INPUTEVENT_KEY_T },
	{ DIK_U, INPUTEVENT_KEY_U },
	{ DIK_W, INPUTEVENT_KEY_W },
	{ DIK_V, INPUTEVENT_KEY_V },
	{ DIK_X, INPUTEVENT_KEY_X },
	{ DIK_Y, INPUTEVENT_KEY_Y },
	{ DIK_Z, INPUTEVENT_KEY_Z },

	{ DIK_CAPITAL, INPUTEVENT_KEY_CAPS_LOCK, ID_FLAG_TOGGLE },

	{ DIK_NUMPAD1, INPUTEVENT_KEY_NP_1 },
	{ DIK_NUMPAD2, INPUTEVENT_KEY_NP_2 },
	{ DIK_NUMPAD3, INPUTEVENT_KEY_NP_3 },
	{ DIK_NUMPAD4, INPUTEVENT_KEY_NP_4 },
	{ DIK_NUMPAD5, INPUTEVENT_KEY_NP_5 },
	{ DIK_NUMPAD6, INPUTEVENT_KEY_NP_6 },
	{ DIK_NUMPAD7, INPUTEVENT_KEY_NP_7 },
	{ DIK_NUMPAD8, INPUTEVENT_KEY_NP_8 },
	{ DIK_NUMPAD9, INPUTEVENT_KEY_NP_9 },
	{ DIK_NUMPAD0, INPUTEVENT_KEY_NP_0 },
	{ DIK_DECIMAL, INPUTEVENT_KEY_NP_PERIOD },
	{ DIK_ADD, INPUTEVENT_KEY_NP_ADD, 0, INPUTEVENT_SPC_VOLUME_UP, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_MASTER_VOLUME_UP, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_CONTROL, INPUTEVENT_SPC_INCREASE_REFRESHRATE, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT  },
	{ DIK_SUBTRACT, INPUTEVENT_KEY_NP_SUB, 0, INPUTEVENT_SPC_VOLUME_DOWN, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_MASTER_VOLUME_DOWN, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_CONTROL, INPUTEVENT_SPC_DECREASE_REFRESHRATE, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT  },
	{ DIK_MULTIPLY, INPUTEVENT_KEY_NP_MUL, 0, INPUTEVENT_SPC_VOLUME_MUTE, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_MASTER_VOLUME_MUTE, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_CONTROL },
	{ DIK_DIVIDE, INPUTEVENT_KEY_NP_DIV, 0, INPUTEVENT_SPC_STATEREWIND, ID_FLAG_QUALIFIER_SPECIAL },
	{ DIK_NUMPADENTER, INPUTEVENT_KEY_ENTER },

	{ DIK_MINUS, INPUTEVENT_KEY_SUB },
	{ DIK_EQUALS, INPUTEVENT_KEY_EQUALS },
	{ DIK_BACK, INPUTEVENT_KEY_BACKSPACE },
	{ DIK_RETURN, INPUTEVENT_KEY_RETURN },
	{ DIK_SPACE, INPUTEVENT_KEY_SPACE },

	{ DIK_LSHIFT, INPUTEVENT_KEY_SHIFT_LEFT, 0, INPUTEVENT_SPC_QUALIFIER_SHIFT },
	{ DIK_LCONTROL, INPUTEVENT_KEY_CTRL, 0, INPUTEVENT_SPC_QUALIFIER_CONTROL },
	{ DIK_LWIN, INPUTEVENT_KEY_AMIGA_LEFT, 0, INPUTEVENT_SPC_QUALIFIER_WIN },
	{ DIK_LMENU, INPUTEVENT_KEY_ALT_LEFT, 0, INPUTEVENT_SPC_QUALIFIER_ALT },
	{ DIK_RMENU, INPUTEVENT_KEY_ALT_RIGHT, 0, INPUTEVENT_SPC_QUALIFIER_ALT },
	{ DIK_RWIN, INPUTEVENT_KEY_AMIGA_RIGHT, 0, INPUTEVENT_SPC_QUALIFIER_WIN },
	{ DIK_APPS, INPUTEVENT_KEY_AMIGA_RIGHT, 0, INPUTEVENT_SPC_QUALIFIER_WIN },
	{ DIK_RCONTROL, INPUTEVENT_KEY_CTRL, 0, INPUTEVENT_SPC_QUALIFIER_CONTROL },
	{ DIK_RSHIFT, INPUTEVENT_KEY_SHIFT_RIGHT, 0, INPUTEVENT_SPC_QUALIFIER_SHIFT },

	{ DIK_UP, INPUTEVENT_KEY_CURSOR_UP },
	{ DIK_DOWN, INPUTEVENT_KEY_CURSOR_DOWN },
	{ DIK_LEFT, INPUTEVENT_KEY_CURSOR_LEFT },
	{ DIK_RIGHT, INPUTEVENT_KEY_CURSOR_RIGHT },

	{ DIK_INSERT, INPUTEVENT_KEY_AMIGA_LEFT, 0, INPUTEVENT_SPC_PASTE, ID_FLAG_QUALIFIER_SPECIAL },
	{ DIK_DELETE, INPUTEVENT_KEY_DEL },
	{ DIK_HOME, INPUTEVENT_KEY_AMIGA_RIGHT },
	{ DIK_NEXT, INPUTEVENT_KEY_HELP },
	{ DIK_PRIOR, INPUTEVENT_SPC_FREEZEBUTTON },

	{ DIK_LBRACKET, INPUTEVENT_KEY_LEFTBRACKET },
	{ DIK_RBRACKET, INPUTEVENT_KEY_RIGHTBRACKET },
	{ DIK_SEMICOLON, INPUTEVENT_KEY_SEMICOLON },
	{ DIK_APOSTROPHE, INPUTEVENT_KEY_SINGLEQUOTE },
	{ DIK_GRAVE, INPUTEVENT_KEY_BACKQUOTE },
	{ DIK_BACKSLASH, INPUTEVENT_KEY_NUMBERSIGN },
	{ DIK_COMMA, INPUTEVENT_KEY_COMMA },
	{ DIK_PERIOD, INPUTEVENT_KEY_PERIOD },
	{ DIK_SLASH, INPUTEVENT_KEY_DIV },

	{ DIK_OEM_102, INPUTEVENT_KEY_30 },
	{ DIK_F11, INPUTEVENT_KEY_BACKSLASH },
	{ DIK_F13, INPUTEVENT_KEY_BACKSLASH },
	{ DIK_F14, INPUTEVENT_KEY_NP_LPAREN },
	{ DIK_F15, INPUTEVENT_KEY_NP_RPAREN },

	{ DIK_SYSRQ, INPUTEVENT_SPC_SCREENSHOT_CLIPBOARD, 0, INPUTEVENT_SPC_SCREENSHOT, ID_FLAG_QUALIFIER_SPECIAL },

	{ DIK_END, INPUTEVENT_SPC_QUALIFIER_SPECIAL },
	{ DIK_PAUSE, INPUTEVENT_SPC_PAUSE, 0, INPUTEVENT_SPC_SINGLESTEP, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_CONTROL, INPUTEVENT_SPC_IRQ7, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT, INPUTEVENT_SPC_WARP, ID_FLAG_QUALIFIER_SPECIAL },
	{ DIK_PAUSE+1, INPUTEVENT_SPC_SINGLESTEP, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_CONTROL, INPUTEVENT_SPC_IRQ7, ID_FLAG_QUALIFIER_SPECIAL | ID_FLAG_QUALIFIER_SHIFT },

	{ DIK_F12, INPUTEVENT_SPC_ENTERGUI, 0, INPUTEVENT_SPC_ENTERDEBUGGER, ID_FLAG_QUALIFIER_SPECIAL, INPUTEVENT_SPC_ENTERDEBUGGER, ID_FLAG_QUALIFIER_SHIFT, INPUTEVENT_SPC_TOGGLEDEFAULTSCREEN, ID_FLAG_QUALIFIER_CONTROL },

	{ DIK_MEDIASTOP, INPUTEVENT_KEY_CDTV_STOP },
	{ DIK_PLAYPAUSE, INPUTEVENT_KEY_CDTV_PLAYPAUSE },
	{ DIK_PREVTRACK, INPUTEVENT_KEY_CDTV_PREV },
	{ DIK_NEXTTRACK, INPUTEVENT_KEY_CDTV_NEXT },

	{ -1, 0 }
};

static struct uae_input_device_kbr_default keytrans_pc1[] = {

	{ DIK_ESCAPE, INPUTEVENT_KEY_ESC },

	{ DIK_F1, INPUTEVENT_KEY_F1 },
	{ DIK_F2, INPUTEVENT_KEY_F2 },
	{ DIK_F3, INPUTEVENT_KEY_F3 },
	{ DIK_F4, INPUTEVENT_KEY_F4 },
	{ DIK_F5, INPUTEVENT_KEY_F5 },
	{ DIK_F6, INPUTEVENT_KEY_F6 },
	{ DIK_F7, INPUTEVENT_KEY_F7 },
	{ DIK_F8, INPUTEVENT_KEY_F8 },
	{ DIK_F9, INPUTEVENT_KEY_F9 },
	{ DIK_F10, INPUTEVENT_KEY_F10 },
	{ DIK_F11, INPUTEVENT_KEY_F11 },
	{ DIK_F12, INPUTEVENT_KEY_F12 },

	{ DIK_1, INPUTEVENT_KEY_1 },
	{ DIK_2, INPUTEVENT_KEY_2 },
	{ DIK_3, INPUTEVENT_KEY_3 },
	{ DIK_4, INPUTEVENT_KEY_4 },
	{ DIK_5, INPUTEVENT_KEY_5 },
	{ DIK_6, INPUTEVENT_KEY_6 },
	{ DIK_7, INPUTEVENT_KEY_7 },
	{ DIK_8, INPUTEVENT_KEY_8 },
	{ DIK_9, INPUTEVENT_KEY_9 },
	{ DIK_0, INPUTEVENT_KEY_0 },

	{ DIK_TAB, INPUTEVENT_KEY_TAB },

	{ DIK_A, INPUTEVENT_KEY_A },
	{ DIK_B, INPUTEVENT_KEY_B },
	{ DIK_C, INPUTEVENT_KEY_C },
	{ DIK_D, INPUTEVENT_KEY_D },
	{ DIK_E, INPUTEVENT_KEY_E },
	{ DIK_F, INPUTEVENT_KEY_F },
	{ DIK_G, INPUTEVENT_KEY_G },
	{ DIK_H, INPUTEVENT_KEY_H },
	{ DIK_I, INPUTEVENT_KEY_I },
	{ DIK_J, INPUTEVENT_KEY_J },
	{ DIK_K, INPUTEVENT_KEY_K },
	{ DIK_L, INPUTEVENT_KEY_L },
	{ DIK_M, INPUTEVENT_KEY_M },
	{ DIK_N, INPUTEVENT_KEY_N },
	{ DIK_O, INPUTEVENT_KEY_O },
	{ DIK_P, INPUTEVENT_KEY_P },
	{ DIK_Q, INPUTEVENT_KEY_Q },
	{ DIK_R, INPUTEVENT_KEY_R },
	{ DIK_S, INPUTEVENT_KEY_S },
	{ DIK_T, INPUTEVENT_KEY_T },
	{ DIK_U, INPUTEVENT_KEY_U },
	{ DIK_W, INPUTEVENT_KEY_W },
	{ DIK_V, INPUTEVENT_KEY_V },
	{ DIK_X, INPUTEVENT_KEY_X },
	{ DIK_Y, INPUTEVENT_KEY_Y },
	{ DIK_Z, INPUTEVENT_KEY_Z },

	{ DIK_CAPITAL, INPUTEVENT_KEY_CAPS_LOCK, ID_FLAG_TOGGLE },

	{ DIK_NUMPAD1, INPUTEVENT_KEY_NP_1 },
	{ DIK_NUMPAD2, INPUTEVENT_KEY_NP_2 },
	{ DIK_NUMPAD3, INPUTEVENT_KEY_NP_3 },
	{ DIK_NUMPAD4, INPUTEVENT_KEY_NP_4 },
	{ DIK_NUMPAD5, INPUTEVENT_KEY_NP_5 },
	{ DIK_NUMPAD6, INPUTEVENT_KEY_NP_6 },
	{ DIK_NUMPAD7, INPUTEVENT_KEY_NP_7 },
	{ DIK_NUMPAD8, INPUTEVENT_KEY_NP_8 },
	{ DIK_NUMPAD9, INPUTEVENT_KEY_NP_9 },
	{ DIK_NUMPAD0, INPUTEVENT_KEY_NP_0 },
	{ DIK_DECIMAL, INPUTEVENT_KEY_NP_PERIOD },
	{ DIK_ADD, INPUTEVENT_KEY_NP_ADD },
	{ DIK_SUBTRACT, INPUTEVENT_KEY_NP_SUB },
	{ DIK_MULTIPLY, INPUTEVENT_KEY_NP_MUL },
	{ DIK_DIVIDE, INPUTEVENT_KEY_NP_DIV },
	{ DIK_NUMPADENTER, INPUTEVENT_KEY_ENTER },

	{ DIK_MINUS, INPUTEVENT_KEY_SUB },
	{ DIK_EQUALS, INPUTEVENT_KEY_EQUALS },
	{ DIK_BACK, INPUTEVENT_KEY_BACKSPACE },
	{ DIK_RETURN, INPUTEVENT_KEY_RETURN },
	{ DIK_SPACE, INPUTEVENT_KEY_SPACE },

	{ DIK_LSHIFT, INPUTEVENT_KEY_SHIFT_LEFT },
	{ DIK_LCONTROL, INPUTEVENT_KEY_CTRL },
	{ DIK_LWIN, INPUTEVENT_KEY_AMIGA_LEFT },
	{ DIK_LMENU, INPUTEVENT_KEY_ALT_LEFT },
	{ DIK_RMENU, INPUTEVENT_KEY_ALT_RIGHT },
	{ DIK_RWIN, INPUTEVENT_KEY_AMIGA_RIGHT },
	{ DIK_APPS, INPUTEVENT_KEY_APPS },
	{ DIK_RCONTROL, INPUTEVENT_KEY_CTRL },
	{ DIK_RSHIFT, INPUTEVENT_KEY_SHIFT_RIGHT },

	{ DIK_UP, INPUTEVENT_KEY_CURSOR_UP },
	{ DIK_DOWN, INPUTEVENT_KEY_CURSOR_DOWN },
	{ DIK_LEFT, INPUTEVENT_KEY_CURSOR_LEFT },
	{ DIK_RIGHT, INPUTEVENT_KEY_CURSOR_RIGHT },

	{ DIK_LBRACKET, INPUTEVENT_KEY_LEFTBRACKET },
	{ DIK_RBRACKET, INPUTEVENT_KEY_RIGHTBRACKET },
	{ DIK_SEMICOLON, INPUTEVENT_KEY_SEMICOLON },
	{ DIK_APOSTROPHE, INPUTEVENT_KEY_SINGLEQUOTE },
	{ DIK_GRAVE, INPUTEVENT_KEY_BACKQUOTE },
	{ DIK_BACKSLASH, INPUTEVENT_KEY_2B },
	{ DIK_COMMA, INPUTEVENT_KEY_COMMA },
	{ DIK_PERIOD, INPUTEVENT_KEY_PERIOD },
	{ DIK_SLASH, INPUTEVENT_KEY_DIV },
	{ DIK_OEM_102, INPUTEVENT_KEY_30 },

	{ DIK_INSERT, INPUTEVENT_KEY_INSERT },
	{ DIK_DELETE, INPUTEVENT_KEY_DEL },
	{ DIK_HOME, INPUTEVENT_KEY_HOME },
	{ DIK_END, INPUTEVENT_KEY_END },
    { DIK_PRIOR, INPUTEVENT_KEY_PAGEUP },
	{ DIK_NEXT, INPUTEVENT_KEY_PAGEDOWN },
	{ DIK_SCROLL, INPUTEVENT_KEY_HELP },
    { DIK_SYSRQ, INPUTEVENT_KEY_SYSRQ },

	{ DIK_MEDIASTOP, INPUTEVENT_KEY_CDTV_STOP },
	{ DIK_PLAYPAUSE, INPUTEVENT_KEY_CDTV_PLAYPAUSE },
	{ DIK_PREVTRACK, INPUTEVENT_KEY_CDTV_PREV },
	{ DIK_NEXTTRACK, INPUTEVENT_KEY_CDTV_NEXT },

	{ -1, 0 }
};

static struct uae_input_device_kbr_default *keytrans[] = {
	keytrans_amiga,
	keytrans_pc1,
	keytrans_pc1
};

static int kb_np[] = { DIK_NUMPAD4, -1, DIK_NUMPAD6, -1, DIK_NUMPAD8, -1, DIK_NUMPAD2, -1, DIK_NUMPAD0, DIK_NUMPAD5, -1, DIK_DECIMAL, -1, DIK_NUMPADENTER, -1, -1 };
static int kb_ck[] = { DIK_LEFT, -1, DIK_RIGHT, -1, DIK_UP, -1, DIK_DOWN, -1, DIK_RCONTROL, DIK_RMENU, -1, DIK_RSHIFT, -1, -1 };
static int kb_se[] = { DIK_A, -1, DIK_D, -1, DIK_W, -1, DIK_S, -1, DIK_LMENU, -1, DIK_LSHIFT, -1, -1 };
static int kb_np3[] = { DIK_NUMPAD4, -1, DIK_NUMPAD6, -1, DIK_NUMPAD8, -1, DIK_NUMPAD2, -1, DIK_NUMPAD0, DIK_NUMPAD5, -1, DIK_DECIMAL, -1, DIK_NUMPADENTER, -1, -1 };
static int kb_ck3[] = { DIK_LEFT, -1, DIK_RIGHT, -1, DIK_UP, -1, DIK_DOWN, -1, DIK_RCONTROL, -1, DIK_RSHIFT, -1, DIK_RMENU, -1, -1 };
static int kb_se3[] = { DIK_A, -1, DIK_D, -1, DIK_W, -1, DIK_S, -1, DIK_LMENU, -1, DIK_LSHIFT, -1, DIK_LCONTROL, -1, -1 };


static int kb_cd32_np[] = { DIK_NUMPAD4, -1, DIK_NUMPAD6, -1, DIK_NUMPAD8, -1, DIK_NUMPAD2, -1, DIK_NUMPAD0, DIK_NUMPAD5, DIK_NUMPAD1, -1, DIK_DECIMAL, DIK_NUMPAD3, -1, DIK_NUMPAD7, -1, DIK_NUMPAD9, -1, DIK_DIVIDE, -1, DIK_SUBTRACT, -1, DIK_MULTIPLY, -1, -1 };
static int kb_cd32_ck[] = { DIK_LEFT, -1, DIK_RIGHT, -1, DIK_UP, -1, DIK_DOWN, -1, DIK_RCONTROL, DIK_RMENU, DIK_RSHIFT, -1, DIK_NUMPAD7, -1, DIK_NUMPAD9, -1, DIK_DIVIDE, -1, DIK_SUBTRACT, -1, DIK_MULTIPLY, -1, -1 };
static int kb_cd32_se[] = { DIK_A, -1, DIK_D, -1, DIK_W, -1, DIK_S, -1, -1, DIK_LMENU, -1, DIK_LSHIFT, -1, DIK_NUMPAD7, -1, DIK_NUMPAD9, -1, DIK_DIVIDE, -1, DIK_SUBTRACT, -1, DIK_MULTIPLY, -1, -1 };

static int kb_cdtv[] = { DIK_NUMPAD1, -1, DIK_NUMPAD3, -1, DIK_NUMPAD7, -1, DIK_NUMPAD9, -1, -1 };

static int kb_arcadia[] = { DIK_F2, -1, DIK_1, -1, DIK_2, -1, DIK_5, -1, DIK_6, -1, -1 };

static int *kbmaps[] = {
	kb_np, kb_ck, kb_se, kb_np3, kb_ck3, kb_se3,
	kb_cd32_np, kb_cd32_ck, kb_cd32_se,
	kb_arcadia, kb_cdtv
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

static const int np[] = {
	DIK_NUMPAD0, 0, DIK_NUMPADPERIOD, 0, DIK_NUMPAD1, 1, DIK_NUMPAD2, 2,
	DIK_NUMPAD3, 3, DIK_NUMPAD4, 4, DIK_NUMPAD5, 5, DIK_NUMPAD6, 6, DIK_NUMPAD7, 7,
	DIK_NUMPAD8, 8, DIK_NUMPAD9, 9, -1 };

bool my_kbd_handler (int keyboard, int scancode, int newstate, bool alwaysrelease)
{
	int code = 0;
	int scancode_new;
	bool amode = currprefs.input_keyboard_type == 0;
	bool special = false;
	static int swapperdrive = 0;

#if 0
	if (scancode == specialkeycode ()) {
		inputdevice_checkqualifierkeycode (keyboard, scancode, newstate);
		return;
	}
#endif
#if 0
	if (scancode == DIK_F8 && key_specialpressed()) {
		if (newstate) {
			extern int blop2;
			blop2++;
		}
		return true;
	}
#endif
#if 0
	if (scancode == DIK_F9 && key_specialpressed()) {
		if (newstate) {
			extern int blop;
			blop++;
		}
		return true;
	}
#endif

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

	scancode_new = scancode;
	if (!key_specialpressed () && inputdevice_iskeymapped (keyboard, scancode))
		scancode = 0;
	
	if (newstate) {
		int defaultguikey = amode ? DIK_F12 : DIK_NUMLOCK;
		if (currprefs.win32_guikey >= 0x100) {
			if (scancode_new == DIK_F12)
				return true;
		} else if (currprefs.win32_guikey > 0) {
			if (scancode_new == defaultguikey && currprefs.win32_guikey != scancode_new) {
				scancode = 0;
				if (key_specialpressed () && key_ctrlpressed() && key_shiftpressed() && key_altpressed ())
					inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
			} else if (scancode_new == currprefs.win32_guikey ) {
				inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
				scancode = 0;
			}
		} else if (currprefs.win32_guikey != 0 && !key_specialpressed () && !key_ctrlpressed() && !key_shiftpressed() && !key_altpressed () && scancode_new == defaultguikey) {
			inputdevice_add_inputcode (AKS_ENTERGUI, 1, NULL);
			scancode = 0;
		}
	}

	//write_log (_T("keyboard = %d scancode = 0x%02x state = %d\n"), keyboard, scancode, newstate );

	if (newstate && code == 0 && amode) {

		switch (scancode)
		{
#if 0
		case DIK_F1:
		case DIK_F2:
		case DIK_F3:
		case DIK_F4:
			if (specialpressed ()) {
				if (ctrlpressed ()) {
				} else {
					if (shiftpressed ())
						code = AKS_EFLOPPY0 + (scancode - DIK_F1);
					else
						code = AKS_FLOPPY0 + (scancode - DIK_F1);
				}
			}
			special = true;
			break;
		case DIK_F5:
#if 0
			{
				disk_prevnext (0, -1);
				return;
				//crap++;
				//write_log (_T("%d\n"), crap);
			}
#endif
			if (specialpressed ()) {
				if (shiftpressed ())
					code = AKS_STATESAVEDIALOG;
				else
					code = AKS_STATERESTOREDIALOG;
			}
			special = true;
			break;
#endif

		case DIK_1:
		case DIK_2:
		case DIK_3:
		case DIK_4:
		case DIK_5:
		case DIK_6:
		case DIK_7:
		case DIK_8:
		case DIK_9:
		case DIK_0:
			if (key_specialpressed ()) {
				int num = scancode - DIK_1;
				if (key_shiftpressed ())
					num += 10;
				if (key_ctrlpressed ()) {
					swapperdrive = num;
					if (swapperdrive > 3)
						swapperdrive = 0;
				} else {
					int i;
					for (i = 0; i < 4; i++) {
						if (!_tcscmp (currprefs.floppyslots[i].df, currprefs.dfxlist[num]))
							changed_prefs.floppyslots[i].df[0] = 0;
					}
					_tcscpy (changed_prefs.floppyslots[swapperdrive].df, currprefs.dfxlist[num]);
					set_config_changed ();
				}
				special = true;
			}
			break;
		case DIK_NUMPAD0:
		case DIK_NUMPAD1:
		case DIK_NUMPAD2:
		case DIK_NUMPAD3:
		case DIK_NUMPAD4:
		case DIK_NUMPAD5:
		case DIK_NUMPAD6:
		case DIK_NUMPAD7:
		case DIK_NUMPAD8:
		case DIK_NUMPAD9:
		case DIK_NUMPADPERIOD:
			if (key_specialpressed ()) {
				int i = 0, v = -1;
				while (np[i] >= 0) {
					v = np[i + 1];
					if (np[i] == scancode)
						break;
					i += 2;
				}
				if (v >= 0)
					code = AKS_STATESAVEQUICK + v * 2 + ((key_shiftpressed () || key_ctrlpressed ()) ? 0 : 1);
				special = true;
			}
			break;
		}
	}

	if (code) {
		inputdevice_add_inputcode (code, 1, NULL);
		return true;
	}


	scancode = scancode_new;
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

	if (special) {
		inputdevice_checkqualifierkeycode (keyboard, scancode, newstate);
		return true;
	}

	return inputdevice_translatekeycode (keyboard, scancode, newstate, alwaysrelease) != 0;
}

void keyboard_settrans (void)
{
	inputdevice_setkeytranslation (keytrans, kbmaps);
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
