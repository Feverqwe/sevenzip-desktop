"""Build workspace with durable per-machine tools, caches and outputs."""

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys

if __package__:
    from .project_paths import harness_root
else:
    from project_paths import harness_root


def host_tag():
    system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[platform.system()]
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    return system + "-" + machine


def workspace_base(root):
    return Path(os.environ.get("SEVENZIP_WORKSPACE", harness_root(root) / "build")).absolute()


def workspace_root(root):
    root = Path(root).resolve()
    mount = workspace_base(root)
    configured_source = os.environ.get("SEVENZIP_SOURCE_ROOT")
    if configured_source and Path(configured_source).resolve() != root:
        # Nested workflow fixtures own their outputs in their temporary workspace.
        temporary = Path(os.environ.get("TMPDIR", mount)).resolve()
        if root.is_relative_to(temporary) or root.is_relative_to(mount.resolve()):
            return root
    return Path(os.environ.get("SEVENZIP_WORKSPACE", mount)).absolute()


def branch_name(root):
    root = Path(root).resolve()
    if (root / ".git").exists():
        return subprocess.check_output(
            ["git", "-C", str(root), "symbolic-ref", "--quiet", "--short", "HEAD"], text=True).strip()
    context = root / ".runner-context.json"
    if context.is_file():
        return json.loads(context.read_text())["branch"]
    return "snapshot"


def branch_component(branch):
    # Reversible ASCII escaping also keeps case-distinct branches separate
    # on Windows, without a digest or lossy truncation.
    return "".join(chr(byte) if 97 <= byte <= 122 or 48 <= byte <= 57 or byte in (45, 95)
                   else f"~{byte:02x}" for byte in branch.encode("utf-8"))


def branch_key(root):
    root = Path(root)
    if not (root / ".git").exists() and not (root / ".runner-context.json").is_file():
        return os.environ.get("SEVENZIP_BRANCH_KEY", branch_component("snapshot"))
    return branch_component(branch_name(root))


def directory(root, category):
    if category not in ("work", "artifacts", "tools", "logs", "cache", "tmp", "packages"):
        raise ValueError("Unknown workspace category")
    workspace = workspace_root(root)
    if category == "cache" and os.environ.get("SEVENZIP_CACHE_ROOT"):
        cache = Path(os.environ["SEVENZIP_CACHE_ROOT"])
        if not cache.is_absolute():
            raise ValueError("Cache directory must be absolute")
        return cache / host_tag()
    if category == "tmp":
        return workspace / "b" / branch_key(root) / "t"
    if category in ("work", "artifacts", "logs", "tmp", "packages"):
        workspace = workspace / "branches" / branch_key(root)
    return workspace / category / host_tag()


def build_root(root, preset="dev"):
    return workspace_root(root) / "branches" / branch_key(root) / "work" / host_tag()


def host_tools(root):
    shared = os.environ.get("SEVENZIP_SHARED_TOOLS_ROOT")
    if shared:
        tools = Path(shared)
        if not tools.is_absolute():
            raise ValueError("Shared tools directory must be absolute")
        return tools / host_tag()
    if platform.system() == "Darwin":
        return harness_root(root) / ".codex/tools" / host_tag()
    return directory(root, "tools")


def checked_output(root, path):
    return contained_output(require_workspace(root), path)


def contained_output(workspace, path):
    roots = [workspace.resolve()]
    path = Path(path).absolute()
    resolved = path.resolve()
    if resolved in roots or not any(resolved.is_relative_to(root) for root in roots):
        raise ValueError(f"Output must be inside the project workspace: {path}")
    return path


def build_directory(root, preset="dev"):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", preset):
        raise ValueError("Invalid build preset")
    return build_root(root, preset) / preset


def require_workspace(root):
    mount = workspace_base(root)
    if mount.is_symlink() or getattr(mount, "is_junction", lambda: False)():
        raise ValueError("Persistent workspace must use its direct path")
    workspace = workspace_root(root)
    return workspace


