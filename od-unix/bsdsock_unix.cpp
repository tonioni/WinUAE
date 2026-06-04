/*
 * UAE - The Un*x Amiga Emulator
 *
 * bsdsocket.library emulation - Unix
 *
 * Copyright 2000-2001 Carl Drougge <carl.drougge@home.se> <bearded@longhaired.org>
 * Copyright 2003-2005 Richard Drummond
 * Copyright 2004      Jeff Shepherd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include <atomic>

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "autoconf.h"
#include "traps.h"
#include "threaddep/thread.h"
#include "native2amiga.h"
#include "bsdsocket.h"
#include "uae.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <csignal>
#include <arpa/inet.h>
#include <unistd.h>

#if defined(__HAIKU__)
/* Haiku does not define these obsolete/rare constants */
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 44
#endif
#ifndef ETOOMANYREFS
#define ETOOMANYREFS    59
#endif
#ifndef IPPROTO_EGP
#define IPPROTO_EGP     8
#endif
#ifndef IPPROTO_PUP
#define IPPROTO_PUP     12
#endif
#ifndef IPPROTO_IDP
#define IPPROTO_IDP     22
#endif
#ifndef IPPROTO_ENCAP
#define IPPROTO_ENCAP   98
#endif
#ifndef SIOCGIFCONF
#include <sys/sockio.h>
#endif
#endif /* __HAIKU__ */

#include <cstddef>
#include <cstring>
#include <vector>
#include <mutex>
static std::mutex bsdsock_mutex;

#define close_socket close
#define close_pipe(fd) close(fd)
#define write_pipe(fd, buf, len) write(fd, buf, len)
#define read_pipe(fd, buf, len) read(fd, buf, len)

#define WAITSIGNAL  waitsig (ctx, sb)

/* Sigqueue is unsafe on SMP machines.
 * Temporary work-around.
 */
#define SETSIGNAL \
	do { \
	uae_Signal (sb->ownertask, sb->sigstosend | ((uae_u32) 1) << sb->signal); \
	sb->dosignal = 1; \
	} while (0)


#define SETERRNO    bsdsocklib_seterrno (ctx, sb,mapErrno (errno))
#define SETHERRNO   bsdsocklib_setherrno (ctx, sb, h_errno)


/* BSD-systems don't seem to have MSG_NOSIGNAL..
   @@@ We need to catch SIGPIPE on those systems! (?) */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define S_GL_result(res) sb->resultval = (res)

uae_u32 bsdthr_Accept_2 (SB);
uae_u32 bsdthr_Recv_2 (SB);
uae_u32 bsdthr_blockingstuff (uae_u32 (*tryfunc)(SB), SB);
uae_u32 bsdthr_SendRecvAcceptConnect (uae_u32 (*tryfunc)(SB), SB);
uae_u32 bsdthr_Send_2 (SB);
uae_u32 bsdthr_Connect_2 (SB);
uae_u32 bsdthr_WaitSelect (SB);
uae_u32 bsdthr_Wait (SB);
void clearsockabort (SB);

static uae_sem_t sem_queue;

/**
 ** Socket Event Monitoring System
 ** Monitors sockets with SO_EVENTMASK set and posts Amiga signals when events occur
 **/

// Entry for a socket being monitored for events
struct socket_event_entry {
	struct socketbase* sb;
	int sd;                    // Amiga socket descriptor (0-based)
	SOCKET_TYPE s;             // Host socket
	int eventmask;             // REP_* flags to monitor
	bool connecting;           // True if connect() is in progress
	bool connected;            // True if socket is connected (or connectionless/listener)
	int fired_mask;            // Events that have fired and need re-enabling
};

// Event monitor thread state
struct event_monitor {
	uae_thread_id thread;      // Monitor thread
	std::mutex mutex;          // Protects socket_list
	int wake_pipe[2];          // Pipe to wake thread on changes
	std::atomic<bool> running; // Thread running flag
	std::vector<socket_event_entry> socket_list;  // Sockets to monitor
};

static struct event_monitor* g_event_monitor = nullptr;

/**
 ** Helper functions
 **/

/*
 * Map host errno to amiga errno
 */
static int mapErrno (int e)
{
	switch (e) {
	case EINTR:     e = 4;  break;
	case EDEADLK:       e = 11; break;
	case EAGAIN:        e = 35; break;
	case EINPROGRESS:   e = 36; break;
	case EALREADY:      e = 37; break;
	case ENOTSOCK:      e = 38; break;
	case EDESTADDRREQ:  e = 39; break;
	case EMSGSIZE:      e = 40; break;
	case EPROTOTYPE:    e = 41; break;
	case ENOPROTOOPT:   e = 42; break;
	case EPROTONOSUPPORT:   e = 43; break;
	case ESOCKTNOSUPPORT:   e = 44; break;
	case EOPNOTSUPP:    e = 45; break;
	case EPFNOSUPPORT:  e = 46; break;
	case EAFNOSUPPORT:  e = 47; break;
	case EADDRINUSE:    e = 48; break;
	case EADDRNOTAVAIL: e = 49; break;
	case ENETDOWN:      e = 50; break;
	case ENETUNREACH:   e = 51; break;
	case ENETRESET:     e = 52; break;
	case ECONNABORTED:  e = 53; break;
	case ECONNRESET:    e = 54; break;
	case ENOBUFS:       e = 55; break;
	case EISCONN:       e = 56; break;
	case ENOTCONN:      e = 57; break;
	case ESHUTDOWN:     e = 58; break;
	case ETOOMANYREFS:  e = 59; break;
	case ETIMEDOUT:     e = 60; break;
	case ECONNREFUSED:  e = 61; break;
	case ELOOP:     e = 62; break;
	case ENAMETOOLONG:  e = 63; break;
	default: break;
	}
	return e;
}

/*
 * Map amiga (s|g)etsockopt level into native one
 */
static int mapsockoptlevel (int level)
{
	switch (level) {
	case 0xffff:
		return SOL_SOCKET;
	case 0:
		return IPPROTO_IP;
	case 1:
		return IPPROTO_ICMP;
	case 2:
		return IPPROTO_IGMP;
#ifdef IPPROTO_IPIP
	case 4:
		return IPPROTO_IPIP;
#endif
	case 6:
		return IPPROTO_TCP;
	case 8:
		return IPPROTO_EGP;
	case 12:
		return IPPROTO_PUP;
	case 17:
		return IPPROTO_UDP;
	case 22:
		return IPPROTO_IDP;
#ifdef IPPROTO_TP
	case 29:
		return IPPROTO_TP;
#endif
	case 98:
		return IPPROTO_ENCAP;
	default:
		write_log ("Unknown sockopt level %d\n", level);
		return level;
	}
}

/*
 * Map amiga (s|g)etsockopt optname into native one
 */
static int mapsockoptname (int level, int optname)
{
	switch (level) {

	case SOL_SOCKET:
		switch (optname) {
		case 0x0001:
			return SO_DEBUG;
		case 0x0002:
			return SO_ACCEPTCONN;
		case 0x0004:
			return SO_REUSEADDR;
		case 0x0008:
			return SO_KEEPALIVE;
		case 0x0010:
			return SO_DONTROUTE;
		case 0x0020:
			return SO_BROADCAST;
#ifdef SO_USELOOPBACK
		case 0x0040:
			return SO_USELOOPBACK;
#endif
		case 0x0080:
			return SO_LINGER;
		case 0x0100:
			return SO_OOBINLINE;
#ifdef SO_REUSEPORT
		case 0x0200:
			return SO_REUSEPORT;
#endif
		case 0x1001:
			return SO_SNDBUF;
		case 0x1002:
			return SO_RCVBUF;
		case 0x1003:
			return SO_SNDLOWAT;
		case 0x1004:
			return SO_RCVLOWAT;
		case 0x1005:
			return SO_SNDTIMEO;
		case 0x1006:
			return SO_RCVTIMEO;
		case 0x1007:
			return SO_ERROR;
		case 0x1008:
			return SO_TYPE;

		default:
			write_log("Invalid setsockopt option 0x%x for level %d\n",
					  optname, level);
			return -1;
		}
		break;

	case IPPROTO_IP:
		switch (optname) {
		case 1:
			return IP_OPTIONS;
		case 2:
			return IP_HDRINCL;
		case 3:
			return IP_TOS;
		case 4:
			return IP_TTL;
		case 5:
			return IP_RECVOPTS;
		case 6:
			return IP_RECVRETOPTS;
		case 8:
			return IP_RETOPTS;
		case 9:
			return IP_MULTICAST_IF;
		case 10:
			return IP_MULTICAST_TTL;
		case 11:
			return IP_MULTICAST_LOOP;
		case 12:
			return IP_ADD_MEMBERSHIP;

		default:
			write_log("Invalid setsockopt option 0x%x for level %d\n",
					  optname, level);
			return -1;
		}
		break;

	case IPPROTO_TCP:
		switch (optname) {
		case 1:
			return TCP_NODELAY;
		case 2:
			return TCP_MAXSEG;

		default:
			write_log("Invalid setsockopt option 0x%x for level %d\n",
					  optname, level);
			return -1;
		}
		break;

	default:
		write_log("Unknown level %d\n", level);
		return -1;
	}
}

/*
 * Map amiga (s|g)etsockopt return value into the correct form
 */
