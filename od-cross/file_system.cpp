#include <stdio.h>
#include "sysdeps.h"

TCHAR start_path_data[MAX_DPATH];

int pissoff_value = 15000 * CYCLE_UNIT;
int pause_emulation = 0;

int my_existsdir(const TCHAR* directoryPath) {
    struct stat st;
    
    if (stat(directoryPath, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 1;
        } 
    } 

   return 0;
}

#ifdef _WIN32

#include <Windows.h>

int my_existsfile (const TCHAR *name)
{
	DWORD attr;
	
	attr = GetFileAttributes (name);
	if (attr == INVALID_FILE_ATTRIBUTES)
		return 0;
	if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
		return 1;
	return 0;
}

#else

int my_existsfile(const TCHAR* name) {
    if (access(name, F_OK) != -1) {
        return 1;
    } else {
        return 0;
    }
}

#endif

int isprinter() {
    return 0;
}

void to_lower(TCHAR *s, int len) {
    for (int i = 0; i < len; i++) {
        s[i] = tolower(s[i]);
    }
}


TCHAR* utf8u (const char *s)
{
	if (s == NULL) return NULL;
	return ua (s);
}

char* uutf8 (const TCHAR *s)
{
	if (s == NULL) return NULL;
	return ua (s);
}

/*
TCHAR *au_copy (TCHAR *dst, int maxlen, const char *src)
{
	// this should match the WinUAE au_copy behavior, where either the
	// entire string is copied (and null-terminated), or the result is
	// an empty string
	if (uae_tcslcpy (dst, src, maxlen) >= maxlen) {
		dst[0] = '\0';
	}
	return dst;
}

char *ua_copy (char *dst, int maxlen, const TCHAR *src)
{
	return au_copy (dst, maxlen, src);
}
*/

TCHAR *my_strdup_ansi (const char *src)
{
	return strdup (src);
}

#define NO_TRANSLATION

TCHAR *au_fs (const char *src) {
#ifdef NO_TRANSLATION
    if (src == NULL) return NULL;
    return strdup(src);
#else
    gsize read, written;
    gchar *result = g_convert(src, -1, "UTF-8",
            "ISO-8859-1", &read, &written, NULL);
    if (result == NULL) {
        write_log("WARNING: au_fs_copy failed to convert string %s", src);
        return strdup("");
    }
    return result;
#endif
}

char *ua_fs (const TCHAR *s, int defchar) {
#ifdef NO_TRANSLATION
    if (s == NULL) return NULL;
    return strdup(s);
#else
    // we convert from fs-uae's internal encoding (UTF-8) to latin-1 here,
    // so file names can be read properly in the amiga

    char def[] = "?";
    if (defchar < 128) {
        def[0] = defchar;
    }

    gsize read, written;
    gchar *result = g_convert_with_fallback(s, -1, "ISO-8859-1",
            "UTF-8", def, &read, &written, NULL);
    if (result == NULL) {
        write_log("WARNING: ua_fs failed to convert string %s", s);
        return strdup("");
    }

    // duplicate with libc malloc
    char *result_malloced = strdup(result);
    free(result);
    return result_malloced;
#endif
}

TCHAR *au_fs_copy (TCHAR *dst, int maxlen, const char *src) {
#ifdef NO_TRANSLATION
    dst[0] = 0;
    strncpy(dst, src, maxlen);
    return dst;
#else
    gsize read, written;
    gchar *result = g_convert(src, -1, "UTF-8",
            "ISO-8859-1", &read, &written, NULL);
    if (result == NULL) {
        write_log("WARNING: au_fs_copy failed to convert string %s", src);
        dst[0] = '\0';
        return dst;
    }

    strncpy(dst, result, maxlen);
    free(result);
    return dst;
#endif
}

char *ua_fs_copy (char *dst, int maxlen, const TCHAR *src, int defchar) {
#ifdef NO_TRANSLATION
    dst[0] = 0;
    strncpy(dst, src, maxlen);
    return dst;
#else
    char def[] = "?";
    if (defchar < 128) {
        def[0] = defchar;
    }

    gsize read, written;
    gchar *result = g_convert_with_fallback(src, -1, "ISO-8859-1",
            "UTF-8", def, &read, &written, NULL);
    if (result == NULL) {
        write_log("WARNING: ua_fs_copy failed to convert string %s", src);
        dst[0] = '\0';
        return dst;
    }

    strncpy(dst, result, maxlen);
    free(result);
    return dst;
#endif
}


TCHAR* target_expand_environment(const TCHAR* path, TCHAR* out, int maxlen) {
	if (!path)
		return NULL;
	if (out == NULL) {
		return strdup(path);
	} else {
		_tcscpy(out, path);
		return out;
	}
}

bool my_stat (const TCHAR* name, struct mystat* ms) {
    UNIMPLEMENTED();
    return FALSE;
}

int my_readdir(struct my_opendir_s* mod, TCHAR* name) {
    UNIMPLEMENTED();
    return 0;
}

struct my_opendir_s* my_opendir(const TCHAR* name, const TCHAR* mask) {
    UNIMPLEMENTED();
    return NULL;
}

void my_closedir(struct my_opendir_s* mod) {
    UNIMPLEMENTED();
}

/*
int hdf_write_target(struct hardfiledata* hfd, void* buffer, uae_u64 offset, int len) {
    UNIMPLEMENTED();
    return 0;
}
*/

int hdf_write_target(struct hardfiledata* hfd, void* buffer, uae_u64 offset, int len, uint32_t* error) {
    UNIMPLEMENTED();
    return 0;
}

struct my_opendir_s* my_opendir(const TCHAR* name) {
    UNIMPLEMENTED();
    return nullptr; 
}


struct a_inode_struct;

int fsdb_set_file_attrs(a_inode_struct* aino) {
    UNIMPLEMENTED();
    return 0;
}

void fetch_nvrampath(TCHAR* out, int size) {
    UNIMPLEMENTED();
}

void fetch_configurationpath(TCHAR* out, int size) {
    out[0] = _T('/');
    out[1] = _T('.');
    out[2] = 0;
}





