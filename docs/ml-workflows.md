# ML and AI workflows

`ntrickle` is useful in ML environments where users routinely download large artifacts through many different tools. This page has practical examples.

The recommended setup is to run the daemon and let it manage bandwidth across users. The daemon handles all the rate decisions; users just wrap their commands.

## Shared machine setup

Run the daemon with a global limit and per-user weights:

```bash
# Start daemon with 500 MB/s global download, 100 MB/s global upload
sudo trickled -d 500M -u 100M
```

Configure per-user caps in `/etc/ntrickle/ntrickled.toml`:

```toml
[users.alice]
weight = 3
download = "200M"

[users.bob]
weight = 1
download = "50M"
```

Users then wrap their commands. No flags needed:

```bash
trickle -- python3 train.py --download-data
trickle -- wget https://example.com/checkpoint.pt
```

## Model and checkpoint downloads

```bash
# Download a model from Hugging Face
trickle -- wget https://huggingface.co/some-model/resolve/main/pytorch_model.bin

# Clone an LFS-heavy repository
trickle -- git clone https://huggingface.co/some-model
```

### Hugging Face CLI and Xet

The Hugging Face `hf` CLI is supported, including its Xet backend (`hf_xet`).
`hf_xet` performs network I/O through libc `read`/`recv`/`send`/`write`/`writev`,
which the preload intercepts, so no `HF_HUB_DISABLE_XET=1` workaround is needed.
Verified with `huggingface_hub` 2.2.0 against a Xet-backed model file.

## Dataset acquisition

```bash
# Download a dataset through a Python script
trickle -- python3 download_dataset.py

# Pull a dataset archive with curl
trickle -- curl -O https://example.com/dataset.tar.gz
```

## Python package installation

```bash
# Keep pip from disrupting interactive work on a shared machine
trickle -- pip install torch transformers

# Limit uploads too (useful when distributing packages)
trickle -- pip install --upgrade some-large-package
```

## Container image pulls

The `trickle` command must wrap the process that does the actual download. For container workflows, this is the container runtime daemon (e.g., `containerd`), not the client CLI (`docker`, `podman`):

```bash
# Throttle the container runtime's network I/O
trickle -- containerd
```

The CLI tools (`docker`, `podman`, `nerdctl`) send API requests over a local socket. Wrapping them with `trickle` throttles the control-plane communication, not the image download.

## Scheduled limits

Use `schedules.toml` to apply different limits during business hours:

```toml
[schedules.daytime]
start = "08:00"
end = "18:00"
days = ["Mon", "Tue", "Wed", "Thu", "Fri"]
download = "100M"
upload = "20M"

[schedules.nighttime]
start = "18:00"
end = "08:00"
download = "500M"
upload = "100M"
```

## Per-process overrides

Use `process.toml` to assign different weights or limits to specific tools:

```toml
[process."pip.*"]
weight = 2
download = "200M"

[process."python.*"]
weight = 2

[process."wget.*"]
weight = 1
download = "50M"

[process."rsync.*"]
weight = 1
download = "50M"
upload = "50M"
```

## Standalone mode

If you do not want to run the daemon, you can set per-command limits with standalone mode. No root, no setup:

```bash
# Limit a single download to 10 MB/s
trickle -m standalone -d 10M -- wget https://huggingface.co/some-model/resolve/main/pytorch_model.bin

# Keep pip installs from disrupting other work
trickle -m standalone -d 20M -- pip install torch transformers

# Pull a dataset with a specific limit
trickle -m standalone -d 50M -- python3 download_dataset.py
```

## Monitoring

```bash
# Interactive TUI dashboard with per-user and per-session bandwidth and history graphs
trickle top

# One-shot stats (JSON, scriptable)
trickle stats

# Diagnose setup issues
trickle doctor
```

## Verified tools

The following tools have been tested with `trickle` and work correctly:

| Tool | Notes |
|---|---|
| `curl` | Fully supported (all modes) |
| `wget` | Fully supported (all modes) |
| Python `requests` | Fully supported (all modes) |
| Python `urllib3` | Fully supported (all modes) |
| `pip` | Supported. Package downloads go through standard libc I/O |
| `git` (including `git-lfs`) | Supported for network operations. LFS transfers use libc I/O |
| Container runtimes | Supported when wrapping the runtime daemon (e.g. `containerd`), not the CLI client |

Any dynamically linked Linux program that uses standard libc socket I/O (`read`, `write`, `send`, `recv`, etc.) can be throttled with `trickle`.

See [limitations.md](limitations.md) for cases where `trickle` cannot apply limits.
