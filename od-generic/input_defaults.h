#ifndef UAE_INPUT_DEFAULTS_H
#define UAE_INPUT_DEFAULTS_H

struct uae_input_device;
struct host_input_device {
    void *context;
    bool (*button)(void *, int);
    bool (*hat)(void *, int);
    int axes;
    bool clear_wheel;
    bool replace_osk;
};
int input_default_mouse(struct uae_input_device *uid, int i, int port, int af, bool gp, bool wheel,
			bool joymouseswap, const host_input_device &device);
int input_default_lightpen(struct uae_input_device *uid, int i, int port, int af, bool gp,
			   bool joymouseswap, int submode, const host_input_device &device);
int input_default_joystick(struct uae_input_device *uid, int i, int port, int af, int mode, bool gp,
			   bool joymouseswap, bool default_osk, const host_input_device &device);
int input_default_joystick_analog(struct uae_input_device *uid, int i, int port, int af, bool gp,
				  bool joymouseswap, bool default_osk,
				  const host_input_device &device);

#endif
