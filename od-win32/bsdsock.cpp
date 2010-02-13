/*
* UAE - The Un*x Amiga Emulator
*
* bsdsocket.library emulation - Win32 OS-dependent part
*
* Copyright 1997,98 Mathias Ortmann
* Copyright 1999,2000 Brian King
*
* GNU Public License
*
*/
#include <winsock2.h>

#include "sysconfig.h"
#include "sysdeps.h"

#if defined(BSDSOCKET)

#include "resource"

#include <stddef.h>
#include <process.h>

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "autoconf.h"
#include "traps.h"
#include "bsdsocket.h"

#include "threaddep/thread.h"
#include "registry.h"
#include "native2amiga.h"
#include "win32gui.h"
#include "wininet.h"
#include "mmsystem.h"
#include "win32.h"

static int hWndSelector = 0; /* Set this to zero to get hSockWnd */

struct threadargs {
	struct socketbase *sb;
	uae_u32 args1;
	uae_u32 args2;
	int args3;
	long args4;
	uae_char buf[MAXGETHOSTSTRUCT];
};

struct threadargsw {
	struct socketbase *sb;
	uae_u32 nfds;
	uae_u32 readfds;
	uae_u32 writefds;
	uae_u32 exceptfds;
	uae_u32 timeout;
};

#define MAX_SELECT_THREADS 64
#define MAX_GET_THREADS 64

struct bsdsockdata {
	HWND hSockWnd;
	HANDLE hSockThread;
	HANDLE hSockReq;
	HANDLE hSockReqHandled;
	CRITICAL_SECTION csSigQueueLock;
	CRITICAL_SECTION SockThreadCS;
	unsigned int threadid;
	WSADATA wsbData;

	volatile HANDLE hGetThreads[MAX_GET_THREADS];
	struct threadargs threadGetargs[MAX_GET_THREADS];
	volatile int threadGetargs_inuse[MAX_GET_THREADS];
	volatile HANDLE hGetEvents[MAX_GET_THREADS];

	volatile HANDLE hThreads[MAX_SELECT_THREADS];
	volatile struct threadargsw *threadargsw[MAX_SELECT_THREADS];
	volatile HANDLE hEvents[MAX_SELECT_THREADS];

	struct socketbase *asyncsb[MAXPENDINGASYNC];
	SOCKET asyncsock[MAXPENDINGASYNC];
	uae_u32 asyncsd[MAXPENDINGASYNC];
	int asyncindex;
};

static struct bsdsockdata *bsd;
static int threadindextable[MAX_GET_THREADS];

static unsigned int __stdcall sock_thread(void *);


extern HWND hAmigaWnd;

#define THREAD(func,arg) (HANDLE)_beginthreadex(NULL, 0, func, arg, 0, &bsd->threadid)
#define THREADEND(result) _endthreadex(result)

#define SETERRNO bsdsocklib_seterrno(sb, WSAGetLastError() - WSABASEERR)
#define SETHERRNO bsdsocklib_setherrno(sb, WSAGetLastError() - WSABASEERR)
#define WAITSIGNAL waitsig(context, sb)

#define SETSIGNAL addtosigqueue(sb,0)
#define CANCELSIGNAL cancelsig(context, sb)

#define FIOSETOWN _IOW('f', 124, long)   /* set owner (struct Task *) */
#define FIOGETOWN _IOR('f', 123, long)   /* get owner (struct Task *) */

#define BEGINBLOCKING if (sb->ftable[sd - 1] & SF_BLOCKING) sb->ftable[sd - 1] |= SF_BLOCKINGINPROGRESS
#define ENDBLOCKING sb->ftable[sd - 1] &= ~SF_BLOCKINGINPROGRESS

static LRESULT CALLBACK SocketWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

static int PASCAL WSAEventSelect(SOCKET,HANDLE,long);

#define PREPARE_THREAD EnterCriticalSection(&bsd->SockThreadCS)
#define TRIGGER_THREAD { SetEvent(bsd->hSockReq); WaitForSingleObject(bsd->hSockReqHandled, INFINITE); LeaveCriticalSection(&bsd->SockThreadCS); }

#define SOCKVER_MAJOR 2
#define SOCKVER_MINOR 2

#define SF_RAW_UDP 0x10000000
#define SF_RAW_RAW 0x20000000
#define SF_RAW_RUDP 0x08000000
#define SF_RAW_RICMP 0x04000000

typedef	struct ip_option_information {
	u_char Ttl;		/* Time To Live (used for traceroute) */
	u_char Tos; 	/* Type Of Service (usually 0) */
	u_char Flags; 	/* IP header flags (usually 0) */
	u_char OptionsSize; /* Size of options data (usually 0, max 40) */
	u_char FAR *OptionsData;   /* Options data buffer */
} IPINFO, *PIPINFO, FAR *LPIPINFO;

static void bsdsetpriority (HANDLE thread)
{
	int pri = THREAD_PRIORITY_NORMAL;
	SetThreadPriority(thread, pri);
}

static int mySockStartup(void)
{
	int result = 0, i;
	SOCKET dummy;
	DWORD lasterror;
	TCHAR *ss;

	if (!bsd) {
		bsd = xcalloc (struct bsdsockdata, 1);
		for (i = 0; i < MAX_GET_THREADS; i++)
			threadindextable[i] = i;
	}
	if (WSAStartup (MAKEWORD (SOCKVER_MAJOR, SOCKVER_MINOR), &bsd->wsbData)) {
		lasterror = WSAGetLastError();
		if(lasterror == WSAVERNOTSUPPORTED) {
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString(IDS_WSOCK2NEEDED, szMessage, MAX_DPATH);
			gui_message(szMessage);
		} else
			write_log (L"BSDSOCK: ERROR - Unable to initialize Windows socket layer! Error code: %d\n", lasterror);
		return 0;
	}

	ss = au (bsd->wsbData.szDescription);
	write_log (L"BSDSOCK: using %s\n", ss);
	xfree (ss);
	// make sure WSP/NSPStartup gets called from within the regular stack
	// (Windows 95/98 need this)
	if((dummy = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) != INVALID_SOCKET)  {
		closesocket(dummy);
		result = 1;
	} else {
		write_log (L"BSDSOCK: ERROR - WSPStartup/NSPStartup failed! Error code: %d\n",
			WSAGetLastError());
		result = 0;
	}

	return result;
}

int init_socket_layer (void)
{
	int result = 0;

	deinit_socket_layer ();
	if (currprefs.socket_emu) {
		if((result = mySockStartup())) {
			if(bsd->hSockThread == NULL) {
				WNDCLASS wc;    // Set up an invisible window and dummy wndproc

				InitializeCriticalSection(&bsd->csSigQueueLock);
				InitializeCriticalSection(&bsd->SockThreadCS);
				bsd->hSockReq = CreateEvent(NULL, FALSE, FALSE, NULL);
				bsd->hSockReqHandled = CreateEvent(NULL, FALSE, FALSE, NULL);

				wc.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
				wc.lpfnWndProc = SocketWindowProc;
				wc.cbClsExtra = 0;
				wc.cbWndExtra = 0;
				wc.hInstance = hInst;
				wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
				wc.hCursor = LoadCursor (NULL, IDC_ARROW);
				wc.hbrBackground = (HBRUSH)GetStockObject (BLACK_BRUSH);
				wc.lpszMenuName = 0;
				wc.lpszClassName = L"SocketFun";
				RegisterClass(&wc);
				bsd->hSockWnd = CreateWindowEx (0,
					L"SocketFun", L"WinUAE Socket Window",
					WS_POPUP,
					0, 0,
					1, 1,
					NULL, NULL, 0, NULL);
				bsd->hSockThread = THREAD(sock_thread, NULL);
				if (!bsd->hSockWnd) {
					write_log (L"bsdsocket initialization failed\n");
					deinit_socket_layer();
					return 0;
				}
			}
		}
	}
	return result;
}

static void close_selectget_threads(void)
{
	int i;

	for (i = 0; i < MAX_SELECT_THREADS; i++) {
		if (bsd->hEvents[i]) {
			HANDLE h = bsd->hEvents[i];
			bsd->hEvents[i] = NULL;
			CloseHandle (h);
		}
		if (bsd->hThreads[i]) {
			CloseHandle (bsd->hThreads[i]);
			bsd->hThreads[i] = NULL;
		}
	}

	for (i = 0; i < MAX_GET_THREADS; i++) {
		if (bsd->hGetThreads[i]) {
			HANDLE h = bsd->hGetThreads[i];
			bsd->hGetThreads[i] = NULL;
			CloseHandle (h);
		}
		if (bsd->hGetEvents[i]) {
			CloseHandle (bsd->hGetEvents[i]);
			bsd->hGetEvents[i] = NULL;
		}
		bsd->threadGetargs_inuse[i] = 0;
	}

}

void deinit_socket_layer(void)
{
	if (!bsd)
		return;
	if(bsd->hSockThread) {
		HANDLE t = bsd->hSockThread;
		DeleteCriticalSection(&bsd->csSigQueueLock);
		DeleteCriticalSection(&bsd->SockThreadCS);
		bsd->hSockThread = NULL;
		SetEvent (bsd->hSockReq);
		WaitForSingleObject(bsd->hSockThread, INFINITE);
		CloseHandle(t);
		CloseHandle(bsd->hSockReq);
		CloseHandle(bsd->hSockReqHandled);
		bsd->hSockReq = NULL;
		bsd->hSockThread = NULL;
		bsd->hSockReqHandled = NULL;
		DestroyWindow (bsd->hSockWnd);
		bsd->hSockWnd = NULL;
		UnregisterClass (L"SocketFun", hInst);
	}
	close_selectget_threads ();
	WSACleanup();
}

#ifdef BSDSOCKET

void locksigqueue(void)
{
	EnterCriticalSection(&bsd->csSigQueueLock);
}

void unlocksigqueue(void)
{
	LeaveCriticalSection(&bsd->csSigQueueLock);
}

// Asynchronous completion notification

// We use window messages posted to hAmigaWnd in the range from 0xb000 to 0xb000+MAXPENDINGASYNC*2
// Socket events cause even-numbered messages, task events odd-numbered messages
// Message IDs are allocated on a round-robin basis and deallocated by the main thread.

// WinSock tends to choke on WSAAsyncCancelMessage(s,w,m,0,0) called too often with an event pending

