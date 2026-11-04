# Licenses and third-party notices

The root [MIT license](LICENSE) covers original project work unless a file or
component specifies another license. It does not relicense imported or adapted
sources. Retain their copyright and license notices when redistributing them.

| Component | License and source information |
| --- | --- |
| libsmb2 server research fork | [LGPL-2.1-or-later](third_party/libsmb2/LICENCE-LGPL-2.1.txt), original per-file notices and [component terms](third_party/libsmb2/COPYING). Local protocol/transport changes retain that license. [Source audit, original baseline and current integration boundary](docs/upstream-port-audit/readonly-smb.md). |
| Read-only F2FS module | GRUB [GPL-3.0-or-later](third_party/grub/COPYING), f2fs-tools selected helpers under their [LGPL-2.1 option](third_party/grub/f2fs-tools-COPYING), retained Samsung/Google/FSF notices; combined module GPL-3.0-or-later. Reused 7-Zip ABI support retains LGPL notices. No unRAR or GPL-2.0-only reader code is linked into this module. [Source audit and baselines](docs/upstream-port-audit/f2fs.md). |
| Official lzlib and read-only Lzip module | Antonio Diaz Diaz's [BSD terms](third_party/lzlib/COPYING); adapted example permits unrestricted copying, distribution and modification. Reused native boundary retains LGPL notices; combined module GPL-2.0-only. [Source audit and baseline](docs/upstream-port-audit/lzip.md). |
| Official LZ4 library and modern Frame module | Library: [BSD-2-Clause](third_party/lz4/lib/LICENSE). Native stream loop adapts Yann Collet's GPL-2.0-or-later CLI; combined module GPL-2.0-only, [GPL text](third_party/linux/LICENSES/preferred/GPL-2.0). Reused 7-Zip/7-Zip-zstd sources retain LGPL notices. [Source audit and baseline](docs/upstream-port-audit/lz4-frame.md). The official raw decoder also serves legacy LZ4, EROFS and SquashFS. |
| Linux legacy LZ4/lzop and composite initramfs module | GPL-2.0-only as a combined module; [GPL text](third_party/linux/LICENSES/preferred/GPL-2.0), retained Linux wrapper/decoder notices and [source audit](docs/upstream-port-audit/cpio-linux.md#composite-initramfs--2026-10-20). Reused SDK CPIO, gzip, bzip2, LZMA, XZ and Zstd sources retain their [7-Zip notices](third_party/7zip/License.txt); Raw LZ4 uses the [official BSD-2-Clause library](third_party/lz4/lib/LICENSE). The separate module contains no unRAR implementation. |
| Linux MSDOS nested partition module | GPL-2.0-only as a combined module; [license text](third_party/linux/LICENSES/preferred/GPL-2.0), retained Linux partition-reader notices and [source audit](docs/upstream-port-audit/mbr-linux.md). Reused 7-Zip handler, FAT probe and stream/ABI sources retain their LGPL notices. The separate module contains no unRAR implementation. |
| 7-Zip sources, adapted UI, language files, help and resources | [7-Zip license](third_party/7zip/License.txt): LGPL-2.1-or-later, public domain and the listed BSD exceptions; RAR decoder code also carries the unRAR restriction. [Source audit](docs/upstream-port-audit.md) and [baseline](docs/upstream-baseline.json). |
| Read-only exFAT/TexFAT module and its Linux adaptation | GPL-2.0-or-later; [license text](third_party/linux/LICENSES/preferred/GPL-2.0), retained SPDX notices and [source audit](docs/upstream-port-audit/exfat-linux.md). |
| GRUB XFS read-only module | GPL-3.0-or-later; Free Software Foundation notices, [GPL text](third_party/grub/COPYING) and [source audit](docs/upstream-port-audit/xfs.md). Reused 7-Zip ABI/stream/CRC32C code retains LGPL-2.1-or-later terms. The separate module contains no unRAR or GPL-2.0-only Linux implementation. |
| Linux-derived FAT compatibility module | GPL-2.0-only; [license text](third_party/linux/LICENSES/preferred/GPL-2.0), retained FAT/NLS notices and [source audit](docs/upstream-port-audit/fat-linux.md). The separate module reuses the LGPL 7-Zip handler and archive ABI; it contains no unRAR implementation. |
| Linux ext2/3/4, fs-verity and fscrypt read module | GPL-2.0-only as a combined module; [GPL text](third_party/linux/LICENSES/preferred/GPL-2.0), retained filesystem, Google, Linaro, ARM, Intel, IBM, Vitaly Chikunov, Kenneth MacKay, Free Software Foundation, Martin Willi, Jason A. Donenfeld/Andrew Moon, Red Hat and other per-file crypto notices, and [source audit](docs/upstream-port-audit/ext-linux.md). The original GPL-2.0-or-later crypto libraries retain their terms; GNU MPI's mixed-word arithmetic header retains its [LGPL-2.0-or-later terms](third_party/linux/LICENSES/preferred/LGPL-2.0). ECC arithmetic retains its BSD-2-Clause terms and full per-file notices. Dual-licensed Poly1305, Streebog and signature test vectors are used under their GPL options. ASN.1 schemas retain IETF attribution and [BSD-3-Clause terms](third_party/linux/LICENSES/preferred/BSD-3-Clause). Reused LGPL 7-Zip archive/stream sources retain their notices. The separate module contains no unRAR implementation. |
| Linux NTFS3 read module and LZNT1/LZX/XPRESS decoders | GPL-2.0-only as a combined module; [license text](third_party/linux/LICENSES/preferred/GPL-2.0), retained Paragon Software and Eric Biggers notices and [source audit](docs/upstream-port-audit/ntfs-linux.md). The original LZX/XPRESS library remains GPL-2.0-or-later. The separate format module reuses LGPL 7-Zip archive/stream sources and contains no unRAR implementation. |
| Linux HFS/HFS+ read module | GPL-2.0-only as a combined module; [license text](third_party/linux/LICENSES/preferred/GPL-2.0), retained Paul H. Hargrove, Brad Boyer/Ardis and ARDI notices and [source audit](docs/upstream-port-audit/hfs-linux.md). Reused 7-Zip archive sources retain LGPL notices; LZFSE/LZVN retains [Apple/Igor Pavlov BSD terms](third_party/lzfse/LICENSE). The separate module contains no unRAR implementation. |
| Linux ISO/Rock Ridge/zisofs read module | GPL-2.0-only as a combined module; [license text](third_party/linux/LICENSES/preferred/GPL-2.0), retained Eric Youngdale/Linus Torvalds/H. Peter Anvin/Gordon Chaffee notices, Acorn attribution to Matthew Wilcox, NLS attribution to Wolfram Pienkoss and the Unicode Organization, and [source audit](docs/upstream-port-audit/isofs-linux.md). Official zlib retains Mark Adler/Jean-loup Gailly attribution and [zlib terms](third_party/zlib/LICENSE). The retained Linux packed-size bound keeps its [original zlib notice](third_party/linux/zlib-LICENSE.txt). Reused 7-Zip archive/stream sources retain LGPL notices. The separate module contains no unRAR implementation. |
| Linux-derived EROFS read module | GPL-2.0-only; [license text](third_party/linux/LICENSES/preferred/GPL-2.0) and [source audit](docs/upstream-port-audit/erofs-linux.md). LZ4 retains its [upstream notices](third_party/lz4/lib/LICENSE). MicroLZMA uses liblzma from XZ Utils, copyright the XZ Utils authors and contributors, under [0BSD](third_party/xz/COPYING.0BSD); individual files retain their author attribution. Reused 7-Zip decoder/ABI sources retain their notices. The separate module contains no unRAR implementation. |
| Linux-derived Btrfs read module | [GPL-2.0 text](third_party/linux/LICENSES/preferred/GPL-2.0), [UAPI exception](third_party/linux/LICENSES/exceptions/Linux-syscall-note) and [source audit](docs/upstream-port-audit/btrfs-linux.md). Linux LZO and BLAKE2b retain their original notices; generic inflate retains the [zlib terms](third_party/linux/zlib-LICENSE.txt). Reused 7-Zip decoder/ABI sources retain their notices. The separate module contains no unRAR implementation. |
| Brotli decoder and 7-Zip codec adaptation | [Brotli MIT license](third_party/brotli/LICENSE) and [7zip-zstd LGPL notice](third_party/brotli/7zip-zstd-LICENSE.txt); see the [source audit](docs/upstream-port-audit.md). |
| Shared Zstandard encoder/decoder and XXHash | [libzstd BSD license](third_party/zstd/LICENSE), retained 7-Zip framing notices and [7-Zip-zstd LGPL encoder adapter notice](third_party/zstd/7zip-zstd-LICENSE.txt); see the [codec audit](docs/upstream-port-audit/zip-zstd.md) and [checksum audit](docs/upstream-port-audit/shared-checksums.md). |
| Shared zlib inflate and Adler32 | Mark Adler/Jean-loup Gailly attribution and [zlib terms](third_party/zlib/LICENSE); used by filesystem modules and the native SDK zlib wrapper. [Checksum audit](docs/upstream-port-audit/shared-checksums.md). |
| Shared liblzma CRC32/CRC64 | XZ Utils authors and contributors, [0BSD](third_party/xz/COPYING.0BSD); Linux/LZOP checksums use CRC32, SDK and native XZ/hash consumers use CRC64. [Checksum audit](docs/upstream-port-audit/shared-checksums.md), [CRC64 audit](docs/upstream-port-audit/shared-crc64.md). |
| Shared CRC32C | Igor Pavlov's 7-Zip VHDX checksum bodies, LGPL-2.1-or-later; shared by VHDX, XFS and Linux filesystem modules. [Source and adaptation audit](docs/upstream-port-audit/shared-checksums.md#shared-crc32c-service), [license](third_party/7zip/DOC/copying.txt). |
| Shared AES key/block primitives | Igor Pavlov's public-domain 7-Zip SDK AES; existing GPL Linux modes and local adapters retain their licenses. [Scope and adaptation audit](docs/upstream-port-audit/shared-checksums.md#shared-aes-blocks-with-linux-modes). |
| Release, packaging, Qt setup and Hyper-V tooling | GPL-2.0-or-later; [retained notice and provenance](#tooling-notice), [GPL text](third_party/linux/LICENSES/preferred/GPL-2.0). |
| libarchive RAR test data | [Retained license](tests/fixtures/rar/libarchive-COPYING.txt) and [fixture provenance](tests/fixtures/rar/README.md). Test data is not shipped as an application dependency. |
| Lucide command/navigation icons | SVG geometry adapted to native Qt paths; [ISC and Feather MIT notices](third_party/lucide/LICENSE), [source provenance](docs/upstream-port-audit/ui-redesign.md#итерация-22-контурные-значки-команд). |

Qt and other runtime dependencies keep their own licenses.
The linked Qt Remote Objects module uses Qt's LGPL-3.0 licensing option
(with its other available licenses retained in the SDK). Its matching source is
available from the [official Qt repository](https://github.com/qt/qtremoteobjects/tree/v6.12.0).
The QCOW/QCOW2 reader also uses QEMU read-side code under the
[retained MIT notice](third_party/qemu/LICENSE); see the
[source audit](docs/upstream-port-audit/container-layers.md#qcow).
Qt Base translations are merged from the matching SDK; the corresponding official
[Qt Translations source](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qttranslations-everywhere-src-6.12.0.tar.xz)
and source provenance are recorded in the [localization audit](docs/upstream-port-audit/qt-linguist.md).
Runtime packages
include dependency notices; packaging details and signing limitations are in
[CI and packaging](docs/ci.md). The repository's MIT license alone does not
describe the licensing of an entire binary distribution.

## Encrypted Apple disk images

The encrypted DMG format module includes adapted apfs-fuse code by Simon Gander,
licensed under GPL-2.0-or-later. The [original license](third_party/apfs-fuse/LICENSE)
and [source audit](docs/upstream-port-audit/dmg-encryption.md) record its scope
and reproducible source baseline. Existing 7-Zip primitives retain their notices.

## Tooling notice

Release, packaging, Qt installation/source-fetching and Hyper-V tooling includes
adaptations from [htopQt](https://github.com/Feverqwe/htop-qt-ai-driven.git),
commit `f57b93a72d765f738a6b779b4b3c94e16c0ef64a`.
The release/packaging import was recorded in Sevenzip commit
`f91e0c010c35d239b6a179ebc462df24d842eb3f`, and the restored release workflow
in `52c0b3e7205a78ac6aa7efcf8ec11dbc8e179a4e`.
The Hyper-V helper was adapted for Sevenzip resources, MSVC, pinned engine
builders, formatter and SSH stdin transport. Its CLI, parallel probes, shared
Bash command payload and discovery-error reporting were refreshed on 2026-10-04
from a modified source worktree based on
`56320ebb4d62b3bac83f38719278cae6a5c454c1`;
the source script SHA256 was
`f4f53f0123a503dcc4fb85f17821d79e1cf156f29cbe015b99d561b45b1903ba`.
Current usage is documented in [CI and packaging](docs/ci.md) and
[Hyper-V runners](docs/hyperv-runners.md).

Retained source project notice:

> htopQt is distributed under the GNU General Public License, version 2 or later.
> The full license text is in third_party/htop/COPYING.
>
> Upstream htop copyright notices and licenses are preserved in third_party/htop.
> Original project glue is licensed under GPL-2.0-or-later.
> Qt and other dependencies retain their respective licenses.

Paths in that notice refer to the source project. This repository retains the
[GPL version 2 text](third_party/linux/LICENSES/preferred/GPL-2.0).
The notice applies to borrowed tooling and its adaptations, not to all Sevenzip
sources.
