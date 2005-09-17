 /*
  * UAE - The Un*x Amiga Emulator
  *
  * WIN32 CDROM/HD low level access code (SPTI)
  *
  * Copyright 2002 Toni Wilen
  *
  */


#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x500

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WINDDK

#include "memory.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"

#include <stddef.h>

#include <devioctl.h>  
#include <ntddstor.h>
#include <winioctl.h>
#include <initguid.h>   // Guid definition
#include <devguid.h>    // Device guids
#include <setupapi.h>   // for SetupDiXxx functions.
#include <cfgmgr32.h>   // for SetupDiXxx functions.

#include <ntddscsi.h>


typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
  SCSI_PASS_THROUGH_DIRECT spt;
  ULONG Filler;
  UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

static int unitcnt = 0;

struct dev_info_spti {
    char *drvpath;
    char *name;
    int mediainserted;
    HANDLE handle;
    int isatapi;
    int type;
    int bus, path, target, lun;
};

static uae_sem_t scgp_sem;
static struct dev_info_spti dev_info[MAX_TOTAL_DEVICES];
static uae_u8 *scsibuf;

static int doscsi (int unitnum, SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER *swb, int *err)
{
    DWORD status, returned;
    struct dev_info_spti *di = &dev_info[unitnum];

    *err = 0;
    if (log_scsi) {
	write_log ("SCSI, H=%X:%d:%d:%d:%d: ", di->handle, di->bus, di->path, di->target, di->lun);
	scsi_log_before (swb->spt.Cdb, swb->spt.CdbLength,
	    swb->spt.DataIn == SCSI_IOCTL_DATA_OUT ? swb->spt.DataBuffer : 0,swb->spt.DataTransferLength);
    }
    gui_cd_led (1);
    swb->spt.ScsiStatus = 0;
    if (di->bus >= 0) {
	swb->spt.PathId = di->path;
	swb->spt.TargetId = di->target;
	swb->spt.Lun = di->lun;
    }
    status = DeviceIoControl (di->handle, IOCTL_SCSI_PASS_THROUGH_DIRECT,
	swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
	swb, sizeof (SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER),
	&returned, NULL);
    if (!status) {
	int lasterror = GetLastError();
	*err = lasterror;
	write_log("SCSI ERROR, H=%X:%d:%d:%d:%d: ", di->handle, di->bus, di->path, di->target, di->lun);
	write_log("Error code = %d, LastError=%d\n", swb->spt.ScsiStatus, lasterror);
	scsi_log_before (swb->spt.Cdb, swb->spt.CdbLength,
	    swb->spt.DataIn == SCSI_IOCTL_DATA_OUT ? swb->spt.DataBuffer : 0,swb->spt.DataTransferLength);
    }
    if (log_scsi)
	scsi_log_after (swb->spt.DataIn == SCSI_IOCTL_DATA_IN ? swb->spt.DataBuffer : 0, swb->spt.DataTransferLength,
	    swb->SenseBuf, swb->spt.SenseInfoLength);
    if (swb->spt.SenseInfoLength > 0 && (swb->SenseBuf[0] == 0 || swb->SenseBuf[0] == 1))
	swb->spt.SenseInfoLength = 0; /* 0 and 1 = success, not error.. */
    if (swb->spt.SenseInfoLength > 0)
	return 0;
    gui_cd_led (1);
    return status;
}


#define MODE_SELECT_6  0x15
#define MODE_SENSE_6   0x1A
#define MODE_SELECT_10 0x55
#define MODE_SENSE_10  0x5A

static int execscsicmd (int unitnum, uae_u8 *data, int len, uae_u8 *inbuf, int inlen)
{
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
    DWORD status;
    int err, dolen;

    uae_sem_wait (&scgp_sem);
    memset (&swb, 0, sizeof (swb));
    swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
    swb.spt.CdbLength = len;
    if (inbuf) {
	swb.spt.DataIn = SCSI_IOCTL_DATA_IN;
	swb.spt.DataTransferLength = inlen;
	swb.spt.DataBuffer = inbuf;
	memset (inbuf, 0, inlen);
    } else {
	swb.spt.DataIn = SCSI_IOCTL_DATA_OUT;
    }
    swb.spt.TimeOutValue = 80 * 60;
    swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
    swb.spt.SenseInfoLength = 32;
    memcpy (swb.spt.Cdb, data, len);
    status = doscsi (unitnum, &swb, &err);
    uae_sem_post (&scgp_sem);
    dolen = swb.spt.DataTransferLength;
    if (!status)
	return -1;
    return dolen;
}

