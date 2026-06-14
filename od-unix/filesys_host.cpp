#include "sysconfig.h"
#include "sysdeps.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include "fsdb.h"
#include "fsusage.h"
#include "uae/io.h"
#include "uae/string.h"
#include "zfile.h"

struct my_opendir_s {
    DIR *dir;
};

struct my_openfile_s {
    int fd;
};

bool my_issamepath(const TCHAR *path1, const TCHAR *path2);

static uae_u32 mode_from_stat(const struct stat &st)
{
    uae_u32 mode = 0;
    if (S_ISDIR(st.st_mode)) {
        mode |= FILEFLAG_DIR;
    }
    if (st.st_mode & S_IRUSR) {
        mode |= FILEFLAG_READ;
    }
    if (st.st_mode & S_IWUSR) {
        mode |= FILEFLAG_WRITE;
    }
    if (st.st_mode & S_IXUSR) {
        mode |= FILEFLAG_EXECUTE;
    }
    return mode;
}

FILE *uae_tfopen(const TCHAR *path, const TCHAR *mode)
{
    return uae_unix_tfopen(path, mode);
}

int dos_errno(void)
{
    switch (errno) {
    case 0: return 0;
    case ENOENT: return ERROR_OBJECT_NOT_AROUND;
    case EACCES:
    case EPERM: return ERROR_WRITE_PROTECTED;
    case ENOTDIR: return ERROR_DIR_NOT_FOUND;
    case EEXIST: return ERROR_OBJECT_EXISTS;
    case ENOSPC: return ERROR_DISK_IS_FULL;
    case ENOTEMPTY: return ERROR_DIRECTORY_NOT_EMPTY;
    default: return ERROR_OBJECT_NOT_AROUND;
    }
}

my_opendir_s *my_opendir(const TCHAR *name, const TCHAR *)
{
    DIR *dir = opendir(name);
    if (!dir) {
        return NULL;
    }
    my_opendir_s *out = xcalloc(my_opendir_s, 1);
    out->dir = dir;
    return out;
}

my_opendir_s *my_opendir(const TCHAR *name)
{
    return my_opendir(name, _T("*"));
}

void my_closedir(my_opendir_s *mod)
{
    if (!mod) {
        return;
    }
    if (mod->dir) {
        closedir(mod->dir);
    }
    xfree(mod);
}

int my_readdir(my_opendir_s *mod, TCHAR *name)
{
    if (!mod || !mod->dir || !name) {
        return 0;
    }
    for (;;) {
        struct dirent *de = readdir(mod->dir);
        if (!de) {
            return 0;
        }
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        uae_tcslcpy(name, de->d_name, MAX_DPATH);
        return 1;
    }
}

int my_rmdir(const TCHAR *name) { return rmdir(name); }
int my_mkdir(const TCHAR *name) { return mkdir(name, 0777); }
int my_unlink(const TCHAR *name, bool) { return unlink(name); }
int my_rename(const TCHAR *oldname, const TCHAR *newname) { return rename(oldname, newname); }
int my_truncate(const TCHAR *name, uae_u64 len) { return truncate(name, (off_t)len); }

my_openfile_s *my_open(const TCHAR *name, int flags)
{
    int fd = open(name, flags, 0666);
    if (fd < 0) {
        return NULL;
    }
    my_openfile_s *out = xcalloc(my_openfile_s, 1);
    out->fd = fd;
    return out;
}

void my_close(my_openfile_s *mos)
{
    if (!mos) {
        return;
    }
    if (mos->fd >= 0) {
        close(mos->fd);
    }
    xfree(mos);
}

uae_s64 my_lseek(my_openfile_s *mos, uae_s64 offset, int whence)
{
    return mos ? (uae_s64)lseek(mos->fd, (off_t)offset, whence) : -1;
}

uae_s64 my_fsize(my_openfile_s *mos)
{
    struct stat st;
    if (!mos || fstat(mos->fd, &st) != 0) {
        return -1;
    }
    return (uae_s64)st.st_size;
}

unsigned int my_read(my_openfile_s *mos, void *b, unsigned int size)
{
    ssize_t ret = mos ? read(mos->fd, b, size) : -1;
    return ret < 0 ? 0 : (unsigned int)ret;
}

unsigned int my_write(my_openfile_s *mos, void *b, unsigned int size)
{
    ssize_t ret = mos ? write(mos->fd, b, size) : -1;
    return ret < 0 ? 0 : (unsigned int)ret;
}

int my_existsfile(const TCHAR *name)
{
    struct stat st;
    return name && stat(name, &st) == 0 && S_ISREG(st.st_mode);
}

