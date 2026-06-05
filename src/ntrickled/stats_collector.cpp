#include "stats_collector.hpp"

#include <cstdio>
#include <ctime>
#include <sys/time.h>

#include <nlohmann/json.hpp>

#include "client.hpp"

using json = nlohmann::json;

namespace ntrickled {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Elapsed seconds since `tv`.
uint32_t elapsed_sec(const struct timeval& tv) {
    struct timeval now;
    gettimeofday(&now, nullptr);
    struct timeval diff;
    timersub(&now, &tv, &diff);
    return static_cast<uint32_t>(diff.tv_sec);
}

/// Elapsed seconds since a time_t.
uint32_t elapsed_since(time_t start) {
    time_t now = std::time(nullptr);
    if (now < start)
        return 0;
    return static_cast<uint32_t>(now - start);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// snapshot
// ---------------------------------------------------------------------------

StatsSnapshot StatsCollector::snapshot(const ClientManager& mgr, const uint64_t* globlim,
                                       const std::string& fairness_mode,
                                       uint32_t idle_threshold_sec, uint32_t burst_seconds,
                                       const std::string& active_schedule,
                                       time_t daemon_start_time) const {
    StatsSnapshot snap;
    snap.fairness_mode = fairness_mode;
    snap.download_limit = globlim[TRICKLE_RECV];
    snap.upload_limit = globlim[TRICKLE_SEND];
    snap.idle_threshold_sec = idle_threshold_sec;
    snap.burst_seconds = burst_seconds;
    snap.active_schedule = active_schedule;
    snap.daemon_start_time = daemon_start_time;

    // Iterate registered clients
    struct UserAgg {
        uint32_t weight = 1;
        uint64_t download_cap = 0;
        uint64_t upload_cap = 0;
        uint64_t download_rate = 0;
        uint64_t upload_rate = 0;
        uint64_t bytes_downloaded = 0;
        uint64_t bytes_uploaded = 0;
        uint32_t session_count = 0;
        bool all_idle = true;
    };
    std::unordered_map<uid_t, UserAgg> user_map;

    for (const auto& pid_cli : mgr.clients()) {
        const auto& cli = pid_cli.second;
        auto* bs = cli->stat();
        if (bs == nullptr)
            continue;

        StatsSnapshot::SessionRow sess;
        sess.pid = cli->pid();
        sess.uid = cli->uid();
        sess.argv0 = cli->argv0();
        sess.download_rate = bs->data[TRICKLE_RECV].winrate;
        sess.upload_rate = bs->data[TRICKLE_SEND].winrate;
        sess.bytes_downloaded = bs->data[TRICKLE_RECV].bytes;
        sess.bytes_uploaded = bs->data[TRICKLE_SEND].bytes;
        sess.connected_sec = elapsed_sec(cli->connect_time());
        sess.is_idle = !cli->is_active(idle_threshold_sec);

        // Grant: we don't store the last grant on the Client; approximate
        // from the bwstat pts and the current schedule state.
        // The real grant is computed in recompute_grants() and sent immediately.
        // We record 0 here; a future enhancement could cache the last grant.
        sess.grant_download = 0;
        sess.grant_upload = 0;

        snap.sessions.push_back(std::move(sess));

        // Aggregate into user
        auto& ua = user_map[cli->uid()];
        // Weight / caps are per-user; resolve from the daemon's user_configs_
        // on the first session we encounter for this UID.
        if (ua.session_count == 0) {
            const auto& uname = cli->uname();
            const auto& ucfgs = mgr.user_configs();
            auto it = ucfgs.find(uname);
            if (it != ucfgs.end()) {
                ua.weight = it->second.weight;
                if (it->second.has_download_cap)
                    ua.download_cap = it->second.download_cap;
                if (it->second.has_upload_cap)
                    ua.upload_cap = it->second.upload_cap;
            } else {
                ua.weight = bs->pts; // fall back to process weight
            }
        }
        ua.download_rate += bs->data[TRICKLE_RECV].winrate;
        ua.upload_rate += bs->data[TRICKLE_SEND].winrate;
        ua.bytes_downloaded += bs->data[TRICKLE_RECV].bytes;
        ua.bytes_uploaded += bs->data[TRICKLE_SEND].bytes;
        ua.session_count++;
        if (!sess.is_idle)
            ua.all_idle = false;
    }

    // Build a UID → username map once, avoiding a per-user scan of
    // the client map inside a nested loop.
    std::unordered_map<uid_t, std::string> uid_names;
    for (const auto& pid_cli : mgr.clients()) {
        uid_t u = pid_cli.second->uid();
        if (!uid_names.count(u))
            uid_names[u] = pid_cli.second->uname();
    }

    // Convert user map to UserRows
    for (const auto& uid_ua : user_map) {
        StatsSnapshot::UserRow ur;
        ur.uid = uid_ua.first;
        ur.name = uid_names[uid_ua.first];
        ur.weight = uid_ua.second.weight;
        ur.download_cap = uid_ua.second.download_cap;
        ur.upload_cap = uid_ua.second.upload_cap;
        ur.download_rate = uid_ua.second.download_rate;
        ur.upload_rate = uid_ua.second.upload_rate;
        ur.bytes_downloaded = uid_ua.second.bytes_downloaded;
        ur.bytes_uploaded = uid_ua.second.bytes_uploaded;
        ur.session_count = uid_ua.second.session_count;
        ur.is_idle = uid_ua.second.all_idle;

        snap.users.push_back(std::move(ur));
    }

    return snap;
}

// ---------------------------------------------------------------------------
// to_json
// ---------------------------------------------------------------------------

std::string StatsCollector::to_json(const StatsSnapshot& snap) {
    json j;

    // Global
    j["global"]["download_limit_bps"] = snap.download_limit;
    j["global"]["upload_limit_bps"] = snap.upload_limit;
    j["global"]["fairness_mode"] = snap.fairness_mode;
    j["global"]["idle_threshold_sec"] = snap.idle_threshold_sec;
    j["global"]["burst_seconds"] = snap.burst_seconds;
    j["global"]["active_schedule"] = snap.active_schedule;
    j["global"]["uptime_sec"] = elapsed_since(snap.daemon_start_time);

    // Users
    json users_json = json::array();
    for (const auto& u : snap.users) {
        json uj;
        uj["uid"] = u.uid;
        uj["name"] = u.name;
        uj["weight"] = u.weight;
        uj["download_cap_bps"] = u.download_cap;
        uj["upload_cap_bps"] = u.upload_cap;
        uj["download_rate_bps"] = u.download_rate;
        uj["upload_rate_bps"] = u.upload_rate;
        uj["bytes_downloaded"] = u.bytes_downloaded;
        uj["bytes_uploaded"] = u.bytes_uploaded;
        uj["session_count"] = u.session_count;
        uj["is_idle"] = u.is_idle;
        users_json.push_back(std::move(uj));
    }
    j["users"] = std::move(users_json);

    // Sessions
    json sessions_json = json::array();
    for (const auto& s : snap.sessions) {
        json sj;
        sj["pid"] = s.pid;
        sj["uid"] = s.uid;
        sj["argv0"] = s.argv0;
        sj["grant_download_bps"] = s.grant_download;
        sj["grant_upload_bps"] = s.grant_upload;
        sj["download_rate_bps"] = s.download_rate;
        sj["upload_rate_bps"] = s.upload_rate;
        sj["bytes_downloaded"] = s.bytes_downloaded;
        sj["bytes_uploaded"] = s.bytes_uploaded;
        sj["connected_sec"] = s.connected_sec;
        sj["is_idle"] = s.is_idle;
        sessions_json.push_back(std::move(sj));
    }
    j["sessions"] = std::move(sessions_json);

    return j.dump();
}

} // namespace ntrickled
