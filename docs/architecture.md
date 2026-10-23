# Architecture

`ntrickle` is an application-independent bandwidth limiter for Linux. It has three runtime components:

- `trickle`, the installed C++ CLI and command launcher (`ntrickle` alias).
- `trickled`, the installed coordination daemon (`ntrickled` alias).
- `libntrickle_preload.so`, the C preload library injected into wrapped processes with `LD_PRELOAD`.

Names, paths, environment variables, and compatibility policy are defined in [Project Identity and Compatibility](compatibility.md).

## CLI (`trickle`)

The launcher is a C++ binary that:

- Parses CLI arguments using argparse (`-d`, `-u`, `-W`, `-m`, etc.).
- Resolves daemon mode: standalone (no daemon), daemon (best-effort), or daemon-required.
- Reads user config from `~/.config/ntrickle/ntrickle.toml`.
- Evaluates process config (`process.toml`) and schedule config (`schedules.toml`).
- Finds the preload library via env var, CWD, `LD_LIBRARY_PATH`, or installed path.
- Merges the preload path into `LD_PRELOAD`.
- Sets `NTRICKLE_*` environment variables.
- `execvp`s the target command.

It also serves as an administration CLI with subcommands: `top` (TUI dashboard), `stats` (JSON query), `config validate` (config validation), `doctor` (diagnostics).

## Daemon (`trickled`)

The daemon is a C++ event-driven server built on libevent:

- Listens on a Unix domain socket for client connections.
- Verifies client identity via kernel-provided `SO_PEERCRED` (UID, GID, PID).
- Implements a two-tier weight-proportional scheduler:
  - **Tier 1**: divides global limits among users by weight.
  - **Tier 2**: subdivides each user's share among their processes by weight.
- Supports per-user directional caps (`[users.<name>]` in daemon config).
- Supports activity-based dynamic fairness (idle clients get a minimal grant, bandwidth is redistributed).
- Supports opt-in burst mode for new connections (disabled by default).
- Evaluates scheduled bandwidth limits every 5 seconds.
- Uses flock-based singleton locking (one daemon instance per socket).
- Exposes a JSON control socket for the `trickle top` and `trickle stats` subcommands.
- Runs in the foreground by default. Daemonize with `-D`.

## Preload library (`libntrickle_preload.so`)

The preload library is a C-only shared object injected via `LD_PRELOAD`:

- Intercepts libc socket I/O functions: `read`, `write`, `recv`, `send`, `readv`, `writev`, `recvfrom`, `sendto`, `sendfile`, `select`, `poll`, `accept`, `dup`, `dup2`, `close`.
- Communicates with the daemon over the Unix socket using the grant-based protocol.
- Enforces bandwidth limits locally with a per-socket token-bucket algorithm.
- Hot-path functions avoid allocation, recursion, and locking overhead.

## Protocol

The wire protocol (v3) is defined in [protocol.md](protocol.md). Three message types: MSG_TYPE_CONF (registration), MSG_TYPE_GRANT (rate limits), MSG_TYPE_UPDATE (byte counts). Wire framing: 1-byte version, 4-byte big-endian payload length, serialized struct.

## Provenance

Original and derived code provenance is documented in [PROVENANCE.md](../PROVENANCE.md).
