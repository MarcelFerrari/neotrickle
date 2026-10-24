#!/usr/bin/env python3
"""Generate SHA256SUMS and a JSON manifest for release artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify(path: Path) -> tuple[str, str, str]:
    name = path.name
    arch = "all"
    if name.endswith(".deb"):
        parts = name.removesuffix(".deb").split("_")
        arch = parts[-1] if parts else "unknown"
        return "debian-binary", arch, "linux-glibc"
    if name.endswith(".dsc"):
        return "debian-source", "source", "linux-glibc"
    if name.endswith(".debian.tar.xz"):
        return "debian-source", "source", "linux-glibc"
    if name.endswith(".orig.tar.gz"):
        return "debian-orig", "source", "linux-glibc"
    if name.endswith(".whl"):
        parts = name.removesuffix(".whl").split("-")
        arch = parts[-1] if len(parts) >= 5 else "unknown"
        return "wheel", arch, "manylinux"
    if name.endswith("-Source.tar.gz"):
        return "source-archive", "source", "linux-glibc"
    if name.endswith(".tar.gz") and "-linux-" in name:
        arch = name.rsplit("-linux-", 1)[1].removesuffix(".tar.gz")
        return "portable-archive", arch, "linux-glibc"
    if name.endswith(".tar.gz"):
        return "python-sdist", "source", "linux-glibc"
    return "other", arch, "unknown"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dist", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    dist = args.dist.resolve()
    output = (args.output or dist).resolve()
    if not dist.is_dir():
        parser.error(f"dist directory does not exist: {dist}")

    artifacts = []
    for path in sorted(dist.rglob("*")):
        if not path.is_file():
            continue
        if path.name in {"SHA256SUMS", "manifest.json"} or path.name.endswith(".sha256"):
            continue
        kind, arch, platform = classify(path)
        artifacts.append(
            {
                "path": str(path.relative_to(dist)),
                "kind": kind,
                "architecture": arch,
                "platform": platform,
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )

    if not artifacts:
        parser.error("no artifacts found")

    checksums = "".join(f"{item['sha256']}  {item['path']}\n" for item in artifacts)
    (output / "SHA256SUMS").write_text(checksums)

    manifest = {
        "project": "neotrickle",
        "version": args.version,
        "commit": args.commit,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "platform_baseline": {
            "os": "linux",
            "libc": "glibc",
            "minimum_glibc": "2.28",
            "architectures": sorted(
                {item["architecture"] for item in artifacts if item["architecture"] != "all"}
            ),
        },
        "artifacts": artifacts,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output / "SHA256SUMS")
    print(output / "manifest.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
