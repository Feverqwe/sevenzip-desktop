#!/usr/bin/env python3
"""Verify source baselines and prepare read-only upstream update reviews."""

import argparse
import difflib
import hashlib
import json
import os
import pathlib
import posixpath
import re
import subprocess
import sys
import tarfile
import tempfile



ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/upstream-baseline.json"
sys.path.insert(0, str(ROOT))
from tools.dependencies.source_archives import SourceArchives


def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True)
    if result.returncode:
        raise ValueError(result.stderr.decode("utf-8", "replace").strip())
    return result.stdout


def source_path(path, data=None):
    if data and data.get("schema") == 2:
        try:
            return data["origins"][path]
        except KeyError as error:
            raise ValueError("Source owner has no recorded origin: " + path) from error
    if path == "third_party/7zip/License.txt":
        return "DOC/License.txt"
    if path.startswith("third_party/7zip/"):
        return path[len("third_party/7zip/") :]
    if path.startswith("src/gpl/CPP/") or path.startswith("src/gpl/C/"):
        return path[len("src/gpl/") :]
    if path.startswith("src/CPP/") or path.startswith("src/C/"):
        return path[len("src/") :]
    raise ValueError("Unexpected borrowed path: " + path)


def load_manifest(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") not in (1, 2) or not re.fullmatch(r"[0-9a-f]{40}", data.get("revision", "")):
        raise ValueError("Manifest requires a supported schema and a full commit hash")
    if data.get("repository") != "https://github.com/ip7z/7zip.git":
        raise ValueError("Unexpected upstream repository")
    groups = [data.get(name) for name in ("exact", "adapted", "local")]
    if any(not isinstance(group, list) or group != sorted(set(group)) for group in groups):
        raise ValueError("Manifest path groups must be sorted, unique lists")
    paths = [path for group in groups for path in group]
    if len(paths) != len(set(paths)):
        raise ValueError("Borrowed paths have multiple owners")
    if data["schema"] == 2:
        borrowed = set(data["exact"] + data["adapted"])
        if set(data.get("origins", {})) != borrowed:
            raise ValueError("Every borrowed owner requires one explicit origin")
        packed = data.get("original_archive")
        if packed:
            local = pathlib.PurePosixPath(packed.get("local", ""))
            if (local.is_absolute() or ".." in local.parts or
                    local.parts[:2] != ("third_party", "7zip") or
                    not re.fullmatch(r"[0-9a-f]{64}", packed.get("sha256", ""))):
                raise ValueError("Invalid original source archive")
        for origin in data["origins"].values():
            relative = pathlib.PurePosixPath(origin)
            if relative.is_absolute() or ".." in relative.parts or not relative.parts:
                raise ValueError("Unsafe original inventory path")
        for origin, original in data.get("originals", {}).items():
            relative = pathlib.PurePosixPath(origin)
            local = pathlib.PurePosixPath(original.get("local", "third_party/7zip"))
            if (relative.is_absolute() or ".." in relative.parts or not relative.parts or
                    local.is_absolute() or ".." in local.parts or
                    local.parts[:2] != ("third_party", "7zip") or
                    not re.fullmatch(r"[0-9a-f]{64}", original.get("sha256", ""))):
                raise ValueError("Invalid repository-owned original source")
        sources = [source_path(path, data) for path in data["exact"] + data["adapted"]]
    else:
        sources = [source_path(path) for path in paths]
    if len(sources) != len(set(sources)):
        raise ValueError("Upstream module has multiple authoritative implementations")
    overrides = data.get("overrides", {})
    if not isinstance(overrides, dict) or set(overrides) - set(paths) or any(
        not re.fullmatch(r"[0-9a-f]{40}", value) for value in overrides.values()
    ):
        raise ValueError("Revision overrides require recorded paths and full hashes")
    hashes = data.get("original_sha256", {})
    if not isinstance(hashes, dict) or set(hashes) - set(data["exact"] + data["adapted"]) or any(
        not re.fullmatch(r"[0-9a-f]{64}", value) for value in hashes.values()
    ):
        raise ValueError("Original SHA256 values require recorded upstream source owners")
    for owner, extraction in data.get("extracted", {}).items():
        if (owner not in data["exact"] + data["adapted"] or owner not in hashes or
                not extraction.get("symbols") or not extraction.get("local") or
                set(extraction["local"]) - set(data["local"] + data["adapted"])):
            raise ValueError("Extracted source requires its original baseline and local owners")
    return data


def tracked_paths(data):
    roots = data.get("inventory_roots", ["third_party/7zip/", "src/CPP/", "src/C/", "src/gpl/"])
    snapshot = ROOT / ".runner-source.json"
    if os.environ.get("SEVENZIP_RUN_TOKEN") and snapshot.is_file():
        paths = [path for path in json.loads(snapshot.read_text())
                 if any(path.startswith(root.rstrip("/") + "/") for root in roots)]
    else:
        paths = git(ROOT, "ls-files", "--cached", "--others", "--exclude-standard",
                    *roots).decode().splitlines()
    return {path for path in paths
            if (data.get("schema") == 2 or path != "third_party/7zip/README.md")
            and (ROOT / path).is_file()}


def check_inventory(data):
    recorded = set(data["adapted"] + data["local"])
    if data.get("original_archive"):
        recorded.add(data["original_archive"]["local"])
        recorded.update(data.get("notices", {}))
    else:
        recorded.update(data["exact"])
        recorded.update(original["local"] for original in data.get("originals", {}).values())
    roots = data.get("inventory_roots", [])
    for manifest in data.get("related_manifests", []):
        related = pathlib.PurePosixPath(manifest)
        if (related.is_absolute() or ".." in related.parts or
                related.parts[0] not in ("docs", "third_party")):
            raise ValueError("Invalid related source baseline")
        reference = json.loads((ROOT / related).read_text())
        recorded.update(path for path in reference.get("owners", {}).values()
                        if any(path.startswith(root.rstrip("/") + "/") for root in roots))
    current = tracked_paths(data)
    missing = current - recorded
    stale = recorded - current
    if missing or stale:
        raise ValueError(f"Inventory mismatch: unrecorded={sorted(missing)}, stale={sorted(stale)}")


def runtime_header_dependencies(snapshot, members, overlays, extra_c=(), format_sources=()):
    """Supplement upstream rules for adapted headers and module-version consumers."""
    # Native NMake rules pass all prerequisites to the compiler. Added header
    # dependencies must not become translation units; retain source batching.
    native_rules = snapshot / "CPP/Build.mak"
    if native_rules.is_file():
        original = native_rules.read_bytes()
        adapted = original.replace(b"$**", b"$(SEVENZIP_RUNTIME_SOURCES)")
        # Keep the standard on C++ recipes only: upstream shares CFLAGS with C.
        adapted = re.sub(
            rb'^(COMPL(?:_O1|_O2|_PCH|B|B_O2)?\s*=\s*\$\(CC\)) ',
            rb'\1 $(SEVENZIP_RUNTIME_CXXFLAGS) ', adapted, flags=re.MULTILINE)
        if adapted != original:
            native_rules.write_bytes(adapted)
    sources = {name for name in members if pathlib.PurePosixPath(name).suffix in
               (".c", ".cpp", ".h", ".hpp", ".inl")}
    headers = {name for name in overlays if pathlib.PurePosixPath(name).suffix in
               (".h", ".hpp", ".inl")}
    # GetModuleProp is the native ABI/version boundary, including when a
    # retained runtime snapshot moves between original upstream baselines.
    # Upstream's DllExports2 object rule does not list this header.
    headers.update({"C/7zVersion.h"} & sources)
    reverse = {}
    pattern = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.MULTILINE)
    for name in sources:
        for include in pattern.findall((snapshot / name).read_text(errors="replace")):
            local = posixpath.normpath(posixpath.join(posixpath.dirname(name), include))
            # Relative includes have one owner. For include-directory searches,
            # retain all possible source-tree matches rather than miss a consumer.
            matches = [local] if local in sources else [path for path in sources
                                                        if path == include or path.endswith("/" + include)]
            for dependency in matches:
                reverse.setdefault(dependency, set()).add(name)
    dependencies = {}
    for header in headers:
        pending = [header]
        seen = set()
        while pending:
            current = pending.pop()
            if current in seen:
                continue
            seen.add(current)
            if pathlib.PurePosixPath(current).suffix in (".c", ".cpp"):
                dependencies.setdefault(pathlib.PurePosixPath(current).stem, set()).add(header)
            pending.extend(reverse.get(current, ()))
    for directory in ("CPP/7zip/UI/Console", "CPP/7zip/Bundles/Format7zF",
                      "CPP/7zip/Bundles/SFXCon", "CPP/7zip/Bundles/SFXWin"):
        for windows, original, generated in ((False, "makefile.gcc", "sevenzip-runtime.gcc.mak"),
                                              (True, "makefile", "sevenzip-runtime.mak")):
            bundle = snapshot / directory
            if not (bundle / original).is_file():
                continue
            bundle_sources = sorted(set(extra_c) | (set(format_sources)
                if directory == "CPP/7zip/Bundles/Format7zF" else set()))
            extra_objects = ["$(O)\\reference_" + re.sub(r"[^a-zA-Z0-9_]", "_", str(pathlib.PurePosixPath(name).with_suffix(''))) + ".obj"
                             for name in bundle_sources] if windows else []
            # Nmake expands the upstream link command while reading its include;
            # supply additional objects through Build.mak's supported LIBS input.
            lines = (["LIBS = $(LIBS) " + " ".join(extra_objects)] if extra_objects else [])
            qemu_module = (directory == "CPP/7zip/Bundles/Format7zF" and
                           "dependencies/qemu.cppm" in overlays)
            if qemu_module:
                if windows:
                    lines += ['!IF EXIST("../../../../dependencies/native-qemu.mak")',
                              '!include "../../../../dependencies/native-qemu.mak"',
                              '!IFDEF SEVENZIP_QEMU_ENABLED',
                              'LIBS = $(LIBS) $(SEVENZIP_QEMU_LIBRARY)', '!ENDIF', '!ENDIF']
                else:
                    lines.append('-include ../../../../dependencies/native-qemu.gcc.mak')
            if windows:
                lines += ["SEVENZIP_RUNTIME_SOURCES = $(filteri %.c %.cpp %.asm %.s %.rc,$**)",
                          "SEVENZIP_RUNTIME_CXXFLAGS = /std:c++23preview /Zc:__cplusplus /permissive-",
                          "CFLAGS = $(CFLAGS) -I../../../.."]
            lines.append(f'!include "{original}"' if windows else f'include {original}')
            lines.append("# Toolchain: " + os.environ.get("SEVENZIP_RUNTIME_TOOLCHAIN", "direct"))
            if windows:
                lines.append("$(O)\\DllSecur.obj: ../../../../C/DllSecur.c sevenzip-runtime.mak")
                if os.environ.get("SEVENZIP_RUNTIME_SANITIZERS") == "1":
                    lines.append('\t$(CC) $(CFLAGS_O2) -DSEVENZIP_RUNTIME_SANITIZERS -Fo"$(O)\\DllSecur.obj" "../../../../C/DllSecur.c"')
            if not windows:
                lines.append("CXX_STD_FLAGS = -std=c++23 -D_GLIBCXX_ASSERTIONS=1 "
                             "-D_LIBCPP_ENABLE_HARDENING=1 "
                             "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE")
                lines.append("LOCAL_FLAGS += -I../../../..")
                if os.environ.get("SEVENZIP_RUNTIME_SANITIZERS") == "1":
                    # An unsanitized console can load instrumented format
                    # plugins only if the sanitizer runtime starts with it.
                    lines.append("MY_LIBS += -fsanitize=address,undefined")
                lines.append("$(PROGPATH): sevenzip-runtime.gcc.mak")
                if sys.platform == "darwin":
                    lines += ["LOCAL_FLAGS += -mmacosx-version-min=14.4",
                              "MY_LIBS += -mmacosx-version-min=14.4"]
            if qemu_module:
                if windows:
                    lines += ['!IFDEF SEVENZIP_QEMU_ENABLED',
                              '$(O)\\QcowHandler.obj: ../../Archive/QcowHandler.cpp '
                              '$(SEVENZIP_QEMU_BMI) $(SEVENZIP_QEMU_LIBRARY) sevenzip-runtime.mak',
                              '\t$(SEVENZIP_QEMU_PYTHON) $(SEVENZIP_QEMU_CACHE) $(CC) '
                              '$(SEVENZIP_RUNTIME_CXXFLAGS) $(CFLAGS_O1) '
                              '-DSEVENZIP_USE_QEMU_MODULE @../../../../dependencies/qemu.modmap '
                              '"../../Archive/QcowHandler.cpp"', '!ENDIF']
                else:
                    lines += ['ifdef SEVENZIP_QEMU_ENABLED',
                              'MY_LIBS += $(SEVENZIP_QEMU_LIBRARY)',
                              '$(O)/QcowHandler.o: CXXFLAGS += $(SEVENZIP_QEMU_FLAGS)',
                              '$(O)/QcowHandler.o: $(SEVENZIP_QEMU_BMI) $(SEVENZIP_QEMU_LIBRARY)',
                              'endif']
            for stem, needed in sorted(dependencies.items()):
                relative = sorted(posixpath.relpath(name, directory) for name in needed)
                target = "$(O)\\" + stem + ".obj" if windows else "$(O)/" + stem + ".o"
                lines.append(target + ": " + " ".join('"' + name + '"' if windows else name
                                                       for name in relative))
            # Preserve each dependency's source language, upstream object outputs
            # and compiler-cache launchers in both native runtimes.
            extra_objects = []
            for name in bundle_sources:
                stem = "reference_" + re.sub(r"[^a-zA-Z0-9_]", "_", str(pathlib.PurePosixPath(name).with_suffix('')))
                target = "$(O)\\" + stem + ".obj" if windows else "$(O)/" + stem + ".o"
                source = posixpath.relpath(name, directory)
                needed = sorted(posixpath.relpath(header, directory)
                                for header in dependencies.get(pathlib.PurePosixPath(name).stem, set()))
                lines.append(target + ": " + " ".join([source, *needed, generated]))
                cpp = name.endswith(".cpp")
                # Keep unchanged third-party C at CMake/MSVC's normal warning
                # level rather than the 7-Zip-specific /Wall pragma contract.
                flags = '$(CFLAGS_O2:-Wall=-W3) -DZSTD_DISABLE_ASM' if name.startswith("zstd/") else '$(CFLAGS_O2)'
                if name.startswith("zlib/"):
                    flags = '$(subst -Gr,-Gd,$(CFLAGS_O2:-Wall=-W3)) -DZ_PREFIX'
                xz_flags = ''
                if name.startswith("xz/"):
                    includes = ("xz/src/liblzma/api", "xz/src/common", "xz/src/liblzma/common")
                    xz_flags = ' '.join('-I' + posixpath.relpath(path, directory) for path in includes)
                    xz_flags += ' -DLZMA_API_STATIC -DHAVE_STDBOOL_H -DHAVE__BOOL -DHAVE_STDINT_H -DHAVE_INTTYPES_H'
                    if sys.byteorder == 'big':
                        xz_flags += ' -DWORDS_BIGENDIAN'
                    flags = '$(subst -Gr,-Gd,$(CFLAGS_O2:-Wall=-W3)) -std:c11 ' + xz_flags
                lines.append(('\t$(CC) ' + flags + ' ' + ('$(SEVENZIP_RUNTIME_CXXFLAGS) -TP' if cpp else '-TC') + ' -Fo"' + target + '" "' + source + '"')
                             if windows else ('\t$(CXX) $(CXXFLAGS) ' if cpp else
                                              '\t$(CC) $(CFLAGS) ' + ('-DZSTD_DISABLE_ASM ' if name.startswith("zstd/") else
                                                                      '-DZ_PREFIX ' if name.startswith("zlib/") else '')) +
                             source)
                if xz_flags and not windows:
                    lines[-1] = '\t$(CC) $(CFLAGS) -std=c11 ' + xz_flags + ' ' + source
                extra_objects.append(target)
            if extra_objects:
                if not windows:
                    lines.append("OBJS += " + " ".join(extra_objects))
                lines.append("$(PROGPATH): " + " ".join(extra_objects))
                if not windows:
                    lines.append("$(PROGPATH_STATIC): " + " ".join(extra_objects))
            lines.append("$(OBJS): " + generated)
            content = "\n".join(lines) + "\n"
            output = bundle / generated
            if output.is_symlink():
                raise ValueError("Refusing linked runtime dependency makefile")
            if not output.exists() or output.read_text() != content:
                output.write_text(content)


