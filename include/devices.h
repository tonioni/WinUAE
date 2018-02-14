#ifndef UAE_DEVICES_H
#define UAE_DEVICES_H

void devices_reset(int hardreset);
void devices_vsync_pre(void);
void devices_vsync_post(void);
void devices_hsync(void);
void devices_rethink(void);
void devices_rethink_all(void func(void));
void devices_syncchange(void);
void devices_update_sound(double clk, double syncadjust);
void devices_update_sync(double svpos, double syncadjust);
void reset_all_systems(void);
void do_leave_program(void);
void virtualdevice_init(void);
void devices_restore_start(void);
void device_check_config(void);
void devices_pause(void);
void devices_unpause(void);

#define IRQ_SOURCE_PCI 0
#define IRQ_SOURCE_SOUND 1
#define IRQ_SOURCE_NE2000 2
#define IRQ_SOURCE_A2065 3
#define IRQ_SOURCE_NCR 4
#define IRQ_SOURCE_NCR9X 5
#define IRQ_SOURCE_CPUBOARD 6
#define IRQ_SOURCE_UAE 7
#define IRQ_SOURCE_SCSI 8
#define IRQ_SOURCE_WD 9
#define IRQ_SOURCE_X86 10
#define IRQ_SOURCE_GAYLE 11
#define IRQ_SOURCE_CIA 12
#define IRQ_SOURCE_CD32CDTV 13
#define IRQ_SOURCE_IDE 14
#define IRQ_SOURCE_MAX 15


#endif /* UAE_DEVICES_H */
