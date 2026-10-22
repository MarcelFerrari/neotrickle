# Migrating from classic trickle

The Debian package keeps the `trickle` and `trickled` command names, but it
runs the modern ntrickle implementation. The classic command-line interface,
configuration format, and wire protocol are not emulated.

## Command-line changes

Classic bandwidth values were bare numbers in KB/s. Modern values require an
explicit unit:

| Classic | Modern |
|---|---|
| `trickle -d 100 command` | `trickle -d 100K command` |
| `trickle -u 50 command` | `trickle -u 50K command` |
| `trickle -s -d 100 command` | `trickle -m standalone -d 100K command` |
| `trickled -f` | `trickled` (foreground is the default) |
| `trickled -s` | no flag; logs go to stderr/journald |
| `trickled -d 100 -u 50` | `trickled -d 100K -u 50K` |

Use `unlimited` or `0` for no limit. Bare numbers are rejected instead of
being interpreted as bytes.

Other classic options are not supported:

- `trickle -v` (verbose) is not implemented.
- Inline `rate:schedule` expressions; use `schedules.toml`.
- `trickle -P` and `trickle -L`.
- `trickled -p` (priority); use weights in the daemon/process configuration.

## Configuration changes

Classic `/etc/trickled.conf` sections can be translated into
`/etc/ntrickle/process.toml`:

```toml
# Classic
[ssh]
Priority = 1
Time-Smoothing = 0.1
Length-Smoothing = 2

# Modern equivalent
[process."ssh.*"]
weight = 1
time-smoothing = 0.1
length-smoothing = 2
```

Classic global limits on the daemon command line map to `trickled -d/-u`; the
modern daemon configuration lives in `/etc/ntrickle/ntrickled.toml`. Classic
inline schedules map to `/etc/ntrickle/schedules.toml`.

## Daemon and control changes

- The daemon socket moved from `/tmp/.trickled.sock` to
  `/run/ntrickle/ntrickled.sock`. The control socket is
  `/run/ntrickle/ntrickled.sock.ctl`.
- The grant-based protocol is not compatible with classic clients or tools.
  Upgrade `trickle` and `trickled` together.
- `tricklectl` is not provided. Use `trickle stats` or `trickle top`.
- `TRICKLE_*` environment variables and `trickle-overload.so` are internal
  classic interfaces. Use the `trickle` command and its options instead of
  setting preload variables directly.

## Defaults

The modern defaults are intentionally not classic's 10 KB/s:

- `trickle`: 1 MiB/s download and upload in standalone mode; daemon grants
  apply when `trickled` is running.
- `trickled`: 100 MiB/s aggregate, weight 1, static fairness.
- Burst mode is disabled by default; enable it with `--burst`.

## Debian upgrades

On upgrade from the classic `trickle` package, existing `/etc/trickled.conf`
content is not read. Translate it as shown above or remove it according to the
Debian package's `NEWS`/conffile handling. Scripts using classic flags or bare
rates must be updated before the new package is deployed.
