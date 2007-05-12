#ifdef A2091

extern addrbank dmaca2091_bank;

extern void a2091_init (void);
extern void a2091_free (void);
extern void a2091_reset (void);

extern void rethink_a2091 (void);

extern void wdscsi_put(uae_u8);
extern uae_u8 wdscsi_get(void);
extern uae_u8 wdscsi_getauxstatus(void);
extern void wdscsi_sasr(uae_u8);
#endif
