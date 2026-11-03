# File-transfer ownership

## Prepared source/destination pairs — 2026-11-03

Rechecked the original FSFolderCopy destination construction, direct-path gate,
self/child checks, copy/move and overwrite callbacks with the current worker,
transaction and window/direct-path consumers. The recorded native inventory and
algorithms remain unchanged; this refactors local batch preparation without an
external import or baseline change.

Preparation now retains each source with its destination instead of returning
a target list indexed against a separate source list. Execution, progress and
failure diagnostics use those checked pairs. Ordered validation, duplicate-target
comparison, overwrite/rename decisions, staging, cancellation, exception cleanup
and partial-Move recovery keep their existing behavior.

The public regression rejects a batch whose later source is missing or maps to
an already selected output name. Both Copy and Move must fail without asking to
overwrite the first existing target; source/destination bytes and the destination
directory must remain intact. This protects all-input preflight before any
per-item interaction, beyond existing mid-transfer rollback cases.

## Filesystem terminal outcome owner — 2026-11-02

Reviewed the complete transaction run/finish, rollback, source-removal, worker
entry and joined window/direct-path consumers. Compared native FSFolderCopy
CopyTo and its callback boundary at recorded
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` in
<https://github.com/ip7z/7zip.git>; the complete original unit matches its existing
baseline hash. Native copying, overwrite, progress and filesystem algorithms
remain unchanged. No external import, omission or baseline advancement.

The transaction returns its completion kind and message instead of changing
the worker's terminal status and emitting its signal. The worker handles escaping
transaction exceptions, stores that outcome and publishes completion in one
place after the transaction returns. The public signal and status contract are
unchanged. Cancellation before completion, skipped merged sources and failure
after source removal retain their distinct outcomes and translated messages.
The current-item/batch guards and partial-Move recovery ordering are preserved.

Existing behavior regressions cover ordinary/direct-path copy and Move,
overwrite choices, cancellation, throwing filesystem services, partial source
removal, EN/RU messages, archive workflow integration and window shutdown.
No private-result representation test or pre-fix failure is claimed. Five
affected Linux suites passed: file transfer, direct-path transfer, EN/RU transfer
results, archive workflow and window worker progress. Incremental build/test
times were 53.043/3.158 seconds. No native OS branch, layout or text changed,
so no secondary-platform, full/slow gate or appearance run was selected.

## Bounded stream preflight — 2026-11-02

Compared the native full-output path construction in ArchiveExtractCallback at
recorded `b88c6bc09339de32f7d18bbab900686b244f42ba` with the Qt preflight caller
and node checks. Native construction and runtime bounds remain unchanged;
there is no import or provenance advancement.

The Qt node loop interpreted each validated component as a separate path.
On Windows, a stream owner named after the current drive made `C:stream` select
the drive's current directory instead of the extraction destination. A real WIM
regression reproduced refusal caused by an unrelated directory before native
extraction. Preflight now appends components to its bounded path before cleaning
it. The regression checks extracted owner/stream bytes and preserves both the
unrelated directory and original archive. Three Linux suites passed for the
shared validator and two Windows suites passed for real extraction and link
boundaries, including the previously failing WIM case. macOS drive-relative
interpretation is unaffected; no new macOS acceptance run is claimed.

## Archive Move extraction outcome — 2026-11-02

Compared the selected native extraction, overwrite, stream, directory and deferred
link bodies, Agent extraction/update callers, panel worker join and update pairing
with recorded `b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>. Complete original Agent, AgentOut,
ArchiveExtractCallback, ExtractCallback, PanelCopy, UpdatePair and UpdateCallback
inventory hashes match the existing baseline. No external import or provenance
advancement. Native decoding, path/link validation, overwrite decisions, filesystem
enumeration, pairing and archive writing remain authoritative.

Native extraction intentionally returns S_OK for overwrite No/NoToAll. The old
Move continuation treated that result as permission to remove every selected
source entry, including an entire directory with a skipped child. A real pre-fix
Linux run reproduced loss of the original archive bytes while different existing
destination bytes survived, for a file and two directory overwrite choices.

A joined extraction report now distinguishes accepted outputs from its HRESULT.
It accounts for skipped output and deferred link completion; solid neighbours
processed without extraction do not invalidate the selected operation. Unknown
or incomplete results retain the whole source selection and report that fact.
The pending Move's unique owner is consumed before callbacks; deletion and final
presentation share one completion path. Its deletion diagnostics use the publication outcome
described below, including errors after source replacement.

