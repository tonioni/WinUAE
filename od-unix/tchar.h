#ifndef WINUAE_OD_UNIX_TCHAR_H
#define WINUAE_OD_UNIX_TCHAR_H

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

typedef char TCHAR;

#ifndef _T
#define _T(x) x
#endif

static inline FILE *uae_unix_tfopen(const TCHAR *path, const TCHAR *mode)
{
    char unixmode[8];
    size_t out = 0;
    for (size_t i = 0; mode[i] != 0 && mode[i] != ',' && out + 1 < sizeof(unixmode); i++) {
        if (mode[i] == 't' || mode[i] == ' ') {
            continue;
        }
        unixmode[out++] = mode[i];
    }
    unixmode[out] = 0;
    return fopen(path, unixmode[0] ? unixmode : mode);
}

#define _istdigit isdigit
#define _istspace isspace
#define _istupper isupper
#define _istxdigit isxdigit
#define _sntprintf snprintf
#define _stscanf sscanf
#define _stprintf sprintf
#define _strtoui64 strtoull
#define _tcscat strcat
#define _tcschr strchr
#define _tcscmp strcmp
#define _tcscpy strcpy
#define _tcscspn strcspn
#define _tcsdup strdup
#define _tcsftime strftime
#define _tcsicmp strcasecmp
#define _tcslen strlen
#define _tcsncat strncat
#define _tcsncmp strncmp
#define _tcsncpy strncpy
#define _tcsnicmp strncasecmp
#define _tcsrchr strrchr
#define _tcsspn strspn
#define _tcsstr strstr
#define _tcstod strtod
#define _tcstok strtok
#define _tcstol strtol
#define _tcstoul strtoul
#define _totlower tolower
#define _totupper toupper
#define _tprintf printf
#define _tstof atof
#define _tstoi atoi
#define _tstoi64 atoll
#define _tstol atol
#define _tfopen uae_unix_tfopen
#define _tfopen64 uae_unix_tfopen
#define _ftelli64 ftello
#define _fseeki64 fseeko
#define _tunlink unlink
#define _vsnprintf vsnprintf
#define _vsntprintf vsnprintf
#define fgetws fgets
#define fputws fputs
#define _wunlink unlink
#define swscanf_s sscanf

#endif /* WINUAE_OD_UNIX_TCHAR_H */
