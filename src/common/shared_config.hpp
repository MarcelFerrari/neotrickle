// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <ctime>

#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace ntrickle {

/// A single entry from process.toml.
struct process_entry {
    std::string pattern_str;
    std::regex pattern;
    std::optional<uint32_t> weight;
    std::optional<uint64_t> download; // bytes/sec
    std::optional<uint64_t> upload; // bytes/sec
    std::optional<double> time_smoothing; // seconds
    std::optional<uint32_t> length_smoothing; // KB
};

/// A single entry from schedules.toml.
struct schedule_entry {
    std::string name;
    std::optional<uint64_t> download; // bytes/sec (parsed from bandwidth string)
    std::optional<uint64_t> upload; // bytes/sec
    uint16_t start_minutes = 0; // minutes since midnight
    uint16_t end_minutes = 0; // minutes since midnight
    uint8_t days_bitmask = 0x7F; // bit 0=Mon … bit 6=Sun (0x7F = all days)
};

/// Effective global bandwidth limits after applying a schedule.
struct schedule_limits {
    uint64_t send = 0;
    uint64_t recv = 0;
};

// ---------------------------------------------------------------------------
// Process config
// ---------------------------------------------------------------------------

/// Parse a process.toml file.
/// Returns an empty vector if the file is missing or unparseable.
std::vector<process_entry> parse_process_config(const std::string& path);

/// Match argv0 against the process entries (first-match-wins).
/// Returns nullptr when no pattern matches.
const process_entry* match_process(const std::vector<process_entry>& entries,
                                   const std::string& argv0);

// ---------------------------------------------------------------------------
// Schedule config
// ---------------------------------------------------------------------------

/// Parse a schedules.toml file.
/// Returns an empty vector if the file is missing or unparseable.
std::vector<schedule_entry> parse_schedules_config(const std::string& path);

/// Evaluate schedule entries against the given time (or current time if 0).
/// Returns the first matching entry, or std::nullopt when nothing matches.
/// Writes collision warnings to stderr when multiple entries overlap.
std::optional<schedule_entry> evaluate_schedules(const std::vector<schedule_entry>& entries,
                                                 bool suppress_warnings = false,
                                                 time_t at_time = 0);

/// Combine the daemon's base limits with an active schedule. Directions the
/// schedule does not set fall back to the corresponding base value.
schedule_limits apply_schedule_limits(uint64_t base_send, uint64_t base_recv,
                                      const schedule_entry* active);

// ---------------------------------------------------------------------------
// Config path resolution (shared by CLI and daemon)
// ---------------------------------------------------------------------------

/// Resolve a config file path with precedence:
///   1. cli_path (if non-empty — the user passed a CLI flag)
///   2. environment variable named `env_var`
///   3. ~/.config/ntrickle/<user_path> (if it exists on disk)
///   4. system_default (compiled-in fallback)
std::string resolve_config_path(const std::string& cli_path, const char* env_var,
                                const char* user_path, const char* system_default);

} // namespace ntrickle
