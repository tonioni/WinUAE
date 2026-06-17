#include "sysconfig.h"
#include "sysdeps.h"

#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <vector>

#include "options.h"
#include "arcadia.h"
#include "fsdb.h"
#include "rommgr.h"
#include "uae/string.h"
#include "uae.h"
#include "zfile.h"

struct detected_rom {
	TCHAR path[MAX_DPATH];
	struct romdata *rd;
	int priority;
};

struct romscan_state {
	std::vector<detected_rom> detected;
	bool got;
};

static bool romscan_dirty = true;
static bool romscan_recursive;

static bool is_rom_extension(const TCHAR *path, bool deepscan)
{
	const TCHAR *ext;

	if (!path) {
		return false;
	}
	ext = _tcsrchr(path, '.');
	if (!ext) {
		return false;
	}
	ext++;

	if (!_tcsicmp(ext, _T("rom")) || !_tcsicmp(ext, _T("bin")) || !_tcsicmp(ext, _T("adf")) || !_tcsicmp(ext, _T("key"))
		|| !_tcsicmp(ext, _T("a500")) || !_tcsicmp(ext, _T("a600"))
		|| !_tcsicmp(ext, _T("a1200")) || !_tcsicmp(ext, _T("a3000")) || !_tcsicmp(ext, _T("a4000")) || !_tcsicmp(ext, _T("cd32"))) {
		return true;
	}
	if (_tcslen(ext) >= 2 && toupper(ext[0]) == 'U' && isdigit(ext[1])) {
		return true;
	}
	if (!deepscan) {
		return false;
	}
	for (int i = 0; uae_archive_extensions[i]; i++) {
		if (!_tcsicmp(ext, uae_archive_extensions[i])) {
			return true;
		}
	}
	return false;
}

static int rom_priority(const TCHAR *path, int size)
{
	const TCHAR *ext = _tcsrchr(path, '.');
	if (!ext) {
		return 80;
	}

	struct stat st;
	int pri = 10;
	if (stat(path, &st) == 0) {
		if (st.st_size == size) {
			pri--;
		}
	} else {
		pri = 100;
	}
	return pri;
}

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
	rom.priority = rom_priority(rom.path, rd->size);

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

static int scan_zfile_rom(struct zfile *file, void *userdata)
{
	romscan_state *state = static_cast<romscan_state*>(userdata);
	const TCHAR *path = zfile_getname(file);
	const TCHAR *romkey = _T("rom.key");

	if (!is_rom_extension(path, true)) {
		return 0;
	}

	struct romdata *rd = scan_single_rom_file(file);
	if (rd) {
		add_detected_rom(state, path, rd);
		if (rd->type & ROMTYPE_KEY) {
			addkeyfile(path);
		}
		state->got = true;
	} else if (_tcslen(path) > _tcslen(romkey) && !_tcsicmp(path + _tcslen(path) - _tcslen(romkey), romkey)) {
		addkeyfile(path);
	}
	return 0;
}

static bool scan_rom_file(const TCHAR *path, romscan_state *state)
{
	if (!is_rom_extension(path, true)) {
		return false;
	}

	const bool had_rom = state->got;
	for (int cnt = 0;; cnt++) {
		TCHAR tmp[MAX_DPATH];
		uae_tcslcpy(tmp, path, sizeof tmp / sizeof(TCHAR));
		struct romdata *rd = scan_arcadia_rom(tmp, cnt);
		if (!rd) {
			break;
		}
		add_detected_rom(state, tmp, rd);
		state->got = true;
	}
	zfile_zopen(path, scan_zfile_rom, state);
	return state->got != had_rom;
}

static void scan_rom_directory(const TCHAR *path, bool recursive, int level, romscan_state *state)
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
			scan_rom_file(full, state);
		} else if (recursive && S_ISDIR(st.st_mode) && level < 2 && name[0] != '.') {
			scan_rom_directory(full, recursive, level + 1, state);
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
	state.got = false;

	for (const std::string &path : rom_scan_paths(prefs)) {
		write_log(_T("ROM scan directory '%s'\n"), path.c_str());
		scan_rom_directory(path.c_str(), romscan_recursive, 0, &state);
	}
	add_nofile_roms(&state);

	for (const detected_rom &rom : state.detected) {
		romlist_add(rom.path, rom.rd);
	}
	return (int)state.detected.size();
}

void unix_romscan_mark_dirty(void)
{
	romscan_dirty = true;
}

void unix_romscan_set_recursive(bool recursive)
{
	if (romscan_recursive != recursive) {
		romscan_recursive = recursive;
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
	const int keys = get_keyring();
	int count = scan_rom_paths(prefs);
	if (get_keyring() > keys) {
		romlist_clear();
		load_keyring(prefs, NULL);
		count = scan_rom_paths(prefs);
	}
	romlist_add(NULL, NULL);
	write_log(_T("ROM scan found %d known ROM%s\n"), count, count == 1 ? _T("") : _T("s"));
	romscan_dirty = false;
}
