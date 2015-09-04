#ifndef UAE_SCSI_H
#define UAE_SCSI_H

#include "uae/types.h"
#include "memory.h"

#define SCSI_DEFAULT_DATA_BUFFER_SIZE (256 * 512)

struct scsi_data_tape
{
	TCHAR tape_dir[MAX_DPATH];
	int file_number;
	uae_s64 file_offset;
	int blocksize;
	bool realdir;
	struct zdirectory *zd;
	struct my_opendir_s *od;
	struct zfile *zf;
	struct zfile *index;
	int beom;
	bool wp;
	bool nomedia;
	bool unloaded;
};

struct scsi_data
{
	int id;
	void *privdata;
	int cmd_len;
	uae_u8 *data;
	int data_len;
	int status;
	uae_u8 sense[256];
	int sense_len;
	uae_u8 reply[256];
	uae_u8 cmd[16];
	uae_u8 msgout[4];
	int reply_len;
	int direction;
	uae_u8 message[1];
	int blocksize;

	int offset;
	uae_u8 *buffer;
	int buffer_size;
	struct hd_hardfiledata *hfd;
	struct scsi_data_tape *tape;
	int device_type;
	int nativescsiunit;
	int cd_emu_unit;
	bool atapi;
	uae_u32 unit_attention;
};

extern struct scsi_data *scsi_alloc_hd(int, struct hd_hardfiledata*);
extern struct scsi_data *scsi_alloc_cd(int, int, bool);
extern struct scsi_data *scsi_alloc_tape(int id, const TCHAR *tape_directory, bool readonly);
extern struct scsi_data *scsi_alloc_native(int, int);
extern void scsi_free(struct scsi_data*);
extern void scsi_reset(void);

extern void scsi_start_transfer(struct scsi_data*);
extern int scsi_send_data(struct scsi_data*, uae_u8);
extern int scsi_receive_data(struct scsi_data*, uae_u8*, bool next);
extern void scsi_emulate_cmd(struct scsi_data *sd);
extern void scsi_illegal_lun(struct scsi_data *sd);
extern void scsi_clear_sense(struct scsi_data *sd);

extern int scsi_hd_emulate(struct hardfiledata *hfd, struct hd_hardfiledata *hdhfd, uae_u8 *cmdbuf, int scsi_cmd_len,
		uae_u8 *scsi_data, int *data_len, uae_u8 *r, int *reply_len, uae_u8 *s, int *sense_len);
extern int scsi_tape_emulate(struct scsi_data_tape *sd, uae_u8 *cmdbuf, int scsi_cmd_len,
		uae_u8 *scsi_data, int *data_len, uae_u8 *r, int *reply_len, uae_u8 *s, int *sense_len);
extern bool scsi_emulate_analyze (struct scsi_data*);

extern bool tape_get_info (int, struct device_info*);
extern struct scsi_data_tape *tape_alloc (int unitnum, const TCHAR *tape_directory, bool readonly);
extern void tape_free (struct scsi_data_tape*);
extern void tape_media_change (int unitnum, struct uaedev_config_info*);

int add_scsi_device(struct scsi_data **sd, int ch, struct uaedev_config_info *ci, struct romconfig *rc);
int add_scsi_hd (struct scsi_data **sd, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci);
int add_scsi_cd (struct scsi_data **sd, int ch, int unitnum);
int add_scsi_tape (struct scsi_data **sd, int ch, const TCHAR *tape_directory, bool readonly);
void free_scsi (struct scsi_data *sd);

void scsi_freenative(struct scsi_data **sd, int max);
void scsi_addnative(struct scsi_data **sd);

#define SCSI_NO_SENSE_DATA		0x00
#define SCSI_NOT_READY			0x04
#define SCSI_NOT_LOADED			0x09
#define SCSI_INSUF_CAPACITY		0x0a
#define SCSI_HARD_DATA_ERROR	0x11
#define SCSI_WRITE_PROTECT		0x17
#define SCSI_CORRECTABLE_ERROR	0x18
#define SCSI_FILE_MARK			0x1c
#define SCSI_INVALID_COMMAND	0x20
#define SCSI_INVALID_FIELD		0x24
#define SCSI_INVALID_LUN		0x25
#define SCSI_UNIT_ATTENTION		0x30
#define SCSI_END_OF_MEDIA		0x34
#define SCSI_MEDIUM_NOT_PRESENT	0x3a

