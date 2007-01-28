 /*
  * UAE - The Un*x Amiga Emulator
  *
  * joystick/mouse emulation
  *
  * Copyright 2001-2006 Toni Wilen
  *
  * new fetures:
  * - very configurable (and very complex to configure :)
  * - supports multiple native input devices (joysticks and mice)
  * - supports mapping joystick/mouse buttons to keys and vice versa
  * - joystick mouse emulation (supports both ports)
  * - supports parallel port joystick adapter
  * - full cd32 pad support (supports both ports)
  * - fully backward compatible with old joystick/mouse configuration
  *
  */

//#define DONGLE_DEBUG

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "keyboard.h"
#include "inputdevice.h"
#include "keybuf.h"
#include "custom.h"
#include "xwin.h"
#include "drawing.h"
#include "memory.h"
#include "events.h"
#include "newcpu.h"
#include "uae.h"
#include "picasso96.h"
#include "catweasel.h"
#include "debug.h"
#include "ar.h"
#include "gui.h"
#include "disk.h"
#include "sound.h"
#include "savestate.h"
#include "arcadia.h"
#include "zfile.h"
#include "cia.h"
#include "autoconf.h"

int inputdevice_logging = 0;
static int potgo_logging = 0;

#define DIR_LEFT 1
#define DIR_RIGHT 2
#define DIR_UP 4
#define DIR_DOWN 8

struct inputevent {
    const char *confname;
    const char *name;
    int allow_mask;
    int type;
    int unit;
    int data;
};

#define JOYBUTTON_1 0 /* fire/left mousebutton */
#define JOYBUTTON_2 1 /* 2nd/right mousebutton */
#define JOYBUTTON_3 2 /* 3rd/middle mousebutton */
#define JOYBUTTON_CD32_PLAY 3
#define JOYBUTTON_CD32_RWD 4
#define JOYBUTTON_CD32_FFW 5
#define JOYBUTTON_CD32_GREEN 6
#define JOYBUTTON_CD32_YELLOW 7
#define JOYBUTTON_CD32_RED 8
#define JOYBUTTON_CD32_BLUE 9

#define INPUTEVENT_JOY1_CD32_FIRST INPUTEVENT_JOY1_CD32_PLAY
#define INPUTEVENT_JOY2_CD32_FIRST INPUTEVENT_JOY2_CD32_PLAY
#define INPUTEVENT_JOY1_CD32_LAST INPUTEVENT_JOY1_CD32_BLUE
#define INPUTEVENT_JOY2_CD32_LAST INPUTEVENT_JOY2_CD32_BLUE

/* event masks */
#define AM_KEY 1 /* keyboard allowed */
#define AM_JOY_BUT 2 /* joystick buttons allowed */
#define AM_JOY_AXIS 4 /* joystick axis allowed */
#define AM_MOUSE_BUT 8 /* mouse buttons allowed */
#define AM_MOUSE_AXIS 16 /* mouse direction allowed */
#define AM_AF 32 /* supports autofire */
#define AM_INFO 64 /* information data for gui */
#define AM_DUMMY 128 /* placeholder */
#define AM_K (AM_KEY|AM_JOY_BUT|AM_MOUSE_BUT|AM_AF) /* generic button/switch */

/* event flags */
#define ID_FLAG_AUTOFIRE 1

#define DEFEVENT(A, B, C, D, E, F) {#A, B, C, D, E, F },
struct inputevent events[] = {
{0, 0, AM_K,0,0,0},
#include "inputevents.def"
{0, 0, 0, 0, 0, 0}
};
#undef DEFEVENT

static int sublevdir[2][MAX_INPUT_SUB_EVENT];

struct uae_input_device2 {
    uae_u32 buttonmask;
    int states[MAX_INPUT_DEVICE_EVENTS / 2];
};

static struct uae_input_device2 joysticks2[MAX_INPUT_DEVICES];
static struct uae_input_device2 mice2[MAX_INPUT_DEVICES];

static uae_u8 mouse_settings_reset[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
static uae_u8 joystick_settings_reset[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];

static uae_u8 *inprec_buffer, *inprec_p;
static struct zfile *inprec_zf;
static int inprec_size;
int input_recording = 0;
static uae_u8 *inprec_plast, *inprec_plastptr;
static int inprec_div;

static uae_u32 oldbuttons[4];
static uae_u16 oldjoy[2];

int inprec_open(char *fname, int record)
{
    uae_u32 t = (uae_u32)time(0);
    int i;

    inprec_close();
    inprec_zf = zfile_fopen(fname, record > 0 ? "wb" : "rb");
    if (inprec_zf == NULL)
	return 0;
    inprec_size = 10000;
    inprec_div = 1;
    if (record < 0) {
	uae_u32 id;
	zfile_fseek (inprec_zf, 0, SEEK_END);
	inprec_size = zfile_ftell (inprec_zf);
	zfile_fseek (inprec_zf, 0, SEEK_SET);
        inprec_buffer = inprec_p = xmalloc (inprec_size);
	zfile_fread (inprec_buffer, inprec_size, 1, inprec_zf);
	inprec_plastptr = inprec_buffer;
	id = inprec_pu32();
	if (id != 'UAE\0') {
	    inprec_close();
	    return 0;
	}
	inprec_pu32();
	t = inprec_pu32();
	i = inprec_pu32();
	while (i-- > 0)
	    inprec_pu8();
	inprec_p = inprec_plastptr;
	oldbuttons[0] = oldbuttons[1] = oldbuttons[2] = oldbuttons[3] = 0;
	oldjoy[0] = oldjoy[1] = 0;
	if (record < -1)
	    inprec_div = maxvpos;
    } else if (record > 0) {
	inprec_buffer = inprec_p = xmalloc (inprec_size);
	inprec_ru32('UAE\0');
	inprec_ru8(1);
	inprec_ru8(UAEMAJOR);
	inprec_ru8(UAEMINOR);
	inprec_ru8(UAESUBREV);
	inprec_ru32(t);
	inprec_ru32(0); // extra header size
    } else {
	return 0;
    }
    input_recording = record;
    srand(t);
    CIA_inprec_prepare();
    write_log ("inprec initialized '%s', mode=%d\n", fname, input_recording);
    return 1;
}

void inprec_close(void)
{
    if (!inprec_zf)
	return;
    if (inprec_buffer && input_recording > 0) {
	hsync_counter++;
	inprec_rstart(INPREC_END);
	inprec_rend();
	hsync_counter--;
	zfile_fwrite (inprec_buffer, inprec_p - inprec_buffer, 1, inprec_zf);
	inprec_p = inprec_buffer;
    }
    zfile_fclose (inprec_zf);
    inprec_zf = NULL;
    xfree (inprec_buffer);
    inprec_buffer = NULL;
    input_recording = 0;
    write_log("inprec finished\n");
}
   
void inprec_ru8(uae_u8 v)
{
    *inprec_p++= v;
}
void inprec_ru16(uae_u16 v)
{
    inprec_ru8((uae_u8)(v >> 8));
    inprec_ru8((uae_u8)v);
}
void inprec_ru32(uae_u32 v)
{
    inprec_ru16((uae_u16)(v >> 16));
    inprec_ru16((uae_u16)v);
}
void inprec_rstr(const char *s)
{
    while(*s) {
	inprec_ru8(*s);
	s++;
    }
    inprec_ru8(0);
}
void inprec_rstart(uae_u8 type)
{
    write_log("INPREC: %08.8X: %d\n", hsync_counter, type);
    inprec_ru32(hsync_counter);
    inprec_ru8(0);
    inprec_plast = inprec_p;
    inprec_ru8(0xff);
    inprec_ru8(type);
}
void inprec_rend(void)
{
    *inprec_plast = inprec_p - (inprec_plast + 2);
    if (inprec_p >= inprec_buffer + inprec_size - 256) {
	zfile_fwrite (inprec_buffer, inprec_p - inprec_buffer, 1, inprec_zf);
	inprec_p = inprec_buffer;
    }
}

int inprec_pstart(uae_u8 type)
{
    uae_u8 *p = inprec_p;
    uae_u32 hc = hsync_counter;
    static uae_u8 *lastp;
    uae_u32 hc_orig, hc2_orig;

    if (savestate_state)
	return 0;
    if (p[5 + 1] == INPREC_END) {
	inprec_close();
	return 0;
    }
    hc_orig = hc;
    hc /= inprec_div;
    hc *= inprec_div;
    for (;;) {
	uae_u32 hc2 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	if (p > lastp) {
	    write_log("INPREC: Next %08.8x (%08.8x=%d): %d (%d)\n", hc2, hc, hc2 - hc, p[5 + 1], p[5]);
	    lastp = p;
	}
	hc2_orig = hc2;
	hc2 /= inprec_div;
	hc2 *= inprec_div;
	if (hc > hc2) {
	    write_log("INPREC: %08.8x > %08.8x: %d (%d) missed!\n", hc, hc2, p[5 + 1], p[5]);
	    inprec_close();
	    return 0;
	}
	if (hc2 != hc) {
    	    lastp = p;
	    break;
	}
	if (p[5 + 1] == type) {
	    write_log("INPREC: %08.8x: %d (%d) (%+d)\n", hc, type, p[5], hc_orig - hc2_orig);
	    inprec_plast = p;
	    inprec_plastptr = p + 5 + 2;
	    return 1;
	}
	p += 5 + 2 + p[5];
    }
    inprec_plast = NULL;
    return 0;
}
void inprec_pend(void)
{
    uae_u8 *p = inprec_p;
    uae_u32 hc = hsync_counter;

    if (!inprec_plast)
	return;
    inprec_plast[5 + 1] = 0;
    inprec_plast = NULL;
    inprec_plastptr = NULL;
    hc /= inprec_div;
    hc *= inprec_div;
    for (;;) {
	uae_u32 hc2 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	hc2 /= inprec_div;
	hc2 *= inprec_div;
	if (hc2 != hc)
	    break;
	if (p[5 + 1] != 0)
	    return;
	p += 5 + 2 + p[5];
    }
    inprec_p = p;
    if (p[5 + 1] == INPREC_END)
	inprec_close();
}

uae_u8 inprec_pu8(void)
{
    return *inprec_plastptr++;
}
uae_u16 inprec_pu16(void)
{
    uae_u16 v = inprec_pu8() << 8;
    v |= inprec_pu8();
    return v;
}
uae_u32 inprec_pu32(void)
{
    uae_u32 v = inprec_pu16() << 16;
    v |= inprec_pu16();
    return v;
}
int inprec_pstr(char *s)
{
    int len = 0;
    for(;;) {
	uae_u8 v = inprec_pu8();
	*s++ = v;
	if (!v)
	    break;
	len++;
    }
    return len;
}

static int isdevice (struct uae_input_device *id)
{
    int i, j;
    for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	    if (id->eventid[i][j] > 0)
		return 1;
	}
    }
    return 0;
}

int inputdevice_uaelib (char *s, char *parm)
{
    int i;

    for (i = 1; events[i].name; i++) {
	if (!strcmp (s, events[i].confname)) {
	    handle_input_event (i, atol (parm), 1, 0);
	    return 1;
	}
    }
    return 0;
}

static struct uae_input_device *joysticks;
static struct uae_input_device *mice;
static struct uae_input_device *keyboards;
static struct uae_input_device_kbr_default *keyboard_default;

static double mouse_axis[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];
static double oldm_axis[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];

static int mouse_x[MAX_INPUT_DEVICES], mouse_y[MAX_INPUT_DEVICE_EVENTS];
static int mouse_delta[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];
static int mouse_deltanoreset[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];
static int joybutton[MAX_INPUT_DEVICES];
static unsigned int joydir[MAX_INPUT_DEVICE_EVENTS];
static int joydirpot[MAX_INPUT_DEVICE_EVENTS][2];
static int mouse_frame_x[2], mouse_frame_y[2];

static int mouse_port[2];
static int cd32_shifter[2];
static int cd32_pad_enabled[2];
static int parport_joystick_enabled;
static int oldmx[4], oldmy[4];
static int oleft[4], oright[4], otop[4], obot[4];
static int potgo_hsync;

static int use_joysticks[MAX_INPUT_DEVICES];
static int use_mice[MAX_INPUT_DEVICES];
static int use_keyboards[MAX_INPUT_DEVICES];

#define INPUT_QUEUE_SIZE 16
struct input_queue_struct {
    int event, storedstate, state, max, framecnt, nextframecnt;
};
static struct input_queue_struct input_queue[INPUT_QUEUE_SIZE];

