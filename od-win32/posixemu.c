/* 
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997 Mathias Ortmann
 */

#include "config.h"
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ddraw.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/utime.h>
#include <process.h>
#include "options.h"
#include "posixemu.h"
#include "threaddep/thread.h"
#include "filesys.h"

/* Our Win32 implementation of this function */
void gettimeofday( struct timeval *tv, void *blah )
{
    struct timeb time;
    ftime( &time );

    tv->tv_sec = time.time;
    tv->tv_usec = time.millitm * 1000;
}

/* convert time_t to/from AmigaDOS time */
#define secs_per_day ( 24 * 60 * 60 )
#define diff ( (8 * 365 + 2) * secs_per_day )

void get_time(time_t t, long* days, long* mins, long* ticks)
{
    /* time_t is secs since 1-1-1970 */
    /* days since 1-1-1978 */
    /* mins since midnight */
    /* ticks past minute @ 50Hz */

    t -= diff;
    *days = t / secs_per_day;
    t -= *days * secs_per_day;
    *mins = t / 60;
    t -= *mins * 60;
    *ticks = t * 50;
}

/* stdioemu, posixemu, mallocemu, and various file system helper routines */
static DWORD lasterror;

static int isillegal (unsigned char *str)
{
    int result = 0;
    unsigned char a = *str, b = str[1], c = str[2];

    if (a >= 'a' && a <= 'z')
	a &= ~' ';
    if (b >= 'a' && b <= 'z')
	b &= ~' ';
    if (c >= 'a' && c <= 'z')
	c &= ~' ';

    result = ( (a == 'A' && b == 'U' && c == 'X') ||
	        (a == 'C' && b == 'O' && c == 'N') ||
	        (a == 'P' && b == 'R' && c == 'N') ||
	        (a == 'N' && b == 'U' && c == 'L') );

    return result;
}

static int checkspace (char *str, char s, char d)
{
    char *ptr = str;

    while (*ptr && *ptr == s)
	ptr++;

    if (!*ptr || *ptr == '/' || *ptr == '\\') {
	while (str < ptr)
	    *(str++) = d;
	return 0;
    }
    return 1;
}

/* This is sick and incomplete... in the meantime, I have discovered six new illegal file name formats
 * M$ sucks! */
void fname_atow (const char *src, char *dst, int size)
{
    char *lastslash = dst, *strt = dst, *posn = NULL, *temp = NULL;
    int i, j;

    temp = xmalloc( size );

    while (size-- > 0) {
	if (!(*dst = *src++))
	    break;

	if (*dst == '~' || *dst == '|' || *dst == '*' || *dst == '?') {
	    if (size > 2) {
		sprintf (dst, "~%02x", *dst);
		size -= 2;
		dst += 2;
	    }
	} else if (*dst == '/') {
	    if (checkspace (lastslash, ' ', (char)0xa0) && (dst - lastslash == 3 || (dst - lastslash > 3 && lastslash[3] == '.')) && isillegal (lastslash)) {
		i = dst - lastslash - 3;
		dst++;
		for (j = i + 1; j--; dst--)
		    *dst = dst[-1];
		*(dst++) = (char)0xa0;
		dst += i;
		size--;
	    } else if (*lastslash == '.' && (dst - lastslash == 1 || (lastslash[1] == '.' && dst - lastslash == 2)) && size) {
		*(dst++) = (char)0xa0;
		size--;
	    }
	    *dst = '\\';
	    lastslash = dst + 1;
	}
	dst++;
    }

    if (checkspace (lastslash, ' ', (char)0xa0) && (dst - lastslash == 3 || (dst - lastslash > 3 && lastslash[3] == '.')) && isillegal (lastslash) && size > 1) {
	i = dst - lastslash - 3;
	dst++;
	for (j = i + 1; j--; dst--)
	    *dst = dst[-1];
	*(dst++) = (char)0xa0;
    } else if (!strcmp (lastslash, ".") || !strcmp (lastslash, ".."))
	strcat (lastslash, "\xa0");

    /* Major kludge, because I can't find the problem... */
    if( ( posn = strstr( strt, "..\xA0\\" ) ) == strt && temp)
    {
        strcpy( temp, "..\\" );
        strcat( temp, strt + 4 );
        strcpy( strt, temp );
    }

    /* Another major kludge, for the MUI installation... */
    if( *strt == ' ' ) /* first char as a space is illegal in Windoze */
    {
        sprintf( temp, "~%02x%s", ' ', strt+1 );
        strcpy( strt, temp );
    }

    free (temp);
}

static int hextol (char a)
{
    if (a >= '0' && a <= '9')
	return a - '0';
    if (a >= 'a' && a <= 'f')
	return a - 'a' + 10;
    if (a >= 'A' && a <= 'F')
	return a - 'A' + 10;
    return 2;
}

