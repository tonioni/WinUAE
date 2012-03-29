
#define COMPIPENAME _T("WinUAE_COM")

extern void *createIPC (const TCHAR *name, int);
extern void closeIPC (void*);
extern int checkIPC (void*,struct uae_prefs*);
extern void *geteventhandleIPC (void*);
extern int sendBinIPC (void*, uae_u8 *msg, int len);
extern int sendIPC (void*, TCHAR *msg);
extern int isIPC (const TCHAR *pipename);
