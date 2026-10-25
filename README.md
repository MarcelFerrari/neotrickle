<p align="center">
  <img src="https://raw.githubusercontent.com/MarcelFerrari/neotrickle-logo/main/neotrickle_logo.svg" alt="ntrickle logo" width="480">
</p>

<p align="center">
  <a href="https://github.com/MarcelFerrari/neotrickle/actions/workflows/ci.yml">
    <img src="https://github.com/MarcelFerrari/neotrickle/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI">
  </a>
  <a href="https://github.com/MarcelFerrari/neotrickle/releases/latest">
    <img src="https://img.shields.io/github/v/release/MarcelFerrari/neotrickle?sort=semver&display_name=tag" alt="Latest release">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="License: GPL-3.0-or-later">
  </a>
  <a href="docs/limitations.md">
    <img src="https://img.shields.io/badge/platform-Linux-lightgrey?logo=linux" alt="Platform: Linux">
  </a>
</p>

<br>

**Application-independent bandwidth control for Linux.** Stop model weights, datasets, and container images from saturating your connection, without changing the tools you already use.

```bash
trickle -d 10M -- curl -O https://huggingface.co/meta-llama/model.bin
```

## The problem

Downloading a large file on a shared machine shouldn't ruin everyone else's day. But every tool handles bandwidth differently. `curl --limit-rate`, `wget --limit-rate`, `pip` has nothing, Python scripts have nothing, and the flags that do exist use different syntax. One uncontrolled download saturates the link. SSH sessions lag, video calls stutter, and your teammates wonder what's going on.

`ntrickle` fixes this at the process level, for every tool. It installs as `trickle` and `trickled` (with `ntrickle`/`ntrickled` aliases) and exposes the modern ntrickle CLI, not the legacy trickle CLI.

## Install

Choose one method per machine. Version 2.0.0 is **not yet tagged or published**;
the PyPI and apt channels are not live yet. See
[Installing neotrickle](docs/installation.md) for all methods, upgrades,
uninstallation, PATH precedence, and troubleshooting.

| Method | Use for |
|---|---|
| `pipx install neotrickle` | Standalone CLI use (after PyPI publication) |
| `uv tool install neotrickle` | Standalone CLI use with uv (after publication) |
| `pip install neotrickle` in a venv | Python/ML project environments (after publication) |
| `trickle_<version>-1_<arch>.deb` | Debian/Ubuntu administrators; apt hosting is maintainer-configured |
| Portable `<arch>` archive | Containers and restricted servers without Python or root |
| Source build | Distribution maintainers, contributors, and current pre-release use |

Build from source now with:

```bash
git clone https://github.com/MarcelFerrari/neotrickle.git
cd neotrickle
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

Prerequisites: `build-essential cmake libevent-dev libncurses-dev` (or your
distro's equivalents).

For system, portable, and wheel layouts, supported platforms, dependency
handling, and rootless user-daemon operation, see
[Distribution and installation](docs/distribution.md).

## Quick start

**Run the daemon** to share bandwidth across users and processes:

```bash
# Start the daemon (once, as root or a dedicated user)
sudo trickled -d 500M -u 100M

# Users wrap their commands; the daemon divides bandwidth fairly
trickle -- python3 train.py --download-data
trickle -- wget https://example.com/dataset.tar.gz
trickle -- pip install torch
```

**Throttle a single download** without the daemon, no root needed:

```bash
# Pull a model from Hugging Face without saturating your connection
trickle -m standalone -d 10M -- wget https://huggingface.co/some-model/resolve/main/pytorch_model.bin

# Keep pip installs from disrupting other work on a shared machine
trickle -m standalone -d 20M -- pip install torch transformers
```

**Monitor live usage:**

```bash
trickle top
```

<p align="center">
  <img src="https://github.com/MarcelFerrari/neotrickle-logo/blob/main/ntrickle_tui.png" alt="ntrickle top dashboard" width="720">
  <br>
  <em>trickle top, the interactive TUI dashboard</em>
</p>

The dashboard uses Unicode block/Braille glyphs and color when the terminal locale is UTF-8 and supports at least eight colors. On older terminals it falls back to ASCII glyphs and text attributes; set `NTRICKLE_TUI_ASCII=1` to force ASCII output.

## Why ntrickle?

- **Works with everything.** Any dynamically linked program that uses standard libc I/O: curl, wget, Python, pip, git, git-lfs, container runtimes. No per-tool configuration.
- **Coordinated sharing.** The daemon divides a global bandwidth pool across users and processes with weight-based fairness, per-user caps, and burst mode for new connections.
- **Scheduled policies.** Higher limits overnight, tighter limits during the workday. `schedules.toml` handles it automatically.
- **No kernel modules, no root for clients.** Just `LD_PRELOAD` and a userspace daemon. Wrap your command with `trickle` (also installed as `ntrickle`) and go.
- **Built for ML workflows.** Designed on shared GPU machines where one person's model download shouldn't tank everyone's SSH session.

## How it works

The `trickle` command injects a small library via `LD_PRELOAD` that interposes on libc socket functions and enforces bandwidth limits with a token-bucket algorithm. In daemon mode, clients receive rate grants from `trickled` over a Unix socket. In standalone mode, limits are enforced locally.

[Architecture](docs/architecture.md) ·
[Protocol (v3)](docs/protocol.md) ·
[Preload internals](docs/preload.md)

## Documentation

| Document | Description |
|---|---|
| [ML and AI workflows](docs/ml-workflows.md) | Recipes for models, datasets, pip, containers, shared machines |
| [Installing neotrickle](docs/installation.md) | pip/pipx/uv, Debian, portable archives, upgrades, troubleshooting |
| [Distribution and installation contract](docs/distribution.md) | Install profiles, supported platforms, dependency and release contract |
| [Publishing](docs/publishing.md) | Maintainer release, PyPI, apt, and Debian handoff checklist |
| [Daemon setup](docs/daemon.md) | Configuration, scheduling, per-user caps, process overrides, user daemon |
| [Architecture](docs/architecture.md) | Component design and data flow |
| [Protocol](docs/protocol.md) | Wire protocol v3 specification |
| [Limitations](docs/limitations.md) | When ntrickle can't help |
| [Compatibility](docs/compatibility.md) | Naming policy and classic trickle relationship |
| [Provenance](PROVENANCE.md) | Code origin and attribution |
| [Contributing](CONTRIBUTING.md) | Build from source, run tests, submit changes |

## Limitations

`ntrickle` is a cooperative userspace tool, not a kernel enforcement mechanism. It needs dynamically linked programs that use libc socket functions. Statically linked binaries, programs that use direct syscalls, and non-Linux platforms are not supported. See [limitations.md](docs/limitations.md).

## History

`ntrickle` builds on [classic trickle](https://github.com/mariusae/trickle), the cooperative userspace bandwidth limiter originally written by Marius Aamodt Eriksen in the early 2000s. It keeps the `LD_PRELOAD` interposition approach and adds a grant-based protocol, weight-proportional scheduler, TOML configuration, per-user caps, time-based schedules, and a TUI dashboard.

## License

`ntrickle` is distributed under [GPL-3.0-or-later](LICENSE). Portions derived from classic trickle retain their original BSD notices. See [LICENSING.md](LICENSING.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
