/* 
 * UAE - The Un*x Amiga Emulator
 *
 * Not a parser, but parallel and serial emulation for Win32
 *
 * Copyright 1997 Mathias Ortmann
 * Copyright 1998-1999 Brian King - added MIDI output support
 */

//#define PS_PRINT_DUMP

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
#include "parallel.h"
#include "zfile.h"
#include "threaddep/thread.h"

#include <Ghostscript/errors.h>
#include <Ghostscript/iapi.h>

static char prtbuf[PRTBUFSIZE];
static int prtbufbytes,wantwrite;
static HANDLE hPrt = INVALID_HANDLE_VALUE;
static DWORD  dwJob;
extern HWND hAmigaWnd;
static int prtopen;
extern void flushpixels(void);
void DoSomeWeirdPrintingStuff( char val );
static int uartbreak;
static int parflush;

static uae_thread_id prt_tid;
static volatile int prt_running;
static volatile int prt_started;
static smp_comm_pipe prt_requests;

#ifdef PS_PRINT_DUMP
static struct zfile *prtdump;
#endif

static int psmode = 0;
static HMODULE gsdll;
static gs_main_instance *gsinstance;
static int gs_exitcode;

typedef int (CALLBACK* GSAPI_REVISION)(gsapi_revision_t *pr, int len);
static GSAPI_REVISION ptr_gsapi_revision;
typedef int (CALLBACK* GSAPI_NEW_INSTANCE)(gs_main_instance **pinstance, void *caller_handle);
static GSAPI_NEW_INSTANCE ptr_gsapi_new_instance;
typedef void (CALLBACK* GSAPI_DELETE_INSTANCE)(gs_main_instance *instance);
static GSAPI_DELETE_INSTANCE ptr_gsapi_delete_instance;
typedef int (CALLBACK* GSAPI_SET_STDIO)(gs_main_instance *instance,
    int (GSDLLCALLPTR stdin_fn)(void *caller_handle, char *buf, int len),
    int (GSDLLCALLPTR stdout_fn)(void *caller_handle, const char *str, int len),
    int (GSDLLCALLPTR stderr_fn)(void *caller_handle, const char *str, int len));
static GSAPI_SET_STDIO ptr_gsapi_set_stdio;
typedef int (CALLBACK* GSAPI_INIT_WITH_ARGS)(gs_main_instance *instance, int argc, char **argv);
static GSAPI_INIT_WITH_ARGS ptr_gsapi_init_with_args;

typedef int (CALLBACK* GSAPI_EXIT)(gs_main_instance *instance);
static GSAPI_EXIT ptr_gsapi_exit;

typedef (CALLBACK* GSAPI_RUN_STRING_BEGIN)(gs_main_instance *instance, int user_errors, int *pexit_code);
static GSAPI_RUN_STRING_BEGIN ptr_gsapi_run_string_begin;
typedef (CALLBACK* GSAPI_RUN_STRING_CONTINUE)(gs_main_instance *instance, const char *str, unsigned int length, int user_errors, int *pexit_code);
static GSAPI_RUN_STRING_CONTINUE ptr_gsapi_run_string_continue;
typedef (CALLBACK* GSAPI_RUN_STRING_END)(gs_main_instance *instance, int user_errors, int *pexit_code);
static GSAPI_RUN_STRING_END ptr_gsapi_run_string_end;

static uae_u8 **psbuffer;
static int psbuffers;

static LONG WINAPI ExceptionFilter (struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
    return EXCEPTION_EXECUTE_HANDLER;
}

static void freepsbuffers (void)
{
    int i;
    for (i = 0; i < psbuffers; i++)
	free (psbuffer[i]);
    free (psbuffer);
    psbuffer = NULL;
    psbuffers = 0;
}

