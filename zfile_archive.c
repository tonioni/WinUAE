 /*
  * UAE - The Un*x Amiga Emulator
  *
  * transparent archive handling
  *
  *     2007 Toni Wilen
  */

#define ZLIB_WINAPI

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "zfile.h"
#include "archivers/zip/unzip.h"
#include "archivers/dms/pfile.h"
#include "crc32.h"
#include "zarchive.h"
#include "disk.h"

#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#include "win32.h"
#endif

static time_t fromdostime (uae_u32 dd)
{
    struct tm tm;
    time_t t;

    memset (&tm, 0, sizeof tm);
    tm.tm_hour = (dd >> 11) & 0x1f;
    tm.tm_min  = (dd >> 5) & 0x3f;
    tm.tm_sec  = ((dd >> 0) & 0x1f) * 2;
    tm.tm_year = ((dd >> 25) & 0x7f) + 80;
    tm.tm_mon  = ((dd >> 21) & 0x0f) - 1;
    tm.tm_mday = (dd >> 16) & 0x1f;
    t = mktime (&tm);
    _tzset ();
    t -= _timezone;
    return t;
}

static struct zvolume *getzvolume (struct zfile *zf, unsigned int id)
{
    struct zvolume *zv = NULL;

    switch (id)
    {
	case ArchiveFormatZIP:
	zv = archive_directory_zip (zf);
	break;
	case ArchiveFormat7Zip:
	zv = archive_directory_7z (zf);
	break;
	case ArchiveFormatRAR:
	zv = archive_directory_rar (zf);
	break;
	case ArchiveFormatLHA:
	zv = archive_directory_lha (zf);
	break;
	case ArchiveFormatLZX:
	zv = archive_directory_lzx (zf);
	break;
	case ArchiveFormatPLAIN:
	zv = archive_directory_plain (zf);
	break;
	case ArchiveFormatADF:
	zv = archive_directory_adf (zf);
	break;
	case ArchiveFormatRDB:
	zv = archive_directory_rdb (zf);
	break;
    }
    if (!zv)
	zv = archive_directory_arcacc (zf, id);
    return zv;
}

struct zfile *archive_getzfile (struct znode *zn, unsigned int id)
{
    struct zfile *zf = NULL;

    switch (id)
    {
	case ArchiveFormatZIP:
	zf = archive_access_zip (zn);
	break;
	case ArchiveFormat7Zip:
	zf = archive_access_7z (zn);
	break;
	case ArchiveFormatRAR:
	zf = archive_access_rar (zn);
	break;
	case ArchiveFormatLHA:
	zf = archive_access_lha (zn);
	break;
	case ArchiveFormatLZX:
	zf = archive_access_lzx (zn);
	break;
	case ArchiveFormatPLAIN:
	zf = archive_access_plain (zn);
	break;
	case ArchiveFormatADF:
	zf = archive_access_adf (zn);
	break;
	case ArchiveFormatRDB:
	zf = archive_access_rdb (zn);
	break;
    }
    return zf;
}

struct zfile *archive_access_select (struct zfile *zf, unsigned int id, int dodefault)
{
    struct zvolume *zv;
    struct znode *zn;
    int zipcnt, first, select;
    TCHAR tmphist[MAX_DPATH];
    struct zfile *z = NULL;
    int we_have_file;

    zv = getzvolume (zf, id);
    if (!zv)
	return zf;
    we_have_file = 0;
    tmphist[0] = 0;
    zipcnt = 1;
    first = 1;
    zn = &zv->root;
    while (zn) {
	int isok = 1;

	if (zn->type != ZNODE_FILE)
	    isok = 0;
	if (zfile_is_ignore_ext (zn->fullname))
	    isok = 0;
	if (isok) {
	    if (tmphist[0]) {
#ifndef _CONSOLE
		DISK_history_add (tmphist, -1);
#endif
		tmphist[0] = 0;
		first = 0;
	    }
	    if (first) {
		if (zfile_isdiskimage (zn->fullname))
		    _tcscpy (tmphist, zn->fullname);
	    } else {
		_tcscpy (tmphist, zn->fullname);
#ifndef _CONSOLE
		DISK_history_add (tmphist, -1);
#endif
		tmphist[0] = 0;
	    }
	    select = 0;
	    if (!zf->zipname)
		select = 1;
	    if (zf->zipname && _tcslen (zn->fullname) >= _tcslen (zf->zipname) && !strcasecmp (zf->zipname, zn->fullname + _tcslen (zn->fullname) - _tcslen (zf->zipname)))
		select = -1;
	    if (zf->zipname && zf->zipname[0] == '#' && _tstol (zf->zipname + 1) == zipcnt)
		select = -1;
	    if (select && !we_have_file) {
		z = archive_getzfile (zn, id);
		if (z) {
		    if (select < 0 || zfile_gettype (z))
			we_have_file = 1;
		    if (!we_have_file) {
			zfile_fclose (z);
			z = NULL;
		    }
		}
	    }
	}
	zipcnt++;
	zn = zn->sibling;
    }
#ifndef _CONSOLE
    if (first && tmphist[0])
	DISK_history_add (zfile_getname(zf), -1);
#endif
    zfile_fclose_archive (zv);
    if (z) {
	zfile_fclose (zf);
	zf = z;
    } else if (!dodefault && zf->zipname && zf->zipname[0]) {
	zfile_fclose (zf);
	zf = NULL;
    }
    return zf;
}

