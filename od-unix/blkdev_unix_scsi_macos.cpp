/*
 * UAE - The Un*x Amiga Emulator
 *
 * macOS SCSITaskLib native SCSI passthrough
 *
 * Copyright 2026 WinUAE contributors
 */

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WITH_SCSI_SPTI

#include "options.h"
#include "threaddep/thread.h"
#include "blkdev.h"
#include "execio.h"
#include "gui.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/scsi/SCSITaskLib.h>

#define INQUIRY_SIZE 36

struct dev_info_scsi_macos {
	io_service_t service;
	UInt64 registry_id;
	bool open;
	bool exclusive;
	bool enabled;
	bool removable;
	int type;
	TCHAR label[MAX_DPATH];
	uae_u8 inquirydata[INQUIRY_SIZE];
	uae_u8 sense[sizeof(SCSI_Sense_Data)];
	int senselen;
	uae_u8 *scsibuf;
	SCSITaskDeviceInterface **interface;
	struct device_info di;
};

static struct dev_info_scsi_macos dev_info[MAX_TOTAL_SCSI_DEVICES];
static int unittable[MAX_TOTAL_SCSI_DEVICES];
static int total_devices;
static int bus_open;
static uae_sem_t scsi_sem;

static void release_interface(struct dev_info_scsi_macos *di)
{
	if (!di || !di->interface) {
		return;
	}
	if (di->exclusive) {
		(*di->interface)->ReleaseExclusiveAccess(di->interface);
		di->exclusive = false;
	}
	(*di->interface)->Release(di->interface);
	di->interface = NULL;
}

static bool create_interface(io_service_t service, SCSITaskDeviceInterface ***out)
{
	IOCFPlugInInterface **plugin = NULL;
	SCSITaskDeviceInterface **interface = NULL;
	SInt32 score = 0;

	if (!out) {
		return false;
	}
	*out = NULL;
	IOReturn kr = IOCreatePlugInInterfaceForService(service,
		kIOSCSITaskDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
	if (kr != kIOReturnSuccess || !plugin) {
		return false;
	}
	HRESULT hr = (*plugin)->QueryInterface(plugin,
		CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID), (LPVOID *)&interface);
	(*plugin)->Release(plugin);
	if (hr || !interface) {
		return false;
	}
	*out = interface;
	return true;
}

static bool open_interface(struct dev_info_scsi_macos *di, bool exclusive)
{
	if (!di) {
		return false;
	}
	if (di->interface) {
		return true;
	}
	if (!create_interface(di->service, &di->interface)) {
		return false;
	}
	if (exclusive) {
		IOReturn kr = (*di->interface)->ObtainExclusiveAccess(di->interface);
		if (kr != kIOReturnSuccess) {
			write_log(_T("SCSITaskLib: exclusive access failed for '%s': 0x%08x\n"), di->label, kr);
			release_interface(di);
			return false;
		}
		di->exclusive = true;
	}
	return true;
}

static int normalized_cdb_size(int cmdlen)
{
	if (cmdlen <= 6) {
		return kSCSICDBSize_6Byte;
	}
	if (cmdlen <= 10) {
		return kSCSICDBSize_10Byte;
	}
	if (cmdlen <= 12) {
		return kSCSICDBSize_12Byte;
	}
	return kSCSICDBSize_16Byte;
}

static int execute_task(struct dev_info_scsi_macos *di, const uae_u8 *cmd, int cmdlen,
	uae_u8 *data, int datalen, int direction, uae_u8 *sense, int senselen,
	uae_u8 *statusp, int *actualp, int timeout_ms)
{
	if (!di || !di->interface || !cmd || cmdlen <= 0 || cmdlen > kSCSICDBSize_Maximum) {
		return -1;
	}

	SCSITaskInterface **task = (*di->interface)->CreateSCSITask(di->interface);
	if (!task) {
		return -1;
	}

	UInt8 cdb[kSCSICDBSize_Maximum];
	memset(cdb, 0, sizeof(cdb));
	memcpy(cdb, cmd, cmdlen);

	IOReturn kr = (*task)->SetTaskAttribute(task, kSCSITask_SIMPLE);
	if (kr == kIOReturnSuccess) {
		kr = (*task)->SetCommandDescriptorBlock(task, cdb, normalized_cdb_size(cmdlen));
	}
	if (kr == kIOReturnSuccess) {
		kr = (*task)->SetTimeoutDuration(task, timeout_ms > 0 ? timeout_ms : 5000);
	}
	if (kr == kIOReturnSuccess) {
		SCSITaskSGElement sg;
		memset(&sg, 0, sizeof(sg));
		UInt8 transfer_direction = kSCSIDataTransfer_NoDataTransfer;
		if (datalen > 0 && data) {
			sg.address = (IOVirtualAddress)data;
			sg.length = datalen;
			transfer_direction = direction;
		}
		kr = (*task)->SetScatterGatherEntries(task, datalen > 0 && data ? &sg : NULL,
			datalen > 0 && data ? 1 : 0, datalen > 0 && data ? datalen : 0, transfer_direction);
	}

	SCSI_Sense_Data sense_data;
	memset(&sense_data, 0, sizeof(sense_data));
	SCSITaskStatus task_status = kSCSITaskStatus_No_Status;
	UInt64 realized = 0;
	if (kr == kIOReturnSuccess) {
		kr = (*task)->ExecuteTaskSync(task, &sense_data, &task_status, &realized);
	}

	if (sense && senselen > 0) {
		int copylen = std::min(senselen, (int)sizeof(sense_data));
		memcpy(sense, &sense_data, copylen);
	}
	if (statusp) {
		*statusp = (uae_u8)task_status;
	}
	if (actualp) {
		*actualp = (int)std::min<UInt64>(realized, datalen > 0 ? (UInt64)datalen : 0);
	}

	(*task)->Release(task);
	return kr == kIOReturnSuccess ? 0 : -1;
}

