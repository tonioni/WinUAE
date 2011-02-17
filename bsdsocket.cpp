/*
* UAE - The Un*x Amiga Emulator
*
* bsdsocket.library emulation machine-independent part
*
* Copyright 1997, 1998 Mathias Ortmann
*
* Library initialization code (c) Tauno Taipaleenmaki
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include <assert.h>
#include <stddef.h>

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "autoconf.h"
#include "traps.h"
#include "bsdsocket.h"
#include "threaddep/thread.h"
#include "native2amiga.h"

#ifdef BSDSOCKET

struct socketbase *socketbases;
static uae_u32 SockLibBase;

#define SOCKPOOLSIZE 128
#define UNIQUE_ID (-1)

/* ObtainSocket()/ReleaseSocket() public socket pool */
struct sockd {
	long sockpoolids[SOCKPOOLSIZE];
	SOCKET_TYPE sockpoolsocks[SOCKPOOLSIZE];
	uae_u32 sockpoolflags[SOCKPOOLSIZE];
};

static long curruniqid = 65536;
static struct sockd *sockdata;

uae_u32 strncpyha (uae_u32 dst, const uae_char *src, int size)
{
	uae_u32 res = dst;
	if (!addr_valid (L"strncpyha", dst, size))
		return res;
	while (size--) {
		put_byte (dst++, *src);
		if (!*src++)
			return res;
	}
	return res;
}

uae_u32 addstr (uae_u32 * dst, const TCHAR *src)
{
	uae_u32 res = *dst;
	int len;
	char *s = ua (src);
	len = strlen (s) + 1;
	strcpyha_safe (*dst, s);
	(*dst) += len;
	xfree (s);
	return res;
}
uae_u32 addstr_ansi (uae_u32 * dst, const uae_char *src)
{
	uae_u32 res = *dst;
	int len;
	len = strlen (src) + 1;
	strcpyha_safe (*dst, src);
	(*dst) += len;
	return res;
}

uae_u32 addmem (uae_u32 * dst, const uae_char *src, int len)
{
	uae_u32 res = *dst;

	if (!src)
		return 0;

	memcpyha_safe (*dst, (uae_u8*)src, len);
	(*dst) += len;

	return res;
}

/* Get current task */
static uae_u32 gettask (TrapContext *context)
{
	uae_u32 currtask, a1 = m68k_areg (regs, 1);
	TCHAR *tskname;

	m68k_areg (regs, 1) = 0;
	currtask = CallLib (context, get_long (4), -0x126); /* FindTask */

	m68k_areg (regs, 1) = a1;

	tskname = au((char*)get_real_address (get_long (currtask + 10)));
	BSDTRACE ((L"[%s] ", au));
	xfree (tskname);
	return currtask;
}

/* errno/herrno setting */
void bsdsocklib_seterrno (SB, int sb_errno)
{
	sb->sb_errno = sb_errno;
	if (sb->sb_errno >= 1001 && sb->sb_errno <= 1005)
		bsdsocklib_setherrno(sb,sb->sb_errno-1000);
	if (sb->errnoptr) {
		switch (sb->errnosize) {
		case 1:
			put_byte (sb->errnoptr, sb_errno);
			break;
		case 2:
			put_word (sb->errnoptr, sb_errno);
			break;
		case 4:
			put_long (sb->errnoptr, sb_errno);
		}
	}
}

void bsdsocklib_setherrno (SB, int sb_herrno)
{
	sb->sb_herrno = sb_herrno;

	if (sb->herrnoptr) {
		switch (sb->herrnosize) {
		case 1:
			put_byte (sb->herrnoptr, sb_herrno);
			break;
		case 2:
			put_word (sb->herrnoptr, sb_herrno);
			break;
		case 4:
			put_long (sb->herrnoptr, sb_herrno);
		}
	}
}

BOOL checksd(SB, int sd)
{
	int iCounter;
	SOCKET s;

	s = getsock(sb,sd);
	if (s != INVALID_SOCKET) {
		for (iCounter  = 1; iCounter <= sb->dtablesize; iCounter++) {
			if (iCounter != sd) {
				if (getsock(sb,iCounter) == s) {
					releasesock(sb,sd);
					return TRUE;
				}
			}
		}
		for (iCounter  = 0; iCounter < SOCKPOOLSIZE; iCounter++) {
			if (s == sockdata->sockpoolsocks[iCounter])
				return TRUE;
		}
	}
	BSDTRACE((L"checksd FALSE s 0x%x sd %d\n",s,sd));
	return FALSE;
}

void setsd(SB, int sd, SOCKET_TYPE s)
{
	sb->dtable[sd - 1] = s;
}

/* Socket descriptor/opaque socket handle management */
int getsd (SB, SOCKET_TYPE s)
{
	int i;
	SOCKET_TYPE *dt = sb->dtable;

	/* return socket descriptor if already exists */
	for (i = sb->dtablesize; i--;)
		if (dt[i] == s)
			return i + 1;

	/* create new table entry */
	for (i = 0; i < sb->dtablesize; i++)
		if (dt[i] == -1) {
			dt[i] = s;
			sb->ftable[i] = SF_BLOCKING;
			return i + 1;
		}
		/* descriptor table full. */
		bsdsocklib_seterrno (sb, 24); /* EMFILE */

		return -1;
}

SOCKET_TYPE getsock (SB, int sd)
{
	if ((unsigned int) (sd - 1) >= (unsigned int) sb->dtablesize) {
		BSDTRACE ((L"Invalid Socket Descriptor (%d)\n", sd));
		bsdsocklib_seterrno (sb, 38); /* ENOTSOCK */
		return -1;
	}
	if (sb->dtable[sd - 1] == INVALID_SOCKET) {
		struct socketbase *sb1, *nsb;
		uaecptr ot;
		if (!addr_valid (L"getsock1", sb->ownertask + 10, 4))
			return -1;
		ot = get_long (sb->ownertask + 10);
		if (!addr_valid (L"getsock2", ot, 1))
			return -1;
		// Fix for Newsrog (All Tasks of Newsrog using the same dtable)
		for (sb1 = socketbases; sb1; sb1 = nsb) {
			uaecptr ot1;
			if (!addr_valid (L"getsock3", sb1->ownertask + 10, 4))
				break;
			ot1 = get_long (sb1->ownertask + 10);
			if (!addr_valid (L"getsock4", ot1, 1))
				break;
			if (strcmp((char*)get_real_address (ot1), (char*)get_real_address (ot)) == 0) {
				// Task with same name already exists -> use same dtable
				if (sb1->dtable[sd - 1] != INVALID_SOCKET)
					return sb1->dtable[sd - 1];
			}
			nsb = sb1->next;
		}
	}
	return sb->dtable[sd - 1];
}

void releasesock (SB, int sd)
{
	if ((unsigned int) (sd - 1) < (unsigned int) sb->dtablesize)
		sb->dtable[sd - 1] = -1;
}

