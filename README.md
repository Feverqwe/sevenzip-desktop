# Sevenzip

An independent file and archive manager for Windows, macOS and Linux, based on
7-Zip. The interface uses Qt 6 Widgets, and archive operations use the 7-Zip
engine with project adaptations. Sevenzip has its own features and architecture;
it is not an official 7-Zip release.

This is an AI-driven project: AI coding agents play a central role in development,
guided by human direction, review and testing.

[Download](https://github.com/Feverqwe/7zip-qt-port/releases) ·
[Report a bug](https://github.com/Feverqwe/7zip-qt-port/issues) ·
[Contributing](CONTRIBUTING.md)

## Installation

Release archives are built for Windows x64, Linux x64 and macOS arm64.
Extract the entire archive, keeping its directories and libraries together.
On macOS, open `Sevenzip.app`; on Windows, run `bin/7zFM.exe`; on Linux,
run `bin/7zFM`. macOS 14.4 or later is required.

Packages do not have Windows Authenticode signatures or Apple Developer ID
signing and notarization. macOS packages use an ad-hoc signature. Checksums
are published alongside the archives in `SHA256SUMS.txt`.

## Project status

The application supports dual panels, filesystem and archive navigation, nested
archives, history, favorites, list views, sorting and selection. Features include
file copy, move, create, rename and delete operations, split/combine, properties,
external viewers, editors and diff tools, checksums, benchmarks and settings.

File → Open device adds experimental read-only browsing and copying from external
devices with supported filesystems without a filesystem driver or full disk image.
See [direct device access](docs/direct-device-reading.md) for usage and limits.

Read-only [F2FS support](docs/f2fs-reading.md) adds browsing and copying from
compatible Android and flash-drive images.

Experimental XFS reading provides access to server and NAS filesystem images;
see [support and limits](docs/upstream-port-audit/xfs.md).

Btrfs volumes and disk images can be browsed and extracted without changing the
source. See [Btrfs support and limitations](docs/upstream-port-audit/btrfs-linux.md#supported-reading-and-limitations).

Archive creation and updates support compression settings, encryption, volumes
and self-extracting archives. Available formats and operations depend on the
selected engine. Extraction supports password and overwrite prompts; an error
or cancellation may leave files that have already been extracted. Updating a
nested archive requires write support throughout its containing archive chain.
Platform metadata, links, alternate data streams and file associations have
additional limitations.

The project is in an early release stage. See the
[acceptance scope](docs/ux-port-audit.md) for validation boundaries and the
[7zFM comparison](docs/7zfm-parity.md) for supported workflows and limitations.

## Building from source

Clone the project:

```sh
git clone https://github.com/Feverqwe/7zip-qt-port.git
cd 7zip-qt-port
```

You need C and C++23 compilers, CMake 3.28+, Ninja, Qt 6 Widgets and `clang-format`.
The 7-Zip source dependencies are included in the repository. macOS builds use the official Qt SDK and target macOS
14.4 or later. Follow the [dependency setup](docs/development.md#dependencies)
for your platform.

From the repository root:

```sh
./scripts/doctor.sh
python3 scripts/prepare-runtime.py
./scripts/check.sh
```

`check.sh` checks C++ formatting, builds the application and runs the default
test suite. On macOS, locate the development build and launch it from a terminal:

```sh
artifact_root="$(python3 scripts/lib/build_paths.py path --root "$PWD" --category artifacts)"
"$artifact_root/dev/Sevenzip.app/Contents/MacOS/Sevenzip" /path/to/archive.7z
```

To build a Release package without test executables:

```sh
./scripts/build-app.sh
```

The script prints the installation directory, including `Sevenzip.app` on macOS.
Use the [workspace resolver](docs/development.md#local-commands) to locate
build outputs for the selected branch and platform.

The application uses the prepared native codec runtime and its own `7zG` process.
The official `7z` client is included as a separate command-line tool and is also
used for reference tests. The File Manager and GUI operations do not invoke it.
See the [development guide](docs/development.md) for platform dependencies,
codec and self-extracting runtime preparation, engine selection and focused
build commands. Local builds and tests do not require access to the maintainer's
infrastructure.

## Documentation

The [documentation map](docs/README.md) links development instructions, source
provenance, architecture, UI specifications and testing. Repository conventions
are in [AGENTS.md](AGENTS.md), with task-specific workflows in `.agents/skills/`.

Local agent memory is optional. Run
`sh .agents/skills/sz-setup/scripts/setup.sh` to initialize the Git-ignored
`.codex/` directory with documentation links. Running it again preserves existing
notes. See the [memory protocol](docs/agent-memory.md) for reading and handoffs.

## Licenses

This repository contains code under several licenses. [MIT](LICENSE) applies
to original project code unless another license is specified. Ported 7-Zip code
retains its own licenses; the exFAT module and adapted development tools use
GPL-2.0-or-later. Qt and other dependencies retain their own terms.
See [third-party notices](THIRD_PARTY_NOTICES.md).
