 /*
  * UAE - The Un*x Amiga Emulator
  *
  * bsdsocket.library emulation
  *
  * Copyright 1997,98 Mathias Ortmann
  *
  */

//#define TRACING_ENABLED

#ifdef TRACING_ENABLED
#define TRACE(x) do { write_log x; } while(0)
#else
#define TRACE(x)
#endif

extern int init_socket_layer (void);
extern void deinit_socket_layer (void);

/* inital size of per-process descriptor table (currently fixed) */
#define DEFAULT_DTABLE_SIZE 64

#define SCRATCHBUFSIZE 128

#define MAXPENDINGASYNC 512

#define MAXADDRLEN 256

/* allocated and maintained on a per-task basis */
struct socketbase {
    struct socketbase *next;
    struct socketbase *nextsig;	/* queue for tasks to signal */

    int dosignal;		/* signal flag */
    uae_u32 ownertask;		/* task that opened the library */
    int signal;			/* signal allocated for that task */
    int sb_errno, sb_herrno;	/* errno and herrno variables */
    uae_u32 errnoptr, herrnoptr;	/* pointers */
    uae_u32 errnosize, herrnosize;	/* pinter sizes */
    int dtablesize;		/* current descriptor/flag etc. table size */
    int *dtable;		/* socket descriptor table */
    int *ftable;		/* socket flags */
    int resultval;
    uae_u32 hostent;		/* pointer to the current hostent structure (Amiga mem) */
    uae_u32 hostentsize;
    uae_u32 protoent;		/* pointer to the current protoent structure (Amiga mem) */
    uae_u32 protoentsize;
    uae_u32 servent;		/* pointer to the current servent structure (Amiga mem) */
    uae_u32 serventsize;
    uae_u32 sigstosend;
    uae_u32 eventsigs;		/* EVENT sigmask */
    uae_u32 eintrsigs;		/* EINTR sigmask */
    int eintr;			/* interrupted by eintrsigs? */
    int eventindex;		/* current socket looked at by GetSocketEvents() to prevent starvation */

    /* host-specific fields below */
#ifdef _WIN32
    unsigned int sockAbort;	/* for aborting WinSock2 select() (damn Microsoft) */
    unsigned int sockAsync;	/* for aborting WSBAsyncSelect() in window message handler */
    int needAbort;		/* abort flag */
    void *hAsyncTask;		/* async task handle */
    void *hEvent;		/* thread event handle */
    unsigned int *mtable;	/* window messages allocated for asynchronous event notification */
#endif
} *socketbases;


#define LIBRARY_SIZEOF 36

struct UAEBSDBase {
    char dummy[LIBRARY_SIZEOF];
    struct socketbase *sb;
    char scratchbuf[SCRATCHBUFSIZE];
};

/* socket flags */
/* socket events to report */
#define REP_ACCEPT	 0x01	/* there is a connection to accept() */
#define REP_CONNECT	 0x02	/* connect() completed */
#define REP_OOB		 0x04	/* socket has out-of-band data */
#define REP_READ	 0x08	/* socket is readable */
#define REP_WRITE	 0x10	/* socket is writeable */
#define REP_ERROR	 0x20	/* asynchronous error on socket */
#define REP_CLOSE	 0x40	/* connection closed (graceful or not) */
#define REP_ALL      0x7f
/* socket events that occurred */
#define SET_ACCEPT	 0x0100	/* there is a connection to accept() */
#define SET_CONNECT	 0x0200	/* connect() completed */
#define SET_OOB		 0x0400	/* socket has out-of-band data */
#define SET_READ	 0x0800	/* socket is readable */
#define SET_WRITE	 0x1000	/* socket is writeable */
#define SET_ERROR	 0x2000	/* asynchronous error on socket */
#define SET_CLOSE	 0x4000	/* connection closed (graceful or not) */
#define SET_ALL      0x7f00
/* socket properties */
#define SF_BLOCKING 0x80000000
#define SF_BLOCKINGINPROGRESS 0x40000000

struct socketbase *get_socketbase (void);

extern uae_u32 addstr (uae_u32 *, char *);
extern uae_u32 addmem (uae_u32 *, char *, int len);

extern char *strncpyah (char *, uae_u32, int);
extern char *strcpyah (char *, uae_u32);
extern uae_u32 strcpyha (uae_u32, char *);
extern uae_u32 strncpyha (uae_u32, char *, int);

#define SB struct socketbase *sb

extern void seterrno (SB, int);
extern void setherrno (SB, int);

extern void sockmsg (unsigned int, unsigned long, unsigned long);
extern void sockabort (SB);

extern void addtosigqueue (SB, int);
extern void removefromsigqueue (SB);
extern void sigsockettasks (void);
extern void locksigqueue (void);
extern void unlocksigqueue (void);

extern BOOL checksd(SB, int sd);
extern void setsd(SB, int ,int );
extern int getsd (SB, int);
extern int getsock (SB, int);
extern void releasesock (SB, int);

extern void waitsig (SB);
extern void cancelsig (SB);

extern int host_sbinit (SB);
extern void host_sbcleanup (SB);
extern void host_sbreset (void);
extern void host_closesocketquick (int);

extern int host_dup2socket (SB, int, int);
extern int host_socket (SB, int, int, int);
extern uae_u32 host_bind (SB, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_listen (SB, uae_u32, uae_u32);
extern void host_accept (SB, uae_u32, uae_u32, uae_u32);
extern void host_sendto (SB, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32);
extern void host_recvfrom (SB, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_shutdown (SB, uae_u32, uae_u32);
extern void host_setsockopt (SB, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_getsockopt (SB, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_getsockname (SB, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_getpeername (SB, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_IoctlSocket (SB, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_shutdown (SB, uae_u32, uae_u32);
extern int host_CloseSocket (SB, int);
extern void host_connect (SB, uae_u32, uae_u32, uae_u32);
extern void host_WaitSelect (SB, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32);
extern uae_u32 host_SetSocketSignals (void);
extern uae_u32 host_getdtablesize (void);
extern uae_u32 host_ObtainSocket (void);
extern uae_u32 host_ReleaseSocket (void);
extern uae_u32 host_ReleaseCopyOfSocket (void);
extern uae_u32 host_Inet_NtoA (SB, uae_u32);
extern uae_u32 host_inet_addr (uae_u32);
extern uae_u32 host_Inet_LnaOf (void);
extern uae_u32 host_Inet_NetOf (void);
extern uae_u32 host_Inet_MakeAddr (void);
extern uae_u32 host_inet_network (void);
extern void host_gethostbynameaddr (SB, uae_u32, uae_u32, long);
extern uae_u32 host_getnetbyname (void);
extern uae_u32 host_getnetbyaddr (void);
extern void host_getservbynameport (SB, uae_u32, uae_u32, uae_u32);
extern void host_getprotobyname (SB, uae_u32);
extern uae_u32 host_getprotobynumber (void);
extern uae_u32 host_vsyslog (void);
extern uae_u32 host_Dup2Socket (void);
extern uae_u32 host_gethostname (uae_u32, uae_u32);


extern void bsdlib_install (void);
extern void bsdlib_reset (void);
