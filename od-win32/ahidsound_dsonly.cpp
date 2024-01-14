/*
* UAE - The Un*x Amiga Emulator
*
* Win32 interface
*
* Copyright 1997 Mathias Ortmann
* Copyright 1997-2001 Brian King
* Copyright 2000-2002 Bernd Roesch
*/

#define NATIVBUFFNUM 4
#define RECORDBUFFER 50 //survive 9 sec of blocking at 44100

#include "sysconfig.h"
#include "sysdeps.h"

#if defined(AHI)

#include <ctype.h>
#include <assert.h>

#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dsound.h>
#include <stdio.h>

#include "options.h"
#include "audio.h"
#include "memory.h"
#include "events.h"
#include "custom.h"
#include "newcpu.h"
#include "traps.h"
#include "sounddep/sound.h"
#include "render.h"
#include "win32.h"
#include "parser.h"
#include "enforcer.h"
#include "ahidsound.h"
#include "picasso96_win.h"

static long samples, playchannel, intcount;
static int record_enabled;
int ahi_on;
static uae_u8 *sndptrmax;
static uae_u8 soundneutral;

static LPSTR lpData,sndptrout;
extern uae_u32 chipmem_mask;
static uae_u8 *ahisndbuffer, *sndrecbuffer;
static int ahisndbufsize, *ahisndbufpt, ahitweak;;
int ahi_pollrate = 40;

int sound_freq_ahi, sound_channels_ahi, sound_bits_ahi;

static int vin, devicenum;
static int amigablksize;

static DWORD sound_flushes2 = 0;

static LPDIRECTSOUND lpDS2 = NULL;
static LPDIRECTSOUNDBUFFER lpDSBprimary2 = NULL;
static LPDIRECTSOUNDBUFFER lpDSB2 = NULL;
static LPDIRECTSOUNDNOTIFY lpDSBN2 = NULL;

// for record
static LPDIRECTSOUNDCAPTURE lpDS2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSBprimary2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSB2r = NULL;

struct winuae	//this struct is put in a6 if you call
	//execute native function
{
	HWND amigawnd;    //address of amiga Window Windows Handle
	unsigned int changenum;   //number to detect screen close/open
	unsigned int z3offset;    //the offset to add to acsess Z3 mem from Dll side
};
static struct winuae uaevar;
static struct winuae *a6;

#if defined(X86_MSVC_ASSEMBLY)

#define CREATE_NATIVE_FUNC_PTR2 uae_u32 (*native_func)(uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32,\
	uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32,uae_u32,uae_u32)
#define SET_NATIVE_FUNC2(x) native_func = (uae_u32 (*)(uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32,uae_u32,uae_u32))(x)
#define CALL_NATIVE_FUNC2(d1,d2,d3,d4,d5,d6,d7,a1,a2,a3,a4,a5,a6,a7) if(native_func) return native_func(d1,d2,d3,d4,d5,d6,d7,a1,a2,a3,a4,a5,a6,a7,regs_)

static uae_u32 REGPARAM2 emulib_ExecuteNativeCode2 (TrapContext *context)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	unsigned int espstore;
	uae_u8* object_UAM = (uae_u8*) m68k_areg (regs, 0);
	uae_u32 d1 = m68k_dreg (regs, 1);
	uae_u32 d2 = m68k_dreg (regs, 2);
	uae_u32 d3 = m68k_dreg (regs, 3);
	uae_u32 d4 = m68k_dreg (regs, 4);
	uae_u32 d5 = m68k_dreg (regs, 5);
	uae_u32 d6 = m68k_dreg (regs, 6);
	uae_u32 d7 = m68k_dreg (regs, 7);
	uae_u32 a1 = m68k_areg (regs, 1);
	uae_u32 a2 = m68k_areg (regs, 2);
	uae_u32 a3 = m68k_areg (regs, 3);
	uae_u32 a4 = m68k_areg (regs, 4);
	uae_u32 a5 = m68k_areg (regs, 5);
	uae_u32 a7 = m68k_areg (regs, 7);
	uae_u32 regs_ = (uae_u32)&regs;
	CREATE_NATIVE_FUNC_PTR2;
	uaevar.z3offset = (uae_u32)(get_real_address (z3fastmem_bank[0].start) - z3fastmem_bank[0].start);
	uaevar.amigawnd = mon->hAmigaWnd;
	a6 = &uaevar;
	if (object_UAM)  {
		SET_NATIVE_FUNC2 (object_UAM);
		__asm
		{   mov espstore,esp
			push regs_
			push a7
			push a6
			push a5
			push a4
			push a3
			push a2
			push a1
			push d7
			push d6
			push d5
			push d4
			push d3
			push d2
			push d1
			call native_func
			mov esp,espstore
		}
		//CALL_NATIVE_FUNC2( d1, d2,d3, d4, d5, d6, d7, a1, a2, a3, a4 , a5 , a6 , a7);
	} else {
		return 0;
	}
}

