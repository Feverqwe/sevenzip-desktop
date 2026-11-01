# Local read-only SMB: implementation and evidence

## Pending read completion ownership — 2026-11-01

Reviewed read admission/range validation, callback completion, event-loop reply
publication, disconnected-client destruction and the QtRO reader consumer.
The local pending-read owner now contains its synchronized optional result;
callback and server-loop consumers no longer manipulate a separate ready flag
and result under separately written lock scopes. Its maximum reply length is
fixed on construction. Taking a result retains the completed state, so late
duplicate callbacks remain ignored. Shared lifetime and pending-count release
still extend beyond client disconnection when a completion retains the owner.

This is a local C++ integration change. Existing libsmb2 READ/STATUS_PENDING,
message correlation and signed reply calls, admission limits, cancellation,
bounds and native decoding are unchanged. No external source is imported and
no recorded baseline changes. Session/broker recovery states were also reviewed
and retain their separate meanings; OS launch/unmount adapters are unchanged.

A separate-process wire regression completes each asynchronous read from
another thread twice with conflicting results. It verifies exact first bytes,
first-error preservation, subsequent healthy Unicode-path reads and final-file
range clipping. Existing transport checks cover delayed extraction consent,
metadata availability during that wait and cancellation without publishing data.
Linux `smb_readonly_server` and `archive_mount_transport` passed 19 checks; five
existing native OS mount opt-ins were skipped. No real mount, OS authorization,
secondary platform or appearance acceptance is claimed. Both selected scratch
directories were empty after the run; incremental outputs and caches remain.

## Decoded cache publication ownership — 2026-11-01

Reviewed the complete retained view, cache acquisition/eviction/materialization,
native single-entry worker, input-layer ownership and mount-source callers.
Compared the existing output-stream/Extract/Close boundary with original
Client7z GetStream and SetOperationResult at
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>. The complete original Client7z.cpp matches
its existing inventory hash. Native decoding and source algorithms are unchanged;
no external code is imported or baseline advanced.

A decoded file remains owned by its joined request until successful cache
publication. One scoped cleanup replaces the separate failure-path removals
and also covers exception unwinding from a recovery-consent service. Cleanup
only removes output actually created by that request. Local input handles are
released before removal; successful publication transfers ownership to the
existing cache and its eviction/destruction paths.

Previously a throwing recovery decision could leave an unpublished decoded file
that prevented a later attempt from creating its output. The public damaged-ZIP
test now covers both a refused decision and a throwing decision, then checks
successful retry with fresh consent, exact bytes, retained CRC warning, cached
reads and a healthy sibling. No private cache paths or implementation-presence
assertions are exposed. No pre-fix runtime reproduction is claimed.

Focused validation passed archive_readonly_view and archive_mount_transport on
Linux, then archive_readonly_view on Windows. The extra Windows check covers
closing file handles before deleting decoded outputs; no distinct macOS path
was changed, so no macOS rerun was selected. Both recovery-decision rows pass
on the checked platforms. The transport suite's opt-in native OS mount rows
remain unrun; this is not native-mount or visual acceptance. Existing build
trees and compiler caches were reused, with no parallelism override or full/slow
gate.

## Read-only cache ownership — 2026-10-31

Reviewed the complete retained-view/cache/read-operation/source pipeline and its
callers. Native extraction completion is successful only for the engine's OK
operation result; accepted recoverable output retains its positive operation
code. Existing extraction and recovery consent remain separate, including the
encrypted-output refusal. No new external source is imported.

Namespace preparation and range validation stay in the view implementation.
Stream acquisition, eviction and materialization now have one private source
owner with shared private state, without a new public interface. The former
530-line unit becomes a 350-line view and a small cache implementation. Cache
policy, bounded output, direct-stream fallback and source reader leases are
preserved. The new units are classified as local integration in the existing
inventory; original revisions and hashes are unchanged.

Cached result metadata is formed in one helper for new and retained entries.
The duplicate unverified flag is removed: the retained native operation result
already distinguishes successful and explicitly accepted unverified extraction.
The direct-stream sentinel remains unchanged. Active-operation registration now
has scoped cleanup under its existing mutex, clearing the cancellation target
before the stack operation is destroyed on normal return or unwinding.

