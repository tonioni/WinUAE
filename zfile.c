 /*
  * UAE - The Un*x Amiga Emulator
  *
  * routines to handle compressed file automatically
  *
  * (c) 1996 Samuel Devulder, Tim Gunn
  *     2002-2007 Toni Wilen
  */

#define ZLIB_WINAPI
#define RECURSIVE_ARCHIVES 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "zfile.h"
#include "disk.h"
#include "gui.h"
#include "crc32.h"
#include "fsdb.h"
#include "fsusage.h"
#include "zarchive.h"

#include "archivers/zip/unzip.h"
#include "archivers/dms/pfile.h"
#include "archivers/wrp/warp.h"

static struct zfile *zlist = 0;

const TCHAR *uae_archive_extensions[] = { L"zip", L"rar", L"7z", L"lha", L"lzh", L"lzx", NULL };

static struct zfile *zfile_create (void)
{
    struct zfile *z;

    z = xmalloc (sizeof *z);
    if (!z)
	return 0;
    memset (z, 0, sizeof *z);
    z->next = zlist;
    zlist = z;
    return z;
}

static void zfile_free (struct zfile *f)
{
    if (f->f)
	fclose (f->f);
    if (f->deleteafterclose) {
	_wunlink (f->name);
	write_log (L"deleted temporary file '%s'\n", f->name);
    }
    xfree (f->name);
    xfree (f->data);
    xfree (f);
}

void zfile_exit (void)
{
    struct zfile *l;
    while ((l = zlist)) {
	zlist = l->next;
	zfile_free (l);
    }
}

void zfile_fclose (struct zfile *f)
{
    struct zfile *pl = NULL;
    struct zfile *l  = zlist;
    struct zfile *nxt;

    if (!f)
	return;
    while (l != f) {
	if (l == 0) {
	    write_log (L"zfile: tried to free already freed filehandle!\n");
	    return;
	}
	pl = l;
	l = l->next;
    }
    if (l)
	nxt = l->next;
    zfile_free (f);
    if (l == 0)
	return;
    if(!pl)
	zlist = nxt;
    else
	pl->next = nxt;
}

static uae_u8 exeheader[]={ 0x00,0x00,0x03,0xf3,0x00,0x00,0x00,0x00 };
static TCHAR *diskimages[] = { L"adf", L"adz", L"ipf", L"fdi", L"dms", L"wrp", L"dsq", 0 };
int zfile_gettype (struct zfile *z)
{
    uae_u8 buf[8];
    TCHAR *ext;

    if (!z || !z->name)
	return ZFILE_UNKNOWN;
    ext = _tcsrchr (z->name, '.');
    if (ext != NULL) {
	int i;
	ext++;
	for (i = 0; diskimages[i]; i++) {
	    if (strcasecmp (ext, diskimages[i]) == 0)
		return ZFILE_DISKIMAGE;
	}
	if (strcasecmp (ext, L"roz") == 0)
	    return ZFILE_ROM;
	if (strcasecmp (ext, L"uss") == 0)
	    return ZFILE_STATEFILE;
	if (strcasecmp (ext, L"rom") == 0)
	    return ZFILE_ROM;
	if (strcasecmp (ext, L"key") == 0)
	    return ZFILE_KEY;
	if (strcasecmp (ext, L"nvr") == 0)
	    return ZFILE_NVR;
	if (strcasecmp (ext, L"uae") == 0)
	    return ZFILE_CONFIGURATION;
    }
    memset (buf, 0, sizeof (buf));
    zfile_fread (buf, 8, 1, z);
    zfile_fseek (z, -8, SEEK_CUR);
    if (!memcmp (buf, exeheader, sizeof(buf)))
	return ZFILE_DISKIMAGE;
    if (!memcmp (buf, "RDSK", 4))
	return ZFILE_HDFRDB;
    if (!memcmp (buf, "DOS", 3))
	return ZFILE_HDF;
    if (ext != NULL) {
	if (strcasecmp (ext, L"hdf") == 0)
	    return ZFILE_HDF;
	if (strcasecmp (ext, L"hdz") == 0)
	    return ZFILE_HDF;
    }
    return ZFILE_UNKNOWN;
}

static struct zfile *zuncompress (struct zfile *z, int);

struct zfile *zfile_gunzip (struct zfile *z)
{
    uae_u8 header[2 + 1 + 1 + 4 + 1 + 1];
    z_stream zs;
    int i, size, ret, first;
    uae_u8 flags;
    uae_s64 offset;
    TCHAR name[MAX_DPATH];
    uae_u8 buffer[8192];
    struct zfile *z2;
    uae_u8 b;

