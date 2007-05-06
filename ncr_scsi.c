 /*
  * UAE - The Un*x Amiga Emulator
  *
  * A4000T NCR 53C710 SCSI (nothing done yet)
  *
  * (c) 2007 Toni Wilen
  */

#define NCR_LOG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "ncr_scsi.h"

#define NCR "NCR53C710"
#define NCR_REGS 0x40

static uae_u8 ncrregs[NCR_REGS];

void ncr_bput(uaecptr addr, uae_u32 val)
{
    addr &= 0xffff;
    if (addr >= NCR_REGS)
	return;
    write_log("%s write %04.4X = %02.2X\n", NCR, addr, val & 0xff);
    ncrregs[addr] = val;
}

uae_u32 ncr_bget(uaecptr addr)
{
    uae_u32 v;

    addr &= 0xffff;
    if (addr >= NCR_REGS)
	return 0;
    v = ncrregs[addr];
    write_log("%s read %04.4X\n", NCR, addr);
    return v;
}
