/*****************************************************************************
 Name    : RetroPlatformGuestIPC.c
 Project : RetroPlatform Player
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2016 Cloanto Italia srl - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Authors : os, mcb
 Created : 2007-08-24 15:28:48
 Updated : 2016-06-17 11:18:00
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

#define VK_NOP 0xFF // do-nothing key (disables Windows keys functionality)

static BOOL g_bLeftWinDown = FALSE;
static BOOL g_bRightWinDown = FALSE;
static int g_nIgnoreLeftWinUp = 0;
static int g_nIgnoreRightWinUp = 0;

struct VKeyFlags
{
	int nVKey;
	DWORD dwFlags;
};


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
    pInfo->hUser32 = LoadLibrary(_T("user32.dll"));
    pInfo->pfnToUnicode = pInfo->hUser32 ? (TOUNICODEFN)GetProcAddress(pInfo->hUser32, "ToUnicode") : NULL;

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
    if (pInfo->hUser32)
	{
		FreeLibrary(pInfo->hUser32);
		pInfo->hUser32 = NULL;
		pInfo->pfnToUnicode = NULL;
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

/*****************************************************************************
 Name      : RPProcessKeyboardInput
 Arguments : DWORD dwFlags            - 
           : BYTE btVirtualKey        - wParam & 0xFF, for a WM_KEYDOWN/WM_KEYUP message
           : BYTE btScanCode          - (lParam >> 16) & 0xFF, for a WM_KEYDOWN/WM_KEYUP message
           : const RPGUESTINFO *pInfo - 
           : LRESULT *plResult        - 
 Return    : HRESULT                  - 
 Authors   : os
 Created   : 2016-06-21 11:04:38
 Comment   : 
 *****************************************************************************/

