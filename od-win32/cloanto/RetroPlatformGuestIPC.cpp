/*****************************************************************************
 Name    : RetroPlatformGuestIPC.c
 Project : RetroPlatform Player
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2017 Cloanto Corporation - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Authors : os, m
 Created : 2007-08-24 15:28:48
 Updated : 2017-09-10 12:13:00
 Comment : RetroPlatform Player interprocess communication functions (guest side)
 Note    : Can be compiled both in Unicode and Multibyte projects
 *****************************************************************************/

#include "RetroPlatformGuestIPC.h"
#include "RetroPlatformIPC.h"

// private functions
static BOOL RegisterWndClass(LPCTSTR pszClassName, HINSTANCE hInstance);
static HMODULE LoadRPGuestDLL(HWND hHostMessageWindow);
static LRESULT CALLBACK RPGuestWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

static const _TCHAR g_szHostWndClass[]  = _T(RPIPC_HostWndClass);
static const _TCHAR g_szGuestWndClass[] = _T(RPIPC_GuestWndClass);
static const WCHAR g_szRegistration[]   = L"Cloanto(R) RetroPlatform(TM) %d.%d";




/*****************************************************************************
 Name      : RPInitializeGuest
 Arguments : RPGUESTINFO *pInfo             - structure receiving IPC context info
           : HINSTANCE hInstance            - current module instance
           : LPCTSTR pszHostInfo            - host information
		   : PFN_MsgFunction pfnMsgFunction - message function to be called with incoming host messages
		   : LPARAM lMsgFunctionParam       - application-defined value to be passed to the message function
 Return    : HRESULT                        - S_OK (successful initialization), S_FALSE (not started as guest), or error code
 Authors   : os
 Created   : 2007-08-24 16:45:32
 Comment   : the guest calls this function (typically at startup time)
             to initialize the IPC context with the host
 *****************************************************************************/

HRESULT RPInitializeGuest(RPGUESTINFO *pInfo, HINSTANCE hInstance, LPCTSTR pszHostInfo,
                          PFN_MsgFunction pfnMsgFunction, LPARAM lMsgFunctionParam)
{
	_TCHAR szGuestClass[(sizeof(g_szGuestWndClass)/sizeof(_TCHAR))+20];
	WCHAR szRegistration[(sizeof(g_szRegistration)/sizeof(WCHAR))+10];
	PFN_RPGuestStartup pfnRPGuestStartup;
	WORD wMajorVersion, wMinorVersion;
	_TCHAR *pszHostClass;
	LRESULT lr;
	HRESULT hr;
	int nLen;

	if (!pInfo || !pszHostInfo)
		return E_POINTER;

	pInfo->hInstance = hInstance;
	pInfo->hHostMessageWindow = NULL;
	pInfo->hGuestMessageWindow = NULL;
	pInfo->bGuestClassRegistered = FALSE;
	pInfo->pfnMsgFunction = pfnMsgFunction;
	pInfo->lMsgFunctionParam = lMsgFunctionParam;
	pInfo->hRPGuestDLL = NULL;
	pInfo->pRPGuestDLLData = NULL;
	pInfo->pfnRPProcessMessage = NULL;
	pInfo->pfnRPSendMessage = NULL;
	pInfo->pfnRPPostMessage = NULL;

	// find the host message window
	//
	pszHostClass = (_TCHAR *)LocalAlloc(LMEM_FIXED, (_tcslen(g_szHostWndClass) + _tcslen(pszHostInfo) + 1) * sizeof(_TCHAR));
	if (!pszHostClass)
	{
		RPUninitializeGuest(pInfo);
		return E_OUTOFMEMORY;
	}
	wsprintf(pszHostClass, g_szHostWndClass, pszHostInfo);
	pInfo->hHostMessageWindow = FindWindow(pszHostClass, NULL);
	LocalFree(pszHostClass);
	if (!pInfo->hHostMessageWindow)
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(ERROR_HOST_UNREACHABLE);
	}
	// create the guest message window
	//
	wsprintf(szGuestClass, g_szGuestWndClass, GetCurrentProcessId());
	if (!RegisterWndClass(szGuestClass, hInstance))
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(GetLastError());
	}
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
	nLen = wsprintfW(szRegistration, g_szRegistration, RETROPLATFORM_API_VER_MAJOR, RETROPLATFORM_API_VER_MINOR);
	if (!RPSendMessage(RP_IPC_TO_HOST_PRIVATE_REGISTER, 0, 0, szRegistration, (nLen + 1) * sizeof(WCHAR), pInfo, &lr))
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(ERROR_HOST_UNREACHABLE);
	}
	if (!lr)
	{
		RPUninitializeGuest(pInfo);
		return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);
	}

	// load RPGuest.dll (or RPGuest64.dll)
	//
	pInfo->hRPGuestDLL = LoadRPGuestDLL(pInfo->hHostMessageWindow);
	if (pInfo->hRPGuestDLL)
	{
		pfnRPGuestStartup = (PFN_RPGuestStartup)GetProcAddress(pInfo->hRPGuestDLL, "RPGuestStartup");
		hr = pfnRPGuestStartup ? pfnRPGuestStartup(pInfo, sizeof(RPGUESTINFO)) : E_NOTIMPL;
		if (FAILED(hr))
		{
			RPUninitializeGuest(pInfo);
			return hr;
		}
	}
	else
	{
		if (!RPSendMessage(RP_IPC_TO_HOST_HOSTAPIVERSION, 0, 0, NULL, 0, pInfo, &lr))
			lr = 0;
		wMajorVersion = LOWORD(lr);
		wMinorVersion = HIWORD(lr);
		if (wMajorVersion > 7 || (wMajorVersion == 7 && wMinorVersion >= 2)) // RPGuest DLL required
		{
			RPUninitializeGuest(pInfo);
			return HRESULT_FROM_WIN32(ERROR_DLL_NOT_FOUND);
		}
	}
	return S_OK;
}

