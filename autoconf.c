 /*
  * UAE - The Un*x Amiga Emulator
  *
  * AutoConfig devices
  *
  * Copyright 1995, 1996 Bernd Schmidt
  * Copyright 1996 Ed Hanway
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "autoconf.h"
#include "osdep/exectasks.h"

/* We'll need a lot of these. */
#define MAX_TRAPS 4096
static TrapFunction traps[MAX_TRAPS];
static int trapmode[MAX_TRAPS];
static const char *trapstr[MAX_TRAPS];
static uaecptr trapoldfunc[MAX_TRAPS];

static int max_trap;
int lasttrap;

/* Stack management */

/* The mechanism for doing m68k calls from native code is as follows:
 *
 * m68k code executes, stack is main
 * calltrap to execute_fn_on_extra_stack. new stack is allocated
 * do_stack_magic is called for the first time
 * current context is saved with setjmp [0]
 *  transfer_control is done
 *   native code executes on new stack
 *   native code calls call_m68k
 *   setjmp saves current context [1]
 *  longjmp back to execute_fn_on_extra_stack [0]
 * pointer to new stack is saved on m68k stack. m68k return address set
 * to RTAREA_BASE + 0xFF00. m68k PC set to called function
 * m68k function executes, stack is main
 * m68k function returns to RTAREA_BASE + 0xFF00
 * calltrap to m68k_mode_return
 * do_stack_magic is called again
 * current context is saved again with setjmp [0]
 *  this time, transfer_control is _not_ done, instead a longjmp[1]
 *  to the previously saved context
 *   native code executes again on temp stack
 *   native function returns to stack_stub
 *  longjmp[0] back to old context
 * back again!
 *
 * A bearded man enters the room, carrying a bowl of spaghetti.
 */

/* This _shouldn't_ crash with a stack size of 4096, but it does...
 * might be a bug */
#ifndef EXTRA_STACK_SIZE
#define EXTRA_STACK_SIZE 32768
#endif

static void *extra_stack_list = NULL;

static void *get_extra_stack (void)
{
    void *s = extra_stack_list;
    //if (s)
    //extra_stack_list = *(void **)s;
    if (!s) {
#ifndef _MSC_VER 
	s = xmalloc (EXTRA_STACK_SIZE);
#else
	{
	    static long opencount=0;
	    extern unsigned long *win32_stackbase;
	    int i;
	    extern unsigned long *win32_freestack[42];
	    for (i=0;i<41;i++) {//0 to MAX_SELECT_THREADS
		if(!win32_freestack[i])
		    break; //get a free block
	    }
	    s=win32_stackbase+(i*EXTRA_STACK_SIZE);
	    win32_freestack[i]=s;   
	}
#endif
    }
    return s;
}

static void free_extra_stack (void *s)
{
    //*(void **)s = extra_stack_list;
    //extra_stack_list = s;
    {
	int i;
	extern unsigned long *win32_freestack[42];
	for (i=0;i<41;i++) {//0 to MAX_SELECT_THREADS
	    if(win32_freestack[i]==s)
		break; //get a free block
	}
	win32_freestack[i]=0;
    }
}

static void stack_stub (void *s, TrapFunction f, uae_u32 *retval)
{
#ifdef CAN_DO_STACK_MAGIC
    /*printf("in stack_stub: %p %p %p %x\n", s, f, retval, (int)*retval);*/
    *retval = f ();
    /*write_log ("returning from stack_stub\n");*/
    longjmp (((jmp_buf *)s)[0], 1);
#endif
}

static void *current_extra_stack = NULL;
static uaecptr m68k_calladdr;

