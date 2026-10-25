#!/usr/bin/env python3
"""Copy non-system runtime libraries into a relocatable neotrickle tree.

This is used by the portable archive builder.  It deliberately does not touch
LD_LIBRARY_PATH: bundled libraries live under <prefix>/lib*/ntrickle and are
found through the executable's private RPATH.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SYSTEM_LIBRARIES = re.compile(
    r"^(linux-vdso|ld-linux|libc\.so|libm\.so|libpthread\.so|libdl\.so|"
    r"librt\.so|libgcc_s\.so|libstdc\+\+\.so)"
)


@dataclass(frozen=True)
class Bundled:
    soname: str
    source: Path
    package: str
    license_source: Path | None


def run(*args: str) -> str:
    result = subprocess.run(args, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def ldd_dependencies(binary: Path) -> dict[str, Path]:
    output = run("ldd", str(binary))
    deps: dict[str, Path] = {}
    for line in output.splitlines():
        line = line.strip()
        if "=>" not in line:
            continue
        name, target = line.split("=>", 1)
        name = name.strip()
        target = target.strip().split(" (", 1)[0]
        if not target or target == "not found":
            continue
        deps[name] = Path(target)
    return deps


def license_for(package: str) -> Path | None:
    candidates = [
        Path("/usr/share/doc") / package / "copyright",
        Path("/usr/share/licenses") / package,
    ]
    if package.endswith("-libs"):
        candidates.append(
            Path("/usr/share/licenses") / (package.removesuffix("-libs") + "-base")
        )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def package_for(path: Path) -> tuple[str, Path | None]:
    resolved = str(path.resolve())
    if shutil.which("dpkg"):
        result = subprocess.run(
            ("dpkg", "-S", resolved), text=True, capture_output=True, check=False
        )
        if result.returncode == 0:
            package = result.stdout.strip().splitlines()[0].split(":", 1)[0]
            return package, license_for(package)
    if shutil.which("rpm"):
        result = subprocess.run(
            ("rpm", "-qf", "--qf", "%{NAME}", resolved),
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode == 0:
            package = result.stdout.strip().splitlines()[0]
            return package, license_for(package)
    return "unknown", None


def copy_with_package_notice(lib: Path, destination: Path) -> Bundled:
    package, license_source = package_for(lib)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(lib.resolve(), destination)
    return Bundled(destination.name, lib.resolve(), package, license_source)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <portable-prefix>", file=sys.stderr)
        return 2

    prefix = Path(sys.argv[1]).resolve()
    binaries = [prefix / "bin" / "trickle", prefix / "bin" / "trickled"]
    missing = [str(path) for path in binaries if not path.is_file()]
    if missing:
        print(f"missing binaries: {', '.join(missing)}", file=sys.stderr)
        return 1

    if (prefix / "lib64" / "ntrickle").exists():
        libdir = prefix / "lib64" / "ntrickle"
    elif (prefix / "lib" / "ntrickle").exists():
        libdir = prefix / "lib" / "ntrickle"
    else:
        libdir = prefix / "lib" / "ntrickle"
    libdir.mkdir(parents=True, exist_ok=True)

    pending: list[Path] = list(binaries)
    seen: set[Path] = set()
    bundled: list[Bundled] = []

    while pending:
        binary = pending.pop()
        for soname, source in ldd_dependencies(binary).items():
            if SYSTEM_LIBRARIES.match(soname):
                continue
            if source in seen:
                continue
            seen.add(source)
            notice = copy_with_package_notice(source, libdir / soname)
            bundled.append(notice)
            pending.append(notice.source)

    if not bundled:
        print("no non-system runtime dependencies found", file=sys.stderr)
        return 1

    patchelf = shutil.which("patchelf")
    if patchelf is None:
        print(
            "patchelf is required to make bundled libraries resolve each other "
            "through $ORIGIN",
            file=sys.stderr,
        )
        return 1
    for item in bundled:
        run(patchelf, "--set-rpath", "$ORIGIN", str(libdir / item.soname))

    notices_dir = prefix / "share" / "licenses" / "ntrickle" / "bundled"
    notices_dir.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Bundled runtime libraries",
        "",
        "These libraries are bundled only in the portable archive and are",
        "loaded privately from `lib/ntrickle` through an RPATH. They are not",
        "exported through LD_LIBRARY_PATH.",
        "",
        "| Library | Package | License source |",
        "|---|---|---|",
    ]
    for item in bundled:
        license_name = item.license_source.name if item.license_source else "MISSING"
        lines.append(f"| `{item.soname}` | `{item.package}` | `{license_name}` |")
        if item.license_source is None:
            print(
                f"cannot find license metadata for {item.soname} ({item.package})",
                file=sys.stderr,
            )
            return 1
        destination = notices_dir / f"{item.soname}-{item.package}"
        if item.license_source.is_dir():
            shutil.copytree(item.license_source, destination, dirs_exist_ok=True)
        else:
            shutil.copy2(item.license_source, destination)
    (notices_dir / "BUNDLED_LIBRARIES.md").write_text("\n".join(lines) + "\n")

    print(f"bundled {len(bundled)} libraries into {libdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
