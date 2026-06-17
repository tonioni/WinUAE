#include "sysconfig.h"
#include "sysdeps.h"

#ifdef UAESERIAL

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "options.h"
#include "serial.h"
#include "threaddep/thread.h"
#include "uaeserial_unix.h"
#include "uae.h"

struct uaeserialdataunix
{
	int fd;
	int listen_fd;
	int conn_fd;
	bool tcpserial;
	bool telnet_iac;
	int telnet_skip;
	bool saved_tios_valid;
	struct termios saved_tios;
	volatile int threadactive;
	uae_sem_t sync_sem;
	void *user;
	int unit;
	pthread_mutex_t lock;
	bool lock_valid;
	uae_u8 rxbuf[8192];
	int rxbuf_len;
};

static void close_fd(int *fd)
{
	if (*fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

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

static bool tcp_accept_pending(struct uaeserialdataunix *sd)
{
	if (!sd->tcpserial || sd->conn_fd >= 0 || sd->listen_fd < 0) {
		return sd->conn_fd >= 0;
	}
	struct sockaddr_storage ss;
	socklen_t sslen = sizeof ss;
	int fd = accept(sd->listen_fd, (struct sockaddr *)&ss, &sslen);
	if (fd < 0) {
		return false;
	}
	set_nonblock(fd);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	sd->conn_fd = fd;
	sd->telnet_iac = false;
	sd->telnet_skip = 0;
	write_log(_T("UAESER_TCP: connection accepted\n"));
	return true;
}

static void tcp_disconnect(struct uaeserialdataunix *sd)
{
	if (sd->conn_fd >= 0) {
		close_fd(&sd->conn_fd);
		write_log(_T("UAESER_TCP: disconnect\n"));
	}
}

static int opentcp(struct uaeserialdataunix *sd, const TCHAR *sername)
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
		write_log(_T("UAESER_TCP: getaddrinfo(%s:%s) failed: %s\n"), host.c_str(), port.c_str(), gai_strerror(err));
		return 0;
	}
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		sd->listen_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sd->listen_fd < 0) {
			continue;
		}
		int one = 1;
		setsockopt(sd->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		if (bind(sd->listen_fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(sd->listen_fd, 1) == 0) {
			break;
		}
		close_fd(&sd->listen_fd);
	}
	freeaddrinfo(res);
	if (sd->listen_fd < 0) {
		write_log(_T("UAESER_TCP: failed to listen on %s:%s: %s\n"), host.c_str(), port.c_str(), strerror(errno));
		return 0;
	}
	set_nonblock(sd->listen_fd);
	sd->tcpserial = true;
	write_log(_T("UAESER_TCP: listening on %s:%s\n"), host.c_str(), port.c_str());
	while (waitmode && !tcp_accept_pending(sd)) {
		Sleep(1000);
		write_log(_T("UAESER_TCP: waiting for connect...\n"));
	}
	return 1;
}

static int configure_serial_fd(struct uaeserialdataunix *sd, int baud, int bits, int sbits, int rtscts, int parity, uae_u32 xonxoff)
{
	if (sd->tcpserial || sd->fd < 0) {
		return 0;
	}
	struct termios tios;
	if (tcgetattr(sd->fd, &tios) < 0) {
		write_log(_T("UAESER: tcgetattr failed: %s\n"), strerror(errno));
		return 5;
	}
	if (!sd->saved_tios_valid) {
		sd->saved_tios = tios;
		sd->saved_tios_valid = true;
	}
	cfmakeraw(&tios);
	speed_t speed = baud_to_speed(baud > 0 ? baud : 9600);
	cfsetispeed(&tios, speed);
	cfsetospeed(&tios, speed);
	tios.c_cflag |= CLOCAL | CREAD;
	tios.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	tios.c_cflag |= bits == 7 ? CS7 : CS8;
	if (sbits == 2) {
		tios.c_cflag |= CSTOPB;
	}
	if (parity == 1 || parity == 2) {
		tios.c_cflag |= PARENB;
		if (parity == 1) {
			tios.c_cflag |= PARODD;
		}
	} else if (parity == 3 || parity == 4) {
#ifdef CMSPAR
		tios.c_cflag |= PARENB | CMSPAR;
		if (parity == 3) {
			tios.c_cflag |= PARODD;
		}
#else
		write_log(_T("UAESER: mark/space parity is not supported by this host termios\n"));
		return 5;
#endif
	}
#ifdef CRTSCTS
	if (rtscts) {
		tios.c_cflag |= CRTSCTS;
	} else {
		tios.c_cflag &= ~CRTSCTS;
	}
#endif
	if (xonxoff & 1) {
		tios.c_iflag |= IXON | IXOFF;
		tios.c_cc[VSTART] = (xonxoff >> 8) & 0xff;
		tios.c_cc[VSTOP] = (xonxoff >> 16) & 0xff;
	} else {
		tios.c_iflag &= ~(IXON | IXOFF);
	}
	if (tcsetattr(sd->fd, TCSANOW, &tios) < 0) {
		write_log(_T("UAESER: tcsetattr failed: %s\n"), strerror(errno));
		return 5;
	}
	return 0;
}