/* Signal queue */
/* @@@ TODO: ensure proper interlocking */
#if 1
struct socketbase *sbsigqueue;
volatile int bsd_int_requested;
#endif

void addtosigqueue (SB, int events)
{
#if 0
	uae_u32 ot, sts;
#endif

	locksigqueue ();

	if (events)
		sb->sigstosend |= sb->eventsigs;
	else
		sb->sigstosend |= ((uae_u32) 1) << sb->signal;
#if 1
	if (!sb->dosignal) {
		sb->nextsig = sbsigqueue;
		sbsigqueue = sb;
	}
	sb->dosignal = 1;

	bsd_int_requested = 1;

	unlocksigqueue ();

#else

	ot = sb->ownertask;
	sts = sb->sigstosend;

	sb->sigstosend = 0;

	unlocksigqueue ();

	if (sts)
		uae_Signal (ot, sts);
#endif

}

#if 1
void bsdsock_fake_int_handler(void)
{
	locksigqueue ();

	bsd_int_requested = 0;

	if (sbsigqueue != NULL) {
		SB;

		for (sb = sbsigqueue; sb; sb = sb->nextsig) {
			if (sb->dosignal == 1) {
				uae_Signal (sb->ownertask, sb->sigstosend);
				sb->sigstosend = 0;
			}
			sb->dosignal = 0;
		}

		sbsigqueue = NULL;
	}

	unlocksigqueue ();
}

#else

static uae_u32 REGPARAM2 bsdsock_int_handler (TrapContext *context)
{
	SB;

	locksigqueue ();
	bsd_int_requested = 0;

	if (sbsigqueue != NULL) {

		for (sb = sbsigqueue; sb; sb = sb->nextsig) {
			if (sb->dosignal == 1) {
				struct regstruct sbved_regs = context->regs;
				m68k_areg (regs, 1) = sb->ownertask;
				m68k_dreg (regs, 0) = sb->sigstosend;
				CallLib (context, get_long (4), -0x144); /* Signal() */
				context->regs = sbved_regs;
				sb->sigstosend = 0;
			}
			sb->dosignal = 0;
		}

		sbsigqueue = NULL;
	}

	unlocksigqueue ();

	return 0;
}
#endif

void waitsig (TrapContext *context, SB)
{
	long sigs;
	m68k_dreg (regs, 0) = (((uae_u32) 1) << sb->signal) | sb->eintrsigs;
	if ((sigs = CallLib (context, get_long (4), -0x13e)) & sb->eintrsigs) { /* Wait */
		sockabort (sb);
		bsdsocklib_seterrno (sb, 4); /* EINTR */

		// Set signal
		m68k_dreg (regs, 0) = sigs;
		m68k_dreg (regs, 1) = sb->eintrsigs;
		sigs = CallLib (context, get_long (4), -0x132); /* SetSignal() */

		sb->eintr = 1;
	} else
		sb->eintr = 0;
}

void cancelsig (TrapContext *context, SB)
{
#if 1
	locksigqueue ();
	if (sb->dosignal)
		sb->dosignal = 2;
	unlocksigqueue ();
#endif
	m68k_dreg (regs, 0) = 0;
	m68k_dreg (regs, 1) = ((uae_u32) 1) << sb->signal;
	CallLib (context, get_long (4), -0x132); /* SetSignal() */

}

/* Allocate and initialize per-task state structure */
static struct socketbase *alloc_socketbase (TrapContext *context)
{
	SB;
	int i;

	if ((sb = xcalloc (struct socketbase, 1)) != NULL) {
		sb->ownertask = gettask (context);

		m68k_dreg (regs, 0) = -1;
		sb->signal = CallLib (context, get_long (4), -0x14A); /* AllocSignal */

		if (sb->signal == -1) {
			write_log (L"bsdsocket: ERROR: Couldn't allocate signal for task 0x%lx.\n", sb->ownertask);
			free (sb);
			return NULL;
		}
		m68k_dreg (regs, 0) = SCRATCHBUFSIZE;
		m68k_dreg (regs, 1) = 0;

		sb->dtablesize = DEFAULT_DTABLE_SIZE;
		/* @@@ check malloc() result */
		sb->dtable = (SOCKET*)malloc (sb->dtablesize * sizeof (*sb->dtable));
		sb->ftable = (int*)malloc (sb->dtablesize * sizeof (*sb->ftable));

		for (i = sb->dtablesize; i--;)
			sb->dtable[i] = -1;

		sb->eintrsigs = 0x1000; /* SIGBREAKF_CTRL_C */

		if (!host_sbinit (context, sb)) {
			/* @@@ free everything   */
		}

		locksigqueue();

		if (socketbases)
			sb->next = socketbases;
		socketbases = sb;

		unlocksigqueue();

		return sb;
	}
	return NULL;
}

STATIC_INLINE struct socketbase *get_socketbase (TrapContext *context)
{
	return (struct socketbase*)get_pointer (m68k_areg (regs, 6) + offsetof (struct UAEBSDBase, sb));
}

static void free_socketbase (TrapContext *context)
{
	struct socketbase *sb, *nsb;

	if ((sb = get_socketbase (context)) != NULL) {
		m68k_dreg (regs, 0) = sb->signal;
		CallLib (context, get_long (4), -0x150); /* FreeSignal */

		if (sb->hostent) {
			m68k_areg (regs, 1) = sb->hostent;
			m68k_dreg (regs, 0) = sb->hostentsize;
			CallLib (context, get_long (4), -0xD2); /* FreeMem */

		}
		if (sb->protoent) {
			m68k_areg (regs, 1) = sb->protoent;
			m68k_dreg (regs, 0) = sb->protoentsize;
			CallLib (context, get_long (4), -0xD2); /* FreeMem */

		}
		if (sb->servent) {
			m68k_areg (regs, 1) = sb->servent;
			m68k_dreg (regs, 0) = sb->serventsize;
			CallLib (context, get_long (4), -0xD2); /* FreeMem */

		}
		host_sbcleanup (sb);

		free (sb->dtable);
		free (sb->ftable);

		locksigqueue ();

		if (sb == socketbases)
			socketbases = sb->next;
		else {
			for (nsb = socketbases; nsb; nsb = nsb->next) {
				if (sb == nsb->next) {
					nsb->next = sb->next;
					break;
				}
			}
		}

#if 1
		if (sb == sbsigqueue)
			sbsigqueue = sb->next;
		else {
			for (nsb = sbsigqueue; nsb; nsb = nsb->next) {
				if (sb == nsb->next) {
					nsb->next = sb->next;
					break;
				}
			}
		}
#endif

		unlocksigqueue ();

		free (sb);
	}
}

static uae_u32 REGPARAM2 bsdsocklib_Expunge (TrapContext *context)
{
	BSDTRACE ((L"Expunge() -> [ignored]\n"));
	return 0;
}

static uae_u32 functable, datatable, inittable;