/* Win32 file name restrictions suck... */
void fname_wtoa (unsigned char *ptr)
{
    unsigned char *lastslash = ptr;

    while (*ptr) {
	if (*ptr == '~') {
	    *ptr = hextol (ptr[1]) * 16 + hextol (ptr[2]);
	    strcpy (ptr + 1, ptr + 3);
	} else if (*ptr == '\\') {
	    if (checkspace (lastslash, ' ', (char)0xa0) && ptr - lastslash > 3 && lastslash[3] == 0xa0 && isillegal (lastslash)) {
		ptr--;
		strcpy (lastslash + 3, lastslash + 4);
	    }
	    *ptr = '/';
	    lastslash = ptr + 1;
	}
	ptr++;
    }

    if (checkspace (lastslash, ' ', (char)0xa0) && ptr - lastslash > 3 && lastslash[3] == 0xa0 && isillegal (lastslash))
	strcpy (lastslash + 3, lastslash + 4);
}

#ifndef HAVE_TRUNCATE
int truncate (const char *name, long int len)
{
    HANDLE hFile;
    BOOL bResult = FALSE;
    int result = -1;

#if 0
    char buf[1024];

    fname_atow(name,buf,sizeof buf);

    if( ( hFile = CreateFile( buf, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ) ) != INVALID_HANDLE_VALUE )
    {
        if( SetFilePointer( hFile, len, NULL, FILE_BEGIN ) == (DWORD)len )
        {
            if( SetEndOfFile( hFile ) == TRUE )
                result = 0;
        }
        else
        {
            write_log( "SetFilePointer() failure for %s to posn %d\n", buf, len );
        }
        CloseHandle( hFile );
    }
    else
    {
        write_log( "CreateFile() failed to open %s\n", buf );
    }
#else
    if( ( hFile = CreateFile( name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ) ) != INVALID_HANDLE_VALUE )
    {
        if( SetFilePointer( hFile, len, NULL, FILE_BEGIN ) == (DWORD)len )
        {
            if( SetEndOfFile( hFile ) == TRUE )
                result = 0;
        }
        else
        {
            write_log( "SetFilePointer() failure for %s to posn %d\n", name, len );
        }
        CloseHandle( hFile );
    }
    else
    {
        write_log( "CreateFile() failed to open %s\n", name );
    }
#endif
    if( result == -1 )
        lasterror = GetLastError();
    return result;
}
#endif

DIR {
    WIN32_FIND_DATA finddata;
    HANDLE hDir;
    int getnext;
};

DIR *posixemu_opendir(const char *path)
{
    char buf[1024];
    DIR *dir;

    if (!(dir = (DIR *)GlobalAlloc(GPTR,sizeof(DIR))))
    {
	    lasterror = GetLastError();
	    return 0;
    }
#if 0
    fname_atow(path,buf,sizeof buf-4);
#else
    strcpy( buf, path );
#endif
    strcat(buf,"\\*");

    if ((dir->hDir = FindFirstFile(buf,&dir->finddata)) == INVALID_HANDLE_VALUE)
    {
	    lasterror = GetLastError();
	    GlobalFree(dir);
	    return 0;
    }

    return dir;
}

struct dirent *posixemu_readdir(DIR *dir)
{
    if (dir->getnext)
    {
	if (!FindNextFile(dir->hDir,&dir->finddata))
	{
	    lasterror = GetLastError();
	    return 0;
	}
    }
    dir->getnext = TRUE;

    fname_wtoa(dir->finddata.cFileName);
    return (struct dirent *)dir->finddata.cFileName;
}

void posixemu_closedir(DIR *dir)
{
    FindClose(dir->hDir);
    GlobalFree(dir);
}

static int w32fopendel(char *name, char *mode, int delflag)
{
	HANDLE hFile;

	if ((hFile = CreateFile(name,
		mode[1] == '+' ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,	// ouch :)
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		delflag ? FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE : FILE_ATTRIBUTE_NORMAL,
		NULL)) == INVALID_HANDLE_VALUE)
	{
		lasterror = GetLastError();
		hFile = 0;
	}

	return (int)hFile;                      /* return handle */
}
	
DWORD getattr(const char *name, LPFILETIME lpft, size_t *size)
{
	HANDLE hFind;
	WIN32_FIND_DATA fd;

	if ((hFind = FindFirstFile(name,&fd)) == INVALID_HANDLE_VALUE)
	{
		lasterror = GetLastError();

		fd.dwFileAttributes = GetFileAttributes(name);

		return fd.dwFileAttributes;
	}

	FindClose(hFind);

	if (lpft) *lpft = fd.ftLastWriteTime;
	if (size) *size = fd.nFileSizeLow;

	return fd.dwFileAttributes;
}

int isspecialdrive(const char *name)
{
    int v, err;
    DWORD last = SetErrorMode (SEM_FAILCRITICALERRORS);
    v = GetFileAttributes(name);
    err = GetLastError ();
    SetErrorMode (last);
    if (v != INVALID_FILE_ATTRIBUTES)
	return 0;
    if (err == ERROR_NOT_READY)
	return 1;
    if (err)
	return -1;
    return 0;
}

