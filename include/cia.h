 /*
  * UAE - The Un*x Amiga Emulator
  *
  * CIA chip support
  *
  * (c) 1995 Bernd Schmidt
  */

extern void CIA_reset (void);
extern void CIA_vsync_handler (void);
extern void CIA_hsync_handler (void);
extern void CIA_handler (void);

extern void diskindex_handler (void);

extern void dumpcia (void);
extern void rethink_cias (void);
extern unsigned int ciaaicr,ciaaimask,ciabicr,ciabimask;
extern unsigned int ciaacra,ciaacrb,ciabcra,ciabcrb;
extern unsigned int ciabpra;
extern unsigned long ciaata,ciaatb,ciabta,ciabtb;
extern unsigned long ciaatod,ciabtod,ciaatol,ciabtol,ciaaalarm,ciabalarm;
extern int ciaatlatch,ciabtlatch;

extern int parallel_direct_write_data (uae_u8, uae_u8);
extern int parallel_direct_read_data (uae_u8*);
extern int parallel_direct_write_status (uae_u8, uae_u8);
extern int parallel_direct_read_status (uae_u8*);

