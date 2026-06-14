#include "sysconfig.h"
#include "sysdeps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "path_expand.h"

static bool require_equal(const char *label, const std::string &actual, const std::string &expected)
{
    if (actual == expected) {
        return true;
    }
    fprintf(stderr, "%s: expected '%s', got '%s'\n", label, expected.c_str(), actual.c_str());
    return false;
}

int main()
{
    bool ok = true;
    setenv("HOME", "/tmp/winuae-home", 1);
    setenv("WINUAE_TEST_PATH", "/tmp/winuae-env", 1);
    setenv("WINUAE_TEST_SUFFIX", "-suffix", 1);
    unsetenv("WINUAE_TEST_MISSING");

    ok = require_equal("home directory", unix_expand_path("~"), "/tmp/winuae-home") && ok;
    ok = require_equal("home subpath", unix_expand_path("~/roms/kick.rom"), "/tmp/winuae-home/roms/kick.rom") && ok;
    ok = require_equal("plain environment", unix_expand_path("$WINUAE_TEST_PATH/kick.rom"), "/tmp/winuae-env/kick.rom") && ok;
    ok = require_equal("braced environment", unix_expand_path("${WINUAE_TEST_PATH}/kick.rom"), "/tmp/winuae-env/kick.rom") && ok;
    ok = require_equal("adjacent environment", unix_expand_path("$WINUAE_TEST_PATH${WINUAE_TEST_SUFFIX}"), "/tmp/winuae-env-suffix") && ok;
    ok = require_equal("embedded environment", unix_expand_path("/roms/$WINUAE_TEST_PATH"), "/roms//tmp/winuae-env") && ok;
    ok = require_equal("missing environment", unix_expand_path("$WINUAE_TEST_MISSING/kick.rom"), "$WINUAE_TEST_MISSING/kick.rom") && ok;
    ok = require_equal("malformed brace", unix_expand_path("${WINUAE_TEST_PATH/kick.rom"), "${WINUAE_TEST_PATH/kick.rom") && ok;
    ok = require_equal("literal dollar", unix_expand_path("disk$"), "disk$") && ok;
    ok = require_equal("legacy linux home", unix_expand_path("/home/winuae-home/roms"), "/tmp/winuae-home/roms") && ok;
    ok = require_equal("named home unsupported", unix_expand_path("~other/roms"), "~other/roms") && ok;
    ok = require_equal("relative absolute path", unix_absolute_path("roms/kick.rom", "/tmp/winuae-base"), "/tmp/winuae-base/roms/kick.rom") && ok;
    ok = require_equal("relative parent normalization", unix_absolute_path("../disk.adf", "/tmp/winuae-base/configs"), "/tmp/winuae-base/disk.adf") && ok;
    ok = require_equal("absolute normalization", unix_absolute_path("/tmp/winuae-base/../disk.adf"), "/tmp/disk.adf") && ok;
    ok = require_equal("unresolved environment absolute path", unix_absolute_path("$WINUAE_TEST_MISSING/kick.rom", "/tmp/winuae-base"), "$WINUAE_TEST_MISSING/kick.rom") && ok;
    ok = require_equal("unresolved named home absolute path", unix_absolute_path("~other/roms", "/tmp/winuae-base"), "~other/roms") && ok;

    return ok ? 0 : 1;
}