static uae_u32 REGPARAM2 bsdsocklib_Open (TrapContext *context)
{
	uae_u32 result = 0;
	int opencount;
	SB;

	BSDTRACE ((L"OpenLibrary() -> "));

	if ((sb = alloc_socketbase (context)) != NULL) {
		put_word (SockLibBase + 32, opencount = get_word (SockLibBase + 32) + 1);

		m68k_areg (regs, 0) = functable;
		m68k_areg (regs, 1) = datatable;
		m68k_areg (regs, 2) = 0;
		m68k_dreg (regs, 0) = sizeof (struct UAEBSDBase);
		m68k_dreg (regs, 1) = 0;
		result = CallLib (context, get_long (4), -0x54); /* MakeLibrary */

		put_pointer (result + offsetof (struct UAEBSDBase, sb), sb);

		BSDTRACE ((L"%0lx [%d]\n", result, opencount));
	} else
		BSDTRACE ((L"failed (out of memory)\n"));

	return result;
}

static uae_u32 REGPARAM2 bsdsocklib_Close (TrapContext *context)
{
	int opencount;

	uae_u32 base = m68k_areg (regs, 6);
	uae_u32 negsize = get_word (base + 16);

	free_socketbase (context);

	put_word (SockLibBase + 32, opencount = get_word (SockLibBase + 32) - 1);

	m68k_areg (regs, 1) = base - negsize;
	m68k_dreg (regs, 0) = negsize + get_word (base + 18);
	CallLib (context, get_long (4), -0xD2); /* FreeMem */

	BSDTRACE ((L"CloseLibrary() -> [%d]\n", opencount));

	return 0;
}

/* socket(domain, type, protocol)(d0/d1/d2) */
static uae_u32 REGPARAM2 bsdsocklib_socket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_socket (sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1),
		m68k_dreg (regs, 2));
}

/* bind(s, name, namelen)(d0/a0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_bind (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_bind (sb, m68k_dreg (regs, 0), m68k_areg (regs, 0),
		m68k_dreg (regs, 1));
}

/* listen(s, backlog)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_listen (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_listen (sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1));
}

/* accept(s, addr, addrlen)(d0/a0/a1) */
static uae_u32 REGPARAM2 bsdsocklib_accept (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_accept (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_areg (regs, 1));
	return sb->resultval;
}

/* connect(s, name, namelen)(d0/a0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_connect (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_connect (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_dreg (regs, 1));
	return sb->sb_errno ? -1 : 0;
}

/* sendto(s, msg, len, flags, to, tolen)(d0/a0/d1/d2/a1/d3) */
static uae_u32 REGPARAM2 bsdsocklib_sendto (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_sendto (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_dreg (regs, 1),
		m68k_dreg (regs, 2), m68k_areg (regs, 1), m68k_dreg (regs, 3));
	return sb->resultval;
}

/* send(s, msg, len, flags)(d0/a0/d1/d2) */
static uae_u32 REGPARAM2 bsdsocklib_send (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_sendto (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_dreg (regs, 1),
		m68k_dreg (regs, 2), 0, 0);
	return sb->resultval;
}

/* recvfrom(s, buf, len, flags, from, fromlen)(d0/a0/d1/d2/a1/a2) */
static uae_u32 REGPARAM2 bsdsocklib_recvfrom (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_recvfrom (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_dreg (regs, 1),
		m68k_dreg (regs, 2), m68k_areg (regs, 1), m68k_areg (regs, 2));
	return sb->resultval;
}

/* recv(s, buf, len, flags)(d0/a0/d1/d2) */
static uae_u32 REGPARAM2 bsdsocklib_recv (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_recvfrom (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_dreg (regs, 1),
		m68k_dreg (regs, 2), 0, 0);
	return sb->resultval;
}

/* shutdown(s, how)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_shutdown (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_shutdown (sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1));
}

/* setsockopt(s, level, optname, optval, optlen)(d0/d1/d2/a0/d3) */
static uae_u32 REGPARAM2 bsdsocklib_setsockopt (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_setsockopt (sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1), m68k_dreg (regs, 2),
		m68k_areg (regs, 0), m68k_dreg (regs, 3));
	return sb->resultval;
}

/* getsockopt(s, level, optname, optval, optlen)(d0/d1/d2/a0/a1) */
static uae_u32 REGPARAM2 bsdsocklib_getsockopt (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_getsockopt (sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1), m68k_dreg (regs, 2),
		m68k_areg (regs, 0), m68k_areg (regs, 1));
}

/* getsockname(s, hostname, namelen)(d0/a0/a1) */
static uae_u32 REGPARAM2 bsdsocklib_getsockname (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_getsockname (sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_areg (regs, 1));
}

/* getpeername(s, hostname, namelen)(d0/a0/a1) */
static uae_u32 REGPARAM2 bsdsocklib_getpeername (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_getpeername (sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_areg (regs, 1));
}

/* *------ generic system calls related to sockets */
/* IoctlSocket(d, request, argp)(d0/d1/a0) */
static uae_u32 REGPARAM2 bsdsocklib_IoctlSocket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_IoctlSocket (context, sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1), m68k_areg (regs, 0));
}

/* *------ AmiTCP/IP specific stuff */
/* CloseSocket(d)(d0) */
static uae_u32 REGPARAM2 bsdsocklib_CloseSocket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_CloseSocket (context, sb, m68k_dreg (regs, 0));
}

/* WaitSelect(nfds, readfds, writefds, execptfds, timeout, maskp)(d0/a0/a1/a2/a3/d1) */
static uae_u32 REGPARAM2 bsdsocklib_WaitSelect (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_WaitSelect (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), m68k_areg (regs, 1),
		m68k_areg (regs, 2), m68k_areg (regs, 3), m68k_dreg (regs, 1));
	return sb->resultval;
}

/* SetSocketSignals(SIGINTR, SIGIO, SIGURG)(d0/d1/d2) */
static uae_u32 REGPARAM2 bsdsocklib_SetSocketSignals (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);

	BSDTRACE ((L"SetSocketSignals(0x%08lx,0x%08lx,0x%08lx) -> ", m68k_dreg (regs, 0), m68k_dreg (regs, 1), m68k_dreg (regs, 2)));
	sb->eintrsigs = m68k_dreg (regs, 0);
	sb->eventsigs = m68k_dreg (regs, 1);

	return 0;
}