#endif

void ahi_close_sound (void)
{
	HRESULT hr = DS_OK;

	if (!ahi_on)
		return;
	ahi_on = 0;
	record_enabled = 0;
	ahisndbufpt = (int*)ahisndbuffer;

	if (lpDSB2) {
		hr = IDirectSoundBuffer_Stop (lpDSB2);
		if(FAILED (hr))
			write_log (_T("AHI: SoundStop() failure: %s\n"), DXError (hr));
	} else {
		write_log (_T("AHI: Sound Stopped...\n"));
	}

	if (lpDSB2)
		IDirectSoundBuffer_Release (lpDSB2);
	lpDSB2 = NULL;
	if (lpDSBprimary2)
		IDirectSoundBuffer_Release (lpDSBprimary2);
	lpDSBprimary2 = NULL;
	if (lpDS2)
		IDirectSound_Release (lpDS2);
	lpDS2 = NULL;

	if (lpDSB2r)
		IDirectSoundCaptureBuffer_Release (lpDSB2r);
	lpDSB2r = NULL;
	if (lpDS2r)
		IDirectSound_Release (lpDS2r);
	lpDS2r = NULL;
	if (ahisndbuffer)
		free (ahisndbuffer);
	ahisndbuffer = NULL;
}

void ahi_updatesound(int force)
{
	HRESULT hr;
	DWORD pos;
	DWORD dwBytes1, dwBytes2;
	LPVOID dwData1, dwData2;
	static int oldpos;

	if (sound_flushes2 == 1) {
		oldpos = 0;
		intcount = 1;
		INTREQ (0x8000 | 0x2000);
		hr = lpDSB2->Play (0, 0, DSBPLAY_LOOPING);
		if(hr == DSERR_BUFFERLOST) {
			lpDSB2->Restore ();
			hr = lpDSB2->Play (0, 0, DSBPLAY_LOOPING);
		}
	}

	hr = lpDSB2->GetCurrentPosition (&pos, 0);
	if (hr != DSERR_BUFFERLOST) {
		pos -= ahitweak;
		if (pos < 0)
			pos += ahisndbufsize;
		if (pos >= ahisndbufsize)
			pos -= ahisndbufsize;
		pos = (pos / (amigablksize * 4)) * (amigablksize * 4);
		if (force == 1) {
			if (oldpos != pos) {
				intcount = 1;
				INTREQ (0x8000 | 0x2000);
				return; //to generate amiga ints every amigablksize
			} else {
				return;
			}
		}
	}

	hr = lpDSB2->Lock (oldpos, amigablksize * 4, &dwData1, &dwBytes1, &dwData2, &dwBytes2, 0);
	if(hr == DSERR_BUFFERLOST) {
		write_log (_T("AHI: lostbuf %d %x\n"), pos, amigablksize);
		IDirectSoundBuffer_Restore (lpDSB2);
		hr = lpDSB2->Lock (oldpos, amigablksize * 4, &dwData1, &dwBytes1, &dwData2, &dwBytes2, 0);
	}
	if(FAILED(hr))
		return;

	if (currprefs.sound_stereo_swap_ahi) {
		int i;
		uae_s16 *p = (uae_s16*)ahisndbuffer;
		for (i = 0; i < (dwBytes1 + dwBytes2) / 2; i += 2) {
			uae_s16 tmp;
			tmp = p[i + 0];
			p[i + 0] = p[i + 1];
			p[i + 1] = tmp;
		}
	}

	memcpy (dwData1, ahisndbuffer, dwBytes1);
	if (dwData2)
		memcpy (dwData2, (uae_u8*)ahisndbuffer + dwBytes1, dwBytes2);

	sndptrmax = ahisndbuffer + ahisndbufsize;
	ahisndbufpt = (int*)ahisndbuffer;

	IDirectSoundBuffer_Unlock (lpDSB2, dwData1, dwBytes1, dwData2, dwBytes2);

	oldpos += amigablksize * 4;
	if (oldpos >= ahisndbufsize)
		oldpos -= ahisndbufsize;
	if (oldpos != pos) {
		intcount = 1;
		INTREQ (0x8000 | 0x2000);
	}
}