// @@@ Enabling all socket event messages for every socket by default and basing things on that would
// be cleaner (and allow us to write a select() emulation that doesn't need to be kludge-aborted).
// However, the latency of the message queue is too high for that at the moment (setting up a dummy
// window from a separate thread would fix that).

// Blocking sockets with asynchronous event notification are currently not safe to use.

int host_sbinit(TrapContext *context, SB)
{
	sb->sockAbort = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

	if (sb->sockAbort == INVALID_SOCKET)
		return 0;
	if ((sb->hEvent = CreateEvent(NULL,FALSE,FALSE,NULL)) == NULL)
		return 0;

	sb->mtable = xcalloc(unsigned int, sb->dtablesize);

	return 1;
}

void host_closesocketquick(SOCKET s)
{
	BOOL b = 1;

	if(s) {
		setsockopt(s, SOL_SOCKET, SO_DONTLINGER, (uae_char*)&b, sizeof(b));
		shutdown(s, 1);
		closesocket(s);
	}
}

void host_sbcleanup(SB)
{
	int i;

	if (!sb) {
		close_selectget_threads ();
		return;
	}

	for (i = 0; i < MAXPENDINGASYNC; i++) {
		if (bsd->asyncsb[i] == sb)
			bsd->asyncsb[i] = NULL;
	}

	if (sb->hEvent != NULL) {
		CloseHandle(sb->hEvent);
		sb->hEvent = NULL;
	}

	for (i = sb->dtablesize; i--; ) {
		if (sb->dtable[i] != INVALID_SOCKET)
			host_closesocketquick(sb->dtable[i]);
		sb->dtable[i] = INVALID_SOCKET;

		if (sb->mtable && sb->mtable[i])
			bsd->asyncsb[(sb->mtable[i] - 0xb000) / 2] = NULL;
	}

	shutdown(sb->sockAbort, 1);
	closesocket(sb->sockAbort);

	free(sb->mtable);
	sb->mtable = NULL;
}

void host_sbreset(void)
{
	int i;
	for (i = 0; i < MAXPENDINGASYNC; i++) {
		bsd->asyncsb[i] = 0;
		bsd->asyncsock[i] = 0;
		bsd->asyncsd[i] = 0;
	}
	for (i = 0; i < MAX_GET_THREADS; i++) {
		bsd->threadargsw[i] = 0;
	}
}

static void sockmsg(unsigned int msg, WPARAM wParam, LPARAM lParam)
{
	SB;
	unsigned int index;
	int sdi;

	index = (msg - 0xb000) / 2;
	sb = bsd->asyncsb[index];

	if (!(msg & 1))
	{
		// is this one really for us?
		if ((SOCKET)wParam != bsd->asyncsock[index])
		{
			// cancel socket event
			WSAAsyncSelect((SOCKET)wParam, hWndSelector ? hAmigaWnd : bsd->hSockWnd, 0, 0);
			return;
		}

		sdi = bsd->asyncsd[index] - 1;

		// asynchronous socket event?
		if (sb && !(sb->ftable[sdi] & SF_BLOCKINGINPROGRESS) && sb->mtable[sdi])
		{
			long wsbevents = WSAGETSELECTEVENT(lParam);
			int fmask = 0;

			// regular socket event?
			if (wsbevents & FD_READ) fmask = REP_READ;
			else if (wsbevents & FD_WRITE) fmask = REP_WRITE;
			else if (wsbevents & FD_OOB) fmask = REP_OOB;
			else if (wsbevents & FD_ACCEPT) fmask = REP_ACCEPT;
			else if (wsbevents & FD_CONNECT) fmask = REP_CONNECT;
			else if (wsbevents & FD_CLOSE) fmask = REP_CLOSE;

			// error?
			if (WSAGETSELECTERROR(lParam)) fmask |= REP_ERROR;

			// notify
			if (sb->ftable[sdi] & fmask) sb->ftable[sdi] |= fmask << 8;

			addtosigqueue(sb, 1);
			return;
		}
	}

	locksigqueue();

	if (sb != NULL) {

		bsd->asyncsb[index] = NULL;

		if (WSAGETASYNCERROR(lParam)) {
			bsdsocklib_seterrno(sb, WSAGETASYNCERROR(lParam) - WSABASEERR);
			if (sb->sb_errno >= 1001 && sb->sb_errno <= 1005) {
				bsdsocklib_setherrno(sb, sb->sb_errno - 1000);
			} else if (sb->sb_errno == 55) { // ENOBUFS
				write_log (L"BSDSOCK: ERROR - Buffer overflow - %d bytes requested\n",
					WSAGETASYNCBUFLEN(lParam));
			}
		} else {
			bsdsocklib_seterrno(sb,0);
		}

		SETSIGNAL;
	}

	unlocksigqueue();
}

static unsigned	int allocasyncmsg(SB,uae_u32 sd,SOCKET s)
{
	int i;

	locksigqueue();
	for (i = bsd->asyncindex + 1; i != bsd->asyncindex; i++) {
		if (i >= MAXPENDINGASYNC)
			i = 0;
		if (!bsd->asyncsb[i]) {
			bsd->asyncsb[i] = sb;
			if (++bsd->asyncindex >= MAXPENDINGASYNC)
				bsd->asyncindex = 0;
			unlocksigqueue();
			if (s == INVALID_SOCKET) {
				return i * 2 + 0xb001;
			} else {
				bsd->asyncsd[i] = sd;
				bsd->asyncsock[i] = s;
				return i * 2 + 0xb000;
			}
		}
	}
	unlocksigqueue();

	bsdsocklib_seterrno(sb, 12); // ENOMEM
	write_log (L"BSDSOCK: ERROR - Async operation completion table overflow\n");

	return 0;
}

static void cancelasyncmsg(TrapContext *context, unsigned int wMsg)
{
	SB;

	wMsg = (wMsg-0xb000) / 2;

	sb = bsd->asyncsb[wMsg];

	if (sb != NULL) {
		bsd->asyncsb[wMsg] = NULL;
		CANCELSIGNAL;
	}
}

void sockabort(SB)
{
	locksigqueue();

	unlocksigqueue();
}

void setWSAAsyncSelect(SB, uae_u32 sd, SOCKET s, long lEvent )
{
	if (sb->mtable[sd - 1]) {
		long wsbevents = 0;
		long eventflags;
		int i;
		locksigqueue();


		eventflags = sb->ftable[sd - 1]  & REP_ALL;

		if (eventflags & REP_ACCEPT)
			wsbevents |= FD_ACCEPT;
		if (eventflags & REP_CONNECT)
			wsbevents |= FD_CONNECT;
		if (eventflags & REP_OOB)
			wsbevents |= FD_OOB;
		if (eventflags & REP_READ)
			wsbevents |= FD_READ;
		if (eventflags & REP_WRITE)
			wsbevents |= FD_WRITE;
		if (eventflags & REP_CLOSE)
			wsbevents |= FD_CLOSE;
		wsbevents |= lEvent;
		i = (sb->mtable[sd - 1] - 0xb000) / 2;
		bsd->asyncsb[i] = sb;
		bsd->asyncsd[i] = sd;
		bsd->asyncsock[i] = s;
		WSAAsyncSelect(s, hWndSelector ? hAmigaWnd : bsd->hSockWnd, sb->mtable[sd - 1], wsbevents);

		unlocksigqueue();
	}
}

// address cleaning
static void prephostaddr(SOCKADDR_IN *addr)
{
	addr->sin_family = AF_INET;
}

static void prepamigaaddr(struct sockaddr *realpt, int len)
{
	// little endian address family value to the byte sin_family member
	((uae_u8*)realpt)[1] = *((uae_u8*)realpt);

	// set size of address
	*((uae_u8*)realpt) = len;
}


int host_dup2socket(SB, int fd1, int fd2)
{
	SOCKET s1,s2;

	BSDTRACE((L"dup2socket(%d,%d) -> ",fd1,fd2));
	fd1++;

	s1 = getsock(sb, fd1);
	if (s1 != INVALID_SOCKET) {
		if (fd2 != -1) {
			if ((unsigned int) (fd2) >= (unsigned int) sb->dtablesize)  {
				BSDTRACE ((L"Bad file descriptor (%d)\n", fd2));
				bsdsocklib_seterrno (sb, 9); /* EBADF */
			}
			fd2++;
			s2 = getsock(sb,fd2);
			if (s2 != INVALID_SOCKET) {
				shutdown(s2,1);
				closesocket(s2);
			}
			setsd(sb,fd2,s1);
			BSDTRACE((L"0\n"));
			return 0;
		} else {
			fd2 = getsd(sb, 1);
			setsd(sb,fd2,s1);
			BSDTRACE((L"%d\n",fd2));
			return (fd2 - 1);
		}
	}
	BSDTRACE((L"-1\n"));
	return -1;
}

int host_socket(SB, int af, int type, int protocol)
{
	int sd;
	SOCKET s;
	unsigned long nonblocking = 1;

	BSDTRACE((L"socket(%s,%s,%d) -> ",af == AF_INET ? "AF_INET" : "AF_other",type == SOCK_STREAM ? "SOCK_STREAM" : type == SOCK_DGRAM ? "SOCK_DGRAM " : "SOCK_RAW",protocol));

	if ((s = socket(af,type,protocol)) == INVALID_SOCKET) {
		SETERRNO;
		BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		return -1;
	} else
		sd = getsd(sb,s);

	sb->ftable[sd-1] = SF_BLOCKING;
	ioctlsocket(s,FIONBIO,&nonblocking);
	BSDTRACE((L"%d\n",sd));

	if (type == SOCK_RAW) {
		if (protocol==IPPROTO_UDP) {
			sb->ftable[sd-1] |= SF_RAW_UDP;
		}
		if (protocol==IPPROTO_ICMP) {
			struct sockaddr_in sin;

			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = INADDR_ANY;
			bind(s,(struct sockaddr *)&sin,sizeof(sin)) ;
		}
		if (protocol==IPPROTO_RAW) {
			sb->ftable[sd-1] |= SF_RAW_RAW;
		}
	}
	return sd-1;
}

