
struct zfile {
    TCHAR *name;
    TCHAR *zipname;
    TCHAR *mode;
    FILE *f;
    uae_u8 *data;
    uae_s64 size;
    uae_s64 seek;
    int deleteafterclose;
    int textmode;
    struct zfile *next;
    int zfdmask;
    struct zfile *parent;
    uae_u64 offset;
    int opencnt;
};

#define ZNODE_FILE 0
#define ZNODE_DIR 1
#define ZNODE_VDIR -1
struct znode {
    int type;
    struct znode *sibling;
    struct znode *child;
    struct zvolume *vchild;
    struct znode *parent;
    struct zvolume *volume;
    struct znode *next;
    struct znode *prev;
    struct znode *vfile; // points to real file when this node is virtual directory
    TCHAR *name;
    TCHAR *fullname;
    uae_s64 size;
    struct zfile *f;
    TCHAR *comment;
    int flags;
    time_t mtime;
    /* decompressor specific */
    unsigned int offset;
    unsigned int offset2;
    unsigned int method;
    unsigned int packedsize;
};

struct zvolume
{
    struct zfile *archive;
    void *handle;
    struct znode root;
    struct zvolume *next;
    struct znode *last;
    struct znode *parentz;
    struct zvolume *parent;
    uae_s64 size;
    unsigned int blocks;
    unsigned int id;
    uae_s64 archivesize;
    unsigned int method;
    TCHAR *volumename;
    int zfdmask;
};

struct zarchive_info
{
    const TCHAR *name;
    uae_s64 size;
    int flags;
    TCHAR *comment;
    time_t t;
};

#define ArchiveFormat7Zip '7z  '
#define ArchiveFormatRAR 'rar '
#define ArchiveFormatZIP 'zip '
#define ArchiveFormatLHA 'lha '
#define ArchiveFormatLZX 'lzx '
#define ArchiveFormatPLAIN '----'
#define ArchiveFormatDIR 'DIR '
#define ArchiveFormatAA 'aa  ' // method only
#define ArchiveFormatADF 'DOS '
#define ArchiveFormatRDB 'RDSK'
#define ArchiveFormatMBR 'MBR '
#define ArchiveFormatFAT 'FAT '

extern int zfile_is_ignore_ext(const TCHAR *name);

extern struct zvolume *zvolume_alloc(struct zfile *z, unsigned int id, void *handle, const TCHAR*);
extern struct zvolume *zvolume_alloc_empty(struct zvolume *zv, const TCHAR *name);

extern struct znode *zvolume_addfile_abs(struct zvolume *zv, struct zarchive_info*);
extern struct znode *zvolume_adddir_abs(struct zvolume *zv, struct zarchive_info *zai);
extern struct znode *znode_adddir(struct znode *parent, const TCHAR *name, struct zarchive_info*);

extern struct zvolume *archive_directory_plain (struct zfile *zf);
extern struct zfile *archive_access_plain (struct znode *zn);
extern struct zvolume *archive_directory_lha(struct zfile *zf);
extern struct zfile *archive_access_lha (struct znode *zn);
extern struct zvolume *archive_directory_zip(struct zfile *zf);
extern struct zfile *archive_access_zip (struct znode *zn);
extern struct zvolume *archive_directory_7z (struct zfile *z);
extern struct zfile *archive_access_7z (struct znode *zn);
extern struct zvolume *archive_directory_rar (struct zfile *z);
extern struct zfile *archive_access_rar (struct znode *zn);
extern struct zvolume *archive_directory_lzx (struct zfile *in_file);
extern struct zfile *archive_access_lzx (struct znode *zn);
extern struct zvolume *archive_directory_arcacc (struct zfile *z, unsigned int id);
extern struct zfile *archive_access_arcacc (struct znode *zn);
extern struct zvolume *archive_directory_adf (struct znode *zn, struct zfile *z);
extern struct zfile *archive_access_adf (struct znode *zn);
extern struct zvolume *archive_directory_rdb (struct zfile *z);
extern struct zfile *archive_access_rdb (struct znode *zn);
extern struct zvolume *archive_directory_fat (struct zfile *z);
extern struct zfile *archive_access_fat (struct znode *zn);
extern struct zfile *archive_access_dir (struct znode *zn);

extern struct zfile *archive_access_select (struct znode *parent, struct zfile *zf, unsigned int id, int doselect, int *retcode);
extern struct zfile *archive_access_arcacc_select (struct zfile *zf, unsigned int id, int *retcode);
extern int isfat (uae_u8*);

extern void archive_access_scan (struct zfile *zf, zfile_callback zc, void *user, unsigned int id);

extern void archive_access_close (void *handle, unsigned int id);

extern struct zfile *archive_getzfile(struct znode *zn, unsigned int id);

extern struct zfile *decompress_zfd (struct zfile*);