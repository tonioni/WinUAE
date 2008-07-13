#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <dxerr8.h>

static int sounddata_frequency = 44100;
static int sounddata_stereo = 2;
static int sounddata_bufsize = 262144 * 2;

#define write_log printf

#define MAX_SOUND_DEVICES 10

static char *sound_devices[MAX_SOUND_DEVICES];
static GUID sound_device_guid[MAX_SOUND_DEVICES];
static int num_sound_devices, devicenum;

static LPDIRECTSOUND lpDS;
static LPDIRECTSOUNDBUFFER lpDSBprimary;
static LPDIRECTSOUNDBUFFER lpDSBsecondary;

static uae_u8 sndbuffer[131072*8];
static int dsoundbuf, snd_configsize;
static DWORD writepos;
static HWND hwnd;
static LARGE_INTEGER qpf;

static FILE *logfile;

static void write_log2 (const char *format, ...)
{
    int count;
    char buffer[1000];

    va_list parms;
    va_start (parms, format);
    count = _vsnprintf( buffer, 1000-1, format, parms );
    printf("%s", buffer);
    if (logfile)
	fwrite(buffer, 1, strlen(buffer), logfile);
    va_end (parms);
}

static const char *DXError (HRESULT ddrval)
{
    static char dderr[200];
    sprintf(dderr, "%08X S=%d F=%04X C=%04X (%d) (%s)",
	ddrval, (ddrval & 0x80000000) ? 1 : 0,
	HRESULT_FACILITY(ddrval),
	HRESULT_CODE(ddrval),
	HRESULT_CODE(ddrval),
	DXGetErrorDescription8 (ddrval));
    return dderr;
}

static void clearbuffer (void)
{
    void *buffer;
    DWORD size;

    HRESULT hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, dsoundbuf, &buffer, &size, NULL, NULL, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (lpDSBsecondary);
	hr = IDirectSoundBuffer_Lock (lpDSBsecondary, 0, dsoundbuf, &buffer, &size, NULL, NULL, 0);
    }
    if (hr != DS_OK) {
	write_log ("failed to Lock sound buffer (clear): %s\n", DXError (hr));
	return;
    }
    memset (buffer, 0, size);
    IDirectSoundBuffer_Unlock (lpDSBsecondary, buffer, size, NULL, 0);
    memset (sndbuffer, 0, sizeof (sndbuffer));
}

static void pause_audio_ds (void)
{
    IDirectSoundBuffer_Stop (lpDSBsecondary);
    clearbuffer ();
}

static void resume_audio_ds (void)
{
    HRESULT hr;

    clearbuffer ();
    hr = IDirectSoundBuffer_Play (lpDSBsecondary, 0, 0, DSBPLAY_LOOPING);
    if (hr != DS_OK)
	write_log ("Play failed: %s\n", DXError (hr));
    writepos = snd_configsize;
}

static int restore (DWORD hr)
{
    if (hr != DSERR_BUFFERLOST)
	return 0;
    hr = IDirectSoundBuffer_Restore (lpDSBsecondary);
    if (hr != DS_OK)
	return 1;
    resume_audio_ds ();
    return 1;
}

static void close_audio_ds (void)
{
    if (lpDSBsecondary)
	IDirectSound_Release (lpDSBsecondary);
    if (lpDSBprimary)
	IDirectSound_Release (lpDSBprimary);
    lpDSBsecondary = lpDSBprimary = 0;
    if (lpDS)
	IDirectSound_Release (lpDS);
    lpDS = 0;
}

