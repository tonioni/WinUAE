 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Pro-Wizard glue code
  *
  * Copyright 2004 Toni Wilen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef PROWIZARD

#include "options.h"
#include "memory.h"
#include "moduleripper.h"
#include "gui.h"
#include "uae.h"

static int got, canceled;

static void mc (uae_u8 *d, uaecptr s, int size)
{
    int i;

    for (i = 0; i < size; i++)
	d[i] = get_byte (s++);
}

#ifdef _WIN32
static LONG WINAPI ExceptionFilter (struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void moduleripper (void)
{
    int size;
    uae_u8 *buf, *p;

    size = currprefs.chipmem_size;
    size += currprefs.fastmem_size;
    size += currprefs.bogomem_size;
    size += currprefs.mbresmem_low_size;
    size += currprefs.mbresmem_high_size;
    size += currprefs.z3fastmem_size;
    size += currprefs.z3fastmem2_size;
    buf = p = (uae_u8*)xmalloc (size);
    if (!buf)
	return;
    memcpy (p, chipmemory, currprefs.chipmem_size);
    p += currprefs.chipmem_size;
    mc (p, fastmem_start, currprefs.fastmem_size);
    p += currprefs.fastmem_size;
    mc (p, bogomem_start, currprefs.bogomem_size);
    p += currprefs.bogomem_size;
    mc (p, a3000lmem_start, currprefs.mbresmem_low_size);
    p += currprefs.mbresmem_low_size;
    mc (p, a3000hmem_start, currprefs.mbresmem_high_size);
    p += currprefs.mbresmem_high_size;
    mc (p, z3fastmem_start, currprefs.z3fastmem_size);
    p += currprefs.z3fastmem_size;
    mc (p, z3fastmem_start + currprefs.z3fastmem_size, currprefs.z3fastmem2_size);
    p += currprefs.z3fastmem2_size;

    got = 0;
    canceled = 0;
#ifdef _WIN32
    __try {
#endif
	prowizard_search (buf, size);
#ifdef _WIN32
    } __except(ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
	write_log ("prowizard scan crashed\n");
    }
#endif
    if (!got)
	notify_user (NUMSG_MODRIP_NOTFOUND);
    else if (!canceled)
	notify_user (NUMSG_MODRIP_FINISHED);
    xfree (buf);
}

FILE *moduleripper_fopen (const char *name, const char *mode)
{
    char tmp[MAX_DPATH], tmp2[MAX_DPATH];
    fetch_ripperpath (tmp, sizeof tmp);
    sprintf (tmp2, "%s%s", tmp, name);
    return fopen (tmp2, mode);
}

FILE *moduleripper2_fopen (const char *name, const char *mode, const char *id, int addr, int size)
{
    char msg[MAX_DPATH], msg2[MAX_DPATH];
    int ret;

    if (canceled)
	return NULL;
    got++;
    translate_message (NUMSG_MODRIP_SAVE, msg);
    sprintf (msg2, msg, id, addr, size);
    ret = gui_message_multibutton (2, msg2);
    if (ret < 0)
	canceled = 1;
    if (ret < 0 || ret != 1)
	return NULL;
    return moduleripper_fopen (name, mode);
}

#else

FILE *moduleripper_fopen (const char *name, const char *mode)
{
    return NULL;
}
FILE *moduleripper2_fopen (const char *name, const char *mode, const char *id)
{
    return NULL;
}

#endif
