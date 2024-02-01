#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"

static struct uae_input_device_kbr_default keytrans_amiga[] = {
	{ INPUTEVENT_KEY_CAPS_LOCK, { { INPUTEVENT_KEY_CAPS_LOCK, ID_FLAG_TOGGLE } } },
	{ -1, { { 0 } } }
};

static struct uae_input_device_kbr_default *keytrans[] = {
	keytrans_amiga,
	keytrans_amiga,
	keytrans_amiga,
};

static int kb_none[] = { -1 };

static int* kbmaps[] = {
	kb_none,
	kb_none,
	kb_none,
	kb_none,
	kb_none,
	kb_none,
	kb_none,
	kb_none,
	kb_none,
	kb_none
};

void keyboard_settrans() {
	inputdevice_setkeytranslation(keytrans, kbmaps);
}

