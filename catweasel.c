
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef CATWEASEL

#include "config.h"
#include "options.h"
#include "memory.h"
#include "ioport.h"
#include "catweasel.h"
#include "uae.h"

struct catweasel_contr cwc;

static int cwhsync;
static int handshake;

void catweasel_hsync (void)
{
    if (cwhsync <= 0)
	return;
    cwhsync--;
    if (cwhsync == 0) {
        if (handshake)
	    ioport_write (currprefs.catweasel_io + 0xd0, 0);
	handshake = 0;
    }
}

int catweasel_read_joystick (uae_u8 *dir, uae_u8 *buttons)
{
    if (cwc.type != CATWEASEL_TYPE_MK3)
	return 0;
    *dir = ioport_read (currprefs.catweasel_io + 0xc0);
    *buttons = ioport_read (currprefs.catweasel_io + 0xc8);
    return 1;
}

int catweasel_read_keyboard (uae_u8 *keycode)
{
    uae_u8 v;
    if (cwc.type != CATWEASEL_TYPE_MK3)
	return 0;
    v = ioport_read (currprefs.catweasel_io + 0xd4);
    if (!(v & 0x80))
	return 0;
    if (handshake)
	return 0;
    *keycode = ioport_read (currprefs.catweasel_io + 0xd0);
    ioport_write (currprefs.catweasel_io + 0xd0, 0);
    handshake = 1;
    cwhsync = 10;
    return 1;
}

uae_u32 catweasel_do_bget (uaecptr addr)
{
    if (cwc.type == CATWEASEL_TYPE_MK3) {
	if ((currprefs.catweasel_io & 3) == 0 && addr >= 0xc0 && addr <= 0xfc)
	    return ioport_read (currprefs.catweasel_io + addr);
    } else {
	if (addr >= currprefs.catweasel_io && addr <= currprefs.catweasel_io + 8) {
	    return ioport_read (addr & 0x3ff);
	} else if(addr >= 0x10000 + currprefs.catweasel_io && addr <= 0x10000 + currprefs.catweasel_io) {
	    return ioport_read (addr & 0x3ff);
	} else if ((addr & 0x3ff) < 0x200 || (addr & 0x3ff) >= 0x400) {
	    write_log("catweasel_bget @%08.8X!\n",addr);
	}
    }
    return 0;
}

void catweasel_do_bput (uaecptr addr, uae_u32 b)
{
    if (cwc.type == CATWEASEL_TYPE_MK3) {
	if ((currprefs.catweasel_io & 3) == 0 && addr >= 0xc0 && addr <= 0xfc)
	    ioport_write (currprefs.catweasel_io + addr, b);
    } else {
	if (addr >= currprefs.catweasel_io && addr <= currprefs.catweasel_io + 8) {
	    ioport_write (addr & 0x3ff, b);
	} else if(addr >= 0x10000 + currprefs.catweasel_io && addr <= 0x10000 + currprefs.catweasel_io) {
	    ioport_write (addr & 0x3ff, b);
	} else if ((addr & 0x3ff) < 0x200 || (addr & 0x3ff) >= 0x400) {
	    write_log("catweasel_bput @%08.8X=%02.2X!\n",addr,b);
	}
    }
}

int catweasel_init (void)
{
    if (!currprefs.catweasel_io)
	return 0;
    if (!ioport_init ())
	return 0;
    cwc.type = currprefs.catweasel_io >= 0x400 ? CATWEASEL_TYPE_MK3 : CATWEASEL_TYPE_MK1;
    cwc.iobase = currprefs.catweasel_io;
    catweasel_init_controller (&cwc);
    return 1;
}

void catweasel_free (void)
{
    if (!currprefs.catweasel_io)
	return;
    ioport_free ();
}

#define outb(v,port) ioport_write(port,v)
#define inb(port) ioport_read(port)

#define LONGEST_TRACK 16000

static uae_u8 mfmbuf[LONGEST_TRACK * 4];
static uae_u8 tmpmfmbuffer[LONGEST_TRACK * 2];

