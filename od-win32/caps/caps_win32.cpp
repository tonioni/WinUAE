
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef CAPS

#include <shlobj.h>

#include "caps_win32.h"
#include "zfile.h"
#include "gui.h"
#include "win32.h"
#include "uae.h"

#include "ComType.h"
#include "CapsAPI.h"

static SDWORD caps_cont[4]= {-1, -1, -1, -1};
static int caps_locked[4];
static int caps_flags = DI_LOCK_DENVAR|DI_LOCK_DENNOISE|DI_LOCK_NOISE|DI_LOCK_UPDATEFD|DI_LOCK_TYPE|DI_LOCK_OVLBIT;
static struct CapsVersionInfo cvi;
static bool oldlib, canseed;

typedef SDWORD (__cdecl* CAPSINIT)(void);
static CAPSINIT pCAPSInit;
typedef SDWORD (__cdecl* CAPSADDIMAGE)(void);
static CAPSADDIMAGE pCAPSAddImage;
typedef SDWORD (__cdecl* CAPSLOCKIMAGEMEMORY)(SDWORD,PUBYTE,UDWORD,UDWORD);
static CAPSLOCKIMAGEMEMORY pCAPSLockImageMemory;
typedef SDWORD (__cdecl* CAPSUNLOCKIMAGE)(SDWORD);
static CAPSUNLOCKIMAGE pCAPSUnlockImage;
typedef SDWORD (__cdecl* CAPSLOADIMAGE)(SDWORD,UDWORD);
static CAPSLOADIMAGE pCAPSLoadImage;
typedef SDWORD (__cdecl* CAPSGETIMAGEINFO)(PCAPSIMAGEINFO,SDWORD);
static CAPSGETIMAGEINFO pCAPSGetImageInfo;
typedef SDWORD (__cdecl* CAPSLOCKTRACK)(PCAPSTRACKINFO,SDWORD,UDWORD,UDWORD,UDWORD);
static CAPSLOCKTRACK pCAPSLockTrack;
typedef SDWORD (__cdecl* CAPSUNLOCKTRACK)(SDWORD,UDWORD);
static CAPSUNLOCKTRACK pCAPSUnlockTrack;
typedef SDWORD (__cdecl* CAPSUNLOCKALLTRACKS)(SDWORD);
static CAPSUNLOCKALLTRACKS pCAPSUnlockAllTracks;
typedef SDWORD (__cdecl* CAPSGETVERSIONINFO)(PCAPSVERSIONINFO,UDWORD);
static CAPSGETVERSIONINFO pCAPSGetVersionInfo;