    _tcscpy (name, z->name);
    memset (&zs, 0, sizeof (zs));
    memset (header, 0, sizeof (header));
    zfile_fread (header, sizeof (header), 1, z);
    flags = header[3];
    if (header[0] != 0x1f && header[1] != 0x8b)
	return z;
    if (flags & 2) /* multipart not supported */
	return z;
    if (flags & 32) /* encryption not supported */
	return z;
    if (flags & 4) { /* skip extra field */
	zfile_fread (&b, 1, 1, z);
	size = b;
	zfile_fread (&b, 1, 1, z);
	size |= b << 8;
	zfile_fseek (z, size + 2, SEEK_CUR);
    }
    if (flags & 8) { /* get original file name */
	i = 0;
	do {
	    zfile_fread (name + i, 1, 1, z);
	} while (i < MAX_DPATH - 1 && name[i++]);
	name[i] = 0;
    }
    if (flags & 16) { /* skip comment */
	i = 0;
	do {
	    b = 0;
	    zfile_fread (&b, 1, 1, z);
	} while (b);
    }
    offset = zfile_ftell (z);
    zfile_fseek (z, -4, SEEK_END);
    zfile_fread (&b, 1, 1, z);
    size = b;
    zfile_fread (&b, 1, 1, z);
    size |= b << 8;
    zfile_fread (&b, 1, 1, z);
    size |= b << 16;
    zfile_fread (&b, 1, 1, z);
    size |= b << 24;
    if (size < 8 || size > 64 * 1024 * 1024) /* safety check */
	return z;
    zfile_fseek (z, offset, SEEK_SET);
    z2 = zfile_fopen_empty (name, size);
    if (!z2)
	return z;
    zs.next_out = z2->data;
    zs.avail_out = size;
    first = 1;
    do {
	zs.next_in = buffer;
	zs.avail_in = zfile_fread (buffer, 1, sizeof (buffer), z);
	if (first) {
	    if (inflateInit2_ (&zs, -MAX_WBITS, ZLIB_VERSION, sizeof(z_stream)) != Z_OK)
		break;
	    first = 0;
	}
	ret = inflate (&zs, 0);
    } while (ret == Z_OK);
    inflateEnd (&zs);
    if (ret != Z_STREAM_END || first != 0) {
	zfile_fclose (z2);
	return z;
    }
    zfile_fclose (z);
    return z2;
}

static struct zfile *dsq (struct zfile *z)
{
    struct zfile *zo;

    zo = zfile_fopen_empty (L"zipped.dsq", 1760 * 512);
    if (zo) {
	struct zvolume *zv = archive_directory_lzx (z);
	if (zv) {
	    if (zv->root.child) {
		struct zfile *zi = archive_access_lzx (zv->root.child);
		if (zi && zi->data && zi->size > 1000) {
		    uae_u8 *buf = zi->data;
		    if (!memcmp (buf, "PKD\x13", 4) || !memcmp (buf, "PKD\x11", 4)) {
			int sectors = buf[18];
			int heads = buf[15];
			int blocks = (buf[6] << 8) | buf[7];
			int blocksize = (buf[10] << 8) | buf[11];
			if (blocksize == 512 && blocks == 1760 && sectors == 22 && heads == 2) {
			    int off = buf[3] == 0x13 ? 52 : 32;
			    int i;
			    for (i = 0; i < blocks / (sectors / heads); i++) {
				zfile_fwrite (zi->data + off, sectors * blocksize / heads, 1, zo);
				off += sectors * (blocksize + 16) / heads;
			    }
			    zfile_fclose_archive (zv);
			    zfile_fclose (z);
			    return zo;
			}
		    }
		}
		zfile_fclose (zi);
	    }
	}
	zfile_fclose (zo);
    }
    return z;
}

static struct zfile *wrp (struct zfile *z)
{
    return unwarp (z);
}

static struct zfile *dms (struct zfile *z)
{
    int ret;
    struct zfile *zo;

    zo = zfile_fopen_empty (L"undms.adf", 1760 * 512);
    if (!zo)
	return z;
    ret = DMS_Process_File (z, zo, CMD_UNPACK, OPT_VERBOSE, 0, 0);
    if (ret == NO_PROBLEM || ret == DMS_FILE_END) {
	zfile_fclose (z);
	return zo;
    }
    return z;
}

const TCHAR *uae_ignoreextensions[] =
    { L".gif", L".jpg", L".png", L".xml", L".pdf", L".txt", 0 };
const TCHAR *uae_diskimageextensions[] =
    { L".adf", L".adz", L".ipf", L".fdi", L".exe", L".dms", L".wrp", L".dsq", 0 };

int zfile_is_ignore_ext (const TCHAR *name)
{
    int i;

    for (i = 0; uae_ignoreextensions[i]; i++) {
	if (_tcslen(name) > _tcslen (uae_ignoreextensions[i]) &&
	    !strcasecmp (uae_ignoreextensions[i], name + _tcslen (name) - _tcslen (uae_ignoreextensions[i])))
		return 1;
    }
    return 0;
}

int zfile_isdiskimage (const TCHAR *name)
{
    int i;

    i = 0;
    while (uae_diskimageextensions[i]) {
	if (_tcslen (name) > 3 && !strcasecmp (name + _tcslen (name) - 4, uae_diskimageextensions[i]))
	    return 1;
	i++;
    }
    return 0;
}


static const TCHAR *plugins_7z[] = { L"7z", L"rar", L"zip", L"lha", L"lzh", L"lzx", NULL };
static const TCHAR *plugins_7z_x[] = { L"7z", L"Rar!", L"MK", NULL, NULL, NULL, NULL };
static const int plugins_7z_t[] = { ArchiveFormat7Zip, ArchiveFormatRAR, ArchiveFormatZIP, ArchiveFormatLHA, ArchiveFormatLHA, ArchiveFormatLZX };

int iszip (struct zfile *z)
{
    TCHAR *name = z->name;
    TCHAR *ext = _tcsrchr (name, '.');
    uae_u8 header[7];
    int i;

    if (!ext)
	return 0;
    memset (header, 0, sizeof (header));
    zfile_fseek (z, 0, SEEK_SET);
    zfile_fread (header, sizeof (header), 1, z);
    zfile_fseek (z, 0, SEEK_SET);
    if (!strcasecmp (ext, L".zip") && header[0] == 'P' && header[1] == 'K')
	return ArchiveFormatZIP;
    if (!strcasecmp (ext, L".7z") && header[0] == '7' && header[1] == 'z')
	return ArchiveFormat7Zip;
    if (!strcasecmp (ext, L".rar") && header[0] == 'R' && header[1] == 'a' && header[2] == 'r' && header[3] == '!')
	return ArchiveFormatRAR;
    if ((!strcasecmp (ext, L".lha") || !strcasecmp (ext, L".lzh")) && header[2] == '-' && header[3] == 'l' && header[4] == 'h' && header[6] == '-')
	return ArchiveFormatLHA;
    if (!strcasecmp (ext, L".lzx") && header[0] == 'L' && header[1] == 'Z' && header[2] == 'X')
	return ArchiveFormatLZX;
#if defined(ARCHIVEACCESS)
    for (i = 0; plugins_7z_x[i]; i++) {
	if (plugins_7z_x[i] && !strcasecmp (ext + 1, plugins_7z[i]) &&
	    !memcmp (header, plugins_7z_x[i], _tcslen (plugins_7z_x[i])))
		return plugins_7z_t[i];
    }
#endif
    return 0;
}

