/*
* UAE - The Un*x Amiga Emulator
*
* lowlevel device glue
*
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"

#include "blkdev.h"
#include "scsidev.h"
#include "savestate.h"

static int scsiemu;

static struct device_functions *device_func[2];
static int have_ioctl;
static int openlist[MAX_TOTAL_DEVICES];
static int forcedunit = -1;

/* convert minutes, seconds and frames -> logical sector number */
int msf2lsn (int msf)
{
	int sector = (((msf >> 16) & 0xff) * 60 * 75 + ((msf >> 8) & 0xff) * 75 + ((msf >> 0) & 0xff));
	sector -= 150;
	return sector;
}

/* convert logical sector number -> minutes, seconds and frames */
int lsn2msf (int sectors)
{
	int msf;
	sectors += 150;
	msf = (sectors / (75 * 60)) << 16;
	msf |= ((sectors / 75) % 60) << 8;
	msf |= (sectors % 75) << 0;
	return msf;
}

uae_u8 frombcd (uae_u8 v)
{
	return (v >> 4) * 10 + (v & 15);
}
uae_u8 tobcd (uae_u8 v)
{
	return ((v / 10) << 4) | (v % 10);
}
int fromlongbcd (uae_u8 *p)
{
	return (frombcd (p[0]) << 16) | (frombcd (p[1]) << 8) | (frombcd (p[2])  << 0);
}
void tolongbcd (uae_u8 *p, int v)
{
	p[0] = tobcd ((v >> 16) & 0xff);
	p[1] = tobcd ((v >> 8) & 0xff);
	p[2] = tobcd ((v >> 0) & 0xff);
}

#ifdef _WIN32

#include "od-win32/win32.h"

extern struct device_functions devicefunc_win32_aspi;
extern struct device_functions devicefunc_win32_spti;
extern struct device_functions devicefunc_win32_ioctl;
extern struct device_functions devicefunc_cdimage;

static void install_driver (int flags)
{
	scsiemu = 0;
	device_func[DF_IOCTL] = NULL;
	device_func[DF_SCSI] = NULL;
	if (devicefunc_cdimage.checkbus (flags | DEVICE_TYPE_CHECKAVAIL)) {
		device_func[DF_IOCTL] = &devicefunc_cdimage;
		device_func[DF_SCSI] = &devicefunc_cdimage;
		scsiemu = 1;
		return;
	}
#ifdef WINDDK
	if (!device_func[DF_IOCTL])
		device_func[DF_IOCTL] = &devicefunc_win32_ioctl;
	if (currprefs.win32_uaescsimode == UAESCSI_CDEMU) {
		device_func[DF_SCSI] = &devicefunc_win32_ioctl;
		scsiemu = 1;
	} else if (flags != DEVICE_TYPE_IOCTL) {
		device_func[DF_SCSI] = &devicefunc_win32_spti;
		if (currprefs.win32_uaescsimode == UAESCSI_ADAPTECASPI ||
			currprefs.win32_uaescsimode == UAESCSI_NEROASPI ||
			currprefs.win32_uaescsimode == UAESCSI_FROGASPI) {
				device_func[DF_SCSI] = &devicefunc_win32_aspi;
				device_func[DF_IOCTL] = 0;
		}
	}
#endif
	if (device_func[DF_SCSI])
		device_func[DF_SCSI]->checkbus (DEVICE_TYPE_CHECKAVAIL);
	if (device_func[DF_IOCTL])
		device_func[DF_IOCTL]->checkbus (DEVICE_TYPE_CHECKAVAIL);
}
#endif

int sys_command_isopen (int unitnum)
{
	return openlist[unitnum];
}

void sys_command_setunit (int unitnum)
{
	forcedunit = unitnum;
}

int sys_command_open (int mode, int unitnum)
{
	int ret = 0;

	if (forcedunit >= 0) {
		if (unitnum != forcedunit)
			return 0;
	}

	if (mode == DF_SCSI || !have_ioctl) {
		if (device_func[DF_SCSI] != NULL)
			ret = device_func[DF_SCSI]->opendev (unitnum);
	} else {
		ret = device_func[DF_IOCTL]->opendev (unitnum);
	}
	if (ret)
		openlist[unitnum]++;
	return ret;
}

