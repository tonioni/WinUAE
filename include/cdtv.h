
#ifdef CDTV

extern addrbank dmac_bank;

extern void cdtv_init (void);
extern void cdtv_free (void);
extern void CDTV_hsync_handler(void);

extern void cdtv_entergui (void);
extern void cdtv_exitgui (void);

void cdtv_battram_write (int addr, int v);
uae_u8 cdtv_battram_read (int addr);

extern void cdtv_loadcardmem(uae_u8*, int);
extern void cdtv_savecardmem(uae_u8*, int);

int cdtv_add_scsi_unit(int ch, char *path, int blocksize, int readonly,
		       char *devname, int sectors, int surfaces, int reserved,
		       int bootpri, char *filesys);

extern void cdtv_getdmadata(int *);

#endif

