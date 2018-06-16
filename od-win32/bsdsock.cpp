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
#include <Ws2tcpip.h>

#include "sysconfig.h"
#include "sysdeps.h"

#if defined(BSDSOCKET)

#include "resource.h"

#include <stddef.h>
#include <process.h>

#include "options.h"
#include "memory.h"
#include "uae/seh.h"
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
#include "dxwrap.h"

int rawsockets = 0;
static int hWndSelector = 0; /* Set this to zero to get hSockWnd */
static HWND hAmigaSockWnd;

struct threadargs {
	struct socketbase *sb;
	uae_u32 args1;
	uae_u32 args2;
	int args3;
	long args4;
	uae_char buf[MAXGETHOSTSTRUCT];
	int wscnt;
};

struct threadargsw {
	struct socketbase *sb;
	uae_u32 nfds;
	uae_u32 readfds;
	uae_u32 writefds;
	uae_u32 exceptfds;
	uae_u32 timeout;
	int wscnt;
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
	volatile struct threadargs *threadGetargs[MAX_GET_THREADS];
	volatile int threadGetargs_inuse[MAX_GET_THREADS];
	volatile HANDLE hGetEvents[MAX_GET_THREADS];
	volatile HANDLE hGetEvents2[MAX_GET_THREADS];

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

#define THREAD(func,arg) (HANDLE)_beginthreadex(NULL, 0, func, arg, 0, &bsd->threadid)
#define THREADEND(result) _endthreadex(result)

#define SETERRNO bsdsocklib_seterrno(ctx, sb, WSAGetLastError() - WSABASEERR)
#define SETHERRNO bsdsocklib_setherrno(ctx, sb, WSAGetLastError() - WSABASEERR)
#define WAITSIGNAL waitsig(ctx, sb)

#define SETSIGNAL addtosigqueue(sb,0)
#define CANCELSIGNAL cancelsig(ctx, sb)

#define FIOSETOWN _IOW('f', 124, long)   /* set owner (struct Task *) */
#define FIOGETOWN _IOR('f', 123, long)   /* get owner (struct Task *) */

#define BEGINBLOCKING if (sb->ftable[sd - 1] & SF_BLOCKING) sb->ftable[sd - 1] |= SF_BLOCKINGINPROGRESS
#define ENDBLOCKING sb->ftable[sd - 1] &= ~SF_BLOCKINGINPROGRESS

static LRESULT CALLBACK SocketWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

#define PREPARE_THREAD EnterCriticalSection(&bsd->SockThreadCS)
#define TRIGGER_THREAD { SetEvent(bsd->hSockReq); WaitForSingleObject(bsd->hSockReqHandled, INFINITE); LeaveCriticalSection(&bsd->SockThreadCS); }

#define SOCKVER_MAJOR 2
#define SOCKVER_MINOR 2

#define SF_RAW_RAW		0x10000000
#define SF_RAW_UDP		0x08000000
#define SF_RAW_RUDP		0x04000000
#define SF_RAW_RICMP	0x02000000
#define SF_RAW_HDR		0x01000000

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
			write_log (_T("BSDSOCK: ERROR - Unable to initialize Windows socket layer! Error code: %d\n"), lasterror);
		return 0;
	}

	ss = au (bsd->wsbData.szDescription);
	write_log (_T("BSDSOCK: using %s\n"), ss);
	xfree (ss);
	// make sure WSP/NSPStartup gets called from within the regular stack
	// (Windows 95/98 need this)
	if((dummy = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) != INVALID_SOCKET)  {
		closesocket(dummy);
		result = 1;
	} else {
		write_log (_T("BSDSOCK: ERROR - WSPStartup/NSPStartup failed! Error code: %d\n"),
			WSAGetLastError());
		result = 0;
	}

	return result;
}

int init_socket_layer (void)
{
	int result = 0;

	if (bsd)
		return -1;
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
				wc.lpszClassName = _T("SocketFun");
				RegisterClass(&wc);
				bsd->hSockWnd = CreateWindowEx (0,
					_T("SocketFun"), _T("WinUAE Socket Window"),
					WS_POPUP,
					0, 0,
					1, 1,
					NULL, NULL, 0, NULL);
				bsd->hSockThread = THREAD(sock_thread, NULL);
				if (!bsd->hSockWnd) {
					write_log (_T("bsdsocket initialization failed\n"));
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
		if (bsd->hGetEvents2[i]) {
			CloseHandle (bsd->hGetEvents2[i]);
			bsd->hGetEvents2[i] = NULL;
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
		UnregisterClass (_T("SocketFun"), hInst);
	}
	close_selectget_threads ();
	WSACleanup();
	xfree (bsd);
	bsd = NULL;
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
	TrapContext *ctx = NULL;

	index = (msg - 0xb000) / 2;
	sb = bsd->asyncsb[index];

	if (!(msg & 1))
	{
		// is this one really for us?
		if ((SOCKET)wParam != bsd->asyncsock[index])
		{
			// cancel socket event
			WSAAsyncSelect((SOCKET)wParam, hWndSelector ? hAmigaSockWnd : bsd->hSockWnd, 0, 0);
			BSDTRACE((_T("unknown sockmsg %d\n"), index));
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
			bsdsocklib_seterrno(NULL, sb, WSAGETASYNCERROR(lParam) - WSABASEERR);
			if (sb->sb_errno >= 1001 && sb->sb_errno <= 1005) {
				TrapContext *ctx = alloc_host_thread_trap_context();
				bsdsocklib_setherrno(ctx, sb, sb->sb_errno - 1000);
				free_host_trap_context(ctx);
			} else if (sb->sb_errno == 55) { // ENOBUFS
				write_log (_T("BSDSOCK: ERROR - Buffer overflow - %d bytes requested\n"),
					WSAGETASYNCBUFLEN(lParam));
			}
		} else {

			TrapContext *ctx = alloc_host_thread_trap_context();
			bsdsocklib_seterrno(ctx, sb,0);
			free_host_trap_context(ctx);
		}

		SETSIGNAL;
	}

	unlocksigqueue();
}

static unsigned	int allocasyncmsg(TrapContext *ctx, SB,uae_u32 sd,SOCKET s)
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

	bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
	write_log (_T("BSDSOCK: ERROR - Async operation completion table overflow\n"));

	return 0;
}

static void cancelasyncmsg(TrapContext *ctx, unsigned int wMsg)
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

static void setWSAAsyncSelect(SB, uae_u32 sd, SOCKET s, long lEvent )
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
		WSAAsyncSelect(s, hWndSelector ? hAmigaSockWnd : bsd->hSockWnd, sb->mtable[sd - 1], wsbevents);

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


int host_dup2socket(TrapContext *ctx, SB, int fd1, int fd2)
{
	SOCKET s1,s2;

	BSDTRACE((_T("dup2socket(%d,%d) -> "),fd1,fd2));
	fd1++;

	s1 = getsock(ctx, sb, fd1);
	if (s1 != INVALID_SOCKET) {
		if (fd2 != -1) {
			if ((unsigned int) (fd2) >= (unsigned int) sb->dtablesize)  {
				BSDTRACE ((_T("Bad file descriptor (%d)\n"), fd2));
				bsdsocklib_seterrno(ctx, sb, 9); /* EBADF */
			}
			fd2++;
			s2 = getsock(ctx, sb, fd2);
			if (s2 != INVALID_SOCKET) {
				shutdown(s2,1);
				closesocket(s2);
			}
			setsd(ctx, sb, fd2, s1);
			BSDTRACE((_T("0\n")));
			return 0;
		} else {
			fd2 = getsd(ctx, sb, 1);
			setsd(ctx, sb, fd2, s1);
			BSDTRACE((_T("%d\n"),fd2));
			return (fd2 - 1);
		}
	}
	BSDTRACE((_T("-1\n")));
	return -1;
}

int host_socket(TrapContext *ctx, SB, int af, int type, int protocol)
{
	int sd;
	SOCKET s;
	unsigned long nonblocking = 1;
	int faketype;

	BSDTRACE((_T("socket(%s,%s,%d) -> "),
		af == AF_INET ? _T("AF_INET") : _T("AF_other"),
		type == SOCK_STREAM ? _T("SOCK_STREAM") : type == SOCK_DGRAM ? _T("SOCK_DGRAM ") : _T("SOCK_RAW"),protocol));

	faketype = type;
	if (protocol == IPPROTO_UDP && type == SOCK_RAW && !rawsockets)
		faketype = SOCK_DGRAM;

	if ((s = socket(af,faketype,protocol)) == INVALID_SOCKET) {
		SETERRNO;
		BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		return -1;
	} else {
		sd = getsd(ctx, sb,s);
	}

	sb->ftable[sd-1] = SF_BLOCKING;
	if (faketype == SOCK_DGRAM || protocol != IPPROTO_TCP)
		sb->ftable[sd-1] |= SF_DGRAM;

	ioctlsocket(s,FIONBIO,&nonblocking);
	BSDTRACE((_T(" -> Socket=%d %x\n"),sd,s));

	if (type == SOCK_RAW) {
		if (protocol==IPPROTO_UDP) {
			sb->ftable[sd-1] |= SF_RAW_UDP;
		} else if (protocol==IPPROTO_ICMP) {
			struct sockaddr_in sin = { 0 };

			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = INADDR_ANY;
			if (bind(s,(struct sockaddr *)&sin,sizeof(sin)))
				write_log (_T("IPPROTO_ICMP socket bind() failed: %d\n"), WSAGetLastError ());
		} else if (protocol==IPPROTO_RAW) {
			sb->ftable[sd-1] |= SF_RAW_RAW;
		}
	}
	callfdcallback (ctx, sb, sd - 1, FDCB_ALLOC);
	return sd-1;
}

