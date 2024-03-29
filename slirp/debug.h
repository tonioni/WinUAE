/*
 * Copyright (c) 1995 Danny Gasparovski.
 * 
 * Please read the file COPYRIGHT for the 
 * terms and conditions of the copyright.
 */

#define SLIRP_DEBUG 0

#define PRN_STDERR	1
#define PRN_SPRINTF	2

extern FILE *dfd;
extern FILE *lfd;
extern int dostats;
extern int slirp_debug;

#define DBG_CALL 0x1
#define DBG_MISC 0x2
#define DBG_ERROR 0x4
#define DEBUG_DEFAULT DBG_CALL|DBG_MISC|DBG_ERROR

#if SLIRP_DEBUG

#define DEBUG_CALL(x) if (slirp_debug & DBG_CALL) { write_log(x); }
#define DEBUG_ARG(x, y) if (slirp_debug & DBG_CALL) { write_log(" "); write_log(x, y); write_log("\n"); }
#define DEBUG_ARGS(x) if (slirp_debug & DBG_CALL) { write_log x ;}
#define DEBUG_MISC(x) if (slirp_debug & DBG_MISC) { write_log x ;}
#define DEBUG_ERROR(x) if (slirp_debug & DBG_ERROR) {write_log x; }

#else

#define DEBUG_CALL(x)
#define DEBUG_ARG(x, y)
#define DEBUG_ARGS(x)
#define DEBUG_MISC(x)
#define DEBUG_ERROR(x)

#endif

void debug_init(char *, int);
//void ttystats(struct ttys *);
void allttystats(void);
void ipstats(void);
void vjstats(void);
void tcpstats(void);
void udpstats(void);
void icmpstats(void);
void mbufstats(void);
void sockstats(void);
void slirp_exi(int);

