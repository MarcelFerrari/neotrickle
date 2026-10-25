#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a relocatable portable archive for the current Linux architecture.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir="${root}/dist/portable"
build_dir="${root}/build-portable"
arch="${NTRICKLE_ARCH:-$(uname -m)}"

while (($#)); do
  case "$1" in
    --out-dir) out_dir=$2; shift 2 ;;
    --build-dir) build_dir=$2; shift 2 ;;
    --arch) arch=$2; shift 2 ;;
    -h|--help)
      echo "usage: $0 [--out-dir DIR] [--build-dir DIR] [--arch ARCH]"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

case "$arch" in
  x86_64|aarch64) ;;
  arm64) arch=aarch64 ;;
  *) echo "unsupported architecture: $arch" >&2; exit 2 ;;
esac

version=$(sed -n 's/^project(ntrickle VERSION \([0-9.]*\).*/\1/p' "${root}/CMakeLists.txt")
if [[ -z "$version" ]]; then
  echo "could not determine project version" >&2
  exit 1
fi

stage="${build_dir}/stage"
prefix="${stage}/neotrickle-${version}-linux-${arch}"
rm -rf "${stage}"
mkdir -p "${out_dir}" "${prefix}"

cmake -S "${root}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNTRICKLE_INSTALL_PROFILE=portable \
  -DNTRICKLE_INSTALL_ALIASES=ON \
  -DNTRICKLE_ENABLE_TESTS=OFF \
  -DNTRICKLE_ENABLE_INTEGRATION_TESTS=OFF
cmake --build "${build_dir}" --parallel
cmake --install "${build_dir}" --prefix "${prefix}"

"${PYTHON:-python3}" "${root}/scripts/bundle_runtime_deps.py" "${prefix}"

install -d "${prefix}/share/doc/neotrickle"
install -m 0644 "${root}/README.md" "${prefix}/share/doc/neotrickle/"
install -m 0644 "${root}/CHANGELOG.md" "${prefix}/share/doc/neotrickle/"
install -m 0644 "${root}/docs/distribution.md" "${prefix}/share/doc/neotrickle/"
install -m 0644 "${root}/docs/migration-classic.md" "${prefix}/share/doc/neotrickle/"

archive="${out_dir}/neotrickle-${version}-linux-${arch}.tar.gz"
tar --owner=0 --group=0 --numeric-owner -C "${stage}" -czf "${archive}" \
  "neotrickle-${version}-linux-${arch}"
sha256sum "${archive}" > "${archive}.sha256"
echo "created ${archive}"
