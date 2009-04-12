#include <stdio.h>
#include <tchar.h>

#include <windows.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "zfile.h"
#include "fsdb.h"
#include "zarchive.h"

TCHAR start_path_exe[MAX_DPATH];
TCHAR start_path_data[MAX_DPATH];
TCHAR sep[] = { FSDB_DIR_SEPARATOR, 0 };

struct uae_prefs currprefs;
static int debug = 1;

#define WRITE_LOG_BUF_SIZE 4096
void write_log (const TCHAR *format, ...)
{
    int count;
    TCHAR buffer[WRITE_LOG_BUF_SIZE];
    va_list parms;
    va_start (parms, format);
    if (debug) {
	count = _vsntprintf (buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	_tprintf (buffer);
    }
    va_end (parms);
}

void gui_message (const TCHAR *format, ...)
{
}

int uaerand (void)
{
    return rand ();
}

static int pattern_match (const TCHAR *str, const TCHAR *pattern)
{
    enum State {
        Exact,        // exact match
        Any,        // ?
        AnyRepeat    // *
    };

    const TCHAR *s = str;
    const TCHAR *p = pattern;
    const TCHAR *q = 0;
    int state = 0;

    int match = TRUE;
    while (match && *p) {
        if (*p == '*') {
            state = AnyRepeat;
            q = p+1;
        } else if (*p == '?') state = Any;
        else state = Exact;

        if (*s == 0) break;

        switch (state) {
            case Exact:
                match = *s == *p;
                s++;
                p++;
                break;

            case Any:
                match = TRUE;
                s++;
                p++;
                break;

            case AnyRepeat:
                match = TRUE;
                s++;

		if (*s == *q){
		    // make a recursive call so we don't match on just a single character
		    if (pattern_match(s,q) == TRUE) {
			p++;
		    }
		}
                break;
        }
    }

    if (state == AnyRepeat) return (*s == *q);
    else if (state == Any) return (*s == *p);
    else return match && (*s == *p);
} 




static void geterror (void)
{
    TCHAR *err = zfile_geterror();
    if (!err)
	return;
    _tprintf (L"%s\n", err);
}

static const TCHAR *prots = L"HSPARWED";

struct arcdir {
    TCHAR *name;
    int isdir;
    uae_u32 flags;
    uae_u64 size;
    TCHAR *comment;
    uae_u32 crc32;
    __time64_t dt;
    int parent, nextlevel;
};

static struct arcdir **filelist;

static void dolist (struct arcdir **filelist, struct arcdir *adp, int entries, int parent, int level)
{
    int ii, i;

    for (ii = 0; ii < 2; ii++) {
	for (i = 0; i < entries; i++) {
	    struct arcdir *ad = filelist[i];
	    int j;
	    TCHAR protflags[9];
	    TCHAR dates[32];
	    int flags;
	    struct tm *dt;

	    if (ad->parent != parent)
		continue;

	    if ((ii == 0 && ad->isdir) || (ii == 1 && !ad->isdir)) {

		flags = ad->flags;

		if (flags >= 0) {
		    for (j = 0; j < 8; j++) {
			protflags[j] = '-';
			if (flags & (1 << (7 - j)))
			    protflags[j] = prots[j];
		    }
		    protflags[j] = 0;
		} else {
    		    _tcscpy (protflags, L"--------");
		}

		if (ad->dt > 0) {
		    dt = _gmtime64 (&ad->dt);
		    _tcsftime (dates, sizeof (dates) / sizeof (TCHAR), L"%Y/%m/%d %H:%M:%S", dt);
		} else {
		    _tcscpy (dates, L"-------------------");
		}

		for (j = 0; j < level; j++)
		    _tprintf (L" ");
		if (ad->isdir > 0)
		    _tprintf (L"     [DIR] %s %s          %s\n", protflags, dates, ad->name);
		else if (ad->isdir < 0)
		    _tprintf (L"    [VDIR] %s %s          %s\n", protflags, dates, ad->name);
		else
		    _tprintf (L"%10I64d %s %s %08X %s\n", ad->size, protflags, dates, ad->crc32, ad->name);
		if (ad->comment)
		    _tprintf (L" \"%s\"\n", ad->comment);
		if (ad->nextlevel >= 0) {
		    level++;
		    dolist (filelist, adp, entries, ad - adp, level);
		    level--;
		}

	    }
	}
    }
}

static int parentid = -1, subdirid;
static int maxentries = 10000, entries;

static int unlist2 (struct arcdir *adp, const TCHAR *src, int all)
{
    struct zvolume *zv;
    void *h;
    int i;
    TCHAR p[MAX_DPATH];
    TCHAR fn[MAX_DPATH];
    struct arcdir *ad;
   
    zv = zfile_fopen_archive_root (src);
    if (zv == NULL) {
	geterror();
	_tprintf (L"Couldn't open archive '%s'\n", src);
	return 0;
    }
    h = zfile_opendir_archive (src);
    if (!h) {
	_tcscpy (p, src);
	_tcscat (p, L".DIR");
	h = zfile_opendir_archive (src);
	if (!h) {
	    geterror();
	    _tprintf (L"Couldn't open directory '%s'\n", src);
	    return 0;
	}
    }

    while (zfile_readdir_archive (h, fn)) {
        struct _stat64 st; 
        int isdir;
	uae_u32 flags;
        TCHAR *comment;
	struct zfile *zf;
	uae_u32 crc32 = 0;
	int nextdir = -1;

        _tcscpy (p, src);
        _tcscat (p, sep);
        _tcscat (p, fn);
	if (!zfile_stat_archive (p, &st)) {
	    st.st_size = -1;
	    st.st_mtime = 0;
	}
	isdir = 0;
	flags = 0;
	comment = 0;
	zfile_fill_file_attrs_archive (p, &isdir, &flags, &comment);
	flags ^= 15;
	if (!isdir) {
	    zf = zfile_open_archive (p, 0);
	    if (zf) {
		crc32 = zfile_crc32 (zf);
	    }
	}

        ad = &adp[entries++];
	ad->isdir = isdir;
	ad->comment = comment;
	ad->flags = flags;
	ad->name = my_strdup (fn);
	ad->size = st.st_size;
	ad->dt = st.st_mtime;
	ad->parent = parentid;
	ad->crc32 = crc32;

	if (isdir && all) {
	    int oldparent = parentid;
	    parentid = ad - adp;
	    nextdir = parentid + 1;
	    unlist2 (adp, p, all);
	    parentid = oldparent;
	}

	ad->nextlevel = nextdir;

	if (entries >= maxentries)
	    break;
    }
    if (parentid >= 0)
	return 1;

    filelist = xmalloc (entries * sizeof (struct arcdir*));
    for (i = 0; i < entries; i++) {
	filelist[i] = &adp[i];
    }

    // bubblesort is the winner!
    for (i = 0; i < entries; i++) {
	int j;
	for (j = i + 1; j < entries; j++) {
	    int diff = _tcsicmp (filelist[i]->name, filelist[j]->name);
	    if (diff > 0) {
		struct arcdir *tmp;
		tmp = filelist[i];
		filelist[i] = filelist[j];
		filelist[j] = tmp;
	    }
	}
    }

    dolist (filelist, adp, entries, -1, 0);
    zfile_closedir_archive (h);
    return 1;
}

static int unlist (const TCHAR *src, int all)
{
    struct arcdir *adp;
    adp = xcalloc (sizeof (struct arcdir), maxentries);
    unlist2 (adp, src, all);
    return 1;
}

static void setdate (const TCHAR *src, __time64_t tm)
{
    struct utimbuf ut;
    if (tm) {
	ut.actime = ut.modtime = tm;
	utime (src, &ut);
    }
}

static int found;

static int unpack (const TCHAR *src, const TCHAR *filename, const TCHAR *dst, int out, int all, int level)
{
    void *h;
    struct zvolume *zv;
    int ret;
    uae_u8 *b;
    int size;
    TCHAR fn[MAX_DPATH];

    ret = 0;
    zv = zfile_fopen_archive_root (src);
    if (zv == NULL) {
	geterror();
	_tprintf (L"Couldn't open archive '%s'\n", src);
	return 0;
    }
    h = zfile_opendir_archive (src);
    if (!h) {
	geterror();
	_tprintf (L"Couldn't open directory '%s'\n", src);
	return 0;
    }
    while (zfile_readdir_archive (h, fn)) {
	if (all || !_tcsicmp (filename, fn)) {
	    TCHAR tmp[MAX_DPATH];
    	    struct zfile *s, *d;
	    struct _stat64 st;

	    found = 1;
	    _tcscpy (tmp, src);
	    _tcscat (tmp, sep);
	    _tcscat (tmp, fn);
	    if (!zfile_stat_archive (tmp, &st)) {
		_tprintf (L"Couldn't stat '%s'\n", tmp);
		continue;
	    }
	    if (dst == NULL || all)
		dst = fn;
	    if (st.st_mode) {
		if (all > 0)
		    continue;
		if (all < 0) {
		    TCHAR oldcur[MAX_DPATH];
		    my_mkdir (fn);
		    my_setcurrentdir (fn, oldcur);
		    unpack (tmp, fn, dst, out, all, 1);
		    my_setcurrentdir (oldcur, NULL);
		    setdate (dst, st.st_mtime);
		    continue;
		}
		_tprintf (L"Directory extraction not yet supported\n");
		return 0;
	    }

	    s = zfile_open_archive (tmp, 0);
	    if (!s) {
		geterror();
		_tprintf (L"Couldn't open '%s' for reading\n", src);
		continue;
	    }
	    zfile_fseek (s, 0, SEEK_END);
	    size = zfile_ftell (s);
	    zfile_fseek (s, 0, SEEK_SET);
	    b = xcalloc (size, 1);
	    if (b) {
		if (zfile_fread (b, size, 1, s) == 1) {
		    if (out) {
			_tprintf (L"\n");
			fwrite (b, size, 1, stdout);
		    } else {
			d = zfile_fopen (dst, L"wb", 0);
			if (d) {
			    if (zfile_fwrite (b, size, 1, d) == 1) {
				ret = 1;
				_tprintf (L"%s extracted, %d bytes\n", dst, size);
			    }
			    zfile_fclose (d);
			    setdate (dst, st.st_mtime);
			}
		    }
		}
		xfree (b);
	    }
	    zfile_fclose (s);
	    if (!all)
		break;
	}
    }
    geterror ();
    if (!found && !level) {
	_tprintf (L"'%s' not found\n", fn);
    }
    return ret;
}

static int unpack2 (const TCHAR *src, const TCHAR *match, int level)
{
    void *h;
    struct zvolume *zv;
    int ret;
    uae_u8 *b;
    int size;
    TCHAR fn[MAX_DPATH];

    ret = 0;
    zv = zfile_fopen_archive_root (src);
    if (zv == NULL) {
	geterror();
	_tprintf (L"Couldn't open archive '%s'\n", src);
	return 0;
    }
    h = zfile_opendir_archive (src);
    if (!h) {
	geterror();
	_tprintf (L"Couldn't open directory '%s'\n", src);
	return 0;
    }
    while (zfile_readdir_archive (h, fn)) {
        TCHAR tmp[MAX_DPATH];
        TCHAR *dst;
	struct zfile *s, *d;
	int isdir, flags;

	_tcscpy (tmp, src);
	_tcscat (tmp, sep);
	_tcscat (tmp, fn);
	zfile_fill_file_attrs_archive (tmp, &isdir, &flags, NULL);
	if (isdir) {
	    TCHAR *p = _tcsstr (fn, L".DIR");
	    if (isdir == ZNODE_VDIR && p && _tcslen (p) == 4) {
		p[0] = 0;
		if (pattern_match (fn, match))
		    continue;
		p[0] = '.';
	    }
	    unpack2 (tmp, match, 1);
	    continue;
	}
	
	if (pattern_match (fn, match)) {
	    struct _stat64 st;

	    if (!zfile_stat_archive (tmp, &st)) {
		st.st_mtime = -1;
	    }
	    found = 1;
	    dst = fn;
	    s = zfile_open_archive (tmp, 0);
	    if (!s) {
		geterror();
		_tprintf (L"Couldn't open '%s' for reading\n", src);
		continue;
	    }
	    zfile_fseek (s, 0, SEEK_END); 
	    size = zfile_ftell (s);
	    zfile_fseek (s, 0, SEEK_SET);
	    b = xcalloc (size, 1);
	    if (b) {
		if (zfile_fread (b, size, 1, s) == 1) {
		    d = zfile_fopen (dst, L"wb", 0);
		    if (d) {
		        if (zfile_fwrite (b, size, 1, d) == 1) {
			    ret = 1;
			    _tprintf (L"%s extracted, %d bytes\n", dst, size);
			}
			zfile_fclose (d);
			setdate (dst, st.st_mtime);
		    }
		}
		xfree (b);
	    }
	    zfile_fclose (s);
	}
    }
    geterror ();
    if (!found && !level) {
	_tprintf (L"'%s' not matched\n", match);
    }
    return ret;
}

static int scanpath (TCHAR *src, TCHAR *outpath)
{
    struct zvolume *zv;
    void *h;
    TCHAR fn[MAX_DPATH];

    zv = zfile_fopen_archive_root (src);
    if (zv == NULL) {
	geterror();
	_tprintf (L"Couldn't open archive '%s'\n", src);
	return 0;
    }
    h = zfile_opendir_archive (src);
    if (!h) {
	geterror();
	_tprintf (L"Couldn't open directory '%s'\n", src);
	return 0;
    }
    while (zfile_readdir_archive (h, fn)) {
        TCHAR tmp[MAX_DPATH];
	int isdir, flags;
        _tcscpy (tmp, src);
	_tcscat (tmp, sep);
	_tcscat (tmp, fn);
	zfile_fill_file_attrs_archive (tmp, &isdir, &flags, NULL);
	if (isdir == ZNODE_VDIR) {
	    _tcscpy (outpath, tmp);
	    scanpath (tmp, outpath);
	    break;
	}
    }
    return 1;
}

int wmain (int argc, wchar_t *argv[], wchar_t *envp[])
{
    int ok = 0, i;
    int list = 0, xtract = 0, extract = 0;
    int out = 0, all = 0;
    TCHAR path[MAX_DPATH], tmppath[MAX_DPATH];
    int used[32] = { 0 };
    TCHAR *parm2 = NULL;
    TCHAR *parm3 = NULL;
    TCHAR *match = NULL;
    
    for (i = 0; i < argc && i < 32; i++) {
	if (!_tcsicmp (argv[i], L"o")) {
	    out = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"-o")) {
	    out = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"l")) {
	    list = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"-l")) {
	    list = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"x")) {
	    xtract = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"-x")) {
	    xtract = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"e")) {
	    extract = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"-e")) {
	    extract = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"*")) {
	    all = 1;
	    used[i] = 1;
	}
	if (!_tcsicmp (argv[i], L"**")) {
	    all = -1;
	    used[i] = 1;
	}
	if (!used[i] && (_tcschr (argv[i], '*') || _tcschr (argv[i], '?'))) {
	    extract = 1;
	    match = argv[i];
	    used[i] = 1;
	}
    }
    for (i = 1; i < argc && i < 32; i++) {
	if (!used[i]) {
	    GetFullPathName (argv[i], MAX_DPATH, path, NULL);
	    used[i] = 1;
	    break;
	}
    }
    for (i = 1; i < argc && i < 32; i++) {
	if (!used[i]) {
	    parm2 = argv[i];
	    used[i] = 1;
	    break;
	}
    }
    for (i = 1; i < argc && i < 32; i++) {
	if (!used[i]) {
	    parm3 = argv[i];
	    used[i] = 1;
	    break;
	}
    }

