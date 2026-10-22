// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <argparse/argparse.hpp>
#include <event.h>
#include <nlohmann/json.hpp>

#include "bandwidth.hpp"
#include "client.hpp"
#include "config.h"
#include "config.hpp"
#include "daemon_context.hpp"
#include "logger.hpp"
#include "stats_collector.hpp"

#include <algorithm>
#include <string>

extern "C" {
#include <sys/queue.h>

#include "bwstat.h"
#include "message.h"
#include "util.h"
}

namespace {

void on_msg(evutil_socket_t fd, short events, void* arg);
void on_signal(evutil_socket_t sig, short events, void* arg);
void on_force_timer(evutil_socket_t fd, short events, void* arg);
void on_notify_timer(evutil_socket_t fd, short events, void* arg);

const char* get_progname(const char* argv0) {
    if (argv0 == nullptr || argv0[0] == '\0')
        return "ntrickled";
    const char* slash = std::strrchr(argv0, '/');
    return slash != nullptr ? slash + 1 : argv0;
}

void handle_message(ntrickled::Client* cli, struct msg& msg, ntrickled::ClientManager* mgr,
                    ntrickled::DaemonContext* ctx) {
    switch (msg.type) {
    case MSG_TYPE_CONF: {
        auto* conf = &msg.data.conf;

        // Credentials are from SO_PEERCRED (kernel-verified at accept).
        // The conf message provides argv0 (which app is running) and
        // self-reported limits (advisory only).
        // conf->argv0 now carries the full command line (truncated to
        // 255 chars).  Store it as-is for display in stats and the TUI.
        cli->set_argv0(std::string_view(conf->argv0));

        // For process config matching, extract just the executable name
        // (first space-delimited token, or basename if the token contains /).
        std::string argv0_full(conf->argv0);
        auto space = argv0_full.find(' ');
        std::string proc_name =
            (space != std::string::npos) ? argv0_full.substr(0, space) : argv0_full;
        auto slash = proc_name.rfind('/');
        if (slash != std::string::npos)
            proc_name = proc_name.substr(slash + 1);

        // Weight from wire (set by CLI/env), may be overridden by process config.
        uint32_t weight = conf->weight > 0 ? conf->weight : ctx->weight;
        auto* proc_match = ntrickle::match_process(ctx->process_configs, proc_name);
        if (proc_match && proc_match->weight) {
            weight = *proc_match->weight;
            ctx->logger->info("Process config match '%s' for '%s': weight=%u",
                              proc_match->pattern_str.c_str(), proc_name.c_str(), weight);
        }
        cli->set_weight(weight);
        // Smoothing: process config match overrides the daemon defaults.
        double tsmooth = ctx->tsmooth_val;
        uint32_t lsmooth = ctx->lsmooth_val;
        if (proc_match) {
            if (proc_match->time_smoothing)
                tsmooth = *proc_match->time_smoothing;
            if (proc_match->length_smoothing)
                lsmooth = *proc_match->length_smoothing;
        }
        cli->set_time_smoothing(tsmooth);
        cli->set_length_smoothing(lsmooth);

        if (!cli->configure()) {
            ctx->logger->info("Client configuration error");
            mgr->unregister_client(cli);
            return;
        }

        mgr->register_client(cli);
        cli->set_flag(ntrickled::client_flag::configured);

        ctx->logger->info("New client: %d (%s/%s)", cli->pid(), cli->argv0().c_str(),
                          cli->uname().c_str());

        // Send initial grant and recompute for all clients
        mgr->recompute_grants(ctx->globlim);
        return;
    }

    default:
        if (!cli->has_flag(ntrickled::client_flag::configured))
            return;
        break;
    }

    switch (msg.type) {
    case MSG_TYPE_UPDATE: {
        auto* update = &msg.data.update;
        cli->update_stats(update->dir, update->len);
        break;
    }
    default:
        ctx->logger->info("Unknown message type %d", msg.type);
        break;
    }
}

void on_msg(evutil_socket_t /*fd*/, short /*events*/, void* arg) {
    auto* cli = static_cast<ntrickled::Client*>(arg);
    auto* ctx = cli->context();

    struct msg msg {};
    if (cli->receive_message(&msg) == -1) {
        ctx->clients->unregister_client(cli);
        return;
    }

    handle_message(cli, msg, ctx->clients, ctx);
}

void on_signal(evutil_socket_t sig, short /*events*/, void* arg) {
    auto* ctx = static_cast<ntrickled::DaemonContext*>(arg);
    const char* name = (sig == SIGTERM) ? "SIGTERM" : "SIGINT";
    ctx->cleanup->run_all();
    ctx->logger->fatal(0, "Received %s; quitting", name);
}

void on_force_timer(evutil_socket_t /*fd*/, short /*events*/, void* arg) {
    auto* ctx = static_cast<ntrickled::DaemonContext*>(arg);

    // Evaluate schedule and always rebuild both directions from the base
    // limits, so a direction omitted by the new schedule does not retain the
    // previous schedule's value.
    if (!ctx->schedule_configs.empty()) {
        auto active = ntrickle::evaluate_schedules(ctx->schedule_configs, ctx->suppress_warnings);
        if (active)
            ctx->active_schedule_name = active->name;
        else
            ctx->active_schedule_name.clear();

        auto limits = ntrickle::apply_schedule_limits(ctx->base_globlim[TRICKLE_SEND],
                                                      ctx->base_globlim[TRICKLE_RECV],
                                                      active ? &*active : nullptr);
        ctx->globlim[TRICKLE_SEND] = limits.send;
        ctx->globlim[TRICKLE_RECV] = limits.recv;
    }

    ctx->clients->force_update_all();
    // Periodically re-evaluate grants to handle burst expiry, idle detection,
    // and schedule changes (every 5 s).
    ctx->clients->recompute_grants();

    struct timeval forcetv = {5, 0};
    evtimer_add(ctx->force_ev, &forcetv);
}

void on_notify_timer(evutil_socket_t /*fd*/, short /*events*/, void* arg) {
    auto* ctx = static_cast<ntrickled::DaemonContext*>(arg);
    ctx->clients->print_rates();
}

// ---------------------------------------------------------------------------
// Control socket (JSON query API)
// ---------------------------------------------------------------------------

namespace {

std::string handle_ctl_query(const std::string& request_json, const ntrickled::DaemonContext* ctx) {
    using json = nlohmann::json;

    json req;
    try {
        req = json::parse(request_json);
    } catch (...) {
        json err;
        err["error"] = "invalid JSON";
        return err.dump();
    }

    auto cmd = req.value("cmd", "");
    std::string fairness_str = ctx->dynamic_fairness ? "dynamic" : "static";

    // Take a single snapshot for all commands that need it.
    // The collector is stateless — snapshot() is a pure query.
    ntrickled::StatsSnapshot snap;
    bool need_snap = (cmd == "stats" || cmd == "top" || cmd == "user");
    if (need_snap) {
        snap = ntrickled::StatsCollector{}.snapshot(*ctx->clients, ctx->globlim, fairness_str,
                                                    ctx->idle_threshold_sec, ctx->burst_seconds,
                                                    ctx->active_schedule_name, ctx->start_time);
    }

    if (cmd == "stats") {
        return ntrickled::StatsCollector::to_json(snap);
    }

    if (cmd == "top") {
        // "top" returns a compact subset: users sorted by bandwidth
        json j;
        auto users = json::array();
        // Sort users by total rate descending
        std::vector<const ntrickled::StatsSnapshot::UserRow*> sorted;
        for (const auto& u : snap.users)
            sorted.push_back(&u);
        std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) {
            return (a->download_rate + a->upload_rate) > (b->download_rate + b->upload_rate);
        });
        for (const auto* u : sorted) {
            json uj;
            uj["name"] = u->name;
            uj["weight"] = u->weight;
            uj["download_rate_bps"] = u->download_rate;
            uj["upload_rate_bps"] = u->upload_rate;
            uj["session_count"] = u->session_count;
            uj["is_idle"] = u->is_idle;
            users.push_back(std::move(uj));
        }
        j["users"] = std::move(users);
        return j.dump();
    }

