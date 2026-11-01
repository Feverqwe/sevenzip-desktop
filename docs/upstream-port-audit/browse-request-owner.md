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
