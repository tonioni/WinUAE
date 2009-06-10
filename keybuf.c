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

static int fakestate[MAX_JPORTS][7] = { {0},{0} };

static int *fs_np, *fs_ck, *fs_se;
#ifdef ARCADIA
static int *fs_xa1, *fs_xa2;
#endif

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
    return key;
}

static void do_fake (int nr)
{
    int *fake = fakestate[nr];
    do_fake_joystick (nr, fake);
}

int record_key (int kc)
{
    if (input_recording < 0 || pause_emulation)
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

    if (fs_np != 0) {
	switch (k) {
	case AK_NP8: fs = 1; fs_np[0] = b; break;
	case AK_NP4: fs = 1; fs_np[1] = b; break;
	case AK_NP6: fs = 1; fs_np[2] = b; break;
	case AK_NP2: fs = 1; fs_np[3] = b; break;
	case AK_NP0: case AK_NP5: fs = 1; fs_np[4] = b; break;
	case AK_NPDEL: case AK_NPDIV: case AK_ENT: fs = 1; fs_np[5] = b; break;
	}
    }
    if (fs_ck != 0) {
	switch (k) {
	case AK_UP: fs = 1; fs_ck[0] = b; break;
	case AK_LF: fs = 1; fs_ck[1] = b; break;
	case AK_RT: fs = 1; fs_ck[2] = b; break;
	case AK_DN: fs = 1; fs_ck[3] = b; break;
	case AK_RCTRL: case AK_RALT: fs = 1; fs_ck[4] = b; break;
	case AK_RSH: fs = 1; fs_ck[5] = b; break;
	}
    }
    if (fs_se != 0) {
	switch (k) {
	case AK_W: fs = 1; fs_se[0] = b; break;
	case AK_A: fs = 1; fs_se[1] = b; break;
	case AK_D: fs = 1; fs_se[2] = b; break;
	case AK_S: fs = 1; fs_se[3] = b; break;
	case AK_LALT: fs = 1; fs_se[4] = b; break;
	case AK_LSH: fs = 1; fs_se[5] = b; break;
	}
    }
#ifdef ARCADIA
    if (fs_xa1 != 0) {
	switch (k) {
	case AK_NP8: fs = 1; fs_xa1[0] = b; break;
	case AK_NP4: fs = 1; fs_xa1[1] = b; break;
	case AK_NP6: fs = 1; fs_xa1[2] = b; break;
	case AK_NP2: case AK_NP5: fs = 1; fs_xa1[3] = b; break;
	case AK_CTRL: fs = 1; fs_xa1[4] = b; break;
	case AK_LALT: fs = 1; fs_xa1[5] = b; break;
	case AK_SPC: fs = 1; fs_xa1[6] = b; break;
	}
    }
    if (fs_xa2 != 0) {
	switch (k) {
	case AK_R: fs = 1; fs_xa2[0] = b; break;
	case AK_D: fs = 1; fs_xa2[1] = b; break;
	case AK_G: fs = 1; fs_xa2[2] = b; break;
	case AK_F: fs = 1; fs_xa2[3] = b; break;
	case AK_A: fs = 1; fs_xa2[4] = b; break;
	case AK_S: fs = 1; fs_xa2[5] = b; break;
	case AK_Q: fs = 1; fs_xa2[6] = b; break;
	}
    }
#endif
    if (fs && currprefs.input_selected_setting == 0) {
	if (JSEM_ISANYKBD (0, &currprefs))
	    do_fake (0);
	if (JSEM_ISANYKBD (1, &currprefs))
	    do_fake (1);
	if (JSEM_ISANYKBD (2, &currprefs))
	    do_fake (2);
	if (JSEM_ISANYKBD (3, &currprefs))
	    do_fake (3);
	return 0;
    } else {
	if ((kc >> 1) == AK_RCTRL) {
	    kc ^= AK_RCTRL << 1;
	    kc ^= AK_CTRL << 1;
	}
#ifdef ARCADIA
	if (fs_xa1 || fs_xa2) {
	    int k2 = k;
	    if (k == AK_1)
		k2 = AK_F1;
	    if (k == AK_2)
		k2 = AK_F2;
	    if (k == AK_3)
		k2 = AK_LALT;
	    if (k == AK_4)
		k2 = AK_RALT;
	    if (k == AK_6)
		k2 = AK_DN;
	    if (k == AK_LBRACKET || k == AK_LSH)
		k2 = AK_SPC;
	    if (k == AK_RBRACKET)
		k2 = AK_RET;
	    if (k == AK_C)
		k2 = AK_1;
	    if (k == AK_5)
		k2 = AK_2;
	    if (k == AK_Z)
		k2 = AK_3;
	    if (k == AK_X)
		k2 = AK_4;
	    if (k != k2)
		kc = (k2 << 1) | (b ? 0 : 1);
	}
#endif
    }

    if (input_recording > 0) {
	inprec_rstart(INPREC_KEY);
	inprec_ru8(kc);
	inprec_rend();
    }

    keybuf[kpb_first] = kc;
    kpb_first = kpb_next;
    return 1;
}

void joystick_setting_changed (void)
{
    int i;

    fs_np = fs_ck = fs_se = 0;
#ifdef ARCADIA
    fs_xa1 = fs_xa2 = 0;
#endif

    for (i = 0; i < MAX_JPORTS; i++) {
	if (JSEM_ISNUMPAD (i, &currprefs))
	    fs_np = fakestate[i];
        if (JSEM_ISCURSOR (i, &currprefs))
	    fs_ck = fakestate[i];
	if (JSEM_ISSOMEWHEREELSE (i, &currprefs))
	    fs_se = fakestate[i];
#ifdef ARCADIA
	if (JSEM_ISXARCADE1 (i, &currprefs))
	    fs_xa1 = fakestate[i];
	if (JSEM_ISXARCADE2 (i, &currprefs))
	    fs_xa2 = fakestate[i];
#endif
    }
}

void keybuf_init (void)
{
    kpb_first = kpb_last = 0;
    inputdevice_updateconfig (&currprefs);
}

#ifdef SAVESTATE

uae_u8 *save_keyboard (int *len)
{
    uae_u8 *dst, *t;
    dst = t = (uae_u8*)malloc (8);
    save_u32 (getcapslockstate() ? 1 : 0);
    save_u32 (0);
    *len = 8;
    return t;
}

uae_u8 *restore_keyboard (uae_u8 *src)
{
    setcapslockstate (restore_u32 ());
    restore_u32 ();
    return src;
}

#endif /* SAVESTATE */