The same report protects interarchive staging before destination import. Original
logical directory prefixes are associated with physical identities; shared parent
reuse remains valid, while merged names, changed identities and repaired names
do not establish complete transfer. Existing private output is skipped without
an overwrite prompt. Directory links are checked before native placeholder
replacement, and data-encoded links refresh the recorded output identity.
On Windows, output identity includes the native canonical stream name because
an NTFS owner and its streams share a file identity. Unknown identities retain
the source. Observation failures cannot escape native final cleanup.

Confirmed relative links, hardlink groups and alternate streams feed the existing
native enumeration and update callbacks through operation-scoped transfer inputs.
The destination resource stays pinned until CopyFrom joins and reopens; its prior
transfer state is restored without changing compression or encryption settings.
On Unix, confirmed sibling stream files missed by selected-host enumeration are
enumerated with the native helper. A per-directory-item sidecar supplies both
native update pairing and kpidIsAltStream; ordinary colon filenames are not
inferred to be streams. Nested streams must already belong to the enumerated
selected directory. Missing inputs and unsupported target metadata prevent import.

Native archive-root symlinks are redirected into the extraction tree. The current
filesystem staging cannot round-trip those targets after its temporary directory
is removed, so interarchive transfer reports that limitation and retains both
archives. Ordinary filesystem extraction and relative links remain available.
This is a staging limitation, not missing native link decoding.

The asynchronous directory fallback receives the same operation purpose. Move
uses the native host-stream inclusion rule, matching source deletion; ordinary
Copy retains its literal stream selection. Known POSIX stream outputs are checked
as standalone files, including their actual leaf and ancestors; a one-character
owner is not interpreted as a Windows drive. Current-folder validation applies
the exact bound source prefix before checking the remaining output. Unbound
traversal and ordinary drive paths remain rejected.

Focused real-archive regressions cover overwrite refusal and cancellation,
successful solid extraction, source-reopen failure after publication, directory
and file streams, link preservation, merged staging paths, unsupported target
metadata, and independent decoding of destination bytes and stream properties.
The directory-stream overwrite reproduced false source retention on Windows
before stream identities were distinguished. The one-character WIM owner
reproduced the POSIX path rejection independently of fixture decoding.
Linux passed eight affected suites; macOS and Windows passed the six suites
covering their extraction, path, link, stream and UI adaptations. The final
fixture-path correction was checked again with the Move suite on all three.
Platform-only cases remain explicit skips. Linux's final combined build/test
times were 17.064/16.750 seconds; macOS 21.81/15.21; Windows 62.98/19.87 for the
combined batch, followed by the corrected Move-only acceptance. Its earlier
failure was fixture preparation, before transfer; the other five passing suites
were reused. A separate Windows transport stall before synchronization was
diagnosed only to its childless bootstrap process and recovered; it does not
establish a framing defect. No full/slow gate or rendered pixel parity is claimed.
EN/RU warning geometry was checked in normal and compact Qt layouts.
Diagnosed temporary fixtures and completed settings were removed; incremental
outputs, compiler caches, runtime deployments and small diagnostic logs remain.

## Interarchive Move publication outcome — 2026-11-02

Compared original ArchiveFolderOut CommonUpdateOperation, its Delete and update
callers, WorkDir MoveToOriginal and FileDir temporary-file/move bodies at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>. Complete original ArchiveFolderOut.cpp,
WorkDir.cpp and FileDir.cpp hashes match the existing baseline. Reviewed the
current atomic-publication adaptation, callback setup/join, panel deletion,
interarchive rollback, destination refresh and backend Move continuation.
No import or provenance advancement. Native update/decoding, cancellation,
reopening, selection and temporary ownership algorithms remain authoritative.

Previously a failed source DeleteItems always rolled the destination back.
However, source archive replacement precedes MoveArc_Finish and ReOpen, either
of which may fail. Cross-volume publication can also succeed before old-temp
cleanup fails. Restoring the previous destination after these failures can
remove the only surviving copy of moved entries.

The operation's joined callback now owns an optional publication result. Native
update reports an initial false value; uninstrumented providers leave it unknown.
The native publisher sets true immediately after successful atomic replacement
in both same-volume and staged cross-volume paths, before later cleanup. Its
optional output points directly into the callback's state, retaining the fact
through later errors or exceptions. COM interfaces and original HRESULTs are
unchanged. Panel deletion returns both facts after the worker has joined; early
refusal reports that publication did not occur.

