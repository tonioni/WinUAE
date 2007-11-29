

typedef struct UAEREG {
    HKEY fkey;
    char *inipath;
} UAEREG;

extern int reginitializeinit (const char *path);
extern void regstatus (void);

extern int regsetstr (UAEREG*, const char *name, const char *str);
extern int regsetint (UAEREG*, const char *name, int val);
extern int regqueryint (UAEREG*, const char *name, int *val);
extern int regquerystr (UAEREG*, const char *name, char *str, int *size);

extern int regdelete (UAEREG*, const char *name);
extern void regdeletetree (UAEREG*, const char *name);

extern int regexists (UAEREG*, const char *name);
extern int regexiststree (UAEREG *, const char *name);

extern int regquerydatasize (UAEREG *root, const char *name, int *size);
extern int regsetdata (UAEREG*, const char *name, const void *str, int size);
extern int regquerydata (UAEREG *root, const char *name, void *str, int *size);

extern int regenumstr (UAEREG*, int idx, char *name, int *nsize, char *str, int *size);

extern UAEREG *regcreatetree (UAEREG*, const char *name);
extern void regclosetree (UAEREG *key);

