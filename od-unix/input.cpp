#include "sysconfig.h"
#include "sysdeps.h"

#ifdef UAE_UNIX_WITH_SDL3
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#include "options.h"
#include "traps.h"
#include "gui.h"
#include "inputdevice.h"
#include "od-generic/keymap.h"
#include "od-generic/input_defaults.h"
#include "input.h"

int pause_emulation;
int tablet_log;
int key_swap_hack;

static TCHAR empty_friendly[] = _T("Unix placeholder");
static TCHAR empty_unique_name[] = _T("unix.placeholder");
static TCHAR mouse_friendly[] = _T("Unix Mouse");
static TCHAR mouse_unique_name[] = _T("unix.mouse");
static TCHAR mouse_axis_names[][16] = {
    _T("X Axis"),
    _T("Y Axis"),
    _T("Wheel")
};
static TCHAR mouse_button_names[][16] = {
    _T("Button 1"),
    _T("Button 2"),
    _T("Button 3")
};
static TCHAR keyboard_friendly[] = _T("Unix Keyboard");
static TCHAR keyboard_unique_name[] = _T("unix.keyboard");
static bool mouse_active;
static bool keyboard_state[512];
static int capslockstate;
static int host_capslockstate;
static int host_numlockstate;
static int host_scrolllockstate;

#ifdef UAE_UNIX_WITH_SDL3
enum {
    UNIX_AXIS_SDL,
    UNIX_AXIS_GAMEPAD_DPAD_X,
    UNIX_AXIS_GAMEPAD_DPAD_Y,
    UNIX_AXIS_HAT_X,
    UNIX_AXIS_HAT_Y
};

struct unix_joystick_device {
    SDL_JoystickID instance_id;
    SDL_Gamepad *gamepad;
    SDL_Joystick *joystick;
    bool is_gamepad;
    TCHAR friendly[128];
    TCHAR unique[160];
    int axis_count;
    int button_count;
    int axis_kind[ID_AXIS_TOTAL];
    int axis_code[ID_AXIS_TOTAL];
    int button_code[ID_BUTTON_TOTAL];
    int axis_state[ID_AXIS_TOTAL];
    bool button_state[ID_BUTTON_TOTAL];
};

static unix_joystick_device unix_joysticks[MAX_INPUT_DEVICES];
static int unix_joystick_count;
static bool unix_joystick_sdl_initialized;

static const SDL_GamepadAxis unix_gamepad_axes[] = {
    SDL_GAMEPAD_AXIS_LEFTX,
    SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
};

static const TCHAR *const unix_gamepad_axis_names[] = {
    _T("Left X Axis"),
    _T("Left Y Axis"),
    _T("Right X Axis"),
    _T("Right Y Axis"),
    _T("Left Trigger"),
    _T("Right Trigger")
};

static const SDL_GamepadButton unix_gamepad_buttons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_MISC1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
    SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
    SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
    SDL_GAMEPAD_BUTTON_TOUCHPAD,
    SDL_GAMEPAD_BUTTON_MISC2,
    SDL_GAMEPAD_BUTTON_MISC3,
    SDL_GAMEPAD_BUTTON_MISC4,
    SDL_GAMEPAD_BUTTON_MISC5,
    SDL_GAMEPAD_BUTTON_MISC6
};

static const TCHAR *const unix_gamepad_button_names[] = {
    _T("South Button"),
    _T("East Button"),
    _T("West Button"),
    _T("North Button"),
    _T("Left Shoulder"),
    _T("Right Shoulder"),
    _T("Start Button"),
    _T("Back Button"),
    _T("Left Stick Button"),
    _T("Right Stick Button"),
    _T("Guide Button"),
    _T("Misc Button 1"),
    _T("Right Paddle 1"),
    _T("Left Paddle 1"),
    _T("Right Paddle 2"),
    _T("Left Paddle 2"),
    _T("Touchpad Button"),
    _T("Misc Button 2"),
    _T("Misc Button 3"),
    _T("Misc Button 4"),
    _T("Misc Button 5"),
    _T("Misc Button 6")
};
#endif

