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
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "serial.h"
#include "enforcer.h"
#include "arcadia.h"
#include "parallel.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#include "serial_host.h"

#define SERIALLOGGING 0
#define SERIALDEBUG 0 /* 0, 1, 2 3 */
#define SERIALHSDEBUG 0
#define SERIAL_HSYNC_BEFORE_OVERFLOW 200
#define SERIAL_BREAK_DELAY (20 * maxvpos)
#define SERIAL_BREAK_TRANSMIT_DELAY 4
#define MAX_SERIAL_TRANSMIT_PENDING 20

static bool sermap_enabled;
static uae_u32 sermap_flags;
static bool serloop_enabled;
static bool serempty_enabled;
static bool serxdevice_enabled;
static uae_u8 serstatus;
static bool ser_accurate;
static bool safe_receive;
static uae_u16 *receive_buf;
static bool sticky_receive_interrupt;
static int receive_buf_size, receive_buf_count;
static void flushqueue(void);

static int data_in_serdat; /* new data written to SERDAT */
static evt_t data_in_serdat_delay;
static evt_t serper_tx_evt;
static int serper_tx_per, serper_tx_bits, serper_tx_cycles;
static int data_in_serdatr; /* new data received */
static evt_t data_in_serdatr_evt;
static int data_in_sershift; /* data transferred from SERDAT to shift register */
static int break_in_serdatr; /* break state */
static int break_delay;
static uae_u16 serdatshift; /* serial shift register */
static int serdatshift_bits;
static uae_u16 serdatshift_masked; /* stop bit masked */
static evt_t serdatshift_start;
static int ovrun;
static int dtr;
static int serial_period_hsyncs;
static int serial_period_transmit_ccks, serial_period_receive_ccks;
static int serial_period_transmit_cck_counter, serial_period_receive_cck_counter;
static bool serial_period_transmit_cck_delayed_irq;
static int ninebit;
static int lastbitcycle_active_hsyncs;
static bool gotlogwrite;
static evt_t lastbitcycle;
static int serial_recv_previous, serial_send_previous;
static int serdatr_last_got;
int serdev;
int seriallog = 0, log_sercon = 0;
int serial_enet;
static bool seriallog_lf;
extern int consoleopen;

void serial_open (void);
void serial_close (void);

uae_u16 serper, serdat, serdatr;
static bool serper_set = false;

static const int allowed_baudrates[] =
{
	0, 110, 300, 600, 1200, 2400, 4800, 9600, 14400,
	19200, 31400, 38400, 57600, 115200, 128000, 256000, -1
};

static void sersend_end(uae_u32 v);

void SERPER (uae_u16 w)
{
	int baud = 0, mbaud = 0, i, per;
	int oldper = serper;
	static int warned;

	if (serper == w && serper_set)  /* don't set baudrate if it's already ok */
		return;

	safe_receive = false;
	serper_set = true;
	ninebit = 0;
	serper = w;
	if (w & 0x8000)
		ninebit = 1;
	w &= 0x7fff;

	per = w;
	if (per == 0)
		per = 1;
	per = (currprefs.ntscmode ? CHIPSET_CLOCK_NTSC : CHIPSET_CLOCK_PAL) / (per + 1);
	if (per <= 0)
		per = 1;
	i = 0;
	while (allowed_baudrates[i] >= 0 && per > allowed_baudrates[i] * 100 / 97)
		i++;
	baud = allowed_baudrates[i];
	if (baud <= 0) {
		baud = allowed_baudrates[1];
	}
	mbaud = baud;

	serial_period_transmit_ccks = ((serper & 0x7fff) + 1) * (1 + (8 + ninebit) + 1);
	serial_period_receive_ccks = serial_period_transmit_ccks / 4;

#if SERIALLOGGING > 0
	seriallog = 1;
	serial_period_transmit_ccks = maxhpos;
#endif
	if (log_sercon > 0) {
		seriallog = log_sercon;
		seriallog_lf = true;
		write_logx(_T("\n"));
	}

	if (!serloop_enabled || seriallog > 0) {
		write_log(_T("SERIAL: period=%d/%d, baud=%d, hsyncs=%d, bits=%d, PC=%x\n"), w, serial_period_transmit_ccks, baud, serial_period_transmit_ccks / maxhpos, ninebit ? 9 : 8, M68K_GETPC);
	}

	if (ninebit) {
		baud *= 2;
	}
	if (currprefs.serial_direct) {
		if (baud != 31400 && baud < 115200) {
			baud = 115200;
		}
		serial_period_transmit_ccks = maxhpos;
		serial_period_receive_ccks = maxhpos;
		safe_receive = true;
	}
	if (sermap_enabled || serxdevice_enabled || currprefs.m68k_speed < 0) {
		safe_receive = true;
	}

	serial_recv_previous = -1;
	serial_send_previous = -1;

#ifdef SERIAL_PORT
	setbaud(baud, mbaud);
#endif

	flushqueue();

	// mid transmit period change
	if ((serloop_enabled || serempty_enabled) && ser_accurate) {
		evt_t c = get_cycles();
		evt_t n = serper_tx_evt + serper_tx_cycles * CYCLE_UNIT;
		if (n > c) {
			int cycles_transmitted = (int)((c - serper_tx_evt) / CYCLE_UNIT);
			serper_tx_cycles -= cycles_transmitted;
			if (serper_tx_cycles >= 0) {
				int serper_tx_cycles_mod = serper_tx_cycles % serper_tx_per;
				serper_tx_cycles /= serper_tx_per;
				serper_tx_per = (serper & 0x7fff) + 1;
				serper_tx_cycles *= serper_tx_per;
				serper_tx_cycles += serper_tx_cycles_mod;
				serper_tx_evt = c;
				event2_newevent_x_replace_exists(serper_tx_cycles, 0, sersend_end);
			}
		}
	}

}