uae_u32 host_bind(SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	uae_char buf[MAXADDRLEN];
	uae_u32 success = 0;
	SOCKET s;

	sd++;
	BSDTRACE((L"bind(%d,0x%lx,%d) -> ",sd, name, namelen));
	s = getsock(sb, sd);

	if (s != INVALID_SOCKET) {
		if (namelen <= sizeof buf) {
			if (!addr_valid (L"host_bind", name, namelen))
				return 0;
			memcpy(buf, get_real_address (name), namelen);

			// some Amiga programs set this field to bogus values
			prephostaddr((SOCKADDR_IN *)buf);

			if ((success = bind(s,(struct sockaddr *)buf, namelen)) != 0) {
				SETERRNO;
				BSDTRACE((L"failed (%d)\n",sb->sb_errno));
			} else
				BSDTRACE((L"OK\n"));
		} else
			write_log (L"BSDSOCK: ERROR - Excessive namelen (%d) in bind()!\n", namelen);
	}

	return success;
}

uae_u32 host_listen(SB, uae_u32 sd, uae_u32 backlog)
{
	SOCKET s;
	uae_u32 success = -1;

	sd++;
	BSDTRACE((L"listen(%d,%d) -> ", sd, backlog));
	s = getsock(sb, sd);

	if (s != INVALID_SOCKET) {
		if ((success = listen(s,backlog)) != 0) {
			SETERRNO;
			BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		} else
			BSDTRACE((L"OK\n"));
	}
	return success;
}

void host_accept(TrapContext *context, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	struct sockaddr *rp_name, *rp_nameuae;
	struct sockaddr sockaddr;
	int hlen, hlenuae = 0;
	SOCKET s, s2;
	int success = 0;
	unsigned int wMsg;

	sd++;
	if (name != 0) {
		if (!addr_valid (L"host_accept1", name, sizeof(struct sockaddr)) || !addr_valid (L"host_accept2", namelen, 4))
			return;
		rp_nameuae = rp_name = (struct sockaddr *)get_real_address (name);
		hlenuae = hlen = get_long (namelen);
		if (hlenuae < sizeof(sockaddr))
		{ // Fix for CNET BBS Windows must have 16 Bytes (sizeof(sockaddr)) otherwise Error WSAEFAULT
			rp_name = &sockaddr;
			hlen = sizeof(sockaddr);
		}
	} else {
		rp_name = &sockaddr;
		hlen = sizeof(sockaddr);
	}
	BSDTRACE((L"accept(%d,%d,%d) -> ",sd,name,hlenuae));

	s = getsock(sb, (int)sd);

	if (s != INVALID_SOCKET) {
		BEGINBLOCKING;

		s2 = accept(s, rp_name, &hlen);

		if (s2 == INVALID_SOCKET) {
			SETERRNO;

			if ((sb->ftable[sd - 1] & SF_BLOCKING) && sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR) {
				if (sb->mtable[sd - 1] || (wMsg = allocasyncmsg(sb, sd, s)) != 0) {
					if (sb->mtable[sd - 1] == 0) {
						WSAAsyncSelect(s,hWndSelector ? hAmigaWnd : bsd->hSockWnd, wMsg, FD_ACCEPT);
					} else {
						setWSAAsyncSelect(sb, sd, s, FD_ACCEPT);
					}

					WAITSIGNAL;

					if (sb->mtable[sd - 1] == 0) {
						cancelasyncmsg(context, wMsg);
					} else {
						setWSAAsyncSelect(sb, sd, s, 0);
					}

					if (sb->eintr) {
						BSDTRACE((L"[interrupted]\n"));
						ENDBLOCKING;
						return;
					}

					s2 = accept(s, rp_name, &hlen);

					if (s2 == INVALID_SOCKET) {
						SETERRNO;

						if (sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR)
							write_log (L"BSDSOCK: ERRRO - accept() would block despite FD_ACCEPT message\n");
					}
				}
			}
		}

		if (s2 == INVALID_SOCKET) {
			sb->resultval = -1;
			BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		} else {
			sb->resultval = getsd(sb, s2);
			sb->ftable[sb->resultval - 1] = sb->ftable[sd - 1]; // new socket inherits the old socket's properties
			sb->resultval--;
			if (rp_name != 0) { // 1.11.2002 XXX
				if (hlen <= hlenuae) { // Fix for CNET BBS Part 2
					prepamigaaddr(rp_name, hlen);
					if (namelen != 0) {
						put_long (namelen, hlen);
					}
				} else { // Copy only the number of bytes requested
					if (hlenuae != 0) {
						prepamigaaddr(rp_name, hlenuae);
						memcpy(rp_nameuae, rp_name, hlenuae);
						put_long (namelen, hlenuae);
					}
				}
			}
			BSDTRACE((L"%d/%d\n", sb->resultval, hlen));
		}

		ENDBLOCKING;
	}

}

typedef enum
{
	connect_req,
	recvfrom_req,
	sendto_req,
	abort_req,
	last_req
} threadsock_e;

struct threadsock_packet
{
	threadsock_e packet_type;
	union
	{
		struct sendto_params
		{
			uae_char *buf;
			uae_char *realpt;
			uae_u32 sd;
			uae_u32 msg;
			uae_u32 len;
			uae_u32 flags;
			uae_u32 to;
			uae_u32 tolen;
		} sendto_s;
		struct recvfrom_params
		{
			uae_char *realpt;
			uae_u32 addr;
			uae_u32 len;
			uae_u32 flags;
			struct sockaddr *rp_addr;
			int *hlen;
		} recvfrom_s;
		struct connect_params
		{
			uae_char *buf;
			uae_u32 namelen;
		} connect_s;
		struct abort_params
		{
			SOCKET *newsock;
		} abort_s;
	} params;
	SOCKET s;
	SB;
} sockreq;

// sockreg.sb may be gone if thread dies at right time.. fixme.. */

static BOOL HandleStuff(void)
{
	BOOL quit = FALSE;
	SB = NULL;
	BOOL handled = TRUE;

	if (bsd->hSockReq) {
		// 100ms sleepiness might need some tuning...
		//if(WaitForSingleObject( hSockReq, 100 ) == WAIT_OBJECT_0 )
		{
			switch(sockreq.packet_type)
			{
			case connect_req:
				sockreq.sb->resultval = connect(sockreq.s,(struct sockaddr *)(sockreq.params.connect_s.buf),sockreq.params.connect_s.namelen);
				break;
			case sendto_req:
				if(sockreq.params.sendto_s.to) {
					sockreq.sb->resultval = sendto(sockreq.s,sockreq.params.sendto_s.realpt,sockreq.params.sendto_s.len,sockreq.params.sendto_s.flags,(struct sockaddr *)(sockreq.params.sendto_s.buf),sockreq.params.sendto_s.tolen);
				} else {
					sockreq.sb->resultval = send(sockreq.s,sockreq.params.sendto_s.realpt,sockreq.params.sendto_s.len,sockreq.params.sendto_s.flags);
				}
				break;
			case recvfrom_req:
				if(sockreq.params.recvfrom_s.addr) {
					sockreq.sb->resultval = recvfrom(sockreq.s, sockreq.params.recvfrom_s.realpt, sockreq.params.recvfrom_s.len,
						sockreq.params.recvfrom_s.flags, sockreq.params.recvfrom_s.rp_addr,
						sockreq.params.recvfrom_s.hlen);

				} else {
					sockreq.sb->resultval = recv(sockreq.s, sockreq.params.recvfrom_s.realpt, sockreq.params.recvfrom_s.len,
						sockreq.params.recvfrom_s.flags);
				}
				break;
			case abort_req:
				*(sockreq.params.abort_s.newsock) = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
				if (*(sockreq.params.abort_s.newsock) != sb->sockAbort) {
					shutdown(sb->sockAbort, 1);
					closesocket(sb->sockAbort);
				}
				handled = FALSE; /* Don't bother the SETERRNO section after the switch() */
				break;
			case last_req:
			default:
				write_log (L"BSDSOCK: Invalid sock-thread request!\n");
				handled = FALSE;
				break;
			}
			if(handled) {
				if(sockreq.sb->resultval == SOCKET_ERROR) {
					sb = sockreq.sb;
					SETERRNO;
				}
			}
			SetEvent(bsd->hSockReqHandled);
		}
	} else {
		quit = TRUE;
	}
	return quit;
}

static LRESULT CALLBACK SocketWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if(message >= 0xB000 && message < 0xB000 + MAXPENDINGASYNC * 2) {
#if DEBUG_SOCKETS
		write_log ( "sockmsg(0x%x, 0x%x, 0x%x)\n", message, wParam, lParam );
#endif
		sockmsg(message, wParam, lParam);
		return 0;
	}
	return DefWindowProc(hwnd, message, wParam, lParam);
}