uae_u32 host_bind(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	uae_char buf[MAXADDRLEN];
	uae_u32 success = 0;
	SOCKET s;

	sd++;
	BSDTRACE((_T("bind(%d,0x%x,%d) -> "),sd, name, namelen));
	s = getsock(ctx, sb, sd);

	if (s != INVALID_SOCKET) {
		if (namelen <= sizeof buf) {
			if (!addr_valid (_T("host_bind"), name, namelen))
				return 0;
			memcpy(buf, get_real_address (name), namelen);

			// some Amiga programs set this field to bogus values
			prephostaddr((SOCKADDR_IN *)buf);

			if ((success = bind(s,(struct sockaddr *)buf, namelen)) != 0) {
				SETERRNO;
				BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
			} else
				BSDTRACE((_T("OK\n")));
		} else
			write_log (_T("BSDSOCK: ERROR - Excessive namelen (%d) in bind()!\n"), namelen);
	}

	return success;
}

uae_u32 host_listen(TrapContext *ctx, SB, uae_u32 sd, uae_u32 backlog)
{
	SOCKET s;
	uae_u32 success = -1;

	sd++;
	BSDTRACE((_T("listen(%d,%d) -> "), sd, backlog));
	s = getsock(ctx, sb, sd);

	if (s != INVALID_SOCKET) {
		if ((success = listen(s,backlog)) != 0) {
			SETERRNO;
			BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		} else
			BSDTRACE((_T("OK\n")));
	}
	return success;
}

