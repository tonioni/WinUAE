 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Drive Click Emulation Support Functions
  *
  * Copyright 2004 James Bagg, Toni Wilen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef DRIVESOUND

#include "uae.h"
#include "config.h"
#include "options.h"
#include "sounddep/sound.h"
#include "zfile.h"
#include "events.h"

#include "driveclick.h"

static struct drvsample drvs[4][DS_END];
static int freq = 44100;

static int drv_starting[4], drv_spinning[4], drv_has_spun[4], drv_has_disk[4];

static int click_initialized;
#define DS_SHIFT 10
static int sample_step;

static uae_s16 *clickbuffer;

uae_s16 *decodewav (uae_u8 *s, int *lenp)
{
    uae_s16 *dst;
    uae_u8 *src = s;
    int len;

    if (memcmp (s, "RIFF", 4))
	return 0;
    if (memcmp (s + 8, "WAVE", 4))
	return 0;
    s += 12;
    len = *lenp;
    while (s < src + len) {
	if (!memcmp (s, "fmt ", 4))
	    freq = s[8 + 4] | (s[8 + 5] << 8);
	if (!memcmp (s, "data", 4)) {
	    s += 4;
	    len = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
	    dst = xmalloc (len);
	    memcpy (dst, s + 4, len);
	    *lenp = len / 2;
	    return dst;
	}
	s += 8 + (s[4] | (s[5] << 8) | (s[6] << 16) | (s[7] << 24));
    }
    return 0;
}

static int loadsample (char *path, struct drvsample *ds)
{
    struct zfile *f;
    uae_u8 *buf;
    int size;
    char name[MAX_DPATH];

    f = zfile_fopen (path, "rb");
    if (!f) {
	strcpy (name, path);
	strcat (name, ".wav");
	f = zfile_fopen (name, "rb");
	if (!f) {
	    write_log ("driveclick: can't open '%s' (or '%s')\n", path, name);
	    return 0;
	}
    }
    zfile_fseek (f, 0, SEEK_END);
    size = zfile_ftell (f);
    buf = xmalloc (size);
    zfile_fseek (f, 0, SEEK_SET);
    zfile_fread (buf, size, 1, f);
    zfile_fclose (f);
    ds->len = size;
    ds->p = decodewav (buf, &ds->len);
    xfree (buf);
    return 1;
}

static void freesample (struct drvsample *s)
{
    xfree (s->p);
    s->p = 0;
}

