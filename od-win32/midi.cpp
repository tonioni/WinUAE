/*
* MODULE:      midi.c
*
* DESCRIPTION: MIDI-handling code for MIDI-I/O.  Parses the MIDI-output stream and
*   sends system-exclusive MIDI-messages out using midiOutLongMsg(), while using
*   midiOutShortMsg() for normal MIDI-messages.  MIDI-input is passed byte-for-byte
*   up to the Amiga's serial-port.
*
* AUTHOR:      Brian_King@CodePoet.com
*
* COPYRIGHT:   Copyright 1999, 2000 under GNU Public License
*
* HISTORY:
*   1999.09.05  1.0    Brian King      - creation
*   2000.01.30  1.1    Brian King      - addition of midi-input
*   2002.05.xx  1.2    Bernd Roesch    - sysex in/MTC/Song Position pointer add
*/

#include "sysconfig.h"
#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>

#include "sysdeps.h"

#include "resource.h"

#include "options.h"
#include "parser.h"
#include "midi.h"
#include "registry.h"
#include "win32gui.h"

//#define MIDI_TRACING_ENABLED

#ifdef MIDI_TRACING_ENABLED
#define TRACE(x) do { write_log x; } while(0)
#else
#define TRACE(x)
#endif
#define MIDI_INBUFFERS 100  //use 13 MB Buffer with this settings
//on my system it work ok with 10 but who
//know when windows rest for a while
//with sysex size of 40 win can 8 sec sleep
#define	INBUFFLEN 120000 //if this is not enough a warning come
int midi_inbuflen = INBUFFLEN;

static int overflow,only_one_time;
BOOL midi_ready = FALSE;
BOOL midi_in_ready = FALSE;
extern int serdev;

static HMIDIIN inHandle;
static MIDIHDR midiin[MIDI_INBUFFERS];

static uae_u8 *inbuffer[MIDI_INBUFFERS] = { 0, 0} ;
static long inbufferlength[MIDI_INBUFFERS] = { 0,0};

static int in_allocated = 0;

/* MIDI OUTPUT */

static MidiOutStatus out_status;
static HMIDIOUT outHandle;
static MIDIHDR midiout[MIDI_BUFFERS];

static uae_u8 *outbuffer[MIDI_BUFFERS] = { 0, 0 };
static long outbufferlength[MIDI_BUFFERS] = { 0, 0 };
static int outbufferselect = 0;
static int out_allocated = 0;
static volatile int exitin = 0;
static CRITICAL_SECTION cs_proc;

/*
* FUNCTION:   getmidiouterr
*
* PURPOSE:    Wrapper for midiOutGetErrorText()
*
* PARAMETERS:
*   err       Midi-out error number
*
* RETURNS:
*   MidiOutErrorMsg char-ptr
*
* NOTES:      none
*
* HISTORY:
*   1999.09.06  1.0    Brian King             - Creation
*
*/
static TCHAR *getmidiouterr(TCHAR *txt, int err)
{
	midiOutGetErrorText(err, txt, MAX_DPATH);
	return txt;
}

static void MidiSetVolume(HMIDIOUT oh)
{
	TCHAR err[MAX_DPATH];
	MMRESULT verr;
	DWORD vol = 0xffffffff;
	if (currprefs.sound_volume_midi > 0) {
		uae_u16 v = (uae_u16)(65535.0 - (65535.0 * currprefs.sound_volume_midi / 100.0));
		vol = (v << 16) | v;
	}
	verr = midiOutSetVolume (oh, vol);
	if (verr) {
		write_log(_T("MIDI OUT: midiOutSetVolume error %s / %d\n"), getmidiouterr(err, verr), verr);
	}
}