static void append_rx_byte(struct uaeserialdataunix *sd, uae_u8 v)
{
	if (sd->rxbuf_len < (int)sizeof(sd->rxbuf)) {
		sd->rxbuf[sd->rxbuf_len++] = v;
	}
}

static void poll_host_bytes(struct uaeserialdataunix *sd)
{
	if (sd->tcpserial) {
		if (!tcp_accept_pending(sd)) {
			return;
		}
		while (sd->rxbuf_len < (int)sizeof(sd->rxbuf)) {
			unsigned char c;
			ssize_t got = recv(sd->conn_fd, &c, 1, 0);
			if (got != 1) {
				if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
					tcp_disconnect(sd);
				}
				return;
			}
			if (sd->telnet_skip > 0) {
				sd->telnet_skip--;
				continue;
			}
			if (sd->telnet_iac) {
				sd->telnet_iac = false;
				if (c == 255) {
					append_rx_byte(sd, c);
					continue;
				}
				if (c == 251 || c == 252 || c == 253 || c == 254) {
					sd->telnet_skip = 1;
				}
				continue;
			}
			if (c == 255) {
				sd->telnet_iac = true;
				continue;
			}
			append_rx_byte(sd, c);
		}
		return;
	}
	if (sd->fd < 0) {
		return;
	}
	while (sd->rxbuf_len < (int)sizeof(sd->rxbuf)) {
		uae_u8 c;
		ssize_t got = read(sd->fd, &c, 1);
		if (got == 1) {
			append_rx_byte(sd, c);
			continue;
		}
		return;
	}
}

static int uaeser_pending(struct uaeserialdataunix *sd)
{
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	poll_host_bytes(sd);
	int pending = sd->rxbuf_len;
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
	}
	return pending;
}

static void uaeser_thread(void *arg)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)arg;
	uae_set_thread_priority(NULL, 1);
	sd->threadactive = 1;
	uae_sem_post(&sd->sync_sem);
	while (sd->threadactive == 1) {
		int sigmask = 2;
		if (uaeser_pending(sd) > 0) {
			sigmask |= 1;
		}
		uaeser_signal(sd->user, sigmask);
		Sleep(10);
	}
	sd->threadactive = 0;
}

int uaeser_getdatalength(void)
{
	return sizeof(struct uaeserialdataunix);
}

int uaeser_open(void *vsd, void *user, int unit)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	const TCHAR *sername = unix_uaeserial_get_port(unit);
	memset(sd, 0, sizeof(*sd));
	sd->fd = -1;
	sd->listen_fd = -1;
	sd->conn_fd = -1;
	sd->user = user;
	sd->unit = unit;
	pthread_mutex_init(&sd->lock, NULL);
	sd->lock_valid = true;

	if (!sername[0] && unit == 0) {
		sername = currprefs.sername;
	}
	if (!sername[0] || !_tcsicmp(sername, _T("none"))) {
		write_log(_T("UAESER: no Unix host port configured for uaeserial.device unit %d\n"), unit);
		pthread_mutex_destroy(&sd->lock);
		sd->lock_valid = false;
		return 0;
	}
	if (!_tcsnicmp(sername, _T("TCP://"), 6)) {
		if (!opentcp(sd, sername + 4)) {
			uaeser_close(sd);
			return 0;
		}
	} else if (!_tcsnicmp(sername, _T("TCP:"), 4)) {
		if (!opentcp(sd, sername + 4)) {
			uaeser_close(sd);
			return 0;
		}
	} else {
		sd->fd = open(sername, O_RDWR | O_NOCTTY | O_NONBLOCK);
		if (sd->fd < 0) {
			write_log(_T("UAESER: failed to open '%s': %s\n"), sername, strerror(errno));
			uaeser_close(sd);
			return 0;
		}
		if (configure_serial_fd(sd, 9600, 8, 1, currprefs.serial_hwctsrts, 0, 0)) {
			uaeser_close(sd);
			return 0;
		}
		write_log(_T("UAESER: using %s for uaeserial.device unit %d\n"), sername, unit);
	}
	uae_sem_init(&sd->sync_sem, 0, 0);
	uae_start_thread(_T("uaeserial_unix"), uaeser_thread, sd, NULL);
	uae_sem_wait(&sd->sync_sem);
	return 1;
}

