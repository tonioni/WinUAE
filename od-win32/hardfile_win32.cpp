#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x600

#include "sysconfig.h"
#include "sysdeps.h"

#include <shellapi.h>

#include "resource.h"

#include "threaddep/thread.h"
#include "options.h"
#include "filesys.h"
#include "blkdev.h"
#include "registry.h"
#include "win32gui.h"
#include "zfile.h"
#include "ini.h"
#include "memory.h"
#include "autoconf.h"
#include "rommgr.h"
#include "fsdb.h"

#ifdef RETROPLATFORM
#include "rp.h"
#endif

#define hfd_log write_log
#define hfd_log2
//#define hdf_log2 write_log

#ifdef WINDDK
#include <devioctl.h>
#include <ntddstor.h>
#include <winioctl.h>
#include <initguid.h>   // Guid definition
#include <devguid.h>    // Device guids
#include <setupapi.h>   // for SetupDiXxx functions.
#include <cfgmgr32.h>   // for SetupDiXxx functions.
#include <Ntddscsi.h>
#endif
#include <stddef.h>

static int usefloppydrives = 0;
static int num_drives;
static bool drives_enumerated;

#define MAX_LOCKED_VOLUMES 8

const static GUID PARTITION_GPT_AMIGA = { 0xbcee823f, 0xc987, 0x9740, { 0x81,0x65,0x89,0xd6,0x54,0x05,0x57,0xc0 } };

struct hardfilehandle
{
	int zfile;
	struct zfile *zf;
	HANDLE h;
	BOOL firstwrite;
	HANDLE locked_volumes[MAX_LOCKED_VOLUMES];
	bool dismounted;
};

struct uae_driveinfo {
	TCHAR vendor_id[128];
	TCHAR product_id[128];
	TCHAR product_rev[128];
	TCHAR product_serial[128];
	TCHAR device_name[1024];
	TCHAR device_path[1024];
	TCHAR device_full_path[2048];
	uae_u8 identity[512];
	uae_u64 size;
	uae_u64 offset;
	int bytespersector;
	int removablemedia;
	int nomedia;
	int dangerous;
	bool partitiondrive;
	int readonly;
	int cylinders, sectors, heads;
	int BusType;
	uae_u16 usb_vid, usb_pid;
	int devicetype;
	bool scsi_direct_fail;
	bool chsdetected;

};

#define HDF_HANDLE_WIN32_NORMAL 1
#define HDF_HANDLE_WIN32_CHS 2
#define HDF_HANDLE_ZFILE 3
#define HDF_HANDLE_UNKNOWN 4

#define CACHE_SIZE 16384
#define CACHE_FLUSH_TIME 5

/* safety check: only accept drives that:
* - contain RDSK in block 0
* - block 0 is zeroed
*/

int harddrive_dangerous; // = 0x1234dead; // test only!
int do_rdbdump;
static struct uae_driveinfo uae_drives[MAX_FILESYSTEM_UNITS];

static bool guidfromstring(const WCHAR *guid, GUID *out)
{
	typedef BOOL(WINAPI *LPFN_GUIDFromString)(LPCTSTR, LPGUID);
	LPFN_GUIDFromString pGUIDFromString = NULL;
	bool ret = false;

	HINSTANCE hInst = LoadLibrary(TEXT("shell32.dll"));
	if (hInst)
	{
		pGUIDFromString = (LPFN_GUIDFromString)GetProcAddress(hInst, MAKEINTRESOURCEA(704));
		if (pGUIDFromString)
			ret = pGUIDFromString(guid, out);
		FreeLibrary(hInst);
	}
	return ret;
}


#if 0
static void fixdrive (struct hardfiledata *hfd)
{
	uae_u8 data[512];
	int i = 0;
	struct zfile *zf = zfile_fopen (_T("d:\\amiga\\hdf\\test_16MB_hdf.bin"), _T("rb"));
	while (zfile_fread (data, 1, 512, zf)) {
		hdf_write (hfd, data, i, 512);
		i += 512;
	}
	zfile_fclose (zf);

}
#endif

static BOOL HD_ReadFile(HANDLE h, LPVOID buffer, DWORD len, DWORD *outlen, uae_u64 offset)
{
	BOOL v = ReadFile(h, buffer, len, outlen, NULL);
	if (!v) {
		write_log("HD_READ(%p,%llx,%x) ERR %d\n", h, offset, len, GetLastError());
	}
	return v;
}
static BOOL HD_WriteFile(HANDLE h, LPVOID buffer, DWORD len, DWORD *outlen, uae_u64 offset)
{
	BOOL v = WriteFile(h, buffer, len, outlen, NULL);
	if (!v) {
		write_log("HD_WRITE(%p,%llx,%x) ERR %d\n", h, offset, len, GetLastError());
	}
	return v;
}

static int isnomediaerr (DWORD err)
{
	if (err == ERROR_NOT_READY ||
		err == ERROR_MEDIA_CHANGED ||
		err == ERROR_NO_MEDIA_IN_DRIVE ||
		err == ERROR_DEV_NOT_EXIST ||
		err == ERROR_BAD_NET_NAME ||
		err == ERROR_WRONG_DISK)
		return 1;
	return 0;
}