static int execscsicmd_direct (int unitnum, uaecptr acmd)
{
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
    DWORD status;
    int sactual = 0, i;
    uaecptr scsi_data = get_long (acmd + 0);
    uae_u32 scsi_len = get_long (acmd + 4);
    uaecptr scsi_cmd = get_long (acmd + 12);
    int scsi_cmd_len = get_word (acmd + 16);
    int scsi_cmd_len_orig = scsi_cmd_len;
    uae_u8 scsi_flags = get_byte (acmd + 20);
    uaecptr scsi_sense = get_long (acmd + 22);
    uae_u16 scsi_sense_len = get_word (acmd + 26);
    int io_error = 0, err, parm;
    addrbank *bank_data = &get_mem_bank (scsi_data);
    uae_u8 *scsi_datap, *scsi_datap_org;

    memset (&swb, 0, sizeof (swb));
    swb.spt.Length = sizeof (SCSI_PASS_THROUGH);
    swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);

    /* do transfer directly to and from Amiga memory */
    if (!bank_data || !bank_data->check (scsi_data, scsi_len))
	return -5; /* IOERR_BADADDRESS */

    uae_sem_wait (&scgp_sem);

    /* the Amiga does not tell us how long the timeout shall be, so make it _very_ long (specified in seconds) */
    swb.spt.TimeOutValue = 80 * 60;
    scsi_datap = scsi_datap_org = scsi_len ? bank_data->xlateaddr (scsi_data) : 0;
    swb.spt.DataIn = (scsi_flags & 1) ? SCSI_IOCTL_DATA_IN : SCSI_IOCTL_DATA_OUT;
    for (i = 0; i < scsi_cmd_len; i++)
	swb.spt.Cdb[i] = get_byte (scsi_cmd + i);
    if (scsi_sense_len > 32)
	scsi_sense_len = 32;
    swb.spt.SenseInfoLength  = (scsi_flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
	(scsi_flags & 2) ? scsi_sense_len : /* SCSIF_AUTOSENSE */
	32;
    if (dev_info[unitnum].isatapi)
	scsi_atapi_fixup_pre (swb.spt.Cdb, &scsi_cmd_len, &scsi_datap, &scsi_len, &parm);
    swb.spt.CdbLength = (UCHAR)scsi_cmd_len;
    swb.spt.DataTransferLength = scsi_len;
    swb.spt.DataBuffer = scsi_datap;

    status = doscsi (unitnum, &swb, &err);

    put_word (acmd + 18, status == 0 ? 0 : scsi_cmd_len_orig); /* fake scsi_CmdActual */
    put_byte (acmd + 21, swb.spt.ScsiStatus); /* scsi_Status */
    if (swb.spt.ScsiStatus) {
	io_error = 45; /* HFERR_BadStatus */
	/* copy sense? */
	for (sactual = 0; scsi_sense && sactual < scsi_sense_len && sactual < swb.spt.SenseInfoLength; sactual++)
	    put_byte (scsi_sense + sactual, swb.SenseBuf[sactual]);
	put_long (acmd + 8, 0); /* scsi_Actual */
    } else {
	int i;
	for (i = 0; i < scsi_sense_len; i++)
	    put_byte (scsi_sense + i, 0);
	sactual = 0;
	if (status == 0) {
	    io_error = 20; /* io_Error, but not specified */
	    put_long (acmd + 8, 0); /* scsi_Actual */
	} else {
	    scsi_len = swb.spt.DataTransferLength;
	    if (dev_info[unitnum].isatapi)
		scsi_atapi_fixup_post (swb.spt.Cdb, scsi_cmd_len, scsi_datap_org, scsi_datap, &scsi_len, parm);
	    io_error = 0;
	    put_long (acmd + 8, scsi_len); /* scsi_Actual */
	}
    }
    put_word (acmd + 28, sactual);
    uae_sem_post (&scgp_sem);

    if (scsi_datap != scsi_datap_org)
	free (scsi_datap);

    return io_error;
}

static uae_u8 *execscsicmd_out (int unitnum, uae_u8 *data, int len)
{
    int v = execscsicmd (unitnum, data, len, 0, 0);
    if (v < 0)
	return 0;
    return data;
}

static uae_u8 *execscsicmd_in (int unitnum, uae_u8 *data, int len, int *outlen)
{
    int v = execscsicmd (unitnum, data, len, scsibuf, DEVICE_SCSI_BUFSIZE);
    if (v < 0)
	return 0;
    if (v == 0)
	return 0;
    if (outlen)
	*outlen = v;
    return scsibuf;
}

