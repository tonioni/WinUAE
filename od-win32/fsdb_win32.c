 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Library of functions to make emulated filesystem as independent as
  * possible of the host filesystem's capabilities.
  * This is the Win32 version.
  *
  * Copyright 1997 Mathias Ortmann
  * Copyright 1999 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "fsdb.h"
#include <windows.h>

/* these are deadly (but I think allowed on the Amiga): */
#define NUM_EVILCHARS 7
static char evilchars[NUM_EVILCHARS] = { '\\', '*', '?', '\"', '<', '>', '|' };

/* Return nonzero for any name we can't create on the native filesystem.  */
int fsdb_name_invalid (const char *n)
{
    int i;
    char a = n[0];
    char b = (a == '\0' ? a : n[1]);
    char c = (b == '\0' ? b : n[2]);
    char d = (c == '\0' ? c : n[3]);
    int l = strlen (n), ll;

    if (a >= 'a' && a <= 'z')
        a -= 32;
    if (b >= 'a' && b <= 'z')
        b -= 32;
    if (c >= 'a' && c <= 'z')
        c -= 32;

    /* reserved dos devices */
    ll = 0;
    if (a == 'A' && b == 'U' && c == 'X') ll = 3; /* AUX  */
    if (a == 'C' && b == 'O' && c == 'N') ll = 3; /* CON  */
    if (a == 'P' && b == 'R' && c == 'N') ll = 3; /* PRN  */
    if (a == 'N' && b == 'U' && c == 'L') ll = 3; /* NUL  */
    if (a == 'L' && b == 'P' && c == 'T'  && (d >= '0' && d <= '9')) ll = 4;  /* LPT# */
    if (a == 'C' && b == 'O' && c == 'M'  && (d >= '0' && d <= '9')) ll = 4; /* COM# */
    /* AUX.anything, CON.anything etc.. are also illegal names */
    if (ll && (l == ll || (l > ll && n[ll] == '.')))
	return 1;

    /* spaces and periods at the end are a no-no */
    i = l - 1;
    if (n[i] == '.' || n[i] == ' ')
	return 1;

    /* these characters are *never* allowed */
    for (i = 0; i < NUM_EVILCHARS; i++) {
        if (strchr (n, evilchars[i]) != 0)
            return 1;
    }

    /* the reserved fsdb filename */
    if (strcmp (n, FSDB_FILE) == 0)
	return 1;
    return 0; /* the filename passed all checks, now it should be ok */
}

uae_u32 filesys_parse_mask(uae_u32 mask)
{
    return mask ^ 0xf;
}

int fsdb_exists (char *nname)
{
    if (GetFileAttributes(nname) == 0xFFFFFFFF)
	return 0;
    return 1;
}

/* For an a_inode we have newly created based on a filename we found on the
 * native fs, fill in information about this file/directory.  */
int fsdb_fill_file_attrs (a_inode *aino)
{
    int mode;

    if((mode = GetFileAttributes(aino->nname)) == 0xFFFFFFFF) {
	write_log("GetFileAttributes('%s') failed! error=%d, aino=%p dir=%d\n", aino->nname,GetLastError(),aino,aino->dir);
	return 0;
    }

    aino->dir = (mode & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    aino->amigaos_mode = A_FIBF_EXECUTE | A_FIBF_READ;
    if (!(FILE_ATTRIBUTE_ARCHIVE & mode))
	aino->amigaos_mode |= A_FIBF_ARCHIVE;
    if (! (FILE_ATTRIBUTE_READONLY & mode))
	aino->amigaos_mode |= A_FIBF_WRITE | A_FIBF_DELETE;
    if (FILE_ATTRIBUTE_SYSTEM & mode)
	aino->amigaos_mode |= A_FIBF_PURE;
    if (FILE_ATTRIBUTE_HIDDEN & mode)
	aino->amigaos_mode |= A_FIBF_HIDDEN;
    aino->amigaos_mode = filesys_parse_mask(aino->amigaos_mode);
    return 1;
}

int fsdb_set_file_attrs (a_inode *aino, int mask)
{
    struct stat statbuf;
    uae_u32 mode=0, tmpmask;

    tmpmask = filesys_parse_mask(mask);

    if (stat (aino->nname, &statbuf) == -1)
	return ERROR_OBJECT_NOT_AROUND;

    /* Unix dirs behave differently than AmigaOS ones.  */
    /* windows dirs go where no dir has gone before...  */
    if (! aino->dir) {
	if ((tmpmask & (A_FIBF_READ | A_FIBF_DELETE)) == 0)
	    mode |= FILE_ATTRIBUTE_READONLY;
	if (!(tmpmask & A_FIBF_ARCHIVE))
	    mode |= FILE_ATTRIBUTE_ARCHIVE;
	else
	    mode &= ~FILE_ATTRIBUTE_ARCHIVE;
	if (tmpmask & A_FIBF_PURE)
	    mode |= FILE_ATTRIBUTE_SYSTEM;
	else
	    mode &= ~FILE_ATTRIBUTE_SYSTEM;
	if (tmpmask & A_FIBF_HIDDEN)
	    mode |= FILE_ATTRIBUTE_HIDDEN;
	else
	    mode &= ~FILE_ATTRIBUTE_HIDDEN;
	SetFileAttributes(aino->nname, mode);
    }

    aino->amigaos_mode = mask;
    aino->dirty = 1;
    return 0;
}

/* Return nonzero if we can represent the amigaos_mode of AINO within the
 * native FS.  Return zero if that is not possible.  */
int fsdb_mode_representable_p (const a_inode *aino)
{
    int mask = aino->amigaos_mode;
    int m1;

    if (aino->dir)
	return aino->amigaos_mode == 0;

    /* S set, or E or R clear, means we can't handle it.  */
    if (mask & (A_FIBF_SCRIPT | A_FIBF_EXECUTE | A_FIBF_READ))
	return 0;
    m1 = A_FIBF_DELETE | A_FIBF_WRITE;
    /* If it's rwed, we are OK... */
    if ((mask & m1) == 0)
	return 1;
    /* We can also represent r-e-, by setting the host's readonly flag.  */
    if ((mask & m1) == m1)
	return 1;
    return 0;
}

char *fsdb_create_unique_nname (a_inode *base, const char *suggestion)
{
    char *c;
    char tmp[256] = "__uae___";
    int i;

    strncat (tmp, suggestion, 240);
	
    /* replace the evil ones... */
    for (i=0; i < NUM_EVILCHARS; i++)
        while ((c = strchr (tmp, evilchars[i])) != 0)
            *c = '_';

    while ((c = strchr (tmp, '.')) != 0)
        *c = '_';
    while ((c = strchr (tmp, ' ')) != 0)
        *c = '_';

    for (;;) {
	char *p = build_nname (base->nname, tmp);
	if (access (p, R_OK) < 0 && errno == ENOENT) {
	    write_log ("unique name: %s\n", p);
	    return p;
	}
	free (p);

	/* tmpnam isn't reentrant and I don't really want to hack configure
	 * right now to see whether tmpnam_r is available...  */
	for (i = 0; i < 8; i++) {
	    tmp[i+8] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[rand () % 63];
	}
    }
}