    if (cmd == "user") {
        auto target = req.value("name", "");
        for (const auto& u : snap.users) {
            if (u.name == target) {
                json uj;
                uj["name"] = u.name;
                uj["uid"] = u.uid;
                uj["weight"] = u.weight;
                uj["download_cap_bps"] = u.download_cap;
                uj["upload_cap_bps"] = u.upload_cap;
                uj["download_rate_bps"] = u.download_rate;
                uj["upload_rate_bps"] = u.upload_rate;
                uj["bytes_downloaded"] = u.bytes_downloaded;
                uj["bytes_uploaded"] = u.bytes_uploaded;
                uj["session_count"] = u.session_count;
                uj["is_idle"] = u.is_idle;
                auto sessions = json::array();
                for (const auto& s : snap.sessions) {
                    if (s.uid == u.uid) {
                        json sj;
                        sj["pid"] = s.pid;
                        sj["argv0"] = s.argv0;
                        sj["download_rate_bps"] = s.download_rate;
                        sj["upload_rate_bps"] = s.upload_rate;
                        sj["bytes_downloaded"] = s.bytes_downloaded;
                        sj["bytes_uploaded"] = s.bytes_uploaded;
                        sj["connected_sec"] = s.connected_sec;
                        sj["is_idle"] = s.is_idle;
                        sessions.push_back(std::move(sj));
                    }
                }
                uj["sessions"] = std::move(sessions);
                return uj.dump();
            }
        }
        json err;
        err["error"] = "user not found";
        return err.dump();
    }