int caps_init (void)
{
	static int init, noticed;
	int i;
	HMODULE h;
	TCHAR *dllname = _T("CAPSImg.dll");

	if (init)
		return 1;
	h = WIN32_LoadLibrary (dllname);
	if (!h) {
		TCHAR tmp[MAX_DPATH];
		if (SUCCEEDED (SHGetFolderPath (NULL, CSIDL_PROGRAM_FILES_COMMON, NULL, 0, tmp))) {
			_tcscat (tmp, _T("\\Software Preservation Society\\"));
			_tcscat (tmp, dllname);
			h = LoadLibrary (tmp);
			if (!h) {
				if (noticed)
					return 0;
				notify_user (NUMSG_NOCAPS);
				noticed = 1;
				return 0;
			}
		}
	}
	if (GetProcAddress (h, "CAPSLockImageMemory") == 0 || GetProcAddress (h, "CAPSGetVersionInfo") == 0) {
		if (noticed)
			return 0;
		notify_user (NUMSG_OLDCAPS);
		noticed = 1;
		return 0;
	}
	pCAPSInit = (CAPSINIT)GetProcAddress (h, "CAPSInit");
	pCAPSAddImage = (CAPSADDIMAGE)GetProcAddress (h, "CAPSAddImage");
	pCAPSLockImageMemory = (CAPSLOCKIMAGEMEMORY)GetProcAddress (h, "CAPSLockImageMemory");
	pCAPSUnlockImage = (CAPSUNLOCKIMAGE)GetProcAddress (h, "CAPSUnlockImage");
	pCAPSLoadImage = (CAPSLOADIMAGE)GetProcAddress (h, "CAPSLoadImage");
	pCAPSGetImageInfo = (CAPSGETIMAGEINFO)GetProcAddress (h, "CAPSGetImageInfo");
	pCAPSLockTrack = (CAPSLOCKTRACK)GetProcAddress (h, "CAPSLockTrack");
	pCAPSUnlockTrack = (CAPSUNLOCKTRACK)GetProcAddress (h, "CAPSUnlockTrack");
	pCAPSUnlockAllTracks = (CAPSUNLOCKALLTRACKS)GetProcAddress (h, "CAPSUnlockAllTracks");
	pCAPSGetVersionInfo = (CAPSGETVERSIONINFO)GetProcAddress (h, "CAPSGetVersionInfo");
	init = 1;
	cvi.type = 1;
	pCAPSGetVersionInfo (&cvi, 0);
	write_log (_T("CAPS: library version %d.%d (flags=%08X)\n"), cvi.release, cvi.revision, cvi.flag);
	oldlib = (cvi.flag & (DI_LOCK_TRKBIT | DI_LOCK_OVLBIT)) != (DI_LOCK_TRKBIT | DI_LOCK_OVLBIT);
	if (!oldlib)
		caps_flags |= DI_LOCK_TRKBIT | DI_LOCK_OVLBIT;
	canseed = (cvi.flag & DI_LOCK_SETWSEED) != 0;
	for (i = 0; i < 4; i++)
		caps_cont[i] = pCAPSAddImage ();
	return 1;
}

void caps_unloadimage (int drv)
{
	if (!caps_locked[drv])
		return;
	pCAPSUnlockAllTracks (caps_cont[drv]);
	pCAPSUnlockImage (caps_cont[drv]);
	caps_locked[drv] = 0;
}

int caps_loadimage (struct zfile *zf, int drv, int *num_tracks)
{
	static int notified;
	struct CapsImageInfo ci;
	int len, ret;
	uae_u8 *buf;
	TCHAR s1[100];
	struct CapsDateTimeExt *cdt;

	if (!caps_init ())
		return 0;
	caps_unloadimage (drv);
	zfile_fseek (zf, 0, SEEK_END);
	len = zfile_ftell (zf);
	zfile_fseek (zf, 0, SEEK_SET);
	buf = xmalloc (uae_u8, len);
	if (!buf)
		return 0;
	if (zfile_fread (buf, len, 1, zf) == 0)
		return 0;
	ret = pCAPSLockImageMemory (caps_cont[drv], buf, len, 0);
	xfree (buf);
	if (ret != imgeOk) {
		if (ret == imgeIncompatible || ret == imgeUnsupported) {
			if (!notified)
				notify_user (NUMSG_OLDCAPS);
			notified = 1;
		}
		write_log (_T("caps: CAPSLockImageMemory() returned %d\n"), ret);
		return 0;
	}
	caps_locked[drv] = 1;
	ret = pCAPSGetImageInfo(&ci, caps_cont[drv]);
	*num_tracks = (ci.maxcylinder - ci.mincylinder + 1) * (ci.maxhead - ci.minhead + 1);

	if (cvi.release < 4) { // pre-4.x bug workaround
		struct CapsTrackInfoT1 cit;
		cit.type = 1;
		if (pCAPSLockTrack ((PCAPSTRACKINFO)&cit, caps_cont[drv], 0, 0, caps_flags) == imgeIncompatible) {
			if (!notified)
				notify_user (NUMSG_OLDCAPS);
			notified = 1;
			caps_unloadimage (drv);
			return 0;
		}
		pCAPSUnlockAllTracks (caps_cont[drv]);
	}

	ret = pCAPSLoadImage(caps_cont[drv], caps_flags);
	cdt = &ci.crdt;
	_stprintf (s1, _T("%d.%d.%d %d:%d:%d"), cdt->day, cdt->month, cdt->year, cdt->hour, cdt->min, cdt->sec);
	write_log (_T("caps: type:%d date:%s rel:%d rev:%d\n"),
		ci.type, s1, ci.release, ci.revision);
	return 1;
}

