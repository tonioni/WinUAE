#include "sysconfig.h"
#include "sysdeps.h"

#ifdef DRIVESOUND

#include "driveclick.h"
#include "uae.h"
#include "zfile.h"

#include <string>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef WINUAE_UNIX_INSTALL_DATA_DIR
#define WINUAE_UNIX_INSTALL_DATA_DIR WINUAE_UNIX_SOURCE_DIR
#endif

#ifndef WINUAE_UNIX_INSTALL_DATADIR_RELATIVE
#define WINUAE_UNIX_INSTALL_DATADIR_RELATIVE "share/winuae"
#endif

int driveclick_pcdrivemask;
int driveclick_pcdrivenum;

static const struct {
	const TCHAR *name;
	int slot;
} builtin_samples[] = {
	{ _T("drive_click.wav"), DS_CLICK },
	{ _T("drive_spin.wav"), DS_SPIN },
	{ _T("drive_spinnd.wav"), DS_SPINND },
	{ _T("drive_startup.wav"), DS_START },
	{ _T("drive_snatch.wav"), DS_SNATCH },
	{ NULL, -1 }
};

static bool load_sample_file(const TCHAR *path, struct drvsample *sample)
{
	struct zfile *zf = zfile_fopen(path, _T("rb"), ZFD_NORMAL);
	if (!zf) {
		return false;
	}
	const uae_s64 size = zfile_size(zf);
	if (size <= 0 || size > 16 * 1024 * 1024) {
		zfile_fclose(zf);
		return false;
	}
	uae_u8 *data = xmalloc(uae_u8, (size_t)size);
	if (!data) {
		zfile_fclose(zf);
		return false;
	}
	const size_t got = zfile_fread(data, 1, (size_t)size, zf);
	zfile_fclose(zf);
	if (got != (size_t)size) {
		xfree(data);
		return false;
	}
	int decoded_len = (int)size;
	sample->p = decodewav(data, &decoded_len);
	sample->len = decoded_len;
	xfree(data);
	return sample->p != NULL && sample->len > 0;
}

static std::string dirname_copy(const std::string &path)
{
	size_t slash = path.find_last_of('/');
	if (slash == std::string::npos) {
		return ".";
	}
	if (slash == 0) {
		return "/";
	}
	return path.substr(0, slash);
}

static std::string join_path(const std::string &dir, const std::string &name)
{
	if (dir.empty() || dir == ".") {
		return name;
	}
	if (dir[dir.size() - 1] == '/') {
		return dir + name;
	}
	return dir + "/" + name;
}

static std::string executable_dir()
{
#ifdef __APPLE__
	char path[MAX_DPATH];
	uint32_t size = sizeof path;
	if (_NSGetExecutablePath(path, &size) == 0) {
		return dirname_copy(path);
	}
#elif defined(__linux__)
	char path[MAX_DPATH];
	ssize_t len = readlink("/proc/self/exe", path, sizeof path - 1);
	if (len > 0) {
		path[len] = 0;
		return dirname_copy(path);
	}
#endif
	return std::string();
}

static bool load_builtin_sample(const TCHAR *name, struct drvsample *sample)
{
	TCHAR path[MAX_DPATH];
	std::vector<std::string> dirs;
	dirs.push_back(WINUAE_UNIX_SOURCE_DIR "/od-win32/resources/");
	dirs.push_back(WINUAE_UNIX_SOURCE_DIR "/resources/");
	dirs.push_back(WINUAE_UNIX_INSTALL_DATA_DIR "/od-win32/resources/");
	if (start_path_data[0]) {
		dirs.push_back(start_path_data);
	}
	if (start_path_data_exe[0]) {
		dirs.push_back(start_path_data_exe);
	}

	const std::string exedir = executable_dir();
	if (!exedir.empty()) {
		dirs.push_back(join_path(exedir, "../Resources/od-win32/resources"));
		dirs.push_back(join_path(exedir, "../" WINUAE_UNIX_INSTALL_DATADIR_RELATIVE "/od-win32/resources"));
		dirs.push_back(join_path(exedir, "od-win32/resources"));
	}

	for (size_t i = 0; i < dirs.size(); i++) {
		if (dirs[i].empty()) {
			continue;
		}
		const char *dir = dirs[i].c_str();
		snprintf(path, sizeof path, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", name);
		if (load_sample_file(path, sample)) {
			return true;
		}
	}
	return false;
}

int driveclick_loadresource(struct drvsample *sp, int)
{
	bool ok = true;
	for (int i = 0; builtin_samples[i].name; i++) {
		struct drvsample *sample = sp + builtin_samples[i].slot;
		if (!load_builtin_sample(builtin_samples[i].name, sample)) {
			write_log(_T("Unix driveclick: missing built-in sample '%s'\n"), builtin_samples[i].name);
			ok = false;
		}
	}
	if (ok) {
		write_log(_T("Unix driveclick: loaded built-in A500 sample set\n"));
	}
	return ok ? 1 : 0;
}

void driveclick_fdrawcmd_seek(int, int)
{
}

void driveclick_fdrawcmd_motor(int, int)
{
}

void driveclick_fdrawcmd_vsync(void)
{
}

void driveclick_fdrawcmd_close(int)
{
}

int driveclick_fdrawcmd_open(int)
{
	return 0;
}

void driveclick_fdrawcmd_detect(void)
{
	driveclick_pcdrivemask = 0;
	driveclick_pcdrivenum = 0;
}

#endif