#define SERDATR_IRQACK 2
#define SERDATR_READ 1

static void serial_rx_irq(void)
{
	int delay = 9;
	// Data in receive buffer
	data_in_serdatr = SERDATR_READ  | SERDATR_IRQACK;
	data_in_serdatr_evt = get_cycles() + delay * CYCLE_UNIT;
	if (ser_accurate) {
		INTREQ_INT(11, delay);
	} else {
		INTREQ_INT(11, 0);
	}
	serdatr_last_got = 0;
}

bool serreceive_external(uae_u16 v)
{
	if (data_in_serdatr) {
		if (!receive_buf) {
			receive_buf_size = 200;
			receive_buf = xcalloc(uae_u16, receive_buf_size);
			if (!receive_buf) {
				return false;
			}
		}
		if (receive_buf_count >= receive_buf_size) {
			return false;
		}
		receive_buf[receive_buf_count++] = v;
		return true;
	}
	serdatr = v;
	serial_rx_irq();
	return true;
}

static void receive_next_buffered(void)
{
	if (receive_buf && receive_buf_count > 0 && !(intreq & (1 << 11))) {
		uae_u16 v = receive_buf[0];
		receive_buf_count--;
		if (receive_buf_count > 0) {
			memmove(receive_buf, receive_buf + 1, receive_buf_count * sizeof(uae_u16));
		}
		serreceive_external(v);
	}
}

void serial_rethink(void)
{
	if (data_in_serdatr == SERDATR_READ) {
		int sdr = 1;
		if (serloop_enabled) {
			sdr = 0;
		}
		if (serxdevice_enabled) {
			sdr = 1;
		}
		if (safe_receive) {
			sdr = 1;
		}
		if (ser_accurate && get_cycles() <= data_in_serdatr_evt) {
			sdr = 0;
		}
		// We get here if program cleared RBF bit but didn't yet read SERDATR
		// Without this hack data can be lost when using fast emulation modes
		// and physical serial port or internally emulated serial devices.
		if (sdr && (intena & (1 << 11)) && (intena & (1 << 14)) && !(intreq & (1 << 11))) {
			INTREQ_INT(11, 0);
		}
	}
}

static TCHAR docharlog(int v)
{
	v &= 0xff;
	if (v >= 32 && v < 127)
		return v;
	if (v == 10)
		return 10;
	return '.';
}

static TCHAR dochar(int v)
{
	v &= 0xff;
	if (v >= 32 && v < 127)
		return v;
	return '.';
}

static void maybesetoverrun(void)
{
	if ((data_in_serdatr & SERDATR_IRQACK) && serdatr_last_got > SERIAL_HSYNC_BEFORE_OVERFLOW) {
#if SERIALDEBUG > 0
		write_log(_T("SERIAL: OVERRUN\n"));
#endif
		flushser();
		ovrun = true;
		sticky_receive_interrupt = false;
	}
	serdatr_last_got = 0;
}

static bool canreceive(void)
{
	// don't replace data in SERDATR until interrupt is cleared and SERDATR is read in safe receive mode
	if (safe_receive) {
		if (data_in_serdatr) {
			return false;
		}
	}
	if (!(data_in_serdatr & SERDATR_IRQACK)) {
		return true;
	}
	if (currprefs.serial_direct) {
		return false;
	}
	if (currprefs.cpu_cycle_exact) {
		return true;
	}
	if (serdatr_last_got > SERIAL_HSYNC_BEFORE_OVERFLOW) {
		return true;
	}
	return false;
}

static void checkreceive_enet (void)
{
#ifdef SERIAL_ENET
	uae_u16 recdata;

	if (!canreceive())
		return;
	if (!enet_readseravail ())
		return;
	maybesetoverrun();
	if (!enet_readser (&recdata))
		return;
	serdatr = recdata & 0x1ff;
	if (recdata & 0x200)
		serdatr |= 0x200;
	else
		serdatr |= 0x100;
	serial_rx_irq();
#if SERIALDEBUG > 2
	write_log (_T("SERIAL: received %02X (%c)\n"), serdatr & 0xff, dochar (serdatr));
#endif
#endif
}

