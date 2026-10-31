"""Exercise focused selection from an unbuilt CMake project through actual wrappers."""

import json
import os
from pathlib import Path
import shutil
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from lib.build_paths import build_directory, directory


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        # Each miniature project owns its outputs; inheriting the outer
        # runner's destination would mix fixtures and bypass their cleanup.
        os.environ.pop("SEVENZIP_OUTPUT_ROOT", None)

    @unittest.skipIf(os.name == "nt", "POSIX output migration links")
    def test_migrated_outputs_reuse_build_and_preserve_compiler_cache(self):
        with tempfile.TemporaryDirectory(prefix="migrated outputs ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            environment = dict(os.environ, CMAKE_PREFIX_PATH=str(root))
            command = ["bash", "scripts/build.sh", "dev", "subject"]
            first = subprocess.run(command, cwd=root, env=environment, text=True, capture_output=True)
            self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
            linked = (root / "subject-links").read_bytes()
            cache = directory(root, "cache") / "retained"
            cache.write_bytes(b"existing compiler data")
            workspace = directory(root, "tools").parents[1]
            shared = root / "shared project"
            shared.mkdir()
            for name in ("branches", "b"):
                shutil.move(workspace / name, shared / name)
                (workspace / name).symlink_to(shared / name, target_is_directory=True)
            environment["SMBSTORAGE_BUILD_ROOT"] = str(shared)
            second = subprocess.run(command, cwd=root, env=environment, text=True, capture_output=True)
            self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
            self.assertEqual((root / "subject-links").read_bytes(), linked)
            self.assertEqual(cache.read_bytes(), b"existing compiler data")
            executable = directory(root, "artifacts") / "dev/subject"
            self.assertEqual(subprocess.run([str(executable)]).returncode, 0)

    def fixture(self, root):
        (root / "scripts/config").mkdir(parents=True)
        (root / "cmake").mkdir()
        (root / "scripts/lib").mkdir(parents=True, exist_ok=True)
        for name in ("build.sh", "ctest.sh", "verify.py", "config/verify-profiles.json",
                     "lib/runner_timing.py", "lib/toolchain.py", "lib/toolchain.sh", "lib/runtime_layout.py",
                     "lib/project_paths.py", "lib/build_paths.py", "lib/cp2_install.py"):
            shutil.copy2(ROOT / "scripts" / name, root / "scripts" / name)
        (root / "tools/build").mkdir(parents=True)
        shutil.copy2(ROOT / "tools/build/project.json", root / "tools/build/project.json")
        shutil.copy2(ROOT / "cmake/DeveloperWorkflow.cmake", root / "cmake/DeveloperWorkflow.cmake")
        shutil.copy2(ROOT / "cmake/BuildDiagnostics.cmake", root / "cmake/BuildDiagnostics.cmake")
        shutil.copy2(ROOT / "cmake/ReadCTestCommands.cmake", root / "cmake/ReadCTestCommands.cmake")
        shutil.copy2(ROOT / "CMakePresets.json", root / "CMakePresets.json")
        (root / "main.cpp").write_text("int main() { return 0; }\n")
        (root / "link.py").write_text(
            "import json,subprocess,sys\nfrom pathlib import Path\n"
            "with Path(sys.argv[1]).open('a') as stream: stream.write(json.dumps(sys.argv[2:])+'\\n')\n"
            "raise SystemExit(subprocess.run(sys.argv[2:]).returncode)\n")
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(workflow LANGUAGES CXX)
include(cmake/BuildDiagnostics.cmake)
file(APPEND "${CMAKE_BINARY_DIR}/configure-count" "configured\n")
enable_testing()
add_executable(subject main.cpp)
add_custom_target(sevenzip_desktop DEPENDS subject)
set_property(TARGET subject PROPERTY CXX_LINKER_LAUNCHER "''' + Path(sys.executable).as_posix() + ''';${CMAKE_SOURCE_DIR}/link.py;${CMAKE_SOURCE_DIR}/subject-links")
add_executable(producer main.cpp)
add_executable(unrelated main.cpp)
add_test(NAME scenario COMMAND subject)
add_test(NAME setup COMMAND producer)
add_test(NAME long COMMAND unrelated)
set_tests_properties(setup PROPERTIES FIXTURES_SETUP input)
set_tests_properties(scenario PROPERTIES FIXTURES_REQUIRED input)
set_tests_properties(long PROPERTIES LABELS slow)
include(cmake/DeveloperWorkflow.cmake)
''')

    def test_default_debug_build_excludes_unrelated_targets(self):
        with tempfile.TemporaryDirectory(prefix="focused default ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            environment = dict(os.environ, CMAKE_PREFIX_PATH=str(root))
            built = subprocess.run(["bash", "scripts/build.sh", "dev"], cwd=root,
                                   env=environment,
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            output = directory(root, "artifacts") / "dev"
            suffix = ".exe" if os.name == "nt" else ""
            self.assertTrue((output / ("subject" + suffix)).is_file())
            self.assertFalse((output / ("producer" + suffix)).exists())
            self.assertFalse((output / ("unrelated" + suffix)).exists())

    def test_diagnostic_modes_restore_light_build_and_allow_overrides(self):
        # Catch stale CMake diagnostic settings, missing sanitizer linkage and
        # accidental loss of Debug assertions while switching one build tree.
        with tempfile.TemporaryDirectory(prefix="diagnostic modes ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "main.cpp").write_text('''#include <cassert>
#ifdef NDEBUG
#error Diagnostic modes must retain Debug assertions
#endif
int main(int argc, char **) {
    assert(argc > 0);
    if (argc > 1) {
        volatile int index = argc + 10;
        auto *values = new int[2];
        values[index] = 7;
        delete[] values;
    }
    return 0;
}
''')
            environment = dict(os.environ, CMAKE_PREFIX_PATH=str(root))
            for name in ("SEVENZIP_BUILD_MODE", "SEVENZIP_DEBUG_SYMBOLS",
                         "SEVENZIP_ENABLE_SANITIZERS", "SEVENZIP_ENABLE_CLANG_TIDY",
                         "SEVENZIP_COMPACT_TEST_BINARIES"):
                environment.pop(name, None)
            suffix = ".exe" if os.name == "nt" else ""
            for mode, overrides, symbols in (("lite", {}, False), ("debug", {}, True),
                    ("sanitize", {"SEVENZIP_DEBUG_SYMBOLS": "OFF"}, False), ("lite", {}, False)):
                with self.subTest(mode=mode):
                    selected = dict(environment, SEVENZIP_BUILD_MODE=mode, **overrides)
                    built = subprocess.run(["bash", "scripts/build.sh", "dev", "subject"],
                                           cwd=root, env=selected, text=True, capture_output=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    binary = directory(root, "artifacts") / "dev" / ("subject" + suffix)
                    safe = subprocess.run([str(binary)], env=selected, capture_output=True, text=True)
                    self.assertEqual(safe.returncode, 0, safe.stdout + safe.stderr)
                    if sys.platform == "linux":
                        sections = subprocess.check_output(["readelf", "-SW", str(binary)], text=True)
                        self.assertEqual(".debug_info" in sections, symbols)
                    if mode == "sanitize":
                        fault = subprocess.run([str(binary), "overflow"], env=selected,
                                               capture_output=True, text=True)
                        self.assertNotEqual(fault.returncode, 0)
                        self.assertIn("AddressSanitizer", fault.stdout + fault.stderr)

    def test_first_build_includes_fixture_and_preserves_incremental_output(self):
        with tempfile.TemporaryDirectory(prefix="verify Юникод ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            with patch.dict(os.environ, {"SEVENZIP_INCLUDE_SLOW_TESTS": "0"}):
                self.run_verify(root, "^scenario$")
                report = directory(root, "logs") / "dev/verify-result.json"
                result = json.loads(report.read_text())
                self.assertEqual(result["status"], "passed")
                suffix = ".exe" if os.name == "nt" else ""
                binary = directory(root, "artifacts") / "dev" / ("subject" + suffix)
                before = (root / "subject-links").read_bytes()
                self.assertFalse((directory(root, "artifacts") / "dev" / ("unrelated" + suffix)).exists())
                self.run_verify(root, "^scenario$")
                self.assertEqual(before, (root / "subject-links").read_bytes())
                self.assertEqual((build_directory(root) / "configure-count").read_text().splitlines(), ["configured"])
                with (root / "CMakeLists.txt").open("a") as stream:
                    stream.write("add_test(NAME added COMMAND subject)\n")
                self.run_verify(root, "^added$", plan=True)
                planned = json.loads((directory(root, "logs") / "dev/verify-plan.json").read_text())
                self.assertEqual(planned["status"], "planned")
                self.assertEqual(json.loads(report.read_text())["status"], "passed")
                self.assertEqual(len((build_directory(root) / "configure-count").read_text().splitlines()), 2)
                self.run_verify(root, "^does_not_exist$", succeeds=False)
                self.assertEqual(json.loads(report.read_text())["status"], "failed")
                self.run_verify(root, "^long$", succeeds=False)

    def test_runner_identity_change_refreshes_native_test_availability(self):
        with tempfile.TemporaryDirectory(prefix="runner selection ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            with (root / "CMakeLists.txt").open("a") as stream:
                stream.write('''
if(NOT "$ENV{SEVENZIP_WINDOWS_RUNNER_HOST}" STREQUAL "" AND
   "$ENV{SEVENZIP_WINDOWS_RUNNER_HOST}" STREQUAL "$ENV{COMPUTERNAME}")
    add_test(NAME configured_native COMMAND subject)
endif()
''')
            with patch.dict(os.environ, {"SEVENZIP_WINDOWS_RUNNER_HOST": "fixture-A",
                                         "COMPUTERNAME": "fixture-A"}):
                self.run_verify(root, "^configured_native$", plan=True)
                os.environ["SEVENZIP_WINDOWS_RUNNER_HOST"] = "fixture-B"
                # A stale configure would still select a test for the wrong host.
                self.run_verify(root, "^configured_native$", plan=True, succeeds=False)

    def test_branches_keep_their_own_binaries_and_reuse_previous_build(self):
        with tempfile.TemporaryDirectory(prefix="branch storage ") as temporary:
            root = Path(temporary)
            self.fixture(root)

            def git(*arguments):
                subprocess.run(["git", "-C", str(root), *arguments], check=True,
                               capture_output=True)

            (root / ".gitignore").write_text("branches/\ncache/\ntools/\n")
            git("init", "-q")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "commit", "--allow-empty", "-qm", "fixture")
            first_binary = None
            previous_branch = previous_time = None
            for branch, text in (("feature/a", "first"), ("feature-a", "second"),
                                 ("feature/a", "first"), ("feature/a", "first")):
                git("checkout", "-q", "-B", branch)
                source = root / "main.cpp"
                content = '#include <cstdio>\nint main() { std::puts("' + text + '"); }\n'
                if source.read_text() != content:
                    source.write_text(content)
                built = subprocess.run(["bash", "scripts/build.sh", "dev", "subject"], cwd=root,
                                       env=dict(os.environ, CMAKE_PREFIX_PATH=str(root)),
                                       text=True, capture_output=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                binary = directory(root, "artifacts") / "dev" / ("subject.exe" if os.name == "nt" else "subject")
                result = subprocess.check_output([str(binary)], text=True).strip()
                self.assertEqual(result, text)
                if first_binary is None:
                    first_binary = binary
                elif branch == "feature-a":
                    self.assertEqual(subprocess.check_output([str(first_binary)], text=True).strip(), "first")
                if branch == previous_branch:
                    self.assertEqual((root / "subject-links").read_bytes(), previous_time)
                previous_branch, previous_time = branch, (root / "subject-links").read_bytes()
            self.assertEqual(subprocess.check_output([str(first_binary)], text=True).strip(), "first")

    def test_linked_checkout_and_verification_reuse_the_same_objects(self):
        with tempfile.TemporaryDirectory(prefix="linked checkout ") as temporary:
            base = Path(temporary)
            root, alias = base / "source", base / "linked source"
            root.mkdir()
            self.fixture(root)
            if os.name == "nt":
                subprocess.run(["cmd", "/d", "/c", "mklink", "/J", str(alias), str(root)],
                               check=True, capture_output=True)
            else:
                alias.symlink_to(root, target_is_directory=True)
            try:
                result = subprocess.run(
                    ["bash", str(alias / "scripts/build.sh"), "dev", "subject"], cwd=alias,
                    env=dict(os.environ, CMAKE_PREFIX_PATH=str(root)), text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                binary = directory(root, "artifacts") / "dev" / ("subject.exe" if os.name == "nt" else "subject")
                before = (root / "subject-links").read_bytes()
                commands = (build_directory(root) / "compile_commands.json").read_text()
                verified = self.run_verify(root, "^scenario$")
                self.assertEqual((root / "subject-links").read_bytes(), before,
                                 commands + '\nAFTER\n' +
                                 (build_directory(root) / "compile_commands.json").read_text() +
                                 result.stdout + verified.stdout)
            finally:
                if os.name == "nt":
                    alias.rmdir()
                else:
                    alias.unlink()

    def test_unwritable_optional_timing_does_not_fail_verification(self):
        with tempfile.TemporaryDirectory(prefix="optional timing ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "runner-tmp").write_text("An unavailable timing directory")
            with patch.dict(os.environ, {
                    "SEVENZIP_RUNNER_TIMING": str(root / "runner-tmp/timing.jsonl"),
                    "SEVENZIP_RUN_TOKEN": "fixture", "SEVENZIP_RUNNER_PYTHON": sys.executable}):
                self.run_verify(root, "^scenario$")
            result = json.loads((directory(root, "logs") / "dev/verify-result.json").read_text())
            self.assertEqual(result["status"], "passed")

    def test_analysis_mode_rechecks_an_unchanged_incremental_build(self):
        # Enabling analysis must run its diagnostics even when the executable
        # was already built, and disabling it must recover the same build tree.
        with tempfile.TemporaryDirectory(prefix="analysis workflow ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "analyze.py").write_text('raise SystemExit("analysis rejected fixture")\n')
            python = Path(sys.executable).as_posix()
            (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(analysis_workflow LANGUAGES CXX)
option(SEVENZIP_ENABLE_CLANG_TIDY "Analyze the fixture" OFF)
if(SEVENZIP_ENABLE_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "''' + python + ''';${CMAKE_SOURCE_DIR}/analyze.py")
endif()
enable_testing()
add_executable(subject main.cpp)
add_test(NAME scenario COMMAND subject)
include(cmake/DeveloperWorkflow.cmake)
''')
            with patch.dict(os.environ, {"SEVENZIP_ENABLE_CLANG_TIDY": "OFF"}):
                self.run_verify(root, "^scenario$")
                os.environ["SEVENZIP_ENABLE_CLANG_TIDY"] = "ON"
                rejected = self.run_verify(root, "^scenario$", succeeds=False)
                self.assertIn("analysis rejected fixture", rejected.stdout + rejected.stderr)
                os.environ["SEVENZIP_ENABLE_CLANG_TIDY"] = "OFF"
                self.run_verify(root, "^scenario$")

    def test_clang_analysis_imports_a_module_and_rejects_dangling_borrows(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if not compiler:
            self.skipTest("Clang module toolchain unavailable")
        version = subprocess.run([compiler, "--version"], text=True, capture_output=True)
        match = re.search(r"clang version (\d+)", version.stdout)
        if not match or int(match[1]) < 19:
            self.skipTest("Clang 19 or newer required")
        analyzer = (shutil.which("clang-tidy-" + match[1]) or
                    shutil.which("clang-tidy", path=str(Path(compiler).parent)))
        if not analyzer:
            self.skipTest("Matching clang-tidy unavailable")
        with tempfile.TemporaryDirectory(prefix="module analysis ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            shutil.copy2(ROOT / "cmake/StaticAnalysis.cmake", root / "cmake/StaticAnalysis.cmake")
            shutil.copy2(ROOT / ".clang-tidy", root / ".clang-tidy")
            shutil.copy2(ROOT / "scripts/lib/clang_tidy_launcher.py",
                         root / "scripts/lib/clang_tidy_launcher.py")
            (root / "answer.cppm").write_text(
                "export module answer;\nexport int answer() { return 42; }\n")
            (root / "main.cpp").write_text("import answer;\nint main() { return answer() == 42 ? 0 : 1; }\n")
            (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.28)
project(module_analysis LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(SEVENZIP_CLANG_TIDY_EXECUTABLE "''' + Path(analyzer).as_posix() + '''" CACHE FILEPATH "")
include(cmake/StaticAnalysis.cmake)
enable_testing()
add_executable(subject main.cpp)
target_sources(subject PRIVATE FILE_SET CXX_MODULES FILES answer.cppm)
sevenzip_track_analysis_inputs()
add_test(NAME scenario COMMAND subject)
include(cmake/DeveloperWorkflow.cmake)
''')
            with patch.dict(os.environ, {"CC": compiler, "CXX": compiler,
                                         "SEVENZIP_ENABLE_CLANG_TIDY": "ON"}):
                self.run_verify(root, "^scenario$")
                for kind, headers, body in (
                    ("span", '#include <array>\n#include <span>\n',
                     'auto dangling() -> std::span<const int> {\n'
                     '    const std::array values{1, 2, 3};\n'
                     '    return std::span<const int>{values};\n}\n'),
                    ("string_view", '#include <string>\n#include <string_view>\n',
                     'auto dangling() -> std::string_view { return std::string("temporary"); }\n'),
                ):
                    with self.subTest(borrow=kind):
                        (root / "main.cpp").write_text(
                            headers + 'import answer;\n' + body +
                            'int main() { return answer() == 42 ? 0 : 1; }\n')
                        rejected = self.run_verify(root, "^scenario$", succeeds=False)
                        self.assertRegex(rejected.stdout + rejected.stderr,
                                         r'bugprone-dangling-handle|clang-diagnostic-(return-stack-address|lifetime-safety)')
                (root / "main.cpp").write_text(
                    'import answer;\nint main() { return answer() == 42 ? 0 : 1; }\n')
                self.run_verify(root, "^scenario$")

    def test_analysis_launcher_preserves_quoted_module_paths(self):
        # Windows response parsing must also treat line breaks as separators;
        # spaces inside BMI paths must reach the tooling driver unchanged.
        with tempfile.TemporaryDirectory(prefix="module paths Юникод ") as temporary:
            root = Path(temporary)
            (root / "clang.modmap").write_text(
                '-fmodule-file=answer="module with spaces.pcm"\n'
                '-fmodule-output="compiled module.pcm"\n')
            (root / "msvc.modmap").write_text('-reference answer=answer.ifc\n')
            (root / "capture.py").write_text(
                'import json,sys\nfrom pathlib import Path\n'
                'Path("arguments.json").write_text(json.dumps(sys.argv[1:]))\n')
            subprocess.run([sys.executable, str(ROOT / "scripts/lib/clang_tidy_launcher.py"),
                            sys.executable, "capture.py", "--", "@clang.modmap", "@msvc.modmap"],
                           cwd=root, check=True)
            self.assertEqual(json.loads((root / "arguments.json").read_text()),
                             ["--", "-fmodule-file=answer=module with spaces.pcm",
                              "-fmodule-output=compiled module.pcm", "@msvc.modmap"])

    def run_verify(self, root, selection, plan=False, succeeds=True):
        command = [sys.executable, str(root / "scripts/verify.py"), "--platform", "local",
                   "--tests", selection]
        if plan:
            command.append("--plan")
        # This fixture exercises the wrappers without any Qt dependency.
        # Supply its own prefix instead of requiring a machine-local SDK.
        environment = dict(os.environ, CMAKE_PREFIX_PATH=str(root))
        result = subprocess.run(command, cwd=root, env=environment, text=True, capture_output=True)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

if __name__ == "__main__":
    unittest.main()
