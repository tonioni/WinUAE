 /*
  * UAE - The Un*x Amiga Emulator
  *
  * [n]curses output.
  *
  * There are 17 color modes:
  *  -H0/-H1 are black/white output
  *  -H2 through -H16 give you different color allocation strategies. On my
  *    system, -H14 seems to give nice results.
  *
  * Copyright 1997 Samuel Devulder, Bernd Schmidt
  */

/****************************************************************************/

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <signal.h>

/****************************************************************************/

#include "config.h"
#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "xwin.h"
#include "keyboard.h"
#include "keybuf.h"
#include "disk.h"
#include "debug.h"
#include "gui.h"

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

/****************************************************************************/

#define MAXGRAYCHAR 128

enum {
    MYCOLOR_BLACK, MYCOLOR_RED, MYCOLOR_GREEN, MYCOLOR_BLUE,
    MYCOLOR_YELLOW, MYCOLOR_CYAN, MYCOLOR_MAGENTA, MYCOLOR_WHITE
};

static int mycolor2curses_map [] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE,
    COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE
};

static int mycolor2pair_map[] = { 1,2,3,4,5,6,7,8 };

static chtype graychar[MAXGRAYCHAR];
static int maxc,max_graychar;
static int curses_on;

static int *x2graymap;

/* Keyboard and mouse */

static int keystate[256];
static int keydelay = 20;

static void curses_exit(void);

/****************************************************************************/

static RETSIGTYPE sigbrkhandler(int foo)
{
    curses_exit();
    activate_debugger();
}

void setup_brkhandler(void)
{
    struct sigaction sa;
    sa.sa_handler = sigbrkhandler;
    sa.sa_flags = 0;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
}

/***************************************************************************/

static void curses_insert_disk(void)
{
    curses_exit();
    gui_changesettings();
    flush_screen(0,0);
}

/****************************************************************************/

/*
 * old:	fmt = " .,:=(Io^vM^vb*X^#M^vX*boI(=:. ^b^vobX^#M" doesn't work: "^vXb*oI(=:. ";
 * good:	fmt = " .':;=(IoJpgFPEB#^vgpJoI(=;:'. ^v^b=(IoJpgFPEB";
 *
 * 	fmt = " .,:=(Io*b^vM^vX^#M^vXb*oI(=:. ";
 */

static void init_graychar(void)
{
    chtype *p = graychar;
    chtype attrs;
    int i,j;
    char *fmt;

    attrs = termattrs();
    if ((currprefs.color_mode & 1) == 0 && (attrs & (A_REVERSE | A_BOLD)))
	fmt = " .':;=(IoJpgFPEB#^vgpJoI(=;:'. ^v^boJpgFPEB";
    else if ((currprefs.color_mode & 1) == 0 && (attrs & A_REVERSE))
	fmt = " .':;=(IoJpgFPEB#^vgpJoI(=;:'. ";
    else
	/* One could find a better pattern.. */
	fmt = " .`'^^\",:;i!1Il+=tfjxznuvyZYXHUOQ0MWB";
    attrs = A_NORMAL | COLOR_PAIR (0);
    while(*fmt) {
	if(*fmt == '^') {
	    ++fmt;
	    switch(*fmt) {
		case 's': case 'S': attrs ^= A_STANDOUT; break;
		case 'v': case 'V': attrs ^= A_REVERSE; break;
		case 'b': case 'B': attrs ^= A_BOLD; break;
		case 'd': case 'D': attrs ^= A_DIM; break;
		case 'u': case 'U': attrs ^= A_UNDERLINE; break;
		case 'p': case 'P': attrs  = A_NORMAL; break;
		case '#': if(ACS_CKBOARD == ':')
			       *p++ = (attrs | '#');
			  else *p++ = (attrs | ACS_CKBOARD); break;
		default:  *p++ = (attrs | *fmt); break;
	    }
	    ++fmt;
	} else *p++ = (attrs | *fmt++);
	if(p >= graychar + MAXGRAYCHAR) break;
    }
    max_graychar = (p - graychar) - 1;

    for (i = 0; i <= maxc; i++)
	x2graymap[i] = i * max_graychar / maxc;
#if 0
    for(j=0;j<LINES;++j) {
	move(j,0);
	for(i=0;i<COLS;++i) addch(graychar[i % (max_graychar+1)]);
    }
    refresh();
    sleep(3);
#endif
}

