#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a repaired manylinux wheel inside a manylinux_2_28 container/chroot.
# Run this script from inside the manylinux environment.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir="${root}/dist/wheel"
python="${PYTHON:-/opt/python/cp312-cp312/bin/python}"

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

if command -v dnf >/dev/null; then
  dnf install -y -q libevent-devel ncurses-devel gcc-c++ make patchelf libstdc++-static
elif command -v yum >/dev/null; then
  yum install -y -q libevent-devel ncurses-devel gcc-c++ make patchelf libstdc++-static
fi

"${python}" -m pip install -q --disable-pip-version-check --upgrade \
  pip build scikit-build-core auditwheel wheel

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "${out_dir}" "${work}/wheel" "${work}/repaired" "${work}/notices"

"${python}" -m build --wheel --outdir "${work}/wheel" "${root}"
"${python}" -m auditwheel repair "${work}"/wheel/*.whl -w "${work}/repaired"

# Use a portable install to collect the same bundled dependency licenses.
cmake -S "${root}" -B "${work}/portable-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNTRICKLE_INSTALL_PROFILE=portable \
  -DNTRICKLE_INSTALL_ALIASES=OFF \
  -DNTRICKLE_ENABLE_TESTS=OFF \
  -DNTRICKLE_ENABLE_INTEGRATION_TESTS=OFF
cmake --build "${work}/portable-build" --parallel
cmake --install "${work}/portable-build" --prefix "${work}/portable"
"${python}" "${root}/scripts/bundle_runtime_deps.py" "${work}/portable"

wheel_file=$(find "${work}/repaired" -maxdepth 1 -name '*.whl' -print -quit)
"${python}" -m wheel unpack "${wheel_file}" -d "${work}/unpacked"
unpacked=$(find "${work}/unpacked" -maxdepth 1 -mindepth 1 -type d -print -quit)
notices="${unpacked}/neotrickle/_native/share/licenses/ntrickle/bundled"
mkdir -p "${notices}"
cp -a "${work}/portable/share/licenses/ntrickle/bundled/." "${notices}/"
"${python}" -m wheel pack "${unpacked}" -d "${out_dir}"

echo "repaired wheel in ${out_dir}"
ls -l "${out_dir}"
