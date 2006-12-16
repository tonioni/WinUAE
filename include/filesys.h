 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Unix file system handler for AmigaDOS
  *
  * Copyright 1997 Bernd Schmidt
  */

struct hardfiledata {
    uae_u64 size;
    uae_u64 offset;
    int nrcyls;
    int secspertrack;
    int surfaces;
    int reservedblocks;
    int blocksize;
    void *handle;
    int handle_valid;
    int readonly;
    int flags;
    uae_u8 *cache;
    int cache_valid;
    uae_u64 cache_offset;
    char vendor_id[8 + 1];
    char product_id[16 + 1];
    char product_rev[4 + 1];
    char device_name[256];
    /* geometry from possible RDSK block */
    unsigned int cylinders;
    unsigned int sectors;
    unsigned int heads;
    uae_u64 size2;
    uae_u64 offset2;
};

#define FILESYS_VIRTUAL 0
#define FILESYS_HARDFILE 1
#define FILESYS_HARDFILE_RDB 2
#define FILESYS_HARDDRIVE 3

#define FILESYS_FLAG_DONOTSAVE 1

#define MAX_FILESYSTEM_UNITS 30

struct uaedev_mount_info;
extern struct uaedev_mount_info options_mountinfo;

extern void filesys_init (void);
extern struct hardfiledata *get_hardfile_data (int nr);
#define FILESYS_MAX_BLOCKSIZE 2048
extern int hdf_open (struct hardfiledata *hfd, char *name);
extern int hdf_dup (struct hardfiledata *dhfd, struct hardfiledata *shfd);
extern void hdf_close (struct hardfiledata *hfd);
extern int hdf_read (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len);
extern int hdf_write (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len);
extern int hdf_getnumharddrives (void);
extern char *hdf_getnameharddrive (int index, int flags);
extern int hdf_init (void);
extern int isspecialdrive(const char *name);
extern int get_native_path(uae_u32 lock, char *out);