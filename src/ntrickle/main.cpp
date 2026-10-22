// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "bandwidth.hpp"
#include "daemon_mode.hpp"
#include "logger.hpp"
#include "ntrickle/version.hpp"
#include "shared_config.hpp"
#include "top_tui.hpp"
#include "user_config.hpp"

#include <limits>
#include <string>
#include <string_view>

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifndef NTRICKLE_PRELOAD_LIBRARY_PATH
#define NTRICKLE_PRELOAD_LIBRARY_PATH "/usr/local/lib/ntrickle/libntrickle_preload.so"
#endif

#ifndef NTRICKLE_DEFAULT_DAEMON_SOCKET
#define NTRICKLE_DEFAULT_DAEMON_SOCKET "/run/ntrickle/ntrickled.sock"
#endif

#ifndef NTRICKLE_DEFAULT_CONFIG_PATH
#define NTRICKLE_DEFAULT_CONFIG_PATH "/etc/ntrickle/ntrickled.toml"
#endif

#ifndef NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH
#define NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH "/etc/ntrickle/process.toml"
#endif

#ifndef NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH
#define NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH "/etc/ntrickle/schedules.toml"
#endif

namespace {

constexpr std::string_view kPreloadName = "libntrickle_preload.so";

// Display name for help, diagnostics, and version output.  Set from argv[0] so
// the canonical `trickle` command and the `ntrickle` alias identify themselves
// correctly.
const char* g_progname = "trickle";

const char* get_progname(const char* argv0) {
    if (argv0 == nullptr || argv0[0] == '\0')
        return "trickle";
    const char* slash = std::strrchr(argv0, '/');
    return slash != nullptr ? slash + 1 : argv0;
}

bool is_ld_sep(char ch) {
    return ch == ':' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool ld_preload_contains(std::string_view existing, std::string_view path) {
    if (existing.empty() || path.empty())
        return false;
    std::size_t pos = 0;
    while (pos < existing.size()) {
        while (pos < existing.size() && is_ld_sep(existing[pos]))
            ++pos;
        std::size_t begin = pos;
        while (pos < existing.size() && !is_ld_sep(existing[pos]))
            ++pos;
        if (existing.substr(begin, pos - begin) == path)
            return true;
    }
    return false;
}

std::string merge_ld_preload(std::string_view preload_path, const char* existing) {
    if (existing == nullptr || existing[0] == '\0')
        return std::string(preload_path);
    const std::string_view ev(existing);
    if (ld_preload_contains(ev, preload_path))
        return std::string(ev);
    std::string merged(preload_path);
    merged += ' ';
    merged += ev;
    return merged;
}

[[noreturn]] void fatal_with_errno(int eval, const char* message) {
    std::fprintf(stderr, "%s: [FATAL] %s: %s\n", g_progname, message, std::strerror(errno));
    std::exit(eval);
}

[[noreturn]] void fatalx(int eval, const char* message) {
    std::fprintf(stderr, "%s: [FATAL] %s\n", g_progname, message);
    std::exit(eval);
}

void warn_with_errno(const char* message) {
    std::fprintf(stderr, "%s: [WARN] %s: %s\n", g_progname, message, std::strerror(errno));
}

std::string find_preload() {
    struct stat sb {};

    // 1. Env-var override (explicit path, not a search path)
    const char* env = std::getenv("NTRICKLE_PRELOAD_PATH");
    if (env != nullptr && env[0] != '\0') {
        if (::stat(env, &sb) == 0)
            return env;
    }

    // 2. CWD-relative
    if (::stat(kPreloadName.data(), &sb) == 0) {
        char cwd[MAXPATHLEN];
        if (::getcwd(cwd, sizeof(cwd)) == nullptr)
            fatalx(1, "getcwd");
        std::string absolute = cwd;
        absolute += '/';
        absolute += kPreloadName;
        return absolute;
    }

    // 3. LD_LIBRARY_PATH — scan each colon-separated directory
    const char* ld_path = std::getenv("LD_LIBRARY_PATH");
    if (ld_path != nullptr) {
        std::string_view sv(ld_path);
        size_t pos = 0;
        while (pos < sv.size()) {
            // Skip leading colons/spaces
            while (pos < sv.size() && (sv[pos] == ':' || sv[pos] == ' '))
                ++pos;
            size_t end = pos;
            while (end < sv.size() && sv[end] != ':')
                ++end;
            if (end > pos) {
                std::string candidate(sv.substr(pos, end - pos));
                if (candidate.back() != '/')
                    candidate += '/';
                candidate += kPreloadName;
                if (::stat(candidate.c_str(), &sb) == 0)
                    return candidate;
            }
            pos = end + 1;
        }
    }

    // 4. Installed path
    {
        const std::string installed = NTRICKLE_PRELOAD_LIBRARY_PATH;
        if (::stat(installed.c_str(), &sb) == 0)
            return installed;
    }

    fatalx(1, "Could not find preload object");
}

void set_env(const char* name, const std::string& value) {
    static_cast<void>(::setenv(name, value.c_str(), 1));
}

void set_env(const char* name, const char* value) {
    static_cast<void>(::setenv(name, value, 1));
}

// Connect to the daemon control socket, send a JSON query, return the response.
std::string query_ctl(const std::string& ctl_path, const std::string& request) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        std::fprintf(stderr, "%s: [ERROR] cannot create socket: %s\n", g_progname,
                     std::strerror(errno));
        std::exit(1);
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, ctl_path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::fprintf(stderr, "%s: [ERROR] cannot reach daemon at %s\n", g_progname,
                     ctl_path.c_str());
        std::exit(1);
    }

