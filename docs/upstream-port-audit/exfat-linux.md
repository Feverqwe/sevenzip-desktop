# Linux exFAT read-only format module

## Seekable files for mounted access — 2026-11-03

The `exFAT.Dmg.EncryptedDmg` mount failure was traced to the local archive adapter:
it exposed extraction but no `IInArchiveGetStream`. The generic mount therefore
materialized requested files and rejected files exceeding its disk-cache quota.
This was unrelated to the image's compression or encryption settings.

The adapter now exposes a seekable file stream through the same C reader used
by extraction. It retains the original volume/input independently of handler
Close/reopen, with private position, hint and cycle-detection state. The shared
input/progress/error context has a separate owner; no opening or extraction UI
callback is retained after its operation. Direct reads share the original
file-size, ValidDataLength zero-fill, active FAT/bitmap, allocation and physical
source checks. Transport errors and cancellation remain failures. No decoded-file
temporary output is created by this interface.

Source comparison covered complete original `cache.c`, `iomap.c`, `inode.c`,
`file.c`, `exfat_fs.h` and `fatent.c` at the existing pinned Linux revision above;
retrieved bytes matched their recorded complete-unit hashes. The first requested
cluster loop from `cache.c::exfat_get_cluster` is partially adapted into the C
reader's file mapper, shared by extraction and seekable reads. It retains cached
FAT-sector traversal; the stream cursor replaces VFS inode/cache lifetime. Every
traversed cluster is cycle checked, including a first read after a forward seek.
Backward seeks reset the cursor and visit set. Contiguous allocations retain
direct cluster arithmetic. Kernel LRU allocation and multi-cluster aggregation
remain omitted performance services. Full original hashes are unchanged; the
baseline records the newly transferred symbol and local owner.

Regressions cover fragmented/contiguous files, TexFAT active allocation pairs,
4096-byte sectors, random/backward reads, EOF, unwritten tails, stream lifetime
through Close/reopen, interleaved streams, cyclic chains and transport failures/
cancellation. Backend and encrypted-image consumers use zero extraction quota.
Native macOS acceptance creates encrypted UDRO and UDZO exFAT images from
synthetic files and reads a 4 MiB file through the actual window's Mount command
and system SMB mount, with no extraction prompt or cache controls. These fixtures
exercise the reported reader chain without the user's private image. Existing
recovery and directory limitations remain in effect.

