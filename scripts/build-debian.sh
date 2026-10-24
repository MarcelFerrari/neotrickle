#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build binary and source Debian packages from a clean temporary copy.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir="${root}/dist/debian"

while (($#)); do
  case "$1" in
    --out-dir) out_dir=$2; shift 2 ;;
    -h|--help)
      echo "usage: $0 [--out-dir DIR]"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

for tool in dpkg-buildpackage dpkg-source dpkg-parsechangelog rsync; do
  command -v "$tool" >/dev/null || {
    echo "missing Debian build tool: $tool" >&2
    exit 1
  }
done

version=$(dpkg-parsechangelog -S Version)
upstream=${version%%-*}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "${out_dir}"

rsync -a \
  --exclude '.git' \
  --exclude '.venv' \
  --exclude 'dist' \
  --exclude '/build/' \
  --exclude '/build-*/' \
  --exclude '/build_*/' \
  --exclude '/obj-*/' \
  --exclude '.pytest_cache' \
  --exclude '__pycache__' \
  --exclude '*.pyc' \
  --exclude 'debian/.debhelper' \
  --exclude 'debian/trickle' \
  --exclude 'debian/files' \
  --exclude 'debian/debhelper-build-stamp' \
  "${root}/" "${tmp}/neotrickle/"

(
  cd "${tmp}/neotrickle"
  DEB_BUILD_OPTIONS="nocheck" dpkg-buildpackage -b -us -uc -d
)

# Build the Debian source package using an upstream orig tarball that excludes
# debian/ and local build output.
mkdir -p "${tmp}/upstream"
rsync -a \
  --exclude '.git' \
  --exclude '.venv' \
  --exclude 'debian' \
  --exclude 'dist' \
  --exclude '/build/' \
  --exclude '/build-*/' \
  --exclude '/build_*/' \
  --exclude '/obj-*/' \
  --exclude '.pytest_cache' \
  --exclude '__pycache__' \
  --exclude '*.pyc' \
  "${root}/" "${tmp}/upstream/"
mv "${tmp}/upstream" "${tmp}/neotrickle-${upstream}"
tar --owner=0 --group=0 --numeric-owner -C "${tmp}" -czf \
  "${tmp}/neotrickle_${upstream}.orig.tar.gz" "neotrickle-${upstream}"
(
  cd "${tmp}/neotrickle"
  dpkg-buildpackage -S -us -uc -d
)

find "${tmp}" -maxdepth 1 -type f \
  \( -name '*.deb' -o -name '*.dsc' -o -name '*.debian.tar.*' -o \
     -name '*.orig.tar.*' -o -name '*.buildinfo' -o -name '*.changes' \) \
  -exec cp --preserve=timestamps {} "${out_dir}/" \;

echo "Debian artifacts in ${out_dir}"
ls -l "${out_dir}"
