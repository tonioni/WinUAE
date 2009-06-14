 /*
  * UAE - The Un*x Amiga Emulator
  *
  * CD32 Akiko emulation
  *
  * - C2P
  * - NVRAM
  * - CDROM
  *
  * Copyright 2001, 2002 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "events.h"
#include "savestate.h"
#include "blkdev.h"
#include "zfile.h"
#include "threaddep/thread.h"
#include "akiko.h"
#include "gui.h"
#include "crc32.h"
#include "uae.h"
#include "custom.h"
#include "newcpu.h"

#define AKIKO_DEBUG_NVRAM 0
#define AKIKO_DEBUG_IO 0
#define AKIKO_DEBUG_IO_CMD 0

// 43 48 49 4E 4F 4E 20 20 4F 2D 36 35 38 2D 32 20 32 34
#define FIRMWAREVERSION "CHINON  O-658-2 24"

static void irq (void)
{
#if AKIKO_DEBUG_IO > 1
    write_log (L"Akiko Interrupt\n");
#endif
    if (!(intreq & 8)) {
	INTREQ_0 (0x8000 | 0x0008);
    }
}

/*
 * CD32 1Kb NVRAM (EEPROM) emulation
 *
 * NVRAM chip is 24C08 CMOS EEPROM (1024x8 bits = 1Kb)
 * Chip interface is I2C (2 wire serial)
 * Akiko addresses used:
 * 0xb80030: bit 7 = SCL (clock), 6 = SDA (data)
 * 0xb80032: 0xb80030 data direction register (0 = input, 1 = output)
 *
 * Because I don't have any experience on I2C, following code may be
 * unnecessarily complex and not 100% correct..
 */

enum i2c { I2C_WAIT, I2C_START, I2C_DEVICEADDR,	I2C_WORDADDR, I2C_DATA };

/* size of EEPROM, don't try to change,
 * (hardcoded in Kickstart)
 */
#define NVRAM_SIZE 1024
/* max size of one write request */
#define NVRAM_PAGE_SIZE 16

static uae_u8 cd32_nvram[NVRAM_SIZE], nvram_writetmp[NVRAM_PAGE_SIZE];
static int nvram_address, nvram_writeaddr;
static int nvram_rw;
static int bitcounter = -1, direction = -1;
static uae_u8 nvram_byte;
static int scl_out, scl_in, scl_dir, oscl, sda_out, sda_in, sda_dir, osda;
static int sda_dir_nvram;
static int state = I2C_WAIT;

static void nvram_write (int offset, int len)
{
    struct zfile *f;

    if (!currprefs.cs_cd32nvram)
	return;
    f = zfile_fopen (currprefs.flashfile, L"rb+", ZFD_NORMAL);
    if (!f) {
	f = zfile_fopen (currprefs.flashfile, L"wb", 0);
	if (!f) return;
	zfile_fwrite (cd32_nvram, NVRAM_SIZE, 1, f);
    }
    zfile_fseek (f, offset, SEEK_SET);
    zfile_fwrite (cd32_nvram + offset, len, 1, f);
    zfile_fclose (f);
}

static void nvram_read (void)
{
    struct zfile *f;

    if (!currprefs.cs_cd32nvram)
	return;
    f = zfile_fopen (currprefs.flashfile, L"rb", ZFD_NORMAL);
    memset (cd32_nvram, 0, NVRAM_SIZE);
    if (!f) return;
    zfile_fread (cd32_nvram, NVRAM_SIZE, 1, f);
    zfile_fclose (f);
}

static void i2c_do (void)
{
#if AKIKO_DEBUG_NVRAM
    int i;
#endif
    sda_in = 1;
    if (!sda_dir_nvram && scl_out && oscl) {
	if (!sda_out && osda) { /* START-condition? */
	    state = I2C_DEVICEADDR;
	    bitcounter = 0;
	    direction = -1;
#if AKIKO_DEBUG_NVRAM
	    write_log (L"START\n");
#endif
	    return;
	} else if(sda_out && !osda) { /* STOP-condition? */
	    state = I2C_WAIT;
	    bitcounter = -1;
#if AKIKO_DEBUG_NVRAM
	    write_log (L"STOP\n");
#endif
	    if (direction > 0) {
		memcpy (cd32_nvram + (nvram_address & ~(NVRAM_PAGE_SIZE - 1)), nvram_writetmp, NVRAM_PAGE_SIZE);
		nvram_write (nvram_address & ~(NVRAM_PAGE_SIZE - 1), NVRAM_PAGE_SIZE);
		direction = -1;
#if AKIKO_DEBUG_NVRAM
		write_log (L"NVRAM write address %04X:", nvram_address & ~(NVRAM_PAGE_SIZE - 1));
		for (i = 0; i < NVRAM_PAGE_SIZE; i++)
		    write_log (L"%02X", nvram_writetmp[i]);
		write_log (L"\n");

#endif
	    }
	    return;
	}
    }
    if (bitcounter >= 0) {
	if (direction) {
	    /* Amiga -> NVRAM */
	    if (scl_out && !oscl) {
		if (bitcounter == 8) {
#if AKIKO_DEBUG_NVRAM
		    write_log (L"RB %02X ", nvram_byte, M68K_GETPC);
#endif
		    sda_in = 0; /* ACK */
		    if (direction > 0) {
			nvram_writetmp[nvram_writeaddr++] = nvram_byte;
			nvram_writeaddr &= 15;
			bitcounter = 0;
		    } else {
			bitcounter = -1;
		    }
		} else {
		    //write_log (L"NVRAM received bit %d, offset %d\n", sda_out, bitcounter);
		    nvram_byte <<= 1;
		    nvram_byte |= sda_out;
		    bitcounter++;
		}
	    }
	} else {
	    /* NVRAM -> Amiga */
	    if (scl_out && !oscl && bitcounter < 8) {
		if (bitcounter == 0)
		    nvram_byte = cd32_nvram[nvram_address];
		sda_dir_nvram = 1;
		sda_in = (nvram_byte & 0x80) ? 1 : 0;
		//write_log (L"NVRAM sent bit %d, offset %d\n", sda_in, bitcounter);
		nvram_byte <<= 1;
		bitcounter++;
		if (bitcounter == 8) {
#if AKIKO_DEBUG_NVRAM
		    write_log (L"NVRAM sent byte %02X address %04X PC=%08X\n", cd32_nvram[nvram_address], nvram_address, M68K_GETPC);
#endif
		    nvram_address++;
		    nvram_address &= NVRAM_SIZE - 1;
		    sda_dir_nvram = 0;
		}
	    }
	    if(!sda_out && sda_dir && !scl_out) /* ACK from Amiga */
		bitcounter = 0;
	}
	if (bitcounter >= 0) return;
    }
    switch (state)
    {
	case I2C_DEVICEADDR:
	if ((nvram_byte & 0xf0) != 0xa0) {
	    write_log (L"WARNING: I2C_DEVICEADDR: device address != 0xA0\n");
	    state = I2C_WAIT;
	    return;
	}
	nvram_rw = (nvram_byte & 1) ? 0 : 1;
	if (nvram_rw) {
	    /* 2 high address bits, only fetched if WRITE = 1 */
	    nvram_address &= 0xff;
	    nvram_address |= ((nvram_byte >> 1) & 3) << 8;
	    state = I2C_WORDADDR;
	    direction = -1;
	} else {
	    state = I2C_DATA;
	    direction = 0;
	    sda_dir_nvram = 1;
	}
	bitcounter = 0;
#if AKIKO_DEBUG_NVRAM
	write_log (L"I2C_DEVICEADDR: rw %d, address %02Xxx PC=%08X\n", nvram_rw, nvram_address >> 8, M68K_GETPC);
#endif
	break;
	case I2C_WORDADDR:
	nvram_address &= 0x300;
	nvram_address |= nvram_byte;
#if AKIKO_DEBUG_NVRAM
	write_log (L"I2C_WORDADDR: address %04X PC=%08X\n", nvram_address, M68K_GETPC);
#endif
	if (direction < 0) {
	    memcpy (nvram_writetmp, cd32_nvram + (nvram_address & ~(NVRAM_PAGE_SIZE - 1)), NVRAM_PAGE_SIZE);
	    nvram_writeaddr = nvram_address & (NVRAM_PAGE_SIZE - 1);
	}
	state = I2C_DATA;
	bitcounter = 0;
	direction = 1;
	break;
    }
}

