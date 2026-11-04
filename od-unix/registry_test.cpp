#include "sysconfig.h"
#include "sysdeps.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "registry.h"

void write_log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

int uaetcslen(const TCHAR *s)
{
    return (int)strlen(s);
}

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    char path[256];
    snprintf(path, sizeof path, "/tmp/winuae_registry_test_%ld/sub/winuae.ini", (long)getpid());
    registry_set_ini_path(path);

    /* Root values. */
    check(regsetstr(NULL, "TestString", "hello world") == 1, "regsetstr");
    check(regsetint(NULL, "TestInt", -42) == 1, "regsetint");
    check(regsetlonglong(NULL, "TestLong", 123456789012345ULL) == 1, "regsetlonglong");

    TCHAR buf[256];
    int size = sizeof buf;
    check(regquerystr(NULL, "TestString", buf, &size) == 1 && !strcmp(buf, "hello world"), "regquerystr");
    int v = 0;
    check(regqueryint(NULL, "TestInt", &v) == 1 && v == -42, "regqueryint");
    unsigned long long lv = 0;
    check(regquerylonglong(NULL, "TestLong", &lv) == 1 && lv == 123456789012345ULL, "regquerylonglong");
    check(regexists(NULL, "TestString") == 1, "regexists");
    check(regexists(NULL, "Missing") == 0, "regexists missing");

    /* Subtrees. */
    UAEREG *tree = regcreatetree(NULL, "DetectedROMs");
    check(tree != NULL, "regcreatetree");
    check(regsetstr(tree, "ROM_1", "kick13.rom") == 1, "tree regsetstr");
    size = sizeof buf;
    check(regquerystr(tree, "ROM_1", buf, &size) == 1 && !strcmp(buf, "kick13.rom"), "tree regquerystr");
    check(regexiststree(NULL, "DetectedROMs") == 1, "regexiststree");

    /* Enumeration. */
    TCHAR name[64];
    int nsize = sizeof name;
    size = sizeof buf;
    check(regenumstr(tree, 0, name, &nsize, buf, &size) == 1 && !strcmp(name, "ROM_1"), "regenumstr");
    regclosetree(tree);

    /* Binary data. */
    const unsigned char data[4] = { 0xde, 0xad, 0xbe, 0xef };
    check(regsetdata(NULL, "TestData", data, sizeof data) == 1, "regsetdata");
    unsigned char out[4] = { 0 };
    size = sizeof out;
    check(regquerydata(NULL, "TestData", out, &size) == 1 && !memcmp(out, data, sizeof data), "regquerydata");
    size = 0;
    check(regquerydatasize(NULL, "TestData", &size) == 1 && size == 4, "regquerydatasize");

    /* Delete. */
    check(regdelete(NULL, "TestInt") == 1, "regdelete");
    check(regexists(NULL, "TestInt") == 0, "deleted value gone");

    /* Values above LLONG_MAX are stored signed, as on Windows. */
    check(regsetlonglong(NULL, "TestBig", 0xfedcba9876543210ULL) == 1, "regsetlonglong big");
    lv = 0;
    check(regquerylonglong(NULL, "TestBig", &lv) == 1 && lv == 0xfedcba9876543210ULL, "regquerylonglong big");

    /* Truncation to the caller's buffer size. */
    char small[4];
    size = sizeof small;
    check(regquerystr(NULL, "TestString", small, &size) == 1 && !strcmp(small, "hel") && size == 3, "regquerystr truncates");

    /* Hex data is accepted in lower case and rejected when malformed. */
    check(regsetstr(NULL, "LowerData", "deadbeef") == 1, "regsetstr lower hex");
    memset(out, 0, sizeof out);
    size = sizeof out;
    check(regquerydata(NULL, "LowerData", out, &size) == 1 && !memcmp(out, data, sizeof data), "regquerydata lower case");
    check(regsetstr(NULL, "BadData", "zz") == 1, "regsetstr bad hex");
    size = sizeof out;
    check(regquerydata(NULL, "BadData", out, &size) == 0, "regquerydata rejects bad hex");

    /* Persistence: flush, drop state, re-open the same file. */
    registry_flush();

    /* On-disk format, the same as winuae.ini on Windows. */
    FILE *f = fopen(path, "r");
    check(f != NULL, "ini file written");
    if (f) {
        char text[4096];
        size_t n = fread(text, 1, sizeof text - 1, f);
        text[n] = 0;
        fclose(f);
        check(strstr(text, "[WinUAE]") != NULL, "root section");
        check(strstr(text, "[DetectedROMs]") != NULL, "tree section");
        check(strstr(text, "TestLong=123456789012345\n") != NULL, "long long format");
        check(strstr(text, "TestBig=-81985529216486896\n") != NULL, "long long signed format");
        check(strstr(text, "TestData=DEADBEEF\n") != NULL, "binary data format");
    }
    registry_set_ini_path(path);
    size = sizeof buf;
    check(regquerystr(NULL, "TestString", buf, &size) == 1 && !strcmp(buf, "hello world"), "persisted string");
    UAEREG *tree2 = regcreatetree(NULL, "DetectedROMs");
    size = sizeof buf;
    check(regquerystr(tree2, "ROM_1", buf, &size) == 1 && !strcmp(buf, "kick13.rom"), "persisted tree value");
    regclosetree(tree2);
    check(regexists(NULL, "TestInt") == 0, "deleted value stays gone");

    /* Tree deletion. */
    regdeletetree(NULL, "DetectedROMs");
    check(regexiststree(NULL, "DetectedROMs") == 0, "regdeletetree");

    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf /tmp/winuae_registry_test_%ld", (long)getpid());
    system(cmd);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("winuae_unix_registry_test: all checks passed\n");
    return 0;
}
