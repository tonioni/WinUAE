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
#include <commctrl.h>
#include "resource.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cia.h"
#include "disk.h"
#include "debug.h"
#include "identify.h"
#include "savestate.h"
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
#define CLASSNAMELENGTH 50

static int inputfinished = 0;

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

char *pname[] = { "OUT1", "OUT2", "MEM1", "MEM2", "DASM1", "DASM2", "BRKPTS", "MISC", "CUSTOM" };
static int pstatuscolor[MAXPAGES];

static int dbgwnd_minx = 800, dbgwnd_miny = 600;

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

static HWND ulbs_hwnd;
static int ulbs_pos;
static void UpdateListboxString(HWND hWnd, int pos, char *out, int mark)
{
    int count;
    char text[MAX_LINEWIDTH + 1], *p;
    COLORREF cr;

    if (!IsWindowEnabled(hWnd)) {
        p = strchr(out, ':');
        if (p)
            *(p + 1) = '\0';
    }
    if (strlen(out) > MAX_LINEWIDTH)
        out[MAX_LINEWIDTH] = '\0';
    p = strchr(out, '\n');
    if (p)
        *p = '\0';
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
static void ULBSINIT(HWND hwnd)
{
    ulbs_hwnd = hwnd;
    ulbs_pos = 0;
}
static void ULBS(const char *format, ...)
{
    char buffer[MAX_LINEWIDTH + 1];
    va_list parms;
    va_start(parms, format);
    _vsnprintf(buffer, MAX_LINEWIDTH, format, parms);
    UpdateListboxString(ulbs_hwnd, ulbs_pos++, buffer, FALSE);
}
static void ULBST(const char *format, ...)
{
    char buffer[MAX_LINEWIDTH + 1];
    va_list parms;
    va_start(parms, format);
    _vsnprintf(buffer, MAX_LINEWIDTH, format, parms);
    UpdateListboxString(ulbs_hwnd, ulbs_pos++, buffer, TRUE);
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

static void ShowMiscCPU(HWND hwnd)
{
    int line = 0;
    char out[MAX_LINEWIDTH + 1];
    int i;

    for (i = 0; m2cregs[i].regno>= 0; i++) {
	if (!movec_illg(m2cregs[i].regno)) {
	    sprintf(out, "%-4s %08.8X", m2cregs[i].regname, val_move2c(m2cregs[i].regno));
	    UpdateListboxString(hwnd, line++, out, TRUE);
	}
    }
}

static int dcustom[] = {
    0x02, 0x9a, 0x9c, 0x8080, 0x8084, 0x8e, 0x90, 0x92, 0x94,
    0x100, 0x102, 0x104, 0x106, 0x10c, 0
};
static uae_u32 gw(uae_u8 *p, int off)
{
    return (p[off] << 8) | p[off + 1];
}
static void ShowCustomSmall(HWND hwnd)
{
    int len, i, j, cnt;
    uae_u8 *p1, *p2, *p3, *p4;
    char out[MAX_LINEWIDTH + 1];

    p1 = p2 = save_custom (&len, 0, 1);
    p1 += 4; // skip chipset type
    for (i = 0; i < 4; i++) {
	p4 = p1 + 0xa0 + i * 16;
	p3 = save_audio (i, &len, 0);
	p4[0] = p3[12];
	p4[1] = p3[13];
	p4[2] = p3[14];
	p4[3] = p3[15];
	p4[4] = p3[4];
	p4[5] = p3[5];
	p4[6] = p3[8];
	p4[7] = p3[9];
	p4[8] = 0;
	p4[9] = p3[1];
	p4[10] = p3[10];
	p4[11] = p3[11];
	free (p3);
    }
    ULBSINIT(hwnd);
    cnt = 0;
    sprintf(out, "CPU %d", currprefs.cpu_model);
    if (currprefs.fpu_model)
	sprintf (out + strlen(out), "/%d", currprefs.fpu_model);
    sprintf(out + strlen(out), " %s", (currprefs.chipset_mask & CSMASK_AGA) ? "AGA" : ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? "ECS" : "OCS"));
    ULBST(out);
    ULBST("VPOS     %04.4X (%d)", vpos, vpos);
    ULBST("HPOS     %04.4X (%d)", current_hpos(), current_hpos());
    for (i = 0; dcustom[i]; i++) {
	for (j = 0; custd[j].name; j++) {
	    if (custd[j].adr == (dcustom[i] & 0x1fe) + 0xdff000) {
		if (dcustom[i] & 0x8000)
		    ULBST("%-8s %08.8X", custd[j].name, (gw(p1, dcustom[i] & 0x1fe) << 16) | gw(p1, (dcustom[i] & 0x1fe) + 2));
		else
		    ULBST("%-8s %04.4X", custd[j].name, gw(p1, dcustom[i] & 0x1fe));
		break;
	    }
	}
    }
    free (p2);
}

static void ShowMisc(void)
{
    int line = 0;
    HWND hMisc;
    int len, i;
    uae_u8 *p, *p2;

    hMisc = GetDlgItem(hDbgWnd, IDC_DBG_MISC);
    ULBSINIT(hMisc);
    for (i = 0; i < 2; i++) {
	p = p2 = save_cia (i, &len, NULL);
	ULBS("");
	ULBS("CIA %c:", i == 1 ? 'B' : 'A');
	ULBS("");
	ULBS("PRA %02X   PRB %02X", p[0], p[1]);
	ULBS("DRA %02X   DRB %02X", p[2], p[3]);
	ULBS("CRA %02X   CRB %02X   ICR %02X   IM %02X",
	    p[14], p[15], p[13], p[16]);
	ULBS("TA  %04X (%04X)   TB %04X (%04X)",
	    (p[5] << 8) | p[4], (p[18] << 8) | p[17],
	    (p[7] << 8) | p[6], (p[20] << 8) | p[19]);
	ULBS("TOD %06X (%06X) ALARM %06X %c%c",
	    (p[10] << 16) | (p[ 9] << 8) | p[ 8],
	    (p[23] << 16) | (p[22] << 8) | p[21],
	    (p[26] << 16) | (p[25] << 8) | p[24],
	    (p[27] & 1) ? 'L' : ' ', (p[27] & 2) ? ' ' : 'S');
        free(p2);
    }
    for (i = 0; i < 4; i++) {
	p = p2 = save_disk (i, &len, NULL);
	ULBS("");
	ULBS("Drive DF%d: (%s)", i, (p[4] & 2) ? "disabled" : "enabled");
	ULBS("ID %08.8X  Motor %s  Cylinder %2d  MFMPOS %d",
	    (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3],
	    (p[4] & 1) ? "On" : "Off",
	    p[5], (p[8] << 24) | (p[9] << 16) | (p[10] << 8) | p[11]);
	if (p[16])
	    ULBS("'%s'", p + 16);
	else
	    ULBS("Drive is empty");
	free(p2);
    }
    p = p2 = save_floppy (&len, NULL);
    ULBS("");
    ULBS("Disk controller:");
    ULBS("");
    ULBS("Shift register: Data=%04.4X Shift=%d. DMA=%d,%d", (p[0] << 8) | p[1], p[2], p[3], p[5]);
    free (p2);
}

static void ShowCustom(void)
{
    int len, i, j, end;
    uae_u8 *p1, *p2, *p3, *p4;

    ULBSINIT(GetDlgItem(hDbgWnd, IDC_DBG_CUSTOM));
    p1 = p2 = save_custom (&len, 0, 1);
    p1 += 4; // skip chipset type
    for (i = 0; i < 4; i++) {
	p4 = p1 + 0xa0 + i * 16;
	p3 = save_audio (i, &len, 0);
	p4[0] = p3[12];
	p4[1] = p3[13];
	p4[2] = p3[14];
	p4[3] = p3[15];
	p4[4] = p3[4];
	p4[5] = p3[5];
	p4[6] = p3[8];
	p4[7] = p3[9];
	p4[8] = 0;
	p4[9] = p3[1];
	p4[10] = p3[10];
	p4[11] = p3[11];
	free (p3);
    }
    end = 0;
    while (custd[end].name)
	end++;
    end++;
    end /= 2;
    for (i = 0; i < end; i++) {
	uae_u16 v1, v2;
	int addr1, addr2;
	j = end + i;
	addr1 = custd[i].adr & 0x1ff;
	addr2 = custd[j].adr & 0x1ff;
	v1 = (p1[addr1 + 0] << 8) | p1[addr1 + 1];
	v2 = (p1[addr2 + 0] << 8) | p1[addr2 + 1];
	ULBS("%03.3X %-15s %04.4X    %03.3X %-15s %04.4X",
	    addr1, custd[i].name, v1,
	    addr2, custd[j].name, v2);
    }
    free (p2);
}

static void ShowBreakpoints(void)
{
    HWND hBrkpts;
    int i, line = 0, lines_old, got;
    char outbp[MAX_LINEWIDTH + 1], outw[50];

    hBrkpts = GetDlgItem(hDbgWnd, IDC_DBG_BRKPTS);
    ULBSINIT(hBrkpts);
    lines_old = SendMessage(hBrkpts, LB_GETCOUNT, 0, 0);
    ULBS("");
    ULBS("Breakpoints:");
    ULBS("");
    got = 0;
    for (i = 0; i < BREAKPOINT_TOTAL; i++) {
        if (!bpnodes[i].enabled)
            continue;
		m68k_disasm_2(outbp, sizeof(outbp), bpnodes[i].addr, NULL, 1, NULL, NULL, 0);
        ULBS(outbp);
        got = 1;
    }
    if (!got)
        ULBS("none");
    ULBS("");
    ULBS("Memwatch breakpoints:");
    ULBS("");
    got = 0;
    for (i = 0; i < MEMWATCH_TOTAL; i++) {
        if (mwnodes[i].size == 0)
            continue;
	memwatch_dump2(outw, sizeof(outw), i);
        ULBS(outw);
        got = 1;
    }
    if (!got)
        ULBS("none");
    for (i = ulbs_pos; i < lines_old; i++)
        SendMessage(hBrkpts, LB_DELETESTRING, line, 0);
}

static void ShowMem(int offset)
{
    uae_u32 addr;
    int i, lines_old, lines_new;
    char out[MAX_LINEWIDTH + 1];
    HWND hMemory;

    dbgpage[currpage].addr += offset;
    addr = dbgpage[currpage].addr;
    hMemory = GetDlgItem(hDbgWnd, IDC_DBG_MEM);
    lines_old = SendMessage(hMemory, LB_GETCOUNT, 0, 0);
    lines_new = GetLBOutputLines(hMemory);
    for (i = 0; i < lines_new; i++) {
        addr = dumpmem2(addr, out, sizeof(out));
        UpdateListboxString(hMemory, i, out, FALSE);
    }
    for (i = lines_new; i < lines_old; i++) {
        SendMessage(hMemory, LB_DELETESTRING, lines_new, 0);
    }
    SendMessage(hMemory, LB_SETTOPINDEX, 0, 0);
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
    int i, lines_old, lines_new;
    char out[MAX_LINEWIDTH + 1];
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
    lines_old = SendMessage(hDasm, LB_GETCOUNT, 0, 0);
    lines_new = GetLBOutputLines(hDasm);
    for (i = 0; i < lines_new; i++) {
        m68k_disasm_2(out, sizeof(out), addr, &addr, 1, NULL, NULL, 0);
        if (addr > dbgpage[currpage].addr)
            UpdateListboxString(hDasm, i, out, FALSE);
        else
            UpdateListboxString(hDasm, i, "", FALSE);
    }
    for (i = lines_new; i < lines_old; i++) {
        SendMessage(hDasm, LB_DELETESTRING, lines_new, 0);
    }
    SendMessage(hDasm, LB_SETTOPINDEX, 0, 0);
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
    HWND hwnd;

    if (index >= pages || ((index == currpage) && !force))
        return;
    if (currpage >= 0) {
        pstatuscolor[currpage] = (currpage < 2 && index > 1) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT;
        if (index < 2)
            pstatuscolor[index == 0 ? 1 : 0] = COLOR_GRAYTEXT;
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
        } else if (id == IDC_DBG_BRKPTS) {
            ShowBreakpoints();
        } else if (id == IDC_DBG_MISC) {
            ShowMisc();
        } else if (id == IDC_DBG_CUSTOM) {
            ShowCustom();
        }
	    ShowWindow(dbgpage[index].ctrl[i], SW_SHOW);
        }
    }
    currpage = index;
    pstatuscolor[currpage] = COLOR_HIGHLIGHT;
    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_STATUS);
    RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
}

