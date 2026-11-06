#ifndef UAE_CFGFILE_MOUNT_H
#define UAE_CFGFILE_MOUNT_H

#include <string>

// Read one field without modifying the input. Callers retain their on-disk
// quoting convention: modern core mount paths keep backslashes literally,
// while editor fields unescape a backslash followed by another character.
template<typename Char>
static bool cfgfile_mount_field(const Char *&input, Char separator,
	std::basic_string<Char>& field, bool require_separator,
	bool unescape_quoted, bool allow_path_suffix = false)
{
	const Char *next = input;
	std::basic_string<Char> value;
	if (*next == '"') {
		next++;
		while (*next && *next != '"') {
			if (unescape_quoted && *next == '\\') {
				next++;
				if (!*next) {
					return false;
				}
			}
			value += *next++;
		}
		if (*next != '"') {
			return false;
		}
		next++;
	} else {
		while (*next && *next != separator) {
			value += *next++;
		}
	}
	if (*next == separator) {
		next++;
	} else if (!(allow_path_suffix && *next == ':')
		&& (*next || require_separator)) {
		return false;
	}
	input = next;
	field = value;
	return true;
}

template<typename Char>
struct cfgfile_mount_prefix {
	bool readonly = false;
	std::basic_string<Char> device, volume, path;
	size_t consumed = 0;
};

// The common prefix of filesystem2, hardfile2 and uaehf mount values.
// Geometry, controller-specific options and legacy drive suffixes follow it.
template<typename Char>
static bool cfgfile_parse_mount_prefix(const Char *input, bool directory,
	cfgfile_mount_prefix<Char>& prefix, bool unescape_quoted,
	bool allow_path_suffix = false)
{
	const Char *next = input;
	std::basic_string<Char> access;
	cfgfile_mount_prefix<Char> parsed;
	if (!cfgfile_mount_field(next, Char(','), access, true, unescape_quoted)
		|| access.size() != 2 || (access[0] != 'r' && access[0] != 'R')) {
		return false;
	}
	if (access[1] == 'o' || access[1] == 'O') {
		parsed.readonly = true;
	} else if (access[1] != 'w' && access[1] != 'W') {
		return false;
	}
	if (!cfgfile_mount_field(next, Char(':'), parsed.device, true, unescape_quoted)
		|| (directory && !cfgfile_mount_field(next, Char(':'), parsed.volume, true, unescape_quoted))
		|| !cfgfile_mount_field(next, Char(','), parsed.path, true, unescape_quoted, allow_path_suffix)) {
		return false;
	}
	parsed.consumed = next - input;
	prefix = parsed;
	return true;
}

// Shared by the TCHAR config writer and editors with their own string types.
// Paths passed to the formatters are already escaped for the chosen format:
// filesystem2/hardfile2 use minimal escaping, legacy uaehf paths use full
// escaping. Geometry and optional controller fields remain serialized fields.
template<typename Char>
static std::basic_string<Char> cfgfile_mount_number(int value)
{
	const std::string number = std::to_string(value);
	return std::basic_string<Char>(number.begin(), number.end());
}

// Editors that only unescape quoted fields also quote embedded double quotes.
template<typename Char>
static std::basic_string<Char> cfgfile_escape_min_string(const std::basic_string<Char>& value,
	bool quote_quotes = false)
{
	bool quote = false;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] == ',' || (quote_quotes && value[i] == '"')
			|| (value[i] == ' ' && (i == 0 || i + 1 == value.size()))) {
			quote = true;
		}
	}
	std::basic_string<Char> result;
	if (quote) {
		result += Char('"');
	}
	for (Char c : value) {
		if (c == '"') {
			result += Char('\\');
		}
		result += c;
	}
	if (quote) {
		result += Char('"');
	}
	return result;
}

template<typename Char>
static std::basic_string<Char> cfgfile_mount_access(bool readonly)
{
	return { Char('r'), Char(readonly ? 'o' : 'w'), Char(',') };
}

template<typename Char>
static std::basic_string<Char> cfgfile_format_filesystem(bool readonly,
	const std::basic_string<Char>& device, const std::basic_string<Char>& volume,
	const std::basic_string<Char>& path, int bootpri)
{
	return cfgfile_mount_access<Char>(readonly) + device + Char(':') + volume
		+ Char(':') + path + Char(',') + cfgfile_mount_number<Char>(bootpri);
}

template<typename Char>
static std::basic_string<Char> cfgfile_format_hardfile(bool readonly,
	const std::basic_string<Char>& device, const std::basic_string<Char>& path,
	const std::basic_string<Char>& geometry, int bootpri,
	const std::basic_string<Char>& extra)
{
	std::basic_string<Char> value = cfgfile_mount_access<Char>(readonly) + device
		+ Char(':') + path + Char(',') + geometry + Char(',')
		+ cfgfile_mount_number<Char>(bootpri);
	if (!extra.empty()) {
		value += Char(',');
		value += extra;
	}
	return value;
}

template<typename Char>
static std::basic_string<Char> cfgfile_format_uaehf(const std::basic_string<Char>& type,
	int unit, const std::basic_string<Char>& value)
{
	return type + cfgfile_mount_number<Char>(unit) + Char(',') + value;
}

template<typename Char>
static std::basic_string<Char> cfgfile_format_uaehf(const std::basic_string<Char>& type,
	const std::basic_string<Char>& value)
{
	return type + Char(',') + value;
}

#endif
