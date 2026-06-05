# Project identity and compatibility

## Names

`ntrickle` is the project name and command name. It is a cooperative bandwidth limiter for Linux that builds on the approach pioneered by classic `trickle`.

The runtime components are:

- `ntrickle`: command launcher and administration CLI.
- `ntrickled`: coordination daemon.
- `libntrickle_preload.so`: `LD_PRELOAD` interposition library.

The environment-variable prefix is `NTRICKLE_*`. Classic `TRICKLE_*` variables are not part of the public ntrickle interface.

Default runtime paths:

- Daemon config: `/etc/ntrickle/ntrickled.toml`
- Daemon socket: `/run/ntrickle/ntrickled.sock`
- Usage database: `/var/lib/ntrickle/usage.db`

## Classic trickle compatibility

Classic `trickle` is a reference and provenance source, not the public identity of this project. `ntrickle` does not silently replace or conflict with an existing classic trickle installation.

`ntrickle` does not install or expose these classic aliases:

- `trickle`
- `trickled`
- `tricklectl`
- `trickle-overload.so`

Compatibility behavior from classic trickle is exposed through ntrickle names. There is no legacy alias mechanism.

## Project history

`ntrickle` builds on [classic trickle](https://github.com/mariusae/trickle), the cooperative userspace bandwidth limiter originally developed by Marius Aamodt Eriksen. It preserves the `LD_PRELOAD` interposition approach and adds a grant-based protocol, two-tier weight-proportional scheduler, TOML configuration, per-user caps, time-based schedules, a TUI dashboard, and a JSON control API.

## Provenance

Code copied from or substantially derived from classic trickle keeps its upstream copyright headers. See [PROVENANCE.md](../PROVENANCE.md) and the root `LICENSE` file for licensing and source attribution.