struct zfile *archive_access_arcacc_select (struct zfile *zf, unsigned int id)
{
    return zf;
}

void archive_access_scan (struct zfile *zf, zfile_callback zc, void *user, unsigned int id)
{
    struct zvolume *zv;
    struct znode *zn;

    zv = getzvolume (zf, id);
    if (!zv)
	return;
    zn = &zv->root;
    while (zn) {
	if (zn->type == ZNODE_FILE) {
	    struct zfile *zf2 = archive_getzfile (zn, id);
	    if (zf2) {
		int ztype = iszip (zf2);
		if (ztype) {
		    zfile_fclose (zf2);
		} else {
		    int ret = zc (zf2, user);
		    zfile_fclose (zf2);
		    if (ret)
			break;
		}
	    }
	}
	zn = zn->sibling;
    }
    zfile_fclose_archive (zv);
}

/* ZIP */

static void archive_close_zip (void *handle)
{
    unzClose (handle);
}

struct zvolume *archive_directory_zip (struct zfile *z)
{
    unzFile uz;
    unz_file_info file_info;
    struct zvolume *zv;
    int err;

    uz = unzOpen (z);
    if (!uz)
	return 0;
    if (unzGoToFirstFile (uz) != UNZ_OK)
	return 0;
    zv = zvolume_alloc (z, ArchiveFormatZIP, uz, NULL);
    for (;;) {
	char filename_inzip2[MAX_DPATH];
	TCHAR c;
	struct zarchive_info zai;
	time_t t;
	unsigned int dd;
        TCHAR *filename_inzip;

	err = unzGetCurrentFileInfo (uz, &file_info, filename_inzip2, sizeof (filename_inzip2), NULL, 0, NULL, 0);
	if (err != UNZ_OK)
	    return 0;
	filename_inzip = au (filename_inzip2);
	dd = file_info.dosDate;
	t = fromdostime (dd);
	memset(&zai, 0, sizeof zai);
	zai.name = filename_inzip;
	zai.t = t;
	zai.flags = -1;
	c = filename_inzip[_tcslen (filename_inzip) - 1];
	if (c != '/' && c != '\\') {
	    int err = unzOpenCurrentFile (uz);
	    if (err == UNZ_OK) {
		struct znode *zn;
		zai.size = file_info.uncompressed_size;
		zn = zvolume_addfile_abs (zv, &zai);
	    }
	} else {
	    filename_inzip[_tcslen (filename_inzip) - 1] = 0;
	    zvolume_adddir_abs (zv, &zai);
	}
	xfree (filename_inzip);
	err = unzGoToNextFile (uz);
	if (err != UNZ_OK)
	    break;
    }
    zv->method = ArchiveFormatZIP;
    return zv;
}


struct zfile *archive_access_zip (struct znode *zn)
{
    struct zfile *z = NULL;
    unzFile uz = zn->volume->handle;
    int i, err;
    TCHAR tmp[MAX_DPATH];
    char *s;

    _tcscpy (tmp, zn->fullname + _tcslen (zn->volume->root.fullname) + 1);
    if (unzGoToFirstFile (uz) != UNZ_OK)
	return 0;
    for (i = 0; tmp[i]; i++) {
	if (tmp[i] == '\\')
	    tmp[i] = '/';
    }
    s = ua (tmp);
    if (unzLocateFile (uz, s, 1) != UNZ_OK) {
	xfree (s);
	for (i = 0; tmp[i]; i++) {
	    if (tmp[i] == '/')
		tmp[i] = '\\';
	}
	s = ua (tmp);
	if (unzLocateFile (uz, s, 1) != UNZ_OK) {
	    xfree (s);
	    return 0;
	}
    }
    xfree (s);
    s = NULL;
    if (unzOpenCurrentFile (uz) != UNZ_OK)
	return 0;
    z = zfile_fopen_empty (uz, zn->fullname, zn->size);
    if (z) {
	err = unzReadCurrentFile (uz, z->data, zn->size);
    }
    unzCloseCurrentFile (uz);
    return z;
}

/* 7Z */

#include "archivers/7z/Types.h"
#include "archivers/7z/7zCrc.h"
#include "archivers/7z/Archive/7z/7zIn.h"
#include "archivers/7z/Archive/7z/7zExtract.h"
#include "archivers/7z/Archive/7z/7zAlloc.h"

static ISzAlloc allocImp;
static ISzAlloc allocTempImp;

typedef struct
{
  ISeekInStream s;
  struct zfile *zf;
} CFileInStream;



static SRes SzFileReadImp (void *object, void *buffer, size_t *size)
{
  CFileInStream *s = (CFileInStream *)object;
  *size = zfile_fread (buffer, 1, *size, s->zf);
  return SZ_OK;
}

static SRes SzFileSeekImp(void *object, Int64 *pos, ESzSeek origin)
{
  CFileInStream *s = (CFileInStream *)object;
  int org = 0;
  switch (origin)
  {
    case SZ_SEEK_SET: org = SEEK_SET; break;
    case SZ_SEEK_CUR: org = SEEK_CUR; break;
    case SZ_SEEK_END: org = SEEK_END; break;
  }
  zfile_fseek (s->zf, *pos, org);
  *pos = zfile_ftell (s->zf);
  return 0;
}

