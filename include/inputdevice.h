 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Joystick, mouse and keyboard emulation prototypes and definitions
  *
  * Copyright 1995 Bernd Schmidt
  * Copyright 2001-2002 Toni Wilen
  */


#define IDTYPE_JOYSTICK 0
#define IDTYPE_MOUSE 1
#define IDTYPE_KEYBOARD 2

struct inputdevice_functions {
    int (*init)(void);
    void (*close)(void);
    int (*acquire)(int,int);
    void (*unacquire)(int);
    void (*read)(void);
    int (*get_num)(void);
    char* (*get_name)(int);
    int (*get_widget_num)(int);
    int (*get_widget_type)(int,int,char*,uae_u32*);
    int (*get_widget_first)(int,int);
};
extern struct inputdevice_functions idev[3];
extern struct inputdevice_functions inputdevicefunc_joystick;
extern struct inputdevice_functions inputdevicefunc_mouse;
extern struct inputdevice_functions inputdevicefunc_keyboard;
extern int pause_emulation;

struct uae_input_device_kbr_default {
    int scancode;
    int event;
};

#define IDEV_WIDGET_NONE 0
#define IDEV_WIDGET_BUTTON 1
#define IDEV_WIDGET_AXIS 2
#define IDEV_WIDGET_KEY 3

#define IDEV_MAPPED_AUTOFIRE_POSSIBLE 1
#define IDEV_MAPPED_AUTOFIRE_SET 2

#define ID_BUTTON_OFFSET 0
#define ID_BUTTON_TOTAL 32
#define ID_AXIS_OFFSET 32
#define ID_AXIS_TOTAL 32

extern int inputdevice_iterate (int devnum, int num, char *name, int *af);
extern int inputdevice_set_mapping (int devnum, int num, char *name, int af, int sub);
extern int inputdevice_get_mapped_name (int devnum, int num, int *pflags, char *name, int sub);
extern void inputdevice_copyconfig (struct uae_prefs *src, struct uae_prefs *dst);
extern void inputdevice_copy_single_config (struct uae_prefs *p, int src, int dst, int devnum);
extern void inputdevice_swap_ports (struct uae_prefs *p, int devnum);
extern void inputdevice_config_change (void);
extern int inputdevice_config_change_test (void);
extern int inputdevice_get_device_index (int devnum);
extern char *inputdevice_get_device_name (int type, int devnum);
extern int inputdevice_get_device_status (int devnum);
extern void inputdevice_set_device_status (int devnum, int enabled);
extern int inputdevice_get_device_total (int type);
extern int inputdevice_get_widget_num (int devnum);
extern int inputdevice_get_widget_type (int devnum, int num, char *name);

extern void input_get_default_mouse (struct uae_input_device *uid);
extern void input_get_default_joystick (struct uae_input_device *uid);

#define DEFEVENT(A, B, C, D, E, F) INPUTEVENT_ ## A,
enum inputevents {
INPUTEVENT_ZERO,
#include "inputevents.def"
INPUTEVENT_END
};
#undef DEFEVENT

enum mousestate { mousehack_unknown, mousehack_normal, mousehack_dontcare, mousehack_follow };

extern void handle_cd32_joystick_cia (uae_u8, uae_u8);
extern uae_u8 handle_parport_joystick (int port, uae_u8 pra, uae_u8 dra);
extern uae_u8 handle_joystick_buttons (uae_u8);
extern int getbuttonstate (int joy, int button);
extern int getjoystate (int joy);

extern void mousehack_set (enum mousestate);
extern int mousehack_get (void);
extern uae_u32 mousehack_helper (void);
extern void mousehack_handle (int sprctl, int sprpos);

extern void setmousebuttonstateall (int mouse, uae_u32 buttonbits, uae_u32 buttonmask);
extern void setjoybuttonstateall (int joy, uae_u32 buttonbits, uae_u32 buttonmask);
extern void setjoybuttonstate (int joy, int button, int state);
extern void setmousebuttonstate (int mouse, int button, int state);
extern void setjoystickstate (int joy, int axle, int state, int max);
void setmousestate (int mouse, int axis, int data, int isabs);
extern void inputdevice_updateconfig (struct uae_prefs *prefs);

extern int inputdevice_translatekeycode (int keyboard, int scancode, int state);
extern void inputdevice_setkeytranslation (struct uae_input_device_kbr_default *trans);
extern void handle_input_event (int nr, int state, int max, int autofire);
extern void inputdevice_do_keyboard (int code, int state);

extern uae_u16 potgo_value;
extern uae_u16 POTGOR (void);
extern void POTGO (uae_u16 v);
extern uae_u16 POT0DAT (void);
extern uae_u16 POT1DAT (void);
extern void JOYTEST (uae_u16 v);
extern uae_u16 JOY0DAT (void);
extern uae_u16 JOY1DAT (void);

extern void inputdevice_vsync (void);
extern void inputdevice_hsync (void);

extern void write_inputdevice_config (struct uae_prefs *p, FILE *f);
extern void read_inputdevice_config (struct uae_prefs *p, char *option, char *value);
extern void reset_inputdevice_config (struct uae_prefs *pr);

extern void inputdevice_init (void);
extern void inputdevice_close (void);
extern void inputdevice_default_prefs (struct uae_prefs *p);

extern void inputdevice_acquire (int);
extern void inputdevice_unacquire (void);

extern void indicator_leds (int num, int state);

extern void warpmode (int mode);
extern void pausemode (int mode);

extern void inputdevice_add_inputcode (int code);
extern void inputdevice_handle_inputcode (void);

#define JSEM_KBDLAYOUT 0
#define JSEM_JOYS 100
#define JSEM_MICE 200
#define JSEM_END 300
#define JSEM_DECODEVAL(port,p) ((port) == 0 ? (p)->jport0 : (p)->jport1)
#define JSEM_ISNUMPAD(port,p) (jsem_iskbdjoy(port,p) == JSEM_KBDLAYOUT)
#define JSEM_ISCURSOR(port,p) (jsem_iskbdjoy(port,p) == JSEM_KBDLAYOUT + 1)
#define JSEM_ISSOMEWHEREELSE(port,p) (jsem_iskbdjoy(port,p) == JSEM_KBDLAYOUT + 2)
extern int compatibility_device[2];

extern int jsem_isjoy (int port, struct uae_prefs *p);
extern int jsem_ismouse (int port, struct uae_prefs *p);
extern int jsem_iskbdjoy (int port, struct uae_prefs *p);
