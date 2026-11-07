# Open archive picker — 2026-10-17

Source reference: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

Reviewed `CPP/7zip/UI/FileManager/resource.rc` (`IDM_MENU`),
`MyLoadMenu.cpp` (file-menu projection and command dispatch),
`BrowseDialog.cpp` (`CBrowseInfo::BrowseForFile`), and the port's
`CApp::ExecuteCommand` / `WndProc`, command availability and
`MainWindow::openArchiveAs` consumers. Original File/Open acts on the focused
item; no original standalone archive-selection command exists. The older local
picker was removed for strict menu parity. The current user request explicitly
restores that convenience as an extension.

No upstream algorithm or module is copied, replaced or newly omitted. A local
extension command uses Qt's existing-file OS picker, existing codec-derived
`ArchiveBackend::openDialogFilter`, and existing `openArchive` integration with
source `CPanel::BindToPath`. Qt owns the new menu row, platform Open shortcut,
localized caption and file selection. Native source item-opening and menu rows
retain their owners. Dependencies are existing Qt Widgets, language services,
codec catalog and archive opening. The original general-purpose browser remains
unchanged; this command requires an existing file.

Source completeness is limited to this extension and its connected consumers;
it does not claim a new whole-application source audit or Windows pixel parity.

Runtime validation: 2026-10-17, macOS arm64 / Qt 6.12.0, working tree based on
`7405c9a384fa7f13e13509d43d66fb815e58c047` with this change:

- Incremental dev build: `Sevenzip`, `shell_menu_window_tests`,
  `browse_dialog_tests`, `codec_catalog_tests`, `command_policy_runtime_tests`.
- Focused CTest: `shell_menu_window`, `browse_dialog`, `codec_catalog`,
  `command_policy_runtime` passed. New picker cases cover opening a real ZIP
  with Unicode/spaces, cancel, and rejecting an ordinary text file. An initial
  test timeout was corrected in the test's focus/filter handling and list
  expectation; no archive-opening implementation change was needed.
- Current EN/RU File-menu JSON at 980×620 and 640×400 shows the new row enabled,
  after the source shell prefix and before item commands, without clipped rows.
- Existing incremental release app target `Sevenzip` built successfully.
- `python3 scripts/package.py --install-only dist` prepared the updated local
  `dist/Sevenzip.app` successfully.

Qt widget-dialog checks use offscreen QPA; native OS picker appearance and
physical shortcuts, Windows and Linux runtime remain unverified. No full or
slow suite is claimed.

## Long-path acceptance — 2026-11-07

The pinned source browser's `FinishOnOK` resolves a path for its general-purpose
callers. The project-owned Open Archive command additionally requires an
existing archive candidate in its long-path fallback, as its system file picker
does for ordinary paths. Native file lookup follows links and retains support
for long paths and sparse-bundle packages. Refusal keeps the typed path available
for correction. Folder and save callers retain their source acceptance.

On Linux, `shell_menu_window`, `source_browse_dialog` and
`source_archive_browse` passed 65 checks with no skips. The picker rejected a
missing file and an ordinary directory in the same dialog before opening a
real ZIP with a Unicode name.