static void out_config (struct zfile *f, int id, int num, char *s1, char *s2)
{
    cfgfile_write (f, "input.%d.%s%d=%s\n", id, s1, num, s2);
    //write_log ("-input.%d.%s%d=%s\n", id, s1, num, s2);
}

static void write_config2 (struct zfile *f, int idnum, int i, int offset, char *tmp1, struct uae_input_device *id)
{
    char tmp2[200], *p;
    int event, got, j, k;
    char *custom;

    p = tmp2;
    got = 0;
    for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	event = id->eventid[i + offset][j];
	custom = id->custom[i + offset][j];
	if (custom == NULL && event <= 0) {
	    for (k = j + 1; k < MAX_INPUT_SUB_EVENT; k++) {
		if (id->eventid[i + offset][k] > 0) break;
	    }
	    if (k == MAX_INPUT_SUB_EVENT)
		break;
	}
	if (p > tmp2) {
	    *p++ = ',';
	    *p = 0;
	}
	if (custom)
	    sprintf (p, "'%s'.%d", custom, id->flags[i + offset][j]);
	else if (event <= 0)
	    sprintf (p, "NULL");
	else
	    sprintf (p, "%s.%d", events[event].confname, id->flags[i + offset][j]);
	p += strlen (p);
    }
    if (p > tmp2)
	out_config (f, idnum, i, tmp1, tmp2);
}

static void write_config (struct zfile *f, int idnum, int devnum, char *name, struct uae_input_device *id, struct uae_input_device2 *id2)
{
    char tmp1[100];
    int i;

    if (!isdevice (id))
	return;
    cfgfile_write (f, "input.%d.%s.%d.disabled=%d\n", idnum, name, devnum, id->enabled ? 0 : 1);
    sprintf (tmp1, "%s.%d.axis.", name, devnum);
    for (i = 0; i < ID_AXIS_TOTAL; i++)
	write_config2 (f, idnum, i, ID_AXIS_OFFSET, tmp1, id);
    sprintf (tmp1, "%s.%d.button." ,name, devnum);
    for (i = 0; i < ID_BUTTON_TOTAL; i++)
	write_config2 (f, idnum, i, ID_BUTTON_OFFSET, tmp1, id);
}

static void kbrlabel (char *s)
{
    while (*s) {
	*s = toupper(*s);
	if (*s == ' ') *s = '_';
	s++;
    }
}

static void write_kbr_config (struct zfile *f, int idnum, int devnum, struct uae_input_device *kbr)
{
    char tmp1[200], tmp2[200], tmp3[200], *p;
    int i, j, k, evt, skip;

    if (!keyboard_default)
	return;
    i = 0;
    while (i < MAX_INPUT_DEVICE_EVENTS && kbr->extra[i][0] >= 0) {
	skip = 0;
	k = 0;
	while (keyboard_default[k].scancode >= 0) {
	    if (keyboard_default[k].scancode == kbr->extra[i][0]) {
		skip = 1;
		for (j = 1; j < MAX_INPUT_SUB_EVENT; j++) {
		    if (kbr->flags[i][j] || kbr->eventid[i][j] > 0)
			skip = 0;
		}
		if (keyboard_default[k].event != kbr->eventid[i][0] || kbr->flags[i][0] != 0)
		    skip = 0;
		break;
	    }
	    k++;
	}
	if (kbr->eventid[i][0] == 0 && kbr->flags[i][0] == 0 && keyboard_default[k].scancode < 0)
	    skip = 1;
	if (skip) {
	    i++;
	    continue;
	}
	p = tmp2;
	p[0] = 0;
	for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	    char *custom = kbr->custom[i][j];
	    evt = kbr->eventid[i][j];
	    if (custom == NULL && evt <= 0) {
		for (k = j + 1; k < MAX_INPUT_SUB_EVENT; k++) {
		    if (kbr->eventid[i][k] > 0) break;
		}
		if (k == MAX_INPUT_SUB_EVENT)
		    break;
	    }
	    if (p > tmp2) {
		*p++ = ',';
		*p = 0;
	    }
	    if (custom)
		sprintf (p, "'%s'.%d", custom, kbr->flags[i][j]);
	    else if (evt > 0)
		sprintf (p, "%s.%d", events[evt].confname, kbr->flags[i][j]);
	    else
		strcat (p, "NULL");
	    p += strlen(p);
	}
	sprintf (tmp3, "%d", kbr->extra[i][0]);
	kbrlabel (tmp3);
	sprintf (tmp1, "keyboard.%d.button.%s", devnum, tmp3);
	cfgfile_write (f, "input.%d.%s=%s\n", idnum, tmp1, tmp2[0] ? tmp2 : "NULL");
	i++;
    }
}

void write_inputdevice_config (struct uae_prefs *p, struct zfile *f)
{
    int i, id;

    cfgfile_write (f, "input.config=%d\n", p->input_selected_setting);
    cfgfile_write (f, "input.joymouse_speed_analog=%d\n", p->input_joymouse_multiplier);
    cfgfile_write (f, "input.joymouse_speed_digital=%d\n", p->input_joymouse_speed);
    cfgfile_write (f, "input.joymouse_deadzone=%d\n", p->input_joymouse_deadzone);
    cfgfile_write (f, "input.joystick_deadzone=%d\n", p->input_joystick_deadzone);
    cfgfile_write (f, "input.mouse_speed=%d\n", p->input_mouse_speed);
    cfgfile_write (f, "input.autofire=%d\n", p->input_autofire_framecnt);
    for (id = 1; id <= MAX_INPUT_SETTINGS; id++) {
	for (i = 0; i < MAX_INPUT_DEVICES; i++)
	    write_config (f, id, i, "joystick", &p->joystick_settings[id][i], &joysticks2[i]);
	for (i = 0; i < MAX_INPUT_DEVICES; i++)
	    write_config (f, id, i, "mouse", &p->mouse_settings[id][i], &mice2[i]);
	for (i = 0; i < MAX_INPUT_DEVICES; i++)
	    write_kbr_config (f, id, i, &p->keyboard_settings[id][i]);
    }
}

static int getnum (char **pp)
{
    char *p = *pp;
    int v = atol (p);

    while (*p != 0 && *p !='.' && *p != ',') p++;
    if (*p == '.' || *p == ',') p++;
    *pp = p;
    return v;
}
static char *getstring (char **pp)
{
    int i;
    static char str[1000];
    char *p = *pp;

    if (*p == 0)
	return 0;
    i = 0;
    while (*p != 0 && *p !='.' && *p != ',') str[i++] = *p++;
    if (*p == '.' || *p == ',') p++;
    str[i] = 0;
    *pp = p;
    return str;
}

void reset_inputdevice_config (struct uae_prefs *pr)
{
    memset (joystick_settings_reset, 0, sizeof (joystick_settings_reset));
    memset (mouse_settings_reset, 0, sizeof (mouse_settings_reset));
}

static void clear_id (struct uae_input_device *id)
{
#ifndef	_DEBUG
    int i, j;
    for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	for (j = 0; j < MAX_INPUT_SUB_EVENT; j++)
	    xfree (id->custom[i][j]);
    }
#endif
    memset (id, 0, sizeof (struct uae_input_device));
    id->enabled = 1;
}

void read_inputdevice_config (struct uae_prefs *pr, char *option, char *value)
{
    struct uae_input_device *id = 0;
    struct inputevent *ie;
    int devnum, num, button, joystick, flags, i, subnum, idnum, keynum;
    int mask;
    char *p, *p2, *custom;

    option += 6; /* "input." */
    p = getstring (&option);
    if (!strcasecmp (p, "config"))
	pr->input_selected_setting = atol (value);
    if (!strcasecmp (p, "joymouse_speed_analog"))
	pr->input_joymouse_multiplier = atol (value);
    if (!strcasecmp (p, "joymouse_speed_digital"))
	pr->input_joymouse_speed = atol (value);
    if (!strcasecmp (p, "joystick_deadzone"))
	pr->input_joystick_deadzone = atol (value);
    if (!strcasecmp (p, "joymouse_deadzone"))
	pr->input_joymouse_deadzone = atol (value);
    if (!strcasecmp (p, "mouse_speed"))
	pr->input_mouse_speed = atol (value);
    if (!strcasecmp (p, "autofire"))
	pr->input_autofire_framecnt = atol (value);
    idnum = atol (p);
    if (idnum <= 0 || idnum > MAX_INPUT_SETTINGS)
	return;
    if (memcmp (option, "mouse.", 6) == 0) {
	p = option + 6;
	devnum = getnum (&p);
	if (devnum < 0 || devnum >= MAX_INPUT_DEVICES)
	    return;
	id = &pr->mouse_settings[idnum][devnum];
	if (!mouse_settings_reset[idnum][devnum])
	    clear_id (id);
	mouse_settings_reset[idnum][devnum] = 1;
	joystick = 0;
    } else if (memcmp (option, "joystick.", 9) == 0) {
	p = option + 9;
	devnum = getnum (&p);
	if (devnum < 0 || devnum >= MAX_INPUT_DEVICES)
	    return;
	id = &pr->joystick_settings[idnum][devnum];
	if (!joystick_settings_reset[idnum][devnum])
	    clear_id (id);
	joystick_settings_reset[idnum][devnum] = 1;
	joystick = 1;
    } else if (memcmp (option, "keyboard.", 9) == 0) {
	joystick = -1;
	p = option + 9;
	devnum = getnum (&p);
	if (devnum < 0 || devnum >= MAX_INPUT_DEVICES)
	    return;
	id = &pr->keyboard_settings[idnum][devnum];
    }
    if (!id)
	return;
    p2 = getstring (&p);
    if (!p2)
	return;
    if (!strcmp (p2, "disabled")) {
	int disabled;
	p = value;
	disabled = getnum (&p);
	id->enabled = disabled == 0 ? 1 : 0;
	return;
    }

    if (joystick < 0) {
	num = getnum (&p);
	keynum = 0;
	while (id->extra[keynum][0] >= 0) {
	    if (id->extra[keynum][0] == num)
		break;
	    keynum++;
	}
	if (id->extra[keynum][0] < 0)
	    return;
    } else {
	button = -1;
	if (!strcmp (p2, "axis"))
	    button = 0;
	else if(!strcmp (p2, "button"))
	    button = 1;
	if (button < 0)
	    return;
	num = getnum (&p);
    }
    p = value;

    custom = NULL;
    for (subnum = 0; subnum < MAX_INPUT_SUB_EVENT; subnum++) {
	xfree (custom);
	custom = NULL;
	p2 = getstring (&p);
	if (!p2)
	    break;
	i = 1;
	while (events[i].name) {
	    if (!strcmp (events[i].confname, p2))
		break;
	    i++;
	}
	ie = &events[i];
	if (!ie->name) {
	    ie = &events[0];
	    if (strlen (p2) > 2 && p2[0] == '\'' && p2[strlen (p2) - 1] == '\'') {
		custom = my_strdup (p2 + 1);
		custom[strlen (custom) - 1] = 0;
	    }
	}
	flags = getnum (&p);
	if (custom == NULL && ie->name == NULL) {
	    if (!strcmp(p2,"NULL")) {
		id->eventid[keynum][subnum] = 0;
		id->flags[keynum][subnum] = 0;
	    }
	    continue;
	}
	if (joystick < 0) {
	    if (!(ie->allow_mask & AM_K))
		continue;
	    id->eventid[keynum][subnum] = ie - events;
	    id->flags[keynum][subnum] = flags;
	    xfree (id->custom[keynum][subnum]);
	    id->custom[keynum][subnum] = custom;
	} else  if (button) {
	    if (joystick)
		mask = AM_JOY_BUT;
	    else
		mask = AM_MOUSE_BUT;
	    if (!(ie->allow_mask & mask))
		continue;
	    id->eventid[num + ID_BUTTON_OFFSET][subnum] = ie - events;
	    id->flags[num + ID_BUTTON_OFFSET][subnum] = flags;
	    xfree (id->custom[num + ID_BUTTON_OFFSET][subnum]);
	    id->custom[num + ID_BUTTON_OFFSET][subnum] = custom;
	} else {
	    if (joystick)
		mask = AM_JOY_AXIS;
	    else
		mask = AM_MOUSE_AXIS;
	    if (!(ie->allow_mask & mask))
		continue;
	    id->eventid[num + ID_AXIS_OFFSET][subnum] = ie - events;
	    id->flags[num + ID_AXIS_OFFSET][subnum] = flags;
	    xfree (id->custom[num + ID_AXIS_OFFSET][subnum]);
	    id->custom[num + ID_AXIS_OFFSET][subnum] = custom;
	}
	custom = NULL;
    }
    xfree (custom);
}

