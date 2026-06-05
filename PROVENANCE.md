# Provenance

This document describes where each component in the `ntrickle` repository comes from.

## Licensing relationship

ntrickle as a combined modified work is distributed under GPL-3.0-or-later. Portions copied from or derived from classic trickle keep their original BSD copyright and license notices.

The GPLv3 text is in [`LICENSE`](LICENSE). Relevant upstream BSD license texts are in [`LICENSES/`](LICENSES/), and copyright attribution is summarized in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Original ntrickle code

The following files were written for `ntrickle` and are original works:

### src/common/

- `bandwidth.hpp`: Bandwidth value parsing and formatting
- `daemon_mode.hpp`: Daemon mode enum and parsing
- `shared_config.cpp`: TOML config parsers (process.toml, schedules.toml)
- `shared_config.hpp`: Shared config data structures

### src/ntrickle/ (launcher)

- `main.cpp`: CLI argument parsing, preload setup, exec
- `user_config.cpp`: Per-user `~/.config/ntrickle/ntrickle.toml` reader
- `user_config.hpp`: User config data structure

### src/ntrickled/ (daemon)

- `ntrickled.cpp`: Daemon main: event loop, socket listen, signal handling
- `client.cpp`: Client connection and bandwidth grant computation
- `client.hpp`: Client, ClientManager classes
- `config.cpp`: TOML config reader for ntrickled
- `config.hpp`: Config reader class
- `daemon_context.cpp`: UnixListener, cleanup
- `daemon_context.hpp`: DaemonContext, Event, UnixListener, cleanup_list
- `logger.hpp`: Logging utility
- `message_codec.cpp`: Wire-format message encode/decode (replacement for xdr.c)
- `message_codec.hpp`: MessageCodec class

### src/preload/ (LD_PRELOAD interposer)

- `codec.c`: Manual XDR-compatible message codec (replacement for upstream xdr.c)
- `codec.h`: Codec header
- `limiter.c`: Bandwidth rate-limiting hot path
- `limiter.h`: Limiter header
- `state.c`: Descriptor table, global state, and locking
- `state.h`: State and descriptor table header
- `readiness.c`: select() and poll() wrappers
- `wrappers.c`: I/O interception wrappers (read/write/send/recv families)
- `ntrickledu.h`: Protocol v3 daemon client header

## Code copied from classic trickle (unmodified)

- `src/preload/atomicio.c`: Copied verbatim from classic trickle. Licensed under BSD-2-Clause by Marius Aamodt Eriksen and Theo de Raadt.

## Code derived from classic trickle (modified)

The following files originated in classic trickle and have been modified for `ntrickle`:

- `src/common/bwstat.c`: Copied from upstream with minor include adjustments. Keeps original BSD-3-Clause notice.
- `src/common/bwstat.h`: Copied from upstream with C++ `extern "C"` guards added. Keeps original BSD-3-Clause notice.
- `src/common/util.h`: Derived from upstream `util.h`, with the `get_progname()` declaration removed and C++ guards added. Keeps original BSD-3-Clause notice.
- `src/common/message.h`: Originally derived from upstream `message.h`. Rewritten for protocol v3 (grant-based instead of delay-based, 64-bit limits, weight field). Original BSD copyright kept; modifications licensed under GPL-3.0-or-later.
- `src/preload/trickle-overload.c`: Originally derived from upstream `trickle-overload.c`. Restructured: symbol resolution, bootstrap, and diagnostics remain, while delay/update logic moved to separate files. Original BSD copyright kept; modifications licensed under GPL-3.0-or-later.
- `src/preload/ntrickledu.c`: Derived from upstream `trickledu.c`. Rewritten for protocol v3 (grant-based, non-blocking poll). Original BSD copyright kept; modifications licensed under GPL-3.0-or-later.

## Vendored third-party code

- `third_party/argparse/`: Vendored argparse library (MIT).
- `third_party/tomlplusplus/`: Vendored toml++ library (MIT).

Neither vendored component has been modified from its upstream release.
