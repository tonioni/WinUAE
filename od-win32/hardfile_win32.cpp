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

#define hfd_log write_log
#define hdf_log2
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

struct hardfilehandle
{
	int zfile;
	struct zfile *zf;
	HANDLE h;
	BOOL firstwrite;
};

struct uae_driveinfo {
	TCHAR vendor_id[128];
	TCHAR product_id[128];
	TCHAR product_rev[128];
	TCHAR product_serial[128];
	TCHAR device_name[1024];
	TCHAR device_path[1024];
	TCHAR device_full_path[2048];
	uae_u64 size;
	uae_u64 offset;
	int bytespersector;
	int removablemedia;
	int nomedia;
	int dangerous;
	bool partitiondrive;
	int readonly;
	int cylinders, sectors, heads;
};

#define HDF_HANDLE_WIN32 1
#define HDF_HANDLE_ZFILE 2
#define HDF_HANDLE_UNKNOWN 3

#define CACHE_SIZE 16384
#define CACHE_FLUSH_TIME 5

/* safety check: only accept drives that:
* - contain RDSK in block 0
* - block 0 is zeroed
*/

int harddrive_dangerous; // = 0x1234dead; // test only!
int do_rdbdump;
static struct uae_driveinfo uae_drives[MAX_FILESYSTEM_UNITS];

#if 1
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
		ReadFile (h, buf, blocksize, &outlen, NULL);
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
	dli = (DRIVE_LAYOUT_INFORMATION_EX*)xmalloc (uae_u8, outsize);
	if (DeviceIoControl (h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, dli, outsize, &written, NULL)) {
		*sign = dli->Mbr.Signature;
		*pstyle = dli->PartitionStyle;
		ok = 1;
	} else {
		hdf_log2 (_T("IOCTL_DISK_GET_DRIVE_LAYOUT_EX() returned %08x\n"), GetLastError ());
	}
	if (!ok) {
		if (DeviceIoControl (h, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0, dli, outsize, &written, NULL)) {
			DRIVE_LAYOUT_INFORMATION *dli2 = (DRIVE_LAYOUT_INFORMATION*)dli;
			*sign = dli2->Signature;
			*pstyle = PARTITION_STYLE_MBR;
			ok = 1;
		} else {
			hdf_log2 (_T("IOCTL_DISK_GET_DRIVE_LAYOUT() returned %08x\n"), GetLastError ());
		}
	}
	hdf_log2 (_T("getsignfromhandle(signature=%08X,pstyle=%d)\n"), *sign, *pstyle);
	xfree (dli);
	return ok;
}

