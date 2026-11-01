#!/usr/bin/env python3
"""Install the pinned cp2 locally or on configured runners; reuse existing builds."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
import os

from lib import hyperv_cp2_install
from lib.build_paths import environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("local", "linux", "windows", "all"))
    parser.add_argument("--binary", help="Reuse a matching binary already on the selected machine")
    args = parser.parse_args()
    if args.platform == "all" and args.binary:
        parser.error("--binary requires a single selected machine")
    os.environ.update(environment(Path(__file__).resolve().parents[1]))
    if args.platform == "local":
        print(json.dumps(hyperv_cp2_install.local(args.binary)))
        return
    spec = importlib.util.spec_from_file_location("runner", Path(__file__).with_name("hyperv-runner.py"))
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    for platform in ("linux", "windows") if args.platform == "all" else (args.platform,):
        record = runner.discover(platform)
        result = hyperv_cp2_install.ensure(record, runner.remote, prebuilt=args.binary)
        print(json.dumps(dict(platform=platform, **result)), flush=True)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(f"cp2 installation failed (command exit {error.returncode})", file=sys.stderr)
        raise SystemExit(1)
    except (OSError, ValueError) as error:
        print(f"cp2 installation: {error}", file=sys.stderr)
        raise SystemExit(1)