void host_accept(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	struct sockaddr *rp_name, *rp_nameuae;
	struct sockaddr sockaddr;
	int hlen, hlenuae = 0;
	SOCKET s, s2;
	int success = 0;
	unsigned int wMsg;

	sd++;
	if (name != 0) {
		if (!trap_valid_address(ctx, name, sizeof(struct sockaddr)) || !trap_valid_address(ctx, namelen, 4))
			return;
		rp_nameuae = rp_name = (struct sockaddr *)get_real_address (name);
		hlenuae = hlen = trap_get_long(ctx, namelen);
		if (hlenuae < sizeof(sockaddr))
		{ // Fix for CNET BBS Windows must have 16 Bytes (sizeof(sockaddr)) otherwise Error WSAEFAULT
			rp_name = &sockaddr;
			hlen = sizeof(sockaddr);
		}
	} else {
		rp_name = &sockaddr;
		hlen = sizeof(sockaddr);
	}
	BSDTRACE((_T("accept(%d,%d,%d) -> "),sd,name,hlenuae));

	s = getsock(ctx, sb, (int)sd);

	if (s != INVALID_SOCKET) {
		BEGINBLOCKING;

		s2 = accept(s, rp_name, &hlen);

		if (s2 == INVALID_SOCKET) {
			SETERRNO;

			if ((sb->ftable[sd - 1] & SF_BLOCKING) && sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR) {
				if (sb->mtable[sd - 1] || (wMsg = allocasyncmsg(ctx, sb, sd, s)) != 0) {
					if (sb->mtable[sd - 1] == 0) {
						WSAAsyncSelect(s,hWndSelector ? hAmigaSockWnd : bsd->hSockWnd, wMsg, FD_ACCEPT);
					} else {
						setWSAAsyncSelect(sb, sd, s, FD_ACCEPT);
					}

					WAITSIGNAL;

					if (sb->mtable[sd - 1] == 0) {
						cancelasyncmsg(ctx, wMsg);
					} else {
						setWSAAsyncSelect(sb, sd, s, 0);
					}

					if (sb->eintr) {
						BSDTRACE((_T("[interrupted]\n")));
						ENDBLOCKING;
						return;
					}

					s2 = accept(s, rp_name, &hlen);

					if (s2 == INVALID_SOCKET) {
						SETERRNO;

						if (sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR)
							write_log (_T("BSDSOCK: ERRRO - accept() would block despite FD_ACCEPT message\n"));
					}
				}
			}
		}

		if (s2 == INVALID_SOCKET) {
			sb->resultval = -1;
			BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		} else {
			sb->resultval = getsd(ctx, sb, s2);
			sb->ftable[sb->resultval - 1] = sb->ftable[sd - 1]; // new socket inherits the old socket's properties
			callfdcallback(ctx, sb, sb->resultval - 1, FDCB_ALLOC);
			sb->resultval--;
			if (rp_name != 0) { // 1.11.2002 XXX
				if (hlen <= hlenuae) { // Fix for CNET BBS Part 2
					prepamigaaddr(rp_name, hlen);
					if (namelen != 0) {
						trap_put_long(ctx, namelen, hlen);
					}
				} else { // Copy only the number of bytes requested
					if (hlenuae != 0) {
						prepamigaaddr(rp_name, hlenuae);
						memcpy(rp_nameuae, rp_name, hlenuae);
						trap_put_long(ctx, namelen, hlenuae);
					}
				}
			}
			BSDTRACE((_T("%d/%d\n"), sb->resultval, hlen));
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
	int wscnt;
} sockreq;

// sockreg.sb may be gone if thread dies at right time.. fixme.. */

static BOOL HandleStuff(void)
{
	BOOL quit = FALSE;
	SB = NULL;
	BOOL handled = TRUE;
	TrapContext *ctx = NULL;

	if (bsd->hSockReq) {
		// 100ms sleepiness might need some tuning...
		//if(WaitForSingleObject( hSockReq, 100 ) == WAIT_OBJECT_0 )
		{
			BSDTRACE((_T("sockreq start %d:%d\n"), sockreq.packet_type,sockreq.wscnt));
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
				write_log (_T("BSDSOCK: Invalid sock-thread request!\n"));
				handled = FALSE;
				break;
			}
			if(handled) {
				if(sockreq.sb->resultval == SOCKET_ERROR) {
					sb = sockreq.sb;
					SETERRNO;
				}
			}
			BSDTRACE((_T("sockreq end %d,%d,%d:%d\n"), sockreq.packet_type,sockreq.sb->resultval,sockreq.sb->sb_errno,sockreq.wscnt));
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
		BSDTRACE((_T("sockmsg(0x%x[%d], 0x%x, 0x%x)\n"), message, (message - 0xb000) / 2, wParam, lParam));
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
	write_log (_T("BSDSOCK: We have exited our sock_thread()\n"));
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

void host_connect(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	int success = 0;
	unsigned int wMsg;
	uae_char buf[MAXADDRLEN];
	static int wscounter;
	int wscnt;

	sd++;
	wscnt = ++wscounter;

	BSDTRACE((_T("connect(%d,0x%x,%d):%d -> "), sd, name, namelen, wscnt));

	if (!addr_valid (_T("host_connect"), name, namelen))
		return;

	s = getsock(ctx, sb,(int)sd);

	if (s != INVALID_SOCKET) {
		if (namelen <= MAXADDRLEN) {
			if (sb->mtable[sd-1] || (wMsg = allocasyncmsg(ctx, sb,sd,s)) != 0) {
				if (sb->mtable[sd-1] == 0) {
					WSAAsyncSelect(s, hWndSelector ? hAmigaSockWnd : bsd->hSockWnd, wMsg, FD_CONNECT);
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
				sockreq.wscnt = wscnt;

				TRIGGER_THREAD;

				if (sb->resultval) {
					if (sb->sb_errno == WSAEWOULDBLOCK - WSABASEERR) {
						if (sb->ftable[sd-1] & SF_BLOCKING) {
							bsdsocklib_seterrno(ctx, sb, 0);

							WAITSIGNAL;

							if (sb->eintr) {
								// Destroy socket to cancel abort, replace it with fake socket to enable proper closing.
								// This is in accordance with BSD behaviour.
								shutdown(s,1);
								closesocket(s);
								sb->dtable[sd-1] = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
							}
						} else {
							bsdsocklib_seterrno(ctx,sb, 36); // EINPROGRESS
						}
					} else {
						CANCELSIGNAL; // Cancel pending signal
					}
				}

				ENDBLOCKING;
				if (sb->mtable[sd-1] == 0) {
					cancelasyncmsg(ctx, wMsg);
				} else {
					setWSAAsyncSelect(sb,sd,s,0);
				}
			}
		} else {
			write_log (_T("BSDSOCK: WARNING - Excessive namelen (%d) in connect():%d!\n"), namelen, wscnt);
		}
	}
	BSDTRACE((_T(" -> connect %d:%d\n"),sb->sb_errno, wscnt));
}


#if 0
struct ip {
	u_char	ip_v:4;			/*  0 version */
	u_char	ip_hl:4;		/*  0 header length */
	u_char	ip_tos;			/*  1 type of service */
	short	ip_len;			/*  2 total length */
	u_short	ip_id;			/*  4 identification */
	short	ip_off;			/*  6 fragment offset field */
	u_char	ip_ttl;			/*  8 time to live */
	u_char	ip_p;			/*  9 protocol */
	u_short	ip_sum;			/* 10 checksum */
	struct	in_addr ip_src,ip_dst;	/* 12 source and dest address */
};  /* 20 */
struct udphdr {
	u_short	uh_sport;		/* 20 source port */
	u_short	uh_dport;		/* 22 destination port */
	short	uh_ulen;		/* 24 udp length */
	u_short	uh_sum;			/* 26 udp checksum */
}; /* 28 */

#endif

void host_sendto (TrapContext *ctx, SB, uae_u32 sd, uae_u32 msg, uae_u8 *hmsg, uae_u32 len, uae_u32 flags, uae_u32 to, uae_u32 tolen)
{
	SOCKET s;
	char *realpt;
	unsigned int wMsg;
	uae_char buf[MAXADDRLEN];
	SOCKADDR_IN *sa = NULL;
	int iCut = 0;
	static int wscounter;
	int wscnt;

	wscnt = ++wscounter;


	if (to)
		BSDTRACE((_T("sendto(%d,0x%x,%p,%d,0x%x,0x%x,%d):%d-> "),sd,msg,hmsg,len,flags,to,tolen,wscnt));
	else
		BSDTRACE((_T("send(%d,0x%x,%p,%d,%d):%d -> "),sd,msg,hmsg,len,flags,wscnt));

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

		if (ISBSDTRACE) {
			write_log(_T("FT %08x "), sb->ftable[sd - 1]);
			for (int i = 0; i < 28; i++) {
				if (i > 0)
					write_log(_T("."));
				write_log(_T("%02X"), (uae_u8)realpt[i]);
			}
			write_log(_T(" -> "));
		}

		if (to) {
			if (tolen > sizeof buf) {
				write_log (_T("BSDSOCK: WARNING - Target address in sendto() too large (%d):%d!\n"), tolen,wscnt);
				sb->resultval = -1;
				bsdsocklib_seterrno(ctx, sb, 22); // EINVAL
				goto error;
			} else {
				if (!addr_valid (_T("host_sendto2"), to, tolen))
					return;
				memcpy(buf, get_real_address (to), tolen);
				// some Amiga software sets this field to bogus values
				sa = (SOCKADDR_IN*)buf;
				prephostaddr(sa);
			}
		}
		if (sb->ftable[sd-1] & SF_RAW_RAW) {
			if (realpt[9] == IPPROTO_ICMP) {
				struct sockaddr_in sin;

				shutdown(s,1);
				closesocket(s);
				s = socket(AF_INET,SOCK_RAW,IPPROTO_ICMP);

				sin.sin_family = AF_INET;
				sin.sin_addr.s_addr = INADDR_ANY;
				sin.sin_port = htons(realpt[20] * 256 + realpt[21]);
				bind(s,(struct sockaddr *)&sin,sizeof(sin));

				sb->dtable[sd-1] = s;
				sb->ftable[sd-1]&= ~SF_RAW_RAW;
				sb->ftable[sd-1]|= SF_RAW_RICMP;
			} else if (realpt[9] == IPPROTO_UDP) {
				struct sockaddr_in sin;

				shutdown(s,1);
				closesocket(s);
				s = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);

				sin.sin_family = AF_INET;
				sin.sin_addr.s_addr = INADDR_ANY;
				sin.sin_port = htons(realpt[20] * 256 + realpt[21]);
				bind(s,(struct sockaddr *)&sin,sizeof(sin));

				sb->dtable[sd-1] = s;
				sb->ftable[sd-1]&= ~SF_RAW_RAW;
				sb->ftable[sd-1]|= SF_RAW_RUDP;
			} else {
				write_log(_T("Unknown RAW protocol %d\n"), realpt[9]);
				sb->resultval = -1;
				bsdsocklib_seterrno(ctx, sb, 22); // EINVAL
				goto error;
			}
		}

		BEGINBLOCKING;

		for (;;) {

			PREPARE_THREAD;

			if (sb->ftable[sd - 1] & SF_RAW_UDP) {
				// Copy DST-Port
				sa->sin_port = htons(realpt[2] * 256 + realpt[3]);
				iCut = 8;
			} else if (sb->ftable[sd - 1] & SF_RAW_RUDP) {
				int iTTL = realpt[8];
				setsockopt(s,IPPROTO_IP,IP_TTL,(char*) &iTTL,sizeof(iTTL));
				// Copy DST-Port
				sa->sin_port = htons(realpt[22] * 256 + realpt[23]);
				iCut = 28;
			} else if (sb->ftable[sd - 1] & SF_RAW_RICMP) {
				int iTTL = realpt[8];
				setsockopt(s,IPPROTO_IP,IP_TTL,(char*) &iTTL,sizeof(iTTL));
				iCut = 20;
			}

			sockreq.params.sendto_s.realpt = realpt + iCut;
			sockreq.params.sendto_s.len = len - iCut;
			sockreq.packet_type = sendto_req;
			sockreq.s = s;
			sockreq.sb = sb;
			sockreq.params.sendto_s.buf = buf;
			sockreq.params.sendto_s.sd = sd;
			sockreq.params.sendto_s.flags = flags;
			sockreq.params.sendto_s.to = to;
			sockreq.params.sendto_s.tolen = tolen;
			sockreq.wscnt = wscnt;

			TRIGGER_THREAD;

			sb->resultval += iCut;

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

			if (sb->mtable[sd - 1] || (wMsg = allocasyncmsg(ctx, sb, sd, s)) != 0) {
				if (sb->mtable[sd - 1] == 0) {
					WSAAsyncSelect(s,hWndSelector ? hAmigaSockWnd : bsd->hSockWnd,wMsg,FD_WRITE);
				} else {
					setWSAAsyncSelect(sb, sd, s, FD_WRITE);
				}

				WAITSIGNAL;

				if (sb->mtable[sd-1] == 0) {
					cancelasyncmsg(ctx, wMsg);
				} else {
					setWSAAsyncSelect(sb, sd, s, 0);
				}

				if (sb->eintr) {
					BSDTRACE((_T("[interrupted]\n")));
					return;
				}
			} else
				break;
		}

		ENDBLOCKING;

	} else
		sb->resultval = -1;

error:
	if (sb->resultval == -1)
		BSDTRACE((_T("sendto failed (%d):%d\n"),sb->sb_errno,wscnt));
	else
		BSDTRACE((_T("sendto %d:%d\n"),sb->resultval,wscnt));
}

void host_recvfrom(TrapContext *ctx, SB, uae_u32 sd, uae_u32 msg, uae_u8 *hmsg, uae_u32 len, uae_u32 flags, uae_u32 addr, uae_u32 addrlen)
{
	SOCKET s;
	uae_char *realpt;
	struct sockaddr *rp_addr = NULL;
	int hlen;
	unsigned int wMsg;
	int waitall, waitallgot;
	static int wscounter;
	int wscnt;

	wscnt = ++wscounter;

	if (addr)
		BSDTRACE((_T("recvfrom(%d,0x%x,%p,%d,0x%x,0x%x,%d):%d -> "),sd,msg,hmsg,len,flags,addr,get_long (addrlen),wscnt));
	else
		BSDTRACE((_T("recv(%d,0x%x,%p,%d,0x%x):%d -> "),sd,msg,hmsg,len,flags,wscnt));

	sd++;
	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		if (hmsg == NULL) {
			if (!addr_valid (_T("host_recvfrom1"), msg, 4))
				return;
			realpt = (char*)get_real_address (msg);
		} else {
			realpt = (char*)hmsg;
		}

		if (addr) {
			if (!addr_valid (_T("host_recvfrom1"), addrlen, 4))
				return;
			hlen = get_long (addrlen);
			if (!addr_valid (_T("host_recvfrom2"), addr, hlen))
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
			sockreq.wscnt = wscnt;

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
					if (sb->mtable[sd-1] || (wMsg = allocasyncmsg(ctx, sb,sd,s)) != 0) {
						if (sb->mtable[sd-1] == 0) {
							WSAAsyncSelect(s, hWndSelector ? hAmigaSockWnd : bsd->hSockWnd, wMsg, FD_READ|FD_CLOSE);
						} else {
							setWSAAsyncSelect(sb, sd, s, FD_READ|FD_CLOSE);
						}

						WAITSIGNAL;

						if (sb->mtable[sd-1] == 0) {
							cancelasyncmsg(ctx, wMsg);
						} else {
							setWSAAsyncSelect(sb, sd, s, 0);
						}

						if (sb->eintr) {
							BSDTRACE((_T("[interrupted]\n")));
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

	if (sb->resultval == -1)
		BSDTRACE((_T("recv failed (%d):%d\n"),sb->sb_errno,wscnt));
	else
		BSDTRACE((_T("recv %d:%d\n"),sb->resultval,wscnt));
}

uae_u32 host_shutdown(SB, uae_u32 sd, uae_u32 how)
{
	TrapContext *ctx = NULL;
	SOCKET s;

	BSDTRACE((_T("shutdown(%d,%d) -> "),sd,how));
	sd++;
	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		if (shutdown(s,how)) {
			SETERRNO;
			BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		} else {
			BSDTRACE((_T("OK\n")));
			return 0;
		}
	}

	return -1;
}

void host_setsockopt(SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 len)
{
	TrapContext *ctx = NULL;
	SOCKET s;
	uae_char buf[MAXADDRLEN];
	int i;

	BSDTRACE((_T("setsockopt(%d,%d,0x%x,0x%x[0x%x],%d) -> "),sd,(short)level,optname,optval,get_long(optval),len));
	sd++;
	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		if (len > sizeof buf) {
			write_log (_T("BSDSOCK: WARNING - Excessive optlen in setsockopt() (%d)\n"), len);
			len = sizeof buf;
		}
#if 1
		if (level == IPPROTO_IP && optname == IP_HDRINCL) { // IP_HDRINCL emulated by icmp.dll
			sb->resultval = 0;
			return;
		}
#endif
		for (i = 0; i < len / 4; i++) {
			((long*)buf)[i] = get_long (optval + i * 4);
		}
		if (len - i == 2)
			((long*)buf)[i] = get_word (optval + i * 4);
		else if (len - i == 1)
			((long*)buf)[i] = get_byte (optval + i * 4);

		/* timeval -> milliseconds */
		if (level == SOL_SOCKET && (optname == SO_SNDTIMEO || optname == SO_RCVTIMEO)) {
			uae_u32 millis = ((long*)buf)[0] * 1000 + ((long*)buf)[1] / 1000;
			((long*)buf)[0] = millis;
			len = 4;
		}

		// handle SO_EVENTMASK
		if (level == SOL_SOCKET && optname == 0x2001) {
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

			if (sb->mtable[sd-1] || (sb->mtable[sd-1] = allocasyncmsg(ctx, sb,sd,s))) {
				WSAAsyncSelect(s,hWndSelector ? hAmigaSockWnd : bsd->hSockWnd,sb->mtable[sd-1],wsbevents);
				sb->resultval = 0;
			} else
				sb->resultval = -1;
		} else {
			sb->resultval = setsockopt(s,level,optname,buf,len);
		}

		if (!sb->resultval) {
			BSDTRACE((_T("OK\n")));
			return;
		} else
			SETERRNO;

		BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
	}
}

uae_u32 host_getsockopt(TrapContext *ctx, SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 optlen)
{
	SOCKET s;
	uae_char buf[MAXADDRLEN];
	int len = sizeof buf;
	uae_u32 outlen;

	if (optval)
		outlen = get_long (optlen);
	else
		outlen = 0;

	BSDTRACE((_T("getsockopt(%d,%d,0x%x,0x%x,0x%x[%d]) -> "),sd,(short)level,optname,optval,optlen,outlen));
	sd++;
	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		if (!getsockopt(s,level,optname,buf,&len)) {
			BSDTRACE((_T("0x%x, %d -> "), *((long*)buf), len));
			uae_u32 outcnt = 0;
			if (outlen) {
				if (level == SOL_SOCKET && (optname == SO_SNDTIMEO || optname == SO_RCVTIMEO)) {
					/* long milliseconds -> timeval */
					uae_u32 millis = *((long*)buf);
					((long*)buf)[0] = millis / 1000; /* secs */
					((long*)buf)[1] = (millis % 1000) * 1000; /* usecs */
					len = 8;
				}
				// len can equal 1 */
				for (int i = 0; i < len; i += 4) {
					uae_u32 v;
					if (len - i >= 4)
						v = *((long*)(buf + i));
					else if (len - i >= 2)
						v = *((short*)(buf + i));
					else
						v = buf[i];
					if (outlen >= 4) {
						put_long (optval + outcnt, v);
						outlen -= 4;
						outcnt += 4;
					} else if (outlen >= 2) {
						put_word (optval + outcnt, v);
						outlen -= 2;
						outcnt += 2;
					} else if (outlen > 0) {
						put_byte (optval + outcnt, v);
						outlen -= 1;
						outcnt += 1;
					}
				}
				put_long (optlen,outcnt);
			}
			BSDTRACE((_T("OK (%d,0x%x)\n"),outcnt,get_long(optval)));
			return 0;
		} else {
			SETERRNO;
			BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		}
	}

	return -1;
}

uae_u32 host_getsockname(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	int len;
	struct sockaddr *rp_name;

	sd++;
	if (!addr_valid (_T("host_getsockname1"), namelen, 4))
		return -1;
	len = get_long (namelen);

	BSDTRACE((_T("getsockname(%d,0x%x,%d) -> "),sd,name,len));

	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		if (!trap_valid_address(ctx, name, len))
			return -1;
		rp_name = (struct sockaddr *)get_real_address (name);

		if (getsockname(s,rp_name,&len)) {
			SETERRNO;
			BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		} else {
			BSDTRACE((_T("%d\n"),len));
			prepamigaaddr(rp_name,len);
			put_long (namelen,len);
			return 0;
		}
	}

	return -1;
}

uae_u32 host_getpeername(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	SOCKET s;
	int len;
	struct sockaddr *rp_name;

	sd++;
	if (!addr_valid (_T("host_getpeername1"), namelen, 4))
		return -1;
	len = get_long (namelen);

	BSDTRACE((_T("getpeername(%d,0x%x,%d) -> "),sd,name,len));

	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		if (!trap_valid_address(ctx, name, len))
			return -1;
		rp_name = (struct sockaddr *)get_real_address (name);

		if (getpeername(s,rp_name,&len)) {
			SETERRNO;
			BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));
		} else {
			BSDTRACE((_T("%d\n"),len));
			prepamigaaddr(rp_name,len);
			put_long (namelen,len);
			return 0;
		}
	}

	return -1;
}

