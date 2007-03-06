 /*
  * UAE - The Un*x Amiga Emulator
  *
  *  Serial Line Emulation
  *
  * (c) 1996, 1997 Stefan Reinauer <stepan@linux.de>
  * (c) 1997 Christian Schmitt <schmitt@freiburg.linux.de>
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"
 
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "cia.h"
#include "serial.h"

#include "od-win32/parser.h"

#define SERIALDEBUG 0 /* 0, 1, 2 3 */
#define SERIALHSDEBUG 0
#define MODEMTEST   0 /* 0 or 1 */

static int data_in_serdat; /* new data written to SERDAT */
static int data_in_serdatr; /* new data received */
static int data_in_sershift; /* data transferred from SERDAT to shift register */
static uae_u16 serdatshift; /* serial shift register */
static int ovrun;
static int dtr;
static int serial_period_hsyncs, serial_period_hsync_counter;
static int ninebit;
int serdev;

void serial_open(void);
void serial_close(void);

uae_u16 serper, serdat, serdatr;

static int allowed_baudrates[] =
    { 0, 110, 300, 600, 1200, 2400, 4800, 9600, 14400,
    19200, 31400, 38400, 57600, 115200, 128000, 256000, -1 };

void SERPER (uae_u16 w)
{
    int baud = 0, i, per;
    static int warned;

    if (serper == w)  /* don't set baudrate if it's already ok */
	return;
    ninebit = 0;
    serper = w;

    if (w & 0x8000) {
        if (!warned) {
            write_log("SERIAL: program uses 9bit mode PC=%x\n", M68K_GETPC);
	    warned++;
        }
        ninebit = 1;
    }
    w &= 0x7fff;

    if (w < 13)
	w = 13;

    per = w;
    if (per == 0) per = 1;
    per = 3546895 / (per + 1);
    if (per <= 0) per = 1;
    i = 0;
    while (allowed_baudrates[i] >= 0 && per > allowed_baudrates[i] * 100 / 97)
	i++;
    baud = allowed_baudrates[i];

    serial_period_hsyncs = (((serper & 0x7fff) + 1) * 10) / maxhpos;
    if (serial_period_hsyncs <= 0)
	serial_period_hsyncs = 1;
    serial_period_hsync_counter = 0;

    write_log ("SERIAL: period=%d, baud=%d, hsyncs=%d PC=%x\n", w, baud, serial_period_hsyncs, M68K_GETPC);

    if (ninebit)
	baud *= 2;
    if (currprefs.serial_direct) {
	if (baud != 31400 && baud < 115200)
	    baud = 115200;
	serial_period_hsyncs = 1;
    }
#ifdef SERIAL_PORT
    setbaud (baud);
#endif
}

static char dochar (int v)
{
    v &= 0xff;
    if (v >= 32 && v < 127) return (char)v;
    return '.';
}

static void checkreceive (int mode)
{
#ifdef SERIAL_PORT
    static uae_u32 lastchartime;
    static int ninebitdata;
    struct timeval tv;
    int recdata;

    if (!readseravail())
	return;

    if (data_in_serdatr) {
        /* probably not needed but there may be programs that expect OVRUNs.. */
	gettimeofday (&tv, NULL);
	if (tv.tv_sec > lastchartime) {
	    ovrun = 1;
	    INTREQ (0x8000 | 0x0800);
	    while (readser (&recdata));
	    write_log ("SERIAL: overrun\n");
	}
	return;
    }

    if (ninebit) {
	for (;;) {
	    if (!readser (&recdata))
		return;
	    if (ninebitdata) {
		serdatr = (ninebitdata & 1) << 8;
		serdatr |= recdata;
		serdatr |= 0x200;
		ninebitdata = 0;
		break;
	    } else {
		ninebitdata = recdata;
		if ((ninebitdata & ~1) != 0xa8) {
		    write_log ("SERIAL: 9-bit serial emulation sync lost, %02.2X != %02.2X\n", ninebitdata & ~1, 0xa8);
		    ninebitdata = 0;
		    return;
		}
		continue;
	    }
	}
    } else {
        if (!readser (&recdata))
	    return;
	serdatr = recdata;
        serdatr |= 0x100;
    }
    gettimeofday (&tv, NULL);
    lastchartime = tv.tv_sec + 5;
    data_in_serdatr = 1;
    serial_check_irq ();
#if SERIALDEBUG > 2
    write_log ("SERIAL: received %02.2X (%c)\n", serdatr & 0xff, dochar (serdatr));
#endif
#endif
}

