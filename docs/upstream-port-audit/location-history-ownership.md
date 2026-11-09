# Location history and address presentation — 2026-10-31

## Canceling password entry for an archive favorite — 2026-11-07

The Qt favorite action uses the native archive binding and password result.
Canceling leaves the current folder, selection and history available. A
subsequent attempt can open the same favorite. A failed open still presents
the unavailable-location notice.

Linux workspace archive, places and history checks passed. A real encrypted
fixture verified cancel, retry with the password and extraction of the
original payload.

## Back from an unavailable favorite — 2026-11-07

Reviewed the original folder-history selection and binding flow. Back and
Forward are Qt workspace commands. The unavailable-favorite notice now lets
Back return to the last open location, including a directory in the retained
archive reader. The failed destination does not become a history entry, and
ordinary Back and Forward are available after returning.

Linux workspace places, history, session and recent places passed. The cases
cover returning before the first explicit visit, preserving an existing
Forward branch and restoring a previously open archive subfolder.

## Window commands when a location cannot be opened — 2026-11-07

Reviewed the original main-window command dispatch and root, bookmark and
settings handlers. The Qt unavailable-location notice permits opening the
drive list, opening a numbered bookmark and changing settings. List reloads
do not publish the retained folder behind the notice. A successful root or
bookmark transition restores the normal file view and command availability.

Linux workspace-place and settings checks passed, including applying settings
in an unavailable pane, opening a bookmark immediately afterward, leaving via
the drive list and retrying the original location. Workspace session, root
commands and shared numbered bookmarks also passed in the preceding batch.

## Numeric bookmarks for system places across languages — 2026-11-07

Reviewed the original PanelFolderChange.cpp bookmark save/open bodies,
MyLoadMenu.cpp bookmark captions and RootFolder.cpp name binding. The Qt
numbered bookmark store retains the native path value and records a system
place identity when available. System bookmarks resolve through the shared
native places model so the destination and menu caption follow the active
language. Path-only settings retain native binding and fallback behavior.

Linux checks covered saving in one window, opening in another after changing
language, reloading persisted settings and replacing the slot with a regular
directory. Existing bookmarks, root commands, workspace places and recent
places also passed.

## Diagnostics for a rejected address — 2026-11-07

Reviewed the recorded address Enter, native BindToPath and RefreshListCtrl
flow. The Qt navigation request publishes its path diagnostic after rollback
and list restoration. Cancellation and the native fallback to an existing
ancestor retain their results. The native binder and archive opening are
unchanged.

Linux address navigation, address lifetime, workspace history and filesystem
reload checks passed. Address coverage includes a failed and canceled read
of a typed archive subfolder, the editable draft and a subsequent transition.

## Recent-place failure after changing panels — 2026-11-07

Reviewed the original PanelFolderChange.cpp history selection and bind flow.
The Qt recent-place menu uses that binding boundary and can complete a folder
read in the background. Failure feedback now belongs to the initiating pane
and appears when that pane is active. The other pane keeps its own feedback.
There is no change to the native binding or saved history.

Linux recent-place and main-shell checks passed. The recent-place regression
switches panels during the background scan, forces a read failure, verifies
both status contexts and retries the same entry successfully.

## Back and Forward to an unavailable location — 2026-11-07

Reviewed the recorded PanelFolderChange.cpp binding, refresh and folder-history
callers. The native binder can fall back to an existing parent. The Qt
Back/Forward workspace already requires the recorded destination to match.
It now reports the unavailable destination after rolling back to the current
folder. Completing a background list rebuild preserves newer status feedback.
The native binder and original folder-history dialog retain their behavior.

Linux history checks covered Back and Forward to a deleted directory, including
background restoration of the current list, retained selection and successful
retry. Existing archive cancellation, workspace navigation, root commands,
filesystem and archive reload, and recent-place checks passed.

## Address editing while applying a language change — 2026-11-07

Compared the recorded OptionsDialog.cpp and App.cpp language-apply and
panel-refresh flow with the Qt settings and address owners. Qt updates the
bound namespace label without replacing an unsubmitted edit. Applying list
settings still reloads the bound folders and keeps the address editor intact.
Namespace operations use the provider path even when the editor contains a
draft. No upstream source import or revision change.

On Linux, both live-language cases passed, including saving through Options
with a backwards selection in an address. The options dialog, panel address
keys, filesystem reload and quick archive reload suites passed. The full
root-command suite timed out on its earlier Back-history expectation, so its
language cases were run directly with the same built QtTest executable.

## Address retry after a deferred read failure — 2026-11-07

