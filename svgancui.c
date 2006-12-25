 /*
  * UAE - The Un*x Amiga Emulator
  *
  * ncurses frontend for a text-based user interface.
  *
  * Copyright 1996 Bernd Schmidt
  * If you find the routines in this file useful, you may use them in your
  * programs without restrictions. Essentially, it's in the public domain.
  *
  */


#include "sysconfig.h"
#include "sysdeps.h"

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif
#include <ctype.h>

#include "config.h"
#include "options.h"
#include "uae.h"
#include "tui.h"

#ifdef DONT_HAVE_ATTR_T
typedef int attr_t;
#endif

static WINDOW *currwin;

static WINDOW *winstack[10]; /* more than enough */
static int winnr = 0;

void tui_setup(void)
{
    int i;

    for (i = 0; i < 10; i++)
	winstack[i] = NULL;
    /* From the ncurses manpage... */
    initscr (); start_color (); cbreak(); noecho(); nonl (); intrflush (stdscr, FALSE); keypad (stdscr, TRUE);
    currwin = stdscr;
    if (has_colors ()) {
	init_pair (1, COLOR_WHITE, COLOR_BLUE);
	init_pair (2, COLOR_BLACK, COLOR_WHITE);
	wattron (currwin, COLOR_PAIR (1) | A_BOLD);
	wbkgd (currwin, ' ' | COLOR_PAIR (1));
    }

    winstack[0] = stdscr;
    winnr = 1;
}

int tui_lines (void)
{
    return LINES;
}

int tui_cols (void)
{
    return COLS;
}

void tui_shutdown (void)
{
    endwin ();
}

void tui_refresh (void)
{
    int w;
    for (w = 0; w < winnr; w++) {
	touchwin (winstack[w]);
	wnoutrefresh (winstack[w]);
    }
    doupdate ();
}

void tui_puts (const char *s)
{
    waddstr (currwin, s);
}

void tui_cursoff(void)
{
}

void tui_curson (void)
{
}

void tui_putc(char c)
{
    waddch (currwin, c);
}

void tui_cr (void)
{
    waddch (currwin, '\r');
}

char tui_getc(void)
{
    return getch ();
}

void tui_gotoxy (int x, int y)
{
    x--; y--;
    wmove (currwin, y, x);
}

void tui_selwin (int w)
{
    currwin = winstack[w];
}

void tui_clrwin (int w)
{
    werase (winstack[w]);
}

void tui_drawbox(int w)
{
    wborder (winstack[w], 0, 0, 0, 0, 0, 0, 0, 0);
}

void tui_hline (int x1, int y1, int x2)
{
    wmove (currwin, y1-1, x1-1);
    whline (currwin, 0, x2-x1+1);
}

int tui_dlog(int x1, int y1, int x2, int y2)
{
    x1--; y1--;
    winstack[winnr] = newwin (y2 - y1, x2 - x1, y1, x1);
    return winnr++;
}

void tui_dlogdie (int w)
{
    if (currwin == winstack[w])
	currwin = stdscr;
    delwin (winstack[w]);
    winstack[w] = NULL;
    while (winstack[winnr-1] == NULL)
	winnr--;

    for (w = 0; w < winnr; w++)
	redrawwin (winstack[w]), wrefresh (winstack[w]);
}

int tui_gets (char *buf, int x, int y, int n)
{
    int i = 0;
    for (;;) {
	int c, j;

	buf[i] = 0;
	wmove (currwin, y, x);
	for (j = 0; j < i; j++)
	    waddch (currwin, buf[j]);
	for (; j < n; j++)
	    waddch (currwin, ' ');

	wmove (currwin, y, x + i);
	wrefresh (currwin);

	c = getch ();

	wmove (currwin, y, x + i);
	if (c == 13)
	    return 1;
	else if (c == 27)
	    return 0;
	else if (i > 0 && c == KEY_BACKSPACE)
	    i--;
	else if (i + 1 < n && !iscntrl (c))
	    buf[i++] = c;
    }
}

