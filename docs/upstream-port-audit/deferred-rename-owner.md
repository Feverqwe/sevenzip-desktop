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
