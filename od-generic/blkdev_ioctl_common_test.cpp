#include "sysconfig.h"
#include "sysdeps.h"
#include "blkdev_ioctl_common.h"
#include <vector>
#include <limits.h>
#undef NDEBUG
#include <cassert>
int log_scsi;
void write_log(const TCHAR *, ...) {}
void gui_flicker_led(int, int, int) {}
static bool audio = true, opened = true;
static int starts, stops, reads, fail_sector = -1, eject_value;
int isaudiotrack(cd_toc_head *, int) { return audio; }
void ciw_cdda_stop(cda_play *c)
{
    stops++;
    c->cdda_play = 0;
}
void ciw_cdda_play(void *) {}
int ciw_cdda_setstate(cda_play *c, int state, int)
{
    c->cdda_play_state = state;
    return 0;
}
int uae_start_thread(const TCHAR *, uae_thread_function, void *, uae_thread_id *)
{
    starts++;
    return 1;
}
// Independent bit-address reference for the core's subchannel transform.
void sub_to_deinterleaved(const uae_u8 *src, uae_u8 *dst)
{
    memset(dst, 0, 96);
    for (int channel = 0; channel < 8; channel++)
	for (int bit = 0; bit < 96; bit++)
	    if (src[bit] & (1 << (7 - channel)))
		dst[channel * 12 + bit / 8] |= 1 << (7 - bit % 8);
}
static int open(void *) { return opened; }
static int read(void *, uae_u8 *out, int sector, int count, int size)
{
    reads++;
    assert(count == 1);
    if (sector == fail_sector)
	return size - 1;
    for (int i = 0; i < size; i++)
	out[i] = (uae_u8)(sector * 17 + i);
    return size;
}
static int eject(int, bool value)
{
    eject_value = value ? 1 : 2;
    return 1;
}
int main()
{
    int table[4] = {2, 0, 4, 5};
    assert(host_cd_unit_index(table, 4, 0) == 1);
    assert(host_cd_unit_index(table, 4, 3) == -1);
    assert(host_cd_unit_index(table, 4, -1) == -1);
    assert(host_cd_unit_index(table, 4, 4) == -1);
    assert(host_cd_unit_number(table, 4, 3) == 2);
    cda_play c = {};
    device_info info = {};
    host_cd_backend b = {NULL, 0, open, read};
    assert(host_cd_pause(NULL, 1) == -1 && host_cd_stop(NULL) == 0);
    assert(host_cd_volume(NULL, 1, 2) == (uae_u32)-1);
    assert(host_cd_pause(&c, 1) == 0 && c.cdda_paused == 0);
    c.cdda_play = 1;
    assert(host_cd_pause(&c, 1) == 0 && c.cdda_paused == 1);
    assert(host_cd_pause(&c, 0) == 1 && c.cdda_paused == 0);
    assert(host_cd_volume(&c, 0xffff, 0x8000) == 0);
    assert(host_cd_volume(&c, 1, 2) == 0x8000ffff);
    c.cdda_play = 0;
    opened = false;
    assert(!host_cd_play(&c, &info, b, NULL, 1, 9, 0, NULL, NULL, false));
    assert(starts == 0);
    opened = true;
    audio = false;
    assert(!host_cd_play(&c, &info, b, NULL, 1, 9, 0, NULL, NULL, false));
    audio = true;
    assert(host_cd_play(&c, &info, b, NULL, 1, 9, -1, NULL, NULL, true));
    assert(starts == 1 && c.cdda_scan == 10 && c.cdda_start == 1 && c.cdda_end == 9);
    for (int err = 0; err < 3; err++)
	for (int sub : {0, 1, 2, 4}) {
	    int data = 2352 + (err == 1 ? 294 : err == 2 ? 296 : 0);
	    int stride = data + (sub == 2 ? 12 : sub ? 96 : 0);
	    std::vector<uae_u8> out(stride * 2 + 2, 0xa5);
	    reads = 0;
	    int n = host_cd_rawread(&c, &info, b, out.data() + 1, 10, 2, 0, (err << 9) | sub);
	    assert(n == stride * 2 && reads == 2 && c.cd_last_pos == 11);
	    assert(out[0] == 0xa5 && out.back() == 0xa5);
	    for (int sector = 0; sector < 2; sector++) {
		for (int i = 0; i < data; i++)
		    assert(out[1 + sector * stride + i] == (uae_u8)((10 + sector) * 17 + i));
		if (sub) {
		    uae_u8 raw[96], deint[96];
		    int offset = 2352 + (err ? 296 : 0);
		    for (int i = 0; i < 96; i++)
			raw[i] = (uae_u8)((10 + sector) * 17 + offset + i);
		    sub_to_deinterleaved(raw, deint);
		    assert(!memcmp(out.data() + 1 + sector * stride + data,
				   sub == 1 ? raw : deint + (sub == 2 ? 12 : 0),
				   sub == 2 ? 12 : 96));
		}
	    }
	}
    uae_u8 out[8192];
    reads = 0;
    assert(host_cd_rawread(&c, &info, b, out, 0, 1, 0, 3) == -1 && reads == 0);
    assert(host_cd_rawread(&c, &info, b, out, 0, 1, 0, 3 << 9) == -1);
    assert(host_cd_rawread(&c, &info, b, out, 0, 1, 0, 2 << 16) == -2);
    fail_sector = 11;
    assert(host_cd_rawread(&c, &info, b, out, 10, 2, 2048, 0) == 2048);
    assert(c.cd_last_pos == 10);
    assert(host_cd_rawread(&c, &info, b, out, INT_MAX, 2, 2048, 0) == 0);
    uae_u8 cmd[6] = {0x1b, 0, 0, 0, 2, 0};
    assert(host_cd_scsiemu(0, cmd, eject) == 1 && eject_value == 1);
    cmd[4] = 3;
    assert(host_cd_scsiemu(0, cmd, eject) == 1 && eject_value == 2);
    cmd[0] = 0;
    assert(host_cd_scsiemu(0, cmd, eject) == -1);
    puts("CD controls, read assembly and partial-read checks passed");
}
