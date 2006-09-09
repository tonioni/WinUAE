#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x500

#include "sysconfig.h"
#include "sysdeps.h"

#include <shellapi.h>
#include "resource.h"

#include "threaddep/thread.h"
#include "filesys.h"
#include "blkdev.h"
#include "win32gui.h"
#include "zfile.h"

#define hfd_log write_log

#ifdef WINDDK
#include <devioctl.h>  
#include <ntddstor.h>
#include <winioctl.h>
#include <initguid.h>   // Guid definition
#include <devguid.h>    // Device guids
#include <setupapi.h>   // for SetupDiXxx functions.
#include <cfgmgr32.h>   // for SetupDiXxx functions.
#endif

struct uae_driveinfo {
    uae_u64 offset2;
    uae_u64 size2;
    char vendor_id[128];
    char product_id[128];
    char product_rev[128];
    char product_serial[128];
    char device_name[256];
    char device_path[2048];
    uae_u64 size;
    uae_u64 offset;
    int bytespersector;
};

#define HDF_HANDLE_WIN32 1
#define HDF_HANDLE_ZFILE 2

#define CACHE_SIZE 16384
#define CACHE_FLUSH_TIME 5

/* safety check: only accept drives that:
 * - contain RDSK in block 0
 * - block 0 is zeroed
 */

int harddrive_dangerous, do_rdbdump;
static int num_drives;
static struct uae_driveinfo uae_drives[MAX_FILESYSTEM_UNITS];

static void rdbdump (HANDLE *h, uae_u64 offset, uae_u8 *buf, int blocksize)
{
    static int cnt = 1;
    int i, blocks;
    char name[100];
    FILE *f;

    blocks = (buf[132] << 24) | (buf[133] << 16) | (buf[134] << 8) | (buf[135] << 0);
    if (blocks < 0 || blocks > 100000)
	return;
    sprintf (name, "rdb_dump_%d.rdb", cnt);
    f = fopen (name, "wb");
    if (!f)
	return;
    for (i = 0; i <= blocks; i++) {
        DWORD outlen, high;
        high = (DWORD)(offset >> 32);
	if (SetFilePointer (h, (DWORD)offset, &high, FILE_BEGIN) == INVALID_FILE_SIZE)
	    break;
        ReadFile (h, buf, blocksize, &outlen, NULL);
	fwrite (buf, 1, blocksize, f);
	offset += blocksize;
    }
    fclose (f);
    cnt++;
}

static int safetycheck (HANDLE *h, uae_u64 offset, uae_u8 *buf, int blocksize)
{
    int i, j, blocks = 63, empty = 1;
    DWORD outlen, high;

    for (j = 0; j < blocks; j++) {
	high = (DWORD)(offset >> 32);
	if (SetFilePointer (h, (DWORD)offset, &high, FILE_BEGIN) == INVALID_FILE_SIZE) {
	    write_log ("hd ignored, SetFilePointer failed, error %d\n", GetLastError());
	    return 0;
	}
	memset (buf, 0xaa, blocksize);
	ReadFile (h, buf, blocksize, &outlen, NULL);
	if (outlen != blocksize) {
	    write_log ("hd ignored, read error %d!\n", GetLastError());
	    return 0;
	}
	if (!memcmp (buf, "RDSK", 4)) {
	    if (do_rdbdump)
		rdbdump (h, offset, buf, blocksize);
	    write_log ("hd accepted (rdb detected at block %d)\n", j);
	    return 1;
	}
	if (j == 0) {
	    for (i = 0; i < blocksize; i++) {
		if (buf[i])
		    empty = 0;
	    }
	}
	offset += blocksize;
    }
    if (harddrive_dangerous != 0x1234dead) {
        if (!empty) {
	    write_log ("hd ignored, not empty and no RDB detected\n");
	    return 0;
	}
	write_log ("hd accepted (empty)\n");
	return 1;
    }
    gui_message_id (IDS_HARDDRIVESAFETYWARNING);
    return 2;
}

static void trim (char *s)
{
    while(strlen(s) > 0 && s[strlen(s) - 1] == ' ')
	s[strlen(s) - 1] = 0;
}