enum {
    UKEY_A = 4,
    UKEY_B = 5,
    UKEY_C = 6,
    UKEY_D = 7,
    UKEY_E = 8,
    UKEY_F = 9,
    UKEY_G = 10,
    UKEY_H = 11,
    UKEY_I = 12,
    UKEY_J = 13,
    UKEY_K = 14,
    UKEY_L = 15,
    UKEY_M = 16,
    UKEY_N = 17,
    UKEY_O = 18,
    UKEY_P = 19,
    UKEY_Q = 20,
    UKEY_R = 21,
    UKEY_S = 22,
    UKEY_T = 23,
    UKEY_U = 24,
    UKEY_V = 25,
    UKEY_W = 26,
    UKEY_X = 27,
    UKEY_Y = 28,
    UKEY_Z = 29,
    UKEY_1 = 30,
    UKEY_2 = 31,
    UKEY_3 = 32,
    UKEY_4 = 33,
    UKEY_5 = 34,
    UKEY_6 = 35,
    UKEY_7 = 36,
    UKEY_8 = 37,
    UKEY_9 = 38,
    UKEY_0 = 39,
    UKEY_RETURN = 40,
    UKEY_ESCAPE = 41,
    UKEY_BACKSPACE = 42,
    UKEY_TAB = 43,
    UKEY_SPACE = 44,
    UKEY_MINUS = 45,
    UKEY_EQUALS = 46,
    UKEY_LEFTBRACKET = 47,
    UKEY_RIGHTBRACKET = 48,
    UKEY_BACKSLASH = 49,
    UKEY_NONUSHASH = 50,
    UKEY_SEMICOLON = 51,
    UKEY_APOSTROPHE = 52,
    UKEY_GRAVE = 53,
    UKEY_COMMA = 54,
    UKEY_PERIOD = 55,
    UKEY_SLASH = 56,
    UKEY_CAPSLOCK = 57,
    UKEY_F1 = 58,
    UKEY_F2 = 59,
    UKEY_F3 = 60,
    UKEY_F4 = 61,
    UKEY_F5 = 62,
    UKEY_F6 = 63,
    UKEY_F7 = 64,
    UKEY_F8 = 65,
    UKEY_F9 = 66,
    UKEY_F10 = 67,
    UKEY_F11 = 68,
    UKEY_F12 = 69,
    UKEY_PRINTSCREEN = 70,
    UKEY_SCROLLLOCK = 71,
    UKEY_PAUSE = 72,
    UKEY_INSERT = 73,
    UKEY_HOME = 74,
    UKEY_PAGEUP = 75,
    UKEY_DELETE = 76,
    UKEY_END = 77,
    UKEY_PAGEDOWN = 78,
    UKEY_RIGHT = 79,
    UKEY_LEFT = 80,
    UKEY_DOWN = 81,
    UKEY_UP = 82,
    UKEY_NUMLOCKCLEAR = 83,
    UKEY_KP_DIVIDE = 84,
    UKEY_KP_MULTIPLY = 85,
    UKEY_KP_MINUS = 86,
    UKEY_KP_PLUS = 87,
    UKEY_KP_ENTER = 88,
    UKEY_KP_1 = 89,
    UKEY_KP_2 = 90,
    UKEY_KP_3 = 91,
    UKEY_KP_4 = 92,
    UKEY_KP_5 = 93,
    UKEY_KP_6 = 94,
    UKEY_KP_7 = 95,
    UKEY_KP_8 = 96,
    UKEY_KP_9 = 97,
    UKEY_KP_0 = 98,
    UKEY_KP_PERIOD = 99,
    UKEY_NONUSBACKSLASH = 100,
    UKEY_APPLICATION = 101,
    UKEY_F13 = 104,
    UKEY_F14 = 105,
    UKEY_F15 = 106,
    UKEY_AUDIOSTOP = 260,
    UKEY_AUDIOPLAY = 261,
    UKEY_AUDIOPREV = 259,
    UKEY_AUDIONEXT = 258,
    UKEY_LCTRL = 224,
    UKEY_LSHIFT = 225,
    UKEY_LALT = 226,
    UKEY_LGUI = 227,
    UKEY_RCTRL = 228,
    UKEY_RSHIFT = 229,
    UKEY_RALT = 230,
    UKEY_RGUI = 231
};