static unsigned int sock_thread2(void *blah)
{
	unsigned int result = 0;
	HANDLE WaitHandle;
	MSG msg;

	if(bsd->hSockWnd) {
		// Make sure we're outrunning the wolves
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

		while(bsd->hSockThread && bsd->hSockWnd) {
			DWORD wait;
			WaitHandle = bsd->hSockReq;
			wait = MsgWaitForMultipleObjects (1, &WaitHandle, FALSE, INFINITE, QS_POSTMESSAGE);
			if (wait == WAIT_ABANDONED_0)
				break;
			if (wait == WAIT_OBJECT_0) {
				if (!bsd->hSockThread || !bsd->hSockWnd)
					break;
				if (HandleStuff()) // See if its time to quit...
					break;
			} else if (wait == WAIT_OBJECT_0 + 1) {
				if (!bsd->hSockThread || !bsd->hSockWnd)
					break;
				while(PeekMessage(&msg, NULL, WM_USER, 0xB000 + MAXPENDINGASYNC * 2, PM_REMOVE)) {
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
		}
	}
	write_log (L"BSDSOCK: We have exited our sock_thread()\n");
	THREADEND(result);
	return result;
}

static unsigned int __stdcall sock_thread(void *p)
{
	__try {
		return sock_thread2 (p);
	} __except(WIN32_ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
	}
	return 0;
}

void host_connect(TrapContext *context, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	int success = 0;
	unsigned int wMsg;
	uae_char buf[MAXADDRLEN];

	sd++;
	BSDTRACE((L"connect(%d,0x%lx,%d) -> ", sd, name, namelen));

	if (!addr_valid (L"host_connect", name, namelen))
		return;

	s = getsock(sb,(int)sd);

	if (s != INVALID_SOCKET) {
		if (namelen <= MAXADDRLEN) {
			if (sb->mtable[sd-1] || (wMsg = allocasyncmsg(sb,sd,s)) != 0) {
				if (sb->mtable[sd-1] == 0) {
					WSAAsyncSelect(s, hWndSelector ? hAmigaWnd : bsd->hSockWnd, wMsg, FD_CONNECT);
				} else {
					setWSAAsyncSelect(sb, sd, s, FD_CONNECT);
				}

				BEGINBLOCKING;
				PREPARE_THREAD;

				memcpy(buf, get_real_address (name), namelen);
				prephostaddr((SOCKADDR_IN *)buf);

				sockreq.packet_type = connect_req;
				sockreq.s = s;
				sockreq.sb = sb;
				sockreq.params.connect_s.buf = buf;
				sockreq.params.connect_s.namelen = namelen;

				TRIGGER_THREAD;

				if (sb->resultval) {
					if (sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR) {
						if (sb->ftable[sd-1] & SF_BLOCKING) {
							bsdsocklib_seterrno(sb, 0);

							WAITSIGNAL;

							if (sb->eintr) {
								// Destroy socket to cancel abort, replace it with fake socket to enable proper closing.
								// This is in accordance with BSD behaviour.
								shutdown(s,1);
								closesocket(s);
								sb->dtable[sd-1] = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
							}
						} else {
							bsdsocklib_seterrno(sb, 36); // EINPROGRESS
						}
					} else {
						CANCELSIGNAL; // Cancel pending signal
					}
				}

				ENDBLOCKING;
				if (sb->mtable[sd-1] == 0) {
					cancelasyncmsg(context, wMsg);
				} else {
					setWSAAsyncSelect(sb,sd,s,0);
				}
			}
		} else
			write_log (L"BSDSOCK: WARNING - Excessive namelen (%d) in connect()!\n", namelen);
	}
	BSDTRACE((L"%d\n",sb->sb_errno));
}

void host_sendto (TrapContext *context, SB, uae_u32 sd, uae_u32 msg, uae_u32 len, uae_u32 flags, uae_u32 to, uae_u32 tolen)
{
	SOCKET s;
	char *realpt;
	unsigned int wMsg;
	uae_char buf[MAXADDRLEN];
	int iCut;

#ifdef TRACING_ENABLED
	if (to)
		BSDTRACE((L"sendto(%d,0x%lx,%d,0x%lx,0x%lx,%d) -> ",sd,msg,len,flags,to,tolen));
	else
		BSDTRACE((L"send(%d,0x%lx,%d,%d) -> ",sd,msg,len,flags));
#endif
	sd++;
	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (!addr_valid (L"host_sendto1", msg, 4))
			return;
		realpt = (char*)get_real_address (msg);

		if (to) {
			if (tolen > sizeof buf) {
				write_log (L"BSDSOCK: WARNING - Target address in sendto() too large (%d)!\n", tolen);
			} else {
				if (!addr_valid (L"host_sendto2", to, tolen))
					return;
				memcpy(buf, get_real_address (to), tolen);
				// some Amiga software sets this field to bogus values
				prephostaddr((SOCKADDR_IN *)buf);
			}
		}
		if (sb->ftable[sd-1] & SF_RAW_RAW) {
			if (*(realpt+9) == 0x1) { // ICMP
				struct sockaddr_in sin;
				shutdown(s,1);
				closesocket(s);
				s = socket(AF_INET,SOCK_RAW,IPPROTO_ICMP);

				sin.sin_family = AF_INET;
				sin.sin_addr.s_addr = INADDR_ANY;
				sin.sin_port = (unsigned short) (*(realpt+21)&0xff)*256 + (unsigned short) (*(realpt+20)&0xff);
				bind(s,(struct sockaddr *)&sin,sizeof(sin)) ;

				sb->dtable[sd-1] = s;
				sb->ftable[sd-1]&= ~SF_RAW_RAW;
				sb->ftable[sd-1]|= SF_RAW_RICMP;
			}
			if (*(realpt+9) == 0x11) { // UDP
				struct sockaddr_in sin;
				shutdown(s,1);
				closesocket(s);
				s = socket(AF_INET,SOCK_RAW,IPPROTO_UDP);

				sin.sin_family = AF_INET;
				sin.sin_addr.s_addr = INADDR_ANY;
				sin.sin_port = (unsigned short) (*(realpt+21)&0xff)*256 + (unsigned short) (*(realpt+20)&0xff);
				bind(s,(struct sockaddr *)&sin,sizeof(sin)) ;

				sb->dtable[sd-1] = s;
				sb->ftable[sd-1]&= ~SF_RAW_RAW;
				sb->ftable[sd-1]|= SF_RAW_RUDP;
			}
		}

		BEGINBLOCKING;

		for (;;) {

			PREPARE_THREAD;

			sockreq.packet_type = sendto_req;
			sockreq.s = s;
			sockreq.sb = sb;
			sockreq.params.sendto_s.realpt = realpt;
			sockreq.params.sendto_s.buf = buf;
			sockreq.params.sendto_s.sd = sd;
			sockreq.params.sendto_s.msg = msg;
			sockreq.params.sendto_s.len = len;
			sockreq.params.sendto_s.flags = flags;
			sockreq.params.sendto_s.to = to;
			sockreq.params.sendto_s.tolen = tolen;

			if (sb->ftable[sd - 1] & SF_RAW_UDP) {
				*(buf+2) = *(realpt+2);
				*(buf+3) = *(realpt+3);
				// Copy DST-Port
				iCut = 8;
				sockreq.params.sendto_s.realpt += iCut;
				sockreq.params.sendto_s.len -= iCut;
			}
			if (sb->ftable[sd - 1] & SF_RAW_RUDP) {
				int iTTL;
				iTTL = (int) *(realpt+8)&0xff;
				setsockopt(s,IPPROTO_IP,4,(char*) &iTTL,sizeof(iTTL));
				*(buf+2) = *(realpt+22);
				*(buf+3) = *(realpt+23);
				// Copy DST-Port
				iCut = 28;
				sockreq.params.sendto_s.realpt += iCut;
				sockreq.params.sendto_s.len -= iCut;
			}
			if (sb->ftable[sd - 1] & SF_RAW_RICMP) {
				int iTTL;
				iTTL = (int) *(realpt+8)&0xff;
				setsockopt(s,IPPROTO_IP,4,(char*) &iTTL,sizeof(iTTL));
				iCut = 20;
				sockreq.params.sendto_s.realpt += iCut;
				sockreq.params.sendto_s.len -= iCut;
			}

			TRIGGER_THREAD;
			if ((sb->ftable[sd - 1] & SF_RAW_UDP) || (sb->ftable[sd - 1] & SF_RAW_RUDP) || (sb->ftable[sd-1] & SF_RAW_RICMP)) {
				sb->resultval += iCut;
			}
			if (sb->resultval == -1) {
				if (sb->sb_errno != WSAEWOULDBLOCK - WSABASEERR || !(sb->ftable[sd - 1] & SF_BLOCKING))
					break;
			} else {
				realpt += sb->resultval;
				len -= sb->resultval;
				if (len <= 0)
					break;
				else
					continue;
			}

			if (sb->mtable[sd - 1] || (wMsg = allocasyncmsg(sb, sd, s)) != 0) {
				if (sb->mtable[sd - 1] == 0) {
					WSAAsyncSelect(s,hWndSelector ? hAmigaWnd : bsd->hSockWnd,wMsg,FD_WRITE);
				} else {
					setWSAAsyncSelect(sb, sd, s, FD_WRITE);
				}

				WAITSIGNAL;

				if (sb->mtable[sd-1] == 0) {
					cancelasyncmsg(context, wMsg);
				} else {
					setWSAAsyncSelect(sb, sd, s, 0);
				}

				if (sb->eintr) {
					BSDTRACE((L"[interrupted]\n"));
					return;
				}
			} else
				break;
		}

		ENDBLOCKING;

	} else
		sb->resultval = -1;

#ifdef TRACING_ENABLED
	if (sb->resultval == -1)
		BSDTRACE((L"failed (%d)\n",sb->sb_errno));
	else
		BSDTRACE((L"%d\n",sb->resultval));
#endif

}

void host_recvfrom(TrapContext *context, SB, uae_u32 sd, uae_u32 msg, uae_u32 len, uae_u32 flags, uae_u32 addr, uae_u32 addrlen)
{
	SOCKET s;
	uae_char *realpt;
	struct sockaddr *rp_addr = NULL;
	int hlen;
	unsigned int wMsg;
	int waitall, waitallgot;

#ifdef TRACING_ENABLED
	if (addr)
		BSDTRACE((L"recvfrom(%d,0x%lx,%d,0x%lx,0x%lx,%d) -> ",sd,msg,len,flags,addr,get_long (addrlen)));
	else
		BSDTRACE((L"recv(%d,0x%lx,%d,0x%lx) -> ",sd,msg,len,flags));
#endif
	sd++;
	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (!addr_valid (L"host_recvfrom1", msg, 4))
			return;
		realpt = (char*)get_real_address (msg);

		if (addr) {
			if (!addr_valid (L"host_recvfrom1", addrlen, 4))
				return;
			hlen = get_long (addrlen);
			if (!addr_valid (L"host_recvfrom2", addr, hlen))
				return;
			rp_addr = (struct sockaddr *)get_real_address (addr);
		}

		BEGINBLOCKING;

		waitall = flags & 0x40;
		flags &= ~0x40;
		waitallgot = 0;

		for (;;) {

			PREPARE_THREAD;

			sockreq.packet_type = recvfrom_req;
			sockreq.s = s;
			sockreq.sb = sb;
			sockreq.params.recvfrom_s.addr = addr;
			sockreq.params.recvfrom_s.flags = flags;
			sockreq.params.recvfrom_s.hlen = &hlen;
			sockreq.params.recvfrom_s.len = len;
			sockreq.params.recvfrom_s.realpt = realpt;
			sockreq.params.recvfrom_s.rp_addr = rp_addr;

			TRIGGER_THREAD;

			if (waitall) {
				if (sb->resultval > 0) {
					int l = sb->resultval;
					realpt += l;
					len -= l;
					waitallgot += l;
					if (len <= 0) {
						sb->resultval = waitallgot;
						break;
					} else {
						sb->sb_errno = WSAEWOULDBLOCK - WSABASEERR;
						sb->resultval = -1;
					}
				} else if (sb->resultval == 0) {
					sb->resultval = waitallgot;
				}
			}


			if (sb->resultval == -1) {
				if (sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR && (sb->ftable[sd-1] & SF_BLOCKING)) {
					if (sb->mtable[sd-1] || (wMsg = allocasyncmsg(sb,sd,s)) != 0) {
						if (sb->mtable[sd-1] == 0) {
							WSAAsyncSelect(s, hWndSelector ? hAmigaWnd : bsd->hSockWnd, wMsg, FD_READ|FD_CLOSE);
						} else {
							setWSAAsyncSelect(sb, sd, s, FD_READ|FD_CLOSE);
						}

						WAITSIGNAL;

						if (sb->mtable[sd-1] == 0) {
							cancelasyncmsg(context, wMsg);
						} else {
							setWSAAsyncSelect(sb, sd, s, 0);
						}

						if (sb->eintr) {
							BSDTRACE((L"[interrupted]\n"));
							return;
						}

					} else
						break;
				} else
					break;
			} else
				break;
		}

		ENDBLOCKING;

		if (addr) {
			prepamigaaddr(rp_addr,hlen);
			put_long (addrlen,hlen);
		}
	} else
		sb->resultval = -1;

#ifdef TRACING_ENABLED
	if (sb->resultval == -1)
		BSDTRACE((L"failed (%d)\n",sb->sb_errno));
	else
		BSDTRACE((L"%d\n",sb->resultval));
#endif

}

