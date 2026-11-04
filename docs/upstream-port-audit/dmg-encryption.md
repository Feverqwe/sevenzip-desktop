# Encrypted DMG read boundary

The encrypted container is a separate GPL-2.0-or-later format module shared by
the application and official `7z`. It supplies a seekable decrypted stream to
the existing recursive engine opening path. UDIF compression, partitions and
HFS+/APFS remain owned by their existing readers. No mount, console process or
whole-image decrypted temporary file is used by this module.

## Network transport buffering — 2026-11-03

The follow-up reports video staying on its first frame while Finder loads previews,
then failing to advance on seeks. The earlier native media acceptance used a local
image and short video; the initial network workload read only one patterned file.
Neither established sustained media progress during previews from a network source.

The encrypted stream previously issued one physical source read for every crypto
chunk. Native images commonly use 4 KiB chunks: uncached SMB input then turns a
single file range into hundreds of round trips. The format's bounded per-chunk
decryption is retained, while physical ciphertext reads now use the already linked,
unchanged SDK `CCachedInStream`. A narrow local block-acquisition adapter seeks and
fills that cache from the retained input. It holds ciphertext only, starts empty
after successful password/header processing, respects physical EOF and preserves
failed-fill invalidation. No whole-image read or temporary extraction is introduced.

Before editing, complete original `DiskImageFile::Read` and setup paths were
compared with the adaptation and exFAT/UDIF/retained-stream callers. The complete
SDK `StreamObjects.cpp/.h` cache bodies, allocation dependencies and existing
format consumers were reviewed. Original units were verified against existing
baselines: `DiskImageFile.cpp` SHA256
`aaddd6b689459ce9eb80066e802fe66b3a88092b06d864402af9530e50c5f042`,
SDK `StreamObjects.cpp` SHA256
`c8ffbc43c1cc267098eafc7823b16d5351ebd989f0f175de6fdd4255c634e76d`
and `.h` SHA256
`290b8195eb151ee2217d35ad91db2d1c167a01ba357761484ac7c74864b0c773`.
The SDK units remain unchanged in their existing archived owner; no copied cache
implementation or provenance revision advance is needed.

`encrypted_dmg_network`, with `SEVENZIP_TEST_VLC` and ffmpeg, now generates a
60-second 720p video and 24 large noisy JPEGs inside the encrypted exFAT image.
The image is served through an outer SMB mount; its backing input adds a small
per-read delay to expose round-trip amplification. QuickLook and isolated VLC run
together, with repeated forward seeks. Acceptance requires distinct newly decoded
frames after all previews finish, followed by exact random-range comparisons in
the large file. The normal DMG suite also checks large seeks, short input reads,
EOF, and an uncached failure after reopening; cached bytes need not reread a source
that subsequently fails. `SEVENZIP_TEST_DMG_MODULE` selects a previous module for
the same native scenario without replacing the installed application.

The comparison reproduced the report with the previous installed module: after
eight forward seeks, VLC still had only one distinct decoded frame and QuickLook
was still running at the 45-second media deadline (67.29 s including cleanup).
With the buffered module, the same delayed-source scenario passed in 22.28 s,
including media generation, mounting, preview/video acceptance, random reads and
cleanup. This is evidence for the synthetic delayed network scenario; the user's
private image and external SMB server were not accessed. Linux's four focused
DMG/exFAT-backend/transport/session suites passed (build 8.51 s, tests 4.63 s).
macOS DMG/UI/transport/session passed in 29.08 s.
The final identical native A/B harness passed again with the current module in
22.48 s. The release app was rebuilt and its deep strict code signature verified.
Windows MSVC compilation succeeded; the first execution stopped in fixture setup
because the official engine runtime had not been prepared for this branch.
Preparing it exposed a missing standard `Archive/StdAfx.h` preamble in the earlier
HFS decmpfs stream adaptation under the native runtime's strict warning flags.
Adding the same preamble used by its owning original handler resolved that build
blocker without changing stream behavior. Windows runtime preparation and the
DMG suite then passed; macOS `hfs_stream` passed in 9.88 s. No full gate was run.

