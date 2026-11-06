#!/usr/bin/env python3
"""Archive a fresh CMake runtime install, with notices and without sources."""
import argparse
import sys
sys.dont_write_bytecode = True
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
from lib.build_paths import build_directory, checked_output, directory, environment
from lib.package_notices import notice_directory, write_notice_index

ROOT = Path(__file__).resolve().parents[1]
DEVELOPMENT_SUFFIXES = {".c", ".cpp", ".cc", ".h", ".hpp", ".mm", ".m",
                        ".py", ".cmake", ".a", ".lib", ".pdb", ".prl"}
DEVELOPMENT_DIRS = {"Source", "Sources", "src", "include", "Headers",
                    "PrivateHeaders", ".git", "tests", "cmake", "__pycache__"}
MACHO_MAGIC = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
               b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
               b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
               b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}


def qt_notices(archive, checksum, version, output):
    prefix = f"qtbase-everywhere-src-{version}"
    if archive.name != prefix + ".tar.xz":
        raise ValueError("Qt notice input must match the linked Qt version")
    digest = hashlib.sha256()
    with archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != checksum.read_text().split()[0]:
        raise ValueError("Qt notice input checksum mismatch")
    count = 0
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            if not member.isfile() or not member.name.startswith(prefix + "/LICENSES/"):
                continue
            relative = Path(member.name[len(prefix) + 1:])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("Unsafe Qt notice path")
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.extractfile(member) as stream:
                target.write_bytes(stream.read())
            count += 1
    if not count:
        raise ValueError("Qt notice input has no licenses")


def prune_development(root):
    for path in sorted(root.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        if (path.name in DEVELOPMENT_DIRS or path.suffix in DEVELOPMENT_SUFFIXES
                or path.suffix == ".dSYM"):
            if path.is_symlink() or path.is_file():
                path.unlink()
            elif path.is_dir():
                shutil.rmtree(path)
        elif path.name.startswith(("libqoffscreen.", "qoffscreen.", "libqminimal.", "qminimal.")):
            path.unlink()
        elif (path.parent.name == "imageformats" and path.is_file()
              and not any(name in path.name for name in ("qico", "qsvg"))):
            # UI resources are BMP/ICO; theme icons may use SVG. No image viewer.
            path.unlink()


def validate_payload(root):
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if (any(part in DEVELOPMENT_DIRS for part in relative.parts)
                or path.suffix in DEVELOPMENT_SUFFIXES
                or path.name in {"CMakeCache.txt", "compile_commands.json", "CMakeLists.txt"}
                or path.name.endswith((".tar.xz", ".tar.gz", ".zip"))):
            raise ValueError(f"Development payload in runtime package: {relative}")
        if path.is_symlink() and (not path.exists() or not path.resolve().is_relative_to(root.resolve())):
            raise ValueError(f"Broken or escaping runtime symlink: {relative}")


def imports(path):
    output = subprocess.check_output(["dumpbin.exe", "/dependents", str(path)], text=True)
    return re.findall(r"^\s+([^\s]+\.dll)\s*$", output, re.MULTILINE | re.IGNORECASE)


def deploy_crt(root, runtime):
    # Deploy the imported CRT dependency closure.
    for path in root.rglob("*.exe"):
        if path.name.lower().startswith("vc_redist."):
            path.unlink()
    candidates = {path.name.lower(): path for path in runtime.glob("*.dll")}
    queue = [path for path in root.rglob("*") if path.suffix.lower() in {".exe", ".dll"}]
    copied = set()
    while queue:
        for name in imports(queue.pop()):
            key = name.lower()
            if key in candidates and key not in copied:
                original = candidates[key]
                target = root / "bin" / original.name
                shutil.copy2(original, target)
                copied.add(key)
                queue.append(target)
    if "vcruntime140.dll" not in copied:
        raise ValueError("Missing imported VC runtime dependency")


def compact_and_sign(bundle):
    architecture = platform.machine()
    for path in bundle.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) not in MACHO_MAGIC:
                continue
        arches = subprocess.check_output(["lipo", "-archs", str(path)], text=True).split()
        if architecture not in arches:
            raise ValueError(f"Missing native runtime slice: {path}")
        if len(arches) > 1:
            thin = path.with_name(path.name + ".thin")
            subprocess.run(["lipo", str(path), "-thin", architecture, "-output", str(thin)], check=True)
            thin.replace(path)
        # Removing a signature first can leave __LINKEDIT padding rejected by
        # Xcode 16 strip. Let strip process the signed layout; sign_bundle
        # replaces the invalidated signatures and verifies the final payload.
        subprocess.run(["strip", "-x", "-no_code_signature_warning", str(path)], check=True)
    sign_bundle(bundle)


