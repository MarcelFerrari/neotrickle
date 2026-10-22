# Daemon

`ntrickled` is the coordination daemon for `ntrickle`. It divides global bandwidth among connected clients, applies per-user and per-process configuration, and serves a JSON control API for monitoring tools.

## Runtime paths

| Resource | Default path | Override |
|---|---|---|
| Config | `/etc/ntrickle/ntrickled.toml` | `-c` / `NTRICKLE_DAEMON_CONFIG` |
| Data socket | `/run/ntrickle/ntrickled.sock` | `-n` |
| Control socket | `<socket-path>.ctl` | (follows `-n`) |
| Lock file | `<socket-path>.lock` | (follows `-n`) |

## Scheduler

The daemon uses a two-tier weight-proportional scheduler:

- **Tier 1 (users)**: global bandwidth is divided among users by weight.
- **Tier 2 (processes)**: each user's share is subdivided among their processes by weight.

### Fairness modes

- **Static**: grants are recomputed only on client connect and disconnect.
- **Dynamic**: idle clients (no update within `Idle-Threshold` seconds) get a minimal 1 KB/s grant. Their bandwidth is redistributed among active clients every 5 seconds.

### Burst mode

Newly connected clients can get unlimited (or configured) bandwidth for a set number of seconds (`Burst-Seconds`, default 15).

### Per-user caps

The `[users.<name>]` section in `ntrickled.toml` can set per-user directional bandwidth ceilings (`download`, `upload`) and weight overrides.

## Configuration

See [config/ntrickled.example.toml](../config/ntrickled.example.toml) for a documented example with all supported options.

### Precedence

Explicit CLI flags take precedence over `[General]` values, which take
precedence over built-in defaults. The daemon configuration path is resolved
as `-c` > `NTRICKLE_DAEMON_CONFIG` > the compiled-in default.

## Process and schedule configs

- `process.toml`: regex-based per-process weight, limits, and smoothing overrides. Applied when a client sends its CONF message (matched by `argv0`).
- `schedules.toml`: time-based bandwidth limit overrides. Evaluated every 5 seconds. First match wins, with collision warnings for overlapping entries. Directions the active schedule does not set fall back to the daemon's base `-d`/`-u` limits.

## Control API

The daemon listens on a second Unix socket (`<data-socket>.ctl`) for JSON queries:

- `{"cmd":"stats"}`: full daemon snapshot (global limits, users, sessions).
- `{"cmd":"top"}`: compact user summary sorted by bandwidth.
- `{"cmd":"user","name":"<username>"}`: per-user detail with sessions.

Session rows report the last grant sent per direction (`grant_download_bps`, `grant_upload_bps`). During the burst window this can be `UINT64_MAX` for unlimited.

These are used by `ntrickle top` (TUI dashboard) and `ntrickle stats` (CLI query).

## Singleton locking

The daemon takes an exclusive `flock` on `<socket-path>.lock` at startup. If another daemon already holds the lock, the new instance exits with an error.

## Signals

- `SIGTERM` / `SIGINT`: clean shutdown (unlinks socket and lock file, runs cleanup handlers).

## Provenance

The daemon is derived from classic trickle's coordination code and has been extended with a new scheduler, protocol, configuration system, control API, and monitoring tools. See [PROVENANCE.md](../PROVENANCE.md).
