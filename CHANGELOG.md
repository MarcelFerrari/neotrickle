# Changelog

## 2.0.0 (unreleased)

Packaging alignment with Debian's `trickle` package. The command-line interface
remains the modern neotrickle interface; classic trickle flags and syntax are
deliberately not emulated.

- **Installed names**: the canonical commands are now `trickle` (CLI) and
  `trickled` (daemon), installed as real binaries. `ntrickle` and `ntrickled`
  remain available as symlink aliases (`-DNTRICKLE_INSTALL_ALIASES=OFF` disables
  them). Diagnostics, help, version output, and TUI titles follow the invoked
  name.
- **Version**: bumped to `2.0.0` so the package sorts above Debian's
  `1.08+ds-2`.
- **Classic-flag guardrails**: `trickle -s` is rejected with a pointer to
  `-m standalone`; `trickled -s` and `trickled -f` are rejected because logging
  goes to stderr/journal and the daemon already runs in the foreground.
- **Bandwidth units required**: `-d`/`-u` now require an explicit unit (`10K`,
  `1M`, `1000B`, `100Mbps`, `unlimited`); bare numbers are rejected. Config
  files use the same parser.
- **`--suppress-warnings`**: now long-only; the short `-s` spelling is no longer
  accepted.
- **Burst disabled by default**: `Burst-Seconds` defaults to `0` so limiting
  applies from the first byte. Operators can opt in with `--burst`.
- **Readiness hardening**: the preload uses a monotonic clock for rate and
  timeout accounting, never extends a finite `select`/`poll` timeout, bounds a
  single token-bucket delay at 5 seconds, and locks statistics-queue insertion
  in `socket()`.
- **CI hardening**: jobs and dependency installs have explicit timeouts, apt
  installs prefer IPv4 and retry once, and CTest applies a per-test timeout.
- **Documentation**: added `trickle(1)` and `trickled(8)` man pages,
  [docs/compatibility.md](docs/compatibility.md) (upstream/package identity and
  deliberate non-compatibility) and
  [docs/migration-classic.md](docs/migration-classic.md) (classic → modern
  command and config mapping).
- **Tests**: smoke and integration tests use the canonical names and cover the
  classic-flag guardrails, unit requirements, alias installation, the disabled
  burst default, timeout arithmetic, corrupt smoothing values, and repeated
  64K downloads.

## 0.1.0

Initial public release.

- **ntrickle**: C++ CLI launcher with argument parsing, user config, process/schedule config evaluation, `LD_PRELOAD` setup, and `execvp`.
- **ntrickled**: C++ coordination daemon with libevent event loop, TOML config, two-tier weight-proportional scheduler, per-user directional caps, activity-based dynamic fairness, burst mode, schedule evaluation, flock-based singleton locking, and JSON control socket.
- **libntrickle_preload.so**: C-only LD_PRELOAD library with token-bucket bandwidth limiting, grant-based protocol (v3, 64-bit limits), 8-module architecture.
- **ntrickle top**: Interactive ncurses terminal dashboard with Braille history graphs, gauges, sortable session table, group-by-user mode, and pause/resume.
- **ntrickle stats**: JSON stats subcommand for querying the daemon.
- **ntrickle config validate**: Config file validation subcommand.
- **ntrickle doctor**: Diagnostics subcommand.
- Smoke tests, unit tests (message codec, bandwidth parser, scheduler, shared config), and integration tests (curl, wget, Python requests/urllib3, daemon lifecycle).
- Documentation: README, AGENTS.md, PROVENANCE.md, LICENSING.md, architecture, protocol, compatibility, and limitations docs.