static int total_devices;

static int adddrive (char *drvpath, int bus, int pathid, int targetid, int lunid)
{
    struct dev_info_spti *di;
    int cnt = total_devices, i;

    if (cnt >= MAX_TOTAL_DEVICES)
	return 0;
    for (i = 0; i < total_devices; i++) {
	di = &dev_info[i];
	if (!strcmp(drvpath, di->drvpath))
	    return 0;
    }
    write_log("SPTI: unit %d '%s' added\n", total_devices, drvpath);
    di = &dev_info[total_devices];
    di->drvpath = my_strdup(drvpath);
    di->type = 0;
    di->bus = bus;
    di->path = pathid;
    di->target = targetid;
    di->lun = lunid;
    total_devices++;
    return 1;
}

int rescan(void);
static int open_scsi_bus (int flags)
{
    int i;

    total_devices = 0;
    uae_sem_init (&scgp_sem, 0, 1);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	memset (&dev_info[i], 0, sizeof (struct dev_info_spti));
	dev_info[i].handle = INVALID_HANDLE_VALUE;
    }
    if (!scsibuf)
	scsibuf = VirtualAlloc (NULL, DEVICE_SCSI_BUFSIZE, MEM_COMMIT, PAGE_READWRITE);
    rescan();
    return total_devices;
}

#if 0
static int open_scsi_bus (int flags)
{
    int dwDriveMask;
    int drive, firstdrive;
    char tmp[10], tmp2[10];
    int i;

    total_devices = 0;
    firstdrive = 0;
    uae_sem_init (&scgp_sem, 0, 1);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	memset (&dev_info[i], 0, sizeof (struct dev_info_spti));
	dev_info[i].handle = INVALID_HANDLE_VALUE;
    }
    if (!scsibuf)
	scsibuf = VirtualAlloc (NULL, DEVICE_SCSI_BUFSIZE, MEM_COMMIT, PAGE_READWRITE);
    dwDriveMask = GetLogicalDrives ();
    device_debug ("SPTI: drive mask = %08.8X\n", dwDriveMask);
    dwDriveMask >>= 2; // Skip A and B drives...
    for( drive = 'C'; drive <= 'Z'; drive++) {
	if (dwDriveMask & 1) {
	    int dt;
	    sprintf( tmp, "%c:\\", drive );
	    sprintf( tmp2, "%c:\\.", drive );
	    dt = GetDriveType (tmp);
	    device_debug ("SPTI: drive %c type %d\n", drive, dt);
	    if (dt == DRIVE_CDROM) {
		if (!firstdrive)
		    firstdrive = drive;
		if (adddrive (total_devices, drive))
		    total_devices++;
	    }
	}
	dwDriveMask >>= 1;
    }
    return total_devices;
}
#endif

static void close_scsi_bus (void)
{
    int i;
    VirtualFree (scsibuf, 0, MEM_RELEASE);
    scsibuf = 0;
    for (i = 0; i < total_devices; i++) {
	xfree(dev_info[i].name);
	xfree(dev_info[i].drvpath);
	dev_info[i].name = NULL;
	dev_info[i].drvpath = NULL;
    }
}

static int inquiry (int unitnum, int *type, uae_u8 *inquirydata, int *inqlen)
{
    uae_u8 cmd[6] = { 0x12,0,0,0,36,0 }; /* INQUIRY */
    uae_u8 out[36];
    int outlen = sizeof (out);
    uae_u8 *p = execscsicmd_in (unitnum, cmd, sizeof (cmd), &outlen);
    int v = 0;

    *inqlen = 0;
    *type = 0x1f;
    if (!p) {
	if (log_scsi)
	    write_log("SPTI: INQUIRY failed!?\n");
	return 0;
    }
    *inqlen = outlen > 36 ? 36 : outlen;
    if (outlen >= 1)
	*type = p[0] & 31;
    if (outlen >= 2 && (p[0] & 31) == 5 && (p[2] & 7) == 0)
	v = 1;
    memcpy (inquirydata, p, *inqlen);
    if (log_scsi) {
	if (outlen >= 36) 
	    write_log("SPTI: INQUIRY: %02.2X%02.2X%02.2X %d '%-8.8s' '%-16.16s'\n",
		p[0], p[1], p[2], v, p + 8, p + 16);
    }
    return v;
}

