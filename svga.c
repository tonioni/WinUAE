 /*
  * UAE - The Un*x Amiga Emulator
  *
  * SVGAlib interface.
  *
  * (c) 1995 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <vga.h>
#include <vgamouse.h>
#include <vgakeyboard.h>

#include "config.h"
#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "memory.h"
#include "keyboard.h"
#include "xwin.h"
#include "custom.h"
#include "drawing.h"
#include "keybuf.h"
#include "newcpu.h"
#include "tui.h"
#include "gui.h"
#include "picasso96.h"

#define SCODE_CB_UP	103	/* Cursor key block. */
#define SCODE_CB_LEFT	105
#define SCODE_CB_RIGHT	106
#define SCODE_CB_DOWN	108

#define SCODE_INSERT	110
#define SCODE_HOME	102
#define SCODE_PGUP	104
#define SCODE_DELETE	111
#define SCODE_END	107
#define SCODE_PGDN	109

#define SCODE_PRTSCR	99
#define SCODE_SLOCK	70
#define SCODE_BREAK	119

#define SCODE_NUMLOCK	69

#define SCODE_KEYPAD0	82
#define SCODE_KEYPAD1	79
#define SCODE_KEYPAD2	80
#define SCODE_KEYPAD3	81
#define SCODE_KEYPAD4	75
#define SCODE_KEYPAD5	76
#define SCODE_KEYPAD6	77
#define SCODE_KEYPAD7	71
#define SCODE_KEYPAD8	72
#define SCODE_KEYPAD9	73
#define SCODE_KEYPADRET	96
#define SCODE_KEYPADADD	78
#define SCODE_KEYPADSUB	74
#define SCODE_KEYPADMUL	55
#define SCODE_KEYPADDIV	98
#define SCODE_KEYPADDOT 83

#define SCODE_Q		16
#define SCODE_W		17
#define SCODE_E		18
#define SCODE_R		19
#define SCODE_T		20
#define SCODE_Y		21
#define SCODE_U		22
#define SCODE_I		23
#define SCODE_O		24
#define SCODE_P		25

#define SCODE_A		30
#define SCODE_S		31
#define SCODE_D		32
#define SCODE_F		33
#define SCODE_G		34
#define SCODE_H		35
#define SCODE_J		36
#define SCODE_K		37
#define SCODE_L		38

#define SCODE_Z		44
#define SCODE_X		45
#define SCODE_C		46
#define SCODE_V		47
#define SCODE_B		48
#define SCODE_N		49
#define SCODE_M		50

#define SCODE_ESCAPE	1
#define SCODE_ENTER	28
#define SCODE_RCONTROL	97
#define SCODE_CONTROL	97
#define SCODE_RALT	100
#define SCODE_LCONTROL	29
#define SCODE_LALT	56
#define SCODE_SPACE	57

#define SCODE_F1	59
#define SCODE_F2	60
#define SCODE_F3	61
#define SCODE_F4	62
#define SCODE_F5	63
#define SCODE_F6	64
#define SCODE_F7	65
#define SCODE_F8	66
#define SCODE_F9	67
#define SCODE_F10	68
#define SCODE_F11	87
#define SCODE_F12	88

#define SCODE_0		11
#define SCODE_1		2
#define SCODE_2		3
#define SCODE_3		4
#define SCODE_4		5
#define SCODE_5		6
#define SCODE_6	 	7
#define SCODE_7		8
#define SCODE_8		9
#define SCODE_9 	10

#define SCODE_LSHIFT	42
#define SCODE_RSHIFT	54
#define SCODE_TAB	15

#define SCODE_BS	14

#define SCODE_asciicircum	41

#define SCODE_bracketleft	26
#define SCODE_bracketright	27
#define SCODE_comma	51
#define SCODE_period	52
#define SCODE_slash	53
#define SCODE_semicolon	39
#define SCODE_grave	40
#define SCODE_minus	12
#define SCODE_equal	13
#define SCODE_numbersign	43
#define SCODE_ltgt	86

#define SCODE_LWIN95 125
#define SCODE_RWIN95 126
#define SCODE_MWIN95 127

void setup_brkhandler(void)
{
}

static int bitdepth, bit_unit, using_linear, vgamode, current_vgamode, gui_requested;
static vga_modeinfo modeinfo;
static char *linear_mem = NULL;
static int need_dither;
static int screen_is_picasso;
static int picasso_vgamode = -1;
static char picasso_invalid_lines[1200];

