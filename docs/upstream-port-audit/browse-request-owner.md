# File-picker request ownership — 2026-11-01

## Source-browser listing and complete path input — 2026-11-05

Compared original BrowseDialog initialization, enumeration/filtering, row
publication, parent/child navigation, folder creation, confirmation and fallback
callers with the complete current Qt browser/control boundary. Original
BrowseDialog.cpp/.h/.rc at `b88c6bc09339de32f7d18bbab900686b244f42ba` match the
existing baseline. No source import or dependency revision is introduced.

The completed native file-info vector now transfers into the browser through
its existing vector Swap instead of copying every entry again. Enumeration
still completes before publication; failed enumeration retains the previous
listing. Native filtering, ordering, selection and path resolution remain
unchanged, with the same native vector and metadata types.

Confirmation rejects embedded NUL in Qt path text before native conversion,
using the existing invalid-name diagnostic and retaining the draft for retry.
Previously a malformed path could be accepted as its shortened prefix. File
and folder wrappers retain their different Cancel contracts and publish the
accepted filter/path together as before. Native picker routing, long/device
path fallback, link handling, folder creation and layout are unchanged.

Four public regressions cover correction or cancellation after invalid input in
file and folder mode, retained rows/raw text and final path/filter publication.
Existing browser/picker/archive-browse checks cover navigation, filtered reload,
sorting, creation, errors and normal/compact geometry. No vector-representation,
copy-count or source-text test is added.

Linux focused checks passed `browse_dialog`, `source_archive_browse` and
`source_browse_dialog`: 46 checks, no skips. The first run exposed a test-fixture
mistake: folder mode begins in the selected folder's parent, so correction now
supplies the intended absolute path. Production behavior was unchanged by that
fixture correction. The final browser build took 5.830 seconds and its tests
0.437 seconds; passing picker and archive-browser evidence was reused.
Configured four-job incremental builds and compiler cache were retained.
No Windows/macOS, full/slow gate or new rendered-original acceptance is claimed.

## Source-browser presentation boundary — 2026-11-04

Compared the complete original BrowseDialog initialization, controls/resources,
navigation, filtering/sorting, create-folder, acceptance and fallback callers at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Original BrowseDialog.cpp/.h hashes
match the recorded baseline; no source is imported or revision advanced.

The private dialog declaration and Qt control construction/bindings now have
their own browse-module files in the existing panel target. Initialization
retains the native initial-directory resolution and parent fallback, with the
same settings and single-click setup order. Temporary control references stay
local to initialization; no persistent controller state or public API is added.
Construction, signal bindings and final focus/tab/size setup are separated from
the filesystem and command methods. The existing native/command/event bodies
and initial path-resolution block were compared unchanged after extraction.

The general selector keeps its link-following enumeration adapter, distinct
from the temporary-file inspector. Raw path/filter publication, accepted-only
results, Cancel, create-folder validation/errors, source long/device-path
fallback and native-picker behavior retain their existing contracts. Controls,
translations, geometry and native OS branches are unchanged. The private
declaration and presentation extraction are recorded in the existing baseline.

Linux focused checks passed `browse_dialog`, `source_browse_dialog`,
`source_archive_browse` and `language_standard_buttons`, including the existing
normal/compact source-browser geometry and its archive-picker consumer.
Compilation took 13.666 seconds, tests 1.656 seconds. The first build exposed
two missing explicit presentation includes, corrected before the passing run;
no test or production behavior workaround was needed. Incremental builds,
compiler cache and configured four-job parallelism were retained. Inventory
and whitespace checks passed. No Windows/macOS or rendered-original acceptance,
full/slow gate, new source-shape tests or extra visual reports are claimed.

## Filesystem path-correction owner — 2026-11-04

The complete native `CorrectFsPath` implementation and its trimming helper now
belong to the existing `fileops/native/FileName.cpp` owner in `sevenzip_common`.
The focused `correctfspath.h` interface serves both the custom browser and panel
create/rename/copy workflows. The browse dialog no longer provides a filesystem
service through its UI header. Existing filename services, consumer operation
bodies, dialog behavior and target relationships are unchanged; no target is added.