static int ismounted (const TCHAR *name, HANDLE hd)
{
	HANDLE h;
	TCHAR volname[MAX_DPATH];
	int mounted;
	DWORD sign, pstyle;

	hdf_log2 (_T("\n"));
	hdf_log2 (_T("Name='%s'\n"), name);
	if (!getsignfromhandle (hd, &sign, &pstyle))
		return 0;
	if (pstyle == PARTITION_STYLE_GPT)
		return 1;
	if (pstyle == PARTITION_STYLE_RAW)
		return 0;
	mounted = 0;
	h = FindFirstVolume (volname, sizeof volname / sizeof (TCHAR));
	while (h && !mounted) {
		HANDLE d;
		if (volname[_tcslen (volname) - 1] == '\\')
			volname[_tcslen (volname) - 1] = 0;
		d = CreateFile (volname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		hdf_log2 (_T("volname='%s' %08x\n"), volname, d);
		if (d != INVALID_HANDLE_VALUE) {
			DWORD isntfs, outsize, written;
			isntfs = 0;
			if (DeviceIoControl (d, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &written, NULL)) {
				VOLUME_DISK_EXTENTS *vde;
				NTFS_VOLUME_DATA_BUFFER ntfs;
				hdf_log2 (_T("FSCTL_IS_VOLUME_MOUNTED returned is mounted\n"));
				if (DeviceIoControl (d, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfs, sizeof ntfs, &written, NULL)) {
					isntfs = 1;
				}
				hdf_log2 (_T("FSCTL_GET_NTFS_VOLUME_DATA returned %d\n"), isntfs);
				outsize = sizeof (VOLUME_DISK_EXTENTS) + sizeof (DISK_EXTENT) * 32;
				vde = (VOLUME_DISK_EXTENTS*)xmalloc (uae_u8, outsize);
				if (DeviceIoControl (d, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
					int i;
					hdf_log2 (_T("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned %d extents\n"), vde->NumberOfDiskExtents);
					for (i = 0; i < vde->NumberOfDiskExtents; i++) {
						TCHAR pdrv[MAX_DPATH];
						HANDLE ph;
						_stprintf (pdrv, _T("\\\\.\\PhysicalDrive%d"), vde->Extents[i].DiskNumber);
						ph = CreateFile (pdrv, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
							NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						hdf_log2 (_T("PhysicalDrive%d: Extent %d Start=%I64X Len=%I64X\n"), i,
							vde->Extents[i].DiskNumber, vde->Extents[i].StartingOffset.QuadPart, vde->Extents[i].ExtentLength.QuadPart);
						if (ph != INVALID_HANDLE_VALUE) {
							DWORD sign2;
							if (getsignfromhandle (ph, &sign2, &pstyle)) {
								if (sign == sign2 && pstyle == PARTITION_STYLE_MBR)
									mounted = isntfs ? -1 : 1;
							}
							CloseHandle (ph);
						}
					}
				} else {
					hdf_log2 (_T("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned %08x\n"), GetLastError ());
				}
			} else {
				hdf_log2 (_T("FSCTL_IS_VOLUME_MOUNTED returned not mounted\n"));
			}
			CloseHandle (d);
		} else {
			write_log (_T("'%s': %d\n"), volname, GetLastError ());
		}
		if (!FindNextVolume (h, volname, sizeof volname / sizeof (TCHAR)))
			break;
	}
	FindVolumeClose (h);
	hdf_log2 (_T("\n"));
	return mounted;
}

#define CA "Commodore\0Amiga\0"
static int safetycheck (HANDLE h, const TCHAR *name, uae_u64 offset, uae_u8 *buf, int blocksize)
{
	uae_u64 origoffset = offset;
	int i, j, blocks = 63, empty = 1;
	DWORD outlen;

	for (j = 0; j < blocks; j++) {
		LARGE_INTEGER fppos;
		fppos.QuadPart = offset;
		if (SetFilePointer (h, fppos.LowPart, &fppos.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
			write_log (_T("hd ignored, SetFilePointer failed, error %d\n"), GetLastError ());
			return 1;
		}
		memset (buf, 0xaa, blocksize);
		ReadFile (h, buf, blocksize, &outlen, NULL);
		if (outlen != blocksize) {
			write_log (_T("hd ignored, read error %d!\n"), GetLastError ());
			return 2;
		}
		if (j == 0 && offset > 0)
			return -5;
		if (j == 0 && buf[0] == 0x39 && buf[1] == 0x10 && buf[2] == 0xd3 && buf[3] == 0x12) {
			// ADIDE "CPRM" hidden block..
			if (do_rdbdump)
				rdbdump (h, offset, buf, blocksize);
			write_log (_T("hd accepted (adide rdb detected at block %d)\n"), j);
			return -3;
		}
		if (!memcmp (buf, "RDSK", 4) || !memcmp (buf, "DRKS", 4)) {
			if (do_rdbdump)
				rdbdump (h, offset, buf, blocksize);
			write_log (_T("hd accepted (rdb detected at block %d)\n"), j);
			return -1;
		}

		if (!memcmp (buf + 2, "CIS@", 4) && !memcmp (buf + 16, CA, strlen (CA))) {
			if (do_rdbdump)
				rdbdump (h, offset, NULL, blocksize);
			write_log (_T("hd accepted (PCMCIA RAM)\n"));
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
		mounted = ismounted (name, h);
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
		return -6;
		//if (harddrive_dangerous == 0x1234dead)
		//	return -6;
		//write_log (_T("hd ignored, not empty and no RDB detected or Windows mounted\n"));
		//return 0;
	}
	if (do_rdbdump > 1)
		rdbdump (h, origoffset, NULL, blocksize);
	write_log (_T("hd accepted (empty)\n"));
	return -9;
}


static void trim (TCHAR *s)
{
	while(_tcslen(s) > 0 && s[_tcslen(s) - 1] == ' ')
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

static TCHAR *hdz[] = { _T("hdz"), _T("zip"), _T("rar"), _T("7z"), NULL };

#if 0
static void getserial (HANDLE h)
{
	DWORD outsize, written;
	DISK_GEOMETRY_EX *out;
	VOLUME_DISK_EXTENTS *vde;

	DWORD serial, mcl, fsflags;
	if (GetVolumeInformationByHandleW (h, NULL, 0, &serial, &mcl, &fsflags, NULL, 0)) {
	}

	outsize = sizeof (DISK_GEOMETRY_EX) + 10 * (sizeof (DISK_DETECTION_INFO) + sizeof (DISK_PARTITION_INFO));
	out = xmalloc (outsize);
	if (DeviceIoControl (h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, out, outsize, &written, NULL)) {
		DISK_DETECTION_INFO *ddi = DiskGeometryGetDetect (out);
		DISK_PARTITION_INFO *dpi = DiskGeometryGetPartition (out);
		write_log (_T(""));
	}
	xfree (out);


	outsize = sizeof (VOLUME_DISK_EXTENTS) + sizeof (DISK_EXTENT) * 10;
	vde = xmalloc (outsize);
	if (DeviceIoControl (h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
		if (vde->NumberOfDiskExtents > 0)
			write_log(_T("%d\n"), vde->Extents[0].DiskNumber);
	}
	xfree (vde);
}
#endif

#if 0
static void queryidentifydevice (struct hardfiledata *hfd)
{
	DWORD r, size;
	uae_u8 *b;
	ATA_PASS_THROUGH_EX *ata;

	size = sizeof (ATA_PASS_THROUGH_EX) + 512;
	b = xcalloc (uae_u8, size);
	ata = (ATA_PASS_THROUGH_EX*)b;

	ata->Length = sizeof ata;
	ata->DataTransferLength = 512;
	ata->TimeOutValue = 10;
	ata->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
	ata->CurrentTaskFile[6] = 0xec;
	ata->DataBufferOffset = ata->Length;
	
	if (!DeviceIoControl (hfd->handle->h, IOCTL_ATA_PASS_THROUGH, b, size, b, size, &r, NULL)) {
		write_log (_T("IOCTL_ATA_PASS_THROUGH_DIRECT Identify Device failed %d\n"), GetLastError ());
	}
}
#endif

static int getstorageproperty (PUCHAR outBuf, int returnedLength, struct uae_driveinfo *udi, int ignoreduplicates);

static bool getdeviceinfo (HANDLE hDevice, struct uae_driveinfo *udi)
{
	DISK_GEOMETRY dg;
	GET_LENGTH_INFORMATION gli;
	DWORD returnedLength;
	bool geom_ok = true, gli_ok;
	UCHAR outBuf[20000];
	DRIVE_LAYOUT_INFORMATION *dli;
	STORAGE_PROPERTY_QUERY query;
	DWORD status;
	TCHAR devname[MAX_DPATH];
	int amipart = -1;

	udi->bytespersector = 512;

	_tcscpy (devname, udi->device_name + 1);

	if (devname[0] == ':' && devname[1] == 'P' && devname[2] == '#' &&
		(devname[4] == '_' || devname[5] == '_')) {
		TCHAR c1 = devname[3];
		TCHAR c2 = devname[4];
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

	if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, (void*)&dg, sizeof (dg), &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (isnomediaerr (err)) {
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
	gli_ok = true;
	if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, (void*)&gli, sizeof (gli), &returnedLength, NULL)) {
		gli_ok = false;
		write_log (_T("IOCTL_DISK_GET_LENGTH_INFO failed with error code %d.\n"), GetLastError());
	} else {
		write_log (_T("IOCTL_DISK_GET_LENGTH_INFO returned size: %I64d (0x%I64x)\n"), gli.Length.QuadPart, gli.Length.QuadPart);
	}
	if (geom_ok == 0 && gli_ok == 0) {
		write_log (_T("Can't detect size of device\n"));
		return false;
	}
	if (geom_ok && dg.BytesPerSector != udi->bytespersector)
		return false;
	udi->size = gli.Length.QuadPart;

	// check for amithlon partitions, if any found = quick mount not possible
	status = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0,
		&outBuf, sizeof (outBuf), &returnedLength, NULL);
	if (!status)
		return true;
	dli = (DRIVE_LAYOUT_INFORMATION*)outBuf;
	if (!dli->PartitionCount)
		return true;
	bool partfound = false;
	for (int i = 0; i < dli->PartitionCount; i++) {
		PARTITION_INFORMATION *pi = &dli->PartitionEntry[i];
		if (pi->PartitionType == PARTITION_ENTRY_UNUSED)
			continue;
		if (pi->RecognizedPartition == 0)
			continue;
		if (pi->PartitionType != 0x76 && pi->PartitionType != 0x30)
			continue;
		if (i == amipart) {
			udi->offset = pi->StartingOffset.QuadPart;
			udi->size = pi->PartitionLength.QuadPart;
		}
	}
	if (amipart >= 0)
		return false;
	return true;
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
	hfd->virtual_size = 0;
	hfd->virtual_rdb = NULL;
	if (!hfd->cache) {
		write_log (_T("VirtualAlloc(%d) failed, error %d\n"), CACHE_SIZE, GetLastError ());
		goto end;
	}
	hfd->handle = xcalloc (struct hardfilehandle, 1);
	hfd->handle->h = INVALID_HANDLE_VALUE;
	hfd_log (_T("hfd attempting to open: '%s'\n"), name);
	if (name[0] == ':') {
		int drvnum = -1;
		TCHAR *p = _tcschr (name + 1, ':');
		if (p) {
			*p++ = 0;
			if (!drives_enumerated) {
				// do not scan for drives if open succeeds and it is a harddrive
				// to prevent spinup of sleeping drives
				h = CreateFile (p,
					GENERIC_READ,
					FILE_SHARE_READ,
					NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
				DWORD err = GetLastError ();
				if (h == INVALID_HANDLE_VALUE && err == ERROR_FILE_NOT_FOUND)
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
			DWORD r;
			hfd->flags = HFD_FLAGS_REALDRIVE;
			if (udi) {
				if (udi->nomedia)
					hfd->drive_empty = -1;
				if (udi->readonly)
					hfd->ci.readonly = 1;
			}
			flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
			h = CreateFile (udi->device_path,
				GENERIC_READ | (hfd->ci.readonly ? 0 : GENERIC_WRITE),
				FILE_SHARE_READ | (hfd->ci.readonly ? 0 : FILE_SHARE_WRITE),
				NULL, OPEN_EXISTING, flags, NULL);
			hfd->handle->h = h;
			if (h == INVALID_HANDLE_VALUE && !hfd->ci.readonly) {
				DWORD err = GetLastError ();
				if (err == ERROR_WRITE_PROTECT || err == ERROR_SHARING_VIOLATION) {
					h = CreateFile (udi->device_path,
						GENERIC_READ,
						FILE_SHARE_READ,
						NULL, OPEN_EXISTING, flags, NULL);
					if (h != INVALID_HANDLE_VALUE)
						hfd->ci.readonly = true;
				}
			}

			if (h == INVALID_HANDLE_VALUE) {
				DWORD err = GetLastError ();
				if (err == ERROR_WRITE_PROTECT)
					ret = -2;
				if (err == ERROR_SHARING_VIOLATION)
					ret = -1;
				goto end;
			}
			if (!DeviceIoControl (h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &r, NULL))
				write_log (_T("WARNING: '%s' FSCTL_ALLOW_EXTENDED_DASD_IO returned %d\n"), name, GetLastError ());

			//queryidentifydevice (hfd);
			_tcsncpy (hfd->vendor_id, udi->vendor_id, 8);
			_tcsncpy (hfd->product_id, udi->product_id, 16);
			_tcsncpy (hfd->product_rev, udi->product_rev, 4);
			hfd->offset = udi->offset;
			hfd->physsize = hfd->virtsize = udi->size;
			hfd->ci.blocksize = udi->bytespersector;
			if (udi->partitiondrive)
				hfd->flags |= HFD_FLAGS_REALDRIVEPARTITION;
			if (hfd->offset == 0 && !hfd->drive_empty) {
				int sf = safetycheck (hfd->handle->h, udi->device_path, 0, hfd->cache, hfd->ci.blocksize);
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
					if (!DeviceIoControl(h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &r, NULL))
						write_log (_T("WARNING: '%s' FSCTL_ALLOW_EXTENDED_DASD_IO returned %d\n"), name, GetLastError ());
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
			hfd->handle_valid = HDF_HANDLE_WIN32;
			hfd->emptyname = my_strdup (name);

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
			if (err == ERROR_WRITE_PROTECT || err == ERROR_SHARING_VIOLATION) {
				h = CreateFile (name, GENERIC_READ, FILE_SHARE_READ, NULL,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
				if (h != INVALID_HANDLE_VALUE)
					hfd->ci.readonly = true;
			}
		}
		
		hfd->handle->h = h;
		i = _tcslen (name) - 1;
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
			hfd->handle_valid = HDF_HANDLE_WIN32;
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
	if (!h->zfile && h->h != INVALID_HANDLE_VALUE)
		CloseHandle (h->h);
	if (h->zfile && h->zf)
		zfile_fclose (h->zf);
	h->zf = NULL;
	h->h = INVALID_HANDLE_VALUE;
	h->zfile = 0;
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
	xfree(hfd->virtual_rdb);
	hfd->virtual_rdb = 0;
	hfd->virtual_size = 0;
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
	if (shfd->handle_valid == HDF_HANDLE_WIN32) {
		HANDLE duphandle;
		if (!DuplicateHandle (GetCurrentProcess (), shfd->handle->h, GetCurrentProcess () , &duphandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
			return 0;
		dhfd->handle->h = duphandle;
		dhfd->handle_valid = HDF_HANDLE_WIN32;
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

static int hdf_seek (struct hardfiledata *hfd, uae_u64 offset)
{
	DWORD ret;

	if (hfd->handle_valid == 0) {
		gui_message (_T("hd: hdf handle is not valid. bug."));
		abort();
	}
	if (offset >= hfd->physsize - hfd->virtual_size) {
		gui_message (_T("hd: tried to seek out of bounds! (%I64X >= %I64X - %I64X)\n"), offset, hfd->physsize, hfd->virtual_size);
		abort ();
	}
	offset += hfd->offset;
	if (offset & (hfd->ci.blocksize - 1)) {
		gui_message (_T("hd: poscheck failed, offset=%I64X not aligned to blocksize=%d! (%I64X & %04X = %04X)\n"),
			offset, hfd->ci.blocksize, offset, hfd->ci.blocksize, offset & (hfd->ci.blocksize - 1));
		abort ();
	}
	if (hfd->handle_valid == HDF_HANDLE_WIN32) {
		LARGE_INTEGER fppos;
		fppos.QuadPart = offset;
		ret = SetFilePointer (hfd->handle->h, fppos.LowPart, &fppos.HighPart, FILE_BEGIN);
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
	uae_s64 pos;

	if (hfd->handle_valid == HDF_HANDLE_WIN32) {
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
			hdf_seek (hfd, offset & ~mask);
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
			hdf_seek (hfd, offset);
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
			hdf_seek (hfd, offset);
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

static int hdf_read_2 (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
	DWORD outlen = 0;
	int coffset;

	if (offset == 0)
		hfd->cache_valid = 0;
	coffset = isincache (hfd, offset, len);
	if (coffset >= 0) {
		memcpy (buffer, hfd->cache + coffset, len);
		return len;
	}
	hfd->cache_offset = offset;
	if (offset + CACHE_SIZE > hfd->offset + (hfd->physsize - hfd->virtual_size))
		hfd->cache_offset = hfd->offset + (hfd->physsize - hfd->virtual_size) - CACHE_SIZE;
	hdf_seek (hfd, hfd->cache_offset);
	poscheck (hfd, CACHE_SIZE);
	if (hfd->handle_valid == HDF_HANDLE_WIN32)
		ReadFile (hfd->handle->h, hfd->cache, CACHE_SIZE, &outlen, NULL);
	else if (hfd->handle_valid == HDF_HANDLE_ZFILE)
		outlen = zfile_fread (hfd->cache, 1, CACHE_SIZE, hfd->handle->zf);
	hfd->cache_valid = 0;
	if (outlen != CACHE_SIZE)
		return 0;
	hfd->cache_valid = 1;
	coffset = isincache (hfd, offset, len);
	if (coffset >= 0) {
		memcpy (buffer, hfd->cache + coffset, len);
		return len;
	}
	write_log (_T("hdf_read: cache bug! offset=%I64d len=%d\n"), offset, len);
	hfd->cache_valid = 0;
	return 0;
}

int hdf_read_target (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
	int got = 0;
	uae_u8 *p = (uae_u8*)buffer;

	if (hfd->drive_empty)
		return 0;
	if (offset < hfd->virtual_size) {
		uae_u64 len2 = offset + len <= hfd->virtual_size ? len : hfd->virtual_size - offset;
		if (!hfd->virtual_rdb)
			return 0;
		memcpy (buffer, hfd->virtual_rdb + offset, len2);
		return len2;
	}
	offset -= hfd->virtual_size;
	while (len > 0) {
		int maxlen;
		DWORD ret;
		if (hfd->physsize < CACHE_SIZE) {
			hfd->cache_valid = 0;
			hdf_seek (hfd, offset);
			poscheck (hfd, len);
			if (hfd->handle_valid == HDF_HANDLE_WIN32) {
				ReadFile (hfd->handle->h, hfd->cache, len, &ret, NULL);
				memcpy (buffer, hfd->cache, ret);
			} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
				ret = zfile_fread (buffer, 1, len, hfd->handle->zf);
			}
			maxlen = len;
		} else {
			maxlen = len > CACHE_SIZE ? CACHE_SIZE : len;
			ret = hdf_read_2 (hfd, p, offset, maxlen);
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

static int hdf_write_2 (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
	DWORD outlen = 0;

	if (hfd->ci.readonly)
		return 0;
	if (hfd->dangerous)
		return 0;
	hfd->cache_valid = 0;
	hdf_seek (hfd, offset);
	poscheck (hfd, len);
	memcpy (hfd->cache, buffer, len);
	if (hfd->handle_valid == HDF_HANDLE_WIN32) {
		TCHAR *name = hfd->emptyname == NULL ? _T("<unknown>") : hfd->emptyname;
		if (offset == 0) {
			if (!hfd->handle->firstwrite && (hfd->flags & HFD_FLAGS_REALDRIVE) && !(hfd->flags & HFD_FLAGS_REALDRIVEPARTITION)) {
				hfd->handle->firstwrite = true;
				if (ismounted (hfd->ci.devname, hfd->handle->h)) {
					gui_message (_T("\"%s\"\n\nBlock zero write attempt but drive has one or more mounted PC partitions or WinUAE does not have Administrator privileges. Erase the drive or unmount all PC partitions first."), name);
					hfd->ci.readonly = true;
					return 0;
				}
			}
		}
		WriteFile (hfd->handle->h, hfd->cache, len, &outlen, NULL);
		if (offset == 0) {
			DWORD outlen2;
			uae_u8 *tmp;
			int tmplen = 512;
			tmp = (uae_u8*)VirtualAlloc (NULL, tmplen, MEM_COMMIT, PAGE_READWRITE);
			if (tmp) {
				memset (tmp, 0xa1, tmplen);
				hdf_seek (hfd, offset);
				ReadFile (hfd->handle->h, tmp, tmplen, &outlen2, NULL);
				if (memcmp (hfd->cache, tmp, tmplen) != 0 || outlen != len)
					gui_message (_T("\"%s\"\n\nblock zero write failed! Make sure WinUAE has Windows Administrator privileges."), name);
				VirtualFree (tmp, 0, MEM_RELEASE);
			}
		}
	} else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
		outlen = zfile_fwrite (hfd->cache, 1, len, hfd->handle->zf);
	}
	return outlen;
}

int hdf_write_target (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
	int got = 0;
	uae_u8 *p = (uae_u8*)buffer;

	if (hfd->drive_empty)
		return 0;
	if (offset < hfd->virtual_size)
		return len;
	offset -= hfd->virtual_size;
	while (len > 0) {
		int maxlen = len > CACHE_SIZE ? CACHE_SIZE : len;
		int ret = hdf_write_2 (hfd, p, offset, maxlen);
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

static int getstorageproperty (PUCHAR outBuf, int returnedLength, struct uae_driveinfo *udi, int ignoreduplicates)
{
	PSTORAGE_DEVICE_DESCRIPTOR devDesc;
	TCHAR orgname[1024];
	PUCHAR p;
	int i, j;
	int size;

	devDesc = (PSTORAGE_DEVICE_DESCRIPTOR) outBuf;
	size = devDesc->Version;
	p = (PUCHAR) outBuf;
	if (offsetof(STORAGE_DEVICE_DESCRIPTOR, CommandQueueing) > size) {
		write_log (_T("too short STORAGE_DEVICE_DESCRIPTOR only %d bytes\n"), size);
		return -2;
	}
	if (devDesc->DeviceType != INQ_DASD && devDesc->DeviceType != INQ_ROMD && devDesc->DeviceType != INQ_OPTD) {
		write_log (_T("not a direct access device, ignored (type=%d)\n"), devDesc->DeviceType);
		return -2;
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, VendorIdOffset) && devDesc->VendorIdOffset && p[devDesc->VendorIdOffset]) {
		j = 0;
		for (i = devDesc->VendorIdOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->vendor_id[j++] = p[i];
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductIdOffset) && devDesc->ProductIdOffset && p[devDesc->ProductIdOffset]) {
		j = 0;
		for (i = devDesc->ProductIdOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->product_id[j++] = p[i];
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductRevisionOffset) && devDesc->ProductRevisionOffset && p[devDesc->ProductRevisionOffset]) {
		j = 0;
		for (i = devDesc->ProductRevisionOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->product_rev[j++] = p[i];
	}
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, SerialNumberOffset) && devDesc->SerialNumberOffset && p[devDesc->SerialNumberOffset]) {
		j = 0;
		for (i = devDesc->SerialNumberOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
			udi->product_serial[j++] = p[i];
	}
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
	write_log (_T("device id string: '%s'\n"), udi->device_name);
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

static BOOL GetDevicePropertyFromName(const TCHAR *DevicePath, DWORD Index, DWORD *index2, uae_u8 *buffer, int ignoreduplicates)
{
	int i, nosp, geom_ok;
	int ret = -1;
	STORAGE_PROPERTY_QUERY query;
	DRIVE_LAYOUT_INFORMATION		*dli;
	struct uae_driveinfo *udi;
	TCHAR orgname[1024];
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	UCHAR outBuf[20000];
	DISK_GEOMETRY			dg;
	GET_LENGTH_INFORMATION		gli;
	int gli_ok;
	BOOL                                status;
	ULONG                               length = 0, returned = 0, returnedLength;
	BOOL showonly = FALSE;
	struct uae_driveinfo tmpudi = { 0 };

	udi = &tmpudi;
	int udiindex = *index2;

	//
	// Now we have the device path. Open the device interface
	// to send Pass Through command

	_tcscpy (udi->device_path, DevicePath);
	write_log (_T("opening device '%s'\n"), udi->device_path);
	hDevice = CreateFile(
		udi->device_path,    // device interface name
		GENERIC_READ,       // dwDesiredAccess
		FILE_SHARE_READ | FILE_SHARE_WRITE, // dwShareMode
		NULL,                               // lpSecurityAttributes
		OPEN_EXISTING,                      // dwCreationDistribution
		0,                                  // dwFlagsAndAttributes
		NULL                                // hTemplateFile
		);

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
		showonly = TRUE;
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
	}
	udi = &uae_drives[udiindex < 0 ? *index2 : udiindex];
	memcpy (udi, &tmpudi, sizeof (struct uae_driveinfo));


	_tcscpy (orgname, udi->device_name);
	udi->bytespersector = 512;
	geom_ok = 1;
	if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, (void*)&dg, sizeof (dg), &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (isnomediaerr (err)) {
			udi->nomedia = 1;
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

	if (showonly) {
		udi->dangerous = -10;
		udi->readonly = -1;
		goto amipartfound;
	}

	gli_ok = 1;
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

	udi->offset = 0;
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
	if (udi->size == 0) {
		write_log (_T("device size is zero!\n"));
		ret = 1;
		goto end;
	}
	write_log (_T("device size %I64d (0x%I64x) bytes\n"), udi->size, udi->size);
	trim (orgname);

	memset (outBuf, 0, sizeof (outBuf));
	status = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0,
		&outBuf, sizeof (outBuf), &returnedLength, NULL);
	if (!status) {
		DWORD err = GetLastError();
		write_log (_T("IOCTL_DISK_GET_DRIVE_LAYOUT failed with error code %d.\n"), err);
		if (err != ERROR_INVALID_FUNCTION) {
			ret = 1;
			goto end;
		}
		goto amipartfound;
	}
	dli = (DRIVE_LAYOUT_INFORMATION*)outBuf;
	if (dli->PartitionCount) {
		struct uae_driveinfo *udi2 = udi;
		int nonzeropart = 0;
		int gotpart = 0;
		int safepart = 0;
		write_log (_T("%d MBR partitions found\n"), dli->PartitionCount);
		for (i = 0; i < dli->PartitionCount && (*index2) < MAX_FILESYSTEM_UNITS; i++) {
			PARTITION_INFORMATION *pi = &dli->PartitionEntry[i];
			if (pi->PartitionType == PARTITION_ENTRY_UNUSED)
				continue;
			write_log (_T("%d: num: %d type: %02X offset: %I64d size: %I64d, "), i,
				pi->PartitionNumber, pi->PartitionType, pi->StartingOffset.QuadPart, pi->PartitionLength.QuadPart);
			if (pi->RecognizedPartition == 0) {
				write_log (_T("unrecognized\n"));
				continue;
			}
			nonzeropart++;
			if (pi->PartitionType != 0x76 && pi->PartitionType != 0x30) {
				write_log (_T("type not 0x76 or 0x30\n"));
				continue;
			}
			memmove (udi, udi2, sizeof (*udi));
			udi->device_name[0] = 0;
			udi->offset = pi->StartingOffset.QuadPart;
			udi->size = pi->PartitionLength.QuadPart;
			write_log (_T("used\n"));
			_stprintf (udi->device_name, _T(":P#%d_%s"), pi->PartitionNumber, orgname);
			udi->dangerous = -5;
			udi->partitiondrive = true;
			udi++;
			(*index2)++;
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
	} else {
		write_log (_T("no MBR partition table detected, checking for RDB\n"));
	}
	if (udi->offset == 0) {
		udi->dangerous = safetycheck (hDevice, udi->device_path, 0, buffer, dg.BytesPerSector);
		if (udi->dangerous > 0)
			goto end;
	}
amipartfound:
	_stprintf (udi->device_name, _T(":%s"), orgname);
	_stprintf (udi->device_full_path, _T("%s:%s"), udi->device_name, udi->device_path);
	if (udiindex < 0) {
		int cnt = 1;
		int off = _tcslen (udi->device_name);
		while (isharddrive (udi->device_name) >= 0) {
			udi->device_name[off] = '_';
			udi->device_name[off + 1] = cnt + '0';
			udi->device_name[off + 2] = 0;
			cnt++;
		}
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

TCHAR *hdf_getnameharddrive (int index, int flags, int *sectorsize, int *dangerousdrive)
{
	static TCHAR name[512];
	TCHAR tmp[32];
	uae_u64 size = uae_drives[index].size;
	int nomedia = uae_drives[index].nomedia;
	TCHAR *dang = _T("?");
	TCHAR *rw = _T("RW");
	bool noaccess = false;

	if (dangerousdrive)
		*dangerousdrive = 0;
	switch (uae_drives[index].dangerous)
	{
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
			_stprintf (name, _T("[ACCESS DENIED] %s"), uae_drives[index].device_name + 1);
			return name;
		}
	} else {
		if (nomedia) {
			dang = _T("[NO MEDIA]");
			if (dangerousdrive)
				*dangerousdrive &= ~1;
		}

		if (uae_drives[index].readonly) {
			rw = _T("RO");
			if (dangerousdrive && !nomedia)
				*dangerousdrive |= 2;
		}

		if (sectorsize)
			*sectorsize = uae_drives[index].bytespersector;
		if (flags & 1) {
			if (nomedia) {
				_tcscpy (tmp, _T("N/A"));
			} else {
				if (size >= 1024 * 1024 * 1024)
					_stprintf (tmp, _T("%.1fG"), ((double)(uae_u32)(size / (1024 * 1024))) / 1024.0);
				else if (size < 10 * 1024 * 1024)
					_stprintf (tmp, _T("%lldK"), size / 1024);
				else
					_stprintf (tmp, _T("%.1fM"), ((double)(uae_u32)(size / (1024))) / 1024.0);
			}
			_stprintf (name, _T("%10s [%s,%s] %s"), dang, tmp, rw, uae_drives[index].device_name + 1);
			return name;
		}
	}
	if (flags & 4)
		return uae_drives[index].device_full_path;
	if (flags & 2)
		return uae_drives[index].device_path;
	return uae_drives[index].device_name;
}

static int hmc (struct hardfiledata *hfd)
{
	uae_u8 *buf = xmalloc (uae_u8, hfd->ci.blocksize);
	DWORD ret, got, err, status;
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
		for (j = 0; j < currprefs.mountitems; j++) {
			if (currprefs.mountconfig[j].configoffset == i) {
				hmc_check (hfd, &currprefs.mountconfig[j], &rescanned, &reopen, &gotinsert, drvname, inserted);
				break;
			}
		}
	}
	for (i = 0; i < currprefs.mountitems; i++) {
		extern struct hd_hardfiledata *pcmcia_sram;
		int reopen = 0;
		struct uaedev_config_data *uci = &currprefs.mountconfig[i];
		if (uci->ci.controller_type == HD_CONTROLLER_TYPE_PCMCIA_SRAM) {
			hmc_check (&pcmcia_sram->hfd, uci, &rescanned, &reopen, &gotinsert, drvname, inserted);
		}
	}

	//write_log (_T("win32_hardfile_media_change returned %d\n"), gotinsert);
	return gotinsert;
}

static int progressdialogreturn;
static int progressdialogactive;

static INT_PTR CALLBACK ProgressDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		progressdialogactive = 0;
		return TRUE;
	case WM_CLOSE:
		if (progressdialogreturn < 0)
			progressdialogreturn = 0;
		return TRUE;
	case WM_INITDIALOG:
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			progressdialogreturn = 0;
			return TRUE;
		}
		break;
	}
	return FALSE;
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
	int pct, cnt;

	cache = VirtualAlloc (NULL, COPY_CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	if (!cache)
		goto err;
	h = CreateFile (uae_drives[idx].device_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
	if (h == INVALID_HANDLE_VALUE)
		goto err;
	size = uae_drives[idx].size;
	path[0] = 0;
	DiskSelection (hDlg, IDC_PATH_NAME, 3, p, 0);
	GetDlgItemText (hDlg, IDC_PATH_NAME, path, MAX_DPATH);
	if (*path == 0)
		goto err;
	hdst = CreateFile (path, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
	if (hdst == INVALID_HANDLE_VALUE)
		goto err;
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
	progressdialogreturn = -1;
	progressdialogactive = 1;
	hwnd = CreateDialog (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_PROGRESSBAR), hDlg, ProgressDialogProc);
	if (hwnd == NULL)
		goto err;
	hwndprogress = GetDlgItem (hwnd, IDC_PROGRESSBAR);
	hwndprogresstxt = GetDlgItem (hwnd, IDC_PROGRESSBAR_TEXT);
	ShowWindow (hwnd, SW_SHOW);
	pct = 0;
	cnt = 1000;
	sizecnt = 0;
	written = 0;
	for (;;) {
		if (progressdialogreturn >= 0)
			break;
		if (cnt > 0) {
			SendMessage (hwndprogress, PBM_SETPOS, (WPARAM)pct, 0);
			_stprintf (tmp, _T("%dM / %dM (%d%%)"), (int)(written >> 20), (int)(size >> 20), pct);
			SendMessage (hwndprogresstxt, WM_SETTEXT, 0, (LPARAM)tmp);
			while (PeekMessage (&msg, hwnd, 0, 0, PM_REMOVE)) {
				if (!IsDialogMessage (hwnd, &msg)) {
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
			}
			cnt = 0;
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
		get = COPY_CACHE_SIZE;
		if (sizecnt + get > size)
			get = size - sizecnt;
		if (!ReadFile (h, cache, get, &got, NULL)) {
			progressdialogreturn = 4;
			break;
		}
		if (get != got) {
			progressdialogreturn = 5;
			break;
		}
		if (got > 0) {
			if (written + got > size)
				got = size - written;
			if (!WriteFile (hdst, cache, got, &gotdst, NULL))  {
				progressdialogreturn = 5;
				break;
			}
			written += gotdst;
			if (written == size)
				break;
		}
		if (got != COPY_CACHE_SIZE) {
			progressdialogreturn = 1;
			break;
		}
		if (got != gotdst) {
			progressdialogreturn = 2;
			break;
		}
		cnt++;
		sizecnt += got;
		pct = (int)(sizecnt * 100 / size);
	}
	if (progressdialogactive) {
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
	DWORD err = GetLastError ();
	LPWSTR pBuffer = NULL;
	WIN32GUI_LoadUIString (IDS_HDCLONE_FAIL, tmp, MAX_DPATH);
	if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&pBuffer, 0, NULL))
		pBuffer = NULL;
	_stprintf (tmp2, tmp, progressdialogreturn, err, pBuffer ? _T("<unknown>") : pBuffer);
	gui_message (tmp2);
	LocalFree (pBuffer);

ok:
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle (h);
	if (cache)
		VirtualFree (cache, 0, MEM_RELEASE);
	if (hdst != INVALID_HANDLE_VALUE)
		CloseHandle (hdst);
	return retcode;
}