#if 0
static void outdisk (void)
{
	struct CapsTrackInfo ci;
	int tr;
	FILE *f;
	static int done;

	if (done)
		return;
	done = 1;
	f = fopen("c:\\out3.dat", "wb");
	if (!f)
		return;
	for (tr = 0; tr < 160; tr++) {
		pCAPSLockTrack(&ci, caps_cont[0], tr / 2, tr & 1, caps_flags);
		fwrite (ci.trackdata[0], ci.tracksize[0], 1, f);
		fwrite ("XXXX", 4, 1, f);
	}
	fclose (f);
}
#endif

static void mfmcopy (uae_u16 *mfm, uae_u8 *data, int len)
{
	int memlen = (len + 7) / 8;
	for (int i = 0; i < memlen; i+=2) {
		if (i + 1 < memlen)
			*mfm++ = (data[i] << 8) + data[i + 1];
		else
			*mfm++ = (data[i] << 8);
	}
}

static int load (struct CapsTrackInfoT2 *ci, int drv, int track, bool seed)
{
	int flags;
	
	flags = caps_flags;
	if (canseed) {
		ci->type = 2;
		if (seed) {
			flags |= DI_LOCK_SETWSEED;
			ci->wseed = uaerand ();
		}
	} else {
		ci->type = 1;
	}
	if (pCAPSLockTrack ((PCAPSTRACKINFO)ci, caps_cont[drv], track / 2, track & 1, flags) != imgeOk)
		return 0;
	return 1;
}

int caps_loadrevolution (uae_u16 *mfmbuf, int drv, int track, int *tracklength)
{
	int len;
	struct CapsTrackInfoT2 ci;

	if (!load (&ci, drv, track, false))
		return 0;
	if (oldlib)
		len = ci.tracklen * 8;
	else
		len = ci.tracklen;
	*tracklength = len;
	mfmcopy (mfmbuf, ci.trackbuf, len);
	return 1;
}

int caps_loadtrack (uae_u16 *mfmbuf, uae_u16 *tracktiming, int drv, int track, int *tracklength, int *multirev, int *gapoffset)
{
	int len;
	struct CapsTrackInfoT2 ci;

	if (tracktiming)
		*tracktiming = 0;
	if (!load (&ci, drv, track, true))
		return 0;
	*multirev = (ci.type & CTIT_FLAG_FLAKEY) ? 1 : 0;
	if (oldlib) {
		len = ci.tracklen * 8;
		*gapoffset = ci.overlap >= 0 ? ci.overlap * 8 : -1;
	} else {
		len = ci.tracklen;
		*gapoffset = ci.overlap >= 0 ? ci.overlap : -1;
	}
	//write_log (_T("%d %d %d %d\n"), track, len, ci.tracklen, ci.overlap);
	*tracklength = len;
	mfmcopy (mfmbuf, ci.trackbuf, len);
#if 0
	{
		FILE *f=fopen("c:\\1.txt","wb");
		fwrite (ci.trackbuf, len, 1, f);
		fclose (f);
	}
#endif
	if (ci.timelen > 0 && tracktiming) {
		for (int i = 0; i < ci.timelen; i++)
			tracktiming[i] = (uae_u16)ci.timebuf[i];
	}
#if 0
	write_log (_T("caps: drive:%d track:%d len:%d multi:%d timing:%d type:%d overlap:%d\n"),
		drv, track, len, *multirev, ci.timelen, type, ci.overlap);
#endif
	return 1;
}

#endif
