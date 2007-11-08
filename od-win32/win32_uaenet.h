extern int uaenet_getdatalenght (void);
extern int uaenet_getbytespending (void*);
extern int uaenet_open (void*, void*, int);
extern void uaenet_close (void*);
extern int uaenet_read (void*, uae_u8 *data, uae_u32 len);
extern int uaenet_write (void*, uae_u8 *data, uae_u32 len);
extern void uaenet_signal (void*, int source);
extern void uaenet_trigger (void*);

