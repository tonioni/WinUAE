#include "sysconfig.h"
#include "sysdeps.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/disk.h>
#include <sys/ioctl.h>
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#include "options.h"
#include "filesys.h"
#include "uae.h"
#include "uae/io.h"

struct hardfilehandle {
    int fd;
    bool realdrive;
};

#define UNIX_MAX_NATIVE_DRIVES MAX_FILESYSTEM_UNITS

struct unix_driveinfo {
    TCHAR device_name[1024];
    TCHAR device_path[1024];
    TCHAR device_full_path[2048];
    uae_u64 size;
    int bytespersector;
    int readonly;
    int dangerous;
};

static unix_driveinfo unix_drives[UNIX_MAX_NATIVE_DRIVES];
static int unix_drive_count;

static bool query_fd_geometry(int fd, uae_u64 *size, int *blocksize)
{
    if (size) {
        *size = 0;
    }
    if (blocksize) {
        *blocksize = 512;
    }
#if defined(__APPLE__)
    uint32_t block_size = 512;
    uint64_t block_count = 0;
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &block_size) == 0 && block_size > 0 && blocksize) {
        *blocksize = (int)block_size;
    }
    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &block_count) == 0 && size) {
        *size = block_count * (uae_u64)(blocksize ? *blocksize : block_size);
        return *size > 0;
    }
#elif defined(__linux__)
    unsigned long long bytes = 0;
    int logical_block_size = 512;
    if (ioctl(fd, BLKSSZGET, &logical_block_size) == 0 && logical_block_size > 0 && blocksize) {
        *blocksize = logical_block_size;
    }
    if (ioctl(fd, BLKGETSIZE64, &bytes) == 0 && size) {
        *size = (uae_u64)bytes;
        return *size > 0;
    }
#endif
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && size) {
        *size = (uae_u64)st.st_size;
        return true;
    }
    return false;
}

static bool query_path_geometry(const char *path, uae_u64 *size, int *blocksize)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    bool ok = query_fd_geometry(fd, size, blocksize);
    close(fd);
    return ok;
}

static bool unix_drive_path_exists(const char *path)
{
    for (int i = 0; i < unix_drive_count; i++) {
        if (!_tcscmp(unix_drives[i].device_path, path)) {
            return true;
        }
    }
    return false;
}

static void add_unix_drive(const char *path, const char *display_name)
{
#ifdef WINUAE_UNIX_WITH_NATIVE_HARDDRIVES
    if (!path || !path[0] || unix_drive_count >= UNIX_MAX_NATIVE_DRIVES || unix_drive_path_exists(path)) {
        return;
    }
    uae_u64 size = 0;
    int blocksize = 512;
    if (!query_path_geometry(path, &size, &blocksize) || size == 0) {
        return;
    }
    unix_driveinfo *udi = &unix_drives[unix_drive_count++];
    memset(udi, 0, sizeof *udi);
    _tcsncpy(udi->device_path, path, sizeof udi->device_path / sizeof(TCHAR) - 1);
    _sntprintf(udi->device_full_path, sizeof udi->device_full_path / sizeof(TCHAR), _T(":%s"), path);
    _sntprintf(udi->device_name, sizeof udi->device_name / sizeof(TCHAR), _T(":%s"),
        display_name && display_name[0] ? display_name : path);
    udi->size = size;
    udi->bytespersector = blocksize > 0 ? blocksize : 512;
    udi->readonly = 1;
    udi->dangerous = 0;
#else
    (void)path;
    (void)display_name;
#endif
}

static void enumerate_unix_drives(void)
{
    unix_drive_count = 0;
#ifdef WINUAE_UNIX_WITH_NATIVE_HARDDRIVES
#if defined(__APPLE__)
    DIR *dir = opendir("/dev");
    if (!dir) {
        return;
    }
    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        const char *name = entry->d_name;
        if (strncmp(name, "rdisk", 5) != 0) {
            continue;
        }
        if (strchr(name + 5, 's')) {
            continue;
        }
        char path[PATH_MAX];
        snprintf(path, sizeof path, "/dev/%s", name);
        add_unix_drive(path, name);
    }
    closedir(dir);
