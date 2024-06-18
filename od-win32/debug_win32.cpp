/*
* WinUAE GUI debugger
*
* Copyright 2008 Karsten Bock
* Copyright 2007 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>
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
#include "registry.h"
#include "win32gui.h"
#include "fpp.h"

#include "uae.h"

static HWND hDbgWnd = 0;
static HWND hOutput = 0;
static HACCEL dbgaccel = 0;
static HFONT udfont = 0;
static HWND hedit = 0;

extern int consoleopen;
BOOL debuggerinitializing = FALSE;

static TCHAR linebreak[] = {'\r', '\n', '\0'};

#define MAXLINES 250
#define MAXINPUTHIST 50
#define MAXPAGECONTROLS 5
#define MAXPAGES 10
#define CLASSNAMELENGTH 50

static int inputfinished = 0;

static WORD* dlgtmpl;
static int reopen;

struct histnode {
	TCHAR *command;
	struct histnode *prev;
	struct histnode *next;
};

static struct histnode *firsthist, *lasthist, *currhist;
static int histcount;

struct debuggerpage {
	HWND ctrl[MAXPAGECONTROLS];
	uae_u32 memaddr;
	uae_u32 dasmaddr;
	TCHAR addrinput[9];
	int selection;
	int init;
	int autoset;
};
static struct debuggerpage dbgpage[MAXPAGES];
static int currpage, pages;
static int pagetype;

const TCHAR *pname[] = { _T("OUT1"), _T("OUT2"), _T("MEM1"), _T("MEM2"), _T("DASM1"), _T("DASM2"), _T("BRKPTS"), _T("MISC"), _T("CUSTOM") };
static int pstatuscolor[MAXPAGES];

static int dbgwnd_minx = 800, dbgwnd_miny = 600;

static BOOL useinternalcmd = FALSE;
static TCHAR internalcmd[MAX_LINEWIDTH + 1];

static const TCHAR *markinstr[] = { _T("JMP"), _T("BT L"), _T("RTS"), _T("RTD"), _T("RTE"), _T("RTR"), 0 };
static const TCHAR *ucbranch[] = { _T("BSR"), _T("JMP"), _T("JSR"), 0 };
static const TCHAR *cbranch[] = { _T("B"), _T("DB"), _T("FB"), _T("FDB"), 0 };
static const TCHAR *ccode[] = { _T("T "), _T("F  "), _T("HI"), _T("LS"), _T("CC"), _T("CS"), _T("NE"), _T("EQ"),
	_T("VC"), _T("VS"), _T("PL"), _T("MI"), _T("GE"), _T("LT"), _T("GT"), _T("LE"), 0 };

static void OutputCurrHistNode(HWND hWnd)
{
	int txtlen;
	TCHAR *buf;

	if (currhist->command) {
		txtlen = GetWindowTextLength(hWnd);
		buf = xmalloc(TCHAR, txtlen + 1);
		GetWindowText(hWnd, buf, txtlen + 1);
		if (_tcscmp(buf, currhist->command)) {
			SetWindowText(hWnd, currhist->command);
			txtlen = uaetcslen(currhist->command);
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

static void AddToHistory(const TCHAR *command)
{
	struct histnode *tmp;

	currhist = NULL;
	if (histcount > 0 && !_tcscmp(command, lasthist->command))
		return;
	else if (histcount == MAXINPUTHIST)
		DeleteFromHistory(1);
	tmp = lasthist;
	lasthist = xmalloc(struct histnode, 1);
	if (histcount == 0)
		firsthist = lasthist;
	lasthist->command = my_strdup(command);
	lasthist->next = NULL;
	lasthist->prev = tmp;
	if (tmp)
		tmp->next = lasthist;
	histcount++;
}

int GetInput (TCHAR *out, int maxlen)
{
	HWND hInput;
	int chars;

	if (!hDbgWnd)
		return 0;
	hInput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
	chars = GetWindowText(hInput, out, maxlen);
	if (chars == 0)
		return 0;
	WriteOutput(linebreak + 1, 2);
	WriteOutput(out, uaetcslen(out));
	WriteOutput(linebreak + 1, 2);
	AddToHistory(out);
	SetWindowText(hInput, _T(""));
	return chars;
}

static int CheckLineLimit(HWND hWnd, const TCHAR *out)
{
	TCHAR *tmp, *p;
	int lines_new = 0, txtlen, visible;
	LRESULT lines_have, lastchr;

	tmp = (TCHAR *)out;
	lines_have = SendMessage(hWnd, EM_GETLINECOUNT, 0, 0);
	while (tmp[0] != '\0' && (p = _tcschr(tmp, '\n'))) {
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

void WriteOutput(const TCHAR *out, int len)
{
	int pos = 0, leave = 0;
	LRESULT count, index, txtlen;
	TCHAR *buf = 0, *p, *tmp;

	if (!hOutput || !_tcscmp(out, _T(">")) || len == 0)
		return;
	if (!CheckLineLimit(hOutput, out))
		return;
	tmp = (TCHAR *)out;
	for(;;) {
		p = _tcschr(tmp, '\n');
		if (p) {
			pos = addrdiff(p, tmp) + 1;
			if (pos > (MAX_LINEWIDTH + 1))
				pos = MAX_LINEWIDTH + 1;
			buf = xcalloc(TCHAR, pos + 2);
			_tcsncpy(buf, tmp, pos - 1);
			_tcscat(buf, linebreak);
		} else if (tmp[0] == '\0') {
			leave = 1;
		} else {
			count = SendMessage(hOutput, EM_GETLINECOUNT, 0, 0);
			index = SendMessage(hOutput, EM_LINEINDEX, count - 1, 0);
			txtlen = SendMessage(hOutput, EM_LINELENGTH, index, 0);
			if (_tcslen(tmp) + txtlen > MAX_LINEWIDTH) {
				buf = xcalloc(TCHAR, MAX_LINEWIDTH + 3 - txtlen);
				_tcsncpy(buf, tmp, MAX_LINEWIDTH - txtlen);
				_tcscat(buf, linebreak);
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
static void UpdateListboxString(HWND hWnd, int pos, TCHAR *out, int mark)
{
	LRESULT count;
	TCHAR text[MAX_LINEWIDTH + 1], *p;
	COLORREF cr;

	if (!IsWindowEnabled(hWnd)) {
		p = _tcschr(out, ':');
		if (p)
			*(p + 1) = '\0';
	}
	if (_tcslen(out) > MAX_LINEWIDTH)
		out[MAX_LINEWIDTH] = '\0';
	p = _tcschr(out, '\n');
	if (p)
		*p = '\0';
	cr = GetSysColor(COLOR_WINDOWTEXT);
	count = SendMessage(hWnd, (UINT) LB_GETCOUNT, 0, 0);
	if (pos < count) {
		memset(text, 0, MAX_LINEWIDTH + 1);
		SendMessage(hWnd, LB_GETTEXT, pos, (LPARAM)((LPTSTR)text));
		if (_tcscmp(out, text) != 0 && mark)
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
static void ULBS(const TCHAR *format, ...)
{
	TCHAR buffer[MAX_LINEWIDTH + 1];
	va_list parms;
	va_start(parms, format);
	_vsntprintf(buffer, MAX_LINEWIDTH, format, parms);
	UpdateListboxString(ulbs_hwnd, ulbs_pos++, buffer, FALSE);
}
static void ULBST(const TCHAR *format, ...)
{
	TCHAR buffer[MAX_LINEWIDTH + 1];
	va_list parms;
	va_start(parms, format);
	_vsntprintf(buffer, MAX_LINEWIDTH, format, parms);
	UpdateListboxString(ulbs_hwnd, ulbs_pos++, buffer, TRUE);
}

static int GetLBOutputLines(HWND hWnd)
{
	int lines = 0;
	LRESULT itemsize, clientsize;
	RECT rc;

	GetClientRect(hWnd, &rc);
	clientsize = rc.bottom - rc.top;
	itemsize = SendMessage(hWnd, LB_GETITEMHEIGHT, 0, 0);
	while (clientsize > itemsize) {
		lines++;
		clientsize -= itemsize;
	}
	return lines;
}

static void ShowMiscCPU(HWND hwnd)
{
	int line = 0;
	TCHAR out[MAX_LINEWIDTH + 1];
	int i;

	for (i = 0; m2cregs[i].regno>= 0; i++) {
		if (!movec_illg(m2cregs[i].regno)) {
			_stprintf(out, _T("%-4s %08X"), m2cregs[i].regname, val_move2c(m2cregs[i].regno));
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
	int i, j, cnt;
	size_t len;
	uae_u8 *p1, *p2, *p3, *p4;
	TCHAR out[MAX_LINEWIDTH + 1];

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
	_stprintf(out, _T("CPU %d"), currprefs.cpu_model);
	if (currprefs.fpu_model)
		_stprintf (out + _tcslen(out), _T("/%d"), currprefs.fpu_model);
	_stprintf(out + _tcslen(out), _T(" %s"), (currprefs.chipset_mask & CSMASK_AGA) ? _T("AGA") : ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? _T("ECS") : _T("OCS")));
	ULBST(out);
	ULBST(_T("VPOS     %04X (%d)"), vpos, vpos);
	ULBST(_T("HPOS     %04X (%d)"), current_hpos(), current_hpos());
	for (i = 0; dcustom[i]; i++) {
		for (j = 0; custd[j].name; j++) {
			if (custd[j].adr == (dcustom[i] & 0x1fe) + 0xdff000) {
				if (dcustom[i] & 0x8000)
					ULBST(_T("%-8s %08X"), custd[j].name, (gw(p1, dcustom[i] & 0x1fe) << 16) | gw(p1, (dcustom[i] & 0x1fe) + 2));
				else
					ULBST(_T("%-8s %04X"), custd[j].name, gw(p1, dcustom[i] & 0x1fe));
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
	int i;
	size_t len;
	uae_u8 *p, *p2;

	hMisc = GetDlgItem(hDbgWnd, IDC_DBG_MISC);
	ULBSINIT(hMisc);
	for (i = 0; i < 2; i++) {
		p = p2 = save_cia (i, &len, NULL);
		ULBS(_T(""));
		ULBS(_T("CIA %c:"), i == 1 ? 'B' : 'A');
		ULBS(_T(""));
		ULBS(_T("PRA %02X   PRB %02X"), p[0], p[1]);
		ULBS(_T("DRA %02X   DRB %02X"), p[2], p[3]);
		ULBS(_T("CRA %02X   CRB %02X   ICR %02X   IM %02X"),
			p[14], p[15], p[13], p[16]);
		ULBS(_T("TA  %04X (%04X)   TB %04X (%04X)"),
			(p[5] << 8) | p[4], (p[18] << 8) | p[17],
			(p[7] << 8) | p[6], (p[20] << 8) | p[19]);
		ULBS(_T("TOD %06X (%06X) ALARM %06X %c%c"),
			(p[10] << 16) | (p[ 9] << 8) | p[ 8],
			(p[23] << 16) | (p[22] << 8) | p[21],
			(p[26] << 16) | (p[25] << 8) | p[24],
			(p[27] & 1) ? 'L' : ' ', (p[27] & 2) ? ' ' : 'S');
		free(p2);
	}
	for (i = 0; i < 4; i++) {
		p = p2 = save_disk (i, &len, NULL, false);
		ULBS(_T(""));
		ULBS(_T("Drive DF%d: (%s)"), i, (p[4] & 2) ? "disabled" : "enabled");
		ULBS(_T("ID %08X  Motor %s  Cylinder %2d  MFMPOS %d"),
			(p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3],
			(p[4] & 1) ? _T("On") : _T("Off"),
			p[5], (p[8] << 24) | (p[9] << 16) | (p[10] << 8) | p[11]);
		if (p[16])
			ULBS(_T("'%s'"), p + 16);
		else
			ULBS(_T("Drive is empty"));
		free(p2);
	}
	p = p2 = save_floppy (&len, NULL);
	ULBS(_T(""));
	ULBS(_T("Disk controller:"));
	ULBS(_T(""));
	ULBS(_T("Shift register: Data=%04X Shift=%d. DMA=%d,%d"), (p[0] << 8) | p[1], p[2], p[3], p[5]);
	free (p2);
}

static void ShowCustom(void)
{
	int i, j, end;
	size_t len;
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
		ULBS(_T("%03.3X %-15s %04X    %03X %-15s %04X"),
			addr1, custd[i].name, v1,
			addr2, custd[j].name, v2);
	}
	free (p2);
}

static void ShowBreakpoints(void)
{
	HWND hBrkpts;
	int i, got;
	LRESULT lines_old;
	TCHAR outbp[MAX_LINEWIDTH + 1], outw[50];

	hBrkpts = GetDlgItem(hDbgWnd, IDC_DBG_BRKPTS);
	ULBSINIT(hBrkpts);
	lines_old = SendMessage(hBrkpts, LB_GETCOUNT, 0, 0);
	ULBS(_T(""));
	ULBS(_T("Breakpoints:"));
	ULBS(_T(""));
	got = 0;
	for (i = 0; i < BREAKPOINT_TOTAL; i++) {
		if (!bpnodes[i].enabled)
			continue;
		m68k_disasm_2(outbp, sizeof outbp / sizeof (TCHAR), bpnodes[i].value1, NULL, 0, NULL, 1, NULL, NULL, 0xffffffff, 0);
		ULBS(outbp);
		got = 1;
	}
	if (!got)
		ULBS(_T("none"));
	ULBS(_T(""));
	ULBS(_T("Memwatch breakpoints:"));
	ULBS(_T(""));
	got = 0;
	for (i = 0; i < MEMWATCH_TOTAL; i++) {
		if (mwnodes[i].size == 0)
			continue;
		memwatch_dump2(outw, sizeof outw / sizeof (TCHAR), i);
		ULBS(outw);
		got = 1;
	}
	if (!got)
		ULBS(_T("none"));
	for (i = ulbs_pos; i < lines_old; i++)
		SendMessage(hBrkpts, LB_DELETESTRING, ulbs_pos, 0);
}

static void ShowMem(int offset)
{
	uae_u32 addr;
	int i, lines_new;
	LRESULT lines_old;
	TCHAR out[MAX_LINEWIDTH + 1];
	HWND hMemory;

	dbgpage[currpage].memaddr += offset;
	addr = dbgpage[currpage].memaddr;
	if (currpage == 0)
		hMemory = GetDlgItem(hDbgWnd, IDC_DBG_MEM2);
	else
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
	dasmaddr = addr - 22;
	while (dasmaddr < addr) {
		next = dasmaddr + 2;
		m68k_disasm_2(NULL, 0, dasmaddr, NULL, 0, &next, 1, NULL, NULL, 0xffffffff, 0);
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
	int i, lines_new;
	LRESULT lines_old;
	TCHAR out[MAX_LINEWIDTH + 1];
	HWND hDasm;

	if (currpage == 0)
		hDasm = GetDlgItem(hDbgWnd, IDC_DBG_DASM2);
	else
		hDasm = GetDlgItem(hDbgWnd, IDC_DBG_DASM);

	if (!dbgpage[currpage].init) {
		addr = m68k_getpc ();
		dbgpage[currpage].init = 1;
	}
	else if (dbgpage[currpage].autoset == 1 && direction == 0) {
		addr = m68k_getpc ();
	}
	else
		addr = dbgpage[currpage].dasmaddr;
	if (direction > 0) {
		m68k_disasm_2(NULL, 0, addr, NULL, 0, &addr, 1, NULL, NULL, 0xffffffff, 0);
		if (!addr || addr < dbgpage[currpage].dasmaddr)
			addr = dbgpage[currpage].dasmaddr;
	}
	else if (direction < 0 && addr > 0) {
		if (GetPrevAddr(addr, &prev))
			addr = prev;
		else
			addr -= 2;
	}
	dbgpage[currpage].dasmaddr = addr;
	lines_old = SendMessage(hDasm, LB_GETCOUNT, 0, 0);
	lines_new = GetLBOutputLines(hDasm);
	for (i = 0; i < lines_new; i++) {
		m68k_disasm_2(out, sizeof out / sizeof (TCHAR), addr, NULL, 0, &addr, 1, NULL, NULL, 0xffffffff, 0);
		if (addr > dbgpage[currpage].dasmaddr)
			UpdateListboxString(hDasm, i, out, FALSE);
		else
			UpdateListboxString(hDasm, i, _T(""), FALSE);
	}
	for (i = lines_new; i < lines_old; i++) {
		SendMessage(hDasm, LB_DELETESTRING, lines_new, 0);
	}
	SendMessage(hDasm, LB_SETTOPINDEX, 0, 0);
}

static void SetMemToPC(void)
{
	int i, id;

	dbgpage[currpage].dasmaddr = m68k_getpc ();
	_stprintf(dbgpage[currpage].addrinput, _T("%08X"), dbgpage[currpage].dasmaddr);
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
				if (index != currpage)
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
			} else if (id == IDC_DBG_MEM || id == IDC_DBG_MEM2) {
				ShowMem(0);
				pagetype = id;
			} else if (id == IDC_DBG_DASM) {
				ShowDasm(0);
				pagetype = id;
			} else if (id == IDC_DBG_DASM2) {
				ShowDasm(0);
			}
			else if (id == IDC_DBG_MEMINPUT) {
				SetWindowText(dbgpage[index].ctrl[i], dbgpage[index].addrinput);
			} else if (id == IDC_DBG_BRKPTS) {
				ShowBreakpoints();
			} else if (id == IDC_DBG_MISC) {
				ShowMisc();
			} else if (id == IDC_DBG_CUSTOM) {
				ShowCustom();
			} else if (id == IDC_DBG_AUTOSET) {
				SendMessage(dbgpage[index].ctrl[i], BM_SETCHECK, (WPARAM)dbgpage[index].autoset ? BST_CHECKED : BST_UNCHECKED, (LPARAM)0);
			}
			ShowWindow(dbgpage[index].ctrl[i], SW_SHOW);
		}
	}
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
	if (pages == 0)
		dbgpage[pages].autoset = 1;
	else
		dbgpage[pages].autoset = 0;
	pages++;
}

static int GetTextSize(HWND hWnd, TCHAR *text, int width)
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
		return tm.tmMaxCharWidth * uaetcslen(text);
	return 0;
}

static void InitPages(void)
{
	int i, parts[MAXPAGES], width, pwidth = 0;
	HWND hwnd;

	int dpage[][MAXPAGECONTROLS + 1] = {
		{ IDC_DBG_OUTPUT1, IDC_DBG_DASM2, IDC_DBG_MEM2, -1 },
		{ IDC_DBG_OUTPUT2, -1 },
		{ IDC_DBG_MEM, IDC_DBG_MEMINPUT, -1 },
		{ IDC_DBG_MEM, IDC_DBG_MEMINPUT, -1 },
		{ IDC_DBG_DASM, IDC_DBG_MEMINPUT, IDC_DBG_MEMTOPC, IDC_DBG_AUTOSET, -1 },
		{ IDC_DBG_DASM, IDC_DBG_MEMINPUT, IDC_DBG_MEMTOPC, IDC_DBG_AUTOSET, -1 },
		{ IDC_DBG_BRKPTS, -1 },
		{ IDC_DBG_MISC, -1 },
		{ IDC_DBG_CUSTOM, -1 }
	};

	pages = 0;
	for (i = 0; i < (sizeof(dpage) / sizeof(dpage[0])); i++)
		AddPage(dpage[i]);
	memset(parts, 0, MAXPAGES * sizeof(int));
	width = GetTextSize(hDbgWnd, _T("12345678"), TRUE); // longest pagename + 2
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
	case WM_CHAR:
		if (!debugger_active)
			return 0;
		break;
	case WM_KEYUP:
		if (!debugger_active)
			return 0;
		switch (wParam) {
		case VK_RETURN:
			inputfinished = 1;
			break;
		case VK_UP:
			SetPrevHistNode(hWnd);
			return 0;
		case VK_DOWN:
			SetNextHistNode(hWnd);
			return 0;
		}
		break;
	}
	oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK MemInputProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HANDLE hdata;
	LPWSTR lptstr;
	TCHAR allowed[] = _T("1234567890abcdefABCDEF");
	int ok = 1;
	TCHAR addrstr[12];
	uae_u32 addr;
	WNDPROC oldproc;

	switch (message) {
	case WM_CHAR:
		switch (wParam) {
		case VK_BACK:
		case VK_CANCEL:	//ctrl+c
		case VK_FINAL:	//ctrl+x
		case 0x16:		//ctrl+v
			break;
		default:
			if (!debugger_active || !_tcschr(allowed, (TCHAR)wParam))
				return 0;
			break;
		}
		break;
	case WM_PASTE:
		if (!OpenClipboard(NULL))
			return TRUE;
		hdata = GetClipboardData(CF_UNICODETEXT);
		if (hdata) {
			lptstr = (LPWSTR)GlobalLock(hdata);
			if (lptstr) {
				if (_tcsspn(lptstr, allowed) != _tcslen(lptstr))
					ok = 0;
				GlobalUnlock(hdata);
			}
		}
		CloseClipboard();
		if (!ok)
			return 0;
		break;
	case WM_KEYUP:
		if (!debugger_active)
			return 0;
		switch (wParam) {
		case VK_RETURN:
			_stprintf(addrstr, _T("0x"));
			GetWindowText(hWnd, addrstr + 2, 9);
			if (addrstr[2] != 0) {
				addr = _tcstoul(addrstr, NULL, 0);
				if (pagetype == IDC_DBG_MEM || pagetype == IDC_DBG_MEM2) {
					dbgpage[currpage].memaddr = addr;
					ShowMem(0);
				}
				else if (pagetype == IDC_DBG_DASM) {
					if (dbgpage[currpage].autoset)
						dbgpage[currpage].autoset = 2;
					dbgpage[currpage].dasmaddr = addr;
					ShowDasm(0);
				}
			}
			return 0;
		}
		break;
	}
	oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static INT_PTR CALLBACK AddrInputDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		EndDialog(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		{
			RECT r;
			WNDPROC oldproc;
			DWORD msgpos = GetMessagePos();
			HWND hwnd = GetDlgItem(hDlg, IDC_DBG_MEMINPUT2);
			SendMessage(hwnd, EM_LIMITTEXT, 8, 0);
			oldproc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)MemInputProc);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)oldproc);
			GetWindowRect(hDlg, &r);
			r.right -= r.left;
			r.bottom -= r.top;
			r.left = GET_X_LPARAM(msgpos) - r.right / 2;
			r.top = GET_Y_LPARAM(msgpos) - r.bottom / 2;
			MoveWindow(hDlg, r.left, r.top, r.right, r.bottom, FALSE);
			ShowWindow(hDlg, SW_SHOWNORMAL);
			SetFocus(GetDlgItem(hDlg, IDC_DBG_MEMINPUT2));
			return TRUE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			{
				TCHAR addrstr[11] = { '0', 'x', '\0' };

				SendMessage(GetDlgItem(hDlg, IDC_DBG_MEMINPUT2), WM_GETTEXT, 9, (LPARAM)(addrstr + 2));
				if (addrstr[2] != 0) {
					uae_u32 addr = _tcstoul(addrstr, NULL, 0);
					if (dbgpage[currpage].selection == IDC_DBG_MEM || dbgpage[currpage].selection == IDC_DBG_MEM2) {
						dbgpage[currpage].memaddr = addr;
						ShowMem(0);
					}
					else {
						if (dbgpage[currpage].autoset)
							dbgpage[currpage].autoset = 2;
						dbgpage[currpage].dasmaddr = addr;
						ShowDasm(0);
					}
				}
				EndDialog(hDlg, 1);
				return TRUE;
			}
		case IDCANCEL:
			EndDialog(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static void CopyListboxText(HWND hwnd, BOOL all)
{
	HANDLE hdata;
	LPWSTR lptstr;
	int i, count, start, end, size = 0;

	if (!OpenClipboard(hwnd))
		return;
	EmptyClipboard();
	if ((count = (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0)) < 1)
		return;
	if (all) {
		start = 0;
		end = count;
	}
	else {
		int id = GetDlgCtrlID(hwnd);
		start = dbgpage[currpage].selection;
		end = start + 1;
	}
	for (i = start; i < end; i++)
		size += (int)(SendMessage(hwnd, LB_GETTEXTLEN, i, 0) + 2);
	size++;
	hdata = GlobalAlloc(GMEM_MOVEABLE, size * sizeof (TCHAR));
	if (hdata) {
		int pos = 0;
		lptstr = (LPWSTR)GlobalLock(hdata);
		lptstr[size - 1] = '\0';
		for (i = start; i < end; i++) {
			int len = (int)SendMessage(hwnd, LB_GETTEXTLEN, i, 0);
			SendMessage(hwnd, LB_GETTEXT, i, (LPARAM)lptstr);
			lptstr[len] = '\r';
			lptstr[len + 1] = '\n';
			lptstr += (len + 2);
		}
		GlobalUnlock(hdata); 
		SetClipboardData(CF_UNICODETEXT, hdata);
	}
	CloseClipboard();
}

static void ToggleBreakpoint(HWND hwnd)
{
	TCHAR addrstr[MAX_LINEWIDTH + 1], *ptr;
	int index = dbgpage[currpage].selection;
	SendMessage(hwnd, LB_GETTEXT, index, (LPARAM)addrstr);
	addrstr[8] = '\0';
	ptr = addrstr;
	console_out_f (_T("\nf %s\n"), addrstr);
	instruction_breakpoint(&ptr);
	RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
}

static void DeleteBreakpoints(HWND hwnd)
{
	TCHAR *cmd = _T("d");
	console_out(_T("\nfd\n"));
	instruction_breakpoint(&cmd);
	RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
}

static void ignore_ws (TCHAR **c)
{
	while (**c && _istspace(**c))
		(*c)++;
}

static void ListboxEndEdit(HWND hwnd, BOOL acceptinput)
{
	MSG msg;
	TCHAR *p, *p2, txt[MAX_LINEWIDTH + 1], tmp[MAX_LINEWIDTH + 1], hexstr[11] = { '0', 'x', '\0' };

	if (!hedit)
		return;
	ReleaseCapture();
	memset(txt, 0, MAX_LINEWIDTH + 1);
	GetWindowText(hedit, txt, MAX_LINEWIDTH + 1);
	p = txt;
	ignore_ws(&p);
	if ((GetWindowTextLength(hedit) == 0) || (p[0] == '\0'))
		acceptinput = FALSE;
	while (PeekMessage(&msg, hedit, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyWindow(hedit);
	hedit = NULL;
	if (acceptinput) {
		int index = dbgpage[currpage].selection, id = GetDlgCtrlID(hwnd);
		if (id == IDC_DBG_DREG) {
			_tcsncpy(hexstr + 2, txt, 8);
			hexstr[10] = '\0';
			m68k_dreg(regs, index) = _tcstoul(hexstr, NULL, 0);
		}
		else if (id == IDC_DBG_AREG) {
			_tcsncpy(hexstr + 2, txt, 8);
			hexstr[10] = '\0';
			m68k_areg(regs, index) = _tcstoul(hexstr, NULL, 0);
		}
		else if (id == IDC_DBG_FPREG) {
			TCHAR *stopstr;
			double value;
			errno = 0;
			value = _tcstod(txt, &stopstr);
			if (stopstr[0] == '\0' && errno == 0)
				regs.fp[index].fp = _tcstod(txt, &stopstr);
		}
		else {
			int bytes, i, offset = -1;
			uae_u8 value;
			uae_u32 addr;
			SendMessage(hwnd, LB_GETTEXT, index, (LPARAM)tmp);
			if (id == IDC_DBG_AMEM) {
				addr = m68k_areg(regs, index);
				offset = 0;
				bytes = 16;
			}
			else if (id == IDC_DBG_MEM || id == IDC_DBG_MEM2) {
				_tcsncpy(hexstr + 2, tmp, 8);
				hexstr[10] = '\0';
				addr = _tcstoul(hexstr, NULL, 0);
				offset = 9;
				bytes = 16;
			}
			else if (id == IDC_DBG_DASM || id == IDC_DBG_DASM2) {
				_tcsncpy(hexstr + 2, tmp, 8);
				hexstr[10] = '\0';
				addr = _tcstoul(hexstr, NULL, 0);
				bytes = 0;
				p = tmp + 9;
				while (_istxdigit(p[0]) && p[4] == ' ') {
					bytes += 2;
					p += 5;
				}
			}
			if (offset >= 0 && !_istxdigit(tmp[offset])) {
				int t = 0;
				do {
					t += 5;
					addr += 2;
					bytes -= 2;
				} while (!_istxdigit(tmp[offset + t]) && !_istxdigit(tmp[offset + t + 1]) && _istspace(tmp[offset + t + 4]));
			}
			p = txt;
			for (i = 0; i < bytes; i++) {
				ignore_ws(&p);
				if (!_istxdigit(p[0]))
					break;
				p2 = p + 1;
				ignore_ws(&p2);
				if (!_istxdigit(p2[0]))
					break;
				hexstr[2] = p[0];
				hexstr[3] = p2[0];
				hexstr[4] = '\0';
				value = (uae_u8)_tcstoul(hexstr, NULL, 0);
				put_byte(addr, value);
				p = p2 + 1;
				addr++;
			}
		}
		update_debug_info();
	}
	else
		RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
}

static LRESULT CALLBACK ListboxEditProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HANDLE hdata;
	LPWSTR lptstr;
	TCHAR allowed[] = _T("1234567890abcdefABCDEF ");
	int ok = 1, id;
	WNDPROC oldproc;
	HWND hparent = GetParent(hWnd);
	id = GetDlgCtrlID(hparent);
	if (id == IDC_DBG_DREG || id == IDC_DBG_AREG)
		allowed[_tcslen(allowed) - 1] = '\0'; // no space
	else if (id == IDC_DBG_FPREG)
		_stprintf(allowed, _T("1234567890deDE.+-"));
	switch (message) {
	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;
	case WM_MOUSELEAVE:
		{
			HWND hwcapt = GetCapture();
			if (!hwcapt)
				SetCapture(hWnd);
			break;
		}
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			RECT rc;
			GetClientRect(hWnd, &rc);
			if (!PtInRect(&rc, pt))
				ListboxEndEdit(hparent, TRUE);
			break;
		}
	case WM_CHAR:
		switch (wParam) {
		case VK_BACK:
		case VK_CANCEL:	//ctrl+c
		case VK_FINAL:	//ctrl+x
		case 0x16:		//ctrl+v
			break;
		case VK_ESCAPE:
			ListboxEndEdit(hparent, FALSE);
			return 0;
		default:
			if (!_tcschr(allowed, (TCHAR)wParam))
				return 0;
			break;
		}
		break;
	case WM_PASTE:
		if (!OpenClipboard(NULL))
			return TRUE;
		hdata = GetClipboardData(CF_UNICODETEXT);
		if (hdata) {
			lptstr = (LPWSTR)GlobalLock(hdata);
			if (lptstr) {
				if (_tcsspn(lptstr, allowed) != _tcslen(lptstr))
					ok = 0;
				GlobalUnlock(hdata);
			}
		}
		CloseClipboard();
		if (!ok)
			return 0;
		break;
	case WM_KEYUP:
		if (wParam == VK_RETURN)
			ListboxEndEdit(hparent, TRUE);
		break;
	}
	oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static void ListboxEdit(HWND hwnd, int x, int y)
{
	int size, id, offset, length, index, radjust = 0;
	RECT rc, ri;
	HFONT hfont;
	WNDPROC oldproc;
	TCHAR txt[MAX_LINEWIDTH + 1], tmp[MAX_LINEWIDTH + 1];
	if (!debugger_active || hedit)
		return;
	if (!hwnd)
		hwnd = GetParent(hedit);
	id = GetDlgCtrlID(hwnd);
	if (id == IDC_DBG_DREG || id == IDC_DBG_AREG) {
		offset = 4;
		length = 0;
	}
	else if(id == IDC_DBG_MEM || id == IDC_DBG_MEM2) {
		offset = 9;
		length = 39;
	}
	else if (id == IDC_DBG_AMEM) {
		offset = 0;
		length = 39;
	}
	else if (id == IDC_DBG_DASM || id == IDC_DBG_DASM2) {
		offset = 9;
		length = 0;
	}
	else if (id == IDC_DBG_FPREG) {
		offset = 5;
		length = 0;
	}
	else
		return;
	hedit = CreateWindow(_T("Edit"), _T("Listbox Edit"), WS_BORDER | WS_CHILD, 0, 0, 1, 1, hwnd, NULL, hInst, NULL);
	if (!hedit)
		return;
	size = GetTextSize(hwnd, NULL, 0);
	index = y / size;
	memset(txt, 0, MAX_LINEWIDTH + 1);
	SendMessage(hwnd, LB_GETITEMRECT, (WPARAM)index, (LPARAM)&ri);
	SendMessage(hwnd, LB_GETTEXT, (WPARAM)index, (LPARAM)(LPTSTR)txt);
	if (id == IDC_DBG_DASM || id == IDC_DBG_DASM2) {
		while (_istxdigit(txt[offset + length]) && _istspace(txt[offset + length + 4]))
			length += 5;
		length--;
	}
	if (length > 0) {
		int t = 0;
		if (!_istxdigit(txt[offset])) {
			while (_istxdigit(txt[offset + length - t - 1]) && _istspace(txt[offset + length - t - 5]))
				t += 5;
			offset += length - t + 1;
			length = t - 1;
		}
		else if (!_istxdigit(txt[offset + length - 1])) {
			while (_istxdigit(txt[offset + t]) && _istspace(txt[offset + t + 4]))
				t += 5;
			length = t - 1;
		}
		if (length <= 0) {
			ListboxEndEdit(hwnd, FALSE);
			return;
		}
		_tcsncpy(tmp, txt + offset, length);
		tmp[length] = '\0';
		radjust = GetTextSize(hwnd, tmp, TRUE);
	}
	else if (id == IDC_DBG_FPREG)
		length = 20;
	else
		length = uaetcslen(txt + offset);
	_tcsncpy(tmp, txt, offset);
	tmp[offset] = '\0';
	ri.left += GetTextSize(hwnd, tmp, TRUE);
	if (radjust)
		ri.right = ri.left + radjust;
	InflateRect(&ri, 2, 2);
	GetClientRect(hwnd, &rc);
	if (ri.left < 0)
		OffsetRect(&ri, 2, 0);
	else if (ri.right > rc.right)
		OffsetRect(&ri, -2, 0);
	if (index == 0)
		OffsetRect(&ri, 0, 2);
	else if (ri.bottom > rc.bottom)
		OffsetRect(&ri, 0, -2);
	if (id == IDC_DBG_DASM || id == IDC_DBG_DASM2)
		OffsetRect(&ri, 2 * size, 0);
	SendMessage(hedit, EM_LIMITTEXT, length, 0);
	MoveWindow(hedit, ri.left, ri.top, ri.right - ri.left, ri.bottom - ri.top, FALSE);
	ShowWindow(hedit, SW_SHOWNORMAL);
	oldproc = (WNDPROC)SetWindowLongPtr(hedit, GWLP_WNDPROC, (LONG_PTR)ListboxEditProc);
	SetWindowLongPtr(hedit, GWLP_USERDATA, (LONG_PTR)oldproc);
	hfont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
	SendMessage(hedit, WM_SETFONT, (WPARAM)hfont, (LPARAM)TRUE);
	memset(txt + offset + length, 0, MAX_LINEWIDTH + 1 - offset - length);
	SetWindowText(hedit, txt + offset);
	SetFocus(hedit);
	SetCapture(hedit);
	dbgpage[currpage].selection = index;
}

static void ToggleCCRFlag(HWND hwnd, int x, int y)
{
	int size = GetTextSize(hwnd, NULL, 0);
	int index = y / size;
	TCHAR txt[MAX_LINEWIDTH + 1];

	memset(txt, 0, MAX_LINEWIDTH + 1);
	SendMessage(hwnd, LB_GETTEXT, (WPARAM)index, (LPARAM)(LPTSTR)txt);
	switch (txt[0]) {
	case 'X':
		SET_XFLG(GET_XFLG() ? 0 : 1);
		break;
	case 'N':
		SET_NFLG(GET_NFLG() ? 0 : 1);
		break;
	case 'Z':
		SET_ZFLG(GET_ZFLG() ? 0 : 1);
		break;
	case 'V':
		SET_VFLG(GET_VFLG() ? 0 : 1);
		break;
	case 'C':
		SET_CFLG(GET_CFLG() ? 0 : 1);
		break;
	}
	update_debug_info();
}

static void set_fpsr (uae_u32 x)
{
	uae_u32 dhex_nan[]   ={0xffffffff, 0x7fffffff};
	double *fp_nan    = (double *)dhex_nan;
	regs.fpsr = x;

	if (x & 0x01000000) {
		regs.fp_result.fp = *fp_nan;
	}
	else if (x & 0x04000000)
		regs.fp_result.fp = 0;
	else if (x & 0x08000000)
		regs.fp_result.fp = -1;
	else
		regs.fp_result.fp = 1;
}

static void ToggleFPSRFlag(HWND hwnd, int x, int y)
{
	int size = GetTextSize(hwnd, NULL, 0);
	int index = y / size;
	uae_u32 fpsr = fpp_get_fpsr();

	fpsr ^= (0x8000000 >> index);
	set_fpsr(fpsr);
	update_debug_info();
}

static LRESULT CALLBACK ListboxProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	WNDPROC oldproc;
	HWND hinput, hsbar;
	RECT rc, r;
	int i, itemheight, count, height, bottom, width, id, top;
	PAINTSTRUCT ps;
	DRAWITEMSTRUCT dis;
	HFONT oldfont, font;
	HDC hdc, compdc;
	HBITMAP compbmp, oldbmp;

	switch (message) {
	case WM_CHAR:
		if (debugger_active) {
			hinput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
			SetFocus(hinput);
			SendMessage(hinput, WM_CHAR, wParam, lParam);
		}
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SETFOCUS:
		return 0;
	case WM_LBUTTONDBLCLK:
		if (debugger_active) {
			int id = GetDlgCtrlID(hWnd);
			if (id == IDC_DBG_CCR)
				ToggleCCRFlag(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			else if (id == IDC_DBG_FPSR)
				ToggleFPSRFlag(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			else
				ListboxEdit(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		}
		return 0;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		GetClientRect(hWnd, &rc);
		height = rc.bottom - rc.top;
		width = rc.right - rc.left;
		bottom = rc.bottom;
		itemheight = (int)SendMessage(hWnd, LB_GETITEMHEIGHT, 0, 0);
		rc.bottom = itemheight;
		count = (int)SendMessage(hWnd, LB_GETCOUNT, 0, 0);
		compdc = CreateCompatibleDC(hdc);
		compbmp = CreateCompatibleBitmap(hdc, width, height);
		oldbmp = (HBITMAP)SelectObject(compdc, compbmp);
		font = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
		oldfont = (HFONT)SelectObject(compdc, font);
		id = GetDlgCtrlID(hWnd);
		dis.CtlType = ODT_LISTBOX;
		dis.CtlID = id;
		dis.itemAction = 0;
		dis.itemState = 0;
		dis.hwndItem = hWnd;
		dis.hDC = compdc;
		top = (int)SendMessage(hWnd, LB_GETTOPINDEX, 0, 0);
		for (i = top; i < count && rc.top < height; i++) {
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
		return 0;
	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case ID_DBG_SETTOA0:
		case ID_DBG_SETTOA1:
		case ID_DBG_SETTOA2:
		case ID_DBG_SETTOA3:
		case ID_DBG_SETTOA4:
		case ID_DBG_SETTOA5:
		case ID_DBG_SETTOA6:
		case ID_DBG_SETTOA7:
			dbgpage[currpage].memaddr = m68k_areg(regs, LOWORD(wParam) - ID_DBG_SETTOA0);
			ShowMem(0);
			return 0;
		case ID_DBG_SETTOPC:
			dbgpage[currpage].dasmaddr =  m68k_getpc();
			ShowDasm(0);
			return 0;
		case ID_DBG_ENTERADDR:
			dbgpage[currpage].selection = GetDlgCtrlID(hWnd);
			CustomDialogBox(IDD_DBGMEMINPUT, hWnd, (DLGPROC)AddrInputDialogProc);
			return 0;
		case ID_DBG_COPYLBLINE:
			CopyListboxText(hWnd, FALSE);
			return 0;
		case ID_DBG_COPYLB:
			CopyListboxText(hWnd, TRUE);
			return 0;
		case ID_DBG_TOGGLEBP:
			ToggleBreakpoint(hWnd);
			return 0;
		case ID_DBG_DELETEBPS:
			DeleteBreakpoints(hWnd);
			return 0;
		}
		break;
	}
	oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK EditProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	WNDPROC oldproc;
	HWND hinput;

	switch (message) {
	case WM_CHAR:
		if (wParam != VK_CANCEL) { // not for Ctrl-C for copying
			if (debugger_active)  {
				hinput = GetDlgItem(hDbgWnd, IDC_DBG_INPUT);
				SetFocus(hinput);
				SendMessage(hinput, WM_CHAR, wParam, lParam);
			}
			return 0;
		}
		break;
	}
	oldproc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	return CallWindowProc(oldproc, hWnd, message, wParam, lParam);
}

static void moveupdown(int dir)
{
	if (pagetype == IDC_DBG_MEM || pagetype == IDC_DBG_MEM2) {
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

static int randidx;
static BOOL CALLBACK childenumproc (HWND hwnd, LPARAM lParam)
{
	int id1y[] = { IDC_DBG_OUTPUT2, IDC_DBG_MEM, IDC_DBG_DASM, IDC_DBG_BRKPTS, IDC_DBG_MISC, IDC_DBG_CUSTOM, -1 };
	int id2y[] = { IDC_DBG_INPUT, IDC_DBG_HELP, IDC_DBG_STATUS, -1 };
	int id3y[] = { IDC_DBG_DASM2, IDC_DBG_MEM2, IDC_DBG_OUTPUT1, -1 };

	int id1x[] = { IDC_DBG_OUTPUT1, IDC_DBG_OUTPUT2, IDC_DBG_MEM, IDC_DBG_MEM2, IDC_DBG_DASM, IDC_DBG_DASM2,
		IDC_DBG_AMEM, IDC_DBG_PREFETCH, IDC_DBG_INPUT, IDC_DBG_STATUS, IDC_DBG_BRKPTS, IDC_DBG_MISC, IDC_DBG_CUSTOM, -1 };
	int id2x[] = { IDC_DBG_HELP, IDC_DBG_CCR, IDC_DBG_SP_VBR, IDC_DBG_MMISC,
		IDC_DBG_FPREG, IDC_DBG_FPSR, IDC_DBG_MCUSTOM, IDC_DBG_MISCCPU, -1 };

	int dlgid, j, count, adjust, remainder, starty;
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
	count = sizeof(id3y) / sizeof(int) - 1;
	adjust = height_adjust / count;
	remainder = height_adjust % count;
	if (randidx < 0) {
		srand((unsigned int)time(NULL));
		randidx = rand() % count;
	}
	while (id3y[j] >= 0) {
		if (id3y[j] == dlgid) {
			starty = j * adjust;
			if (j < randidx)
				adjustitem(hwnd, 0, starty, 0, adjust);
			else if (j == randidx)
				adjustitem(hwnd, 0, starty, 0, adjust + remainder);
			else
				adjustitem(hwnd, 0, starty + remainder, 0, adjust);
		}
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
	WINDOWINFO pwi = { sizeof(WINDOWINFO) };
	GetClientRect(hDlg, &r);
	width_adjust = (r.right - r.left) - (dlgRect.right - dlgRect.left);
	height_adjust = (r.bottom - r.top) - (dlgRect.bottom - dlgRect.top);
	GetWindowRect(hDlg, &dlgRect);
	r2.left = r2.top = r2.right = r2.bottom = 0;
	GetWindowInfo(hDlg, &pwi);
	AdjustWindowRectEx(&r2, pwi.dwStyle, FALSE, pwi.dwExStyle);
	dlgRect.left -= r2.left;
	dlgRect.top -= r2.top;
	randidx = -1;
	EnumChildWindows (hDlg, childenumproc, 0);
	dlgRect = r;
	RedrawWindow(hDlg, 0, 0, RDW_INVALIDATE);
}

static BOOL CALLBACK InitChildWindows(HWND hWnd, LPARAM lParam)
{
	int i, id, enable = TRUE, items = 0;
	WNDPROC newproc = NULL, oldproc;
	TCHAR classname[CLASSNAMELENGTH];
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
			if (!_tcscmp(classname, _T("ListBox")))
				newproc = ListboxProc;
			else if (!_tcscmp(classname, _T("Edit")))
				newproc = EditProc;
		}
		break;
	}
	if (newproc) {
		oldproc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)newproc);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)oldproc);
	}
	EnableWindow(hWnd, enable);
	return TRUE;
}

static void step(BOOL over)
{
	if (over)
		_tcscpy(internalcmd, _T("z"));
	else
		_tcscpy(internalcmd, _T("t"));
	useinternalcmd = TRUE;
	inputfinished = 1;
}

static void ShowContextMenu(HWND hwnd, int x, int y)
{
	POINT pt = { x, y };
	HMENU hmenu, hsubmenu;
	int id = GetDlgCtrlID(hwnd);
	if (x == -1 || y == -1) {
		DWORD msgpos = GetMessagePos();
		pt.x = GET_X_LPARAM(msgpos);
		pt.y = GET_Y_LPARAM(msgpos);
	}
	hmenu = LoadMenu(hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE(IDM_DBGCONTEXTMENU));
	if (!hmenu)
		return;
	if (!debugger_active)
		hsubmenu = GetSubMenu(hmenu, 0);
	else if (id == IDC_DBG_MEM || id == IDC_DBG_MEM2)
		hsubmenu = GetSubMenu(hmenu, 1);
	else if (id == IDC_DBG_DASM || id == IDC_DBG_DASM2)
		hsubmenu = GetSubMenu(hmenu, 2);
	TrackPopupMenu(hsubmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
	DestroyMenu(hmenu);
	SendMessage(hwnd, LB_SETCURSEL, -1, 0);
}

static void SelectListboxLine(HWND hwnd, int x, int y)
{
	POINT pt = { x, y };
	int index;
	int size = GetTextSize(hwnd, NULL, 0);
	int id = GetDlgCtrlID(hwnd);
	if (x == -1 || y == -1) {
		DWORD msgpos = GetMessagePos();
		pt.x = GET_X_LPARAM(msgpos);
		pt.y = GET_Y_LPARAM(msgpos);
	}
	ScreenToClient(hwnd, &pt);
	index = pt.y / size;
	SendMessage(hwnd, LB_SETCURSEL, index, 0);
	dbgpage[currpage].selection = index;
}

static INT_PTR CALLBACK DebuggerProc (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND hwnd;
	static BOOL sizing = FALSE;
	switch (message) {
	case WM_INITDIALOG:
		{
			int newpos = 0;
			int x, y, w, h;
			RECT rw;
			HFONT hfont;
			LOGFONT lf;
			GetWindowRect(hDlg, &rw);
			dbgwnd_minx = rw.right - rw.left;
			dbgwnd_miny = rw.bottom - rw.top;
			GetClientRect(hDlg, &dlgRect);
			newpos = 1;
			if (!regqueryint (NULL, _T("DebuggerPosX"), &x))
				newpos = 0;
			if (!regqueryint (NULL, _T("DebuggerPosY"), &y))
				newpos = 0;
			if (!regqueryint (NULL, _T("DebuggerPosW"), &w))
				newpos = 0;
			if (!regqueryint (NULL, _T("DebuggerPosH"), &h))
				newpos = 0;
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
			hfont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
			GetObject(hfont, sizeof(LOGFONT), &lf);
			lf.lfEscapement = lf.lfOrientation = 1800;
			udfont = CreateFontIndirect(&lf);
			return TRUE;
		}
	case WM_CLOSE:
		DestroyWindow(hDlg);
		return TRUE;
	case WM_DESTROY:
		{
			RECT *r;
			int xoffset = 0, yoffset = 0;
			WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
			MONITORINFO mi = { sizeof(MONITORINFO) };
			HMONITOR hmon = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
			if (hmon && GetMonitorInfo(hmon, &mi)) {
				xoffset = mi.rcWork.left - mi.rcMonitor.left;
				yoffset = mi.rcWork.top - mi.rcMonitor.top;
			}
			if (GetWindowPlacement (hDlg, &wp)) {
				r = &wp.rcNormalPosition;
				r->right -= r->left;
				r->bottom -= r->top;
				r->left += xoffset;
				r->top += yoffset;
				regsetint (NULL, _T("DebuggerPosX"), r->left);
				regsetint (NULL, _T("DebuggerPosY"), r->top);
				regsetint (NULL, _T("DebuggerPosW"), r->right);
				regsetint (NULL, _T("DebuggerPosH"), r->bottom);
				regsetint (NULL, _T("DebuggerMaximized"), (IsZoomed(hDlg) || (wp.flags & WPF_RESTORETOMAXIMIZED)) ? 1 : 0);
			}
			hDbgWnd = 0;
			PostQuitMessage(0);
			DeleteFromHistory(histcount);
			DeleteObject(udfont);
			consoleopen = 0;
			deactivate_debugger ();
			return TRUE;
		}
	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *mmi = (MINMAXINFO*)lParam;
			mmi->ptMinTrackSize.x = dbgwnd_minx;
			mmi->ptMinTrackSize.y = dbgwnd_miny;
			return TRUE;
		}
	case WM_ENTERSIZEMOVE:
		sizing = TRUE;
		return FALSE;
	case WM_EXITSIZEMOVE:
		{
			AdjustDialog(hDlg);
			ShowPage(currpage, TRUE);
			sizing = FALSE;
			return TRUE;
		}
	case WM_SIZE:
		{
			if (!sizing && (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)) {
				AdjustDialog(hDlg);
				ShowPage(currpage, TRUE);
			}
			return TRUE;
		}
	case WM_CTLCOLORSTATIC:
		{
			int id = GetDlgCtrlID((HWND)lParam);
			if (id == IDC_DBG_OUTPUT1 || id == IDC_DBG_OUTPUT2) {
				SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
				return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
			}
			return FALSE;
		}
	case WM_CTLCOLORLISTBOX:
		hwnd = (HWND)lParam;
		if (!IsWindowEnabled(hwnd)) {
			SetBkColor((HDC)wParam, GetSysColor(COLOR_3DFACE));
			return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
		}
		SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
		return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
	case WM_COMMAND:
		if (!debugger_active) {
			if (LOWORD(wParam) == IDC_DBG_AUTOSET && HIWORD(wParam) == BN_CLICKED)
				SendMessage((HWND)lParam, BM_SETCHECK, dbgpage[currpage].autoset ? BST_CHECKED : BST_UNCHECKED, 0);
			return TRUE;
		}
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
		case ID_DBG_STEP_OVER:
			step(TRUE);
			return TRUE;
		case ID_DBG_STEP_INTO:
			step(FALSE);
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
		case IDC_DBG_AUTOSET:
			{
				if (pagetype == IDC_DBG_DASM) {
					HWND hctrl;
					dbgpage[currpage].autoset = 1 - dbgpage[currpage].autoset;
					hctrl = GetDlgItem(hDbgWnd, IDC_DBG_AUTOSET);
					SendMessage(hctrl, BM_SETCHECK, dbgpage[currpage].autoset ? BST_CHECKED : BST_UNCHECKED, 0);
					hctrl = GetDlgItem(hDbgWnd, IDC_DBG_MEMINPUT);
					SetFocus(hctrl);
				}
				return TRUE;
			}
		}
		break;
	case WM_CONTEXTMENU:
		{
			int id = GetDlgCtrlID((HWND)wParam);
			if (id == IDC_DBG_MEM || id == IDC_DBG_MEM2 || id == IDC_DBG_DASM || id == IDC_DBG_DASM2) {
				if (!hedit){
					SelectListboxLine((HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
					ShowContextMenu((HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				}
				return TRUE;
			}
			break;
		}
	case WM_MEASUREITEM:
		((MEASUREITEMSTRUCT*)(lParam))->itemHeight = GetTextSize(hDlg, NULL, FALSE);
		return TRUE;
	case WM_DRAWITEM:
		{
			DRAWITEMSTRUCT *pdis = (DRAWITEMSTRUCT *)lParam;
			HDC hdc = pdis->hDC;
			RECT rc = pdis->rcItem;
			TCHAR text[MAX_LINEWIDTH + 1];
			uae_u32 addr;
			SetBkMode(hdc, TRANSPARENT);
			if (wParam == IDC_DBG_STATUS) {
				SetTextColor(hdc, GetSysColor(pstatuscolor[pdis->itemID]));
				DrawText(hdc, pname[pdis->itemID], uaetcslen(pname[pdis->itemID]), &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
				return TRUE;
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
				SetTextColor(hdc, (COLORREF)pdis->itemData);
				if (wParam == IDC_DBG_DASM || wParam == IDC_DBG_DASM2) {
					TCHAR addrstr[11] = { '0', 'x', '\0'}, *btemp;
					int i, j, size = rc.bottom - rc.top;
					_tcsncpy(addrstr + 2, text, 8);
					addrstr[10] = 0;
					addr = _tcstoul(addrstr, NULL, 0);
					for (i = 0; i < BREAKPOINT_TOTAL; i++) {
						if (addr == bpnodes[i].value1 && bpnodes[i].enabled) {
							int offset = 0;
							if (size >= 9)
								offset = 3;
							SelectObject(hdc, GetSysColorBrush(COLOR_HIGHLIGHT));
							Ellipse(hdc, rc.left + offset, rc.top + offset, rc.left + size - offset, rc.bottom - offset);
						}
					}
					rc.left += size;
					i = 0;
					btemp = NULL;
					addrstr[2] = '\0';
					while (ucbranch[i])  {
						if (!_tcsncmp(text + 34, ucbranch[i], _tcslen(ucbranch[i]))) {
							btemp = _tcschr(text + 34, '=');
							if (btemp)
								_tcsncpy(addrstr + 2, btemp + 4, 8);
							else {
								int pos = 34 + uaetcslen(ucbranch[i]) + 3;
								if (text[pos] == '$')	//absolute addressing
									_tcsncpy(addrstr + 2, text + pos + 1, 8);
								else if (text[pos] == '(' && _istdigit(text[pos + 2])) { //address register indirect
									int reg = _tstoi(text + pos + 2);
									uae_u32 loc = m68k_areg (regs, reg);
									_stprintf(addrstr + 2, _T("%08x"), loc);
								}
							}
							break;
						}
						i++;
					}
					i = 0;
					while (addrstr[2] == '\0' && cbranch[i]) {
						if (!_tcsncmp(text + 34, cbranch[i], _tcslen(cbranch[i]))) {
							j = 0;
							while (ccode[j]) {
								if (!_tcsncmp(text + 34 + _tcslen(cbranch[i]), ccode[j], _tcslen(ccode[j]))) {
									btemp = _tcschr(text + 34, '=');
									if (btemp)
										_tcsncpy(addrstr + 2, btemp + 4, 8);
									break;
								}
								j++;
							}
						}
						i++;
					}
					if (addrstr[2] != '\0') {
						uae_u32 branchaddr = _tcstoul(addrstr, NULL, 0);
						if (branchaddr < addr)
							TextOut(hdc, rc.left, rc.top, _T("^"), 1);
						else if (branchaddr > addr) {
							HFONT hfontold = (HFONT)SelectObject(hdc, udfont);
							int width = GetTextSize(hDlg, _T("^"), TRUE);
							TextOut(hdc, rc.left + width, rc.bottom, _T("^"), 1);
							SelectObject(hdc, hfontold);
						}
						else
							TextOut(hdc, rc.left, rc.top, _T("="), 1);
					}
					rc.left += size;
					if (addr == m68k_getpc()) {
						FillRect(hdc, &rc, GetSysColorBrush(COLOR_HIGHLIGHT));
						SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
						SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
					}
					TextOut(hdc, rc.left, rc.top, text, uaetcslen(text));
					i = 0;
					while (markinstr[i])  {
						if (!_tcsncmp(text + 34, markinstr[i], uaetcslen(markinstr[i]))) {
							MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
							LineTo(hdc, rc.right, rc.bottom - 1);
							break;
						}
						i++;
					}
					if ((pdis->itemState) & (ODS_SELECTED))
						DrawFocusRect(hdc, &rc);
				}
				else if (wParam == IDC_DBG_MEM || wParam == IDC_DBG_MEM2) {
					TextOut(hdc, rc.left, rc.top, text, uaetcslen(text));
					if ((pdis->itemState) & (ODS_SELECTED))
						DrawFocusRect(hdc, &rc);
				}
				else
					TextOut(hdc, rc.left, rc.top, text, uaetcslen(text));
				return TRUE;
			}
			break;
		}
	}
	return FALSE;
}

int open_debug_window(void)
{

	struct newresource *nr;
	int maximized;

	if (hDbgWnd)
		return 0;
	debuggerinitializing = TRUE;
	reopen = 0;
	dbgaccel = LoadAccelerators(hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDR_DBGACCEL));
	nr = getresource(IDD_DEBUGGER);
	if (nr) {
		typedef DPI_AWARENESS_CONTEXT(CALLBACK *SETTHREADDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
		SETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext = (SETTHREADDPIAWARENESSCONTEXT)GetProcAddress(userdll, "SetThreadDpiAwarenessContext");
		DPI_AWARENESS_CONTEXT ac = DPI_AWARENESS_CONTEXT_UNAWARE;
		if (pSetThreadDpiAwarenessContext) {
			ac = pSetThreadDpiAwarenessContext(ac);
		}
		hDbgWnd = CreateDialogIndirect (nr->inst, nr->sourceresource, NULL, DebuggerProc);
		if (pSetThreadDpiAwarenessContext) {
			pSetThreadDpiAwarenessContext(ac);
		}
	}
	freescaleresource(nr);
	debuggerinitializing = FALSE;
	if (!hDbgWnd)
		return 0;
	rawinput_release();
	InitPages();
	ShowPage(0, TRUE);
	if (!regqueryint (NULL, _T("DebuggerMaximized"), &maximized))
		maximized = 0;
	ShowWindow(hDbgWnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
	UpdateWindow(hDbgWnd);
	SetForegroundWindow (hDbgWnd);
	update_debug_info();
	return 1;
}

void close_debug_window(void)
{
	DestroyWindow(hDbgWnd);
	rawinput_alloc();
}

int console_get_gui (TCHAR *out, int maxlen)
{
	MSG msg;
	int ret;

	while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0) {
		if (!debugger_active || ret == -1) {
			return -1;
		} else if (!TranslateAccelerator(hDbgWnd, dbgaccel, &msg)) {
			if (!IsWindow(hDbgWnd) || !IsDialogMessage(hDbgWnd, &msg)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		if (inputfinished) {
			if (dbgpage[currpage].autoset == 2)
				dbgpage[currpage].autoset = 1; 
			inputfinished = 0;
			if (useinternalcmd) {
				useinternalcmd = FALSE;
				console_out(_T("\n"));
				console_out(internalcmd);
				console_out(_T("\n"));
				_tcsncpy(out, internalcmd, maxlen);
				return uaetcslen(out);
			}
			else
				return GetInput(out, maxlen);
		}
	}
	return 0;
}

void update_debug_info(void)
{
	int i;
	TCHAR out[MAX_LINEWIDTH + 1];
	HWND hwnd;
	struct instr *dp;
	struct mnemolookup *lookup1, *lookup2;
	uae_u32 fpsr;
	TCHAR *fpsrflag[] = { _T("N:   "), _T("Z:   "), _T("I:   "), _T("NAN: ") };

	if (!hDbgWnd)
		return;
	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_DREG);
	for (i = 0; i < 8; i++) {
		_stprintf(out, _T("D%d: %08X"), i, m68k_dreg (regs, i));
		UpdateListboxString(hwnd, i, out, TRUE);
	}

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_AREG);
	for (i = 0; i < 8; i++) {
		hwnd = GetDlgItem(hDbgWnd, IDC_DBG_AREG);
		_stprintf(out, _T("A%d: %08X"), i, m68k_areg (regs, i));
		UpdateListboxString(hwnd, i, out, TRUE);
		hwnd = GetDlgItem(hDbgWnd, IDC_DBG_AMEM);
		dumpmem2(m68k_areg (regs, i), out, sizeof(out));
		UpdateListboxString(hwnd, i, out + 9, TRUE);
	}

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_CCR);
	UpdateListboxString(hwnd, 0, GET_XFLG() ? _T("X: 1") : _T("X: 0"), TRUE);
	UpdateListboxString(hwnd, 1, GET_NFLG() ? _T("N: 1") : _T("N: 0"), TRUE);
	UpdateListboxString(hwnd, 2, GET_ZFLG() ? _T("Z: 1") : _T("Z: 0"), TRUE);
	UpdateListboxString(hwnd, 3, GET_VFLG() ? _T("V: 1") : _T("V: 0"), TRUE);
	UpdateListboxString(hwnd, 4, GET_CFLG() ? _T("C: 1") : _T("C: 0"), TRUE);

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_SP_VBR);
	_stprintf(out, _T("USP: %08X"), regs.usp);
	UpdateListboxString(hwnd, 0, out, TRUE);
	_stprintf(out, _T("ISP: %08X"), regs.isp);
	UpdateListboxString(hwnd, 1, out, TRUE);
	_stprintf(out, _T("SR:  %04X"), regs.sr);
	UpdateListboxString(hwnd, 2, out, TRUE);

	ShowMiscCPU(GetDlgItem(hDbgWnd, IDC_DBG_MISCCPU));

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_MMISC);
	_stprintf(out, _T("T:     %d%d"), regs.t1, regs.t0);
	UpdateListboxString(hwnd, 0, out, TRUE);
	_stprintf(out, _T("S:     %d"), regs.s);
	UpdateListboxString(hwnd, 1, out, TRUE);
	_stprintf(out, _T("M:     %d"), regs.m);
	UpdateListboxString(hwnd, 2, out, TRUE);
	_stprintf(out, _T("IMASK: %d"), regs.intmask);
	UpdateListboxString(hwnd, 3, out, TRUE);
	_stprintf(out, _T("STP:   %d"), regs.stopped);
	UpdateListboxString(hwnd, 4, out, TRUE);

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_PC);
	_stprintf(out, _T("PC: %08X"), m68k_getpc ());
	UpdateListboxString(hwnd, 0, out, TRUE);

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_PREFETCH);
	dp = table68k + regs.irc;
	for (lookup1 = lookuptab; lookup1->mnemo != dp->mnemo; lookup1++);
	dp = table68k + regs.ir;
	for (lookup2 = lookuptab; lookup2->mnemo != dp->mnemo; lookup2++);
	_stprintf(out, _T("Prefetch: %04X (%s) %04X (%s)"), regs.irc, lookup1->name, regs.ir, lookup2->name);
	UpdateListboxString(hwnd, 0, out, TRUE);

	ShowCustomSmall(GetDlgItem(hDbgWnd, IDC_DBG_MCUSTOM));

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_FPREG);
	for (i = 0; i < 8; i++) {
		_stprintf(out, _T("FP%d: %s"), i, fpp_print(&regs.fp[i], 0));
		UpdateListboxString(hwnd, i, out, TRUE);
	}

	hwnd = GetDlgItem(hDbgWnd, IDC_DBG_FPSR);
	fpsr = fpp_get_fpsr();
	for (i = 0; i < 4; i++) {
		_stprintf(out, _T("%s%d"), fpsrflag[i], (fpsr & (0x8000000 >> i)) != 0 ? 1 : 0);
		UpdateListboxString(hwnd, i, out, TRUE);
	}
	ShowPage(currpage, TRUE);
}

void update_disassembly(uae_u32 addr)
{
	if (!hDbgWnd || (pagetype != IDC_DBG_DASM && currpage != 0))
		return;
	if (dbgpage[currpage].autoset)
		dbgpage[currpage].autoset = 2;
	dbgpage[currpage].dasmaddr = addr;
	ShowDasm(0);
}

void update_memdump(uae_u32 addr)
{
	if (!hDbgWnd || (pagetype != IDC_DBG_MEM && currpage != 0))
		return;
	dbgpage[currpage].memaddr = addr;
	ShowMem(0);
}
