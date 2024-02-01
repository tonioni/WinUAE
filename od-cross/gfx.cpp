#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"
#include "uae/slirp.h"

// needed by custom.cpp
int vsync_activeheight, vsync_totalheight;
int max_uae_width = 8192, max_uae_height = 8192;
int saveimageoriginalpath;
float vsync_vblank, vsync_hblank;
uae_u16 serper, serdat, serdatr;

// related to GSYNC/VSYNC
int target_get_display_scanline2(int displayindex) {
    UNIMPLEMENTED();
    return 0;
}

// related to GSYNC/VSYNC
int target_get_display_scanline(int displayindex) {
    UNIMPLEMENTED();
    return 0;
}

void target_spin(int cycles) { }

void gui_message(const char *format, ...) {
	va_list parms;

	va_start (parms, format);
	vprintf (format, parms);
	va_end (parms);
}

int lockscr() {
    TRACE();
    return 1;
}

void unlockscr() {
    UNIMPLEMENTED();
}

int lockscr(struct vidbuffer*, bool, bool, bool) {
    TRACE();
    return 1;
}

