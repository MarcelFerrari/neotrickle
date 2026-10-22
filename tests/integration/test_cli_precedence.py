# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression tests for CLI-over-config precedence in the launcher.

An explicitly supplied CLI flag must win even when its value equals the
argument default.  The launcher previously inferred explicitness by comparing
against the default strings ("1M", weight 1), so ``-d 1M`` could be silently
overridden by process.toml or schedules.toml.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from datetime import datetime, timedelta
from pathlib import Path

import pytest

from conftest import NtricklePaths, require_ntrickle_paths

MIB = 1024 * 1024


def _write_configs(tmp_path: Path) -> tuple[Path, Path]:
    process_cfg = tmp_path / "process.toml"
    process_cfg.write_text(
        '[process."env"]\n'
        'download = "3M"\n'
        'upload = "5M"\n'
        "weight = 5\n",
        encoding="utf-8",
    )

    # A window covering the current minute, including across midnight.
    now = datetime.now()
    start = (now - timedelta(minutes=10)).strftime("%H:%M")
    end = (now + timedelta(minutes=10)).strftime("%H:%M")
    schedules_cfg = tmp_path / "schedules.toml"
    schedules_cfg.write_text(
        "[schedules.window]\n"
        f'start = "{start}"\n'
        f'end = "{end}"\n'
        'download = "2M"\n'
        'upload = "4M"\n',
        encoding="utf-8",
    )
    return process_cfg, schedules_cfg


def _run_env(paths: NtricklePaths, tmp_path: Path, *cli_args: str) -> dict[str, str]:
    require_ntrickle_paths(paths)
    if shutil.which("env") is None:
        pytest.skip("env is not available")

    process_cfg, schedules_cfg = _write_configs(tmp_path)

    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    env["XDG_CONFIG_HOME"] = str(tmp_path / "xdg")
    env["NTRICKLE_PRELOAD_PATH"] = str(paths.preload_lib)
    # Keep ambient daemon/process/schedule config out of the launcher.
    env.pop("NTRICKLE_PROCESS_CONFIG", None)
    env.pop("NTRICKLE_SCHEDULES_CONFIG", None)

    argv = [
        str(paths.ntrickle_bin),
        "-m",
        "standalone",
        "--process-config",
        str(process_cfg),
        "--schedules",
        str(schedules_cfg),
        *cli_args,
        "--",
        "env",
    ]
    completed = subprocess.run(
        argv,
        cwd=paths.preload_lib.parent,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=20,
    )
    assert completed.returncode == 0, completed.stderr.decode("utf-8", errors="replace")

    values: dict[str, str] = {}
    for line in completed.stdout.decode("utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values


def test_explicit_defaults_win_over_config(ntrickle_paths, tmp_path: Path) -> None:
    values = _run_env(ntrickle_paths, tmp_path, "-d", "1M", "-u", "1M", "-W", "1")

    assert values["NTRICKLE_DOWNLOAD_LIMIT"] == str(MIB)
    assert values["NTRICKLE_UPLOAD_LIMIT"] == str(MIB)
    assert values["NTRICKLE_WEIGHT"] == "1"


def test_config_applies_when_flags_absent(ntrickle_paths, tmp_path: Path) -> None:
    values = _run_env(ntrickle_paths, tmp_path)

    # schedules.toml is evaluated first, then process.toml overrides it.
    assert values["NTRICKLE_DOWNLOAD_LIMIT"] == str(3 * MIB)
    assert values["NTRICKLE_UPLOAD_LIMIT"] == str(5 * MIB)
    assert values["NTRICKLE_WEIGHT"] == "5"


def test_partial_override_keeps_other_config_values(ntrickle_paths, tmp_path: Path) -> None:
    values = _run_env(ntrickle_paths, tmp_path, "-d", "1M")

    assert values["NTRICKLE_DOWNLOAD_LIMIT"] == str(MIB)
    assert values["NTRICKLE_UPLOAD_LIMIT"] == str(5 * MIB)
    assert values["NTRICKLE_WEIGHT"] == "5"


def test_long_aliases_are_treated_as_explicit(ntrickle_paths, tmp_path: Path) -> None:
    values = _run_env(
        ntrickle_paths,
        tmp_path,
        "--download",
        "1M",
        "--upload",
        "1M",
        "--weight",
        "1",
    )

    assert values["NTRICKLE_DOWNLOAD_LIMIT"] == str(MIB)
    assert values["NTRICKLE_UPLOAD_LIMIT"] == str(MIB)
    assert values["NTRICKLE_WEIGHT"] == "1"
