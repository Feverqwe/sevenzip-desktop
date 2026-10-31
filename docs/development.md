# Development toolchain

Start with [dependencies](#dependencies) for setup, [local commands](#local-commands)
for focused builds/tests, or [automation](#automation) for optional remote runners.
Native runtime preparation and deployment requirements are described below.

Sevenzip is an independent application based on 7-Zip. The required
[source ownership model](architecture.md#module-directories-and-source-ownership)
keeps all 7-Zip build inputs in this repository. Unchanged 7-Zip sources are
archived locally and expanded into the build workspace; neither configuration
nor runtime/SFX preparation requires a separate upstream checkout or fetch.
Project module directory migration and final platform acceptance are pending.

The Qt kit must include Remote Objects and its `repc` compiler. Install the
`qtremoteobjects` module alongside the selected Qt version on all platforms.
Both GUI processes must come from the same build: their generated operation
interface is checked by Qt Remote Objects during connection. Qt's deployment
tools include the linked runtime module in private installs and packages.

The native panel keyboard adapter requires the private development headers of
the selected Qt GUI SDK (`Qt6::GuiPrivate`). Install those headers with the same
Qt build as Core/Gui/Widgets; on distribution SDKs this can be a separate private
development package. `qtnativepanelkeys.cpp` uses Qt's native keyboard-layout
mapping instead of maintaining another platform key map. This private API has
no ABI compatibility guarantee: build and deploy against the same Qt module
build, and rebuild the application when changing it. CMake suppresses Qt's generic
private-module warning and rejects a missing matching private SDK. Bundled packages must carry that
same Qt runtime; this adds no separate runtime module.

Native console SFX can be built from the recorded official baseline with
`./scripts/build-sfx.sh [artifact-directory]` on macOS/Linux.
The default output is the release preset's SFX deployment. The script extracts
an owned source snapshot, builds upstream SFXCon unchanged and writes
`7zCon.sfx`, its license and a SHA-256/source/host/version JSON record. It refuses
to replace an unowned module artifact. SFX builds retain their incremental outputs
and use the existing compiler cache. Source scope and acceptance evidence are
in [the port audit](upstream-port-audit.md). Windows uses
the official GUI `7z.sfx` runtime; this script does not create a Windows module.

For Windows, use `scripts/build-sfx.ps1 -Architecture arm64` (or `x64`/`x86`)
in a matching Visual Studio developer PowerShell. It requires Python,
MSVC/nmake, linker and resource compiler, snapshots the same recorded revision,
and builds unchanged `Bundles/SFXWin/makefile` with `PLATFORM`. Optional
`-OutputDirectory` selects the output.
It writes `7z.sfx`, `7z.sfx.json` and `7z-LICENSE.txt`, refuses unowned/reparse-point
artifacts, and uses the repository-owned source archive. The sidecar producer must
match the Windows builder. Use a scoped Visual Studio developer environment
rather than relying on the default SSH PATH.
`build.sh` requests process-local English MSVC diagnostics (`VSLANG=1033`)
unless supplied by the caller. It also switches its console to UTF-8 and restores
the previous code page on exit so CMake/Ninja parse `/showIncludes` reliably
even when English compiler resources are not installed.
This setting does not change application language. If stored header dependencies
appear damaged, collect diagnostics before proposing a cache/build-tree repair;
follow [cache retention](../AGENTS.md#validation-scope).

Archive operations require the official native codec runtime.
`./scripts/build-codec-runtime.sh [artifact-directory]`
builds `UI/Console` and `Bundles/Format7zF` from the pinned source
with the engine adaptations selected in the [baseline](upstream-baseline.json),
into the preset’s runtime deployment on macOS/Linux. It produces the reference `7z`, `7z.so`,
license and source/version/SHA records, and refuses unowned artifacts. The Debug
app uses this prepared runtime when present; deployed builds look beside the app
or in `Resources/7zip`. `SEVENZIP_CODEC_MODULE_PATH` explicitly selects a native
module. The console is shipped as an independent sidecar and used by reference
tests; neither 7zFM nor 7zG invokes it.
The console and application use the same deployed filesystem/format modules.
Build the corresponding CMake module targets as well as preparing the core;
copying the console and core alone does not provide the external readers.
Reader source ownership and shared probes are recorded in the
[source audit](upstream-port-audit/license-variants.md).
Codecs/Formats discovery and ABI checks follow original LoadCodecs in the selected
deployment, without executing or parsing console information output.
The immutable codec image uses Qt's selected temporary directory to copy and
load its module. That directory must permit writes and executable mappings.
On Linux with a `noexec` default temporary filesystem, a process-scoped `TMPDIR`
can select an approved private directory with those capabilities. The isolated
[runtime check](archive/qt-port/upstream-port-audit.md#p3-04--isolated-linux-runtime-and-temporary-directory-contract-2026-10-10)
verifies this override, cleanup and unchanged archives; it changes no system policy.

Build wrappers use persistent project storage for intermediates and temporary
data. They do not provision RAM disks. A Linux runner can use its existing shared
smbStorage through the [runner configuration](runner-configuration.md).

Build files, binaries, runtimes and reports are separated by branch and platform.
Shared-storage installations retain their established logical build paths while
generated outputs live in the project's namespace on the common mount. Linux
runner source copies may share that namespace. The local repository, installed
tools and persistent compiler/download caches remain on ordinary disk.
Use `scripts/lib/build_paths.py path --root "$PWD" --category work --preset dev`
to locate the selected profile's build trees. Disposable test settings and startup
data use project scratch storage. vcpkg profiles remain optional; native builds
install no manifest packages.

On macOS, installed host tools and the prebuilt Qt SDK are retained in the main
checkout's ignored `.codex/tools/` directory. They are installed once and reused
by worktrees. Project compilation, temporary data and compiler/package caches
use the storage selection above. The system Ninja is used normally.

Use the build wrapper after preparing a deployment: it selects that preset's
exact native module and refreshes stale module paths in the existing CMake cache.
Completed package evidence and its limits are linked from [acceptance](ux-port-audit.md).
On Windows, `scripts/build-codec-runtime.ps1 -Architecture arm64` (or `x64`/`x86`)
uses a matching developer environment and the same optional output arguments
as the SFX builder. It compiles unchanged Console/Format7zF makefiles and
produces the reference client, native module, license and runtime manifest.
Dependency facade interfaces also need CMake and Ninja. The native Make/NMake
build uses the same selected compiler for those interfaces and its C++ consumers;
the 7-Zip engine keeps its own build and C sources retain their language.
Both codec-runtime builders retain their source/build directory across runs,
preserve unchanged source timestamps and use the existing compiler cache when
available. Runtime provenance records the adapted-source hashes; changing one
invalidates the prepared-runtime cache. All source inputs come from this repository.
For engine-only checks, `python3 scripts/prepare-runtime.py --codec-only`
prepares the console and codec core without building SFX or external CMake modules.
Do not install test-only codec fixtures in a production runtime. Earlier ARM64
builder samples are retained in [the tooling archive](archive/qt-port/workflow-evidence.md).

Windows CMake installation places the Qt application, original7zFM/7zG entry
aliases, project shell DLL, official `7z.dll` and the independent `7z.exe` sidecar
together in `bin`.
`scripts/check-windows-package.sh` prepares a private installation of Qt runtime,
platform plugins and compiler runtime using Qt's own deployment script. It
first checks the prepared runtime's producer, repository, pinned revision,
architecture and binary SHA256 values. Runtime provenance and7-Zip license are
installed under `share`; installation does not register shell handlers or
write associations. `SEVENZIP_WINDOWS_RUNTIME` can select another prepared
runtime directory at configure time.

`scripts/check-windows-package.sh` builds and installs into the snapshot's
the branch’s package directory, then exercises its actual DLL and GUI helper with
only package/system paths in the child process environment. Run it on the
interactive Windows runner. This private test layout is not a release package
or final license/distribution acceptance; see [packaging scope](ci.md).

`scripts/check-installed-operations.sh` prepares a private runtime and runs the
focused operation and entrypoint checks against it. It uses Cocoa on macOS,
the interactive Windows desktop, or X11 on Linux (Xvfb if no display is supplied).
`--entry-only` checks the installed shell/GUI entrypoints without repeating the
operation suites. Both modes reuse the existing incremental build and runtime.
On Windows the ordinary interactive invocation checks registered shell launch;
run `--entry-only` separately with the runner's `--interactive --elevated` mode
for the startup/symlink suite. The wrapper selects these complementary scopes
from the actual token; see [runner workflows](hyperv-runners.md).
These checks do not establish rendered pixel parity or release acceptance.

## Dependencies

- C and C++23 compiler, CMake 3.28+, Ninja, Qt 6 Widgets, LinguistTools and
  Qt Base translations, Git, clang-format
- Python 3.9+ for CTest UI-reference checks (standard library only); clang/GCC
  additionally for extracting resources, not for comparing stored references
- Prepared native codec runtime and its reference `7z` client for tests
- Qt 6 DBus for the Linux launcher service; Unix launcher regression tests also
  need `dbus-daemon` to create their own private bus. macOS builds with testing
  enabled require Qt DBus and libdbus alongside `dbus-daemon`; Cocoa-only
  production builds do not. The two private-bus tests set the discovered libdbus
  directory in their own loader environment for official Qt kits.

macOS builds target macOS 14.4+ and use the official shared Qt 6.12.0 kit,
matching the runner version. Install the macOS desktop Qt Base SDK (including
private headers) with the [Qt installer](https://www.qt.io/development/download-qt-installer-oss)
under the local host-tools directory. Query it with
`python3 scripts/lib/build_paths.py path --root "$PWD" --category host-tools`;
the SDK belongs in its `qt/6.12.0/macos` subdirectory.
For an SDK installed elsewhere, set `CMAKE_PREFIX_PATH` to its `macos` directory.
Build and deploy with the same SDK. Homebrew supplies the other local tools:

```sh
brew install cmake ninja sevenzip clang-format ccache
```

The application now requires CMake 3.28 or newer and a C++23 compiler.
Windows requires MSVC 19.43+ (Visual Studio 2022 17.13+) for the dedicated
C++23 language mode. Older CMake mappings to `/std:c++latest` are bounded to
`/std:c++23preview`, including the independent native runtime/SFX build. This
keeps later experimental language features outside the project's standard;
see [Microsoft's standard-mode documentation](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version).

### Build diagnostics

Build wrappers default to `SEVENZIP_BUILD_MODE=lite`: fast Debug compilation
without debug symbols, sanitizers or Clang-Tidy. Warnings, Debug assertions and
input validation remain enabled. The Release preset still selects an optimized
application build without test targets.

Use `SEVENZIP_BUILD_MODE=debug` for debug symbols or `sanitize` for symbols plus
AddressSanitizer (and UndefinedBehaviorSanitizer on macOS/Linux). Individual
`SEVENZIP_DEBUG_SYMBOLS`, `SEVENZIP_ENABLE_SANITIZERS` and
`SEVENZIP_ENABLE_CLANG_TIDY` environment variables accept `ON` or `OFF` and
override the mode defaults. Clang-Tidy remains opt-in in every mode.
The same names are CMake cache options for direct CMake use. Wrappers reset
unspecified diagnostic overrides to the selected mode, so returning to an
ordinary command does not retain a previous investigation's expensive settings.
Build trees and compiler caches are reused.

For example, scope diagnostics to the needed target and its dependencies:

```sh
SEVENZIP_BUILD_MODE=debug ./scripts/build.sh dev worker_checkpoint_tests
SEVENZIP_ENABLE_SANITIZERS=ON python3 scripts/verify.py --tests '^worker_checkpoint$'
SEVENZIP_ENABLE_CLANG_TIDY=ON ./scripts/build.sh dev sevenzip_progress_sync
```

These options are forwarded to runners. Restrict expensive diagnostic builds to
the components and checks needed for the task; avoid `all` and the full gate
unless the investigation requires that scope. Sanitizer-enabled format modules
need a matching console host; prepare it with the same diagnostic options using
`scripts/prepare-runtime.py` or `verify.py --prepare-runtime`.

`SEVENZIP_UI_REPORTS=ON` enables widget JSON reports during the selected UI tests.
`SEVENZIP_CAPTURE_SCREENSHOTS=1` also saves PNGs and enables their JSON reports.
Normal geometry assertions run without saving these optional artifacts.
Reference-comparison fixtures enable the JSON they need as test input themselves.
`SEVENZIP_SCRIPT_OUTPUT=verbose` enables configuration output and compiler/linker
commands; warnings and errors remain visible in the default compact output.
Verification keeps its small result/timing summary and runner command logs for
failure diagnosis. A plain application build does not run tests or create UI reports.

Clang-Tidy requires Clang-Tidy on `PATH`. With Clang, the matching versioned executable
(for example, `clang-tidy-19`) is preferred; `clang-tidy` is also supported.
Named-module analysis requires a Clang compiler and Clang-Tidy from the same
LLVM release. Configuration rejects other BMI producers or a mismatched analyzer:
MSVC IFC and GCC GCM cannot be analyzed as Clang modules. Keep modules enabled
and use a matching Clang build for analysis. See
[Clang's module compatibility requirements](https://clang.llvm.org/docs/StandardCPlusPlusModules.html#clang-cl).
For the independent native runtime's dependency modules, set
`SEVENZIP_ENABLE_CLANG_TIDY=ON` when running `scripts/prepare-runtime.py` or the
codec-runtime builder. They use the same analysis configuration; switching this
mode revalidates the runtime while retaining incremental objects and compiler caches.
Changes to `.clang-tidy` are checked on the next analysis-enabled build.
Set `SEVENZIP_ENABLE_CLANG_TIDY=ON` when invoking build, verify or runner scripts
to select analysis for both the application and native dependency build. Use
`OFF` to disable it explicitly in an existing build tree. The same variable is
forwarded to Linux and Windows runners; their toolchains must provide Clang-Tidy.
On macOS, build, check, verify and runtime-preparation scripts automatically
select installed Homebrew LLVM 23+ for C, C++ and Objective-C++, including its
module scanner and analysis tools. `modules-gcc-dev` selects Homebrew GCC.
Explicit `CC`, `CXX`, `OBJCXX` and toolchain settings take priority. Linked
worktrees reuse the main checkout's Qt kit; their build outputs remain local.
The ordinary commands need no manual compiler or Qt path exports.
Native Make/NMake runtime and SFX builds also select C++23 for C++ sources;
upstream C and assembly sources retain their original language.
Atomic catalog ownership uses `std::atomic<std::shared_ptr<...>>` when the
standard library advertises support. Current libc++ lacks
[P0718R2](https://libcxx.llvm.org/Status/Cxx20.html); the catalog owner encapsulates
its standard atomic shared-pointer functions with the same publication semantics.
AppleClang 21's ASan retains global registrations after `dlclose`; a standalone
library reload reproduces an ODR failure, and codec snapshots can crash during
later loads. Configuration rejects this affected sanitizer toolchain.
The CTest wrapper defaults UBSan to stop on the first finding; an explicit
`UBSAN_OPTIONS` value can be supplied for a diagnostic run.
Instrumented tests receive three times their ordinary CTest deadline and
reserve two parallel scheduler slots to accommodate sanitizer overhead on
small runners. Windows development runtimes and isolated CLI fixtures receive
the selected compiler's CRT/ASan libraries beside the executable, preserving
the official console's restricted DLL search policy.
Development runtime preparation links the Unix console host with sanitizer
runtimes so instrumented format plugins can initialize safely. For a release
runtime use `prepare-runtime.py --preset release --sanitizer-runtime off`; `build-app.sh` selects
that mode automatically. This startup linkage does not instrument the separate
upstream engine's algorithm bodies.
Each preset deploys its own engine, SFX and format plugins, preventing GCC, Clang,
debug and release runtimes from overwriting another preset's deployment. The
ordinary `dev` commands retain their existing runtime location. Native build
outputs and compiler caches remain incremental. Use the same preset when
preparing, building and testing a runtime.
Startup sanitizer linkage follows the preset build type automatically; explicit
`--sanitizer-runtime on|off` remains available for diagnostics.
Native catalog discovery allows extra startup time in ASan builds for
instrumented format modules. Explicit discovery deadlines remain available for
diagnostics and cancellation checks.

GCC/libstdc++ assertions and libc++ extensive hardening are enabled for C++
and Objective-C++ sources in every configuration. Clang-Tidy treats detected
dangling handles, GSL lifetime and escaping-stack diagnostics as errors; LLVM 23+
also enables its data-flow lifetime analysis. These checks find defects but do
not establish Rust's ownership guarantees. See the official
[Clang lifetime analysis](https://clang.llvm.org/docs/LifetimeSafety.html) and
[libc++ hardening](https://libcxx.llvm.org/Hardening.html) documentation.

The root vcpkg manifest pins its registry and includes the Qt, formatting and
inference dependencies. To use it, install vcpkg at the pinned registry revision,
set `VCPKG_ROOT` to that checkout and build with the `vcpkg-dev` or
`vcpkg-release` preset. These presets have their own build trees. The ordinary
presets continue to use the selected Qt SDK and system dependencies. Custom
vcpkg ports belong under `ports/`; a patched port must include its complete
port recipe as required by vcpkg.

Windows manifest builds deploy the application's Qt plugins and their runtime
dependencies alongside the executable. Focused tests use the installed Qt
runtime matching their build configuration.

Linux Ninja builds with static Qt and sanitizers serialize linking to avoid
concurrent linkers exhausting memory; compilation remains parallel. An explicit
`CMAKE_JOB_POOL_LINK` selection takes precedence over the project default.

On Debian/Ubuntu, vcpkg bootstrap requires `curl`, `zip`, `unzip` and `tar` in
addition to the compiler/build tools below. Building the manifest's dependencies
from source also requires parser generators and autotools:

```sh
sudo apt install bison flex autoconf autoconf-archive automake libtool
```

On macOS, vcpkg source builds also need pkg-config and autotools:

```sh
brew install pkgconf autoconf autoconf-archive automake libtool
```

The build wrapper keeps package installation progress and diagnostics visible.
The macOS overlay triplets keep dependencies on the application's SDK/deployment
floor and pass through compiler launchers. SDK metadata participates in their ABI
cache keys.
With Apple Command Line Tools selected, desktop Qt builds retain the SDK version
checks and omit the full Xcode version check.
SDK include paths remain implicit in warm build trees, preserving libc++ header
ordering when packages such as Iconv are consumed.

Dependency facades use C++23 named modules when the selected compiler supplies
a scanner. Enable `SEVENZIP_ENABLE_DEPENDENCY_MODULES` explicitly to require
them; unsupported compilers then fail configuration. Otherwise those compilers
use the same facade API through headers. Vendored C libraries retain C compilation.
The `modules-dev` preset requires module support. On macOS the build scripts
select one installed Homebrew LLVM toolchain for C, C++ and Objective-C++ so
sanitizer runtimes match, and reuse the harness Qt kit:

```sh
brew install llvm
./scripts/build.sh modules-dev filesystem_zlib_tests sevenzip_squashfs
./scripts/ctest.sh --preset modules-dev -R '^filesystem_zlib$' --output-on-failure
```

On Ubuntu 24.04, install `clang-19`, `clang-tools-19`, `clang-tidy-19`,
`libclang-rt-19-dev` and use
`modules-linux-dev` to select that C/C++ compiler and its scanner. The wrappers
also select it for native runtime preparation; direct preparation accepts
`prepare-runtime.py --preset modules-linux-dev`. Clang 18 with the system
libstdc++ does not expose `std::expected`; this preset requires Clang 19. GCC 14.2
crashes while serializing the sanitized zlib facade; GCC module builds require
GCC 16 or newer. Focused runner checks accept the same preset:

```sh
python3 scripts/verify.py --preset modules-linux-dev --platform linux \
  --tests '^filesystem_zlib$'
```

MSVC module consumers retain ccache through a launcher that hashes the imported
BMI files. This accounts for interface changes that ccache does not track from
MSVC module references itself. Build trees and compiler caches remain incremental.
Interface producers use the compiler when Ninja needs to rebuild
them: the object-only ccache result cannot restore their additional IFC output
after a RAM-disk reset. Completed interfaces remain incremental build outputs;
ordinary sources and importers continue using the existing compiler cache.
This handles unsupported paired outputs without clearing caches or disabling
named modules. The [ccache module limitations](https://ccache.dev/manual/latest.html#_c_modules)
describe this distinction.

The separate C++23 module example requires module scanning, `std::views::chunk`
and `std::println`. On macOS, use Homebrew GCC through `modules-gcc-dev` for
these library features. The build scripts select the compiler and shared Qt kit:

```sh
brew install gcc
./scripts/build.sh modules-gcc-dev sevenzip_cpp23_example
"$(python3 scripts/lib/build_paths.py path --root "$PWD" --category artifacts)/modules-gcc-dev/sevenzip_cpp23_example"
```

On Ubuntu 24.04, install `g++-14` for its C++23 standard library and build the
example with `modules-linux-dev`; Clang 19 selects the installed libstdc++ 14.

On Windows, sanitizer builds stage the example's compiler runtime DLLs beside
the executable, allowing it to run outside the developer prompt.

On Linux, install CMake, Ninja, Qt 6 base development files, clang-format,
pkg-config and GLib/GIO development files (`libglib2.0-dev` on Ubuntu). The native
association adapter links `gio-unix-2.0`. On Windows, use a Qt 6 Widgets kit plus
CMake, Ninja, and a C++23 compiler. The shared Windows runner uses the
MSVC2022/Qt6.12.0 kit; see [runtime artifacts and CI scope](ci.md). Point
`CMAKE_PREFIX_PATH` to Qt when CMake cannot locate it.

## Local commands

Linux runner is the primary validation platform. For a focused correction,
configure, build the selected targets and run their CTest suites there first.
After Linux passes, check macOS locally and Windows; these secondary checks may
run in parallel. `verify.py` defaults to Linux; `--platform local` selects the
current machine explicitly.
`--platform all` checks Linux before Windows and stops if Linux fails.

```sh
python3 scripts/verify.py --list
python3 scripts/verify.py navigation
python3 scripts/verify.py archive-open
python3 scripts/verify.py options --plan
python3 scripts/verify.py --tests '^address_combo$'
python3 scripts/verify.py navigation --platform local
python3 scripts/verify.py navigation --platform windows
```

Profiles in `scripts/config/verify-profiles.json` define scenario scope and whether a
Windows desktop is needed. They are starting points: extend the selection for
other affected consumers. `--tests` overrides a profile's CTest regex. Targets
come from generated CMake/CTest registrations, including executable arguments
and fixture producers; their names are not guessed from suite names. An empty
enabled selection fails. Slow suites retain their explicit opt-in requirement.
`--plan` configures and prints the selection without building, testing or
preparing runtime. Verification uses the test-enabled `dev` preset. Use
`./scripts/build.sh release` for application-only builds; that preset disables tests
and is rejected by `verify.py` before configuration.

Keep command entrypoints in `scripts/`, shared implementation and build helpers
in `scripts/lib/`, and scenario configuration in `scripts/config/`.

`verify.py` prints its report path and keeps `verify-result.json` in the selected
profile's persistent log directory with the selection,
targets, result and time spent on runtime/configuration/selection/build/tests.
It uses `build.sh --configure-only`, then `build.sh --build-only <targets>` to
avoid configuring twice. Both modes retain the build wrapper's toolchain setup;
the latter requires an existing CMake cache. Plan output uses `verify-plan.json`
and preserves the last verification result. The build wrapper checks the
requested options/environment and cache signature; when unchanged, Ninja checks
CMake inputs/globs and regenerates only when needed, before test selection.
Changed wrapper/preset/options/environment or an externally edited CMake cache
triggers normal configuration. Incremental builds retain the same directory.

Runtime preparation uses `python3 scripts/prepare-runtime.py`: the official
builders run only when the pinned upstream revision/repository, builder code,
compiler/toolchain fingerprint or an artifact hash changes. A failed build does
not publish a cache hit. The builders also prepare the pinned reference client.
`verify.py` uses the existing runtime by default. Pass `--prepare-runtime`
to prepare or refresh it before verification. When the prepared
module exists, `build.sh` configures its exact path, replacing a stale cached
system module. Pinned source archives are included in the repository, so runtime
preparation requires neither an external upstream checkout nor source downloads.

Remote verification uses the same command in the persistent guest worktree
selected by the current Git branch; see [sync, locks and recovery](hyperv-runners.md#persistent-worktree).
There are no per-run source/build directories on the guests. Distinct run IDs
identify local logs and timing records only.

`SEVENZIP_COMPACT_TEST_BINARIES=ON` can reduce linked test debug information
when compiling with symbols enabled. The option is forwarded to runners.
Linux test executables omit linked DWARF debug sections; MSVC test
executables omit linker PDB/ILK files and remove unreferenced code, while retaining
distinct function addresses. Compiler debug information, Debug assertions and
runtime checks are retained, as are production application symbols. Ordinary
builds default to OFF. For debugger work, rebuild the affected test with
`SEVENZIP_COMPACT_TEST_BINARIES=OFF ./scripts/build.sh dev <target>` in the same
worktree/toolchain. The option does not delete artifacts from older builds.

Runtime packages use Release builds with testing disabled; local checks remain
unchanged. GitHub Actions builds release packages; see [CI scope](ci.md).
For a local application with its runtime dependencies, run `./scripts/build-app.sh`.
With the optional vcpkg checkout installed, `./scripts/build-app.sh vcpkg-release`
builds and installs the manifest-based application instead.
It prints the installation directory in the branch's artifacts. The local runtime receives the same size optimizations as
the archive payload, including native-architecture stripping and verified ad-hoc
signing on macOS. Its size also depends on the selected Qt kit and its dependencies.
The installation replaces the previous runtime only after preparation succeeds,
so dependencies from an older Qt kit do not remain in the installation.
To package an existing Release build, run `python3 scripts/package.py
--qt-source <matching-qtbase.tar.xz> --qt-sha256 <checksum-file>`; add
`--icu-license <license-file>` if the deployed Qt kit uses ICU. The script uses
a fresh CMake installation and writes only runtime archives to the branch’s package directory.
Source inputs stay private. macOS packages have verified ad-hoc signatures.
The install includes the project's MIT notice under `Notices/sevenzip-desktop`
in the macOS bundle or `share/licenses/sevenzip-desktop/sevenzip-desktop`
on Linux/Windows; engine and dependency notices remain separate.
Windows additionally requires `--vc-runtime <Microsoft.VC143.CRT-directory>`
and `--vc-notices <Visual-Studio-Redist.txt>`; only imported CRT DLLs are copied.
See [CI and release helper](ci.md) for exact artifact scope and release commands.

```sh
sh .agents/skills/sz-setup/scripts/setup.sh
./scripts/doctor.sh
./scripts/build.sh dev <targets>
./scripts/ctest.sh --preset dev -R '<test-name-regex>' --output-on-failure
```

Without target arguments, `build.sh dev` builds the application and its required
dependencies, not every test executable. Use `verify.py` for a focused scenario
or pass explicit targets. `build.sh dev all` is an explicit all-target build.

`./scripts/build.sh dev sevenzip_window_tests` builds the application and its
window test consumers without running them. Select runtime checks separately
with `verify.py` or `ctest.sh`; long suites remain opt-in.

Focused checks are the default for agent work. Select targets and suites using
[testing](testing.md); use `./scripts/check.sh` only when the full-gate criteria
in [AGENTS.md](../AGENTS.md#validation-and-ui-reference) apply. Before each batch,
follow [artifact retention](testing.md#test-artifact-retention). The wrappers do
not automatically prune old runs; the agent performs the scoped cleanup.

`check.sh` checks formatting, builds with the development CMake preset, and runs CTest with long suites excluded. Both `check.sh` and `ctest.sh` run up to four tests in parallel by default. CTest resource locks serialize tests that write to the shared UI report or screenshot directories. Set `SEVENZIP_CTEST_JOBS=1` for a sequential run or another positive number to change concurrency; `CTEST_PARALLEL_LEVEL` and an explicit `--parallel` argument to `ctest.sh` also take precedence. CTest reports failed suites through `--output-on-failure` and continues running the remaining suites. For a release build, run `./scripts/build.sh release`. The Release preset disables testing; run tests with the `dev` preset.

Long suites are excluded by default, including focused `-R` selections;
see [opt-in commands](#long-suites). Agents follow the explicit-request rule
in [AGENTS.md](../AGENTS.md#validation-and-ui-reference).
An empty executed selection fails, including a regex matching only excluded
slow suites. `--show-only` still allows discovery without running tests.

Qt Test suites run with `-silent`, so a failed suite reports its failed cases without listing the cases that passed. `ctest.sh` filters CTest's `Start` and successful `Passed` lines as they arrive; failed suite status, diagnostics, and the final summary remain visible. Each retained line is flushed immediately, and a failed CTest still returns a nonzero exit status.

`check.sh` exits before any work when `CODEX_SANDBOX` identifies an active sandbox. The complete suite needs an unsandboxed run: DBus socket creation and native process-priority tests fail with `Operation not permitted` in the Codex sandbox. Focused tests can still be run there when their required system calls are permitted.

`build.sh` uses CMake `--log-level=WARNING`, discards configuration stdout, and builds with Ninja `--quiet`: successful configuration and build progress lines are hidden, while warnings and errors remain visible as they occur. `ctest.sh` streams CTest output directly, including failure details when `--output-on-failure` is passed. When `CODEX_SESSION_ID` is set, only the formatting phase of `check.sh` buffers output; on failure it shows the last 30 log lines and the path to the full temporary log. Set `SEVENZIP_SCRIPT_OUTPUT=verbose` to see that formatting output as it runs. `ctest.sh` forwards arguments to CTest after setting the default parallel level and combining caller label exclusions with the default slow-suite exclusion; agents use it for both focused and default gate runs.

The buffered log belongs to the shell that called `sevenzip_setup_output`.
Background progress notifiers cannot finalize or remove that parent's log,
including when a fast command cancels a notifier before its traps are installed.
`agent_output` covers fast-command termination, delayed progress and failure-log
retention across the supported Bash versions.

If `ccache` is installed, `build.sh` automatically uses it for C, C++ and Objective-C++ compilation, including builds started by `check.sh`. Install it with `brew install ccache` on macOS or your package manager on Linux. It speeds up recompilation after a clean build or branch switch; Ninja already skips unchanged files, and the full CTest suite still runs on every `check.sh` invocation.
On macOS the script defaults `CCACHE_DIR` to `/private/tmp/sevenzip-desktop-ccache`, which is writable in the local sandbox and reuses the existing cache. Set `CCACHE_DIR` before running the script to use another directory.
Windows ccache uses CMake 3.25+ with embedded MSVC `/Z7` debug information;
the project runners provide that toolchain. `verify-result.json` records actual
compiler launchers and ccache hit/miss deltas without resetting shared statistics.

Validation reuses the existing incremental build and compiler cache. Full/release
checks do not imply a cold build; clearing or bypassing the cache requires an
explicit user request under [the validation rules](../AGENTS.md#validation-scope).

Build specific targets with `./scripts/build.sh dev <target> [target ...]` and run
focused cases with `./scripts/ctest.sh --preset dev -R '<test-name-regex>' --output-on-failure`.
The build script configures the preset and applies platform toolchain workarounds;
without targets it builds everything. Requirements for focused/full validation,
repeat runs and docs-only changes are in [AGENTS.md](../AGENTS.md#validation-and-ui-reference).

Use `scripts/build-app.sh release` to prepare a native application installation;
it prints the installation directory. Build outputs are ignored. The development preset
exports `compile_commands.json` in its selected build tree for editor navigation.

Some Apple Command Line Tools installations contain C++ standard headers inside the macOS SDK but omit them from Clang's default include path. `scripts/build.sh` detects this and sets `CPLUS_INCLUDE_PATH` only for that build process; it does not alter system files.

## Long suites

The current labels are in [SlowTests.cmake](../cmake/SlowTests.cmake).
Long matrices and high-memory boundary checks are excluded by default.
Agents run these only on an explicit request for long tests or the exhaustive
suite, following [AGENTS.md](../AGENTS.md#validation-and-ui-reference).

The full `panel_creation_path`, `interarchive_recovery` and `copy_archive_refresh`
matrices are opt-in. The default
`interarchive_recovery_native` subset covers native reopen failure, cancellation
and failed restore. Acceptance of that subset does not accept the full matrix.

Prior timing observations and cap rationale are retained in
[the maintenance archive](archive/agent-maintenance-2026-10-25.md#прежние-основания-long-suite-лимитов).
Current timeouts belong to CMake; old results and exclusions are not acceptance
of the current matrices.

```sh
# Only long suites:
SEVENZIP_INCLUDE_SLOW_TESTS=1 ./scripts/ctest.sh --preset dev -L slow --output-on-failure
# One explicitly requested long suite (build its targets first):
SEVENZIP_INCLUDE_SLOW_TESTS=1 ./scripts/ctest.sh --preset dev -R '^native_rename_2$' --output-on-failure
# Exhaustive local gate:
SEVENZIP_INCLUDE_SLOW_TESTS=1 ./scripts/check.sh
# Exhaustive gate on both existing runners:
SEVENZIP_INCLUDE_SLOW_TESTS=1 python3 scripts/hyperv-runner.py check all --interactive --elevated
```

Remote focused checks must build their targets after source synchronization in
the persistent worktree; use `verify.py` or follow [Hyper-V runners](hyperv-runners.md).
A default gate does not
establish acceptance of excluded slow suites.

## Automation

Remote checks use the hosts and toolchains in
[local runner configuration](runner-configuration.md). These optional commands
require authenticated SSH access; contributors can build and test locally without them:

```sh
python3 scripts/hyperv-runner.py probe linux
python3 scripts/hyperv-runner.py probe windows
python3 scripts/hyperv-runner.py check linux
python3 scripts/hyperv-runner.py check windows
```

For a separate Release build, use
`python3 scripts/hyperv-runner.py run all -- scripts/build.sh release`.
Run tests with the `dev` preset; Release disables test targets.
The helper uses either the configured SSH connection or an optional
readiness command for VM startup and address discovery. Each guest reuses its
configured Sevenzip worktree for the current Git branch. See [Hyper-V runners](hyperv-runners.md)
for toolchains, pinned engine caching, source sync scope and validation results.
This is project integration glue; upstream 7zFM has no runner implementation.

The old `scripts/runner.py` wrapper is retained for explicitly selected local
QEMU/UTM workflows supported by the global skill. Historical ARM64 VM checks
do not validate the current Hyper-V guests.

Local task memory is initialized by the setup script in `.codex/memory/` and
`.codex/notes/`. Repeated setup preserves the populated index, notes, and
attachments; it refreshes documentation symlinks and task templates. See
[the memory protocol](agent-memory.md) for reading, updating, and transferring
memory between checkouts.

There is no project pre-commit hook. Run `scripts/check.sh` manually according to `AGENTS.md`; setup does not install hooks or modify Git configuration. Local tests run on the current machine; separate platform checks are needed to establish cross-platform results.

## Upstream checkout

An optional reference checkout can point to `https://github.com/ip7z/7zip.git`.
Use one for original-source comparison and upstream update review:

```sh
git clone https://github.com/ip7z/7zip.git ../7zip-upstream
```

Keep upstream updates separate from UI commits. Record the upstream version or commit in a release note when packaging.
Use [the pinned baseline and update review procedure](upstream-update.md) to
verify the exact borrowed files and compare old/new source before transferring
changes into adapted Qt modules. The original sibling checkout stays read-only.