static int ievent_alive = 0;
static int lastmx, lastmy;
static uae_u32 magicmouse_ibase = 0;

static uaecptr get_intuitionbase(void)
{
    uaecptr v = get_long(4);
    addrbank *b = &get_mem_bank(v);

    if (!b || !b->check(v, 400) || b->flags != ABFLAG_RAM)
	return 0;
    v += 378; // liblist
    while (v = get_long(v)) {
	uae_u32 v2;
	uae_u8 *p;
	b = &get_mem_bank(v);
	if (!b || !b->check (v, 32) || b->flags != ABFLAG_RAM) {
	    magicmouse_ibase = 0xffffffff;
	    return 0;
	}
	v2 = get_long(v + 10); // name
	b = &get_mem_bank(v2);
	if (!b || !b->check (v2, 20)) {
	    magicmouse_ibase = 0xffffffff;
	    return 0;
	}
	if (b->flags != ABFLAG_ROM)
	    return 0;
	p = b->xlateaddr(v2);
	if (!strcmp(p, "intuition.library"))
	    return v;
    }
    return 0;
}

static void mousehack_enable (void)
{
    if (!uae_boot_rom)
	return;
    if (rtarea[get_long (RTAREA_BASE + 40) + 12 - 1])
	return;
    rtarea[get_long (RTAREA_BASE + 40) + 12 - 1] = 1;
}

static void mousehack_setpos(int mousexpos, int mouseypos)
{
    uae_u8 *p;
    if (!uae_boot_rom)
	return;
    p = rtarea + get_long (RTAREA_BASE + 40) + 12;
    p[0] = mousexpos >> 8;
    p[1] = mousexpos;
    p[2] = mouseypos >> 8;
    p[3] = mouseypos;
    //write_log("%dx%d\n", mousexpos, mouseypos);
}

static int mouseedge_x, mouseedge_y, mouseedge_time;
#define MOUSEEDGE_RANGE 1000
#define MOUSEEDGE_RANGE_MENU 1000
#define MOUSEEDGE_TIME 2

void setamigamouse(int x, int y)
{
    mousehack_enable();
    mousehack_setpos(x, y);
}

extern void setmouseactivexy(int,int,int);
extern void drawing_adjust_mousepos(int*,int*);

static void mouseedge(void)
{
    int x, y, dir;
    uaecptr ib;
    static int melast_x, melast_y;
    static int isnonzero;

    if (!currprefs.win32_outsidemouse || magicmouse_ibase == 0xffffffff)
	return;
    dir = 0;
    if (!mouseedge_time) {
	isnonzero = 0;
	goto end;
    }
    if (magicmouse_ibase == 0)
	ib = get_intuitionbase();
    else
	ib = magicmouse_ibase;
    if (!ib)
	return;
    x = get_word(ib + 70);
    y = get_word(ib + 68);
    if (x || y)
	isnonzero = 1;
    //write_log("%x x %d\n", x, y);
    if (!isnonzero)
	return;
    if (melast_x == x) {
	if (mouseedge_x < -MOUSEEDGE_RANGE) {
	    mouseedge_x = 0;
	    dir |= 1;
	    goto end;
	}
	if (mouseedge_x > MOUSEEDGE_RANGE) {
	    mouseedge_x = 0;
	    dir |= 2;
	    goto end;
	}
    } else {
	mouseedge_x = 0;
        melast_x = x;
    }
    if (melast_y == y) {
	if (mouseedge_y < -MOUSEEDGE_RANGE_MENU) {
	    mouseedge_y = 0;
	    dir |= 4;
	    goto end;
	}
	if (mouseedge_y > MOUSEEDGE_RANGE) {
	    mouseedge_y = 0;
	    dir |= 8;
	    goto end;
	}
    } else {
	mouseedge_y = 0;
        melast_y = y;
    }
    return;

end:
    mouseedge_time = 0;
    if (dir) {
	if (!picasso_on) {
	    drawing_adjust_mousepos(&x, &y);
	}
	//write_log("%d\n", dir);
	if (!dmaen(DMA_SPRITE))
	    setmouseactivexy(x, y, 0);
	else
	    setmouseactivexy(x, y, dir);
    }
}

int mousehack_alive (void)
{
    return ievent_alive > 0;
}

static void mousehack_helper (void)
{
    int mousexpos, mouseypos;

    if (!mousehack_allowed())
	return;
#ifdef PICASSO96
    if (picasso_on) {
	mousexpos = lastmx - picasso96_state.XOffset;
	mouseypos = lastmy - picasso96_state.YOffset;
    } else
#endif
    {
	mouseypos = coord_native_to_amiga_y (lastmy) << 1;
	mousexpos = coord_native_to_amiga_x (lastmx);
    }
    mousehack_setpos(mousexpos, mouseypos);
}

STATIC_INLINE int adjust (int val)
{
    if (val > 127)
	return 127;
    else if (val < -127)
	return -127;
    return val;
}

int getbuttonstate (int joy, int button)
{
    int v;

    v = (joybutton[joy] & (1 << button)) ? 1 : 0;
    if (input_recording > 0 && ((joybutton[joy] ^ oldbuttons[joy]) & (1 << button))) {
	oldbuttons[joy] &= ~(1 << button);
	if (v)
	    oldbuttons[joy] |= 1 << button;
	inprec_rstart(INPREC_JOYBUTTON);
	inprec_ru8(joy);
	inprec_ru8(button);
	inprec_ru8(v);
	inprec_rend();
    } else if (input_recording < 0) {
	while(inprec_pstart (INPREC_JOYBUTTON)) {
	    uae_u8 j = inprec_pu8();
	    uae_u8 but = inprec_pu8();
	    uae_u8 vv = inprec_pu8();
	    inprec_pend();
	    oldbuttons[j] &= ~(1 << but);
	    if (vv)
		oldbuttons[j] |= 1 << but;
	}
	v = (oldbuttons[joy] & (1 << button)) ? 1 : 0;
    }
    return v;
}

static void mouseupdate (int pct)
{
    int v, i;

    if (pct == 100) {
	if (mouse_delta[0][0] < 0) {
	    if (mouseedge_x > 0)
		mouseedge_x = 0;
	    else
		mouseedge_x += mouse_delta[0][0];
	    mouseedge_time = MOUSEEDGE_TIME;
	}
	if (mouse_delta[0][0] > 0) {
	    if (mouseedge_x < 0)
		mouseedge_x = 0;
	    else
		mouseedge_x += mouse_delta[0][0];
	    mouseedge_time = MOUSEEDGE_TIME;
	}
	if (mouse_delta[0][1] < 0) {
	    if (mouseedge_y > 0)
		mouseedge_y = 0;
	    else
		mouseedge_y += mouse_delta[0][1];
	    mouseedge_time = MOUSEEDGE_TIME;
	}
	if (mouse_delta[0][1] > 0) {
	    if (mouseedge_y < 0)
		mouseedge_y = 0;
	    else
		mouseedge_y += mouse_delta[0][1];
	    mouseedge_time = MOUSEEDGE_TIME;
	}
	if (mouseedge_time > 0) {
	    mouseedge_time--;
	    if (mouseedge_time == 0) {
		mouseedge_x = 0;
		mouseedge_y = 0;
	    }
	}
    }

    for (i = 0; i < 2; i++) {

	v = mouse_delta[i][0] * pct / 100;
	mouse_x[i] += v;
	if (!mouse_deltanoreset[i][0])
	    mouse_delta[i][0] -= v;

	v = mouse_delta[i][1] * pct / 100;
	mouse_y[i] += v;
	if (!mouse_deltanoreset[i][1])
	    mouse_delta[i][1] -= v;

	v = mouse_delta[i][2] * pct / 100;
	if (v > 0)
	    record_key (0x7a << 1);
	else if (v < 0)
	    record_key (0x7b << 1);
	if (!mouse_deltanoreset[i][2])
	    mouse_delta[i][2] = 0;

	if (mouse_frame_x[i] - mouse_x[i] > 127)
	    mouse_x[i] = mouse_frame_x[i] - 127;
	if (mouse_frame_x[i] - mouse_x[i] < -127)
	    mouse_x[i] = mouse_frame_x[i] + 127;

	if (mouse_frame_y[i] - mouse_y[i] > 127)
	    mouse_y[i] = mouse_frame_y[i] - 127;
	if (mouse_frame_y[i] - mouse_y[i] < -127)
	    mouse_y[i] = mouse_frame_y[i] + 127;

	if (pct == 100) {
	    if (!mouse_deltanoreset[i][0])
		mouse_delta[i][0] = 0;
	    if (!mouse_deltanoreset[i][1])
		mouse_delta[i][1] = 0;
	    if (!mouse_deltanoreset[i][2])
		mouse_delta[i][2] = 0;
	    mouse_frame_x[i] = mouse_x[i];
	    mouse_frame_y[i] = mouse_y[i];
	}

    }
}

static int input_read, input_vpos;
extern int vpos;
static void readinput (void)
{
    if (!input_read && (vpos & ~31) != (input_vpos & ~31)) {
	idev[IDTYPE_JOYSTICK].read ();
	idev[IDTYPE_MOUSE].read ();
	mouseupdate ((vpos - input_vpos) * 100 / maxvpos);
	input_vpos = vpos;
    }
    if (input_read) {
	input_vpos = vpos;
	input_read = 0;
    }
}

int getjoystate (int joy)
{
    int left = 0, right = 0, top = 0, bot = 0;
    uae_u16 v = 0;

    if (inputdevice_logging > 2)
	write_log("JOY%dDAT %x\n", joy, m68k_getpc(&regs));
    readinput ();
    if (joydir[joy] & DIR_LEFT)
	left = 1;
    if (joydir[joy] & DIR_RIGHT)
	right = 1;
    if (joydir[joy] & DIR_UP)
	top = 1;
    if (joydir[joy] & DIR_DOWN)
	bot = 1;
    v = (uae_u8)mouse_x[joy] | (mouse_y[joy] << 8);
    if (left || right || top || bot || !mouse_port[joy]) {
	if (left)
	    top = !top;
	if (right)
	    bot = !bot;
	v &= ~0x0303;
	v |= bot | (right << 1) | (top << 8) | (left << 9);
    }
#ifdef DONGLE_DEBUG
    if (notinrom ())
	write_log ("JOY%dDAT %04.4X %s\n", joy, v, debuginfo (0));
#endif
    if (input_recording > 0 && oldjoy[joy] != v) {
	oldjoy[joy] = v;
	inprec_rstart(INPREC_JOYPORT);
	inprec_ru16(v);
	inprec_rend();
    } else if (input_recording < 0) {
	v = oldjoy[joy];
	if (inprec_pstart (INPREC_JOYPORT)) {
	    v = inprec_pu16();
	    inprec_pend();
	}
	oldjoy[joy] = v;
    }
    return v;
}

uae_u16 JOY0DAT (void)
{
    return getjoystate (0);
}
uae_u16 JOY1DAT (void)
{
    return getjoystate (1);
}

void JOYTEST (uae_u16 v)
{
    mouse_x[0] &= 3;
    mouse_y[0] &= 3;
    mouse_x[1] &= 3;
    mouse_y[1] &= 3;
    mouse_x[0] |= v & 0xFC;
    mouse_x[1] |= v & 0xFC;
    mouse_y[0] |= (v >> 8) & 0xFC;
    mouse_y[1] |= (v >> 8) & 0xFC;
    mouse_frame_x[0] = mouse_x[0];
    mouse_frame_y[0] = mouse_y[0];
    mouse_frame_x[1] = mouse_x[1];
    mouse_frame_y[1] = mouse_y[1];
//    write_log ("%d:%04.4X %p\n",vpos,v,m68k_getpc());
}

static uae_u8 parconvert (uae_u8 v, int jd, int shift)
{
    if (jd & DIR_UP)
	v &= ~(1 << shift);
    if (jd & DIR_DOWN)
	v &= ~(2 << shift);
    if (jd & DIR_LEFT)
	v &= ~(4 << shift);
    if (jd & DIR_RIGHT)
	v &= ~(8 << shift);
    return v;
}

