#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "threaddep/thread.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "disk.h"
#include "autoconf.h"
#include "filesys.h"
#include "execlib.h"
#include "scsidev.h"

uaecptr scsidev_startup (uaecptr resaddr) { return resaddr; }
void scsidev_install (void) {}
void scsidev_reset (void) {}
void scsidev_start_threads (void) {}

