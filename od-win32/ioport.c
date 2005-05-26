
#include "sysconfig.h"
#include "sysdeps.h"

#include <cia.h>

//#define IOPORT_EMU
#define io_log
#define para_log write_log

typedef int bool;

#include <windows.h>
#include "win32.h"

typedef bool (_stdcall* INITIALIZEWINIO)(void);
static INITIALIZEWINIO pInitializeWinIo;
typedef void (_stdcall* SHUTDOWNWINIO)(void);
static SHUTDOWNWINIO pShutdownWinIo;
typedef bool (_stdcall* GETPORTVAL)(WORD,PDWORD,BYTE);
static GETPORTVAL pGetPortVal;
typedef bool (_stdcall* SETPORTVAL)(WORD,DWORD,BYTE);
static SETPORTVAL pSetPortVal;
static HMODULE ioh;

#ifndef IOPORT_EMU
#include <WinIo.h>
#endif

static bool initialized;

int ioport_init (void)
{
    if (initialized)
	return 1;
#ifndef IOPORT_EMU
    ioh = WIN32_LoadLibrary ("winio.dll");
    if (!ioh)
	return 0;
    pInitializeWinIo = (INITIALIZEWINIO)GetProcAddress (ioh, "InitializeWinIo");
    pShutdownWinIo = (SHUTDOWNWINIO)GetProcAddress (ioh, "ShutdownWinIo");
    pGetPortVal = (GETPORTVAL)GetProcAddress (ioh, "GetPortVal");
    pSetPortVal = (SETPORTVAL)GetProcAddress (ioh, "SetPortVal");
    if (!pInitializeWinIo || !pShutdownWinIo || !pGetPortVal || !pSetPortVal) {
	io_log ("incompatible winio.dll\n");
	FreeLibrary (ioh);
	return 0;
    }
    __try {
        initialized = pInitializeWinIo();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        initialized = 0;
    }
#else
    initialized = 1;
#endif
    io_log ("io initialize returned %d\n", initialized);
    return initialized;
}

void ioport_free (void)
{
#ifndef IOPORT_EMU
    if (initialized) {
	pShutdownWinIo();
	FreeLibrary (ioh);
        io_log ("io freed\n");
    }
#endif
    initialized = 0;
}

uae_u8 ioport_read (int port)
{
    DWORD v = 0;
#ifndef IOPORT_EMU
    pGetPortVal (port, &v, 1);
#endif
    io_log ("ioport_read %04.4X returned %02.2X\n", port, v);
    return (uae_u8)v;
}

void ioport_write (int port, uae_u8 v)
{
#ifndef IOPORT_EMU
    pSetPortVal (port, v, 1);
#endif
    io_log ("ioport_write %04.4X %02.2X\n", port, v);
}


#ifndef PARALLEL_DIRECT

void paraport_free (void) { }
int paraport_init (void) { return 0; }
int paraport_open (char *port) { return 0; }
int parallel_direct_write_status (uae_u8 v, uae_u8 dir) { return 0; }
int parallel_direct_read_status (uae_u8 *vp) { return 0; }
int parallel_direct_write_data (uae_u8 v, uae_u8 dir) { return 0; }
int parallel_direct_read_data (uae_u8 *v) { return 0; }

#else

#include <ParaPort.h>

typedef BOOL (*closePort)(HANDLE);
typedef BOOL (*executeCycle)(HANDLE, PARAPORT_CYCLE*, int);
typedef BOOL (*getPortInfo)(HANDLE, PARAPORT_INFO*);
typedef HANDLE* (*openPort)(const char*);
static closePort pp_closeport;
static executeCycle pp_executecycle;
static getPortInfo pp_getportinfo;
static openPort pp_openport;
static HMODULE para;
static HANDLE pport;

void paraport_free (void)
{
    if (para) {
	if (pport)
	    pp_closeport (pport);
	pport = 0;	
	FreeLibrary (para);
	para = 0;
    }
}

int paraport_init (void)
{
    char tmp[10];
    int mask = 0, i;
    HANDLE pp;

    paraport_free ();
    para = WIN32_LoadLibrary("ParaPort.dll");
    if (!para) {
	write_log ("PARAPORT: no ParaPort.dll, direct parallel port emulation disabled\n");
	return 0;
    }
    pp_closeport = (closePort)GetProcAddress (para, "closePort");
    pp_executecycle = (executeCycle)GetProcAddress (para, "executeCycle");
    pp_getportinfo = (getPortInfo)GetProcAddress (para, "getPortInfo");
    pp_openport = (openPort)GetProcAddress (para, "openPort");
    if (!pp_openport || !pp_closeport || !pp_executecycle) {
	write_log ("PARAPORT: GetProcAddress() failed\n");
	paraport_free ();
    }
    write_log("PARAPORT:");
    for (i = 0; i < 4 ; i++) {
	sprintf (tmp, "LPT%d", i + 1);
	pp = pp_openport (tmp);
	if (pp != INVALID_HANDLE_VALUE) {
	    mask |= 1 << i;
	    pp_closeport (pp);
	    write_log(" %s", tmp);
	}
	pp = 0;
    }
    if (!mask)
	write_log ("no parallel ports detected");
    write_log("\n");
    return mask;
}

