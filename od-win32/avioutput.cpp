/*
UAE - The Ultimate Amiga Emulator

avioutput.c

Copyright(c) 2001 - 2002; §ane
2005-2006; Toni Wilen

*/

#include <windows.h>

#include <mmsystem.h>
#include <vfw.h>
#include <msacm.h>
#include <ks.h>
#include <ksmedia.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "resource.h"

#include "options.h"
#include "audio.h"
#include "custom.h"
#include "memory.h"
#include "newcpu.h"
#include "picasso96.h"
#include "render.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "sound.h"
#include "gfxfilter.h"
#include "xwin.h"
#include "avioutput.h"
#include "registry.h"
#include "fsdb.h"
#include "threaddep/thread.h"
#include "zfile.h"
#include "savestate.h"
#include "gfxboard.h"

void WIN32GUI_LoadUIString(DWORD id, TCHAR *string, DWORD dwStringLen);

#define MAX_AVI_SIZE (0x80000000 - 0x1000000)

static smp_comm_pipe workindex;
static smp_comm_pipe queuefull;
static volatile int alive;
static volatile int avioutput_failed;

static int avioutput_init = 0;
static int avioutput_needs_restart;

static int frame_start; // start frame
static int frame_count; // current frame
static int frame_skip;
static uae_u64 total_avi_size;
static int partcnt;
static int first_frame;
static int aviout_monid;
static int fps_in_use;

int avioutput_audio, avioutput_video, avioutput_enabled, avioutput_requested;
static int videoallocated;

static int aviout_width_out, aviout_height_out;
static int aviout_width_in, aviout_height_in;
static int aviout_xoffset_out, aviout_yoffset_out;
int avioutput_width, avioutput_height, avioutput_bits;
static int avioutput_bitmode = 24;
static int avioutput_fps = VBLANK_HZ_PAL;
int avioutput_framelimiter = 0, avioutput_nosoundoutput = 0;
int avioutput_nosoundsync = 1, avioutput_originalsize = 0;
int avioutput_split_files = 1;

TCHAR avioutput_filename_gui[MAX_DPATH];
TCHAR avioutput_filename_auto[MAX_DPATH];
TCHAR avioutput_filename_inuse[MAX_DPATH];
static TCHAR avioutput_filename_tmp[MAX_DPATH];

extern struct uae_prefs workprefs;
extern TCHAR config_filename[256];

static CRITICAL_SECTION AVIOutput_CriticalSection;
static int cs_allocated;

static PAVIFILE pfile = NULL; // handle of our AVI file
static PAVISTREAM AVIStreamInterface = NULL; // Address of stream interface
static struct zfile *FileStream;

struct avientry {
	uae_u8 *lpVideo;
	LPBITMAPINFOHEADER lpbi;
	uae_u8 *lpAudio;
	int sndsize;
	int expectedsize;
};

#define AVIENTRY_MAX 10
static int avientryindex;
static struct avientry *avientries[AVIENTRY_MAX + 1];

/* audio */

static int FirstAudio;
static bool audio_validated;
static DWORD dwAudioInputRemaining;
static unsigned int StreamSizeAudio; // audio write position
static float StreamSizeAudioExpected, StreamSizeAudioGot;
static PAVISTREAM AVIAudioStream = NULL; // compressed stream pointer
static HACMSTREAM has = NULL; // stream handle that can be used to perform conversions
static ACMSTREAMHEADER ash;
static ACMFORMATCHOOSE acmopt;
static TCHAR acmopt_title[MAX_PATH];
static WAVEFORMATEXTENSIBLE wfxSrc; // source audio format
static LPWAVEFORMATEX pwfxDst = NULL; // pointer to destination audio format
static DWORD wfxMaxFmtSize;
static FILE *wavfile;
static uae_u8 *lpAudioDst, *lpAudioSrc;
static DWORD dwAudioOutputBytes, dwAudioInputBytes;

static uae_u8 *avi_sndbuffer;
static int avi_sndbufsize;
static int avi_sndbuffered;

/* video */

static PAVISTREAM AVIVideoStream = NULL; // compressed stream pointer
static AVICOMPRESSOPTIONS videoOptions;
static AVICOMPRESSOPTIONS FAR * aOptions[] = { &videoOptions }; // array of pointers to AVICOMPRESSOPTIONS structures
static LPBITMAPINFOHEADER lpbi;
static PCOMPVARS pcompvars;

extern bool need_genlock_data;
extern uae_u8 **row_map_genlock;

static bool usealpha(void)
{
	return need_genlock_data != 0 && row_map_genlock && currprefs.genlock_image && currprefs.genlock_alpha;
}

void avi_message (const TCHAR *format,...)
{
	TCHAR msg[MAX_DPATH];
	va_list parms;
	DWORD flags = MB_OK | MB_TASKMODAL;

	va_start (parms, format);
	_vsntprintf (msg, sizeof msg / sizeof (TCHAR), format, parms);
	va_end (parms);

	write_log (msg);
	if (msg[_tcslen (msg) - 1] != '\n')
		write_log (_T("\n"));

	if (!MessageBox (NULL, msg, _T("AVI"), flags))
		write_log (_T("MessageBox(%s) failed, err=%d\n"), msg, GetLastError ());
}

static int lpbisize (void)
{
	return sizeof (BITMAPINFOHEADER) + (((avioutput_bits <= 8) ? 1 << avioutput_bits : 0) * sizeof (RGBQUAD));
}

static void freeavientry (struct avientry *ae)
{
	if (!ae)
		return;
	xfree (ae->lpAudio);
	xfree (ae->lpVideo);
	xfree (ae->lpbi);
	xfree (ae);
}

static struct avientry *allocavientry_audio (uae_u8 *snd, int size, int expectedsize)
{
	struct avientry *ae = xcalloc (struct avientry, 1);
	ae->lpAudio = xmalloc (uae_u8, size);
	memcpy (ae->lpAudio, snd, size);
	ae->sndsize = size;
	ae->expectedsize = expectedsize;
	return ae;
}

static struct avientry *allocavientry_video(void)
{
	struct avientry *ae = xcalloc (struct avientry, 1); 
	ae->lpbi = (LPBITMAPINFOHEADER)xmalloc (uae_u8, lpbisize ());
	memcpy (ae->lpbi, lpbi, lpbisize ());
	ae->lpVideo = xcalloc (uae_u8, lpbi->biSizeImage);
	return ae;
}

static void queueavientry (struct avientry *ae)
{
	EnterCriticalSection (&AVIOutput_CriticalSection);
	avientries[++avientryindex] = ae;
	LeaveCriticalSection (&AVIOutput_CriticalSection);
	write_comm_pipe_u32 (&workindex, 0, 1);
}

static struct avientry *getavientry (void)
{
	int i;
	struct avientry *ae;
	if (avientryindex < 0)
		return NULL;
	ae = avientries[0];
	for (i = 0; i < avientryindex; i++)
		avientries[i] = avientries[i + 1];
	avientryindex--;
	return ae;
}

static void freequeue (void)
{
	struct avientry *ae;
	while ((ae = getavientry())) {
		freeavientry(ae);
	}
}

static void waitqueuefull (void)
{
	for (;;) {
		EnterCriticalSection (&AVIOutput_CriticalSection);
		if (avientryindex < AVIENTRY_MAX) {
			LeaveCriticalSection (&AVIOutput_CriticalSection);
			while (comm_pipe_has_data (&queuefull))
				read_comm_pipe_u32_blocking (&queuefull);
			return;
		}
		LeaveCriticalSection (&AVIOutput_CriticalSection);
		read_comm_pipe_u32_blocking (&queuefull);
	}
}

static UAEREG *openavikey (void)
{
	return regcreatetree (NULL, _T("AVConfiguration"));
}

static void storesettings (UAEREG *avikey)
{
	regsetint (avikey, _T("FrameLimiter"), avioutput_framelimiter);
	regsetint (avikey, _T("NoSoundOutput"), avioutput_nosoundoutput);
	regsetint (avikey, _T("NoSoundSync"), avioutput_nosoundsync);
	regsetint (avikey, _T("Original"), avioutput_originalsize);
	regsetint(avikey, _T("FPS"), avioutput_fps);
	regsetint(avikey, _T("RecordMode"), avioutput_audio);
	regsetstr(avikey, _T("FileName"), avioutput_filename_gui);
}
static void getsettings (UAEREG *avikey, bool allowaudio)
{
	int val;
	if (regqueryint (avikey, _T("NoSoundOutput"), &val))
		avioutput_nosoundoutput = val;
	if (regqueryint (avikey, _T("NoSoundSync"), &val))
		avioutput_nosoundsync = val;
	if (regqueryint (avikey, _T("FrameLimiter"), &val))
		avioutput_framelimiter = val;
	if (regqueryint (avikey, _T("Original"), &val))
		avioutput_originalsize = val;
	if (!avioutput_framelimiter)
		avioutput_nosoundoutput = 1;
	if (regqueryint (avikey, _T("FPS"), &val))
		avioutput_fps = val;
	if (allowaudio) {
		if (regqueryint(avikey, _T("RecordMode"), &val))
			avioutput_audio = val;
	}
	val = sizeof avioutput_filename_gui / sizeof(TCHAR);
	regquerystr(avikey, _T("FileName"), avioutput_filename_gui, &val);
}