void ahi_finish_sound_buffer (void)
{
	sound_flushes2++;
	ahi_updatesound(2);
}

static WAVEFORMATEX wavfmt;

static int ahi_init_record_win32 (void)
{
	HRESULT hr;
	DSCBUFFERDESC sound_buffer_rec;
	// Record begin
	hr = DirectSoundCaptureCreate (NULL, &lpDS2r, NULL);
	if (FAILED (hr)) {
		write_log (_T("AHI: DirectSoundCaptureCreate() failure: %s\n"), DXError (hr));
		record_enabled = -1;
		return 0;
	}
	memset (&sound_buffer_rec, 0, sizeof (DSCBUFFERDESC));
	sound_buffer_rec.dwSize = sizeof (DSCBUFFERDESC);
	sound_buffer_rec.dwBufferBytes = amigablksize * 4 * RECORDBUFFER;
	sound_buffer_rec.lpwfxFormat = &wavfmt;
	sound_buffer_rec.dwFlags = 0 ;

	hr = IDirectSoundCapture_CreateCaptureBuffer (lpDS2r, &sound_buffer_rec, &lpDSB2r, NULL);
	if (FAILED (hr)) {
		write_log (_T("AHI: CreateCaptureSoundBuffer() failure: %s\n"), DXError(hr));
		record_enabled = -1;
		return 0;
	}

	hr = IDirectSoundCaptureBuffer_Start (lpDSB2r, DSCBSTART_LOOPING);
	if (FAILED (hr)) {
		write_log (_T("AHI: DirectSoundCaptureBuffer_Start failed: %s\n"), DXError (hr));
		record_enabled = -1;
		return 0;
	}
	record_enabled = 1;
	write_log (_T("AHI: Init AHI Audio Recording \n"));
	return 1;
}

void setvolume_ahi(LONG volume)
{
	HRESULT hr;

	if (!lpDS2)
		return;

	float adjvol = (100.0f - currprefs.sound_volume_board) * (100.0f - volume) / 100.0f;
	LONG vol = DSBVOLUME_MIN;
	if (adjvol > 0) {
		vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log(1 + (2.718281828 - 1) * (adjvol / 100.0)));
	}
	hr = IDirectSoundBuffer_SetVolume(lpDSB2, vol);
	if (FAILED(hr))
		write_log(_T("AHI: SetVolume(%d) failed: %s\n"), vol, DXError (hr));
}

static int ahi_init_sound_win32 (void)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	HRESULT hr;
	DSBUFFERDESC sound_buffer;
	DSCAPS DSCaps;

	if (lpDS2)
		return 0;

	enumerate_sound_devices ();
	wavfmt.wFormatTag = WAVE_FORMAT_PCM;
	wavfmt.nChannels = sound_channels_ahi;
	wavfmt.nSamplesPerSec = sound_freq_ahi;
	wavfmt.wBitsPerSample = sound_bits_ahi;
	wavfmt.nBlockAlign = wavfmt.wBitsPerSample / 8 * wavfmt.nChannels;
	wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * sound_freq_ahi;
	wavfmt.cbSize = 0;

	write_log (_T("AHI: Init AHI Sound Rate %d, Channels %d, Bits %d, Buffsize %d\n"),
		sound_freq_ahi, sound_channels_ahi, sound_bits_ahi, amigablksize);

	if (!amigablksize)
		return 0;
	soundneutral = 0;
	ahisndbufsize = (amigablksize * 4) * NATIVBUFFNUM;  // use 4 native buffer
	ahisndbuffer = xmalloc (uae_u8, ahisndbufsize + 32);
	if (!ahisndbuffer)
		return 0;
	if (sound_devices[currprefs.win32_soundcard]->type != SOUND_DEVICE_DS)
		hr = DirectSoundCreate (NULL, &lpDS2, NULL);
	else
		hr = DirectSoundCreate (&sound_devices[currprefs.win32_soundcard]->guid, &lpDS2, NULL);
	if (FAILED (hr)) {
		write_log (_T("AHI: DirectSoundCreate() failure: %s\n"), DXError (hr));
		return 0;
	}
	memset (&sound_buffer, 0, sizeof (DSBUFFERDESC));
	sound_buffer.dwSize = sizeof (DSBUFFERDESC);
	sound_buffer.dwFlags = DSBCAPS_PRIMARYBUFFER;
	sound_buffer.dwBufferBytes = 0;
	sound_buffer.lpwfxFormat = NULL;

	DSCaps.dwSize = sizeof(DSCAPS);
	hr = IDirectSound_GetCaps (lpDS2, &DSCaps);
	if (SUCCEEDED (hr)) {
		if (DSCaps.dwFlags & DSCAPS_EMULDRIVER)
			write_log (_T("AHI: Your DirectSound Driver is emulated via WaveOut - yuck!\n"));
	}
	if (FAILED (IDirectSound_SetCooperativeLevel (lpDS2, mon->hMainWnd, DSSCL_PRIORITY)))
		return 0;
	hr = IDirectSound_CreateSoundBuffer (lpDS2, &sound_buffer, &lpDSBprimary2, NULL);
	if (FAILED (hr)) {
		write_log (_T("AHI: CreateSoundBuffer() failure: %s\n"), DXError(hr));
		return 0;
	}
	hr = IDirectSoundBuffer_SetFormat (lpDSBprimary2, &wavfmt);
	if (FAILED (hr)) {
		write_log (_T("AHI: SetFormat() failure: %s\n"), DXError (hr));
		return 0;
	}
	sound_buffer.dwBufferBytes = ahisndbufsize;
	sound_buffer.lpwfxFormat = &wavfmt;
	sound_buffer.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLVOLUME
		| DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE;
	sound_buffer.guid3DAlgorithm = GUID_NULL;
	hr = IDirectSound_CreateSoundBuffer (lpDS2, &sound_buffer, &lpDSB2, NULL);
	if (FAILED (hr)) {
		write_log (_T("AHI: CreateSoundBuffer() failure: %s\n"), DXError (hr));
		return 0;
	}

	setvolume_ahi (0);

	hr = IDirectSoundBuffer_GetFormat (lpDSBprimary2,&wavfmt,500,0);
	if (FAILED (hr)) {
		write_log (_T("AHI: GetFormat() failure: %s\n"), DXError (hr));
		return 0;
	}

	ahisndbufpt =(int*)ahisndbuffer;
	sndptrmax = ahisndbuffer + ahisndbufsize;
	memset (ahisndbuffer,  soundneutral, amigablksize * 8);
	ahi_on = 1;
	return sound_freq_ahi;
}

