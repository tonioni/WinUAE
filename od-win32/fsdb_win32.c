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
#include "config.h"
#include "options.h"
#include "memory.h"

#include "fsdb.h"
#include "win32.h"
#include <windows.h>

#define TRACING_ENABLED 0
#if TRACING_ENABLED
#define TRACE(x)	do { write_log x; } while(0)
#else
#define TRACE(x)
#endif

/* these are deadly (but I think allowed on the Amiga): */
#define NUM_EVILCHARS 7
static char evilchars[NUM_EVILCHARS] = { '\\', '*', '?', '\"', '<', '>', '|' };

#define UAEFSDB_BEGINS "__uae___"
#define UAEFSDB_BEGINSX "__uae___*"
#define UAEFSDB_LEN 604

/* The on-disk format is as follows:
 * Offset 0, 1 byte, valid
 * Offset 1, 4 bytes, mode
 * Offset 5, 257 bytes, aname
 * Offset 263, 257 bytes, nname
 * Offset 519, 81 bytes, comment
 * Offset 600, 4 bytes, Windows-side mode
 */

static char *make_uaefsdbpath (const char *dir, const char *name)
{
    int len;
    char *p;
    
    len = strlen (dir) + 1 + 1;
    if (name)
	len += 1 + strlen (name);
    len += 1 + strlen (FSDB_FILE);
    p = xmalloc (len);
    if (!p)
	return NULL;
    if (name)
	sprintf (p, "%s\\%s:%s", dir, name, FSDB_FILE);
    else
	sprintf (p, "%s:%s", dir, FSDB_FILE);
    return p;
}