static int x_map[900], y_map[700], y_rev_map [700];


/****************************************************************************/

static void init_colors(void)
{
    int i;

    maxc = 0;

    for(i = 0; i < 4096; ++i) {
	int r,g,b,r1,g1,b1;
	int m, comp;
	int ctype;

	r =  i >> 8;
	g = (i >> 4) & 15;
	b =  i & 15;

	xcolors[i] = (77 * r + 151 * g + 28 * b)/16;
	if(xcolors[i] > maxc)
	    maxc = xcolors[i];
	m = r;
	if (g > m)
	    m = g;
	if (b > m)
	    m = b;
	if (m == 0) {
	    xcolors[i] |= MYCOLOR_WHITE << 8; /* to get gray instead of black in dark areas */
	    continue;
	}

	if ((currprefs.color_mode & ~1) != 0) {
	    r1 = r*15 / m;
	    g1 = g*15 / m;
	    b1 = b*15 / m;

	    comp = 8;
	    for (;;) {
		if (b1 < comp) {
		    if (r1 < comp)
			ctype = MYCOLOR_GREEN;
		    else if (g1 < comp)
			ctype = MYCOLOR_RED;
		    else
			ctype = MYCOLOR_YELLOW;
		} else {
		    if (r1 < comp) {
			if (g1 < comp)
			    ctype = MYCOLOR_BLUE;
			else
			    ctype = MYCOLOR_CYAN;
		    } else if (g1 < comp)
			    ctype = MYCOLOR_MAGENTA;
		    else {
			comp += 4;
			if (comp == 12 && (currprefs.color_mode & 2) != 0)
			    continue;
			ctype = MYCOLOR_WHITE;
		    }
		}
		break;
	    }
	    if (currprefs.color_mode & 8) {
		if (ctype == MYCOLOR_BLUE && xcolors[i] > /*27*/50)
		    ctype = r1 > (g1+2) ? MYCOLOR_MAGENTA : MYCOLOR_CYAN;
		if (ctype == MYCOLOR_RED && xcolors[i] > /*75*/ 90)
		    ctype = b1 > (g1+6) ? MYCOLOR_MAGENTA : MYCOLOR_YELLOW;
	    }
	    xcolors[i] |= ctype << 8;
	}
    }
    if (currprefs.color_mode & 4) {
	int j;
	for (j = MYCOLOR_RED; j < MYCOLOR_WHITE; j++) {
	    int best = 0, maxv = 0;
	    int multi, divi;

	    for (i = 0; i < 4096; i++)
		if ((xcolors[i] & 255) > maxv && (xcolors[i] >> 8) == j) {
		    best = i;
		    maxv = (xcolors[best] & 255);
		}
	    /* Now maxv is the highest intensity a color of type J is supposed to have.
	     * In  reality, it will most likely only have intensity maxv*multi/divi.
	     * We try to correct this. */
	    maxv = maxv * 256 / maxc;

	    divi = 256;
	    switch (j) {
	     case MYCOLOR_RED:     multi = 77; break;
	     case MYCOLOR_GREEN:   multi = 151; break;
	     case MYCOLOR_BLUE:    multi = 28; break;
	     case MYCOLOR_YELLOW:  multi = 228; break;
	     case MYCOLOR_CYAN:    multi = 179; break;
	     case MYCOLOR_MAGENTA: multi = 105; break;
	     default: abort();
	    }
#if 1 /* This makes the correction less extreme */
	    if (! (currprefs.color_mode & 8))
		multi = (multi + maxv) / 2;
#endif
	    for (i = 0; i < 4096; i++) {
		int v = xcolors[i];
		if ((v >> 8) != j)
		    continue;
		v &= 255;
		/* I don't think either of these is completely correct, but
		 * the first one produces rather good results. */
#if 1
		v = v * divi / multi;
		if (v > maxc)
		    v = maxc;
#else
		v = v * 256 / maxv);
		if (v > maxc)
		    /*maxc = v*/abort();
#endif
		xcolors[i] = v | (j << 8);
	    }
	}
    }
    x2graymap = (int *)malloc(sizeof(int) * (maxc+1));
}

