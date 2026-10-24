# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
"""neotrickle: native Linux bandwidth limiter with Python entry points."""

from __future__ import annotations

__all__ = ["__version__"]

try:
    from importlib.metadata import version as _distribution_version

    __version__ = _distribution_version("neotrickle")
except Exception:  # pragma: no cover - source-tree imports without metadata
    __version__ = "unknown"