/* io-pins floating: dir=1 -> return data, dir=0 -> always return 1 */
uae_u8 handle_parport_joystick (int port, uae_u8 pra, uae_u8 dra)
{
    uae_u8 v;
    switch (port)
    {
	case 0:
	v = (pra & dra) | (dra ^ 0xff);
	if (parport_joystick_enabled) {
	    v = parconvert (v, joydir[2], 0);
	    v = parconvert (v, joydir[3], 4);
	}
	return v;
	case 1:
	v = ((pra & dra) | (dra ^ 0xff)) & 0x7;
	if (parport_joystick_enabled) {
	    if (getbuttonstate (2, 0)) v &= ~4;
	    if (getbuttonstate (3, 0)) v &= ~1;
	}
	return v;
	default:
	abort ();
	return 0;
    }
}

uae_u8 handle_joystick_buttons (uae_u8 dra)
{
    uae_u8 but = 0;
    int i;

    for (i = 0; i < 2; i++) {
	if (cd32_pad_enabled[i]) {
	    uae_u16 p5dir = 0x0200 << (i * 4);
	    uae_u16 p5dat = 0x0100 << (i * 4);
	    but |= 0x40 << i;
	    /* Red button is connected to fire when p5 is 1 or floating */
	    if (!(potgo_value & p5dir) || ((potgo_value & p5dat) && (potgo_value & p5dir))) {
		if (getbuttonstate (i, JOYBUTTON_CD32_RED))
		    but &= ~(0x40 << i);
	    }
	} else if (!getbuttonstate (i, JOYBUTTON_1)) {
	    but |= 0x40 << i;
	}
    }
    //write_log("%02.2X:%02.2X %x\n", dra, but, M68K_GETPC);
    return but;
}

/* joystick 1 button 1 is used as a output for incrementing shift register */
void handle_cd32_joystick_cia (uae_u8 pra, uae_u8 dra)
{
    static int oldstate[2];
    int i;

    for (i = 0; i < 2; i++) {
	uae_u8 but = 0x40 << i;
	uae_u16 p5dir = 0x0200 << (i * 4); /* output enable P5 */
	uae_u16 p5dat = 0x0100 << (i * 4); /* data P5 */
	if (!(potgo_value & p5dir) || !(potgo_value & p5dat)) {
	    if ((dra & but) && (pra & but) != oldstate[i]) {
		if (!(pra & but)) {
		    cd32_shifter[i]--;
		    if (cd32_shifter[i] < 0)
			cd32_shifter[i] = 0;
		}
	    }
	}
	oldstate[i] = pra & but;
    }
}

/* joystick port 1 button 2 is input for button state */
static uae_u16 handle_joystick_potgor (uae_u16 potgor)
{
    int i;

    for (i = 0; i < 2; i++) {
	uae_u16 p9dir = 0x0800 << (i * 4); /* output enable P9 */
	uae_u16 p9dat = 0x0400 << (i * 4); /* data P9 */
	uae_u16 p5dir = 0x0200 << (i * 4); /* output enable P5 */
	uae_u16 p5dat = 0x0100 << (i * 4); /* data P5 */

	if (mouse_port[i]) {
	    /* official Commodore mouse has pull-up resistors in button lines
	     * NOTE: 3rd party mice may not have pullups! */
	    if (!(potgo_value & p5dir))
		potgor |= p5dat;
	    if (!(potgo_value & p9dir))
		potgor |= p9dat;
	}
	if (potgo_hsync < 0) {
	    /* first 10 or so lines after potgo has started
	     * forces input-lines to zero
	     */
	    if (!(potgo_value & p5dir))
		potgor &= ~p5dat;
	    if (!(potgo_value & p9dir))
		potgor &= ~p9dat;
	}

	if (cd32_pad_enabled[i]) {
	    /* p5 is floating in input-mode */
	    potgor &= ~p5dat;
	    potgor |= potgo_value & p5dat;
	    if (!(potgo_value & p9dir))
		potgor |= p9dat;
	    /* P5 output and 1 -> shift register is kept reset (Blue button) */
	    if ((potgo_value & p5dir) && (potgo_value & p5dat))
		cd32_shifter[i] = 8;
	    /* shift at 1 == return one, >1 = return button states */
	    if (cd32_shifter[i] == 0)
		potgor &= ~p9dat; /* shift at zero == return zero */
	    if (cd32_shifter[i] >= 2 && (joybutton[i] & ((1 << JOYBUTTON_CD32_PLAY) << (cd32_shifter[i] - 2))))
		potgor &= ~p9dat;
	} else {
	    if (getbuttonstate (i, JOYBUTTON_3))
		potgor &= ~p5dat;
	    if (getbuttonstate (i, JOYBUTTON_2))
		potgor &= ~p9dat;
	}
    }
    return potgor;
}

uae_u16 potgo_value;
static uae_u16 potdats[2];
static int inputdelay;

void inputdevice_hsync (void)
{
    int joy;

    for (joy = 0; joy < 2; joy++) {
	if (potgo_hsync >= 0) {
	    int active;

	    active = 0;
	    if ((potgo_value >> 9) & 1) /* output? */
		active = ((potgo_value >> 8) & 1) ? 0 : 1;
	    if (potgo_hsync < joydirpot[joy][0])
		active = 1;
	    if (getbuttonstate (joy, JOYBUTTON_3))
		active = 1;
	    if (active)
		potdats[joy] = ((potdats[joy] + 1) & 0xFF) | (potdats[joy] & 0xFF00);

	    active = 0;
	    if ((potgo_value >> 11) & 1) /* output? */
		active = ((potgo_value >> 10) & 1) ? 0 : 1;
	    if (potgo_hsync < joydirpot[joy][1])
		active = 1;
	    if (getbuttonstate (joy, JOYBUTTON_2))
		active = 1;
	    if (active)
		potdats[joy] += 0x100;
	}
    }
    potgo_hsync++;
    if (potgo_hsync > 255)
	potgo_hsync = 255;


#ifdef CATWEASEL
    catweasel_hsync ();
#endif
    if (inputdelay > 0) {
	inputdelay--;
	if (inputdelay == 0) {
	    idev[IDTYPE_JOYSTICK].read ();
	    idev[IDTYPE_KEYBOARD].read ();
	}
    }
}

uae_u16 POT0DAT (void)
{
    return potdats[0];
}
uae_u16 POT1DAT (void)
{
    return potdats[1];
}

/* direction=input, data pin floating, last connected logic level or previous status
 *                  written when direction was ouput
 *                  otherwise it is currently connected logic level.
 * direction=output, data pin is current value, forced to zero if joystick button is pressed
 * it takes some tens of microseconds before data pin changes state
 */

void POTGO (uae_u16 v)
{
    int i;

    if (potgo_logging)
	write_log ("W:%d: %04.4X %p\n", vpos, v, M68K_GETPC);
#ifdef DONGLE_DEBUG
    if (notinrom ())
	write_log ("POTGO %04.4X %s\n", v, debuginfo(0));
#endif
    potgo_value = potgo_value & 0x5500; /* keep state of data bits */
    potgo_value |= v & 0xaa00; /* get new direction bits */
    for (i = 0; i < 8; i += 2) {
	uae_u16 dir = 0x0200 << i;
	if (v & dir) {
	    uae_u16 data = 0x0100 << i;
	    potgo_value &= ~data;
	    potgo_value |= v & data;
	}
    }
    for (i = 0; i < 2; i++) {
	if (cd32_pad_enabled[i]) {
	    uae_u16 p5dir = 0x0200 << (i * 4); /* output enable P5 */
	    uae_u16 p5dat = 0x0100 << (i * 4); /* data P5 */
	    if ((potgo_value & p5dir) && (potgo_value & p5dat))
		cd32_shifter[i] = 8;
	}
    }
    if (v & 1) {
	potdats[0] = potdats[1] = 0;
	potgo_hsync = -15;
    }
}

uae_u16 POTGOR (void)
{
    uae_u16 v = handle_joystick_potgor (potgo_value) & 0x5500;
#ifdef DONGLE_DEBUG
    if (notinrom ())
	write_log ("POTGOR %04.4X %s\n", v, debuginfo(0));
#endif
    if (potgo_logging)
	write_log("R:%d:%04.4X %d %p\n", vpos, v, cd32_shifter[1], M68K_GETPC);
    return v;
}

static int check_input_queue (int event)
{
    struct input_queue_struct *iq;
    int i;
    for (i = 0; i < INPUT_QUEUE_SIZE; i++) {
	iq = &input_queue[i];
	if (iq->event == event) return i;
    }
    return -1;
}

static void queue_input_event (int event, int state, int max, int framecnt, int autofire)
{
    struct input_queue_struct *iq;
    int i = check_input_queue (event);

    if (event <= 0)
	return;
    if (state < 0 && i >= 0) {
	iq = &input_queue[i];
	iq->nextframecnt = -1;
	iq->framecnt = -1;
	iq->event = 0;
	if (iq->state == 0)
	    handle_input_event (event, 0, 1, 0);
    } else if (i < 0) {
	for (i = 0; i < INPUT_QUEUE_SIZE; i++) {
	    iq = &input_queue[i];
	    if (iq->framecnt < 0) break;
	}
	if (i == INPUT_QUEUE_SIZE) {
	    write_log ("input queue overflow\n");
	    return;
	}
	iq->event = event;
	iq->state = iq->storedstate = state;
	iq->max = max;
	iq->framecnt = framecnt;
	iq->nextframecnt = autofire > 0 ? framecnt : -1;
    }
}

static uae_u8 keybuf[256];
static int inputcode_pending, inputcode_pending_state;

 void inputdevice_release_all_keys (void)
 {
     int i;
 
     for (i = 0; i < 0x80; i++) {
 	if (keybuf[i] != 0) {
 	    keybuf[i] = 0;
 	    record_key (i << 1|1);
 	}
     }
 }
 

void inputdevice_add_inputcode (int code, int state)
{
    inputcode_pending = code;
    inputcode_pending_state = state;
}

void inputdevice_do_keyboard (int code, int state)
{
    if (code < 0x80) {
	uae_u8 key = code | (state ? 0x00 : 0x80);
	keybuf[key & 0x7f] = (key & 0x80) ? 0 : 1;
	if (((keybuf[AK_CTRL] || keybuf[AK_RCTRL]) && keybuf[AK_LAMI] && keybuf[AK_RAMI]) || key == AK_RESETWARNING) {
	    int r = keybuf[AK_LALT] | keybuf[AK_RALT];
	    memset (keybuf, 0, sizeof (keybuf));
	    uae_reset (r);
	}
	record_key ((uae_u8)((key << 1) | (key >> 7)));
	if (inputdevice_logging > 0)
	    write_log("Amiga key %02.2X %d\n", key & 0x7f, key >> 7);
	return;
    }
    inputdevice_add_inputcode (code, state);
}