/*
* FUNCTION:   MidiOut_Alloc
*
* PURPOSE:    Allocate the double-buffering needed for processing midi-out messages
*
* PARAMETERS: none
*
* RETURNS:
*   allocated - the number of buffers allocated
*
* NOTES:      none
*
* HISTORY:
*   1999.09.06  1.0    Brian King             - Creation
*
*/
static int MidiOut_Alloc(void)
{
	int i;

	if(!out_allocated) {
		for(i = 0; i < MIDI_BUFFERS; i++) {
			if(!outbuffer[i]) {
				outbuffer[i] = xmalloc(uae_u8, BUFFLEN);
				if(outbuffer[i]) {
					outbufferlength[i] = BUFFLEN;
					out_allocated++;
				} else {
					outbufferlength[i] = 0;
				}
			}
		}
		outbufferselect = 0;
	} else {
		write_log (_T("MIDI: ERROR - MidiOutAlloc() called twice?\n"));
	}
	return out_allocated;
}

/*
* FUNCTION:   MidiOut_Free
*
* PURPOSE:    Free the double-buffering needed for processing midi-out messages
*
* PARAMETERS: none
*
* RETURNS:    none
*
* NOTES:      none
*
* HISTORY:
*   1999.09.06  1.0    Brian King             - Creation
*
*/
static void MidiOut_Free(void)
{
	int i;

	for(i = 0; i < out_allocated; i++) {
		if(outbuffer[i])  {
			xfree(outbuffer[i]);
			outbufferlength[i] = 0;
			outbuffer[i] = NULL;
		}
	}
	outbufferselect = 0;
	out_allocated = 0;
}

/*
* FUNCTION:   MidiOut_PrepareHeader
*
* PURPOSE:    Wrapper for midiOutPrepareHeader
*
* PARAMETERS:
*   midihdr   - MIDIHDR-ptr which we will prepare
*   data      - byte-ptr to data which MIDIHDR will contain
*   length    - length of data
*
* RETURNS:
*   result    - 1 for success, 0 otherwise
*
* NOTES:      none
*
* HISTORY:
*   1999.08.02  1.0    Brian King             - Creation
*
*/
static int MidiOut_PrepareHeader(LPMIDIHDR out, LPSTR data, DWORD length)
{
	int result = 1;
	TCHAR err[MAX_DPATH];

	out->lpData = data;
	out->dwBufferLength = length;
	out->dwBytesRecorded = length;
	out->dwUser = 0;
	out->dwFlags = 0;

	if((result = midiOutPrepareHeader(outHandle, out, sizeof( MIDIHDR)))) {
		write_log (_T("MIDI: error %s / %d\n"), getmidiouterr (err, result), result);
		result = 0;
	}
	return result;
}

/* MIDI INPUT */


/*
* FUNCTION:   getmidiinerr
*
* PURPOSE:    Wrapper for midiInGetErrorText()
*
* PARAMETERS:
*   err       Midi-in error number
*
* RETURNS:
*   MidiInErrorMsg char-ptr
*
* NOTES:      none
*
* HISTORY:
*   1999.09.06  1.0    Brian King             - Creation
*
*/
static TCHAR *getmidiinerr(TCHAR *txt, int err)
{
	midiInGetErrorText(err, txt, MAX_DPATH);
	return txt;
}

/*
* FUNCTION:   MidiIn_Alloc
*
* PURPOSE:    Allocate the double-buffering needed for processing midi-out messages
*
* PARAMETERS: none
*
* RETURNS:
*   allocated - the number of buffers allocated
*
* NOTES:      none
*
* HISTORY:
*   1999.09.06  1.0    Brian King             - Creation
*
*/
static int MidiIn_Alloc(void)
{
	int i;

	if(!in_allocated)  {
		for(i = 0; i < MIDI_INBUFFERS; i++)  {
			if(!inbuffer[i]) {
				inbuffer[i] = xmalloc(uae_u8, midi_inbuflen);
				if(inbuffer[i]) {
					inbufferlength[i] = midi_inbuflen;
					in_allocated++;
				} else {
					inbufferlength[i] = 0;
				}
			}
		}
	} else {
		write_log (_T("MIDI: ERROR - MidiInAlloc() called twice?\n"));
	}
	return in_allocated;
}