void sys_command_close (int mode, int unitnum)
{
	if (mode == DF_SCSI || !have_ioctl) {
		if (device_func[DF_SCSI] != NULL)
			device_func[DF_SCSI]->closedev (unitnum);
	} else {
		device_func[DF_IOCTL]->closedev (unitnum);
	}
	if (openlist[unitnum] > 0)
		openlist[unitnum]--;
}

void device_func_reset (void)
{
	have_ioctl = 0;
}

int device_func_init (int flags)
{
	static int old_flags = -1;
	int support_scsi = 0, support_ioctl = 0;
	int oflags;
	
	if (flags & DEVICE_TYPE_USE_OLD) {
		if (old_flags >= 0)
			flags = old_flags;
		else
			flags &= ~DEVICE_TYPE_USE_OLD;
	}

	old_flags = flags;

	oflags = (flags & DEVICE_TYPE_SCSI) ? 0 : (1 << INQ_ROMD);

	forcedunit = -1;
	install_driver (flags);
	if (device_func[DF_IOCTL])
		have_ioctl = 1;
	else
		have_ioctl = 0;
	if (flags & DEVICE_TYPE_ALLOWEMU)
		oflags |= DEVICE_TYPE_ALLOWEMU;
	if (device_func[DF_SCSI]) {
		if (device_func[DF_SCSI] != device_func[DF_IOCTL])
			support_scsi = device_func[DF_SCSI]->openbus (oflags) ? 1 : 0;
		else
			support_scsi = 1;
	}
	oflags |= 1 << INQ_ROMD;
	if (have_ioctl)
		support_ioctl = device_func[DF_IOCTL]->openbus (oflags) ? 1 : 0;
	write_log (L"support_scsi = %d support_ioctl = %d\n", support_scsi, support_ioctl);
	return (support_scsi ? (1 << DF_SCSI) : 0) | (support_ioctl ? (1 << DF_IOCTL) : 0);
}

static int audiostatus (int unitnum)
{
	uae_u8 cmd[10] = {0x42,2,0x40,1,0,0,0,(uae_u8)(DEVICE_SCSI_BUFSIZE>>8),(uae_u8)(DEVICE_SCSI_BUFSIZE&0xff),0};
	uae_u8 *p = device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
	if (!p)
		return 0;
	return p[1];
}

/* pause/unpause CD audio */
void sys_command_cd_pause (int mode, int unitnum, int paused)
{
	if (mode == DF_SCSI || !have_ioctl) {
		int as = audiostatus (unitnum);
		if ((paused && as == 0x11) && (!paused && as == 0x12)) {
			uae_u8 cmd[10] = {0x4b,0,0,0,0,0,0,0,paused?0:1,0};
			device_func[DF_SCSI]->exec_out (unitnum, cmd, sizeof (cmd));
		}
		return;
	}
	device_func[DF_IOCTL]->pause (unitnum, paused);
}

/* stop CD audio */
void sys_command_cd_stop (int mode, int unitnum)
{
	if (mode == DF_SCSI || !have_ioctl) {
		int as = audiostatus (unitnum);
		if (as == 0x11) {
			uae_u8 cmd[6] = {0x4e,0,0,0,0,0};
			device_func[DF_SCSI]->exec_out (unitnum, cmd, sizeof (cmd));
		}
		return;
	}
	device_func[DF_IOCTL]->stop (unitnum);
}

#if 0
static int adjustplaypos (int unitnum, int startlsn)
{
	uae_u8 q[SUBQ_SIZE];
	if (!device_func[DF_IOCTL]->qcode (unitnum, q, startlsn - 1))
		return startlsn;
	int otrack = frombcd (q[4 + 1]);
	int lsn = startlsn;
	int max = 150;
	while (max-- > 0 && startlsn > 0) {
		if (!device_func[DF_IOCTL]->qcode (unitnum, q, startlsn - 1))
			break;
		int track = frombcd (q[4 + 1]);
		int idx = frombcd (q[4 + 2]);
		//write_log (L"%d %d\n", track, idx);
		if (idx != 0 || otrack != track)
			break;
		startlsn--;
	}
	if (lsn != startlsn)
		write_log (L"CD play adjust %d -> %d\n", lsn, startlsn);
	startlsn -= 10;
	if (startlsn < 0)
		startlsn = 0;
	return startlsn;
}
#endif

