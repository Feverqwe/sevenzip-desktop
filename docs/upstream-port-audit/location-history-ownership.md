# Location history and address presentation — 2026-10-31

## Shared display-address conversion — 2026-11-05

Reviewed complete native BindToPath/BindToPathAndRefresh bodies, address edit
dispatch, current folder-chain reuse and the Qt address/history/bookmark callers.
The complete original PanelFolderChange.cpp hash matches the existing baseline
at `b88c6bc09339de32f7d18bbab900686b244f42ba`. No native path parsing, archive
opening algorithm, dependency revision or imported scope changes.

Manual address entry now shares the existing saved-location display adapter
instead of replacing every spaced separator. Conversion recognizes the physical
archive or the currently displayed open archive, including a nested archive,
and leaves literal spaces in path components intact. Native source addresses
from the dropdown still bypass this presentation conversion. The original
binding, error policy, history persistence and deferred focus owners are unchanged.

The persisted-location regression now also enters both source and display paths
to the same literal-space archive folder after leaving it, checks the resulting
provider prefix and readable row, and verifies unchanged archive/fixture bytes.
The existing nested-archive case enters the displayed location with native path
separators while retaining its open chain. These checks cover the old global
replacement failure and prevent losing nested-archive context during conversion;
no private helper or source-text assertion is added.

## Complete persisted-navigation Qt owner — 2026-11-04

Reviewed the original PanelFolderChange bookmark/history handlers, AppState
stores and App history normalization at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>, their current bodies, menu consumers and Qt
filesystem/archive/root navigation callers. Complete original PanelFolderChange,
App and AppState hashes match the existing baseline.

The complete Qt visited-path, history-selection, bookmark save/open and menu
refresh methods now live with their legacy saved-path adapter in
`mainwindow_persistednavigation.cpp`, in the same window component. Their bodies
are unchanged; toolbar, sorting and view preferences retain their existing owner.
No controller, state, native algorithm or dependency revision is added.
Accepted-only history edits, cancellation versus empty root, originating-panel
selection, sparse bookmark slots and literal path components retain their
existing semantics. Existing behavior tests cover these contracts; no source
shape or private representation test is added.

The Windows network-namespace fixtures are platform-specific and unchanged.
Linux checks the shared Qt integration; it does not establish new Windows
namespace or rendered visual acceptance.

Linux `folder_history`, `bookmarks` and `persisted_location` passed 32 checks
without failures or skips, including accepted/canceled edits, root entries,
literal legacy paths, nested archives and originating-panel navigation.

## Shared visited-path recording

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