## Original source and comparison

[The baseline](../dmg-encryption-baseline.json) pins apfs-fuse
`66b86bd525e8cb90f9012543be89b1f092b75cf3`, retrievable from
`https://github.com/sgan81/apfs-fuse.git`, and hashes complete original units.
Before adaptation, `DiskImageFile.cpp/.h`, `DeviceDMG.cpp`, their structures,
callers and crypto dependencies were reviewed. `DeviceDMG::Open` confirms that
decrypted bytes may contain either UDIF or a raw disk image. The existing
7-Zip `DmgHandler::Open2`, `GetStream`, registration and recursive
`CArchiveLink`/`ArchiveNativeOpen` consumers were compared as well.

Transferred scope:

- `DiskImageFile` encryption detection, v1/v2 structures, password key-slot
  traversal, v1 reverse/double-CBC unwrap and chunk-number HMAC IV/read algorithm
  belong to `src/formats/encrypted-dmg/`. The structures are extracted into
  `dmgheaders.h`; inventory-unit hashes remain those of the complete originals.
- DES/TripleDES C++ bodies and tables belong to
  `src/dependencies/apfscrypto/`, linked through one static owner. Their original
  implementation is retained, with formatting, removal of the unused bit-print
  helper and non-elidable destruction of key schedules. The original GPL text
  remains under `third_party/apfs-fuse/`.
- Existing 7-Zip AES, HMAC-SHA1 and PBKDF2 owners replace the candidate's duplicate
  primitives. AES supports 128/192/256-bit keys; no imported C is translated.
  CMake compiles SDK C sources as C. Derivation checks cancellation before/after
  each bounded KDF call; data reads process at most one bounded chunk per call.

Omitted: file-path/ifstream and terminal password services; all debug output;
apfs-fuse UDIF/decompression/filesystem code; its duplicate AES/SHA/PBKDF2;
unused v1 integrity-key debugging. V1 does not authenticate the complete image.
V2 padding and `CKIE\0` verify key unwrap, not data integrity. Existing inner
reader checksums and extraction result reporting remain authoritative.

Adaptations reject short headers/reads, overflowing or overlapping data ranges,
invalid chunk/keyblob sizes and padding, wrapping modes and unsupported key
records before accessing their bytes. Offset/length checks use the physical
input size. IV numbering cannot wrap beyond 32 bits. Resource limits are
64 key records, 1 MiB chunks and 10 million KDF rounds per candidate.
Unsupported password slots do not prevent trying another usable slot.
Certificate/keybag unlocking, sparsebundle, FileVault/CoreStorage and encrypted
APFS volumes are outside this change.

The [wrapping reference baseline](../dmg-wrapping-reference-baseline.json) pins
`sparsebundlefs.c` at `e467aadf761ca0b55ed3972a500cc612696e9e3f` for comparison of
the modern algorithm identifier and IV convention. No C implementation or
embedded crypto from that repository is imported. Modern AES wrapping extends
the C++ key-slot flow using existing SDK AES, with full padding and marker
validation. Native macOS fixtures establish compatibility separately from that
source comparison; README claims in other candidates are not acceptance.

The local archive adapter reuses the existing `CHandlerCont` extraction and
extent-stream implementation. `ERROR_INVALID_PASSWORD` lets the application's
native-open owner reset an opening's cached password and repeat the existing
prompt. Noninteractive clients receive the error. Ordinary cancellation uses
the same callbacks and input-request scope as other readers. No callback
pointing into an opening worker is retained by the decrypting stream.
Password and unexpected-end results retain the actual Win32 HRESULT facility;
the SDK's Unix `HRESULT_FROM_WIN32` macro encodes errno and must not be used
with these Windows numeric codes. The console regression checks the resulting
password diagnostic as well as failure status.

Native HFS+ acceptance also exposed the macOS root directory-hardlink store's
trailing CR. After comparing the original `HfsHandler` catalog/name/path bodies
and the current raw-name lookup adapter, the existing bracketed private-store
display adaptation is extended to that exact root metadata directory. Raw
catalog names remain unchanged. Extraction-path validation is unchanged;
extract-all no longer fails on this standard metadata name.

