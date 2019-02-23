#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"

#include "fsdb.h"
#include "zfile.h"
#include "win32.h"

#include <windows.h>
#include <sys/timeb.h>
#include <Shobjidl.h>
#include <ShlGuid.h>
#include <Shlwapi.h>

bool my_isfilehidden (const TCHAR *path)
{
	DWORD attr = GetFileAttributes (path);
	if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN))
		return true;
	return false;
}
void my_setfilehidden (const TCHAR *path, bool hidden)
{
	DWORD attr = GetFileAttributes (path);
	if (attr == INVALID_FILE_ATTRIBUTES)
		return;
	DWORD attro = attr;
	attr &= ~FILE_ATTRIBUTE_HIDDEN;
	if (hidden)
		attr |= FILE_ATTRIBUTE_HIDDEN;
	if (attro == attr)
		return;
	SetFileAttributes (path, attr);
}

int my_setcurrentdir (const TCHAR *curdir, TCHAR *oldcur)
{
	int ret = 0;
	if (oldcur)
		ret = GetCurrentDirectory (MAX_DPATH, oldcur);
	if (curdir) {
		const TCHAR *namep;
		TCHAR path[MAX_DPATH];
	
		if (currprefs.win32_filesystem_mangle_reserved_names == false) {
			_tcscpy (path, PATHPREFIX);
			_tcscat (path, curdir);
			namep = path;
		} else {
			namep = curdir;
		}
		ret = SetCurrentDirectory (namep);
	}
	return ret;
}

int my_mkdir (const TCHAR *name)
{
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];
	
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}
	return CreateDirectory (namep, NULL) == 0 ? -1 : 0;
}

static int recycle (const TCHAR *name)
{
	DWORD dirattr = GetFileAttributesSafe (name);
	bool isdir = dirattr != INVALID_FILE_ATTRIBUTES && (dirattr & FILE_ATTRIBUTE_DIRECTORY);
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];
	
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}

	if (currprefs.win32_norecyclebin || isdir || currprefs.win32_filesystem_mangle_reserved_names == false) {
		if (isdir)
			return RemoveDirectory (namep) ? 0 : -1;
		else
			return DeleteFile (namep) ? 0 : -1;
	} else {
		SHFILEOPSTRUCT fos;
		HANDLE h;
		
		h = CreateFile (namep, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			LARGE_INTEGER size;
			if (GetFileSizeEx (h, &size)) {
				if (size.QuadPart == 0) {
					CloseHandle (h);
					return DeleteFile (namep) ? 0 : -1;
				}
			}
			CloseHandle (h);
		}

		/* name must be terminated by \0\0 */
		TCHAR *p = xcalloc (TCHAR, _tcslen (namep) + 2);
		int v;

		_tcscpy (p, namep);
		memset (&fos, 0, sizeof (fos));
		fos.wFunc = FO_DELETE;
		fos.pFrom = p;
		fos.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NORECURSION | FOF_SILENT;
		v = SHFileOperation (&fos);
		xfree (p);
		switch (v)
		{
		case 0xb7: //DE_ERROR_MAX
		case 0x7c: //DE_INVALIDFILES
		case 0x402: // "unknown error"
			v = ERROR_FILE_NOT_FOUND;
			break;
		case 0x75: //DE_OPCANCELLED:
		case 0x10000: //ERRORONDEST:
		case 0x78: //DE_ACCESSDENIEDSRC:
		case 0x74: //DE_ROOTDIR:
			v = ERROR_ACCESS_DENIED;
			break;
		}
		SetLastError (v);
		return v ? -1 : 0;
	}
}

