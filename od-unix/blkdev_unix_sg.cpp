/*
 * UAE - The Un*x Amiga Emulator
 *
 * Linux SG_IO native SCSI passthrough
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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define INQUIRY_SIZE 36

struct dev_info_sg {
	int fd;
	bool open;
	bool readonly;
	bool enabled;
	bool removable;
	bool isatapi;
	int type;
	char path[MAX_DPATH];
	char realpath[MAX_DPATH];
	TCHAR label[MAX_DPATH];
	uae_u8 inquirydata[INQUIRY_SIZE];
	uae_u8 sense[32];
	int senselen;
	uae_u8 *scsibuf;
	struct device_info di;
};

static struct dev_info_sg dev_info[MAX_TOTAL_SCSI_DEVICES];
static int unittable[MAX_TOTAL_SCSI_DEVICES];
static int total_devices;
static int bus_open;
static uae_sem_t scgp_sem;

static bool digits_only(const char *s)
{
	if (!s || !*s) {
		return false;
	}
	while (*s) {
		if (!isdigit((unsigned char)*s)) {
			return false;
		}
		s++;
	}
	return true;
}

static bool scsi_devname(const char *name)
{
	if (!name) {
		return false;
	}
	if (!strncmp(name, "sg", 2) && digits_only(name + 2)) {
		return true;
	}
	if (!strncmp(name, "sr", 2) && digits_only(name + 2)) {
		return true;
	}
	if (!strncmp(name, "scd", 3) && digits_only(name + 3)) {
		return true;
	}
	if (!strncmp(name, "st", 2) && digits_only(name + 2)) {
		return true;
	}
	if (!strncmp(name, "nst", 3) && digits_only(name + 3)) {
		return true;
	}
	return false;
}

static std::string resolved_path(const char *path)
{
	char tmp[PATH_MAX];
	if (realpath(path, tmp)) {
		return std::string(tmp);
	}
	return std::string(path);
}

static void append_if_new(std::vector<std::string> *paths, const std::string &path)
{
	const std::string real = resolved_path(path.c_str());
	for (const std::string &p : *paths) {
		if (resolved_path(p.c_str()) == real) {
			return;
		}
	}
	paths->push_back(path);
}

static std::vector<std::string> candidate_paths()
{
	std::vector<std::string> paths;
	DIR *dev = opendir("/dev");
	if (dev) {
		struct dirent *entry;
		while ((entry = readdir(dev)) != NULL) {
			if (scsi_devname(entry->d_name)) {
				std::string path("/dev/");
				path += entry->d_name;
				append_if_new(&paths, path);
			}
		}
		closedir(dev);
	}

	DIR *byid = opendir("/dev/tape/by-id");
	if (byid) {
		struct dirent *entry;
		while ((entry = readdir(byid)) != NULL) {
			if (entry->d_name[0] == '.') {
				continue;
			}
			std::string path("/dev/tape/by-id/");
			path += entry->d_name;
			append_if_new(&paths, path);
		}
		closedir(byid);
	}

	return paths;
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

static void inquiry_label(TCHAR *dst, size_t dstlen, const char *path, const uae_u8 *inq)
{
	char vendor[9] = { 0 };
	char product[17] = { 0 };
	char revision[5] = { 0 };
	char label[MAX_DPATH];

	trim_copy(vendor, sizeof(vendor), inq + 8, 8);
	trim_copy(product, sizeof(product), inq + 16, 16);
	trim_copy(revision, sizeof(revision), inq + 32, 4);
	snprintf(label, sizeof(label), "%s %s %s (%s)", vendor, product, revision, path);
	TCHAR *tlabel = au(label);
	_tcsncpy(dst, tlabel, dstlen - 1);
	dst[dstlen - 1] = 0;
	xfree(tlabel);
}

static struct dev_info_sg *unitcheck(int unitnum)
{
	if (unitnum < 0 || unitnum >= MAX_TOTAL_SCSI_DEVICES) {
		return NULL;
	}
	if (unittable[unitnum] <= 0) {
		return NULL;
	}
	return &dev_info[unittable[unitnum] - 1];
}

static struct dev_info_sg *unitisopen(int unitnum)
{
	struct dev_info_sg *di = unitcheck(unitnum);
	if (!di || !di->open) {
		return NULL;
	}
	return di;
}

static int sg_exec_fd(int fd, uae_u8 *cmd, int cmdlen, uae_u8 *data, int datalen,
	int direction, uae_u8 *sense, int senselen, uae_u8 *statusp, int *actualp, int timeout_ms)
{
	sg_io_hdr_t hdr;
	memset(&hdr, 0, sizeof(hdr));
	hdr.interface_id = 'S';
	hdr.cmdp = cmd;
	hdr.cmd_len = cmdlen;
	hdr.dxferp = data;
	hdr.dxfer_len = datalen;
	hdr.dxfer_direction = datalen > 0 ? direction : SG_DXFER_NONE;
	hdr.sbp = sense;
	hdr.mx_sb_len = senselen;
	hdr.timeout = timeout_ms > 0 ? timeout_ms : 5000;

	if (sense && senselen > 0) {
		memset(sense, 0, senselen);
	}
	if (ioctl(fd, SG_IO, &hdr) < 0) {
		return -1;
	}
	if (statusp) {
		*statusp = hdr.status;
	}
	if (actualp) {
		*actualp = datalen - hdr.resid;
	}
	if ((hdr.info & SG_INFO_OK_MASK) != SG_INFO_OK && hdr.status == 0) {
		return -1;
	}
	return 0;
}

static bool inquiry_fd(int fd, uae_u8 *inq, uae_u8 *statusp)
{
	uae_u8 cmd[6] = { 0x12, 0, 0, 0, INQUIRY_SIZE, 0 };
	uae_u8 sense[32];
	int actual = 0;
	memset(inq, 0, INQUIRY_SIZE);
	if (sg_exec_fd(fd, cmd, sizeof(cmd), inq, INQUIRY_SIZE, SG_DXFER_FROM_DEV,
		sense, sizeof(sense), statusp, &actual, 5000) < 0) {
		return false;
	}
	return (!statusp || *statusp == 0) && actual >= 5;
}

static int open_path_rw(const char *path, bool *readonly)
{
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd >= 0) {
		if (readonly) {
			*readonly = false;
		}
		return fd;
	}
	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd >= 0 && readonly) {
		*readonly = true;
	}
	return fd;
}

static bool add_device(const char *path)
{
	if (total_devices >= MAX_TOTAL_SCSI_DEVICES) {
		return false;
	}

	std::string real = resolved_path(path);
	for (int i = 0; i < total_devices; i++) {
		if (!strcmp(dev_info[i].realpath, real.c_str())) {
			return false;
		}
	}

	bool readonly = false;
	int fd = open_path_rw(path, &readonly);
	if (fd < 0) {
		return false;
	}

	uae_u8 inq[INQUIRY_SIZE];
	uae_u8 status = 0;
	if (!inquiry_fd(fd, inq, &status)) {
		close(fd);
		return false;
	}
	close(fd);

	struct dev_info_sg *di = &dev_info[total_devices];
	memset(di, 0, sizeof(*di));
	di->fd = -1;
	di->enabled = true;
	di->readonly = readonly;
	di->type = inq[0] & 31;
	di->removable = (inq[1] & 0x80) != 0;
	di->isatapi = di->type == INQ_ROMD && (inq[2] & 7) == 0;
	memcpy(di->inquirydata, inq, sizeof(di->inquirydata));
	strncpy(di->path, path, sizeof(di->path) - 1);
	strncpy(di->realpath, real.c_str(), sizeof(di->realpath) - 1);
	inquiry_label(di->label, sizeof(di->label) / sizeof(TCHAR), path, inq);

	write_log(_T("SG_IO: unit %d '%s' type=%d %s\n"),
		total_devices, di->label, di->type, readonly ? _T("readonly") : _T("read/write"));
	total_devices++;
	return true;
}

static int mediacheck(struct dev_info_sg *di)
{
	if (!di || !di->open) {
		return -1;
	}
	uae_u8 cmd[6] = { 0, 0, 0, 0, 0, 0 };
	uae_u8 status = 0;
	int actual = 0;
	if (sg_exec_fd(di->fd, cmd, sizeof(cmd), NULL, 0, SG_DXFER_NONE,
		di->sense, sizeof(di->sense), &status, &actual, 5000) < 0) {
		return 0;
	}
	return status == 0 ? 1 : 0;
}

static void update_device_info(int unitnum)
{
	struct dev_info_sg *disg = unitcheck(unitnum);
	if (!disg) {
		return;
	}
	struct device_info *di = &disg->di;
	memset(di, 0, sizeof(*di));
	_tcscpy(di->label, disg->label);
	TCHAR *path = au(disg->path);
	_tcsncpy(di->mediapath, path, sizeof(di->mediapath) / sizeof(TCHAR) - 1);
	xfree(path);
	di->type = disg->type;
	di->media_inserted = mediacheck(disg);
	di->removable = disg->removable;
	di->write_protected = disg->readonly;
	di->bytespersector = disg->type == INQ_ROMD ? 2048 : 512;
	di->trackspercylinder = 1;
	di->sectorspertrack = 1;
	di->cylinders = 0;
	di->bus = 0;
	di->target = unitnum;
	di->lun = 0;
	di->unitnum = unitnum + 1;
	di->backend = _T("SG_IO");

	if (disg->open && (disg->type == INQ_DASD || disg->type == INQ_ROMD)) {
		uae_u8 cmd[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		uae_u8 data[32];
		uae_u8 status = 0;
		int actual = 0;
		if (sg_exec_fd(disg->fd, cmd, sizeof(cmd), data, sizeof(data), SG_DXFER_FROM_DEV,
			disg->sense, sizeof(disg->sense), &status, &actual, 5000) == 0 && status == 0 && actual >= 8) {
			di->bytespersector = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
			di->sectorspertrack = ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]) + 1;
			di->cylinders = 1;
		}
	}
}

static int open_sg_device2(struct dev_info_sg *di, int unitnum)
{
	if (!di || !di->enabled) {
		return 0;
	}
	if (!di->scsibuf) {
		di->scsibuf = xmalloc(uae_u8, DEVICE_SCSI_BUFSIZE);
	}
	di->fd = open_path_rw(di->path, &di->readonly);
	if (di->fd < 0) {
		write_log(_T("SG_IO: failed to open '%s': %s\n"), di->label, strerror(errno));
		return 0;
	}
	uae_u8 status = 0;
	if (!inquiry_fd(di->fd, di->inquirydata, &status)) {
		write_log(_T("SG_IO: inquiry failed for '%s'\n"), di->label);
		close(di->fd);
		di->fd = -1;
		return 0;
	}
	di->open = true;
	update_device_info(unitnum);
	return 1;
}

static void close_sg_device2(struct dev_info_sg *di)
{
	if (!di || !di->open) {
		return;
	}
	di->open = false;
	if (di->fd >= 0) {
		close(di->fd);
		di->fd = -1;
	}
}

static int open_sg_device(int unitnum, const TCHAR *ident, int flags)
{
	struct dev_info_sg *di = NULL;
	if (ident && ident[0]) {
		for (int i = 0; i < total_devices; i++) {
			di = &dev_info[i];
			TCHAR *path = au(di->path);
			bool match = !_tcsicmp(path, ident) || !_tcsicmp(di->label, ident);
			xfree(path);
			if (match) {
				unittable[unitnum] = i + 1;
				if (open_sg_device2(di, unitnum)) {
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
	if (open_sg_device2(di, unitnum)) {
		return 1;
	}
	unittable[unitnum] = 0;
	return 0;
}

static void close_sg_device(int unitnum)
{
	struct dev_info_sg *di = unitisopen(unitnum);
	if (!di) {
		return;
	}
	close_sg_device2(di);
	unittable[unitnum] = 0;
}

static struct device_info *info_sg_device(int unitnum, struct device_info *di, int quick, int session)
{
	struct dev_info_sg *disg = unitcheck(unitnum);
	if (!disg) {
		return NULL;
	}
	if (!quick) {
		update_device_info(unitnum);
	}
	disg->di.open = disg->open;
	memcpy(di, &disg->di, sizeof(struct device_info));
	return di;
}

static int sg_exec_direct_common(struct dev_info_sg *di, int unitnum, struct amigascsi *as)
{
	uae_u8 cmd[16];
	uae_u8 *scsi_datap = as->len ? as->data : NULL;
	uae_u8 *scsi_datap_org = scsi_datap;
	int datalen = as->len;
	int cmdlen = as->cmd_len;
	int parm = 0;

	if (datalen > DEVICE_SCSI_BUFSIZE) {
		datalen = DEVICE_SCSI_BUFSIZE;
	}
	if (cmdlen > (int)sizeof(cmd)) {
		cmdlen = sizeof(cmd);
	}
	memcpy(cmd, as->cmd, cmdlen);

	if (di->isatapi) {
		scsi_atapi_fixup_pre(cmd, &cmdlen, &scsi_datap, &datalen, &parm);
	}

	if (datalen > 0 && scsi_datap) {
		memcpy(di->scsibuf, scsi_datap, datalen);
	}

	const int direction = (as->flags & 1) ? SG_DXFER_FROM_DEV : SG_DXFER_TO_DEV;
	uae_u8 status = 0;
	int actual = 0;
	int senselen = as->sense_len;
	if (senselen > (int)sizeof(di->sense)) {
		senselen = sizeof(di->sense);
	}
	if (as->flags & 4) {
		senselen = std::min(senselen, 4);
	}

	uae_sem_wait(&scgp_sem);
	int ret = sg_exec_fd(di->fd, cmd, cmdlen, datalen > 0 ? di->scsibuf : NULL, datalen,
		direction, di->sense, senselen, &status, &actual, 80 * 60 * 1000);
	uae_sem_post(&scgp_sem);

	if (datalen > 0 && scsi_datap && direction == SG_DXFER_FROM_DEV) {
		memcpy(scsi_datap, di->scsibuf, datalen);
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
		if (scsi_datap != scsi_datap_org) {
			free(scsi_datap);
		}
		return IOERR_BadStatus;
	}

	if (ret < 0) {
		as->actual = 0;
		if (scsi_datap != scsi_datap_org) {
			free(scsi_datap);
		}
		return IOERR_NotSpecified;
	}

	as->len = actual;
	if (di->isatapi) {
		scsi_atapi_fixup_post(cmd, cmdlen, scsi_datap_org, scsi_datap, &as->len, parm);
	}
	as->actual = as->len;
	for (int i = 0; i < as->sense_len; i++) {
		as->sensedata[i] = 0;
	}
	if (scsi_datap != scsi_datap_org) {
		free(scsi_datap);
	}
	return 0;
}

static int execsg_direct(int unitnum, struct amigascsi *as)
{
	struct dev_info_sg *di = unitisopen(unitnum);
	if (!di) {
		return -1;
	}

	if (as->cmd[0] == 0x03 && di->senselen > 0) {
		int len = di->senselen;
		if (len > as->len) {
			len = as->len;
		}
		memcpy(as->data, di->sense, len);
		as->actual = len;
		as->status = 0;
		as->sactual = 0;
		as->cmdactual = as->cmd_len;
		di->senselen = 0;
		return 0;
	}

	return sg_exec_direct_common(di, unitnum, as);
}

static uae_u8 *execsg_out(int unitnum, uae_u8 *data, int len)
{
	struct amigascsi as;
	memset(&as, 0, sizeof(as));
	as.data = data;
	as.len = 0;
	memcpy(as.cmd, data, len);
	as.cmd_len = len;
	as.flags = 0;
	if (execsg_direct(unitnum, &as) != 0) {
		return NULL;
	}
	return data;
}

static uae_u8 *execsg_in(int unitnum, uae_u8 *data, int len, int *outlen)
{
	struct dev_info_sg *di = unitisopen(unitnum);
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
	if (execsg_direct(unitnum, &as) != 0 || as.actual <= 0) {
		return NULL;
	}
	if (outlen) {
		*outlen = std::min(*outlen, (int)as.actual);
	}
	return di->scsibuf;
}

static int sg_isatapi(int unitnum)
{
	struct dev_info_sg *di = unitcheck(unitnum);
	return di && di->isatapi;
}

static int sg_ismedia(int unitnum, int quick)
{
	struct dev_info_sg *di = unitisopen(unitnum);
	if (!di) {
		return -1;
	}
	if (quick) {
		return di->di.media_inserted;
	}
	update_device_info(unitnum);
	return di->di.media_inserted;
}

static int open_sg_bus(int flags)
{
	if (bus_open) {
		write_log(_T("SG_IO open_bus() more than once!\n"));
		return 1;
	}
	total_devices = 0;
	memset(dev_info, 0, sizeof(dev_info));
	memset(unittable, 0, sizeof(unittable));
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		dev_info[i].fd = -1;
	}

	uae_sem_init(&scgp_sem, 0, 1);
	const std::vector<std::string> paths = candidate_paths();
	for (const std::string &path : paths) {
		add_device(path.c_str());
	}
	bus_open = 1;
	write_log(_T("SG_IO driver open, %d devices.\n"), total_devices);
	return total_devices;
}

static void close_sg_bus(void)
{
	if (!bus_open) {
		write_log(_T("SG_IO close_bus() when already closed!\n"));
		return;
	}
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		close_sg_device2(&dev_info[i]);
		xfree(dev_info[i].scsibuf);
		dev_info[i].scsibuf = NULL;
		unittable[i] = 0;
	}
	total_devices = 0;
	bus_open = 0;
	uae_sem_destroy(&scgp_sem);
	write_log(_T("SG_IO driver closed.\n"));
}

struct device_functions devicefunc_scsi_spti = {
	_T("SG_IO"),
	open_sg_bus, close_sg_bus, open_sg_device, close_sg_device, info_sg_device,
	execsg_out, execsg_in, execsg_direct,
	0, 0, 0, 0, 0,
	0, 0, 0, 0,
	sg_isatapi, sg_ismedia, 0
};

#endif /* WITH_SCSI_SPTI */