static void init_7z (void)
{
    static int initialized;

    if (initialized)
	return;
    initialized = 1;
    allocImp.Alloc = SzAlloc;
    allocImp.Free = SzFree;
    allocTempImp.Alloc = SzAllocTemp;
    allocTempImp.Free = SzFreeTemp;
    CrcGenerateTable ();
    _tzset ();
}

struct SevenZContext
{
    CSzArEx db;
    CFileInStream archiveStream;
    CLookToRead lookStream;
    Byte *outBuffer;
    size_t outBufferSize;
    UInt32 blockIndex;
};

static void archive_close_7z (struct SevenZContext *ctx)
{
    SzArEx_Free (&ctx->db, &allocImp);
    allocImp.Free (&allocImp, ctx->outBuffer);
    xfree (ctx);
}

#define EPOCH_DIFF 0x019DB1DED53E8000LL /* 116444736000000000 nsecs */
#define RATE_DIFF 10000000 /* 100 nsecs */

struct zvolume *archive_directory_7z (struct zfile *z)
{
    SRes res;
    struct zvolume *zv;
    int i;
    struct SevenZContext *ctx;

    init_7z ();
    ctx = xcalloc (sizeof (struct SevenZContext), 1);
    ctx->blockIndex = 0xffffffff;
    ctx->archiveStream.s.Read = SzFileReadImp;
    ctx->archiveStream.s.Seek = SzFileSeekImp;
    ctx->archiveStream.zf = z;
    LookToRead_CreateVTable (&ctx->lookStream, False);
    ctx->lookStream.realStream = &ctx->archiveStream.s;
    LookToRead_Init (&ctx->lookStream);

    SzArEx_Init (&ctx->db);
    res = SzArEx_Open (&ctx->db, &ctx->lookStream.s, &allocImp, &allocTempImp);
    if (res != SZ_OK) {
	write_log (L"7Z: SzArchiveOpen %s returned %d\n", zfile_getname (z), res);
	xfree (ctx);
	return NULL;
    }
    zv = zvolume_alloc (z, ArchiveFormat7Zip, ctx, NULL);
    for (i = 0; i < ctx->db.db.NumFiles; i++) {
	CSzFileItem *f = ctx->db.db.Files + i;
	TCHAR *name = au (f->Name);
	struct zarchive_info zai;

	memset(&zai, 0, sizeof zai);
	zai.name = name;
	zai.flags = -1;
	zai.size = f->Size;
	zai.t = 0;
	if (f->MTimeDefined) {
	    uae_u64 t = (((uae_u64)f->MTime.High) << 32) | f->MTime.Low;
	    if (t >= EPOCH_DIFF) {
		zai.t = (t - EPOCH_DIFF) / RATE_DIFF;
		zai.t -= _timezone;
		if (_daylight)
		    zai.t += 1 * 60 * 60;
	    }
	}
	if (!f->IsDir) {
	    struct znode *zn = zvolume_addfile_abs (zv, &zai);
	    zn->offset = i;
	}
	xfree (name);
    }
    zv->method = ArchiveFormat7Zip;
    return zv;
}

struct zfile *archive_access_7z (struct znode *zn)
{
    SRes res;
    struct zvolume *zv = zn->volume;
    struct zfile *z = NULL;
    size_t offset;
    size_t outSizeProcessed;
    struct SevenZContext *ctx;

    ctx = zv->handle;
    res = SzAr_Extract (&ctx->db, &ctx->lookStream.s, zn->offset,
		    &ctx->blockIndex, &ctx->outBuffer, &ctx->outBufferSize,
		    &offset, &outSizeProcessed,
		    &allocImp, &allocTempImp);
    if (res == SZ_OK) {
	z = zfile_fopen_empty (NULL, zn->fullname, zn->size);
	zfile_fwrite (ctx->outBuffer + offset, zn->size, 1, z);
    } else {
	write_log (L"7Z: SzExtract %s returned %d\n", zn->fullname, res);
    }
    return z;
}

/* RAR */



/* copy and paste job? you are only imagining it! */
static struct zfile *rarunpackzf; /* stupid unrar.dll */
#include <unrar.h>
typedef HANDLE (_stdcall* RAROPENARCHIVEEX)(struct RAROpenArchiveDataEx*);
static RAROPENARCHIVEEX pRAROpenArchiveEx;
typedef int (_stdcall* RARREADHEADEREX)(HANDLE,struct RARHeaderDataEx*);
static RARREADHEADEREX pRARReadHeaderEx;
typedef int (_stdcall* RARPROCESSFILE)(HANDLE,int,char*,char*);
static RARPROCESSFILE pRARProcessFile;
typedef int (_stdcall* RARCLOSEARCHIVE)(HANDLE);
static RARCLOSEARCHIVE pRARCloseArchive;
typedef void (_stdcall* RARSETCALLBACK)(HANDLE,UNRARCALLBACK,LONG);
static RARSETCALLBACK pRARSetCallback;
typedef int (_stdcall* RARGETDLLVERSION)(void);
static RARGETDLLVERSION pRARGetDllVersion;

static int rar_resetf (struct zfile *z)
{
    z->f = _tfopen (z->name, L"rb");
    if (!z->f) {
	zfile_fclose (z);
	return 0;
    }
    return 1;
}