Interarchive Move rolls back only when nonpublication is known. Otherwise it
retains the destination, refreshes both presentations and explicitly reports
the source error and retained copy. Unknown is handled conservatively; no
source rollback, atomic whole-Move guarantee or complete-success claim is added.
The typed backend Move continuation retains its original HRESULT handling.

Focused regression uses real native 7z/ZIP archives and independent extraction
of both resulting archives. It covers success, refused deletion, source writer
failure before publication and source reopen failure after publication. The
existing delegating format fixture injects only the terminal failure after a
real native open/write. The test waits for the sending view's focus and checks
the copy dialog destination. Five rows include the late warning in EN/RU and
normal/compact label/OK bounds. Widget dialog fixtures prevent native panels
and responder reentry; this is Qt behavior/geometry evidence, not native rendered
parity. No pre-fix product runtime reproduction is claimed.

A Linux WorkDir regression uses two real filesystems and denies source-directory
cleanup after cross-volume copying starts. It checks failure alongside published
bytes and a true publication result, then restores permissions/removes its temp.
Astra independently reviewed the state lifetime, unknown/default behavior,
exception paths and atomic marker placement without duplicating runtime runs.

Validation reused incremental builds, compiler caches and configured concurrency:

- Linux `20261102T081729Z-4a6c36fb`: eleven focused suites passed, 170 Qt checks
  without skips; build 17.985 s, tests 38.865 s. This includes the fast native
  recovery subset, existing archive/ADS Move, mutation and publication consumers.
  The final five-row EN/RU/widget-geometry regression passed separately as seven
  Qt checks in `20261102T082714Z-c5e6276d`; build 6.351 s, tests 6.353 s.
- macOS: work_dir passed six checks. Native publication passed nine checks with
  three unavailable `/dev/shm` cases skipped after preparing its missing official
  SFX module; repeat build 12.683 s, tests 0.719 s. Final Move passed seven checks;
  build 14.12 s, tests 7.45 s. Passing consumers were not repeated wholesale.
- Windows `20261102T083947Z-19865e57`: final Move and native publication passed
  sixteen checks, with the same three cross-filesystem cases skipped; build
  169.170 s, tests 9.048 s. The interactive receipt records session 1, normal
  priority and a non-elevated token. Earlier work_dir passed six checks. The
  missing official SFX module was prepared with the repository's existing builder.

The earlier Windows interactive attempt stopped in tool preparation before test
execution. Its exact childless transport process was terminated and the released
branch lock confirmed before retry. Bounded probes of the same framing and
pinned receiver preparation passed; no reproducible transport defect or runner
change is claimed. Temporary settings and absent-object registry records were
removed after completion. Prepared SFX deployments, useful incremental snapshots
and caches remain. No full/slow matrix, new JSON reports/screenshots or native
rendered parity acceptance was run. Cross-volume cleanup-failure runtime evidence
is Linux-only; unknown providers and late exceptions were source-reviewed.

## Current-item and batch rollback — 2026-11-02

Reviewed the complete staging/publication/source-removal flow, worker and copy
callbacks, and direct-path/window consumers. The source CopyFile_Ask, IsDestChild
and CopyFolder bodies were compared again at recorded revision
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>; FSFolderCopy.cpp's complete original hash
matches the existing baseline. Native copying, link handling, overwrite choices,
validation, cancellation and result publication are unchanged. No external code
is imported or baseline advanced.

One transfer record now holds source state, backup and merged-source inventory
from staging through completed-batch ownership. The current item's renamed flag
and separate backup/written-source locals are removed. Current-item failure and
reverse batch recovery use one rollback implementation. Its owned physical path
remains distinct from the intended destination, so a failed stage-to-destination
rename still restores the previous destination at its original name. A record
is copied into the completed batch before dismissing its current-item guard;
failed insertion leaves that guard's inputs intact without allocating a new
container from cleanup.

The existing distinction between full rollback and retaining published bytes
after source removal starts remains unchanged, including exceptions after an
actual deletion. Existing public worker/direct-name/UI tests cover rename and
copy fallback, overwrite/skip/autorenaming, directory merge, cancellation,
service exceptions and mixed partial Move recovery. No new representation-only
test or pre-fix failure is claimed. No dialog text/layout or OS branch changes.