uae_u32 host_IoctlSocket(TrapContext *ctx, SB, uae_u32 sd, uae_u32 request, uae_u32 arg)
{
	SOCKET s;
	uae_u32 data;
	int success = SOCKET_ERROR;

	BSDTRACE((_T("IoctlSocket(%d,0x%x,0x%x) "),sd,request,arg));
	sd++;
	s = getsock(ctx, sb,sd);

	if (s != INVALID_SOCKET) {
		switch (request)
		{
		case FIOSETOWN:
			sb->ownertask = trap_get_long(ctx, arg);
			success = 0;
			break;
		case FIOGETOWN:
			trap_put_long(ctx, arg,sb->ownertask);
			success = 0;
			break;
		case FIONBIO:
			BSDTRACE((_T("[FIONBIO] -> ")));
			if (trap_get_long(ctx, arg)) {
				BSDTRACE((_T("nonblocking\n")));
				sb->ftable[sd-1] &= ~SF_BLOCKING;
			} else {
				BSDTRACE((_T("blocking\n")));
				sb->ftable[sd-1] |= SF_BLOCKING;
			}
			success = 0;
			break;
		case FIONREAD:
			ioctlsocket(s,request,(u_long *)&data);
			BSDTRACE((_T("[FIONREAD] -> %d\n"),data));
			trap_put_long(ctx, arg,data);
			success = 0;
			break;
		case FIOASYNC:
			if (trap_get_long(ctx, arg)) {
				sb->ftable[sd-1] |= REP_ALL;

				BSDTRACE((_T("[FIOASYNC] -> enabled\n")));
				if (sb->mtable[sd-1] || (sb->mtable[sd-1] = allocasyncmsg(ctx, sb, sd, s))) {
					WSAAsyncSelect(s,hWndSelector ? hAmigaSockWnd : bsd-> hSockWnd, sb->mtable[sd-1],
						FD_ACCEPT | FD_CONNECT | FD_OOB | FD_READ | FD_WRITE | FD_CLOSE);
					success = 0;
					break;
				}
			}
			else
				write_log (_T("BSDSOCK: WARNING - FIOASYNC disabling unsupported.\n"));

			success = -1;
			break;
		default:
			write_log (_T("BSDSOCK: WARNING - Unknown IoctlSocket request: 0x%08lx\n"), request);
			bsdsocklib_seterrno(ctx, sb, 22); // EINVAL
			break;
		}
	}

	return success;
}