static uae_u8 dither_buf[1000]; /* I hate having to think about array bounds */

#define MAX_SCREEN_MODES 9

static int x_size_table[MAX_SCREEN_MODES] = { 320, 320, 320, 640, 640, 800, 1024, 1152, 1280 };
static int y_size_table[MAX_SCREEN_MODES] = { 200, 240, 400, 350, 480, 600, 768, 864, 1024 };

static int vga_mode_table[MAX_SCREEN_MODES][MAX_COLOR_MODES+1] =
 { { G320x200x256, G320x200x32K, G320x200x64K, G320x200x256, G320x200x16, G320x200x16M32 },
   { G320x240x256, -1, -1, G320x240x256, -1, -1 },
   { G320x400x256, -1, -1, G320x400x256, -1, -1 },
   { -1, -1, -1, -1, G640x350x16, -1 },
   { G640x480x256, G640x480x32K, G640x480x64K, G640x480x256, G640x480x16, G640x480x16M32 },
   { G800x600x256, G800x600x32K, G800x600x64K, G800x600x256, G800x600x16, G800x600x16M32 },
   { G1024x768x256, G1024x768x32K, G1024x768x64K, G1024x768x256, G1024x768x16, G1024x768x16M32 },
   { G1152x864x256, G1152x864x32K, G1152x864x64K, G1152x864x256, G1152x864x16, G1152x864x16M32 },
   { G1280x1024x256, G1280x1024x32K, G1280x1024x64K, G1280x1024x256, G1280x1024x16, G1280x1024x16M32 }
 };

static int mode_bitdepth[MAX_COLOR_MODES+1][3] =
  { { 8, 8, 0 }, { 15, 16, 0 }, { 16, 16, 0 }, { 8, 8, 1 }, { 4, 8, 1 }, { 24, 32, 0 } };

struct bstring *video_mode_menu = NULL;

void flush_line(int y)
{
    int target_y = y;
    char *addr;

    if (linear_mem != NULL && !need_dither)
	return;

    addr = gfxvidinfo.linemem;
    if (addr == NULL)
	addr = gfxvidinfo.bufmem + y*gfxvidinfo.rowbytes;

    if (linear_mem == NULL) {
	if (target_y < modeinfo.height && target_y >= 0) {
	    if (need_dither) {
		DitherLine (dither_buf, (uae_u16 *)addr, 0, y, gfxvidinfo.width, bit_unit);
		addr = dither_buf;
	    }
	    vga_drawscanline(target_y, addr);
	}
    } else {
	if (need_dither && target_y >= 0) {
	    DitherLine (linear_mem + modeinfo.linewidth * target_y, (uae_u16 *)addr, 0, y,
			gfxvidinfo.width, bit_unit);
	}
    }
}

void flush_block (int a, int b)
{
    abort();
}

void flush_screen (int a, int b)
{
}

static int colors_allocated;
static long palette_entries[256][3];

static void restore_vga_colors (void)
{
    int i;
    if (gfxvidinfo.pixbytes != 1)
	return;
    for (i = 0; i < 256; i++)
	vga_setpalette (i, palette_entries[i][0], palette_entries[i][1], palette_entries[i][2]);
}

static int get_color (int r, int g, int b, xcolnr *cnp)
{
    if (colors_allocated == 256)
	return -1;
    *cnp = colors_allocated;
    palette_entries[colors_allocated][0] = doMask (r, 6, 0);
    palette_entries[colors_allocated][1] = doMask (g, 6, 0);
    palette_entries[colors_allocated][2] = doMask (b, 6, 0);
    vga_setpalette(colors_allocated, doMask (r, 6, 0), doMask (g, 6, 0), doMask (b, 6, 0));
    colors_allocated++;
    return 1;
}

static void init_colors (void)
{
    int i;
    if (need_dither) {
	setup_dither (bitdepth, get_color);
    } else {
	int rw = 5, gw = 5, bw = 5;
	colors_allocated = 0;
	if (currprefs.color_mode == 2) gw = 6;

	switch (gfxvidinfo.pixbytes) {
	 case 4:
	    alloc_colors64k (8, 8, 8, 16, 8, 0);
	    break;
	 case 2:
	    alloc_colors64k (rw, gw, bw, gw+bw, bw, 0);
	    break;
	 case 1:
	    alloc_colors256 (get_color);
	    break;
	 default:
	    abort();
	}
    }
    switch (gfxvidinfo.pixbytes) {
     case 2:
	for (i = 0; i < 4096; i++)
	    xcolors[i] = xcolors[i] * 0x00010001;
	gfxvidinfo.can_double = 1;
	break;
     case 1:
	for (i = 0; i < 4096; i++)
	    xcolors[i] = xcolors[i] * 0x01010101;
	gfxvidinfo.can_double = 1;
	break;
     default:
	gfxvidinfo.can_double = 0;
	break;
    }
}