static void processclicks(struct drvsample *ds) 
{
    unsigned int n = 0;
    unsigned int nClick = 0;

    for (n = 0; n < CLICK_TRACKS; n++)  {
	ds->indexes[n] = 0;
	ds->lengths[n] = 0;
    }
    for(n = 0; n < ds->len; n++) {
	uae_s16 smp = ds->p[n];
	if (smp > 0x6ff0 && nClick < CLICK_TRACKS)  {
		ds->indexes[nClick] = n - 128;
		ds->lengths[nClick] = 2800;
		nClick ++;
		n += 3000;
	}
    }
    if (nClick == 0) {
        for(n = 0; n < CLICK_TRACKS; n++) {
            ds->indexes[n] = 0;
            ds->lengths[n] = ds->len;
        }
    } else {
        if (nClick == 1) {	
	    ds->lengths[0] = ds->len - ds->indexes[0];
	    for(n = 1; n < CLICK_TRACKS; n++) {
	        ds->indexes[n] = ds->indexes[0];
	        ds->lengths[n] = ds->lengths[0];
	    }
	} else  {
	    for(n = nClick; n < CLICK_TRACKS; n++) {
	        ds->indexes[n] = ds->indexes[nClick-1];
	        ds->lengths[n] = ds->lengths[nClick-1]; 
	    }
	}
    }
}
void driveclick_init(void)
{
    int v, vv, i, j;
    char tmp[MAX_DPATH];

    driveclick_free ();
    vv = 0;
    for (i = 0; i < 4; i++) {
	for (j = 0; j < CLICK_TRACKS; j++)  {
   	    drvs[i][DS_CLICK].indexes[j] = 0;
	    drvs[i][DS_CLICK].lengths[j] = 0;
	}
	if (currprefs.dfxclick[i]) {
	    if (currprefs.dfxclick[i] > 0) {
		v = 0;
		if (driveclick_loadresource (drvs[i], currprefs.dfxclick[i]))
		    v = 3;
		for (j = 0; j < CLICK_TRACKS; j++)
		    drvs[i][DS_CLICK].lengths[j] = drvs[i][DS_CLICK].len;
	    } else if (currprefs.dfxclick[i] == -1) {
		for (j = 0; j < CLICK_TRACKS; j++)
		    drvs[i][DS_CLICK].lengths[j] = drvs[i][DS_CLICK].len;
		sprintf (tmp, "%suae_data%cdrive_click_%s",
		    start_path_data, FSDB_DIR_SEPARATOR, currprefs.dfxclickexternal[i]);
		v = loadsample (tmp, &drvs[i][DS_CLICK]);
		if (v)
		    processclicks (&drvs[i][DS_CLICK]);
		sprintf (tmp, "%suae_data%cdrive_spin_%s",
		    start_path_data, FSDB_DIR_SEPARATOR, currprefs.dfxclickexternal[i]);
		v += loadsample (tmp, &drvs[i][DS_SPIN]);
		sprintf (tmp, "%suae_data%cdrive_spinnd_%s",
		    start_path_data, FSDB_DIR_SEPARATOR, currprefs.dfxclickexternal[i]);
		v += loadsample (tmp, &drvs[i][DS_SPINND]);
		sprintf (tmp, "%suae_data%cdrive_startup_%s",
		    start_path_data, FSDB_DIR_SEPARATOR, currprefs.dfxclickexternal[i]);
		v += loadsample (tmp, &drvs[i][DS_START]);
		sprintf (tmp, "%suae_data%cdrive_snatch_%s",
		    start_path_data, FSDB_DIR_SEPARATOR, currprefs.dfxclickexternal[i]);
		v += loadsample (tmp, &drvs[i][DS_SNATCH]);
	    }
	    if (v == 0) {
		int j;
		for (j = 0; j < DS_END; j++)
		    freesample (&drvs[i][j]);
		currprefs.dfxclick[i] = changed_prefs.dfxclick[i] = 0;
	    } else {
		vv++;
	    }
	    for (j = 0; j < DS_END; j++)
		drvs[i][j].len <<= DS_SHIFT;
	    drvs[i][DS_CLICK].pos = drvs[i][DS_CLICK].len;
	    drvs[i][DS_SNATCH].pos = drvs[i][DS_SNATCH].len;
	}
    }
    if (vv > 0) {
	driveclick_reset ();
	click_initialized = 1;
    }
}

void driveclick_reset (void)
{
    xfree (clickbuffer);
    clickbuffer = xmalloc (sndbufsize);
    sample_step = (freq << DS_SHIFT) / currprefs.sound_freq;
}

void driveclick_free (void)
{
    int i, j;

    for (i = 0; i < 4; i++) {
	for (j = 0; j < DS_END; j++)
	    freesample (&drvs[i][j]);
    }
    memset (drvs, 0, sizeof (drvs));
    xfree (clickbuffer);
    clickbuffer = 0;
    click_initialized = 0;
}

STATIC_INLINE uae_s16 getsample(void)
{
    uae_s32 smp = 0;
    int div = 0, i;

    for (i = 0; i < 4; i++) {
	if (currprefs.dfxclick[i]) {
	    struct drvsample *ds_start = &drvs[i][DS_START];
	    struct drvsample *ds_spin = drv_has_disk[i] ? &drvs[i][DS_SPIN] : &drvs[i][DS_SPINND];
	    struct drvsample *ds_click = &drvs[i][DS_CLICK];
	    struct drvsample *ds_snatch = &drvs[i][DS_SNATCH];
	    div += 2;
	    if (drv_spinning[i] || drv_starting[i]) {
		if (drv_starting[i] && drv_has_spun[i]) {
		    if (ds_start->p && ds_start->pos < ds_start->len) {
			smp = ds_start->p[ds_start->pos >> DS_SHIFT];
			ds_start->pos += sample_step;
		    } else {
			drv_starting[i] = 0;
		    }
		} else if (drv_starting[i] && drv_has_spun[i] == 0) {
		    if (ds_snatch->p && ds_snatch->pos < ds_snatch->len) {
			smp = ds_snatch->p[ds_snatch->pos >> DS_SHIFT];
			ds_snatch->pos += sample_step;
		    } else {
			drv_starting[i] = 0;
			ds_start->pos = ds_start->len;
			drv_has_spun[i] = 1;
		    }
		}
		if (ds_spin->p && drv_starting[i] == 0) {
		    if (ds_spin->pos >= ds_spin->len)
			ds_spin->pos -= ds_spin->len;
		    smp = ds_spin->p[ds_spin->pos >> DS_SHIFT];
		    ds_spin->pos += sample_step;
		}
	    }
	    if (ds_click->p && ds_click->pos < ds_click->len) {
		smp += ds_click->p[ds_click->pos >> DS_SHIFT];
		ds_click->pos += sample_step;
	    }
	}
    }
    if (!div)
	return 0;
    return smp / div;
}

