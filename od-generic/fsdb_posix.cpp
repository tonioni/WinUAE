/*
 * UAE - The Un*x Amiga Emulator
 * POSIX directory-drive names and Amiberry metadata.
 *
 * Filename and metadata format based on Amiberry osdep/fsdb_host.cpp.
 * Copyright 1997 Mathias Ortmann
 * Copyright 1999 Bernd Schmidt
 * Copyright 2025 Dimitris Panokostas
 * Copyright 2012 Frode Solheim
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "fsdb.h"
#include "uae.h"
#include "fsdb_posix.h"
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

static unsigned char fold(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xde))
	c += 32;
    return c;
}
int same_aname(const TCHAR *a, const TCHAR *b)
{
    if (!a || !b)
	return 0;
    while (*a && *b)
	if (fold((unsigned char)*a++) != fold((unsigned char)*b++))
	    return 0;
    return *a == *b;
}
static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9')
	return c - '0';
    c = fold(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
std::string fsdb_posix_encode(const char *name, bool ascii)
{
    std::string in = name ? name : "", out;
    size_t len = in.size();
    std::string lower = in;
    for (char &c : lower)
	c = (char)fold((unsigned char)c);
    size_t device = 0;
    for (const char *n : {"aux", "con", "prn", "nul"})
	if (lower.compare(0, 3, n) == 0)
	    device = 3;
    if (len >= 4 && (lower.compare(0, 3, "com") == 0 || lower.compare(0, 3, "lpt") == 0) &&
	lower[3] >= '0' && lower[3] <= '9')
	device = 4;
    bool reserved = device && (len == device || in[device] == '.');
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
	unsigned char c = (unsigned char)in[i];
	bool escape = c < 32 || (ascii && c > 127) || (in == FSDB_FILE && i == 0) ||
		      strchr("%\\*?\"/|<>", c) || (reserved && i == 2);
	if (i + 1 == len &&
	    (c == '.' || c == ' ' || (len >= 5 && lower.compare(len - 5, 5, ".uaem") == 0)))
	    escape = true;
	if (escape) {
	    out += '%';
	    out += digits[c >> 4];
	    out += digits[c & 15];
	} else if (c >= 128) {
	    out += (char)(0xc0 | (c >> 6));
	    out += (char)(0x80 | (c & 63));
	} else
	    out += (char)c;
    }
    return out;
}
static unsigned char compose_latin1(unsigned char base, unsigned int mark)
{
    // HFS+ exposes decomposed UTF-8 names. These are the canonical
    // base/combining-mark pairs representable in the guest character set.
    static const struct {
	unsigned int mark;
	const char *bases;
	const char *composed;
    } pairs[] = {
	{0x0300, "AEIOUaeiou", "\xc0\xc8\xcc\xd2\xd9\xe0\xe8\xec\xf2\xf9"},
	{0x0301, "AEIOUYaeiouy", "\xc1\xc9\xcd\xd3\xda\xdd\xe1\xe9\xed\xf3\xfa\xfd"},
	{0x0302, "AEIOUaeiou", "\xc2\xca\xce\xd4\xdb\xe2\xea\xee\xf4\xfb"},
	{0x0303, "ANOano", "\xc3\xd1\xd5\xe3\xf1\xf5"},
	{0x0308, "AEIOUaeiouy", "\xc4\xcb\xcf\xd6\xdc\xe4\xeb\xef\xf6\xfc\xff"},
	{0x030a, "Aa", "\xc5\xe5"},
	{0x0327, "Cc", "\xc7\xe7"},
    };
    for (const auto &pair : pairs) {
	if (pair.mark == mark) {
	    const char *p = strchr(pair.bases, base);
	    if (p)
		return (unsigned char)pair.composed[p - pair.bases];
	}
    }
    return 0;
}
std::string fsdb_posix_decode(const char *name, bool noconvert)
{
    std::string in = name ? name : "", latin, out;
    bool valid = true;
    for (size_t i = 0; i < in.size(); i++) {
	unsigned char c = (unsigned char)in[i];
	if (c < 128)
	    latin += (char)c;
	else if (c >= 0xc2 && c <= 0xdf && i + 1 < in.size() &&
		 ((unsigned char)in[i + 1] & 0xc0) == 0x80) {
	    unsigned int code = ((c & 31) << 6) | ((unsigned char)in[++i] & 63);
	    if (code <= 255) {
		latin += (char)code;
	    } else {
		unsigned char composed = latin.empty() ? 0 :
		    compose_latin1((unsigned char)latin.back(), code);
		if (!composed) {
		    valid = false;
		    break;
		}
		latin.back() = (char)composed;
	    }
	} else {
	    valid = false;
	    break;
	}
    }
    // Keep old raw Latin-1 names readable, and do not lose other host names.
    if (!valid || noconvert)
	latin = in;
    for (size_t i = 0; i < latin.size(); i++) {
	if (latin[i] == '%' && i + 2 < latin.size() && hex(latin[i + 1]) >= 0 &&
	    hex(latin[i + 2]) >= 0) {
	    int c = (hex(latin[i + 1]) << 4) | hex(latin[i + 2]);
	    if (c) {
		out += (char)c;
		i += 2;
		continue;
	    }
	}
	out += latin[i];
    }
    return out;
}
bool fsdb_posix_hidden(const char *name)
{
    size_t n = strlen(name);
    return !strcmp(name, FSDB_FILE) || (n >= 5 && !strcasecmp(name + n - 5, ".uaem"));
}
static bool raw_time(const char *name, mytimeval *tv)
{
    struct stat st;
    if (stat(name, &st))
	return false;
    struct tm local;
    if (!localtime_r(&st.st_mtime, &local))
	return false;
    tv->tv_sec = (uae_s64)st.st_mtime + local.tm_gmtoff;
#ifdef __APPLE__
    tv->tv_usec = st.st_mtimespec.tv_nsec / 1000;
#else
    tv->tv_usec = st.st_mtim.tv_nsec / 1000;
#endif
    return true;
}
int fsdb_posix_read(const char *name, fsdb_posix_info *info)
{
    if (!currprefs.filesys_custom_uaefsdb)
	return 0;
    std::string path = std::string(name) + ".uaem";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
	return errno == ENOENT ? 0 : -1;
    char buf[4097];
    size_t n = fread(buf, 1, sizeof(buf), f);
    bool failed = ferror(f) != 0;
    fclose(f);
    if (failed || n > 4096 || memchr(buf, 0, n))
	return -1;
    buf[n] = 0;
    const char *s = buf;
    if (n >= 3 && !memcmp(s, "\xef\xbb\xbf", 3)) {
	s += 3;
	n -= 3;
    }
    if (n < 31 || s[8] != ' ')
	return -1;
    int mask = 0;
    const char *flags = "hsparwed";
    for (int i = 0; i < 8; i++) {
	if (s[i] != '-' && s[i] != flags[i])
	    return -1;
	if (s[i] == flags[i])
	    mask |= 1 << (7 - i);
    }
    const char *t = s + 9;
    for (int i = 0; i < 22; i++) {
	char sep = i == 4 || i == 7	? '-'
		   : i == 10		? ' '
		   : i == 13 || i == 16 ? ':'
		   : i == 19		? '.'
					: 0;
	if (sep ? t[i] != sep : t[i] < '0' || t[i] > '9')
	    return -1;
    }
    int y, mo, d, h, mi, se, cs;
    if (sscanf(t, "%4d-%2d-%2d %2d:%2d:%2d.%2d", &y, &mo, &d, &h, &mi, &se, &cs) != 7)
	return -1;
    if (y < 1978 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59)
	return -1;
    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = se;
    time_t secs = timegm(&tm);
    if (secs == (time_t)-1 || tm.tm_year != y - 1900 || tm.tm_mon != mo - 1 || tm.tm_mday != d)
	return -1;
    fsdb_posix_info result;
    result.mode = mask ^ 15;
    result.time.tv_sec = secs;
    result.time.tv_usec = cs * 10000;
    const char *comment = t + 22;
    if (*comment == ' ')
	comment++;
    else if (*comment && *comment != '\n' && *comment != '\r')
	return -1;
    std::string encoded_comment(comment, strcspn(comment, "\r\n"));
    result.comment = fsdb_posix_decode(encoded_comment.c_str(), true);
    if (result.comment.size() > 80)
	return -1;
    *info = result;
    return 1;
}
int fsdb_posix_write(const char *name, const fsdb_posix_info &info)
{
    if (!currprefs.filesys_custom_uaefsdb)
	return 0;
    fsdb_posix_info previous;
    int exists = fsdb_posix_read(name, &previous);
    if (exists < 0)
	return ERROR_BAD_NUMBER;
    // Metadata-free files need no sidecar; their date is stored by the host.
    if (!info.mode && info.comment.empty()) {
	if (exists && unlink((std::string(name) + ".uaem").c_str()))
	    return dos_errno();
	return 0;
    }
    if (info.comment.size() > 80)
	return ERROR_COMMENT_TOO_BIG;
    time_t seconds = (time_t)info.time.tv_sec;
    struct tm tm;
    if (!gmtime_r(&seconds, &tm))
	return ERROR_BAD_NUMBER;
    char flags[9] = "--------";
    for (int i = 0; i < 8; i++)
	if ((info.mode ^ 15) & (1 << (7 - i)))
	    flags[i] = "hsparwed"[i];
    std::string dest = std::string(name) + ".uaem";
    std::string temp = dest + ".XXXXXX";
    std::vector<char> path(temp.begin(), temp.end());
    path.push_back(0);
    int fd = mkstemp(path.data());
    if (fd < 0)
	return dos_errno();
    FILE *f = fdopen(fd, "wb");
    if (!f) {
	int err = dos_errno();
	close(fd);
	unlink(path.data());
	return err;
    }
    std::string comment = fsdb_posix_encode(info.comment.c_str(), true);
    bool ok = fprintf(f, "%s %04d-%02d-%02d %02d:%02d:%02d.%02d %s\n", flags, tm.tm_year + 1900,
		      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
		      info.time.tv_usec / 10000, comment.c_str()) >= 0;
    if (fclose(f))
	ok = false;
    if (ok && rename(path.data(), dest.c_str()) == 0)
	return 0;
    int err = dos_errno();
    unlink(path.data());
    return err ? err : ERROR_DISK_IS_FULL;
}
void fsdb_posix_sync_time(const char *name, const mytimeval *time)
{
    fsdb_posix_info info;
    if (fsdb_posix_read(name, &info) != 1)
	return;
    if (time)
	info.time = *time;
    else if (!raw_time(name, &info.time))
	return;
    int err = fsdb_posix_write(name, info);
    if (err)
	write_log(_T("FSDB: could not update metadata time for '%s' (%d)\n"), name, err);
}

int fsdb_exists(const TCHAR *name) { return name && access(name, F_OK) == 0; }
int fsdb_name_invalid(a_inode *, const TCHAR *name)
{
    if (!name || !*name)
	return 1;
    return fsdb_posix_encode(name) != name || !strcmp(name, FSDB_FILE);
}
int fsdb_name_invalid_dir(a_inode *, const TCHAR *name)
{
    return !name || !*name || !strcmp(name, ".") || !strcmp(name, "..") || fsdb_posix_hidden(name);
}
int fsdb_fill_file_attrs(a_inode *, a_inode *aino)
{
    struct stat st;
    if (stat(aino->nname, &st))
	return 0;
    aino->dir = S_ISDIR(st.st_mode) != 0;
    aino->amigaos_mode = (st.st_mode & S_IWUSR) ? 0 : A_FIBF_WRITE;
    fsdb_posix_info info;
    if (fsdb_posix_read(aino->nname, &info) == 1) {
	aino->amigaos_mode = info.mode;
	xfree(aino->comment);
	aino->comment = info.comment.empty() ? NULL : my_strdup(info.comment.c_str());
    }
    return 1;
}
int fsdb_set_file_attrs(a_inode *aino)
{
    fsdb_posix_info info;
    int found = fsdb_posix_read(aino->nname, &info);
    if (found < 0)
	return ERROR_BAD_NUMBER;
    if (!found && !raw_time(aino->nname, &info.time))
	return dos_errno();
    struct stat st;
    if (stat(aino->nname, &st))
	return dos_errno();
    mode_t mode = st.st_mode;
    mode = (aino->amigaos_mode & A_FIBF_READ) ? mode & ~S_IRUSR : mode | S_IRUSR;
    mode = (aino->amigaos_mode & A_FIBF_WRITE) ? mode & ~S_IWUSR : mode | S_IWUSR;
    if (S_ISDIR(mode)) {
	mode_t bits = S_IXUSR | S_IXGRP | S_IXOTH;
	mode = (aino->amigaos_mode & A_FIBF_EXECUTE) ? mode & ~bits : mode | bits;
    } else if (aino->amigaos_mode & A_FIBF_EXECUTE)
	mode &= ~(S_IXUSR | S_IXGRP | S_IXOTH);
    if (mode & S_IRUSR)
	mode |= S_IRGRP | S_IROTH;
    if (chmod(aino->nname, mode))
	return dos_errno();
    info.mode = aino->amigaos_mode;
    info.comment = aino->comment ? aino->comment : "";
    int err = fsdb_posix_write(aino->nname, info);
    aino->dirty = 1;
    return err;
}
int fsdb_mode_representable_p(const a_inode *, int)
{
    return currprefs.filesys_custom_uaefsdb ? 1 : 0;
}
int fsdb_mode_supported(const a_inode *aino)
{
    return currprefs.filesys_custom_uaefsdb ? aino->amigaos_mode : 0;
}

static std::string find_native(const char *dir, const char *guest)
{
    std::string encoded = fsdb_posix_encode(guest);
    std::string full = std::string(dir) + "/" + encoded;
    if (fsdb_exists(full.c_str()))
	return encoded;
    DIR *d = opendir(dir);
    if (!d)
	return "";
    std::string match;
    while (dirent *e = readdir(d)) {
	if (fsdb_posix_hidden(e->d_name) || !strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
	    continue;
	std::string decoded = fsdb_posix_decode(e->d_name);
	if (same_aname(decoded.c_str(), guest)) {
	    match = e->d_name;
	    break;
	}
    }
    closedir(d);
    return match;
}
TCHAR *fsdb_search_dir(const TCHAR *dirname, TCHAR *rel, TCHAR **relalt)
{
    if (relalt)
	*relalt = NULL;
    std::string name = find_native(dirname, rel);
    if (name.empty())
	return NULL;
    std::string guest = fsdb_posix_decode(name.c_str());
    if (relalt && name != guest)
	*relalt = my_strdup(name.c_str());
    return guest == rel ? rel : my_strdup(guest.c_str());
}
TCHAR *fsdb_create_unique_nname(a_inode *base, const TCHAR *suggestion)
{
    std::string name = fsdb_posix_encode(suggestion);
    return build_nname(base->nname, name.c_str());
}
a_inode *custom_fsdb_lookup_aino_nname(a_inode *base, const TCHAR *name)
{
    if (fsdb_posix_hidden(name))
	return NULL;
    TCHAR *full = build_nname(base->nname, name);
    if (!fsdb_exists(full)) {
	xfree(full);
	return NULL;
    }
    a_inode *a = xcalloc(a_inode, 1);
    a->nname = full;
    a->aname = my_strdup(fsdb_posix_decode(name).c_str());
    if (!fsdb_fill_file_attrs(base, a)) {
	xfree(a->nname);
	xfree(a->aname);
	xfree(a);
	return NULL;
    }
    return a;
}
a_inode *custom_fsdb_lookup_aino_aname(a_inode *base, const TCHAR *name)
{
    std::string native = find_native(base->nname, name);
    return native.empty() ? NULL : custom_fsdb_lookup_aino_nname(base, native.c_str());
}
int custom_fsdb_used_as_nname(a_inode *, const TCHAR *) { return 1; }
