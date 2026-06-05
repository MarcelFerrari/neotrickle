// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ntrickle {

enum class daemon_mode {
    standalone,
    daemon,
    daemon_required,
};

inline std::optional<daemon_mode> parse_daemon_mode(std::string_view input) {
    if (input == "standalone")
        return daemon_mode::standalone;
    if (input == "daemon")
        return daemon_mode::daemon;
    if (input == "daemon-required")
        return daemon_mode::daemon_required;
    return std::nullopt;
}

} // namespace ntrickle
