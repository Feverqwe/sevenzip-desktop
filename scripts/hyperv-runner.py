#!/usr/bin/env python3
"""Sync current source and run checks in a persistent guest worktree per Git branch."""

import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import datetime
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
sys.dont_write_bytecode = True
import tempfile
import time
import uuid

from lib.hyperv_interactive import interactive_command
from lib.hyperv_source import stage_tree
from lib.hyperv_transport import collect_logs
from lib import hyperv_cp2_sync
from lib.hyperv_workspace import current_branch, load_workspaces, worktree_root
from lib.runner_config import load_config, runner_config
from lib.project_paths import harness_root
from lib.runtime_layout import codec_runtime
from lib.build_paths import branch_component, directory, environment as workspace_environment

ROOT = Path(__file__).resolve().parents[1]
HARNESS = harness_root(ROOT)
STATE = directory(ROOT, "logs") / "runners"


def invoke(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def powershell(script):
    wrapped = ("$ProgressPreference='SilentlyContinue'; $ErrorActionPreference='Stop'; "
               "try {\n" + script + "\n} catch { Write-Error $_; exit 1 }")
    return "powershell.exe -NoProfile -NonInteractive -EncodedCommand " + base64.b64encode(
        wrapped.encode("utf-16le")).decode()


def discover(platform):
    config = runner_config(platform)
    if "ready_command" in config:
        result = invoke(config["ready_command"], capture_output=True)
        record = json.loads(result.stdout)
    else:
        record = {"ssh_argv": config["ssh"], "vm": platform}
    if (not isinstance(record, dict) or not isinstance(record.get("vm"), str)
            or not isinstance(record.get("ssh_argv"), list) or len(record["ssh_argv"]) < 2
            or not all(isinstance(arg, str) and arg for arg in record["ssh_argv"])):
        raise ValueError("Readiness command must return vm and a nonempty ssh_argv array")
    record.update(platform=platform, config=config, name=record["vm"], id=record.get("vm_id"))
    return record


def remote(record, script, **kwargs):
    command = "bash -se"
    if record["platform"] == "windows":
        # Read a bounded payload without waiting for OpenSSH's delayed EOF.
        script = base64.b64encode(script.encode("utf-8")).decode("ascii")
        command = powershell(
            f"$payload=[char[]]::new({len(script)}); "
            "$received=[Console]::In.ReadBlock($payload,0,$payload.Length); "
            "if($received -ne $payload.Length){throw 'Truncated runner command'}; "
            "$source=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String((-join $payload))); "
            "& ([ScriptBlock]::Create($source))")
    return invoke([*record["ssh_argv"], command], input=script, **kwargs)


def lock_command(record, root, token, action, **kwargs):
    code = (ROOT / "scripts/lib/hyperv_lock.py").read_text()
    if action == "recover":
        operation = f"root=Path({root!r})\nrecord=json.loads((root.with_name(root.name+'.lock')/'owner.json').read_text())\n"
        operation += "operate(root, record['token'], 'release-completed')\n"
    else:
        operation = f"operate({root!r}, {token!r}, {action!r})\n"
    code += "\ntry:\n" + "".join("    " + line + "\n" for line in operation.splitlines())
    code += ("except (ValueError, OSError) as error:\n"
             "    import sys\n    print(f'Runner lock: {error}', file=sys.stderr)\n"
             "    raise SystemExit(1)\n")
    encoded = base64.b64encode(code.encode()).decode()
    statement = f"import base64; exec(base64.b64decode('{encoded}'))"
    if record["platform"] == "windows":
        script = f"& '{record['config']['python']}' -c \"{statement}\"\nif ($LASTEXITCODE -ne 0) {{ exit $LASTEXITCODE }}\n"
    else:
        script = shlex.quote(record["config"]["python"]) + " -c " + shlex.quote(statement) + "\n"
    return remote(record, script, **kwargs)


def command_payload(preset, command):
    commands = [command] if command is not None else [["bash", "scripts/check.sh"]]
    if command is None and preset == "release":
        commands += [["bash", "scripts/build.sh", "release"]]
    return "set -euo pipefail\n" + "".join(shlex.join(item) + "\n" for item in commands)


def clang_tidy_mode():
    value = os.environ.get("SEVENZIP_ENABLE_CLANG_TIDY")
    if value is None:
        return None
    value = value.upper()
    if value not in ("ON", "OFF"):
        raise ValueError("SEVENZIP_ENABLE_CLANG_TIDY must be ON or OFF")
    return value


def diagnostic_environment():
    values = {}
    choices = {
        'SEVENZIP_BUILD_MODE': ('lite', 'debug', 'sanitize'),
        'SEVENZIP_DEBUG_SYMBOLS': ('ON', 'OFF'),
        'SEVENZIP_ENABLE_SANITIZERS': ('ON', 'OFF'),
        'SEVENZIP_COMPACT_TEST_BINARIES': ('ON', 'OFF'),
        'SEVENZIP_UI_REPORTS': ('ON', 'OFF'),
        'SEVENZIP_CAPTURE_SCREENSHOTS': ('0', '1'),
        'SEVENZIP_SCRIPT_OUTPUT': ('compact', 'verbose'),
    }
    for name, allowed in choices.items():
        if name not in os.environ:
            continue
        value = os.environ[name]
        if allowed == ('ON', 'OFF'):
            value = value.upper()
        if value not in allowed:
            raise ValueError(name + ' must be ' + ' or '.join(allowed))
        values[name] = value
    return values


def execute_check(record, platform, preset, root, token, command=None,
                  prepare_runtime=False, output=None, interactive=False, elevated=False):
    import os
    config = record.get("config") or runner_config(platform)
    include_slow = os.environ.get("SEVENZIP_INCLUDE_SLOW_TESTS", "0")
    if include_slow not in ("0", "1"):
        raise ValueError("SEVENZIP_INCLUDE_SLOW_TESTS must be 0 or 1")
    requested_jobs = os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL")
    build_jobs = int(requested_jobs) if requested_jobs is not None else None
    if build_jobs is not None and build_jobs <= 0:
        raise ValueError("CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer")
    analysis_mode = clang_tidy_mode()
    diagnostics = diagnostic_environment()
    payload = command_payload(preset, command)
    use_vcpkg = preset.startswith("vcpkg-")
    qt_prefix = "" if use_vcpkg else config['qt_prefix']
    tool_paths = [*config['path']]
    if qt_prefix:
        tool_paths.insert(0 if platform == 'linux' else len(tool_paths), qt_prefix + '/bin')
    storage = config['workspace']['root'].rsplit('/', 1)[0]
    branch = record.get("branch") or current_branch(ROOT)
    tag = platform + '-x86_64'
    build_environment = {'SEVENZIP_WORKSPACE': storage, 'SEVENZIP_SOURCE_ROOT': root}
    if config.get('tools_root'):
        build_environment['SEVENZIP_SHARED_TOOLS_ROOT'] = config['tools_root']
    if config.get('cache_root'):
        build_environment['SEVENZIP_CACHE_ROOT'] = config['cache_root']
    build_environment.update(diagnostics)
    if use_vcpkg and platform == 'linux':
        build_environment['SEVENZIP_VCPKG_QT_PCH'] = 'OFF'
    runtime_directory = f'{storage}/branches/{branch_component(branch)}/artifacts/{tag}/{preset}/codec-runtime'
    if platform == "linux":
        parallelism = str(build_jobs) if build_jobs is not None else (
            f'$({shlex.quote(config["python"])} -c "import os; print(os.cpu_count() or 1)")')
        storage_environment = ''.join(f'export {key}={shlex.quote(value)}\n'
                                      for key, value in build_environment.items())
        analysis_environment = (f"export SEVENZIP_ENABLE_CLANG_TIDY={analysis_mode}\n"
                                if analysis_mode is not None else "")
        script = f"""set -euo pipefail
cd {shlex.quote(root)}
mkdir -p runner-tmp
export TMPDIR="$PWD/runner-tmp" TMP="$PWD/runner-tmp" TEMP="$PWD/runner-tmp"
export QT_QPA_PLATFORM="${{QT_QPA_PLATFORM:-offscreen}}"
export CMAKE_BUILD_PARALLEL_LEVEL={parallelism}
{'export VCPKG_MAX_CONCURRENCY="$CMAKE_BUILD_PARALLEL_LEVEL"' if use_vcpkg else ''}
{storage_environment}{analysis_environment}workspace_environment="$({shlex.quote(config['python'])} -B scripts/lib/build_paths.py env --root "$PWD" --preset {shlex.quote(preset)})"
eval "$workspace_environment"
export SEVENZIP_INCLUDE_SLOW_TESTS={include_slow}
export CMAKE_PREFIX_PATH={shlex.quote(qt_prefix)}
export PATH={shlex.quote(':'.join(tool_paths))}:"$PATH"
export LIBGL_ALWAYS_SOFTWARE=1
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8
export SEVENZIP_RUN_TOKEN={token}
export SEVENZIP_RUNNER_PYTHON={shlex.quote(config['python'])}
export CCACHE_MAXSIZE=2G
set +e
(
set -e
"""
        if prepare_runtime:
            script += (shlex.quote(config['python']) +
                       ' scripts/prepare-runtime.py --preset ' +
                       shlex.quote(preset) + '\n' +
                       'export SEVENZIP_RUNTIME_PREPARED=1\n')
        script += ('export PATH=' + shlex.quote(runtime_directory) + ':"$PATH"\n'
                   'export SEVENZIP_CODEC_MODULE_DIR=' + shlex.quote(runtime_directory) + '\n' + payload)
        script += (f") > check.log 2>&1\ncode=$?\nprintf '%s\\n' '{token}' > .runner-complete\n"
                   'if [ "$code" -ne 0 ]; then tail -n 80 check.log; fi\nexit "$code"\n')
    else:
        parallelism = str(build_jobs) if build_jobs is not None else '%NUMBER_OF_PROCESSORS%'
        commands = ['@echo off', 'chcp 65001 >nul', *[f'set "{key}={root}/runner-tmp"' for key in ("TEMP", "TMP", "TMPDIR")],
                    f'call "{config["vcvars"]}"', 'if errorlevel 1 exit /b %errorlevel%']
        commands += ['call workspace-env.cmd', 'if errorlevel 1 exit /b %errorlevel%']
        # vcvars supplies its own VCPKG_ROOT; explicit project settings win
        # after compiler bootstrap and before native preparation or commands.
        commands += [f'set "{key}={value}"' for key, value in build_environment.items()]
        if use_vcpkg:
            # Keep vcpkg's clean PATH while letting CMake reuse configured host tools.
            commands += [f'set "CMAKE_PROGRAM_PATH={";".join(tool_paths)}"',
                         'set "VCPKG_KEEP_ENV_VARS=CMAKE_PROGRAM_PATH;%VCPKG_KEEP_ENV_VARS%"']
        if analysis_mode is not None:
            commands.append(f'set "SEVENZIP_ENABLE_CLANG_TIDY={analysis_mode}"')
        commands += [f'set "PATH={runtime_directory};{";".join(tool_paths)};%PATH%"',
                    'set "CCACHE_MAXSIZE=2G"',
                    f'set "CMAKE_PREFIX_PATH={qt_prefix}"', 'set "CC=cl.exe"', 'set "CXX=cl.exe"',
                    f'set "SEVENZIP_WINDOWS_RUNNER_HOST={config["computer_name"]}"',
                    f'set "SEVENZIP_RUNNER_PYTHON={config["python"]}"',
                    f'set "CMAKE_BUILD_PARALLEL_LEVEL={parallelism}"',
                    f'set "SEVENZIP_INCLUDE_SLOW_TESTS={include_slow}"',
                    f'set "SEVENZIP_CODEC_MODULE_DIR={runtime_directory}"',
                    'set "PYTHONUTF8=1"', 'set "PYTHONIOENCODING=utf-8"',
                    f'set "SEVENZIP_RUN_TOKEN={token}"',
                    f'cd /d "{root}"']
        if use_vcpkg:
            commands.append('set "VCPKG_MAX_CONCURRENCY=%CMAKE_BUILD_PARALLEL_LEVEL%"')
        if prepare_runtime:
            commands += [f'"{config["python"]}" scripts/prepare-runtime.py --preset {preset}',
                         'if errorlevel 1 exit /b %errorlevel%',
                         'set "SEVENZIP_RUNTIME_PREPARED=1"']
        commands += ['bash runner-command.sh', 'exit /b %errorlevel%']
        batch = base64.b64encode(("\r\n".join(commands) + "\r\n").encode()).decode()
        encoded_payload = base64.b64encode(payload.encode()).decode()
        launch = interactive_command(root, elevated, token) if interactive else (
            "& cmd.exe /d /c 'check.cmd > check.log 2>&1'\n$code=$LASTEXITCODE\n")
        native_environment = '\n'.join(f"$env:{key}='{value}'" for key, value in build_environment.items())
        script = f"""Set-Location '{root}'
{native_environment}
$env:PYTHONDONTWRITEBYTECODE='1'
$env:SEVENZIP_RUNNER_PYTHON='{config['python']}'
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned -Force
. ./scripts/lib/workspace.ps1
Initialize-SevenzipWorkspace '{root}'
$workspace = & '{config['python']}' -B scripts/lib/build_paths.py env --root '{root}' --preset '{preset}' --format json | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) {{ throw 'Build workspace verification failed' }}
$lines=@($workspace.PSObject.Properties | ForEach-Object {{ 'set "'+$_.Name+'='+[string]$_.Value+'"' }})
[IO.File]::WriteAllLines('{root}/workspace-env.cmd', $lines, (New-Object Text.UTF8Encoding($false)))
New-Item -ItemType Directory '{root}/runner-tmp' -Force | Out-Null
$env:TEMP=$env:TMP=$env:TMPDIR='{root}/runner-tmp'
[IO.File]::WriteAllBytes('{root}/check.cmd', [Convert]::FromBase64String('{batch}'))
[IO.File]::WriteAllBytes('{root}/runner-command.sh', [Convert]::FromBase64String('{encoded_payload}'))
{launch}
[IO.File]::WriteAllText('{root}/.runner-complete', '{token}')
if ($code -ne 0) {{ Get-Content check.log -Tail 80 }}
if ($code -ne 0) {{ exit $code }}
"""
    return remote(record, script, **({"stdout": output, "stderr": subprocess.STDOUT} if output else {}))


def script_command(arguments, platforms):
    path = Path(arguments[0])
    if path.is_absolute() or ".." in path.parts or not (ROOT / path).is_file():
        raise ValueError("Script must be an existing project-relative path")
    from lib.hyperv_source import source_name
    if not source_name(path.as_posix()):
        raise ValueError("Script is outside synchronized source directories")
    ignored = subprocess.run(["git", "check-ignore", "-q", "--", str(path)], cwd=ROOT)
    if ignored.returncode == 0:
        raise ValueError("Ignored scripts are not synchronized")
    interpreters = {".sh": ["bash"], ".py": ["python"],
                    ".ps1": ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "RemoteSigned", "-File"]}
    if path.suffix not in interpreters or (path.suffix == ".ps1" and platforms != ["windows"]):
        raise ValueError("Use .sh/.py, or .ps1 for Windows only")
    return [*interpreters[path.suffix], path.as_posix(), *arguments[1:]]


