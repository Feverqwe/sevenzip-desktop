"""Locate shared local tooling without changing the worktree being developed."""

from pathlib import Path
import subprocess
import sys


def harness_root(source_root):
    root = Path(source_root).resolve()
    if not (root / ".git").is_file():
        return root
    common = subprocess.check_output(
        ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
        cwd=root, text=True).strip()
    return Path(common).resolve().parent


if __name__ == "__main__":
    print(Path(sys.argv[1]).resolve().as_posix())