uae_u32 host_shutdown(SB, uae_u32 sd, uae_u32 how)
{
	SOCKET s;

	BSDTRACE((L"shutdown(%d,%d) -> ",sd,how));
	sd++;
	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (shutdown(s,how)) {
			SETERRNO;
			BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		} else {
			BSDTRACE((L"OK\n"));
			return 0;
		}
	}

	return -1;
}

void host_setsockopt(SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 len)
{
	SOCKET s;
	uae_char buf[MAXADDRLEN];

	BSDTRACE((L"setsockopt(%d,%d,0x%lx,0x%lx,%d) -> ",sd,(short)level,optname,optval,len));
	sd++;
	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (len > sizeof buf) {
			write_log (L"BSDSOCK: WARNING - Excessive optlen in setsockopt() (%d)\n", len);
			len = sizeof buf;
		}
		if (level == IPPROTO_IP && optname == 2) { // IP_HDRINCL emulated by icmp.dll
			sb->resultval = 0;
			return;
		}
		if (level == SOL_SOCKET && optname == SO_LINGER) {
			((LINGER *)buf)->l_onoff = get_long (optval);
			((LINGER *)buf)->l_linger = get_long (optval + 4);
		} else {
			if (len == 4)
				*(long *)buf = get_long (optval);
			else if (len == 2)
				*(short *)buf = get_word (optval);
			else
				write_log (L"BSDSOCK: ERROR - Unknown optlen (%d) in setsockopt(%d,%d)\n", len, level, optname);
		}

		// handle SO_EVENTMASK
		if (level == 0xffff && optname == 0x2001) {
			long wsbevents = 0;
			uae_u32 eventflags = get_long (optval);

			sb->ftable[sd-1] = (sb->ftable[sd-1] & ~REP_ALL) | (eventflags & REP_ALL);

			if (eventflags & REP_ACCEPT)
				wsbevents |= FD_ACCEPT;
			if (eventflags & REP_CONNECT)
				wsbevents |= FD_CONNECT;
			if (eventflags & REP_OOB)
				wsbevents |= FD_OOB;
			if (eventflags & REP_READ)
				wsbevents |= FD_READ;
			if (eventflags & REP_WRITE)
				wsbevents |= FD_WRITE;
			if (eventflags & REP_CLOSE)
				wsbevents |= FD_CLOSE;

			if (sb->mtable[sd-1] || (sb->mtable[sd-1] = allocasyncmsg(sb,sd,s))) {
				WSAAsyncSelect(s,hWndSelector ? hAmigaWnd : bsd->hSockWnd,sb->mtable[sd-1],wsbevents);
				sb->resultval = 0;
			} else
				sb->resultval = -1;
		} else
			sb->resultval = setsockopt(s,level,optname,buf,len);

		if (!sb->resultval) {
			BSDTRACE((L"OK\n"));
			return;
		} else
			SETERRNO;

		BSDTRACE((L"failed (%d)\n",sb->sb_errno));
	}
}

uae_u32 host_getsockopt(SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 optlen)
{
	SOCKET s;
	uae_char buf[MAXADDRLEN];
	int len = sizeof(buf);

	BSDTRACE((L"getsockopt(%d,%d,0x%lx,0x%lx,0x%lx) -> ",sd,(short)level,optname,optval,optlen));
	sd++;
	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (!getsockopt(s,level,optname,buf,&len)) {
			if (level == SOL_SOCKET && optname == SO_LINGER) {
				put_long (optval,((LINGER *)buf)->l_onoff);
				put_long (optval+4,((LINGER *)buf)->l_linger);
			} else {
				if (len == 4)
					put_long (optval,*(long *)buf);
				else if (len == 2)
					put_word (optval,*(short *)buf);
				else
					write_log (L"BSDSOCK: ERROR - Unknown optlen (%d) in setsockopt(%d,%d)\n", len, level, optname);
			}

			//			put_long (optlen,len); // some programs pass the	actual length instead of a pointer to the length, so...
			BSDTRACE((L"OK (%d,%d)\n",len,*(long *)buf));
			return 0;
		} else {
			SETERRNO;
			BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		}
	}

	return -1;
}

uae_u32 host_getsockname(SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	int len;
	struct sockaddr *rp_name;

	sd++;
	if (!addr_valid (L"host_getsockname1", namelen, 4))
		return -1;
	len = get_long (namelen);

	BSDTRACE((L"getsockname(%d,0x%lx,%d) -> ",sd,name,len));

	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (!addr_valid (L"host_getsockname2", name, len))
			return -1;
		rp_name = (struct sockaddr *)get_real_address (name);

		if (getsockname(s,rp_name,&len)) {
			SETERRNO;
			BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		} else {
			BSDTRACE((L"%d\n",len));
			prepamigaaddr(rp_name,len);
			put_long (namelen,len);
			return 0;
		}
	}

	return -1;
}

uae_u32 host_getpeername(SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	int len;
	struct sockaddr *rp_name;

	sd++;
	if (!addr_valid (L"host_getpeername1", namelen, 4))
		return -1;
	len = get_long (namelen);

	BSDTRACE((L"getpeername(%d,0x%lx,%d) -> ",sd,name,len));

	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		if (!addr_valid (L"host_getpeername2", name, len))
			return -1;
		rp_name = (struct sockaddr *)get_real_address (name);

		if (getpeername(s,rp_name,&len)) {
			SETERRNO;
			BSDTRACE((L"failed (%d)\n",sb->sb_errno));
		} else {
			BSDTRACE((L"%d\n",len));
			prepamigaaddr(rp_name,len);
			put_long (namelen,len);
			return 0;
		}
	}

	return -1;
}

uae_u32 host_IoctlSocket(TrapContext *context, SB, uae_u32 sd, uae_u32 request, uae_u32 arg)
{
	SOCKET s;
	uae_u32 data;
	int success = SOCKET_ERROR;

	BSDTRACE((L"IoctlSocket(%d,0x%lx,0x%lx) ",sd,request,arg));
	sd++;
	s = getsock(sb,sd);

	if (s != INVALID_SOCKET) {
		switch (request)
		{
		case FIOSETOWN:
			sb->ownertask = get_long (arg);
			success = 0;
			break;
		case FIOGETOWN:
			put_long (arg,sb->ownertask);
			success = 0;
			break;
		case FIONBIO:
			BSDTRACE((L"[FIONBIO] -> "));
			if (get_long (arg)) {
				BSDTRACE((L"nonblocking\n"));
				sb->ftable[sd-1] &= ~SF_BLOCKING;
			} else {
				BSDTRACE((L"blocking\n"));
				sb->ftable[sd-1] |= SF_BLOCKING;
			}
			success = 0;
			break;
		case FIONREAD:
			ioctlsocket(s,request,(u_long *)&data);
			BSDTRACE((L"[FIONREAD] -> %d\n",data));
			put_long (arg,data);
			success = 0;
			break;
		case FIOASYNC:
			if (get_long (arg)) {
				sb->ftable[sd-1] |= REP_ALL;

				BSDTRACE((L"[FIOASYNC] -> enabled\n"));
				if (sb->mtable[sd-1] || (sb->mtable[sd-1] = allocasyncmsg(sb,sd,s))) {
					WSAAsyncSelect(s,hWndSelector ? hAmigaWnd : bsd-> hSockWnd, sb->mtable[sd-1],
						FD_ACCEPT | FD_CONNECT | FD_OOB | FD_READ | FD_WRITE | FD_CLOSE);
					success = 0;
					break;
				}
			}
			else
				write_log (L"BSDSOCK: WARNING - FIOASYNC disabling unsupported.\n");

			success = -1;
			break;
		default:
			write_log (L"BSDSOCK: WARNING - Unknown IoctlSocket request: 0x%08lx\n", request);
			bsdsocklib_seterrno(sb, 22); // EINVAL
			break;
		}
	}

	return success;
}

int host_CloseSocket(TrapContext *context, SB, int sd)
{
	unsigned int wMsg;
	SOCKET s;

	BSDTRACE((L"CloseSocket(%d) -> ",sd));
	sd++;

	s = getsock(sb,sd);
	if (s != INVALID_SOCKET) {

		if (sb->mtable[sd-1]) {
			bsd->asyncsb[(sb->mtable[sd-1]-0xb000)/2] = NULL;
			sb->mtable[sd-1] = 0;
		}

		if (checksd(sb ,sd) == TRUE)
			return 0;

		BEGINBLOCKING;

		for (;;) {

			shutdown(s,1);
			if (!closesocket(s)) {
				releasesock(sb,sd);
				BSDTRACE((L"OK\n"));
				return 0;
			}

			SETERRNO;

			if (sb->sb_errno != WSAEWOULDBLOCK-WSABASEERR || !(sb->ftable[sd-1] & SF_BLOCKING))
				break;

			if ((wMsg = allocasyncmsg(sb,sd,s)) != 0) {
				WSAAsyncSelect(s,hWndSelector ? hAmigaWnd : bsd->hSockWnd,wMsg,FD_CLOSE);

				WAITSIGNAL;

				cancelasyncmsg(context, wMsg);

				if (sb->eintr) {
					BSDTRACE((L"[interrupted]\n"));
					break;
				}
			} else
				break;
		}

		ENDBLOCKING;
	}

	BSDTRACE((L"failed (%d)\n",sb->sb_errno));

	return -1;
}

