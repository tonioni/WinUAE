 /*
  * UAE - The Un*x Amiga Emulator
  *
  * joystick/mouse emulation
  *
  * Copyright 2001-2008 Toni Wilen
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
#include "audio.h"
#include "sound.h"
#include "savestate.h"
#include "arcadia.h"
#include "zfile.h"
#include "cia.h"
#include "autoconf.h"
#include "rp.h"

// 01 = host events
// 02 = joystick
// 04 = cia buttons
// 16 = potgo
// 32 = vsync

int inputdevice_logging = 0;

#define DIR_LEFT 1
#define DIR_RIGHT 2
#define DIR_UP 4
#define DIR_DOWN 8

struct inputevent {
    const TCHAR *confname;
    const TCHAR *name;
    int allow_mask;
    int type;
    int unit;
    int data;
};

#define IE_INVERT 0x80

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
#define AM_CUSTOM 256 /* custom event */
#define AM_K (AM_KEY|AM_JOY_BUT|AM_MOUSE_BUT|AM_AF) /* generic button/switch */

/* event flags */
#define ID_FLAG_AUTOFIRE 1

#define DEFEVENT(A, B, C, D, E, F) {L#A, B, C, D, E, F },
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

static int input_acquired;
static int testmode;

static uae_u8 *inprec_buffer, *inprec_p;
static struct zfile *inprec_zf;
static int inprec_size;
int input_recording = 0;
static uae_u8 *inprec_plast, *inprec_plastptr;
static int inprec_div;

static uae_u32 oldbuttons[4];
static uae_u16 oldjoy[2];

int inprec_open (TCHAR *fname, int record)
{
    uae_u32 t = (uae_u32)time(0);
    int i;

    inprec_close();
    inprec_zf = zfile_fopen (fname, record > 0 ? L"wb" : L"rb");
    if (inprec_zf == NULL)
	return 0;
    inprec_size = 10000;
    inprec_div = 1;
    if (record < 0) {
	uae_u32 id;
	zfile_fseek (inprec_zf, 0, SEEK_END);
	inprec_size = zfile_ftell (inprec_zf);
	zfile_fseek (inprec_zf, 0, SEEK_SET);
	inprec_buffer = inprec_p = (uae_u8*)xmalloc (inprec_size);
	zfile_fread (inprec_buffer, inprec_size, 1, inprec_zf);
	inprec_plastptr = inprec_buffer;
	id = inprec_pu32();
	if (id != 'UAE\0') {
	    inprec_close ();
	    return 0;
	}
	inprec_pu32();
	t = inprec_pu32 ();
	i = inprec_pu32 ();
	while (i-- > 0)
	    inprec_pu8 ();
	inprec_p = inprec_plastptr;
	oldbuttons[0] = oldbuttons[1] = oldbuttons[2] = oldbuttons[3] = 0;
	oldjoy[0] = oldjoy[1] = 0;
	if (record < -1)
	    inprec_div = maxvpos;
    } else if (record > 0) {
	inprec_buffer = inprec_p = xmalloc (inprec_size);
	inprec_ru32 ('UAE\0');
	inprec_ru8 (1);
	inprec_ru8 (UAEMAJOR);
	inprec_ru8 (UAEMINOR);
	inprec_ru8 (UAESUBREV);
	inprec_ru32 (t);
	inprec_ru32 (0); // extra header size
    } else {
	return 0;
    }
    input_recording = record;
    srand(t);
    CIA_inprec_prepare ();
    write_log (L"inprec initialized '%s', mode=%d\n", fname, input_recording);
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
    write_log (L"inprec finished\n");
}

void inprec_ru8(uae_u8 v)
{
    *inprec_p++= v;
}
void inprec_ru16 (uae_u16 v)
{
    inprec_ru8 ((uae_u8)(v >> 8));
    inprec_ru8 ((uae_u8)v);
}
void inprec_ru32 (uae_u32 v)
{
    inprec_ru16 ((uae_u16)(v >> 16));
    inprec_ru16 ((uae_u16)v);
}
void inprec_rstr (const TCHAR *src)
{
    char *s = ua (src);
    while(*s) {
	inprec_ru8 (*s);
	s++;
    }
    inprec_ru8 (0);
    xfree (s);
}
void inprec_rstart (uae_u8 type)
{
    write_log (L"INPREC: %08X: %d\n", hsync_counter, type);
    inprec_ru32 (hsync_counter);
    inprec_ru8 (0);
    inprec_plast = inprec_p;
    inprec_ru8 (0xff);
    inprec_ru8 (type);
}
void inprec_rend (void)
{
    *inprec_plast = inprec_p - (inprec_plast + 2);
    if (inprec_p >= inprec_buffer + inprec_size - 256) {
	zfile_fwrite (inprec_buffer, inprec_p - inprec_buffer, 1, inprec_zf);
	inprec_p = inprec_buffer;
    }
}

int inprec_pstart (uae_u8 type)
{
    uae_u8 *p = inprec_p;
    uae_u32 hc = hsync_counter;
    static uae_u8 *lastp;
    uae_u32 hc_orig, hc2_orig;

    if (savestate_state)
	return 0;
    if (p[5 + 1] == INPREC_END) {
	inprec_close ();
	return 0;
    }
    hc_orig = hc;
    hc /= inprec_div;
    hc *= inprec_div;
    for (;;) {
	uae_u32 hc2 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	if (p > lastp) {
	    write_log (L"INPREC: Next %08x (%08x=%d): %d (%d)\n", hc2, hc, hc2 - hc, p[5 + 1], p[5]);
	    lastp = p;
	}
	hc2_orig = hc2;
	hc2 /= inprec_div;
	hc2 *= inprec_div;
	if (hc > hc2) {
	    write_log (L"INPREC: %08x > %08x: %d (%d) missed!\n", hc, hc2, p[5 + 1], p[5]);
	    inprec_close ();
	    return 0;
	}
	if (hc2 != hc) {
	    lastp = p;
	    break;
	}
	if (p[5 + 1] == type) {
	    write_log (L"INPREC: %08x: %d (%d) (%+d)\n", hc, type, p[5], hc_orig - hc2_orig);
	    inprec_plast = p;
	    inprec_plastptr = p + 5 + 2;
	    return 1;
	}
	p += 5 + 2 + p[5];
    }
    inprec_plast = NULL;
    return 0;
}
void inprec_pend (void)
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
	inprec_close ();
}

uae_u8 inprec_pu8 (void)
{
    return *inprec_plastptr++;
}
uae_u16 inprec_pu16 (void)
{
    uae_u16 v = inprec_pu8 () << 8;
    v |= inprec_pu8 ();
    return v;
}
uae_u32 inprec_pu32 (void)
{
    uae_u32 v = inprec_pu16 () << 16;
    v |= inprec_pu16 ();
    return v;
}
int inprec_pstr (TCHAR *dst)
{
    char tmp[MAX_DPATH];
    char *s;
    int len = 0;

    s = tmp;
    for(;;) {
	uae_u8 v = inprec_pu8 ();
	*s++ = v;
	if (!v)
	    break;
	len++;
    }
    au_copy (dst, MAX_DPATH, tmp);
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

int inputdevice_uaelib (TCHAR *s, TCHAR *parm)
{
    int i;

    for (i = 1; events[i].name; i++) {
	if (!_tcscmp (s, events[i].confname)) {
	    handle_input_event (i, _tstol (parm), 1, 0);
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

uae_u16 potgo_value;
static int pot_cap[2][2];
static uae_u8 pot_dat[2][2];
static int pot_dat_act[2][2];
static int analog_port[2][2];
static int digital_port[2][2];
#define POTDAT_DELAY_PAL 8
#define POTDAT_DELAY_NTSC 7

static int use_joysticks[MAX_INPUT_DEVICES];
static int use_mice[MAX_INPUT_DEVICES];
static int use_keyboards[MAX_INPUT_DEVICES];

#define INPUT_QUEUE_SIZE 16
struct input_queue_struct {
    int event, storedstate, state, max, framecnt, nextframecnt;
};
static struct input_queue_struct input_queue[INPUT_QUEUE_SIZE];


static void freejport (struct uae_prefs *dst, int num)
{
    memset (&dst->jports[num], 0, sizeof (struct jport));
}
static void copyjport (const struct uae_prefs *src, struct uae_prefs *dst, int num)
{
    freejport (dst, num);
    _tcscpy (dst->jports[num].configname, src->jports[num].configname);
    _tcscpy (dst->jports[num].name, src->jports[num].name);
    dst->jports[num].id = src->jports[num].id;
}

static void out_config (struct zfile *f, int id, int num, TCHAR *s1, TCHAR *s2)
{
    TCHAR tmp[MAX_DPATH];
    _stprintf (tmp, L"input.%d.%s%d", id, s1, num);
    cfgfile_write_str (f, tmp, s2);
}

static void write_config2 (struct zfile *f, int idnum, int i, int offset, TCHAR *tmp1, struct uae_input_device *id)
{
    TCHAR tmp2[200], tmp3[200], *p;
    int evt, got, j, k;
    TCHAR *custom;

    p = tmp2;
    got = 0;
    for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	evt = id->eventid[i + offset][j];
	custom = id->custom[i + offset][j];
	if (custom == NULL && evt <= 0) {
	    for (k = j + 1; k < MAX_INPUT_SUB_EVENT; k++) {
		if (id->eventid[i + offset][k] > 0 || id->custom[i + offset][k] != NULL)
		    break;
	    }
	    if (k == MAX_INPUT_SUB_EVENT)
		break;
	}
	if (p > tmp2) {
	    *p++ = ',';
	    *p = 0;
	}
	if (custom)
	    _stprintf (p, L"'%s'.%d", custom, id->flags[i + offset][j] & 0xff);
	else if (evt <= 0)
	    _stprintf (p, L"NULL");
	else
	    _stprintf (p, L"%s.%d", events[evt].confname, id->flags[i + offset][j]);
	p += _tcslen (p);
    }
    if (p > tmp2) {
	_stprintf (tmp3, L"input.%d.%s%d", idnum, tmp1, i);
	cfgfile_write_str (f, tmp3, tmp2);
    }
}

static struct inputdevice_functions *getidf (int devnum);

static void write_config (struct zfile *f, int idnum, int devnum, TCHAR *name, struct uae_input_device *id, struct uae_input_device2 *id2, struct inputdevice_functions *idf)
{
    TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH], *s;
    int i;

    if (!isdevice (id)) {
	_stprintf (tmp2, L"input.%d.%s.%d.empty", idnum, name, devnum);
	cfgfile_write_bool (f, tmp2, 1);
	if (id->enabled) {
	    _stprintf (tmp2, L"input.%d.%s.%d.disabled", idnum, name, devnum);
	    cfgfile_write (f, tmp2, L"%d", id->enabled ? 0 : 1);
	}
	return;
    }

    _stprintf (tmp2, L"input.%d.%s.%d.empty", idnum, name, devnum);
    cfgfile_write_bool (f, tmp2, 0);
    _stprintf (tmp2, L"input.%d.%s.%d.disabled", idnum, name, devnum);
    cfgfile_write (f, tmp2, L"%d", id->enabled ? 0 : 1);

    s = NULL;
    if (id->name)
	s = id->name;
    else if (devnum < idf->get_num ())
	s = idf->get_friendlyname (devnum);
    if (s) {
	_stprintf (tmp2, L"input.%d.%s.%d.friendlyname", idnum, name, devnum);
        cfgfile_write_str (f, tmp2, s);
    }

    s = NULL;
    if (id->configname)
        s = id->configname;
    else if (devnum < idf->get_num ())
	s = idf->get_uniquename (devnum);
    if (s) {
	_stprintf (tmp2, L"input.%d.%s.%d.name", idnum, name, devnum);
        cfgfile_write_str (f, tmp2, s);
    }

    _stprintf (tmp1, L"%s.%d.axis.", name, devnum);
    for (i = 0; i < ID_AXIS_TOTAL; i++)
	write_config2 (f, idnum, i, ID_AXIS_OFFSET, tmp1, id);
    _stprintf (tmp1, L"%s.%d.button." ,name, devnum);
    for (i = 0; i < ID_BUTTON_TOTAL; i++)
	write_config2 (f, idnum, i, ID_BUTTON_OFFSET, tmp1, id);
}

static void kbrlabel (TCHAR *s)
{
    while (*s) {
	*s = toupper(*s);
	if (*s == ' ') *s = '_';
	s++;
    }
}

static void write_kbr_config (struct zfile *f, int idnum, int devnum, struct uae_input_device *kbr, struct inputdevice_functions *idf)
{
    TCHAR tmp1[200], tmp2[200], tmp3[200], tmp4[200], *p;
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
	    TCHAR *custom = kbr->custom[i][j];
	    evt = kbr->eventid[i][j];
	    if (custom == NULL && evt <= 0) {
		for (k = j + 1; k < MAX_INPUT_SUB_EVENT; k++) {
		    if (kbr->eventid[i][k] > 0 || kbr->custom[i][k] != NULL) break;
		}
		if (k == MAX_INPUT_SUB_EVENT)
		    break;
	    }
	    if (p > tmp2) {
		*p++ = ',';
		*p = 0;
	    }
	    if (custom)
		_stprintf (p, L"'%s'.%d", custom, kbr->flags[i][j] & 0xff);
	    else if (evt > 0)
		_stprintf (p, L"%s.%d", events[evt].confname, kbr->flags[i][j]);
	    else
		_tcscat (p, L"NULL");
	    p += _tcslen(p);
	}
	_stprintf (tmp3, L"%d", kbr->extra[i][0]);
	kbrlabel (tmp3);
	_stprintf (tmp1, L"keyboard.%d.button.%s", devnum, tmp3);
	_stprintf (tmp4, L"input.%d.%s", idnum, tmp1);
	cfgfile_write_str (f, tmp4, tmp2[0] ? tmp2 : L"NULL");
	i++;
    }
}

