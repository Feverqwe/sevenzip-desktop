"""Prepare the same pinned cp2 executable locally and on configured guests."""

import base64
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

from . import cp2_install
from .build_paths import host_tools
from .hyperv_transport import PROTECTED

ROOT = Path(__file__).resolve().parents[2]


def pin():
    data = json.loads((ROOT / "tools/cp2/cp2.lock.json").read_text())
    data["source_edits"] = []
    for edit in data.get("edits", []):
        content = (ROOT / edit["path"]).read_bytes()
        if hashlib.sha256(content).hexdigest() != edit["sha256"]:
            raise ValueError("cp2 adaptation checksum mismatch")
        data["source_edits"].append(json.loads(content))
    return data


def installer_source():
    return (ROOT / "scripts/lib/cp2_install.py").read_text()


def local(prebuilt=None):
    return cp2_install.install(host_tools(ROOT), pin(), prebuilt)


def ensure(record, remote, output=None, prebuilt=None):
    config = record["config"]
    tools = config["workspace"]["root"].rsplit("/", 1)[0] + "/tools/" + record["platform"] + "-x86_64"
    filters = "\n".join((*PROTECTED, "__pycache__/", "*.pyc", ".DS_Store")) + "\n"
    storage_wrapper = config.get("build_wrapper", [])
    wrapper_key = hashlib.sha256(filters.encode() + Path(__file__).read_bytes() +
                                 json.dumps(storage_wrapper).encode()).hexdigest()[:16]
    code = installer_source()
    code += "\nresult=install(" + repr(tools) + ", " + repr(pin())
    code += ", prebuilt=" + repr(prebuilt) + ", vcvars=" + repr(config.get("vcvars")) + ")\n"
    code += "filters=" + repr(filters) + "\n"
    code += "wrapper_key=" + repr(wrapper_key) + "\n"
    code += "storage_wrapper=" + repr(storage_wrapper) + "\n"
    code += '''
import shlex
binary=Path(result['path'])
folder=binary.parent/'receiver'/wrapper_key
folder.mkdir(parents=True,exist_ok=True)
patterns=folder/'excludes.txt'
wrapper=folder/('cp2-runner.cmd' if os.name=='nt' else 'cp2-runner')
if os.name=='nt':
    body='@echo off\\n"'+str(binary)+'" --exclude-from "'+str(patterns)+'" %*\\nexit /b %errorlevel%\\n'
else:
    body='#!/bin/sh\\nexec '+shlex.join([*storage_wrapper,str(binary)])+' --exclude-from '+shlex.quote(str(patterns))+' "$@"\\n'
with install_lock(folder/'prepare.lock'):
    if not patterns.exists() or patterns.read_text()!=filters:patterns.write_text(filters)
    if not wrapper.exists() or wrapper.read_text()!=body:wrapper.write_text(body)
    wrapper.chmod(0o755)
result.update(server_path=str(wrapper),time_ns=time.time_ns())
print('CP2_INSTALL='+json.dumps(result))
'''
    encoded = base64.b64encode(code.encode()).decode()
    statement = "import base64;exec(base64.b64decode(" + repr(encoded) + "))"
    if record["platform"] == "windows":
        # The pinned adaptations can exceed CreateProcess's argument limit.
        # The outer SSH transport already frames this script; pipe its ASCII
        # payload to Python instead of putting it in the child's command line.
        script = (f"'{encoded}' | & '{config['python']}' -c "
                  "\"import base64,sys;exec(base64.b64decode(sys.stdin.buffer.read()))\"\n"
                  "if($LASTEXITCODE){exit $LASTEXITCODE}\n")
    else:
        script = shlex.join([config["python"], "-c", statement]) + "\n"
    try:
        result = remote(record, script, capture_output=True)
    except subprocess.CalledProcessError as error:
        diagnostic = (error.stdout or "") + (error.stderr or "")
        if output:
            output.write(diagnostic)
            output.flush()
        else:
            sys.stderr.write(diagnostic)
        raise
    if output:
        for line in result.stdout.splitlines(keepends=True):
            if not line.startswith("CP2_INSTALL="):
                output.write(line)
        output.write(result.stderr)
        output.flush()
    for line in reversed(result.stdout.splitlines()):
        if line.startswith("CP2_INSTALL="):
            result = json.loads(line.split("=", 1)[1])
            result["path"] = result["path"].replace("\\", "/")
            result["server_path"] = result["server_path"].replace("\\", "/")
            return result
    raise ValueError("cp2 installer did not report its executable")