/* SetDTableSize(size)(d0) */
static uae_u32 bsdsocklib_SetDTableSize (SB, int newSize)
{
	int *newdtable;
	int *newftable;
	unsigned int *newmtable;
	int i;

	if (newSize < sb->dtablesize) {
		/* I don't support lowering the size */
		return 0;
	}

	newdtable = xcalloc (int, newSize);
	newftable = xcalloc (int, newSize);
	newmtable = xcalloc (unsigned int, newSize);

	if (newdtable == NULL || newftable == NULL || newmtable == NULL) {
		sb->resultval = -1;
		bsdsocklib_seterrno(sb, ENOMEM);
		free (newdtable);
		free (newftable);
		free (newmtable);
		return -1;
	}

	memcpy (newdtable, sb->dtable, sb->dtablesize * sizeof(*sb->dtable));
	memcpy (newftable, sb->ftable, sb->dtablesize * sizeof(*sb->ftable));
	memcpy (newmtable, sb->mtable, sb->dtablesize * sizeof(*sb->mtable));
	for (i = sb->dtablesize + 1; i < newSize; i++)
		newdtable[i] = -1;

	sb->dtablesize = newSize;
	free(sb->dtable);
	free(sb->ftable);
	free(sb->mtable);
	sb->dtable = (SOCKET*)newdtable;
	sb->ftable = newftable;
	sb->mtable = newmtable;
	sb->resultval = 0;
	return 0;
}

static int sockpoolindex (long id)
{
	int i;

	for (i = 0; i < SOCKPOOLSIZE; i++)
		if (sockdata->sockpoolids[i] == id)
			return i;

	return -1;
}

/* ObtainSocket(id, domain, type, protocol)(d0/d1/d2/d3) */
static uae_u32 REGPARAM2 bsdsocklib_ObtainSocket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	int sd;
	long id;
	SOCKET_TYPE s;
	int i;

	id = m68k_dreg (regs, 0);

	BSDTRACE ((L"ObtainSocket(%d,%d,%d,%d) -> ", id, m68k_dreg (regs, 1), m68k_dreg (regs, 2), m68k_dreg (regs, 3)));

	i = sockpoolindex (id);

	if (i == -1) {
		BSDTRACE ((L"[invalid key]\n"));
		return -1;
	}
	s = sockdata->sockpoolsocks[i];

	sd = getsd (sb, s);

	BSDTRACE ((L"%d\n", sd));

	if (sd != -1) {
		sb->ftable[sd - 1] = sockdata->sockpoolflags[i];
		sockdata->sockpoolids[i] = UNIQUE_ID;
		return sd - 1;
	}

	return -1;
}

/* ReleaseSocket(fd, id)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_ReleaseSocket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	int sd;
	long id;
	SOCKET_TYPE s;
	int i;
	uae_u32 flags;

	sd = m68k_dreg (regs, 0);
	id = m68k_dreg (regs, 1);

	sd++;
	BSDTRACE ((L"ReleaseSocket(%d,%d) -> ", sd, id));

	s = getsock (sb, sd);

	if (s != -1) {
		flags = sb->ftable[sd - 1];

		if (flags & REP_ALL) {
			write_log (L"bsdsocket: ERROR: ReleaseSocket() is not supported for sockets with async event notification enabled!\n");
			return -1;
		}
		releasesock (sb, sd);

		if (id == UNIQUE_ID) {
			for (;;) {
				if (sockpoolindex (curruniqid) == -1)
					break;
				curruniqid += 129;
				if ((unsigned long) (curruniqid + 1) < 65536)
					curruniqid += 65537;
			}

			id = curruniqid;
		} else if (id < 0 && id > 65535) {
			if (sockpoolindex (id) != -1) {
				BSDTRACE ((L"[unique ID already exists]\n"));
				return -1;
			}
		}
		i = sockpoolindex (-1);

		if (i == -1) {
			BSDTRACE ((L"-1\n"));
			write_log (L"bsdsocket: ERROR: Global socket pool overflow\n");
			return -1;
		}
		sockdata->sockpoolids[i] = id;
		sockdata->sockpoolsocks[i] = s;
		sockdata->sockpoolflags[i] = flags;

		BSDTRACE ((L"id %d s 0x%x\n", id,s));
	} else {
		BSDTRACE ((L"[invalid socket descriptor]\n"));
		return -1;
	}

	return id;
}

/* ReleaseCopyOfSocket(fd, id)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_ReleaseCopyOfSocket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	int sd;
	long id;
	SOCKET_TYPE s;
	int i;
	uae_u32 flags;

	sd = m68k_dreg (regs, 0);
	id = m68k_dreg (regs, 1);

	sd++;
	BSDTRACE ((L"ReleaseSocket(%d,%d) -> ", sd, id));

	s = getsock (sb, sd);

	if (s != -1) {
		flags = sb->ftable[sd - 1];

		if (flags & REP_ALL) {
			write_log (L"bsdsocket: ERROR: ReleaseCopyOfSocket() is not supported for sockets with async event notification enabled!\n");
			return -1;
		}
		if (id == UNIQUE_ID) {
			for (;;) {
				if (sockpoolindex (curruniqid) == -1)
					break;
				curruniqid += 129;
				if ((unsigned long) (curruniqid + 1) < 65536)
					curruniqid += 65537;
			}
			id = curruniqid;
		} else if (id < 0 && id > 65535) {
			if (sockpoolindex (id) != -1) {
				BSDTRACE ((L"[unique ID already exists]\n"));
				return -1;
			}
		}
		i = sockpoolindex (-1);

		if (i == -1) {
			BSDTRACE ((L"-1\n"));
			write_log (L"bsdsocket: ERROR: Global socket pool overflow\n");
			return -1;
		}
		sockdata->sockpoolids[i] = id;
		sockdata->sockpoolsocks[i] = s;
		sockdata->sockpoolflags[i] = flags;

		BSDTRACE ((L"id %d s 0x%x\n", id,s));

	} else {

		BSDTRACE ((L"[invalid socket descriptor]\n"));
		return -1;
	}

	return id;
}

/* Errno()() */
static uae_u32 REGPARAM2 bsdsocklib_Errno (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	BSDTRACE ((L"Errno() -> %d\n", sb->sb_errno));
	return sb->sb_errno;
}

/* SetErrnoPtr(errno_p, size)(a0/d0) */
static uae_u32 REGPARAM2 bsdsocklib_SetErrnoPtr (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	uae_u32 errnoptr = m68k_areg (regs, 0), size = m68k_dreg (regs, 0);

	BSDTRACE ((L"SetErrnoPtr(0x%lx,%d) -> ", errnoptr, size));

	if (size == 1 || size == 2 || size == 4) {
		sb->errnoptr = errnoptr;
		sb->errnosize = size;
		BSDTRACE ((L"OK\n"));
		return 0;
	}
	bsdsocklib_seterrno (sb, 22); /* EINVAL */

	return -1;
}

/* *------ inet library calls related to inet address manipulation */
/* Inet_NtoA(in)(d0) */
static uae_u32 REGPARAM2 bsdsocklib_Inet_NtoA (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_Inet_NtoA (context, sb, m68k_dreg (regs, 0));
}

/* inet_addr(cp)(a0) */
static uae_u32 REGPARAM2 bsdsocklib_inet_addr (TrapContext *context)
{
	return host_inet_addr (m68k_areg (regs, 0));
}

/* Inet_LnaOf(in)(d0) */
static uae_u32 REGPARAM2 bsdsocklib_Inet_LnaOf (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: Inet_LnaOf()\n");
	return 0;
}