static void do_stack_magic (TrapFunction f, void *s, int has_retval)
{
#ifdef CAN_DO_STACK_MAGIC
    uaecptr a7;
    jmp_buf *j = (jmp_buf *)s;
    switch (setjmp (j[0])) {
     case 0:
	/* Returning directly */
	current_extra_stack = s;
	if (has_retval == -1) {
	    /*write_log ("finishing m68k mode return\n");*/
	    longjmp (j[1], 1);
	}
	/*write_log ("calling native function\n");*/
	transfer_control (s, EXTRA_STACK_SIZE, stack_stub, f, has_retval);
	/* not reached */
	abort ();

     case 1:
	/*write_log ("native function complete\n");*/
	/* Returning normally. */
	if (stack_has_retval (s, EXTRA_STACK_SIZE))
	    m68k_dreg (regs, 0) = get_retval_from_stack (s, EXTRA_STACK_SIZE);
	free_extra_stack (s);
	break;

     case 2:
	/* Returning to do a m68k call. We're now back on the main stack. */
	a7 = m68k_areg(regs, 7) -= (sizeof (void *) + 7) & ~3;
	/* Save stack to restore */
	*((void **)get_real_address (a7 + 4)) = s;
	/* Save special return address: this address contains a
	 * calltrap that will longjmp to the right stack. */
	put_long (m68k_areg (regs, 7), RTAREA_BASE + 0xFF00);
	m68k_setpc (m68k_calladdr);
	fill_prefetch_slow ();
	/*write_log ("native function calls m68k\n");*/
	break;
    }
    current_extra_stack = 0;
#endif
}

static uae_u32 execute_fn_on_extra_stack (TrapFunction f, int has_retval)
{
#ifdef CAN_DO_STACK_MAGIC
    void *s = get_extra_stack ();
    do_stack_magic (f, s, has_retval);
#endif
    return 0;
}

static uae_u32 m68k_mode_return (void)
{
#ifdef CAN_DO_STACK_MAGIC
    uaecptr a7 = m68k_areg(regs, 7);
    void *s = *(void **)get_real_address(a7);
    m68k_areg(regs, 7) += (sizeof (void *) + 3) & ~3;
    /*write_log ("doing m68k mode return\n");*/
    do_stack_magic (NULL, s, -1);
#endif
    return 0;
}

static uae_u32 call_m68k (uaecptr addr, int saveregs)
{
    volatile uae_u32 retval = 0;
    volatile int do_save = saveregs;
    if (current_extra_stack == NULL)
	abort();
#ifdef CAN_DO_STACK_MAGIC
    {
	volatile struct regstruct saved_regs;
	jmp_buf *j = (jmp_buf *)current_extra_stack;

	if (do_save)
	    saved_regs = regs;
	m68k_calladdr = addr;
	switch (setjmp(j[1])) {
	 case 0:
	    /*write_log ("doing call\n");*/
	    /* Returning directly: now switch to main stack and do the call */
	    longjmp (j[0], 2);
	 case 1:
	    /*write_log ("returning from call\n");*/
	    retval = m68k_dreg (regs, 0);
	    if (do_save)
		regs = saved_regs;
	    /* Returning after the call. */
	    break;
	}
    }
#endif
    return retval;
}

uae_u32 CallLib (uaecptr base, uae_s16 offset)
{
    int i;
    uaecptr olda6 = m68k_areg(regs, 6);
    uae_u32 retval;
#if 0
    for (i = 0; i < n_libpatches; i++) {
	if (libpatches[i].libbase == base && libpatches[i].functions[-offset/6] != NULL)
	    return (*libpatches[i].functions[-offset/6])();
    }
#endif
    m68k_areg(regs, 6) = base;
    retval = call_m68k(base + offset, 1);
    m68k_areg(regs, 6) = olda6;
    return retval;
}

/* Commonly used autoconfig strings */

uaecptr EXPANSION_explibname, EXPANSION_doslibname, EXPANSION_uaeversion;
uaecptr EXPANSION_uaedevname, EXPANSION_explibbase = 0, EXPANSION_haveV36;
uaecptr EXPANSION_bootcode, EXPANSION_nullfunc;
uaecptr EXPANSION_cddevice;

/* ROM tag area memory access */

uae_u8 *rtarea;