int host_CloseSocket(TrapContext *ctx, SB, int sd)
{
	unsigned int wMsg;
	SOCKET s;

	BSDTRACE((_T("CloseSocket(%d) -> "),sd));
	sd++;

	s = getsock(ctx, sb,sd);
	if (s != INVALID_SOCKET) {

		if (sb->mtable[sd-1]) {
			bsd->asyncsb[(sb->mtable[sd-1]-0xb000)/2] = NULL;
			sb->mtable[sd-1] = 0;
		}

		if (checksd(ctx, sb ,sd) == true)
			return 0;

		BEGINBLOCKING;

		for (;;) {

			shutdown(s,1);
			if (!closesocket(s)) {
				releasesock(ctx, sb, sd);
				BSDTRACE((_T("OK\n")));
				return 0;
			}

			SETERRNO;

			if (sb->sb_errno != WSAEWOULDBLOCK-WSABASEERR || !(sb->ftable[sd-1] & SF_BLOCKING))
				break;

			if ((wMsg = allocasyncmsg(ctx, sb, sd, s)) != 0) {
				WSAAsyncSelect(s,hWndSelector ? hAmigaSockWnd : bsd->hSockWnd,wMsg,FD_CLOSE);

				WAITSIGNAL;

				cancelasyncmsg(ctx, wMsg);

				if (sb->eintr) {
					BSDTRACE((_T("[interrupted]\n")));
					break;
				}
			} else
				break;
		}

		ENDBLOCKING;
	}

	BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));

	return -1;
}

// For the sake of efficiency, we do not malloc() the fd_sets here.
// 64 sockets should be enough for everyone.
static void makesocktable(TrapContext *ctx, SB, uae_u32 fd_set_amiga, struct fd_set *fd_set_win, int nfds, SOCKET addthis, const TCHAR *name)
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
		write_log (_T("BSDSOCK: ERROR - select()ing more sockets (%d) than socket descriptors available (%d)!\n"), nfds, sb->dtablesize);
		nfds = sb->dtablesize;
	}

	for (j = 0; ; j += 32, fd_set_amiga += 4) {
		currlong = trap_get_long (ctx, fd_set_amiga);

		mask = 1;

		for (i = 0; i < 32; i++, mask <<= 1) {
			if (i+j > nfds) {
				fd_set_win->fd_array[fd_set_win->fd_count] = INVALID_SOCKET;
				return;
			}

			if (currlong & mask) {
				s = getsock(ctx, sb,j+i+1);

				if (s != INVALID_SOCKET) {
					BSDTRACE((_T("%s:%d=%x\n"), name, fd_set_win->fd_count, s));
					fd_set_win->fd_array[fd_set_win->fd_count++] = s;

					if (fd_set_win->fd_count >= FD_SETSIZE) {
						write_log (_T("BSDSOCK: ERROR - select()ing more sockets (%d) than the hard-coded fd_set limit (%d) - please report\n"), nfds, FD_SETSIZE);
						return;
					}
				}
			}
		}
	}

	fd_set_win->fd_array[fd_set_win->fd_count] = INVALID_SOCKET;
}

static void makesockbitfield(TrapContext *ctx, SB, uae_u32 fd_set_amiga, struct fd_set *fd_set_win, int nfds)
{
	int n, i, j, val, mask;
	SOCKET currsock;

	for (n = 0; n < nfds; n += 32) {
		val = 0;
		mask = 1;

		for (i = 0; i < 32; i++, mask <<= 1) {
			if ((currsock = getsock(ctx, sb, n+i+1)) != INVALID_SOCKET) {
				// Do not use sb->dtable directly because of Newsrog
				for (j = fd_set_win->fd_count; j--; ) {
					if (fd_set_win->fd_array[j] == currsock) {
						val |= mask;
						break;
					}
				}
			}
		}
		trap_put_long(ctx, fd_set_amiga, val);
		fd_set_amiga += 4;
	}
}

static void fd_zero(TrapContext *ctx, uae_u32 fdset, uae_u32 nfds)
{
	unsigned int i;
	for (i = 0; i < nfds; i += 32, fdset += 4)
		trap_put_long(ctx, fdset,0);
}

// This seems to be the only way of implementing a cancelable WinSock2 select() call... sigh.
static unsigned int thread_WaitSelect2(void *indexp)
{
	int index = *((int*)indexp);
	unsigned int result = 0, resultval;
	int wscnt;
	long nfds;
	uae_u32 readfds, writefds, exceptfds;
	uae_u32 timeout;
	struct fd_set readsocks, writesocks, exceptsocks;
	struct timeval tv;
	volatile struct threadargsw *args;
	TrapContext *ctx = NULL;

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
			wscnt = args->wscnt;

			// construct descriptor tables
			makesocktable(ctx, sb, readfds, &readsocks, nfds, sb->sockAbort, _T("R"));
			if (writefds)
				makesocktable(ctx, sb, writefds, &writesocks, nfds, INVALID_SOCKET, _T("W"));
			if (exceptfds)
				makesocktable(ctx, sb, exceptfds, &exceptsocks, nfds, INVALID_SOCKET, _T("E"));

			if (timeout) {
				tv.tv_sec = get_long (timeout);
				tv.tv_usec = get_long (timeout+4);
				BSDTRACE((_T("(to: %d.%06d) "),tv.tv_sec,tv.tv_usec));
			}

			BSDTRACE((_T("tWS2(%d) -> "), wscnt));

			resultval = select(nfds+1,
				readsocks.fd_count > 0 ? &readsocks : NULL,
				writefds && writesocks.fd_count > 0 ? &writesocks : NULL,
				exceptfds && exceptsocks.fd_count > 0 ? &exceptsocks : NULL,
				timeout ? &tv : NULL);
			if (bsd->hEvents[index] == NULL)
				break;

			BSDTRACE((_T("tWS2(%d,%d) -> "), resultval, wscnt));
			if (resultval == 0) {
				BSDTRACE((_T("timeout -> ")));
			}

			sb->resultval = resultval;
			if (sb->resultval == SOCKET_ERROR) {
				// select was stopped by sb->sockAbort
				if (readsocks.fd_count > 1) {
					makesocktable(ctx, sb, readfds, &readsocks, nfds, INVALID_SOCKET, _T("R2"));
					tv.tv_sec = 0;
					tv.tv_usec = 10000;
					// Check for 10ms if data is available
					resultval = select(nfds+1, &readsocks, writefds ? &writesocks : NULL,exceptfds ? &exceptsocks : NULL,&tv);
					if (bsd->hEvents[index] == NULL)
						break;
					sb->resultval = resultval;

#if 0				/* what was this doing here? */
					if (sb->resultval == 0) { // Now timeout -> really no data available
						if (GetLastError() != 0) {
							sb->resultval = SOCKET_ERROR;
							// Set old resultval
						}
					}
#endif

				}
			}
			if (FD_ISSET(sb->sockAbort,&readsocks)) {
				BSDTRACE((_T("tWS2 abort %d:%d\n"), sb->resultval, wscnt));
				if (sb->resultval != SOCKET_ERROR) {
					sb->resultval--;
				}
			} else {
				sb->needAbort = 0;
			}
			if (sb->resultval == SOCKET_ERROR) {
				SETERRNO;
				BSDTRACE((_T("tWS2 failed %d:%d - "),sb->sb_errno,wscnt));
				if (readfds)
					fd_zero(ctx, readfds,nfds);
				if (writefds)
					fd_zero(ctx, writefds,nfds);
				if (exceptfds)
					fd_zero(ctx, exceptfds,nfds);
			} else {
				BSDTRACE((_T("tWS2 ok %d\n"), wscnt));
				if (readfds)
					makesockbitfield(ctx, sb,readfds,&readsocks,nfds);
				if (writefds)
					makesockbitfield(ctx, sb,writefds,&writesocks,nfds);
				if (exceptfds)
					makesockbitfield(ctx, sb,exceptfds,&exceptsocks,nfds);
			}

			SETSIGNAL;

			bsd->threadargsw[index] = NULL;
			SetEvent(sb->hEvent);
		}
	}
	write_log (_T("BSDSOCK: thread_WaitSelect2 terminated\n"));
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

static void fddebug(const TCHAR *name, uae_u32 nfds, uae_u32 fd)
{
	if (!ISBSDTRACE)
		return;
	if (!nfds)
		return;
	if (!fd)
		return;
	TCHAR out[40];
	uae_u32 v = get_long (fd);
	for (int i = 0; i < nfds && i < 32; i++) {
		out[i] = (v & (1 << i)) ? 'x' : '-';
		out[i + 1] = 0;
	}
	BSDTRACE((_T("%s: %08x %s\n"), name, v, out));
}