static int open_audio_ds (void)
{
    HRESULT hr;
    DSBUFFERDESC sound_buffer;
    DSCAPS DSCaps;
    DSBCAPS DSBCaps;
    WAVEFORMATEX wavfmt;
    int freq = sounddata_frequency;

    hr = DirectSoundCreate (&sound_device_guid[devicenum], &lpDS, NULL);
    if (hr != DS_OK)  {
	write_log ("SOUND: DirectSoundCreate() failure: %s\n", DXError (hr));
	return 0;
    }
    memset (&DSCaps, 0, sizeof (DSCaps));
    DSCaps.dwSize = sizeof(DSCaps);
    hr = IDirectSound_GetCaps (lpDS, &DSCaps);
    if (hr!= DS_OK) {
	write_log ("SOUND: Error getting DirectSound capabilities: %s\n", DXError (hr));
	goto error;
    }
    if (DSCaps.dwFlags & DSCAPS_EMULDRIVER) {
	write_log ("SOUND: Emulated DirectSound driver detected, don't complain if sound quality is crap :)\n");
    }
    if (DSCaps.dwFlags & DSCAPS_CONTINUOUSRATE) {
	int minfreq = DSCaps.dwMinSecondarySampleRate;
	int maxfreq = DSCaps.dwMaxSecondarySampleRate;
	if (minfreq > freq && freq < 22050) {
	    freq = minfreq;
	    write_log ("SOUND: minimum supported frequency: %d\n", minfreq);
	}
	if (maxfreq < freq && freq > 44100) {
	    freq = maxfreq;
	    write_log ("SOUND: maximum supported frequency: %d\n", maxfreq);
	}
    }

    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_GETCURRENTPOSITION2;
    hr = IDirectSound_CreateSoundBuffer (lpDS, &sound_buffer, &lpDSBprimary, NULL);
    if( hr != DS_OK )  {
	write_log ("SOUND: Primary CreateSoundBuffer() failure: %s\n", DXError (hr));
	goto error;
    }

    memset(&DSBCaps, 0, sizeof(DSBCaps));
    DSBCaps.dwSize = sizeof(DSBCaps);
    hr = IDirectSoundBuffer_GetCaps(lpDSBprimary, &DSBCaps);
    if (hr != DS_OK) {
	write_log ("SOUND: Primary GetCaps() failure: %s\n",  DXError (hr));
	goto error;
    }

    wavfmt.wFormatTag = WAVE_FORMAT_PCM;
    wavfmt.nChannels = sounddata_stereo ? 2 : 1;
    wavfmt.nSamplesPerSec = freq;
    wavfmt.wBitsPerSample = 16;
    wavfmt.nBlockAlign = 16 / 8 * wavfmt.nChannels;
    wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * freq;

    hr = IDirectSound_SetCooperativeLevel (lpDS, hwnd, DSSCL_PRIORITY);
    if (hr != DS_OK) {
	write_log ("SOUND: Can't set cooperativelevel: %s\n", DXError (hr));
	goto error;
    }

    dsoundbuf = sounddata_bufsize;
    if (dsoundbuf < DSBSIZE_MIN)
	dsoundbuf = DSBSIZE_MIN;
    if (dsoundbuf > DSBSIZE_MAX)
	dsoundbuf = DSBSIZE_MAX;

    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwBufferBytes = dsoundbuf;
    sound_buffer.lpwfxFormat = &wavfmt;
    sound_buffer.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_STATIC;

    hr = IDirectSound_CreateSoundBuffer( lpDS, &sound_buffer, &lpDSBsecondary, NULL );
    if (hr != DS_OK) {
	write_log ("SOUND: Secondary CreateSoundBuffer() failure: %s\n", DXError (hr));
	goto error;
    }

    hr = IDirectSoundBuffer_SetFormat (lpDSBprimary, &wavfmt);
    if( hr != DS_OK )  {
	write_log ("SOUND: Primary SetFormat() failure: %s\n", DXError (hr));
	goto error;
    }

    clearbuffer ();

    return 1;

error:
    close_audio_ds ();
    return 0;
}

static BOOL CALLBACK DSEnumProc(LPGUID lpGUID, LPCTSTR lpszDesc, LPCTSTR lpszDrvName,  LPVOID lpContext)
{
    int i = num_sound_devices;
    if (i == MAX_SOUND_DEVICES)
	return TRUE;
    if (lpGUID != NULL)
	memcpy (&sound_device_guid[i], lpGUID, sizeof (GUID));
    sound_devices[i] = my_strdup (lpszDesc);
    num_sound_devices++;
    return TRUE;
}

