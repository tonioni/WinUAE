
/* IDE drive registers */
#define IDE_DATA	0x00
#define IDE_ERROR	0x01	    /* see err-bits */
#define IDE_NSECTOR	0x02	    /* sector count, nr of sectors to read/write */
#define IDE_SECTOR	0x03	    /* starting sector */
#define IDE_LCYL	0x04	    /* starting cylinder */
#define IDE_HCYL	0x05	    /* high byte of starting cyl */
#define IDE_SELECT	0x06	    /* 101dhhhh , d=drive, hhhh=head */
#define IDE_STATUS	0x07	    /* see status-bits */

#define IDE_SECONDARY 0x0400
#define IDE_DEVCON	0x0406
#define IDE_DRVADDR	0x0407

struct ide_registers
{
	uae_u8 ide_select, ide_nsector, ide_sector, ide_lcyl, ide_hcyl, ide_devcon, ide_error, ide_feat;
	uae_u8 ide_nsector2, ide_sector2, ide_lcyl2, ide_hcyl2, ide_feat2;
	uae_u8 ide_status;
};

struct ide_thread_state;
struct ide_hdf;

struct ide_board
{
	uae_u8 *rom;
	uae_u8 acmemory[128];
	int rom_size;
	int rom_mask;
	int configured;
	addrbank *bank;
	struct ide_hdf *ide;
	bool irq;
	bool intena;
};

struct ide_hdf
{
	struct hd_hardfiledata hdhfd;
	struct ide_board *board;
	struct ide_registers regs;
	struct ide_registers *regs0;
	struct ide_registers *regs1;
	struct ide_hdf *pair; // master<>slave
	struct ide_thread_state *its;
	bool byteswap;

	uae_u8 *secbuf;
	int secbuf_size;
	int data_offset;
	int data_size;
	int data_multi;
	int direction; // 0 = read, 1 = write
	bool intdrq;
	bool lba48;
	bool lba48cmd;
	uae_u8 multiple_mode;
	int irq_delay;
	int irq;
	int num;
	int blocksize;
	int maxtransferstate;
	int ide_drv;

	bool atapi;
	bool atapi_drdy;
	int cd_unit_num;
	int packet_state;
	int packet_data_size;
	int packet_data_offset;
	int packet_transfer_size;
	struct scsi_data *scsi;
};

struct ide_thread_state
{
	struct ide_hdf **idetable;
	int idetotal;
	volatile int state;
	smp_comm_pipe requests;	
};

uae_u32 ide_read_reg (struct ide_hdf *ide, int ide_reg);
void ide_write_reg (struct ide_hdf *ide, int ide_reg, uae_u32 val);
void ide_put_data (struct ide_hdf *ide, uae_u16 v);
uae_u16 ide_get_data (struct ide_hdf *ide);

bool ide_interrupt_check(struct ide_hdf **ide, int num);
bool ide_isdrive(struct ide_hdf *ide);
void ide_initialize(struct ide_hdf **idetable, int chpair);
struct ide_hdf *add_ide_unit (struct ide_hdf **idetable, int max, int ch, struct uaedev_config_info *ci);
void remove_ide_unit(struct ide_hdf **idetable, int ch);
void alloc_ide_mem (struct ide_hdf **ide, int max, struct ide_thread_state *its);

void start_ide_thread(struct ide_thread_state *its);
void stop_ide_thread(struct ide_thread_state *its);

uae_u8 *ide_save_state(uae_u8 *dst, struct ide_hdf *ide);
uae_u8 *ide_restore_state(uae_u8 *src, struct ide_hdf *ide);

