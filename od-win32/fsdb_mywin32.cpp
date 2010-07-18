#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"

#include "fsdb.h"
#include "win32.h"
#include <windows.h>

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
	if (curdir)
		ret = SetCurrentDirectory (curdir);
	return ret;
}

int my_mkdir (const TCHAR *name)
{
	return CreateDirectory (name, NULL) == 0 ? -1 : 0;
}

static int recycle (const TCHAR *name)
{
	if (currprefs.win32_norecyclebin) {
		DWORD dirattr = GetFileAttributes (name);
		if (dirattr != INVALID_FILE_ATTRIBUTES && (dirattr & FILE_ATTRIBUTE_DIRECTORY))
			return RemoveDirectory (name) ? 0 : -1;
		return DeleteFile(name) ? 0 : -1;
	} else {
		SHFILEOPSTRUCT fos;
		/* name must be terminated by \0\0 */
		TCHAR *p = xcalloc (TCHAR, _tcslen (name) + 2);
		int v;

		_tcscpy (p, name);
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
		if (!_tcscmp (tname, L".") || !_tcscmp (tname, L".."))
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
	return MoveFile (oldname, newname) == 0 ? -1 : 0;
}

struct my_opendir_s {
	HANDLE h;
	WIN32_FIND_DATA fd;
	int first;
};

struct my_opendir_s *my_opendir (const TCHAR *name)
{
	return my_opendir (name, L"*.*");
}
struct my_opendir_s *my_opendir (const TCHAR *name, const TCHAR *mask)
{
	struct my_opendir_s *mod;
	TCHAR tmp[MAX_DPATH];

	_tcscpy (tmp, name);
	_tcscat (tmp, L"\\");
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
	LARGE_INTEGER li;

	li.QuadPart = offset;
	li.LowPart = SetFilePointer (mos->h, li.LowPart, &li.HighPart,
		whence == SEEK_SET ? FILE_BEGIN : (whence == SEEK_END ? FILE_END : FILE_CURRENT));
	if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
		li.QuadPart = -1;
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

static DWORD GetFileAttributesSafe (const TCHAR *name)
{
	DWORD attr, last;

	last = SetErrorMode (SEM_FAILCRITICALERRORS);
	attr = GetFileAttributes (name);
	SetErrorMode (last);
	return attr;
}

int my_existsfile (const TCHAR *name)
{
	DWORD attr = GetFileAttributesSafe (name);
	if (attr == INVALID_FILE_ATTRIBUTES)
		return 0;
	if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
		return 1;
	return 0;
}

int my_existsdir (const TCHAR *name)
{
	DWORD attr = GetFileAttributesSafe (name);
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
		DWORD err = GetLastError();
		if (err == ERROR_ACCESS_DENIED && (DesiredAccess & GENERIC_WRITE)) {
			DesiredAccess &= ~GENERIC_WRITE;
			h = CreateFile (name, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
			if (h == INVALID_HANDLE_VALUE)
				err = GetLastError();
		}
		if (h == INVALID_HANDLE_VALUE) {
			write_log (L"failed to open '%s' %x %x err=%d\n", name, DesiredAccess, CreationDisposition, err);
			xfree (mos);
			mos = NULL;
			goto err;
		}
	}
	mos->h = h;
err:
	//write_log (L"open '%s' = %x\n", name, mos ? mos->h : 0);
	return mos;
}

int my_truncate (const TCHAR *name, uae_u64 len)
{
	HANDLE hFile;
	BOOL bResult = FALSE;
	int result = -1;

	if ((hFile = CreateFile (name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ) ) != INVALID_HANDLE_VALUE )
	{
		LARGE_INTEGER li;
		li.QuadPart = len;
		li.LowPart = SetFilePointer (hFile, li.LowPart, &li.HighPart, FILE_BEGIN);
		if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR) {
			write_log (L"truncate: SetFilePointer() failure for %s to posn %d\n", name, len);
		} else {
			if (SetEndOfFile (hFile) == TRUE)
				result = 0;
		}
		CloseHandle (hFile);
	} else {
		write_log (L"truncate: CreateFile() failed to open %s\n", name);
	}
	return result;
}

int dos_errno (void)
{
	DWORD e = GetLastError ();

	//write_log (L"ec=%d\n", e);
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
		return ERROR_OBJECT_NOT_AROUND;

	case ERROR_HANDLE_DISK_FULL:
	case ERROR_DISK_FULL:
		return ERROR_DISK_IS_FULL;

	case ERROR_SHARING_VIOLATION:
	case ERROR_BUSY:
	case ERROR_INVALID_HANDLE:
	case ERROR_USER_MAPPED_FILE:
		return ERROR_OBJECT_IN_USE;

	case ERROR_CURRENT_DIRECTORY:
		return ERROR_DIRECTORY_NOT_EMPTY;

	case ERROR_NEGATIVE_SEEK:
	case ERROR_SEEK_ON_DEVICE:
		return ERROR_SEEK_ERROR;

	default:
		{
			static int done;
			if (!done)
				gui_message (L"Unimplemented error %d\nContact author!", e);
			done = 1;
		}
		return ERROR_NOT_IMPLEMENTED;
	}
}

typedef BOOL (CALLBACK* GETVOLUMEPATHNAME)
	(LPCTSTR lpszFileName, LPTSTR lpszVolumePathName, DWORD cchBufferLength);

int my_getvolumeinfo (const TCHAR *root)
{
	DWORD v, err;
	int ret = 0;
	GETVOLUMEPATHNAME pGetVolumePathName;
	TCHAR volume[MAX_DPATH];

	v = GetFileAttributesSafe (root);
	err = GetLastError ();
	if (v == INVALID_FILE_ATTRIBUTES)
		return -1;
	if (!(v & FILE_ATTRIBUTE_DIRECTORY))
		return -1;
	/*
	if (v & FILE_ATTRIBUTE_READONLY)
	ret |= MYVOLUMEINFO_READONLY;
	*/
	pGetVolumePathName = (GETVOLUMEPATHNAME)GetProcAddress(
		GetModuleHandle (L"kernel32.dll"), "GetVolumePathNameW");
	if (pGetVolumePathName && pGetVolumePathName (root, volume, sizeof (volume))) {
		TCHAR fsname[MAX_DPATH];
		DWORD comlen;
		DWORD flags;
		if (GetVolumeInformation (volume, NULL, 0, NULL, &comlen, &flags, fsname, sizeof (fsname))) {
			//write_log (L"Volume %s FS=%s maxlen=%d flags=%08X\n", volume, fsname, comlen, flags);
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

	f = _tfopen (name, L"rb");
	if (!f)
		return NULL;
	v = fread (tmp, 1, 4, f);
	fclose (f);
	if (v == 4) {
		if (tmp[0] == 0xef && tmp[1] == 0xbb && tmp[2] == 0xbf)
			return _tfopen (name, L"r, ccs=UTF-8");
		if (tmp[0] == 0xff && tmp[1] == 0xfe)
			return _tfopen (name, L"r, ccs=UTF-16LE");
	}
	return _tfopen (name, L"r");
}
