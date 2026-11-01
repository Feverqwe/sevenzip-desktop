#!/usr/bin/env python3
"""Check the project wrapper's slow-test opt-in using actual CTest selection."""

import os
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class CTestSelectionTests(unittest.TestCase):
    def test_slow_tests_require_opt_in_and_other_exclusions_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix="ctest selection ") as temporary:
            root = Path(temporary)
            (root / "scripts/lib").mkdir(parents=True)
            (root / "runner-tmp").mkdir()
            for name in ("ctest.sh", "lib/runner_timing.py", "lib/build_paths.py",
                         "lib/project_paths.py", "lib/cp2_install.py"):
                shutil.copy2(ROOT / "scripts" / name, root / "scripts" / name)
            (root / "mark.py").write_text(
                "from pathlib import Path\nimport sys\nPath(sys.argv[1]).write_text(str(Path.cwd()))\n")
            commands = []
            for name in ("fast", "slow", "other"):
                commands.append(f'add_test(NAME {name} COMMAND "{Path(sys.executable).as_posix()}" '
                                f'"{(root / "mark.py").as_posix()}" "{(root / name).as_posix()}")')
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.21)\nproject(selection NONE)\nenable_testing()\n" +
                "\n".join(commands) + "\nset_tests_properties(slow PROPERTIES LABELS slow)\n" +
                "set_tests_properties(other PROPERTIES LABELS other)\n" +
                f'set_tests_properties(fast PROPERTIES WORKING_DIRECTORY "{root.as_posix()}/scratch/fast")\n')
            self.run_command(["cmake", "-S", str(root), "-B", str(root / "build"), "-G", "Ninja"])
            scratch = root / "scratch/fast"
            (root / "build/PrepareTestWorkspace.cmake").write_text(
                f'file(MAKE_DIRECTORY [=[{scratch.as_posix()}]=])\n')
            wrapper = ["bash", str(root / "scripts/ctest.sh"), "--test-dir", str(root / "build"),
                       "--output-on-failure"]
            timing = root / "runner-tmp/timing.jsonl"
            timing.write_text("")
            env = dict(os.environ, SEVENZIP_INCLUDE_SLOW_TESTS="0", SEVENZIP_RUN_TOKEN="fixture",
                       SEVENZIP_RUNNER_PYTHON=Path(sys.executable).as_posix(),
                       SEVENZIP_RUNNER_TIMING=timing.as_posix())
            self.run_command([*wrapper, "--show-only=json-v1"], env)
            self.assertEqual(timing.read_text(), "")
            self.run_command([*wrapper, "--label-exclude", "^other$"], env)
            events = [json.loads(line) for line in timing.read_text().splitlines()]
            self.assertEqual([event["event"] for event in events], ["tests_start"])
            self.assertTrue((root / "fast").exists())
            self.assertFalse((root / "slow").exists())
            self.assertFalse((root / "other").exists())
            self.assertEqual(Path((root / "fast").read_text()).resolve(), scratch.resolve())
            # A persistent build tree remains after its disposable working directory
            # disappears. The next invocation must restore it before startup.
            shutil.rmtree(root / "scratch")
            self.run_command([*wrapper, "-R", "^fast$"], env)
            self.assertEqual(Path((root / "fast").read_text()).resolve(), scratch.resolve())
            events = [json.loads(line) for line in timing.read_text().splitlines()]
            self.run_command([*wrapper, "-R", "^slow$"], env, succeeds=False)
            self.assertFalse((root / "slow").exists())
            self.run_command([*wrapper, "-R", "^missing$"], env, succeeds=False)
            # Discovery remains usable even when a selection has no runnable tests.
            self.run_command([*wrapper, "-R", "^slow$", "--show-only=json-v1"], env)
            self.assertEqual([json.loads(line) for line in timing.read_text().splitlines()], events)
            env["SEVENZIP_INCLUDE_SLOW_TESTS"] = "1"
            self.run_command([*wrapper, "-L", "slow"], env)
            self.assertTrue((root / "slow").exists())

    def run_command(self, command, env=None, succeeds=True):
        result = subprocess.run(command, env=env, text=True, capture_output=True)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
