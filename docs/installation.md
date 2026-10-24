# Installing neotrickle

`neotrickle` is one native implementation. The Python package, portable
archive, and Debian package all install the same `trickle` and `trickled`
commands. Choose one installation method per machine unless you deliberately
want to test a second build.

> **Release status:** version 2.0.0 is not yet tagged or published. The commands
> below show the intended interfaces; only source builds and locally built
> artifacts are available until the release and channel setup are complete.
> No PyPI or apt channel is live yet.

## Supported platforms

| Platform | Support |
|---|---|
| Linux/glibc x86_64 | Supported and validated |
| Linux/glibc aarch64 | Target architecture; validate the published artifact before relying on it |
| Minimum glibc | 2.28 baseline for wheels and portable archives |
| Python | 3.9 or newer for pip/pipx/uv |
| Debian/Ubuntu | Debian 12/13 and Ubuntu 22.04/24.04 packages are built per release |

macOS, Windows, Alpine/musl, 32-bit systems, Conda, Snap, Flatpak, and RPM
packaging are not supported initially.

## Choose a method

| Method | Best for | Status |
|---|---|---|
| `pipx install neotrickle` | Individual CLI users | After PyPI publication |
| `uv tool install neotrickle` | Individual CLI users who use uv | After PyPI publication |
| `pip install neotrickle` in a venv | Python/ML workflows | After PyPI publication |
| Debian/Ubuntu `.deb` / apt | Administrators | Downloadable `.deb`; apt hosting is maintainer-configured |
| Portable archive | Containers, restricted servers, no Python/root | Built per release |
| Source build | Distribution maintainers and contributors | Available now |

## pipx (recommended for CLI use)

```bash
pipx install neotrickle
trickle -V
trickled -V
```

pipx creates an isolated environment per application and exposes `trickle`,
`trickled`, `ntrickle`, and `ntrickled`. Upgrade or remove with:

```bash
pipx upgrade neotrickle
pipx uninstall neotrickle
```

## uv tool

```bash
uv tool install neotrickle
trickle -V
uv tool upgrade neotrickle
uv tool uninstall neotrickle
```

## pip in a virtual environment (Python/ML users)

Do not install with `sudo pip` or bypass externally-managed Python protections.
Create a virtual environment for the project that needs the wrapper:

```bash
python3 -m venv ~/.venvs/ntrickle
. ~/.venvs/ntrickle/bin/activate
python -m pip install neotrickle
trickle -V
```

Because the wheel has no Python ABI dependency, the same architecture-specific
wheel works for every supported Python version. Upgrade with
`python -m pip install --upgrade neotrickle`, and uninstall with
`python -m pip uninstall neotrickle`.

## Debian and Ubuntu

A release provides `trickle_<version>-1_<arch>.deb` packages, built and tested
per distribution. Install the downloaded package explicitly:

```bash
sudo apt install ./trickle_2.0.0-1_amd64.deb
trickle -V
trickle doctor
```

The binary package is named `trickle`; the source package is `neotrickle`.
Classic `trickle` scripts are not compatible; see
[migration-classic.md](migration-classic.md).

If a repository is configured later, it uses a scoped signing key:

```bash
sudo install -d -m 0755 /usr/share/keyrings
sudo cp neotrickle-archive-keyring.gpg /usr/share/keyrings/
# Replace the placeholder host with the maintainer-configured repository host.
echo 'deb [signed-by=/usr/share/keyrings/neotrickle-archive-keyring.gpg] https://<apt-host>/neotrickle/debian bookworm main' |
  sudo tee /etc/apt/sources.list.d/neotrickle.list
sudo apt update
sudo apt install trickle
```

The service unit is installed but not enabled or started. Review the
configuration and enable it explicitly:

```bash
sudo systemctl enable --now ntrickled.service
```

Remove the package with `sudo apt remove trickle`. Purge with
`sudo apt purge trickle`; user-created files under `/etc/ntrickle` are not
package-owned and remain.

## Portable archive

