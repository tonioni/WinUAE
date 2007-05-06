
extern void gayle_reset(int);
extern void gayle_hsync(void);
extern int gayle_add_ide_unit(int ch, char *path, int blocksize, int readonly,
		       char *devname, int sectors, int surfaces, int reserved,
		       int bootpri, char *filesys);
extern void gayle_free_ide_units(void);
