
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef CAPS

#include "caps_win32.h"
#include "zfile.h"

#include "ComType.h"
#include "CapsAPI.h"
#include "CapsLib.h"

static SDWORD caps_cont[4]= {-1, -1, -1, -1};
static int caps_locked[4];
static int caps_flags = DI_LOCK_DENVAR|DI_LOCK_DENNOISE|DI_LOCK_NOISE;

int caps_init (void)
{
    static int init, noticed;
    int i;
    HMODULE h;

    if (init) return 1;
    h = LoadLibrary ("CAPSImg.dll");
    if (!h) {
	if (noticed)
	    return 0;
	gui_message (
	    "This disk image needs the C.A.P.S. plugin\nwhich is available from\nhttp//www.caps-project.org/download.shtml");
	noticed = 1;
	return 0;
    }
    if (GetProcAddress(h, "CAPSLockImageMemory") == 0) {
	if (noticed)
	    return 0;
	gui_message (
	    "You need updated C.A.P.S. plugin\nwhich is available from\nhttp//www.caps-project.org/download.shtml");
	noticed = 1;
	return 0;
    }	
    FreeLibrary (h);
    init = 1;
    for (i = 0; i < 4; i++)
	caps_cont[i] = CAPSAddImage();
    return 1;
}

void caps_unloadimage (int drv)
{
    if (!caps_locked[drv])
	return;
    CAPSUnlockAllTracks (caps_cont[drv]);
    CAPSUnlockImage (caps_cont[drv]);
    caps_locked[drv] = 0;
}

int caps_loadimage (struct zfile *zf, int drv, int *num_tracks)
{
    struct CapsImageInfo ci;
    int len,ret ;
    uae_u8 *buf;

    if (!caps_init ())
	return 0;
    caps_unloadimage (drv);
    zfile_fseek (zf, 0, SEEK_END);
    len = zfile_ftell (zf);
    zfile_fseek (zf, 0, SEEK_SET);
    buf = xmalloc (len);
    if (!buf)
	return 0;
    if (zfile_fread (buf, len, 1, zf) == 0)
	return 0;
    ret = CAPSLockImageMemory(caps_cont[drv], buf, len, 0);
    free (buf);
    if (ret != imgeOk) {
	free (buf);
	return 0;
    }
    caps_locked[drv] = 1;
    CAPSGetImageInfo(&ci, caps_cont[drv]);
    *num_tracks = (ci.maxcylinder - ci.mincylinder + 1) * (ci.maxhead - ci.minhead + 1);
    CAPSLoadImage(caps_cont[drv], caps_flags);
    return 1;
}

int caps_loadtrack (uae_u16 *mfmbuf, uae_u16 **trackpointers, uae_u16 **tracktiming, int drv, int track, int *tracklengths, int *revolutions)
{
    struct CapsTrackInfo ci;
    int i, j, len;
    uae_u16 *tt, *mfm;

    *tracktiming = 0;
    CAPSLockTrack(&ci, caps_cont[drv], track / 2, track & 1, caps_flags);
    mfm = mfmbuf;
    *revolutions = ci.trackcnt;
    for (j = 0; j < ci.trackcnt; j++) {
	len = ci.tracksize[j];
	trackpointers[j] = mfm;
	tracklengths[j] = len * 8;
	for (i = 0; i < (len + 1) / 2; i++) {
	    uae_u8 *data = ci.trackdata[j]+ i * 2;
	    *mfm++ = 256 * *data + *(data + 1);
	}
    }
#if 0
    {
	FILE *f=fopen("c:\\1.txt","wb");
	fwrite (ci.trackdata[0], len, 1, f);
	fclose (f);
    }
#endif
    if (ci.timelen > 0) {
	tt = xmalloc (ci.timelen * sizeof (uae_u16));
	for (i = 0; i < ci.timelen; i++)
	    tt[i] = (uae_u16)ci.timebuf[i];
	*tracktiming = tt;
    }
#if 0
    write_log ("caps: drive: %d, track: %d, revolutions: %d, timing: %d\n",
	drv, track, *revolutions, ci.timelen);
    for (i = 0; i < *revolutions; i++)
	write_log ("- %d: length: %d bits, %d bytes\n", i, tracklengths[i], tracklengths[i] / 8);
#endif
    return 1;
}

#endif