static int mediacheck (int unitnum)
{
    uae_u8 cmd [6] = { 0,0,0,0,0,0 }; /* TEST UNIT READY */
    int v;
    if (dev_info[unitnum].handle == INVALID_HANDLE_VALUE)
	return 0;
    v = execscsicmd (unitnum, cmd, sizeof (cmd), 0, 0);
    return v >= 0 ? 1 : 0;
}

static void close_scsi_device (int unitnum)
{
    write_log ("SPTI: cd unit %d closed\n", unitnum);
    if (dev_info[unitnum].handle != INVALID_HANDLE_VALUE)
	CloseHandle (dev_info[unitnum].handle);
    dev_info[unitnum].handle = INVALID_HANDLE_VALUE;
}

int open_scsi_device (int unitnum)
{
    HANDLE h;
    char *dev;
    struct dev_info_spti *di;

    di = &dev_info[unitnum];
    if (unitnum >= total_devices)
	return 0;
    if (!di)
	return 0;
    if (di->bus >= 0) {
	dev = xmalloc (100);
	sprintf (dev, "\\\\.\\Scsi%d:", di->bus);
    } else {
        dev = my_strdup(di->drvpath);
    }
    h = CreateFile(dev,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    di->handle = h;
    if (h == INVALID_HANDLE_VALUE) {
	write_log ("SPTI: failed to open cd unit %d err=%d ('%s')\n", unitnum, GetLastError(), dev);
    } else {
	uae_u8 inqdata[37] = { 0 };
	int inqlen;
        dev_info[unitnum].isatapi = inquiry (unitnum, &dev_info[unitnum].type, inqdata, &inqlen);
	if (inqlen == 0) {
	    write_log ("SPTI: inquiry failed unit %d ('%s':%d:%d:%d:%d)\n", unitnum, dev,
		di->bus, di->path, di->target, di->lun);
	    close_scsi_device (unitnum);
	    xfree (dev);
	    return 0;
	}
	inqdata[36] = 0;
	if (dev_info[unitnum].type == INQ_ROMD) {
	    dev_info[unitnum].mediainserted = mediacheck (unitnum);
	    write_log ("SPTI: unit %d opened [%s], %s, '%s'\n", unitnum,
		dev_info[unitnum].isatapi ? "ATAPI" : "SCSI",
		dev_info[unitnum].mediainserted ? "CD inserted" : "Drive empty", inqdata + 8);
	} else {
	    write_log ("SPTI: unit %d, '%s'\n",
		unitnum, dev_info[unitnum].type, inqdata + 8);
	}
	dev_info[unitnum].name = my_strdup (inqdata + 8);
        xfree (dev);
	return 1;
    }
    xfree (dev);
    return 0;
}

static struct device_info *info_device (int unitnum, struct device_info *di)
{
    if (unitnum >= MAX_TOTAL_DEVICES || dev_info[unitnum].handle == INVALID_HANDLE_VALUE)
	return 0;
    di->label = my_strdup(dev_info[unitnum].name);
    di->bus = 0;
    di->target = unitnum;
    di->lun = 0;
    di->media_inserted = mediacheck (unitnum);
    di->write_protected = 1;
    di->bytespersector = 2048;
    di->cylinders = 1;
    di->type = dev_info[unitnum].type;
    di->id = unitnum + 1;
    return di;
}

void win32_spti_media_change (char driveletter, int insert)
{
    int i, now;

    for (i = 0; i < total_devices; i++) {
	if (dev_info[i].type == INQ_ROMD) {
	    now = mediacheck (i);
	    if (now != dev_info[i].mediainserted) {
		write_log ("SPTI: media change %c %d\n", driveletter, insert);
		dev_info[i].mediainserted = now;
		scsi_do_disk_change (i + 1, insert);
	    }
	}
    }
}

static int check_isatapi (int unitnum)
{
    return dev_info[unitnum].isatapi;
}

struct device_functions devicefunc_win32_spti = {
    open_scsi_bus, close_scsi_bus, open_scsi_device, close_scsi_device, info_device,
    execscsicmd_out, execscsicmd_in, execscsicmd_direct,
    0, 0, 0, 0, 0, 0, 0, check_isatapi
};

#define	SCSI_INFO_BUFFER_SIZE	0x5000	// Big enough to hold all Bus/Device info.
static char* BusType[] = {
    "UNKNOWN",  // 0x00
    "SCSI",
    "ATAPI",
    "ATA",
    "IEEE 1394",
    "SSA",
    "FIBRE",
    "USB",
    "RAID"
};

static void GetInquiryData(PCTSTR pDevId, DWORD idx)
{
    SP_DEVICE_INTERFACE_DATA            interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA    interfaceDetailData = NULL;
    STORAGE_PROPERTY_QUERY              query;
    PSTORAGE_ADAPTER_DESCRIPTOR         adpDesc;
    PSCSI_BUS_DATA			BusData;
    PSCSI_INQUIRY_DATA		        InquiryData;
    PSCSI_ADAPTER_BUS_INFO          	AdapterInfo;
    HANDLE                              hDevice;
    BOOL                                status;
    DWORD                               index = 0;
    HDEVINFO                            hIntDevInfo;
    UCHAR                               outBuf[512];
    BOOL	                        Claimed;
    ULONG                               returnedLength;
    SHORT	                        Bus,
					Luns;
    DWORD                               interfaceDetailDataSize,
					reqSize,
					errorCode;


    hIntDevInfo = SetupDiGetClassDevs (
		 &StoragePortClassGuid,
		 pDevId,                                 // Enumerator
		 NULL,                                   // Parent Window
		 (DIGCF_PRESENT | DIGCF_INTERFACEDEVICE  // Only Devices present & Interface class
		 ));

    if (hIntDevInfo == INVALID_HANDLE_VALUE) {
	write_log ("SetupDiGetClassDevs failed with error: %d\n", GetLastError());
	return;
    }

    interfaceData.cbSize = sizeof (SP_INTERFACE_DEVICE_DATA);

    status = SetupDiEnumDeviceInterfaces ( 
		hIntDevInfo,            // Interface Device Info handle
		0,
		(LPGUID) &StoragePortClassGuid,
		index,                  // Member
		&interfaceData          // Device Interface Data
		);

    if (status == FALSE) {
	errorCode = GetLastError();
	if (errorCode == ERROR_NO_MORE_ITEMS)
	    return;
	write_log ("SetupDiEnumDeviceInterfaces failed with error: %d\n", errorCode);
	return;
    }
	
    //
    // Find out required buffer size, so pass NULL 
    //

    status = SetupDiGetDeviceInterfaceDetail (
		hIntDevInfo,        // Interface Device info handle
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
	    write_log ("SetupDiGetDeviceInterfaceDetail failed with error: %d\n", errorCode);
	    return;
	}
    }

    //
    // Allocate memory to get the interface detail data
    // This contains the devicepath we need to open the device
    //

    interfaceDetailDataSize = reqSize;
    interfaceDetailData = malloc (interfaceDetailDataSize);
    if (interfaceDetailData == NULL) {
	write_log ("Unable to allocate memory to get the interface detail data.\n");
	return;
    }
    interfaceDetailData->cbSize = sizeof (SP_INTERFACE_DEVICE_DETAIL_DATA);

    status = SetupDiGetDeviceInterfaceDetail (
		  hIntDevInfo,              // Interface Device info handle
		  &interfaceData,           // Interface data for the event class
		  interfaceDetailData,      // Interface detail data
		  interfaceDetailDataSize,  // Interface detail data size
		  &reqSize,                 // Buffer size required to get the detail data
		  NULL);                    // Interface device info

    if (status == FALSE) {
	write_log ("Error in SetupDiGetDeviceInterfaceDetail failed with error: %d\n", GetLastError());
	return;
    }

    //
    // Now we have the device path. Open the device interface
    // to get adapter property and inquiry data


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

    if (hDevice == INVALID_HANDLE_VALUE) {
	write_log ("CreateFile failed with error: %d\n", GetLastError());
	CloseHandle ( hDevice );	
	return;
    }

    query.PropertyId = StorageAdapterProperty;
    query.QueryType = PropertyStandardQuery;

    status = DeviceIoControl(
			hDevice,                
			IOCTL_STORAGE_QUERY_PROPERTY,
			&query,
			sizeof(STORAGE_PROPERTY_QUERY),
			&outBuf,                   
			512,                      
			&returnedLength,      
			NULL                    
			);
    if (!status) {
	write_log ("IOCTL failed with error code%d.\n\n", GetLastError());
    } else {
	adpDesc = (PSTORAGE_ADAPTER_DESCRIPTOR) outBuf;
	write_log ("\nAdapter Properties\n");
	write_log ("------------------\n");
	write_log ("Bus Type       : %s\n",   BusType[adpDesc->BusType]);
	write_log ("Max. Tr. Length: 0x%x\n", adpDesc->MaximumTransferLength );
	write_log ("Max. Phy. Pages: 0x%x\n", adpDesc->MaximumPhysicalPages );
	write_log ("Alignment Mask : 0x%x\n", adpDesc->AlignmentMask );
    }
    
    AdapterInfo = (PSCSI_ADAPTER_BUS_INFO) malloc(SCSI_INFO_BUFFER_SIZE) ;
    if (AdapterInfo == NULL) {
	CloseHandle (hDevice);	
	return;
    }

    // Get the SCSI inquiry data for all devices for the given SCSI bus
    status = DeviceIoControl(	
			    hDevice,
			    IOCTL_SCSI_GET_INQUIRY_DATA,
			    NULL,
			    0,
			    AdapterInfo,
			    SCSI_INFO_BUFFER_SIZE,
			    &returnedLength,
			    NULL );

    if (!status) {
	write_log ("Error in IOCTL_SCSI_GET_INQUIRY_DATA\n" );
	free (AdapterInfo);
	CloseHandle (hDevice);
	return;
    }

    write_log ("Initiator   Path ID  Target ID    LUN  Claimed  Device   \n");
    write_log ("---------------------------------------------------------\n");

    for (Bus = 0; Bus < AdapterInfo->NumberOfBuses; Bus++) {
	BusData = &AdapterInfo->BusData[Bus];
	InquiryData = (PSCSI_INQUIRY_DATA) ( (PUCHAR) AdapterInfo + BusData->InquiryDataOffset );
	for (Luns = 0; Luns < BusData->NumberOfLogicalUnits; Luns++) {
	    char label[100];
	    int type = InquiryData->InquiryData[0] & 0x1f;
	    Claimed = InquiryData->DeviceClaimed;
	    write_log ("   %3d        %d         %d          %d     %s     %d\n",
		BusData->InitiatorBusId, InquiryData->PathId, InquiryData->TargetId, 
		InquiryData->Lun, Claimed ? "Yes" : "No ", type);
	    if (!Claimed) {
		sprintf (label, "SCSI(%d):%d:%d:%d:%d", idx, BusData->InitiatorBusId,
		    InquiryData->PathId, InquiryData->TargetId, InquiryData->Lun);
		//adddrive (label, idx, InquiryData->PathId, InquiryData->TargetId, InquiryData->Lun);
		adddrive (label, idx, 0, 1, 0);
	    }
	    InquiryData = (PSCSI_INQUIRY_DATA) ( (PUCHAR) AdapterInfo + InquiryData->NextInquiryDataOffset );
	}   // for Luns
    }	// for Bus

    write_log ("\n" );
    free (AdapterInfo);
    CloseHandle (hDevice);
}