void host_WaitSelect(TrapContext *ctx, SB, uae_u32 nfds, uae_u32 readfds, uae_u32 writefds, uae_u32 exceptfds, uae_u32 timeout, uae_u32 sigmp)
{
	static int wscount;
	uae_u32 sigs, wssigs;
	int i;
	struct threadargsw taw;
	int wscnt;

	wscnt = ++wscount;

	wssigs = sigmp ? trap_get_long(ctx, sigmp) : 0;

	BSDTRACE((_T("WaitSelect(%d,0x%x,0x%x,0x%x,0x%x,0x%x):%d\n"),
		nfds, readfds, writefds, exceptfds, timeout, wssigs, wscnt));
	fddebug(_T("read  :"), nfds, readfds);
	fddebug(_T("write :"), nfds, writefds);
	fddebug(_T("except:"), nfds, exceptfds);

	if (!readfds && !writefds && !exceptfds && !timeout && !wssigs) {
		sb->resultval = 0;
		BSDTRACE((_T("-> [ignored]\n")));
		return;
	}
	if (wssigs) {
		trap_call_add_dreg(ctx ,0, 0);
		trap_call_add_dreg(ctx, 1, wssigs);
		sigs = trap_call_lib(ctx, sb->sysbase, -0x132) & wssigs; // SetSignal()

		if (sigs) {
			BSDTRACE((_T("-> [preempted by signals 0x%08lx]\n"),sigs & wssigs));
			put_long (sigmp,sigs & wssigs);
			// Check for zero address -> otherwise WinUAE crashes
			if (readfds)
				fd_zero(ctx, readfds,nfds);
			if (writefds)
				fd_zero(ctx, writefds,nfds);
			if (exceptfds)
				fd_zero(ctx, exceptfds,nfds);
			sb->resultval = 0;
			bsdsocklib_seterrno(ctx, sb,0);
			return;
		}
	}
	if (nfds == 0) {
		// No sockets to check, only wait for signals
		if (wssigs != 0) {
			trap_call_add_dreg(ctx, 0, wssigs);
			sigs = trap_call_lib(ctx, sb->sysbase, -0x13e); // Wait()
			trap_put_long(ctx, sigmp, sigs & wssigs);
		}

		if (readfds)
			fd_zero(ctx, readfds,nfds);
		if (writefds)
			fd_zero(ctx, writefds,nfds);
		if (exceptfds)
			fd_zero(ctx, exceptfds,nfds);
		sb->resultval = 0;
		return;
	}

	ResetEvent(sb->hEvent);

	sb->needAbort = 1;

	locksigqueue ();

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
					unlocksigqueue ();
					write_log (_T("BSDSOCK: ERROR - Thread/Event creation failed - error code: %d\n"),
						GetLastError());
					bsdsocklib_seterrno(ctx, sb,12); // ENOMEM
					sb->resultval = -1;
					return;
				}
				// this should improve responsiveness
				SetThreadPriority(bsd->hThreads[i], THREAD_PRIORITY_ABOVE_NORMAL);
				break;
			}
		}
	}

	unlocksigqueue ();

	if (i >= MAX_SELECT_THREADS) {
		write_log (_T("BSDSOCK: ERROR - Too many select()s, %d\n"), wscnt);
	} else {
		SOCKET newsock = INVALID_SOCKET;

		taw.sb = sb;
		taw.nfds = nfds;
		taw.readfds = readfds;
		taw.writefds = writefds;
		taw.exceptfds = exceptfds;
		taw.timeout = timeout;
		taw.wscnt = wscnt;

		bsd->threadargsw[i] = &taw;

		SetEvent(bsd->hEvents[i]);

		trap_call_add_dreg(ctx, 0, (((uae_u32)1) << sb->signal) | sb->eintrsigs | wssigs);
		sigs = trap_call_lib(ctx, sb->sysbase, -0x13e);	// Wait()
		/*
		if ((1<<sb->signal) & sigs)
		{ // 2.3.2002/SR Fix for AmiFTP -> Thread is ready, no need to Abort
		sb->needAbort = 0;
		}
		*/
		if (sb->needAbort) {
			if ((newsock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) == INVALID_SOCKET)
				write_log (_T("BSDSOCK: ERROR - Cannot create socket: %d, %d\n"), WSAGetLastError(),wscnt);
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

		if(sigmp)
			trap_put_long(ctx, sigmp, sigs & wssigs);

		if (sigs & wssigs) {
			uae_u32 gotsigs = sigs & wssigs;
			BSDTRACE((_T("[interrupted by signals 0x%08lx]:%d\n"), gotsigs, wscnt));
			if (readfds) fd_zero(ctx, readfds,nfds);
			if (writefds) fd_zero(ctx, writefds,nfds);
			if (exceptfds) fd_zero(ctx, exceptfds,nfds);
			bsdsocklib_seterrno(ctx, sb, 0);
			sb->resultval = 0;
		} else if (sigs & sb->eintrsigs) {
			uae_u32 gotsigs = sigs & sb->eintrsigs;
			BSDTRACE((_T("[interrupted 0x%08x]:%d\n"), gotsigs, wscnt));
			sb->resultval = -1;
			bsdsocklib_seterrno(ctx, sb, 4); // EINTR
			/* EINTR signals are kept active */
			trap_call_add_dreg(ctx, 0, gotsigs);
			trap_call_add_dreg(ctx, 1, gotsigs);
			trap_call_lib(ctx, sb->sysbase, -0x132); // SetSignal
		}

		if (sb->resultval >= 0) {
			BSDTRACE((_T("WaitSelect, %d:%d\n"),sb->resultval,wscnt));
		} else {
			BSDTRACE((_T("WaitSelect error, %d errno %d:%d\n"),sb->resultval,sb->sb_errno,wscnt));
		}
	}
}

uae_u32 host_Inet_NtoA(TrapContext *ctx, SB, uae_u32 in)
{
	uae_char *addr;
	struct in_addr ina;
	uae_u32 scratchbuf;

	*(uae_u32 *)&ina = htonl(in);

	BSDTRACE((_T("Inet_NtoA(%x) -> "),in));

	if ((addr = inet_ntoa(ina)) != NULL) {
		scratchbuf = trap_get_areg(ctx, 6) + offsetof(struct UAEBSDBase,scratchbuf);
		strncpyha(ctx, scratchbuf, addr, SCRATCHBUFSIZE);
		if (ISBSDTRACE) {
			TCHAR *s = au (addr);
			BSDTRACE((_T("%s\n"),s));
			xfree (s);
		}
		return scratchbuf;
	} else
		SETERRNO;

	BSDTRACE((_T("failed (%d)\n"),sb->sb_errno));

	return 0;
}

uae_u32 host_inet_addr(TrapContext *ctx, uae_u32 cp)
{
	uae_u32 addr;
	char *cp_rp;

	if (!trap_valid_address(ctx, cp, 4))
		return 0;
	cp_rp = trap_get_alloc_string(ctx, cp, 256);
	addr = htonl(inet_addr(cp_rp));
	if (ISBSDTRACE) {
		TCHAR *s = au (cp_rp);
		BSDTRACE((_T("inet_addr(%s) -> 0x%08lx\n"), s, addr));
		xfree (s);
	}
	xfree(cp_rp);
	return addr;
}

int isfullscreen (void);
static BOOL CheckOnline(SB)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	DWORD dwFlags;
	BOOL bReturn = TRUE;

	hAmigaSockWnd = mon->hAmigaWnd;
	if (InternetGetConnectedState(&dwFlags,0) == FALSE) { // Internet is offline
		if (InternetAttemptConnect(0) != ERROR_SUCCESS) { // Show Dialer window
			sb->sb_errno = 10001;
			sb->sb_herrno = 1;
			bReturn = FALSE;
			// No success or aborted
		}
		if (isfullscreen() > 0) {
			ShowWindow(mon->hAmigaWnd, SW_RESTORE);
			SetActiveWindow(mon->hAmigaWnd);
		}
	}
	return bReturn;
}

#define GET_STATE_FREE 0
#define GET_STATE_ACTIVE 1
#define GET_STATE_CANCEL 2
#define GET_STATE_FINISHED 3
#define GET_STATE_DONE 4
#define GET_STATE_REALLY_DONE 5

