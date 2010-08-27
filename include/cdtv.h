
#ifdef CDTV

extern addrbank dmac_bank;

extern void cdtv_init (void);
extern void cdtv_free (void);
extern void CDTV_hsync_handler(void);
extern void cdtv_check_banks (void);

void cdtv_battram_write (int addr, int v);
uae_u8 cdtv_battram_read (int addr);

extern void cdtv_loadcardmem (uae_u8*, int);
extern void cdtv_savecardmem (uae_u8*, int);

int cdtv_add_scsi_unit (int ch, TCHAR *path, int blocksize, int readonly,
		       TCHAR *devname, int sectors, int surfaces, int reserved,
		       int bootpri, TCHAR *filesys);

extern void cdtv_getdmadata (uae_u32*);

extern void rethink_cdtv (void);
extern void cdtv_scsi_int (void);
extern void cdtv_scsi_clear_int (void);

extern bool cdtv_front_panel (int);

#endif