int tui_wgets (char *buf, const char *title, int n)
{
    int l = strlen (title);
    int ww = (l > n ? l : n) + 2;
    int w = tui_dlog((tui_cols ()-ww)/2, tui_lines ()/2-1, (tui_cols ()+ww)/2, tui_lines ()/2+1);
    int result;

    tui_selwin (w); tui_drawbox(w);
    wmove (currwin, 0, (ww-l)/2);
    waddstr (currwin, title);
    result = tui_gets (buf, 1, 1, n);
    tui_dlogdie (w);
    return result;
}

int tui_menubrowse (struct bstring *menu, int xoff, int yoff, int selected, int height)
{
    int count = 0, maxsel = 0, maxw = 0;
    int i, j, w, s, yp, oldyp;
    chtype moresave[6][2];
    int xpos, ypos;

    const char *mtitle = NULL;

    for (i = 0; menu[i].val != -3; i++) {
	int tmp;
	if (menu[i].val == -4) {
	    if (maxsel < selected)
		selected--;
	    continue;
	}
	if (menu[i].val != 0) {
	    count++;
	    if (menu[i].val != -2)
		maxsel++;
	} else
	    mtitle = menu[i].data;
	if ((tmp = strlen (menu[i].data)) > maxw)
	    maxw = tmp;
    }
    if (height > count)
	height = count;
    maxw += 3;
    if (strlen (mtitle ? mtitle : "") + 8 > maxw)
	maxw = strlen (mtitle ? mtitle : "") + 8;
    if (xoff > 0)
	xpos = xoff;
    else
	xpos = tui_cols () + xoff - maxw - 1;
    if (yoff > 0)
	ypos = yoff;
    else
	ypos = tui_lines () + yoff - height - 2;
    w = tui_dlog(xpos, ypos, xpos+maxw, ypos+height+1);
    tui_selwin (w);
    tui_drawbox(w);
    if (mtitle != NULL) {
	mvwaddstr (currwin, 0, 1, mtitle);
    }
    for (i = 0; i < 6; i++) {
	moresave[i][0] = mvwinch (currwin, 0, maxw-6+i);
	moresave[i][1] = mvwinch (currwin, height+1, maxw-6+i);
    }
    s = yp = 0; oldyp = -1;
    for (;;) {
	int c;
	int s2;

	while (selected < yp)
	    yp--;
	while (selected >= yp + height)
	    yp++;
	if (yp == 0)
	    for (i = 0; i < 6; i++)
		mvwaddch (currwin, 0, maxw-6+i, moresave[i][0]);
	else
	    mvwaddstr (currwin, 0, maxw-6, "(more)");
	if (yp + height == count)
	    for (i = 0; i < 6; i++)
		mvwaddch (currwin, height+1, maxw-6+i, moresave[i][1]);
	else
	    mvwaddstr (currwin, height+1, maxw-6, "(more)");
	for (i = s2 = j = 0; i < count; i++, j++) {
	    int k, x;
	    attr_t a = s2 == selected ? A_STANDOUT : 0;
	    while (menu[j].val == 0 || menu[j].val == -4)
		j++;
	    if (i >= yp && i < yp+height) {
		mvwaddch (currwin, 1+i-yp, 1, ' ' | a);
		for (k = x = 0; menu[j].data[k]; k++, x++) {
		    int a2 = 0;
		    c = menu[j].data[k];
		    if (c == '_')
			c = menu[j].data[++k], a2 = A_UNDERLINE;
		    mvwaddch (currwin, 1+i-yp, 2+x, c | a | a2);
		}
		for (; x < maxw-2; x++) {
		    mvwaddch (currwin, 1+i-yp, 2+x, ' ' | a);
		}
	    }
	    if (menu[j].val != -2)
		s2++;
	}

	tui_refresh ();
	c = getch ();
	if (c == 27) {
	    tui_dlogdie (w);
	    return -1;
	} else if (c == KEY_ENTER || c == 13 || c == ' ') {
	    tui_dlogdie (w);
	    for (i = s2 = j = 0; s2 <= selected; j++) {
		if (menu[j].val == -4) {
		    i++; j++; continue;
		}
		while (menu[j].val == 0)
		    j++;
		if (s2 == selected)
		    return i;
		if (menu[j].val != -2)
		    s2++, i++;
	    }
	    abort();
	} else switch (c) {
	 case KEY_UP:
	    if (selected > 0)
		selected--;
	    break;
	 case KEY_DOWN:
	    if (selected + 1 < count)
		selected++;
	    break;
	 case KEY_PPAGE:
	    if (selected > height)
		selected -= height;
	    else
		selected = 0;
	    break;
	 case KEY_NPAGE:
	    if (selected + height < count)
		selected += height;
	    else
		selected = count-1;
	    break;
	 default:
	    for (j = i = 0; menu[i].val != -3; i++)
		if (menu[i].val == toupper (c)) {
		    tui_dlogdie (w);
		    return j;
		} else if (menu[i].val == -1 || menu[i].val == -4 || menu[i].val > 0) {
		    j++;
		}

	    break;
	}
    }
    return -1; /* Can't get here */
}

