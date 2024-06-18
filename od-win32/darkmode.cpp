
#include <climits>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>
#include <Uxtheme.h>
#include <WindowsX.h>
#include <Vssym32.h>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Uxtheme.lib")

#include "resource.h"

#include "darkmode.h"

#include "IatHook.h"

extern void write_log(const char *, ...);

bool g_darkModeSupported = false;
bool g_darkModeEnabled = false;
int darkModeForced = 0;
int darkModeDetect = 0;
DWORD g_buildNumber = 0;

fnSetWindowCompositionAttribute _SetWindowCompositionAttribute = nullptr;
fnShouldAppsUseDarkMode _ShouldAppsUseDarkMode = nullptr;
fnAllowDarkModeForWindow _AllowDarkModeForWindow = nullptr;
fnAllowDarkModeForApp _AllowDarkModeForApp = nullptr;
fnFlushMenuThemes _FlushMenuThemes = nullptr;
fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState = nullptr;
fnIsDarkModeAllowedForWindow _IsDarkModeAllowedForWindow = nullptr;
fnGetIsImmersiveColorUsingHighContrast _GetIsImmersiveColorUsingHighContrast = nullptr;
fnOpenNcThemeData _OpenNcThemeData = nullptr;
// 1903 18362
fnShouldSystemUseDarkMode _ShouldSystemUseDarkMode = nullptr;
fnSetPreferredAppMode _SetPreferredAppMode = nullptr;

COLORREF dark_text_color, dark_bg_color;

int GetAppDarkModeState(void)
{
	if (!_ShouldAppsUseDarkMode) {
		return -1;
	}
	return _ShouldAppsUseDarkMode() ? 1 : 0;
}

bool AllowDarkModeForWindow(HWND hWnd, bool allow)
{
	if (g_darkModeSupported)
		return _AllowDarkModeForWindow(hWnd, allow);
	return false;
}

bool IsHighContrast()
{
	HIGHCONTRASTW highContrast = { sizeof(highContrast) };
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, FALSE))
		return highContrast.dwFlags & HCF_HIGHCONTRASTON;
	return false;
}

void RefreshTitleBarThemeColor(HWND hWnd)
{
	BOOL dark = FALSE;
	if (_IsDarkModeAllowedForWindow(hWnd) &&
		_ShouldAppsUseDarkMode() &&
		!IsHighContrast())
	{
		dark = TRUE;
	}
	if (g_buildNumber < 18362)
		SetPropW(hWnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(dark)));
	else if (_SetWindowCompositionAttribute)
	{
		WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
		_SetWindowCompositionAttribute(hWnd, &data);
	}
}

bool IsColorSchemeChangeMessage(LPARAM lParam)
{
	bool is = false;
	if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
	{
		_RefreshImmersiveColorPolicyState();
		is = true;
	}
	_GetIsImmersiveColorUsingHighContrast(IHCM_REFRESH);
	return is;
}

bool IsColorSchemeChangeMessage(UINT message, LPARAM lParam)
{
	if (message == WM_SETTINGCHANGE)
		return IsColorSchemeChangeMessage(lParam);
	return false;
}

void AllowDarkModeForApp(PreferredAppMode dark)
{
	if (_AllowDarkModeForApp)
		_AllowDarkModeForApp(dark != PreferredAppMode::Default);
	else if (_SetPreferredAppMode)
		_SetPreferredAppMode(dark);
}

static void FixDarkScrollBar()
{
	HMODULE hComctl = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (hComctl)
	{
		auto addr = FindDelayLoadThunkInModule(hComctl, "uxtheme.dll", 49); // OpenNcThemeData
		if (addr)
		{
			DWORD oldProtect;
			if (VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &oldProtect))
			{
				auto MyOpenThemeData = [](HWND hWnd, LPCWSTR classList) -> HTHEME {
					if (wcscmp(classList, L"ScrollBar") == 0)
					{
						hWnd = nullptr;
						classList = L"Explorer::ScrollBar";
					}
					return _OpenNcThemeData(hWnd, classList);
				};

				addr->u1.Function = reinterpret_cast<ULONG_PTR>(static_cast<fnOpenNcThemeData>(MyOpenThemeData));
				VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);
			}
		}
	}
}

