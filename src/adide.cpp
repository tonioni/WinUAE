 /*
  * UAE - The Un*x Amiga Emulator
  *
  * ADIDE
  *
  * (c) 2009 Toni Wilen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "gayle.h"

static struct ide_hdf *idedrive[2];

int adide_add_ide_unit (int ch, TCHAR *path, int blocksize, int readonly,
		       TCHAR *devname, int sectors, int surfaces, int reserved,
		       int bootpri, TCHAR *filesys)
{
    struct ide_hdf *ide;

    if (ch >= 2)
	return -1;
    alloc_ide_mem ();
    ide = idedrive[ch];
    if (!hdf_hd_open (&ide->hdhfd, path, blocksize, readonly, devname, sectors, surfaces, reserved, bootpri, filesys))
	return -1;
    ide->lba48 = ide->hdhfd.size >= 128 * (uae_u64)0x40000000 ? 1 : 0;
    write_log (_T("IDE%d '%s', CHS=%d,%d,%d. %uM. LBA48=%d\n"),
	ch, path, ide->hdhfd.cyls, ide->hdhfd.heads, ide->hdhfd.secspertrack, (int)(ide->hdhfd.size / (1024 * 1024)), ide->lba48);
    ide->status = 0;
    ide->data_offset = 0;
    ide->data_size = 0;
    //dumphdf (&ide->hdhfd.hfd);
    return 1;
}
