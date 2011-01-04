/*
* UAE - The Un*x Amiga Emulator
*
* Save/restore emulator state
*
* (c) 1999-2001 Toni Wilen
*
* see below for ASF-structure
*/

/* Features:
*
* - full CPU state (68000/68010/68020/68030/68040/68060)
* - FPU (68881/68882/68040/68060)
* - full CIA-A and CIA-B state (with all internal registers)
* - saves all custom registers and audio internal state.
* - Chip, Bogo, Fast, Z3 and Picasso96 RAM supported
* - disk drive type, imagefile, track and motor state
* - Kickstart ROM version, address and size is saved. This data is not used during restore yet.
* - Action Replay state is saved
*/

/* Notes:
*
* - blitter state is not saved, blitter is forced to finish immediately if it
*   was active
* - disk DMA state is completely saved
* - does not ask for statefile name and description. Currently uses DF0's disk
*   image name (".adf" is replaced with ".asf")
* - only Amiga state is restored, harddisk support, autoconfig, expansion boards etc..
*   are not saved/restored (and probably never will).
* - use this for saving games that can't be saved to disk
*/

/* Usage :
*
* save:
*
* set savestate_state = STATE_DOSAVE, savestate_filename = "..."
*
* restore:
*
* set savestate_state = STATE_DORESTORE, savestate_filename = "..."
*
*/

#define OPEN_LOG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "zfile.h"
#include "ar.h"
#include "autoconf.h"
#include "custom.h"
#include "newcpu.h"
#include "savestate.h"
#include "uae.h"
#include "gui.h"
#include "audio.h"
#include "filesys.h"
#include "inputrecord.h"
#include "disk.h"

int savestate_state = 0;
static int savestate_first_capture;

static bool new_blitter = false;

static int replaycounter;

struct zfile *savestate_file;
static int savestate_docompress, savestate_specialdump, savestate_nodialogs;

TCHAR savestate_fname[MAX_DPATH];

#define STATEFILE_ALLOC_SIZE 600000
static int statefile_alloc;
static int staterecords_max = 1000;
static int staterecords_first = 0;
static struct zfile *staterecord_statefile;
struct staterecord
{
	int len;
	int inuse;
	uae_u8 *cpu;
	uae_u8 *data;
	uae_u8 *end;
	int inprecoffset;
};

static struct staterecord **staterecords;

static void state_incompatible_warn (void)
{
	static int warned;
	int dowarn = 0;
	int i;

#ifdef BSDSOCKET
	if (currprefs.socket_emu)
		dowarn = 1;
#endif
#ifdef UAESERIAL
	if (currprefs.uaeserial)
		dowarn = 1;
#endif
#ifdef SCSIEMU
	if (currprefs.scsi)
		dowarn = 1;
#endif
#ifdef CATWEASEL
	if (currprefs.catweasel)
		dowarn = 1;
#endif
#ifdef FILESYS
	for(i = 0; i < currprefs.mountitems; i++) {
		struct mountedinfo mi;
		int type = get_filesys_unitconfig (&currprefs, i, &mi);
		if (mi.ismounted && type != FILESYS_VIRTUAL && type != FILESYS_HARDFILE && type != FILESYS_HARDFILE_RDB)
			dowarn = 1;
	}
#endif
	if (!warned && dowarn) {
		warned = 1;
		notify_user (NUMSG_STATEHD);
	}
}

/* functions for reading/writing bytes, shorts and longs in big-endian
* format independent of host machine's endianess */

static uae_u8 *storepos;
void save_store_pos_func (uae_u8 **dstp)
{
	storepos = *dstp;
	*dstp += 4;
}
void save_store_size_func (uae_u8 **dstp)
{
	uae_u8 *p = storepos;
	save_u32_func (&p, *dstp - storepos);
}
void restore_store_pos_func (uae_u8 **srcp)
{
	storepos = *srcp;
	*srcp += 4;
}
void restore_store_size_func (uae_u8 **srcp)
{
	uae_u8 *p = storepos;
	uae_u32 len = restore_u32_func (&p);
	*srcp = storepos + len;
}

void save_u32_func (uae_u8 **dstp, uae_u32 v)
{
	uae_u8 *dst = *dstp;
	*dst++ = (uae_u8)(v >> 24);
	*dst++ = (uae_u8)(v >> 16);
	*dst++ = (uae_u8)(v >> 8);
	*dst++ = (uae_u8)(v >> 0);
	*dstp = dst;
}
void save_u64_func (uae_u8 **dstp, uae_u64 v)
{
	save_u32_func (dstp, (uae_u32)(v >> 32));
	save_u32_func (dstp, (uae_u32)v);
}
void save_u16_func (uae_u8 **dstp, uae_u16 v)
{
	uae_u8 *dst = *dstp;
	*dst++ = (uae_u8)(v >> 8);
	*dst++ = (uae_u8)(v >> 0);
	*dstp = dst;
}
void save_u8_func (uae_u8 **dstp, uae_u8 v)
{
	uae_u8 *dst = *dstp;
	*dst++ = v;
	*dstp = dst;
}
void save_string_func (uae_u8 **dstp, const TCHAR *from)
{
	uae_u8 *dst = *dstp;
	char *s, *s2;
	s2 = s = uutf8 (from);
	while (s && *s)
		*dst++ = *s++;
	*dst++ = 0;
	*dstp = dst;
	xfree (s2);
}
void save_path_func (uae_u8 **dstp, const TCHAR *from, int type)
{
	save_string_func (dstp, from);
}

uae_u32 restore_u32_func (uae_u8 **dstp)
{
	uae_u32 v;
	uae_u8 *dst = *dstp;
	v = (dst[0] << 24) | (dst[1] << 16) | (dst[2] << 8) | (dst[3]);
	*dstp = dst + 4;
	return v;
}
uae_u64 restore_u64_func (uae_u8 **dstp)
{
	uae_u64 v;

	v = restore_u32_func (dstp);
	v <<= 32;
	v |= restore_u32_func (dstp);
	return v;
}
uae_u16 restore_u16_func (uae_u8 **dstp)
{
	uae_u16 v;
	uae_u8 *dst = *dstp;
	v=(dst[0] << 8) | (dst[1]);
	*dstp = dst + 2;
	return v;
}
uae_u8 restore_u8_func (uae_u8 **dstp)
{
	uae_u8 v;
	uae_u8 *dst = *dstp;
	v = dst[0];
	*dstp = dst + 1;
	return v;
}
TCHAR *restore_string_func (uae_u8 **dstp)
{
	int len;
	uae_u8 v;
	uae_u8 *dst = *dstp;
	char *top, *to;
	TCHAR *s;

	len = strlen ((char*)dst) + 1;
	top = to = xmalloc (char, len);
	do {
		v = *dst++;
		*top++ = v;
	} while (v);
	*dstp = dst;
	s = utf8u (to);
	xfree (to);
	return s;
}
TCHAR *restore_path_func (uae_u8 **dstp, int type)
{
	TCHAR *newpath;
	TCHAR *s = restore_string_func (dstp);
	TCHAR *out = NULL;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];

	if (s[0] == 0)
		return s;
	if (zfile_exists (s))
		return s;
	if (type == SAVESTATE_PATH_HD)
		return s;
	getfilepart (tmp, sizeof tmp / sizeof (TCHAR), s);
	if (zfile_exists (tmp)) {
		xfree (s);
		return my_strdup (tmp);
	}
	for (int i = 0; i < MAX_PATHS; i++) {
		newpath = NULL;
		if (type == SAVESTATE_PATH_FLOPPY)
			newpath = currprefs.path_floppy.path[i];
		else if (type == SAVESTATE_PATH_VDIR || type == SAVESTATE_PATH_HDF)
			newpath = currprefs.path_hardfile.path[i];
		else if (type == SAVESTATE_PATH_CD)
			newpath = currprefs.path_cd.path[i];
		if (newpath == NULL || newpath[0] == 0)
			break;
		_tcscpy (tmp2, newpath);
		_tcscat (tmp2, tmp);
		fullpath (tmp2, sizeof tmp2 / sizeof (TCHAR));
		if (zfile_exists (tmp2)) {
			xfree (s);
			return my_strdup (tmp2);
		}
	}
	getpathpart(tmp2, sizeof tmp2 / sizeof (TCHAR), savestate_fname);
	_tcscat (tmp2, tmp);
	if (zfile_exists (tmp2)) {
		xfree (s);
		return my_strdup (tmp2);
	}
	return s;
}

