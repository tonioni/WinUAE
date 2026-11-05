#include "sysconfig.h"
#include "sysdeps.h"

#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <vector>

#include "options.h"
#include "fsdb.h"
#include "rommgr.h"
#include "uae/string.h"
#include "uae.h"

struct detected_rom {
	TCHAR path[MAX_DPATH];
	struct romdata *rd;
	int priority;
};

struct romscan_state {
	std::vector<detected_rom> detected;
};

static bool romscan_dirty = true;
static int recursiveromscan;

static bool same_rom_identity(const struct romdata *a, const struct romdata *b)
{
	return a && b && a->id == b->id && a->group == b->group;
}

static void add_detected_rom(romscan_state *state, const TCHAR *path, struct romdata *rd)
{
	if (!state || !path || !rd) {
		return;
	}

	detected_rom rom;
	uae_tcslcpy(rom.path, path, sizeof rom.path / sizeof(TCHAR));
	if (rom.path[0] != ':') {
		fullpath(rom.path, sizeof rom.path / sizeof(TCHAR));
	}
	rom.rd = rd;
	rom.priority = romscan_priority(rom.path, rd->size);

	for (detected_rom &existing : state->detected) {
		if (same_rom_identity(existing.rd, rd)) {
			if (rom.priority < existing.priority) {
				existing = rom;
			}
			return;
		}
	}
	state->detected.push_back(rom);
}

static bool scan_rom_result(void *user, romscan_event event, const TCHAR *path, struct romdata *rd)
{
	if (event == ROMSCAN_ARCADIA || event == ROMSCAN_ROM) {
		add_detected_rom((romscan_state*)user, path, rd);
	}
	return true;
}

static void scan_rom_directory(const TCHAR *path, int level, romscan_state *state)
{
	struct my_opendir_s *dir = my_opendir(path);
	if (!dir) {
		return;
	}

	TCHAR name[MAX_DPATH];
	while (my_readdir(dir, name)) {
		if (!_tcscmp(name, _T(".")) || !_tcscmp(name, _T(".."))) {
			continue;
		}

			TCHAR full[MAX_DPATH];
			uae_tcslcpy(full, path, sizeof full / sizeof(TCHAR));
			fixtrailing(full);
			if (_tcslen(full) + _tcslen(name) >= sizeof full / sizeof(TCHAR)) {
				continue;
			}
			_tcscat(full, name);

		struct stat st;
		if (stat(full, &st) != 0) {
			continue;
		}
		if (S_ISREG(st.st_mode) && st.st_size < 10000000) {
			romscan_file(full, true, scan_rom_result, state);
		} else if (S_ISDIR(st.st_mode) && name[0] != '.') {
			if (recursiveromscan < 0 || recursiveromscan > level) {
				scan_rom_directory(full, level + 1, state);
			}
		}
	}
	my_closedir(dir);
}

static bool add_scan_path(std::vector<std::string> *paths, const TCHAR *path)
{
	if (!path || !path[0]) {
		return false;
	}

	TCHAR full[MAX_DPATH];
	uae_tcslcpy(full, path, sizeof full / sizeof(TCHAR));
	fullpath(full, sizeof full / sizeof(TCHAR));
	fixtrailing(full);
	if (!full[0]) {
		return false;
	}

	std::string value(full);
	if (std::find(paths->begin(), paths->end(), value) != paths->end()) {
		return false;
	}
	paths->push_back(value);
	return true;
}

static std::vector<std::string> rom_scan_paths(struct uae_prefs *prefs)
{
	std::vector<std::string> paths;

	if (prefs) {
		for (int i = 0; i < MAX_PATHS; i++) {
			add_scan_path(&paths, prefs->path_rom.path[i]);
		}
	}

	TCHAR path[MAX_DPATH];
	fetch_rompath(path, sizeof path / sizeof(TCHAR));
	add_scan_path(&paths, path);

	return paths;
}

bool unix_resolve_rom_path(struct uae_prefs *prefs, TCHAR *path, int size)
{
	if (!path || !path[0] || size <= 0) {
		return false;
	}
	// Leave absolute paths and ":NAME" config-name references untouched;
	// matches the file[0]/':' guard cfgfile uses for its ROM multipath.
	if (path[0] == '/' || path[0] == '\\' || _tcschr(path, ':')) {
		return false;
	}

	for (const std::string &dir : rom_scan_paths(prefs)) {
		TCHAR full[MAX_DPATH];
		uae_tcslcpy(full, dir.c_str(), sizeof full / sizeof(TCHAR));
		fixtrailing(full);
		if (_tcslen(full) + _tcslen(path) >= sizeof full / sizeof(TCHAR)) {
			continue;
		}
		_tcscat(full, path);

		struct stat st;
		if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
			uae_tcslcpy(path, full, size);
			return true;
		}
	}
	return false;
}

static void add_nofile_roms(romscan_state *state)
{
	for (int id = 1;; id++) {
		struct romdata *rd = getromdatabyid(id);
		if (!rd) {
			break;
		}
		if (rd->crc32 != 0xffffffff) {
			continue;
		}

		TCHAR path[MAX_DPATH];
		if (rd->configname) {
			_stprintf(path, _T(":%s"), rd->configname);
		} else {
			_stprintf(path, _T(":ROM_%03d"), rd->id);
		}
		add_detected_rom(state, path, rd);
	}
}

static int scan_rom_paths(struct uae_prefs *prefs)
{
	romscan_state state;

	for (const std::string &path : rom_scan_paths(prefs)) {
		write_log(_T("ROM scan directory '%s'\n"), path.c_str());
		scan_rom_directory(path.c_str(), 0, &state);
	}
	add_nofile_roms(&state);

	for (const detected_rom &rom : state.detected) {
		romlist_add(rom.path, rom.rd);
	}
	return (int)state.detected.size();
}

struct romscan_paths_data {
	struct uae_prefs *prefs;
	int count;
};

static bool scan_rom_pass(void *user)
{
	struct romscan_paths_data *data = (struct romscan_paths_data*)user;
	data->count = scan_rom_paths(data->prefs);
	return true;
}

static void restart_rom_scan(void *user)
{
	struct romscan_paths_data *data = (struct romscan_paths_data*)user;
	romlist_clear();
	load_keyring(data->prefs, NULL);
}

void unix_romscan_mark_dirty(void)
{
	romscan_dirty = true;
}

void unix_romscan_set_recursive(bool recursive)
{
	const int value = recursive ? 2 : 0;
	if (recursiveromscan != value) {
		recursiveromscan = value;
		unix_romscan_mark_dirty();
	}
}

void unix_romscan_refresh(struct uae_prefs *prefs, bool force)
{
	if (!force && !romscan_dirty) {
		return;
	}

	romlist_clear();
	load_keyring(prefs, NULL);
	struct romscan_paths_data data = { prefs, 0 };
	romscan_with_keyring(scan_rom_pass, restart_rom_scan, &data, 1);
	int count = data.count;
	romlist_add(NULL, NULL);
	write_log(_T("ROM scan found %d known ROM%s\n"), count, count == 1 ? _T("") : _T("s"));
	romscan_dirty = false;
}
