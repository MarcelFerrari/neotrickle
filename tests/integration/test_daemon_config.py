# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression tests for daemon configuration precedence and env overrides."""

from __future__ import annotations

import subprocess
from pathlib import Path

from conftest import (
    NtricklePaths,
    query_ctl,
    require_ntrickle_paths,
    send_conf_and_read_grant,
)

CONFIG = """\
[General]
Fairness = "dynamic"
Idle-Threshold = 7
Burst-Seconds = 3
"""

BURST_CONFIG = """\
[General]
Burst-Seconds = 60
Burst-Bandwidth = "64K"
"""

UINT64_MAX = 2**64 - 1


def test_config_only_values_apply(ntrickled_daemon_factory) -> None:
    handle = ntrickled_daemon_factory(config_text=CONFIG)
    stats = query_ctl(handle.ctl_path, {"cmd": "stats"})

    assert stats["global"]["fairness_mode"] == "dynamic"
    assert stats["global"]["idle_threshold_sec"] == 7
    assert stats["global"]["burst_seconds"] == 3


def test_explicit_cli_values_win(ntrickled_daemon_factory) -> None:
    handle = ntrickled_daemon_factory(
        config_text=CONFIG,
        extra_args=["-F", "static", "--idle-threshold", "9", "--burst", "4"],
    )
    stats = query_ctl(handle.ctl_path, {"cmd": "stats"})

    assert stats["global"]["fairness_mode"] == "static"
    assert stats["global"]["idle_threshold_sec"] == 9
    assert stats["global"]["burst_seconds"] == 4


def test_env_config_path_is_honored(ntrickled_daemon_factory, tmp_path: Path) -> None:
    config_path = tmp_path / "env.toml"
    config_path.write_text(CONFIG, encoding="utf-8")

    handle = ntrickled_daemon_factory(
        use_config_flag=False,
        env={"NTRICKLE_DAEMON_CONFIG": str(config_path)},
    )
    stats = query_ctl(handle.ctl_path, {"cmd": "stats"})

    assert stats["global"]["fairness_mode"] == "dynamic"
    assert stats["global"]["idle_threshold_sec"] == 7
    assert stats["global"]["burst_seconds"] == 3


def test_explicit_config_flag_wins_over_env(
    ntrickled_daemon_factory, tmp_path: Path
) -> None:
    env_config = tmp_path / "env.toml"
    env_config.write_text(CONFIG, encoding="utf-8")
    cli_config = tmp_path / "cli.toml"
    cli_config.write_text(
        '[General]\nFairness = "static"\nIdle-Threshold = 11\nBurst-Seconds = 2\n',
        encoding="utf-8",
    )

    handle = ntrickled_daemon_factory(
        config_path=cli_config,
        env={"NTRICKLE_DAEMON_CONFIG": str(env_config)},
    )
    stats = query_ctl(handle.ctl_path, {"cmd": "stats"})

    assert stats["global"]["fairness_mode"] == "static"
    assert stats["global"]["idle_threshold_sec"] == 11
    assert stats["global"]["burst_seconds"] == 2


def test_burst_bandwidth_from_config(ntrickled_daemon_factory) -> None:
    handle = ntrickled_daemon_factory(config_text=BURST_CONFIG)

    assert send_conf_and_read_grant(handle.socket_path) == (65536, 65536)


def test_burst_bandwidth_cli_overrides_config(ntrickled_daemon_factory) -> None:
    handle = ntrickled_daemon_factory(
        config_text=BURST_CONFIG,
        extra_args=["--burst-bandwidth", "10M"],
    )

    assert send_conf_and_read_grant(handle.socket_path) == (10 * 1024 * 1024, 10 * 1024 * 1024)


def test_explicit_unlimited_burst_bandwidth_wins(ntrickled_daemon_factory) -> None:
    handle = ntrickled_daemon_factory(
        config_text=BURST_CONFIG,
        extra_args=["--burst-bandwidth", "unlimited"],
    )

    assert send_conf_and_read_grant(handle.socket_path) == (UINT64_MAX, UINT64_MAX)


def test_invalid_fairness_in_config_fails(
    ntrickle_paths: NtricklePaths, tmp_path: Path
) -> None:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    config_path = tmp_path / "bad.toml"
    config_path.write_text('[General]\nFairness = "bogus"\n', encoding="utf-8")
    socket_path = tmp_path / "bad.sock"

    completed = subprocess.run(
        [
            str(ntrickle_paths.ntrickled_bin),
            "-n",
            str(socket_path),
            "-c",
            str(config_path),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=5,
    )

    assert completed.returncode != 0
    assert b"invalid fairness mode" in completed.stderr