/*
* FUNCTION:   MidiIn_Free
*
* PURPOSE:    Free the double-buffering needed for processing midi-in messages
*
* PARAMETERS: none
*
* RETURNS:    none
*
* NOTES:      none
*
* HISTORY:
*   1999.09.06  1.0    Brian King             - Creation
*
*/
static void MidiIn_Free( void )
{
	int i;

	for( i = 0; i < in_allocated; i++ ) {
		if(inbuffer[i]) {
			//in_allocated--;
			free(inbuffer[i]);
			inbufferlength[i] = 0;
			inbuffer[i] = NULL;
		}
	}
	in_allocated = 0;
	only_one_time = 0;
}

static const uae_u8 plen[128] = {
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	1,1,2,1,0,0,0,0,0,0,0,0,0,0,0,0
};

#define DBGOUT_MIDI_BYTE(x) TRACE((_T("MIDI: 0x%02x\n"), (x) ))

/*
* FUNCTION:   Midi_Parse
*
* PURPOSE:    Parse the MIDI data-stream based on the direction.
*
* PARAMETERS:
*   direction - Either midi_input or midi_output
*   data      - byte of MIDI data
*
* RETURNS:    none
*
* NOTES:      Written with much assistance from Todor Fay of Blue Ribbon
*             (Bars'n'Pipes) fame.
*
* HISTORY:
*   1999.08.02  1.0    Brian King             - Creation
*
*/
int Midi_Parse(midi_direction_e direction, BYTE *dataptr)
{
	int result = 0;
	static unsigned short bufferindex;
	static uae_u8 *bufferpoint = 0;

	if(direction == midi_output) {
		BYTE data = *dataptr;
		DBGOUT_MIDI_BYTE(data);
		if(data >= 0x80) {
			if(data >= MIDI_CLOCK) {
				switch(data)
				{
				case MIDI_CLOCK:
					TRACE((_T("MIDI: MIDI_CLOCK\n") ));
					break;
				case MIDI_START:
					TRACE((_T("MIDI: MIDI_START\n") ));
					break;
				case MIDI_CONTINUE:
					TRACE((_T("MIDI: MIDI_CONTINUE\n") ));
					break;
				case MIDI_STOP:
					TRACE((_T("MIDI: MIDI_STOP\n") ));
					break;
				default:
					break;
				}
			} else if(out_status.sysex) {
				if(out_allocated) {
					bufferpoint[bufferindex++] = (char)MIDI_EOX;
					if(bufferindex >= BUFFLEN)
						bufferindex = BUFFLEN - 1;
					out_status.status = MIDI_SYSX;
					// Flush this buffer using midiOutLongMsg
					MidiOut_PrepareHeader(&midiout[outbufferselect], (LPSTR)bufferpoint, bufferindex);
					midiOutLongMsg(outHandle, &midiout[outbufferselect], sizeof(MIDIHDR));
					outbufferselect = !outbufferselect;
					bufferpoint = outbuffer[outbufferselect];
					midiOutUnprepareHeader(outHandle, &midiout[outbufferselect], sizeof(MIDIHDR));
				}
				out_status.sysex = 0; // turn off MIDI_SYSX mode
				out_status.unknown = 1; // now in an unknown state
				if(data == MIDI_EOX)
					return 0;
			}
			out_status.status = data;
			out_status.length = plen[data & 0x7F];
			out_status.posn = 0;
			out_status.unknown = 0;
			if(data == MIDI_SYSX) {
				out_status.sysex = 1; // turn on MIDI_SYSX mode
				if(out_allocated) {
					bufferindex = 0;
					bufferpoint = outbuffer[outbufferselect];
					bufferpoint[bufferindex++] = (char)MIDI_SYSX;
				}
				return 0;
			}
		} else if(out_status.sysex) {
			if(out_allocated) {
				bufferpoint[bufferindex++] = data;
				if(bufferindex >= BUFFLEN)
					bufferindex = BUFFLEN - 1;
			}
			return 0;
		} else if(out_status.unknown) {
			return 0;
		} else if(++out_status.posn == 1) {
			out_status.byte1 = data;
		} else {
			out_status.byte2 = data;
		}
		if(out_status.posn >= out_status.length) {
			DWORD shortMsg;
			out_status.posn = 0;
			// flush the packet using midiOutShortMessage
			shortMsg = MAKELONG(MAKEWORD(out_status.status, out_status.byte1), MAKEWORD(out_status.byte2, 0));
			midiOutShortMsg(outHandle, shortMsg);
		}
	} else { // handle input-data

	}
	return result;
}

