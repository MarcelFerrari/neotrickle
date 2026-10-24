// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>

#include <string>

namespace ntrickle {

inline bool is_absolute_path(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

inline std::string home_directory() {
    const char* home = std::getenv("HOME");
    return home != nullptr && home[0] != '\0' ? home : std::string{};
}

inline std::string xdg_config_home() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && is_absolute_path(xdg))
        return xdg;
    const std::string home = home_directory();
    return home.empty() ? std::string{} : home + "/.config";
}

inline std::string xdg_runtime_dir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && is_absolute_path(xdg))
        return xdg;
    const std::string home = home_directory();
    return home.empty() ? std::string{} : home + "/.run";
}

inline std::string user_config_dir() {
    const std::string base = xdg_config_home();
    return base.empty() ? std::string{} : base + "/ntrickle";
}

inline std::string user_runtime_dir() {
    const std::string base = xdg_runtime_dir();
    return base.empty() ? std::string{} : base + "/ntrickle";
}

inline std::string user_config_path(const char* filename) {
    const std::string dir = user_config_dir();
    return dir.empty() ? std::string{} : dir + "/" + filename;
}

inline std::string user_socket_path() {
    const std::string dir = user_runtime_dir();
    return dir.empty() ? std::string{} : dir + "/ntrickled.sock";
}

} // namespace ntrickle
