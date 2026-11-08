#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "keyboard.h"
#include "keymap.h"
#include "input_defaults.h"

uae_prefs currprefs, changed_prefs;
static bool special, shift, control, alt, mapped;
static int action, qualifier, changed, spare;
bool key_specialpressed() { return special; }
bool key_shiftpressed() { return shift; }
bool key_ctrlpressed() { return control; }
bool key_altpressed() { return alt; }
int inputdevice_iskeymapped(int, int) { return mapped; }
void inputdevice_add_inputcode(int code, int, const TCHAR *) { action = code; }
void inputdevice_checkqualifierkeycode(int, int, int) { qualifier++; }
void set_config_changed(int) { changed++; }
void inputdevice_sparecopy(uae_input_device *, int, int) { spare++; }
static bool button(void *, int n) { return n < 7; }
static bool hat(void *, int n) { return n == 2; }
#define CHECK(x)                                                                                   \
    do {                                                                                           \
	if (!(x)) {                                                                                \
	    fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
	    return 1;                                                                              \
	}                                                                                          \
    } while (0)

int main()
{
    static host_keymap keys;
    int native[HOST_KEY_COUNT];
    for (int i = 0; i < HOST_KEY_COUNT; i++)
	native[i] = i * 3 + 7;
    native[HOST_KEY_NONUSHASH] = -1;
    host_keymap_init(&keys, native, false);
    for (int mode = 0; mode < 2; mode++) {
	bool pause = false;
	for (auto *entry = keys.table[mode]; entry->scancode >= 0; entry++) {
	    int k = host_key_from_native(native, entry->scancode);
	    CHECK(k != HOST_KEY_NONE);
	    if (k == HOST_KEY_PAUSE)
		pause = true;
	    if (k == HOST_KEY_F1)
		CHECK(entry->node[0].evt == INPUTEVENT_KEY_F1);
	}
	CHECK(pause == (mode == 0));
    }
    host_keymap_init(&keys, native, true);
    CHECK(keys.translations[1] == keys.translations[2]);
    CHECK(keys.mapping[0][0] == native[HOST_KEY_NUMPAD4]);
    CHECK(keys.mapping[9][0] == native[HOST_KEY_F2]);
    CHECK(keys.mapping[10][0] == native[HOST_KEY_NUMPAD1]);
    static uae_input_device uid;
    host_input_device device = {NULL, button, hat, 4, true, false};
    input_default_joystick(&uid, 0, 0, JPORT_AF_TOGGLE, JSEM_MODE_JOYSTICK_CD32, true, false, false,
			   device);
    CHECK(uid.eventid[ID_AXIS_OFFSET + 2][0] == INPUTEVENT_JOY1_HORIZ);
    CHECK(uid.eventid[ID_AXIS_OFFSET + 3][0] == INPUTEVENT_JOY1_VERT);
    CHECK(uid.eventid[ID_BUTTON_OFFSET][0] == INPUTEVENT_JOY1_CD32_RED);
    CHECK(uid.eventid[ID_BUTTON_OFFSET + 6][0] == INPUTEVENT_JOY1_CD32_PLAY);
    CHECK((uid.flags[ID_BUTTON_OFFSET][0] & ID_FLAG_AUTOFIRE_MASK) ==
	  (ID_FLAG_AUTOFIRE | ID_FLAG_TOGGLE));
    CHECK(uid.port[ID_BUTTON_OFFSET][0] == 1 && spare > 0);
    uid.eventid[ID_BUTTON_OFFSET + 3][0] = INPUTEVENT_KEY_A;
    input_default_joystick(&uid, 0, 0, 0, JSEM_MODE_JOYSTICK, false, false, true, device);
    CHECK(uid.eventid[ID_BUTTON_OFFSET + 3][0] == INPUTEVENT_KEY_A);
    device.replace_osk = true;
    input_default_joystick(&uid, 0, 0, 0, JSEM_MODE_JOYSTICK, false, false, true, device);
    CHECK(uid.eventid[ID_BUTTON_OFFSET + 3][0] == INPUTEVENT_SPC_OSK);
    currprefs.input_advancedmultiinput = true;
    for (int i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	uid.eventid[ID_BUTTON_OFFSET][i] = INPUTEVENT_KEY_A;
    input_default_mouse(&uid, 0, 1, 0, true, true, false, device);
    for (int i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	CHECK(uid.eventid[ID_BUTTON_OFFSET][i] == INPUTEVENT_KEY_A);
    currprefs.input_keyboard_type = 0;
    special = true;
    CHECK(host_keyboard_hotkey(0, 42, HOST_KEY_NUMPAD3, 88, 88, 1));
    CHECK(action == AKS_STATESAVEQUICK + 7);
    shift = true;
    CHECK(host_keyboard_hotkey(0, 42, HOST_KEY_NUMPAD3, 88, 88, 1));
    CHECK(action == AKS_STATESAVEQUICK + 6);
    shift = false;
    control = true;
    CHECK(host_keyboard_hotkey(0, 42, HOST_KEY_2, 88, 88, 1));
    control = false;
    _tcscpy(currprefs.dfxlist[0], _T("disk.adf"));
    CHECK(host_keyboard_hotkey(0, 42, HOST_KEY_1, 88, 88, 1));
    CHECK(!_tcscmp(changed_prefs.floppyslots[1].df, _T("disk.adf")) && changed == 1);
    CHECK(!host_keyboard_hotkey(0, 42, HOST_KEY_1, 88, 88, 0));
    special = false;
    mapped = true;
    action = 0;
    CHECK(!host_keyboard_hotkey(0, 42, HOST_KEY_NUMPAD3, 88, 88, 1) && action == 0);
    currprefs.host_guikey = -1;
    CHECK(!host_keyboard_hotkey(0, 88, HOST_KEY_F12, 88, 88, 1));
    CHECK(action == AKS_ENTERGUI);
    currprefs.host_guikey = 0x100;
    CHECK(host_keyboard_hotkey(0, 88, HOST_KEY_F12, 88, 88, 1));
    return 0;
}