/* read and write IFF-style hunks */

static void save_chunk (struct zfile *f, uae_u8 *chunk, size_t len, TCHAR *name, int compress)
{
	uae_u8 tmp[8], *dst;
	uae_u8 zero[4]= { 0, 0, 0, 0 };
	uae_u32 flags;
	size_t pos;
	size_t chunklen, len2;
	char *s;

	if (!chunk)
		return;

	if (compress < 0) {
		zfile_fwrite (chunk, 1, len, f);
		return;
	}

	/* chunk name */
	s = ua (name);
	zfile_fwrite (s, 1, 4, f);
	xfree (s);
	pos = zfile_ftell (f);
	/* chunk size */
	dst = &tmp[0];
	chunklen = len + 4 + 4 + 4;
	save_u32 (chunklen);
	zfile_fwrite (&tmp[0], 1, 4, f);
	/* chunk flags */
	flags = 0;
	dst = &tmp[0];
	save_u32 (flags | compress);
	zfile_fwrite (&tmp[0], 1, 4, f);
	/* chunk data */
	if (compress) {
		int tmplen = len;
		size_t opos;
		dst = &tmp[0];
		save_u32 (len);
		opos = zfile_ftell (f);
		zfile_fwrite (&tmp[0], 1, 4, f);
		len = zfile_zcompress (f, chunk, len);
		if (len > 0) {
			zfile_fseek (f, pos, SEEK_SET);
			dst = &tmp[0];
			save_u32 (len + 4 + 4 + 4 + 4);
			zfile_fwrite (&tmp[0], 1, 4, f);
			zfile_fseek (f, 0, SEEK_END);
		} else {
			len = tmplen;
			compress = 0;
			zfile_fseek (f, opos, SEEK_SET);
			dst = &tmp[0];
			save_u32 (flags);
			zfile_fwrite (&tmp[0], 1, 4, f);
		}
	}
	if (!compress)
		zfile_fwrite (chunk, 1, len, f);
	/* alignment */
	len2 = 4 - (len & 3);
	if (len2)
		zfile_fwrite (zero, 1, len2, f);

	write_log (L"Chunk '%s' chunk size %d (%d)\n", name, chunklen, len);
}

static uae_u8 *restore_chunk (struct zfile *f, TCHAR *name, size_t *len, size_t *totallen, size_t *filepos)
{
	uae_u8 tmp[6], dummy[4], *mem, *src;
	uae_u32 flags;
	int len2;

	*totallen = 0;
	/* chunk name */
	zfile_fread (tmp, 1, 4, f);
	tmp[4] = 0;
	au_copy (name, 5, (char*)tmp);
	/* chunk size */
	zfile_fread (tmp, 1, 4, f);
	src = tmp;
	len2 = restore_u32 () - 4 - 4 - 4;
	if (len2 < 0)
		len2 = 0;
	*len = len2;
	if (len2 == 0) {
		*filepos = zfile_ftell (f);
		return 0;
	}

	/* chunk flags */
	zfile_fread (tmp, 1, 4, f);
	src = tmp;
	flags = restore_u32 ();
	*totallen = *len;
	if (flags & 1) {
		zfile_fread (tmp, 1, 4, f);
		src = tmp;
		*totallen = restore_u32 ();
		*filepos = zfile_ftell (f) - 4 - 4 - 4;
		len2 -= 4;
	} else {
		*filepos = zfile_ftell (f) - 4 - 4;
	}
	/* chunk data.  RAM contents will be loaded during the reset phase,
	   no need to malloc multiple megabytes here.  */
	if (_tcscmp (name, L"CRAM") != 0
		&& _tcscmp (name, L"BRAM") != 0
		&& _tcscmp (name, L"FRAM") != 0
		&& _tcscmp (name, L"ZRAM") != 0
		&& _tcscmp (name, L"ZCRM") != 0
		&& _tcscmp (name, L"PRAM") != 0
		&& _tcscmp (name, L"A3K1") != 0
		&& _tcscmp (name, L"A3K2") != 0)
	{
		/* extra bytes at the end needed to handle old statefiles that now have new fields */
		mem = xcalloc (uae_u8, *totallen + 100);
		if (!mem)
			return NULL;
		if (flags & 1) {
			zfile_zuncompress (mem, *totallen, f, len2);
		} else {
			zfile_fread (mem, 1, len2, f);
		}
	} else {
		mem = 0;
		zfile_fseek (f, len2, SEEK_CUR);
	}

	/* alignment */
	len2 = 4 - (len2 & 3);
	if (len2)
		zfile_fread (dummy, 1, len2, f);
	return mem;
}

void restore_ram (size_t filepos, uae_u8 *memory)
{
	uae_u8 tmp[8];
	uae_u8 *src = tmp;
	int size, fullsize;
	uae_u32 flags;

	if (filepos == 0 || memory == NULL)
		return;
	zfile_fseek (savestate_file, filepos, SEEK_SET);
	zfile_fread (tmp, 1, sizeof tmp, savestate_file);
	size = restore_u32 ();
	flags = restore_u32 ();
	size -= 4 + 4 + 4;
	if (flags & 1) {
		zfile_fread (tmp, 1, 4, savestate_file);
		src = tmp;
		fullsize = restore_u32 ();
		size -= 4;
		zfile_zuncompress (memory, fullsize, savestate_file, size);
	} else {
		zfile_fread (memory, 1, size, savestate_file);
	}
}

static uae_u8 *restore_log (uae_u8 *src)
{
#if OPEN_LOG > 0
	TCHAR *s = utf8u (src);
	write_log (L"%s\n", s);
	xfree (s);
#endif
	src += strlen ((char*)src) + 1;
	return src;
}