Existing behavioral suites cover ranges, quotas, eviction/fresh consent, retained
reader generations, damaged ZIP consent and cached warnings, output limits and
cancellation. No private-state or source-presence tests were added. A synthetic
allocator-failure run is not part of this evidence.

Final focused checks passed on Linux (four suites, 3.73 seconds), macOS (four,
7.79 seconds) and Windows (three, 4.10 seconds). The Unix-only transport suite
accounts for the platform difference; its five opt-in native rows remain gated
in each Unix default run. Separate native probes passed the manager's EN/RU
setup, extraction consent, owner-close cancellation, busy-unmount/continued-read
and final detach on Linux (two scenario rows, 1.31 seconds) and macOS (two,
6.92 seconds). The elevated Windows synthetic exFAT device probe passed source
loss, retry and detach (one scenario row, 1.88 seconds), without skips. These
probes do not establish all gated lifecycle cases, UAC/PolicyKit interaction or
Windows manager visual parity.

Source inventory and whitespace checks passed. No full/slow suite or heavy
diagnostics ran; incremental builds and compiler caches were preserved. Compact
logs/results and two busy-manager JSON reports per Unix platform are retained;
the disposable report originals were removed after capture.

## Session cleanup state — 2026-10-31

Reviewed the complete manager/session/source/control/platform chain, including
preparation publication, cancellation during mounting, busy unmount, broker
recovery and owner-close continuation. This is local integration without a 7zFM
counterpart; native reader, SMB protocol and OS-client implementations remain
unchanged. No external source is imported.

Session cleanup now derives from its existing `Stopping`/`Stopped` phase instead
of maintaining a second mutable flag. Single-start and deferred-unmount requests
remain separate facts. The server and reader still stay alive until the OS
confirms unmount; an unmount error retains the connection for retry. Cleanup
still removes only known empty directories, without recursing through a mount.

New public behavior checks cancel TAR/ZIP preparation immediately and from its
prepared-state notification. They verify one stopped notification, no late server
error and unchanged archive bytes. Focused suites passed on Linux (five suites,
4.49 seconds), macOS (five, 8.20 seconds) and Windows (four, 25.36 seconds).
`archive_mount_transport` is Unix-only. The final cancellation assertion was
rechecked in the affected target on macOS (1.61 seconds) and Windows (0.56 seconds);
the final Linux batch already included it. Opt-in native-client/lifecycle rows
remain gated in the default suites.

Separate native probes passed the manager's EN/RU setup, extraction consent,
owner-close cancellation, busy-unmount/continued-read and final detach on Linux
(two scenario rows, 1.33 seconds) and macOS (two, 6.32 seconds). The elevated
Windows synthetic exFAT device probe passed source loss, retry and detach (one
scenario row, 2.55 seconds). These probes had no skips; they do not establish
new UAC/PolicyKit interaction or Windows manager UI acceptance.

Linux validation resumed after the user restored its removed Qt kit and stale
Qt package discovery was refreshed. The build tree and compiler cache were
preserved; the one-time repair script was removed. Existing source inventory
verification and whitespace checks passed. No full/slow suite or heavy
diagnostics ran. Two busy-manager JSON reports per Unix platform are retained;
their disposable originals were removed after capture.

## Scope and current boundary

2026-10-25, `feature/readonly-smb`, based on `6d955531`.
The requested product scenario is a loopback-only SMB mount backed by the existing
readers, with no separately installed filesystem driver or server. The user
explicitly authorized a research fork of libsmb2 after reproduction of missing
server-side compound handling. That authorization permits protocol fixes inside
the fork; it does not authorize disabling OS security settings or creating new
format readers.

The user clarified the Linux prerequisite: use the system CIFS client. If CIFS or
its required mount utility is unavailable, explain what the user must install
(including `cifs-utils` where applicable). Do not build a replacement driver or
install packages automatically. This Linux prerequisite is an explicit exception
to the original no-extra-component deployment request.

The user additionally requires explicit confirmation before extracting any file
for a mount. Direct seekable reads and reuse of an existing cache do not extract.
The backend now denies materialization when there is no consent callback or the
user declines; every new materialization requires authorization before temporary
output is created. This is connected to the product manager; a second, independent
default-deny choice controls use of recoverable unverified extraction output.
Cached unverified output retains its warning. Wrong-password/encrypted failure
output is never offered as recovered data.

