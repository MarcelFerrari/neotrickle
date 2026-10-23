# AGENTS.md

This file is written for coding agents working on `ntrickle`. Read it before adding code. The project implements protocol v3 (64-bit limits, weight field), a two-tier proportional scheduler, per-user directional caps, process config (regex patterns), scheduled limits (time-of-day), idle-threshold / burst mode, flock-based singleton lock, `config validate`/`stats`/`doctor` subcommands, and an interactive ncurses TUI dashboard. The daemon runs foreground by default. The preload is 8 C modules with a standalone codec (no librpc).

## 1. Project summary

`ntrickle` is an application-independent bandwidth limiter for Linux. It prevents large downloads (model weights, datasets, container images, artifacts) from saturating a network connection without requiring per-application configuration. It builds on the classic `trickle` cooperative bandwidth-limiting approach.

The tool is **cooperative** — it is not a kernel enforcement system and is not a security boundary. It relies on `LD_PRELOAD` to intercept libc socket calls and works with dynamically linked programs that use standard libc I/O functions.

Primary audiences: ML engineers and researchers downloading large artifacts on shared machines; homelab and workstation users wanting to prevent background transfers from monopolizing their connection; administrators of shared compute environments who need per-user bandwidth governance.

## 2. High-level architecture

Three runtime components:

- **`trickle`**: installed C++ CLI launcher (`ntrickle` alias). Parses arguments with argparse (`-d 8M`, `-W 3`, `-m standalone`), reads user config, resolves the preload library, merges `LD_PRELOAD`, evaluates process/schedule configs, and `execvp`s the command. No throttling logic.
- **`trickled`**: installed C++ coordination daemon (`ntrickled` alias). libevent event loop, argparse CLI, toml++ TOML config, C++ `MessageCodec` for wire serialization. Handles client registration (SO_PEERCRED), per-user weight-proportional sharing with directional caps, activity-based dynamic fairness, opt-in burst mode for new connections, schedule re-evaluation every 5 s, flock-based singleton locking. Runs foreground by default.
- **`libntrickle_preload.so`**: C-only LD_PRELOAD library. Intercepts libc socket I/O, enforces bandwidth locally via token-bucket (daemon grants or standalone limits), communicates over Unix socket. Refactored into 8 modules. Must remain C-only.

The flow:
1. User runs `trickle -d 1M -W 2 -- curl ...`
2. CLI sets env vars (including `LD_PRELOAD`), evaluates process/schedule configs, execs curl
3. Preload connects to daemon, sends CONF (with weight), receives rate grant, enforces locally
4. Daemon divides global limits among weighted users, sends grant updates on connect/disconnect and every 5 s (burst expiry, idle detection, and schedule changes)

## 3. Directory map

```
src/
  ntrickle/             C++ CLI (main.cpp, top_tui.cpp/hpp, tui_text.hpp,
                          tui_glyphs.hpp, user_config.cpp/hpp)
  ntrickled/             C++ daemon (ntrickled.cpp, client.cpp/hpp, config.cpp/hpp,
                          daemon_context.cpp/hpp, message_codec.cpp/hpp,
                          stats_collector.cpp/hpp)
  preload/               C-only preload (8 modules, see Section 4)
  common/
    bwstat.c/h           Token-bucket bandwidth statistics (shared C, linked by preload + daemon)
    message.h            Protocol constants and struct definitions (shared C header)
    util.h               Preload utility macros (SET/CLR/ISSET, timersub)
    bandwidth.hpp        Bandwidth unit parser (header-only, shared C++)
    daemon_mode.hpp      Daemon mode enum (header-only, shared C++)
    shared_config.hpp/cpp  Process/schedule config types and parsers (shared C++)
    logger.hpp           Variadic-template Logger (header-only, shared C++)
    config.h.in          Generated system-feature header template
    version.hpp.in       Generated version/path header template
  third_party/
    argparse/            argparse.hpp v3.2.0 (MIT)
    tomlplusplus/         toml++.hpp v3.4.0 (MIT)
    nlohmann/            json.hpp (MIT), used by the daemon control API and CLI
  tests/
    unit/                CTest: test_message_codec, test_bandwidth, test_scheduler,
                          test_shared_config, test_daemon_context, test_stats_collector,
                          test_tui_text, test_tui_glyphs
    integration/         pytest: curl, wget, Python requests/urllib3, daemon lifecycle,
                          config precedence, IPv6, sendfile accounting
    smoke/               CTest: version, help, install, standalone wrap
    valgrind/            pytest: daemon/CLI lifecycle under Valgrind
    manual/              Interactive test server and manual checks
    requirements.txt     Python test dependencies
  scripts/               Developer scripts (e.g. mock_daemon.py)
  config/                Example TOML configs (ntrickled, process, schedules), systemd template
  cmake/                 CMake modules
```

