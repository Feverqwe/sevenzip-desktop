# Selection command boundary — 2026-10-31

## Copy/Move workflow implementation owner — 2026-11-04

The complete application Copy/Move workflow now lives in `AppCopy.cpp`: native
destination reduction/routing, request snapshots, post-operation refresh and
completion, and the archive-to-archive staging bridge. Its private request
definition moves with those methods. `CApp` retains its existing interface and
fields; the implementation stays in `sevenzip_panel`. Application composition,
navigation and settings remain in App.cpp. No new state, class or target is added.

Rechecked original complete OnCopy, destination reduction and source panel copy
branches/types against current dialog/dispatch, Qt execution/publication,
archive staging and completion consumers. Complete App.cpp/.h and PanelCopy.cpp/
Panel.h original hashes match the existing
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` baseline from
<https://github.com/ip7z/7zip.git>. The extraction mapping records the moved
source phases; no original bytes/hashes or external code are changed.

All moved bodies and the private request definition are preserved. Paths,
history, source/destination classification, Windows path/ADS guards, selection,
timer/notification guards, staging/metadata refusal and publication order are
unchanged. The source selection request remains distinct from Qt's publication
continuation. App.cpp can reset its existing shared request through its forward
declaration; its two panel constructors still include their complete external
session owner. Compilation context and file-specific settings are unchanged.
Existing routing, same-copy, publication, reload and interarchive regressions
cover behavior; no representation-only test or appearance change is introduced.

Linux `app_copy_routing`, `ui_copy_routing`, `ui_copy_same`,
`copy_publication_order`, `panel_copy_reload`, `interarchive_staging` and
`interarchive_move_publication` passed: 196 checks without failures or skips.
The same target, native platform branches and interfaces make Linux sufficient
for this ownership move; no new Windows/macOS or appearance acceptance is claimed.
Configured four-job incremental builds/compiler caches were preserved. Before
the batch, an abandoned owned staging runtime was removed only after confirming
its process was gone, directory identity matched and no process referenced it;
completed test settings were removed afterward. Source inventory/whitespace
checks passed. No full or slow gate was run.

## Copy/Combine summary owner — 2026-11-04

The complete selection summary and its private counting/formatting helpers now
live in `panels/PanelInfo.cpp`, separate from application routing in App.cpp.
The shared count/size formatter has a focused header beside its implementation;
Combine includes it directly instead of redeclaring the function. The existing
global symbol and `CPanel::GetItemsInfoString` interface, state and target
(`sevenzip_panel`) are retained. No new class, cache or operation state is added.

Compared original complete App.cpp summary helpers/body, panel declarations and
copy-dialog limit with current source copy, dialog, native provider and Combine
consumers. Complete App.cpp, Panel.h and CopyDialog.h original hashes match the
existing `9128b80e3a471108678e2b3ec3c985861c8d7b0f` baseline from
<https://github.com/ip7z/7zip.git>. The extraction mapping records transferred
symbols; original bytes/hashes and native algorithms are unchanged.

All moved bodies are preserved. Unknown-size propagation, zero-sized entries,
file/folder totals, native number formatting, relative path order, directory
suffixes and list truncation retain their existing contracts. Dialog, copy and
Combine command bodies are unchanged; no appearance, translation or behavior
change is intended. The same compilation context applies, without special
source-file definitions or separately compiled App.cpp consumers. Existing
public summary and workflow regressions are reused without new source-shape tests.

Linux `panel_copy_info`, `copy_dialog`, `app_copy_routing`, `volume_dialog` and
`volume_workflow` passed: 98 checks without failures or skips. After making the
existing string-adapter include explicit, only `panel_copy_info` was rebuilt
and repeated successfully; the other passing behavior evidence remains valid.
The portable bodies and unchanged target/platform services make Linux sufficient;
no additional Windows/macOS or appearance acceptance is claimed. Configured
four-job incremental outputs/compiler caches were retained, completed temporary
settings removed, and source inventory/whitespace checks passed.

## Source command boundary — 2026-10-31

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared FileManager PanelSelect
mask/type/all/invert bodies and their selection notification, selected-index,
path-conversion and operation consumers. Complete original PanelSelect.cpp,
PanelItems.cpp, Panel.h and App.cpp match their recorded SHA256 baselines.

The retained CPanel algorithms own selection; current operations consume its
source indices and native path services. Two unused MainWindow filesystem and
archive-item path projections have no callers and are removed together with
their declarations and includes. The context menu also stops discarding the
result of a selected-index query: that const query does not synchronize or
modify selection. Its selection changes and command refresh remain in place.

The live archive selection expansion used by extraction remains intact, as do
mask matching, type selection, focus/parent handling and alternative marks.
Deferred selection snapshots now belong directly to their panel state, without
being copied into and out of the active-window projection. Completion takes an
owning local snapshot before entering the original restoration code, while
cancellation and failed publication clear only the corresponding panel request.
No upstream algorithm or functional consumer is replaced,
no external code is imported and no baseline changes.

Existing selection, two-panel, Copy/Move and extraction regressions cover the
active command boundary. A public two-panel regression refreshes both large
folders with distinct marks and unselected focused rows, switches to the peer,
and verifies each panel's selection, cursor and final focus in standard and
alternative modes. It checks restoration ownership without requiring a
particular scheduling or projection implementation. No implementation-presence
test or pre-fix product failure is claimed. No visible control or layout changes
are intended.

Focused acceptance passed on Linux, macOS and the interactive Windows desktop:
panel selection, source selection, filesystem/archive selection, Copy routing,
UI operations, native selected extraction, external archive security and
post-Copy panel reload. The new two-panel case passes in both selection modes.
Its focus check follows the owning panel: scanning can intentionally transfer
focus from a disabled list to that panel's address field. Source transfer was
reviewed separately from these runtime results; no algorithm import or visual
parity claim is involved.