static void AddPage(int *iddata)
{
    int i;

    if (pages >= MAXPAGES)
        return;
    memset(&dbgpage[pages], 0, sizeof(struct debuggerpage));
    for (i = 0; iddata[i] > 0; i++) {
        dbgpage[pages].ctrl[i] = GetDlgItem(hDbgWnd, iddata[i]);
        ShowWindow(dbgpage[pages].ctrl[i], SW_HIDE);
    }
    pages++;
}

static int GetTextSize(HWND hWnd, char *text, int width)
{
    HDC hdc;
    TEXTMETRIC tm;
    HFONT hfont, hfontold;
    hdc = GetDC(hWnd);
    hfont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
    hfontold = (HFONT)SelectObject(hdc, hfont);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, hfontold);
    ReleaseDC(hWnd, hdc);
    if (!width)
        return tm.tmHeight + tm.tmExternalLeading;
    else if (text)
        return tm.tmMaxCharWidth * strlen(text);
    return 0;
}

static void InitPages(void)
{
    int i, parts[MAXPAGES], width, pwidth = 0;
    HWND hwnd;

    int dpage[][MAXPAGECONTROLS + 1] = {
        { IDC_DBG_OUTPUT1, -1 },
        { IDC_DBG_OUTPUT2, -1 },
        { IDC_DBG_MEM, IDC_DBG_MEMINPUT, -1 },
        { IDC_DBG_MEM, IDC_DBG_MEMINPUT, -1 },
        { IDC_DBG_DASM, IDC_DBG_MEMINPUT, IDC_DBG_MEMTOPC, -1 },
        { IDC_DBG_DASM, IDC_DBG_MEMINPUT, IDC_DBG_MEMTOPC, -1 },
        { IDC_DBG_BRKPTS, -1 },
        { IDC_DBG_MISC, -1 },
        { IDC_DBG_CUSTOM, -1 }
    };

    pages = 0;
    for (i = 0; i < (sizeof(dpage) / sizeof(dpage[0])); i++)
        AddPage(dpage[i]);
    memset(parts, 0, MAXPAGES * sizeof(int));
    width = GetTextSize(hDbgWnd, "12345678", TRUE); // longest pagename + 2
    for (i = 0; i < pages; i++) {
        pwidth += width;
        parts[i] = pwidth;
    }
    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_STATUS);
    SendMessage(hwnd, SB_SETPARTS, (WPARAM)pages, (LPARAM)parts);
    for (i = 0; i < pages; i++) {
        SendMessage(hwnd, SB_SETTEXT, i | SBT_OWNERDRAW, 0);
        pstatuscolor[i] = COLOR_GRAYTEXT;
    }
}

