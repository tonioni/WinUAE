/* 
 * UAE - The Un*x Amiga Emulator
 *
 * Not a parser, but parallel and serial emulation for Win32
 *
 * Copyright 1997 Mathias Ortmann
 * Copyright 1998-1999 Brian King
 */

#define PRTBUFSIZE 4096

int setbaud (long baud );
void getserstat(int *status);
void setserstat (int mask, int onoff);
int readser (int *buffer);
int readseravail (void);
void writeser (int c);
int openser (char *sername);
void closeser (void);
void doserout (void);
void closeprinter (void);
void flushprinter (void);
int checkserwrite (void);
void serialuartbreak (int);

#define TIOCM_CAR 1
#define TIOCM_DSR 2
#define TIOCM_RI 4
#define TIOCM_DTR 8
#define TIOCM_RTS 16
#define TIOCM_CTS 32

extern void unload_ghostscript (void);
extern int load_ghostscript (void);
