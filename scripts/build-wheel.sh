#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir="${root}/dist/python"
python="${PYTHON:-python3}"

while (($#)); do
  case "$1" in
    --out-dir) out_dir=$2; shift 2 ;;
    --python) python=$2; shift 2 ;;
    -h|--help)
      echo "usage: $0 [--out-dir DIR] [--python PYTHON]"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

mkdir -p "${out_dir}"
"${python}" -m build --wheel --sdist --outdir "${out_dir}"
(
  cd "${out_dir}"
  find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\n' |
    sort | xargs -r sha256sum > SHA256SUMS
)
echo "python artifacts in ${out_dir}"