void inputdevice_handle_inputcode (void)
{
    int code = inputcode_pending;
    int state = inputcode_pending_state;
    inputcode_pending = 0;
    if (code == 0)
	return;
    if (vpos != 0)
	write_log ("inputcode=%d but vpos = %d", code, vpos);

#ifdef ARCADIA
    switch (code)
    {
    case AKS_ARCADIADIAGNOSTICS:
	arcadia_flag &= ~1;
	arcadia_flag |= state ? 1 : 0;
	break;
    case AKS_ARCADIAPLY1:
	arcadia_flag &= ~4;
	arcadia_flag |= state ? 4 : 0;
	break;
    case AKS_ARCADIAPLY2:
	arcadia_flag &= ~2;
	arcadia_flag |= state ? 2 : 0;
	break;
    case AKS_ARCADIACOIN1:
	if (state)
	    arcadia_coin[0]++;
	break;
    case AKS_ARCADIACOIN2:
	if (state)
	    arcadia_coin[1]++;
	break;
    }
#endif

    if (!state)
	return;
    switch (code)
    {
    case AKS_ENTERGUI:
	gui_display (-1);
	break;
    case AKS_SCREENSHOT:
	screenshot(1, 1);
	break;
#ifdef ACTION_REPLAY
    case AKS_FREEZEBUTTON:
	action_replay_freeze ();
	break;
#endif
    case AKS_FLOPPY0:
	gui_display (0);
	break;
    case AKS_FLOPPY1:
	gui_display (1);
	break;
    case AKS_FLOPPY2:
	gui_display (2);
	break;
    case AKS_FLOPPY3:
	gui_display (3);
	break;
    case AKS_EFLOPPY0:
	disk_eject (0);
	break;
    case AKS_EFLOPPY1:
	disk_eject (1);
	break;
    case AKS_EFLOPPY2:
	disk_eject (2);
	break;
    case AKS_EFLOPPY3:
	disk_eject (3);
	break;
    case AKS_IRQ7:
	Interrupt (7);
	break;
    case AKS_PAUSE:
	pausemode (-1);
	break;
    case AKS_WARP:
	warpmode (-1);
	break;
    case AKS_INHIBITSCREEN:
	toggle_inhibit_frame (IHF_SCROLLLOCK);
	break;
    case AKS_STATEREWIND:
	savestate_dorewind(1);
	break;
    case AKS_VOLDOWN:
	sound_volume (-1);
	break;
    case AKS_VOLUP:
	sound_volume (1);
	break;
    case AKS_VOLMUTE:
	sound_volume (0);
	break;
    case AKS_QUIT:
	uae_quit ();
	break;
    case AKS_SOFTRESET:
	uae_reset (0);
	break;
    case AKS_HARDRESET:
	uae_reset (1);
	break;
    case AKS_STATESAVEQUICK:
    case AKS_STATESAVEQUICK1:
    case AKS_STATESAVEQUICK2:
    case AKS_STATESAVEQUICK3:
    case AKS_STATESAVEQUICK4:
    case AKS_STATESAVEQUICK5:
    case AKS_STATESAVEQUICK6:
    case AKS_STATESAVEQUICK7:
    case AKS_STATESAVEQUICK8:
    case AKS_STATESAVEQUICK9:
	savestate_quick ((code - AKS_STATESAVEQUICK) / 2, 1);
	break;
    case AKS_STATERESTOREQUICK:
    case AKS_STATERESTOREQUICK1:
    case AKS_STATERESTOREQUICK2:
    case AKS_STATERESTOREQUICK3:
    case AKS_STATERESTOREQUICK4:
    case AKS_STATERESTOREQUICK5:
    case AKS_STATERESTOREQUICK6:
    case AKS_STATERESTOREQUICK7:
    case AKS_STATERESTOREQUICK8:
    case AKS_STATERESTOREQUICK9:
	savestate_quick ((code - AKS_STATERESTOREQUICK) / 2, 0);
	break;
    case AKS_TOGGLEFULLSCREEN:
	toggle_fullscreen ();
	break;
    case AKS_TOGGLEMOUSEGRAB:
	toggle_mousegrab ();
	break;
    case AKS_ENTERDEBUGGER:
	activate_debugger ();
	break;
    case AKS_STATESAVEDIALOG:
	gui_display (5);
    break;
	case AKS_STATERESTOREDIALOG:
	gui_display (4);
	break;
    case AKS_DECREASEREFRESHRATE:
    case AKS_INCREASEREFRESHRATE:
    {
        int dir = code == AKS_INCREASEREFRESHRATE ? 5 : -5;
        if (currprefs.chipset_refreshrate == 0)
	    currprefs.chipset_refreshrate = currprefs.ntscmode ? 60 : 50;
	changed_prefs.chipset_refreshrate = currprefs.chipset_refreshrate + dir;
	if (changed_prefs.chipset_refreshrate < 10)
	    changed_prefs.chipset_refreshrate = 10;
	if (changed_prefs.chipset_refreshrate > 900)
	    changed_prefs.chipset_refreshrate = 900;
    }
    break;
    }
}

int handle_input_event (int nr, int state, int max, int autofire)
{
    struct inputevent *ie;
    int joy;

    if (nr <= 0)
	return 0;
    ie = &events[nr];
    if (inputdevice_logging > 0)
	write_log("'%s' %d %d\n", ie->name, state, max);
    if (autofire) {
	if (state)
	    queue_input_event (nr, state, max, currprefs.input_autofire_framecnt, 1);
	else
	    queue_input_event (nr, -1, 0, 0, 1);
    }
    switch (ie->unit)
    {
	case 5: /* lightpen/gun */
	{
	    int delta = state * currprefs.input_mouse_speed / 100;
	    if (ie->data)
		lightpen_y += delta;
	    else
		lightpen_x += delta;
	}
	break;
	case 1: /* ->JOY1 */
	case 2: /* ->JOY2 */
	case 3: /* ->Parallel port joystick adapter port #1 */
	case 4: /* ->Parallel port joystick adapter port #2 */
	    joy = ie->unit - 1;
	    if (ie->type & 4) {
		if (state)
		    joybutton[joy] |= 1 << ie->data;
		else
		    joybutton[joy] &= ~(1 << ie->data);
	    } else if (ie->type & 8) {
		/* real mouse / analog stick mouse emulation */
		int delta;
		int deadzone = currprefs.input_joymouse_deadzone * max / 100;
		if (max) {
		    if (state < deadzone && state > -deadzone) {
			state = 0;
		    } else if (state < 0) {
			state += deadzone;
		    } else {
			state -= deadzone;
		    }
		    max -= deadzone;
		    delta = state * currprefs.input_joymouse_multiplier / max;
		} else {
		    delta = state;
		}
		mouse_delta[joy][ie->data] += delta;

	    } else if (ie->type & 32) { /* button mouse emulation vertical */

		int speed = currprefs.input_joymouse_speed;
		if (state && (ie->data & DIR_UP)) {
		    mouse_delta[joy][1] = -speed;
		    mouse_deltanoreset[joy][1] = 1;
		} else if (state && (ie->data & DIR_DOWN)) {
		    mouse_delta[joy][1] = speed;
		    mouse_deltanoreset[joy][1] = 1;
		} else
		    mouse_deltanoreset[joy][1] = 0;

	    } else if (ie->type & 64) { /* button mouse emulation horizontal */

		int speed = currprefs.input_joymouse_speed;
		if (state && (ie->data & DIR_LEFT)) {
		    mouse_delta[joy][0] = -speed;
		    mouse_deltanoreset[joy][0] = 1;
		} else if (state && (ie->data & DIR_RIGHT)) {
		    mouse_delta[joy][0] = speed;
		    mouse_deltanoreset[joy][0] = 1;
		} else
		    mouse_deltanoreset[joy][0] = 0;

	    } else if (ie->type & 128) { /* analog (paddle) */
		int deadzone = currprefs.input_joymouse_deadzone * max / 100;
		if (max) {
		    if (state < deadzone && state > -deadzone) {
			state = 0;
		    } else if (state < 0) {
			state += deadzone;
		    } else {
			state -= deadzone;
		    }
		    state = state * max / (max - deadzone);
		}
		state = state / 256 + 128;
		joydirpot[joy][ie->data] = state;
	    } else {
		int left = oleft[joy], right = oright[joy], top = otop[joy], bot = obot[joy];
		if (ie->type & 16) {
		    /* button to axis mapping */
		    if (ie->data & DIR_LEFT) left = oleft[joy] = state ? 1 : 0;
		    if (ie->data & DIR_RIGHT) right = oright[joy] = state ? 1 : 0;
		    if (ie->data & DIR_UP) top = otop[joy] = state ? 1 : 0;
		    if (ie->data & DIR_DOWN) bot = obot[joy] = state ? 1 : 0;
		} else {
		    /* "normal" joystick axis */
		    int deadzone = currprefs.input_joystick_deadzone * max / 100;
		    int neg, pos;
		    if (state < deadzone && state > -deadzone)
			state = 0;
		    neg = state < 0 ? 1 : 0;
		    pos = state > 0 ? 1 : 0;
		    if (ie->data & DIR_LEFT) left = oleft[joy] = neg;
		    if (ie->data & DIR_RIGHT) right = oright[joy] = pos;
		    if (ie->data & DIR_UP) top = otop[joy] = neg;
		    if (ie->data & DIR_DOWN) bot = obot[joy] = pos;
		}
		joydir[joy] = 0;
		if (left) joydir[joy] |= DIR_LEFT;
		if (right) joydir[joy] |= DIR_RIGHT;
		if (top) joydir[joy] |= DIR_UP;
		if (bot) joydir[joy] |= DIR_DOWN;
	    }
	break;
	case 0: /* ->KEY */
	    inputdevice_do_keyboard (ie->data, state);
	break;
    }
    return 1;
}

void inputdevice_vsync (void)
{
    struct input_queue_struct *iq;
    int i;

    for (i = 0; i < INPUT_QUEUE_SIZE; i++) {
	iq = &input_queue[i];
	if (iq->framecnt > 0) {
	    iq->framecnt--;
	    if (iq->framecnt == 0) {
		if (iq->state)
		    iq->state = 0;
		else
		    iq->state = iq->storedstate;
		handle_input_event (iq->event, iq->state, iq->max, 0);
		iq->framecnt = iq->nextframecnt;
	    }
	}
    }
    mouseupdate (100);
    inputdelay = uaerand () % (maxvpos == 0 ? 1 : maxvpos - 1);
    idev[IDTYPE_MOUSE].read ();
    input_read = 1;
    input_vpos = 0;
    inputdevice_handle_inputcode ();
    if (ievent_alive > 0)
	ievent_alive--;
#ifdef ARCADIA
    if (arcadia_bios)
	arcadia_vsync ();
#endif
    mouseedge();
}

void inputdevice_reset (void)
{
    ievent_alive = 0;
    magicmouse_ibase = 0;
}

static int switchdevice(struct uae_input_device *id, int num)
{
#if 0
    struct inputevent *ie, *ie2;
    int i, j, k, event, unit;
#endif

    if (num >= 4)
	return 0;
    return 0;
    write_log ("switchdev %d\n", num);
    if (!currprefs.input_selected_setting)
	return 0;

#if 0
    id->enabled = 1;
    for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	    event = id->eventid[i][j];
	    if (event <= 0)
		continue;
	    ie = &events[event];
	    if (ie->unit <= 0)
		continue;
	    unit = ie->unit;
	    k = 1;
	    while (events[k].confname) {
		ie2 = &events[k];
		if (ie2->type == ie->type && ie2->data == ie->data && ie2->allow_mask == ie->allow_mask) {
		    id->eventid[i][j] = k;
		    break;
		}
		k++;
	    }
	}
    }
#endif
    return 0;
}

static void setbuttonstateall (struct uae_input_device *id, struct uae_input_device2 *id2, int button, int state)
{
    int event, autofire, i;
    uae_u32 mask = 1 << button;
    uae_u32 omask = id2->buttonmask & mask;
    uae_u32 nmask = (state ? 1 : 0) << button;
    char *custom;

    if (!id->enabled) {
	if (state)
	    switchdevice(id, button);
	return;
    }
    if (button >= ID_BUTTON_TOTAL)
	return;
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
	event = id->eventid[ID_BUTTON_OFFSET + button][sublevdir[state <= 0 ? 1 : 0][i]];
	custom = id->custom[ID_BUTTON_OFFSET + button][sublevdir[state <= 0 ? 1 : 0][i]];
	if (event <= 0 && custom == NULL)
	    continue;
	autofire = (id->flags[ID_BUTTON_OFFSET + button][sublevdir[state <= 0 ? 1 : 0][i]] & ID_FLAG_AUTOFIRE) ? 1 : 0;
	if (state < 0) {
	    handle_input_event (event, 1, 1, 0);
	    queue_input_event (event, 0, 1, 1, 0); /* send release event next frame */
	} else {
	    if ((omask ^ nmask) & mask)
		handle_input_event (event, state, 1, autofire);
	}
    }
    if ((omask ^ nmask) & mask) {
	if (state)
	    id2->buttonmask |= mask;
	else
	    id2->buttonmask &= ~mask;
    }
}


/* - detect required number of joysticks and mice from configuration data
 * - detect if CD32 pad emulation is needed
 * - detect device type in ports (mouse or joystick)
 */

static int iscd32 (int ei)
{
    if (ei >= INPUTEVENT_JOY1_CD32_FIRST && ei <= INPUTEVENT_JOY1_CD32_LAST) {
	cd32_pad_enabled[0] = 1;
	return 1;
    }
    if (ei >= INPUTEVENT_JOY2_CD32_FIRST && ei <= INPUTEVENT_JOY2_CD32_LAST) {
	cd32_pad_enabled[1] = 1;
	return 2;
    }
    return 0;
}

static int isparport (int ei)
{
    if (ei > INPUTEVENT_PAR_JOY1_START && ei < INPUTEVENT_PAR_JOY_END) {
	parport_joystick_enabled = 1;
	return 1;
    }
    return 0;
}

static int ismouse (int ei)
{
    if (ei >= INPUTEVENT_MOUSE1_FIRST && ei <= INPUTEVENT_MOUSE1_LAST) {
	mouse_port[0] = 1;
	return 1;
    }
    if (ei >= INPUTEVENT_MOUSE2_FIRST && ei <= INPUTEVENT_MOUSE2_LAST) {
	mouse_port[1] = 1;
	return 2;
    }
    return 0;
}