static int keystate[256];

static int scancode2amiga (int scancode)
{
    switch (scancode) {
     case SCODE_A: return AK_A;
     case SCODE_B: return AK_B;
     case SCODE_C: return AK_C;
     case SCODE_D: return AK_D;
     case SCODE_E: return AK_E;
     case SCODE_F: return AK_F;
     case SCODE_G: return AK_G;
     case SCODE_H: return AK_H;
     case SCODE_I: return AK_I;
     case SCODE_J: return AK_J;
     case SCODE_K: return AK_K;
     case SCODE_L: return AK_L;
     case SCODE_M: return AK_M;
     case SCODE_N: return AK_N;
     case SCODE_O: return AK_O;
     case SCODE_P: return AK_P;
     case SCODE_Q: return AK_Q;
     case SCODE_R: return AK_R;
     case SCODE_S: return AK_S;
     case SCODE_T: return AK_T;
     case SCODE_U: return AK_U;
     case SCODE_V: return AK_V;
     case SCODE_W: return AK_W;
     case SCODE_X: return AK_X;
     case SCODE_Y: return AK_Y;
     case SCODE_Z: return AK_Z;

     case SCODE_0: return AK_0;
     case SCODE_1: return AK_1;
     case SCODE_2: return AK_2;
     case SCODE_3: return AK_3;
     case SCODE_4: return AK_4;
     case SCODE_5: return AK_5;
     case SCODE_6: return AK_6;
     case SCODE_7: return AK_7;
     case SCODE_8: return AK_8;
     case SCODE_9: return AK_9;

     case SCODE_KEYPAD0: return AK_NP0;
     case SCODE_KEYPAD1: return AK_NP1;
     case SCODE_KEYPAD2: return AK_NP2;
     case SCODE_KEYPAD3: return AK_NP3;
     case SCODE_KEYPAD4: return AK_NP4;
     case SCODE_KEYPAD5: return AK_NP5;
     case SCODE_KEYPAD6: return AK_NP6;
     case SCODE_KEYPAD7: return AK_NP7;
     case SCODE_KEYPAD8: return AK_NP8;
     case SCODE_KEYPAD9: return AK_NP9;

     case SCODE_KEYPADADD: return AK_NPADD;
     case SCODE_KEYPADSUB: return AK_NPSUB;
     case SCODE_KEYPADMUL: return AK_NPMUL;
     case SCODE_KEYPADDIV: return AK_NPDIV;
     case SCODE_KEYPADRET: return AK_ENT;
     case SCODE_KEYPADDOT: return AK_NPDEL;

     case SCODE_F1: return AK_F1;
     case SCODE_F2: return AK_F2;
     case SCODE_F3: return AK_F3;
     case SCODE_F4: return AK_F4;
     case SCODE_F5: return AK_F5;
     case SCODE_F6: return AK_F6;
     case SCODE_F7: return AK_F7;
     case SCODE_F8: return AK_F8;
     case SCODE_F9: return AK_F9;
     case SCODE_F10: return AK_F10;

     case SCODE_BS: return AK_BS;
     case SCODE_LCONTROL: return AK_CTRL;
     case SCODE_RCONTROL: return AK_RCTRL;
     case SCODE_TAB: return AK_TAB;
     case SCODE_LALT: return AK_LALT;
     case SCODE_RALT: return AK_RALT;
     case SCODE_ENTER: return AK_RET;
     case SCODE_SPACE: return AK_SPC;
     case SCODE_LSHIFT: return AK_LSH;
     case SCODE_RSHIFT: return AK_RSH;
     case SCODE_ESCAPE: return AK_ESC;

     case SCODE_INSERT: return AK_HELP;
     case SCODE_END: return AK_NPRPAREN;
     case SCODE_HOME: return AK_NPLPAREN;

     case SCODE_DELETE: return AK_DEL;
     case SCODE_CB_UP: return AK_UP;
     case SCODE_CB_DOWN: return AK_DN;
     case SCODE_CB_LEFT: return AK_LF;
     case SCODE_CB_RIGHT: return AK_RT;

     case SCODE_PRTSCR: return AK_BACKSLASH;
     case SCODE_asciicircum: return AK_BACKQUOTE;
     case SCODE_bracketleft: return AK_LBRACKET;
     case SCODE_bracketright: return AK_RBRACKET;
     case SCODE_comma: return AK_COMMA;
     case SCODE_period: return AK_PERIOD;
     case SCODE_slash: return AK_SLASH;
     case SCODE_semicolon: return AK_SEMICOLON;
     case SCODE_grave: return AK_QUOTE;
     case SCODE_minus: return AK_MINUS;
     case SCODE_equal: return AK_EQUAL;

	/* This one turns off screen updates. */
     case SCODE_SLOCK: return AK_inhibit;

     case SCODE_PGUP: case SCODE_RWIN95: return AK_RAMI;
     case SCODE_PGDN: case SCODE_LWIN95: return AK_LAMI;

/*#ifdef KBD_LANG_DE*/
     case SCODE_numbersign: return AK_NUMBERSIGN;
     case SCODE_ltgt: return AK_LTGT;
/*#endif*/
    }
    return -1;
}

