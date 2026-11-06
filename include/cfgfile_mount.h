#ifndef UAE_CFGFILE_MOUNT_H
#define UAE_CFGFILE_MOUNT_H

#include <string>

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