## Repeatable source review

Use separate read-only reference snapshots with the recorded/proposed commits:

```sh
python3 scripts/upstream_baseline.py reference-verify \
  --manifest docs/dmg-encryption-baseline.json --upstream /tmp/apfs-reference
python3 scripts/upstream_baseline.py reference-review \
  --manifest docs/dmg-encryption-baseline.json --upstream /tmp/apfs-reference \
  --new-revision <full-commit> --output /tmp/dmg-review.md
```

The same commands accept `docs/dmg-wrapping-reference-baseline.json` and a
snapshot of its recorded repository. Review complete units and local adaptation
together; retain bounds, password, cancellation and diagnostic restrictions.

## Validation boundary

Focused suites: `encrypted_dmg` and `encrypted_dmg_ui`. Artificial fixtures cover v1 AES-128 and
v2 AES-128/256 with 3DES and AES wrapping, patterned random reads, chunk/AES
boundaries, short underlying reads, EOF, wrong passwords, cancellation, malformed
headers and I/O failures. Native macOS fixtures independently cover modern
wrapping and HFS+ recursive opening/extraction. Fixture origin and commands are
recorded beside them. V1 has no independent native-reference sample yet;
source transfer and artificial regression evidence must not be presented as
verified compatibility with every historical v1 producer.

2026-11-03, branch `codex/encrypted-dmg`, base `01409f41` with this working-tree
change: Linux x86_64 passed `encrypted_dmg`, `hfs_stream`, `native_listing`,
`native_archive_test`, `archive_open_options` and `encrypted_dmg_ui`. The final
password-protocol batch built in 19.15 s and tested in 3.46 s; the earlier
`hfs_stream` pass remains applicable to the unchanged HFS adaptation.

macOS arm64 passed `encrypted_dmg`, `hfs_stream` and the Cocoa
`encrypted_dmg_ui` scenario. The final DMG/backend/UI batch built in 132.53 s
and tested in 6.30 s, retaining the previous HFS stream evidence. Builds reused
incremental outputs and ccache. Cocoa requires access to WindowServer and
passed outside the sandbox.
It drives the existing password dialog through wrong/correct/Cancel and copies
the Unicode-named file from the actual application window. The backend suite
also exercises the official `7z` consumer and ordinary unencrypted UDIF.
These are focused behavior checks, not rendered Windows pixel-parity evidence.
Windows, separate APFS/GPT-content fixtures, full gates and slow suites were not
run. The existing inner readers were reused; this change does not certify new
filesystem features or encrypted APFS/FileVault support.

## Mounted access follow-up

The follow-up compares the complete recorded 7-Zip `ApfsHandler.cpp` unit's
`GetStream`, `Extract`, `GetAttrStream`, inode flags and decmpfs attributes with
the current HFS handler and shared decoder. The original APFS `GetStream` exposes
an empty data fork for decmpfs files; HFS explicitly declines compressed streams.
The APFS unit now belongs to `src/formats/apfs/`, retaining its original baseline
hash and source language. Include paths and the compressed-stream boundary are
adapted; ordinary extent, attribute and extraction algorithms are retained.

`HfsDecmpfsStream.cpp` adapts the original resource-map validation and independent
64 KiB chunk layout into a seekable boundary. Its original inventory owner remains
the complete `HfsHandler.cpp` baseline unit. Sequential extraction and random reads
share the transferred block decoder in `HfsDecmpfs.cpp`; no second codec is added.
Inline units decode lazily into bounded memory; resource streams read only the
requested block. Inline units larger than one block and unsupported methods retain
the existing extraction fallback. This is not encrypted APFS-volume support.

The local mount source retains one extraction decision for its lifetime, including
denial and cache eviction. The generic read-only view still requests consent for
each materialization; other consumers retain their existing contract. Recovery of
unverified output remains a separate per-file decision. EN/RU prompt and Help text
describe the mount scope and remount boundary. This is project-owned Qt/platform
integration with no original Windows 7zFM dialog counterpart.