static int bitshiftcompare(uae_u8 *src,int bit,int len,uae_u8 *comp)
{
	uae_u8 b;
	int ones,zeros,len2;

	ones=zeros=0;
	len2=len;
	while(len--) {
		b = (comp[0] << bit) | (comp[1] >> (8 - bit));
		if(b != *src) return 1;
		if(b==0x00) zeros++;
		if(b==0xff) ones++;
		src++;
		comp++;
	}
	if(ones==len2||zeros==len2) return 1;
	return 0;
}

static uae_u8 *mergepieces(uae_u8 *start,int len,int bits,uae_u8 *sync)
{
	uae_u8 *dst=tmpmfmbuffer;
	uae_u8 b;
	int size;
	int shift;
	
	size=len-(sync-start);
	memcpy(dst,sync,size);
	dst+=size;
	b=start[len];
	b&=~(255>>bits);
	b|=start[0]>>bits;
	*dst++=b;
	shift=8-bits;
	while(start<=sync+2000) {
		*dst++=(start[0]<<shift)|(start[1]>>(8-shift));
		start++;
	}
	return tmpmfmbuffer;
}	
		
#define SCANOFFSET 1 /* scanning range in bytes, -SCANOFFSET to SCANOFFSET */
#define SCANOFFSET2 20
#define SCANLENGHT 200 /* scanning length in bytes */

static uae_u8* scantrack(uae_u8 *sync1,uae_u8 *sync2,int *trackbytes,int *trackbits)
{
	int i,bits,bytes,matched;
	uae_u8 *sync2bak=sync2;
	
	sync1+=SCANOFFSET2;
	sync2+=SCANOFFSET2;
	while(sync1 < sync2bak - 2*SCANOFFSET - SCANOFFSET2 - SCANLENGHT) {
		matched=0x7fff;
		for(i=0;i<2*SCANOFFSET*8;i++) {
			bits=i&7;
			bytes=-SCANOFFSET+(i>>3);
			if(!bitshiftcompare(sync1,bits,SCANLENGHT,sync2+bytes)) {
				if(matched==0x7fff) {
					matched=i;
				} else {
					break;
				}
			}
		}
		if(matched!=0x7fff && i>=2*SCANOFFSET*8) {
			bits=matched&7;
			bytes=-SCANOFFSET+(matched>>3);
			*trackbytes=sync2+bytes-sync1;
			*trackbits=bits;
			return mergepieces(sync1,*trackbytes,*trackbits,sync2bak);
		}
		sync1++;
		sync2++;
	}
	return 0;
}	

static unsigned char threshtab[128];

static void codec_makethresh(int trycnt, const unsigned char *origt, unsigned char *t, int numthresh)
{
    static unsigned char tab[10] = { 0, 0, 0, 0, -1, -2, 1, 2, -1, 1 };

    if (trycnt >= sizeof (tab))
	trycnt = sizeof (tab) - 1;
    while(numthresh--)
	t[numthresh] = origt[numthresh] + tab[trycnt];
}

static void codec_init_threshtab(int trycnt, const unsigned char *origt)
{
    static unsigned char old_thresholds[2] = { 0, 0 };
    unsigned char t[2];
    int a, i;

    codec_makethresh(trycnt, origt, t, 2);

    if(*(unsigned short*)t == *(unsigned short*)old_thresholds)
	return;

    for(i=0,a=2; i<128; i++) {
	if(i == t[0] || i == t[1])
	    a++;
	threshtab[i] = a;
    }

    *(unsigned short*)&old_thresholds = *(unsigned short*)t;
}

static __inline__ void CWSetCReg(catweasel_contr *c, unsigned char clear, unsigned char set)
{
    c->control_register = (c->control_register & ~clear) | set;
    outb(c->control_register, c->io_sr);
}

static void CWTriggerStep(catweasel_contr *c)
{
    CWSetCReg(c, c->crm_step, 0);
    CWSetCReg(c, 0, c->crm_step);
}

