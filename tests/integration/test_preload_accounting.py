# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression test for sendfile byte accounting in the preload.

CPython's ``socket.sendfile`` binds ``sendfile64`` on glibc, so the preload
must interpose both ``sendfile`` and ``sendfile64``.  The test uploads a known
payload through a wrapped Python client and checks the daemon's per-session
``bytes_uploaded`` counter while the client is still connected.
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

from conftest import NtricklePaths, require_ntrickle_paths

CLIENT_SCRIPT = r"""
import socket
import sys

path = sys.argv[1]
port = int(sys.argv[2])
sock = socket.create_connection(("127.0.0.1", port), timeout=10)
with open(path, "rb") as f:
    sock.sendfile(f)
sock.recv(1)
sock.close()
"""


def _wait_for_path(
    path: Path, process: subprocess.Popen[bytes], timeout: float = 5.0
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            pytest.fail(f"ntrickled exited before {path} appeared")
        if path.exists():
            return
        time.sleep(0.05)
    pytest.fail(f"timed out waiting for {path}")


def _query_ctl(ctl_path: Path, request: dict) -> dict:
    payload = (json.dumps(request) + "\n").encode("utf-8")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5.0)
        sock.connect(str(ctl_path))
        sock.sendall(payload)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    return json.loads(b"".join(chunks).decode("utf-8"))


@pytest.fixture
def running_daemon(tmp_path: Path, ntrickle_paths: NtricklePaths):
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)

    socket_path = tmp_path / "ntrickled.sock"
    config_path = tmp_path / "ntrickled.toml"
    config_path.write_text("[General]\n", encoding="utf-8")
    process = subprocess.Popen(
        [
            str(ntrickle_paths.ntrickled_bin),
            "-n",
            str(socket_path),
            "-c",
            str(config_path),
            "-d",
            "1G",
            "-u",
            "1G",
            "--burst",
            "60",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    ctl_path = Path(str(socket_path) + ".ctl")
    try:
        _wait_for_path(socket_path, process)
        _wait_for_path(ctl_path, process)
        yield socket_path, ctl_path
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        for path in (socket_path, ctl_path, Path(str(socket_path) + ".lock")):
            try:
                path.unlink()
            except FileNotFoundError:
                pass


def test_sendfile_bytes_are_accounted(
    running_daemon, ntrickle_paths: NtricklePaths, tmp_path: Path
) -> None:
    data_socket, ctl_path = running_daemon
    payload = bytes(range(256)) * 1024  # 256 KiB
    payload_path = tmp_path / "payload.bin"
    payload_path.write_bytes(payload)

    release = threading.Event()
    received_all = threading.Event()
    received = bytearray()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]

    def receiver() -> None:
        conn, _ = server.accept()
        with conn:
            while len(received) < len(payload):
                chunk = conn.recv(65536)
                if not chunk:
                    break
                received.extend(chunk)
            received_all.set()
            release.wait(timeout=10)
            conn.sendall(b"a")

    thread = threading.Thread(target=receiver, name="ntrickle-receiver", daemon=True)
    thread.start()

    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    env["NTRICKLE_PRELOAD_PATH"] = str(ntrickle_paths.preload_lib)
    env.pop("NTRICKLE_PROCESS_CONFIG", None)
    env.pop("NTRICKLE_SCHEDULES_CONFIG", None)

    client = subprocess.Popen(
        [
            str(ntrickle_paths.ntrickle_bin),
            "-n",
            str(data_socket),
            "--",
            sys.executable,
            "-c",
            CLIENT_SCRIPT,
            str(payload_path),
            str(port),
        ],
        cwd=ntrickle_paths.preload_lib.parent,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    try:
        assert received_all.wait(timeout=20), "client did not send the payload"
        assert bytes(received) == payload

        deadline = time.monotonic() + 5.0
        session = None
        while time.monotonic() < deadline:
            stats = _query_ctl(ctl_path, {"cmd": "stats"})
            session = next(
                (s for s in stats.get("sessions", []) if s.get("pid") == client.pid),
                None,
            )
            if session is not None and session.get("bytes_uploaded", 0) >= len(payload):
                break
            time.sleep(0.05)

        assert session is not None, "client session never appeared in daemon stats"
        assert session["bytes_uploaded"] >= len(payload)
    finally:
        release.set()
        if client.poll() is None:
            try:
                client.wait(timeout=5)
            except subprocess.TimeoutExpired:
                client.kill()
                client.wait(timeout=5)
        client.communicate(timeout=5)
        server.close()
