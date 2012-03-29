/*
UAE - The Ultimate Amiga Emulator

avioutput.c

Copyright(c) 2001 - 2002; §ane
2005-2006; Toni Wilen

*/

#include <windows.h>

#include <ddraw.h>

#include <mmsystem.h>
#include <vfw.h>
#include <msacm.h>
#include <ks.h>
#include <ksmedia.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "resource"

#include "options.h"
#include "audio.h"
#include "custom.h"
#include "memory.h"
#include "newcpu.h"
#include "picasso96.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "opengl.h"
#include "sound.h"
#include "gfxfilter.h"
#include "xwin.h"
#include "avioutput.h"
#include "registry.h"
#include "threaddep/thread.h"

#define MAX_AVI_SIZE (0x80000000 - 0x1000000)

static smp_comm_pipe workindex;
static smp_comm_pipe queuefull;
static volatile int alive;
static volatile int avioutput_failed;

static int avioutput_init = 0;
static int actual_width = 320, actual_height = 256;
static int avioutput_needs_restart;

static int frame_start; // start frame
static int frame_count; // current frame
static int frame_skip;
static unsigned int total_avi_size;
static int partcnt;
static int first_frame = 1;

int avioutput_audio, avioutput_video, avioutput_enabled, avioutput_requested;
static int videoallocated;

int avioutput_width, avioutput_height, avioutput_bits;
int avioutput_fps = VBLANK_HZ_PAL;
int avioutput_framelimiter = 0, avioutput_nosoundoutput = 0;
int avioutput_nosoundsync = 1, avioutput_originalsize = 0;

TCHAR avioutput_filename[MAX_DPATH];
static TCHAR avioutput_filename_tmp[MAX_DPATH];

extern struct uae_prefs workprefs;
extern TCHAR config_filename[256];

static CRITICAL_SECTION AVIOutput_CriticalSection;
static int cs_allocated;

static PAVIFILE pfile = NULL; // handle of our AVI file
static PAVISTREAM AVIStreamInterface = NULL; // Address of stream interface

struct avientry {
	uae_u8 *lpVideo;
	LPBITMAPINFOHEADER lpbi;
	uae_u8 *lpAudio;
	int sndsize;
};

#define AVIENTRY_MAX 10
static int avientryindex;
static struct avientry *avientries[AVIENTRY_MAX + 1];

/* audio */

static unsigned int StreamSizeAudio; // audio write position
static double StreamSizeAudioExpected;
static PAVISTREAM AVIAudioStream = NULL; // compressed stream pointer
static HACMSTREAM has = NULL; // stream handle that can be used to perform conversions
static ACMSTREAMHEADER ash;
static ACMFORMATCHOOSE acmopt;
static WAVEFORMATEXTENSIBLE wfxSrc; // source audio format
static LPWAVEFORMATEX pwfxDst = NULL; // pointer to destination audio format
static DWORD wfxMaxFmtSize;
static FILE *wavfile;

/* video */

static PAVISTREAM AVIVideoStream = NULL; // compressed stream pointer
static AVICOMPRESSOPTIONS videoOptions;
static AVICOMPRESSOPTIONS FAR * aOptions[] = { &videoOptions }; // array of pointers to AVICOMPRESSOPTIONS structures
static LPBITMAPINFOHEADER lpbi;
static PCOMPVARS pcompvars;


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

static struct avientry *allocavientry_audio (uae_u8 *snd, int size)
{
	struct avientry *ae = xcalloc (struct avientry, 1);
	ae->lpAudio = xmalloc (uae_u8, size);
	memcpy (ae->lpAudio, snd, size);
	ae->sndsize = size;
	return ae;
}

static struct avientry *allocavientry_video (void)
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
	while ((ae = getavientry ()))
		freeavientry (ae);
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
	regsetint (avikey, _T("FPS"), avioutput_fps);
}
static void getsettings (UAEREG *avikey)
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
}