void uaeser_close(void *vsd)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (sd->threadactive) {
		sd->threadactive = -1;
		while (sd->threadactive) {
			Sleep(10);
		}
	}
	if (sd->fd >= 0 && sd->saved_tios_valid) {
		tcsetattr(sd->fd, TCSANOW, &sd->saved_tios);
	}
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	close_fd(&sd->fd);
	close_fd(&sd->conn_fd);
	close_fd(&sd->listen_fd);
	sd->tcpserial = false;
	sd->saved_tios_valid = false;
	sd->user = NULL;
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
		pthread_mutex_destroy(&sd->lock);
		sd->lock_valid = false;
	}
}

int uaeser_query(void *vsd, uae_u16 *status, uae_u32 *pending)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (pending) {
		*pending = uaeser_pending(sd);
	}
	if (status) {
		int modem = 0;
		uae_u16 s = 0;
		if (sd->tcpserial) {
			if (tcp_accept_pending(sd)) {
				modem = TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
			}
		} else if (sd->fd >= 0) {
			ioctl(sd->fd, TIOCMGET, &modem);
		}
		s |= (modem & TIOCM_CTS) ? 0 : (1 << 4);
		s |= (modem & TIOCM_DSR) ? 0 : (1 << 7);
		s |= (modem & TIOCM_RI) ? (1 << 2) : 0;
		*status = s;
	}
	return 1;
}

int uaeser_read(void *vsd, uae_u8 *data, uae_u32 len)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (len == 0) {
		return 1;
	}
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	poll_host_bytes(sd);
	if (sd->rxbuf_len < (int)len) {
		if (sd->lock_valid) {
			pthread_mutex_unlock(&sd->lock);
		}
		return 0;
	}
	memcpy(data, sd->rxbuf, len);
	sd->rxbuf_len -= len;
	if (sd->rxbuf_len > 0) {
		memmove(sd->rxbuf, sd->rxbuf + len, sd->rxbuf_len);
	}
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
	}
	return 1;
}

int uaeser_write(void *vsd, uae_u8 *data, uae_u32 len)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (len == 0) {
		return 1;
	}
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	int fd = sd->fd;
	if (sd->tcpserial) {
		if (!tcp_accept_pending(sd)) {
			if (sd->lock_valid) {
				pthread_mutex_unlock(&sd->lock);
			}
			return 0;
		}
		fd = sd->conn_fd;
	}
	if (fd < 0) {
		if (sd->lock_valid) {
			pthread_mutex_unlock(&sd->lock);
		}
		return 0;
	}
	uae_u32 done = 0;
	while (done < len) {
		ssize_t written = sd->tcpserial
			? send(fd, data + done, len - done, 0)
			: write(fd, data + done, len - done);
		if (written > 0) {
			done += (uae_u32)written;
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			fd_set wfds;
			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			struct timeval tv = { 0, 10000 };
			if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
				continue;
			}
		}
		if (sd->tcpserial) {
			tcp_disconnect(sd);
		}
		if (sd->lock_valid) {
			pthread_mutex_unlock(&sd->lock);
		}
		return done > 0 ? 1 : 0;
	}
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
	}
	return 1;
}

int uaeser_setparams(void *vsd, int baud, int, int bits, int sbits, int rtscts, int parity, uae_u32 xonxoff)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	int ret = configure_serial_fd(sd, baud, bits, sbits, rtscts, parity, xonxoff);
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
	}
	return ret;
}

int uaeser_break(void *vsd, int)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	if (sd->fd >= 0) {
		tcsendbreak(sd->fd, 0);
	}
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
	}
	return 1;
}

void uaeser_trigger(void *)
{
}

void uaeser_clearbuffers(void *vsd)
{
	struct uaeserialdataunix *sd = (struct uaeserialdataunix*)vsd;
	if (sd->lock_valid) {
		pthread_mutex_lock(&sd->lock);
	}
	if (sd->fd >= 0) {
		tcflush(sd->fd, TCIOFLUSH);
	}
	sd->rxbuf_len = 0;
	if (sd->lock_valid) {
		pthread_mutex_unlock(&sd->lock);
	}
}

#endif