int isharddrive (char *name)
{
    int i;

    for (i = 0; i < num_drives; i++) {
	if (!strcmp (uae_drives[i].device_name, name))
	    return i;
    }
    return -1;
}

static char *hdz[] = { "hdz", "zip", "rar", "7z", NULL };

int hdf_open (struct hardfiledata *hfd, char *name)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD flags;
    int i;
    struct uae_driveinfo *udi;

    hdf_close (hfd);
    hfd->cache = VirtualAlloc (NULL, CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
    hfd->cache_valid = 0;
    if (!hfd->cache) {
	write_log ("VirtualAlloc(%d) failed, error %d\n", CACHE_SIZE, GetLastError());
	return 0;
    }
    hfd_log ("hfd open: '%s'\n", name);
    if (strlen (name) > 4 && !memcmp (name, "HD_", 3)) {
	hdf_init ();
	i = isharddrive (name);
	if (i >= 0) {
	    udi = &uae_drives[i];
	    hfd->flags = 1;
	    flags =  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
	    h = CreateFile (udi->device_path, GENERIC_READ | (hfd->readonly ? 0 : GENERIC_WRITE), FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, flags, NULL);
	    hfd->handle = h;
	    if (h == INVALID_HANDLE_VALUE) {
		hdf_close (hfd);
		return 0;
	    }
	    strncpy (hfd->vendor_id, udi->vendor_id, 8);
	    strncpy (hfd->product_id, udi->product_id, 16);
	    strncpy (hfd->product_rev, udi->product_rev, 4);
	    hfd->offset2 = hfd->offset = udi->offset;
	    hfd->size2 = hfd->size = udi->size;
	    if (hfd->offset != udi->offset2 || hfd->size != udi->size2) {
		gui_message ("Harddrive safety check: fatal memory corruption\n");
		abort ();
	    }
	    hfd->blocksize = udi->bytespersector;
	    if (hfd->offset == 0) {
		if (!safetycheck (hfd->handle, 0, hfd->cache, hfd->blocksize)) {
		    hdf_close (hfd);
		    return 0;
		}
	    }
	    hfd->handle_valid = HDF_HANDLE_WIN32;
	}
    } else {
	int zmode = 0;
        char *ext = strrchr (name, '.');
	if (ext != NULL) {
	    ext++;
	    for (i = 0; hdz[i]; i++) {
		if (!stricmp (ext, hdz[i]))
		    zmode = 1;
	    }
	}
	h = CreateFile (name, GENERIC_READ | (hfd->readonly ? 0 : GENERIC_WRITE), hfd->readonly ? FILE_SHARE_READ : 0, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
        hfd->handle = h;
        i = strlen (name) - 1;
	while (i >= 0) {
	    if ((i > 0 && (name[i - 1] == '/' || name[i - 1] == '\\')) || i == 0) {
		strcpy (hfd->vendor_id, "UAE");
		strncpy (hfd->product_id, name + i, 15);
		strcpy (hfd->product_rev, "0.3");
		break;
	    }
	    i--;
	}
	if (h != INVALID_HANDLE_VALUE) {
	    DWORD ret, low, high;
	    high = 0;
	    ret = SetFilePointer (h, 0, &high, FILE_END);
	    if (ret == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
		hdf_close (hfd);
		return 0;
	    }
	    low = GetFileSize (h, &high);
	    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
		hdf_close (hfd);
		return 0;
	    }
	    low &= ~(hfd->blocksize - 1);
	    hfd->size = hfd->size2 = ((uae_u64)high << 32) | low;
	    hfd->handle_valid = HDF_HANDLE_WIN32;
	    if (hfd->size < 64 * 1024 * 1024 && zmode) {
		write_log ("HDF '%s' re-opened in zfile-mode\n", name);
		CloseHandle (h);
		hfd->handle = h = zfile_fopen(name, hfd->readonly ? "rb" : "r+b");
		if (!h) {
		    hdf_close (hfd);
		    return 0;
		}
		zfile_fseek (h, 0, SEEK_END);
		hfd->size = hfd->size2 = zfile_ftell (h);
		zfile_fseek (h, 0, SEEK_SET);
		hfd->handle_valid = HDF_HANDLE_ZFILE;
	    }
	} else {
	    write_log ("HDF '%s' failed to open. error = %d\n", name, GetLastError ());
	}
    }
    hfd->handle = h;
    if (hfd->handle != INVALID_HANDLE_VALUE) {
	hfd_log ("HDF '%s' opened succesfully, handle=%p, mode=%d\n", name, hfd->handle, hfd->handle_valid);
	return 1;
    }
    hdf_close (hfd);
    return 0;
}