// For the sake of efficiency, we do not malloc() the fd_sets here.
// 64 sockets should be enough for everyone.
static void makesocktable(SB, uae_u32 fd_set_amiga, struct fd_set *fd_set_win, int nfds, SOCKET addthis)
{
	int i, j;
	uae_u32 currlong, mask;
	SOCKET s;

	if (addthis != INVALID_SOCKET) {
		*fd_set_win->fd_array = addthis;
		fd_set_win->fd_count = 1;
	} else
		fd_set_win->fd_count = 0;

	if (!fd_set_amiga) {
		fd_set_win->fd_array[fd_set_win->fd_count] = INVALID_SOCKET;
		return;
	}

	if (nfds > sb->dtablesize) {
		write_log (L"BSDSOCK: ERROR - select()ing more sockets (%d) than socket descriptors available (%d)!\n", nfds, sb->dtablesize);
		nfds = sb->dtablesize;
	}

	for (j = 0; ; j += 32, fd_set_amiga += 4) {
		currlong = get_long (fd_set_amiga);

		mask = 1;

		for (i = 0; i < 32; i++, mask <<= 1) {
			if (i+j > nfds) {
				fd_set_win->fd_array[fd_set_win->fd_count] = INVALID_SOCKET;
				return;
			}

			if (currlong & mask) {
				s = getsock(sb,j+i+1);

				if (s != INVALID_SOCKET) {
					fd_set_win->fd_array[fd_set_win->fd_count++] = s;

					if (fd_set_win->fd_count >= FD_SETSIZE) {
						write_log (L"BSDSOCK: ERROR - select()ing more sockets (%d) than the hard-coded fd_set limit (%d) - please report\n", nfds, FD_SETSIZE);
						return;
					}
				}
			}
		}
	}

	fd_set_win->fd_array[fd_set_win->fd_count] = INVALID_SOCKET;
}

static void makesockbitfield(SB, uae_u32 fd_set_amiga, struct fd_set *fd_set_win, int nfds)
{
	int n, i, j, val, mask;
	SOCKET currsock;

	for (n = 0; n < nfds; n += 32) {
		val = 0;
		mask = 1;

		for (i = 0; i < 32; i++, mask <<= 1) {
			if ((currsock = getsock(sb, n+i+1)) != INVALID_SOCKET) {
				// Do not use sb->dtable directly because of Newsrog
				for (j = fd_set_win->fd_count; j--; ) {
					if (fd_set_win->fd_array[j] == currsock) {
						val |= mask;
						break;
					}
				}
			}
		}
		put_long (fd_set_amiga, val);
		fd_set_amiga += 4;
	}
}

static void fd_zero(uae_u32 fdset, uae_u32 nfds)
{
	unsigned int i;
	for (i = 0; i < nfds; i += 32, fdset += 4)
		put_long (fdset,0);
}

// This seems to be the only way of implementing a cancelable WinSock2 select() call... sigh.
static unsigned int thread_WaitSelect2(void *indexp)
{
	int index = *((int*)indexp);
	unsigned int result = 0, resultval;
	long nfds;
	uae_u32 readfds, writefds, exceptfds;
	uae_u32 timeout;
	struct fd_set readsocks, writesocks, exceptsocks;
	struct timeval tv;
	volatile struct threadargsw *args;

	SB;

	while (bsd->hEvents[index]) {
		if (WaitForSingleObject(bsd->hEvents[index], INFINITE) == WAIT_ABANDONED)
			break;
		if (bsd->hEvents[index] == NULL)
			break;

		if ((args = bsd->threadargsw[index]) != NULL) {
			sb = args->sb;
			nfds = args->nfds;
			readfds = args->readfds;
			writefds = args->writefds;
			exceptfds = args->exceptfds;
			timeout = args->timeout;

			// construct descriptor tables
			makesocktable(sb, readfds, &readsocks, nfds, sb->sockAbort);
			if (writefds)
				makesocktable(sb, writefds, &writesocks, nfds, INVALID_SOCKET);
			if (exceptfds)
				makesocktable(sb, exceptfds, &exceptsocks, nfds, INVALID_SOCKET);

			if (timeout) {
				tv.tv_sec = get_long (timeout);
				tv.tv_usec = get_long (timeout+4);
				BSDTRACE((L"(timeout: %d.%06d) ",tv.tv_sec,tv.tv_usec));
			}

			BSDTRACE((L"-> "));

			resultval = select(nfds+1, &readsocks, writefds ? &writesocks : NULL,
				exceptfds ? &exceptsocks : NULL, timeout ? &tv : 0);
			if (bsd->hEvents[index] == NULL)
				break;
			sb->resultval = resultval;
			if (sb->resultval == SOCKET_ERROR) {
				// select was stopped by sb->sockAbort
				if (readsocks.fd_count > 1) {
					makesocktable(sb, readfds, &readsocks, nfds, INVALID_SOCKET);
					tv.tv_sec = 0;
					tv.tv_usec = 10000;
					// Check for 10ms if data is available
					resultval = select(nfds+1, &readsocks, writefds ? &writesocks : NULL,exceptfds ? &exceptsocks : NULL,&tv);
					if (bsd->hEvents[index] == NULL)
						break;
					sb->resultval = resultval;
					if (sb->resultval == 0) { // Now timeout -> really no data available
						if (GetLastError() != 0) {
							sb->resultval = SOCKET_ERROR;
							// Set old resultval
						}
					}
				}
			}
			if (FD_ISSET(sb->sockAbort,&readsocks)) {
				if (sb->resultval != SOCKET_ERROR) {
					sb->resultval--;
				}
			} else {
				sb->needAbort = 0;
			}
			if (sb->resultval == SOCKET_ERROR) {
				SETERRNO;
				BSDTRACE((L"failed (%d) - ",sb->sb_errno));
				if (readfds)
					fd_zero(readfds,nfds);
				if (writefds)
					fd_zero(writefds,nfds);
				if (exceptfds)
					fd_zero(exceptfds,nfds);
			} else {
				if (readfds)
					makesockbitfield(sb,readfds,&readsocks,nfds);
				if (writefds)
					makesockbitfield(sb,writefds,&writesocks,nfds);
				if (exceptfds)
					makesockbitfield(sb,exceptfds,&exceptsocks,nfds);
			}

			SETSIGNAL;

			bsd->threadargsw[index] = NULL;
			SetEvent(sb->hEvent);
		}
	}
	write_log (L"BSDSOCK: thread_WaitSelect2 terminated\n");
	THREADEND(result);
	return result;
}

static unsigned int __stdcall thread_WaitSelect(void *p)
{
	__try {
		return thread_WaitSelect2 (p);
	} __except(WIN32_ExceptionFilter(GetExceptionInformation(), GetExceptionCode())) {
	}
	return 0;
}

void host_WaitSelect(TrapContext *context, SB, uae_u32 nfds, uae_u32 readfds, uae_u32 writefds, uae_u32 exceptfds, uae_u32 timeout, uae_u32 sigmp)
{
	uae_u32 sigs, wssigs;
	int i;
	struct threadargsw taw;

	wssigs = sigmp ? get_long (sigmp) : 0;

	BSDTRACE((L"WaitSelect(%d,0x%lx,0x%lx,0x%lx,0x%lx,0x%lx) ",
		nfds, readfds, writefds, exceptfds, timeout, wssigs));

	if (!readfds && !writefds && !exceptfds && !timeout && !wssigs) {
		sb->resultval = 0;
		BSDTRACE((L"-> [ignored]\n"));
		return;
	}
	if (wssigs) {
		m68k_dreg (regs,0) = 0;
		m68k_dreg (regs,1) = wssigs;
		sigs = CallLib (context, get_long (4),-0x132) & wssigs; // SetSignal()

		if (sigs) {
			BSDTRACE((L"-> [preempted by signals 0x%08lx]\n",sigs & wssigs));
			put_long (sigmp,sigs & wssigs);
			// Check for zero address -> otherwise WinUAE crashes
			if (readfds)
				fd_zero(readfds,nfds);
			if (writefds)
				fd_zero(writefds,nfds);
			if (exceptfds)
				fd_zero(exceptfds,nfds);
			sb->resultval = 0;
			bsdsocklib_seterrno(sb,0);
			return;
		}
	}
	if (nfds == 0) {
		// No sockets to check, only wait for signals
		if (wssigs != 0) {
			m68k_dreg (regs, 0) = wssigs;
			sigs = CallLib (context, get_long (4),-0x13e); // Wait()
			put_long (sigmp, sigs & wssigs);
		}

		if (readfds)
			fd_zero(readfds,nfds);
		if (writefds)
			fd_zero(writefds,nfds);
		if (exceptfds)
			fd_zero(exceptfds,nfds);
		sb->resultval = 0;
		return;
	}

	ResetEvent(sb->hEvent);

	sb->needAbort = 1;

	for (i = 0; i < MAX_SELECT_THREADS; i++) {
		if (bsd->hThreads[i] && !bsd->threadargsw[i])
			break;
	}

	if (i >= MAX_SELECT_THREADS) {
		for (i = 0; i < MAX_SELECT_THREADS; i++) {
			if (!bsd->hThreads[i]) {
				bsd->hEvents[i] = CreateEvent(NULL,FALSE,FALSE,NULL);
				bsd->hThreads[i] = THREAD(thread_WaitSelect, &threadindextable[i]);
				if (bsd->hEvents[i] == NULL || bsd->hThreads[i] == NULL) {
					bsd->hThreads[i] = 0;
					write_log (L"BSDSOCK: ERROR - Thread/Event creation failed - error code: %d\n",
						GetLastError());
					bsdsocklib_seterrno(sb,12); // ENOMEM
					sb->resultval = -1;
					return;
				}
				// this should improve responsiveness
				SetThreadPriority(bsd->hThreads[i], THREAD_PRIORITY_ABOVE_NORMAL);
				break;
			}
		}
	}

	if (i >= MAX_SELECT_THREADS)
		write_log (L"BSDSOCK: ERROR - Too many select()s\n");
	else {
		SOCKET newsock = INVALID_SOCKET;

		taw.sb = sb;
		taw.nfds = nfds;
		taw.readfds = readfds;
		taw.writefds = writefds;
		taw.exceptfds = exceptfds;
		taw.timeout = timeout;

		bsd->threadargsw[i] = &taw;

		SetEvent(bsd->hEvents[i]);

		m68k_dreg (regs, 0) = (((uae_u32)1) << sb->signal) | sb->eintrsigs | wssigs;
		sigs = CallLib (context, get_long (4), -0x13e);	// Wait()
		/*
		if ((1<<sb->signal) & sigs)
		{ // 2.3.2002/SR Fix for AmiFTP -> Thread is ready, no need to Abort
		sb->needAbort = 0;
		}
		*/
		if (sb->needAbort) {
			if ((newsock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) == INVALID_SOCKET)
				write_log (L"BSDSOCK: ERROR - Cannot create socket: %d\n", WSAGetLastError());
			shutdown(sb->sockAbort,1);
			if (newsock != sb->sockAbort) {
				shutdown(sb->sockAbort, 1);
				closesocket(sb->sockAbort);
			}
		}

		WaitForSingleObject(sb->hEvent, INFINITE);

		CANCELSIGNAL;

		if (newsock != INVALID_SOCKET)
			sb->sockAbort = newsock;

		if(sigmp) {
			put_long (sigmp,sigs & wssigs);

			if (sigs & sb->eintrsigs) {
				BSDTRACE((L"[interrupted]\n"));
				sb->resultval = -1;
				bsdsocklib_seterrno(sb,4); // EINTR
			} else if (sigs & wssigs) {
				BSDTRACE((L"[interrupted by signals 0x%08lx]\n",sigs & wssigs));
				if (readfds) fd_zero(readfds,nfds);
				if (writefds) fd_zero(writefds,nfds);
				if (exceptfds) fd_zero(exceptfds,nfds);
				bsdsocklib_seterrno(sb,0);
				sb->resultval = 0;
			}
			if (sb->resultval >= 0) {
				BSDTRACE((L"%d\n",sb->resultval));
			} else {
				BSDTRACE((L"%d errno %d\n",sb->resultval,sb->sb_errno));
			}
		} else
			BSDTRACE((L"%d\n",sb->resultval));
	}
}