#elif defined(__linux__)
    DIR *dir = opendir("/sys/block");
    if (!dir) {
        return;
    }
    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        const char *name = entry->d_name;
        if (name[0] == '.') {
            continue;
        }
        if (!strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) || !strncmp(name, "zram", 4) || !strncmp(name, "dm-", 3)) {
            continue;
        }
        char path[PATH_MAX];
        snprintf(path, sizeof path, "/dev/%s", name);
        add_unix_drive(path, name);
    }
    closedir(dir);
#endif
#endif
}

static bool is_native_device_name(const TCHAR *name)
{
    return name && name[0] == ':' && name[1] == '/';
}

int hdf_init_target(void)
{
    enumerate_unix_drives();
    return 1;
}

int hdf_open_target(struct hardfiledata *hfd, const TCHAR *name)
{
    if (!hfd || !name) {
        return 0;
    }
    hdf_close_target(hfd);
    hfd->handle = xcalloc(hardfilehandle, 1);
    hfd->handle->fd = -1;
    hfd->cache = xcalloc(uae_u8, 16384);
    hfd->cache_valid = 0;
    hfd->flags = 0;
    if (is_native_device_name(name)) {
#ifdef WINUAE_UNIX_WITH_NATIVE_HARDDRIVES
        const TCHAR *path = name + 1;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            hdf_close_target(hfd);
            return 0;
        }
        uae_u64 size = 0;
        int blocksize = hfd->ci.blocksize > 0 ? hfd->ci.blocksize : 512;
        if (!query_fd_geometry(fd, &size, &blocksize) || size == 0) {
            close(fd);
            hdf_close_target(hfd);
            return 0;
        }
        hfd->handle->fd = fd;
        hfd->handle->realdrive = true;
        hfd->flags = HFD_FLAGS_REALDRIVE;
        hfd->ci.readonly = true;
        hfd->physsize = hfd->virtsize = size;
        hfd->offset = 0;
        hfd->handle_valid = 1;
        hfd->ci.blocksize = blocksize;
        _tcsncpy(hfd->vendor_id, _T("Unix"), sizeof hfd->vendor_id / sizeof(TCHAR) - 1);
        _tcsncpy(hfd->product_id, path, sizeof hfd->product_id / sizeof(TCHAR) - 1);
        return 1;
#else
        hdf_close_target(hfd);
        return 0;
#endif
    }
    int open_flags = hfd->ci.readonly ? O_RDONLY : O_RDWR;
    hfd->handle->fd = open(name, open_flags | O_CLOEXEC);
    if (hfd->handle->fd < 0 && !hfd->ci.readonly) {
        hfd->ci.readonly = true;
        hfd->handle->fd = open(name, O_RDONLY | O_CLOEXEC);
    }
    if (hfd->handle->fd < 0) {
        hdf_close_target(hfd);
        return 0;
    }
    uae_u64 size = 0;
    int blocksize = hfd->ci.blocksize > 0 ? hfd->ci.blocksize : 512;
    if (!query_fd_geometry(hfd->handle->fd, &size, &blocksize)) {
        hdf_close_target(hfd);
        return 0;
    }
    hfd->physsize = hfd->virtsize = size;
    hfd->offset = 0;
    hfd->handle_valid = 1;
    if (hfd->ci.blocksize <= 0) {
        hfd->ci.blocksize = blocksize > 0 ? blocksize : 512;
    }
    return 1;
}

int hdf_dup_target(struct hardfiledata *dhfd, const struct hardfiledata *shfd)
{
    if (!dhfd || !shfd) {
        return 0;
    }
    *dhfd = *shfd;
    dhfd->handle = NULL;
    dhfd->cache = NULL;
    return hdf_open_target(dhfd, shfd->ci.rootdir);
}

