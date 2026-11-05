# Apple sparse image containers

The application and official `7z` use one read-only native implementation for
`SparseImage` and `SparseBundle`, deployed with the encrypted-DMG format module.
It exposes seekable disk bytes to the existing partition, HFS+ and APFS readers.
There is no runtime mount, console conversion, band concatenation or plaintext
image spool. Filesystem support is unchanged; reading a Time Machine container
does not establish support for all backup snapshots or encrypted APFS volumes.

## Original inventory and transferred scope

The [apfs-fuse baseline](../dmg-encryption-baseline.json) pins retrievable commit
`66b86bd525e8cb90f9012543be89b1f092b75cf3`. Complete `DeviceSparseImage.cpp/.h`
and `Device.cpp/.h` units were reviewed alongside `DiskImageFile.cpp/.h` and
their callers. The header/index slot scans, physical slot mapping and sparse
read traversal from `DeviceSparseImage::Open/Read` are adapted in
`src/formats/sparse-image/sparseimagestream.cpp/.h`. The source remains C++.
The existing GPL-2.0-or-later license stays under `third_party/apfs-fuse/`.

The [sparsebundle baseline](../sparsebundle-baseline.json) pins complete
`src/sparsebundlefs/sparsebundlefs.c/.h` at retrievable
`https://github.com/jief666/sparsebundlefs-lib.git` commit
`e467aadf761ca0b55ed3972a500cc612696e9e3f`.
`sparsebundle_iterate_bands` supplies the band-number, within-band and
cross-boundary iteration in `bandmap.c/.h`; imported C stays compiled as C.
The Tor Arne Vestbø/Jief Luce BSD-2-Clause notice is retained in the source and
`third_party/sparsebundlefs/LICENSE`.

Reviewed but omitted: POSIX paths/FDs, pthread/global caches, FUSE, terminal
passwords, process exits, diagnostic/key dumps, permissive error-to-zero
conversion and all embedded/OpenSSL cryptography. The separate native adapter
owns callback lifetimes, bounded plist parsing, generated band paths and stream
errors. It reuses unchanged SDK `MyXml`, `StringToInt` and stream/cache owners;
their complete original units remain pinned by the existing 7-Zip baseline.
No independent filesystem, UDIF, KDF, AES, HMAC or DES implementation is added.

Both manifests support the existing `reference-verify` and `reference-review`
commands described in [the update procedure](../upstream-update.md). Run them
against separate read-only Git snapshots at the recorded/proposed revisions.
The wrapping reference manifest remains reference-only for that separate
comparison; the new sparsebundle manifest records the actual partial C import.
Original inventory hashes pin upstream bytes, independently of local edits.

## Adaptation boundaries and source-visible defects

For sparseimage, `DeviceSparseImage::Open` establishes decrypt-before-index
mapping. The shared encrypted-DMG stream therefore remains the outer container;
plain sparseimage starts directly at the sparse index. Header versions 1 and 3
are accepted. The original fixed `offs >> 20`, unchecked reads/index IDs and
wrong `hdr.signature` check inside the index loop are replaced with variable
band division, propagated errors and checked index signatures/chains. Bounds,
duplicates, index/data overlap, cycles, arithmetic and seek limits are checked
before consuming data. Unallocated logical slots return zeroes.

For sparsebundle, `sparsebundlefs_open`, `v2_read_token`, password unwrap,
`compute_iv`, encrypted/plain band readers and complete iteration bodies were
compared with the shared DMG reader. Native fixtures confirm that token v2 has
zero data length and a separate physical header extent. Size/band geometry comes
from `Info.plist`; cipher chunk numbering restarts inside each band. Shared key
setup is retained once per opening, with per-band read-only streams and the
existing ciphertext buffering. AES-128/256 data and 3DES/AES key wrapping use
the same authoritative crypto owners as encrypted DMG.

