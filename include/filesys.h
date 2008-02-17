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
    int warned;
    uae_u8 *virtual_rdb;
    uae_u64 virtual_size;
    int unitnum;

    int drive_empty;
    char *emptyname;
};

#define HFD_FLAGS_REALDRIVE 1

struct hd_hardfiledata {
    struct hardfiledata hfd;
    int bootpri;
    uae_u64 size;
    unsigned int cyls;
    unsigned int heads;
    unsigned int secspertrack;
    unsigned int cyls_def;
    unsigned int secspertrack_def;
    unsigned int heads_def;
    char *path;
    int ansi_version;
};

#define HD_CONTROLLER_UAE 0
#define HD_CONTROLLER_IDE0 1
#define HD_CONTROLLER_IDE1 2
#define HD_CONTROLLER_IDE2 3
#define HD_CONTROLLER_IDE3 4
#define HD_CONTROLLER_SCSI0 5
#define HD_CONTROLLER_SCSI1 6
#define HD_CONTROLLER_SCSI2 7
#define HD_CONTROLLER_SCSI3 8
#define HD_CONTROLLER_SCSI4 9
#define HD_CONTROLLER_SCSI5 10
#define HD_CONTROLLER_SCSI6 11
#define HD_CONTROLLER_PCMCIA_SRAM 12
#define HD_CONTROLLER_PCMCIA_IDE 13

#define FILESYS_VIRTUAL 0
#define FILESYS_HARDFILE 1
#define FILESYS_HARDFILE_RDB 2
#define FILESYS_HARDDRIVE 3

#define MAX_FILESYSTEM_UNITS 30

struct uaedev_mount_info;
extern struct uaedev_mount_info options_mountinfo;

extern struct hardfiledata *get_hardfile_data (int nr);
#define FILESYS_MAX_BLOCKSIZE 2048
extern int hdf_open (struct hardfiledata *hfd, const char *name);
extern int hdf_dup (struct hardfiledata *dhfd, const struct hardfiledata *shfd);
extern void hdf_close (struct hardfiledata *hfd);
extern int hdf_read (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len);
extern int hdf_write (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len);
extern int hdf_getnumharddrives (void);
extern char *hdf_getnameharddrive (int index, int flags, int *sectorsize);
extern int hdf_init (void);
extern int isspecialdrive(const char *name);
extern int get_native_path(uae_u32 lock, char *out);
extern void hardfile_do_disk_change (struct uaedev_config_info *uci, int insert);

void hdf_hd_close(struct hd_hardfiledata *hfd);
int hdf_hd_open(struct hd_hardfiledata *hfd, const char *path, int blocksize, int readonly,
		       const char *devname, int sectors, int surfaces, int reserved,
		       int bootpri, const char *filesys);
