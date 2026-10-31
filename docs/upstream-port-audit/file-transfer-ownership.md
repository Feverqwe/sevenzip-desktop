# File-transfer ownership

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