Mount setup checks seekable-stream availability for the selected folder using the
retained native reader, without reading file contents or extracting them. Only
folders requiring materialization show cache settings and Help/F1. The prepared
view is passed into the session, preserving the open container chain. Capability
depends on the reader: extraction-only image readers still require a cache.
This is local mount integration with no upstream 7zFM counterpart. Its localized
help uses the existing Qt help viewer; the original bundled CHM pages are unchanged.

Tools exposes Mount read-only and Read-only mounts for a retained native
container. The modeless manager owns sessions independently of panel navigation,
provides paths and unmount actions, preserves a busy mount and intercepts owner
closure. A separate SMB child and OS-client broker communicate over user-private
Qt Remote Objects endpoints. macOS uses NetFS; modern Windows uses a restricted
global SMB mapping after UAC; Linux calls the system mount.cifs after system
authorization. Credentials never enter process arguments. Acceptance evidence
and remaining platform limitations below must be distinguished from source scope.

## Library selection and provenance

The C++23 dependency facade wraps synchronous connection delivery and UTF-16
name conversion. Callbacks borrow library-owned sessions, cannot unwind across
the C boundary, and remain alive until the serve loop returns. Conversion uses
the library's original routine and allocator with RAII ownership; results use
`std::expected`. Existing raw protocol structures stay at the local integration
boundary. The library's C protocol implementation and original baseline bytes
are unchanged. The same facade API is available through a header when named
modules are unsupported. Unicode and empty names, callback failures, cancellation
and rejected reads are exercised through the focused SMB checks.

On 2026-10-27 the Linux GCC/UBSan run stopped in `smb2_create_signing_key` during
the initial NTLM challenge: the original SMB2 branch called `memcpy` with a null
session-key pointer and zero size. The complete function, session-setup caller,
context reset and NTLM key allocator were compared with the pinned revision.
That branch now copies only a nonempty key. Empty-key state, key derivation,
authentication and signing policy are unchanged; original inventory hashes
remain pinned. The existing authenticated connection/cancellation/forged-read
scenarios reproduce the failure under UBSan and validate the correction.
The forged-read fixture also queues CLOSE for the disconnected attacker's handle,
so context shutdown can release it; Linux LSan had reported the two omitted
handles. The Linux module preset uses Clang 19 because Clang 18 with Ubuntu's
libstdc++ omits `std::expected`. Configuration now checks that required library
facility before constructing targets.

