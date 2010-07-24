struct netdriverdata
{
    TCHAR *name;
    TCHAR *desc;
    int mtu;
    uae_u8 mac[6];
    int active;
};

typedef void (uaenet_gotfunc)(struct s2devstruct *dev, const uae_u8 *data, int len);
typedef int (uaenet_getfunc)(struct s2devstruct *dev, uae_u8 *d, int *len);

#define MAX_TOTAL_NET_DEVICES 10

extern struct netdriverdata *uaenet_enumerate (struct netdriverdata **out, const TCHAR *name);
extern void uaenet_enumerate_free (struct netdriverdata *tcp);
extern void uaenet_close_driver (struct netdriverdata *tc);

extern int uaenet_getdatalenght (void);
extern int uaenet_getbytespending (void*);
extern int uaenet_open (void*, struct netdriverdata*, void*, uaenet_gotfunc*, uaenet_getfunc*, int);
extern void uaenet_close (void*);
extern void uaenet_gotdata (struct s2devstruct *dev, const uae_u8 *data, int len);
extern int uaenet_getdata (struct s2devstruct *dev, uae_u8 *d, int *len);
extern void uaenet_trigger (void*);