static bool inquiry_device(struct dev_info_scsi_macos *di, uae_u8 *statusp)
{
	uae_u8 cmd[6] = { 0x12, 0, 0, 0, INQUIRY_SIZE, 0 };
	int actual = 0;

	memset(di->inquirydata, 0, sizeof(di->inquirydata));
	if (execute_task(di, cmd, sizeof(cmd), di->inquirydata, INQUIRY_SIZE,
		kSCSIDataTransfer_FromTargetToInitiator, di->sense, sizeof(di->sense),
		statusp, &actual, 5000) < 0) {
		return false;
	}
	return (!statusp || *statusp == 0) && actual >= 5;
}

static bool trim_copy(char *dst, size_t dstsize, const uae_u8 *src, size_t len)
{
	if (!dst || dstsize == 0) {
		return false;
	}
	while (len > 0 && src[len - 1] == ' ') {
		len--;
	}
	size_t start = 0;
	while (start < len && src[start] == ' ') {
		start++;
	}
	len -= start;
	if (len >= dstsize) {
		len = dstsize - 1;
	}
	memcpy(dst, src + start, len);
	dst[len] = 0;
	return len > 0;
}

static void inquiry_label(TCHAR *dst, size_t dstlen, const char *fallback, const uae_u8 *inq)
{
	char vendor[9] = { 0 };
	char product[17] = { 0 };
	char revision[5] = { 0 };
	char label[MAX_DPATH];

	trim_copy(vendor, sizeof(vendor), inq + 8, 8);
	trim_copy(product, sizeof(product), inq + 16, 16);
	trim_copy(revision, sizeof(revision), inq + 32, 4);
	if (vendor[0] || product[0]) {
		snprintf(label, sizeof(label), "%s %s %s", vendor, product, revision);
	} else {
		snprintf(label, sizeof(label), "%s", fallback);
	}
	TCHAR *tlabel = au(label);
	_tcsncpy(dst, tlabel, dstlen - 1);
	dst[dstlen - 1] = 0;
	xfree(tlabel);
}

static struct dev_info_scsi_macos *unitcheck(int unitnum)
{
	if (unitnum < 0 || unitnum >= MAX_TOTAL_SCSI_DEVICES) {
		return NULL;
	}
	if (unittable[unitnum] <= 0) {
		return NULL;
	}
	return &dev_info[unittable[unitnum] - 1];
}

static struct dev_info_scsi_macos *unitisopen(int unitnum)
{
	struct dev_info_scsi_macos *di = unitcheck(unitnum);
	if (!di || !di->open) {
		return NULL;
	}
	return di;
}

static void update_device_info(int unitnum)
{
	struct dev_info_scsi_macos *dmac = unitcheck(unitnum);
	if (!dmac) {
		return;
	}
	struct device_info *di = &dmac->di;
	memset(di, 0, sizeof(*di));
	_tcscpy(di->label, dmac->label);
	_tcscpy(di->mediapath, dmac->label);
	di->type = dmac->type;
	di->media_inserted = dmac->open ? 1 : -1;
	di->removable = dmac->removable;
	di->write_protected = true;
	di->bytespersector = dmac->type == INQ_ROMD ? 2048 : 512;
	di->trackspercylinder = 1;
	di->sectorspertrack = 1;
	di->cylinders = 0;
	di->bus = 0;
	di->target = unitnum;
	di->lun = 0;
	di->unitnum = unitnum + 1;
	di->backend = _T("SCSITaskLib");
}