Linux file_transfer, file_transfer_direct, ui_copy_same and ui_copy_routing
passed 168 checks without skips; run `20261101T220654Z-6ca9f12f`, build
15.035 seconds, tests 40.011 seconds. The runner's SSH channel reset after
verification completed, so its outer report records transport exit 255. The
matching passed verification receipt, guest test log, matching completion marker
and unlocked worktree confirm finished execution; tests were not restarted.
Only common transaction ownership changed, so no secondary OS batch was added.
Two disposable settings files were removed after checking processes; incremental
builds and caches remain. Inventory and whitespace checks passed.

## Mixed Move recovery state — 2026-11-01

Reviewed the complete transaction, worker, overwrite/copy callbacks and window
completion consumers against original `FSFolderCopy.cpp` CopyFile_Ask,
CopyFolder, copy/move I/O and callback/state dependencies at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` in
<https://github.com/ip7z/7zip.git>. The repository source archive's complete
original unit still matches SHA256
`9e3728bf2ad8c6f809019c7f2a7059b98ef85e7094c84cdda5fb5e6443e58c86`.
The post-change comparison retains the original native copy algorithms and all
existing Qt validation, overwrite, staging, cancellation and publication paths.
No external code is imported, no baseline revision/hash changes and no source
branch or dependency is omitted.

The local whole-batch transaction now records whether each source is intact,
renamed or has entered removal. One reverse recovery pass handles both full
rollback before removal and partial recovery afterward. Source removal has a
separate completion step; its state changes before a potentially destructive
service call, including one that throws. Renamed entries and entries whose
removal started retain their published bytes on partial failure; untouched
entries restore the previous destination. This preserves the earlier recovery
policy while removing overlapping copied-move/retain flags and a duplicate
recovery loop.

The public worker regression mixes rename and copy fallback in the same batch,
with occupied destinations and Unicode/spaced names. It checks bytes, retained
sources, restored destination contents and absence of staging residue after
removal failure or cancellation following actual deletion. Existing tests
covered these transfer modes separately, but not their combined partial recovery.
This is a behavior-preserving refactor; no pre-fix product failure is claimed.

The six focused suites passed on Linux, macOS and interactive Windows:
file_transfer, file_transfer_direct, language_file_transfer, ui_copy_same,
ui_copy_routing and worker_checkpoint. Both mixed-Move regression rows passed
on all three platforms. The first Linux build exhausted guest memory; the
repeat reused its outputs/cache with two build jobs. Initial macOS/Windows
UI runs lacked the new branch's codec runtime; after preparation only the two
affected UI suites were repeated. No full/slow gate or visual acceptance is
claimed. Completed temporary fixtures were removed; incremental build trees,
native runtime and compact verification evidence are retained.

## Staging and completion ownership — 2026-10-31

Reviewed the complete Qt transaction, worker entry/cancel/wait boundary, copy
callback and window completion consumers against the recorded source
FSFolderCopy CopyFile_Ask/CopyFolder, state, callback and native I/O branches.
Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; the complete original
`CPP/7zip/UI/FileManager/FSFolderCopy.cpp` matches its existing baseline hash.
The whole-batch transaction remains the local integration described below;
source copy, matching, progress, validation and link services are unchanged.
No source is imported and no baseline changes.

Per-item rollback now owns the current staged or published path directly.
Backup paths become owned only after the previous destination was successfully
renamed. This removes parallel stage-started, published and backup-owned flags
while retaining restoration on cancellation, failure and exceptions. Ownership
still transfers to the completed batch only after its record is accepted.
Successful Copy/Move share backup disposal and result publication. Partial Move
recovery stays separate: a destination is retained once source removal may have
consumed bytes, including a throwing removal service. The per-item loop remains
together to keep publication and rollback ordering explicit.

Existing public worker tests cover successful copy/move, direct names, overwrite
answers across mixed batches, canceled/failed staging, throwing rename/removal
services, cross-volume source-removal failure, merged directories and retained
bytes. UI consumers cover same-name and two-panel copies. No new test that
merely asserts the changed representation is needed, and no pre-fix product
failure is claimed.

The six focused suites passed on Linux (183 checks, 44.41 seconds), macOS
(183 checks, 73.22 seconds) and interactive Windows (182 checks, 86.89 seconds),
without skips: file_transfer, file_transfer_direct, language_file_transfer,
ui_copy_same, ui_copy_routing and worker_checkpoint. The POSIX link case is
compiled only on Linux/macOS. Existing incremental trees and compiler caches
were reused; disposable fixtures were removed after preserving the reports.
No full/slow gate, appearance change or visual-parity acceptance is claimed.

## Filesystem completion publication — 2026-10-31

Compared the complete original `CApp::OnCopy` body in the recorded 7-Zip source
archive with its current command/dialog, execution, worker-result, retained
continuation and panel-reload consumers. The baseline is
<https://github.com/ip7z/7zip.git>, commit
`b88c6bc09339de32f7d18bbab900686b244f42ba`,
`CPP/7zip/UI/FileManager/App.cpp`. Original post-copy behavior retains the source
and destination selection states, refreshes the applicable panels even after
operation failure, then restores selection and source focus.

The filesystem worker result no longer supplies a second panel-publication
policy alongside the retained continuation. It joins/releases the worker,
presents the terminal progress result and releases operation state before
notifying that owner. The existing owner handles both panel reloads, asynchronous
publication, saved selection and final focus. A redundant refresh-source argument
and destination-path/active-panel checks disappear. Transaction staging,
rollback, overwrite answers and cancellation remain unchanged. Archive routes
retain their existing notifications for panels already reopened or refreshed.
No external code is imported or omitted and no original baseline hashes change.

Existing behavior coverage exercises two-panel copy/move, output outside the peer
panel, single-panel and new-name copies, cancellation, overwritten bytes, saved
selection/focus and peer-load failures. No implementation-presence test is added.

The seven focused suites passed on Linux (35.88 seconds), macOS (71.75 seconds)
and Windows (107.76 seconds), without skips: `ui_copy_routing`, `ui_copy_same`,
`panel_copy_reload`, `copy_publication_order`, `file_transfer`,
`file_transfer_direct` and `worker_checkpoint`. Builds reused existing trees
and compiler caches. No full/slow gate or layout change/visual acceptance is
claimed. Compact verification reports and test logs are retained separately
from the subsequent build-storage migration.

## Filesystem overwrite metadata snapshots — 2026-11-01

Reviewed the complete transaction, worker wait/cancel protocol, copy callback,
window routing and overwrite consumers. Compared original FSFolderCopy
CopyFile_Ask/CopyFolder and ExtractCallback AskWrite/AskOverwrite at
9128b80e3a471108678e2b3ec3c985861c8d7b0f. The complete FSFolderCopy unit matches
the recorded SHA256 9e3728bf2ad8c6f809019c7f2a7059b98ef85e7094c84cdda5fb5e6443e58c86;
the previously verified ExtractCallback baseline is unchanged. Native copy,
recursion, matching, staging, rollback and source-removal algorithms are unchanged.

The filesystem copy bridge no longer drops native source time and size. The
worker publishes an owned overwrite snapshot, including the staging entry's
actual metadata when a merged child is compared under its visible final name.
The ordinary path uses the existing native FileFind service; supplied callback
metadata is retained directly. Remembered batch answers return before snapshot
construction, avoiding extra metadata reads for Skip/Overwrite/Rename-all.

Archive and filesystem prompts share one local contract and renderer. The
contract and native snapshot adapters have their own common target under fileops;
widgets own the conversion to the existing dialog model. Filesystem icon/folder
classification and omission of directory sizes preserve their previous display
policy. The GUI no longer recomputes file time/size or copies partially populated
native dialog-info objects. This is local integration, with no external code
import or original-baseline revision/hash change.

Three public worker rows cover an ordinary conflict, a merged child and a link
object. They change source and destination bytes before queued delivery, verify
the original native snapshot and visible paths, then cancel and check that those
later file changes survive without staging leftovers. Existing mixed-batch,
direct-path, remembered-answer, cancellation, link and rollback checks remain.

Fourteen focused suites passed on Linux (349 Qt checks, 76.27 seconds), macOS
(349 checks, 66.56 seconds) and interactive Windows (347 checks, 139.31 seconds).
Windows skipped the existing colon-filename case; the POSIX-only link case is
not compiled there. The three new snapshot rows passed on every platform.
The suites cover transfer workers, window routing/lifecycle, archive backend
and extraction consumers, cancellation, overwrite layout and prompt parity.
Current compact/normal widget reports have no clipped visible controls on all
three platforms. Linux/macOS reports use offscreen Qt; Windows reports cover
both native and offscreen Qt.
After the desktop was unlocked, the additional Cocoa overwrite run passed on
`70c9a260`: four Qt checks without skips (1.11 seconds). Its four current native
reports cover normal/compact sizes with optional and supplied metadata, without
clipped visible controls.
No full/slow gate or rendered-original visual comparison is claimed. Existing
incremental builds and compiler caches were reused, reports retained and
completed disposable fixtures removed.

## Overwrite decisions across a mixed batch — 2026-10-31

Reviewed the complete original FileManager `ExtractCallback.cpp::AskWrite` and
`AskOverwrite`, their callback types and filesystem-copy callers at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` in
<https://github.com/ip7z/7zip.git>. The complete original ExtractCallback unit
matches its recorded SHA256 baseline. The Qt transaction, copy callback, worker
wait/cancel protocol and window prompt/shutdown consumers were compared together.

