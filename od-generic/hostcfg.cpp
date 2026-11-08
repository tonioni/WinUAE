#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "scsidev.h"
#include "hostcfg.h"

namespace {

enum class value_type { integer, boolean, activity, priority, mouse, scsi,
	vblank, multiplier };

struct host_option {
	const TCHAR *name;
	value_type type;
	int uae_prefs::*number;
	bool uae_prefs::*flag;
	int fallback;

	host_option(const TCHAR *name, value_type type, int uae_prefs::*field,
		int fallback) : name(name), type(type), number(field),
		flag(NULL), fallback(fallback) {}
	host_option(const TCHAR *name, value_type type, bool uae_prefs::*field,
		int fallback) : name(name), type(type), number(NULL),
		flag(field), fallback(fallback) {}
};

const host_option options[] = {
#define HOSTCFG(key, type, field, fallback) \
	host_option(_T(key), value_type::type, &uae_prefs::field, fallback),
#include "hostcfg.def"
#undef HOSTCFG
};

const TCHAR *multipliers[] = { _T("1x"), _T("2x"), _T("3x"),
	_T("4x"), _T("5x"), _T("6x"), _T("7x"), _T("8x"), NULL };
const TCHAR *scsi_modes[] = { _T("SCSIEMU"), _T("SPTI"),
	_T("SPTI+SCSISCAN"), NULL };
const int priority_values[] = { 1, 0, -1, -2 };

bool config_space(TCHAR value)
{
	return value == ' ' || (value >= '\t' && value <= '\r');
}

// Unix also accepts "on" and trims whitespace in activity settings.
// Retain that syntax without changing the Windows configuration parser.
bool unix_boolean(const TCHAR *value)
{
	while (config_space(*value))
		value++;
	size_t length = _tcslen(value);
	while (length && config_space(value[length - 1]))
		length--;
	const TCHAR *true_values[] = { _T("1"), _T("true"), _T("yes"),
		_T("on"), NULL };
	for (int i = 0; true_values[i]; i++) {
		if (_tcslen(true_values[i]) == length &&
			!_tcsnicmp(value, true_values[i], length))
			return true;
	}
	return false;
}

bool unix_integer(const TCHAR *value, int *out)
{
	while (config_space(*value))
		value++;
	if (!*value)
		return false;
	TCHAR *end;
	long number = _tcstol(value, &end, 0);
	while (config_space(*end))
		end++;
	if (*end)
		return false;
	*out = (int)number;
	return true;
}

int priority_index(int value, int fallback)
{
	for (int i = 0; i < 4; i++) {
		if (priority_values[i] == value)
			return i;
	}
	return fallback;
}

}

int hostcfg_parse_option(struct uae_prefs *p, const TCHAR *option,
	const TCHAR *value, hostcfg_style style)
{
	for (const host_option &entry : options) {
		if (_tcsicmp(option, entry.name))
			continue;
		int number = 0;
		bool flag = false;
		switch (entry.type) {
		case value_type::boolean:
		case value_type::mouse:
			if (style == HOSTCFG_UNIX)
				flag = unix_boolean(value);
			else
				cfgfile_yesno(entry.name, value, entry.name, &flag);
			if (entry.type == value_type::mouse) {
				if (flag)
					p->input_mouse_untrap |= MOUSEUNTRAP_MIDDLEBUTTON;
				else
					p->input_mouse_untrap &= ~MOUSEUNTRAP_MIDDLEBUTTON;
			} else {
				p->*(entry.flag) = flag;
			}
			break;
		case value_type::integer:
			cfgfile_intval(entry.name, value, entry.name,
				&(p->*(entry.number)), 1);
			break;
		case value_type::activity:
		case value_type::priority:
			if (style == HOSTCFG_UNIX) {
				if (!unix_integer(value, &number))
					return 0;
			} else {
				cfgfile_intval(entry.name, value, entry.name, &number, 1);
			}
			p->*(entry.number) = entry.type == value_type::priority ?
				priority_index(number, entry.fallback) : number;
			// Windows resets these when reading the active priority.
			if (style == HOSTCFG_WINDOWS &&
				entry.number == &uae_prefs::host_active_capture_priority) {
				p->host_active_nocapture_pause = false;
				p->host_active_nocapture_nosound = false;
			}
			break;
		case value_type::scsi:
			cfgfile_strval(entry.name, value, entry.name,
				&(p->*(entry.number)), scsi_modes, 0);
			if (style == HOSTCFG_WINDOWS &&
				p->config_version < ((2 << 16) | (3 << 8)))
				p->host_uaescsimode = UAESCSI_CDEMU;
			break;
		case value_type::multiplier:
			cfgfile_strval(entry.name, value, entry.name,
				&(p->*(entry.number)), multipliers, 0);
			break;
		case value_type::vblank: {
			TCHAR text[CONFIG_BLEN];
			cfgfile_string(entry.name, value, entry.name, text,
				sizeof text / sizeof(TCHAR));
			auto matches = [&text, style](const TCHAR *name) {
				return style == HOSTCFG_UNIX ? !_tcsicmp(text, name) :
					!_tcscmp(text, name);
			};
			p->host_rtgvblankrate = matches(_T("real")) ? -1 :
				matches(_T("disabled")) ? -2 :
				matches(_T("chipset")) ? 0 : _tstol(text);
			break;
		}
		}
		return 1;
	}
	return 0;
}

void hostcfg_save_options(struct zfile *f, const struct uae_prefs *p)
{
	for (const host_option &entry : options) {
		if (entry.type == value_type::boolean) {
			cfgfile_target_dwrite_bool(f, entry.name, p->*(entry.flag));
			continue;
		}
		int value = p->*(entry.number);
		switch (entry.type) {
		case value_type::mouse:
			cfgfile_target_write_bool(f, entry.name,
				(value & MOUSEUNTRAP_MIDDLEBUTTON) != 0);
			break;
		case value_type::scsi:
			if (value < 0 || value > UAESCSI_LAST)
				value = UAESCSI_SPTI;
			cfgfile_target_dwrite_str(f, entry.name, scsi_modes[value]);
			break;
		case value_type::multiplier:
			if (value < 0 || value >= 8)
				value = 0;
			cfgfile_target_dwrite_str(f, entry.name, multipliers[value]);
			break;
		case value_type::vblank:
			if (value <= 0) {
				cfgfile_target_dwrite_str(f, entry.name, value == -1 ?
					_T("real") : value == -2 ? _T("disabled") : _T("chipset"));
				break;
			}
			cfgfile_target_dwrite(f, entry.name, _T("%d"), value);
			break;
		case value_type::priority:
			if (value < 0 || value >= 4)
				value = 1;
			cfgfile_target_dwrite(f, entry.name, _T("%d"),
				priority_values[value]);
			break;
		default:
			cfgfile_target_dwrite(f, entry.name, _T("%d"), value);
			break;
		}
	}
}
