// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <sys/queue.h>
#include <sys/types.h>

#include <event.h>

#include "bwstat.h"
#include "config.hpp"
#include "daemon_context.hpp"
#include "message.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ntrickled {

struct DaemonContext;

enum class client_flag : uint8_t {
    configured = 0x01,
};

class Client {
public:
    Client(int fd, pid_t pid, uid_t uid, gid_t gid, std::string uname, std::string gname);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    pid_t pid() const {
        return pid_;
    }
    void set_argv0(std::string_view a) {
        argv0_ = a;
    }
    const std::string& argv0() const {
        return argv0_;
    }
    const std::string& uname() const {
        return uname_;
    }
    const std::string& gname() const {
        return gname_;
    }
    uid_t uid() const {
        return uid_;
    }
    gid_t gid() const {
        return gid_;
    }
    int fd() const {
        return s_;
    }
    bwstat* stat() {
        return stat_.get();
    }

    bool has_flag(client_flag f) const;
    void set_flag(client_flag f);
    void clear_flag(client_flag f);

    void set_weight(uint32_t w);
    void set_time_smoothing(double t);
    void set_length_smoothing(uint32_t l);
    void set_limits(uint64_t send_limit, uint64_t recv_limit);
    bool configure();

    bool send_message(const struct msg& msg);
    /// Send a grant and cache it for statistics reporting.
    bool send_grant(uint64_t send_bps, uint64_t recv_bps);
    uint64_t grant_send() const {
        return grant_[TRICKLE_SEND];
    }
    uint64_t grant_recv() const {
        return grant_[TRICKLE_RECV];
    }
    int receive_message(struct msg* msg);

    void update_stats(short which, size_t len);

    struct event* event_ptr() {
        return &ev_;
    }

    void mark_active();
    bool is_active(uint32_t threshold_sec) const;
    const struct timeval& connect_time() const {
        return connect_time_;
    }

    void set_context(DaemonContext* ctx) {
        ctx_ = ctx;
    }
    DaemonContext* context() const {
        return ctx_;
    }

private:
    int s_ = -1;
    struct event ev_ {};
    int flags_ = 0;
    pid_t pid_ = 0;
    std::string argv0_;
    uid_t uid_ = 0;
    gid_t gid_ = 0;
    std::string uname_;
    std::string gname_;

    uint64_t lim_[2]{};
    uint64_t grant_[2]{};
    uint32_t weight_ = 1;
    std::unique_ptr<bwstat, decltype(&bwstat_free)> stat_{nullptr, bwstat_free};
    uint32_t lsmooth_ = 10;
    double tsmooth_ = 5.0;

    DaemonContext* ctx_ = nullptr;
    struct timeval last_active_ {};
    struct timeval connect_time_ {};
};

class ClientManager {
public:
    explicit ClientManager(uint32_t winsz);
    ~ClientManager();

    void set_logger(Logger* logger) {
        logger_ = logger;
    }
    void set_globlim(const uint64_t* globlim) {
        globlim_ = globlim;
    }
    void set_dynamic_fairness(bool v) {
        dynamic_fairness_ = v;
    }
    void set_idle_threshold(uint32_t sec) {
        idle_threshold_sec_ = sec;
    }
    void set_burst_seconds(uint32_t sec) {
        burst_seconds_ = sec;
    }
    void set_burst_limits(uint64_t send_bps, uint64_t recv_bps) {
        burst_send_ = send_bps;
        burst_recv_ = recv_bps;
    }
    void set_user_configs(std::unordered_map<std::string, user_config_entry> cfgs) {
        user_configs_ = std::move(cfgs);
    }

    Client* accept_client(int fd, pid_t pid, uid_t uid, gid_t gid, const std::string& uname,
                          const std::string& gname);
    void register_client(Client* client);
    void unregister_client(Client* client);
    void force_update_all();
    void print_rates();
    void recompute_grants(const uint64_t* globlim);
    void recompute_grants() {
        if (globlim_)
            recompute_grants(globlim_);
    }

    // Read-only access for stats collection
    const auto& clients() const {
        return clients_;
    }
    const auto& user_configs() const {
        return user_configs_;
    }

private:
    Logger* logger_ = nullptr;
    const uint64_t* globlim_ = nullptr;
    bool dynamic_fairness_ = false;
    uint32_t idle_threshold_sec_ = 3;
    uint32_t burst_seconds_ = 0;
    uint64_t burst_send_ = UINT64_MAX; // effectively unlimited
    uint64_t burst_recv_ = UINT64_MAX; // effectively unlimited
    std::unordered_map<std::string, user_config_entry> user_configs_;
    std::vector<std::unique_ptr<Client>> pending_;
    std::unordered_map<pid_t, std::unique_ptr<Client>> clients_;
};

} // namespace ntrickled
