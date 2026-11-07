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
#include "serial_helpers.h"
#include "serial_host.h"
#include "uae.h"
#ifdef WITH_MIDI
#include "midi.h"
#endif
#ifdef WITH_MIDIEMU
#include "midiemu.h"
#endif

static int serial_fd = -1;
static int listen_fd = -1;
static int conn_fd = -1;
static bool tcpserial;
static uae_u8 output_buffer[256];
static int output_size;
static bool telnet_iac;
static int telnet_skip;
static struct termios saved_tios;
static bool saved_tios_valid;

static void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}
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
	speed_t speed = baud == 110 ? B110 : baud == 600 ? B600
		: serial_baud_to_speed(baud > 0 ? baud : 9600);
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

static int opentcp(const TCHAR *sername)
{
	std::string host;
	std::string port;
	bool waitmode = false;
	serial_parse_tcp_spec(sername, &host, &port, &waitmode);

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
	output_size = 0;
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

static void flush_serial_output(void)
{
	int fd = tcpserial ? (tcp_accept_pending() ? conn_fd : -1) : serial_fd;
	if (fd < 0) {
		output_size = 0;
		return;
	}
	while (output_size > 0) {
		ssize_t n = tcpserial ? send(fd, output_buffer, output_size, 0)
			: write(fd, output_buffer, output_size);
		if (n > 0) {
			output_size -= (int)n;
			memmove(output_buffer, output_buffer + n, output_size);
		} else if (n < 0 && errno == EINTR) {
			continue;
		} else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return;
		} else {
			if (tcpserial) {
				tcp_disconnect();
			}
			output_size = 0;
			return;
		}
	}
}

void host_serial_hsync(void)
{
	flush_serial_output();
}

void writeser_flush(void)
{
	flush_serial_output();
}

void writeser(int c)
{
	if (!tcpserial) {
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
	}
	flush_serial_output();
	if (output_size < (int)sizeof(output_buffer)) {
		output_buffer[output_size++] = (uae_u8)c;
	}
	flush_serial_output();
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
	*pstatus = ((status & TIOCM_CAR) ? SERIAL_HOST_CAR : 0)
		| ((status & TIOCM_DSR) ? SERIAL_HOST_DSR : 0)
		| ((status & TIOCM_RI) ? SERIAL_HOST_RI : 0)
		| ((status & TIOCM_CTS) ? SERIAL_HOST_CTS : 0);
}

void setserstat(int mask, int onoff)
{
	mask = ((mask & SERIAL_HOST_DTR) ? TIOCM_DTR : 0)
		| ((mask & SERIAL_HOST_RTS) ? TIOCM_RTS : 0);
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

int checkserwrite(int spaceneeded)
{
	flush_serial_output();
	return spaceneeded >= 0 && spaceneeded <= (int)sizeof(output_buffer) - output_size;
}

void serialuartbreak(int enabled)
{
	if (serial_fd >= 0) {
		ioctl(serial_fd, enabled ? TIOCSBRK : TIOCCBRK);
	}
}

bool host_serial_map_available(void)
{
	return false;
}

void host_serial_map_send(uae_u32)
{
}

uae_u32 host_serial_map_receive(void)
{
	return 0xffffffff;
}

void host_serial_map_deactivate(void)
{
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
