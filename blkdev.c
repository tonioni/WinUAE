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

static struct device_functions *device_func[2];
static int have_ioctl;

#ifdef _WIN32

static int initialized;

#include "od-win32/win32.h"

extern struct device_functions devicefunc_win32_aspi;
extern struct device_functions devicefunc_win32_spti;
extern struct device_functions devicefunc_win32_ioctl;

static void install_driver (int flags)
{
    int installed = 0;

    device_func[DF_SCSI] = &devicefunc_win32_aspi;
#ifdef WINDDK
    if (os_winnt) {
	device_func[DF_IOCTL] = &devicefunc_win32_ioctl;
	device_func[DF_SCSI] = &devicefunc_win32_spti;
	installed = 1;
    }
    if (currprefs.win32_uaescsimode == UAESCSI_ADAPTECASPI ||
	currprefs.win32_uaescsimode == UAESCSI_NEROASPI ||
	currprefs.win32_uaescsimode == UAESCSI_FROGASPI ||
	!installed) {
	device_func[DF_SCSI] = &devicefunc_win32_aspi;
	device_func[DF_IOCTL] = 0;
    }
#endif
}
#endif

int sys_command_open (int mode, int unitnum)
{
    if (mode == DF_SCSI || !have_ioctl)
	return device_func[DF_SCSI]->opendev (unitnum);
    else
	return device_func[DF_IOCTL]->opendev (unitnum);
}

void sys_command_close (int mode, int unitnum)
{
    if (mode == DF_SCSI || !have_ioctl)
	device_func[DF_SCSI]->closedev (unitnum);
    else
	device_func[DF_IOCTL]->closedev (unitnum);
}

void device_func_reset (void)
{
    initialized = 0;
    have_ioctl = 0;
}

int device_func_init (int flags)
{
    int support_scsi = 0, support_ioctl = 0;
    int oflags = (flags & DEVICE_TYPE_SCSI) ? 0 : (1 << INQ_ROMD);

    if (initialized)
	return initialized;
    install_driver (flags);
    if (device_func[DF_IOCTL])
	have_ioctl = 1;
    else
	have_ioctl = 0;
    support_scsi = device_func[DF_SCSI]->openbus (oflags) ? 1 : 0;
    if (have_ioctl)
	support_ioctl = device_func[DF_IOCTL]->openbus (1 << INQ_ROMD) ? 1 : 0;
    initialized = 1;
    write_log ("support_scsi = %d support_ioctl = %d\n", support_scsi, support_ioctl);
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

/* play CD audio */
int sys_command_cd_play (int mode, int unitnum,uae_u32 startmsf, uae_u32 endmsf, int scan)
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
    return device_func[DF_IOCTL]->play (unitnum, startmsf, endmsf, scan);
}

/* read qcode */
uae_u8 *sys_command_cd_qcode (int mode, int unitnum)
{
    if (mode == DF_SCSI || !have_ioctl) {
	uae_u8 cmd[10] = {0x42,2,0x40,1,0,0,0,(uae_u8)(DEVICE_SCSI_BUFSIZE>>8),(uae_u8)(DEVICE_SCSI_BUFSIZE&0xff),0};
	return  device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
    }
    return device_func[DF_IOCTL]->qcode (unitnum);
};

/* read table of contents */
uae_u8 *sys_command_cd_toc (int mode, int unitnum)
{
    if (mode == DF_SCSI || !have_ioctl) {
	uae_u8 cmd [10] = { 0x43,0,2,0,0,0,1,(uae_u8)(DEVICE_SCSI_BUFSIZE>>8),(uae_u8)(DEVICE_SCSI_BUFSIZE&0xff),0};
	return device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof(cmd), 0);
    }
    return device_func[DF_IOCTL]->toc (unitnum);
}

