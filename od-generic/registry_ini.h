#ifndef OD_GENERIC_REGISTRY_INI_H
#define OD_GENERIC_REGISTRY_INI_H

/* The ini-file side of the registry.h API (winuae.ini).
 *
 * The host owns the ini_data, loads and saves it, and implements the
 * reg* functions from registry.h on top of these. root is the tree's
 * section name (UAEREG inipath), or NULL for the "WinUAE" root section.
 * Subtrees are slash-joined section names. */

struct ini_data;

int regini_setstr(struct ini_data *ini, const TCHAR *root, const TCHAR *name, const TCHAR *str);
int regini_setint(struct ini_data *ini, const TCHAR *root, const TCHAR *name, int val);
int regini_queryint(struct ini_data *ini, const TCHAR *root, const TCHAR *name, int *val);
int regini_setlonglong(struct ini_data *ini, const TCHAR *root, const TCHAR *name, unsigned long long val);
int regini_querylonglong(struct ini_data *ini, const TCHAR *root, const TCHAR *name, unsigned long long *val);
int regini_querystr(struct ini_data *ini, const TCHAR *root, const TCHAR *name, TCHAR *str, int *size);
int regini_enumstr(struct ini_data *ini, const TCHAR *root, int idx, TCHAR *name, int *nsize, TCHAR *str, int *size);
int regini_querydatasize(struct ini_data *ini, const TCHAR *root, const TCHAR *name, int *size);
int regini_setdata(struct ini_data *ini, const TCHAR *root, const TCHAR *name, const void *str, int size);
int regini_querydata(struct ini_data *ini, const TCHAR *root, const TCHAR *name, void *str, int *size);
int regini_delete(struct ini_data *ini, const TCHAR *root, const TCHAR *name);
int regini_exists(struct ini_data *ini, const TCHAR *root, const TCHAR *name);
void regini_deletetree(struct ini_data *ini, const TCHAR *root, const TCHAR *name);
int regini_existstree(struct ini_data *ini, const TCHAR *root, const TCHAR *name);
/* Section name for subtree name of root; free with xfree(). */
TCHAR *regini_treepath(const TCHAR *root, const TCHAR *name);

#endif /* OD_GENERIC_REGISTRY_INI_H */