/* play CD audio */
int sys_command_cd_play (int mode, int unitnum, int startlsn, int endlsn, int scan)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd[12] = {0,0,0,0,0,0,0,0,0,0,0,0};
#if 0
		if (scan) {
			cmd[0] = 0xba;
			cmd[1] = scan < 0 ? 0x10 : 0x0;
			cmd[3] = (uae_u8)(startmsf >> 16);
			cmd[4] = (uae_u8)(startmsf >> 8);
			cmd[5] = (uae_u8)(startmsf >> 0);
			cmd[9] = 0x40;
		} else {
#endif
			int startmsf = lsn2msf (startlsn);
			int endmsf = lsn2msf (endlsn);
			cmd[0] = 0x47;
			cmd[3] = (uae_u8)(startmsf >> 16);
			cmd[4] = (uae_u8)(startmsf >> 8);
			cmd[5] = (uae_u8)(startmsf >> 0);
			cmd[6] = (uae_u8)(endmsf >> 16);
			cmd[7] = (uae_u8)(endmsf >> 8);
			cmd[8] = (uae_u8)(endmsf >> 0);
#if 0
		}
#endif
		return device_func[DF_SCSI]->exec_out (unitnum, cmd, sizeof (cmd)) == 0 ? 0 : 1;
	}
	//startlsn = adjustplaypos (unitnum, startlsn);
	return device_func[DF_IOCTL]->play (unitnum, startlsn, endlsn, scan, NULL);
}

/* play CD audio with subchannels */
int sys_command_cd_play (int mode, int unitnum, int startlsn, int endlsn, int scan, play_subchannel_callback subfunc)
{
	if (mode == DF_SCSI || !have_ioctl)
		return 0;
	//startlsn = adjustplaypos (unitnum, startlsn);
	return device_func[DF_IOCTL]->play (unitnum, startlsn, endlsn, scan, subfunc);
}

/* set CD audio volume */
void sys_command_cd_volume (int mode, int unitnum, uae_u16 volume)
{
	if (mode == DF_SCSI || !have_ioctl)
		return;
	device_func[DF_IOCTL]->volume (unitnum, volume);
}

/* read qcode */
int sys_command_cd_qcode (int mode, int unitnum, uae_u8 *buf)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd[10] = {0x42,2,0x40,1,0,0,0,(uae_u8)(DEVICE_SCSI_BUFSIZE>>8),(uae_u8)(DEVICE_SCSI_BUFSIZE&0xff),0};
		uae_u8 *p = device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
		if (p) {
			memcpy (buf, p, SUBQ_SIZE);
			return 1;
		}
		return 0; 
	}
	return device_func[DF_IOCTL]->qcode (unitnum, buf, -1);
};

/* read table of contents */
struct cd_toc_head *sys_command_cd_toc (int mode, int unitnum)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd [10] = { 0x43,0,2,0,0,0,1,(uae_u8)(DEVICE_SCSI_BUFSIZE>>8),(uae_u8)(DEVICE_SCSI_BUFSIZE&0xff),0};
		return NULL;
//		return device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof(cmd), 0);
	}
	return device_func[DF_IOCTL]->toc (unitnum);
}

/* read one cd sector */
uae_u8 *sys_command_cd_read (int mode, int unitnum, uae_u8 *data, int block, int size)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, 1, 0x10, 0, 0 };
		cmd[3] = (uae_u8)(block >> 16);
		cmd[4] = (uae_u8)(block >> 8);
		cmd[5] = (uae_u8)(block >> 0);
		uae_u8 *p = device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
		if (p && data)
			memcpy (data, p, size);
		return p;
	}
	return device_func[DF_IOCTL]->read (unitnum, data, block, size);
}
uae_u8 *sys_command_cd_rawread (int mode, int unitnum, uae_u8 *data, int sector, int size, int sectorsize)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, size, 0x10, 0, 0 };
		cmd[3] = (uae_u8)(sector >> 16);
		cmd[4] = (uae_u8)(sector >> 8);
		cmd[5] = (uae_u8)(sector >> 0);
		uae_u8 *p = device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
		if (p && data)
			memcpy (data, p, size);
		return p;
	}
	return device_func[DF_IOCTL]->rawread (unitnum, data, sector, size, sectorsize);
}

/* read block */
uae_u8 *sys_command_read (int mode, int unitnum, uae_u8 *data, int block, int size)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
		cmd[2] = (uae_u8)(block >> 24);
		cmd[3] = (uae_u8)(block >> 16);
		cmd[4] = (uae_u8)(block >> 8);
		cmd[5] = (uae_u8)(block >> 0);
		uae_u8 *p = device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
		if (p && data)
			memcpy (data, p, size);
		return p;
	}
	return device_func[DF_IOCTL]->read (unitnum, data, block, size);
}

