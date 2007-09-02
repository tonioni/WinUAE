 /*
  * UAE - The Un*x Amiga Emulator
  *
  * 7z decompression library support
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <initguid.h>
#include <basetyps.h>

#include "config.h"
#include "options.h"
#include "zfile.h"

typedef UINT32 (WINAPI * CreateObjectFunc)(
    const GUID *clsID,
    const GUID *interfaceID,
    void **outObject);

DEFINE_GUID (IID_IInArchive, 0x23170F69, 0x40C1, 0x278A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00);
DEFINE_GUID (CLSID_CFormat7z, 0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x05, 0x00, 0x00);

STDMETHOD(Open)(IInStream *stream, const UINT64 *maxCheckStartPosition,
    IArchiveOpenCallback *openArchiveCallback) PURE;
STDMETHOD(Close)(void) PURE;
STDMETHOD(GetNumberOfItems)(UINT32 *numItems) PURE;
STDMETHOD(GetProperty)(UINT32 index, PROPID propID, PROPVARIANT *value) PURE;
STDMETHOD(Extract)(const UINT32* indices, UINT32 numItems,
    INT32 testMode, IArchiveExtractCallback *extractCallback) PURE;
STDMETHOD(GetArchiveProperty)(PROPID propID, PROPVARIANT *value) PURE;
STDMETHOD(GetNumberOfProperties)(UINT32 *numProperties) PURE;
STDMETHOD(GetPropertyInfo)(UINT32 index,
    BSTR *name, PROPID *propID, VARTYPE *varType) PURE;
STDMETHOD(GetNumberOfArchiveProperties)(UINT32 *numProperties) PURE;
STDMETHOD(GetArchivePropertyInfo)(UINT32 index,
    BSTR *name, PROPID *propID, VARTYPE *varType) PURE;

void test7z (void)
{
    HMODULE lib;
    CreateObjectFunc createObjectFunc;
    void *archive;

    lib = LoadLibrary ("7z.dll");
    if (!lib)
	return;
    createObjectFunc =  (CreateObjectFunc)GetProcAddress (lib, "CreateObject");
    if (createObjectFunc (&CLSID_CFormat7z,  &IID_IInArchive, (void **)&archive) != S_OK)
	return;

 }
















