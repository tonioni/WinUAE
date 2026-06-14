#include "sysconfig.h"
#include "sysdeps.h"

#ifdef SERIAL_PORT

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "custom.h"
#include "options.h"
#include "serial.h"
#include "uae.h"
#ifdef WITH_MIDI
#include "midi.h"
#endif
#ifdef WITH_MIDIEMU
#include "midiemu.h"
#endif

#define SERIALDEBUG 0
#define SERIAL_LOOPBACK _T("LOOPBACK_SERIAL")

void serial_open(void);
void serial_close(void);

int serdev;
int seriallog;
int log_sercon;
int doreadser;
int serstat = -1;
uae_u16 serper;
uae_u16 serdat;

static int serial_fd = -1;
static int listen_fd = -1;
static int conn_fd = -1;
static bool tcpserial;
static bool serloop_enabled;
static bool serempty_enabled;
static bool rx_full;
static bool rx_irq;
static bool ovrun;
static bool dtr;
static bool telnet_iac;
static int telnet_skip;
static uae_u16 serdatr;
static uae_u8 oldserbits;
static uae_u8 serial_send_previous = 0xff;
static struct termios saved_tios;
static bool saved_tios_valid;

static void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}
}

static speed_t baud_to_speed(int baud)
{
	switch (baud) {
	case 300: return B300;
	case 1200: return B1200;
	case 2400: return B2400;
	case 4800: return B4800;
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
#ifdef B230400
	case 230400: return B230400;
#endif
	default: return B9600;
	}
}

static int serial_period_to_baud(uae_u16 v)
{
	if ((v & 0x7fff) == 0) {
		return 0;
	}
	const double hz = currprefs.ntscmode ? 3579545.0 : 3546895.0;
	const int baud = (int)(hz / (double)((v & 0x7fff) + 1) + 0.5);
	const int standard[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400 };
	int best = standard[0];
	int bestdiff = abs(baud - best);
	if (baud >= 30000 && baud <= 32500) {
		return 31400;
	}
	for (int i = 1; i < (int)(sizeof standard / sizeof standard[0]); i++) {
		int diff = abs(baud - standard[i]);
		if (diff < bestdiff) {
			best = standard[i];
			bestdiff = diff;
		}
	}
	return best;
}

static bool configure_serial_fd(int baud)
{
	if (serial_fd < 0) {
		return false;
	}
	struct termios tios;
	if (tcgetattr(serial_fd, &tios) < 0) {
		write_log(_T("SERIAL: tcgetattr failed: %s\n"), strerror(errno));
		return false;
	}
	if (!saved_tios_valid) {
		saved_tios = tios;
		saved_tios_valid = true;
	}
	cfmakeraw(&tios);
	speed_t speed = baud_to_speed(baud > 0 ? baud : 9600);
	cfsetispeed(&tios, speed);
	cfsetospeed(&tios, speed);
	tios.c_cflag |= CLOCAL | CREAD;
	if (currprefs.serial_hwctsrts) {
#ifdef CRTSCTS
		tios.c_cflag |= CRTSCTS;
#endif
	} else {
#ifdef CRTSCTS
		tios.c_cflag &= ~CRTSCTS;
#endif
	}
	tios.c_cflag &= ~CSTOPB;
	if (currprefs.serial_stopbits) {
		tios.c_cflag |= CSTOPB;
	}
	if (tcsetattr(serial_fd, TCSANOW, &tios) < 0) {
		write_log(_T("SERIAL: tcsetattr failed: %s\n"), strerror(errno));
		return false;
	}
	return true;
}

