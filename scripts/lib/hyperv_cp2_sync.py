"""Synchronize through pinned cp2 and the configured authenticated SSH."""

import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time

from . import cp2_install, cp2_snapshot, hyperv_cp2_install
from .hyperv_transport import PROTECTED
from .build_paths import directory as workspace_directory, host_tools

ROOT = Path(__file__).resolve().parents[2]


def ssh_launcher():
    source = ROOT / "tools/cp2/cp2-ssh.rs"
    tools = host_tools(ROOT)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()[:20]
    directory = tools / "cp2-ssh" / digest
    binary = directory / ("ssh.exe" if os.name == "nt" else "ssh")
    if binary.exists():
        return binary
    directory.mkdir(parents=True, exist_ok=True)
    with cp2_install.install_lock(directory / "install.lock"):
        if not binary.exists():
            pin = hyperv_cp2_install.pin()
            cargo, environment = cp2_install.rust_environment(tools, directory, pin)
            with tempfile.TemporaryDirectory(dir=directory) as temporary:
                staged = Path(temporary) / binary.name
                subprocess.run([str(cargo.with_name("rustc" + (".exe" if os.name == "nt" else ""))),
                                "+" + pin["rust"], "--edition=2021", "-O", str(source), "-o", str(staged)],
                               env=environment, check=True)
                staged.replace(binary)
    return binary


def destination_filesystem(record, root, remote):
    if record["platform"] != "linux" or not record.get("config", {}).get("build_wrapper"):
        return None
    response = remote(record, shlex.join([
        "findmnt", "--json", "--target", root, "--output", "FSTYPE"]), capture_output=True)
    mounts = json.loads(response.stdout).get("filesystems", [])
    if len(mounts) != 1 or not isinstance(mounts[0].get("fstype"), str) or not mounts[0]["fstype"]:
        raise ValueError("Cannot determine source destination filesystem")
    return mounts[0]["fstype"]


def command(binary, record, source, root, receiver, checksum=False, filesystem=None):
    argv = [str(binary), str(source) + "/", record["ssh_argv"][-1] + ":" + root,
            "--remote-path", receiver, "--no-auto-install", "--delete", "--no-progress"]
    if record["platform"] == "windows":
        argv.append("--no-perms")
    if filesystem == "cifs":
        argv += ["--timestamp-resolution-ns", "100"]
    if checksum:
        argv += ["--checksum", "--verify"]
    for pattern in (*PROTECTED, "__pycache__/", "*.pyc", ".DS_Store"):
        argv += ["--exclude", pattern]
    return argv


def synchronize(record, platform, source, root, token, directory, remote, powershell, output,
                checksum=False):
    started = time.monotonic()
    filesystem = destination_filesystem(record, root, remote)
    client = hyperv_cp2_install.local()["path"]
    launcher = ssh_launcher()
    receiver = hyperv_cp2_install.ensure(record, remote, output)
    preparation = time.monotonic() - started
    key = hashlib.sha256(json.dumps([record["ssh_argv"], root]).encode()).hexdigest()
    cache = workspace_directory(ROOT, "cache") / "cp2-times" / (key + ".json")
    # Keep OpenSSH control paths short enough for Unix-domain socket limits.
    with tempfile.TemporaryDirectory(prefix="cp2-", dir=workspace_directory(ROOT, "tmp")) as temporary, \
            tempfile.TemporaryDirectory(prefix="7z-ssh-", dir="/tmp" if os.name != "nt" else None) as sockets:
        temporary = Path(temporary)
        staged = temporary / "source"
        entries = cp2_snapshot.prepare(source, staged, cache, receiver["time_ns"])
        argv = list(record["ssh_argv"])
        argv[0] = shutil.which(argv[0]) or argv[0]
        config = temporary / "ssh-argv"
        config.write_bytes("\0".join(argv).encode())
        environment = dict(os.environ, PATH=str(launcher.parent) + os.pathsep + os.environ["PATH"],
                           SEVENZIP_CP2_SSH_ARGV=str(config), TMPDIR=sockets)
        if platform == "windows":
            environment["SEVENZIP_CP2_WINDOWS_PROBE"] = powershell(
                "[Console]::Out.WriteLine($env:PROCESSOR_ARCHITECTURE)")
        try:
            started = time.monotonic()
            result = subprocess.run(command(client, record, staged, root, receiver["server_path"],
                                            checksum, filesystem),
                                    env=environment, stdout=output, stderr=subprocess.STDOUT, check=True)
            transfer = time.monotonic() - started
            cp2_snapshot.commit(cache, entries)
        finally:
            # Only this invocation can own sockets in the private directory.
            for socket in Path(sockets).glob("cp2-ssh-*"):
                subprocess.run([*argv[:-1], "-S", str(socket), "-O", "exit", argv[-1]],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    details = {"preparation_seconds": round(preparation, 3), "transfer_seconds": round(transfer, 3),
               "receiver": receiver, "checksum": checksum, "destination_filesystem": filesystem,
               "timestamp_cache": str(cache),
               "exit_code": result.returncode}
    (directory / "cp2-sync.json").write_text(json.dumps(details, indent=2) + "\n")
    return details