The filesystem transaction now resolves remembered overwrite answers through
one path for top-level files and conflicts inside merged folders. Ordinary
Yes/No remain per-file; Yes-to-all, No-to-all and automatic rename remain scoped
to the batch. Directory merging, source copy recursion, staged publication,
rollback and source removal retain their existing owners. The native archive
callback and its source algorithms are unchanged. No external code is imported,
no baseline changes and no source branches or dependencies are omitted.

The public worker regression copies and moves a mixed file/folder selection in
both orders. It checks visible conflict paths, prompt count, destination bytes,
renamed output and retained/removed source bytes for all six answers. Cancel is
delivered at the second conflict after an earlier item has published, checking
rollback across the folder boundary. Existing cases covered individual
top-level and nested conflicts, but did not cross that boundary with a retained
answer. These are behavior-preserving simplifications; no pre-fix product
failure is claimed.

Seven focused suites passed on Linux, macOS and native interactive Windows:
`file_transfer`, `file_transfer_direct`, `language_file_transfer`, `ui_operations`,
`ui_copy_routing`, `app_lifecycle_window` and `worker_checkpoint`. The first
macOS run exposed a test-only Unicode comparison: Qt creates decomposed file
names there, while native enumeration retains that representation. Comparing
canonically equivalent paths fixes the fixture. The final `file_transfer`
repeat passed on all three platforms; the other six suites were unchanged and
already passing. Builds reused the existing trees and compiler caches. No full/slow gate,
appearance change or rendered visual-parity acceptance is claimed.