static uae_u32 rtarea_lget (uaecptr) REGPARAM;
static uae_u32 rtarea_wget (uaecptr) REGPARAM;
static uae_u32 rtarea_bget (uaecptr) REGPARAM;
static void rtarea_lput (uaecptr, uae_u32) REGPARAM;
static void rtarea_wput (uaecptr, uae_u32) REGPARAM;
static void rtarea_bput (uaecptr, uae_u32) REGPARAM;
static uae_u8 *rtarea_xlate (uaecptr) REGPARAM;

addrbank rtarea_bank = {
    rtarea_lget, rtarea_wget, rtarea_bget,
    rtarea_lput, rtarea_wput, rtarea_bput,
    rtarea_xlate, default_check, NULL
};

uae_u8 REGPARAM2 *rtarea_xlate (uaecptr addr)
{
    addr &= 0xFFFF;
    return rtarea + addr;
}

uae_u32 REGPARAM2 rtarea_lget (uaecptr addr)
{
    special_mem |= S_READ;
    addr &= 0xFFFF;
    return (uae_u32)(rtarea_wget (addr) << 16) + rtarea_wget (addr+2);
}

uae_u32 REGPARAM2 rtarea_wget (uaecptr addr)
{
    special_mem |= S_READ;
    addr &= 0xFFFF;
    return (rtarea[addr]<<8) + rtarea[addr+1];
}

uae_u32 REGPARAM2 rtarea_bget (uaecptr addr)
{
    special_mem |= S_READ;
    addr &= 0xFFFF;
    return rtarea[addr];
}

void REGPARAM2 rtarea_lput (uaecptr addr, uae_u32 value)
{
    special_mem |= S_WRITE;
}

void REGPARAM2 rtarea_wput (uaecptr addr, uae_u32 value)
{
    special_mem |= S_WRITE;
}

void REGPARAM2 rtarea_bput (uaecptr addr, uae_u32 value)
{
    special_mem |= S_WRITE;
}

static const int trace_traps = 1;

void REGPARAM2 call_calltrap(int func)
{
    uae_u32 retval = 0;
    int has_retval = (trapmode[func] & TRAPFLAG_NO_RETVAL) == 0;
    int implicit_rts = (trapmode[func] & TRAPFLAG_DORET) != 0;

    if (trapstr[func] && *trapstr[func] != 0 && trace_traps)
	write_log ("TRAP: %s\n", trapstr[func]);

    /* For monitoring only? */
    if (traps[func] == NULL) {
	m68k_setpc(trapoldfunc[func]);
	fill_prefetch_slow ();
	return;
    }

    if (func < max_trap) {
	if (trapmode[func] & TRAPFLAG_EXTRA_STACK) {
	    execute_fn_on_extra_stack(traps[func], has_retval);
	    return;
	}
	retval = (*traps[func])();
    } else
	write_log ("illegal emulator trap\n");

    if (has_retval)
	m68k_dreg(regs, 0) = retval;
    if (implicit_rts) {
	m68k_do_rts ();
	fill_prefetch_slow ();
    }
}

/* @$%&§ compiler bugs */
static volatile int four = 4;

uaecptr libemu_InstallFunctionFlags (TrapFunction f, uaecptr libbase, int offset,
				     int flags, const char *tracename)
{
    int i;
    uaecptr retval;
    uaecptr execbase = get_long (four);
    int trnum;
    uaecptr addr = here();
    calltrap (trnum = deftrap2 (f, flags, tracename));
    dw (RTS);

    m68k_areg(regs, 1) = libbase;
    m68k_areg(regs, 0) = offset;
    m68k_dreg(regs, 0) = addr;
    retval = CallLib (execbase, -420);

    trapoldfunc[trnum] = retval;
#if 0
    for (i = 0; i < n_libpatches; i++) {
	if (libpatches[i].libbase == libbase)
	    break;
    }
    if (i == n_libpatches) {
	int j;
	libpatches[i].libbase = libbase;
	for (j = 0; j < 300; j++)
	    libpatches[i].functions[j] = NULL;
	n_libpatches++;
    }
    libpatches[i].functions[-offset/6] = f;
#endif
    return retval;
}

