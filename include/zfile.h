 /*
  * UAE - The Un*x Amiga Emulator
  *
  * routines to handle compressed file automatically
  *
  * (c) 1996 Samuel Devulder
  */

struct zfile;
extern int is_zlib;

extern struct zfile *zfile_fopen (const char *, const char *);
extern struct zfile *zfile_fopen_empty (const char *name, int size);
extern int zfile_exists (const char *name);
extern void zfile_fclose (struct zfile *);
extern int zfile_fseek (struct zfile *z, long offset, int mode);
extern long zfile_ftell (struct zfile *z);
extern size_t zfile_fread  (void *b, size_t l1, size_t l2, struct zfile *z);
extern size_t zfile_fwrite  (void *b, size_t l1, size_t l2, struct zfile *z);
extern void zfile_exit (void);
extern int execute_command (char *);
extern int zfile_iscompressed (struct zfile *z);
extern int zfile_zcompress (struct zfile *dst, void *src, int size);
extern int zfile_zuncompress (void *dst, int dstsize, struct zfile *src, int srcsize);
extern int zfile_gettype (struct zfile *z);

#define ZFILE_UNKNOWN 0
#define ZFILE_CONFIGURATION 1
#define ZFILE_DISKIMAGE 2
#define ZFILE_ROM 3
#define ZFILE_KEY 4
#define ZFILE_HDF 5
#define ZFILE_STATEFILE 6
#define ZFILE_NVR 7