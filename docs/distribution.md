# Distribution and installation contract

This document defines how `neotrickle` is built, installed, discovered, and
upgraded. It is the reference for the packaging work; it does not claim that a
distribution channel is live.

## Release status

The source tree is version `2.0.0`, but version `2.0.0` is **unreleased**:
there is no `v2.0.0` tag or GitHub release. No Debian package has been
uploaded. The intended Debian arrangement is `src:neotrickle`, `bin:trickle`.
A downloadable `.deb` and a live apt repository are separate milestones.

## Supported platform baseline

| Item | Initial support |
|---|---|
| Kernel / libc | Linux, glibc |
| Architectures | x86_64 and aarch64 |
| Portable/wheel libc baseline | `manylinux_2_28` (glibc 2.28) |
| Python (wheel/pipx/uv) | Python 3.9 or newer |
| Native build | Linux with CMake 3.20+, libevent, ncursesw, a C11/C++17 toolchain |

The Python package has no Python ABI dependency: one `py3-none-manylinux_2_28_<arch>`
wheel is built per architecture, not one wheel per Python minor version. The
same wheel is usable by every supported Python version.

x86_64 is the primary validation architecture. aarch64 is built and advertised
only after the same wheel, portable, and native-package installation and
runtime checks pass on that architecture.

Out of initial scope: macOS, Windows, Alpine/musl, 32-bit systems, Conda, Snap,
Flatpak, and RPM packaging. Other glibc distributions can use the wheel or
portable archive.

## Installation layouts

### System profile (default)

Used by distro packages and `cmake --install`:

```
/usr/bin/trickle
/usr/bin/trickled
/usr/bin/ntrickle -> trickle        (optional alias)
/usr/bin/ntrickled -> trickled      (optional alias)
/usr/lib/<libdir>/ntrickle/libntrickle_preload.so
/usr/share/man/man1/trickle.1
/usr/share/man/man8/trickled.8
/usr/share/licenses/ntrickle/...
/etc/ntrickle/*.example.toml        (examples)
/usr/lib/systemd/system/ntrickled.service (optional)
```

Configuration and runtime state use `/etc/ntrickle`, `/run/ntrickle`, and
`/var/lib/ntrickle` as documented in `daemon.md`.

### Portable profile

A relocatable, rootless tree intended for tarballs and containers:

```
prefix/bin/trickle
prefix/bin/trickled
prefix/bin/ntrickle -> trickle       (optional)
prefix/bin/ntrickled -> trickled     (optional)
prefix/lib/ntrickle/libntrickle_preload.so
prefix/share/neotrickle/examples/*.toml
prefix/share/man/...
prefix/share/licenses/ntrickle/...
```

Select it with:

```bash
cmake -S . -B build -DNTRICKLE_INSTALL_PROFILE=portable
cmake --build build
cmake --install build --prefix /opt/neotrickle
```

The portable profile does not install systemd units and does not write under
`/etc`, `/run`, or `/var/lib`.

Relevant CMake controls:

| Option | Values | Purpose |
|---|---|---|
| `NTRICKLE_INSTALL_PROFILE` | `system`, `portable`, `wheel` | Selects paths and relocatable discovery defaults |
| `NTRICKLE_INSTALL_ALIASES` | `ON`, `OFF` | Installs `ntrickle`/`ntrickled` symlinks |
| `NTRICKLE_INSTALL_SYSTEMD_UNIT` | `ON`, `OFF` | System-profile only; rejected for portable/wheel |
| `NTRICKLE_PRELOAD_LIBRARY_PATH` | path or empty | Compiled fallback; empty for relocatable profiles |
| `NTRICKLE_EXAMPLE_INSTALL_DIR` | path | Where example TOML files are installed |
| `CMAKE_INSTALL_BINDIR`, `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_SYSCONFDIR` | relative paths | Standard GNU installation directory overrides |

`DESTDIR` staging and install-time `--prefix` overrides are supported by the
install rules.

### Wheel profile

The wheel uses the same relocatable layout inside its Python package. Native
binaries and the preload library are package data; Python entry points locate
them relative to the installed package and `exec` them. The wheel does not
install system services or modify system configuration.

## Executable and preload discovery

The launcher resolves the matching preload library in this order:

1. `NTRICKLE_PRELOAD_PATH`, when explicitly set. An invalid explicit path is a
   hard error; it never silently falls back.
2. Package-relative paths derived from the real executable (`/proc/self/exe`),
   including `../lib/ntrickle/libntrickle_preload.so` and
   `../lib/libntrickle_preload.so`.
