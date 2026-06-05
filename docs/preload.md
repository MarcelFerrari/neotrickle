# Preload

`libntrickle_preload.so` is the `LD_PRELOAD` interposition library. It is injected into target processes by the `ntrickle` launcher and enforces bandwidth limits at the libc socket I/O level.

## Architecture

The preload is organized into 8 C modules:

| Module | Role |
|---|---|
| `trickle-overload.c` | Bootstrap: symbol resolution (`dlsym`), environment parsing, diagnostics |
| `state.h` / `state.c` | Global state, descriptor table (`socket`/`close`/`dup`/`dup2`/`accept`), locking |
| `limiter.h` / `limiter.c` | Hot path: `delay`, `getdelay`, `update`, `updatesd` |
| `wrappers.c` | I/O wrappers: `read`/`readv`/`recv`/`recvfrom`, `write`/`writev`/`send`/`sendto`/`sendfile` |
| `readiness.c` | `select` and `poll` wrappers |
| `ntrickledu.h` / `ntrickledu.c` | Daemon client: connect, receive grants, periodic poll, fire-and-forget updates |
| `codec.h` / `codec.c` | Manual wire-format serialization (`htonl`/`memcpy`, no librpc) |
| `atomicio.c` | Resilient read/write loop |

## Environment variables

The `ntrickle` CLI sets these variables before `execvp`:

| Variable | Description |
|---|---|
| `NTRICKLE_DOWNLOAD_LIMIT` | Download rate limit (bytes/sec) |
| `NTRICKLE_UPLOAD_LIMIT` | Upload rate limit (bytes/sec) |
| `NTRICKLE_WINDOW_SIZE` | Token-bucket window size (KB) |
| `NTRICKLE_ARGV` | Original command name for daemon registration |
| `NTRICKLE_SOCKNAME` | Daemon Unix socket path (empty for standalone mode) |
| `NTRICKLE_TSMOOTH` | Time smoothing parameter (seconds) |
| `NTRICKLE_LSMOOTH` | Length smoothing parameter (KB) |
| `NTRICKLE_WEIGHT` | Bandwidth share weight |
| `NTRICKLE_VERBOSE` | Verbosity level |

## Design constraints

- **C-only**. No C++, no STL, no dynamic allocation in hot paths.
- **No recursion**. Logging and I/O paths must avoid calling back into intercepted functions.
- **errno preservation**. Wrapper functions preserve `errno` so callers see the same errors they would without preloading.
- **Thread safety**. Descriptor table access uses a per-process mutex.
- **Minimal dependencies**. Only libc, libdl, and pthreads.

## Provenance

The preload's interposition and token-bucket core is derived from classic trickle. The daemon client (`ntrickledu.c`), codec (`codec.c`), and the 8-module structure are ntrickle additions. See [PROVENANCE.md](../PROVENANCE.md).