The portable archive needs no Python, root, compiler, or development headers.
It contains its own `libevent`/`ncurses` libraries and uses a private RPATH:

```bash
tar -xzf neotrickle-2.0.0-linux-x86_64.tar.gz
./neotrickle-2.0.0-linux-x86_64/bin/trickle -V
./neotrickle-2.0.0-linux-x86_64/bin/trickle doctor
```

Add the archive's `bin` directory to `PATH`, or symlink `trickle` and
`trickled` into a directory already on `PATH`. Delete the extracted directory
to uninstall. The archive is architecture-specific; do not copy an x86_64
archive to an aarch64 machine.

## Source build

For a system installation:

```bash
git clone https://github.com/MarcelFerrari/neotrickle.git
cd neotrickle
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

For a rootless, relocatable installation without systemd or `/etc` writes:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNTRICKLE_INSTALL_PROFILE=portable
cmake --build build -j"$(nproc)"
cmake --install build --prefix "$HOME/.local/opt/neotrickle"
```

See [distribution.md](distribution.md) for the full installation contract.

## Verify the installation

```bash
trickle -V
trickle doctor
```

`doctor` prints the executable, install profile, selected preload library and
its source, and the daemon/control socket endpoints. A daemon socket failure is
normal when no daemon is running; a preload failure is not.

Run a throttled standalone transfer:

```bash
trickle -m standalone -d 10M -- curl -O https://example.com/dataset.tar.gz
```

Run a private per-user daemon (no root and no system service):

```bash
trickled --user -d 100M -u 20M &
trickle -- wget https://example.com/dataset.tar.gz
trickle stats
```

The user daemon uses `$XDG_CONFIG_HOME/ntrickle` and
`$XDG_RUNTIME_DIR/ntrickle` (fallback `~/.run/ntrickle`), with mode 0700 for
the directory and 0600 for its sockets. Stop it with `kill %1` or `SIGTERM`.

## Upgrade and uninstall

| Method | Upgrade | Uninstall |
|---|---|---|
| pipx | `pipx upgrade neotrickle` | `pipx uninstall neotrickle` |
| uv tool | `uv tool upgrade neotrickle` | `uv tool uninstall neotrickle` |
| pip venv | `pip install --upgrade neotrickle` | `pip uninstall neotrickle` |
| Debian/Ubuntu | `apt install ./trickle_<new>.deb` | `apt remove trickle` |
| portable | extract the new archive | remove the directory |
| source | rebuild and install | remove the install prefix |

## PATH precedence when several installs coexist

Native packages install `/usr/bin/trickle`; pipx and uv install into a user
bin directory such as `~/.local/bin`; a venv installs into
`<venv>/bin/trickle`. The first matching directory on `PATH` wins.

```bash
type -a trickle
command -v trickle
```

Prefer one installation method. If you need to keep more than one for testing,
invoke the intended one by absolute path and use `trickle doctor` to confirm
which preload library it selected.

## Troubleshooting

- **`preload library found ... FAIL`**: set `NTRICKLE_PRELOAD_PATH` to the
  packaged `libntrickle_preload.so`, or reinstall the artifact so the library
  sits beside `bin/` under `lib/ntrickle` or `lib64/ntrickle`.
- **`Could not reach ntrickled, working independently`**: start `trickled`
  (system service or `--user`) or use `-m standalone` deliberately.
- **Permission denied opening `/run/ntrickle/...`**: use `trickled --user` or
  an explicit writable `-n` socket; do not make the system runtime directory
  world-writable.
- **`externally-managed-environment` from pip**: use a venv, pipx, or uv.
- **`GLIBC_... not found` or a wheel refuses to install**: use the artifact for
  your architecture and glibc baseline, or build from source.
- **A native `trickle` shadows a pipx install**: check `type -a trickle` and
  reorder `PATH`, or remove the unused installation.

Familiar command names do **not** imply classic CLI, configuration, or protocol
compatibility. See [migration-classic.md](migration-classic.md) before replacing
a classic installation.
