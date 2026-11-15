"""Actual main/AppKit launch: one document window, ordinary launch and CLI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import uuid


def run(args):
    engine = shutil.which("7z")
    if not engine:
        raise RuntimeError("The official 7z fixture engine is required")
    with tempfile.TemporaryDirectory(prefix="sevenzip-native-startup-") as temporary:
        root = Path(temporary)
        extension = "sevenzip-fixture-" + uuid.uuid4().hex
        application = root / "Native startup 空.app"
        binary = application / "Contents/MacOS/sevenzip_desktop"
        binary.parent.mkdir(parents=True)
        shutil.copy2(args.probe, binary)
        info = {
            "CFBundleIdentifier": "org.sevenzip-desktop.fixture." + uuid.uuid4().hex,
            "CFBundleName": "Sevenzip native startup fixture",
            "CFBundleExecutable": binary.name,
            "CFBundlePackageType": "APPL",
            "CFBundleDocumentTypes": [{"CFBundleTypeExtensions": [extension],
                                       "CFBundleTypeName": "Owned archive fixture",
                                       "CFBundleTypeRole": "Viewer",
                                       "LSHandlerRank": "Alternate"}],
        }
        (application / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
        subprocess.run(["codesign", "--force", "--sign", "-", str(application)], check=True)
        member = root / "проверка 空 with spaces.txt"
        payload = b"native launch bytes\x00\xff"
        member.write_bytes(payload)
        archive = root / ("архив 空 with spaces." + extension)
        subprocess.run([engine, "a", "-tzip", str(archive), member.name], cwd=root,
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        digest = hashlib.sha256(archive.read_bytes()).digest()
        folder = root / "folder 空 with spaces"
        folder.mkdir()
        (folder / "folder marker.txt").touch()
        external = root / "deferred 空.stream"
        lzip_fixture = Path(__file__).resolve().parents[1] / "fixtures/lzip/fox.lz"
        shutil.copyfile(lzip_fixture, external)
        unregister = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                          "LaunchServices.framework/Support/lsregister")
        try:
            for mode in ("document", "normal", "cli"):
                report = root / (mode + ".json")
                expected = member.name if mode == "document" else ""
                environment = dict(os.environ, SEVENZIP_NATIVE_STARTUP_FOLDER="",
                                   SEVENZIP_NATIVE_STARTUP_ARCHIVE="")
                if mode != "document":
                    environment["SEVENZIP_NATIVE_STARTUP_FOLDER"] = str(folder)
                if mode == "cli":
                    # Filesystem navigation still works with an unavailable engine.
                    environment.update(QT_QPA_PLATFORM="cocoa",
                                       SEVENZIP_NATIVE_STARTUP_REPORT=str(report),
                                       SEVENZIP_NATIVE_STARTUP_MEMBER="",
                                       SEVENZIP_SETTINGS_PATH=str(report) + ".ini",
                                       SEVENZIP_CODEC_MODULE_PATH=str(root / "unavailable" / "7z.so"))
                    command = [str(binary), str(root)]
                else:
                    if mode == "normal":
                        environment["SEVENZIP_NATIVE_STARTUP_ARCHIVE"] = str(external)
                    request = str(archive) if mode == "document" else "--normal"
                    command = [args.launcher, str(application), request, str(report), expected]
                subprocess.run(command, cwd=root, env=environment, check=True, timeout=25)
                actual = json.loads(report.read_text())
                assert not actual["timedOut"], actual
                assert actual["windows"] == 1, actual
                assert actual["archives"] == (1 if mode == "document" else 0), actual
                assert not actual["wrongDocumentShown"], actual
                assert actual["firstPaintMs"] >= actual["firstShowMs"] >= 0, actual
                assert actual["readyMs"] >= actual["firstPaintMs"], actual
                assert not actual["waitingAtShow"], actual
                assert not actual["waitingAtReady"], actual
                assert actual["deferredArchiveOpened"] == (mode == "normal"), actual
                assert Path(actual["executable"]).resolve() == binary.resolve(), actual
                assert hashlib.sha256(archive.read_bytes()).digest() == digest
                assert member.read_bytes() == payload
                assert external.read_bytes() == lzip_fixture.read_bytes()
                print(f"{mode}: one expected window, source bytes retained; "
                      f"first show {actual['firstShowMs']} ms, first paint {actual['firstPaintMs']} ms, "
                      f"ready {actual['readyMs']} ms, "
                      f"maximum event gap after show {actual['maximumGapAfterShowMs']} ms")
        finally:
            # Only this unique fixture bundle/extension; never a real ZIP handler.
            subprocess.run([str(unregister), "-u", str(application)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--launcher", required=True)
    run(parser.parse_args())