static int canrar (void)
{
    static int israr;

    if (israr == 0) {
	israr = -1;
#ifdef _WIN32
	{
	    HMODULE rarlib;

	    rarlib = WIN32_LoadLibrary (L"unrar.dll");
	    if (rarlib) {
		pRAROpenArchiveEx = (RAROPENARCHIVEEX)GetProcAddress (rarlib, "RAROpenArchiveEx");
		pRARReadHeaderEx = (RARREADHEADEREX)GetProcAddress (rarlib, "RARReadHeaderEx");
		pRARProcessFile = (RARPROCESSFILE)GetProcAddress (rarlib, "RARProcessFile");
		pRARCloseArchive = (RARCLOSEARCHIVE)GetProcAddress (rarlib, "RARCloseArchive");
		pRARSetCallback = (RARSETCALLBACK)GetProcAddress (rarlib, "RARSetCallback");
		pRARGetDllVersion = (RARGETDLLVERSION)GetProcAddress (rarlib, "RARGetDllVersion");
		if (pRAROpenArchiveEx && pRARReadHeaderEx && pRARProcessFile && pRARCloseArchive && pRARSetCallback) {
		    israr = 1;
		    write_log (L"unrar.dll version %08X detected and used\n", pRARGetDllVersion ? pRARGetDllVersion() : -1);

		}
	    }
	}
#endif
    }
    return israr < 0 ? 0 : 1;
}

static int CALLBACK RARCallbackProc (UINT msg,LONG UserData,LONG P1,LONG P2)
{
    if (msg == UCM_PROCESSDATA) {
	zfile_fwrite ((uae_u8*)P1, 1, P2, rarunpackzf);
	return 0;
    }
    return -1;
}

struct RARContext
{
    struct RAROpenArchiveDataEx OpenArchiveData;
    struct RARHeaderDataEx HeaderData;
    HANDLE hArcData;
};


static void archive_close_rar (struct RARContext *rc)
{
    xfree (rc);
}

struct zvolume *archive_directory_rar (struct zfile *z)
{
    struct zvolume *zv;
    struct RARContext *rc;
    struct zfile *zftmp;
    int cnt;

    if (!canrar ())
	return archive_directory_arcacc (z, ArchiveFormatRAR);
    if (z->data)
	 /* wtf? stupid unrar.dll only accept filename as an input.. */
	return archive_directory_arcacc (z, ArchiveFormatRAR);
    rc = xcalloc (sizeof (struct RARContext), 1);
    zv = zvolume_alloc (z, ArchiveFormatRAR, rc, NULL);
    fclose (z->f); /* bleh, unrar.dll fails to open the archive if it is already open.. */
    z->f = NULL;
    rc->OpenArchiveData.ArcNameW = z->name;
    rc->OpenArchiveData.OpenMode = RAR_OM_LIST;
    rc->hArcData = pRAROpenArchiveEx (&rc->OpenArchiveData);
    if (rc->OpenArchiveData.OpenResult != 0) {
	xfree (rc);
	if (!rar_resetf (z)) {
	    zfile_fclose_archive (zv);
	    return NULL;
	}
	zfile_fclose_archive (zv);
	return archive_directory_arcacc (z, ArchiveFormatRAR);
    }
    pRARSetCallback (rc->hArcData, RARCallbackProc, 0);
    cnt = 0;
    while (pRARReadHeaderEx (rc->hArcData, &rc->HeaderData) == 0) {
	struct zarchive_info zai;
	struct znode *zn;
	memset (&zai, 0, sizeof zai);
	zai.name = rc->HeaderData.FileNameW;
	zai.size = rc->HeaderData.UnpSize;
	zai.flags = -1;
	zai.t = fromdostime (rc->HeaderData.FileTime);
	zn = zvolume_addfile_abs (zv, &zai);
	zn->offset = cnt++;
	pRARProcessFile (rc->hArcData, RAR_SKIP, NULL, NULL);
    }
    pRARCloseArchive (rc->hArcData);
    zftmp = zfile_fopen_empty (z, z->name, 0);
    zv->archive = zftmp;
    zv->method = ArchiveFormatRAR;
    return zv;
}

struct zfile *archive_access_rar (struct znode *zn)
{
    struct RARContext *rc = zn->volume->handle;
    int i;
    struct zfile *zf = NULL;

    if (zn->volume->method != ArchiveFormatRAR)
	return archive_access_arcacc (zn);
    rc->OpenArchiveData.OpenMode = RAR_OM_EXTRACT;
    rc->hArcData = pRAROpenArchiveEx (&rc->OpenArchiveData);
    if (rc->OpenArchiveData.OpenResult != 0)
	return NULL;
    pRARSetCallback (rc->hArcData, RARCallbackProc, 0);
    for (i = 0; i <= zn->offset; i++) {
	if (pRARReadHeaderEx (rc->hArcData, &rc->HeaderData))
	    return NULL;
	if (i < zn->offset) {
	    if (pRARProcessFile (rc->hArcData, RAR_SKIP, NULL, NULL))
		goto end;
	}
    }
    zf = zfile_fopen_empty (zn->volume->archive, zn->fullname, zn->size);
    rarunpackzf = zf;
    if (pRARProcessFile (rc->hArcData, RAR_TEST, NULL, NULL)) {
	zfile_fclose (zf);
	zf = NULL;
    }
end:
    pRARCloseArchive(rc->hArcData);
    return zf;
}