static void rdbdump (HANDLE h, uae_u64 offset, uae_u8 *buf, int blocksize)
{
	static int cnt = 1;
	int i, blocks;
	TCHAR name[100];
	FILE *f;
	bool needfree = false;

	write_log (_T("creating rdb dump.. offset=%I64X blocksize=%d\n"), offset, blocksize);
	if (buf) {
		blocks = (buf[132] << 24) | (buf[133] << 16) | (buf[134] << 8) | (buf[135] << 0);
		if (blocks < 0 || blocks > 100000)
			return;
	} else {
		blocks = 16383;
		buf = (uae_u8*)VirtualAlloc (NULL, CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
		needfree = true;
	}
	_stprintf (name, _T("rdb_dump_%d.rdb"), cnt);
	f = _tfopen (name, _T("wb"));
	if (!f) {
		write_log (_T("failed to create file '%s'\n"), name);
		return;
	}
	for (i = 0; i <= blocks; i++) {
		DWORD outlen;
		LARGE_INTEGER fppos;
		fppos.QuadPart = offset;
		if (SetFilePointer (h, fppos.LowPart, &fppos.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
			break;
		HD_ReadFile(h, buf, blocksize, &outlen, offset);
		fwrite (buf, 1, blocksize, f);
		offset += blocksize;
	}
	fclose (f);
	if (needfree)
		VirtualFree (buf, 0, MEM_RELEASE);
	write_log (_T("'%s' saved\n"), name);
	cnt++;
}

static int getsignfromhandle (HANDLE h, DWORD *sign, DWORD *pstyle)
{
	int ok;
	DWORD written, outsize;
	DRIVE_LAYOUT_INFORMATION_EX *dli;

	ok = 0; 
	outsize = sizeof (DRIVE_LAYOUT_INFORMATION_EX) + sizeof (PARTITION_INFORMATION_EX) * 32;
	dli = (DRIVE_LAYOUT_INFORMATION_EX*)xmalloc(uae_u8, outsize);
	if (DeviceIoControl (h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, dli, outsize, &written, NULL)) {
		if (dli->PartitionStyle == PARTITION_STYLE_MBR) {
			*sign = dli->Mbr.Signature;
		}
		*pstyle = dli->PartitionStyle;
		ok = 1;
	} else {
		hfd_log(_T("IOCTL_DISK_GET_DRIVE_LAYOUT_EX() returned %08x\n"), GetLastError());
	}
	hfd_log2(_T("getsignfromhandle(signature=%08X,pstyle=%d)\n"), *sign, *pstyle);
	xfree (dli);
	return ok;
}

static int ismounted(const TCHAR *name, HANDLE hd, int *typep)
{
	HANDLE h;
	TCHAR volname[MAX_DPATH];
	int mounted, ret;
	DWORD sign, pstyle;

	hfd_log2(_T("\n"));
	hfd_log2(_T("Name='%s'\n"), name);
	*typep = -1;
	ret = getsignfromhandle(hd, &sign, &pstyle);
	if (!ret) {
		return 0;
	}
	*typep = pstyle;
	if (pstyle == PARTITION_STYLE_GPT) {
		return 2;
	}
	if (pstyle == PARTITION_STYLE_RAW) {
		return 0;
	}
	mounted = 0;
	h = FindFirstVolume (volname, sizeof volname / sizeof (TCHAR));
	while (h != INVALID_HANDLE_VALUE && !mounted) {
		HANDLE d;
		if (volname[_tcslen (volname) - 1] == '\\')
			volname[_tcslen (volname) - 1] = 0;
		d = CreateFile (volname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		hfd_log2(_T("volname='%s' %08x\n"), volname, d);
		if (d != INVALID_HANDLE_VALUE) {
			DWORD isntfs, outsize, written;
			isntfs = 0;
			if (DeviceIoControl (d, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &written, NULL)) {
				VOLUME_DISK_EXTENTS *vde;
				NTFS_VOLUME_DATA_BUFFER ntfs;
				hfd_log(_T("FSCTL_IS_VOLUME_MOUNTED(%s) returned is mounted\n"), volname);
				if (DeviceIoControl (d, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfs, sizeof ntfs, &written, NULL)) {
					isntfs = 1;
				}
				hfd_log(_T("FSCTL_GET_NTFS_VOLUME_DATA returned %d\n"), isntfs);
				const int maxextends = 30;
				outsize = sizeof (VOLUME_DISK_EXTENTS) + sizeof (DISK_EXTENT) * (maxextends + 1);
				vde = (VOLUME_DISK_EXTENTS*)xcalloc(uae_u8, outsize);
				if (vde && DeviceIoControl(d, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
					hfd_log(_T("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned %d extents\n"), vde->NumberOfDiskExtents);
					for (int i = 0; i < vde->NumberOfDiskExtents && i < maxextends; i++) {
						TCHAR pdrv[MAX_DPATH];
						HANDLE ph;
						_stprintf (pdrv, _T("\\\\.\\PhysicalDrive%d"), vde->Extents[i].DiskNumber);
						ph = CreateFile (pdrv, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
							NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						hfd_log(_T("PhysicalDrive%d: Extent %d Start=%I64X Len=%I64X\n"),
							vde->Extents[i].DiskNumber, i, vde->Extents[i].StartingOffset.QuadPart, vde->Extents[i].ExtentLength.QuadPart);
						if (ph != INVALID_HANDLE_VALUE) {
							DWORD sign2;
							if (getsignfromhandle (ph, &sign2, &pstyle)) {
								*typep = pstyle;
								if (sign == sign2 && pstyle == PARTITION_STYLE_MBR)
									mounted = isntfs ? -1 : 1;
							}
							CloseHandle (ph);
						}
					}
				} else {
					hfd_log(_T("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned %08x\n"), GetLastError());
				}
			} else {
				hfd_log(_T("FSCTL_IS_VOLUME_MOUNTED returned not mounted\n"));
			}
			CloseHandle (d);
		} else {
			hfd_log(_T("'%s': %08x\n"), volname, GetLastError());
		}
		if (!FindNextVolume (h, volname, sizeof volname / sizeof (TCHAR)))
			break;
	}
	FindVolumeClose (h);
	hfd_log2(_T("\n"));
	return mounted;
}

static void tochs(uae_u8 *data, uae_s64 offset, int *cp, int *hp, int *sp)
{
	int c, h, s;
	c = (data[1 * 2 + 0] << 8) | (data[1 * 2 + 1] << 0);
	h = (data[3 * 2 + 0] << 8) | (data[3 * 2 + 1] << 0);
	s = (data[6 * 2 + 0] << 8) | (data[6 * 2 + 1] << 0);
	if (offset >= 0) {
		offset /= 512;
		c = (int)(offset / (h * s));
		offset -= c * h * s;
		h = (int)(offset / s);
		offset -= h * s;
		s = (int)offset + 1;
	}
	*cp = c;
	*hp = h;
	*sp = s;
}

static bool ischs(uae_u8 *identity)
{
	if (!identity[0] && !identity[1])
		return false;
	uae_u8 *d = identity;
	// C/H/S = zeros?
	if ((!d[2] && !d[3]) || (!d[6] && !d[7]) || (!d[12] && !d[13]))
		return false;
	// LBA = zero?
	if (d[60 * 2 + 0] || d[60 * 2 + 1] || d[61 * 2 + 0] || d[61 * 2 + 1])
		return false;
	uae_u16 v = (d[49 * 2 + 0] << 8) | (d[49 * 2 + 1] << 0);
	if (!(v & (1 << 9))) { // LBA not supported?
		return true;
	}
	return false;
}

#define CA "Commodore\0Amiga\0"
static bool do_scsi_read10_chs(HANDLE handle, uae_u32 lba, int c, int h, int s, uae_u8 *data, int cnt, int *flags, bool log);
static int safetycheck (HANDLE h, const TCHAR *name, uae_u64 offset, uae_u8 *buf, int blocksize, uae_u8 *identity, bool canchs, bool *accepted)
{
	uae_u64 origoffset = offset;
	int i, j, blocks = 63, empty = 1;
	DWORD outlen;
	int specialaccessmode = 0;

	if (accepted) {
		*accepted = false;
	}
	for (j = 0; j < blocks; j++) {
		memset(buf, 0xaa, blocksize);

		if (ischs(identity) && canchs) {
			int cc, hh, ss;
			tochs(identity, j * 512, &cc, &hh, &ss);
			if (!do_scsi_read10_chs(h, -1, cc, hh, ss, buf, 1, &specialaccessmode, false)) {
				write_log(_T("hd ignored, do_scsi_read10_chs failed\n"));
				return 1;
			}

		} else {

			LARGE_INTEGER fppos;
			fppos.QuadPart = offset;
			if (SetFilePointer(h, fppos.LowPart, &fppos.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
				write_log(_T("hd ignored, SetFilePointer failed, error %d\n"), GetLastError());
				return 1;
			}
			HD_ReadFile(h, buf, blocksize, &outlen, offset);
			if (outlen != blocksize) {
				write_log(_T("hd ignored (out=%d bs=%d), read error %d!\n"), outlen, blocksize, GetLastError());
				return 2;
			}
		}

		if (j == 0 && offset > 0)
			return -5;
		if (j == 0 && buf[0] == 0x39 && buf[1] == 0x10 && buf[2] == 0xd3 && buf[3] == 0x12) {
			// ADIDE "CPRM" hidden block..
			if (do_rdbdump)
				rdbdump(h, offset, buf, blocksize);
			write_log(_T("hd accepted (adide rdb detected at block %d)\n"), j);
			if (accepted) {
				*accepted = true;
			}
			return -3;
		}
		if (!memcmp(buf, "RDSK", 4) || !memcmp(buf, "DRKS", 4)) {
			if (do_rdbdump)
				rdbdump(h, offset, buf, blocksize);
			write_log(_T("hd accepted (rdb detected at block %d)\n"), j);
			if (accepted) {
				*accepted = true;
			}
			return -1;
		}

		if (!memcmp(buf + 2, "CIS@", 4) && !memcmp(buf + 16, CA, strlen(CA))) {
			if (do_rdbdump)
				rdbdump(h, offset, NULL, blocksize);
			write_log(_T("hd accepted (PCMCIA RAM)\n"));
			return -2;
		}
		if (j == 0) {
			for (i = 0; i < blocksize; i++) {
				if (buf[i])
					empty = 0;
			}
		}
		offset += blocksize;
	}
	if (!empty) {
		int mounted;
		if (regexiststree (NULL, _T("DangerousDrives"))) {
			UAEREG *fkey = regcreatetree (NULL, _T("DangerousDrives"));
			int match = 0;
			if (fkey) {
				int idx = 0;
				int size, size2;
				TCHAR tmp2[MAX_DPATH], tmp[MAX_DPATH];
				for (;;) {
					size = sizeof (tmp) / sizeof (TCHAR);
					size2 = sizeof (tmp2) / sizeof (TCHAR);
					if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
						break;
					if (!_tcscmp (tmp, name))
						match = 1;
					idx++;
				}
				regclosetree (fkey);
			}
			if (match) {
				if (do_rdbdump > 1)
					rdbdump (h, origoffset, NULL, blocksize);
				write_log (_T("hd accepted, enabled in registry!\n"));
				return -7;
			}
		}
		int ptype;
		mounted = ismounted (name, h, &ptype);
		if (!mounted) {
			if (do_rdbdump > 1)
				rdbdump (h, origoffset, NULL, blocksize);
			write_log (_T("hd accepted, not empty and not mounted in Windows\n"));
			return -8;
		}
		if (mounted < 0) {
			write_log (_T("hd ignored, NTFS partitions\n"));
			return 0;
		}
		if (mounted > 1) {
			return 3;
		}
		if (ptype == PARTITION_STYLE_GPT) {
			return -11;
		}
		if (ptype == PARTITION_STYLE_MBR) {
			return -6;
		}
		return -10;
	}
	if (do_rdbdump > 1)
		rdbdump (h, origoffset, NULL, blocksize);
	write_log (_T("hd accepted (empty)\n"));
	return -9;
}


static void trim (TCHAR *s)
{
	while(s[0] != '\0' && s[_tcslen(s) - 1] == ' ')
		s[_tcslen(s) - 1] = 0;
}

int isharddrive (const TCHAR *name)
{
	int i;
	for (i = 0; i < hdf_getnumharddrives (); i++) {
		if (!_tcscmp (uae_drives[i].device_name, name))
			return i;
	}
	return -1;
}

static const TCHAR *hdz[] = { _T("hdz"), _T("zip"), _T("rar"), _T("7z"), NULL };

static INT_PTR CALLBACK ProgressDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
	SCSI_PASS_THROUGH_DIRECT spt;
	ULONG Filler;
	UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;


static int do_scsi_in(HANDLE h, const uae_u8 *cdb, int cdblen, uae_u8 *in, int insize, bool fast)
{
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	DWORD status, returned;

	memset(&swb, 0, sizeof swb);
	swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
	swb.spt.CdbLength = cdblen;
	swb.spt.DataIn = insize > 0 ? SCSI_IOCTL_DATA_IN : 0;
	swb.spt.DataTransferLength = insize;
	swb.spt.DataBuffer = in;
	swb.spt.TimeOutValue = fast ? 2 : 10 * 60;
	swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
	swb.spt.SenseInfoLength = 32;
	memcpy(swb.spt.Cdb, cdb, cdblen);
	write_log(_T("IOCTL_SCSI_PASS_THROUGH_DIRECT: "));
	for (int i = 0; i < cdblen; i++) {
		write_log(_T("%02X."), cdb[i]);
	}
	status = DeviceIoControl (h, IOCTL_SCSI_PASS_THROUGH_DIRECT,
		&swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
		&returned, NULL);
	if (!status) {
		DWORD err = GetLastError();
		write_log(_T(" failed %08x\n"), err);
		// stupid hardware
		if (err == ERROR_SEM_TIMEOUT)
			return -2;
		return -1;
	} else if (swb.spt.ScsiStatus) {
		write_log(_T("\n"));
		write_log(_T("SENSE: "));
		for (int i = 0; i < swb.spt.SenseInfoLength; i++) {
			write_log(_T("%02X."), swb.SenseBuf[i]);
		}
		write_log(_T("\n"));
		return -1;
	} else {
		write_log(_T(" OK (%d bytes)\n"), swb.spt.DataTransferLength);
	}
	return swb.spt.DataTransferLength;
}

#if 0
static const uae_u8 inquiry[] = { 0x12, 0x01, 0x81, 0, 0xf0, 0 };
static const uae_u8 modesense[] = { 0x1a, 0x00, 0x3f, 0, 0xf0, 0 };

static void scsidirect(HANDLE h)
{
	uae_u8 *inbuf;

	inbuf = (uae_u8*)VirtualAlloc (NULL, 65536, MEM_COMMIT, PAGE_READWRITE);

	do_scsi_in(h, modesense, 6, inbuf, 0xf0);
	do_scsi_in(h, inquiry, 6, inbuf, 0xf0);
	do_scsi_in(h, realtek_read, 6, inbuf, 0xff00);

	FILE *f = fopen("c:\\temp\\identity.bin", "wb");
	fwrite(inbuf, 1, 512, f);
	fclose(f);

	VirtualFree(inbuf, 65536, MEM_RELEASE);
}

#endif

#if 0
static void getserial (HANDLE h)
{
	DWORD outsize, written;
	DISK_GEOMETRY_EX *out;
	VOLUME_DISK_EXTENTS *vde;

	outsize = sizeof (DISK_GEOMETRY_EX) + 10 * (sizeof (DISK_DETECTION_INFO) + sizeof (DISK_PARTITION_INFO));
	out = (DISK_GEOMETRY_EX*)xmalloc(uae_u8, outsize);
	if (DeviceIoControl (h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, out, outsize, &written, NULL)) {
		DISK_DETECTION_INFO *ddi = DiskGeometryGetDetect (out);
		DISK_PARTITION_INFO *dpi = DiskGeometryGetPartition (out);
		write_log (_T(""));
	}
	xfree (out);


	outsize = sizeof (VOLUME_DISK_EXTENTS) + sizeof (DISK_EXTENT) * 10;
	vde = (VOLUME_DISK_EXTENTS*)xmalloc(uae_u8, outsize);
	if (DeviceIoControl (h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
		if (vde->NumberOfDiskExtents > 0)
			write_log(_T("%d\n"), vde->Extents[0].DiskNumber);
	}
	xfree (vde);
}
#endif

#if 0
static void queryataidentity(HANDLE h)
{
	DWORD r, size;
	uae_u8 *b;
	ATA_PASS_THROUGH_EX *ata;

	size = sizeof (ATA_PASS_THROUGH_EX) + 512;
	b = xcalloc (uae_u8, size);
	ata = (ATA_PASS_THROUGH_EX*)b;
	uae_u8 *data = b + sizeof(ATA_PASS_THROUGH_EX);

	ata->Length = sizeof(ATA_PASS_THROUGH_EX);
	ata->DataTransferLength = 512;
	ata->TimeOutValue = 10;
	ata->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
	IDEREGS* ir = (IDEREGS*)ata->CurrentTaskFile;
	ir->bCommandReg = ID_CMD;
	ata->DataBufferOffset = data - b;
	
	if (!DeviceIoControl (h, IOCTL_ATA_PASS_THROUGH, b, size, b, size, &r, NULL)) {
		write_log (_T("IOCTL_ATA_PASS_THROUGH_DIRECT Identify Device failed %d\n"), GetLastError ());
	}

	xfree(b);
}
#endif

static int getstorageproperty (PUCHAR outBuf, int returnedLength, struct uae_driveinfo *udi, int ignoreduplicates);
static bool getstorageinfo(uae_driveinfo *udi, STORAGE_DEVICE_NUMBER sdnp);

#if 0
typedef enum _STORAGE_PROTOCOL_TYPE { 
  ProtocolTypeUnknown      = 0x00,
  ProtocolTypeScsi,
  ProtocolTypeAta,
  ProtocolTypeNvme,
  ProtocolTypeSd,
  ProtocolTypeProprietary  = 0x7E,
  ProtocolTypeMaxReserved  = 0x7F
} STORAGE_PROTOCOL_TYPE, *PSTORAGE_PROTOCOL_TYPE;

typedef struct _STORAGE_PROTOCOL_SPECIFIC_DATA {
  STORAGE_PROTOCOL_TYPE ProtocolType;
  DWORD                 DataType;
  DWORD                 ProtocolDataRequestValue;
  DWORD                 ProtocolDataRequestSubValue;
  DWORD                 ProtocolDataOffset;
  DWORD                 ProtocolDataLength;
  DWORD                 FixedProtocolReturnData;
  DWORD                 Reserved[3];
} STORAGE_PROTOCOL_SPECIFIC_DATA, *PSTORAGE_PROTOCOL_SPECIFIC_DATA;

typedef enum _STORAGE_PROTOCOL_ATA_DATA_TYPE { 
  AtaDataTypeUnknown   = 0,
  AtaDataTypeIdentify,
  AtaDataTypeLogPage
} STORAGE_PROTOCOL_ATA_DATA_TYPE, *PSTORAGE_PROTOCOL_ATA_DATA_TYPE;

static void getstorageproperty_ataidentity(HANDLE hDevice)
{
	DWORD returnedLength;
	DWORD bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + 512;
	uae_u8 *buffer = xcalloc(uae_u8, bufferLength);
	PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)buffer;
	PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

	query->PropertyId = (STORAGE_PROPERTY_ID)50;
	query->QueryType = PropertyStandardQuery;

	protocolData->ProtocolType = ProtocolTypeAta;
	protocolData->DataType = AtaDataTypeIdentify;
	protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = 512;

	DWORD status = DeviceIoControl(
		hDevice,
		IOCTL_STORAGE_QUERY_PROPERTY,
		query,
		bufferLength,
		query,
		bufferLength,
		&returnedLength,
		NULL);

	DWORD spqerr = GetLastError();

	xfree(buffer);
}
#endif

static void bintotextpart(TCHAR *out, uae_u8 *data, int size)
{
	for (int i = 0; i < size; i++) {
		uae_u8 b = data[i];
		if (b >= 32 && b < 127)
			out[i] = (TCHAR)b;
		else
			out[i] = '.';
	}
	out[size] = 0;
}

static TCHAR *bintotextline(TCHAR *out, uae_u8 *data, int size)
{
	TCHAR tmp2[MAX_DPATH];
	int w = 32;
	for (int i = 0; i < size; i += w) {
		for (int j = 0; j < w + 1 + w / 2; j++) {
			tmp2[j * 2 + 0] = ' ';
			tmp2[j * 2 + 1] = ' ';
		}
		for (int j = 0; j < w && j + i < size; j++) {
			uae_u8 b = data[i + j];
			_stprintf (tmp2 + j * 2, _T("%02X"), b);
			tmp2[j * 2 + 2] = ' ';
			if (b >= 32 && b < 127)
				tmp2[w * 2 + 1 + j] = (TCHAR)b;
			else
				tmp2[w * 2 + 1 + j] = '.';
		}
		tmp2[w * 2] = ' ';
		tmp2[w * 2 + 1 + w] = 0;
		_tcscat (out, tmp2);
		_tcscat (out, _T("\r\n"));
	}
	return out + _tcslen(out);
}

static int chs_secs, chs_cyls, chs_heads;
static TCHAR *parse_identity(uae_u8 *data, struct ini_data *ini, TCHAR *s)
{
	uae_u16 v;

	v = (data[1 * 2 + 0] << 8) | (data[1 * 2 + 1] << 0);
	chs_cyls = v;
	if (ini)
		ini_addnewval(ini, _T("IDENTITY"), _T("Geometry_Cylinders"), v);
	if (s) {
		_stprintf(s, _T("Cylinders: %04X (%u)\r\n"), v, v);
		s += _tcslen(s);
	}
	v = (data[3 * 2 + 0] << 8) | (data[3 * 2 + 1] << 0);
	chs_heads = v;
	if (ini)
		ini_addnewval(ini, _T("IDENTITY"), _T("Geometry_Heads"), v);
	if (s) {
		_stprintf(s, _T("Heads: %04X (%u)\r\n"), v, v);
		s += _tcslen(s);
	}
	v = (data[6 * 2 + 0] << 8) | (data[6 * 2 + 1] << 0);
	chs_secs = v;
	if (ini)
		ini_addnewval(ini, _T("IDENTITY"), _T("Geometry_Surfaces"), v);
	if (s) {
		_stprintf(s, _T("Surfaces: %04X (%u)\r\n"), v, v);
		s += _tcslen(s);
	}

	v = (data[49 * 2 + 0] << 8) | (data[49 * 2 + 1] << 0);
	if (v & (1 << 9)) {
		// LBA supported
		uae_u32 lba = (data[60 * 2 + 0] << 24) | (data[60 * 2 + 1] << 16) | (data[61 * 2 + 0] << 8) | (data[61 * 2 + 1] << 0);
		if (ini)
			ini_addnewval(ini, _T("IDENTITY"), _T("LBA"), lba);
		if (s) {
			_stprintf(s, _T("LBA: %08X (%u)\r\n"), lba, lba);
			s += _tcslen(s);
		}
	}
	v = (data[83 * 2 + 0] << 8) | (data[83 * 2 + 1] << 0);
	if ((v & 0xc000) == 0x4000 && (v & (1 << 10))) {
		// LBA48 supported
		uae_u64 lba = (data[100 * 2 + 0] << 24) | (data[100 * 2 + 1] << 16) | (data[101 * 2 + 0] << 8) | (data[101 * 2 + 1] << 0);
		lba <<= 32;
		lba |= (data[102 * 2 + 0] << 24) | (data[102 * 2 + 1] << 16) | (data[103 * 2 + 0] << 8) | (data[103 * 2 + 1] << 0);
		if (ini)
			ini_addnewval64(ini, _T("IDENTITY"), _T("LBA48"), lba);
		if (s) {
			_stprintf(s, _T("LBA48: %012llX (%llu)\r\n"), lba, lba);
			s += _tcslen(s);
		}
	}
	return s;
}

void gui_infotextbox(HWND hDlg, const TCHAR *text);

static bool hd_read_ata(HANDLE h, uae_u8 *ideregs, uae_u8 *datap, int length)
{
	DWORD r, size;
	uae_u8 *b;
	ATA_PASS_THROUGH_EX *ata;

	size = sizeof(ATA_PASS_THROUGH_EX) + length;
	b = xcalloc(uae_u8, size);
	ata = (ATA_PASS_THROUGH_EX*)b;
	uae_u8 *data = b + sizeof(ATA_PASS_THROUGH_EX);
	ata->Length = sizeof(ATA_PASS_THROUGH_EX);
	ata->DataTransferLength = length;
	ata->TimeOutValue = 10;
	ata->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
	IDEREGS* ir = (IDEREGS*)ata->CurrentTaskFile;
	ir->bFeaturesReg = ideregs[1];
	ir->bSectorCountReg = ideregs[2];
	ir->bSectorNumberReg = ideregs[3];
	ir->bCylLowReg = ideregs[4];
	ir->bCylHighReg = ideregs[5];
	ir->bDriveHeadReg = ideregs[6];
	ir->bCommandReg = ideregs[7];
	ata->DataBufferOffset = data - b;
	if (!DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH, b, size, b, size, &r, NULL)) {
		write_log(_T("IOCTL_ATA_PASS_THROUGH_DIRECT READ failed %08x\n"), GetLastError());
		return false;
	}
	write_log(_T("IOCTL_ATA_PASS_THROUGH_DIRECT READ succeeded\n"));
	memcpy(datap, data, length);
	xfree(b);
	return true;
}

static bool hd_get_meta_ata(HANDLE h, bool atapi, uae_u8 *datap)
{
	DWORD r, size;
	uae_u8 *b;
	ATA_PASS_THROUGH_EX *ata;

	size = sizeof (ATA_PASS_THROUGH_EX) + 512;
	b = xcalloc (uae_u8, size);
	ata = (ATA_PASS_THROUGH_EX*)b;
	uae_u8 *data = b + sizeof(ATA_PASS_THROUGH_EX);
	ata->Length = sizeof(ATA_PASS_THROUGH_EX);
	ata->DataTransferLength = 512;
	ata->TimeOutValue = 10;
	ata->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
	IDEREGS* ir = (IDEREGS*)ata->CurrentTaskFile;
	ir->bCommandReg = atapi ? ATAPI_ID_CMD : ID_CMD;
	ata->DataBufferOffset = data - b;
	if (!DeviceIoControl (h, IOCTL_ATA_PASS_THROUGH, b, size, b, size, &r, NULL)) {
		write_log (_T("IOCTL_ATA_PASS_THROUGH_DIRECT ID failed %08x\n"), GetLastError());
		return false;
	}
	write_log(_T("IOCTL_ATA_PASS_THROUGH_DIRECT succeeded\n"));
	memcpy(datap, data, 512);
	xfree(b);
	return true;
}

#define INQUIRY_LEN 240

static bool hd_meta_hack_jmicron(HANDLE h, uae_u8 *data, uae_u8 *inq)
{
	uae_u8 cmd[16];

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xdf;
	cmd[1] = 0x10;
	cmd[4] = 1;
	cmd[6] = 0x72;
	cmd[7] = 0x0f;
	cmd[11] = 0xfd;
	if (do_scsi_in(h, cmd, 12, data + 32, 1, true) < 0) {
		memset(data, 0, 512);
		return false;
	}
	if (!(data[32] & 0x40) && !(data[32] & 0x04)) {
		memset(data, 0, 512);
		return false;
	}
#if 0
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xdf;
	cmd[1] = 0x10;
	cmd[4] = 16;
	cmd[6] = 0x80;
	cmd[11] = 0xfd;
	if (do_scsi_in(h, cmd, 12, data, 16) < 0) {
		memset(data, 0, 512);
		return false;
	}
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xdf;
	cmd[1] = 0x10;
	cmd[4] = 16;
	cmd[6] = 0x90;
	cmd[11] = 0xfd;
	if (do_scsi_in(h, cmd, 12, data + 16, 16) < 0) {
		memset(data, 0, 512);
		return false;
	}
#endif	
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xdf;
	cmd[1] = 0x10;
	cmd[3] = 512 >> 8;
	cmd[10] = 0xa0 | ((data[32] & 0x40) ? 0x10 : 0x00);
	cmd[11] = ID_CMD;
	if (do_scsi_in(h, cmd, 12, data, 512, true) < 0) {
		memset(data, 0, 512);
		return false;
	}
	return true;
}

static const uae_u8 realtek_inquiry_0x83[] = { 0x12, 0x01, 0x83, 0, 0xf0, 0 };
static const uae_u8 realtek_read[] = { 0xf0, 0x0d, 0xfa, 0x00, 0x02, 0x00 };

static bool hd_get_meta_hack_realtek(HWND hDlg, HANDLE h, uae_u8 *data, uae_u8 *inq)
{
	uae_u8 cmd[16];

	memset(data, 0, 512);
	memcpy(cmd, realtek_read, sizeof(realtek_read));
	if (do_scsi_in(h, cmd, 6, data, 512, true) < 0) {
		memset(data, 0, 512);
		return false;
	}

	int state = 0;

	memset(cmd, 0, 6); // TEST UNIT READY
	const TCHAR *infotxt;
	if (do_scsi_in(h, cmd, 6, data, 0, true) < 0) {
		state = 1;
		infotxt = _T("Realtek hack, insert card.");
	} else {
		infotxt = _T("Realtek hack, remove and insert the card.");
	}

	SAVECDS;
	HWND hwnd = CustomCreateDialog(IDD_PROGRESSBAR, hDlg, ProgressDialogProc, &cdstate);
	if (hwnd == NULL) {
		RESTORECDS;
		return false;
	}
	HWND hwndprogress = GetDlgItem (hwnd, IDC_PROGRESSBAR);
	ShowWindow(hwndprogress, SW_HIDE);
	HWND hwndprogresstxt = GetDlgItem (hwnd, IDC_PROGRESSBAR_TEXT);
	ShowWindow(hwnd, SW_SHOW);

	int tcnt = 0;
	while(cdstate.active) {
		MSG msg;
		SendMessage (hwndprogresstxt, WM_SETTEXT, 0, (LPARAM)infotxt);
		while (PeekMessage (&msg, hwnd, 0, 0, PM_REMOVE)) {
			if (!IsDialogMessage (hwnd, &msg)) {
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
		Sleep(100);
		tcnt++;
		if (tcnt >= 10) {
			memset(cmd, 0, 6);
			if (do_scsi_in(h, cmd, 6, data, 0, true) >= 0) {
				if (state != 0) {
					break;
				}
			} else {
				if (state == 0) {
					state++;
					infotxt = _T("Removed. Re-insert the card.");
				}
			}
			tcnt = 0;
		}
	}

	if (cdstate.active) {
		DestroyWindow (hwnd);
		MSG msg;
		while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	RESTORECDS;

	memset(data, 0, 512);
	memcpy(cmd, realtek_read, sizeof(realtek_read));
	if (do_scsi_in(h, cmd, 6, data, 512, true) > 0)
		return true;

	return false;
}

static bool hd_get_meta_hack(HWND hDlg, HANDLE h, uae_u8 *data, uae_u8 *inq, struct uae_driveinfo *udi)
{
	uae_u8 cmd[16];

	memset(data, 0, 512);
	if (udi->usb_vid == 0x152d && (udi->usb_pid == 0x2329 || udi->usb_pid == 0x2336 || udi->usb_pid == 0x2338 || udi->usb_pid == 0x2339)) {
		return hd_meta_hack_jmicron(h, data, inq);
	}
	if (!hDlg)
		return false;
	memcpy(cmd, realtek_inquiry_0x83, sizeof(realtek_inquiry_0x83));
	if (do_scsi_in(h, cmd, 6, data, 0xf0, true) >= 0 && !memcmp(data + 20, "realtek\0", 8)) {
		return hd_get_meta_hack_realtek(hDlg, h, data, inq);
	}
	memset(data, 0, 512);
	return false;
}

void ata_byteswapidentity(uae_u8 *d);

static const uae_u16 blacklist[] =
{
	0x14cd, 0xffff,
	0x0aec, 0xffff,
	0, 0
};

static bool readidentity(HANDLE h, struct uae_driveinfo *udi, struct hardfiledata *hfd)
{
	uae_u8 cmd[16];
	uae_u8 *data = NULL;
	bool ret = false;
	bool satl = false;
	bool handleopen = false;
	int v;

	memset(udi->identity, 0, 512);
	if (hfd)
		memset(hfd->identity, 0, 512);

	if (udi->scsi_direct_fail)
		return false;
	if (udi->usb_vid) {
		for (int i = 0; blacklist[i]; i += 2) {
			if (udi->usb_vid == blacklist[i]) {
				if (udi->usb_pid == blacklist[i + 1] || blacklist[i + 1] == 0xffff) {
					udi->scsi_direct_fail = true;
					write_log(_T("VID=%04x PID=%04x blacklisted\n"), udi->usb_vid, udi->usb_pid);
					return false;
				}
			}
		}
	}

	data = (uae_u8*)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
	if (!data)
		goto end;

	if (h == INVALID_HANDLE_VALUE) {
		DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
		h = CreateFile(udi->device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, flags, NULL);
		if (h == INVALID_HANDLE_VALUE)
			goto end;
		handleopen = true;
	}

	if (udi->BusType == BusTypeAta || udi->BusType == BusTypeSata || udi->BusType == BusTypeAtapi) {
		if (!_tcscmp(udi->vendor_id, _T("ATA"))) {
			satl = true;
		}
	}

	if (satl || udi->BusType == BusTypeScsi || udi->BusType == BusTypeUsb || udi->BusType == BusTypeRAID) {

		if (udi->devicetype == 5) {

			memset(cmd, 0, sizeof(cmd));
			memset(data, 0, 512);
			cmd[0] = 0x85; // SAT ATA PASSTHROUGH (16) (12 conflicts with MMC BLANK command)
			cmd[1] = 4 << 1; // PIO data-in
			cmd[2] = 0x08 | 0x04 | 0x02; // dir = from device, 512 byte block, sector count = block cnt
			cmd[6] = 1; // block count
			cmd[14] = 0xa1; // identity packet device
			v = do_scsi_in(h, cmd, 16, data, 512, true);
			if (v > 0) {
				ret = true;
			} else {
				write_log(_T("SAT: ATA PASSTHROUGH(16) failed\n"));
				if (v < -1)
					udi->scsi_direct_fail = true;
			}

		} else {

			memset(cmd, 0, sizeof(cmd));
			memset(data, 0, 512);
			cmd[0] = 0xa1; // SAT ATA PASSTHROUGH (12)
			cmd[1] = 4 << 1; // PIO data-in
			cmd[2] = 0x08 | 0x04 | 0x02; // dir = from device, 512 byte block, sector count = block cnt
			cmd[4] = 1; // block count
			cmd[9] = 0xec; // identity
			v = do_scsi_in(h, cmd, 12, data, 512, true);
			if (v > 0) {
				ret = true;
			} else {
				write_log(_T("SAT: ATA PASSTHROUGH(12) failed\n"));
				if (v < -1)
					udi->scsi_direct_fail = true;
			}
		}
	}

	if (!ret) {
		if (udi->BusType == BusTypeUsb) {
			ret = hd_get_meta_hack(NULL, h, data, NULL, udi);
		} else if (udi->BusType == BusTypeAta || udi->BusType == BusTypeSata || udi->BusType == BusTypeAtapi) {
			ret = hd_get_meta_ata(h, udi->BusType == BusTypeAtapi, data);
		}
	}

	if (ret) {
		ata_byteswapidentity(data);
		memcpy(udi->identity, data, 512);
		if (hfd)
			memcpy(hfd->identity, data, 512);
		write_log(_T("Real harddrive IDENTITY read\n"));
	}

end:

	if (handleopen && h != INVALID_HANDLE_VALUE)
		CloseHandle(h);
	if (data)
		VirtualFree(data, 0, MEM_RELEASE);
	return ret;
}

static bool do_scsi_read10_chs(HANDLE handle, uae_u32 lba, int c, int h, int s, uae_u8 *data, int cnt, int *pflags, bool log)
{
	uae_u8 cmd[10];
	bool r;
	int flags = *pflags;

#if 0
	cmd[0] = 0x28;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;
	cmd[5] = 1;
	cmd[8] = 1;
	do_scsi_in(handle, cmd, 10, data, 512);
#endif
	memset(data, 0, sizeof(cmd));

	if (!flags) {
		// use direct ATA to read if direct ATA identity read succeeded
		if (hd_get_meta_ata(handle, false, data)) {
			flags = 2;
		}
	}

	if (flags == 2) {
		memset(cmd, 0, sizeof(cmd));
		cmd[2] = cnt;
		cmd[3] = s;
		cmd[4] = c;
		cmd[5] = c >> 8;
		cmd[6] = 0xa0 | (h & 15);
		cmd[7] = 0x20; // read sectors
		r = hd_read_ata(handle, cmd, data, cnt * 512);
		if (r)
			goto done;
	}
	
	memset(data, 0, 512 * cnt);
	memset(cmd, 0, sizeof(cmd));

	cmd[0] = 0x28;
	if (lba != 0xffffffff) {
		cmd[2] = lba >> 24;
		cmd[3] = lba >> 16;
		cmd[4] = lba >> 8;
		cmd[5] = lba >> 0;
	} else {
		cmd[2] = h & 15;
		cmd[3] = c >> 8;
		cmd[4] = c;
		cmd[5] = s;
	}
	cmd[8] = cnt;
	
	r = do_scsi_in(handle, cmd, 10, data, 512 * cnt, false) > 0;

done:
	if (r && log) {
		int s = 32;
		int o = 0;
		for (int i = 0; i < 512; i += s) {
			for (int j = 0; j < s; j++) {
				write_log(_T("%02x"), data[o + j]);
			}
			write_log(_T(" "));
			for (int j = 0; j < s; j++) {
				uae_u8 v = data[o + j];
				write_log(_T("%c"), v >= 32 && v <= 126 ? v : '.');
			}
			write_log(_T("\n"));
			o += s;
		}
	}

	*pflags = flags;

	return r;
}

static bool hd_get_meta_satl(HWND hDlg, HANDLE h, uae_u8 *data, TCHAR *text, struct ini_data *ini, bool *atapi)
{
	uae_u8 cmd[16];
	TCHAR cline[256];
	bool ret = false;

	*atapi = false;
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x12; // inquiry
	cmd[4] = INQUIRY_LEN;
	if (do_scsi_in(h, cmd, 6, data, INQUIRY_LEN, true) < 0) {
		write_log(_T("SAT: INQUIRY failed\n"));
		return false;
	}

	int type = data[0] & 0x3f;

	_tcscat (text, _T("INQUIRY:\r\n"));
	int len = INQUIRY_LEN;
	while (len > 0) {
		len--;
		if (data[len] != 0)
			break;
	}
	len += 3;
	len &= ~3;
	bintotextline(text, data, len);
	_tcscat (text, _T("\r\n"));
	bintotextpart(cline, data + 8, 44 - 8);
	ini_addnewcomment(ini, _T("INQUIRY"), cline);
	ini_addnewdata(ini, _T("INQUIRY"), _T("00"), data, len);

	memset(data, 0, 512);
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x25;
	if (do_scsi_in(h, cmd, 10, data, 8, true) >= 8) {
		_tcscat (text, _T("READ CAPACITY:\r\n"));
		bintotextline(text, data, 8);
		_tcscat (text, _T("\r\n"));
		ini_addnewdata(ini, _T("READ CAPACITY"), _T("DATA"), data, 8);
	}

	// get supported evpd pages
	memset(data, 0, 512);
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x12; // inquiry
	cmd[1] = 1;
	cmd[4] = INQUIRY_LEN;
	if (do_scsi_in(h, cmd, 6, data, INQUIRY_LEN, true) > 0) {
		uae_u8 evpd[256];
		int cnt = 0;
		uae_u8 pl = data[3];
		uae_u8 *p = &data[4];
		while (pl != 0) {
			if (*p) {
				evpd[cnt++] = *p;
			}
			p++;
			pl--;
		}
		for (int i = 0; i < cnt; i++) {
			for (int j = i + 1; j < cnt; j++) {
				if (evpd[i] > evpd[j]) {
					uae_u8 t = evpd[i];
					evpd[i] = evpd[j];
					evpd[j] = t;
				}
			}
		}
		for (int i = 0; i < cnt; i++) {
			cmd[2] = evpd[i];
			memset(data, 0, 512);
			if (do_scsi_in(h, cmd, 6, data, INQUIRY_LEN, true) > 0) {
				TCHAR tmp[256];
				_stprintf(tmp, _T("INQUIRY %02X:\r\n"), evpd[i]);
				_tcscat (text, tmp);
				int len = INQUIRY_LEN;
				while (len > 0) {
					len--;
					if (data[len] != 0)
						break;
				}
				len += 3;
				len &= ~3;
				bintotextline(text, data, len);
				_tcscat (text, _T("\r\n"));
				_stprintf(tmp, _T("%02X"), evpd[i]);
				ini_addnewdata(ini, _T("INQUIRY"),tmp, data, len);
			}
		}
	}

	// get mode sense pages
	memset(data, 0, 0xff00);
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x5a;
	cmd[2] = 0x80 | 0x3f;
	cmd[7] = 0xff;
	cmd[8] = 0;
	len = do_scsi_in(h, cmd, 10, data, 0xff00, true);
	if (len > 0) {
		TCHAR tmp[4000];
		int l = (data[0] << 8) | data[1];
		write_log(_T("MODE SENSE LEN %d\n"), l);
		if (l > len)
			l = len;
		_tcscat (text, _T("MODE SENSE:\r\n"));
		bintotextline(text, data + 2, 4);
		_tcscat (text, _T("\r\n"));
		ini_addnewdata(ini, _T("MODE SENSE"),_T("PARAMETER LIST"), data + 2, 4);
		uae_u16 dbd = (data[6] << 8) | data[7];
		l -= 8;
		write_log(_T("MODE SENSE DBD %d\n"), dbd);
		if (dbd <= 8) {
			l -= dbd;
			if (dbd == 8) {
				_tcscat(text, _T("MODE SENSE BLOCK DESCRIPTOR DATA:\r\n"));
				bintotextline(text, data + 8, dbd);
				_tcscat(text, _T("\r\n"));
				ini_addnewdata(ini, _T("MODE SENSE"), _T("BLOCK DESCRIPTOR DATA"), data + 8, dbd);
			}
			uae_u8 *p = &data[8 + dbd];
			while (l >= 2) {
				uae_u8 page = p[0];
				uae_u8 pl = p[1];
				if (pl > l - 2)
					break;
				write_log(_T("MODE SENSE PAGE %02x LEN %d\n"), page, pl);
				_stprintf(tmp, _T("MODE SENSE %02x:\r\n"), page);
				_tcscat(text, tmp);
				bintotextline(text, p, pl + 2);
				_tcscat(text, _T("\r\n"));
				_stprintf(tmp, _T("%02X"), page);
				ini_addnewdata(ini, _T("MODE SENSE"), tmp, p, pl + 2);
				p += 2 + pl;
				l -= 2 + pl;
			}
		}
		write_log(_T("MODE SENSE END\n"));
	}

	if (type != 0 && type != 05) {
		write_log(_T("SAT: Not Direct access or CD device\n"));
		return false;
	}

	if (type == 5) {

		memset(cmd, 0, sizeof(cmd));
		memset(data, 0, 512);
		cmd[0] = 0x85; // SAT ATA PASSTHROUGH (16)
		cmd[1] = 4 << 1; // PIO data-in
		cmd[2] = 0x08 | 0x04 | 0x02; // dir = from device, 512 byte block, sector count = block cnt
		cmd[6] = 1; // block count
		cmd[14] = 0xa1; // identity packet device
		if (do_scsi_in(h, cmd, 16, data, 512, true) > 0) {
			ret = true;
			*atapi = true;
		} else {
			write_log(_T("SAT: ATA PASSTHROUGH(16) failed\n"));
		}

	} else {

		memset(cmd, 0, sizeof(cmd));
		memset(data, 0, 512);
		cmd[0] = 0xa1; // SAT ATA PASSTHROUGH (12)
		cmd[1] = 4 << 1; // PIO data-in
		cmd[2] = 0x08 | 0x04 | 0x02; // dir = from device, 512 byte block, sector count = block cnt
		cmd[4] = 1; // block count
		cmd[9] = 0xec; // identity
		if (do_scsi_in(h, cmd, 12, data, 512, true) > 0) {
			ret = true;
		} else {
			write_log(_T("SAT: ATA PASSTHROUGH(12) failed\n"));
		}
	}

	return ret;
}

static INT_PTR CALLBACK CHSDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		SetDlgItemInt(hDlg, IDC_CHS_SECTORS, chs_secs, FALSE);
		SetDlgItemInt(hDlg, IDC_CHS_CYLINDERS, chs_cyls, FALSE);
		SetDlgItemInt(hDlg, IDC_CHS_HEADS, chs_heads, FALSE);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_CHS_SECTORS:
			chs_secs = GetDlgItemInt(hDlg, IDC_CHS_SECTORS, NULL, FALSE);
			break;
		case IDC_CHS_CYLINDERS:
			chs_cyls = GetDlgItemInt(hDlg, IDC_CHS_CYLINDERS, NULL, FALSE);
			break;
		case IDC_CHS_HEADS:
			chs_heads = GetDlgItemInt(hDlg, IDC_CHS_HEADS, NULL, FALSE);
			break;
		case IDOK:
			CustomDialogClose(hDlg, 1);
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}


static int gethdfchs(HWND hDlg, struct uae_driveinfo *udi, HANDLE h, int *cylsp, int *headsp, int *secsp)
{
	uae_u8 cmd[10];
	int cyls = 0, heads = 0, secs = 0;
	uae_u8 *data = (uae_u8*)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
	DWORD err = 0;
	HFONT font;
	HWND hwnd;

	SAVECDS;

	memset(data, 0, 512);
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x25;
	if (do_scsi_in(h, cmd, 10, data, 8, true) == 8) {
#if 1
		if (data[0] != 0xff || data[1] != 0xff || data[2] != 0xff || data[3] != 0xff) {
			err = -11;
			goto end;
		}
		if (data[4] != 0x00 || data[5] != 0x00 || data[6] != 0x02 || data[7] != 0x00) {
			err = -12;
			goto end;
		}
#endif
	} else {
		write_log(_T("READ_CAPACITY FAILED\n"));
		err = -10;
	}
	if (!cylsp || !headsp || !secsp)
		return err;

	if (readidentity(h, udi, NULL)) {
		parse_identity(udi->identity, NULL, NULL);
	}

	hwnd = CustomCreateDialog(IDD_CHSQUERY, hDlg, CHSDialogProc, &cdstate);
	if (hwnd == NULL) {
		err = -15;
		goto end;
	}
	font = CreateFont(getscaledfontsize(-1, hDlg), 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Lucida Console"));
	if (font)
		SendMessage(GetDlgItem(hwnd, IDD_CHSQUERY), WM_SETFONT, WPARAM(font), FALSE);
	while (cdstate.active) {
		MSG msg;
		int ret;
		WaitMessage();
		while ((ret = GetMessage(&msg, NULL, 0, 0))) {
			if (ret == -1)
				break;
			if (!IsWindow(hwnd) || !IsDialogMessage(hwnd, &msg)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}
	if (font) {
		DeleteObject(font);
	}
	if (cdstate.status == 0) {
		err = -100;
		goto end;
	}
	secs = chs_secs;
	heads = chs_heads;
	cyls = chs_cyls;
	if (secs <= 0 || heads <= 0 || cyls <= 0) {
		err = -13;
		goto end;
	}
	if (secs >= 256 || heads > 16 || cyls > 2048) {
		err = -14;
		goto end;
	}
end:
	RESTORECDS;
	VirtualFree(data, 0, MEM_RELEASE);
	if (cylsp)
		*cylsp = cyls;
	if (headsp)
		*headsp = heads;
	if (secsp)
		*secsp = secs;
	return err;
}


static TCHAR geometry_file[MAX_DPATH];
static struct ini_data *hdini;
static INT_PTR CALLBACK StringBoxDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		ShowWindow(GetDlgItem (hDlg, IDC_SAVEBOOTBLOCK), SW_SHOW);
		SetWindowText(hDlg, _T("Harddrive information"));
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_SAVEBOOTBLOCK:
		{
			TCHAR out[MAX_DPATH];
			out[0] = 0;
			if (DiskSelection(hDlg, 0, 24, &workprefs, geometry_file, out)) {
				ini_save(hdini, out);
			}
			break;
		}
		case IDOK:
			CustomDialogClose(hDlg, 1);
			DestroyWindow (hDlg);
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void ata_byteswapidentity(uae_u8 *d);

void hd_get_meta(HWND hDlg, int idx, TCHAR *geometryfile)
{
	struct uae_driveinfo *udi = &uae_drives[idx];
	bool satl = false;
	uae_u8 *data = NULL;
	uae_u8 inq[INQUIRY_LEN + 4] = { 0 };
	TCHAR *text, *tptr;
	struct ini_data *ini = NULL;
	bool atapi = false;
	HWND hwnd;
	bool empty = true;

	geometryfile[0] = 0;
	text = xcalloc(TCHAR, 100000);


	HANDLE h = CreateFile(udi->device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (h == INVALID_HANDLE_VALUE) {
		write_log(_T("CreateFile('%s') failed, err=%08x\n"), udi->device_path, GetLastError());
		goto end;
	}

	ini = ini_new();
	ini_addstring(ini, _T("GEOMETRY"), NULL, NULL);
	data = (uae_u8*)VirtualAlloc (NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
	inq[0] = 0xff;

	if (udi->BusType == BusTypeAta || udi->BusType == BusTypeSata || udi->BusType == BusTypeAtapi) {
		if (!_tcscmp(udi->vendor_id, _T("ATA"))) {
			satl = true;
		}
	}

	_stprintf(text, _T("BusType: 0x%02x\r\nVendor: '%s'\r\nProduct: '%s'\r\nRevision: '%s'\r\nSerial: '%s'\r\nSize: %llu\r\n"),
		 udi->BusType, udi->vendor_id, udi->product_id, udi->product_rev, udi->product_serial, udi->size);
	tptr = text + _tcslen(text);
	if (udi->BusType == BusTypeUsb && udi->usb_vid && udi->usb_pid) {
		_stprintf(tptr, _T("VID: %04x PID: %04x\r\n"), udi->usb_vid, udi->usb_pid);
		tptr += _tcslen(tptr);
	}
	_tcscpy(tptr, _T("\r\n"));
	tptr += _tcslen(tptr);

	write_log(_T("SATL=%d\n%s"), satl, text);

	if (satl || udi->BusType == BusTypeScsi || udi->BusType == BusTypeUsb || udi->BusType == BusTypeRAID) {
		write_log(_T("SCSI ATA passthrough\n"));
		if (!hd_get_meta_satl(hDlg, h, data, tptr, ini, &atapi)) {
			write_log(_T("SAT Passthrough failed\n"));
			memset(data, 0, 512);
			if (udi->BusType == BusTypeUsb) {
				hd_get_meta_hack(hDlg, h, data, inq, udi);
			}
			if (tptr[0] == 0) {
				_tcscpy(tptr, _T("\r\nSCSI ATA Passthrough error."));
				goto doout;
			}
		}
	} else if (udi->BusType == BusTypeAta || udi->BusType == BusTypeSata || udi->BusType == BusTypeAtapi) {
		write_log(_T("ATA passthrough\n"));
		if (!hd_get_meta_ata(h, udi->BusType == BusTypeAtapi, data)) {
			write_log(_T("ATA Passthrough failed\n"));
			_tcscpy(tptr, _T("\r\nATA Passthrough error."));
			goto doout;
		}
	} else {
		_stprintf(tptr, _T("Unsupported bus type 0x%02x\n"), udi->BusType);
		goto doout;
	}

	for (int i = 0; i < 512; i++) {
		if (data[i] != 0)
			empty = false;
	}

	if (empty) {
		write_log(_T("Nothing returned!\n"));
		if (tptr[0] == 0) {
			_tcscpy(tptr, _T("Nothing returned!"));
			goto doout;
		}
	}

	ata_byteswapidentity(data);

	if (!empty) {
		TCHAR cline[256];
		_tcscat (tptr, _T("IDENTITY:\r\n"));
		tptr += _tcslen(tptr);
		tptr = bintotextline(tptr, data, 512);
		_tcscpy(tptr, _T("\r\n"));
		tptr += _tcslen(tptr);

		bintotextpart(cline, data + 27 * 2, 40);
		ini_addnewcomment(ini, _T("IDENTITY"), cline);
		bintotextpart(cline, data + 23 * 2, 8);
		ini_addnewcomment(ini, _T("IDENTITY"), cline);
		bintotextpart(cline, data + 10 * 2, 20);
		ini_addnewcomment(ini, _T("IDENTITY"), cline);

		if (udi->BusType == BusTypeAtapi || atapi)
			ini_addnewstring(ini, _T("IDENTITY"), _T("ATAPI"), _T("true"));

		ini_addnewdata(ini, _T("IDENTITY"), _T("DATA"), data, 512);

		tptr = parse_identity(data, ini, tptr);
	}

doout:

	geometry_file[0] = 0;
	if (udi->vendor_id[0]) {
		_tcscat(geometry_file, udi->vendor_id);
	}
	if (udi->product_id[0]) {
		if (geometry_file[0] && geometry_file[_tcslen(geometry_file) - 1] != ' ')
			_tcscat(geometry_file, _T(" "));
		_tcscat(geometry_file, udi->product_id);
	}
	if (udi->product_rev[0]) {
		if (geometry_file[0] && geometry_file[_tcslen(geometry_file) - 1] != ' ')
			_tcscat(geometry_file, _T(" "));
		_tcscat(geometry_file, udi->product_rev);
	}
	if (udi->product_serial[0]) {
		if (geometry_file[0] && geometry_file[_tcslen(geometry_file) - 1] != ' ')
			_tcscat(geometry_file, _T(" "));
		_tcscat(geometry_file, udi->product_serial);
	}
	if (udi->size)
		_stprintf(geometry_file + _tcslen(geometry_file), _T(" %llX"), udi->size);
	if (geometry_file[0])
		_tcscat(geometry_file, _T(".geo"));
	makesafefilename(geometry_file, true);

	SAVECDS;
	hdini = ini;
	hwnd = CustomCreateDialog(IDD_DISKINFO, hDlg, StringBoxDialogProc, &cdstate);
	if (hwnd != NULL) {
		HFONT font = CreateFont (getscaledfontsize(-1, hDlg), 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Lucida Console"));
		if (font)
			SendMessage (GetDlgItem (hwnd, IDC_DISKINFOBOX), WM_SETFONT, WPARAM(font), FALSE);
		SendMessage (GetDlgItem (hwnd, IDC_DISKINFOBOX), WM_SETTEXT, 0, (LPARAM)text);
		while (cdstate.active) {
			MSG msg;
			int ret;
			WaitMessage ();
			while ((ret = GetMessage (&msg, NULL, 0, 0))) {
				if (ret == -1)
					break;
				if (!IsWindow (hwnd) || !IsDialogMessage (hwnd, &msg)) {
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
			}
		}
		if (font) {
			DeleteObject (font);
		}
	}
	RESTORECDS;

end:
	if (ini)
		ini_free(ini);

	if (data)
		VirtualFree(data, 0, MEM_RELEASE);

	if (h != INVALID_HANDLE_VALUE)
		CloseHandle(h);

	_tcscpy(geometryfile, geometry_file);

	xfree(text);
}

#define GUIDSTRINGLEN (8 + 1 + 4 + 1 + 4 + 1 + 4 + 1 + 12)


static bool getdeviceinfo(HANDLE hDevice, struct uae_driveinfo *udi)
{
	DISK_GEOMETRY dg;
	GET_LENGTH_INFORMATION gli;
	DWORD returnedLength;
	bool geom_ok = true, gli_ok;
	UCHAR outBuf[20000];
	DRIVE_LAYOUT_INFORMATION_EX *dli;
	STORAGE_PROPERTY_QUERY query;
	DWORD status;
	TCHAR devname[MAX_DPATH];
	int amipart = -1;
	TCHAR amigaguids[1 + GUIDSTRINGLEN + 1 + 1];
	GUID amigaguid = { 0 };

	udi->bytespersector = 512;

	_tcscpy (devname, udi->device_name + 1);

	TCHAR *n = udi->device_name;
	if (n[0] == ':' && n[1] == 'P' && n[2] == '#' &&
		(n[4] == '_' || n[5] == '_')) {
		TCHAR c1 = n[3];
		TCHAR c2 = n[4];
		if (c1 >= '0' && c1 <= '9') {
			amipart = c1 - '0';
			if (c2 != '_') {
				if (c2 >= '0' && c2 <= '9') {
					amipart *= 10;
					amipart += c2 - '0';
					_tcscpy (devname, udi->device_name + 6);
				} else {
					amipart = -1;
				}
			} else {
				_tcscpy (devname, udi->device_name + 5);
			}
		}
	}
	if (n[0] == ':' && n[1] == 'G' && n[2] == 'P' && n[3] == '#' && n[4] == '{' && _tcslen(n) >= 4 + 1 + GUIDSTRINGLEN + 1 && n[5 + 1 + GUIDSTRINGLEN] == '}' && n[5 + 1 + GUIDSTRINGLEN + 1] == '_') {
		memcpy(amigaguids, n + 4, (1 + GUIDSTRINGLEN + 1) * sizeof(TCHAR));
		amigaguids[(1 + GUIDSTRINGLEN + 1) - 1] = 0;
		guidfromstring(amigaguids, &amigaguid);
		_tcscpy(devname, n + 4 + 1 + GUIDSTRINGLEN + 1 + 1);
	}

	//queryidentifydevice(hDevice);

	udi->device_name[0] = 0;
	memset (outBuf, 0, sizeof outBuf);
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	status = DeviceIoControl(
		hDevice,
		IOCTL_STORAGE_QUERY_PROPERTY,
		&query,
		sizeof (STORAGE_PROPERTY_QUERY),
		&outBuf,
		sizeof outBuf,
		&returnedLength,
		NULL);
	if (status) {
		if (getstorageproperty (outBuf, returnedLength, udi, false) != -1)
			return false;
	} else {
		return false;
	}

	if (_tcsicmp (devname, udi->device_name) != 0) {
		write_log (_T("Non-enumeration mount: mismatched device names\n"));
		return false;
	}

	STORAGE_DEVICE_NUMBER sdn;
	if (DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &returnedLength, NULL)) {
		getstorageinfo(udi, sdn);
	}
	readidentity(INVALID_HANDLE_VALUE, udi, NULL);

	gli_ok = true;
	gli.Length.QuadPart = 0;
	if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, (void*)& gli, sizeof(gli), &returnedLength, NULL)) {
		gli_ok = false;
		write_log(_T("IOCTL_DISK_GET_LENGTH_INFO failed with error code %d.\n"), GetLastError());
	}
	else {
		write_log(_T("IOCTL_DISK_GET_LENGTH_INFO returned size: %I64d (0x%I64x)\n"), gli.Length.QuadPart, gli.Length.QuadPart);
	}

	if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, (void*)&dg, sizeof (dg), &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (isnomediaerr (err)) {
			write_log(_T("IOCTL_DISK_GET_DRIVE_GEOMETRY no disk, error code %d.\n"), err);
			udi->nomedia = 1;
			return true;
		}
		write_log (_T("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with error code %d.\n"), err);
		geom_ok = false;
	}
	if (!DeviceIoControl (hDevice, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &returnedLength, NULL)) {
		DWORD err = GetLastError ();
		if (err == ERROR_WRITE_PROTECT)
			udi->readonly = 1;
	}

	if (ischs(udi->identity) && gli.Length.QuadPart == 0) {
		int c, h, s;
		tochs(udi->identity, -1, &c, &h, &s);
		gli.Length.QuadPart = udi->size = c * h * s * 512;
		udi->cylinders = c;
		udi->heads = h;
		udi->sectors = s;
		geom_ok = true;
	}

	if (geom_ok == 0 && gli_ok == 0) {
		write_log (_T("Can't detect size of device\n"));
		return false;
	}
	if (geom_ok && dg.BytesPerSector != udi->bytespersector)
		return false;
	udi->size = gli.Length.QuadPart;

	// check for amithlon partitions, if any found = quick mount not possible
	status = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
		&outBuf, sizeof (outBuf), &returnedLength, NULL);
	if (!status)
		return true;
	dli = (DRIVE_LAYOUT_INFORMATION_EX*)outBuf;
	if (!dli->PartitionCount)
		return true;
	if (dli->PartitionStyle == PARTITION_STYLE_MBR) {
		for (int i = 0; i < dli->PartitionCount; i++) {
			PARTITION_INFORMATION_EX *pi = &dli->PartitionEntry[i];
			if (pi->Mbr.PartitionType == PARTITION_ENTRY_UNUSED)
				continue;
			if (pi->Mbr.RecognizedPartition == 0)
				continue;
			if (i + 1 == amipart) {
				udi->offset = pi->StartingOffset.QuadPart;
				udi->size = pi->PartitionLength.QuadPart;
				return false;
			}
		}
	} else if (dli->PartitionStyle == PARTITION_STYLE_GPT) {
		for (int i = 0; i < dli->PartitionCount; i++) {
			PARTITION_INFORMATION_EX *pi = &dli->PartitionEntry[i];
			if (!memcmp(&pi->Gpt.PartitionType, &PARTITION_GPT_AMIGA, sizeof(GUID)) &&
				!memcmp(&pi->Gpt.PartitionId, &amigaguid, sizeof(GUID))) {
				udi->offset = pi->StartingOffset.QuadPart;
				udi->size = pi->PartitionLength.QuadPart;
				return false;
			}
		}
	}
	return true;
}

static void lock_drive(struct hardfiledata *hfd, const TCHAR *name, HANDLE drvhandle)
{
	DWORD written;
	TCHAR volname[MAX_DPATH];
	DWORD sign, pstyle;
	bool ntfs_found = false;

	if (!hfd->ci.lock)
		return;
	if (hfd->flags & HFD_FLAGS_REALDRIVEPARTITION)
		return;

	// single partition FAT drives seem to lock this way, without need for administrator privileges
	if (DeviceIoControl(drvhandle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &written, NULL)) {
		if (DeviceIoControl(drvhandle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &written, NULL)) {
			write_log(_T("'%s' locked and dismounted successfully.\n"), name);
			hfd->handle->dismounted = true;
		}
	}

	if (!getsignfromhandle (drvhandle, &sign, &pstyle))
		return;
	HANDLE h = FindFirstVolume (volname, sizeof volname / sizeof (TCHAR));
	while (h != INVALID_HANDLE_VALUE) {
		bool isntfs = false;
		if (volname[_tcslen (volname) - 1] == '\\')
			volname[_tcslen (volname) - 1] = 0;
		HANDLE d = CreateFile (volname, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (d != INVALID_HANDLE_VALUE) {
			if (DeviceIoControl (d, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &written, NULL)) {
				VOLUME_DISK_EXTENTS *vde;
				NTFS_VOLUME_DATA_BUFFER ntfs;
				if (DeviceIoControl (d, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfs, sizeof ntfs, &written, NULL)) {
					isntfs = true;
				}
				DWORD outsize = sizeof (VOLUME_DISK_EXTENTS) + sizeof (DISK_EXTENT) * 32;
				vde = (VOLUME_DISK_EXTENTS*)xmalloc (uae_u8, outsize);
				if (DeviceIoControl (d, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
					for (int i = 0; i < vde->NumberOfDiskExtents; i++) {
						int mounted = 0;
						TCHAR pdrv[MAX_DPATH];
						_stprintf (pdrv, _T("\\\\.\\PhysicalDrive%d"), vde->Extents[i].DiskNumber);
						HANDLE ph = CreateFile (pdrv, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						if (ph != INVALID_HANDLE_VALUE) {
							DWORD sign2, pstyle2;
							if (getsignfromhandle (ph, &sign2, &pstyle2)) {
								if (sign == sign2 && pstyle == PARTITION_STYLE_MBR) {
									if (isntfs)
										ntfs_found = true;
									mounted = isntfs ? -1 : 1;
								}
							}
							CloseHandle(ph);
							if (mounted > 0) {
								for (int j = 0; j < MAX_LOCKED_VOLUMES; j++) {
									if (hfd->handle->locked_volumes[j] == INVALID_HANDLE_VALUE) {
										write_log(_T("%d: Partition found: PhysicalDrive%d: Extent %d Start=%I64X Len=%I64X\n"), i,
											vde->Extents[i].DiskNumber, j, vde->Extents[i].StartingOffset.QuadPart, vde->Extents[i].ExtentLength.QuadPart);
										hfd->handle->locked_volumes[j] = d;
										d = INVALID_HANDLE_VALUE;
										break;
									}
								}
							}
						}
					}
				} else {
					write_log (_T("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned %08x\n"), GetLastError ());
				}
			}
			if (d != INVALID_HANDLE_VALUE)
				CloseHandle (d);
		}
		if (!FindNextVolume (h, volname, sizeof volname / sizeof (TCHAR)))
			break;
	}
	FindVolumeClose(h);

	if (ntfs_found) {
		write_log(_T("Not locked: At least one NTFS partition detected.\n"));
	}

	for (int i = 0; i < MAX_LOCKED_VOLUMES; i++) {
		HANDLE d = hfd->handle->locked_volumes[i];
		if (d != INVALID_HANDLE_VALUE) {
			if (ntfs_found) {
				CloseHandle(d);
				hfd->handle->locked_volumes[i] = INVALID_HANDLE_VALUE;
			} else {
				if (DeviceIoControl(d, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &written, NULL)) {
					if (DeviceIoControl(d, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &written, NULL)) {
						write_log(_T("ID=%d locked and dismounted successfully.\n"), i, name);
					} else {
						write_log (_T("WARNING: ID=%d FSCTL_DISMOUNT_VOLUME returned %d\n"), i, GetLastError());
					}
				} else {
					write_log (_T("WARNING: ID=%d FSCTL_LOCK_VOLUME returned %d\n"), i, GetLastError());
				}
			}
		}
	}
}

int hdf_open_target (struct hardfiledata *hfd, const TCHAR *pname)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD flags;
	int i;
	struct uae_driveinfo *udi = NULL, tmpudi;
	TCHAR *name = my_strdup (pname);
	int ret = 0;

	hfd->flags = 0;
	hfd->drive_empty = 0;
	hdf_close (hfd);
	hfd->cache = (uae_u8*)VirtualAlloc (NULL, CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	hfd->cache_valid = 0;
	if (!hfd->cache) {
		write_log (_T("VirtualAlloc(%d) failed, error %d\n"), CACHE_SIZE, GetLastError ());
		goto end;
	}
	hfd->handle = xcalloc (struct hardfilehandle, 1);
	for(int i = 0; i < MAX_LOCKED_VOLUMES; i++) {
		hfd->handle->locked_volumes[i] = INVALID_HANDLE_VALUE;
	}
	hfd->handle->h = INVALID_HANDLE_VALUE;
	hfd_log (_T("hfd attempting to open: '%s'\n"), name);
	if (name[0] == ':') {
		DWORD rw = GENERIC_READ;
		DWORD srw = FILE_SHARE_READ;
		int drvnum = -1;
		TCHAR *p = _tcschr (name + 1, ':');
		if (p) {
			// open partitions in shared read/write mode
			if (name[0] ==':' && name[1] == 'P') {
				rw |= GENERIC_WRITE;
				srw |= FILE_SHARE_WRITE;
			}
			*p++ = 0;
			// do not scan for drives if open succeeds and it is a harddrive
			// to prevent spinup of sleeping drives
			h = CreateFile (p, rw, srw,
				NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
			DWORD err = GetLastError ();
			if (h == INVALID_HANDLE_VALUE && err == ERROR_FILE_NOT_FOUND) {
				if (!drives_enumerated)
					goto emptyreal;
			}
		}
		if (h != INVALID_HANDLE_VALUE) {
			udi = &tmpudi;
			memset (udi, 0, sizeof (struct uae_driveinfo));
			_tcscpy (udi->device_full_path, name);
			_tcscat (udi->device_full_path, _T(":"));
			_tcscat (udi->device_full_path, p);
			_tcscpy (udi->device_name, name);
			_tcscpy (udi->device_path, p);
			if (!getdeviceinfo (h, udi))
				udi = NULL;
			CloseHandle (h);
			h = INVALID_HANDLE_VALUE;
		}
		if (udi == NULL) {
			if (!drives_enumerated) {
				write_log (_T("Enumerating drives..\n"));
				hdf_init_target ();
				write_log (_T("Enumeration end..\n"));
			}
			drvnum = isharddrive (name);
			if (drvnum >= 0)
				udi = &uae_drives[drvnum];
		}
		if (udi != NULL) {
			bool chs = udi->chsdetected;
			hfd->flags = HFD_FLAGS_REALDRIVE;
			if (udi) {
				if (udi->nomedia)
					hfd->drive_empty = -1;
				if (udi->readonly)
					hfd->ci.readonly = 1;
			}
			readidentity(INVALID_HANDLE_VALUE, udi, hfd);

			flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
			h = CreateFile (udi->device_path,
				rw | (hfd->ci.readonly && !chs ? 0 : GENERIC_WRITE),
				srw | (hfd->ci.readonly && !chs ? 0 : FILE_SHARE_WRITE),
				NULL, OPEN_EXISTING, flags, NULL);
			hfd->handle->h = h;
			if (h == INVALID_HANDLE_VALUE && !hfd->ci.readonly) {
				DWORD err = GetLastError();
				write_log(_T("Real HD open (RW) error: %d\n"), err);
				if (err == ERROR_WRITE_PROTECT || err == ERROR_SHARING_VIOLATION) {
					h = CreateFile (udi->device_path,
						GENERIC_READ,
						FILE_SHARE_READ,
						NULL, OPEN_EXISTING, flags, NULL);
					if (h != INVALID_HANDLE_VALUE) {
						hfd->ci.readonly = true;
						write_log(_T("Real HD open succeeded in read-only mode\n"));
					}
				}
			}

			if (h == INVALID_HANDLE_VALUE) {
				DWORD err = GetLastError ();
				write_log(_T("Real HD open error: %d\n"), err);
				if (err == ERROR_WRITE_PROTECT)
					ret = -2;
				if (err == ERROR_SHARING_VIOLATION)
					ret = -1;
				goto end;
			}

			_tcsncpy (hfd->vendor_id, udi->vendor_id, 8);
			_tcsncpy (hfd->product_id, udi->product_id, 16);
			_tcsncpy (hfd->product_rev, udi->product_rev, 4);
			hfd->offset = udi->offset;
			hfd->physsize = hfd->virtsize = udi->size;
			hfd->ci.blocksize = udi->bytespersector;
			if (udi->partitiondrive)
				hfd->flags |= HFD_FLAGS_REALDRIVEPARTITION;
			if (hfd->offset == 0 && !hfd->drive_empty) {
				int sf = safetycheck (hfd->handle->h, udi->device_path, 0, hfd->cache, hfd->ci.blocksize, hfd->identity, udi->chsdetected, NULL);
				if (sf > 0)
					goto end;
				if (sf == 0 && !hfd->ci.readonly && harddrive_dangerous != 0x1234dead) {
					write_log (_T("'%s' forced read-only, safetycheck enabled\n"), udi->device_path);
					hfd->dangerous = 1;
					// clear GENERIC_WRITE
					CloseHandle (h);
					h = CreateFile (udi->device_path,
						GENERIC_READ,
						FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, flags, NULL);
					hfd->handle->h = h;
					if (h == INVALID_HANDLE_VALUE)
						goto end;
				}

#if 0
				if (sf == 0 && hfd->warned >= 0) {
					if (harddrive_dangerous != 0x1234dead) {
						if (!hfd->warned)
							gui_message_id (IDS_HARDDRIVESAFETYWARNING1);
						hfd->warned = 1;
						goto end;
					}
					if (!hfd->warned) {
						gui_message_id (IDS_HARDDRIVESAFETYWARNING2);
						hfd->warned = 1;
					}
				}
			} else {
				hfd->warned = -1;
#endif
			}

			lock_drive(hfd, name, h);

			hfd->handle_valid = HDF_HANDLE_WIN32_NORMAL;
			
			if (chs) {
				hfd->handle_valid = HDF_HANDLE_WIN32_CHS;
				hfd->ci.chs = true;
				tochs(hfd->identity, -1, &hfd->ci.pcyls, &hfd->ci.pheads, &hfd->ci.psecs);
			} else {
				hfd->ci.chs = false;
			}
			
			hfd->emptyname = my_strdup (name);

			//getstorageproperty_ataidentity(h);
			//queryataidentity(h);
			//scsidirect(h);
			//fixdrive (hfd);

		} else {
emptyreal:
			hfd->flags = HFD_FLAGS_REALDRIVE;
			hfd->drive_empty = -1;
			hfd->emptyname = my_strdup (name);
		}
	} else {
		int zmode = 0;
		TCHAR *ext = _tcsrchr (name, '.');
		if (ext != NULL) {
			ext++;
			for (i = 0; hdz[i]; i++) {
				if (!_tcsicmp (ext, hdz[i]))
					zmode = 1;
			}
		}
		h = CreateFile (name, GENERIC_READ | (hfd->ci.readonly ? 0 : GENERIC_WRITE), hfd->ci.readonly ? FILE_SHARE_READ : 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
		if (h == INVALID_HANDLE_VALUE && !hfd->ci.readonly) {
			DWORD err = GetLastError ();
			if (err == ERROR_WRITE_PROTECT || err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION) {
				h = CreateFile (name, GENERIC_READ, FILE_SHARE_READ, NULL,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
				if (h != INVALID_HANDLE_VALUE)
					hfd->ci.readonly = true;
			}
		}
		
		hfd->handle->h = h;
		i = uaetcslen(name) - 1;
		while (i >= 0) {
			if ((i > 0 && (name[i - 1] == '/' || name[i - 1] == '\\')) || i == 0) {
				_tcsncpy (hfd->product_id, name + i, 15);
				break;
			}
			i--;
		}
		_tcscpy (hfd->vendor_id, _T("UAE"));
		_tcscpy (hfd->product_rev, _T("0.4"));
		if (h != INVALID_HANDLE_VALUE) {
			DWORD ret, low;
			LONG high = 0;
			DWORD high2;
			ret = SetFilePointer (h, 0, &high, FILE_END);
			if (ret == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
				goto end;
			low = GetFileSize (h, &high2);
			if (low == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
				goto end;
			low &= ~(hfd->ci.blocksize - 1);
			hfd->physsize = hfd->virtsize = ((uae_u64)high2 << 32) | low;
			if (hfd->physsize < hfd->ci.blocksize || hfd->physsize == 0) {
				write_log (_T("HDF '%s' is too small\n"), name);
				goto end;
			}
			hfd->handle_valid = HDF_HANDLE_WIN32_NORMAL;
			if (hfd->physsize < 64 * 1024 * 1024 && zmode) {
				write_log (_T("HDF '%s' re-opened in zfile-mode\n"), name);
				CloseHandle (h);
				hfd->handle->h = INVALID_HANDLE_VALUE;
				hfd->handle->zf = zfile_fopen (name, _T("rb"), ZFD_NORMAL);
				hfd->handle->zfile = 1;
				if (!hfd->handle->zf)
					goto end;
				zfile_fseek (hfd->handle->zf, 0, SEEK_END);
				hfd->physsize = hfd->virtsize = zfile_ftell (hfd->handle->zf);
				zfile_fseek (hfd->handle->zf, 0, SEEK_SET);
				hfd->handle_valid = HDF_HANDLE_ZFILE;
			}
		} else {
			DWORD err = GetLastError ();
			if (err == ERROR_WRITE_PROTECT)
				ret = -2;
			if (err == ERROR_SHARING_VIOLATION)
				ret = -1;
			write_log (_T("HDF '%s' failed to open. error = %d\n"), name, ret);
		}
	}
	if (hfd->handle_valid || hfd->drive_empty) {
		hfd_log (_T("HDF '%s' %p opened, size=%dK mode=%d empty=%d\n"),
			name, hfd, (int)(hfd->physsize / 1024), hfd->handle_valid, hfd->drive_empty);
		return 1;
	}
end:
	hdf_close (hfd);
	xfree (name);
	return ret;
}

static void freehandle (struct hardfilehandle *h)
{
	if (!h)
		return;
	for (int i = 0; i < MAX_LOCKED_VOLUMES; i++) {
		if (h->locked_volumes[i] != INVALID_HANDLE_VALUE) {
			CloseHandle(h->locked_volumes[i]);
		}
	}
	if (!h->zfile && h->h != INVALID_HANDLE_VALUE)
		CloseHandle(h->h);
	if (h->zfile && h->zf)
		zfile_fclose (h->zf);
	h->zf = NULL;
	h->h = INVALID_HANDLE_VALUE;
	h->zfile = 0;
}

HANDLE hdf_get_real_handle(struct hardfilehandle *h)
{
	return h->h;
}

void hdf_close_target (struct hardfiledata *hfd)
{
	freehandle (hfd->handle);
	xfree (hfd->handle);
	xfree (hfd->emptyname);
	hfd->emptyname = NULL;
	hfd->handle = NULL;
	hfd->handle_valid = 0;
	if (hfd->cache)
		VirtualFree (hfd->cache, 0, MEM_RELEASE);
	hfd->cache = 0;
	hfd->cache_valid = 0;
	hfd->drive_empty = 0;
	hfd->dangerous = 0;
}

int hdf_dup_target (struct hardfiledata *dhfd, const struct hardfiledata *shfd)
{
	if (!shfd->handle_valid)
		return 0;
	freehandle (dhfd->handle);
	if (shfd->handle_valid == HDF_HANDLE_WIN32_NORMAL || shfd->handle_valid == HDF_HANDLE_WIN32_CHS) {
		HANDLE duphandle;
		if (!DuplicateHandle (GetCurrentProcess (), shfd->handle->h, GetCurrentProcess () , &duphandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
			return 0;
		dhfd->handle->h = duphandle;
		dhfd->handle_valid = shfd->handle_valid;
	} else if (shfd->handle_valid == HDF_HANDLE_ZFILE) {
		struct zfile *zf;
		zf = zfile_dup (shfd->handle->zf);
		if (!zf)
			return 0;
		dhfd->handle->zf = zf;
		dhfd->handle->zfile = 1;
		dhfd->handle_valid = HDF_HANDLE_ZFILE;
	}
	dhfd->cache = (uae_u8*)VirtualAlloc (NULL, CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	dhfd->cache_valid = 0;
	if (!dhfd->cache) {
		hdf_close (dhfd);
		return 0;
	}
	return 1;
}

static int hdf_seek (struct hardfiledata *hfd, uae_u64 offset, bool write)
{
	DWORD ret;

	if (hfd->handle_valid == 0) {
		gui_message (_T("hd: hdf handle is not valid. bug."));
		abort();
	}
	if (hfd->physsize) {
		if (offset >= hfd->physsize - hfd->virtual_size) {
			if (hfd->virtual_rdb)
				return -1;
			if (write) {
				gui_message (_T("hd: tried to seek out of bounds! (%I64X >= %I64X - %I64X)\n"), offset, hfd->physsize, hfd->virtual_size);
				abort ();
			}
			write_log(_T("hd: tried to seek out of bounds! (%I64X >= %I64X - %I64X)\n"), offset, hfd->physsize, hfd->virtual_size);
			return -1;
		}
		offset += hfd->offset;
		if (offset & (hfd->ci.blocksize - 1)) {
			if (write) {
				gui_message (_T("hd: poscheck failed, offset=%I64X not aligned to blocksize=%d! (%I64X & %04X = %04X)\n"),
					offset, hfd->ci.blocksize, offset, hfd->ci.blocksize, offset & (hfd->ci.blocksize - 1));
				abort ();
			}
			write_log(_T("hd: poscheck failed, offset=%I64X not aligned to blocksize=%d! (%I64X & %04X = %04X)\n"),
				offset, hfd->ci.blocksize, offset, hfd->ci.blocksize, offset & (hfd->ci.blocksize - 1));
			return -1;
		}
	}
	if (hfd->handle_valid == HDF_HANDLE_WIN32_NORMAL) {
		LARGE_INTEGER fppos;
		fppos.QuadPart = offset;
		ret = SetFilePointer(hfd->handle->h, fppos.LowPart, &fppos.HighPart, FILE_BEGIN);
		if (ret == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
			return -1;
	} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
		zfile_fseek (hfd->handle->zf, (long)offset, SEEK_SET);
	}
	return 0;
}

static void poscheck (struct hardfiledata *hfd, int len)
{
	DWORD err;
	uae_s64 pos = -1;

	if (hfd->handle_valid == HDF_HANDLE_WIN32_NORMAL) {
		LARGE_INTEGER fppos;
		fppos.QuadPart = 0;
		fppos.LowPart = SetFilePointer (hfd->handle->h, 0, &fppos.HighPart, FILE_CURRENT);
		if (fppos.LowPart == INVALID_SET_FILE_POINTER) {
			err = GetLastError ();
			if (err != NO_ERROR) {
				gui_message (_T("hd: poscheck failed. seek failure, error %d"), err);
				abort ();
			}
		}
		pos = fppos.QuadPart;
	} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
		pos = zfile_ftell (hfd->handle->zf);
	} else if (hfd->handle_valid == HDF_HANDLE_WIN32_CHS) {
		pos = 0;
	}
	if (len < 0) {
		gui_message (_T("hd: poscheck failed, negative length! (%d)"), len);
		abort ();
	}
	if (pos < hfd->offset) {
		gui_message (_T("hd: poscheck failed, offset out of bounds! (%I64d < %I64d)"), pos, hfd->offset);
		abort ();
	}
	if (pos >= hfd->offset + hfd->physsize - hfd->virtual_size || pos >= hfd->offset + hfd->physsize + len - hfd->virtual_size) {
		gui_message (_T("hd: poscheck failed, offset out of bounds! (%I64d >= %I64d, LEN=%d)"), pos, hfd->offset + hfd->physsize, len);
		abort ();
	}
	if (pos & (hfd->ci.blocksize - 1)) {
		gui_message (_T("hd: poscheck failed, offset not aligned to blocksize! (%I64X & %04X = %04X\n"), pos, hfd->ci.blocksize, pos & hfd->ci.blocksize);
		abort ();
	}
}

static int isincache (struct hardfiledata *hfd, uae_u64 offset, int len)
{
	if (!hfd->cache_valid)
		return -1;
	if (offset >= hfd->cache_offset && offset + len <= hfd->cache_offset + CACHE_SIZE)
		return (int)(offset - hfd->cache_offset);
	return -1;
}

#if 0
void hfd_flush_cache (struct hardfiledata *hfd, int now)
{
	DWORD outlen = 0;
	if (!hfd->cache_needs_flush || !hfd->cache_valid)
		return;
	if (now || time (NULL) > hfd->cache_needs_flush + CACHE_FLUSH_TIME) {
		hdf_log ("flushed %d %d %d\n", now, time(NULL), hfd->cache_needs_flush);
		hdf_seek (hfd, hfd->cache_offset);
		poscheck (hfd, CACHE_SIZE);
		WriteFile (hfd->handle, hfd->cache, CACHE_SIZE, &outlen, NULL);
		hfd->cache_needs_flush = 0;
	}
}
#endif

#if 0

static int hdf_rw (struct hardfiledata *hfd, void *bufferp, uae_u64 offset, int len, int dowrite)
{
	DWORD outlen = 0, outlen2;
	uae_u8 *buffer = bufferp;
	int soff, size, mask, bs;

	bs = hfd->ci.blocksize;
	mask = hfd->ci.blocksize - 1;
	hfd->cache_valid = 0;
	if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
		if (dowrite)
			outlen = zfile_fwrite (buffer, len, 1, hfd->handle);
		else
			outlen = zfile_fread (buffer, len, 1, hfd->handle);
	} else {
		soff = offset & mask;
		if (soff > 0) { /* offset not aligned to blocksize */
			size = bs - soff;
			if (size > len)
				size = len;
			hdf_seek (hfd, offset & ~mask, dowrite != 0);
			poscheck (hfd, len);
			if (dowrite)
				WriteFile (hfd->handle, hfd->cache, bs, &outlen2, NULL);
			else
				ReadFile (hfd->handle, hfd->cache, bs, &outlen2, NULL);
			if (outlen2 != hfd->ci.blocksize)
				goto end;
			outlen += size;
			memcpy (buffer, hfd->cache + soff,  size);
			buffer += size;
			offset += size;
			len -= size;
		}
		while (len >= bs) { /* aligned access */
			hdf_seek (hfd, offset, dowrite != 0);
			poscheck (hfd, len);
			size = len & ~mask;
			if (size > CACHE_SIZE)
				size = CACHE_SIZE;
			if (dowrite) {
				WriteFile (hfd->handle, hfd->cache, size, &outlen2, NULL);
			} else {
				int coff = isincache(hfd, offset, size);
				if (coff >= 0) {
					memcpy (buffer, hfd->cache + coff, size);
					outlen2 = size;
				} else {
					ReadFile (hfd->handle, hfd->cache, size, &outlen2, NULL);
					if (outlen2 == size)
						memcpy (buffer, hfd->cache, size);
				}
			}
			if (outlen2 != size)
				goto end;
			outlen += outlen2;
			buffer += size;
			offset += size;
			len -= size;
		}
		if (len > 0) { /* len > 0 && len < bs */
			hdf_seek (hfd, offset, dowrite != 0);
			poscheck (hfd, len);
			if (dowrite)
				WriteFile (hfd->handle, hfd->cache, bs, &outlen2, NULL);
			else
				ReadFile (hfd->handle, hfd->cache, bs, &outlen2, NULL);
			if (outlen2 != bs)
				goto end;
			outlen += len;
			memcpy (buffer, hfd->cache, len);
		}
	}
end:
	return outlen;
}

int hdf_read (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
	return hdf_rw (hfd, buffer, offset, len, 0);
}
int hdf_write (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
	return hdf_rw (hfd, buffer, offset, len, 1);
}

#else

static int hdf_read_2(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len, uae_u32 *error)
{
	DWORD outlen = 0;
	int coffset;

	if (len == 0) {
		return 0;
	}
	if (offset == 0) {
		hfd->cache_valid = 0;
	}
	coffset = isincache (hfd, offset, len);
	if (coffset >= 0) {
		memcpy (buffer, hfd->cache + coffset, len);
		return len;
	}
	hfd->cache_offset = offset;
	if (offset + CACHE_SIZE > hfd->offset + (hfd->physsize - hfd->virtual_size))
		hfd->cache_offset = hfd->offset + (hfd->physsize - hfd->virtual_size) - CACHE_SIZE;
	if (hdf_seek(hfd, hfd->cache_offset, false)) {
		*error = 45;
		return 0;
	}
	poscheck (hfd, CACHE_SIZE);
	if (hfd->handle_valid == HDF_HANDLE_WIN32_NORMAL) {
		HD_ReadFile(hfd->handle->h, hfd->cache, CACHE_SIZE, &outlen, hfd->cache_offset);
	} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
		outlen = (DWORD)zfile_fread(hfd->cache, 1, CACHE_SIZE, hfd->handle->zf);
	}
	hfd->cache_valid = 0;
	if (outlen != CACHE_SIZE) {
		*error = 45;
		return 0;
	}
	hfd->cache_valid = 1;
	coffset = isincache (hfd, offset, len);
	if (coffset >= 0) {
		memcpy (buffer, hfd->cache + coffset, len);
		return len;
	}
	write_log (_T("hdf_read: cache bug! offset=%I64d len=%d\n"), offset, len);
	hfd->cache_valid = 0;
	*error = 45;
	return 0;
}

int hdf_read_target(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len, uae_u32 *error)
{
	int got = 0;
	uae_u8 *p = (uae_u8*)buffer;
	uae_u32 error2 = 0;

	if (error) {
		*error = 0;
	} else {
		error = &error2;
	}

	if (hfd->drive_empty) {
		*error = 29;
		return 0;
	}

	if (hfd->handle_valid == HDF_HANDLE_WIN32_CHS) {
		int len2 = len;
		while (len > 0) {
			int c, h, s;
			tochs(hfd->identity, offset, &c, &h, &s);
			do_scsi_read10_chs(hfd->handle->h, -1, c, h, s, (uae_u8*)buffer, 1, &hfd->specialaccessmode, false);
			len -= 512;
		}
		return len2;
	}

	while (len > 0) {
		int maxlen;
		DWORD ret;
		if (hfd->physsize < CACHE_SIZE) {
			hfd->cache_valid = 0;
			if (hdf_seek(hfd, offset, false))
				return got;
			if (hfd->physsize)
				poscheck (hfd, len);
			if (hfd->handle_valid == HDF_HANDLE_WIN32_NORMAL) {
				HD_ReadFile(hfd->handle->h, hfd->cache, len, &ret, offset);
				memcpy (buffer, hfd->cache, ret);
			} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
				ret = (DWORD)zfile_fread (buffer, 1, len, hfd->handle->zf);
			}
			maxlen = len;
		} else {
			maxlen = len > CACHE_SIZE ? CACHE_SIZE : len;
			ret = hdf_read_2 (hfd, p, offset, maxlen, error);
		}
		got += ret;
		if (ret != maxlen)
			return got;
		offset += maxlen;
		p += maxlen;
		len -= maxlen;
	}
	return got;
}

static int hdf_write_2(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len, uae_u32 *error)
{
	DWORD outlen = 0;

	if (hfd->dangerous) {
		*error = 28;
#ifdef RETROPLATFORM
		rp_drive_access_error(3);
#endif
		return 0;
	}
	if (hfd->ci.readonly) {
		*error = 28;
#ifdef RETROPLATFORM
		rp_drive_access_error(4);
#endif
		return 0;
	}
	if (len == 0)
		return 0;

	hfd->cache_valid = 0;
	if (hdf_seek(hfd, offset, true)) {
		*error = 45;
#ifdef RETROPLATFORM
		rp_drive_access_error(2);
#endif
		return 0;
	}
	poscheck (hfd, len);
	memcpy (hfd->cache, buffer, len);
	if (hfd->handle_valid == HDF_HANDLE_WIN32_NORMAL) {
		const TCHAR *name = hfd->emptyname == NULL ? _T("<unknown>") : hfd->emptyname;
		if (offset == 0) {
			if (!hfd->handle->firstwrite && (hfd->flags & HFD_FLAGS_REALDRIVE) && !(hfd->flags & HFD_FLAGS_REALDRIVEPARTITION)) {
				hfd->handle->firstwrite = true;
				int ptype;
				if (ismounted (hfd->ci.devname, hfd->handle->h, &ptype)) {
					gui_message (_T("\"%s\"\n\nBlock zero write attempt but drive has one or more mounted PC partitions or WinUAE does not have Administrator privileges. Erase the drive or unmount all PC partitions first."), name);
					hfd->ci.readonly = true;
					*error = 45;
#ifdef RETROPLATFORM
					rp_drive_access_error(0);
#endif
					return 0;
				}
			}
		}
		HD_WriteFile(hfd->handle->h, hfd->cache, len, &outlen, offset);
		if (outlen != len) {
			*error = 45;
#ifdef RETROPLATFORM
			rp_drive_access_error(5);
#endif
		}
		if (offset == 0) {
			DWORD err = GetLastError();
			DWORD outlen2;
			uae_u8 *tmp;
			int tmplen = 512;
			tmp = (uae_u8*)VirtualAlloc (NULL, tmplen, MEM_COMMIT, PAGE_READWRITE);
			if (tmp) {
				int cmplen = tmplen > len ? len : tmplen;
				memset (tmp, 0xa1, tmplen);
				hdf_seek (hfd, offset, true);
				HD_ReadFile(hfd->handle->h, tmp, tmplen, &outlen2, offset);
				if (memcmp (hfd->cache, tmp, cmplen) != 0 || outlen != len) {
					gui_message (_T("\"%s\"\n\nblock zero write failed! Make sure WinUAE has Windows Administrator privileges. Error=%d"), name, err);
					*error = 45;
#ifdef RETROPLATFORM
					rp_drive_access_error(1);
#endif
				}
				VirtualFree (tmp, 0, MEM_RELEASE);
			}
		}
	} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
		outlen = (DWORD)zfile_fwrite (hfd->cache, 1, len, hfd->handle->zf);
	}
	return outlen;
}

int hdf_write_target(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len, uae_u32 *error)
{
	int got = 0;
	uae_u8 *p = (uae_u8*)buffer;
	uae_u32 error2 = 0;

	if (error) {
		*error = 0;
	} else {
		error = &error2;
	}

	if (hfd->handle_valid == HDF_HANDLE_WIN32_CHS)
		return 0;
	if (hfd->drive_empty || hfd->physsize == 0)
		return 0;

	while (len > 0) {
		int maxlen = len > CACHE_SIZE ? CACHE_SIZE : len;
		int ret = hdf_write_2(hfd, p, offset, maxlen, error);
		if (ret < 0)
			return ret;
		got += ret;
		if (ret != maxlen)
			return got;
		offset += maxlen;
		p += maxlen;
		len -= maxlen;
	}
	return got;
}

int hdf_resize_target (struct hardfiledata *hfd, uae_u64 newsize)
{
	LONG highword = 0;
	DWORD ret, err;

	if (newsize >= 0x80000000) {
		highword = (DWORD)(newsize >> 32);
		ret = SetFilePointer (hfd->handle->h, (DWORD)newsize, &highword, FILE_BEGIN);
	} else {
		ret = SetFilePointer (hfd->handle->h, (DWORD)newsize, NULL, FILE_BEGIN);
	}
	err = GetLastError ();
	if (ret == INVALID_SET_FILE_POINTER && err != NO_ERROR) {
		write_log (_T("hdf_resize_target: SetFilePointer() %d\n"), err);
		return 0;
	}
	if (SetEndOfFile (hfd->handle->h)) {
		hfd->physsize = newsize;
		return 1;
	}
	err = GetLastError ();
	write_log (_T("hdf_resize_target: SetEndOfFile() %d\n"), err);
	return 0;
}

#endif

#ifdef WINDDK

static void generatestorageproperty (struct uae_driveinfo *udi, int ignoreduplicates)
{
	_tcscpy (udi->vendor_id, _T("UAE"));
	_tcscpy (udi->product_id, _T("DISK"));
	_tcscpy (udi->product_rev, _T("1.2"));
	_stprintf (udi->device_name, _T("%s"), udi->device_path);
	udi->removablemedia = 1;
}
static bool validoffset(ULONG offset, ULONG max)
{
	return offset > 0 && offset < max;
}

static int getstorageproperty (PUCHAR outBuf, int returnedLength, struct uae_driveinfo *udi, int ignoreduplicates)
{
	PSTORAGE_DEVICE_DESCRIPTOR devDesc;
	TCHAR orgname[1024];
	PUCHAR p;
	int i, j;
	ULONG size, size2;

	devDesc = (PSTORAGE_DEVICE_DESCRIPTOR) outBuf;
	size = devDesc->Version;
	size2 = devDesc->Size > returnedLength ? returnedLength : devDesc->Size;
	p = (PUCHAR) outBuf;
	if (offsetof(STORAGE_DEVICE_DESCRIPTOR, CommandQueueing) > size) {
		write_log (_T("too short STORAGE_DEVICE_DESCRIPTOR only %d bytes\n"), size);
		return -2;
	}
	if (devDesc->DeviceType != INQ_DASD && devDesc->DeviceType != INQ_ROMD && devDesc->DeviceType != INQ_OPTD && devDesc->DeviceType != INQ_OMEM) {
		write_log (_T("not a direct access device, ignored (type=%d)\n"), devDesc->DeviceType);
		return -2;
	}
	udi->devicetype = devDesc->DeviceType;
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, VendorIdOffset) && validoffset(devDesc->VendorIdOffset, size2) && p[devDesc->VendorIdOffset]) {
		j = 0;
		for (i = devDesc->VendorIdOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->vendor_id[j++] = p[i];
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductIdOffset) && validoffset(devDesc->ProductIdOffset, size2) && p[devDesc->ProductIdOffset]) {
		j = 0;
		for (i = devDesc->ProductIdOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->product_id[j++] = p[i];
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductRevisionOffset) && validoffset(devDesc->ProductRevisionOffset, size2) && p[devDesc->ProductRevisionOffset]) {
		j = 0;
		for (i = devDesc->ProductRevisionOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->product_rev[j++] = p[i];
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, SerialNumberOffset) && validoffset(devDesc->SerialNumberOffset, size2) && p[devDesc->SerialNumberOffset]) {
		j = 0;
		for (i = devDesc->SerialNumberOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->product_serial[j++] = p[i];
	}

	my_trim(udi->vendor_id);
	my_trim(udi->product_id);
	my_trim(udi->product_rev);
	my_trim(udi->product_serial);

	if (udi->vendor_id[0])
		_tcscat (udi->device_name, udi->vendor_id);
	if (udi->product_id[0]) {
		if (udi->device_name[0])
			_tcscat (udi->device_name, _T(" "));
		_tcscat (udi->device_name, udi->product_id);
	}
	if (udi->product_rev[0]) {
		if (udi->device_name[0])
			_tcscat (udi->device_name, _T(" "));
		_tcscat (udi->device_name, udi->product_rev);
	}
	if (udi->product_serial[0]) {
		if (udi->device_name[0])
			_tcscat (udi->device_name, _T(" "));
		_tcscat (udi->device_name, udi->product_serial);
	}
	if (!udi->device_name[0]) {
		write_log (_T("empty device id?!?, replacing with device path\n"));
		_tcscpy (udi->device_name, udi->device_path);
	}
	udi->removablemedia = devDesc->RemovableMedia;
	while (udi->device_name[0] != '\0' && udi->device_name[_tcslen(udi->device_name) - 1] == ':')
		udi->device_name[_tcslen(udi->device_name) - 1] = 0;
	for (int i = 0; i < _tcslen(udi->device_name); i++) {
		if (udi->device_name[i] == ':')
			udi->device_name[i] = '_';
	}
	write_log (_T("device id string: '%s'\n"), udi->device_name);
	udi->BusType = devDesc->BusType;
	if (ignoreduplicates) {
		if (!udi->removablemedia) {
			write_log (_T("drive letter not removable, ignored\n"));
			return -2;
		}
		_stprintf (orgname, _T(":%s"), udi->device_name);
		for (i = 0; i < hdf_getnumharddrives (); i++) {
			if (!_tcscmp (uae_drives[i].device_name, orgname)) {
				if (uae_drives[i].dangerous == -10) {
					write_log (_T("replaced old '%s'\n"), uae_drives[i].device_name);
					return i;
				}
				write_log (_T("duplicate device, ignored\n"));
				return -2;
			}
		}
	}
	return -1;
}

static bool getstorageinfo(uae_driveinfo *udi, STORAGE_DEVICE_NUMBER sdnp)
{
	int idx;
	const GUID *di = udi->devicetype == INQ_ROMD ? &GUID_DEVINTERFACE_CDROM : &GUID_DEVINTERFACE_DISK;
	HDEVINFO hIntDevInfo;
	DWORD vpm[3];

	vpm[0] = 0xffffffff;
	vpm[1] = 0xffffffff;
	vpm[2] = 0xffffffff;
	hIntDevInfo = SetupDiGetClassDevs(di, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (!hIntDevInfo)
		return false;
	idx = -1;
	for (;;) {
		PSP_DEVICE_INTERFACE_DETAIL_DATA pInterfaceDetailData = NULL;
		SP_DEVICE_INTERFACE_DATA interfaceData = { 0 };
		SP_DEVINFO_DATA deviceInfoData = { 0 };
		DWORD dwRequiredSize, returnedLength;

		idx++;
		interfaceData.cbSize = sizeof(interfaceData);
		if (!SetupDiEnumDeviceInterfaces(hIntDevInfo, NULL, di, idx, &interfaceData))
			break;
		dwRequiredSize = 0;
		if (!SetupDiGetDeviceInterfaceDetail(hIntDevInfo, &interfaceData, NULL, 0, &dwRequiredSize, NULL)) {
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				break;
			if (dwRequiredSize <= 0)
				break;
		}
		pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LPTR, dwRequiredSize);
		pInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		deviceInfoData.cbSize = sizeof(deviceInfoData);
		if (!SetupDiGetDeviceInterfaceDetail(hIntDevInfo, &interfaceData, pInterfaceDetailData, dwRequiredSize, &dwRequiredSize, &deviceInfoData)) {
			LocalFree(pInterfaceDetailData);
			continue;
		}
		HANDLE hDev = CreateFile(pInterfaceDetailData->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDev == INVALID_HANDLE_VALUE) {
			LocalFree(pInterfaceDetailData);
			continue;
		}
		LocalFree(pInterfaceDetailData);
		STORAGE_DEVICE_NUMBER sdn;
		if (!DeviceIoControl(hDev, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &returnedLength, NULL)) {
			CloseHandle(hDev);
			continue;
		}
		CloseHandle(hDev);
		if (sdnp.DeviceType != sdn.DeviceType || sdnp.DeviceNumber != sdn.DeviceNumber) {
			continue;
		}
		TCHAR pszDeviceInstanceId[1024];
		DEVINST dnDevInstParent, dwDeviceInstanceIdSize;
		dwDeviceInstanceIdSize = sizeof(pszDeviceInstanceId) / sizeof(TCHAR);
		if (!SetupDiGetDeviceInstanceId(hIntDevInfo, &deviceInfoData, pszDeviceInstanceId, dwDeviceInstanceIdSize, &dwRequiredSize)) {
			continue;
		}
		if (CM_Get_Parent(&dnDevInstParent, deviceInfoData.DevInst, 0) == CR_SUCCESS) {
			TCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
			if (CM_Get_Device_ID(dnDevInstParent, szDeviceInstanceID, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
				const static LPCTSTR arPrefix[3] = { TEXT("VID_"), TEXT("PID_"), TEXT("MI_") };
				LPTSTR pszNextToken;
				LPTSTR pszToken = _tcstok_s(szDeviceInstanceID, TEXT("\\#&"), &pszNextToken);
				while (pszToken != NULL) {
					for (int j = 0; j < 3; j++) {
						if (_tcsncmp(pszToken, arPrefix[j], lstrlen(arPrefix[j])) == 0) {
							wchar_t *endptr;
							vpm[j] = _tcstol(pszToken + lstrlen(arPrefix[j]), &endptr, 16);
						}
					}
					pszToken = _tcstok_s(NULL, TEXT("\\#&"), &pszNextToken);
				}
			}
		}
		if (vpm[0] != 0xffffffff && vpm[1] != 0xffffffff)
			break;
	}
	SetupDiDestroyDeviceInfoList(hIntDevInfo);
	if (vpm[0] == 0xffffffff || vpm[1] == 0xffffffff)
		return false;
	udi->usb_vid = (uae_u16)vpm[0];
	udi->usb_pid = (uae_u16)vpm[1];
	return true;
}

static void checkhdname(struct uae_driveinfo *udi)
{
	int cnt = 1;
	size_t off = _tcslen(udi->device_name);
	TCHAR tmp[MAX_DPATH];
	_tcscpy(tmp, udi->device_name);
	udi->device_name[0] = 0;
	for (;;) {
		if (isharddrive(tmp) < 0) {
			_tcscpy(udi->device_name, tmp);
			return;
		}
		tmp[off] = '_';
		tmp[off + 1] = cnt + '0';
		tmp[off + 2] = 0;
		cnt++;
	}
}

static BOOL GetDevicePropertyFromName(const TCHAR *DevicePath, DWORD Index, DWORD *index2, uae_u8 *buffer, int ignoreduplicates)
{
	int i, nosp, geom_ok;
	int ret = -1;
	STORAGE_PROPERTY_QUERY query;
	DRIVE_LAYOUT_INFORMATION_EX *dli;
	struct uae_driveinfo *udi;
	TCHAR orgname[1024];
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	UCHAR outBuf[20000];
	DISK_GEOMETRY			dg;
	GET_LENGTH_INFORMATION		gli;
	int gli_ok;
	BOOL                                status;
	ULONG                               length = 0, returned = 0, returnedLength;
	BOOL readonly = FALSE;
	struct uae_driveinfo tmpudi = { 0 };
	struct uae_driveinfo* udi2;
	udi = &tmpudi;
	int udiindex = *index2;

	//
	// Now we have the device path. Open the device interface
	// to send Pass Through command

	_tcscpy (udi->device_path, DevicePath);
	write_log (_T("opening device '%s'\n"), udi->device_path);

	// try read-write first, direct scsi needs also write access
	hDevice = CreateFile(
		udi->device_path,    // device interface name
		GENERIC_READ | GENERIC_WRITE,       // dwDesiredAccess
		FILE_SHARE_READ | FILE_SHARE_WRITE, // dwShareMode
		NULL,                               // lpSecurityAttributes
		OPEN_EXISTING,                      // dwCreationDistribution
		0,                                  // dwFlagsAndAttributes
		NULL                                // hTemplateFile
		);


	if (hDevice == INVALID_HANDLE_VALUE) {
		hDevice = CreateFile(
			udi->device_path,    // device interface name
			GENERIC_READ,       // dwDesiredAccess
			FILE_SHARE_READ | FILE_SHARE_WRITE, // dwShareMode
			NULL,                               // lpSecurityAttributes
			OPEN_EXISTING,                      // dwCreationDistribution
			0,                                  // dwFlagsAndAttributes
			NULL                                // hTemplateFile
		);
	}

	//
	// We have the handle to talk to the device.
	// So we can release the interfaceDetailData buffer
	//


	if (hDevice == INVALID_HANDLE_VALUE) {
		hDevice = CreateFile(
			udi->device_path,    // device interface name
			0,       // dwDesiredAccess
			FILE_SHARE_READ | FILE_SHARE_WRITE, // dwShareMode
			NULL,                               // lpSecurityAttributes
			OPEN_EXISTING,                      // dwCreationDistribution
			0,                                  // dwFlagsAndAttributes
			NULL                                // hTemplateFile
			);
		if (hDevice == INVALID_HANDLE_VALUE) {
			write_log (_T("CreateFile failed with error: %d\n"), GetLastError());
			ret = 1;
			goto end;
		}
		readonly = TRUE;
	}

	memset (outBuf, 0, sizeof outBuf);
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	status = DeviceIoControl(
		hDevice,
		IOCTL_STORAGE_QUERY_PROPERTY,
		&query,
		sizeof (STORAGE_PROPERTY_QUERY),
		&outBuf,
		sizeof outBuf,
		&returnedLength,
		NULL);
	if (!status) {
		DWORD err = GetLastError ();
		write_log (_T("IOCTL_STORAGE_QUERY_PROPERTY failed with error code %d.\n"), err);
		if (err != ERROR_INVALID_FUNCTION) {
			ret = 1;
			goto end;
		}
		nosp = 1;
		generatestorageproperty (udi, ignoreduplicates);
		udiindex = -1;
	} else {
		nosp = 0;
		udiindex = getstorageproperty (outBuf, returnedLength, udi, ignoreduplicates);
		if (udiindex == -2) {
			ret = 1;
			goto end;
		}
		STORAGE_DEVICE_NUMBER sdn;
		if (DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &returnedLength, NULL)) {
			getstorageinfo(udi, sdn);
		}
		readidentity(INVALID_HANDLE_VALUE, udi, NULL);
	}
	udi = &uae_drives[udiindex < 0 ? *index2 : udiindex];
	memcpy (udi, &tmpudi, sizeof (struct uae_driveinfo));
	udi2 = udi;

	_tcscpy (orgname, udi->device_name);
	udi->bytespersector = 512;
	geom_ok = 1;
	if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, (void*)&dg, sizeof (dg), &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (isnomediaerr (err)) {
			udi->nomedia = 1;
			write_log("no media\n");
			goto amipartfound;
		}
		write_log (_T("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with error code %d.\n"), err);
		dg.BytesPerSector = 512;
		geom_ok = 0;
	}
	udi->readonly = 0;
	if (!DeviceIoControl (hDevice, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &returnedLength, NULL)) {
		DWORD err = GetLastError ();
		if (err == ERROR_WRITE_PROTECT)
			udi->readonly = 1;
	}

	udi->offset = 0;
	udi->size = 0;

#if 0
	if (readonly) {
		memset(outBuf, 0, sizeof(outBuf));
		status = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
			&outBuf, sizeof(outBuf), &returnedLength, NULL);
		if (status) {
			dli = (DRIVE_LAYOUT_INFORMATION_EX *)outBuf;
			if (dli->PartitionCount && dli->PartitionStyle == PARTITION_STYLE_MBR) {
				write_log("MBR but access denied\n");
				ret = 1;
				udi->dangerous = -10;
				udi->readonly = -1;
				goto end;
			}
			if (dli->PartitionCount && dli->PartitionStyle == PARTITION_STYLE_GPT) {
				write_log("GPT but access denied\n");
				ret = 1;
				goto end;
			}
		}
		write_log("skipped, unsupported drive\n");
		udiindex = -1;
		ret = 1;
		goto end;
	}
#endif

	gli_ok = 1;
	gli.Length.QuadPart = 0;
	if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, (void*)&gli, sizeof (gli), &returnedLength, NULL)) {
		gli_ok = 0;
		write_log (_T("IOCTL_DISK_GET_LENGTH_INFO failed with error code %d.\n"), GetLastError());
		if (!nosp)
			write_log (_T("IOCTL_DISK_GET_LENGTH_INFO not supported, detected disk size may not be correct.\n"));
	} else {
		write_log (_T("IOCTL_DISK_GET_LENGTH_INFO returned size: %I64d (0x%I64x)\n"), gli.Length.QuadPart, gli.Length.QuadPart);
	}
	if (geom_ok == 0 && gli_ok == 0) {
		write_log (_T("Can't detect size of device\n"));
		ret = 1;
		goto end;
	}

	if (geom_ok) {
		udi->bytespersector = dg.BytesPerSector;
		if (dg.BytesPerSector < 512) {
			write_log (_T("unsupported blocksize < 512 (%d)\n"), dg.BytesPerSector);
			ret = 1;
			goto end;
		}
		if (dg.BytesPerSector > 2048) {
			write_log (_T("unsupported blocksize > 2048 (%d)\n"), dg.BytesPerSector);
			ret = 1;
			goto end;
		}
		write_log (_T("BPS=%d Cyls=%I64d TPC=%d SPT=%d MediaType=%d\n"),
			dg.BytesPerSector, dg.Cylinders.QuadPart, dg.TracksPerCylinder, dg.SectorsPerTrack, dg.MediaType);
		udi->size = (uae_u64)dg.BytesPerSector * (uae_u64)dg.Cylinders.QuadPart *
			(uae_u64)dg.TracksPerCylinder * (uae_u64)dg.SectorsPerTrack;
		udi->cylinders = dg.Cylinders.QuadPart > 65535 ? 0 : dg.Cylinders.LowPart;
		udi->sectors = dg.SectorsPerTrack;
		udi->heads = dg.TracksPerCylinder;
	}

	if (gli_ok && gli.Length.QuadPart)
		udi->size = gli.Length.QuadPart;

	if (ischs(udi->identity) && gli.Length.QuadPart == 0) {
		int c, h, s;
		tochs(udi->identity, -1, &c, &h, &s);
		udi->size = (uae_u64)c * h * s * 512;
		udi->cylinders = c;
		udi->heads = h;
		udi->sectors = s;
		udi->chsdetected = true;
	}

	write_log (_T("device size %I64d (0x%I64x) bytes\n"), udi->size, udi->size);
	trim (orgname);

	memset (outBuf, 0, sizeof (outBuf));
	status = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
		&outBuf, sizeof (outBuf), &returnedLength, NULL);
	if (!status) {
		DWORD err = GetLastError();
		write_log (_T("IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed with error code %d.\n"), err);
	} else {
		dli = (DRIVE_LAYOUT_INFORMATION_EX*)outBuf;
		if (dli->PartitionCount && dli->PartitionStyle == PARTITION_STYLE_MBR) {
			int nonzeropart = 0;
			int gotpart = 0;
			int safepart = 0;
			udi->dangerous = -6;
			udi->readonly = readonly ? 2 : 0;
			if (dli->PartitionCount == 1 && dli->PartitionEntry[0].StartingOffset.QuadPart == 0) {
				goto checkrdb;
			}
			write_log (_T("%d MBR partitions found\n"), dli->PartitionCount);
			for (i = 0; i < dli->PartitionCount && (*index2) < MAX_FILESYSTEM_UNITS; i++) {
				PARTITION_INFORMATION_EX *pi = &dli->PartitionEntry[i];
				write_log (_T("%d: num: %d type: %02X offset: %I64d size: %I64d, "), i,
					pi->PartitionNumber, pi->Mbr.PartitionType, pi->StartingOffset.QuadPart, pi->PartitionLength.QuadPart);

				bool accepted = false;
				if (i == 0) {
					// check if drive is MBR partitioned with RDB on top of it.
					int dang = safetycheck(hDevice, udi->device_path, 0, buffer, dg.BytesPerSector, udi->identity, udi->chsdetected, &accepted);
					if (accepted) {
						udi->dangerous = dang;
					}
				}

				if (!accepted && (pi->Mbr.RecognizedPartition == 0 || pi->Mbr.PartitionType == PARTITION_ENTRY_UNUSED)) {
					write_log(_T("unrecognized\n"));
					udi->readonly = readonly ? 2 : 0;
					continue;
				}

				nonzeropart++;
				udi++;
				(*index2)++;
				memmove (udi, udi2, sizeof (*udi));
				udi->device_name[0] = 0;
				udi->offset = pi->StartingOffset.QuadPart;
				udi->size = pi->PartitionLength.QuadPart;
				_stprintf (udi->device_name, _T(":P#%d_%s"), pi->PartitionNumber, orgname);
				_stprintf(udi->device_full_path, _T("%s:%s"), udi->device_name, udi->device_path);
				checkhdname(udi);
				if (pi->Mbr.PartitionType != 0x76 && pi->Mbr.PartitionType != 0x30) {
					write_log(_T("type not 0x76 or 0x30\n"));
				} else {
					write_log(_T("selected\n"));
					udi->partitiondrive = true;
				}
				udi->dangerous = -5;
				udi->readonly = readonly ? 2 : 0;
				safepart = 1;
				gotpart = 1;
			}
			if (!nonzeropart) {
				write_log (_T("empty MBR partition table detected, checking for RDB\n"));
			} else if (!gotpart) {
				write_log (_T("non-empty MBR partition table detected, doing RDB check anyway\n"));
			} else if (safepart) {
				goto amipartfound; /* ugly but bleh.. */
			}
		} else if (dli->PartitionCount && dli->PartitionStyle == PARTITION_STYLE_GPT) {
			int nonzeropart = 0;
			int gotpart = 0;
			int safepart = 0;
			udi->dangerous = -11;
			udi->readonly = readonly ? 2 : 0;
			write_log(_T("%d GPT partitions found\n"), dli->PartitionCount);
			for (i = 0; i < dli->PartitionCount && (*index2) < MAX_FILESYSTEM_UNITS; i++) {
				PARTITION_INFORMATION_EX *pi = &dli->PartitionEntry[i];
				nonzeropart++;
				if (pi->Gpt.PartitionType == PARTITION_GPT_AMIGA) {
					udi++;
					(*index2)++;
					memmove(udi, udi2, sizeof(*udi));
					udi->device_name[0] = 0;
					udi->offset = pi->StartingOffset.QuadPart;
					udi->size = pi->PartitionLength.QuadPart;
					_stprintf(udi->device_name, _T(":GP#%08x_%04x_%04x_%02x%02x_%02x%02x%02x%02x%02x%02x_%s"),
						pi->Gpt.PartitionId.Data1, pi->Gpt.PartitionId.Data2, pi->Gpt.PartitionId.Data3,
						pi->Gpt.PartitionId.Data4[0], pi->Gpt.PartitionId.Data4[1], pi->Gpt.PartitionId.Data4[2], pi->Gpt.PartitionId.Data4[3],
						pi->Gpt.PartitionId.Data4[4], pi->Gpt.PartitionId.Data4[5], pi->Gpt.PartitionId.Data4[6], pi->Gpt.PartitionId.Data4[7],
						pi->Gpt.Name);
					_stprintf(udi->device_full_path, _T("%s:%s"), udi->device_name, udi->device_path);
					checkhdname(udi);
					udi->dangerous = -5;
					udi->partitiondrive = true;
					safepart = 1;
				}
			}
			if (!nonzeropart) {
				write_log(_T("empty GPT partition table detected\n"));
				goto end;
			} else if (!gotpart) {
				write_log(_T("non-empty GPT partition table detected, no supported partition GUIDs found\n"));
				goto end;
			} else if (safepart) {
				goto amipartfound;
			}

		} else {
			write_log (_T("no MBR partition table detected, checking for RDB\n"));
		}
	}
checkrdb:
	if (udi->offset == 0 && udi->size) {
		udi->dangerous = safetycheck (hDevice, udi->device_path, 0, buffer, dg.BytesPerSector, udi->identity, udi->chsdetected, NULL);
		if (udi->dangerous > 0)
			goto end;
	}
amipartfound:
	_stprintf (udi2->device_name, _T(":%s"), orgname);
	_stprintf (udi2->device_full_path, _T("%s:%s"), udi2->device_name, udi2->device_path);
	checkhdname(udi2);
	if (udiindex < 0) {
		(*index2)++;
	}
end:
	if (hDevice != INVALID_HANDLE_VALUE)
		CloseHandle (hDevice);
	return ret;
}

/* see MS KB article Q264203 more more information */

static BOOL GetDeviceProperty(HDEVINFO IntDevInfo, DWORD Index, DWORD *index2, uae_u8 *buffer)
	/*++

	Routine Description:

	This routine enumerates the disk devices using the Device interface
	GUID DiskClassGuid. Gets the Adapter & Device property from the port
	driver. Then sends IOCTL through SPTI to get the device Inquiry data.

	Arguments:

	IntDevInfo - Handles to the interface device information list

	Index      - Device member

	Return Value:

	TRUE / FALSE. This decides whether to continue or not

	--*/
{
	SP_DEVICE_INTERFACE_DATA            interfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA    interfaceDetailData = NULL;
	BOOL                                status;
	DWORD                               interfaceDetailDataSize = 0,
		reqSize,
		errorCode;
	int ret = -1;

	interfaceData.cbSize = sizeof (SP_INTERFACE_DEVICE_DATA);

	status = SetupDiEnumDeviceInterfaces (
		IntDevInfo,             // Interface Device Info handle
		0,                      // Device Info data
		&GUID_DEVINTERFACE_DISK, // Interface registered by driver
		Index,                  // Member
		&interfaceData          // Device Interface Data
		);

	if (status == FALSE) {
		errorCode = GetLastError();
		if (errorCode != ERROR_NO_MORE_ITEMS) {
			write_log (_T("SetupDiEnumDeviceInterfaces failed with error: %d\n"), errorCode);
		}
		ret = 0;
		goto end;
	}

	//
	// Find out required buffer size, so pass NULL
	//

	status = SetupDiGetDeviceInterfaceDetail (
		IntDevInfo,         // Interface Device info handle
		&interfaceData,     // Interface data for the event class
		NULL,               // Checking for buffer size
		0,                  // Checking for buffer size
		&reqSize,           // Buffer size required to get the detail data
		NULL                // Checking for buffer size
		);

	//
	// This call returns ERROR_INSUFFICIENT_BUFFER with reqSize
	// set to the required buffer size. Ignore the above error and
	// pass a bigger buffer to get the detail data
	//

	if (status == FALSE) {
		errorCode = GetLastError();
		if (errorCode != ERROR_INSUFFICIENT_BUFFER) {
			write_log (_T("SetupDiGetDeviceInterfaceDetail failed with error: %d\n"), errorCode);
			ret = 0;
			goto end;
		}
	}

	//
	// Allocate memory to get the interface detail data
	// This contains the devicepath we need to open the device
	//

	interfaceDetailDataSize = reqSize;
	interfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)xmalloc (uae_u8, interfaceDetailDataSize);
	if (interfaceDetailData == NULL) {
		write_log (_T("Unable to allocate memory to get the interface detail data.\n"));
		ret = 0;
		goto end;
	}
	interfaceDetailData->cbSize = sizeof (SP_INTERFACE_DEVICE_DETAIL_DATA);

	status = SetupDiGetDeviceInterfaceDetail (
		IntDevInfo,               // Interface Device info handle
		&interfaceData,           // Interface data for the event class
		interfaceDetailData,      // Interface detail data
		interfaceDetailDataSize,  // Interface detail data size
		&reqSize,                 // Buffer size required to get the detail data
		NULL);                    // Interface device info

	if (status == FALSE) {
		write_log (_T("Error in SetupDiGetDeviceInterfaceDetail failed with error: %d\n"), GetLastError());
		ret = 0;
		goto end;
	}

	ret = GetDevicePropertyFromName (interfaceDetailData->DevicePath, Index, index2, buffer, 0);

end:
	free (interfaceDetailData);

	return ret;
}

#endif


static int hdf_init2 (int force)
{
#ifdef WINDDK
	HDEVINFO hIntDevInfo;
#endif
	DWORD index = 0, index2 = 0, drive;
	uae_u8 *buffer;
	DWORD dwDriveMask;

	if (drives_enumerated && !force)
		return num_drives;
	drives_enumerated = true;
	num_drives = 0;
#ifdef WINDDK
	buffer = (uae_u8*)VirtualAlloc (NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
	if (buffer) {
		memset (uae_drives, 0, sizeof (uae_drives));
		num_drives = 0;
		hIntDevInfo = SetupDiGetClassDevs (&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
		if (hIntDevInfo != INVALID_HANDLE_VALUE) {
			while (index < MAX_FILESYSTEM_UNITS) {
				memset (uae_drives + index2, 0, sizeof (struct uae_driveinfo));
				if (!GetDeviceProperty (hIntDevInfo, index, &index2, buffer))
					break;
				index++;
				num_drives = index2;
			}
			SetupDiDestroyDeviceInfoList (hIntDevInfo);
		}
		dwDriveMask = GetLogicalDrives ();
		for(drive = 'A'; drive <= 'Z'; drive++) {
			if((dwDriveMask & 1) && (drive >= 'C' || usefloppydrives)) {
				TCHAR tmp1[20], tmp2[20];
				DWORD drivetype;
				_stprintf (tmp1, _T("%c:\\"), drive);
				drivetype = GetDriveType (tmp1);
				if (drivetype != DRIVE_REMOTE) {
					_stprintf (tmp2, _T("\\\\.\\%c:"), drive);
					GetDevicePropertyFromName (tmp2, index, &index2, buffer, 1);
					num_drives = index2;
				}
			}
			dwDriveMask >>= 1;
		}
#if 0
		hIntDevInfo = SetupDiGetClassDevs (&GUID_DEVCLASS_MTD, NULL, NULL, DIGCF_PRESENT);
		if (hIntDevInfo != INVALID_HANDLE_VALUE) {
			while (index < MAX_FILESYSTEM_UNITS) {
				memset (uae_drives + index2, 0, sizeof (struct uae_driveinfo));
				index++;
				num_drives = index2;
			}
			SetupDiDestroyDeviceInfoList(hIntDevInfo);
		}
#endif
		VirtualFree (buffer, 0, MEM_RELEASE);
	}
	num_drives = index2;
	write_log (_T("Drive scan result: %d drives detected\n"), num_drives);
#endif
	return num_drives;
}

int hdf_init_target (void)
{
	return hdf_init2 (0);
}

int hdf_getnumharddrives (void)
{
	return num_drives;
}

TCHAR *hdf_getnameharddrive (int index, int flags, int *sectorsize, int *dangerousdrive, uae_u32 *outflags)
{
	struct uae_driveinfo *udi = &uae_drives[index];
	static TCHAR name[512];
	TCHAR tmp[32];
	uae_u64 size = udi->size;
	int nomedia = udi->nomedia;
	const TCHAR *dang = _T("?");
	const TCHAR *rw = _T("RW");
	bool noaccess = false;

	if (outflags) {
		*outflags = 0;
		if (udi->identity[0] || udi->identity[1])
			*outflags = 1;
	}

	if (dangerousdrive)
		*dangerousdrive = 0;
	switch (udi->dangerous)
	{
	case -11:
		dang = _T("[GPT]");
		break;
	case -10:
		dang = _T("[???]");
		noaccess = true;
		break;
	case -5:
		dang = _T("[PART]");
		break;
	case -6:
		dang = _T("[MBR]");
		break;
	case -7:
		dang = _T("[!]");
		break;
	case -8:
		dang = _T("[UNK]");
		break;
	case -9:
		dang = _T("[EMPTY]");
		break;
	case -4:
		dang = _T("(CHS)");
		break;
	case -3:
		dang = _T("(CPRM)");
		break;
	case -2:
		dang = _T("(SRAM)");
		break;
	case -1:
		dang = _T("(RDB)");
		break;
	case 0:
		dang = _T("[OS]");
		if (dangerousdrive)
			*dangerousdrive |= 1;
		break;
	}

	if (noaccess) {
		if (dangerousdrive)
			*dangerousdrive = -1;
		if (flags & 1) {
			_stprintf (name, _T("[ACCESS DENIED] %s"), udi->device_name + 1);
			return name;
		}
	} else {
		if (nomedia) {
			dang = _T("[NO MEDIA]");
			if (dangerousdrive)
				*dangerousdrive &= ~1;
		}
		if (udi->readonly > 1) {
			rw = _T("ACCESS DENIED");
		} else if (udi->readonly) {
			rw = _T("RO");
			if (dangerousdrive && !nomedia)
				*dangerousdrive |= 2;
		}

		if (sectorsize)
			*sectorsize = udi->bytespersector;
		if (flags & 1) {
			if (nomedia) {
				_tcscpy (tmp, _T("N/A"));
			} else {
				if (size == 0)
					_tcscpy(tmp, _T("?"));
				else if (size >= 1024 * 1024 * 1024)
					_stprintf (tmp, _T("%.1fG"), ((double)(uae_u32)(size / (1024 * 1024))) / 1024.0);
				else if (size < 10 * 1024 * 1024)
					_stprintf (tmp, _T("%lldK"), size / 1024);
				else
					_stprintf (tmp, _T("%.1fM"), ((double)(uae_u32)(size / (1024))) / 1024.0);
			}
			_stprintf (name, _T("%10s [%s,%s] %s"), dang, tmp, rw, udi->device_name + 1);
			return name;
		}
	}
	if (flags & 4)
		return udi->device_full_path;
	if (flags & 2)
		return udi->device_path;
	return udi->device_name;
}

static int hmc (struct hardfiledata *hfd)
{
	uae_u8 *buf = xmalloc (uae_u8, hfd->ci.blocksize);
	DWORD ret = 0, got, err = 0, status = 0;
	int first = 1;

	while (hfd->handle_valid) {
		write_log (_T("testing if %s has media inserted\n"), hfd->emptyname);
		status = 0;
		SetFilePointer (hfd->handle->h, 0, NULL, FILE_BEGIN);
		ret = ReadFile (hfd->handle->h, buf, hfd->ci.blocksize, &got, NULL);
		err = GetLastError ();
		if (ret) {
			if (got == hfd->ci.blocksize) {
				write_log (_T("read ok (%d)\n"), got);
			} else {
				write_log (_T("read ok but no data (%d)\n"), hfd->ci.blocksize);
				ret = 0;
				err = 0;
			}
		} else {
			write_log (_T("=%d\n"), err);
		}
		if (!ret && (err == ERROR_DEV_NOT_EXIST || err == ERROR_WRONG_DISK)) {
			if (!first)
				break;
			first = 0;
			hdf_open (hfd, hfd->emptyname);
			if (!hfd->handle_valid) {
				/* whole device has disappeared */
				status = -1;
				goto end;
			}
			continue;
		}
		break;
	}
	if (ret && hfd->drive_empty)
		status = 1;
	if (!ret && !hfd->drive_empty && isnomediaerr (err))
		status = -1;
end:
	xfree (buf);
	write_log (_T("hmc returned %d\n"), status);
	return status;
}

int hardfile_remount (int nr);


static void hmc_check (struct hardfiledata *hfd, struct uaedev_config_data *uci, int *rescanned, int *reopen,
	int *gotinsert, const TCHAR *drvname, int inserted)
{
	int ret;

	if (!hfd || !hfd->emptyname)
		return;
	if (*rescanned == 0) {
		hdf_init2 (1);
		*rescanned = 1;
	}
	if (hfd->emptyname && _tcslen (hfd->emptyname) >= 6 && drvname && _tcslen (drvname) == 3 && drvname[1] == ':' && drvname[2] == '\\' && !inserted) { /* drive letter check */
		TCHAR tmp[10], *p;
		_stprintf (tmp, _T("\\\\.\\%c:"), drvname[0]);
		p = hfd->emptyname + _tcslen (hfd->emptyname) - 6;
		write_log( _T("workaround-remove test: '%s' and '%s'\n"), p, drvname);
		if (!_tcscmp (tmp, p)) {
			TCHAR *en = my_strdup (hfd->emptyname);
			write_log (_T("workaround-remove done\n"));
			hdf_close (hfd);
			hfd->emptyname = en;
			hfd->drive_empty = 1;
			hardfile_do_disk_change (uci, 0);
			return;
		}
	}

	if (hfd->drive_empty < 0 || !hfd->handle_valid) {
		int empty = hfd->drive_empty;
		int r;
		//write_log (_T("trying to open '%s' de=%d hv=%d\n"), hfd->emptyname, hfd->drive_empty, hfd->handle_valid);
		r = hdf_open (hfd, hfd->emptyname);
		//write_log (_T("=%d\n"), r);
		if (r <= 0)
			return;
		*reopen = 1;
		if (hfd->drive_empty < 0)
			return;
		hfd->drive_empty = empty ? 1 : 0;
	}
	ret = hmc (hfd);
	if (!ret)
		return;
	if (ret > 0) {
		if (*reopen == 0) {
			hdf_open (hfd, hfd->emptyname);
			if (!hfd->handle_valid)
				return;
		}
		*gotinsert = 1;
		//hardfile_remount (uci);
	}
	hardfile_do_disk_change (uci, ret < 0 ? 0 : 1);
}

int win32_hardfile_media_change (const TCHAR *drvname, int inserted)
{
	int gotinsert = 0, rescanned = 0;
	int i, j;

	for (i = 0; i < MAX_FILESYSTEM_UNITS; i++) {
		struct hardfiledata *hfd = get_hardfile_data (i);
		int reopen = 0;
		if (!hfd || !(hfd->flags & HFD_FLAGS_REALDRIVE))
			continue;
		if (hfd->ci.lock || hfd->ci.controller_type != HD_CONTROLLER_TYPE_UAE) {
			for (j = 0; j < currprefs.mountitems; j++) {
				if (currprefs.mountconfig[j].configoffset == i) {
					hmc_check(hfd, &currprefs.mountconfig[j], &rescanned, &reopen, &gotinsert, drvname, inserted);
					break;
				}
			}
		}
	}
	for (i = 0; i < currprefs.mountitems; i++) {
		extern struct hd_hardfiledata *pcmcia_disk;
		int reopen = 0;
		struct uaedev_config_data *uci = &currprefs.mountconfig[i];
		if (uci->ci.lock || uci->ci.controller_type != HD_CONTROLLER_TYPE_UAE) {
			const struct expansionromtype *ert = get_unit_expansion_rom(uci->ci.controller_type);
			if (ert && (ert->deviceflags & EXPANSIONTYPE_PCMCIA) && pcmcia_disk) {
				hmc_check(&pcmcia_disk->hfd, uci, &rescanned, &reopen, &gotinsert, drvname, inserted);
			}
		}
	}
	//write_log (_T("win32_hardfile_media_change returned %d\n"), gotinsert);
	return gotinsert;
}

extern HMODULE hUIDLL;
extern HINSTANCE hInst;

#define COPY_CACHE_SIZE 1024*1024
int harddrive_to_hdf (HWND hDlg, struct uae_prefs *p, int idx)
{
	HANDLE h = INVALID_HANDLE_VALUE, hdst = INVALID_HANDLE_VALUE;
	void *cache = NULL;
	DWORD ret, got, gotdst, get;
	uae_u64 size, sizecnt, written;
	LARGE_INTEGER li;
	TCHAR path[MAX_DPATH], tmp[MAX_DPATH], tmp2[MAX_DPATH];
	DWORD retcode = 0;
	HWND hwnd, hwndprogress, hwndprogresstxt;
	MSG msg;
	int pct;
	DWORD r;
	bool chsmode = false;
	DWORD erc = 0;
	int cyls = 0, heads = 0, secs = 0;
	int cyl = 0, head = 0;
	int specialaccessmode = 0;
	int progressdialogreturn = 0;
	int seconds = -1;

	SAVECDS;

	cache = VirtualAlloc (NULL, COPY_CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	if (!cache)
		goto err;

	// Direct scsi for CHS check requires both READ and WRITE access.
	h = CreateFile(uae_drives[idx].device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		if (!gethdfchs(hDlg, &uae_drives[idx], h, NULL, NULL, NULL)) {
			chsmode = true;
			erc = gethdfchs(hDlg, &uae_drives[idx], h, &cyls, &heads, &secs);
			if (erc == -100) {
				chsmode = false;
				CloseHandle(h);
				h = INVALID_HANDLE_VALUE;
			} else if (erc) {
				goto err;
			} else {
				size = (uae_u64)cyls * heads * secs * 512;
			}
		} else {
			CloseHandle(h);
		}
	}
	erc = 0;
	if (!chsmode) {
		size = uae_drives[idx].size;
		h = CreateFile(uae_drives[idx].device_path, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
	}
	if (h == INVALID_HANDLE_VALUE)
		goto err;
	path[0] = 0;
	DiskSelection (hDlg, IDC_PATH_NAME, 3, p, NULL, NULL);
	GetDlgItemText (hDlg, IDC_PATH_NAME, path, MAX_DPATH);
	if (*path == 0)
		goto err;
	hdst = CreateFile (path, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
	if (hdst == INVALID_HANDLE_VALUE)
		goto err;

	if (DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &r, NULL)) {
		if (DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &r, NULL)) {
			write_log(_T("Volume locked and dismounted\n"));
		} else {
			write_log(_T("WARNING: '%s' FSCTL_DISMOUNT_VOLUME returned %d\n"), path, GetLastError());
		}
	} else {
		write_log(_T("WARNING: '%s' FSCTL_LOCK_VOLUME returned %d\n"), path, GetLastError());
	}
	if (!DeviceIoControl(h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &r, NULL)) {
		write_log (_T("WARNING: '%s' FSCTL_ALLOW_EXTENDED_DASD_IO returned %d\n"), path, GetLastError ());
	}
	li.QuadPart = size;
	ret = SetFilePointer (hdst, li.LowPart, &li.HighPart, FILE_BEGIN);
	if (ret == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
		goto err;
	if (!SetEndOfFile (hdst))
		goto err;
	li.QuadPart = 0;
	SetFilePointer (hdst, 0, &li.HighPart, FILE_BEGIN);
	li.QuadPart = 0;
	SetFilePointer (h, 0, &li.HighPart, FILE_BEGIN);
	hwnd = CustomCreateDialog(IDD_PROGRESSBAR, hDlg, ProgressDialogProc, &cdstate);
	if (hwnd == NULL)
		goto err;
	hwndprogress = GetDlgItem (hwnd, IDC_PROGRESSBAR);
	hwndprogresstxt = GetDlgItem (hwnd, IDC_PROGRESSBAR_TEXT);
	ShowWindow (hwnd, SW_SHOW);
	pct = 0;
	sizecnt = 0;
	written = 0;
	
	for (;;) {
		progressdialogreturn = cdstate.active > 0 ? -1 : 0;
		if (progressdialogreturn >= 0)
			break;
		SYSTEMTIME t;
		GetLocalTime(&t);
		if (t.wSecond != seconds) {
			seconds = t.wSecond;
			SendMessage (hwndprogress, PBM_SETPOS, (WPARAM)pct, 0);
			if (chsmode) {
				_stprintf(tmp2, _T("Cyl %d/%d Head %d/%d"), cyl, cyls, head, heads);
			} else {
				_stprintf(tmp2, _T("LBA %lld/%lld"), written / 512, size / 512);
			}
			_stprintf (tmp, _T("%s %dM/%dM (%d%%)"), tmp2, (int)(written >> 20), (int)(size >> 20), pct);
			SendMessage (hwndprogresstxt, WM_SETTEXT, 0, (LPARAM)tmp);
			while (PeekMessage (&msg, hwnd, 0, 0, PM_REMOVE)) {
				if (!IsDialogMessage (hwnd, &msg)) {
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
			}
		}
		got = gotdst = 0;
		li.QuadPart = sizecnt;
		if (SetFilePointer(h, li.LowPart, &li.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			DWORD err = GetLastError ();
			if (err != NO_ERROR) {
				progressdialogreturn = 3;
				break;
			}
		}
		if (chsmode) {
			int readsize = secs;
			int seccnt = 0;
			uae_u8 *p = (uae_u8*)cache;
			while (seccnt < secs) {
				int extrablock = 1;
				if (seccnt + readsize > secs)
					readsize = secs - seccnt;
				if (head == heads - 1 && cyl == cyls - 1)
					extrablock = 0;
				do_scsi_read10_chs(h, -1, cyl, head, seccnt + extrablock, p, readsize, &specialaccessmode, false);
				get = 512 * readsize;
				got = 512 * readsize;
				p += 512 * readsize;
				seccnt += readsize;
			}
			head++;
			if (head >= heads) {
				head = 0;
				cyl++;
			}
		} else {
			get = COPY_CACHE_SIZE;
			if (sizecnt + get > size)
				get = (DWORD)(size - sizecnt);
			if (!ReadFile(h, cache, get, &got, NULL)) {
				progressdialogreturn = 4;
				break;
			}
		}
		if (get != got) {
			progressdialogreturn = 5;
			break;
		}
		if (got > 0) {
			if (written + got > size)
				got = (DWORD)(size - written);
			if (!WriteFile (hdst, cache, got, &gotdst, NULL))  {
				progressdialogreturn = 5;
				break;
			}
			written += gotdst;
			if (written == size)
				break;
		}
		if (got != COPY_CACHE_SIZE && !chsmode) {
			progressdialogreturn = 1;
			break;
		}
		if (got != gotdst) {
			progressdialogreturn = 2;
			break;
		}
		sizecnt += got;
		pct = (int)(sizecnt * 100 / size);
	}
	if (cdstate.active) {
		DestroyWindow (hwnd);
		while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	if (progressdialogreturn >= 0)
		goto err;
	retcode = 1;
	WIN32GUI_LoadUIString (IDS_HDCLONE_OK, tmp, MAX_DPATH);
	gui_message (tmp);
	goto ok;

err:
	if (!erc)
		erc = GetLastError ();
	if (erc) {
		LPWSTR pBuffer = NULL;
		WIN32GUI_LoadUIString(IDS_HDCLONE_FAIL, tmp, MAX_DPATH);
		if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, erc, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&pBuffer, 0, NULL))
			pBuffer = NULL;
		_stprintf(tmp2, tmp, progressdialogreturn, erc, pBuffer ? _T("<unknown>") : pBuffer);
		gui_message(tmp2);
		LocalFree(pBuffer);
	}

ok:
	RESTORECDS;
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle (h);
	if (cache)
		VirtualFree (cache, 0, MEM_RELEASE);
	if (hdst != INVALID_HANDLE_VALUE)
		CloseHandle (hdst);
	return retcode;
}
