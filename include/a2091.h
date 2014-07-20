
#ifndef A2091_H
#define A2091_H

#ifdef A2091

#define WD_STATUS_QUEUE 2

struct wd_state {
	bool enabled;
	const TCHAR *name;
	int configured;
	bool autoconfig;
	uae_u8 dmacmemory[100];
	uae_u8 *rom;
	int rombankswitcher, rombank;
	int rom_size, rom_mask;

	uae_u32 dmac_istr, dmac_cntr;
	uae_u32 dmac_dawr;
	uae_u32 dmac_acr;
	uae_u32 dmac_wtc;
	int dmac_dma;
	volatile uae_u8 sasr, scmd, auxstatus;
	volatile int wd_used;
	volatile int wd_phase, wd_next_phase, wd_busy, wd_data_avail;
	volatile bool wd_selected;
	volatile int wd_dataoffset;
	volatile uae_u8 wd_data[32];

	volatile int scsidelay_irq[WD_STATUS_QUEUE];
	volatile uae_u8 scsidelay_status[WD_STATUS_QUEUE];
	volatile int queue_index;
	smp_comm_pipe requests;
	volatile int scsi_thread_running;

	int old_dmac;
	int superdmac;
	int wd33c93_ver;// 0 or 1

	uae_u8 xt_control;
	uae_u8 xt_status;
	uae_u16 xt_cyls, xt_heads, xt_sectors;

	bool xt_irq;
	int xt_offset;
	int xt_datalen;
	uae_u8 xt_cmd[6];
	uae_u8 xt_statusbyte;

	// unit 7 = XT
	struct scsi_data *scsis[8];
	struct scsi_data *scsi;

	uae_u8 wdregs[32];

	bool cdtv;
};
extern wd_state wd_cdtv;

extern void init_scsi (struct wd_state*);
extern void scsi_dmac_start_dma (struct wd_state*);
extern void scsi_dmac_stop_dma (struct wd_state*);

extern addrbank *a2091_init (int devnum);
extern void a2091_free (void);
extern void a2091_reset (void);

extern void a3000scsi_init (void);
extern void a3000scsi_free (void);
extern void a3000scsi_reset (void);
extern void rethink_a2091 (void);

extern void wdscsi_put (struct wd_state*, uae_u8);
extern uae_u8 wdscsi_get (struct wd_state*);
extern uae_u8 wdscsi_getauxstatus (struct wd_state*);
extern void wdscsi_sasr (struct wd_state*, uae_u8);

extern void scsi_hsync (void);

#define WDTYPE_A2091 0
#define WDTYPE_A2091_2 1
#define WDTYPE_A3000 2
#define WDTYPE_CDTV 3

#define WD33C93 _T("WD33C93")

extern int a2091_add_scsi_unit(int ch, struct uaedev_config_info *ci, int devnum);
extern int a3000_add_scsi_unit(int ch, struct uaedev_config_info *ci);

extern int add_wd_scsi_hd (struct wd_state *wd, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level);
extern int add_wd_scsi_cd (struct wd_state *wd, int ch, int unitnum);
extern int add_wd_scsi_tape (struct wd_state *wd, int ch, const TCHAR *tape_directory, bool readonly);

#endif

#endif /* A2091H */