/* some quick & dirty code to fill in the rt area and save me a lot of
 * scratch paper
 */

static int rt_addr;
static int rt_straddr;

uae_u32 addr (int ptr)
{
    return (uae_u32)ptr + RTAREA_BASE;
}

void db (uae_u8 data)
{
    rtarea[rt_addr++] = data;
}

void dw (uae_u16 data)
{
    rtarea[rt_addr++] = (uae_u8)(data >> 8);
    rtarea[rt_addr++] = (uae_u8)data;
}

void dl (uae_u32 data)
{
    rtarea[rt_addr++] = data >> 24;
    rtarea[rt_addr++] = data >> 16;
    rtarea[rt_addr++] = data >> 8;
    rtarea[rt_addr++] = data;
}

/* store strings starting at the end of the rt area and working
 * backward.  store pointer at current address
 */

uae_u32 ds (char *str)
{
    int len = strlen (str) + 1;

    rt_straddr -= len;
    strcpy ((char *)rtarea + rt_straddr, str);

    return addr (rt_straddr);
}

void calltrap (uae_u32 n)
{
    dw (0xA000 + n);
}

void org (uae_u32 a)
{
    rt_addr = a - RTAREA_BASE;
}

uae_u32 here (void)
{
    return addr (rt_addr);
}

int deftrap2 (TrapFunction func, int mode, const char *str)
{
    int num = max_trap++;
    traps[num] = func;
    trapstr[num] = str;
    trapmode[num] = mode;
    return num;
}

int deftrap (TrapFunction func)
{
    return deftrap2 (func, 0, "");
}

void align (int b)
{
    rt_addr = (rt_addr + b - 1) & ~(b - 1);
}

static uae_u32 nullfunc(void)
{
    write_log ("Null function called\n");
    return 0;
}

static uae_u32 getchipmemsize (void)
{
    return allocated_chipmem;
}

static uae_u32 uae_puts (void)
{
    puts (get_real_address (m68k_areg (regs, 0)));
    return 0;
}

static void rtarea_init_mem (void)
{
    rtarea = mapped_malloc (0x10000, "rtarea");
    if (!rtarea) {
	write_log ("virtual memory exhausted (rtarea)!\n");
	abort ();
    }
    rtarea_bank.baseaddr = rtarea;
}

void rtarea_init (void)
{
    uae_u32 a;
    char uaever[100];

    rt_straddr = 0xFF00 - 2;
    rt_addr = 0;
    max_trap = 0;

    rtarea_init_mem ();

    sprintf (uaever, "uae-%d.%d.%d", UAEMAJOR, UAEMINOR, UAESUBREV);

    EXPANSION_uaeversion = ds (uaever);
    EXPANSION_explibname = ds ("expansion.library");
    EXPANSION_doslibname = ds ("dos.library");
    EXPANSION_uaedevname = ds ("uae.device");

    deftrap (NULL); /* Generic emulator trap */
    lasttrap = 0;

    EXPANSION_nullfunc = here ();
    calltrap (deftrap (nullfunc));
    dw (RTS);

    a = here();
    /* Standard "return from 68k mode" trap */
    org (RTAREA_BASE + 0xFF00);
    calltrap (deftrap2 (m68k_mode_return, TRAPFLAG_NO_RETVAL, ""));

    org (RTAREA_BASE + 0xFF80);
    calltrap (deftrap2 (getchipmemsize, TRAPFLAG_DORET, ""));

    org (RTAREA_BASE + 0xFF10);
    calltrap (deftrap2 (uae_puts, TRAPFLAG_NO_RETVAL, ""));
    dw (RTS);

    org (a);

    filesys_install_code ();
}

volatile int uae_int_requested = 0;

void set_uae_int_flag (void)
{
    rtarea[0xFFFB] = uae_int_requested;
}

void rtarea_setup(void)
{
}