void AVIOutput_GetSettings (void)
{
	UAEREG *avikey = openavikey ();
	if (avikey)
		getsettings (avikey, true);
	regclosetree (avikey);
}
void AVIOutput_SetSettings (void)
{
	UAEREG *avikey = openavikey ();
	if (avikey)
		storesettings (avikey);
	regclosetree (avikey);
}

void AVIOutput_ReleaseAudio (void)
{
	audio_validated = false;
}

static void AVIOutput_FreeAudioDstFormat ()
{
	xfree (pwfxDst);
	pwfxDst = NULL;
}

static int AVIOutput_AudioAllocated (void)
{
	return pwfxDst && audio_validated;
}

static int AVIOutput_AllocateAudio (bool immediate)
{
	struct uae_prefs *p = immediate ? &workprefs : &currprefs;

	AVIOutput_ReleaseAudio ();

	// set the source format
	memset (&wfxSrc, 0, sizeof (wfxSrc));
	wfxSrc.Format.wFormatTag = WAVE_FORMAT_PCM;
	wfxSrc.Format.nChannels = get_audio_nativechannels (p->sound_stereo) ? get_audio_nativechannels (p->sound_stereo) : 2;
	wfxSrc.Format.nSamplesPerSec = p->sound_freq ? p->sound_freq : 44100;
	wfxSrc.Format.nBlockAlign = wfxSrc.Format.nChannels * 16 / 8;
	wfxSrc.Format.nAvgBytesPerSec = wfxSrc.Format.nBlockAlign * wfxSrc.Format.nSamplesPerSec;
	wfxSrc.Format.wBitsPerSample = 16;
	wfxSrc.Format.cbSize = 0;

	if (wfxSrc.Format.nChannels > 2) {
		wfxSrc.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
		wfxSrc.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		wfxSrc.Samples.wValidBitsPerSample = 16;
		switch (wfxSrc.Format.nChannels)
		{
		case 4:
			wfxSrc.dwChannelMask = KSAUDIO_SPEAKER_SURROUND;
			break;
		case 6:
			wfxSrc.dwChannelMask = KSAUDIO_SPEAKER_5POINT1_SURROUND;
			break;
		case 8:
			wfxSrc.dwChannelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
			break;
		}
	}

	if (!pwfxDst) {
		MMRESULT err;
		if ((err = acmMetrics (NULL, ACM_METRIC_MAX_SIZE_FORMAT, &wfxMaxFmtSize))) {
			gui_message (_T("acmMetrics() FAILED (%X)\n"), err);
			return 0;
		}
		if (wfxMaxFmtSize < sizeof (WAVEFORMATEX))
			return 0;
		pwfxDst = (LPWAVEFORMATEX)xmalloc (uae_u8, wfxMaxFmtSize);
		memcpy(pwfxDst, &wfxSrc.Format, sizeof WAVEFORMATEX);
		pwfxDst->cbSize = (WORD) (wfxMaxFmtSize - sizeof (WAVEFORMATEX)); // shrugs
	}


	memset(&acmopt, 0, sizeof (ACMFORMATCHOOSE));
	acmopt.cbStruct = sizeof (ACMFORMATCHOOSE);
	acmopt.fdwStyle = ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT;
	acmopt.pwfx = pwfxDst;
	acmopt.cbwfx = wfxMaxFmtSize;
	WIN32GUI_LoadUIString(IDS_CHOOSE_AUDIO_CODEC, acmopt_title, sizeof(acmopt_title) / sizeof(TCHAR));
	acmopt.pszTitle = acmopt_title;

	//acmopt.szFormatTag =; // not valid until the format is chosen
	//acmopt.szFormat =; // not valid until the format is chosen

	//acmopt.pszName =; // can use later in config saving loading
	//acmopt.cchName =; // size of pszName, as pszName can be non-null-terminated

	acmopt.fdwEnum = ACM_FORMATENUMF_INPUT | ACM_FORMATENUMF_NCHANNELS |
		ACM_FORMATENUMF_NSAMPLESPERSEC;
	//ACM_FORMATENUMF_CONVERT // renders WinUAE unstable for some unknown reason
	//ACM_FORMATENUMF_WBITSPERSAMPLE // MP3 doesn't apply so it will be removed from codec selection
	//ACM_FORMATENUMF_SUGGEST // with this flag set, only MP3 320kbps is displayed, which is closest to the source format

	acmopt.pwfxEnum = &wfxSrc.Format;
	FirstAudio = 1;
	dwAudioInputRemaining = 0;
	return 1;
}

static int AVIOutput_ValidateAudio (WAVEFORMATEX *wft, TCHAR *name, int len)
{
	DWORD ret;
	ACMFORMATTAGDETAILS aftd;
	ACMFORMATDETAILS afd;

	audio_validated = false;
	memset(&aftd, 0, sizeof (ACMFORMATTAGDETAILS));
	aftd.cbStruct = sizeof (ACMFORMATTAGDETAILS);
	aftd.dwFormatTag = wft->wFormatTag;
	ret = acmFormatTagDetails (NULL, &aftd, ACM_FORMATTAGDETAILSF_FORMATTAG);
	if (ret) {
		write_log(_T("acmFormatTagDetails %08x failed\n"), wft->wFormatTag);
		return 0;
	}

	memset (&afd, 0, sizeof (ACMFORMATDETAILS));
	afd.cbStruct = sizeof (ACMFORMATDETAILS);
	afd.dwFormatTag = wft->wFormatTag;
	afd.pwfx = wft;
	afd.cbwfx = sizeof (WAVEFORMATEX) + wft->cbSize;
	ret = acmFormatDetails (NULL, &afd, ACM_FORMATDETAILSF_FORMAT);
	if (ret) {
		write_log(_T("acmFormatDetails %08x failed\n"), wft->wFormatTag);
		return 0;
	}

	if (name)
		_stprintf (name, _T("%s %s"), aftd.szFormatTag, afd.szFormat);
	audio_validated = true;
	return 1;
}

static int AVIOutput_GetAudioFromRegistry (WAVEFORMATEX *wft)
{
	int ss;
	int ok = 0;
	UAEREG *avikey;

	avikey = openavikey ();
	if (!avikey)
		return 0;
	getsettings (avikey, true);
	if (wft) {
		ok = -1;
		ss = wfxMaxFmtSize;
		if (regquerydata (avikey, _T("AudioConfigurationVars"), wft, &ss)) {
			if (AVIOutput_ValidateAudio (wft, NULL, 0))
				ok = 1;
		}
	}
	if (ok < 0 || !wft)
		regdelete (avikey, _T("AudioConfigurationVars"));
	regclosetree (avikey);
	return ok;
}

static int AVIOutput_GetAudioCodecName (WAVEFORMATEX *wft, TCHAR *name, int len)
{
	return AVIOutput_ValidateAudio (wft, name, len);
}

int AVIOutput_GetAudioCodec (TCHAR *name, int len)
{
	AVIOutput_Initialize ();
	if (AVIOutput_AudioAllocated ())
		return AVIOutput_GetAudioCodecName (pwfxDst, name, len);
	if (!AVIOutput_AllocateAudio (true))
		return 0;
	if (AVIOutput_GetAudioFromRegistry (pwfxDst) > 0) {
		if (AVIOutput_GetAudioCodecName (pwfxDst, name, len)) {
			write_log(_T("Audio: %s\n"), name);
			return 1;
		}
		write_log(_T("Failed to open audio %08x\n"), pwfxDst->wFormatTag);
	}
	AVIOutput_ReleaseAudio ();
	return 0;
}