static void checkreceive_serial (void)
{
#ifdef SERIAL_PORT
	static int ninebitdata;
	int recdata;

	if (!canreceive()) {
		return;
	}

	if (ninebit) {
		bool breakcond;
		int status = readseravail(&breakcond);
		if (break_in_serdatr == -1 || break_in_serdatr > 0) {
			serial_recv_previous = 0;
			serdatr = 0;
			if (break_in_serdatr < 0) {
				break_in_serdatr = SERIAL_BREAK_DELAY;
				break_delay = SERIAL_BREAK_TRANSMIT_DELAY;
			}
			if (break_in_serdatr == 1) {
				serdatr |= 0x200;
				break_in_serdatr = 0;
			}
			break_delay--;
			if (break_delay && break_in_serdatr) {
				return;
			}
			break_delay = SERIAL_BREAK_TRANSMIT_DELAY;
		} else {
			if (breakcond && !break_in_serdatr) {
				break_in_serdatr = -1;
				break_in_serdatr -= status;
				if (break_in_serdatr == -1)
					return;
			}
			if (status <= 0) {
				return;
			}
			for (;;) {
				status = readser(&recdata);
				if (!status)
					return;
				if (break_in_serdatr > 0) {
					break_in_serdatr = 0;
				}
				if (ninebitdata) {
					serdatr = (ninebitdata & 1) << 8;
					serdatr |= recdata;
					serdatr |= 0x200;
					ninebitdata = 0;
					if (break_in_serdatr < -1) {
						break_in_serdatr++;
					}
					break;
				} else {
					ninebitdata = recdata;
					if ((ninebitdata & ~1) != 0xa8) {
						write_log(_T("SERIAL: 9-bit serial emulation sync lost, %02X != %02X\n"), ninebitdata & ~1, 0xa8);
						ninebitdata = 0;
						return;
					}
					continue;
				}
			}
		}
	} else {
		bool breakcond;
		int status = readseravail(&breakcond);
		if (break_in_serdatr == -1 || break_in_serdatr > 0) {
			// break: stop bit is zero
			// Paula for some reason keeps receiving zeros continuously in break condition.
			serial_recv_previous = 0;
			serdatr = 0;
			if (break_in_serdatr < 0) {
				break_in_serdatr = SERIAL_BREAK_DELAY;
				break_delay = SERIAL_BREAK_TRANSMIT_DELAY;
#if SERIALDEBUG
				write_log(_T("SERIAL: BREAK START\n"));
#endif
			}
			// serial.device requires valid serial word before it finally returns break error
			if (break_in_serdatr == 1) {
				serdatr |= 0x100;
#if SERIALDEBUG
				write_log(_T("SERIAL: BREAK COMPLETE\n"));
#endif
				break_in_serdatr = 0;
			}
			break_delay--;
			if (break_delay && break_in_serdatr) {
				return;
			}
			break_delay = SERIAL_BREAK_TRANSMIT_DELAY;
		} else {
			if (breakcond && !break_in_serdatr) {
				break_in_serdatr = -1;
#if SERIALDEBUG
				write_log(_T("SERIAL: BREAK DETECT (%d buffered)\n"), status);
#endif
				break_in_serdatr -= status;
				if (break_in_serdatr == -1)
					return;
			}
			if (status <= 0) {
				return;
			}
			if (!readser(&recdata))
				return;
			if (break_in_serdatr > 0) {
#if SERIALDEBUG
				write_log(_T("SERIAL: BREAK END\n"));
#endif
				break_in_serdatr = 0;
			}
			if (currprefs.serial_crlf) {
				if (recdata == 0 || (serial_recv_previous == 13 && recdata == 10)) {
					//write_log(_T(" [%02X] "), (uae_u8)recdata);
					serial_recv_previous = -1;
					return;
				}
			}
			//write_log(_T(" %02X "), (uae_u8)recdata);
			serial_recv_previous = recdata;
			serdatr = recdata;
			serdatr |= 0x0100;
			if (break_in_serdatr < -1) {
				break_in_serdatr++;
			}
		}
	}

	serial_rx_irq();
#if SERIALDEBUG > 2
	write_log(_T("SERIAL: received %02X (%c)\n"), serdatr & 0xff, dochar(serdatr));
#endif
#endif
}


static void serdatcopy(void);