static void my_kbd_handler (int scancode, int newstate)
{
    int akey = scancode2amiga (scancode);

    assert (scancode >= 0 && scancode < 0x100);
    if (scancode == SCODE_F12) {
	uae_quit ();
    } else if (scancode == SCODE_F11) {
	gui_requested = 1;
    }
    if (keystate[scancode] == newstate)
	return;

    keystate[scancode] = newstate;

    if (akey == -1)
	return;

    if (newstate == KEY_EVENTPRESS) {
	if (akey == AK_inhibit)
	    toggle_inhibit_frame (0);
	else
	    record_key (akey << 1);
    } else
	record_key ((akey << 1) | 1);

    /* "Affengriff" */
    if ((keystate[AK_CTRL] || keystate[AK_RCTRL]) && keystate[AK_LAMI] && keystate[AK_RAMI])
	uae_reset ();
}

static void leave_graphics_mode (void)
{
    keyboard_close ();
    mouse_close ();
    sleep (1); /* Maybe this will fix the "screen full of garbage" problem */
    current_vgamode = TEXT;
    vga_setmode (TEXT);
}

static int post_enter_graphics (void)
{
    vga_setmousesupport (1);
    mouse_init("/dev/mouse", vga_getmousetype (), 10);
    if (keyboard_init() != 0) {
	leave_graphics_mode ();
	write_log ("Are you sure you have a keyboard??\n");
	return 0;
    }
    keyboard_seteventhandler (my_kbd_handler);
    keyboard_translatekeys (DONT_CATCH_CTRLC);

    mouse_setxrange (-1000, 1000);
    mouse_setyrange (-1000, 1000);
    mouse_setposition (0, 0);

    return 1;
}

static int enter_graphics_mode (int which)
{
    int oldmode = current_vgamode;
    vga_setmode (TEXT);
    if (vga_setmode (which) < 0) {
	sleep(1);
	vga_setmode (TEXT);
	write_log ("SVGAlib doesn't like my video mode (%d). Giving up.\n", which);
	return 0;
    }
    current_vgamode = which;

    linear_mem = 0;
    if ((modeinfo.flags & CAPABLE_LINEAR) && ! currprefs.svga_no_linear) {
	int val = vga_setlinearaddressing ();
	int new_ul = val != -1 ? !need_dither : 0;
	if (using_linear == -1)
	    using_linear = new_ul;
	else
	    if (using_linear != new_ul) {
		leave_graphics_mode ();
		write_log ("SVGAlib feeling not sure about linear modes???\n");
		abort ();
	    }
	if (val != -1) {
	    linear_mem = (char *)vga_getgraphmem ();
	    write_log ("Using linear addressing: %p.\n", linear_mem);
	}
    }

    return post_enter_graphics ();
}