HRESULT RPProcessKeyboardInput(DWORD dwFlags, BYTE btVirtualKey, BYTE btScanCode, const RPGUESTINFO *pInfo, LRESULT *plResult)
{
	static const struct VKeyFlags s_VKeyFlags[] =
	{
		{ VK_CAPITAL,  RP_PREPROCESSKEY_CAPS_DOWN   },
		{ VK_SHIFT,    RP_PREPROCESSKEY_SHIFT_DOWN  },
		{ VK_LSHIFT,   RP_PREPROCESSKEY_LSHIFT_DOWN },
		{ VK_RSHIFT,   RP_PREPROCESSKEY_RSHIFT_DOWN },
		{ VK_MENU,     RP_PREPROCESSKEY_ALT_DOWN    },
		{ VK_LMENU,    RP_PREPROCESSKEY_LALT_DOWN   },
		{ VK_RMENU,    RP_PREPROCESSKEY_RALT_DOWN   },
		{ VK_CONTROL,  RP_PREPROCESSKEY_CTRL_DOWN   },
		{ VK_LCONTROL, RP_PREPROCESSKEY_LCTRL_DOWN  },
		{ VK_RCONTROL, RP_PREPROCESSKEY_RCTRL_DOWN  },
	};
    static const int s_nVKeyFlagsCount = sizeof(s_VKeyFlags) / sizeof(s_VKeyFlags[0]);
    BYTE btKeyboardState[256], btProcessedVirtualKey, btProcessedScanCode;
	WPARAM wParam;
	LPARAM lParam;
	LRESULT lResult;
    wchar_t wcChars[2];
	int n;

	if (!pInfo || !plResult)
		return E_POINTER;

	if (!pInfo->pfnToUnicode)
		return S_FALLBACK_HANDLING;

    if (dwFlags & (RPPKIF_EXTENDED0 | RPPKIF_EXTENDED1))
    {
        switch (btVirtualKey)
        {
            case VK_CONTROL: btVirtualKey = VK_RCONTROL; break;
            case VK_MENU: btVirtualKey = VK_RMENU; break;
        }
    }
    else
    {
        switch (btVirtualKey)
        {
            case VK_CONTROL: btVirtualKey = VK_LCONTROL; break;
            case VK_MENU: btVirtualKey = VK_LMENU; break;
            case VK_SHIFT: btVirtualKey = (MapVirtualKey(VK_RSHIFT, MAPVK_VK_TO_VSC) == btScanCode) ? VK_RSHIFT : VK_LSHIFT; break;
        }
    }

	if (dwFlags & RPPKIF_MORE)
	{
		if (!RPSendMessage(RP_IPC_TO_HOST_PROCESSKEY, RP_PROCESSKEY_MORE, 0, NULL, 0, pInfo, plResult))
			return E_FAIL;
		if (!(*plResult & RP_PROCESSKEY_OK))
			return S_FALLBACK_HANDLING;
	}
	else if (dwFlags & RPPKIF_KEYDOWN)
	{
		if (dwFlags & RPPKIF_DISABLEWIN)
		{
			switch (btVirtualKey)
			{
				case VK_LWIN:
				case VK_RWIN: // disable Windows-key system functions (e.g. WinKey = Start Menu, WinKey+1/2/3 = activate first/second/third appbar program)
				{
					keybd_event(VK_NOP, 255, 0, 0); // a do-nothing key must precede the "up" event, or the Start Menu pops up
					keybd_event(VK_NOP, 255, KEYEVENTF_KEYUP, 0);
					keybd_event(btVirtualKey, btScanCode, KEYEVENTF_KEYUP, 0);
					GetKeyboardState(btKeyboardState);
					btKeyboardState[btVirtualKey] = 0;
					SetKeyboardState(btKeyboardState);
					if (btVirtualKey == VK_LWIN)
					{
						g_nIgnoreLeftWinUp += 1; // ignore the up event just synthesized
						if (g_bLeftWinDown)
							dwFlags |= RPPKIF_AUTOREPEAT; // flag as auto-repeated key event
						else
							g_bLeftWinDown = TRUE;
					}
					else
					{
						g_nIgnoreRightWinUp += 1;
						if (g_bRightWinDown)
							dwFlags |= RPPKIF_AUTOREPEAT;
						else
							g_bRightWinDown = TRUE;
					}
					break;
				}
				case VK_NOP:
					return S_IGNORE;
			}
		}
	    if (dwFlags & RPPKIF_AUTOREPEAT) // ignore repeated keys (autorepeat handled on guest side)
	        return S_IGNORE_REPEATED;

		wParam = RP_PREPROCESSKEY_SET_VKEY(btVirtualKey) |
		         RP_PREPROCESSKEY_SET_SCANCODE(btScanCode);
		if (dwFlags & RPPKIF_EXTENDED0)
			wParam |= RP_PREPROCESSKEY_EXTENDED0;
		if (dwFlags & RPPKIF_EXTENDED1)
			wParam |= RP_PREPROCESSKEY_EXTENDED1;
		if (dwFlags & RPPKIF_RAWINPUT)
			wParam |= RP_PREPROCESSKEY_RAWINPUT;
		if (GetKeyState(VK_CAPITAL) & 0x01)
			wParam |= RP_PREPROCESSKEY_CAPS_TOGGLED;

		for (n = 0; n < s_nVKeyFlagsCount; n++)
		{
			if (GetKeyState(s_VKeyFlags[n].nVKey) & 0x80)
				wParam |= (WPARAM)s_VKeyFlags[n].dwFlags;
		}
		if (!RPSendMessage(RP_IPC_TO_HOST_PREPROCESSKEY, wParam, 0, NULL, 0, pInfo, &lResult)) // ask how we are supposed to handle this event
			return E_FAIL;
		if (!(lResult & RP_PREPROCESSKEY_OK)) // host does not support RP_IPC_TO_HOST_PREPROCESSKEY (or other type of error - e.g. system not supported)
			return S_FALLBACK_HANDLING;

		btProcessedVirtualKey = RP_PREPROCESSKEY_GET_VKEY(lResult);
		btProcessedScanCode = RP_PREPROCESSKEY_GET_SCANCODE(lResult);
		if (btProcessedVirtualKey == 0) // event consumed by host
			return S_IGNORE;

		wParam = RP_PROCESSKEY_KEYDOWN;
		lParam = 0;

		if ((lResult & RP_PREPROCESSKEY_CHAR_KEY) &&
			btProcessedVirtualKey != 0)
		{
			// just set qualifier state, so that multiple keypresses can be detected
			// (e.g. press 'B' while pressing 'A')
			memset(btKeyboardState, 0, sizeof(btKeyboardState));
			for (n = 0; n < s_nVKeyFlagsCount; n++)
			{
				if (lResult & s_VKeyFlags[n].dwFlags)
					btKeyboardState[s_VKeyFlags[n].nVKey] |= 0x80;
			}
			if (lResult & RP_PREPROCESSKEY_CAPS_TOGGLED)
				btKeyboardState[VK_CAPITAL] |= 0x01;

			btKeyboardState[btProcessedVirtualKey] = 0x80;

			memset(wcChars, 0, sizeof(wcChars));
			n = pInfo->pfnToUnicode((UINT)btProcessedVirtualKey, btProcessedScanCode, btKeyboardState, wcChars, 2, 0);
			lParam = MAKELONG(wcChars[0], wcChars[1]);
			if (n < 0)
			{
				pInfo->pfnToUnicode((UINT)btProcessedVirtualKey, btProcessedScanCode, btKeyboardState, wcChars, 2, 0); // reset ToUnicode() internal flags
				n = 3;
			}
			else if (n > 2)
				n = 2;
			wParam |= RP_PROCESSKEY_SET_CHARCOUNT(n);
		}
		wParam |= RP_PROCESSKEY_SET_VKEY(btProcessedVirtualKey) |
		          RP_PROCESSKEY_SET_SCANCODE(btProcessedScanCode);
		if (lResult & RP_PREPROCESSKEY_EXTENDED0)
			wParam |= RP_PROCESSKEY_EXTENDED0;
		if (lResult & RP_PREPROCESSKEY_EXTENDED1)
			wParam |= RP_PROCESSKEY_EXTENDED1;
		if (lResult & RP_PREPROCESSKEY_RAWINPUT)
			wParam |= RP_PROCESSKEY_RAWINPUT;

		if (!RPSendMessage(RP_IPC_TO_HOST_PROCESSKEY, wParam, lParam, NULL, 0, pInfo, plResult))
			return E_FAIL;
		if (!(*plResult & RP_PROCESSKEY_OK)) // host does not support RP_IPC_TO_HOST_PROCESSKEY
			return S_FALLBACK_HANDLING;
    }
	else
	{
		switch (btVirtualKey)
		{
			case VK_NOP:
				return S_IGNORE;
			case VK_LWIN:
				if (dwFlags & RPPKIF_DISABLEWIN)
				{
					if (g_nIgnoreLeftWinUp)
					{
						g_nIgnoreLeftWinUp -= 1;
						return S_IGNORE;
					}
					g_bLeftWinDown = FALSE;
				}
				break;
			case VK_RWIN:
				if (dwFlags & RPPKIF_DISABLEWIN)
				{
					if (g_nIgnoreRightWinUp)
					{
						g_nIgnoreRightWinUp -= 1;
						return S_IGNORE;
					}
					g_bRightWinDown = FALSE;
				}
				break;
		}
		wParam = RP_PROCESSKEY_SET_VKEY(btVirtualKey) |
		         RP_PROCESSKEY_SET_SCANCODE(btScanCode);
		if (dwFlags & RPPKIF_EXTENDED0)
			wParam |= RP_PROCESSKEY_EXTENDED0;
		if (dwFlags & RPPKIF_EXTENDED1)
			wParam |= RP_PROCESSKEY_EXTENDED1;
		if (dwFlags & RPPKIF_RAWINPUT)
			wParam |= RP_PROCESSKEY_RAWINPUT;

		if (!RPSendMessage(RP_IPC_TO_HOST_PROCESSKEY, wParam, 0, NULL, 0, pInfo, plResult))
			return E_FAIL;
		if (!(*plResult & RP_PROCESSKEY_OK)) // host does not support RP_IPC_TO_HOST_PROCESSKEY
			return S_FALLBACK_HANDLING;
		if (*plResult == RP_PROCESSKEY_OK) // event consumed by host (e.g. Escape key) or no equivalent key in guest keyboard
			return S_IGNORE;
	}
	return NOERROR;
}