static void curses_init(void)
{
    initscr ();

    start_color ();
    if (! has_colors () || COLOR_PAIRS < 20 /* whatever */)
	currprefs.color_mode &= 1;
    else {
	init_pair (1, COLOR_BLACK, COLOR_BLACK);
	init_pair (2, COLOR_RED, COLOR_BLACK);
	init_pair (3, COLOR_GREEN, COLOR_BLACK);
	init_pair (4, COLOR_BLUE, COLOR_BLACK);
	init_pair (5, COLOR_YELLOW, COLOR_BLACK);
	init_pair (6, COLOR_CYAN, COLOR_BLACK);
	init_pair (7, COLOR_MAGENTA, COLOR_BLACK);
	init_pair (8, COLOR_WHITE, COLOR_BLACK);
    }
    printf ("curses_init: %d pairs available\n", COLOR_PAIRS);

    cbreak(); noecho();
    nonl (); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    leaveok(stdscr, TRUE);

    attron (A_NORMAL | COLOR_PAIR (0));
    bkgd(' '|COLOR_PAIR(0));

#ifdef NCURSES_MOUSE_VERSION
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED |
	      BUTTON2_PRESSED | BUTTON2_RELEASED |
	      BUTTON3_PRESSED | BUTTON3_RELEASED |
	      REPORT_MOUSE_POSITION, NULL);
#endif

    init_graychar();
    curses_on = 1;
}

static void curses_exit(void)
{
#ifdef NCURSES_MOUSE_VERSION
    mousemask(0, NULL);
#endif

    nocbreak(); echo(); nl(); intrflush(stdscr, TRUE);
    keypad(stdscr, FALSE); nodelay(stdscr, FALSE); leaveok(stdscr, FALSE);
    endwin();
    curses_on = 0;
}

/****************************************************************************/

static int getgraycol(int x, int y)
{
    uae_u8 *bufpt;
    int xs, xl, ys, yl, c, cm;

    xl = x_map[x+1] - (xs = x_map[x]);
    yl = y_map[y+1] - (ys = y_map[y]);

    bufpt = ((uae_u8 *)gfxvidinfo.bufmem) + ys*currprefs.gfx_width + xs;

    cm = c = 0;
    for(y = 0; y < yl; y++, bufpt += currprefs.gfx_width)
	for(x = 0; x < xl; x++) {
	    c += bufpt[x];
	    ++cm;
	}
    if (cm)
	c /= cm;
    if (! currprefs.curses_reverse_video)
	c = maxc - c;
    return graychar[x2graymap[c]];
}

static int getcol(int x, int y)
{
    uae_u16 *bufpt;
    int xs, xl, ys, yl, c, cm;
    int bestcol = MYCOLOR_BLACK, bestccnt = 0;
    unsigned char colcnt [8];

    memset (colcnt, 0 , sizeof colcnt);

    xl = x_map[x+1] - (xs = x_map[x]);
    yl = y_map[y+1] - (ys = y_map[y]);

    bufpt = ((uae_u16 *)gfxvidinfo.bufmem) + ys*currprefs.gfx_width + xs;

    cm = c = 0;
    for(y = 0; y < yl; y++, bufpt += currprefs.gfx_width)
	for(x = 0; x < xl; x++) {
	    int v = bufpt[x];
	    int cnt;

	    c += v & 0xFF;
	    cnt = ++colcnt[v >> 8];
	    if (cnt > bestccnt) {
		bestccnt = cnt;
		bestcol = v >> 8;
	    }
	    ++cm;
	}
    if (cm)
	c /= cm;
    if (! currprefs.curses_reverse_video)
	c = maxc - c;
    return (graychar[x2graymap[c]] & ~A_COLOR) | COLOR_PAIR (mycolor2pair_map[bestcol]);
}

static void flush_line_txt(int y)
{
    int x;
    move (y,0);
    if (currprefs.color_mode < 2)
	for (x = 0; x < COLS; ++x) {
	    int c;

	    c = getgraycol(x,y);
	    addch(c);
	}
    else
	for (x = 0; x < COLS; ++x) {
	    int c;

	    c = getcol(x,y);
	    addch(c);
	}
}