static struct zfile *zuncompress (struct zfile *z, int dodefault)
{
    TCHAR *name = z->name;
    TCHAR *ext = _tcsrchr (name, '.');
    uae_u8 header[7];
    int i;

    if (ext != NULL) {
	ext++;
	if (strcasecmp (ext, L"7z") == 0)
	     return archive_access_select (z, ArchiveFormat7Zip, dodefault);
	if (strcasecmp (ext, L"zip") == 0)
	     return archive_access_select (z, ArchiveFormatZIP, dodefault);
	if (strcasecmp (ext, L"lha") == 0 || strcasecmp (ext, L"lzh") == 0)
	     return archive_access_select (z, ArchiveFormatLHA, dodefault);
	if (strcasecmp (ext, L"lzx") == 0)
	     return archive_access_select (z, ArchiveFormatLZX, dodefault);
	if (strcasecmp (ext, L"rar") == 0)
	     return archive_access_select (z, ArchiveFormatRAR, dodefault);
	if (strcasecmp (ext, L"gz") == 0)
	     return zfile_gunzip (z);
	if (strcasecmp (ext, L"adz") == 0)
	     return zfile_gunzip (z);
	if (strcasecmp (ext, L"roz") == 0)
	     return zfile_gunzip (z);
	if (strcasecmp (ext, L"hdz") == 0)
	     return zfile_gunzip (z);
	if (strcasecmp (ext, L"dms") == 0)
	     return dms (z);
	if (strcasecmp (ext, L"wrp") == 0)
	     return wrp (z);
	if (strcasecmp (ext, L"dsq") == 0)
	     return dsq (z);
#if defined(ARCHIVEACCESS)
	for (i = 0; plugins_7z_x[i]; i++) {
	    if (strcasecmp (ext, plugins_7z[i]) == 0)
		return archive_access_arcacc_select (z, plugins_7z_t[i]);
	}
#endif
	memset (header, 0, sizeof (header));
	zfile_fseek (z, 0, SEEK_SET);
	zfile_fread (header, sizeof (header), 1, z);
	zfile_fseek (z, 0, SEEK_SET);
	if (header[0] == 0x1f && header[1] == 0x8b)
	    return zfile_gunzip (z);
	if (header[0] == 'P' && header[1] == 'K')
	     return archive_access_select (z, ArchiveFormatZIP, dodefault);
	if (header[0] == 'R' && header[1] == 'a' && header[2] == 'r' && header[3] == '!')
	     return archive_access_select (z, ArchiveFormatRAR, dodefault);
	if (header[0] == 'D' && header[1] == 'M' && header[2] == 'S' && header[3] == '!')
	    return dms (z);
	if (header[0] == 'L' && header[1] == 'Z' && header[2] == 'X')
	     return archive_access_select (z, ArchiveFormatLZX, dodefault);
	if (header[2] == '-' && header[3] == 'l' && header[4] == 'h' && header[6] == '-')
	     return archive_access_select (z, ArchiveFormatLHA, dodefault);
    }
    return z;
}


#ifdef SINGLEFILE
extern uae_u8 singlefile_data[];

static struct zfile *zfile_opensinglefile(struct zfile *l)
{
    uae_u8 *p = singlefile_data;
    int size, offset;
    TCHAR tmp[256], *s;

    _tcscpy (tmp, l->name);
    s = tmp + _tcslen (tmp) - 1;
    while (*s != 0 && *s != '/' && *s != '\\') s--;
    if (s > tmp)
	s++;
    write_log (L"loading from singlefile: '%s'\n", tmp);
    while (*p++);
    offset = (p[0] << 24)|(p[1] << 16)|(p[2] << 8)|(p[3] << 0);
    p += 4;
    for (;;) {
	size = (p[0] << 24)|(p[1] << 16)|(p[2] << 8)|(p[3] << 0);
	if (!size)
	    break;
	if (!strcmpi (tmp, p + 4)) {
	    l->data = singlefile_data + offset;
	    l->size = size;
	    write_log (L"found, size %d\n", size);
	    return l;
	}
	offset += size;
	p += 4;
	p += _tcslen (p) + 1;
    }
    write_log (L"not found\n");
    return 0;
}
#endif

struct zfile *zfile_fopen_nozip (const TCHAR *name, const TCHAR *mode)
{
    struct zfile *l;
    FILE *f;

    if(*name == '\0')
	return NULL;
    l = zfile_create ();
    l->name = my_strdup (name);
    f = _tfopen (name, mode);
    if (!f) {
	zfile_fclose (l);
	return 0;
    }
    l->f = f;
    return l;
}


static struct zfile *openzip (const TCHAR *pname)
{
    int i, j;
    TCHAR v;
    TCHAR name[MAX_DPATH];
    TCHAR zippath[MAX_DPATH];

