/* 
 * UAE - The Un*x Amiga Emulator
 *
 * Not a parser, but parallel and serial emulation for Win32
 *
 * Copyright 1997 Mathias Ortmann
 * Copyright 1998-1999 Brian King - added MIDI output support
 */

#include "config.h"
#include "sysconfig.h"
#include <windows.h>
#include <winspool.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef _MSC_VER
#include <mmsystem.h>
#include <ddraw.h>
#include <commctrl.h>
#include <commdlg.h>
#else
#include "winstuff.h"
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>

#include "sysdeps.h"
#include "options.h"
#include "gensound.h"
#include "events.h"
#include "uae.h"
#include "include/memory.h"
#include "custom.h"
#include "autoconf.h"
#include "od-win32/win32gui.h"
#include "od-win32/parser.h"
#include "od-win32/midi.h"
#include "od-win32/ahidsound.h"
#include "win32.h"
#include "ioport.h"

static UINT prttimer;
static char prtbuf[PRTBUFSIZE];
static int prtbufbytes,wantwrite;
static HANDLE hPrt = INVALID_HANDLE_VALUE;
static DWORD  dwJob;
extern HWND hAmigaWnd;
static int prtopen;
extern void flushpixels(void);
void DoSomeWeirdPrintingStuff( char val );
static int uartbreak;

static void flushprtbuf (void)
{
    DWORD written = 0;

    if (hPrt != INVALID_HANDLE_VALUE)
    {
	if( WritePrinter( hPrt, prtbuf, prtbufbytes, &written ) )
	{
	    if( written != prtbufbytes )
		write_log( "PRINTER: Only wrote %d of %d bytes!\n", written, prtbufbytes );
	}
	else
	{
	    write_log( "PRINTER: Couldn't write data!\n" );
	}
    }
    else
    {
	write_log( "PRINTER: Not open!\n" );
    }
    prtbufbytes = 0;
}

void finishjob (void)
{
    flushprtbuf ();
}
 
static void DoSomeWeirdPrintingStuff( char val )
{
	//if (prttimer)
	//KillTimer (hAmigaWnd, prttimer);
    if (prtbufbytes < PRTBUFSIZE) {
	prtbuf[prtbufbytes++] = val;
	//prttimer = SetTimer (hAmigaWnd, 1, 2000, NULL);
    } else {
	flushprtbuf ();
	*prtbuf = val;
	prtbufbytes = 1;
	prttimer = 0;
    }
}

int isprinter (void)
{
    if (!strcasecmp(currprefs.prtname,"none"))
	return 0;
    if (!memcmp(currprefs.prtname,"LPT", 3)) {
	paraport_open (currprefs.prtname);
	return -1;
    }
    return 1;
}

static void openprinter( void )
{
    DOC_INFO_1 DocInfo;

    closeprinter ();
    if (!strcasecmp(currprefs.prtname,"none"))
	return;
    if( ( hPrt == INVALID_HANDLE_VALUE ) && *currprefs.prtname)
    {
	if( OpenPrinter(currprefs.prtname, &hPrt, NULL ) )
	{
	    // Fill in the structure with info about this "document."
	    DocInfo.pDocName = "My Document";
	    DocInfo.pOutputFile = NULL;
	    DocInfo.pDatatype = "RAW";
	    // Inform the spooler the document is beginning.
	    if( (dwJob = StartDocPrinter( hPrt, 1, (LPSTR)&DocInfo )) == 0 )
	    {
		ClosePrinter( hPrt );
		hPrt = INVALID_HANDLE_VALUE;
	    }
	    else if( StartPagePrinter( hPrt ) )
	    {
		prtopen = 1;
	    }
	}
	else
	{
	    hPrt = INVALID_HANDLE_VALUE; // Stupid bug in Win32, where OpenPrinter fails, but hPrt ends up being zero
	}
    }
    if( hPrt != INVALID_HANDLE_VALUE )
    {
	write_log( "PRINTER: Opening printer \"%s\" with handle 0x%x.\n", currprefs.prtname, hPrt );
    }
    else if( *currprefs.prtname )
    {
	write_log( "PRINTER: ERROR - Couldn't open printer \"%s\" for output.\n", currprefs.prtname );
    }
}

void flushprinter (void)
{
    if (hPrt != INVALID_HANDLE_VALUE) {
        SetJob(
	    hPrt,  // handle to printer object
	    dwJob,      // print job identifier
	    0,      // information level
	    0,     // job information buffer
	    5     // job command value
	);
	closeprinter();
    }
}

void closeprinter( void )
{
    if( hPrt != INVALID_HANDLE_VALUE )
    {
	EndPagePrinter( hPrt );
	EndDocPrinter( hPrt );
	ClosePrinter( hPrt );
	hPrt = INVALID_HANDLE_VALUE;
	write_log( "PRINTER: Closing printer.\n" );
    }
    //KillTimer( hAmigaWnd, prttimer );
    prttimer = 0;
    prtopen = 0;
}

