# Local runtime artifacts and CI scope

Licensing of borrowed tooling and runtime components is summarized in
[third-party notices](../THIRD_PARTY_NOTICES.md). The root MIT license does not
replace the GPL license of borrowed tooling.

The [release workflow](../.github/workflows/release.yml)
builds Windows x64, Linux x64 and macOS arm64 on `v*` tags. The tag must match
the CMake application version. After all builds succeed, it publishes runtime
archives and `SHA256SUMS.txt` in a GitHub Release with generated release notes.
Manual dispatch builds Actions artifacts without publishing a release.
Local validation uses [focused checks and a full gate when needed](development.md#local-commands),
and Windows/Linux acceptance uses [Hyper-V runners](hyperv-runners.md).
Release runtime packaging uses `BUILD_TESTING=OFF` and the matching Qt kit;
tests and native acceptance are separate checks.
Local macOS builds default to the official shared Qt 6.12.0 kit and target
macOS 14.4+. This avoids the external ICU/GLib dependency chain of Homebrew Qt;
see [SDK setup](development.md#dependencies).

Runtime/SFX builders compile private source trees expanded from the local
7-Zip archive and adaptations recorded in [the baseline](upstream-baseline.json).
Release CI uses those same repository-owned inputs without fetching 7-Zip.
Source ownership and generated build views are described in
[architecture](architecture.md#module-directories-and-source-ownership).
`scripts/install-qt-windows.py` adapts aqtinstall to the selected Qt kit's
compiler-specific repository layout. Pins from the removed workflow are
[historical evidence](archive/qt-port/workflow-evidence.md).

`scripts/package.py` archives a fresh CMake installation, never the checkout or
build tree. Artifacts contain the application, retained entry aliases, official
engine/module/SFX, Qt runtime dependencies and notices. Headers, static/import
libraries, debug symbols, test plugins, source archives and build metadata are
excluded. macOS contains a single `.app` in ZIP; Linux has a runtime directory
in tar.gz; Windows has a runtime directory in ZIP. Only ICO/SVG image plugins
are retained for embedded resources/theme icons. macOS ZIPs omit resource-fork
metadata alongside the application. Qt deploy tools resolve the
linked libraries and plugins. System libraries remain platform dependencies.
Manifest builds also include the installed vcpkg dependencies' copyright texts,
covering static libraries whose notices cannot be found by scanning runtime files.
Windows packages copy the imported VC CRT DLL dependency closure from the
installed redistributable, with its notices, instead of shipping a CRT installer.
The packaging command prints the resulting archives' paths in the project workspace.
The packaging script itself does not upload artifacts or publish a release.

Matching Qt source archives are checksum-verified private inputs for extracting
license texts only; they are never copied into runtime artifacts. The ICU notice
is included when ICU is deployed. macOS binaries are thinned to the native
architecture and stripped, then signed inside out using an ad-hoc signature, including the bundled 7-Zip
engine/module/SFX. SFX checksum metadata is refreshed after these transformations.
Frameworks and the app are signed last; `codesign --verify --deep --strict` must
pass before ZIP creation. Local `build-app.sh` installations use the same runtime
compaction and signing. Developer ID,
notarization and Windows Authenticode are not configured.

After `./scripts/build-app.sh release`, use
`python3 scripts/check-installed-runtime.py --install` with the printed installation
directory to check startup and native archive operations without SDK search paths
in the environment. The check uses disposable Unicode/space-containing files
and private settings, and verifies extracted bytes, a deployed external codec
and unchanged inputs. Linux requires a desktop session or system `xvfb-run`;
Windows needs an interactive runner session.

`./scripts/release.sh patch` (also `minor`, `major` or an explicit larger version)
requires a clean tree, changes the CMake version, commits it, creates an annotated
`vX.Y.Z` tag, and atomically pushes branch and tag to `origin`.
`--remote` selects another remote; `--dry-run` checks without changing
files, commits or tags. `--check-tag vX.Y.Z` checks the version locally.
Running this helper publishes Git refs; importing it does not run a release.

The workflow builds the pinned official engine and SFX before packaging, using
the source repository and revision from the baseline. Release CI builds and
deploys runtime payloads; it does not run CTest or native acceptance scenarios.
Actions artifacts are retained for seven days, including successful platforms
when another build fails. Only the publish job receives `contents: write`.
Native, rendered and clean-environment package acceptance remain separate
from hosted release builds and local compilation/relocation checks.