static void akiko_nvram_write (int offset, uae_u32 v)
{
    int sda;
    switch (offset)
    {
	case 0:
	oscl = scl_out;
	scl_out = (v & 0x80) ? 1 : 0;
	osda = sda_out;
	sda_out = (v & 0x40) ? 1 : 0;
	break;
	case 2:
	scl_dir = (v & 0x80) ? 1 : 0;
	sda_dir = (v & 0x40) ? 1 : 0;
	break;
	default:
	return;
    }
    sda = sda_out;
    if (oscl != scl_out || osda != sda) {
	i2c_do ();
	oscl = scl_out;
	osda = sda;
    }
}

static uae_u32 akiko_nvram_read (int offset)
{
    uae_u32 v = 0;
    switch (offset)
    {
	case 0:
	if (!scl_dir)
	    v |= scl_in ? 0x80 : 0x00;
	    else
	    v |= scl_out ? 0x80 : 0x00;
	if (!sda_dir)
	    v |= sda_in ? 0x40 : 0x00;
	    else
	    v |= sda_out ? 0x40 : 0x00;
	break;
	case 2:
	v |= scl_dir ? 0x80 : 0x00;
	v |= sda_dir ? 0x40 : 0x00;
	break;
    }
    return v;
}

/* CD32 Chunky to Planar hardware emulation
 * Akiko addresses used:
 * 0xb80038-0xb8003b
 */

static uae_u32 akiko_buffer[8];
static int akiko_read_offset, akiko_write_offset;
static uae_u32 akiko_result[8];

static void akiko_c2p_do (void)
{
    int i;

    for (i = 0; i < 8; i++)
	akiko_result[i] = 0;
    /* FIXME: better c2p algoritm than this piece of crap.... */
    for (i = 0; i < 8 * 32; i++) {
	if (akiko_buffer[7 - (i >> 5)] & (1 << (i & 31)))
	    akiko_result[i & 7] |= 1 << (i >> 3);
    }
}

static void akiko_c2p_write (int offset, uae_u32 v)
{
    if (offset == 3)
	akiko_buffer[akiko_write_offset] = 0;
    akiko_buffer[akiko_write_offset] |= v << ( 8 * (3 - offset));
    if (offset == 0) {
	akiko_write_offset++;
	akiko_write_offset &= 7;
    }
    akiko_read_offset = 0;
}

static uae_u32 akiko_c2p_read (int offset)
{
    uae_u32 v;

    if (akiko_read_offset == 0 && offset == 3)
	akiko_c2p_do ();
    akiko_write_offset = 0;
    v = akiko_result[akiko_read_offset];
    if (offset == 0) {
	akiko_read_offset++;
	akiko_read_offset &= 7;
    }
    return v >> (8 * (3 - offset));
}

/* CD32 CDROM hardware emulation
 * Akiko addresses used:
 * 0xb80004-0xb80028
 *
 * I can't believe cd.device and custom loaders are fooled to think
 * this piece of crap emulates real CD32 CDROM controller and drive :)
 */

#define CDSTATUS_FRAME		    0x80000000
#define CDSTATUS_UNK1		    0x40000000 /* not used by ROM */
#define CDSTATUS_UNK2		    0x20000000 /* not used by ROM */
#define CDSTATUS_DATA_AVAILABLE	    0x10000000
#define CDSTATUS_DATASECTOR_ERROR   0x08000000 /* ?? */
#define CDSTATUS_DATASECTOR	    0x04000000

#define CDS_ERROR 0x80
#define CDS_PLAYING 0x08

#define AUDIO_STATUS_NOT_SUPPORTED  0x00
#define AUDIO_STATUS_IN_PROGRESS    0x11
#define AUDIO_STATUS_PAUSED         0x12
#define AUDIO_STATUS_PLAY_COMPLETE  0x13
#define AUDIO_STATUS_PLAY_ERROR     0x14
#define AUDIO_STATUS_NO_STATUS      0x15

#define CH_ERR_BADCOMMAND       0x80 // %10000000
#define CH_ERR_CHECKSUM         0x88 // %10001000
#define CH_ERR_DRAWERSTUCK      0x90 // %10010000
#define CH_ERR_DISKUNREADABLE   0x98 // %10011000
#define CH_ERR_INVALIDADDRESS   0xa0 // %10100000
#define CH_ERR_WRONGDATA        0xa8 // %10101000
#define CH_ERR_FOCUSERROR       0xc8 // %11001000
#define CH_ERR_SPINDLEERROR     0xd0 // %11010000
#define CH_ERR_TRACKINGERROR    0xd8 // %11011000
#define CH_ERR_SLEDERROR        0xe0 // %11100000
#define CH_ERR_TRACKJUMP        0xe8 // %11101000
#define CH_ERR_ABNORMALSEEK     0xf0 // %11110000
#define CH_ERR_NODISK           0xf8 // %11111000

static uae_u32 cdrom_status1, cdrom_status2;
static uae_u8 cdrom_status3;
static uae_u32 cdrom_address1, cdrom_address2;
static uae_u32 cdrom_longmask;
static uae_u32 cdrom_readmask_r, cdrom_readmask_w;
static uae_u8 cdrom_command_offset_complete; /* 0x19 */
static uae_u8 cdrom_command_offset_todo; /* 0x1d */
static uae_u8 cdrom_result_complete; /* 0x1a */
static uae_u8 cdrom_result_last_pos; /* 0x1f */
static uae_u8 cdrom_result_buffer[32];
static uae_u8 cdrom_command_buffer[32];
static uae_u8 cdrom_command;

#define	MAX_TOC_ENTRIES 103 /* tracks 1-99, A0, A1 and A2 */
static int cdrom_toc_entries;
static int cdrom_toc_counter;
static uae_u32 cdrom_toc_crc;
static uae_u8 cdrom_toc_buffer[MAX_TOC_ENTRIES * 13];
static uae_u8 cdrom_toc_cd_buffer[4 + MAX_TOC_ENTRIES * 11];

static int cdrom_disk, cdrom_paused, cdrom_playing;
static int cdrom_command_active;
static int cdrom_command_length;
static int cdrom_checksum_error;
static int cdrom_data_offset, cdrom_speed, cdrom_sector_counter;
static int cdrom_current_sector;
static int cdrom_data_end, cdrom_leadout;
static int cdrom_audiotimeout;
static int cdrom_led;
static int cdrom_dosomething;