def run_platform(platform, local, source, preset, command, prepare_runtime,
                 interactive=False, elevated=False, workspace=None, branch=None,
                 sync_checksum=False):
    directory = local / platform
    directory.mkdir()
    workspace = workspace or load_workspaces()[platform]
    branch = branch if branch is not None else current_branch(ROOT)
    root = worktree_root(workspace, branch)
    token = local.name
    result = {"platform": platform, "status": "running", "transport": "cp2",
              "branch": branch, "directory": root, "interactive": interactive and platform == "windows",
              "elevated": elevated and platform == "windows", "seconds": {}}
    acquired = launched = interrupted_sync = False
    print(f"[{platform}] Branch: {branch}; persistent worktree: {root}; "
          f"log: {directory / 'transport.log'}", flush=True)
    with (directory / "transport.log").open("w") as output:
        transport = {"stdout": output, "stderr": subprocess.STDOUT}
        try:
            started = time.monotonic()
            record = discover(platform)
            record["branch"] = branch
            result.update(record)
            result["seconds"]["ready"] = round(time.monotonic() - started, 3)
            started = time.monotonic()
            lock_command(record, root, token, "acquire", **transport)
            acquired = True
            result["seconds"]["guard_and_lock"] = round(time.monotonic() - started, 3)
            started = time.monotonic()
            hyperv_cp2_sync.synchronize(record, platform, source, root, token, directory,
                                       remote, powershell, output, sync_checksum)
            result["seconds"]["sync"] = round(time.monotonic() - started, 3)
            resolved = list(command) if command else None
            if resolved and resolved[0] == "python":
                resolved[0] = record["config"]["python"]
            launched = True
            started = time.monotonic()
            try:
                execute_check(record, platform, preset, root, token, resolved, prepare_runtime,
                              output, result["interactive"], result["elevated"])
            finally:
                result["seconds"]["execute"] = round(time.monotonic() - started, 3)
            result.update(status="passed", exit_code=0)
        except subprocess.CalledProcessError as error:
            result.update(status="failed", exit_code=error.returncode, error=str(error))
            if error.stderr:
                result["error"] += ": " + error.stderr.strip()
                output.write(error.stderr)
        except KeyboardInterrupt:
            interrupted_sync = not launched
            result.update(status="interrupted", exit_code=130)
            raise
        except Exception as error:
            result.update(status="error", exit_code=1, error=str(error))
        finally:
            if acquired:
                try:
                    if launched:
                        result.update(collect_logs(record, root, directory, command, token, remote, output, branch))
                except (subprocess.CalledProcessError, OSError, ValueError) as error:
                    result["log_error"] = str(error)
                try:
                    if interrupted_sync:
                        raise ValueError("Sync interrupted; lock retained until its remote sync process is checked")
                    # Hold the lock through log collection, so a new invocation
                    # cannot replace the evidence. Unknown children keep it.
                    lock_command(record, root, token, "release-completed" if launched else "release", **transport)
                except Exception as error:
                    result["lock_retained"] = str(error)
                    if result["status"] == "passed":
                        result.update(status="error", exit_code=1)
            (directory / "run.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"[{platform}] {result['status']}; timing: {result['seconds']}; logs: {directory}", flush=True)
    if result["status"] != "passed" or result.get("lock_retained"):
        log = directory / "check.log"
        if not log.exists():
            log = directory / "transport.log"
        for line in log.read_text(errors="replace").splitlines()[-20:]:
            print(f"[{platform}] {line}", flush=True)
    return result


def run_platforms(platforms, preset, command=None, prepare_runtime=False,
                  interactive=False, elevated=False, sync_checksum=False):
    clang_tidy_mode()
    diagnostic_environment()
    os.environ.update(workspace_environment(ROOT))
    if elevated and (not interactive or "windows" not in platforms):
        raise ValueError("Elevated fixtures require an interactive Windows guest")
    workspaces = load_workspaces()
    if set(platforms) - set(workspaces):
        raise ValueError("A selected runner has no local configuration")
    branch = current_branch(ROOT)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    local = STATE / (stamp + "-" + uuid.uuid4().hex[:8])
    local.mkdir(parents=True)
    summary = {"run_id": local.name, "command": command, "preset": preset, "prepare_runtime": prepare_runtime,
               "transport": "cp2", "interactive": interactive, "elevated": elevated, "branch": branch,
               "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "status": "running", "platforms": []}
    report = local / "run.json"
    try:
        with tempfile.TemporaryDirectory(prefix="source-", dir=directory(ROOT, "tmp")) as temporary:
            started = time.monotonic()
            source = Path(temporary)
            stage_tree(ROOT, source, source_times=True)
            summary["source_seconds"] = round(time.monotonic() - started, 3)
            report.write_text(json.dumps(summary, indent=2) + "\n")
            with ThreadPoolExecutor(max_workers=len(platforms)) as executor:
                futures = [executor.submit(run_platform, platform, local, source,
                                           preset, command, prepare_runtime, interactive, elevated,
                                           workspaces[platform], branch, sync_checksum) for platform in platforms]
                summary["platforms"] = [future.result() for future in futures]
            summary["status"] = "passed" if all(p["status"] == "passed" for p in summary["platforms"]) else "failed"
    except BaseException as error:
        summary.update(status="interrupted" if isinstance(error, KeyboardInterrupt) else "error", error=str(error))
        raise
    finally:
        report.write_text(json.dumps(summary, indent=2) + "\n")
        print(f"Report: {report}", flush=True)
    return 0 if summary["status"] == "passed" else 1


def probe_platform(platform):
    record = discover(platform)
    config = record["config"]
    path = [config["qt_prefix"] + "/bin", *config["path"]]
    if platform == "linux":
        script = (f'export PATH={shlex.quote(":".join(path))}:"$PATH"\n'
                  "set -euo pipefail\nuname -a\ncmake --version\nc++ --version\n"
                  "ccache --version\nqmake -query QT_VERSION\nclang-format --version\n")
    else:
        script = f"$env:PATH='{';'.join(path)};'+$env:PATH; "
        script += "Get-CimInstance Win32_OperatingSystem | Select Caption,Version,OSArchitecture | ConvertTo-Json; "
        for tool in ["cmake --version", "ccache --version", "qmake -query QT_VERSION", "clang-format --version"]:
            script += f"& {tool}; if ($LASTEXITCODE -ne 0) {{ exit $LASTEXITCODE }}; "
    return remote(record, script, capture_output=True)


def probe_platforms(platforms):
    failed = False
    with ThreadPoolExecutor(max_workers=len(platforms)) as executor:
        futures = [(p, executor.submit(probe_platform, p)) for p in platforms]
        for platform, future in futures:
            try:
                result = future.result()
                print(f"[{platform}] passed\n{result.stdout}", end="", flush=True)
                if result.stderr:
                    print(result.stderr, end="", file=sys.stderr)
            except Exception as error:
                failed = True
                print(f"[{platform}] failed: {error}", file=sys.stderr, flush=True)
                if isinstance(error, subprocess.CalledProcessError):
                    for output in (error.stdout, error.stderr):
                        if output:
                            print(output, end="", file=sys.stderr)
    return 1 if failed else 0


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    for action in ("probe", "check", "run", "status", "recover"):
        subparser = actions.add_parser(action)
        subparser.add_argument("platform", choices=["linux", "windows", "all"])
        if action in ("status", "recover"):
            subparser.add_argument("--branch", help="Branch to inspect (default: current Git branch)")
        if action in ("check", "run"):
            subparser.add_argument("--interactive", action="store_true")
            subparser.add_argument("--elevated", action="store_true")
            subparser.add_argument("--sync-checksum", action="store_true",
                                   help="Compare contents instead of size/mtime with cp2")
            subparser.add_argument("--prepare-runtime", action="store_true",
                                   help="Prepare or refresh the native engine/SFX before the command")
        if action == "check":
            subparser.add_argument("--preset", choices=["dev", "release"], default="dev")
        if action == "run":
            subparser.add_argument("--preset", choices=["dev", "release", "modules-dev",
                                   "modules-linux-dev", "modules-gcc-dev", "vcpkg-dev",
                                   "vcpkg-release"], default="dev",
                                   help="Build preset used for native runtime toolchain selection")
            subparser.epilog = "Use -- <project-script> [arguments] after runner options; new nonignored scripts are included."
    separator = argv.index("--") if "--" in argv else len(argv)
    args = parser.parse_args(argv[:separator])
    args.platforms = ["linux", "windows"] if args.platform == "all" else [args.platform]
    if getattr(args, "interactive", False) and args.platform == "linux":
        parser.error("--interactive requires Windows")
    if getattr(args, "elevated", False) and not getattr(args, "interactive", False):
        parser.error("--elevated requires --interactive")
    args.command = None
    if args.action == "run":
        if separator == len(argv) or not argv[separator + 1:]:
            parser.error("run requires -- <project-script> [arguments]")
        try:
            args.command = script_command(argv[separator + 1:], args.platforms)
        except ValueError as error:
            parser.error(str(error))
    elif separator != len(argv):
        parser.error("-- <project-script> is only accepted by run")
    return args


def main():
    args = parse_arguments(sys.argv[1:])
    if set(args.platforms) - set(load_config()):
        raise ValueError("A selected runner has no local configuration")
    if args.action == "probe":
        os.environ.update(workspace_environment(ROOT))
        return probe_platforms(args.platforms)
    if args.action in ("status", "recover"):
        os.environ.update(workspace_environment(ROOT))
        branch = args.branch or current_branch(ROOT)
        for platform in args.platforms:
            record = discover(platform)
            workspace = load_workspaces()[platform]
            root = worktree_root(workspace, branch)
            print(f"[{platform}] Worktree: {root}", flush=True)
            lock_command(record, root, "", args.action)
        return 0
    return run_platforms(args.platforms, getattr(args, "preset", "dev"),
                         args.command, args.prepare_runtime, args.interactive, args.elevated,
                         args.sync_checksum)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Runner failed: {error}; see {STATE}", file=sys.stderr)
        raise SystemExit(error.returncode if isinstance(error, subprocess.CalledProcessError) else 1)