Compared original BrowseDialog.cpp/.h correction and creation callers,
PanelOperations/App consumers and the FileName.cpp/.h destination owner and
dependencies. All four complete BrowseDialog and FileName original units match
their recorded hashes at `9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>. The baseline maps transferred symbols to the
existing adapted owner and focused header without changing original hashes.
No external source is imported.

Both platform bodies are preserved. Windows retains separator normalization,
root/super/device-path handling, existing-component checks and trailing-dot/space
correction for new components; POSIX retains the original identity behavior.
Only namespace lookup is scoped inside the moved Windows function. Native
FileFind/StringConvert dependencies are explicit. Neither old nor new source
has a special Unicode file definition to transfer. The implementation now moves
from the widget target to the common target, so focused Windows validation is
required in addition to the default Linux integration checks. Existing creation,
source-browser, rename and copy-routing regressions are reused without new
source-layout tests.

Focused runtime validation passed `source_create`, `source_browse_dialog`,
`panel_rename_state` and `app_copy_routing` on Linux (109 checks without skips)
and Windows. Windows acceptance combines three passing suites with the final
interactive browser rerun (24 checks without skips); the first browser run in
SSH session 0 could not expose its windows. The Windows build also exposed a
missing explicit native type include in the shared RAM-info header, corrected
without changing its calculation. Incremental builds, compiler caches and the
configured four jobs were retained. Selected temporary settings were removed;
no macOS, full/slow or rendered-original visual acceptance is claimed.

## File-picker request publication — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete
CBrowseInfo::BrowseForFile and CCommonDialogInfo::CommonDlg_BrowseForFile bodies,
both native encoding branches, the custom-browser fallback and Add/editor
callers. Complete original BrowseDialog.cpp and CommonDialog.cpp hashes match
the existing baseline. No external source import or provenance advancement.

The Qt native-picker adapter now prepares its basename and result in a local
copy of the existing portable request record. Confirmation publishes the path
and selected filter together. Cancellation needs no manual restoration of a
temporarily modified caller path. Title/owner/mode, initial-directory splitting,
filter order and accepted-only output retain their existing contracts. The
source long/device-path browser and folder-picker behavior are unchanged.

The existing native-adapter cancellation regression now changes both filename
and filter before Cancel, verifies the original result remains and then opens
again to confirm a Unicode selection. Existing suites cover open/save flags,
nonexistent names, unchanged working directory, source-browser long paths,
archive extension/filter selection and external-editor integration. These are
observable picker/caller contracts, not source-shape assertions.

Widget automation is distinct from rendered OS-native panel acceptance. No
geometry, text, modality, archive algorithm or native Qt SDK change is made.

Validation: browse_dialog, source_browse_dialog, source_archive_browse,
compression_format and external_editors_ui built and passed first on Linux,
then macOS and Windows. Linux/macOS reported 73 passes each; Windows reported
75, without failures or skips. Linux acceptance combines four unchanged passing
suites with the final picker-suite rerun after correcting widget input focus
in its cancellation fixture. Incremental outputs/caches were retained, completed
scratch removed after capture, and no full or slow gate repeated.

## Source-browser navigation path — 2026-11-02

Reviewed source-browser initialization and parent fallback, reload, filter/sort
refresh, parent/child navigation, folder creation and accepted/cancelled output
against the original BrowseDialog.cpp/.h at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Complete original inventory hashes
match the existing manifest. No source is imported or baseline advanced.

The original reload contract warned callers not to pass the current directory
by reference because reload changes that directory. The Qt port now captures
the requested path by value at the reload boundary, removing three separate
caller-side copies. Enumeration still precedes publication, and failure retains
the previous listing. Source filtering, sorting, cursor/path-edit policy, folder
creation, native-picker fallback and accepted-only path/filter output retain
their existing bodies and behavior. No layout, text or geometry change is made.

Existing source-browser regressions cover long paths, filter/sort refresh,
child/parent navigation, Unicode folder creation, cancellation/failure and raw
save-name preservation. Picker and archive-browse consumers cover confirmation
and filter/extension publication. No private-signature or temporary-variable
test is added, and no pre-fix runtime failure is claimed.

Linux browse_dialog, source_browse_dialog and source_archive_browse passed:
42 checks without skips. Only portable request-string ownership changed, with
no changed OS branch; no new secondary-platform or rendered-native acceptance
is claimed. Configured four-job builds reused incremental outputs and compiler
caches. Selected scratch directories were empty afterward; whitespace and source
inventory checks passed. No full or slow gate ran.

## Failed parent and child navigation — 2026-11-07

The pinned source browser stages enumeration before replacing the listing.
The Qt parent and child handlers now leave the path draft alone when that
enumeration fails. Both report the failed destination, and a subsequent
navigation uses the original selection and path context. The successful native
enumeration and selection rules are unchanged.

Linux `source_browse_dialog` and `shell_menu_window` passed. The source-browser
test removes and restores the target while the dialog is open, then retries
with the same button or key.

The original browser requests a new folder name in a modal dialog. In the Qt
browser the inline draft stays with its current directory. A failed navigation
keeps the draft available, and a successful move to a different directory
closes it.

After creating a nested folder, the Qt browser selects its first visible
ancestor in the current list. This lets Enter open the new path while keeping
the entered archive name available for saving.

## Failed list refresh — 2026-11-07

The original filter, column and Ctrl+R handlers reread the current directory.
The Qt adapter retains that enumeration, but reports a failed reread and
restores the controls to match the retained listing. The path draft remains
available and a retry can apply the filter, order or refresh. Linux
`source_browse_dialog`, `source_archive_browse` and `shell_menu_window` passed
69 checks with no skips.