void write_inputdevice_config (struct uae_prefs *p, struct zfile *f)
{
    int i, id;

    cfgfile_write (f, L"input.config", L"%d", p->input_selected_setting);
    cfgfile_write (f, L"input.joymouse_speed_analog", L"%d", p->input_joymouse_multiplier);
    cfgfile_write (f, L"input.joymouse_speed_digital", L"%d", p->input_joymouse_speed);
    cfgfile_write (f, L"input.joymouse_deadzone", L"%d", p->input_joymouse_deadzone);
    cfgfile_write (f, L"input.joystick_deadzone", L"%d", p->input_joystick_deadzone);
    cfgfile_write (f, L"input.analog_joystick_multiplier", L"%d", p->input_analog_joystick_mult);
    cfgfile_write (f, L"input.analog_joystick_offset", L"%d", p->input_analog_joystick_offset);
    cfgfile_write (f, L"input.mouse_speed", L"%d", p->input_mouse_speed);
    cfgfile_write (f, L"input.autofire", L"%d", p->input_autofire_framecnt);
    for (id = 1; id <= MAX_INPUT_SETTINGS; id++) {
	for (i = 0; i < MAX_INPUT_DEVICES; i++)
	    write_config (f, id, i, L"joystick", &p->joystick_settings[id][i], &joysticks2[i], &idev[IDTYPE_JOYSTICK]);
	for (i = 0; i < MAX_INPUT_DEVICES; i++)
	    write_config (f, id, i, L"mouse", &p->mouse_settings[id][i], &mice2[i], &idev[IDTYPE_MOUSE]);
	for (i = 0; i < MAX_INPUT_DEVICES; i++)
	    write_kbr_config (f, id, i, &p->keyboard_settings[id][i], &idev[IDTYPE_KEYBOARD]);
    }
}

static int getnum (const TCHAR **pp)
{
    const TCHAR *p = *pp;
    int v = _tstol (p);

    while (*p != 0 && *p !='.' && *p != ',') p++;
    if (*p == '.' || *p == ',') p++;
    *pp = p;
    return v;
}
static TCHAR *getstring (const TCHAR **pp)
{
    int i;
    static TCHAR str[1000];
    const TCHAR *p = *pp;

    if (*p == 0)
	return 0;
    i = 0;
    while (*p != 0 && *p !='.' && *p != ',')
	str[i++] = *p++;
    if (*p == '.' || *p == ',')
	p++;
    str[i] = 0;
    *pp = p;
    return str;
}

void reset_inputdevice_config (struct uae_prefs *pr)
{
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
    xfree (id->configname);
    xfree (id->name);
    memset (id, 0, sizeof (struct uae_input_device));
}

