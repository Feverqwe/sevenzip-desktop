# Selected native reader transfer

Source branch: `local-dev` at `d439ffdf29a0e3960942af7449aef2609b7c5761`.
Target baseline: `0d997ccad5e2500813aaf5f999500e631e76f404`.
This record covers the selected transfer requested on 2026-10-23; universal
extraction replay and a blanket restriction on nested editing are excluded.

## Progress controls

The completed transfer is recorded in [progress controls](progress-controls.md).

## Native enumeration and testing

### Read-callback item ownership — 2026-11-04

Rechecked the current read worker, Test/single-entry dispatch, callbacks and
backend diagnostic/publication consumers against the original item/result and
block-error projection in `ArchiveExtractCallback.cpp` at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. The complete original unit hash
matches the existing baseline; no external source or revision is imported.

The Qt callback now retains the current item path, encryption flag and index in
one record. Metadata reads prepare that record before publishing it under the
existing state mutex. Progress snapshots read its path; operation-result
delivery updates the selected output result and appends any item error within
one critical section. Explicit indexed/block reports use the same local record
shape while preserving their distinct index rules and block labels.

Native GetItem/encryption queries, skipped solid siblings, output-index checks,
error codes, password/memory/cancel services and joined backend publication
remain unchanged. No decoder or format implementation is replaced. Existing
Test and single-entry extraction regressions cover the observable contracts;
no private-record or source-presence test is added.

Sources: `3e276e74cc04b80c11c1276827ed2f9640b6f3eb`,
`ae78a45a02f17a04c88a3514558f55cdba29aab6` and
`8c0c78acaecd82060dbc0ca2eaefe12ef2ecc2e1`, using their final combined
`QtArchiveReadOperation` rather than intermediate workers. Upstream source:
<https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`,
`CPP/7zip/UI/Common/OpenArchive.cpp` (`CArchiveLink::Open`, `CArc` properties),
`ArchiveExtractCallback.cpp` (`GetStream`, `SetOperationResult`,
`ReportExtractResult`, `RequestMemoryUse`), `CPP/7zip/UI/Agent/Agent.cpp`
and `CPP/7zip/UI/Console/List.cpp` (`GetPropName`, `PrintTime`,
`PrintItemInfo`, `PrintPropertyPair2`, `Print_OpenArchive_Props`).
The test-callback projection in the source branch originally cites
`b88c6bc09339de32f7d18bbab900686b244f42ba`; the relevant bodies and dependencies
were compared again with the available 9128 baseline.

Copied the Console property-name table and retained its technical property/time,
attribute and raw NT metadata conversion. Omitted console drivers, formatting
and output transport. Qt owns typed snapshots and worker delivery. Open/Extract,
format detection, item mappings and decoding remain official engine operations.
The worker uses original item/block error projection, an operation-scoped
password service and cooperative pause/cancel checkpoints; memory requests keep
the already allowed engine budget. No memory-limit consent is invented for the
standalone non-dialog API. Open diagnostics do not turn a partial test into a
successful one, and incomplete/cancelled listing snapshots are not published.

Folder navigation keeps the native catalog/reader instead of reopening through
CLI text listing. This retains distinct indexes for duplicate member names.
The Console projection belongs only to `adapted`, not `runtime_adapted`:
the official runtime retains its original console module. No-catalog read
consumers now acquire native providers as recorded below. Materialized nested editing remains enabled;
automatic direct container chains retain their existing update boundary.

Focused runtime acceptance on 2026-10-23: macOS passed native listing/test,
opening/options, native folder and RAR-memory consumers, update and nested
editing scenarios. The complete application target also built successfully.
Linux passed the 13-suite selected set in `20261023T181315Z-e4d9023f`;
Windows passed that set in desktop session `20261023T181906Z-5145d185`.
The Windows SSH session-0 attempt was not accepted: codec discovery and archive
progress required the desktop repeat. Test-only corrections cover canonical
macOS filenames, actual native indexes, unavailable CLI and repeated dismissal
of source error dialogs; existing nested-edit behavior checks are retained.
No slow suites or full gate were run. These checks accept file-based native
enumeration/testing; direct stream chains and retained generations are separate
scope below and are not established by this result.

The FileManager opening adapter preserves `S_FALSE` from optional layer-context
metadata as "no engine-owned context", rather than treating it as a failed
archive opening. Failed HRESULTs still abort before navigation state changes.
The original `PanelItemOpen.cpp::OpenAsArc` opening/parent-chain body was compared
again; upstream has no separate layer-context metadata interface. This local
adapter does not change native archive-open `S_FALSE` or format recognition.

## QCOW

Transferred the QCOW portion of
`d439ffdf29a0e3960942af7449aef2609b7c5761`. Upstream reference:
<https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`,
`CPP/7zip/Archive/QcowHandler.cpp`, `HandlerCont.cpp/.h`,
`CPP/7zip/Archive/SquashfsHandler.cpp`, `C/ZstdDec.c/.h` and
`CPP/7zip/Common/CWrappers.cpp/.h`.

Compared the complete QCOW handler, image handler callers, tables, stream and
decoder dependencies. Formatting is separate from functional adaptation:
the handler retains upstream mappings, Deflate, sparse/zero reads and bounds
checks. The additional Zstandard cluster path uses the official caller-buffer
decoder, stops at the frame boundary before sector padding, and requires a full
cluster without extra output. The initial transfer retained limitations for
external data, backing files and encryption. Subsequent read-side updates,
including extended L2 and dependent files, are recorded below.

One SDK adaptation is registered in both `adapted` and `runtime_adapted` in the
baseline manifest. Normal runtime preparation overlays that source onto the
official engine build; no second QCOW implementation is introduced. The existing
official decoder and LGPL notices remain authoritative. Test images use the
existing disposable MBR/ext4 source; the Zstandard image was produced by QEMU.

Runtime evidence: macOS, Linux and Windows `archive_qcow` pass all nine cases, including
stored/Deflate/Zstandard source-byte comparison, sparse tail and backward reads,
unsupported layout rejection and corrupted cluster failure. Linux and Windows
receipts are the focused runs above. Retained stream-chain validation is recorded
below.

### QEMU read-side update — 2026-10-24

Reference: <https://github.com/qemu/qemu.git>, full commit
`b6c4b7e86dec12d07f5cac65265b986d38ee9002`, `docs/interop/qcow2.rst`,
`block/qcow2.h` (`QCow2ClusterType`, `QCow2SubclusterType`,
`qcow2_get_cluster_type`, `qcow2_get_subcluster_type`), `block/qcow2.c`
(`validate_compression_type`, `qcow2_do_open`) and `block/qcow2-cluster.c`
(cluster lookup callers). Compared header fields, active L1/L2 mapping,
standard/compressed descriptors, subcluster classification, corruption handling,
dependencies and omitted block services. Official 7-Zip HEAD
`b88c6bc09339de32f7d18bbab900686b244f42ba` has the same QCOW handler as our
9128 baseline; it provides no extended-L2 implementation to transfer.

