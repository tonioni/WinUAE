 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Miscellaneous machine dependent support functions and definitions
  *
  * Copyright 1996 Bernd Schmidt
  */


#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "machdep/m68k.h"
#include "events.h"

#ifdef X86_ASSEMBLY

#ifndef USE_UNDERSCORE
#define LARGE_ALIGNMENT ".align 16\n"
#else
#define LARGE_ALIGNMENT ".align 4,0x90\n"
#endif

struct flag_struct regflags;

/*
 * Careful: No unique labels. Unfortunately, not each installation of GCC 
 * comes with GAS. Bletch.
 */

int fast_memcmp(const void *foo, const void *bar, int len)
{
    int differs, baz;
    __asm__ __volatile__ ("subl $4, %2\n"
			  "jc  LLA2\n"
			  "LLA1:\n"
			  "movl (%0),%%ebx\n"
			  "cmpl (%1),%%ebx\n"
			  "jne LLA5\n"
			  "addl $4, %0\n"
			  "addl $4, %1\n"
			  "subl $4, %2\n"
			  "jnc  LLA1\n"
			  "LLA2:\n"
			  "addl $4, %2\n"
			  "jz LLA4\n"
			  "LLA3:\n"
			  "movb (%0),%%bl\n"
			  "cmpb (%1),%%bl\n"
			  "jne LLA5\n"
			  "incl %0\n"
			  "incl %1\n"
			  "decl %2\n"
			  "jnz LLA3\n"
			  "LLA4:\n"
			  "movl $0, %3\n"
			  "jmp LLA6\n"
			  "LLA5:\n"
			  "movl $1, %3\n"
			  "LLA6:\n"
			  : "=&r" (foo), "=&r" (bar), "=&rm" (len), "=rm" (differs),
			    "=&b" (baz)
			  : "0" (foo), "1" (bar), "2" (len), "3" (baz) : "cc");
    return differs;
}

int memcmpy(void *foo, const void *bar, int len)
{
    int differs = 0, baz = 0, uupzuq = 0;

    __asm__ __volatile__ ("subl %1, %2\n"
			  "subl $16, %3\n"
			  "jc LLB7\n"
			  LARGE_ALIGNMENT
			  "LLB8:\n"
			  "movl (%2,%1),%%ebx\n"
			  "movl (%1),%%ecx\n"
			  "cmpl %%ebx, %%ecx\n"
			  "jne LLC1\n"
			  
			  "movl 4(%2,%1),%%ebx\n"
			  "movl 4(%1),%%ecx\n"
			  "cmpl %%ebx, %%ecx\n"
			  "jne LLC2\n"
			  
			  "movl 8(%2,%1),%%ebx\n"
			  "movl 8(%1),%%ecx\n"
			  "cmpl %%ebx, %%ecx\n"
			  "jne LLC3\n"
			  
			  "movl 12(%2,%1),%%ebx\n"
			  "movl 12(%1),%%ecx\n"
			  "cmpl %%ebx, %%ecx\n"
			  "jne LLC4\n"
			  
			  "addl $16, %1\n"
			  "subl $16, %3\n"
			  "jnc  LLB8\n"
			  
			  "LLB7:\n"
			  "addl $16, %3\n"
			  "subl $4, %3\n"
			  "jc  LLB2\n"

			  "LLB1:\n"
			  "movl (%2,%1),%%ebx\n"
			  "movl (%1),%%ecx\n"
			  "cmpl %%ebx, %%ecx\n"
			  "jne LLC5\n"
			  "addl $4, %1\n"
			  "subl $4, %3\n"
			  "jnc  LLB1\n"
			  
			  "LLB2:\n"
			  "addl $4, %3\n"
			  "jz LLB9\n"
			  
			  "LLB3:\n"
			  "movb (%2,%1),%%bl\n"
			  "movb (%1),%%cl\n"
			  "cmpb %%bl,%%cl\n"
			  "jne LLC6\n"
			  "incl %1\n"
			  "decl %3\n"
			  "jnz LLB3\n"
			  
			  "jmp LLB9\n"

			  LARGE_ALIGNMENT
			  /* Once we find a difference, we switch to plain memcpy() */
			  "LLC01:\n"
			  "movl (%2,%1),%%ebx\n"
			  "LLC1:\n"
			  "movl %%ebx, (%1)\n"
			  
			  "movl 4(%2,%1),%%ebx\n"
			  "LLC2:\n"
			  "movl %%ebx, 4(%1)\n"
			  
			  "movl 8(%2,%1),%%ebx\n"
			  "LLC3:\n"
			  "movl %%ebx, 8(%1)\n"
			  
			  "movl 12(%2,%1),%%ebx\n"
			  "LLC4:\n"
			  "movl %%ebx, 12(%1)\n"
			  
			  "addl $16, %1\n"
#if 0
			  "movl $1,%0\n"
			  
			  "addl %1,%2\n"
			  "movl %3,%%ecx\n"
			  "shrl $2,%%ecx\n"
			  "je LLC02a\n"
			  "rep\n"
			  "movsl\n"
			  "andl $3,%3\n"
			  "je LLB9\n"
			  "LLC02a:\n"
			  "movb (%2),%%bl\n"
			  
			  "movb %%bl,(%1)\n"
			  "incl %1\n"
			  "decl %3\n"
			  "jnz LLC02a\n"
			  "jmp LLB9\n"
#else
#if 0
			  "movl $1,%0\n"
			  "jnc  LLB8\n"
#else
			  "subl $16, %3\n"
			  "jnc LLC01\n"
#endif

			  "addl $16, %3\n"
			  "subl $4, %3\n"
			  "jc  LLC03\n"
#endif
			  
			  "LLC02:\n"
			  "movl (%2,%1),%%ebx\n"
			  "LLC5:\n"
			  "movl %%ebx, (%1)\n"
			  "addl $4, %1\n"
			  "subl $4, %3\n"
			  "jnc  LLC02\n"
			  
			  "LLC03:\n"
			  "addl $4, %3\n"
			  "jz LLC05\n"
			  
			  "LLC04:\n"
			  "movb (%2,%1),%%bl\n"
			  "LLC6:\n"
			  "movb %%bl,(%1)\n"
			  "incl %1\n"
			  "decl %3\n"
			  "jnz LLC04\n"
			  
			  "LLC05:\n"
			  "movl $1,%0\n"
			  "LLB9:"
			  : "=m" (differs)
			  : "D" (foo), "S" (bar), "r" (len), "b" (baz), "c" (uupzuq), "0" (differs) : "cc", "memory");
    /* Now tell the compiler that foo, bar and len have been modified 
     * If someone finds a way to express all this cleaner in constraints that
     * GCC 2.7.2 understands, please FIXME */
    __asm__ __volatile__ ("" : "=rm" (foo), "=rm" (bar), "=rm" (len) : :  "ebx", "ecx", "edx", "eax", "esi", "memory");

    return differs;
}