Selected research candidate: [sahlberg/libsmb2](https://github.com/sahlberg/libsmb2),
commit `1395c54006f0cfb4da5df4b9fd9bf531feffe56a`. The
[baseline](../libsmb2-server-baseline.json) pins complete original inventory units,
including the headers, source/build dependencies and original example used for
assessment. `adapted` maps original units to their authoritative local owners in
`third_party/libsmb2`; unchanged dependencies are distinguished from local fixes.
The reference example is studied, not shipped. The two probes are local API
integration, not copies of its virtual disk implementation.

The library is LGPL-2.1-or-later; notices and the original license are retained in
`third_party/libsmb2`. Its C sources remain C. DCE/RPC, Kerberos/GSS providers,
QUIC/RDMA transports and example utilities are not selected for the application
build. Only the public server API is a candidate integration boundary; the
directory wire diagnostic also inspects private output vectors to check emitted
data. Product consumers must not depend on those private vectors.

The fork retains upstream module boundaries. Current changes are:

- `socket.c`: bind only IPv4 loopback; retain compound inheritance within one
  received chain, after signature verification against original request bytes.
- `libsmb2-private.h`, `smb2-cmd-create.c`, `pdu.c`, `libsmb2.c`: retain effective
  FileId/SessionId/TreeId and failure status for related operations. Handlers
  receive resolved handles, not protocol sentinel values.
- `smb2-cmd-query-directory.c`: relative NextEntryOffset, eight-byte entry
  alignment, bounded output and an error when no entry fits. Unsupported
  directory information classes do not masquerade as an empty directory.
- `libsmb2.c`, public server header: allow callback errors to carry NTSTATUS,
  preserving the original fallback for `-errno`/`-1`. This lets adapters report
  read-only, missing-handle and source errors without queuing a response while
  the callback still uses its request.
- Directory reply codecs support DIRECTORY/FULL/BOTH and ID_FULL/ID_BOTH using
  the library's existing universal directory structure. QUERY_INFO adds scalar
  Internal/EA/Access/Mode/Alignment and Name information through the existing
  encoders. File and directory timestamps preserve pre-1970 values; QUERY reply
  zero differs from SET_INFO's unchanged-time sentinel.
- NTLM challenge flags include NEGOTIATE_NTLM and TARGET_TYPE_SERVER, required
  by the native Windows SSP. Existing NTLM/signing primitives remain authoritative.
- A positive READ callback result uses the library's existing STATUS_PENDING,
  AsyncId and final-reply correlation. Qt IPC completion queues the final reply
  on the server event loop; consent and decoding no longer block metadata traffic.
  Outstanding reads and clients are bounded. CANCEL has no response of its own
  and does not replace an outstanding request in the correlation queue;
  cancellation of already running decoding is best effort at the OS-request
  level, while session cancellation aborts its reader and pending consent.
- POSIX select descriptors are checked against FD_SETSIZE before use.

Only signed SMB 2.1 is accepted. This is a local read-only application adapter,
not acceptance of the library as a general network server. Forged READ signature
and cleared-signed-flag regressions verify rejection before invoking the reader;
broader hostile-protocol fuzzing and additional dialects are outside this evidence.
No new signing, NTLM or encryption algorithm is implemented by the adapter.
The library's global context registry requires one server instance per process,
without client contexts in that process. The product's child isolation supplies
that boundary; wire tests use a separate synthetic server process as well.

### Reproducible review

Use a separate source checkout of the repository above. Original bytes are checked
without depending on the current adaptation:

```sh
python3 scripts/upstream_baseline.py reference-verify \
  --manifest docs/libsmb2-server-baseline.json --upstream build/review/libsmb2
python3 scripts/upstream_baseline.py reference-review \
  --manifest docs/libsmb2-server-baseline.json --upstream build/review/libsmb2 \
  --new-revision <proposed-full-commit> --output build/review/libsmb2-update.md
```

The existing review includes each mapped local adaptation alongside the original
baseline and proposed upstream revision. Do not update baseline hashes to match
local protocol fixes. Review dependencies and omitted scope before updating.

### Alternatives investigated

| Candidate | Source findings and decision |
| --- | --- |
| [SMBLibrary](https://github.com/TalAloni/SMBLibrary), `582635541d2ec0c84eac5f66cbebd41e2d35c7ae` | C# server with INTFileStore and independent NTLM provider, LGPL-3.0-or-later. The reviewed receive/dispatch path parses requests and signs responses, but does not call the available signature verifier for incoming requests. Not selected without a security fix and a bundled runtime/deployment evaluation. This is a source finding, not a native interoperability test. [Reference inventory](../smblibrary-server-baseline.json). |
| [Impacket](https://github.com/fortra/impacket), `e9ae5bc550689fdcac37cd55bc33c5e1370e1e49` | Python server, modified Apache license with additional component notices. `processRequest` replaces the received SMB2 signature by calling `signSMBv2` instead of authenticating it; filesystem operations use local OS paths and descriptors. Virtual readers would require replacing multiple command handlers. Not selected. No native runtime test. [Reference inventory](../impacket-server-baseline.json). |
| [Samba](https://www.samba.org/) | Mature Unix server with [VFS modules](https://www.samba.org/~ab/output/htmldocs/Samba3-Developers-Guide/vfs.html), loopback binding and alternate ports. A bundled `smbd` plus version-bound VFS module is a different deployment from a small server library, and does not establish a native Windows server solution. Not built or ruled out universally; retained as an alternative if the libsmb2 fork is abandoned. No Samba source imported. |

## Source integration review

The upstream 7-Zip reference checkout is `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
`CPanel::OpenItemInArchive` in `CPP/7zip/UI/FileManager/PanelItemOpen.cpp` obtains
`IInArchiveGetStream`, then uses the source CopyTo/materialization workflow when
needed. No usable upstream SMB server or mount-session owner was found in the UI
source. The SMB library adapter and lifecycle integration are new platform glue.

Existing local owners reviewed before reader integration:

- `ArchiveBackend::openNativeArchive`, `openDeviceFilesystem` and
  `extractNativeArchive` retain the existing backend boundary.
- `QtArchiveFolderManager::OpenLinkImpl` binds streams, device inputs, recovery
  consent and native reader generations. Reopening a physical path alone would
  lose nested-container ownership and consent state.
- `QtArchiveFileResource` retains the codec catalog, native reader, direct input,
  materialized temporary lease and limitations. The mount must retain a resource
  generation independently of panel navigation.
- `CreateArchiveItemStream` retains that resource and serializes independent
  cursors through `ArchiveNativeOpen::itemStreamMutex`; it exposes seek only when
  the native stream supplies it. Nonseekable entries need the existing bounded
  materialization/cache route, not replay from offset zero for every request.

`ArchiveReadOnlyView` retains that resource generation, projects a safe immutable
tree and obtains seekable native streams. Nonseekable entries use the existing
`QtArchiveReadOperation` with a retained native reader, a bounded output stream
and explicit extraction consent. Recoverable unencrypted CRC/data/truncation
output requires an additional scoped choice before exposure. Unsupported decoding,
missing bytes, cancellation and password requirements remain errors. Unverified
data is reported visibly and never becomes a verified extraction success.

`QtArchiveMountSource` owns the reader on a dedicated thread; `SevenzipSmb` owns
the server and receives only immutable metadata and bounded range replies over
a user-restricted local QtRO endpoint. Credentials are generated per session.
The macOS session owner uses NetFS rather than credentials in process arguments,
and keeps the reader/server until unmount succeeds. Native tests pass for TAR and
ZIP through this owner, including extraction consent and cleanup. NetFS receives
a canonical mount path (its path validation rejects symlink ancestors); its
completion is delivered through a Qt queued call rather than depending on a GUI
dispatch queue in offscreen tests. The OS broker now owns the native mount in an
independent process and force-detaches after losing its controller. The existing
device/nested resource lease is retained rather than reopening paths. Nested TAR
native media checks pass after both folder/window services close. The synthetic
direct-device view check passes retained exFAT reads, an explicitly allowed dirty
opening, source disconnect/error and retry. A missing direct-device limitation
description falls back to a visible indication of the selected limited mode.
Physical-device authorization/removal through a native mounted path is unverified.

## Runtime evidence

### Current-folder projection and skipped entries

The panel command captures the source folder's bound path together with its
retained reader generation. Only that folder's descendants are projected, with
the folder prefix removed before SMB name validation. An explicitly bound `.`
or `..` archive directory is therefore usable without interpreting it as host
filesystem navigation. Siblings are excluded, including sibling paths that merely
start with the selected folder's name. The existing native indices and stream
leases remain authoritative for reads and per-file extraction consent.

Unrepresentable names, conflicting entries, unreadable item metadata and items
without usable sizes are skipped individually; supported entries remain available.
The first accepted namespace entry is retained on a conflict. Cancellation and
resource-exhaustion failures still stop preparation. Each skip records its source
path and reason in diagnostic output and the selected session's details log;
there is no count-based gate or substitution of unsafe names.

Items identified by `kpidIsAltStream`, including ext `$XATTR.*` metadata, are also
logged and omitted from this projection. Their contents remain available through
Sevenzip's existing reader and extraction paths. The mount does not claim to
restore xattrs or expose SMB named streams. This reuses the existing upstream
`Archive_IsItem_AltStream` helper and `CAgentFolder` path contract without importing
or changing a reader. Regressions cover the real panel command, scoped TAR/ZIP and
CPIO reads, retained extraction of an omitted stream and native SMB folder contents.

### Case-distinct entries

The archive projection and server namespace preserve case-distinct files and
directories. The server advertises case-sensitive search and uses it for opens
and directory patterns. It no longer folds paths to lowercase for identity;
canonical Unicode normalization stays in place; duplicate or unsafe paths are
omitted and logged during projection. This corrects local integration, without importing or changing an
archive reader or the SMB library. Backend regressions read different bytes from
case-distinct TAR/ZIP files and directories. The native-client fixture alternates
opens of `libip6t_hl.so` and `libip6t_HL.so` with different contents, so an alias
or client cache substitution is detected.

2026-10-25: this native-client case passes on macOS (NetFS), Windows 11
(restricted native global mapping) and Linux (`mount.cifs`), alongside the
existing detach/owner-loss scenarios. No system case-handling or SMB security
policy was changed. The advertised search capability follows
[FileFsAttributeInformation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_fs_attribute_information).
The CPIO regressions also declare their external reader runtime dependency,
so focused builds do not depend on modules left by an earlier application build.

### CPIO current-directory entries

The mount namespace accepts explicit current-directory records and `./` path
components, as found in installer Payload CPIO archives. They refer to the mount
root and ordinary descendants. Parent traversal, absolute paths and conflicting
names are omitted and logged. This is local SMB namespace integration; the upstream
extraction path correction also handles dot components, but additionally changes
unsafe names, which is unsuitable for a faithful mounted namespace. No reader or
external source was imported for this correction. Synthetic CPIO regressions
reuse the nested-copy fixture and check both retained reads and signed SMB reads.

2026-10-25, macOS 27.0.1 arm64, native `/sbin/mount_smbfs`, synthetic fixture only.

Before the compound fix, the server issued handle `1`, then received the related
QUERY_INFO FileId `ffffffffffffffff`; native mounting failed with `Bad file
descriptor`. After the fix the same opening succeeded. This follows the
[related-request contract](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/46dd4182-62d3-4e30-9fe5-e2ec124edca1),
which requires inheritance from the preceding operation. The public callback API
had no effective-handle resolution or complete compound context for an adapter.

The original directory encoder emitted offsets `96, 192, 0` for three equal-sized
entries and returned 288 bytes for a client buffer of 96 bytes. The focused probe
failed. The fork emits `96, 96, 0`; the bounded case returns one 96-byte entry.
Both focused checks pass. The probe checks observable response traversal and
buffer bounds; it does not search source text or pin private symbol spelling.

The first successful mounted fixture used three synthetic 64 MiB files. Native
checks passed for all names (including Cyrillic/spaces), nonsequential reads,
three parallel readers and mmap. macOS presents decomposed Unicode names; checks
compare normalized names. Write/create/delete/rename/metadata attempts received
EROFS through the read-only mount. This does not alone prove every server-side
write rejection; direct request checks remain necessary.

`lsof` confirmed `127.0.0.1:20445`; `smbutil statshares` reported SMB 2.1,
SIGNING_REQUIRED=TRUE and SIGNING_ON=TRUE. No global client security setting was
changed. A synthetic username/password was used, never an OS-account password.
The mount was removed before stopping the test server.

SMB 3.1.1 with the original example reached server authentication but did not
complete mounting. The exact cause was not isolated; this is not accepted SMB3
support. The shared production adapter subsequently passed native range reads
beyond 4 GiB, EOF, mmap and parallel reads against a synthetic `(5 GiB + 123)`
byte virtual file. The probe does not allocate that entire file.

The retained-reader and child transport suites pass with synthetic TAR and ZIP:
range reads survive panel-resource release, direct seekable reads avoid
extraction, denied extraction produces no data, explicit consent enables bounded
materialization, cached reads require no new consent, wrong credentials fail,
and direct SMB create/write/delete/rename/mkdir requests fail. The local socket
transport check requires an unsandboxed run on this host; sandbox socket creation
is denied. Additional focused checks pass for cancellation while awaiting consent
and fresh consent after cache eviction. The native macOS NetFS test passes for
mount/read/unmount of retained TAR/ZIP readers through the child. EN/RU manager
checks exercise extraction consent, Copy Path, owner-close Cancel and busy
unmount: the server remains readable until held files close and normal unmount
then succeeds. Native-client tests also detach after loss of the owner channel.

The extraction and unverified-output confirmations default to Cancel. Return,
Escape, close and widget destruction deny; explicit acceptance is required.
The contextual dialog uses standard Qt labels and button boxes with wrapped
plain-text paths. EN/RU normal/compact widget JSON is checked on each platform;
Windows exposed a form-wrapping height issue; vertically grouped fields and
explicit wrapped-caption heights now pass normal/compact EN/RU geometry checks.
Native Cocoa modality, VoiceOver
and rendered Windows comparison have not been checked. Apple HIG pages were
inaccessible to the web reader beyond JavaScript/unsupported Markdown; the local
`macos-hig` reference dated 2026-10-18 informed the new contextual alert.

Build and run the focused library checks with the normal worktree wrappers:

```sh
./scripts/build.sh dev smb2_directory_probe smb2_mount_probe
./scripts/ctest.sh --preset dev -R '^smb2_server_directory$' --output-on-failure
```

The mount probe is opt-in: it starts a synthetic local SMB server with public test
credentials. It does not open images/devices. Use a private empty mount directory,
the native client with read-only/no-browse/new-session options, and unmount before
stopping it. Native diagnostics can block inside the OS despite a subprocess
timeout; preserve that lifecycle ordering.

PDFKit has read the generated PDF's page/text through a mounted native TAR reader;
AVPlayer has played and decoded changing frames after forward/backward seeks.
No extraction occurred. AVFoundation releases file handles asynchronously, so
the check keeps the server alive and retries a busy normal unmount. This is
native-framework acceptance, not a rendered Preview/QuickTime UI check: computer
use access to Sevenzip was unavailable in this session.

A synthetic exFAT device is also exercised through macOS and Windows native SMB mounts:
removal before the first data read reports an entry error without successful
bytes, and restoring the source permits a correct retry. The source's disconnect
switch is atomic because native reads run on another thread. A separate macOS native
session test kills its own SMB child after a successful read, observes the session
failure and force-detaches the remaining OS mount. These are deterministic
simulations, not physical-device removal acceptance.

The broker acknowledges its private control channel before starting a native
mount. Unexpected broker exit retains the server and session parameters, including
when the mount-path acknowledgement was lost. Retrying unmount starts a recovery
broker: macOS checks the exact mountpoint and SMB share, Windows selects only the
exact session UNC mapping, and Linux checks the protected session mountpoint in
the kernel mount table. A mismatching volume is preserved. The macOS native
lifecycle test kills only its own broker, verifies continued reads, and successfully
detaches through a replacement broker. The same lifecycle scenarios now pass on
Windows using the current MSVC build: normal detach, control-channel loss, and
terminating only the test's own broker followed by continued reads and recovery
detach. The updated Linux broker compiles and its prerequisite/device/wire checks
pass. Linux normal detach and loss of the controller channel also pass through
mount.cifs; replacement-broker recovery after a forced helper crash is not yet
runtime-accepted on Linux.

Windows 11 Pro 10.0.26300 x64 passed alternate-port signed SMB 2.1 synthetic
directory/Unicode/spaces, parallel ranges, >4 GiB, EOF, mmap and write-denial
checks. An unqualified synthetic username failed in SSP before server authorization;
`SevenzipLocal\\<session-user>` succeeds without changing signing or NTLM policy.
The production private-IPC broker passed New-SmbGlobalMapping with FullAccess
restricted to the requesting account. Actual UAC interaction and a non-elevated
consumer's access to that global mapping remain unverified.

Ubuntu 24.04 x64/kernel 6.8 passed the same native kernel CIFS data checks via an
explicit util-linux diagnostic. The initial runner had no mount.cifs, and the
missing-cifs-utils explanation was checked in that state. The user subsequently
authorized installing cifs-utils (7.0); no Samba server or replacement driver was
installed. The actual Sevenzip broker now passes system mount.cifs connection,
reading and unmount with the runner's existing sudo authorization. EN/RU manager
checks cover setup, extraction consent, Copy Path, owner-close cancellation,
continued reads after a busy unmount and successful detach after the file closes.
A native exFAT source-loss/retry scenario also passes. PolicyKit's interactive
authorization path is unverified because pkexec is not installed on this runner.
`noserverino` avoids relying on an unsupported QFid CREATE context;
the client assigns inode values. Hardlink inode identity is not promised.
The private Linux installation passes native X11 startup. Both installed helpers
reach their command-line entrypoints with developer library paths removed;
`ldd` resolves their Qt/libsmb2 dependencies without the Qt kit or build tree.
After cifs-utils installation, the relocated device probe also passes source
loss/retry and normal unmount through that installed runtime. Its driver and the
separate test-only QtTest copy have their ELF search paths rewritten to the
installation; the production helpers do not use the developer Qt kit.

The Linux native manager run exposed a Russian compact-window minimum width
larger than requested when force unmount was visible. Mount actions now occupy
their own row above the standard Close footer. EN/RU normal/compact widget
geometry and the actual busy-mount scenario are checked on macOS and Linux.
The follow-up Windows geometry run is blocked by shared runner maintenance;
earlier Windows native/installed mount acceptance remains separate from that
unverified layout update.
Widget responders use the existing fixture that selects Qt dialogs; native
NSAlert automation is not inferred from that check. The test waits for session
cleanup before checking that the owner may close.

macOS runtime packaging installs the dynamic libsmb2, helpers, Qt dependencies
and licenses, rewrites loader paths and signs nested code. Installed Cocoa startup
passes. The installed native PDF/video tests for root and nested TAR also pass using the bundled server,
OS broker and codec module with developer DYLD paths removed. Only its temporary
test driver used a separate QtTest library; the test overlay was removed and the
original bundle's deep/strict signature verified afterward.
The private Windows installation passes the native exFAT loss/retry/unmount
scenario and GUI startup with PATH restricted to the installed payload, system
directories and a separate test-only QtTest runtime. The driver is temporarily
placed next to the installed broker and removed afterward; the server and native
codec are selected from that installation. No developer Qt/server directory is
available to the child processes. Reproduce this installed check after preparing
the runtime with the existing packaging command:

```sh
python3 scripts/hyperv-runner.py run windows --interactive --elevated --skip-runtime -- \
  scripts/probe-native-smb.py --product-device --installed-root build/readonly-smb-install
python3 scripts/hyperv-runner.py run linux --skip-runtime -- \
  scripts/probe-native-smb.py --product-manager
python3 scripts/hyperv-runner.py run linux --skip-runtime -- \
  scripts/probe-native-smb.py --product-device --installed-root build/readonly-smb-install
```

Current Windows focused regressions cover the backend, retained reader lifetime,
device handling, forged SMB signatures, extraction prompts and command consumers.
Physical source removal, rendered application media UI and broader malformed-protocol cases remain
outside current runtime evidence. Slow suites and full/release gates were not run.

### Platform constraints to validate

[Windows alternate-port mapping](https://learn.microsoft.com/en-us/windows-server/storage/file-server/smb-ports)
requires Windows 11 24H2 / Server 2025 or newer and administrator permissions.
Older Windows clients cannot simply use a free alternate TCP port. Do not disable
the system SMB service or change global signing policy to free port 445.
The [Linux kernel CIFS documentation](https://docs.kernel.org/admin-guide/cifs/usage.html)
distinguishes kernel CIFS support from the recommended mount.cifs helper; neither
may be assumed present on every desktop. The product must detect the native
capability and explain a concrete limitation rather than installing a filesystem
driver or promising universal mounts.

### Manager session records — 2026-11-01

Reviewed the complete manager creation/selection/details/remove/owner-close
path, session startup/cancellation/stop cleanup, extraction prompts and source
issue delivery against the existing local integration scope above. There is no
upstream 7zFM mount-session owner to transfer; native client, SMB reader and
session lifecycle implementations and their recorded source baselines are
unchanged. No external code is imported.

Each manager record now keeps its session and read diagnostics together. This
removes the separate pointer-keyed diagnostic table and paired removal step.
Queued issue delivery resolves the retained session record and ignores a record
already removed from the manager. Selection, detail ordering, translations,
action availability, stopped-only removal and owner-close decisions retain
their previous behavior. QObject session ownership and platform operations are
unchanged; no text, layout, visibility or geometry change is introduced.

The existing two-session removal regression now checks distinct diagnostics,
cancellation-report exclusion, selection switching and new diagnostics after
the preceding row is removed. It detects cross-session attribution or lost
messages after row changes through public session notifications and displayed
details; it does not inspect manager storage or private helpers. Setup fails
before starting a server or native mount, preserving the fixture source bytes.

Focused Linux validation passed mount_extraction_prompt, mount_session and
archive_mount_transport: 50 passed rows, with five native-OS mount rows left
behind their existing opt-in. The manager removal/diagnostic regression ran;
the installed CIFS prerequisite was detected without installing or mounting
anything. Only portable manager state changed, so secondary OS and native mount
acceptance are not claimed. Source inventory and whitespace checks passed;
ordinary four-job builds reused incremental outputs and the compiler cache.
No full/slow suite, appearance inspection or platform adapter change is involved.
