/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test_stats_collector.cpp — Cached per-session grant reporting.
 *
 * Drives all three grant paths (burst, dynamic-idle, fair share) through
 * ClientManager::recompute_grants() and verifies both the wire message and the
 * snapshot/JSON values.  This ensures every send site uses the caching helper,
 * not just that the helper itself works.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

extern "C" {
#include "bwstat.h"
#include "message.h"
}

#include "client.hpp"
#include "message_codec.hpp"
#include "stats_collector.hpp"

static int failures = 0;

static void check_u64(const char* label, uint64_t got, uint64_t want) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: got %llu, want %llu\n", label,
                     static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
        ++failures;
    }
}

static bool read_grant(int fd, uint64_t* send_bps, uint64_t* recv_bps) {
    auto msg = ntrickled::MessageCodec::read_from(fd);
    if (!msg || msg->type != MSG_TYPE_GRANT) {
        std::fprintf(stderr, "FAIL: expected MSG_TYPE_GRANT on the wire\n");
        ++failures;
        return false;
    }
    *send_bps = msg->data.grant.grant[TRICKLE_SEND];
    *recv_bps = msg->data.grant.grant[TRICKLE_RECV];
    return true;
}

static uint64_t session_grant(const ntrickled::StatsSnapshot& snap, bool upload) {
    if (snap.sessions.size() != 1)
        return UINT64_MAX;
    return upload ? snap.sessions[0].grant_upload : snap.sessions[0].grant_download;
}

int main() {
    constexpr uint64_t kGlobSend = 1000000;
    constexpr uint64_t kGlobRecv = 2000000;
    uint64_t globlim[2] = {kGlobSend, kGlobRecv};

    {
        // Only one ClientManager per process: its constructor reinitializes
        // the global bwstat queue.
        ntrickled::ClientManager mgr(1024 * 200);
        mgr.set_globlim(globlim);

        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
            std::fprintf(stderr, "FAIL: socketpair\n");
            return 1;
        }

        auto* cli = mgr.accept_client(sv[0], getpid(), getuid(), getgid(), "testuser", "testgroup");
        cli->set_argv0("test-prog");
        cli->set_weight(2);
        cli->set_time_smoothing(3.0);
        cli->set_length_smoothing(20);
        if (!cli->configure()) {
            std::fprintf(stderr, "FAIL: client configure failed\n");
            ++failures;
        }
        mgr.register_client(cli);

        uint64_t send_bps = 0;
        uint64_t recv_bps = 0;

        // --- Burst branch --------------------------------------------------
        mgr.set_burst_seconds(60);
        mgr.set_burst_limits(700, 800);
        mgr.recompute_grants();
        if (read_grant(sv[1], &send_bps, &recv_bps)) {
            check_u64("burst wire send", send_bps, 700);
            check_u64("burst wire recv", recv_bps, 800);
        }
        auto snap = ntrickled::StatsCollector{}.snapshot(mgr, globlim, "static", 3, 60, "",
                                                         std::time(nullptr));
        check_u64("burst cached upload", session_grant(snap, true), 700);
        check_u64("burst cached download", session_grant(snap, false), 800);

        // --- Dynamic-idle branch -------------------------------------------
        mgr.set_burst_seconds(0);
        mgr.set_dynamic_fairness(true);
        mgr.set_idle_threshold(0); // is_active(0) is always false
        mgr.recompute_grants();
        if (read_grant(sv[1], &send_bps, &recv_bps)) {
            check_u64("idle wire send", send_bps, 1024);
            check_u64("idle wire recv", recv_bps, 1024);
        }
        snap = ntrickled::StatsCollector{}.snapshot(mgr, globlim, "dynamic", 0, 0, "",
                                                    std::time(nullptr));
        check_u64("idle cached upload", session_grant(snap, true), 1024);
        check_u64("idle cached download", session_grant(snap, false), 1024);

        // --- Fair-share branch ---------------------------------------------
        mgr.set_dynamic_fairness(false);
        ntrickled::user_config_entry cfg;
        cfg.weight = 1;
        cfg.upload_cap = 500;
        cfg.has_upload_cap = true;
        std::unordered_map<std::string, ntrickled::user_config_entry> users;
        users["testuser"] = cfg;
        mgr.set_user_configs(std::move(users));

        mgr.recompute_grants();
        if (read_grant(sv[1], &send_bps, &recv_bps)) {
            check_u64("fair-share wire send", send_bps, 500);
            check_u64("fair-share wire recv", recv_bps, kGlobRecv);
        }
        snap = ntrickled::StatsCollector{}.snapshot(mgr, globlim, "static", 3, 0, "",
                                                    std::time(nullptr));
        check_u64("fair-share cached upload", session_grant(snap, true), 500);
        check_u64("fair-share cached download", session_grant(snap, false), kGlobRecv);

        auto json_text = ntrickled::StatsCollector::to_json(snap);
        auto j = nlohmann::json::parse(json_text);
        bool found = false;
        for (const auto& session : j["sessions"]) {
            if (session.value("pid", 0) == static_cast<int>(getpid())) {
                found = true;
                check_u64("json grant upload", session.value("grant_upload_bps", 0ULL), 500);
                check_u64("json grant download", session.value("grant_download_bps", 0ULL),
                          kGlobRecv);
            }
        }
        if (!found) {
            std::fprintf(stderr, "FAIL: session missing from control-API JSON\n");
            ++failures;
        }

        close(sv[1]);
    } // ClientManager destroys clients and their bwstat entries

    // Free the singleton totals entry so the test is Valgrind-clean.
    if (auto* totals = bwstat_gettot())
        bwstat_free(totals);

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: stats collector grant tests\n");
    return 0;
}
