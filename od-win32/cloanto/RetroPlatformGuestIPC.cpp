/*****************************************************************************
 Name    : RetroPlatformGuestIPC.c
 Project : RetroPlatform Player
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2012 Cloanto Italia srl - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Authors : os, mcb
 Created : 2007-08-24 15:28:48
 Updated : 2012-11-29 13:47:00
 Comment : RetroPlatform Player interprocess communication functions (guest side)
 Note    : Can be compiled both in Unicode and Multibyte projects
 *****************************************************************************/

#include "RetroPlatformGuestIPC.h"
#include "RetroPlatformIPC.h"

// private functions
static BOOL RegisterWndClass(LPCTSTR pszClassName, HINSTANCE hInstance);
static LRESULT CALLBACK RPGuestWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

static const _TCHAR g_szHostWndClass[]  = _T(RPIPC_HostWndClass);
static const _TCHAR g_szGuestWndClass[] = _T(RPIPC_GuestWndClass);
static const WCHAR g_szRegistration[]   = L"Cloanto(R) RetroPlatform(TM)";



/*****************************************************************************
 Name      : RPInitializeGuest
 Arguments : RPGUESTINFO *pInfo          - structure receiving IPC context info
           : HINSTANCE hInstance         - current module instance
           : LPCTSTR pszHostInfo         - host information
		   : RPGUESTMSGFN pfnMsgFunction - message function to be called with incoming host messages
		   : LPARAM lMsgFunctionParam    - application-defined value to be passed to the message function
 Return    : HRESULT                     - S_OK (successful initialization), S_FALSE (not started as guest), or error code
 Authors   : os
 Created   : 2007-08-24 16:45:32
 Comment   : the guest calls this function (typically at startup time)
             to initialize the IPC context with the host
 *****************************************************************************/

HRESULT RPInitializeGuest(RPGUESTINFO *pInfo, HINSTANCE hInstance, LPCTSTR pszHostInfo,
                          RPGUESTMSGFN pfnMsgFunction, LPARAM lMsgFunctionParam)
{
	_TCHAR szGuestClass[(sizeof(g_szGuestWndClass)/sizeof(_TCHAR))+20];
	_TCHAR *pszHostClass;
	LRESULT lr;

	if (!pInfo || !pszHostInfo)
		return E_POINTER;

	pInfo->hInstance = hInstance;
	pInfo->hHostMessageWindow = NULL;
	pInfo->hGuestMessageWindow = NULL;
	pInfo->bGuestClassRegistered = FALSE;
	pInfo->pfnMsgFunction = pfnMsgFunction;
	pInfo->lMsgFunctionParam = lMsgFunctionParam;

	// find the host message window
	//
	pszHostClass = (_TCHAR *)LocalAlloc(LMEM_FIXED, (_tcslen(g_szHostWndClass) + _tcslen(pszHostInfo) + 1) * sizeof(_TCHAR));
	if (!pszHostClass)
		return E_OUTOFMEMORY;
	wsprintf(pszHostClass, g_szHostWndClass, pszHostInfo);
	pInfo->hHostMessageWindow = FindWindow(pszHostClass, NULL);
	LocalFree(pszHostClass);
	if (!pInfo->hHostMessageWindow)
		return HRESULT_FROM_WIN32(ERROR_HOST_UNREACHABLE);

	// create the guest message window
	//
	wsprintf(szGuestClass, g_szGuestWndClass, GetCurrentProcessId());
	if (!RegisterWndClass(szGuestClass, hInstance))
		return HRESULT_FROM_WIN32(GetLastError());
	pInfo->bGuestClassRegistered = TRUE;
	//
	pInfo->hGuestMessageWindow = CreateWindow(szGuestClass, NULL, 0, 0,0, 1,1, NULL, NULL, hInstance, (LPVOID)pInfo);
	if (!pInfo->hGuestMessageWindow)
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(GetLastError());
	}

	// register with the host
	//
	if (!RPSendMessage(RP_IPC_TO_HOST_REGISTER, 0, 0, g_szRegistration, sizeof(g_szRegistration), pInfo, &lr))
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(ERROR_HOST_UNREACHABLE);
	}
	if (!lr)
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);
	}
	return S_OK;
}

/*****************************************************************************
 Name      : RPUninitializeGuest
 Arguments : RPGUESTINFO *pInfo -
 Return    : void
 Authors   : os
 Created   : 2007-08-27 16:16:21
 Comment   : the guest calls this function (typically at uninitialization time)
             to free the IPC context resources
             allocated by a successfull call to RPInitializeGuest()
 *****************************************************************************/

void RPUninitializeGuest(RPGUESTINFO *pInfo)
{
	_TCHAR szGuestClass[(sizeof(g_szGuestWndClass)/sizeof(_TCHAR))+20];

	if (!pInfo)
		return;

	if (pInfo->hGuestMessageWindow)
	{
		DestroyWindow(pInfo->hGuestMessageWindow);
		pInfo->hGuestMessageWindow = NULL;
	}
	if (pInfo->bGuestClassRegistered)
	{
		wsprintf(szGuestClass, g_szGuestWndClass, GetCurrentProcessId());
		UnregisterClass(szGuestClass, pInfo->hInstance);
		pInfo->bGuestClassRegistered = FALSE;
	}
}

