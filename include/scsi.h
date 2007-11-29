
#define SCSI_DATA_BUFFER_SIZE (512 * 512)
struct scsi_data
{
    int id;
    int len;
    uae_u8 *data;
    int data_len;
    int status;
    uae_u8 sense[256];
    int sense_len;
    uae_u8 reply[256];
    uae_u8 cmd[16];
    int reply_len;
    int direction;

    int offset;
    uae_u8 buffer[SCSI_DATA_BUFFER_SIZE];
    struct hd_hardfiledata *hfd;
    int nativescsiunit;
};

extern struct scsi_data *scsi_alloc(int,struct hd_hardfiledata*);
extern struct scsi_data *scsi_alloc_native(int,int);
extern void scsi_free(struct scsi_data*);
extern void scsi_reset(void);

extern void scsi_start_transfer(struct scsi_data*,int);
extern int scsi_send_data(struct scsi_data*, uae_u8);
extern int scsi_receive_data(struct scsi_data*, uae_u8*);
extern void scsi_emulate_cmd(struct scsi_data *sd);
extern int scsi_data_dir(struct scsi_data *sd);


extern int scsi_emulate(struct hardfiledata *hfd, struct hd_hardfiledata *hdhfd, uae_u8 *cmdbuf, int scsi_cmd_len,
		uae_u8 *scsi_data, int *data_len, uae_u8 *r, int *reply_len, uae_u8 *s, int *sense_len);