static void checksend(void)
{
	if (data_in_sershift != 1 && data_in_sershift != 2) {
		return;
	}
	if (serloop_enabled) {
		return;
	}

#ifdef ARCADIA
	if (alg_flag || currprefs.genlock_image >= 7) {
		ld_serial_read(serdatshift);
	}
#endif
	if (cubo_enabled) {
		touch_serial_read(serdatshift);
	}
#ifdef RETROPLATFORM
	if (rp_ismodem()) {
		rp_writemodem((uae_u8)serdatshift_masked);
	}
#endif
	if (serempty_enabled && !serxdevice_enabled) {
		goto end;
	}
	if (host_serial_map_available() && sermap_enabled) {
		host_serial_map_send(serdatshift);
	}
#ifdef SERIAL_ENET
	if (serial_enet) {
		enet_writeser(serdatshift);
	}
#endif
#ifdef SERIAL_PORT
	if (ninebit) {
		if (!checkserwrite(2)) {
			data_in_sershift = 2;
			return;
		}
		writeser(((serdatshift >> 8) & 1) | 0xa8);
		writeser(serdatshift_masked);
	} else {
		if (currprefs.serial_crlf) {
			if (serdatshift_masked == 10 && serial_send_previous != 13) {
				if (!checkserwrite(2)) {
					data_in_sershift = 2;
					return;
				}
				writeser(13);
			}
		}
		if (!checkserwrite(1)) {
			data_in_sershift = 2;
			return;
		}
		writeser(serdatshift_masked);
		serial_send_previous = serdatshift_masked;
	}
#endif
end:
	if (serial_period_transmit_ccks <= maxhpos || data_in_sershift == 2) {
		data_in_sershift = 0;
		serdatcopy();
	} else {
		data_in_sershift = 3;
	}
#if SERIALDEBUG > 2
	write_log(_T("SERIAL: send %04X (%c)\n"), serdatshift, dochar(serdatshift));
#endif
}

static bool checkshiftempty(void)
{
	checksend();
	if (data_in_sershift == 3) {
		data_in_sershift = 0;
		serdatcopy();
		return true;
	}
	return false;
}

static void sersend_end(uae_u32 v)
{
	data_in_sershift = 0;
	serdatcopy();
	data_in_serdat = 0;
	data_in_serdat_delay = get_cycles() + CYCLE_UNIT;
}

static void sersend_serloop(uae_u32 v)
{
	serdatr = serdatshift;
	serial_rx_irq();
	event2_newevent_xx(-1, v * CYCLE_UNIT, 0, sersend_end);
}

static void sersend_ce(uae_u32 v)
{
	if (checkshiftempty()) {
		lastbitcycle = get_cycles() + ((serper & 0x7fff) + 1) * CYCLE_UNIT;
		lastbitcycle_active_hsyncs = ((serper & 0x7fff) + 1) / maxhpos + 2;
	} else if (data_in_sershift == 1 || data_in_sershift == 2) {
		event2_newevent_x_replace(maxhpos, 0, sersend_ce);
	}
}

static void serdatcopy(void)
{
	int sper = (serper & 0x7fff) + 1;
	int bits;

	if (data_in_sershift > 0 || !data_in_serdat)
		return;
	serdatshift = serdat;
	bits = 8;
	if ((serdatshift & 0xff80) == 0x80) {
		bits = 7;
	}
	serdatshift_start = get_cycles();
	serdatshift_masked = serdatshift & ((1 << bits) - 1);
	data_in_serdat = 0;

	if (seriallog > 0 || (consoleopen && seriallog < 0)) {
		gotlogwrite = true;
		if (seriallog_lf && seriallog > 2) {
			TCHAR *ts = write_log_get_ts();
			if (ts)
				write_logx(_T("%s:"), ts);
			seriallog_lf = false;
		}
		TCHAR ch = docharlog(serdatshift_masked);
		write_logx(_T("%c"), ch);
		if (ch == 10)
			seriallog_lf = true;
	}

	if (serper == 372) {
		if (enforcermode & 2) {
			console_out_f(_T("%c"), docharlog(serdatshift_masked));
		}
	}

	if (data_in_sershift == 0) {
		// delay transmit interrupt if number of queued transmitted bytes is too large.
		if (!ser_accurate && serial_period_transmit_cck_counter >= MAX_SERIAL_TRANSMIT_PENDING * serial_period_transmit_ccks) {
			serial_period_transmit_cck_delayed_irq = true;
		} else {
			INTREQ_INT(0, 1);
		}
	}
	data_in_sershift = 1;

	serdatshift_bits = 16;
	for (int i = 15; i >= 0; i--) {
		if (serdatshift & (1 << i))
			break;
		serdatshift_bits--;
	}

	data_in_serdat_delay = get_cycles() + CYCLE_UNIT;

	// if someone uses serial port as some kind of timer..
	if (ser_accurate) {
		int per = sper * (serdatshift_bits + 1);

		serper_tx_evt = get_cycles();
		serper_tx_per = sper;
		serper_tx_bits = serdatshift_bits + 1;
		serper_tx_cycles = per;

		// not connected, emulate only TX
		if (serempty_enabled && !serxdevice_enabled) {
			event2_newevent_xx(-1, per * CYCLE_UNIT, 0, sersend_end);
			return;
		}

		// loopback receive complete
		if (serloop_enabled) {
			int recper = sper * (ninebit ? 10 : 9) + (sper - 1) / 2;
			int perdiff = per - recper;
			if (perdiff < 0) {
				perdiff = 0;
			}

			// TX to RX delay (1488/1489 chip delays)
			recper += 3;

			event2_newevent_xx(-1, recper * CYCLE_UNIT, perdiff, sersend_serloop);
			return;
		}

		if (lastbitcycle_active_hsyncs) {
			// if last bit still transmitting, add remaining time.
			int extraper = (int)((lastbitcycle - get_cycles()) / CYCLE_UNIT);
			per += extraper;
		}

		event2_newevent_x_replace(per, 0, sersend_ce);

		checksend();

	} else {

		if (serloop_enabled) {
			sersend_serloop(0);
		} else {
			checksend();
			sersend_end(0);
		}

	}

}


