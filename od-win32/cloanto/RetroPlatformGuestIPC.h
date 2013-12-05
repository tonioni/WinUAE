/*****************************************************************************
 Name    : RetroPlatformGuestIPC.h
 Project : RetroPlatform Player
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2013 Cloanto Italia srl - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Authors : os, mcb
 Created : 2007-08-24 15:29:26
 Updated : 2012-11-29 13:47:00
 Comment : RetroPlatform Player interprocess communication include file (guest side)
 *****************************************************************************/

#ifndef __CLOANTO_RETROPLATFORMGUESTIPC_H__
#define __CLOANTO_RETROPLATFORMGUESTIPC_H__

#include <windows.h>
#include <tchar.h>

typedef LRESULT (CALLBACK *RPGUESTMSGFN)(UINT uMessage, WPARAM wParam, LPARAM lParam, LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam);

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

#ifdef __cplusplus
}   // ... extern "C"
#endif

#endif // __CLOANTO_RETROPLATFORMGUESTIPC_H__