static bool add_service(io_service_t service)
{
	if (total_devices >= MAX_TOTAL_SCSI_DEVICES) {
		return false;
	}

	UInt64 registry_id = 0;
	if (IORegistryEntryGetRegistryEntryID(service, &registry_id) != kIOReturnSuccess) {
		registry_id = 0;
	}
	for (int i = 0; i < total_devices; i++) {
		if (registry_id && dev_info[i].registry_id == registry_id) {
			return false;
		}
	}

	SCSITaskDeviceInterface **test_interface = NULL;
	if (!create_interface(service, &test_interface)) {
		return false;
	}
	(*test_interface)->Release(test_interface);

	io_name_t name;
	if (IORegistryEntryGetName(service, name) != kIOReturnSuccess) {
		strcpy(name, "SCSITask device");
	}

	struct dev_info_scsi_macos *di = &dev_info[total_devices];
	memset(di, 0, sizeof(*di));
	di->service = service;
	IOObjectRetain(di->service);
	di->registry_id = registry_id;
	di->enabled = true;
	di->type = INQ_DASD;
	TCHAR *tname = au(name);
	_tcsncpy(di->label, tname, sizeof(di->label) / sizeof(TCHAR) - 1);
	xfree(tname);

	write_log(_T("SCSITaskLib: unit %d '%s'\n"), total_devices, di->label);
	total_devices++;
	return true;
}

static void enumerate_class(const char *classname)
{
	CFMutableDictionaryRef matching = IOServiceMatching(classname);
	if (!matching) {
		return;
	}
	io_iterator_t iterator = IO_OBJECT_NULL;
	if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != kIOReturnSuccess) {
		return;
	}
	io_service_t service;
	while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
		add_service(service);
		IOObjectRelease(service);
	}
	IOObjectRelease(iterator);
}

static int open_macos_device2(struct dev_info_scsi_macos *di, int unitnum)
{
	if (!di || !di->enabled) {
		return 0;
	}
	if (!di->scsibuf) {
		di->scsibuf = xmalloc(uae_u8, DEVICE_SCSI_BUFSIZE);
	}
	if (!open_interface(di, true)) {
		return 0;
	}

	uae_u8 status = 0;
	if (inquiry_device(di, &status)) {
		di->type = di->inquirydata[0] & 31;
		di->removable = (di->inquirydata[1] & 0x80) != 0;
		inquiry_label(di->label, sizeof(di->label) / sizeof(TCHAR), "SCSITask device", di->inquirydata);
	} else {
		write_log(_T("SCSITaskLib: inquiry failed for '%s' status=%02x\n"), di->label, status);
	}
	di->open = true;
	update_device_info(unitnum);
	return 1;
}

static void close_macos_device2(struct dev_info_scsi_macos *di)
{
	if (!di) {
		return;
	}
	di->open = false;
	release_interface(di);
}

static int open_macos_device(int unitnum, const TCHAR *ident, int flags)
{
	struct dev_info_scsi_macos *di = NULL;
	if (ident && ident[0]) {
		for (int i = 0; i < total_devices; i++) {
			di = &dev_info[i];
			if (!_tcsicmp(di->label, ident)) {
				unittable[unitnum] = i + 1;
				if (open_macos_device2(di, unitnum)) {
					return 1;
				}
				unittable[unitnum] = 0;
				return 0;
			}
		}
		return 0;
	}
	if (unitnum >= total_devices) {
		return 0;
	}
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (unittable[i] == unitnum + 1) {
			return 0;
		}
	}
	di = &dev_info[unitnum];
	unittable[unitnum] = unitnum + 1;
	if (open_macos_device2(di, unitnum)) {
		return 1;
	}
	unittable[unitnum] = 0;
	return 0;
}

static void close_macos_device(int unitnum)
{
	struct dev_info_scsi_macos *di = unitisopen(unitnum);
	if (!di) {
		return;
	}
	close_macos_device2(di);
	unittable[unitnum] = 0;
}

static struct device_info *info_macos_device(int unitnum, struct device_info *di, int quick, int session)
{
	struct dev_info_scsi_macos *dmac = unitcheck(unitnum);
	if (!dmac) {
		return NULL;
	}
	if (!quick) {
		update_device_info(unitnum);
	}
	dmac->di.open = dmac->open;
	memcpy(di, &dmac->di, sizeof(struct device_info));
	return di;
}

