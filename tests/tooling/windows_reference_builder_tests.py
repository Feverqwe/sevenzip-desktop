"""Exercise the native-reference builder with disposable source and fake compile output."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from lib.build_paths import directory
from lib.runtime_layout import codec_runtime


@unittest.skipUnless(os.name == "nt", "Windows PowerShell builder")
class ReferenceBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="reference builder Юникод ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = self.root / "project"
        self.upstream = self.root / "upstream"
        self.bin = self.root / "bin"
        for path in (self.project / "scripts", self.project / "tests/support",
                     self.project / "docs", codec_runtime(self.project), self.bin, self.upstream):
            path.mkdir(parents=True, exist_ok=True)
        for name in ("build-windows-reference.ps1", "upstream_baseline.py"):
            shutil.copy2(ROOT / "scripts" / name, self.project / "scripts" / name)
        (self.project / "scripts/lib").mkdir()
        for helper in ("build_paths.py", "project_paths.py", "workspace.ps1", "cp2_install.py"):
            shutil.copy2(ROOT / "scripts/lib" / helper, self.project / "scripts/lib" / helper)
        (self.project / "tools/build").mkdir(parents=True)
        shutil.copy2(ROOT / "tools/build/project.json", self.project / "tools/build/project.json")
        (self.project / "tools/dependencies").mkdir()
        shutil.copy2(ROOT / "tools/dependencies/source_archives.py",
                     self.project / "tools/dependencies/source_archives.py")
        self.adapter = self.project / "tests/support/native_reference_entry.cpp"
        self.adapter.write_bytes(b"private reference wrapper")
        for name in ("7z.dll", "7z.exe"):
            (codec_runtime(self.project) / name).write_bytes(b"synthetic runtime")
        self.git("init", "-q")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "core.autocrlf", "false")
        module = self.upstream / "CPP/7zip/UI/FileManager"
        module.mkdir(parents=True)
        (module / "FM.cpp").write_bytes(b"original reference entry")
        for name, macro, tail in (("RegistryUtils.cpp", "REG_PATH_7Z", ""),
                                 ("ViewSettings.cpp", "REG_PATH_FM", ' TEXT(STRING_PATH_SEPARATOR) TEXT("FM")')):
            (module / name).write_text('#define ' + macro +
                ' TEXT("Software") TEXT(STRING_PATH_SEPARATOR) TEXT("7-Zip")' + tail + '\n')
        (self.upstream / "DOC").mkdir()
        (self.upstream / "DOC/License.txt").write_bytes(b"fixture license")
        self.git("add", ".")
        self.git("commit", "-qm", "original reference")
        revision = self.git("rev-parse", "HEAD")
        (self.project / "docs/upstream-baseline.json").write_text(json.dumps({
            "schema": 1, "revision": revision, "repository": "https://github.com/ip7z/7zip.git",
            "adapted": [], "exact": [], "local": []}))
        # Get-Command locates this stand-in compiler; no native code is executed.
        shutil.copy2(sys.executable, self.bin / "cl.exe")
        compile_script = self.bin / "compile.py"
        compile_script.write_text('from pathlib import Path\n'
            'p = Path("x64/7zFM.exe")\np.parent.mkdir(exist_ok=True)\n'
            'p.write_bytes(Path("FM.cpp").read_bytes())\n')
        (self.bin / "nmake.cmd").write_text('@"%SEVENZIP_RUNNER_PYTHON%" "%SEVENZIP_REFERENCE_COMPILE%"\n')
        self.environment = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
            SEVENZIP_RUNNER_PYTHON=sys.executable, SEVENZIP_WINDOWS_RUNNER_HOST=os.environ["COMPUTERNAME"],
            VSCMD_ARG_TGT_ARCH="x64", SEVENZIP_REFERENCE_COMPILE=str(compile_script))
        self.output = directory(self.project, "artifacts") / "windows-reference"

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.upstream), *args], text=True).strip()

    def run_builder(self, succeeds=True):
        result = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "RemoteSigned",
            "-File", str(self.project / "scripts/build-windows-reference.ps1"),
            "-UpstreamCheckout", str(self.upstream)], cwd=self.project, env=self.environment,
            text=True, encoding="utf-8", errors="replace", capture_output=True)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repeat_reuses_output_and_changed_input_rebuilds_without_discarding_objects(self):
        self.run_builder()
        binary = self.output / "7zFM.exe"
        first = binary.stat().st_mtime_ns
        manifest = json.loads((self.output / "reference.json").read_text(encoding="utf-8-sig"))
        cached = self.output / "source/cached.obj"
        cached.write_bytes(b"retained incremental object")
        (codec_runtime(self.project) / "7z.dll").write_bytes(b"updated runtime module")
        self.run_builder()
        self.assertEqual(binary.stat().st_mtime_ns, first)
        self.assertEqual((self.output / "7z.dll").read_bytes(), b"updated runtime module")
        self.adapter.write_bytes(b"updated private reference wrapper")
        self.run_builder()
        self.assertEqual(binary.read_bytes(), self.adapter.read_bytes())
        self.assertEqual(cached.read_bytes(), b"retained incremental object")
        current = json.loads((self.output / "reference.json").read_text(encoding="utf-8-sig"))
        self.assertEqual(current["registryKey"], manifest["registryKey"])
        self.assertEqual(current["binarySha256"], hashlib.sha256(binary.read_bytes()).hexdigest())
        self.assertEqual((self.upstream / "CPP/7zip/UI/FileManager/FM.cpp").read_bytes(),
                         b"original reference entry")

    def test_unowned_output_is_refused_without_touching_private_file(self):
        self.output.mkdir()
        private = self.output / "private.txt"
        private.write_bytes(b"user-owned")
        self.run_builder(succeeds=False)
        self.assertEqual(private.read_bytes(), b"user-owned")


if __name__ == "__main__":
    unittest.main()