def sign_bundle(bundle):
    # Sign nested runtime code before its enclosing containers.
    # Include the console engine/module in Resources as nested runtime code.
    containers = []
    main_executable = bundle / "Contents/MacOS/Sevenzip"
    for path in bundle.rglob("*"):
        if path.is_symlink():
            continue
        if path.is_dir() and path.suffix in {".framework", ".bundle", ".app"}:
            containers.append(path)
        # Signing the main executable also signs its enclosing app, which must
        # wait until all nested code has a valid signature.
        if path.is_file() and path != main_executable:
            with path.open("rb") as stream:
                if stream.read(4) not in MACHO_MAGIC:
                    continue
            subprocess.run(["codesign", "--force", "--sign", "-",
                            "--timestamp=none", str(path)], check=True)
    # Signing/stripping changes the SFX bytes; discovery verifies this sidecar.
    sfx = bundle / "Contents/Resources/7zip/7zCon.sfx"
    record_path = sfx.with_suffix(".sfx.json")
    record = json.loads(record_path.read_text())
    record["sha256"] = hashlib.sha256(sfx.read_bytes()).hexdigest()
    record_path.write_text(json.dumps(record, indent=2) + "\n")
    for path in sorted(containers, key=lambda item: len(item.parts), reverse=True):
        subprocess.run(["codesign", "--force", "--sign", "-",
                        "--timestamp=none", str(path)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-",
                    "--timestamp=none", str(bundle)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)


def finalize_runtime(install, system):
    if system == "Darwin":
        bundle = install / "Sevenzip.app"
        executable_dir = bundle / "Contents/MacOS"
        executable = "Sevenzip"
    elif system == "Linux":
        executable_dir = install / "bin"
        executable = "sevenzip_desktop"
    if system in {"Darwin", "Linux"}:
        # Entry names select the original FM/GUI modes; share their executable.
        for alias in ("7zFM", "7zG"):
            path = executable_dir / alias
            path.unlink(missing_ok=True)
            path.symlink_to(executable)
    prune_development(install)
    write_notice_index(notice_directory(install, system))
    validate_payload(install)
    if system == "Darwin":
        compact_and_sign(bundle)


def install_local_runtime(build_dir, install, system):
    install.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".sevenzip-install-", dir=install.parent) as temporary:
        stage = Path(temporary)
        runtime = stage / "runtime"
        subprocess.run(["cmake", "--install", str(build_dir), "--prefix", str(runtime)], check=True)
        finalize_runtime(runtime, system)
        # A fresh payload excludes dependencies left by a previous Qt kit.
        previous = stage / "previous"
        if install.exists():
            install.rename(previous)
        try:
            runtime.rename(install)
        except OSError:
            if previous.exists():
                previous.rename(install)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--install-only", type=Path, metavar="DIRECTORY",
                        help="Prepare a local runtime installation without creating an archive")
    parser.add_argument("--qt-source", type=Path)
    parser.add_argument("--qt-sha256", type=Path)
    parser.add_argument("--icu-license", type=Path)
    parser.add_argument("--vc-runtime", type=Path)
    parser.add_argument("--vc-notices", type=Path)
    args = parser.parse_args()
    import os
    os.environ.update(environment(ROOT))
    build_input = args.build_dir or build_directory(ROOT, "release")
    args.build_dir = checked_output(ROOT, build_input)
    system = platform.system()
    if args.install_only is not None:
        install = checked_output(ROOT, args.install_only)
        install_local_runtime(args.build_dir, install, system)
        print(install)
        return
    if args.qt_source is None or args.qt_sha256 is None:
        parser.error("Archives require --qt-source and --qt-sha256")
    info = json.loads((args.build_dir / "PackageInfo.json").read_text())
    destination = directory(ROOT, "artifacts") / "packages"
    destination.mkdir(parents=True, exist_ok=True)
    if system == "Windows" and (args.vc_runtime is None or args.vc_notices is None):
        parser.error("Windows packages require --vc-runtime and --vc-notices")
    arch = {"AMD64": "x86_64", "aarch64": "arm64"}.get(platform.machine(), platform.machine())
    name = f"sevenzip-desktop-{info['version']}-{system.lower()}-{arch}"
    with tempfile.TemporaryDirectory(prefix="stage-", dir=destination) as temporary:
        stage = Path(temporary)
        install = stage / name
        subprocess.run(["cmake", "--install", str(args.build_dir), "--prefix", str(install)], check=True)
        if system == "Darwin":
            bundle = install / "Sevenzip.app"
        notices = notice_directory(install, system)
        qt_notices(args.qt_source, args.qt_sha256, info["qt_version"], notices / "Qt")
        if system == "Windows":
            deploy_crt(install, args.vc_runtime)
            shutil.copy2(args.vc_notices, notices / "Microsoft-VC-Redist.txt")
        if any("icu" in path.name.lower() for path in install.rglob("*")):
            if args.icu_license is None:
                raise ValueError("Deployed ICU needs its license input")
            shutil.copy2(args.icu_license, notices / "ICU-LICENSE")
        finalize_runtime(install, system)
        if system == "Darwin":
            archive = stage / (name + ".zip")
            subprocess.run(["ditto", "--norsrc", "-c", "-k", "--keepParent",
                            str(bundle), str(archive)], check=True)
        elif system == "Windows":
            archive = Path(shutil.make_archive(str(stage / name), "zip", stage, name))
        else:
            archive = stage / (name + ".tar.gz")
            with tarfile.open(archive, "w:gz") as stream:
                stream.add(install, arcname=name)
        archive.replace(destination / archive.name)
        print(destination / archive.name)


if __name__ == "__main__":
    main()