/*****************************************************************************
 Name      : LoadRPGuestDLL
 Arguments : HWND hHostMessageWindow - 
 Return    : static HMODULE          - 
 Authors   : os
 Created   : 2017-08-11 10:31:53
 Comment   : 
 *****************************************************************************/

static HMODULE LoadRPGuestDLL(HWND hHostMessageWindow)
{
	typedef DWORD (WINAPI *PFN_GetModuleFileNameEx)(HANDLE hProcess, HMODULE hModule, LPTSTR lpFilename, DWORD nSize);
	PFN_GetModuleFileNameEx pfnGetModuleFileNameEx;
	DWORD dwHostProcessId;
	HANDLE hHostProcess;
	_TCHAR szPath[MAX_PATH];
	HINSTANCE hPsapi;
	LPTSTR pszDLLName;
	HMODULE hRPGuestDLL;

	hRPGuestDLL = NULL;
	GetWindowThreadProcessId(hHostMessageWindow, &dwHostProcessId);
	hHostProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwHostProcessId);
	if (hHostProcess)
	{
		hPsapi = LoadLibrary(_T("psapi.dll"));
		if (hPsapi)
		{
			pfnGetModuleFileNameEx = (PFN_GetModuleFileNameEx)GetProcAddress(hPsapi, (sizeof(_TCHAR) == 1) ? "GetModuleFileNameExA" :  "GetModuleFileNameExW");
			if (pfnGetModuleFileNameEx)
			{
				if (pfnGetModuleFileNameEx(hHostProcess, (HMODULE)GetWindowLongPtr(hHostMessageWindow, GWLP_HINSTANCE), szPath, (sizeof(szPath)/sizeof(_TCHAR))))
				{
					pszDLLName = _tcsrchr(szPath, '\\');
					if (pszDLLName)
					{
						_tcsncpy(pszDLLName + 1, (sizeof(void*) == 8) ? _T("RPGuest64.dll") : _T("RPGuest.dll"), (sizeof(szPath)/sizeof(_TCHAR)) - (pszDLLName - szPath) - 1);
						hRPGuestDLL = LoadLibrary(szPath);
					}
				}
			}
			FreeLibrary(hPsapi);
		}
		CloseHandle(hHostProcess);
	}
	return hRPGuestDLL;
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
	PFN_RPGuestShutdown pfnRPGuestShutdown;

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
	if (pInfo->hRPGuestDLL)
	{
		pfnRPGuestShutdown = (PFN_RPGuestShutdown)GetProcAddress(pInfo->hRPGuestDLL, "RPGuestShutdown");
		if (pfnRPGuestShutdown)
			pfnRPGuestShutdown(pInfo, sizeof(RPGUESTINFO));
		FreeLibrary(pInfo->hRPGuestDLL);
		pInfo->hRPGuestDLL = NULL;
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

	if (pInfo->pfnRPSendMessage)
	{
		if (pInfo->pfnRPSendMessage(uMessage, wParam, lParam, pData, dwDataSize, pInfo, plResult))
			return TRUE; // message sent by RPGuest DLL
	}
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

	if (pInfo->pfnRPPostMessage)
	{
		if (pInfo->pfnRPPostMessage(uMessage, wParam, lParam, pInfo))
			return TRUE; // message posted by RPGuest DLL
	}
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
	if (pInfo)
	{
		if (pInfo->pfnRPProcessMessage)
		{
			LRESULT lr;
			if (pInfo->pfnRPProcessMessage(hWnd, uMessage, wParam, lParam, pInfo, &lr))
				return lr; // message fully processed by RPGuest DLL
		}
	}
	switch (uMessage)
	{
		case WM_CREATE:
		{
			LPCREATESTRUCT lpcs = (LPCREATESTRUCT)lParam;
			if (!lpcs)
				return -1;
			pInfo = (RPGUESTINFO *)lpcs->lpCreateParams;
			if (!pInfo)
				return -1;
  			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pInfo);
			return 0;
		}
		case WM_COPYDATA:
			if (pInfo && lParam)
			{
				COPYDATASTRUCT *pcds = (COPYDATASTRUCT *)lParam;
				if ((UINT)pcds->dwData >= WM_APP && (UINT)pcds->dwData <= 0xBFFF)
					return pInfo->pfnMsgFunction((UINT)pcds->dwData, 0, 0, pcds->lpData, pcds->cbData, pInfo->lMsgFunctionParam);
			}
			break;
		default:
			if (pInfo && uMessage >= WM_APP && uMessage <= 0xBFFF)
				return pInfo->pfnMsgFunction(uMessage, wParam, lParam, NULL, 0, pInfo->lMsgFunctionParam);
			break;
	}
	return DefWindowProc(hWnd, uMessage, wParam, lParam);
}
