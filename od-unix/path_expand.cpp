#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <cstdlib>
#include <limits.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "path_expand.h"

std::string unix_expand_path(const std::string &path)
{
    std::string out;
    const char *home = getenv("HOME");

    if (!path.empty() && path[0] == '~' && (path.size() == 1 || path[1] == '/')) {
        out = home ? home : "";
        out += path.substr(1);
    } else {
        out = path;
    }

    if (home) {
        std::string homestr(home);
        size_t slash = homestr.find_last_of('/');
        std::string user = slash == std::string::npos ? homestr : homestr.substr(slash + 1);
        std::string oldhome = "/home/" + user;
        if (!user.empty() && out.compare(0, oldhome.size(), oldhome) == 0 &&
            (out.size() == oldhome.size() || out[oldhome.size()] == '/')) {
            out = homestr + out.substr(oldhome.size());
        }
    }

    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] != '$') {
            continue;
        }
        size_t start = i + 1;
        size_t end = start;
        std::string name;
        if (start < out.size() && out[start] == '{') {
            start++;
            end = out.find('}', start);
            if (end == std::string::npos) {
                continue;
            }
            name = out.substr(start, end - start);
            end++;
        } else {
            while (end < out.size() && (isalnum((unsigned char)out[end]) || out[end] == '_')) {
                end++;
            }
            name = out.substr(start, end - start);
        }
        if (name.empty()) {
            continue;
        }
        const char *value = getenv(name.c_str());
        if (!value) {
            continue;
        }
        const size_t value_len = strlen(value);
        out.replace(i, end - i, value);
        if (value_len > 0) {
            i += value_len - 1;
        }
    }

    return out;
}

static bool unix_path_is_absolute(const std::string &path)
{
    return !path.empty() && path[0] == '/';
}

static bool unix_path_has_unresolved_prefix(const std::string &path)
{
    return path.find('$') != std::string::npos || (!path.empty() && path[0] == '~');
}

static std::string unix_join_path(const std::string &dir, const std::string &name)
{
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (!dir.empty() && dir[dir.size() - 1] == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

static std::string unix_current_directory()
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) {
        return cwd;
    }
    return ".";
}

static std::string unix_lexically_normalize_absolute(const std::string &path)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < path.size()) {
        while (pos < path.size() && path[pos] == '/') {
            pos++;
        }
        size_t end = pos;
        while (end < path.size() && path[end] != '/') {
            end++;
        }
        std::string part = path.substr(pos, end - pos);
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        pos = end;
    }

    std::string out = "/";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) {
            out += "/";
        }
        out += parts[i];
    }
    return out;
}

std::string unix_absolute_path(const std::string &path, const std::string &base)
{
    if (path.empty() || unix_path_has_unresolved_prefix(path)) {
        return path;
    }

    std::string base_path = base.empty() ? unix_current_directory() : base;
    if (!unix_path_is_absolute(base_path)) {
        base_path = unix_join_path(unix_current_directory(), base_path);
    }

    const std::string combined = unix_path_is_absolute(path) ? path : unix_join_path(base_path, path);
    char resolved[PATH_MAX];
    if (realpath(combined.c_str(), resolved)) {
        return resolved;
    }
    return unix_lexically_normalize_absolute(combined);
}
