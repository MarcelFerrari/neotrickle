# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the modern CLI's explicit rejection of classic trickle forms.

The installed command names are classic, but the CLI is the modern ntrickle
interface.  Classic options and bare KB/s values must fail loudly rather than
being silently reinterpreted.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from conftest import NtricklePaths, require_ntrickle_paths


def _run(
    paths: NtricklePaths,
    tmp_path: Path,
    *args: str,
) -> subprocess.CompletedProcess[bytes]:
    require_ntrickle_paths(paths)
    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    env["NTRICKLE_PRELOAD_PATH"] = str(paths.preload_lib)
    env.pop("NTRICKLE_PROCESS_CONFIG", None)
    env.pop("NTRICKLE_SCHEDULES_CONFIG", None)
    return subprocess.run(
        [str(paths.ntrickle_bin), *args],
        cwd=paths.preload_lib.parent,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=20,
    )


def _env_values(result: subprocess.CompletedProcess[bytes]) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in result.stdout.decode("utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values


def test_classic_standalone_flag_is_rejected(ntrickle_paths, tmp_path: Path) -> None:
    result = _run(ntrickle_paths, tmp_path, "-s", "--", "true")

    assert result.returncode != 0
    stderr = result.stderr.decode("utf-8", errors="replace")
    assert "classic -s" in stderr
    assert "-m standalone" in stderr


def test_bare_rates_are_rejected(ntrickle_paths, tmp_path: Path) -> None:
    for flag in ("-d", "-u"):
        result = _run(ntrickle_paths, tmp_path, "-m", "standalone", flag, "10", "--", "env")

        assert result.returncode != 0
        stderr = result.stderr.decode("utf-8", errors="replace")
        assert "include a unit" in stderr


def test_explicit_units_are_accepted(ntrickle_paths, tmp_path: Path) -> None:
    cases = {
        "10K": 10 * 1024,
        "1M": 1024 * 1024,
        "1000B": 1000,
        "unlimited": 0,
    }
    for rate, expected in cases.items():
        result = _run(
            ntrickle_paths,
            tmp_path,
            "-m",
            "standalone",
            "-d",
            rate,
            "--",
            "env",
        )
        assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
        values = _env_values(result)
        assert values["NTRICKLE_DOWNLOAD_LIMIT"] == str(expected)


def test_suppress_warnings_is_long_only(ntrickle_paths, tmp_path: Path) -> None:
    result = _run(
        ntrickle_paths,
        tmp_path,
        "-m",
        "standalone",
        "--suppress-warnings",
        "--",
        "env",
    )

    assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
