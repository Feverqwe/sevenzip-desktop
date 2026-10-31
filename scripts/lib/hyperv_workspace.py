"""Allowlisted guest workspace roots and fail-closed data-volume identity."""

from contextlib import contextmanager
import queue
import shlex
import subprocess
import threading
from .build_paths import branch_component
from .runner_config import load_config


@contextmanager
def shared_storage(record, output):
    """Keep the verified mount busy while separate SSH phases use it."""
    wrapper = record.get("config", {}).get("build_wrapper")
    if not wrapper:
        yield
        return
    code = "import sys;print('STORAGE_PINNED',flush=True);sys.stdin.buffer.read()"
    command = shlex.join([*wrapper, record["config"]["python"], "-u", "-c", code])
    process = subprocess.Popen([*record["ssh_argv"], command], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=output, text=True)
    ready = queue.Queue()
    threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True).start()
    try:
        try:
            line = ready.get(timeout=20)
        except queue.Empty:
            raise TimeoutError("Shared storage did not become ready") from None
        if line.strip() != "STORAGE_PINNED" or process.poll() is not None:
            raise RuntimeError("Shared storage mount could not be pinned")
        yield
    finally:
        try:
            process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
        process.stdout.close()


def load_workspaces():
    return {platform: entry["workspace"] for platform, entry in load_config().items()}


def current_branch(root):
    result = subprocess.run(["git", "symbolic-ref", "--quiet", "--short", "HEAD"],
                            cwd=root, text=True, capture_output=True)
    if result.returncode == 1:
        raise ValueError("Runner requires a Git branch; switch from detached HEAD before running")
    result.check_returncode()
    return result.stdout.strip()


def worktree_root(workspace, branch):
    return workspace["root"] + "/branch-" + branch_component(branch)


def mount_guard(workspace):
    return ('test "$(findmnt -n -o UUID --mountpoint ' + shlex.quote(workspace["mount"]) + ')" = '
            + shlex.quote(workspace["filesystem_uuid"]) + '\n')
