"""Shared cp2 exclusions and collection of guest diagnostics."""

import base64
import json
import shlex
import zlib
from .build_paths import branch_component

PROTECTED = ("/.git/", "/build/", "/ui-reports/", "/runner-tmp/", "/upstream/", "/check.log", "/check.cmd",
             "/runner-command.sh", "/interactive-exit.txt", "/interactive-session.txt",
             "/.runner-owner.json", "/.runner-complete", "/workspace-env.cmd")


def download_reports(record, paths, directory, remote, output):
    # Text-safe framing also survives Windows PowerShell's native stdout handling.
    code = ("import base64,json,zlib\nfrom pathlib import Path\n"
            f"paths={paths!r}\nreports={{}}\n"
            "for name,path in paths.items():\n"
            "    try:\n"
            "        data=base64.b64encode(zlib.compress(Path(path).read_bytes())).decode('ascii')\n"
            "        reports[name]={'data':data}\n"
            "    except OSError as error:\n"
            "        reports[name]={'error':str(error)}\n"
            "print(json.dumps(reports))\n")
    encoded = base64.b64encode(code.encode()).decode()
    statement = f"import base64;exec(base64.b64decode('{encoded}'))"
    python = record["config"]["python"]
    if record["platform"] == "windows":
        script = f"& '{python}' -c \"{statement}\"\nif ($LASTEXITCODE) {{ exit $LASTEXITCODE }}\n"
    else:
        script = shlex.join([python, "-c", statement]) + "\n"
    response = remote(record, script, capture_output=True)
    if output and response.stderr:
        output.write(response.stderr)
    reports = json.loads(response.stdout)
    if not isinstance(reports, dict):
        raise ValueError("Invalid guest diagnostics response")
    errors = {}
    for name in paths:
        path = directory / name
        try:
            report = reports[name]
            if not isinstance(report, dict):
                raise ValueError(f"Invalid guest report: {name}")
            data = zlib.decompress(base64.b64decode(report["data"], validate=True)) if "data" in report else None
        except (KeyError, TypeError, zlib.error) as error:
            raise ValueError(f"Invalid guest report: {name}") from error
        if data is None:
            path.unlink(missing_ok=True)
            errors[name] = report.get("error", "Missing report data")
        else:
            path.write_bytes(data)
    return errors


def collect_logs(record, root, directory, command, token, remote, output, branch):
    paths = {"check.log": f"{root}/check.log"}
    verification_requested = command and len(command) >= 2 and command[1] == "scripts/verify.py"
    if verification_requested:
        preset = (command + [""])[command.index("--preset") + 1] if "--preset" in command else "dev"
        if preset not in ("dev", "release", "modules-dev", "modules-linux-dev", "modules-gcc-dev",
                           "vcpkg-dev", "vcpkg-release"):
            raise ValueError("Invalid verification preset")
        name = "verify-plan.json" if "--plan" in command else "verify-result.json"
        tag = record["platform"] + "-x86_64"
        paths[name] = f"{root}/../../branches/{branch_component(branch)}/logs/{tag}/{preset}/{name}"
    errors = download_reports(record, paths, directory, remote, output)
    if "check.log" in errors:
        raise OSError(f"Guest command log is unavailable: {errors['check.log']}")
    if not verification_requested:
        return {}
    if name in errors:
        return {"verification_error": f"Guest verification report is unavailable: {errors[name]}"}
    verification = json.loads((directory / name).read_text())
    if verification.get("run_token") == token:
        return {"verification": verification}
    (directory / name).unlink()
    return {"verification_error": "Report belongs to another invocation"}
