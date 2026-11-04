import importlib.util
import hashlib
import io
import json
import pathlib
import os
import shutil
import subprocess
import sys
import time
import tempfile
import tarfile
import unittest
from unittest.mock import patch


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts/upstream_baseline.py"
spec = importlib.util.spec_from_file_location("upstream_baseline", SCRIPT)
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.DEVNULL).decode().strip()


class UpstreamBaselineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = pathlib.Path(self.temporary.name)
        self.port = root / "port"
        self.upstream = root / "upstream"
        self.port.mkdir()
        self.upstream.mkdir()
        for repo in (self.port, self.upstream):
            git(repo, "init", "-q")
            git(repo, "config", "user.name", "Fixture")
            git(repo, "config", "user.email", "fixture@example.invalid")
            # Exact-byte fixtures must not inherit the host Git newline policy.
            git(repo, "config", "core.autocrlf", "false")
        self.source = "CPP/7zip/UI/FileManager/NetFolder.cpp"
        self.exact = "third_party/7zip/CPP/Windows/Net.cpp"
        self.local = "src/CPP/7zip/UI/FileManager/PanelSort.h"
        (self.upstream / self.source).parent.mkdir(parents=True)
        (self.upstream / "CPP/Windows").mkdir(parents=True)
        (self.upstream / self.source).write_text("source body\n")
        (self.upstream / "CPP/Windows/Net.cpp").write_bytes(b"exact body\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "baseline")
        self.old = git(self.upstream, "rev-parse", "HEAD")
        (self.port / "third_party/7zip/CPP/Windows").mkdir(parents=True)
        (self.port / "src/CPP/7zip/UI/FileManager").mkdir(parents=True)
        (self.port / self.exact).write_bytes(b"exact body\n")
        (self.port / ("src/" + self.source)).write_text("source body with Qt adapter\n")
        (self.port / self.local).write_text("local declaration\n")
        git(self.port, "add", ".")
        git(self.port, "commit", "-qm", "port")
        self.manifest = {
            "schema": 1,
            "repository": "https://github.com/ip7z/7zip.git",
            "revision": self.old,
            "exact": [self.exact],
            "adapted": ["src/" + self.source],
            "local": [self.local],
        }
        self.manifest_path = root / "manifest.json"
        self.manifest_path.write_text(json.dumps(self.manifest))
        old_root = baseline.ROOT
        baseline.ROOT = self.port
        self.addCleanup(setattr, baseline, "ROOT", old_root)

    def test_inventory_and_exact_bytes(self):
        data = baseline.load_manifest(self.manifest_path)
        baseline.verify(data, self.upstream)
        (self.port / self.exact).write_text("changed vendor\n")
        with self.assertRaisesRegex(ValueError, "Exact vendor differs"):
            baseline.verify(data, self.upstream)

    def test_adapted_original_hash_is_checked_against_upstream(self):
        data = dict(self.manifest, original_sha256={
            "src/" + self.source: hashlib.sha256(
                baseline.git(self.upstream, "show", self.old + ":" + self.source)).hexdigest()})
        baseline.verify(data, self.upstream)
        data["original_sha256"]["src/" + self.source] = "0" * 64
        with self.assertRaisesRegex(ValueError, "Original source hash mismatch"):
            baseline.verify(data, self.upstream)

    def reference_manifest(self):
        paths = [self.source, "CPP/Windows/Net.cpp"]
        return {
            "repository": "https://github.com/qemu/qemu.git",
            "revision": self.old,
            "files": {path: hashlib.sha256(baseline.git(self.upstream, "show",
                                                     self.old + ":" + path)).hexdigest()
                      for path in paths},
            "adapted": {self.source: "src/" + self.source},
        }

    def test_reference_verifies_originals_without_pinning_local_adaptation(self):
        data = self.reference_manifest()
        baseline.verify_reference(data, self.upstream, self.port)
        (self.port / ("src/" + self.source)).write_text("revised platform adaptation\n")
        baseline.verify_reference(data, self.upstream, self.port)
        data["files"][self.source] = "0" * 64
        with self.assertRaisesRegex(ValueError, "source hash mismatch"):
            baseline.verify_reference(data, self.upstream, self.port)

    def test_reference_rejects_unchanged_owner_from_another_dependency(self):
        data = self.reference_manifest()
        data["owners"] = {**data["adapted"], "CPP/Windows/Net.cpp": self.exact}
        data["unchanged"] = {"CPP/Windows/Net.cpp": self.exact}
        baseline.verify_reference(data, self.upstream, self.port)
        unrelated = "third_party/other/Net.cpp"
        (self.port / unrelated).parent.mkdir(parents=True)
        (self.port / unrelated).write_bytes(b"different dependency body\n")
        data["owners"]["CPP/Windows/Net.cpp"] = unrelated
        with self.assertRaisesRegex(ValueError, "Unchanged reference owner differs"):
            baseline.verify_reference(data, self.upstream, self.port)

    def test_reference_edits_are_checked_and_reviewed_without_changing_originals(self):
        data = self.reference_manifest()
        original = baseline.git(self.upstream, "show", self.old + ":" + self.source)
        data.pop("adapted")
        owner = self.port / "adaptation.json"
        owner.write_text(json.dumps({"source": self.source, "symbols": ["fixture"],
                                    "replacements": [{"before": "source body", "after": "adapted body"}]}))
        data["adapted_edits"] = {self.source: "adaptation.json"}
        baseline.verify_reference(data, self.upstream, self.port)
        report = baseline.review_reference(data, self.upstream, self.old, [], self.port)
        self.assertIn("+adapted body", report.splitlines())
        self.assertEqual(baseline.git(self.upstream, "show", self.old + ":" + self.source), original)
        (self.upstream / self.source).write_text("changed upstream context\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "change context")
        proposed = git(self.upstream, "rev-parse", "HEAD")
        report = baseline.review_reference(data, self.upstream, proposed, [], self.port)
        self.assertIn("Recorded edits require review:", report)
        self.assertEqual((self.upstream / self.source).read_text(), "changed upstream context\n")
        owner.write_text(json.dumps({"source": self.source, "symbols": ["fixture"],
                                    "replacements": [{"before": "missing context", "after": "adapted body"}]}))
        with self.assertRaisesRegex(ValueError, "context does not match"):
            baseline.verify_reference(data, self.upstream, self.port)

    def test_reference_patch_is_applied_and_checks_proposed_context_without_writing_sources(self):
        data = self.reference_manifest()
        data.pop("adapted")
        original = baseline.git(self.upstream, "show", self.old + ":" + self.source)
        owner = self.port / "adaptation.patch"
        owner.write_text("--- a/" + self.source + "\n+++ b/" + self.source +
                         "\n@@ -1 +1 @@\n-source body\n+adapted body\n")
        data["adapted_edits"] = {self.source: "adaptation.patch"}
        data["symbols"] = {self.source: ["fixture"]}
        checkout_source = self.port / self.source
        checkout_source.parent.mkdir(parents=True)
        checkout_source.write_bytes(original)
        with patch.dict(os.environ, {"GIT_DIR": str(self.port / ".git"),
                                     "GIT_WORK_TREE": str(self.port)}), \
                patch.object(tempfile, "tempdir", str(self.port)):
            adapted = baseline.reference_edited_source(data, self.source, original, self.port)
        self.assertEqual(adapted, original.replace(b"source body", b"adapted body"))
        self.assertEqual(checkout_source.read_bytes(), original)
        baseline.verify_reference(data, self.upstream, self.port)
        report = baseline.review_reference(data, self.upstream, self.old, [], self.port)
        self.assertIn("+adapted body", report.splitlines())
        self.assertEqual(baseline.git(self.upstream, "show", self.old + ":" + self.source), original)
        (self.upstream / self.source).write_text("changed upstream context\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "change context")
        proposed = git(self.upstream, "rev-parse", "HEAD")
        report = baseline.review_reference(data, self.upstream, proposed, [], self.port)
        self.assertIn("Recorded edits require review:", report)
        self.assertEqual((self.upstream / self.source).read_text(), "changed upstream context\n")
        owner.write_text(owner.read_text().replace("-source body", "-missing context"))
        with self.assertRaisesRegex(ValueError, "patch context does not match"):
            baseline.verify_reference(data, self.upstream, self.port)

    def test_reference_review_reports_changes_and_deletions_without_writing_sources(self):
        data = self.reference_manifest()
        (self.upstream / self.source).write_text("new reference body\n")
        (self.upstream / "CPP/Windows/Net.cpp").unlink()
        git(self.upstream, "add", "-A")
        git(self.upstream, "commit", "-qm", "reference update")
        proposed = git(self.upstream, "rev-parse", "HEAD")
        local = self.port / ("src/" + self.source)
        before = local.read_bytes()
        report = baseline.review_reference(data, self.upstream, proposed, [], self.port)
        report_lines = report.splitlines()
        self.assertIn("-source body", report_lines)
        self.assertIn("+new reference body", report_lines)
        self.assertIn("deleted file mode", report)
        self.assertIn("+source body with Qt adapter", report_lines)
        self.assertEqual(local.read_bytes(), before)
        self.assertEqual(git(self.upstream, "status", "--porcelain"), "")
        self.assertEqual(git(self.port, "status", "--porcelain"), "")

    def test_reference_review_rejects_unrecorded_paths(self):
        with self.assertRaisesRegex(ValueError, "absent from reference baseline"):
            baseline.review_reference(self.reference_manifest(), self.upstream, self.old,
                                      ["unreviewed/file.h"], self.port)

    def test_reference_review_compares_binary_fixture_without_modifying_it(self):
        fixture = self.upstream / "fixture.lz"
        original = b"LZIP\0\xff\xa2"
        fixture.write_bytes(original)
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "binary baseline")
        revision = git(self.upstream, "rev-parse", "HEAD")
        local = self.port / "fixture.lz"
        local.write_bytes(original)
        data = {"repository": "https://example.invalid/source.git", "revision": revision,
                "files": {"fixture.lz": hashlib.sha256(original).hexdigest()},
                "adapted": {"fixture.lz": "fixture.lz"}}
        report = baseline.review_reference(data, self.upstream, revision, [], self.port)
        self.assertIn("Identical bytes.", report)
        changed = b"LZIP\0\xff\xa3"
        local.write_bytes(changed)
        report = baseline.review_reference(data, self.upstream, revision, [], self.port)
        self.assertIn("Local bytes differ.", report)
        self.assertIn(hashlib.sha256(changed).hexdigest(), report)
        self.assertEqual(local.read_bytes(), changed)

    def test_review_reports_adapted_and_exact_changes(self):
        (self.upstream / self.source).write_text("upstream revised source\n")
        (self.upstream / "CPP/Windows/Net.cpp").write_text("upstream revised vendor\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "proposal")
        new = git(self.upstream, "rev-parse", "HEAD")
        data = baseline.load_manifest(self.manifest_path)
        baseline.verify(data, self.upstream)
        report = baseline.review(data, self.upstream, new, [])
        self.assertIn(self.exact + "` (exact", report)
        self.assertIn("src/" + self.source + "` (adapted", report)
        self.assertIn("`" + self.old + "` → `" + new + "`", report)

    def test_exact_source_extractions_remain_verifiable_and_visible_in_review(self):
        original = baseline.git(self.upstream, "show", self.old + ":CPP/Windows/Net.cpp")
        extraction = {"local": [self.local], "symbols": ["FixtureCallback::GetStream"]}
        self.manifest["original_sha256"] = {self.exact: hashlib.sha256(original).hexdigest()}
        self.manifest["extracted"] = {self.exact: extraction}
        self.manifest_path.write_text(json.dumps(self.manifest))
        data = baseline.load_manifest(self.manifest_path)
        baseline.verify(data, self.upstream)
        (self.port / self.local).write_text("refactored local projection\n")
        baseline.verify(data, self.upstream)
        (self.upstream / "CPP/Windows/Net.cpp").write_text("upstream revised vendor\n")
        git(self.upstream, "commit", "-qam", "proposal")
        proposed = git(self.upstream, "rev-parse", "HEAD")
        report = baseline.review(data, self.upstream, proposed, [self.exact])
        self.assertIn(self.local, report)
        self.assertIn(extraction["symbols"][0], report)
        self.assertEqual((self.port / self.exact).read_bytes(), original)

    def test_source_move_can_be_verified_before_staging(self):
        destination = "src/CPP/Windows/Net.cpp"
        (self.port / destination).parent.mkdir(parents=True)
        (self.port / self.exact).rename(self.port / destination)
        self.manifest["exact"] = []
        self.manifest["adapted"] = sorted([*self.manifest["adapted"], destination])
        self.manifest_path.write_text(json.dumps(self.manifest))
        data = baseline.load_manifest(self.manifest_path)
        baseline.verify(data, self.upstream)
        # New worktree files still require provenance.
        data["adapted"].remove(destination)
        with self.assertRaisesRegex(ValueError, "Inventory mismatch: unrecorded="):
            baseline.verify(data, self.upstream)

    def test_module_owner_can_move_without_changing_its_original_baseline(self):
        old_owner = "src/" + self.source
        owner = "src/modules/network/folder.cpp"
        (self.port / owner).parent.mkdir(parents=True)
        (self.port / old_owner).rename(self.port / owner)
        self.manifest.update(schema=2, adapted=[owner],
                             origins={self.exact: "CPP/Windows/Net.cpp", owner: self.source},
                             inventory_roots=["src", "third_party/7zip"])
        self.manifest_path.write_text(json.dumps(self.manifest))
        data = baseline.load_manifest(self.manifest_path)
        baseline.verify(data, self.upstream)
        (self.upstream / self.source).write_text("updated original\n")
        git(self.upstream, "commit", "-qam", "update")
        proposed = git(self.upstream, "rev-parse", "HEAD")
        report = baseline.review(data, self.upstream, proposed, [owner])
        self.assertIn(owner, report)
        self.assertEqual((self.port / owner).read_text(), "source body with Qt adapter\n")

    def compile_source_view(self, source, build, includes=()):
        if not shutil.which("cmake") or not shutil.which("ninja"):
            self.skipTest("CMake/Ninja unavailable")
        # Keep fixture sources inside the CMake project so object/depfile names
        # are relative to it, including in branch workspaces with escaped names.
        project = build.parent
        project.mkdir(exist_ok=True)
        paths = " ".join('"' + str(path).replace('\\', '/') + '"' for path in includes)
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(SourceView LANGUAGES C CXX)\n'
            'add_executable(probe "' + str(source).replace('\\', '/') + '")\n'
            'target_include_directories(probe PRIVATE ' + paths + ')\n')
        command = ["cmake", "-G", "Ninja", "-S", str(project), "-B", str(build)]
        if cache := shutil.which("ccache"):
            command.append("-DCMAKE_CXX_COMPILER_LAUNCHER=" + cache)
            command.append("-DCMAKE_C_COMPILER_LAUNCHER=" + cache)
        for invocation in (command, ["cmake", "--build", str(build)]):
            result = subprocess.run(invocation, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.source_view_log = result.stdout + result.stderr
        return subprocess.run([str(build / ("probe.exe" if os.name == "nt" else "probe"))]).returncode

    def test_native_view_rebases_module_headers_and_retains_selected_owner_scope(self):
        original = "CPP/7zip/Archive/Probe.cpp"
        header = "CPP/7zip/Archive/Probe.h"
        owner, header_owner = "src/reader/probe.cpp", "src/reader/probe.h"
        for path, content in [(original, '#include "Probe.h"\nint main() { return value(); }\n'),
                              (header, 'inline int value() { return 17; }\n')]:
            target = self.upstream / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "native view fixture")
        (self.port / owner).parent.mkdir(parents=True)
        (self.port / owner).write_text('#include "reader/probe.h"\nint main() { return value(); }\n')
        (self.port / header_owner).write_text('inline int value() { return 43; }\n')
        data = dict(self.manifest, schema=2, revision=git(self.upstream, "rev-parse", "HEAD"),
                    origins={owner: original, header_owner: header},
                    adapted=[owner, header_owner], runtime_adapted=[owner])
        source = pathlib.Path(self.temporary.name) / "native-view"
        build = pathlib.Path(self.temporary.name) / "native-view-build"
        baseline.prepare_runtime_snapshot(data, source, self.upstream)
        self.assertEqual(self.compile_source_view(source / original, build), 17)
        data["runtime_adapted"].append(header_owner)
        baseline.prepare_runtime_snapshot(data, source, self.upstream)
        self.assertEqual(self.compile_source_view(source / original, build), 43, self.source_view_log)

    def test_foreign_header_spelling_reads_the_live_project_module_header(self):
        owner = "src/reader/probe.h"
        (self.port / owner).parent.mkdir(parents=True)
        (self.port / owner).write_text('inline int value() { return 17; }\n')
        data = dict(self.manifest, origins={owner: "CPP/Foreign/Probe.h"}, adapted=[owner])
        compatibility = pathlib.Path(self.temporary.name) / "header-view"
        baseline.prepare_header_snapshot(data, compatibility)
        source = pathlib.Path(self.temporary.name) / "probe.cpp"
        source.write_text('#include "Foreign/Probe.h"\nint main() { return value(); }\n')
        build = pathlib.Path(self.temporary.name) / "header-view-build"
        includes = [compatibility / "CPP", self.port / "src"]
        self.assertEqual(self.compile_source_view(source, build, includes), 17)
        (self.port / owner).write_text('inline int value() { return 43; }\n')
        # Ordinary compiler dependencies must see edits without regenerating
        # or duplicating a module's implementation in the compatibility tree.
        self.assertEqual(self.compile_source_view(source, build, includes), 43, self.source_view_log)

    def dependency_archive(self):
        files = {"include/base.h": b"#define BASE 17\n",
                 "lib/decoder.c": b'#include "base.h"\nint decode(void) { return BASE; }\n'}
        packed = self.port / "third_party/probe/source.tar.xz"
        packed.parent.mkdir(parents=True)
        with tarfile.open(packed, "w:xz") as archive:
            for name, content in files.items():
                entry = tarfile.TarInfo(name)
                entry.size = len(content)
                archive.addfile(entry, io.BytesIO(content))
        data = {"dependency_archives": {"probe": {
            "local": packed.relative_to(self.port).as_posix(),
            "sha256": hashlib.sha256(packed.read_bytes()).hexdigest(),
            "files": {name: hashlib.sha256(content).hexdigest() for name, content in files.items()},
            "overlays": {"lib/client.c": "src/probe/client.c"}}}}
        owner = self.port / "src/probe/client.c"
        owner.parent.mkdir(parents=True)
        owner.write_text('#include "decoder.c"\nint main(void) { return decode(); }\n')
        return packed, owner, data

    def test_archived_dependency_build_tracks_live_c_adaptation(self):
        _, owner, data = self.dependency_archive()
        source = pathlib.Path(self.temporary.name) / "dependencies"
        inputs = baseline.SourceArchives(self.port, data)
        inputs.prepare(source)
        build = pathlib.Path(self.temporary.name) / "dependency-build"
        includes = [source / "probe/include", source / "probe/lib"]
        self.assertEqual(self.compile_source_view(source / "probe/lib/client.c", build, includes), 17)
        before = (source / "probe/lib/decoder.c").stat().st_mtime_ns
        owner.write_text('#include "decoder.c"\nint main(void) { return decode() + 26; }\n')
        self.assertEqual(self.compile_source_view(source / "probe/lib/client.c", build, includes), 43)
        inputs.prepare(source)
        self.assertEqual((source / "probe/lib/decoder.c").stat().st_mtime_ns, before)

    def test_dependency_archive_corruption_and_linked_output_are_rejected(self):
        packed, _, data = self.dependency_archive()
        source = pathlib.Path(self.temporary.name) / "dependencies"
        outside = pathlib.Path(self.temporary.name) / "outside"
        source.mkdir()
        outside.mkdir()
        try:
            (source / "probe").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("Creating directory symlinks is unavailable")
        with self.assertRaisesRegex(ValueError, "linked dependency"):
            baseline.SourceArchives(self.port, data).prepare(source)
        self.assertEqual(list(outside.iterdir()), [])
        (source / "probe").unlink()
        packed.write_bytes(packed.read_bytes()[:-5] + b"wrong")
        with self.assertRaisesRegex(ValueError, "archive differs"):
            baseline.SourceArchives(self.port, data).prepare(source)

    def test_runtime_uses_local_originals_without_an_external_checkout(self):
        originals = {}
        for source in (self.source, "CPP/Windows/Net.cpp"):
            content = baseline.git(self.upstream, "show", self.old + ":" + source)
            local = "third_party/7zip/" + source
            target = self.port / local
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            originals[source] = {"local": local, "sha256": hashlib.sha256(content).hexdigest()}
        self.upstream.rename(self.upstream.with_name("unavailable-upstream"))
        self.manifest.update(originals=originals, runtime_adapted=["src/" + self.source])
        snapshot = pathlib.Path(self.temporary.name) / "offline-runtime"
        baseline.prepare_runtime_snapshot(self.manifest, snapshot)
        self.assertEqual((snapshot / self.source).read_bytes(),
                         (self.port / ("src/" + self.source)).read_bytes())
        self.assertEqual((snapshot / "CPP/Windows/Net.cpp").read_bytes(), b"exact body\n")
        (self.port / self.exact).write_bytes(b"unreviewed dependency edit\n")
        with self.assertRaisesRegex(ValueError, "differs from baseline"):
            baseline.prepare_runtime_snapshot(self.manifest, snapshot)

    def test_archived_originals_expand_offline_and_preserve_incremental_outputs(self):
        packed = self.port / "third_party/7zip/source.tar.xz"
        originals = {}
        with tarfile.open(packed, "w:xz") as archive:
            for source in (self.source, "CPP/Windows/Net.cpp"):
                original = self.upstream / source
                archive.add(original, arcname=source)
                originals[source] = {"sha256": hashlib.sha256(original.read_bytes()).hexdigest()}
        data = dict(self.manifest, originals=originals,
                    original_archive={"local": packed.relative_to(self.port).as_posix(),
                                      "sha256": hashlib.sha256(packed.read_bytes()).hexdigest()},
                    runtime_adapted=["src/" + self.source])
        self.upstream.rename(self.upstream.with_name("unavailable-upstream"))
        (self.port / self.exact).unlink()
        snapshot = pathlib.Path(self.temporary.name) / "archived-runtime"
        baseline.prepare_runtime_snapshot(data, snapshot)
        self.assertEqual((snapshot / self.source).read_text(), "source body with Qt adapter\n")
        original = snapshot / "CPP/Windows/Net.cpp"
        self.assertEqual(original.read_bytes(), b"exact body\n")
        before = original.stat().st_mtime_ns
        output = snapshot / "cached.o"
        output.write_bytes(b"retained compiler output")
        baseline.prepare_runtime_snapshot(data, snapshot)
        self.assertEqual(original.stat().st_mtime_ns, before)
        self.assertEqual(output.read_bytes(), b"retained compiler output")
        packed.write_bytes(b"corrupt source archive")
        with self.assertRaisesRegex(ValueError, "archive differs from baseline"):
            baseline.prepare_runtime_snapshot(data, snapshot)

    def test_snapshot_refuses_a_linked_parent_without_writing_outside(self):
        snapshot = pathlib.Path(self.temporary.name) / "runtime"
        baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        outside = pathlib.Path(self.temporary.name) / "outside"
        outside.mkdir()
        shutil.rmtree(snapshot / "CPP")
        try:
            (snapshot / "CPP").symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Native symlink unavailable: {error}")
        with self.assertRaisesRegex(ValueError, "linked runtime source"):
            baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        self.assertEqual(list(outside.iterdir()), [])

    def test_runtime_snapshot_updates_selected_sources_and_retains_incremental_outputs(self):
        snapshot = pathlib.Path(self.temporary.name) / "runtime"
        self.manifest["runtime_adapted"] = ["src/" + self.source]
        baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        adapted = snapshot / self.source
        self.assertEqual(adapted.read_bytes(), (self.port / ("src/" + self.source)).read_bytes())
        self.assertEqual((snapshot / "CPP/Windows/Net.cpp").read_bytes(), b"exact body\n")
        compiled = snapshot / "CPP/7zip/output.o"
        compiled.write_bytes(b"cached compiler output")
        before = adapted.stat().st_mtime_ns
        baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        self.assertEqual(adapted.stat().st_mtime_ns, before)
        (self.port / ("src/" + self.source)).write_bytes(b"new adapted input")
        baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        self.assertEqual(adapted.read_bytes(), b"new adapted input")
        self.assertEqual(compiled.read_bytes(), b"cached compiler output")

    def test_original_snapshot_ignores_format_support_and_adaptations(self):
        snapshot = pathlib.Path(self.temporary.name) / "original-runtime"
        self.manifest["runtime_adapted"] = ["src/" + self.source]
        self.manifest["runtime_dependencies"] = [self.local]
        self.manifest["runtime_format_sources"] = [self.local.replace(".h", ".cpp")]
        self.manifest_path.write_text(json.dumps(self.manifest))
        with patch.object(sys, "argv", ["upstream_baseline.py", "runtime-snapshot",
                "--original-only", "--manifest", str(self.manifest_path),
                "--snapshot", str(snapshot), "--upstream", str(self.upstream)]):
            self.assertEqual(baseline.main(), 0)
        original = subprocess.check_output(
            ["git", "-C", str(self.upstream), "show", self.old + ":" + self.source])
        self.assertEqual((snapshot / self.source).read_bytes(), original)
        self.assertFalse((snapshot / self.local.removeprefix("src/")).exists())


    def test_runtime_snapshot_refuses_unowned_directory(self):
        snapshot = pathlib.Path(self.temporary.name) / "runtime"
        snapshot.mkdir()
        private = snapshot / "user-file"
        private.write_bytes(b"user data")
        with self.assertRaisesRegex(ValueError, "unowned"):
            baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        self.assertEqual(private.read_bytes(), b"user data")

    def test_original_snapshot_excludes_engine_adaptations_and_retains_outputs(self):
        snapshot = pathlib.Path(self.temporary.name) / "sfx"
        self.manifest["runtime_adapted"] = ["src/" + self.source]
        self.manifest_path.write_text(json.dumps(self.manifest))
        command = [sys.executable, str(SCRIPT), "runtime-snapshot", "--original-only",
                   "--manifest", str(self.manifest_path), "--upstream", str(self.upstream),
                   "--snapshot", str(snapshot)]
        subprocess.run(command, check=True)
        self.assertEqual((snapshot / self.source).read_bytes(),
                         baseline.git(self.upstream, "show", self.old + ":" + self.source))
        compiled = snapshot / "cached.o"
        compiled.write_bytes(b"incremental SFX output")
        before = (snapshot / self.source).stat().st_mtime_ns
        subprocess.run(command, check=True)
        self.assertEqual((snapshot / self.source).stat().st_mtime_ns, before)
        self.assertEqual(compiled.read_bytes(), b"incremental SFX output")

    def test_reference_snapshot_preserves_blob_bytes_with_checkout_line_endings(self):
        (self.upstream / ".gitattributes").write_text("*.cpp text eol=crlf\n")
        git(self.upstream, "add", ".gitattributes")
        git(self.upstream, "commit", "-qm", "checkout attributes")
        data = dict(self.manifest, revision=git(self.upstream, "rev-parse", "HEAD"))
        snapshot = pathlib.Path(self.temporary.name) / "original-blob-source"
        baseline.prepare_runtime_snapshot(data, snapshot, self.upstream)
        expected = baseline.git(self.upstream, "show", data["revision"] + ":" + self.source)
        self.assertEqual((snapshot / self.source).read_bytes(), expected)

    def test_reference_snapshot_preserves_original_make_rules_and_incremental_output(self):
        original = b"compile: input.c\n\t$(CC) $**\n"
        (self.upstream / "CPP/Build.mak").write_bytes(original)
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "reference build rules")
        self.manifest["revision"] = git(self.upstream, "rev-parse", "HEAD")
        self.manifest_path.write_text(json.dumps(self.manifest))
        snapshot = pathlib.Path(self.temporary.name) / "reference"
        command = [sys.executable, str(SCRIPT), "runtime-snapshot", "--original-only", "--no-build-rules",
                   "--manifest", str(self.manifest_path), "--upstream", str(self.upstream),
                   "--snapshot", str(snapshot)]
        subprocess.run(command, check=True)
        self.assertEqual((snapshot / "CPP/Build.mak").read_bytes(), original)
        before = (snapshot / "CPP/Build.mak").stat().st_mtime_ns
        output = snapshot / "cached.obj"
        output.write_bytes(b"retained reference object")
        subprocess.run(command, check=True)
        self.assertEqual((snapshot / "CPP/Build.mak").stat().st_mtime_ns, before)
        self.assertEqual(output.read_bytes(), b"retained reference object")

    def test_runtime_snapshot_applies_partial_exact_update(self):
        snapshot = pathlib.Path(self.temporary.name) / "runtime"
        baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        compiled = snapshot / "cached.o"
        compiled.write_bytes(b"retain incremental output")
        (self.upstream / "CPP/Windows/Net.cpp").write_bytes(b"updated upstream dependency\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "partial update")
        revision = git(self.upstream, "rev-parse", "HEAD")
        self.manifest["overrides"] = {self.exact: revision}
        (self.port / self.exact).write_bytes(b"updated upstream dependency\n")
        self.manifest["original_sha256"] = {
            self.exact: hashlib.sha256(b"updated upstream dependency\n").hexdigest()}
        baseline.verify(self.manifest, self.upstream)
        baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
        self.assertEqual((snapshot / "CPP/Windows/Net.cpp").read_bytes(),
                         b"updated upstream dependency\n")
        self.assertEqual(compiled.read_bytes(), b"retain incremental output")
        self.manifest["original_sha256"][self.exact] = "0" * 64
        with self.assertRaisesRegex(ValueError, "Original source hash mismatch"):
            baseline.verify(self.manifest, self.upstream)

    def test_duplicate_source_owner_is_rejected_across_roots(self):
        sdk = "src/" + self.source
        gpl = "src/gpl/" + self.source
        (self.port / gpl).parent.mkdir(parents=True)
        (self.port / gpl).write_bytes(b"Linux reader variant\n")
        self.manifest["adapted"] = sorted([sdk, gpl])
        self.manifest_path.write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, "multiple authoritative implementations"):
            baseline.load_manifest(self.manifest_path)

    def test_runtime_rebuilds_transitive_adapted_header_consumers(self):
        self.assert_runtime_rebuilds_header_consumers(
            "CPP/7zip/Common/Model.h",
            (("struct Model { char value; };\n", 1), ("struct Model { long long value; };\n", 8)),
            "sizeof(Model)", adapted=True)

    def test_runtime_rebuilds_native_version_after_baseline_change(self):
        self.assert_runtime_rebuilds_header_consumers(
            "C/7zVersion.h", (("#define MY_VER_MINOR 5\n", 5), ("#define MY_VER_MINOR 4\n", 4)),
            "MY_VER_MINOR", adapted=False)

    def test_sfx_rebuilds_native_version_after_baseline_change(self):
        self.assert_runtime_rebuilds_header_consumers(
            "C/7zVersion.h", (("#define MY_VER_MINOR 4\n", 4), ("#define MY_VER_MINOR 5\n", 5)),
            "MY_VER_MINOR", adapted=False,
            directory="CPP/7zip/Bundles/" + ("SFXWin" if sys.platform == "win32" else "SFXCon"))

    def test_runtime_rebuilds_external_header_consumers(self):
        self.assert_runtime_rebuilds_header_consumers(
            "block/qcow2.h", (("#define DISK_VALUE 3\n", 3), ("#define DISK_VALUE 7\n", 7)),
            "DISK_VALUE", adapted=False, dependency=True)

    def test_runtime_links_c_reference_and_rebuilds_after_header_change(self):
        self.assert_runtime_rebuilds_header_consumers(
            "block/reader.h", (("#define DISK_VALUE 3\n", 3), ("#define DISK_VALUE 7\n", 7)),
            "reference_value()", adapted=False, dependency=True, c_reference=True)

    def test_runtime_rebuilds_cpp_dependencies(self):
        self.assert_runtime_rebuilds_header_consumers(
            "block/reader.h", (("#define DISK_VALUE 3\n", 3), ("#define DISK_VALUE 7\n", 7)),
            "reference_value()", adapted=False, dependency=True, cpp_reference=True)

    def test_runtime_links_vendored_c_and_rebuilds_after_header_change(self):
        self.assert_runtime_rebuilds_header_consumers(
            "block/reader.h", (("#define DISK_VALUE 3\n", 3), ("#define DISK_VALUE 7\n", 7)),
            "reference_value()", adapted=False, dependency=True, c_reference=True,
            dependency_root="third_party")

    def test_format_support_links_and_rebuilds_without_linking_into_console(self):
        self.assert_runtime_rebuilds_header_consumers(
            "block/reader.h", (("#define DISK_VALUE 3\n", 3), ("#define DISK_VALUE 7\n", 7)),
            "reference_value()", adapted=False, dependency=True, cpp_reference=True,
            directory="CPP/7zip/Bundles/Format7zF", format_only=True)

    def assert_runtime_rebuilds_header_consumers(self, header, versions, expression, adapted,
                                               dependency=False, c_reference=False,
                                               cpp_reference=False,
                                               directory="CPP/7zip/UI/Console", format_only=False,
                                               dependency_root="src"):
        include = "Model.h" if adapted else "../../../" + header
        files = {
            directory + "/Probe.cpp": '#include "../../Common/Bridge.h"\nint main() { return ' + expression + '; }\n',
            "CPP/7zip/Common/Bridge.h": '#include "' + include + '"\n',
            header: versions[0][0],
        }
        if c_reference:
            files["CPP/7zip/Common/Bridge.h"] += 'extern "C" int reference_value(void);\n'
        if cpp_reference:
            files["CPP/7zip/Common/Bridge.h"] += 'int reference_value();\n'
        windows = sys.platform == "win32"
        cache = shutil.which("ccache")
        self.assertIsNotNone(cache, "Runtime compilation must use the existing compiler cache")
        compiler = '"' + cache + '" ' + ("cl /nologo" if windows else "c++")
        if windows:
            makefile = ('O=objects\nPROGPATH=$(O)\\probe.exe\nOBJS=$(O)\\Probe.obj\n'
                        f'CC={compiler}\nCFLAGS_O2=/c\nall: $(PROGPATH)\n'
                        '$(PROGPATH): $(OBJS)\n'
                        f'\t{compiler} /Fe$@ $(OBJS) $(LIBS)\n'
                        '$(O)\\Probe.obj: Probe.cpp\n'
                        f'\t{compiler} /c /Fo$@ Probe.cpp\n')
        else:
            makefile = ('O=objects\nPROGPATH=$(O)/probe\nPROGPATH_STATIC=$(O)/probes\n'
                        'OBJS=$(O)/Probe.o\n'
                        f'CC="{cache}" cc\nCFLAGS=-c -o $@\nCXX={compiler}\nCXXFLAGS=-c -o $@\nall: $(PROGPATH)\n'
                        '$(PROGPATH): $(OBJS)\n'
                        f'\t{compiler} -o $@ $(OBJS)\n'
                        '$(O)/Probe.o: Probe.cpp\n'
                        f'\t{compiler} -c -o $@ $<\n')
        files[directory + ("/makefile" if windows else "/makefile.gcc")] = makefile
        if format_only:
            files[directory + "/Probe.cpp"] += '\nint format_anchor() { return 0; }\n'
            console = "CPP/7zip/UI/Console"
            files[console + "/Probe.cpp"] = 'int main() { return 0; }\n'
            files[console + ("/makefile" if windows else "/makefile.gcc")] = makefile
        for name, contents in files.items():
            if dependency and name == header:
                continue
            path = self.upstream / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "native dependency fixture")
        self.manifest["revision"] = git(self.upstream, "rev-parse", "HEAD")
        local = dependency_root + "/" + header
        if adapted or dependency:
            (self.port / local).parent.mkdir(parents=True, exist_ok=True)
            (self.port / local).write_text(files[header])
            if adapted:
                self.manifest["adapted"] = sorted([*self.manifest["adapted"], local])
            else:
                self.manifest["runtime_dependencies"] = [local]
                if c_reference:
                    source = self.port / dependency_root / "block/reader.c"
                    source.write_text('#include "reader.h"\nint reference_value(void) { return DISK_VALUE; }\n')
                    self.manifest["runtime_dependencies"] = sorted([local, dependency_root + "/block/reader.c"])
                if cpp_reference:
                    source = self.port / "src/block/reader.cpp"
                    source.write_text('#include "reader.h"\nint reference_value() { return static_cast<int>(DISK_VALUE); }\n')
                    self.manifest["runtime_dependencies"] = sorted([local, "src/block/reader.cpp"])
                    if format_only:
                        self.manifest["runtime_format_sources"] = ["src/block/reader.cpp"]
                        source.write_text('#include "reader.h"\nint format_anchor();\n'
                            'int reference_value() { return DISK_VALUE + format_anchor(); }\n')
        self.manifest["runtime_adapted"] = [local] if adapted else []
        snapshot = pathlib.Path(self.temporary.name) / "runtime"
        environment = dict(os.environ)
        if sys.platform == "darwin":
            environment.setdefault("CCACHE_DIR", "/private/tmp/sevenzip-desktop-ccache")
        command = ["nmake", "/nologo", "/f", "sevenzip-runtime.mak"] if windows else [
            "make", "-f", "sevenzip-runtime.gcc.mak"]
        for iteration, (contents, expected) in enumerate(versions):
            # Apple GNU make 3.81 and nmake compare whole-second timestamps.
            if iteration:
                time.sleep(1.1)
            if adapted or dependency:
                (self.port / local).write_text(contents)
            elif iteration:
                (self.upstream / header).write_text(contents)
                git(self.upstream, "add", ".")
                git(self.upstream, "commit", "-qm", "change native version")
                self.manifest["revision"] = git(self.upstream, "rev-parse", "HEAD")
            baseline.prepare_runtime_snapshot(self.manifest, snapshot, self.upstream)
            bundle = snapshot / directory
            (bundle / "objects").mkdir(exist_ok=True)
            compiled = subprocess.run(command, cwd=bundle, env=environment, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executable = bundle / "objects" / ("probe.exe" if windows else "probe")
            checked = executable
            if windows:
                # Keep the relink target out of Windows' executable image locks.
                checked = executable.with_name("probe-checked-" + str(expected) + ".exe")
                shutil.copy2(executable, checked)
            self.assertEqual(subprocess.run([str(checked)]).returncode, expected)
            if format_only:
                console_bundle = snapshot / "CPP/7zip/UI/Console"
                (console_bundle / "objects").mkdir(exist_ok=True)
                console_build = subprocess.run(command, cwd=console_bundle, env=environment,
                                               capture_output=True, text=True)
                self.assertEqual(console_build.returncode, 0,
                                 console_build.stdout + console_build.stderr)


if __name__ == "__main__":
    unittest.main()
