 /*
  * WinUAE GUI debugger
  *
  * Copyright 2007 Karsten Bock
  * Copyright 2007 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <string.h>
#include <windows.h>
#include "resource.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "debug_win32.h"
#include "win32.h"
#include "win32gui.h"

#include "uae.h"

static HWND hDbgWnd = 0;
static HWND hOutput = 0;
static HACCEL dbgaccel = 0;

extern int consoleopen;
extern uae_u32 get_fpsr();

static char linebreak[] = {'\r', '\n', '\0'};

#define MAXLINES 250
#define MAXINPUTHIST 50
#define MAXPAGECONTROLS 5
#define MAXPAGES 10

static int inputfinished = 0;

static FARPROC OldInputProc, OldMemInputProc;
static WORD* dlgtmpl;
static int reopen;

struct histnode {
    char *command;
	struct histnode *prev;
	struct histnode *next;
};

static struct histnode *firsthist, *lasthist, *currhist;
static int histcount;

struct debuggerpage {
    HWND ctrl[MAXPAGECONTROLS];
    uae_u32 addr;
    char addrinput[9];
    int init;
};
static struct debuggerpage dbgpage[MAXPAGES];
static int currpage, pages;
static int pagetype;

static void OutputCurrHistNode(HWND hWnd)
{
    int txtlen;
    char *buf;

    if (currhist->command) {
        txtlen = GetWindowTextLength(hWnd);
        buf = malloc(txtlen + 1);
        GetWindowText(hWnd, buf, txtlen + 1);
        if (strcmp(buf, currhist->command)) {
            SetWindowText(hWnd, currhist->command);
            txtlen = strlen(currhist->command);
            SendMessage(hWnd, EM_SETSEL, (WPARAM)txtlen, (LPARAM)txtlen);
            SendMessage(hWnd, EM_SETSEL, -1, -1);
        }
    }
}

static void SetPrevHistNode(HWND hWnd)
{
    if (currhist) {
        if (currhist->prev)
            currhist = currhist->prev;
        OutputCurrHistNode(hWnd);

    }
    else if (lasthist) {
        currhist = lasthist;
        OutputCurrHistNode(hWnd);
    }
}

static void SetNextHistNode(HWND hWnd)
{
    if (currhist) {
        if (currhist->next)
            currhist = currhist->next;
        OutputCurrHistNode(hWnd);
    }
}

static void DeleteFromHistory(int count)
{
    int i;
    struct histnode *tmp;

    for (i = 0; i < count && histcount; i++) {
        tmp = firsthist;
        firsthist = firsthist->next;
        if (currhist == tmp)
            currhist = NULL;
        if (lasthist == tmp)
            lasthist = NULL;
        if (firsthist)
            firsthist->prev = NULL;
        free(tmp->command);
        free(tmp);
        histcount--;
    }
}

static void AddToHistory(const char *command)
{
    struct histnode *tmp;

    if (histcount > 0 && !strcmp(command, lasthist->command))
        return;
    else if (histcount == MAXINPUTHIST) {
        DeleteFromHistory(1);
    }
    tmp = lasthist;
    lasthist = malloc(sizeof(struct histnode));
    if (histcount == 0)
        firsthist = lasthist;
    lasthist->command = strdup(command);
    lasthist->next = NULL;
    lasthist->prev = tmp;
    if (tmp)
        tmp->next = lasthist;
    histcount++;
    currhist = NULL;
}

int GetInput (char *out, int maxlen)
{
    HWND hInput;
    int chars;

    if (!hDbgWnd)
        return 0;
    inputfinished = 0;
    hInput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
    chars = GetWindowText(hInput, out, maxlen);
    if (chars == 0)
        return 0;
    WriteOutput(linebreak + 1, 2);
    WriteOutput(out, strlen(out));
    WriteOutput(linebreak + 1, 2);
    AddToHistory(out);
    SetWindowText(hInput, "");
    return chars;
}

static int CheckLineLimit(HWND hWnd, const char *out)
{
    char *tmp, *p;
    int lines_have, lines_new = 0, lastchr, txtlen, visible;

    tmp = (char *)out;
    lines_have = SendMessage(hWnd, EM_GETLINECOUNT, 0, 0);
    while (strlen(tmp) > 0 && (p = strchr(tmp, '\n')) > 0) {
        lines_new++;
        tmp = p + 1;
    }
    lines_new++;
    if (lines_new > MAXLINES)
        return 0;
    if (lines_have + lines_new > MAXLINES) {
        visible = IsWindowVisible(hWnd);
        if (visible)
            SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
        lastchr = SendMessage(hWnd, EM_LINEINDEX, lines_have + lines_new - MAXLINES, 0);
        SendMessage(hWnd, EM_SETSEL, 0, lastchr);
        SendMessage(hWnd, EM_REPLACESEL, FALSE, (LPARAM)"");
        txtlen = GetWindowTextLength(hWnd);
        SendMessage(hWnd, EM_SETSEL, (WPARAM)txtlen, (LPARAM)txtlen);
        SendMessage(hWnd, EM_SETSEL, -1, -1);
        if (visible)
            SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
    }
    return 1;
}

void WriteOutput(const char *out, int len)
{
    int txtlen, pos = 0, count, index, leave = 0;
    char *buf = 0, *p, *tmp;

    if (!hOutput || !strcmp(out, ">") || len == 0)
        return;
    if (!CheckLineLimit(hOutput, out))
        return;
    tmp = (char *)out;
    for(;;) {
        p = strchr(tmp, '\n');
        if (p) {
            pos = p - tmp + 1;
            if (pos > (MAX_LINEWIDTH + 1))
                pos = MAX_LINEWIDTH + 1;
            buf = xmalloc(pos + 2);
            memset(buf, 0, pos + 2);
            strncpy(buf, tmp, pos - 1);
            strcat(buf, linebreak);
        } else if (strlen(tmp) == 0) {
            leave = 1;
	} else {
            count = SendMessage(hOutput, EM_GETLINECOUNT, 0, 0);
            index = SendMessage(hOutput, EM_LINEINDEX, count - 1, 0);
            txtlen = SendMessage(hOutput, EM_LINELENGTH, index, 0);
            if (strlen(tmp) + txtlen > MAX_LINEWIDTH) {
                buf = xmalloc(MAX_LINEWIDTH + 3 - txtlen);
                memset(buf, 0, MAX_LINEWIDTH + 3 - txtlen);
                strncpy(buf, tmp, MAX_LINEWIDTH - txtlen);
                strcat(buf, linebreak);
            }
            leave = 1;
        }
        txtlen = GetWindowTextLength(hOutput);
        SendMessage(hOutput, EM_SETSEL, (WPARAM)txtlen, (LPARAM)txtlen);
        SendMessage(hOutput, EM_REPLACESEL, FALSE, (LPARAM)(buf ? buf : tmp));
        if (buf) {
           xfree(buf);
           buf = 0;
           tmp += pos;
        }
        if (leave)
            return;
    }
}

static void UpdateListboxString(HWND hWnd, int pos, char *out, int mark)
{
    int count;
    char text[MAX_LINEWIDTH + 1];
    COLORREF cr;

    if (!IsWindowEnabled(hWnd))
        return;
    if (strlen(out) > MAX_LINEWIDTH)
        out[MAX_LINEWIDTH] = '\0';
    cr = GetSysColor(COLOR_WINDOWTEXT);
    count = SendMessage(hWnd, (UINT) LB_GETCOUNT, 0, 0);
    if (pos < count) {
        memset(text, 0, MAX_LINEWIDTH + 1);
        SendMessage(hWnd, LB_GETTEXT, pos, (LPARAM)((LPTSTR)text));
        if (strcmp(out, text) != 0 && mark)
            cr = GetSysColor(COLOR_HIGHLIGHT);
        SendMessage(hWnd, LB_DELETESTRING, pos, 0);
    }
    SendMessage(hWnd, LB_INSERTSTRING, pos, (LPARAM)out);
    SendMessage(hWnd, LB_SETITEMDATA, pos, cr);
}

static int GetLBOutputLines(HWND hWnd)
{
    int lines = 0, clientsize, itemsize;
    RECT rc;

    GetClientRect(hWnd, &rc);
    clientsize = rc.bottom - rc.top;
    itemsize = SendMessage(hWnd, LB_GETITEMHEIGHT, 0, 0);  
    while (clientsize > itemsize) {
        lines ++;
        clientsize -= itemsize;
    }
    return lines;
}

static void ShowMem(int offset)
{
    uae_u32 addr;
    int i;
    char out[MAX_LINEWIDTH + 1];
    HWND hMemory;

    dbgpage[currpage].addr += offset;
    addr = dbgpage[currpage].addr;
    hMemory = GetDlgItem(hDbgWnd, IDC_DBG_MEM);
    for (i = 0; i < GetLBOutputLines(hMemory); i++) {
        addr = dumpmem2(addr, out, sizeof(out));
        UpdateListboxString(hMemory, i, out, FALSE);
    }
}

static int GetPrevAddr(uae_u32 addr, uae_u32 *prevaddr)
{
    uae_u32 dasmaddr, next;

    *prevaddr = addr;
    dasmaddr = addr - 20;
    while (dasmaddr < addr) {
	next = dasmaddr + 2;
        m68k_disasm_2(NULL, 0, dasmaddr, &next, 1, NULL, NULL, 0);
        if (next == addr) {
            *prevaddr = dasmaddr;
            return 1;
        }
        dasmaddr = next;
    }
    return 0;
}

static void ShowDasm(int direction)
{
    uae_u32 addr = 0, prev;
    int i;
    char out[MAX_LINEWIDTH + 1], *p;
    HWND hDasm;

    hDasm = GetDlgItem(hDbgWnd, IDC_DBG_DASM);
    if (!dbgpage[currpage].init) {
        addr = m68k_getpc(&regs);
        dbgpage[currpage].init = 1;
    }
    else
        addr = dbgpage[currpage].addr;
    if (direction > 0) {
        m68k_disasm_2(NULL, 0, addr, &addr, 1, NULL, NULL, 0);
        if (!addr || addr < dbgpage[currpage].addr)
            addr = dbgpage[currpage].addr;
    }
    else if (direction < 0 && addr > 0) {
        if (GetPrevAddr(addr, &prev))
            addr = prev;
        else
            addr -= 2;
    }
    if (addr % 2)
        return;
    dbgpage[currpage].addr = addr;
    for (i = 0; i < GetLBOutputLines(hDasm); i++) {
        m68k_disasm_2(out, sizeof(out), addr, &addr, 1, NULL, NULL, 0);
        p = strchr(out, '\n');
        if (p)
            *p = '\0';
        if (addr > dbgpage[currpage].addr)
            UpdateListboxString(hDasm, i, out, FALSE);
        else
            UpdateListboxString(hDasm, i, "", FALSE);
    }
}

static void SetMemToPC(void)
{
    int i, id;

    dbgpage[currpage].addr = m68k_getpc(&regs);
    sprintf(dbgpage[currpage].addrinput, "%08lX", dbgpage[currpage].addr);
    for (i = 0; i < MAXPAGECONTROLS; i++) {
        id = GetDlgCtrlID(dbgpage[currpage].ctrl[i]);
        if (id == IDC_DBG_MEMINPUT)
            SetWindowText(dbgpage[currpage].ctrl[i], dbgpage[currpage].addrinput);
    }
    ShowDasm(0);
}

static void ShowPage(int index, int force)
{
    int i, id;

    if (index >= pages || ((index == currpage) && !force))
        return;
    if (currpage >= 0) {
        for (i = 0; i < MAXPAGECONTROLS; i++) {
            if (dbgpage[currpage].ctrl[i]) {
                id = GetDlgCtrlID(dbgpage[currpage].ctrl[i]);
                if (id == IDC_DBG_MEMINPUT)
                    GetWindowText(dbgpage[currpage].ctrl[i], dbgpage[currpage].addrinput, 9);
                ShowWindow(dbgpage[currpage].ctrl[i], SW_HIDE);
            }
        }
    }
    pagetype = 0;
    currpage = index;
    for (i = 0; i < MAXPAGECONTROLS; i++) {
        if (dbgpage[index].ctrl[i]) {
            id = GetDlgCtrlID(dbgpage[index].ctrl[i]);
	    if (id == IDC_DBG_OUTPUT1 || id == IDC_DBG_OUTPUT2) {
                hOutput = dbgpage[index].ctrl[i];
	    } else if (id == IDC_DBG_MEM) {
                ShowMem(0);
		pagetype = id;
	    } else if (id == IDC_DBG_DASM) {
                ShowDasm(0);
		pagetype = id;
	    } else if (id == IDC_DBG_MEMINPUT) {
                SetWindowText(dbgpage[index].ctrl[i], dbgpage[index].addrinput);
	    }
	    ShowWindow(dbgpage[index].ctrl[i], SW_SHOW);
        }
    }
}

static void AddPage(int *iddata) // iddata[0] = idcount!
{
    int i;

    if (pages >= MAXPAGES)
        return;
    memset(&dbgpage[pages], 0, sizeof(struct debuggerpage));
    for (i = 0; i < iddata[0]; i++) {
        dbgpage[pages].ctrl[i] = GetDlgItem(hDbgWnd, iddata[i + 1]);
        ShowWindow(dbgpage[pages].ctrl[i], SW_HIDE);
    }
    pages++;
}

static void InitPages(void)
{
    int i;

    int dpage[][MAXPAGECONTROLS + 1] = {
        { 1, IDC_DBG_OUTPUT1 },
        { 1, IDC_DBG_OUTPUT2 },
        { 4, IDC_DBG_MEM, IDC_DBG_MEMINPUT, IDC_DBG_MEMUP, IDC_DBG_MEMDOWN },
        { 4, IDC_DBG_MEM, IDC_DBG_MEMINPUT, IDC_DBG_MEMUP, IDC_DBG_MEMDOWN },
        { 5, IDC_DBG_DASM, IDC_DBG_MEMINPUT, IDC_DBG_MEMTOPC },
        { 5, IDC_DBG_DASM, IDC_DBG_MEMINPUT, IDC_DBG_MEMTOPC }
    };

    pages = 0;
    for (i = 0; i < (sizeof(dpage) / sizeof(dpage[0])); i++)
        AddPage(dpage[i]);
}

static LRESULT CALLBACK InputProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
     switch (message) {
        case WM_KEYUP:
            switch (wParam) {
                case VK_RETURN:
                    inputfinished = 1;
                    break;
                case VK_UP: 
                    SetPrevHistNode(hWnd);
                    return TRUE;
                case VK_DOWN:
                    SetNextHistNode(hWnd);
                    return TRUE;
            }
            break;
    }
    return CallWindowProc((WNDPROC)OldInputProc, hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK MemInputProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HANDLE hdata;
    LPTSTR lptstr;
    char allowed[] = "1234567890abcdefABCDEF";
    int ok = 1;
    char addrstr[11];
    uae_u32 addr;

    switch (message) {
        case WM_CHAR:
            if (wParam == VK_BACK)
                break;
            if (!strchr(allowed, wParam))
                return TRUE;
            break;
        case WM_PASTE:
            if (!OpenClipboard(NULL))
                return TRUE;
            hdata = GetClipboardData(CF_TEXT);
            if (hdata) {
                lptstr = GlobalLock(hdata); 
                if (lptstr) {
                    if (strspn(lptstr, allowed) != strlen(lptstr))
                        ok = 0;
                    GlobalUnlock(hdata);
                }
            }
            CloseClipboard();
            if (!ok)
                return TRUE;
            break;
        case WM_KEYUP:
             switch (wParam) {
                case VK_RETURN:
                    sprintf(addrstr, "0x");
                    GetWindowText(hWnd, addrstr + 2, 9);
                    addr = strtoul(addrstr, NULL, 0);
                    dbgpage[currpage].addr = addr;
                    ShowPage(currpage, TRUE);
                    break;
            }
            break;
    }
    return CallWindowProc((WNDPROC)OldMemInputProc, hWnd, message, wParam, lParam);
}

static void moveupdown(int dir)
{
    if (pagetype == IDC_DBG_MEM) {
	if (dir > 1 || dir < -1)
	    dir *= 4;
	ShowMem(dir * 16);
    } else if (pagetype == IDC_DBG_DASM) {
	if (dir > 1 || dir < -1)
	    dir *= 4;
	while(dir) {
	    ShowDasm(dir > 0 ? 1 : -1);
	    if (dir > 0)
		dir--;
	    if (dir < 0)
		dir++;
	}
    }
}

static int width_adjust, height_adjust;
static RECT dlgRect;

static void adjustitem(HWND hwnd, int x, int y, int w, int h)
{
    WINDOWINFO pwi;
    RECT *r;
    pwi.cbSize = sizeof pwi;
    GetWindowInfo(hwnd, &pwi);
    r = &pwi.rcWindow;
    r->bottom -= r->top;
    r->right -= r->left;
    r->left -= dlgRect.left;
    r->top -= dlgRect.top;
    r->left += x;
    r->top += y;
    r->right += w;
    r->bottom += h;
    MoveWindow(hwnd, r->left, r->top, r->right, r->bottom, TRUE);
}

static BOOL CALLBACK childenumproc (HWND hwnd, LPARAM lParam)
{
    int id1y[] = { IDC_DBG_OUTPUT1, IDC_DBG_OUTPUT2, IDC_DBG_MEM, IDC_DBG_DASM, -1 };
    int id2y[] = { IDC_DBG_INPUT, IDC_DBG_HELP, -1 };

    int id1x[] = { IDC_DBG_OUTPUT1, IDC_DBG_OUTPUT2, IDC_DBG_MEM, IDC_DBG_DASM,
	IDC_DBG_AMEM, IDC_DBG_PREFETCH, IDC_DBG_INPUT, -1 };
    int id2x[] = { IDC_DBG_HELP, IDC_DBG_CCR, IDC_DBG_SP_VBR, IDC_DBG_MISC,
	IDC_DBG_FPREG, IDC_DBG_FPSR, -1 };

    int dlgid, j;

    dlgid = GetDlgCtrlID(hwnd);

    j = 0;
    while (id1y[j] >= 0) {
	if (id1y[j] == dlgid)
	    adjustitem(hwnd, 0, 0, 0, height_adjust);
	j++;
    }
    j = 0;
    while (id2y[j] >= 0) {
	if (id2y[j] == dlgid)
	    adjustitem(hwnd, 0, height_adjust, 0, 0);
	j++;
    }
    j = 0;
    while (id1x[j] >= 0) {
	if (id1x[j] == dlgid)
	    adjustitem(hwnd, 0, 0, width_adjust,0);
	j++;
    }
    j = 0;
    while (id2x[j] >= 0) {
	if (id2x[j] == dlgid)
	    adjustitem(hwnd, width_adjust,0, 0, 0);
	j++;
    }
    return TRUE;
}

static void AdjustDialog(HWND hDlg)
{
    RECT r, r2;
    GetClientRect(hDlg, &r);
    width_adjust = (r.right - r.left) - (dlgRect.right - dlgRect.left);
    height_adjust = (r.bottom - r.top) - (dlgRect.bottom - dlgRect.top);
    GetWindowRect(hDlg, &dlgRect);
    r2.left = r2.top = r2.right = r2.bottom = 0;
    AdjustWindowRect(&r2, WS_POPUP | WS_CAPTION | WS_THICKFRAME, FALSE);
    dlgRect.left -= r2.left;
    dlgRect.top -= r2.top;
    EnumChildWindows (hDlg, &childenumproc, 0);
    dlgRect = r;
}


static LRESULT CALLBACK DebuggerProc (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int len;
    HWND hwnd;
    DRAWITEMSTRUCT *pdis;
    HDC hdc;
    RECT rc;
    char text[MAX_LINEWIDTH + 1];
    HFONT hfont, hfontold;
    TEXTMETRIC tm;

    switch (message) {
        case WM_INITDIALOG:
	{
	    int newpos = 0;
	    LONG x, y, w, h;
	    DWORD regkeytype;
	    DWORD regkeysize = sizeof(LONG);
	    GetClientRect(hDlg, &dlgRect);
	    if (hWinUAEKey) {
		newpos = 1;
		if (RegQueryValueEx (hWinUAEKey, "DebuggerPosX", 0, &regkeytype, (LPBYTE)&x, &regkeysize) != ERROR_SUCCESS)
		    newpos = 0;
		if (RegQueryValueEx (hWinUAEKey, "DebuggerPosY", 0, &regkeytype, (LPBYTE)&y, &regkeysize) != ERROR_SUCCESS)
		    newpos = 0;
		if (RegQueryValueEx (hWinUAEKey, "DebuggerPosW", 0, &regkeytype, (LPBYTE)&w, &regkeysize) != ERROR_SUCCESS)
		    newpos = 0;
		if (RegQueryValueEx (hWinUAEKey, "DebuggerPosH", 0, &regkeytype, (LPBYTE)&h, &regkeysize) != ERROR_SUCCESS)
		    newpos = 0;
	    }
	    if (newpos) {
		RECT rc;
		rc.left = x;
		rc.top = y;
		rc.right = x + w;
		rc.bottom = y + h;
		if (MonitorFromRect (&rc, MONITOR_DEFAULTTONULL) != NULL)
		    SetWindowPos(hDlg, 0, x, y, w, h, SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOACTIVATE | SWP_DEFERERASE);
	    }
	    SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon (GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON)));
            hwnd = GetDlgItem(hDlg, IDC_DBG_INPUT);
            OldInputProc = (FARPROC)SetWindowLong(hwnd, GWL_WNDPROC, (DWORD)InputProc);
            SendMessage(hwnd, EM_LIMITTEXT, MAX_LINEWIDTH, 0);
            hwnd = GetDlgItem(hDlg, IDC_DBG_MEMINPUT);
            OldMemInputProc = (FARPROC)SetWindowLong(hwnd, GWL_WNDPROC, (DWORD)MemInputProc);
            SendMessage(hwnd, EM_LIMITTEXT, 8, 0);
            if (!currprefs.cpu_compatible) {
                hwnd = GetDlgItem(hDlg, IDC_DBG_PREFETCH);
                EnableWindow(hwnd, FALSE);
            }
            if (currprefs.cpu_model < 68020) {
                hwnd = GetDlgItem(hDlg, IDC_DBG_FPREG);
                EnableWindow(hwnd, FALSE);
                hwnd = GetDlgItem(hDlg, IDC_DBG_FPSR);
                EnableWindow(hwnd, FALSE);
            }
            currpage = -1;
            firsthist = lasthist = currhist = NULL;
            histcount = 0;
            inputfinished = 0;
	    AdjustDialog(hDlg);
            return TRUE;
	}
        case WM_CLOSE:
            DestroyWindow(hDlg);
	    uae_quit();
            return TRUE;
        case WM_DESTROY:
	{
	    RECT r;
	    if (GetWindowRect (hDlg, &r) && hWinUAEKey) {
		r.right -= r.left;
		r.bottom -= r.top;
	        RegSetValueEx (hWinUAEKey, "DebuggerPosX", 0, REG_DWORD, (LPBYTE)&r.left, sizeof(LONG));
	        RegSetValueEx (hWinUAEKey, "DebuggerPosY", 0, REG_DWORD, (LPBYTE)&r.top, sizeof(LONG));
	        RegSetValueEx (hWinUAEKey, "DebuggerPosW", 0, REG_DWORD, (LPBYTE)&r.right, sizeof(LONG));
	        RegSetValueEx (hWinUAEKey, "DebuggerPosH", 0, REG_DWORD, (LPBYTE)&r.bottom, sizeof(LONG));
	    }
            hDbgWnd = 0;
            PostQuitMessage(0);
	    DeleteFromHistory(histcount);
	    consoleopen = 0;
            return TRUE;
	}
	case WM_GETMINMAXINFO:
	{
	    MINMAXINFO *mmi = (MINMAXINFO*)lParam;
	    mmi->ptMinTrackSize.x = 640;
	    mmi->ptMinTrackSize.y = 480;
	    return TRUE;
	}
	case WM_EXITSIZEMOVE:
	{
	    AdjustDialog(hDlg);
	    return TRUE;
	}

        case WM_CTLCOLORSTATIC:
            SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        case WM_CTLCOLORLISTBOX:
            hwnd = (HWND)lParam;
            if (!IsWindowEnabled(hwnd)) {
                SetBkColor((HDC)wParam, GetSysColor(COLOR_3DFACE));
                return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
            }
            SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_DBG_HELP:
                    WriteOutput(linebreak + 1, 2);
                    debug_help();
                    return TRUE;
                case ID_DBG_PAGE1:
                case ID_DBG_PAGE2:
                case ID_DBG_PAGE3:
                case ID_DBG_PAGE4:
                case ID_DBG_PAGE5:
                case ID_DBG_PAGE6:
                    // IDs have to be consecutive and in order of page order for this to work
                    ShowPage(LOWORD(wParam) - ID_DBG_PAGE1, FALSE);
                    return TRUE;
                case IDC_DBG_MEMUP:
		    moveupdown(-1);
		    return TRUE;
                case IDC_DBG_MEMDOWN:
                    moveupdown(1);
                    return TRUE;
                case IDC_DBG_MEMUPFAST:
		    moveupdown(-2);
		    return TRUE;
                case IDC_DBG_MEMDOWNFAST:
                    moveupdown(2);
                    return TRUE;
                case IDC_DBG_MEMTOPC:
                    SetMemToPC();
                    return TRUE;
            }
            break;
        case WM_MEASUREITEM:
            hdc = GetDC(hDlg);
            hfont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
            hfontold = (HFONT)SelectObject(hdc, hfont);
            GetTextMetrics(hdc, &tm);
            ((MEASUREITEMSTRUCT*)(lParam))->itemHeight = tm.tmHeight + tm.tmExternalLeading;
            SelectObject(hdc, hfontold);
            ReleaseDC(hDlg, hdc);
            return TRUE;
        case WM_DRAWITEM:
            pdis = (DRAWITEMSTRUCT *)lParam;
            hdc = pdis->hDC;
            rc = pdis->rcItem;
            SetBkMode(hdc, TRANSPARENT);
            if (pdis->itemID < 0) {
                if (pdis->itemAction & ODA_FOCUS)
                    DrawFocusRect(hdc, &rc);
                return TRUE;
            }
            memset(text, MAX_LINEWIDTH + 1, 0);
            len = SendMessage(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)(LPSTR)text);
            if (!IsWindowEnabled(pdis->hwndItem)) {
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
                SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
            }
            else {
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
                SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            }
            SetTextColor(hdc, pdis->itemData);
            if (len > 0)
                TextOut(hdc, rc.left, rc.top, text, strlen(text));
            if ((pdis->itemState) & (ODS_FOCUS))
               DrawFocusRect(hdc, &rc);
            return TRUE;
    }
    return FALSE;
}

int open_debug_window(void)
{

    struct newresource *nr;

    if (hDbgWnd)
        return 0;
    reopen = 0;
    dbgaccel = LoadAccelerators(hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDR_DBGACCEL));
    nr = getresource(IDD_DEBUGGER);
    if (nr) {
        hDbgWnd = CreateDialogIndirect (nr->inst, nr->resource, NULL, DebuggerProc);
        freescaleresource(nr);
    }
    if (!hDbgWnd)
        return 0;
    InitPages();
    ShowPage(0, TRUE);
    ShowWindow(hDbgWnd, SW_SHOWNORMAL);
    UpdateWindow(hDbgWnd);
    update_debug_info();
    return 1;
}

void close_debug_window(void)
{
    DestroyWindow(hDbgWnd);
 }

int console_get_gui (char *out, int maxlen)
{
    MSG msg;
    int ret;

    while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0) {
	if (!debugger_active || ret == -1) {
	    return -1;
	} else if (!IsWindow(hDbgWnd) || !TranslateAccelerator(hDbgWnd, dbgaccel, &msg) || !IsDialogMessage(hDbgWnd, &msg)) {
	    TranslateMessage(&msg);
	    DispatchMessage(&msg);
	}
	if (inputfinished)
	    return GetInput(out, maxlen);
    }
    return 0;
}

void update_debug_info(void)
{
    int i;
    char out[MAX_LINEWIDTH + 1];
    HWND hwnd;
    struct instr *dp;
    struct mnemolookup *lookup1, *lookup2;
    uae_u32 fpsr;

    if (!hDbgWnd)
        return;
    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_DREG);
    for (i = 0; i < 8; i++) {
        sprintf(out, "D%d: %08lX", i, m68k_dreg(&regs, i));
        UpdateListboxString(hwnd, i, out, TRUE);
    }

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_AREG);
    for (i = 0; i < 8; i++) {
        hwnd = GetDlgItem(hDbgWnd, IDC_DBG_AREG);
        sprintf(out, "A%d: %08lX", i, m68k_areg(&regs, i));
        UpdateListboxString(hwnd, i, out, TRUE);
        hwnd = GetDlgItem(hDbgWnd, IDC_DBG_AMEM);
        dumpmem2(m68k_areg(&regs, i), out, sizeof(out));
        UpdateListboxString(hwnd, i, out + 9, TRUE);
    }

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_CCR);
    UpdateListboxString(hwnd, 0, GET_XFLG(&regs.ccrflags) ? "X: 1" : "X: 0", TRUE);
    UpdateListboxString(hwnd, 1, GET_NFLG(&regs.ccrflags) ? "N: 1" : "N: 0", TRUE);
    UpdateListboxString(hwnd, 2, GET_ZFLG(&regs.ccrflags) ? "Z: 1" : "Z: 0", TRUE);
    UpdateListboxString(hwnd, 3, GET_VFLG(&regs.ccrflags) ? "V: 1" : "V: 0", TRUE);
    UpdateListboxString(hwnd, 4, GET_CFLG(&regs.ccrflags) ? "C: 1" : "C: 0", TRUE);

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_SP_VBR);
    sprintf(out, "USP: %08lX", regs.usp);
    UpdateListboxString(hwnd, 0, out, TRUE);
    sprintf(out, "ISP: %08lX", regs.isp);
    UpdateListboxString(hwnd, 1, out, TRUE);
    sprintf(out, "MSP: %08lX", regs.msp);
    UpdateListboxString(hwnd, 2, out, TRUE);
    sprintf(out, "VBR: %08lX", regs.vbr);
    UpdateListboxString(hwnd, 3, out, TRUE);

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_MISC);
    sprintf(out, "T:     %d%d", regs.t1, regs.t0);
    UpdateListboxString(hwnd, 0, out, TRUE);
    sprintf(out, "S:     %d", regs.s);
    UpdateListboxString(hwnd, 1, out, TRUE);
    sprintf(out, "M:     %d", regs.m);
    UpdateListboxString(hwnd, 2, out, TRUE);
    sprintf(out, "IMASK: %d", regs.intmask);
    UpdateListboxString(hwnd, 3, out, TRUE);
    sprintf(out, "STP:   %d", regs.stopped);
    UpdateListboxString(hwnd, 4, out, TRUE);

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_PC);
    sprintf(out, "PC: %08lX", m68k_getpc(&regs));
    UpdateListboxString(hwnd, 0, out, TRUE);

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_PREFETCH);
    dp = table68k + regs.irc;
    for (lookup1 = lookuptab; lookup1->mnemo != dp->mnemo; lookup1++);
    dp = table68k + regs.ir;
    for (lookup2 = lookuptab; lookup2->mnemo != dp->mnemo; lookup2++);
    sprintf(out, "Prefetch: %04X (%s) %04X (%s)", regs.irc, lookup1->name, regs.ir, lookup2->name);
    UpdateListboxString(hwnd, 0, out, TRUE);

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_FPREG);
    for (i = 0; i < 8; i++) {
        sprintf(out, "FP%d: %g", i, regs.fp[i]);
        UpdateListboxString(hwnd, i, out, TRUE);
    }

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_FPSR);
    fpsr = get_fpsr();
    UpdateListboxString(hwnd, 0, ((fpsr & 0x8000000) != 0) ? "N:   1" : "N:   0", TRUE);
    UpdateListboxString(hwnd, 1, ((fpsr & 0x4000000) != 0) ? "Z:   1" : "Z:   0", TRUE);
    UpdateListboxString(hwnd, 2, ((fpsr & 0x2000000) != 0) ? "I:   1" : "I:   0", TRUE);
    UpdateListboxString(hwnd, 3, ((fpsr & 0x1000000) != 0) ? "NAN: 1" : "NAN: 0", TRUE);

    ShowPage(currpage, TRUE);
}
