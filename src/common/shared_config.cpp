// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include <toml++/toml.hpp>

#include "bandwidth.hpp"

namespace ntrickle {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Parse "HH:MM" into minutes-since-midnight. Returns false on failure.
bool parse_time_of_day(std::string_view s, uint16_t& minutes) {
    if (s.size() < 4 || s.size() > 5)
        return false;
    auto colon = s.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 2)
        return false;

    int h = 0, m = 0;
    auto h_str = s.substr(0, colon);
    auto m_str = s.substr(colon + 1);
    for (char c : h_str)
        if (c < '0' || c > '9')
            return false;
    for (char c : m_str)
        if (c < '0' || c > '9')
            return false;
    h = std::stoi(std::string(h_str));
    m = std::stoi(std::string(m_str));
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return false;

    minutes = static_cast<uint16_t>(h * 60 + m);
    return true;
}

/// Parse abbreviated day names ("Mon", "Tue", …) into a bitmask bit.
/// Returns 0 for unrecognised names.
uint8_t day_bit(std::string_view day) {
    if (day == "Mon")
        return 0x01;
    if (day == "Tue")
        return 0x02;
    if (day == "Wed")
        return 0x04;
    if (day == "Thu")
        return 0x08;
    if (day == "Fri")
        return 0x10;
    if (day == "Sat")
        return 0x20;
    if (day == "Sun")
        return 0x40;
    return 0;
}

/// Parse an optional bandwidth value from a TOML node.
std::optional<uint64_t> parse_bandwidth_opt(toml::node_view<toml::node> node) {
    if (!node)
        return std::nullopt;
    if (auto str = node.value<std::string>()) {
        return parse_bandwidth(*str);
    }
    return std::nullopt;
}

