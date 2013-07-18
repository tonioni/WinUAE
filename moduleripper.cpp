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
	buf = p = xmalloc (uae_u8, size);
	if (!buf)
		return;
	memcpy (p, chipmem_bank.baseaddr, currprefs.chipmem_size);
	p += currprefs.chipmem_size;
	mc (p, fastmem_bank.start, currprefs.fastmem_size);
	p += currprefs.fastmem_size;
	mc (p, bogomem_bank.start, currprefs.bogomem_size);
	p += currprefs.bogomem_size;
	mc (p, a3000lmem_bank.start, currprefs.mbresmem_low_size);
	p += currprefs.mbresmem_low_size;
	mc (p, a3000hmem_bank.start, currprefs.mbresmem_high_size);
	p += currprefs.mbresmem_high_size;
	mc (p, z3fastmem_bank.start, currprefs.z3fastmem_size);
	p += currprefs.z3fastmem_size;
	mc (p, z3fastmem_bank.start + currprefs.z3fastmem_size, currprefs.z3fastmem2_size);
	p += currprefs.z3fastmem2_size;

	got = 0;
	canceled = 0;
#ifdef _WIN32
	__try {
#endif
		prowizard_search (buf, size);
#ifdef _WIN32
	} __except(ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
		write_log (_T("prowizard scan crashed\n"));
	}
#endif
	if (!got)
		notify_user (NUMSG_MODRIP_NOTFOUND);
	else if (!canceled)
		notify_user (NUMSG_MODRIP_FINISHED);
	xfree (buf);
}

extern "C"
{

FILE *moduleripper_fopen (const char *aname, const char *amode)
{
	TCHAR tmp2[MAX_DPATH];
	TCHAR tmp[MAX_DPATH];
	TCHAR *name, *mode;
	FILE *f;

	fetch_ripperpath (tmp, sizeof tmp);
	name = au (aname);
	mode = au (amode);
	_stprintf (tmp2, _T("%s%s"), tmp, name);
	f = _tfopen (tmp2, mode);
	xfree (mode);
	xfree (name);
	return f;
}

FILE *moduleripper2_fopen (const char *name, const char *mode, const char *aid, int addr, int size)
{
	TCHAR msg[MAX_DPATH], msg2[MAX_DPATH];
	TCHAR *id;
	int ret;

	if (canceled)
		return NULL;
	got++;
	translate_message (NUMSG_MODRIP_SAVE, msg);
	id = au (aid);
	_stprintf (msg2, msg, id, addr, size);
	ret = gui_message_multibutton (2, msg2);
	xfree (id);
	if (ret < 0)
		canceled = 1;
	if (ret < 0 || ret != 1)
		return NULL;
	return moduleripper_fopen (name, mode);
}

void pw_write_log (const char *format,...)
{
}

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