int ahi_open_sound (void)
{
	int rate;

	uaevar.changenum++;
	if (!sound_freq_ahi)
		return 0;
	if (ahi_on)
		ahi_close_sound ();
	sound_flushes2 = 1;
	if ((rate = ahi_init_sound_win32 ()))
		return rate;
	return 0;
}


static void *bswap_buffer = NULL;
static uae_u32 bswap_buffer_size = 0;
static float syncdivisor;

#if CPU_64_BIT
#define FAKE_HANDLE_WINLAUNCH 0xfffffffeLL
#else
#define FAKE_HANDLE_WINLAUNCH 0xfffffffe
#endif

typedef uae_u32 (*fake_func_get)(struct fake_handle_struct*, const char*);
typedef uae_u32 (*fake_func_exec)(struct fake_handle_struct*, TrapContext*);

struct fake_handle_struct
{
	uae_u32 handle;
	uae_u32 func_start;
	uae_u32 func_end;
	fake_func_get get;
	fake_func_exec exec;
};

// "Emulate" winlaunch

static uae_u32 fake_winlaunch_get(struct fake_handle_struct *me, const char *name)
{
	if (!stricmp(name, "_launch"))
		return me->func_start;
	return 0;
}
static const int fake_winlaunch_cmdval[] =
{
	SW_HIDE, SW_MAXIMIZE, SW_MINIMIZE, SW_RESTORE, SW_SHOW, SW_SHOWDEFAULT, SW_SHOWMAXIMIZED,
	SW_SHOWMINIMIZED, SW_SHOWMINNOACTIVE, SW_SHOWNA, SW_SHOWNOACTIVATE, SW_SHOWNORMAL
};
static uae_u32 fake_winlaunch_exec(struct fake_handle_struct *me, TrapContext *ctx)
{
	uae_u32 file = m68k_dreg(regs, 1);
	if (!valid_address(file, 2))
		return 0;
	uae_u32 parms = m68k_dreg(regs, 2);
	uae_u8 *fileptr = get_real_address(file);
	uae_u8 *parmsptr = NULL;
	if (parms)
		parmsptr = get_real_address(parms);
	uae_u32 showcmdval = m68k_dreg(regs, 3);
	if (showcmdval > 11)
		return 0;
	uae_u32 ret = (uae_u32)(uae_u64)ShellExecuteA(NULL, NULL, (char*)fileptr, (char*)parmsptr, "", fake_winlaunch_cmdval[showcmdval]);
	uae_u32 aret = 0;
	switch (ret) {
		case 0:
			aret = 1;
		break;
		case ERROR_FILE_NOT_FOUND:
			aret = 2;
		break;
		case ERROR_PATH_NOT_FOUND:
			aret = 3;
		break;
		case ERROR_BAD_FORMAT:
			aret = 4;
		break;
		case SE_ERR_ACCESSDENIED:
			aret = 5;
		break;
		case SE_ERR_ASSOCINCOMPLETE:
			aret = 6;
		break;
		case SE_ERR_DDEBUSY:
			aret = 7;
		break;
		case SE_ERR_DDEFAIL:
			aret = 8;
		break;
		case SE_ERR_DDETIMEOUT:
			aret = 9;
		break;
		case SE_ERR_DLLNOTFOUND:
			aret = 10;
		break;
		case SE_ERR_NOASSOC:
			aret = 11;
		break;
		case SE_ERR_OOM:
			aret = 12;
		break;
		case SE_ERR_SHARE:
			aret = 13;
		break;
	}
	return aret;
}

