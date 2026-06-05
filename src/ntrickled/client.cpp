// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "client.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>

#include "logger.hpp"

extern "C" {
#include "bwstat.h"
}

#include "message.h"
#include "message_codec.hpp"

namespace ntrickled {

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(int fd, pid_t pid, uid_t uid, gid_t gid, std::string uname, std::string gname)
    : s_(fd), pid_(pid), uid_(uid), gid_(gid), uname_(std::move(uname)), gname_(std::move(gname)) {
    gettimeofday(&last_active_, nullptr);
    gettimeofday(&connect_time_, nullptr);
    auto* bs = bwstat_new();
    stat_.reset(bs);
}

Client::~Client() {
    if (event_initialized(&ev_)) {
        event_del(&ev_);
    }
    if (s_ != -1) {
        close(s_);
    }
}

bool Client::has_flag(client_flag f) const {
    return (flags_ & static_cast<int>(f)) != 0;
}

void Client::set_flag(client_flag f) {
    flags_ |= static_cast<int>(f);
}

void Client::clear_flag(client_flag f) {
    flags_ &= ~static_cast<int>(f);
}

void Client::set_weight(uint32_t w) {
    weight_ = w;
}
void Client::set_time_smoothing(double t) {
    tsmooth_ = t;
}
void Client::set_length_smoothing(uint32_t l) {
    lsmooth_ = l;
}

void Client::set_limits(uint64_t send_limit, uint64_t recv_limit) {
    lim_[TRICKLE_SEND] = send_limit;
    lim_[TRICKLE_RECV] = recv_limit;
}

bool Client::configure() {
    auto* bs = stat_.get();
    if (weight_ < 1)
        return false;
    bs->pts = weight_;
    if (tsmooth_ < 0.0)
        return false;
    bs->tsmooth = tsmooth_;
    if (lsmooth_ == 0)
        return false;
    bs->lsmooth = lsmooth_;
    return true;
}

bool Client::send_message(const struct msg& msg) {
    if (s_ == -1)
        return false;
    return MessageCodec::write_to(s_, msg);
}

void Client::mark_active() {
    gettimeofday(&last_active_, nullptr);
}

bool Client::is_active(uint32_t threshold_sec) const {
    struct timeval now;
    gettimeofday(&now, nullptr);
    struct timeval diff;
    timersub(&now, &last_active_, &diff);
    return static_cast<uint32_t>(diff.tv_sec) < threshold_sec;
}

int Client::receive_message(struct msg* msg) {
    if (s_ == -1)
        return -1;
    auto result = MessageCodec::read_from(s_);
    if (!result)
        return -1;
    *msg = *result;
    return 0;
}

void Client::update_stats(short which, size_t len) {
    mark_active();

    auto* bsd = &stat_->data[which];
    ctx_->logger->trace("Statistics (%s) for %d (%s/%s):", which == TRICKLE_SEND ? "SEND" : "RECV",
                        pid_, argv0_.c_str(), uname_.c_str());

    bwstat_update(stat_.get(), len, which);

    ctx_->logger->trace(
        "\tavg: %lu.%lu KB/s; win: %lu.%lu KB/s", (unsigned long) (bsd->rate / 1024),
        (unsigned long) ((bsd->rate % 1024) * 100 / 1024), (unsigned long) (bsd->winrate / 1024),
        (unsigned long) ((bsd->winrate % 1024) * 100 / 1024));
}

// ---------------------------------------------------------------------------
// ClientManager
// ---------------------------------------------------------------------------

ClientManager::ClientManager(uint32_t winsz) {
    bwstat_init(winsz);
}

ClientManager::~ClientManager() = default;

Client* ClientManager::accept_client(int fd, pid_t pid, uid_t uid, gid_t gid,
                                     const std::string& uname, const std::string& gname) {
    auto cli = std::make_unique<Client>(fd, pid, uid, gid, std::string(uname), std::string(gname));
    auto* raw = cli.get();
    pending_.push_back(std::move(cli));
    return raw;
}

void ClientManager::register_client(Client* client) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->get() == client) {
            auto ptr = std::move(*it);
            pending_.erase(it);
            clients_[ptr->pid()] = std::move(ptr);
            return;
        }
    }
}

void ClientManager::unregister_client(Client* client) {
    if (client->has_flag(client_flag::configured)) {
        if (logger_)
            logger_->info("Removed client: %d (%s/%s)", client->pid(), client->argv0().c_str(),
                          client->uname().c_str());
    }

    auto pid = client->pid();
    if (pid != 0 && clients_.count(pid)) {
        clients_.erase(pid);
        recompute_grants();
        return;
    }
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->get() == client) {
            pending_.erase(it);
            return;
        }
    }
}

void ClientManager::force_update_all() {
    for (auto& [pid, cli] : clients_) {
        auto* stat = cli->stat();
        bwstat_update(stat, 0, BWSTAT_SEND);
        bwstat_update(stat, 0, BWSTAT_RECV);
    }
    bwstat_update(nullptr, 0, BWSTAT_SEND);
    bwstat_update(nullptr, 0, BWSTAT_RECV);
}

