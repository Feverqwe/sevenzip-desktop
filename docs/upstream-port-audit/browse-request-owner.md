# File-picker request ownership — 2026-11-01

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