void hdf_close (struct hardfiledata *hfd)
{
    if (!hfd->handle_valid)
	return;
    hfd_log ("close handle=%p\n", hfd->handle);
    hfd->flags = 0;
    if (hfd->handle && hfd->handle != INVALID_HANDLE_VALUE) {
	if (hfd->handle_valid == HDF_HANDLE_WIN32)
	    CloseHandle (hfd->handle);
	else if(hfd->handle_valid == HDF_HANDLE_ZFILE)
	    zfile_fclose (hfd->handle);
    }
    hfd->handle = 0;
    hfd->handle_valid = 0;
    if (hfd->cache)
	VirtualFree (hfd->cache, 0, MEM_RELEASE);
    hfd->cache = 0;
    hfd->cache_valid = 0;
}

int hdf_dup (struct hardfiledata *dhfd, struct hardfiledata *shfd)
{
    if (!shfd->handle_valid)
	return 0;
    if (shfd->handle_valid == HDF_HANDLE_WIN32) {
        HANDLE duphandle;
	if (!DuplicateHandle (GetCurrentProcess(), shfd->handle, GetCurrentProcess() , &duphandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
	    return 0;
	dhfd->handle = duphandle;
	dhfd->handle_valid = HDF_HANDLE_WIN32;
    } else if (shfd->handle_valid == HDF_HANDLE_ZFILE) {
	struct zfile *zf;
	zf = zfile_dup (shfd->handle);
	if (!zf)
	    return 0;
	dhfd->handle = zf;
	dhfd->handle_valid = HDF_HANDLE_ZFILE;
    }
    dhfd->cache = VirtualAlloc (NULL, CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
    dhfd->cache_valid = 0;
    if (!dhfd->cache) {
	hdf_close (dhfd);
	return 0;
    }
    return 1;
}

static int hdf_seek (struct hardfiledata *hfd, uae_u64 offset)
{
    DWORD high, ret;

    if (hfd->handle_valid == 0) {
	gui_message ("hd: hdf handle is not valid. bug.");
	abort();
    }
    if (hfd->offset != hfd->offset2 || hfd->size != hfd->size2) {
	gui_message ("hd: memory corruption detected in seek");
	abort ();
    }
    if (offset >= hfd->size) {
	gui_message ("hd: tried to seek out of bounds! (%I64X >= %I64X)\n", offset, hfd->size);
	abort ();
    }
    offset += hfd->offset;
    if (offset & (hfd->blocksize - 1)) {
	gui_message ("hd: poscheck failed, offset not aligned to blocksize! (%I64X & %04.4X = %04.4X)\n", offset, hfd->blocksize, offset & (hfd->blocksize - 1));
	abort ();
    }
    if (hfd->handle_valid == HDF_HANDLE_WIN32) {
        high = (DWORD)(offset >> 32);
	ret = SetFilePointer (hfd->handle, (DWORD)offset, &high, FILE_BEGIN);
	if (ret == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
	    return -1;
    } else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
	zfile_fseek (hfd->handle, (long)offset, SEEK_SET);
    }
    return 0;
}

static void poscheck (struct hardfiledata *hfd, int len)
{
    DWORD high, ret, err;
    uae_u64 pos;

    if (hfd->offset != hfd->offset2 || hfd->size != hfd->size2) {
	gui_message ("hd: memory corruption detected in poscheck");
	abort ();
    }
    if (hfd->handle_valid == HDF_HANDLE_WIN32) {
	high = 0;
	ret = SetFilePointer (hfd->handle, 0, &high, FILE_CURRENT);
	err = GetLastError ();
	if (ret == INVALID_FILE_SIZE && err != NO_ERROR) {
	    gui_message ("hd: poscheck failed. seek failure, error %d", err);
	    abort ();
	}
        pos = ((uae_u64)high) << 32 | ret;
    } else if (hfd->handle_valid == HDF_HANDLE_ZFILE) {
	pos = zfile_ftell (hfd->handle);
    }
    if (len < 0) {
	gui_message ("hd: poscheck failed, negative length! (%d)", len);
	abort ();
    }
    if (pos < hfd->offset) {
	gui_message ("hd: poscheck failed, offset out of bounds! (%I64d < %I64d)", pos, hfd->offset);
	abort ();
    }
    if (pos >= hfd->offset + hfd->size || pos >= hfd->offset + hfd->size + len) {
	gui_message ("hd: poscheck failed, offset out of bounds! (%I64d >= %I64d, LEN=%d)", pos, hfd->offset + hfd->size, len);
	abort ();
    }
    if (pos & (hfd->blocksize - 1)) {
	gui_message ("hd: poscheck failed, offset not aligned to blocksize! (%I64X & %04.4X = %04.4X\n", pos, hfd->blocksize, pos & hfd->blocksize);
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

    bs = hfd->blocksize;
    mask = hfd->blocksize - 1;
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
	    if (outlen2 != hfd->blocksize)
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
    if (offset + CACHE_SIZE > hfd->offset + hfd->size)
	hfd->cache_offset = hfd->offset + hfd->size - CACHE_SIZE;
    hdf_seek (hfd, hfd->cache_offset);
    poscheck (hfd, CACHE_SIZE);
    if (hfd->handle_valid == HDF_HANDLE_WIN32)
	ReadFile (hfd->handle, hfd->cache, CACHE_SIZE, &outlen, NULL);
    else if (hfd->handle_valid == HDF_HANDLE_ZFILE)
	outlen = zfile_fread (hfd->cache, 1, CACHE_SIZE, hfd->handle);
    hfd->cache_valid = 0;
    if (outlen != CACHE_SIZE)
	return 0;
    hfd->cache_valid = 1;
    coffset = isincache (hfd, offset, len);
    if (coffset >= 0) {
	memcpy (buffer, hfd->cache + coffset, len);
	return len;
    }
    write_log ("hdf_read: cache bug! offset=%I64d len=%d\n", offset, len);
    hfd->cache_valid = 0;
    return 0;
}

int hdf_read (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
    int got = 0;
    uae_u8 *p = buffer;
    while (len > 0) {
	int maxlen = len > CACHE_SIZE ? CACHE_SIZE : len;
	int ret = hdf_read_2(hfd, p, offset, maxlen);
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
    if (hfd->readonly)
	return 0;
    hfd->cache_valid = 0;
    hdf_seek (hfd, offset);
    poscheck (hfd, len);
    memcpy (hfd->cache, buffer, len);
    if (hfd->handle_valid == HDF_HANDLE_WIN32)
	WriteFile (hfd->handle, hfd->cache, len, &outlen, NULL);
    else if (hfd->handle_valid == HDF_HANDLE_ZFILE)
	outlen = zfile_fwrite (hfd->cache, 1, len, hfd->handle);
    return outlen;
}

int hdf_write (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
    int got = 0;
    uae_u8 *p = buffer;
    while (len > 0) {
	int maxlen = len > CACHE_SIZE ? CACHE_SIZE : len;
	int ret = hdf_write_2(hfd, p, offset, maxlen);
	got += ret;
	if (ret != maxlen)
	    return got;
	offset += maxlen;
	p += maxlen;
	len -= maxlen;
    }
    return got;
}

#endif

#ifdef WINDDK

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
    STORAGE_PROPERTY_QUERY              query;
    SP_DEVICE_INTERFACE_DATA            interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA    interfaceDetailData = NULL;
    PSTORAGE_ADAPTER_DESCRIPTOR         adpDesc;
    PSTORAGE_DEVICE_DESCRIPTOR          devDesc;
    HANDLE                              hDevice = INVALID_HANDLE_VALUE;
    BOOL                                status;
    PUCHAR                              p;
    UCHAR                               outBuf[20000];
    ULONG                               length = 0,
                                        returned = 0,
                                        returnedLength;
    DWORD                               interfaceDetailDataSize = 0,
                                        reqSize,
                                        errorCode, 
                                        i, j;
    DRIVE_LAYOUT_INFORMATION		*dli;
    DISK_GEOMETRY			dg;
    GET_LENGTH_INFORMATION		gli;
    int gli_ok;
    int ret = -1;
    struct uae_driveinfo *udi;
    char orgname[1024];

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
            write_log ("SetupDiEnumDeviceInterfaces failed with error: %d\n", errorCode);
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
            write_log("SetupDiGetDeviceInterfaceDetail failed with error: %d\n", errorCode);
	    ret = 0;
	    goto end;
        }
    }

    //
    // Allocate memory to get the interface detail data
    // This contains the devicepath we need to open the device
    //

    interfaceDetailDataSize = reqSize;
    interfaceDetailData = malloc (interfaceDetailDataSize);
    if ( interfaceDetailData == NULL ) {
        write_log ("Unable to allocate memory to get the interface detail data.\n");
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

    if ( status == FALSE ) {
        write_log("Error in SetupDiGetDeviceInterfaceDetail failed with error: %d\n", GetLastError());
	ret = 0;
	goto end;
    }

    //
    // Now we have the device path. Open the device interface
    // to send Pass Through command

    udi = &uae_drives[*index2];
    strcpy (udi->device_path, interfaceDetailData->DevicePath);
    write_log ("opening device '%s'\n", udi->device_path);
    hDevice = CreateFile(
                interfaceDetailData->DevicePath,    // device interface name
                GENERIC_READ | GENERIC_WRITE,       // dwDesiredAccess
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

    free (interfaceDetailData);
    interfaceDetailData = NULL;

    if (hDevice == INVALID_HANDLE_VALUE) {
        write_log ("CreateFile failed with error: %d\n", GetLastError());
	ret = 1;
	goto end;
    }

    query.PropertyId = StorageAdapterProperty;
    query.QueryType = PropertyStandardQuery;

    status = DeviceIoControl(
                        hDevice,                
                        IOCTL_STORAGE_QUERY_PROPERTY,
                        &query,
                        sizeof( STORAGE_PROPERTY_QUERY ),
                        &outBuf,                   
                        sizeof (outBuf),
                        &returnedLength,      
                        NULL                    
                        );
    if ( !status ) {
        write_log("IOCTL_STORAGE_QUERY_PROPERTY failed with error code%d.\n", GetLastError());
	ret = 1;
	goto end;
    }

    adpDesc = (PSTORAGE_ADAPTER_DESCRIPTOR) outBuf;
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    status = DeviceIoControl(
			hDevice,                
                        IOCTL_STORAGE_QUERY_PROPERTY,
                        &query,
                        sizeof( STORAGE_PROPERTY_QUERY ),
                        &outBuf,                   
                        sizeof (outBuf),                      
                        &returnedLength,
                        NULL);
        if (!status) {
            write_log ("IOCTL_STORAGE_QUERY_PROPERTY failed with error code %d.\n", GetLastError());
	    ret = 1;
	    goto end;
        }
        devDesc = (PSTORAGE_DEVICE_DESCRIPTOR) outBuf;
        p = (PUCHAR) outBuf; 
        if (devDesc->DeviceType != INQ_DASD && devDesc->DeviceType != INQ_ROMD && devDesc->DeviceType != INQ_OPTD) {
	    ret = 1;
	    write_log ("not a direct access device, ignored (type=%d)\n", devDesc->DeviceType);
	    goto end;
	}
        if (devDesc->VendorIdOffset && p[devDesc->VendorIdOffset]) {
            j = 0;
            for (i = devDesc->VendorIdOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
	        udi->vendor_id[j++] = p[i];
        }
        if (devDesc->ProductIdOffset && p[devDesc->ProductIdOffset]) {
            j = 0;
            for (i = devDesc->ProductIdOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
	        udi->product_id[j++] = p[i];
        }
        if (devDesc->ProductRevisionOffset && p[devDesc->ProductRevisionOffset]) {
	    j = 0;
	    for (i = devDesc->ProductRevisionOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
	        udi->product_rev[j++] = p[i];
        }
        if (devDesc->SerialNumberOffset && p[devDesc->SerialNumberOffset]) {
	    j = 0;
	    for (i = devDesc->SerialNumberOffset; p[i] != (UCHAR) NULL && i < returnedLength; i++)
	        udi->product_serial[j++] = p[i];
        }
	if (udi->vendor_id[0])
	    strcat (udi->device_name, udi->vendor_id);
	if (udi->product_id[0]) {
	    if (udi->device_name[0])
		strcat (udi->device_name, " ");
	    strcat (udi->device_name, udi->product_id);
	}
	if (udi->product_rev[0]) {
	    if (udi->device_name[0])
		strcat (udi->device_name, " ");
	    strcat (udi->device_name, udi->product_rev);
	}
	if (udi->product_serial[0]) {
	    if (udi->device_name[0])
		strcat (udi->device_name, " ");
	    strcat (udi->device_name, udi->product_serial);
	}

	write_log ("device id string: '%s'\n", udi->device_name);
    if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, (void*)&dg, sizeof (dg), &returnedLength, NULL)) {
        write_log ("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with error code %d.\n", GetLastError());
        ret = 1;
        goto end;
    }
    gli_ok = 1;
    if (!DeviceIoControl (hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, (void*)&gli, sizeof (gli), &returnedLength, NULL)) {
        write_log ("IOCTL_DISK_GET_LENGTH_INFO failed with error code %d.\n", GetLastError());
	gli_ok = 0;
	write_log ("IOCTL_DISK_GET_LENGTH_INFO not supported, detected disk size may not be correct.\n");
    }
    udi->bytespersector = dg.BytesPerSector;
    if (dg.BytesPerSector < 512) {
	write_log ("unsupported blocksize < 512 (%d)\n", dg.BytesPerSector);
	ret = 1;
	goto end;
    }
    if (dg.BytesPerSector > 2048) {
	write_log ("unsupported blocksize > 2048 (%d)\n", dg.BytesPerSector);
	ret = 1;
	goto end;
    }
    udi->offset = udi->offset2 = 0;
    write_log ("BytesPerSector=%d Cyls=%I64d TracksPerCyl=%d SecsPerTrack=%d\n",
	dg.BytesPerSector, dg.Cylinders.QuadPart, dg.TracksPerCylinder, dg.SectorsPerTrack);
    udi->size = udi->size2 = (uae_u64)dg.BytesPerSector * (uae_u64)dg.Cylinders.QuadPart *
	(uae_u64)dg.TracksPerCylinder * (uae_u64)dg.SectorsPerTrack;
    if (gli_ok)
	udi->size = udi->size2 = gli.Length.QuadPart;
    write_log ("device size %I64d (0x%I64x) bytes\n", udi->size, udi->size);

    memset (outBuf, 0, sizeof (outBuf));
    status = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0,
        &outBuf, sizeof (outBuf), &returnedLength, NULL);
    if (!status) {
	write_log("IOCTL_DISK_GET_DRIVE_LAYOUT failed with error code%d.\n", GetLastError());
	ret = 1;
	goto end;
    }
    strcpy (orgname, udi->device_name);
    trim (orgname);
    dli = (DRIVE_LAYOUT_INFORMATION*)outBuf;
    if (dli->PartitionCount) {
        struct uae_driveinfo *udi2 = udi;
        int nonzeropart = 0;
        int gotpart = 0;
	int safepart = 0;
	write_log ("%d MBR partitions found\n", dli->PartitionCount);
	for (i = 0; i < dli->PartitionCount && (*index2) < MAX_FILESYSTEM_UNITS; i++) {
	    PARTITION_INFORMATION *pi = &dli->PartitionEntry[i];
	    if (pi->PartitionType == PARTITION_ENTRY_UNUSED)
		continue;
	    write_log ("%d: num: %d type: %02.2X offset: %I64d size: %I64d, ", i, pi->PartitionNumber, pi->PartitionType, pi->StartingOffset.QuadPart, pi->PartitionLength.QuadPart);
	    if (pi->RecognizedPartition == 0) {
		write_log ("unrecognized\n");
		continue;
	    }
	    nonzeropart++;
	    if (pi->PartitionType != 0x76) {
		write_log ("type not 0x76\n");
		continue;
	    }
	    memmove (udi, udi2, sizeof (*udi));
	    udi->device_name[0] = 0;
	    udi->offset = udi->offset2 = pi->StartingOffset.QuadPart;
	    udi->size = udi->size2 = pi->PartitionLength.QuadPart;
	    write_log ("used\n");
	    if (safetycheck (hDevice, udi->offset, buffer, dg.BytesPerSector)) {
		sprintf (udi->device_name, "HD_P#%d_%s", pi->PartitionNumber, orgname);
		udi++;
		(*index2)++;
		safepart = 1;
	    }
	    gotpart = 1;
	}
	if (!nonzeropart) {
	    write_log ("empty MBR partition table detected, checking for RDB\n");
	} else if (!gotpart) {
	    write_log ("non-empty MBR partition table detected, doing RDB check anyway\n");
	} else if (safepart) {
	    goto amipartfound; /* ugly but bleh.. */
	}
    } else {
	write_log ("no MBR partition table detected, checking for RDB\n");
    }

    i = safetycheck (hDevice, 0, buffer, dg.BytesPerSector);
    if (!i) {
	ret = 1;
	goto end;
    }
amipartfound:
    if (i > 1)
	sprintf (udi->device_name, "HD_*_%s", orgname);
    else
	sprintf (udi->device_name, "HD_%s", orgname);
    while (isharddrive (udi->device_name) >= 0)
	strcat (udi->device_name, "_");
    (*index2)++;
end:
    free (interfaceDetailData);
    if (hDevice != INVALID_HANDLE_VALUE)
	CloseHandle (hDevice);
    return ret;
}
#endif

int hdf_init (void)
{
#ifdef WINDDK
    HDEVINFO hIntDevInfo;
#endif
    DWORD index = 0, index2 = 0;
    uae_u8 *buffer;
    static int done;

    if (done)
	return num_drives;
    done = 1;
    num_drives = 0;
#ifdef WINDDK
    buffer = VirtualAlloc (NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
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
	    SetupDiDestroyDeviceInfoList(hIntDevInfo);
	}
	VirtualFree (buffer, 0, MEM_RELEASE);
    }
    num_drives = index2;
    write_log ("Drive scan result: %d Amiga formatted drives detected\n", num_drives);
#endif
    return num_drives;
}

int hdf_getnumharddrives (void)
{
    return num_drives;
}

char *hdf_getnameharddrive (int index, int flags)
{
    static char name[512];
    char tmp[32];
    uae_u64 size = uae_drives[index].size;

    if (flags & 1) {
	    if (size >= 1024 * 1024 * 1024)
	        sprintf (tmp, "%.1fG", ((double)(uae_u32)(size / (1024 * 1024))) / 1024.0);
	    else
	        sprintf (tmp, "%.1fM", ((double)(uae_u32)(size / (1024))) / 1024.0);
 	sprintf (name, "[%s] %s", tmp, uae_drives[index].device_name);
	return name;
    }
    if (flags & 2)
	return uae_drives[index].device_path;
    return uae_drives[index].device_name;
}


static int progressdialogreturn;
static int progressdialogactive;

static INT_PTR CALLBACK ProgressDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
	case WM_DESTROY:
	PostQuitMessage (0);
	progressdialogactive = 0;
	return TRUE;
	case WM_CLOSE:
	DestroyWindow(hDlg);
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
		    DestroyWindow (hDlg);
		return TRUE;
	    }
	break;
    }
    return FALSE;
}