static host_keymap keymap;
static const int native_keys[HOST_KEY_COUNT] = {
 -1, // HOST_KEY_NONE
 UKEY_ESCAPE, // HOST_KEY_ESCAPE
 UKEY_F1, // HOST_KEY_F1
 UKEY_F2, // HOST_KEY_F2
 UKEY_F3, // HOST_KEY_F3
 UKEY_F4, // HOST_KEY_F4
 UKEY_F5, // HOST_KEY_F5
 UKEY_F6, // HOST_KEY_F6
 UKEY_F7, // HOST_KEY_F7
 UKEY_F8, // HOST_KEY_F8
 UKEY_F9, // HOST_KEY_F9
 UKEY_F10, // HOST_KEY_F10
 UKEY_1, // HOST_KEY_1
 UKEY_2, // HOST_KEY_2
 UKEY_3, // HOST_KEY_3
 UKEY_4, // HOST_KEY_4
 UKEY_5, // HOST_KEY_5
 UKEY_6, // HOST_KEY_6
 UKEY_7, // HOST_KEY_7
 UKEY_8, // HOST_KEY_8
 UKEY_9, // HOST_KEY_9
 UKEY_0, // HOST_KEY_0
 UKEY_TAB, // HOST_KEY_TAB
 UKEY_A, // HOST_KEY_A
 UKEY_B, // HOST_KEY_B
 UKEY_C, // HOST_KEY_C
 UKEY_D, // HOST_KEY_D
 UKEY_E, // HOST_KEY_E
 UKEY_F, // HOST_KEY_F
 UKEY_G, // HOST_KEY_G
 UKEY_H, // HOST_KEY_H
 UKEY_I, // HOST_KEY_I
 UKEY_J, // HOST_KEY_J
 UKEY_K, // HOST_KEY_K
 UKEY_L, // HOST_KEY_L
 UKEY_M, // HOST_KEY_M
 UKEY_N, // HOST_KEY_N
 UKEY_O, // HOST_KEY_O
 UKEY_P, // HOST_KEY_P
 UKEY_Q, // HOST_KEY_Q
 UKEY_R, // HOST_KEY_R
 UKEY_S, // HOST_KEY_S
 UKEY_T, // HOST_KEY_T
 UKEY_U, // HOST_KEY_U
 UKEY_W, // HOST_KEY_W
 UKEY_V, // HOST_KEY_V
 UKEY_X, // HOST_KEY_X
 UKEY_Y, // HOST_KEY_Y
 UKEY_Z, // HOST_KEY_Z
 UKEY_CAPSLOCK, // HOST_KEY_CAPITAL
 UKEY_KP_1, // HOST_KEY_NUMPAD1
 UKEY_KP_2, // HOST_KEY_NUMPAD2
 UKEY_KP_3, // HOST_KEY_NUMPAD3
 UKEY_KP_4, // HOST_KEY_NUMPAD4
 UKEY_KP_5, // HOST_KEY_NUMPAD5
 UKEY_KP_6, // HOST_KEY_NUMPAD6
 UKEY_KP_7, // HOST_KEY_NUMPAD7
 UKEY_KP_8, // HOST_KEY_NUMPAD8
 UKEY_KP_9, // HOST_KEY_NUMPAD9
 UKEY_KP_0, // HOST_KEY_NUMPAD0
 UKEY_KP_PERIOD, // HOST_KEY_DECIMAL
 UKEY_KP_PLUS, // HOST_KEY_ADD
 UKEY_KP_MINUS, // HOST_KEY_SUBTRACT
 UKEY_KP_MULTIPLY, // HOST_KEY_MULTIPLY
 UKEY_KP_DIVIDE, // HOST_KEY_DIVIDE
 UKEY_KP_ENTER, // HOST_KEY_NUMPADENTER
 UKEY_MINUS, // HOST_KEY_MINUS
 UKEY_EQUALS, // HOST_KEY_EQUALS
 UKEY_BACKSPACE, // HOST_KEY_BACK
 UKEY_RETURN, // HOST_KEY_RETURN
 UKEY_SPACE, // HOST_KEY_SPACE
 UKEY_LSHIFT, // HOST_KEY_LSHIFT
 UKEY_LCTRL, // HOST_KEY_LCONTROL
 UKEY_LGUI, // HOST_KEY_LWIN
 UKEY_LALT, // HOST_KEY_LMENU
 UKEY_RALT, // HOST_KEY_RMENU
 UKEY_RGUI, // HOST_KEY_RWIN
 UKEY_APPLICATION, // HOST_KEY_APPS
 UKEY_RCTRL, // HOST_KEY_RCONTROL
 UKEY_RSHIFT, // HOST_KEY_RSHIFT
 UKEY_UP, // HOST_KEY_UP
 UKEY_DOWN, // HOST_KEY_DOWN
 UKEY_LEFT, // HOST_KEY_LEFT
 UKEY_RIGHT, // HOST_KEY_RIGHT
 UKEY_INSERT, // HOST_KEY_INSERT
 UKEY_DELETE, // HOST_KEY_DELETE
 UKEY_HOME, // HOST_KEY_HOME
 UKEY_PAGEDOWN, // HOST_KEY_NEXT
 UKEY_PAGEUP, // HOST_KEY_PRIOR
 UKEY_LEFTBRACKET, // HOST_KEY_LBRACKET
 UKEY_RIGHTBRACKET, // HOST_KEY_RBRACKET
 UKEY_SEMICOLON, // HOST_KEY_SEMICOLON
 UKEY_APOSTROPHE, // HOST_KEY_APOSTROPHE
 UKEY_GRAVE, // HOST_KEY_GRAVE
 UKEY_BACKSLASH, // HOST_KEY_BACKSLASH
 UKEY_COMMA, // HOST_KEY_COMMA
 UKEY_PERIOD, // HOST_KEY_PERIOD
 UKEY_SLASH, // HOST_KEY_SLASH
 UKEY_NONUSBACKSLASH, // HOST_KEY_OEM_102
 UKEY_F11, // HOST_KEY_F11
 UKEY_F13, // HOST_KEY_F13
 UKEY_F14, // HOST_KEY_F14
 UKEY_F15, // HOST_KEY_F15
 UKEY_PRINTSCREEN, // HOST_KEY_SYSRQ
 UKEY_END, // HOST_KEY_END
 UKEY_PAUSE, // HOST_KEY_PAUSE
 -1, // HOST_KEY_PAUSE_RELEASE
 UKEY_F12, // HOST_KEY_F12
 UKEY_AUDIOSTOP, // HOST_KEY_MEDIASTOP
 UKEY_AUDIOPLAY, // HOST_KEY_PLAYPAUSE
 UKEY_AUDIOPREV, // HOST_KEY_PREVTRACK
 UKEY_AUDIONEXT, // HOST_KEY_NEXTTRACK
 UKEY_SCROLLLOCK, // HOST_KEY_SCROLL
 UKEY_NUMLOCKCLEAR, // HOST_KEY_NUMLOCK
 UKEY_NONUSHASH, // HOST_KEY_NONUSHASH
};

static const int keyboard_keycodes[] = {
    UKEY_ESCAPE,
    UKEY_F1, UKEY_F2, UKEY_F3, UKEY_F4, UKEY_F5, UKEY_F6, UKEY_F7, UKEY_F8, UKEY_F9, UKEY_F10, UKEY_F11, UKEY_F12,
    UKEY_1, UKEY_2, UKEY_3, UKEY_4, UKEY_5, UKEY_6, UKEY_7, UKEY_8, UKEY_9, UKEY_0,
    UKEY_TAB,
    UKEY_A, UKEY_B, UKEY_C, UKEY_D, UKEY_E, UKEY_F, UKEY_G, UKEY_H, UKEY_I, UKEY_J, UKEY_K, UKEY_L, UKEY_M,
    UKEY_N, UKEY_O, UKEY_P, UKEY_Q, UKEY_R, UKEY_S, UKEY_T, UKEY_U, UKEY_V, UKEY_W, UKEY_X, UKEY_Y, UKEY_Z,
    UKEY_CAPSLOCK,
    UKEY_KP_1, UKEY_KP_2, UKEY_KP_3, UKEY_KP_4, UKEY_KP_5, UKEY_KP_6, UKEY_KP_7, UKEY_KP_8, UKEY_KP_9, UKEY_KP_0,
    UKEY_KP_PERIOD, UKEY_KP_PLUS, UKEY_KP_MINUS, UKEY_KP_MULTIPLY, UKEY_KP_DIVIDE, UKEY_KP_ENTER,
    UKEY_MINUS, UKEY_EQUALS, UKEY_BACKSPACE, UKEY_RETURN, UKEY_SPACE,
    UKEY_LSHIFT, UKEY_LCTRL, UKEY_LGUI, UKEY_LALT, UKEY_RALT, UKEY_RGUI, UKEY_APPLICATION, UKEY_RCTRL, UKEY_RSHIFT,
    UKEY_UP, UKEY_DOWN, UKEY_LEFT, UKEY_RIGHT,
    UKEY_INSERT, UKEY_DELETE, UKEY_HOME, UKEY_END, UKEY_PAGEUP, UKEY_PAGEDOWN, UKEY_SCROLLLOCK, UKEY_PRINTSCREEN, UKEY_PAUSE,
    UKEY_LEFTBRACKET, UKEY_RIGHTBRACKET, UKEY_SEMICOLON, UKEY_APOSTROPHE, UKEY_GRAVE, UKEY_BACKSLASH, UKEY_NONUSHASH, UKEY_NONUSBACKSLASH,
    UKEY_COMMA, UKEY_PERIOD, UKEY_SLASH,
    UKEY_F13, UKEY_F14, UKEY_F15,
    UKEY_AUDIOSTOP, UKEY_AUDIOPLAY, UKEY_AUDIOPREV, UKEY_AUDIONEXT
};