void catweasel_init_controller(catweasel_contr *c)
{
    int i, j;

    if(!c->iobase)
	return;

    switch(c->type) {
    case CATWEASEL_TYPE_MK1:
	c->crm_sel0 = 1 << 5;
	c->crm_sel1 = 1 << 4;
	c->crm_mot0 = 1 << 3;
	c->crm_mot1 = 1 << 7;
	c->crm_dir  = 1 << 1;
	c->crm_step = 1 << 0;
	c->srm_trk0 = 1 << 4;
	c->srm_dchg = 1 << 5;
	c->srm_writ = 1 << 1;
	c->io_sr    = c->iobase + 2;
	c->io_mem   = c->iobase;
	break;
    case CATWEASEL_TYPE_MK3:
	c->crm_sel0 = 1 << 2;
	c->crm_sel1 = 1 << 3;
	c->crm_mot0 = 1 << 1;
	c->crm_mot1 = 1 << 5;
	c->crm_dir  = 1 << 4;
	c->crm_step = 1 << 7;
	c->srm_trk0 = 1 << 2;
	c->srm_dchg = 1 << 5;
	c->srm_writ = 1 << 6;
	c->srm_dskready = 1 << 4;
	c->io_sr    = c->iobase + 0xe8;
	c->io_mem   = c->iobase + 0xe0;
	break;
    default:
	return;	
    }

    c->control_register = 255;

    /* select all drives, step inside */
    CWSetCReg(c, c->crm_dir | c->crm_sel0 | c->crm_sel1, 0);
    for(i=0;i<2;i++) {
	c->drives[i].number = i;
	c->drives[i].contr = c;
	c->drives[i].diskindrive = 0;
	
	/* select only the respective drive, step to track 0 */
	if(i == 0) {
	    CWSetCReg(c, c->crm_sel0, c->crm_dir | c->crm_sel1);
	} else {
	    CWSetCReg(c, c->crm_sel1, c->crm_dir | c->crm_sel0);
	}

	for(j = 0; j < 86 && (inb(c->io_sr) & c->srm_trk0); j++) {
	    CWTriggerStep(c);
	    sleep_millis(6);
	}
	
	if(j < 86) {
	    c->drives[i].type = 1;
	    c->drives[i].track = 0;
	} else {
	    c->drives[i].type = 0;
	}
    }
    c->drives[0].sel = c->crm_sel0;
    c->drives[0].mot = c->crm_mot0;
    c->drives[1].sel = c->crm_sel1;
    c->drives[1].mot = c->crm_mot1;
    CWSetCReg(c, 0, c->crm_sel0 | c->crm_sel1); /* deselect all drives */
}

void catweasel_free_controller(catweasel_contr *c)
{
    if(!c->iobase)
	return;

    /* all motors off, deselect all drives */
    CWSetCReg(c, 0, c->crm_mot0 | c->crm_mot1 | c->crm_sel0 | c->crm_sel1);
}

void catweasel_set_motor(catweasel_drive *d, int on)
{
    CWSetCReg(d->contr, d->sel, 0);
    if (on)
	CWSetCReg(d->contr, d->mot, 0);
    else
	CWSetCReg(d->contr, 0, d->mot);
    CWSetCReg(d->contr, 0, d->sel);
}

int catweasel_step(catweasel_drive *d, int dir)
{
    catweasel_contr *c = d->contr;
    CWSetCReg(c, d->sel, 0);
    if (dir > 0)
	CWSetCReg(c, c->crm_dir, 0);
    else
	CWSetCReg(c, 0, c->crm_dir);
    CWTriggerStep (c);
    CWSetCReg(c, 0, d->sel);
    d->track += dir > 0 ? 1 : -1;
    return 1;
}

int catweasel_disk_changed(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = (inb(d->contr->io_sr) & d->contr->srm_dchg) ? 0 : 1;
    CWSetCReg(d->contr, 0, d->sel);
    return ret;
}

int catweasel_diskready(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = (inb(d->contr->io_sr) & d->contr->srm_dskready) ? 0 : 1;
    CWSetCReg(d->contr, 0, d->sel);
    return ret;
}

int catweasel_track0(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = (inb(d->contr->io_sr) & d->contr->srm_trk0) ? 0 : 1;
    CWSetCReg(d->contr, 0, d->sel);
    if (ret)
	d->track = 0;
    return ret;
}

int catweasel_write_protected(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = !(inb(d->contr->io_sr) & 8);
    CWSetCReg(d->contr, 0, d->sel);
    return ret;
}

uae_u8 catweasel_read_byte(catweasel_drive *d)
{
    return inb(d->contr->io_mem);
}

static const unsigned char amiga_thresholds[] = { 0x22, 0x30 }; // 27, 38 for 5.25"

#define FLOPPY_WRITE_LEN 6250