static uae_u8 *sector_buffer_1, *sector_buffer_2;
static int sector_buffer_sector_1, sector_buffer_sector_2;
#define	SECTOR_BUFFER_SIZE 64
static uae_u8 *sector_buffer_info_1, *sector_buffer_info_2;

static int unitnum = -1;
static int cdromok = 0;
static int cd_hunt;

static void checkint (void)
{
    if (cdrom_status1 & cdrom_status2)
	irq ();
}

static void set_status (uae_u32 status)
{
    cdrom_status1 |= status;
    checkint ();
}

void rethink_akiko (void)
{
    checkint ();
}


static uae_u8 frombcd (uae_u8 v)
{
    return (v >> 4) * 10 + (v & 15);
}

static uae_u8 tobcd (uae_u8 v)
{
    return ((v / 10) << 4) | (v % 10);
}

static int fromlongbcd (uae_u8 *p)
{
    return (frombcd (p[0]) << 16) | (frombcd (p[1]) << 8) | (frombcd (p[2])  << 0);
}

/* convert minutes, seconds and frames -> logical sector number */
static int msf2lsn (int msf)
{
    int sector = (((msf >> 16) & 0xff) * 60 * 75 + ((msf >> 8) & 0xff) * 75 + ((msf >> 0) & 0xff)) - 150;
    if (sector < 0)
	sector = 0;
    return sector;
}

/* convert logical sector number -> minutes, seconds and frames */
static int lsn2msf (int sectors)
{
    int msf;
    sectors += 150;
    msf = (sectors / (75 * 60)) << 16;
    msf |= ((sectors / 75) % 60) << 8;
    msf |= (sectors % 75) << 0;
    return msf;
}

static void cdaudiostop (void)
{
    cdrom_playing = 0;
    cdrom_paused = 0;
    if (unitnum < 0)
	return;
    sys_command_cd_pause (DF_IOCTL, unitnum, 0);
    sys_command_cd_stop (DF_IOCTL, unitnum);
    sys_command_cd_pause (DF_IOCTL, unitnum, 1);
    cdrom_audiotimeout = 0;
}

static uae_u32 last_play_end;
static int cd_play_audio (uae_u32 startmsf, uae_u32 endmsf, int scan)
{
#if 1
    uae_u8 *buf = cdrom_toc_cd_buffer;
    uae_u8 *s;
    uae_u32 addr;
    int i;

    for (i = 0; i < cdrom_toc_entries; i++) {
	s = buf + 4 + i * 11;
	addr = (s[8] << 16) | (s[9] << 8) | (s[10] << 0);
	if (s[3] > 0 && s[3] < 100 && addr >= startmsf)
	    break;
    }
    if ((s[1] & 0x0c) == 0x04) {
	write_log (L"tried to play data track %d!\n", s[3]);
	s += 11;
	startmsf = (s[8] << 16) | (s[9] << 8) | (s[10] << 0);
	s += 11;
	endmsf = (s[8] << 16) | (s[9] << 8) | (s[10] << 0);
	//cdrom_audiotimeout = 312;
	return 0;
    }
#endif
    last_play_end = endmsf;
    cdrom_audiotimeout = 0;
    return sys_command_cd_play (DF_IOCTL, unitnum, startmsf, endmsf, scan);
}


/* read qcode */
static uae_u32 last_play_pos;
static int cd_qcode (uae_u8 *d)
{
    uae_u8 *buf, *s, as;

    if (d)
	memset (d, 0, 11);
    last_play_pos = 0;
    buf = sys_command_cd_qcode (DF_IOCTL, unitnum);
    if (!buf)
	return 0;
    as = buf[1];
    if (as != 0x11 && as != 0x12 && as != 0x13 && as != 0x15) /* audio status ok? */
	return 0;
    s = buf + 4;
    last_play_pos = (s[5] << 16) | (s[6] << 8) | (s[7] << 0);
    if (!d)
	return 0;
    /* ??? */
    d[0] = 0;
    /* CtlAdr */
    d[1] = (s[1] >> 4) | (s[1] << 4);
    /* Track */
    d[2] = tobcd (s[2]);
    /* Index */
    d[3] = tobcd (s[3]);
    /* TrackPos */
    d[4] = tobcd (s[9]);
    d[5] = tobcd (s[10]);
    d[6] = tobcd (s[11]);
    /* DiskPos */
    d[7] = 0;
    d[8] = tobcd (s[5]);
    d[9] = tobcd (s[6]);
    d[10] = tobcd (s[7]);
    if (as == 0x15) {
	/* Make sure end of disc position is passed.
	 */
	int lsn = msf2lsn ((s[5] << 16) | (s[6] << 8) | (s[7] << 0));
	int msf = lsn2msf (cdrom_leadout);
	if (lsn >= cdrom_leadout || cdrom_leadout - lsn < 10) {
	    d[8] = tobcd ((uae_u8)(msf >> 16));
	    d[9] = tobcd ((uae_u8)(msf >> 8));
	    d[10] = tobcd ((uae_u8)(msf >> 0));
	}
    }
    return 0;
}

/* read toc */
static int cdrom_toc (void)
{
    int i, j;
    int datatrack = 0, secondtrack = 0;
    uae_u8 *s, *d, *buf;

    cdrom_toc_counter = -1;
    cdrom_toc_entries = 0;
    buf = sys_command_cd_toc (DF_IOCTL, unitnum);
    if (!buf)
	return 1;
    i = (buf[0] << 8) | (buf[1] << 0);
    i -= 2;
    i /= 11;
    if (i > MAX_TOC_ENTRIES)
	return -1;
    cdrom_toc_entries = i;
    memset (cdrom_toc_buffer, 0, MAX_TOC_ENTRIES * 13);
    memcpy (cdrom_toc_cd_buffer, buf, 4 + cdrom_toc_entries * 11);
    cdrom_data_end = -1;
    for (j = 0; j < cdrom_toc_entries; j++) {
	uae_u32 addr;
	s = buf + 4 + j * 11;
	d = &cdrom_toc_buffer[j * 13];
	addr = msf2lsn ((s[8] << 16) | (s[9] << 8) | (s[10] << 0));
	d[1] = (s[1] >> 4) | (s[1] << 4);
	d[3] = s[3] < 100 ? tobcd(s[3]) : s[3];
	d[8] = tobcd (s[8]);
	d[9] = tobcd (s[9]);
	d[10] = tobcd (s[10]);
	if (s[3] == 1 && (s[1] & 0x0c) == 0x04)
	    datatrack = 1;
	if (s[3] == 2)
	    secondtrack = addr;
	if (s[3] == 0xa2)
	    cdrom_leadout = addr;
    }
    cdrom_toc_crc = get_crc32(cdrom_toc_buffer, cdrom_toc_entries * 13);
    if (datatrack) {
	if (secondtrack)
	    cdrom_data_end = secondtrack;
	else
	    cdrom_data_end = cdrom_leadout;
    }
    return 0;
}

