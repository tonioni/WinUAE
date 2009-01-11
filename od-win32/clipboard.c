
#include "sysconfig.h"
#include "sysdeps.h"

#include <stdlib.h>
#include <stdarg.h>

#include <windows.h>

#include "clipboard.h"

static HWND chwnd;

void clipboard_init (HWND hwnd)
{
    chwnd = hwnd;
}

void clipboard_changed (HWND hwnd)
{
    HGLOBAL hglb;
    
    if (!IsClipboardFormatAvailable (CF_TEXT)) 
	return;
    if (!OpenClipboard (hwnd))
	return;
    hglb = GetClipboardData (CF_TEXT); 
    if (hglb != NULL) { 
	char *lptstr = GlobalLock (hglb); 
	if (lptstr != NULL) {
	    GlobalUnlock (hglb);
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
    return ret;
}
