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

char *my_strdup (const char *s)
{
    /* The casts to char * are there to shut up the compiler on HPUX */
    char *x = (char*)xmalloc(strlen((char *)s) + 1);
    strcpy(x, (char *)s);
    return x;
}

#endif

void *xmalloc (size_t n)
{
    void *a = malloc (n);
    if (a == NULL) {
	write_log ("xmalloc(%d): virtual memory exhausted\n", n);
	abort ();
    }
    return a;
}

void *xcalloc (size_t n, size_t size)
{
    void *a = calloc (n, size);
    if (a == NULL) {
	write_log ("xcalloc(%d): virtual memory exhausted\n", n * size);
	abort ();
    }
    return a;
}