/* open device */
static int sys_cddev_open (void)
{
    int first = -1;
    struct device_info di1, *di2;
    int cd32unit = -1;
    int audiounit = -1;

    for (unitnum = 0; unitnum < MAX_TOTAL_DEVICES; unitnum++) {
	if (sys_command_open (DF_IOCTL, unitnum)) {
	    di2 = sys_command_info (DF_IOCTL, unitnum, &di1);
	    if (di2 && di2->type == INQ_ROMD) {
		write_log (L"%s: ", di2->label);
		if (first < 0)
		    first = unitnum;
		if (!cdrom_toc ()) {
		    if (cdrom_data_end > 0) {
			uae_u8 *p = sys_command_cd_read (DF_IOCTL, unitnum, 16);
			if (p) {
			    if (!memcmp (p + 8, "CDTV", 4) || !memcmp (p + 8, "CD32", 4)) {
				uae_u32 crc;
				write_log (L"CD32 or CDTV");
				if (cd32unit < 0)
				    cd32unit = unitnum;
				p = sys_command_cd_read (DF_IOCTL, unitnum, 21);
				crc = get_crc32 (p, 2048);
				if (crc == 0xe56c340f)
				    write_log (L" [CD32.TM]");
				write_log (L"\n");
			    } else {
				write_log (L"non CD32/CDTV data CD\n");
			    }
			} else {
			    write_log (L"read error\n");
			}
		    } else {
			write_log (L"Audio CD\n");
			if (audiounit < 0)
			    audiounit = unitnum;
		    }
		} else {
		    write_log (L"can't read TOC\n");
		}
	    }
	    sys_command_close (DF_IOCTL, unitnum);
	}
    }
    unitnum = audiounit;
    if (cd32unit >= 0)
	unitnum = cd32unit;
    if (unitnum < 0)
	unitnum = first;
    if (unitnum < 0)
	return 1;
    if (!sys_command_open (DF_IOCTL, unitnum))
	write_log (L"re-opening unit %d failed!\n", unitnum);
    di2 = sys_command_info (DF_IOCTL, unitnum, &di1);
    if (!di2) {
	write_log (L"unit %d info failed\n", unitnum);
	sys_command_close (DF_IOCTL, unitnum);
	return 1;
    }
    if (!sys_command_ismedia(DF_IOCTL, unitnum, 0))
	cd_hunt = 1;
    write_log (L"using drive %s (unit %d, media %d)\n", di2->label, unitnum, di2->media_inserted);
    /* make sure CD audio is not playing */
    cdaudiostop ();
    return 0;
}

/* close device */
static void sys_cddev_close (void)
{
    cdaudiostop ();
    sys_command_close (DF_IOCTL, unitnum);
}

static int command_lengths[] = { 1,2,1,1,12,2,1,1,4,1,-1,-1,-1,-1,-1 };

static void cdrom_return_data (int len)
{
    uae_u32 cmd_buf = cdrom_address2;
    int i;
    uae_u8 checksum;

    if (len <= 0)
	return;
#if AKIKO_DEBUG_IO_CMD
    write_log (L"OUT:");
#endif
    checksum = 0xff;
    for (i = 0; i < len; i++) {
	checksum -= cdrom_result_buffer[i];
	put_byte (cmd_buf + ((cdrom_result_complete + i) & 0xff), cdrom_result_buffer[i]);
#if AKIKO_DEBUG_IO_CMD
	write_log (L"%02X ", cdrom_result_buffer[i]);
#endif
    }
    put_byte (cmd_buf + ((cdrom_result_complete + len) & 0xff), checksum);
#if AKIKO_DEBUG_IO_CMD
    write_log (L"%02X\n", checksum);
#endif
    cdrom_result_complete += len + 1;
    set_status (CDSTATUS_DATA_AVAILABLE);
}

static int cdrom_command_led (void)
{
    int v = cdrom_command_buffer[1];
    int old = cdrom_led;
    cdrom_led = v & 1;
    if (cdrom_led != old)
	gui_cd_led (0, cdrom_led ? 1 : -1);
    if (v & 0x80) {
	cdrom_result_buffer[0] = cdrom_command;
	cdrom_result_buffer[1] = cdrom_led;
	return 2;
    }
    return 0;
}

static int cdrom_command_media_status (void)
{
    cdrom_result_buffer[0] = 10;
    cdrom_result_buffer[1] = sys_command_ismedia (DF_IOCTL, unitnum, 0);
    return 2;
}

/* check if cd drive door is open or closed, return firmware info */
static int cdrom_command_status (void)
{
    cdrom_result_buffer[1] = 0x01;
    //cdrom_result_buffer[1] = 0x80; door open
    if (unitnum >= 0)
	cdrom_toc ();
    /* firmware info */
    strcpy (cdrom_result_buffer + 2, FIRMWAREVERSION);
    cdrom_result_buffer[0] = cdrom_command;
    return 20;
}

/* return one TOC entry */
static int cdrom_return_toc_entry (void)
{
    cdrom_result_buffer[0] = 6;
    if (cdrom_toc_entries == 0) {
	cdrom_result_buffer[1] = CDS_ERROR;
	return 15;
    }
    cdrom_result_buffer[1] = 0;
    memcpy (cdrom_result_buffer + 2, cdrom_toc_buffer + cdrom_toc_counter * 13, 13);
    cdrom_toc_counter++;
    if (cdrom_toc_counter >= cdrom_toc_entries)
	cdrom_toc_counter = -1;
    return 15;
}
static int checkerr (void)
{
    if (!cdrom_disk) {
	cdrom_result_buffer[1] = CH_ERR_NODISK;
	return 1;
    }
    return 0;
}

/* pause CD audio */
static int cdrom_command_pause (void)
{
    cdrom_toc_counter = -1;
    cdrom_result_buffer[0] = cdrom_command;
    if (checkerr ())
	return 2;
    cdrom_result_buffer[1] = cdrom_playing ? CDS_PLAYING : 0;
    if (!cdrom_playing)
	return 2;
    if (cdrom_paused)
	return 2;
    cdrom_audiotimeout = 0;
    sys_command_cd_pause (DF_IOCTL, unitnum,1);
    cdrom_paused = 1;
    return 2;
}

/* unpause CD audio */
static int cdrom_command_unpause (void)
{
    cdrom_result_buffer[0] = cdrom_command;
    if (checkerr ())
	return 2;
    cdrom_result_buffer[1] = cdrom_playing ? CDS_PLAYING : 0;
    if (!cdrom_paused)
	return 2;
    if (!cdrom_playing)
	return 2;
    cdrom_paused = 0;
    sys_command_cd_pause (DF_IOCTL, unitnum,0);
    return 2;
}