void serial_hsynchandler (void)
{
	host_serial_hsync();
	bool can = canreceive();

#ifdef ARCADIA
	if (alg_flag || currprefs.genlock_image >= 7) {
		if (can) {
			int ch = ld_serial_write();
			if (ch >= 0) {
				serdatr = ch | 0x100;
				serial_rx_irq();
			}
		}
	}
#endif
	if (cubo_enabled) {
		if (can) {
			int ch = touch_serial_write();
			if (ch >= 0) {
				serdatr = ch | 0x100;
				serial_rx_irq();
			}
		}
	}
	if (seriallog > 1 && !data_in_serdatr && gotlogwrite) {
		int ch = read_log();
		if (ch > 0) {
			serdatr = ch | 0x100;
			serial_rx_irq();
		}
	}

	if (lastbitcycle_active_hsyncs > 0) {
		lastbitcycle_active_hsyncs--;
	}
	if (host_serial_map_available() && sermap_enabled) {
		if (can) {
			for (;;) {
				uae_u32 v = host_serial_map_receive();
				if (v == 0xffffffff) {
					break;
				}
				if (!(v & 0xffff0000)) {
					serdatr = (uae_u16)v;
					serial_rx_irq();
					break;
				} else if ((v & 0x80000000) == 0x80000000) {
					sermap_flags &= 0x0fff0000;
					sermap_flags |= v & 0xffff;
				} else if ((v & 0x40000000) == 0x40000000) {
					if (v & (0x10000 | 0x20000)) {
						sermap_flags &= ~(0x10000 | 0x20000);
						sermap_flags |= v & (0x10000 | 0x20000);
						break;
					}
				}
			}
		}
		// break on
		if (sermap_flags & 0x20000) {
			break_in_serdatr = maxvpos;
		}
		if (break_in_serdatr) {
			serdatr = 0;
			if (break_delay == 0) {
				serial_rx_irq();
				break_delay = SERIAL_BREAK_TRANSMIT_DELAY;
			}
			if (break_delay > 0) {
				break_delay--;
			}
		}
		// break off
		if (break_in_serdatr == 1) {
			break_in_serdatr = 0;
			break_delay = 0;
			serdatr |= 0x100;
			serial_rx_irq();
		}
	}
	if (data_in_serdatr & SERDATR_IRQACK) {
		serdatr_last_got++;
	}
	if (!ser_accurate) {
		serial_period_transmit_cck_counter -= maxhpos;
		if (serial_period_transmit_cck_delayed_irq && serial_period_transmit_cck_counter < MAX_SERIAL_TRANSMIT_PENDING * serial_period_transmit_ccks) {
			serial_period_transmit_cck_delayed_irq = false;
			INTREQ_INT(0, 1);
		}
		if (serial_period_transmit_cck_counter < 0) {
			serial_period_transmit_cck_counter = 0;
		}
	} else {
		serial_period_transmit_cck_counter = 0;
	}
	serial_period_receive_cck_counter += maxhpos;
	if (serial_period_receive_cck_counter >= serial_period_receive_ccks && serial_period_receive_ccks) {
		serial_period_receive_cck_counter %= serial_period_receive_ccks;
		serial_period_receive_cck_counter += maxhpos - 1;
		receive_next_buffered();
		checkreceive_serial();
		checkreceive_enet();
	}


	if (break_in_serdatr > 1) {
		break_in_serdatr--;
		if (break_in_serdatr == 1) {
#if SERIALDEBUG
			write_log(_T("SERIAL: BREAK TIMEOUT\n"));
#endif
			flushser();
		}
	}
}

static void flushqueue(void)
{
	if (host_serial_map_available() && sermap_enabled) {
		while (host_serial_map_receive() != 0xffffffff);
	}
	for (;;) {
		bool breakcond;
		int recdata;
		if (!readseravail(&breakcond)) {
			break;
		}
		readser(&recdata);
	}
}

static void SERDAT_send(uae_u32 v)
{
	uae_u16 w = (uae_u16)v;

	serial_period_transmit_cck_counter += serial_period_transmit_ccks;
#if SERIALDEBUG > 2
	write_log(_T("SERIAL: SERDAT write 0x%04x (%c) PC=%x\n"), w, dochar(w), M68K_GETPC);
#endif
	if (ser_accurate) {
		serdat = w;
		data_in_serdat = 1;
		if (!data_in_sershift) {
			if (serloop_enabled || serempty_enabled || serxdevice_enabled) {
				data_in_sershift = -1;
				INTREQ_INT(0, 1);
			}
			serdatcopy();
		}

	} else {
		serdatcopy();

		serdat = w;

		if (!w) {
#if SERIALDEBUG > 1
			write_log (_T("SERIAL: zero serial word written?! PC=%x\n"), M68K_GETPC);
#endif
			return;
		}

#if SERIALDEBUG > 1
		if (data_in_serdat) {
			write_log (_T("SERIAL: program wrote to SERDAT but old byte wasn't fetched yet\n"));
		}
#endif

		data_in_serdat = 1;
		serdatcopy();
	}
}

