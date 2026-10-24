# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rootless user-daemon lifecycle and coordination tests."""

from __future__ import annotations

import json
import os
import signal
import socket
import stat
import subprocess
import sys
import time
from pathlib import Path

import pytest

from conftest import (
    REPO_ROOT,
    NtricklePaths,
    build_wrapped_command,
    require_ntrickle_paths,
)

CLIENT = r"""
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=10) as response:
    sys.stdout.buffer.write(response.read())
"""


def _user_env(tmp_path: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["HOME"] = str(tmp_path / "home")
    env["XDG_RUNTIME_DIR"] = str(tmp_path / "run")
    env["XDG_CONFIG_HOME"] = str(tmp_path / "config")
    Path(env["HOME"]).mkdir(parents=True, exist_ok=True)
    Path(env["XDG_RUNTIME_DIR"]).mkdir(mode=0o700, exist_ok=True)
    Path(env["XDG_CONFIG_HOME"]).mkdir(parents=True, exist_ok=True)
    return env


def _start_user_daemon(
    paths: NtricklePaths,
    env: dict[str, str],
    *extra: str,
) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        [
            str(paths.ntrickled_bin),
            "--user",
            "--download-rate",
            "1M",
            "--upload-rate",
            "1M",
            *extra,
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        env=env,
    )


def _wait_for_running_daemon(
    process: subprocess.Popen[bytes], ctl_path: Path, timeout: float = 10.0
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            _, stderr = process.communicate()
            pytest.fail(
                "user daemon exited before becoming ready\n"
                + stderr.decode(errors="replace")
            )
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(0.5)
                sock.connect(str(ctl_path))
                sock.sendall(b'{"cmd":"stats"}\n')
                data = b""
                while True:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    data += chunk
            json.loads(data.decode())
            return
        except (OSError, ValueError):
            time.sleep(0.05)
    pytest.fail(f"timed out waiting for user daemon at {ctl_path}")


def _stop_daemon(process: subprocess.Popen[bytes]) -> int:
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
    try:
        return process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        return process.wait(timeout=5)


def _json_query(ctl_path: Path, request: dict) -> dict:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5.0)
        sock.connect(str(ctl_path))
        sock.sendall((json.dumps(request) + "\n").encode())
        data = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode())


def test_service_does_not_predelete_socket_or_lock() -> None:
    service = (REPO_ROOT / "config" / "ntrickled.service.in").read_text()
    assert "ExecStartPre" not in service
    assert "rm -f" not in service