static void putmidibytes(uae_u8 *data, int len)
{
	if (!currprefs.win32_midirouter || !midi_ready)
		return;
	for (int i = 0; i < len; i++) {
		BYTE b = data[i];
		Midi_Parse(midi_output, &b);
	}
}

/*
* FUNCTION:   MidiIn support and Callback function
*
* PURPOSE:    Translate Midi in messages to raw serial data

* NOTES:      Sysex in not supported

*/

static uae_u8 midibuf[BUFFLEN];
static long midi_inptr = 0, midi_inlast = 0;

static void add1byte(DWORD_PTR w) //put 1 Byte to Midibuffer
{
	if(midi_inlast >= BUFFLEN - 10) {
		TRACE((_T("add1byte buffer full %d %d (%02X)\n"), midi_inlast, midi_inptr, w));
		return;
	}
	midibuf[midi_inlast] = (uae_u8)w;
	putmidibytes (&midibuf[midi_inlast], 1);
	midi_inlast++;
}
static void add2byte(DWORD_PTR w) //put 2 Byte to Midibuffer
{
	if(midi_inlast >= BUFFLEN - 10) {
		TRACE((_T("add2byte buffer full %d %d (%04X)\n"), midi_inlast, midi_inptr, w));
		return;
	}
	midibuf[midi_inlast+0] = (uae_u8)w;
	w = w >> 8;
	midibuf[midi_inlast+1] = (uae_u8)w;
	putmidibytes (&midibuf[midi_inlast], 2);
	midi_inlast+=2;
}
static void add3byte(DWORD_PTR w) //put 3 Byte to Midibuffer
{
	if(midi_inlast >= BUFFLEN - 10) {
		TRACE((_T("add3byte buffer full %d %d (%08X)\n"), midi_inlast, midi_inptr, w));
		return;
	}
	midibuf[midi_inlast+0] = (uae_u8)w;
	w = w >> 8;
	midibuf[midi_inlast+1] = (uae_u8)w;
	w = w >> 8;
	midibuf[midi_inlast+2] = (uae_u8)w;
	putmidibytes (&midibuf[midi_inlast], 3);
	midi_inlast+=3;
}

int ismidibyte(void)
{
	if (midi_inptr < midi_inlast)
		return 1;
	return 0;
}

LONG getmidibyte(void) //return midibyte or -1 if none
{
	int i;
	LONG rv;

	EnterCriticalSection (&cs_proc);
	if (overflow == 1) {
		TCHAR szMessage[MAX_DPATH];
		WIN32GUI_LoadUIString(IDS_MIDIOVERFLOW, szMessage, MAX_DPATH);
		gui_message(szMessage);
		overflow = 0;
	}
	TRACE((_T("getmidibyte(%02X)\n"), midibuf[midi_inptr]));
	if (midibuf[midi_inptr] >= 0xf0) { // only check for free buffers if status sysex
		for (i = 0;i < MIDI_INBUFFERS;i++) {
			if (midiin[i].dwFlags == (MHDR_DONE|MHDR_PREPARED)) {
				// add a buffer if one is free
				LeaveCriticalSection(&cs_proc);
				midiInAddBuffer(inHandle, &midiin[i], sizeof(MIDIHDR));
				EnterCriticalSection(&cs_proc);
			}
		}
	}
	rv = -1;
	if (midi_inptr < midi_inlast)
		rv = midibuf[midi_inptr++];
	if (midi_inptr >= midi_inlast)
		midi_inptr = midi_inlast = 0;
	LeaveCriticalSection(&cs_proc);
	return rv;
}