uae_u16 SERDATR(void)
{
	serdatr &= 0x03ff;
	if (!data_in_serdat && ((!ser_accurate && !serial_period_transmit_cck_delayed_irq) || (ser_accurate && get_cycles() >= data_in_serdat_delay))) {
		serdatr |= 0x2000; // TBE (Transmit buffer empty)
	}
	if (!data_in_sershift && (serdatr & 0x2000)) {
		serdatr |= 0x1000; // TSRE (Transmit shift register empty)
	}
	if (data_in_serdatr & SERDATR_IRQACK) {
		serdatr |= 0x4000; // RBF (Receive buffer full)
	}
	if (ovrun) {
		serdatr |= 0x8000; // OVRUN (Receiver overrun)
	}
	if (serloop_enabled) {
		int per = ((serper & 0x7fff) + 1) * CYCLE_UNIT;
		evt_t sper = per * serdatshift_bits;
		evt_t c = get_cycles();
		evt_t diff = serdatshift_start + sper - c;
		if (break_in_serdatr < 0) {
			serdatr |= 0x0800;
		} else if (diff > 0) {
			int bit = (int)(c - serdatshift_start) / per;
			if (bit > 0 && !(serdatshift & (1 << (bit - 1)))) {
				serdatr |= 0x0800;
			}
		} else {
			if (break_in_serdatr <= 0) {
				serdatr |= 0x0800;
			}
		}
	} else {
		if (break_in_serdatr <= 0) {
			serdatr |= 0x0800; // RXD
		}
	}
#if SERIALDEBUG > 2
	write_log (_T("SERIAL: read 0x%04x (%c) %x\n"), serdatr, dochar (serdatr), M68K_GETPC);
#endif
	data_in_serdatr &= ~SERDATR_READ;
	if (!data_in_serdatr) {
		// interrupt was previously cleared but SERDATR was not read.
		// Clear it now when SERDATR was read.
		INTREQ_INT_clear(11, 0);
	}
	serdatr_last_got = 0;
	return serdatr;
}

void SERDAT(uae_u16 w)
{
	if (ser_accurate) {
		event2_newevent_xx(-1, 1 * CYCLE_UNIT, w, SERDAT_send);
	} else {
		SERDAT_send(w);
	}
}

void serial_rbf_change(bool set)
{
	ovrun = set;
	if (!set) {
		data_in_serdatr &= ~SERDATR_IRQACK;
	}
}

void serial_dtr_on(void)
{
#if SERIALHSDEBUG > 0
	write_log ( "SERIAL: DTR on\n" );
#endif
#ifdef RETROPLATFORM
	if (rp_ismodem()) {
		rp_modemstate(1);
	}
#endif
	dtr = 1;
	if (currprefs.serial_demand)
		serial_open();
#ifdef SERIAL_PORT
	setserstat(SERIAL_HOST_DTR, dtr);
#endif
}

void serial_dtr_off(void)
{
#if SERIALHSDEBUG > 0
	write_log ( "SERIAL: DTR off\n" );
#endif
#ifdef RETROPLATFORM
	if (rp_ismodem()) {
		rp_modemstate(0);
	}
#endif
	dtr = 0;
#ifdef SERIAL_PORT
	if (currprefs.serial_demand)
		serial_close();
	setserstat(SERIAL_HOST_DTR, dtr);
#endif
}

void serial_flush_buffer (void)
{
}

static uae_u8 oldserbits;

static void serial_status_debug(const TCHAR *s)
{
#if SERIALHSDEBUG > 1
	write_log (_T("%s: DTR=%d RTS=%d CD=%d CTS=%d DSR=%d\n"), s,
		(oldserbits & 0x80) ? 0 : 1, (oldserbits & 0x40) ? 0 : 1,
		(oldserbits & 0x20) ? 0 : 1, (oldserbits & 0x10) ? 0 : 1, (oldserbits & 0x08) ? 0 : 1);
#endif
}