static void GetChildDevices(DEVINST DevInst)
{
    DEVINST         childDevInst;
    DEVINST         siblingDevInst;
    CONFIGRET       configRetVal;
    TCHAR           deviceInstanceId[MAX_DEVICE_ID_LEN];
//    HDEVINFO	    tmpdi;

    configRetVal = CM_Get_Child (
			&childDevInst,
			DevInst,
			0 );

    if (configRetVal == CR_NO_SUCH_DEVNODE) {
	write_log ("No child devices\n");
	return;
    }

    if (configRetVal != CR_SUCCESS) {
	write_log ("CM_Get_Child failed with error: %x\n", configRetVal);
	return;
    }

    do {

	// Get the Device Instance ID using the handle
	configRetVal = CM_Get_Device_ID(
			    childDevInst,
			    deviceInstanceId,
			    sizeof(deviceInstanceId)/sizeof(TCHAR),
			    0
			    );
	if (configRetVal != CR_SUCCESS) {
	    write_log ("CM_Get_Device_ID: Get Device Instance ID failed with error: %x\n", configRetVal);
	    return;
	}

	write_log ("'%s'\n", deviceInstanceId);
#if 0
	tmpdi = SetupDiCreateDeviceInfoList (NULL, NULL);
	if (tmpdi) {
	    SP_DEVINFO_DATA did;
	    did.cbSize = sizeof (did);
	    SetupDiOpenDeviceInfo(tmpdi, deviceInstanceId, NULL, 0, &did);
	    adddrive (deviceInstanceId, -1, -1, -1, -1);
	    SetupDiDestroyDeviceInfoList (tmpdi);
	}
#endif
	// Get sibling
	configRetVal = CM_Get_Sibling (
			    &siblingDevInst,
			    childDevInst,
			    0 );

	if (configRetVal == CR_NO_SUCH_DEVNODE)
	    return;

	if (configRetVal != CR_SUCCESS) {
	    write_log ("CM_Get_Sibling failed with error: 0x%X\n", configRetVal);
	    return;
	}
	childDevInst = siblingDevInst;

    } while (TRUE);

}

