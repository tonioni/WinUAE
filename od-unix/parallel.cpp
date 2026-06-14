#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "parallel.h"
#include "cia.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/parport.h>
#include <linux/ppdev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

static FILE *printer_output;
static bool printer_is_pipe;

#if defined(__linux__)
static int parallel_fd = -1;
static TCHAR parallel_fd_name[MAX_DPATH];
static uae_u8 parallel_control;
#endif

static bool starts_with(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool openprinter(void)
{
	if (printer_output) {
		return true;
	}
	if (!currprefs.prtname[0] || !_tcsicmp(currprefs.prtname, _T("none"))) {
		return false;
	}

	const char *spec = currprefs.prtname;
	if (!_tcsicmp(currprefs.prtname, _T("default"))) {
		spec = DEFPRTNAME;
	}

	if (spec[0] == '|') {
		const char *command = spec + 1;
		while (*command && isspace((unsigned char)*command)) {
			command++;
		}
		if (!*command) {
			write_log(_T("PARALLEL: empty printer command '%s'\n"), currprefs.prtname);
			return false;
		}
		printer_output = popen(command, "w");
		printer_is_pipe = true;
	} else if (!strcmp(spec, "lpr") || starts_with(spec, "lpr ") || !strcmp(spec, "lp") || starts_with(spec, "lp ")) {
		printer_output = popen(spec, "w");
		printer_is_pipe = true;
	} else {
		TCHAR path[MAX_DPATH];
		target_expand_environment(spec, path, MAX_DPATH);
		printer_output = fopen(path, "ab");
		printer_is_pipe = false;
	}

	if (!printer_output) {
		write_log(_T("PARALLEL: failed to open printer target '%s': %s\n"), currprefs.prtname, strerror(errno));
		return false;
	}
	write_log(_T("PARALLEL: printer output opened: %s\n"), currprefs.prtname);
	return true;
}

#if defined(__linux__)
static bool direct_parallel_spec(const TCHAR *name)
{
	return name && (starts_with(name, "/dev/parport") || starts_with(name, "parport:"));
}

static const TCHAR *direct_parallel_path(const TCHAR *name)
{
	if (name && starts_with(name, "parport:")) {
		return name + 8;
	}
	return name;
}

static void close_parallel_direct(void)
{
	if (parallel_fd >= 0) {
		ioctl(parallel_fd, PPRELEASE);
		close(parallel_fd);
		parallel_fd = -1;
	}
	parallel_fd_name[0] = 0;
	parallel_control = 0;
}

static bool open_parallel_direct(void)
{
	if (!direct_parallel_spec(currprefs.prtname)) {
		close_parallel_direct();
		return false;
	}
	const TCHAR *path = direct_parallel_path(currprefs.prtname);
	if (parallel_fd >= 0 && !_tcscmp(parallel_fd_name, path)) {
		return true;
	}

	close_parallel_direct();
	parallel_fd = open(path, O_RDWR | O_CLOEXEC);
	if (parallel_fd < 0) {
		write_log(_T("PARALLEL: failed to open direct port '%s': %s\n"), path, strerror(errno));
		return false;
	}
	if (ioctl(parallel_fd, PPCLAIM) < 0) {
		write_log(_T("PARALLEL: failed to claim direct port '%s': %s\n"), path, strerror(errno));
		close_parallel_direct();
		return false;
	}

	int direction = 0;
	ioctl(parallel_fd, PPDATADIR, &direction);
	parallel_control = PARPORT_CONTROL_INIT | PARPORT_CONTROL_SELECT;
	ioctl(parallel_fd, PPWCONTROL, &parallel_control);
	_tcsncpy(parallel_fd_name, path, sizeof parallel_fd_name / sizeof(TCHAR) - 1);
	parallel_fd_name[sizeof parallel_fd_name / sizeof(TCHAR) - 1] = 0;
	write_log(_T("PARALLEL: direct port opened: %s\n"), path);
	return true;
}
#else
static bool direct_parallel_spec(const TCHAR *)
{
	return false;
}

static void close_parallel_direct(void)
{
}

static bool open_parallel_direct(void)
{
	return false;
}
#endif

int isprinter(void)
{
	if (!currprefs.prtname[0] || !_tcsicmp(currprefs.prtname, _T("none"))) {
		return 0;
	}
	if (direct_parallel_spec(currprefs.prtname)) {
		return open_parallel_direct() ? -1 : 0;
	}
	return 1;
}

void doprinter(uae_u8 val)
{
	if (!openprinter()) {
		return;
	}
	if (fputc(val, printer_output) == EOF) {
		write_log(_T("PARALLEL: printer write failed: %s\n"), strerror(errno));
		closeprinter();
	}
}

void flushprinter(void)
{
	if (printer_output) {
		fflush(printer_output);
	}
}

void closeprinter(void)
{
	if (!printer_output) {
		return;
	}
	flushprinter();
	if (printer_is_pipe) {
		pclose(printer_output);
	} else {
		fclose(printer_output);
	}
	printer_output = NULL;
	printer_is_pipe = false;
}

int isprinteropen(void)
{
	return printer_output != NULL;
}

void initparallel(void)
{
	closeprinter();
	close_parallel_direct();
}

int parallel_direct_write_data(uae_u8 v, uae_u8 dir)
{
#if defined(__linux__)
	if (!open_parallel_direct() || dir != 0xff) {
		return 0;
	}
	int direction = 0;
	ioctl(parallel_fd, PPDATADIR, &direction);
	if (ioctl(parallel_fd, PPWDATA, &v) < 0) {
		write_log(_T("PARALLEL: direct data write failed: %s\n"), strerror(errno));
		return 0;
	}
	uae_u8 control = parallel_control & ~PARPORT_CONTROL_STROBE;
	ioctl(parallel_fd, PPWCONTROL, &control);
	control |= PARPORT_CONTROL_STROBE;
	ioctl(parallel_fd, PPWCONTROL, &control);
	control &= ~PARPORT_CONTROL_STROBE;
	ioctl(parallel_fd, PPWCONTROL, &control);
	parallel_control = control;
	return 1;
#else
	return 0;
#endif
}

int parallel_direct_read_data(uae_u8 *v)
{
#if defined(__linux__)
	if (!open_parallel_direct() || !v) {
		return 0;
	}
	int direction = 1;
	ioctl(parallel_fd, PPDATADIR, &direction);
	if (ioctl(parallel_fd, PPRDATA, v) < 0) {
		write_log(_T("PARALLEL: direct data read failed: %s\n"), strerror(errno));
		return 0;
	}
	return 1;
#else
	return 0;
#endif
}

int parallel_direct_write_status(uae_u8 v, uae_u8 dir)
{
#if defined(__linux__)
	if (!open_parallel_direct()) {
		return 0;
	}
	if (dir & 3) {
		write_log(_T("PARALLEL: BUSY and POUT can't be driven by direct output\n"));
	}
	if (dir & 4) {
		if (v & 4) {
			parallel_control &= ~PARPORT_CONTROL_SELECT;
		} else {
			parallel_control |= PARPORT_CONTROL_SELECT;
		}
		if (ioctl(parallel_fd, PPWCONTROL, &parallel_control) < 0) {
			write_log(_T("PARALLEL: direct control write failed: %s\n"), strerror(errno));
			return 0;
		}
	}
	return (dir & 3) ? 0 : 1;
#else
	return 0;
#endif
}

int parallel_direct_read_status(uae_u8 *v)
{
#if defined(__linux__)
	if (!open_parallel_direct() || !v) {
		return 0;
	}
	uae_u8 status = 0;
	if (ioctl(parallel_fd, PPRSTATUS, &status) < 0) {
		write_log(_T("PARALLEL: direct status read failed: %s\n"), strerror(errno));
		return 0;
	}
	uae_u8 out = 0;
	if (status & PARPORT_STATUS_SELECT) {
		out |= 4;
	}
	if (status & PARPORT_STATUS_PAPEROUT) {
		out |= 2;
	}
	if (!(status & PARPORT_STATUS_BUSY)) {
		out |= 1;
	}
	*v &= ~7;
	*v |= out & 7;
	if (status & PARPORT_STATUS_ACK) {
		cia_parallelack();
	}
	return 1;
#else
	return 0;
#endif
}
