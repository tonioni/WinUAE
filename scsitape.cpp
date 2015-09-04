/*
* UAE - The Un*x Amiga Emulator
*
* SCSI tape emulation
*
* Copyright 2013 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "filesys.h"
#include "scsi.h"
#include "blkdev.h"
#include "zfile.h"
#include "memory.h"
#include "scsi.h"
#include "threaddep/thread.h"
#include "a2091.h"
#include "fsdb.h"

int log_tapeemu = 1;

#define TAPE_INDEX _T("index.tape")

static struct scsi_data_tape *tapeunits[MAX_FILESYSTEM_UNITS];

static bool notape (struct scsi_data_tape *tape)
{
	return tape->tape_dir[0] == 0 || tape->nomedia;
}

static int rl (uae_u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}
static void wl (uae_u8 *p, int v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

void tape_free (struct scsi_data_tape *tape)
{
	if (!tape)
		return;
	zfile_fclose (tape->zf);
	zfile_fclose (tape->index);
	zfile_closedir_archive (tape->zd);
	xfree(tape);
	for (int i = 0; i < MAX_FILESYSTEM_UNITS; i++) {
		if (tapeunits[i] == tape)
			tapeunits[i] = NULL;
	}
}

static void tape_init (int unit, struct scsi_data_tape *tape, const TCHAR *tape_directory, bool readonly)
{
	TCHAR path[MAX_DPATH];

	memset (tape, 0, sizeof (struct scsi_data_tape));
	_tcscpy (tape->tape_dir, tape_directory);

	tape->blocksize = 512;
	tape->wp = readonly;
	tape->beom = -1;
	tape->nomedia = false;
	tape->unloaded = false;

	if (my_existsdir (tape->tape_dir)) {
		tape->realdir = true;
	} else {
		tape->zd = zfile_opendir_archive (tape_directory, ZFD_ARCHIVE | ZFD_NORECURSE);
		if (!tape->zd)
			tape->nomedia = true;
	}

	_tcscpy (path, tape_directory);
	_tcscat (path, FSDB_DIR_SEPARATOR_S);
	_tcscat (path, TAPE_INDEX);
	tape->index = zfile_fopen (path, _T("rb"), ZFD_NORMAL);
	if (tape->index)
		write_log (_T("TAPEEMU INDEX: '%s'\n"), path);
	tapeunits[unit] = tape;
}

struct scsi_data_tape *tape_alloc (int unitnum, const TCHAR *tape_directory, bool readonly)
{
	struct scsi_data_tape *tape = xcalloc (struct scsi_data_tape, 1);
	
	tape_init (unitnum, tape, tape_directory, readonly);
	return tape;
}

void tape_media_change (int unitnum, struct uaedev_config_info *ci)
{
	struct scsi_data_tape *tape = tapeunits[unitnum];
	if (!tape)
		return;
	tape_init (unitnum, tape, ci->rootdir, ci->readonly);
}

bool tape_get_info (int unitnum, struct device_info *di)
{
	struct scsi_data_tape *tape = tapeunits[unitnum];
	memset (di, 0, sizeof (struct device_info));
	if (!tape)
		return false;
	di->media_inserted = notape (tape) == false;
	_tcscpy (di->label, tape->tape_dir);
	return true;
}

static void rewind (struct scsi_data_tape *tape)
{
	zfile_fclose (tape->zf);
	tape->zf = NULL;
	my_closedir (tape->od);
	tape->file_number = 0;
	tape->file_offset = 0;
	tape->od = NULL;
	tape->beom = -1;
	if (tape->index)
		zfile_fseek (tape->index, 0, SEEK_SET);
	if (tape->zd)
		zfile_resetdir_archive (tape->zd);
}

static void erase (struct scsi_data_tape *tape)
{
	struct my_opendir_s *od;
	if (!tape->realdir)
		return;
	rewind (tape);
	od = my_opendir (tape->tape_dir);
	if (od) {
		for (;;) {
			TCHAR path[MAX_DPATH], filename[MAX_DPATH];
			if (!my_readdir (od, filename))
				break;
			TCHAR *ext = _tcsrchr (filename, '.');
			if (ext && !_tcsicmp (ext, _T(".tape"))) {
				_stprintf (path, _T("%s%s%s"), tape->tape_dir, FSDB_DIR_SEPARATOR_S, filename);
				if (my_existsfile (path))
					my_unlink (path);
			}
		}
		my_closedir (od);
	}
}

static bool next_file (struct scsi_data_tape *tape)
{
	zfile_fclose (tape->zf);
	tape->zf = NULL;
	tape->file_offset = 0;
	if (tape->index) {
		TCHAR path[MAX_DPATH];
		TCHAR name[256];
		name[0] = 0;
		zfile_fgets (name, sizeof name / sizeof (TCHAR), tape->index);
		my_trim (name);
		if (name[0] == 0)
			goto end;
		_tcscpy (path, tape->tape_dir);
		_tcscat (path, FSDB_DIR_SEPARATOR_S);
		_tcscat (path, name);
		tape->zf = zfile_fopen (path, _T("rb"), ZFD_NORMAL);
		write_log (_T("TAPEEMU: File '%s'\n"), path);
	} else if (tape->realdir) {
		TCHAR path[MAX_DPATH], filename[MAX_DPATH];
		if (tape->od == NULL)
			tape->od =  my_opendir (tape->tape_dir);
		if (!tape->od) {
			tape_free (tape);
			goto end;
		}
		for (;;) {
			if (!my_readdir (tape->od, filename))
				goto end;
			if (_tcsicmp (filename, TAPE_INDEX))
				continue;
			_stprintf (path, _T("%s%s%s"), tape->tape_dir, FSDB_DIR_SEPARATOR_S, filename);
			if (!my_existsfile (path))
				continue;
			tape->zf = zfile_fopen (path, _T("rb"), 0);
			if (!tape->zf)
				continue;
			break;
		}
		if (tape->zf)
			write_log (_T("TAPEEMU DIR: File '%s'\n"), zfile_getname (tape->zf));
	} else {
		tape->zf = zfile_readdir_archive_open (tape->zd, _T("rb"));
		if (tape->zf)
			write_log (_T("TAPEEMU ARC: File '%s'\n"), zfile_getname (tape->zf));
	}
	if (tape->zf) {
		tape->file_number++;
		return true;
	}
end:
	write_log (_T("TAPEEMU: end of tape\n"));
	tape->beom = 1;
	return false;
}

static int tape_read (struct scsi_data_tape *tape, uae_u8 *scsi_data, int len, bool *eof)
{
	int got;

	*eof = false;
	if (tape->beom > 0) {
		*eof = true;
		return -1;
	}

	if (!tape->zf) {
		rewind (tape);
		if (!next_file (tape))
			return -1;
	}
	zfile_fseek (tape->zf, tape->file_offset, SEEK_SET);
	got = zfile_fread (scsi_data, 1, len, tape->zf);
	tape->file_offset += got;
	if (got < len) {
		*eof = true;
		next_file (tape);
	}
	return got;
}

static int tape_write (struct scsi_data_tape *tape, uae_u8 *scsi_data, int len)
{
	TCHAR path[MAX_DPATH], numname[30];
	int exists;

	if (!tape->realdir)
		return -1;
	_stprintf (numname, _T("%05d.tape"), tape->file_number);
	_stprintf (path, _T("%s%s%s"), tape->tape_dir, FSDB_DIR_SEPARATOR_S, numname);
	exists = my_existsfile (path);
	struct zfile *zf = zfile_fopen (path, _T("a+b"));
	if (!zf)
		return -1;
	zfile_fseek (zf, 0, SEEK_END);
	len = zfile_fwrite (scsi_data, 1, len, zf);
	zfile_fclose (zf);
	if (!exists) {
		_stprintf (path, _T("%s%s%s"), tape->tape_dir, FSDB_DIR_SEPARATOR_S, TAPE_INDEX);
		zf = zfile_fopen (path, _T("a+b"));
		if (zf) {
			zfile_fputs (zf, numname);
			zfile_fputs (zf, _T("\n"));
		}
		zfile_fclose (zf);
	}
	return len;
}

int scsi_tape_emulate (struct scsi_data_tape *tape, uae_u8 *cmdbuf, int scsi_cmd_len,
	uae_u8 *scsi_data, int *data_len, uae_u8 *r, int *reply_len, uae_u8 *s, int *sense_len)
{
	uae_s64 len;
	int lr = 0, ls = 0;
	int scsi_len = -1;
	int status = 0;
	int lun;
	bool eof;

	if (cmdbuf == NULL)
		return 0;

	if (log_tapeemu)
		write_log (_T("TAPEEMU: %02X.%02X.%02X.%02X.%02X.%02X\n"),
		cmdbuf[0], cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);

	if (cmdbuf[0] == 3) {
		if (tape->beom == -1)
			s[9] |= 0x8; // beginning of media
		if (tape->beom == 1)
			s[2] |= 0x40; // end of media
		if (*sense_len < 0x12)
			*sense_len = 0x12;
		return 0;
	}

	*reply_len = *sense_len = 0;
	memset (r, 0, 256);
	memset (s, 0, 256);
	s[0] = 0x70;

	lun = cmdbuf[1] >> 5;
	if (cmdbuf[0] != 0x03 && cmdbuf[0] != 0x12 && lun) {
		status = 2; /* CHECK CONDITION */
		s[0] = 0x70;
		s[2] = 5; /* ILLEGAL REQUEST */
		s[12] = 0x25; /* INVALID LUN */
		ls = 0x12;
		goto end;
	}

	switch (cmdbuf[0])
	{
	case 0x00: /* TEST UNIT READY */
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		scsi_len = 0;
		break;

	case 0x19: /* ERASE */
		if (log_tapeemu)
			write_log (_T("TAPEEMU ERASE\n"));
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		if (tape->wp)
			goto writeprot;
		erase (tape);
		rewind (tape);
		tape->beom = 1;
		scsi_len = 0;
		break;

	case 0x1b: /* LOAD/UNLOAD */
	{
		bool load = (cmdbuf[4] & 1) != 0;
		bool ret = (cmdbuf[4] & 2) != 0;
		bool eot = (cmdbuf[4] & 4) != 0;
		if (log_tapeemu)
			write_log (_T("TAPEEMU LOAD/UNLOAD %d:%d:%d\n"), eot, ret, load);
		if (notape (tape))
			goto notape;
		if (load && eot)
			goto errreq;
		rewind (tape);
		tape->unloaded = !load;
		if (eot) {
			tape->beom = 1;
		} else {
			tape->beom = -1;
		}
		scsi_len = 0;
		break;
	}

	case 0x01: /* REWIND */
		if (log_tapeemu)
			write_log (_T("TAPEEMU: REWIND. IMMED=%d\n"), cmdbuf[1] & 1);
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		rewind (tape);
		scsi_len = 0;
		break;

	case 0x11: /* SPACE */
	{
		int code = cmdbuf[1] & 15;
		int count = rl (cmdbuf + 1) & 0xffffff;
		if (log_tapeemu)
			write_log (_T("TAPEEMU: SPACE code=%d count=%d\n"), code, count);
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		if (code >= 2)
			goto errreq;
		if (code == 1) {
			if (!next_file (tape))
				goto endoftape;
		}
		scsi_len = 0;
	}
	break;

	case 0x10: /* WRITE FILEMARK */
		len = rl (cmdbuf + 1) & 0xffffff;
		if (log_tapeemu)
			write_log (_T("TAPEEMU WRITE FILEMARK %lld\n"), len);
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		if (tape->wp)
			goto writeprot;
		if (len > 0) {
			tape->file_number++;
			tape_write (tape, scsi_data, 0);
		}
		scsi_len = 0;
		break;

	case 0x0a: /* WRITE (6) */
		len = rl (cmdbuf + 1) & 0xffffff;
		if (cmdbuf[1] & 1)
			len *= tape->blocksize;
		if (log_tapeemu)
			write_log (_T("TAPEEMU WRITE %lld (%d, %d)\n"), len, rl (cmdbuf + 1) & 0xffffff, cmdbuf[1] & 1);
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		if (tape->wp)
			goto writeprot;
		if (tape->beom < 0)
			tape->beom = 0;
		scsi_len = tape_write (tape, scsi_data, len);
		if (scsi_len < 0)
			goto writeprot;
		break;

	case 0x08: /* READ (6) */
		len = rl (cmdbuf + 1) & 0xffffff;
		if (cmdbuf[1] & 1)
			len *= tape->blocksize;
		if (log_tapeemu)
			write_log (_T("TAPEEMU READ %lld (%d, %d)\n"), len, rl (cmdbuf + 1) & 0xffffff, cmdbuf[1] & 1);
		if (notape (tape))
			goto notape;
		if (tape->unloaded)
			goto unloaded;
		if (tape->beom < 0)
			tape->beom = 0;
		scsi_len = tape_read (tape, scsi_data, len, &eof);
		if (log_tapeemu)
			write_log (_T("-> READ %d bytes\n"), scsi_len);
		if ((cmdbuf[1] & 1) && scsi_len > 0 && scsi_len < len) {
			int gap = tape->blocksize - (scsi_len & (tape->blocksize - 1));
			if (gap > 0 && gap < tape->blocksize)
				memset (scsi_data + scsi_len, 0, gap);
			scsi_len += tape->blocksize - 1;
			scsi_len &= ~(tape->blocksize - 1);
		}
		if (scsi_len < 0) {
			tape->beom = 2;
			status = SCSI_STATUS_CHECK_CONDITION;
			s[0] = 0x80 | 0x70;
			s[2] = 8; /* BLANK CHECK */
			if (cmdbuf[1] & 1)
				wl (&s[3], len / tape->blocksize);
			else
				wl (&s[3], len);
			s[12] = 0;
			s[13] = 2; /* End-of-partition/medium detected */
			ls = 0x12;
		} else if (eof) {
			status = SCSI_STATUS_CHECK_CONDITION;
			s[0] = 0x80 | 0x70; // Valid + code
			s[2] = 0x80 | 0; /* File Mark Detected + NO SENSE */
			if (cmdbuf[1] & 1)
				wl (&s[3], (len - scsi_len) / tape->blocksize);
			else
				wl (&s[3], len);
			s[12] = 0;
			s[13] = 1; /* File Mark detected */
			ls = 0x12;
			if (log_tapeemu)
				write_log (_T("TAPEEMU READ FILE END, %lld remaining\n"), len - scsi_len);
		}
	break;

	case 0x5a: // MODE SENSE(10)
	case 0x1a: /* MODE SENSE(6) */
	{
		uae_u8 *p;
		int maxlen;
		bool sense10 = cmdbuf[0] == 0x5a;
		int totalsize, bdsize;
		int pc = cmdbuf[2] >> 6;
		int pcode = cmdbuf[2] & 0x3f;
		int dbd = cmdbuf[1] & 8;
		if (cmdbuf[0] == 0x5a)
			dbd = 1;
		if (log_tapeemu)
			write_log (_T("TAPEEMU MODE SENSE PC=%d CODE=%d DBD=%d\n"), pc, pcode, dbd);
		p = r;
		if (sense10) {
			totalsize = 8 - 2;
			maxlen = (cmdbuf[7] << 8) | cmdbuf[8];
			p[2] = 0;
			p[3] = 0;
			p[4] = 0;
			p[5] = 0;
			p[6] = 0;
			p[7] = 0;
			p += 8;
		} else {
			totalsize = 4 - 1;
			maxlen = cmdbuf[4];
			p[1] = 0;
			p[2] = tape->wp ? 0x80 : 0;
			p[3] = 0;
			p += 4;
		}
		bdsize = 0;
		if (!dbd) {
			wl(p + 0, 0);
			wl(p + 4, tape->blocksize);
			bdsize = 8;
			p += bdsize;
		}
		if (pcode)
			goto errreq;
		if (sense10) {
			totalsize += bdsize;
			r[6] = bdsize >> 8;
			r[7] = bdsize & 0xff;
			r[0] = totalsize >> 8;
			r[1] = totalsize & 0xff;
		} else {
			totalsize += bdsize;
			r[3] = bdsize & 0xff;
			r[0] = totalsize & 0xff;
		}
		lr = totalsize + 1;
		if (lr > maxlen)
			lr = maxlen;
	}
	break;

	case 0x55: // MODE SELECT(10)
	case 0x15: // MODE SELECT(6)
	{
		uae_u8 *p;
		bool mode10 = cmdbuf[0] == 0x55;
		int nb = 0, bl = 512;
		p = scsi_data + 4;
		if (mode10)
			p += 4;
		if (scsi_data[3] >= 8) {
			nb = rl (p) & 0xffffff;
			bl = rl (p + 4) & 0xffffff;
		}
		if (log_tapeemu)
			write_log (_T("TAPEEMU MODE SELECT BUFM=%d BDL=%d Density=%d NB=%d BL=%d\n"), (scsi_data[2] & 0x10) != 0, scsi_data[3], p[0], nb, bl);
		if (nb || bl != 512)
			goto err;
		scsi_len = 0;
		break;
	}

	case 0x05: /* READ BLOCK LIMITS */
		if (log_tapeemu)
			write_log (_T("TAPEEMU READ BLOCK LIMITS\n"));
		r[0] = 0;
		r[1] = 0;
		r[2] = 2; // 0x200 = 512
		r[3] = 0;
		r[4] = 2; // 0x200 = 512
		r[5] = 0;
		lr = 6;
	break;

	case 0x16: /* RESERVE UNIT */
		if (log_tapeemu)
			write_log (_T("TAPEEMU RESERVE UNIT\n"));
		scsi_len = 0;
		break;

	case 0x17: /* RELEASE UNIT */
		if (log_tapeemu)
			write_log (_T("TAPEEMU RELEASE UNIT\n"));
		scsi_len = 0;
		break;

	case 0x1e: /* PREVENT/ALLOW MEDIUM REMOVAL */
		if (log_tapeemu)
			write_log (_T("TAPEEMU PREVENT/ALLOW MEDIUM REMOVAL\n"));
		scsi_len = 0;
		break;

	case 0x12: /* INQUIRY */
	{
		if (log_tapeemu)
			write_log (_T("TAPEEMU INQUIRY\n"));
		if ((cmdbuf[1] & 1) || cmdbuf[2] != 0)
			goto err;
		len = cmdbuf[4];
		if (cmdbuf[1] >> 5) {
			r[0] = 0x7f;
		} else {
			r[0] = INQ_SEQD;
		}
		r[1] |= 0x80; // removable
		r[2] = 2; /* supports SCSI-2 */
		r[3] = 2; /* response data format */
		r[4] = 32; /* additional length */
		r[7] = 0;
		scsi_len = lr = len < 36 ? len : 36;
		r[2] = 2;
		r[3] = 2;
		char *s = ua (_T("UAE"));
		memcpy (r + 8, s, strlen (s));
		xfree (s);
		s = ua (_T("SCSI TAPE"));
		memcpy (r + 16, s, strlen (s));
		xfree (s);
		s = ua (_T("0.1"));
		memcpy (r + 32, s, strlen (s));
		xfree (s);
		for (int i = 8; i < 36; i++) {
			if (r[i] == 0)
				r[i] = 32;
		}
	}
	break;
	default:
		write_log (_T("TAPEEMU: unsupported scsi command 0x%02X\n"), cmdbuf[0]);
err:
		status = SCSI_STATUS_CHECK_CONDITION;
		s[0] = 0x70;
		s[2] = SCSI_SK_ILLEGAL_REQ;
		s[12] = SCSI_INVALID_COMMAND;
		ls = 0x12;
		break;
writeprot:
		status = 2; /* CHECK CONDITION */
		s[0] = 0x70;
		s[2] = 7; /* DATA PROTECT */
		s[12] = 0x27; /* WRITE PROTECTED */
		ls = 0x12;
		break;
errreq:
		lr = -1;
		status = SCSI_STATUS_CHECK_CONDITION;
		s[0] = 0x70;
		s[2] = SCSI_SK_ILLEGAL_REQ;
		s[12] = SCSI_INVALID_FIELD;
		ls = 0x12;
		break;
endoftape:
		status = SCSI_STATUS_CHECK_CONDITION;
		s[0] = 0x70;
		s[2] = SCSI_SK_MED_ERR;
		s[12] = 0;
		s[13] = 2; /* End-of-partition/medium detected */
		ls = 0x12;
		break;
unloaded:
		status = SCSI_STATUS_CHECK_CONDITION;
		s[0] = 0x70;
		s[2] = SCSI_SK_NOT_READY;
		s[12] = SCSI_NOT_READY;
		ls = 0x12;
		break;
notape:
		status = SCSI_STATUS_CHECK_CONDITION;
		s[0] = 0x70;
		s[2] = SCSI_SK_NOT_READY;
		s[12] = SCSI_MEDIUM_NOT_PRESENT;
		ls = 0x12;
		break;
	}
end:
	*data_len = scsi_len;
	*reply_len = lr;
	*sense_len = ls;
	if (lr > 0) {
		if (log_tapeemu) {
			write_log (_T("TAPEEMU REPLY: "));
			for (int i = 0; i < lr && i < 40; i++)
				write_log (_T("%02X."), r[i]);
			write_log (_T("\n"));
		}
	}
	if (ls > 0) {
		if (tape->beom == 1)
			s[2] |= 0x40;
		if (tape->beom == -1)
			s[9] |= 0x8;
		if (log_tapeemu) {
			write_log (_T("TAPEEMU SENSE: "));
			for (int i = 0; i < ls; i++)
				write_log (_T("%02X."), s[i]);
			write_log (_T("\n"));
		}
	}
	return status;
}
