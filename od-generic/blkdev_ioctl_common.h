#ifndef UAE_BLKDEV_IOCTL_COMMON_H
#define UAE_BLKDEV_IOCTL_COMMON_H
#include "threaddep/thread.h"
struct TrapContext;
#include "cda_play.h"
struct host_cd_backend {
    void *context;
    int unitnum;
    int (*open)(void *context);
    int (*read)(void *context, uae_u8 *data, int sector, int count, int sectorsize);
};
int host_cd_unit_index(const int *table, int count, int unit);
int host_cd_unit_number(const int *table, int count, int index);
int host_cd_pause(cda_play *cda, int paused);
int host_cd_stop(cda_play *cda);
uae_u32 host_cd_volume(cda_play *cda, uae_u16 left, uae_u16 right);
int host_cd_play(cda_play *cda, device_info *info, const host_cd_backend &backend,
		 cda_play_read_block read, int start, int end, int scan,
		 play_status_callback status, play_subchannel_callback sub, bool clear_subcode);
int host_cd_copy_audio(uae_u8 *dest, const uae_u8 *source, int errorfield, int subcode);
int host_cd_rawread(cda_play *cda, device_info *info, const host_cd_backend &backend, uae_u8 *data,
		    int sector, int count, int sectorsize, uae_u32 extra);
void host_cd_init_info(device_info *info, int unitnum);
void host_cd_finish_info(device_info *info, int unitnum, bool cdrom, const TCHAR *label);
int host_cd_scsiemu(int unitnum, const uae_u8 *command, int (*eject)(int, bool));
#endif