/* write block */
int sys_command_write (int mode, int unitnum, uae_u8 *data, int offset, int size)
{
	if (mode == DF_SCSI || !have_ioctl) {
		uae_u8 cmd[10] = { 0x2a, 0, 0, 0, 0, 0, 0, 0, size, 0 };
		cmd[2] = (uae_u8)(offset >> 24);
		cmd[3] = (uae_u8)(offset >> 16);
		cmd[4] = (uae_u8)(offset >> 8);
		cmd[5] = (uae_u8)(offset >> 0);
		if (device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0))
			return -1;
		return 0;
	}
	return device_func[DF_IOCTL]->write (unitnum, data, offset, size);
}

int sys_command_ismedia (int mode, int unitnum, int quick)
{
	struct device_info di;

	if (forcedunit >= 0) {
		if (unitnum != forcedunit)
			return -1;
	}

	if (mode == DF_SCSI || !have_ioctl || !device_func[DF_IOCTL]->ismedia) {
		if (quick)
			return -1;
		memset(&di, 0, sizeof di);
		if (device_func[DF_SCSI]->info (unitnum, &di, quick) == NULL, quick)
			return -1;
		return di.media_inserted;
	} else {
		return device_func[DF_IOCTL]->ismedia (unitnum, quick);
	}
}

struct device_info *sys_command_info (int mode, int unitnum, struct device_info *di, int quick)
{
	if (mode == DF_SCSI || !have_ioctl)
		return device_func[DF_SCSI]->info (unitnum, di, quick);
	else
		return device_func[DF_IOCTL]->info (unitnum, di, quick);
}

struct device_scsi_info *sys_command_scsi_info (int mode, int unitnum, struct device_scsi_info *dsi)
{
	if (mode == DF_SCSI || !have_ioctl)
		return device_func[DF_SCSI]->scsiinfo (unitnum, dsi);
	else
		return device_func[DF_IOCTL]->scsiinfo (unitnum, dsi);
}

#define MODE_SELECT_6 0x15
#define MODE_SENSE_6 0x1a
#define MODE_SELECT_10 0x55
#define MODE_SENSE_10 0x5a

void scsi_atapi_fixup_pre (uae_u8 *scsi_cmd, int *len, uae_u8 **datap, int *datalenp, int *parm)
{
	uae_u8 cmd, *p, *data = *datap;
	int l, datalen = *datalenp;

	*parm = 0;
	cmd = scsi_cmd[0];
	if (cmd != MODE_SELECT_6 && cmd != MODE_SENSE_6)
		return;
	l = scsi_cmd[4];
	if (l > 4)
		l += 4;
	scsi_cmd[7] = l >> 8;
	scsi_cmd[8] = l;
	if (cmd == MODE_SELECT_6) {
		scsi_cmd[0] = MODE_SELECT_10;
		scsi_cmd[9] = scsi_cmd[5];
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[4] = scsi_cmd[5] = scsi_cmd[6] = 0;
		*len = 10;
		p = xmalloc (uae_u8, 8 + datalen + 4);
		if (datalen > 4)
			memcpy (p + 8, data + 4, datalen - 4);
		p[0] = 0;
		p[1] = data[0];
		p[2] = data[1];
		p[3] = data[2];
		p[4] = p[5] = p[6] = 0;
		p[7] = data[3];
		if (l > 8)
			datalen += 4;
		*parm = MODE_SELECT_10;
		*datap = p;
	} else {
		scsi_cmd[0] = MODE_SENSE_10;
		scsi_cmd[9] = scsi_cmd[5];
		scsi_cmd[3] = scsi_cmd[4] = scsi_cmd[5] = scsi_cmd[6] = 0;
		if (l > 8)
			datalen += 4;
		*datap = xmalloc (uae_u8, datalen);
		*len = 10;
		*parm = MODE_SENSE_10;
	}
	*datalenp = datalen;
}

