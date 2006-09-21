/*
  * UAE - The Un*x Amiga Emulator
  *
  * Definitions for accessing cycle counters on a given machine, if possible.
  *
  * Copyright 1997, 1998 Bernd Schmidt
  * Copyright 1999 Brian King - Win32 specific
  */
#ifndef _RPT_H_
#define _RPT_H_

#ifdef HIBERNATE_TEST
extern int rpt_skip_trigger;
#endif

typedef unsigned long frame_time_t;

/* For CPUs that lack the rdtsc instruction or systems that change CPU frequency on the fly (most laptops) */
extern frame_time_t read_processor_time_qpc(void);
extern int useqpc;

STATIC_INLINE frame_time_t read_processor_time_qpc (void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter( &counter );
    if (useqpc > 0)
	return (frame_time_t)(counter.LowPart);
    return (frame_time_t)(counter.QuadPart >> 6);
}

STATIC_INLINE frame_time_t read_processor_time (void)
{
    frame_time_t foo;

    if (useqpc) /* No RDTSC or RDTSC is not stable */
	return read_processor_time_qpc();

#if defined(X86_MSVC_ASSEMBLY)
    {
	frame_time_t bar;
	__asm
	{
	    rdtsc
	    mov foo, eax
	    mov bar, edx
	}
	/* very high speed CPU's RDTSC might overflow without this.. */
	foo >>= 6;
	foo |= bar << 26;
    }
#else
    foo = 0;
#endif
#ifdef HIBERNATE_TEST
    if (rpt_skip_trigger) {
	foo += rpt_skip_trigger;
	rpt_skip_trigger = 0;
    }
#endif
    return foo;
}

#endif