static void checksend (int mode)
{
    int bufstate = 0;

#ifdef SERIAL_PORT
    bufstate = checkserwrite ();
#endif
    if (!data_in_serdat && !data_in_sershift)
	return;

    if (data_in_sershift && mode == 0 && bufstate)
	data_in_sershift = 0;

    if (data_in_serdat && !data_in_sershift) {
	data_in_sershift = 1;
	serdatshift = serdat;
#ifdef SERIAL_PORT
	if (ninebit)
	    writeser (((serdatshift >> 8) & 1) | 0xa8);
	writeser (serdatshift);
#endif
	data_in_serdat = 0;
        INTREQ (0x8000 | 0x0001);
#if SERIALDEBUG > 2
	write_log ("SERIAL: send %04.4X (%c)\n", serdatshift, dochar (serdatshift));
#endif
    }
}    

void serial_hsynchandler (void)
{
#ifdef AHI
    extern void hsyncstuff(void);
    hsyncstuff();
#endif
    if (serial_period_hsyncs == 0)
	return;
    serial_period_hsync_counter++;
    if (serial_period_hsyncs == 1 || (serial_period_hsync_counter % (serial_period_hsyncs - 1)) == 0)
	checkreceive (0);
    if ((serial_period_hsync_counter % serial_period_hsyncs) == 0)
	checksend (0);
}

/* Not (fully) implemented yet: (on windows this work)
 *
 *  -  Something's wrong with the Interrupts.
 *     (NComm works, TERM does not. TERM switches to a
 *     blind mode after a connect and wait's for the end
 *     of an asynchronous read before switching blind
 *     mode off again. It never gets there on UAE :-< )
 *
 */

void SERDAT (uae_u16 w)
{
    serdat = w;

    if (!(w & 0x3ff)) {
#if SERIALDEBUG > 1
	write_log ("SERIAL: zero serial word written?! PC=%x\n", m68k_getpc());
#endif
	return;
    }

#if SERIALDEBUG > 1
    if (data_in_serdat) {
	write_log ("SERIAL: program wrote to SERDAT but old byte wasn't fetched yet\n");
    }
#endif
    
    if (serper == 372) {
	extern int enforcermode;
	if (enforcermode & 2) {
	    console_out ("%c", dochar (w));
	    if (w == 266)
		console_out("\n");
	}
    }

    data_in_serdat = 1;
    if (!data_in_sershift)
	checksend (1);

#if SERIALDEBUG > 2
    write_log ("SERIAL: wrote 0x%04x (%c) PC=%x\n", w, dochar (w), m68k_getpc());
#endif

    return;
}

uae_u16 SERDATR (void)
{
    serdatr &= 0x03ff;
    if (!data_in_serdat)
	serdatr |= 0x2000;
    if (!data_in_sershift)
	serdatr |= 0x1000;
    if (data_in_serdatr)
	serdatr |= 0x4000;
    if (ovrun)
	serdatr |= 0x8000;
#if SERIALDEBUG > 2
    write_log( "SERIAL: read 0x%04.4x (%c) %x\n", serdatr, dochar (serdatr), m68k_getpc());
#endif
    ovrun = 0;
    data_in_serdatr = 0;
    return serdatr;
}

void serial_check_irq (void)
{
    if (data_in_serdatr)
        INTREQ_0 (0x8000 | 0x0800);
}

void serial_dtr_on(void)
{
#if SERIALHSDEBUG > 0
    write_log( "SERIAL: DTR on\n" );
#endif
    dtr = 1;
    if (currprefs.serial_demand)
	serial_open ();
#ifdef SERIAL_PORT
    setserstat(TIOCM_DTR, dtr);
#endif
}

void serial_dtr_off(void)
{
#if SERIALHSDEBUG > 0
    write_log( "SERIAL: DTR off\n" );
#endif
    dtr = 0;
#ifdef SERIAL_PORT
    if (currprefs.serial_demand)
	serial_close ();
    setserstat(TIOCM_DTR, dtr);
#endif
}

void serial_flush_buffer(void)
{
}

static uae_u8 oldserbits;

static void serial_status_debug(char *s)
{
#if SERIALHSDEBUG > 1
    write_log("%s: DTR=%d RTS=%d CD=%d CTS=%d DSR=%d\n", s,
	(oldserbits & 0x80) ? 0 : 1, (oldserbits & 0x40) ? 0 : 1,
	(oldserbits & 0x20) ? 0 : 1, (oldserbits & 0x10) ? 0 : 1, (oldserbits & 0x08) ? 0 : 1);
#endif
}