/* Inet_NetOf(in)(d0) */
static uae_u32 REGPARAM2 bsdsocklib_Inet_NetOf (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: Inet_NetOf()\n");
	return 0;
}

/* Inet_MakeAddr(net, host)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_Inet_MakeAddr (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: Inet_MakeAddr()\n");
	return 0;
}

/* inet_network(cp)(a0) */
static uae_u32 REGPARAM2 bsdsocklib_inet_network (TrapContext *context)
{
	return host_inet_addr (m68k_areg (regs, 0));
}

/* *------ gethostbyname etc */
/* gethostbyname(name)(a0) */
static uae_u32 REGPARAM2 bsdsocklib_gethostbyname (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_gethostbynameaddr (context, sb, m68k_areg (regs, 0), 0, -1);
	return sb->sb_herrno ? 0 : sb->hostent;
}

/* gethostbyaddr(addr, len, type)(a0/d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_gethostbyaddr (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_gethostbynameaddr (context, sb, m68k_areg (regs, 0), m68k_dreg (regs, 0), m68k_dreg (regs, 1));
	return sb->sb_herrno ? 0 : sb->hostent;
}

/* getnetbyname(name)(a0) */
static uae_u32 REGPARAM2 bsdsocklib_getnetbyname (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: getnetbyname()\n");
	return 0;
}

/* getnetbyaddr(net, type)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_getnetbyaddr (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: getnetbyaddr()\n");
	return 0;
}

/* getservbyname(name, proto)(a0/a1) */
static uae_u32 REGPARAM2 bsdsocklib_getservbyname (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_getservbynameport (context, sb, m68k_areg (regs, 0), m68k_areg (regs, 1), 0);
	return sb->sb_errno ? 0 : sb->servent;
}

/* getservbyport(port, proto)(d0/a0) */
static uae_u32 REGPARAM2 bsdsocklib_getservbyport (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_getservbynameport (context, sb, m68k_dreg (regs, 0), m68k_areg (regs, 0), 1);
	return sb->sb_errno ? 0 : sb->servent;
}

/* getprotobyname(name)(a0) */
static uae_u32 REGPARAM2 bsdsocklib_getprotobyname (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_getprotobyname (context, sb, m68k_areg (regs, 0));
	return sb->sb_errno ? 0 : sb->protoent;
}

/* getprotobynumber(proto)(d0)  */
static uae_u32 REGPARAM2 bsdsocklib_getprotobynumber (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	host_getprotobynumber (context, sb, m68k_dreg (regs, 0));
	return sb->sb_errno ? 0 : sb->protoent;
}

/* *------ syslog functions */
/* Syslog(level, format, ap)(d0/a0/a1) */
static uae_u32 REGPARAM2 bsdsocklib_vsyslog (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: vsyslog()\n");
	return 0;
}

/* *------ AmiTCP/IP 1.1 extensions */
/* Dup2Socket(fd1, fd2)(d0/d1) */
static uae_u32 REGPARAM2 bsdsocklib_Dup2Socket (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	return host_dup2socket (sb, m68k_dreg (regs, 0), m68k_dreg (regs, 1));
}

static uae_u32 REGPARAM2 bsdsocklib_sendmsg (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: sendmsg()\n");
	return 0;
}

static uae_u32 REGPARAM2 bsdsocklib_recvmsg (TrapContext *context)
{
	write_log (L"bsdsocket: UNSUPPORTED: recvmsg()\n");
	return 0;
}

static uae_u32 REGPARAM2 bsdsocklib_gethostname (TrapContext *context)
{
	return host_gethostname (m68k_areg (regs, 0), m68k_dreg (regs, 0));
}

static uae_u32 REGPARAM2 bsdsocklib_gethostid (TrapContext *context)
{
	write_log (L"bsdsocket: WARNING: Process '%s' calls deprecated function gethostid() - returning 127.0.0.1\n",
		get_real_address (get_long (gettask (context) + 10)));
	return 0x7f000001;
}

static const TCHAR *errortexts[] =
{L"No error", L"Operation not permitted", L"No such file or directory",
L"No such process", L"Interrupted system call", L"Input/output error", L"Device not configured",
L"Argument list too long", L"Exec format error", L"Bad file descriptor", L"No child processes",
L"Resource deadlock avoided", L"Cannot allocate memory", L"Permission denied", L"Bad address",
L"Block device required", L"Device busy", L"Object exists", L"Cross-device link",
L"Operation not supported by device", L"Not a directory", L"Is a directory", L"Invalid argument",
L"Too many open files in system", L"Too many open files", L"Inappropriate ioctl for device",
L"Text file busy", L"File too large", L"No space left on device", L"Illegal seek",
L"Read-only file system", L"Too many links", L"Broken pipe", L"Numerical argument out of domain",
L"Result too large", L"Resource temporarily unavailable", L"Operation now in progress",
L"Operation already in progress", L"Socket operation on non-socket", L"Destination address required",
L"Message too long", L"Protocol wrong type for socket", L"Protocol not available",
L"Protocol not supported", L"Socket type not supported", L"Operation not supported",
L"Protocol family not supported", L"Address family not supported by protocol family",
L"Address already in use", L"Can't assign requested address", L"Network is down",
L"Network is unreachable", L"Network dropped connection on reset", L"Software caused connection abort",
L"Connection reset by peer", L"No buffer space available", L"Socket is already connected",
L"Socket is not connected", L"Can't send after socket shutdown", L"Too many references: can't splice",
L"Connection timed out", L"Connection refused", L"Too many levels of symbolic links",
L"File name too long", L"Host is down", L"No route to host", L"Directory not empty",
L"Too many processes", L"Too many users", L"Disc quota exceeded", L"Stale NFS file handle",
L"Too many levels of remote in path", L"RPC struct is bad", L"RPC version wrong",
L"RPC prog. not avail", L"Program version wrong", L"Bad procedure for program", L"No locks available",
L"Function not implemented", L"Inappropriate file type or format", L"PError 0"};

static uae_u32 errnotextptrs[sizeof (errortexts) / sizeof (*errortexts)];
static const uae_u32 number_sys_error = sizeof (errortexts) / sizeof (*errortexts);


static const TCHAR *herrortexts[] =
{L"No error", L"Unknown host", L"Host name lookup failure", L"Unknown server error",
L"No address associated with name"};

static uae_u32 herrnotextptrs[sizeof (herrortexts) / sizeof (*herrortexts)];
static const uae_u32 number_host_error = sizeof (herrortexts) / sizeof (*herrortexts);

static const TCHAR * const strErr = L"Errlist lookup error";
static uae_u32 strErrptr;


#define TAG_DONE   (0L)		/* terminates array of TagItems. ti_Data unused */
#define TAG_IGNORE (1L)		/* ignore this item, not end of array */
#define TAG_MORE   (2L)		/* ti_Data is pointer to another array of TagItems */
#define TAG_SKIP   (3L)		/* skip this and the next ti_Data items */
#define TAG_USER   ((uae_u32)(1L << 31))