static int enter_graphics_mode_picasso (int which)
{
    int oldmode = current_vgamode;
    if (which == oldmode)
	return 1;

    vga_setmode (TEXT);
    if (vga_setmode (which) < 0) {
	sleep (1);
	vga_setmode (TEXT);
	write_log ("SVGAlib doesn't like my video mode (%d). Giving up.\n", which);
	exit (1);
    }
    current_vgamode = which;

    linear_mem = 0;
    if ((modeinfo.flags & CAPABLE_LINEAR) && ! currprefs.svga_no_linear) {
	int val = vga_setlinearaddressing ();
	if (val != -1) {
	    linear_mem = (char *)vga_getgraphmem ();
	    write_log ("Using linear addressing: %p.\n", linear_mem);
	}
    }

    keyboard_close ();
    mouse_close ();
    return post_enter_graphics ();
}

int graphics_setup (void)
{
    int i,j, count = 1;

    vga_init();

    current_vgamode = TEXT;

    for (i = 0; i < MAX_SCREEN_MODES; i++) {
	/* Ignore the larger modes which only make sense for Picasso screens.  */
	if (x_size_table[i] > 800 || y_size_table[i] > 600)
	    continue;

	for (j = 0; j < MAX_COLOR_MODES+1; j++) {
	    /* Delete modes which are not available on this card.  */
	    if (!vga_hasmode (vga_mode_table[i][j])) {
		vga_mode_table[i][j] = -1;
	    }

	    if (vga_mode_table[i][j] != -1)
		count++;
	}
    }

    video_mode_menu = (struct bstring *)malloc (sizeof (struct bstring)*count);
    memset (video_mode_menu, 0, sizeof (struct bstring)*count);
    count = 0;

    for (i = 0; i < MAX_SCREEN_MODES; i++) {
	/* Ignore the larger modes which only make sense for Picasso screens.  */
	if (x_size_table[i] > 800 || y_size_table[i] > 600)
	    continue;

	for (j = 0; j < MAX_COLOR_MODES+1; j++) {
	    char buf[80];
	    if (vga_mode_table[i][j] == -1)
		continue;

	    sprintf (buf, "%3dx%d, %s", x_size_table[i], y_size_table[i],
		     colormodes[j]);
	    video_mode_menu[count].val = -1;
	    video_mode_menu[count++].data = strdup(buf);
	}
    }
    video_mode_menu[count].val = -3;
    video_mode_menu[count++].data = NULL;
    return 1;
}

void vidmode_menu_selected(int m)
{
    int i, j;
    for (i = 0; i < MAX_SCREEN_MODES; i++) {
	/* Ignore the larger modes which only make sense for Picasso screens.  */
	if (x_size_table[i] > 800 || y_size_table[i] > 600)
	    continue;
	for (j = 0; j < MAX_COLOR_MODES+1; j++) {
	    if (vga_mode_table[i][j] != -1)
		if (!m--)
		    goto found;

	}
    }
    abort();

    found:
    currprefs.gfx_width = x_size_table[i];
    currprefs.gfx_height = y_size_table[i];
    currprefs.color_mode = j;
}

static int select_mode_from_prefs (void)
{
    int mode_nr0, mode_nr;
    int i;

    if (currprefs.color_mode > 5)
	write_log ("Bad color mode selected. Using default.\n"), currprefs.color_mode = 0;

    mode_nr0 = 0;
    for (i = 1; i < MAX_SCREEN_MODES; i++) {
	if (x_size_table[mode_nr0] >= currprefs.gfx_width)
	    break;
	if (x_size_table[i-1] != x_size_table[i])
	    mode_nr0 = i;
    }
    mode_nr = -1;
    for (i = mode_nr0; i < MAX_SCREEN_MODES && x_size_table[i] == x_size_table[mode_nr0]; i++) {
	if ((y_size_table[i] >= currprefs.gfx_height
	     || i + 1 == MAX_SCREEN_MODES
	     || x_size_table[i+1] != x_size_table[mode_nr0])
	    && vga_mode_table[i][currprefs.color_mode] != -1)
	{
	    mode_nr = i;
	    break;
	}
    }
    if (mode_nr == -1) {
	write_log ("Sorry, this combination of color and video mode is not supported.\n");
	return 0;
    }
    vgamode = vga_mode_table[mode_nr][currprefs.color_mode];
    if (vgamode == -1) {
	write_log ("Bug!\n");
	abort ();
    }
    write_log ("Desired resolution: %dx%d, using: %dx%d\n",
	     currprefs.gfx_width, currprefs.gfx_height,
	     x_size_table[mode_nr], y_size_table[mode_nr]);

    currprefs.gfx_width = x_size_table[mode_nr];
    currprefs.gfx_height = y_size_table[mode_nr];

    return 1;
}

