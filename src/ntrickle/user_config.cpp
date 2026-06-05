// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "user_config.hpp"

#include <cstdlib>

#include <toml++/toml.hpp>

#include <filesystem>

namespace ntrickle {

static std::string config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && xdg[0] != '\0') {
        return std::string(xdg) + "/ntrickle/ntrickle.toml";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.config/ntrickle/ntrickle.toml";
    }
    return {};
}

user_config read_user_config() noexcept {
    user_config cfg{};

    auto path = config_path();
    if (path.empty())
        return cfg;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return cfg;

    try {
        auto tbl = toml::parse_file(path);

        if (auto* daemon = tbl["daemon"].as_table()) {
            if (auto mode = daemon->at_path("mode").value<std::string>()) {
                cfg.mode = *mode;
            }
            if (auto sock = daemon->at_path("socket").value<std::string>()) {
                cfg.socket = *sock;
            }
        }
    } catch (const toml::parse_error&) {
        // unparseable config — return defaults
    }

    return cfg;
}

} // namespace ntrickle