__inline__ void flush_line(int y)
{
    if(y < 0 || y >= currprefs.gfx_height) {
/*       printf("flush_line out of window: %d\n", y); */
       return;
    }
    if(!curses_on)
	return;
    flush_line_txt(y_rev_map[y]);
}

void flush_block (int ystart, int ystop)
{
    int y;
    if(!curses_on)
	return;
    ystart = y_rev_map[ystart];
    ystop  = y_rev_map[ystop];
    for(y = ystart; y <= ystop; ++y)
	flush_line_txt(y);
}

void flush_screen (int ystart, int ystop)
{
    if(!debugging && !curses_on) {
	curses_init();
	flush_block(0, currprefs.gfx_height - 1);
    }
    refresh();
}

/****************************************************************************/

struct bstring *video_mode_menu = NULL;

void vidmode_menu_selected(int a)
{
}

int graphics_setup(void)
{
    return 1;
}

int graphics_init(void)
{
    int i;

    if (currprefs.color_mode > 16)
	write_log ("Bad color mode selected. Using default.\n"), currprefs.color_mode = 0;

    init_colors();

    curses_init();
    write_log("Using %s.\n",longname());

    if (debugging)
	curses_exit ();

    /* we have a 320x256x8 pseudo screen */

    currprefs.gfx_width = 320;
    currprefs.gfx_height = 256;
    currprefs.gfx_lores = 1;

    gfxvidinfo.width = currprefs.gfx_width;
    gfxvidinfo.height = currprefs.gfx_height;
    gfxvidinfo.maxblocklines = 1000;
    gfxvidinfo.pixbytes = currprefs.color_mode < 2 ? 1 : 2;
    gfxvidinfo.rowbytes = gfxvidinfo.pixbytes * currprefs.gfx_width;
    gfxvidinfo.bufmem = (char *)calloc(gfxvidinfo.rowbytes, currprefs.gfx_height+1);
    gfxvidinfo.linemem = 0;
    gfxvidinfo.emergmem = 0;
    gfxvidinfo.can_double = 0;
    switch (gfxvidinfo.pixbytes) {
     case 1:
	for (i = 0; i < 4096; i++)
	    xcolors[i] = xcolors[i] * 0x01010101;
	gfxvidinfo.can_double = 1;
	break;
     case 2:
	for (i = 0; i < 4096; i++)
	    xcolors[i] = xcolors[i] * 0x00010001;
	gfxvidinfo.can_double = 1;
	break;
    }
    if(!gfxvidinfo.bufmem) {
	write_log("Not enough memory.\n");
	return 0;
    }

    for (i = 0; i < sizeof x_map / sizeof *x_map; i++)
	x_map[i] = (i * currprefs.gfx_width) / COLS;
    for (i = 0; i < sizeof y_map / sizeof *y_map; i++)
	y_map[i] = (i * currprefs.gfx_height) / LINES;
    for (i = 0; i < sizeof y_map / sizeof *y_map - 1; i++) {
	int l1 = y_map[i];
	int l2 = y_map[i+1];
	int j;
	if (l2 >= sizeof y_rev_map / sizeof *y_rev_map)
	    break;
	for (j = l1; j < l2; j++)
	    y_rev_map[j] = i;
    }

    buttonstate[0] = buttonstate[1] = buttonstate[2] = 0;
    for(i=0; i<256; i++)
	keystate[i] = 0;

    lastmx = lastmy = 0;
    newmousecounters = 0;

    return 1;
}

/****************************************************************************/

void graphics_leave(void)
{
    curses_exit();
}

/****************************************************************************/

static int keycode2amiga(int ch)
{
    switch(ch) {
	case KEY_A1:    return AK_NP7;
	case KEY_UP:    return AK_NP8;
	case KEY_A3:    return AK_NP9;
	case KEY_LEFT:  return AK_NP4;
	case KEY_B2:    return AK_NP5;
	case KEY_RIGHT: return AK_NP6;
	case KEY_C1:    return AK_NP1;
	case KEY_DOWN:  return AK_NP2;
	case KEY_C3:    return AK_NP3;
	case KEY_ENTER: return AK_ENT;
	case 13:        return AK_RET;
	case ' ':       return AK_SPC;
	case 27:        return AK_ESC;
	default: return -1;
    }
}