#else
struct flag_struct regflags;

int fast_memcmp(const void *foo, const void *bar, int len)
{
    return memcmp(foo, bar, len);
}

int memcmpy(void *foo, const void *bar, int len)
{
    int differs = memcmp(foo, bar, len);
    memcpy(foo, bar, len);
    return differs;
}

#endif

/* All the Win32 configurations handle this in od-win32/win32.c */
#ifndef _WIN32

#include <signal.h>

static volatile frame_time_t last_time, best_time;
static volatile int loops_to_go;

#ifdef __cplusplus
static RETSIGTYPE alarmhandler(...)
#else
static RETSIGTYPE alarmhandler(int foo)
#endif
{
    frame_time_t bar;
    bar = read_processor_time ();
    if (bar - last_time < best_time)
	best_time = bar - last_time;
    if (--loops_to_go > 0) {
	signal (SIGALRM, alarmhandler);
	last_time = read_processor_time();
	alarm (1);
    }
}

#ifdef __cplusplus
static RETSIGTYPE illhandler(...)
#else
static RETSIGTYPE illhandler(int foo)
#endif
{
    rpt_available = 0;
}

void machdep_init (void)
{
    rpt_available = 1;
    signal (SIGILL, illhandler);
    read_processor_time ();
    signal (SIGILL, SIG_DFL);
    if (! rpt_available) {
	fprintf (stderr, "Your processor does not support the RDTSC instruction.\n");
	return;
    }
    fprintf (stderr, "Calibrating delay loop.. ");
    fflush (stderr);
    best_time = (frame_time_t)-1;
    loops_to_go = 5;
    signal (SIGALRM, alarmhandler);
    /* We want exact values... */
    sync (); sync (); sync ();
    last_time = read_processor_time();
    alarm (1);
    while (loops_to_go != 0)
	usleep (1000000);
    fprintf (stderr, "ok - %.2f BogoMIPS\n",
	     ((double)best_time / 1000000), best_time);
    vsynctime = best_time / 50;
}

#endif
