"""Runtime package boundaries and release version behavior, with local fixtures."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"scripts/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load_script("package")
release = load_script("release")


class PackageTests(unittest.TestCase):
    def test_notice_index_follows_deployed_documents_without_changing_text(self):
        with tempfile.TemporaryDirectory(prefix="notices 空 ") as temporary:
            runtime = Path(temporary)
            root = runtime / "share/licenses/sevenzip-desktop"
            root.mkdir(parents=True)
            binary = runtime / "bin/sevenzip_desktop"
            binary.parent.mkdir()
            binary.write_bytes(b"runtime")
            payload = {"THIRD_PARTY_NOTICES.md": "overview δ\n",
                       "7zip/LICENSE.txt": "original <text>\n" * 100}
            for name, text in payload.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            obsolete = root / "Qt/include/stale.txt"
            obsolete.parent.mkdir(parents=True)
            obsolete.write_bytes(b"development-only payload")
            package.write_notice_index(root)
            # Qt and other deployment owners add their documents later.
            extra = root / "Qt/LGPL.txt"
            extra.parent.mkdir(exist_ok=True)
            extra.write_text("Qt original text\n", encoding="utf-8")
            payload["Qt/LGPL.txt"] = "Qt original text\n"
            package.finalize_runtime(runtime, "Windows" if os.name == "nt" else "Linux")
            index = json.loads((root / "index.json").read_text(encoding="utf-8"))
            actual = {row["path"]: (root / row["path"]).read_text(encoding="utf-8")
                      for row in index["documents"]}
            self.assertEqual(actual, payload)
            self.assertEqual(len(index["documents"]), len(payload))
            self.assertEqual(index["documents"][0]["path"], "THIRD_PARTY_NOTICES.md")

    def test_notice_index_rejects_empty_or_escaping_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "Notices"
            root.mkdir()
            with self.assertRaises(ValueError):
                package.write_notice_index(root)
            outside = Path(temporary) / "outside.txt"
            outside.write_text("not a deployed license")
            try:
                (root / "external.txt").symlink_to(outside)
            except OSError:
                self.skipTest("Symbolic links unavailable on this host")
            with self.assertRaises(ValueError):
                package.write_notice_index(root)

    def test_local_install_replaces_old_dependencies_only_after_success(self):
        with tempfile.TemporaryDirectory(prefix="install 空 ") as temporary:
            install = Path(temporary) / "dist"
            install.mkdir()
            (install / "app").write_bytes(b"old working application")
            (install / "old-qt-dependency").write_bytes(b"obsolete runtime")

            def install_fixture(command, **kwargs):
                runtime = Path(command[command.index("--prefix") + 1])
                runtime.mkdir()
                (runtime / "app").write_bytes(b"new working application")

            with patch.object(package.subprocess, "run", side_effect=install_fixture):
                with patch.object(package, "finalize_runtime", side_effect=ValueError("bad runtime")):
                    with self.assertRaises(ValueError):
                        package.install_local_runtime(Path("build"), install, "Darwin")
                self.assertEqual((install / "app").read_bytes(), b"old working application")
                self.assertEqual((install / "old-qt-dependency").read_bytes(), b"obsolete runtime")
                with patch.object(package, "finalize_runtime"):
                    package.install_local_runtime(Path("build"), install, "Darwin")
            self.assertEqual((install / "app").read_bytes(), b"new working application")
            self.assertEqual(sum(path.stat().st_size for path in install.rglob("*")
                                 if path.is_file()), len(b"new working application"))

    @unittest.skipIf(os.name == "nt", "Unix entry aliases")
    def test_compact_runtime_preserves_named_entries_without_duplicate_payload(self):
        with tempfile.TemporaryDirectory(prefix="runtime 空 ") as temporary:
            root = Path(temporary)
            binary = root / "bin/sevenzip_desktop"
            binary.parent.mkdir()
            binary.write_text('#!/bin/sh\nbasename "$0"\n')
            binary.chmod(0o755)
            notices = root / "share/licenses/sevenzip-desktop"
            notices.mkdir(parents=True)
            (notices / "LICENSE").write_text("original license\n")
            for alias in ("7zFM", "7zG"):
                shutil.copy2(binary, binary.parent / alias)
            for _ in range(2):
                package.finalize_runtime(root, "Linux")
                for alias in ("7zFM", "7zG"):
                    self.assertEqual(subprocess.check_output([str(binary.parent / alias)], text=True),
                                     alias + "\n")
                payload_bytes = sum(path.stat().st_size for path in binary.parent.rglob("*")
                                    if path.is_file() and not path.is_symlink())
                self.assertEqual(payload_bytes, binary.stat().st_size)

    def test_windows_copies_transitive_crt_only_without_installer(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "package"
            runtime = Path(temporary) / "crt"
            (root / "bin").mkdir(parents=True)
            runtime.mkdir()
            (root / "bin/app.exe").write_bytes(b"app")
            (root / "bin/vc_redist.x64.exe").write_bytes(b"installer")
            for name in ("msvcp140.dll", "vcruntime140.dll", "unused.dll"):
                (runtime / name).write_bytes(name.encode())
            dependencies = {"app.exe": ["msvcp140.dll", "kernel32.dll"],
                            "msvcp140.dll": ["vcruntime140.dll"]}
            with patch.object(package, "imports", side_effect=lambda path: dependencies.get(path.name, [])):
                package.deploy_crt(root, runtime)
            self.assertEqual(sorted(path.name for path in (root / "bin").iterdir()),
                             ["app.exe", "msvcp140.dll", "vcruntime140.dll"])

    def test_rejects_sources_build_metadata_and_nested_archives(self):
        with tempfile.TemporaryDirectory(prefix="package 空 ") as temporary:
            root = Path(temporary)
            for name in ("code.cpp", "Source/source.txt", "CMakeCache.txt", "source.tar.xz"):
                with self.subTest(name=name):
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("fixture")
                    with self.assertRaises(ValueError):
                        package.validate_payload(root)
                    path.unlink()
                    if path.parent != root:
                        path.parent.rmdir()

    def test_prunes_development_preserves_runtime_notices(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("Headers/qt.h", "runtime.dylib", "Notices/LICENSE.txt",
                         "libqoffscreen.dylib", "imageformats/libqjpeg.dylib",
                         "imageformats/libqico.dylib"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture")
            package.prune_development(root)
            package.validate_payload(root)
            self.assertEqual(sorted(path.relative_to(root).as_posix() for path in root.rglob("*")
                                    if path.is_file()), ["Notices/LICENSE.txt",
                                                         "imageformats/libqico.dylib", "runtime.dylib"])

    def test_rejects_escaping_symlink_preserves_alias(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "runtime"
            root.mkdir()
            (root / "app").write_bytes(b"runtime")
            try:
                (root / "alias").symlink_to("app")
            except OSError:
                self.skipTest("Native symbolic links are unavailable")
            package.validate_payload(root)
            (root / "escape").symlink_to("../outside")
            with self.assertRaises(ValueError):
                package.validate_payload(root)

    def test_qt_archive_extracts_only_licenses_and_checks_version_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "qtbase-everywhere-src-6.12.0.tar.xz"
            with tarfile.open(archive, "w:xz") as stream:
                for name in ("LICENSES/LGPL-3.0-only.txt", "src/code.cpp"):
                    member = tarfile.TarInfo("qtbase-everywhere-src-6.12.0/" + name)
                    member.size = 7
                    stream.addfile(member, io.BytesIO(b"fixture"))
            checksum = root / "checksum"
            checksum.write_text(hashlib.sha256(archive.read_bytes()).hexdigest())
            output = root / "notices"
            package.qt_notices(archive, checksum, "6.12.0", output)
            self.assertTrue((output / "LICENSES/LGPL-3.0-only.txt").is_file())
            self.assertFalse((output / "src").exists())
            with self.assertRaises(ValueError):
                package.qt_notices(archive, checksum, "6.11.2", output)
            checksum.write_text("0" * 64)
            with self.assertRaises(ValueError):
                package.qt_notices(archive, checksum, "6.12.0", output)

    def test_release_versions_and_tag_match_without_git_writes(self):
        for choice, expected in (("patch", "0.1.1"), ("minor", "0.2.0"),
                                 ("major", "1.0.0"), ("1.2.3", "1.2.3")):
            self.assertEqual(release.next_version("0.1.0", choice), expected)
        for choice in ("0.1.0", "0.0.1", "01.2.3", "nonsense"):
            with self.assertRaises(ValueError):
                release.next_version("0.1.0", choice)
        release.check_tag("v" + release.current_version())
        with self.assertRaises(ValueError):
            release.check_tag("v999.0.0")


if __name__ == "__main__":
    unittest.main()