static void restore_header (uae_u8 *src)
{
	TCHAR *emuname, *emuversion, *description;

	restore_u32 ();
	emuname = restore_string ();
	emuversion = restore_string ();
	description = restore_string ();
	write_log (L"Saved with: '%s %s', description: '%s'\n",
		emuname, emuversion, description);
	xfree (description);
	xfree (emuversion);
	xfree (emuname);
}

/* restore all subsystems */

void restore_state (const TCHAR *filename)
{
	struct zfile *f;
	uae_u8 *chunk,*end;
	TCHAR name[5];
	size_t len, totallen;
	size_t filepos, filesize;
	int z3num;

	chunk = 0;
	f = zfile_fopen (filename, L"rb", ZFD_NORMAL);
	if (!f)
		goto error;
	zfile_fseek (f, 0, SEEK_END);
	filesize = zfile_ftell (f);
	zfile_fseek (f, 0, SEEK_SET);
	savestate_state = STATE_RESTORE;
	savestate_init ();

	chunk = restore_chunk (f, name, &len, &totallen, &filepos);
	if (!chunk || _tcsncmp (name, L"ASF ", 4)) {
		write_log (L"%s is not an AmigaStateFile\n", filename);
		goto error;
	}
	write_log (L"STATERESTORE:\n");
	config_changed = 1;
	savestate_file = f;
	restore_header (chunk);
	xfree (chunk);
	changed_prefs.bogomem_size = 0;
	changed_prefs.chipmem_size = 0;
	changed_prefs.fastmem_size = 0;
	changed_prefs.z3fastmem_size = 0;
	changed_prefs.z3fastmem2_size = 0;
	changed_prefs.mbresmem_low_size = 0;
	changed_prefs.mbresmem_high_size = 0;
	z3num = 0;
	for (;;) {
		name[0] = 0;
		chunk = end = restore_chunk (f, name, &len, &totallen, &filepos);
		write_log (L"Chunk '%s' size %d (%d)\n", name, len, totallen);
		if (!_tcscmp (name, L"END ")) {
#ifdef _DEBUG
			if (filesize > filepos + 8)
				continue;
#endif
			break;
		}
		if (!_tcscmp (name, L"CRAM")) {
			restore_cram (totallen, filepos);
			continue;
		} else if (!_tcscmp (name, L"BRAM")) {
			restore_bram (totallen, filepos);
			continue;
		} else if (!_tcscmp (name, L"A3K1")) {
			restore_a3000lram (totallen, filepos);
			continue;
		} else if (!_tcscmp (name, L"A3K2")) {
			restore_a3000hram (totallen, filepos);
			continue;
#ifdef AUTOCONFIG
		} else if (!_tcscmp (name, L"FRAM")) {
			restore_fram (totallen, filepos);
			continue;
		} else if (!_tcscmp (name, L"ZRAM")) {
			restore_zram (totallen, filepos, z3num++);
			continue;
		} else if (!_tcscmp (name, L"ZCRM")) {
			restore_zram (totallen, filepos, -1);
			continue;
		} else if (!_tcscmp (name, L"BORO")) {
			restore_bootrom (totallen, filepos);
			continue;
#endif
#ifdef PICASSO96
		} else if (!_tcscmp (name, L"PRAM")) {
			restore_pram (totallen, filepos);
			continue;
#endif
		} else if (!_tcscmp (name, L"CYCS")) {
			end = restore_cycles (chunk);
		} else if (!_tcscmp (name, L"CPU ")) {
			end = restore_cpu (chunk);
		} else if (!_tcscmp (name, L"CPUX"))
			end = restore_cpu_extra (chunk);
		else if (!_tcscmp (name, L"CPUT"))
			end = restore_cpu_trace (chunk);
#ifdef FPUEMU
		else if (!_tcscmp (name, L"FPU "))
			end = restore_fpu (chunk);
#endif
#ifdef MMUEMU
		else if (!_tcscmp (name, L"MMU "))
			end = restore_mmu (chunk);
#endif
		else if (!_tcscmp (name, L"AGAC"))
			end = restore_custom_agacolors (chunk);
		else if (!_tcscmp (name, L"SPR0"))
			end = restore_custom_sprite (0, chunk);
		else if (!_tcscmp (name, L"SPR1"))
			end = restore_custom_sprite (1, chunk);
		else if (!_tcscmp (name, L"SPR2"))
			end = restore_custom_sprite (2, chunk);
		else if (!_tcscmp (name, L"SPR3"))
			end = restore_custom_sprite (3, chunk);
		else if (!_tcscmp (name, L"SPR4"))
			end = restore_custom_sprite (4, chunk);
		else if (!_tcscmp (name, L"SPR5"))
			end = restore_custom_sprite (5, chunk);
		else if (!_tcscmp (name, L"SPR6"))
			end = restore_custom_sprite (6, chunk);
		else if (!_tcscmp (name, L"SPR7"))
			end = restore_custom_sprite (7, chunk);
		else if (!_tcscmp (name, L"CIAA"))
			end = restore_cia (0, chunk);
		else if (!_tcscmp (name, L"CIAB"))
			end = restore_cia (1, chunk);
		else if (!_tcscmp (name, L"CHIP"))
			end = restore_custom (chunk);
		else if (!_tcscmp (name, L"CINP"))
			end = restore_input (chunk);
		else if (!_tcscmp (name, L"CHPX"))
			end = restore_custom_extra (chunk);
		else if (!_tcscmp (name, L"CHPD"))
			end = restore_custom_event_delay (chunk);
		else if (!_tcscmp (name, L"AUD0"))
			end = restore_audio (0, chunk);
		else if (!_tcscmp (name, L"AUD1"))
			end = restore_audio (1, chunk);
		else if (!_tcscmp (name, L"AUD2"))
			end = restore_audio (2, chunk);
		else if (!_tcscmp (name, L"AUD3"))
			end = restore_audio (3, chunk);
		else if (!_tcscmp (name, L"BLIT"))
			end = restore_blitter (chunk);
		else if (!_tcscmp (name, L"BLTX"))
			end = restore_blitter_new (chunk);
		else if (!_tcscmp (name, L"DISK"))
			end = restore_floppy (chunk);
		else if (!_tcscmp (name, L"DSK0"))
			end = restore_disk (0, chunk);
		else if (!_tcscmp (name, L"DSK1"))
			end = restore_disk (1, chunk);
		else if (!_tcscmp (name, L"DSK2"))
			end = restore_disk (2, chunk);
		else if (!_tcscmp (name, L"DSK3"))
			end = restore_disk (3, chunk);
		else if (!_tcscmp (name, L"DSD0"))
			end = restore_disk2 (0, chunk);
		else if (!_tcscmp (name, L"DSD1"))
			end = restore_disk2 (1, chunk);
		else if (!_tcscmp (name, L"DSD2"))
			end = restore_disk2 (2, chunk);
		else if (!_tcscmp (name, L"DSD3"))
			end = restore_disk2 (3, chunk);
		else if (!_tcscmp (name, L"KEYB"))
			end = restore_keyboard (chunk);
#ifdef AUTOCONFIG
		else if (!_tcscmp (name, L"EXPA"))
			end = restore_expansion (chunk);
#endif
		else if (!_tcscmp (name, L"ROM "))
			end = restore_rom (chunk);
#ifdef PICASSO96
		else if (!_tcscmp (name, L"P96 "))
			end = restore_p96 (chunk);
#endif
#ifdef ACTION_REPLAY
		else if (!_tcscmp (name, L"ACTR"))
			end = restore_action_replay (chunk);
		else if (!_tcscmp (name, L"HRTM"))
			end = restore_hrtmon (chunk);
#endif
#ifdef FILESYS
		else if (!_tcscmp (name, L"FSYS"))
			end = restore_filesys (chunk);
		else if (!_tcscmp (name, L"FSYC"))
			end = restore_filesys_common (chunk);
#endif
#ifdef CD32
		else if (!_tcscmp (name, L"CD32"))
			end = restore_akiko (chunk);
#endif
#ifdef CDTV
		else if (!_tcscmp (name, L"CDTV"))
			end = restore_cdtv (chunk);
		else if (!_tcscmp (name, L"DMAC"))
			end = restore_dmac (chunk);
#endif
		else if (!_tcscmp (name, L"GAYL"))
			end = restore_gayle (chunk);
		else if (!_tcscmp (name, L"IDE "))
			end = restore_ide (chunk);
		else if (!_tcsncmp (name, L"CDU", 3))
			end = restore_cd (name[3] - '0', chunk);
#ifdef A2065
		else if (!_tcsncmp (name, L"2065", 4))
			end = restore_a2065 (chunk);
#endif
		else if (!_tcsncmp (name, L"DMWP", 4))
			end = restore_debug_memwatch (chunk);

		else if (!_tcscmp (name, L"CONF"))
			end = restore_configuration (chunk);
		else if (!_tcscmp (name, L"LOG "))
			end = restore_log (chunk);
		else {
			end = chunk + len;
			write_log (L"unknown chunk '%s' size %d bytes\n", name, len);
		}
		if (end == NULL)
			write_log (L"Chunk '%s', size %d bytes was not accepted!\n",
			name, len);
		else if (totallen != end - chunk)
			write_log (L"Chunk '%s' total size %d bytes but read %d bytes!\n",
			name, totallen, end - chunk);
		xfree (chunk);
	}
	target_addtorecent (filename, 0);
	return;

error:
	savestate_state = 0;
	savestate_file = 0;
	if (chunk)
		xfree (chunk);
	if (f)
		zfile_fclose (f);
}

