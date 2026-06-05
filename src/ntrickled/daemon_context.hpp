// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <event.h>

#include "logger.hpp"
#include "shared_config.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ntrickled {

class ClientManager;
class config_reader;
using Logger = ntrickle::Logger;

// Simple RAII cleanup list — callbacks run on run_all().
class cleanup_list {
public:
    void add(std::function<void()> fn) {
        items_.push_back(std::move(fn));
    }
    void run_all() {
        for (auto it = items_.rbegin(); it != items_.rend(); ++it)
            (*it)();
        items_.clear();
    }

private:
    std::vector<std::function<void()>> items_;
};

// Daemon-wide state passed through libevent callback void* args.
struct DaemonContext {
    ClientManager* clients = nullptr;
    cleanup_list* cleanup = nullptr;
    config_reader* config = nullptr;
    Logger* logger = nullptr;
    const char* progname = "ntrickled";
    uint64_t* globlim = nullptr;
    uint32_t weight = 1;
    double tsmooth_val = 5.0;
    uint32_t lsmooth_val = 10;
    bool dynamic_fairness = false; // true = activity-based, false = connect/disconnect only
    bool suppress_warnings = false;
    uint32_t idle_threshold_sec = 3;
    uint32_t burst_seconds = 15; // 0 = disabled
    uint64_t burst_send = UINT64_MAX; // effectively unlimited
    uint64_t burst_recv = UINT64_MAX;
    std::string active_schedule_name; // set by on_force_timer when schedule is active
    time_t start_time = 0; // daemon start time for uptime
    int ctl_fd = -1; // control-socket listener fd
    std::vector<ntrickle::process_entry> process_configs;
    std::vector<ntrickle::schedule_entry> schedule_configs;
    uint64_t base_globlim[2]{}; // original limits from CLI/config (before schedule override)
    struct event* force_ev = nullptr;
    struct event* notify_ev = nullptr;
    int listener_fd = -1;
};

// RAII wrapper for libevent events. Calls event_del() on destruction.
class Event {
public:
    Event() = default;
    ~Event() {
        if (event_initialized(&ev_))
            event_del(&ev_);
    }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    struct event* get() {
        return &ev_;
    }

    void set(evutil_socket_t fd, short events, void (*cb)(evutil_socket_t, short, void*),
             void* arg) {
        event_set(&ev_, fd, events, cb, arg);
    }
    void add() {
        event_add(&ev_, nullptr);
    }
    void add(struct timeval* tv) {
        event_add(&ev_, tv);
    }

    void set_signal(int signum, void (*cb)(evutil_socket_t, short, void*), void* arg) {
        signal_set(&ev_, signum, cb, arg);
    }
    void add_signal() {
        signal_add(&ev_, nullptr);
    }

    void set_timer(void (*cb)(evutil_socket_t, short, void*), void* arg) {
        evtimer_set(&ev_, cb, arg);
    }
    void add_timer(struct timeval* tv) {
        evtimer_add(&ev_, tv);
    }

private:
    struct event ev_ {};
};

// RAII Unix domain listen socket.
// Owns the socket fd and the filesystem path entry.  Call release() to
// explicitly close and unlink; the destructor is a safe no-op if release()
// was already called (idempotent — safe to call from a cleanup_list
// callback before the destructor runs).
class UnixListener {
public:
    UnixListener(const char* path);
    ~UnixListener();

    UnixListener(const UnixListener&) = delete;
    UnixListener& operator=(const UnixListener&) = delete;

    int fd() const {
        return fd_;
    }
    const std::string& path() const {
        return path_;
    }

    void release();

private:
    int fd_ = -1;
    std::string path_;
};

} // namespace ntrickled