void AVIOutput_GetSettings (void)
{
	UAEREG *avikey = openavikey ();
	if (avikey)
		getsettings (avikey);
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
	if (pwfxDst) {
		xfree (pwfxDst);
		pwfxDst = NULL;
	}
}

static int AVIOutput_AudioAllocated (void)
{
	return pwfxDst ? 1 : 0;
}

static int AVIOutput_AllocateAudio (void)
{
	MMRESULT err;

	AVIOutput_ReleaseAudio ();

	if ((err = acmMetrics (NULL, ACM_METRIC_MAX_SIZE_FORMAT, &wfxMaxFmtSize))) {
		gui_message (_T("acmMetrics() FAILED (%X)\n"), err);
		return 0;
	}

	// set the source format
	memset (&wfxSrc, 0, sizeof (wfxSrc));
	wfxSrc.Format.wFormatTag = WAVE_FORMAT_PCM;
	wfxSrc.Format.nChannels = get_audio_nativechannels (currprefs.sound_stereo) ? get_audio_nativechannels (currprefs.sound_stereo) : 2;
	wfxSrc.Format.nSamplesPerSec = workprefs.sound_freq ? workprefs.sound_freq : 44100;
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

	if (!(pwfxDst = (LPWAVEFORMATEX)xmalloc (uae_u8, wfxMaxFmtSize)))
		return 0;

	// set the initial destination format to match source
	memset (pwfxDst, 0, wfxMaxFmtSize);
	memcpy (pwfxDst, &wfxSrc, sizeof (WAVEFORMATEX));
	pwfxDst->cbSize = (WORD) (wfxMaxFmtSize - sizeof (WAVEFORMATEX)); // shrugs

	memset(&acmopt, 0, sizeof (ACMFORMATCHOOSE));
	acmopt.cbStruct = sizeof (ACMFORMATCHOOSE);
	acmopt.fdwStyle = ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT;
	acmopt.pwfx = pwfxDst;
	acmopt.cbwfx = wfxMaxFmtSize;
	acmopt.pszTitle  = _T("Choose Audio Codec");

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
	return 1;
}

static int AVIOutput_ValidateAudio (WAVEFORMATEX *wft, TCHAR *name, int len)
{
	DWORD ret;
	ACMFORMATTAGDETAILS aftd;
	ACMFORMATDETAILS afd;

	memset(&aftd, 0, sizeof (ACMFORMATTAGDETAILS));
	aftd.cbStruct = sizeof (ACMFORMATTAGDETAILS);
	aftd.dwFormatTag = wft->wFormatTag;
	ret = acmFormatTagDetails (NULL, &aftd, ACM_FORMATTAGDETAILSF_FORMATTAG);
	if (ret)
		return 0;

	memset (&afd, 0, sizeof (ACMFORMATDETAILS));
	afd.cbStruct = sizeof (ACMFORMATDETAILS);
	afd.dwFormatTag = wft->wFormatTag;
	afd.pwfx = wft;
	afd.cbwfx = sizeof (WAVEFORMATEX) + wft->cbSize;
	ret = acmFormatDetails (NULL, &afd, ACM_FORMATDETAILSF_FORMAT);
	if (ret)
		return 0;

	if (name)
		_stprintf (name, _T("%s %s"), aftd.szFormatTag, afd.szFormat);
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
	getsettings (avikey);
	if (wft) {
		ss = wfxMaxFmtSize;
		if (regquerydata (avikey, _T("AudioConfigurationVars"), wft, &ss)) {
			if (AVIOutput_ValidateAudio (wft, NULL, 0))
				ok = 1;
		}
	}
	if (!ok)
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
	if (!AVIOutput_AllocateAudio ())
		return 0;
	if (AVIOutput_GetAudioFromRegistry (pwfxDst)) {
		AVIOutput_GetAudioCodecName (pwfxDst, name, len);
		return 1;
	}
	AVIOutput_ReleaseAudio ();
	return 0;
}