void savestate_restore_finish (void)
{
	if (!isrestore ())
		return;
	zfile_fclose (savestate_file);
	savestate_file = 0;
	restore_cpu_finish ();
	restore_audio_finish ();
	restore_disk_finish ();
	restore_blitter_finish ();
	restore_akiko_finish ();
	restore_cdtv_finish ();
	restore_p96_finish ();
	restore_a2065_finish ();
	restore_cia_finish ();
	savestate_state = 0;
	init_hz_full ();
	audio_activate ();
}

/* 1=compressed,2=not compressed,3=ram dump,4=audio dump */
void savestate_initsave (const TCHAR *filename, int mode, int nodialogs, bool save)
{
	if (filename == NULL) {
		savestate_fname[0] = 0;
		savestate_docompress = 0;
		savestate_specialdump = 0;
		savestate_nodialogs = 0;
		return;
	}
	_tcscpy (savestate_fname, filename);
	savestate_docompress = (mode == 1) ? 1 : 0;
	savestate_specialdump = (mode == 3) ? 1 : (mode == 4) ? 2 : 0;
	savestate_nodialogs = nodialogs;
	new_blitter = false;
	if (save) {
		savestate_free ();
		inprec_close (true);
	}
}

static void save_rams (struct zfile *f, int comp)
{
	uae_u8 *dst;
	int len;

	dst = save_cram (&len);
	save_chunk (f, dst, len, L"CRAM", comp);
	dst = save_bram (&len);
	save_chunk (f, dst, len, L"BRAM", comp);
	dst = save_a3000lram (&len);
	save_chunk (f, dst, len, L"A3K1", comp);
	dst = save_a3000hram (&len);
	save_chunk (f, dst, len, L"A3K2", comp);
#ifdef AUTOCONFIG
	dst = save_fram (&len);
	save_chunk (f, dst, len, L"FRAM", comp);
	dst = save_zram (&len, 0);
	save_chunk (f, dst, len, L"ZRAM", comp);
	dst = save_zram (&len, 1);
	save_chunk (f, dst, len, L"ZRAM", comp);
	dst = save_zram (&len, -1);
	save_chunk (f, dst, len, L"ZCRM", comp);
	dst = save_bootrom (&len);
	save_chunk (f, dst, len, L"BORO", comp);
#endif
#ifdef PICASSO96
	dst = save_pram (&len);
	save_chunk (f, dst, len, L"PRAM", comp);
#endif
}

/* Save all subsystems */