void read_inputdevice_config (struct uae_prefs *pr, TCHAR *option, TCHAR *value)
{
    struct uae_input_device *id = 0;
    struct inputevent *ie;
    int devnum, num, button, joystick, flags, i, subnum, idnum, keynum;
    int mask;
    TCHAR *p, *p2, *custom;

    option += 6; /* "input." */
    p = getstring (&option);
    if (!strcasecmp (p, L"config"))
	pr->input_selected_setting = _tstol (value);
    if (!strcasecmp (p, L"joymouse_speed_analog"))
	pr->input_joymouse_multiplier = _tstol (value);
    if (!strcasecmp (p, L"joymouse_speed_digital"))
	pr->input_joymouse_speed = _tstol (value);
    if (!strcasecmp (p, L"joystick_deadzone"))
	pr->input_joystick_deadzone = _tstol (value);
    if (!strcasecmp (p, L"joymouse_deadzone"))
	pr->input_joymouse_deadzone = _tstol (value);
    if (!strcasecmp (p, L"mouse_speed"))
	pr->input_mouse_speed = _tstol (value);
    if (!strcasecmp (p, L"autofire"))
	pr->input_autofire_framecnt = _tstol (value);
    if (!strcasecmp (p, L"analog_joystick_multiplier"))
	pr->input_analog_joystick_mult = _tstol (value);
    if (!strcasecmp (p, L"analog_joystick_offset"))
	pr->input_analog_joystick_offset = _tstol (value);

    idnum = _tstol (p);
    if (idnum <= 0 || idnum > MAX_INPUT_SETTINGS)
	return;

    if (_tcsncmp (option, L"mouse.", 6) == 0) {
	p = option + 6;
    } else if (_tcsncmp (option, L"joystick.", 9) == 0) {
	p = option + 9;
    } else if (_tcsncmp (option, L"keyboard.", 9) == 0) {
	p = option + 9;
    } else
	return;

    devnum = getnum (&p);
    if (devnum < 0 || devnum >= MAX_INPUT_DEVICES)
        return;

    p2 = getstring (&p);
    if (!p2)
	return;

    if (_tcsncmp (option, L"mouse.", 6) == 0) {
	id = &pr->mouse_settings[idnum][devnum];
	joystick = 0;
    } else if (_tcsncmp (option, L"joystick.", 9) == 0) {
	id = &pr->joystick_settings[idnum][devnum];
	joystick = 1;
    } else if (_tcsncmp (option, L"keyboard.", 9) == 0) {
	id = &pr->keyboard_settings[idnum][devnum];
	joystick = -1;
    }
    if (!id)
	return;

    if (!_tcscmp (p2, L"name")) {
	xfree (id->configname);
	id->configname = my_strdup (value);
	return;
    }
    if (!_tcscmp (p2, L"friendlyname")) {
	xfree (id->name);
	id->name = my_strdup (value);
	return;
    }

    if (!_tcscmp (p2, L"empty")) {
	clear_id (id);
	id->enabled = 1;
	return;
    }

    if (!_tcscmp (p2, L"disabled")) {
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
	if (!_tcscmp (p2, L"axis"))
	    button = 0;
	else if(!_tcscmp (p2, L"button"))
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
	    if (!_tcscmp (events[i].confname, p2))
		break;
	    i++;
	}
	ie = &events[i];
	if (!ie->name) {
	    ie = &events[0];
	    if (_tcslen (p2) > 2 && p2[0] == '\'' && p2[_tcslen (p2) - 1] == '\'') {
		custom = my_strdup (p2 + 1);
		custom[_tcslen (custom) - 1] = 0;
	    }
	}
	flags = 0;
	if (p[-1] == '.')
	    flags = getnum (&p);
	if (custom == NULL && ie->name == NULL) {
	    if (!_tcscmp(p2, L"NULL")) {
		if (joystick < 0) {
		    id->eventid[keynum][subnum] = 0;
		    id->flags[keynum][subnum] = 0;
		} else if (button) {
		    id->eventid[num + ID_BUTTON_OFFSET][subnum] = 0;
		    id->flags[num + ID_BUTTON_OFFSET][subnum] = 0;
		} else {
		    id->eventid[num + ID_AXIS_OFFSET][subnum] = 0;
		    id->flags[num + ID_AXIS_OFFSET][subnum] = 0;
		}
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

static int mouseedge_alive, mousehack_alive_cnt;
static int lastmx, lastmy;
static uaecptr magicmouse_ibase, magicmouse_gfxbase;
static int dimensioninfo_width, dimensioninfo_height, dimensioninfo_dbl;
static int vp_xoffset, vp_yoffset, mouseoffset_x, mouseoffset_y;
static int tablet_maxx, tablet_maxy, tablet_data;

int mousehack_alive (void)
{
    return mousehack_alive_cnt > 0 ? mousehack_alive_cnt : 0;
}

static uaecptr get_base (const TCHAR *name)
{
    uaecptr v = get_long (4);
    addrbank *b = &get_mem_bank(v);

    if (!b || !b->check(v, 400) || b->flags != ABFLAG_RAM)
	return 0;
    v += 378; // liblist
    while (v = get_long (v)) {
	uae_u32 v2;
	uae_u8 *p;
	b = &get_mem_bank(v);
	if (!b || !b->check (v, 32) || b->flags != ABFLAG_RAM)
	    return 0xffffffff;
	v2 = get_long (v + 10); // name
	b = &get_mem_bank(v2);
	if (!b || !b->check (v2, 20))
	    return 0xffffffff;
	if (b->flags != ABFLAG_ROM && b->flags != ABFLAG_RAM)
	    return 0;
	p = b->xlateaddr(v2);
	if (!memcmp (p, name, _tcslen (name) + 1))
	    return v;
    }
    return 0;
}

static uaecptr get_intuitionbase (void)
{
    if (magicmouse_ibase == 0xffffffff)
	return 0;
    if (magicmouse_ibase)
	return magicmouse_ibase;
    magicmouse_ibase = get_base (L"intuition.library");
    return magicmouse_ibase;
}
static uaecptr get_gfxbase (void)
{
    if (magicmouse_gfxbase == 0xffffffff)
	return 0;
    if (magicmouse_gfxbase)
	return magicmouse_gfxbase;
    magicmouse_gfxbase = get_base (L"graphics.library");
    return magicmouse_gfxbase;
}

#define MH_E 0
#define MH_CNT 2
#define MH_MAXX 4
#define MH_MAXY 6
#define MH_MAXZ 8
#define MH_X 10
#define MH_Y 12
#define MH_Z 14
#define MH_RESX 16
#define MH_RESY 18
#define MH_MAXAX 20
#define MH_MAXAY 22
#define MH_MAXAZ 24
#define MH_AX 26
#define MH_AY 28
#define MH_AZ 30
#define MH_PRESSURE 32
#define MH_BUTTONBITS 34
#define MH_INPROXIMITY 38
#define MH_ABSX 40
#define MH_ABSY 42

#define MH_END 44
#define MH_START 4

int inputdevice_is_tablet (void)
{
    int v;
    if (!uae_boot_rom)
	return 0;
    if (currprefs.input_tablet == TABLET_OFF)
	return 0;
    if (currprefs.input_tablet == TABLET_MOUSEHACK)
	return -1;
    v = is_tablet ();
    if (!v)
	return 0;
    if (kickstart_version < 37)
	return v ? -1 : 0;
    return v ? 1 : 0;
}

static void mousehack_reset (void)
{
    int off;

    dimensioninfo_width = dimensioninfo_height = 0;
    mouseoffset_x = mouseoffset_y = 0;
    dimensioninfo_dbl = 0;
    mousehack_alive_cnt = 0;
    vp_xoffset = vp_yoffset = 0;
    tablet_data = 0;
    if (!uae_boot_rom)
	return;
    off = 12 + get_long (rtarea_base + 36);
    rtarea[off + MH_E] = 0;
}

static void mousehack_enable (void)
{
    int off, mode;

    if (!uae_boot_rom || currprefs.input_tablet == TABLET_OFF)
	return;
    off = 12 + get_long (rtarea_base + 36);
    if (rtarea[off + MH_E])
	return;
    mode = 0x80;
    if (currprefs.input_tablet == TABLET_MOUSEHACK)
	mode |= 1;
    if (inputdevice_is_tablet () > 0)
	mode |= 2;
    write_log (L"Tablet driver enabled (%s)\n", ((mode & 3) == 3 ? "tablet+mousehack" : ((mode & 3) == 2) ? "tablet" : "mousehack"));
    rtarea[off + MH_E] = 0x80;
}

void input_mousehack_mouseoffset (uaecptr pointerprefs)
{
    mouseoffset_x = (uae_s16)get_word (pointerprefs + 28);
    mouseoffset_y = (uae_s16)get_word (pointerprefs + 30);
}

void input_mousehack_status (int mode, uaecptr diminfo, uaecptr dispinfo, uaecptr vp, uae_u32 moffset)
{
    if (mode == 0) {
	uae_u8 v = rtarea[12 + get_long (rtarea_base + 36)];
	v |= 0x40;
	rtarea[12 + get_long (rtarea_base + 36)] = v;
	write_log (L"Tablet driver running (%02x)\n", v);
    } else if (mode == 1) {
        int x1 = -1, y1 = -1, x2 = -1, y2 = -1;
	uae_u32 props = 0;
	dimensioninfo_width = -1;
	dimensioninfo_height = -1;
	vp_xoffset = 0;
	vp_yoffset = 0;
	if (diminfo) {
	    x1 = get_word (diminfo + 50);
	    y1 = get_word (diminfo + 52);
	    x2 = get_word (diminfo + 54);
	    y2 = get_word (diminfo + 56);
	    dimensioninfo_width = x2 - x1 + 1;
	    dimensioninfo_height = y2 - y1 + 1;
	}
	if (vp) {
	    vp_xoffset = get_word (vp + 28);
	    vp_yoffset = get_word (vp + 30);
	}
	if (dispinfo)
	    props = get_long (dispinfo + 18);
	dimensioninfo_dbl = (props & 0x00020000) ? 1 : 0;
	write_log (L"%08x %08x %08x (%dx%d)-(%dx%d) d=%dx%d %s\n",
	    diminfo, props, vp, x1, y1, x2, y2, vp_xoffset, vp_yoffset,
	    (props & 0x00020000) ? "dbl" : "");
    } else if (mode == 2) {
	if (mousehack_alive_cnt == 0)
	    mousehack_alive_cnt = -100;
	else if (mousehack_alive_cnt > 0)
	    mousehack_alive_cnt = 100;
    }
}

void get_custom_mouse_limits (int *w, int *h, int *dx, int *dy, int dbl);

void inputdevice_tablet_strobe (void)
{
    uae_u8 *p;
    uae_u32 off;

    mousehack_enable ();
    if (!uae_boot_rom)
	return;
    if (!tablet_data)
	return;
    off = 12 + get_long (rtarea_base + 36);
    p = rtarea + off;
    p[MH_CNT]++;
}

void inputdevice_tablet (int x, int y, int z, int pressure, uae_u32 buttonbits, int inproximity, int ax, int ay, int az)
{
    uae_u8 *p;
    uae_u8 tmp[MH_END];
    uae_u32 off;

    mousehack_enable ();
    if (inputdevice_is_tablet () <= 0)
	return;
    //write_log (L"%d %d %d %d %08X %d %d %d %d\n", x, y, z, pressure, buttonbits, inproximity, ax, ay, az);
    off = 12 + get_long (rtarea_base + 36);
    p = rtarea + off;

    memcpy (tmp, p + MH_START, MH_END - MH_START); 
#if 0
    if (currprefs.input_magic_mouse) {
	int maxx, maxy, diffx, diffy;
	int dw, dh, ax, ay, aw, ah;
	float xmult, ymult;
	float fx, fy;

	fx = (float)x;
	fy = (float)y;
	desktop_coords (&dw, &dh, &ax, &ay, &aw, &ah);
	xmult = (float)tablet_maxx / dw;
	ymult = (float)tablet_maxy / dh;

	diffx = 0;
	diffy = 0;
	if (picasso_on) {
	    maxx = gfxvidinfo.width;
	    maxy = gfxvidinfo.height;
	} else {
	    get_custom_mouse_limits (&maxx, &maxy, &diffx, &diffy);
	}
	diffx += ax;
	diffy += ah;

	fx -= diffx * xmult;
	if (fx < 0)
	    fx = 0;
	if (fx >= aw * xmult)
	    fx = aw * xmult - 1;
	fy -= diffy * ymult;
	if (fy < 0)
	    fy = 0;
	if (fy >= ah * ymult)
	    fy = ah * ymult - 1;

	x = (int)(fx * (aw * xmult) / tablet_maxx + 0.5);
	y = (int)(fy * (ah * ymult) / tablet_maxy + 0.5);

    }
#endif
    p[MH_X] = x >> 8;
    p[MH_X + 1] = x;
    p[MH_Y] = y >> 8;
    p[MH_Y + 1] = y;
    p[MH_Z] = z >> 8;
    p[MH_Z + 1] = z;

    p[MH_AX] = ax >> 8;
    p[MH_AX + 1] = ax;
    p[MH_AY] = ay >> 8;
    p[MH_AY + 1] = ay;
    p[MH_AZ] = az >> 8;
    p[MH_AZ + 1] = az;

    p[MH_MAXX] = tablet_maxx >> 8;
    p[MH_MAXX + 1] = tablet_maxx;
    p[MH_MAXY] = tablet_maxy >> 8;
    p[MH_MAXY + 1] = tablet_maxy;

    p[MH_PRESSURE] = pressure >> 8;
    p[MH_PRESSURE + 1] = pressure;

    p[MH_BUTTONBITS + 0] = buttonbits >> 24;
    p[MH_BUTTONBITS + 1] = buttonbits >> 16;
    p[MH_BUTTONBITS + 2] = buttonbits >>  8;
    p[MH_BUTTONBITS + 3] = buttonbits >>  0;

    if (inproximity < 0) {
	p[MH_INPROXIMITY] = p[MH_INPROXIMITY + 1] = 0xff;
    } else {
	p[MH_INPROXIMITY] = 0;
	p[MH_INPROXIMITY + 1] = inproximity ? 1 : 0;
    }

    if (!memcmp (tmp, p + MH_START, MH_END - MH_START))
	return;
    rtarea[off + MH_E] = 0xc0 | 2;
    p[MH_CNT]++;
}

void inputdevice_tablet_info (int maxx, int maxy, int maxz, int maxax, int maxay, int maxaz, int xres, int yres)
{
    uae_u8 *p;

    if (!uae_boot_rom)
	return;
    p = rtarea + 12 + get_long (rtarea_base + 36);

    tablet_maxx = maxx;
    tablet_maxy = maxy;
    p[MH_MAXX] = maxx >> 8;
    p[MH_MAXX + 1] = maxx;
    p[MH_MAXY] = maxy >> 8;
    p[MH_MAXY + 1] = maxy;
    p[MH_MAXZ] = maxz >> 8;
    p[MH_MAXZ + 1] = maxz;

    p[MH_RESX] = xres >> 8;
    p[MH_RESX + 1] = xres;
    p[MH_RESY] = yres >> 8;
    p[MH_RESY + 1] = yres;

    p[MH_MAXAX] = maxax >> 8;
    p[MH_MAXAX + 1] = maxax;
    p[MH_MAXAY] = maxay >> 8;
    p[MH_MAXAY + 1] = maxay;
    p[MH_MAXAZ] = maxaz >> 8;
    p[MH_MAXAZ + 1] = maxaz;
}


void getgfxoffset (int *dx, int *dy, int*,int*);

static void inputdevice_mh_abs (int x, int y)
{
    uae_u8 *p;
    uae_u8 tmp[4];
    uae_u32 off;

    mousehack_enable ();
    off = 12 + get_long (rtarea_base + 36);
    p = rtarea + off;

    memcpy (tmp, p + MH_ABSX, 4);

    x -= mouseoffset_x + 1;
    y -= mouseoffset_y + 2;

    p[MH_ABSX] = x >> 8;
    p[MH_ABSX + 1] = x;
    p[MH_ABSY] = y >> 8;
    p[MH_ABSY + 1] = y;

    if (!memcmp (tmp, p + MH_ABSX, 4))
	return;
    rtarea[off + MH_E] = 0xc0 | 1;
    p[MH_CNT]++;
    tablet_data = 1;
}

#if 0
static void inputdevice_mh_abs_v36 (int x, int y)
{
    uae_u8 *p;
    uae_u8 tmp[MH_END];
    uae_u32 off;
    int maxx, maxy, diffx, diffy;
    int fdy, fdx, fmx, fmy;

    mousehack_enable ();
    off = 12 + get_long (rtarea_base + 36);
    p = rtarea + off;

    memcpy (tmp, p + MH_START, MH_END - MH_START); 

    getgfxoffset (&fdx, &fdy, &fmx, &fmy);
    x -= fdx;
    y -= fdy;
    x += vp_xoffset;
    y += vp_yoffset;

    diffx = diffy = 0;
    maxx = maxy = 0;

    if (picasso_on) {
	maxx = picasso96_state.Width;
	maxy = picasso96_state.Height;
    } else if (dimensioninfo_width > 0 && dimensioninfo_height > 0) {
	maxx = dimensioninfo_width;
	maxy = dimensioninfo_height;
	get_custom_mouse_limits (&maxx, &maxy, &diffx, &diffy, dimensioninfo_dbl);
    } else {
	uaecptr gb = get_gfxbase ();
	maxx = 0; maxy = 0;
	if (gb) {
	    maxy = get_word (gb + 216);
	    maxx = get_word (gb + 218);
	}
	get_custom_mouse_limits (&maxx, &maxy, &diffx, &diffy, 0);
    }
#if 0
    {
	uaecptr gb = get_intuitionbase ();
	maxy = get_word (gb + 1344 + 2);
	maxx = get_word (gb + 1348 + 2);
	write_log (L"%d %d\n", maxx, maxy);
    }
#endif
#if 1
    {
	uaecptr gb = get_gfxbase ();
	uaecptr view = get_long (gb + 34);
	if (view) {
	    uaecptr vp = get_long (view);
	    if (vp) {
		int w, h, dw, dh;
		w = get_word (vp + 24);
		h = get_word (vp + 26) * 2;
		dw = get_word (vp + 28);
		dh = get_word (vp + 30);
		//write_log (L"%d %d %d %d\n", w, h, dw, dh);
		if (w < maxx)
		    maxx = w;
		if (h < maxy)
		    maxy = h;
		x -= dw;
		y -= dh;
	    }
	}
	//write_log (L"* %d %d\n", get_word (gb + 218), get_word (gb + 216));
    }
    //write_log (L"%d %d\n", maxx, maxy);
#endif

    maxx = maxx * 1000 / fmx;
    maxy = maxy * 1000 / fmy;

    if (maxx <= 0)
	maxx = 1;
    if (maxy <= 0)
	maxy = 1;

    x -= diffx;
    if (x < 0)
	x = 0;
    if (x >= maxx)
	x = maxx - 1;

    y -= diffy;
    if (y < 0)
	y = 0;
    if (y >= maxy)
	y = maxy - 1;

    //write_log (L"%d %d %d %d\n", x, y, maxx, maxy);

    p[MH_X] = x >> 8;
    p[MH_X + 1] = x;
    p[MH_Y] = y >> 8;
    p[MH_Y + 1] = y;
    p[MH_MAXX] = maxx >> 8;
    p[MH_MAXX + 1] = maxx;
    p[MH_MAXY] = maxy >> 8;
    p[MH_MAXY + 1] = maxy;
    
    p[MH_Z] = p[MH_Z + 1] = 0;
    p[MH_MAXZ] = p[MH_MAXZ + 1] = 0;
    p[MH_AX] = p[MH_AX + 1] = 0;
    p[MH_AY] = p[MH_AY + 1] = 0;
    p[MH_AZ] = p[MH_AZ + 1] = 0;
    p[MH_PRESSURE] = p[MH_PRESSURE + 1] = 0;
    p[MH_INPROXIMITY] = p[MH_INPROXIMITY + 1] = 0xff;

    if (!memcmp (tmp, p + MH_START, MH_END - MH_START))
	return;
    p[MH_CNT]++;
    tablet_data = 1;
}
#endif

static void mousehack_helper (void)
{
    int x, y;
    int fdy, fdx, fmx, fmy;

    if (currprefs.input_magic_mouse == 0 || currprefs.input_tablet < TABLET_MOUSEHACK)
	return;
#if 0
    if (kickstart_version >= 36) {
	inputdevice_mh_abs_v36 (lastmx, lastmy);
	return;
    }
#endif
    x = lastmx;
    y = lastmy;
    getgfxoffset (&fdx, &fdy, &fmx, &fmy);


#ifdef PICASSO96
    if (picasso_on) {
	x -= picasso96_state.XOffset;
	y -= picasso96_state.YOffset;
	x = x * fmx / 1000;
	y = y * fmy / 1000;
	x -= fdx * fmx / 1000;
	y -= fdy * fmy / 1000;
    } else
#endif
    {
	x = x * fmx / 1000;
	y = y * fmy / 1000;
	x -= fdx * fmx / 1000 - 1;
	y -= fdy * fmy / 1000 - 2;
    	x = coord_native_to_amiga_x (x);
	y = coord_native_to_amiga_y (y) << 1;
    }
    inputdevice_mh_abs (x, y);
}

static int mouseedge_x, mouseedge_y, mouseedge_time;
#define MOUSEEDGE_RANGE 100
#define MOUSEEDGE_TIME 2

extern void setmouseactivexy (int,int,int);

static int mouseedge (void)
{
    int x, y, dir;
    uaecptr ib;
    static int melast_x, melast_y;
    static int isnonzero;

    if (currprefs.input_magic_mouse == 0 || currprefs.input_tablet > 0)
	return 0;
    if (magicmouse_ibase == 0xffffffff)
	return 0;
    dir = 0;
    if (!mouseedge_time) {
	isnonzero = 0;
	goto end;
    }
    ib = get_intuitionbase ();
    if (!ib)
	return 0;
    x = get_word (ib + 70);
    y = get_word (ib + 68);
    if (x || y)
	isnonzero = 1;
    if (!isnonzero)
	return 0;
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
	if (mouseedge_y < -MOUSEEDGE_RANGE) {
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
    return 1;

end:
    mouseedge_time = 0;
    if (dir) {
	if (!picasso_on) {
	    int aw = 0, ah = 0, dx, dy;
	    get_custom_mouse_limits (&aw, &ah, &dx, &dy, dimensioninfo_dbl);
	    x += dx;
	    y += dy;
	}
	if (!dmaen(DMA_SPRITE))
	    setmouseactivexy (x, y, 0);
	else
	    setmouseactivexy (x, y, dir);
    }
    return 1;
}

int magicmouse_alive (void)
{
    return mouseedge_alive > 0;
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

static int getvelocity (int num, int subnum, int pct)
{
    int val;
    int v;

    val = mouse_delta[num][subnum];
    v = val * pct / 1000;
    if (!v) {
	if (val < -maxvpos / 2)
	    v = -2;
	else if (val < 0)
	    v = -1;
	else if (val > maxvpos / 2)
	    v = 2;
	else if (val > 0)
	    v = 1;
    }
    if (!mouse_deltanoreset[num][subnum])
	mouse_delta[num][subnum] -= v;
    return v;
}

static void mouseupdate (int pct, int vsync)
{
    int v, i;
    int max = 127;

    if (pct > 1000)
	pct = 1000;

    if (vsync) {
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

	v = getvelocity (i, 0, pct);
	mouse_x[i] += v;

	v = getvelocity (i, 1, pct);
	mouse_y[i] += v;

	v = getvelocity (i, 2, pct);
	if (v > 0)
	    record_key (0x7a << 1);
	else if (v < 0)
	    record_key (0x7b << 1);
	if (!mouse_deltanoreset[i][2])
	    mouse_delta[i][2] = 0;

	if (mouse_frame_x[i] - mouse_x[i] > max)
	    mouse_x[i] = mouse_frame_x[i] - max;
	if (mouse_frame_x[i] - mouse_x[i] < -max)
	    mouse_x[i] = mouse_frame_x[i] + max;

	if (mouse_frame_y[i] - mouse_y[i] > max)
	    mouse_y[i] = mouse_frame_y[i] - max;
	if (mouse_frame_y[i] - mouse_y[i] < -max)
	    mouse_y[i] = mouse_frame_y[i] + max;

	if (!vsync) {
	    mouse_frame_x[i] = mouse_x[i];
	    mouse_frame_y[i] = mouse_y[i];
	}

    }
}

static int input_vpos, input_frame;
extern int vpos;
static void readinput (void)
{
    uae_u32 totalvpos;

    totalvpos = input_frame * maxvpos + vpos;
    mouseupdate ((totalvpos - input_vpos) * 1000 / maxvpos, 0);
    input_vpos = totalvpos;

}

int getjoystate (int joy)
{
    int left = 0, right = 0, top = 0, bot = 0;
    uae_u16 v = 0;

    if (inputdevice_logging & 2)
	write_log (L"JOY%dDAT %08x\n", joy, M68K_GETPC);
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
	mouse_x[joy] &= ~3;
	mouse_y[joy] &= ~3;
	if (left)
	    top = !top;
	if (right)
	    bot = !bot;
	v &= ~0x0303;
	v |= bot | (right << 1) | (top << 8) | (left << 9);
    }
#ifdef DONGLE_DEBUG
    if (notinrom ())
	write_log (L"JOY%dDAT %04X %s\n", joy, v, debuginfo (0));
#endif
    if (input_recording > 0 && oldjoy[joy] != v) {
	oldjoy[joy] = v;
	inprec_rstart (INPREC_JOYPORT);
	inprec_ru16 (v);
	inprec_rend ();
    } else if (input_recording < 0) {
	v = oldjoy[joy];
	if (inprec_pstart (INPREC_JOYPORT)) {
	    v = inprec_pu16 ();
	    inprec_pend ();
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
    if (inputdevice_logging & 2)
	write_log (L"JOYTEST: %04X PC=%x\n", v , M68K_GETPC);
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

static void charge_cap (int joy, int idx, int charge)
{
    if (charge < -1 || charge > 1)
	charge = charge * 80;
    pot_cap[joy][idx] += charge;
    if (pot_cap[joy][idx] < 0)
	pot_cap[joy][idx] = 0;
    if (pot_cap[joy][idx] > 511)
	pot_cap[joy][idx] = 511;
}

static void cap_check (void)
{
    int joy, i;

    for (joy = 0; joy < 2; joy++) {
	for (i = 0; i < 2; i++) {
	    int charge = 0;
	    uae_u16 pdir = 0x0200 << (joy * 4 + i * 2); /* output enable */
	    uae_u16 pdat = 0x0100 << (joy * 4 + i * 2); /* data */
	    int isbutton = getbuttonstate (joy, i == 0 ? JOYBUTTON_3 : JOYBUTTON_2);

	    if (cd32_pad_enabled[joy])
		continue;
	    if (analog_port[joy][i] && pot_cap[joy][i] < joydirpot[joy][i])
	        charge = 1; // slow charge via pot variable resistor
	    if ((digital_port[joy][i] || mouse_port[joy]))
		charge = 1; // slow charge via pull-up resistor
	    if (!(potgo_value & pdir)) { // input?
		if (pot_dat_act[joy][i])
		    pot_dat[joy][i]++;
		/* first 8 lines after potgo has been started = discharge cap */
		if (pot_dat_act[joy][i] == 1) {
		    if (pot_dat[joy][i] < (currprefs.ntscmode ? POTDAT_DELAY_NTSC : POTDAT_DELAY_PAL)) {
			charge = -2; /* fast discharge delay */
		    } else {
			pot_dat_act[joy][i] = 2;
			pot_dat[joy][i] = 0;
		    }
		}
		if (analog_port[joy][i] && pot_dat_act[joy][i] == 2 && pot_cap[joy][i] >= joydirpot[joy][i])
		    pot_dat_act[joy][i] = 0;
		if ((digital_port[joy][i] || mouse_port[joy]) && pot_dat_act[joy][i] == 2) {
		    if (pot_cap[joy][i] >= 10 && !isbutton)
			pot_dat_act[joy][i] = 0;
		}
	    } else { // output?
		charge = (potgo_value & pdat) ? 2 : -2; /* fast (dis)charge if output */
		if (potgo_value & pdat)
		    pot_dat_act[joy][i] = 0; // instant stop if output+high
		if (isbutton)
		    pot_dat[joy][i]++; // "free running" if output+low
	    }

	    if (isbutton)
	        charge = -2; // button press overrides everything

	    if (currprefs.cs_cdtvcd) {
		/* CDTV P9 is not floating */
		if (!(potgo_value & pdir) && i == 1 && charge == 0)
		    charge = 2;
	    }
	    /* official Commodore mouse has pull-up resistors in button lines
	     * NOTE: 3rd party mice may not have pullups! */
	    if (mouse_port[joy] && charge == 0)
	        charge = 2;
	    /* emulate pullup resistor if button mapped because there too many broken
	     * programs that read second button in input-mode (and most 2+ button pads have
	     * pullups)
	     */
	    if (digital_port[joy][i] && charge == 0)
	        charge = 2;

	    charge_cap (joy, i, charge);
	}
    }
}

uae_u8 handle_joystick_buttons (uae_u8 dra)
{
    uae_u8 but = 0;
    int i;

    cap_check ();
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
    if (inputdevice_logging & 4)
	write_log (L"BFE001: %02X:%02X %x\n", dra, but, M68K_GETPC);
    return but;
}

/* joystick 1 button 1 is used as a output for incrementing shift register */
void handle_cd32_joystick_cia (uae_u8 pra, uae_u8 dra)
{
    static int oldstate[2];
    int i;

    cap_check ();
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

    cap_check ();
    for (i = 0; i < 2; i++) {
	uae_u16 p9dir = 0x0800 << (i * 4); /* output enable P9 */
	uae_u16 p9dat = 0x0400 << (i * 4); /* data P9 */
	uae_u16 p5dir = 0x0200 << (i * 4); /* output enable P5 */
	uae_u16 p5dat = 0x0100 << (i * 4); /* data P5 */

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

	    potgor &= ~p5dat;
	    if (pot_cap[i][0] > 100)
		potgor |= p5dat;

	    potgor &= ~p9dat;
	    if (pot_cap[i][1] > 100)
		potgor |= p9dat;

	}
    }
    return potgor;
}


static int inputdelay;

void inputdevice_hsync (void)
{

    cap_check ();

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

static uae_u16 POTDAT (int joy)
{
    uae_u16 v = (pot_dat[joy][1] << 8) | pot_dat[joy][0];
    if (inputdevice_logging & 16)
	write_log (L"POTDAT%d: %04X %08X\n", joy, v, M68K_GETPC);
    return v;
}

uae_u16 POT0DAT (void)
{
    return POTDAT (0);
}
uae_u16 POT1DAT (void)
{
    return POTDAT (1);
}

/* direction=input, data pin floating, last connected logic level or previous status
 *                  written when direction was ouput
 *                  otherwise it is currently connected logic level.
 * direction=output, data pin is current value, forced to zero if joystick button is pressed
 * it takes some tens of microseconds before data pin changes state
 */

void POTGO (uae_u16 v)
{
    int i, j;

    if (inputdevice_logging & 16)
	write_log (L"POTGO_W: %04X %08X\n", v, M68K_GETPC);
#ifdef DONGLE_DEBUG
    if (notinrom ())
	write_log (L"POTGO %04X %s\n", v, debuginfo(0));
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
	for (i = 0; i < 2; i++) {
	    for (j = 0; j < 2; j++) {
	        pot_dat_act[i][j] = 1;
	        pot_dat[i][j] = 0;
	    }
	}
    }
}

uae_u16 POTGOR (void)
{
    uae_u16 v = handle_joystick_potgor (potgo_value) & 0x5500;
#ifdef DONGLE_DEBUG
    if (notinrom ())
	write_log (L"POTGOR %04X %s\n", v, debuginfo(0));
#endif
    if (inputdevice_logging & 16)
	write_log (L"POTGO_R: %04X %08X %d\n", v, M68K_GETPC, cd32_shifter[1]);
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
	    write_log (L"input queue overflow\n");
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
	if (key == AK_RESETWARNING) {
	    resetwarning_do (0);
	    return;
	} else if ((keybuf[AK_CTRL] || keybuf[AK_RCTRL]) && keybuf[AK_LAMI] && keybuf[AK_RAMI]) {
	    int r = keybuf[AK_LALT] | keybuf[AK_RALT];
	    if (!r && currprefs.cs_resetwarning && resetwarning_do (1))
		return;
	    memset (keybuf, 0, sizeof (keybuf));
	    uae_reset (r);
	}
	record_key ((uae_u8)((key << 1) | (key >> 7)));
	if (inputdevice_logging & 1)
	    write_log (L"Amiga key %02X %d\n", key & 0x7f, key >> 7);
	return;
    }
    inputdevice_add_inputcode (code, state);
}

void inputdevice_handle_inputcode (void)
{
    static int swapperslot;
    int code = inputcode_pending;
    int state = inputcode_pending_state;

    inputcode_pending = 0;
    if (code == 0)
	return;
    if (vpos != 0)
	write_log (L"inputcode=%d but vpos = %d", code, vpos);

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
	NMI_delayed ();
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
	sound_mute (-1);
	break;
    case AKS_MVOLDOWN:
	master_sound_volume (-1);
	break;
    case AKS_MVOLUP:
	master_sound_volume (1);
	break;
    case AKS_MVOLMUTE:
	master_sound_volume (0);
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
    case AKS_DISKSWAPPER_NEXT:
	swapperslot++;
	if (swapperslot >= MAX_SPARE_DRIVES || currprefs.dfxlist[swapperslot][0] == 0)
	    swapperslot = 0;
    break;
    case AKS_DISKSWAPPER_PREV:
	swapperslot--;
	if (swapperslot < 0)
	    swapperslot = MAX_SPARE_DRIVES - 1;
	while (swapperslot > 0) {
	    if (currprefs.dfxlist[swapperslot][0])
		break;
	    swapperslot--;
	}
    break;
    case AKS_DISKSWAPPER_INSERT0:
    case AKS_DISKSWAPPER_INSERT1:
    case AKS_DISKSWAPPER_INSERT2:
    case AKS_DISKSWAPPER_INSERT3:
	_tcscpy (changed_prefs.df[code - AKS_DISKSWAPPER_INSERT0], currprefs.dfxlist[swapperslot]);
    break;

    break;
    case AKS_INPUT_CONFIG_1:
    case AKS_INPUT_CONFIG_2:
    case AKS_INPUT_CONFIG_3:
    case AKS_INPUT_CONFIG_4:
	changed_prefs.input_selected_setting = currprefs.input_selected_setting = code - AKS_INPUT_CONFIG_1 + 1;
	inputdevice_updateconfig (&currprefs);
    break;
    }
}

int handle_custom_event (TCHAR *custom)
{
    TCHAR *p, *buf, *nextp;

    if (custom == NULL)
	return 0;
    p = buf = my_strdup (custom);
    while (p && *p) {
	TCHAR *p2;
	if (*p != '\"')
	    break;
	p++;
	p2 = p;
	while (*p2 != '\"' && *p2 != 0)
	    p2++;
	if (*p2 == '\"') {
	    *p2++ = 0;
	    nextp = p2 + 1;
	    while (*nextp == ' ')
		nextp++;
	}
	cfgfile_parse_line (&changed_prefs, p, 0);
	p = nextp;
    }
    xfree(buf);
    return 0;
}

int handle_input_event (int nr, int state, int max, int autofire)
{
    struct inputevent *ie;
    int joy;

    if (nr <= 0)
	return 0;
    ie = &events[nr];
    if (inputdevice_logging & 1)
	write_log (L"'%s' %d %d\n", ie->name, state, max);
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
		int unit = ie->data & 0x7f;
		if (max) {
		    if (state <= deadzone && state >= -deadzone) {
			state = 0;
			mouse_deltanoreset[joy][unit] = 0;
		    } else if (state < 0) {
			state += deadzone;
			mouse_deltanoreset[joy][unit] = 1;
		    } else {
			state -= deadzone;
			mouse_deltanoreset[joy][unit] = 1;
		    }
		    max -= deadzone;
		    delta = state * currprefs.input_joymouse_multiplier / max;
		} else {
		    delta = state;
	    	    mouse_deltanoreset[joy][unit] = 0;
		}
		mouse_delta[joy][unit] += delta * ((ie->data & IE_INVERT) ? -1 : 1);

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
		int unit = ie->data & 0x7f;
		if (max) {
		    if (state <= deadzone && state >= -deadzone) {
			state = 0;
		    } else if (state < 0) {
			state += deadzone;
		    } else {
			state -= deadzone;
		    }
		    state = state * max / (max - deadzone);
		}
		if (ie->data & IE_INVERT)
		    state = -state;
		state = state / (312 * 100 / currprefs.input_analog_joystick_mult) + (128 * currprefs.input_analog_joystick_mult / 100) + currprefs.input_analog_joystick_offset;
		if (state < 0)
		    state = 0;
		joydirpot[joy][unit] = state;

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

static void inputdevice_checkconfig (void)
{
    if (currprefs.jports[0].id != changed_prefs.jports[0].id ||
	currprefs.jports[1].id != changed_prefs.jports[1].id ||
	currprefs.input_selected_setting != changed_prefs.input_selected_setting ||
	currprefs.input_joymouse_multiplier != changed_prefs.input_joymouse_multiplier ||
	currprefs.input_joymouse_deadzone != changed_prefs.input_joymouse_deadzone ||
	currprefs.input_joystick_deadzone != changed_prefs.input_joystick_deadzone ||
	currprefs.input_joymouse_speed != changed_prefs.input_joymouse_speed ||
	currprefs.input_autofire_framecnt != changed_prefs.input_autofire_framecnt ||
	currprefs.input_mouse_speed != changed_prefs.input_mouse_speed) {

	currprefs.input_selected_setting = changed_prefs.input_selected_setting;
	currprefs.input_joymouse_multiplier = changed_prefs.input_joymouse_multiplier;
	currprefs.input_joymouse_deadzone = changed_prefs.input_joymouse_deadzone;
	currprefs.input_joystick_deadzone = changed_prefs.input_joystick_deadzone;
	currprefs.input_joymouse_speed = changed_prefs.input_joymouse_speed;
	currprefs.input_autofire_framecnt = changed_prefs.input_autofire_framecnt;
	currprefs.input_mouse_speed = changed_prefs.input_mouse_speed;

	inputdevice_updateconfig (&currprefs);
    }
}

void inputdevice_vsync (void)
{
    struct input_queue_struct *iq;
    int i;

    if (inputdevice_logging & 32)
	write_log (L"*\n");

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

    input_frame++;
    mouseupdate (0, 1);
    idev[IDTYPE_MOUSE].read ();
    idev[IDTYPE_JOYSTICK].read ();
 
    inputdelay = uaerand () % (maxvpos <= 1 ? 1 : maxvpos - 1);
    inputdevice_handle_inputcode ();
    if (mouseedge_alive > 0)
	mouseedge_alive--;
#ifdef ARCADIA
    if (arcadia_bios)
	arcadia_vsync ();
#endif
    if (mouseedge ())
	mouseedge_alive = 10;
    if (mousehack_alive_cnt > 0) {
	mousehack_alive_cnt--;
	if (mousehack_alive_cnt == 0)
	    setmouseactive (-1);
    } else if (mousehack_alive_cnt < 0) {
	mousehack_alive_cnt++;
	if (mousehack_alive_cnt == 0) {
	    mousehack_alive_cnt = 100;
	    setmouseactive (0);
	    setmouseactive (1);
	}
    }
    inputdevice_checkconfig ();
}

void inputdevice_reset (void)
{
    magicmouse_ibase = 0;
    magicmouse_gfxbase = 0;
    mousehack_reset ();
    if (inputdevice_is_tablet ())
	mousehack_enable ();
}

static int getoldport (struct uae_input_device *id)
{
    int i, j;

    for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
	    int evt = id->eventid[i][j];
	    if (evt > 0) {
		int unit = events[evt].unit;
		if (unit >= 1 && unit <= 4)
		    return unit;
	    }
	}
    }
    return -1;
}

static int switchdevice (struct uae_input_device *id, int num, int button)
{
    int i, j;
    int ismouse = 0;
    int newport = 0;
    int flags = 0;
    TCHAR *name = NULL;
    int otherbuttonpressed = 0;

    if (num >= 4)
	return 0;
    if (!button)
	return 0;
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (id == &joysticks[i]) {
	    name = idev[IDTYPE_JOYSTICK].get_uniquename (i);
	    newport = num == 0 ? 1 : 0;
	    flags = idev[IDTYPE_JOYSTICK].get_flags (i);
	    for (j = 0; j < MAX_INPUT_DEVICES; j++) {
		if (j != i) {
		    struct uae_input_device2 *id2 = &joysticks2[j];
		    if (id2->buttonmask)
			otherbuttonpressed = 1;
		}
	    }
	}
	if (id == &mice[i]) {
	    ismouse = 1;
	    name = idev[IDTYPE_MOUSE].get_uniquename (i);
	    newport = num == 0 ? 0 : 1;
	    flags = idev[IDTYPE_MOUSE].get_flags (i);
	}
    }
    if (!name)
	return 0;
    if (num == 0 && otherbuttonpressed)
        newport = newport ? 0 : 1;
    if (!currprefs.input_selected_setting) {
	if (num == 0 || num == 1) {
	    int om = jsem_ismouse (num, &currprefs);
	    int om1 = jsem_ismouse (0, &currprefs);
	    int om2 = jsem_ismouse (1, &currprefs);
	    if ((om1 >= 0 || om2 >= 0) && ismouse)
		return 0;
	    if (flags)
		return 0;
	    if (name) {
	        write_log (L"inputdevice change '%s':%d->%d\n", name, num, newport);
		inputdevice_joyport_config (&changed_prefs, name, newport, 2);
		inputdevice_copyconfig (&changed_prefs, &currprefs);
		return 1;
	    }
	}
	return 0;
    } else {
	int oldport = getoldport (id);
	int i, j, k, evt;
	struct inputevent *ie, *ie2;

	if (flags)
	    return 0;
	if (oldport <= 0)
	    return 0;
	newport++;
	/* do not switch if switching mouse and any "supermouse" mouse enabled */
	if (ismouse) {
	    for (i = 0; i < MAX_INPUT_SETTINGS + 1; i++) {
		if (mice[i].enabled && idev[IDTYPE_MOUSE].get_flags (i))
		    return 0;
	    }
	}
	for (i = 0; i < MAX_INPUT_SETTINGS + 1; i++) {
	    if (getoldport (&joysticks[i]) == newport)
		joysticks[i].enabled = 0;
	    if (getoldport (&mice[i]) == newport)
		mice[i].enabled = 0;
	}   
	id->enabled = 1;
	for (i = 0; i < MAX_INPUT_DEVICE_EVENTS; i++) {
	    for (j = 0; j < MAX_INPUT_SUB_EVENT; j++) {
		evt = id->eventid[i][j];
		if (evt <= 0)
		    continue;
		ie = &events[evt];
		if (ie->unit == oldport) {
		    k = 1;
		    while (events[k].confname) {
			ie2 = &events[k];
			if (ie2->type == ie->type && ie2->data == ie->data && ie2->allow_mask == ie->allow_mask && ie2->unit == newport) {
			    id->eventid[i][j] = k;
			    break;
			}
			k++;
		    }
		} else if (ie->unit == newport) {
		    k = 1;
		    while (events[k].confname) {
			ie2 = &events[k];
			if (ie2->type == ie->type && ie2->data == ie->data && ie2->allow_mask == ie->allow_mask && ie2->unit == oldport) {
			    id->eventid[i][j] = k;
			    break;
			}
			k++;
		    }
		}
	    }
	}
        write_log (L"inputdevice change '%s':%d->%d\n", name, num, newport);
	inputdevice_copyconfig (&currprefs, &changed_prefs);
	inputdevice_copyconfig (&changed_prefs, &currprefs);
	return 1;
    }
    return 0;
}

static void process_custom_event (struct uae_input_device *id, int offset, int state)
{
    int idx = -1;
    int custompos = (id->flags[offset][0] >> 15) & 1;
    TCHAR *custom;

    if (state < 0) {
	idx = 0;
	custompos = 0;
    } else {
	idx = state > 0 ? 0 : 1;
	if (custompos)
	    idx += 2;
	if (state == 0)
	    custompos ^= 1;
    }
    custom = id->custom[offset][idx];
    if (custom == NULL) {
	if (idx >= 2)
	    custom = id->custom[offset][idx - 2];
    }
    handle_custom_event (custom);
    id->flags[offset][0] &= ~0x8000;
    id->flags[offset][0] |= custompos << 15;
}

static void setbuttonstateall (struct uae_input_device *id, struct uae_input_device2 *id2, int button, int state)
{
    int evt, autofire, i;
    uae_u32 mask = 1 << button;
    uae_u32 omask = id2->buttonmask & mask;
    uae_u32 nmask = (state ? 1 : 0) << button;

    if (!id->enabled) {
	if (state)
	    switchdevice (id, button, 1);
	return;
    }
    if (button >= ID_BUTTON_TOTAL)
	return;

    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
	evt = id->eventid[ID_BUTTON_OFFSET + button][sublevdir[state <= 0 ? 1 : 0][i]];
	autofire = (id->flags[ID_BUTTON_OFFSET + button][sublevdir[state <= 0 ? 1 : 0][i]] & ID_FLAG_AUTOFIRE) ? 1 : 0;
	if (state < 0) {
	    handle_input_event (evt, 1, 1, 0);
	    queue_input_event (evt, 0, 1, 1, 0); /* send release event next frame */
	    if (i == 0)
		process_custom_event (id, ID_BUTTON_OFFSET + button, state);
	} else {
	    if ((omask ^ nmask) & mask) {
		handle_input_event (evt, state, 1, autofire);
		if (i == 0)
		    process_custom_event (id, ID_BUTTON_OFFSET + button, state);
	    }
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

static int isanalog (int ei)
{
    if (ei == INPUTEVENT_JOY1_HORIZ_POT || ei == INPUTEVENT_JOY1_HORIZ_POT_INV) {
	analog_port[0][0] = 1;
	return 1;
    }
    if (ei == INPUTEVENT_JOY1_VERT_POT || ei == INPUTEVENT_JOY1_VERT_POT_INV) {
	analog_port[0][1] = 1;
	return 1;
    }
    if (ei == INPUTEVENT_JOY2_HORIZ_POT || ei == INPUTEVENT_JOY2_HORIZ_POT_INV) {
	analog_port[1][0] = 1;
	return 1;
    }
    if (ei == INPUTEVENT_JOY2_VERT_POT || ei == INPUTEVENT_JOY2_VERT_POT_INV) {
	analog_port[1][1] = 1;
	return 1;
    }
    return 0;
}

static int isdigitalbutton (int ei)
{
    if (ei == INPUTEVENT_JOY1_2ND_BUTTON) {
	digital_port[0][0] = 1;
	return 1;
    }
    if (ei == INPUTEVENT_JOY1_3RD_BUTTON) {
	digital_port[0][1] = 1;
	return 1;
    }
    if (ei == INPUTEVENT_JOY2_2ND_BUTTON) {
	digital_port[1][0] = 1;
	return 1;
    }
    if (ei == INPUTEVENT_JOY2_3RD_BUTTON) {
	digital_port[1][1] = 1;
	return 1;
    }
    return 0;
}

static void scanevents (struct uae_prefs *p)
{
    int i, j, k, ei;
    const struct inputevent *e;
    int n_joy = idev[IDTYPE_JOYSTICK].get_num();
    int n_mouse = idev[IDTYPE_MOUSE].get_num();

    cd32_pad_enabled[0] = cd32_pad_enabled[1] = 0;
    parport_joystick_enabled = 0;
    mouse_port[0] = mouse_port[1] = 0;

    for (i = 0; i < 2; i++) {
	for (j = 0; j < 2; j++) {
	    digital_port[i][j] = 0;
	    analog_port[i][j] = 0;
	    joydirpot[i][j] = 128 / (312 * 100 / currprefs.input_analog_joystick_mult) + (128 * currprefs.input_analog_joystick_mult / 100) + currprefs.input_analog_joystick_offset;
	}
    }

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
		    isdigitalbutton (ei);
		    if (joysticks[i].eventid[ID_BUTTON_OFFSET + j][k] > 0)
			use_joysticks[i] = 1;
		}
		if (mice[i].enabled && i < n_mouse) {
		    ei = mice[i].eventid[ID_BUTTON_OFFSET + j][k];
		    e = &events[ei];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		    isdigitalbutton (ei);
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
		    isanalog (ei);
		    isdigitalbutton (ei);
		    if (ei > 0)
			use_joysticks[i] = 1;
		}
		if (mice[i].enabled && i < n_mouse) {
		    ei = mice[i].eventid[ID_AXIS_OFFSET + j][k];
		    iscd32 (ei);
		    isparport (ei);
		    ismouse (ei);
		    isanalog (ei);
		    isdigitalbutton (ei);
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
		    isdigitalbutton (ei);
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

    prefs->jports[0].name[0] = prefs->jports[1].name[0] = 0;
    prefs->jports[0].configname[0] = prefs->jports[1].configname[0] = 0;
    compatibility_device[0] = -1;
    compatibility_device[1] = -1;
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	clear_id (&mice[i]);
	clear_id (&joysticks[i]);
    }

    if ((joy = jsem_ismouse (0, prefs)) >= 0) {
	input_get_default_mouse (mice, joy, 0);
	mice[joy].enabled = 1;
	_tcsncpy (prefs->jports[0].name, idev[IDTYPE_MOUSE].get_friendlyname (joy), MAX_JPORTNAME - 1);
	_tcsncpy (prefs->jports[0].configname, idev[IDTYPE_MOUSE].get_uniquename (joy), MAX_JPORTNAME - 1);
    }
    if ((joy = jsem_ismouse (1, prefs)) >= 0) {
	input_get_default_mouse (mice, joy, 1);
	mice[joy].enabled = 1;
	_tcsncpy (prefs->jports[1].name, idev[IDTYPE_MOUSE].get_friendlyname (joy), MAX_JPORTNAME - 1);
	_tcsncpy (prefs->jports[1].configname, idev[IDTYPE_MOUSE].get_uniquename (joy), MAX_JPORTNAME - 1);
    }

    joy = jsem_isjoy (1, prefs);
    if (joy >= 0) {
	used[joy] = 1;
	input_get_default_joystick (joysticks, joy, 1);
	joysticks[joy].enabled = 1;
	_tcsncpy (prefs->jports[1].name, idev[IDTYPE_JOYSTICK].get_friendlyname (joy), MAX_JPORTNAME - 1);
	_tcsncpy (prefs->jports[1].configname, idev[IDTYPE_JOYSTICK].get_uniquename (joy), MAX_JPORTNAME - 1);
    }

    joy = jsem_isjoy (0, prefs);
    if (joy >= 0) {
	used[joy] = 1;
	input_get_default_joystick (joysticks, joy, 0);
	joysticks[joy].enabled = 1;
 	_tcsncpy (prefs->jports[0].name, idev[IDTYPE_JOYSTICK].get_friendlyname (joy), MAX_JPORTNAME - 1);
	_tcsncpy (prefs->jports[0].configname, idev[IDTYPE_JOYSTICK].get_uniquename (joy), MAX_JPORTNAME - 1);
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

static void matchdevices (struct inputdevice_functions *inf, struct uae_input_device *uid)
{
    int i, j;

    for (i = 0; i < inf->get_num (); i++) {
	TCHAR *aname1 = inf->get_friendlyname (i);
	TCHAR *aname2 = inf->get_uniquename (i);
        int match = -1;
	for (j = 0; j < MAX_INPUT_DEVICES; j++) {
	    if (aname2 && uid[j].configname) {
		TCHAR bname[MAX_DPATH];
		TCHAR bname2[MAX_DPATH];
		TCHAR *p1 ,*p2;
		_tcscpy (bname, uid[j].configname);
		_tcscpy (bname2, aname2);
		p1 = _tcschr (bname, ' ');
		p2 = _tcschr (bname2, ' ');
		if (p1 && p2 && p1 - bname == p2 - bname2) {
		    *p1 = 0;
		    *p2 = 0;
		    if (bname && !_tcscmp (bname2, bname)) {
			if (match >= 0)
			    match = -2;
			else
			    match = j;
		    }
		}
		if (match == -2)
		    break;
	    }
	}
	// multiple matches -> use complete local-only id string for comparisons
	if (match == -2) {
	    for (j = 0; j < MAX_INPUT_DEVICES; j++) {
		TCHAR *bname = uid[j].configname;
		if (aname2 && bname && !_tcscmp (aname2, bname))
		    match = j;
	    }
	}
	if (match < 0) {
	    for (j = 0; j < MAX_INPUT_DEVICES; j++) {
		TCHAR *bname = uid[j].name;
		if (aname2 && bname && !_tcscmp (aname2, bname))
		    match = j;
	    }
	}
	if (match >= 0) {
	    j = match;
	    if (j != i) {
		struct uae_input_device *tmp = xmalloc (sizeof (struct uae_input_device));
		memcpy (tmp, &uid[j], sizeof (struct uae_input_device));
		memcpy (&uid[j], &uid[i], sizeof (struct uae_input_device));
		memcpy (&uid[i], tmp, sizeof (struct uae_input_device));
		xfree (tmp);
	    }
	}
    }
    for (i = 0; i < inf->get_num (); i++) {
	if (uid[i].name == NULL)
	    uid[i].name = my_strdup (inf->get_friendlyname (i));
	if (uid[i].configname == NULL)
	    uid[i].configname = my_strdup (inf->get_uniquename (i));
    }
}

static void matchdevices_all (struct uae_prefs *prefs)
{
    int i;
    for (i = 0; i < MAX_INPUT_SETTINGS + 1; i++) {
	matchdevices (&idev[IDTYPE_MOUSE], prefs->mouse_settings[i]);
	matchdevices (&idev[IDTYPE_JOYSTICK], prefs->joystick_settings[i]);
	matchdevices (&idev[IDTYPE_KEYBOARD], prefs->keyboard_settings[i]);
    }
}

void inputdevice_updateconfig (struct uae_prefs *prefs)
{
    int i;

    int j = jsem_isjoy (1, &currprefs);
    int m = jsem_ismouse (1, &currprefs);
    int k = jsem_iskbdjoy (1, &currprefs);

    copyjport (&changed_prefs, &currprefs, 0);
    copyjport (&changed_prefs, &currprefs, 1);
#ifdef RETROPLATFORM
    rp_input_change (0);
    rp_input_change (1);
#endif

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

    matchdevices_all (prefs);

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

}

/* called when devices get inserted or removed */
void inputdevice_devicechange (struct uae_prefs *prefs)
{
    int acc = input_acquired;
    int i, idx;
    TCHAR *jports[2];

    for (i = 0; i < 2; i++) {
	jports[i] = 0;
	idx = inputdevice_getjoyportdevice (prefs->jports[i].id) - JSEM_LASTKBD;
	if (idx >= 0) {
	    struct inputdevice_functions *idf = getidf (idx);
	    int devidx = inputdevice_get_device_index (idx);
	    jports[i] = my_strdup (idf->get_uniquename (devidx));
	}
    }

    inputdevice_unacquire ();
    idev[IDTYPE_JOYSTICK].close ();
    idev[IDTYPE_MOUSE].close ();
    idev[IDTYPE_KEYBOARD].close ();
    idev[IDTYPE_JOYSTICK].init ();
    idev[IDTYPE_MOUSE].init ();
    idev[IDTYPE_KEYBOARD].init ();
    matchdevices (&idev[IDTYPE_MOUSE], mice);
    matchdevices (&idev[IDTYPE_JOYSTICK], joysticks);
    matchdevices (&idev[IDTYPE_KEYBOARD], keyboards);

    for (i = 0; i < 2; i++) {
	freejport (prefs, i);
	if (jports[i])
	    inputdevice_joyport_config (prefs, jports[i], i, 2);
	xfree (jports[i]);
    }

    if (prefs == &changed_prefs)
	inputdevice_copyconfig (&changed_prefs, &currprefs);
    if (acc)
        inputdevice_acquire (TRUE);
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
			    write_log (L"corrupt default keyboard mappings\n");
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
    int i, j;

    inputdevice_init ();
    p->input_joymouse_multiplier = 20;
    p->input_joymouse_deadzone = 33;
    p->input_joystick_deadzone = 33;
    p->input_joymouse_speed = 10;
    p->input_analog_joystick_mult = 100;
    p->input_analog_joystick_offset = 0;
    p->input_mouse_speed = 100;
    p->input_autofire_framecnt = 10;
    for (i = 0; i <= MAX_INPUT_SETTINGS; i++) {
	set_kbr_default (p, i, 0);
	for (j = 0; j < MAX_INPUT_DEVICES; j++) {
	    if (input_get_default_mouse (p->mouse_settings[i], j, j & 1))
		p->mouse_settings[i]->enabled = 1;
	    if (input_get_default_joystick (p->joystick_settings[i], j, j & 1))
		p->joystick_settings[i]->enabled = 1;
	}
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
		int evt = na->eventid[j][sublevdir[state == 0 ? 1 : 0][k]];
		handled |= handle_input_event (evt, state, 1, autofire);
	    }
	    process_custom_event (na, j, state);
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
	sendmmcodes (scancode, state);
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

static struct uae_input_device *get_uid (const struct inputdevice_functions *id, int devnum)
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

static int get_event_data (const struct inputdevice_functions *id, int devnum, int num, int *eventid, TCHAR **custom, int *flags, int sub)
{
    const struct uae_input_device *uid = get_uid (id, devnum);
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

static int put_event_data (const struct inputdevice_functions *id, int devnum, int num, int eventid, TCHAR *custom, int flags, int sub)
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

static int is_event_used (const struct inputdevice_functions *id, int devnum, int isnum, int isevent)
{
    struct uae_input_device *uid = get_uid (id, devnum);
    int num, evt, flag, sub;
    TCHAR *custom;

    for (num = 0; num < id->get_widget_num (devnum); num++) {
	for (sub = 0; sub < MAX_INPUT_SUB_EVENT; sub++) {
	    if (get_event_data (id, devnum, num, &evt, &custom, &flag, sub) >= 0) {
		if (evt == isevent && isnum != num)
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
    int type = gettype (devnum);
    if (type < 0)
	return NULL;
    return &idev[type];
}


/* returns number of devices of type "type" */
int inputdevice_get_device_total (int type)
{
    return idev[type].get_num ();
}
/* returns the name of device */
TCHAR *inputdevice_get_device_name (int type, int devnum)
{
    return idev[type].get_friendlyname (devnum);
}
/* returns machine readable name of device */
TCHAR *inputdevice_get_device_unique_name (int type, int devnum)
{
    return idev[type].get_uniquename (devnum);
}
/* returns state (enabled/disabled) */
int inputdevice_get_device_status (int devnum)
{
    const struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    return uid->enabled;
}

/* set state (enabled/disabled) */
void inputdevice_set_device_status (int devnum, int enabled)
{
    const struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    uid->enabled = enabled;
}

/* returns number of axis/buttons and keys from selected device */
int inputdevice_get_widget_num (int devnum)
{
    const struct inputdevice_functions *idf = getidf (devnum);
    return idf->get_widget_num (inputdevice_get_device_index (devnum));
}

static void get_ename (const struct inputevent *ie, TCHAR *out)
{
    if (!out)
	return;
    if (ie->allow_mask == AM_K)
	_stprintf (out, L"%s (0x%02X)", ie->name, ie->data);
    else
	_tcscpy (out, ie->name);
}

int inputdevice_iterate (int devnum, int num, TCHAR *name, int *af)
{
    const struct inputdevice_functions *idf = getidf (devnum);
    static int id_iterator;
    struct inputevent *ie;
    int mask, data, flags, type;
    int devindex = inputdevice_get_device_index (devnum);
    TCHAR *custom;

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
		if (ie2->allow_mask & mask)
		    break;
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

int inputdevice_get_mapped_name (int devnum, int num, int *pflags, TCHAR *name, TCHAR *custom, int sub)
{
    const struct inputdevice_functions *idf = getidf (devnum);
    const struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    int flags = 0, flag, data;
    int devindex = inputdevice_get_device_index (devnum);
    TCHAR *customp = NULL;

    if (name)
	_tcscpy (name, L"<none>");
    if (custom)
	custom[0] = 0;
    if (pflags)
	*pflags = 0;
    if (uid == 0 || num < 0)
	return 0;
    if (get_event_data (idf, devindex, num, &data, &customp, &flag, sub) < 0)
	return 0;
    if (customp && custom)
	_tcscpy (custom, customp);
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

int inputdevice_set_mapping (int devnum, int num, TCHAR *name, TCHAR *custom, int af, int sub)
{
    const struct inputdevice_functions *idf = getidf (devnum);
    const struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    int eid, data, flag, amask;
    TCHAR ename[256];
    int devindex = inputdevice_get_device_index (devnum);
    TCHAR *customp = NULL;

    if (uid == 0 || num < 0)
	return 0;
    if (name) {
	eid = 1;
	while (events[eid].name) {
	    get_ename (&events[eid], ename);
	    if (!_tcscmp(ename, name)) break;
	    eid++;
	}
	if (!events[eid].name)
	    return 0;
	if (events[eid].allow_mask & AM_INFO)
	    return 0;
    } else {
	eid = 0;
    }
    if (get_event_data (idf, devindex, num, &data, &customp, &flag, sub) < 0)
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

int inputdevice_get_widget_type (int devnum, int num, TCHAR *name)
{
    const struct inputdevice_functions *idf = getidf (devnum);
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

void inputdevice_copyconfig (const struct uae_prefs *src, struct uae_prefs *dst)
{
    int i, j;

    dst->input_selected_setting = src->input_selected_setting;
    dst->input_joymouse_multiplier = src->input_joymouse_multiplier;
    dst->input_joymouse_deadzone = src->input_joymouse_deadzone;
    dst->input_joystick_deadzone = src->input_joystick_deadzone;
    dst->input_joymouse_speed = src->input_joymouse_speed;
    dst->input_mouse_speed = src->input_mouse_speed;
    dst->input_autofire_framecnt = src->input_autofire_framecnt;
    copyjport (src, dst, 0);
    copyjport (src, dst, 1);

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
    const struct inputdevice_functions *idf = getidf (devnum);
    struct uae_input_device *uid = get_uid (idf, inputdevice_get_device_index (devnum));
    int i, j, k, event, unit;
    const struct inputevent *ie, *ie2;

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

void inputdevice_acquire (int allmode)
{
    int i;

    inputdevice_unacquire ();
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (use_joysticks[i] || (allmode && !idev[IDTYPE_JOYSTICK].get_flags (i)))
	    idev[IDTYPE_JOYSTICK].acquire (i, 0);
    }
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (use_mice[i] || (allmode && !idev[IDTYPE_MOUSE].get_flags (i)))
	    idev[IDTYPE_MOUSE].acquire (i, 0);
    }
    for (i = 0; i < MAX_INPUT_DEVICES; i++) {
	if (use_keyboards[i])
	    idev[IDTYPE_KEYBOARD].acquire (i, 0);
    }
//    if (!input_acquired)
//	write_log (L"input devices acquired (%s)\n", allmode ? "all" : "selected only");
    input_acquired = 1;
}

void inputdevice_unacquire (void)
{
    int i;

//    if (input_acquired)
//	write_log (L"input devices unacquired\n");
    input_acquired = 0;
    for (i = 0; i < MAX_INPUT_DEVICES; i++)
	idev[IDTYPE_JOYSTICK].unacquire (i);
    for (i = 0; i < MAX_INPUT_DEVICES; i++)
	idev[IDTYPE_MOUSE].unacquire (i);
    for (i = 0; i < MAX_INPUT_DEVICES; i++)
	idev[IDTYPE_KEYBOARD].unacquire (i);
}

static void testrecord (int type, int num, int wtype, int wnum, int state)
{
    TCHAR tmp[2000];
    tmp[0] = 0;
    wnum += idev[type].get_widget_first (num, wtype);
    idev[type].get_widget_type (num, wnum, tmp, NULL);
    write_log (L"%s: %s %d\n", idev[type].get_friendlyname (num), tmp, state);
}

int inputdevice_istest (void)
{
    return testmode;
}

int inputdevice_testread (TCHAR *name)
{
    testmode = 1;
    idev[IDTYPE_KEYBOARD].read ();
    idev[IDTYPE_JOYSTICK].read ();
    idev[IDTYPE_MOUSE].read ();
    testmode = 0;
    return 0;
}

static int ignoreoldinput (int joy)
{
    if (!use_joysticks[joy])
	return 0;
    if (currprefs.input_selected_setting == 0) {
	if (jsem_isjoy (0, &currprefs) != joy && jsem_isjoy (1, &currprefs) != joy)
	    return 1;
    }
    return 0;
}

void do_fake_joystick (int nr, int *fake)
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
	    switchdevice (&joysticks[joy], button, 1);
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
    if (testmode) {
	testrecord (IDTYPE_MOUSE, mouse, IDEV_WIDGET_BUTTON, button, state);
	return;
    }
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

    if (testmode) {
	testrecord (IDTYPE_JOYSTICK, joy, IDEV_WIDGET_AXIS, axis, state);
	return;
    }
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
	    switchdevice (&joysticks[joy], axis * 2 + (v1 < 0 ? 0 : 1), 0);
	return;
    }
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++)
	handle_input_event (id->eventid[ID_AXIS_OFFSET + axis][i], state, max,
	    id->flags[ID_AXIS_OFFSET + axis][i]);
    id2->states[axis] = state;
}
int getjoystickstate(int joy)
{
    if (testmode)
	return 1;
    return joysticks[joy].enabled;
}

void setmousestate (int mouse, int axis, int data, int isabs)
{
    int i, v;
    double *mouse_p, *oldm_p, d, diff;
    struct uae_input_device *id = &mice[mouse];
    static double fract1[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];
    static double fract2[MAX_INPUT_DEVICES][MAX_INPUT_DEVICE_EVENTS];

    if (testmode) {
	testrecord (IDTYPE_MOUSE, mouse, IDEV_WIDGET_AXIS, axis, data);
	return;
    }
    if (!mice[mouse].enabled) {
	if (isabs && currprefs.input_tablet > 0) {
	    if (axis == 0)
		lastmx = data;
	    else
		lastmy = data;
	    if (axis)
		mousehack_helper ();
	}
	return;
    }
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
	if (axis)
	    mousehack_helper ();
	if (currprefs.input_tablet == TABLET_MOUSEHACK && mousehack_alive ())
	    return;
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
}
int getmousestate (int joy)
{
    if (testmode)
	return 1;
    return mice[joy].enabled;
}

void warpmode (int mode)
{
    int fr, fr2;
    
    write_log (L"%d %d %d\n", currprefs.gfx_framerate, turbo_emulation, mode);

    fr = currprefs.gfx_framerate;
    if (fr == 0)
	fr = -1;
    fr2 = turbo_emulation;
    if (fr2 == -1)
	fr2 = 0;

    if (mode < 0) {
	if (turbo_emulation) {
	    changed_prefs.gfx_framerate = currprefs.gfx_framerate = fr2;
	    turbo_emulation = 0;
	} else {
	    turbo_emulation = fr;
	}
    } else if (mode == 0 && turbo_emulation) {
	if (turbo_emulation > 0)
	    changed_prefs.gfx_framerate = currprefs.gfx_framerate = fr2;
	turbo_emulation = 0;
    } else if (mode > 0 && !turbo_emulation) {
	turbo_emulation = fr;
    }
    if (turbo_emulation) {
	if (!currprefs.cpu_cycle_exact && !currprefs.blitter_cycle_exact)
	    changed_prefs.gfx_framerate = currprefs.gfx_framerate = 10;
	pause_sound ();
    } else {
	resume_sound ();
    }
    compute_vsynctime ();
#ifdef RETROPLATFORM
    rp_turbo (turbo_emulation);
#endif
}

void pausemode (int mode)
{
    if (mode < 0)
	pause_emulation = pause_emulation ? 0 : 1;
    else
	pause_emulation = mode;
}

int jsem_isjoy (int port, const struct uae_prefs *p)
{
    int v = JSEM_DECODEVAL (port, p);
    if (v < JSEM_JOYS)
	return -1;
    v -= JSEM_JOYS;
    if (v >= inputdevice_get_device_total (IDTYPE_JOYSTICK))
	return -1;
    return v;
}

int jsem_ismouse (int port, const struct uae_prefs *p)
{
    int v = JSEM_DECODEVAL (port, p);
    if (v < JSEM_MICE)
	return -1;
    v -= JSEM_MICE;
    if (v >= inputdevice_get_device_total (IDTYPE_MOUSE))
	return -1;
    return v;
}

int jsem_iskbdjoy (int port, const struct uae_prefs *p)
{
    int v = JSEM_DECODEVAL (port, p);
    if (v < JSEM_KBDLAYOUT)
	return -1;
    v -= JSEM_KBDLAYOUT;
    if (v >= JSEM_LASTKBD)
	return -1;
    return v;
}

int inputdevice_joyport_config (struct uae_prefs *p, TCHAR *value, int portnum, int type)
{
    switch (type)
    {
	case 1:
	case 2:
	{
	    int i, j;
	    for (j = 0; j < 2; j++) {
		struct inputdevice_functions *idf;
		int type = IDTYPE_MOUSE;
		int idnum = JSEM_MICE;
		if (j == 0) {
		    type = IDTYPE_JOYSTICK;
		    idnum = JSEM_JOYS;
		}
		idf = &idev[type];
    		for (i = 0; i < idf->get_num (); i++) {
		    TCHAR *name1 = idf->get_friendlyname (i);
		    TCHAR *name2 = idf->get_uniquename (i);
		    if ((name1 && !_tcscmp (name1, value)) || (name2 && !_tcscmp (name2, value))) {
			p->jports[portnum].id = idnum + i;
			return 1;
		    }
		}
	    }
	}
	break;
	case 0:
	{
	    int start = -1, got = 0;
	    TCHAR *pp = 0;
	    if (_tcsncmp (value, L"kbd", 3) == 0) {
		start = JSEM_KBDLAYOUT;
		pp = value + 3;
		got = 1;
	    } else if (_tcsncmp (value, L"joy", 3) == 0) {
		start = JSEM_JOYS;
		pp = value + 3;
		got = 1;
	    } else if (_tcsncmp (value, L"mouse", 5) == 0) {
		start = JSEM_MICE;
		pp = value + 5;
		got = 1;
	    } else if (_tcscmp (value, L"none") == 0) {
		got = 2;
	    }
	    if (got) {
		if (pp) {
		    int v = _tstol (pp);
		    if (start >= 0) {
			if (start == JSEM_KBDLAYOUT && v > 0)
			    v--;
			if (v >= 0) {
			    start += v;
			    got = 2;
			}
		    }
		}
		if (got == 2) {
		    p->jports[portnum].id = start;
		    return 1;
		}
	    }
	}
	break;
    }
    return 0;
}

int inputdevice_getjoyportdevice (int jport)
{
    int idx;
    if (jport < 0) {
	idx = -1;
    } else if (jport >= JSEM_MICE) {
        idx = jport - JSEM_MICE;
        if (idx >= inputdevice_get_device_total (IDTYPE_MOUSE))
	    idx = 0;
	else
	    idx += inputdevice_get_device_total (IDTYPE_JOYSTICK);
	idx += JSEM_LASTKBD;
    } else if (jport >= JSEM_JOYS) {
        idx = jport - JSEM_JOYS;
        if (idx >= inputdevice_get_device_total (IDTYPE_JOYSTICK))
	    idx = 0;
	idx += JSEM_LASTKBD;
    } else {
        idx = jport - JSEM_KBDLAYOUT;
    }
    return idx;
}
