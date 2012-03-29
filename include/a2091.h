#ifdef A2091

extern addrbank dmaca2091_bank;

extern void a2091_init (void);
extern void a2091_free (void);
extern void a2091_reset (void);

extern void a3000scsi_init (void);
extern void a3000scsi_free (void);
extern void a3000scsi_reset (void);
extern void rethink_a2091 (void);

extern void wdscsi_put (uae_u8);
extern uae_u8 wdscsi_get (void);
extern uae_u8 wdscsi_getauxstatus (void);
extern void wdscsi_sasr (uae_u8);

extern void scsi_hsync (void);

extern uae_u8 wdregs[32];
extern struct scsi_data *scsis[8];

#define WD33C93 _T("WD33C93")
#define SCSIID (scsis[wdregs[WD_DESTINATION_ID] & 7])

extern int a2091_add_scsi_unit (int ch, const TCHAR *path, int blocksize, int readonly,
		       const TCHAR *devname, int sectors, int surfaces, int reserved,
		       int bootpri, const TCHAR *filesys);
extern int a3000_add_scsi_unit (int ch, const TCHAR *path, int blocksize, int readonly,
		       const TCHAR *devname, int sectors, int surfaces, int reserved,
		       int bootpri, const TCHAR *filesys);

extern int addscsi (int ch, const TCHAR *path, int blocksize, int readonly,
		       const TCHAR *devname, int sectors, int surfaces, int reserved,
		       int bootpri, const TCHAR *filesys, int scsi_level);

#endif
