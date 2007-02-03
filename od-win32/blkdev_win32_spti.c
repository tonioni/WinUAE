 /*
  * UAE - The Un*x Amiga Emulator
  *
  * WIN32 CDROM/HD low level access code (SPTI)
  *
  * Copyright 2002-2005 Toni Wilen
  *
  */


#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x500

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"

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

#include <ntddscsi.h>

#define INQUIRY_SIZE 36

typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
  SCSI_PASS_THROUGH_DIRECT spt;
  ULONG Filler;
  UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

static int unitcnt = 0;

struct dev_info_spti {
    char *drvpath;
    char *name;
    char *inquirydata;
    int mediainserted;
    HANDLE handle;
    int isatapi;
    int type;
    int bus, path, target, lun;
    int scanmode;
    uae_u8 *scsibuf;
};

static uae_sem_t scgp_sem;
static struct dev_info_spti dev_info[MAX_TOTAL_DEVICES];

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
	write_log("Status = %d, Error code = %d, LastError=%d\n", status, swb->spt.ScsiStatus, lasterror);
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
    int v = execscsicmd (unitnum, data, len, dev_info[unitnum].scsibuf, DEVICE_SCSI_BUFSIZE);
    if (v < 0)
	return 0;
    if (v == 0)
	return 0;
    if (outlen)
	*outlen = v;
    return dev_info[unitnum].scsibuf;
}

static int total_devices;


static void close_scsi_device (int unitnum)
{
    write_log ("SPTI: unit %d closed\n", unitnum);
    if (dev_info[unitnum].handle != INVALID_HANDLE_VALUE)
	CloseHandle (dev_info[unitnum].handle);
    dev_info[unitnum].handle = INVALID_HANDLE_VALUE;
}

static void free_scsi_device(int dev)
{
    close_scsi_device (dev);
    xfree(dev_info[dev].name);
    xfree(dev_info[dev].drvpath);
    xfree(dev_info[dev].inquirydata);
    VirtualFree (dev_info[dev].scsibuf, 0, MEM_RELEASE);
    dev_info[dev].name = NULL;
    dev_info[dev].drvpath = NULL;
    dev_info[dev].inquirydata = NULL;
    dev_info[dev].scsibuf = NULL;
    memset(&dev_info[dev], 0, sizeof (struct dev_info_spti));
}

static int rescan(void);
static int open_scsi_bus (int flags)
{
    int i;

    total_devices = 0;
    uae_sem_init (&scgp_sem, 0, 1);
    for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
	memset (&dev_info[i], 0, sizeof (struct dev_info_spti));
	dev_info[i].handle = INVALID_HANDLE_VALUE;
    }
    rescan();
    return total_devices;
}

static void close_scsi_bus (void)
{
    int i;
    for (i = 0; i < total_devices; i++)
	free_scsi_device(i);
}

