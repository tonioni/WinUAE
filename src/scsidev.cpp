 /*
  * UAE - The Un*x Amiga Emulator
  *
  * a SCSI device
  *
  * Copyright 1995 Bernd Schmidt
  * Copyright 1999 Patrick Ohly
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "disk.h"
#include "autoconf.h"
#include "filesys.h"
#include "execlib.h"
#include "native2amiga.h"
#include "scsidev.h"

#include <stdio.h>

/* the new libscg should always have a scsi_close */
#ifndef SCSI_CLOSE
#define SCSI_CLOSE
#endif

typedef int BOOL;

#include "scg/scgcmd.h"
#include "scg/scsitransp.h"
#include "scg/scsireg.h"

/* our configure does not have a separate UAE_SCSIDEV_THREADS */
#if defined(UAE_FILESYS_THREADS) && !defined(SCSI_IS_NOT_THREAD_SAFE)
#define UAE_SCSIDEV_THREADS
#endif

#undef DEBUGME

/****************** generic SCSI stuff stolen from cdrecord and scsitransp.c ***********/
static int scsierr(SCSI *scgp)
{
    register struct scg_cmd *cp = scgp->scmd;

    if(cp->error != SCG_NO_ERROR ||
       cp->ux_errno != 0 || *(u_char *)&cp->scb != 0)
	return -1;
    return 0;
}

static int inquiry (SCSI *scgp, void *bp, int cnt)
{
    struct scg_cmd *scmd = scgp->scmd;

    memset(bp, cnt, '\0');
    memset((caddr_t)scmd, sizeof(*scmd), '\0');
    scmd->addr = bp;
    scmd->size = cnt;
    scmd->flags = SCG_RECV_DATA|SCG_DISRE_ENA;
    scmd->cdb_len = SC_G0_CDBLEN;
    scmd->sense_len = CCS_SENSE_LEN;
    scmd->target = scgp->target;
    scmd->cdb.g0_cdb.cmd = SC_INQUIRY;
    scmd->cdb.g0_cdb.lun = scgp->lun;
    scmd->cdb.g0_cdb.count = cnt;

    scgp->cmdname = "inquiry";

    if (scsicmd(scgp) < 0)
	return (-1);
    return (0);
}

static void print_product(struct scsi_inquiry *ip)
{
    write_log ("'%.8s' ", ip->info);
    write_log ("'%.16s' ", ip->ident);
    write_log ("'%.4s' ", ip->revision);
    if (ip->add_len < 31) {
	write_log ("NON CCS ");
    }
}

/* get integer value from env or return default value, if unset */
static int getenvint (const char *varname, int def)
{
    const char *val = getenv (varname);
    return val ? atoi (val) : def;
}

/* wrapper for the underlying combination of scsi_smalloc()/scsi_open() */
static SCSI *openscsi (int scsibus, int target, int lun)
{
    SCSI *scgp = scsi_smalloc ();
    if (!scgp) {
	return NULL;
    }

    scgp->debug = getenvint ("UAE_SCSI_DEBUG", 0);
    scgp->kdebug = getenvint ("UAE_SCSI_KDEBUG", 0);
    scgp->silent = getenvint ("UAE_SCSI_SILENT", 1);
	 scgp->verbose = getenvint ("UAE_SCSI_VERBOSE", 0);
    scgp->scsibus = scsibus;
    scgp->target = target;
    scgp->lun = lun;

    if (!scsi_open(scgp, NULL, scsibus, target, lun)) {
	scsi_sfree (scgp);
	return NULL;
    } else {
	return scgp;
    }
}

static void closescsi (SCSI *scgp)
{
    scsi_close (scgp);
    scsi_sfree (scgp);
}

/********************* start of our own code ************************/

static int opencount = 0;
static SCSI *scgp; /* SCSI handle which is to be used by the main thread */
uae_sem_t scgp_sem;


/****************** unit handling *******************/