int posixemu_stat(const char *name, struct stat *statbuf)
{
    DWORD attr;
    FILETIME ft, lft;

    if ((attr = getattr(name,&ft,(size_t*)&statbuf->st_size)) == (DWORD)~0)
    {
	lasterror = GetLastError();
	return -1;
    }
    else
    {
	statbuf->st_mode = (attr & FILE_ATTRIBUTE_READONLY) ? FILEFLAG_READ : FILEFLAG_READ | FILEFLAG_WRITE;
	if (attr & FILE_ATTRIBUTE_ARCHIVE) statbuf->st_mode |= FILEFLAG_ARCHIVE;
	if (attr & FILE_ATTRIBUTE_DIRECTORY) statbuf->st_mode |= FILEFLAG_DIR;
	FileTimeToLocalFileTime(&ft,&lft);
	statbuf->st_mtime = (long)((*(__int64 *)&lft-((__int64)(369*365+89)*(__int64)(24*60*60)*(__int64)10000000))/(__int64)10000000);
    }
    return 0;
}

int posixemu_chmod(const char *name, int mode)
{
    DWORD attr = FILE_ATTRIBUTE_NORMAL;
    if (!(mode & FILEFLAG_WRITE)) attr |= FILE_ATTRIBUTE_READONLY;
    if (mode & FILEFLAG_ARCHIVE) attr |= FILE_ATTRIBUTE_ARCHIVE;
    if (SetFileAttributes(name,attr)) return 1;
    lasterror = GetLastError();
    return -1;
}

void tmToSystemTime( struct tm *tmtime, LPSYSTEMTIME systime )
{
    if( tmtime == NULL )
    {
        GetSystemTime( systime );
    }
    else
    {
        systime->wDay       = tmtime->tm_mday;
        systime->wDayOfWeek = tmtime->tm_wday;
        systime->wMonth     = tmtime->tm_mon + 1;
        systime->wYear      = tmtime->tm_year + 1900;
        systime->wHour      = tmtime->tm_hour;
        systime->wMinute    = tmtime->tm_min;
        systime->wSecond    = tmtime->tm_sec;
        systime->wMilliseconds = 0;
    }
}

static int setfiletime(const char *name, unsigned int days, int minute, int tick, int tolocal)
{
    FILETIME LocalFileTime, FileTime;
    HANDLE hFile;
    int success;
    if ((hFile = CreateFile(name, GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL)) == INVALID_HANDLE_VALUE)
    {
	lasterror = GetLastError();
	return 0;
    }

    *(__int64 *)&LocalFileTime = (((__int64)(377*365+91+days)*(__int64)1440+(__int64)minute)*(__int64)(60*50)+(__int64)tick)*(__int64)200000;
    
    if (tolocal) {
	if (!LocalFileTimeToFileTime(&LocalFileTime,&FileTime)) FileTime = LocalFileTime;
    } else {
	FileTime = LocalFileTime;
    }
    
    if (!(success = SetFileTime(hFile,&FileTime,&FileTime,&FileTime))) lasterror = GetLastError();
    CloseHandle(hFile);
    
    return success;
}

int posixemu_utime( const char *name, struct utimbuf *ttime )
{
    int result = -1, tolocal;
    long days, mins, ticks, actime;

    if (!ttime) {
	actime = time (NULL);
	tolocal = 0;
    } else {
	tolocal = 1;
	actime = ttime->actime;
    }
    get_time(actime, &days, &mins, &ticks);

    if( setfiletime( name, days, mins, ticks, tolocal ) )
        result = 0;

	return result;
}

#if 1
/* pthread Win32 emulation */
void sem_init (HANDLE * event, int manual_reset, int initial_state)
{
    if( *event )
    {
	if( initial_state )
	{
	    SetEvent( *event );
	}
	else
	{
	    ResetEvent( *event );
	}
    }
    else
    {
        *event = CreateEvent (NULL, manual_reset, initial_state, NULL);
    }
}

void sem_wait (HANDLE * event)
{
    WaitForSingleObject (*event, INFINITE);
}

void sem_post (HANDLE * event)
{
    SetEvent (*event);
}

int sem_trywait (HANDLE * event)
{
    return WaitForSingleObject (*event, 0) == WAIT_OBJECT_0 ? 0 : -1;
}

void sem_close (HANDLE * event)
{
    if( *event )
    {
	CloseHandle( *event );
	*event = NULL;
    }
}

typedef unsigned (__stdcall *BEGINTHREADEX_FUNCPTR)(void *);

int start_penguin (void *(*f)(void *), void *arg, DWORD * foo)
{
    HANDLE hThread;
    int result = 1;

    hThread = (HANDLE)_beginthreadex( NULL, 0, (BEGINTHREADEX_FUNCPTR)f, arg, 0, foo );
    if (hThread)
        SetThreadPriority (hThread, THREAD_PRIORITY_ABOVE_NORMAL);
    else
        result = 0;
    return result;
}

#endif

void set_thread_priority (int pri)
{
}