The local CArc/Agent/panel directory gates, volume callbacks, typed backend and
read-only-view consumers were compared with their complete pinned 7-Zip units.
A package path opens `Info.plist` while retaining the original container path.
The retained volume callback permits only token and generated hexadecimal band
paths after recursive opening. Package/bands directories and band symlinks are
validated; missing source directories, access failures and I/O errors are not
sparse holes. The normal volume safety checks remain active. Existing macOS
SMB `F_NOCACHE` handling is reused by every band handle, and physical cipher
reads still use `CCachedInStream` rather than one transport read per AES chunk.
The bundle owns a bounded LRU of independent band handles and cipher caches,
retaining useful buffering when metadata and data reads alternate. Package
handles bypass the ordinary multi-volume inventory so repeated band evictions
do not grow retained volume records. Failed read handles are discarded, allowing
a later request to reopen after a transient transport failure. Opening UI
callbacks are released at the end of opening; retained image streams do not
dereference the former password/progress UI.

The official runtime includes the same adapted volume callback and its existing
typed fscrypt interface dependency. This preserves one callback implementation
for the application and console rather than a separate package-file provider.
The COM extension uses the SDK's `novtable` declaration convention; builds
without RTTI retain the generic password callback rather than casting a UI
extension that the standalone console does not implement.

Metadata support uses the native producer's XML `Info.plist` and v2 encryption
token. Binary plists, reconstructing missing metadata from `Info.bckup`, and
certificate/keybag unlocking are not implemented. Package contents are expected
to remain stable during an opening; bounded caches do not provide a filesystem
snapshot or guarantee consistency with concurrent writers on an SMB share.

## Damaged input

An absent band and an unwritten band tail are sparse zeroes by the format's
contract. A wholly deleted allocated band cannot be distinguished from a hole;
neither the index-free bundle nor its encryption authenticates that distinction.
Readable blocks in other bands remain accessible after a band error. Complete
CBC prefixes before a torn AES block can be read; damaged tail requests fail
instead of synthesizing ciphertext or verified plaintext. Wrong passwords retain
the existing retry protocol and are separate from data damage.

Detected torn encrypted bands publish `Read-only limitations` on the outer
container. The existing filesystem consent/persistent limited-mode path presents
the warning when detected during opening. Later per-file failures retain the
reader's named extraction errors; a recovered partial source-entry output is
reported as incomplete, never successful. The source is never repaired or
written. Corrupt key metadata, unavailable passwords and unreadable filesystem
metadata cannot be bypassed by consent.

## Large APFS backup follow-up — 2026-11-04

The first small native fixtures did not cover scattered APFS metadata or object
maps retaining several transactions for one object. A reported Time Machine
opening exposed both gaps. The former transport cache fetched a large window on
every miss, amplifying a small metadata request by up to 1024 times. The shared
DMG adapter now uses small SDK cache blocks and starts bounded read-ahead only
after a contiguous miss sequence. The SDK cache implementation and crypto chunk
boundaries remain unchanged. A failed speculative fill retries the requested
extent; abort and allocation errors remain authoritative.

The regression reads 64 KiB of scattered metadata, validates every byte and
counts physical bytes as well as calls. It failed before the change and now
transfers 512 KiB on macOS. Sequential transport, source failures, cancellation,
damaged-band access and the real native SMB scenario still pass. This measures
synthetic metadata amplification, not the private backup's end-to-end open time.

The existing SDK APFS `CObjectMap::Parse` rejected repeated object identifiers
and left transaction selection as a source TODO. Its adapted owner now validates
the full identifier/transaction ordering and retains the newest version visible
to the selected container/volume superblock. Future versions are excluded and
deletion markers cannot revive an older mapping. A failed volume object-map read
also propagates its HRESULT instead of consuming a partially populated map.
The recorded SDK unit/revision/hash are unchanged. Formatting is limited to the
changed object-map unit. No external APFS decoder or GPL implementation was
imported into the core; the pinned apfs-fuse node-mapper was inspected as a
comparison reference only.

