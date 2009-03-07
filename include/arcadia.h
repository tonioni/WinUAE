
#ifdef ARCADIA

extern void arcadia_init (void);
extern int is_arcadia_rom (const TCHAR *path);
extern int arcadia_map_banks (void);
extern void arcadia_unmap (void);
extern void arcadia_vsync (void);
extern void arcadia_reset (void);
extern uae_u8 arcadia_parport (int port, uae_u8 pra, uae_u8 dra);
extern struct romdata *scan_arcadia_rom (TCHAR*, int);

struct arcadiarom {
    int romid;
    TCHAR *name, *rom;
    int type, extra;
    int b7, b6, b5, b4, b3, b2, b1, b0;
};

extern struct arcadiarom *arcadia_bios, *arcadia_game;
extern int arcadia_flag, arcadia_coin[2];

#define NO_ARCADIA_ROM 0
#define ARCADIA_BIOS 1
#define ARCADIA_GAME 2

#endif