static void putprinter (char val)
{
    DoSomeWeirdPrintingStuff( val );
}

int doprinter (uae_u8 val)
{
    if (!prtopen)
	openprinter ();
    if (prtopen)
        putprinter (val);
    return prtopen;
}

static HANDLE hCom = INVALID_HANDLE_VALUE;
static DCB dcb;
static HANDLE writeevent;
#define SERIAL_WRITE_BUFFER 100
#define SERIAL_READ_BUFFER 100
static uae_u8 outputbuffer[SERIAL_WRITE_BUFFER];
static uae_u8 outputbufferout[SERIAL_WRITE_BUFFER];
static uae_u8 inputbuffer[SERIAL_READ_BUFFER];
static int datainoutput;
static int dataininput, dataininputcnt;
static OVERLAPPED writeol, readol;
static writepending;

int openser (char *sername)
{
    char buf[32];
    COMMTIMEOUTS CommTimeOuts;

    sprintf (buf, "\\.\\\\%s", sername);

   if (!(writeevent = CreateEvent (NULL, TRUE, FALSE, NULL))) {
	write_log ("SERIAL: Failed to create event!\n");
	return 0;
    }
    SetEvent (writeevent);
    writeol.hEvent = writeevent;
    uartbreak = 0;
    if ((hCom = CreateFile (buf, GENERIC_READ | GENERIC_WRITE,
			    0,
			    NULL,
			    OPEN_EXISTING,
			    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
			    NULL)) != INVALID_HANDLE_VALUE) {
	SetCommMask (hCom, EV_RXFLAG);
	SetupComm (hCom, 65536,128);
	PurgeComm (hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	CommTimeOuts.ReadIntervalTimeout = 0xFFFFFFFF;
	CommTimeOuts.ReadTotalTimeoutMultiplier = 0;
	CommTimeOuts.ReadTotalTimeoutConstant = 0;
	CommTimeOuts.WriteTotalTimeoutMultiplier = 0;
	CommTimeOuts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts (hCom, &CommTimeOuts);

	GetCommState (hCom, &dcb);

	dcb.BaudRate = 9600;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

        dcb.fDsrSensitivity = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
   
	if (currprefs.serial_hwctsrts) {
	    dcb.fOutxCtsFlow = TRUE;
	    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	} else {
	    dcb.fRtsControl = RTS_CONTROL_DISABLE;
	    dcb.fOutxCtsFlow = FALSE;
	}   

	dcb.fTXContinueOnXoff = FALSE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;

	dcb.fErrorChar = FALSE;
	dcb.fNull = FALSE;
	dcb.fAbortOnError = FALSE;
	
	dcb.XoffLim = 512;
	dcb.XonLim = 2048;

	if (SetCommState (hCom, &dcb)) {
	    write_log ("SERIAL: Using %s CTS/RTS=%d\n", sername, currprefs.serial_hwctsrts);
	    return 1;
	}
	write_log ("SERIAL: serial driver didn't accept new parameters\n");
	CloseHandle (hCom);
	hCom = INVALID_HANDLE_VALUE;
    }
    return 0;
}

void closeser (void)
{
    if (hCom != INVALID_HANDLE_VALUE)  {
	CloseHandle (hCom);
	hCom = INVALID_HANDLE_VALUE;
    }
    if (midi_ready)
        Midi_Close();
    if( writeevent )
	CloseHandle( writeevent );
    writeevent = 0;
    uartbreak = 0;
}

static void outser (void)
{
    DWORD actual;
    if (WaitForSingleObject (writeevent, 0) == WAIT_OBJECT_0 && datainoutput > 0) {
        memcpy (outputbufferout, outputbuffer, datainoutput);
        WriteFile (hCom, outputbufferout, datainoutput, &actual, &writeol);
        datainoutput = 0;
    }
}

void writeser (int c)
{
    if (midi_ready)
    {
	BYTE outchar = (BYTE)c;
        Midi_Parse( midi_output, &outchar );
    }
    else
    {
	if (!currprefs.use_serial)
	    return;
	if (datainoutput + 1 < sizeof(outputbuffer)) {
	    outputbuffer[datainoutput++] = c;
	} else {
	    write_log ("serial output buffer overflow, data will be lost\n");
	    datainoutput = 0;
	}
	outser ();
    }
}

int checkserwrite (void)
{
    if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
	return 1;
    if (midi_ready) {
	return 1;
    } else {
        outser ();
	if (datainoutput >= sizeof (outputbuffer) - 1)
	    return 0;
    }
    return 1;
}

int readseravail (void)
{
    COMSTAT ComStat;
    DWORD dwErrorFlags;
    if (midi_ready) {
	if (ismidibyte ())
	    return 1;
    } else {
	if (!currprefs.use_serial)
	    return 0;
	if (dataininput > dataininputcnt)
	    return 1;
	if (hCom != INVALID_HANDLE_VALUE)  {
	    ClearCommError (hCom, &dwErrorFlags, &ComStat);
	    if (ComStat.cbInQue > 0)
		return 1;
	}
    }
    return 0;
}

int readser (int *buffer)
{
    COMSTAT ComStat;
    DWORD dwErrorFlags;
    DWORD actual;
    
    
    if (midi_ready)
    {
	*buffer = getmidibyte ();
	if (*buffer < 0)
	    return 0;
	return 1;
    }
    else
    {
	if (!currprefs.use_serial)
	    return 0;
	if (dataininput > dataininputcnt) {
	    *buffer = inputbuffer[dataininputcnt++];
	    return 1;
	}
        dataininput = 0;
	dataininputcnt = 0;
	if (hCom != INVALID_HANDLE_VALUE) 
	{
	    /* only try to read number of bytes in queue */
	    ClearCommError (hCom, &dwErrorFlags, &ComStat);
	    if (ComStat.cbInQue) 
	    {
		int len = ComStat.cbInQue;
		
		if (len > sizeof (inputbuffer))
		    len = sizeof (inputbuffer);
		if (ReadFile (hCom, inputbuffer, len, &actual, &readol)) 
		{
		    dataininput = actual;
		    dataininputcnt = 0;
		    if (actual == 0)
			return 0;
		    return readser (buffer);
		}
	    }
	}
    }
    return 0;
}

void serialuartbreak (int v)
{
    if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
	return;

    if (v)
	EscapeCommFunction (hCom, SETBREAK);
    else
	EscapeCommFunction (hCom, CLRBREAK);
}

void getserstat (int *pstatus)
{
    DWORD stat;
    int status = 0;

    *pstatus = 0;
    if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
	return;
    
    GetCommModemStatus (hCom, &stat);
    if (stat & MS_CTS_ON)
        status |= TIOCM_CTS;
    if (stat & MS_RLSD_ON)
        status |= TIOCM_CAR;
    if (stat & MS_DSR_ON)
        status |= TIOCM_DSR;
    if (stat & MS_RING_ON)
        status |= TIOCM_RI;
    *pstatus = status;
}


void setserstat (int mask, int onoff)
{
    if (!currprefs.use_serial || hCom == INVALID_HANDLE_VALUE)
	return;

    if (mask & TIOCM_DTR)
        EscapeCommFunction (hCom, onoff ? SETDTR : CLRDTR);
    if (!currprefs.serial_hwctsrts) {
	if (mask & TIOCM_RTS)
	    EscapeCommFunction (hCom, onoff ? SETRTS : CLRRTS);
    }
}

int setbaud (long baud)
{
    if( baud == 31400 && currprefs.win32_midioutdev >= -1) /* MIDI baud-rate */
    {
        if (!midi_ready)
        {
            if (Midi_Open())
		write_log ("Midi enabled\n");
        }
        return 1;
    }
    else
    {
        if (midi_ready)
        {
            Midi_Close();
        }
        if (!currprefs.use_serial)
	    return 1;
	if (hCom != INVALID_HANDLE_VALUE) 
        {
	    if (GetCommState (hCom, &dcb)) 
            {
		dcb.BaudRate = baud;
	        if (!SetCommState (hCom, &dcb)) {
		    write_log ("SERIAL: Error setting baud rate %d!\n", baud);
		    return 0;
		}
	    } 
            else
            {
		write_log ("SERIAL: setbaud internal error!\n");
            }
        }
    }
    return 1;
}

void hsyncstuff(void)
//only generate Interrupts when 
//writebuffer is complete flushed
//check state of lwin rwin
{
    static int keycheck = 0;
    static int installahi;
    
#ifdef AHI
    { //begin ahi_sound
	static int count;
	if (ahi_on) {
	    count++;
	    //15625/count freebuffer check
	    if(count > 20) {
		ahi_updatesound (1);
		count = 0;
	    }
	}
	if (!installahi)
	{ 
	    uaecptr a = here (); //this install the ahisound
	    org (RTAREA_BASE + 0xFFC0);
	    calltrap (deftrap (ahi_demux));
	    dw (0x4e75);// rts
	    org (a);
	    installahi=1;
	}
    } //end ahi_sound
#endif
#ifdef PARALLEL_PORT
    keycheck++;
    if(keycheck==1000)
    {
	if (prtbufbytes)
	{
	    flushprtbuf ();
	} 
	{
	    extern flashscreen;
	    int DX_Fill( int , int , int, int, uae_u32 , enum RGBFTYPE  );
	    //extern int warned_JIT_0xF10000;
	    //warned_JIT_0xF10000 = 0;
	    if (flashscreen) {
		DX_Fill(0,0,300,40,0x000000,9);
		flashscreen--;
	    }
	}
	keycheck = 0;
    }
#endif
}
