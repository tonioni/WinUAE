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

#include "IArchive.h"
#include "DLL.h"
#include "MyCom.h"
#include "FileStreams.h"
#include "PropVariant.h"
#include "PropVariantConversions.h"
#include "StringConvert.h"

typedef UINT32 (WINAPI * CreateObjectFunc)(
    const GUID *clsID, 
    const GUID *interfaceID, 
    void **outObject);

DEFINE_GUID (CLSID_CFormat7z, 0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x05, 0x00, 0x00);

void test7z (void)
{
  NWindows::NDLL::CLibrary library;
  if (!library.Load("7z.dll"))
  {
    return;
  }
  CreateObjectFunc createObjectFunc = (CreateObjectFunc)library.GetProcAddress("CreateObject");
  CMyComPtr<IInArchive> archive;
  if (createObjectFunc(&CLSID_CFormat7z, 
        &IID_IInArchive, (void **)&archive) != S_OK)
  {
    return;
  }

  CInFileStream *fileSpec = new CInFileStream;
  CMyComPtr<IInStream> file = fileSpec;

  if (!fileSpec->Open("test.7z"))
  {
    return;
  }
  if (archive->Open(file, 0, 0) != S_OK)
    return;
  UINT32 numItems = 0;
  archive->GetNumberOfItems(&numItems);  
  for (UINT32 i = 0; i < numItems; i++)
  {
    NWindows::NCOM::CPropVariant propVariant;
    archive->GetProperty(i, kpidPath, &propVariant);
    UString s = ConvertPropVariantToString(propVariant);
    printf("%s\n", (LPCSTR)GetOemString(s));
  }
}

 
 
 
 
 
 
 
 