struct scsidevdata {
    int bus, target, lun; /* the real values */
    int aunit;            /* Amiga unit number, by default calculated like that: */
#define BTL2UNIT(bus, target, lun) \
      (2 * (bus) + (target) / 8) * 100 + \
      (lun) * 10 + \
      (target % 8)
    SCSI *scgp;
    long max_dma;
    int isatapi;
#ifdef UAE_SCSIDEV_THREADS
    /* Threading stuff */
    smp_comm_pipe requests;
    uae_thread_id tid;
    int thread_running;
    uae_sem_t sync_sem;
#endif
};

#define MAX_DRIVES 16
static struct scsidevdata drives[MAX_DRIVES];
static int num_drives;
static struct scsidevdata *get_scsidev_data (int unit)
{
    int i;

    for (i = 0; i < num_drives; i++) {
	if (unit == drives[i].aunit) {
	    return &drives[i];
	}
    }
    return NULL;
}

static struct scsidevdata *add_scsidev_data (int bus, int target, int lun, int aunit)
{
    if (num_drives + 1 < MAX_DRIVES) {
	memset(&drives[num_drives], 0, sizeof(drives[num_drives]));
	drives[num_drives].bus = bus;
	drives[num_drives].target = target;
	drives[num_drives].lun = lun;
	drives[num_drives].aunit = aunit;
#if !defined(UAE_SCSIDEV_THREADS)
	drives[num_drives].scgp = scgp;
	drives[num_drives].max_dma = scsi_bufsize (scgp, 512 * 1024);
#endif
	/* check if this drive is an ATAPI drive */
	scgp->scsibus = bus;
	scgp->target = target;
	scgp->lun = lun;
	drives[num_drives].isatapi = scsi_isatapi (scgp);
	return &drives[num_drives++];
    }

    return NULL;
}

static void *scsidev_thread(void *);
static int start_thread (struct scsidevdata *sdd)
{
#ifdef UAE_SCSIDEV_THREADS
    if (sdd->thread_running)
	return 1;
    init_comm_pipe (&sdd->requests, 10, 1);
    uae_sem_init (&sdd->sync_sem, 0, 0);
    uae_start_thread (scsidev_thread, sdd, &sdd->tid);
    uae_sem_wait (&sdd->sync_sem);
    return sdd->thread_running;
#else
    return 1;
#endif
}

/************* Exec device functions ****************/


static uae_u32 scsidev_open (void)
{
    uaecptr tmp1 = m68k_areg (regs, 1); /* IOReq */
    uae_u32 unit = m68k_dreg (regs, 0);
    struct scsidevdata *sdd;

#ifdef DEBUGME
    printf("scsidev_open(0x%x, %d)\n", tmp1, unit);
#endif

    /* Check unit number */
    if ((sdd = get_scsidev_data (unit)) &&
	start_thread (sdd)) {
	opencount++;
	put_word (m68k_areg (regs, 6)+32, get_word (m68k_areg (regs, 6)+32) + 1);
	put_long (tmp1 + 24, unit); /* io_Unit */
	put_byte (tmp1 + 31, 0); /* io_Error */
	put_byte (tmp1 + 8, 7); /* ln_type = NT_REPLYMSG */
	return 0;
    }

    put_long (tmp1 + 20, (uae_u32)-1);
    put_byte (tmp1 + 31, (uae_u8)-1);
    return (uae_u32)-1;
}

static uae_u32 scsidev_close (void)
{
#ifdef DEBUGME
    printf("scsidev_close()\n");
#endif

    opencount--;
    put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) - 1);

    return 0;
}

static uae_u32 scsidev_expunge (void)
{
 #ifdef DEBUGME
    printf("scsidev_expunge()\n");
#endif
    return 0; /* Simply ignore this one... */
}

#define MODE_SELECT_6  0x15
#define MODE_SENSE_6   0x1A
#ifndef MODE_SENSE_10
#define MODE_SELECT_10 0x55
#define MODE_SENSE_10  0x5A
#endif


#ifdef DEBUG_CDR
/* please ignore this code - it can be used to debug raw CD-R writing... */