Constructed checksummed derivatives of the existing artificial native APFS device
fixture reproduce the pre-fix refusal, read the expected file with older/current/
future versions, and reject deletion/order failures. Their provenance is in
`tests/fixtures/sparse-images/apfs-versions.json`. The readable derivative also
opens inside an encrypted sparsebundle with a 16 TB sparse address space without
materializing that capacity. Ordinary native case-sensitive APFS opening was
checked separately. Native snapshot creation required privileges unavailable to
this test run; these derivatives do not establish complete Time Machine snapshot
history support or acceptance of the user's private backup.

## APFS directory browsing — 2026-11-04

Compared the recorded SDK `ApfsHandler.cpp` bodies and dependencies for `ReadMap`,
`ReadObjectMap`, `OpenVolume`, `FillRefs`, parent/path projection and extraction,
along with `OpenArchive`, Agent and proxy consumers. The existing B-tree reader,
inode/attribute/extent decoders, integrity checks and file streams remain
authoritative. The local adaptation adds bounded object-ID range traversal and
on-demand object-map resolution around those bodies; it does not introduce a
second filesystem decoder. Sealed-volume extent selection uses the existing
FEXT decoder, with range selection for requested file IDs. Per-call node storage
prevents nested object-map lookups from overwriting an active filesystem node.
Ghost-node enumeration and whole-tree count validation are deliberately omitted
for partial directory slices, where their full-tree assumptions do not hold.
Selected records retain the original bounds, block/hash and decode validation.

The optional `IArchiveDirectoryAccess` contract and private `apfsdirectory.inl`
are project integration, recorded as runtime dependencies in the existing
[7-Zip baseline](../upstream-baseline.json). Original inventory hashes and the
pinned upstream revision do not change. New source Agent/resource owners keep
the same native reader behind `ArchiveBackend`; default complete enumeration
for backend and console callers remains available. No new external source is
imported. The shared vector owner gains an allocation-free storage swap used to
publish a freshly constructed proxy without invalidating existing folder
objects. Native indexes append, and proxy folder bindings, including alternate
streams, retain their identities. Failed directory loads discard the unpublished
slice; already completed directories from an interrupted recursive operation
remain readable. Reopening through Refresh preserves the requested directory mode.

Ordinary panel browsing requests one directory at a time through a joined native
worker with the existing progress/cancellation lifecycle. Folder aggregate
properties remain undefined until the subtree is complete. Copying selected
directories expands only those subtrees before collecting extraction indexes.
Flat View, explicit complete extraction and the read-only export projection
request recursive metadata enumeration. They are not lazy exports. Traversed
metadata is retained for the lifetime of the opening, rather than evicted.

The artificial native case-sensitive APFS fixture has 12 directories with 128
files each, a nested Unicode filename, a resource fork, hardlink, symlink and an
empty directory. Its producer, uncompressed hash and payloads are recorded in
`tests/fixtures/sparse-images/apfs-directories.json`. `apfs_directory` compares
lazy and complete listings, verifies stable file reads after expansion and exact
recursive path equality, injects a deferred metadata failure and retries it, and
cancels a directory read before publication. The small fixture establishes
deferred traversal and reduced root I/O, not a large-backup throughput claim.
`sparse_image_ui` exercises actual panel navigation and selected-subtree copying
through encrypted sparsebundles, including nested Unicode and hardlink data.

Focused Linux x86_64 checks passed for `apfs_directory`, `sparse_image`,
`sparse_image_ui`, `encrypted_dmg`, `source_extract_backend` and
`device_filesystem`. macOS ARM64 passed `apfs_directory`, `sparse_image_ui`,
`source_extract_backend`, `device_filesystem` and `sparse_image_network`.
The latter also opens the encrypted artificial APFS bundle through a real native
SMB mount in two independent readers, checks the initial partial listing, then
validates full-export file hashes. No private backup was read for these checks.
This directory-browsing change has no Windows runtime acceptance, native
multi-volume fixture or sealed-volume runtime fixture. Existing snapshot and
internal-encryption limitations remain; the source changes alone do not establish
complete Time Machine history support or macOS-equivalent opening latency.

### Size command availability follows pending directory reads — 2026-11-05

