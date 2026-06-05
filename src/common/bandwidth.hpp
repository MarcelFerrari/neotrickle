// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <optional>
#include <string>
#include <string_view>

namespace ntrickle {

constexpr uint64_t KIB = 1024;
constexpr uint64_t MIB = KIB * 1024;
constexpr uint64_t GIB = MIB * 1024;

inline std::optional<uint64_t> parse_bandwidth(std::string_view input) {
    // Trim whitespace
    auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos)
        return std::nullopt;
    auto end = input.find_last_not_of(" \t\r\n");
    auto trimmed = input.substr(start, end - start + 1);

    // "unlimited" (case-insensitive) or "0"
    if (trimmed.empty())
        return std::nullopt;
    std::string lower;
    lower.reserve(trimmed.size());
    for (char c : trimmed)
        lower.push_back(static_cast<char>(std::tolower(c)));
    if (lower == "unlimited")
        return uint64_t{0};

    // Extract numeric prefix
    size_t num_end = 0;
    bool has_dot = false;
    while (num_end < trimmed.size()) {
        char c = trimmed[num_end];
        if (std::isdigit(c)) {
            ++num_end;
            continue;
        }
        if (c == '.' && !has_dot) {
            has_dot = true;
            ++num_end;
            continue;
        }
        break;
    }
    if (num_end == 0)
        return std::nullopt;

    double number = 0.0;
    // std::from_chars for double is C++20; use std::strtod (C++11) so
    // the project can target C++17.  The numeric prefix was already
    // validated to contain only digits and at most one dot, so strtod
    // will never encounter "inf", "nan", or hex-float edge cases.
    std::string num_str(trimmed.substr(0, num_end));
    char* strtod_end = nullptr;
    number = std::strtod(num_str.c_str(), &strtod_end);
    if (strtod_end == nullptr || strtod_end != num_str.c_str() + num_str.size())
        return std::nullopt;
    if (number < 0)
        return std::nullopt;

    // Extract suffix. Uppercase first char only (K/M/G case-insensitive),
    // keep the rest as-is (B/b distinction matters).
    auto suffix_sv = trimmed.substr(num_end);
    std::string suffix;
    suffix.reserve(suffix_sv.size());
    if (!suffix_sv.empty()) {
        suffix.push_back(static_cast<char>(std::toupper(suffix_sv[0])));
        suffix.append(suffix_sv.substr(1));
    }

    // Strip optional trailing "/s" (case-insensitive)
    if (suffix.size() >= 2 &&
        (suffix[suffix.size() - 2] == '/' || suffix[suffix.size() - 2] == '\\') &&
        (suffix[suffix.size() - 1] == 's' || suffix[suffix.size() - 1] == 'S')) {
        suffix.resize(suffix.size() - 2);
    }

    // No suffix = raw bytes
    if (suffix.empty()) {
        return static_cast<uint64_t>(number);
    }

    // Suffix table: case-sensitive for B/b, prefix already normalized to uppercase K/M/G.
    // Longest match first.
    struct Entry {
        const char* s;
        uint64_t mul;
        uint64_t div;
    };
    static constexpr Entry table[] = {
        {"Gbps", GIB, 8}, {"Mbps", MIB, 8}, {"Kbps", KIB, 8}, {"Gb", GIB, 8}, {"Mb", MIB, 8},
        {"Kb", KIB, 8},   {"GB", GIB, 1},   {"MB", MIB, 1},   {"KB", KIB, 1}, {"G", GIB, 1},
        {"M", MIB, 1},    {"K", KIB, 1},    {"bps", 1, 8},    {"b", 1, 8},    {"B", 1, 1},
    };

    for (const auto& entry : table) {
        if (suffix == entry.s) {
            double result =
                number * static_cast<double>(entry.mul) / static_cast<double>(entry.div);
            if (result >= static_cast<double>(UINT64_MAX))
                return UINT64_MAX;
            auto bytes = static_cast<uint64_t>(result);
            // Minimum: 1 Kbps = 128 bytes/s
            if (bytes > 0 && bytes < 128)
                bytes = 128;
            // 0 * anything = 0 = unlimited
            if (number == 0.0)
                bytes = 0;
            return bytes;
        }
    }

    return std::nullopt;
}

inline std::string format_bandwidth(uint64_t bytes_per_sec) {
    if (bytes_per_sec == 0)
        return "unlimited";

    // Format as <value> <unit>/s, choosing the largest suitable unit.
    // For values that round to a clean integer, omit the decimal point.
    struct UnitInfo {
        uint64_t divisor;
        const char* label;
    };
    static constexpr UnitInfo units[] = {
        {GIB, "GB/s"},
        {MIB, "MB/s"},
        {KIB, "KB/s"},
    };

    for (const auto& u : units) {
        if (bytes_per_sec >= u.divisor) {
            double val = static_cast<double>(bytes_per_sec) / static_cast<double>(u.divisor);
            double intpart;
            if (std::modf(val, &intpart) < 0.05)
                return std::to_string(static_cast<uint64_t>(intpart)) + " " + u.label;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f %s", val, u.label);
            return buf;
        }
    }
    return std::to_string(bytes_per_sec) + " B/s";
}

// Compact format for space-constrained displays (TUI columns).
// Produces strings like "1M", "500K", "2.5G" without a "/s" suffix.
inline std::string format_bandwidth_short(uint64_t bytes_per_sec) {
    if (bytes_per_sec == 0)
        return "0";
    const char* u[] = {"K", "M", "G", "T"};
    double v = static_cast<double>(bytes_per_sec);
    int ui = -1;
    while (v >= 1024.0 && ui < 3) {
        v /= 1024.0;
        ++ui;
    }
    char buf[16];
    if (ui < 0)
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else if (v >= 100.0)
        std::snprintf(buf, sizeof(buf), "%.0f%s", v, u[ui]);
    else if (v >= 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f%s", v, u[ui]);
    else
        std::snprintf(buf, sizeof(buf), "%.2f%s", v, u[ui]);
    return buf;
}

} // namespace ntrickle