static LRESULT CALLBACK InputProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC oldproc;

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
    oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWL_USERDATA);    
    return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK MemInputProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HANDLE hdata;
    LPTSTR lptstr;
    char allowed[] = "1234567890abcdefABCDEF";
    int ok = 1;
    char addrstr[11];
    uae_u32 addr;
    WNDPROC oldproc;

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
    oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWL_USERDATA);    
    return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK ListboxProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC oldproc;
    HWND hinput, hsbar;
    RECT rc, r;
    int i, itemheight, count, height, bottom, width, id;
    PAINTSTRUCT ps;
    DRAWITEMSTRUCT dis;
    HFONT oldfont, font;
    HDC hdc, compdc;
    HBITMAP compbmp, oldbmp;
 
    switch (message) {
        case WM_CHAR:
            hinput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
            SetFocus(hinput);
            SendMessage(hinput, WM_CHAR, wParam, lParam);
            return TRUE;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_SETFOCUS:
            return TRUE;
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rc);
            height = rc.bottom - rc.top;
            width = rc.right - rc.left;
            bottom = rc.bottom;
            itemheight = SendMessage(hWnd, LB_GETITEMHEIGHT, 0, 0);
            rc.bottom = itemheight;
            count = SendMessage(hWnd, LB_GETCOUNT, 0, 0);
            compdc = CreateCompatibleDC(hdc);
            compbmp = CreateCompatibleBitmap(hdc, width, height);
            oldbmp = SelectObject(compdc, compbmp);
            font = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
            oldfont = SelectObject(compdc, font);
            id = GetDlgCtrlID(hWnd);
            dis.CtlType = ODT_LISTBOX;
            dis.CtlID = id;
            dis.itemAction = 0;
            dis.itemState = 0;
            dis.hwndItem = hWnd;
            dis.hDC = compdc;
            for (i = 0; i < count && rc.top < height; i++) {
                dis.itemID = i;
                dis.rcItem = rc;
                dis.itemData =  SendMessage(hWnd, LB_GETITEMDATA, i, 0);
                SendMessage(hDbgWnd, WM_DRAWITEM, id, (LPARAM)&dis);
                rc.top += itemheight;
                rc.bottom += itemheight;
            }
            rc.bottom = bottom;
            if (!IsWindowEnabled(hWnd))
                FillRect(compdc, &rc, GetSysColorBrush(COLOR_3DFACE));
            else
                FillRect(compdc, &rc, GetSysColorBrush(COLOR_WINDOW));
            GetWindowRect(hWnd, &rc);
            hsbar = GetDlgItem(hDbgWnd, IDC_DBG_STATUS);
            GetWindowRect(hsbar, &r);
            if (rc.top < r.top) { // not below status bar
                if (rc.bottom > r.top) // partly visible
                    height -= rc.bottom - r.top;
                BitBlt(hdc, 0, 0, width, height, compdc, 0, 0, SRCCOPY);
            }
            SelectObject(compdc, oldfont);
            SelectObject(compdc, oldbmp);
            DeleteObject(compbmp);
            DeleteDC(compdc);
            EndPaint(hWnd, &ps);
            return TRUE;
    }
    oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWL_USERDATA);    
    return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK EditProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC oldproc;
    HWND hinput;

    switch (message) {
        case WM_CHAR:
            if (wParam != VK_CANCEL) { // not for Ctrl-C for copying
                hinput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
                SetFocus(hinput);
                SendMessage(hinput, WM_CHAR, wParam, lParam);
                return TRUE;
            }
            break;
    }
    oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWL_USERDATA);    
    return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
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
    int id1y[] = { IDC_DBG_OUTPUT1, IDC_DBG_OUTPUT2, IDC_DBG_MEM, IDC_DBG_DASM, IDC_DBG_BRKPTS, IDC_DBG_MISC, IDC_DBG_CUSTOM, -1 };
    int id2y[] = { IDC_DBG_INPUT, IDC_DBG_HELP, IDC_DBG_STATUS, -1 };

    int id1x[] = { IDC_DBG_OUTPUT1, IDC_DBG_OUTPUT2, IDC_DBG_MEM, IDC_DBG_DASM,
	IDC_DBG_AMEM, IDC_DBG_PREFETCH, IDC_DBG_INPUT, IDC_DBG_STATUS, IDC_DBG_BRKPTS, IDC_DBG_MISC, IDC_DBG_CUSTOM, -1 };
    int id2x[] = { IDC_DBG_HELP, IDC_DBG_CCR, IDC_DBG_SP_VBR, IDC_DBG_MMISC,
	IDC_DBG_FPREG, IDC_DBG_FPSR, IDC_DBG_MCUSTOM, IDC_DBG_MISCCPU, -1 };

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
    EnumChildWindows (hDlg, childenumproc, 0);
    dlgRect = r;
    RedrawWindow(hDlg, 0, 0, RDW_INVALIDATE);
}