#define SCSI_SK_NO_SENSE        0x0
#define SCSI_SK_REC_ERR         0x1     /* recovered error */
#define SCSI_SK_NOT_READY       0x2
#define SCSI_SK_MED_ERR         0x3     /* medium error */
#define SCSI_SK_HW_ERR          0x4     /* hardware error */
#define SCSI_SK_ILLEGAL_REQ     0x5
#define SCSI_SK_UNIT_ATT        0x6     /* unit attention */
#define SCSI_SK_DATA_PROTECT    0x7
#define SCSI_SK_BLANK_CHECK     0x8
#define SCSI_SK_VENDOR_SPEC     0x9
#define SCSI_SK_COPY_ABORTED    0xA
#define SCSI_SK_ABORTED_CMND    0xB
#define SCSI_SK_VOL_OVERFLOW    0xD
#define SCSI_SK_MISCOMPARE      0xE

#define SCSI_STATUS_GOOD                   0x00
#define SCSI_STATUS_CHECK_CONDITION        0x02
#define SCSI_STATUS_CONDITION_MET          0x04
#define SCSI_STATUS_BUSY                   0x08
#define SCSI_STATUS_INTERMEDIATE           0x10
#define SCSI_STATUS_ICM                    0x14 /* intermediate condition met */
#define SCSI_STATUS_RESERVATION_CONFLICT   0x18
#define SCSI_STATUS_COMMAND_TERMINATED     0x22
#define SCSI_STATUS_QUEUE_FULL             0x28
#define SCSI_STATUS_ACA_ACTIVE             0x30

#define SCSI_MEMORY_FUNCTIONS(x, y, z) \
static void REGPARAM2 x ## _bput(uaecptr addr, uae_u32 b) \
{ \
	y ## _bput(&z, addr, b); \
} \
static void REGPARAM2 x ## _wput(uaecptr addr, uae_u32 b) \
{ \
	y ## _wput(&z, addr, b); \
} \
static void REGPARAM2 x ## _lput(uaecptr addr, uae_u32 b) \
{ \
	y ## _lput(&z, addr, b); \
} \
static uae_u32 REGPARAM2 x ## _bget(uaecptr addr) \
{ \
return y ## _bget(&z, addr); \
} \
static uae_u32 REGPARAM2 x ## _wget(uaecptr addr) \
{ \
return y ## _wget(&z, addr); \
} \
static uae_u32 REGPARAM2 x ## _lget(uaecptr addr) \
{ \
return y ## _lget(&z, addr); \
}

void soft_scsi_put(uaecptr addr, int size, uae_u32 v);
uae_u32 soft_scsi_get(uaecptr addr, int size);

void ncr80_rethink(void);

void apollo_scsi_bput(uaecptr addr, uae_u8 v);
uae_u8 apollo_scsi_bget(uaecptr addr);
void apollo_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

void soft_scsi_free(void);
void soft_scsi_reset(void);

uae_u8 parallel_port_scsi_read(int reg, uae_u8 data, uae_u8 dir);
void parallel_port_scsi_write(int reg, uae_u8 v, uae_u8 dir);
extern bool parallel_port_scsi;

addrbank *supra_init(struct romconfig*);
void supra_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *golem_init(struct romconfig*);
void golem_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *stardrive_init(struct romconfig*);
void stardrive_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *kommos_init(struct romconfig*);
void kommos_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *vector_init(struct romconfig*);
void vector_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *protar_init(struct romconfig *rc);
void protar_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *add500_init(struct romconfig *rc);
void add500_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *kronos_init(struct romconfig *rc);
void kronos_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *adscsi_init(struct romconfig *rc);
void adscsi_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

void rochard_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
bool rochard_scsi_init(struct romconfig *rc, uaecptr baseaddress);
uae_u8 rochard_scsi_get(uaecptr addr);
void rochard_scsi_put(uaecptr addr, uae_u8 v);

addrbank *cltda1000scsi_init(struct romconfig *rc);
void cltda1000scsi_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *ptnexus_init(struct romconfig *rc);
void ptnexus_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *dataflyer_init(struct romconfig *rc);
void dataflyer_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *tecmar_init(struct romconfig *rc);
void tecmar_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *xebec_init(struct romconfig *rc);
void xebec_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *microforge_init(struct romconfig *rc);
void microforge_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *paradox_init(struct romconfig *rc);
void paradox_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *hda506_init(struct romconfig *rc);
void hda506_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *alf1_init(struct romconfig *rc);
void alf1_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *promigos_init(struct romconfig *rc);
void promigos_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *system2000_init(struct romconfig *rc);
void system2000_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

addrbank *omtiadapter_init(struct romconfig *rc);
void omtiadapter_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

void x86_xt_hd_bput(int, uae_u8);
uae_u8 x86_xt_hd_bget(int);
addrbank *x86_xt_hd_init(struct romconfig *rc);
void x86_add_xt_hd_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

#endif /* UAE_SCSI_H */
