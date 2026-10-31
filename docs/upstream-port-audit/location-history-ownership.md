# Location history and address presentation — 2026-10-31

Source reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete FileManager
`PanelFolderChange.cpp::LoadFullPathAndShow`, history/bookmark handlers and
`App.cpp::CFolderHistory::AddString`, with their headers, callers and Qt
navigation/persistence consumers. Complete original PanelFolderChange, Panel,
App and AppState units match the recorded SHA256 baseline.

The existing navigation owner records and saves the visited source location for
filesystem, archive and namespace views. Address presentation used a second
history-write path for filesystem folders, duplicating the entry already
recorded before presentation. That write and its boolean policy argument are
removed. Address text, icons, native path inspection and title refresh retain
their source implementation; MRU insertion, uniqueness, bounds and persistence
retain their existing owners. No new external code is imported and no baseline
changes. The original history insertion remains in the navigation path rather
than being repeated when presenting an address.

Bookmark slot persistence, legacy display-path conversion, nested archive
locations and originating-panel selection are unchanged. History cancellation
still preserves stored entries; an accepted empty root location remains
distinct from cancellation. Existing behavior coverage exercises these routes,
history editing and persistence, address hierarchy selection, stale locations
and navigation through ordinary and nested archives. No implementation-presence
test is added. This is a behavior-preserving ownership simplification, without
a claimed pre-fix product failure or visual change.

Seven focused suites passed on Linux, macOS and native interactive Windows:
`folder_history`, `bookmarks`, `persisted_location`, `address_combo`,
`address_navigation`, `nested_archive` and `ui_navigation`. The first Windows
attempt stalled during tool preparation before source sync or tests. After
confirming ownership and absence of child work, its abandoned preparation was
stopped; an ordinary retry passed without code changes. No transport change is
claimed. Existing incremental trees and compiler caches were reused. No
full/slow gate or rendered visual-parity acceptance was run.