static int exec_macos_direct(int unitnum, struct amigascsi *as)
{
	struct dev_info_scsi_macos *di = unitisopen(unitnum);
	if (!di) {
		return -1;
	}

	uae_u8 cmd[kSCSICDBSize_Maximum];
	uae_u8 *scsi_datap = as->len ? as->data : NULL;
	int datalen = as->len;
	int cmdlen = std::min(as->cmd_len, (int)sizeof(cmd));
	if (datalen > DEVICE_SCSI_BUFSIZE) {
		datalen = DEVICE_SCSI_BUFSIZE;
	}
	memcpy(cmd, as->cmd, cmdlen);
	if (datalen > 0 && scsi_datap) {
		memcpy(di->scsibuf, scsi_datap, datalen);
	}

	const int direction = (as->flags & 1)
		? kSCSIDataTransfer_FromTargetToInitiator
		: kSCSIDataTransfer_FromInitiatorToTarget;
	uae_u8 status = 0;
	int actual = 0;
	int senselen = std::min((int)as->sense_len, (int)sizeof(di->sense));

	uae_sem_wait(&scsi_sem);
	int ret = execute_task(di, cmd, cmdlen, datalen > 0 ? di->scsibuf : NULL, datalen,
		direction, di->sense, senselen, &status, &actual, 80 * 60 * 1000);
	uae_sem_post(&scsi_sem);

	if (datalen > 0 && scsi_datap && direction == kSCSIDataTransfer_FromTargetToInitiator) {
		memcpy(scsi_datap, di->scsibuf, actual);
	}

	as->status = status;
	as->cmdactual = ret < 0 ? 0 : as->cmd_len;
	as->sactual = 0;
	if (status) {
		for (int i = 0; i < senselen; i++) {
			as->sensedata[i] = di->sense[i];
			as->sactual++;
		}
		as->actual = 0;
		return IOERR_BadStatus;
	}
	if (ret < 0) {
		as->actual = 0;
		return IOERR_NotSpecified;
	}
	as->len = actual;
	as->actual = actual;
	for (int i = 0; i < senselen; i++) {
		as->sensedata[i] = 0;
	}
	return 0;
}

static uae_u8 *exec_macos_out(int unitnum, uae_u8 *data, int len)
{
	struct amigascsi as;
	memset(&as, 0, sizeof(as));
	as.data = data;
	as.len = 0;
	memcpy(as.cmd, data, len);
	as.cmd_len = len;
	as.flags = 0;
	if (exec_macos_direct(unitnum, &as) != 0) {
		return NULL;
	}
	return data;
}

static uae_u8 *exec_macos_in(int unitnum, uae_u8 *data, int len, int *outlen)
{
	struct dev_info_scsi_macos *di = unitisopen(unitnum);
	if (!di) {
		return NULL;
	}
	struct amigascsi as;
	memset(&as, 0, sizeof(as));
	as.data = di->scsibuf;
	as.len = DEVICE_SCSI_BUFSIZE;
	memcpy(as.cmd, data, len);
	as.cmd_len = len;
	as.flags = 1;
	as.sense_len = sizeof(as.sensedata);
	if (exec_macos_direct(unitnum, &as) != 0 || as.actual <= 0) {
		return NULL;
	}
	if (outlen) {
		*outlen = std::min(*outlen, (int)as.actual);
	}
	return di->scsibuf;
}

static int macos_isatapi(int unitnum)
{
	return 0;
}

static int macos_ismedia(int unitnum, int quick)
{
	struct dev_info_scsi_macos *di = unitisopen(unitnum);
	return di ? 1 : -1;
}

static int open_macos_bus(int flags)
{
	if (bus_open) {
		write_log(_T("SCSITaskLib open_bus() more than once!\n"));
		return 1;
	}
	total_devices = 0;
	memset(dev_info, 0, sizeof(dev_info));
	memset(unittable, 0, sizeof(unittable));
	uae_sem_init(&scsi_sem, 0, 1);

	enumerate_class("IOSCSITaskDevice");
	enumerate_class("IOSCSIPeripheralDeviceNub");
	enumerate_class("IOSCSIPrimaryCommandsDevice");
	enumerate_class("IOSCSIMultimediaCommandsDevice");

	bus_open = 1;
	write_log(_T("SCSITaskLib driver open, %d devices.\n"), total_devices);
	return total_devices;
}

static void close_macos_bus(void)
{
	if (!bus_open) {
		write_log(_T("SCSITaskLib close_bus() when already closed!\n"));
		return;
	}
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		close_macos_device2(&dev_info[i]);
		xfree(dev_info[i].scsibuf);
		dev_info[i].scsibuf = NULL;
		if (dev_info[i].service) {
			IOObjectRelease(dev_info[i].service);
			dev_info[i].service = IO_OBJECT_NULL;
		}
		unittable[i] = 0;
	}
	total_devices = 0;
	bus_open = 0;
	uae_sem_destroy(&scsi_sem);
	write_log(_T("SCSITaskLib driver closed.\n"));
}

struct device_functions devicefunc_scsi_spti = {
	_T("SCSITaskLib"),
	open_macos_bus, close_macos_bus, open_macos_device, close_macos_device, info_macos_device,
	exec_macos_out, exec_macos_in, exec_macos_direct,
	0, 0, 0, 0, 0,
	0, 0, 0, 0,
	macos_isatapi, macos_ismedia, 0
};

#endif /* WITH_SCSI_SPTI */
