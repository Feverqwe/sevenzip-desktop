# Deferred Rename ownership — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed `CPP/7zip/UI/FileManager/PanelOperations.cpp` label-edit handlers,
`PanelListNotify.cpp` notification dispatch, panel folder/selection ownership,
and their Qt table/icon editor, panel projection and backend completion callers.
Borrowed module baselines remain unchanged.

The source notification addresses its sending CPanel. Qt defers completion until
the editor has closed; its previous callback instead used whichever panel was
focused at delivery, carrying an index from the earlier folder. Switching panes
lost the intended rename. Navigating in flat mode could rename an unrelated file
at that index in the destination folder. Both failures were reproduced before
the fix for both source panes.

The Qt editor now retains the sending panel and folder, a persistent model index
and the original source path. Delivery checks that the item still belongs to that
folder and list, then runs the existing source handler under the sending panel's
projection. Modal progress and native backend completion retain that owner.
Navigation, list replacement and closed/busy operations retire the callback.
Selection-marker changes do not consume an outstanding text edit. Table and icon
editors share this boundary. Source name validation, folder operations, error
handling and refresh selection remain authoritative; no rename algorithm or
source branch was replaced or omitted.

The focused regression checks filesystem results and bytes after pane switching,
flat navigation, same-folder reload and alternative-selection changes, plus a
real native ZIP rename with a peer archive left intact. Existing rename,
selection, focus, navigation, reload, copy and application-lifecycle suites cover
the direct consumers. This changes event ownership, not dialog layout or text.

The application built and all 12 selected suites passed on macOS arm64, Linux
x64 and Windows x64 / Qt 6.12. Guest run `20261018T084859Z-625b8806` reused the
persistent worktrees and compiler caches; Windows used the elevated interactive
desktop. The missing official Linguist tools and their QtQml dependency were
added to the existing guest kits for the concurrently changed build requirements.
No full gate, opt-in slow rename matrix or visual comparison was run.

## Editor preparation retirement — 2026-11-02

Reviewed the complete editor begin/commit/cancel/deferred-apply path, projected
panel, native label-edit and selection/reload consumers. Compared original label
handlers/name validation and retained their existing algorithm/refresh contract.
No external source import or provenance advancement is introduced.

Failed editor preparation now retires its request through one scope guard,
covering invalidated items and Qt editor refusal. The guard checks controller
lifetime and request identity, so it cannot clear a replacement request; it is
dismissed once an editor is found. Successful editing retains the existing
Editing/Committed/Cancelled phases and deferred completion. Window publication
uses one reference to the already projected source panel through native rename,
rejection and refresh-selection retrieval. Projection, notification and modal
guards, embedded-NUL rejection, unchanged-flat-name handling and native error
ownership remain unchanged.

Existing ordinary rename lifecycle, deferred ownership, panel-state and archive
rename suites cover the public scenario. No private-phase or guard-presence test
is added. The separately registered native_rename_2/native_rename_301 matrices
remain slow and are excluded from this focused validation.

Linux passed all four ordinary suites: 74 checks without skips. Source inventory
and whitespace checks passed. Shared Qt lifecycle/ownership changed without
native OS branches, layout or text changes, so secondary platforms and appearance
checks were not selected. Configured four-job builds retained incremental outputs
and compiler caches; completed scratch settings were removed. No full/slow gate
or new rendered parity acceptance is claimed.

## Qt edit text before native conversion — 2026-11-02

Rechecked complete QtRenameController editor/commit/cancel/deferred-apply flow,
window rename/rejection publication and native panel label-edit handlers, plus
folder-operation dispatch and filesystem rename. Original OnBeginLabelEdit,
OnEndLabelEdit and name validation at
`b88c6bc09339de32f7d18bbab900686b244f42ba` were compared; the complete original
PanelOperations.cpp hash matches the existing baseline. Native rename, path
validation, collision handling and refresh algorithms are unchanged. No external
source import or provenance advancement.

The Qt boundary rejects an embedded NUL before converting its QString to native
UString text. The existing conversion uses a terminated wchar string and would
otherwise silently rename to the prefix before NUL. Original Win32 label text
cannot carry that full Qt value. Rejection uses the source invalid-argument
diagnostic and common edit rollback/selection path, retaining timer suppression
while the modal error is visible. Ordinary names still enter the original handler.
No global string-conversion or new filename-normalization policy is introduced.

The filesystem state fixture now checks NUL rejection on both panes in EN/RU:
unchanged files/bytes, retained selection, no truncated destination and a successful
retry after dismissal. The archive rejection fixture also checks unchanged native
ZIP bytes and entry name. Existing suites cover collision, dot-name refusal,
normal/table/icon editing, Escape, refused editors and window destruction.
No pre-fix run is claimed.