void tui_errorbox(const char *err)
{
    const char *hak = "Hit any key";
    int n = strlen (hak);
    int l = strlen (err);
    int ww = (l > n ? l : n) + 2;
    int w = tui_dlog((tui_cols ()-ww)/2, tui_lines ()/2-1, (tui_cols ()+ww)/2, tui_lines ()/2+1);
    tui_selwin (w); tui_drawbox(w);

    wmove (currwin, 0, (ww-6)/2);
    waddstr (currwin, "Error!");
    wmove (currwin, 1, (ww-l)/2);
    waddstr (currwin, err);
    wmove (currwin, 2, (ww-n)/2);
    waddstr (currwin, hak);

    wrefresh (currwin);
    for (;;) {
	int c = getch ();
	if (c == 13)
	    break;
    }
    tui_dlogdie (w);
}

static char *pattern;
static int maxlen;

static void put_filename (char *s, int x, int y, attr_t a)
{
    char buf[256];
    int i;

    tui_gotoxy (x,y);
    if (strcmp (s, ".") == 0)
	strcpy (buf, "(none)");
    else
	strcpy (buf, s);
    buf[maxlen] = 0;
    for (i = 0; i < strlen (buf); i++)
	waddch (currwin, buf[i] | a);
    for (; i < maxlen; i++)
	waddch (currwin, ' ' | a);
}

static char fsbuf[256];

static int selectfn (const struct dirent *de)
{
    int l1, l2;

/*    l1 = strlen (pattern + 1);*/
    l2 = strlen (de->d_name);

    if (l2 >= tui_cols ()-10) /* Restrict length of filenames so we won't mess up the display */
	return 0;

    /* No pattern matching for now. But we don't show hidden files. */
    if (strcmp (de->d_name, ".") != 0 && strcmp (de->d_name, "..") != 0
	&& de->d_name[0] == '.')
	return 0;
    if (l2 > maxlen)
	maxlen = l2;
    return 1;
}

static int my_alphasort (const void *a, const void *b)
{
    return strcmp ((*(struct dirent **) a)->d_name,
		   (*(struct dirent **) b)->d_name);
}