void ClientManager::print_rates() {
    if (!logger_)
        return;
    auto* bs = bwstat_gettot();

    static constexpr struct {
        int dir;
        const char* label;
    } directions[] = {{TRICKLE_SEND, "UPLOAD"}, {TRICKLE_RECV, "DOWNLOAD"}};

    for (auto& d : directions) {
        auto* bsd = &bs->data[d.dir];
        logger_->info("%s total:\n"
                      "\tavg: %lu.%lu KB/s; win: %lu.%lu KB/s",
                      d.label, (unsigned long) (bsd->rate / 1024),
                      (unsigned long) ((bsd->rate % 1024) * 100 / 1024),
                      (unsigned long) (bsd->winrate / 1024),
                      (unsigned long) ((bsd->winrate % 1024) * 100 / 1024));
    }
}

void ClientManager::recompute_grants(const uint64_t* globlim) {
    if (clients_.empty())
        return;

    constexpr uint32_t kMinGrant = 1024; // 1 KB/s for inactive clients

    // Per-user aggregation: total process weight within user, list of clients.
    struct UserAgg {
        uint32_t user_weight = 1;
        uint32_t proc_weight_sum = 0;
        std::vector<Client*> sessions;
    };
    std::unordered_map<uid_t, UserAgg> users;

    struct timeval now;
    gettimeofday(&now, nullptr);

    for (auto& [pid, cli] : clients_) {
        auto* bs = cli->stat();
        if (bs == nullptr)
            continue;

        // Burst: newly-connected clients get unlimited (or configured) bandwidth
        // for the first burst_seconds_ seconds.
        if (burst_seconds_ > 0) {
            struct timeval diff;
            timersub(&now, &cli->connect_time(), &diff);
            if (static_cast<uint32_t>(diff.tv_sec) < burst_seconds_) {
                struct msg msg {};
                msg.type = MSG_TYPE_GRANT;
                msg.data.grant.grant[TRICKLE_SEND] = burst_send_;
                msg.data.grant.grant[TRICKLE_RECV] = burst_recv_;
                cli->send_message(msg);
                continue;
            }
        }

        // In dynamic mode, idle clients get a minimal grant and are
        // excluded from the fair-share pool so others can use their bandwidth.
        if (dynamic_fairness_ && !cli->is_active(idle_threshold_sec_)) {
            struct msg msg {};
            msg.type = MSG_TYPE_GRANT;
            msg.data.grant.grant[TRICKLE_SEND] = kMinGrant;
            msg.data.grant.grant[TRICKLE_RECV] = kMinGrant;
            cli->send_message(msg);
            continue;
        }

        auto& agg = users[cli->uid()];
        agg.proc_weight_sum += bs->pts; // pts = weight_ (set in configure())
        agg.sessions.push_back(cli.get());
    }

    if (users.empty())
        return;

    // Apply per-user weight and cap from daemon config ([users] section).
    for (auto& [uid, agg] : users) {
        if (agg.sessions.empty())
            continue;
        const auto& uname = agg.sessions[0]->uname();
        auto it = user_configs_.find(uname);
        if (it != user_configs_.end()) {
            agg.user_weight = it->second.weight;
        }
    }

    // Total user weight across all users
    uint64_t total_user_weight = 0;
    for (auto& [uid, agg] : users) {
        total_user_weight += agg.user_weight;
    }
    if (total_user_weight == 0)
        return;

    // Two-tier proportional scheduler:
    //   Tier 1: user_share = globlim * user_weight / total_user_weight
    //   Tier 2: proc_share = user_share * proc_weight / proc_weight_sum
    for (auto& [uid, agg] : users) {
        uint64_t user_send = globlim[TRICKLE_SEND] * agg.user_weight / total_user_weight;
        uint64_t user_recv = globlim[TRICKLE_RECV] * agg.user_weight / total_user_weight;

        // Apply per-user directional caps if configured
        if (!agg.sessions.empty()) {
            const auto& uname = agg.sessions[0]->uname();
            auto it = user_configs_.find(uname);
            if (it != user_configs_.end()) {
                if (it->second.has_download_cap) {
                    if (user_recv > it->second.download_cap)
                        user_recv = it->second.download_cap;
                }
                if (it->second.has_upload_cap) {
                    if (user_send > it->second.upload_cap)
                        user_send = it->second.upload_cap;
                }
            }
        }

        for (auto* cli : agg.sessions) {
            auto* bs = cli->stat();
            if (bs == nullptr || agg.proc_weight_sum == 0)
                continue;

            uint64_t proc_send = user_send * bs->pts / agg.proc_weight_sum;
            uint64_t proc_recv = user_recv * bs->pts / agg.proc_weight_sum;

            // A grant of 0 means "unlimited" in the preload (see getdelay:
            // if (xlim == 0) return NULL).  Clamp to the daemon's minimum grant
            // to prevent integer underflow from producing an accidental free pass.
            if (proc_send == 0)
                proc_send = kMinGrant;
            if (proc_recv == 0)
                proc_recv = kMinGrant;

            struct msg msg {};
            msg.type = MSG_TYPE_GRANT;
            msg.data.grant.grant[TRICKLE_SEND] = proc_send;
            msg.data.grant.grant[TRICKLE_RECV] = proc_recv;
            cli->send_message(msg);
        }
    }
}

} // namespace ntrickled
