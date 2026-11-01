"""Configured guest workspaces and branch source paths."""

import subprocess
from .build_paths import branch_component
from .runner_config import load_config


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