uae_u8 serial_readstatus(uae_u8 dir)
{
    int status = 0;
    uae_u8 serbits = oldserbits;

#ifdef SERIAL_PORT
    getserstat (&status);
#endif
    if (!(status & TIOCM_CAR)) {
	if (!(serbits & 0x20)) {
	    serbits |= 0x20;
#if SERIALHSDEBUG > 0
	    write_log( "SERIAL: CD off\n" );
#endif
	}
    } else {
	if (serbits & 0x20) {
	    serbits &= ~0x20;
#if SERIALHSDEBUG > 0
	    write_log( "SERIAL: CD on\n" );
#endif
	}
    }

    if (!(status & TIOCM_DSR)) {
        if (!(serbits & 0x08)) {
	    serbits |= 0x08;
#if SERIALHSDEBUG > 0
	    write_log( "SERIAL: DSR off\n" );
#endif
        }
    } else {
        if (serbits & 0x08) {
	    serbits &= ~0x08;
#if SERIALHSDEBUG > 0
	    write_log( "SERIAL: DSR on\n" );
#endif
	}
    }

    if (!(status & TIOCM_CTS)) {
        if (!(serbits & 0x10)) {
	    serbits |= 0x10;
#if SERIALHSDEBUG > 0
	    write_log( "SERIAL: CTS off\n" );
#endif
        }
    } else {
        if (serbits & 0x10) {
	    serbits &= ~0x10;
#if SERIALHSDEBUG > 0
	    write_log( "SERIAL: CTS on\n" );
#endif
	}
    }

    serbits &= 0x08 | 0x10 | 0x20;
    oldserbits &= ~(0x08 | 0x10 | 0x20);
    oldserbits |= serbits;

    serial_status_debug("read");

    return oldserbits;
}

uae_u8 serial_writestatus (uae_u8 newstate, uae_u8 dir)
{
    static int logcnt = 10;

#ifdef SERIAL_PORT
    if (((oldserbits ^ newstate) & 0x80) && (dir & 0x80)) {
	if (newstate & 0x80)
	    serial_dtr_off();
	else
	    serial_dtr_on();
    }

    if (!currprefs.serial_hwctsrts && (dir & 0x40)) {
	if ((oldserbits ^ newstate) & 0x40) {
	    if (newstate & 0x40) {
		setserstat (TIOCM_RTS, 0);
#if SERIALHSDEBUG > 0
		write_log ("SERIAL: RTS cleared\n");
#endif
	    } else {
	        setserstat (TIOCM_RTS, 1);
#if SERIALHSDEBUG > 0
		write_log ("SERIAL: RTS set\n");
#endif
	    }
	}
    }

 #if 0 /* CIA io-pins can be read even when set to output.. */
    if ((newstate & 0x20) != (oldserbits & 0x20) && (dir & 0x20))
        write_log ("SERIAL: warning, program tries to use CD as an output!\n");
    if ((newstate & 0x10) != (oldserbits & 0x10) && (dir & 0x10))
        write_log ("SERIAL: warning, program tries to use CTS as an output!\n");
    if ((newstate & 0x08) != (oldserbits & 0x08) && (dir & 0x08))
        write_log ("SERIAL: warning, program tries to use DSR as an output!\n");
#endif

    if (logcnt > 0) {
	if (((newstate ^ oldserbits) & 0x40) && !(dir & 0x40)) {
	    write_log ("SERIAL: warning, program tries to use RTS as an input! PC=%x\n", M68K_GETPC);
	    logcnt--;
	}
	if (((newstate ^ oldserbits) & 0x80) && !(dir & 0x80)) {
	    write_log ("SERIAL: warning, program tries to use DTR as an input! PC=%x\n", M68K_GETPC);
	    logcnt--;
	}
    }

#endif

    oldserbits &= ~(0x80 | 0x40);
    newstate &= 0x80 | 0x40;
    oldserbits |= newstate;
    serial_status_debug("write");

    return oldserbits;
}

void serial_open(void)
{
#ifdef SERIAL_PORT
    if (serdev)
	return;
    serper = 0;
    if(!openser(currprefs.sername)) {
        write_log("SERIAL: Could not open device %s\n", currprefs.sername);
        return;
    }
    serdev = 1;
#endif
}

void serial_close (void)
{
#ifdef SERIAL_PORT
    closeser();
    serdev = 0;
#endif
}

void serial_init (void)
{
#ifdef SERIAL_PORT
    if (!currprefs.use_serial)
	return;

    if (!currprefs.serial_demand)
	serial_open ();

#endif
}

void serial_exit (void)
{
#ifdef SERIAL_PORT
    serial_close ();	/* serial_close can always be called because it	*/
#endif
    dtr = 0;		/* just closes *opened* filehandles which is ok	*/
    oldserbits = 0;	/* when exiting.				*/
}

void serial_uartbreak (int v)
{
#ifdef SERIAL_PORT
    serialuartbreak (v);
#endif
}
