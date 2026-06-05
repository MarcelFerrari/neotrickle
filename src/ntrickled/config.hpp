// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <toml++/toml.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace ntrickled {

/// Per-user weight and directional caps from the [users] section of ntrickled.toml.
struct user_config_entry {
    uint32_t weight = 1; // share multiplier (default 1)
    uint64_t download_cap = 0; // bytes/sec hard ceiling (0 = unlimited)
    uint64_t upload_cap = 0; // bytes/sec hard ceiling (0 = unlimited)
    bool has_download_cap = false;
    bool has_upload_cap = false;
};

class config_reader {
public:
    explicit config_reader(std::string_view path);

    int get_int(std::string_view section, std::string_view key, int default_val) const;
    double get_float(std::string_view section, std::string_view key, double default_val) const;
    std::string get_str(std::string_view section, std::string_view key,
                        std::string_view default_val) const;

    /// Parse the [users] section. Keyed by username.
    std::unordered_map<std::string, user_config_entry> get_user_configs() const;

private:
    toml::table tbl_;
};

} // namespace ntrickled