static BOOL CALLBACK InitChildWindows(HWND hWnd, LPARAM lParam)
{
    int i, id, enable = TRUE, items = 0;
    WNDPROC newproc = NULL, oldproc;
    char classname[CLASSNAMELENGTH];
    WINDOWINFO pwi;
    RECT *r;

    id = GetDlgCtrlID(hWnd);
    switch (id) {
        case IDC_DBG_INPUT:
            newproc = InputProc;
            SendMessage(hWnd, EM_LIMITTEXT, MAX_LINEWIDTH, 0);
            break;
        case IDC_DBG_MEMINPUT:
            newproc = MemInputProc;
            SendMessage(hWnd, EM_LIMITTEXT, 8, 0);
            break;
        case IDC_DBG_PREFETCH:
            newproc = ListboxProc;
            enable = currprefs.cpu_compatible ? TRUE : FALSE;
            break;
        case IDC_DBG_FPREG:
        case IDC_DBG_FPSR:
            newproc = ListboxProc;
            enable = currprefs.cpu_model < 68020 ? FALSE : TRUE;
            break;
        case IDC_DBG_MISCCPU:
            if (currprefs.cpu_model == 68000) {
                items = 4;
                enable = FALSE;
            }
            else {
                for (i = 0; m2cregs[i].regno>= 0; i++) {
	                if (!movec_illg(m2cregs[i].regno))
                        items++;
                }
            }
            pwi.cbSize = sizeof pwi;
            GetWindowInfo(hWnd, &pwi);
            r = &pwi.rcClient;
            r->bottom = r->top + items * GetTextSize(hWnd, NULL, FALSE);
            AdjustWindowRectEx(r, pwi.dwStyle, FALSE, pwi.dwExStyle);
            SetWindowPos(hWnd, 0, 0, 0, r->right - r->left, r->bottom - r->top, SWP_NOMOVE | SWP_NOZORDER);
            newproc = ListboxProc;
            break;
        default:
            if (GetClassName(hWnd, classname, CLASSNAMELENGTH)) {
                if (!strcmp(classname, "ListBox"))
                    newproc = ListboxProc;
                else if (!strcmp(classname, "Edit"))
                    newproc = EditProc;
            }
            break;
    }
    if (newproc) {
        oldproc = (WNDPROC)SetWindowLongPtr(hWnd, GWL_WNDPROC, (LONG_PTR)newproc);
        SetWindowLongPtr(hWnd, GWL_USERDATA, (LONG_PTR)oldproc);
    }
    EnableWindow(hWnd, enable);
    return TRUE;
}