/* seek	head/play CD audio/read	data sectors */
static int cdrom_command_multi (void)
{
    int seekpos = fromlongbcd (cdrom_command_buffer + 1);
    int endpos = fromlongbcd (cdrom_command_buffer + 4);

    if (cdrom_playing)
	cdaudiostop ();
    cdrom_speed = (cdrom_command_buffer[8] & 0x40) ? 2 : 1;
    cdrom_result_buffer[0] = cdrom_command;
    cdrom_result_buffer[1] = 0;
    if (!cdrom_disk) {
	cdrom_result_buffer[1] = 1; // no disk
	return 2;
    }

    if (cdrom_command_buffer[7] == 0x80) {    /* data read */
	int cdrom_data_offset_end = msf2lsn (endpos);
	cdrom_data_offset = msf2lsn (seekpos);
#if AKIKO_DEBUG_IO_CMD
	write_log (L"READ DATA %06X (%d) - %06X (%d) SPD=%dx PC=%08X\n",
	    seekpos, cdrom_data_offset, endpos, cdrom_data_offset_end, cdrom_speed, M68K_GETPC);
#endif
	cdrom_result_buffer[1] |= 0x02;
    } else if (cdrom_command_buffer[10] & 4) { /* play audio */
	int scan = 0;
	if (cdrom_command_buffer[7] & 0x04)
	    scan = 1;
	else if (cdrom_command_buffer[7] & 0x08)
	    scan = -1;
#if AKIKO_DEBUG_IO_CMD
	write_log (L"PLAY FROM %06X (%d) to %06X (%d) SCAN=%d\n",
	    seekpos, msf2lsn (seekpos), endpos, msf2lsn (endpos), scan);
#endif
	if (!cd_play_audio (seekpos, endpos, 0)) {
	    cdrom_result_buffer[1] = CDS_ERROR;
	} else {
	    cdrom_playing = 1;
	    cdrom_result_buffer[1] |= CDS_PLAYING;
	}
    } else {
#if AKIKO_DEBUG_IO_CMD
	write_log (L"SEEKTO %06X\n",seekpos);
#endif
	if (seekpos < 150)
	    cdrom_toc_counter = 0;
	else
	    cdrom_toc_counter = -1;
    }
    return 2;
}

/* return subq entry */
static int cdrom_command_subq (void)
{
    cdrom_result_buffer[0] = cdrom_command;
    cdrom_result_buffer[1] = 0;
    if (cd_qcode (cdrom_result_buffer + 2))
	cdrom_result_buffer[1] = CDS_ERROR;
    return 15;
}

static void cdrom_run_command (void)
{
    uae_u32 cmd_buf = cdrom_address2 + 0x200;
    int i, cmd_len;
    uae_u8 checksum;

    for (;;) {
	if (cdrom_command_active)
	    return;
	if (cdrom_command_offset_complete == cdrom_command_offset_todo)
	    return;
	cdrom_command = get_byte (cmd_buf + cdrom_command_offset_complete);
	if ((cdrom_command & 0xf0) == 0)
	    return;
	cdrom_checksum_error = 0;
	cmd_len = command_lengths[cdrom_command & 0x0f];
	if (cmd_len < 0) {
#if AKIKO_DEBUG_IO_CMD
	    write_log (L"unknown command\n");
#endif
	    cmd_len = 1;
	}
#if AKIKO_DEBUG_IO_CMD
	write_log (L"IN:");
#endif
	checksum = 0;
	for (i = 0; i < cmd_len + 1; i++) {
	    cdrom_command_buffer[i] = get_byte (cmd_buf + ((cdrom_command_offset_complete + i) & 0xff));
	    checksum += cdrom_command_buffer[i];
#if AKIKO_DEBUG_IO_CMD
	    write_log (L"%02X ", cdrom_command_buffer[i]);
#endif
	}
	if (checksum!=0xff) {
#if AKIKO_DEBUG_IO_CMD
	    write_log (L" checksum error");
#endif
	    cdrom_checksum_error = 1;
	}
#if AKIKO_DEBUG_IO_CMD
	write_log (L"\n");
#endif
	cdrom_command_active = 1;
	cdrom_command_length = cmd_len;
	return;
    }
}

static void cdrom_run_command_run (void)
{
    int len;

    cdrom_command_offset_complete = (cdrom_command_offset_complete + cdrom_command_length + 1) & 0xff;
    memset (cdrom_result_buffer, 0, sizeof (cdrom_result_buffer));
    switch (cdrom_command & 0x0f)
    {
	case 2:
	len = cdrom_command_pause ();
	break;
	case 3:
	len = cdrom_command_unpause ();
	break;
	case 4:
	len = cdrom_command_multi ();
	break;
	case 5:
	cdrom_dosomething = 1; // this is a hack
	len = cdrom_command_led ();
	break;
	case 6:
	len = cdrom_command_subq ();
	break;
	case 7:
	len = cdrom_command_status ();
	break;
	default:
	len = 0;
	break;
    }
    if (len == 0)
	return;
    if (cdrom_checksum_error)
	cdrom_result_buffer[1] |= 0x80;
    cdrom_return_data (len);
}

extern void encode_l2 (uae_u8 *p, int address);

/* DMA transfer one CD sector */
static void cdrom_run_read (void)
{
    int i, j, sector;
    int read = 0;
    uae_u8 buf[2352];
    int sec;

    if (!(cdrom_longmask & 0x04000000))
	return;
    if (!cdrom_readmask_w)
	return;
    if (cdrom_data_offset < 0)
	return;
    j = cdrom_sector_counter & 15;
    if (unitnum >= 0 && (cdrom_readmask_w & (1 << j))) {
	sector = cdrom_current_sector = cdrom_data_offset + cdrom_sector_counter;
	sec = sector - sector_buffer_sector_1;
	if (sector_buffer_sector_1 >= 0 && sec >= 0 && sec < SECTOR_BUFFER_SIZE) {
	    if (sector_buffer_info_1[sec] != 0xff && sector_buffer_info_1[sec] != 0) {
		memcpy (buf + 16, sector_buffer_1 + sec * 2048, 2048);
		encode_l2 (buf, sector + 150);
		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = cdrom_sector_counter;
		for (i = 0; i < 2352; i++)
		    put_byte (cdrom_address1 + j * 4096 + i, buf[i]);
		cdrom_readmask_r |= 1 << j;
	    }
	    if (sector_buffer_info_1[sec] != 0xff)
		sector_buffer_info_1[sec]--;
	} else {
	    return;
	}
#if AKIKO_DEBUG_IO_CMD
	write_log (L"read sector=%d, scnt=%d -> %d. %08X\n",
	    cdrom_data_offset, cdrom_sector_counter, sector, cdrom_address1 + j * 4096);
#endif
	cdrom_readmask_w &= ~(1 << j);
    }
    cdrom_sector_counter++;
    if (cdrom_readmask_w == 0)
	set_status (CDSTATUS_DATASECTOR);

}

static uae_sem_t akiko_sem;
static int lastmediastate = 0;

static void akiko_handler (void)
{
    static int mediacheckcnt;

    if (unitnum < 0)
	return;
    if (cdrom_result_complete > cdrom_result_last_pos && cdrom_result_complete - cdrom_result_last_pos < 100) {
	//set_status (CDSTATUS_DATA_AVAILABLE);
	return;
    }
    if (cdrom_result_last_pos < cdrom_result_complete)
	return;
    if (mediacheckcnt > 0)
	mediacheckcnt--;
    if (mediacheckcnt == 0) {
	int media = sys_command_ismedia (DF_IOCTL, unitnum, 0);
	mediacheckcnt = 312 * 50 * 2;
	if (media != lastmediastate) {
	    write_log (L"media changed = %d\n", media);
	    lastmediastate = cdrom_disk = media;
	    cdrom_return_data (cdrom_command_media_status ());
	    if (!media)
		cd_hunt = 1;
	    cdrom_toc ();
	     /* do not remove! first try may fail */
	    cdrom_toc ();
	    return;
	}
    }
    if (cdrom_toc_counter >= 0 && !cdrom_command_active && cdrom_dosomething) {
	cdrom_return_data (cdrom_return_toc_entry ());
	cdrom_dosomething--;
	return;
    }
}