int my_rmdir (const TCHAR *name)
{
	struct my_opendir_s *od;
	int cnt;
	TCHAR tname[MAX_DPATH];

	/* SHFileOperation() ignores FOF_NORECURSION when deleting directories.. */
	od = my_opendir (name);
	if (!od) {
		SetLastError (ERROR_FILE_NOT_FOUND);
		return -1;
	}
	cnt = 0;
	while (my_readdir (od, tname)) {
		if (!_tcscmp (tname, _T(".")) || !_tcscmp (tname, _T("..")))
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
int my_unlink (const TCHAR *name)
{
	return recycle (name);
}

int my_rename (const TCHAR *oldname, const TCHAR *newname)
{
	const TCHAR *onamep, *nnamep;
	TCHAR opath[MAX_DPATH], npath[MAX_DPATH];

	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (opath, PATHPREFIX);
		_tcscat (opath, oldname);
		onamep = opath;
		_tcscpy (npath, PATHPREFIX);
		_tcscat (npath, newname);
		nnamep = npath;
	} else {
		onamep = oldname;
		nnamep = newname;
	}
	return MoveFile (onamep, nnamep) == 0 ? -1 : 0;
}

struct my_opendir_s {
	HANDLE h;
	WIN32_FIND_DATA fd;
	int first;
};

struct my_opendir_s *my_opendir (const TCHAR *name)
{
	return my_opendir (name, _T("*.*"));
}
struct my_opendir_s *my_opendir (const TCHAR *name, const TCHAR *mask)
{
	struct my_opendir_s *mod;
	TCHAR tmp[MAX_DPATH];

	tmp[0] = 0;
	if (currprefs.win32_filesystem_mangle_reserved_names == false)
		_tcscpy (tmp, PATHPREFIX);
	_tcscat (tmp, name);
	_tcscat (tmp, _T("\\"));
	_tcscat (tmp, mask);
	mod = xmalloc (struct my_opendir_s, 1);
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

void my_closedir (struct my_opendir_s *mod)
{
	if (mod)
		FindClose (mod->h);
	xfree (mod);
}

int my_readdir (struct my_opendir_s *mod, TCHAR *name)
{
	if (mod->first) {
		_tcscpy (name, mod->fd.cFileName);
		mod->first = 0;
		return 1;
	}
	if (!FindNextFile (mod->h, &mod->fd))
		return 0;
	_tcscpy (name, mod->fd.cFileName);
	return 1;
}

struct my_openfile_s {
	HANDLE h;
};

void my_close (struct my_openfile_s *mos)
{
	CloseHandle (mos->h);
	xfree (mos);
}

uae_s64 int my_lseek (struct my_openfile_s *mos, uae_s64 int offset, int whence)
{
	LARGE_INTEGER li, old;

	old.QuadPart = 0;
	old.LowPart = SetFilePointer (mos->h, 0, &old.HighPart, FILE_CURRENT);
	if (old.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
		return -1;
	if (offset == 0 && whence == SEEK_CUR)
		return old.QuadPart;
	li.QuadPart = offset;
	li.LowPart = SetFilePointer (mos->h, li.LowPart, &li.HighPart,
		whence == SEEK_SET ? FILE_BEGIN : (whence == SEEK_END ? FILE_END : FILE_CURRENT));
	if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
		return -1;
	return old.QuadPart;
}
uae_s64 int my_fsize (struct my_openfile_s *mos)
{
	LARGE_INTEGER li;
	if (!GetFileSizeEx (mos->h, &li))
		return -1;
	return li.QuadPart;
}


unsigned int my_read (struct my_openfile_s *mos, void *b, unsigned int size)
{
	DWORD read = 0;
	ReadFile (mos->h, b, size, &read, NULL);
	return read;
}

unsigned int my_write (struct my_openfile_s *mos, void *b, unsigned int size)
{
	DWORD written = 0;
	WriteFile (mos->h, b, size, &written, NULL);
	return written;
}

BOOL SetFileAttributesSafe (const TCHAR *name, DWORD attr)
{
	DWORD last;
	BOOL ret;
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];

	last = SetErrorMode (SEM_FAILCRITICALERRORS);
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}
	ret = SetFileAttributes (namep, attr);
	SetErrorMode (last);
	return ret;
}

DWORD GetFileAttributesSafe (const TCHAR *name)
{
	DWORD attr, last;
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];

	last = SetErrorMode (SEM_FAILCRITICALERRORS);
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}
	attr = GetFileAttributes (namep);
	SetErrorMode (last);
	return attr;
}

int my_existsfile (const TCHAR *name)
{
	DWORD attr;
	
	attr = GetFileAttributesSafe (name);
	if (attr == INVALID_FILE_ATTRIBUTES)
		return 0;
	if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
		return 1;
	return 0;
}

int my_existsdir (const TCHAR *name)
{
	DWORD attr;

	attr = GetFileAttributesSafe (name);
	if (attr == INVALID_FILE_ATTRIBUTES)
		return 0;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
		return 1;
	return 0;
}