int graphics_init (void)
{
    int i;
    need_dither = 0;
    screen_is_picasso = 0;

    if (!select_mode_from_prefs ())
	return 0;

    bitdepth = mode_bitdepth[currprefs.color_mode][0];
    bit_unit = mode_bitdepth[currprefs.color_mode][1];
    need_dither = mode_bitdepth[currprefs.color_mode][2];

    modeinfo = *vga_getmodeinfo (vgamode);

    gfxvidinfo.pixbytes = modeinfo.bytesperpixel;
    if (!need_dither) {
	if (modeinfo.bytesperpixel == 0) {
	    printf("Got a bogus value from SVGAlib...\n");
	    gfxvidinfo.pixbytes = 1;
	}
    } else {
	gfxvidinfo.pixbytes = 2;
    }

    using_linear = -1;

    if (!enter_graphics_mode (vgamode))
	return 0;

    sleep(2);
    gfxvidinfo.maxblocklines = 0;

    gfxvidinfo.width = modeinfo.width;
    gfxvidinfo.height = modeinfo.height;

    if (linear_mem != NULL && !need_dither) {
	gfxvidinfo.bufmem = linear_mem;
	gfxvidinfo.rowbytes = modeinfo.linewidth;
    } else {
	gfxvidinfo.rowbytes = (modeinfo.width * gfxvidinfo.pixbytes + 3) & ~3;
#if 1
	gfxvidinfo.bufmem = malloc (gfxvidinfo.rowbytes);
	gfxvidinfo.linemem = gfxvidinfo.bufmem;
	memset (gfxvidinfo.bufmem, 0, gfxvidinfo.rowbytes);
#else
	gfxvidinfo.bufmem = malloc(gfxvidinfo.rowbytes * modeinfo.height);
	memset (gfxvidinfo.bufmem, 0, gfxvidinfo.rowbytes * modeinfo.height);
#endif
	gfxvidinfo.emergmem = 0;
    }
    printf ("rowbytes %d\n", gfxvidinfo.rowbytes);
    init_colors ();
    buttonstate[0] = buttonstate[1] = buttonstate[2] = 0;
    for(i = 0; i < 256; i++)
	keystate[i] = 0;

    lastmx = lastmy = 0;
    newmousecounters = 0;

    return 1;
}

void graphics_leave (void)
{
    leave_graphics_mode ();
    dumpcustom();
}

void handle_events (void)
{
    int button = mouse_getbutton ();

    gui_requested = 0;
    keyboard_update ();
    mouse_update ();
    lastmx += mouse_getx ();
    lastmy += mouse_gety ();
    mouse_setposition (0, 0);

    buttonstate[0] = button & 4;
    buttonstate[1] = button & 2;
    buttonstate[2] = button & 1;

#ifdef PICASSO96
    if (screen_is_picasso && !picasso_vidinfo.extra_mem) {
	int i;
	char *addr = gfxmemory + (picasso96_state.Address - gfxmem_start);
	for (i = 0; i < picasso_vidinfo.height; i++, addr += picasso96_state.BytesPerRow) {
	    if (!picasso_invalid_lines[i])
		continue;
	    picasso_invalid_lines[i] = 0;
	    vga_drawscanline (i, addr);
	}
    }
#endif

    if (!screen_is_picasso && gui_requested) {
	leave_graphics_mode ();
	gui_changesettings ();
	enter_graphics_mode (vgamode);
	if (linear_mem != NULL && !need_dither)
	    gfxvidinfo.bufmem = linear_mem;
	restore_vga_colors ();
	notice_screen_contents_lost ();
    }
}

int check_prefs_changed_gfx (void)
{
    return 0;
}

int debuggable (void)
{
    return 0;
}

int needmousehack (void)
{
    return 0;
}

void LED (int on)
{
}

#ifdef PICASSO96

void DX_Invalidate (int first, int last)
{
    do {
	picasso_invalid_lines[first] = 1;
	first++;
    } while (first <= last);
}

int DX_BitsPerCannon (void)
{
    return 8;
}

void DX_SetPalette(int start, int count)
{
    if (!screen_is_picasso || picasso_vidinfo.pixbytes != 1)
	return;

    while (count-- > 0) {
	vga_setpalette(start, picasso96_state.CLUT[start].Red * 63 / 255,
		       picasso96_state.CLUT[start].Green * 63 / 255,
		       picasso96_state.CLUT[start].Blue * 63 / 255);
	start++;
    }
}