#define SBTF_VAL 0x0000
#define SBTF_REF 0x8000
#define SBTB_CODE 1
#define SBTS_CODE 0x3FFF
#define SBTM_CODE(tag) ((((UWORD)(tag)) >> SBTB_CODE) & SBTS_CODE)
#define SBTF_GET  0x0
#define SBTF_SET  0x1
#define SBTM_GETREF(code) \
	(TAG_USER | SBTF_REF | (((code) & SBTS_CODE) << SBTB_CODE))
#define SBTM_GETVAL(code) (TAG_USER | (((code) & SBTS_CODE) << SBTB_CODE))
#define SBTM_SETREF(code) \
	(TAG_USER | SBTF_REF | (((code) & SBTS_CODE) << SBTB_CODE) | SBTF_SET)
#define SBTM_SETVAL(code) \
	(TAG_USER | (((code) & SBTS_CODE) << SBTB_CODE) | SBTF_SET)
#define SBTC_BREAKMASK	    1
#define SBTC_SIGIOMASK	    2
#define SBTC_SIGURGMASK	    3
#define SBTC_SIGEVENTMASK   4
#define SBTC_ERRNO	    6
#define SBTC_HERRNO	    7
#define SBTC_DTABLESIZE	    8
#define SBTC_FDCALLBACK	    9
#define SBTC_LOGSTAT	    10
#define SBTC_LOGTAGPTR	    11
#define SBTC_LOGFACILITY    12
#define SBTC_LOGMASK	    13
#define SBTC_ERRNOSTRPTR    14 /* <sys/errno.h> */
#define SBTC_HERRNOSTRPTR   15 /* <netdb.h> */
#define SBTC_IOERRNOSTRPTR  16 /* <exec/errors.h> */
#define SBTC_S2ERRNOSTRPTR  17 /* <devices/sana2.h> */
#define SBTC_S2WERRNOSTRPTR 18 /* <devices/sana2.h> */
#define SBTC_ERRNOBYTEPTR   21
#define SBTC_ERRNOWORDPTR   22
#define SBTC_ERRNOLONGPTR   24
#define SBTC_HERRNOLONGPTR  25
#define SBTC_RELEASESTRPTR  29

static void tagcopy (uae_u32 currtag, uae_u32 currval, uae_u32 tagptr, uae_u32 * ptr)
{
	switch (currtag & 0x8001) {
	case 0x0000:	/* SBTM_GETVAL */

		put_long (tagptr + 4, *ptr);
		break;
	case 0x8000:	/* SBTM_GETREF */

		put_long (currval, *ptr);
		break;
	case 0x0001:	/* SBTM_SETVAL */

		*ptr = currval;
		break;
	default:		/* SBTM_SETREF */

		*ptr = get_long (currval);
	}
}

