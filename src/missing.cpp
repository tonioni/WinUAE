/*
* UAE - The Un*x Amiga Emulator
*
* Various stuff missing in some OSes.
*
* Copyright 1997 Bernd Schmidt
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "uae.h"

#ifndef HAVE_STRDUP

TCHAR *my_strdup (const TCHAR *s)
{
	TCHAR *x = (char*)malloc(strlen((TCHAR *)s) + 1);
	strcpy(x, (TCHAR *)s);
	return x;
}

#endif

#if 0

void *xmalloc (size_t n)
{
	void *a = malloc (n);
	return a;
}

void *xcalloc (size_t n, size_t size)
{
	void *a = calloc (n, size);
	return a;
}

void xfree (const void *p)
{

	free (p);
}

#endif
