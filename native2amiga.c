 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Call Amiga Exec functions outside the main UAE thread.
  *
  * Copyright 1999 Patrick Ohly
  * 
  * Uses the EXTER interrupt that is setup in filesys.c
  * and needs thread support.
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "disk.h"
#include "autoconf.h"
#include "filesys.h"
#include "execlib.h"
#include "native2amiga.h"

smp_comm_pipe native2amiga_pending;

/*
 * to be called when setting up the hardware
 */

void native2amiga_install (void)
{
    init_comm_pipe (&native2amiga_pending, 10, 2);
}

/*
 * to be called when the Amiga boots, i.e. by filesys_diagentry()
 */
void native2amiga_startup (void)
{
}

#ifdef SUPPORT_THREADS

void uae_Cause(uaecptr interrupt)
{
    write_comm_pipe_int (&native2amiga_pending, 3, 0);
    write_comm_pipe_u32 (&native2amiga_pending, interrupt, 1);

    uae_int_requested = 1;
}

void uae_ReplyMsg(uaecptr msg)
{
    write_comm_pipe_int (&native2amiga_pending, 2, 0);
    write_comm_pipe_u32 (&native2amiga_pending, msg, 1);

    uae_int_requested = 1;
}

void uae_PutMsg(uaecptr port, uaecptr msg)
{
    uae_pt data;
    data.i = 1;
    write_comm_pipe_int (&native2amiga_pending, 1, 0);
    write_comm_pipe_u32 (&native2amiga_pending, port, 0);
    write_comm_pipe_u32 (&native2amiga_pending, msg, 1);

    uae_int_requested = 1;
}

void uae_Signal(uaecptr task, uae_u32 mask)
{
    write_comm_pipe_int (&native2amiga_pending, 0, 0);
    write_comm_pipe_u32 (&native2amiga_pending, task, 0);
    write_comm_pipe_int (&native2amiga_pending, mask, 1);
    
    uae_int_requested = 1;
}

void uae_NotificationHack(uaecptr port, uaecptr nr)
{
    write_comm_pipe_int (&native2amiga_pending, 4, 0);
    write_comm_pipe_int (&native2amiga_pending, port, 0);
    write_comm_pipe_int (&native2amiga_pending, nr, 1);
    
    uae_int_requested = 1;
}

#endif

void uae_NewList(uaecptr list)
{
    put_long (list, list + 4);
    put_long (list + 4, 0);
    put_long (list + 8, list);
}

uaecptr uae_AllocMem (uae_u32 size, uae_u32 flags)
{
    m68k_dreg (regs, 0) = size;
    m68k_dreg (regs, 1) = flags;
    write_log ("allocmem(%d,%08.8X)\n", size, flags);
    return CallLib (get_long (4), -198); /* AllocMem */
}

void uae_FreeMem (uaecptr memory, uae_u32 size)
{
    m68k_dreg (regs, 0) = size;
    m68k_areg (regs, 1) = memory;
    CallLib (get_long (4), -0xD2); /* FreeMem */
}
