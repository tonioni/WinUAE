#ifndef CAPSLIB_H
#define CAPSLIB_H

#undef LIB_USER
#ifdef CAPS_USER
#define LIB_USER
#endif
#include "comlib.h"

ExtSub SDWORD __cdecl CAPSInit();
ExtSub SDWORD __cdecl CAPSExit();
ExtSub SDWORD __cdecl CAPSAddImage();
ExtSub SDWORD __cdecl CAPSRemImage(SDWORD id);
ExtSub SDWORD __cdecl CAPSLockImage(SDWORD id, PCHAR name);
ExtSub SDWORD __cdecl CAPSLockImageMemory(SDWORD id, PUBYTE buffer, UDWORD length, UDWORD flag);
ExtSub SDWORD __cdecl CAPSUnlockImage(SDWORD id);
ExtSub SDWORD __cdecl CAPSLoadImage(SDWORD id, UDWORD flag);
ExtSub SDWORD __cdecl CAPSGetImageInfo(PCAPSIMAGEINFO pi, SDWORD id);
ExtSub SDWORD __cdecl CAPSLockTrack(PCAPSTRACKINFO pi, SDWORD id, UDWORD cylinder, UDWORD head, UDWORD flag);
ExtSub SDWORD __cdecl CAPSUnlockTrack(SDWORD id, UDWORD cylinder, UDWORD head);
ExtSub SDWORD __cdecl CAPSUnlockAllTracks(SDWORD id);
ExtSub PCHAR  __cdecl CAPSGetPlatformName(UDWORD pid);
ExtSub SDWORD __cdecl CAPSGetVersionInfo(PCAPSVERSIONINFO pi, UDWORD flag);

#endif
