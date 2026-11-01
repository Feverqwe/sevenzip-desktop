#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

project_root="$(python3 "$(dirname "${BASH_SOURCE[0]}")/lib/project_paths.py" \
  "$(dirname "${BASH_SOURCE[0]}")/..")"
cd "$project_root"
preset="${1:-dev}"
build_mode="${SEVENZIP_BUILD_MODE:-lite}"
case "$build_mode" in
  lite|debug|sanitize) ;;
  *) printf 'SEVENZIP_BUILD_MODE must be lite, debug or sanitize.\n' >&2; exit 2 ;;
esac
source "$project_root/scripts/lib/toolchain.sh"
sevenzip_select_toolchain "$preset"
if [[ "$preset" == vcpkg-* && ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
  printf 'Install the optional vcpkg checkout in %s before selecting this preset.\n' "$VCPKG_ROOT" >&2
  exit 2
fi
build_directory="$SEVENZIP_BUILD_ROOT/$preset"
build_only=0
if [[ "${2:-}" == --build-only ]]; then
  build_only=1
fi
if [[ "$preset" == vcpkg-* && "${SEVENZIP_VCPKG_QT_PCH:-ON}" == OFF ]]; then
  qt_overlay="$(python3 scripts/prepare-vcpkg-overlay.py --vcpkg "$VCPKG_ROOT" \
    --output "$VCPKG_ROOT/../overlays/qt-no-pch")"
  overlay_separator=:
  if [[ "${OS:-}" == Windows_NT ]]; then overlay_separator=';'; fi
  export VCPKG_OVERLAY_PORTS="$qt_overlay${VCPKG_OVERLAY_PORTS:+$overlay_separator$VCPKG_OVERLAY_PORTS}"
fi

# Keep MSVC /showIncludes encoding consistent even without English language resources.
if [[ "${OS:-}" == Windows_NT ]]; then
  export VSLANG="${VSLANG:-1033}"
  windows_code_page_output="$(chcp.com)"
  if [[ "$windows_code_page_output" =~ ([0-9]+)[^0-9]*$ ]]; then
    windows_code_page="${BASH_REMATCH[1]}"
    chcp.com 65001 >/dev/null
    trap 'chcp.com "$windows_code_page" >/dev/null' EXIT
  else
    printf 'Could not read the Windows console code page.\n' >&2
    exit 1
  fi
fi

compiler_cache=""
if command -v ccache >/dev/null 2>&1; then
  compiler_cache="$(command -v ccache)"
  if [[ "${OS:-}" == Windows_NT ]]; then
    compiler_cache="$(cygpath -m "$compiler_cache")"
  fi
  if [[ "$preset" == vcpkg-* ]]; then
    export CMAKE_C_COMPILER_LAUNCHER="${CMAKE_C_COMPILER_LAUNCHER:-$compiler_cache}"
    export CMAKE_CXX_COMPILER_LAUNCHER="${CMAKE_CXX_COMPILER_LAUNCHER:-$compiler_cache}"
    export CMAKE_OBJCXX_COMPILER_LAUNCHER="${CMAKE_OBJCXX_COMPILER_LAUNCHER:-$compiler_cache}"
  fi
fi

configure() {
  if [[ "$build_only" == 1 ]]; then
    if [[ ! -f "$build_directory/CMakeCache.txt" ]]; then
      printf 'Configure %s before using --build-only.\n' "$preset" >&2
      exit 2
    fi
    return
  fi
  set -- "-DSEVENZIP_BUILD_MODE=$build_mode" "$@"
  local diagnostic value
  for diagnostic in SEVENZIP_DEBUG_SYMBOLS SEVENZIP_ENABLE_SANITIZERS SEVENZIP_ENABLE_CLANG_TIDY SEVENZIP_COMPACT_TEST_BINARIES; do
    value="${!diagnostic:-}"
    case "$value" in
      '') set -- "-U$diagnostic" "$@" ;;
      [Oo][Nn]) set -- "-D$diagnostic=ON" "$@" ;;
      [Oo][Ff][Ff]) set -- "-D$diagnostic=OFF" "$@" ;;
      *) printf '%s must be ON or OFF.\n' "$diagnostic" >&2; exit 2 ;;
    esac
  done
  set -- "-DCMAKE_INSTALL_PREFIX=$SEVENZIP_ARTIFACT_ROOT/install/$preset" "$@"
  set -- "-DSEVENZIP_TEST_SCRATCH_DIR=$SEVENZIP_TEST_SCRATCH_ROOT/$preset" "$@"
  if [[ "$preset" == vcpkg-* ]]; then
    # The pinned vcpkg toolchain caches its root independently of its file path.
    set -- "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      "-DZ_VCPKG_ROOT_DIR=$VCPKG_ROOT" "$@"
  fi
  if [[ "$preset" == vcpkg-* && -n "${VCPKG_INSTALLED_DIR:-}" ]]; then
    set -- "-DVCPKG_INSTALLED_DIR=$VCPKG_INSTALLED_DIR" "$@"
  fi
  if [[ -n "${SEVENZIP_OUTPUT_ROOT:-}" ]]; then
    local output_directory="$SEVENZIP_OUTPUT_ROOT/$preset"
    set -- "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$output_directory" \
      "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$output_directory" \
      "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$output_directory" \
      "-DCMAKE_PDB_OUTPUT_DIRECTORY=$output_directory" "$@"
  fi
  if [[ -z "${CMAKE_TOOLCHAIN_FILE:-}" && -n "${CC:-}" && -n "${CXX:-}" ]]; then
    if [[ "$(uname -s)" == Darwin || "$(uname -s)" == Linux ]]; then
      set -- "-DCMAKE_C_COMPILER=$CC" "-DCMAKE_CXX_COMPILER=$CXX" "$@"
    fi
    if [[ "$(uname -s)" == Darwin ]]; then
      set -- "-DCMAKE_OBJCXX_COMPILER=${OBJCXX:-$CXX}" "$@"
    fi
  fi
  if [[ "${OS:-}" == Windows_NT && -n "$compiler_cache" ]]; then
    # MSVC shared /Zi databases cannot be cached. Embedded /Z7 retains debug
    # information in each object and lets ccache restore complete outputs.
    set -- -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded "$@"
  fi
  local managed_runtime
  managed_runtime="$(python3 scripts/lib/runtime_layout.py --root "$project_root" --preset "$preset")"
  set -- "-DSEVENZIP_CODEC_RUNTIME_DIR=$managed_runtime" "$@"
  local managed_sfx
  managed_sfx="$(python3 scripts/lib/runtime_layout.py --root "$project_root" --preset "$preset" --component sfx)"
  set -- "-DSEVENZIP_SFX_RUNTIME_DIR=$managed_sfx" "$@"
  local runtime_directory="${SEVENZIP_CODEC_MODULE_DIR:-$managed_runtime}"
  local runtime_module="$runtime_directory/7z.so"
  if [[ "${OS:-}" == Windows_NT ]]; then
    runtime_module="$runtime_directory/7z.dll"
  fi
  set -- "-DSEVENZIP_CODEC_MODULE=$runtime_module" "$@"
  if [[ -n "$compiler_cache" ]]; then
    set -- \
      "-DCMAKE_C_COMPILER_LAUNCHER=$compiler_cache" \
      "-DCMAKE_CXX_COMPILER_LAUNCHER=$compiler_cache" \
      "-DCMAKE_OBJCXX_COMPILER_LAUNCHER=$compiler_cache" "$@"
  fi
  if [[ "${SEVENZIP_SCRIPT_OUTPUT:-compact}" == verbose || "$preset" == vcpkg-* ]]; then
    cmake --preset "$preset" "$@"
  else
    cmake --preset "$preset" --log-level=WARNING "$@"
  fi
}