static void GetDarkmodeFontColor(void)
{

	HTHEME hTheme = OpenThemeData(nullptr, L"ItemsView");
	if (hTheme) {
		COLORREF color;
		if (SUCCEEDED(GetThemeColor(hTheme, 0, 0, TMT_TEXTCOLOR, &color)))
		{
			dark_text_color = color;
		}
		if (SUCCEEDED(GetThemeColor(hTheme, 0, 0, TMT_FILLCOLOR, &color)))
		{
			dark_bg_color = color;
		}
		CloseThemeData(hTheme);
	}

}

constexpr bool CheckBuildNumber(DWORD buildNumber)
{
	return buildNumber <= 29999;
}

void InitDarkMode(int enable)
{
	auto RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
	if (RtlGetNtVersionNumbers)
	{
		DWORD major, minor;
		RtlGetNtVersionNumbers(&major, &minor, &g_buildNumber);
		g_buildNumber &= ~0xF0000000;
		if (major == 10 && minor == 0 && CheckBuildNumber(g_buildNumber))
		{
			HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (hUxtheme)
			{
				_OpenNcThemeData = reinterpret_cast<fnOpenNcThemeData>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(49)));
				_RefreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
				_GetIsImmersiveColorUsingHighContrast = reinterpret_cast<fnGetIsImmersiveColorUsingHighContrast>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(106)));
				_ShouldAppsUseDarkMode = reinterpret_cast<fnShouldAppsUseDarkMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));
				_AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));

				auto ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
				if (g_buildNumber < 18362)
					_AllowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
				else
					_SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);

				//_FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));
				_IsDarkModeAllowedForWindow = reinterpret_cast<fnIsDarkModeAllowedForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(137)));

				_SetWindowCompositionAttribute = reinterpret_cast<fnSetWindowCompositionAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));

				if (_OpenNcThemeData &&
					_RefreshImmersiveColorPolicyState &&
					_ShouldAppsUseDarkMode &&
					_AllowDarkModeForWindow &&
					(_AllowDarkModeForApp || _SetPreferredAppMode) &&
					_IsDarkModeAllowedForWindow)
				{
					g_darkModeSupported = true;

					if (!darkModeDetect) {
						if (darkModeForced > 0) {
							AllowDarkModeForApp(PreferredAppMode::ForceDark);
						} else  if (darkModeForced < 0) {
							AllowDarkModeForApp(PreferredAppMode::ForceLight);
						} else if (enable == -1 || enable == 1) {
							AllowDarkModeForApp(PreferredAppMode::AllowDark);
						} else {
							AllowDarkModeForApp(PreferredAppMode::Default);
						}
					} else if (darkModeDetect < 0) {
						if (_ShouldAppsUseDarkMode()) {
							AllowDarkModeForApp(PreferredAppMode::ForceDark);
						} else {
							AllowDarkModeForApp(PreferredAppMode::ForceLight);
						}
					}

					_RefreshImmersiveColorPolicyState();

					g_darkModeEnabled = _ShouldAppsUseDarkMode() && !IsHighContrast();
					
					if (g_darkModeEnabled && enable < -1) {
						if (darkModeDetect < 0) {
							AllowDarkModeForApp(PreferredAppMode::ForceDark);
						}
					}

					if (!g_darkModeEnabled && enable > 0 && darkModeForced >= 0) {
						if (darkModeDetect < 0) {
							AllowDarkModeForApp(PreferredAppMode::ForceDark);
						}
						_RefreshImmersiveColorPolicyState();
						g_darkModeEnabled = _ShouldAppsUseDarkMode() && !IsHighContrast();
					}

					if (g_darkModeEnabled) {
						FixDarkScrollBar();
						GetDarkmodeFontColor();
					}
				}
			}
		}
	}

	if (!enable && g_darkModeSupported) {
		if (darkModeDetect < 0) {
			AllowDarkModeForApp(PreferredAppMode::Default);
		}
		if (_RefreshImmersiveColorPolicyState != NULL) {
			_RefreshImmersiveColorPolicyState();
		}
		g_darkModeEnabled = false;
	}
}


struct SubClassData
{
	HTHEME hTheme = nullptr;
	COLORREF headerTextColor;
	int iStateID = 0;

	~SubClassData()
	{
		CloseTheme();
	}

	bool EnsureTheme(HWND hwnd, const TCHAR *theme)
	{
		if (!hTheme)
		{
			hTheme = OpenThemeData(hwnd, theme);
		}
		return hTheme != nullptr;
	}