static uae_u32 REGPARAM2 bsdsocklib_SocketBaseTagList (TrapContext *context)
{
	struct socketbase *sb = get_socketbase (context);
	uae_u32 tagptr = m68k_areg (regs, 0);
	uae_u32 tagsprocessed = 1;
	uae_u32 currtag;
	uae_u32 currval;

	BSDTRACE ((L"SocketBaseTagList("));

	for (;;) {
		currtag = get_long (tagptr);
		currval = get_long (tagptr + 4);
		tagsprocessed++;

		switch (currtag) {
		case TAG_DONE:
			BSDTRACE ((L"TAG_DONE"));
			tagsprocessed = 0;
			goto done;
		case TAG_IGNORE:
			BSDTRACE ((L"TAG_IGNORE"));
			break;
		case TAG_MORE:
			BSDTRACE ((L"TAG_MORE(0x%lx)", currval));
			tagptr = currval;
			break;
		case TAG_SKIP:
			BSDTRACE ((L"TAG_SKIP(%d)", currval));
			tagptr += currval * 8;
			break;

		default:
			if (currtag & TAG_USER) {
				BSDTRACE ((L"SBTM_"));
				BSDTRACE ((currtag & 0x0001 ? L"SET" : L"GET"));
				BSDTRACE ((currtag & 0x8000 ? L"REF(" : L"VAL("));

				switch ((currtag >> 1) & SBTS_CODE) {
				case SBTC_BREAKMASK:
					BSDTRACE ((L"SBTC_BREAKMASK),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->eintrsigs);
					break;
				case SBTC_SIGEVENTMASK:
					BSDTRACE ((L"SBTC_SIGEVENTMASK),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->eventsigs);
					break;
				case SBTC_SIGIOMASK:
					BSDTRACE ((L"SBTC_SIGEVENTMASK),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->eventsigs);
					break;
				case SBTC_ERRNO:
					BSDTRACE ((L"SBTC_ERRNO),%d", currval));
					tagcopy (currtag, currval, tagptr, (uae_u32*)&sb->sb_errno);
					break;
				case SBTC_HERRNO:
					BSDTRACE ((L"SBTC_HERRNO),%d", currval));
					tagcopy (currtag, currval, tagptr, (uae_u32*)&sb->sb_herrno);
					break;
				case SBTC_DTABLESIZE:
					BSDTRACE ((L"SBTC_DTABLESIZE),0x%lx", currval));
					if (currtag & 1) {
						bsdsocklib_SetDTableSize(sb, currval);
					} else {
						put_long (tagptr + 4, sb->dtablesize);
					}
					break;
				case SBTC_ERRNOSTRPTR:
					if (currtag & 1) {
						BSDTRACE ((L"ERRNOSTRPTR),invalid"));
					} else {
						unsigned long ulTmp;
						if (currtag & 0x8000) { /* SBTM_GETREF */
							ulTmp = get_long (currval);
						} else { /* SBTM_GETVAL */
							ulTmp = currval;
						}
						BSDTRACE ((L"ERRNOSTRPTR),%d", ulTmp));
						if (ulTmp < number_sys_error) {
							tagcopy (currtag, currval, tagptr, &errnotextptrs[ulTmp]);
						} else {
							tagcopy (currtag, currval, tagptr, &strErrptr);
						}
					}
					break;
				case SBTC_HERRNOSTRPTR:
					if (currtag & 1) {
						BSDTRACE ((L"HERRNOSTRPTR),invalid"));
					} else {
						unsigned long ulTmp;
						if (currtag & 0x8000) { /* SBTM_GETREF */
							ulTmp = get_long (currval);
						} else { /* SBTM_GETVAL */
							ulTmp = currval;
						}
						BSDTRACE ((L"HERRNOSTRPTR),%d", ulTmp));
						if (ulTmp < number_host_error) {
							tagcopy (currtag, currval, tagptr, &herrnotextptrs[ulTmp]);
						} else {
							tagcopy (currtag, currval, tagptr, &strErrptr);
						}
					}
					break;
				case SBTC_ERRNOBYTEPTR:
					BSDTRACE ((L"SBTC_ERRNOBYTEPTR),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->errnoptr);
					sb->errnosize = 1;
					break;
				case SBTC_ERRNOWORDPTR:
					BSDTRACE ((L"SBTC_ERRNOWORDPTR),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->errnoptr);
					sb->errnosize = 2;
					break;
				case SBTC_ERRNOLONGPTR:
					BSDTRACE ((L"SBTC_ERRNOLONGPTR),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->errnoptr);
					sb->errnosize = 4;
					break;
				case SBTC_HERRNOLONGPTR:
					BSDTRACE ((L"SBTC_HERRNOLONGPTR),0x%lx", currval));
					tagcopy (currtag, currval, tagptr, &sb->herrnoptr);
					sb->herrnosize = 4;
					break;
				default:
					write_log (L"bsdsocket: WARNING: Unsupported tag type (%08x) in SocketBaseTagList(%x)\n",
						currtag, m68k_areg (regs, 0));
					break;
				}
			} else {
				BSDTRACE ((L"TAG_UNKNOWN(0x%x)", currtag));
				/* Aminetradio uses 0x00004e55 as an ending tag */
				if ((currtag & 0xffff8000) == 0) {
					write_log (L"bsdsocket: WARNING: Corrupted SocketBaseTagList(%x) tag detected (%08x)\n",
						m68k_areg (regs, 0), currtag);
					goto done;
				}
			}
		}

		BSDTRACE ((L","));
		tagptr += 8;
	}

done:
	BSDTRACE ((L") -> %d\n", tagsprocessed));

	return tagsprocessed;
}

static uae_u32 REGPARAM2 bsdsocklib_GetSocketEvents (TrapContext *context)
{
#ifdef _WIN32
	struct socketbase *sb = get_socketbase (context);
	int i;
	int flags;
	uae_u32 ptr = m68k_areg (regs, 0);

	BSDTRACE ((L"GetSocketEvents(0x%x) -> ", ptr));

	for (i = sb->dtablesize; i--; sb->eventindex++) {
		if (sb->eventindex >= sb->dtablesize)
			sb->eventindex = 0;

		if (sb->mtable[sb->eventindex]) {
			flags = sb->ftable[sb->eventindex] & SET_ALL;
			if (flags) {
				sb->ftable[sb->eventindex] &= ~SET_ALL;
				put_long (m68k_areg (regs, 0), flags >> 8);
				BSDTRACE ((L"%d (0x%x)\n", sb->eventindex + 1, flags >> 8));
				return sb->eventindex; // xxx
			}
		}
	}
#endif
	BSDTRACE ((L"-1\n"));
	return -1;
}

static uae_u32 REGPARAM2 bsdsocklib_getdtablesize (TrapContext *context)
{
	return get_socketbase (context)->dtablesize;
}

static uae_u32 REGPARAM2 bsdsocklib_null (TrapContext *context)
{
	return 0;
}

static uae_u32 REGPARAM2 bsdsocklib_init (TrapContext *context)
{
	uae_u32 tmp1;
	int i;

	write_log (L"Creating UAE bsdsocket.library 4.1\n");
	if (SockLibBase)
		bsdlib_reset ();

	m68k_areg (regs, 0) = functable;
	m68k_areg (regs, 1) = datatable;
	m68k_areg (regs, 2) = 0;
	m68k_dreg (regs, 0) = LIBRARY_SIZEOF;
	m68k_dreg (regs, 1) = 0;
	tmp1 = CallLib (context, m68k_areg (regs, 6), -0x54); /* MakeLibrary */

	if (!tmp1) {
		write_log (L"bsdoscket: FATAL: Cannot create bsdsocket.library!\n");
		return 0;
	}
	m68k_areg (regs, 1) = tmp1;
	CallLib (context, m68k_areg (regs, 6), -0x18c); /* AddLibrary */
	SockLibBase = tmp1;

	/* Install error strings in Amiga memory */
	tmp1 = 0;
	for (i = number_sys_error; i--;)
		tmp1 += _tcslen (errortexts[i]) + 1;

	for (i = number_host_error; i--;)
		tmp1 += _tcslen (herrortexts[i]) + 1;

	tmp1 += _tcslen (strErr) + 1;

	m68k_dreg (regs, 0) = tmp1;
	m68k_dreg (regs, 1) = 0;
	tmp1 = CallLib (context, get_long (4), -0xC6); /* AllocMem */

	if (!tmp1) {
		write_log (L"bsdsocket: FATAL: Ran out of memory while creating bsdsocket.library!\n");
		return 0;
	}
	for (i = 0; i < (int) (number_sys_error); i++)
		errnotextptrs[i] = addstr (&tmp1, errortexts[i]);
	for (i = 0; i < (int) (number_host_error); i++)
		herrnotextptrs[i] = addstr (&tmp1, herrortexts[i]);
	strErrptr = addstr (&tmp1, strErr);

#if 0
	/* @@@ someone please implement a proper interrupt handler setup here :) */
	tmp1 = here ();
	calltrap (deftrap2 (bsdsock_int_handler, TRAPFLAG_EXTRA_STACK | TRAPFLAG_NO_RETVAL, "bsdsock_int_handler"));
	dw (0x4ef9);
	dl (get_long (context->regs.vbr + 0x78));
	put_long (context->regs.vbr + 0x78, tmp1);
#endif

	m68k_dreg (regs, 0) = 1;
	return 0;
}

void bsdlib_reset (void)
{
	SB, *nsb;
	int i;

	if (!SockLibBase)
		return;

	SockLibBase = 0;

	write_log (L"BSDSOCK: cleanup start..\n");
	host_sbcleanup (NULL);
	for (sb = socketbases; sb; sb = nsb) {
		nsb = sb->next;

		write_log (L"BSDSOCK: cleanup start socket %x\n", sb);
		host_sbcleanup (sb);

		free (sb->dtable);
		free (sb->ftable);

		free (sb);
	}
	write_log (L"BSDSOCK: cleanup end\n");

	socketbases = NULL;
#if 1
	sbsigqueue = NULL;
#endif

	for (i = 0; i < SOCKPOOLSIZE; i++) {
		if (sockdata->sockpoolids[i] != UNIQUE_ID) {
			sockdata->sockpoolids[i] = UNIQUE_ID;
			host_closesocketquick (sockdata->sockpoolsocks[i]);
		}
	}

	host_sbreset ();
	write_log (L"BSDSOCK: cleanup finished\n");
}

static const TrapHandler sockfuncs[] = {
	bsdsocklib_init, bsdsocklib_Open, bsdsocklib_Close, bsdsocklib_Expunge,
	bsdsocklib_socket, bsdsocklib_bind, bsdsocklib_listen, bsdsocklib_accept,
	bsdsocklib_connect, bsdsocklib_sendto, bsdsocklib_send, bsdsocklib_recvfrom, bsdsocklib_recv,
	bsdsocklib_shutdown, bsdsocklib_setsockopt, bsdsocklib_getsockopt, bsdsocklib_getsockname,
	bsdsocklib_getpeername, bsdsocklib_IoctlSocket, bsdsocklib_CloseSocket, bsdsocklib_WaitSelect,
	bsdsocklib_SetSocketSignals, bsdsocklib_getdtablesize, bsdsocklib_ObtainSocket, bsdsocklib_ReleaseSocket,
	bsdsocklib_ReleaseCopyOfSocket, bsdsocklib_Errno, bsdsocklib_SetErrnoPtr, bsdsocklib_Inet_NtoA,
	bsdsocklib_inet_addr, bsdsocklib_Inet_LnaOf, bsdsocklib_Inet_NetOf, bsdsocklib_Inet_MakeAddr,
	bsdsocklib_inet_network, bsdsocklib_gethostbyname, bsdsocklib_gethostbyaddr, bsdsocklib_getnetbyname,
	bsdsocklib_getnetbyaddr, bsdsocklib_getservbyname, bsdsocklib_getservbyport, bsdsocklib_getprotobyname,
	bsdsocklib_getprotobynumber, bsdsocklib_vsyslog, bsdsocklib_Dup2Socket, bsdsocklib_sendmsg,
	bsdsocklib_recvmsg, bsdsocklib_gethostname, bsdsocklib_gethostid, bsdsocklib_SocketBaseTagList,
	bsdsocklib_GetSocketEvents
};

static const TCHAR * const funcnames[] = {
	L"bsdsocklib_init", L"bsdsocklib_Open", L"bsdsocklib_Close", L"bsdsocklib_Expunge",
	L"bsdsocklib_socket", L"bsdsocklib_bind", L"bsdsocklib_listen", L"bsdsocklib_accept",
	L"bsdsocklib_connect", L"bsdsocklib_sendto", L"bsdsocklib_send", L"bsdsocklib_recvfrom", L"bsdsocklib_recv",
	L"bsdsocklib_shutdown", L"bsdsocklib_setsockopt", L"bsdsocklib_getsockopt", L"bsdsocklib_getsockname",
	L"bsdsocklib_getpeername", L"bsdsocklib_IoctlSocket", L"bsdsocklib_CloseSocket", L"bsdsocklib_WaitSelect",
	L"bsdsocklib_SetSocketSignals", L"bsdsocklib_getdtablesize", L"bsdsocklib_ObtainSocket", L"bsdsocklib_ReleaseSocket",
	L"bsdsocklib_ReleaseCopyOfSocket", L"bsdsocklib_Errno", L"bsdsocklib_SetErrnoPtr", L"bsdsocklib_Inet_NtoA",
	L"bsdsocklib_inet_addr", L"bsdsocklib_Inet_LnaOf", L"bsdsocklib_Inet_NetOf", L"bsdsocklib_Inet_MakeAddr",
	L"bsdsocklib_inet_network", L"bsdsocklib_gethostbyname", L"bsdsocklib_gethostbyaddr", L"bsdsocklib_getnetbyname",
	L"bsdsocklib_getnetbyaddr", L"bsdsocklib_getservbyname", L"bsdsocklib_getservbyport", L"bsdsocklib_getprotobyname",
	L"bsdsocklib_getprotobynumber", L"bsdsocklib_vsyslog", L"bsdsocklib_Dup2Socket", L"bsdsocklib_sendmsg",
	L"bsdsocklib_recvmsg", L"bsdsocklib_gethostname", L"bsdsocklib_gethostid", L"bsdsocklib_SocketBaseTagList",
	L"bsdsocklib_GetSocketEvents"
};

static uae_u32 sockfuncvecs[sizeof (sockfuncs) / sizeof (*sockfuncs)];

static uae_u32 res_name, res_id, res_init;

uaecptr bsdlib_startup (uaecptr resaddr)
{
	if (res_name == 0 || !currprefs.socket_emu)
		return resaddr;
	put_word (resaddr + 0x0, 0x4AFC);
	put_long (resaddr + 0x2, resaddr);
	put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
	put_word (resaddr + 0xA, 0x8004); /* RTF_AUTOINIT, RT_VERSION */
	put_word (resaddr + 0xC, 0x0970); /* NT_LIBRARY, RT_PRI */
	put_long (resaddr + 0xE, res_name);
	put_long (resaddr + 0x12, res_id);
	put_long (resaddr + 0x16, res_init);
	resaddr += 0x1A;
	return resaddr;
}

void bsdlib_install (void)
{
	int i;

	if (!sockdata) {
		sockdata = xcalloc (struct sockd, 1);
		for (i = 0; i < SOCKPOOLSIZE; i++)
			sockdata->sockpoolids[i] = UNIQUE_ID;
	}

	if (!init_socket_layer ())
		return;

	res_name = ds (L"bsdsocket.library");
	res_id = ds (L"UAE bsdsocket.library 4.1");

	for (i = 0; i < (int) (sizeof (sockfuncs) / sizeof (sockfuncs[0])); i++) {
		sockfuncvecs[i] = here ();
		calltrap (deftrap2 (sockfuncs[i], TRAPFLAG_EXTRA_STACK, funcnames[i]));
		dw (RTS);
	}

	/* FuncTable */
	functable = here ();
	for (i = 1; i < 4; i++)
		dl (sockfuncvecs[i]);	/* Open / Close / Expunge */
	dl (EXPANSION_nullfunc);	/* Null */
	for (i = 4; i < (int) (sizeof (sockfuncs) / sizeof (sockfuncs[0])); i++)
		dl (sockfuncvecs[i]);
	dl (0xFFFFFFFF);		/* end of table */

	/* DataTable */
	datatable = here ();
	dw (0xE000);		/* INITBYTE */
	dw (0x0008);		/* LN_TYPE */
	dw (0x0900);		/* NT_LIBRARY */
	dw (0xE000);		/* INITBYTE */
	dw (0x0009);		/* LN_PRI */
	dw (0xCE00);		/* -50 */
	dw (0xC000);		/* INITLONG */
	dw (0x000A);		/* LN_NAME */
	dl (res_name);
	dw (0xE000);		/* INITBYTE */
	dw (0x000E);		/* LIB_FLAGS */
	dw (0x0600);		/* LIBF_SUMUSED | LIBF_CHANGED */
	dw (0xD000);		/* INITWORD */
	dw (0x0014);		/* LIB_VERSION */
	dw (0x0004);
	dw (0xD000);
	dw (0x0016);		/* LIB_REVISION */
	dw (0x0001);
	dw (0xC000);
	dw (0x0018);		/* LIB_IDSTRING */
	dl (res_id);
	dl (0x00000000);		/* end of table */

	res_init = here ();
	dl (512);
	dl (functable);
	dl (datatable);
	dl (*sockfuncvecs);

	write_log (L"bsdsocked.library installed\n");
}

#endif /* ! BSDSOCKET */
