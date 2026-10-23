# Project identity and compatibility

## Names

`ntrickle` is the upstream project and repository name. It is a cooperative
bandwidth limiter for Linux that builds on the approach pioneered by classic
`trickle`.

The installed commands are:

- `trickle`: command launcher and administration CLI.
- `trickled`: coordination daemon.

The optional aliases `ntrickle` and `ntrickled` point at the same binaries.
They are installed by default (`-DNTRICKLE_INSTALL_ALIASES=ON`) and can be
disabled for packaging that wants only the canonical names.

Internal names are intentionally unchanged: the preload library is
`libntrickle_preload.so`, the environment-variable prefix is `NTRICKLE_*`, and
runtime paths stay under `/etc/ntrickle` and `/run/ntrickle`.

Default runtime paths:

- Daemon config: `/etc/ntrickle/ntrickled.toml`
- Daemon socket: `/run/ntrickle/ntrickled.sock`
- Usage database: `/var/lib/ntrickle/usage.db`

## Release and Debian status

The intended Debian arrangement is `src:neotrickle` with `bin:trickle`.
Upstream version `2.0.0` (Debian revision `2.0.0-1`) would sort above classic
`1.08+ds-2` without an epoch. The
installed binaries would retain the classic package's command names so an
upgrade delivers the maintained implementation.

This arrangement is prepared but not yet published: there is no `v2.0.0` tag
or GitHub release, and no Debian package has been uploaded. The upstream CPack
source archive is `neotrickle-2.0.0-Source.tar.gz`; `trickle` is the intended
Debian binary package name, not the upstream package or archive identity.

## Classic trickle compatibility

The classic command names are retained, but the classic command-line interface
is **not** emulated. `trickle` and `trickled` expose the modern ntrickle CLI,
configuration, and protocol. Classic scripts and tools require migration.

Deliberately unsupported classic surfaces:

- `trickle -s` for standalone mode. The modern equivalent is
  `trickle -m standalone -- COMMAND`. Invoking `-s` is rejected with guidance.
- Bare numeric rates interpreted as KB/s. Modern rates require an explicit
  unit (`10K`, `10M`, `100Mbps`, `100B`, or `unlimited`). Bare numbers are
  rejected rather than silently treated as bytes.
- Classic defaults (10 KB/s). Modern defaults are 1 MiB/s for the client and
  100 MiB/s for the daemon, with burst disabled.
- `trickled -f` (foreground is already the default), `trickled -s` (syslog),
  `trickled -p`, and inline `rate:schedule` expressions.
- `/etc/trickled.conf` and its `Priority`/`Time-Smoothing`/
  `Length-Smoothing` format. Use `ntrickled.toml`, `process.toml`, and
  `schedules.toml`.
- `/tmp/.trickled.sock` and the classic wire protocol. Use the modern socket
  path and grant protocol.
- `TRICKLE_*` environment variables, `trickle-overload.so`, and `libtrickle.so`.
  The internal `NTRICKLE_*` interface and `libntrickle_preload.so` are not
  public compatibility surfaces.
- `tricklectl`. The classic utility was an unfinished stub; use
  `trickle stats` or `trickle top` instead.

See [migration-classic.md](migration-classic.md) for the practical mapping and
example commands.

## Project history

`ntrickle` builds on [classic trickle](https://github.com/mariusae/trickle),
the cooperative userspace bandwidth limiter originally developed by Marius
Aamodt Eriksen. It preserves the `LD_PRELOAD` interposition approach and adds a
grant-based protocol, two-tier weight-proportional scheduler, TOML
configuration, per-user caps, time-based schedules, a TUI dashboard, and a JSON
control API.

## Provenance

Code copied from or substantially derived from classic trickle keeps its
upstream copyright headers. See [PROVENANCE.md](../PROVENANCE.md) and the root
`LICENSE` file for licensing and source attribution.
