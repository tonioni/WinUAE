// Keep the key lookup independent of the core for launcher-only builds.
#include "tchar.h"
#include "hostcfg.h"

bool hostcfg_is_compat_option(const TCHAR *option)
{
#define HOSTCFG(key, type, field, fallback) \
	if (!_tcsicmp(option, _T(key))) return true;
#include "hostcfg.def"
#undef HOSTCFG
	return false;
}