Follow-up validation, 2026-11-03: Linux x86_64 passed `hfs_stream`,
`archive_readonly_view`, `archive_mount_transport`, `mount_extraction_prompt`,
`encrypted_dmg`, `encrypted_dmg_ui` and `device_filesystem` (incremental build
10.41 s, tests 14.37 s). The transport-only eviction refinement also passed
(build 12.58 s, tests 3.70 s). macOS arm64 passed the five changed reader/UI/
transport suites (tests 9.75 s), then Cocoa geometry and one native manager mount;
see the [mount evidence](readonly-smb.md#mount-scoped-extraction-decision--2026-11-03).
The encrypted APFS fixture is tested with zero extraction-cache capacity; HFS
resource tests cover out-of-order block reads, EOF and underlying I/O failure.
Full gates, slow suites and Windows were not run for this focused follow-up.
The macOS release was rebuilt with existing incremental outputs and installed in
the feature branch's release directory. Deep strict signature verification and
the installed official `7z` tests of encrypted HFS+ and compressed APFS fixtures
passed. Disposable follow-up fixture scratch was removed; source fixtures,
current geometry evidence, logs, incremental outputs and compiler caches remain.

## Standard encrypted UDRO/UDZO follow-up

The user still reports extraction/cache-limit failures on an unavailable private
image made from a folder with native read-only encryption. That specific failure
has not been reproduced. It must not be reported as resolved by the earlier
decmpfs change or by these synthetic fixtures.

Fresh native `hdiutil create -srcfolder ... -format UDRO/UDZO -encryption AES-256`
images pass direct reads with a one-byte extraction quota. The complete Cocoa
window → Mount command → native SMB filesystem path also reads a 4 MiB file from
both variants with no extraction prompt and no cache controls. These opt-in native
cases clean up only their test mounts. They do not require the user's image.

The UDRO case exposed a separate short-read integration defect: a valid encrypted
input returns one encryption block per call; propagating each short read as a
network reply made the 4 MiB native read take about 100 s. The local mounted-view
boundary now fills each bounded requested range, checking errors and cancellation
on every underlying read. The native streams and their block algorithms are
unchanged. A portable encrypted raw-image regression covers range boundaries,
EOF and cancellation. The native window cases together pass in 16.29 s, including
image creation and mounting.

When a mount still falls back and fails, details now retain the native reader
chain, method, GetStream result and whether a sequential stream was returned.
This avoids inferring missing direct access from the cache-limit message alone.
No private image path or password is needed to report those diagnostic fields.

2026-11-03 validation: Linux `encrypted_dmg`, `encrypted_dmg_ui`,
`archive_readonly_view`, `archive_mount_transport` passed (build 13.48 s,
tests 3.94 s). macOS reader/view/transport batch passed (15.96 s); the two actual
native Cocoa mount cases passed separately. Full gates, slow suites and Windows
were not run. The remaining user-specific cache fallback needs its new diagnostic
fields or a reproducible artificial fixture before acceptance.

## Reported exFAT chain resolved

The user supplied `reader=exFAT.Dmg.EncryptedDmg; GetStream=0x00000000;
sequential stream=no`. The local exFAT module exposed no native stream interface,
so the generic adapter returned no stream and mounting fell back to extraction.
The [exFAT stream follow-up](exfat-linux.md#seekable-files-for-mounted-access--2026-11-03)
adds that missing boundary using the existing reader. This identifies and resolves
the source-visible cause of the reported fallback; the private image was not read.

Linux x86_64 passed `exfat`, `exfat_backend`, `encrypted_dmg`, `encrypted_dmg_ui`
and `archive_mount_transport` (build 11.96 s, tests 3.95 s). The final interleaved
stream regression also passed in the exFAT-only rerun. macOS arm64 passed exFAT,
backend and DMG suites (26.94 s), then the actual Cocoa Mount-command scenarios
for encrypted exFAT UDRO and UDZO (14.73 s including native image generation).
Both read the 4 MiB synthetic payload without extraction prompts or cache controls.
The macOS release was rebuilt with existing caches. Full gates, slow suites and
Windows were not run for this focused fix.