static void mapsockoptreturn(int level, int optname, uae_u32 optval, void *buf)
{
	switch (level) {

	case SOL_SOCKET:
		switch (optname) {
		case SO_DEBUG:
		case SO_ACCEPTCONN:
		case SO_REUSEADDR:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_BROADCAST:
#ifdef SO_USELOOPBACK
		case SO_USELOOPBACK:
#endif
		case SO_LINGER:
		case SO_OOBINLINE:
#ifdef SO_REUSEPORT
		case SO_REUSEPORT:
#endif
		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		case SO_TYPE:
			put_long (optval, *(int *)buf);
			break;

		case SO_ERROR:
			write_log("New errno is %d\n", mapErrno(*(int *)buf));
			put_long (optval, mapErrno(*(int *)buf));
			break;
		default:
			break;
		}
		break;

	case IPPROTO_IP:
		switch (optname) {
		case IP_OPTIONS:
		case IP_HDRINCL:
		case IP_TOS:
		case IP_TTL:
		case IP_RECVOPTS:
		//case IP_RECVRETOPTS:
		//case IP_RETOPTS:
		case IP_MULTICAST_IF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
			put_long (optval, *(int *)buf);
			break;

		default:
			break;
		}
		break;

	case IPPROTO_TCP:
		switch (optname) {
		case TCP_NODELAY:
		case TCP_MAXSEG:
			put_long (optval,*(int *)buf);
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}
}

/*
 * Map amiga (s|g)etsockopt value from amiga to the appropriate value
 */
static void mapsockoptvalue(int level, int optname, uae_u32 optval, void *buf)
{
	switch (level) {

	case SOL_SOCKET:
		switch (optname) {
		case SO_DEBUG:
		case SO_ACCEPTCONN:
		case SO_REUSEADDR:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_BROADCAST:
#ifdef SO_USELOOPBACK
		case SO_USELOOPBACK:
#endif
		case SO_LINGER:
		case SO_OOBINLINE:
#ifdef SO_REUSEPORT
		case SO_REUSEPORT:
#endif
		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		case SO_TYPE:
		case SO_ERROR:
			*((int *)buf) = get_long (optval);
			break;
		default:
			break;
		}
		break;

	case IPPROTO_IP:
		switch (optname) {
		case IP_OPTIONS:
		case IP_HDRINCL:
		case IP_TOS:
		case IP_TTL:
		case IP_RECVOPTS:
		//case IP_RECVRETOPTS:
		//case IP_RETOPTS:
		case IP_MULTICAST_IF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
			*((int *)buf) = get_long (optval);
			break;

		default:
			break;
		}
		break;

	case IPPROTO_TCP:
		switch (optname) {
		case TCP_NODELAY:
		case TCP_MAXSEG:
			*((int *)buf) = get_long (optval);
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}
}

STATIC_INLINE int bsd_amigaside_FD_ISSET (int n, uae_u32 set)
{
	uae_u32 foo = get_long (set + (n / 32));
	if (foo & (1 << (n % 32)))
		return 1;
	return 0;
}

STATIC_INLINE void bsd_amigaside_FD_ZERO (uae_u32 set, int nfds)
{
	unsigned int i;
	for (i = 0; i < (unsigned int)nfds; i += 32, set += 4)
		put_long (set, 0);
}

STATIC_INLINE void bsd_amigaside_FD_SET (int n, uae_u32 set)
{
	set = set + (n / 32);
	put_long (set, get_long (set) | (1 << (n % 32)));
}

static void printSockAddr(struct sockaddr_in* in)
{
	write_log("Family %d, ", in->sin_family);
	write_log("Port %d,", ntohs(in->sin_port));
	write_log("Address %s,", inet_ntoa(in->sin_addr));
}

/**
 ** Socket Event Monitoring Functions
 **/

// Post an Amiga signal when a socket event occurs
static void post_socket_event(struct socketbase* sb, int sd, int event_type)
{
	if (!sb || sd < 0) return;

	// Verify socket still has an active event mask: race with SO_EVENTMASK=0
	if (!(sb->ftable[sd] & REP_ALL)) return;

	// Set the appropriate SET_* flag in ftable
	sb->ftable[sd] |= (event_type << 8);

	// Signal the Amiga task
	addtosigqueue(sb, 1);
}

static int peek_socket(int s)
{
	char buf[1];
	int res;
	// Peek 1 byte without waiting
	res = recv(s, buf, 1, MSG_PEEK | MSG_DONTWAIT);
	if (res > 0) return 2; // Data available
	if (res == 0) return 1; // EOF (Ready to read 0 bytes)
	return 0; // Error/Spurious/WouldBlock
}

// Event monitor thread - monitors sockets and posts signals
static void event_monitor_thread(void* data)
{
	struct event_monitor* monitor = (struct event_monitor*)data;

	write_log("BSDSOCK: Event monitor thread started\n");

	while (monitor->running) {
		fd_set readfds, writefds, exceptfds;
		int maxfd = monitor->wake_pipe[0];
		struct timeval timeout;

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		// Always monitor wake pipe
		FD_SET(monitor->wake_pipe[0], &readfds);

		// Lock mutex to build fd_sets from socket list
		monitor->mutex.lock();

		if (!monitor->socket_list.empty()) {
			BSDTRACE((_T("BSDSOCK: Event monitor checking %d sockets\n"), (int)monitor->socket_list.size()));
		}

		for (const auto& entry : monitor->socket_list) {
			if (entry.s == INVALID_SOCKET) continue;

			// Skip sockets with no events to monitor
			if (entry.eventmask == 0) continue;

			if (entry.s > maxfd) {
				maxfd = entry.s;
			}

			// Add socket to appropriate fd_sets based on event mask
			// Filter out events that have already fired (one-shot) for default handling
			int active_mask = entry.eventmask & ~entry.fired_mask;

			// Use active_mask to respect One-Shot behavior (Wait for re-enablement via recv/accept)
			if (active_mask & (REP_READ | REP_ACCEPT)) {
				if (active_mask & REP_READ) {
					// Prevent premature monitoring of READ on connecting/disconnected sockets
					if (!entry.connecting && entry.connected) {
						FD_SET(entry.s, &readfds);
					}
				} else {
					// REP_ACCEPT always monitored (if in active_mask)
					FD_SET(entry.s, &readfds);
					BSDTRACE((_T("BSDSOCK: Adding socket %d to readfds (mask has REP_ACCEPT)\n"), entry.sd));
				}
			}

			// REP_CLOSE requires readfds to detect EOF via peek_socket
			if ((active_mask & REP_CLOSE) && entry.connected && !entry.connecting) {
				FD_SET(entry.s, &readfds);
			}

			// REP_WRITE is treated as Level Triggered in select() but Edge Triggered/One-Shot for Amiga signals.
			// If connected and not connecting, we monitor for write if the event is active (not fired).
			if ((active_mask & (REP_WRITE | REP_CONNECT)) && entry.connected && !entry.connecting) {
				FD_SET(entry.s, &writefds);
			}

			// REP_CONNECT is One-Shot (handled via active_mask).
			// Only monitor if explicitly connecting.
			if ((active_mask & REP_CONNECT) && entry.connecting) {
				FD_SET(entry.s, &writefds);
				BSDTRACE((_T("BSDSOCK: Monitoring socket %d for connect completion (connecting=true)\n"), entry.sd));
			}

			if (active_mask & REP_OOB) {
				FD_SET(entry.s, &exceptfds);
			}
		}

		monitor->mutex.unlock();

		// Wait for events with 1 second timeout
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int result = select(maxfd + 1, &readfds, &writefds, &exceptfds, &timeout);

		BSDTRACE((_T("BSDSOCK: select() returned %d\n"), result));

		if (result < 0) {
			if (errno == EINTR) continue;
			if (errno == EBADF) {
				BSDTRACE((_T("BSDSOCK: Event monitor select() got EBADF, rebuilding\n")));
				continue;
			}
			write_log("BSDSOCK: Event monitor select() error: %d\n", errno);
			Sleep(100);
			continue;
		}

		if (result == 0) {
			// Timeout, just loop again
			continue;
		}

		// Check wake pipe
		if (FD_ISSET(monitor->wake_pipe[0], &readfds)) {
			char buf[256];
			read_pipe(monitor->wake_pipe[0], buf, sizeof(buf));
			// Socket list changed, loop again to rebuild fd_sets
			continue;
		}

		// Check sockets for events
		monitor->mutex.lock();

		for (auto& entry : monitor->socket_list) {
			if (entry.s == INVALID_SOCKET) continue;

			int events = 0;
            // Add slight delay if we are spinning on Level Triggered events to prevent CPU hog
            // Only if we found something? No, loop level.
            // We rely on select() usage. If select returns immediately, we spin.
            // We cannot easily throttle here without affecting response time.
            // Hoping App clears the signal buffers or handles it.

			if (FD_ISSET(entry.s, &readfds)) {
				// Filter "phantom" read events (where select says ready but peek returns error/block)
				if ((entry.eventmask & REP_READ) || (entry.eventmask & REP_CLOSE)) {
					int peek = peek_socket(entry.s);
					if (peek == 2) { // Data
						if ((entry.eventmask & REP_READ) && !(entry.fired_mask & REP_READ)) {
							events |= REP_READ;
						}
					} else if (peek == 1) { // EOF
						if ((entry.eventmask & REP_CLOSE) && !(entry.fired_mask & REP_CLOSE)) {
							events |= REP_CLOSE;
						}
						// EOF is also readable (read returns 0)
						if ((entry.eventmask & REP_READ) && !(entry.fired_mask & REP_READ)) {
							events |= REP_READ;
						}
					}
				}
				if ((entry.eventmask & REP_ACCEPT) && !(entry.fired_mask & REP_ACCEPT)) {
					events |= REP_ACCEPT;
				}
			}

			if (FD_ISSET(entry.s, &writefds)) {
				bool wrote = false;

				if (entry.connecting) {
					int error = 0;
					socklen_t len = sizeof(error);
					if (getsockopt(entry.s, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0 || error != 0) {
						// Connection failed
						write_log("BSDSOCK: Socket %d connect check failed (errno=%d), checking SO_ERROR\n", entry.sd, errno);
						// We don't set REP_ERROR here, maybe we should? But WinUAE usually handles it via generic error?
						// Actually, we should probably set REP_ERROR if the app asked for it.
						if (entry.eventmask & REP_ERROR) events |= REP_ERROR;
						entry.connecting = false;
					} else {
						entry.connecting = false;
						entry.connected = true;

						if ((entry.eventmask & REP_CONNECT) && !(entry.fired_mask & REP_CONNECT)) {
							events |= REP_CONNECT;
						}
						// Writable now
						if ((entry.eventmask & REP_WRITE) && !(entry.fired_mask & REP_WRITE)) {
							events |= REP_WRITE;
						}
                        // Do NOT set REP_READ here blindly. Let readfds handle it.
						BSDTRACE((_T("BSDSOCK: Socket %d CONNECT completed successfully\n"), entry.sd));
					}
					wrote = true;
				}

				// Standard Write Signaling
				if ((entry.eventmask & REP_WRITE) && !(entry.fired_mask & REP_WRITE)) {
					events |= REP_WRITE;
					wrote = true;
				}

				// Fallback: If App asked for REP_CONNECT but NOT REP_WRITE, and we are connected (writable).
				// We must signal REP_CONNECT (or WRITE) to wake it up.
				if (!wrote && (entry.eventmask & REP_CONNECT) && entry.connected) {
					events |= REP_WRITE | REP_CONNECT;
				}
			}

			if (FD_ISSET(entry.s, &exceptfds)) {
				if (entry.eventmask & REP_OOB) {
					events |= REP_OOB;
				}
			}

			// Post events to Amiga and clear them from the mask (one-shot delivery)
			if (events) {
				post_socket_event(entry.sb, entry.sd, events);

				// Mark these events as fired so we don't post them again until re-enabled
				// This implements the one-shot behavior required by Amiga apps
				entry.fired_mask |= events;

				// Do NOT clear them from eventmask, as that loses the user's request.
				// Do NOT update ftable here, post_socket_event handles the SET_ flags.

				BSDTRACE((_T("BSDSOCK: Fired events 0x%x for socket %d, fired_mask now 0x%x\n"),
				          events, entry.sd, entry.fired_mask));
			}
		}

		monitor->mutex.unlock();

		// Throttle the loop to prevent lock starvation of the Amiga Task
		// If we spin freely, addtosigqueue() locks can starve GetSocketEvents().
		Sleep(10);
	}

	write_log("BSDSOCK: Event monitor thread exiting\n");
	return;
}

// Start the event monitor thread
static bool start_event_monitor()
{
	if (g_event_monitor) {
		return true; // Already running
	}

	g_event_monitor = new event_monitor();
	if (!g_event_monitor) {
		write_log("BSDSOCK: Failed to allocate event monitor\n");
		return false;
	}

	// Create wake pipe
	if (pipe(g_event_monitor->wake_pipe) < 0) {
		write_log("BSDSOCK: Failed to create wake pipe: %d\n", errno);
		delete g_event_monitor;
		g_event_monitor = nullptr;
		return false;
	}

	// Initialize state
	g_event_monitor->running = true;
	g_event_monitor->socket_list.clear();

	// Start thread
	if (!uae_start_thread("bsdsock_event_monitor", event_monitor_thread, g_event_monitor, &g_event_monitor->thread)) {
		write_log("BSDSOCK: Failed to start event monitor thread\n");
		close_pipe(g_event_monitor->wake_pipe[0]);
		close_pipe(g_event_monitor->wake_pipe[1]);
		delete g_event_monitor;
		g_event_monitor = nullptr;
		return false;
	}

	write_log("BSDSOCK: Event monitor started\n");
	return true;
}

// Stop the event monitor thread
static void stop_event_monitor()
{
	if (!g_event_monitor) {
		return;
	}

	write_log("BSDSOCK: Stopping event monitor\n");

	// Signal thread to stop
	g_event_monitor->running = false;

	// Wake up the thread
	char wake = 1;
	write_pipe(g_event_monitor->wake_pipe[1], &wake, 1);

	// Wait for thread to exit
	uae_wait_thread(g_event_monitor->thread);

	// Cleanup
	close_pipe(g_event_monitor->wake_pipe[0]);
	close_pipe(g_event_monitor->wake_pipe[1]);
	delete g_event_monitor;
	g_event_monitor = nullptr;

	write_log("BSDSOCK: Event monitor stopped\n");
}

// Register a socket for event monitoring
static void register_socket_events(struct socketbase* sb, int sd, SOCKET_TYPE s, int eventmask)
{
	if (!g_event_monitor) {
		if (!start_event_monitor()) {
			write_log("BSDSOCK: Failed to start event monitor for socket %d\n", sd);
			return;
		}
	}

	g_event_monitor->mutex.lock();

	// Check if socket already registered
	bool found = false;
	for (auto& entry : g_event_monitor->socket_list) {
		if (entry.sb == sb && entry.sd == sd) {
			// Update existing entry
			entry.eventmask = eventmask;
			found = true;
			BSDTRACE((_T("BSDSOCK: Updated event mask 0x%x for socket %d\n"), eventmask, sd));
			break;
		}
	}

	if (!found) {
		// Add new entry
		socket_event_entry entry;
		entry.sb = sb;
		entry.sd = sd;
		entry.s = s;
		entry.eventmask = eventmask;
		entry.connecting = false;
		// Determine actual connection state; bare sockets must not fire REP_WRITE/READ
		struct sockaddr_in peer;
		socklen_t plen = sizeof(peer);
		entry.connected = (getpeername(s, (struct sockaddr*)&peer, &plen) == 0);
		entry.fired_mask = 0;
		g_event_monitor->socket_list.push_back(entry);

		BSDTRACE((_T("BSDSOCK: Registered socket %d (native %d) for event monitoring (mask 0x%x)\n"), sd, s, eventmask));
	}

	// Wake up monitor thread to rebuild fd_sets
	char wake = 1;
	write_pipe(g_event_monitor->wake_pipe[1], &wake, 1);

	g_event_monitor->mutex.unlock();
}

// Unregister a socket from event monitoring
static void unregister_socket_events(struct socketbase* sb, int sd)
{
	if (!g_event_monitor) {
		return;
	}

	g_event_monitor->mutex.lock();

	// Remove socket from list
	auto it = g_event_monitor->socket_list.begin();
	while (it != g_event_monitor->socket_list.end()) {
		if (it->sb == sb && it->sd == sd) {
			it = g_event_monitor->socket_list.erase(it);
			BSDTRACE((_T("BSDSOCK: Unregistered socket %d from event monitoring\n"), sd));
		} else {
			++it;
		}
	}

	// Wake up monitor thread
	char wake = 1;
	write_pipe(g_event_monitor->wake_pipe[1], &wake, 1);

	g_event_monitor->mutex.unlock();
}

// Unregister all sockets for a socketbase (called during cleanup)
static void unregister_all_socket_events(struct socketbase* sb)
{
	if (!g_event_monitor) {
		return;
	}

	g_event_monitor->mutex.lock();

	bool removed = false;
	auto it = g_event_monitor->socket_list.begin();
	while (it != g_event_monitor->socket_list.end()) {
		if (it->sb == sb) {
			it = g_event_monitor->socket_list.erase(it);
			removed = true;
		} else {
			++it;
		}
	}

	if (removed) {
		char wake = 1;
		write_pipe(g_event_monitor->wake_pipe[1], &wake, 1);
	}

	g_event_monitor->mutex.unlock();
}

// Set the connecting state for a socket
static void set_socket_connecting(struct socketbase* sb, int sd, bool connecting)
{
	if (!g_event_monitor) return;

	g_event_monitor->mutex.lock();
	for (auto& entry : g_event_monitor->socket_list) {
		if (entry.sb == sb && entry.sd == sd) {
			entry.connecting = connecting;
			BSDTRACE((_T("BSDSOCK: Socket %d connecting state set to %d\n"), sd, connecting));
			break;
		}
	}
	// Wake up monitor to update handling
	if (g_event_monitor->wake_pipe[1] != -1) {
		char b = 1;
		write_pipe(g_event_monitor->wake_pipe[1], &b, 1);
	}
	g_event_monitor->mutex.unlock();
}

// Re-enable specific events for a socket (called by IO functions)
static void socket_reenable_events(struct socketbase* sb, int sd, int events)
{
	if (!g_event_monitor) return;

	g_event_monitor->mutex.lock();
	for (auto& entry : g_event_monitor->socket_list) {
		if (entry.sb == sb && entry.sd == sd) {
			if (entry.fired_mask & events) {
				entry.fired_mask &= ~events;
				BSDTRACE((_T("BSDSOCK: Re-enabled events 0x%x for socket %d\n"), events, sd));
				// Wake up monitor to check this socket again
				if (g_event_monitor->wake_pipe[1] != -1) {
					char b = 1;
					write_pipe(g_event_monitor->wake_pipe[1], &b, 1);
				}
			}
			break;
		}
	}
	g_event_monitor->mutex.unlock();
}

static int copysockaddr_a2n(struct sockaddr_in* addr, uae_u32 a_addr, unsigned int len)
{
	if ((len > sizeof(struct sockaddr_in)) || (len < 8))
		return 1;

	if (a_addr == 0)
		return 0;

	addr->sin_family = get_byte(a_addr + 1);
#if defined(__HAIKU__)
	if (addr->sin_family == 2) addr->sin_family = AF_INET;
#endif
	addr->sin_port = htons(get_word(a_addr + 2));
	addr->sin_addr.s_addr = htonl(get_long(a_addr + 4));

	if (len > 8)
		memcpy(&addr->sin_zero, get_real_address(a_addr + 8), static_cast<size_t>(len) - 8);   /* Pointless? */

	return 0;
}

/*
 * Copy a sockaddr object from native space to amiga space
 */
static int copysockaddr_n2a (uae_u32 a_addr, const struct sockaddr_in *addr, unsigned int len)
{
	if (len < 8)
		return 1;

	if (a_addr == 0)
		return 0;

	put_byte (a_addr, 0);                       /* Anyone use this field? */
#if defined(__HAIKU__)
	{ int amiga_af = addr->sin_family;
	  if (amiga_af == AF_INET) amiga_af = 2;
	  put_byte(a_addr + 1, amiga_af); }
#else
	put_byte (a_addr + 1, addr->sin_family);
#endif
	put_word (a_addr + 2, ntohs (addr->sin_port));
	put_long (a_addr + 4, ntohl (addr->sin_addr.s_addr));

	if (len > 8)
		memset (get_real_address (a_addr + 8), 0, static_cast<size_t>(len) - 8);

	return 0;
}

/*
 * Copy a hostent object from native space to amiga space
 */
static void copyHostent(TrapContext* ctx, const struct hostent* hostent, SB)
{
	int size = 28;
	int i;
	int numaddr = 0;
	int numaliases = 0;
	uae_u32 aptr;

	if (hostent->h_name != NULL)
		size += strlen(hostent->h_name) + 1;

	if (hostent->h_aliases != NULL)
		while (hostent->h_aliases[numaliases])
			size += strlen(hostent->h_aliases[numaliases++]) + 5;

	if (hostent->h_addr_list != NULL) {
		while (hostent->h_addr_list[numaddr])
			numaddr++;
		size += numaddr * (hostent->h_length + 4);
	}

	aptr = sb->hostent + 28 + numaliases * 4 + numaddr * 4;

	// transfer hostent to Amiga memory
	trap_put_long(ctx, sb->hostent + 4, sb->hostent + 20);
	trap_put_long(ctx, sb->hostent + 8, hostent->h_addrtype);
	trap_put_long(ctx, sb->hostent + 12, hostent->h_length);
	trap_put_long(ctx, sb->hostent + 16, sb->hostent + 24 + numaliases * 4);

	for (i = 0; i < numaliases; i++)
		trap_put_long(ctx, sb->hostent + 20 + i * 4, addstr(ctx, &aptr, hostent->h_aliases[i]));
	trap_put_long(ctx, sb->hostent + 20 + numaliases * 4, 0);

	for (i = 0; i < numaddr; i++) {
		trap_put_long(ctx, sb->hostent + 24 + (numaliases + i) * 4, addmem(ctx, &aptr, hostent->h_addr_list[i], hostent->h_length));
	}
	trap_put_long(ctx, sb->hostent + 24 + numaliases * 4 + numaddr * 4, 0);
	trap_put_long(ctx, sb->hostent, aptr);
	addstr(ctx, &aptr, hostent->h_name);

	bsdsocklib_seterrno(ctx, sb, 0);
}

/*
 * Copy a protoent object from native space to Amiga space
 */
static void copyProtoent(TrapContext* ctx, SB, const struct protoent* p)
{
	int size = 16;
	int numaliases = 0;
	int i;
	uae_u32 aptr;

	// compute total size of protoent
	if (p->p_name != NULL)
		size += strlen(p->p_name) + 1;

	if (p->p_aliases != NULL)
		while (p->p_aliases[numaliases])
			size += strlen(p->p_aliases[numaliases++]) + 5;

	if (sb->protoent) {
		uae_FreeMem(ctx, sb->protoent, sb->protoentsize, sb->sysbase);
	}

	sb->protoent = uae_AllocMem(ctx, size, 0, sb->sysbase);

	if (!sb->protoent) {
		write_log("BSDSOCK: WARNING - copyProtoent() ran out of Amiga memory (couldn't allocate %d bytes)\n", size);
		bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
		return;
	}

	sb->protoentsize = size;

	aptr = sb->protoent + 16 + numaliases * 4;

	// transfer protoent to Amiga memory
	trap_put_long(ctx, sb->protoent + 4, sb->protoent + 12);
	trap_put_long(ctx, sb->protoent + 8, p->p_proto);

	for (i = 0; i < numaliases; i++)
		trap_put_long(ctx, sb->protoent + 12 + i * 4, addstr(ctx, &aptr, p->p_aliases[i]));
	trap_put_long(ctx, sb->protoent + 12 + numaliases * 4, 0);
	trap_put_long(ctx, sb->protoent, aptr);
	addstr(ctx, &aptr, p->p_name);
	bsdsocklib_seterrno(ctx, sb, 0);
}

uae_u32 bsdthr_Accept_2 (SB)
{
	int foo, s, s2;
	long flags;
	struct sockaddr_in addr{};
	socklen_t hlen = sizeof (struct sockaddr_in);

	if ((s = accept (sb->s, (struct sockaddr *)&addr, &hlen)) >= 0) {
		if ((flags = fcntl (s, F_GETFL)) == -1)
			flags = 0;
		fcntl (s, F_SETFL, flags & ~O_NONBLOCK); /* @@@ Don't do this if it's supposed to stay nonblocking... */
		s2 = getsd (sb->context, sb, s);
		if (s2 == -1) {
			write_log("bsdthr_Accept_2: descriptor table full, closing accepted socket %d\n", s);
			close(s);
			return -1;
		}
		sb->ftable[s2-1] = sb->ftable[sb->len]; /* new socket inherits the old socket's properties */
		write_log ("Accept: AmigaSide %d, NativeSide %d, len %d(%d)", sb->resultval, s, hlen, get_long (sb->a_addrlen));
		printSockAddr (&addr);
		foo = get_long (sb->a_addrlen);
		if (foo > 16)
			put_long (sb->a_addrlen, 16);
		copysockaddr_n2a (sb->a_addr, &addr, foo);
		return s2 - 1;
	} else {
		return -1;
	}
}

uae_u32 bsdthr_Recv_2 (SB)
{
	int foo;
	int socktype = 0;
	socklen_t optlen = sizeof(socktype);
	getsockopt(sb->s, SOL_SOCKET, SO_TYPE, (char*)&socktype, &optlen);
	int retries = (socktype == SOCK_RAW) ? 5 : 1;
	if (sb->from == 0) {
		ssize_t n;
		do {
			n = recv(sb->s, (char*)sb->buf, sb->len, sb->flags /*| MSG_NOSIGNAL*/);
			foo = (int)n;
			if (foo >= 0) break;
		} while (errno == EINTR && --retries > 0);
	} else {
		struct sockaddr_in addr{};
		socklen_t l = sizeof(struct sockaddr_in);
		int i = get_long(sb->fromlen);
		ssize_t n;
		copysockaddr_a2n(&addr, sb->from, i);
		do {
			n = recvfrom(sb->s, (char*)sb->buf, sb->len, sb->flags | MSG_NOSIGNAL, (struct sockaddr *)&addr, &l);
			foo = (int)n;
			if (foo >= 0) {
				copysockaddr_n2a(sb->from, &addr, l);
				put_long(sb->fromlen, l);
				break;
			}
		} while (errno == EINTR && --retries > 0);
	}
	return foo;
}

uae_u32 bsdthr_Send_2 (SB)
{
	if (sb->to == 0) {
		ssize_t n;
		n = send (sb->s, (const char*)sb->buf, sb->len, sb->flags | MSG_NOSIGNAL);
		return (int)n;
	} else {
		struct sockaddr_in addr{};
		int l = sizeof (struct sockaddr_in);
		ssize_t n;
		copysockaddr_a2n (&addr, sb->to, sb->tolen);
		n = sendto (sb->s, (const char*)sb->buf, sb->len, sb->flags | MSG_NOSIGNAL, (struct sockaddr *)&addr, l);
		return (int)n;
	}
}

uae_u32 bsdthr_Connect_2 (SB)
{
	if (sb->action == 1) {
		struct sockaddr_in addr{};
		int len = sizeof (struct sockaddr_in);
		int retval;
		copysockaddr_a2n (&addr, sb->a_addr, sb->a_addrlen);
		retval = connect (sb->s, (struct sockaddr *)&addr, len);
		/* Hack: I need to set the action to something other than
		 * 1 but I know action == 2 does the correct thing
		 */
		sb->action = 2;
		if (retval == 0) {
			 errno = 0;
		}
		return retval;
	} else {
		int foo;
		socklen_t bar;
		bar = sizeof (foo);
		if (getsockopt (sb->s, SOL_SOCKET, SO_ERROR, (char*)&foo, &bar) == 0) {
			errno = foo;
			return (foo == 0) ? 0 : -1;
		}
		return -1;
	}
}

uae_u32 bsdthr_SendRecvAcceptConnect (uae_u32 (*tryfunc)(SB), SB)
{
	return bsdthr_blockingstuff (tryfunc, sb);
}

uae_u32 bsdthr_blockingstuff(uae_u32(*tryfunc)(SB), SB)
{
    int done = 0, foo = 0;
    long flags;
    int nonblock;
    int saved_errno = 0;
    int socktype = 0;
    socklen_t optlen = sizeof(socktype);
    int is_raw = 0;
    struct timeval orig_timeout = {0}, timeout = {0};
    socklen_t tvlen = sizeof(orig_timeout);
    int timeout_set = 0;
    if ((flags = fcntl(sb->s, F_GETFL)) == -1)
        flags = 0;
    // Check if this is a raw socket
    if (getsockopt(sb->s, SOL_SOCKET, SO_TYPE, (char*)&socktype, &optlen) == 0 && socktype == SOCK_RAW) {
        is_raw = 1;
        // Save original timeout
        if (getsockopt(sb->s, SOL_SOCKET, SO_RCVTIMEO, (char*)&orig_timeout, &tvlen) == 0) {
            timeout_set = 1;
        }
        // Set a 1 second timeout for raw sockets
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(sb->s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    }
    nonblock = (flags & O_NONBLOCK);
    // Only set non-blocking for non-raw sockets
    if (!is_raw) {
        fcntl(sb->s, F_SETFL, flags | O_NONBLOCK);
    }
    while (!done) {
        done = 1;
        do {
            foo = tryfunc(sb);
        } while (foo < 0 && errno == EINTR); // retry on EINTR
        /* Save errno immediately after tryfunc(); any intervening call (write_log,
         * getsockopt, etc.) can clobber it. Use saved_errno for all checks below,
         * and restore it so code inside the block that reads errno directly is consistent. */
        saved_errno = errno;
        if (foo < 0 && !nonblock) {
            errno = saved_errno;
            if ((saved_errno == EAGAIN) || (saved_errno == EWOULDBLOCK) || (saved_errno == EINPROGRESS)) {
                fd_set readset, writeset, exceptset;
                int maxfd = (sb->s > sb->sockabort[0]) ? sb->s : sb->sockabort[0];
                int num;

                FD_ZERO(&readset);
                FD_ZERO(&writeset);
                FD_ZERO(&exceptset);

                if (sb->action == 3 || sb->action == 6)
                    FD_SET(sb->s, &readset);
                if (sb->action == 2 || sb->action == 1 || sb->action == 4)
                    FD_SET(sb->s, &writeset);
                FD_SET(sb->sockabort[0], &readset);

                do {
                    num = select(maxfd + 1, &readset, &writeset, &exceptset, NULL);
                } while (num == -1 && errno == EINTR); // retry on EINTR
                if (num == -1) {
                    int _select_err = errno; /* save before write_log/fcntl/setsockopt clobber it */
                    write_log("Blocking select(%d) returns -1,errno is %d\n", sb->sockabort[0], _select_err);
                    if (!is_raw) fcntl(sb->s, F_SETFL, flags);
                    if (is_raw && timeout_set) setsockopt(sb->s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&orig_timeout, sizeof(orig_timeout));
                    errno = _select_err; /* restore after cleanup calls */
                    return -1;
                }

                if (FD_ISSET(sb->sockabort[0], &readset) || FD_ISSET(sb->sockabort[0], &writeset)) {
                    /* reset sock abort pipe */
                    /* read from the pipe to reset it */
                    clearsockabort(sb);
                    errno = EINTR;
                    done = 1;
                }
                else {
                    done = 0;
                }
            }
            else if (errno == EINTR)
                done = 1;
        }
    }
    if (!is_raw) fcntl(sb->s, F_SETFL, flags);
    if (is_raw && timeout_set) setsockopt(sb->s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&orig_timeout, sizeof(orig_timeout));
    /* Restore errno after fcntl/setsockopt cleanup; caller (bsdlib_threadfunc) reads
     * errno via SETERRNO immediately after we return. */
    errno = saved_errno;
    return foo;
}

static void bsdlib_threadfunc(void* arg)
{
	auto* sb = (struct socketbase*)arg;

	while (1) {
		uae_sem_wait(&sb->sem);

		TrapContext* ctx = sb->context;

		switch (sb->action) {
		case 0:       /* kill thread (CloseLibrary) */

			uae_sem_destroy(&sb->sem);
			return;

		case 1:       /* Connect */
			sb->resultval = bsdthr_SendRecvAcceptConnect(bsdthr_Connect_2, sb);
			if ((int)sb->resultval < 0) {
				SETERRNO;
			} else {
				bsdsocklib_seterrno(ctx, sb, 0);
			}
			break;

			/* @@@ Should check (from|to)len so it's 16.. */
		case 2:       /* Send[to] */
			sb->resultval = bsdthr_SendRecvAcceptConnect(bsdthr_Send_2, sb);
			if ((int)sb->resultval < 0) {
				SETERRNO;
			} else {
				bsdsocklib_seterrno(ctx, sb, 0);
			}
			break;

		case 3:       /* Recv[from] */
			sb->resultval = bsdthr_SendRecvAcceptConnect(bsdthr_Recv_2, sb);
			if ((int)sb->resultval < 0) {
				SETERRNO;
			} else {
				bsdsocklib_seterrno(ctx, sb, 0);
			}
			break;

		case 4: {     /* Gethostbyname */
#if defined(__linux__)
			struct hostent hent, *tmphostent = nullptr;
			char buf[1024];
			int herr, ret;
			ret = gethostbyname_r((char*)get_real_address(sb->name), &hent, buf, sizeof(buf), &tmphostent, &herr);
			if (ret == 0 && tmphostent) {
				copyHostent(ctx, tmphostent, sb);
				bsdsocklib_setherrno(ctx, sb, 0);
			} else {
				bsdsocklib_setherrno(ctx, sb, herr);
				SETERRNO;
			}
#else
			std::lock_guard<std::mutex> lock(bsdsock_mutex);
			struct hostent* tmphostent = gethostbyname((char*)get_real_address(sb->name));
			if (tmphostent) {
				copyHostent(ctx, tmphostent, sb);
				bsdsocklib_setherrno(ctx, sb, 0);
			} else {
				SETHERRNO;
				SETERRNO;
			}
#endif
			break;
		}

		case 5:       /* WaitSelect */
			sb->resultval = bsdthr_WaitSelect(sb);
			break;

		case 6:       /* Accept */
			sb->resultval = bsdthr_SendRecvAcceptConnect(bsdthr_Accept_2, sb);
			if ((int)sb->resultval < 0) {
				SETERRNO;
			}
			break;

		case 7: {
#if defined(__linux__)
			struct hostent hent, *tmphostent = nullptr;
			char buf[1024];
			int herr, ret;
			ret = gethostbyaddr_r((char*)get_real_address(sb->name), sb->a_addrlen, sb->flags, &hent, buf, sizeof(buf), &tmphostent, &herr);
			if (ret == 0 && tmphostent) {
				copyHostent(ctx, tmphostent, sb);
				bsdsocklib_setherrno(ctx, sb, 0);
			} else {
				bsdsocklib_setherrno(ctx, sb, herr);
				SETERRNO;
			}
#else
			std::lock_guard<std::mutex> lock(bsdsock_mutex);
			struct hostent* tmphostent = gethostbyaddr((const char*)get_real_address(sb->name), sb->a_addrlen, sb->flags);
			if (tmphostent) {
				copyHostent(ctx, tmphostent, sb);
				bsdsocklib_setherrno(ctx, sb, 0);
			} else {
				SETHERRNO;
				SETERRNO;
			}
#endif
			break;
		}
		}
		SETSIGNAL;
	}
	return;
}

void clearsockabort(SB)
{
	int chr;

	while (read_pipe(sb->sockabort[0], &chr, sizeof(chr)) >= 0) {
	}
}

int init_socket_layer(void)
{
	int result = 0;

	if (currprefs.socket_emu) {
		uae_sem_init(&sem_queue, 0, 1);
		return 1;
	}

	return result;
}

void deinit_socket_layer(void)
{
	stop_event_monitor();
	uae_sem_destroy(&sem_queue);
}

void locksigqueue(void)
{
	uae_sem_wait(&sem_queue);
}

void unlocksigqueue(void)
{
	uae_sem_post(&sem_queue);
}

int host_sbinit (TrapContext *ctx, SB)
{
	if (pipe (sb->sockabort) < 0) {
		return 0;
	}

	if (fcntl (sb->sockabort[0], F_SETFL, O_NONBLOCK) < 0) {
		write_log ("Set nonblock failed %d\n", errno);
	}

	uae_sem_init (&sb->sem, 0, 0);

	/* Alloc hostent buffer */
	sb->hostent = uae_AllocMem (ctx, 1024, 0, sb->sysbase);
	sb->hostentsize = 1024;

	/* @@@ The thread should be PTHREAD_CREATE_DETACHED */
	if (uae_start_thread ("bsdsocket", bsdlib_threadfunc, (void *)sb, &sb->thread) == BAD_THREAD) {
		write_log ("BSDSOCK: Failed to create thread.\n");
		uae_sem_destroy (&sb->sem);
		close_pipe (sb->sockabort[0]);
		close_pipe (sb->sockabort[1]);
		return 0;
	}
	return 1;
}

void host_closesocketquick (int s)
{
	struct linger l{};
	l.l_onoff = 0;
	l.l_linger = 0;
	if(s != -1) {
		setsockopt (s, SOL_SOCKET, SO_LINGER, (const char*)&l, sizeof(l));
		close_socket (s);
	}
}

void host_sbcleanup (SB)
{
	int i;

	if (!sb) {
		return;
	}

	unregister_all_socket_events(sb);

	uae_thread_id thread = sb->thread;
	/* Abort any pending blocking operation BEFORE closing the pipe.
	 * Without this, a connect() blocked in select() inside bsdthr_blockingstuff
	 * will never see the wakeup and the thread hangs forever. */
	sb->action = 0;
	sockabort(sb);           /* unblocks any select() waiting on sockabort[0] */
	uae_sem_post(&sb->sem);  /* wakes thread if blocked on semaphore instead */

	close_pipe (sb->sockabort[0]);
	close_pipe (sb->sockabort[1]);
	for (i = 0; i < sb->dtablesize; i++) {
		if (sb->dtable[i] != -1) {
			close_socket(sb->dtable[i]);
		}
	}

	/* We need to join with the socket thread to allow the thread to die
	 * and clean up resources when the underlying thread layer is pthreads.
	 * Ideally, this shouldn't be necessary, but, for example, when SDL uses
	 * pthreads, it always creates joinable threads - and we can't do anything
	 * about that. */
	uae_wait_thread (thread);
}

void host_sbreset (void)
{
	stop_event_monitor();
}

void sockabort (SB)
{
	int chr = 1;
	if (write_pipe (sb->sockabort[1], &chr, sizeof (chr)) != sizeof (chr)) {
		write_log("sockabort - did not write %zd bytes\n", sizeof(chr));
	}
}

int host_dup2socket(TrapContext *ctx, SB, int fd1, int fd2)
{
	int s1, s2;

	fd1++;

	s1 = getsock(ctx, sb, fd1);
	if (s1 != -1) {
		if (fd2 != -1) {
			if ((unsigned int) (fd2) >= (unsigned int) sb->dtablesize) {
				bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
				return -1;
			}
			fd2++;
			s2 = getsock(ctx, sb, fd2);
			if (s2 != -1) {
				unregister_socket_events(sb, fd2 - 1);
				close_socket (s2);
			}
			setsd (ctx, sb, fd2, dup (s1));
			return fd2 - 1;
		} else {
			fd2 = getsd (ctx, sb, 1);
			if (fd2 != -1) {
				setsd (ctx, sb, fd2, dup (s1));
				return (fd2 - 1);
			} else {
				return -1;
			}
		}
	}
	return -1;
}

int host_socket(TrapContext *ctx, SB, int af, int type, int protocol)
{
    int sd;
    int s;
#if defined(__HAIKU__)
    /* On Haiku AF_INET=1, but Amiga/BSD use AF_INET=2. Translate. */
	if (af == 2) af = AF_INET;
#endif

	write_log("socket(%s,%s,%d) -> ",af == AF_INET ? "AF_INET" : "AF_other",
	       type == SOCK_STREAM ? "SOCK_STREAM" : type == SOCK_DGRAM ?
	       "SOCK_DGRAM " : type == SOCK_RAW ? "SOCK_RAW" : "SOCK_other", protocol);

	if ((s = socket (af, type, protocol)) == -1)  {
		SETERRNO;
		write_log("failed (%d)\n", sb->sb_errno);
		return -1;
	} else {
		int arg = 1;
		sd = getsd (ctx, sb, s);
		if (sd == -1) {
			write_log("host_socket: descriptor table full, closing socket %d\n", s);
			close(s);
			return -1;
		}
		setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const char*)&arg, sizeof(arg));
	}

	sb->ftable[sd-1] = SF_BLOCKING;
	write_log("socket returns Amiga %d, NativeSide %d\n", sd - 1, s);
	return sd - 1;
}

uae_u32 host_bind(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	uae_u32 success = 0;
	struct sockaddr_in addr{};
	int len = sizeof (struct sockaddr_in);
	int s;

	s = getsock(ctx, sb, sd + 1);
	if (s == -1) {
		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
		return -1;
	}

	write_log("bind(%u[%d], 0x%x, %u) -> ", sd, s, name, namelen);
	copysockaddr_a2n (&addr, name, namelen);
	printSockAddr (&addr);
	if ((success = ::bind (s, (struct sockaddr *)&addr, len)) != (uae_u32)0) {
		SETERRNO;
		// Improved error logging
		write_log("failed (%d: %s)\n", sb->sb_errno, strerror(errno));
		// Special message for privileged ports
		if (errno == EACCES && ntohs(addr.sin_port) < 1024) {
			write_log("bind() failed: Port %d is privileged (<1024), requires root privileges.\n", ntohs(addr.sin_port));
		}
	} else {
		write_log("OK\n");
	}
	return success;
}

uae_u32 host_listen(TrapContext *ctx, SB, uae_u32 sd, uae_u32 backlog)
{
	int s;
	uae_u32 success = -1;

	write_log("listen(%d,%d) -> ", sd, backlog);
	s = getsock(ctx, sb, sd + 1);

	if (s == -1) {
		bsdsocklib_seterrno (ctx, sb, 9);
		return -1;
	}

	if ((success = listen (s, backlog)) != 0) {
		SETERRNO;
		write_log("failed (%d)\n", sb->sb_errno);
	} else {
		write_log("OK\n");
	}
	return success;
}

void host_accept(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	sb->s = getsock(ctx, sb, sd + 1);
	if (sb->s == -1) {
		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
		return;
	}

	write_log("accept(%d, %x, %x)\n", sb->s, name, namelen);
	sb->a_addr    = name;
	sb->a_addrlen = namelen;
	sb->action    = 6;
	sb->len       = sd;
	// used by bsdthr_Accept_2
	sb->context = ctx;

	uae_sem_post (&sb->sem);

	WAITSIGNAL;
	write_log("Accept returns %d\n", sb->resultval);

	// Implicitly re-enable REP_ACCEPT
	if (sb->resultval >= 0) {
		socket_reenable_events(sb, sd, REP_ACCEPT);
	}
}

void host_connect(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	static int wscounter;
	int wscnt;

	sd++;
	wscnt = ++wscounter;

	if (!addr_valid (_T("host_connect"), name, namelen))
		return;

	s = getsock(ctx, sb, sd);

	if (s != INVALID_SOCKET) {
		if (namelen <= MAXADDRLEN) {
			sb->s = getsock(ctx, sb, sd);
			sb->a_addr    = name;
			sb->a_addrlen = namelen;
			sb->action    = 1;

			// Notify event monitor that connect is in progress
			// Note: sd was incremented at start of function, so we must pass sd-1
			// to match the 0-based descriptor used in registration
			set_socket_connecting(sb, sd - 1, true);

			uae_sem_post (&sb->sem);

			WAITSIGNAL;

			// Implicitly re-enable REP_CONNECT (and REP_WRITE as they are related on success)
			socket_reenable_events(sb, sd - 1, REP_CONNECT | REP_WRITE);
		} else {
			write_log (_T("BSDSOCK: WARNING - Excessive namelen (%d) in connect():%d!\n"), namelen, wscnt);
		}
	} else {
		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
		return;
	}
}

void host_sendto (TrapContext *ctx, SB, uae_u32 sd, uae_u32 msg, uae_u8 *hmsg, uae_u32 len, uae_u32 flags, uae_u32 to, uae_u32 tolen)
{
	SOCKET s;
	char *realpt;
	static int wscounter;
	int wscnt;

	wscnt = ++wscounter;

	sd++;
	s = getsock(ctx, sb, sd);

	if (s != INVALID_SOCKET) {
		if (hmsg == NULL) {
			if (!addr_valid (_T("host_sendto1"), msg, 4))
				return;
			realpt = (char*)get_real_address (msg);
		} else {
			realpt = (char*)hmsg;
		}

		sb->s = s;
		sb->buf    = realpt;
		sb->len    = len;
		sb->flags  = flags;
		sb->to     = to;
		sb->tolen  = tolen;
		sb->action = 2;

		uae_sem_post (&sb->sem);

		WAITSIGNAL;

		// Implicitly re-enable REP_WRITE
		socket_reenable_events(sb, sd - 1, REP_WRITE);

	} else {
		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
		return;
	}
}

void host_recvfrom(TrapContext *ctx, SB, uae_u32 sd, uae_u32 msg, uae_u8 *hmsg, uae_u32 len, uae_u32 flags, uae_u32 addr, uae_u32 addrlen)
{
	int s;
	uae_char *realpt;
	static int wscounter;
	int wscnt;

	wscnt = ++wscounter;

	s = getsock(ctx, sb, sd + 1);

	if (s != -1) {
		if (hmsg == NULL) {
			if (!addr_valid (_T("host_recvfrom1"), msg, 4))
				return;
			realpt = (char*)get_real_address (msg);
		} else {
			realpt = (char*)hmsg;
		}
	} else {
		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */;
		return;
	}

	sb->s      = s;
	sb->buf    = realpt;
	sb->len    = len;
	sb->flags  = flags;
	sb->from   = addr;
	sb->fromlen= addrlen;
	sb->action = 3;

	uae_sem_post (&sb->sem);

	WAITSIGNAL;

	// Implicitly re-enable REP_READ and REP_OOB
	socket_reenable_events(sb, sd, REP_READ | REP_OOB);
}

uae_u32 host_shutdown(SB, uae_u32 sd, uae_u32 how)
{
	TrapContext *ctx = NULL;
	SOCKET s;

	write_log("shutdown(%d,%d) -> ", sd, how);
	s = getsock(ctx, sb, sd + 1);

	if (s != INVALID_SOCKET) {
		if (shutdown (s, how)) {
			SETERRNO;
			write_log("failed (%d)\n", sb->sb_errno);
		} else {
			write_log("OK\n");
			return 0;
		}
	}

	return -1;
}

void host_setsockopt(SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 len)
{
	TrapContext* ctx = NULL;
	int s = getsock(ctx, sb, sd + 1);
	void* buf = NULL;
	struct linger sl;
	struct timeval timeout;

	if (s == INVALID_SOCKET) {
		sb->resultval = -1;
		bsdsocklib_seterrno(ctx, sb, 9); /* EBADF */
		return;
	}

	// Handle SO_EVENTMASK (0x2001) - Amiga-specific async event notification
	// Must be checked BEFORE mapsockoptname validation since it's not in the mapping table
	// Level 0xFFFF is Amiga's SOL_SOCKET value
	if (level == 0xFFFF && optname == 0x2001) {
		uae_u32 eventflags = 0;
		if (optval && len >= 4) {
			eventflags = get_long(optval);
		}

		// Fix for dynAMIte and other apps that rely on implicit Writability after Connect:
		if (eventflags & REP_CONNECT) {
			eventflags |= REP_WRITE;
			write_log("BSDSOCK: Force-enabled REP_WRITE for socket %d (requested mask 0x%x -> 0x%x)\n", sd, get_long(optval), eventflags);
		}

		BSDTRACE((_T("BSDSOCK: SO_EVENTMASK called for socket %d, eventflags=0x%x\n"), sd, eventflags));

		// Store event mask in ftable (using lower bits)
		sb->ftable[sd] = (sb->ftable[sd] & ~REP_ALL) | (eventflags & REP_ALL);

		// Register or unregister with event monitor
		if (eventflags & REP_ALL) {
			// Register socket for event monitoring
			register_socket_events(sb, sd, s, eventflags & REP_ALL);
		} else {
			// Unregister socket from event monitoring
			unregister_socket_events(sb, sd);
			// Clear pending SET_* flags to prevent stale events on fd reuse
			sb->ftable[sd] &= ~SET_ALL;
		}

		sb->resultval = 0;
		return;
	}

	// Now map the level and option name
	int nativelevel = mapsockoptlevel(level);
	int nativeoptname = mapsockoptname(nativelevel, optname);

	// Prevent invalid setsockopt calls
	if (nativeoptname == -1) {
		write_log("host_setsockopt: Invalid option 0x%x for level %d (native level %d), not calling setsockopt.\n", optname, level, nativelevel);
		sb->resultval = -1;
		errno = EINVAL;
		SETERRNO;
		return;
	}

	if (optval) {
		buf = malloc(len);
		if (buf == NULL) {
			sb->resultval = -1;
			bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
			return;
		}
		if (nativeoptname == SO_LINGER) {
			sl.l_onoff = get_long(optval);
			sl.l_linger = get_long(optval + 4);
		}
		else if (nativeoptname == SO_RCVTIMEO || nativeoptname == SO_SNDTIMEO) {
			timeout.tv_sec = get_long(optval);
			timeout.tv_usec = get_long(optval + 4);
		}
		else {
			mapsockoptvalue(nativelevel, nativeoptname, optval, buf);
		}
	}
	if (nativeoptname == SO_RCVTIMEO || nativeoptname == SO_SNDTIMEO) {
		sb->resultval = setsockopt(s, nativelevel, nativeoptname, (const char*)&timeout, sizeof(timeout));
	}
	else if (nativeoptname == SO_LINGER) {
		sb->resultval = setsockopt(s, nativelevel, nativeoptname, (const char*)&sl, sizeof(sl));
	}
	else {
		sb->resultval = setsockopt(s, nativelevel, nativeoptname, (const char*)buf, len);
	}
	if (buf)
		free(buf);
	SETERRNO;

	write_log("setsockopt: sock %d, level %d, 'name' %d(%d), len %d -> %d, %d\n",
		s, level, optname, nativeoptname, len,
		sb->resultval, errno);
}

uae_u32 host_getsockopt(TrapContext* ctx, SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 optlen)
{
	socklen_t len = 0;
	int r;
	int s;
	int nativelevel = mapsockoptlevel(level);
	int nativeoptname = mapsockoptname(nativelevel, optname);
	void* buf = NULL;
	struct linger sl;
	struct timeval timeout;

	s = getsock(ctx, sb, sd + 1);

	if (s == INVALID_SOCKET) {
		bsdsocklib_seterrno(ctx, sb, 9); /* EBADF */
		return -1;
	}

	// Handle SO_EVENTMASK (0x2001) - Amiga-specific, no host equivalent
	if (level == 0xFFFF && optname == 0x2001) {
		if (optval && optlen) {
			int mask = sb->ftable[sd] & REP_ALL;
			put_long(optval, mask);
			put_long(optlen, 4);
		}
		bsdsocklib_seterrno(ctx, sb, 0);
		sb->resultval = 0;
		return 0;
	}

	if (optlen) {
		len = get_long(optlen);
		buf = malloc(len);
		if (buf == NULL) {
			return -1;
		}
	}

	if (nativeoptname == SO_RCVTIMEO || nativeoptname == SO_SNDTIMEO) {
		len = sizeof(timeout);
		r = getsockopt(s, nativelevel, nativeoptname, (char*)&timeout, &len);
	}
	else if (nativeoptname == SO_LINGER) {
		len = sizeof(sl);
		r = getsockopt(s, nativelevel, nativeoptname, (char*)&sl, &len);
	}
	else {
		r = getsockopt(s, nativelevel, nativeoptname, optval ? (char*)buf : NULL, optlen ? &len : NULL);
	}

	// Write back Amiga-appropriate optlen for size-mismatched types
	if (r == 0 && optlen) {
		if (nativeoptname == SO_RCVTIMEO || nativeoptname == SO_SNDTIMEO) {
			len = 8; // Amiga sizeof(struct timeval) = 4+4
		} else if (nativeoptname == SO_LINGER) {
			len = 8; // Amiga sizeof(struct linger) = 4+4
		}
	}

	if (optlen)
		put_long(optlen, len);

	SETERRNO;
	write_log("getsockopt: sock AmigaSide %d NativeSide %d, level %d, 'name' %x(%d), len %d -> %d, %d\n",
		sd, s, level, optname, nativeoptname, len, r, errno);

	if (optval) {
		if (r == 0) {
			if (nativeoptname == SO_RCVTIMEO || nativeoptname == SO_SNDTIMEO) {
				put_long(optval, timeout.tv_sec);
				put_long(optval + 4, timeout.tv_usec);
			}
			else if (nativeoptname == SO_LINGER) {
				put_long(optval, sl.l_onoff);
				put_long(optval + 4, sl.l_linger);
			}
			else {
				mapsockoptreturn(nativelevel, nativeoptname, optval, buf);
			}
		}
	}

	if (buf != NULL)
		free(buf);
	return r;
}

uae_u32 host_getsockname(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	int s;
	socklen_t len = sizeof (struct sockaddr_in);
	struct sockaddr_in addr{};

	write_log("getsockname(%u, 0x%x, %u) -> ", sd, name, len);

	s = getsock(ctx, sb, sd + 1);

	if (s != INVALID_SOCKET) {
		if (getsockname (s, (struct sockaddr *)&addr, &len)) {
			SETERRNO;
			write_log("failed (%d)\n", sb->sb_errno);
		} else {
			int a_nl;
			write_log("okay\n");
			a_nl = get_long (namelen);
			copysockaddr_n2a (name, &addr, a_nl);
			if (a_nl > 16)
				put_long (namelen, 16);
			return 0;
		}
	}

	return -1;
}

uae_u32 host_getpeername(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	int s;
	socklen_t len = sizeof (struct sockaddr_in);
	struct sockaddr_in addr{};

	write_log("getpeername(%u, 0x%x, %u) -> ", sd, name, len);

	s = getsock(ctx, sb, sd + 1);

	if (s != INVALID_SOCKET) {
		if (getpeername (s, (struct sockaddr *)&addr, &len)) {
			SETERRNO;
			write_log("failed (%d)\n", sb->sb_errno);
		} else {
			int a_nl;
			write_log("okay\n");
			a_nl = get_long (namelen);
			copysockaddr_n2a (name, &addr, a_nl);
			if (a_nl > 16)
				put_long (namelen, 16);
			return 0;
		}
	}

	return -1;
}

uae_u32 host_IoctlSocket(TrapContext *ctx, SB, uae_u32 sd, uae_u32 request, uae_u32 arg)
{
	sd++;
	int sock = getsock(ctx, sb, sd);
	int r, argval = get_long (arg);
	long flags;

	if (sock == INVALID_SOCKET) {
		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
		return -1;
	}

	if ((flags = fcntl (sock, F_GETFL)) == -1) {
		SETERRNO;
		return -1;
	}

	switch (request) {
	case 0x4004667B: /* FIOGETOWN */
		sb->ownertask = get_long (arg);
		return 0;

	case 0x8004667C: /* FIOSETOWN */
		trap_put_long(ctx, arg,sb->ownertask);
		return 0;
	case 0x8004667D: /* FIOASYNC */
#if defined(O_ASYNC)
		r = fcntl (sock, F_SETFL, argval ? flags | O_ASYNC : flags & ~O_ASYNC);
		return r;
#   else
		/* O_ASYNC is only available on Linux and BSD systems */
		return fcntl (sock, F_GETFL);
#   endif

	case 0x8004667E: /* FIONBIO */
	{
		r = fcntl (sock, F_SETFL, argval ?
			   flags | O_NONBLOCK : flags & ~O_NONBLOCK);
		if (argval) {
			sb->ftable[sd-1] &= ~SF_BLOCKING;
		} else {
			sb->ftable[sd-1] |= SF_BLOCKING;
		}
		return r;
	}

	case 0x4004667F: /* FIONREAD */
	{
		int nbytes = 0;
		r = ioctl(sock, FIONREAD, &nbytes);

		if (r >= 0) {
			put_long (arg, nbytes);
			return 0;
		}
		break;
	}

	case 0x80106921: /* SIOCGIFADDR */
	case 0x80106923: /* SIOCGIFDSTADDR */
	case 0x80106925: /* SIOCGIFBRDADDR */
	case 0x80106927: /* SIOCGIFNETMASK */
	case 0xc0206911: /* SIOCGIFFLAGS */
	{
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		// Read interface name from Amiga memory
		for (int i = 0; i < IFNAMSIZ - 1; i++) {
			ifr.ifr_name[i] = trap_get_byte(ctx, arg + i);
			if (ifr.ifr_name[i] == 0) break;
		}
		ifr.ifr_name[IFNAMSIZ - 1] = 0;
		r = ioctl(sock, request, &ifr);
		if (r >= 0) {
			// Write result back
			if (request == 0xc0206911) { // SIOCGIFFLAGS
				trap_put_word(ctx, arg + IFNAMSIZ, ifr.ifr_flags);
			} else { // Address IOCTLs
				copysockaddr_n2a(arg + IFNAMSIZ, (struct sockaddr_in*)&ifr.ifr_addr, 16);
			}
		}
		return r;
	}

	case 0xc0086924: /* SIOCGIFCONF */
	{
		struct ifconf ifc;
		char buf[1024];
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = buf;
		r = ioctl(sock, SIOCGIFCONF, &ifc);
		if (r >= 0) {
			// Write back the interface list
			trap_put_long(ctx, arg, ifc.ifc_len);
			uae_u32 bufptr = trap_get_long(ctx, arg + 4);
			if (bufptr) {
				for (int i = 0; i < ifc.ifc_len && i < (int)sizeof(buf);) {
					struct ifreq *ifr = (struct ifreq*)(buf + i);
					// Write interface name
					for (int j = 0; j < IFNAMSIZ; j++) {
						trap_put_byte(ctx, bufptr + i + j, ifr->ifr_name[j]);
					}
					// Write address
					copysockaddr_n2a(bufptr + i + IFNAMSIZ, (struct sockaddr_in*)&ifr->ifr_addr, 16);
					i += sizeof(struct ifreq);
				}
			}
		}
		return r;
	}

	} /* end switch */

	bsdsocklib_seterrno (ctx, sb, EINVAL);
	return -1;
}

int host_CloseSocket(TrapContext *ctx, SB, int sd)
{
	int s = getsock(ctx, sb, sd + 1);
	int retval;

	if (s == INVALID_SOCKET) {
		bsdsocklib_seterrno (ctx, sb, 9); /* EBADF */
		return -1;
	}

	/*
	if (checksd (sb, sd + 1) == 1) {
	return 0;
	}
	*/
	write_log("CloseSocket Amiga: %d, NativeSide %d\n", sd, s);

	// Unregister from event monitoring if registered
	unregister_socket_events(sb, sd);
	// Clear pending event flags to prevent stale GetSocketEvents on fd reuse
	sb->ftable[sd] &= ~SET_ALL;

	retval = close_socket (s);
	SETERRNO;
	releasesock (ctx, sb, sd + 1);
	return retval;
}

static void fd_zero(TrapContext *ctx, uae_u32 fdset, uae_u32 nfds)
{
	unsigned int i;
	for (i = 0; i < nfds; i += 32, fdset += 4)
		trap_put_long(ctx, fdset,0);
}

uae_u32 bsdthr_WaitSelect(SB)
{
	fd_set sets[3];
	int i, s, set, a_s, max;
	uae_u32 a_set;
	struct timeval tv {};
	int r;
	TrapContext* ctx = sb->context;

	int nfds = sb->nfds;
	if (nfds > sb->dtablesize) {
		write_log(_T("BSDSOCK: WaitSelect nfds (%d) exceeds dtablesize (%d), clamping\n"), nfds, sb->dtablesize);
		nfds = sb->dtablesize;
	}

	BSDTRACE((_T("WaitSelect: %d 0x%x 0x%x 0x%x 0x%x 0x%x\n"), sb->nfds, sb->sets[0], sb->sets[1], sb->sets[2], sb->timeout, sb->sigmp));

	if (sb->timeout)
		BSDTRACE((_T("WaitSelect: timeout %d %d\n"), get_long(sb->timeout), get_long(sb->timeout + 4)));

	FD_ZERO(&sets[0]);
	FD_ZERO(&sets[1]);
	FD_ZERO(&sets[2]);

	/* Set up the abort socket */
	FD_SET(sb->sockabort[0], &sets[0]);
	FD_SET(sb->sockabort[0], &sets[2]);
	max = sb->sockabort[0];

	for (set = 0; set < 3; set++) {
		if (sb->sets[set] != 0) {
			a_set = sb->sets[set];
			for (i = 0; i < nfds; i++) {
				if (bsd_amigaside_FD_ISSET(i, a_set)) {
					s = getsock(ctx, sb, i + 1);
					BSDTRACE((_T("WaitSelect: AmigaSide %d set. NativeSide %d.\n"), i, s));
					if (s == -1) {
						write_log(_T("BSDSOCK: WaitSelect() called with invalid descriptor %d in set %d.\n"), i, set);
					} else {
						FD_SET(s, &sets[set]);
						if (max < s) max = s;
					}
				}
			}
		}
	}

	max++;

	if (sb->timeout) {
		tv.tv_sec = get_long(sb->timeout);
		tv.tv_usec = get_long(sb->timeout + 4);
	}

	BSDTRACE((_T("Select going to select\n")));
	r = select(max, &sets[0], &sets[1], &sets[2], (sb->timeout == 0) ? NULL : &tv);
	BSDTRACE((_T("Select returns %d, errno is %d\n"), r, errno));
	if (r > 0) {
		/* Socket told us to abort */
		if (FD_ISSET(sb->sockabort[0], &sets[0])) {
			/* read from the pipe to reset it */
			BSDTRACE((_T("WaitSelect aborted from signal\n")));
			r = 0;
			for (set = 0; set < 3; set++)
				if (sb->sets[set] != 0)
					bsd_amigaside_FD_ZERO(sb->sets[set], nfds);
			clearsockabort(sb);
		}
		else
			/* This is perhaps slightly inefficient, but I don't care.. */
			for (set = 0; set < 3; set++) {
				a_set = sb->sets[set];
				if (a_set != 0) {
					bsd_amigaside_FD_ZERO(a_set, nfds);
					for (i = 0; i < nfds; i++) {
						a_s = getsock(ctx, sb, i + 1);
						if (!(a_s < 0)) {
							if (FD_ISSET(a_s, &sets[set])) {
								BSDTRACE((_T("WaitSelect: NativeSide %d set. AmigaSide %d.\n"), a_s, i));

								bsd_amigaside_FD_SET(i, a_set);
							}
						}
					}
				}
			}
	} else if (r == 0) {         /* Timeout. I think we're supposed to clear the sets.. */
		for (set = 0; set < 3; set++)
			if (sb->sets[set] != 0)
				bsd_amigaside_FD_ZERO(sb->sets[set], nfds);
	}
	BSDTRACE((_T("WaitSelect: r=%d errno=%d\n"), r, errno));
	return r;
}

void host_WaitSelect(TrapContext *ctx, SB, uae_u32 nfds, uae_u32 readfds, uae_u32 writefds, uae_u32 exceptfds, uae_u32 timeout, uae_u32 sigmp)
{
	uae_u32 wssigs = (sigmp) ? trap_get_long(ctx, sigmp) : 0;
	uae_u32 sigs;

	if (wssigs) {
		trap_call_add_dreg(ctx, 0, 0);
		trap_call_add_dreg(ctx, 1, wssigs);
		sigs = trap_call_lib(ctx, sb->sysbase, -0x132) & wssigs; // SetSignal()
		if (sigs) {
			put_long (sigmp, sigs);
			// Check for zero address -> otherwise WinUAE crashes
			if (readfds)
				fd_zero(ctx, readfds,nfds);
			if (writefds)
				fd_zero(ctx, writefds,nfds);
			if (exceptfds)
				fd_zero(ctx, exceptfds,nfds);
			sb->resultval = 0;
			bsdsocklib_seterrno (ctx, sb, 0);
			return;
		}
	}

	if (nfds == 0 && wssigs == 0 && timeout == 0) {
		/* Nothing to wait for: no sockets, no signals, no timeout */
		if (readfds)
			fd_zero(ctx, readfds, nfds);
		if (writefds)
			fd_zero(ctx, writefds, nfds);
		if (exceptfds)
			fd_zero(ctx, exceptfds, nfds);
		sb->resultval = 0;
		bsdsocklib_seterrno(ctx, sb, 0);
		return;
	}

	sb->nfds = nfds;
	sb->sets [0] = readfds;
	sb->sets [1] = writefds;
	sb->sets [2] = exceptfds;
	sb->timeout  = timeout;
	sb->sigmp    = wssigs;
	sb->action   = 5;

	uae_sem_post (&sb->sem);

	trap_call_add_dreg(ctx, 0, (((uae_u32)1) << sb->signal) | sb->eintrsigs | wssigs);
	sigs = trap_call_lib(ctx, sb->sysbase, -0x13e);	// Wait()

	if (sigmp)
		trap_put_long(ctx, sigmp, sigs & wssigs);

	if (sigs & wssigs) {
		/* Received the signals we were waiting on */
		BSDTRACE((_T("WaitSelect: got signal(s) %x\n"), sigs));


		if (!(sigs & (((uae_u32)1) << sb->signal))) {
			sockabort (sb);
			WAITSIGNAL;
			sb->resultval = 0;
		}
		/*
		if (readfds)
			fd_zero(ctx, readfds, nfds);
		if (writefds)
			fd_zero(ctx, writefds, nfds);
		if (exceptfds)
			fd_zero(ctx, exceptfds, nfds);
		*/

		bsdsocklib_seterrno (ctx, sb, 0);
	} else if (sigs & sb->eintrsigs) {
		/* Wait select was interrupted */
		BSDTRACE((_T("WaitSelect: interrupted\n")));

		if (!(sigs & (((uae_u32)1) << sb->signal))) {
			sockabort (sb);
			WAITSIGNAL;
		}

		sb->resultval = -1;
		bsdsocklib_seterrno (ctx, sb, mapErrno (EINTR));
	}
	clearsockabort(sb);
}

uae_u32 host_Inet_NtoA(TrapContext *ctx, SB, uae_u32 in)
{
	uae_char *addr;
	struct in_addr ina;
	uae_u32 scratchbuf;

	*(uae_u32 *)&ina = htonl(in);
	BSDTRACE((_T("Inet_NtoA(%x) -> "), in));

	if ((addr = inet_ntoa(ina)) != NULL) {
		scratchbuf = trap_get_areg(ctx, 6) + offsetof(struct UAEBSDBase, scratchbuf);
		strncpyha(ctx, scratchbuf, addr, SCRATCHBUFSIZE);
		if (ISBSDTRACE) {
			TCHAR *s = au(addr);
			BSDTRACE((_T("%s\n"), s));
			xfree(s);
		}
		return scratchbuf;
	}
	SETERRNO;
	BSDTRACE((_T("failed (%d)\n"), sb->sb_errno));
	return 0;
}

uae_u32 host_inet_addr(TrapContext *ctx, uae_u32 cp)
{
	uae_u32 addr;
	char *cp_rp;

	if (!trap_valid_address(ctx, cp, 4)) {
		return 0;
	}
	cp_rp = trap_get_alloc_string(ctx, cp, 256);
	addr = htonl(inet_addr(cp_rp));
	if (ISBSDTRACE) {
		TCHAR *s = au(cp_rp);
		BSDTRACE((_T("inet_addr(%s) -> 0x%08x\n"), s, addr));
		xfree(s);
	}
	xfree(cp_rp);
	return addr;
}

uae_u32 host_gethostname(TrapContext *ctx, uae_u32 name, uae_u32 namelen)
{
	uae_char buf[256];
	size_t len;

	if (!trap_valid_address(ctx, name, namelen)) {
		return -1;
	}
	if (gethostname(buf, sizeof buf) < 0) {
		return -1;
	}
	buf[sizeof buf - 1] = 0;
	len = strlen(buf) + 1;
	if (len > namelen) {
		len = namelen;
	}
	trap_put_bytes(ctx, buf, name, (int)len);
	return 0;
}

// --- Wrap getservbyname, getservbyport, getprotobyname, getprotobynumber with mutex ---

void host_getprotobyname (TrapContext *ctx, SB, uae_u32 name)
{
#if defined(__linux__)
	struct protoent *p = getprotobyname ((char *)get_real_address (name));
#else
	// Thread safety: protect non-reentrant getprotobyname
	std::lock_guard<std::mutex> lock(bsdsock_mutex);
	struct protoent *p = getprotobyname ((char *)get_real_address (name));
#endif
	write_log("Getprotobyname(%s) = %p\n", get_real_address (name), p);
	if (p == NULL) {
		SETHERRNO;
		SETERRNO;
		return;
	}
	copyProtoent(ctx, sb, p);
}

void host_getprotobynumber(TrapContext *ctx, SB, uae_u32 number)
{
#if defined(__linux__)
	struct protoent *p = getprotobynumber(number);
#else
	// Thread safety: protect non-reentrant getprotobynumber
	std::lock_guard<std::mutex> lock(bsdsock_mutex);
	struct protoent *p = getprotobynumber(number);
#endif
	write_log("getprotobynumber(%d) = %p\n", number, p);
	if (p == NULL) {
		SETHERRNO;
		SETERRNO;
		return;
	}
	copyProtoent(ctx, sb, p);
}

void host_getservbynameport(TrapContext *ctx, SB, uae_u32 nameport, uae_u32 proto, uae_u32 type)
{
	struct servent *s;
#if defined(__linux__)
	s = (type) ?
		getservbyport (htons((unsigned short)nameport), (char *)get_real_address (proto)) :
		getservbyname ((char *)get_real_address (nameport), (char *)get_real_address (proto));
#else
	// Thread safety: protect non-reentrant getservby* functions
	std::lock_guard<std::mutex> lock(bsdsock_mutex);
	s = (type) ?
		getservbyport (htons((unsigned short)nameport), (char *)get_real_address (proto)) :
		getservbyname ((char *)get_real_address (nameport), (char *)get_real_address (proto));
#endif
	int size;
	int numaliases = 0;
	uae_u32 aptr;
	int i;
	if (type) {
		write_log("Getservbyport(%d, %s) = %p\n", nameport, get_real_address (proto), s);
	} else {
		write_log("Getservbyname(%s, %s) = %p\n", get_real_address (nameport), get_real_address (proto), s);
	}
	if (s != NULL) {
		// compute total size of servent
		size = 20;
		if (s->s_name != NULL)
			size += strlen (s->s_name) + 1;
		if (s->s_proto != NULL)
			size += strlen (s->s_proto) + 1;

		if (s->s_aliases != NULL)
			while (s->s_aliases[numaliases])
				size += strlen (s->s_aliases[numaliases++]) + 5;

		if (sb->servent) {
			uae_FreeMem(ctx, sb->servent, sb->serventsize, sb->sysbase);
		}

		sb->servent = uae_AllocMem (ctx, size, 0, sb->sysbase);

		if (!sb->servent) {
			write_log ("BSDSOCK: WARNING - getservby%s() ran out of Amiga memory (couldn't allocate %d bytes)\n",type ? "port" : "name", size);
			bsdsocklib_seterrno (ctx, sb, 12); // ENOMEM
			return;
		}

		sb->serventsize = size;

		aptr = sb->servent + 20 + numaliases * 4;

		// transfer servent to Amiga memory
		trap_put_long(ctx, sb->servent + 4, sb->servent + 16);
		trap_put_long(ctx, sb->servent + 8, (unsigned short)htons (s->s_port));

		for (i = 0; i < numaliases; i++)
			trap_put_long(ctx, sb->servent + 16 + i * 4, addstr_ansi(ctx, &aptr, s->s_aliases[i]));
		trap_put_long(ctx, sb->servent + 16 + numaliases * 4, 0);
		trap_put_long(ctx, sb->servent, aptr);
		addstr_ansi(ctx, &aptr, s->s_name);
		trap_put_long(ctx, sb->servent + 12, aptr);
		addstr_ansi(ctx, &aptr, s->s_proto);

		bsdsocklib_seterrno (ctx, sb,0);
	} else {
		// Free previous allocation and clear so Amiga side returns NULL
		if (sb->servent) {
			uae_FreeMem(ctx, sb->servent, sb->serventsize, sb->sysbase);
		}
		sb->servent = 0;
		SETHERRNO;
		SETERRNO;
		return;
	}
}

void host_gethostbynameaddr (TrapContext *ctx, SB, uae_u32 name, uae_u32 namelen, long addrtype)
{
	sb->name      = name;
	sb->a_addrlen = namelen;
	sb->flags     = addrtype;
	if (addrtype == -1)
		sb->action  = 4;
	else
		sb->action = 7;

	uae_sem_post (&sb->sem);

	WAITSIGNAL;
}