uae_u8 serial_readstatus(uae_u8 v, uae_u8 dir)
{
	int status = 0;
	uae_u8 serbits = oldserbits;

	if (serloop_enabled) {
		if (serstatus & 0x80) { // DTR -> DSR + CD
			status |= SERIAL_HOST_DSR | SERIAL_HOST_CAR;
		}
		if (serstatus & 0x10) { // RTS -> CTS
			status |= SERIAL_HOST_CTS;
		}

#ifdef RETROPLATFORM
	} else if (rp_ismodem()) {
		bool dsr, cd, cts, ri;
		rp_readmodemstatus(&dsr, &cd, &cts, &ri);
		if (dsr) {
			status |= SERIAL_HOST_DSR;
		}
		if (cd) {
			status |= SERIAL_HOST_CAR;
		}
		if (cts) {
			status |= SERIAL_HOST_CTS;
		}
		if (ri) {
			status |= SERIAL_HOST_RI;
		}

#endif
	} else if (sermap_enabled) {
		if (sermap_flags & 1) {
			status |= SERIAL_HOST_DSR;
		}
		if (sermap_flags & 2) {
			status |= SERIAL_HOST_CAR;
		}
		if (sermap_flags & 4) {
			status |= SERIAL_HOST_CTS;
		}
	} else if (currprefs.use_serial) {
#ifdef SERIAL_PORT
		getserstat(&status);
#endif
	} else {
		return v;
	}

	if (currprefs.serial_rtsctsdtrdtecd) {
		if (!(status & SERIAL_HOST_CAR)) {
			if (!(serbits & 0x20)) {
				serbits |= 0x20;
#if SERIALHSDEBUG > 0
				write_log ( "SERIAL: CD off\n" );
#endif
			}
		} else {
			if (serbits & 0x20) {
				serbits &= ~0x20;
#if SERIALHSDEBUG > 0
				write_log ( "SERIAL: CD on\n" );
#endif
			}
		}

		if (!(status & SERIAL_HOST_DSR)) {
			if (!(serbits & 0x08)) {
				serbits |= 0x08;
#if SERIALHSDEBUG > 0
				write_log ( "SERIAL: DSR off\n" );
#endif
			}
		} else {
			if (serbits & 0x08) {
				serbits &= ~0x08;
#if SERIALHSDEBUG > 0
				write_log ( "SERIAL: DSR on\n" );
#endif
			}
		}

		if (!(status & SERIAL_HOST_CTS)) {
			if (!(serbits & 0x10)) {
				serbits |= 0x10;
#if SERIALHSDEBUG > 0
				write_log ( "SERIAL: CTS off\n" );
#endif
			}
		} else {
			if (serbits & 0x10) {
				serbits &= ~0x10;
#if SERIALHSDEBUG > 0
				write_log ( "SERIAL: CTS on\n" );
#endif
			}
		}
	}

	// SEL == RI
	if (isprinter()) {
		serbits &= ~0x04;
		serbits |= v & 0x04;
		if (currprefs.serial_ri && (status & SERIAL_HOST_RI)) {
			serbits &= ~0x04;
		}
	} else if (currprefs.serial_ri) {
		serbits |= 0x04;
		if (status & SERIAL_HOST_RI) {
			serbits &= ~0x04;
		}
	} else {
		serbits &= ~0x04;
		serbits |= v & 0x04;
	}

	serbits &= 0x04 | 0x08 | 0x10 | 0x20;
	oldserbits &= ~(0x04 | 0x08 | 0x10 | 0x20);
	oldserbits |= serbits;

	serial_status_debug (_T("read"));

	v = (v & (0x80 | 0x40 | 0x02 | 0x01)) | serbits;
	return v;
}

uae_u8 serial_writestatus (uae_u8 newstate, uae_u8 dir)
{
	static int logcnt = 10;

	serstatus = newstate & dir;

#ifdef SERIAL_PORT
	if (currprefs.use_serial) {
		if (currprefs.serial_rtsctsdtrdtecd && ((oldserbits ^ newstate) & 0x80) && (dir & 0x80)) {
			if (newstate & 0x80)
				serial_dtr_off();
			else
				serial_dtr_on();
		}
		if (!currprefs.serial_hwctsrts && currprefs.serial_rtsctsdtrdtecd && (dir & 0x40)) {
			if ((oldserbits ^ newstate) & 0x40) {
				if (newstate & 0x40) {
					setserstat(SERIAL_HOST_RTS, 0);
#if SERIALHSDEBUG > 0
					write_log(_T("SERIAL: RTS cleared\n"));
#endif
				} else {
					setserstat(SERIAL_HOST_RTS, 1);
#if SERIALHSDEBUG > 0
					write_log(_T("SERIAL: RTS set\n"));
#endif
				}
			}
		}
	}
#endif

#if 0 /* CIA io-pins can be read even when set to output.. */
	if ((newstate & 0x20) != (oldserbits & 0x20) && (dir & 0x20))
		write_log (_T("SERIAL: warning, program tries to use CD as an output!\n"));
	if ((newstate & 0x10) != (oldserbits & 0x10) && (dir & 0x10))
		write_log (_T("SERIAL: warning, program tries to use CTS as an output!\n"));
	if ((newstate & 0x08) != (oldserbits & 0x08) && (dir & 0x08))
		write_log (_T("SERIAL: warning, program tries to use DSR as an output!\n"));
#endif

	if (logcnt > 0) {
		if (((newstate ^ oldserbits) & 0x40) && !(dir & 0x40)) {
			write_log(_T("SERIAL: warning, program tries to use RTS as an input! PC=%x\n"), M68K_GETPC);
			logcnt--;
		}
		if (((newstate ^ oldserbits) & 0x80) && !(dir & 0x80)) {
			write_log(_T("SERIAL: warning, program tries to use DTR as an input! PC=%x\n"), M68K_GETPC);
			logcnt--;
		}
	}

#ifdef RETROPLATFORM
	if (rp_ismodem()) {
		if ((oldserbits & (0x80 | 0x40)) != (newstate & (0x80 | 0x40))) {
			rp_writemodemstatus(
				(newstate & 0x40) == 0, (oldserbits & 0x40) != (newstate & 0x40),
				(newstate & 0x80) == 0, (oldserbits & 0x80) != (newstate & 0x80));
		}
	}
#endif

	if (host_serial_map_available() && sermap_enabled) {
		uae_u32 flags = 0x80000000;
		bool changed = false;
		if (currprefs.serial_rtsctsdtrdtecd && ((oldserbits ^ newstate) & 0x80) && (dir & 0x80)) {
			if (!(newstate & 0x80)) {
				flags |= 1; // DSR
				flags |= 2; // CD
			}
			changed = true;
		}
		if (!currprefs.serial_hwctsrts && currprefs.serial_rtsctsdtrdtecd && (dir & 0x40)) {
			if ((oldserbits ^ newstate) & 0x40) {
				if (!(newstate & 0x40)) {
					flags |= 4; // RTS
				}
				changed = true;
			}
		}
		if (changed) {
			host_serial_map_send(flags);
		}
	}


	oldserbits &= ~(0x80 | 0x40);
	newstate &= 0x80 | 0x40;
	oldserbits |= newstate;

	serial_status_debug(_T("write"));

	return oldserbits;
}

