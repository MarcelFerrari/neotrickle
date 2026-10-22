// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <ctime>

#include <string>
#include <vector>

namespace ntrickled {

class Client;
class ClientManager;

/// A point-in-time snapshot of daemon state for the JSON control API.
struct StatsSnapshot {
    // -- global --
    std::string fairness_mode; // "static" or "dynamic"
    uint64_t download_limit = 0; // bytes/sec
    uint64_t upload_limit = 0; // bytes/sec
    uint32_t idle_threshold_sec = 3;
    uint32_t burst_seconds = 0;
    std::string active_schedule; // empty if none
    time_t daemon_start_time = 0;

    // -- per user --
    struct UserRow {
        uid_t uid = 0;
        std::string name;
        uint32_t weight = 1;
        uint64_t download_cap = 0; // 0 = unlimited
        uint64_t upload_cap = 0;
        uint64_t download_rate = 0; // bytes/sec (window rate)
        uint64_t upload_rate = 0;
        uint64_t bytes_downloaded = 0;
        uint64_t bytes_uploaded = 0;
        uint32_t session_count = 0;
        bool is_idle = false; // all sessions idle (dynamic mode)
    };
    std::vector<UserRow> users;

    // -- per session --
    struct SessionRow {
        pid_t pid = 0;
        uid_t uid = 0;
        std::string argv0;
        uint64_t grant_download = 0; // bytes/sec
        uint64_t grant_upload = 0;
        uint64_t download_rate = 0; // bytes/sec (window rate)
        uint64_t upload_rate = 0;
        uint64_t bytes_downloaded = 0;
        uint64_t bytes_uploaded = 0;
        time_t connected_sec = 0;
        bool is_idle = false;
    };
    std::vector<SessionRow> sessions;
};

/// Accumulates per-client and per-user statistics from the live daemon state.
/// Call snapshot() on demand (from the control-socket handler) to produce
/// a consistent StatsSnapshot.
class StatsCollector {
public:
    StatsCollector() = default;

    /// Produce a point-in-time snapshot from the ClientManager.
    StatsSnapshot snapshot(const ClientManager& mgr, const uint64_t* globlim,
                           const std::string& fairness_mode, uint32_t idle_threshold_sec,
                           uint32_t burst_seconds, const std::string& active_schedule,
                           time_t daemon_start_time) const;

    /// Serialise a snapshot to a compact JSON string.
    static std::string to_json(const StatsSnapshot& snap);
};

} // namespace ntrickled