static void CALLBACK MidiInProc(HMIDIIN hMidiIn,UINT wMsg,DWORD_PTR dwInstance,DWORD_PTR dwParam1,DWORD_PTR dwParam2)
{
	EnterCriticalSection (&cs_proc);
	if(wMsg == MIM_ERROR) {
		TRACE((_T("MIDI Data Lost\n")));
	}
	if(wMsg == MIM_LONGDATA) {
		LPMIDIHDR midiin = (LPMIDIHDR)dwParam1;
		TRACE((_T("MIM_LONGDATA bytes=%d ts=%u\n"), midiin->dwBytesRecorded, dwParam2));
		if (exitin == 1)
			goto end; //for safeness midi want close
		if ((midi_inlast + midiin->dwBytesRecorded) >= (BUFFLEN-6)) {
			overflow = 1;
			TRACE((_T("MIDI overflow1\n")));
			//for safeness if buffer too full (should not occur)
			goto end;
		}
		if (midiin->dwBufferLength == midiin->dwBytesRecorded) {
			//for safeness if buffer too full (should not occur)
			overflow = 1;
			TRACE((_T("MIDI overflow2\n")));
			goto end;
		}
		memcpy(&midibuf[midi_inlast], midiin->lpData, midiin->dwBytesRecorded);
		putmidibytes(&midibuf[midi_inlast], midiin->dwBytesRecorded);
		midi_inlast = midi_inlast + midiin->dwBytesRecorded;
	}

	if(wMsg == MM_MIM_DATA || wMsg == MM_MIM_MOREDATA) {
		BYTE state = (BYTE)dwParam1;
		TRACE((_T("%s %08X\n"), wMsg == MM_MIM_DATA ? "MM_MIM_DATA" : "MM_MIM_MOREDATA", dwParam1));
		if(state == 254)
			goto end;
		if(state < 0xf0)
			state = state & 0xf0;
		switch (state)
		{
		case 0x80: //Note OFF
			add3byte(dwParam1);
			break;
		case 0x90: // Note On
			add3byte(dwParam1);
			break;
		case 0xa0: // Poly Press
			add3byte(dwParam1);
			break;
		case 0xb0: //CTRL Change
			add3byte(dwParam1);
			break;
		case 0xc0:  //ProgramChange
			add2byte(dwParam1);
			break;
		case 0xd0:   //ChanPress
			add2byte(dwParam1);
			break;
		case 0xe0:   //PitchBend
			add3byte(dwParam1);
			break;
			//System Common Messages
		case 0xf1:   //QuarterFrame-message ... MIDI_Time_Code
			add2byte(dwParam1);
			break;
		case 0xf2:   //Song Position
			add3byte(dwParam1);
			break;
		case 0xf3:   //Song Select
			add2byte(dwParam1);
			break;
		case 0xf6:   //Tune Request
			add3byte(dwParam1);
			break;
			//System Real Time Messages
		case 0xf8:   //MIDI-Clock
			add1byte((char)dwParam1);
			break;
		case 0xfa:   //Start
			add1byte((char)dwParam1);
			break;
		case 0xfb:   //Continue
			add1byte((char)dwParam1);
			break;
		case 0xfc:   //Stop
			add1byte((char)dwParam1);
			break;
		case 0xfe:   //Active Sense (selden used)
			add1byte((char)dwParam1);
			break;
		case 0xff:   //Reset (selden used)
			add2byte(dwParam1);
			break;
		}
	}
end:
	LeaveCriticalSection(&cs_proc);
}
/*
* FUNCTION:   Midi_Open
*
* PURPOSE:    Open the desired MIDI handles (output, and possibly input)
*
* PARAMETERS: none
*
* RETURNS:
*   result    - 1 for success, 0 for failure
*
* NOTES:      none
*
* HISTORY:
*   1999.08.02  1.0    Brian King             - Creation
*
*/
int Midi_Open(void)
{
	unsigned long result = 0, i;
	TCHAR err[MAX_DPATH];

	if (currprefs.win32_midioutdev < -1)
		return 0;
	if((result = midiOutOpen(&outHandle, currprefs.win32_midioutdev, 0, 0,CALLBACK_NULL))) {
		write_log (_T("MIDI OUT: error %s / %d while opening port %d\n"), getmidiouterr(err, result), result, currprefs.win32_midioutdev);
		result = 0;
	} else {
		InitializeCriticalSection(&cs_proc);
		MidiSetVolume(outHandle);
		// We don't need input for output...
		if((currprefs.win32_midiindev >= 0) &&
			(result = midiInOpen(&inHandle, currprefs.win32_midiindev, (DWORD_PTR)MidiInProc, 0, CALLBACK_FUNCTION|MIDI_IO_STATUS))) {
				write_log (_T("MIDI IN: error %s / %d while opening port %d\n"), getmidiinerr(err, result), result, currprefs.win32_midiindev);
		} else {
			midi_in_ready = TRUE;
			result=midiInStart(inHandle);
		}

		if(MidiOut_Alloc()) {
			if(midi_in_ready) {
				if(!MidiIn_Alloc()) {
					midiInClose(inHandle);
					midi_in_ready = FALSE;
				} else {
					for (i = 0;i < MIDI_INBUFFERS; i++) {
						midiin[i].lpData = (LPSTR)inbuffer[i];
						midiin[i].dwBufferLength = midi_inbuflen - 1;
						midiin[i].dwBytesRecorded = midi_inbuflen - 1;
						midiin[i].dwUser = 0;
						midiin[i].dwFlags = 0;
						result=midiInPrepareHeader(inHandle, &midiin[i], sizeof(MIDIHDR));
						result=midiInAddBuffer(inHandle, &midiin[i], sizeof(MIDIHDR));
					}
				}
			}
			midi_ready = TRUE;
			result = 1;
			serdev = 1;
		} else {
			midiOutClose(outHandle);
			if(midi_in_ready) {
				midiInClose(inHandle);
				midi_in_ready = FALSE;
			}
			result = 0;
			DeleteCriticalSection(&cs_proc);
		}
	}
	return result;
}

