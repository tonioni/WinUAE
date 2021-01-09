
#include "sysconfig.h"
#include "sysdeps.h"

#include <cia.h>

//#define IOPORT_EMU
#define io_log
#define para_log write_log

#ifndef __cplusplus
typedef int bool;
#endif

#include <windows.h>
#include "win32.h"

// TVicPORT
typedef BOOL (_stdcall* OPENTVICPORT)(void);
static OPENTVICPORT pOpenTVicPort;
typedef void (_stdcall* CLOSETVICPORT)(void);
static CLOSETVICPORT pCloseTVicPort;
typedef BOOL (_stdcall* ISDRIVEROPENED)(void);
static ISDRIVEROPENED pIsDriverOpened;
typedef UCHAR (_stdcall* READPORT)(USHORT);
static READPORT pReadPort;
typedef UCHAR (_stdcall* WRITEPORT)(USHORT,UCHAR);
static WRITEPORT pWritePort;

// WINIO
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
#include <TVicPort.h>
#include <WinIo.h>
#endif

static int initialized;

int ioport_init (void)
{
	if (initialized)
		return initialized > 0 ? 1 : 0;

#ifndef IOPORT_EMU
	ioh = WIN32_LoadLibrary (_T("tvicport.dll"));
	if (ioh) {
		for (;;) {
			pOpenTVicPort = (OPENTVICPORT)GetProcAddress (ioh, "OpenTVicPort");
			pCloseTVicPort = (CLOSETVICPORT)GetProcAddress (ioh, "CloseTVicPort");
			pIsDriverOpened = (ISDRIVEROPENED)GetProcAddress (ioh, "IsDriverOpened");
			pReadPort = (READPORT)GetProcAddress (ioh, "ReadPort");
			pWritePort = (WRITEPORT)GetProcAddress (ioh, "WritePort");
			if (!pOpenTVicPort || !pCloseTVicPort || !pIsDriverOpened || !pReadPort || !pWritePort) {
				write_log (_T("IO: incompatible tvicport.dll\n"));
				break;
			}
			if (!pOpenTVicPort()) {
				write_log (_T("IO: tvicport.dll failed to initialize\n"));
				break;
			}
			if (!pIsDriverOpened()) {
				write_log (_T("IO: tvicport.dll failed to initialize!\n"));
				pCloseTVicPort();
				break;
			}
			initialized = 1;
			write_log (_T("IO: tvicport.dll initialized\n"));
			return 1;
		}
	}
	FreeLibrary(ioh);
	ioh = WIN32_LoadLibrary (_T("winio.dll"));
	if (ioh) {
		for (;;) {
			pInitializeWinIo = (INITIALIZEWINIO)GetProcAddress (ioh, "InitializeWinIo");
			pShutdownWinIo = (SHUTDOWNWINIO)GetProcAddress (ioh, "ShutdownWinIo");
			pGetPortVal = (GETPORTVAL)GetProcAddress (ioh, "GetPortVal");
			pSetPortVal = (SETPORTVAL)GetProcAddress (ioh, "SetPortVal");
			if (!pInitializeWinIo || !pShutdownWinIo || !pGetPortVal || !pSetPortVal) {
				write_log (_T("IO: incompatible winio.dll\n"));
				break;
			}
			__try {
				initialized = pInitializeWinIo() ? 2 : 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				write_log (_T("IO: winio.dll initialization failed\n"));
			}
			if (!initialized)
				break;
			write_log (_T("IO: winio.dll initialized\n"));
			return 1;
		}
	}
	FreeLibrary(ioh);
	initialized = -1;
	write_log (_T("IO: tvicport.dll or winio.dll failed to initialize\n"));
	return 0;
#else
	initialized = 1;
	return 1;
#endif
}

void ioport_free (void)
{
#ifndef IOPORT_EMU
	if (initialized == 1)
		pCloseTVicPort();
	if (initialized == 2)
		pShutdownWinIo();
	if (initialized)
		FreeLibrary (ioh);
#endif
	initialized = 0;
}

uae_u8 ioport_read (int port)
{
	DWORD v = 0;
#ifndef IOPORT_EMU
	if (initialized == 1)
		v = pReadPort (port);
	else if (initialized == 2)
		pGetPortVal (port, &v, 1);
#endif
	io_log ("ioport_read %04X returned %02X\n", port, v);
	return (uae_u8)v;
}

void ioport_write (int port, uae_u8 v)
{
#ifndef IOPORT_EMU
	if (initialized == 1)
		pWritePort (port, v);
	else if (initialized == 2)
		pSetPortVal (port, v, 1);
#endif
	io_log ("ioport_write %04X %02X\n", port, v);
}


#ifndef PARALLEL_DIRECT

void paraport_free (void) { }
int paraport_init (void) { return 0; }
int paraport_open (TCHAR *port) { return 0; }
int parallel_direct_write_status (uae_u8 v, uae_u8 dir) { return 0; }
int parallel_direct_read_status (uae_u8 *vp) { return 0; }
int parallel_direct_write_data (uae_u8 v, uae_u8 dir) { return 0; }
int parallel_direct_read_data (uae_u8 *v) { return 0; }

#else

#include <paraport/ParaPort.h>