if [[ "$preset" != dev && "$preset" != release && "$preset" != modules-dev &&
      "$preset" != modules-gcc-dev && "$preset" != modules-linux-dev &&
      "$preset" != vcpkg-dev && "$preset" != vcpkg-release ]]; then
    printf 'Usage: %s [dev|release|modules-dev|modules-gcc-dev|modules-linux-dev|vcpkg-dev|vcpkg-release] [target ...]\n' "$0" >&2
  exit 2
fi

if [[ "$(uname -s)" == Darwin ]]; then
  compiler_probe="${CXX:-c++}"
  if [[ "$preset" == modules-gcc-dev ]]; then
    sdk_path="$(xcrun --show-sdk-path)"
    export CFLAGS="${CFLAGS:+$CFLAGS }-isysroot $sdk_path"
    export CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-isysroot $sdk_path"
    export LDFLAGS="${LDFLAGS:+$LDFLAGS }-isysroot $sdk_path"
    if ! printf '#include <expected>\n' | "$compiler_probe" -isysroot "$sdk_path" \
        -x c++ -std=c++23 -fsyntax-only - >/dev/null 2>&1; then
      printf 'The selected compiler cannot use the macOS SDK C++23 headers.\n' >&2
      exit 1
    fi
  elif ! printf '#include <expected>\n' | "$compiler_probe" -x c++ -std=c++23 -fsyntax-only - >/dev/null 2>&1; then
    sdk_path="$(xcrun --show-sdk-path)"
    sdk_cxx_headers="$sdk_path/usr/include/c++/v1"
    if [[ ! -f "$sdk_cxx_headers/expected" ]]; then
      printf 'C++23 standard headers were not found in the macOS SDK.\n' >&2
      exit 1
    fi
    export CPLUS_INCLUDE_PATH="$sdk_cxx_headers${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
  fi

  if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    # A different SDK must not inherit cached package locations from Homebrew.
    # Keep the build tree and compiler cache; refresh only Qt package discovery.
    configure -U 'Qt6*_DIR' "-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
  else
    configure -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
  fi
else
  configure
fi

if [[ "$#" -gt 0 ]]; then
  shift
fi
if [[ "$build_only" == 1 ]]; then
  shift
fi
if [[ "${1:-}" == --configure-only ]]; then
  if [[ "$#" != 1 ]]; then
    printf '%s: --configure-only does not accept targets.\n' "$0" >&2
    exit 2
  fi
  exit 0
fi
if [[ "${SEVENZIP_SCRIPT_OUTPUT:-compact}" == verbose ]]; then
  if [[ "$#" == 0 ]]; then set -- sevenzip_desktop; fi
  cmake --build --preset "$preset" --target "$@" --verbose
elif [[ "$#" -gt 0 ]]; then
  cmake --build --preset "$preset" --target "$@" -- --quiet
else
  cmake --build --preset "$preset" --target sevenzip_desktop -- --quiet
fi