/* ArchiveAccess */


#if defined(ARCHIVEACCESS)

struct aaFILETIME
{
    uae_u32 dwLowDateTime;
    uae_u32 dwHighDateTime;
};
typedef void* aaHandle;
// This struct contains file information from an archive. The caller may store
// this information for accessing this file after calls to findFirst, findNext
#define FileInArchiveInfoStringSize 1024
struct aaFileInArchiveInfo {
	int ArchiveHandle; // handle for Archive/class pointer
	uae_u64 CompressedFileSize;
	uae_u64 UncompressedFileSize;
	uae_u32 attributes;
	int IsDir;
	struct aaFILETIME LastWriteTime;
	char path[FileInArchiveInfoStringSize];
};

typedef HRESULT (__stdcall *aaReadCallback)(int StreamID, uae_u64 offset, uae_u32 count, void* buf, uae_u32 *processedSize);
typedef HRESULT (__stdcall *aaWriteCallback)(int StreamID, uae_u64 offset, uae_u32 count, const void *buf, uae_u32 *processedSize);
typedef aaHandle (__stdcall *aapOpenArchive)(aaReadCallback function, int StreamID, uae_u64 FileSize, int ArchiveType, int *result, TCHAR *password);
typedef int (__stdcall *aapGetFileCount)(aaHandle ArchiveHandle);
typedef int (__stdcall *aapGetFileInfo)(aaHandle ArchiveHandle, int FileNum, struct aaFileInArchiveInfo *FileInfo);
typedef int (__stdcall *aapExtract)(aaHandle ArchiveHandle, int FileNum, int StreamID, aaWriteCallback WriteFunc, uae_u64 *written);
typedef int (__stdcall *aapCloseArchive)(aaHandle ArchiveHandle);

static aapOpenArchive aaOpenArchive;
static aapGetFileCount aaGetFileCount;
static aapGetFileInfo aaGetFileInfo;
static aapExtract aaExtract;
static aapCloseArchive aaCloseArchive;

#ifdef _WIN32
static HMODULE arcacc_mod;

static void arcacc_free (void)
{
    if (arcacc_mod)
	FreeLibrary (arcacc_mod);
    arcacc_mod = NULL;
}

static int arcacc_init (struct zfile *zf)
{
    if (arcacc_mod)
	return 1;
    arcacc_mod = WIN32_LoadLibrary (L"archiveaccess.dll");
    if (!arcacc_mod) {
	write_log (L"failed to open archiveaccess.dll ('%s')\n", zfile_getname (zf));
	return 0;
    }
    aaOpenArchive = (aapOpenArchive) GetProcAddress (arcacc_mod, "aaOpenArchive");
    aaGetFileCount = (aapGetFileCount) GetProcAddress (arcacc_mod, "aaGetFileCount");
    aaGetFileInfo = (aapGetFileInfo) GetProcAddress (arcacc_mod, "aaGetFileInfo");
    aaExtract = (aapExtract) GetProcAddress (arcacc_mod, "aaExtract");
    aaCloseArchive = (aapCloseArchive) GetProcAddress (arcacc_mod, "aaCloseArchive");
    if (!aaOpenArchive || !aaGetFileCount || !aaGetFileInfo || !aaExtract || !aaCloseArchive) {
	write_log (L"Missing functions in archiveaccess.dll. Old version?\n");
	arcacc_free ();
	return 0;
    }
    return 1;
}
#endif

#define ARCACC_STACKSIZE 10
static struct zfile *arcacc_stack[ARCACC_STACKSIZE];
static int arcacc_stackptr = -1;

static int arcacc_push (struct zfile *f)
{
    if (arcacc_stackptr == ARCACC_STACKSIZE - 1)
	return -1;
    arcacc_stackptr++;
    arcacc_stack[arcacc_stackptr] = f;
    return arcacc_stackptr;
}
static void arcacc_pop (void)
{
    arcacc_stackptr--;
}

static HRESULT __stdcall readCallback (int StreamID, uae_u64 offset, uae_u32 count, void *buf, uae_u32 *processedSize)
{
    struct zfile *f = arcacc_stack[StreamID];
    int ret;

    zfile_fseek (f, (long)offset, SEEK_SET);
    ret = zfile_fread (buf, 1, count, f);
    if (processedSize)
	*processedSize = ret;
    return 0;
}
static HRESULT __stdcall writeCallback (int StreamID, uae_u64 offset, uae_u32 count, const void *buf, uae_u32 *processedSize)
{
    struct zfile *f = arcacc_stack[StreamID];
    int ret;

    ret = zfile_fwrite ((void*)buf, 1, count, f);
    if (processedSize)
	*processedSize = ret;
    if (ret != count)
	return -1;
    return 0;
}

struct zvolume *archive_directory_arcacc (struct zfile *z, unsigned int id)
{
    aaHandle ah;
    int id_r, status;
    int fc, f;
    struct zvolume *zv;
    int skipsize = 0;