static void scanevents(struct uae_prefs *p)
{
    int i, j, k, ei;
    struct inputevent *e;
    int n_joy = idev[IDTYPE_JOYSTICK].get_num();
    int n_mouse = idev[IDTYPE_MOUSE].get_num();

    cd32_pad_enabled[0] = cd32_pad_enabled[1] = 0;
    parport_joystick_enabled = 0;
    mouse_port[0] = mouse_port[1] = 0;
    for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++)
	joydir[i] = 0;

    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	use_joysticks[i] = 0;
	use_mice[i] = 0;
	for (k = 0; k < MAX_INPUT_SUB_EVENT; k++) {
	    for (j = 0; j < ID_BUTTON_TOTAL; j++) {

		if (joysticks[i].enabled && i < n_joy) {
		    ei = joysticks[i].eventid[ID_BUTTON_OFFSET + j][k];
		    e = &events[ei];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		    if (joysticks[i].eventid[ID_BUTTON_OFFSET + j][k] > 0)
			use_joysticks[i] = 1;
		}
		if (mice[i].enabled && i < n_mouse) {
		    ei = mice[i].eventid[ID_BUTTON_OFFSET + j][k];
		    e = &events[ei];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		    if (mice[i].eventid[ID_BUTTON_OFFSET + j][k] > 0)
			use_mice[i] = 1;
		}

	    }

	    for (j = 0; j < ID_AXIS_TOTAL; j++) {

		if (joysticks[i].enabled && i < n_joy) {
		    ei = joysticks[i].eventid[ID_AXIS_OFFSET + j][k];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		    if (ei > 0)
			use_joysticks[i] = 1;
		}
		if (mice[i].enabled && i < n_mouse) {
		    ei = mice[i].eventid[ID_AXIS_OFFSET + j][k];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		    if (ei > 0)
			use_mice[i] = 1;
		}
	    }
	}
    }
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	use_keyboards[i] = 0;
	if (keyboards[i].enabled && i < idev[IDTYPE_KEYBOARD].get_num()) {
	    j = 0;
	    while (keyboards[i].extra[j][0] >= 0) {
		use_keyboards[i] = 1;
		for (k = 0; k < MAX_INPUT_SUB_EVENT; k++) {
		    ei = keyboards[i].eventid[j][k];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		}
		j++;
	    }
	}
    }
}

#ifdef CD32
static void setcd32 (int joy, int n)
{
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 0][0] = n ? INPUTEVENT_JOY2_CD32_RED : INPUTEVENT_JOY1_CD32_RED;
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 1][0] = n ? INPUTEVENT_JOY2_CD32_BLUE : INPUTEVENT_JOY1_CD32_BLUE;
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 2][0] = n ? INPUTEVENT_JOY2_CD32_YELLOW : INPUTEVENT_JOY1_CD32_YELLOW;
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 3][0] = n ? INPUTEVENT_JOY2_CD32_GREEN : INPUTEVENT_JOY1_CD32_GREEN;
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 4][0] = n ? INPUTEVENT_JOY2_CD32_FFW : INPUTEVENT_JOY1_CD32_FFW;
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 5][0] = n ? INPUTEVENT_JOY2_CD32_RWD : INPUTEVENT_JOY1_CD32_RWD;
    joysticks[joy].eventid[ID_BUTTON_OFFSET + 6][0] = n ? INPUTEVENT_JOY2_CD32_PLAY :  INPUTEVENT_JOY1_CD32_PLAY;
}
#endif

int compatibility_device[2];
static void compatibility_mode (struct uae_prefs *prefs)
{
    int joy, i;
    int used[4] = { 0, 0, 0, 0};

    compatibility_device[0] = -1;
    compatibility_device[1] = -1;
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	memset (&mice[i], 0, sizeof (*mice));
	memset (&joysticks[i], 0, sizeof (*joysticks));
    }

    if ((joy = jsem_ismouse (0, prefs)) >= 0) {
	mice[joy].eventid[ID_AXIS_OFFSET + 0][0] = INPUTEVENT_MOUSE1_HORIZ;
	mice[joy].eventid[ID_AXIS_OFFSET + 1][0] = INPUTEVENT_MOUSE1_VERT;
	mice[joy].eventid[ID_AXIS_OFFSET + 2][0] = INPUTEVENT_MOUSE1_WHEEL;
	mice[joy].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY1_FIRE_BUTTON;
	mice[joy].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY1_2ND_BUTTON;
	mice[joy].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY1_3RD_BUTTON;
	mice[joy].eventid[ID_BUTTON_OFFSET + 3][0] = INPUTEVENT_KEY_ALT_LEFT;
	mice[joy].eventid[ID_BUTTON_OFFSET + 3][1] = INPUTEVENT_KEY_CURSOR_LEFT;
	mice[joy].eventid[ID_BUTTON_OFFSET + 4][0] = INPUTEVENT_KEY_ALT_LEFT;
	mice[joy].eventid[ID_BUTTON_OFFSET + 4][1] = INPUTEVENT_KEY_CURSOR_RIGHT;
	mice[joy].enabled = 1;
    }
    if ((joy = jsem_ismouse (1, prefs)) >= 0) {
	mice[joy].eventid[ID_AXIS_OFFSET + 0][0] = INPUTEVENT_MOUSE2_HORIZ;
	mice[joy].eventid[ID_AXIS_OFFSET + 1][0] = INPUTEVENT_MOUSE2_VERT;
	mice[joy].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY2_FIRE_BUTTON;
	mice[joy].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY2_2ND_BUTTON;
	mice[joy].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY2_3RD_BUTTON;
	mice[joy].enabled = 1;
    }

    joy = jsem_isjoy (1, prefs);
    if (joy >= 0) {
	joysticks[joy].eventid[ID_AXIS_OFFSET + 0][0] = INPUTEVENT_JOY2_HORIZ;
	joysticks[joy].eventid[ID_AXIS_OFFSET + 1][0] = INPUTEVENT_JOY2_VERT;
	used[joy] = 1;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY2_FIRE_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY2_2ND_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY2_3RD_BUTTON;
#ifdef CD32
	if (currprefs.cs_cd32cd)
	    setcd32 (joy, 1);
#endif
	joysticks[joy].enabled = 1;
    }

    joy = jsem_isjoy (0, prefs);
    if (joy >= 0) {
	used[joy] = 1;
	joysticks[joy].eventid[ID_AXIS_OFFSET + 0][0] = INPUTEVENT_JOY1_HORIZ;
	joysticks[joy].eventid[ID_AXIS_OFFSET + 1][0] = INPUTEVENT_JOY1_VERT;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY1_FIRE_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY1_2ND_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY1_3RD_BUTTON;
	joysticks[joy].enabled = 1;
    }

    for (joy = 0; used[joy]; joy++);
    if (JSEM_ISANYKBD (0, prefs)) {
	joysticks[joy].eventid[ID_AXIS_OFFSET +  0][0] = INPUTEVENT_JOY1_HORIZ;
	joysticks[joy].eventid[ID_AXIS_OFFSET +  1][0] = INPUTEVENT_JOY1_VERT;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY1_FIRE_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY1_2ND_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY1_3RD_BUTTON;
	joysticks[joy].enabled = 1;
	used[joy] = 1;
	compatibility_device[0] = joy;
    }
    for (joy = 0; used[joy]; joy++);
    if (JSEM_ISANYKBD (1, prefs)) {
	joysticks[joy].eventid[ID_AXIS_OFFSET +  0][0] = INPUTEVENT_JOY2_HORIZ;
	joysticks[joy].eventid[ID_AXIS_OFFSET +  1][0] = INPUTEVENT_JOY2_VERT;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY2_FIRE_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY2_2ND_BUTTON;
	joysticks[joy].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY2_3RD_BUTTON;
#ifdef CD32
	if (currprefs.cs_cd32cd)
	    setcd32 (joy, 1);
#endif
	joysticks[joy].enabled = 1;
	used[joy] = 1;
	compatibility_device[1] = joy;
    }
}

void inputdevice_updateconfig (struct uae_prefs *prefs)
{
    int i;

    if (currprefs.jport0 != changed_prefs.jport0
	|| currprefs.jport1 != changed_prefs.jport1) {
	currprefs.jport0 = changed_prefs.jport0;
	currprefs.jport1 = changed_prefs.jport1;
    }
    joybutton[0] = joybutton[1] = 0;
    joydir[0] = joydir[1] = 0;
    oldmx[0] = oldmx[1] = -1;
    oldmy[0] = oldmy[1] = -1;
    cd32_shifter[0] = cd32_shifter[1] = 8;
    oleft[0] = oleft[1] = 0;
    oright[0] = oright[1] = 0;
    otop[0] = otop[1] = 0;
    obot[0] = obot[1] = 0;
    for (i = 0; i < 2; i++) {
	mouse_deltanoreset[i][0] = 0;
	mouse_delta[i][0] = 0;
	mouse_deltanoreset[i][1] = 0;
	mouse_delta[i][1] = 0;
	mouse_deltanoreset[i][2] = 0;
	mouse_delta[i][2] = 0;
    }
    memset (keybuf, 0, sizeof (keybuf));

    for (i = 0; i < INPUT_QUEUE_SIZE; i++)
	input_queue[i].framecnt = input_queue[i].nextframecnt = -1;

    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
	sublevdir[0][i] = i;
	sublevdir[1][i] = MAX_INPUT_SUB_EVENT - i - 1;
    }

    joysticks = prefs->joystick_settings[prefs->input_selected_setting];
    mice = prefs->mouse_settings[prefs->input_selected_setting];
    keyboards = prefs->keyboard_settings[prefs->input_selected_setting];

    memset (joysticks2, 0, sizeof (joysticks2));
    memset (mice2, 0, sizeof (mice2));
    if (prefs->input_selected_setting == 0)
	compatibility_mode (prefs);

    joystick_setting_changed ();

    scanevents (prefs);

#ifdef CD32
    if (currprefs.input_selected_setting == 0 && currprefs.cs_cd32cd)
	cd32_pad_enabled[1] = 1;
#endif

    if (mousehack_allowed())
        mousehack_enable();
}

static void set_kbr_default (struct uae_prefs *p, int index, int num)
{
    int i, j, k, l;
    struct uae_input_device_kbr_default *trans = keyboard_default;
    struct uae_input_device *kbr;
    struct inputdevice_functions *id = &idev[IDTYPE_KEYBOARD];
    uae_u32 scancode;

    if (!trans)
	return;
    for (j = 0; j < MAX_INPUT_DEVICES; j++) {
	kbr = &p->keyboard_settings[index][j];
	for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	    memset (kbr, 0, sizeof (struct uae_input_device));
	    kbr->extra[i][0] = -1;
	}
	if (j < id->get_num ()) {
	    if (j == 0)
		kbr->enabled = 1;
	    for (i = 0; i < id->get_widget_num (num); i++) {
		id->get_widget_type (num, i, 0, &scancode);
		kbr->extra[i][0] = scancode;
		l = 0;
		while (trans[l].scancode >= 0) {
		    if (kbr->extra[i][0] == trans[l].scancode) {
			for (k = 0; k < MAX_INPUT_SUB_EVENT; k++) {
			    if (kbr->eventid[i][k] == 0) break;
			}
			if (k == MAX_INPUT_SUB_EVENT) {
			    write_log ("corrupt default keyboard mappings\n");
			    return;
			}
			kbr->eventid[i][k] = trans[l].event;
			break;
		    }
		    l++;
		}
	    }
	}
    }
}

void inputdevice_default_prefs (struct uae_prefs *p)
{
    int i;

    inputdevice_init ();
    p->input_joymouse_multiplier = 20;
    p->input_joymouse_deadzone = 33;
    p->input_joystick_deadzone = 33;
    p->input_joymouse_speed = 10;
    p->input_mouse_speed = 100;
    p->input_autofire_framecnt = 10;
    for (i = 0; i <= MAX_INPUT_SETTINGS; i++) {
	set_kbr_default (p, i, 0);
	input_get_default_mouse (p->mouse_settings[i]);
	input_get_default_joystick (p->joystick_settings[i]);
    }
}

void inputdevice_setkeytranslation (struct uae_input_device_kbr_default *trans)
{
    keyboard_default = trans;
}