static int clickcnt;

static void mix (void)
{
    int total = ((uae_u8*)sndbufpt - (uae_u8*)sndbuffer) / (currprefs.sound_stereo ? 4 : 2);
    
    if (currprefs.dfxclickvolume > 0) {
	while (clickcnt < total) {
	    clickbuffer[clickcnt++] = getsample() * (100 - currprefs.dfxclickvolume) / 100;
	}
    } else {
	while (clickcnt < total) {
	    clickbuffer[clickcnt++] = getsample();
	}
    }
}

STATIC_INLINE uae_s16 limit (uae_s32 v)
{
    if (v < -32768)
	v = -32768;
    if (v > 32767)
	v = 32767;
    return v;
}

void driveclick_mix (uae_s16 *sndbuffer, int size)
{
    int i;

    if (!click_initialized)
	return;
    mix();
    clickcnt = 0;
    if (currprefs.sound_stereo) {
        for (i = 0; i < size / 2; i++) {
	    uae_s16 s = clickbuffer[i];
	    sndbuffer[0] = limit(((sndbuffer[0] + s) * 2) / 3);
	    sndbuffer[1] = limit(((sndbuffer[1] + s) * 2) / 3);
	    sndbuffer += 2;
        }
    } else {
        for (i = 0; i < size; i++) {
	    sndbuffer[0] = limit(((sndbuffer[0] + clickbuffer[i]) * 2) / 3);
	    sndbuffer++;
	}
    }
}

void driveclick_click (int drive, int cyl)
{
    static int prevcyl[4];
    if (!click_initialized)
	return;
    if (!currprefs.dfxclick[drive])
	return;
    if (prevcyl[drive] == 0 && cyl == 0) // "noclick" check
	return;
    prevcyl[drive] = cyl;
    mix();
    drvs[drive][DS_CLICK].pos = drvs[drive][DS_CLICK].indexes[cyl] << DS_SHIFT;
    drvs[drive][DS_CLICK].len = (drvs[drive][DS_CLICK].indexes[cyl] + (drvs[drive][DS_CLICK].lengths[cyl] / 2)) << DS_SHIFT;
}

void driveclick_motor (int drive, int running)
{
    if (!click_initialized)
	return;
    if (!currprefs.dfxclick[drive])
	return;
    mix();
    if (running == 0) {
	drv_starting[drive] = 0;
	drv_spinning[drive] = 0;
    } else {
        if (drv_spinning[drive] == 0) {
	    drv_starting[drive] = 1;
	    drv_spinning[drive] = 1;
	    if (drv_has_disk[drive] && drv_has_spun[drive] == 0 && drvs[drive][DS_SNATCH].pos >= drvs[drive][DS_SNATCH].len)
		drvs[drive][DS_SNATCH].pos = 0;
	    if (running == 2)
		drvs[drive][DS_START].pos = 0;
	    drvs[drive][DS_SPIN].pos = 0;
	}
    }
}

void driveclick_insert (int drive, int eject)
{
    if (!click_initialized)
	return;
    if (!currprefs.dfxclick[drive])
	return;
    if (eject)
	drv_has_spun[drive] = 0;
    drv_has_disk[drive] = !eject;
}

void driveclick_check_prefs (void)
{
    int i;

    if (currprefs.dfxclickvolume != changed_prefs.dfxclickvolume ||
	currprefs.dfxclick[0] != changed_prefs.dfxclick[0] ||
	currprefs.dfxclick[1] != changed_prefs.dfxclick[1] ||
	currprefs.dfxclick[2] != changed_prefs.dfxclick[2] ||
	currprefs.dfxclick[3] != changed_prefs.dfxclick[3] ||
	strcmp (currprefs.dfxclickexternal[0], changed_prefs.dfxclickexternal[0]) ||
	strcmp (currprefs.dfxclickexternal[1], changed_prefs.dfxclickexternal[1]) ||
	strcmp (currprefs.dfxclickexternal[2], changed_prefs.dfxclickexternal[2]) ||
	strcmp (currprefs.dfxclickexternal[3], changed_prefs.dfxclickexternal[3]))
    {
	currprefs.dfxclickvolume = changed_prefs.dfxclickvolume;
	for (i = 0; i < 4; i++) {
	    currprefs.dfxclick[i] = changed_prefs.dfxclick[i];
	    strcpy (currprefs.dfxclickexternal[i], changed_prefs.dfxclickexternal[i]);
	}
	driveclick_init ();
    }
}

#endif