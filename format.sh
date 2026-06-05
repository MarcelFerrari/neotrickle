#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# format.sh — Run clang-format across all ntrickle source files.
#
# Usage:
#   ./format.sh           Format all sources in-place.
#   ./format.sh --check   Check formatting without modifying files (dry-run).
#                         Exits 0 if already formatted, 1 if changes needed.
#
# Requires clang-format. Adjust CLANG_FORMAT below if needed.

set -euo pipefail

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

# Directories to search for source files.
SOURCE_DIRS=(
    src
    tests
)

MODE="format"
if [[ "${1:-}" == "--check" ]]; then
    MODE="check"
elif [[ "${1:-}" != "" ]]; then
    echo "usage: $0 [--check]" >&2
    exit 2
fi

# ---------------------------------------------------------------------------
# Check that clang-format is available.
# ---------------------------------------------------------------------------
if ! command -v "$CLANG_FORMAT" &>/dev/null; then
    echo "error: clang-format not found (set CLANG_FORMAT env var to override)" >&2
    exit 1
fi

CLANG_FORMAT_VERSION=$("$CLANG_FORMAT" --version 2>/dev/null || true)

# ---------------------------------------------------------------------------
# Collect source files.
# ---------------------------------------------------------------------------
declare -a FILES=()

# Collect files: find *.c *.h *.cpp *.hpp in each source dir,
# excluding third_party/ directories.
for dir in "${SOURCE_DIRS[@]}"; do
    if [[ ! -d "$dir" ]]; then
        continue
    fi
    while IFS= read -r -d '' file; do
        FILES+=("$file")
    done < <(find "$dir" \
        -path '*/third_party/*' -prune -o \
        \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
        -print0)
done

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No source files found." >&2
    exit 0
fi

echo "Found ${#FILES[@]} source files."
echo "clang-format: $CLANG_FORMAT_VERSION"
echo

# ---------------------------------------------------------------------------
# Format or check.
# ---------------------------------------------------------------------------
FAILED=0

if [[ "$MODE" == "check" ]]; then
    echo "=== Checking formatting (dry-run) ==="
    while IFS= read -r -d '' file; do
        if ! "$CLANG_FORMAT" --dry-run --Werror "$file" &>/dev/null; then
            echo "  NEEDS FORMAT: $file"
            FAILED=1
        fi
    done < <(printf '%s\0' "${FILES[@]}")
else
    echo "=== Formatting ==="
    "$CLANG_FORMAT" -i "${FILES[@]}"
    echo "Done."
fi

if [[ "$FAILED" -eq 1 ]]; then
    echo
    echo "Some files need formatting. Run ./format.sh to fix."
    exit 1
fi

exit 0