def test_user_daemon_runtime_layout_and_transfer(
    ntrickle_paths: NtricklePaths, tmp_path: Path, download_fixture
) -> None:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    env = _user_env(tmp_path)
    process = _start_user_daemon(ntrickle_paths, env)
    socket_path = Path(env["XDG_RUNTIME_DIR"]) / "ntrickle" / "ntrickled.sock"
    ctl_path = Path(str(socket_path) + ".ctl")
    try:
        _wait_for_running_daemon(process, ctl_path)

        runtime_dir = socket_path.parent
        assert stat.S_IMODE(runtime_dir.stat().st_mode) == 0o700
        assert stat.S_IMODE(socket_path.stat().st_mode) == 0o600
        assert stat.S_IMODE(ctl_path.stat().st_mode) == 0o600

        stats = _json_query(ctl_path, {"cmd": "stats"})
        assert stats["global"]["download_limit_bps"] == 1024 * 1024

        discovered = subprocess.run(
            [str(ntrickle_paths.ntrickle_bin), "stats"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
            env=env,
        )
        assert discovered.returncode == 0, discovered.stderr.decode(errors="replace")
        assert b"ntrickle daemon" in discovered.stdout

        prepared = build_wrapped_command(
            ntrickle_paths,
            [sys.executable, "-c", CLIENT, download_fixture.url],
            download_limit="1M",
            standalone=False,
            socket_path=socket_path,
        )
        completed = subprocess.run(
            prepared.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
            cwd=prepared.cwd,
            env=env,
        )
        assert completed.returncode == 0, completed.stderr.decode(errors="replace")
        assert len(completed.stdout) == download_fixture.size

        # With no explicit -n, the launcher should discover the user socket
        # after the system socket is found to be absent.
        discovered_client = build_wrapped_command(
            ntrickle_paths,
            [sys.executable, "-c", CLIENT, download_fixture.url],
            download_limit="1M",
            standalone=False,
        )
        completed = subprocess.run(
            discovered_client.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
            cwd=discovered_client.cwd,
            env=env,
        )
        assert completed.returncode == 0, completed.stderr.decode(errors="replace")
        assert len(completed.stdout) == download_fixture.size
    finally:
        assert _stop_daemon(process) == 0

    assert not socket_path.exists()
    assert not ctl_path.exists()
    assert Path(str(socket_path) + ".lock").exists()


def test_user_daemon_uses_xdg_config(
    ntrickle_paths: NtricklePaths, tmp_path: Path
) -> None:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    env = _user_env(tmp_path)
    config_dir = Path(env["XDG_CONFIG_HOME"]) / "ntrickle"
    config_dir.mkdir(parents=True)
    (config_dir / "ntrickled.toml").write_text(
        '[General]\nFairness = "dynamic"\n', encoding="utf-8"
    )

    process = _start_user_daemon(ntrickle_paths, env)
    socket_path = Path(env["XDG_RUNTIME_DIR"]) / "ntrickle" / "ntrickled.sock"
    ctl_path = Path(str(socket_path) + ".ctl")
    try:
        _wait_for_running_daemon(process, ctl_path)
        stats = _json_query(ctl_path, {"cmd": "stats"})
        assert stats["global"]["fairness_mode"] == "dynamic"
    finally:
        _stop_daemon(process)


def test_user_daemon_respects_explicit_paths(
    ntrickle_paths: NtricklePaths, tmp_path: Path
) -> None:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    env = _user_env(tmp_path)
    explicit_socket = tmp_path / "custom" / "trickled.sock"
    explicit_config = tmp_path / "custom" / "trickled.toml"
    explicit_socket.parent.mkdir(mode=0o700)
    explicit_config.write_text('[General]\nFairness = "dynamic"\n', encoding="utf-8")

    process = _start_user_daemon(
        ntrickle_paths,
        env,
        "-n",
        str(explicit_socket),
        "-c",
        str(explicit_config),
    )
    ctl_path = Path(str(explicit_socket) + ".ctl")
    try:
        _wait_for_running_daemon(process, ctl_path)
        stats = _json_query(ctl_path, {"cmd": "stats"})
        assert stats["global"]["fairness_mode"] == "dynamic"
        assert not (Path(env["XDG_RUNTIME_DIR"]) / "ntrickle" / "ntrickled.sock").exists()
    finally:
        _stop_daemon(process)


def test_second_user_daemon_is_rejected(
    ntrickle_paths: NtricklePaths, tmp_path: Path
) -> None:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    env = _user_env(tmp_path)
    first = _start_user_daemon(ntrickle_paths, env)
    socket_path = Path(env["XDG_RUNTIME_DIR"]) / "ntrickle" / "ntrickled.sock"
    ctl_path = Path(str(socket_path) + ".ctl")
    try:
        _wait_for_running_daemon(first, ctl_path)
        second = subprocess.run(
            [str(ntrickle_paths.ntrickled_bin), "--user"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
            env=env,
        )
        assert second.returncode != 0
        assert b"already running" in second.stderr
    finally:
        _stop_daemon(first)


def test_stale_user_socket_is_recovered(
    ntrickle_paths: NtricklePaths, tmp_path: Path
) -> None:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    env = _user_env(tmp_path)
    socket_path = Path(env["XDG_RUNTIME_DIR"]) / "ntrickle" / "ntrickled.sock"
    ctl_path = Path(str(socket_path) + ".ctl")

    first = _start_user_daemon(ntrickle_paths, env)
    _wait_for_running_daemon(first, ctl_path)
    first.kill()
    first.wait(timeout=5)
    assert socket_path.exists(), "SIGKILL should leave a stale socket"

    second = _start_user_daemon(ntrickle_paths, env)
    try:
        _wait_for_running_daemon(second, ctl_path)
        _json_query(ctl_path, {"cmd": "stats"})
    finally:
        _stop_daemon(second)