static void akiko_internal (void)
{
    if (!currprefs.cs_cd32cd)
	return;
    cdrom_run_command ();
    if (cdrom_command_active > 0) {
	cdrom_command_active--;
	if (!cdrom_command_active)
	    cdrom_run_command_run ();
    }
}

static void do_hunt(void)
{
    int i;
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	if (sys_command_ismedia(DF_IOCTL, i, 1) > 0)
	    break;
    }
    if (i == MAX_TOTAL_DEVICES)
	return;
    if (unitnum >= 0) {
	int ou = unitnum;
	cdaudiostop();
	unitnum = -1;
	sys_command_close(DF_IOCTL, ou);
    }
    if (sys_command_open(DF_IOCTL, i) > 0) {
	unitnum = i;
	cd_hunt = 0;
	write_log (L"CD32: autodetected unit %d\n", unitnum);
    }
}

void AKIKO_hsync_handler (void)
{
    static int framecounter;

    if (!currprefs.cs_cd32cd)
	return;

    if (cd_hunt) {
	static int huntcnt;
	if (huntcnt <= 0) {
	    do_hunt();
	    huntcnt = 312 * 50 * 2;
	}
	huntcnt--;
    }

    framecounter--;
    if (framecounter <= 0) {
	if (cdrom_led)
	    gui_cd_led (0, 1);
	cdrom_run_read ();
	framecounter = 1000000 / (74 * 75 * cdrom_speed);
	set_status (CDSTATUS_FRAME);
	cdrom_status3++;
    }
    if (cdrom_playing) {
	static int frame2counter;
	if (cdrom_audiotimeout > 0) {
	    cdrom_audiotimeout--;
	    if (cdrom_audiotimeout == 0) {
		set_status (CDSTATUS_DATA_AVAILABLE);
		cdrom_playing = 0;
		cdrom_result_buffer[1] = 0;
		cdrom_return_data (2);
	    }
	}
	frame2counter--;
	if (frame2counter <= 0) {
	    uae_u8 *s;
	    frame2counter = 312 * 50 * 2;
	    s = sys_command_cd_qcode (DF_IOCTL, unitnum);
	    if (s) {
		uae_u8 as = s[1];
		if (as == AUDIO_STATUS_IN_PROGRESS) {
		    int lsn = msf2lsn ((s[5 + 4] << 16) | (s[6 + 4] << 8) | (s[7 + 4] << 0));
		    //write_log("%d %d (%d %d)\n", lsn, msf2lsn (last_play_end) - lsn, cdrom_leadout, msf2lsn (last_play_end));
		    // make sure audio play really ends because not all drives report position accurately
		    if ((lsn >= cdrom_leadout - 3 * 75 || lsn >= msf2lsn(last_play_end) - 3 * 75) && !cdrom_audiotimeout) {
			cdrom_audiotimeout = 3 * 312;
			//write_log("audiotimeout starts\n");
		    }
		}
	    }
	}
    }
    akiko_internal ();
    akiko_handler ();
}

static volatile int akiko_thread_running;

/* cdrom data buffering thread */
static void *akiko_thread (void *null)
{
    int i;
    uae_u8 *tmp1;
    uae_u8 *tmp2;
    int tmp3;
    uae_u8 *p;
    int offset;
    int sector;

    while(akiko_thread_running) {
	uae_sem_wait (&akiko_sem);
	sector = cdrom_current_sector;
	for (i = 0; i < SECTOR_BUFFER_SIZE; i++) {
	    if (sector_buffer_info_1[i] == 0xff)
		break;
	}
	if (cdrom_data_end > 0 && sector >= 0 &&
		(sector_buffer_sector_1 < 0 || sector < sector_buffer_sector_1 || sector >= sector_buffer_sector_1 + SECTOR_BUFFER_SIZE * 2 / 3 || i != SECTOR_BUFFER_SIZE)) {
	    memset (sector_buffer_info_2, 0, SECTOR_BUFFER_SIZE);
#if AKIKO_DEBUG_IO_CMD
	    write_log (L"filling buffer sector=%d (max=%d)\n", sector, cdrom_data_end);
#endif
	    sector_buffer_sector_2 = sector;
	    offset = 0;
	    while (offset < SECTOR_BUFFER_SIZE) {
		p = 0;
		if (sector < cdrom_data_end)
		    p = sys_command_cd_read (DF_IOCTL, unitnum, sector);
		if (p)
		    memcpy (sector_buffer_2 + offset * 2048, p, 2048);
		sector_buffer_info_2[offset] = p ? 3 : 0;
		offset++;
		sector++;
	    }
	    tmp1 = sector_buffer_info_1;
	    sector_buffer_info_1 = sector_buffer_info_2;
	    sector_buffer_info_2 = tmp1;
	    tmp2 = sector_buffer_1;
	    sector_buffer_1 = sector_buffer_2;
	    sector_buffer_2 = tmp2;
	    tmp3 = sector_buffer_sector_1;
	    sector_buffer_sector_1 = sector_buffer_sector_2;
	    sector_buffer_sector_2 = tmp3;
	}
	uae_sem_post (&akiko_sem);
	sleep_millis (10);
    }
    akiko_thread_running = -1;
    return 0;
}

STATIC_INLINE uae_u8 akiko_get_long (uae_u32 v, int offset)
{
    return v >> ((3 - offset) * 8);
}

STATIC_INLINE void akiko_put_long (uae_u32 *p, int offset, int v)
{
    *p &= ~(0xff << ((3 - offset) * 8));
    *p |= v << ((3 - offset) * 8);
}