#define MFMMASK 0x55555555
static uae_u32 getmfmlong (uae_u16 * mbuf)
{
	return (uae_u32)(((*mbuf << 16) | *(mbuf + 1)) & MFMMASK);
}

static int drive_write_adf_amigados (uae_u16 *mbuf, uae_u16 *mend, uae_u8 *writebuffer, int track)
{
	int i, secwritten = 0;
	uae_u32 odd, even, chksum, id, dlong;
	uae_u8 *secdata;
	uae_u8 secbuf[544];
	char sectable[22];
	int num_sectors = 11;
	int ec = 0;

	memset (sectable, 0, sizeof (sectable));
	mend -= (4 + 16 + 8 + 512);
	while (secwritten < num_sectors) {
		int trackoffs;

		do {
			while (*mbuf++ != 0x4489) {
			    if (mbuf >= mend) {
				ec = 1;
				goto err;
			    }
			}
		} while (*mbuf++ != 0x4489);

		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		id = (odd << 1) | even;

		trackoffs = (id & 0xff00) >> 8;
		if (trackoffs > 10) {
		    ec = 2;
		    goto err;
		}
		chksum = odd ^ even;
		for (i = 0; i < 4; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 8);
			mbuf += 2;

			dlong = (odd << 1) | even;
			if (dlong) {
			    ec = 6;
			    goto err;
			}
			chksum ^= odd ^ even;
		} /* could check here if the label is nonstandard */
		mbuf += 8;
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		if (((odd << 1) | even) != chksum) {
		    ec = 3;
		    goto err;
		}
		odd = (id & 0x00ff0000) >> 16;
		if (odd != track) {
		    ec = 7;
		    goto err;
		}
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		chksum = (odd << 1) | even;
		secdata = secbuf + 32;
		for (i = 0; i < 128; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 256);
			mbuf += 2;
			dlong = (odd << 1) | even;
			*secdata++ = dlong >> 24;
			*secdata++ = dlong >> 16;
			*secdata++ = dlong >> 8;
			*secdata++ = dlong;
			chksum ^= odd ^ even;
		}
		mbuf += 256;
		if (chksum) {
		    ec = 4;
		    goto err;
		}
		sectable[trackoffs] = 1;
		secwritten++;
		memcpy (writebuffer + trackoffs * 512, secbuf + 32, 512);
	}
	if (secwritten == 0 || secwritten < 0) {
	    ec = 5;
	    goto err;
	}
	return 0;
err:
	write_log ("mfm decode error %d. secwritten=%d\n", ec, secwritten);
	for (i = 0; i < num_sectors; i++)
	    write_log ("%d:%d ", i, sectable[i]);
	write_log ("\n");
	return ec;
}

static void mfmcode (uae_u16 * mfm, int words)
{
    uae_u32 lastword = 0;

    while (words--) {
	uae_u32 v = *mfm;
	uae_u32 lv = (lastword << 16) | v;
	uae_u32 nlv = 0x55555555 & ~lv;
	uae_u32 mfmbits = (nlv << 1) & (nlv >> 1);

	*mfm++ = v | mfmbits;
	lastword = v;
    }
}

#define FLOPPY_GAP_LEN 360

