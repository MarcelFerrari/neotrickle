# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""Python entry points for the native neotrickle distribution."""

from __future__ import annotations

import os
import sys
from pathlib import Path

_NATIVE = Path(__file__).resolve().parent / "_native"
_DAEMON_OVERRIDE_ARGS = {
    "--user",
    "-n",
    "--socket",
    "-c",
    "--config",
    "-V",
    "--version",
    "-h",
    "--help",
}


def _exec_native(binary: str, *, daemon: bool) -> None:
    executable = _NATIVE / "bin" / binary
    if not executable.is_file():
        raise SystemExit(f"neotrickle: native executable not found: {executable}")

    args = list(sys.argv[1:])
    if daemon and not any(arg in _DAEMON_OVERRIDE_ARGS for arg in args):
        # A wheel-installed daemon has no system service and should not need
        # root.  Explicit socket/config/user flags still take precedence.
        args.insert(0, "--user")

    preload = None
    for relative in (
        Path("lib") / "ntrickle",
        Path("lib64") / "ntrickle",
        Path("lib") / "x86_64-linux-gnu" / "ntrickle",
        Path("lib") / "aarch64-linux-gnu" / "ntrickle",
    ):
        candidate = _NATIVE / relative / "libntrickle_preload.so"
        if candidate.is_file():
            preload = candidate
            break

    env = os.environ.copy()
    env["NTRICKLE_PACKAGE_ROOT"] = str(_NATIVE)
    if preload is not None:
        env["NTRICKLE_PRELOAD_PATH"] = str(preload)

    argv0 = Path(sys.argv[0]).name or binary
    os.execve(str(executable), [argv0, *args], env)


def cli() -> None:
    _exec_native("trickle", daemon=False)


def cli_alias() -> None:
    _exec_native("trickle", daemon=False)


def daemon() -> None:
    _exec_native("trickled", daemon=True)


def daemon_alias() -> None:
    _exec_native("trickled", daemon=True)