/*
** convert time in (BCD) min:sec:frame to block address
*/
typedef signed char BYTE;
typedef unsigned char UBYTE;
typedef long LONG;
typedef BYTE BCD;
typedef BYTE WORD[2];
#define BCD_DEC(x) (((x) >> 4) * 10 + ((x) & 0xF))
static LONG TestNegativeTime(LONG block)
{
    /* block -151 == 99:59:74
       -150 == 100:00:00 = 00:00:00 */
    if (block > (97 * 60 * 75))
    {
	/* must be a negative block */
	block -= 100 * 60 * 75;
    }
    return block;
}
static LONG BCDTime2Block(UBYTE min, UBYTE sec, UBYTE frame)
{
    return(TestNegativeTime((LONG)((BCD_DEC(min) * 60 + BCD_DEC(sec)) * 75 + BCD_DEC(frame) - 2 * 75)));
}
static LONG Time2Block(UBYTE min, UBYTE sec, UBYTE frame)
{
    return(TestNegativeTime((LONG)((min * 60 + sec) * 75 + frame - 2 * 75)));
}
static LONG BCDTime2Block_Pointer (UBYTE *p)
{
    return BCDTime2Block (p[0], p[1], p[2]);
}
static LONG Time2Block_Pointer (UBYTE *p)
{
    return Time2Block (p[0], p[1], p[2]);
}
#endif