uae_u32 host_Inet_NtoA(TrapContext *context, SB, uae_u32 in)
{
	uae_char *addr;
	struct in_addr ina;
	uae_u32 scratchbuf;

	*(uae_u32 *)&ina = htonl(in);

	BSDTRACE((L"Inet_NtoA(%lx) -> ",in));

	if ((addr = inet_ntoa(ina)) != NULL) {
		scratchbuf = m68k_areg (regs,6) + offsetof(struct UAEBSDBase,scratchbuf);
		strncpyha(scratchbuf,addr,SCRATCHBUFSIZE);
		BSDTRACE((L"%s\n",addr));
		return scratchbuf;
	} else
		SETERRNO;

	BSDTRACE((L"failed (%d)\n",sb->sb_errno));

	return 0;
}

uae_u32 host_inet_addr(uae_u32 cp)
{
	uae_u32 addr;
	char *cp_rp;

	if (!addr_valid (L"host_inet_addr", cp, 4))
		return 0;
	cp_rp = (char*)get_real_address (cp);

	addr = htonl(inet_addr(cp_rp));

	BSDTRACE((L"inet_addr(%s) -> 0x%08lx\n",cp_rp,addr));

	return addr;
}

int isfullscreen (void);
static BOOL CheckOnline(SB)
{
	DWORD dwFlags;
	BOOL bReturn = TRUE;

	if (InternetGetConnectedState(&dwFlags,0) == FALSE) { // Internet is offline
		if (InternetAttemptConnect(0) != ERROR_SUCCESS) { // Show Dialer window
			sb->sb_errno = 10001;
			sb->sb_herrno = 1;
			bReturn = FALSE;
			// No success or aborted
		}
		if (isfullscreen() > 0) {
			ShowWindow (hAmigaWnd, SW_RESTORE);
			SetActiveWindow(hAmigaWnd);
		}
	}
	return bReturn;
}

static unsigned int thread_get2 (void *indexp)
{
	int index = *((int*)indexp);
	unsigned int result = 0;
	struct threadargs *args;
	uae_u32 name;
	uae_u32 namelen;
	long addrtype;
	char *name_rp;
	SB;

	while (bsd->hGetEvents[index]) {

		if (WaitForSingleObject(bsd->hGetEvents[index], INFINITE) == WAIT_ABANDONED)
			break;
		if (bsd->hGetEvents[index] == NULL)
			break;

		if (bsd->threadGetargs_inuse[index] == -1)
			bsd->threadGetargs_inuse[index] = 0;

		if (bsd->threadGetargs_inuse[index]) {
			args = &bsd->threadGetargs[index];
			sb = args->sb;

			if (args->args1 == 0) {

				// gethostbyname or gethostbyaddr
				struct hostent *host;
				name = args->args2;
				namelen = args->args3;
				addrtype = args->args4;
				if (addr_valid (L"thread_get1", name, 1))
					name_rp = (char*)get_real_address (name);
				else
					name_rp = "";

				if (strchr (name_rp, '.') == 0 || CheckOnline(sb) == TRUE) {
					// Local Address or Internet Online ?
					if (addrtype == -1) {
						host = gethostbyname (name_rp);
					} else {
						host = gethostbyaddr (name_rp, namelen, addrtype);
					}
					if (bsd->threadGetargs_inuse[index] != -1) {
						// No CTRL-C Signal
						if (host == 0) {
							// Error occured
							SETERRNO;
							BSDTRACE((L"failed (%d) - ", sb->sb_errno));
						} else {
							bsdsocklib_seterrno(sb, 0);
							memcpy(args->buf, host, sizeof(HOSTENT));
						}
					}
				}

			} else if (args->args1 == 1) {

				// getprotobyname
				struct protoent  *proto;

				name = args->args2;
				if (addr_valid (L"thread_get2", name, 1))
					name_rp = (char*)get_real_address (name);
				else
					name_rp = "";
				proto = getprotobyname (name_rp);
				if (bsd->threadGetargs_inuse[index] != -1) { // No CTRL-C Signal
					if (proto == 0) {
						// Error occured
						SETERRNO;
						BSDTRACE((L"failed (%d) - ", sb->sb_errno));
					} else {
						bsdsocklib_seterrno(sb, 0);
						memcpy(args->buf, proto, sizeof(struct protoent));
					}
				}

			} else if (args->args1 == 2) {

				// getservbyport and getservbyname
				uae_u32 nameport;
				uae_u32 proto;
				uae_u32 type;
				char *proto_rp = 0;
				struct servent *serv;

				nameport = args->args2;
				proto = args->args3;
				type = args->args4;

				if (proto) {
					if (addr_valid (L"thread_get3", proto, 1))
						proto_rp = (char*)get_real_address (proto);
				}

				if (type) {
					serv = getservbyport(nameport, proto_rp);
				} else {
					if (addr_valid (L"thread_get4", nameport, 1))
						name_rp = (char*)get_real_address (nameport);
					serv = getservbyname(name_rp, proto_rp);
				}
				if (bsd->threadGetargs_inuse[index] != -1) {
					// No CTRL-C Signal
					if (serv == 0) {
						// Error occured
						SETERRNO;
						BSDTRACE((L"failed (%d) - ", sb->sb_errno));
					} else {
						bsdsocklib_seterrno(sb, 0);
						memcpy(args->buf, serv, sizeof (struct servent));
					}
				}
			}

			BSDTRACE((L"-> "));

			if (bsd->threadGetargs_inuse[index] != -1)
				SETSIGNAL;

			bsd->threadGetargs_inuse[index] = 0;

		}
	}
	write_log (L"BSDSOCK: thread_get2 terminated\n");
	THREADEND(result);
	return result;
}

static unsigned int __stdcall thread_get(void *p)
{
	__try {
		return thread_get2 (p);
	} __except(WIN32_ExceptionFilter(GetExceptionInformation(), GetExceptionCode())) {
	}
	return 0;
}

static volatile struct threadargs *run_get_thread(TrapContext *context, SB, struct threadargs *args)
{
	int i;

	for (i = 0; i < MAX_GET_THREADS; i++)  {
		if (bsd->threadGetargs_inuse[i] == -1) {
			bsd->threadGetargs_inuse[i] = 0;
		}
		if (bsd->hGetThreads[i] && !bsd->threadGetargs_inuse[i])
			break;
	}

	if (i >= MAX_GET_THREADS) {
		for (i = 0; i < MAX_GET_THREADS; i++) {
			if (bsd->hGetThreads[i] == NULL) {
				bsd->hGetEvents[i] = CreateEvent(NULL,FALSE,FALSE,NULL);
				bsd->hGetThreads[i] = THREAD(thread_get, &threadindextable[i]);
				if (bsd->hGetEvents[i] == NULL || bsd->hGetThreads[i] == NULL) {
					bsd->hGetThreads[i] = NULL;
					write_log (L"BSDSOCK: ERROR - Thread/Event creation failed - error code: %d\n",
						GetLastError());
					bsdsocklib_seterrno(sb, 12); // ENOMEM
					sb->resultval = -1;
					return 0;
				}
				break;
			}
		}
	}

	if (i >= MAX_GET_THREADS) {
		write_log (L"BSDSOCK: ERROR - Too many gethostbyname()s\n");
		bsdsocklib_seterrno(sb, 12); // ENOMEM
		sb->resultval = -1;
		return 0;
	} else {
		bsdsetpriority (bsd->hGetThreads[i]);
		memcpy (&bsd->threadGetargs[i], args, sizeof (struct threadargs));
		bsd->threadGetargs_inuse[i] = 1;
		SetEvent(bsd->hGetEvents[i]);
	}

	sb->eintr = 0;
	while (bsd->threadGetargs_inuse[i] != 0 && sb->eintr == 0) {
		WAITSIGNAL;
		if (sb->eintr == 1)
			bsd->threadGetargs_inuse[i] = -1;
	}
	CANCELSIGNAL;

	return &bsd->threadGetargs[i];
}

