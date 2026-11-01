"""Offline source reconciliation, locking, guest context and failure regressions."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("hyperv_runner", ROOT / "scripts/hyperv-runner.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
from lib import hyperv_cp2_sync, cp2_snapshot, hyperv_interactive, hyperv_lock, hyperv_source, hyperv_transport, hyperv_workspace
from lib.runner_config import load_config
from lib.build_paths import branch_component, directory, host_tag


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


class RunnerTests(unittest.TestCase):

    def test_cp2_snapshot_rebuilds_edits_made_during_previous_build_and_recovers_lost_cache(self):
        if not shutil.which("cmake") or not shutil.which("ninja"):
            self.skipTest("CMake/Ninja unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            project, upstream = self.source_fixture(base)
            (project / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(Freshness NONE)\n'
                'add_custom_command(OUTPUT result.txt COMMAND "${CMAKE_COMMAND}" -E copy '
                '"${CMAKE_SOURCE_DIR}/src/changed.cpp" result.txt DEPENDS "${CMAKE_SOURCE_DIR}/src/changed.cpp")\n'
                'add_custom_target(copy ALL DEPENDS result.txt)\n')
            cache = base / "time-cache.json"
            first, guest = base / "first", base / "guest"
            first.mkdir()
            hyperv_source.stage_tree(project, first, source_times=True)
            entries = cp2_snapshot.prepare(first, guest, cache, time.time_ns())
            cp2_snapshot.commit(cache, entries)
            # The real source changes after the old snapshot, before the old
            # build finishes. Its old restored mtime must not hide that edit.
            original = project / "src/changed.cpp"
            kept_time = original.stat().st_mtime_ns
            original.write_text("new")
            os.utime(original, ns=(kept_time, kept_time))
            def build(*args):
                subprocess.run(["cmake", *args], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            build("-G", "Ninja", "-S", str(guest), "-B", str(guest / "build"))
            build("--build", str(guest / "build"))
            output = guest / "build/result.txt"
            self.assertEqual(output.read_text(), "old")
            second = base / "second"
            second.mkdir()
            hyperv_source.stage_tree(project, second, source_times=True)
            def sync(name):
                staged = base / name
                entries = cp2_snapshot.prepare(second, staged, cache, time.time_ns())
                shutil.copytree(staged, guest, dirs_exist_ok=True)
                cp2_snapshot.commit(cache, entries)
                build("--build", str(guest / "build"))
                self.assertEqual(output.read_text(), "new")
            sync("new-version")
            before = output.stat().st_mtime_ns
            sync("unchanged-version")
            self.assertEqual(output.stat().st_mtime_ns, before)
            (guest / "src/changed.cpp").unlink()
            sync("externally-deleted")
            cache.unlink()
            sync("lost-cache")
            cache.write_text("damaged cache")
            sync("damaged-cache")
            self.assertEqual(original.stat().st_mtime_ns, kept_time)

    def test_snapshot_retries_a_source_edit_during_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            project, upstream = self.source_fixture(base)
            staged = base / "snapshot"
            staged.mkdir()
            original = shutil.copy2
            changed = project / "src/changed.cpp"
            edited = False
            def racing_copy(source, target):
                nonlocal edited
                result = original(source, target)
                if Path(source) == changed and not edited:
                    changed.write_text("updated during snapshot")
                    edited = True
                return result
            with patch.object(hyperv_source.shutil, "copy2", side_effect=racing_copy):
                hyperv_source.stage_tree(project, staged, source_times=True)
            self.assertEqual((staged / "src/changed.cpp").read_text(), "updated during snapshot")

    def test_interactive_branch_worktree_and_unsafe_roots(self):
        workspace = load_config()["windows"]["workspace"]
        root = hyperv_workspace.worktree_root(workspace, "feature/Btrfs images 空")
        # Must accept the same isolated branch root used by the transport.
        hyperv_interactive.interactive_command(root)
        for unsafe in (root + "/child", root + "'", workspace["root"] + "/../elsewhere",
                       workspace["root"] + "-other/current"):
            with self.subTest(root=unsafe), self.assertRaises(ValueError):
                hyperv_interactive.interactive_command(unsafe)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="runner config ")
        self.addCleanup(temporary.cleanup)
        self.config_path = Path(temporary.name) / "runners.local.json"
        shutil.copy2(ROOT / "scripts/config/runners.example.json", self.config_path)
        environment = patch.dict(os.environ, {"SEVENZIP_RUNNER_CONFIG": str(self.config_path)})
        environment.start()
        self.addCleanup(environment.stop)
        os.environ.pop("SEVENZIP_SHARED_TOOLS_ROOT", None)
        os.environ.pop("SEVENZIP_CACHE_ROOT", None)

    def workspace_fixture(self, root):
        shutil.copy2(ROOT / "scripts/lib/workspace.ps1", root / "scripts/lib/workspace.ps1")
        # The command tests execute the guest shell on the current OS. The
        # storage provider stays real, with fixture-owned durable outputs.
        (root / "scripts/lib/build_paths.py").write_text(
            "import os,sys\n" +
            f"sys.path.insert(0, {str(ROOT / 'scripts')!r})\n" +
            "from lib.build_paths import main\n" +
            f"os.environ.update(SEVENZIP_WORKSPACE={str(root)!r}, SEVENZIP_SOURCE_ROOT={str(root)!r})\n" +
            "main()\n")

    def source_fixture(self, root):
        project, upstream = root / "project", root / "upstream"
        for path in (project, upstream):
            path.mkdir()
            git(path, "init", "-q")
            git(path, "config", "user.name", "Fixture")
            git(path, "config", "user.email", "fixture@example.invalid")
        (upstream / "source").write_text("pinned")
        git(upstream, "add", ".")
        git(upstream, "commit", "-qm", "baseline")
        revision = git(upstream, "rev-parse", "HEAD")
        (project / "docs").mkdir()
        (project / "docs/upstream-baseline.json").write_text(json.dumps({"revision": revision}))
        (project / "src").mkdir()
        (project / "src/changed.cpp").write_text("old")
        (project / "src/deleted.cpp").write_text("old")
        (project / ".gitignore").write_text("/build/\nsrc/ignored-secret\n__pycache__/\n")
        git(project, "add", ".")
        git(project, "commit", "-qm", "baseline")
        return project, upstream

    def test_source_includes_current_new_unicode_files_and_deletions_without_private_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, upstream = self.source_fixture(root)
            (project / "src/changed.cpp").write_text("new")
            (project / "src/deleted.cpp").unlink()
            (project / "src/Юникод $(touch injected).cpp").write_text("new file")
            (project / "translations").mkdir()
            catalog = project / "translations/fixture.ts"
            catalog.write_text("current translation input")
            tool_asset = project / "tools/cp2/fixture.json"
            tool_asset.parent.mkdir(parents=True)
            tool_asset.write_text('{"tool": "current resource"}')
            build_manifest = project / "vcpkg.json"
            build_manifest.write_text('{"name":"fixture"}')
            registry_config = project / "vcpkg-configuration.json"
            registry_config.write_text('{"overlay-ports":["ports"]}')
            tidy_config = project / ".clang-tidy"
            tidy_config.write_text("Checks: 'bugprone-*'\n")
            overlay_patch = project / "ports/fixture/fix.patch"
            overlay_patch.parent.mkdir(parents=True)
            overlay_patch.write_text("fixture patch")
            overlay_triplet = project / "triplets/fixture.cmake"
            overlay_triplet.parent.mkdir()
            overlay_triplet.write_text("set(VCPKG_TARGET_ARCHITECTURE arm64)\n")
            (project / "src/ignored-secret").write_text("excluded")
            (project / "build").mkdir()
            (project / "build/secret").write_text("excluded")
            (project / ".codex").mkdir()
            (project / ".codex/private").write_text("excluded")
            (project / "scripts/config").mkdir(parents=True)
            (project / "scripts/config/runners.local.json").write_text("private connection settings")
            (upstream / "source").write_text("uncommitted; do not change")
            before = git(upstream, "status", "--porcelain")
            destination = root / "stage"
            destination.mkdir()
            hyperv_source.stage_tree(project, destination)
            self.assertEqual((destination / "src/changed.cpp").read_text(), "new")
            self.assertEqual((destination / "translations/fixture.ts").read_text(), catalog.read_text())
            self.assertEqual((destination / "tools/cp2/fixture.json").read_text(), tool_asset.read_text())
            for source in (build_manifest, registry_config, tidy_config, overlay_patch, overlay_triplet):
                self.assertEqual((destination / source.relative_to(project)).read_text(), source.read_text())
            self.assertTrue((destination / "src/Юникод $(touch injected).cpp").exists())
            self.assertFalse((destination / "src/deleted.cpp").exists())
            self.assertFalse((destination / "src/ignored-secret").exists())
            self.assertFalse((destination / "build").exists())
            self.assertFalse((destination / ".codex").exists())
            self.assertFalse((destination / "scripts/config/runners.local.json").exists())
            self.assertEqual(before, git(upstream, "status", "--porcelain"))
            with patch.dict(os.environ, {"SEVENZIP_RUN_TOKEN": "fixture"}):
                context = hyperv_source.runner_context(destination)
            self.assertEqual(context["revision"], git(project, "rev-parse", "HEAD"))
            self.assertTrue(context["sourceState"]["modified"])

    def test_source_rejects_links_outside_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, upstream = self.source_fixture(root)
            try:
                (project / "src/link").symlink_to("../../upstream/source")
            except OSError as error:
                self.skipTest(f"Native symlink unavailable: {error}")
            destination = root / "stage"
            destination.mkdir()
            with self.assertRaises(ValueError):
                hyperv_source.stage_tree(project, destination)

    def test_lock_serializes_sync_and_keeps_unconfirmed_or_wrong_owner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "current"
            hyperv_lock.operate(root, "first", "acquire")
            with self.assertRaises(ValueError):
                hyperv_lock.operate(root, "second", "acquire")
            with self.assertRaises(ValueError):
                hyperv_lock.operate(root, "second", "release")
            with self.assertRaises(ValueError):
                hyperv_lock.operate(root, "first", "release-completed")
            (root / ".runner-complete").write_text("first")
            hyperv_lock.operate(root, "first", "release-completed")
            hyperv_lock.operate(root, "second", "acquire")
            self.assertFalse((root / ".runner-complete").exists())
            hyperv_lock.operate(root, "second", "release")

    def test_busy_remote_lock_reports_owner_without_traceback_or_changing_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "guest"
            hyperv_lock.operate(root, "active-run", "acquire")
            def local_remote(record, script, **kwargs):
                return subprocess.run(["bash", "-se"], input=script, text=True,
                                      capture_output=True, check=True)
            record = {"platform": "linux", "config": {"python": Path(sys.executable).as_posix()}}
            with patch.object(runner, "remote", local_remote), \
                    self.assertRaises(subprocess.CalledProcessError) as error:
                runner.lock_command(record, str(root), "new-run", "acquire")
            self.assertIn("active-run", error.exception.stderr)
            self.assertNotIn("Traceback", error.exception.stderr)
            owner = json.loads((root.with_name("guest.lock") / "owner.json").read_text())
            self.assertEqual(owner["token"], "active-run")

    def test_unowned_existing_directory_is_never_adopted_or_deleted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "current"
            root.mkdir()
            (root / "private").write_text("preserve")
            with self.assertRaises(ValueError):
                hyperv_lock.operate(root, "first", "acquire")
            self.assertEqual((root / "private").read_text(), "preserve")
            self.assertFalse(root.with_name("current.lock").exists())

    def test_branch_worktrees_isolate_concurrent_runs_and_reuse_builds_across_checkouts(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            project, _ = self.source_fixture(base)
            git(project, "checkout", "-qb", "feature/Юникод")
            workspace = {"root": str(base / "guest")}
            branch = hyperv_workspace.current_branch(project)
            root = Path(hyperv_workspace.worktree_root(workspace, branch))
            hyperv_lock.operate(root, "first", "acquire")
            (root / "build").mkdir()
            (root / "build/result").write_text("retained build")
            others = []
            # Include names that collide under slash replacement or Windows
            # case folding; they must still acquire independent locks.
            for name in ("feature/other", "feature-other", "Feature-other"):
                other = Path(hyperv_workspace.worktree_root(workspace, name))
                hyperv_lock.operate(other, name, "acquire")
                others.append((other, name))
            clone = base / "another-checkout"
            git(base, "clone", "-q", str(project), str(clone))
            git(clone, "checkout", "-q", branch)
            reused = Path(hyperv_workspace.worktree_root(workspace, hyperv_workspace.current_branch(clone)))
            with self.assertRaisesRegex(ValueError, "busy"):
                hyperv_lock.operate(reused, "duplicate", "acquire")
            hyperv_lock.operate(root, "first", "release")
            hyperv_lock.operate(reused, "next", "acquire")
            self.assertEqual((reused / "build/result").read_text(), "retained build")
            hyperv_lock.operate(reused, "next", "release")
            for other, token in others:
                hyperv_lock.operate(other, token, "release")
            git(project, "checkout", "-q", "--detach")
            with self.assertRaisesRegex(ValueError, "detached HEAD"):
                hyperv_workspace.current_branch(project)

    def test_nested_worktree_runner_uses_shared_harness_and_its_own_uncommitted_source(self):
        with tempfile.TemporaryDirectory(prefix="worktree harness ") as temporary:
            base = Path(temporary)
            project, upstream = self.source_fixture(base)
            upstream.rename(base / "7zip-upstream")
            shutil.copytree(ROOT / "scripts/lib", project / "scripts/lib", ignore=shutil.ignore_patterns("__pycache__"))
            shutil.copy2(ROOT / "scripts/hyperv-runner.py", project / "scripts/hyperv-runner.py")
            (project / "scripts/fixture.py").write_text(
                "from pathlib import Path\nPath('observed').write_text(Path('src/changed.cpp').read_text())\n")
            (project / ".gitignore").write_text((project / ".gitignore").read_text() + "/.worktrees/\n/scripts/config/*.local.json\n")
            git(project, "add", ".")
            git(project, "commit", "-qm", "runner harness")
            worktree = project / ".worktrees/feature"
            git(project, "worktree", "add", "-qb", "feature/nested", str(worktree))
            (worktree / "src/changed.cpp").write_text("feature edits")
            (project / "src/changed.cpp").write_text("main edits must not travel")
            settings = json.loads(self.config_path.read_text())
            settings["runners"] = {"linux": settings["runners"]["linux"]}
            settings["runners"]["linux"].update(ssh=["ssh", "fixture"], python=sys.executable,
                                                workspace={"root": "/fixture/sevenzip/runs"})
            (project / "scripts/config").mkdir()
            (project / "scripts/config/runners.local.json").write_text(json.dumps(settings))
            # Replace only guest transport/execution with a local guest. Keep
            # CLI selection, config discovery, staging, locks and reports real.
            driver = base / "drive.py"
            driver.write_text('''import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path.cwd() / 'scripts'))
from lib import hyperv_lock
spec = importlib.util.spec_from_file_location('runner', 'scripts/hyperv-runner.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
def guest_path(root):
    return Path(__file__).parent / 'guest' / root.rsplit('/', 1)[-1]
def sync(argv, **kwargs):
    shutil.copytree(argv[-2], guest_path(argv[-1].split(':', 1)[1].rstrip('/')), dirs_exist_ok=True)
def lock(record, root, token, action, **kwargs):
    hyperv_lock.operate(guest_path(root), token, action)
def execute(record, platform, preset, root, token, command, *args):
    subprocess.run(command, cwd=guest_path(root), check=True)
    (guest_path(root) / '.runner-complete').write_text(token)
runner.hyperv_cp2_sync.synchronize = lambda record, platform, source, root, *args: shutil.copytree(source, guest_path(root), dirs_exist_ok=True)
runner.lock_command = lock
runner.execute_check = execute
runner.collect_logs = lambda *args: {}
sys.argv = ['runner', 'run', 'linux', '--', 'scripts/fixture.py']
raise SystemExit(runner.main())
''')
            environment = dict(os.environ)
            environment.pop("SEVENZIP_RUNNER_CONFIG", None)
            result = subprocess.run([sys.executable, str(driver)], cwd=worktree, env=environment,
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report_path = Path(next(line.removeprefix("Report: ") for line in result.stdout.splitlines()
                                    if line.startswith("Report: ")))
            report = json.loads(report_path.read_text())
            self.assertEqual(report["branch"], "feature/nested")
            guest = base / "guest" / report["platforms"][0]["directory"].rsplit("/", 1)[-1]
            self.assertEqual((guest / "observed").read_text(), "feature edits")
            self.assertFalse(guest.with_name(guest.name + ".lock").exists())
            self.assertFalse((guest / "scripts/config/runners.local.json").exists())

    def test_invalid_workspace_paths_fail_before_connecting(self):
        initial = json.loads(self.config_path.read_text())
        for platform, path in (("linux", "/unowned"),
                               ("linux", "/mnt/custom-data/runs/../other"),
                               ("windows", "C:/"),
                               ("windows", "C:/runner/project/../other")):
            with self.subTest(platform=platform, path=path):
                settings = json.loads(json.dumps(initial))
                settings["runners"][platform]["workspace"] = {"root": path}
                self.config_path.write_text(json.dumps(settings))
                with patch.object(sys, "argv", ["hyperv-runner.py", "status", platform]), \
                        patch.object(runner, "discover") as discover:
                    with self.assertRaises(ValueError):
                        runner.main()
                    discover.assert_not_called()

    def test_missing_config_fails_before_connecting(self):
        self.config_path.unlink()
        with patch.object(runner, "invoke") as invoke:
            with self.assertRaisesRegex(ValueError, "configuration missing"):
                runner.discover("linux")
            invoke.assert_not_called()

    def test_reports_share_one_transfer_and_preserve_bytes_with_missing_or_stale_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / "sources/guest 空"
            destination = base / "download"
            root.mkdir(parents=True)
            destination.mkdir()
            payload = "Command diagnostics: Юникод 空\r\n".encode() + bytes(range(256)) * 1024
            (root / "check.log").write_bytes(payload)
            platform = "windows" if os.name == "nt" else "linux"
            report = (base / "branches" / branch_component("fixture") / "logs" /
                      (platform + "-x86_64") / "modules-linux-dev/verify-result.json")
            report.parent.mkdir(parents=True)
            record = {"platform": platform, "config": {"python": sys.executable}}
            def execute(record, script, **kwargs):
                argv = runner.powershell(script).split() if os.name == "nt" else ["bash", "-se"]
                return subprocess.run(argv, input=None if os.name == "nt" else script,
                                      text=True, check=True, **kwargs)
            transfer = Mock(side_effect=execute)
            for scenario in ("complete", "stale-verification", "missing-verification"):
                with self.subTest(scenario=scenario):
                    report.write_text(json.dumps({"status": "passed", "run_token": "current"}))
                    if scenario == "stale-verification":
                        report.write_text(json.dumps({"run_token": "previous"}))
                    elif scenario == "missing-verification":
                        report.unlink()
                    # Leftovers from earlier downloads must not masquerade as this run.
                    (destination / report.name).write_text("old result")
                    transfer.reset_mock()
                    result = hyperv_transport.collect_logs(
                        record, root.as_posix(), destination,
                        ["python", "scripts/verify.py", "--preset", "modules-linux-dev"],
                        "current", transfer, None, "fixture")
                    self.assertEqual(transfer.call_count, 1)
                    self.assertEqual((destination / "check.log").read_bytes(), payload)
                    if scenario in ("stale-verification", "missing-verification"):
                        self.assertIn("verification_error", result)
                        self.assertFalse((destination / report.name).exists())
                    else:
                        self.assertEqual(result["verification"]["status"], "passed")
            (root / "check.log").unlink()
            report.write_text(json.dumps({"status": "failed", "run_token": "current"}))
            with self.assertRaisesRegex(OSError, "command log is unavailable"):
                hyperv_transport.collect_logs(
                    record, root.as_posix(), destination,
                    ["python", "scripts/verify.py", "--preset", "modules-linux-dev"],
                    "current", transfer, None, "fixture")
            self.assertFalse((destination / "check.log").exists())
            self.assertEqual((destination / report.name).read_bytes(), report.read_bytes())

    def test_connections_use_configured_alias_port_and_nondefault_windows_drive(self):
        settings = {"schema": 1, "runners": load_config()}
        settings["runners"]["windows"]["ssh"] = ["ssh", "-p", "2222", "builder@custom-host"]
        self.config_path.write_text(json.dumps(settings))
        with patch.object(runner, "STATE", self.config_path.parent / "state"):
            record = runner.discover("windows")
        record["name"] = "custom-builder"
        command = hyperv_cp2_sync.command("cp2", record, Path("source"), "D:/build/custom/runs/current", "cp2-server")
        self.assertIn("builder@custom-host:D:/build/custom/runs/current", command)
        with patch.object(runner, "invoke") as invoke:
            runner.remote(record, "Write-Output configured")
            self.assertIn("powershell.exe", invoke.call_args.args[0][-1])
            self.assertEqual(invoke.call_args.args[0][:-1], record["ssh_argv"])

    @unittest.skipUnless(shutil.which("powershell.exe"), "Windows PowerShell is required")
    def test_interactive_result_recovers_lock_without_an_observer(self):
        import ctypes
        from ctypes import wintypes

        session = wintypes.DWORD()
        get_session = ctypes.windll.kernel32.ProcessIdToSessionId
        get_session.argtypes = (wintypes.DWORD, ctypes.POINTER(wintypes.DWORD))
        get_session.restype = wintypes.BOOL
        if not get_session(os.getpid(), ctypes.byref(session)):
            raise ctypes.WinError()
        if not session.value:
            self.skipTest("An interactive Windows session is required")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "completion job"
            token = "owned-completion-fixture"
            hyperv_lock.operate(root, token, "acquire")
            (root / "runner-tmp").mkdir()
            (root / "check.cmd").write_bytes(b"@echo off\r\nexit /b 23\r\n")
            command = hyperv_interactive.interactive_child_command(root.as_posix(), token=token)
            result = subprocess.run(runner.powershell(command).split(), capture_output=True,
                                    timeout=20)
            self.assertEqual(result.returncode, 23, result.stderr.decode(errors="replace"))
            hyperv_lock.operate(root, token, "release-completed")
            hyperv_lock.operate(root, "next-owned-job", "acquire")
            hyperv_lock.operate(root, "next-owned-job", "release")

    @unittest.skipUnless(shutil.which("powershell.exe"), "Windows PowerShell is required")
    def test_windows_command_executes_with_large_unicode_input_and_delayed_eof(self):
        with tempfile.TemporaryDirectory() as temporary:
            relay = Path(temporary) / "ssh.py"
            relay.write_text('''import subprocess, sys
payload = sys.stdin.buffer.read()
child = subprocess.Popen(sys.argv[1].split(), stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    child.stdin.write(payload)
    child.stdin.flush()
    code = child.wait(timeout=10)
except BaseException:
    child.kill()
    child.wait()
    raise
finally:
    child.stdin.close()
sys.stdout.buffer.write(child.stdout.read())
sys.stderr.buffer.write(child.stderr.read())
sys.exit(code)
''')
            record = {"platform": "windows", "ssh_argv": [sys.executable, str(relay)]}
            script = ("[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n"
                      "Write-Output 'данные ✓'\n" + "# padding for the SSH input buffer\n" * 2000)
            result = runner.remote(record, script, capture_output=True, timeout=20)
            self.assertEqual(result.stdout.strip(), "данные ✓")
            with self.assertRaises(subprocess.CalledProcessError) as error:
                runner.remote(record, "exit 7", capture_output=True, timeout=20)
            self.assertEqual(error.exception.returncode, 7)


    def test_custom_readiness_command_supplies_connection_without_fixed_vm_names(self):
        provider = self.config_path.parent / "discover.py"
        provider.write_text("print(" + repr(json.dumps({"vm": "my-build-vm", "vm_id": "fixture",
                            "ssh_argv": ["ssh", "my-dynamic-host"]})) + ")\n")
        settings = {"schema": 1, "runners": load_config()}
        config = settings["runners"]["linux"]
        config.pop("ssh")
        config["ready_command"] = [sys.executable, str(provider)]
        self.config_path.write_text(json.dumps(settings))
        with patch.object(runner, "STATE", self.config_path.parent / "state"):
            record = runner.discover("linux")
        command = hyperv_cp2_sync.command("cp2", record, Path("source"), config["workspace"]["root"] + "/current", "cp2-server")
        self.assertTrue(any(arg.startswith("my-dynamic-host:") for arg in command))

    def test_windows_host_guard_uses_configuration_and_rejects_a_different_machine(self):
        expected = load_config()["windows"]["computer_name"]
        for host, succeeds in ((expected.lower(), True), ("other-host", False)):
            environment = dict(os.environ, COMPUTERNAME=host, SEVENZIP_RUNNER_PYTHON=sys.executable)
            environment.pop("SEVENZIP_WINDOWS_RUNNER_HOST", None)
            result = subprocess.run(["bash", "-c", "source scripts/lib/windows-runner.sh; sevenzip_require_windows_runner"],
                                    cwd=ROOT, env=environment, text=True, capture_output=True)
            self.assertEqual(result.returncode == 0, succeeds, result.stderr)

    def test_failed_command_keeps_exit_code_literal_args_context_and_completion(self):
        with tempfile.TemporaryDirectory(prefix="runner Юникод ") as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/lib").mkdir()
            self.workspace_fixture(root)
            (root / "scripts/fixture.sh").write_text('printf "%s\\n" "$@" "$TEMP" "$QT_QPA_PLATFORM" "$CMAKE_BUILD_PARALLEL_LEVEL"\nexit 23\n')
            args = ["space name", "Юникод", "$(touch injected)", "a'b", "", "first\nsecond"]
            def local_remote(record, script, **kwargs):
                return subprocess.run(["bash", "-se"], input=script, text=True, check=True, capture_output=True)
            with patch.object(runner, "remote", local_remote), \
                    patch.dict(os.environ, {"QT_QPA_PLATFORM": "offscreen", "CMAKE_BUILD_PARALLEL_LEVEL": "1"}):
                with self.assertRaises(subprocess.CalledProcessError) as error:
                    runner.execute_check({"branch": "snapshot"}, "linux", "dev", root.as_posix(), "token",
                                         ["bash", "scripts/fixture.sh", *args], False)
            self.assertEqual(error.exception.returncode, 23)
            log = (root / "check.log").read_text()
            self.assertIn("\n".join(args) + "\n", log)
            lines = log.splitlines()
            self.assertEqual(lines[-2:], ["offscreen", "1"])
            scratch = Path(lines[-3])
            self.assertTrue(scratch.is_dir())
            self.assertTrue(scratch.resolve().is_relative_to(root.resolve()))
            self.assertEqual((root / ".runner-complete").read_text().strip(), "token")
            self.assertFalse((root / "injected").exists())
            self.assertFalse((root / ".git").exists())

    def test_manifest_command_receives_vcpkg_without_a_competing_qt_sdk(self):
        # Without VCPKG_ROOT the actual build wrapper rejects the manifest
        # preset; a configured Qt kit must not take precedence over its Qt.
        with tempfile.TemporaryDirectory(prefix="manifest runner ") as temporary:
            root = Path(temporary)
            (root / "scripts/lib").mkdir(parents=True)
            self.workspace_fixture(root)
            (root / "scripts/probe.py").write_text('''import json, os, subprocess, sys
from pathlib import Path
subprocess.run([sys.executable, str(Path(os.environ['VCPKG_ROOT']) / 'host-tool.py')], check=True)
Path('selected.json').write_text(json.dumps({key: os.environ.get(key) for key in
    ('VCPKG_ROOT', 'VCPKG_MAX_CONCURRENCY', 'VCPKG_INSTALLED_DIR',
     'SEVENZIP_OUTPUT_ROOT', 'CMAKE_PREFIX_PATH', 'PATH')}))
''')
            (root / "scripts/prepare-runtime.py").write_text(
                (root / "scripts/probe.py").read_text().replace("selected.json", "prepared.json"))
            config = load_config()["linux"]

            def local_remote(record, script, **kwargs):
                return subprocess.run(["bash", "-se"], input=script, text=True,
                                      check=True, capture_output=True)

            settings = dict(config, workspace={"root": (root / "sources").as_posix()},
                            tools_root=(root / "shared tools").as_posix(),
                            cache_root=(root / "persistent cache").as_posix())
            tool_root = Path(settings['tools_root']) / host_tag() / "vcpkg"
            tool_root.mkdir(parents=True, exist_ok=True)
            archive = Path(settings['cache_root']) / host_tag() / "vcpkg/archives/retained"
            archive.parent.mkdir(parents=True)
            archive.write_bytes(b'existing dependency')
            (tool_root / "host-tool.py").write_text(
                "import os\nfrom pathlib import Path\n"
                "assert (Path(os.environ['VCPKG_DEFAULT_BINARY_CACHE'])/'retained').read_bytes()==b'existing dependency'\n")
            for jobs in (None, "2"):
                with self.subTest(jobs=jobs), patch.object(runner, "remote", local_remote), \
                        patch.dict(os.environ):
                    if jobs is None:
                        os.environ.pop("CMAKE_BUILD_PARALLEL_LEVEL", None)
                    else:
                        os.environ["CMAKE_BUILD_PARALLEL_LEVEL"] = jobs
                    runner.execute_check({"config": settings, "branch": "snapshot"}, "linux", "vcpkg-dev",
                                         root.as_posix(), "token", [sys.executable, "scripts/probe.py"], True)
                    selected = json.loads((root / "selected.json").read_text())
                    self.assertEqual(selected["VCPKG_MAX_CONCURRENCY"], jobs or str(os.cpu_count() or 1))
            selected = json.loads((root / "selected.json").read_text())
            prepared = json.loads((root / "prepared.json").read_text())
            self.assertEqual({key: value for key, value in selected.items() if key != 'PATH'},
                             {key: value for key, value in prepared.items() if key != 'PATH'})
            self.assertFalse(selected["CMAKE_PREFIX_PATH"])
            self.assertNotIn(config["qt_prefix"] + "/bin", selected["PATH"].split(os.pathsep))
            self.assertFalse((root / "injected").exists())
            self.assertTrue(Path(selected['VCPKG_INSTALLED_DIR']).is_relative_to(root))
            self.assertTrue(Path(selected['SEVENZIP_OUTPUT_ROOT']).is_relative_to(root))

    @unittest.skipUnless(os.name == "nt", "Windows compiler bootstrap environment")
    def test_windows_bootstrap_preserves_selected_vcpkg_for_runtime_and_command(self):
        # vcvars sets its bundled VCPKG_ROOT. Both native preparation and the
        # requested command must instead execute the explicitly selected tool.
        with tempfile.TemporaryDirectory(prefix="windows bootstrap ") as temporary:
            root = Path(temporary)
            (root / "scripts/lib").mkdir(parents=True)
            self.workspace_fixture(root)
            selected, bundled = directory(root, "tools") / "vcpkg", root / "bundled tool"
            for tool_directory, status in ((selected, 0), (bundled, 61)):
                tool_directory.mkdir(parents=True)
                (tool_directory / "vcpkg.py").write_text(f"raise SystemExit({status})\n")
            (root / "vcvars.bat").write_text(
                '@echo off\nset "VCPKG_ROOT=' + bundled.as_posix() + '"\n')
            probe = '''import os, subprocess, sys
from pathlib import Path
subprocess.run([sys.executable, str(Path(os.environ['VCPKG_ROOT']) / 'vcpkg.py')], check=True)
jobs = str(os.cpu_count() or 1)
assert os.environ['CMAKE_BUILD_PARALLEL_LEVEL'] == jobs
assert os.environ['VCPKG_MAX_CONCURRENCY'] == jobs
'''
            (root / "scripts/prepare-runtime.py").write_text(probe)
            (root / "scripts/probe.py").write_text(probe)
            settings = dict(load_config()["windows"], python=Path(sys.executable).as_posix(),
                            vcvars=(root / "vcvars.bat").as_posix(),
                            workspace={"root": (root / "sources").as_posix()},
                            path=[Path(sys.executable).parent.as_posix(),
                                  Path(shutil.which("bash")).parent.as_posix()])

            def local_remote(record, script, **kwargs):
                return subprocess.run(runner.powershell(script).split(), text=True,
                                      capture_output=True)

            with patch.object(runner, "remote", local_remote), patch.dict(os.environ):
                os.environ.pop("CMAKE_BUILD_PARALLEL_LEVEL", None)
                result = runner.execute_check({"config": settings, "branch": "snapshot"}, "windows", "vcpkg-dev",
                                              root.as_posix(), "fixture",
                                              [Path(sys.executable).as_posix(), "scripts/probe.py"], True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_linux_is_first_and_failure_stops_secondary_verification(self):
        spec = importlib.util.spec_from_file_location("verify_order", ROOT / "scripts/verify.py")
        verify = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verify)
        for selection, failure, expected in (([], 0, ["linux"]),
                (["--platform", "all"], 0, ["linux", "windows"]),
                (["--platform", "all"], 7, ["linux"])):
            with self.subTest(selection=selection, failure=failure):
                visited = []

                def run_stage(command, **kwargs):
                    platform = command[3]
                    visited.append(platform)
                    guest = command[command.index("--") + 2:]
                    guest_args = verify.parse_args(guest, {})
                    if guest_args.platform != "local":
                        raise RuntimeError("Guest verification would dispatch another runner")
                    return subprocess.CompletedProcess(command, failure if platform == "linux" else 0)

                with patch.object(verify.subprocess, "run", side_effect=run_stage):
                    status = verify.main(["--tests", "^fixture$", *selection])
                self.assertEqual(status, failure)
                self.assertEqual(visited, expected)

    def test_remote_verification_prepares_the_requested_runtime_toolchain(self):
        # A default runtime with a module preset reproduced incompatible ASan
        # runtimes. Exercise the actual verify -> runner -> bootstrap handoff;
        # the fixture command refuses a runtime prepared for another preset.
        spec = importlib.util.spec_from_file_location("verify_fixture", ROOT / "scripts/verify.py")
        verify = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verify)
        with tempfile.TemporaryDirectory(prefix="runtime toolchain ") as temporary:
            root = Path(temporary)
            (root / "scripts/lib").mkdir(parents=True)
            self.workspace_fixture(root)
            (root / "scripts/prepare-runtime.py").write_text('''import argparse, os, json
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--preset', default='dev')
args, _ = parser.parse_known_args()
Path('runtime-toolchain').write_text(args.preset)
Path('runtime-analysis').write_text(os.environ.get('SEVENZIP_ENABLE_CLANG_TIDY', 'unset'))
Path('runtime-diagnostics').write_text(json.dumps({name: os.environ.get(name) for name in
    ('SEVENZIP_BUILD_MODE', 'SEVENZIP_DEBUG_SYMBOLS', 'SEVENZIP_ENABLE_SANITIZERS')}))
with Path('preparations').open('a') as stream: stream.write(args.preset + '\\n')
''')
            (root / "scripts/verify.py").write_text('''import argparse, os, json
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--preset', required=True)
args, _ = parser.parse_known_args()
matches = (Path('runtime-toolchain').read_text() == args.preset and
           Path('runtime-analysis').read_text() == Path('requested-analysis').read_text() and
           os.environ.get('SEVENZIP_ENABLE_CLANG_TIDY') == Path('requested-analysis').read_text())
expected = json.loads(Path('requested-diagnostics').read_text())
matches = matches and json.loads(Path('runtime-diagnostics').read_text()) == expected
matches = matches and all(os.environ.get(name) == value for name, value in expected.items())
raise SystemExit(0 if matches else 9)
''')
            original_run = subprocess.run

            def local_remote(record, script, **kwargs):
                return original_run(["bash", "-se"], input=script, text=True,
                                    check=True, capture_output=True)

            def dispatch(command, **kwargs):
                if len(command) < 2 or Path(command[1]).name != "hyperv-runner.py":
                    return original_run(command, **kwargs)
                args = runner.parse_arguments(command[2:])
                args.command[0] = sys.executable
                runner.execute_check({"branch": "snapshot"}, "linux", args.preset, root.as_posix(), "token",
                                     args.command, args.prepare_runtime)
                return subprocess.CompletedProcess(command, 0)

            with patch.object(runner, "remote", local_remote), \
                    patch.object(verify.subprocess, "run", dispatch):
                for mode in ("ON", "OFF"):
                    (root / "requested-analysis").write_text(mode)
                    diagnostics = {"SEVENZIP_BUILD_MODE": "sanitize" if mode == "ON" else "lite",
                                   "SEVENZIP_DEBUG_SYMBOLS": mode, "SEVENZIP_ENABLE_SANITIZERS": mode}
                    (root / "requested-diagnostics").write_text(json.dumps(diagnostics))
                    with self.subTest(mode=mode), \
                            patch.dict(os.environ, dict(diagnostics, SEVENZIP_ENABLE_CLANG_TIDY=mode)):
                        self.assertEqual(verify.main(["--platform", "linux", "--prepare-runtime", "--preset", "modules-linux-dev",
                                                      "--tests", "^scenario$"]), 0)
                        prepared = (root / "preparations").read_bytes()
                        self.assertEqual(verify.main(["--platform", "linux", "--preset", "modules-linux-dev",
                                                      "--tests", "^scenario$"]), 0)
                        self.assertEqual((root / "preparations").read_bytes(), prepared)

    def test_invalid_build_parallelism_is_rejected_before_remote_execution(self):
        for platform in ("linux", "windows"):
            for value in ("0", "-1", "", "1; touch injected"):
                with self.subTest(platform=platform, value=value), \
                        patch.dict(os.environ, {"CMAKE_BUILD_PARALLEL_LEVEL": value}), \
                        patch.object(runner, "remote") as remote:
                    with self.assertRaises(ValueError):
                        runner.execute_check({}, platform, "dev", "/unused", "token", prepare_runtime=False)
                    remote.assert_not_called()

    def test_invalid_analysis_mode_does_not_launch_remote_commands(self):
        for platform in ("linux", "windows"):
            for name in ("SEVENZIP_ENABLE_CLANG_TIDY", "SEVENZIP_BUILD_MODE",
                         "SEVENZIP_DEBUG_SYMBOLS", "SEVENZIP_ENABLE_SANITIZERS",
                         "SEVENZIP_UI_REPORTS", "SEVENZIP_SCRIPT_OUTPUT"):
                with self.subTest(platform=platform, option=name), \
                        patch.dict(os.environ, {name: 'ON" & echo injected'}), \
                        patch.object(runner, "remote") as remote:
                    with self.assertRaises(ValueError):
                        runner.execute_check({}, platform, "dev", "/unused", "token", prepare_runtime=False)
                    remote.assert_not_called()

    def test_gate_accepts_runner_snapshot_without_project_git_and_keeps_local_diff_check(self):
        with tempfile.TemporaryDirectory(prefix="gate snapshot ") as temporary:
            root = Path(temporary)
            (root / "scripts/lib").mkdir(parents=True)
            (root / "bin").mkdir()
            shutil.copy2(ROOT / "scripts/check.sh", root / "scripts/check.sh")
            shutil.copy2(ROOT / "scripts/lib/agent-output.sh", root / "scripts/lib/agent-output.sh")
            for helper in ("toolchain.py", "toolchain.sh", "runtime_layout.py", "build_paths.py",
                           "project_paths.py", "cp2_install.py"):
                shutil.copy2(ROOT / "scripts/lib" / helper, root / "scripts/lib" / helper)
            for name, content in (("bin/git", "exit 73\n"), ("bin/clang-format", "exit 0\n"),
                                  ("scripts/build.sh", "touch built\n"), ("scripts/ctest.sh", "exit 0\n")):
                path = root / name
                path.write_text("#!/usr/bin/env bash\n" + content)
                path.chmod(0o755)
            environment = dict(os.environ, PATH=str(root / "bin") + os.pathsep + os.environ["PATH"],
                               CODEX_SANDBOX="none", SEVENZIP_RUN_TOKEN="",
                               CMAKE_PREFIX_PATH=str(root), CC=shutil.which("cc") or "cc",
                               CXX=shutil.which("c++") or "c++")
            local = subprocess.run(["bash", "scripts/check.sh"], cwd=root, env=environment,
                                   text=True, capture_output=True)
            self.assertEqual(local.returncode, 73, local.stdout + local.stderr)
            self.assertFalse((root / "built").exists())
            environment["SEVENZIP_RUN_TOKEN"] = "fixture"
            guest = subprocess.run(["bash", "scripts/check.sh"], cwd=root, env=environment,
                                   text=True, capture_output=True)
            self.assertEqual(guest.returncode, 0, guest.stdout + guest.stderr)
            self.assertTrue((root / "built").exists())

    def test_custom_command_keeps_percent_variables_and_metacharacters_literal(self):
        payload = runner.command_payload("dev", ["printf", "%s\\n", "%PATH%", "$(touch injected)", "Юникод"])
        result = subprocess.run(["bash", "-se"], input=payload, text=True, capture_output=True, check=True)
        self.assertEqual(result.stdout, "%PATH%\n$(touch injected)\nЮникод\n")

    def test_release_payload_stops_after_failed_debug_gate(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/check.sh").write_text("exit 29\n")
            (root / "scripts/build.sh").write_text("touch unexpected\n")
            result = subprocess.run(["bash", "-se"], input=runner.command_payload("release", None), cwd=root, text=True)
            self.assertEqual(result.returncode, 29)
            self.assertFalse((root / "unexpected").exists())

    def test_release_payload_builds_after_gate_without_testing_disabled_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/check.sh").write_text("touch gate-passed\n")
            (root / "scripts/build.sh").write_text(
                'test -f gate-passed || exit 31\n'
                'test "$1" = release || exit 32\n'
                'touch release-built\n')
            # Release has BUILD_TESTING=OFF: asking CTest for it is an error.
            (root / "scripts/ctest.sh").write_text("exit 33\n")
            result = subprocess.run(["bash", "-se"], input=runner.command_payload("release", None),
                                    cwd=root, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((root / "release-built").exists())

    def test_interrupted_execution_requests_confirmed_release_and_records_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            record = {"name": "runner-linux", "ssh_argv": ["ssh", "fixture"], "ip": "fixture",
                      "platform": "linux", "config": {"python": sys.executable}}
            current = Path(temporary) / "worktree/current"
            def local_lock(record, root, token, action, **kwargs):
                return hyperv_lock.operate(current, token, action)
            with patch.object(runner, "discover", return_value=record), \
                    patch.object(runner, "remote", side_effect=subprocess.CalledProcessError(255, "ssh")), \
                    patch.object(runner, "lock_command", side_effect=local_lock), \
                    patch.object(runner.hyperv_cp2_sync, "synchronize"), \
                    patch.object(runner, "execute_check", side_effect=KeyboardInterrupt()):
                with self.assertRaises(KeyboardInterrupt):
                    runner.run_platform("linux", Path(temporary), Path(temporary), "dev", None, False,
                                        branch="fixture")
                self.assertTrue(current.with_name("current.lock").is_dir())
            result = json.loads((Path(temporary) / "linux/run.json").read_text())
            self.assertEqual(result["status"], "interrupted")
            self.assertIn("log_error", result)

    @unittest.skipUnless(os.name == "nt", "Windows PowerShell script dispatch")
    def test_powershell_script_dispatch_works_with_restricted_process_default(self):
        with tempfile.TemporaryDirectory(prefix="PowerShell script ") as temporary:
            root = Path(temporary)
            git(root, "init", "-q")
            (root / "scripts").mkdir()
            (root / "scripts/fixture.ps1").write_text("Write-Output $args[0]\n")
            with patch.object(runner, "ROOT", root):
                command = runner.script_command(["scripts/fixture.ps1", "literal space $(not-executed)"],
                                                ["windows"])
            environment = dict(os.environ, PSExecutionPolicyPreference="Restricted")
            result = subprocess.run(command, cwd=root, env=environment, text=True,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), "literal space $(not-executed)")

    def test_cli_rejects_wrong_desktop_transport_and_unsafe_script_paths(self):
        for argv in (["check", "linux", "--interactive"], ["check", "windows", "--elevated"],
                     ["check", "linux", "--transport", "scp"], ["run", "all"],
                     ["run", "linux", "--", "../other.sh"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    runner.parse_arguments(argv)
        self.assertEqual(runner.parse_arguments(["status", "all"]).platforms, ["linux", "windows"])


if __name__ == "__main__":
    unittest.main()