static int openprinter_ps (void)
{
    char *gsargv[] = {
        "-dNOPAUSE", "-dBATCH", "-dNOPAGEPROMPT", "-dNOPROMPT", "-dQUIET", "-dNoCancel", 
        "-sDEVICE=mswinpr2", NULL
    };
    int gsargc, gsargc2, i;
    char *tmpparms[100];
    char tmp[MAX_DPATH];

    if (ptr_gsapi_new_instance (&gsinstance, NULL) < 0)
        return 0;
    tmpparms[0] = "WinUAE";
    gsargc2 = cmdlineparser (currprefs.ghostscript_parameters, tmpparms + 1, 100 - 10) + 1;
    for (gsargc = 0; gsargv[gsargc]; gsargc++);
    for (i = 0; i < gsargc; i++)
	tmpparms[gsargc2++] = gsargv[i];
    sprintf (tmp, "-sOutputFile=%%printer%%%s", currprefs.prtname);
    tmpparms[gsargc2++] = tmp;
    __try {
	ptr_gsapi_init_with_args (gsinstance, gsargc2, tmpparms);
	ptr_gsapi_run_string_begin (gsinstance, 0, &gs_exitcode);
    } __except(ExceptionFilter(GetExceptionInformation(), GetExceptionCode())) {
	write_log("GS crashed\n");
	return 0;
    }
    psmode = 1;
    return 1;
}

static void *prt_thread (void *p)
{
    uae_u8 **buffers = p;
    int err, cnt, ok;

    ok = 1;
    prt_running++;
    prt_started = 1;
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    if (load_ghostscript ()) {
        if (openprinter_ps ()) {
            write_log ("PostScript printing emulation started..\n");
	    cnt = 0;
	    while (buffers[cnt]) {
		uae_u8 *p = buffers[cnt];
		err = ptr_gsapi_run_string_continue (gsinstance, p + 2, (p[0] << 8) | p[1], 0, &gs_exitcode);
		if (err != e_NeedInput && err <= e_Fatal) {
		    ptr_gsapi_exit (gsinstance);
		    write_log ("PostScript parsing failed.\n");
		    ok = 0;
		    break;
		}
		cnt++;
	    }
	    cnt = 0;
	    while (buffers[cnt]) {
		free (buffers[cnt]);
		cnt++;
	    }
	    free (buffers);
	    if (ok) {
    		write_log ("PostScript printing emulation finished..\n");
		ptr_gsapi_run_string_end (gsinstance, 0, &gs_exitcode);
	    }
	} else {
	    write_log ("gsdll32.dll failed to initialize\n");
	}
    } else {
    	write_log ("gsdll32.dll failed to load\n");
    }
    unload_ghostscript ();
    prt_running--;
    return 0;
}



static void flushprtbuf (void)
{
    DWORD written = 0;

    if (!prtbufbytes)
	return;

#ifdef PS_PRINT_DUMP
    if (prtdump)
	zfile_fwrite (prtbuf, prtbufbytes, 1, prtdump);
#endif
    if (currprefs.parallel_postscript_emulation) {
	if (psmode) {
	    uae_u8 *p;
	    psbuffer = realloc (psbuffer, (psbuffers + 2) * sizeof (uae_u8*));
	    p = malloc (prtbufbytes + 2);
	    p[0] = prtbufbytes >> 8;
	    p[1] = prtbufbytes;
	    memcpy (p + 2, prtbuf, prtbufbytes);
	    psbuffer[psbuffers++] = p;
	    psbuffer[psbuffers] = NULL;
	}
	prtbufbytes = 0;
	return;
    } else if (hPrt != INVALID_HANDLE_VALUE) {
	if (WritePrinter(hPrt, prtbuf, prtbufbytes, &written)) {
	    if (written != prtbufbytes)
		write_log("PRINTER: Only wrote %d of %d bytes!\n", written, prtbufbytes);
	} else {
	    write_log("PRINTER: Couldn't write data!\n");
	}
    } else {
	write_log("PRINTER: Not open!\n");
    }
    prtbufbytes = 0;
}

void finishjob (void)
{
    flushprtbuf ();
}