static HWND GetConsoleHwnd(void)
{
    #define MY_BUFSIZE 1024 // buffer size for console window titles
    HWND hwndFound;         // this is what is returned to the caller
    char pszNewWindowTitle[MY_BUFSIZE]; // contains fabricated WindowTitle
    char pszOldWindowTitle[MY_BUFSIZE]; // contains original WindowTitle

    // fetch current window title

    GetConsoleTitle(pszOldWindowTitle, MY_BUFSIZE);

    // format a "unique" NewWindowTitle

    wsprintf(pszNewWindowTitle,"%d/%d",
		GetTickCount(),
		GetCurrentProcessId());

    // change current window title

    SetConsoleTitle(pszNewWindowTitle);

    // ensure window title has been updated

    Sleep(1000);

    // look for NewWindowTitle

    hwndFound=FindWindow(NULL, pszNewWindowTitle);

    // restore original window title

    SetConsoleTitle(pszOldWindowTitle);

    return(hwndFound);
}

static LARGE_INTEGER qpfc;

static void storeqpf (void)
{
    QueryPerformanceCounter(&qpfc);
}
static double getqpf (void)
{
    LARGE_INTEGER qpfc2;
    QueryPerformanceCounter(&qpfc2);
    return (qpfc2.QuadPart - qpfc.QuadPart) / (qpf.QuadPart / 1000.0);
}


static int mm_timerres, timeon;
static HANDLE timehandle;

static int timeend (void)
{
    if (!timeon)
	return 1;
    timeon = 0;
    if (timeEndPeriod (mm_timerres) == TIMERR_NOERROR)
	return 1;
    write_log ("TimeEndPeriod() failed\n");
    return 0;
}

static int timebegin (void)
{
    if (timeon) {
	timeend();
	return timebegin();
    }
    timeon = 0;
    if (timeBeginPeriod (mm_timerres) == TIMERR_NOERROR) {
	timeon = 1;
	return 1;
    }
    write_log ("TimeBeginPeriod() failed\n");
    return 0;
}

static int init_mmtimer (void)
{
    TIMECAPS tc;
    mm_timerres = 0;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR)
	return 0;
    mm_timerres = min(max(tc.wPeriodMin, 1), tc.wPeriodMax);
    timehandle = CreateEvent (NULL, TRUE, FALSE, NULL);
    return 1;
}

static int sm (int ms)
{
    UINT TimerEvent;
    TimerEvent = timeSetEvent (ms, 0, timehandle, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    if (!TimerEvent) {
	printf ("timeSetEvent() failed, exiting..\n");
	return 0;
    } else {
	WaitForSingleObject (timehandle, ms);
	ResetEvent (timehandle);
	timeKillEvent (TimerEvent);
    }
    return 1;
}

static int getpos (void)
{
    DWORD playpos, safepos;
    HRESULT hr;

    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSBsecondary, &playpos, &safepos);
    if (hr != DS_OK) {
	write_log ("GetCurrentPosition failed: %s\n", DXError (hr));
	return -1;
    }
    return playpos;
}

static void runtest(void)
{
    int pos, spos, expected, len, freqdiff;
    int lastpos, minpos, maxpos;
    int mult = sounddata_stereo ? 4 : 2;
    double qv;

    sm (100);
    write_log2 ("frequency: %d\n", sounddata_frequency);
    pause_audio_ds ();
    storeqpf ();
    sm (100);
    qv = getqpf ();
    if (qv < 95 || qv > 105)
	write_log2("timing mismatch, something wrong with your system timer (%.1fms)\n", qv);

    resume_audio_ds ();
    pos = 0;
    storeqpf ();
    while (pos == 0 && getqpf () < 1000) {
	pos = getpos();
    }
    if (getqpf () >= 1000) {
	printf ("sound didn't start!?\n");
	return;
    }
    printf ("startup-delay: %d samples (%.1fms)\n", pos, getqpf());
    minpos = 100000;
    maxpos = -1;
    lastpos = getpos();
    while (pos < 20000) {
	pos = getpos();
	if (pos - lastpos <= 0)
	    continue;
	if (pos - lastpos < minpos)
	    minpos = pos - lastpos;
	if (pos - lastpos > maxpos)
	    maxpos = pos -lastpos;
	lastpos = pos;
    }
    write_log2 ("position granularity: minimum %d, maximum %d samples\n", minpos, maxpos);
    len = 200;
    while (len <= 1400) {
	pause_audio_ds ();
	resume_audio_ds ();
	while (pos > 1000)
	    pos = getpos();
	while (pos < 1000)
	    pos = getpos();
	spos = pos;
	storeqpf ();
	do {
	    qv = getqpf();
	} while (qv < len);
	pos = getpos();
	pos -= spos;
	expected = (int)(len / 1000.0 * sounddata_frequency * mult);
	write_log2("%d samples, should be %d (%d samples, %.2f%%) %dms delay\n",
	    pos,  expected, pos - expected, (pos * 100.0 / expected), len);
	if (len == 1000) {
	    freqdiff = expected - pos;
	}
	len += 400;
    }
    write_log2("real calculated frequency: %d\n",
	sounddata_frequency - freqdiff);
    pause_audio_ds ();
}