static void scsidev_do_scsi (struct scsidevdata *sdd, uaecptr request)
{
    SCSI *scgp = sdd->scgp;
    struct scg_cmd *scmd = scgp->scmd;
    uaecptr acmd = get_long (request + 40);
    uaecptr scsi_data = get_long (acmd + 0);
    uae_u32 scsi_len = get_long (acmd + 4);
    uaecptr scsi_cmd = get_long (acmd + 12);
    uae_u16 scsi_cmd_len = get_word (acmd + 16);
    uae_u8 scsi_flags = get_byte (acmd + 20);
    uaecptr scsi_sense = get_long (acmd + 22);
    uae_u16 scsi_sense_len = get_word (acmd + 26);
    int sactual = 0;
    addrbank *bank_data = &get_mem_bank (scsi_data);
    addrbank *bank_cmd  = &get_mem_bank (scsi_cmd);

    /* do transfer directly to and from Amiga memory */
    if (!bank_data || !bank_data->check (scsi_data, scsi_len) ||
	!bank_cmd  || !bank_cmd->check (scsi_cmd, scsi_cmd_len)) {
	put_byte (request + 31, (uae_u8)-5); /* IOERR_BADADDRESS */
	return;
    }

#ifdef SCSI_IS_NOT_THREAD_SAFE
    uae_sem_wait (&scgp_sem);
#endif

	 scmd->timeout = 80 * 60; /* the Amiga does not tell us how long the timeout shall be, so make it _very_ long (specified in seconds) */
    scmd->addr = bank_data->xlateaddr (scsi_data);
    scmd->size = scsi_len;
    scmd->flags = ((scsi_flags & 1) ? SCG_RECV_DATA : 0) | SCG_DISRE_ENA;
    scmd->cdb_len = scsi_cmd_len;
    memcpy(&scmd->cdb, bank_cmd->xlateaddr (scsi_cmd), scsi_cmd_len);
    scmd->target = sdd->target;
    scmd->sense_len = (scsi_flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
	(scsi_flags & 2) ? scsi_sense_len : /* SCSIF_AUTOSENSE */
	-1;
    scmd->sense_count = 0;
    *(uae_u8 *)&scmd->scb = 0;

    #ifdef DEBUG_CDR
    /* please ignore this code - it can be used to debug raw CD-R writing... */
    if (!(scsi_len % 2368)) {
	/* Structure for generating bytes 2353...2368 if writing in ultra raw mode */
	typedef struct QDATAtag {
	    BYTE ControlAdr;
	    BCD Tno;
	    BCD Point;
	    BCD Min;
	    BCD Sec;
	    BCD Frame;
	    BYTE Zero;
	    BCD PMin;
	    BCD PSec;
	    BCD PFrame;
	    WORD Crc;
	    BYTE Reserved[3];
	    BYTE PChannel;
	} QDATA;

	int i = scsi_len / 2368;
	QDATA *data = (QDATA *)&((unsigned char *)scmd->addr)[2352];
	for (; i > 0; i--, data = (QDATA *)&((unsigned char *)data)[2368]) {
	    printf ("$%02x: $%02x $%02x | $%02x:$%02x:$%02x = %6ld | $%02x | $%02x:$%02x:$%02x = %6ld\n",
		    (int)data->ControlAdr, (int)*(UBYTE *)&data->Tno, (int)*(UBYTE *)&data->Point,
		    (int)*(UBYTE *)&data->Min, (int)*(UBYTE *)&data->Sec, (int)*(UBYTE *)&data->Frame,
		    BCDTime2Block_Pointer (&data->Min) + 150,
		    *(UBYTE *)&data->Zero,
		    *(UBYTE *)&data->PMin, *(UBYTE *)&data->PSec, *(UBYTE *)&data->PFrame,
		    BCDTime2Block_Pointer (&data->PMin));
	}
	fflush (stdout);
    }
    #endif

    scgp->scsibus = sdd->bus;
    scgp->target  = sdd->target;
    scgp->lun     = sdd->lun;
    scgp->cmdname = "???";
    scgp->curcmdname = "???";

    /* replace MODE_SELECT/SENSE_6 if we access a ATAPI drive,
       otherwise send it now */
    if (sdd->isatapi &&
	(scmd->cdb.g0_cdb.cmd == MODE_SELECT_6 ||
	 scmd->cdb.g0_cdb.cmd == MODE_SENSE_6)) {
	uae_u8 buffer[256 + 2], *data = scmd->addr, *tmp;
	int len = 0, page_len, i;
	int do_it = 1;
	uae_u8 sp = scmd->cdb.g0_cdb.high_addr & 1;
	uae_u8 alloc_len = scmd->cdb.g0_cdb.count;
	uae_u8 pcf_page_code = scmd->cdb.g0_cdb.mid_addr;
	uae_u8 cmd = scmd->cdb.g0_cdb.cmd;

	memset (&scmd->cdb.g1_cdb, 0, sizeof(scmd->cdb.g1_cdb));
	if (cmd == MODE_SELECT_6) {
	    /* expand parameter list */
	    tmp = data;
	    buffer[len++] = *tmp++;      /* first byte, should be 0 */
	    buffer[len++] = 0;           /* reserved */
	    buffer[len++] = *tmp++;      /* medium type */
	    buffer[len++] = 0; *tmp++;   /* ignore host application code */
	    for (i = 0; i < 4; i++) {
		buffer[len++] = 0;
	    }
	    if (*tmp) {
		/* skip block descriptor */
		tmp += 8;
	    }
	    tmp++;
	    page_len = scsi_len - (tmp - data);
	    if (page_len > 0) {
		memcpy (&buffer[len], tmp, page_len);
		len += page_len;

		scmd->cdb.g1_cdb.cmd = MODE_SELECT_10;
		scmd->cdb.g1_cdb.lun = sdd->lun;
		scmd->cdb.g1_cdb.res = 1 << 3; /* PF bit */
		scmd->cdb.g1_cdb.reladr = sp;
		scmd->cdb.g1_cdb.count[0] = len >> 8;
		scmd->cdb.g1_cdb.count[1] = len;
	    } else {
		do_it = 0;
		scmd->error = 0;
		*(uae_u8 *)&scmd->scb = 0;
		scmd->ux_errno = 0;
	    }
	} else {
	    /* MODE_SENSE_6 */
	    len = alloc_len + 2;
	    scmd->cdb.g1_cdb.cmd = MODE_SENSE_10;
	    scmd->cdb.g1_cdb.lun = sdd->lun;
	    scmd->cdb.g1_cdb.addr[0] = pcf_page_code;
	    scmd->cdb.g1_cdb.count[0] = len >> 8;
	    scmd->cdb.g1_cdb.count[1] = len;
	}
	if (do_it) {
	    scmd->cdb_len = 10;
	    scmd->addr = buffer;
	    scmd->size = len;
	    scmd->sense_count = 0;
	    *(uae_u8 *)&scmd->scb = 0;

	    scsicmd (scgp);

	    if (cmd == MODE_SENSE_6 &&
		!scmd->error &&
		!scmd->ux_errno &&
		!*(uae_u8 *)&scmd->scb) {
		int req_len = len;

		/* compress result */
		tmp = buffer;
		len = 0;
		tmp++;                 /* skip first byte of length - should better be zero */
		data[len++] = *tmp++;  /* mode data length */
		data[len++] = *tmp++;  /* medium type */
		data[len++] = 0;       /* host application type */
		data[len++] = 0;       /* block descr length */
		tmp += 4;
		if (*tmp) {
		    /* skip block descr - should not happen */
		    tmp += *tmp;
		}
		tmp++;
		memcpy (&data[len], tmp, req_len - (tmp - buffer));
	    }
	}
    } else {
	scsicmd (scgp);
    }

    put_word (acmd + 18, scmd->error == SCG_FATAL ? 0 : scsi_cmd_len); /* fake scsi_CmdActual */
    put_byte (acmd + 21, *(uae_u8 *)&scmd->scb); /* scsi_Status */
    if (*(uae_u8 *)&scmd->scb) {
	put_byte (request + 31, 45); /* HFERR_BadStatus */

	/* copy sense? */
	for (sactual = 0;
	     scsi_sense && sactual < scsi_sense_len && sactual < scmd->sense_count;
	     sactual++) {
	    put_byte (scsi_sense + sactual, scmd->u_sense.cmd_sense[sactual]);
	}
	put_long (acmd + 8, 0); /* scsi_Actual */
    } else {
	int i;

	for (i = 0; i < scsi_sense_len; i++) {
	    put_byte (scsi_sense + i, 0);
	}
	sactual = 0;

	if (scmd->error != SCG_NO_ERROR ||
	    scmd->ux_errno != 0) {
	    /* we might have been limited by the hosts DMA limits,
	       which is usually indicated by ENOMEM */
	    if (scsi_len > (unsigned int)sdd->max_dma &&
		scmd->ux_errno == ENOMEM) {
		put_byte (request + 31, (uae_u8)-4); /* IOERR_BADLENGTH */
	    } else {
		put_byte (request + 31, 20); /* io_Error, but not specified */
		put_long (acmd + 8, 0); /* scsi_Actual */
	    }
	} else {
	    put_byte (request + 31, 0);
	    put_long (acmd + 8, scsi_len - scmd->resid); /* scsi_Actual */
	}
    }
    put_word (acmd + 28, sactual);

#ifdef SCSI_IS_NOT_THREAD_SAFE
    uae_sem_post (&scgp_sem);
#endif
}

static void scsidev_do_io (struct scsidevdata *sdd, uaecptr request)
{
    uae_u32 tmp2, dataptr, offset;

    tmp2 = get_word (request+28); /* io_Command */
    switch (tmp2) {
     case 28:
	/* HD_SCSICMD */
	scsidev_do_scsi (sdd, request);
	break;
     default:
	/* Command not understood. */
	put_byte (request+31, (uae_u8)-3); /* io_Error */
	break;
    }
#ifdef DEBUGME
    printf ("scsidev: did io: sdd     = 0x%x\n", sdd);
    printf ("scsidev: did io: request = %08lx\n", (unsigned long)request);
    printf ("scsidev: did io: error   = %d\n", (int)get_word (request+31));
#endif
}


static uae_u32 scsidev_beginio (void)
{
    uae_u32 request = m68k_areg (regs, 1);
    int unit = get_long (request + 24);
    struct scsidevdata *sdd = get_scsidev_data (unit);

#ifdef DEBUGME
    printf ("scsidev_begin_io: sdd     = 0x%x\n", sdd);
    printf ("scsidev_begin_io: request = %08lx\n", (unsigned long)request);
    printf ("scsidev_begin_io: cmd     = %d\n", (int)get_word (request+28));
#endif

    put_byte (request+8, NT_MESSAGE);
    put_byte (request+31, 0); /* no error yet */

#ifdef UAE_SCSIDEV_THREADS
    {
	uae_pt data;

	/* clear IOF_QUICK */
	put_byte (request+30, get_byte (request+30) & ~1);
	/* forward to unit thread */
	write_comm_pipe_u32 (&sdd->requests, request, 1);
	return 0;
    }
#else
    put_byte (request+30, get_byte (request+30) & ~1);
    scsidev_do_io (sdd, request);
    return get_byte (request+31); /* do we really have to return io_Error? */
#endif
}

#ifdef UAE_SCSIDEV_THREADS
static void *scsidev_thread (void *sddv)
{
    struct scsidevdata *sdd = sddv;

#ifdef DEBUGME
    printf ("scsidev_penguin: sdd  = 0x%x ready\n", sdd);
#endif
    /* init SCSI */
    if (!(sdd->scgp = openscsi (sdd->bus, sdd->target, sdd->lun)) ||
	(sdd->max_dma = scsi_bufsize (sdd->scgp, 512 * 1024)) <= 0) {
	sdd->thread_running = 0;
	uae_sem_post (&sdd->sync_sem);
	return 0;
    }
    sdd->thread_running = 1;
    uae_sem_post (&sdd->sync_sem);

    for (;;) {
	uaecptr request;

	request = (uaecptr)read_comm_pipe_u32_blocking (&sdd->requests);
#ifdef DEBUGME
	printf ("scsidev_penguin: sdd  = 0x%x\n", sdd);
	printf ("scsidev_penguin: req  = %08lx\n", (unsigned long)request);
	printf ("scsidev_penguin: cmd  = %d\n", (int)get_word (request+28));
#endif
	if (!request) {
	    printf ("scsidev_penguin: going down with 0x%x\n", sdd->sync_sem);
	    /* Death message received. */
	    sdd->thread_running = 0;
	    uae_sem_post (&sdd->sync_sem);
	    /* Die.  */
	    return 0;
	}

	scsidev_do_io (sdd, request);
	uae_ReplyMsg (request);
    }
    return 0;
}
#endif


static uae_u32 scsidev_abortio (void)
{
    return (uae_u32)-3;
}

static uae_u32 scsidev_init (void)
{
#ifdef DEBUGME
    printf("scsidev_init()\n");
#endif

    if (scgp) {
	/* we still have everything in place */
	return m68k_dreg (regs, 0); /* device base */
    }

    /* init global SCSI */
    if (!(scgp = openscsi (-1, -1, -1))) {
	return 0;
    }

    uae_sem_init (&scgp_sem, 0, 1);

    /* add all units we find */
    for (scgp->scsibus=0; scgp->scsibus < 8; scgp->scsibus++) {
	if (!scsi_havebus(scgp, scgp->scsibus))
	    continue;
	printf("scsibus%d:\n", scgp->scsibus);
	for (scgp->target=0; scgp->target < 16; scgp->target++) {
	    struct scsi_inquiry inq;
	    scgp->lun = 0;
	    if (inquiry (scgp, &inq, sizeof(inq))) {
		continue;
	    }
	    for (scgp->lun=0; scgp->lun < 8; scgp->lun++) {
		if (!inquiry (scgp, &inq, sizeof(inq))) {
		    int aunit = BTL2UNIT(scgp->scsibus, scgp->target, scgp->lun);
		    struct scsidevdata *sdd;

		    write_log ("   %2.01d,%d (= %3.d): ", scgp->target, scgp->lun, aunit);
		    print_product (&inq);
		    sdd = add_scsidev_data (scgp->scsibus, scgp->target, scgp->lun, aunit);
		    write_log (!sdd ? " - init failed ???" : sdd->isatapi ? " - ATAPI" : " - SCSI");
		    write_log ("\n");
		}
	    }
	}
    }
    return m68k_dreg (regs, 0); /* device base */
}

static uaecptr ROM_scsidev_resname = 0,
    ROM_scsidev_resid = 0,
    ROM_scsidev_init = 0;

uaecptr scsidev_startup (uaecptr resaddr)
{
#ifdef DEBUGME
    printf("scsidev_startup(0x%x)\n", resaddr);
#endif
    /* Build a struct Resident. This will set up and initialize
     * the uaescsi.device */
    put_word (resaddr + 0x0, 0x4AFC);
    put_long (resaddr + 0x2, resaddr);
    put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
    put_word (resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
    put_word (resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
    put_long (resaddr + 0xE, ROM_scsidev_resname);
    put_long (resaddr + 0x12, ROM_scsidev_resid);
    put_long (resaddr + 0x16, ROM_scsidev_init); /* calls scsidev_init */
    resaddr += 0x1A;

    return resaddr;
}

void scsidev_install (void)
{
    uae_u32 functable, datatable;
    uae_u32 initcode, openfunc, closefunc, expungefunc;
    uae_u32 beginiofunc, abortiofunc;

#ifdef DEBUGME
    printf("scsidev_install(): 0x%x\n", here ());
#endif

    ROM_scsidev_resname = ds ("uaescsi.device");
    ROM_scsidev_resid = ds ("UAE scsi.device 0.1");

    /* initcode */
    initcode = here ();
    calltrap (deftrap (scsidev_init)); dw (RTS);

    /* Open */
    openfunc = here ();
    calltrap (deftrap (scsidev_open)); dw (RTS);

    /* Close */
    closefunc = here ();
    calltrap (deftrap (scsidev_close)); dw (RTS);

    /* Expunge */
    expungefunc = here ();
    calltrap (deftrap (scsidev_expunge)); dw (RTS);

    /* BeginIO */
    beginiofunc = here ();
    calltrap (deftrap (scsidev_beginio));
#ifndef UAE_SCSIDEV_THREADS
    /* don't reply when using threads - native2amiga's Reply() does that */
    dw (0x48E7); dw (0x8002); /* movem.l d0/a6,-(a7) */
    dw (0x0829); dw (0); dw (30); /* btst #0,30(a1) */
    dw (0x6608); /* bne.b +8 */
    dw (0x2C78); dw (0x0004); /* move.l 4,a6 */
    dw (0x4EAE); dw (-378); /* jsr ReplyMsg(a6) */
    dw (0x4CDF); dw (0x4001); /* movem.l (a7)+,d0/a6 */
#endif
    dw (RTS);

    /* AbortIO */
    abortiofunc = here ();
    calltrap (deftrap (scsidev_abortio)); dw (RTS);

    /* FuncTable */
    functable = here ();
    dl (openfunc); /* Open */
    dl (closefunc); /* Close */
    dl (expungefunc); /* Expunge */
    dl (EXPANSION_nullfunc); /* Null */
    dl (beginiofunc); /* BeginIO */
    dl (abortiofunc); /* AbortIO */
    dl (0xFFFFFFFFul); /* end of table */

    /* DataTable */
    datatable = here ();
    dw (0xE000); /* INITBYTE */
    dw (0x0008); /* LN_TYPE */
    dw (0x0300); /* NT_DEVICE */
    dw (0xC000); /* INITLONG */
    dw (0x000A); /* LN_NAME */
    dl (ROM_scsidev_resname);
    dw (0xE000); /* INITBYTE */
    dw (0x000E); /* LIB_FLAGS */
    dw (0x0600); /* LIBF_SUMUSED | LIBF_CHANGED */
    dw (0xD000); /* INITWORD */
    dw (0x0014); /* LIB_VERSION */
    dw (0x0004); /* 0.4 */
    dw (0xD000); /* INITWORD */
    dw (0x0016); /* LIB_REVISION */
    dw (0x0000); /* end of table already ??? */
    dw (0xC000); /* INITLONG */
    dw (0x0018); /* LIB_IDSTRING */
    dl (ROM_scsidev_resid);
    dw (0x0000); /* end of table */

    ROM_scsidev_init = here ();
    dl (0x00000100); /* size of device base */
    dl (functable);
    dl (datatable);
    dl (initcode);
}

void scsidev_reset (void)
{
#ifdef DEBUGME
    printf("scsidev_reset()\n");
#endif

#ifdef SCSI_CLOSE
#ifdef UAE_SCSIDEV_THREADS
    {
	int i;

	for (i = 0; i < num_drives; i++) {
	    if (!drives[i].thread_running) {
		continue;
	    }
	    write_comm_pipe_int (&drives[i].requests, 0, 1);
	    uae_sem_wait (&drives[i].sync_sem);
	}
	num_drives = 0;
    }
#endif

    if (scgp) {
	closescsi (scgp);
	scgp = NULL;
    }
#endif

    opencount = 0;
}

void scsidev_start_threads (void)
{
#ifdef DEBUGME
    printf("scsidev_start_threads()\n");
#endif
}

