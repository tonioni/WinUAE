#pragma once

/* Unix host settings store with the Windows registry.cpp API, always in
 * ini-file mode. Values persist in winuae.ini next to the executable
 * (portable mode) or in the per-user configuration directory. */

typedef struct UAEREG {
    TCHAR *inipath;
} UAEREG;

extern const TCHAR *getregmode (void);
extern int reginitializeinit (TCHAR **path);
extern void regstatus (void);

extern int regsetstr (UAEREG*, const TCHAR *name, const TCHAR *str);
extern int regsetint (UAEREG*, const TCHAR *name, int val);
extern int regqueryint (UAEREG*, const TCHAR *name, int *val);
extern int regquerystr (UAEREG*, const TCHAR *name, TCHAR *str, int *size);
extern int regsetlonglong (UAEREG *root, const TCHAR *name, unsigned long long val);
extern int regquerylonglong (UAEREG *root, const TCHAR *name, unsigned long long *val);

extern int regdelete (UAEREG*, const TCHAR *name);
extern void regdeletetree (UAEREG*, const TCHAR *name);

extern int regexists (UAEREG*, const TCHAR *name);
extern int regexiststree (UAEREG *, const TCHAR *name);

extern int regquerydatasize (UAEREG *root, const TCHAR *name, int *size);
extern int regsetdata (UAEREG*, const TCHAR *name, const void *str, int size);
extern int regquerydata (UAEREG *root, const TCHAR *name, void *str, int *size);

extern int regenumstr (UAEREG*, int idx, TCHAR *name, int *nsize, TCHAR *str, int *size);

extern UAEREG *regcreatetree (UAEREG*, const TCHAR *name);
extern void regclosetree (UAEREG *key);

/* Unix additions. */
extern void registry_set_ini_path (const TCHAR *path);
extern void registry_flush (void);
