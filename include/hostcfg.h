#ifndef UAE_HOSTCFG_H
#define UAE_HOSTCFG_H

struct uae_prefs;
struct zfile;

enum hostcfg_style { HOSTCFG_WINDOWS, HOSTCFG_UNIX };

bool hostcfg_is_compat_option(const TCHAR *option);
int hostcfg_parse_option(struct uae_prefs *p, const TCHAR *option,
	const TCHAR *value, hostcfg_style style);
void hostcfg_save_options(struct zfile *f, const struct uae_prefs *p);

#endif
