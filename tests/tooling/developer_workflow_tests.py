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
from lib.build_paths import build_directory, directory, host_tag


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        # Each miniature project owns its outputs; inheriting the outer
        # runner's destination would mix fixtures and bypass their cleanup.
        os.environ.pop("SEVENZIP_OUTPUT_ROOT", None)
        os.environ.pop("SEVENZIP_BRANCH_KEY", None)
        os.environ.pop("SEVENZIP_SHARED_TOOLS_ROOT", None)
        os.environ.pop("SEVENZIP_CACHE_ROOT", None)

    def directory_alias(self, alias, target):
        if os.name == "nt":
            result = subprocess.run(["cmd", "/d", "/c", "mklink", "/J", str(alias), str(target)],
                                    text=True, capture_output=True)
            diagnostic = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, diagnostic)
        else:
            alias.symlink_to(target, target_is_directory=True)

    def test_external_tools_and_cache_preserve_incremental_build(self):
        with tempfile.TemporaryDirectory(prefix="shared outputs ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            workspace = root / "runner project"
            os.environ.update(SEVENZIP_WORKSPACE=str(workspace), SEVENZIP_SOURCE_ROOT=str(root))
            os.environ["SEVENZIP_CACHE_ROOT"] = str(root / "persistent cache")
            (root / "scratch-probe.py").write_text(
                "import os,pathlib,subprocess,sys,tempfile\n"
                "tool=pathlib.Path(os.environ['SEVENZIP_TOOLS_ROOT'])/'probe-tool.py'\n"
                "data=subprocess.check_output([sys.executable,str(tool)])\n"
                "with tempfile.TemporaryDirectory() as temporary:\n"
                "    file=pathlib.Path(temporary)/'payload'\n"
                "    file.write_bytes(data)\n"
                "    assert file.read_bytes()==b'fixture data\\n'\n"
                "    pathlib.Path(sys.argv[1]).write_text(str(file.resolve()))\n")
            with (root / "CMakeLists.txt").open("a") as cmake:
                cmake.write('file(APPEND "${CMAKE_SOURCE_DIR}/configure-runs" "configure\\n")\n')
                cmake.write('add_test(NAME scratch COMMAND "' + Path(sys.executable).as_posix() +
                            '" "${CMAKE_SOURCE_DIR}/scratch-probe.py" "${CMAKE_SOURCE_DIR}/scratch-proof")\n')
            environment = dict(os.environ, CMAKE_PREFIX_PATH=str(root),
                               SEVENZIP_SHARED_TOOLS_ROOT=str(root / "shared tools"))
            tools = root / "shared tools" / host_tag()
            tools.mkdir(parents=True)
            (tools / "probe-tool.py").write_text("import sys\nsys.stdout.buffer.write(b'fixture data\\n')\n")
            command = ["bash", "scripts/build.sh", "dev", "subject"]
            first = subprocess.run(command, cwd=root, env=environment, text=True, capture_output=True)
            self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
            linked = (root / "subject-links").read_bytes()
            configured = (root / "configure-runs").read_bytes()
            cache = directory(root, "cache") / "retained"
            cache.write_bytes(b"existing compiler data")
            second = subprocess.run(command, cwd=root, env=environment, text=True, capture_output=True)
            self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
            self.assertEqual((root / "configure-runs").read_bytes(), configured)
            self.assertEqual((root / "subject-links").read_bytes(), linked)
            self.assertEqual(cache.read_bytes(), b"existing compiler data")
            executable = directory(root, "artifacts") / "dev" / ("subject.exe" if os.name == "nt" else "subject")
            self.assertEqual(subprocess.run([str(executable)]).returncode, 0)
            checked = subprocess.run(["bash", "scripts/ctest.sh", "--test-dir",
                                      str(build_directory(root, "dev")), "-R", "^scratch$",
                                      "--output-on-failure"], cwd=root, env=environment,
                                     text=True, capture_output=True)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            self.assertTrue(Path((root / "scratch-proof").read_text()).is_relative_to(workspace.resolve()))

    def test_configuration_reuse_refreshes_settings_tools_and_edited_cache(self):
        with tempfile.TemporaryDirectory(prefix="configure reuse ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            with (root / "CMakeLists.txt").open("a") as cmake:
                cmake.write('''
file(APPEND "${CMAKE_SOURCE_DIR}/configure-runs" "configure\\n")
if("$ENV{SEVENZIP_WINDOWS_RUNNER_HOST}" STREQUAL "reject")
    message(FATAL_ERROR "Rejected fixture configuration")
endif()
file(WRITE "${CMAKE_SOURCE_DIR}/configured-host" "$ENV{SEVENZIP_WINDOWS_RUNNER_HOST}")
file(WRITE "${CMAKE_SOURCE_DIR}/configured-tool" "$ENV{FIXTURE_TOOL_MARKER}")
file(WRITE "${CMAKE_SOURCE_DIR}/configured-cache" "${FIXTURE_EDIT}")
''')
            environment = dict(os.environ, CMAKE_PREFIX_PATH=str(root),
                               SEVENZIP_WINDOWS_RUNNER_HOST="first")

            def configure(succeeds=True):
                result = subprocess.run(["bash", "scripts/build.sh", "dev", "--configure-only"],
                                        cwd=root, env=environment, text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
                return result

            configure()
            environment["SEVENZIP_WINDOWS_RUNNER_HOST"] = "second"
            configure()
            self.assertEqual((root / "configured-host").read_text(), "second")

            cache = build_directory(root, "dev") / "CMakeCache.txt"
            with cache.open("a") as stream:
                stream.write("\nFIXTURE_EDIT:STRING=external change\n")
            configure()
            self.assertEqual((root / "configured-cache").read_text(), "external change")

            tool = root / "alternate tools/cmake"
            tool.parent.mkdir()
            real_cmake = Path(shutil.which("cmake")).as_posix()
            tool.write_text('#!/bin/sh\nexport FIXTURE_TOOL_MARKER=alternate\nexec "' +
                            real_cmake + '" "$@"\n')
            tool.chmod(0o755)
            environment["PATH"] = str(tool.parent) + os.pathsep + environment["PATH"]
            configure()
            self.assertEqual((root / "configured-tool").read_text(), "alternate")

            environment["SEVENZIP_WINDOWS_RUNNER_HOST"] = "reject"
            failed = configure(succeeds=False)
            self.assertIn("Rejected fixture configuration", failed.stdout + failed.stderr)
            configure(succeeds=False)
            environment["SEVENZIP_WINDOWS_RUNNER_HOST"] = "second"
            configure()
            configured = (root / "configure-runs").read_bytes()
            configure()
            self.assertEqual((root / "configure-runs").read_bytes(), configured)

    def test_configure_failure_preserves_launcher_diagnostic(self):
        with tempfile.TemporaryDirectory(prefix="failed configure ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "bin").mkdir()
            cmake = root / "bin/cmake"
            cmake.write_text("#!/bin/sh\nprintf '%s\\n' 'Cannot start configured CMake launcher'\nexit 23\n")
            cmake.chmod(0o755)
            result = subprocess.run(["bash", "scripts/build.sh", "dev", "--configure-only"],
                                    cwd=root, text=True, capture_output=True,
                                    env=dict(os.environ, CMAKE_PREFIX_PATH=str(root),
                                             PATH=str(cmake.parent) + os.pathsep + os.environ["PATH"]))
            self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
            self.assertIn("Cannot start configured CMake launcher", result.stdout + result.stderr)

    def fixture(self, root):
        (root / "scripts/config").mkdir(parents=True)
        (root / "cmake").mkdir()
        (root / "scripts/lib").mkdir(parents=True, exist_ok=True)
        for name in ("build.sh", "ctest.sh", "verify.py", "config/verify-profiles.json",
                     "lib/toolchain.py", "lib/toolchain.sh", "lib/runtime_layout.py",
                     "lib/project_paths.py", "lib/build_paths.py", "lib/cp2_install.py"):
            shutil.copy2(ROOT / "scripts" / name, root / "scripts" / name)
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

    def test_platform_specific_profile_rejects_partial_runs_before_dispatch(self):
        with tempfile.TemporaryDirectory(prefix="platform workflow ") as temporary:
            root = Path(temporary)
            self.fixture(root)
            required = "linux" if os.name == "nt" else "windows"
            (root / "scripts/config/verify-profiles.json").write_text(json.dumps({
                "fixture": {"tests": "^scenario$", "platform": required}}))
            (root / "scripts/hyperv-runner.py").write_text(
                "from pathlib import Path\nimport sys\nPath('dispatched').write_text(sys.argv[2])\n")
            command = [sys.executable, "scripts/verify.py", "fixture", "--platform"]
            for platform in ("local", "all", "windows" if required == "linux" else "linux"):
                result = subprocess.run([*command, platform], cwd=root, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(required, result.stderr)
                self.assertFalse((root / "dispatched").exists())
            result = subprocess.run([*command, required], cwd=root, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((root / "dispatched").read_text(), required)

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
                with (root / "CMakeLists.txt").open("a") as stream:
                    stream.write("add_test(NAME added COMMAND subject)\n")
                self.run_verify(root, "^added$", plan=True)
                planned = json.loads((directory(root, "logs") / "dev/verify-plan.json").read_text())
                self.assertEqual(planned["status"], "planned")
                self.assertEqual(json.loads(report.read_text())["status"], "passed")
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
            self.directory_alias(alias, root)
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
