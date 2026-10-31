"""Load machine-local runner connections and toolchains before remote actions."""

import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import sys
import uuid

if __package__:
    from .project_paths import harness_root
else:
    from project_paths import harness_root

CONFIG_PATH = harness_root(Path(__file__).resolve().parents[2]) / "scripts/config/runners.local.json"


def text(value, name):
    if not isinstance(value, str) or not value or any(c in value for c in '\r\n\0"\'%'):
        raise ValueError(f"Invalid runner setting: {name}")
    return value


def arguments(value, name):
    if not isinstance(value, list) or not value:
        raise ValueError(f"{name} must be a nonempty argument list")
    for item in value:
        text(item, name)
    return value


def workspace(value, platform):
    if not isinstance(value, dict) or set(value) - {"root", "mount", "filesystem_uuid"}:
        raise ValueError("Workspace requires a root and optional mount/UUID pair")
    root = text(value.get("root"), "workspace.root")
    path = PureWindowsPath(root) if platform == "windows" else PurePosixPath(root)
    if (not path.is_absolute() or (platform == "windows" and not re.match(r"^[A-Za-z]:/", root))
            or "\\" in root or any(p in (".", "..", "") for p in root.split("/")[1:])
            or len(path.parts) < 4):
        raise ValueError("Workspace must be an absolute project directory, not a volume or user root")
    if ("mount" in value) != ("filesystem_uuid" in value):
        raise ValueError("Workspace mount and filesystem_uuid must be supplied together")
    if "mount" in value:
        mount = text(value["mount"], "workspace.mount")
        identifier = value["filesystem_uuid"]
        if (platform != "linux" or not PurePosixPath(mount).is_absolute()
                or ".." in PurePosixPath(mount).parts
                or not path.is_relative_to(mount)):
            raise ValueError("Workspace must be inside its configured Linux mount")
        if not isinstance(identifier, str) or str(uuid.UUID(identifier)) != identifier:
            raise ValueError("Workspace UUID must use canonical form")
    return value


def load_config(path=None):
    path = Path(path or os.environ.get("SEVENZIP_RUNNER_CONFIG", CONFIG_PATH)).expanduser()
    try:
        data = json.loads(path.read_text())
    except FileNotFoundError:
        raise ValueError(f"Runner configuration missing: {path}. Copy scripts/config/runners.example.json "
                         "to runners.local.json and configure your hosts first.") from None
    if not isinstance(data, dict) or data.get("schema") != 1 or set(data) != {"schema", "runners"}:
        raise ValueError("Runner configuration requires schema 1 and runners")
    entries = data["runners"]
    if not isinstance(entries, dict) or not entries or set(entries) - {"linux", "windows"}:
        raise ValueError("Configure linux and/or windows runners")
    common = {"workspace", "ssh", "ready_command", "python", "qt_prefix", "path", "build_wrapper"}
    for platform, entry in entries.items():
        if not isinstance(entry, dict) or set(entry) - common - {"vcvars", "computer_name"}:
            raise ValueError(f"Unknown runner settings for {platform}")
        workspace(entry.get("workspace"), platform)
        if "build_wrapper" in entry:
            if platform != "linux":
                raise ValueError("Build wrappers currently require Linux")
            arguments(entry["build_wrapper"], "build_wrapper")
        if ("ssh" in entry) == ("ready_command" in entry):
            raise ValueError("Each runner requires exactly one of ssh or ready_command")
        if "ssh" in entry:
            ssh = arguments(entry["ssh"], "ssh")
            if Path(ssh[0]).name not in ("ssh", "ssh.exe") or len(ssh) < 2 or ssh[-1].startswith("-"):
                raise ValueError("ssh must contain an SSH executable, options and destination")
        else:
            arguments(entry["ready_command"], "ready_command")
        for name in ("python", "qt_prefix"):
            text(entry.get(name), name)
        if not isinstance(entry.get("path"), list):
            raise ValueError("Runner path must be a list of tool directories")
        for directory in entry["path"]:
            text(directory, "path")
        if platform == "windows":
            text(entry.get("vcvars"), "vcvars")
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", text(entry.get("computer_name"), "computer_name")):
                raise ValueError("Invalid Windows computer name")
    return entries


def runner_config(platform):
    entries = load_config()
    if platform not in entries:
        raise ValueError(f"No {platform} runner configured")
    return entries[platform]


if __name__ == "__main__":
    if sys.argv[1:] != ["windows-host"]:
        raise SystemExit("Usage: runner_config.py windows-host")
    try:
        print(runner_config("windows")["computer_name"])
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