[The QEMU baseline](../qcow-qemu-baseline.json) pins SHA256 values for these
complete original units and maps the adapted header to its local owner, using
the same reference model as Linux. The
[update workflow](../upstream-update.md#external-reference-baselines)
verifies the snapshot and produces upstream/local-difference reports without
overwriting the partial port. This inventory records reference-only callers
and specification files as well as the transferred header; it does not claim
they are compiled into the product.
Baseline tooling validation: the four hashes were verified against the official
QEMU Git commit; twelve `upstream_baseline` regressions passed on macOS, including
reference changes/deletions, local-fix preservation and original-hash mismatch.

Adapted the original inline classification bodies and required enums/masks in
`src/dependencies/qemu/qcow2.h`, preserving QEMU's path, symbols, branches and MIT notice.
The C-compatible header remains an inline header: block-graph state/locks are
replaced by explicit flags and GLib assertions by standard assertions. No C
translation unit is converted to C++. Omitted QEMU allocation, mutation,
coroutines, block graph, backing/external-file I/O, encryption, snapshots and
refcount repair. The SDK handler remains the sole QCOW reader, using the
existing 7-Zip Deflate/Zstandard decoders; QEMU performs no product I/O.

The handler now traverses extended entries and bounds reads at subcluster
boundaries. It validates conflicting allocation/zero bitmaps, absent host
allocations, reserved/misaligned offsets, allocated data bounds, L1 coverage,
header length and compression feature/field agreement. Existing 7-Zip cluster-size
acceptance and QCOW1's path are preserved: QEMU's 2-MiB implementation ceiling
does not introduce a new gate on otherwise supported images. Dirty/corrupt flags
remain readable without writes or replay.
Backing files, external data, encryption and unknown features remain unsupported;
this is not a complete transfer of the QEMU block driver or recovery support.

The existing baseline's `runtime_dependencies` selects the QEMU header for both
runtime overlays and fingerprints. Runtime dependency rules include its transitive
consumers and retain incremental objects. Its full license is installed on all
platforms. Fixtures with extended stored/Deflate/Zstandard clusters are generated
and independently checked/compared to raw source by QEMU 11.1.2.
Runtime validation on macOS: `archive_qcow` passed the disk-byte comparisons,
subcluster visibility and malformed-metadata cases; `archive_layer_chain` passed
all generated variants through QCOW → MBR → ext4, selected Unicode-file copying,
automatic opening, TAR parent transport, cancellation and source errors.
For the larger extended fixtures, fault injection targets the decoded partition
source so the QCOW compressed-input cache cannot avoid the injected read;
the original fixtures retain backing-source fault coverage. QEMU independently
confirmed the zero/unallocated subcluster result with stale host bytes present.
`archive_layer_lifetime`, `upstream_baseline`, `upstream_inventory` and
`runtime_cache` passed, including rebuilding a transitive runtime-header consumer.
Official codec/client targets rebuilt incrementally; baseline verification and
`git diff --check` passed. Linux/Windows, installed-package acceptance, full gate
and slow suites were not run for this platform-independent reader update.

### Dependent QCOW images — 2026-10-24

The original QCOW investigation included backing chains, external data, encrypted
images and internal snapshots. This step completes the raw/QCOW dependent-file
scenario; it does not establish completion of encryption or snapshot access.

Compared the complete `qcow2_read_extensions`, `qcow2_do_open`,
`qcow2_get_host_offset`, `qcow2_co_preadv_task` and
`qcow2_co_preadv_part` paths, external-data fixed-offset constraints, short
backing-file zero tails, and QCOW1's `qcow_open`/`qcow_co_preadv`, at the same
QEMU commit recorded above. `block/qcow.c` is added to the existing reference
baseline. Compared the original 7-Zip `VhdxHandler.cpp::OpenParent` and
`VhdHandler.cpp::Open2`, volume ownership, cycle/depth checks, and
`COpenCallbackImp::GetStream` at the pinned 7-Zip revision.

`src/dependencies/qemu/qcow2.c` adapts QEMU's read-side extension loop/dispatch as C, preserving
its MIT notice. The block graph is replaced by a bounded read callback and an
owned metadata result; it retains backing-format, external-data and crypto-header
records. Feature/bitmap mutation and crypto-context creation are not imported
into this parser. The handler uses the existing 7-Zip volume callback and
VHDX-style stream ownership/depth guard, resolves each child's relative name
against that child's directory, and retains parent streams for the lifetime of
the selected image. Callback path checks are not bypassed. Missing dependencies,
cyclic chains and incompatible backing formats fail explicitly; they do not
produce an apparently complete zero-filled image.

Unallocated regions inherit raw/QCOW parent bytes, short parents have zero tails,
and explicit zero regions override the parent. External data supports standard
and extended L2, including the copied-bit representation of host offset zero.
Host offsets must match guest offsets; compressed entries in external-data images
and invalid data-file-raw/backing combinations are rejected. Data bounds use the
external file size, while L1/L2 metadata bounds use the selected metadata stream.
The existing SDK handler remains the only format implementation and the engine
still performs all reads/decoding; there is no QEMU console subprocess.

Both runtime builders receive the C module through the existing dependency
manifest. Generated make/nmake rules use C compilation and the existing compiler
launcher/cache. A real C-to-C++ link regression also changes a shared header and
checks the resulting executable, covering incremental recompilation without
pinning generated makefile text. The QEMU reference manifest maps the new C source
to its original inventory unit.

QEMU-generated fixtures cover a Unicode-named QCOW → extended-L2 QCOW → shorter
raw chain, overrides/explicit zeros, external raw-compatible data and external
extended-L2 data. QEMU check/convert/compare provide the independent disk oracle.
`archive_qcow_references` verifies full disk bytes, unaligned/backward reads,
retained references, source immutability, missing dependencies, cycles, path
escapes, external-offset validation, and actual Extract success/output-error/cancel.
macOS passed this suite and the focused `archive_qcow`, `archive_layer_chain`,
`archive_layer_lifetime`, `upstream_baseline` and `runtime_cache` checks.
Linux passed `archive_qcow` and `archive_qcow_references` in
`20261024T070040Z-82183221`; Windows passed those suites plus
`upstream_baseline` and `runtime_cache` in `20261024T070136Z-ebbe6383`.
The reference-header regression exposed a Windows pipe deadlock in snapshot preparation:
the tar reader stopped before git's final archive padding while the producer
could still be blocked writing it. The preparer now drains the stream before
waiting for git. The baseline suite has a bounded CTest timeout; report checks
accept source CRLF/LF differences without normalizing the original hashes.
A dedicated producer-padding regression, using an actual archive plus padding
larger than a pipe buffer, also passed on macOS and Windows (the latter in
`20261024T071014Z-115c817f`). It fails with a bounded wait if archive draining
regresses, instead of leaving validation hanging indefinitely.

Both guests also contained an obsolete `sevenzip_qcow` plugin from removed
`src/lgpl3`/`qcowlib*` sources, confirmed by their Ninja logs and absent current
CMake target. These owned build artifacts were moved outside `Formats` to
`build/qcow-review/retired/`, preserving their bytes for diagnosis. The accepted
runs above load the current SDK implementation. Earlier Linux discovery timeouts
and the interrupted Windows archive-producer test are not acceptance results.
No compiler cache or incremental build tree was cleared. Full/slow suites and
installed-package acceptance were not run for this focused step.

### Legacy QCOW compressed clusters — 2026-10-24

Compared QEMU `block/qcow.c` at the pinned QEMU revision above:
`qcow_open`, `get_cluster_offset`, `qcow_co_preadv`, `decompress_cluster`,
`decompress_buffer`, and their compressed-writer caller. Adapted the descriptor
decoding from `decompress_cluster` into `src/dependencies/qemu/qcow.c`, with the narrow
declaration in `src/dependencies/qemu/qcow.h`; the C source and original MIT notice remain
intact. The baseline maps that original unit to its adapted owner and the runtime
overlay compiles it as C. QEMU block-graph I/O and zlib are omitted: native stream
I/O, cache and the existing official 7-Zip Deflate decoder perform those roles.
The adapter supplies cluster bits explicitly instead of a `BDRVQcowState`.

QCOW1 compressed descriptors use exact byte lengths and byte offsets. The SDK
handler now reads those ranges, validates their source bounds, and exposes the
decoded stream instead of marking all compressed QCOW1 images unsupported.
It requires a complete output cluster. Like QEMU's acceptance of zlib's
`Z_BUF_ERROR` with complete output, QCOW1 does not require a final Deflate marker
within the descriptor's byte range. QCOW2 retains its finished-stream check.
QCOW1 encryption remains separate missing work.

QEMU-generated stored/compressed QCOW1 fixtures were independently compared with
the raw source. The compressed producer is `qemu-io`: QEMU's QCOW1 compressed
writer rejects the final zero-byte request made by `qemu-img convert -c`, so the
fixture generator writes complete clusters to a fresh image instead. It does
not accept a failed conversion as an oracle. The reader suite covers complete
disk bytes, backward/sparse reads, invalid Deflate and truncated payloads; the
existing layer-chain suite consumes all three QCOW1 variants.

Current integration validation on macOS with the shared 7-Zip 26.05 baseline:
all six focused suites passed (`archive_qcow`, `archive_qcow_references`,
`archive_layer_chain`, `archive_layer_lifetime`, `upstream_baseline`,
`runtime_cache`). QEMU independently accepted the shortened-Deflate fixture;
our reader returned S_FALSE before the finish-mode fix and full matching bytes
after it. Five original QEMU file hashes verified. The legacy step has no new
Linux/Windows or installed-package acceptance; the guest evidence above predates
this step. Full and slow suites were not run.

Current scope decision — 2026-10-25: the user removed internal snapshot reading
and legacy AES-CBC while retaining extended L2, raw/QCOW backing chains, external
data and compressed QCOW1. Their crypto/snapshot adaptations, fixtures and tests
are removed; unchanged `HandlerCont.h` is again owned by the vendor dependency.
The QEMU baseline retains only the five original units used by this read scope,
with its MIT notice and source-to-owner mappings. Encryption, snapshot selection,
backing formats beyond raw/QCOW and dependency selection beyond the existing
volume callback remain unsupported. This is the selected product scope, not a
claim that QEMU cannot read those inputs or that the port has full QEMU parity.

### C++23 QEMU dependency facade — 2026-10-27

The QEMU baseline above retains its original commit and complete inventory
hashes. Its local integration now includes a named C++ module used by the native
QCOW handler. Compared the original compressed descriptor extraction, complete
cluster/subcluster classifiers and extension-reader dispatch with their C
adaptations, handler callers, stream helpers and Close/destructor paths.

The facade owns extension metadata, lends string views until reset/destruction,
and passes callback buffers as spans. It preserves the C parser's exact status
codes through `std::expected`. The stream callbacks already have nonthrowing
7-Zip seek/read contracts. A C ABI shim delegates to the existing static-inline
classifier so the exported module does not expose a translation-unit-local
function. The C bodies, allocation/zero classification and decoding loops remain
authoritative; their algorithms and omitted QEMU scope are unchanged.

The independent Make/NMake core build imports the module only in its QCOW
consumer. A small CMake dependency project builds the interface/archive using
the native compiler; generated Make prerequisites track the BMI and archive.
MSVC's cache launcher includes the imported IFC in its cache key. C dependencies
remain C, and native C++ standard-library hardening matches the module producer.
Runtime fingerprints include these build inputs without discarding incremental
objects or compiler caches. This does not turn the forked 7-Zip project into a
C++ module.

GCC 16 reports an array-bounds error after speculative devirtualization selects
`CLocalProgress::Release` for the separate `CHandlerImgProgress` COM implementation
in the existing adapted `HandlerCont.cpp`, unchanged by this iteration. The
same diagnostic selects `CFilterCoder::AddRef` for `CCopyCoder` in the existing
RAR handler. Disabling that optimization for these two units allows their
compilation; other optimizations and warnings remain enabled. This compatibility
setting is restricted to GCC 16.

Validation on macOS: the native Clang 23 consumer imports the module; the four
existing QCOW/reference/layer-chain/lifetime suites pass with the development
sanitizer environment. GCC 16 builds the independent console and format library,
and extracts all nine QCOW1/QCOW2 compression/extended-L2 fixtures with matching
source bytes and zero-filled sparse tails. Original QEMU reference hashes and
the baseline/runtime-cache/developer-workflow suites pass. A repeated Clang
runtime preparation reuses the verified artifact cache. Homebrew GCC's current
libraries require a newer macOS deployment version than the project's 14.4
floor; that floor is not accepted for this optional compiler. Windows and Linux
module builds remain unverified for this iteration.

## Scoped native read ownership — 2026-11-02

Reviewed the complete Qt input-layer/item-stream adapters, native generation
owner, stream-acquisition worker and their extraction/read-only consumers.
Compared the original serialized seek/read pattern in
7zDecode.cpp::CLockedSequentialInStreamMT::Read. Complete original StreamObjects,
LimitedStreams and 7zDecode inventories match the recorded baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; no external source is imported or
provenance advanced.

One scoped Qt context now retains the native reader generation, holds its stream
mutex and installs the operation's input callback. Destruction restores the
previous callback before releasing the lock and generation. Item stream factory,
Read/Seek, native extraction/Test and read-only enumeration use that same owner
instead of independently pairing a lock and callback guard. This also keeps a
reader alive while a factory transfers its other reference to the returned
stream. Initial opening still binds its request before generation publication;
metadata-only locks retain their existing scope.

The read-only view creates its local cancellation callback before entering this
context; that callback only references the view's cancellation flag and performs
no native access during construction. Cursor positions, read bounds, seek/read
algorithms, cancellation checks, generation replacement and direct-versus-spooled
input policy are unchanged. Existing public regressions exercise retained
descendants after parent refresh/window release, independent cursors, current
factory/read callbacks, cancellation/retry, native extraction/Test and read-only
views. No new test is added for private guard composition or class names, and no
pre-fix runtime failure is claimed.

Linux focused validation passed archive_layer_lifetime, archive_layer_chain,
archive_readonly_view, native_source_extract and native_archive_test: 149 Qt
checks without skips. This validates the common reader scope and checked direct
consumers on Linux; no new Windows/macOS, full/slow gate or appearance acceptance
is claimed. Selected scratch directories are empty; incremental builds/compiler
caches are retained.

## Retained native streams and generations

Selected source changes:
`a304ac2dce8b333973b034a883595879dcb938c2`,
`60ac293c1585ba12f2f2b546a559a7cec76065e8`,
`c11916e5fd515c1081a48f1b657296e2475a7e93`,
`33828e5a4d3849d59d2547c1cae919d80ad5e9e4`, native portions of
`5ce5784332c0c1567032a8856c46b8321581f895`, and final branch corrections.
Upstream reference: <https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`:

- `CPP/7zip/UI/Common/OpenArchive.cpp` complete `CArchiveLink::Open` stream descent,
  `CArc::OpenStreamOrFile` and reopen/volume callback dependencies;
- `CPP/7zip/UI/Agent/Agent.cpp/.h` stream-index/proxy mapping, update capability,
  reopen and folder ownership;
- `CPP/7zip/UI/FileManager/PanelItemOpen.cpp`, `FileFolderPluginOpen.cpp/.h`,
  `ExtractCallback.cpp/.h`: stream-first opening, CopyTo, virtual-file memory
  growth, spill, close, changed-file detection and parent-update prompt;
- `CPP/7zip/Common/StreamObjects.cpp`, `CPP/7zip/Common/LimitedStreams.cpp`,
  `CPP/7zip/Archive/7z/7zDecode.cpp::CLockedSequentialInStreamMT`: cursor/seek and
  serialized shared input patterns. The source branch originally cites
  `b88c6bc09339de32f7d18bbab900686b244f42ba`; these bodies were compared with
  the available 9128 baseline again.

The native handlers and their mappings are retained. Qt integration supplies
reader-generation leases, independent cursor positions and scoped operation
requests, with source seek/read control flow and bounds. Device generations
also receive independent authorized cursors; stream properties such as sector
size are forwarded when supplied. Native Extract/Open do not require a live
window backend. Remaining window/process service callbacks use guarded owners.
The native read-only boundary follows the input source, not total nesting depth.

The source branch's lazy factory advertises seek before resolving its native
stream, then falls back to replay. That factory was not copied. The original
`IInArchiveGetStream::GetStream` signature has no per-call progress argument,
so a narrow request-aware bridge preserves Agent's original index mapping and
calls the real native factory in the joined opening worker. Only an actual
native `IInStream` is exposed as seekable. No fallback decoder or synthetic
sequential-Extract seek implementation is introduced.

CopyTo retains its source memory allocation/spill logic. Its caller explicitly
requests the existing owned-file materialization when opening the extracted
memory result; arbitrary native streams remain direct. This preserves the
materialized child's update path and source modified-file prompt. The blanket
nested editing prohibition from `6c9ee3fc` is excluded.

Also excluded: replay input/cache/budget, replay seek probes and special open
results, replay-only memory/partial-result bridges and their tests, Alpine
tooling and the branch's historical migration log. TAR provides a real native
parent stream in the retained-chain regressions; ZIP remains an Extract-only
child in the lifetime case. Fixtures check byte results rather than source text.

Source comparison is complete for the selected stream, callback and caller
scope above. Native format mapping/decoder bodies were not independently
rewritten.

Runtime evidence, 2026-10-23: the selected 18 suites passed on macOS and Linux.
They cover stored/Deflate/Zstandard QCOW through MBR/ext to selected Unicode
file bytes, sparse/backward reads without full intermediate copies, current
request cancellation during native factory mapping and ancestor reads,
independent old/new-generation cursors, refresh after backend destruction,
retained extraction and final module release. Existing memory/spill, nested
edit/publication, external editing, native test/listing and device consumers
remain in the selected set. Linux receipt: `20261023T185403Z-5ca436fe`.

Windows passed 17 suites in that same desktop run. `codec_update` reached its
60-second limit there, then passed alone in 53.98 seconds, without source changes
(`20261023T190042Z-a3121d3a`). The earlier 15-suite run also passed that suite;
its new opening-options assertion needed native separator normalization, now
covered by the final run. This accepts the selected Windows scenarios while
retaining the timeout as a validation limitation. Incremental builds and compiler
caches were reused. The application target built on all three platforms; Linux
and Windows build receipts are `20261023T185917Z-15d6ed62` and
`20261023T190202Z-5f97b611`. Slow suites and a full gate were not run; no physical-device
or rendered-pixel parity is implied.

## Migration of remaining CLI operations

The migration starts at master `8a824d45ea46f1ef5ae212ee04c389e32d4317eb`.
This section extends the existing inventory; the selected native transfer above
does not establish completion of all archive operations.

Source inspection on 2026-10-23 identifies the remaining production dispatch:

| Operation | Current owner and remaining boundary |
| --- | --- |
| Catalog discovery | Native `LoadDll`/`LoadExternalModules`/`SetLibCodecs` supply providers and version metadata without executing the console. Deployment images retain provider lifetimes; optional console copying remains for unmigrated consumers. |
| Listing and testing | `ArchiveBackend::startNativeRead` uses official Open/Extract; its joined worker acquires native providers when no catalog was supplied. CLI dispatch and technical-listing parsers have been removed. |
| Extraction to a destination | `ArchiveBackend::extract` runs original `DecompressArchive` and `CArchiveExtractCallback` in the joined native worker. Original selection and overwrite replace the CLI passes; effective native selection is revalidated before writes. |
| Extraction of one source entry | The joined read worker acquires native providers as needed and extracts the selected native index into a private stream. CLI dispatch, text error decoding and console selection files have been removed. |
| Creation and public mutations | `archivebackend_creation.cpp` and `archivebackend_mutations.cpp` still dispatch creation stages, add, delete, rename and replace through the process transport. |
| Source updates | `archivebackend_sourceupdate.cpp` and `archivebackend_sourceplan.cpp` still dispatch file/add, mutation and staged plan operations. Native `UpdateArchive` and Agent update interfaces exist and need their callers connected. |

Direct consumers include file-only `QtArchiveFileResource` extraction/update,
source batch extraction/opening, standalone backend APIs and materialized nested
editing/publication. The retained native reader path must remain independent of
backend/window lifetime. Process completion owns continuations, temporary-file
cleanup and publication; migrating dispatch must migrate these responsibilities
together. Hashing and benchmark providers already use source/native workers.
The remaining write operations must migrate before
removing the shared CLI transport. Native discovery and retained runtime-image ownership
are now independent of console availability.

### Native extraction of one source entry

Upstream reference: <https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`:
`CPP/7zip/UI/Client7z/Client7z.cpp` (`CArchiveExtractCallback::GetStream`,
`PrepareOperation`, `SetOperationResult` and the `IInArchive::Extract` caller),
`CPP/7zip/UI/Common/ArchiveExtractCallback.cpp` (item/block diagnostics and
stream-output branch), `OpenArchive.cpp` (`CArc::GetItem_Path`),
`CPP/7zip/UI/Agent/Agent.cpp` (`CAgentFolder::Extract`) and
`CPP/7zip/Common/FileStreams.cpp/.h` (`COutFileStream`).

The existing read worker retains original Open/Extract and callback error
projection. The source file-stream/Extract/Close ownership is adapted to a
single private spool; the stream is returned only for the selected index in
extract mode, including when solid decoding requests skipped siblings. The
source caller retains final filesystem presentation and metadata handling.
The full Client7z filesystem extractor, console UI, directory construction,
attributes and overwrite policy are omitted because this API writes one
already selected private spool rather than a destination tree. Those features
remain in the original source extraction workflow.

The Qt boundary maps its exact name/ADS request to an actual native index;
the Console wildcard/censor interface is unsuitable for this literal selection
contract. Ambiguous duplicate names are rejected rather than concatenated;
index-based retained-reader extraction remains available for those entries.
Native `Create_NEW` protects an existing or raced output. Output created by the
operation is closed before completion and removed on cancellation or failure;
recovery-format item errors can retain unverified bytes with `S_FALSE` and the
decoder operation result or typed opening diagnostic. No text label is decoded to infer native CRC
or password failures, and partial output is never reported as complete success.

Source comparison covers the selected callback, stream, opener and caller scope;
format decoders and metadata/overwrite algorithms were not reimplemented.

Validation also exposed a retained-runtime dependency gap: after restoring the
pinned 26.04 header, Linux's older `DllExports2.o` still advertised 26.05 while
the console advertised 26.04. Its upstream make rule omits `C/7zVersion.h`.
The existing supplemental header-dependency generator now includes original
module-version consumers as well as adapted-header consumers. A regression
changes the upstream version baseline and executes the rebuilt consumer in the
same build tree. This repairs dependency tracking without clearing build outputs
or compiler caches, or forcing a cold build. The affected failed receipt is
`20261023T192213Z-50356b84`; its Windows selected set passed.

Runtime acceptance on 2026-10-23: macOS passed `native_source_extract`,
`native_listing`, `native_archive_test`, `source_extract_backend` and
`source_extract_contract` (4.78 s), plus `upstream_baseline` (7.30 s) and
`runtime_cache` (0.22 s). Linux and Windows passed the same seven-suite set in
`20261023T192554Z-cb0e1396`, with Windows running in its desktop session. The new
extraction suite passes 23 cases including initialization/cleanup, with no skips;
its backend has an unavailable CLI executable and a loaded official module.
Cases cover bytes for ZIP/7z/gzip/bzip2/xz, literal Unicode/space/wildcard/newline
names, duplicate rejection, solid siblings, decoder CRC and opening warnings,
passwords during both opening and decoding, cancellation/join, retry and
destruction while waiting for a password. Existing source consumers and native
listing/testing remain covered. Incremental build trees and compiler caches were
retained; disposable fixtures clean themselves up. No slow suite or full gate
was run. This accepts the selected native extraction path, not completion of
the remaining CLI operations listed above.

### Native catalog discovery

Starting at `cfb18b78`, discovery no longer launches `7zz i` or parses its banner
and library listing. Source reference: <https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`,
`CPP/7zip/UI/Common/LoadCodecs.cpp/.h`: complete `CCodecs::Load`, `LoadDll`,
`IsSupportedDll`, `LoadDllsFromFolder`, provider registration, `SetLibCodecs`,
`CloseLibs` and their version/property and registry dependencies were compared
with the existing port before the change and reviewed again afterward.
The existing mixed-revision baselines remain authoritative; this comparison
does not relabel the whole adapted module as a new upstream revision.

Original native module ABI/version queries, directory enumeration, provider
registration and diagnostics remain the functional owners. The original
`GetBaseFolderPrefixFromRegistry` body is exposed through a narrow overload so
the Qt-selected deployment can supply its starting directory. The zero-argument
source caller retains its original starting point. The Qt adapter resolves its
deployment paths and schedules loading; it does not replace the Windows
registry search or infer codecs from console text. Module-only explicit loading
retains its version-check contract. Deployment loading accepts usable external
formats when the main module is missing and preserves the nonfatal diagnostic.

Immutable module/plugin assets, original-path diagnostics, joined cancellation,
deadlines and retained generations remain in the existing owners. Native DLL
entrypoints cannot be safely interrupted; cancellation discards their result
after the joined load returns. An optional console copy is retained solely for
the remaining process consumers, without making native discovery depend on it.
Console information output and its text transport are omitted from discovery.

The affected native listing also now uses original `GetFormatNamePtr` like
Console `List.cpp`, preserving the parser (`#`) type when no format index exists.
Existing parser behavior coverage detects this missing projection. Opening tests
now verify extracted stream bytes after caller/manager release and retained
catalog extraction after failed replacement; obsolete spool-presence and
process-listing hooks no longer describe those native paths.

Focused acceptance covers discovery without a console (including a hanging or
wrong-version console), module/provider warnings and failed replacement,
plugin-only deployments, Unicode/space names, native list/test/source extraction,
deadline/cancel/retry/destruction, opening and retained-layer consumers.
Source comparison establishes the selected loader/adapter scope separately
from runtime checks; ordinary destination extraction, writes and no-catalog
fallbacks remain migration work.

Runtime acceptance on 2026-10-23 selected `codec_probe`, `codec_catalog`,
`external_codecs`, `native_listing`, `native_archive_test`,
`native_source_extract`, `agent_opening`, `native_folder_opening`,
`archive_layer_lifetime`, `associated_window` and `codec_capabilities`.
macOS passed the eleven-suite set (11.61 s), the expanded plugin-only external
codec cases (4.56 s), and the affected probe/opening rechecks after fixture
isolation. Initial Linux/Windows receipt `20261023T194744Z-58670aea` passed nine
suites and exposed two test fixtures accidentally discovering sibling build
plugins. The slow modules now live in private temporary deployments. Linux
passed both affected suites in `20261023T195103Z-e9baeeef` (9.65 s).
Windows passed both in desktop receipt `20261023T195213Z-954bf7bf`.
These runner checks cover DLL/so loading and the Windows registry boundary. Existing
incremental trees and compiler caches were retained. Private fixtures clean up
automatically; foreign runner artifacts were preserved. Host storage remained
healthy, with no VHDX growth. No slow suite, full gate or visual check was run.

### Native reads without a prepared catalog

Baseline `da0aae61`. Recompared original `LoadGlobalCodecs`/`CAgent::Open` and
`BindToRootFolder`, `ArchiveFolderOpen.cpp` factory/binding, `CCodecs::Load`,
and Console `Main.cpp` load/list/test/extract dispatch at upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`
(<https://github.com/ip7z/7zip.git>), with current catalog, read worker,
folder manager, file resource, reopen and source extraction consumers.
Original Open/Extract, property enumeration, proxy binding and codec loading
remain authoritative; no decoder, format parser or new detection algorithm
was introduced. Existing source module baselines and licenses are unchanged.

The shared native discovery body can now run inside the joined read worker.
Standalone listing, testing and single-entry extraction always use that worker,
including when no catalog was supplied. It checks pause/cancel around native
loading and retains the immutable image until loading and opening have joined.
Successful provider acquisition is published before result receivers can start
another operation; cancelled acquisition is discarded. An explicit failed
catalog is not silently replaced. Provider warnings and archive damage remain
separate diagnostics. Automatic source extraction classifies recovery using
the opened native format, preserving closed owned output and typed damage
results without requiring a prior listing or an explicit type string.

Agent opening requests discovery before selecting its direct stream or CopyTo
boundary, preserving native streams and reader generations for callers that did
not preload providers. The existing typed backend metadata bridge remains for
alternate service implementations; ArchiveBackend's own reads always load the
official native engine. Discovery is an overridable backend command like the
other asynchronous operation commands, allowing alternate metadata services to
own their provider availability without restoring console reads.

Removed read CLI argument construction, technical-listing/header parsers,
printed-error classification, redirected source output, exact-selection list
files and their completion/cleanup branches. The process transport remains for
destination extraction and writes. Its progress/error/password handling still
has direct write consumers and is not removed in this iteration.

Affected regressions use archives produced by official writers for literal
unsafe names, Unicode/space names, alternate streams, passwords (including an
empty defined password), lifecycle and cancellation. Process regressions target
the remaining write transport. Binding cancellation uses source item-progress
callbacks rather than a listing hook. Native rollback/retry and nested writer
failure/cancel use the existing delegating format fixture; property-fault
selection is extended to required opening properties. Reader-only fixtures
replace the obsolete assumption that TAR cannot update. The full interarchive
matrix is opt-in after its old process hooks produced a 120 s timeout; the
default native subset covers reopen failure, cancellation and failed restore.

Encrypted nested editing exposed a genuine native reopen adapter gap:
`COpenCallbackImp` intentionally omits `IProgress` from QueryInterface. The Qt
adapter's failed progress query also discarded its crypto service, producing
`E_NOINTERFACE` after a protected update despite a correctly retained password.
Compared complete original `CArchiveLink::ReOpen` and `COpenCallbackImp`
progress/crypto delegation and the File Manager update/reopen callers at the
revision above. `CNativeOpenProgress` now accepts the existing archive-open
callback directly and delegates its file/byte progress, password and fscrypt-key
requests through the original interfaces. The source QueryInterface contract
and original callback bodies remain unchanged; no new password store or prompt
policy is added. Callback lifetime is still scoped to the joined opening.

Final macOS acceptance on 2026-10-23: the application and selected targets built;
26 suites passed together (45.16 s): backend, mutation, opening, progress,
native source extraction/listing/testing, Agent opening/lifecycle, source extract
backend/contract, probe/catalog/external codecs/capabilities, native folder
opening, retained layers, open options, nested navigation/editing/update,
device folders, archive recovery, native parent publication, associated windows
and the focused interarchive recovery subset. The nested editor suite passes
15 cases, including separate protected inner/outer passwords and actual native
writer failure/cancel. Reference extraction uses UTF-8 include files for its
canonically unique fixture names so QProcess filesystem argument normalization
does not misread native NFC renames; production selection remains literal.

This iteration changes shared Qt scheduling and interface adaptation, without
changing DLL/so search/loading or OS-specific services, so validation is macOS
only under the user's runner rule. Linux/Windows were not rerun and are not
accepted for this revision by earlier receipts. The full interarchive diagnostic
hit its old 120 s cap before its obsolete hooks were replaced; only the native
subset is accepted. No opt-in exhaustive matrix, full gate or visual comparison
was run. Incremental builds and caches were preserved; obsolete task-owned
timeout fixtures and the temporary stack sample were removed after diagnosis.

### Selected-directory preparation — 2026-11-02

Reviewed the complete directory adapter and read-operation/backend consumers
with the original Common/Extract selection, callback initialization/close and
Agent CurrentPaths contracts. Source selection, decoding and callback algorithms
remain authoritative; no new source import or baseline advancement occurs.

Directory-specific option projection is now separate from the extraction call,
and its prepared options are immutable for that call. The bounded path policy
outlives those options and the native callbacks. Both callback implementations
use the existing typed COM owners without a separate raw/interface alias.
Allocation, reference acquisition and callback release order remain unchanged.
Literal selection, UTF-16 path parts, ADS copy/move policy, mtime fallback,
RejectExisting handling, late path validation and clean-success status publication
retain their previous behavior. Batch extraction keeps its distinct option rules.

Existing extraction-options behavior coverage is reused; no test of the new
private preparation function or pointer representation is added.

Linux extraction_options passed 23 checks without skips. Source inventory and
whitespace checks passed. Only shared preparation/ownership changed, without
native OS branches or ABI changes, so secondary platforms were not selected.
Selected scratch was empty before and after validation; configured four-job
incremental builds and compiler caches were retained. No full/slow or appearance
gate ran.

### Native extraction to a destination

Baseline `fd7d5229`; source review on 2026-10-24 against
<https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`:
`CPP/7zip/UI/Common/Extract.cpp/.h` (`Extract`, `DecompressArchive`),
`ArchiveExtractCallback.cpp/.h` (initialization, selection, paths, overwrite,
output/link metadata, item/block errors, memory requests and closing),
`IFileExtractCallback.h`, `Common/Wildcard.cpp/.h`,
`CPP/7zip/UI/Agent/Agent.cpp` (`CAgentFolder::Extract`) and their
GUI/ExtractGUI and FileManager callback consumers. Rechecked complete helper
and callback paths after integration. Existing source baselines, modules,
notices and decoder implementations are retained.

`DecompressArchive` is now callable for an already opened reader. Its native
selection, AUX/alternate-stream rules, callback initialization and close/result
sequence are preserved. A narrow optional extraction service sends its final
indices through ArchiveBackend's existing serialized native extraction boundary;
the source batch caller keeps its original direct invocation. Optional source
path parts provide Agent-style CurrentPaths/ADS-folder prefix removal. Original
outer-file mtime fallback is applied before extraction. No selection, overwrite,
rename, metadata or archive algorithm is independently implemented in Qt.

The destination adapter supplies typed progress, password, overwrite and
item/block/filesystem diagnostics in the existing joined worker. Overwrite
answers wait off the GUI thread and cancellation wakes that wait. Missing
decision consumers fail without replacing the existing output. The existing
validation body is shared with the worker and checks the actual selected native
indices, including descendants of a selected directory, before extraction writes.
The existing bounded-path policy remains attached to ArchiveExtractCallback;
the destination is checked again after an overwrite decision in case the GUI
wait allowed a link/path substitution. Backend source-snapshot requirements and
the source-workflow-only advanced options remain unchanged.

Removed the independent preliminary overwrite walk, its skip/overwrite/rename
CLI passes, extraction process continuations and extraction-specific password
state. All production archive reads/extraction now avoid console dispatch.
Creation, public mutations and staged source writes remain pending; this does
not complete the all-CLI migration goal.

Observable source alignment: CurrentPaths now actually removes the selected
archive-folder prefix (the prior CLI backend silently used full paths). Original
overwrite happens during decoding, so a ZIP data-password failure can leave an
unverified output after overwrite approval; no preliminary decode/replay pass
has been retained. Item/data/open errors remain failures, with their native
diagnostics. Tests cover failure separately from a successful overwrite retry.

Focused macOS validation: app and nine affected targets built; `extraction_options`,
`archive_backend`, `native_source_extract`, `native_archive_test`, `native_listing`,
`source_extract_backend`, `source_extract_contract`, `source_extract_advanced`
and `native_extract_callback` passed (11.91 s). After the final path-prefix
normalization adjustment, the app and affected targets rebuilt and
`extraction_options`/`archive_backend` passed again (42.51 s).
Reference-fixture commands used
the prepared official runtime on PATH. Coverage includes 7z/ZIP passwords,
overwrite choices/all/rename/no consumer, pause/cancel ownership, selected
Unicode/space names, destination substitution, unsafe descendants, CPIO dot paths,
source batch extraction, metadata and existing advanced-source options.
Obsolete fake-console fixtures and launch-count assertions were replaced or
removed while preserving output/error assertions. CPIO's old module-only row
incorrectly expected a GPL format provider inside the SDK module; it now uses
native deployment discovery with that provider.

This changes portable Qt/callback integration, not platform services or native
loading. Under the user's runner rule Linux/Windows were not rerun; native NTFS
security/ADS cases remain unverified for this iteration. No slow suite, full gate
or visual acceptance was run. Incremental build trees/caches were retained and
diagnostic logging/temporary fixtures removed.

### Return to standalone 7zG operations: source boundary review

On 2026-10-24 the requested direction changed from migrating every backend CLI
operation to restoring the upstream File Manager/7zG division. Completed native
reads and destination extraction remain the engine foundation. The new scope
is standalone test, extract, create/update, checksum and benchmark; live archive
folders, device inputs and parent-update chains retain their direct source path.
This is a requested boundary, not a claim that process isolation is implemented.

Reviewed against <https://github.com/ip7z/7zip.git>,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`: Common `CompressCall.cpp`
(`Call7zGui`, `CreateMap`, archive grouping/test/extract/create/hash/benchmark),
`CompressCall2.cpp` in-process counterparts, FileManager `CPanel::TestArchives`,
GUI `Main2` and exception/exit policy, `ExtractGUI`/`CThreadExtracting`,
`UpdateGUI`/`CThreadUpdating` and their callbacks/services. Original source module
boundaries and the existing baselines remain authoritative.

The current `main.cpp` selects the GUI entry by the `7zG` executable name;
`qtguimain.cpp` owns a separate QApplication workflow, native discovery and
QtShellArchiveServices without a manager window. It disables quit-on-last-modal
to preserve sequential source dialogs. CMake creates the GUI alias next to the
application; packaging creates Unix aliases and signs the macOS bundle. This
existing entry can be reused; a lighter executable is not a prerequisite.

For test, GUI `Main2` retains source enumeration, native types, callbacks,
ExtractGUI and exit-code handling. QtShellArchiveServices passes to original
ExtractGUI with a retained native opener. Its selected read path does not need
another engine migration. GUI update likewise reaches the existing native
`updateSourceArchive` overload and original UpdateArchive, rather than the
separate legacy staged CLI mutation API. These are distinct consumers; do not
route the GUI back through the legacy process API during extraction into 7zG.

The manager's filesystem Test still calls the typed in-process CompressCall2
workflow through `runArchiveBatchCommand`; its modal scope holds manager state.
The source archive-folder branch instead uses CopyTo/testMode over live indices
and must remain direct. The existing virtual context-menu Test operation is a
dispatch boundary, but the panel currently calls the global typed overload.
This is the first consumer seam to connect to the standalone launcher.

The upstream Windows sender owns a UTF-16 selection mapping/event and waits for
mapping acknowledgement or process exit. That proves source selection transfer
and startup ownership, not the requested portable versioned channel or a final
structured result. The current GUI dispatcher returns source process exit codes;
it has no manager-job ACK/result channel. Its public shell CLI and mapping parser
must remain compatible. Structured jobs should enter the same source dispatcher,
not recreate test/extract/update workflows or parse progress-dialog text.

Source-visible work before accepting the first integrated Test scenario:
versioned local job/ACK/result transport, native child launch/observation whose
owner does not terminate accepted work when the manager closes, separate launch,
cancel, warning/error and crash handling, and manager consumer/result binding.
Original Windows CProcess closes its handle without killing the child; the
portable adapter must preserve that lifetime while observing exit. A socket
disconnect alone cannot prove that a child crashed and must not cancel its work.
No corresponding portable process/result implementation exists in the reviewed
upstream modules; this is the narrow Qt/OS service adaptation to implement.

Baseline runtime evidence: the current standalone GUI entry builds and its
existing create/test/extract/excluded-format/broken-input scenarios pass on macOS.
The regression is extended to run actual 7zG tests against a module-only private
deployment and an explicitly unavailable console, for healthy and broken ZIP
inputs with Unicode, spaces and quotes. Existing tests used an available console
and could not detect a regression that wrapped test operations in 7zz again.
This establishes the reusable native entry, not manager IPC/lifetime acceptance
or installed/cross-platform packaging. No runtime/OS implementation changed in
this review. `unix_gui_entry_tests` and its app dependency built incrementally;
the expanded `unix_gui_entry` suite passed on macOS (5.61 s), with the prepared
official runtime on PATH for reference-fixture creation. Private deployments
cleaned up automatically and the task-owned registry is empty. Runners, slow
suites and a full gate were not run.

### Standalone filesystem Test

Source comparison on 2026-10-24 against the same retrievable repository/revision
above covers complete `Common/CompressCall.cpp` test/group launch and large-page
switch handling, `Windows/ProcessUtils.cpp/.h` process ownership, GUI `Main2`
parsing/enumeration/test/exit branches and `CPanel::TestArchives` with its live
CopyTo caller. Rechecked changed bodies and direct consumers after implementation.
Existing source baselines, notices and native decoder/callback modules remain
unchanged. No archive algorithm is newly implemented.

The filesystem panel dispatches through its existing services interface. The
File Manager supplies an independent Test launcher only when there is no
retained parent chain. The archive-folder CopyTo/testMode branch is unchanged;
other source consumers keep the in-process services default. After launch the
manager releases its synchronous command scope; an application-owned observer
tracks the child independently of the backend/window.

The narrow OS adapter reuses original Windows `CProcess::Create` and handle
ownership, with wait/exit observation. POSIX uses spawn/waitpid, a separate
session and closed inherited descriptors. Neither destructor kills accepted
work. Windows exception exit codes and POSIX signals are distinguished from
normal source results. A socket EOF alone means neither cancellation nor crash.
The new versioned, bounded, framed local protocol supplies readiness, one literal
job, acceptance and a typed result; its final code must agree with the native
process exit. Windows uses a current-user named pipe; POSIX uses a private short
directory under `/tmp` because a valid system TMPDIR can exceed sockaddr_un's
path limit. There are no persisted job files or selected filenames in argv.

The endpoint captures the complete selection before ACK and retains it after
disconnect. It initializes options through original Parse1/Parse2 defaults and
the source large-page setting, then supplies literal archive censor items to the
same GUI dispatcher. Original enumeration, codec discovery, ExtractGUI, password
and progress dialogs, callbacks, errors and exit policy remain authoritative.
The shell command-line parser remains available. Each child owns its runtime and
windows; this first scenario has ordinary read-only inputs and no parent-owned
materialized spool to transfer. Other commands, write conflicts and complete
three-platform installed-package acceptance remain subsequent parts of the goal.

Behavior regressions exercise real 7zG success/failure without an available
console, source password-dialog cancellation, kill/crash versus normal results,
another successful job after a crash, and native completion after an ACK-owning
parent exits. The lifetime fixture adds a test-only barrier around the production
endpoint and the same native dispatcher, with no switches in the installed
executable. Manager regressions verify repeated tests, recovery after a killed
child, exact selected paths, two-panel state and preservation of the live-folder
CopyTo route. Protocol checks cover fragmented/coalesced messages, large literal
Unicode/space/quote selections and malformed input. Forced-exit fixtures reclaim
only their inactive registered temporary images after identity revalidation.

The first focused manager run exposed the long-TMPDIR socket limit; the short
private socket directory fixes that actual integration failure. Existing toolbar
fixtures now use canonical temporary roots to compare filesystem identity without
depending on macOS `/var` versus `/private/var` spelling, and their remaining
filesystem Test expectations now observe the independent child rather than a
parent modal. No source-text assertions or implementation-presence tests were
added. Runtime validation is recorded separately below.

Focused validation on 2026-10-24: the app and selected targets built with existing
incremental outputs/caches. macOS passed eight suites (`operation_process`,
`operation_protocol`, `operation_launcher`, `toolbar_archive_workflow`,
`unix_gui_entry`, `shell_archive_services`, `source_extract_backend`, `language`;
11.47 s). The final fixture-only diagnostic change passed `operation_launcher`
again (3.99 s). Linux and interactive Windows passed the four operation/toolbar
suites through `verify.py --tests` (runner receipt
`20261023T220951Z-8ca13976`); source transfer and target builds preceded those runs.
Linux used offscreen widgets; Windows toolbar checks used its native desktop.
This is functional/source acceptance, not rendered Windows/macOS/Linux parity.

The first Windows lifetime run returned native failure code 2 after the parent
had exited; the child did survive and publish its result. The exact native cause
was not captured. Both isolated and original parallel retries passed, including
the final combined run. The fixture now retains public codec-discovery/progress
diagnostics in its failure report; no speculative engine change or enlarged
timeout was made. A future recurrence still needs that diagnostic evidence.

A private macOS runtime installation passed deployment and strict ad-hoc signature
verification. Its actual installed 7zG passed the two native healthy/damaged IPC
rows with the Cocoa platform (four QtTest checks, 1.08 s); Qt Network was deployed
by the existing Qt deployment script. No installed Linux/Windows payload, native
physical-input dialog comparison, slow matrix or full gate is claimed here.
Task-owned timeout fixtures and inactive crash deployment images were cleaned;
active/unrelated data and compiler/build caches were preserved.

### Standalone filesystem Extract: source comparison

Compared complete upstream/port `CPanel::ExtractArchives`, Common
`CompressCall.cpp`/`CompressCall2.cpp` extraction/group callers, GUI `Main2`
extraction branch and all `ExtractGUI.cpp` bodies against the revision above.
Reviewed `CExtractOptionsBase`, Parse2 output/overwrite/zone/NT defaults and
Common/Extract output-name substitution, with the existing callback/path/recovery
audit supplying the unchanged native decoder boundary. The panel owns selection,
default output folder, batch wildcard and context WriteZone; its retained-parent
branch remains OnCopy. The source GUI owns parameter edits, passwords, overwrite,
progress, callback errors and partial output. No source-visible decoder gap was
found for the selected ordinary-file scenario.

The remaining integration gaps are the filesystem service dispatch, structured
extraction parameters, child-owned GUI and reporting of the actual output root
after source dialog edits. `Main2` copies initial options into a local extraction
object; reporting the initial job destination would be incorrect. The existing
QtShell extraction service sees the final typed options and already owns a
dynamic output-root path policy. Use that boundary for completion metadata rather
than parsing dialog text or duplicating archive-name substitution. The manager
must refresh current affected filesystem panels after partial/error/cancel too,
without restoring old navigation. This section records source scope before
implementation; it is not runtime acceptance.

The same review includes the source Explorer ContextMenu extraction/test switch
and the File Manager's Qt shell-menu service owner. ContextMenu already supplies
literal filesystem paths, output folder, dialog choice, ElimDup and WriteZone.
Its ordinary filesystem Test/Extract/ExtractHere/ExtractTo consumers use the same
launcher; retained-parent/live-provider cases retain the direct service and
hash-file testing remains with the later checksum scenario.

Implementation preserves those source bodies and adds one optional notification
immediately before the source extraction worker starts, after successful parameter
confirmation and output normalization. The existing QtShell output policy supplies
its current root; archive-name substitution remains solely in Common/Extract.
The child reports that root over the local channel before writing and repeats it
in the final result. The observer drains queued data/EOF before publishing a native
exit or crash, retaining announced roots even without a final result. Source
callbacks and exit policy still decide success, errors and cancellation; a partial
file is never evidence of successful verification.

Structured Extract fields cover destination, dialog choice, ElimDup and WriteZone;
the source parser initializes all other defaults. Literal archive paths use the
same censor seam as Test. Manager launch/result glue is separated from archive
commands; both toolbar and source context verbs use it. Deferred refresh checks
the current filesystem provider and path, including ancestor directories, rather
than restoring a captured panel binding. It also works with auto-refresh disabled
and waits behind another active source call/scan. Retained archive/device/nested
flows remain direct; no temp input is newly borrowed from the parent.

Behavior checks exercise actual native-only 7zG extraction with an unavailable
console, edited/cancelled parameter dialogs, overwrite choices, correct/incorrect/
cancelled password prompts, completed entries retained after cancellation, CRC
failure with readable and unverified output, extraction after parent exit, unsafe
member bounds, and crash metadata while parent event delivery is deliberately
withheld. Manager checks cover both panes, single/batch defaults, all filesystem
context extraction/test verbs, navigation during work, and deferred refresh behind
the source Link modal. Existing title/geometry evidence now comes from that same
dialog in the child fixture; source-service and shell CLI regressions remain.

Validation found two fixture defects rather than weakening production behavior:
context settings were saved before the source Load initialized WriteZone, and
UTF-8 bytes passed through the Windows environment code page corrupted a child
dialog's destination. The tests now use source initialization and a Unicode OS
environment setter on Windows. Reported actual output roots established the
encoding error. The Windows toolbar plugin override also preserves the existing
per-suite registry/startup environment, so forced-exit cleanup retains ownership
proof instead of falling back to a shared registry.

On 2026-10-24 macOS built the app and selected targets; eight focused suites passed
together (37.41 s): operation extraction/launcher/protocol, toolbar archive workflow,
shell menu window, shell archive services, source extract advanced and Unix GUI
entry. The final Windows focused verify passed four suites after target builds
(`20261023T225045Z-f3765084`): operation extraction/launcher/protocol and toolbar
archive workflow, on the interactive desktop for native toolbar/child dialogs.
The Windows checks are required by path/WriteZone adaptation and the native test
environment; Linux-specific process/loading code was not changed and its runners
were not repeated. Installed three-platform acceptance remains pending for the
whole goal, as do Create/Update and checksum/benchmark migration. No full or slow
gate, rendered parity or physical-input acceptance is claimed by these checks.
After the final fixture environment adjustment, its two direct macOS suites
passed again (18.73 s). Disposable timeout/diagnostic fixtures and inactive
registered crash images were removed; no incremental trees or compiler caches
were reset. The Windows host storage check showed no VHDX growth. Older guest
temporary directories without proven task ownership were preserved.

### Standalone Create/Update: source comparison

Output refresh review (2026-10-30): compared pinned `Panel.cpp` AddToArchive,
GetFilePaths and ExtractArchives, `PanelItems.cpp` reload/selection workflow and
their current source/service callers. Child completion metadata, current-provider
matching, ordinary archive reopening, deleted-input fallback and stream/device
exclusions remain local integration around those retained bodies. A window-owned
queue now combines overlapping output paths, waits behind source calls/scans and
discards pending refreshes at shutdown. Each panel consumes its own batch before
calling source/UI code, retaining results delivered by nested events. No upstream
code is imported or decoding/update algorithm changed by this queue refactor.

The next boundary review compares `CompressCall.cpp`/`CompressCall2.cpp` creation
arguments, `CPanel::AddToArchive`, File Manager create/shell/drop consumers, GUI
`Main2` update dispatch, `UpdateGUI`/ShowDialog option transfer, native catalog
`UpdateArchive`, Common/Update and its move/deletion callbacks against the same
retrievable upstream revision above. The existing GUI path already invokes the
native official updater; it must not be routed through the separate staged CLI
mutation API. Child dialogs can retain original action sets, properties, volumes,
SFX and delete-input choices without serializing passwords into a job.

Source-visible publication gaps must be resolved before accepting this scenario.
Common/Update deletes the old archive before MoveArc_Start and before moving its
completed temporary file. A cancellation there or a failed cross-volume copy can
leave the original path missing. The current native catalog calls that body
directly; the stronger staged CLI publication owner does not surround it.
Also, Unix SFX executable marking currently happens in ArchiveBackend after
UpdateArchive has already performed optional source deletion. The new process
must preserve publication/deletion ordering and reject competing writers; simply
adding a Compress job to the launcher would not complete the requested scenario.
MultiOutStream's existing temporary-volume/final-name behavior and native SFX
creation are part of the selected component, not independent replacement
algorithms. This is source evidence, not acceptance of standalone Create/Update.

The in-progress publication adaptation keeps Common/Update's compression,
enumeration, callbacks, close, error projection and input-deletion phases.
It removes the pre-publication deletion and calls a narrow FileDir replacement
adapter for an existing archive. Upstream FileDir has no atomic replacement
with cancellable cross-filesystem staging: its Windows move does not replace an
existing destination, and its POSIX EXDEV copy creates the final destination
directly. The adapter uses native rename/MoveFileEx publication on one filesystem
and a registered QTemporaryFile beside the destination across filesystems, followed
by the same native publication. Replacement is explicit; new outputs use atomic
no-replace publication, including final numbered volumes. Copy cancellation leaves
the previous archive intact; completed source temporaries remain available under
the original updater's failure policy. Cross-filesystem copying preserves file
permissions and Qt's modification-time precision. This boundary adapts platform
file publication, not archive encoding or the original volume algorithm.

SFX executable preparation now belongs immediately after the source Compress
output closes, before temporary-file publication and optional input deletion.
The previous post-return ArchiveBackend step is removed, including its duplicate
ownership. This also applies the requirement to both native catalog entry points.
The changed publication body and original Windows/POSIX move bodies were compared
again against the upstream revision above. Fresh single-file archives now use the
existing source temporary writer as well. MultiOutStream retains its original
volume sizing, restriction, stream-closing and rollback algorithm; only its final
move uses the no-replace publisher. A volume SFX header uses source CreateTempFile2
and CTempFiles ownership, is made executable and published after completed volumes,
and remains covered by source failure cleanup. Archive encoding is unchanged.

The manager's filesystem Add and nonblocking shell Compress consumers now pass a
typed Compress request through the existing launcher. Source filename/format
selection and default switches remain derived from CompressCall/Panel. The child
owns UpdateGUI dialogs and native update callbacks. Passwords stay in child UI;
email platform support remains the original boundary. Synchronous drag/drop and
open/nested providers retain the direct source path. The source UpdateGUI boundary
reports chosen archive/volume outputs and input-deletion directories before the
worker starts; the launcher preserves those paths through crash results. The
manager refreshes current filesystem panels and reopens matching ordinary archive
panels without restoring an obsolete navigation location.

A narrow per-user QLockFile adapter serializes cooperating writers across native
Common/Update and retained Agent CommonUpdateOperation. It canonicalizes output
aliases and volume namespaces, locks multiple destinations in sorted order and
does not expire a live owner based on time. Agent checks retained file identity,
size and source modification time before editing under that lease; an obsolete
generation remains readable but cannot overwrite a newer publication. This is
cooperative writer exclusion, not protection against unrelated writers that ignore
the lease. Windows CInFile::OpenShared adds FILE_SHARE_DELETE for ordinary files
so old read generations do not prevent replacement; device sharing is unchanged.
On Windows an open destination also needs extended rename semantics: the narrow
publisher retries an Access denied replacement with FileRenameInfoEx and
FILE_RENAME_FLAG_POSIX_SEMANTICS, reusing upstream CInFile's rename handle owner.
Ordinary MoveFileEx and upstream FileDir have no equivalent open-generation
replacement. The adapter follows Microsoft's
[extended rename contract](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/4217551b-d2c0-42cb-9dc1-69a716cf6d0c)
and [handle API](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle).
Unsupported filesystems still fail without removing the original; no direct-write
fallback is introduced.
Source bodies, callers and dependencies for Update, MultiOutStream, WorkDir,
FileDir, FileIO and ArchiveFolderOut were compared before these adaptations.

The new native-publication regression creates its fixture through the official
native updater without a console executable, then checks successful replacement,
Cancel/error before publication, preservation of inputs requested for deletion,
and executable SFX readiness before input deletion. The pre-fix macOS run lost
the original archive path in both Cancel/error rows. Linux additionally exercises
real `/dev/shm` to disk publication and cancellation during the destination-side
copy. Missing a separate `/dev/shm` mount explicitly skips those rows elsewhere.
macOS passed the focused suite (3.61 s; three cross-filesystem rows skipped).
Linux passed all rows after preparing its official SFX runtime
(`20261023T232250Z-97b8e5a7`, 0.16 s). No cache or build tree was reset.
Windows passed after its SFX runtime preparation
(`20261023T232507Z-4b66dc8d`, 1.27 s; three cross-filesystem rows skipped).
One Windows catalog-ready observation took seven seconds; only the fixture wait
was adjusted to ten seconds, without changing engine deadlines.

A broader local diagnostic of `source_update_backend` and `codec_commands_ui`
did not pass: one panel update and seven successful Copy rows still require the
CLI-specific engineStarted signal, while two unavailable-catalog rows disagree
with current Add availability. The obsolete CLI-event requirements were removed
from those native-operation tests, retaining payload, untouched-member, source
preservation and cancellation checks. The repeat passed `source_update_backend`
and the new publication suite; `codec_commands_ui` reached 22 passing cases and
only the two Add-availability disagreements. That capability behavior still
requires review before complete Create/Update acceptance. The whole diagnostic
set is not counted as passing acceptance. Initial runner
attempts also exposed a Windows pointer/string conversion build error (fixed)
and absent SFX runtime prerequisites (prepared through the existing wrappers).

The complete creation candidate adds real child-process ZIP/7z create/update,
parameter edits and Cancel, UI-only passwords, volumes, executable SFX and volume
SFX extraction, input deletion, parent exit, competing-writer refusal/retry and
forced-child-exit/retry checks. The manager consumer tests exercise toolbar and
shell entry points, both panels, refreshed input/output directories, reopened
archive peers and deleted opened inputs. Retained native update tests protect the
newer published archive from stale writer generations. Existing CLI-event-only
assertions were removed while preserving payload and cancellation checks. The
unavailable-catalog scenario now verifies the actual failed opening and retained
filesystem navigation rather than assuming an archive provider was entered.

Focused consumer validation found and fixed a source Cancel result overwritten by
the new Compress wrapper, and a peer ReOpen guard left active after projection.
The latter regression checks that panels become usable and remember source focus
after reload. The source update suite completed 76 cases in 35.16 s locally; its
CTest limit is now 90 s instead of 30 s, with no added matrix or engine timeout.
These checks cover behavior, not private implementation presence. Three-platform
acceptance of this complete candidate is recorded separately after the runner
results; the earlier publication-only receipts above do not establish it.

Create/Update focused acceptance, 2026-10-24: macOS passed the final nine shared
consumer suites (45.38 s), manager creation workflow (10.55 s), source update and
shell GUI consumers (76 and 11 cases), then the changed publication fixtures
(three suites, 42.00 s) and final creation fixture (19.39 s). Linux passed all
11 selected suites in `20261024T004715Z-047ed054` (68.41 s), including actual
cross-filesystem publication. Windows passed eight of those suites initially;
its three failures identified open-destination rename semantics and a fixture
working-directory setting written to the wrong parent-side environment path.
After the native rename adaptation, manager creation, native publication and
retained codec update passed in `20261024T010046Z-199442f0`; final independent
creation passed in `20261024T010308Z-cb6e0862`. Windows UI consumers used the
interactive desktop. The stale-view fixture now publishes with the production
adapter rather than Qt's separate QSaveFile replacement contract.

These receipts establish the selected filesystem creation/update scenario and
its retained direct update consumers. No installed-package, visual parity, slow
matrix or full-gate acceptance is claimed. Checksum/benchmark migration and final
installed three-platform checks remain part of the active goal. All builds reused
the existing incremental trees and compiler cache; sibling upstream was read-only.

### Standalone checksum: source comparison

The next scenario compares upstream 9128 `CompressCall.cpp` checksum and hash-file
verification entries, GUI.cpp hash dispatch, complete PanelCrc.cpp enumerator,
worker and CApp caller, HashGUI.cpp callbacks/results, HashCalc.cpp scan/read/error
projection and the existing Qt callers. Their mixed 0766 adaptation baseline and
notices remain unchanged. The two source workflows are deliberately distinct:
File Manager PanelCrc hashes exact operated paths with EnterToDirs controlled by
flat mode; shell `h` uses HashCalcGUI and the original censor enumeration. Archive
panel hashes retain original stream-mode CopyTo, including passwords and CRC
failure handling. No container materialization is introduced.

The filesystem block of CalculateCrc2 is made callable by the process owner through
a narrow entry adapter in the same module. Its enumerator, prefix/ADS handling,
scan and read loops, link boundary and results remain the one existing source
implementation. The structured request retains the literal relative paths and
base prefix, with traversal/absolute-path validation before acceptance. Shell
requests keep literal absolute selections; checksum-file verification uses source
`t -thash`. The original command-line parser still handles shell argv.

PanelCrc and HashGUI originally returned S_OK after creating/joining the progress
window regardless of worker outcome. The process boundary now projects the typed
worker result and existing hash error count, preserving E_ABORT and reporting
partial input as warning. Hash digests stay in the child-owned source results
dialog; the manager receives an outcome, not parsed UI text. Acceptance and
remaining consumer coverage are recorded after focused validation.

Hash-only discovery also preserves registered native hashers when an archive
runtime is absent. The source GUI dispatcher permits hashing without archive
formats; its factory and hash-provider load now retain that distinction instead
of making every command depend on an external archive DLL. This does not introduce
a console fallback or relax archive-command catalog requirements. Shell command
invocation keeps source KillSelection after acceptance; each subsequent shell
operation uses the new explicit selection.

Eligibility now has one manager owner shared by toolbar and shell consumers.
Source Is_IO_FS_Folder includes the raw-device namespace; the process boundary
explicitly excludes that prefix and nested/open-container providers. Native device
hashing keeps CalculateCrc2 and its existing direct reader. File-backed ADS have
no borrowed stream handle and remain eligible; their source colon-prefix handling,
digests, source preservation and EN/RU normal/compact results geometry moved from
the old manager-modal namespace test to the independent consumer test.

Checksum acceptance, 2026-10-24: the final macOS eight-suite set passed (45.53 s):
operation hash/protocol, manager hash workflow, retained checksum and shell archive
services, namespace hash, and toolbar archive/creation workflows. The final manager
crash/retry fixture passed again (7.40 s) with an accepted-job barrier. Earlier shared
GUI-factory consumers also passed operation creation/extraction/launcher. The
obsolete filesystem-modal digest assertion moved to actual child results; opened
archive digest/password/CRC regressions remain. Opening/stream-transport signal
counts were removed from that UI scenario while retaining actual digest checks.

Windows passed nine direct consumers in `20261024T012809Z-1c527ffd`. After centralizing
the device boundary and moving ADS coverage, all affected targets, including the
retained namespace suite, built in `20261024T013456Z-39b9f2ab`; the final manager
hash and toolbar archive/creation suites passed on the interactive desktop in
`20261024T013825Z-2c1ff7ea`. Hash regressions cover registered hashers without a DLL
or console, literal Unicode/space selections (quotes/wildcards where valid),
recursion/flat mode, partial input, invalid methods, Cancel, manifests, parent exit,
manager close/navigation and crash/retry. Windows paths and ADS justify its runner
checks; portable Linux behavior was not rerun. No disks were created/attached;
unchanged private-device VHD fixtures were not executed. No full/slow, installed
package or rendered pixel-parity acceptance is claimed. Benchmark and installed
three-platform acceptance remain in the active goal.

### Independent benchmark process — 2026-10-24

Source baseline remains `9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>; existing older-module provenance is retained.
Compared `UI/Common/CompressCall.cpp::Benchmark`, the GUI dispatcher and
`UI/GUI/BenchmarkDialog.{cpp,h,rc}` with the adapted dialog, controls, callers,
properties, native services and complete worker/start/stop/restart/finish paths.
The source manager launches `7zG b`, adds `-mm=*` for total mode and the configured
large-page switch, and does not wait. The manager now uses the common app-owned
operation launcher for that same process boundary; the framed job carries the
mode without file paths. The child retains source Parse1/Parse2, codec/provider
loading, properties, benchmark algorithms, callbacks, dialogs and thread join.

The source dialog wrapper returns S_OK after closing even after an engine error.
That legacy caller contract remains; an optional completion result exposes the
existing task/thread HRESULTs, memory refusal and thread-create/wait failures to
the GUI dispatcher. A normally completed pass is success; closing before completion
is cancellation. Total mode uses its existing finished-pass count because the
source exits its total worker before setting the normal-mode finish flag. No
benchmark algorithm or callback was replaced, and no source branch/dependency was
omitted in this scope. Existing Qt presentation, native services and the previously
audited starting-thread validation remain. The obsolete manager-only benchmark
service factory was removed; native service injection in the dialog is retained.

Regression fixtures bound real Bench work with ordinary properties, without adding
automation switches to production. They exercise LZMA without a console executable,
invalid methods, normal/total cancellation, representative CRC32 output in the
total-mode dialog, accepted work after parent exit, manager close, and crash/retry.
The CRC32 case selects one native method after selecting the total presentation;
it does not claim acceptance of the full all-codec performance matrix. Existing
dialog lifecycle checks retain Stop/Restart/join and geometry coverage and now
also check the completion result for cancellation, memory refusal and engine errors.

macOS validation: affected application/test targets built incrementally. Ten of
eleven focused suites passed in the shared 52.49 s run: operation protocol,
benchmark lifecycle/command, operation launcher/extraction/creation/hash, manager
hash and both toolbar workflows. The remaining operation-benchmark suite passed
separately (23.94 s) after fixing its fixture to recognize the translated final
report heading. Native benchmark also passed (19.77 s) in the initial focused run.
The crash fixture now dismisses and checks the manager's real error dialog; its
previous unattended modal caused the initial test timeout. Production behavior was
not relaxed to satisfy either fixture. The changes are platform-independent, so
Linux/Windows runners were not used in this iteration. Installed three-platform
acceptance remains pending for the goal; no full/slow or pixel-parity acceptance
is claimed here.

### Installed operation acceptance and scope audit — 2026-10-24

The installed-runtime checks reuse the existing behavior suites instead of adding
source-text, file-presence or configuration-value assertions. Their native GUI
cases can select an installed executable with `SEVENZIP_OPERATION_GUI` and its
native QPA with `SEVENZIP_OPERATION_QPA`. Ordinary dev runs retain their previous
targets/platform. The first installed macOS run exposed the harness forcing
offscreen, a test-only plugin deliberately pruned from runtime packages. The
fixture now selects the actual native platform for installed children; production
startup and package pruning are unchanged.

`scripts/check-installed-operations.sh` builds the affected targets, installs with
the existing atomic private-runtime deployment, then runs focused consumers. The
actual installed executable handles Test, crash/retry, literal extraction, damaged
output, traversal rejection and ZIP/7z creation/update/verification. Parameter,
password, overwrite, cancellation, volumes/SFX, publication/conflicts, parent-exit,
manager-hash and benchmark cases continue to use their explicit native GUI test
host, which drives the same source dispatcher and public dialogs. Passing these
suites does not mean every test row executes the installed binary.

Installed/runtime receipt: macOS six suites passed (33.26 s), after the initial
installed three-suite set passed (16.67 s). Linux and Windows six-suite sets passed
in `20261024T020940Z-f8a96489` (24.68 s and 50.66 s respectively). The sets are
operation launcher/extraction/creation/benchmark, benchmark command and manager
hash workflow. macOS uses Cocoa and a deployed, strictly verified ad-hoc signed
bundle; Linux uses the deployed X11 plugin on Xvfb; Windows runs in the logged-on
interactive desktop. Existing compiler caches and persistent build trees were
retained. No new disks were created or attached.

The additional entry-only check launches installed 7zFM with its smoke entry.
Unix CLI create/test/extract, excluded handlers, broken inputs and module-only
dispatch also select installed 7zG. macOS passed this suite (5.87 s), Linux passed
it (0.66 s, `20261024T021906Z-9459fed2`). The corresponding Windows check uses the
existing real shell-DLL launch and File Manager startup suites, scoped temporary
registration and package verification.

Windows entry checks require complementary tokens. The first Limited run passed
shell/package but failed the two startup symlink fixtures; the first Highest run
passed startup/package but failed registered shell activation (host exit 6, whose
individual COM/data-object failure was not further diagnosed). The wrapper now
selects registered shell checks under Limited and startup/symlink checks under
Highest instead of treating either token as universal acceptance. Final Limited
shell/package passed (3.19 s, `20261024T022337Z-7c4f272d`); final Highest startup
passed (14.84 s, `20261024T022355Z-406a922c`). No symlink, COM or security policy was
changed and no assertion was removed.

Requirement audit against the requested plan:

| Requirement | Current source and behavior evidence |
| --- | --- |
| Retain native work and original module boundaries | The preceding Test, Extract, Create/Update, checksum and benchmark sections record complete source comparisons, source revisions, dependencies and necessary Qt/OS adaptations. Selected native workers remain authoritative; this iteration changes only validation tooling. |
| Independent process owns engine, UI and resources | `qtguimain` owns catalog/services; original GUI workers own callbacks, inputs, staged writes and dialogs. Actual module-only installed operations prove no console dependency in their selected paths. Parent-exit cases cover each operation family. |
| One launcher, ACK and typed completion | Backend GUI entry methods share the launcher/endpoint/protocol. The request is captured before ACK; process observation distinguishes result/Cancel/warning from kill/crash and protocol failure. Version, UTF-8, exact selections and malformed frames have behavior coverage. Only the channel address enters the manager-created argv; no password/job file is produced. |
| Complete command scenarios | Manager toolbar/shell and child consumers cover Test, Extract, Create/Update, hash and benchmark through the standalone route. The follow-up below also connects drag-created archives. Success, errors, password/overwrite/Cancel, partial/corrupt extraction and malicious paths are covered by the focused operation suites. |
| Lifetime, refresh and concurrent writes | App-owned observers and native handle release do not terminate accepted work. Crash/retry, close/navigation during work, output announcement, stale-generation rejection, competing writers, atomic/cross-filesystem publication, volumes/SFX and input deletion have the recorded behavior checks. |
| Explicit stream/device boundary | Current manager eligibility excludes raw-device prefixes and parent/open-container chains; live archive operations retain their source owner. No blanket nested read-only rule, whole-container materialization, decoding replacement or source repair/replay was introduced. |
| Build, distribution and acceptance limits | Incremental focused checks and private installed runtimes pass on all three platforms as described above. Full/slow matrices, release distribution and pixel parity are not claimed. Computer Use returned “Computer Use was not approved to use Sevenzip” on macOS; the user explicitly chose to leave manual installed UI inspection unverified. That exception does not imply visual acceptance. Source/native dialog regressions remain part of the recorded checks. |

With that explicit manual-UI exception, the selected standalone-operation plan
has no remaining implementation or required runtime/package acceptance item.
Live streams/devices and parent-update chains retain the intentional direct
boundary; source transfer completeness and the focused runtime scope are recorded
separately above. This completion does not extend to full engine isolation,
the deferred lightweight executable, a full/slow gate or release distribution.

### Follow-up source comparison of restored launch routes — 2026-10-24

Reviewed HEAD `30b05340c989f14c1080c7e05074ef80ccc828c5` against the
pre-native-migration parent `ff7c8bbdb96162c627b6d0544c631a66d8255444`,
the standalone acceptance commit
`5c258e52d8beeddfdc4a3e2f0486a2863effa84e`, and upstream
<https://github.com/ip7z/7zip.git> at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Compared Common/CompressCall launch/options and its FileManager Panel, PanelDrag,
MyLoadMenu and Explorer/ContextMenu consumers with the corresponding port services,
GUI dispatcher and operation endpoint. This is a source review; no new runtime
acceptance is claimed.

The restored implementation is not a source rollback. The existing GUI dispatcher
and native workers are reused through a new portable job/ACK/result channel.
Toolbar Add/Test/Extract, shell extract variants, compression variants,
checksum generation/verification/calculation and both benchmark modes remain
connected. The selected launcher, endpoint, dispatcher and drag presentation files
have no intervening changes between standalone acceptance and this HEAD.
Direct Test/Extract within an open archive is also present in upstream; that
boundary alone is not a missing external launch.

The review found one unported upstream launch route: PanelDrag::CompressDropFiles creates
a new archive through Common/CompressCall in a separate 7zG, with either
waitFinish value. At the reviewed HEAD, QtFileManagerDrag::CompressFiles calls the in-process
CompressCall2 service for both values. In particular, an ordinary-file drop that
does not require waiting still keeps the dialog and worker in the manager.
The drag presentation and PanelDrag files are unchanged from the pre-migration
parent, so this is a retained port gap, not code lost in the latest restoration.
The earlier selected-plan completion does not establish that every upstream 7zG
call site has been restored.

The follow-up fix connects both branches to ArchiveBackend's existing standalone
Compress operation. The original PanelDrag choice of destination, archive name,
source selection and wait flags is unchanged. For waitFinish, a local Qt event
loop observes completion while the existing source-call/close guards retain the
drag source and exclude further user input. The child owns dialogs and native
workers. Cancellation returns E_ABORT; failure returns E_FAIL. Nonwaiting drops
reuse the application-owned observer and can finish after the manager closes.
No decoding, update algorithm, process implementation or protocol was added.

The existing drag_drop_add_ui regressions now drive the real child GUI dispatcher
instead of a parent Create dialog. They retain the external-archive-open and
native filename/content checks, and additionally cover waiting/nonwaiting
success, cancellation and unsupported-format failure. A child barrier verifies
that the temporary-source call cannot return or close the manager before child
completion; output bytes are extracted after the source file is released.

Validation of this fix on macOS: application and selected test targets built;
drag_drop_add_ui, drag_drop_ui, drag_protocol and operation_creation passed
(four suites, 6.60 s). The initial new output-verification rows lacked the native
reader's list/open preparation; adding that existing public workflow fixed the
fixture. Final diff whitespace check passed. No Windows/Linux rerun, full/slow
gate or rendered UI comparison was performed; platform-specific process code and
dialog layout are unchanged.

The C++23 validation on 2026-10-27 found that `source_drop_add` still expected
the obsolete parent-process Create dialog. It now shares the existing child
launcher fixture and checks 7z/ZIP output bytes, cancellation without output,
and failure without modifying a damaged destination. Those four cases and the
three existing drag/drop suites pass on macOS with the development sanitizers.
No production source changed; Linux/Windows checks await runner access.

## 2026-10-24 — retire console transport, retain official `7z` sidecar

This iteration removes the old `ArchiveBackend` console creation/mutation,
source-plan reconstruction, process parsing/pause and temporary-volume transport.
The authoritative operation implementations remain the original GUI/Common and
FileManager/Agent modules documented above. Their source baseline remains
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip>, with the existing per-module mixed-revision records;
no upstream algorithms or archive handlers are replaced in this cleanup.

Source comparison covered `CPP/7zip/UI/Agent/AgentOut.cpp`,
`CPP/7zip/UI/FileManager/PanelCopy.cpp`, `PanelOperations.cpp` and
`CPP/7zip/UI/GUI/UpdateGUI.cpp`, their callers, callback types and native resource
owners. Archive-to-filesystem Move now reaches the ported `CPanel::DeleteItems`
after successful native extraction. The narrow Qt adaptation skips its second
confirmation after the user has already confirmed Move and retains the captured
native indices. `CAgent::CopyTo(moveMode)` itself remains unsupported upstream;
this adapter does not claim otherwise.

The official Console build (`CPP/7zip/UI/Console`) produces dynamic `7z` beside
its format module. It is installed as an independent user-facing utility, as in
the official distribution. Neither manager nor operation GUI invokes it. The
Alone2/`7zz` target is no longer built or published. Runtime manifest retirement
removes a previously owned, hash-matching published artifact; private compiler
outputs and caches are not reset. Module discovery uses a module path, including
`SEVENZIP_CODEC_MODULE_PATH`, without searching PATH for a console executable.

Tests that seeded archives through removed console APIs now use the official
`IOutArchive` interface or the existing GUI update worker. Compression properties,
volumes, SFX, encryption, cancellation and publication remain observable checks.
Obsolete fake-process-only suites and event assertions are retired; native Agent
transaction, renderer, external-edit and operation-process regressions own those
behaviors. Frontend selection tests capture the public `7zG` request and leave
actual child/native execution to the operation suites. This source review is
separate from the runtime acceptance evidence below.

The native ZIP extraction callback may truncate an accepted overwrite destination
before detecting a wrong password. The migrated regression checks the error,
unchanged archive source and correct-password overwrite retry; it does not promise
rollback of an existing destination. This cleanup does not change the upstream
extraction algorithm or provide a destination transaction.

### Validation and coverage boundary for console retirement

The dynamic sidecar shares the prepared module deployment and can load its
selected format extensions. Tests no longer require the limitations of the
retired static Alone2 executable. The last synthetic console stdout fixtures
for cancellation and properties were replaced by native worker coverage and
typed property projection, including undefined sizes, the unsigned 64-bit range
and raw checksums. Menu/toolbar tests observe public operation requests; actual
child creation, formats, SFX, passwords and cancellation remain covered by the
independent operation and native update suites. Source-bound drag tests observe
published spool ownership rather than a backend process-start event.

Runtime evidence on 2026-10-24:

- macOS: all targets compiled incrementally. Installed Cocoa entry and six
  operation consumers passed; the updated entry-only wrapper also passed.
  Installed `7z` independently created, tested and extracted a Unicode/spaces
  archive with exact output bytes. The retired published and private build
  executable was removed; object files and compiler caches were retained.
- Linux: focused native backend, source update, discovery, publication,
  progress, volumes/SFX and copy routing passed (12 suites). Installed X11
  entry/operations and the independent sidecar round-trip passed. Follow-up
  source-update, backend, SFX and typed-property checks passed with one build job
  after an earlier compiler process was killed for insufficient memory; the
  existing build and ccache were reused.
- Windows: installed native interactive operation/shell entry checks passed
  (8 suites), including the independent sidecar round-trip. Focused backend,
  discovery/publication, progress and volumes checks passed. Copy routing needed
  the interactive desktop; its session-0 activation failures do not establish
  a product failure. The interactive copy-routing retry, native path diagnostic
  and SFX naming checks passed. Final directory-sync fixture and elevated startup
  follow-ups also passed; their receipts are listed below.

The initial macOS default run passed 429 of 459 suites before the test-fixture
follow-ups above; it is not a passing full gate. `check.sh` itself stopped on
formatting in the unchanged `src/crypto/modes/ecrdsa_defs.h`. The untouched source/UI
scenarios with unresolved results from that run are `ntfs_stream`, `udf_stream`,
`iso_stream`, `language_dialog`, `panel_navigation`, `ui_layout`,
`compression_encryption`, `panel_creation_path`, `panel_activation`, `panel_key`,
`ui_parity_system`, `root_command` and `archive_recovery_ui`. They include native
filesystem extraction diagnostics/names, dialog/profile expectations, navigation
and two startup segmentation faults. This iteration does not establish a
pre-change runtime baseline for those failures or claim their acceptance.
Focused retries of the retired-transport consumers and benchmark passed without
repeating the full run. Suites labeled `slow` were excluded; manual appearance
acceptance remains unverified by the user's choice.

Final follow-ups passed on all three platforms: native directory synchronization
uses the same native writer seed and relative source paths, and the installed
sidecar round-trip runs from its own temporary working directory. Windows
startup/symlink acceptance passed separately with an elevated interactive token.
The final guest receipts are `20261024T054044Z-1f69eae2` (source update) and
`20261024T054217Z-b1ad34d7` (entry/sidecar). Earlier installed operation receipts
are `20261024T050918Z-087dae69` (Linux) and
`20261024T051558Z-510c8938` (Windows). This does not change the full-gate limits
listed above.

### Qt Remote Objects operation exchange — 2026-10-24

Scope: replace the custom framed JSON exchange between the manager and its
independent operation GUI with a generated Qt Remote Objects interface. The
selected scenario includes Test, Extract, Create/Update, checksum/manifest and
Benchmark, toolbar/context consumers and drag-created archives with temporary
source lifetimes. Existing stream/device/open-container boundaries are retained.

Before editing, compared complete current protocol, launcher, native process
adapter and endpoint bodies, backend factories, manager observation/refresh,
drag waitFinish, GUI entry/dispatcher and all operation fixtures. Compared
upstream Common/CompressCall (Call7zGui/CreateMap and all operation wrappers),
Common/ArchiveCommandLine (ParseMapWithPaths/CEventSetEnd), Windows/ProcessUtils,
GUI dispatch and PanelDrag against the existing port. The selected original
files have no changes between `9128b80e3a471108678e2b3ec3c985861c8d7b0f`
and current `b88c6bc09339de32f7d18bbab900686b244f42ba`. Existing mixed source
baselines in the provenance inventory remain unchanged. Source parser/defaults,
native engine/GUI workers, native process launch/observation and temporary-source
ownership are not reimplemented. The upstream mapping/event transport has no
portable Qt Remote Objects implementation to transfer; this is a platform-service
adaptation of the previously audited independent-process boundary.

`src/operations/operation.rep` defines immutable job data, output notifications, explicit
accept/reject calls and a completion call with a delivery acknowledgement.
`repc` generates marshalling and the interface signature. The application retains
path/option/outcome validation, startup deadline, native crash/exit-code checks,
metadata retention and independent child lifetime. A child copies and validates
the full request before accept; source loss after that point does not cancel it.
Completion waits at most one second for the observer's reply. Native work and
error/cancel policy still belong to the original dispatcher. The old JSON
encoder, decoder, framing and manual socket pump are removed; native Windows
shell argv/mapping entry remains supported by the original source parser.

Qt Remote Objects is a linked SDK/runtime dependency, not vendored or adapted
source. CMake generates both source and replica from the same contract; release
kit installation includes the module, and Qt deployment resolves the linked
library. Official source: <https://github.com/qt/qtremoteobjects.git>, tag
`v6.12.0`, commit `5add069a1b80a345690b911b9c55826a21ca251c`.
Core/Network and `repc` are required; optional QML/Bluetooth/WebSockets transports
are unused. The local shared macOS SDK addition builds that unmodified tag for
arm64/x86_64 with the existing compiler cache. Runtime uses its LGPL-3.0 option;
the SDK retains original notices/licenses. No Qt source snippet was imported
into the application or its third_party inventory.

Source-port review: the selected exchange and its consumers are connected for
all operation families above; no parallel framed-JSON implementation remains.
The original parser, dispatcher, workers and native process adapter are retained.
QtRO's source dispatcher still uses its connection after invoking a source slot,
so public acceptance is queued outside that stack. A behavior regression deletes
the observer from its accepted signal, then releases the child and verifies its
successful completion. Acceptance precedes the public finished signal. Existing
tests also cover parent exit, crash/retry, errors/cancellation, output metadata,
Unicode/literal paths and temporary drag sources. Wire-fragment tests were
replaced by a real QtRO large-selection exchange and semantic validation checks.

Validation on macOS: 11 focused operation/consumer suites passed (68.40 seconds),
four direct UI consumers passed (27.78 seconds), and the installed Cocoa runtime
passed seven suites (81.68 seconds), including 7zFM/7zG entry and independent
7z sidecar round trips. After the observer-lifetime fix, affected installed
launcher/extraction suites passed again. Final drag groups passed (16.66 seconds)
and toolbar/runner checks passed (33.60 seconds).

Windows installed native acceptance passed all eight suites (143.85 seconds,
run `20261024T114150Z-983753d6`), including package/registered-shell entry and
the independent sidecar. Protocol/hash and toolbar-creation focused checks also
passed. Final drag and toolbar groups passed all five suites (116.24 seconds,
run `20261024T121415Z-2db794f0`). The interactive runner's old `current`-only
guard required a branch-worktree correction. Rebase retains the equivalent
correction already integrated in master, with its unsafe-root behavior test.
Complementary elevated installed startup/symlink acceptance passed (74.88 seconds,
run `20261024T122012Z-430537e8`).

Early Linux installed attempts reported native-discovery/source failures and
startup deadlines; later focused follow-ups passed the selected operation and
consumer scenarios. Final Linux drag and toolbar groups passed all five suites
(244.41 seconds, run `20261024T121732Z-4c5cfd86`).
Final installed Linux XCB acceptance passed all seven suites (169.33 seconds,
run `20261024T122326Z-9c41a05c`), including GUI entry and the independent sidecar.
Aggregate toolbar and drag tests exceeded their existing budgets, so their
registrations now separate cohesive scenarios with the same
timeouts and complete original data rows. They share UI locks and each receives
a private startup directory. Test setup now waits before enabling the second
pane; negative-format automation uses original source `-y` to avoid an unhandled
native Windows MessageBox. The benchmark cancellation fixture keeps native work
unfinished until Cancel; extraction's fixture watchdog reaps its own timed-out
child. These changes do not relax production deadlines or alter operation policy.

Validation used the existing incremental builds and compiler caches. No full or
slow suites were run. XCB checks used a virtual display; these results establish
no rendered Windows pixel-parity acceptance. Source completeness above is
separate from runtime evidence on these three checked platforms.

### Independent operation observer lifecycle — 2026-10-31

Compared the complete launcher, endpoint, native process adapter and manager
result consumers with the independent-process boundary reviewed above, including
the original `Call7zGui` and mapping/event callers. The existing original-source
baselines and omissions remain unchanged; no external code is imported here.

The Qt observer now represents its lifecycle with explicit phases instead of
four independently mutable flags. Child acceptance and delivery of the public
acceptance notification remain separate: output/result messages may arrive
between them, while public completion waits for that notification. Native launch,
timeouts, cancellation outcomes, crash/output retention and independent child
ownership retain their existing implementations.

Focused runtime validation passed `operation_protocol`, `operation_launcher`,
`operation_extraction` and `gui_output_refresh` on Linux (4.91 seconds), macOS
(18.57 seconds) and the Windows interactive runner (13.97 seconds), without
skipped rows. These cover acceptance/completion order, observer/parent deletion,
crash/retry, cancellation, output retention and panel refresh. Existing
incremental builds and compiler caches were reused; no full or slow suites ran.

### Independent operation channel owner — 2026-11-02

Rechecked the complete launcher, endpoint and native process adapter, backend
launch methods and manager result/refresh consumers. Original Call7zGui,
mapping/event preparation and process release were compared at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; the complete original
Common/CompressCall.cpp hash matches the recorded baseline. No source import or
provenance advancement.

The private Remote Objects channel now retains an explicit reference to its
owning observer instead of four mutable std::function forwarding callbacks.
Acceptance, rejection, output accumulation and completion handling have named
private bodies outside construction. Channel, host and observer retain their
existing lifetime/destruction order. Deferred public acceptance still leaves
the QtRO dispatch stack before a receiver can delete the observer; output/result
ordering, startup deadlines, native process observation and independent child
lifetime remain unchanged. Generated IPC and public signals are unchanged.

Existing behavior coverage is sufficient for this extraction: no new tests pin
the private structure. Linux primary run `20261102T021909Z-a9476ede` passed
operation_protocol, operation_launcher, operation_extraction and
gui_output_refresh: 38 checks, no skips. Build 25.982 s; tests 3.712 s.
This validates the portable channel/observer dispatch; native Windows/macOS
launch and UI behavior were not newly accepted. Selected scratch directories
were empty; incremental builds and compiler caches retained. No full/slow gate.

### Selected-folder extraction path projection — 2026-11-01

Rechecked original ArchiveExtractCallback CorrectPathParts and its extract/hash
callers, ExtractingFilePath Correct_FsPath and path helpers, Agent extraction
prefix handling and the current directory/native-index validation consumers.
The reference remains <https://github.com/ip7z/7zip.git>, commit
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Complete original callback and
ExtractingFilePath units match their existing baseline hashes. No external
code is imported, baseline advanced or native branch/dependency omitted.

The backend now separates one entry's validated relative output name from
whole-selection name conflicts and destination occupancy checks. The scoped
projection context borrows the existing request values; it creates no retained
reader, worker or parallel mutable settings. Full/current/absolute/no-path modes,
bound-folder prefix removal, native alternate streams, source links and native
name correction retain their previous decisions and diagnostic order. Source
path normalization and actual extraction remain in their existing 7-Zip owners.
This is a behavior-preserving refactor, with no pre-fix product failure claimed.

Existing behavior coverage checks selected folders and unsafe descendants,
bound versus unbound prefixes, overwrite/cancellation, corrected-name conflicts,
absolute outputs, alternate streams and link redirection. No source-presence or
implementation-mirroring test is added.

Six focused suites passed: archive_backend, extraction_options,
native_source_extract, source_extract_advanced, source_link_extract and
native_extract_callback. Linux is the primary shared-behavior check; macOS
checks its native path handling, and Windows covers alternate streams, NTFS
security and reparse links. The four Windows-only rows are skipped on Linux
and macOS. On Windows, four suites passed under the ordinary desktop token;
the two suites creating native symlink fixtures passed with the runner's
scoped elevated desktop token after ordinary-token fixture creation failed.

The CPIO consumers now depend on preparation of their existing external reader.
Their first focused Linux run could not open CPIO because the module was absent
from this branch's runtime; connecting the existing deployment target resolved
that failure. Incremental builds and compiler caches were retained. No full/slow
gate or appearance/visual-parity acceptance is claimed.

### Read-worker dialog responses — 2026-11-01

Compared the original FileManager ExtractCallback password/overwrite bodies,
their current modal adapters and Common extraction consumers with the joined
read worker, directory callback and backend delivery/cancellation/completion.
The complete ExtractCallback.cpp inventory still matches its recorded original
hash at `b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. No external code, decoder algorithm or source
revision changes in this iteration.

Password and overwrite responses now each express value presence directly,
instead of coupling a value with a separate ready flag. An initial empty
password still means no supplied response; explicitly answering with an empty
password is a defined response. An invalid overwrite answer remains a received
failure, rather than an unanswered prompt. Each new overwrite question discards
its preceding answer. Prompt-once tracking, mutex/wait ownership, progress
checkpoints, pause/cancel wakeups, native HRESULTs and post-dialog path validation
are preserved. No generic dialog/decoder framework is introduced.

Existing native test/list/single-entry/directory-extraction and retained-reader
suites cover empty/wrong/preset passwords, retry, multiple overwrite questions,
all/rename/cancel/no-consumer decisions and destruction while awaiting a reply.
No new test is needed for the changed representation; those checks observe
operation results and output/source bytes rather than private response fields.

Focused Linux validation passed native_archive_test, native_listing,
native_source_extract, extraction_options and archive_readonly_view, with no
skipped rows. Only portable Qt response ownership changed, so secondary OS,
native dialog appearance and physical interaction are not claimed. Source
inventory and whitespace checks passed; the existing incremental build and
compiler cache were retained with ordinary four-job defaults. No full or slow
suite ran.