    json err;
    err["error"] = std::string("unknown cmd: ") + cmd;
    return err.dump();
}

// Bundle the daemon context and the libevent event so on_ctl_msg can
// clean up both after handling the query.
struct ctl_conn {
    ntrickled::DaemonContext* ctx;
    struct event ev;
};

void on_ctl_msg(evutil_socket_t fd, short /*events*/, void* arg) {
    auto* conn = static_cast<ctl_conn*>(arg);
    auto* ctx = conn->ctx;

    // Read request until newline or buffer full.  Unix-domain stream
    // sockets may deliver a partial message, though for the tiny JSON
    // requests we handle this is rare in practice.
    char buf[8192];
    size_t total = 0;
    bool have_newline = false;
    while (total < sizeof(buf) - 1) {
        ssize_t n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0)
            break;
        total += static_cast<size_t>(n);
        // Scan the newly-read bytes for a newline
        for (size_t i = total - static_cast<size_t>(n); i < total; ++i) {
            if (buf[i] == '\n') {
                total = i + 1; // include the newline
                have_newline = true;
                break;
            }
        }
        if (have_newline)
            break;
    }
    if (total == 0) {
        close(fd);
        event_del(&conn->ev);
        delete conn;
        return;
    }
    buf[total] = '\0';

    std::string response =
        handle_ctl_query(std::string(buf, have_newline ? total - 1 : total), ctx);
    response += "\n";

    size_t written = 0;
    while (written < response.size()) {
        ssize_t w = write(fd, response.data() + written, response.size() - written);
        if (w == -1 && errno == EINTR)
            continue;
        if (w <= 0)
            break;
        written += static_cast<size_t>(w);
    }
    close(fd);
    event_del(&conn->ev);
    delete conn;
}

void on_ctl_accept(evutil_socket_t /*fd*/, short /*events*/, void* arg) {
    auto* ctx = static_cast<ntrickled::DaemonContext*>(arg);

    struct sockaddr sa {};
    socklen_t salen = sizeof(sa);
    int s = accept(ctx->ctl_fd, &sa, &salen);
    if (s == -1) {
        ctx->logger->error_errno("ctl accept()");
        return;
    }

    auto* conn = new ctl_conn{ctx, {}};
    event_set(&conn->ev, s, EV_READ, on_ctl_msg, conn);
    event_add(&conn->ev, nullptr);
}

} // anonymous namespace

// ---------------------------------------------------------------------------