Reviewed the complete Qt availability/invocation path, native directory-index
mapping, normal/flat property presentation and optional directory-access service.
Compared complete original GetRealIndex and GetProperty bodies with current
Agent adapters at `b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>; the full original Agent.cpp hash matches the
existing baseline. Native mapping, property values, proxy aggregation and readers
are unchanged; no external source or revision is imported.

Availability now asks the existing Agent subtree-completeness service for a
valid native directory index. The former empty-size-property heuristic also
enabled a read request for complete archives and synthetic Flat View folders.
This removes presentation metadata from command capability decisions without
a new scanner, controller, state or public interface. Pending APFS reads remain
actionable; completed or unsupported directory reads are not offered.

The complete command regression suite now owns the four source-panel/Cancel
cases previously embedded in sparse_image_ui. It adds real ZIP/7z normal/flat
round trips and completed APFS Flat View checks, using existing native runtimes
and the recorded raw APFS fixture. Archive/image bytes, selections, address
drafts and cancellation retry remain checked. The remaining sparse-image suite
matches its pre-extraction bodies; no behavior coverage is discarded.

Before the fix, all six normal/flat command cases failed on the redundant
enabled action. Focused Linux archive_directory_command and sparse_image_ui
then passed 30 Qt checks without skips. Reader error/cancellation evidence from
the preceding unchanged apfs_directory run remains applicable. No secondary OS,
full/slow gate or new appearance acceptance is claimed.

### Size command publication stays with its initiating panel — 2026-11-05

Reviewed the complete Qt size-command consumer, joined directory worker,
progress transport, selection restoration and panel projection. Compared the
original CPanel::EditItem size branch, CProgressDialog::Create and complete
CProxyArc2::CalculateSizes with their current bodies and callers at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Complete PanelItems.cpp, ProgressDialog2.cpp
and AgentProxy.cpp original hashes match the baseline. No native algorithms,
imports or revisions change; the explicit size command is local Qt integration.

The command now retains its panel index before the modal read and uses the
existing scoped panel projection for the complete result publication: row
refresh, selection restoration, error reporting and action refresh. This removes
the dependency on whichever panel receives focus when progress closes. The
original no-address-update refresh and return to the peer projection remain;
no new operation state, controller, worker or cancellation policy is introduced.
Cancelled reads still publish usable results from the retained reader.

Four regressions use the existing APFS fixture as a disposable raw image and
cover either source panel with success or early native cancellation. Progress
close restores peer focus; the checks retain both address drafts, native folder
locations and selections, verify retry after cancellation, completed selected
size versus unknown sibling size, and byte-for-byte source preservation. They
do not require encryption, a mounted filesystem or a user image.

Focused Linux sparse_image_ui and apfs_directory passed all 31 Qt checks without
skips, including deferred read failure/retry and cancelled directory traversal.
The source-visible ownership gap was fixed before the acceptance run; no pre-fix
runtime failure is claimed. Native readers, progress controls and platform
services are unchanged; no Windows/macOS, full/slow or new appearance acceptance
is claimed.

### Size calculation, Copy and mount preparation follow-up

The size command's result publication now uses the common bound-folder
publisher, retaining its no-address-update request and selected state. The
archive renderer therefore runs under the same call/state ownership as ordinary
archive publication. The command dispatcher already holds a call scope through
the joined read and publication; the command's extra native timer guard is
unnecessary because the Qt poller excludes active source calls. The native
notification guard still protects the worker's mutable proxy. Source-panel
projection, partial results on Cancel, diagnostics and retry remain unchanged.
The complete worker, Agent directory expansion/proxy refresh and existing
publication consumers were reviewed with the original `CPanel::EditItem` size
branch at the recorded baseline. No reader or source algorithm is replaced.

The explicit **Calculate folder size** command uses the same retained source
Agent, selected-directory expansion and existing `CProxyArc2::CalculateSizes`
aggregation. The command and toolbar binding are local Qt integration. Reviewed
the existing `CPanel::EditItem`, filesystem `CalcItemFullSize`, native Agent
selection, proxy aggregation and command availability owners before adding it.
The explicit worker keeps the opening UI lifetime/cancellation contract and
refreshes the panel's projection while retaining its selection. No separate
size scanner or decoder is added; unknown siblings remain unknown.

Source comparison of Agent `Extract`, `GetRealIndices` and proxy recursion with
the existing typed directory extractor found that ordinary Copy still requested
implicit alternate streams in the source path. A macOS APFS attribute could
therefore appear as an ordinary `filename:attribute` output (Finder displays a
colon as a slash). The source path now follows the same literal Copy selection
as the typed extractor. Explicitly selected streams remain selectable; Move,
archive staging and test operations retain their requested stream scope.
This does not implement restoration of APFS attributes as destination xattrs.

Read-only-view initialization previously expanded the APFS archive root before
filtering to the mount folder. A selected, already published directory now
scopes native recursive enumeration to that subtree. Complete root export
still enumerates the root. Preparation and direct-read probing publish a
mutex-protected progress snapshot; the GUI timer displays stage, count when
known, current entry and elapsed time. Initial discovery remains indeterminate,
and the existing cancellation path still joins the worker before releasing it.
The new mount controls are project UI, with no original Windows dialog reference;
the layout follows existing Qt dialog controls and the local macOS HIG summary
for progress/Cancel (the current Apple page required JavaScript in the reader).

Regressions cover selected-subtree export without reading a sibling, Copy of a
native APFS file with a resource fork without extra files, an explicit size
request changing the previously unknown row size while a sibling remains
unknown, and responsive EN/RU mount preparation/Cancel at normal and compact
widths. Linux x86_64 and macOS ARM64 passed `apfs_directory`, `sparse_image_ui`,
`archive_readonly_view`, `mount_extraction_prompt`, `source_extract_backend` and
`source_extract_contract`; `agent_property_projection` also passed after the
shared-reader/proxy completeness guard. macOS additionally passed
`sparse_image_network`. Current Cocoa JSON for `mount-preparing-*` and
`apfs-size-*` confirms EN/RU normal/compact bounds, reachable Cancel and disabled
Mount during preparation. The native help-key check now sends Qt's platform
HelpContents sequence, rather than assuming F1 on Cocoa. No rendered Windows
acceptance or private-image latency measurement is claimed.

### Lazy SMB mount follow-up

SMB mounting now requests a lazy view, superseding the earlier complete-tree
mount preparation above. Explicit full-view consumers retain their default
enumeration contract. The SDK-derived APFS reader exposes stable direct-child
indexes and its retained filesystem input through the optional project interface.
These are native integration methods, not a new decoder. A mount opens a separate
instance of the same handler over the already decoded input, retaining the
original stream/key/module owner through the existing synchronized cursor.
This keeps background directory growth separate from the panel's metadata and
does not require a new password or a decrypted image spool. Stream-only sources
use the actual native handler input rather than assuming CArc retains a file
input. The existing manifest records the new private integration source.

Root preparation does not enumerate children or probe all file streams. The
selected root's ancestors alone are resolved. A directory is projected on demand
and committed after validation; failed reads remain retryable, successful lists
are cached. Cache settings remain available for later files requiring decoding.
The decoder still reads one directory as a whole; this change does not add
incremental APFS record pagination or guarantee immediate Finder navigation
cancellation. Protocol adaptation and source/runtime evidence are recorded in
the [SMB owner audit](readonly-smb.md#lazy-apfs-mount-directories--2026-11-04).

### Recursive size/copy metadata amplification follow-up

A subsequent report compared Finder's fast size result for a mounted
`node_modules` with slow size/copy preparation in the application. Finder and
the panel have isolated directory metadata, and Finder can reuse its own lists;
the panel also publishes its proxy representation before extracting. The report
alone does not establish whether Finder used a cold complete traversal.

The artificial native directory fixture did reproduce an independent concrete
defect: the directory workflow requested 15,306,770 input bytes, compared with
557,056 for the complete eager reader. On-demand B-tree queries repeatedly
reread the same metadata pages. The adapted reader now reuses unchanged SDK
`CCachedInStream` around exact metadata-block reads during one directory
operation. Complete original `StreamObjects.cpp/.h` bodies, callers, allocation,
seek, read and failed-fill paths were compared; their existing original
inventory/hash remains authoritative. No cache algorithm or filesystem decoder
is imported or independently replaced. The local wrapper is scoped to the
operation, restores the prior metadata input on failure/Cancel and keeps source
error retry independent of that operation's cached pages. Small attribute/data
reads remain on their original input, without added speculative page reads.
All existing parser/hash/bounds checks still run on cached metadata.

The same workflow subsequently requested 1,335,314 input bytes (about 11 times
less). This measures reader input requests on synthetic data, not network wire
traffic or an end-to-end NAS speedup. A regression bounds amplification relative
to the same fixture's full eager traversal while retaining exact path/data,
failure/retry and cancellation checks. The source Agent also avoids rebuilding
its complete proxy before copying a subtree when native item indexes/counts
have not changed; already known aggregate values remain valid.

Linux x86_64 passed `apfs_directory`, `sparse_image_ui`,
`agent_property_projection` and `source_extract_backend`; the native/UI suites
were repeated after limiting the cache to exact metadata reads. macOS ARM64
passed `apfs_directory`, `sparse_image_ui` and `sparse_image_network`, including
encrypted APFS over the native SMB source/export scenario. The focused macOS
build took 3.50 s and test execution 36.63 s; these are separate from the input
byte measurement above. The ordinary incremental macOS release package builds.
The user's private `node_modules` was not used as a fixture or timed here.

## Runtime evidence

Native reference fixtures were produced by `hdiutil` on macOS 27.2 (26B123).
[Fixture provenance](../../tests/fixtures/sparse-images/provenance.json) records
the exact creator, original members and independent read-only attached-device
range hashes. Ordinary/AES-128/AES-256 variants exist for both containers.
Modern native password slots use AES wrapping. Older 3DES wrapping has
deterministic constructed-fixture evidence; no historical native-producer
sample has been accepted. Current native sparseimage fixtures have version 3.

Focused checks on 2026-11-04 establish the following runtime evidence:

- Linux x86_64: `sparse_image`, `sparse_image_ui`, `encrypted_dmg`,
  `archive_open_options`, `open_callback`, `source_extract_backend` and
  `ext_fscrypt_backend` passed through the configured runner. Checks cover native
  reference hashes, password retry/cancel, retained stream lifetime, sparse
  holes, malformed indexes, source errors, bounded transport reads when switching
  bands, and reopening a failed band after a transient error. Damaged encrypted
  bands preserve readable neighboring files and partial output reports failure.
- macOS ARM64: the same native reader/backend checks passed. Cocoa UI checks
  open/copy ordinary and encrypted images, accept/decline damaged reads, and
  validate visible EN/RU warning labels/buttons at normal/minimum sizes. Current
  widget JSON was inspected; this is not rendered Windows pixel-parity evidence.
- macOS native SMB: `sparse_image_network` passed for ordinary and AES-256 native
  sparsebundles residing on a real system SMB mount served by the existing
  read-only archive mount transport. Two independent openings check repeated
  random disk ranges against attached-device hashes and extracted file hashes,
  including Unicode names and spaces. No whole-image extraction consent is
  requested. This is loopback SMB evidence, not an external NAS latency benchmark
  or a claim of uninterrupted reads after a disconnected share.
- Windows x86_64: `sparse_image`, `encrypted_dmg`, `archive_open_options`,
  `open_callback`, `source_extract_backend` and `ext_fscrypt_backend` passed
  through the configured runner, including the Win32 file/package callback,
  native fixture hashes and the official console consumer. No Windows native
  SMB-source or rendered UI acceptance is claimed by these checks.

The official console checks consume the package's `Info.plist`; console argument
enumeration of a directory as an archive remains unchanged. Source comparison
and runtime evidence are separate acceptance boundaries. FileVault/CoreStorage,
ASIF, APFS encryption, writing, journal replay and repair remain outside scope.
