#!/usr/bin/env python3
"""Validate release artifacts without installing them system-wide."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

SYSTEM_LIB_RE = re.compile(
    r"^(linux-vdso|ld-linux|libc\.so|libm\.so|libpthread\.so|libdl\.so|"
    r"librt\.so|libgcc_s\.so|libstdc\+\+\.so)"
)
BUILD_PATH_RE = re.compile(rb"/(?:home|build-src|tmp)/[^\x00\s\"']+")
errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


def run(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_build_paths(path: Path) -> None:
    data = path.read_bytes()
    if b"/build-src/" in data or b"/home/" in data:
        fail(f"{path}: contains a build-machine path")


def check_elf(path: Path, *, require_origin: bool = False) -> None:
    result = run("readelf", "-d", str(path))
    if result.returncode == 0:
        dynamic = result.stdout
        if require_origin:
            if "RPATH" not in dynamic and "RUNPATH" not in dynamic:
                fail(f"{path}: missing RPATH/RUNPATH")
            if "$ORIGIN" not in dynamic:
                fail(f"{path}: RPATH/RUNPATH does not use $ORIGIN")
            if re.search(r"RPATH.*/(?:home|build-src|tmp)/", dynamic):
                fail(f"{path}: RPATH/RUNPATH contains a build path")
    check_build_paths(path)


def check_source_archive(path: Path, *, sdist: bool) -> None:
    with tarfile.open(path) as archive:
        names = archive.getnames()
        joined = "\n".join(names)
        required = ["CMakeLists.txt", "src/preload", "src/ntrickled", "pyproject.toml"]
        if not sdist:
            required.append("CMakeLists.txt")
        for item in required:
            if item not in joined:
                fail(f"{path}: missing {item}")
        for name in names:
            stripped = name.rstrip("/")
            components = stripped.split("/")
            directories = components if name.endswith("/") else components[:-1]
            if any(
                component in {".git", ".venv", "build"}
                or component.startswith(("build-", "build_", "obj-"))
                for component in directories
            ):
                fail(f"{path}: contains build/VCS directory {name}")
                break
        if sdist and "python/neotrickle/_launcher.py" not in joined:
            fail(f"{path}: missing Python launcher")


def check_wheel(path: Path, expected_arch: str) -> None:
    with zipfile.ZipFile(path) as wheel:
        names = wheel.namelist()
        joined = "\n".join(names)
        if "/__pycache__/" in joined:
            fail(f"{path}: contains __pycache__")
        wheel_meta = next((n for n in names if n.endswith(".dist-info/WHEEL")), None)
        if wheel_meta is None:
            fail(f"{path}: missing WHEEL metadata")
        else:
            text = wheel.read(wheel_meta).decode()
            if "Root-Is-Purelib: true" in text:
                fail(f"{path}: native wheel is marked purelib")
            tags = [line.split(":", 1)[1].strip() for line in text.splitlines() if line.startswith("Tag:")]
            if not tags:
                fail(f"{path}: no wheel tags")
            for tag in tags:
                if not tag.startswith("py3-none-"):
                    fail(f"{path}: unexpected wheel tag {tag}")
                if "manylinux" not in tag and os.environ.get("NTRICKLE_ALLOW_LINUX_TAG") != "1":
                    fail(f"{path}: wheel tag {tag} is not manylinux")
                if expected_arch and expected_arch not in tag:
                    fail(f"{path}: wheel tag {tag} does not match {expected_arch}")
        if not any(n.startswith("neotrickle/_native/") for n in names):
            fail(f"{path}: missing neotrickle/_native package data")
        for item in (
            "neotrickle/_native/bin/trickle",
            "neotrickle/_native/bin/trickled",
            "neotrickle/_launcher.py",
        ):
            if item not in joined:
                fail(f"{path}: missing {item}")
        if not any("libntrickle_preload.so" in n for n in names):
            fail(f"{path}: missing preload library")
        if ".libs/" in joined and "share/licenses/ntrickle/bundled/BUNDLED_LIBRARIES.md" not in joined:
            fail(f"{path}: bundled libraries lack license notices")
        with tempfile.TemporaryDirectory() as tmp:
            for name in names:
                if ".so" not in name and "/bin/" not in name:
                    continue
                if name.endswith("/"):
                    continue
                target = Path(tmp) / Path(name).name
                target.write_bytes(wheel.read(name))
                check_elf(target, require_origin="/bin/" in name)


def check_deb(path: Path) -> None:
    info = run("dpkg-deb", "-f", str(path))
    if info.returncode != 0:
        fail(f"{path}: cannot read control metadata")
        return
    fields = {
        key.strip(): value.strip()
        for key, value in (line.split(":", 1) for line in info.stdout.splitlines() if ":" in line)
    }
    if fields.get("Package") != "trickle":
        fail(f"{path}: package is not named trickle")
    for dependency in ("libc6", "libevent", "libncurses", "libtinfo"):
        if dependency not in fields.get("Depends", ""):
            fail(f"{path}: missing runtime dependency {dependency}")
    listing = run("dpkg-deb", "-c", str(path))
    if listing.returncode != 0:
        fail(f"{path}: cannot list contents")
        return
    contents = listing.stdout
    for item in (
        "./usr/bin/trickle",
        "./usr/bin/trickled",
        "libntrickle_preload.so",
        "ntrickled.service",
        "man1/trickle.1",
        "man8/trickled.8",
        "share/doc/trickle/README.Debian",
    ):
        if item not in contents:
            fail(f"{path}: missing {item}")
    if "./etc/" in contents:
        fail(f"{path}: package writes active configuration under /etc")
    with tempfile.TemporaryDirectory() as tmp:
        result = run("dpkg-deb", "-x", str(path), tmp)
        if result.returncode != 0:
            fail(f"{path}: cannot extract")
        else:
            for binary in (Path(tmp) / "usr/bin/trickle", Path(tmp) / "usr/bin/trickled"):
                if binary.exists():
                    check_elf(binary, require_origin=False)


def check_portable(path: Path, host_arch: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(path) as archive:
            archive.extractall(tmp)
            names = archive.getnames()
        top = next((Path(n).parts[0] for n in names if n.endswith("/bin/trickle")), None)
        if top is None:
            fail(f"{path}: missing bin/trickle")
            return
        prefix = Path(tmp) / top
        for item in (
            "bin/trickle",
            "bin/trickled",
            "libntrickle_preload.so",
            "share/licenses/ntrickle/bundled/BUNDLED_LIBRARIES.md",
        ):
            if not any(item in name for name in names):
                fail(f"{path}: missing {item}")
        for binary in (prefix / "bin/trickle", prefix / "bin/trickled"):
            if binary.exists():
                check_elf(binary, require_origin=True)
        for preload in prefix.rglob("libntrickle_preload.so"):
            check_elf(preload, require_origin=False)
        if host_arch == path.name.rsplit("-linux-", 1)[-1].removesuffix(".tar.gz"):
            result = run(str(prefix / "bin/trickle"), "-V")
            if result.returncode != 0 or "trickle version" not in result.stdout:
                fail(f"{path}: cannot execute: {result.stderr}")
            result = run(str(prefix / "bin/trickle"), "-m", "standalone", "-d", "1M", "--", "true")
            if result.returncode != 0:
                fail(f"{path}: standalone execution failed: {result.stderr}")
            ldd = run("ldd", str(prefix / "bin/trickle"))
            for line in ldd.stdout.splitlines():
                if "=> not found" in line:
                    fail(f"{path}: unresolved dependency: {line.strip()}")
                if "libntrickle" in line:
                    fail(f"{path}: preload unexpectedly linked into lookup")


def check_manifest(dist: Path, version: str) -> None:
    manifest_path = dist / "manifest.json"
    if not manifest_path.exists():
        fail("manifest.json is missing")
        return
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("version") != version:
        fail(f"manifest version {manifest.get('version')} != {version}")
    listed = {item["path"] for item in manifest.get("artifacts", [])}
    for item in manifest.get("artifacts", []):
        target = dist / item["path"]
        if not target.exists():
            fail(f"manifest lists missing artifact {item['path']}")
        elif sha256(target) != item["sha256"]:
            fail(f"checksum mismatch for {item['path']}")
    for path in dist.rglob("*"):
        if not path.is_file() or path.name in {"manifest.json", "SHA256SUMS"}:
            continue
        if path.name.endswith(".sha256"):
            continue
        if str(path.relative_to(dist)) not in listed:
            fail(f"artifact not in manifest: {path.relative_to(dist)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dist", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--host-arch", default=os.uname().machine)
    args = parser.parse_args()
    dist = args.dist.resolve()

    check_manifest(dist, args.version)
    for path in sorted(dist.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix == ".whl":
            wheel_arch = args.host_arch
            if "aarch64" in path.name or "arm64" in path.name:
                wheel_arch = "aarch64"
            elif "x86_64" in path.name:
                wheel_arch = "x86_64"
            check_wheel(path, wheel_arch)
        elif path.suffix == ".deb":
            # debhelper emits a separate -dbgsym package for the debug files;
            # it does not carry the main package's binaries or dependencies.
            if "-dbgsym_" in path.name:
                continue
            check_deb(path)
        elif path.name.endswith("-Source.tar.gz"):
            check_source_archive(path, sdist=False)
        elif path.name.endswith(".tar.gz") and "-linux-" in path.name:
            check_portable(path, args.host_arch)
        elif path.name.endswith(".tar.gz") and not path.name.endswith(".orig.tar.gz"):
            check_source_archive(path, sdist=True)

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"validated artifacts in {dist}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