static int amigados_mfmcode (uae_u8 *src, uae_u16 *dst, int num_secs, int track)
{
	int sec;
	memset (dst, 0xaa, FLOPPY_GAP_LEN * 2);

	for (sec = 0; sec < num_secs; sec++) {
	    uae_u8 secbuf[544];
	    int i;
	    uae_u16 *mfmbuf = dst + 544 * sec + FLOPPY_GAP_LEN;
	    uae_u32 deven, dodd;
	    uae_u32 hck = 0, dck = 0;

	    secbuf[0] = secbuf[1] = 0x00;
	    secbuf[2] = secbuf[3] = 0xa1;
	    secbuf[4] = 0xff;
	    secbuf[5] = track;
	    secbuf[6] = sec;
	    secbuf[7] = num_secs - sec;

	    for (i = 8; i < 24; i++)
		secbuf[i] = 0;

	    mfmbuf[0] = mfmbuf[1] = 0xaaaa;
	    mfmbuf[2] = mfmbuf[3] = 0x4489;

	    memcpy (secbuf + 32, src + sec * 512, 512);
	    deven = ((secbuf[4] << 24) | (secbuf[5] << 16)
		     | (secbuf[6] << 8) | (secbuf[7]));
	    dodd = deven >> 1;
	    deven &= 0x55555555;
	    dodd &= 0x55555555;

	    mfmbuf[4] = dodd >> 16;
	    mfmbuf[5] = dodd;
	    mfmbuf[6] = deven >> 16;
	    mfmbuf[7] = deven;

	    for (i = 8; i < 48; i++)
		mfmbuf[i] = 0xaaaa;
	    for (i = 0; i < 512; i += 4) {
		deven = ((secbuf[i + 32] << 24) | (secbuf[i + 33] << 16)
			 | (secbuf[i + 34] << 8) | (secbuf[i + 35]));
		dodd = deven >> 1;
		deven &= 0x55555555;
		dodd &= 0x55555555;
		mfmbuf[(i >> 1) + 32] = dodd >> 16;
		mfmbuf[(i >> 1) + 33] = dodd;
		mfmbuf[(i >> 1) + 256 + 32] = deven >> 16;
		mfmbuf[(i >> 1) + 256 + 33] = deven;
	    }

	    for (i = 4; i < 24; i += 2)
		hck ^= (mfmbuf[i] << 16) | mfmbuf[i + 1];

	    deven = dodd = hck;
	    dodd >>= 1;
	    mfmbuf[24] = dodd >> 16;
	    mfmbuf[25] = dodd;
	    mfmbuf[26] = deven >> 16;
	    mfmbuf[27] = deven;

	    for (i = 32; i < 544; i += 2)
		dck ^= (mfmbuf[i] << 16) | mfmbuf[i + 1];

	    deven = dodd = dck;
	    dodd >>= 1;
	    mfmbuf[28] = dodd >> 16;
	    mfmbuf[29] = dodd;
	    mfmbuf[30] = deven >> 16;
	    mfmbuf[31] = deven;
	    mfmcode (mfmbuf + 4, 544 - 4);

	}
    return (num_secs * 544 + FLOPPY_GAP_LEN) * 2 * 8;
}

static uae_u16 amigamfmbuffer[LONGEST_TRACK];
static uae_u8 amigabuffer[512*22];

/* search and align to 0x4489 WORDSYNC markers */
static int isamigatrack(uae_u8 *mfmdata, uae_u8 *mfmdatae, uae_u16 *mfmdst, int track)
{
	uae_u16 *dst = amigamfmbuffer;
	int len;
	int shift, syncshift, sync,ret;
	uae_u32 l;
	uae_u16 w;

	sync = syncshift = shift = 0;
	len = (mfmdatae - mfmdata) * 8;
	if (len > LONGEST_TRACK * 8)
	    len = LONGEST_TRACK * 8;
	while (len--) {
		l = (mfmdata[0] << 16) | (mfmdata[1] << 8) | (mfmdata[2] << 0);
		w = l >> (8 - shift);
		if (w == 0x4489) {
			sync = 1;
			syncshift = 0;
		}
		if (sync) {
			if (syncshift == 0) *dst++ = w;
			syncshift ++;
			if (syncshift == 16) syncshift = 0;
		}
		shift++;
		if (shift == 8) {
			mfmdata++;
			shift = 0;
		}
	}
	if (sync) {
		ret=drive_write_adf_amigados (amigamfmbuffer, dst, amigabuffer, track);
		if(!ret)
		    return amigados_mfmcode (amigabuffer, mfmdst, 11, track);
		write_log ("decode error %d\n", ret);
	} else {
	    write_log ("decode error: no sync found\n");
	}
	return 0;
}