extern HMODULE hUIDLL;
extern HINSTANCE hInst;

#define COPY_CACHE_SIZE 1024*1024
int harddrive_to_hdf(HWND hDlg, struct uae_prefs *p, int idx)
{
    HANDLE h = INVALID_HANDLE_VALUE, hdst = INVALID_HANDLE_VALUE;
    void *cache = NULL;
    DWORD ret, got, gotdst, get;
    uae_u64 size, sizecnt, written;
    LARGE_INTEGER li;
    char path[MAX_DPATH], tmp[MAX_DPATH], tmp2[MAX_DPATH];
    DWORD retcode = 0;
    HWND hwnd, hwndprogress, hwndprogresstxt;
    MSG msg;
    int pct, cnt;
 
    cache = VirtualAlloc (NULL, COPY_CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (!cache)
	goto err;
    h = CreateFile (uae_drives[idx].device_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
	OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_NO_BUFFERING, NULL);
    if (h == INVALID_HANDLE_VALUE)
	goto err;
    size = uae_drives[idx].size;
    path[0] = 0;
    DiskSelection (hDlg, IDC_PATH_NAME, 3, p, 0);
    GetDlgItemText (hDlg, IDC_PATH_NAME, path, MAX_DPATH);
    if (*path == 0)
	goto err;
    hdst = CreateFile (path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
	CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
    if (hdst == INVALID_HANDLE_VALUE)
	goto err;
    li.QuadPart = size;
    ret = SetFilePointer(hdst, li.LowPart, &li.HighPart, FILE_BEGIN);
    if (ret == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
	goto err;
    if (!SetEndOfFile(hdst))
	goto err;
    li.QuadPart = 0;
    SetFilePointer(hdst, 0, &li.HighPart, FILE_BEGIN);
    li.QuadPart = 0;
    SetFilePointer(h, 0, &li.HighPart, FILE_BEGIN);
    progressdialogreturn = -1;
    progressdialogactive = 1;
    hwnd = CreateDialog (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_PROGRESSBAR), hDlg, ProgressDialogProc);
    if (hwnd == NULL)
	goto err;
    hwndprogress = GetDlgItem(hwnd, IDC_PROGRESSBAR);
    hwndprogresstxt = GetDlgItem(hwnd, IDC_PROGRESSBAR_TEXT);
    ShowWindow (hwnd, SW_SHOW);
    pct = 0;
    cnt = 1000;
    sizecnt = 0;
    written = 0;
    for (;;) {
	if (progressdialogreturn >= 0)
    	    break;
	if (cnt > 0) {
	    SendMessage(hwndprogress, PBM_SETPOS, (WPARAM)pct, 0);
	    sprintf (tmp, "%dM / %dM (%d%%)", (int)(written >> 20), (int)(size >> 20), pct);
	    SendMessage(hwndprogresstxt, WM_SETTEXT, 0, (LPARAM)tmp);
	    while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	    }
	    cnt = 0;
	}
	got = gotdst = 0;
	li.QuadPart = sizecnt;
	if (SetFilePointer(h, li.LowPart, &li.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
	    DWORD err = GetLastError();
	    if (err != NO_ERROR) {
		progressdialogreturn = 3;
		break;
	    }
	}
	get = COPY_CACHE_SIZE;
	if (sizecnt + get > size)
	    get = size - sizecnt;
	if (!ReadFile(h, cache, get, &got, NULL)) {
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
	    if (!WriteFile(hdst, cache, got, &gotdst, NULL))  {
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
    WIN32GUI_LoadUIString (IDS_HDCLONE_FAIL, tmp, MAX_DPATH);
    sprintf (tmp2, tmp, progressdialogreturn, GetLastError());
    gui_message (tmp2);
    
ok:
    if (h != INVALID_HANDLE_VALUE)
	CloseHandle(h);
    if (cache)
	VirtualFree(cache, 0, MEM_RELEASE);
    if (hdst != INVALID_HANDLE_VALUE)
	CloseHandle(hdst);
    return retcode;
}