typedef BOOL (_stdcall* closePort)(HANDLE);
typedef BOOL (_stdcall* executeCycle)(HANDLE, PARAPORT_CYCLE*, int);
typedef BOOL (_stdcall* getPortInfo)(HANDLE, PARAPORT_INFO*);
typedef HANDLE* (_stdcall* openPort)(const char*);
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
	int mask = 0, i;
	HANDLE pp;

	paraport_free ();
	para = WIN32_LoadLibrary(_T("ParaPort.dll"));
	if (!para) {
		write_log (_T("PARAPORT: no ParaPort.dll, direct parallel port emulation disabled\n"));
		return 0;
	}
	pp_closeport = (closePort)GetProcAddress (para, "closePort");
	pp_executecycle = (executeCycle)GetProcAddress (para, "executeCycle");
	pp_getportinfo = (getPortInfo)GetProcAddress (para, "getPortInfo");
	pp_openport = (openPort)GetProcAddress (para, "openPort");
	if (!pp_openport || !pp_closeport || !pp_executecycle) {
		write_log (_T("PARAPORT: GetProcAddress() failed\n"));
		paraport_free ();
		return 0;
	}
	write_log (_T("PARAPORT:"));
	for (i = 0; i < 4 ; i++) {
		char tmp[10];
		sprintf (tmp, "LPT%d", i + 1);
		pp = pp_openport (tmp);
		if (pp != INVALID_HANDLE_VALUE) {
			mask |= 1 << i;
			pp_closeport (pp);
		}
		pp = 0;
	}
	if (!mask)
		write_log (_T("no parallel ports detected"));
	write_log (_T("\n"));
	return mask;
}

int paraport_open (TCHAR *port)
{
	static TCHAR oldport[10];
	PARAPORT_CYCLE c[1];
	char *port2;

	if (!para)
		return 0;
	if (pport && !_tcscmp (port, oldport))
		return 1;
	port2 = ua (port);
	pport = pp_openport (port2);
	xfree (port2);
	if (!pport) {
		write_log (_T("PARAPORT: couldn't open '%s'\n"), port);
		paraport_free ();
		return 0;
	}
	_tcscpy (oldport, port);
	write_log (_T("PARAPORT: port '%s' opened\n"), port);
	memset (c, 0, sizeof (PARAPORT_CYCLE));
	c[0].MaskControl = PARAPORT_MASK_CONTROL | PARAPORT_MASK_CONTROL_DIRECTION;
	c[0].Control = PARAPORT_MASK_CONTROL_INIT | PARAPORT_MASK_CONTROL_DIRECTION;
	if (!pp_executecycle (pport, c, 1)) {
		write_log (_T("PARAPORT: init executeCycle failed\n"));
	}
	return 1;
}


int parallel_direct_write_status (uae_u8 v, uae_u8 dir)
{
	PARAPORT_CYCLE c[1];
	int ok = 1;

	if (!pport)
		return 0;
	memset (c, 0, sizeof (PARAPORT_CYCLE));
	c[0].MaskControl = PARAPORT_MASK_CONTROL_SELECTIN;
	if ((dir & 1)) {
		write_log (_T("PARAPORT: BUSY can't be output\n"));
		ok = 0;
	}
	if ((dir & 2)) {
		write_log (_T("PARAPORT: POUT can't be output\n"));
		ok = 0;
	}
	if ((dir & 4) && !(v & 4))
		c[0].Control |= PARAPORT_MASK_CONTROL_SELECTIN;
	if (!pp_executecycle (pport, c, 1)) {
		write_log (_T("PARAPORT: write executeCycle failed, CTL=%02X DIR=%02X\n"), v & 7, dir & 7);
		return 0;
	}
	para_log (_T("PARAPORT: write CTL=%02X DIR=%02X\n"), v & 7, dir & 7);
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
		write_log (_T("PARAPORT: CTL read executeCycle failed\n"));
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
	para_log (_T("PARAPORT: read CTL=%02X\n"), v);
	v &= 7;
	*vp &= ~7;
	*vp |= v;
	return ok;
}

int parallel_direct_write_data (uae_u8 v, uae_u8 dir)
{
	PARAPORT_CYCLE c[3];
	int ok = 1;

	if (!pport)
		return 0;
	if (dir != 0xff) {
		write_log (_T("PARAPORT: unsupported mixed i/o attempted, DATA=%02X DIR=%02X, ignored\n"), v, dir);
		return 0;
	}
	memset (c, 0, 3 * sizeof (PARAPORT_CYCLE));

	c[0].Data = v;
	c[0].MaskData = 0xff;
	c[0].MaskControl = PARAPORT_MASK_CONTROL_STROBE;

	c[1].MaskControl = PARAPORT_MASK_CONTROL_STROBE;
	c[1].Control = PARAPORT_MASK_CONTROL_STROBE;

	c[2].MaskControl = PARAPORT_MASK_CONTROL_STROBE;

	if (!pp_executecycle (pport, c, 3)) {
		write_log (_T("PARAPORT: write executeCycle failed, data=%02X\n"), v);
		return 0;
	}
	para_log (_T("PARAPORT: write DATA=%02X\n"), v);
	return 1;
}

int parallel_direct_read_data (uae_u8 *v)
{
	static uae_u8 olda, oldb;
	PARAPORT_CYCLE c[3];
	int ok = 1;

	if (!pport)
		return 0;
	memset (c, 0, 3 * sizeof (PARAPORT_CYCLE));

	c[0].MaskData = 0xff;
	c[0].MaskControl = PARAPORT_MASK_CONTROL_DIRECTION | PARAPORT_MASK_CONTROL_STROBE;
	c[0].Control = PARAPORT_MASK_CONTROL_DIRECTION;

	c[1].MaskControl = PARAPORT_MASK_CONTROL_STROBE;
	c[1].Control = PARAPORT_MASK_CONTROL_STROBE;

	c[2].MaskControl = PARAPORT_MASK_CONTROL_STROBE;

	if (!pp_executecycle (pport, c, 3)) {
		write_log (_T("PARAPORT: DATA read executeCycle failed\n"));
		return 0;
	}
	*v = c[0].Data;
	para_log (_T("PARAPORT: read DATA=%02X\n"), *v);
	return ok;
}

#endif