static BOOL GetRegistryProperty(HDEVINFO DevInfo, DWORD Index, int *first)
{
    SP_DEVINFO_DATA deviceInfoData;
    DWORD errorCode;
    DWORD bufferSize = 0;
    DWORD dataType;
    LPTSTR buffer = NULL;
    BOOL status;
    CONFIGRET configRetVal;
    TCHAR deviceInstanceId[MAX_DEVICE_ID_LEN];

    // Get the DEVINFO data. This has the handle to device instance 
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    status = SetupDiEnumDeviceInfo(
	DevInfo,                // Device info set
	Index,                  // Index in the info set
	&deviceInfoData);       // Info data. Contains handle to dev inst

    if (status == FALSE) {
        errorCode = GetLastError();
	if (errorCode == ERROR_NO_MORE_ITEMS) {
	    if (*first)
		write_log ("SPTI: No SCSI adapters detected\n");
	    return FALSE;
	}
	write_log ("SetupDiEnumDeviceInfo failed with error: %d\n", errorCode);
        return FALSE;
    }
    *first = 0;

    // Get the Device Instance ID using the handle
    configRetVal = CM_Get_Device_ID(
	deviceInfoData.DevInst,                 // Handle to dev inst
	deviceInstanceId,                       // Buffer to receive dev inst
	sizeof(deviceInstanceId)/sizeof(TCHAR), // Buffer size
	0);                                     // Must be zero
    if (configRetVal != CR_SUCCESS) {
        write_log ("CM_Get_Device_ID failed with error: %d\n", configRetVal);
        return FALSE;
    }

    //
    // We won't know the size of the HardwareID buffer until we call
    // this function. So call it with a null to begin with, and then 
    // use the required buffer size to Alloc the necessary space.
    // Call it again with the obtained buffer size
    //
    status = SetupDiGetDeviceRegistryProperty(
	DevInfo,
	&deviceInfoData,
	SPDRP_HARDWAREID,
	&dataType,
	(PBYTE)buffer,
	bufferSize,
	&bufferSize);

    if (status == FALSE) {
	errorCode = GetLastError();
	if (errorCode != ERROR_INSUFFICIENT_BUFFER) {
	    if (errorCode == ERROR_INVALID_DATA) {
		//
		// May be a Legacy Device with no HardwareID. Continue.
		//
		write_log ("No Hardware ID, may be a legacy device!\n");
	} else {
		write_log ("SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode);
		return FALSE;
	    }
	}
    }

    //
    // We need to change the buffer size.
    //
    buffer = LocalAlloc(LPTR, bufferSize);
    status = SetupDiGetDeviceRegistryProperty(
	DevInfo,
	&deviceInfoData,
	SPDRP_HARDWAREID,
	&dataType,
	(PBYTE)buffer,
	bufferSize,
	&bufferSize);

    if (status == FALSE) {
	errorCode = GetLastError();
	if (errorCode == ERROR_INVALID_DATA) {
	    //
	    // May be a Legacy Device with no HardwareID. Continue.
	    //
	    write_log ("No Hardware ID, may be a legacy device!\n");
	} else {
	    write_log ("SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode);
	    return FALSE;
	}
    }
    write_log ("Device ID '%s'\n",buffer);

    if (buffer)
        LocalFree(buffer);

    // **** Now get the Device Description

    //
    // We won't know the size of the Location Information buffer until
    // we call this function. So call it with a null to begin with,
    // and then use the required buffer size to Alloc the necessary space.
    // Call it again with the obtained buffer size
    //

    status = SetupDiGetDeviceRegistryProperty(
	DevInfo,
	&deviceInfoData,
	SPDRP_DEVICEDESC,
	&dataType,
	(PBYTE)buffer,
	bufferSize,
	&bufferSize);

    if (status == FALSE) {
	errorCode = GetLastError();
	if (errorCode != ERROR_INSUFFICIENT_BUFFER) {
	    if (errorCode == ERROR_INVALID_DATA) {
		write_log("No Device Description!\n");
	    } else {
		write_log("SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode);
		return FALSE;
	    }
	}
    }

    //
    // We need to change the buffer size.
    //

    buffer = LocalAlloc(LPTR, bufferSize);
    
    status = SetupDiGetDeviceRegistryProperty(
	DevInfo,
	&deviceInfoData,
	SPDRP_DEVICEDESC,
	&dataType,
	(PBYTE)buffer,
	bufferSize,
	&bufferSize);

    if (status == FALSE) {
	errorCode = GetLastError();
	if (errorCode == ERROR_INVALID_DATA) {
	    write_log ("No Device Description!\n");
	} else {
	    write_log ("SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode);
	    return FALSE;
	}
    }

    write_log ("Device Description '%s'\n",buffer);

    if (buffer)
        LocalFree(buffer);

    // Reenumerate the SCSI adapter device node
    configRetVal = CM_Reenumerate_DevNode(deviceInfoData.DevInst, 0);
    
    if (configRetVal != CR_SUCCESS) {
	write_log ("CM_Reenumerate_DevNode failed with error: %d\n", configRetVal);
	return FALSE;
    }

    GetInquiryData ((PCTSTR)&deviceInstanceId, Index);

    // Find the children devices
    GetChildDevices (deviceInfoData.DevInst);

    return TRUE;
}

static int getCDROMProperty(int idx, HDEVINFO DevInfo, const GUID *guid)
{
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA interfaceDetailData = NULL;
    DWORD interfaceDetailDataSize, reqSize;
    DWORD status, errorCode;

    interfaceData.cbSize = sizeof (SP_INTERFACE_DEVICE_DATA);
    status = SetupDiEnumDeviceInterfaces ( 
	DevInfo,	// Interface Device Info handle
	0,		// Device Info data
	guid,		// Interface registered by driver
	idx,		// Member
	&interfaceData	// Device Interface Data
	);
    if (status == FALSE)
	return FALSE;

    status = SetupDiGetDeviceInterfaceDetail (
	DevInfo,	// Interface Device info handle
	&interfaceData,	// Interface data for the event class
	NULL,		// Checking for buffer size
	0,		// Checking for buffer size
	&reqSize,	// Buffer size required to get the detail data
	NULL		// Checking for buffer size
	);

    if (status == FALSE) {
	errorCode = GetLastError();
	if (errorCode != ERROR_INSUFFICIENT_BUFFER)
	    return FALSE;
    }

    interfaceDetailDataSize = reqSize;
    interfaceDetailData = malloc (interfaceDetailDataSize);
    if (interfaceDetailData == NULL)
	return FALSE;
    interfaceDetailData->cbSize = sizeof (SP_INTERFACE_DEVICE_DETAIL_DATA);

    status = SetupDiGetDeviceInterfaceDetail (
		DevInfo,		// Interface Device info handle
		&interfaceData,		// Interface data for the event class
		interfaceDetailData,	// Interface detail data
		interfaceDetailDataSize,// Interface detail data size
		&reqSize,		// Buffer size required to get the detail data
		NULL);			// Interface device info

    if (status == FALSE)
	return FALSE;

    adddrive (interfaceDetailData->DevicePath, -1, -1, -1, -1);

    free (interfaceDetailData);

    return TRUE;
}

static const GUID *guids[] = {
    &GUID_DEVINTERFACE_CDROM,
    &GUID_DEVCLASS_IMAGE,
    &GUID_DEVCLASS_TAPEDRIVE,
    NULL };
static const char *scsinames[] = { "Tape", "Scanner", NULL };

static int rescan(void)
{
    HDEVINFO hDevInfo;
    HANDLE h;
    int idx, idx2;
    int first;
    char tmp[100];

    for (idx2 = 0; guids[idx2]; idx2++) {
	hDevInfo = SetupDiGetClassDevs(
	    guids[idx2],
	    NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
	if (hDevInfo != INVALID_HANDLE_VALUE) {
	    for (idx = 0; ; idx++) {
		if (!getCDROMProperty(idx, hDevInfo, guids[idx2]))
		    break;
	    }
	    SetupDiDestroyDeviceInfoList(hDevInfo);
	}
    }

    for (idx2 = 0; scsinames[idx2]; idx2++) {
	int max = 10;
	for (idx = 0; idx < max; idx++) {
	    sprintf (tmp, "\\\\.\\%s%d", scsinames[idx2], idx);
	    h = CreateFile(tmp, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	    if (h != INVALID_HANDLE_VALUE) {
		adddrive(tmp, -1, -1, -1, -1);
		CloseHandle(h);
		if (idx == max - 1)
		    max++;
	    }
	}
    }
/*
    first = 1;
    hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_SCSIADAPTER, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
	for (idx = 0; ; idx++) {
	    if (!GetRegistryProperty(hDevInfo, idx, &first))
		break;
	}
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
*/
    return 1;
}


#endif
