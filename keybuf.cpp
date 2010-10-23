/*
* UAE - The Un*x Amiga Emulator
*
* Keyboard buffer. Not really needed for X, but for SVGAlib and possibly
* Mac and DOS ports.
*
* Note: it's possible to have two threads in UAE, one reading keystrokes
* and the other one writing them. Despite this, no synchronization effort
* is needed. This code should be perfectly thread safe. At least if you
* assume that integer store instructions are atomic.
*
* Copyright 1995, 1997 Bernd Schmidt
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include <assert.h>

#include "options.h"
#include "keybuf.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "custom.h"
#include "savestate.h"

static int kpb_first, kpb_last;

static int keybuf[256];

int keys_available (void)
{
	int val;
	val = kpb_first != kpb_last;
	return val;
}

int get_next_key (void)
{
	int key;
	assert (kpb_first != kpb_last);

	key = keybuf[kpb_last];
	if (++kpb_last == 256)
		kpb_last = 0;
	//write_log (L"%02x:%d\n", key >> 1, key & 1);
	return key;
}

int record_key (int kc)
{
	if (pause_emulation)
		return 0;
	return record_key_direct (kc);
}

int record_key_direct (int kc)
{
	int fs = 0;
	int kpb_next = kpb_first + 1;
	int k = kc >> 1;
	int b = !(kc & 1);

	//write_log (L"got kc %02X\n", ((kc << 7) | (kc >> 1)) & 0xff);
	if (kpb_next == 256)
		kpb_next = 0;
	if (kpb_next == kpb_last) {
		write_log (L"Keyboard buffer overrun. Congratulations.\n");
		return 0;
	}

	if ((kc >> 1) == AK_RCTRL) {
		kc ^= AK_RCTRL << 1;
		kc ^= AK_CTRL << 1;
	}

	keybuf[kpb_first] = kc;
	kpb_first = kpb_next;
	return 1;
}

void keybuf_init (void)
{
	kpb_first = kpb_last = 0;
	inputdevice_updateconfig (&currprefs);
}