static void DoSomeWeirdPrintingStuff (char val)
{
    static char prev[4];
    static uae_u8 one = 1;
    static uae_u8 three = 3;

    memmove (prev, prev + 1, 3);
    prev[3] = val;
    if (currprefs.parallel_postscript_detection) {
	if (psmode && val == 4) {
	    flushprtbuf ();
	    *prtbuf = val;
	    prtbufbytes = 1;
	    flushprtbuf ();
	    write_log ("PostScript end detected..\n");
#ifdef PS_PRINT_DUMP
	    zfile_fclose (prtdump);
	    prtdump = NULL;
#endif
	    if (currprefs.parallel_postscript_emulation) {
		prt_started = 0;
		if (uae_start_thread (prt_thread, psbuffer, &prt_tid)) {
		    while (!prt_started)
			Sleep (5);
		    psbuffers = 0;
		    psbuffer = NULL;
		}
	    } else {
		closeprinter ();
	    }
	    freepsbuffers ();
	    return;
	} else if (!psmode && !stricmp (prev, "%!PS")) {
#ifdef PS_PRINT_DUMP
	    prtdump = zfile_fopen ("psdump.dat", "wb");
#endif
	    psmode = 1;
	    psbuffer = malloc (sizeof (uae_u8*));
	    psbuffer[0] = 0;
	    psbuffers = 0;
	    strcpy (prtbuf, "%!PS");
	    prtbufbytes = strlen (prtbuf);
	    flushprtbuf ();
	    write_log ("PostScript start detected..\n");
	    return;
	}
    }
    if (prtbufbytes < PRTBUFSIZE) {
	prtbuf[prtbufbytes++] = val;
    } else {
	flushprtbuf ();
	*prtbuf = val;
	prtbufbytes = 1;
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

int isprinteropen (void)
{
    if (prtopen)
	return 1;
    return 0;
}

int load_ghostscript (void)
{
    struct gsapi_revision_s r;
    char path[MAX_DPATH];

    if (gsdll)
	return 1;
    gsdll = WIN32_LoadLibrary ("gsdll32.dll");
    if (!gsdll) {
	if (GetEnvironmentVariable ("GS_DLL", path, sizeof (path)))
	    gsdll = LoadLibrary (path);
    }
    if (!gsdll) {
	HKEY key;
	if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, "SOFTWARE\\AFPL Ghostscript", 0, KEY_ALL_ACCESS, &key) == ERROR_SUCCESS) {
	    int idx = 0;
	    char tmp1[MAX_DPATH];
	    for (;;) {
		DWORD size1 = sizeof (tmp1);
		FILETIME ft;
	        if (RegEnumKeyEx (key, idx, tmp1, &size1, NULL, NULL, NULL, &ft) == ERROR_SUCCESS) {
		    HKEY key2;
		    if (RegOpenKeyEx (key, tmp1, 0, KEY_ALL_ACCESS, &key2) == ERROR_SUCCESS) {
		        DWORD type = REG_SZ;
			DWORD size = sizeof (path);
			if (RegQueryValueEx (key2, "GS_DLL", 0, &type, (LPBYTE)path, &size) == ERROR_SUCCESS)
			    gsdll = LoadLibrary (path);
			RegCloseKey (key2);
			if (gsdll)
			    break;
		    }
		}
		idx++;
	    }
	    RegCloseKey (key);
	}
    }
    if (!gsdll)
	return 0;
    ptr_gsapi_revision = (GSAPI_REVISION)GetProcAddress (gsdll, "gsapi_revision");
    if (!ptr_gsapi_revision) {
	unload_ghostscript ();
        return -1;
    }
    if (ptr_gsapi_revision(&r, sizeof(r))) {
	unload_ghostscript ();
	return -2;
    }
    ptr_gsapi_new_instance = (GSAPI_NEW_INSTANCE)GetProcAddress (gsdll, "gsapi_new_instance");
    ptr_gsapi_delete_instance = (GSAPI_DELETE_INSTANCE)GetProcAddress (gsdll, "gsapi_delete_instance");
    ptr_gsapi_set_stdio = (GSAPI_SET_STDIO)GetProcAddress (gsdll, "gsapi_set_stdio");
    ptr_gsapi_exit = (GSAPI_EXIT)GetProcAddress (gsdll, "gsapi_exit");
    ptr_gsapi_run_string_begin = (GSAPI_RUN_STRING_BEGIN)GetProcAddress (gsdll, "gsapi_run_string_begin");
    ptr_gsapi_run_string_continue = (GSAPI_RUN_STRING_CONTINUE)GetProcAddress (gsdll, "gsapi_run_string_continue");
    ptr_gsapi_run_string_end = (GSAPI_RUN_STRING_END)GetProcAddress (gsdll, "gsapi_run_string_end");
    ptr_gsapi_init_with_args = (GSAPI_INIT_WITH_ARGS)GetProcAddress (gsdll, "gsapi_init_with_args");
    if (!ptr_gsapi_new_instance || !ptr_gsapi_delete_instance || !ptr_gsapi_exit ||
	!ptr_gsapi_run_string_begin || !ptr_gsapi_run_string_continue || !ptr_gsapi_run_string_end ||
	!ptr_gsapi_init_with_args) {
	unload_ghostscript ();
	return -3;
    }
    write_log ("gsdll32.dll: %s rev %d initialized\n", r.product, r.revision);
    return 1;
}

