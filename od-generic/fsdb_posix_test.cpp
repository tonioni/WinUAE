#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "fsdb.h"
#include "fsdb_posix.h"
#undef NDEBUG
#include <cassert>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

uae_prefs currprefs;
void write_log(const TCHAR *, ...) {}
int dos_errno(void) { return errno == EACCES ? ERROR_WRITE_PROTECTED : ERROR_OBJECT_NOT_AROUND; }
TCHAR *build_nname(const TCHAR *base, const TCHAR *name)
{
    return my_strdup((std::string(base) + "/" + name).c_str());
}
static void put(const std::string &name, const std::string &data)
{
    FILE *f = fopen(name.c_str(), "wb");
    assert(f);
    assert(fwrite(data.data(), 1, data.size(), f) == data.size());
    assert(!fclose(f));
}
static std::string get(const std::string &name)
{
    FILE *f = fopen(name.c_str(), "rb");
    assert(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return std::string(buf, n);
}
int main()
{
    currprefs.filesys_custom_uaefsdb = true;
    assert(fsdb_posix_encode("Foo\\Bar") == "Foo%5cBar");
    assert(fsdb_posix_encode("100%") == "100%25");
    assert(fsdb_posix_encode("AUX.txt") == "AU%58.txt");
    assert(fsdb_posix_encode("LPT1") == "LP%541");
    assert(fsdb_posix_encode("note.uaem") == "note.uae%6d");
    assert(fsdb_posix_encode("name.") == "name%2e");
    assert(fsdb_posix_encode("\xe4") == "\xc3\xa4");
    assert(fsdb_posix_encode("\xe4", true) == "%e4");
    for (int c = 1; c < 256; c++) {
	char s[2] = {(char)c, 0};
	assert(fsdb_posix_decode(fsdb_posix_encode(s).c_str()) == s);
	assert(fsdb_posix_decode(fsdb_posix_encode(s, true).c_str(), true) == s);
    }
    assert(fsdb_posix_decode("bad%0g%00%") == "bad%0g%00%");
    assert(same_aname("\xc4GER", "\xe4ger"));
    char temp[] = "/tmp/winuae-fsdb-test-XXXXXX";
    assert(mkdtemp(temp));
    std::string path = std::string(temp) + "/MiXeD";
    put(path, "payload");
    // This is the line format emitted by Amiberry's fsdb_write_uaem_file.
    put(path + ".uaem", "-s--rwed 2026-01-15 12:34:56.78 percent%25%20caf%e9%0aend\n");
    fsdb_posix_info info;
    assert(fsdb_posix_read(path.c_str(), &info) == 1);
    assert(info.mode == A_FIBF_SCRIPT && info.comment == "percent% caf\xe9\nend");
    assert(info.time.tv_usec == 780000);
    assert(!fsdb_posix_write(path.c_str(), info));
    assert(get(path + ".uaem") == "-s--rwed 2026-01-15 12:34:56.78 percent%25 caf%e9%0aend\n");
    for (int mode = 0; mode < 256; mode++) {
	info.mode = mode;
	info.comment = "keep";
	assert(!fsdb_posix_write(path.c_str(), info));
	fsdb_posix_info read;
	assert(fsdb_posix_read(path.c_str(), &read) == 1 && read.mode == mode &&
	       read.time.tv_sec == info.time.tv_sec);
    }
    a_inode base = {};
    base.nname = temp;
    a_inode *a = custom_fsdb_lookup_aino_aname(&base, "mixed");
    assert(a);
    assert(same_aname(a->aname, "mixed"));
    xfree(a->aname);
    xfree(a->nname);
    xfree(a->comment);
    xfree(a);
    std::string bad = "----rwed 2026-02-31 12:34:56.78 invalid\n";
    put(path + ".uaem", bad);
    assert(fsdb_posix_read(path.c_str(), &info) == -1);
    assert(fsdb_posix_write(path.c_str(), info) == ERROR_BAD_NUMBER);
    assert(get(path + ".uaem") == bad);
    put(path + ".uaem", "----rwed 2026-01-15 12:34:56.78 " + std::string(81, 'x') + "\n");
    assert(fsdb_posix_read(path.c_str(), &info) == -1);
    put(path + ".uaem", std::string(4097, 'x'));
    assert(fsdb_posix_read(path.c_str(), &info) == -1);
    unlink((path + ".uaem").c_str());
    unlink(path.c_str());
    assert(!rmdir(temp));
    puts("POSIX metadata and filename checks passed");
}
