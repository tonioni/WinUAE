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
	if (!fsdb_exists (p)) {
	    write_log ("unique name: %s\n", p);
	    return p;
	}
	xfree (p);
	/* tmpnam isn't reentrant and I don't really want to hack configure
	 * right now to see whether tmpnam_r is available...  */
	for (i = 0; i < 8; i++) {
	    tmp[i+8] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[rand () % 63];
	}
    }
}

int my_mkdir (const char *name)
{
    return CreateDirectory (name, NULL) == 0 ? -1 : 0;
}

static int recycle (const char *name)
{
    SHFILEOPSTRUCT fos;
    /* name must be terminated by \0\0 */
    char *p = xcalloc (strlen (name) + 2, 1);
    int v;
    
    strcpy (p, name);
    memset (&fos, 0, sizeof (fos));
    fos.wFunc = FO_DELETE;
    fos.pFrom = p;
    fos.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NORECURSION | FOF_SILENT;
    v = SHFileOperation (&fos);
    xfree (p);
    return v ? -1 : 0;
}

int my_rmdir (const char *name)
{
    return recycle (name);
    //return RemoveDirectory (name) == 0 ? -1 : 0;
}

/* "move to Recycle Bin" (if enabled) -version of DeleteFile() */
int my_unlink (const char *name)
{
    return recycle (name);
    //return DeleteFile (name) == 0 ? -1 : 0;
}

int my_rename (const char *oldname, const char *newname)
{
    return MoveFile (oldname, newname) == 0 ? -1 : 0;
}

struct my_opendirs {
    HANDLE *h;
    WIN32_FIND_DATA fd;
    int first;
};

void *my_opendir (const char *name)
{
    struct my_opendirs *mod;
    char tmp[MAX_DPATH];
    
    strcpy (tmp, name);
    strcat (tmp, "\\*.*");
    mod = xmalloc (sizeof (struct my_opendirs));
    if (!mod)
	return NULL;
    mod->h = FindFirstFile(tmp, &mod->fd);
    if (mod->h == INVALID_HANDLE_VALUE) {
	xfree (mod);
	return NULL;
    }
    mod->first = 1;
    return mod;
}

void my_closedir (void *d)
{
    struct my_opendirs *mod = d;
    FindClose (mod->h);
    xfree (mod);
}

int my_readdir (void *d, char *name)
{
    struct my_opendirs *mod = d;
    if (mod->first) {
	strcpy (name, mod->fd.cFileName);
	mod->first = 0;
	return 1;
    }
    if (!FindNextFile (mod->h, &mod->fd))
	return 0;
    strcpy (name, mod->fd.cFileName);
    return 1;
}

struct my_opens {
    HANDLE *h;
};

void my_close (void *d)
{
    struct my_opens *mos = d;
    CloseHandle (mos->h);
    xfree (mos);
}

unsigned int my_lseek (void *d, unsigned int offset, int whence)
{
    struct my_opens *mos = d;
    return SetFilePointer (mos->h, offset, NULL,
	whence == SEEK_SET ? FILE_BEGIN : (whence == SEEK_END ? FILE_END : FILE_CURRENT));
}

unsigned int my_read (void *d, void *b, unsigned int size)
{
    struct my_opens *mos = d;
    DWORD read = 0;
    ReadFile (mos->h, b, size, &read, NULL);
    return read;
}

unsigned int my_write (void *d, void *b, unsigned int size)
{
    struct my_opens *mos = d;
    DWORD written = 0;
    WriteFile (mos->h, b, size, &written, NULL);
    return written;
}

void *my_open (const char *name, int flags)
{
    struct my_opens *mos;
    HANDLE h;
    DWORD DesiredAccess = GENERIC_READ | GENERIC_WRITE;
    DWORD ShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD CreationDisposition = OPEN_EXISTING;
    DWORD FlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
    
    mos = xmalloc (sizeof (struct my_opens));
    if (!mos)
	return NULL;
    if (flags & O_TRUNC)
	CreationDisposition = TRUNCATE_EXISTING;
    if (flags & O_CREAT)
	CreationDisposition = CREATE_ALWAYS;
    if (flags & O_WRONLY)
	DesiredAccess = GENERIC_WRITE;
    if (flags & O_RDONLY)
	DesiredAccess = GENERIC_READ;
    if (flags & O_RDWR)
	DesiredAccess = GENERIC_READ | GENERIC_WRITE;
    h = CreateFile (name, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
    if (h == INVALID_HANDLE_VALUE) {
	xfree (mos);
	return 0;
    }
    mos->h = h;
    return mos;
}

int my_truncate (const char *name, long int len)
{
    HANDLE hFile;
    BOOL bResult = FALSE;
    int result = -1;

    if ((hFile = CreateFile (name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
	OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ) ) != INVALID_HANDLE_VALUE )
    {
        if (SetFilePointer (hFile, len, NULL, FILE_BEGIN) == (DWORD)len) {
            if (SetEndOfFile (hFile) == TRUE)
                result = 0;
        } else {
            write_log ("truncate: SetFilePointer() failure for %s to posn %d\n", name, len);
        }
        CloseHandle( hFile );
    } else {
        write_log ("truncate: CreateFile() failed to open %s\n", name);
    }
    return result;
}
