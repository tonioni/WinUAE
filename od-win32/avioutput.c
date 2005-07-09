/*
  UAE - The Ultimate Amiga Emulator
  
  avioutput.c
  
  Copyright(c) 2001 - 2002; §ane

*/

#include <windows.h>

#include <ddraw.h>

#include <mmsystem.h>
#include <vfw.h>
#include <msacm.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "custom.h"
#include "picasso96.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "opengl.h"
#include "sound.h"
#include "gfxfilter.h"
#include "xwin.h"
#include "resource.h"
#include "avioutput.h"

#define MAX_AVI_SIZE (0x80000000 - 0x1000000)

static int avioutput_init = 0;
static int actual_width = 320, actual_height = 256;
static int avioutput_needs_restart;

static int frame_start; // start frame
static int frame_count; // current frame
static int frame_end; // end frame (0 = no end, infinite)
static int frame_skip;
static unsigned int total_avi_size;
static int partcnt;

static unsigned int StreamSizeAudio; // audio write position
static double StreamSizeAudioExpected;

int avioutput_audio, avioutput_video, avioutput_enabled, avioutput_requested;

int avioutput_width = 320, avioutput_height = 256, avioutput_bits = 24;
int avioutput_fps = VBLANK_HZ_PAL;
int avioutput_framelimiter = 0;

char avioutput_filename[MAX_DPATH];
static char avioutput_filename_tmp[MAX_DPATH];

extern struct uae_prefs workprefs;
extern char config_filename[256];

static CRITICAL_SECTION AVIOutput_CriticalSection;


static PAVIFILE pfile = NULL; // handle of our AVI file
static PAVISTREAM AVIStreamInterface = NULL; // Address of stream interface


/* audio */

static PAVISTREAM AVIAudioStream = NULL; // compressed stream pointer

static HACMSTREAM has = NULL; // stream handle that can be used to perform conversions
static ACMSTREAMHEADER ash;

static ACMFORMATCHOOSE acmopt;

static WAVEFORMATEX wfxSrc; // source audio format
static LPWAVEFORMATEX pwfxDst = NULL; // pointer to destination audio format

static uae_u8 *lpAudio; // pointer to audio data

static FILE *wavfile;

/* video */

static PAVISTREAM AVIVideoStream = NULL; // compressed stream pointer

static AVICOMPRESSOPTIONS videoOptions;
static AVICOMPRESSOPTIONS FAR * aOptions[] = { &videoOptions }; // array of pointers to AVICOMPRESSOPTIONS structures

static PCOMPVARS pcompvars;

static LPBITMAPINFOHEADER lpbi = NULL; // can also be used as LPBITMAPINFO because we allocate memory for bitmap info header + bitmap infomation
static uae_u8 *lpVideo = NULL; // pointer to video data (bitmap bits)



static UINT CALLBACK acmFilterChooseHookProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(uMsg == MM_ACM_FORMATCHOOSE)
	{
		switch(wParam)
		{
		case FORMATCHOOSE_FORMATTAG_VERIFY:
			switch(lParam) // remove known error prone codecs
			{
			case WAVE_FORMAT_ADPCM: // 0x0002 Microsoft Corporation
			case WAVE_FORMAT_IMA_ADPCM: // 0x0011 Intel Corporation
			case WAVE_FORMAT_GSM610: // 0x0031 Microsoft Corporation
			case WAVE_FORMAT_SONY_SCX: // 0x0270 Sony Corp.
			return TRUE;
			}
			break;
		}
	}

	return FALSE;
}

void AVIOutput_ReleaseAudio(void)
{
	if(lpAudio)
	{
		free(lpAudio);
		lpAudio = NULL;
	}

	if(pwfxDst)
	{
		free(pwfxDst);
		pwfxDst = NULL;
	}
}

void AVIOutput_ReleaseVideo(void)
{
	if(pcompvars)
	{
		ICClose(pcompvars->hic); // <sane> did we inadvertently open it?
		ICCompressorFree(pcompvars);
		free(pcompvars);
		pcompvars = NULL;
	}

	if(lpbi)
	{
		free(lpbi);
		lpbi = NULL;
	}

	if(lpVideo)
	{
		free(lpVideo);
		lpVideo = NULL;
	}
}