    zippath[0] = 0;
    _tcscpy (name, pname);
    i = _tcslen (name) - 2;
    while (i > 0) {
	if (name[i] == '/' || name[i] == '\\' && i > 4) {
	    v = name[i];
	    name[i] = 0;
	    for (j = 0; plugins_7z[j]; j++) {
		int len = _tcslen (plugins_7z[j]);
		if (name[i - len - 1] == '.' && !strcasecmp (name + i - len, plugins_7z[j])) {
		    struct zfile *f = zfile_fopen_nozip (name, L"rb");
		    if (f) {
			f->zipname = my_strdup (name + i + 1);
			return f;
		    }
		    break;
		}
	    }
	    name[i] = v;
	}
	i--;
    }
    return 0;
}

static struct zfile *zfile_fopen_2 (const TCHAR *name, const TCHAR *mode)
{
    struct zfile *l;
    FILE *f;

    if(*name == '\0')
	return NULL;
#ifdef SINGLEFILE
    if (zfile_opensinglefile (l))
	return l;
#endif
    l = openzip (name);
    if (l) {
	if (_tcsicmp (mode, L"rb") && _tcsicmp (mode, L"r")) {
	    zfile_fclose (l);
	    return 0;
	}
    } else {
	l = zfile_create ();
	l->name = my_strdup (name);
	if (!_tcsicmp (mode, L"r")) {
	    f = my_opentext (l->name);
	    l->textmode = 1;
	} else {
	    f = _tfopen (l->name, mode);
	}
	if (!f) {
	    zfile_fclose (l);
	    return 0;
	}
	l->f = f;
    }
    return l;
}

#ifdef _WIN32
#include "win32.h"

#define AF L"%AMIGAFOREVERDATA%"

static void manglefilename (TCHAR *out, const TCHAR *in)
{
    out[0] = 0;
    if (!strncasecmp (in, AF, _tcslen (AF)))
	_tcscpy (out, start_path_data);
    if ((in[0] == '/' || in[0] == '\\') || (_tcslen(in) > 3 && in[1] == ':' && in[2] == '\\'))
	out[0] = 0;
    _tcscat (out, in);
}
#else
static void manglefilename(TCHAR *out, const TCHAR *in)
{
    _tcscpy(out, in);
}
#endif

int zfile_zopen (const TCHAR *name, zfile_callback zc, void *user)
{
    struct zfile *l;
    int ztype;
    TCHAR path[MAX_DPATH];

    manglefilename (path, name);
    l = zfile_fopen_2 (path, L"rb");
    if (!l)
	return 0;
    ztype = iszip (l);
    if (ztype == 0)
	zc (l, user);
    else
	archive_access_scan (l, zc, user, ztype);
    zfile_fclose (l);
    return 1;
}

/*
 * fopen() for a compressed file
 */
struct zfile *zfile_fopen (const TCHAR *name, const TCHAR *mode)
{
    int cnt = 10;
    struct zfile *l, *l2;
    TCHAR path[MAX_DPATH];

    manglefilename (path, name);
    l = zfile_fopen_2 (path, mode);
    if (!l)
	return 0;
    l2 = NULL;
    while (cnt-- > 0) {
	l = zuncompress (l, 0);
	if (!l)
	    break;
	zfile_fseek (l, 0, SEEK_SET);
	if (l == l2)
	    break;
	l2 = l;
    }
    return l;
}

struct zfile *zfile_dup (struct zfile *zf)
{
    struct zfile *nzf;
    if (!zf || !zf->data)
	return NULL;
    nzf = zfile_create ();
    nzf->data = xmalloc (zf->size);
    memcpy (nzf->data, zf->data, zf->size);
    nzf->size = zf->size;
    return nzf;
}

int zfile_exists (const TCHAR *name)
{
    TCHAR fname[2000];
    struct zfile *f;

    if (_tcslen (name) == 0)
	return 0;
    manglefilename (fname, name);
    f = openzip (fname);
    if (!f) {
	FILE *f2;
	manglefilename (fname, name);
	if (!my_existsfile (fname))
	    return 0;
	f2 = _tfopen (fname, L"rb");
	if (!f2)
	    return 0;
	fclose (f2);
    }
    zfile_fclose (f);
    return 1;
}

int zfile_iscompressed (struct zfile *z)
{
    return z->data ? 1 : 0;
}

struct zfile *zfile_fopen_empty (const TCHAR *name, uae_u64 size)
{
    struct zfile *l;
    l = zfile_create ();
    l->name = name ? my_strdup (name) : L"";
    if (size) {
	l->data = xcalloc (size, 1);
	l->size = size;
    } else {
	l->data = xcalloc (1, 1);
	l->size = 0;
    }
    return l;
}

struct zfile *zfile_fopen_data (const TCHAR *name, uae_u64 size, uae_u8 *data)
{
    struct zfile *l;
    l = zfile_create ();
    l->name = name ? my_strdup (name) : L"";
    l->data = xmalloc (size);
    l->size = size;
    memcpy (l->data, data, size);
    return l;
}

uae_s64 zfile_ftell (struct zfile *z)
{
    if (z->data)
	return z->seek;
    return _ftelli64 (z->f);
}

uae_s64 zfile_fseek (struct zfile *z, uae_s64 offset, int mode)
{
    if (z->data) {
	int ret = 0;
	switch (mode)
	{
	    case SEEK_SET:
	    z->seek = offset;
	    break;
	    case SEEK_CUR:
	    z->seek += offset;
	    break;
	    case SEEK_END:
	    z->seek = z->size + offset;
	    break;
	}
	if (z->seek < 0) {
	    z->seek = 0;
	    ret = 1;
	}
	if (z->seek > z->size) {
	    z->seek = z->size;
	    ret = 1;
	}
	return ret;
    }
    return _fseeki64 (z->f, offset, mode);
}