void unload_ghostscript (void)
{
    if (gsinstance) {
        ptr_gsapi_exit (gsinstance);
        ptr_gsapi_delete_instance (gsinstance);
    }
    gsinstance = NULL;
    if (gsdll)
        FreeLibrary (gsdll);
    gsdll = NULL;
    psmode = 0;
}

void openprinter( void )
{
    DOC_INFO_1 DocInfo;
    static int first;
    
    closeprinter ();
    if (!strcasecmp(currprefs.prtname,"none"))
	return;

    if (currprefs.parallel_postscript_emulation) {
	prtopen = 1;
	return;
    } else if (hPrt == INVALID_HANDLE_VALUE) {
	flushprtbuf ();
	if (OpenPrinter(currprefs.prtname, &hPrt, NULL)) {
	    // Fill in the structure with info about this "document."
	    DocInfo.pDocName = "My Document";
	    DocInfo.pOutputFile = NULL;
	    DocInfo.pDatatype = "RAW";
	    // Inform the spooler the document is beginning.
	    if( (dwJob = StartDocPrinter( hPrt, 1, (LPSTR)&DocInfo )) == 0 ) {
		ClosePrinter( hPrt );
		hPrt = INVALID_HANDLE_VALUE;
	    } else if( StartPagePrinter( hPrt ) ) {
		prtopen = 1;
	    }
	} else {
	    hPrt = INVALID_HANDLE_VALUE; // Stupid bug in Win32, where OpenPrinter fails, but hPrt ends up being zero
	}
    }
    if( hPrt != INVALID_HANDLE_VALUE ) {
	write_log( "PRINTER: Opening printer \"%s\" with handle 0x%x.\n", currprefs.prtname, hPrt );
    } else if( *currprefs.prtname ) {
	write_log( "PRINTER: ERROR - Couldn't open printer \"%s\" for output.\n", currprefs.prtname );
    }
}

void flushprinter (void)
{
    closeprinter();
}

void closeprinter( void )
{
#ifdef PRINT_DUMP
    zfile_fclose (prtdump);
#endif
    parflush = 0;
    psmode = 0;
    if (hPrt != INVALID_HANDLE_VALUE) {
	EndPagePrinter (hPrt);
	EndDocPrinter (hPrt);
	ClosePrinter (hPrt);
	hPrt = INVALID_HANDLE_VALUE;
	write_log ("PRINTER: Closing printer.\n");
    }
    if (currprefs.parallel_postscript_emulation)
	prtopen = 1;
    else
	prtopen = 0;
    if (prt_running) {
	write_log ("waiting for printing to finish...\n");
	while (prt_running)
	    Sleep (10);
    }
    freepsbuffers ();
}

static void putprinter (char val)
{
    DoSomeWeirdPrintingStuff( val );
}

int doprinter (uae_u8 val)
{
    parflush = 0;
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
	    if(count > ahi_pollrate) {
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
        flushprtbuf ();
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
    if (currprefs.parallel_autoflush_time) {
        parflush++;
        if (parflush / ((currprefs.ntscmode ? MAXVPOS_NTSC : MAXVPOS_PAL) * MAXHPOS_PAL / maxhpos) >= currprefs.parallel_autoflush_time * 50) {
	    flushprinter ();
	    parflush = 0;
	}
    }
#endif
}