static int inputdevice_translatekeycode_2 (int keyboard, int scancode, int state)
{
    struct uae_input_device *na = &keyboards[keyboard];
    int j, k;
    int handled = 0;

    if (!keyboards || scancode < 0)
	return handled;
    j = 0;
    while (na->extra[j][0] >= 0) {
	if (na->extra[j][0] == scancode) {
	    for (k = 0; k < MAX_INPUT_SUB_EVENT; k++) {/* send key release events in reverse order */
		int autofire = (na->flags[j][sublevdir[state == 0 ? 1 : 0][k]] & ID_FLAG_AUTOFIRE) ? 1 : 0;
		int event = na->eventid[j][sublevdir[state == 0 ? 1 : 0][k]];
		char *custom = na->custom[j][sublevdir[state == 0 ? 1 : 0][k]];
		handled |= handle_input_event (event, state, 1, autofire);
	    }
	    return handled;
	}
	j++;
    }
    return handled;
}

#define IECODE_UP_PREFIX 0x80
#define RAW_STEALTH 0x68
#define STEALTHF_E0KEY 0x08
#define STEALTHF_UPSTROKE 0x04
#define STEALTHF_SPECIAL 0x02
#define STEALTHF_E1KEY 0x01

static void sendmmcodes(int code, int newstate)
{
    uae_u8 b;

    b = RAW_STEALTH | IECODE_UP_PREFIX;
    record_key(((b << 1) | (b >> 7)) & 0xff);
    b = IECODE_UP_PREFIX;
    if ((code >> 8) == 0x01)
	b |= STEALTHF_E0KEY;
    if ((code >> 8) == 0x02)
	b |= STEALTHF_E1KEY;
    if (!newstate)
	b |= STEALTHF_UPSTROKE;
    record_key(((b << 1) | (b >> 7)) & 0xff);
    b = ((code >> 4) & 0x0f) | IECODE_UP_PREFIX;
    record_key(((b << 1) | (b >> 7)) & 0xff);
    b = (code & 0x0f) | IECODE_UP_PREFIX;
    record_key(((b << 1) | (b >> 7)) & 0xff);
}

int inputdevice_translatekeycode (int keyboard, int scancode, int state)
{
    if (inputdevice_translatekeycode_2 (keyboard, scancode, state))
	return 1;
    if (currprefs.mmkeyboard)
	sendmmcodes(scancode, state);
    return 0;
}

static struct inputdevice_functions idev[3];

void inputdevice_init (void)
{
    idev[IDTYPE_JOYSTICK] = inputdevicefunc_joystick;
    idev[IDTYPE_JOYSTICK].init ();
    idev[IDTYPE_MOUSE] = inputdevicefunc_mouse;
    idev[IDTYPE_MOUSE].init ();
    idev[IDTYPE_KEYBOARD] = inputdevicefunc_keyboard;
    idev[IDTYPE_KEYBOARD].init ();
}

void inputdevice_close (void)
{
    idev[IDTYPE_JOYSTICK].close ();
    idev[IDTYPE_MOUSE].close ();
    idev[IDTYPE_KEYBOARD].close ();
    inprec_close ();
}

static struct uae_input_device *get_uid (struct inputdevice_functions *id, int devnum)
{
    struct uae_input_device *uid = 0;
    if (id == &idev[IDTYPE_JOYSTICK]) {
	uid = &joysticks[devnum];
    } else if (id == &idev[IDTYPE_MOUSE]) {
	uid = &mice[devnum];
    } else if (id == &idev[IDTYPE_KEYBOARD]) {
	uid = &keyboards[devnum];
    }
    return uid;
}

static int get_event_data (struct inputdevice_functions *id, int devnum, int num, int *eventid, char **custom, int *flags, int sub)
{
    struct uae_input_device *uid = get_uid (id, devnum);
    int type = id->get_widget_type (devnum, num, 0, 0);
    int i;
    if (type == IDEV_WIDGET_BUTTON) {
	i = num - id->get_widget_first (devnum, type) + ID_BUTTON_OFFSET;
	*eventid = uid->eventid[i][sub];
	*flags = uid->flags[i][sub];
	*custom = uid->custom[i][sub];
	return i;
    } else if (type == IDEV_WIDGET_AXIS) {
	i = num - id->get_widget_first (devnum, type) + ID_AXIS_OFFSET;
	*eventid = uid->eventid[i][sub];
	*flags = uid->flags[i][sub];
	*custom = uid->custom[i][sub];
	return i;
    } else if (type == IDEV_WIDGET_KEY) {
	i = num - id->get_widget_first (devnum, type);
	*eventid = uid->eventid[i][sub];
	*flags = uid->flags[i][sub];
	*custom = uid->custom[i][sub];
	return i;
    }
    return -1;
}

static int put_event_data (struct inputdevice_functions *id, int devnum, int num, int eventid, char *custom, int flags, int sub)
{
    struct uae_input_device *uid = get_uid (id, devnum);
    int type = id->get_widget_type (devnum, num, 0, 0);
    int i;
    if (type == IDEV_WIDGET_BUTTON) {
	i = num - id->get_widget_first (devnum, type) + ID_BUTTON_OFFSET;
	uid->eventid[i][sub] = eventid;
	uid->flags[i][sub] = flags;
	xfree (uid->custom[i][sub]);
	uid->custom[i][sub] = custom ? my_strdup (custom) : NULL;
	return i;
    } else if (type == IDEV_WIDGET_AXIS) {
	i = num - id->get_widget_first (devnum, type) + ID_AXIS_OFFSET;
	uid->eventid[i][sub] = eventid;
	uid->flags[i][sub] = flags;
	xfree (uid->custom[i][sub]);
	uid->custom[i][sub] = custom ? my_strdup (custom) : NULL;
	return i;
    } else if (type == IDEV_WIDGET_KEY) {
	i = num - id->get_widget_first (devnum, type);
	uid->eventid[i][sub] = eventid;
	uid->flags[i][sub] = flags;
	xfree (uid->custom[i][sub]);
	uid->custom[i][sub] = custom ? my_strdup (custom) : NULL;
	return i;
    }
    return -1;
}

static int is_event_used (struct inputdevice_functions *id, int devnum, int isnum, int isevent)
{
    struct uae_input_device *uid = get_uid (id, devnum);
    int num, event, flag, sub;
    char *custom;

    for (num = 0; num < id->get_widget_num (devnum); num++) {
	for (sub = 0; sub < MAX_INPUT_SUB_EVENT; sub++) {
	    if (get_event_data (id, devnum, num, &event, &custom, &flag, sub) >= 0) {
		if (event == isevent && isnum != num)
		    return 1;
	    }
	}
    }
    return 0;
}

int inputdevice_get_device_index (int devnum)
{
    if (devnum < idev[IDTYPE_JOYSTICK].get_num())
	return devnum;
    else if (devnum < idev[IDTYPE_JOYSTICK].get_num() + idev[IDTYPE_MOUSE].get_num())
	return devnum - idev[IDTYPE_JOYSTICK].get_num();
    else if (devnum < idev[IDTYPE_JOYSTICK].get_num() + idev[IDTYPE_MOUSE].get_num() + idev[IDTYPE_KEYBOARD].get_num())
	return devnum - idev[IDTYPE_JOYSTICK].get_num() - idev[IDTYPE_MOUSE].get_num();
    else
	return -1;
}

static int gettype (int devnum)
{
    if (devnum < idev[IDTYPE_JOYSTICK].get_num())
	return IDTYPE_JOYSTICK;
    else if (devnum < idev[IDTYPE_JOYSTICK].get_num() + idev[IDTYPE_MOUSE].get_num())
	return IDTYPE_MOUSE;
    else if (devnum < idev[IDTYPE_JOYSTICK].get_num() + idev[IDTYPE_MOUSE].get_num() + idev[IDTYPE_KEYBOARD].get_num())
	return IDTYPE_KEYBOARD;
    else
	return -1;
}

static struct inputdevice_functions *getidf (int devnum)
{
    return &idev[gettype (devnum)];
}


/* returns number of devices of type "type" */
int inputdevice_get_device_total (int type)
{
    return idev[type].get_num ();
}
/* returns the name of device */
char *inputdevice_get_device_name (int type, int devnum)
{
    return idev[type].get_name (devnum);
}
/* returns state (enabled/disabled) */
int inputdevice_get_device_status (int devnum)
{
    struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    return uid->enabled;
}

/* set state (enabled/disabled) */
void inputdevice_set_device_status (int devnum, int enabled)
{
    struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    uid->enabled = enabled;
}

/* returns number of axis/buttons and keys from selected device */
int inputdevice_get_widget_num (int devnum)
{
    struct inputdevice_functions *idf = getidf (devnum);
    return idf->get_widget_num (inputdevice_get_device_index (devnum));
}

static void get_ename (struct inputevent *ie, char *out)
{
    if (!out)
	return;
    if (ie->allow_mask == AM_K)
	sprintf (out, "%s (0x%02.2X)", ie->name, ie->data);
    else
	strcpy (out, ie->name);
}

int inputdevice_iterate (int devnum, int num, char *name, int *af)
{
    struct inputdevice_functions *idf = getidf (devnum);
    static int id_iterator;
    struct inputevent *ie;
    int mask, data, flags, type;
    int devindex = inputdevice_get_device_index (devnum);
    char *custom;

    *af = 0;
    *name = 0;
    for (;;) {
	ie = &events[++id_iterator];
	if (!ie->confname) {
	    id_iterator = 0;
	    return 0;
	}
	mask = 0;
	type = idf->get_widget_type (devindex, num, 0, 0);
	if (type == IDEV_WIDGET_BUTTON) {
	    if (idf == &idev[IDTYPE_JOYSTICK]) {
		mask |= AM_JOY_BUT;
	    } else {
		mask |= AM_MOUSE_BUT;
	    }
	} else if (type == IDEV_WIDGET_AXIS) {
	    if (idf == &idev[IDTYPE_JOYSTICK]) {
		mask |= AM_JOY_AXIS;
	    } else {
		mask |= AM_MOUSE_AXIS;
	    }
	} else if (type == IDEV_WIDGET_KEY) {
	    mask |= AM_K;
	}
	if (ie->allow_mask & AM_INFO) {
	    struct inputevent *ie2 = ie + 1;
	    while (!(ie2->allow_mask & AM_INFO)) {
		if (is_event_used (idf, devindex, ie2 - ie, -1)) {
		    ie2++;
		    continue;
		}
		if (ie2->allow_mask & mask) break;
		ie2++;
	    }
	    if (!(ie2->allow_mask & AM_INFO))
		mask |= AM_INFO;
	}
	if (!(ie->allow_mask & mask))
	    continue;
	get_event_data (idf, devindex, num, &data, &custom, &flags, 0);
	get_ename (ie, name);
	*af = (flags & ID_FLAG_AUTOFIRE) ? 1 : 0;
	return 1;
    }
}

int inputdevice_get_mapped_name (int devnum, int num, int *pflags, char *name, char *custom, int sub)
{
    struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    int flags = 0, flag, data;
    int devindex = inputdevice_get_device_index (devnum);
    char *customp = NULL;

    if (name)
	strcpy (name, "<none>");
    if (custom)
	custom[0] = 0;
    if (pflags)
	*pflags = 0;
    if (uid == 0 || num < 0)
	return 0;
    if (get_event_data (idf, devindex, num, &data, &customp, &flag, sub) < 0)
	return 0;
    if (customp && custom)
	sprintf (custom, "\"%s\"", customp);
    if (flag & ID_FLAG_AUTOFIRE)
	flags |= IDEV_MAPPED_AUTOFIRE_SET;
    if (!data)
	return 0;
    if (events[data].allow_mask & AM_AF)
	flags |= IDEV_MAPPED_AUTOFIRE_POSSIBLE;
    if (pflags)
	*pflags = flags;
    get_ename (&events[data], name);
    return data;
}

int inputdevice_set_mapping (int devnum, int num, char *name, char *custom, int af, int sub)
{
    struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    int eid, data, flag, amask;
    char ename[256];
    int devindex = inputdevice_get_device_index (devnum);

    if (uid == 0 || num < 0)
	return 0;
    if (name) {
	eid = 1;
	while (events[eid].name) {
	    get_ename (&events[eid], ename);
	    if (!strcmp(ename, name)) break;
	    eid++;
	}
	if (!events[eid].name)
	    return 0;
	if (events[eid].allow_mask & AM_INFO)
	    return 0;
    } else {
	eid = 0;
    }
    if (get_event_data (idf, devindex, num, &data, &custom, &flag, sub) < 0)
	return 0;
    if (data >= 0) {
	amask = events[eid].allow_mask;
	flag &= ~ID_FLAG_AUTOFIRE;
	if (amask & AM_AF)
	    flag |= af ? ID_FLAG_AUTOFIRE : 0;
	put_event_data (idf, devindex, num, eid, custom, flag, sub);
	return 1;
    }
    return 0;
}