## Exception ownership — 2026-10-18

Source reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`,
`CPP/7zip/UI/FileManager/FSFolderCopy.cpp`, CopyFile_Ask/CopyFolder and their
state/callback types. The complete QtFileTransferTransaction and
FileTransferOperation bodies/headers, callback bridge, native link/file
services and window result/join consumers were reviewed. The existing
[transaction boundary review](../archive/qt-port/upstream-port-audit/p2-11.md)
records why upstream's immediate per-entry move does not provide this
whole-batch staging and rollback service. Source copy recursion, native I/O,
overwrite and progress implementations remain authoritative and unchanged.

The Qt transaction registered a completed item after publication, but had no
cleanup owner covering exceptions before registration. Earlier published items
also had no exception rollback, and exceptions could escape the QThread entry
point. An exception during source deletion could bypass the existing partial
move recovery after some source bytes had already been removed.

The current item now retains a scope guard until its completed record is
accepted. A second guard retains batch rollback until the completion phase
takes responsibility. Both use the existing move/copy restoration rules;
backup ownership is recorded only after a successful backup rename. Source
deletion exceptions enter the existing partial-move path, preserving the
published copy after an attempted deletion. The worker boundary converts other
escaping exceptions to a failure result after cleanup. No source algorithm,
module boundary or dependency was replaced, and no source branch was omitted.
The existing per-item loop remains together to preserve rollback ordering;
this focused fix does not further split the reviewed transaction component.

Two new public worker regressions use its existing filesystem adapter and
private Unicode/spaced paths. A rename-service exception on the second source
must restore the first source and the previous destination bytes without stage
residue. An exception after actual source removal must report failure while
preserving the removed file's published bytes and the untouched second source.
Existing coverage did not exercise throwing services. The old source allowed
these exceptions to escape the worker; no pre-fix crashing run was performed.
Tests use ordinary C++ service exceptions, without allocation-failure injection,
Qt modification, standalone reproducers or real user files.

The affected targets rebuilt incrementally and passed file_transfer,
file_transfer_direct, language_file_transfer, ui_copy_same and worker_checkpoint
on macOS arm64 (5/5, 0.95 s), Linux x64 (5/5, 2.75 s) and Windows x64
(5/5, 10.07 s), Qt 6.12. Guest run: `20261018T134603Z-8d612cf9`.
Compiler caches and persistent build trees were retained. These checks cover
the transfer worker and its direct consumers; no full/slow gate or UI appearance
change/acceptance is claimed.
