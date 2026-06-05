# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Valgrind Memcheck lifecycle tests for ntrickle daemon and CLI.

These tests run the daemon and CLI through start/use/stop cycles under
Valgrind and verify that no memory errors, definite leaks, or indirect
leaks are reported.  They complement the simpler CTest ``valgrind_*``
targets (which run unit and smoke binaries directly under Valgrind) by
exercising multi-step interactions.
"""

from __future__ import annotations

import os
import signal
import subprocess
import time
from pathlib import Path

import pytest

from conftest import (
    ValgrindPaths,
    require_binaries,
    valgrind_command,
    valgrind_log_clean,
    wait_for_socket,
)


# ---------------------------------------------------------------------------
# Daemon lifecycle under Valgrind
# ---------------------------------------------------------------------------


def test_daemon_startup_stats_shutdown(vg_paths: ValgrindPaths, tmp_path: Path) -> None:
    """Start ntrickled under Valgrind, query stats, graceful SIGTERM shutdown.

    Verifies the full daemon lifecycle: socket creation, client connection
    via the control socket, signal handling, and cleanup.  Both the data
    socket and control socket are exercised.
    """
    require_binaries(vg_paths, need_daemon=True)

    socket_path = tmp_path / "ntrickled.sock"
    log_path = tmp_path / "valgrind-daemon.log"

    # Build the Valgrind-wrapped daemon command.
    vg_cmd = valgrind_command(vg_paths, log_path=log_path)
    daemon_cmd = [
        str(vg_paths.ntrickled_bin),
        "-n", str(socket_path),
        "-d", "100M",
        "-u", "100M",
    ]

    proc = subprocess.Popen(
        vg_cmd + daemon_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        # Wait for the daemon's data socket to appear.
        wait_for_socket(socket_path, proc)

        # Under Valgrind the daemon starts slowly — the control socket
        # appears after the data socket, not simultaneously.  Wait for it
        # explicitly before trying to connect.
        ctl_path_vg = Path(str(socket_path) + ".ctl")
        wait_for_socket(ctl_path_vg, proc)

        # Send a stats query via the control socket.
        result = subprocess.run(
            [str(vg_paths.ntrickle_bin), "-n", str(socket_path), "stats"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=15,
            check=False,
        )
        assert result.returncode == 0, (
            f"stats query failed:\n{result.stderr.decode(errors='replace')}"
        )

        # Graceful shutdown via SIGTERM.
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)

    # Verify the Valgrind log is clean.
    assert valgrind_log_clean(log_path), (
        f"Valgrind errors or leaks detected in daemon lifecycle.\n"
        f"Log excerpt:\n{_log_tail(log_path)}"
    )


def test_daemon_client_interaction(vg_paths: ValgrindPaths, tmp_path: Path) -> None:
    """Start daemon under Valgrind, run a client connection, shut down.

    This test wraps /bin/true under ntrickle (which connects to the daemon
    via LD_PRELOAD), then verifies the daemon remained clean.
    """
    require_binaries(vg_paths, need_daemon=True)

    socket_path = tmp_path / "ntrickled.sock"
    log_path = tmp_path / "valgrind-daemon-client.log"

    vg_cmd = valgrind_command(vg_paths, log_path=log_path)
    daemon_cmd = [
        str(vg_paths.ntrickled_bin),
        "-n", str(socket_path),
        "-d", "100M",
        "-u", "100M",
    ]

    proc = subprocess.Popen(
        vg_cmd + daemon_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        wait_for_socket(socket_path, proc)

        # Run a client through the daemon (standalone=False uses daemon mode).
        client_result = subprocess.run(
            [
                str(vg_paths.ntrickle_bin),
                "-n", str(socket_path),
                "-d", "10M",
                "--", "/bin/true",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=15,
            check=False,
            cwd=vg_paths.preload_lib.parent,
        )
        # Client should succeed (daemon is running).
        assert client_result.returncode == 0, (
            f"client failed:\n{client_result.stderr.decode(errors='replace')}"
        )

        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)

    assert valgrind_log_clean(log_path), (
        f"Valgrind errors or leaks after client interaction.\n"
        f"Log excerpt:\n{_log_tail(log_path)}"
    )


# ---------------------------------------------------------------------------
# CLI lifecycle under Valgrind
# ---------------------------------------------------------------------------


def test_cli_clean_startup_exit(vg_paths: ValgrindPaths, tmp_path: Path) -> None:
    """Run ntrickle -V under Valgrind — validates CLI startup and clean exit."""
    require_binaries(vg_paths)

    log_path = tmp_path / "valgrind-cli.log"
    vg_cmd = valgrind_command(vg_paths, log_path=log_path)

    result = subprocess.run(
        vg_cmd + [str(vg_paths.ntrickle_bin), "-V"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=15,
        check=False,
    )
    # -V prints version and exits 0.
    assert result.returncode == 0, (
        f"ntrickle -V failed:\n{result.stderr.decode(errors='replace')}"
    )
    assert valgrind_log_clean(log_path), (
        f"Valgrind errors or leaks in CLI startup.\n"
        f"Log excerpt:\n{_log_tail(log_path)}"
    )


def test_cli_standalone_wrap(vg_paths: ValgrindPaths, tmp_path: Path) -> None:
    """Run ntrickle standalone wrap of /bin/true under Valgrind.

    Validates the full standalone path: preload resolution, env setup,
    LD_PRELOAD merge, execvp, and preload init/exit.
    """
    require_binaries(vg_paths)

    log_path = tmp_path / "valgrind-standalone.log"
    vg_cmd = valgrind_command(vg_paths, log_path=log_path)

    result = subprocess.run(
        vg_cmd
        + [str(vg_paths.ntrickle_bin), "-m", "standalone", "-d", "1M", "--", "/bin/true"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=15,
        check=False,
        cwd=vg_paths.preload_lib.parent,
    )
    assert result.returncode == 0, (
        f"standalone wrap failed:\n{result.stderr.decode(errors='replace')}"
    )
    assert valgrind_log_clean(log_path), (
        f"Valgrind errors or leaks in standalone wrap.\n"
        f"Log excerpt:\n{_log_tail(log_path)}"
    )


def test_repeated_wrap_no_leak_accumulation(vg_paths: ValgrindPaths, tmp_path: Path) -> None:
    """Repeated standalone wraps under Valgrind — catches accumulating leaks."""
    require_binaries(vg_paths)

    # Re-use the same log file for all 3 iterations to accumulate results.
    log_path = tmp_path / "valgrind-repeated.log"

    # Run 3 wraps sequentially — the preload initialises from scratch each
    # time (each process gets a fresh address space), so accumulating leaks
    # would come from the launcher itself or from a shared resource.
    for i in range(3):
        vg_cmd = valgrind_command(vg_paths, log_path=log_path)
        result = subprocess.run(
            vg_cmd
            + [
                str(vg_paths.ntrickle_bin),
                "-m", "standalone",
                "-d", "1M",
                "--", "/bin/true",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=15,
            check=False,
            cwd=vg_paths.preload_lib.parent,
        )
        assert result.returncode == 0, (
            f"repeated wrap iteration {i} failed:\n{result.stderr.decode(errors='replace')}"
        )
        assert valgrind_log_clean(log_path), (
            f"Valgrind errors or leaks in repeated wrap iteration {i}.\n"
            f"Log excerpt:\n{_log_tail(log_path)}"
        )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _log_tail(path: Path, lines: int = 40) -> str:
    """Return the last *lines* of the Valgrind log for diagnostic output."""
    if not path.exists():
        return "(log file not found)"
    all_lines = path.read_text(errors="replace").splitlines()
    return "\n".join(all_lines[-lines:])