size_t zfile_fread  (void *b, size_t l1, size_t l2,struct zfile *z)
{
    if (z->data) {
	if (z->seek + l1 * l2 > z->size) {
	    if (l1)
		l2 = (z->size - z->seek) / l1;
	    else
		l2 = 0;
	    if (l2 < 0)
		l2 = 0;
	}
	memcpy (b, z->data + z->seek, l1 * l2);
	z->seek += l1 * l2;
	return l2;
    }
    return fread (b, l1, l2, z->f);
}

size_t zfile_fwrite (void *b, size_t l1, size_t l2, struct zfile *z)
{
    if (z->data) {
	if (z->seek + l1 * l2 > z->size) {
	    if (l1)
		l2 = (z->size - z->seek) / l1;
	    else
		l2 = 0;
	    if (l2 < 0)
		l2 = 0;
	}
	memcpy (z->data + z->seek, b, l1 * l2);
	z->seek += l1 * l2;
	return l2;
    }
    return fwrite (b, l1, l2, z->f);
}

size_t zfile_fputs (struct zfile *z, TCHAR *s)
{
    char *s2 = ua (s);
    size_t t;
    t = zfile_fwrite (s2, strlen (s2), 1, z);
    xfree (s2);
    return t;
}

TCHAR *zfile_fgets (TCHAR *s, int size, struct zfile *z)
{
    if (z->data) {
	char s2[MAX_DPATH];
	char *p = s2;
	int i;
	for (i = 0; i < size - 1; i++) {
	    if (z->seek == z->size) {
		if (i == 0)
		    return NULL;
		break;
	    }
	    *p = z->data[z->seek++];
	    if (*p == '\n') {
		p++;
		break;
	    }
	    p++;
	}
	*p = 0;
	if (size > strlen (s2) + 1)
	    size = strlen (s2) + 1;
	au_copy (s, size, s2);
	return s + size;
    } else {
	char s2[MAX_DPATH];
	char *s1;
	s1 = fgets (s2, size, z->f);
	if (!s1)
	    return NULL;
	if (size > strlen (s2) + 1)
	    size = strlen (s2) + 1;
	au_copy (s, size, s2);
	return s + size;
    }
}

int zfile_putc (int c, struct zfile *z)
{
    uae_u8 b = (uae_u8)c;
    return zfile_fwrite (&b, 1, 1, z) ? 1 : -1;
}

int zfile_getc (struct zfile *z)
{
    int out = -1;
    if (z->data) {
	if (z->seek < z->size)
	    out = z->data[z->seek++];
    } else {
	out = fgetc (z->f);
    }
    return out;
}

int zfile_ferror (struct zfile *z)
{
    return 0;
}

uae_u8 *zfile_getdata (struct zfile *z, uae_s64 offset, int len)
{
    uae_s64 pos;
    uae_u8 *b;
    if (len < 0)
	len = z->size;
    b = xmalloc (len);
    if (z->data) {
	memcpy (b, z->data + offset, len);
    } else {
	pos = zfile_ftell (z);
	zfile_fseek (z, offset, SEEK_SET);
	zfile_fread (b, len, 1, z);
	zfile_fseek (z, pos, SEEK_SET);
    }
    return b;
}

int zfile_zuncompress (void *dst, int dstsize, struct zfile *src, int srcsize)
{
    z_stream zs;
    int v;
    uae_u8 inbuf[4096];
    int incnt;

    memset (&zs, 0, sizeof(zs));
    if (inflateInit_ (&zs, ZLIB_VERSION, sizeof(z_stream)) != Z_OK)
	return 0;
    zs.next_out = (Bytef*)dst;
    zs.avail_out = dstsize;
    incnt = 0;
    v = Z_OK;
    while (v == Z_OK && zs.avail_out > 0) {
	if (zs.avail_in == 0) {
	    int left = srcsize - incnt;
	    if (left == 0)
		break;
	    if (left > sizeof (inbuf)) left = sizeof (inbuf);
	    zs.next_in = inbuf;
	    zs.avail_in = zfile_fread (inbuf, 1, left, src);
	    incnt += left;
	}
	v = inflate (&zs, 0);
    }
    inflateEnd (&zs);
    return 0;
}

int zfile_zcompress (struct zfile *f, void *src, int size)
{
    int v;
    z_stream zs;
    uae_u8 outbuf[4096];

    memset (&zs, 0, sizeof (zs));
    if (deflateInit_ (&zs, Z_DEFAULT_COMPRESSION, ZLIB_VERSION, sizeof (z_stream)) != Z_OK)
	return 0;
    zs.next_in = (Bytef*)src;
    zs.avail_in = size;
    v = Z_OK;
    while (v == Z_OK) {
	zs.next_out = outbuf;
	zs.avail_out = sizeof (outbuf);
	v = deflate (&zs, Z_NO_FLUSH | Z_FINISH);
	if (sizeof (outbuf) - zs.avail_out > 0)
	    zfile_fwrite (outbuf, 1, sizeof (outbuf) - zs.avail_out, f);
    }
    deflateEnd (&zs);
    return zs.total_out;
}

TCHAR *zfile_getname (struct zfile *f)
{
    return f->name;
}

uae_u32 zfile_crc32 (struct zfile *f)
{
    uae_u8 *p;
    int pos, size;
    uae_u32 crc;

    if (!f)
	return 0;
    if (f->data)
	return get_crc32 (f->data, f->size);
    pos = zfile_ftell (f);
    zfile_fseek (f, 0, SEEK_END);
    size = zfile_ftell (f);
    p = xmalloc (size);
    if (!p)
	return 0;
    memset (p, 0, size);
    zfile_fseek (f, 0, SEEK_SET);
    zfile_fread (p, 1, size, f);
    zfile_fseek (f, pos, SEEK_SET);
    crc = get_crc32 (p, size);
    xfree (p);
    return crc;
}