Linux primary `20261102T051521Z-90ef9042` passed panel_rename_state,
rename_lifecycle and archive_rename_window: 63 checks, no skips. Build 17.485 s;
tests 12.140 s. Only portable Qt input validation changed, with the existing
diagnostic/layout; secondary OS and new rendered parity were not validated.
Removed three completed settings fixtures; incremental outputs and compiler
caches retained. Inventory/whitespace checks passed. No full/slow gate or new
reports/screenshots.

## Atomic Linux no-replace rename — 2026-11-04

Reviewed Panel label-edit validation and rename worker, CFSFolder's loaded-index
and parent-prefix handling, Qt link adapters, native move/copy fallback and archive
publication. Complete original PanelOperations.cpp, FSFolder.cpp and FileDir.cpp/.h
hashes match the recorded reference above. Original CFSFolder dispatch, native
copy, progress, stat and unlink remain authoritative; no source is imported or
provenance advanced.

The Linux no-replace adapter previously checked destination existence before
calling ordinary rename. Another writer could create a file, directory or link
after that check and have its entry replaced. The adapter now requests atomic
RENAME_NOREPLACE. The original EXDEV copy/stat/unlink tail has one private owner,
shared by ordinary native moves and the new Linux entry point; Create_NEW still
protects a destination on another filesystem. No fallback weakens no-replace on
unsupported-kernel/filesystem errors. Existing archive publication and its distinct
registered staging/metadata policy are unchanged. Windows and macOS rename
branches retain their existing behavior.

The Linux-only regression uses the public IFolderOperations::Rename and native
linker wrappers for rename/renameat2. A scoped competitor creates the selected
destination immediately before the real kernel operation. The fixture covers
both APIs, asserts retained source/destination contents and link targets, and
adds no production hook or source-text assertion. Wrappers live only in this
standalone test executable. Missing-destination success covers files, directories
and dangling links; real cross-filesystem rows check successful file moves and
occupied destinations with Unicode/spaced paths. Existing public filesystem/link
and inline-rename regressions cover direct consumers.

Linux passed `native_fs_rename`, `native_fs_operations`, `filesystem_link` and
`rename_lifecycle`: 60 checks, no failures or skips. Competing file/directory/link
destinations and both real cross-filesystem rows passed. Incremental build took
86.973s and tests 1.282s, retaining configured four jobs and compiler caches.
Selected scratch was empty after removing the completed Rename settings file;
no active test processes remained. No secondary platform, full/slow gate or new
appearance validation was selected.

The new fixture also skips its cross-filesystem rows when private storage on
`/dev/shm` cannot be created. After that fixture-only adjustment,
`native_fs_rename` passed again: ten checks without skips (build 7.554s, tests
0.456s). Production code and the other passing suites were unchanged; selected
scratch remained empty.

## Complete inline-rename completion owner — 2026-11-04

The existing rename controller now owns the complete deferred completion:
validated source request, native label-edit call, rejected-edit restoration and
source selection publication. Removed the window-only completion method and
the two thin editor-entry wrappers. Table/icon setup and the source EditLabel
callback invoke the existing controller directly. No controller, request state
or public operation interface is added.

Compared original PanelOperations OnBeginLabelEdit/OnEndLabelEdit, name
validation and rename worker with PanelListNotify dispatch, current retained
request/projection and result consumers at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Both complete original unit hashes
match the existing baseline. Native algorithms and their current adaptations
remain unchanged; no external source is imported or provenance advanced.

Folder/model/index/path checks, notification/peer guards, deferred delivery,
editor cancellation and shared request lifetime are retained. The completion
uses the already validated panel/context instead of classifying it again through
MainWindow. The filesystem-parent/flat-mode gate stays after the unchanged-name
branch and before native execution. Embedded-NUL rejection, source-owned worker
diagnostics, rejected model text, flat-archive no-op publication and selection
restoration retain their existing behavior and ordering. Existing public
lifecycle, deferred, state and archive regressions cover these contracts; no
source-layout or private-owner test is added.

Linux rename_lifecycle, deferred_rename, panel_rename_state and
archive_rename_window passed: 74 Qt checks, no failures or skips. Incremental
compilation took 57.632s and tests 12.356s with four configured jobs and compiler
cache. Result/rejection bodies match the prior implementation after qualifying
the new owner/request; source inventory and whitespace checks passed. No native
platform branch, layout or text changed. Secondary platforms, full gates, slow
native_rename_2/native_rename_301 matrices and new appearance acceptance are not
claimed. Completed test settings and the unreferenced retired object were removed;
incremental build trees and compiler caches were preserved.