static int inquiry (int unitnum, int *type, uae_u8 *inquirydata, int *inqlen)
{
    uae_u8 cmd[6] = { 0x12,0,0,0,36,0 }; /* INQUIRY */
    uae_u8 out[INQUIRY_SIZE] = { 0 };
    int outlen = sizeof (out);
    uae_u8 *p = execscsicmd_in (unitnum, cmd, sizeof (cmd), &outlen);
    int v = 0;

    *inqlen = 0;
    *type = 0x1f;
    if (!p) {
	if (log_scsi)
	    write_log("SPTI: INQUIRY failed\n");
	return 0;
    }
    *inqlen = outlen > INQUIRY_SIZE ? INQUIRY_SIZE : outlen;
    if (outlen >= 1)
	*type = p[0] & 31;
    if (outlen >= 2 && (p[0] & 31) == 5 && (p[2] & 7) == 0)
	v = 1;
    memcpy (inquirydata, p, *inqlen);
    if (log_scsi) {
	if (outlen >= INQUIRY_SIZE) 
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

static int mediacheck_full (int unitnum, struct device_info *di)
{
    uae_u8 cmd1[10] = { 0x25,0,0,0,0,0,0,0,0,0 }; /* READ CAPACITY */
    uae_u8 *p;
    int outlen;

    di->bytespersector = 2048;
    di->cylinders = 1;
    di->write_protected = 1;
    if (dev_info[unitnum].handle == INVALID_HANDLE_VALUE)
	return 0;
    outlen = 32;
    p = execscsicmd_in(unitnum, cmd1, sizeof cmd1, &outlen);
    if (p && outlen >= 8) {
	di->bytespersector = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
	di->cylinders = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }
    if (di->type == INQ_DASD) {
	uae_u8 cmd2[10] = { 0x5a,0x08,0,0,0,0,0,0,0xf0,0 }; /* MODE SENSE */
	outlen = 32;
	p = execscsicmd_in(unitnum, cmd2, sizeof cmd2, &outlen);
	if (p && outlen >= 4) {
	    di->write_protected = (p[3]& 0x80) ? 1 : 0;
	}
    }
    return 1;
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
    if (!di->scsibuf)
	di->scsibuf = VirtualAlloc (NULL, DEVICE_SCSI_BUFSIZE, MEM_COMMIT, PAGE_READWRITE);
    h = CreateFile(dev,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    di->handle = h;
    if (h == INVALID_HANDLE_VALUE) {
	write_log ("SPTI: failed to open unit %d err=%d ('%s')\n", unitnum, GetLastError(), dev);
    } else {
	uae_u8 inqdata[INQUIRY_SIZE + 1] = { 0 };
	int inqlen;
        dev_info[unitnum].isatapi = inquiry (unitnum, &dev_info[unitnum].type, inqdata, &inqlen);
	if (inqlen == 0) {
	    write_log ("SPTI: inquiry failed unit %d ('%s':%d:%d:%d:%d)\n", unitnum, dev,
		di->bus, di->path, di->target, di->lun);
	    close_scsi_device (unitnum);
	    xfree (dev);
	    return 0;
	}
	inqdata[INQUIRY_SIZE] = 0;
	if (dev_info[unitnum].type == INQ_ROMD) {
	    dev_info[unitnum].mediainserted = mediacheck (unitnum);
	    write_log ("SPTI: unit %d opened [%s], %s, '%s'\n", unitnum,
		dev_info[unitnum].isatapi ? "ATAPI" : "SCSI",
		dev_info[unitnum].mediainserted ? "media inserted" : "drive empty", inqdata + 8);
	} else {
	    write_log ("SPTI: unit %d, type %d, '%s'\n",
		unitnum, dev_info[unitnum].type, inqdata + 8);
	}
	dev_info[unitnum].name = my_strdup (inqdata + 8);
	dev_info[unitnum].inquirydata = xmalloc (INQUIRY_SIZE);
	memcpy (dev_info[unitnum].inquirydata, inqdata, INQUIRY_SIZE);
        xfree (dev);
	return 1;
    }
    xfree (dev);
    return 0;
}

static int adddrive (char *drvpath, int bus, int pathid, int targetid, int lunid, int scanmode)
{
    struct dev_info_spti *di;
    int cnt = total_devices, i;
    int freeit = 1;

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
    di->scanmode = scanmode;
    total_devices++;
    if (open_scsi_device(cnt)) {
	for (i = 0; i < cnt; i++) {
	    if (!memcmp(di->inquirydata, dev_info[i].inquirydata, INQUIRY_SIZE) && di->scanmode != dev_info[i].scanmode) {
		write_log("duplicate device, skipped..\n");
		break;
	    }
	}
	if (i == cnt) {
	    freeit = 0;
	    close_scsi_device(cnt);
	}
    }
    if (freeit) {
	free_scsi_device(cnt);
	total_devices--;
    }
    return 1;
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
    mediacheck_full (unitnum, di);
    di->type = dev_info[unitnum].type;
    di->id = unitnum + 1;
    if (log_scsi) {
	write_log ("MI=%d TP=%d WP=%d CY=%d BK=%d '%s'\n",
	    di->media_inserted, di->type, di->write_protected, di->cylinders, di->bytespersector, di->label);
    }
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

static struct device_scsi_info *scsi_info (int unitnum, struct device_scsi_info *dsi)
{
    dsi->buffer = dev_info[unitnum].scsibuf;
    dsi->bufsize = DEVICE_SCSI_BUFSIZE;
    return dsi;
}

struct device_functions devicefunc_win32_spti = {
    open_scsi_bus, close_scsi_bus, open_scsi_device, close_scsi_device, info_device,
    execscsicmd_out, execscsicmd_in, execscsicmd_direct,
    0, 0, 0, 0, 0, 0, 0, check_isatapi, scsi_info, 0
};

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

    adddrive (interfaceDetailData->DevicePath, -1, -1, -1, -1, 1);

    free (interfaceDetailData);

    return TRUE;
}

#define	SCSI_INFO_BUFFER_SIZE 0x5000
static void scanscsi(void)
{
    PSCSI_BUS_DATA BusData;
    PSCSI_INQUIRY_DATA InquiryData;
    PSCSI_ADAPTER_BUS_INFO AdapterInfo;
    HANDLE h;
    BOOL status;
    BOOL Claimed;
    ULONG returnedLength;
    SHORT Bus, Luns;
    DWORD bytesTransferred;
    int idx;
    char DeviceName[256];

    AdapterInfo = (PSCSI_ADAPTER_BUS_INFO)xmalloc(SCSI_INFO_BUFFER_SIZE) ;
    if (AdapterInfo == NULL)
	return;

    idx = 0;
    for (;;) {
        sprintf(DeviceName, "\\\\.\\Scsi%d:", idx++);
        h = CreateFile (DeviceName,
	    GENERIC_READ | GENERIC_WRITE,
	    0,
	    NULL, // no SECURITY_ATTRIBUTES structure
	    OPEN_EXISTING, // No special create flags
	    0, // No special attributes
	    NULL);
	if (h == INVALID_HANDLE_VALUE)
	    return;

	if(!DeviceIoControl(h,
	    IOCTL_SCSI_RESCAN_BUS,
	    NULL,
	    0,
	    NULL,
	    0,
	    &bytesTransferred,
	    NULL)) {
	    write_log( "Rescan SCSI port %d failed [Error %d]\n", idx - 1, GetLastError());
	    CloseHandle(h);
            continue;
        }

	// Get the SCSI inquiry data for all devices for the given SCSI bus
	status = DeviceIoControl(	
	    h,
	    IOCTL_SCSI_GET_INQUIRY_DATA,
	    NULL,
	    0,
	    AdapterInfo,
	    SCSI_INFO_BUFFER_SIZE,
	    &returnedLength,
	    NULL);

	if (!status) {
	    write_log ("Error in IOCTL_SCSI_GET_INQUIRY_DATA\n" );
	    CloseHandle (h);
	    continue;
	}

	for (Bus = 0; Bus < AdapterInfo->NumberOfBuses; Bus++) {
	    int luncheck = 0;
	    BusData = &AdapterInfo->BusData[Bus];
	    InquiryData = (PSCSI_INQUIRY_DATA) ( (PUCHAR) AdapterInfo + BusData->InquiryDataOffset );
	    for (Luns = 0; Luns < BusData->NumberOfLogicalUnits; Luns++) {
		char label[100];
		int type = InquiryData->InquiryData[0] & 0x1f;
		Claimed = InquiryData->DeviceClaimed;
		write_log ("SCSI=%d Initiator=%d Path=%d Target=%d LUN=%d Claimed=%s Type=%d\n",
		    idx - 1,
		    BusData->InitiatorBusId, InquiryData->PathId, InquiryData->TargetId, 
		    InquiryData->Lun, Claimed ? "Yes" : "No ", type);
		if (Claimed == 0 && !luncheck) {
		    luncheck = 1;
		    sprintf (label, "SCSI(%d):%d:%d:%d:%d", idx - 1, BusData->InitiatorBusId,
			InquiryData->PathId, InquiryData->TargetId, InquiryData->Lun);
		    adddrive (label, idx - 1, InquiryData->PathId, InquiryData->TargetId, InquiryData->Lun, 3);
		}
		InquiryData = (PSCSI_INQUIRY_DATA) ( (PUCHAR) AdapterInfo + InquiryData->NextInquiryDataOffset );
	    }   // for Luns
	}	// for Bus
	CloseHandle(h);
    }
}

static const GUID *guids[] = {
    &GUID_DEVINTERFACE_CDROM,
    &GUID_DEVCLASS_IMAGE,
    &GUID_DEVCLASS_TAPEDRIVE,
    NULL };
static const char *scsinames[] = { "Tape", "Scanner", "Changer", NULL };

static int rescan(void)
{
    int idx, idx2;

    for (idx2 = 0; guids[idx2]; idx2++) {
	HDEVINFO hDevInfo = SetupDiGetClassDevs(
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
	    char tmp[100];
	    HANDLE h;
	    sprintf (tmp, "\\\\.\\%s%d", scsinames[idx2], idx);
	    h = CreateFile(tmp, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	    if (h != INVALID_HANDLE_VALUE) {
		adddrive(tmp, -1, -1, -1, -1, 2);
		CloseHandle(h);
		if (idx == max - 1)
		    max++;
	    }
	}
    }
    if (currprefs.win32_uaescsimode == UAESCSI_SPTISCAN) {
	write_log("SCSI adapter enumeration..\n");
	scanscsi();
	write_log("SCSI adapter enumeration ends\n");
    }
    return 1;
}


#endif