static struct zvolume *zvolume_list;

static struct znode *znode_alloc (struct znode *parent, const TCHAR *name)
{
    TCHAR fullpath[MAX_DPATH];
    struct znode *zn = xcalloc (sizeof (struct znode), 1);

    _stprintf (fullpath, L"%s%c%s", parent->fullname, FSDB_DIR_SEPARATOR, name);
    zn->fullname = my_strdup (fullpath);
    zn->name = my_strdup (name);
    zn->volume = parent->volume;
    zn->volume->last->next = zn;
    zn->prev = zn->volume->last;
    zn->volume->last = zn;
    return zn;
}

static struct znode *znode_alloc_child (struct znode *parent, const TCHAR *name)
{
    struct znode *zn = znode_alloc (parent, name);

    if (!parent->child) {
	parent->child = zn;
    } else {
	struct znode *pn = parent->child;
	while (pn->sibling)
	    pn = pn->sibling;
	pn->sibling = zn;
    }
    zn->parent = parent;
    return zn;
}
static struct znode *znode_alloc_sibling (struct znode *sibling, const TCHAR *name)
{
    struct znode *zn = znode_alloc (sibling->parent, name);

    if (!sibling->sibling) {
	sibling->sibling = zn;
    } else {
	struct znode *pn = sibling->sibling;
	while (pn->sibling)
	    pn = pn->sibling;
	pn->sibling = zn;
    }
    zn->parent = sibling->parent;
    return zn;
}

static void zvolume_addtolist (struct zvolume *zv)
{
    if (!zv)
	return;
    if (!zvolume_list) {
	zvolume_list = zv;
    } else {
	struct zvolume *v = zvolume_list;
	while (v->next)
	    v = v->next;
	v->next = zv;
    }
}

static struct zvolume *zvolume_alloc_2 (const TCHAR *name, struct zfile *z, unsigned int id, void *handle)
{
    struct zvolume *zv = xcalloc (sizeof (struct zvolume), 1);
    struct znode *root;
    uae_s64 pos;

    root = &zv->root;
    zv->last = root;
    zv->archive = z;
    zv->handle = handle;
    zv->id = id;
    zv->blocks = 4;
    root->volume = zv;
    root->name = my_strdup (name);
    root->fullname = my_strdup (name);
    if (z) {
	pos = zfile_ftell (z);
	zfile_fseek (z, 0, SEEK_END);
	zv->archivesize = zfile_ftell (z);
	zfile_fseek (z, pos, SEEK_SET);
    }
    return zv;
}
struct zvolume *zvolume_alloc (struct zfile *z, unsigned int id, void *handle)
{
    return zvolume_alloc_2 (zfile_getname (z), z, id, handle);
}
struct zvolume *zvolume_alloc_empty (const TCHAR *name)
{
    return zvolume_alloc_2(name, 0, 0, 0);
}

static struct zvolume *get_zvolume (const TCHAR *path)
{
    struct zvolume *zv = zvolume_list;
    while (zv) {
	TCHAR *s = zfile_getname (zv->archive);
	if (_tcslen (path) >= _tcslen(s) && !memcmp (path, s, _tcslen(s)))
	    return zv;
	zv = zv->next;
    }
    return NULL;
}

static struct zvolume *zfile_fopen_archive_ext (struct zfile *zf)
{
    struct zvolume *zv = NULL;
    TCHAR *ext = _tcsrchr (zfile_getname(zf), '.');

    if (ext != NULL) {
	ext++;
	if (strcasecmp (ext, L"lha") == 0 || strcasecmp (ext, L"lzh") == 0)
	     zv = archive_directory_lha (zf);
	if (strcasecmp (ext, L"zip") == 0)
	     zv = archive_directory_zip (zf);
	if (strcasecmp (ext, L"7z") == 0)
	     zv = archive_directory_7z (zf);
	if (strcasecmp (ext, L"lzx") == 0)
	     zv = archive_directory_lzx (zf);
	if (strcasecmp (ext, L"rar") == 0)
	     zv = archive_directory_rar (zf);
    }
    return zv;
}


struct zvolume *zfile_fopen_archive_data (struct zfile *zf)
{
    struct zvolume *zv = NULL;
    uae_u8 header[7];

    memset (header, 0, sizeof (header));
    zfile_fread (header, sizeof (header), 1, zf);
    zfile_fseek (zf, 0, SEEK_SET);
    if (header[0] == 'P' && header[1] == 'K')
	 zv = archive_directory_zip (zf);
    if (header[0] == 'R' && header[1] == 'a' && header[2] == 'r' && header[3] == '!')
	 zv = archive_directory_rar (zf);
    if (header[0] == 'L' && header[1] == 'Z' && header[2] == 'X')
	 zv = archive_directory_lzx (zf);
    if (header[2] == '-' && header[3] == 'l' && header[4] == 'h' && header[6] == '-')
	 zv = archive_directory_lha (zf);
    return zv;
}

static struct znode *get_znode (struct zvolume *zv, const TCHAR *ppath);