static int save_state_internal (struct zfile *f, const TCHAR *description, int comp, bool savepath)
{
	uae_u8 endhunk[] = { 'E', 'N', 'D', ' ', 0, 0, 0, 8 };
	uae_u8 header[1000];
	TCHAR tmp[100];
	uae_u8 *dst;
	TCHAR name[5];
	int i, len;

	write_log (L"STATESAVE (%s):\n", f ? zfile_getname (f) : L"<internal>");
	dst = header;
	save_u32 (0);
	save_string (L"UAE");
	_stprintf (tmp, L"%d.%d.%d", UAEMAJOR, UAEMINOR, UAESUBREV);
	save_string (tmp);
	save_string (description);
	save_chunk (f, header, dst-header, L"ASF ", 0);

	dst = save_cycles (&len, 0);
	save_chunk (f, dst, len, L"CYCS", 0);
	xfree (dst);

	dst = save_cpu (&len, 0);
	save_chunk (f, dst, len, L"CPU ", 0);
	xfree (dst);

	dst = save_cpu_extra (&len, 0);
	save_chunk (f, dst, len, L"CPUX", 0);
	xfree (dst);

	dst = save_cpu_trace (&len, 0);
	save_chunk (f, dst, len, L"CPUT", 0);
	xfree (dst);

#ifdef FPUEMU
	dst = save_fpu (&len,0 );
	save_chunk (f, dst, len, L"FPU ", 0);
	xfree (dst);
#endif

#ifdef MMUEMU
	dst = save_mmu (&len, 0);
	save_chunk (f, dst, len, L"MMU ", 0);
	xfree (dst);
#endif

	_tcscpy(name, L"DSKx");
	for (i = 0; i < 4; i++) {
		dst = save_disk (i, &len, 0, savepath);
		if (dst) {
			name[3] = i + '0';
			save_chunk (f, dst, len, name, 0);
			xfree (dst);
		}
	}
	_tcscpy(name, L"DSDx");
	for (i = 0; i < 4; i++) {
		dst = save_disk2 (i, &len, 0);
		if (dst) {
			name[3] = i + '0';
			save_chunk (f, dst, len, name, comp);
			xfree (dst);
		}
	}


	dst = save_floppy (&len, 0);
	save_chunk (f, dst, len, L"DISK", 0);
	xfree (dst);

	dst = save_custom (&len, 0, 0);
	save_chunk (f, dst, len, L"CHIP", 0);
	xfree (dst);

	dst = save_custom_extra (&len, 0);
	save_chunk (f, dst, len, L"CHPX", 0);
	xfree (dst);

	dst = save_custom_event_delay (&len, 0);
	save_chunk (f, dst, len, L"CHPD", 0);
	xfree (dst);

	dst = save_blitter_new (&len, 0);
	save_chunk (f, dst, len, L"BLTX", 0);
	xfree (dst);
	if (new_blitter == false) {
		dst = save_blitter (&len, 0);
		save_chunk (f, dst, len, L"BLIT", 0);
		xfree (dst);
	}

	dst = save_input (&len, 0);
	save_chunk (f, dst, len, L"CINP", 0);
	xfree (dst);

	dst = save_custom_agacolors (&len, 0);
	save_chunk (f, dst, len, L"AGAC", 0);
	xfree (dst);

	_tcscpy (name, L"SPRx");
	for (i = 0; i < 8; i++) {
		dst = save_custom_sprite (i, &len, 0);
		name[3] = i + '0';
		save_chunk (f, dst, len, name, 0);
		xfree (dst);
	}

	_tcscpy (name, L"AUDx");
	for (i = 0; i < 4; i++) {
		dst = save_audio (i, &len, 0);
		name[3] = i + '0';
		save_chunk (f, dst, len, name, 0);
		xfree (dst);
	}

	dst = save_cia (0, &len, 0);
	save_chunk (f, dst, len, L"CIAA", 0);
	xfree (dst);

	dst = save_cia (1, &len, 0);
	save_chunk (f, dst, len, L"CIAB", 0);
	xfree (dst);

	dst = save_keyboard (&len, NULL);
	save_chunk (f, dst, len, L"KEYB", 0);
	xfree (dst);

#ifdef AUTOCONFIG
	dst = save_expansion (&len, 0);
	save_chunk (f, dst, len, L"EXPA", 0);
#endif
#ifdef A2065
	dst = save_a2065 (&len, NULL);
	save_chunk (f, dst, len, L"2065", 0);
#endif
#ifdef PICASSO96
	dst = save_p96 (&len, 0);
	save_chunk (f, dst, len, L"P96 ", 0);
#endif
	save_rams (f, comp);

	dst = save_rom (1, &len, 0);
	do {
		if (!dst)
			break;
		save_chunk (f, dst, len, L"ROM ", 0);
		xfree (dst);
	} while ((dst = save_rom (0, &len, 0)));

#ifdef CD32
	dst = save_akiko (&len, NULL);
	save_chunk (f, dst, len, L"CD32", 0);
	xfree (dst);
#endif
#ifdef CDTV
	dst = save_cdtv (&len, NULL);
	save_chunk (f, dst, len, L"CDTV", 0);
	xfree (dst);
	dst = save_dmac (&len, NULL);
	save_chunk (f, dst, len, L"DMAC", 0);
	xfree (dst);
#endif

#ifdef ACTION_REPLAY
	dst = save_action_replay (&len, NULL);
	save_chunk (f, dst, len, L"ACTR", comp);
	dst = save_hrtmon (&len, NULL);
	save_chunk (f, dst, len, L"HRTM", comp);
#endif
#ifdef FILESYS
	dst = save_filesys_common (&len);
	if (dst) {
		save_chunk (f, dst, len, L"FSYC", 0);
		for (i = 0; i < nr_units (); i++) {
			dst = save_filesys (i, &len);
			if (dst) {
				save_chunk (f, dst, len, L"FSYS", 0);
				xfree (dst);
			}
		}
	}
#endif
	dst = save_gayle (&len, NULL);
	if (dst) {
		save_chunk (f, dst, len, L"GAYL", 0);
		xfree(dst);
	}
	for (i = 0; i < 4; i++) {
		dst = save_ide (i, &len, NULL);
		if (dst) {
			save_chunk (f, dst, len, L"IDE ", 0);
			xfree (dst);
		}
	}

	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		dst = save_cd (i, &len);
		if (dst) {
			_stprintf (name, L"CDU%d", i);
			save_chunk (f, dst, len, name, 0);
		}
	}

	dst = save_debug_memwatch (&len, NULL);
	if (dst) {
		save_chunk (f, dst, len, L"DMWP", 0);
		xfree(dst);
	}

	/* add fake END tag, makes it easy to strip CONF and LOG hunks */
	/* move this if you want to use CONF or LOG hunks when restoring state */
	zfile_fwrite (endhunk, 1, 8, f);

	dst = save_configuration (&len);
	if (dst) {
		save_chunk (f, dst, len, L"CONF", comp);
		xfree(dst);
	}
	len = 30000;
	dst = save_log (TRUE, &len);
	if (dst) {
		save_chunk (f, dst, len, L"LOG ", comp);
		xfree (dst);
	}

	zfile_fwrite (endhunk, 1, 8, f);

	return 1;
}

int save_state (const TCHAR *filename, const TCHAR *description)
{
	struct zfile *f;
	int comp = savestate_docompress;

	if (!savestate_specialdump && !savestate_nodialogs) {
		state_incompatible_warn ();
		if (!save_filesys_cando ()) {
			gui_message (L"Filesystem active. Try again later.");
			return -1;
		}
	}
	new_blitter = false;
	savestate_nodialogs = 0;
	custom_prepare_savestate ();
	f = zfile_fopen (filename, L"w+b", 0);
	if (!f)
		return 0;
	if (savestate_specialdump) {
		size_t pos;
		if (savestate_specialdump == 2)
			write_wavheader (f, 0, 22050);
		pos = zfile_ftell (f);
		save_rams (f, -1);
		if (savestate_specialdump == 2) {
			int len, len2, i;
			uae_u8 *tmp;
			len = zfile_ftell (f) - pos;
			tmp = xmalloc (uae_u8, len);
			zfile_fseek(f, pos, SEEK_SET);
			len2 = zfile_fread (tmp, 1, len, f);
			for (i = 0; i < len2; i++)
				tmp[i] += 0x80;
			write_wavheader (f, len, 22050);
			zfile_fwrite (tmp, len2, 1, f);
			xfree (tmp);
		}
		zfile_fclose (f);
		return 1;
	}
	int v = save_state_internal (f, description, comp, true);
	if (v)
		write_log (L"Save of '%s' complete\n", filename);
	zfile_fclose (f);
	savestate_state = 0;
	return v;
}

void savestate_quick (int slot, int save)
{
	int i, len = _tcslen (savestate_fname);
	i = len - 1;
	while (i >= 0 && savestate_fname[i] != '_')
		i--;
	if (i < len - 6 || i <= 0) { /* "_?.uss" */
		i = len - 1;
		while (i >= 0 && savestate_fname[i] != '.')
			i--;
		if (i <= 0) {
			write_log (L"savestate name skipped '%s'\n", savestate_fname);
			return;
		}
	}
	_tcscpy (savestate_fname + i, L".uss");
	if (slot > 0)
		_stprintf (savestate_fname + i, L"_%d.uss", slot);
	if (save) {
		write_log (L"saving '%s'\n", savestate_fname);
		savestate_docompress = 1;
		save_state (savestate_fname, L"");
	} else {
		if (!zfile_exists (savestate_fname)) {
			write_log (L"staterestore, file '%s' not found\n", savestate_fname);
			return;
		}
		savestate_state = STATE_DORESTORE;
		write_log (L"staterestore starting '%s'\n", savestate_fname);
	}
}