Current status handling includes [limited read-only access](#limited-read-only-status-2026-10-21)
for dirty/media-failure volumes and [partial directory access](#partial-directory-access-2026-10-23).
The original clean-only TexFAT boundary below is retained as dated evidence.

The boot reader now uses the original shared Linux string scanner; source scope
and validation are recorded with the [shared string-service transfer](erofs-linux.md#original-shared-string-services--2026-10-22).

## Sources and reproducible baseline

Selected on 2026-10-17 from the current mainline Linux tree, rather than the
historical Samsung release:

- Repository: <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git>.
- Full commit: `58161ead29e50d70a317ea26b472e0c7508e69da`.
- Inventory unit: the complete `fs/exfat` directory, including its headers,
  Makefile and Kconfig; all sixteen files were read or compared for dependencies.
- Original-file SHA256 values: [Linux baseline](../exfat-linux-baseline.json).
  Retrieve files from that commit when reviewing or updating the adaptation.
- Linux notices and GPL-2.0-or-later SPDX identifiers are retained. The source
  license text is in `third_party/linux/LICENSES/preferred/GPL-2.0` and is installed
  with the module. The root MIT license does not replace these notices.

The format ABI reuses the official 7-Zip implementation at commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`, repository
<https://github.com/ip7z/7zip.git>. `CPP/7zip/Archive/ArchiveExports.cpp` is copied
unchanged and registered in the existing [7-Zip baseline](../upstream-baseline.json).
`CreateObject`/the handler CLSID come from `Archive/DllExports.cpp` and
`GetModuleProp` from `Compress/CodecExports.cpp`; the module entrypoint adapter
retains only the archive ABI/version boundary. Existing portable 7-Zip COM,
properties, streams, Unicode and time helpers are reused.

## Compared scope, transfer and platform boundary

The source comparison covered the boot/mount path in `super.c`, directory
enumeration and entry-set consumers in `dir.c`/`namei.c`, FAT traversal in
`fatent.c`/`exfat_fs.h`, allocation-bitmap readers in `balloc.c`, timestamps and
checksums in `misc.c`, upcase handling in `nls.c`, and read mapping through
`inode.c`/`cache.c`/`iomap.c`. The complete driver is not ported.

Disk-facing adapted code remains C under `src/fs/exfat/`. The same inventory
also accounts for callers that remain at the archive/platform boundary:

| Upstream module | Read scope and boundary |
| --- | --- |
| `super.c` | `exfat_calibrate_blocksize`, `exfat_read_boot_sector`, `exfat_verify_boot_region` |
| `dir.c` | `exfat_extract_uni_name`, `exfat_get_uniname_from_ext_entry`, `exfat_readdir`, `exfat_get_entry_type`, entry location/read, entry-set loading/validation/release, volume-label lookup/read |
| `fatent.c` | `__exfat_ent_get`, `exfat_ent_get`, `exfat_count_num_clusters` |
| `balloc.c` | Bitmap range testing, allocation/loading/freeing, `exfat_test_bitmap` and `exfat_count_used_clusters` |
| `misc.c` | `exfat_calc_chksum16`, `exfat_calc_chksum32`, `exfat_chain_set`, `exfat_chain_dup`; no mutation transport |
| `exfat_fs.h`, `exfat_raw.h` | Original constants, packed disk structures, read-facing types and geometry/cluster-chain helpers, including `exfat_cluster_walk`/`exfat_chain_advance` |
| `namei.c` | `exfat_find` metadata/zero-size rules are projected by the existing C entry adapter; pathname lookup, dentry aliases and mutation are not archive-index operations. `zero_size_dir` creation was compared to establish the disk read contract, not copied as a writer. |
| `inode.c` | Root/file state, valid-size, flags and timestamps are represented by the C volume/items/cursors. VFS inode publication, generated IDs/link counts, credential/mask defaults and writeback are omitted. |
| `cache.c` | Compared `exfat_get_cluster` and its cache consumers with the retained original cluster walker. The archive cursor keeps its own hint; kernel LRU/cache lifetime and contiguous-I/O aggregation are performance services. |
| `iomap.c` | Compared read mapping, byte-valid boundary and end-I/O tail zeroing. The archive sequential stream returns the same data/zero regions without folios, bio, mmap, direct I/O or SEEK_DATA/SEEK_HOLE APIs. Writeback/swap activation are not image decoding. |
| `file.c` | Stored DOS attributes and birth/modification/access times are exposed as properties; the label ioctl reader is connected through the transferred `dir.c` functions. Generic read/seek/splice/open, statx/ioctl transport, leases and mutation stay with VFS. |
| `nls.c` | Original UTF-16 names/labels reach the SDK string boundary. NLS encodings, upcase/default-table lookup, name hashes and trailing-dot lookup options serve filesystem pathname resolution, which this indexed archive interface does not expose. These tables do not map file bytes. |
| `Kconfig`, `Makefile` | Driver configuration/build inventory; the separate module's CMake target owns userspace dependencies and deployment. |

The platform header replaces kernel allocation, endian helpers and block reads.
Each volume has its own stream context; there is no kernel, FUSE, system mount,
raw-device discovery or write callback. Readahead is omitted as a kernel
performance service; reads remain demand driven. The bitmap word width is
explicitly 64 bits on LLP64 hosts. VFS-only inode/superblock fields are omitted.
Packing is applied around the original raw structures for MSVC and Unix compilers.

`exfatreader.c` supplies volume lifetime, the stream-backed `buffer_head` service,
directory/extraction cursors and the C/C++ boundary. Metadata assignments follow
`namei.c::exfat_find`; stream reads advance through the original cluster walker,
using the retained cursor rather than Linux's VFS/page-cache/extent-cache APIs.
The unwritten tail follows `iomap.c::__exfat_iomap_begin`'s `valid_size` distinction
and is delivered as zero bytes. Timestamp decoding now uses the original Linux
C functions described below; 7-Zip helpers only project UNIX time into FILETIME.

`exfathandler.cpp` implements the new `IInArchive` adapter: filesystem traversal,
properties, callback progress, extraction and test mode. There is no usable
upstream 7-Zip exFAT handler at its pinned revision, and Linux has no 7-Zip COM
adapter. The new code is restricted to that boundary and stream/lifetime services;
the filesystem readers themselves come from Linux. Widgets and `ArchiveBackend`
retain their existing generic format-discovery and operation paths.

Explicit adaptations for untrusted image streams:

- Establish volume/cluster/extent bounds that Linux normally obtains from a
  block device; reject unsupported major revisions. Two-FAT/TexFAT reads are
  selected from the active allocation pair; status limitations are exposed as
  described in the current limited-read section below.
- Preserve bounded root-chain bootstrap and validate requested file clusters.
  Child-directory enumeration uses the bounded original Linux walker and the
  declared directory size; ancestor cycles remain incomplete directories.
- Require complete filename/stream entry sets and matching entry-set checksums;
  expose skipped malformed sets as directory limitations while retaining readable
  independent entries.
- Preserve UTF-16 names until the 7-Zip boundary and reject path components that
  could introduce traversal, separators or alternate streams during extraction.
- Propagate open/extract cancellation and source/output I/O errors; clean failed
  open state and callback leases before a handler is reused.

Omitted scope is intentional: all writes, allocation/free operations, repair,
deleted-file recovery, VFS mounting/options/inode publication, kernel caches and
bio/iomap scheduling. Upcase loading and case-insensitive lookup are not needed
because the module enumerates original names and 7-Zip selects indexed entries;
no filesystem name lookup API is exposed. The Linux bitmap reader supplies
allocation checks when available; missing bitmap requires limited-read consent.
`IOutArchive` and `IInArchiveGetStream` are not advertised;
ordinary extraction is the available file-data interface. This is a bounded
read-only source adaptation, not a complete source transfer of the Linux driver.

Formatting is separate: copied Linux bodies retain kernel formatting; new C++
and integration headers use the repository formatter. No sibling upstream files
are changed.

## Deployment and validation

The application build stages the separate format module into the existing
dynamic engine's `Formats` directory. Installation places it beside that engine
and installs its GPL notice. The standalone `7zz` executable cannot load it;
the application's existing dynamic `7z` runtime is required. An explicitly
overridden external engine needs its own compatible module deployment.

`exfat` exercises the exported ABI and the official process engine on a local,
deterministic volume. It covers nested/Unicode and maximum-length names, entry
sets crossing a fragmented directory boundary, fragmented file contents, empty
files, timestamps, unwritten tails, malformed boot/entry checksums, ranges,
cycles, traversal names, truncated input, open/extract cancellation, output errors
and source-image preservation. `exfat_backend` covers real application discovery,
read-only capability, listing, test and extraction using a private runtime.
Existing external-codec suites cover the shared module loader separately.

Focused validation on 2026-10-17, using the retained incremental builds and
compiler caches:

- macOS arm64, Qt 6.12: `exfat` and `exfat_backend` PASS (18 Qt cases including
  setup/cleanup, CTest 0.84 s). The final reader includes the 4096-byte logical
  sector case. `external_codecs` and `codec_probe` also passed in the immediately
  preceding four-suite run (6.97 s).
- Linux x64, Qt 6.12: final two exFAT suites PASS, CTest 0.29 s.
- Windows x64/MSVC, Qt 6.12: final two exFAT suites PASS, CTest 2.17 s, without
  task-owned compiler warnings. A previous CLI test failed because its output
  used the Windows console code page; the test now requests `-sccUTF-8`, matching
  the existing backend transport. Backend integration already passed that run.
- Both guests tested the same source snapshot SHA256
  `66de622532823f5b44c28437591d7de61f74128894df247eff51fbf9bdb1ad69`.
  Run receipt: `20261017T190349Z-1b610fc7`; the earlier loader/codec-probe run is
  `20261017T190123Z-1628baf1`. Those two unchanged shared-loader suites passed
  on both guests in the earlier run.
- Original Linux-file hashes and the byte-identical 7-Zip archive exports were
  checked against their recorded sources. Generated macOS install rules place
  the module under the bundle runtime's `Formats` and include its GPL notice.

The source transfer is limited to the read scenario described above. Runtime
evidence covers generated local images and actual official-engine/application
backend consumers on each OS; it does not establish recovery, physical-device,
all-image compatibility or installed-package acceptance. The macOS system
formatter requires a special device and did not format the temporary regular
file; no device was mounted or formatted. UI geometry/rendered parity and long
suites are outside this focused nonvisual validation. A full gate was not needed
because the format module is isolated and its direct loader/backend consumers
were covered.

## Consistent TexFAT images (2026-10-17)

The preceding single-FAT implementation was committed as `f356400`. This
follow-up extends the same read-only engine module; it does not implement
transaction replay or writes. The Linux source revision/baseline above is
unchanged.

Before implementation, complete bodies and consumers were compared again:
`super.c::exfat_read_boot_sector`, `exfat_fs.h::FAT_ENT_OFFSET_SECTOR`,
`fatent.c::__exfat_ent_get`/`exfat_ent_get`, `balloc.c::exfat_load_bitmap`/
`exfat_allocate_bitmap`/`exfat_test_bitmap_range`, the root/directory and file
cluster walkers, plus `dir.c`'s entry classification/validation. The selected
Linux implementation stores two FAT offsets but reads FAT1 and selects only
bitmap identifier zero; its bitmap loader assumes contiguous physical sectors.
There is no usable active-pair selection implementation in that module to copy.

The narrow additions implement the public Microsoft
[exFAT specification](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification),
sections 3.1.13.1, 3.1.16, 4, 7.1 and 7.10:

- Retain `ActiveFat` from the **Main** Boot Sector; backup VolumeFlags are stale.
  All FAT reads, including root and bitmap chains, use the selected FAT offset.
- Enumerate and require the appropriate bitmap identifiers without duplicates;
  load only the bitmap paired with the selected FAT. The inactive pair is stale
  and is neither interpreted nor used as an error fallback.
- Follow the active bitmap's FAT chain with the existing Linux chain walkers,
  including fragmented bitmap extents. Bound its declared allocation by the
  cluster heap; check chain length/cycles and the allocation of its actual
  clusters rather than assuming a contiguous bitmap.
- Continue skipping benign primary entries, including TexFAT Padding, through
  the existing upstream `exfat_get_entry_type`/`exfat_readdir` branches. Unknown
  critical secondary entries remain unsupported; security metadata is not
  exposed or applied to extracted files.
- Reject two-FAT images marked `VolumeDirty` or `MediaFailure`, and reject the
  invalid single-FAT/second-active combination. Do not clear flags, switch pairs,
  infer a replacement active pair or attempt repair.

Here, consistent-image support means clean status flags plus the retained
structural checks for the selected data view. It is not a proof of every
filesystem invariant or validation/recovery of an interrupted transaction.

The fixture set distinguishes the two copies with different data-cluster chains
and bitmap contents, and deliberately gives backup VolumeFlags a different
selection. Tests exercise either active pair, fragmented bitmaps with an
allocation bit in a later bitmap cluster, 4096-byte sectors, padding, nested
Unicode files and original-image preservation. Failure cases cover dirty/media
status, missing/duplicate bitmap entries, active root/bitmap cycles, an
unallocated active root, refusal to use stale data after an active-file error,
and cancellation followed by reuse. The official process engine and
`ArchiveBackend` exercise both choices with actual listing/test/extraction.

Focused validation passed on macOS, Linux and Windows:

- macOS: incremental build of `exfat_tests`, `exfat_backend_tests` and
  `sevenzip_exfat_runtime`; `exfat` and `exfat_backend` passed, 44 Qt cases,
  4.37 seconds. Local receipt: `build/review/texfat/local-last-test.log`.
- Linux/Windows: `scripts/verify.py --tests '^(exfat|exfat_backend)$'
  --platform all --skip-runtime` built the selected targets in the existing
  guest worktrees and passed both suites, 1.12/3.95 seconds respectively.
  Run receipt: `build/runners/hyperv/20261017T191621Z-00d9c19f/run.json`;
  both guests verified source SHA-256
  `9d79cb5628f8c0dd585c9e01f243779a4fdfcc14db8aa796b542140ceaea707a`.

Validation used generated images, not real Windows CE volumes. No rendered UI,
installed-package, full-suite or long-suite acceptance is claimed. Existing
incremental builds and compiler caches were retained; no new archive transport
or UI changes are involved.

## Cached FAT cancellation (2026-10-19)

The [Linux filesystem review](filesystems-linux.md) revisited the same pinned
Linux revision. For this scenario, complete bodies and callers were compared:
`fatent.c::__exfat_ent_get`, `exfat_ent_get`, `exfat_count_num_clusters`,
`exfat_fs.h::exfat_cluster_walk`/`exfat_chain_advance`, directory entry-set
loading, bitmap chain consumers, and the local open/extract stream adapters.
The upstream traversal, cluster validation, loop bound and buffer ownership are
retained. The additional userspace adaptation checks the existing cancellation
service in `exfat_ent_get`, including reads satisfied from its FAT-sector cache.
Kernel traversal has no archive callback to copy; checking only in `sb_bread`
left cached cyclic walks without a cancellation checkpoint.

The ABI regression requests cancellation when input data returns during
extraction of a file with a cyclic FAT chain. It requires `E_ABORT`, no published
file bytes and successful reuse for a valid image. It does not assert a callback
count or source structure. With the new checkpoint removed, the regression
failed: extraction completed with a data-error result instead of cancellation.
With the checkpoint restored, the focused ABI/backend suites passed on macOS.
The remaining runtime/platform evidence is recorded in the review entry.

## Timestamp decoding (2026-10-21)

Compared complete `misc.c:exfat_get_entry_time`, `exfat_adjust_tz`,
`exfat_tz_offset`, their `namei.c:exfat_find` callers, inode timestamp consumers,
`super.c:exfat_init_fs_context` defaults and `kernel/time/time.c:mktime64`.
The previous independent C++ DOS conversion hid zero/normalized dates and
increments above 199; timestamps without UTC-valid also depended on the host
timezone instead of the driver's default zero offset.

Original `exfat_get_entry_time` and `exfat_adjust_tz` bodies are now compiled
as C in `src/formats/exfat/misc.c`, using the existing authoritative
`src/compat/linux/time/time.c:mktime64`. The latter retains its GPL-2.0 notice and
Linux source baseline; its original source hash is added to the exFAT manifest.
All seventeen previous baseline source/license hashes were verified again.
The module remains a separate GPL format module; standalone 7zz is unchanged.

The only mount-boundary adaptation is `exfat_tz_offset`: archive reads have
neither explicit mount options nor `sys_tz` services, so it returns the original
zero-initialized default offset. Explicit disk offsets retain both signed
branches. The C/C++ adapter preserves little-endian input and seconds/nanoseconds;
the existing SDK UNIX-time conversion supplies FILETIME. All three public
timestamp properties use the same decoder, with no increment for access time.
Write-side timestamp conversion, VFS truncation and mount option services are
omitted. Existing payload/chain/bitmap readers and TexFAT adaptations are retained.

Post-change comparison confirmed complete selected decode branches and all
three property consumers. ABI regressions assert exact FILETIME for unknown
offsets (including ignored offset bits), both signed offset extremes, 10ms
fractions, increment 255, zero date, leap-day and clock overflow. Separate child
processes repeat these checks with `TZ=UTC0` and `TZ=HST10`, protecting against
host-dependent conversion. Existing extraction/cancellation/malformed-volume
and process-engine/backend checks remain part of the focused suites.

`exfat` and `exfat_backend` passed on macOS arm64/Qt 6.12 (2.54 s), Linux x64
and Windows x64/MSVC/Qt 6.12, remote receipt `20261021T014812Z-4572b3cc`.
Native exFAT `6.8.0-147-generic` confirmed all eight images through read-only
loop mounts: exact mtime/atime and file payload, receipt
`20261021T014934Z-8faff0f4`. Images remained unchanged and mounts/temporary
fixtures were removed. The older running kernel supplements pinned source
comparison; native creation-time export was not checked. Incremental builds
and compiler caches were preserved. Full/slow suites, UI and physical devices
were not checked; this scenario does not close the overall format audit.

## Volume-label read feature (2026-10-21)

The pinned Linux driver also implements `dir.c:exfat_get_volume_label_dentry`
and `exfat_read_volume_label`, consumed by `file.c:exfat_ioctl_get_volume_label`.
Complete bodies, types, root-chain setup/validation, entry classification,
cached-entry lifetime and `nls.c` output consumers were compared before adding
this previously absent read feature. All eighteen baseline hashes were verified.

Both original C readers now live in `src/formats/exfat/dir.c`; root-chain walking,
first-match selection, deleted/unused entry handling, length limit, endian
conversion and buffer release branches are retained. Omitted scope consists of
write-side free-entry hints and their root VFS inode, plus the VFS mutex:
the archive owner serializes its per-volume source. Root chain counting still
precedes the scanner, and the existing FAT adapter uses the selected active pair.
The write-label function and ioctl userspace transport are not copied.

The C volume owner reads and retains the label during Open. The existing 7-Zip
adapter exposes `kpidVolumeName` through the standard archive-property table;
the previously available physical size is now discoverable through that table
too. UTF-16 is retained until the SDK string boundary, including surrogate pairs.
The first NUL terminates presentation as in Linux's label ioctl converter.
Absence is an empty label; after Close or failed Open the property is undefined.
This is eager archive metadata validation: a malformed first label or a source
error rejects Open, whereas Linux reports that error when its label ioctl runs.
Source failure/cancellation HRESULTs survive the C reader and cleanup.

Post-change source comparison covered the transferred readers, their existing
dependencies and every new volume/ABI consumer. Twelve label scenarios cover
first/fragmented-root placement, both TexFAT active pairs, 4096-byte sectors,
Unicode/maximum/empty/absent/deleted labels, first-match duplicates, unused
termination and embedded NUL. ABI checks verify label, unchanged file payload,
failed-open cleanup, source failure/abort and reuse. The official process engine
lists the Unicode label and tests/extracts the image for exFAT and both TexFAT
pairs. Existing backend consumers remain covered.

Final `exfat`/`exfat_backend` PASS on macOS arm64/Qt 6.12 (1.91 s), Linux x64
(1.20 s) and Windows x64/MSVC/Qt 6.12 (3.65 s), receipt
`20261021T015503Z-20b73622`. Incremental builds and compiler caches were
preserved. Full/slow suites, UI appearance and physical devices were not checked.

Native `6.8.0-147-generic` lacks this ioctl: a read-only mounted fixture returned
ENOTTY, receipt `20261021T015619Z-6a18da24`. Native label acceptance is therefore
unverified; the implementation is compared against the newer pinned source.
The image remained unchanged and mount/temporary exporter/fixtures were removed.
No package installation was needed. This feature does not close the remaining
format audit or change standalone 7zz's exFAT boundary.

## Allocation statistics and geometry (2026-10-21)

Compared complete `balloc.c:exfat_count_used_clusters`, bitmap loader and
root/active-pair consumers, `super.c:exfat_statfs`/`__exfat_fill_super`,
`include/linux/bitmap.h:BITMAP_LAST_WORD_MASK`, `bitops.h:hweight_long` and
`lib/hweight.c` software helpers before integration. Post-change comparison
covered every selected branch and the archive-property consumers. All twenty-one
source/license hashes in the extended manifest were verified.

The original count loop now runs after loading the selected bitmap and stores
`used_clusters` in the original volume state. Sector transitions and the final
partial-word mask are retained. Bitmap word/pointer/stride and mask constants
use the existing explicit 64-bit service on LLP64 as well as LP64. A per-sector
archive cancellation checkpoint also covers cached scans. Original
`__sw_hweight32`/`__sw_hweight64` bodies live in `src/compat/linux/core/hweight.c`, compiled
as C with their GPL-2.0 notice. Kernel export/include transport is omitted;
the fixed word-width service chooses the software 64-bit branch without CPU
instruction requirements. The 8/16-bit helpers are not dependencies and are
not copied. All new Linux code remains in the separate GPL exFAT module.

The C/C++ adapter publishes free bytes, cluster size and sector size through
the standard archive-property table. Free bytes use the original `exfat_statfs`
data-cluster/free-cluster expressions; kernel device fsid, VFS inode/dentry
publication and statfs transport are omitted. Boot PercentInUse is not an
allocation count and is not substituted for the bitmap. Existing chain bounds,
bitmap acceptance, payload and TexFAT behavior remain in place.

Nine ABI scenarios cover 512/4096-byte sectors, both active FAT/bitmap pairs,
fragmented multi-sector bitmaps, a partial final word with deliberately allocated
padding outside the heap, and the last valid cluster inside that word. Tests
assert free bytes and geometry, including a 1024-byte cluster with a 512-byte
sector, scaling with allocation units and clearing properties on Close.
Existing source error/cancellation, malformed input,
process-engine listing/extraction and ArchiveBackend checks are retained.

Final `exfat`/`exfat_backend` PASS on macOS arm64/Qt 6.12 (1.97 s), Linux x64
(1.26 s) and Windows x64/MSVC/Qt 6.12 (3.56 s), receipt
`20261021T020231Z-59be93bd`. Earlier eight-scenario checks also passed on
all three platforms before adding the distinct cluster/sector geometry case.

Native exFAT `6.8.0-147-generic` confirmed three read-only images through
`statvfs`: base, partial-word padding and the last valid cluster, receipt
`20261021T020105Z-499c71eb`. Exact free-cluster counts, cluster size and file
payload matched; images remained unchanged, and mounts/temporary exporter/
fixtures were removed. This native check uses 512-byte sectors/clusters and
does not establish native TexFAT acceptance. Incremental builds and compiler
caches were retained. Full/slow suites, UI and physical devices were not checked.

## Zero-size directories (2026-10-21)

Compared complete `namei.c:exfat_add_entry`/`exfat_find`/`exfat_lookup`,
`inode.c:exfat_fill_inode`, directory counting/iteration and existing portable
metadata/traversal/extraction consumers. The pinned driver can create directory
entries without allocating a cluster (`zero_size_dir`); its read path normalizes
every zero-size entry to EOF/no-FAT-chain and accepts an empty directory. All
twenty-one baseline source/license hashes were verified again.

The existing C reader already retained the original zero-size normalization,
but an independent archive validation rejected zero-size directories before
reaching it. That extra rejection is removed. The archive traversal now emits
an empty directory without enqueueing a child cursor or inserting its shared
EOF marker into the directory-allocation cycle guard. Existing nonempty
directory bounds, allocation/cycle checks, timestamp and extraction paths are
retained. This is an adaptation correction in the existing archive boundary;
no new filesystem parser, VFS inode implementation or write path is added.

Post-change comparison confirmed the normalized EOF/no-FAT-chain state and
all directory consumers. Seven ABI scenarios cover ordinary exFAT, both
TexFAT active pairs, 4096-byte sectors and zero-size entries whose original
start-cluster field is zero, stale data, the root itself or outside the heap.
Two empty directories can coexist without a false cycle; old backing directory
contents stay invisible. Directory extraction reports success without data.
Official CLI and ArchiveBackend also list/test/extract an image containing two
empty directories alongside normal nested files, checking actual empty output
directories and unchanged original images. Existing corrupt nonempty-directory
regressions remain in the same suites.

Final `exfat`/`exfat_backend` PASS on macOS arm64/Qt 6.12 (4.64 s), Linux x64
(1.82 s) and Windows x64/MSVC/Qt 6.12 (5.49 s), receipt
`20261021T020605Z-570fdbc3`. Incremental builds and compiler caches were retained.
Full/slow suites, UI appearance and physical devices were not checked; the
overall format audit and standalone exFAT boundary are unchanged.

Native exFAT `6.8.0-147-generic` confirmed all four start-cluster variants
through read-only loop mounts, receipt `20261021T020637Z-c7b0283a`: both
directories have size zero and empty listings, and the ordinary file payload
still matches. Images remained unchanged; mounts, exporter, temporary fixtures
and review helper were removed. This older running kernel supplements the
comparison against the newer pinned source; TexFAT native acceptance is not
claimed by these four single-FAT controls.

## Read-side source audit closure (2026-10-21)

The complete `fs/exfat` inventory above has now been reconciled with the current
archive reader and its direct consumers. The final pass revisited root setup,
entry-set loading/validation, benign/vendor entry classification, name assembly,
pathname/upcase consumers, inode projection, cached/uncached FAT mapping,
bitmap loading/counting, normal/direct read and end-I/O zero-tail branches,
metadata/ioctl readers and lifecycle/error paths. No additional disk read feature
required by this read-only archive interface remains disconnected in the pinned
driver inventory. This closes exFAT's source read-scope review, not the entire
multi-format goal and not a transfer of every Linux driver function.

The archive boundary remains explicit: Unicode names are retained rather than
converted through a mount charset, entries are selected by archive index rather
than by the filesystem's upcase-table lookup, empty directories need no allocation
cursor, and inode credentials/permissions supplied by mount options are not
claimed as stored POSIX metadata. Upcase loading/checksum/default fallback is
therefore not an unused parser added to volume acceptance. There is no Linux
exFAT xattr/ACL, symlink, sparse-extent or compression reader omitted from this
inventory; ValidDataLength zero regions are covered by the existing stream.

Damaged-image behavior is deliberately bounded by the documented archive
adaptations: entry-set checksums, complete mappings, name/path bounds, directory
aliases/cycles and bitmap allocation are checked before publishing or extracting
data. Linux's mount repair, silent size clamping and dirty-volume mutation are
not reproduced. TexFAT active-pair support remains the documented extension
to this Linux revision. Neither this comparison nor passing generated fixtures
establishes recovery equivalence for every corrupt volume.

Runtime evidence is separate and retained in the scenario sections: current
ABI/process-engine/ArchiveBackend checks passed on macOS, Linux and Windows;
native read-only controls cover payloads, timestamps, allocation statistics and
empty directories. The older native kernel lacks the label ioctl, so that native
feature remains unverified despite source/ABI/CLI coverage. Native TexFAT,
physical devices, UI appearance, full/slow suites and a new installed-package
run are not claimed. This closure changes documentation only; `git diff --check`
is the applicable new check, with the existing incremental build/cache results
left intact.

## Limited read-only status (2026-10-21)

The Read-only filesystem access policy review found an extra adapter gate:
two-FAT images were refused solely for VolumeDirty or MediaFailure. Pinned Linux
`fs/exfat/super.c:exfat_read_boot_sector` retains those bits and warns rather than
refusing read access. Full boot/verification, FAT selection, active bitmap
loading/counting, directory traversal and extraction callers were compared before
and after this change. The existing TexFAT extension continues selecting the
declared active pair; it never guesses a replacement pair or replays a transaction.
All 21 baseline hashes were checked, including the tracked upstream license.

The extra status gate is removed. The module exposes standard ReadOnly capability
and a named BSTR archive property, Read-only limitations, containing the applicable
Linux status diagnostics. Clean images have no limitation text. Geometry, primary
and backup checksums, entry-set validation, name/path checks, bitmap allocation,
chain bounds/cycles and per-file DataError remain enforced. Neither status bit
authorizes repair, writes, a different active pair or missing decoding.

The engine ABI/process interface can retrieve the selected data and its limitation
metadata. This metadata describes volume state, not a synthetic per-file read
failure; existing operation errors and process completion checks are unchanged.
The Qt archive-opening boundary reads that public property after a successful
native Open and asks for confirmation before publishing the folder. The existing
direct-device warning is reused in a focused shared prompt component. EN/RU text
explains missing/outdated/unreadable data and no replay/cleanup/repair; Cancel is
default and Escape cancels. A persistent limited-access badge applies to these
filesystem-image folders and their descendants as well as direct devices.

Accepted limitations belong to the resource for that opening. A new opening asks
again, including after the source status changes. Source F5 relists the current
reader, so it retains the chosen mode; it does not itself reread boot status.
The explicit ReOpen path also checks current limitations, following the SDK's
ReOpenCallback wrapper to the joined operation's UI. No callback pointer is retained
beyond that operation. This is Qt/platform integration; no kernel write or VFS
service and no new filesystem decoding algorithm is introduced.

ABI coverage replaces the old dirty/media-refusal cases with nine layouts covering
one FAT and both active TexFAT pairs with either/both flags. Exact diagnostics and
payload are checked. Damaged active allocation still fails without falling back
to stale inactive data. Backend cases list, test and extract flagged images with
unchanged source bytes. EN/RU UI cases cover consent/cancel, copying, mutation
restrictions, ordinary/compact badge geometry, mode retention and a new question
on a later opening. These controlled declarations do not establish recovery of
arbitrary interrupted TexFAT transactions.

Focused ABI/backend/language checks passed on macOS, Linux and Windows. The first
remote batch `20261021T033747Z-794a8a72` passed all five suites on Linux; Windows
passed the exFAT/backend/language/new image UI cases, while device_ui stopped in
its codec-catalog setup. Final `filesystem_read_ui` and `device_ui` passed on all
three platforms: macOS Cocoa 29.03 s, Linux offscreen 6.02 s and Windows offscreen
20.67 s, remote receipt `20261021T034826Z-97ddeb89`.

The new native UI harness initially searched the wrong window hierarchy and hit
a timeout; it now handles the actual top-level progress-owned prompt. An invalid
test expectation that F5 performs a new Open was corrected from the original
source flow. Windows correctly held a read-only source lease, so fixture changes
for the later-opening case occur after closing the earlier opening. The affected
device_ui test now waits for asynchronous native catalog loading beyond QTest's
default five seconds; the production probe deadline is unchanged. Earlier Windows
diagnostics also exist in `20261021T034559Z-7b98586b`.

Native exFAT `6.8.0-147-generic` read three single-FAT images with dirty, media and
combined flags through read-only loop mounts, with exact file data and unchanged
image hashes, receipt `20261021T033536Z-6a5e1b72`. The temporary exporter reused the
existing fixture object and compiler cache; mounts, images and helper were removed,
and helper deletion was synchronized to both guests. Native TexFAT transaction
acceptance is not claimed.

Current Cocoa EN/RU JSON for both status diagnostics and the normal/compact badge
was inspected: labels/buttons are visible and no clipping was found. Application
builds passed on all three platforms, remote receipt
`20261021T034959Z-5a5a3a02`. Existing build trees/caches were retained. Full/slow
suites, physical devices, a new installed package, VoiceOver and native Windows/
Linux appearance were not checked. This closes the selected status-policy scenario;
the broader filesystem/format goal remains separate.

## Partial directory access (2026-10-23)

The expanded [read-only policy audit](read-only-access.md) found two adapter
restrictions: a malformed entry set stopped the whole namespace, and a complete
child FAT-chain precheck blocked a readable directory prefix. Compared the pinned
Linux `fs/exfat/dir.c::exfat_readdir`, entry-set/name readers and their complete
`exfat_find` consumers, `fatent.c::exfat_count_num_clusters`, and the chain helpers
in `exfat_fs.h`. The four reference files match the existing baseline hashes.
The Linux revision/repository above are unchanged; no new source snapshot or
independent filesystem decoder is introduced.

The original `exfat_readdir` continuation after a rejected entry set is restored.
A per-cursor diagnostic records skipped records; allocation failure and caller
cancellation remain fatal. Child enumeration uses that same demand-driven reader,
bounded by the declared directory size and existing geometry/cluster checks.
The extra whole-chain precheck is removed from child enumeration. Root bootstrap,
entry-set checksums, path validation, the active FAT/bitmap choice and file-data
mapping are preserved.

The archive traversal retains independently readable siblings and known prefixes
when a directory cannot be completed. A safe reference to an already encountered
directory can be read through both names; references to an ancestor and traversal
depth overflow become incomplete directories. The depth boundary follows the
existing 7-Zip FAT traversal. Item errors and the volume limitation property
describe incomplete directories. Copying such a directory reports DataError;
copying a readable file preserves its own successful result. A skipped root entry
also sets the standard archive header-error property. Whole-catalog extraction
reports HeadersError through the original SDK extended callback, or S_FALSE for a
consumer without it, after retaining extracted readable data. Source notices and
the SDK archive ABI/dependencies are unchanged.

Limited filesystem-image opens now use the retained native handler for metadata
and extraction, as direct devices already do. The callback, consent, progress and
path checks remain with their existing owners. This avoids losing an opened
partial catalogue to a second failing CLI listing or ambiguous item mapping.
Reopen follows the same path. Ordinary clean filesystem images retain their
existing transport. The existing EN/RU limited-read alert, Cancel default and
visible badge are reused; there is no additional permission mode or source write.

ABI regressions replace the former whole-open refusals for damaged entry sets,
invalid item metadata/names and a directory cycle. They check readable sibling
bytes, skipped unsafe entries, partial child-chain data, isolated source I/O
failure, incomplete-directory extraction, safe aliases and reusable opening state.
The existing cancellation, output failure, clean/TexFAT and metadata checks remain.
EN/RU UI cases cover root-entry corruption, child-chain failure and a cycle, with
cancel/accept, healthy copy, errors for incomplete copy, persistent mode, fresh
consent and unchanged source bytes.

Source transfer for this directory scenario is complete. The subsequent section
records the separate bootstrap, short-source and file-data review.

Validation: **20/20 focused suites PASS on macOS, Linux and Windows**. The set
contains exFAT ABI/backend, the new directory UI group, all existing limited-read
filesystem UI consumers, native folder opening, archive recovery and device
filesystem/folder consumers. macOS used Cocoa (352.52 s); Linux/Windows used
offscreen (248.48/386.49 s), remote receipt `20261023T031240Z-84082e34`.
The new UI group took 19.19 s on macOS and retains its normal timeout. Current
EN/RU warning and normal/compact badge JSON were inspected, without clipping.

The first directory UI attempt timed out because test automation tried closing
the parent progress window while an error alert was modal above it. The helper
now handles the active modal child first. The root extraction error is delivered
through the SDK message callback; no timeout increase or decoder workaround was
used. After the diagnosis and successful rerun, ten exact task-owned remnants of
the earlier killed/sandbox runs were removed after an open-handle check. Active
runs, unrelated fixtures, build trees, compiler caches and current reports remain.
Full/slow suites, physical devices, native Windows/Linux appearance, VoiceOver and
installed-package acceptance are not claimed.

Incremental application builds also passed on macOS and both guests. The remote
portable `sevenzip_desktop` target and updated plugin deployment passed in receipt
`20261023T032226Z-e29d75b4`; the initial `Sevenzip` bundle target name applies only
to macOS. The handler boundary was reviewed after this change: traversal,
properties and extraction share one archive ABI owner, while disk decoding remains
in C. Keeping that owner preserves the existing callback/error lifecycle.

## Partial bootstrap and file data (2026-10-23)

Compared complete pinned Linux `super.c::__exfat_fill_super`, boot verification,
`inode.c::exfat_read_root`, `cache.c::exfat_get_cluster`, `fatent.c` chain readers,
`balloc.c` bitmap load/test/count, `dir.c` label/readdir and `iomap.c` valid-size
mapping with their local callers and stream boundaries. Repository, full Linux
revision and per-file baseline above remain unchanged. The kernel root count
guards bootstrap scans against loops; file mapping walks requested data instead
of prevalidating an entire file chain. The kernel can proceed without a bitmap
during bootstrap and privately marks an unallocated root cluster. Those behaviors
support a bounded read-only subset without repair or an inactive TexFAT fallback.

The C stream adapter now separates actual source length from declared geometry.
Boot identification, supported geometry, logical/physical bounds and entry-set
validation remain mandatory. Failed boot-region verification, short input,
incomplete root chains, missing/unreadable active bitmap, an unallocated root
cluster and unavailable volume labels become explicit limitations. The original
checksum algorithm and extended-signature warning remain; no boot sector is
reconstructed. Bitmap absence never bypasses cluster-range validation. Known
unallocated non-root clusters still fail. The active FAT and bitmap remain the
only allocation pair; inactive metadata is not used to recover old payloads.

Original root counting retains the unique prefix through a host-owned visited
cluster callback. Root bitmap/label scans are bounded by that count, and namespace
enumeration retains its incomplete-root diagnostic. No readable root entries
means failed opening with a specific diagnostic, rather than an apparently empty
successful archive. The direct-device format probe retains that diagnostic after
trying its other filesystem candidates. Free space is omitted when allocation or
bootstrap metadata is unverified; an unreadable label is omitted separately.

File reads use the original demand walker, existing valid-size zero tails and
bounded stream offsets. Repeated data clusters stop at the readable prefix.
Short source data and ordinary source I/O failures report per-file DataError
after preserving delivered bytes, then allow independent files to continue.
Callback failures, cancellation, allocation failure and output errors still abort;
they cannot authorize partial opening. No source-write interface was added.

The existing limited-read prompt, default Cancel, persistent badge and fresh
consent on reopening cover these new diagnostics. EN/RU translations and native
metadata/extraction are reused. ABI regressions cover boot/checksum/signature,
root and bitmap failures, both TexFAT pairs, missing source tails, free/cyclic/EOF
file links, isolated source I/O, independent healthy extraction, cancellation,
callback failure and reusable handler state. UI regressions cover cancel/accept,
healthy copying, erroneous prefix copying and unchanged source bytes. Device
coverage checks the no-readable-root failure reason. This completes the selected
exFAT bootstrap/data scenario, not the broader filesystem/archive policy audit.

Validation: eight focused suites passed on macOS, Linux and Windows: exFAT ABI,
backend, both exFAT UI groups, device filesystem/folder, native opening and archive
recovery. macOS used Cocoa; the new recovery group took under 30 seconds and
remains a normal suite. Remote receipt `20261023T035250Z-34358a59` contains the
Linux/Windows results. The initial device regression used a non-sector-aligned
truncated fixture and was rejected by the device boundary; aligning its length
fixed the fixture, and all 55 device cases passed. EN/RU current warning JSON and
normal/compact badge geometry were inspected without clipping. Temporary fixtures
clean themselves; runner worktrees were unlocked before checks, no obsolete run
payloads were present, and incremental outputs/caches were preserved. Physical
devices, native Windows/Linux appearance, VoiceOver, installed packages and
full/slow suites are not claimed.

Incremental application builds passed on macOS and both guests; Linux/Windows
receipt `20261023T035513Z-228a4ae2`. The final source comparison retains the
original active-pair, checksum, mapping and valid-size branches; local changes
remain read-only stream/diagnostic boundaries. No runtime or build tree reset was
used.