void scsi_atapi_fixup_post (uae_u8 *scsi_cmd, int len, uae_u8 *olddata, uae_u8 *data, int *datalenp, int parm)
{
	int datalen = *datalenp;
	if (!data || !datalen)
		return;
	if (parm == MODE_SENSE_10) {
		olddata[0] = data[1];
		olddata[1] = data[2];
		olddata[2] = data[3];
		olddata[3] = data[7];
		datalen -= 4;
		if (datalen > 4)
			memcpy (olddata + 4, data + 8, datalen - 4);
		*datalenp = datalen;
	}
}

static void scsi_atapi_fixup_inquiry (struct amigascsi *as)
{
	uae_u8 *scsi_data = as->data;
	uae_u32 scsi_len = as->len;
	uae_u8 *scsi_cmd = as->cmd;
	uae_u8 cmd;

	cmd = scsi_cmd[0];
	/* CDROM INQUIRY: most Amiga programs expect ANSI version == 2
	* (ATAPI normally responds with zero)
	*/
	if (cmd == 0x12 && scsi_len > 2 && scsi_data) {
		uae_u8 per = scsi_data[0];
		uae_u8 b = scsi_data[2];
		/* CDROM and ANSI version == 0 ? */
		if ((per & 31) == 5 && (b & 7) == 0) {
			b |= 2;
			scsi_data[2] = b;
		}
	}
}

void scsi_log_before (uae_u8 *cdb, int cdblen, uae_u8 *data, int datalen)
{
	int i;
	for (i = 0; i < cdblen; i++) {
		write_log (L"%s%02X", i > 0 ? "." : "", cdb[i]);
	}
	write_log (L"\n");
	if (data) {
		write_log (L"DATAOUT: %d\n", datalen);
		for (i = 0; i < datalen && i < 100; i++)
			write_log (L"%s%02X", i > 0 ? "." : "", data[i]);
		if (datalen > 0)
			write_log (L"\n");
	}
}

void scsi_log_after (uae_u8 *data, int datalen, uae_u8 *sense, int senselen)
{
	int i;
	write_log (L"DATAIN: %d\n", datalen);
	for (i = 0; i < datalen && i < 100 && data; i++)
		write_log (L"%s%02X", i > 0 ? "." : "", data[i]);
	if (data && datalen > 0)
		write_log (L"\n");
	if (senselen > 0) {
		write_log (L"SENSE: %d,", senselen);
		for (i = 0; i < senselen && i < 32; i++) {
			write_log (L"%s%02X", i > 0 ? "." : "", sense[i]);
		}
		write_log (L"\n");
	}
}

