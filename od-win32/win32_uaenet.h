struct netdriverdata
{
    TCHAR *name;
    int mtu;
    uae_u8 mac[6];
    int active;
};

#define MAX_TOTAL_NET_DEVICES 10

int uaenet_open_driver (struct netdriverdata *tc);
void uaenet_close_driver (struct netdriverdata *tc);

extern int uaenet_getdatalenght (void);
extern int uaenet_getbytespending (void*);
extern int uaenet_open (void*, struct netdriverdata*, void*, int);
extern void uaenet_close (void*);
extern void uaenet_gotdata (struct devstruct *dev, const uae_u8 *data, int len);
extern int uaenet_getdata (struct devstruct *dev, uae_u8 *d, int *len);
extern void uaenet_trigger (void*);

