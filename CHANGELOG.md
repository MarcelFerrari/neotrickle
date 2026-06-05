# Changelog

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