static uae_u32 REGPARAM3 akiko_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 akiko_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 akiko_bget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 akiko_lgeti (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 akiko_wgeti (uaecptr) REGPARAM;
static void REGPARAM3 akiko_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 akiko_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 akiko_bput (uaecptr, uae_u32) REGPARAM;

static uae_u32 akiko_bget2 (uaecptr addr, int msg)
{
    uae_u8 v = 0;

    addr &= 0xffff;

    switch (addr)
    {
	/* "CAFE" = Akiko identification.
	 * Kickstart ignores Akiko C2P if this ID isn't correct */
	case 0x02:
	return 0xCA;
	case 0x03:
	return 0xFE;
	/* NVRAM */
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
	if (currprefs.cs_cd32nvram)
	    v =  akiko_nvram_read (addr - 0x30);
	return v;

	/* C2P */
	case 0x38:
	case 0x39:
	case 0x3a:
	case 0x3b:
	if (currprefs.cs_cd32c2p)
	    v = akiko_c2p_read (addr - 0x38);
	return v;
    }


    uae_sem_wait (&akiko_sem);
    switch (addr)
    {
	if (currprefs.cs_cd32cd) {
	    /* CDROM control */
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
	    v = akiko_get_long (cdrom_status1, addr - 0x04);
	    break;
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	    v = akiko_get_long (cdrom_status2, addr - 0x08);
	    break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	    v = akiko_get_long (cdrom_address1, addr - 0x10);
	    break;
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	    v = akiko_get_long (cdrom_address2, addr - 0x14);
	    break;
	case 0x18:
	    v = cdrom_status3;
	    break;
	case 0x19:
	    v = cdrom_command_offset_complete;
	    break;
	case 0x1a:
	    v = cdrom_result_complete;
	    break;
	case 0x1f:
	    v = cdrom_result_last_pos;
	    break;
	case 0x20:
	case 0x21:
	    v = akiko_get_long (cdrom_readmask_r, addr - 0x20 + 2);
	    break;
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
	    v = akiko_get_long (cdrom_longmask, addr - 0x24);
	    break;
	} else if (addr < 0x30) {
	    break;
	}

	default:
	write_log (L"akiko_bget: unknown address %08X\n", addr);
	v = 0;
	break;
    }
    akiko_internal ();
    uae_sem_post (&akiko_sem);
    if (msg && addr < 0x30 && AKIKO_DEBUG_IO)
	write_log (L"akiko_bget %08X: %08X %02X\n", M68K_GETPC, addr, v & 0xff);
    return v;
}

static uae_u32 REGPARAM2 akiko_bget (uaecptr addr)
{
#ifdef JIT
    special_mem |= S_READ;
#endif
    return akiko_bget2 (addr, 1);
}

static uae_u32 REGPARAM2 akiko_wget (uaecptr addr)
{
    uae_u16 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 0xffff;
    v = akiko_bget2 (addr + 1, 0);
    v |= akiko_bget2 (addr + 0, 0) << 8;
    if (addr < 0x30 && AKIKO_DEBUG_IO)
	write_log (L"akiko_wget %08X: %08X %04X\n", M68K_GETPC, addr, v & 0xffff);
    return v;
}

static uae_u32 REGPARAM2 akiko_lget (uaecptr addr)
{
    uae_u32 v;

#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 0xffff;
    v = akiko_bget2 (addr + 3, 0);
    v |= akiko_bget2 (addr + 2, 0) << 8;
    v |= akiko_bget2 (addr + 1, 0) << 16;
    v |= akiko_bget2 (addr + 0, 0) << 24;
    if (addr < 0x30 && (addr != 4 && addr != 8) && AKIKO_DEBUG_IO)
	write_log (L"akiko_lget %08: %08X %08X\n", M68K_GETPC, addr, v);
    return v;
}

static void write_readmask(uae_u16 v)
{
    int i, cnt;

    cnt = 0;
    for (i = 0; i < 16; i++) {
	if (v & (1 << i))
	    cnt++;
    }
    cdrom_readmask_w |= v;
    cdrom_readmask_r = 0;
}


static void akiko_bput2 (uaecptr addr, uae_u32 v, int msg)
{
    uae_u32 tmp;

    addr &= 0xffff;
    v &= 0xff;

    if(msg && addr < 0x30 && AKIKO_DEBUG_IO)
	write_log (L"akiko_bput %08X: %08X=%02X\n", M68K_GETPC, addr, v & 0xff);

    switch (addr)
    {
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
	    if (currprefs.cs_cd32nvram)
		akiko_nvram_write (addr - 0x30, v);
        return;

	case 0x38:
	case 0x39:
	case 0x3a:
	case 0x3b:
	    if (currprefs.cs_cd32c2p)
		akiko_c2p_write (addr - 0x38, v);
	return;
    }

    uae_sem_wait (&akiko_sem);
    switch (addr)
    {
	if (currprefs.cs_cd32cd) {
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
	    akiko_put_long (&cdrom_status1, addr - 0x04, v);
	    break;
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	    akiko_put_long (&cdrom_status2, addr - 0x08, v);
	    if (addr == 8)
		cdrom_status1 &= cdrom_status2;
	    break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	    akiko_put_long (&cdrom_address1, addr - 0x10, v);
	    break;
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	    akiko_put_long (&cdrom_address2, addr - 0x14, v);
	    break;
	case 0x18:
	    if (cdrom_status3 > 7)
		cdrom_status3 = 7;
	    if (cdrom_status3 > 0)
		cdrom_status3--;
	    if (cdrom_status3 == 0)
		cdrom_status1 &= ~CDSTATUS_FRAME;
	    break;
	case 0x19:
	    cdrom_command_offset_complete = v;
	    break;
	case 0x1a:
	    cdrom_result_complete = v;
	    break;
	case 0x1d:
	    cdrom_command_offset_todo = v;
	    break;
	case 0x1f:
	    cdrom_result_last_pos = v;
	    break;
	case 0x20:
	    write_readmask(v <<8);
	    break;
	case 0x21:
	    write_readmask(v);
	    break;
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
	    tmp = cdrom_longmask;
	    akiko_put_long (&cdrom_longmask, addr - 0x24, v);
	    if ((cdrom_longmask & 0x04000000) && !(tmp & 0x04000000))
		cdrom_sector_counter = 0;
	    break;
	} else if (addr < 0x30) {
	    break;
	}

	default:
	write_log (L"akiko_bput: unknown address %08X\n", addr);
	break;
    }
    akiko_internal ();
    uae_sem_post (&akiko_sem);
}

static void REGPARAM2 akiko_bput (uaecptr addr, uae_u32 v)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    akiko_bput2 (addr, v, 1);
}

static void REGPARAM2 akiko_wput (uaecptr addr, uae_u32 v)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    addr &= 0xfff;
    if((addr < 0x30 && AKIKO_DEBUG_IO))
	write_log (L"akiko_wput %08X: %08X=%04X\n", M68K_GETPC, addr, v & 0xffff);
    akiko_bput2 (addr + 1, v & 0xff, 0);
    akiko_bput2 (addr + 0, v >> 8, 0);
}

static void REGPARAM2 akiko_lput (uaecptr addr, uae_u32 v)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    addr &= 0xffff;
    if(addr < 0x30 && AKIKO_DEBUG_IO)
	write_log (L"akiko_lput %08X: %08X=%08X\n", M68K_GETPC, addr, v);
    akiko_bput2 (addr + 3, (v >> 0) & 0xff, 0);
    akiko_bput2 (addr + 2, (v >> 8) & 0xff, 0);
    akiko_bput2 (addr + 1, (v >> 16) & 0xff, 0);
    akiko_bput2 (addr + 0, (v >> 24) & 0xff, 0);
}