def snapshot_path(snapshot, name):
    relative = pathlib.PurePosixPath(name)
    if (relative.is_absolute() or ".." in relative.parts or not relative.parts or
            str(relative) != name or "\\" in name or ":" in name):
        raise ValueError("Unsafe runtime source entry: " + name)
    destination = snapshot / relative
    if any(path.is_symlink() for path in [destination, *destination.parents]
           if path == snapshot or snapshot in path.parents):
        raise ValueError("Refusing linked runtime source: " + name)
    return destination


def runtime_content(data, owner, destination, content):
    """Rebase project includes into the native build's generated source view."""
    if data.get("schema") != 2 or not owner:
        return content
    destinations = {**data.get("origins", {}), **data.get("runtime_paths", {})}
    def include(match):
        name = match[2]
        target = destinations.get("src/" + name) or destinations.get("third_party/" + name)
        if target is None:
            for candidate in (name, "CPP/" + name):
                if candidate in data.get("originals", {}):
                    target = candidate
                    break
        if target is None:
            return match[0]
        relative = posixpath.relpath(target, posixpath.dirname(destination))
        return match[1] + relative + match[3]
    text = content.decode("utf-8")
    return re.sub(r'(^\s*#\s*include\s+")([^"\n]+)(")', include,
                  text, flags=re.MULTILINE).encode("utf-8")