int AVIOutput_ChooseAudioCodec (HWND hwnd, TCHAR *s, int len)
{
	AVIOutput_Initialize ();
	AVIOutput_End();
	if (!AVIOutput_AllocateAudio ())
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

static int AVIOutput_VideoAllocated (void)
{
	return videoallocated ? 1 : 0;
}

void AVIOutput_ReleaseVideo (void)
{
	videoallocated = 0;
	freequeue ();
	xfree (lpbi);
	lpbi = NULL;
}

static int AVIOutput_AllocateVideo (void)
{
	avioutput_width = avioutput_height = avioutput_bits = 0;

	avioutput_fps = (int)(vblank_hz + 0.5);
	if (!avioutput_fps)
		avioutput_fps = ispal () ? 50 : 60;
	if (avioutput_originalsize)
		getfilterbuffer (&avioutput_width, &avioutput_height, NULL, &avioutput_bits);

	if (avioutput_width == 0 || avioutput_height == 0 || avioutput_bits == 0) {
		avioutput_width = WIN32GFX_GetWidth ();
		avioutput_height = WIN32GFX_GetHeight ();
		avioutput_bits = WIN32GFX_GetDepth (0);
	}

	AVIOutput_Initialize ();
	AVIOutput_ReleaseVideo ();
	if (avioutput_width == 0 || avioutput_height == 0) {
		avioutput_width = workprefs.gfx_size.width;
		avioutput_height = workprefs.gfx_size.height;
		avioutput_bits = WIN32GFX_GetDepth (0);
	}
	if (avioutput_bits == 0)
		avioutput_bits = 24;
	if (avioutput_bits > 24)
		avioutput_bits = 24;
	lpbi = (LPBITMAPINFOHEADER)xcalloc (uae_u8, lpbisize ());
	lpbi->biSize = sizeof (BITMAPINFOHEADER);
	lpbi->biWidth = avioutput_width;
	lpbi->biHeight = avioutput_height;
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
static void AVIOutput_FreeCOMPVARS (COMPVARS *pcv)
{
	ICClose(pcv->hic);
	if (compressorallocated)
		ICCompressorFree(pcv);
	compressorallocated = FALSE;
	pcv->hic = NULL;
}

static int AVIOutput_GetCOMPVARSFromRegistry (COMPVARS *pcv)
{
	UAEREG *avikey;
	int ss;
	int ok = 0;

	avikey = openavikey ();
	if (!avikey)
		return 0;
	getsettings (avikey);
	if (pcv) {
		ss = pcv->cbSize;
		pcv->hic = 0;
		if (regquerydata (avikey, _T("VideoConfigurationVars"), pcv, &ss)) {
			pcv->hic = 0;
			pcv->lpbiIn = pcv->lpbiOut = 0;
			pcv->cbState = 0;
			if (regquerydatasize (avikey, _T("VideoConfigurationState"), &ss)) {
				if (ss > 0) {
					LPBYTE state = xmalloc (BYTE, ss);
					if (regquerydata (avikey, _T("VideoConfigurationState"), state, &ss)) {
						pcv->hic = ICOpen (pcv->fccType, pcv->fccHandler, ICMODE_COMPRESS);
						if (pcv->hic) {
							ok = 1;
							ICSetState (pcv->hic, state, ss);
						}
					}
					xfree (state);
				} else {
					ok = 1;
				}
			}
		}
	}
	if (!ok) {
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

int AVIOutput_GetVideoCodec (TCHAR *name, int len)
{
	AVIOutput_Initialize ();

	if (AVIOutput_VideoAllocated ())
		return AVIOutput_GetVideoCodecName (pcompvars, name, len);
	if (!AVIOutput_AllocateVideo ())
		return 0;
	AVIOutput_FreeCOMPVARS (pcompvars);
	if (AVIOutput_GetCOMPVARSFromRegistry (pcompvars)) {
		AVIOutput_GetVideoCodecName (pcompvars, name, len);
		return 1;
	}
	AVIOutput_ReleaseVideo ();
	return 0;
}

int AVIOutput_ChooseVideoCodec (HWND hwnd, TCHAR *s, int len)
{
	AVIOutput_Initialize ();

	AVIOutput_End ();
	if (!AVIOutput_AllocateVideo ())
		return 0;
	AVIOutput_FreeCOMPVARS (pcompvars);

	// we really should check first to see if the user has a particular compressor installed before we set one
	// we could set one but we will leave it up to the operating system and the set priority levels for the compressors

	// default
	//pcompvars->fccHandler = mmioFOURCC('C','V','I','D'); // "Cinepak Codec by Radius"
	//pcompvars->fccHandler = mmioFOURCC('M','R','L','E'); // "Microsoft RLE"
	//pcompvars->fccHandler = mmioFOURCC('D','I','B',' '); // "Full Frames (Uncompressed)"

	pcompvars->lQ = 10000; // 10000 is maximum quality setting or ICQUALITY_DEFAULT for default
	pcompvars->lKey = avioutput_fps; // default to one key frame per second, every (FPS) frames
	pcompvars->dwFlags = 0;
	if (ICCompressorChoose (hwnd, ICMF_CHOOSE_DATARATE | ICMF_CHOOSE_KEYFRAME, lpbi, NULL, pcompvars, "Choose Video Codec") == TRUE) {
		UAEREG *avikey;
		int ss;
		uae_u8 *state;

		compressorallocated = TRUE;
		ss = ICGetState (pcompvars->hic, NULL, 0);
		if (ss > 0) {
			DWORD err;
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
			state = xmalloc (uae_u8, 1);
		avikey = openavikey ();
		if (avikey) {
			regsetdata (avikey, _T("VideoConfigurationState"), state, ss);
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

static void checkAVIsize (int force)
{
	int tmp_partcnt = partcnt + 1;
	int tmp_avioutput_video = avioutput_video;
	int tmp_avioutput_audio = avioutput_audio;
	TCHAR fn[MAX_DPATH];

	if (!force && total_avi_size < MAX_AVI_SIZE)
		return;
	if (total_avi_size == 0)
		return;
	_tcscpy (fn, avioutput_filename_tmp);
	_stprintf (avioutput_filename, _T("%s_%d.avi"), fn, tmp_partcnt);
	write_log (_T("AVI split %d at %d bytes, %d frames\n"),
		tmp_partcnt, total_avi_size, frame_count);
	AVIOutput_End ();
	first_frame = 0;
	total_avi_size = 0;
	avioutput_video = tmp_avioutput_video;
	avioutput_audio = tmp_avioutput_audio;
	AVIOutput_Begin ();
	_tcscpy (avioutput_filename_tmp, fn);
	partcnt = tmp_partcnt;
}

static void dorestart (void)
{
	write_log (_T("AVIOutput: parameters changed, restarting..\n"));
	avioutput_needs_restart = 0;
	checkAVIsize (1);
}

static void AVIOuput_AVIWriteAudio (uae_u8 *sndbuffer, int sndbufsize)
{
	struct avientry *ae;

	if (avioutput_failed) {
		AVIOutput_End ();
		return;
	}
	checkAVIsize (0);
	if (avioutput_needs_restart)
		dorestart ();
	waitqueuefull ();
	ae = allocavientry_audio (sndbuffer, sndbufsize);
	queueavientry (ae);
}

static int AVIOutput_AVIWriteAudio_Thread (struct avientry *ae)
{
	DWORD dwOutputBytes = 0;
	LONG written = 0, swritten = 0;
	unsigned int err;
	uae_u8 *lpAudio = NULL;

	if (avioutput_audio) {
		if (!avioutput_init)
			goto error;

		if ((err = acmStreamSize (has, ae->sndsize, &dwOutputBytes, ACM_STREAMSIZEF_SOURCE) != 0)) {
			gui_message (_T("acmStreamSize() FAILED (%X)\n"), err);
			goto error;
		}

		if (!(lpAudio = xmalloc (uae_u8, dwOutputBytes)))
			goto error;

		ash.cbStruct = sizeof (ACMSTREAMHEADER);
		ash.fdwStatus = 0;
		ash.dwUser = 0;

		// source
		ash.pbSrc = ae->lpAudio;

		ash.cbSrcLength = ae->sndsize;
		ash.cbSrcLengthUsed = 0; // This member is not valid until the conversion is complete.

		ash.dwSrcUser = 0;

		// destination
		ash.pbDst = lpAudio;

		ash.cbDstLength = dwOutputBytes;
		ash.cbDstLengthUsed = 0; // This member is not valid until the conversion is complete.

		ash.dwDstUser = 0;

		if ((err = acmStreamPrepareHeader (has, &ash, 0))) {
			avi_message (_T("acmStreamPrepareHeader() FAILED (%X)\n"), err);
			goto error;
		}

		if ((err = acmStreamConvert (has, &ash, ACM_STREAMCONVERTF_BLOCKALIGN))) {
			avi_message (_T("acmStreamConvert() FAILED (%X)\n"), err);
			goto error;
		}

		if ((err = AVIStreamWrite (AVIAudioStream, StreamSizeAudio, ash.cbDstLengthUsed / pwfxDst->nBlockAlign, lpAudio, ash.cbDstLengthUsed, 0, &swritten, &written)) != 0) {
			avi_message (_T("AVIStreamWrite() FAILED (%X)\n"), err);
			goto error;
		}

		StreamSizeAudio += swritten;
		total_avi_size += written;

		acmStreamUnprepareHeader (has, &ash, 0);

		free(lpAudio);
		lpAudio = NULL;
	}

	return 1;

error:
	xfree (lpAudio);
	return 0;
}

static void AVIOuput_WAVWriteAudio (uae_u8 *sndbuffer, int sndbufsize)
{
	fwrite (sndbuffer, 1, sndbufsize, wavfile);
}

static int skipsample;

void AVIOutput_WriteAudio (uae_u8 *sndbuffer, int sndbufsize)
{
	int size = sndbufsize;

	if (!avioutput_audio || !avioutput_enabled)
		return;
	if (skipsample > 0 && size > wfxSrc.Format.nBlockAlign) {
		size -= wfxSrc.Format.nBlockAlign;
		skipsample--;
	}
	if (avioutput_audio == AVIAUDIO_WAV)
		AVIOuput_WAVWriteAudio (sndbuffer, size);
	else
		AVIOuput_AVIWriteAudio (sndbuffer, size);
}

static int getFromDC (struct avientry *avie)
{
	HDC hdc;
	HBITMAP hbitmap = NULL;
	HBITMAP hbitmapOld = NULL;
	HDC hdcMem = NULL;
	int ok = 1;

	hdc = gethdc ();
	if (!hdc)
		return 0;
	// create a memory device context compatible with the application's current screen
	hdcMem = CreateCompatibleDC (hdc);
	hbitmap = CreateCompatibleBitmap (hdc, avioutput_width, avioutput_height);
	hbitmapOld = (HBITMAP)SelectObject (hdcMem, hbitmap);
	// probably not the best idea to use slow GDI functions for this,
	// locking the surfaces and copying them by hand would be more efficient perhaps
	// draw centered in frame
	BitBlt (hdcMem, (avioutput_width / 2) - (actual_width / 2), (avioutput_height / 2) - (actual_height / 2), actual_width, actual_height, hdc, 0, 0, SRCCOPY);
	SelectObject (hdcMem, hbitmapOld);
	if (GetDIBits (hdc, hbitmap, 0, avioutput_height, avie->lpVideo, (LPBITMAPINFO)lpbi, DIB_RGB_COLORS) == 0) {
		gui_message (_T("GetDIBits() FAILED (%X)\n"), GetLastError ());
		ok = 0;
	}
	DeleteObject (hbitmap);
	DeleteDC (hdcMem);
	releasehdc (hdc);
	return ok;
}

static int rgb_type;

void AVIOutput_RGBinfo (int rb, int gb, int bb, int rs, int gs, int bs)
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
extern uae_u8 *bufmem_ptr;

static int getFromBuffer (struct avientry *ae, int original)
{
	int x, y, w, h, d;
	uae_u8 *src;
	uae_u8 *dst = ae->lpVideo;
	int spitch, dpitch;

	dpitch = ((avioutput_width * avioutput_bits + 31) & ~31) / 8;
	if (original) {
		src = getfilterbuffer (&w, &h, &spitch, &d);
	} else {
		spitch = gfxvidinfo.outbuffer->rowbytes;
		src = bufmem_ptr;
	}
	if (!src)
		return 0;
	dst += dpitch * avioutput_height;
	for (y = 0; y < (gfxvidinfo.outbuffer->outheight > avioutput_height ? avioutput_height : gfxvidinfo.outbuffer->outheight); y++) {
		uae_u8 *d;
		dst -= dpitch;
		d = dst;
		for (x = 0; x < (gfxvidinfo.outbuffer->outwidth > avioutput_width ? avioutput_width : gfxvidinfo.outbuffer->outwidth); x++) {
			if (avioutput_bits == 8) {
				*d++ = src[x];
			} else if (avioutput_bits == 16) {
				uae_u16 v = ((uae_u16*)src)[x];
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
				uae_u32 v = ((uae_u32*)src)[x];
				((uae_u32*)d)[0] = v;
				d += 4;
			} else if (avioutput_bits == 24) {
				uae_u32 v = ((uae_u32*)src)[x];
				*d++ = v;
				*d++ = v >> 8;
				*d++ = v >> 16;
			}
		}
		src += spitch;
	}
	return 1;
}
#endif

void AVIOutput_WriteVideo (void)
{
	struct avientry *ae;
	int v;

	if (avioutput_failed) {
		AVIOutput_End ();
		return;
	}

	checkAVIsize (0);
	if (avioutput_needs_restart)
		dorestart ();
	waitqueuefull ();
	ae = allocavientry_video ();
	if (avioutput_originalsize && !WIN32GFX_IsPicassoScreen ()) {
		v = getFromBuffer (ae, 1);
	} else {
#if defined (GFXFILTER)
		if (!usedfilter || WIN32GFX_IsPicassoScreen ())
			v = getFromDC (ae);
		else
			v = getFromBuffer (ae, 0);
#else
		v = getFromDC (avie);
#endif
	}
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

		actual_width = gfxvidinfo.outbuffer->outwidth;
		actual_height = gfxvidinfo.outbuffer->outheight;

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

	if ((frame_count % (avioutput_fps * 10)) == 0)
		write_log (_T("AVIOutput: %d frames, (%d fps)\n"), frame_count, avioutput_fps);
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

void AVIOutput_Restart (void)
{
	if (first_frame)
		return;
	avioutput_needs_restart = 1;
}

void AVIOutput_End (void)
{
	first_frame = 1;
	avioutput_enabled = 0;

	if (alive) {
		write_log (_T("killing worker thread\n"));
		write_comm_pipe_u32 (&workindex, 0xfffffffe, 1);
		while (alive) {
			while (comm_pipe_has_data (&queuefull))
				read_comm_pipe_u32_blocking (&queuefull);
			Sleep (10);
		}
	}
	avioutput_failed = 0;
	freequeue ();
	destroy_comm_pipe (&workindex);
	destroy_comm_pipe (&queuefull);
	if (has) {
		acmStreamUnprepareHeader (has, &ash, 0);
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
	partcnt = 0;

	if (wavfile) {
		writewavheader (ftell (wavfile));
		fclose (wavfile);
		wavfile = 0;
	}
}

static void *AVIOutput_worker (void *arg);

void AVIOutput_Begin (void)
{
	AVISTREAMINFO avistreaminfo; // Structure containing information about the stream, including the stream type and its sample rate
	int i, err;
	TCHAR *ext1, *ext2;
	struct avientry *ae = NULL;

	AVIOutput_Initialize ();

	avientryindex = -1;
	if (avioutput_enabled) {
		if (!avioutput_requested)
			AVIOutput_End ();
		return;
	}
	if (!avioutput_requested)
		return;

	changed_prefs.sound_auto = currprefs.sound_auto = 0;
	reset_sound ();

	if (avioutput_audio == AVIAUDIO_WAV) {
		ext1 = _T(".wav"); ext2 = _T(".avi");
	} else {
		ext1 = _T(".avi"); ext2 = _T(".wav");
	}
	if (_tcslen (avioutput_filename) >= 4 && !_tcsicmp (avioutput_filename + _tcslen (avioutput_filename) - 4, ext2))
		avioutput_filename[_tcslen (avioutput_filename) - 4] = 0;
	if (_tcslen (avioutput_filename) >= 4 && _tcsicmp (avioutput_filename + _tcslen (avioutput_filename) - 4, ext1))
		_tcscat (avioutput_filename, ext1);
	_tcscpy (avioutput_filename_tmp, avioutput_filename);
	i = _tcslen (avioutput_filename_tmp) - 1;
	while (i > 0 && avioutput_filename_tmp[i] != '.') i--;
	if (i > 0)
		avioutput_filename_tmp[i] = 0;

	avioutput_needs_restart = 0;
	avioutput_enabled = avioutput_audio || avioutput_video;
	if (!avioutput_init || !avioutput_enabled)
		goto error;

	// delete any existing file before writing AVI
	SetFileAttributes (avioutput_filename, FILE_ATTRIBUTE_ARCHIVE);
	DeleteFile (avioutput_filename);

	if (avioutput_audio == AVIAUDIO_WAV) {
		wavfile = _tfopen (avioutput_filename, _T("wb"));
		if (!wavfile) {
			gui_message (_T("Failed to open wave-file\n\nThis can happen if the path and or file name was entered incorrectly.\n"));
			goto error;
		}
		writewavheader (0);
		write_log (_T("wave-output to '%s' started\n"), avioutput_filename);
		return;
	}

	if (((err = AVIFileOpen (&pfile, avioutput_filename, OF_CREATE | OF_WRITE, NULL)) != 0)) {
		gui_message (_T("AVIFileOpen() FAILED (Error %X)\n\nThis can happen if the path and or file name was entered incorrectly.\nRequired *.avi extension.\n"), err);
		goto error;
	}

	if (avioutput_audio) {
		if (!AVIOutput_AllocateAudio ())
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
	}

	if (avioutput_video) {
		ae = allocavientry_video ();
		if (!AVIOutput_AllocateVideo ())
			goto error;

		// fill in the header for the video stream
		memset (&avistreaminfo, 0, sizeof(AVISTREAMINFO));
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

		SetRect (&avistreaminfo.rcFrame, 0, 0, ae->lpbi->biWidth, ae->lpbi->biHeight); // rectangle for stream

		//avistreaminfo.dwEditCount =; // Number of times the stream has been edited. The stream handler maintains this count.
		//avistreaminfo.dwFormatChangeCount =; // Number of times the stream format has changed. The stream handler maintains this count.
		_tcscpy (avistreaminfo.szName, _T("Videostream")); // description of the stream.

		// create the stream
		if ((err = AVIFileCreateStream (pfile, &AVIStreamInterface, &avistreaminfo)) != 0) {
			gui_message (_T("AVIFileCreateStream() FAILED (%X)\n"), err);
			goto error;
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
		if ((err = AVIMakeCompressedStream (&AVIVideoStream, AVIStreamInterface, &videoOptions, NULL)) != AVIERR_OK) {
			gui_message (_T("AVIMakeCompressedStream() FAILED (%X)\n"), err);
			goto error;
		}
	}
	freeavientry (ae);
	init_comm_pipe (&workindex, 20, 1);
	init_comm_pipe (&queuefull, 20, 1);
	alive = -1;
	uae_start_thread (_T("aviworker"), AVIOutput_worker, NULL, NULL);
	write_log (_T("AVIOutput enabled: video=%d audio=%d\n"), avioutput_video, avioutput_audio);
	return;

error:
	freeavientry (ae);
	AVIOutput_End ();
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

	if (pcompvars) {
		AVIOutput_FreeCOMPVARS (pcompvars);
		xfree (pcompvars);
		pcompvars = NULL;
	}

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
}


static void *AVIOutput_worker (void *arg)
{
	write_log (_T("AVIOutput worker thread started\n"));
	alive = 1;
	for (;;) {
		uae_u32 idx = read_comm_pipe_u32_blocking (&workindex);
		struct avientry *ae;
		int r1 = 1;
		int r2 = 1;
		if (idx == 0xffffffff)
			break;
		for (;;) {
			EnterCriticalSection (&AVIOutput_CriticalSection);
			ae = getavientry ();
			LeaveCriticalSection (&AVIOutput_CriticalSection);
			if (ae == NULL)
				break;
			write_comm_pipe_u32 (&queuefull, 0, 1);
			if (!avioutput_failed) {
				if (ae->lpAudio)
					r1 = AVIOutput_AVIWriteAudio_Thread (ae);
				if (ae->lpVideo)
					r2 = AVIOutput_AVIWriteVideo_Thread (ae);
				if (r1 == 0 || r2 == 0)
					avioutput_failed = 1;
			}
			freeavientry (ae);
			if (idx != 0xfffffffe)
				break;
		}
		if (idx == 0xfffffffe || idx == 0xffffffff)
			break;
	}
	write_log (_T("AVIOutput worker thread killed\n"));
	alive = 0;
	return 0;
}


#include <math.h>

#define ADJUST_SIZE 10
#define EXP 1.1

void frame_drawn (void)
{
#if 0
	double diff, skipmode;
#endif
	int idiff;

	if (!avioutput_video || !avioutput_enabled)
		return;

	if (first_frame) {
		first_frame = 0;
		return;
	}

	AVIOutput_WriteVideo ();

	if (!avioutput_audio)
		return;

	StreamSizeAudioExpected += ((double)currprefs.sound_freq) / avioutput_fps;
	idiff = StreamSizeAudio - StreamSizeAudioExpected;
	if (idiff > 0) {
		skipsample += idiff / 10;
		if (skipsample > 4)
			skipsample = 4;
	}
	sound_setadjust (0.0);

#if 0
	write_log (_T("%d "), idiff);
	diff = idiff / 20.0;
	skipmode = pow (diff < 0 ? -diff : diff, EXP);
	if (idiff < 0)
		skipmode = -skipmode;
	if (skipmode < -ADJUST_SIZE)
		skipmode = -ADJUST_SIZE;
	if (skipmode > ADJUST_SIZE)
		skipmode = ADJUST_SIZE;
	write_log (_T("%d/%.2f\n"), idiff, skipmode);

	sound_setadjust (skipmode);

	if (0 && !(frame_count % avioutput_fps))
		write_log (_T("AVIOutput: diff=%.2f skip=%.2f (%d-%d=%d)\n"), diff, skipmode,
		StreamSizeAudio, StreamSizeAudioExpected, idiff);
#endif
}