static int input_init(void)
{
    host_keymap_init(&keymap, native_keys, true);
    inputdevice_setkeytranslation(keymap.translations, keymap.mappings);
    return 1;
}
static void input_close(void) {}
static int input_acquire(int, int) { return 1; }
static void input_unacquire(int) {}
static void input_read(void) {}
static int empty_get_num(void) { return 0; }
static TCHAR *empty_get_friendlyname(int) { return empty_friendly; }
static TCHAR *empty_get_uniquename(int) { return empty_unique_name; }
static int empty_get_widget_num(int) { return 0; }
static int empty_get_widget_type(int, int, TCHAR *, uae_u32 *) { return IDEV_WIDGET_NONE; }
static int empty_get_widget_first(int, int) { return -1; }
static int empty_get_flags(int) { return 0; }

static int mouse_get_num(void) { return 1; }
static TCHAR *mouse_get_friendlyname(int) { return mouse_friendly; }
static TCHAR *mouse_get_uniquename(int) { return mouse_unique_name; }
static int mouse_get_widget_num(int) { return 6; }
static int mouse_get_widget_type(int, int widget, TCHAR *name, uae_u32 *code)
{
    if (code) {
        *code = widget;
    }
    if (widget >= 0 && widget < 3) {
        if (name) {
            _tcscpy(name, mouse_axis_names[widget]);
        }
        return IDEV_WIDGET_AXIS;
    }
    if (widget >= 3 && widget < 6) {
        if (name) {
            _tcscpy(name, mouse_button_names[widget - 3]);
        }
        return IDEV_WIDGET_BUTTON;
    }
    return IDEV_WIDGET_NONE;
}
static int mouse_get_widget_first(int, int type)
{
    if (type == IDEV_WIDGET_AXIS) {
        return 0;
    }
    if (type == IDEV_WIDGET_BUTTON) {
        return 3;
    }
    return -1;
}
static int mouse_get_flags(int) { return 0; }

static int keyboard_get_num(void) { return 1; }
static TCHAR *keyboard_get_friendlyname(int) { return keyboard_friendly; }
static TCHAR *keyboard_get_uniquename(int) { return keyboard_unique_name; }
static int keyboard_get_widget_num(int) { return sizeof keyboard_keycodes / sizeof keyboard_keycodes[0]; }
static int keyboard_get_widget_type(int, int widget, TCHAR *name, uae_u32 *code)
{
    if (widget < 0 || widget >= keyboard_get_widget_num(0)) {
        return IDEV_WIDGET_NONE;
    }
    int scancode = keyboard_keycodes[widget];
    if (name) {
        _sntprintf(name, 64, _T("Key %d"), scancode);
        name[63] = 0;
    }
    if (code) {
        *code = scancode;
    }
    return IDEV_WIDGET_KEY;
}
static int keyboard_get_widget_first(int, int type)
{
    return type == IDEV_WIDGET_KEY ? 0 : -1;
}
static int keyboard_get_flags(int) { return 0; }

#ifdef UAE_UNIX_WITH_SDL3
static void joystick_release_device(int index)
{
    if (index < 0 || index >= unix_joystick_count) {
        return;
    }
    unix_joystick_device *dev = &unix_joysticks[index];
    for (int i = 0; i < dev->axis_count && i < ID_AXIS_TOTAL; i++) {
        if (dev->axis_state[i]) {
            setjoystickstate(index, i, 0, 32767);
            dev->axis_state[i] = 0;
        }
    }
    for (int i = 0; i < dev->button_count && i < ID_BUTTON_TOTAL; i++) {
        if (dev->button_state[i]) {
            setjoybuttonstate(index, i, 0);
            dev->button_state[i] = false;
        }
    }
}

static void joystick_close_devices(void)
{
    for (int i = 0; i < unix_joystick_count; i++) {
        joystick_release_device(i);
        if (unix_joysticks[i].gamepad) {
            SDL_CloseGamepad(unix_joysticks[i].gamepad);
        } else if (unix_joysticks[i].joystick) {
            SDL_CloseJoystick(unix_joysticks[i].joystick);
        }
    }
    memset(unix_joysticks, 0, sizeof unix_joysticks);
    unix_joystick_count = 0;
}

static void joystick_copy_text(TCHAR *dst, int dstlen, const char *src, const TCHAR *fallback)
{
    if (!dst || dstlen <= 0) {
        return;
    }
    if (src && src[0]) {
        _sntprintf(dst, dstlen, _T("%s"), src);
    } else {
        _sntprintf(dst, dstlen, _T("%s"), fallback);
    }
    dst[dstlen - 1] = 0;
}

