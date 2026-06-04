#ifndef WINUAE_OD_UNIX_INPUT_H
#define WINUAE_OD_UNIX_INPUT_H

void unix_input_mouse_motion(int dx, int dy);
void unix_input_mouse_button(int button, bool pressed);
void unix_input_mouse_wheel(int x, int y);
void unix_input_set_mouse_active(bool active);
bool unix_input_get_mouse_active(void);
enum {
    UNIX_INPUT_LOCK_CAPS = 1 << 0,
    UNIX_INPUT_LOCK_NUM = 1 << 1,
    UNIX_INPUT_LOCK_SCROLL = 1 << 2
};
void unix_input_keyboard_key(int scancode, bool pressed, int lockstate);
void unix_input_release_keys(void);
void unix_input_joystick_device_changed(void);

#endif /* WINUAE_OD_UNIX_INPUT_H */
