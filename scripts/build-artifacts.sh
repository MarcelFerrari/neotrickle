#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build all release artifacts from the current source tree without publishing.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir="${root}/dist"
arch="${NTRICKLE_ARCH:-$(uname -m)}"
build_debian=1
build_portable=1
build_python=1
python_bin="${PYTHON:-python3}"

while (($#)); do
  case "$1" in
    --out-dir) out_dir=$2; shift 2 ;;
    --arch) arch=$2; shift 2 ;;
    --skip-debian) build_debian=0; shift ;;
    --skip-portable) build_portable=0; shift ;;
    --skip-python) build_python=0; shift ;;
    -h|--help)
      echo "usage: $0 [--out-dir DIR] [--arch ARCH] [--skip-debian] [--skip-portable] [--skip-python]"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

cd "${root}"
version=$(sed -n 's/^project(ntrickle VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
commit=$(git rev-parse HEAD)
if [[ -z "${version}" ]]; then
  echo "could not determine project version" >&2
  exit 1
fi
if [[ "${NTRICKLE_ALLOW_DIRTY:-0}" != "1" && -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "working tree is dirty; commit changes or set NTRICKLE_ALLOW_DIRTY=1" >&2
  exit 1
fi

rm -rf "${out_dir}"
mkdir -p "${out_dir}/source"

source_build="${root}/build-release-source"
rm -rf "${source_build}"
cmake -S "${root}" -B "${source_build}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNTRICKLE_ENABLE_TESTS=OFF \
  -DNTRICKLE_ENABLE_INTEGRATION_TESTS=OFF
(
  cd "${source_build}"
  cpack --config CPackSourceConfig.cmake -B "${out_dir}/source"
)
rm -rf "${out_dir}/source/_CPack_Packages"

if ((build_portable)); then
  "${root}/scripts/build-portable.sh" --out-dir "${out_dir}/portable" --arch "${arch}"
fi

if ((build_python)); then
  "${root}/scripts/build-wheel.sh" --out-dir "${out_dir}/wheel" --python "${python_bin}"
fi

if ((build_debian)); then
  "${root}/scripts/build-debian.sh" --out-dir "${out_dir}/debian"
fi

"${python_bin}" "${root}/scripts/generate_manifest.py" \
  --dist "${out_dir}" \
  --version "${version}" \
  --commit "${commit}"

echo "release artifacts for ${version} (${commit}) are in ${out_dir}"