int AVIOutput_ChooseAudioCodec (HWND hwnd, TCHAR *s, int len)
{
	AVIOutput_Initialize ();
	AVIOutput_End();
	if (!AVIOutput_AllocateAudio (true))
		return 0;

	acmopt.hwndOwner = hwnd;

	switch (acmFormatChoose (&acmopt))
	{
	case MMSYSERR_NOERROR:
		{
			UAEREG *avikey;
			_tcscpy (s, acmopt.szFormatTag);
			avikey = openavikey ();
			if (avikey) {
				regsetdata (avikey, _T("AudioConfigurationVars"), pwfxDst, pwfxDst->cbSize + sizeof (WAVEFORMATEX));
				storesettings (avikey);
				regclosetree (avikey);
			}
			return 1;
		}

	case ACMERR_CANCELED:
		AVIOutput_GetAudioFromRegistry (NULL);
		AVIOutput_ReleaseAudio();
		break;

	case ACMERR_NOTPOSSIBLE:
		MessageBox (hwnd, _T("The buffer identified by the pwfx member of the ACMFORMATCHOOSE structure is too small to contain the selected format."), VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;

	case MMSYSERR_INVALFLAG:
		MessageBox (hwnd, _T("At least one flag is invalid."), VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;

	case MMSYSERR_INVALHANDLE:
		MessageBox (hwnd, _T("The specified handle is invalid."), VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;

	case MMSYSERR_INVALPARAM:
		MessageBox (hwnd, _T("At least one parameter is invalid."), VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;

	case MMSYSERR_NODRIVER:
		MessageBox (hwnd, _T("A suitable driver is not available to provide valid format selections.\n(Unsupported channel-mode selected in Sound-panel?)"), VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;

	default:
		MessageBox (hwnd, _T("acmFormatChoose() FAILED"), VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
	}
	return 0;
}

static int AVIOutput_VideoAllocated(void)
{
	return videoallocated ? 1 : 0;
}

void AVIOutput_ReleaseVideo(void)
{
	videoallocated = 0;
	freequeue();
	xfree(lpbi);
	lpbi = NULL;
}

static int AVIOutput_AllocateVideo(void)
{
	struct AmigaMonitor *mon = &AMonitors[aviout_monid];
	bool locked = false;

	avioutput_width = avioutput_height = avioutput_bits = 0;
	aviout_width_out = aviout_height_out = 0;
	aviout_xoffset_out = aviout_yoffset_out = 0;

	avioutput_fps = (int)(vblank_hz + 0.5);
	if (!avioutput_fps)
		avioutput_fps = ispal(NULL) ? 50 : 60;
	if (avioutput_originalsize || WIN32GFX_IsPicassoScreen(mon)) {
		int pitch;
		if (!gfxboard_isgfxboardscreen(0)) {
			uae_u8 *p = getfilterbuffer(0, &avioutput_width, &avioutput_height, &pitch, &avioutput_bits, &locked);
			freefilterbuffer(0, p, locked);
		} else {
			gfxboard_freertgbuffer(0, gfxboard_getrtgbuffer(0, &avioutput_width, &avioutput_height, &pitch, &avioutput_bits, NULL));
		}
	}

	if (avioutput_width == 0 || avioutput_height == 0 || avioutput_bits == 0) {
		avioutput_width = WIN32GFX_GetWidth(mon);
		avioutput_height = WIN32GFX_GetHeight(mon);
		avioutput_bits = 32;
		if (WIN32GFX_IsPicassoScreen(mon) && avioutput_originalsize) {
			struct picasso96_state_struct *state = &picasso96_state[aviout_monid];
			avioutput_width = state->Width;
			avioutput_height = state->Height;
		}
	}

	aviout_width_in = avioutput_width;
	aviout_height_in = avioutput_height;
	aviout_width_out = avioutput_width + 15;
	aviout_width_out &= ~15;
	aviout_height_out = avioutput_height + 1;
	aviout_height_out &= ~1;

	AVIOutput_Initialize();
	AVIOutput_ReleaseVideo();
	if (avioutput_width == 0 || avioutput_height == 0) {
		avioutput_width = workprefs.gfx_monitor[0].gfx_size.width;
		avioutput_height = workprefs.gfx_monitor[0].gfx_size.height;
		avioutput_bits = 32;
	}
	if (!aviout_height_out)
		aviout_height_out = avioutput_height;
	if (!aviout_width_out)
		aviout_width_out = avioutput_width;

	if (avioutput_originalsize) {
		if (currprefs.aviout_width > 0)
			aviout_width_out = currprefs.aviout_width;
		if (currprefs.aviout_height > 0)
			aviout_height_out = currprefs.aviout_height;
		if (currprefs.aviout_xoffset >= 0)
			aviout_xoffset_out = currprefs.aviout_xoffset;
		if (currprefs.aviout_yoffset >= 0)
			aviout_yoffset_out = currprefs.aviout_yoffset;
	}

	if (avioutput_bits == 0)
		avioutput_bits = 24;
	if (avioutput_bits > 24)
		avioutput_bits = 24;
	if (avioutput_bitmode && avioutput_bits >= 24)
		avioutput_bits = avioutput_bitmode;

	lpbi = (LPBITMAPINFOHEADER)xcalloc(uae_u8, lpbisize());
	lpbi->biSize = sizeof(BITMAPINFOHEADER);
	lpbi->biWidth = aviout_width_out;
	lpbi->biHeight = aviout_height_out;
	lpbi->biPlanes = 1;
	lpbi->biBitCount = avioutput_bits;
	lpbi->biCompression = BI_RGB; // uncompressed format
	lpbi->biSizeImage = (((lpbi->biWidth * lpbi->biBitCount + 31) & ~31) / 8) * lpbi->biHeight;
	lpbi->biXPelsPerMeter = 0; // ??
	lpbi->biYPelsPerMeter = 0; // ??
	lpbi->biClrUsed = (lpbi->biBitCount <= 8) ? 1 << lpbi->biBitCount : 0;
	lpbi->biClrImportant = 0;

	videoallocated = 1;
	return 1;
}

static int compressorallocated;
static void AVIOutput_FreeVideoDstFormat(void)
{
	if (!pcompvars)
		return;
	ICClose(pcompvars->hic);
	if (compressorallocated)
		ICCompressorFree(pcompvars);
	compressorallocated = FALSE;
	pcompvars->hic = NULL;
}

static int AVIOutput_GetCOMPVARSFromRegistry(COMPVARS *pcv)
{
	UAEREG *avikey;
	int ss;
	int ok = 0;

	avikey = openavikey ();
	if (!avikey)
		return 0;
	getsettings (avikey, false);
	if (pcv) {
		ok = -1;
		ss = pcv->cbSize;
		pcv->hic = 0;
		if (regquerydata (avikey, _T("VideoConfigurationVars"), pcv, &ss)) {
			pcv->hic = 0;
			pcv->lpbiIn = pcv->lpbiOut = 0;
			pcv->cbState = 0;
			if (regquerydatasize (avikey, _T("VideoConfigurationState"), &ss)) {
				LPBYTE state = NULL;
				if (ss > 0) {
					state = xmalloc (BYTE, ss);
					regquerydata(avikey, _T("VideoConfigurationState"), state, &ss);
				}
				pcv->hic = ICOpen (pcv->fccType, pcv->fccHandler, ICMODE_COMPRESS);
				if (pcv->hic || (!pcv->hic && pcv->fccHandler == mmioFOURCC('D', 'I', 'B', ' '))) { // DIB = uncompressed
					ok = 1;
					if (pcv->hic)
						ICSetState (pcv->hic, state, ss);
				}
				xfree (state);
			}
		}
	}
	if (ok < 0 || !pcv) {
		regdelete (avikey, _T("VideoConfigurationVars"));
		regdelete (avikey, _T("VideoConfigurationState"));
	}
	regclosetree (avikey);
	return ok;
}

static int AVIOutput_GetVideoCodecName (COMPVARS *pcv, TCHAR *name, int len)
{
	ICINFO icinfo = { 0 };

	name[0] = 0;
	if (pcv->fccHandler == mmioFOURCC ('D','I','B',' ')) {
		_tcscpy (name, _T("Full Frames (Uncompressed)"));
		return 1;
	}
	if (ICGetInfo (pcv->hic, &icinfo, sizeof (ICINFO)) != 0) {
		_tcsncpy (name, icinfo.szDescription, len);
		return 1;
	}
	return 0;
}

int AVIOutput_GetVideoCodec(TCHAR *name, int len)
{
	AVIOutput_Initialize();

	if (AVIOutput_VideoAllocated())
		return AVIOutput_GetVideoCodecName(pcompvars, name, len);
	if (!AVIOutput_AllocateVideo())
		return 0;
	AVIOutput_FreeVideoDstFormat();
	if (AVIOutput_GetCOMPVARSFromRegistry(pcompvars) > 0) {
		if (AVIOutput_GetVideoCodecName(pcompvars, name, len)) {
			write_log(_T("Video %s\n"), name);
			return 1;
		}
		write_log(_T("Failed to open video %08x\n"), pcompvars->fccType);
	}
	AVIOutput_ReleaseVideo();
	return 0;
}

int AVIOutput_ChooseVideoCodec (HWND hwnd, TCHAR *s, int len)
{
	TCHAR tmp1[MAX_PATH];
	char tmp2[MAX_PATH];

	AVIOutput_Initialize();

	AVIOutput_End();
	if (!AVIOutput_AllocateVideo())
		return 0;
	AVIOutput_FreeVideoDstFormat();

	// we really should check first to see if the user has a particular compressor installed before we set one
	// we could set one but we will leave it up to the operating system and the set priority levels for the compressors

	// default
	//pcompvars->fccHandler = mmioFOURCC('C','V','I','D'); // "Cinepak Codec by Radius"
	//pcompvars->fccHandler = mmioFOURCC('M','R','L','E'); // "Microsoft RLE"
	//pcompvars->fccHandler = mmioFOURCC('D','I','B',' '); // "Full Frames (Uncompressed)"

	pcompvars->lQ = 10000; // 10000 is maximum quality setting or ICQUALITY_DEFAULT for default
	pcompvars->lKey = avioutput_fps; // default to one key frame per second, every (FPS) frames
	pcompvars->dwFlags = 0;
	WIN32GUI_LoadUIString(IDS_CHOOSE_VIDEO_CODEC, tmp1, sizeof(tmp1) / sizeof(TCHAR));
	ua_copy(tmp2, sizeof(tmp2), tmp1);
	if (ICCompressorChoose(hwnd, ICMF_CHOOSE_DATARATE | ICMF_CHOOSE_KEYFRAME, lpbi, NULL, pcompvars, tmp2) == TRUE) {
		UAEREG *avikey;
		LRESULT ss;
		uae_u8 *state;

		compressorallocated = TRUE;
		ss = ICGetState (pcompvars->hic, NULL, 0);
		if (ss > 0) {
			LRESULT err;
			state = xmalloc (uae_u8, ss);
			err = ICGetState (pcompvars->hic, state, ss);
			if (err < 0) {
				ss = 0;
				xfree (state);
			}
		} else {
			ss = 0;
		}
		if (ss == 0)
			state = xcalloc (uae_u8, 1);
		avikey = openavikey ();
		if (avikey) {
			regsetdata (avikey, _T("VideoConfigurationState"), state, (DWORD)ss);
			regsetdata (avikey, _T("VideoConfigurationVars"), pcompvars, pcompvars->cbSize);
			storesettings (avikey);
			regclosetree (avikey);
		}
		xfree (state);
		return AVIOutput_GetVideoCodecName (pcompvars, s, len);
	} else {
		AVIOutput_GetCOMPVARSFromRegistry (NULL);
		AVIOutput_ReleaseVideo ();
		return 0;
	}
}

static void AVIOutput_Begin2(bool fullstart, bool immediate);
static void AVIOutput_End2(bool);

uae_s64 max_avi_size = MAX_AVI_SIZE;

static void checkAVIsize(bool force, bool split)
{
	int tmp_avioutput_video = avioutput_video;
	int tmp_avioutput_audio = avioutput_audio;
	TCHAR fn[MAX_DPATH];

	if (!force && total_avi_size < max_avi_size)
		return;
	if (total_avi_size == 0)
		return;
	if (split) {
		if (!avioutput_split_files)
			return;
		partcnt++;
		_tcscpy(fn, avioutput_filename_tmp);
		_stprintf(avioutput_filename_inuse, _T("%s_%d.avi"), fn, partcnt);
		write_log(_T("AVI split %d at %lld bytes, %d frames\n"), partcnt, total_avi_size, frame_count);
	} else {
		write_log(_T("AVI restart at %lld bytes, %d frames\n"), total_avi_size, frame_count);
	}
	AVIOutput_End2(false);
	total_avi_size = 0;
	avioutput_video = tmp_avioutput_video;
	avioutput_audio = tmp_avioutput_audio;
	AVIOutput_Begin2(false, false);
	_tcscpy (avioutput_filename_tmp, fn);
}

static void dorestart(void)
{
	write_log (_T("AVIOutput: parameters changed, restarting..\n"));
	checkAVIsize(true, avioutput_needs_restart > 0);
	avioutput_needs_restart = 0;
}

static void AVIOuput_AVIWriteAudio (uae_u8 *sndbuffer, int sndbufsize, int expectedsize)
{
	struct avientry *ae;

	if (avioutput_failed) {
		AVIOutput_End ();
		return;
	}
	if (!sndbufsize)
		return;
	checkAVIsize(false, true);
	if (avioutput_needs_restart)
		dorestart ();
	waitqueuefull ();
	ae = allocavientry_audio (sndbuffer, sndbufsize, expectedsize);
	queueavientry (ae);
}


typedef short          HWORD;
typedef unsigned short UHWORD;
typedef int            LWORD;
typedef unsigned int   ULWORD;
static int resampleFast(float factor, HWORD *in, HWORD *out, int inCount, int outCount, int nChans);

static uae_u8 *hack_resample(uae_u8 *srcbuffer, int gotSize, int wantedSize)
{
	uae_u8 *outbuf;
	int ch = wfxSrc.Format.nChannels;
	int bytesperframe = ch * 2;

	int gotSamples = gotSize / bytesperframe;
	int wantedSamples = wantedSize / bytesperframe;

	float factor = (float)wantedSamples / gotSamples;
	outbuf = xmalloc(uae_u8, wantedSize + bytesperframe);
	resampleFast(factor, (HWORD*)srcbuffer, (HWORD*)outbuf, gotSamples, wantedSamples, ch);
	return outbuf;
}

static int AVIOutput_AVIWriteAudio_Thread (struct avientry *ae)
{
	DWORD flags;
	LONG swritten = 0, written = 0;
	unsigned int err;
	uae_u8 *srcbuffer = NULL;
	uae_u8 *freebuffer = NULL;
	int srcbuffersize = 0;

	if (!avioutput_audio)
		return 1;

	if (!avioutput_init)
		goto error;

	if (ae) {
		srcbuffer = ae->lpAudio;
		srcbuffersize = ae->sndsize;
		// size didn't match, resample it to keep AV sync.
		if (ae->expectedsize && ae->expectedsize != ae->sndsize) {
			srcbuffer = hack_resample(srcbuffer, ae->sndsize, ae->expectedsize);
			srcbuffersize = ae->expectedsize;
			freebuffer = srcbuffer;
		}
	}

	if (FirstAudio) {
		if ((err = acmStreamSize(has, wfxSrc.Format.nSamplesPerSec, &dwAudioOutputBytes, ACM_STREAMSIZEF_SOURCE) != 0)) {
			gui_message (_T("acmStreamSize() FAILED (%X)\n"), err);
			goto error;
		}
		dwAudioInputBytes = wfxSrc.Format.nSamplesPerSec;
		if (!(lpAudioSrc = xcalloc (uae_u8, dwAudioInputBytes)))
			goto error;
		if (!(lpAudioDst = xcalloc (uae_u8, dwAudioOutputBytes)))
			goto error;
		
		memset(&ash, 0, sizeof ash);
		ash.cbStruct = sizeof (ACMSTREAMHEADER);

		// source
		ash.pbSrc = lpAudioSrc;
		ash.cbSrcLength = dwAudioInputBytes;

		// destination
		ash.pbDst = lpAudioDst;
		ash.cbDstLength = dwAudioOutputBytes;

		if ((err = acmStreamPrepareHeader (has, &ash, 0))) {
			avi_message (_T("acmStreamPrepareHeader() FAILED (%X)\n"), err);
			goto error;
		}
	}

	ash.cbSrcLength = 0;
	if (srcbuffer) {
		if (srcbuffersize + dwAudioInputRemaining > dwAudioInputBytes) {
			avi_message(_T("AVIOutput audio buffer overflow!?"));
			goto error;
		}
		ash.cbSrcLength = srcbuffersize;
		memcpy(ash.pbSrc + dwAudioInputRemaining, srcbuffer, srcbuffersize);
	}
	ash.cbSrcLength += dwAudioInputRemaining;
	ash.cbSrcLengthUsed = 0;

	flags = ACM_STREAMCONVERTF_BLOCKALIGN;
	if (FirstAudio)
		flags |= ACM_STREAMCONVERTF_START;
	if (!ae)
		flags |= ACM_STREAMCONVERTF_END;

	if ((err = acmStreamConvert (has, &ash, flags))) {
		avi_message (_T("acmStreamConvert() FAILED (%X)\n"), err);
		goto error;
	}

	dwAudioInputRemaining = ash.cbSrcLength - ash.cbSrcLengthUsed;
	if (dwAudioInputRemaining)
		memmove(ash.pbSrc, ash.pbSrc + ash.cbSrcLengthUsed, dwAudioInputRemaining);

	if (ash.cbDstLengthUsed) {
		if (FileStream) {
			zfile_fwrite(lpAudioDst, 1, ash.cbDstLengthUsed, FileStream);
		}  else {
			if ((err = AVIStreamWrite (AVIAudioStream, StreamSizeAudio, ash.cbDstLengthUsed / pwfxDst->nBlockAlign, lpAudioDst, ash.cbDstLengthUsed, 0, &swritten, &written)) != 0) {
				avi_message (_T("AVIStreamWrite() FAILED (%X)\n"), err);
				goto error;
			}
		}
	}

	StreamSizeAudio += swritten;
	total_avi_size += written;

	FirstAudio = 0;
	xfree(freebuffer);

	return 1;

error:
	xfree(freebuffer);
	return 0;
}

static void AVIOutput_AVIWriteAudio_Thread_End(void)
{
	if (!FirstAudio && !avioutput_failed) {
		AVIOutput_AVIWriteAudio_Thread(NULL);
	}
	ash.cbSrcLength = avioutput_failed ? 0 : dwAudioInputBytes;
	acmStreamUnprepareHeader(has, &ash, 0);
	xfree(lpAudioDst);
	lpAudioDst = NULL;
	xfree(lpAudioSrc);
	lpAudioSrc = NULL;
	FirstAudio = 1;
	dwAudioInputRemaining = 0;
}


static void AVIOuput_WAVWriteAudio(uae_u8 *sndbuffer, int sndbufsize)
{
	fwrite(sndbuffer, 1, sndbufsize, wavfile);
}

static int getFromRenderTarget11(struct avientry *avie, bool renderTarget)
{
	int ok = 0;
	int w, h, pitch, bits = 32, d;
	void *data;

	bool got = D3D11_capture(aviout_monid, &data, &w, &h, &d, &pitch, renderTarget);
	if (got) {
		int dpitch = ((aviout_width_out * avioutput_bits + 31) & ~31) / 8;
		for (int y = 0; y < h && y < aviout_height_out; y++) {
			uae_u8 *d = (uae_u8*)avie->lpVideo + (aviout_height_out - y - 1) * dpitch;
			uae_u32 *s = (uae_u32*)((uae_u8*)data + y * pitch);
			for (int x = 0; x < w && x < aviout_width_out; x++) {
				uae_u32 v = *s++;
				d[0] = v >> 0;
				d[1] = v >> 8;
				d[2] = v >> 16;
				if (avioutput_bits == 32) {
					d[3] = v >> 24;
					d += 4;
				} else {
					d += 3;
				}
			}
		}
		D3D11_capture(aviout_monid, NULL, NULL, NULL, NULL, NULL, renderTarget);
		ok = 1;
	}
	return ok;
}

static int getFromRenderTarget(struct avientry *avie, bool renderTarget)
{
	int ok = 0;
	int w, h, bits;
	HRESULT hr;
	D3DLOCKED_RECT l;
	LPDIRECT3DSURFACE9 s = D3D_capture(aviout_monid, &w, &h, &bits, renderTarget);
	if (s) {
		hr = s->LockRect(&l, NULL, D3DLOCK_READONLY);
		if (SUCCEEDED(hr)) {
			int dpitch = ((aviout_width_out * avioutput_bits + 31) & ~31) / 8;
			if (bits == 32) {
				for (int y = 0; y < h && y < aviout_height_out && y < aviout_height_in; y++) {
					uae_u8 *d = (uae_u8*)avie->lpVideo + (aviout_height_out - y - 1) * dpitch;
					uae_u32 *s = (uae_u32*)((uae_u8*)l.pBits + y * l.Pitch);
					for (int x = 0; x < w && x < aviout_width_out && x < aviout_width_in; x++) {
						uae_u32 v = *s++;
						d[0] = v >> 0;
						d[1] = v >> 8;
						d[2] = v >> 16;
						if (avioutput_bits == 32) {
							d[3] = v >> 24;
							d += 4;
						} else {
							d += 3;
						}
					}
				}
			} else if (bits == 16 || bits == 15) {
				for (int y = 0; y < h && y < aviout_height_out && y < aviout_height_in; y++) {
					uae_u8 *d = (uae_u8*)avie->lpVideo + (aviout_height_out - y - 1) * dpitch;
					uae_u16 *s = (uae_u16*)((uae_u8*)l.pBits + y * l.Pitch);
					for (int x = 0; x < w && x < aviout_width_out && x < aviout_width_in; x++) {
						uae_u16 v = s[x];
						uae_u16 v2 = v;
						if (bits == 16) {
							v2 = v & 31;
							v2 |= ((v & (31 << 5)) << 1) | (((v >> 5) & 1) << 5);
							v2 |= (v & (31 << 10)) << 1;
						} else {
							v2 = v & 31;
							v2 |= (v >> 1) & (31 << 5);
							v2 |= (v >> 1) & (31 << 10);
						}
						((uae_u16*)d)[0] = v2;
						d += 2;
					}
				}
			}
			s->UnlockRect();
		}
		ok = 1;
	}
	return ok;
}

static int getFromDC(struct avientry *avie)
{
	HDC hdc;
	HBITMAP hbitmap = NULL;
	HBITMAP hbitmapOld = NULL;
	HDC hdcMem = NULL;
	int ok = 1;

	hdc = gethdc(aviout_monid);
	if (!hdc)
		return 0;
	// create a memory device context compatible with the application's current screen
	hdcMem = CreateCompatibleDC(hdc);
	hbitmap = CreateCompatibleBitmap(hdc, avioutput_width, avioutput_height);
	hbitmapOld = (HBITMAP)SelectObject(hdcMem, hbitmap);
	// probably not the best idea to use slow GDI functions for this,
	// locking the surfaces and copying them by hand would be more efficient perhaps
	// draw centered in frame
	BitBlt(hdcMem, 0, 0, avioutput_width, avioutput_height, hdc, 0, 0, SRCCOPY);
	SelectObject(hdcMem, hbitmapOld);
	if (GetDIBits(hdc, hbitmap, 0, avioutput_height, avie->lpVideo, (LPBITMAPINFO)lpbi, DIB_RGB_COLORS) == 0) {
		gui_message(_T("GetDIBits() FAILED (%X)\n"), GetLastError());
		ok = 0;
	}
	DeleteObject(hbitmap);
	DeleteDC(hdcMem);
	releasehdc(aviout_monid, hdc);
	return ok;
}

static int rgb_type;

void AVIOutput_RGBinfo (int rb, int gb, int bb, int ab, int rs, int gs, int bs, int as)
{
	if (bs == 0 && gs == 5 && rs == 11)
		rgb_type = 1;
	else if (bs == 0 && gs == 8 && rs == 16)
		rgb_type = 2;
	else if (bs == 0 && gs == 5 && rs == 10)
		rgb_type = 3;
	else
		rgb_type = 0;
}

#if defined (GFXFILTER)

static int getFromBuffer(struct avientry *ae, int original)
{
	struct AmigaMonitor *mon = &AMonitors[aviout_monid];
	struct vidbuf_description *vidinfo = &adisplays[aviout_monid].gfxvidinfo;
	int x, y, w, h, d;
	bool locked = false;
	uae_u8 *src = NULL, *mem = NULL;
	uae_u8 *dst = ae->lpVideo;
	int spitch, dpitch;
	int maxw, maxh;
	int freed = 0;

	dpitch = ((aviout_width_out * avioutput_bits + 31) & ~31) / 8;
	maxw = aviout_width_out;
	maxh = aviout_height_out;
	if (original || WIN32GFX_IsPicassoScreen(mon)) {
		if (!gfxboard_isgfxboardscreen(aviout_monid)) {
			src = mem = getfilterbuffer(aviout_monid, &w, &h, &spitch, &d, &locked);
			maxw = w;
			maxh = h;
			freed = 1;
		} else {
			src = mem = gfxboard_getrtgbuffer(aviout_monid, &w, &h, &spitch, &d, NULL);
			maxw = w;
			maxh = h;
			freed = 2;
		}
	}
	if (!src) {
		return 0;
	}

	int xoffset = currprefs.aviout_xoffset < 0 ? (aviout_width_out - avioutput_width) / 2 : -currprefs.aviout_xoffset;
	int yoffset = currprefs.aviout_yoffset < 0 ? (aviout_height_out - avioutput_height) / 2 : -currprefs.aviout_yoffset;

	dst += dpitch * aviout_height_out;
	dst += (aviout_width_out - maxw) / 2;
	dst -= (aviout_height_out - maxh) / 2;

	if (yoffset > 0) {
		if (yoffset >= aviout_height_out - avioutput_height)
			yoffset = aviout_height_out - avioutput_height;
		dst -= dpitch * yoffset;
	} else if (yoffset < 0) {
		yoffset = -yoffset;
		if (yoffset >= avioutput_height - aviout_height_out)
			yoffset = avioutput_height - aviout_height_out;
		src += spitch * yoffset;
	}
	int xoffset2 = 0;
	if (xoffset < 0) {
		xoffset2 = -xoffset;
		xoffset = 0;
	}
	int dbpx = avioutput_bits / 8;
	int sbpx = avioutput_bits / 8;
	if (sbpx == 3)
		sbpx = 4;

	for (y = 0; y < avioutput_height && y < maxh && y < aviout_height_out && y < aviout_height_in; y++) {
		uae_u8 *s, *d;
		dst -= dpitch;
		d = dst;
		s = src;
		if (xoffset > 0) {
			d += xoffset * dbpx;
		} else if (xoffset2 > 0) {
			s += xoffset2 * sbpx;
		}
		for (x = 0; x < avioutput_width && x < maxw && x < aviout_width_out && x < aviout_width_in; x++) {
			if (avioutput_bits == 8) {
				*d++ = s[x];
			} else if (avioutput_bits == 16) {
				uae_u16 v = ((uae_u16*)s)[x];
				uae_u16 v2 = v;
				if (rgb_type == 3) {
					v2 = v & 31;
					v2 |= ((v & (31 << 5)) << 1) | (((v >> 5) & 1) << 5);
					v2 |= (v & (31 << 10)) << 1;
				} else if (rgb_type) {
					v2 = v & 31;
					v2 |= (v >> 1) & (31 << 5);
					v2 |= (v >> 1) & (31 << 10);
				}
				((uae_u16*)d)[0] = v2;
				d += 2;
			} else if (avioutput_bits == 32) {
				uae_u32 v = ((uae_u32*)s)[x];
				((uae_u32*)d)[0] = v;
				d += 4;
			} else if (avioutput_bits == 24) {
				uae_u32 v = ((uae_u32*)s)[x];
				*d++ = v;
				*d++ = v >> 8;
				*d++ = v >> 16;
			}
		}
		src += spitch;
	}
	if (mem) {
		if (freed == 2) {
			gfxboard_freertgbuffer(aviout_monid, mem);
		} else if (freed == 1) {
			freefilterbuffer(aviout_monid, mem, locked);
		}
	}
	return 1;
}
#endif

static void AVIOutput_WriteVideo(void)
{
	struct AmigaMonitor *mon = &AMonitors[aviout_monid];
	struct avientry *ae;
	int v;

	if (avioutput_failed) {
		AVIOutput_End ();
		return;
	}

	checkAVIsize(false, true);
	if (avioutput_needs_restart)
		dorestart ();
	waitqueuefull ();
	ae = allocavientry_video();
	lockrtg();
	if (avioutput_originalsize) {
		v = getFromBuffer (ae, 1);
		if (!v) {
			if (D3D_isenabled(0) == 2) {
				v = getFromRenderTarget11(ae, false);
			} else if (D3D_isenabled(0) == 1) {
				v = getFromRenderTarget(ae, false);
			}
		}
	} else {
		if (D3D_isenabled(0) == 2) {
			v = getFromRenderTarget11(ae, true);
		} else if (D3D_isenabled(0) == 1) {
			v = getFromRenderTarget(ae, true);
		} else {
			v = getFromDC (ae);
		}
	}
	unlockrtg();
	if (v)
		queueavientry (ae);
	else
		AVIOutput_End ();
}

static int AVIOutput_AVIWriteVideo_Thread (struct avientry *ae)
{
	LONG written = 0;
	unsigned int err;

	if (avioutput_video) {

		if (!avioutput_init)
			goto error;

		// GetDIBits tends to change this and ruins palettized output
		ae->lpbi->biClrUsed = (avioutput_bits <= 8) ? 1 << avioutput_bits : 0;

		if (!frame_count) {
			if ((err = AVIStreamSetFormat (AVIVideoStream, frame_count, ae->lpbi, ae->lpbi->biSize + (ae->lpbi->biClrUsed * sizeof (RGBQUAD)))) != 0) {
				avi_message (_T("AVIStreamSetFormat() FAILED (%X)\n"), err);
				goto error;
			}
		}

		if ((err = AVIStreamWrite (AVIVideoStream, frame_count, 1, ae->lpVideo, ae->lpbi->biSizeImage, 0, NULL, &written)) != 0) {
			avi_message (_T("AVIStreamWrite() FAILED (%X)\n"), err);
			goto error;
		}

		frame_count++;
		total_avi_size += written;

	} else {

		avi_message (_T("DirectDraw_GetDC() FAILED\n"));
		goto error;

	}

	if ((frame_count % (fps_in_use * 10)) == 0)
		write_log (_T("AVIOutput: %d frames, (%d fps)\n"), frame_count, fps_in_use);
	return 1;

error:
	return 0;
}

static void writewavheader (uae_u32 size)
{
	uae_u16 tw;
	uae_u32 tl;
	int bits = 16;
	int channels = get_audio_nativechannels (currprefs.sound_stereo);

	fseek (wavfile, 0, SEEK_SET);
	fwrite ("RIFF", 1, 4, wavfile);
	tl = 0;
	if (size)
		tl = size - 8;
	fwrite (&tl, 1, 4, wavfile);
	fwrite ("WAVEfmt ", 1, 8, wavfile);
	tl = 16;
	fwrite (&tl, 1, 4, wavfile);
	tw = 1;
	fwrite (&tw, 1, 2, wavfile);
	tw = channels;
	fwrite (&tw, 1, 2, wavfile);
	tl = currprefs.sound_freq;
	fwrite (&tl, 1, 4, wavfile);
	tl = currprefs.sound_freq * channels * bits / 8;
	fwrite (&tl, 1, 4, wavfile);
	tw = channels * bits / 8;
	fwrite (&tw, 1, 2, wavfile);
	tw = bits;
	fwrite (&tw, 1, 2, wavfile);
	fwrite ("data", 1, 4, wavfile);
	tl = 0;
	if (size)
		tl = size - 44;
	fwrite (&tl, 1, 4, wavfile);
}

void AVIOutput_Restart(bool split)
{
	if (first_frame)
		return;
	avioutput_needs_restart = split ? 1 : -1;
}

static void AVIOutput_End2(bool fullrestart)
{
	if (alive) {
		write_log (_T("killing worker thread\n"));
		write_comm_pipe_u32 (&workindex, 0xfffffffe, 1);
		while (alive) {
			while (comm_pipe_has_data (&queuefull))
				read_comm_pipe_u32_blocking (&queuefull);
			Sleep (10);
		}
	}
	first_frame = 0;
	avioutput_enabled = 0;
	avioutput_failed = 0;
	freequeue ();
	destroy_comm_pipe (&workindex);
	destroy_comm_pipe (&queuefull);
	if (has) {
		acmStreamClose (has, 0);
		has = NULL;
	}

	if (AVIAudioStream) {
		AVIStreamRelease (AVIAudioStream);
		AVIAudioStream = NULL;
	}

	if (AVIVideoStream) {
		AVIStreamRelease (AVIVideoStream);
		AVIVideoStream = NULL;
	}

	if (AVIStreamInterface) {
		AVIStreamRelease (AVIStreamInterface);
		AVIStreamInterface = NULL;
	}

	if (pfile) {
		AVIFileRelease (pfile);
		pfile = NULL;
	}

	StreamSizeAudio = frame_count = 0;
	StreamSizeAudioExpected = 0;
	StreamSizeAudioGot = 0;
	if (fullrestart) {
		partcnt = 0;
		avi_sndbuffered = 0;
		xfree(avi_sndbuffer);
		avi_sndbuffer = NULL;
		avi_sndbufsize = 0;
	}

	if (wavfile) {
		writewavheader (ftell (wavfile));
		fclose (wavfile);
		wavfile = 0;
	}
}

void AVIOutput_End(void)
{
	AVIOutput_End2(true);
}

static void AVIOutput_worker (void *arg);

static void AVIOutput_Begin2(bool fullstart, bool immediate)
{
	AVISTREAMINFO avistreaminfo; // Structure containing information about the stream, including the stream type and its sample rate
	int i, err;
	TCHAR *ext1, *ext2;
	struct avientry *ae = NULL;

	AVIOutput_Initialize ();

	if (avioutput_enabled) {
		if (!avioutput_requested)
			AVIOutput_End ();
		return;
	}
	if (!avioutput_requested)
		return;

	aviout_monid = getfocusedmonitor();

	if (avioutput_audio == AVIAUDIO_WAV) {
		ext1 = _T(".wav"); ext2 = _T(".avi");
	} else {
		ext1 = _T(".avi"); ext2 = _T(".wav");
	}
	if (uaetcslen(avioutput_filename_inuse) >= 4 && !_tcsicmp (avioutput_filename_inuse + uaetcslen(avioutput_filename_inuse) - 4, ext2))
		avioutput_filename_inuse[uaetcslen(avioutput_filename_inuse) - 4] = 0;
	if (uaetcslen(avioutput_filename_inuse) >= 4 && _tcsicmp (avioutput_filename_inuse + uaetcslen(avioutput_filename_inuse) - 4, ext1))
		_tcscat (avioutput_filename_inuse, ext1);
	_tcscpy (avioutput_filename_tmp, avioutput_filename_inuse);
	i = uaetcslen(avioutput_filename_tmp) - 1;
	while (i > 0 && avioutput_filename_tmp[i] != '.') i--;
	if (i > 0)
		avioutput_filename_tmp[i] = 0;

	avioutput_needs_restart = 0;
	avioutput_enabled = avioutput_audio || avioutput_video;
	if (!avioutput_init || !avioutput_enabled) {
		write_log (_T("No video or audio enabled\n"));
		goto error;
	}

	if (fullstart) {
		reset_sound();
		first_frame = 0;
		if (savestate_state && !avioutput_originalsize)
			first_frame = 1;
	} else {
		first_frame = 0;
	}

	// delete any existing file before writing AVI
	SetFileAttributes (avioutput_filename_inuse, FILE_ATTRIBUTE_ARCHIVE);
	DeleteFile (avioutput_filename_inuse);

	if (avioutput_audio == AVIAUDIO_WAV) {
		wavfile = _tfopen (avioutput_filename_inuse, _T("wb"));
		if (!wavfile) {
			gui_message (_T("Failed to open wave-file\n\nThis can happen if the path and or file name was entered incorrectly.\n"));
			goto error;
		}
		writewavheader (0);
		write_log (_T("wave-output to '%s' started\n"), avioutput_filename_inuse);
		return;
	}

	if (!FileStream) {
		if (((err = AVIFileOpen (&pfile, avioutput_filename_inuse, OF_CREATE | OF_WRITE, NULL)) != 0)) {
			gui_message (_T("AVIFileOpen() FAILED (Error %X)\n\nThis can happen if the path and or file name was entered incorrectly.\nRequired *.avi extension.\n"), err);
			goto error;
		}
	}

	if (avioutput_audio) {
		if (!AVIOutput_AllocateAudio (immediate))
			goto error;
		memset (&avistreaminfo, 0, sizeof (AVISTREAMINFO));
		avistreaminfo.fccType = streamtypeAUDIO;
		avistreaminfo.fccHandler = 0; // This member is not used for audio streams.
		avistreaminfo.dwFlags = 0;
		//avistreaminfo.dwCaps =; // Capability flags; currently unused.
		//avistreaminfo.wPriority =;
		//avistreaminfo.wLanguage =;
		avistreaminfo.dwScale = pwfxDst->nBlockAlign;
		avistreaminfo.dwRate = pwfxDst->nAvgBytesPerSec;
		avistreaminfo.dwStart = 0;
		avistreaminfo.dwLength = -1;
		avistreaminfo.dwInitialFrames = 0;
		avistreaminfo.dwSuggestedBufferSize = 0; // Use zero if you do not know the correct buffer size.
		avistreaminfo.dwQuality = -1; // -1 default quality value
		avistreaminfo.dwSampleSize = pwfxDst->nBlockAlign;
		//avistreaminfo.rcFrame; // doesn't apply to audio
		//avistreaminfo.dwEditCount =; // Number of times the stream has been edited. The stream handler maintains this count.
		//avistreaminfo.dwFormatChangeCount =; // Number of times the stream format has changed. The stream handler maintains this count.
		_tcscpy (avistreaminfo.szName, _T("Audiostream")); // description of the stream.

		// create the audio stream
		if ((err = AVIFileCreateStream (pfile, &AVIAudioStream, &avistreaminfo)) != 0) {
			gui_message (_T("AVIFileCreateStream() FAILED (%X)\n"), err);
			goto error;
		}

		if ((err = AVIStreamSetFormat (AVIAudioStream, 0, pwfxDst, sizeof (WAVEFORMATEX) + pwfxDst->cbSize)) != 0) {
			gui_message (_T("AVIStreamSetFormat() FAILED (%X)\n"), err);
			goto error;
		}

		if ((err = acmStreamOpen(&has, NULL, &wfxSrc.Format, pwfxDst, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME)) != 0) {
			gui_message (_T("acmStreamOpen() FAILED (%X)\n"), err);
			goto error;
		}
		audio_validated = true;
	}

	if (avioutput_video) {
		int retrycnt = 0;
		while (retrycnt < 2) {
			avioutput_bitmode = retrycnt ? 32 : 24;
			if (usealpha()) {
				avioutput_bitmode = avioutput_bitmode == 32 ? 24 : 32;
			}
			retrycnt++;
			ae = allocavientry_video();
			if (!AVIOutput_AllocateVideo())
				goto error;

			// fill in the header for the video stream
			memset(&avistreaminfo, 0, sizeof(AVISTREAMINFO));
			avistreaminfo.fccType = streamtypeVIDEO; // stream type

			// unsure about this, as this is the uncompressed stream, not the compressed stream
			//avistreaminfo.fccHandler = 0;

			// incase the amiga changes palette
			if (ae->lpbi->biBitCount < 24)
				avistreaminfo.dwFlags = AVISTREAMINFO_FORMATCHANGES;
			//avistreaminfo.dwCaps =; // Capability flags; currently unused
			//avistreaminfo.wPriority =; // Priority of the stream
			//avistreaminfo.wLanguage =; // Language of the stream
			avistreaminfo.dwScale = 1;
			avistreaminfo.dwRate = avioutput_fps; // our playback speed default (PAL 50fps), (NTSC 60fps)
			avistreaminfo.dwStart = 0; // no delay
			avistreaminfo.dwLength = 1; // initial length
			//avistreaminfo.dwInitialFrames =; // audio only
			avistreaminfo.dwSuggestedBufferSize = ae->lpbi->biSizeImage;
			avistreaminfo.dwQuality = -1; // drivers will use the default quality setting
			avistreaminfo.dwSampleSize = 0; // variable video data samples

			SetRect(&avistreaminfo.rcFrame, 0, 0, ae->lpbi->biWidth, ae->lpbi->biHeight); // rectangle for stream

			//avistreaminfo.dwEditCount =; // Number of times the stream has been edited. The stream handler maintains this count.
			//avistreaminfo.dwFormatChangeCount =; // Number of times the stream format has changed. The stream handler maintains this count.
			_tcscpy(avistreaminfo.szName, _T("Videostream")); // description of the stream.

			// create the stream
			if ((err = AVIFileCreateStream(pfile, &AVIStreamInterface, &avistreaminfo)) != 0) {
				if (retrycnt < 2) {
					freeavientry(ae);
					continue;
				}
				gui_message(_T("AVIFileCreateStream() FAILED (%08X)\n"), err);
				goto error;
			}
			break;
		}

		videoOptions.fccType = streamtypeVIDEO;
		videoOptions.fccHandler = pcompvars->fccHandler;
		videoOptions.dwKeyFrameEvery = pcompvars->lKey;
		videoOptions.dwQuality = pcompvars->lQ;

		videoOptions.dwBytesPerSecond = pcompvars->lDataRate * 1024;
		videoOptions.dwFlags = AVICOMPRESSF_VALID | AVICOMPRESSF_KEYFRAMES | AVICOMPRESSF_INTERLEAVE | AVICOMPRESSF_DATARATE;

		videoOptions.dwInterleaveEvery = 1;

		videoOptions.cbFormat = sizeof(BITMAPINFOHEADER);
		videoOptions.lpFormat = lpbi;

		videoOptions.cbParms = pcompvars->cbState;
		videoOptions.lpParms = pcompvars->lpState;

		// create a compressed stream from our uncompressed stream and a compression filter
		if ((err = AVIMakeCompressedStream(&AVIVideoStream, AVIStreamInterface, &videoOptions, NULL)) != AVIERR_OK) {
			gui_message(_T("AVIMakeCompressedStream() FAILED (%X)\n"), err);
			goto error;
		}
	}
	freeavientry(ae);
	init_comm_pipe(&workindex, 20, 1);
	init_comm_pipe(&queuefull, 20, 1);
	avientryindex = -1;
	alive = -1;
	fps_in_use = avioutput_fps;
	uae_start_thread(_T("aviworker"), AVIOutput_worker, NULL, NULL);
	write_log(_T("AVIOutput enabled: monitor=%d video=%d audio=%d path='%s'\n"), aviout_monid, avioutput_video, avioutput_audio, avioutput_filename_inuse);
	return;

error:
	freeavientry(ae);
	AVIOutput_End();
}

void AVIOutput_Begin(bool immediate)
{
	AVIOutput_Begin2(true, immediate);
}

void AVIOutput_Release (void)
{
	AVIOutput_End ();

	AVIOutput_ReleaseAudio ();
	AVIOutput_ReleaseVideo ();

	if (avioutput_init) {
		AVIFileExit ();
		avioutput_init = 0;
	}

	AVIOutput_FreeAudioDstFormat();
	AVIOutput_FreeVideoDstFormat();
	xfree (pcompvars);
	pcompvars = NULL;

	if (cs_allocated) {
		DeleteCriticalSection (&AVIOutput_CriticalSection);
		cs_allocated = 0;
	}
}

void AVIOutput_Initialize (void)
{
	if (avioutput_init)
		return;

	InitializeCriticalSection (&AVIOutput_CriticalSection);
	cs_allocated = 1;

	pcompvars = xcalloc (COMPVARS, 1);
	if (!pcompvars)
		return;
	pcompvars->cbSize = sizeof (COMPVARS);

	AVIFileInit ();
	avioutput_init = 1;
	avientryindex = -1;
}


static void AVIOutput_worker (void *arg)
{
	bool quit = false;
	write_log (_T("AVIOutput worker thread started\n"));
	alive = 1;
	for (;;) {
		uae_u32 idx = read_comm_pipe_u32_blocking (&workindex);
		struct avientry *ae;
		int r1 = 1;
		int r2 = 1;
		if (idx == 0xffffffff)
			break;
		if (idx == 0xfffffffe)
			quit = true;
		for (;;) {
			EnterCriticalSection (&AVIOutput_CriticalSection);
			ae = getavientry ();
			LeaveCriticalSection (&AVIOutput_CriticalSection);
			if (ae == NULL)
				break;
			if (!quit) {
				write_comm_pipe_u32(&queuefull, 0, 1);
			}
			if (!avioutput_failed) {
				if (ae->lpAudio)
					r1 = AVIOutput_AVIWriteAudio_Thread (ae);
				if (ae->lpVideo)
					r2 = AVIOutput_AVIWriteVideo_Thread (ae);
				if (r1 == 0 || r2 == 0)
					avioutput_failed = 1;
			}
			freeavientry (ae);
		}
		if (idx == 0xffffffff || quit || avioutput_failed)
			break;
	}
	AVIOutput_AVIWriteAudio_Thread_End();
	write_log (_T("AVIOutput worker thread killed. quit=%d\n"), quit);
	alive = 0;
}

void AVIOutput_Toggle (int mode, bool immediate)
{
	if (mode < 0) {
		avioutput_requested = !avioutput_requested;
	} else {
		if (mode == avioutput_requested)
			return;
		avioutput_requested = mode;
	}

	if (!avioutput_requested)
		AVIOutput_End ();

	if (immediate && avioutput_requested) {
		TCHAR tmp[MAX_DPATH];
		_tcscpy (avioutput_filename_inuse, avioutput_filename_auto);
		if (!avioutput_filename_inuse[0]) {
			fetch_path (_T("VideoPath"), avioutput_filename_inuse, sizeof (avioutput_filename_inuse) / sizeof (TCHAR));
			_tcscat (avioutput_filename_inuse, _T("output.avi"));
		}

		for (;;) {
			int num = 0;
			TCHAR *ext = NULL;
			TCHAR *p = _tcsrchr (avioutput_filename_inuse, '.');
			if (p)
				ext = my_strdup (p);
			else
				p = avioutput_filename_inuse + _tcslen (avioutput_filename_inuse);
			p[0] = 0;
			if (p > avioutput_filename_inuse && p[-1] >= '0' && p[-1] <= '9') {
				p--;
				while (p > avioutput_filename_inuse && p[-1] >= '0' && p[-1] <= '9')
					p--;
				num = _tstol (p);
			} else {
				*p++ = '_';
				*p = 0;
			}
			num++;
			_stprintf (p, _T("%u"), num);
			if (ext)
				_tcscat (avioutput_filename_inuse, ext);
			xfree (ext);
			if (!my_existsfile (avioutput_filename_inuse))
				break;
		}

		if (avioutput_audio != AVIAUDIO_WAV) {
			avioutput_audio = AVIOutput_GetAudioCodec (tmp, sizeof tmp / sizeof (TCHAR));
			avioutput_video = AVIOutput_GetVideoCodec (tmp, sizeof tmp / sizeof (TCHAR));
		}
		AVIOutput_Begin (immediate);
	} else if (!immediate && avioutput_requested) {
		_tcscpy (avioutput_filename_inuse, avioutput_filename_gui);
	}
}

static void start_if_requested(void)
{
	if (!avioutput_requested)
		return;
	AVIOutput_Begin(true);
}

static bool can_record(void)
{
	if (savestate_state == STATE_DORESTORE || savestate_state == STATE_RESTORE)
		return false;
	return true;
}

bool AVIOutput_WriteAudio(uae_u8 *sndbuffer, int sndbufsize)
{
	if (!can_record())
		return false;

	start_if_requested();

	if (!avioutput_audio || !avioutput_enabled)
		return false;
	if (!sndbufsize)
		return false;
	if (avioutput_failed)
		return false;

	if (!avi_sndbuffer) {
		avi_sndbufsize = sndbufsize * 2;
		avi_sndbuffer = xcalloc(uae_u8, avi_sndbufsize);
		avi_sndbuffered = 0;
	} 
	while (avi_sndbuffered + sndbufsize > avi_sndbufsize) {
		avi_sndbufsize *= 2;
		avi_sndbuffer = xrealloc(uae_u8, avi_sndbuffer, avi_sndbufsize);
	}
	memcpy(avi_sndbuffer + avi_sndbuffered, sndbuffer, sndbufsize);
	avi_sndbuffered += sndbufsize;
	return true;
}

bool frame_drawn(int monid)
{
	if (monid != aviout_monid)
		return false;
	if (!can_record())
		return false;

	start_if_requested();

	if (screenshot_multi) {
		screenshot(monid, 1, 1);
		if (screenshot_multi > 0)
			screenshot_multi--;
	}

	if (!avioutput_enabled)
		return false;
	if (avioutput_failed)
		return false;

	if (avioutput_audio == AVIAUDIO_WAV) {
		finish_sound_buffer();
		if (!first_frame) {
			AVIOuput_WAVWriteAudio(avi_sndbuffer, avi_sndbuffered);
		}
		first_frame = 0;
		avi_sndbuffered = 0;
		return true;
	}

	if (avioutput_audio) {
		finish_sound_buffer();
		if (first_frame == 0 || first_frame == -1) {
			int bytesperframe;
			bytesperframe = wfxSrc.Format.nChannels * 2;
			StreamSizeAudioGot += avi_sndbuffered / bytesperframe;
			unsigned int lastexpected = (unsigned int)StreamSizeAudioExpected;
			StreamSizeAudioExpected += ((float)wfxSrc.Format.nSamplesPerSec) / fps_in_use;
			if (avioutput_video) {
				int idiff = (int)(StreamSizeAudioGot - StreamSizeAudioExpected);
				if ((vsync_counter % 5) == 0)
					write_log(_T("%.1f %.1f %d\n"), StreamSizeAudioExpected, StreamSizeAudioGot, idiff);
				if (idiff) {
					StreamSizeAudioGot = StreamSizeAudioExpected;
					AVIOuput_AVIWriteAudio(avi_sndbuffer, avi_sndbuffered, (int)(StreamSizeAudioExpected - lastexpected) * bytesperframe);
				} else {
					AVIOuput_AVIWriteAudio(avi_sndbuffer, avi_sndbuffered, 0);
				}
			} else {
				AVIOuput_AVIWriteAudio(avi_sndbuffer, avi_sndbuffered, 0);
			}
		}
		avi_sndbuffered = 0;
	}

	if (first_frame) {
		if (first_frame > 0)
			first_frame--;
		else
			first_frame++;
		return true;
	}

	if (!avioutput_video)
		return false;

	AVIOutput_WriteVideo();
	return true;
}

/* Resampler from:
 * Digital Audio Resampling Home Page located at
 * http://ccrma.stanford.edu/~jos/resample/.
 */

#define Nhc       8
#define Na        7
#define Np       (Nhc+Na)
#define Npc      (1<<Nhc)
#define Amask    ((1<<Na)-1)
#define Pmask    ((1<<Np)-1)
#define Nh       16
#define Nb       16
#define Nhxn     14
#define Nhg      (Nh-Nhxn)
#define NLpScl   13

#define MAX_HWORD (32767)
#define MIN_HWORD (-32768)

STATIC_INLINE HWORD WordToHword(LWORD v, int scl)
{
	HWORD out;
	LWORD llsb = (1 << (scl - 1));
	v += llsb;          /* round */
	v >>= scl;
	if (v > MAX_HWORD) {
		v = MAX_HWORD;
	} else if (v < MIN_HWORD) {
		v = MIN_HWORD;
	}
	out = (HWORD)v;
	return out;
}

static int SrcLinear(HWORD X[], HWORD Y[], float factor, ULWORD *Time, UHWORD Nx)
{
	HWORD iconst;
	HWORD *Xp, *Ystart;
	LWORD v, x1, x2;

	float dt;                  /* Step through input signal */
	ULWORD dtb;                  /* Fixed-point version of Dt */
	ULWORD endTime;              /* When Time reaches EndTime, return to user */

	dt = 1.0f / factor;            /* Output sampling period */
	dtb = (ULWORD)(dt*(1 << Np) + 0.5);     /* Fixed-point representation */

	Ystart = Y;
	endTime = *Time + (1 << Np)*(LWORD)Nx;
	while (*Time < endTime) {
		iconst = (*Time) & Pmask;
		Xp = &X[(*Time) >> Np];      /* Ptr to current input sample */
		x1 = *Xp++;
		x2 = *Xp;
		x1 *= ((1 << Np) - iconst);
		x2 *= iconst;
		v = x1 + x2;
		*Y++ = WordToHword(v, Np);   /* Deposit output */
		*Time += dtb;               /* Move to next sample by time increment */
	}
	return (int)(Y - Ystart);            /* Return number of output samples */
}

#define IBUFFSIZE 4096                         /* Input buffer size */
#define OBUFFSIZE 8192                         /* Output buffer size */

static int resampleFast(  /* number of output samples returned */
	float factor,              /* factor = Sndout/Sndin */
	HWORD *in,                   /* input and output file descriptors */
	HWORD *out,
	int inCount,                /* number of input samples to convert */
	int outCount,               /* number of output samples to compute */
	int nChans)                 /* number of sound channels (1 or 2) */
{
	ULWORD Time, Time2;          /* Current time/pos in input sample */
	UHWORD Xp, Ncreep, Xoff, Xread;
	HWORD X1[IBUFFSIZE], Y1[OBUFFSIZE]; /* I/O buffers */
	HWORD X2[IBUFFSIZE], Y2[OBUFFSIZE]; /* I/O buffers */
	UHWORD Nout, Nx;
	int i, Ycount, last;
	int inseekpos = 0;
	int outseekpos = 0;

	Xoff = 10;

	Nx = IBUFFSIZE - 2 * Xoff;     /* # of samples to process each iteration */
	last = 0;                   /* Have not read last input sample yet */
	Ycount = 0;                 /* Current sample and length of output file */

	Xp = Xoff;                  /* Current "now"-sample pointer for input */
	Xread = Xoff;               /* Position in input array to read into */
	Time = (Xoff << Np);          /* Current-time pointer for converter */

	for (i = 0; i<Xoff; X1[i++] = 0); /* Need Xoff zeros at begining of sample */
	for (i = 0; i<Xoff; X2[i++] = 0); /* Need Xoff zeros at begining of sample */

	do {
		if (!last)              /* If haven't read last sample yet */
		{
			int idx = Xread;
			while (inCount > 0 && idx < IBUFFSIZE) {
				if (nChans == 2) {
					X1[idx] = in[inseekpos * 2 + 0];
					X2[idx] = in[inseekpos * 2 + 1];
				} else {
					X1[idx] = in[inseekpos];
				}
				inCount--;
				idx++;
				inseekpos++;
			}
			if (inCount == 0)
				last = idx;
			if (last && (last - Xoff<Nx)) { /* If last sample has been read... */
				Nx = last - Xoff; /* ...calc last sample affected by filter */
				if (Nx <= 0)
					break;
			}
		}

		/* Resample stuff in input buffer */
		Time2 = Time;
		Nout = SrcLinear(X1, Y1, factor, &Time, Nx);
		if (nChans == 2)
			Nout = SrcLinear(X2, Y2, factor, &Time2, Nx);

		Time -= (Nx << Np);       /* Move converter Nx samples back in time */
		Xp += Nx;               /* Advance by number of samples processed */
		Ncreep = (Time >> Np) - Xoff; /* Calc time accumulation in Time */
		if (Ncreep) {
			Time -= (Ncreep << Np);    /* Remove time accumulation */
			Xp += Ncreep;            /* and add it to read pointer */
		}
		for (i = 0; i<IBUFFSIZE - Xp + Xoff; i++) { /* Copy part of input signal */
			X1[i] = X1[i + Xp - Xoff]; /* that must be re-used */
			if (nChans == 2)
				X2[i] = X2[i + Xp - Xoff]; /* that must be re-used */
		}
		if (last) {             /* If near end of sample... */
			last -= Xp;         /* ...keep track were it ends */
			if (!last)          /* Lengthen input by 1 sample if... */
				last++;           /* ...needed to keep flag TRUE */
		}
		Xread = i;              /* Pos in input buff to read new data into */
		Xp = Xoff;

		Ycount += Nout;
		if (Ycount>outCount) {
			Nout -= (Ycount - outCount);
			Ycount = outCount;
		}

		if (Nout > OBUFFSIZE) /* Check to see if output buff overflowed */
			return -1;

		if (nChans == 1) {
			for (i = 0; i < Nout; i++) {
				out[outseekpos] = Y1[i];
				outseekpos++;
			}
		} else {
			for (i = 0; i < Nout; i++) {
				out[outseekpos * 2 + 0] = Y1[i];
				out[outseekpos * 2 + 1] = Y2[i];
				outseekpos++;
			}
		}

	} while (Ycount<outCount); /* Continue until done */

	return(Ycount);             /* Return # of samples in output file */
}