bool savestate_check (void)
{
	if (vpos == 0 && !savestate_state) {
		if (hsync_counter == 0 && input_play == INPREC_PLAY_NORMAL)
			savestate_memorysave ();
		savestate_capture (0);
	}
	if (savestate_state == STATE_DORESTORE) {
		savestate_state = STATE_RESTORE;
		return true;
	} else if (savestate_state == STATE_DOREWIND) {
		savestate_state = STATE_REWIND;
		return true;
	}
	return false;
}

static int rewindmode;


static struct staterecord *canrewind (int pos)
{
	if (pos < 0)
		pos += staterecords_max;
	if (!staterecords)
		return 0;
	if (staterecords[pos] == NULL)
		return NULL;
	if (staterecords[pos]->inuse == 0)
		return NULL;
	if ((pos + 1) % staterecords_max  == staterecords_first)
		return NULL;
	return staterecords[pos];
}

int savestate_dorewind (int pos)
{
	rewindmode = pos;
	if (pos < 0)
		pos = replaycounter - 1;
	if (canrewind (pos)) {
		savestate_state = STATE_DOREWIND;
		write_log (L"dorewind %d (%010d/%03d) -> %d\n", replaycounter - 1, hsync_counter, vsync_counter, pos);
		return 1;
	}
	return 0;
}
#if 0
void savestate_listrewind (void)
{
	int i = replaycounter;
	int cnt;
	uae_u8 *p;
	uae_u32 pc;

	cnt = 1;
	for (;;) {
		struct staterecord *st;
		st = &staterecords[i];
		if (!st->start)
			break;
		p = st->cpu + 17 * 4;
		pc = restore_u32_func (&p);
		console_out_f (L"%d: PC=%08X %c\n", cnt, pc, regs.pc == pc ? '*' : ' ');
		cnt++;
		i--;
		if (i < 0)
			i += MAX_STATERECORDS;
	}
}
#endif

void savestate_rewind (void)
{
	int len, i, dummy;
	uae_u8 *p, *p2;
	struct staterecord *st;
	int pos;
	bool rewind = false;

	if (hsync_counter % currprefs.statecapturerate <= 25 && rewindmode <= -2) {
		pos = replaycounter - 2;
		rewind = true;
	} else {
		pos = replaycounter - 1;
	}
	st = canrewind (pos);
	if (!st) {
		rewind = false;
		pos = replaycounter - 1;
		st = canrewind (pos);
		if (!st)
			return;
	}
	p = st->data;
	p2 = st->end;
	write_log (L"rewinding %d -> %d\n", replaycounter - 1, pos);
	hsync_counter = restore_u32_func (&p);
	vsync_counter = restore_u32_func (&p);
	p = restore_cpu (p);
	p = restore_cycles (p);
	p = restore_cpu_extra (p);
	if (restore_u32_func (&p))
		p = restore_cpu_trace (p);
#ifdef FPUEMU
	if (restore_u32_func (&p))
		p = restore_fpu (p);
#endif
	for (i = 0; i < 4; i++) {
		p = restore_disk (i, p);
		if (restore_u32_func (&p))
			p = restore_disk2 (i, p);
	}
	p = restore_floppy (p);
	p = restore_custom (p);
	p = restore_custom_extra (p);
	if (restore_u32_func (&p))
		p = restore_custom_event_delay (p);
	p = restore_blitter_new (p);
	p = restore_custom_agacolors (p);
	for (i = 0; i < 8; i++) {
		p = restore_custom_sprite (i, p);
	}
	for (i = 0; i < 4; i++) {
		p = restore_audio (i, p);
	}
	p = restore_cia (0, p);
	p = restore_cia (1, p);
	p = restore_keyboard (p);
	p = restore_inputstate (p);
#ifdef AUTOCONFIG
	p = restore_expansion (p);
#endif
#ifdef PICASSO96
	if (restore_u32_func (&p))
		p = restore_p96 (p);
#endif
	len = restore_u32_func (&p);
	memcpy (chipmemory, p, currprefs.chipmem_size > len ? len : currprefs.chipmem_size);
	p += len;
	len = restore_u32_func (&p);
	memcpy (save_bram (&dummy), p, currprefs.bogomem_size > len ? len : currprefs.bogomem_size);
	p += len;
#ifdef AUTOCONFIG
	len = restore_u32_func (&p);
	memcpy (save_fram (&dummy), p, currprefs.fastmem_size > len ? len : currprefs.fastmem_size);
	p += len;
	len = restore_u32_func (&p);
	memcpy (save_zram (&dummy, 0), p, currprefs.z3fastmem_size > len ? len : currprefs.z3fastmem_size);
	p += len;
#endif
#ifdef ACTION_REPLAY
	if (restore_u32_func (&p))
		p = restore_action_replay (p);
	if (restore_u32_func (&p))
		p = restore_hrtmon (p);
#endif
#ifdef CD32
	if (restore_u32_func (&p))
		p = restore_akiko (p);
#endif
#ifdef CDTV
	if (restore_u32_func (&p))
		p = restore_cdtv (p);
	if (restore_u32_func (&p))
		p = restore_dmac (p);
#endif
	if (restore_u32_func (&p))
		p = restore_gayle (p);
	for (i = 0; i < 4; i++) {
		if (restore_u32_func (&p))
			p = restore_ide (p);
	}
	p += 4;
	if (p != p2) {
		gui_message (L"reload failure, address mismatch %p != %p", p, p2);
		uae_reset (0);
		return;
	}
	inprec_setposition (st->inprecoffset, pos);
	write_log (L"state %d restored.  (%010d/%03d)\n", pos, hsync_counter, vsync_counter);
	if (rewind) {
		replaycounter--;
		if (replaycounter < 0)
			replaycounter += staterecords_max;
		st = canrewind (replaycounter);
		st->inuse = 0;
	}

}

#define BS 10000

STATIC_INLINE int bufcheck (struct staterecord *sr, uae_u8 *p, int len)
{
	if (p - sr->data + BS + len >= sr->len)
		return 1;
	return 0;
}

void savestate_memorysave (void)
{
	new_blitter = true;
	// create real statefile in memory too for later saving
	zfile_fclose (staterecord_statefile);
	staterecord_statefile = zfile_fopen_empty (NULL, L"statefile.inp.uss");
	if (staterecord_statefile)
		save_state_internal (staterecord_statefile, L"rerecording", 1, false);
}