uae_u8 *save_cd (int num, int *len)
{
	uae_u8 *dstbak, *dst;

	if (num != 0)
		return NULL;
	if (!currprefs.cdimagefile[0])
		return NULL;

	dstbak = dst = xmalloc (uae_u8, 4 + 256);
	save_u32 (4);
	save_string (currprefs.cdimagefile);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_cd (int unit, uae_u8 *src)
{
	uae_u32 flags;
	TCHAR *s;

	flags = restore_u32 ();
	s = restore_string ();
	if ((flags & 4) && unit == 0) {
		_tcscpy (changed_prefs.cdimagefile, s);
		_tcscpy (currprefs.cdimagefile, s);
	}
	return src;
}

static bool nodisk (struct device_info *di)
{
	return di->media_inserted == 0;
}
static uae_u64 cmd_readx (int unitnum, uae_u8 *dataptr, int offset, int len)
{
	if (device_func[DF_IOCTL]->read (unitnum, dataptr, offset, len))
		return len;
	else
		return 0;
}

static void wl (uae_u8 *p, int v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static void ww (uae_u8 *p, int v)
{
	p[0] = v >> 8;
	p[1] = v;
}
static int rl (uae_u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}

static struct cd_toc *gettoc (struct device_info *di, int block)
{
	for (int i = di->toc.first_track_offset; i < di->toc.last_track_offset; i++) {
		struct cd_toc *t = &di->toc.toc[i];
		if (t->paddress > block) {
			if (t->point == 1)
				return t;
			return t - 1;
		}
	}
	return &di->toc.toc[di->toc.last_track_offset];
}

static int scsi_emulate (int unitnum, uae_u8 *cmdbuf, int scsi_cmd_len,
	uae_u8 *scsi_data, int *data_len, uae_u8 *r, int *reply_len, uae_u8 *s, int *sense_len)
{
	uae_u64 len, offset;
	int lr = 0, ls = 0;
	int scsi_len = -1;
	int status = 0;
	int i;
	char *ss;
	struct device_info di;

	*reply_len = *sense_len = 0;
	memset (r, 0, 256);
	memset (s, 0, 256);

	sys_command_info (DF_IOCTL, unitnum, &di, 1);

	if (cmdbuf[0] == 0) { /* TEST UNIT READY */
		if (nodisk (&di))
			goto nodisk;
		scsi_len = 0;
		goto end;
	}
		
	switch (cmdbuf[0])
	{
	case 0x12: /* INQUIRY */
		if ((cmdbuf[1] & 1) || cmdbuf[2] != 0)
			goto err;
		len = cmdbuf[4];
		if (cmdbuf[1] >> 5)
			goto err;
		r[0] = 5; // CDROM
		r[1] |= 0x80; // removable
		r[2] = 2; /* supports SCSI-2 */
		r[3] = 2; /* response data format */
		r[4] = 32; /* additional length */
		r[7] = 0x20; /* 16 bit bus */
		scsi_len = lr = len < 36 ? (uae_u32)len : 36;
		r[2] = 2;
		r[3] = 2;
		ss = "UAE";
		i = 0; /* vendor id */
		while (i < 8 && ss[i]) {
			r[8 + i] = ss[i];
			i++;
		}
		while (i < 8) {
			r[8 + i] = 32;
			i++;
		}
		char tmp[256];
		sprintf (tmp, "SCSI CD%d EMU", unitnum);
		ss = tmp;
		i = 0; /* product id */
		while (i < 16 && ss[i]) {
			r[16 + i] = ss[i];
			i++;
		}
		while (i < 16) {
			r[16 + i] = 32;
			i++;
		}
		ss = "0.1";
		i = 0; /* product revision */
		while (i < 4 && ss[i]) {
			r[32 + i] = ss[i];
			i++;
		}
		while (i < 4) {
			r[32 + i] = 32;
			i++;
		}
		break;
	case 0x1a: /* MODE SENSE(6) */
		{
			uae_u8 *p;
			int pc = cmdbuf[2] >> 6;
			int pcode = cmdbuf[2] & 0x3f;
			int dbd = cmdbuf[1] & 8;
			int cyl, cylsec, head, tracksec;
			if (nodisk (&di))
				goto nodisk;
			cyl = di.cylinders;
			head = 1;
			cylsec = tracksec = di.trackspercylinder;
			//write_log (L"MODE SENSE PC=%d CODE=%d DBD=%d\n", pc, pcode, dbd);
			p = r;
			p[0] = 4 - 1;
			p[1] = 0;
			p[2] = 0;
			p[3] = 0;
			p += 4;
			if (!dbd) {
				uae_u32 blocks = di.sectorspertrack * di.cylinders * di.trackspercylinder;
				p[-1] = 8;
				wl(p + 0, blocks);
				wl(p + 4, di.bytespersector);
				p += 8;
			}
			if (pcode == 0) {
				p[0] = 0;
				p[1] = 0;
				p[2] = 0x20;
				p[3] = 0;
				r[0] += 4;
			} else if (pcode == 3) {
				p[0] = 3;
				p[1] = 24;
				p[3] = 1;
				p[10] = tracksec >> 8;
				p[11] = tracksec;
				p[12] = di.bytespersector >> 8;
				p[13] = di.bytespersector;
				p[15] = 1; // interleave
				p[20] = 0x80;
				r[0] += p[1];
			} else if (pcode == 4) {
				p[0] = 4;
				wl(p + 1, cyl);
				p[1] = 24;
				p[5] = head;
				wl(p + 13, cyl);
				ww(p + 20, 0);
				r[0] += p[1];
			} else {
				goto err;
			}
			r[0] += r[3];
			scsi_len = lr = r[0] + 1;
			break;
		}
		break;
	case 0x1d: /* SEND DIAGNOSTICS */
		scsi_len = 0;
		break;
	case 0x25: /* READ_CAPACITY */
		{
			int pmi = cmdbuf[8] & 1;
			uae_u32 lba = (cmdbuf[2] << 24) | (cmdbuf[3] << 16) | (cmdbuf[4] << 8) | cmdbuf[5];
			int cyl, cylsec, head, tracksec;
			if (nodisk (&di))
				goto nodisk;
			uae_u32 blocks = di.sectorspertrack * di.cylinders * di.trackspercylinder;
			cyl = di.cylinders;
			head = 1;
			cylsec = tracksec = di.trackspercylinder;
			if (pmi == 0 && lba != 0)
				goto errreq;
			if (pmi) {
				lba += tracksec * head;
				lba /= tracksec * head;
				lba *= tracksec * head;
				if (lba > blocks)
					lba = blocks;
				blocks = lba;
			}
			wl (r, blocks);
			wl (r + 4, di.bytespersector);
			scsi_len = lr = 8;
		}
		break;
	case 0x08: /* READ (6) */
	{
		if (nodisk (&di))
			goto nodisk;
		offset = ((cmdbuf[1] & 31) << 16) | (cmdbuf[2] << 8) | cmdbuf[3];
		struct cd_toc *t = gettoc (&di, offset);
		if ((t->control & 0x0c) == 0x04) {
			len = cmdbuf[4];
			if (!len)
				len = 256;
			scsi_len = (uae_u32)cmd_readx (unitnum, scsi_data, offset, len) * di.bytespersector;;
		} else {
			goto notdatatrack;
		}
	}
	break;
	case 0x0a: /* WRITE (6) */
		goto readprot;
	case 0x28: /* READ (10) */
	{
		if (nodisk (&di))
			goto nodisk;
		offset = rl (cmdbuf + 2);
		struct cd_toc *t = gettoc (&di, offset);
		if ((t->control & 0x0c) == 0x04) {
			len = rl (cmdbuf + 7 - 2) & 0xffff;
			scsi_len = cmd_readx (unitnum, scsi_data, offset, len) * di.bytespersector;
		} else {
			goto notdatatrack;
		}
	}
	break;
	case 0x2a: /* WRITE (10) */
		goto readprot;
	case 0xa8: /* READ (12) */
	{
		if (nodisk (&di))
			goto nodisk;
		offset = rl (cmdbuf + 2);
		struct cd_toc *t = gettoc (&di, offset);
		if ((t->control & 0x0c) == 0x04) {
			len = rl (cmdbuf + 6);
			scsi_len = (uae_u32)cmd_readx (unitnum, scsi_data, offset, len) * di.bytespersector;;
		} else {
			goto notdatatrack;
		}
	}
	break;
	case 0xaa: /* WRITE (12) */
		goto readprot;
	case 0x43: // READ TOC
		{
			uae_u8 *p = scsi_data;
			int strack = cmdbuf[6];
			int msf = cmdbuf[1] & 2;
			int maxlen = (cmdbuf[7] << 8) | cmdbuf[8];
			struct cd_toc_head *toc = sys_command_cd_toc (DF_IOCTL, unitnum);
			if (!toc)
				goto readerr;
			if (maxlen < 4)
				goto errreq;
			if (strack == 0)
				strack = toc->first_track;
			if (strack >= 100 && strack != 0xaa)
				goto errreq;
			if (strack == 0xaa)
				strack = 0xa2;
			uae_u8 *p2 = p + 4;
			p[2] = 0;
			p[3] = 0;
			for (int i = 0; i < toc->points; i++) {
				if (strack == toc->toc[i].point) {
					int trk = strack >= 100 ? 0xaa : strack;
					if (p[2] == 0)
						p[2] = trk;
					p[3] = trk;
					int addr = toc->toc[i].paddress;
					if (msf)
						addr = lsn2msf (addr);
					if (p2 - p + 8 > maxlen)
						goto errreq;
					p2[0] = 0;
					p2[1] = (toc->toc[i].adr << 4) | toc->toc[i].control;
					p2[2] = trk;
					p2[3] = 0;
					p2[4] = addr >> 24;
					p2[5] = addr >> 16;
					p2[6] = addr >>  8;
					p2[7] = addr >>  0;
					p2 += 8;
					strack++;
				}
				if (i == toc->points - 1) {
					if (strack >= 0xa2)
						break;
					i = 0;
					strack = 0xa2;
				}
			}
			int tlen = p2 - (p + 4);
			p[0] = tlen >> 8;
			p[1] = tlen >> 0;
			scsi_len = tlen + 4;
		}
		break;
readprot:
		status = 2; /* CHECK CONDITION */
		s[0] = 0x70;
		s[2] = 7; /* DATA PROTECT */
		s[12] = 0x27; /* WRITE PROTECTED */
		ls = 12;
		break;
nodisk:
		status = 2; /* CHECK CONDITION */
		s[0] = 0x70;
		s[2] = 2; /* NOT READY */
		s[12] = 0x3A; /* MEDIUM NOT PRESENT */
		ls = 12;
		break;
readerr:
		status = 2; /* CHECK CONDITION */
		s[0] = 0x70;
		s[2] = 2; /* NOT READY */
		s[12] = 0x11; /* UNRECOVERED READ ERROR */
		ls = 12;
		break;
notdatatrack:
		status = 2;
		s[0] = 0x70;
		s[2] = 5;
		s[12] = 0x64; /* ILLEGAL MODE FOR THIS TRACK */
		ls = 12;
		break;

	default:
err:
		write_log (L"CDEMU: unsupported scsi command 0x%02X\n", cmdbuf[0]);
errreq:
		lr = -1;
		status = 2; /* CHECK CONDITION */
		s[0] = 0x70;
		s[2] = 5; /* ILLEGAL REQUEST */
		s[12] = 0x24; /* ILLEGAL FIELD IN CDB */
		ls = 12;
		break;
	}
end:
	*data_len = scsi_len;
	*reply_len = lr;
	*sense_len = ls;
	return status;
}

static int execscsicmd_direct (int unitnum, struct amigascsi *as)
{
	int sactual = 0;
	int io_error = 0;
	uae_u8 *scsi_datap, *scsi_datap_org;
	uae_u32 scsi_cmd_len_orig = as->cmd_len;
	uae_u8 cmd[16];
	uae_u8 replydata[256];
	int datalen = as->len;
	int senselen = as->sense_len;
	int replylen = 0;

	memcpy (cmd, as->cmd, as->cmd_len);
	scsi_datap = scsi_datap_org = as->len ? as->data : 0;
	if (as->sense_len > 32)
		as->sense_len = 32;

	as->status = scsi_emulate (unitnum, cmd, as->cmd_len, scsi_datap, &datalen, replydata, &replylen, as->sensedata, &senselen);

	as->cmdactual = as->status == 0 ? 0 : as->cmd_len; /* fake scsi_CmdActual */
	if (as->status) {
		io_error = 45; /* HFERR_BadStatus */
		as->sense_len = senselen;
		as->actual = 0; /* scsi_Actual */
	} else {
		int i;
		if (replylen > 0) {
			for (i = 0; i < replylen; i++)
				scsi_datap[i] = replydata[i];
			datalen = replylen;
		}
		for (i = 0; i < as->sense_len; i++)
			as->sensedata[i] = 0;
		sactual = 0;
		if (datalen < 0) {
			io_error = 20; /* io_Error, but not specified */
			as->actual = 0; /* scsi_Actual */
		} else {
			as->len = datalen;
			io_error = 0;
			as->actual = as->len; /* scsi_Actual */
		}
	}
	as->sactual = sactual;

	return io_error;
}

int sys_command_scsi_direct_native (int unitnum, struct amigascsi *as)
{
	if (scsiemu) {
		return execscsicmd_direct (unitnum, as);
	} else {
		if (!device_func[DF_SCSI] || !device_func[DF_SCSI]->exec_direct)
			return -1;
	}
	int ret = device_func[DF_SCSI]->exec_direct (unitnum, as);
	if (!ret && device_func[DF_SCSI]->isatapi(unitnum))
		scsi_atapi_fixup_inquiry (as);
	return ret;
}

int sys_command_scsi_direct (int unitnum, uaecptr acmd)
{
	int ret, i;
	struct amigascsi as;
	uaecptr ap;
	addrbank *bank;

	ap = get_long (acmd + 0);
	as.len = get_long (acmd + 4);

	bank = &get_mem_bank (ap);
	if (!bank || !bank->check(ap, as.len))
		return -5;
	as.data = bank->xlateaddr (ap);

	ap = get_long (acmd + 12);
	as.cmd_len = get_word (acmd + 16);
	for (i = 0; i < as.cmd_len; i++)
		as.cmd[i] = get_byte (ap++);
	as.flags = get_byte (acmd + 20);
	as.sense_len = get_word (acmd + 26);

	ret = sys_command_scsi_direct_native (unitnum, &as);

	put_long (acmd + 8, as.actual);
	put_word (acmd + 18, as.cmdactual);
	put_byte (acmd + 21, as.status);
	put_word (acmd + 28, as.sactual);

	ap = get_long (acmd + 22);
	for (i = 0; i < as.sactual; i++)
		put_byte (ap, as.sensedata[i]);

	return ret;
}