/*****************************************************************************
 Name      : RPSendMessage
 Arguments : UINT uMessage            - message ID
           : WPARAM wParam            - message information (ignored if pData is not NULL)
           : LPARAM lParam            - message information (ignored if pData is not NULL)
           : LPCVOID pData            - message large-size information
           : DWORD dwDataSize         - size of the data pointed to by pData
           : const RPGUESTINFO *pInfo - IPC context information
           : LRESULT *plResult        - optional pointer to get the response returned by the host
 Return    : BOOL                     - TRUE, if the message was successfully sent
 Authors   : os
 Created   : 2007-08-27 17:17:31
 Comment   : the guest calls this function to send messages to the host;
             if pData is NULL then the information sent along the message
			 is held by the wParam/lParam pair
 *****************************************************************************/

BOOL RPSendMessage(UINT uMessage, WPARAM wParam, LPARAM lParam,
                   LPCVOID pData, DWORD dwDataSize, const RPGUESTINFO *pInfo, LRESULT *plResult)
{
	#define SRPMSG_TIMEOUT	5000
	DWORD_PTR dwResult;

	if (!pInfo)
		return FALSE;
	if (!pInfo->hHostMessageWindow)
		return FALSE;

	if (pData)
	{
		COPYDATASTRUCT cds;
		cds.dwData = (ULONG_PTR)uMessage;
		cds.cbData = dwDataSize;
		cds.lpData = (LPVOID)pData;
		dwResult = SendMessage(pInfo->hHostMessageWindow, WM_COPYDATA, (WPARAM)pInfo->hGuestMessageWindow, (LPARAM)&cds);
	}
	else
	{
		// SendMessageTimeout is not used, since the host
		// might display MessageBoxes during notifications
		// (e.g. go-to-fullscreen message)
		dwResult = SendMessage(pInfo->hHostMessageWindow, uMessage, wParam, lParam);
	}
	if (plResult)
		*plResult = dwResult;

	return TRUE;
}

/*****************************************************************************
 Name      : RPPostMessage
 Arguments : UINT uMessage            -
           : WPARAM wParam            -
           : LPARAM lParam            -
           : const RPGUESTINFO *pInfo -
 Return    : BOOL                     -
 Authors   : os
 Created   : 2008-06-10 13:30:34
 Comment   : the guest calls this function to post messages to the host
             (unlike RPSendMessage(), this function sends messages
			  in asynchronous fashion and cannot be used to post messages which require
			  a reply from the host and/or messages which include additional data)
 *****************************************************************************/

BOOL RPPostMessage(UINT uMessage, WPARAM wParam, LPARAM lParam, const RPGUESTINFO *pInfo)
{
	if (!pInfo)
		return FALSE;
	if (!pInfo->hHostMessageWindow)
		return FALSE;

	return PostMessage(pInfo->hHostMessageWindow, uMessage, wParam, lParam);
}

/*****************************************************************************
 Name      : RegisterWndClass
 Arguments : LPCTSTR pszClassName -
           : HINSTANCE hInstance  -
 Return    : BOOL                 -
 Authors   : os
 Created   : 2007-08-27 16:09:12
 Comment   : registers the guest window message class
 *****************************************************************************/

static BOOL RegisterWndClass(LPCTSTR pszClassName, HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize        = sizeof(WNDCLASSEX);
	wcex.style         = 0;
	wcex.lpfnWndProc   = RPGuestWndProc;
	wcex.cbClsExtra    = 0;
	wcex.cbWndExtra    = 0;
	wcex.hInstance     = hInstance;
	wcex.hIcon         = NULL;
	wcex.hCursor       = NULL;
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName  = NULL;
	wcex.lpszClassName = pszClassName;
	wcex.hIconSm       = NULL;

	return RegisterClassEx(&wcex);
}

/*****************************************************************************
 Name      : RPGuestWndProc
 Arguments : HWND hWnd     -
           : UINT uMessage -
           : WPARAM wParam -
           : LPARAM lParam -
 Return    : LRESULT       - response returned to the host
 Authors   : os
 Created   : 2007-08-27 15:24:37
 Comment   : window procedure (dispatches host messages to the guest callback function)
 *****************************************************************************/

static LRESULT CALLBACK RPGuestWndProc(HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	RPGUESTINFO *pInfo = (RPGUESTINFO *)(LONG_PTR)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	if (uMessage == WM_CREATE)
	{
		LPCREATESTRUCT lpcs = (LPCREATESTRUCT)lParam;
		if (!lpcs)
			return -1;
		pInfo = (RPGUESTINFO *)lpcs->lpCreateParams;
		if (!pInfo)
			return -1;
		#pragma warning (push)
		#pragma warning (disable : 4244) // ignore LONG_PTR cast warning in 32-bit compilation
  		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pInfo);
		#pragma warning (pop)
		return 0;
	}
	else if (uMessage == WM_COPYDATA && pInfo && lParam)
	{
		COPYDATASTRUCT *pcds = (COPYDATASTRUCT *)lParam;
		return pInfo->pfnMsgFunction((UINT)pcds->dwData, 0, 0, pcds->lpData, pcds->cbData, pInfo->lMsgFunctionParam);
	}
	else if (uMessage >= WM_APP && uMessage <= 0xBFFF && pInfo)
	{
		return pInfo->pfnMsgFunction(uMessage, wParam, lParam, NULL, 0, pInfo->lMsgFunctionParam);
	}
	else
	{
		return DefWindowProc(hWnd, uMessage, wParam, lParam);
	}
}
