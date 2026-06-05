# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared fixtures for ntrickle Valgrind Memcheck lifecycle tests."""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path
from typing import NamedTuple

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]


class ValgrindPaths(NamedTuple):
    ntrickle_bin: Path
    ntrickled_bin: Path
    preload_lib: Path
    valgrind_exe: Path
    suppressions: Path


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--ntrickle-bin",
        default=os.environ.get("NTRICKLE_BIN", str(REPO_ROOT / "build" / "bin" / "ntrickle")),
        help="Path to the built ntrickle binary.",
    )
    parser.addoption(
        "--ntrickled-bin",
        default=os.environ.get("NTRICKLED_BIN", str(REPO_ROOT / "build" / "bin" / "ntrickled")),
        help="Path to the built ntrickled binary.",
    )
    parser.addoption(
        "--ntrickle-preload-lib",
        default=os.environ.get(
            "NTRICKLE_PRELOAD_LIB", str(REPO_ROOT / "build" / "lib" / "libntrickle_preload.so")
        ),
        help="Path to the built libntrickle_preload.so.",
    )
    parser.addoption(
        "--valgrind-exe",
        default=os.environ.get("VALGRIND_EXE", "valgrind"),
        help="Path to the valgrind executable.",
    )
    parser.addoption(
        "--valgrind-suppressions",
        default=os.environ.get(
            "VALGRIND_SUPPRESSIONS", str(REPO_ROOT / "tests" / "valgrind.supp")
        ),
        help="Path to the valgrind suppressions file.",
    )


@pytest.fixture
def vg_paths(pytestconfig: pytest.Config) -> ValgrindPaths:
    return ValgrindPaths(
        ntrickle_bin=Path(pytestconfig.getoption("--ntrickle-bin")).resolve(),
        ntrickled_bin=Path(pytestconfig.getoption("--ntrickled-bin")).resolve(),
        preload_lib=Path(pytestconfig.getoption("--ntrickle-preload-lib")).resolve(),
        valgrind_exe=Path(pytestconfig.getoption("--valgrind-exe")),
        suppressions=Path(pytestconfig.getoption("--valgrind-suppressions")),
    )


def require_binaries(vp: ValgrindPaths, *, need_daemon: bool = False) -> None:
    if not vp.ntrickle_bin.exists():
        pytest.fail(f"ntrickle binary not found at {vp.ntrickle_bin}")
    if not vp.preload_lib.exists():
        pytest.fail(f"preload library not found at {vp.preload_lib}")
    if need_daemon and not vp.ntrickled_bin.exists():
        pytest.fail(f"ntrickled binary not found at {vp.ntrickled_bin}")


def valgrind_command(vp: ValgrindPaths, *, log_path: Path) -> list[str]:
    """Build the standard Valgrind Memcheck command line."""
    return [
        str(vp.valgrind_exe),
        "--tool=memcheck",
        "--leak-check=full",
        "--show-leak-kinds=all",
        "--errors-for-leak-kinds=definite,indirect",
        "--track-origins=yes",
        "--track-fds=yes",
        "--trace-children=yes",
        "--error-exitcode=1",
        f"--suppressions={vp.suppressions}",
        f"--log-file={log_path}",
    ]


def valgrind_log_clean(log_path: Path) -> bool:
    """Return True if the Valgrind log contains no errors or definite/indirect leaks."""
    if not log_path.exists():
        return False
    text = log_path.read_text(errors="replace")

    # Error summary must be zero
    if "ERROR SUMMARY: 0 errors" not in text:
        return False

    # No definite or indirect leaks
    for line in text.splitlines():
        if "definitely lost:" in line and "0 bytes in 0 blocks" not in line:
            return False
        if "indirectly lost:" in line and "0 bytes in 0 blocks" not in line:
            return False

    return True


def wait_for_socket(
    path: Path, process: subprocess.Popen[bytes], timeout: float = 10.0
) -> None:
    """Poll until the Unix socket appears or the process exits."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            pytest.fail(
                f"ntrickled exited before its socket became ready\n"
                f"stdout:\n{stdout.decode(errors='replace')}\n"
                f"stderr:\n{stderr.decode(errors='replace')}"
            )
        if path.exists():
            return
        time.sleep(0.05)
    pytest.fail(f"timed out waiting for socket {path}")