	void CloseTheme()
	{
		if (hTheme)
		{
			CloseThemeData(hTheme);
			hTheme = nullptr;
		}
	}
};

static void RenderButton(HWND hwnd, HDC hdc, HTHEME hTheme, int iPartID, int iStateID)
{
	RECT rcClient = { 0 };
	TCHAR szText[256] = { 0 };
	DWORD nState = static_cast<DWORD>(SendMessage(hwnd, BM_GETSTATE, 0, 0));
	DWORD uiState = static_cast<DWORD>(SendMessage(hwnd, WM_QUERYUISTATE, 0, 0));
	DWORD nStyle = GetWindowLong(hwnd, GWL_STYLE);

	HFONT hFont = nullptr;
	HFONT hOldFont = nullptr;
	HFONT hCreatedFont = nullptr;
	LOGFONT lf = { 0 };
	if (SUCCEEDED(GetThemeFont(hTheme, hdc, iPartID, iStateID, TMT_FONT, &lf)))
	{
		hCreatedFont = CreateFontIndirect(&lf);
		hFont = hCreatedFont;
	}

	if (!hFont) {
		hFont = reinterpret_cast<HFONT>(SendMessage(hwnd, WM_GETFONT, 0, 0));
	}

	hOldFont = (HFONT)SelectObject(hdc, hFont);

	DWORD dtFlags = DT_LEFT; // DT_LEFT is 0
	dtFlags |= (nStyle & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;
	dtFlags |= ((nStyle & BS_CENTER) == BS_CENTER) ? DT_CENTER : (nStyle & BS_RIGHT) ? DT_RIGHT : 0;
	dtFlags |= ((nStyle & BS_VCENTER) == BS_VCENTER) ? DT_VCENTER : (nStyle & BS_BOTTOM) ? DT_BOTTOM : 0;
	dtFlags |= (uiState & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0;

	if (!(nStyle & BS_MULTILINE) && !(nStyle & BS_BOTTOM) && !(nStyle & BS_TOP))
	{
		dtFlags |= DT_VCENTER;
	}

	GetClientRect(hwnd, &rcClient);
	GetWindowText(hwnd, szText, sizeof(szText) / sizeof(TCHAR));

	SIZE szBox = { 13, 13 };
	GetThemePartSize(hTheme, hdc, iPartID, iStateID, NULL, TS_DRAW, &szBox);

	RECT rcText = rcClient;
	GetThemeBackgroundContentRect(hTheme, hdc, iPartID, iStateID, &rcClient, &rcText);

	RECT rcBackground = rcClient;
	if (dtFlags & DT_SINGLELINE)
	{
		rcBackground.top += (rcText.bottom - rcText.top - szBox.cy) / 2;
	}
	rcBackground.bottom = rcBackground.top + szBox.cy;
	rcBackground.right = rcBackground.left + szBox.cx;
	rcText.left = rcBackground.right + 3;

	DrawThemeParentBackground(hwnd, hdc, &rcClient);
	DrawThemeBackground(hTheme, hdc, iPartID, iStateID, &rcBackground, nullptr);

	DTTOPTS dtto = { sizeof(DTTOPTS), DTT_TEXTCOLOR };
	dtto.crText = dark_text_color;

	DrawThemeTextEx(hTheme, hdc, iPartID, iStateID, szText, -1, dtFlags, &rcText, &dtto);

	if ((nState & BST_FOCUS) && !(uiState & UISF_HIDEFOCUS))
	{
		RECT rcTextOut = rcText;
		dtto.dwFlags |= DTT_CALCRECT;
		DrawThemeTextEx(hTheme, hdc, iPartID, iStateID, szText, -1, dtFlags | DT_CALCRECT, &rcTextOut, &dtto);
		RECT rcFocus = rcTextOut;
		rcFocus.bottom++;
		rcFocus.left--;
		rcFocus.right++;
		DrawFocusRect(hdc, &rcFocus);
	}

	if (hCreatedFont) DeleteObject(hCreatedFont);
	SelectObject(hdc, hOldFont);
}

static void PaintButton(HWND hwnd, HDC hdc, SubClassData &buttonData)
{
	DWORD nState = static_cast<DWORD>(SendMessage(hwnd, BM_GETSTATE, 0, 0));
	DWORD nStyle = GetWindowLong(hwnd, GWL_STYLE);
	DWORD nButtonStyle = nStyle & 0xF;

	int iPartID = BP_PUSHBUTTON;
	if (nButtonStyle == BS_CHECKBOX || nButtonStyle == BS_AUTOCHECKBOX || nButtonStyle == BS_3STATE || nButtonStyle == BS_AUTO3STATE) {
		iPartID = BP_CHECKBOX;
	} else if (nButtonStyle == BS_RADIOBUTTON || nButtonStyle == BS_AUTORADIOBUTTON) {
		iPartID = BP_RADIOBUTTON;
	} else {
		return;
	}

	// states of BP_CHECKBOX and BP_RADIOBUTTON are the same
	int iStateID = RBS_UNCHECKEDNORMAL;

	if (nStyle & WS_DISABLED)
		iStateID = RBS_UNCHECKEDDISABLED;
	else if (nState & BST_PUSHED)
		iStateID = RBS_UNCHECKEDPRESSED;
	else if (nState & BST_HOT)
		iStateID = RBS_UNCHECKEDHOT;
	
	if (nState & BST_CHECKED)
		iStateID += 4;
	else if ((nButtonStyle == BS_3STATE || nButtonStyle == BS_AUTO3STATE) && (nStyle & BST_INDETERMINATE))
		iStateID += 8;

	if (BufferedPaintRenderAnimation(hwnd, hdc)) {
		return;
	}

	BP_ANIMATIONPARAMS animParams = { sizeof(animParams) };
	animParams.style = BPAS_LINEAR;
	if (iStateID != buttonData.iStateID) {
		GetThemeTransitionDuration(buttonData.hTheme, iPartID, buttonData.iStateID, iStateID, TMT_TRANSITIONDURATIONS, &animParams.dwDuration);
	}

	RECT rcClient = { 0 };
	GetClientRect(hwnd, &rcClient);

	HDC hdcFrom = nullptr;
	HDC hdcTo = nullptr;
	HANIMATIONBUFFER hbpAnimation = BeginBufferedAnimation(hwnd, hdc, &rcClient, BPBF_COMPATIBLEBITMAP, nullptr, &animParams, &hdcFrom, &hdcTo);
	if (hbpAnimation) {
		if (hdcFrom) {
			RenderButton(hwnd, hdcFrom, buttonData.hTheme, iPartID, buttonData.iStateID);
		}
		if (hdcTo) {
			RenderButton(hwnd, hdcTo, buttonData.hTheme, iPartID, iStateID);
		}
		buttonData.iStateID = iStateID;
		EndBufferedAnimation(hbpAnimation, TRUE);
	} else {
		RenderButton(hwnd, hdc, buttonData.hTheme, iPartID, iStateID);
		buttonData.iStateID = iStateID;
	}
}


static void PaintGroupbox(HWND hwnd, HDC hdc, SubClassData &buttonData)
{
	DWORD nStyle = GetWindowLong(hwnd, GWL_STYLE);
	int iPartID = BP_GROUPBOX;
	int iStateID = GBS_NORMAL;

	if (nStyle & WS_DISABLED) {
		iStateID = GBS_DISABLED;
	}

	RECT rcClient = { 0 };
	GetClientRect(hwnd, &rcClient);

	RECT rcText = rcClient;
	RECT rcBackground = rcClient;

	HFONT hFont = nullptr;
	HFONT hOldFont = nullptr;
	HFONT hCreatedFont = nullptr;
	LOGFONT lf = { 0 };
	if (SUCCEEDED(GetThemeFont(buttonData.hTheme, hdc, iPartID, iStateID, TMT_FONT, &lf))) {
		hCreatedFont = CreateFontIndirect(&lf);
		hFont = hCreatedFont;
	}

	if (!hFont) {
		hFont = reinterpret_cast<HFONT>(SendMessage(hwnd, WM_GETFONT, 0, 0));
	}

	hOldFont = (HFONT)SelectObject(hdc, hFont);

	TCHAR szText[256] = { 0 };
	GetWindowText(hwnd, szText, sizeof(szText) / sizeof(TCHAR));

	if (szText[0]) {
		SIZE textSize = { 0 };
		GetTextExtentPoint32(hdc, szText, static_cast<int>(wcslen(szText)), &textSize);
		rcBackground.top += textSize.cy / 2;
		rcText.left += 7;
		rcText.bottom = rcText.top + textSize.cy;
		rcText.right = rcText.left + textSize.cx + 4;
		ExcludeClipRect(hdc, rcText.left, rcText.top, rcText.right, rcText.bottom);
	} else {
		SIZE textSize = { 0 };
		GetTextExtentPoint32(hdc, L"M", 1, &textSize);
		rcBackground.top += textSize.cy / 2;
	}

	RECT rcContent = rcBackground;
	GetThemeBackgroundContentRect(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, &rcBackground, &rcContent);
	ExcludeClipRect(hdc, rcContent.left, rcContent.top, rcContent.right, rcContent.bottom);

	DrawThemeParentBackground(hwnd, hdc, &rcClient);
	DrawThemeBackground(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, &rcBackground, nullptr);

	SelectClipRgn(hdc, nullptr);

	if (szText[0]) {
		rcText.right -= 2;
		rcText.left += 2;
		DTTOPTS dtto = { sizeof(DTTOPTS), DTT_TEXTCOLOR };
		dtto.crText = dark_text_color;
		DrawThemeTextEx(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, szText, -1, DT_LEFT | DT_SINGLELINE, &rcText, &dtto);
	}

	if (hCreatedFont) DeleteObject(hCreatedFont);
	SelectObject(hdc, hOldFont);
}

void SubclassButtonControl(HWND hwnd)
{
	if (g_darkModeSupported && g_darkModeEnabled) {
		SetWindowSubclass(hwnd, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {
			auto data = reinterpret_cast<SubClassData *>(dwRefData);
			switch (uMsg)
			{
				case WM_UPDATEUISTATE:
				if (HIWORD(wParam) & (UISF_HIDEACCEL | UISF_HIDEFOCUS))
				{
					InvalidateRect(hWnd, nullptr, FALSE);
				}
				break;
				case WM_NCDESTROY:
				{
					delete data;
				}
				break;
				case WM_ERASEBKGND:
				{
					if (g_darkModeEnabled && data->EnsureTheme(hWnd, L"Button")) {
						return TRUE;
					} else {
						break;
					}
				}
				case WM_THEMECHANGED:
					data->CloseTheme();
					break;
				case WM_PRINTCLIENT:
				case WM_PAINT:
					if (g_darkModeEnabled && data->EnsureTheme(hWnd, L"Button")) {
						DWORD nStyle = GetWindowLong(hWnd, GWL_STYLE);
						DWORD nButtonStyle = nStyle & 0xF;
						if (nButtonStyle == BS_CHECKBOX || nButtonStyle == BS_AUTOCHECKBOX || nButtonStyle == BS_RADIOBUTTON || nButtonStyle == BS_AUTORADIOBUTTON ||
							nButtonStyle == BS_3STATE || nButtonStyle == BS_AUTO3STATE || nButtonStyle == BS_GROUPBOX) {
							PAINTSTRUCT ps = { 0 };
							HDC hdc = reinterpret_cast<HDC>(wParam);
							if (!hdc) {
								hdc = BeginPaint(hWnd, &ps);
							}
							if (nButtonStyle == BS_GROUPBOX) {
								PaintGroupbox(hWnd, hdc, *data);
							} else {
								PaintButton(hWnd, hdc, *data);
							}
							if (ps.hdc) {
								EndPaint(hWnd, &ps);
							}
							return 0;
						}
					}
					break;
				case WM_SIZE:
				case WM_DESTROY:
					BufferedPaintStopAllAnimations(hWnd);
					break;
			}
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
			}, 0, reinterpret_cast<DWORD_PTR>(new SubClassData{}));
	}
	SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
}

void SubclassListViewControl(HWND hListView)
{
	if (g_darkModeSupported && g_darkModeEnabled) {
		HWND hHeader = ListView_GetHeader(hListView);

		SetWindowSubclass(hListView, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {
			switch (uMsg)
			{
				case WM_NOTIFY:
				{
					if (reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW) {
						LPNMCUSTOMDRAW nmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
						switch (nmcd->dwDrawStage)
						{
							case CDDS_PREPAINT:
								return CDRF_NOTIFYITEMDRAW;
							case CDDS_ITEMPREPAINT:
							{
								auto info = reinterpret_cast<SubClassData*>(dwRefData);
								SetTextColor(nmcd->hdc, info->headerTextColor);
								return CDRF_DODEFAULT;
							}
						}
					}
				}
				break;
				case WM_THEMECHANGED:
				{
					if (g_darkModeSupported) {
						HWND hHeader = ListView_GetHeader(hWnd);

						AllowDarkModeForWindow(hWnd, g_darkModeEnabled);
						if (hHeader) {
							AllowDarkModeForWindow(hHeader, g_darkModeEnabled);
						}

						HTHEME hTheme = OpenThemeData(nullptr, L"ItemsView");
						if (hTheme) {
							COLORREF color;
							if (SUCCEEDED(GetThemeColor(hTheme, 0, 0, TMT_TEXTCOLOR, &color)))
							{
								ListView_SetTextColor(hWnd, color);
							}
							if (SUCCEEDED(GetThemeColor(hTheme, 0, 0, TMT_FILLCOLOR, &color)))
							{
								ListView_SetTextBkColor(hWnd, color);
								ListView_SetBkColor(hWnd, color);
							}
							CloseThemeData(hTheme);
						}

						hTheme = OpenThemeData(hHeader, L"Header");
						if (hTheme) {
							auto info = reinterpret_cast<SubClassData*>(dwRefData);
							GetThemeColor(hTheme, HP_HEADERITEM, 0, TMT_TEXTCOLOR, &(info->headerTextColor));
							CloseThemeData(hTheme);
						}

						if (hHeader != NULL) {
							SendMessageW(hHeader, WM_THEMECHANGED, wParam, lParam);
						}

						RedrawWindow(hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
					}
				}
				break;
				case WM_DESTROY:
				{
					auto data = reinterpret_cast<SubClassData*>(dwRefData);
					delete data;
				}
				break;
			}
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
			}, 0, reinterpret_cast<DWORD_PTR>(new SubClassData{}));

		ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);

		// Hide focus dots
		SendMessage(hListView, WM_CHANGEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);

		if (hHeader != NULL) {
			SetWindowTheme(hHeader, L"ItemsView", nullptr); // DarkMode
		}
		SetWindowTheme(hListView, L"ItemsView", nullptr); // DarkMode
	}
}

void SubclassTreeViewControl(HWND hListView)
{
	if (g_darkModeSupported && g_darkModeEnabled) {
		SetWindowSubclass(hListView, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {
			switch (uMsg)
			{
				case WM_NOTIFY:
				{
					if (reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW && 0)
					{
						LPNMCUSTOMDRAW nmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
						switch (nmcd->dwDrawStage)
						{
							case CDDS_PREPAINT:
								return CDRF_NOTIFYITEMDRAW;
							case CDDS_ITEMPREPAINT:
							{
								auto info = reinterpret_cast<SubClassData*>(dwRefData);
								SetTextColor(nmcd->hdc, info->headerTextColor);
								return CDRF_DODEFAULT;
							}
						}
					}
				}
				break;
				case WM_THEMECHANGED:
				{
					if (g_darkModeSupported) {
						AllowDarkModeForWindow(hWnd, g_darkModeEnabled);

						HTHEME hTheme = OpenThemeData(nullptr, L"ItemsView");
						if (hTheme) {
							COLORREF color;
							if (SUCCEEDED(GetThemeColor(hTheme, 0, 0, TMT_TEXTCOLOR, &color))) {
								TreeView_SetTextColor(hWnd, color);
							}
							if (SUCCEEDED(GetThemeColor(hTheme, 0, 0, TMT_FILLCOLOR, &color))) {
								TreeView_SetBkColor(hWnd, color);
							}
							CloseThemeData(hTheme);
						}

						RedrawWindow(hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
					}
				}
				break;
				case WM_DESTROY:
				{
					auto data = reinterpret_cast<SubClassData*>(dwRefData);
					delete data;
				}
				break;
			}
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
			}, 0, reinterpret_cast<DWORD_PTR>(new SubClassData{}));

		SendMessageW(hListView, WM_THEMECHANGED, 0, 0);
	}
}

void SubClassStatusBar(HWND hwnd)
{
	if (g_darkModeSupported && g_darkModeEnabled) {
		SetWindowSubclass(hwnd, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {
			switch (uMsg)
			{
				case WM_ERASEBKGND:
				{
					if (g_darkModeEnabled) {
						HDC hdc = (HDC)wParam;
						HBRUSH brush = CreateSolidBrush(dark_bg_color);
						RECT rect;
						GetClientRect(hWnd, &rect);
						FillRect(hdc, &rect, brush);
						DeleteObject(brush);
						return TRUE;
					} else {
						break;
					}
				}
			}
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
			}, 0, reinterpret_cast<DWORD_PTR>(new SubClassData{}));
	}
	SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
}
