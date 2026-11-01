`plain.iso.zlib` and `joliet.iso.zlib` contain only generated patterned files,
an empty file, a nested directory and a generated 2048-byte El Torito boot image.
They were created on Ubuntu with genisoimage 1.1.11:

```
genisoimage -quiet -iso-level 3 -V IsoFixture -no-pad \
  -b boot.bin -no-emul-boot -boot-load-size 4 -c boot.cat \
  -o plain.iso seed
```

The Joliet image adds `-J -input-charset utf-8` and a generated Unicode filename.
Input files used modification time 1760000000. No user archives, physical
devices or executable boot code were used. `tests/formats/iso/support/iso_fixture.py` specifies
expected bytes and derives logical-block, XAR, multi-section and damaged cases.

`rock.iso.zlib` and `rock-joliet.iso.zlib` use the same genisoimage version with
`-R -iso-level 3 -V RockFixture -no-pad`, with `-J -input-charset utf-8` for the
second image. They contain generated Unicode and punctuation names, a 239-byte
name, two symlinks and eleven nested directories, including native CL/RE/PL
relocation and NM/SL continuation records. File modes are 0640 and directory
modes 0750, UID/GID 1000; genisoimage also records native generated metadata.
Modification times were set to 1760000000; native change/access times may differ.
`tests/formats/iso/support/iso_rock_fixture.py` defines payloads and the focused CE/SL/CL, TF/PN and
primary-root variants. No symlink was followed to copy outside fixture data.

`zisofs.iso.zlib` was created with mkzftree 1.0.7 (`mkzftree -f seed compressed`)
and genisoimage 1.1.11 (`-R -z -no-pad -iso-level 3 -V ZisofsFixture`).
It contains only generated patterned, deterministic random, sparse, Unicode
and empty files, with input modification time 1760000000.
`tests/formats/iso/support/iso_zisofs_fixture.py` defines the expected bytes and derives focused
pointer-table, compression-block, header, multi-section and corruption cases.

`tests/formats/iso/support/iso_sierra_fixture.py` projects the generated plain/XAR/multi-section
payloads to the High Sierra layout described by Linux UAPI `iso_fs.h`.
No historical user images are used. The reserved flags byte is deliberately
nonzero; files preserve the same expected payloads. The 1024-byte variants
distinguish logical-block padding from padding compatible with older kernels.
Short recording-date variants exercise timezone limits and normalization.

`tests/formats/iso/support/iso_name_fixture.py` derives Joliet component/surrogate/NUL/long-name,
NLS, RR-without-NM and Acorn variants from these generated images. It uses no
user filenames or historical media. The Acorn marker and file type are synthetic;
malformed marker/length variants must retain the normal fallback name.

`tests/formats/iso/support/iso_tree_fixture.py` constructs deep directory chains from the same
native record templates and generated payload storage. It derives valid
768/4096-level trees and a deep cycle; no host directory tree is created.
