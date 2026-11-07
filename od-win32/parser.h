/*
 * UAE - The Un*x Amiga Emulator
 *
 * Not a parser, but parallel and serial emulation for Win32
 *
 * Copyright 1997 Mathias Ortmann
 * Copyright 1998-1999 Brian King
 */

#define PRTBUFSIZE 65536

#include "serial_host.h"

void doserout(void);
void closeprinter(void);
void flushprinter(void);

void hsyncstuff(void);

void shmem_serial_delete(void);
bool shmem_serial_create(void);
int shmem_serial_state(void);

#define TIOCM_CAR SERIAL_HOST_CAR
#define TIOCM_DSR SERIAL_HOST_DSR
#define TIOCM_RI SERIAL_HOST_RI
#define TIOCM_DTR SERIAL_HOST_DTR
#define TIOCM_RTS SERIAL_HOST_RTS
#define TIOCM_CTS SERIAL_HOST_CTS

extern void unload_ghostscript (void);
extern int load_ghostscript (void);

#define MAX_MIDI_PORTS 100
struct midiportinfo
{
	TCHAR *name;
    TCHAR *label;
    int mid, pid;
    int devid;
};
extern struct midiportinfo *midiinportinfo[MAX_MIDI_PORTS];
extern struct midiportinfo *midioutportinfo[MAX_MIDI_PORTS];

#define MAX_SERPAR_PORTS 100
struct serparportinfo
{
    TCHAR *dev;
    TCHAR *cfgname;
    TCHAR *name;
};
extern struct serparportinfo *comports[MAX_SERPAR_PORTS];
extern struct serparportinfo *parports[MAX_SERPAR_PORTS];

extern int enumserialports (void);
extern int enummidiports (void);
extern void sernametodev (TCHAR*);
extern void serdevtoname (TCHAR*);

extern void epson_printchar (uae_u8 c);
extern int epson_init (const TCHAR *printername, int pins);
extern void epson_close (void);

#define PARALLEL_MATRIX_TEXT 1
#define PARALLEL_MATRIX_EPSON 2
#define PARALLEL_MATRIX_EPSON9 2
#define PARALLEL_MATRIX_EPSON24 3
#define PARALLEL_MATRIX_EPSON48 4