static void joystick_make_unique(TCHAR *dst, int dstlen, const TCHAR *kind, SDL_JoystickID instance_id, int ordinal)
{
    char guid[64];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(instance_id), guid, sizeof guid);
    _sntprintf(dst, dstlen, _T("unix.%s.%s.%d"), kind, guid, ordinal);
    dst[dstlen - 1] = 0;
}

static void joystick_add_axis(unix_joystick_device *dev, int kind, int code)
{
    if (!dev || dev->axis_count >= ID_AXIS_TOTAL) {
        return;
    }
    int axis = dev->axis_count++;
    dev->axis_kind[axis] = kind;
    dev->axis_code[axis] = code;
}

static void joystick_add_button(unix_joystick_device *dev, int code)
{
    if (!dev || dev->button_count >= ID_BUTTON_TOTAL) {
        return;
    }
    dev->button_code[dev->button_count++] = code;
}

static void joystick_register_gamepad(SDL_JoystickID instance_id)
{
    if (unix_joystick_count >= MAX_INPUT_DEVICES) {
        return;
    }

    SDL_Gamepad *gamepad = SDL_OpenGamepad(instance_id);
    if (!gamepad) {
        write_log(_T("SDL3: failed to open gamepad %d: %s\n"), (int)instance_id, SDL_GetError());
        return;
    }

    unix_joystick_device *dev = &unix_joysticks[unix_joystick_count];
    memset(dev, 0, sizeof *dev);
    dev->instance_id = instance_id;
    dev->gamepad = gamepad;
    dev->is_gamepad = true;
    joystick_copy_text(dev->friendly, sizeof dev->friendly / sizeof dev->friendly[0],
        SDL_GetGamepadName(gamepad), _T("SDL Gamepad"));
    joystick_make_unique(dev->unique, sizeof dev->unique / sizeof dev->unique[0],
        _T("gamepad"), instance_id, unix_joystick_count);

    for (int i = 0; i < (int)(sizeof unix_gamepad_axes / sizeof unix_gamepad_axes[0]); i++) {
        if (SDL_GamepadHasAxis(gamepad, unix_gamepad_axes[i])) {
            joystick_add_axis(dev, UNIX_AXIS_SDL, unix_gamepad_axes[i]);
        }
    }
    joystick_add_axis(dev, UNIX_AXIS_GAMEPAD_DPAD_X, 0);
    joystick_add_axis(dev, UNIX_AXIS_GAMEPAD_DPAD_Y, 0);

    for (int i = 0; i < (int)(sizeof unix_gamepad_buttons / sizeof unix_gamepad_buttons[0]); i++) {
        if (SDL_GamepadHasButton(gamepad, unix_gamepad_buttons[i])) {
            joystick_add_button(dev, unix_gamepad_buttons[i]);
        }
    }

    write_log(_T("SDL3: gamepad %d: '%s' (%s), %d axes, %d buttons\n"),
        unix_joystick_count, dev->friendly, dev->unique, dev->axis_count, dev->button_count);
    unix_joystick_count++;
}

static void joystick_register_joystick(SDL_JoystickID instance_id)
{
    if (unix_joystick_count >= MAX_INPUT_DEVICES || SDL_IsGamepad(instance_id)) {
        return;
    }

    SDL_Joystick *joystick = SDL_OpenJoystick(instance_id);
    if (!joystick) {
        write_log(_T("SDL3: failed to open joystick %d: %s\n"), (int)instance_id, SDL_GetError());
        return;
    }

    unix_joystick_device *dev = &unix_joysticks[unix_joystick_count];
    memset(dev, 0, sizeof *dev);
    dev->instance_id = instance_id;
    dev->joystick = joystick;
    joystick_copy_text(dev->friendly, sizeof dev->friendly / sizeof dev->friendly[0],
        SDL_GetJoystickName(joystick), _T("SDL Joystick"));
    joystick_make_unique(dev->unique, sizeof dev->unique / sizeof dev->unique[0],
        _T("joystick"), instance_id, unix_joystick_count);

    int axes = SDL_GetNumJoystickAxes(joystick);
    int hats = SDL_GetNumJoystickHats(joystick);
    int buttons = SDL_GetNumJoystickButtons(joystick);
    if (axes < 0) {
        axes = 0;
    }
    if (hats < 0) {
        hats = 0;
    }
    if (buttons < 0) {
        buttons = 0;
    }
    for (int i = 0; i < axes && dev->axis_count < ID_AXIS_TOTAL; i++) {
        joystick_add_axis(dev, UNIX_AXIS_SDL, i);
    }
    for (int i = 0; i < hats && dev->axis_count + 1 < ID_AXIS_TOTAL; i++) {
        joystick_add_axis(dev, UNIX_AXIS_HAT_X, i);
        joystick_add_axis(dev, UNIX_AXIS_HAT_Y, i);
    }
    for (int i = 0; i < buttons && dev->button_count < ID_BUTTON_TOTAL; i++) {
        joystick_add_button(dev, i);
    }

    write_log(_T("SDL3: joystick %d: '%s' (%s), %d axes, %d buttons\n"),
        unix_joystick_count, dev->friendly, dev->unique, dev->axis_count, dev->button_count);
    unix_joystick_count++;
}

static void joystick_open_devices(void)
{
    joystick_close_devices();

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; i++) {
            joystick_register_gamepad(ids[i]);
        }
        SDL_free(ids);
    }

    count = 0;
    ids = SDL_GetJoysticks(&count);
    if (ids) {
        for (int i = 0; i < count; i++) {
            joystick_register_joystick(ids[i]);
        }
        SDL_free(ids);
    }
}

