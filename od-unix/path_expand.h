#ifndef WINUAE_OD_UNIX_PATH_EXPAND_H
#define WINUAE_OD_UNIX_PATH_EXPAND_H

#include <string>

std::string unix_expand_path(const std::string &path);
std::string unix_absolute_path(const std::string &path, const std::string &base = std::string());

#endif /* WINUAE_OD_UNIX_PATH_EXPAND_H */