static int read_uaefsdb (const char *dir, const char *name, uae_u8 *fsdb)
{
    char *p;
    HANDLE h;
    DWORD read;

    p = make_uaefsdbpath (dir, name);
    h = CreateFile (p, GENERIC_READ, 0,
	NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    xfree (p);
    if (h != INVALID_HANDLE_VALUE) {
	ReadFile (h, fsdb, UAEFSDB_LEN, &read, NULL);
	CloseHandle (h);
	if (read == UAEFSDB_LEN)
	    return 1;
    }
    memset (fsdb, 0, UAEFSDB_LEN);
    return 0;
}

static int write_uaefsdb (const char *dir, uae_u8 *fsdb)
{
    char *p;
    HANDLE h;
    DWORD written, attr = INVALID_FILE_ATTRIBUTES;
    FILETIME t1, t2, t3;
    int time_valid = FALSE;
    int ret = 0;
    
    p = make_uaefsdbpath (dir, NULL);
    h = CreateFile (dir, GENERIC_READ, 0,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {    
	if (GetFileTime (h, &t1, &t2, &t3))
	    time_valid = TRUE;
	CloseHandle (h);
    }
    h = CreateFile (p, GENERIC_WRITE, 0,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE && GetLastError () == ERROR_ACCESS_DENIED) {
	attr = GetFileAttributes (p);
	if (attr != INVALID_FILE_ATTRIBUTES) {
	    if (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
		SetFileAttributes (p, attr & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN));
		h = CreateFile (p, GENERIC_WRITE, 0,
		    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	    }
	}
    }
    if (h != INVALID_HANDLE_VALUE) {
	WriteFile (h, fsdb, UAEFSDB_LEN, &written, NULL);
	CloseHandle (h);
	if (written == UAEFSDB_LEN) {
	    ret = 1;
	    goto end;
	}
    }
    DeleteFile (p);
end:
    if (attr != INVALID_FILE_ATTRIBUTES)
	SetFileAttributes (p, attr);
    if (time_valid) {
	h = CreateFile (dir, GENERIC_WRITE, 0,
	    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {	
	    SetFileTime (h, &t1, &t2, &t3);
	    CloseHandle (h);
	}
    }
    xfree (p);
    return ret;
}

static void create_uaefsdb (a_inode *aino, uae_u8 *buf, int winmode)
{
    buf[0] = 1;
    do_put_mem_long ((uae_u32 *)(buf + 1), aino->amigaos_mode);
    strncpy (buf + 5, aino->aname, 256);
    buf[5 + 256] = '\0';
    strncpy (buf + 5 + 257, nname_begin (aino->nname), 256);
    buf[5 + 257 + 256] = '\0';
    strncpy (buf + 5 + 2 * 257, aino->comment ? aino->comment : "", 80);
    buf[5 + 2 * 257 + 80] = '\0';
    do_put_mem_long ((uae_u32 *)(buf + 5 + 2 * 257 + 81), winmode); 
    aino->has_dbentry = 0;
    aino->dirty = 0;
}

static a_inode *aino_from_buf (a_inode *base, uae_u8 *buf, int *winmode)
{
    uae_u32 mode;
    a_inode *aino = (a_inode *) xcalloc (sizeof (a_inode), 1);

    mode = do_get_mem_long ((uae_u32 *)(buf + 1));
    buf += 5;
    aino->aname = my_strdup (buf);
    buf += 257;
    aino->nname = build_nname (base->nname, buf);
    buf += 257;
    aino->comment = *buf != '\0' ? my_strdup (buf) : 0;
    buf += 81;
    aino->amigaos_mode = mode;
    *winmode = do_get_mem_long ((uae_u32 *)buf);
    *winmode &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;
    aino->has_dbentry = 0;
    aino->dirty = 0;
    aino->db_offset = 0;
    return aino;
}

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
    if (GetFileAttributes(nname) == INVALID_FILE_ATTRIBUTES)
	return 0;
    return 1;
}

/* For an a_inode we have newly created based on a filename we found on the
 * native fs, fill in information about this file/directory.  */
int fsdb_fill_file_attrs (a_inode *base, a_inode *aino)
{
    int mode, winmode, oldamode;
    uae_u8 fsdb[UAEFSDB_LEN];
    int reset = 0;

    if((mode = GetFileAttributes(aino->nname)) == INVALID_FILE_ATTRIBUTES) {
	write_log("GetFileAttributes('%s') failed! error=%d, aino=%p dir=%d\n",
	    aino->nname, GetLastError(), aino,aino->dir);
	return 0;
    }
    aino->dir = (mode & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    mode &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;

    if ((base->volflags & MYVOLUMEINFO_STREAMS) && read_uaefsdb (aino->nname, NULL, fsdb)) {
	aino->amigaos_mode = do_get_mem_long ((uae_u32 *)(fsdb + 1));
	xfree (aino->comment);
	if (fsdb[5 + 2 * 257])
	    aino->comment = my_strdup (fsdb + 5 + 2 * 257);
	xfree (aino_from_buf (base, fsdb, &winmode));
	if (winmode == mode) /* no Windows-side editing? */
	    return 1;
	write_log ("FS: '%s' protection flags edited from Windows-side\n", aino->nname);
	reset = 1;
	/* edited from Windows-side -> use Windows side flags instead */
    }

    oldamode = aino->amigaos_mode;
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
    aino->amigaos_mode |= oldamode & A_FIBF_SCRIPT;
    if (reset) {
	if (base->volflags & MYVOLUMEINFO_STREAMS) {
	    create_uaefsdb (aino, fsdb, mode);
	    write_uaefsdb (aino->nname, fsdb);
	}
    }
    return 1;
}

int fsdb_set_file_attrs (a_inode *aino)
{
    uae_u32 tmpmask;
    uae_u8 fsdb[UAEFSDB_LEN];
    uae_u32 mode;

    tmpmask = filesys_parse_mask (aino->amigaos_mode);

    mode = GetFileAttributes (aino->nname);
    if (mode == INVALID_FILE_ATTRIBUTES)
	return ERROR_OBJECT_NOT_AROUND;
    mode &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;

    /* Unix dirs behave differently than AmigaOS ones.  */
    /* windows dirs go where no dir has gone before...  */
    if (! aino->dir) {
	mode = 0;
	if ((tmpmask & (A_FIBF_READ | A_FIBF_DELETE)) == 0)
	    mode |= FILE_ATTRIBUTE_READONLY;
	if (!(tmpmask & A_FIBF_ARCHIVE))
	    mode |= FILE_ATTRIBUTE_ARCHIVE;
	if (tmpmask & A_FIBF_PURE)
	    mode |= FILE_ATTRIBUTE_SYSTEM;
	if (tmpmask & A_FIBF_HIDDEN)
	    mode |= FILE_ATTRIBUTE_HIDDEN;
	SetFileAttributes (aino->nname, mode);
    }

    aino->dirty = 1;
    if (aino->volflags & MYVOLUMEINFO_STREAMS) {
	create_uaefsdb (aino, fsdb, mode);
	write_uaefsdb (aino->nname, fsdb);
    }
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
    char tmp[256] = UAEFSDB_BEGINS;
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

char *fsdb_search_dir (const char *dirname, char *rel)
{
    WIN32_FIND_DATA fd;
    HANDLE h;
    char *tmp, *p = 0;

    tmp = build_nname (dirname, rel);
    h = FindFirstFile (tmp, &fd);
    if (h != INVALID_HANDLE_VALUE) {
	if (strcmp (fd.cFileName, rel) == 0)
	    p = rel;
	else
	    p = my_strdup (fd.cFileName);
	FindClose (h);
    }
    xfree (tmp);
    return p;
}

static a_inode *custom_fsdb_lookup_aino (a_inode *base, const char *aname, int offset, int dontcreate)
{
    uae_u8 fsdb[UAEFSDB_LEN];
    char *tmp1;
    HANDLE h;
    WIN32_FIND_DATA fd;
    static a_inode dummy;
    
    tmp1 = build_nname (base->nname, UAEFSDB_BEGINSX);
    if (!tmp1)
	return NULL;
    h = FindFirstFile (tmp1, &fd);
    if (h != INVALID_HANDLE_VALUE) {
	do {
	    if (read_uaefsdb (base->nname, fd.cFileName, fsdb)) {
		if (same_aname (fsdb + offset, aname)) {
		    int winmode;
		    FindClose (h);
		    xfree (tmp1);
		    if (dontcreate)
			return &dummy;
		    return aino_from_buf (base, fsdb, &winmode);
		}
	    }
	} while (FindNextFile (h, &fd));
	FindClose (h);
    }
    xfree (tmp1);
    return NULL;
}

a_inode *custom_fsdb_lookup_aino_aname (a_inode *base, const char *aname)
{
    return custom_fsdb_lookup_aino (base, aname, 5, 0);
}
a_inode *custom_fsdb_lookup_aino_nname (a_inode *base, const char *nname)
{
    return custom_fsdb_lookup_aino (base, nname, 5 + 257, 0);
}

int custom_fsdb_used_as_nname (a_inode *base, const char *nname)
{
    if (custom_fsdb_lookup_aino (base, nname, 5 + 257, 1))
	return 1;
    return 0;
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
    fos.fFlags = (currprefs.win32_norecyclebin ? 0 : FOF_ALLOWUNDO) |
	FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NORECURSION | FOF_SILENT;
    v = SHFileOperation (&fos);
    xfree (p);
    return v ? -1 : 0;
}

int my_rmdir (const char *name)
{
    void *od;
    int cnt;
    char tname[MAX_DPATH];

    /* SHFileOperation() ignores FOF_NORECURSION when deleting directories.. */
    od = my_opendir (name);
    if (!od) {
	SetLastError (ERROR_FILE_NOT_FOUND);
	return -1;
    }
    cnt = 0;
    while (my_readdir (od, tname)) {
	if (!strcmp (tname, ".") || !strcmp (tname, ".."))
	    continue;
	cnt++;
	break;
    }
    my_closedir (od);
    if (cnt > 0) {
	SetLastError (ERROR_CURRENT_DIRECTORY);
	return -1;
    }

    return recycle (name);
}

/* "move to Recycle Bin" (if enabled) -version of DeleteFile() */
int my_unlink (const char *name)
{
    return recycle (name);
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
    //write_log ("closehandle %x\n", mos->h);
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

int my_existsfile (const char *name)
{
    HANDLE h = CreateFile (name, GENERIC_READ, FILE_SHARE_READ,
	NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
	return 0;
    CloseHandle (h);
    return 1;
}

int my_existsdir (const char *name)
{
    DWORD attr = GetFileAttributes (name);
    if (attr == INVALID_FILE_ATTRIBUTES)
	return 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
	return 1;
    return 0;
}

void *my_open (const char *name, int flags)
{
    struct my_opens *mos;
    HANDLE h;
    DWORD DesiredAccess = GENERIC_READ;
    DWORD ShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD CreationDisposition = OPEN_EXISTING;
    DWORD FlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
    DWORD attr;
    
    mos = xmalloc (sizeof (struct my_opens));
    if (!mos)
	return NULL;
    attr = GetFileAttributes (name);
    if (flags & O_TRUNC)
	CreationDisposition = CREATE_ALWAYS;
    else if (flags & O_CREAT)
	CreationDisposition = OPEN_ALWAYS;
    if (flags & O_WRONLY)
	DesiredAccess = GENERIC_WRITE;
    if (flags & O_RDONLY) {
	DesiredAccess = GENERIC_READ;
	CreationDisposition = OPEN_EXISTING;
    }
    if (flags & O_RDWR)
	DesiredAccess = GENERIC_READ | GENERIC_WRITE;
    if (CreationDisposition == CREATE_ALWAYS && attr != INVALID_FILE_ATTRIBUTES &&
	(attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
	SetFileAttributes (name, FILE_ATTRIBUTE_NORMAL);
    h = CreateFile (name, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
    if (h == INVALID_HANDLE_VALUE) {
	if (GetLastError () == ERROR_ACCESS_DENIED && (DesiredAccess & GENERIC_WRITE)) {
	    DesiredAccess &= ~GENERIC_WRITE;
	    h = CreateFile (name, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
	}
	if (h == INVALID_HANDLE_VALUE) {
	    write_log ("failed to open '%s' %x %x\n", name, DesiredAccess, CreationDisposition);
	    xfree (mos);
	    mos = 0;
	    goto err;
	}
    }
    mos->h = h;
err:
    //write_log ("open '%s' = %x\n", name, mos ? mos->h : 0);
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

int dos_errno (void)
{
    DWORD e = GetLastError ();

    //write_log ("ec=%d\n", e);
    switch (e) {
     case ERROR_NOT_ENOUGH_MEMORY:
     case ERROR_OUTOFMEMORY:
	return ERROR_NO_FREE_STORE;

     case ERROR_FILE_EXISTS:
     case ERROR_ALREADY_EXISTS:
	return ERROR_OBJECT_EXISTS;

     case ERROR_WRITE_PROTECT:
     case ERROR_ACCESS_DENIED:
	return ERROR_WRITE_PROTECTED;

     case ERROR_FILE_NOT_FOUND:
     case ERROR_INVALID_DRIVE:
	return ERROR_OBJECT_NOT_AROUND;

     case ERROR_HANDLE_DISK_FULL:
	return ERROR_DISK_IS_FULL;

     case ERROR_SHARING_VIOLATION:
     case ERROR_BUSY:
	return ERROR_OBJECT_IN_USE;

     case ERROR_CURRENT_DIRECTORY:
	return ERROR_DIRECTORY_NOT_EMPTY;

     case ERROR_NEGATIVE_SEEK:
     case ERROR_SEEK_ON_DEVICE:
	return ERROR_SEEK_ERROR;

     default:
        write_log ("Unimplemented error %d\n", e);
        return ERROR_NOT_IMPLEMENTED;
    }
}

typedef BOOL (CALLBACK* GETVOLUMEPATHNAME)
  (LPCTSTR lpszFileName, LPTSTR lpszVolumePathName, DWORD cchBufferLength);

int my_getvolumeinfo (char *root)
{
    DWORD last, v, err;
    int ret = 0;
    GETVOLUMEPATHNAME pGetVolumePathName;
    char volume[MAX_DPATH];

    last = SetErrorMode (SEM_FAILCRITICALERRORS);
    v = GetFileAttributes (root);
    err = GetLastError ();
    SetErrorMode (last);
    if (v == INVALID_FILE_ATTRIBUTES)
	return -1;
    if (!(v & FILE_ATTRIBUTE_DIRECTORY))
	return -1;
/*
    if (v & FILE_ATTRIBUTE_READONLY)
	ret |= MYVOLUMEINFO_READONLY;
*/
    if (!os_winnt)
	return ret;
    pGetVolumePathName = (GETVOLUMEPATHNAME)GetProcAddress(
	GetModuleHandle("kernel32.dll"), "GetVolumePathNameA");
    if (pGetVolumePathName && pGetVolumePathName (root, volume, sizeof (volume))) {
	char fsname[MAX_DPATH];
	DWORD comlen;
	DWORD flags;
	if (GetVolumeInformation (volume, NULL, 0, NULL, &comlen, &flags, fsname, sizeof (fsname))) {
	    write_log ("Volume %s FS=%s maxlen=%d flags=%08.8X\n", volume, fsname, comlen, flags);
	    if (flags & FILE_NAMED_STREAMS)
		ret |= MYVOLUMEINFO_STREAMS;
	}
    }
    return ret;
}