LPSTR AVIOutput_ChooseAudioCodec(HWND hwnd)
{
	DWORD wfxMaxFmtSize;
	MMRESULT err;

	AVIOutput_ReleaseAudio();

	if((err = acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &wfxMaxFmtSize)))
	{
		gui_message("acmMetrics() FAILED (%X)\n", err);
		return NULL;
	}

	// set the source format
	wfxSrc.wFormatTag = WAVE_FORMAT_PCM;
	wfxSrc.nChannels = workprefs.sound_stereo ? 2 : 1;
	wfxSrc.nSamplesPerSec = workprefs.sound_freq;
	wfxSrc.nBlockAlign = wfxSrc.nChannels * (workprefs.sound_bits / 8);
	wfxSrc.nAvgBytesPerSec = wfxSrc.nBlockAlign * wfxSrc.nSamplesPerSec;
	wfxSrc.wBitsPerSample = workprefs.sound_bits;
	wfxSrc.cbSize = 0;

	if(!(pwfxDst = (LPWAVEFORMATEX) malloc(wfxMaxFmtSize)))
		return NULL;

	// set the initial destination format to match source
	memset(pwfxDst, 0, wfxMaxFmtSize);
	memcpy(pwfxDst, &wfxSrc, sizeof(WAVEFORMATEX));
	pwfxDst->cbSize = (WORD) (wfxMaxFmtSize - sizeof(WAVEFORMATEX)); // shrugs

	memset(&acmopt, 0, sizeof(ACMFORMATCHOOSE));

	acmopt.cbStruct = sizeof(ACMFORMATCHOOSE);
	acmopt.fdwStyle = ACMFORMATCHOOSE_STYLEF_ENABLEHOOK | ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT;
	acmopt.hwndOwner = hwnd;

	acmopt.pwfx = pwfxDst;
	acmopt.cbwfx = wfxMaxFmtSize;

	acmopt.pszTitle  = "Choose Audio Codec";

	//acmopt.szFormatTag =; // not valid until the format is chosen
	//acmopt.szFormat =; // not valid until the format is chosen

	//acmopt.pszName =; // can use later in config saving loading
	//acmopt.cchName =; // size of pszName, as pszName can be non-null-terminated

	acmopt.fdwEnum = ACM_FORMATENUMF_INPUT | ACM_FORMATENUMF_NCHANNELS | ACM_FORMATENUMF_NSAMPLESPERSEC;
	//ACM_FORMATENUMF_CONVERT // renders WinUAE unstable for some unknown reason
	//ACM_FORMATENUMF_WBITSPERSAMPLE // MP3 doesn't apply so it will be removed from codec selection
	//ACM_FORMATENUMF_SUGGEST // with this flag set, only MP3 320kbps is displayed, which is closest to the source format

	acmopt.pwfxEnum = &wfxSrc;

	acmopt.pfnHook = acmFilterChooseHookProc;

	switch(acmFormatChoose(&acmopt))
	{
	case MMSYSERR_NOERROR:
		return acmopt.szFormatTag;
		
	case ACMERR_CANCELED:
		//MessageBox(hwnd, "The user chose the Cancel button or the Close command on the System menu to close the dialog box.", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
		
	case ACMERR_NOTPOSSIBLE:
		MessageBox(hwnd, "The buffer identified by the pwfx member of the ACMFORMATCHOOSE structure is too small to contain the selected format.", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
		
	case MMSYSERR_INVALFLAG:
		MessageBox(hwnd, "At least one flag is invalid.", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
		
	case MMSYSERR_INVALHANDLE:
		MessageBox(hwnd, "The specified handle is invalid.", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
		
	case MMSYSERR_INVALPARAM:
		MessageBox(hwnd, "At least one parameter is invalid.", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
		
	case MMSYSERR_NODRIVER:
		MessageBox(hwnd, "A suitable driver is not available to provide valid format selections.", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
		
	default:
		MessageBox(hwnd, "acmFormatChoose() FAILED", VersionStr, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		break;
	}

	return NULL;
}

LPSTR AVIOutput_ChooseVideoCodec(HWND hwnd)
{
	ICINFO icinfo = { 0 };

	AVIOutput_ReleaseVideo();

	avioutput_width = workprefs.gfx_width;
	avioutput_height = workprefs.gfx_height;
	avioutput_bits = workprefs.color_mode == 2 ? 16 : workprefs.color_mode > 2 ? 32 : 8;

	if(!(lpbi = (LPBITMAPINFOHEADER) malloc(sizeof(BITMAPINFOHEADER) + (((avioutput_bits <= 8) ? 1 << avioutput_bits : 0) * sizeof(RGBQUAD)))))
		return NULL;

	lpbi->biSize = sizeof(BITMAPINFOHEADER);
	lpbi->biWidth = avioutput_width;
	lpbi->biHeight = avioutput_height;
	lpbi->biPlanes = 1;
	lpbi->biBitCount = avioutput_bits;
	lpbi->biCompression = BI_RGB; // uncompressed format
	lpbi->biSizeImage = (lpbi->biWidth * lpbi->biHeight) * (lpbi->biBitCount / 8);
	lpbi->biXPelsPerMeter = 0; // ??
	lpbi->biYPelsPerMeter = 0; // ??
	lpbi->biClrUsed = (lpbi->biBitCount <= 8) ? 1 << lpbi->biBitCount : 0;
	lpbi->biClrImportant = 0;

	if(!(pcompvars = (PCOMPVARS) malloc(sizeof(COMPVARS))))
		return NULL;

	memset(pcompvars, 0, sizeof(COMPVARS));
	pcompvars->cbSize = sizeof(COMPVARS);

	// we really should check first to see if the user has a particular compressor installed before we set one
	// we could set one but we will leave it up to the operating system and the set priority levels for the compressors

	// default
	//pcompvars->fccHandler = mmioFOURCC('C','V','I','D'); // "Cinepak Codec by Radius"
	//pcompvars->fccHandler = mmioFOURCC('M','R','L','E'); // "Microsoft RLE"
	//pcompvars->fccHandler = mmioFOURCC('D','I','B',' '); // "Full Frames (Uncompressed)"

	pcompvars->lQ = 10000; // 10000 is maximum quality setting or ICQUALITY_DEFAULT for default
	pcompvars->lKey = avioutput_fps; // default to one key frame per second, every (FPS) frames

	pcompvars->dwFlags = ICMF_COMPVARS_VALID;

	if(ICCompressorChoose(hwnd, ICMF_CHOOSE_DATARATE | ICMF_CHOOSE_KEYFRAME, lpbi, NULL, pcompvars, "Choose Video Codec") == TRUE)
	{
		if(pcompvars->fccHandler == mmioFOURCC('D','I','B',' '))
			return "Full Frames (Uncompressed)";
		
		if(ICGetInfo(pcompvars->hic, &icinfo, sizeof(ICINFO)) != 0)
		{
			static char string[128];
			
			if(WideCharToMultiByte(CP_ACP, 0, icinfo.szDescription, -1, string, 128, NULL, NULL) != 0)
				return string;
		}
	}

	return NULL;
}

static void checkAVIsize (int force)
{
    int tmp_partcnt = partcnt + 1;
    int tmp_avioutput_video = avioutput_video;
    int tmp_avioutput_audio = avioutput_audio;
    char fn[MAX_DPATH];

    if (!force && total_avi_size < MAX_AVI_SIZE)
	return;
    strcpy (fn, avioutput_filename_tmp);
    sprintf (avioutput_filename, "%s_%d.avi", fn, tmp_partcnt);
    write_log("AVI split %d at %d bytes, %d frames\n",
	tmp_partcnt, total_avi_size, frame_count);
    AVIOutput_End ();
    total_avi_size = 0;
    avioutput_video = tmp_avioutput_video;
    avioutput_audio = tmp_avioutput_audio;
    AVIOutput_Begin ();
    strcpy (avioutput_filename_tmp, fn);
    partcnt = tmp_partcnt;
}

static void dorestart (void)
{
    write_log ("AVIOutput: parameters changed, restarting..\n");
    avioutput_needs_restart = 0;
    checkAVIsize (1);
}

static void AVIOuput_AVIWriteAudio (uae_u8 *sndbuffer, int sndbufsize)
{
	DWORD dwOutputBytes = 0, written = 0, swritten = 0;
	unsigned int err;

	if (avioutput_needs_restart)
	    dorestart ();

	EnterCriticalSection(&AVIOutput_CriticalSection);

	if(avioutput_audio)
	{
		if(!avioutput_init)
			goto error;

		if((err = acmStreamSize(has, sndbufsize, &dwOutputBytes, ACM_STREAMSIZEF_SOURCE) != 0))
		{
			gui_message("acmStreamSize() FAILED (%X)\n", err);
			goto error;
		}

		if(!(lpAudio = malloc(dwOutputBytes)))
		{
			goto error;
		}

		ash.cbStruct = sizeof(ACMSTREAMHEADER);
		ash.fdwStatus = 0;
		ash.dwUser = 0;

		// source
		ash.pbSrc = sndbuffer;

		ash.cbSrcLength = sndbufsize;
		ash.cbSrcLengthUsed = 0; // This member is not valid until the conversion is complete.

		ash.dwSrcUser = 0;

		// destination
		ash.pbDst = lpAudio;

		ash.cbDstLength = dwOutputBytes;
		ash.cbDstLengthUsed = 0; // This member is not valid until the conversion is complete.

		ash.dwDstUser = 0;

		if((err = acmStreamPrepareHeader(has, &ash, 0)))
		{
			gui_message("acmStreamPrepareHeader() FAILED (%X)\n", err);
			goto error;
		}

		if((err = acmStreamConvert(has, &ash, ACM_STREAMCONVERTF_BLOCKALIGN)))
		{
			gui_message("acmStreamConvert() FAILED (%X)\n", err);
			goto error;
		}

		if((err = AVIStreamWrite(AVIAudioStream, StreamSizeAudio, ash.cbDstLengthUsed / pwfxDst->nBlockAlign, lpAudio, ash.cbDstLengthUsed, 0, &swritten, &written)) != 0)
		{
			gui_message("AVIStreamWrite() FAILED (%X)\n", err);
			goto error;
		}

		StreamSizeAudio += swritten;
		total_avi_size += written;

		acmStreamUnprepareHeader(has, &ash, 0);
		
		if(lpAudio)
		{
			free(lpAudio);
			lpAudio = NULL;
		}
		checkAVIsize (0);
	}

	LeaveCriticalSection(&AVIOutput_CriticalSection);
	return;

error:
	LeaveCriticalSection(&AVIOutput_CriticalSection);
	AVIOutput_End();
}

static void AVIOuput_WAVWriteAudio (uae_u8 *sndbuffer, int sndbufsize)
{
    fwrite (sndbuffer, 1, sndbufsize, wavfile);
}

void AVIOutput_WriteAudio(uae_u8 *sndbuffer, int sndbufsize)
{
    if (!avioutput_audio || !avioutput_enabled)
	return;
    if (avioutput_audio == AVIAUDIO_WAV)
	AVIOuput_WAVWriteAudio (sndbuffer, sndbufsize);
    else
	AVIOuput_AVIWriteAudio (sndbuffer, sndbufsize);
}

static int getFromDC(LPBITMAPINFO lpbi)
{
    HDC hdc;
    HBITMAP hbitmap = NULL;
    HBITMAP hbitmapOld = NULL;
    HDC hdcMem = NULL;
    int ok = 1;

    hdc = gethdc();
    if (!hdc)
	return 0;
    // create a memory device context compatible with the application's current screen
    hdcMem = CreateCompatibleDC(hdc);
    hbitmap = CreateCompatibleBitmap(hdc, avioutput_width, avioutput_height);
    hbitmapOld = SelectObject(hdcMem, hbitmap);
    // probably not the best idea to use slow GDI functions for this,
    // locking the surfaces and copying them by hand would be more efficient perhaps
    // draw centered in frame
    BitBlt(hdcMem, (avioutput_width / 2) - (actual_width / 2), (avioutput_height / 2) - (actual_height / 2), actual_width, actual_height, hdc, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbitmapOld);
    if(GetDIBits(hdc, hbitmap, 0, avioutput_height, lpVideo, (LPBITMAPINFO) lpbi, DIB_RGB_COLORS) == 0)
    {
	gui_message("GetDIBits() FAILED (%X)\n", GetLastError());
	ok = 0;
    }
    DeleteObject(hbitmap);
    DeleteDC(hdcMem);
    releasehdc(hdc);
    return ok;
}

static int rgb_type;

void AVIOutput_RGBinfo(int rb, int gb, int bb, int rs, int gs, int bs)
{
    if (bs == 0 && gs == 5 && rs == 11)
	rgb_type = 1;
    else if (bs == 0 && gs == 8 && rs == 16)
	rgb_type = 2;
    else
	rgb_type = 0;
}

#if defined (GFXFILTER)
extern int bufmem_width, bufmem_height;
extern uae_u8 *bufmem_ptr;

static int getFromBuffer(LPBITMAPINFO lpbi)
{
    int w, h, x, y;
    uae_u8 *src;
    uae_u8 *dst = lpVideo;

    src = bufmem_ptr;
    if (!src)
	return 0;
    w = bufmem_width;
    h = bufmem_height;
    dst += avioutput_width * gfxvidinfo.pixbytes * avioutput_height;
    for (y = 0; y < (gfxvidinfo.height > avioutput_height ? avioutput_height : gfxvidinfo.height); y++) {
	dst -= avioutput_width * gfxvidinfo.pixbytes;
	for (x = 0; x < (gfxvidinfo.width > avioutput_width ? avioutput_width : gfxvidinfo.width); x++) {
	    if (gfxvidinfo.pixbytes == 1) {
		dst[x] = src[x];
	    } else if (gfxvidinfo.pixbytes == 2) {
		uae_u16 v = ((uae_u16*)src)[x];
		uae_u16 v2 = v;
		if (rgb_type) {
		    v2 = v & 31;
		    v2 |= (v >> 1) & (31 << 5);
		    v2 |= (v >> 1) & (31 << 10);
		}
		((uae_u16*)dst)[x] = v2;
	    } else if (gfxvidinfo.pixbytes == 4) {
		uae_u32 v = ((uae_u32*)src)[x];
		((uae_u32*)dst)[x] = v;
	    }
	}
	src += gfxvidinfo.rowbytes;
    }
    return 1;
}
#endif

void AVIOutput_WriteVideo(void)
{
    DWORD written = 0;
    int v;
    unsigned int err;

    if (avioutput_needs_restart)
	dorestart ();

    EnterCriticalSection(&AVIOutput_CriticalSection);

    if(avioutput_video) {

	if(!avioutput_init)
	    goto error;

	actual_width = gfxvidinfo.width;
	actual_height = gfxvidinfo.height;

#if defined (GFXFILTER)
	if (!usedfilter || (usedfilter && usedfilter->x[0]) || WIN32GFX_IsPicassoScreen ())
	    v = getFromDC((LPBITMAPINFO)lpbi);
	else
	    v = getFromBuffer((LPBITMAPINFO)lpbi);
#else
	v = getFromDC((LPBITMAPINFO)lpbi);
#endif
	if (!v)
	    goto error;

	// GetDIBits tends to change this and ruins palettized output
	lpbi->biClrUsed = (avioutput_bits <= 8) ? 1 << avioutput_bits : 0;

	if(!frame_count)
	{
	    if((err = AVIStreamSetFormat(AVIVideoStream, frame_count, lpbi, lpbi->biSize + (lpbi->biClrUsed * sizeof(RGBQUAD)))) != 0)
	    {
		gui_message("AVIStreamSetFormat() FAILED (%X)\n", err);
		goto error;
	    }
	}
			
	if((err = AVIStreamWrite(AVIVideoStream, frame_count, 1, lpVideo, lpbi->biSizeImage, 0, NULL, &written)) != 0)
	{
	    gui_message("AVIStreamWrite() FAILED (%X)\n", err);
	    goto error;
	}

	frame_count++;
	total_avi_size += written;

	if(frame_end)
	{
	    if(frame_count >= frame_end)
	    {
		AVIOutput_End();
	    }
	}
	checkAVIsize (0);

    } else {

	gui_message("DirectDraw_GetDC() FAILED\n");
	goto error;

    }

    LeaveCriticalSection(&AVIOutput_CriticalSection);
    if ((frame_count % (avioutput_fps * 10)) == 0)
	write_log ("AVIOutput: %d frames, (%d fps)\n", frame_count, avioutput_fps);
    return;

error:

    LeaveCriticalSection(&AVIOutput_CriticalSection);
    AVIOutput_End();
}

static void writewavheader (uae_u32 size)
{
    uae_u16 tw;
    uae_u32 tl;
    int bits = 16, channels = currprefs.sound_stereo ? 2 : 1;

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

void AVIOutput_Restart(void)
{
    avioutput_needs_restart = 1;
}

void AVIOutput_End(void)
{
	EnterCriticalSection(&AVIOutput_CriticalSection);

	avioutput_enabled = 0;
	if(has)
	{
		acmStreamUnprepareHeader(has, &ash, 0);
		acmStreamClose(has, 0);
		has = NULL;
	}

	if(AVIAudioStream)
	{
		AVIStreamRelease(AVIAudioStream);
		AVIAudioStream = NULL;
	}

	if(AVIVideoStream)
	{
		AVIStreamRelease(AVIVideoStream);
		AVIVideoStream = NULL;
	}

	if(AVIStreamInterface)
	{
		AVIStreamRelease(AVIStreamInterface);
		AVIStreamInterface = NULL;
	}

	if(pfile)
	{
		AVIFileRelease(pfile);
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

	LeaveCriticalSection(&AVIOutput_CriticalSection);
}

void AVIOutput_Begin(void)
{
	AVISTREAMINFO avistreaminfo; // Structure containing information about the stream, including the stream type and its sample rate
	int i, err;
	char *ext1, *ext2;

	if (avioutput_enabled) {
	    if (!avioutput_requested)
		AVIOutput_End ();
	    return;
	}
	if (!avioutput_requested)
	    return;

	reset_sound ();
	if (avioutput_audio == AVIAUDIO_WAV) {
	    ext1 = ".wav"; ext2 = ".avi";
	} else {
	    ext1 = ".avi"; ext2 = ".wav";
	}
	if (strlen (avioutput_filename) >= 4 && !strcmpi (avioutput_filename + strlen (avioutput_filename) - 4, ext2))
	    avioutput_filename[strlen (avioutput_filename) - 4] = 0;
	if (strlen (avioutput_filename) >= 4 && strcmpi (avioutput_filename + strlen (avioutput_filename) - 4, ext1))
	    strcat (avioutput_filename, ext1);
	strcpy (avioutput_filename_tmp, avioutput_filename);
	i = strlen (avioutput_filename_tmp) - 1;
	while (i > 0 && avioutput_filename_tmp[i] != '.') i--;
	if (i > 0)
	    avioutput_filename_tmp[i] = 0;

	avioutput_needs_restart = 0;
	avioutput_enabled = avioutput_audio || avioutput_video;
	if(!avioutput_init || !avioutput_enabled)
		goto error;

	// delete any existing file before writing AVI
	SetFileAttributes(avioutput_filename, FILE_ATTRIBUTE_ARCHIVE);
	DeleteFile(avioutput_filename);

	if (avioutput_audio == AVIAUDIO_WAV) {
	    wavfile = fopen (avioutput_filename, "wb");
	    if (!wavfile) {
		gui_message("Failed to open wave-file\n\nThis can happen if the path and or file name was entered incorrectly.\n");
		goto error;
	    }
	    writewavheader (0);
	    write_log ("wave-output to '%s' started\n", avioutput_filename);
	    return;
	}
	
	if(((err = AVIFileOpen(&pfile, avioutput_filename, OF_CREATE | OF_WRITE, NULL)) != 0))
	{
		gui_message("AVIFileOpen() FAILED (Error %X)\n\nThis can happen if the path and or file name was entered incorrectly.\nRequired *.avi extension.\n", err);
		goto error;
	}
	
	if(avioutput_audio)
	{
		memset(&avistreaminfo, 0, sizeof(AVISTREAMINFO));
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
		strcpy(avistreaminfo.szName, "Audiostream"); // description of the stream.

		// create the audio stream
		if((err = AVIFileCreateStream(pfile, &AVIAudioStream, &avistreaminfo)) != 0)
		{
			gui_message("AVIFileCreateStream() FAILED (%X)\n", err);
			goto error;
		}

		if((err = AVIStreamSetFormat(AVIAudioStream, 0, pwfxDst, sizeof(WAVEFORMATEX) + pwfxDst->cbSize)) != 0)
		{
			gui_message("AVIStreamSetFormat() FAILED (%X)\n", err);
			goto error;
		}

		if((err = acmStreamOpen(&has, NULL, &wfxSrc, pwfxDst, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME)) != 0)
		{
			gui_message("acmStreamOpen() FAILED (%X)\n", err);
			goto error;
		}
	}

	if(avioutput_video)
	{
		if(!(lpVideo = malloc(lpbi->biSizeImage)))
		{
			goto error;
		}

		// fill in the header for the video stream
		memset(&avistreaminfo, 0, sizeof(AVISTREAMINFO));
		avistreaminfo.fccType = streamtypeVIDEO; // stream type

		// unsure about this, as this is the uncompressed stream, not the compressed stream
		//avistreaminfo.fccHandler = 0;

		// incase the amiga changes palette
		if(lpbi->biBitCount < 24)
			avistreaminfo.dwFlags = AVISTREAMINFO_FORMATCHANGES;

		//avistreaminfo.dwCaps =; // Capability flags; currently unused
		//avistreaminfo.wPriority =; // Priority of the stream
		//avistreaminfo.wLanguage =; // Language of the stream
		avistreaminfo.dwScale = 1;
		avistreaminfo.dwRate = avioutput_fps; // our playback speed default (PAL 50fps), (NTSC 60fps)
		avistreaminfo.dwStart = 0; // no delay
		avistreaminfo.dwLength = 1; // initial length
		//avistreaminfo.dwInitialFrames =; // audio only
		avistreaminfo.dwSuggestedBufferSize = lpbi->biSizeImage;
		avistreaminfo.dwQuality = -1; // drivers will use the default quality setting
		avistreaminfo.dwSampleSize = 0; // variable video data samples

		SetRect(&avistreaminfo.rcFrame, 0, 0, lpbi->biWidth, lpbi->biHeight); // rectangle for stream

		//avistreaminfo.dwEditCount =; // Number of times the stream has been edited. The stream handler maintains this count.
		//avistreaminfo.dwFormatChangeCount =; // Number of times the stream format has changed. The stream handler maintains this count.
		strcpy(avistreaminfo.szName, "Videostream"); // description of the stream.

		// create the stream
		if((err = AVIFileCreateStream(pfile, &AVIStreamInterface, &avistreaminfo)) != 0)
		{
			gui_message("AVIFileCreateStream() FAILED (%X)\n", err);
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
		if((err = AVIMakeCompressedStream(&AVIVideoStream, AVIStreamInterface, &videoOptions, NULL)) != AVIERR_OK)
		{
			gui_message("AVIMakeCompressedStream() FAILED (%X)\n", err);
			goto error;
		}
	}
	write_log ("AVIOutput enabled: video=%d audio=%d\n", avioutput_video, avioutput_audio);
	return;

error:
	AVIOutput_End();
}

void AVIOutput_Release(void)
{
	AVIOutput_End();
	
	AVIOutput_ReleaseAudio();
	AVIOutput_ReleaseVideo();

	if(avioutput_init)
	{
		AVIFileExit();
		avioutput_init = 0;
	}

	DeleteCriticalSection(&AVIOutput_CriticalSection);
}

void AVIOutput_Initialize(void)
{
	InitializeCriticalSection(&AVIOutput_CriticalSection);

	if(!avioutput_init)
	{
		AVIFileInit();
		avioutput_init = 1;
	}
}

#include <math.h>

#define ADJUST_SIZE 10
#define EXP 1.5

void frame_drawn(void)
{
    double diff, skipmode;
    static int frame;

    if (!avioutput_video || !avioutput_enabled)
	return;

    AVIOutput_WriteVideo();

    if (avioutput_audio && (frame_count % avioutput_fps) == 0) {
	StreamSizeAudioExpected += currprefs.sound_freq;
	diff = (StreamSizeAudio - StreamSizeAudioExpected) / sndbufsize;
	skipmode = pow (diff < 0 ? -diff : diff, EXP);
	if (diff < 0) skipmode = -skipmode;
	if (skipmode < -ADJUST_SIZE) skipmode = -ADJUST_SIZE;
	if (skipmode > ADJUST_SIZE) skipmode = ADJUST_SIZE;
	sound_setadjust (skipmode);
	write_log("AVIOutput: diff=%.2f skip=%.2f\n", diff, skipmode);
    }
}