struct my_openfile_s *my_open (const TCHAR *name, int flags)
{
	struct my_openfile_s *mos;
	HANDLE h;
	DWORD DesiredAccess = GENERIC_READ;
	DWORD ShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	DWORD CreationDisposition = OPEN_EXISTING;
	DWORD FlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
	DWORD attr;
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];
	
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}
	mos = xmalloc (struct my_openfile_s, 1);
	if (!mos)
		return NULL;
	attr = GetFileAttributesSafe (name);
	if (flags & O_TRUNC)
		CreationDisposition = CREATE_ALWAYS;
	else if (flags & O_CREAT)
		CreationDisposition = OPEN_ALWAYS;
	if (flags & O_WRONLY)
		DesiredAccess = GENERIC_WRITE;
	if (flags == O_RDONLY) {
		DesiredAccess = GENERIC_READ;
		CreationDisposition = OPEN_EXISTING;
	}
	if (flags & O_RDWR)
		DesiredAccess = GENERIC_READ | GENERIC_WRITE;
	if (CreationDisposition == CREATE_ALWAYS && attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
		SetFileAttributesSafe (name, FILE_ATTRIBUTE_NORMAL);
	h = CreateFile (namep, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_ACCESS_DENIED && (DesiredAccess & GENERIC_WRITE)) {
			DesiredAccess &= ~GENERIC_WRITE;
			h = CreateFile (namep, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
			if (h == INVALID_HANDLE_VALUE)
				err = GetLastError();
		}
		if (h == INVALID_HANDLE_VALUE) {
			write_log (_T("failed to open '%s' %x %x err=%d\n"), namep, DesiredAccess, CreationDisposition, err);
			xfree (mos);
			mos = NULL;
			goto err;
		}
	}
	mos->h = h;
err:
	//write_log (_T("open '%s' = %x\n"), namep, mos ? mos->h : 0);
	return mos;
}

int my_truncate (const TCHAR *name, uae_u64 len)
{
	HANDLE hFile;
	BOOL bResult = FALSE;
	int result = -1;
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];
	
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}
	if ((hFile = CreateFile (namep, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ) ) != INVALID_HANDLE_VALUE )
	{
		LARGE_INTEGER li;
		li.QuadPart = len;
		li.LowPart = SetFilePointer (hFile, li.LowPart, &li.HighPart, FILE_BEGIN);
		if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR) {
			write_log (_T("truncate: SetFilePointer() failure for %s to posn %d\n"), namep, len);
		} else {
			if (SetEndOfFile (hFile) == TRUE)
				result = 0;
		}
		CloseHandle (hFile);
	} else {
		write_log (_T("truncate: CreateFile() failed to open %s\n"), namep);
	}
	return result;
}

int dos_errno (void)
{
	DWORD e = GetLastError ();

	//write_log (_T("ec=%d\n"), e);
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
	case ERROR_INVALID_NAME:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_NOT_READY:
	case ERROR_BAD_UNIT:
	case ERROR_REQUEST_ABORTED:
	case ERROR_INVALID_HANDLE:
	case ERROR_DEV_NOT_EXIST:
	case ERROR_INVALID_PARAMETER:
	case ERROR_NETNAME_DELETED:
	case ERROR_NETWORK_ACCESS_DENIED:
	case ERROR_BAD_NET_NAME:
	case ERROR_BAD_NETPATH:
	case ERROR_NETWORK_BUSY:
	case ERROR_SEM_TIMEOUT:
		return ERROR_OBJECT_NOT_AROUND;

	case ERROR_HANDLE_DISK_FULL:
	case ERROR_DISK_FULL:
		return ERROR_DISK_IS_FULL;

	case ERROR_SHARING_VIOLATION:
	case ERROR_BUSY:
	case ERROR_USER_MAPPED_FILE:
		return ERROR_OBJECT_IN_USE;

	case ERROR_CURRENT_DIRECTORY:
		return ERROR_DIRECTORY_NOT_EMPTY;

	case ERROR_NEGATIVE_SEEK:
	case ERROR_SEEK_ON_DEVICE:
		return ERROR_SEEK_ERROR;

	default:
		write_log (_T("Unimplemented error %d\n"), e);
		/* most common */
		return ERROR_OBJECT_NOT_AROUND;
	}
}

