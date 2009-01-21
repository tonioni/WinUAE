
#include "sysconfig.h"
#include "sysdeps.h"

#include <stdlib.h>
#include <stdarg.h>

#include <windows.h>

#include "clipboard_win32.h"
#include "clipboard.h"

#include "memory.h"
#include "native2amiga_api.h"

static HWND chwnd;
static uaecptr clipboard_data;
static int vdelay, signaling;
static uae_u8 *to_amiga;
static uae_u32 to_amiga_size;
static int clipopen;

void clipboard_vsync (void)
{
    uaecptr task;
    if (!signaling || !clipboard_data)
	return;
    vdelay--;
    if (vdelay > 0)
	return;
    task = get_long (clipboard_data + 8);
    if (task)
	uae_Signal (task, 1 << 13);
    vdelay = 50;
}

void clipboard_reset (void)
{
    vdelay = 100;
    clipboard_data = 0;
    signaling = 0;
    xfree (to_amiga);
    to_amiga = NULL;
    to_amiga_size = 0;
}

void clipboard_init (HWND hwnd)
{
    chwnd = hwnd;
}

static void to_amiga_start (void)
{
    if (!clipboard_data)
	return;
    put_long (clipboard_data, to_amiga_size);
    uae_Signal (get_long (clipboard_data + 8), 1 << 13);
}

static char *pctoamiga (const char *txt)
{
    int len;
    char *txt2;
    int i, j;

    len = strlen (txt) + 1;
    txt2 = xmalloc (len);
    j = 0;
    for (i = 0; i < len; i++) {
	char c = txt[i];
	if (c == 13)
	    continue;
	txt2[j++] = c;
    }
    return txt2;
}
static char *amigatopc (const char *txt)
{
    int i, j, cnt;
    int len, pc;
    char *txt2;

    pc = 0;
    cnt = 0;
    len = strlen (txt) + 1;
    for (i = 0; i < len; i++) {
	char c = txt[i];
	if (c == 13)
	    pc = 1;
	if (c == 10)
	    cnt++;
    }
    if (pc)
        return my_strdup (txt);
    txt2 = xmalloc (len + cnt);
    j = 0;
    for (i = 0; i < len; i++) {
	char c = txt[i];
	if (c == 0 && i + 1 < len)
	    continue;
	if (c == 10)
	    txt2[j++] = 13;
	txt2[j++] = c;
    }
    return txt2;
}


static void to_iff_text (char *pctxt)
{
    uae_u8 b[] = { 'F','O','R','M',0,0,0,0,'F','T','X','T','C','H','R','S',0,0,0,0 };
    uae_u32 size;
    int txtlen;
    char *txt;
    
    txt = pctoamiga (pctxt);
    txtlen = strlen (txt);
    xfree (to_amiga);
    size = txtlen + sizeof b + (txtlen & 1) - 8;
    b[4] = size >> 24;
    b[5] = size >> 16;
    b[6] = size >>  8;
    b[7] = size >>  0;
    size = txtlen;
    b[16] = size >> 24;
    b[17] = size >> 16;
    b[18] = size >>  8;
    b[19] = size >>  0;
    to_amiga_size = sizeof b + txtlen + (txtlen & 1);
    to_amiga = xcalloc (to_amiga_size, 1);
    memcpy (to_amiga, b, sizeof b);
    memcpy (to_amiga + sizeof b, txt, strlen (txt));
    to_amiga_start ();
    xfree (txt);
}

static void from_iff_text (uaecptr ftxt, uae_u32 len)
{
    uae_u32 size;
    uae_u8 *addr;
    char *txt, *pctxt;

    if (len < 18)
	return;
    if (!valid_address (ftxt, len))
	return;
    addr = get_real_address (ftxt);
    if (memcmp ("FORM", addr, 4))
	return;
    if (memcmp ("FTXTCHRS", addr + 8, 8))
	return;
    size = (addr[16] << 24) | (addr[17] << 16) | (addr[18] << 8) | (addr[19] << 0);
    if (size >= len)
	return;
    txt = xcalloc (size + 1, 1);
    if (!txt)
	return;
    memcpy (txt, addr + 20, size);
    pctxt = amigatopc (txt);
    clipboard_put_text (pctxt);
    xfree (pctxt);
    xfree (txt);
}

void clipboard_changed (HWND hwnd)
{
    HGLOBAL hglb;
    UINT f;
    int text = FALSE;
    
    if (!clipboard_data)
	return;
    if (clipopen)
	return;
    if (!OpenClipboard (hwnd))
	return;
    f = 0;
    write_log ("clipboard: windows clipboard change: ");
    while (f = EnumClipboardFormats (f)) {
	write_log ("%d ", f);
	if (f == CF_TEXT)
	    text = TRUE;
    }
    write_log ("\n");
    if (text) {
	hglb = GetClipboardData (CF_TEXT); 
	if (hglb != NULL) { 
	    char *lptstr = GlobalLock (hglb); 
	    if (lptstr != NULL) {
		to_iff_text (lptstr);
		GlobalUnlock (hglb);
	    }
	}
    }
    CloseClipboard ();
}

int clipboard_put_text (const char *txt)
{
    HGLOBAL hglb;
    int ret = FALSE;

    if (!OpenClipboard (chwnd)) 
	return ret;
    clipopen++;
    EmptyClipboard (); 
    hglb = GlobalAlloc (GMEM_MOVEABLE, strlen (txt) + 1);
    if (hglb) {
	char *lptstr = GlobalLock (hglb);
	strcpy (lptstr, txt);
	GlobalUnlock (hglb);
	SetClipboardData (CF_TEXT, hglb); 
	ret = TRUE;
    }
    CloseClipboard ();
    clipopen--;
    write_log ("clipboard: writing to windows clipboard\n");
    return ret;
}

void amiga_clipboard_init (void)
{
    signaling = 0;
    write_log ("clipboard active\n");
}

void amiga_clipboard_task_start (uaecptr data)
{
    clipboard_data = data;
    signaling = 1;
    write_log ("clipboard task init: %08x\n", data);
}

uae_u32 amiga_clipboard_proc_start (void)
{
    write_log ("clipboard process init\n");
    signaling = 1;
    return clipboard_data;
}

void amiga_clipboard_got_data (uaecptr data, uae_u32 size, uae_u32 actual)
{
    uae_u8 *addr = get_real_address (data);
    write_log ("clipboard: <-amiga, %08x %d %d\n", data, size, actual);
    from_iff_text (data, actual);
}

void amiga_clipboard_want_data (void)
{
    uae_u32 addr;

    addr = get_long (clipboard_data + 4);
    if (addr) {
	uae_u8 *raddr = get_real_address (addr);
	memcpy (raddr, to_amiga, to_amiga_size);
    }
    xfree (to_amiga);
    write_log ("clipboard: ->amiga, %08x %d bytes\n", addr, to_amiga_size);
    to_amiga = NULL;
    to_amiga_size = 0;
}
