#!/usr/bin/env python3
"""Configure, build and test a focused scenario in the existing build tree."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
import time

from lib.runner_timing import mark
from lib.build_paths import build_directory, directory

INVOCATION_STARTED = time.monotonic()
ROOT = Path(__file__).resolve().parents[1]


def run(command, root, timings, phase, capture=False):
    started = time.monotonic()
    print(f"[{phase}]", flush=True)
    mark(phase + "_start")
    try:
        return subprocess.run(command, cwd=root, check=True, text=True,
                              stdout=subprocess.PIPE if capture else None)
    finally:
        mark(phase + "_end")
        timings[phase] = round(time.monotonic() - started, 3)


def executable_targets(build):
    artifacts = {os.path.normcase(str(Path(path).resolve())): target
                 for line in (build / "verify-artifacts.tsv").read_text().splitlines()
                 for target, path in [line.split("\t", 1)]}
    commands = {parts[0]: parts[1:] for line in (build / "verify-commands.tsv").read_text().splitlines()
                for parts in [line.split("\t")]}
    return {name: sorted({artifacts[os.path.normcase(str(Path(argument).resolve()))]
                         for argument in arguments
                         if os.path.normcase(str(Path(argument).resolve())) in artifacts})
            for name, arguments in commands.items()}


def selected_targets(tests, artifacts, build):
    targets = set()
    for test in tests:
        if test["name"] not in artifacts:
            raise ValueError(f"No CMake registration for {test['name']}")
        targets.update(artifacts[test["name"]])
    return sorted(targets)


def verify(root, args, profile):
    build = build_directory(root, args.preset)
    build.mkdir(parents=True, exist_ok=True)
    reports = directory(root, "logs") / args.preset
    reports.mkdir(parents=True, exist_ok=True)
    report = reports / ("verify-plan.json" if args.plan else "verify-result.json")
    timings = {}
    result = {"status": "running", "profile": args.profile, "selection": args.tests or profile["tests"],
              "preset": args.preset, "tests": [], "targets": [], "seconds": timings,
              "run_token": os.environ.get("SEVENZIP_RUN_TOKEN")}
    try:
        if not args.plan and args.prepare_runtime and \
                os.environ.get("SEVENZIP_RUNTIME_PREPARED") != "1":
            run([sys.executable, str(root / "scripts/prepare-runtime.py"), "--preset", args.preset],
                root, timings, "runtime")
        run(["bash", str(root / "scripts/build.sh"), args.preset, "--configure-only"],
            root, timings, "configure")
        run(["cmake", f"-DSEVENZIP_TEST_DIR={build}", "-P",
             str(root / "cmake/ReadCTestCommands.cmake")], root, timings, "targets")
        result["compiler_launchers"] = {
            key: value for line in (build / "CMakeCache.txt").read_text().splitlines()
            if "=" in line and ":" in line.split("=", 1)[0]
            for typed_key, value in [line.split("=", 1)]
            for key in [typed_key.split(":", 1)[0]]
            if key in ("CMAKE_C_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_LAUNCHER", "CMAKE_OBJCXX_COMPILER_LAUNCHER")}
        selection = ["bash", str(root / "scripts/ctest.sh"), "--preset", args.preset,
                     "-R", result["selection"]]
        data = json.loads(run([*selection, "--show-only=json-v1"], root, timings,
                              "select", capture=True).stdout)
        tests = data["tests"]
        if not tests:
            raise ValueError("Selection contains no enabled tests (slow suites require explicit opt-in)")
        if os.environ.get("SEVENZIP_INCLUDE_SLOW_TESTS", "0") != "1" and any(
                prop["name"] == "LABELS" and "slow" in prop["value"]
                for test in tests for prop in test.get("properties", [])):
            raise ValueError("Selection pulls a slow fixture; explicit long-test opt-in is required")
        result["tests"] = [test["name"] for test in tests]
        result["targets"] = selected_targets(tests, executable_targets(build), build)
        print("Tests: " + ", ".join(result["tests"]), flush=True)
        print("Build targets: " + (", ".join(result["targets"]) or "none (script tests)"), flush=True)
        if args.plan:
            result["status"] = "planned"
            return
        if result["targets"]:
            run(["bash", str(root / "scripts/build.sh"), args.preset, "--build-only", *result["targets"]],
                root, timings, "build")
        # Freeze the fixture-expanded selection used for the build as well.
        pattern = "^(" + "|".join(re.escape(test["name"]) for test in tests) + ")$"
        run(["bash", str(root / "scripts/ctest.sh"), "--preset", args.preset,
             "-R", pattern, "--no-tests=error", "--output-on-failure"], root, timings, "test")
        result["status"] = "passed"
    except BaseException as error:
        result["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        result["error"] = str(error)
        raise
    finally:
        report.write_text(json.dumps(result, indent=2) + "\n")
        print("Timing: " + ", ".join(f"{name}={seconds:.2f}s" for name, seconds in timings.items()), flush=True)
        print(f"Result: {result['status']}; {report}", flush=True)


def parse_args(argv, profiles):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", nargs="?", choices=sorted(profiles))
    parser.add_argument("--tests", help="CTest regex; CMake supplies actual executable targets")
    parser.add_argument("--preset", choices=["dev", "modules-dev", "modules-gcc-dev",
                                           "modules-linux-dev", "vcpkg-dev"], default="dev",
                        help="test-enabled preset; build the application with scripts/build.sh release")
    parser.add_argument("--platform", choices=["local", "linux", "windows", "all"], default="linux",
                        help="validation platform (default: linux runner)")
    parser.add_argument("--plan", action="store_true", help="configure and show tests/targets without building or testing")
    parser.add_argument("--list", action="store_true", help="list scenario profiles")
    parser.add_argument("--prepare-runtime", action="store_true",
                        help="prepare or refresh the native engine/SFX before verification")
    parser.add_argument("--interactive", action="store_true", help="use the logged-on Windows desktop")
    parser.add_argument("--elevated", action="store_true", help="run privileged Windows fixtures on that desktop")
    args = parser.parse_args(argv)
    if not args.list and not args.profile and not args.tests:
        parser.error("select a profile or --tests (use --list to inspect profiles)")
    if args.elevated and args.platform not in ("windows", "all"):
        parser.error("--elevated requires a Windows runner")
    return args


def main(argv=None):
    profiles = json.loads((ROOT / "scripts/config/verify-profiles.json").read_text())
    args = parse_args(sys.argv[1:] if argv is None else argv, profiles)
    if args.list:
        for name, profile in profiles.items():
            print(f"{name}: {profile['description']}")
        return 0
    profile = profiles.get(args.profile, {"tests": args.tests})
    if args.platform != "local":
        platforms = ("linux", "windows") if args.platform == "all" else (args.platform,)
        for platform in platforms:
            command = [sys.executable, str(ROOT / "scripts/hyperv-runner.py"), "run", platform,
                       "--preset", args.preset]
            if platform == "windows" and (args.interactive or args.elevated or profile.get("interactive")):
                command.append("--interactive")
            if platform == "windows" and args.elevated:
                command.append("--elevated")
            if args.prepare_runtime and not args.plan:
                command.append("--prepare-runtime")
            command += ["--", "scripts/verify.py", "--platform", "local", "--preset", args.preset]
            if args.profile:
                command.append(args.profile)
            if args.tests:
                command += ["--tests", args.tests]
            if args.plan:
                command.append("--plan")
            if args.prepare_runtime:
                command.append("--prepare-runtime")
            environment = dict(os.environ, SEVENZIP_INVOCATION_STARTED=str(INVOCATION_STARTED))
            result = subprocess.run(command, cwd=ROOT, env=environment)
            if result.returncode:
                return result.returncode
        return 0
    from lib.toolchain import apply_environment
    apply_environment(ROOT, args.preset)
    verify(ROOT, args, profile)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"verify: {error}", file=sys.stderr)
        raise SystemExit(error.returncode if isinstance(error, subprocess.CalledProcessError) else 1)