static unsigned int thread_get2 (void *indexp)
{
	int index = *((int*)indexp);
	int wscnt;
	unsigned int result = 0;
	volatile struct threadargs *args;
	uae_u32 name;
	uae_u32 namelen;
	long addrtype;
	char *name_rp;
	SB;
	TrapContext *ctx = NULL;

	while (bsd->hGetEvents[index]) {

		if (WaitForSingleObject(bsd->hGetEvents[index], INFINITE) == WAIT_ABANDONED)
			break;
		if (bsd->hGetEvents[index] == NULL)
			break;

		args = bsd->threadGetargs[index];

		BSDTRACE((_T("tg2 %p,%d,%d:%d -> "), args->sb, index, bsd->threadGetargs_inuse[index], args->wscnt));

		if (bsd->threadGetargs_inuse[index] == GET_STATE_ACTIVE) {
			wscnt = args->wscnt;
			sb = args->sb;

			if (args->args1 == 0) {

				// gethostbyname or gethostbyaddr
				struct hostent *host;
				name = args->args2;
				namelen = args->args3;
				addrtype = args->args4;
				if (addr_valid (_T("thread_get1"), name, 1))
					name_rp = (char*)get_real_address (name);
				else
					name_rp = "";

				if (strchr (name_rp, '.') == 0 || CheckOnline(sb) == TRUE) {
					// Local Address or Internet Online ?
					BSDTRACE((_T("tg2_0a %d:%d -> "),addrtype,wscnt));
					if (addrtype == -1) {
						host = gethostbyname (name_rp);
					} else {
						host = gethostbyaddr (name_rp, namelen, addrtype);
					}
					BSDTRACE((_T("tg2_0b %d -> "), wscnt));
					if (bsd->threadGetargs_inuse[index] != GET_STATE_CANCEL) {
						// No CTRL-C Signal
						if (host == 0) {
							// Error occurred
							SETERRNO;
							BSDTRACE((_T("tg2_0 failed %d:%d -> "), sb->sb_errno,wscnt));
						} else {
							bsdsocklib_seterrno(ctx, sb, 0);
							memcpy((void*)args->buf, host, sizeof(HOSTENT));
						}
					}
				}

			} else if (args->args1 == 1) {

				// getprotobyname
				struct protoent  *proto;

				name = args->args2;
				if (addr_valid (_T("thread_get2"), name, 1))
					name_rp = (char*)get_real_address (name);
				else
					name_rp = "";
				proto = getprotobyname (name_rp);
				if (bsd->threadGetargs_inuse[index] != GET_STATE_CANCEL) { // No CTRL-C Signal
					if (proto == 0) {
						// Error occurred
						SETERRNO;
						BSDTRACE((_T("tg2_1 failed %d:%d -> "), sb->sb_errno, wscnt));
					} else {
						bsdsocklib_seterrno(ctx, sb, 0);
						memcpy((void*)args->buf, proto, sizeof(struct protoent));
					}
				}

			} else if (args->args1 == 2) {

				// getservbyport and getservbyname
				uae_u32 nameport;
				uae_u32 proto;
				uae_u32 type;
				char *proto_rp = 0;
				struct servent *serv = NULL;

				nameport = args->args2;
				proto = args->args3;
				type = args->args4;

				if (proto) {
					if (addr_valid (_T("thread_get3"), proto, 1))
						proto_rp = (char*)get_real_address (proto);
				}

				if (type) {
					serv = getservbyport(nameport, proto_rp);
				} else {
					if (addr_valid(_T("thread_get4"), nameport, 1)) {
						name_rp = (char*)get_real_address(nameport);
						serv = getservbyname(name_rp, proto_rp);
					}
				}
				if (bsd->threadGetargs_inuse[index] != GET_STATE_CANCEL) {
					// No CTRL-C Signal
					if (serv == 0) {
						// Error occurred
						SETERRNO;
						BSDTRACE((_T("tg2_2 failed %d:%d -> "), sb->sb_errno, wscnt));
					} else {
						bsdsocklib_seterrno(ctx, sb, 0);
						memcpy((void*)args->buf, serv, sizeof (struct servent));
					}
				}
			}

			locksigqueue ();
			bsd->threadGetargs_inuse[index] = GET_STATE_FINISHED;
			unlocksigqueue ();

			SETSIGNAL;

			locksigqueue ();
			bsd->threadGetargs_inuse[index] = GET_STATE_DONE;
			unlocksigqueue ();

			SetEvent(bsd->hGetEvents2[index]);

			BSDTRACE((_T("tg2 done %d:%d\n"), index, wscnt));
		}
	}
	write_log (_T("BSDSOCK: thread_get2 terminated\n"));
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

static int run_get_thread(TrapContext *ctx, SB, struct threadargs *args)
{
	int i;

	sb->eintr = 0;

	locksigqueue ();

	for (i = 0; i < MAX_GET_THREADS; i++)  {
		if (bsd->threadGetargs_inuse[i] == GET_STATE_REALLY_DONE) {
			bsd->threadGetargs_inuse[i] = GET_STATE_FREE;
		}
		if (bsd->hGetThreads[i] && bsd->threadGetargs_inuse[i] == GET_STATE_FREE) {
			break;
		}
	}

	if (i >= MAX_GET_THREADS) {
		for (i = 0; i < MAX_GET_THREADS; i++) {
			if (bsd->hGetThreads[i] == NULL) {
				bsd->threadGetargs_inuse[i] = GET_STATE_FREE;
				bsd->hGetEvents[i] = CreateEvent(NULL,FALSE,FALSE,NULL);
				bsd->hGetEvents2[i] = CreateEvent(NULL,FALSE,FALSE,NULL);
				if (bsd->hGetEvents[i] && bsd->hGetEvents2[i])
					bsd->hGetThreads[i] = THREAD(thread_get, &threadindextable[i]);
				if (bsd->hGetEvents[i] == NULL || bsd->hGetEvents2[i] == NULL || bsd->hGetThreads[i] == NULL) {
					if (bsd->hGetEvents[i])
						CloseHandle (bsd->hGetEvents[i]);
					bsd->hGetEvents[i] = NULL;
					if (bsd->hGetEvents2[i])
						CloseHandle (bsd->hGetEvents2[i]);
					bsd->hGetEvents2[i] = NULL;
					write_log (_T("BSDSOCK: ERROR - Thread/Event creation failed - error code: %d:%d\n"),
						GetLastError(), args->wscnt);
					bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
					sb->resultval = -1;
					unlocksigqueue ();
					return -1;
				}
				bsdsetpriority (bsd->hGetThreads[i]);
				break;
			}
		}
	}

	if (i >= MAX_GET_THREADS) {
		write_log (_T("BSDSOCK: ERROR - Too many gethostbyname()s:%d\n"), args->wscnt);
		bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
		sb->resultval = -1;
		unlocksigqueue ();
		return -1;
	} else {
		bsd->threadGetargs[i] = args;
		bsd->threadGetargs_inuse[i] = GET_STATE_ACTIVE;
		ResetEvent(bsd->hGetEvents2[i]);
		SetEvent(bsd->hGetEvents[i]);
	}

	unlocksigqueue ();

	while (bsd->threadGetargs_inuse[i] != GET_STATE_FINISHED && bsd->threadGetargs_inuse[i] != GET_STATE_DONE) {
		WAITSIGNAL;
		locksigqueue ();
		int inuse = bsd->threadGetargs_inuse[i];
		if (sb->eintr == 1 && inuse != GET_STATE_FINISHED && inuse != GET_STATE_DONE)
			bsd->threadGetargs_inuse[i] = GET_STATE_CANCEL;
		unlocksigqueue ();
	}

	if (bsd->threadGetargs_inuse[i] >= GET_STATE_FINISHED)
		WaitForSingleObject(bsd->hGetEvents2[i], INFINITE);

	CANCELSIGNAL;

	return i;
}


static void release_get_thread(int index)
{
	if (index < 0)
		return;
	bsd->threadGetargs_inuse[index] = GET_STATE_REALLY_DONE;
}

void host_gethostbynameaddr (TrapContext *ctx, SB, uae_u32 name, uae_u32 namelen, long addrtype)
{
	static int wscounter;
	HOSTENT *h;
	int size, numaliases = 0, numaddr = 0;
	uae_u32 aptr;
	char *name_rp;
	int i, tindex;
	struct threadargs args;
	volatile struct threadargs *argsp;
	uae_u32 addr;
	uae_u32 *addr_list[2];
	volatile uae_char *buf;
	unsigned int wMsg = 0;

	//	TCHAR on	= 1;
	//	InternetSetOption(0,INTERNET_OPTION_SETTINGS_CHANGED,&on,strlen(&on));
	//  Do not use:	Causes locks with some machines

	tindex = -1;
	memset(&args, 0, sizeof (args));
	argsp = &args;
	argsp->wscnt = ++wscounter;
	buf = argsp->buf;

	name_rp = "";

	if (addr_valid (_T("host_gethostbynameaddr"), name, 1))
		name_rp = (char*)get_real_address (name);

	if (addrtype == -1) {
		if (ISBSDTRACE) {
			TCHAR *s = au (name_rp);
			BSDTRACE((_T("gethostbyname(%s) -> "),s));
			xfree (s);
		}
		// workaround for numeric host "names"
		if ((addr = inet_addr(name_rp)) != INADDR_NONE) {
			bsdsocklib_seterrno(ctx, sb,0);
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
		BSDTRACE((_T("gethostbyaddr(0x%x,0x%x,%ld):%d -> "),name,namelen,addrtype,argsp->wscnt));
	}

	argsp->sb = sb;
	argsp->args1 = 0;
	argsp->args2 = name;
	argsp->args3 = namelen;
	argsp->args4 = addrtype;

	tindex = run_get_thread(ctx, sb, &args);
	if (tindex < 0)
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
			uae_FreeMem(ctx, sb->hostent, sb->hostentsize, sb->sysbase);
		}

		sb->hostent = uae_AllocMem(ctx, size, 0, sb->sysbase);

		if (!sb->hostent) {
			write_log (_T("BSDSOCK: WARNING - gethostby%s() ran out of Amiga memory ")
				_T("(couldn't allocate %ld bytes) while returning result of lookup for '%s':%d\n"),
				addrtype == -1 ? _T("name") : _T("addr"), size, name_rp, argsp->wscnt);
			bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
			release_get_thread (tindex);
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
			put_long (sb->hostent + 20 + i * 4, addstr_ansi(ctx, &aptr, h->h_aliases[i]));
		put_long (sb->hostent + 20 + numaliases * 4, 0);
		for (i = 0; i < numaddr; i++)
			put_long (sb->hostent + 24 + (numaliases + i) * 4, addmem(ctx, &aptr, h->h_addr_list[i], h->h_length));
		put_long (sb->hostent + 24 + numaliases * 4 + numaddr * 4, 0);
		put_long (sb->hostent, aptr);
		addstr_ansi(ctx, &aptr, h->h_name);

		if (ISBSDTRACE) {
			TCHAR *s = au (h->h_name);
			BSDTRACE((_T("OK (%s):%d\n"), s, argsp->wscnt));
			xfree (s);
		}

		bsdsocklib_seterrno(ctx, sb, 0);
		bsdsocklib_setherrno(ctx, sb, 0);

	} else {
		BSDTRACE((_T("failed (%d/%d):%d\n"), sb->sb_errno, sb->sb_herrno,argsp->wscnt));
	}

	release_get_thread (tindex);

}

void host_getprotobyname(TrapContext *ctx, SB, uae_u32 name)
{
	static int wscounter;
	PROTOENT *p;
	int size, numaliases = 0;
	uae_u32 aptr;
	char *name_rp;
	int i, tindex;
	struct threadargs args;
	volatile struct threadargs *argsp;

	memset(&args, 0, sizeof (args));
	argsp = &args;
	argsp->sb = sb;
	argsp->wscnt = ++wscounter;

	name_rp = NULL;
	if (ISBSDTRACE) {
		if (trap_valid_address(ctx, name, 1)) {
			name_rp = trap_get_alloc_string(ctx, name, 256);
		}
	}

	if (ISBSDTRACE) {
		TCHAR *s = au (name_rp ? name_rp : "");
		BSDTRACE((_T("getprotobyname(%s):%d -> "),s, argsp->wscnt));
		xfree (s);
	}

	argsp->args1 = 1;
	argsp->args2 = name;

	tindex = run_get_thread(ctx, sb, &args);
	if (tindex < 0)
		return;

	if (!sb->sb_errno) {
		p = (PROTOENT *)argsp->buf;

		// compute total size of protoent
		size = 16;
		if (p->p_name != NULL)
			size += strlen(p->p_name)+1;

		if (p->p_aliases != NULL)
			while (p->p_aliases[numaliases])
				size += strlen(p->p_aliases[numaliases++])+5;

		if (sb->protoent) {
			uae_FreeMem(ctx, sb->protoent, sb->protoentsize, sb->sysbase);
		}

		sb->protoent = uae_AllocMem(ctx, size, 0, sb->sysbase);

		if (!sb->protoent) {
			if (ISBSDTRACE) {
				TCHAR *s = au(name_rp ? name_rp : "");
				write_log (_T("BSDSOCK: WARNING - getprotobyname() ran out of Amiga memory ")
					_T("(couldn't allocate %ld bytes) while returning result of lookup for '%s':%d\n"),
					size, s, argsp->wscnt);
				xfree (s);
			}
			bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
			release_get_thread (tindex);
			return;
		}

		sb->protoentsize = size;

		aptr = sb->protoent + 16 + numaliases * 4;

		// transfer protoent to Amiga memory
		trap_put_long(ctx, sb->protoent + 4, sb->protoent + 12);
		trap_put_long(ctx, sb->protoent + 8, p->p_proto);

		for (i = 0; i < numaliases; i++)
			trap_put_long(ctx, sb->protoent + 12 + i * 4, addstr_ansi(ctx, &aptr, p->p_aliases[i]));
		trap_put_long(ctx, sb->protoent + 12 + numaliases * 4,0);
		trap_put_long(ctx, sb->protoent, aptr);
		addstr_ansi(ctx, &aptr, p->p_name);
		if (ISBSDTRACE) {
			TCHAR *s = au (p->p_name);
			BSDTRACE((_T("OK (%s, %d):%d\n"), s, p->p_proto, argsp->wscnt));
			xfree (s);
		}
		bsdsocklib_seterrno(ctx, sb,0);

	} else {
		BSDTRACE((_T("failed (%d):%d\n"), sb->sb_errno, argsp->wscnt));
	}

	xfree(name_rp);
	release_get_thread (tindex);
}

void host_getprotobynumber(TrapContext *ctx, SB, uae_u32 name)
{
	bsdsocklib_seterrno(ctx, sb, 1);
}

void host_getservbynameport(TrapContext *ctx, SB, uae_u32 nameport, uae_u32 proto, uae_u32 type)
{
	static int wscounter;
	SERVENT *s;
	int size, numaliases = 0;
	uae_u32 aptr;
	TCHAR *name_rp = NULL, *proto_rp = NULL;
	int i, tindex;
	struct threadargs args;
	volatile struct threadargs *argsp;

	memset(&args, 0, sizeof (args));
	argsp = &args;
	argsp->sb = sb;
	argsp->wscnt = ++wscounter;

	if (proto) {
		if (trap_valid_address(ctx, proto, 1)) {
			uae_char buf[256];
			trap_get_string(ctx, buf, proto, sizeof buf);
			proto_rp = au(buf);
		}
	}

	if (type) {
		BSDTRACE((_T("getservbyport(%d,%s);%d -> "), nameport, proto_rp ? proto_rp : _T("NULL"), argsp->wscnt));
	} else {
		if (trap_valid_address(ctx, nameport, 1)) {
			uae_char buf[256];
			trap_get_string(ctx, buf, nameport, sizeof buf);
			name_rp = au(buf);
		}
		BSDTRACE((_T("getservbyname(%s,%s):%d -> "), name_rp, proto_rp ? proto_rp : _T("NULL"), argsp->wscnt));
	}

	argsp->args1 = 2;
	argsp->args2 = nameport;
	argsp->args3 = proto;
	argsp->args4 = type;

	tindex = run_get_thread(ctx, sb, &args);
	if (tindex < 0)
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
			uae_FreeMem(ctx, sb->servent, sb->serventsize, sb->sysbase);
		}

		sb->servent = uae_AllocMem(ctx, size, 0, sb->sysbase);

		if (!sb->servent) {
			write_log (_T("BSDSOCK: WARNING - getservby%s() ran out of Amiga memory (couldn't allocate %ld bytes):%d\n"), type ? _T("port") : _T("name"), size, argsp->wscnt);
			bsdsocklib_seterrno(ctx, sb, 12); // ENOMEM
			release_get_thread (tindex);
			return;
		}

		sb->serventsize = size;

		aptr = sb->servent + 20 + numaliases * 4;

		// transfer servent to Amiga memory
		trap_put_long(ctx, sb->servent + 4, sb->servent + 16);
		trap_put_long(ctx, sb->servent + 8, (unsigned short)htons(s->s_port));

		for (i = 0; i < numaliases; i++)
			trap_put_long(ctx, sb->servent + 16 + i * 4, addstr_ansi(ctx, &aptr,s->s_aliases[i]));
		trap_put_long(ctx, sb->servent + 16 + numaliases * 4,0);
		trap_put_long(ctx, sb->servent, aptr);
		addstr_ansi(ctx, &aptr, s->s_name);
		trap_put_long(ctx, sb->servent + 12, aptr);
		addstr_ansi(ctx, &aptr, s->s_proto);

		if (ISBSDTRACE) {
			TCHAR *ss = au(s->s_name);
			BSDTRACE((_T("OK (%s, %d):%d\n"), ss, (unsigned short)htons(s->s_port), argsp->wscnt));
			xfree (ss);
		}

		bsdsocklib_seterrno(ctx, sb, 0);

	} else {
		BSDTRACE((_T("failed (%d):%d\n"),sb->sb_errno, argsp->wscnt));
	}

	release_get_thread (tindex);
}

uae_u32 host_gethostname(TrapContext *ctx, uae_u32 name, uae_u32 namelen)
{
	if (!trap_valid_address(ctx, name, namelen))
		return -1;
	uae_char buf[256];
	trap_get_string(ctx, buf, name, sizeof buf);
	return gethostname(buf, namelen);
}

#endif

#endif
