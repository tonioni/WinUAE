/*****************************************************************************
 Name    : RetroPlatformGuestIPC.h
 Project : RetroPlatform Player
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2016 Cloanto Italia srl - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Authors : os, mcb
 Created : 2007-08-24 15:29:26
 Updated : 2016-06-17 11:18:00
 Comment : RetroPlatform Player interprocess communication include file (guest side)
 *****************************************************************************/

#ifndef __CLOANTO_RETROPLATFORMGUESTIPC_H__
#define __CLOANTO_RETROPLATFORMGUESTIPC_H__

#include <windows.h>
#include <tchar.h>

typedef LRESULT (CALLBACK *RPGUESTMSGFN)(UINT uMessage, WPARAM wParam, LPARAM lParam, LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam);
typedef int (WINAPI *TOUNICODEFN)(UINT wVirtKey, UINT wScanCode, const PBYTE lpKeyState, LPWSTR pwszBuff, int cchBuff, UINT wFlags);

// the RPGuestInfo fields should be considered private,
// since future implementations of RetroPlatform interprocess communication
// might use a different IPC mechanism;
// the guest (emulator engine) is just supposed to call
// the RetroPlatform IPC public functions (see below)
// to communicate with the host
//
typedef struct RPGuestInfo
{
	HINSTANCE hInstance;
	HWND hHostMessageWindow;
	HWND hGuestMessageWindow;
	BOOL bGuestClassRegistered;
	RPGUESTMSGFN pfnMsgFunction;
	LPARAM lMsgFunctionParam;
	HMODULE hUser32;
	TOUNICODEFN pfnToUnicode;
} RPGUESTINFO;

#ifdef __cplusplus
extern "C" {
#endif

// RetroPlatform IPC public functions
// (see instructions in RetroPlatformGuestIPC.c)
//
HRESULT RPInitializeGuest(RPGUESTINFO *pInfo, HINSTANCE hInstance, LPCTSTR pszHostInfo, RPGUESTMSGFN pfnMsgFunction, LPARAM lMsgFunctionParam);
void RPUninitializeGuest(RPGUESTINFO *pInfo);
BOOL RPSendMessage(UINT uMessage, WPARAM wParam, LPARAM lParam, LPCVOID pData, DWORD dwDataSize, const RPGUESTINFO *pInfo, LRESULT *plResult);
BOOL RPPostMessage(UINT uMessage, WPARAM wParam, LPARAM lParam, const RPGUESTINFO *pInfo);
HRESULT RPProcessKeyboardInput(DWORD dwFlags, BYTE btVirtualKey, BYTE btScanCode, const RPGUESTINFO *pInfo, LRESULT *plResult);

// RPProcessKeyboardInput dwFlags
#define RPPKIF_KEYDOWN		(1<<0) // bit set, if WM_KEYDOWN, or if (RAWKEYBOARD.Flags & RI_KEY_BREAK) == 0
#define RPPKIF_AUTOREPEAT	(1<<1) // bit set, if lParam & (1<<30) in a WM_KEYDOWN/WM_KEYUP message
#define RPPKIF_EXTENDED0	(1<<2) // bit set, if lParam & (1<<24) in a WM_KEYDOWN/WM_KEYUP message, or if RAWKEYBOARD.Flags & RI_KEY_E0
#define RPPKIF_EXTENDED1	(1<<3) // bit set, if RAWKEYBOARD.Flags & RI_KEY_E1
#define RPPKIF_DISABLEWIN	(1<<4) // bit set, if Windows keys need to be disabled (e.g. for WM_KEYDOWN/WM_KEYUP, or non-exclusive DirectInput client)
#define RPPKIF_RAWINPUT		(1<<5) // bit set, if DirectInput/rawinput client
#define RPPKIF_MORE			(1<<6) // bit set, if additional guest key events must be retrieved for the same keyboard input

// RPProcessKeyboardInput return codes
#define S_FALLBACK_HANDLING	((HRESULT)1L)
#define S_IGNORE            ((HRESULT)2L)
#define S_IGNORE_REPEATED   ((HRESULT)3L)

#ifdef __cplusplus
}   // ... extern "C"
#endif

#endif // __CLOANTO_RETROPLATFORMGUESTIPC_H__
