// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "user_config.hpp"

#include <cstdlib>

#include <toml++/toml.hpp>

#include "user_paths.hpp"

#include <filesystem>

namespace ntrickle {

static std::string config_path() {
    return user_config_path("ntrickle.toml");
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