def prepare_header_snapshot(data, snapshot):
    """Expose foreign include contracts without constraining project directories."""
    snapshot.mkdir(parents=True, exist_ok=True)
    owner = snapshot / ".sevenzip-header-source.json"
    if snapshot.is_symlink() or (any(snapshot.iterdir()) and not owner.is_file()):
        raise ValueError("Refusing an unowned header compatibility directory")
    previous = json.loads(owner.read_text()) if owner.exists() else {"files": []}
    headers = {origin: path for path, origin in data.get("origins", {}).items()
               if path in data["adapted"] and path.endswith((".h", ".hpp", ".inl"))}
    headers.update(data.get("header_aliases", {}))
    for name, path in headers.items():
        source = ROOT / path
        if source.is_symlink() or not source.is_file():
            raise ValueError("Header compatibility requires a local source owner: " + path)
        target = snapshot_path(snapshot, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not path.startswith("src/"):
            raise ValueError("Header compatibility requires a project module owner")
        # Foreign code keeps its include spelling; the compiler still reads
        # the single live module header and tracks its ordinary dependencies.
        content = ('#include "' + path[len("src/"):] + '"\n').encode("utf-8")
        if not target.exists() or target.read_bytes() != content:
            target.write_bytes(content)
    for name in set(previous["files"]) - set(headers):
        snapshot_path(snapshot, name).unlink(missing_ok=True)
    owner.write_text(json.dumps({"files": sorted(headers)}) + "\n")


def original_sources(data, upstream=None):
    """Read repository-owned inputs, or an explicitly selected review snapshot."""
    if upstream is None:
        if not data.get("originals"):
            raise ValueError("The baseline has no repository-owned original sources")
        packed = data.get("original_archive")
        if packed:
            owner = ROOT / packed["local"]
            if owner.is_symlink() or not owner.is_file():
                raise ValueError("Original archive must be a regular local file")
            if hashlib.sha256(owner.read_bytes()).hexdigest() != packed["sha256"]:
                raise ValueError("Original archive differs from baseline")
            seen = set()
            with tarfile.open(owner, "r:*") as archive:
                for member in archive:
                    if member.isdir():
                        continue
                    name = member.name
                    relative = pathlib.PurePosixPath(name)
                    if (not member.isfile() or relative.is_absolute() or ".." in relative.parts
                            or name in seen or name not in data["originals"]):
                        raise ValueError("Unsafe or unrecorded original archive member: " + name)
                    content = archive.extractfile(member).read()
                    if hashlib.sha256(content).hexdigest() != data["originals"][name]["sha256"]:
                        raise ValueError("Archived original differs from baseline: " + name)
                    seen.add(name)
                    yield name, content, member.mode
            if seen != set(data["originals"]):
                raise ValueError("Original archive omits recorded source inputs")
            return
        for name, original in data["originals"].items():
            source = ROOT / original["local"]
            if source.is_symlink() or not source.is_file():
                raise ValueError("Original source must be a regular local file: " + name)
            content = source.read_bytes()
            if hashlib.sha256(content).hexdigest() != original["sha256"]:
                raise ValueError("Repository-owned original differs from baseline: " + name)
            yield name, content, source.stat().st_mode
        return
    units = []
    for entry in git(upstream, "ls-tree", "-rz", data["revision"]).split(b"\0"):
        if not entry:
            continue
        metadata, name = entry.split(b"\t", 1)
        mode, kind, identifier = metadata.split()
        if kind != b"blob" or mode not in (b"100644", b"100755"):
            raise ValueError("Unsupported original source entry: " + name.decode())
        units.append((name.decode(), identifier, int(mode, 8) & 0o777))
    # git archive applies checkout attributes such as eol=crlf. Provenance pins
    # original blob bytes, independently of host and export presentation.
    process = subprocess.Popen(["git", "-C", str(upstream), "cat-file", "--batch"],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for name, identifier, mode in units:
            process.stdin.write(identifier + b"\n")
            process.stdin.flush()
            header = process.stdout.readline().split()
            if len(header) != 3 or header[0] != identifier or header[1] != b"blob":
                raise ValueError("Unable to retrieve original source: " + name)
            size = int(header[2])
            content = process.stdout.read(size)
            if len(content) != size or process.stdout.read(1) != b"\n":
                raise ValueError("Truncated original source: " + name)
            yield name, content, mode
        process.stdin.close()
        if process.wait():
            raise ValueError("Unable to retrieve pinned reference source")
    finally:
        if not process.stdin.closed:
            process.stdin.close()
        process.stdout.close()
        if process.poll() is None:
            process.terminate()
            process.wait()


def prepare_runtime_snapshot(data, snapshot, upstream=None, build_rules=True, dependency_only=False):
    """Overlay only engine adaptations explicitly selected in the baseline."""
    paths = data.get("runtime_adapted", [])
    if not isinstance(paths, list) or paths != sorted(set(paths)) or set(paths) - set(data["adapted"]):
        raise ValueError("Runtime sources must be selected adapted upstream modules")
    isolated = set(data.get("runtime_excluded", []))
    if (isolated.intersection(paths) or
            data.get("schema") == 1 and any(path.startswith("src/gpl/") for path in paths)):
        raise ValueError("Full GPL format readers cannot be overlaid into the core runtime")
    snapshot.mkdir(parents=True, exist_ok=True)
    owner = snapshot / ".sevenzip-runtime-source.json"
    if snapshot.is_symlink() or (any(snapshot.iterdir()) and not owner.is_file()):
        raise ValueError("Refusing an unowned runtime source directory")
    previous = json.loads(owner.read_text()) if owner.exists() else {"files": []}
    if previous.get("producer", "sevenzip-runtime-source") != "sevenzip-runtime-source":
        raise ValueError("Unexpected runtime source owner")
    if not owner.exists():
        owner.write_text(json.dumps({"producer": "sevenzip-runtime-source", "files": []}) + "\n")
    # A partial upstream update must reach the standalone engine as well as
    # statically linked consumers. Keep unchanged dependencies byte-exact.
    exact_overrides = (sorted(set(data.get("overrides", {})) & set(data["exact"]))
                       if not data.get("original_archive") else [])
    overlays = {source_path(path, data): ROOT / path for path in paths + exact_overrides}
    overlay_owners = {source_path(path, data): path for path in paths + exact_overrides}
    archive_sources = SourceArchives(ROOT, data)
    # Non-7-Zip C/header dependencies retain their declared snapshot paths.
    dependencies = data.get("runtime_dependencies", [])
    if dependencies != sorted(set(dependencies)):
        raise ValueError("Runtime dependencies must be sorted and unique")
    dependency_paths = {}
    format_sources = data.get("runtime_format_sources", [])
    if (not isinstance(format_sources, list) or format_sources != sorted(set(format_sources)) or
            set(format_sources) - set(dependencies) or
            any(not path.endswith((".c", ".cpp")) for path in format_sources)):
        raise ValueError("Format support sources must be selected runtime dependencies")
    for path in dependencies:
        relative = pathlib.PurePosixPath(path)
        if (relative.parts[:1] not in (("src",), ("third_party",)) or ".." in relative.parts or
                relative.suffix not in (".h", ".c", ".cpp", ".cppm", ".inl") or path in isolated or
                (data.get("schema") == 1 and path.startswith("src/gpl/")) or
                path in paths or not archive_sources.contains(path)):
            raise ValueError("Unsafe runtime source dependency: " + path)
        destination = data.get("runtime_paths", {}).get(path)
        if destination is None:
            if data.get("schema") == 2:
                raise ValueError("Runtime dependency has no recorded destination: " + path)
            destination = str(pathlib.PurePosixPath(*relative.parts[1:]))
        if destination in overlays:
            raise ValueError("Duplicate runtime source destination: " + destination)
        dependency_paths[path] = destination
        overlays[destination] = ROOT / path
        overlay_owners[destination] = path
    members = []
    selected = {source_path(path, data) for path in data["exact"]} if dependency_only else None
    for name, original, mode in original_sources(data, upstream):
        if selected is not None and name not in selected:
            continue
        destination = snapshot_path(snapshot, name)
        source = overlays.get(name)
        content = runtime_content(data, overlay_owners.get(name), name,
                                  archive_sources.read_bytes(overlay_owners[name])) if source else original
        if destination.is_symlink() or (source and source.is_symlink()):
            raise ValueError("Refusing linked runtime source: " + name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != content:
            destination.write_bytes(content)
            destination.chmod(mode)
        members.append(name)
    for name, source in overlays.items():
        if name in members:
            continue
        destination = snapshot_path(snapshot, name)
        if destination.is_symlink():
            raise ValueError("Refusing linked runtime dependency: " + name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        content = runtime_content(data, overlay_owners.get(name), name,
                                  archive_sources.read_bytes(overlay_owners[name]))
        if not destination.exists() or destination.read_bytes() != content:
            destination.write_bytes(content)
        members.append(name)
    # Remove only previously owned source paths; retain all incremental outputs.
    for name in set(previous["files"]) - set(members):
        snapshot_path(snapshot, name).unlink(missing_ok=True)
    owner.write_text(json.dumps({"producer": "sevenzip-runtime-source", "files": members}) + "\n")
    hashes = {}
    for path in paths + exact_overrides + dependencies:
        source = ROOT / path
        destination = snapshot / (dependency_paths[path] if path in dependencies else source_path(path, data))
        if source.is_symlink() or destination.is_symlink() or not destination.is_file():
            raise ValueError("Runtime adaptation requires regular source files: " + path)
        hashes[path] = hashlib.sha256(destination.read_bytes()).hexdigest()
    (snapshot / "runtime-adaptations.json").write_text(json.dumps(hashes, indent=2) + "\n")
    if build_rules:
        runtime_header_dependencies(snapshot, members, overlays,
                                    [dependency_paths[path] for path in dependencies
                                     if path.endswith((".c", ".cpp")) and path not in format_sources],
                                    [dependency_paths[path] for path in format_sources])


def verify(data, upstream):
    check_inventory(data)
    revisions = {data["revision"], *data.get("overrides", {}).values()}
    for revision in revisions:
        if git(upstream, "rev-parse", "--verify", revision + "^{commit}").decode().strip() != revision:
            raise ValueError("Recorded revision is unavailable: " + revision)
    packed_originals = ({name: content for name, content, _ in original_sources(data)}
                        if data.get("original_archive") else {})
    for path, original in data.get("notices", {}).items():
        if (ROOT / path).read_bytes() != packed_originals[original]:
            raise ValueError("Retained notice differs from its original: " + path)
    for path in data["exact"] + data["adapted"]:
        revision = data.get("overrides", {}).get(path, data["revision"])
        original = git(upstream, "show", revision + ":" + source_path(path, data))
        if packed_originals and packed_originals[source_path(path, data)] != original:
            raise ValueError("Archived original differs from recorded upstream unit: " + path)
        expected = data.get("original_sha256", {}).get(path)
        if expected and hashlib.sha256(original).hexdigest() != expected:
            raise ValueError("Original source hash mismatch: " + path)
        local = (packed_originals[source_path(path, data)] if data.get("original_archive")
                 and path in data["exact"] else (ROOT / path).read_bytes())
        if path in data["exact"] and local != original:
            raise ValueError("Exact vendor differs from baseline: " + path)
    for path in data["local"]:
        if data.get("schema") == 2:
            continue
        revision = data.get("overrides", {}).get(path, data["revision"])
        result = subprocess.run(
            ["git", "-C", str(upstream), "cat-file", "-e", revision + ":" + source_path(path)],
            capture_output=True,
        )
        if result.returncode == 0:
            raise ValueError("Local-only path now has an upstream counterpart: " + path)
    print(
        f"Verified {len(data['exact'])} exact, {len(data['adapted'])} adapted, "
        f"{len(data['local'])} local paths across {len(revisions)} revision(s)"
    )


def review(data, upstream, new_revision, paths):
    new = git(upstream, "rev-parse", "--verify", new_revision + "^{commit}").decode().strip()
    selected = paths or data["exact"] + data["adapted"]
    known = set(data["exact"] + data["adapted"])
    if set(selected) - known:
        raise ValueError("Review path absent from borrowed manifest")
    rows = [f"# Upstream update review: proposed {new}", ""]
    rows.append("Review changed dependencies and local adaptations before updating provenance.")
    rows.append("This report does not modify the port or sibling upstream checkout.\n")
    for path in selected:
        old = data.get("overrides", {}).get(path, data["revision"])
        source = source_path(path, data)
        status = git(upstream, "diff", "--name-status", old, new, "--", source).decode().strip()
        if not status:
            continue
        kind = "exact" if path in data["exact"] else "adapted"
        rows.append(f"- `{path}` ({kind}, `{old}` → `{new}`): `{status}`")
        if kind == "adapted":
            rows.append("  Review the upstream diff against the current Qt/platform adaptations.")
        else:
            rows.append("  Replace with exact new source only after dependency and build review.")
        extraction = data.get("extracted", {}).get(path)
        if extraction:
            rows.append("  Extracted symbols: " + ", ".join(extraction["symbols"]) + ".")
            rows.append("  Also compare: " + ", ".join("`" + unit + "`"
                for unit in extraction["local"]) + ".")
    if len(rows) == 4:
        rows.append("No selected upstream paths changed.")
    return "\n".join(rows) + "\n"


def reference_edited_source(data, path, original, root):
    owner = pathlib.PurePosixPath(data["adapted_edits"][path])
    if owner.is_absolute() or ".." in owner.parts:
        raise ValueError("Invalid reference edit owner")
    if owner.suffix == ".patch":
        if not data.get("symbols", {}).get(path):
            raise ValueError("Reference patch requires selected symbols")
        with tempfile.TemporaryDirectory(prefix="sevenzip-reference-patch-") as temporary:
            source = pathlib.Path(temporary) / path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(original)
            environment = os.environ.copy()
            for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE"):
                environment.pop(name, None)
            environment["GIT_CEILING_DIRECTORIES"] = str(pathlib.Path(temporary).resolve().parent)
            result = subprocess.run(
                ["git", "apply", "--whitespace=nowarn", "--include=" + path,
                 str((root / owner).resolve())], cwd=temporary, env=environment,
                capture_output=True)
            if result.returncode:
                raise ValueError("Reference patch context does not match: " +
                                 result.stderr.decode("utf-8", "replace").strip())
            adapted = source.read_bytes()
            if adapted == original:
                raise ValueError("Reference patch does not change its recorded source")
            return adapted
    edit = json.loads((root / owner).read_text())
    if edit.get("source") != path or not edit.get("symbols"):
        raise ValueError("Reference edit requires original path and selected symbols")
    for change in edit["replacements"]:
        before, after = change["before"].encode(), change["after"].encode()
        if not before or original.count(before) != 1:
            raise ValueError("Source adaptation context does not match exactly once")
        original = original.replace(before, after, 1)
    return original


def verify_reference(data, upstream, root=ROOT):
    """Check original inventory units from a non-7-Zip source baseline."""
    revision = data.get("revision", "")
    if not re.fullmatch(r"[0-9a-f]{40}", revision) or not data.get("repository"):
        raise ValueError("Reference requires repository and full commit hash")
    files = data.get("files", {})
    if not isinstance(files, dict) or not files:
        raise ValueError("Reference requires original-file SHA256 values")
    for path, expected in files.items():
        relative = pathlib.PurePosixPath(path)
        if relative.is_absolute() or ".." in relative.parts or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise ValueError("Invalid reference path/hash: " + path)
        original = git(upstream, "show", revision + ":" + path)
        if hashlib.sha256(original).hexdigest() != expected:
            raise ValueError("Reference source hash mismatch: " + path)
    # Linux baselines also use lists of adapted paths. Explicit maps additionally
    # locate the local owners for adaptation review (without pinning their bytes).
    adapted = data.get("owners", data.get("adapted", {}))
    source_inputs = SourceArchives(root)
    if isinstance(adapted, dict):
        for source, local in adapted.items():
            relative = pathlib.PurePosixPath(local)
            if (source not in files or relative.is_absolute() or ".." in relative.parts or
                    not source_inputs.contains(local)):
                raise ValueError("Invalid adapted reference owner: " + local)
            current = source_inputs.read_bytes(local)
            unchanged = source in data.get("unchanged", {}) or source_inputs.is_archived(local)
            if unchanged and hashlib.sha256(current).hexdigest() != files[source]:
                raise ValueError("Unchanged reference owner differs from original: " + local)
    for path in data.get("adapted_edits", {}):
        if path not in files:
            raise ValueError("Reference edit absent from original inventory")
        reference_edited_source(data, path, git(upstream, "show", revision + ":" + path), root)
    return len(files)


def review_reference(data, upstream, new_revision, paths, root=ROOT):
    """Compare original revisions and show adaptations; never merge a partial port."""
    verify_reference(data, upstream, root)
    new = git(upstream, "rev-parse", "--verify", new_revision + "^{commit}").decode().strip()
    selected = paths or sorted(data["files"])
    if set(selected) - set(data["files"]):
        raise ValueError("Review path absent from reference baseline")
    old = data["revision"]
    source_inputs = SourceArchives(root)
    rows = ["# Reference update review", "", "Repository: " + data["repository"],
            "Baseline: " + old, "Proposed: " + new, "",
            "Review dependencies, omitted scope and local fixes before updating the baseline.",
            "No source or baseline is modified. Partial adaptations require manual transfer.", ""]
    for path in selected:
        rows += ["## " + path, ""]
        diff = git(upstream, "diff", "--no-ext-diff", "--no-textconv", old, new, "--", path).decode()
        rows += ["```diff", diff.rstrip(), "```", ""] if diff else ["Upstream unchanged.", ""]
        adapted = data.get("owners", data.get("adapted", {}))
        edits = data.get("adapted_edits", {})
        if (isinstance(adapted, dict) and path in adapted) or path in edits:
            original = git(upstream, "show", old + ":" + path)
            if path in edits:
                local = edits[path]
                current = reference_edited_source(data, path, original, root)
                try:
                    reference_edited_source(data, path, git(upstream, "show", new + ":" + path), root)
                    rows += ["Recorded edits apply to the proposed original.", ""]
                except (ValueError, OSError) as error:
                    rows += ["Recorded edits require review: " + str(error), ""]
            else:
                local = adapted[path]
                current = source_inputs.read_bytes(local)
            try:
                original_text = original.decode("utf-8")
                current_text = current.decode("utf-8")
            except UnicodeDecodeError:
                original_text = current_text = None
            if original_text is None or b"\0" in original or b"\0" in current:
                rows += ["Binary local owner: " + local,
                         "Original SHA256: " + hashlib.sha256(original).hexdigest(),
                         "Local SHA256: " + hashlib.sha256(current).hexdigest(),
                         "Identical bytes." if original == current else "Local bytes differ.", ""]
                continue
            before = original_text.splitlines(keepends=True)
            after = current_text.splitlines(keepends=True)
            delta = "".join(difflib.unified_diff(before, after, fromfile=old + "/" + path,
                                               tofile=local))
            rows += ["Local adaptation against the baseline:", "", "```diff", delta.rstrip(),
                     "```", ""]
    return "\n".join(rows) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("inventory", "verify", "review", "runtime-snapshot", "dependency-snapshot", "header-snapshot",
                                            "reference-verify", "reference-review"))
    parser.add_argument("--snapshot", type=pathlib.Path)
    parser.add_argument("--original-only", action="store_true",
                        help="Prepare original sources without runtime overlays (SFX builds)")
    parser.add_argument("--no-build-rules", action="store_true",
                        help="With --original-only, retain byte-exact upstream build rules for a UI reference")
    parser.add_argument("--upstream", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path, default=MANIFEST)
    parser.add_argument("--new-revision", help="Proposed upstream commit for review")
    parser.add_argument("--path", action="append", default=[],
                        help="Recorded local path for 7-Zip, upstream path for reference review")
    parser.add_argument("--output", type=pathlib.Path, help="Write review outside the source tree")
    args = parser.parse_args()
    try:
        if args.command.startswith("reference-"):
            if not args.upstream:
                raise ValueError("Reference commands require --upstream")
            data = json.loads(args.manifest.read_text(encoding="utf-8"))
            count = verify_reference(data, args.upstream)
            if args.command == "reference-review":
                if not args.new_revision or not args.output:
                    raise ValueError("Review requires --new-revision and --output")
                report = review_reference(data, args.upstream, args.new_revision, args.path)
                with args.output.open("x", encoding="utf-8") as output:
                    output.write(report)
                print("Wrote " + str(args.output))
            else:
                print(f"Verified {count} original reference files at {data['revision']}")
            return 0
        data = load_manifest(args.manifest)
        if args.command == "header-snapshot":
            if not args.snapshot:
                raise ValueError("Header snapshot directory is required")
            prepare_header_snapshot(data, args.snapshot)
            return 0
        if args.command in ("runtime-snapshot", "dependency-snapshot"):
            if not args.snapshot:
                raise ValueError("Runtime snapshot directory is required")
            if args.no_build_rules and not args.original_only:
                raise ValueError("--no-build-rules requires --original-only")
            if args.original_only or args.command == "dependency-snapshot":
                data = dict(data, runtime_adapted=[], runtime_dependencies=[],
                            runtime_format_sources=[], overrides={})
            prepare_runtime_snapshot(data, args.snapshot, args.upstream,
                                     not args.no_build_rules and args.command != "dependency-snapshot",
                                     dependency_only=args.command == "dependency-snapshot")
            return 0
        if args.command == "inventory":
            check_inventory(data)
            print("Recorded inventory matches current borrowed sources")
            return 0
        if not args.upstream:
            raise ValueError("Verify and review require --upstream")
        verify(data, args.upstream)
        if args.command == "review":
            if not args.new_revision or not args.output:
                raise ValueError("Review requires --new-revision and --output")
            report = review(data, args.upstream, args.new_revision, args.path)
            if args.output.exists():
                raise ValueError("Review output already exists: " + str(args.output))
            args.output.write_text(report, encoding="utf-8")
            print("Wrote " + str(args.output))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("upstream_baseline: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
