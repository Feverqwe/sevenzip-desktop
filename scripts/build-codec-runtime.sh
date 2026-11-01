#!/usr/bin/env bash
# Prepare the official dynamic console engine and module for external plugins.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_root/scripts/lib/toolchain.sh"
sevenzip_select_toolchain dev runtime
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL-$(python3 -c 'import os; print(os.cpu_count() or 1)')}"
if [[ ! "$CMAKE_BUILD_PARALLEL_LEVEL" =~ ^[1-9][0-9]*$ ]]; then
  printf 'CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer.\n' >&2
  exit 2
fi
artifact_dir="${1:-$SEVENZIP_ARTIFACT_ROOT/dev/codec-runtime}"
python3 -B "$project_root/scripts/lib/build_paths.py" check-output --root "$project_root" --output "$artifact_dir" >/dev/null
case "$(uname -s)" in
  Darwin|Linux) ;;
  *) printf 'Unix runtime builder; Windows uses official 7z.exe and 7z.dll.\n' >&2; exit 1 ;;
esac
mkdir -p "$artifact_dir"
python3 - "$artifact_dir" <<'PY'
import json, pathlib, sys
directory = pathlib.Path(sys.argv[1])
files = [directory / name for name in ("7z", "7z.so", "runtime.json", "LICENSE.txt")]
if any(path.exists() or path.is_symlink() for path in files):
    if any(path.is_symlink() for path in files):
        raise SystemExit("Refusing to replace a symlink codec runtime")
    manifest = directory / "runtime.json"
    if not manifest.is_file() or json.loads(manifest.read_text()).get("producer") != "sevenzip-desktop/build-codec-runtime":
        raise SystemExit("Refusing to replace an unowned codec runtime")
PY
snapshot="$artifact_dir/.build-runtime"
python3 "$project_root/scripts/upstream_baseline.py" runtime-snapshot \
  --snapshot "$snapshot"
if [[ "$(uname -s)" == Darwin ]]; then
  if ! printf '#include <type_traits>\n' | "${CXX:-c++}" -x c++ -std=c++23 -fsyntax-only - >/dev/null 2>&1; then
    task_codec_sdk="$(xcrun --show-sdk-path)"
    export CPLUS_INCLUDE_PATH="$task_codec_sdk/usr/include/c++/v1${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
  fi
fi
task_runtime_cc="${CC:-cc}"
task_runtime_cxx="${CXX:-c++}"
if command -v ccache >/dev/null 2>&1; then
  task_runtime_cc="ccache $task_runtime_cc"
  task_runtime_cxx="ccache $task_runtime_cxx"
fi
python3 "$project_root/scripts/build-native-dependencies.py" --snapshot "$snapshot"
for source in CPP/7zip/UI/Console CPP/7zip/Bundles/Format7zF; do
  make -s -C "$snapshot/$source" -f sevenzip-runtime.gcc.mak \
    CC="$task_runtime_cc" CXX="$task_runtime_cxx" LFLAGS_STRIP= -j"$CMAKE_BUILD_PARALLEL_LEVEL"
done
python3 - "$project_root/docs/upstream-baseline.json" "$snapshot" "$artifact_dir" <<'PY'
import hashlib, json, pathlib, platform, re, shutil, sys
baseline = json.loads(pathlib.Path(sys.argv[1]).read_text())
snapshot, output = map(pathlib.Path, sys.argv[2:])
sources = {"7z": "CPP/7zip/UI/Console/_o/7z",
           "7z.so": "CPP/7zip/Bundles/Format7zF/_o/7z.so"}
header = (snapshot / "C/7zVersion.h").read_text()
major, minor = [int(re.search(r"#define MY_VER_" + name + r"\s+(\d+)", header)[1])
                for name in ("MAJOR", "MINOR")]
files = {name: {"source": path, "sha256": hashlib.sha256((snapshot / path).read_bytes()).hexdigest()}
         for name, path in sources.items()}
manifest = {"schema": 1, "producer": "sevenzip-desktop/build-codec-runtime",
            "repository": baseline["repository"], "revision": baseline["revision"],
            "system": platform.system(), "architecture": platform.machine(),
            "adapted_sources": json.loads((snapshot / "runtime-adaptations.json").read_text()),
            "version": (major << 16) | minor, "files": files}
previous = output / "runtime.json"
retired = []
if previous.is_file():
    record = json.loads(previous.read_text())
    if record.get("producer") != "sevenzip-desktop/build-codec-runtime":
        raise SystemExit("Refusing to retire artifacts from an unowned runtime")
    for name, metadata in record.get("files", {}).items():
        if name in sources:
            continue
        path = output / name
        if pathlib.Path(name).name != name or path.is_symlink():
            raise SystemExit("Unsafe retired runtime artifact")
        if path.exists():
            if hashlib.sha256(path.read_bytes()).hexdigest() != metadata.get("sha256"):
                raise SystemExit("Refusing to remove a modified retired runtime artifact")
            retired.append(path)
for name, source in sources.items():
    staged = snapshot / name
    shutil.copy2(snapshot / source, staged)
    staged.replace(output / name)
shutil.copy2(snapshot / "DOC/License.txt", output / "LICENSE.txt")
record = snapshot / "runtime.json"
record.write_text(json.dumps(manifest, indent=2) + "\n")
record.replace(output / record.name)
for path in retired:
    path.unlink()
print("Prepared official external-codec runtime:", output)
print("Upstream:", baseline["revision"], "version:", f"{major}.{minor:02}")
PY