void savestate_capture (int force)
{
	uae_u8 *p, *p2, *p3, *dst;
	int i, len, tlen, retrycnt;
	struct staterecord *st;
	bool firstcapture = false;

#ifdef FILESYS
	if (nr_units ())
		return;
#endif
	if (!staterecords)
		return;
	if (!input_record)
		return;
	if (currprefs.statecapturerate && hsync_counter == 0 && input_record == INPREC_RECORD_START && savestate_first_capture > 0) {
		// first capture
		force = true;
		firstcapture = true;
	} else if (savestate_first_capture < 0) {
		force = true;
		firstcapture = false;
	}
	if (!force) {
		if (currprefs.statecapturerate <= 0)
			return;
		if (hsync_counter % currprefs.statecapturerate)
			return;
	}
	savestate_first_capture = false;

	retrycnt = 0;
retry2:
	st = staterecords[replaycounter];
	if (st == NULL) {
		st = (struct staterecord*)xmalloc (uae_u8, statefile_alloc);
		st->len = statefile_alloc;
	} else if (retrycnt > 0) {
		write_log (L"realloc %d -> %d\n", st->len, st->len + STATEFILE_ALLOC_SIZE);
		st->len += STATEFILE_ALLOC_SIZE;
		st = (struct staterecord*)xrealloc (uae_u8, st, st->len);
	}
	if (st->len > statefile_alloc)
		statefile_alloc = st->len;
	st->inuse = 0;
	st->data = (uae_u8*)(st + 1);
	staterecords[replaycounter] = st;
	retrycnt++;
	p = p2 = st->data;
	tlen = 0;
	save_u32_func (&p, hsync_counter);
	save_u32_func (&p, vsync_counter);
	tlen += 8;

	if (bufcheck (st, p, 0))
		goto retry;
	st->cpu = p;
	save_cpu (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	save_cycles (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	save_cpu_extra (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_cpu_trace (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}

#ifdef FPUEMU
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_fpu (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
#endif
	for (i = 0; i < 4; i++) {
		if (bufcheck (st, p, 0))
			goto retry;
		save_disk (i, &len, p, true);
		tlen += len;
		p += len;
		p3 = p;
		save_u32_func (&p, 0);
		tlen += 4;
		if (save_disk2 (i, &len, p)) {
			save_u32_func (&p3, 1);
			tlen += len;
			p += len;
		}
	}

	if (bufcheck (st, p, 0))
		goto retry;
	save_floppy (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	save_custom (&len, p, 0);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	save_custom_extra (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_custom_event_delay (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}

	if (bufcheck (st, p, 0))
		goto retry;
	save_blitter_new (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, 0))
		goto retry;
	save_custom_agacolors (&len, p);
	tlen += len;
	p += len;
	for (i = 0; i < 8; i++) {
		if (bufcheck (st, p, 0))
			goto retry;
		save_custom_sprite (i, &len, p);
		tlen += len;
		p += len;
	}

	for (i = 0; i < 4; i++) {
		if (bufcheck (st, p, 0))
			goto retry;
		save_audio (i, &len, p);
		tlen += len;
		p += len;
	}

	if (bufcheck (st, p, len))
		goto retry;
	save_cia (0, &len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, len))
		goto retry;
	save_cia (1, &len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, len))
		goto retry;
	save_keyboard (&len, p);
	tlen += len;
	p += len;

	if (bufcheck (st, p, len))
		goto retry;
	save_inputstate (&len, p);
	tlen += len;
	p += len;

#ifdef AUTOCONFIG
	if (bufcheck (st, p, len))
		goto retry;
	save_expansion (&len, p);
	tlen += len;
	p += len;
#endif

#ifdef PICASSO96
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_p96 (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
#endif

	dst = save_cram (&len);
	if (bufcheck (st, p, len))
		goto retry;
	save_u32_func (&p, len);
	memcpy (p, dst, len);
	tlen += len + 4;
	p += len;
	dst = save_bram (&len);
	if (bufcheck (st, p, len))
		goto retry;
	save_u32_func (&p, len);
	memcpy (p, dst, len);
	tlen += len + 4;
	p += len;
#ifdef AUTOCONFIG
	dst = save_fram (&len);
	if (bufcheck (st, p, len))
		goto retry;
	save_u32_func (&p, len);
	memcpy (p, dst, len);
	tlen += len + 4;
	p += len;
	dst = save_zram (&len, 0);
	if (bufcheck (st, p, len))
		goto retry;
	save_u32_func (&p, len);
	memcpy (p, dst, len);
	tlen += len + 4;
	p += len;
#endif
#ifdef ACTION_REPLAY
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_action_replay (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_hrtmon (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
#endif
#ifdef CD32
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_akiko (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
#endif
#ifdef CDTV
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_cdtv (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_dmac (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
#endif
	if (bufcheck (st, p, 0))
		goto retry;
	p3 = p;
	save_u32_func (&p, 0);
	tlen += 4;
	if (save_gayle (&len, p)) {
		save_u32_func (&p3, 1);
		tlen += len;
		p += len;
	}
	for (i = 0; i < 4; i++) {
		if (bufcheck (st, p, 0))
			goto retry;
		p3 = p;
		save_u32_func (&p, 0);
		tlen += 4;
		if (save_ide (i, &len, p)) {
			save_u32_func (&p3, 1);
			tlen += len;
			p += len;
		}
	}
	save_u32_func (&p, tlen);
	st->end = p;
	st->inuse = 1;
	st->inprecoffset = inprec_getposition ();

	replaycounter++;
	if (replaycounter >= staterecords_max)
		replaycounter -= staterecords_max;
	if (replaycounter == staterecords_first) {
		staterecords_first++;
		if (staterecords_first >= staterecords_max)
			staterecords_first -= staterecords_max;
	}

	write_log (L"state capture %d (%010d/%03d,%d/%d) (%d bytes, alloc %d)\n",
		replaycounter, hsync_counter, vsync_counter,
		hsync_counter % current_maxvpos (), current_maxvpos (),
		st->end - st->data, statefile_alloc);

	if (firstcapture) {
		savestate_memorysave ();
		input_record++;
		for (i = 0; i < 4; i++) {
			bool wp = true;
			DISK_validate_filename (currprefs.floppyslots[i].df, false, &wp, NULL, NULL);
			inprec_recorddiskchange (i, currprefs.floppyslots[i].df, wp);
		}
		input_record--;
	}


	return;
retry:
	if (retrycnt < 10)
		goto retry2;
	write_log (L"can't save, too small capture buffer or out of memory\n");
	return;
}

void savestate_free (void)
{
	xfree (staterecords);
	staterecords = NULL;
}

void savestate_capture_request (void)
{
	savestate_first_capture = -1;
}

void savestate_init (void)
{
	savestate_free ();
	replaycounter = 0;
	staterecords_max = currprefs.statecapturebuffersize;
	staterecords = xcalloc (struct staterecord*, staterecords_max);
	statefile_alloc = STATEFILE_ALLOC_SIZE;
	if (input_record && savestate_state != STATE_DORESTORE) {
		zfile_fclose (staterecord_statefile);
		staterecord_statefile = NULL;
		inprec_close (false);
		inprec_open (NULL, NULL);
		savestate_first_capture = 1;
	}
}


void statefile_save_recording (const TCHAR *filename)
{
	if (!staterecord_statefile)
		return;
	struct zfile *zf = zfile_fopen (filename, L"wb", 0);
	if (zf) {
		int len = zfile_size (staterecord_statefile);
		uae_u8 *data = zfile_getdata (staterecord_statefile, 0, len);
		zfile_fwrite (data, len, 1, zf);
		xfree (data);
		zfile_fclose (zf);
		write_log (L"input statefile '%s' saved\n", filename);
	}
}


/*

My (Toni Wilen <twilen@arabuusimiehet.com>)
proposal for Amiga-emulators' state-save format

Feel free to comment...

This is very similar to IFF-fileformat
Every hunk must end to 4 byte boundary,
fill with zero bytes if needed

version 0.8

HUNK HEADER (beginning of every hunk)

hunk name (4 ascii-characters)
hunk size (including header)
hunk flags

bit 0 = chunk contents are compressed with zlib (maybe RAM chunks only?)

HEADER

"ASF " (AmigaStateFile)

statefile version
emulator name ("uae", "fellow" etc..)
emulator version string (example: "0.8.15")
free user writable comment string

CPU

"CPU "

CPU model               4 (68000,68010,68020,68030,68040,68060)
CPU typeflags           bit 0=EC-model or not, bit 31 = clock rate included
D0-D7                   8*4=32
A0-A6                   7*4=32
PC                      4
unused			4
68000 prefetch (IRC)    2
68000 prefetch (IR)     2
USP                     4
ISP                     4
SR/CCR                  2
flags                   4 (bit 0=CPU was HALTed)

CPU specific registers

68000: SR/CCR is last saved register
68010: save also DFC,SFC and VBR
68020: all 68010 registers and CAAR,CACR and MSP
etc..

68010+:

DFC                     4
SFC                     4
VBR                     4

68020+:

CAAR                    4
CACR                    4
MSP                     4

68030+:

AC0                     4
AC1                     4
ACUSR                   2
TT0                     4
TT1                     4

68040+:

ITT0                    4
ITT1                    4
DTT0                    4
DTT1                    4
TCR                     4
URP                     4
SRP                     4

68060:

BUSCR                   4
PCR                     4

All:

Clock in KHz            4 (only if bit 31 in flags)
4 (spare, only if bit 31 in flags)


FPU (only if used)

"FPU "

FPU model               4 (68881/68882/68040/68060)
FPU typeflags           4 (bit 31 = clock rate included)
FP0-FP7                 4+4+2 (80 bits)
FPCR                    4
FPSR                    4
FPIAR                   4

Clock in KHz            4 (only if bit 31 in flags)
4 (spare, only if bit 31 in flags)

MMU (when and if MMU is supported in future..)

"MMU "

MMU model               4 (68040)
flags			4 (none defined yet)

CUSTOM CHIPS

"CHIP"

chipset flags   4      OCS=0,ECSAGNUS=1,ECSDENISE=2,AGA=4
ECSAGNUS and ECSDENISE can be combined

DFF000-DFF1FF   352    (0x120 - 0x17f and 0x0a0 - 0xdf excluded)

sprite registers (0x120 - 0x17f) saved with SPRx chunks
audio registers (0x0a0 - 0xdf) saved with AUDx chunks

AGA COLORS

"AGAC"

AGA color               8 banks * 32 registers *
registers               LONG (XRGB) = 1024

SPRITE

"SPR0" - "SPR7"


SPRxPT                  4
SPRxPOS                 2
SPRxCTL                 2
SPRxDATA                2
SPRxDATB                2
AGA sprite DATA/DATB    3 * 2 * 2
sprite "armed" status   1

sprites maybe armed in non-DMA mode
use bit 0 only, other bits are reserved


AUDIO
"AUD0" "AUD1" "AUD2" "AUD3"

audio state             1
machine mode
AUDxVOL                 1
irq?                    1
data_written?           1
internal AUDxLEN        2
AUDxLEN                 2
internal AUDxPER        2
AUDxPER                 2
internal AUDxLC         4
AUDxLC                  4
evtime?                 4

BLITTER

"BLIT"

internal blitter state

flags                   4
bit 0=blitter active
bit 1=fill carry bit
internal ahold          4
internal bhold          4
internal hsize          2
internal vsize          2

CIA

"CIAA" and "CIAB"

BFE001-BFEF01   16*1 (CIAA)
BFD000-BFDF00   16*1 (CIAB)

internal registers

IRQ mask (ICR)  1 BYTE
timer latches   2 timers * 2 BYTES (LO/HI)
latched tod     3 BYTES (LO/MED/HI)
alarm           3 BYTES (LO/MED/HI)
flags           1 BYTE
bit 0=tod latched (read)
bit 1=tod stopped (write)
div10 counter	1 BYTE

FLOPPY DRIVES

"DSK0" "DSK1" "DSK2" "DSK3"

drive state

drive ID-word           4
state                   1 (bit 0: motor on, bit 1: drive disabled, bit 2: current id bit)
rw-head track           1
dskready                1
id-mode                 1 (ID mode bit number 0-31)
floppy information

bits from               4
beginning of track
CRC of disk-image       4 (used during restore to check if image
is correct)
disk-image              null-terminated
file name

INTERNAL FLOPPY	CONTROLLER STATUS

"DISK"

current DMA word        2
DMA word bit offset     1
WORDSYNC found          1 (no=0,yes=1)
hpos of next bit        1
DSKLENGTH status        0=off,1=written once,2=written twice
unused                  2

RAM SPACE

"xRAM" (CRAM = chip, BRAM = bogo, FRAM = fast, ZRAM = Z3, P96 = RTG RAM, A3K1/A3K2 = MB RAM)

start address           4 ("bank"=chip/slow/fast etc..)
of RAM "bank"
RAM "bank" size         4
RAM flags               4 (bit 0 = zlib compressed)
RAM "bank" contents

ROM SPACE

"ROM "

ROM start               4
address
size of ROM             4
ROM type                4 KICK=0
ROM flags               4
ROM version             2
ROM revision            2
ROM CRC                 4 see below
ROM-image ID-string     null terminated, see below
path to rom image
ROM contents            (Not mandatory, use hunk size to check if
this hunk contains ROM data or not)

Kickstart ROM:
ID-string is "Kickstart x.x"
ROM version: version in high word and revision in low word
Kickstart ROM version and revision can be found from ROM start
+ 12 (version) and +14 (revision)

ROM version and CRC is only meant for emulator to automatically
find correct image from its ROM-directory during state restore.

Usually saving ROM contents is not good idea.

ACTION REPLAY

"ACTR"

Model (1,2,3)		4
path to rom image
RAM space		(depends on model)
ROM CRC             4

"CDx "

Flags               4 (bit 0 = scsi, bit 1 = ide, bit 2 = image)
Path                  (for example image file or drive letter)

END
hunk "END " ends, remember hunk size 8!


EMULATOR SPECIFIC HUNKS

Read only if "emulator name" in header is same as used emulator.
Maybe useful for configuration?

misc:

- save only at position 0,0 before triggering VBLANK interrupt
- all data must be saved in bigendian format
- should we strip all paths from image file names?

*/