addrbank akiko_bank = {
    akiko_lget, akiko_wget, akiko_bget,
    akiko_lput, akiko_wput, akiko_bput,
    default_xlate, default_check, NULL, L"Akiko",
    dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void akiko_cdrom_free (void)
{
    if (unitnum >= 0)
	sys_cddev_close ();
    unitnum = -1;
    xfree (sector_buffer_1);
    xfree (sector_buffer_2);
    xfree (sector_buffer_info_1);
    xfree (sector_buffer_info_2);
    sector_buffer_1 = 0;
    sector_buffer_2 = 0;
    sector_buffer_info_1 = 0;
    sector_buffer_info_2 = 0;
    cdromok = 0;
}

void akiko_reset (void)
{
    cdaudiostop ();
    nvram_read ();
    state = I2C_WAIT;
    bitcounter = -1;
    direction = -1;

    cdrom_speed = 1;
    cdrom_current_sector = -1;
    cdrom_command_offset_complete = 0;
    cdrom_command_offset_todo = 0;
    cdrom_led = 0;
    lastmediastate = 0;

    if (akiko_thread_running > 0) {
	akiko_thread_running = 0;
	while(akiko_thread_running == 0)
	    Sleep (10);
	akiko_thread_running = 0;
    }
    akiko_cdrom_free ();
}

static uae_u8 patchdata1[]={0x0c,0x82,0x00,0x00,0x03,0xe8,0x64,0x00,0x00,0x46};
static uae_u8 patchdata2[]={0x0c,0x82,0x00,0x00,0x03,0xe8,0x4e,0x71,0x4e,0x71};

static void patchrom (void)
{
    int i;
    uae_u8 *p = (uae_u8*)extendedkickmemory;

    if (!p)
	return;
    for (i = 0; i < 524288 - sizeof (patchdata1); i++) {
	if (!memcmp (p + i, patchdata1, sizeof(patchdata1))) {
	    p[i + 6] = 0x4e;
	    p[i + 7] = 0x71;
	    p[i + 8] = 0x4e;
	    p[i + 9] = 0x71;
	    write_log (L"extended rom delay loop patched at 0x%08x\n", i + 6 + 0xe00000);
	    return;
	}
	if (!memcmp (p + i, patchdata2, sizeof(patchdata2)))
	    return;
    }
    write_log (L"couldn't patch extended rom\n");
}

void akiko_free (void)
{
    akiko_reset ();
    akiko_cdrom_free ();
}

int akiko_init (void)
{
    if (currprefs.cs_cd32cd && cdromok == 0) {
	unitnum = -1;
	if (!device_func_init (DEVICE_TYPE_ANY)) {
	    write_log (L"no CDROM support\n");
	    return 0;
	}
	if (!sys_cddev_open ()) {
	    cdromok = 1;
	    sector_buffer_1 = xmalloc (SECTOR_BUFFER_SIZE * 2048);
	    sector_buffer_2 = xmalloc (SECTOR_BUFFER_SIZE * 2048);
	    sector_buffer_info_1 = xmalloc (SECTOR_BUFFER_SIZE);
	    sector_buffer_info_2 = xmalloc (SECTOR_BUFFER_SIZE);
	    sector_buffer_sector_1 = -1;
	    sector_buffer_sector_2 = -1;
	    patchrom ();
	}
    }
    uae_sem_init (&akiko_sem, 0, 1);
    if (!savestate_state) {
	cdrom_playing = cdrom_paused = 0;
	cdrom_data_offset = -1;
    }
    if (cdromok && !akiko_thread_running) {
	akiko_thread_running = 1;
	uae_start_thread (L"akiko", akiko_thread, 0, NULL);
    }
    return 1;
}

#ifdef SAVESTATE

uae_u8 *save_akiko(int *len)
{
    uae_u8 *dstbak, *dst;
    int i;

    if (!currprefs.cs_cd32cd)
	return NULL;

    dstbak = dst = malloc (1000);
    save_u16 (0);
    save_u16 (0xCAFE);
    save_u32 (cdrom_status1);
    save_u32 (cdrom_status2);
    save_u32 (0);
    save_u32 (cdrom_address1);
    save_u32 (cdrom_address2);
    save_u8 (cdrom_status3);
    save_u8 (cdrom_command_offset_complete);
    save_u8 (cdrom_result_complete);
    save_u8 (0);
    save_u8 (0);
    save_u8 (cdrom_command_offset_todo);
    save_u8 (0);
    save_u8 (cdrom_result_last_pos);
    save_u16 ((uae_u16)cdrom_readmask_w);
    save_u16 (0);
    save_u32 (cdrom_longmask);
    save_u32 (0);
    save_u32 (0);
    save_u32 ((scl_dir ? 0x8000 : 0) | (sda_dir ? 0x4000 : 0));
    save_u32 (0);
    save_u32 (0);

    for (i = 0; i < 8; i++)
	save_u32 (akiko_buffer[i]);
    save_u8 ((uae_u8)akiko_read_offset);
    save_u8 ((uae_u8)akiko_write_offset);

    save_u32 ((cdrom_playing ? 1 : 0) | (cdrom_paused ? 2 : 0) | (cdrom_disk ? 4 : 0));
    if (cdrom_playing)
	cd_qcode (0);
    save_u32 (last_play_pos);
    save_u32 (last_play_end);
    save_u8 ((uae_u8)cdrom_toc_counter);

    save_u8 (cdrom_speed);
    save_u8 (cdrom_current_sector);

    save_u32 (cdrom_toc_crc);
    save_u8 (cdrom_toc_entries);
    save_u32 (cdrom_leadout);

    *len = dst - dstbak;
    return dstbak;
}

uae_u8 *restore_akiko(uae_u8 *src)
{
    uae_u32 v;
    int i;

    if (!currprefs.cs_cd32cd) {
	changed_prefs.cs_cd32c2p = changed_prefs.cs_cd32cd = changed_prefs.cs_cd32nvram = 1;
	currprefs.cs_cd32c2p = currprefs.cs_cd32cd = currprefs.cs_cd32nvram = 1;
	akiko_init ();
    }

    restore_u16 ();
    restore_u16 ();
    cdrom_status1 = restore_u32 ();
    cdrom_status2 = restore_u32 ();
    restore_u32 ();
    cdrom_address1 = restore_u32 ();
    cdrom_address2 = restore_u32 ();
    cdrom_status3 = restore_u8 ();
    cdrom_command_offset_complete = restore_u8 ();
    cdrom_result_complete = restore_u8 ();
    restore_u8 ();
    restore_u8 ();
    cdrom_command_offset_todo = restore_u8 ();
    restore_u8 ();
    cdrom_result_last_pos = restore_u8 ();
    cdrom_readmask_w = restore_u16 ();
    restore_u16 ();
    cdrom_longmask = restore_u32 ();
    restore_u32 ();
    restore_u32 ();
    v = restore_u32 ();
    scl_dir = (v & 0x8000) ? 1 : 0;
    sda_dir = (v & 0x4000) ? 1 : 0;
    restore_u32 ();
    restore_u32 ();

    for (i = 0; i < 8; i++)
	akiko_buffer[i] = restore_u32 ();
    akiko_read_offset = restore_u8 ();
    akiko_write_offset = restore_u8 ();

    cdrom_playing = cdrom_paused = 0;
    v = restore_u32 ();
    if (v & 1)
	cdrom_playing = 1;
    if (v & 2)
	cdrom_paused = 1;

    last_play_pos = restore_u32 ();
    last_play_end = restore_u32 ();
    cdrom_toc_counter = (uae_s8)restore_u8 ();
    cdrom_speed = restore_u8 ();
    cdrom_current_sector = (uae_s8)restore_u8 ();

    restore_u32 ();
    restore_u8 ();
    restore_u32 ();

    return src;
}

void restore_akiko_finish(void)
{
    if (!currprefs.cs_cd32cd)
	return;
    akiko_c2p_do ();
    sys_command_cd_pause (DF_IOCTL, unitnum, 0);
    sys_command_cd_stop (DF_IOCTL, unitnum);
    sys_command_cd_pause (DF_IOCTL, unitnum, 1);
    if (cdrom_playing) {
        sys_command_cd_pause (DF_IOCTL, unitnum, 0);
	sys_command_cd_play (DF_IOCTL, unitnum, last_play_pos, last_play_end, 0);
    }
}

#endif

void akiko_entergui (void)
{
    if (cdrom_playing)
	sys_command_cd_pause (DF_IOCTL, unitnum, 1);
}
void akiko_exitgui (void)
{
    if (cdrom_playing)
	sys_command_cd_pause (DF_IOCTL, unitnum, 0);
}