static int joystick_init(void)
{
    input_init();
    if (unix_joystick_sdl_initialized) {
        return 1;
    }

    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        write_log(_T("SDL3: joystick/gamepad unavailable: %s\n"), SDL_GetError());
        return 0;
    }
    SDL_SetJoystickEventsEnabled(true);
    SDL_SetGamepadEventsEnabled(true);
    unix_joystick_sdl_initialized = true;
    joystick_open_devices();
    return 1;
}

static void joystick_close(void)
{
    if (!unix_joystick_sdl_initialized) {
        return;
    }
    joystick_close_devices();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    unix_joystick_sdl_initialized = false;
}

static int joystick_acquire(int num, int)
{
    return num < 0 || num < unix_joystick_count;
}

static void joystick_unacquire(int num)
{
    if (num >= 0 && num < unix_joystick_count) {
        joystick_release_device(num);
    }
}

static void joystick_set_axis(unix_joystick_device *dev, int joy, int axis, int value, int max)
{
    if (!dev || axis < 0 || axis >= dev->axis_count || axis >= ID_AXIS_TOTAL) {
        return;
    }
    if (dev->axis_state[axis] == value) {
        return;
    }
    dev->axis_state[axis] = value;
    setjoystickstate(joy, axis, value, max);
}

static void joystick_set_button(unix_joystick_device *dev, int joy, int button, bool down)
{
    if (!dev || button < 0 || button >= dev->button_count || button >= ID_BUTTON_TOTAL) {
        return;
    }
    if (dev->button_state[button] == down) {
        return;
    }
    dev->button_state[button] = down;
    setjoybuttonstate(joy, button, down ? 1 : 0);
}