3. The compiled system install path, when the system profile is used.

The current working directory and `LD_LIBRARY_PATH` are **not** searched. This
prevents an unrelated `libntrickle_preload.so` in a download directory or a
user library path from overriding the packaged library. Development builds
under `build/bin` work because the build-tree preload is at
`build/lib/libntrickle_preload.so`, which is found relative to the executable.

Relocatable profiles give the executables an install RPATH of
`$ORIGIN/../<libdir>/ntrickle`. Bundled runtime dependencies, when present, live
there and are never exported globally; wrapped applications do not inherit an
`LD_LIBRARY_PATH` pointing at ntrickle's private libraries.

Run `trickle doctor` to see the executable, install profile, selected preload
library and its source, and the daemon/control socket paths.

## Configuration precedence

For the launcher and daemon:

```
CLI flag > environment variable > user configuration > system default
```

User configuration is under `$XDG_CONFIG_HOME/ntrickle` (default
`~/.config/ntrickle`). System configuration is under `/etc/ntrickle`. The
daemon deliberately does not read the user configuration when run as a system
service.

## Daemon modes

- **System daemon**: `trickled`, normally managed by the systemd unit. The
  default socket is `/run/ntrickle/ntrickled.sock`; it is shared by local users
  and clients are identified with `SO_PEERCRED`.
- **User daemon**: `trickled --user`. It uses
  `$XDG_RUNTIME_DIR/ntrickle/ntrickled.sock` (falling back to
  `~/.run/ntrickle/ntrickled.sock`) and `$XDG_CONFIG_HOME/ntrickle` for
  configuration. The runtime directory is mode 0700 and the data/control
  sockets are mode 0600. Explicit `-n`/`--socket` and `-c`/`--config` always
  override these defaults.
- **Standalone**: `trickle -m standalone -- COMMAND` requires no daemon, does
  not write to `/etc`, `/run`, or `/var/lib`, and is the default rootless
  "just works" path.

The launcher picks an explicit socket, then the user-configured socket, then a
reachable system socket, then a reachable user socket, and finally the compiled
system default. A missing system socket does not silently redirect an existing
system-daemon deployment.

## Dependencies

Native/system packages use distribution-provided libevent and ncursesw and
declare them as runtime dependencies. The preload itself depends only on libc,
libdl, and pthreads; it must stay compatible with the host process's libc.

Portable archives and wheels must ship their private libevent/ncurses
dependencies under `lib/ntrickle` with the matching licenses, or link them
statically where licensing and relocatability allow. They must not require
development headers, a compiler, root, or Python at runtime.

Unsupported platforms and architectures fail during configuration with a
clear message. Native source builds remain possible on other glibc Linux
systems when the profile is `system`, but they are not advertised as supported
until tested.

## Debian/Ubuntu matrix

Planned initial targets, advertised only after installation and runtime tests:

| Distribution | Suite | Architectures |
|---|---|---|
| Debian 12 | bookworm | amd64, arm64 |
| Debian 13 | trixie | amd64, arm64 |
| Ubuntu 22.04 | jammy | amd64, arm64 |
| Ubuntu 24.04 | noble | amd64, arm64 |

The `.deb` is named `trickle`, while the source package remains `neotrickle`.
Version `2.0.0-1` sorts above classic `1.08+ds-2` without an epoch. Debian
acceptance and archive upload are maintainer decisions; this repository only
prepares the materials.

## Upgrade and removal

- User-created files under `/etc/ntrickle` are never overwritten by package
  examples; examples are conffile-free documentation unless a distribution
  package explicitly promotes one to a conffile.
- Removing or purging the package does not remove user-created configuration.
- The systemd unit does not pre-delete socket or lock files. The daemon acquires
  its singleton flock first, then removes only demonstrably stale Unix sockets
  left by a crash. A live daemon's socket is never removed.
- Lock files are not unlinked on exit; this avoids split locks between two
  inodes.
- Familiar `trickle`/`trickled` command names do **not** imply classic CLI,
  configuration, or wire-protocol compatibility. See
  [migration-classic.md](migration-classic.md).

## Release artifacts

Every release is built from one tagged source tree and one version. Artifacts
are:

- source archive (`neotrickle-<version>-Source.tar.gz`);
- native `.deb` packages for the tested Debian/Ubuntu matrix;
- `py3-none-manylinux_2_28_<arch>` wheels and an sdist;
- portable `<arch>` archives.

A manifest records the commit, version, architecture, supported platform
baseline, and SHA-256 checksums. Publication uses the tested artifacts; it does
not rebuild them.
