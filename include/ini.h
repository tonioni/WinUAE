struct ini_line
{
	int section_order;
	TCHAR *section;
	TCHAR *key;
	TCHAR *value;
};

struct ini_data
{
	struct ini_line **inidata;
	int inilines;
};

void ini_free(struct ini_data *ini);
struct ini_data *ini_new(void);
struct ini_data *ini_load(const TCHAR *path);
bool ini_save(struct ini_data *ini, const TCHAR *path);

void ini_addnewstring(struct ini_data *ini, const TCHAR *section, const TCHAR *key, const TCHAR *val);
void ini_addnewdata(struct ini_data *ini, const TCHAR *section, const TCHAR *key, const uae_u8 *data, int len);
void ini_addnewcomment(struct ini_data *ini, const TCHAR *section, const TCHAR *val);

bool ini_getstring(struct ini_data *ini, const TCHAR *section, const TCHAR *key, TCHAR **out);
bool ini_getval(struct ini_data *ini, const TCHAR *section, const TCHAR *key, int *v);
bool ini_getsectionstring(struct ini_data *ini, const TCHAR *section, int idx, TCHAR **keyout, TCHAR **valout);
bool ini_getdata(struct ini_data *ini, const TCHAR *section, const TCHAR *key, uae_u8 **out, int *size);
bool ini_addstring(struct ini_data *ini, const TCHAR *section, const TCHAR *key, const TCHAR *val);
bool ini_delete(struct ini_data *ini, const TCHAR *section, const TCHAR *key);
