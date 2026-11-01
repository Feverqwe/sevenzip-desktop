#!/usr/bin/env python3
"""Sign deterministic ext4/fs-verity test images with published Linux test keys.

OpenSSL independently verifies every signature against the explicit test chain.
Only public certificates, compressed images and expected content hashes remain.
"""
import argparse
import ast
import base64
import hashlib
import json
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--linux", type=Path, default=Path("build/review/filesystems-linux/linux"))
    parser.add_argument("--openssl", required=True)
    parser.add_argument("--output", type=Path, default=Path("tests/fixtures/ext4/verity-signatures.json"))
    args = parser.parse_args()
    capabilities = subprocess.run([args.openssl, "cms", "-help"], text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if capabilities.returncode or "-no_signing_time" not in capabilities.stdout:
        parser.error("--openssl must select an OpenSSL CMS tool with -no_signing_time support")
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from tests.formats.ext.support.ext_verity_fixture import VerityVolume
    from tests.formats.ext.support.ext_fixture import NAME
    rsa = runpy.run_path(str(Path(__file__).with_name("generate-rsa-fixtures.py")))
    source = (args.linux / "crypto/testmgr.h").read_bytes()
    manifest = json.loads(Path("docs/ext-linux-baseline.json").read_text())
    assert hashlib.sha256(source).hexdigest() == manifest["files"]["crypto/testmgr.h"]
    private = None
    for entry in rsa["entries"](source.decode(), "rsa_tv_template"):
        encoded = rsa["field"](entry, "key")
        values = rsa["integers"](encoded)
        if len(values) == 9 and values[1].bit_length() == 512:
            private = encoded
    assert private is not None
    certificates = {}
    text = Path("tests/fixtures/asn1/trust-chain.h").read_text()
    for name in ("root", "leaf", "intermediate", "collision"):
        body = re.search(r"trust_chain_" + name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
        certificates[name] = b"".join(ast.literal_eval("b" + v) for v in re.findall(r'"[^"]*"', body))
    result = {"linux_revision": manifest["revision"], "path": NAME,
              "root": certificates["root"].hex(), "collision": certificates["collision"].hex(), "cases": []}
    with tempfile.TemporaryDirectory(prefix="sevenzip-ext-verity-signatures-") as temporary:
        temp = Path(temporary)

        def openssl(*command):
            subprocess.run([args.openssl, *command], check=True, stdout=subprocess.DEVNULL)

        for name, der in certificates.items():
            (temp / (name + ".der")).write_bytes(der)
            openssl("x509", "-inform", "DER", "-in", str(temp / (name + ".der")), "-out", str(temp / (name + ".pem")))
        (temp / "key.der").write_bytes(private)

        def signer(algorithm, digest):
            formatted = b"FSVerity" + algorithm.to_bytes(2, "little") + len(digest).to_bytes(2, "little") + digest
            (temp / "digest").write_bytes(formatted)
            attributes = ("-nosmimecap", "-no_signing_time") if with_attributes else ("-noattr",)
            openssl("cms", "-sign", "-binary", *attributes, "-md", "sha256", "-in", str(temp / "digest"),
                    "-signer", str(temp / "leaf.pem"), "-inkey", str(temp / "key.der"), "-keyform", "DER",
                    "-certfile", str(temp / "intermediate.pem"), "-outform", "DER", "-out", str(temp / "signature"))
            openssl("cms", "-verify", "-binary", "-inform", "DER", "-in", str(temp / "signature"),
                    "-content", str(temp / "digest"), "-CAfile", str(temp / "root.pem"), "-auth_level", "0",
                    "-purpose", "any", "-out", str(temp / "verified"))
            assert (temp / "verified").read_bytes() == formatted
            return (temp / "signature").read_bytes()

        for algorithm in ("sha256", "sha512", "sha256-attrs", "unsigned"):
            with_attributes = algorithm == "sha256-attrs"
            volume = VerityVolume("sha512" if algorithm == "sha512" else "sha256", b"signed salt",
                                  signer=signer if algorithm != "unsigned" else None)
            result["cases"].append({"name": algorithm, "size": len(volume.data),
                "image": base64.b64encode(zlib.compress(volume.data, 9)).decode(),
                "file_size": volume.size, "sha256": hashlib.sha256(volume.read(0, volume.size)).hexdigest(),
                "digest": volume.file_digest.hex(), "signature_offset": volume.signature_physical,
                "signature_size": volume.signature_size})
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
