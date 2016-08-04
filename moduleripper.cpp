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
#include "uae/io.h"
#include "memory.h"
#include "uae/seh.h"
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
	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		size += currprefs.fastmem[i].size;
		size += currprefs.z3fastmem[i].size;
	}
	size += currprefs.bogomem_size;
	size += currprefs.mbresmem_low_size;
	size += currprefs.mbresmem_high_size;
	buf = p = xmalloc (uae_u8, size);
	if (!buf)
		return;
	memcpy (p, chipmem_bank.baseaddr, currprefs.chipmem_size);
	p += currprefs.chipmem_size;
	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		mc (p, fastmem_bank[i].start, currprefs.fastmem[i].size);
		p += currprefs.fastmem[i].size;
	}
	mc (p, bogomem_bank.start, currprefs.bogomem_size);
	p += currprefs.bogomem_size;
	mc (p, a3000lmem_bank.start, currprefs.mbresmem_low_size);
	p += currprefs.mbresmem_low_size;
	mc (p, a3000hmem_bank.start, currprefs.mbresmem_high_size);
	p += currprefs.mbresmem_high_size;
	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		mc (p, z3fastmem_bank[i].start, currprefs.z3fastmem[i].size);
		p += currprefs.z3fastmem[i].size;
	}

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

static void namesplit(TCHAR *s)
{
	int l;

	l = _tcslen(s) - 1;
	while (l >= 0) {
		if (s[l] == '.')
			s[l] = 0;
		if (s[l] == '\\' || s[l] == '/' || s[l] == ':' || s[l] == '?') {
			l++;
			break;
		}
		l--;
	}
	if (l > 0)
		memmove(s, s + l, (_tcslen(s + l) + 1) * sizeof (TCHAR));
}

static void moduleripper_filename(const char *aname, TCHAR *out, bool fullpath)
{
	TCHAR tmp[MAX_DPATH];
	TCHAR img[MAX_DPATH];
	TCHAR *name;

	fetch_ripperpath(tmp, sizeof tmp / sizeof(TCHAR));

	img[0] = 0;
	if (currprefs.floppyslots[0].dfxtype >= 0)
		_tcscpy(img, currprefs.floppyslots[0].df);
	else if (currprefs.cdslots[0].inuse)
		_tcscpy(img, currprefs.cdslots[0].name);
	if (img[0]) {
		namesplit(img);
		_tcscat(img, _T("_"));
	}

	name = au(aname);
	if (!fullpath)
		tmp[0] = 0;
	_stprintf(out, _T("%s%s%s"), tmp, img, name);
	xfree(name);
}

extern "C"
{

FILE *moduleripper_fopen (const char *aname, const char *amode)
{
	TCHAR outname[MAX_DPATH];
	TCHAR *mode;
	FILE *f;

	moduleripper_filename(aname, outname, true);

	mode = au (amode);
	f = uae_tfopen (outname, mode);
	xfree (mode);
	return f;
}

FILE *moduleripper2_fopen (const char *name, const char *mode, const char *aid, int addr, int size)
{
	TCHAR msg[MAX_DPATH], msg2[MAX_DPATH];
	TCHAR outname[MAX_DPATH];
	TCHAR *id;
	int ret;

	if (canceled)
		return NULL;
	got++;
	translate_message (NUMSG_MODRIP_SAVE, msg);
	moduleripper_filename(name, outname, false);
	id = au (aid);
	_stprintf (msg2, msg, id, addr, size, outname);
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