int inputdevice_get_widget_type (int devnum, int num, char *name)
{
    struct inputdevice_functions *idf = getidf (devnum);
    return idf->get_widget_type (inputdevice_get_device_index (devnum), num, name, 0);
}

static int config_change;

void inputdevice_config_change (void)
{
    config_change = 1;
}

int inputdevice_config_change_test (void)
{
    int v = config_change;
    config_change = 0;
    return v;
}

void inputdevice_copyconfig (struct uae_prefs *src, struct uae_prefs *dst)
{
    int i, j;

    dst->input_selected_setting = src->input_selected_setting;
    dst->input_joymouse_multiplier = src->input_joymouse_multiplier;
    dst->input_joymouse_deadzone = src->input_joymouse_deadzone;
    dst->input_joystick_deadzone = src->input_joystick_deadzone;
    dst->input_joymouse_speed = src->input_joymouse_speed;
    dst->input_mouse_speed = src->input_mouse_speed;
    dst->input_autofire_framecnt = src->input_autofire_framecnt;
    dst->jport0 = src->jport0;
    dst->jport1 = src->jport1;

    for (i = 0; i < MAX_INPUT_SETTINGS + 1; i++) {
	for (j = 0; j < MAX_INPUT_DEVICES; j++) {
	    memcpy (&dst->joystick_settings[i][j], &src->joystick_settings[i][j], sizeof (struct uae_input_device));
	    memcpy (&dst->mouse_settings[i][j], &src->mouse_settings[i][j], sizeof (struct uae_input_device));
	    memcpy (&dst->keyboard_settings[i][j], &src->keyboard_settings[i][j], sizeof (struct uae_input_device));
	}
    }

    inputdevice_updateconfig (dst);
}

void inputdevice_swap_ports (struct uae_prefs *p, int devnum)
{
    struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    int i, j, k, event, unit;
    struct inputevent *ie, *ie2;

    for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	    event = uid->eventid[i][j];
	    if (event <= 0)
		continue;
	    ie = &events[event];
	    if (ie->unit <= 0)
		continue;
	    unit = ie->unit;
	    k = 1;
	    while (events[k].confname) {
		ie2 = &events[k];
		if (ie2->type == ie->type && ie2->data == ie->data && ie2->unit - 1 == ((ie->unit - 1) ^ 1) && ie2->allow_mask == ie->allow_mask) {
		    uid->eventid[i][j] = k;
		    break;
		}
		k++;
	    }
	}
    }
}

void inputdevice_copy_single_config (struct uae_prefs *p, int src, int dst, int devnum)
{
    if (src == dst)
	return;
    if (devnum < 0 || gettype (devnum) == IDTYPE_JOYSTICK)
	memcpy (p->joystick_settings[dst], p->joystick_settings[src], sizeof (struct uae_input_device) * MAX_INPUT_DEVICES);
    if (devnum < 0 || gettype (devnum) == IDTYPE_MOUSE)
	memcpy (p->mouse_settings[dst], p->mouse_settings[src], sizeof (struct uae_input_device) * MAX_INPUT_DEVICES);
    if (devnum < 0 || gettype (devnum) == IDTYPE_KEYBOARD)
	memcpy (p->keyboard_settings[dst], p->keyboard_settings[src], sizeof (struct uae_input_device) * MAX_INPUT_DEVICES);
}

void inputdevice_acquire (void)
{
    int i;

    inputdevice_unacquire ();
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (use_joysticks[i])
	    idev[IDTYPE_JOYSTICK].acquire (i, 0);
    }
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (use_mice[i])
	    idev[IDTYPE_MOUSE].acquire (i, 0);
    }
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (use_keyboards[i])
	    idev[IDTYPE_KEYBOARD].acquire (i, 0);
    }
}

void inputdevice_unacquire (void)
{
    int i;

    for (i = 0; i < MAX_INPUT_DEVICES; i++)
	idev[IDTYPE_JOYSTICK].unacquire (i);
    for (i = 0; i < MAX_INPUT_DEVICES; i++)
	idev[IDTYPE_MOUSE].unacquire (i);
    for (i = 0; i < MAX_INPUT_DEVICES; i++)
	idev[IDTYPE_KEYBOARD].unacquire (i);
}

static int ignoreoldinput(int joy)
{
    if (currprefs.input_selected_setting == 0) {
        if (jsem_isjoy (0, &currprefs) != joy && jsem_isjoy (1, &currprefs) != joy)
	    return 1;
    }
    return 0;
}

void do_fake_joystick(int nr, int *fake)
{
    struct uae_input_device *id1;
    struct uae_input_device2 *id2;
    int state, i, axis;

    nr = compatibility_device[nr];
    id1 = &joysticks[nr];
    id2 = &joysticks2[nr];

    axis = 0;
    state = fake[1] ? -100 : (fake[2] ? 100 : 0);
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	handle_input_event (id1->eventid[ID_AXIS_OFFSET + axis][i], state, 100,
	    id1->flags[ID_AXIS_OFFSET + axis][i]);
    id2->states[axis] = state;

    axis = 1;
    state = fake[0] ? -100 : (fake[3] ? 100 : 0);
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	handle_input_event (id1->eventid[ID_AXIS_OFFSET + axis][i], state, 100,
	    id1->flags[ID_AXIS_OFFSET + axis][i]);
    id2->states[axis] = state;

    setbuttonstateall (id1, id2, 0, fake[4] ? 1 : 0);
    setbuttonstateall (id1, id2, 1, fake[5] ? 1 : 0);
    setbuttonstateall (id1, id2, 2, fake[6] ? 1 : 0);
}

/* Call this function when host machine's joystick/joypad/etc button state changes
 * This function translates button events to Amiga joybutton/joyaxis/keyboard events
 */

/* button states:
 * state = -1 -> mouse wheel turned or similar (button without release)
 * state = 1 -> button pressed
 * state = 0 -> button released
 */

void setjoybuttonstate (int joy, int button, int state)
{
    if (ignoreoldinput(joy)) {
	if (state)
	    switchdevice(&joysticks[joy], button);
	return;
    }
    setbuttonstateall (&joysticks[joy], &joysticks2[joy], button, state ? 1 : 0);
}

/* buttonmask = 1 = normal toggle button, 0 = mouse wheel turn or similar
 */
void setjoybuttonstateall (int joy, uae_u32 buttonbits, uae_u32 buttonmask)
{
    int i;

    if (ignoreoldinput(joy))
	return;
    for (i = 0; i < ID_BUTTON_TOTAL; i++) {
	if (buttonmask & (1 << i))
	    setbuttonstateall (&joysticks[joy], &joysticks2[joy], i, (buttonbits & (1 << i)) ? 1 : 0);
	else if (buttonbits & (1 << i))
	    setbuttonstateall (&joysticks[joy], &joysticks2[joy], i, -1);
    }
}
/* mouse buttons (just like joystick buttons)
 */
void setmousebuttonstateall (int mouse, uae_u32 buttonbits, uae_u32 buttonmask)
{
    int i;

    for (i = 0; i < ID_BUTTON_TOTAL; i++) {
	if (buttonmask & (1 << i))
	    setbuttonstateall (&mice[mouse], &mice2[mouse], i, (buttonbits & (1 << i)) ? 1 : 0);
	else if (buttonbits & (1 << i))
	    setbuttonstateall (&mice[mouse], &mice2[mouse], i, -1);
    }
}

void setmousebuttonstate (int mouse, int button, int state)
{
    setbuttonstateall (&mice[mouse], &mice2[mouse], button, state);
}

/* same for joystick axis (analog or digital)
 * (0 = center, -max = full left/top, max = full right/bottom)
 */
void setjoystickstate (int joy, int axis, int state, int max)
{
    struct uae_input_device *id = &joysticks[joy];
    struct uae_input_device2 *id2 = &joysticks2[joy];
    int deadzone = currprefs.input_joymouse_deadzone * max / 100;
    int i, v1, v2;

    v1 = state;
    v2 = id2->states[axis];
    if (v1 < deadzone && v1 > -deadzone)
	v1 = 0;
    if (v2 < deadzone && v2 > -deadzone)
	v2 = 0;
    if (v1 == v2)
	return;
    if (!joysticks[joy].enabled || ignoreoldinput(joy)) {
	if (v1)
	    switchdevice(&joysticks[joy], axis * 2 + (v1 < 0 ? 0 : 1));
	return;
    }
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	handle_input_event (id->eventid[ID_AXIS_OFFSET + axis][i], state, max,
	    id->flags[ID_AXIS_OFFSET + axis][i]);
    id2->states[axis] = state;
}
int getjoystickstate(int joy)
{
    return joysticks[joy].enabled;
}

void setmousestate (int mouse, int axis, int data, int isabs)
{
    int i, v;
    double *mouse_p, *oldm_p, d, diff;
    struct uae_input_device *id = &mice[mouse];
    static double fract1[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];
    static double fract2[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];

    if (!mice[mouse].enabled)
	return;
    d = 0;
    mouse_p = &mouse_axis[mouse][axis];
    oldm_p = &oldm_axis[mouse][axis];
    if (!isabs) {
	*oldm_p = *mouse_p;
	*mouse_p += data;
	d = (*mouse_p - *oldm_p) * currprefs.input_mouse_speed / 100.0;
    } else {
	d = data - (int)(*oldm_p);
	*oldm_p = data;
	*mouse_p += d;
	if (axis == 0)
	    lastmx = data;
	else
	    lastmy = data;
    }
    v = (int)(d > 0 ? d + 0.5 : d - 0.5);
    fract1[mouse][axis] += d;
    fract2[mouse][axis] += v;
    diff = fract2[mouse][axis] - fract1[mouse][axis];
    if (diff > 1 || diff < -1) {
	v -= (int)diff;
	fract2[mouse][axis] -= diff;
    }
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	handle_input_event (id->eventid[ID_AXIS_OFFSET + axis][i], v, 0, 0);
    mousehack_helper();
}
int getmousestate(int joy)
{
    return mice[joy].enabled;
}

void warpmode (int mode)
{
    if (mode < 0) {
	if (turbo_emulation) {
	    changed_prefs.gfx_framerate = currprefs.gfx_framerate = turbo_emulation;
	    turbo_emulation = 0;
	}  else {
	    turbo_emulation = currprefs.gfx_framerate;
	}
    } else if (mode == 0 && turbo_emulation > 0) {
	changed_prefs.gfx_framerate = currprefs.gfx_framerate = turbo_emulation;
	turbo_emulation = 0;
    } else if (mode > 0 && !turbo_emulation) {
	turbo_emulation = currprefs.gfx_framerate;
    }
    if (turbo_emulation) {
	if (!currprefs.cpu_cycle_exact && !currprefs.blitter_cycle_exact)
	    changed_prefs.gfx_framerate = currprefs.gfx_framerate = 10;
	pause_sound ();
    } else {
	resume_sound ();
    }
    compute_vsynctime ();
}

void pausemode (int mode)
{
    if (mode < 0)
	pause_emulation = pause_emulation ? 0 : 1;
    else
	pause_emulation = mode;
}

int jsem_isjoy (int port, struct uae_prefs *p)
{
    int v = JSEM_DECODEVAL (port, p);
    if (v < JSEM_JOYS)
	return -1;
    v -= JSEM_JOYS;
    if (v >= inputdevice_get_device_total (IDTYPE_JOYSTICK))
	return -1;
    return v;
}
int jsem_ismouse (int port, struct uae_prefs *p)
{
    int v = JSEM_DECODEVAL (port, p);
    if (v < JSEM_MICE)
	return -1;
    v -= JSEM_MICE;
    if (v >= inputdevice_get_device_total (IDTYPE_MOUSE))
	return -1;
    return v;
}
int jsem_iskbdjoy (int port, struct uae_prefs *p)
{
    int v = JSEM_DECODEVAL (port, p);
    if (v < JSEM_KBDLAYOUT)
	return -1;
    v -= JSEM_KBDLAYOUT;
    if (v >= JSEM_LASTKBD)
	return -1;
    return v;
}

extern int jsem_ismouse (int v, struct uae_prefs*);
extern int jsem_iskbd (int v, struct uae_prefs*);