int my_getvolumeinfo (const TCHAR *root)
{
	DWORD v, err;
	int ret = 0;
	TCHAR volume[MAX_DPATH];

	v = GetFileAttributesSafe (root);
	err = GetLastError ();
	if (v == INVALID_FILE_ATTRIBUTES)
		return -1;
	if (!(v & FILE_ATTRIBUTE_DIRECTORY))
		return -2;
	/*
	if (v & FILE_ATTRIBUTE_READONLY)
	ret |= MYVOLUMEINFO_READONLY;
	*/
	if (GetVolumePathName (root, volume, sizeof (volume) / sizeof(TCHAR))) {
		TCHAR fsname[MAX_DPATH];
		DWORD comlen;
		DWORD flags;
		if (GetVolumeInformation (volume, NULL, 0, NULL, &comlen, &flags, fsname, sizeof fsname / sizeof (TCHAR))) {
			//write_log (_T("Volume %s FS=%s maxlen=%d flags=%08X\n"), volume, fsname, comlen, flags);
			if (flags & FILE_NAMED_STREAMS)
				ret |= MYVOLUMEINFO_STREAMS;
		}
	}
	return ret;
}

FILE *my_opentext (const TCHAR *name)
{
	FILE *f;
	uae_u8 tmp[4];
	int v;

	f = _tfopen (name, _T("rb"));
	if (!f)
		return NULL;
	v = fread (tmp, 1, sizeof tmp, f);
	fclose (f);
	if (v == 4) {
		if (tmp[0] == 0xef && tmp[1] == 0xbb && tmp[2] == 0xbf)
			return _tfopen (name, _T("r, ccs=UTF-8"));
		if (tmp[0] == 0xff && tmp[1] == 0xfe)
			return _tfopen (name, _T("r, ccs=UTF-16LE"));
	}
	return _tfopen (name, _T("r"));
}

typedef BOOL (CALLBACK* GETVOLUMEINFORMATIONBYHANDLEW)
    (_In_ HANDLE hFile,
    LPWSTR lpVolumeNameBuffer,
    DWORD nVolumeNameSize,
    LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength,
    LPDWORD lpFileSystemFlags,
    LPWSTR lpFileSystemNameBuffer,
    DWORD nFileSystemNameSize);


static bool isfat (HANDLE h)
{
	TCHAR fsname[MAX_DPATH];
	DWORD comlen;
	DWORD flags;
	GETVOLUMEINFORMATIONBYHANDLEW pGetVolumeInformationByHandleW;
	
	pGetVolumeInformationByHandleW = (GETVOLUMEINFORMATIONBYHANDLEW)GetProcAddress(
		GetModuleHandle (_T("kernel32.dll")), "GetVolumeInformationByHandleW");
	if (!pGetVolumeInformationByHandleW)
		return false;
	if (pGetVolumeInformationByHandleW (h, NULL, NULL, NULL, &comlen, &flags, fsname, sizeof fsname / sizeof (TCHAR))) {
		if (!_tcsncmp (fsname, _T("FAT"), 3)) {
			return true;
		}
	}
	return false;
}