/*
* FUNCTION:   Midi_Close
*
* PURPOSE:    Close all opened MIDI handles
*
* PARAMETERS: none
*
* RETURNS:    none
*
* NOTES:      none
*
* HISTORY:
*   1999.08.02  1.0    Brian King             - Creation
*
*/
void Midi_Close(void)
{
	int i;
	if(midi_ready) {
		midiOutReset(outHandle);
		for(i = 0; i < MIDI_BUFFERS; i++) {
			while(MIDIERR_STILLPLAYING == midiOutUnprepareHeader(outHandle, &midiout[i], sizeof(MIDIHDR))) {
				Sleep(10);
			}
		}
		MidiOut_Free();
		midiOutClose(outHandle);

		if(midi_in_ready) {
			exitin = 1; //for safeness sure no callback come now
			midiInReset(inHandle);
			for (i = 0; i < MIDI_INBUFFERS; i++) {
				midiInUnprepareHeader(inHandle, &midiin[i], sizeof(MIDIHDR));
			}
			MidiIn_Free();
			midiInClose(inHandle);
			midi_in_ready = FALSE;
			exitin = 0;
		}
		midi_ready = FALSE;
		write_log (_T("MIDI: closed.\n"));
		DeleteCriticalSection(&cs_proc);
	}
}
