// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>

#include "bandwidth.hpp"

#include <optional>
#include <string_view>
#include <unordered_map>

namespace ntrickled {

config_reader::config_reader(std::string_view path) {
    try {
        tbl_ = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        // Distinguish "file not found" (acceptable — config is optional)
        // from a genuine parse error (fatal — the user expects the config
        // to take effect and silently ignoring it is dangerous).
        struct stat st {};
        std::string path_str(path);
        if (::stat(path_str.c_str(), &st) == -1 && errno == ENOENT) {
            std::fprintf(stderr,
                         "ntrickled: config file %s not found, "
                         "using defaults\n",
                         path_str.c_str());
            tbl_ = toml::table{};
            return;
        }
        std::fprintf(stderr, "ntrickled: config error in %s: %s\n", path_str.c_str(), err.what());
        std::exit(1);
    }
}

int config_reader::get_int(std::string_view section, std::string_view key, int default_val) const {
    const auto* root = tbl_.as_table();
    if (root == nullptr)
        return default_val;

    auto lookup = [&](std::string_view sec) -> std::optional<int64_t> {
        auto it = root->find(sec);
        if (it == root->end())
            return std::nullopt;
        const auto* sec_tbl = it->second.as_table();
        if (sec_tbl == nullptr)
            return std::nullopt;
        return sec_tbl->at_path(key).value<int64_t>();
    };

    if (auto val = lookup(section))
        return static_cast<int>(*val);
    if (auto val = lookup("General"))
        return static_cast<int>(*val);
    return default_val;
}

double config_reader::get_float(std::string_view section, std::string_view key,
                                double default_val) const {
    const auto* root = tbl_.as_table();
    if (root == nullptr)
        return default_val;

    auto lookup = [&](std::string_view sec) -> std::optional<double> {
        auto it = root->find(sec);
        if (it == root->end())
            return std::nullopt;
        const auto* sec_tbl = it->second.as_table();
        if (sec_tbl == nullptr)
            return std::nullopt;
        auto val = sec_tbl->at_path(key).value<double>();
        if (val)
            return *val;
        auto ival = sec_tbl->at_path(key).value<int64_t>();
        if (ival)
            return static_cast<double>(*ival);
        return std::nullopt;
    };

    if (auto val = lookup(section))
        return *val;
    if (auto val = lookup("General"))
        return *val;
    return default_val;
}

std::string config_reader::get_str(std::string_view section, std::string_view key,
                                   std::string_view default_val) const {
    const auto* root = tbl_.as_table();
    if (root == nullptr)
        return std::string(default_val);

    auto lookup = [&](std::string_view sec) -> std::optional<std::string> {
        auto it = root->find(sec);
        if (it == root->end())
            return std::nullopt;
        const auto* sec_tbl = it->second.as_table();
        if (sec_tbl == nullptr)
            return std::nullopt;
        return sec_tbl->at_path(key).value<std::string>();
    };

    if (auto val = lookup(section))
        return *val;
    if (auto val = lookup("General"))
        return *val;
    return std::string(default_val);
}

std::unordered_map<std::string, user_config_entry> config_reader::get_user_configs() const {
    std::unordered_map<std::string, user_config_entry> result;

    const auto* root = tbl_.as_table();
    if (root == nullptr)
        return result;

    auto users_it = root->find("users");
    if (users_it == root->end())
        return result;

    const auto* users_tbl = users_it->second.as_table();
    if (users_tbl == nullptr)
        return result;

    for (auto& [key, val] : *users_tbl) {
        auto* sub = val.as_table();
        if (sub == nullptr)
            continue;

        user_config_entry entry;
        entry.weight = 1;

        if (auto w = sub->at_path("weight").value<int64_t>()) {
            if (*w >= 1)
                entry.weight = static_cast<uint32_t>(*w);
        }

        if (auto cap_str = sub->at_path("download").value<std::string>()) {
            auto cap_bytes = ntrickle::parse_bandwidth(*cap_str);
            if (cap_bytes && *cap_bytes > 0) {
                entry.download_cap = *cap_bytes;
                entry.has_download_cap = true;
            }
        }

        if (auto cap_str = sub->at_path("upload").value<std::string>()) {
            auto cap_bytes = ntrickle::parse_bandwidth(*cap_str);
            if (cap_bytes && *cap_bytes > 0) {
                entry.upload_cap = *cap_bytes;
                entry.has_upload_cap = true;
            }
        }

        result[std::string(key.str())] = entry;
    }

    return result;
}

} // namespace ntrickled