void hdf_close_target(struct hardfiledata *hfd)
{
    if (!hfd) {
        return;
    }
    if (hfd->handle) {
        if (hfd->handle->fd >= 0) {
            close(hfd->handle->fd);
        }
        xfree(hfd->handle);
        hfd->handle = NULL;
    }
    xfree(hfd->cache);
    hfd->cache = NULL;
    hfd->cache_valid = 0;
    hfd->handle_valid = 0;
}

int hdf_read_target(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len, uae_u32 *error)
{
    if (error) {
        *error = 0;
    }
    if (!hfd || !hfd->handle || hfd->handle->fd < 0) {
        if (error) {
            *error = errno;
        }
        return 0;
    }
    ssize_t got = pread(hfd->handle->fd, buffer, len, (off_t)(offset + hfd->offset));
    if (got < 0) {
        if (error) {
            *error = errno;
        }
        return 0;
    }
    return (int)got;
}

int hdf_write_target(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len, uae_u32 *error)
{
    if (error) {
        *error = 0;
    }
    if (!hfd || hfd->ci.readonly || !hfd->handle || hfd->handle->fd < 0 || hfd->handle->realdrive) {
        if (error) {
            *error = errno ? errno : EACCES;
        }
        return 0;
    }
    ssize_t done = pwrite(hfd->handle->fd, buffer, len, (off_t)(offset + hfd->offset));
    if (done < 0) {
        if (error) {
            *error = errno;
        }
        return 0;
    }
    return (int)done;
}

int hdf_resize_target(struct hardfiledata *hfd, uae_u64 newsize)
{
    if (!hfd || !hfd->handle || hfd->handle->fd < 0 || hfd->handle->realdrive) {
        return 0;
    }
    if (ftruncate(hfd->handle->fd, (off_t)newsize) != 0) {
        return 0;
    }
    hfd->physsize = hfd->virtsize = newsize;
    return 1;
}

int hdf_getnumharddrives(void)
{
    if (unix_drive_count == 0) {
        enumerate_unix_drives();
    }
    return unix_drive_count;
}

TCHAR *hdf_getnameharddrive(int index, int flags, int *sectorsize, int *dangerousdrive, uae_u32 *outflags)
{
    static TCHAR name[2048];
    if (sectorsize) {
        *sectorsize = 512;
    }
    if (dangerousdrive) {
        *dangerousdrive = 0;
    }
    if (outflags) {
        *outflags = 0;
    }
    if (index < 0 || index >= hdf_getnumharddrives()) {
        name[0] = 0;
        return name;
    }
    unix_driveinfo *udi = &unix_drives[index];
    if (sectorsize) {
        *sectorsize = udi->bytespersector;
    }
    if (dangerousdrive) {
        *dangerousdrive = udi->dangerous ? 0 : 1;
        if (udi->readonly) {
            *dangerousdrive |= 2;
        }
    }
    if (flags & 4) {
        return udi->device_full_path;
    }
    if (flags & 2) {
        return udi->device_path;
    }
    if (flags & 1) {
        TCHAR size_text[32];
        if (udi->size >= 1024ULL * 1024ULL * 1024ULL) {
            _sntprintf(size_text, sizeof size_text / sizeof(TCHAR), _T("%.1fG"), (double)udi->size / (1024.0 * 1024.0 * 1024.0));
        } else if (udi->size >= 1024ULL * 1024ULL) {
            _sntprintf(size_text, sizeof size_text / sizeof(TCHAR), _T("%.1fM"), (double)udi->size / (1024.0 * 1024.0));
        } else {
            _sntprintf(size_text, sizeof size_text / sizeof(TCHAR), _T("%lluK"), (unsigned long long)(udi->size / 1024));
        }
        _sntprintf(name, sizeof name / sizeof(TCHAR), _T("%10s [%s,RO] %s"), _T("[OS]"), size_text, udi->device_name + 1);
        return name;
    }
    return udi->device_name;
}

int get_guid_target(uae_u8 *out)
{
    if (!out) {
        return 0;
    }
    for (int i = 0; i < 16; i++) {
        out[i] = (uae_u8)uaerand();
    }
    return 1;
}
