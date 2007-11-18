extern int uaenet_getdatalenght (void);
extern int uaenet_getbytespending (void*);
extern int uaenet_open (void*, struct tapdata*, void*, int);
extern void uaenet_close (void*);
extern int uaenet_read (void*, uae_u8 *data, uae_u32 len);
extern int uaenet_write (void*, uae_u8 *data, uae_u32 len);
extern void uaenet_gotdata (struct devstruct *dev, uae_u8 *data, int len);
extern int uaenet_getdata (struct devstruct *dev, uae_u8 *d, int *len);
extern void uaenet_trigger (void*);