bool my_stat (const TCHAR *name, struct mystat *statbuf)
{
	DWORD attr, ok;
	FILETIME ft, lft;
	HANDLE h;
	BY_HANDLE_FILE_INFORMATION fi;
	const TCHAR *namep;
	bool fat;
	TCHAR path[MAX_DPATH];

	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}

	// FILE_FLAG_BACKUP_SEMANTICS = can also "open" directories
	h = CreateFile (namep, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	fat = isfat (h);

	ok = GetFileInformationByHandle (h, &fi);
	CloseHandle (h);

	attr = 0;
	ft.dwHighDateTime = ft.dwLowDateTime = 0;
	if (ok) {
		attr = fi.dwFileAttributes;
		if (fat) {
			// fat lastwritetime only has 2 second resolution
			// fat creationtime has 10ms resolution
			// use creationtime if creationtime is inside lastwritetime 2s resolution
			ULARGE_INTEGER ct, wt;
			ct.HighPart = fi.ftCreationTime.dwHighDateTime;
			ct.LowPart = fi.ftCreationTime.dwLowDateTime;
			wt.HighPart = fi.ftLastWriteTime.dwHighDateTime;
			wt.LowPart = fi.ftLastWriteTime.dwLowDateTime;
			uae_u64 ctsec = ct.QuadPart / 10000000;
			uae_u64 wtsec = wt.QuadPart / 10000000;
			if (wtsec == ctsec || wtsec + 1 == ctsec) {
				ft = fi.ftCreationTime;
			} else {
				ft = fi.ftLastWriteTime;
			}
		} else {
			ft = fi.ftLastWriteTime;
		}
		statbuf->size = ((uae_u64)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
	} else {
		write_log (_T("GetFileInformationByHandle(%s) failed: %d\n"), namep, GetLastError ());
		return false;
	}

	statbuf->mode = (attr & FILE_ATTRIBUTE_READONLY) ? FILEFLAG_READ : FILEFLAG_READ | FILEFLAG_WRITE;
	if (attr & FILE_ATTRIBUTE_ARCHIVE)
		statbuf->mode |= FILEFLAG_ARCHIVE;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
		statbuf->mode |= FILEFLAG_DIR;

	FileTimeToLocalFileTime (&ft,&lft);
	uae_u64 t = (*(__int64 *)&lft-((__int64)(369*365+89)*(__int64)(24*60*60)*(__int64)10000000));
	statbuf->mtime.tv_sec = t / 10000000;
	statbuf->mtime.tv_usec = (t / 10) % 1000000;

	return true;
}


bool my_chmod (const TCHAR *name, uae_u32 mode)
{
	DWORD attr = FILE_ATTRIBUTE_NORMAL;
	if (!(mode & FILEFLAG_WRITE))
		attr |= FILE_ATTRIBUTE_READONLY;
	if (mode & FILEFLAG_ARCHIVE)
		attr |= FILE_ATTRIBUTE_ARCHIVE;
	if (SetFileAttributesSafe (name, attr))
		return true;
	return false;
}

static void tmToSystemTime (struct tm *tmtime, LPSYSTEMTIME systime)
{
	if (tmtime == NULL) {
		GetSystemTime (systime);
	} else {
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

static int setfiletime (const TCHAR *name, int days, int minute, int tick, int tolocal)
{
	FILETIME LocalFileTime, FileTime;
	HANDLE hFile;
	const TCHAR *namep;
	TCHAR path[MAX_DPATH];
	
	if (currprefs.win32_filesystem_mangle_reserved_names == false) {
		_tcscpy (path, PATHPREFIX);
		_tcscat (path, name);
		namep = path;
	} else {
		namep = name;
	}

	if ((hFile = CreateFile(namep, FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)) == INVALID_HANDLE_VALUE)
		return 0;

	for (;;) {
		ULARGE_INTEGER lft;

		lft.QuadPart = (((uae_u64)(377*365+91+days)*(uae_u64)1440+(uae_u64)minute)*(uae_u64)(60*50)+(uae_u64)tick)*(uae_u64)200000;
		LocalFileTime.dwHighDateTime = lft.HighPart;
		LocalFileTime.dwLowDateTime = lft.LowPart;
		if (tolocal) {
			if (!LocalFileTimeToFileTime (&LocalFileTime, &FileTime))
				FileTime = LocalFileTime;
		} else {
			FileTime = LocalFileTime;
		}
		if (!SetFileTime (hFile, &FileTime, &FileTime, &FileTime)) {
			if (days > 47846) { // > 2108-12-31 (fat limit)
				days = 47846;
				continue;
			}
			if (days < 730) { // < 1980-01-01 (fat limit)
				days = 730;
				continue;
			}
		}
		break;
	}

	CloseHandle (hFile);

	return 1;
}

bool my_utime (const TCHAR *name, struct mytimeval *tv)
{
	int result = -1, tolocal;
	int days, mins, ticks;
	struct mytimeval tv2;

	if (!tv) {
		struct timeb time;
		ftime (&time);
		tv2.tv_sec = time.time;
		tv2.tv_usec = time.millitm * 1000;
		tolocal = 0;
	} else {
		tv2.tv_sec = tv->tv_sec;
		tv2.tv_usec = tv->tv_usec;
		tolocal = 1;
	}
	timeval_to_amiga (&tv2, &days, &mins, &ticks, 50);
	if (setfiletime (name, days, mins, ticks, tolocal))
		return true;

	return false;
}

bool my_createsoftlink(const TCHAR *path, const TCHAR *target)
{
	return CreateSymbolicLink(path, target, my_existsdir (target) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0) != 0;
}

bool my_issamepath(const TCHAR *path1, const TCHAR *path2)
{
	TCHAR path1o[MAX_DPATH], path2o[MAX_DPATH];
	my_canonicalize_path(path1, path1o, sizeof path1o / sizeof(TCHAR));
	my_canonicalize_path(path2, path2o, sizeof path2o / sizeof(TCHAR));
	if (!_tcsicmp(path1o, path2o))
		return true;
	return false;
}

void my_canonicalize_path(const TCHAR *path, TCHAR *out, int size)
{
	TCHAR tmp[MAX_DPATH];
	int v;

	// don't attempt to validate and canonicalize invalid or fake paths
	if (path[0] == ':' || path[0] == 0 || _tcscmp(path, _T("\\")) == 0 || _tcscmp(path, _T("/")) == 0) {
		_tcsncpy(out, path, size);
		out[size - 1] = 0;
		return;
	}
	// skip network paths, prevent common few seconds delay.
	if (path[0] == '\\' && path[1] == '\\') {
		_tcsncpy(out, path, size);
		out[size - 1] = 0;
		return;
	}
	v = GetLongPathName (path, tmp, sizeof tmp / sizeof (TCHAR));
	if (v > sizeof tmp / sizeof (TCHAR)) {
		_tcsncpy (out, path, size);
		out[size - 1] = 0;
		return;
	}
	if (!v)
		_tcscpy(tmp, path);
	GetFullPathName(tmp, size, out, NULL);
}

int my_issamevolume(const TCHAR *path1, const TCHAR *path2, TCHAR *path)
{
	TCHAR p1[MAX_DPATH];
	TCHAR p2[MAX_DPATH];
	int len, cnt;

	my_canonicalize_path(path1, p1, sizeof p1 / sizeof (TCHAR));
	my_canonicalize_path(path2, p2, sizeof p2 / sizeof (TCHAR));
	len = _tcslen (p1);
	if (len > _tcslen (p2))
		len = _tcslen (p2);
	if (_tcsnicmp (p1, p2, len))
		return 0;
	_tcscpy (path, p2 + len);
	cnt = 0;
	for (int i = 0; i < _tcslen (path); i++) {
		if (path[i] == '\\' || path[i] == '/') {
			path[i] = '/';
			cnt++;
		}
	}
	write_log (_T("'%s' (%s) matched with '%s' (%s), extra = '%s'\n"), path1, p1, path2, p2, path);
	return cnt;
}

typedef struct _REPARSE_DATA_BUFFER {
  ULONG  ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG  Flags;
      WCHAR  PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR  PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

static int my_resolvessymboliclink2(TCHAR *linkfile, int size)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
	int ret = -1;
	DWORD returnedDataSize;
	uae_u8 tmp[MAX_DPATH * 2];
	TCHAR tmp2[MAX_DPATH];

	h = FindFirstFile (linkfile, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return -1;
	FindClose(h);
	if (fd.dwReserved0 != IO_REPARSE_TAG_SYMLINK || !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		return -1;
	h = CreateFile (linkfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_REPARSE_POINT | FILE_FLAG_OPEN_REPARSE_POINT , NULL);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	if (DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, tmp, sizeof tmp, &returnedDataSize, NULL)) {
		REPARSE_DATA_BUFFER *rdb = (REPARSE_DATA_BUFFER*)tmp;
		if (size > 0) {
			if (rdb->SymbolicLinkReparseBuffer.Flags & 1) { // SYMLINK_FLAG_RELATIVE
				_tcscpy (tmp2, linkfile);
				PathRemoveFileSpec (tmp2);
				_tcscat (tmp2, _T("\\"));
				TCHAR *p = tmp2 + _tcslen (tmp2);
				memcpy (p,
					(uae_u8*)rdb->SymbolicLinkReparseBuffer.PathBuffer + rdb->SymbolicLinkReparseBuffer.SubstituteNameOffset,
					rdb->SymbolicLinkReparseBuffer.SubstituteNameLength);
				p[rdb->SymbolicLinkReparseBuffer.SubstituteNameLength / 2] = 0;
			} else {
				memcpy (tmp2,
					(uae_u8*)rdb->SymbolicLinkReparseBuffer.PathBuffer + rdb->SymbolicLinkReparseBuffer.SubstituteNameOffset,
					rdb->SymbolicLinkReparseBuffer.SubstituteNameLength);
				tmp2[rdb->SymbolicLinkReparseBuffer.SubstituteNameLength / 2] = 0;
			}
			if (!_tcsnicmp (tmp2, _T("\\??\\"), 4)) {
				memmove (tmp2, tmp2 + 4, (_tcslen (tmp2 + 4) + 1) * sizeof (TCHAR));
			}
			my_canonicalize_path (tmp2, linkfile, size);
		}
		ret = 1;
	}
	CloseHandle(h);
	return ret;
}
bool my_resolvessymboliclink(TCHAR *linkfile, int size)
{
	return my_resolvessymboliclink2(linkfile, size) > 0;
}

// http://msdn.microsoft.com/en-us/library/aa969393.aspx
bool my_resolveshortcut(TCHAR *linkfile, int size) 
{ 
	bool ok = false;
    HRESULT hres; 
    IShellLink* psl; 
    WCHAR szGotPath[MAX_DPATH]; 
    WCHAR szDescription[MAX_DPATH]; 
    WIN32_FIND_DATA wfd; 
 
	const TCHAR *ext = _tcsrchr (linkfile, '.');
	if (!ext || _tcsicmp (ext, _T(".lnk")) != 0)
		return false;

    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called. 
    hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl); 
    if (SUCCEEDED(hres)) 
    { 
        IPersistFile* ppf;
 
        // Get a pointer to the IPersistFile interface. 
        hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf); 
        
        if (SUCCEEDED(hres)) 
        { 
            // Add code here to check return value from MultiByteWideChar 
            // for success.
 
            // Load the shortcut. 
            hres = ppf->Load(linkfile, STGM_READ); 
            
            if (SUCCEEDED(hres)) 
            { 
                // Resolve the link. 
                hres = psl->Resolve(NULL, SLR_NO_UI); 

                if (SUCCEEDED(hres)) 
                { 
                    // Get the path to the link target. 
                    hres = psl->GetPath(szGotPath, MAX_DPATH, (WIN32_FIND_DATA*)&wfd, SLGP_SHORTPATH);

                    if (SUCCEEDED(hres)) 
                    { 
                        // Get the description of the target. 
                        hres = psl->GetDescription(szDescription, MAX_DPATH);

                        if (SUCCEEDED(hres)) 
                        {
							if (size > 0)
								my_canonicalize_path (szGotPath, linkfile, size);
                            ok = SUCCEEDED(hres);
                        }
                    }
                } 
            } 

            // Release the pointer to the IPersistFile interface. 
            ppf->Release(); 
        } 

        // Release the pointer to the IShellLink interface. 
        psl->Release(); 
    } 
    return ok; 
}

// http://msdn.microsoft.com/en-us/library/aa969393.aspx
bool my_createshortcut(const TCHAR *source, const TCHAR *target, const TCHAR *description) 
{ 
    HRESULT hres; 
    IShellLink* psl;
	TCHAR tmp[MAX_DPATH];
 
    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called.
    hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl); 
    if (SUCCEEDED(hres)) 
    { 
        IPersistFile* ppf; 
 
        // Set the path to the shortcut target and add the description. 
        psl->SetPath(target); 
        psl->SetDescription(description); 
 
        // Query IShellLink for the IPersistFile interface, used for saving the 
        // shortcut in persistent storage. 
        hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 
 
        if (SUCCEEDED(hres)) 
        { 
            // Save the link by calling IPersistFile::Save. 
			_tcscpy (tmp, source);
			const TCHAR *ext = _tcsrchr (tmp, '.');
			if (!ext || _tcsicmp (ext, _T(".lnk")) != 0)
				_tcscat (tmp, _T(".lnk"));
            hres = ppf->Save(tmp, TRUE); 
            ppf->Release(); 
        } 
        psl->Release(); 
    }
	return SUCCEEDED(hres);
}


bool my_resolvesoftlink(TCHAR *linkfile, int size, bool linkonly)
{
	if (!linkonly) {
		int v = my_resolvessymboliclink2(linkfile, size);
		if (v > 0)
			return true;
		if (v == 0)
			return false;
	}
	if (my_resolveshortcut(linkfile,size))
		return true;
	return false;
}

const TCHAR *my_getfilepart(const TCHAR *filename)
{
	const TCHAR *p;

	p = _tcsrchr(filename, '\\');
	if (p)
		return p + 1;
	p = _tcsrchr(filename, '/');
	if (p)
		return p + 1;
	return filename;
}