## 4. Component boundaries

### Hard boundary: preload stays C

Everything in `src/preload/` is C-only, `-Wall -Wextra`, linked into the LD_PRELOAD shared library. No C++, no STL, no dynamic allocation in hot paths.

| File | Role |
|---|---|
| `trickle-overload.c` | Bootstrap (`trickle_init`), symbol resolution, diagnostics |
| `state.h` / `state.c` | Global state, descriptor table (`socket`/`close`/`dup`/`dup2`/`accept`), locking |
| `limiter.h` / `limiter.c` | Hot path: `delay`, `getdelay`, `update`, `updatesd` |
| `wrappers.c` | I/O wrappers: `read`/`readv`/`recv`/`recvfrom`, `write`/`writev`/`send`/`sendto`/`sendfile` |
| `readiness.c` | `select` and `poll` wrappers |
| `ntrickledu.h` / `ntrickledu.c` | Grant-based daemon client: connect, receive grant, periodic poll, fire-and-forget updates |
| `codec.h` / `codec.c` | Manual XDR-compatible serialization (`htonl`/`memcpy`, no librpc) |
| `atomicio.c` | Resilient read/write loop |

### Shared code (`src/common/`): minimal

`src/common/` holds `ntrickle::shared_c` (linked into preload and daemon), header-only helpers, and the config parser library:
- `bwstat.c` / `bwstat.h` — token-bucket bandwidth statistics (the only shared `.c` file)
- `message.h` — protocol constants (PROTOCOL_VERSION, TRICKLE_SEND/RECV, MSG_TYPE_*), struct msg
- `util.h` — SET/CLR/ISSET macros, timersub, atomicio declaration
- `shared_config.cpp` / `shared_config.hpp` — process/schedule config parsers and effective schedule limits (`ntrickle::config_shared`)
- `logger.hpp` — variadic-template Logger shared by the CLI and daemon

### Daemon: modern C++

| File | Role |
|---|---|
| `ntrickled.cpp` | `main()`: argparse, libevent loop, signal handling, message dispatch, flock locking, schedule evaluation |
| `client.cpp` / `client.hpp` | `Client` (per-process state, activity tracking, connect time) and `ClientManager` (registry, two-tier weight-proportional grant computation, idle detection, burst mode) |
| `config.cpp` / `config.hpp` | TOML config reader (get_int, get_float, get_str), user config parsing ([users] section) |
| `message_codec.cpp` / `message_codec.hpp` | C++ codec: encode/decode `struct msg` to/from wire format (v3, 288-byte CONF data / 296-byte encoded payload, 64-bit) |
| `daemon_context.hpp` / `daemon_context.cpp` | DaemonContext, RAII Event, RAII UnixListener, cleanup_list |
| `stats_collector.cpp` / `stats_collector.hpp` | Point-in-time daemon snapshot for the JSON control API, including cached per-session grants |

### CLI: modern C++