void on_accept(evutil_socket_t /*fd*/, short /*events*/, void* arg) {
    auto* ctx = static_cast<ntrickled::DaemonContext*>(arg);

    struct sockaddr sa {};
    socklen_t salen = sizeof(sa);
    int s = accept(ctx->listener_fd, &sa, &salen);
    if (s == -1) {
        ctx->logger->error_errno("accept()");
        return;
    }

    // Kernel-verified peer credentials
    struct ucred cred {};
    socklen_t cred_len = sizeof(cred);
    pid_t peer_pid = 0;
    uid_t peer_uid = 0;
    gid_t peer_gid = 0;
    if (getsockopt(s, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
        peer_pid = cred.pid;
        peer_uid = cred.uid;
        peer_gid = cred.gid;
    }

    std::string uname;
    std::string gname;
    struct passwd* pw = getpwuid(peer_uid);
    if (pw != nullptr)
        uname = pw->pw_name;
    struct group* gr = getgrgid(peer_gid);
    if (gr != nullptr)
        gname = gr->gr_name;

    auto* cli = ctx->clients->accept_client(s, peer_pid, peer_uid, peer_gid, uname, gname);
    cli->set_context(ctx);
    event_set(cli->event_ptr(), s, EV_READ | EV_PERSIST, on_msg, cli);
    if (event_add(cli->event_ptr(), nullptr) == -1)
        ctx->logger->error_errno("event_add()");
}

} // namespace

