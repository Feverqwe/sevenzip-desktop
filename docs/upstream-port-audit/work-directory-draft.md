# Working-directory settings draft — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete FoldersPage
bodies/header/resources, NWorkDir::CInfo fields/defaults/load/save, the options
Apply/Cancel caller and WorkDir temporary-file consumer. Complete original
FoldersPage.cpp/.h/.rc and FoldersPage2.rc hashes match the existing baseline.
No import or provenance advancement.

The page kept its loaded CInfo for its whole lifetime, while all current choices
already lived in Qt controls. Apply overwrote every field before saving, and
no consumer read the stored copy. Loading now uses a local record; Apply creates
and populates a local record directly from the controls. The original GetWorkDir
body moved to its sole Apply caller. The page header no longer depends on the
settings record. No replacement state or shared mutable owner was introduced.

Source mode selection, path conversion, modified/init guards, accepted-only
folder browsing and save timing are preserved. The hidden POSIX removable-drive
choice retains its loaded value. Registry/settings format and temporary-file
placement/creation algorithms are unchanged. No layout, labels or defaults change.

Existing regressions cover Browse cancellation, disabled/enabled path controls,
invalid stored configuration, Cancel, Apply followed by another edit and Cancel,
and creation/removal of a real temporary file in the selected Unicode directory.
No new test is needed for the removal of private state; source-shape assertions
would add no useful behavior coverage.

Validation: options_dialog, options_integration, options_window and
source_browse_dialog built and passed on Linux first, then macOS and Windows.
Linux/macOS reported 61 passes each, Windows 63, without failures or skips.
Incremental builds/compiler caches retained; completed selected scratch removed
after evidence capture. No full/slow gate or heavy diagnostics were needed.