    if (!arcacc_init (z))
	return NULL;
    zv = zvolume_alloc (z, id, NULL, NULL);
    id_r = arcacc_push (z);
    ah = aaOpenArchive (readCallback, id_r, zv->archivesize, id, &status, NULL);
    if (!status) {
	zv->handle = ah;
	fc = aaGetFileCount (ah);
	for (f = 0; f < fc; f++) {
	    struct aaFileInArchiveInfo fi;
	    TCHAR *name;
	    struct znode *zn;
	    struct zarchive_info zai;

	    memset (&fi, 0, sizeof (fi));
	    aaGetFileInfo (ah, f, &fi);
	    if (fi.IsDir)
		continue;

	    name = au (fi.path);
	    memset (&zai, 0, sizeof zai);
	    zai.name = name;
	    zai.flags = -1;
	    zai.size = (unsigned int)fi.UncompressedFileSize;
	    zn = zvolume_addfile_abs (zv, &zai);
	    xfree (name);
	    zn->offset = f;
	    zn->method = id;

	    if (id == ArchiveFormat7Zip) {
		if (fi.CompressedFileSize)
		    skipsize = 0;
		skipsize += (int)fi.UncompressedFileSize;
	    }
	}
	aaCloseArchive (ah);
    }
    arcacc_pop ();
    zv->method = ArchiveFormatAA;
    return zv;
}


struct zfile *archive_access_arcacc (struct znode *zn)
{
    struct zfile *zf;
    struct zfile *z = zn->volume->archive;
    int status, id_r, id_w;
    aaHandle ah;
    int ok = 0;

    id_r = arcacc_push (z);
    ah = aaOpenArchive (readCallback, id_r, zn->volume->archivesize, zn->method, &status, NULL);
    if (!status) {
	int err;
	uae_u64 written = 0;
	struct aaFileInArchiveInfo fi;
	memset (&fi, 0, sizeof (fi));
	aaGetFileInfo (ah, zn->offset, &fi);
	zf = zfile_fopen_empty (z, zn->fullname, zn->size);
	id_w = arcacc_push (zf);
	err = aaExtract(ah, zn->offset, id_w, writeCallback, &written);
	if (zf->seek == fi.UncompressedFileSize)
	    ok = 1;
	arcacc_pop();
    }
    aaCloseArchive(ah);
    arcacc_pop();
    if (ok)
	return zf;
    zfile_fclose(zf);
    return NULL;
}
#endif

/* plain single file */

static struct znode *addfile (struct zvolume *zv, struct zfile *zf, const TCHAR *path, uae_u8 *data, int size)
{
    struct zarchive_info zai;
    struct znode *zn;
    struct zfile *z = zfile_fopen_empty (zf, path, size);

    zfile_fwrite (data, size, 1, z);
    memset(&zai, 0, sizeof zai);
    zai.name = path;
    zai.flags = -1;
    zai.size = size;
    zn = zvolume_addfile_abs (zv, &zai);
    if (zn)
	zn->f = z;
    else
	zfile_fclose (z);
    return zn;
}

static uae_u8 exeheader[]={0x00,0x00,0x03,0xf3,0x00,0x00,0x00,0x00};
struct zvolume *archive_directory_plain (struct zfile *z)
{
    struct zfile *zf, *zf2;
    struct zvolume *zv;
    struct znode *zn;
    struct zarchive_info zai;
    uae_u8 id[8];

    memset (&zai, 0, sizeof zai);
    zv = zvolume_alloc (z, ArchiveFormatPLAIN, NULL, NULL);
    memset(id, 0, sizeof id);
    zai.name = zfile_getfilename (z);
    zai.flags = -1;
    zfile_fseek(z, 0, SEEK_END);
    zai.size = zfile_ftell(z);
    zfile_fseek(z, 0, SEEK_SET);
    zfile_fread(id, sizeof id, 1, z);
    zfile_fseek(z, 0, SEEK_SET);
    zn = zvolume_addfile_abs (zv, &zai);
    if (!memcmp (id, exeheader, sizeof id)) {
	uae_u8 *data = xmalloc (1 + _tcslen (zai.name) + 1 + 2);
	sprintf (data, "\"%s\"\n", zai.name);
	zn = addfile (zv, z, L"s/startup-sequence", data, strlen (data));
	xfree (data);
    }
    zf = zfile_dup (z);
    zf2 = zuncompress (zf, 0, 1);
    if (zf2 != zf) {
	zf = zf2;
	zai.name = zfile_getfilename (zf);
	zai.flags = -1;
	zfile_fseek(zf, 0, SEEK_END);
	zai.size = zfile_ftell (zf2);
	zfile_fseek(zf, 0, SEEK_SET);
	zn = zvolume_addfile_abs (zv, &zai);
	if (zn)
	    zn->offset = 1;
    }
    zfile_fclose (zf2);
    return zv;
}
struct zfile *archive_access_plain (struct znode *zn)
{
    struct zfile *z;

    if (zn->offset) {
	struct zfile *zf;
	z = zfile_fopen_empty (zn->volume->archive, zn->fullname, zn->size);
	zf = zfile_fopen (zfile_getname (zn->volume->archive), L"rb", zn->volume->archive->zfdmask);
	zfile_fread (z->data, zn->size, 1, zf);
	zfile_fclose (zf);
    } else {
	z = zfile_fopen_empty (zn->volume->archive, zn->fullname, zn->size);
	zfile_fseek (zn->volume->archive, 0, SEEK_SET);
	zfile_fread (z->data, zn->size, 1, zn->volume->archive);
    }
    return z;
}

struct adfhandle {
    int size;
    int highblock;
    int blocksize;
    int rootblock;
    struct zfile *z;
    uae_u8 block[65536];
    uae_u32 dostype;
};