int DX_FillResolutions (uae_u16 *ppixel_format)
{
    int i, count = 0;
    uae_u16 format = 0;

    for (i = 0; i < MAX_SCREEN_MODES; i++) {
	int mode = vga_mode_table[i][0];
	if (mode != -1) {
	    DisplayModes[count].res.width = x_size_table[i];
	    DisplayModes[count].res.height = y_size_table[i];
	    DisplayModes[count].depth = 1;
	    DisplayModes[count].refresh = 75;
	    count++;
	    format |= RGBFF_CHUNKY;
	}
	mode = vga_mode_table[i][2];
	if (mode != -1) {
	    DisplayModes[count].res.width = x_size_table[i];
	    DisplayModes[count].res.height = y_size_table[i];
	    DisplayModes[count].depth = 2;
	    DisplayModes[count].refresh = 75;
	    count++;
	    format |= RGBFF_R5G6B5PC;
	}
	mode = vga_mode_table[i][5];
	if (mode != -1) {
	    DisplayModes[count].res.width = x_size_table[i];
	    DisplayModes[count].res.height = y_size_table[i];
	    DisplayModes[count].depth = 4;
	    DisplayModes[count].refresh = 75;
	    count++;
	    format |= RGBFF_B8G8R8A8;
	}
    }

    *ppixel_format = format;
    return count;
}

static void set_window_for_picasso (void)
{
    enter_graphics_mode_picasso (picasso_vgamode);
    if (linear_mem != NULL)
	picasso_vidinfo.extra_mem = 1;
    else
	picasso_vidinfo.extra_mem = 0;
    printf ("em: %d\n", picasso_vidinfo.extra_mem);
    DX_SetPalette (0, 256);
}

static void set_window_for_amiga (void)
{
    leave_graphics_mode ();
    enter_graphics_mode (vgamode);
    if (linear_mem != NULL && !need_dither)
	gfxvidinfo.bufmem = linear_mem;

    restore_vga_colors ();
}

void gfx_set_picasso_modeinfo (int w, int h, int depth, int rgbfmt)
{
    vga_modeinfo *info;
    int i, mode;

    for (i = 0; i < MAX_SCREEN_MODES; i++)
	if (x_size_table[i] == w && y_size_table[i] == h)
	    break;
    printf ("::: %d %d %d, %d\n", w, h, depth, i);
    if (i == MAX_SCREEN_MODES)
	abort ();
    mode = (depth == 8 ? vga_mode_table[i][0]
	    : depth == 16 ? vga_mode_table[i][2]
	    : depth == 32 ? vga_mode_table[i][5]
	    : -1);
    printf ("::: %d\n", mode);
    if (mode == -1)
	abort ();

    info = vga_getmodeinfo (mode);
    printf ("::: %d\n", info->linewidth);
    picasso_vgamode = mode;
    picasso_vidinfo.width = w;
    picasso_vidinfo.height = h;
    picasso_vidinfo.depth = depth;
    picasso_vidinfo.pixbytes = depth>>3;
    picasso_vidinfo.rowbytes = info->linewidth;
    picasso_vidinfo.rgbformat = (depth == 8 ? RGBFB_CHUNKY
				 : depth == 16 ? RGBFB_R5G6B5PC
				 : RGBFB_B8G8R8A8);
    if (screen_is_picasso)
	set_window_for_picasso ();
}

void gfx_set_picasso_baseaddr (uaecptr a)
{
}

void gfx_set_picasso_state (int on)
{
    if (on == screen_is_picasso)
	return;
    screen_is_picasso = on;
    if (on)
	set_window_for_picasso ();
    else
	set_window_for_amiga ();
}

uae_u8 *gfx_lock_picasso (void)
{
    return linear_mem;
}
void gfx_unlock_picasso (void)
{
}
#endif

int lockscr (void)
{
    return 1;
}

void unlockscr (void)
{
}

void target_save_options (FILE *f, struct uae_prefs *p)
{
    fprintf (f, "svga.no_linear=%s\n", p->svga_no_linear ? "true" : "false");
}

int target_parse_option (struct uae_prefs *p, char *option, char *value)
{
    return (cfgfile_yesno (option, value, "no_linear", &p->svga_no_linear));
}

/* Dummy entry to make it compile */
void DX_SetPalette_vsync(void)
{}