static int selectdevice (void)
{
    int i, val;
    for (i = 0; i < num_sound_devices; i++) {
	printf ("%d: %s\n", i + 1, sound_devices[i]);
    }
    printf("select sound driver (1 - %d) and press return: ", num_sound_devices);
    scanf ("%d", &val);
    if (val < 1)
	val = 1;
    if (val > num_sound_devices)
	val = 1;
    val--;
    printf("\n");
    write_log2("testing '%s'\n", sound_devices[val]);
    return val;
}

static int os_winnt, os_winnt_admin;

static OSVERSIONINFO osVersion;

static int osdetect (void)
{
    HANDLE hAccessToken;
    UCHAR InfoBuffer[1024];
    PTOKEN_GROUPS ptgGroups = (PTOKEN_GROUPS)InfoBuffer;
    DWORD dwInfoBufferSize;
    PSID psidAdministrators;
    SID_IDENTIFIER_AUTHORITY siaNtAuthority = SECURITY_NT_AUTHORITY;
    UINT x;
    BOOL bSuccess;

    os_winnt = 0;
    os_winnt_admin = 0;

    osVersion.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );
    if( GetVersionEx( &osVersion ) )
    {
	if (osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT)
	    os_winnt = 1;
    }

    if (!os_winnt) {
	return 1;
    }

    if(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE,
	 &hAccessToken )) {
	 if(GetLastError() != ERROR_NO_TOKEN)
	    return 1;
	 //
	 // retry against process token if no thread token exists
	 //
	 if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY,
	    &hAccessToken))
	    return 1;
      }

      bSuccess = GetTokenInformation(hAccessToken,TokenGroups,InfoBuffer,
	 1024, &dwInfoBufferSize);

      CloseHandle(hAccessToken);

      if(!bSuccess )
	 return 1;

      if(!AllocateAndInitializeSid(&siaNtAuthority, 2,
	 SECURITY_BUILTIN_DOMAIN_RID,
	 DOMAIN_ALIAS_RID_ADMINS,
	 0, 0, 0, 0, 0, 0,
	 &psidAdministrators))
	 return 1;

   // assume that we don't find the admin SID.
      bSuccess = FALSE;

      for(x=0;x<ptgGroups->GroupCount;x++)
      {
	 if( EqualSid(psidAdministrators, ptgGroups->Groups[x].Sid) )
	 {
	    bSuccess = TRUE;
	    break;
	 }

      }
      FreeSid(psidAdministrators);
      os_winnt_admin = bSuccess ? 1 : 0;
      return 1;
   }

static int srates[] = { 22050, 44100, 48000, 0 };

int main (int argc, char **argv)
{
    int i;

    logfile = fopen("soundlog.txt","w");
    osdetect();
    write_log2("WinUAE soundtest 0.2. OS: %s %d.%d%s\n",
	os_winnt ? "NT" : "W9X/ME", osVersion.dwMajorVersion, osVersion.dwMinorVersion, os_winnt_admin ? " (Admin)" : "");

    init_mmtimer();
    if (!QueryPerformanceFrequency(&qpf)) {
	printf ("no QPF, exiting..\n");
	goto end;
    }
    write_log2("QPF: %.2fMHz\n", qpf.QuadPart / 1000000.0);
    hwnd = GetConsoleHwnd();
    DirectSoundEnumerate ((LPDSENUMCALLBACK)DSEnumProc, 0);
    devicenum = selectdevice ();
    i = 0;
    while (srates[i]) {
	sounddata_frequency = srates[i];
	if (open_audio_ds ()) {
	    if (!timebegin ()) {
		printf ("no MM timer, exiting..\n");
		goto end;
	    }
	    write_log2("\n");
	    runtest ();
	    timeend ();
	    close_audio_ds ();
	} else break;
	i++;
    }
end:
    fclose(logfile);
    return 0;
}












