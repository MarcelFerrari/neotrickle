# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression test for the preload's global lock under multithreaded clients.

The Hugging Face Xet backend (`hf_xet`) performs network I/O from a multithreaded
Rust runtime.  Without the preload's global mutex, concurrent MSG_TYPE_UPDATE
writes and grant polls corrupt the daemon control stream, and the daemon drops
the client.  This test drives several client threads through the daemon and
checks that the session remains registered and accounted.
"""

from __future__ import annotations

import os
import socketserver
import subprocess
import sys
import threading
import time
from pathlib import Path

from conftest import NtricklePaths, query_ctl

CLIENT_SCRIPT = r"""
import socket
import sys
import threading
import time

host = sys.argv[1]
port = int(sys.argv[2])
seconds = float(sys.argv[3])
payload = b"x" * 4096
deadline = time.monotonic() + seconds
errors = []


def worker():
    try:
        sock = socket.create_connection((host, port), timeout=5)
        sock.settimeout(5)
        while time.monotonic() < deadline:
            sock.sendall(payload)
            received = 0
            while received < len(payload):
                chunk = sock.recv(len(payload) - received)
                if not chunk:
                    return
                received += len(chunk)
        sock.close()
    except Exception as exc:  # noqa: BLE001 - report from worker threads
        errors.append(repr(exc))


threads = [threading.Thread(target=worker) for _ in range(8)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()

if errors:
    print(errors, file=sys.stderr)
    sys.exit(1)
"""


class EchoHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        while True:
            data = self.request.recv(65536)
            if not data:
                return
            self.request.sendall(data)


class EchoServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def _session_for_pid(stats: dict, pid: int) -> dict | None:
    return next((s for s in stats.get("sessions", []) if s.get("pid") == pid), None)


def test_multithreaded_client_keeps_daemon_session(
    ntrickled_daemon_factory,
    ntrickle_paths: NtricklePaths,
    tmp_path: Path,
) -> None:
    handle = ntrickled_daemon_factory(extra_args=["--burst", "0"])

    server = EchoServer(("127.0.0.1", 0), EchoHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    port = server.server_address[1]

    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    env["NTRICKLE_PRELOAD_PATH"] = str(ntrickle_paths.preload_lib)
    env.pop("NTRICKLE_PROCESS_CONFIG", None)
    env.pop("NTRICKLE_SCHEDULES_CONFIG", None)

    client = subprocess.Popen(
        [
            str(ntrickle_paths.ntrickle_bin),
            "-n",
            str(handle.socket_path),
            "--",
            sys.executable,
            "-c",
            CLIENT_SCRIPT,
            "127.0.0.1",
            str(port),
            "4",
        ],
        cwd=ntrickle_paths.preload_lib.parent,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    try:
        time.sleep(1.5)
        first = _session_for_pid(query_ctl(handle.ctl_path, {"cmd": "stats"}), client.pid)
        assert first is not None, "client session disappeared from the daemon"
        assert first["bytes_uploaded"] + first["bytes_downloaded"] > 0

        time.sleep(1.0)
        second = _session_for_pid(query_ctl(handle.ctl_path, {"cmd": "stats"}), client.pid)
        assert second is not None, "client session disappeared under multithreaded I/O"
        assert second["bytes_uploaded"] + second["bytes_downloaded"] > 0

        client.wait(timeout=20)
        assert client.returncode == 0, client.stderr.read().decode("utf-8", errors="replace")
    finally:
        if client.poll() is None:
            client.kill()
            client.wait(timeout=5)
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=5)
