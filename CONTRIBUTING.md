# Contributing to neotrickle

People and coding agents follow the same contribution process. Read [AGENTS.md](AGENTS.md) for architecture, component boundaries, and project scope. Discuss larger features or behavior changes in an issue before implementing them.

## Before opening a PR

- Work on a branch from the latest `main`; target `main` when opening the PR.
- Keep each PR focused on one change. Separate unrelated cleanup and formatting.
- Add a regression test for bug fixes and appropriate coverage for new behavior, including failure paths.
- Update relevant documentation when changing CLI options, configuration, protocol, or behavior.
- Keep the preload library C-only.

## PR title and tags

Use **one required type tag** and **at most one optional component tag**:

```text
[Type] Short, specific description
[Type][Component] Short, specific description
```

| Type | Use for |
| --- | --- |
| `[Feature]` | New functionality |
| `[BugFix]` | Incorrect behavior |
| `[Perf]` | Performance improvements |
| `[Refactor]` | Internal changes that preserve behavior |
| `[Doc]` | Documentation |
| `[Test]` | Tests and test tooling |
| `[CI]` | Build and continuous integration |
| `[Misc]` | Other maintenance; use sparingly |

Optional components: `[CLI]`, `[Daemon]`, `[Preload]`, `[Config]`, `[Protocol]`, `[TUI]`.

Omit the component for changes spanning several areas. Choose the type that best describes the main purpose.

Examples:

```text
[BugFix][Preload] Preserve errno after failed socket reads
[Feature][Config] Add validation for schedule overlaps
[Doc] Clarify standalone mode limitations
```

These are PR-title tags. GitHub labels are separate; use existing labels when applicable and available to you.

## PR description

Include the following sections in every PR, including PRs opened through an agent or CLI:

```markdown
## Summary

Explain the problem, why it matters, and what changes.
Give a before/after example when useful.

## User-facing changes

Describe changes to behavior, CLI, configuration, or compatibility.
Call out breaking changes and any action users need to take.
Write "None" when applicable.

## Testing

Include exact commands and results.
Describe relevant regression coverage.
Explain skipped checks and remaining limitations.
Include before/after measurements for performance claims.

## Related issues

Use "Fixes #123" for an issue fully resolved by the PR,
"Refs #123" for related work, or "None".
```

Use a draft PR while work or validation is incomplete. Before requesting review, inspect the final diff, complete applicable checks, and resolve CI failures. Update the description if the scope changes.

## Testing locally

Run from the repository root on Linux. You need a C/C++ toolchain, CMake 3.20+, libevent and ncurses development headers, Python with venv support, `curl`, `wget`, and `clang-format`.

```bash
python3 -m venv .venv
.venv/bin/pip install -r tests/requirements.txt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DNTRICKLE_ENABLE_TESTS=ON \
  -DNTRICKLE_ENABLE_INTEGRATION_TESTS=ON \
  -DNTRICKLE_WARNINGS_AS_ERRORS=ON

cmake --build build --parallel
./format.sh --check
ctest --test-dir build --output-on-failure
```

Run `./format.sh` to apply formatting; include only relevant formatting changes. See [integration tests](tests/integration/README.md) for direct pytest commands.

For memory or resource-lifecycle changes, install Valgrind, reconfigure with the same options plus `-DNTRICKLE_ENABLE_VALGRIND_TESTS=ON`, rebuild, and run:

```bash
ctest --test-dir build --output-on-failure -L valgrind
```

For documentation-only changes, check rendering and links; code tests may be marked "Not run — documentation only".

Agents must report only checks actually executed and distinguish local results from CI results. Never claim that an unrun check passed.

## Reporting issues and licensing

When reporting a bug, include:

- The version from `ntrickle -V`.
- Your environment.
- Steps to reproduce.
- Expected versus actual behavior.

Contributions are accepted under GPL-3.0-or-later. Preserve existing notices and see [LICENSING.md](LICENSING.md) and [PROVENANCE.md](PROVENANCE.md).
