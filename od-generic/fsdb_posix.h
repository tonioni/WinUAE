/* POSIX directory-drive metadata, compatible with Amiberry. */
#ifndef UAE_FSDB_POSIX_H
#define UAE_FSDB_POSIX_H
#include <string>
#include "zfile.h"
struct fsdb_posix_info {
    int mode;
    mytimeval time;
    std::string comment;
};
std::string fsdb_posix_encode(const char *name, bool ascii = false);
std::string fsdb_posix_decode(const char *name, bool noconvert = false);
int fsdb_posix_read(const char *name, fsdb_posix_info *info);
int fsdb_posix_write(const char *name, const fsdb_posix_info &info);
void fsdb_posix_sync_time(const char *name, const mytimeval *time);
bool fsdb_posix_hidden(const char *name);
#endif