char *tui_filereq(char *s, char *oldfile, const char *title)
{
    char cwd[256];
    char *retval = fsbuf;
    char *tmp;
    int fin = 0;
    chtype moresave[6][2];

    /* Save wd */
    if (getcwd (cwd, 256) == NULL)
	return NULL;

    /* Change into directory of old file */
    strcpy (fsbuf, oldfile);
    tmp = strrchr (fsbuf, '/');
    if (tmp != NULL) {
	*tmp = 0;
	if (strlen (fsbuf) > 0)
	    chdir (fsbuf);
    }

    pattern = s;
    if (s[0] != '*')
	write_log ("Can't handle wildcard %s\n", s);
    if (s[1] != 0 && strchr (s+1, '*') != NULL)
	write_log ("Can't handle wildcard %s\n", s);
    for (;!fin;) {
	struct dirent **names;
	int i, w, n, l, yp, oldyp, s;

	maxlen = 0;
	n = scandir (".", &names, selectfn, my_alphasort);

	if (n <= 0)
	    return NULL;
	if (title != NULL && strlen (title) + 6 > maxlen)
	    maxlen = strlen (title) + 6;
	l = n;
	if (l > 15)
	    l = 15;
	yp = s = 0; oldyp = -1;
	w = tui_dlog (tui_cols () - maxlen - 8, 5, tui_cols () - 5, 5 + l + 1);
	tui_selwin (w); tui_drawbox (w);
	if (title)
	    mvwaddstr (currwin, 0, 2, title);
	for (i = 0; i < 6; i++) {
	    moresave[i][0] = mvwinch (currwin, 0, maxlen-3+i);
	    moresave[i][1] = mvwinch (currwin, l+1, maxlen-3+i);
	}
	for (;;) {
	    int c;
	    char tmp[256];
	    while (s < yp)
		yp--;
	    while (s >= yp + l)
		yp++;
	    if (oldyp != yp) {
		oldyp = yp;
		for (i = 0; i < l; i++) {
		    put_filename (names[i + yp]->d_name, 3, 2 + i, 0);
		}
	    }
	    put_filename (names[s]->d_name, 3, 2 + s - yp, A_STANDOUT);

	    if (yp == 0)
		for (i = 0; i < 6; i++)
		    mvwaddch (currwin, 0, maxlen-3+i, moresave[i][0]);
	    else
		mvwaddstr (currwin, 0, maxlen-3, "(more)");
	    if (yp + l == n)
		for (i = 0; i < 6; i++)
		    mvwaddch (currwin, l+1, maxlen-3+i, moresave[i][1]);
	    else
		mvwaddstr (currwin, l+1, maxlen-3, "(more)");

	    tui_refresh ();
	    c = getch ();
	    put_filename (names[s]->d_name, 3, 2 + s - yp, 0);
	    if (c == 27) {
		retval = NULL; fin = 1;
		break;
	    } else if (c == KEY_ENTER || c == 13 || c == ' ') {
		int err;

		if (strcmp (names[s]->d_name, ".") == 0) {
		    fin = 1;
		    strcpy (fsbuf, "");
		    break;
		}
		err = chdir (names[s]->d_name);

		if (err == 0)
		    break;
		else if (errno == ENOTDIR) {
		    fin = 1;
		    if (getcwd (fsbuf, 256) == NULL)
			retval = NULL;
		    if (strlen (fsbuf) + strlen (names[s]->d_name) + 2 >= 256)
			retval = NULL;
		    else {
			strcat(fsbuf, "/");
			strcat(fsbuf, names[s]->d_name);
		    }
		    break;
		} /* else what? */
	    }
	    switch (c) {
	     case KEY_UP:
		if (s > 0)
		    s--;
		break;
	     case KEY_DOWN:
		if (s + 1 < n)
		    s++;
		break;
	     case KEY_PPAGE:
		if (s > l)
		    s -= l;
		else
		    s = 0;
		break;
	     case KEY_NPAGE:
		if (s + l < n)
		    s += l;
		else
		    s = n - 1;
		break;
	     default:
		i = 0;
		if (names[s]->d_name[0] == c)
		    i = s+1;
		for (; i < n*2; i++) {
		    int j = i;
		    if (i >= n)
			j -= n;
		    if (names[j]->d_name[0] == c) {
			s = j;
			break;
		    }
		}
	    }
	}
#if 0
	/* @@@ is this right? */
	for (i = 0; i < n; i++)
	    free (names[i]);
	free (names);
#endif
	tui_dlogdie (w);
    }
    chdir (cwd);
    return retval;
}

int tui_backup_optionsfile (void)
{
    char tmp[257];
    strcpy (tmp, optionsfile);
    strcat (tmp, "~");
    return rename (optionsfile, tmp);
}
