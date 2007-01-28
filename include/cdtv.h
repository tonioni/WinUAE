
#ifdef CDTV

extern addrbank dmac_bank;

extern void dmac_init (void);
extern void CDTV_hsync_handler(void);

extern void cdtv_entergui (void);
extern void cdtv_exitgui (void);

void cdtv_battram_write (int addr, int v);
uae_u8 cdtv_battram_read (int addr);

extern void cdtv_loadcardmem(uae_u8*, int);
extern void cdtv_savecardmem(uae_u8*, int);

#endif