static int enet_is (TCHAR *name)
{
	return !_tcsnicmp (name, _T("ENET:"), 5);
}

void serial_open (void)
{
#ifdef SERIAL_PORT
	if (serdev)
		return;
	serper = 0;
	if (0) {
#ifdef SERIAL_ENET
	} else if (enet_is (currprefs.sername)) {
		enet_open (currprefs.sername);
#endif
	} else if (!_tcsicmp(currprefs.sername, SERIAL_INTERNAL)) {
		sermap_enabled = true;
	} else if (!_tcsicmp(currprefs.sername, SERIAL_LOOPBACK)) {
		serloop_enabled = true;
	} else if (!currprefs.sername[0] && currprefs.win32_midioutdev < -1 && currprefs.win32_midiindev < 0) {
		serempty_enabled = true;
	} else {
		if(currprefs.sername[0] && !openser(currprefs.sername)) {
			write_log (_T("SERIAL: Could not open device %s\n"), currprefs.sername);
			return;
		}
	}
	if (alg_flag || currprefs.genlock_image >= 7 || cubo_enabled || sermap_enabled
#ifdef RETROPLATFORM
		|| rp_ismodem()
#endif
	) {
		serxdevice_enabled = true;
	}
	serdev = 1;
	ser_accurate = currprefs.cpu_memory_cycle_exact || (currprefs.cpu_model <= 68020 && currprefs.cpu_compatible && currprefs.m68k_speed == 0);
#endif
}

void serial_map_deactivate(void)
{
	sermap_enabled = false;
	sermap_flags = 0;
	host_serial_map_deactivate();
}

void serial_close(void)
{
#ifdef SERIAL_PORT
	closeser();
#ifdef SERIAL_ENET
	enet_close();
#endif
	serdev = 0;
	serial_map_deactivate();
#endif
#ifdef RETROPLATFORM
	if (rp_ismodem()) {
		rp_modemstate(0);
	}
#endif
	if (receive_buf) {
		xfree(receive_buf);
		receive_buf = NULL;
	}
	receive_buf_size = 0;
	receive_buf_count = 0;

	serloop_enabled = false;
	serempty_enabled = false;
	serxdevice_enabled = false;
	serper_set = false;
	ser_accurate = false;
	data_in_serdat_delay = 0;
}

void serial_init(void)
{
#ifdef SERIAL_PORT
	if (!currprefs.use_serial) {
		serial_open();
		return;
	}
	if (!currprefs.serial_demand)
		serial_open();
#endif
}

void serial_exit (void)
{
#ifdef SERIAL_PORT
	serial_close ();	/* serial_close can always be called because it	*/
#endif
	dtr = 0;		/* just closes *opened* filehandles which is ok	*/
	oldserbits = 0;	/* when exiting.				*/
	data_in_serdat = 0;
	data_in_serdatr = 0;
	data_in_sershift = 0;
	break_in_serdatr = 0;
	ovrun = 0;
	serdatr_last_got = 0;
}

void serial_uartbreak (int v)
{
	if (host_serial_map_available() && sermap_enabled) {
		host_serial_map_send(0x40000000 | (v ? 0x20000 : 0x10000));
	}
#ifdef SERIAL_PORT
	serialuartbreak (v);
#endif
}
