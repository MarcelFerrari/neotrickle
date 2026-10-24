#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a signed apt repository from already-built .deb files.  This script
# creates repository contents only; it does not upload or host them.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
deb_dir="${root}/dist/debian"
out_dir="${root}/dist/apt"
suite="bookworm"
component="main"
arch="amd64"
origin="neotrickle"
label="neotrickle"
key_id="${APT_GPG_KEY_ID:-}"

while (($#)); do
  case "$1" in
    --deb-dir) deb_dir=$2; shift 2 ;;
    --out-dir) out_dir=$2; shift 2 ;;
    --suite) suite=$2; shift 2 ;;
    --component) component=$2; shift 2 ;;
    --arch) arch=$2; shift 2 ;;
    --origin) origin=$2; shift 2 ;;
    --label) label=$2; shift 2 ;;
    --key-id) key_id=$2; shift 2 ;;
    -h|--help)
      echo "usage: $0 [--deb-dir DIR] [--out-dir DIR] [--suite SUITE] [--arch ARCH] [--key-id KEY]"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

for tool in dpkg-scanpackages apt-ftparchive gpg; do
  command -v "$tool" >/dev/null || {
    echo "missing apt repository tool: $tool" >&2
    exit 1
  }
done
if [[ -z "$key_id" ]]; then
  echo "APT_GPG_KEY_ID or --key-id is required; unsigned repositories are not produced" >&2
  exit 1
fi
shopt -s nullglob
debs=("${deb_dir}"/*.deb)
if ((${#debs[@]} == 0)); then
  echo "no .deb files found in ${deb_dir}" >&2
  exit 1
fi

repo="${out_dir}/repo"
rm -rf "${repo}"
mkdir -p "${repo}/dists/${suite}/${component}/binary-${arch}" \
  "${repo}/pool/${suite}/${component}/t/trickle"

for deb in "${debs[@]}"; do
  package=$(dpkg-deb -f "$deb" Package)
  version=$(dpkg-deb -f "$deb" Version)
  deb_arch=$(dpkg-deb -f "$deb" Architecture)
  if [[ "$deb_arch" != "$arch" && "$deb_arch" != "all" ]]; then
    echo "skipping ${deb}: architecture ${deb_arch} (want ${arch})" >&2
    continue
  fi
  install -m 0644 "$deb" "${repo}/pool/${suite}/${component}/t/${package}/${package}_${version}_${deb_arch}.deb"
done

(
  cd "${repo}"
  dpkg-scanpackages --multiversion "pool/${suite}/${component}" /dev/null \
    > "dists/${suite}/${component}/binary-${arch}/Packages"
  gzip -9 -c "dists/${suite}/${component}/binary-${arch}/Packages" \
    > "dists/${suite}/${component}/binary-${arch}/Packages.gz"
)

apt-ftparchive \
  -o "APT::FTPArchive::Release::Origin=${origin}" \
  -o "APT::FTPArchive::Release::Label=${label}" \
  -o "APT::FTPArchive::Release::Suite=${suite}" \
  -o "APT::FTPArchive::Release::Codename=${suite}" \
  -o "APT::FTPArchive::Release::Architectures=${arch}" \
  -o "APT::FTPArchive::Release::Components=${component}" \
  -o "APT::FTPArchive::Release::Description=${label} apt repository" \
  release "${repo}/dists/${suite}" > "${repo}/dists/${suite}/Release"

gpg --batch --yes --local-user "${key_id}" --clearsign \
  -o "${repo}/dists/${suite}/InRelease" "${repo}/dists/${suite}/Release"
gpg --batch --yes --local-user "${key_id}" --armor --detach-sign \
  -o "${repo}/dists/${suite}/Release.gpg" "${repo}/dists/${suite}/Release"

echo "signed apt repository in ${repo}"
echo "suite=${suite} component=${component} arch=${arch}"