static void joystick_read_gamepad(unix_joystick_device *dev, int joy)
{
    int dpad_x = 0;
    int dpad_y = 0;

    for (int axis = 0; axis < dev->axis_count; axis++) {
        switch (dev->axis_kind[axis]) {
        case UNIX_AXIS_SDL:
            joystick_set_axis(dev, joy, axis, SDL_GetGamepadAxis(dev->gamepad, (SDL_GamepadAxis)dev->axis_code[axis]), 32767);
            break;
        case UNIX_AXIS_GAMEPAD_DPAD_X:
            dpad_x = SDL_GetGamepadButton(dev->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1 : 0;
            dpad_x -= SDL_GetGamepadButton(dev->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? 1 : 0;
            joystick_set_axis(dev, joy, axis, dpad_x, 1);
            break;
        case UNIX_AXIS_GAMEPAD_DPAD_Y:
            dpad_y = SDL_GetGamepadButton(dev->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1 : 0;
            dpad_y -= SDL_GetGamepadButton(dev->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1 : 0;
            joystick_set_axis(dev, joy, axis, dpad_y, 1);
            break;
        }
    }

    for (int button = 0; button < dev->button_count; button++) {
        joystick_set_button(dev, joy, button,
            SDL_GetGamepadButton(dev->gamepad, (SDL_GamepadButton)dev->button_code[button]));
    }
}

static void joystick_read_joystick(unix_joystick_device *dev, int joy)
{
    for (int axis = 0; axis < dev->axis_count; axis++) {
        switch (dev->axis_kind[axis]) {
        case UNIX_AXIS_SDL:
            joystick_set_axis(dev, joy, axis, SDL_GetJoystickAxis(dev->joystick, dev->axis_code[axis]), 32767);
            break;
        case UNIX_AXIS_HAT_X:
        {
            Uint8 hat = SDL_GetJoystickHat(dev->joystick, dev->axis_code[axis]);
            int value = (hat & SDL_HAT_RIGHT) ? 1 : 0;
            value -= (hat & SDL_HAT_LEFT) ? 1 : 0;
            joystick_set_axis(dev, joy, axis, value, 1);
            break;
        }
        case UNIX_AXIS_HAT_Y:
        {
            Uint8 hat = SDL_GetJoystickHat(dev->joystick, dev->axis_code[axis]);
            int value = (hat & SDL_HAT_DOWN) ? 1 : 0;
            value -= (hat & SDL_HAT_UP) ? 1 : 0;
            joystick_set_axis(dev, joy, axis, value, 1);
            break;
        }
        }
    }

    for (int button = 0; button < dev->button_count; button++) {
        joystick_set_button(dev, joy, button, SDL_GetJoystickButton(dev->joystick, dev->button_code[button]));
    }
}

static void joystick_read(void)
{
    if (!unix_joystick_sdl_initialized) {
        return;
    }
    SDL_UpdateGamepads();
    SDL_UpdateJoysticks();
    for (int i = 0; i < unix_joystick_count; i++) {
        unix_joystick_device *dev = &unix_joysticks[i];
        if (dev->gamepad) {
            joystick_read_gamepad(dev, i);
        } else if (dev->joystick) {
            joystick_read_joystick(dev, i);
        }
    }
}

static int joystick_get_num(void)
{
    return unix_joystick_count;
}

static TCHAR *joystick_get_friendlyname(int joy)
{
    return joy >= 0 && joy < unix_joystick_count ? unix_joysticks[joy].friendly : empty_friendly;
}

static TCHAR *joystick_get_uniquename(int joy)
{
    return joy >= 0 && joy < unix_joystick_count ? unix_joysticks[joy].unique : empty_unique_name;
}

static int joystick_get_widget_num(int joy)
{
    if (joy < 0 || joy >= unix_joystick_count) {
        return 0;
    }
    return unix_joysticks[joy].axis_count + unix_joysticks[joy].button_count;
}

static void joystick_axis_name(unix_joystick_device *dev, int axis, TCHAR *name)
{
    if (!name) {
        return;
    }
    switch (dev->axis_kind[axis]) {
    case UNIX_AXIS_SDL:
        if (dev->is_gamepad) {
            for (int i = 0; i < (int)(sizeof unix_gamepad_axes / sizeof unix_gamepad_axes[0]); i++) {
                if (dev->axis_code[axis] == unix_gamepad_axes[i]) {
                    _tcscpy(name, unix_gamepad_axis_names[i]);
                    return;
                }
            }
        }
        _sntprintf(name, 64, _T("Axis %d"), dev->axis_code[axis] + 1);
        name[63] = 0;
        return;
    case UNIX_AXIS_GAMEPAD_DPAD_X:
        _tcscpy(name, _T("DPad X Axis"));
        return;
    case UNIX_AXIS_GAMEPAD_DPAD_Y:
        _tcscpy(name, _T("DPad Y Axis"));
        return;
    case UNIX_AXIS_HAT_X:
        _sntprintf(name, 64, _T("Hat %d X Axis"), dev->axis_code[axis] + 1);
        name[63] = 0;
        return;
    case UNIX_AXIS_HAT_Y:
        _sntprintf(name, 64, _T("Hat %d Y Axis"), dev->axis_code[axis] + 1);
        name[63] = 0;
        return;
    }
    _tcscpy(name, _T("Axis"));
}

static void joystick_button_name(unix_joystick_device *dev, int button, TCHAR *name)
{
    if (!name) {
        return;
    }
    if (dev->is_gamepad) {
        for (int i = 0; i < (int)(sizeof unix_gamepad_buttons / sizeof unix_gamepad_buttons[0]); i++) {
            if (dev->button_code[button] == unix_gamepad_buttons[i]) {
                _tcscpy(name, unix_gamepad_button_names[i]);
                return;
            }
        }
    }
    _sntprintf(name, 64, _T("Button %d"), dev->button_code[button] + 1);
    name[63] = 0;
}

static int joystick_get_widget_type(int joy, int widget, TCHAR *name, uae_u32 *code)
{
    if (joy < 0 || joy >= unix_joystick_count) {
        return IDEV_WIDGET_NONE;
    }
    unix_joystick_device *dev = &unix_joysticks[joy];
    if (code) {
        *code = widget;
    }
    if (widget >= 0 && widget < dev->axis_count) {
        joystick_axis_name(dev, widget, name);
        return IDEV_WIDGET_AXIS;
    }
    int button = widget - dev->axis_count;
    if (button >= 0 && button < dev->button_count) {
        joystick_button_name(dev, button, name);
        return IDEV_WIDGET_BUTTON;
    }
    return IDEV_WIDGET_NONE;
}

static int joystick_get_widget_first(int joy, int type)
{
    if (joy < 0 || joy >= unix_joystick_count) {
        return -1;
    }
    switch (type) {
    case IDEV_WIDGET_AXIS:
        return unix_joysticks[joy].axis_count > 0 ? 0 : -1;
    case IDEV_WIDGET_BUTTON:
        return unix_joysticks[joy].button_count > 0 ? unix_joysticks[joy].axis_count : -1;
    }
    return -1;
}

static int joystick_get_flags(int) { return 0; }

static bool joystick_has_button(int joy, int button)
{
    return joy >= 0 && joy < unix_joystick_count && button >= 0 && button < unix_joysticks[joy].button_count;
}

static bool joystick_axis_is_dpad_or_hat(int joy, int axis)
{
    if (joy < 0 || joy >= unix_joystick_count || axis < 0 || axis >= unix_joysticks[joy].axis_count) {
        return false;
    }
    int kind = unix_joysticks[joy].axis_kind[axis];
    return kind == UNIX_AXIS_GAMEPAD_DPAD_X || kind == UNIX_AXIS_GAMEPAD_DPAD_Y ||
        kind == UNIX_AXIS_HAT_X || kind == UNIX_AXIS_HAT_Y;
}

void unix_input_joystick_device_changed(void)
{
    if (unix_joystick_sdl_initialized) {
        joystick_open_devices();
    }
}
#else
static int joystick_init(void) { return input_init(); }
static void joystick_close(void) {}
static int joystick_acquire(int, int) { return 1; }
static void joystick_unacquire(int) {}
static void joystick_read(void) {}
static int joystick_get_num(void) { return 0; }
static TCHAR *joystick_get_friendlyname(int) { return empty_friendly; }
static TCHAR *joystick_get_uniquename(int) { return empty_unique_name; }
static int joystick_get_widget_num(int) { return 0; }
static int joystick_get_widget_type(int, int, TCHAR *, uae_u32 *) { return IDEV_WIDGET_NONE; }
static int joystick_get_widget_first(int, int) { return -1; }
static int joystick_get_flags(int) { return 0; }
static bool joystick_has_button(int, int) { return false; }
static bool joystick_axis_is_dpad_or_hat(int, int) { return false; }
void unix_input_joystick_device_changed(void) {}
#endif

inputdevice_functions inputdevicefunc_joystick = {
    joystick_init, joystick_close, joystick_acquire, joystick_unacquire, joystick_read,
    joystick_get_num, joystick_get_friendlyname, joystick_get_uniquename,
    joystick_get_widget_num, joystick_get_widget_type, joystick_get_widget_first,
    joystick_get_flags
};

inputdevice_functions inputdevicefunc_mouse = {
    input_init, input_close, input_acquire, input_unacquire, input_read,
    mouse_get_num, mouse_get_friendlyname, mouse_get_uniquename,
    mouse_get_widget_num, mouse_get_widget_type, mouse_get_widget_first,
    mouse_get_flags
};

inputdevice_functions inputdevicefunc_keyboard = {
    input_init, input_close, input_acquire, input_unacquire, input_read,
    keyboard_get_num, keyboard_get_friendlyname, keyboard_get_uniquename,
    keyboard_get_widget_num, keyboard_get_widget_type, keyboard_get_widget_first,
    keyboard_get_flags
};

void unix_input_mouse_motion(int dx, int dy)
{
    if (dx) {
        setmousestate(0, 0, dx, 0);
    }
    if (dy) {
        setmousestate(0, 1, dy, 0);
    }
}

void unix_input_mouse_button(int button, bool pressed)
{
    if (button >= 0 && button < 3) {
        setmousebuttonstate(0, button, pressed ? 1 : 0);
    }
}

void unix_input_mouse_wheel(int, int y)
{
    if (y) {
        setmousestate(0, 2, y * 120, 0);
    }
}

void unix_input_set_mouse_active(bool active)
{
    mouse_active = active;
}

bool unix_input_get_mouse_active(void)
{
    return mouse_active;
}

static void unix_input_update_lock_state(int lockstate)
{
    host_capslockstate = (lockstate & UNIX_INPUT_LOCK_CAPS) != 0;
    host_numlockstate = (lockstate & UNIX_INPUT_LOCK_NUM) != 0;
    host_scrolllockstate = (lockstate & UNIX_INPUT_LOCK_SCROLL) != 0;
}

void unix_input_keyboard_key(int scancode, bool pressed, int lockstate)
{
    if (scancode <= 0 || scancode >= (int)(sizeof keyboard_state / sizeof keyboard_state[0])) {
        return;
    }
    unix_input_update_lock_state(lockstate);
    if (keyboard_state[scancode] == pressed) {
        return;
    }

    keyboard_state[scancode] = pressed;
    if (!host_keyboard_hotkey(0, scancode,
            host_key_from_native(native_keys, scancode),
            currprefs.input_keyboard_type == 0 ? UKEY_F12 : UKEY_NUMLOCKCLEAR,
            UKEY_F12, pressed ? 1 : 0)) {
        inputdevice_translatekeycode(0, scancode, pressed ? 1 : 0, false);
    }
}

void unix_input_release_keys(void)
{
    for (int scancode = 0; scancode < (int)(sizeof keyboard_state / sizeof keyboard_state[0]); scancode++) {
        if (keyboard_state[scancode]) {
            keyboard_state[scancode] = false;
            inputdevice_translatekeycode(0, scancode, 0, true);
        }
    }
    setmousebuttonstateall(0, 0, 7);
}

void release_keys(void) { unix_input_release_keys(); }
int input_get_default_keyboard(int num)
{
    if (num < 0) {
        return 0;
    }
    return num == 0 ? 1 : 0;
}
static bool default_button(void *context, int button)
{
 int dev = (int)(intptr_t)context;
 return dev < 0 ? button < 3 : joystick_has_button(dev, button);
}
static bool default_hat(void *context, int axis)
{
 int dev = (int)(intptr_t)context;
 return dev >= 0 && joystick_axis_is_dpad_or_hat(dev, axis) &&
  joystick_axis_is_dpad_or_hat(dev, axis + 1);
}
int input_get_default_mouse(uae_input_device *uid, int dev, int port, int af, bool gp, bool wheel, bool joymouseswap)
{
 if (joymouseswap || dev != 0)
  return 0;
 int axes = 3;
 host_input_device device = { (void *)(intptr_t)-1, default_button, default_hat, axes, false, true };
 return input_default_mouse(uid, dev, port, af, gp, wheel, joymouseswap, device);
}
int input_get_default_joystick(uae_input_device *uid, int dev, int port, int af, int mode, bool gp, bool joymouseswap, bool default_osk)
{
 if (joymouseswap || dev < 0 || dev >= joystick_get_num())
  return 0;
 int axes = joystick_get_widget_first(dev, IDEV_WIDGET_BUTTON);
 if (axes < 0) axes = joystick_get_widget_num(dev);
 host_input_device device = { (void *)(intptr_t)dev, default_button, default_hat, axes, false, true };
 return input_default_joystick(uid, dev, port, af, mode, gp, joymouseswap, default_osk, device);
}
int input_get_default_joystick_analog(uae_input_device *uid, int dev, int port, int af, bool gp, bool joymouseswap, bool default_osk)
{
 if (joymouseswap || dev < 0 || dev >= joystick_get_num())
  return 0;
 int axes = 2;
 host_input_device device = { (void *)(intptr_t)dev, default_button, default_hat, axes, false, true };
 return input_default_joystick_analog(uid, dev, port, af, gp, joymouseswap, default_osk, device);
}
int input_get_default_lightpen(uae_input_device *, int, int, int, bool, bool, int) { return 0; }
int is_tablet(void) { return 0; }
bool ismouseactive(void) { return unix_input_get_mouse_active(); }
void setmouseactive(int, int active) { unix_input_set_mouse_active(active != 0); }
bool target_can_autoswitchdevice(void) { return false; }
void target_inputdevice_acquire(void) {}
void target_inputdevice_unacquire(bool) {}
int getcapslockstate(void) { return capslockstate; }
void setcapslockstate(int state) { capslockstate = state; }
int target_checkcapslock(int scancode, int *state)
{
    if (scancode != UKEY_CAPSLOCK && scancode != UKEY_NUMLOCKCLEAR && scancode != UKEY_SCROLLLOCK) {
        return 0;
    }
    if (currprefs.keyboard_mode > 0) {
        return 1;
    }
    if (*state == 0) {
        return -1;
    }
    if (scancode == UKEY_CAPSLOCK) {
        *state = host_capslockstate;
        if (gui_data.capslock != (host_capslockstate != 0)) {
            gui_data.capslock = host_capslockstate != 0;
            gui_led(LED_CAPS, gui_data.capslock, -1);
        }
    } else if (scancode == UKEY_NUMLOCKCLEAR) {
        *state = host_numlockstate;
    } else if (scancode == UKEY_SCROLLLOCK) {
        *state = host_scrolllockstate;
    }
    return 1;
}
