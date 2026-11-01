# File-transfer ownership

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