/// Parse an optional uint32 from a TOML node.
std::optional<uint32_t> parse_uint32_opt(toml::node_view<toml::node> node) {
    if (!node)
        return std::nullopt;
    if (auto val = node.value<int64_t>()) {
        if (*val < 0)
            return std::nullopt;
        return static_cast<uint32_t>(*val);
    }
    return std::nullopt;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Process config
// ---------------------------------------------------------------------------

std::vector<process_entry> parse_process_config(const std::string& path) {
    std::vector<process_entry> entries;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error&) {
        return entries; // empty on parse failure
    }

    auto process_tbl = tbl["process"];
    if (!process_tbl)
        return entries;

    auto* pt = process_tbl.as_table();
    if (pt == nullptr)
        return entries;

    for (auto& [key, val] : *pt) {
        auto* sub = val.as_table();
        if (sub == nullptr)
            continue;

        std::string pattern_str(key.str());

        process_entry entry;
        entry.pattern_str = pattern_str;
        try {
            entry.pattern = std::regex(pattern_str);
        } catch (const std::regex_error& e) {
            std::fprintf(stderr, "ntrickle: invalid regex '%s' in process config: %s\n",
                         pattern_str.c_str(), e.what());
            continue;
        }

        entry.weight = parse_uint32_opt(sub->at_path("weight"));
        entry.download = parse_bandwidth_opt(sub->at_path("download"));
        entry.upload = parse_bandwidth_opt(sub->at_path("upload"));

        if (auto ts = sub->at_path("time-smoothing").value<double>()) {
            if (*ts >= 0.0)
                entry.time_smoothing = *ts;
        }
        if (auto ls = sub->at_path("length-smoothing").value<int64_t>()) {
            if (*ls > 0)
                entry.length_smoothing = static_cast<uint32_t>(*ls);
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

const process_entry* match_process(const std::vector<process_entry>& entries,
                                   const std::string& argv0) {
    for (auto& entry : entries) {
        try {
            if (std::regex_match(argv0, entry.pattern)) {
                return &entry;
            }
        } catch (const std::regex_error&) {
            continue;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Schedule config
// ---------------------------------------------------------------------------

std::vector<schedule_entry> parse_schedules_config(const std::string& path) {
    std::vector<schedule_entry> entries;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error&) {
        return entries;
    }

    auto sched_tbl = tbl["schedules"];
    if (!sched_tbl)
        return entries;

    auto* st = sched_tbl.as_table();
    if (st == nullptr)
        return entries;

    for (auto& [key, val] : *st) {
        auto* sub = val.as_table();
        if (sub == nullptr)
            continue;

        schedule_entry entry;
        entry.name = std::string(key.str());

        // Time range
        auto start_str = sub->at_path("start").value<std::string>();
        auto end_str = sub->at_path("end").value<std::string>();
        if (!start_str || !end_str)
            continue; // both required
        uint16_t start_m, end_m;
        if (!parse_time_of_day(*start_str, start_m))
            continue;
        if (!parse_time_of_day(*end_str, end_m))
            continue;
        entry.start_minutes = start_m;
        entry.end_minutes = end_m;

        // Days (optional, default all days)
        entry.days_bitmask = 0;
        if (auto days_arr = sub->at_path("days")) {
            if (auto* arr = days_arr.as_array()) {
                for (auto& elem : *arr) {
                    if (auto day_str = elem.value<std::string>()) {
                        entry.days_bitmask |= day_bit(*day_str);
                    }
                }
            }
        }
        if (entry.days_bitmask == 0)
            entry.days_bitmask = 0x7F; // default all

        // Bandwidth limits (optional)
        entry.download = parse_bandwidth_opt(sub->at_path("download"));
        entry.upload = parse_bandwidth_opt(sub->at_path("upload"));

        entries.push_back(std::move(entry));
    }

    return entries;
}

std::optional<schedule_entry> evaluate_schedules(const std::vector<schedule_entry>& entries,
                                                 bool suppress_warnings, time_t at_time) {
    if (entries.empty())
        return std::nullopt;

    if (at_time == 0)
        at_time = std::time(nullptr);
    struct tm tm_buf;
    ::localtime_r(&at_time, &tm_buf);

    // tm_wday: 0=Sun, 1=Mon, …, 6=Sat
    // Our bitmask: 0x01=Mon, 0x02=Tue, …, 0x40=Sun
    static constexpr uint8_t tm_to_bit[] = {
        0x40, // Sunday
        0x01, // Monday
        0x02, // Tuesday
        0x04, // Wednesday
        0x08, // Thursday
        0x10, // Friday
        0x20, // Saturday
    };
    uint8_t today_bit = tm_to_bit[tm_buf.tm_wday];
    uint16_t now_minutes = static_cast<uint16_t>(tm_buf.tm_hour * 60 + tm_buf.tm_min);

    const schedule_entry* first_match = nullptr;
    bool collision_warned = false;

    for (auto& entry : entries) {
        // Day check
        if (!(entry.days_bitmask & today_bit))
            continue;

        // Time window check (handle overnight ranges)
        bool in_window;
        if (entry.start_minutes <= entry.end_minutes) {
            in_window = (now_minutes >= entry.start_minutes && now_minutes < entry.end_minutes);
        } else {
            // Overnight range: e.g. 22:00–06:00
            in_window = (now_minutes >= entry.start_minutes || now_minutes < entry.end_minutes);
        }

        if (in_window) {
            if (first_match == nullptr) {
                first_match = &entry;
            } else if (!collision_warned && !suppress_warnings) {
                std::fprintf(stderr,
                             "ntrickle: schedule collision: '%s' and '%s' both match "
                             "(using first match '%s')\n",
                             first_match->name.c_str(), entry.name.c_str(),
                             first_match->name.c_str());
                collision_warned = true;
            }
        }
    }

    if (first_match != nullptr)
        return *first_match;
    return std::nullopt;
}

schedule_limits apply_schedule_limits(uint64_t base_send, uint64_t base_recv,
                                      const schedule_entry* active) {
    schedule_limits limits{base_send, base_recv};
    if (active == nullptr)
        return limits;

    if (active->upload)
        limits.send = *active->upload;
    if (active->download)
        limits.recv = *active->download;
    return limits;
}

// ---------------------------------------------------------------------------
// Config path resolution
// ---------------------------------------------------------------------------

std::string resolve_config_path(const std::string& cli_path, const char* env_var,
                                const char* user_path, const char* system_default) {
    if (!cli_path.empty())
        return cli_path;

    const char* env = std::getenv(env_var);
    if (env != nullptr && env[0] != '\0')
        return env;

    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        std::string user = std::string(home) + "/.config/ntrickle/" + user_path;
        struct stat st {};
        if (::stat(user.c_str(), &st) == 0)
            return user;
    }

    return system_default;
}

} // namespace ntrickle