int catweasel_fillmfm (catweasel_drive *d, uae_u16 *mfm, int side, int clock, int rawmode)
{
    int i, j, oldsync, syncs[10], synccnt, endcnt;
    uae_u32 tt1 = 0, tt2 = 0;
    uae_u8 *p1;
    int bytes = 0, bits = 0;
    static int lasttrack, trycnt;

    if (cwc.type == 0)
	return 0;
    if (d->contr->control_register & d->mot)
	return 0;
    if (!catweasel_read (d, side, 1, rawmode))
	return 0;
    if(d->contr->type == CATWEASEL_TYPE_MK1) {
	inb(d->contr->iobase + 1);
	inb(d->contr->io_mem); /* ignore first byte */
    } else {
	outb(0, d->contr->iobase + 0xe4);
    }
    catweasel_read_byte (d);
    if (lasttrack == d->track)
	trycnt++;
    else
	trycnt = 0;
    lasttrack = d->track;
    codec_init_threshtab(trycnt, amiga_thresholds);
    i = 0; j = 0;
    synccnt = 0;
    oldsync = -1;
    endcnt = 0;
    while (j < LONGEST_TRACK * 4) {
	uae_u8 b = catweasel_read_byte (d);
	if (b >= 250) {
	    if (b == 255 - endcnt) {
		endcnt++;
		if (endcnt == 5)
		    break;
	    } else
		endcnt = 0;
	}
	if (rawmode) {
	    if (b & 0x80) {
		if (oldsync < j) {
		    syncs[synccnt++] = j;
		    oldsync = j + 300;
		}
	    }
	    if (synccnt >= 3 && j > oldsync)
		break;
	}
	b = threshtab[b & 0x7f];
	tt1 = (tt1 << b) + 1;
	tt2 += b;

	if (tt2 >= 16) {
	    tt2 -= 16;
	    mfmbuf[j++] = tt1 >> (tt2 + 8);
	    mfmbuf[j++] = tt1 >> tt2;
	}
	i++;
    }
    write_log ("cyl=%d, side=%d, length %d, syncs %d\n", d->track, side, j, synccnt);
    if (rawmode) {
	if (synccnt >= 3) {
	    p1 = scantrack (mfmbuf + syncs[1], mfmbuf + syncs[2], &bytes, &bits);
	    if (p1) {
		j = 0;
		for (i = 0; i < bytes + 2; i+=2) {
		    mfm[j++] = (p1[i] << 8) | p1[i + 1];
		}
	        return bytes * 8 + bits;
	    }
	}
    } else {
	return isamigatrack (mfmbuf, mfmbuf + j, mfm, d->track * 2 + side);
    }
    return 0;
}
	
int catweasel_read(catweasel_drive *d, int side, int clock, int rawmode)
{
    int iobase = d->contr->iobase;

    CWSetCReg(d->contr, d->sel, 0);
    if(d->contr->type == CATWEASEL_TYPE_MK1) {
	CWSetCReg(d->contr, 1<<2, (!side)<<2); /* set disk side */

	inb(iobase+1); /* ra reset */
	outb(clock*128, iobase+3);

	inb(iobase+1);
	inb(iobase+0);
//	inb(iobase+0);
//	outb(0, iobase+3); /* don't store index pulse */

	inb(iobase+1);

	inb(iobase+7); /* start reading */
	sleep_millis(rawmode ? 550 : 225);
	outb(0, iobase+1); /* stop reading, don't reset RAM pointer */

	outb(128, iobase+0); /* add data end mark */
	outb(128, iobase+0);

	inb(iobase+1); /* Reset RAM pointer */
    } else {
	CWSetCReg(d->contr, 1<<6, (!side)<<6); /* set disk side */

	outb(0, iobase + 0xe4); /* Reset memory pointer */
	switch(clock) {
	case 0: /* 28MHz */
	    outb(128, iobase + 0xec);
	    break;
	case 1: /* 14MHz */
	    outb(0, iobase + 0xec);
	    break;
	}
	inb(iobase + 0xe0);
	inb(iobase + 0xe0);
	outb(0, iobase + 0xec); /* no IRQs, no MFM predecode */
	inb(iobase + 0xe0);
	outb(0, iobase + 0xec); /* don't store index pulse */

	outb(0, iobase + 0xe4); /* Reset memory pointer */
	inb(iobase + 0xf0); /* start reading */
	sleep_millis(rawmode ? 550 : 225);
	inb(iobase + 0xe4); /* stop reading, don't reset RAM pointer */

	outb(255, iobase + 0xe0); /* add data end mark */
	outb(254, iobase + 0xe0); /* add data end mark */
	outb(253, iobase + 0xe0); /* add data end mark */
	outb(252, iobase + 0xe0); /* add data end mark */
	outb(251, iobase + 0xe0); /* add data end mark */
	outb(0, iobase + 0xe4); /* Reset memory pointer */
    }
    CWSetCReg(d->contr, 0, d->sel);
    return 1;
}

#endif