void host_gethostbynameaddr (TrapContext *context, SB, uae_u32 name, uae_u32 namelen, long addrtype)
{
	HOSTENT *h;
	int size, numaliases = 0, numaddr = 0;
	uae_u32 aptr;
	char *name_rp;
	int i;
	struct threadargs args;
	volatile struct threadargs *argsp;
	uae_u32 addr;
	uae_u32 *addr_list[2];
	volatile uae_char *buf;
	unsigned int wMsg = 0;

	//	TCHAR on	= 1;
	//	InternetSetOption(0,INTERNET_OPTION_SETTINGS_CHANGED,&on,strlen(&on));
	//  Do not use:	Causes locks with some machines

	memset(&args, 0, sizeof (args));
	argsp = &args;
	buf = argsp->buf;
	name_rp = "";

	if (addr_valid (L"host_gethostbynameaddr", name, 1))
		name_rp = (char*)get_real_address (name);

	if (addrtype == -1) {
		BSDTRACE((L"gethostbyname(%s) -> ",name_rp));

		// workaround for numeric host "names"
		if ((addr = inet_addr(name_rp)) != INADDR_NONE) {
			bsdsocklib_seterrno(sb,0);
			((HOSTENT *)buf)->h_name = name_rp;
			((HOSTENT *)buf)->h_aliases = NULL;
			((HOSTENT *)buf)->h_addrtype = AF_INET;
			((HOSTENT *)buf)->h_length = 4;
			((HOSTENT *)buf)->h_addr_list = (uae_char**)&addr_list;
			addr_list[0] = &addr;
			addr_list[1] = NULL;

			goto kludge;
		}
	} else {
		BSDTRACE((L"gethostbyaddr(0x%lx,0x%lx,%ld) -> ",name,namelen,addrtype));
	}

	argsp->sb = sb;
	argsp->args1 = 0;
	argsp->args2 = name;
	argsp->args3 = namelen;
	argsp->args4 = addrtype;

	argsp = run_get_thread(context, sb, &args);
	if (!argsp)
		return;
	buf = argsp->buf;

	if (!sb->sb_errno) {
kludge:
		h = (HOSTENT *)buf;

		// compute total size of hostent
		size = 28;
		if (h->h_name != NULL)
			size += strlen(h->h_name) + 1;

		if (h->h_aliases != NULL)
			while (h->h_aliases[numaliases])
				size += strlen(h->h_aliases[numaliases++]) + 5;

		if (h->h_addr_list != NULL) {
			while (h->h_addr_list[numaddr]) numaddr++;
			size += numaddr*(h->h_length + 4);
		}

		if (sb->hostent) {
			uae_FreeMem(context, sb->hostent, sb->hostentsize);
		}

		sb->hostent = uae_AllocMem(context, size, 0);

		if (!sb->hostent) {
			write_log (L"BSDSOCK: WARNING - gethostby%s() ran out of Amiga memory "
				L"(couldn't allocate %ld bytes) while returning result of lookup for '%s'\n",
				addrtype == -1 ? L"name" : L"addr", size, name_rp);
			bsdsocklib_seterrno(sb, 12); // ENOMEM
			return;
		}

		sb->hostentsize = size;

		aptr = sb->hostent + 28 + numaliases * 4 + numaddr * 4;

		// transfer hostent to Amiga memory
		put_long (sb->hostent + 4, sb->hostent + 20);
		put_long (sb->hostent + 8, h->h_addrtype);
		put_long (sb->hostent + 12, h->h_length);
		put_long (sb->hostent + 16, sb->hostent + 24 + numaliases * 4);

		for (i = 0; i < numaliases; i++)
			put_long (sb->hostent + 20 + i * 4, addstr_ansi (&aptr, h->h_aliases[i]));
		put_long (sb->hostent + 20 + numaliases * 4, 0);
		for (i = 0; i < numaddr; i++)
			put_long (sb->hostent + 24 + (numaliases + i) * 4, addmem (&aptr, h->h_addr_list[i], h->h_length));
		put_long (sb->hostent + 24 + numaliases * 4 + numaddr * 4, 0);
		put_long (sb->hostent, aptr);
		addstr_ansi (&aptr, h->h_name);

		BSDTRACE((L"OK (%s)\n", h->h_name));
		bsdsocklib_seterrno(sb, 0);
		bsdsocklib_setherrno(sb, 0);

	} else {
		BSDTRACE((L"failed (%d/%d)\n", sb->sb_errno, sb->sb_herrno));
	}

}

void host_getprotobyname(TrapContext *context, SB, uae_u32 name)
{
	PROTOENT *p;
	int size, numaliases = 0;
	uae_u32 aptr;
	char *name_rp;
	int i;
	struct threadargs args;
	volatile struct threadargs *argsp;

	name_rp = "";
	if (addr_valid (L"host_gethostbynameaddr", name, 1))
		name_rp = (char*)get_real_address (name);

	BSDTRACE((L"getprotobyname(%s) -> ",name_rp));

	memset(&args, 0, sizeof (args));
	argsp = &args;
	argsp->sb = sb;
	argsp->args1 = 1;
	argsp->args2 = name;

	argsp = run_get_thread(context, sb, &args);
	if (!argsp)
		return;

	if (!sb->sb_errno) {
		p = (PROTOENT *)argsp->buf;

		// compute total size of protoent
		size = 16;
		if (p->p_name != NULL)
			size += strlen(p->p_name)+1;

		if (p->p_aliases != NULL)
			while (p->p_aliases[numaliases]) size += strlen(p->p_aliases[numaliases++])+5;

		if (sb->protoent) {
			uae_FreeMem(context, sb->protoent, sb->protoentsize);
		}

		sb->protoent = uae_AllocMem(context, size, 0);

		if (!sb->protoent) {
			write_log (L"BSDSOCK: WARNING - getprotobyname() ran out of Amiga memory "
				L"(couldn't allocate %ld bytes) while returning result of lookup for '%s'\n",
				size, name_rp);
			bsdsocklib_seterrno(sb,12); // ENOMEM
			return;
		}

		sb->protoentsize = size;

		aptr = sb->protoent+16+numaliases*4;

		// transfer protoent to Amiga memory
		put_long (sb->protoent+4,sb->protoent+12);
		put_long (sb->protoent+8,p->p_proto);

		for (i = 0; i < numaliases; i++)
			put_long (sb->protoent + 12 + i * 4, addstr_ansi (&aptr, p->p_aliases[i]));
		put_long (sb->protoent + 12 + numaliases * 4,0);
		put_long (sb->protoent, aptr);
		addstr_ansi (&aptr, p->p_name);
		BSDTRACE((L"OK (%s, %d)\n", p->p_name, p->p_proto));
		bsdsocklib_seterrno (sb,0);

	} else {
		BSDTRACE((L"failed (%d)\n", sb->sb_errno));
	}

}

void host_getprotobynumber(TrapContext *context, SB, uae_u32 name)
{
	bsdsocklib_seterrno(sb, 1);
}

void host_getservbynameport(TrapContext *context, SB, uae_u32 nameport, uae_u32 proto, uae_u32 type)
{
	SERVENT *s;
	int size, numaliases = 0;
	uae_u32 aptr;
	TCHAR *name_rp = NULL, *proto_rp = NULL;
	int i;
	struct threadargs args;
	volatile struct threadargs *argsp;

	if (proto) {
		if (addr_valid (L"host_getservbynameport1", proto, 1))
			proto_rp = au ((char*)get_real_address (proto));
	}

	if (type) {
		BSDTRACE((L"getservbyport(%d,%s) -> ",nameport, proto_rp ? proto_rp : L"NULL"));
	} else {
		if (addr_valid (L"host_getservbynameport2", nameport, 1))
			name_rp = au ((char*)get_real_address (nameport));
		BSDTRACE((L"getservbyname(%s,%s) -> ",name_rp, proto_rp ? proto_rp : L"NULL"));
	}

	memset(&args, 0, sizeof (args));
	argsp = &args;
	argsp->sb = sb;
	argsp->args1 = 2;
	argsp->args2 = nameport;
	argsp->args3 = proto;
	argsp->args4 = type;

	argsp = run_get_thread (context, sb, &args);
	if (!argsp)
		return;

	if (!sb->sb_errno) {
		s = (SERVENT *)argsp->buf;

		// compute total size of servent
		size = 20;
		if (s->s_name != NULL)
			size += strlen(s->s_name)+1;
		if (s->s_proto != NULL)
			size += strlen(s->s_proto)+1;

		if (s->s_aliases != NULL)
			while (s->s_aliases[numaliases])
				size += strlen(s->s_aliases[numaliases++])+5;

		if (sb->servent) {
			uae_FreeMem(context, sb->servent, sb->serventsize);
		}

		sb->servent = uae_AllocMem(context, size, 0);

		if (!sb->servent) {
			write_log (L"BSDSOCK: WARNING - getservby%s() ran out of Amiga memory (couldn't allocate %ld bytes)\n", type ? "port" : "name", size);
			bsdsocklib_seterrno(sb, 12); // ENOMEM
			return;
		}

		sb->serventsize = size;

		aptr = sb->servent + 20 + numaliases * 4;

		// transfer servent to Amiga memory
		put_long (sb->servent + 4, sb->servent + 16);
		put_long (sb->servent + 8, (unsigned short)htons(s->s_port));

		for (i = 0; i < numaliases; i++)
			put_long (sb->servent + 16 + i * 4,addstr_ansi (&aptr,s->s_aliases[i]));
		put_long (sb->servent + 16 + numaliases * 4,0);
		put_long (sb->servent, aptr);
		addstr_ansi (&aptr, s->s_name);
		put_long (sb->servent + 12, aptr);
		addstr_ansi (&aptr, s->s_proto);

		BSDTRACE((L"OK (%s, %d)\n", s->s_name, (unsigned short)htons(s->s_port)));
		bsdsocklib_seterrno(sb, 0);

	} else {
		BSDTRACE((L"failed (%d)\n",sb->sb_errno));
	}

}

uae_u32 host_gethostname(uae_u32 name, uae_u32 namelen)
{
	if (!addr_valid (L"host_gethostname", name, namelen))
		return -1;
	return gethostname ((char*)get_real_address (name),namelen);
}

#endif

#endif