//    _tcscpy (tmppath, path);
//    scanpath (tmppath, path);

    if (match) {
	unpack2 (path, match, 0);
	ok = 1;
    } else if (!parm2 && all > 0) {
	unpack2 (path, L"*", 0);
	ok = 1;
    } else if (extract && parm2) {
	unpack2 (path, parm2, 0);
	ok = 1;
    } else if (argc == 2 || (argc > 2 && list)) {
	unlist (path, all);
	ok = 1;
    } else if (((xtract && parm2) || all || (argc >= 3 && parm2)) && !out) {
	unpack (path, parm2, parm3, 0, all, 0);
	ok = 1;
    } else if (parm2 && (argc >= 4 && out)) {
	unpack (path, parm2, parm3, 1, all, 0);
	ok = 1;
    }
    if (!ok) {
	_tprintf (L"UAE unpacker uaeunp 0.5c by Toni Wilen (c)2009\n");
	_tprintf (L"\n");
	_tprintf (L"List: \"uaeunp (-l) <path>\"\n");
	_tprintf (L"List all recursively: \"uaeunp -l <path> **\"\n");
	_tprintf (L"Extract to file: \"uaeunp (-x) <path> <filename> [<dst name>]\"\n");
	_tprintf (L"Extract all (single directory): \"uaeunp (-x) <path> *\"\n");
	_tprintf (L"Extract all (recursively): \"uaeunp (-x) <path> **\"\n");
	_tprintf (L"Extract all (recursively, current dir): \"uaeunp -e <path> <match string>\"\n");
	_tprintf (L"Output to console: \"uaeunp (-x) -o <path> <filename>\"\n");
	_tprintf (L"\n");
	_tprintf (L"Supported disk image formats:\n");
	_tprintf (L" ADF and HDF (OFS/FFS/SFS/SFS2), DMS, encrypted DMS, IPF, FDI, DSQ, WRP\n");
	_tprintf (L"Supported archive formats:\n");
	_tprintf (L" 7ZIP, LHA, LZX, RAR (unrar.dll), ZIP, ArchiveAccess.DLL\n");
	_tprintf (L"Miscellaneous formats:\n");
	_tprintf (L" RDB partition table, GZIP\n");


    }
    return 0;
}

/*
    0.5:

    - adf protection flags fixed
    - sfs support added
    - >512 block sizes supported (rdb hardfiles only)

    0.5b:

    - SFS file extraction fixed
    - SFS2 supported
    - block size autodetection implemented (if non-rdb hardfile)

    0.5c:

    - rdb_dump.dat added to rdb hardfiles, can be used to dump/backup rdb blocks

*/