static struct fake_handle_struct fake_handles[] =
{
	{ FAKE_HANDLE_WINLAUNCH, 4, 4, fake_winlaunch_get, fake_winlaunch_exec, },
	{ 0 }
};


static HMODULE native_override(const TCHAR *dllname, TrapContext *ctx)
{
	const TCHAR *s = _tcsrchr(dllname, '/');
	if (!s)
		s = _tcsrchr(dllname, '\\');
	if (!s) {
		s = dllname;
	} else if (s) {
		s++;
	}
	if (!_tcsicmp(s, _T("winlaunch.alib"))) {
		return (HMODULE)FAKE_HANDLE_WINLAUNCH;
	}
	return 0;
}

uae_u32 REGPARAM2 ahi_demux (TrapContext *context)
{
	//use the extern int (6 #13)
	// d0 0=opensound      d1=unit d2=samplerate d3=blksize ret: sound frequency
	// d0 6=opensound_new  d1=unit d2=samplerate d3=blksize ret d4=channels d5=bits d6=zero: sound frequency
	// d0 1=closesound     d1=unit
	// d0 2=writesamples   d1=unit a0=addr     write blksize samples to card
	// d0 3=readsamples    d1=unit a0=addr     read samples from card ret: d0=samples read
	// make sure you have from amigaside blksize*4 mem alloced
	// d0=-1 no data available d0=-2 no recording open
	// d0 > 0 there are more blksize Data in the que
	// do the loop until d0 get 0
	// if d0 is greater than 200 bring a message
	// that show the user that data is lost
	// maximum blocksbuffered are 250 (8,5 sec)
	// d0 4=writeinterrupt d1=unit d0=0 no interrupt happen for this unit
	// d0=-2 no playing open
	//note units for now not support use only unit 0
	// d0 5=?
	// d0=10 get clipboard size  d0=size in bytes
	// d0=11 get clipboard data  a0=clipboarddata
	//Note: a get clipboard size must do before
	// d0=12 write clipboard data	 a0=clipboarddata
	// d0=13 setp96mouserate	 d1=hz value
	// d0=100 open dll		 d1=dll name in windows name conventions
	// d0=101 get dll function addr	 d1=dllhandle a0 function/var name
	// d0=102 exec dllcode		 a0=addr of function (see 101)
	// d0=103 close dll
	// d0=104 screenlost
	// d0=105 mem offset
	// d0=106 16Bit byteswap
	// d0=107 32Bit byteswap
	// d0=108 free swap array
	// d0=200 ahitweak		 d1=offset for dsound position pointer

	int opcode = m68k_dreg (regs, 0);

	switch (opcode)
	{
		static int cap_pos, clipsize;
		static TCHAR *clipdat;

	case 0:
		cap_pos = 0;
		sound_bits_ahi = 16;
		sound_channels_ahi = 2;
		sound_freq_ahi = m68k_dreg (regs, 2);
		amigablksize = m68k_dreg (regs, 3);
		sound_freq_ahi = ahi_open_sound();
		uaevar.changenum--;
		return sound_freq_ahi;
	case 6: /* new open function */
		cap_pos = 0;
		sound_freq_ahi = m68k_dreg (regs, 2);
		amigablksize = m68k_dreg (regs, 3);
		sound_channels_ahi = m68k_dreg (regs, 4);
		sound_bits_ahi = m68k_dreg (regs, 5);
		sound_freq_ahi = ahi_open_sound();
		uaevar.changenum--;
		return sound_freq_ahi;

	case 1:
		ahi_close_sound();
		sound_freq_ahi = 0;
		return 0;

	case 2:
		{
			int i;
			uaecptr addr = m68k_areg (regs, 0);
			for (i = 0; i < amigablksize * 4; i += 4)
				*ahisndbufpt++ = get_long (addr + i);
			ahi_finish_sound_buffer();
		}
		return amigablksize;

	case 3:
		{
			LPVOID pos1, pos2;
			DWORD t, cur_pos;
			uaecptr addr;
			HRESULT hr;
			int i, todo;
			DWORD byte1, byte2;

			if (!ahi_on)
				return -2;
			if (record_enabled == 0)
				ahi_init_record_win32();
			if (record_enabled < 0)
				return -2;
			hr = lpDSB2r->GetCurrentPosition(&t, &cur_pos);
			if (FAILED(hr))
				return -1;

			t =  amigablksize * 4;
			if (cap_pos <= cur_pos)
				todo = cur_pos - cap_pos;
			else
				todo = cur_pos + (RECORDBUFFER * t) - cap_pos;
			if (todo < t) //if no complete buffer ready exit
				return -1;
			hr = lpDSB2r->Lock(cap_pos, t, &pos1, &byte1, &pos2, &byte2, 0);
			if (FAILED(hr))
				return -1;
			if ((cap_pos + t) < (t * RECORDBUFFER))
				cap_pos = cap_pos + t;
			else
				cap_pos = 0;
			addr = m68k_areg (regs, 0);
			uae_u16 *sndbufrecpt = (uae_u16*)pos1;
			t /= 4;
			for (i = 0; i < t; i++) {
				uae_u32 s1, s2;
				if (currprefs.sound_stereo_swap_ahi) {
					s1 = sndbufrecpt[1];
					s2 = sndbufrecpt[0];
				} else {
					s1 = sndbufrecpt[0];
					s2 = sndbufrecpt[1];
				}
				sndbufrecpt += 2;
				put_long (addr, (s1 << 16) | s2);
				addr += 4;
			}
			t *= 4;
			lpDSB2r->Unlock(pos1, byte1, pos2, byte2);
			return (todo - t) / t;
		}

	case 4:
		{
			int i;
			if (!ahi_on)
				return -2;
			i = intcount;
			intcount = 0;
			return i;
		}

	case 5:
		if (!ahi_on)
			return 0;
		ahi_updatesound ( 1 );
		return 1;

	case 10:
#if 1
		if (OpenClipboard (0)) {
			clipdat = (TCHAR*)GetClipboardData (CF_UNICODETEXT);
			if (clipdat) {
				clipsize = uaetcslen(clipdat);
				clipsize++;
				return clipsize;
			}
		}
#endif
		return 0;

	case 11:
		{
#if 1
			put_byte (m68k_areg (regs, 0), 0);
			if (clipdat) {
				char *tmp = ua (clipdat);
				int i;
				for (i = 0; i < clipsize && i < strlen (tmp); i++)
					put_byte (m68k_areg (regs, 0) + i, tmp[i]);
				put_byte (m68k_areg (regs, 0) + clipsize - 1, 0);
				xfree (tmp);
			}
			CloseClipboard ();
#endif
		}
		return 0;

	case 12:
		{
#if 1
			TCHAR *s = au ((char*)get_real_address (m68k_areg (regs, 0)));
			static LPTSTR p;
			int slen;

			if (OpenClipboard (0)) {
				EmptyClipboard();
				slen = uaetcslen(s);
				if (p)
					GlobalFree (p);
				p = (LPTSTR)GlobalAlloc (GMEM_MOVEABLE, (slen + 1) * sizeof (TCHAR));
				if (p) {
					TCHAR *p2 = (TCHAR*)GlobalLock (p);
					if (p2) {
						_tcscpy (p2, s);
						GlobalUnlock (p);
						SetClipboardData (CF_UNICODETEXT, p);
					}
				}
				CloseClipboard ();
			}
			xfree (s);
#endif
		}
		return 0;

	case 13: /* HACK */
		{ //for higher P96 mouse draw rate
			set_picasso_hack_rate (m68k_dreg (regs, 1) * 2);
		} //end for higher P96 mouse draw rate
		return 0;

	case 20:
		return enforcer_enable(m68k_dreg (regs, 1));

	case 21:
		return enforcer_disable();

	case 25:
		flushprinter ();
		return 0;

	case 100: // open dll
		{
			if (!currprefs.native_code)
				return 0;
			TCHAR *dlldir = TEXT ("winuae_dll");
			TCHAR *dllname;
			uaecptr dllptr;
			HMODULE h = NULL;
			int ok = 0;
			DWORD err = 0;

			dllptr = m68k_areg (regs, 0);
			if (!valid_address(dllptr, 2))
				return 0;
			dllname = au ((uae_char*)get_real_address (dllptr));
			h = native_override(dllname, context);
#if defined(X86_MSVC_ASSEMBLY)
			if (h == 0) {
				TCHAR *filepart;
				TCHAR dpath[MAX_DPATH];
				TCHAR newdllpath[MAX_DPATH];
				dpath[0] = 0;
				GetFullPathName (dllname, sizeof dpath / sizeof (TCHAR), dpath, &filepart);
				if (_tcslen (dpath) > _tcslen (start_path_data) && !_tcsncmp (dpath, start_path_data, _tcslen (start_path_data))) {
					/* path really is relative to winuae directory */
					ok = 1;
					_tcscpy (newdllpath, dpath + _tcslen (start_path_data));
					if (!_tcsncmp (newdllpath, dlldir, _tcslen (dlldir))) /* remove "winuae_dll" */
						_tcscpy (newdllpath, dpath + _tcslen (start_path_data) + 1 + _tcslen (dlldir));
					_stprintf (dpath, _T("%s%s%s"), start_path_data, WIN32_PLUGINDIR, newdllpath);
					h = LoadLibrary (dpath);
					if (h == NULL)
						err = GetLastError();
					if (h == NULL) {
						_stprintf (dpath, _T("%s%s\\%s"), start_path_data, dlldir, newdllpath);
						h = LoadLibrary (dllname);
						if (h == NULL) {
							DWORD err2 = GetLastError();
							if (h == NULL) {
								write_log (_T("fallback native open: '%s' = %d, %d\n"), dpath, err2, err);
							}
						}
					}
				} else {
					write_log (_T("native open outside of installation dir '%s'!\n"), dpath);
				}
			}
#endif
			xfree (dllname);
			syncdivisor = (3580000.0f * CYCLE_UNIT) / (float)syncbase;
			return (uae_u32)(uae_u64)h;
		}

	case 101:	//get dll label
		{
			if (currprefs.native_code) {
				uaecptr funcaddr;
				char *funcname;
				uae_u32 m = m68k_dreg (regs, 1);
				funcaddr = m68k_areg (regs, 0);
				funcname = (char*)get_real_address (funcaddr);
				for (int i = 0; fake_handles[i].handle; i++) {
					if (fake_handles[i].handle == m) {
						return fake_handles[i].get(&fake_handles[i], funcname);
					}
				}
#if defined(X86_MSVC_ASSEMBLY)
				return (uae_u32) GetProcAddress ((HMODULE)m, funcname);
#endif
			}
			return 0;
		}

	case 102:	//execute native code
		{
			uae_u32 ret = 0;
			if (currprefs.native_code) {
				uaecptr funcptr = m68k_areg(regs, 0);
				for (int i = 0; fake_handles[i].handle; i++) {
					if (fake_handles[i].func_start >= funcptr && fake_handles[i].func_end <= funcptr) {
						return fake_handles[i].exec(&fake_handles[i], context);
					}
				}
#if defined(X86_MSVC_ASSEMBLY)
				frame_time_t rate1;
				double v;
				rate1 = read_processor_time ();
				ret = emulib_ExecuteNativeCode2 (context);
				rate1 = read_processor_time () - rate1;
				v = syncdivisor * rate1;
				if (v > 0) {
					if (v > 1000000 * CYCLE_UNIT)
						v = 1000000 * CYCLE_UNIT;
					do_extra_cycles ((unsigned long)(syncdivisor * rate1)); //compensate the time stay in native func
				}
#endif
			}
			return ret;
		}

	case 103:	//close dll
		{
			if (currprefs.native_code) {
				uae_u32 addr = m68k_dreg (regs, 1);
				for (int i = 0; fake_handles[i].handle; i++) {
					if (addr == fake_handles[i].handle) {
						addr = 0;
						break;
					}
				}
#if defined(X86_MSVC_ASSEMBLY)
				if (addr) {
					HMODULE libaddr = (HMODULE)addr;
					FreeLibrary (libaddr);
				}
#endif
			}
			return 0;
		}

	case 104:        //screenlost
		{
			static int oldnum = 0;
			if (uaevar.changenum == oldnum)
				return 0;
			oldnum = uaevar.changenum;
			return 1;
		}

#if defined(X86_MSVC_ASSEMBLY)

	case 105:   //returns memory offset
		return (uae_u32) get_real_address (0);

	case 106:   //byteswap 16bit vars
	{
		//a0 = start address
		//d1 = number of 16bit vars
		//returns address of new array
		uae_u32 src = m68k_areg (regs, 0);
		uae_u32 num_vars = m68k_dreg (regs, 1);

		if (bswap_buffer_size < num_vars * 2) {
			bswap_buffer_size = (num_vars + 1024) * 2;
			free(bswap_buffer);
			bswap_buffer = (void*)malloc(bswap_buffer_size);
		}
		if (!bswap_buffer)
			return 0;

		__asm {
			mov esi, dword ptr [src]
			mov edi, dword ptr [bswap_buffer]
			mov ecx, num_vars

				mov ebx, ecx
				and ecx, 3
				je BSWAP_WORD_4X

BSWAP_WORD_LOOP:
			mov ax, [esi]
			mov dl, al
				mov al, ah
				mov ah, dl
				mov [edi], ax
				add esi, 2
				add edi, 2
				loopne BSWAP_WORD_LOOP

BSWAP_WORD_4X:
			mov ecx, ebx
				shr ecx, 2
				je BSWAP_WORD_END
BSWAP_WORD_4X_LOOP:
			mov ax, [esi]
			mov dl, al
				mov al, ah
				mov ah, dl
				mov [edi], ax
				mov ax, [esi+2]
			mov dl, al
				mov al, ah
				mov ah, dl
				mov [edi+2], ax
				mov ax, [esi+4]
			mov dl, al
				mov al, ah
				mov ah, dl
				mov [edi+4], ax
				mov ax, [esi+6]
			mov dl, al
				mov al, ah
				mov ah, dl
				mov [edi+6], ax
				add esi, 8
				add edi, 8
				loopne BSWAP_WORD_4X_LOOP
BSWAP_WORD_END:
		}
		return (uae_u32) bswap_buffer;
	}

	case 107:   //byteswap 32bit vars - see case 106
	{
		//a0 = start address
		//d1 = number of 32bit vars
		//returns address of new array
		uae_u32 src = m68k_areg (regs, 0);
		uae_u32 num_vars = m68k_dreg (regs, 1);
		if (bswap_buffer_size < num_vars * 4) {
			bswap_buffer_size = (num_vars + 16384) * 4;
			free(bswap_buffer);
			bswap_buffer = (void*)malloc(bswap_buffer_size);
		}
		if (!bswap_buffer)
			return 0;
		__asm {
			mov esi, dword ptr [src]
			mov edi, dword ptr [bswap_buffer]
			mov ecx, num_vars

				mov ebx, ecx
				and ecx, 3
				je BSWAP_DWORD_4X

BSWAP_DWORD_LOOP:
			mov eax, [esi]
			bswap eax
				mov [edi], eax
				add esi, 4
				add edi, 4
				loopne BSWAP_DWORD_LOOP

BSWAP_DWORD_4X:
			mov ecx, ebx
				shr ecx, 2
				je BSWAP_DWORD_END
BSWAP_DWORD_4X_LOOP:
			mov eax, [esi]
			bswap eax
				mov [edi], eax
				mov eax, [esi+4]
			bswap eax
				mov [edi+4], eax
				mov eax, [esi+8]
			bswap eax
				mov [edi+8], eax
				mov eax, [esi+12]
			bswap eax
				mov [edi+12], eax
				add esi, 16
				add edi, 16
				loopne BSWAP_DWORD_4X_LOOP

BSWAP_DWORD_END:
		}
		return (uae_u32) bswap_buffer;
	}

	case 108: //frees swap array
		bswap_buffer_size = 0;
		free (bswap_buffer);
		bswap_buffer = NULL;
		return 0;

	case 110:
		{
			LARGE_INTEGER p;
			QueryPerformanceFrequency (&p);
			put_long (m68k_areg (regs, 0), p.HighPart);
			put_long (m68k_areg (regs, 0) + 4, p.LowPart);
		}
		return 1;

	case 111:
		{
			LARGE_INTEGER p;
			QueryPerformanceCounter (&p);
			put_long (m68k_areg (regs, 0), p.HighPart);
			put_long (m68k_areg (regs, 0) + 4, p.LowPart);
		}
		return 1;

#endif

	case 200:
		ahitweak = m68k_dreg (regs, 1);
		ahi_pollrate = m68k_dreg (regs, 2);
		if (ahi_pollrate < 10)
			ahi_pollrate = 10;
		if (ahi_pollrate > 60)
			ahi_pollrate = 60;
		return 1;

	default:
		return 0x12345678;     // Code for not supportet function
	}
}

#endif
