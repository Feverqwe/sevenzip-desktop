#!/usr/bin/env python3
"""Build dependency modules for the independent native Make/NMake runtime."""

import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MODULE = "sevenzip.dependencies.qemu"


def write_changed(path, content):
    if path.is_symlink():
        raise ValueError("Refusing linked native dependency metadata: " + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != content:
        path.write_bytes(content)


def prepare(snapshot):
    snapshot = snapshot.resolve()
    if not (snapshot / ".sevenzip-runtime-source.json").is_file():
        raise ValueError("Prepare the owned native source snapshot first")
    build = snapshot.parent / ".dependency-modules"
    owner = build / ".sevenzip-native-dependencies.json"
    if build.is_symlink() or (build.exists() and any(build.iterdir()) and not owner.is_file()):
        raise ValueError("Refusing an unowned native dependency build directory")
    if owner.is_file() and json.loads(owner.read_text(encoding="utf-8")).get("producer") != "sevenzip-native-dependencies":
        raise ValueError("Unexpected native dependency build owner")
    write_changed(owner, b'{"producer":"sevenzip-native-dependencies"}\n')
    configure = ["cmake", "-S", str(ROOT / "tools/native-dependencies"), "-B", str(build),
                 "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DSEVENZIP_ENABLE_CLANG_TIDY=" + os.environ.get("SEVENZIP_ENABLE_CLANG_TIDY", "OFF"),
                 "-DSEVENZIP_RUNTIME_SOURCE_DIR=" + str(snapshot)]
    compiler = os.environ.get("CXX", "cl" if platform.system() == "Windows" else "c++")
    parts = [compiler] if Path(compiler).is_file() else shlex.split(compiler)
    configure.append("-DCMAKE_CXX_COMPILER=" + ";".join(parts))
    launcher = os.environ.get("CMAKE_CXX_COMPILER_LAUNCHER")
    if not launcher and (cache := shutil.which("ccache")):
        launcher = Path(cache).as_posix()
    if launcher:
        configure.append("-DCMAKE_CXX_COMPILER_LAUNCHER=" + launcher)
    if platform.system() == "Darwin":
        sdk = os.environ.get("SDKROOT") or subprocess.check_output(
            ["xcrun", "--show-sdk-path"], text=True).strip()
        configure += ["-DCMAKE_OSX_SYSROOT=" + sdk, "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4"]
    subprocess.run(configure, check=True)
    toolchain = json.loads((build / "toolchain.json").read_text(encoding="utf-8"))
    metadata = snapshot / "dependencies"
    if not toolchain["modules"]:
        for name in ("native-qemu.gcc.mak", "native-qemu.mak"):
            write_changed(metadata / name, b"# Dependency facade uses the C++ header fallback.\n")
        return
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel",
                    os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "4")], check=True)
    package = build / "package"
    subprocess.run(["cmake", "--install", str(build), "--config", "Release", "--prefix", str(package)],
                   check=True)
    installed = [Path(line) for line in (build / "install_manifest.txt").read_text(encoding="utf-8").splitlines()]
    modules = [path for path in installed if path.suffix in (".pcm", ".ifc", ".gcm")]
    if len(modules) != 1:
        raise ValueError("Expected one installed QEMU module interface")
    library = package / "lib" / Path((build / "facade-library.txt").read_text(encoding="utf-8").strip()).name
    if not library.is_file():
        raise ValueError("Native dependency archive is missing")
    bundle = snapshot / "CPP/7zip/Bundles/Format7zF"

    def relative(path):
        return Path(os.path.relpath(path, bundle)).as_posix()

    bmi, archive = relative(modules[0]), relative(library)
    write_changed(build / "module_cache_launcher.py", (ROOT / "scripts/lib/module_cache_launcher.py").read_bytes())
    support = relative(build / "module_cache_launcher.py")
    if toolchain["compiler"] == "MSVC":
        write_changed(metadata / "qemu.modmap", ('-reference "' + MODULE + '=' + bmi + '"\n').encode())
        content = (f'SEVENZIP_QEMU_ENABLED = 1\nSEVENZIP_QEMU_LIBRARY = "{archive}"\n'
                   f'SEVENZIP_QEMU_BMI = "{bmi}"\nSEVENZIP_QEMU_CACHE = "{support}"\n'
                   f'SEVENZIP_QEMU_PYTHON = "{Path(sys.executable).as_posix()}"\n')
        write_changed(metadata / "native-qemu.mak", content.encode())
    else:
        if toolchain["compiler"] == "GNU":
            write_changed(metadata / "qemu.gcc.modmap", (MODULE + ' ' + bmi + '\n').encode())
            flags = "-fmodules-ts -fmodule-mapper=../../../../dependencies/qemu.gcc.modmap"
        else:
            flags = "-fmodule-file=" + MODULE + "=" + bmi
        content = (f'SEVENZIP_QEMU_ENABLED = 1\nSEVENZIP_QEMU_LIBRARY = {shlex.quote(archive)}\n'
                   f'SEVENZIP_QEMU_BMI = {bmi}\n'
                   f'SEVENZIP_QEMU_FLAGS = -DSEVENZIP_USE_QEMU_MODULE {flags}\n')
        if toolchain["compiler"] == "GNU" and toolchain["version"].split(".")[0] == "16":
            # GCC 16 speculates unrelated COM implementations and diagnoses
            # their larger destructor/refcount fields outside the real object.
            content += ('$(O)/HandlerCont.o $(O)/RarHandler.o: '
                        'CXXFLAGS += -fno-devirtualize-speculatively\n')
        write_changed(metadata / "native-qemu.gcc.mak", content.encode())
    print("Prepared native QEMU module:", modules[0], flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.snapshot)


if __name__ == "__main__":
    main()