int my_existsdir(const TCHAR *name)
{
    struct stat st;
    return name && stat(name, &st) == 0 && S_ISDIR(st.st_mode);
}

bool my_existsfiledir(const TCHAR *name)
{
    struct stat st;
    return name && stat(name, &st) == 0;
}

FILE *my_opentext(const TCHAR *name)
{
    return uae_tfopen(name, _T("r"));
}

bool my_stat(const TCHAR *name, struct mystat *ms)
{
    struct stat st;
    if (!name || !ms || stat(name, &st) != 0) {
        return false;
    }
    ms->size = (uae_s64)st.st_size;
    ms->mode = mode_from_stat(st);
    ms->mtime.tv_sec = (uae_s64)st.st_mtime;
    ms->mtime.tv_usec = 0;
    return true;
}

bool my_utime(const TCHAR *name, struct mytimeval *tv)
{
    if (!name) {
        return false;
    }
    if (!tv) {
        return utimes(name, NULL) == 0;
    }
    struct timeval times[2];
    times[0].tv_sec = (time_t)tv->tv_sec;
    times[0].tv_usec = tv->tv_usec;
    times[1] = times[0];
    return utimes(name, times) == 0;
}

bool my_chmod(const TCHAR *name, uae_u32 mode)
{
    struct stat st;
    if (!name || stat(name, &st) != 0) {
        return false;
    }
    mode_t m = st.st_mode;
    if (mode & FILEFLAG_WRITE) {
        m |= S_IWUSR;
    } else {
        m &= ~S_IWUSR;
    }
    return chmod(name, m) == 0;
}

bool my_resolveshortcut(TCHAR *, int) { return false; }
bool my_resolvessymboliclink(TCHAR *, int) { return false; }
bool my_resolvesoftlink(TCHAR *, int, bool) { return false; }
bool my_isfilehidden(const TCHAR *path) { return path && path[0] == '.'; }
void my_setfilehidden(const TCHAR *, bool) {}
int my_readonlyfile(const TCHAR *path)
{
    return access(path, W_OK) != 0;
}

const TCHAR *my_getfilepart(const TCHAR *filename)
{
    const TCHAR *slash = filename ? _tcsrchr(filename, FSDB_DIR_SEPARATOR) : NULL;
    return slash ? slash + 1 : filename;
}

void my_canonicalize_path(const TCHAR *path, TCHAR *out, int size)
{
    if (!path || !out || size <= 0) {
        return;
    }
    char resolved[PATH_MAX];
    if (realpath(path, resolved)) {
        uae_tcslcpy(out, resolved, size);
    } else {
        uae_tcslcpy(out, path, size);
    }
}

int my_setcurrentdir(const TCHAR *curdir, TCHAR *oldcur)
{
    if (oldcur) {
        if (!getcwd(oldcur, MAX_DPATH)) {
            oldcur[0] = 0;
        }
    }
    return curdir ? chdir(curdir) : -1;
}

int my_issamevolume(const TCHAR *path1, const TCHAR *path2, TCHAR *path)
{
    if (path && path1) {
        uae_tcslcpy(path, path1, MAX_DPATH);
    }
    return my_issamepath(path1, path2);
}

bool my_issamepath(const TCHAR *path1, const TCHAR *path2)
{
    char r1[PATH_MAX], r2[PATH_MAX];
    const char *p1 = realpath(path1, r1) ? r1 : path1;
    const char *p2 = realpath(path2, r2) ? r2 : path2;
    return p1 && p2 && !_tcscmp(p1, p2);
}

bool my_createsoftlink(const TCHAR *path, const TCHAR *target)
{
    return symlink(target, path) == 0;
}

bool my_createshortcut(const TCHAR *, const TCHAR *, const TCHAR *) { return false; }
void makesafefilename(TCHAR *s, bool)
{
    for (; s && *s; s++) {
        if (*s == '/' || *s == ':') {
            *s = '_';
        }
    }
}

int my_getvolumeinfo(const TCHAR *)
{
    return 0;
}

int get_fs_usage(const TCHAR *path, const TCHAR *, struct fs_usage *fsp)
{
    struct statvfs svfs;
    if (!path || !fsp || statvfs(path, &svfs) != 0) {
        return -1;
    }
    uae_u64 bsize = svfs.f_frsize ? svfs.f_frsize : svfs.f_bsize;
    fsp->total = (uae_u64)svfs.f_blocks * bsize / 512;
    fsp->avail = (uae_u64)svfs.f_bavail * bsize / 512;
    return 0;
}
