 /*
  * UAE - The Un*x Amiga Emulator
  *
  * 7-zip DLL-plugin support
  *
  * (c) 2004 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "zfile.h"
#include "unzip.h"
#include "disk.h"
#include "dms/pfile.h"
#include "gui.h"
#include "crc32.h"

#include <windows.h>

struct FileInArchiveInfo {
    int ArchiveHandle;
    uae_u64 CompressedFileSize;
    uae_u64 UncompressedFileSize;
    int attributes;
    int IsDir;
    FILETIME LastWriteTime;
    char path[MAX_DPATH];
};

typedef HRESULT (__stdcall *ReadCallback)(int StreamID, uae_u64 offset, uae_u32 count, void* buf, uae_u32 *processedSize);
typedef HRESULT (__stdcall *WriteCallback)(int StreamID, uae_u32 count, const void *buf, uae_u32 *processedSize);

typedef int (CALLBACK *pOpenArchive)(ReadCallback function, int StreamID, uae_u64 FileSize, int ArchiveType, int *result);
static pOpenArchive openArchive;
typedef int (CALLBACK *pGetFileCount)(int ArchiveHandle);
static pGetFileCount getFileCount;
typedef int (CALLBACK *pGetFileInfo)(int ArchiveHandle, int FileNum, struct FileInArchiveInfo *FileInfo);
static pGetFileInfo getFileInfo;
typedef int (CALLBACK *pExtract)(int ArchiveHandle, int FileNum, int StreamID, WriteCallback WriteFunc);
static pExtract extract;
typedef int (CALLBACK *pCloseArchive)(int ArchiveHandle);
static pCloseArchive closeArchive;

static HRESULT __stdcall readCallback (int StreamID, uae_u64 offset, uae_u32 count, void* buf, uae_u32 *processedSize)
{
    FILE *f = (FILE*)StreamID;
    int ret;

    fseek (f, (long)offset, SEEK_SET);
    ret = fread (buf, 1, count, f);
    if (processedSize)
	*processedSize = ret;
    return S_OK;
}
HRESULT __stdcall writeCallback (int StreamID, uae_u32 count, const void* buf, uae_u32 *processedSize)
{
    return 0;
}

static HMODULE arcacc_mod;

void arcacc_free (void)
{
    if (arcacc_mod)
	FreeLibrary (arcacc_mod);
    arcacc_mod = NULL;
}

int arcacc_init (void)
{
    if (arcacc_mod)
	return 1;
    arcacc_mod = LoadLibrary ("archiveaccess-debug.dll");
    if (!arcacc_mod)
	return 0;
    openArchive = (pOpenArchive) GetProcAddress (arcacc_mode, "openArchive");
    getFileCount = (pGetFileCount) GetProcAddress (arcacc_mode, "getFileCount");
    getFileInfo = (pGetFileInfo) GetProcAddress (arcacc_mode, "getFileInfo");
    extract = (pExtract) GetProcAddress (arcacc_mode, "extract");
    closeArchive = (pCloseArchive) GetProcAddress (arcacc_mode, "closeArchive");
    if (!OpenArchive || !getFileCount || !getFileInfo || !extract || !closeArchive) {
	arcacc_free ();
	return 0;
    }
    return 1;
}

void test (void)
{
    int ah, i;
    uae_u64 size;
    int status;
    FILE *f;
    struct FileInArchiveInfo fi;

    if (!arcacc_init ())
	return;

    f = fopen ("d:\\amiga\\test.7z", "rb");
    fseek (f, 0, SEEK_END);
    size = ftell (f);
    fseek (f, 0, SEEK_SET);

    ah = openArchive (readCallback, (int)f, size, 7, &status);
    if (1 || status == 0) {
	int fc = getFileCount (ah);
	for (i = 0; i < fc; i++) {
	    memset (&fi, 0, sizeof (fi));
	    getFileInfo (ah, i, &fi);
	}
	closeArchive (ah);
    }
    FreeLibrary (m);
}