static TCHAR *getBSTR (uae_u8 *bstr)
{
    int n = *bstr++;
    uae_char buf[257];
    int i;

    for (i = 0; i < n; i++)
	buf[i] = *bstr++;
    buf[i] = 0;
    return au (buf);
}
static uae_u32 gl (struct adfhandle *adf, int off)
{
    uae_u8 *p = adf->block + off;
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
}

static const int secs_per_day = 24 * 60 * 60;
static const int diff = (8 * 365 + 2) * (24 * 60 * 60);
static time_t put_time (long days, long mins, long ticks)
{
    time_t t;

    if (days < 0)
	days = 0;
    if (days > 9900 * 365)
	days = 9900 * 365; // in future far enough?
    if (mins < 0 || mins >= 24 * 60)
	mins = 0;
    if (ticks < 0 || ticks >= 60 * 50)
	ticks = 0;

    t = ticks / 50;
    t += mins * 60;
    t += ((uae_u64)days) * secs_per_day;
    t += diff;

    return t;
}

static int adf_read_block (struct adfhandle *adf, int block)
{
    memset (adf->block, 0, adf->blocksize);
    if (block >= adf->highblock || block < 0)
	return 0;
    zfile_fseek (adf->z, block * adf->blocksize, SEEK_SET);
    zfile_fread (adf->block, adf->blocksize, 1, adf->z);
    return 1;
}

static void recurseadf (struct znode *zn, int root, TCHAR *name)
{
    int i;
    struct zvolume *zv = zn->volume;
    struct adfhandle *adf = zv->handle;
    TCHAR name2[MAX_DPATH];

    for (i = 6; i < 78; i++) {
	int block;
	if (!adf_read_block (adf, root))
	    return;
	block = gl (adf, i * 4);
	while (block > 0 && block < adf->size / 512) {
	    struct zarchive_info zai;
	    TCHAR *fname;
	    uae_u32 size, secondary;

	    if (!adf_read_block (adf, block))
		return;
	    if (gl (adf, 0) != 2)
	        break;
	    if (gl (adf, 1 * 4) != block)
		break;
	    secondary = gl (adf, 512 - 1 * 4);
	    if (secondary != -3 && secondary != 2)
	        break;
	    memset (&zai, 0, sizeof zai);
	    fname = getBSTR (adf->block + 512 - 20 * 4);
	    size = gl (adf, 512 - 47 * 4);
	    name2[0] = 0;
	    if (name[0]) {
		TCHAR sep[] = { FSDB_DIR_SEPARATOR, 0 };
		_tcscpy (name2, name);
		_tcscat (name2, sep);
	    }
	    _tcscat (name2, fname);
	    zai.name = name2;
	    zai.size = size;
	    zai.flags = gl (adf, 512 - 48);
	    zai.t = put_time (gl (adf, 512 - 23 * 4),
		gl (adf, 512 - 22 * 4),
		gl (adf, 512 - 21 * 4));
	    if (secondary == -3) {
	        struct znode *znnew = zvolume_addfile_abs (zv, &zai);
	        znnew->offset = block;
	    } else {
		struct znode *znnew = zvolume_adddir_abs (zv, &zai);
		znnew->offset = block;
		recurseadf (znnew, block, name2);
		if (!adf_read_block (adf, block))
		    return;
	    }
	    xfree (fname);
	    block = gl (adf, 512 - 4 * 4);
	}
    }
}

struct zvolume *archive_directory_adf (struct zfile *z)
{
    struct zvolume *zv;
    struct adfhandle *adf;
    TCHAR *volname;
    TCHAR name[MAX_DPATH];

    adf = xcalloc (sizeof (struct adfhandle), 1);
    zfile_fseek (z, 0, SEEK_END);
    adf->size = zfile_ftell (z);
    zfile_fseek (z, 0, SEEK_SET);

    adf->blocksize = 512;
    adf->highblock = adf->size / adf->blocksize;
    adf->z = z;

    if (!adf_read_block (adf, 0)) {
	xfree (adf);
	return NULL;
    }
    adf->dostype = gl (adf, 0);

    adf->rootblock = ((adf->size / 512) - 1 + 2) / 2;
    for (;;) {
	if (!adf_read_block (adf, adf->rootblock)) {
	    xfree (adf);
	    return NULL;
	}
	if (gl (adf, 0) != 2 || gl (adf, 512 - 1 * 4) != 1) {
	    if (adf->size < 2000000 && adf->rootblock != 880) {
		adf->rootblock = 880;
		continue;
	    }
	    xfree (adf);
	    return NULL;
	}
	break;
    }

    volname = getBSTR (adf->block + 512 - 20 * 4);

    zv = zvolume_alloc (z, ArchiveFormatADF, NULL, NULL);
    zv->method = ArchiveFormatADF;
    zv->handle = adf;

    name[0] = 0;
    recurseadf (&zv->root, adf->rootblock, name);
    return zv;
}

struct zfile *archive_access_adf (struct znode *zn)
{
    struct zfile *z;
    int block, root, ffs;
    struct adfhandle *adf = zn->volume->handle;
    int size;
    int i;
    uae_u8 *dst;