static void close_fd(int *fd)
{
	if (*fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

static bool tcp_accept_pending(void)
{
	if (!tcpserial || conn_fd >= 0 || listen_fd < 0) {
		return conn_fd >= 0;
	}
	struct sockaddr_storage ss;
	socklen_t sslen = sizeof ss;
	int fd = accept(listen_fd, (struct sockaddr *)&ss, &sslen);
	if (fd < 0) {
		return false;
	}
	set_nonblock(fd);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	conn_fd = fd;
	telnet_iac = false;
	telnet_skip = 0;
	write_log(_T("SERIAL_TCP: connection accepted\n"));
	return true;
}

static void tcp_disconnect(void)
{
	if (conn_fd >= 0) {
		close_fd(&conn_fd);
		write_log(_T("SERIAL_TCP: disconnect\n"));
	}
}

static bool parse_tcp_spec(const TCHAR *spec, std::string *host, std::string *port, bool *waitmode)
{
	std::string s = spec ? spec : "";
	*waitmode = false;
	if (s.compare(0, 2, "//") == 0) {
		s.erase(0, 2);
	}
	size_t slash = s.find('/');
	if (slash != std::string::npos) {
		std::string opt = s.substr(slash + 1);
		s.erase(slash);
		if (!strcasecmp(opt.c_str(), "wait")) {
			*waitmode = true;
		}
	}
	*host = "127.0.0.1";
	*port = "1234";
	if (s.empty()) {
		return true;
	}
	size_t colon = s.rfind(':');
	if (colon == std::string::npos) {
		bool digits = true;
		for (char c : s) {
			if (!isdigit((unsigned char)c)) {
				digits = false;
				break;
			}
		}
		if (digits) {
			*port = s;
		} else {
			*host = s;
		}
		return true;
	}
	if (colon > 0) {
		*host = s.substr(0, colon);
	}
	if (colon + 1 < s.size()) {
		*port = s.substr(colon + 1);
	}
	return true;
}

static int opentcp(const TCHAR *sername)
{
	std::string host;
	std::string port;
	bool waitmode = false;
	parse_tcp_spec(sername, &host, &port, &waitmode);

	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	struct addrinfo *res = NULL;
	int err = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
	if (err) {
		write_log(_T("SERIAL_TCP: getaddrinfo(%s:%s) failed: %s\n"), host.c_str(), port.c_str(), gai_strerror(err));
		return 0;
	}
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		listen_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (listen_fd < 0) {
			continue;
		}
		int one = 1;
		setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		if (bind(listen_fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(listen_fd, 1) == 0) {
			break;
		}
		close_fd(&listen_fd);
	}
	freeaddrinfo(res);
	if (listen_fd < 0) {
		write_log(_T("SERIAL_TCP: failed to listen on %s:%s: %s\n"), host.c_str(), port.c_str(), strerror(errno));
		return 0;
	}
	set_nonblock(listen_fd);
	tcpserial = true;
	write_log(_T("SERIAL_TCP: listening on %s:%s\n"), host.c_str(), port.c_str());
	while (waitmode && !tcp_accept_pending()) {
		Sleep(1000);
		write_log(_T("SERIAL_TCP: waiting for connect...\n"));
	}
	return 1;
}

int openser(const TCHAR *sername)
{
	if (!_tcsnicmp(sername, _T("TCP://"), 6)) {
		return opentcp(sername + 4);
	}
	if (!_tcsnicmp(sername, _T("TCP:"), 4)) {
		return opentcp(sername + 4);
	}
	serial_fd = open(sername, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (serial_fd < 0) {
		write_log(_T("SERIAL: failed to open '%s': %s\n"), sername, strerror(errno));
		return 0;
	}
	if (!configure_serial_fd(9600)) {
		close_fd(&serial_fd);
		return 0;
	}
	write_log(_T("SERIAL: using %s CTS/RTS=%d\n"), sername, currprefs.serial_hwctsrts);
	return 1;
}

void closeser(void)
{
#ifdef WITH_MIDIEMU
	if (midi_emu) {
		midi_emu_close();
	}
#endif
	if (serial_fd >= 0 && saved_tios_valid) {
		tcsetattr(serial_fd, TCSANOW, &saved_tios);
	}
	close_fd(&serial_fd);
	close_fd(&conn_fd);
	close_fd(&listen_fd);
	tcpserial = false;
	saved_tios_valid = false;
}

static int serial_read_byte(int *out)
{
	if (tcpserial) {
		if (!tcp_accept_pending()) {
			return 0;
		}
		unsigned char c;
		ssize_t got = recv(conn_fd, &c, 1, 0);
		if (got == 1) {
			*out = c;
			return 1;
		}
		if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
			tcp_disconnect();
		}
		return 0;
	}
#ifdef WITH_MIDI
	if (midi_ready) {
		int value = (int)getmidibyte();
		if (value < 0) {
			return 0;
		}
		*out = value;
		return 1;
	}
#endif
	if (serial_fd < 0) {
		return 0;
	}
	unsigned char c;
	ssize_t got = read(serial_fd, &c, 1);
	if (got == 1) {
		*out = c;
		return 1;
	}
	return 0;
}

int readser(int *buffer)
{
	for (;;) {
		int value;
		if (!serial_read_byte(&value)) {
			return 0;
		}
		if (!tcpserial) {
			*buffer = value;
			return 1;
		}
		if (telnet_skip > 0) {
			telnet_skip--;
			continue;
		}
		if (telnet_iac) {
			telnet_iac = false;
			if (value == 255) {
				*buffer = value;
				return 1;
			}
			if (value == 251 || value == 252 || value == 253 || value == 254) {
				telnet_skip = 1;
			}
			continue;
		}
		if (value == 255) {
			telnet_iac = true;
			continue;
		}
		*buffer = value;
		return 1;
	}
}

int readseravail(bool *breakcond)
{
	if (breakcond) {
		*breakcond = false;
	}
	if (tcpserial) {
		if (!tcp_accept_pending()) {
			return 0;
		}
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(conn_fd, &fds);
		struct timeval tv = { 0, 0 };
		int ret = select(conn_fd + 1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
			tcp_disconnect();
			return 0;
		}
		return ret > 0 ? 1 : 0;
	}
#ifdef WITH_MIDI
	if (midi_ready) {
		return ismidibyte();
	}
#endif
	if (serial_fd < 0 || !currprefs.use_serial) {
		return 0;
	}
	int pending = 0;
	if (ioctl(serial_fd, FIONREAD, &pending) == 0 && pending > 0) {
		return pending;
	}
	return 0;
}

void writeser_flush(void)
{
#ifdef WITH_MIDI
	if (midi_ready) {
		return;
	}
#endif
	if (serial_fd >= 0) {
		tcdrain(serial_fd);
	}
}

void writeser(int c)
{
	unsigned char b = (unsigned char)c;
	if (tcpserial) {
		if (tcp_accept_pending() && send(conn_fd, &b, 1, 0) != 1) {
			tcp_disconnect();
		}
		return;
	}
#ifdef WITH_MIDIEMU
	if (midi_emu) {
		uae_u8 outchar = (uae_u8)c;
		midi_emu_parse(&outchar, 1);
		return;
	}
#endif
#ifdef WITH_MIDI
	if (midi_ready) {
		BYTE outchar = (BYTE)c;
		Midi_Parse(midi_output, &outchar);
		return;
	}
#endif
	if (serial_fd >= 0) {
		write(serial_fd, &b, 1);
	}
}

void flushser(void)
{
	int data;
	while (readseravail(NULL) && readser(&data)) {
	}
}

void getserstat(int *pstatus)
{
	int status = 0;
	if (tcpserial) {
		if (tcp_accept_pending()) {
			status = TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
		}
	} else if (serial_fd >= 0) {
		ioctl(serial_fd, TIOCMGET, &status);
	}
	*pstatus = status;
}

void setserstat(int mask, int onoff)
{
	if (serial_fd < 0) {
		return;
	}
	int status = 0;
	if (ioctl(serial_fd, TIOCMGET, &status) < 0) {
		return;
	}
	if (onoff) {
		status |= mask;
	} else {
		status &= ~mask;
	}
	ioctl(serial_fd, TIOCMSET, &status);
}

int setbaud(int baud, int org_baud)
{
#ifdef WITH_MIDI
	if (org_baud == 31400 && currprefs.win32_midioutdev >= -1) {
#ifdef WITH_MIDIEMU
		if (currprefs.win32_midioutdev >= 0) {
			const TCHAR *name = unix_midi_output_device_config_name_for_id(currprefs.win32_midioutdev);
			if (!_tcsncmp(name, _T("Munt "), 5)) {
				midi_emu_open(name);
				return 1;
			}
		}
#endif
		if (!midi_ready && Midi_Open()) {
			write_log(_T("Midi enabled\n"));
		}
		return 1;
	}
	if (midi_ready) {
		Midi_Close();
	}
#endif
#ifdef WITH_MIDIEMU
	if (midi_emu) {
		midi_emu_close();
	}
#endif
	if (serial_fd < 0) {
		return currprefs.use_serial ? 0 : 1;
	}
	return configure_serial_fd(baud) ? 1 : 0;
}

static void serial_rx_irq(void)
{
	rx_full = true;
	rx_irq = true;
	INTREQ_INT(11, 0);
}

static void checkreceive_serial(void)
{
	if (rx_full) {
		return;
	}
	int recdata;
	if (!readseravail(NULL) || !readser(&recdata)) {
		return;
	}
	if (currprefs.serial_crlf) {
		static int previous = -1;
		if (recdata == 0 || (previous == 13 && recdata == 10)) {
			previous = -1;
			return;
		}
		previous = recdata;
	}
	serdatr = (uae_u16)((recdata & 0xff) | 0x0100);
	serial_rx_irq();
}

void SERPER(uae_u16 w)
{
	if (serper == w) {
		return;
	}
	serper = w;
	const int baud = serial_period_to_baud(w);
	if (baud > 0) {
		setbaud(baud == 31400 ? 38400 : baud, baud);
	}
}

uae_u16 SERDATR(void)
{
	uae_u16 v = serdatr & 0x03ff;
	v |= 0x2000 | 0x1000 | 0x0800;
	if (rx_full) {
		v |= 0x4000;
	}
	if (ovrun) {
		v |= 0x8000;
	}
	rx_full = false;
	if (!rx_irq) {
		INTREQ_INT(11, 0);
	}
	return v;
}

void SERDAT(uae_u16 w)
{
	serdat = w;
	if (!serdev) {
		return;
	}
	const int c = w & 0xff;
	if (w & 0x100) {
		writeser(((w >> 8) & 1) | 0xa8);
	}
	if (currprefs.serial_crlf && c == 10 && serial_send_previous != 13) {
		writeser(13);
	}
	writeser(c);
	serial_send_previous = (uae_u8)c;
}

void serial_rbf_change(bool set)
{
	ovrun = set;
	if (!set) {
		rx_irq = false;
	}
}

void serial_dtr_on(void)
{
	dtr = true;
	if (currprefs.serial_demand) {
		serial_open();
	}
	setserstat(TIOCM_DTR, 1);
}

void serial_dtr_off(void)
{
	dtr = false;
	setserstat(TIOCM_DTR, 0);
	if (currprefs.serial_demand) {
		serial_close();
	}
}

uae_u8 serial_readstatus(uae_u8 v, uae_u8)
{
	int status = 0;
	uae_u8 serbits = oldserbits;

	if (serloop_enabled) {
		status = TIOCM_DSR | TIOCM_CAR | TIOCM_CTS;
	} else if (currprefs.use_serial) {
		getserstat(&status);
	} else {
		return v;
	}

	if (currprefs.serial_rtsctsdtrdtecd) {
		if (status & TIOCM_CAR) {
			serbits &= ~0x20;
		} else {
			serbits |= 0x20;
		}
		if (status & TIOCM_DSR) {
			serbits &= ~0x08;
		} else {
			serbits |= 0x08;
		}
		if (status & TIOCM_CTS) {
			serbits &= ~0x10;
		} else {
			serbits |= 0x10;
		}
	}
	if (currprefs.serial_ri) {
		if (status & TIOCM_RI) {
			serbits &= ~0x04;
		} else {
			serbits |= 0x04;
		}
	} else {
		serbits &= ~0x04;
		serbits |= v & 0x04;
	}

	serbits &= 0x04 | 0x08 | 0x10 | 0x20;
	oldserbits &= ~(0x04 | 0x08 | 0x10 | 0x20);
	oldserbits |= serbits;
	return (v & (0x80 | 0x40 | 0x02 | 0x01)) | serbits;
}

uae_u8 serial_writestatus(uae_u8 newstate, uae_u8 dir)
{
	if (currprefs.use_serial) {
		if (currprefs.serial_rtsctsdtrdtecd && ((oldserbits ^ newstate) & 0x80) && (dir & 0x80)) {
			if (newstate & 0x80) {
				serial_dtr_off();
			} else {
				serial_dtr_on();
			}
		}
		if (!currprefs.serial_hwctsrts && currprefs.serial_rtsctsdtrdtecd && ((oldserbits ^ newstate) & 0x40) && (dir & 0x40)) {
			setserstat(TIOCM_RTS, (newstate & 0x40) ? 0 : 1);
		}
	}
	oldserbits &= ~(0x80 | 0x40);
	oldserbits |= newstate & (0x80 | 0x40);
	return oldserbits;
}

void serial_flush_buffer(void)
{
	writeser_flush();
}

void serial_rethink(void)
{
	checkreceive_serial();
}

void serial_hsynchandler(void)
{
	checkreceive_serial();
}

void serial_uartbreak(int v)
{
	if (serial_fd < 0) {
		return;
	}
	if (v) {
		tcsendbreak(serial_fd, 0);
	}
}

void serial_open(void)
{
	if (serdev) {
		return;
	}
	serper = 0;
	if (!_tcsicmp(currprefs.sername, SERIAL_LOOPBACK)) {
		serloop_enabled = true;
	} else if (!currprefs.sername[0]) {
		serempty_enabled = true;
	} else if (!openser(currprefs.sername)) {
		write_log(_T("SERIAL: Could not open device %s\n"), currprefs.sername);
		return;
	}
	serdev = 1;
	serdatr = 0x0100;
}

void serial_close(void)
{
	closeser();
	serdev = 0;
	serloop_enabled = false;
	serempty_enabled = false;
	rx_full = false;
	rx_irq = false;
	ovrun = false;
}

void serial_init(void)
{
	if (!currprefs.use_serial) {
		return;
	}
	if (!currprefs.serial_demand) {
		serial_open();
	}
	serdatr = 0x0100;
}

void serial_exit(void)
{
	serial_close();
	dtr = false;
	oldserbits = 0;
	serdat = 0;
	serdatr = 0x0100;
}

void enet_writeser(uae_u16)
{
}

int enet_readseravail(void)
{
	return 0;
}

int enet_readser(uae_u16 *)
{
	return 0;
}

int enet_open(TCHAR *)
{
	return 0;
}

void enet_close(void)
{
}

#endif