int main(int argc, char** argv) {
    const char* progname = get_progname(argc > 0 ? argv[0] : nullptr);
    int verbose = 0;
    bool daemonize = false;
    std::string downlim_str = "100M";
    std::string uplim_str = "100M";
    double tsmooth_val = 3.0;
    int lsmooth_val = 20;
    int weight_val = 1;
    int winsz_kb = 200;
    std::string sockname;
    std::string conf_path = NTRICKLE_DEFAULT_CONFIG_PATH;
    int notify_secs = 0;

    argparse::ArgumentParser program("ntrickled", NTRICKLE_PROJECT_VERSION,
                                     argparse::default_arguments::help);
    program.add_description("Bandwidth limiter coordination daemon");

    program.add_argument("-V", "--version")
        .action([](const auto&) {
            std::printf("ntrickle version %s\n", NTRICKLE_PROJECT_VERSION);
            std::exit(0);
        })
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    program.add_argument("-v", "--verbose")
        .help("Increase verbosity level")
        .action([&](const auto&) { ++verbose; })
        .append()
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    program.add_argument("-D", "--daemonize").help("Fork and detach from the terminal").flag();

    program.add_argument("--log-file")
        .help("Write log output to <file> instead of stderr (useful with -D)")
        .default_value(std::string(""))
        .metavar("FILE");

    program.add_argument("-d", "--download-rate")
        .help("Set maximum download rate (e.g. 8M, 1.5G, 100Mbps, unlimited)")
        .default_value(std::string("100M"))
        .metavar("RATE");

    program.add_argument("-u", "--upload-rate")
        .help("Set maximum upload rate (e.g. 8M, 1.5G, 100Mbps, unlimited)")
        .default_value(std::string("100M"))
        .metavar("RATE");

    program.add_argument("-t", "--time-smooth")
        .help("Set default smoothing time to <seconds> s")
        .scan<'g', double>()
        .default_value(3.0)
        .metavar("SECONDS");

    program.add_argument("-l", "--length-smooth")
        .help("Set default smoothing length to <length> KB")
        .scan<'i', int>()
        .default_value(20)
        .metavar("KB");

    program.add_argument("-W", "--weight")
        .help("Set default bandwidth weight (share count, >= 1)")
        .scan<'i', int>()
        .default_value(1)
        .metavar("WEIGHT");

    program.add_argument("--idle-threshold")
        .help("Idle timeout for dynamic fairness (seconds, default 3)")
        .scan<'i', int>()
        .default_value(3)
        .metavar("SECONDS");

    program.add_argument("--burst")
        .help("Burst duration for new connections (seconds, 0 = disabled, default 15)")
        .scan<'i', int>()
        .default_value(15)
        .metavar("SECONDS");

    program.add_argument("--burst-bandwidth")
        .help("Bandwidth during burst window (default 'unlimited')")
        .default_value(std::string("unlimited"))
        .metavar("RATE");

    program.add_argument("-c", "--config")
        .help("Use configuration file <file>")
        .default_value(std::string(NTRICKLE_DEFAULT_CONFIG_PATH))
        .metavar("FILE");

    program.add_argument("-n", "--socket")
        .help("Set socket name to <path>")
        .default_value(std::string(""))
        .metavar("PATH");

    program.add_argument("-N", "--notify")
        .help("Notify of bandwidth usage every <seconds> s")
        .scan<'i', int>()
        .default_value(0)
        .metavar("SECONDS");

    program.add_argument("-F", "--fairness")
        .help("Fairness mode: static (connect/disconnect) or dynamic (activity-based)")
        .default_value(std::string("static"))
        .metavar("MODE");

    program.add_argument("-w", "--window")
        .help("Set window size to <length> KB")
        .scan<'i', int>()
        .default_value(200)
        .metavar("KB");

    program.add_argument("--schedules")
        .help("Path to schedules.toml (time-based limit overrides)")
        .default_value(std::string(""))
        .metavar("PATH");

    program.add_argument("--process-config")
        .help("Path to process.toml (per-process weight/limit overrides)")
        .default_value(std::string(""))
        .metavar("PATH");

    program.add_argument("-s", "--suppress-warnings")
        .help("Suppress schedule collision warnings")
        .flag();

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::fprintf(stderr, "%s\n", err.what());
        std::cerr << program;
        return 1;
    }

    downlim_str = program.get<std::string>("-d");
    uplim_str = program.get<std::string>("-u");
    tsmooth_val = program.get<double>("-t");
    lsmooth_val = program.get<int>("-l");
    weight_val = program.get<int>("-W");
    conf_path = program.get<std::string>("-c");
    if (!program.is_used("-c")) {
        const char* env_config = std::getenv("NTRICKLE_DAEMON_CONFIG");
        if (env_config != nullptr && env_config[0] != '\0')
            conf_path = env_config;
    }
    sockname = program.get<std::string>("-n");
    notify_secs = program.get<int>("-N");
    winsz_kb = program.get<int>("-w");
    daemonize = program.get<bool>("-D");
    std::string log_file = program.get<std::string>("--log-file");
    std::string schedules_path = program.get<std::string>("--schedules");
    std::string process_config_path = program.get<std::string>("--process-config");
    bool suppress_warnings = program.get<bool>("-s");
    int idle_threshold = program.get<int>("--idle-threshold");
    int burst_seconds = program.get<int>("--burst");
    std::string burst_bw = program.get<std::string>("--burst-bandwidth");
    std::string fairness_mode = program.get<std::string>("-F");

    uint32_t winsz = static_cast<uint32_t>(winsz_kb) * 1024;

    // Resolved settings are also stored in DaemonContext so the control API
    // reports the effective values rather than the struct defaults.
    uint32_t idle_sec = 3;
    uint32_t burst_sec = 15;
    uint64_t burst_send = UINT64_MAX;
    uint64_t burst_recv = UINT64_MAX;

    auto down_parsed = ntrickle::parse_bandwidth(downlim_str);
    auto up_parsed = ntrickle::parse_bandwidth(uplim_str);
    if (!down_parsed) {
        std::fprintf(stderr, "ntrickled: invalid download rate\n");
        return 1;
    }
    if (!up_parsed) {
        std::fprintf(stderr, "ntrickled: invalid upload rate\n");
        return 1;
    }

    uint64_t globlim[2] = {*up_parsed, *down_parsed};

    if (weight_val < 1) {
        std::fprintf(stderr, "ntrickled: Invalid weight %d (must be >= 1)\n", weight_val);
        return 1;
    }

    ntrickled::Logger logger(progname, verbose);

    // Load TOML config
    struct stat sb {};
    if (stat(conf_path.c_str(), &sb) == -1 && errno == ENOENT) {
        logger.error_errno("Skipping configuration file: %s", conf_path.c_str());
    }
    auto config = ntrickled::config_reader(conf_path);

    if (daemonize && daemon(1, 0) == -1)
        logger.fatal_errno(1, "daemon()");

    // After daemonizing, stderr is closed.  Redirect to a log file if
    // --log-file was provided, otherwise log output is lost.
    if (!log_file.empty()) {
        if (freopen(log_file.c_str(), "a", stderr) == nullptr)
            std::fprintf(stderr, "%s: [ERROR] Cannot open log file %s: %s\n", progname,
                         log_file.c_str(), std::strerror(errno));
    }

    event_init();

    if (sockname.empty()) {
        sockname = NTRICKLE_DEFAULT_DAEMON_SOCKET;
    }

    // Ensure the parent directory exists.
    {
        std::string dir = sockname;
        auto slash = dir.rfind('/');
        if (slash != std::string::npos) {
            dir.resize(slash);
            if (mkdir(dir.c_str(), 0755) == -1 && errno != EEXIST)
                logger.fatal_errno(1, "Cannot create directory %s", dir.c_str());
        }
    }

    // Take an exclusive flock on a lock file to ensure only one daemon runs.
    // The lock is released automatically if the process exits or crashes.
    std::string lock_path = sockname + ".lock";
    int lock_fd = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd == -1)
        logger.fatal_errno(1, "Cannot create lock file %s", lock_path.c_str());
    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            logger.fatal(1,
                         "Another ntrickled instance is already running "
                         "(lock file %s is held)",
                         lock_path.c_str());
        logger.fatal_errno(1, "Cannot acquire lock on %s", lock_path.c_str());
    }

    if (stat(sockname.c_str(), &sb) != -1)
        logger.fatal(1, "Socket name %s not free", sockname.c_str());

    auto cleanup = ntrickled::cleanup_list{};
    // Unlink lock file and close its fd on clean exit
    cleanup.add([lock_path, lock_fd]() {
        unlink(lock_path.c_str());
        close(lock_fd);
    });
    // Fairness mode: CLI flag > config > default (static)
    bool dynamic_fairness = false;
    std::string fairness_source = "command line";
    if (!program.is_used("-F")) {
        fairness_mode = config.get_str("General", "Fairness", "static");
        fairness_source = "configuration file";
    }
    if (fairness_mode == "dynamic") {
        dynamic_fairness = true;
    } else if (fairness_mode == "static") {
        dynamic_fairness = false;
    } else {
        std::fprintf(stderr,
                     "ntrickled: invalid fairness mode '%s' from %s "
                     "(use static or dynamic)\n",
                     fairness_mode.c_str(), fairness_source.c_str());
        return 1;
    }

    auto clients = ntrickled::ClientManager(winsz);
    clients.set_logger(&logger);
    clients.set_globlim(globlim);
    clients.set_dynamic_fairness(dynamic_fairness);
    clients.set_user_configs(config.get_user_configs());

    // Idle threshold: CLI flag > TOML > default (3)
    {
        int idle_value = idle_threshold;
        if (!program.is_used("--idle-threshold"))
            idle_value = config.get_int("General", "Idle-Threshold", idle_threshold);
        idle_sec = static_cast<uint32_t>(idle_value > 0 ? idle_value : 3);
        clients.set_idle_threshold(idle_sec);
    }

    // Burst: CLI flag > TOML > default (15 s, unlimited)
    {
        int burst_value = burst_seconds;
        if (!program.is_used("--burst"))
            burst_value = config.get_int("General", "Burst-Seconds", burst_seconds);
        burst_sec = static_cast<uint32_t>(burst_value >= 0 ? burst_value : 15);
        clients.set_burst_seconds(burst_sec);

        // Burst bandwidth: CLI flag > TOML > default ("unlimited" = 0)
        std::string burst_bw_str = burst_bw;
        if (!program.is_used("--burst-bandwidth"))
            burst_bw_str = config.get_str("General", "Burst-Bandwidth", "unlimited");

        auto burst_parsed = ntrickle::parse_bandwidth(burst_bw_str);
        uint64_t burst_bps = burst_parsed.value_or(0);
        if (burst_bps == 0) {
            // "unlimited" → send a very large grant so the preload applies it
            burst_send = UINT64_MAX;
            burst_recv = UINT64_MAX;
        } else {
            burst_send = burst_bps;
            burst_recv = burst_send;
        }
        clients.set_burst_limits(burst_send, burst_recv);
    }

    // Process config: CLI flag > env var > default
    if (process_config_path.empty()) {
        const char* env = std::getenv("NTRICKLE_PROCESS_CONFIG");
        if (env != nullptr && env[0] != '\0') {
            process_config_path = env;
        } else {
            process_config_path = NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH;
        }
    }

    // Schedule config: CLI flag > env var > default
    if (schedules_path.empty()) {
        const char* env = std::getenv("NTRICKLE_SCHEDULES_CONFIG");
        if (env != nullptr && env[0] != '\0') {
            schedules_path = env;
        } else {
            schedules_path = NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH;
        }
    }

    auto listener = ntrickled::UnixListener(sockname.c_str());
    if (listener.fd() == -1)
        logger.fatal_errno(1, "UnixListener()");
    // Signal-safe cleanup: release() is idempotent; the destructor
    // will be a no-op afterwards, avoiding a double close/unlink.
    cleanup.add([&listener]() { listener.release(); });

    ntrickled::DaemonContext ctx{};
    ctx.clients = &clients;
    ctx.cleanup = &cleanup;
    ctx.config = &config;
    ctx.logger = &logger;
    ctx.progname = progname;
    ctx.globlim = globlim;
    ctx.weight = static_cast<uint32_t>(weight_val);
    ctx.tsmooth_val = tsmooth_val;
    ctx.lsmooth_val = static_cast<uint32_t>(lsmooth_val);
    ctx.dynamic_fairness = dynamic_fairness;
    ctx.suppress_warnings = suppress_warnings;
    ctx.idle_threshold_sec = idle_sec;
    ctx.burst_seconds = burst_sec;
    ctx.burst_send = burst_send;
    ctx.burst_recv = burst_recv;
    ctx.process_configs = ntrickle::parse_process_config(process_config_path);
    ctx.schedule_configs = ntrickle::parse_schedules_config(schedules_path);
    // Save base limits for restore when no schedule is active
    ctx.base_globlim[TRICKLE_SEND] = globlim[TRICKLE_SEND];
    ctx.base_globlim[TRICKLE_RECV] = globlim[TRICKLE_RECV];
    ctx.listener_fd = listener.fd();
    ctx.start_time = std::time(nullptr);

    // Control socket
    std::string ctl_path = sockname + ".ctl";
    auto ctl_listener = ntrickled::UnixListener(ctl_path.c_str());
    if (ctl_listener.fd() == -1)
        logger.fatal_errno(1, "UnixListener() for control socket");
    cleanup.add([&ctl_listener]() { ctl_listener.release(); });
    ctx.ctl_fd = ctl_listener.fd();

    logger.info("Using socket name: %s", sockname.c_str());
    logger.info("Control socket: %s", ctl_path.c_str());

    // Listen event
    ntrickled::Event listenev;
    listenev.set(listener.fd(), EV_READ | EV_PERSIST, on_accept, &ctx);
    listenev.add();

    // Control-socket listen event
    ntrickled::Event ctlev;
    ctlev.set(ctl_listener.fd(), EV_READ | EV_PERSIST, on_ctl_accept, &ctx);
    ctlev.add();

    // Signals
    ntrickled::Event sigterm;
    sigterm.set_signal(SIGTERM, on_signal, &ctx);
    sigterm.add_signal();

    ntrickled::Event sigint;
    sigint.set_signal(SIGINT, on_signal, &ctx);
    sigint.add_signal();

    // Notify timer
    ntrickled::Event notifyev;
    ctx.notify_ev = notifyev.get();
    if (notify_secs > 0) {
        struct timeval notifytv = {notify_secs, 0};
        notifyev.set_timer(on_notify_timer, &ctx);
        notifyev.add_timer(&notifytv);
    }

    // Force timer
    ntrickled::Event forceev;
    ctx.force_ev = forceev.get();
    {
        struct timeval forcetv = {5, 0};
        forceev.set_timer(on_force_timer, &ctx);
        forceev.add_timer(&forcetv);
    }

    event_dispatch();

    cleanup.run_all();
    return 0;
}