`src/ntrickle/main.cpp` — argparse, bandwidth parser, user config, process/schedule config evaluation, preload resolution, `LD_PRELOAD` merge, execvp. `src/ntrickle/top_tui.cpp` implements the ncurses dashboard.

### Third-party

- `argparse` v3.2.0 (MIT) — CLI parsing
- `tomlplusplus` v3.4.0 (MIT) — TOML parsing
- `nlohmann/json` (MIT) — control-API and CLI JSON
- `libevent` — daemon event loop (system)
- `ncursesw` — TUI dashboard (system)
- No librpc, no BSD shims

## 5. Preload-specific rules

Same as before — preload is highest-risk. Keep C-only. Avoid recursion, allocation in hot paths. Preserve errno. Use libc function pointers for real I/O.

## 6. Protocol (v3, grant-based, 64-bit)

Three message types over Unix socket:

| Type | Direction | Purpose |
|---|---|---|
| MSG_TYPE_CONF (1) | preload → daemon | Registration (lim[2], pid, argv0[256], uid, gid, weight) |
| MSG_TYPE_GRANT (9) | daemon → preload | Rate grant: `{send, recv} bytes/sec` |
| MSG_TYPE_UPDATE (2) | preload → daemon | Fire-and-forget byte counts |

Wire: `[1 byte: version=3] [4 bytes: len BE] [serialized struct msg]`

`struct msg_conf` is 288 bytes of data (was 284 in v1); the encoded payload is 296 bytes, including the 8-byte status/type header. Added `uint32_t weight` after `gid`.

- Preload: `codec.c` (standalone C, `htonl`/`memcpy`)
- Daemon: `MessageCodec` (standalone C++)
- Verified byte-for-byte by `test_message_codec`

## 7. Daemon responsibilities

- **Client registration** with SO_PEERCRED (kernel-verified UID/GID/PID)
- **Two-tier weight-proportional scheduler**:
  - Tier 1: `user_share = globlim × user_weight / total_user_weight`
  - Tier 2: `proc_share = user_share × proc_weight / user_proc_weight_sum`
- **Per-user directional caps** (`[users.<name>]` → `download`, `upload`)
- **Activity-based dynamic fairness** (`--fairness dynamic`): idle clients get 1 KB/s minimal grant, bandwidth redistributed every 5 s. Idle threshold configurable (`--idle-threshold` / `Idle-Threshold`).
- **Burst mode**: opt-in; new connections get unlimited (or configured) bandwidth for the first N seconds (`--burst` / `Burst-Seconds`). Default 0 (disabled), bandwidth configurable (`--burst-bandwidth` / `Burst-Bandwidth`).
- **Process config** (`process.toml`): regex-based per-process weight/limit/smoothing overrides, applied on MSG_TYPE_CONF.
- **Schedule config** (`schedules.toml`): time-based bandwidth limit overrides, re-evaluated every 5 s. First-match-wins, collision warnings (suppressible via `--suppress-warnings`).
- **flock-based singleton lock**: refuses to start if another daemon holds the lock file (`<socket-path>.lock`).
- **JSON control socket** (`<socket-path>.ctl`): `stats`, `top`, and `user` queries for `trickle stats` and `trickle top`.
- **Foreground by default**: daemonize only with `-D`/`--daemonize`.
- Crash detection via Unix-socket EOF (libevent triggers unregister on disconnect).

## 8. CLI responsibilities

