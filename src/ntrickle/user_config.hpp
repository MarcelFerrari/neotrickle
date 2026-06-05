// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace ntrickle {

struct user_config {
    std::string mode;
    std::string socket;
};

// Reads ~/.config/ntrickle/ntrickle.toml (respecting XDG_CONFIG_HOME).
// Returns defaults (empty strings) if the file is missing or unparseable.
user_config read_user_config() noexcept;

} // namespace ntrickle