static LRESULT CALLBACK DebuggerProc (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hwnd;
    DRAWITEMSTRUCT *pdis;
    HDC hdc;
    RECT rc;
    char text[MAX_LINEWIDTH + 1];

    switch (message) {
        case WM_INITDIALOG:
	{
	    int newpos = 0;
	    LONG x, y, w, h;
	    DWORD regkeytype;
	    DWORD regkeysize = sizeof(LONG);
            RECT rw;
            GetWindowRect(hDlg, &rw);
            dbgwnd_minx = rw.right - rw.left;
            dbgwnd_miny = rw.bottom - rw.top;
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
	    EnumChildWindows(hDlg, InitChildWindows, 0);
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
	    mmi->ptMinTrackSize.x = dbgwnd_minx;
	    mmi->ptMinTrackSize.y = dbgwnd_miny;
	    return TRUE;
	}
	case WM_EXITSIZEMOVE:
	{
	    AdjustDialog(hDlg);
        ShowPage(currpage, TRUE);
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
                {
                    HWND hinput;
                    WriteOutput(linebreak + 1, 2);
                    debug_help();
                    hinput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
                    SetFocus(hinput);
                    return TRUE;
                }
                case ID_DBG_PAGE1:
                case ID_DBG_PAGE2:
                case ID_DBG_PAGE3:
                case ID_DBG_PAGE4:
                case ID_DBG_PAGE5:
                case ID_DBG_PAGE6:
                case ID_DBG_PAGE7:
                case ID_DBG_PAGE8:
                case ID_DBG_PAGE9:
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
                {
                    HWND hmeminput;
                    SetMemToPC();
                    hmeminput = GetDlgItem(hDbgWnd, IDC_DBG_MEMINPUT);
                    SetFocus(hmeminput);
                    return TRUE;
                }
            }
            break;
        case WM_MEASUREITEM:
            ((MEASUREITEMSTRUCT*)(lParam))->itemHeight = GetTextSize(hDlg, NULL, FALSE);
            return TRUE;
        case WM_DRAWITEM:
            pdis = (DRAWITEMSTRUCT *)lParam;
            hdc = pdis->hDC;
            rc = pdis->rcItem;
            SetBkMode(hdc, TRANSPARENT);
            if (wParam == IDC_DBG_STATUS) {
                SetTextColor(hdc, GetSysColor(pstatuscolor[pdis->itemID]));
                DrawText(hdc, pname[pdis->itemID], lstrlen(pname[pdis->itemID]), &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            else {
                if (pdis->itemID < 0) {
                    return TRUE;
                }
                memset(text, 0, MAX_LINEWIDTH + 1);
                SendMessage(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)(LPSTR)text);
                if (!IsWindowEnabled(pdis->hwndItem)) {
                    FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
                    SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
                }
                else {
                    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
                    SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
                }
                SetTextColor(hdc, pdis->itemData);
                TextOut(hdc, rc.left, rc.top, text, strlen(text));
                return TRUE;
            }
            break;
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
    char *fpsrflag[] = { "N:   ", "Z:   ", "I:   ", "NAN: " };

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

    ShowMiscCPU(GetDlgItem(hDbgWnd, IDC_DBG_MISCCPU));

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_MMISC);
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

    ShowCustomSmall(GetDlgItem(hDbgWnd, IDC_DBG_MCUSTOM));

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_FPREG);
    for (i = 0; i < 8; i++) {
        sprintf(out, "FP%d: %g", i, regs.fp[i]);
        UpdateListboxString(hwnd, i, out, TRUE);
    }

    hwnd = GetDlgItem(hDbgWnd, IDC_DBG_FPSR);
    fpsr = get_fpsr();
    for (i = 0; i < 4; i++) {
        sprintf(out, "%s%d", fpsrflag[i], (fpsr & (0x8000000 >> i)) != 0 ? 1 : 0);
        UpdateListboxString(hwnd, i, out, TRUE);
    }
    ShowPage(currpage, TRUE);
}