- **argparse**: `-m` mode, `-d`/`-u` bandwidth rates with required units (K/M/G/bps/B), `-w` window, `-W` weight, `-t`/`-l` smoothing, `-n` socket, `--schedules`, `--process-config`, `--suppress-warnings`, `-V`
- **Bandwidth parser**: `parse_bandwidth("8M")` → bytes/s, `"unlimited"` alias; bare numbers are rejected
- **User config**: `~/.config/ntrickle/ntrickle.toml` (`[daemon]` → mode, socket)
- **Process config evaluation**: matches `argv0` against `process.toml` patterns, exports weight/limits as env vars if not explicitly overridden by CLI flags
- **Schedule config evaluation**: evaluates `schedules.toml` against current time, exports matching limits as env vars
- **Subcommands**: `top` (ncurses dashboard), `stats` (JSON query), `config validate` (validate all config files), `doctor` (diagnostics)
- **LD_PRELOAD merge**, preload resolution, execvp
- Sets 8 `NTRICKLE_*` env vars: DOWNLOAD_LIMIT, UPLOAD_LIMIT, WINDOW_SIZE, ARGV, SOCKNAME, TSMOOTH, LSMOOTH, WEIGHT. (`NTRICKLE_VERBOSE` is read by the preload if already set, but the launcher does not export it.)
- **TUI**: `NTRICKLE_TUI_ASCII=1` forces ASCII glyphs instead of Unicode blocks/Braille.

## 9. Config files

| File | System default | User default | Override | Consumed by |
|---|---|---|---|---|
| `ntrickled.toml` | `/etc/ntrickle/ntrickled.toml` | N/A | `-c` / `NTRICKLE_DAEMON_CONFIG` | daemon |
| `ntrickle.toml` | N/A | `~/.config/ntrickle/ntrickle.toml` | auto | CLI |
| `process.toml` | `/etc/ntrickle/process.toml` | `~/.config/ntrickle/process.toml` | `--process-config` / `NTRICKLE_PROCESS_CONFIG` | daemon + CLI |
| `schedules.toml` | `/etc/ntrickle/schedules.toml` | `~/.config/ntrickle/schedules.toml` | `--schedules` / `NTRICKLE_SCHEDULES_CONFIG` | daemon + CLI |

Precedence: CLI flag > env var > user (~/.config) > system (/etc). Daemon skips user (~/.config).

## 10. Testing

18 tests: 7 smoke + 1 install + 1 integration + 9 unit. Run:
```bash
cmake -S . -B build -DNTRICKLE_ENABLE_TESTS=ON -DNTRICKLE_ENABLE_INTEGRATION_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

| Test | Type | What it checks |
|---|---|---|
| `message_codec_unit` | unit | Protocol v3 wire roundtrip (C++ ↔ C), all 3 message types, 64-bit limits, weight field |
| `bandwidth_unit` | unit | `parse_bandwidth()` and `format_bandwidth()` |
| `scheduler_unit` | unit | 8 deterministic bwstat fairness tests, including corrupt/invalid smoothing-value caps |
| `readiness_math_unit` | unit | select/poll timeout arithmetic, negative elapsed deltas, and saturation |
| `shared_config_unit` | unit | process.toml and schedules.toml parsing, regex matching, schedule time evaluation, effective schedule limits |
| `daemon_context_unit` | unit | UnixListener ownership, idempotent release/destructor, cleanup_list pattern |
| `stats_collector_unit` | unit | Cached per-session grants across burst, dynamic-idle, and fair-share paths (wire, snapshot, JSON) |
| `tui_text_unit` | unit | Command-string sanitization for the TUI table |
| `tui_glyphs_unit` | unit | Terminal locale/color detection and ASCII glyph fallback |
| `trickle_version/help` | smoke | CLI flag behaviour |
| `trickled_version/help` | smoke | Daemon flag behaviour |
| `trickle_missing_command` | smoke | Error on missing command |
| `preload_load_true` | smoke | Preload loads without crashing |
| `trickle_standalone_wrap_true` | smoke | Standalone mode wrap test |
| `install_smoke` | install | `make install` builds from source and verifies installed binaries |
| `pytest_integration` | integration | Full-stack: curl, wget, Python requests/urllib3, daemon lifecycle, CLI/config precedence, IPv6, sendfile accounting |

## 11. Out-of-scope

Kernel enforcement, admin GUI, anti-cheat security model, statically-linked program support, non-Linux platforms, runtime config reload (restart the daemon instead).
