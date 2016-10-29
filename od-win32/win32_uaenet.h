
#include "ethernet.h"

typedef void (uaenet_gotfunc)(void *dev, const uae_u8 *data, int len);
typedef int (uaenet_getfunc)(void *dev, uae_u8 *d, int *len);

extern struct netdriverdata *uaenet_enumerate (const TCHAR *name);
extern void uaenet_enumerate_free (void);
extern void uaenet_close_driver (struct netdriverdata *tc);

extern int uaenet_getdatalenght (void);
extern int uaenet_getbytespending (void*);
extern int uaenet_open (void*, struct netdriverdata*, void*, uaenet_gotfunc*, uaenet_getfunc*, int, const uae_u8*);
extern void uaenet_close (void*);
extern void uaenet_trigger (void*);