/* read one cd sector */
uae_u8 *sys_command_cd_read (int mode, int unitnum, int offset)
{
    if (mode == DF_SCSI || !have_ioctl) {
	uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, 1, 0x10, 0, 0 };
	cmd[3] = (uae_u8)(offset >> 16);
	cmd[4] = (uae_u8)(offset >> 8);
	cmd[5] = (uae_u8)(offset >> 0);
	return device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
    }
    return device_func[DF_IOCTL]->read (unitnum, offset);
}
uae_u8 *sys_command_cd_rawread (int mode, int unitnum, int offset, int size)
{
    if (mode == DF_SCSI || !have_ioctl) {
	uae_u8 cmd[12] = { 0xbe, 0, 0, 0, 0, 0, 0, 0, 1, 0x10, 0, 0 };
	cmd[3] = (uae_u8)(offset >> 16);
	cmd[4] = (uae_u8)(offset >> 8);
	cmd[5] = (uae_u8)(offset >> 0);
	return device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
    }
    return device_func[DF_IOCTL]->rawread (unitnum, offset, size);
}

/* read block */
uae_u8 *sys_command_read (int mode, int unitnum, int offset)
{
    if (mode == DF_SCSI || !have_ioctl) {
	uae_u8 cmd[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
	cmd[2] = (uae_u8)(offset >> 24);
	cmd[3] = (uae_u8)(offset >> 16);
	cmd[4] = (uae_u8)(offset >> 8);
	cmd[5] = (uae_u8)(offset >> 0);
	return device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0);
    }
    return device_func[DF_IOCTL]->read (unitnum, offset);
}

/* write block */
int sys_command_write (int mode, int unitnum, int offset)
{
    if (mode == DF_SCSI || !have_ioctl) {
	uae_u8 cmd[10] = { 0x2a, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
	cmd[2] = (uae_u8)(offset >> 24);
	cmd[3] = (uae_u8)(offset >> 16);
	cmd[4] = (uae_u8)(offset >> 8);
	cmd[5] = (uae_u8)(offset >> 0);
	if (device_func[DF_SCSI]->exec_in (unitnum, cmd, sizeof (cmd), 0))
	    return -1;
	return 0;
    }
    return device_func[DF_IOCTL]->write (unitnum, offset);
}

int sys_command_ismedia (int mode, int unitnum, int quick)
{
    struct device_info di;

    if (mode == DF_SCSI || !have_ioctl || !device_func[DF_IOCTL]->ismedia) {
	if (quick)
	    return -1;
	memset(&di, 0, sizeof di);
	device_func[DF_SCSI]->info (unitnum, &di);
	return di.media_inserted;
    } else {
	return device_func[DF_IOCTL]->ismedia (unitnum, quick);
    }
}

struct device_info *sys_command_info (int mode, int unitnum, struct device_info *di)
{
    if (mode == DF_SCSI || !have_ioctl)
	return device_func[DF_SCSI]->info (unitnum, di);
    else
	return device_func[DF_IOCTL]->info (unitnum, di);
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
	p = (uae_u8*)xmalloc (8 + datalen + 4);
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
	*datap = (uae_u8*)xmalloc (datalen);
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

int sys_command_scsi_direct_native(int unitnum, struct amigascsi *as)
{
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

void scsi_log_before (uae_u8 *cdb, int cdblen, uae_u8 *data, int datalen)
{
    int i;
    for (i = 0; i < cdblen; i++) {
	write_log ("%s%02.2X", i > 0 ? "." : "", cdb[i]);
    }
    write_log ("\n");
    if (data) {
	write_log ("DATAOUT: %d\n", datalen);
	for (i = 0; i < datalen && i < 100; i++)
	    write_log ("%s%02.2X", i > 0 ? "." : "", data[i]);
	if (datalen > 0)
	    write_log ("\n");
    }
}

void scsi_log_after (uae_u8 *data, int datalen, uae_u8 *sense, int senselen)
{
    int i;
    write_log ("DATAIN: %d\n", datalen);
    for (i = 0; i < datalen && i < 100 && data; i++)
	write_log ("%s%02.2X", i > 0 ? "." : "", data[i]);
    if (data && datalen > 0)
	write_log ("\n");
    if (senselen > 0) {
	write_log ("SENSE: %d,", senselen);
	for (i = 0; i < senselen && i < 32; i++) {
	    write_log ("%s%02.2X", i > 0 ? "." : "", sense[i]);
	}
	write_log ("\n");
    }
}