static int zfile_fopen_archive_recurse (struct zvolume *zv)
{
    struct znode *zn;
    int i, added;

    added = 0;
    zn = zv->root.child;
    while (zn) {
	TCHAR *ext = _tcsrchr (zn->name, '.');
	if (ext && !zn->vchild && zn->isfile) {
	    for (i = 0; plugins_7z[i]; i++) {
		if (!strcasecmp (ext + 1, plugins_7z[i])) {
		    struct zvolume *zvnew;
		    struct znode *zndir;
		    TCHAR tmp[MAX_DPATH];

		    _stprintf (tmp, L"%s.DIR", zn->fullname + _tcslen (zv->root.name) + 1);
		    zndir = get_znode (zv, tmp);
		    if (!zndir) {
			struct zarchive_info zai = { 0 };
			zvnew = zvolume_alloc_empty (tmp);
			zai.name = tmp;
			zai.t = zn->mtime;
			zndir = zvolume_adddir_abs (zv, &zai);
			zndir->vfile = zn;
			zndir->vchild = zvnew;
			zvnew->parent = zv;
		    }
		}
	    }
	}
	zn = zn->next;
    }
    return 0;
}

static void recursivepath (TCHAR *path, struct zvolume *zv)
{
    TCHAR tmp[2] = { FSDB_DIR_SEPARATOR, 0 };
    if (!zv)
	return;
    recursivepath (path, zv->parent);
    _tcscat (path, zv->root.fullname);
    _tcscat (path, tmp);
}

static struct zvolume *prepare_recursive_volume (struct zvolume *zv, const TCHAR *path)
{
    struct zfile *zf = NULL;
    struct zvolume *zvnew = NULL;

    write_log (L"unpacking '%s'\n", path);
    zf = zfile_open_archive (path, 0);
    if (!zf)
	goto end;
    zvnew = zfile_fopen_archive_ext (zf);
    if (!zvnew)
	goto end;
    zvnew->parent = zv->parent;
    zfile_fopen_archive_recurse (zvnew);
    zfile_fclose_archive (zv);
    return zvnew;
end:
    write_log (L"unpack failed\n");
    zfile_fclose_archive (zvnew);
    zfile_fclose (zf);
    return NULL;
}

static struct znode *get_znode (struct zvolume *zv, const TCHAR *ppath)
{
    struct znode *zn;
    int prevlen = 0;
    TCHAR path[MAX_DPATH];

    if (!zv)
	return NULL;
    _tcscpy (path, ppath);
    zn = &zv->root;
    while (zn) {
	if (zn->isfile) {
	    if (!_tcsicmp (zn->fullname, path))
		return zn;
	} else {
	    int len = _tcslen (zn->fullname);
	    if (_tcslen (path) >= len && (path[len] == 0 || path[len] == FSDB_DIR_SEPARATOR) && !_tcsncmp (zn->fullname, path, len)) {
		if (path[len] == 0)
		    return zn;
		if (zn->vchild) {
		    /* jump to separate tree, recursive archives */
		    struct zvolume *zvdeep = zn->vchild;
		    TCHAR *p = path + prevlen + 1;
		    if (zvdeep->archive == NULL) {
			zvdeep = prepare_recursive_volume (zvdeep, zn->fullname);
			if (!zvdeep) {
			    write_log (L"failed to unpack '%s'\n", zn->fullname);
			    return NULL;
			}
			/* replace dummy empty volume with real volume */
			zn->vchild = zvdeep;
		    }
		    zn = zvdeep->root.child;
		    while (*p && *p != FSDB_DIR_SEPARATOR)
			p++;
		    if (*p == 0)
			return NULL;
		    p++;
		    _tcscpy (path, zn->volume->root.name);
		    memmove (path + _tcslen (path) + 1, p, _tcslen (p) + 1);
		    path[_tcslen(path)] = FSDB_DIR_SEPARATOR;
		} else {
		    zn = zn->child;
		}
		prevlen = len;
		continue;
	    }
	}
	zn = zn->sibling;
    }
    return NULL;
}


static void addvolumesize (struct zvolume *zv, uae_s64 size)
{
    unsigned int blocks = (size + 511) / 512;

    if (blocks == 0)
	blocks++;
    while (zv) {
	zv->blocks += blocks;
	zv->size += size;
	zv = zv->parent;
    }
}

struct znode *znode_adddir (struct znode *parent, const TCHAR *name, struct zarchive_info *zai)
{
    struct znode *zn;
    TCHAR path[MAX_DPATH];

    _stprintf (path, L"%s%c%s", parent->fullname, FSDB_DIR_SEPARATOR, name);
    zn = get_znode (parent->volume, path);
    if (zn)
	return zn;
    zn = znode_alloc_child (parent, name);
    zn->mtime = zai->t;
    addvolumesize (parent->volume, 0);
    return zn;
}

struct znode *zvolume_adddir_abs (struct zvolume *zv, struct zarchive_info *zai)
{
    struct znode *zn2;
    TCHAR *path = my_strdup (zai->name);
    TCHAR *p, *p2;
    int i;

    if (_tcslen (path) > 0) {
	/* remove possible trailing / or \ */
	TCHAR last;
	last = path[_tcslen (path) - 1];
	if (last == '/' || last == '\\')
	    path[_tcslen (path) - 1] = 0;
    }
    zn2 = &zv->root;
    p = p2 = path;
    for (i = 0; path[i]; i++) {
	if (path[i] == '/' || path[i] == '\\') {
	    path[i] = 0;
	    zn2 = znode_adddir (zn2, p, zai);
	    path[i] = FSDB_DIR_SEPARATOR;
	    p = p2 = &path[i + 1];
	}
    }
    return znode_adddir (zn2, p, zai);
}

struct znode *zvolume_addfile_abs (struct zvolume *zv, struct zarchive_info *zai)
{
    struct znode *zn, *zn2;
    int i;
    TCHAR *path = my_strdup (zai->name);
    TCHAR *p, *p2;