/***************************************************************************/

void handle_events(void)
{
    int ch;
    int kc;

    /* Hack to simulate key release */
    for(kc = 0; kc < 256; ++kc) {
	if(keystate[kc]) if(!--keystate[kc]) record_key((kc << 1) | 1);
    }
    if(buttonstate[0]) --buttonstate[0];
    if(buttonstate[1]) --buttonstate[1];
    if(buttonstate[2]) --buttonstate[2];

    newmousecounters = 0;
    if(!curses_on) return;

    while((ch = getch())!=ERR) {
	if(ch == 12) {clearok(stdscr,TRUE);refresh();}
#ifdef NCURSES_MOUSE_VERSION
	if(ch == KEY_MOUSE) {
	    MEVENT ev;
	    if(getmouse(&ev) == OK) {
		lastmx = (ev.x*currprefs.gfx_width)/COLS;
		lastmy = (ev.y*currprefs.gfx_height)/LINES;
		if(ev.bstate & BUTTON1_PRESSED)  buttonstate[0] = keydelay;
		if(ev.bstate & BUTTON1_RELEASED) buttonstate[0] = 0;
		if(ev.bstate & BUTTON2_PRESSED)  buttonstate[1] = keydelay;
		if(ev.bstate & BUTTON2_RELEASED) buttonstate[1] = 0;
		if(ev.bstate & BUTTON3_PRESSED)  buttonstate[2] = keydelay;
		if(ev.bstate & BUTTON3_RELEASED) buttonstate[2] = 0;
	    }
	}
#endif
	if (ch == 6)  ++lastmx; /* ^F */
	if (ch == 2)  --lastmx; /* ^B */
	if (ch == 14) ++lastmy; /* ^N */
	if (ch == 16) --lastmy; /* ^P */
	if (ch == 11) {buttonstate[0] = keydelay;ch = 0;} /* ^K */
	if (ch == 25) {buttonstate[2] = keydelay;ch = 0;} /* ^Y */
	if (ch == 15) uae_reset (); /* ^O */
	if (ch == 17) uae_quit (); /* ^Q */
	if (ch == KEY_F(1)) {
	  curses_insert_disk();
	  ch = 0;
	}

	if(isupper(ch)) {
	    keystate[AK_LSH] =
	    keystate[AK_RSH] = keydelay;
	    record_key(AK_LSH << 1);
	    record_key(AK_RSH << 1);
	    kc = keycode2amiga(tolower(ch));
	    keystate[kc] = keydelay;
	    record_key(kc << 1);
	} else if((kc = keycode2amiga(ch)) >= 0) {
	    keystate[kc] = keydelay;
	    record_key(kc << 1);
	}
    }
    gui_handle_events();
}

/***************************************************************************/

void target_specific_usage(void)
{
    printf("----------------------------------------------------------------------------\n");
    printf("[n]curses specific usage:\n");
    printf("  -x : Display reverse video.\n");
    printf("By default uae will assume a black on white display. If yours\n");
    printf("is light on dark, use -x. In case of graphics garbage, ^L will\n");
    printf("redisplay the screen. ^K simulate left mouse button, ^Y RMB.\n");
    printf("If you are using a xterm UAE can use the mouse. Else use ^F ^B\n");
    printf("^P ^N to emulate mouse mouvements.\n");
    printf("----------------------------------------------------------------------------\n");
}

/***************************************************************************/

int check_prefs_changed_gfx (void)
{
    return 0;
}

int debuggable(void)
{
    return 1;
}

int needmousehack(void)
{
    return 1;
}

void LED(int on)
{
}

void write_log (const char *buf, ...)
{

}

int lockscr (void)
{
    return 1;
}

void unlockscr (void)
{
}

void target_save_options (FILE *f, struct uae_prefs *p)
{
    fprintf (f, "curses.reverse_video=%s\n", p->curses_reverse_video ? "true" : "false");
}

int target_parse_option (struct uae_prefs *p, char *option, char *value)
{
    return (cfgfile_yesno (option, value, "reverse_video", &p->curses_reverse_video));
}