Reviewed original Panel.cpp address Enter and PanelFolderChange.cpp binding
against the existing 7-Zip baseline and the Qt address, filesystem publication
and workspace rollback owners. The original requests list focus after a
successful synchronous bind. After a deferred failure rolls back the source
location, Qt restores the address draft, including edits made while the
folder was loading, and returns editing focus to the initiating pane if it
is still current. The native binding and
error result are unchanged. No source import or baseline revision changes.

Focused Linux checks passed for filesystem reload, address navigation,
address lifetime and workspace navigation. The filesystem worker failure
cases check the diagnostic, restored location, address and caret, and a retry
using keyboard focus in either pane.

## Display-address return to directory archive packages — 2026-11-05

Reviewed native address Enter/BindToPath, the current source/display conversion,
package opening, rendered archive paths and history/bookmark consumers together.
Complete original PanelFolderChange.cpp matches the existing baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Sparse image/bundle readers and their recorded
source scope remain unchanged; no import or baseline revision changes.

Display conversion now reuses the existing archive-file/package classification
for its physical container boundary. A sparsebundle is a directory, so the old
isFile-only check could not recognize its copied display address after the panel
left it. Existing literal-space handling, currently open nested-archive handling
and native-source-path bypass remain intact. No second package classifier,
path parser or reader is introduced.

The existing-directory probe in native binding retains a trailing separator.
The package adapter now removes that separator from a recognized sparsebundle
container before its existing archive-open branch consumes the remaining virtual
suffix. Ordinary directories keep their native binding behavior. This narrow
container adaptation also fixes native subfolder paths into a closed bundle.

The existing sparse-image browse/extract workflow now leaves each unencrypted
file/bundle container and re-enters both native and copied subfolder addresses
before extracting the actual Unicode payload. It reuses the same fixtures and
rows; encrypted Cancel/wrong-password/retry paths are unchanged. Existing
persisted-location tests cover literal spaces and nested archive addresses.

The direct APFS traversal test normalizes native path separators only when
matching enumerated paths against fixture expectations; symbolic-link targets
remain byte-for-byte comparisons. This preserves the reader's Windows path
contract while checking the same directory loading, cancellation and retry.

Focused runtime checks passed on Linux (sparse-image UI, native navigation,
persisted locations and direct APFS traversal) and Windows (sparse-image UI and
direct APFS traversal with the current prepared native runtime). Source review
is limited to the address/package adaptation above; no decoder transfer or
macOS acceptance is claimed.

## Native archive opening through a filesystem link — 2026-11-05

Compared original OpenItem/OpenAsArc_Index and BindToPath file/parent fallback
with current explicit/path/address/item callers, native archive binding,
resource-backed extraction and parent navigation. Complete PanelItemOpen.cpp and
PanelFolderChange.cpp originals match the existing baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. No source import, revision or native opening
algorithm changes.

Qt explicit opening no longer rejects an existing file solely because it is a
symbolic link. Path navigation reuses the existing file/package classification
instead of repeating its file/sparsebundle test with another link exclusion.
Native binding owns the actual archive probe and result, including the existing
archive-only refusal when BindToPath returns a containing filesystem folder.
The address path and native item opener already support this native path.
Drop classification retains its separately reviewed original Add behavior for
links. Reader/decoding and extraction destination validation retain their owners.

Public regressions cover explicit opening, openPath, typed address and Open
Inside with a real ZIP link, ordinary-file link and dangling link. Successful
cases extract the real Unicode payload and return to the link's parent;
all cases preserve the link object and underlying source bytes. Existing
refusal/cancellation, deferred focus and native navigation cases remain covered.

Focused validation passed on Linux: address_navigation (77 Qt checks) and
native_navigation_window (57). Windows address_navigation passed 93 checks,
including actual reparse links. No appearance changes or native pixel acceptance
are involved.

## Archive picker initiating panel — 2026-11-05

Reviewed native FM command dispatch and panel binding with the current Qt-only
archive-picker extension, capability/call scopes, focus projection, opening,
refusal/cancellation and history-selection consumers. Complete original FM.cpp
and PanelFolderChange.cpp hashes match the existing baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. No native command or opening algorithm,
dependency revision or imported scope changes.

The picker now retains its initiating panel before entering the modal dialog
and restores that projection before opening a selected path, as history selection
already does. It no longer takes the destination from whichever panel regained
focus during modal close. Cancellation performs no navigation. File filtering,
native opening, refusal diagnostics and focus restoration retain their existing
owners; no new controller, state object or dialog interface is introduced.

Existing picker regressions retain their one-panel rows and add both panel
initiators with late peer focus for opening, cancellation and invalid input.
They observe the source contents and the peer's path, address draft and selection.
Before the fix Linux failed four rows: successful selection opened in the peer,
and refused input overwrote the peer draft; cancellation passed. These behavior
checks do not depend on the captured variable or handler structure.

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