    // Send request
    std::string req = request;
    if (req.back() != '\n')
        req += '\n';
    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t n = write(fd, req.data() + sent, req.size() - sent);
        if (n <= 0) {
            std::fprintf(stderr, "%s: [ERROR] write error\n", g_progname);
            close(fd);
            std::exit(1);
        }
        sent += static_cast<size_t>(n);
    }

    // Read response
    char buf[32768];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        std::fprintf(stderr, "%s: [ERROR] no response from daemon\n", g_progname);
        std::exit(1);
    }
    buf[n] = '\0';
    return std::string(buf, static_cast<size_t>(n));
}

// Resolve the control socket path from the data socket path.
std::string ctl_socket_path(const std::string& data_socket) {
    return data_socket + ".ctl";
}

} // namespace

// Known subcommand names that don't require -- before them.
static bool is_subcommand_name(const char* s) {
    return s != nullptr && (std::strcmp(s, "top") == 0 || std::strcmp(s, "stats") == 0 ||
                            std::strcmp(s, "config") == 0 || std::strcmp(s, "doctor") == 0 ||
                            std::strcmp(s, "help") == 0);
}

int main(int argc, char** argv) {
    g_progname = get_progname(argc > 0 ? argv[0] : nullptr);

    // Find the -- separator or a bare subcommand name.
    // Everything before the separator/subcommand is parsed as trickle options;
    // everything after is the command to run or subcommand.
    int split_pos = argc; // default: everything is options, no command
    bool bare_subcommand = false;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--") == 0) {
            split_pos = i;
            break;
        }
        if (is_subcommand_name(argv[i])) {
            split_pos = i;
            bare_subcommand = true;
            break;
        }
    }

    // Classic trickle used -s for standalone mode.  This implementation does
    // not emulate the classic CLI; reject the flag explicitly instead of
    // silently interpreting it as something else.
    for (int i = 1; i < split_pos; ++i) {
        if (std::strcmp(argv[i], "-s") == 0) {
            std::fprintf(stderr,
                         "%s: classic -s (standalone) is not supported; "
                         "use -m standalone\n",
                         g_progname);
            return 1;
        }
    }

    // Collect the command (everything after the split point; in bare subcommand
    // mode the subcommand name itself is the first element).
    std::vector<std::string> command;
    for (int i = split_pos; i < argc; i++)
        command.push_back(argv[i]);
    // If -- was used, skip the -- token itself.
    if (!bare_subcommand && !command.empty() && command[0] == "--")
        command.erase(command.begin());

    // Only expose arguments before the split to argparse.
    int ntrickle_argc = split_pos;

    std::string winsz = "200";
    std::string downlim_str = "1M";
    std::string uplim_str = "1M";
    std::string mode_str;
    std::string tsmooth = "3.0";
    std::string lsmooth = "20";
    int weight_val = 1;
    std::string sockname;

    argparse::ArgumentParser program(g_progname, NTRICKLE_PROJECT_VERSION,
                                     argparse::default_arguments::help);
    program.add_description("Application-independent bandwidth limiter");

    program.add_argument("-V", "--version")
        .action([](const auto&) {
            std::printf("%s version %s\n", g_progname, NTRICKLE_PROJECT_VERSION);
            std::exit(0);
        })
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    program.add_argument("-m", "--mode")
        .help("Daemon mode: standalone, daemon (default), or daemon-required")
        .default_value(std::string(""))
        .metavar("MODE");

    program.add_argument("-d", "--download")
        .help("Set maximum download rate (requires a unit: 8M, 1.5G, 100Mbps, 100B, unlimited)")
        .default_value(std::string("1M"))
        .metavar("RATE");

    program.add_argument("-u", "--upload")
        .help("Set maximum upload rate (requires a unit: 8M, 1.5G, 100Mbps, 100B, unlimited)")
        .default_value(std::string("1M"))
        .metavar("RATE");

    program.add_argument("-w", "--window")
        .help("Set window length to <length> KB")
        .default_value(std::string("200"))
        .metavar("KB");

    program.add_argument("-W", "--weight")
        .help("Set bandwidth weight (share count, >= 1)")
        .scan<'i', int>()
        .default_value(1)
        .metavar("WEIGHT");

    program.add_argument("-t", "--time-smooth")
        .help("Set default smoothing time to <seconds> s")
        .default_value(std::string("3.0"))
        .metavar("SECONDS");

    program.add_argument("-l", "--length-smooth")
        .help("Set default smoothing length to <length> KB")
        .default_value(std::string("20"))
        .metavar("KB");

    program.add_argument("-n", "--socket")
        .help("Use the daemon socket at <path>")
        .default_value(std::string(""))
        .metavar("PATH");

    program.add_argument("--schedules")
        .help("Path to schedules.toml (time-based limit overrides)")
        .default_value(std::string(""))
        .metavar("PATH");

    program.add_argument("--process-config")
        .help("Path to process.toml (per-process weight/limit overrides)")
        .default_value(std::string(""))
        .metavar("PATH");

    program.add_argument("--suppress-warnings").help("Suppress schedule collision warnings").flag();

    try {
        program.parse_args(ntrickle_argc, argv);
    } catch (const std::exception& err) {
        std::fprintf(stderr, "%s\n", err.what());
        std::cerr << program;
        return 1;
    }

    if (command.empty()) {
        std::cerr << program;
        return 1;
    }

    // ---------------------------------------------------------------------------

    // Resolve socket for subcommand use: CLI flag > env > compiled-in default
    sockname = program.get<std::string>("-n");
    if (sockname.empty()) {
        sockname = NTRICKLE_DEFAULT_DAEMON_SOCKET;
    }
    // Subcommand: config validate
    // ---------------------------------------------------------------------------
    if (command.size() >= 2 && command[0] == "config" && command[1] == "validate") {
        int errors = 0;

        // Resolve daemon config path
        std::string daemon_cfg_path = NTRICKLE_DEFAULT_CONFIG_PATH;
        {
            const char* env = std::getenv("NTRICKLE_DAEMON_CONFIG");
            if (env != nullptr && env[0] != '\0')
                daemon_cfg_path = env;
        }

        // Resolve process config path
        std::string proc_path = NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH;
        {
            const char* env = std::getenv("NTRICKLE_PROCESS_CONFIG");
            if (env != nullptr && env[0] != '\0')
                proc_path = env;
        }

        // Resolve schedules config path
        std::string sched_path = NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH;
        {
            const char* env = std::getenv("NTRICKLE_SCHEDULES_CONFIG");
            if (env != nullptr && env[0] != '\0')
                sched_path = env;
        }

        std::printf("%s config validate\n", g_progname);
        std::printf("  daemon config:    %s\n", daemon_cfg_path.c_str());
        std::printf("  process config:   %s\n", proc_path.c_str());
        std::printf("  schedules config: %s\n", sched_path.c_str());

        // Daemon config
        {
            try {
                auto tbl = toml::parse_file(daemon_cfg_path);
                std::printf("  daemon config parse: OK\n");
            } catch (const toml::parse_error& err) {
                std::fprintf(stderr, "  daemon config parse: ERROR - %s\n", err.what());
                ++errors;
            }
        }
        // Process config
        {
            auto entries = ntrickle::parse_process_config(proc_path);
            std::printf("  process config: %zu entries OK\n", entries.size());
        }
        // Schedules config
        {
            auto entries = ntrickle::parse_schedules_config(sched_path);
            std::printf("  schedules config: %zu entries OK\n", entries.size());
        }

        return errors > 0 ? 1 : 0;
    }

    // -------------------------------------------------------------------------
    // Subcommand: stats
    // -------------------------------------------------------------------------
    if (command.size() >= 1 && command[0] == "stats") {
        std::string ctl =
            ctl_socket_path(sockname.empty() ? NTRICKLE_DEFAULT_DAEMON_SOCKET : sockname);
        auto resp = query_ctl(ctl, R"({"cmd":"stats"})");
        auto j = nlohmann::json::parse(resp);
        if (j.contains("error")) {
            std::fprintf(stderr, "%s: %s\n", g_progname, j["error"].get<std::string>().c_str());
            return 1;
        }
        // Print global
        auto& g = j["global"];
        std::printf("=== ntrickle daemon ===\n");
        std::printf("  Uptime:       %u s\n", g.value("uptime_sec", 0U));
        std::printf("  Fairness:     %s\n", g.value("fairness_mode", "").c_str());
        std::printf("  Idle thresh:  %u s\n", g.value("idle_threshold_sec", 0U));
        std::printf("  Burst:        %u s\n", g.value("burst_seconds", 0U));
        std::printf("  Schedule:     %s\n", g.value("active_schedule", "none").c_str());
        std::printf("  Download lim: %s\n",
                    ntrickle::format_bandwidth(g.value("download_limit_bps", 0U)).c_str());
        std::printf("  Upload lim:   %s\n",
                    ntrickle::format_bandwidth(g.value("upload_limit_bps", 0U)).c_str());

        // Print users
        std::printf("\n=== users ===\n");
        for (auto& u : j["users"]) {
            std::printf("  %-10s uid=%-5u weight=%-2u dl=%s ul=%s sessions=%u %s\n",
                        u.value("name", "?").c_str(), u.value("uid", 0U), u.value("weight", 1U),
                        ntrickle::format_bandwidth(u.value("download_rate_bps", 0U)).c_str(),
                        ntrickle::format_bandwidth(u.value("upload_rate_bps", 0U)).c_str(),
                        u.value("session_count", 0U), u.value("is_idle", false) ? "(idle)" : "");
        }
        if (j["users"].empty())
            std::printf("  (no active clients)\n");

        // Print sessions
        std::printf("\n=== sessions ===\n");
        for (auto& s : j["sessions"]) {
            std::printf("  pid=%-6d %-15s dl=%s ul=%s bytes_dl=%lu bytes_ul=%lu "
                        "conn=%us %s\n",
                        s.value("pid", 0), s.value("argv0", "?").c_str(),
                        ntrickle::format_bandwidth(s.value("download_rate_bps", 0U)).c_str(),
                        ntrickle::format_bandwidth(s.value("upload_rate_bps", 0U)).c_str(),
                        s.value("bytes_downloaded", 0UL), s.value("bytes_uploaded", 0UL),
                        s.value("connected_sec", 0U), s.value("is_idle", false) ? "(idle)" : "");
        }
        if (j["sessions"].empty())
            std::printf("  (no active sessions)\n");
        return 0;
    }

    // -------------------------------------------------------------------------
    // Subcommand: top
    // -------------------------------------------------------------------------
    if (command.size() >= 1 && command[0] == "top") {
        // Parse top-specific arguments: --once, --interval <seconds>, --socket <path>
        bool once = false;
        double interval = 1.0;
        std::string top_socket;

        for (size_t i = 1; i < command.size(); ++i) {
            if (command[i] == "--once") {
                once = true;
            } else if (command[i] == "--interval" && i + 1 < command.size()) {
                ++i;
                try {
                    interval = std::stod(command[i]);
                } catch (...) {
                    std::fprintf(stderr, "%s: invalid interval '%s'\n", g_progname,
                                 command[i].c_str());
                    return 1;
                }
            } else if (command[i] == "--socket" && i + 1 < command.size()) {
                ++i;
                top_socket = command[i];
            } else if (command[i] == "--help" || command[i] == "-h") {
                std::printf("Usage: %s top [OPTIONS]\n"
                            "\n"
                            "Interactive terminal dashboard for bandwidth monitoring.\n"
                            "\n"
                            "Options:\n"
                            "  --once              Render a single frame and exit\n"
                            "  --interval <secs>   Refresh interval in seconds (default: 1.0)\n"
                            "  --socket <path>     Daemon socket path\n"
                            "  --help, -h          Show this help\n"
                            "\n"
                            "Keyboard shortcuts:\n"
                            "  q                   Quit\n"
                            "  p                   Pause / resume updates\n"
                            "  s                   Cycle sort column\n"
                            "  r                   Reverse sort order\n"
                            "  g                   Toggle group-by-user mode\n"
                            "  h                   Show this help\n"
                            "  ↑/↓ or j/k          Scroll sessions\n"
                            "  PgUp/PgDn           Page scroll\n"
                            "  Home/End            Jump to top/bottom\n",
                            g_progname);
                return 0;
            } else {
                std::fprintf(stderr, "%s: unknown top option '%s'\n", g_progname,
                             command[i].c_str());
                std::fprintf(stderr, "Try %s top --help\n", g_progname);
                return 1;
            }
        }

        // Resolve socket path: explicit --socket > main --socket/-n > default
        std::string resolved_socket = sockname;
        if (!top_socket.empty())
            resolved_socket = top_socket;
        if (resolved_socket.empty())
            resolved_socket = NTRICKLE_DEFAULT_DAEMON_SOCKET;

        std::string ctl = ctl_socket_path(resolved_socket);

        ntrickle::TopTui tui(ctl, once, interval, g_progname);
        return tui.run();
    }

    // -------------------------------------------------------------------------
    // Subcommand: doctor
    // -------------------------------------------------------------------------
    if (command.size() >= 1 && command[0] == "doctor") {
        int issues = 0;
        auto check = [&](const char* label, bool ok) {
            std::printf("  %-30s %s\n", label, ok ? "OK" : "FAIL");
            if (!ok)
                ++issues;
        };

        std::printf("%s doctor\n", g_progname);

        // Preload library
        auto preload_path = find_preload();
        struct stat st {};
        check("preload library found", ::stat(preload_path.c_str(), &st) == 0);

        // Data socket
        std::string sock = sockname.empty() ? NTRICKLE_DEFAULT_DAEMON_SOCKET : sockname;
        check("data socket reachable", ::stat(sock.c_str(), &st) == 0);

        // Control socket
        std::string ctl = ctl_socket_path(sock);
        check("control socket reachable", ::stat(ctl.c_str(), &st) == 0);

        // Config files
        check("daemon config parseable", []() {
            try {
                auto t = toml::parse_file(NTRICKLE_DEFAULT_CONFIG_PATH);
                return true;
            } catch (...) {
                return false;
            }
        }());
        check("process config parseable",
              !ntrickle::parse_process_config(NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH).empty() ||
                  ::stat(NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH, &st) == -1); // missing is OK
        check("schedules config parseable",
              !ntrickle::parse_schedules_config(NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH).empty() ||
                  ::stat(NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH, &st) == -1);

        return issues > 0 ? 1 : 0;
    }
    downlim_str = program.get<std::string>("-d");
    uplim_str = program.get<std::string>("-u");
    mode_str = program.get<std::string>("-m");
    winsz = program.get<std::string>("-w");
    tsmooth = program.get<std::string>("-t");
    lsmooth = program.get<std::string>("-l");
    weight_val = program.get<int>("-W");
    std::string schedules_path = program.get<std::string>("--schedules");
    std::string process_config_path = program.get<std::string>("--process-config");
    bool suppress_warnings = program.get<bool>("--suppress-warnings");

    // Distinguish an explicit CLI value from the argparse default.  Config
    // overrides below must not replace a value the user typed, even when it
    // happens to equal the default.
    const bool download_explicit = program.is_used("-d");
    const bool upload_explicit = program.is_used("-u");
    const bool weight_explicit = program.is_used("-W");

    // Parse bandwidth rates
    auto down_parsed = ntrickle::parse_bandwidth(downlim_str);
    if (!down_parsed)
        fatalx(1, "Invalid download rate (include a unit, e.g. 10M, 500K, 100Mbps, 100B)");
    auto up_parsed = ntrickle::parse_bandwidth(uplim_str);
    if (!up_parsed)
        fatalx(1, "Invalid upload rate (include a unit, e.g. 10M, 500K, 100Mbps, 100B)");

    if (weight_val < 1)
        fatalx(1, "Weight must be >= 1");

    uint64_t down_bytes = *down_parsed;
    uint64_t up_bytes = *up_parsed;

    // Resolve daemon mode: CLI flag > user config > default
    auto ucfg = ntrickle::read_user_config();
    ntrickle::daemon_mode mode = ntrickle::daemon_mode::daemon;

    if (!mode_str.empty()) {
        auto parsed = ntrickle::parse_daemon_mode(mode_str);
        if (!parsed)
            fatalx(1, "Invalid daemon mode; valid: standalone, daemon, daemon-required");
        mode = *parsed;
    } else if (!ucfg.mode.empty()) {
        auto parsed = ntrickle::parse_daemon_mode(ucfg.mode);
        if (parsed)
            mode = *parsed;
    }

    // Resolve socket: CLI flag > user config > compiled-in default
    if (sockname.empty() && !ucfg.socket.empty()) {
        sockname = ucfg.socket;
    }

    std::string preload_path = find_preload();

    if (mode == ntrickle::daemon_mode::standalone) {
        sockname.clear();
    } else {
        if (sockname.empty()) {
            sockname = NTRICKLE_DEFAULT_DAEMON_SOCKET;
        }

        struct stat sb {};
        if (::stat(sockname.c_str(), &sb) == -1 && (errno == EACCES || errno == ENOENT)) {
            if (mode == ntrickle::daemon_mode::daemon_required) {
                fatalx(1, "Daemon not reachable (required by --mode daemon-required)");
            }
            warn_with_errno("Could not reach ntrickled, working independently");
            sockname.clear();
        }
    }

    // Schedule config
    {
        auto resolved_sched_path =
            ntrickle::resolve_config_path(schedules_path, "NTRICKLE_SCHEDULES_CONFIG",
                                          "schedules.toml", NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH);
        auto sched_entries = ntrickle::parse_schedules_config(resolved_sched_path);
        auto active = ntrickle::evaluate_schedules(sched_entries, suppress_warnings);
        if (active) {
            // Only apply if the user did not explicitly override on the CLI.
            if (!download_explicit && active->download) {
                down_bytes = *active->download;
            }
            if (!upload_explicit && active->upload) {
                up_bytes = *active->upload;
            }
        }
    }

    // Process config
    {
        auto resolved_process_path =
            ntrickle::resolve_config_path(process_config_path, "NTRICKLE_PROCESS_CONFIG",
                                          "process.toml", NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH);
        auto proc_entries = ntrickle::parse_process_config(resolved_process_path);
        auto* match = ntrickle::match_process(proc_entries, command[0]);
        if (match) {
            // Only apply if the user did not explicitly override on the CLI.
            if (!download_explicit && match->download) {
                down_bytes = *match->download;
            }
            if (!upload_explicit && match->upload) {
                up_bytes = *match->upload;
            }
            if (!weight_explicit && match->weight) {
                weight_val = static_cast<int>(*match->weight);
            }
        }
    }

    set_env("NTRICKLE_DOWNLOAD_LIMIT", std::to_string(down_bytes));
    set_env("NTRICKLE_UPLOAD_LIMIT", std::to_string(up_bytes));
    set_env("NTRICKLE_WINDOW_SIZE", winsz);
    // Build the full command line (argv[0] + all arguments) for display
    // in stats and the TUI.  Truncate to 255 chars to fit the protocol's
    // 256-byte argv0 field (one byte reserved for NUL).
    {
        std::string full_cmd = command[0];
        for (size_t i = 1; i < command.size(); ++i) {
            full_cmd += ' ';
            full_cmd += command[i];
        }
        if (full_cmd.size() > 255) {
            full_cmd.resize(252);
            full_cmd += "...";
        }
        set_env("NTRICKLE_ARGV", full_cmd.c_str());
    }
    set_env("NTRICKLE_SOCKNAME", sockname);
    set_env("NTRICKLE_TSMOOTH", tsmooth);
    set_env("NTRICKLE_LSMOOTH", lsmooth);
    set_env("NTRICKLE_WEIGHT", std::to_string(weight_val));

    set_env("LD_PRELOAD", merge_ld_preload(preload_path, std::getenv("LD_PRELOAD")));

    std::vector<const char*> exec_args;
    exec_args.reserve(command.size() + 1);
    for (const auto& arg : command) {
        exec_args.push_back(arg.c_str());
    }
    exec_args.push_back(nullptr);

    ::execvp(exec_args[0], const_cast<char* const*>(exec_args.data()));

    // execvp only returns on error.
    char msg[512];
    std::snprintf(msg, sizeof(msg), "Cannot execute '%s'", exec_args[0]);
    fatal_with_errno(1, msg);
}