    zn2 = &zv->root;
    p = p2 = path;
    for (i = 0; path[i]; i++) {
	if (path[i] == '/' || path[i] == '\\') {
	    path[i] = 0;
	    zn2 = znode_adddir (zn2, p, zai);
	    path[i] = FSDB_DIR_SEPARATOR;
	    p = p2 = &path[i + 1];
	}
    }
    if (p2) {
	zn = znode_alloc_child (zn2, p2);
	zn->size = zai->size;
	zn->isfile = 1;
	zn->mtime = zai->t;
	if (zai->comment)
	    zn->comment = my_strdup (zai->comment);
	zn->flags = zai->flags;
	addvolumesize (zn->volume, zai->size);
    }
    xfree (path);
    return zn;
}

struct zvolume *zfile_fopen_archive (const TCHAR *filename)
{
    struct zvolume *zv = NULL;
    struct zfile *zf = zfile_fopen_nozip (filename, L"rb");

    if (!zf)
	return NULL;
    zv = zfile_fopen_archive_ext (zf);
    if (!zv)
	zv = zfile_fopen_archive_data (zf);
#if RECURSIVE_ARCHIVES
    if (zv)
	zfile_fopen_archive_recurse (zv);
#endif
    /* pointless but who cares? */
    if (!zv)
	zv = archive_directory_plain (zf);
    if (zv)
	zvolume_addtolist (zv);
    else
	zfile_fclose (zf);
    return zv;
}

void zfile_fclose_archive (struct zvolume *zv)
{
    struct znode *zn;
    struct zvolume *v;

    if (!zv)
	return;
    zn = &zv->root;
    while (zn) {
	struct znode *zn2 = zn->next;
	if (zn->vchild)
	    zfile_fclose_archive (zn->vchild);
	xfree (zn->comment);
	xfree (zn->fullname);
	xfree (zn->name);
	zfile_fclose (zn->f);
	if (zn != &zv->root)
	    xfree (zn);
	zn = zn2;
    }
    archive_access_close (zv->handle, zv->id);
    if (zvolume_list == zv) {
	zvolume_list = zvolume_list->next;
    } else {
	v = zvolume_list;
	while (v) {
	    if (v->next == zv) {
		v->next = zv->next;
		break;
	    }
	    v = v->next;
	}
    }
    xfree (zv);
}

struct zdirectory {
    struct znode *n;
};

void *zfile_opendir_archive (const TCHAR *path)
{
    struct zvolume *zv = get_zvolume (path);
    struct znode *zn = get_znode (zv, path);
    struct zdirectory *zd = xmalloc (sizeof (struct zdirectory));

    if (!zn || (!zn->child && !zn->vchild))
	return NULL;
    if (zn->child) {
	zd->n = zn->child;
    } else {
	if (zn->vchild->archive == NULL) {
	    struct zvolume *zvnew = prepare_recursive_volume (zn->vchild, path);
	    if (zvnew)
		zn->vchild = zvnew;
	}
	zd->n = zn->vchild->root.next;
    }
    return zd;
}
void zfile_closedir_archive (struct zdirectory *zd)
{
    xfree (zd);
}
int zfile_readdir_archive (struct zdirectory *zd, TCHAR *out)
{
    if (!zd->n)
	return 0;
    _tcscpy (out, zd->n->name);
    zd->n = zd->n->sibling;
    return 1;
}

int zfile_fill_file_attrs_archive (const TCHAR *path, int *isdir, int *flags, TCHAR **comment)
{
    struct zvolume *zv = get_zvolume (path);
    struct znode *zn = get_znode (zv, path);

    *isdir = 0;
    *flags = 0;
    *comment = 0;
    if (!zn)
	return 0;
    *isdir = zn->isfile ? 0 : 1;
    *flags = zn->flags;
    if (zn->comment)
	*comment = my_strdup (zn->comment);
    return 1;
}

int zfile_fs_usage_archive (const TCHAR *path, const TCHAR *disk, struct fs_usage *fsp)
{
    struct zvolume *zv = get_zvolume (path);

    if (!zv)
	return -1;
    fsp->fsu_blocks = zv->blocks;
    fsp->fsu_bavail = 0;
    return 0;
}

int zfile_stat_archive (const TCHAR *path, struct _stat64 *s)
{
    struct zvolume *zv = get_zvolume (path);
    struct znode *zn = get_znode (zv, path);

    if (!zn)
	return 0;
    s->st_mode = zn->isfile ? 0 : FILEFLAG_DIR;
    s->st_size = zn->size;
    s->st_mtime = zn->mtime;
    return 1;
}

uae_s64 zfile_lseek_archive (void *d, uae_s64 offset, int whence)
{
    if (zfile_fseek (d, offset, whence))
	return -1;
    return zfile_ftell (d);
}

unsigned int zfile_read_archive (void *d, void *b, unsigned int size)
{
    return zfile_fread (b, 1, size, d);
}

void zfile_close_archive (void *d)
{
    /* do nothing, keep file cached */
}

void *zfile_open_archive (const TCHAR *path, int flags)
{
    struct zvolume *zv = get_zvolume (path);
    struct znode *zn = get_znode (zv, path);
    struct zfile *z;

    if (!zn)
	return 0;
    if (zn->f) {
	zfile_fseek (zn->f, 0, SEEK_SET);
	return zn->f;
    }
    if (zn->vfile)
	zn = zn->vfile;
    z = archive_getzfile (zn, zn->volume->id);
    if (z)
	zfile_fseek (z, 0, SEEK_SET);
    zn->f = z;
    return zn->f;
}

int zfile_exists_archive (const TCHAR *path, const TCHAR *rel)
{
    TCHAR tmp[MAX_DPATH];
    struct zvolume *zv;
    struct znode *zn;

    _stprintf (tmp, L"%s%c%s", path, FSDB_DIR_SEPARATOR, rel);
    zv = get_zvolume (tmp);
    zn = get_znode (zv, tmp);
    return zn ? 1 : 0;
}

