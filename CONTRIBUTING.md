# Contributing

Read [AGENTS.md](AGENTS.md) before adding code. It defines the architecture, component boundaries, and what's out of scope.

## Prerequisites

- Linux (kernel 4.x or later recommended)
- `build-essential`, `cmake`, `libevent-dev`, `libncurses-dev`
- Python 3.8+ with `pytest` (for integration tests)

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake libevent-dev libncurses-dev
```

## Build and test before submitting

```bash
python3 -m venv .venv
.venv/bin/pip install -r tests/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNTRICKLE_ENABLE_TESTS=ON -DNTRICKLE_ENABLE_INTEGRATION_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Formatting

```bash
./format.sh          # apply formatting
./format.sh --check  # check only (CI mode)
```

## Valgrind Memcheck testing

An opt-in Valgrind test suite runs unit, smoke, and lifecycle tests under Memcheck with strict leak and file-descriptor tracking.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DNTRICKLE_ENABLE_TESTS=ON \
  -DNTRICKLE_ENABLE_INTEGRATION_TESTS=ON \
  -DNTRICKLE_ENABLE_VALGRIND_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -L valgrind
```

The Valgrind tests use CTest labels so they stay separate from the normal test suite. The same command runs in CI. See `tests/valgrind.supp` for suppressions (currently empty, no project-owned leaks are suppressed).

## Running integration tests directly

```bash
.venv/bin/pytest tests/integration --ntrickle-mode=both \
  --ntrickle-bin build/bin/ntrickle \
  --ntrickled-bin build/bin/ntrickled \
  --ntrickle-preload-lib build/lib/libntrickle_preload.so
```

## Commit expectations

- Keep commits focused and reviewable.
- Don't mix formatting changes with functional changes.
- Run tests and formatting checks before submitting.
- Preserve existing behavior unless fixing a clear bug.

## Pull requests

- Describe what the change does and why.
- Include test coverage for bug fixes.
- Keep changes small and focused on one thing at a time.

## Reporting issues

- Include the `ntrickle` version (`ntrickle -V`).
- Describe what you expected and what happened instead.
- Include steps to reproduce when you can.

## Scope

Contributions should fit the project's cooperative userspace bandwidth limiting focus. For larger changes, open an issue first to discuss the approach.

See [PROVENANCE.md](PROVENANCE.md) and [LICENSING.md](LICENSING.md) for licensing information. Contributions are accepted under GPL-3.0-or-later.
