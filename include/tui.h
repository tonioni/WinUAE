 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Text-based user interface
  * Sie haben es sich verdient!
  *
  * Copyright 1996 Tim Gunn, Bernd Schmidt
  */

extern void tui_setup(void);
extern void tui_shutdown(void);
extern void tui_refresh(void);

extern void tui_curson(void);
extern void tui_cursoff(void);

extern void tui_cr(void);
extern void tui_gotoxy(int, int);
extern void tui_puts(const char *);
extern void tui_putc(char);
extern char tui_getc(void);
extern int tui_gets(char *, int, int, int);
extern int tui_wgets(char *, const char *, int);
extern void tui_clrwin(int);
extern void tui_selwin(int);

extern int tui_dlog(int, int, int, int);
extern int tui_menubrowse(struct bstring *, int, int, int, int);

extern void tui_dlogdie(int);
extern char *tui_filereq(char *, char *, const char *);
extern void tui_drawbox(int);
extern void tui_hline(int, int, int);
extern void tui_errorbox(const char *);

extern int tui_cols(void);
extern int tui_lines(void);

extern int tui_backup_optionsfile(void);