def environment(root, preset="dev"):
    workspace = require_workspace(root)
    workspace.mkdir(parents=True, exist_ok=True)
    values = {
        "SEVENZIP_WORKSPACE": str(workspace),
        "SEVENZIP_SOURCE_ROOT": str(Path(root).resolve()),
        "SEVENZIP_BRANCH_KEY": branch_key(root),
        "SEVENZIP_BUILD_ROOT": str(build_root(root, preset)),
        "SEVENZIP_TEST_SCRATCH_ROOT": str(directory(root, "tmp").resolve() / "ctest"),
        "SEVENZIP_ARTIFACT_ROOT": str(directory(root, "artifacts")),
        "SEVENZIP_OUTPUT_ROOT": str(directory(root, "artifacts")),
        "SEVENZIP_TOOLS_ROOT": str(host_tools(root)),
        "SEVENZIP_LOG_ROOT": str(directory(root, "logs")),
        "CCACHE_DIR": str(directory(root, "cache") / "ccache"),
        "CCACHE_TEMPDIR": str(directory(root, "tmp") / "ccache"),
        "TMPDIR": str(directory(root, "tmp")),
        "TMP": str(directory(root, "tmp")),
        "TEMP": str(directory(root, "tmp")),
        "PIP_CACHE_DIR": str(directory(root, "cache") / "pip"),
        "VCPKG_DOWNLOADS": str(directory(root, "cache") / "vcpkg/downloads"),
        "VCPKG_DEFAULT_BINARY_CACHE": str(directory(root, "cache") / "vcpkg/archives"),
        "VCPKG_INSTALLED_DIR": str(directory(root, "packages") / "vcpkg"),
        "VCPKG_ROOT": str((host_tools(root) if os.environ.get("SEVENZIP_SHARED_TOOLS_ROOT")
                           else directory(root, "tools")) / "vcpkg"),
    }
    cache_names = ("CCACHE_DIR", "PIP_CACHE_DIR", "VCPKG_DOWNLOADS",
                   "VCPKG_DEFAULT_BINARY_CACHE")
    paths = [contained_output(workspace, value) for name, value in values.items()
             if name not in ("SEVENZIP_WORKSPACE", "SEVENZIP_SOURCE_ROOT", "SEVENZIP_BRANCH_KEY", "SEVENZIP_TOOLS_ROOT", "VCPKG_ROOT", *cache_names, "SEVENZIP_BUILD_ROOT", "CCACHE_TEMPDIR", "TMPDIR", "TMP", "TEMP")]
    paths.extend(contained_output(directory(root, "cache"), values[name]) for name in cache_names)
    paths.extend([contained_output(workspace, values["TMPDIR"]),
                  contained_output(workspace, values["CCACHE_TEMPDIR"]),
                  contained_output(workspace, values["SEVENZIP_BUILD_ROOT"])])
    tools = host_tools(root)
    if platform.system() == "Darwin" and not tools.resolve().is_relative_to(harness_root(root).resolve()):
        raise ValueError("Local tools directory escapes the project")
    paths.append(tools)
    paths.append(Path(values["VCPKG_ROOT"]))
    for path in set(paths):
        if not path.is_dir():
            path.mkdir(parents=True, exist_ok=True)
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("env", "status", "path", "check-output"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--format", choices=("shell", "json"), default="shell")
    parser.add_argument("--category", default="work")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--preset", default="dev")
    args = parser.parse_args()
    if args.action == "env":
        values = environment(args.root, args.preset)
        if args.format == "json":
            print(json.dumps(values))
        else:
            for name, value in values.items():
                print(f"export {name}={shlex.quote(value)}")
    elif args.action == "check-output":
        if args.output is None:
            parser.error("check-output requires --output")
        print(checked_output(args.root, args.output))
    else:
        require_workspace(args.root)
        if args.action == "path":
            print(host_tools(args.root) if args.category == "host-tools" else
                  build_root(args.root, args.preset) if args.category == "work" else directory(args.root, args.category))
        else:
            print(workspace_root(args.root))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"workspace: {error}")