int paraport_open (char *port)
{
    static char oldport[10];

    if (!para)
	return 0;
    if (pport && !strcmp (port, oldport))
	return 1;
    pport = pp_openport(port);
    if (!pport) {
	write_log ("PARAPORT: couldn't open '%s'\n", port);
	paraport_free ();
	return 0;
    }
    strcpy (oldport, port);
    write_log("PARAPORT: port '%s' opened\n", port);
    return 1;
}


int parallel_direct_write_status (uae_u8 v, uae_u8 dir)
{
    PARAPORT_CYCLE c[2];
    int ok = 1;

    if (!pport)
	return 0;
    memset (c + 0, 0, sizeof (PARAPORT_CYCLE));
    c[0].MaskControl = PARAPORT_MASK_CONTROL_SELECTIN;
    if ((dir & 1)) {
	write_log ("PARAPORT: BUSY can't be output\n");
	ok = 0;
    }
    if ((dir & 2)) {
	write_log ("PARAPORT: POUT can't be output\n");
	ok = 0;
    }
    if ((dir & 4) && !(v & 4))
	c[0].Control |= PARAPORT_MASK_CONTROL_SELECTIN;
    if (!pp_executecycle (pport, c, 2)) {
	write_log ("PARAPORT: write executeCycle failed, CTL=%02.2X DIR=%02.2X\n", v & 7, dir & 7);
	return 0;
    }
    para_log ("PARAPORT: write CTL=%02.2X DIR=%02.2X\n", v & 7, dir & 7);
    return ok;
}

int parallel_direct_read_status (uae_u8 *vp)
{
    PARAPORT_CYCLE c[1];
    int ok = 1;
    uae_u8 v = 0;
    static int oldack;

    if (!pport)
	return 0;
    memset (c + 0, 0, sizeof (PARAPORT_CYCLE));
    c[0].MaskStatus = PARAPORT_MASK_STATUS;
    if (!pp_executecycle (pport, c, 1)) {
	write_log ("PARAPORT: CTL read executeCycle failed\n");
	return 0;
    }
    if (c[0].Status & PARAPORT_MASK_STATUS_SELECT)
	v |= 4;
    if (c[0].Status & PARAPORT_MASK_STATUS_PAPEREND)
	v |= 2;
    if (!(c[0].Status & PARAPORT_MASK_STATUS_BUSY))
	v |= 1;
    if (c[0].Status & PARAPORT_MASK_STATUS_ACKNOWLEDGE) {
	v |= 8;
	if (!oldack)
	    cia_parallelack ();
	oldack = 1;
    } else {
	oldack = 0;
    }
    para_log ("PARAPORT: read CTL=%02.2X\n", v);
    v &= 7;
    *vp &= ~7;
    *vp |= v;
    return ok;
}

int parallel_direct_write_data (uae_u8 v, uae_u8 dir)
{
    PARAPORT_CYCLE c[2];
    int ok = 1;

    if (!pport)
	return 0;
    if (dir != 0xff) {
	write_log ("PARAPORT: unsupported mixed i/o attempted, DATA=%02.2X DIR=%02.2X, ignored\n", v, dir);
	return 0;
    }
    memset (c + 0, 0, sizeof (PARAPORT_CYCLE));
    memset (c + 1, 0, sizeof (PARAPORT_CYCLE));
    c[0].Data = v;
    c[0].MaskData = 0xff;
    c[0].MaskControl = PARAPORT_MASK_CONTROL_STROBE;
    c[0].Control = PARAPORT_MASK_CONTROL_STROBE;
    c[0].RepeatFactor = 1;
    c[1].MaskControl = PARAPORT_MASK_CONTROL_STROBE;
    if (!pp_executecycle (pport, c, 2)) {
	write_log ("PARAPORT: write executeCycle failed, data=%02.2X\n", v);
	return 0;
    }
    para_log ("PARAPORT: write DATA=%02.2X\n", v);
    return 1;
}

int parallel_direct_read_data (uae_u8 *v)
{
    static uae_u8 olda, oldb;
    PARAPORT_CYCLE c[2];
    int ok = 1;

    if (!pport)
	return 0;
    memset (c + 0, 0, sizeof (PARAPORT_CYCLE));
    memset (c + 1, 0, sizeof (PARAPORT_CYCLE));
    c[0].MaskData = 0xff;
    c[0].MaskControl = PARAPORT_MASK_CONTROL_DIRECTION | PARAPORT_MASK_CONTROL_STROBE;
    c[0].Control = PARAPORT_MASK_CONTROL_DIRECTION | PARAPORT_MASK_CONTROL_STROBE;
    c[0].RepeatFactor = 1;
    c[1].MaskControl = PARAPORT_MASK_CONTROL_STROBE;
    c[1].MaskData = 0;
    if (!pp_executecycle (pport, c, 2)) {
	write_log ("PARAPORT: DATA read executeCycle failed\n");
	return 0;
    }
    *v = c[0].Data;
    para_log ("PARAPORT: read DATA=%02.2X\n", v);
    return ok;
}

#endif