    size = zn->size;
    z = zfile_fopen_empty (zn->volume->archive, zn->fullname, size);
    if (!z)
	return NULL;
    ffs = adf->dostype & 1;
    root = zn->offset;
    dst = z->data;
    for (;;) {
	adf_read_block (adf, root);
	for (i = 128 - 51; i >= 6; i--) {
	    int bsize = ffs ? 512 : 488;
	    block = gl (adf, i * 4);
	    if (size < bsize)
		bsize = size;
	    if (ffs)
		zfile_fseek (adf->z, block * adf->blocksize, SEEK_SET);
	    else
		zfile_fseek (adf->z, block * adf->blocksize + (512 - 488), SEEK_SET);
	    zfile_fread (dst, bsize, 1, adf->z);
	    size -= bsize;
	    dst += bsize;
	    if (size <= 0)
		break;
	}
	if (size <= 0)
	    break;
	root = gl (adf, 512 - 2 * 4);
    }
    return z;
}

static void archive_close_adf (void *v)
{
    struct adfhandle *adf = v;
    xfree (adf);
}

static int rl (uae_u8 *p)
{
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}
static TCHAR *tochar (uae_u8 *s, int len)
{
    int i, j;
    uae_char tmp[256];
    j = 0;
    for (i = 0; i < len; i++) {
	uae_char c = *s++;
	if (c >= 0 && c <= 9) {
	    tmp[j++] = '\\';
	    tmp[j++] = '0' + c;
	} else if (c < ' ' || c > 'z') {
	    tmp[j++] = '.';
	} else {
	    tmp[j++] = c;
	}
	tmp[j] = 0;
    }
    return au (tmp);
}

struct zvolume *archive_directory_rdb (struct zfile *z)
{
    uae_u8 buf[512] = { 0 };
    int partnum;
    TCHAR *devname;
    struct zvolume *zv;

    zv = zvolume_alloc (z, ArchiveFormatRDB, NULL, NULL);

    zfile_fseek (z, 0, SEEK_SET);
    zfile_fread (buf, 1, 512, z);

    partnum = 0;
    for (;;) {
	int partblock;
	struct znode *zn;
        struct zarchive_info zai;
	TCHAR tmp[MAX_DPATH];
	int surf, spt, lowcyl, highcyl, reserved;
	int size, block, blocksize, rootblock;
	uae_u8 *p;
	TCHAR comment[81], *com;

	if (partnum == 0)
	    partblock = rl (buf + 28);
	else
	    partblock = rl (buf + 4 * 4);
	partnum++;
	if (partblock <= 0)
	    break;
	zfile_fseek (z, partblock * 512, SEEK_SET);
	zfile_fread (buf, 1, 512, z);
	if (memcmp (buf, "PART", 4))
	    break;

	p = buf + 128 - 16;
	surf = rl (p + 28);
	spt = rl (p + 36);
	reserved = rl (p + 40);
	lowcyl = rl (p + 52);
	highcyl = rl (p + 56);
	blocksize = rl (p + 20) * 4;
	block = lowcyl * surf * spt;

	size = (highcyl - lowcyl + 1) * surf * spt;
	size *= blocksize;

	rootblock = ((size / blocksize) - 1 + 2) / 2;

	devname = getBSTR (buf + 36);
	_stprintf (tmp, L"%s.hdf", devname);
	memset (&zai, 0, sizeof zai);
	com = tochar (buf + 192, 4);
	_stprintf (comment, L"FS=%s LO=%d HI=%d HEADS=%d SPT=%d RES=%d BLOCK=%d ROOT=%d",
	    com, lowcyl, highcyl, surf, spt, reserved, blocksize, rootblock);
	zai.comment = comment;
	xfree (com);
        zai.name = tmp;
	zai.size = size;
	zai.flags = -1;
	zn = zvolume_addfile_abs (zv, &zai);
	zn->offset = partblock;
    }

    zv->method = ArchiveFormatRDB;
    return zv;
}

struct zfile *archive_access_rdb (struct znode *zn)
{
    struct zfile *z = zn->volume->archive;
    struct zfile *zf;
    uae_u8 buf[512] = { 0 };
    int surf, spb, spt, lowcyl, highcyl;
    int size, block, blocksize;
    uae_u8 *p;

    zfile_fseek (z, zn->offset * 512, SEEK_SET);
    zfile_fread (buf, 1, 512, z);
 
    p = buf + 128 - 16;
    surf = rl (p + 28);
    spb = rl (p + 32);
    spt = rl (p + 36);
    lowcyl = rl (p + 52);
    highcyl = rl (p + 56);
    blocksize = rl (p + 20) * 4;
    block = lowcyl * surf * spt;

    size = (highcyl - lowcyl + 1) * surf * spt;
    size *= blocksize;

    zf = zfile_fopen_empty (z, zn->fullname, size);
    zfile_fseek (z, block * blocksize, SEEK_SET);
    zfile_fread (zf->data, size, 1, z);
    return zf;
}

void archive_access_close (void *handle, unsigned int id)
{
    switch (id)
    {
	case ArchiveFormatZIP:
	archive_close_zip (handle);
	break;
	case ArchiveFormat7Zip:
	archive_close_7z (handle);
	break;
	case ArchiveFormatRAR:
	archive_close_rar (handle);
	break;
	case ArchiveFormatLHA:
	break;
	case ArchiveFormatADF:
	archive_close_adf (handle);
	break;
    }
}
