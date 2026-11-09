# Automatic refresh state ownership — 2026-10-31

## Complete panel list lifecycle — 2026-11-04

Rechecked original PanelItems.cpp RefreshListCtrl, icon assignment, saved
selection/sort completion and OnReload/OnTimer with Panel.h, current provider,
filesystem/archive/namespace publication and refresh consumers. Complete original
units match their recorded hashes at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. No source is imported or provenance advanced.

The complete current load, lazy icon and refresh/selection/timer block now lives
in PanelListLoad.cpp within the same sevenzip_panel target. General item queries,
selection capture, open/edit commands and path projection stay in PanelItems.cpp.
The panel interface and its state remain unchanged; no controller, cache or
publication stage is added. Existing original extraction mappings are extended,
retaining the earlier column owner and all original hashes.

Moved and remaining bodies are unchanged. Provider-result gates, directory/icon
caches, deleted-archive flags, source sort/fallback selection, flat-path matching,
notification/timer guards, reload completion and portable-change handling retain
their ordering. The existing Qt split between source loading and row publication
remains; pending selection and loaded selection keep their distinct lifetimes.
Windows shell-icon queries and their Qt fallback use the same dependencies and
compilation options. Existing behavioral regressions are reused; no source-layout
or private-helper test is added. No layout or visible text changes are made.

Focused Linux validation passed panel_list_load, panel_refresh_completion,
panel_item_icons, filesystem_reload, panel_auto_refresh and
archive_reload_command: 312 Qt checks, no failures, two expected native Windows
icon skips. Existing cases exercise provider failures/cancellation, lazy icons,
namespace publication, saved selection, sorting, independent panels and
synchronous/deferred archive and filesystem reloads. Incremental builds and
compiler caches were retained; completed settings were removed and selected
scratch contained no other files. This validates the shared lifecycle on Linux;
no new Windows/macOS, full/slow or appearance acceptance is claimed.

## Active view viewport — 2026-11-02

Reviewed the complete refresh routing, polling, snapshot consumption, filesystem
publication/selection completion and active-view creation. Compared original
PanelItems OnTimer/OnReload at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba`; its complete original hash matches
the existing baseline. Native enumeration, change detection and selection
algorithms remain unchanged. No import or provenance advancement.

The pending viewport now identifies the active Qt item view and retains both
scroll axes. Previously capture and restoration always used the table, even
when an icon/list presentation was active. After reloading, native selection
restoration could then move the visible list without restoring its position.
The view's layout is completed before applying its saved bars. The table keeps
its existing top-file anchor and vertical fallback; horizontal position is
restored after scrollTo. Icons/lists retain their own horizontal and vertical
positions. The pending record is still consumed before callbacks, remains
directory-scoped and rejects a deleted or no-longer-current view.

No new grid anchoring or pixel/item conversion policy is introduced. The three
icon/list modes share one widget, but view-mode commands are disabled during a
scan; no separate mode-change race is claimed. Native scan joining, notification
guards, small-folder behavior, deferred selection and independent panel state
are preserved. No widget layout, labels or style changes.

Six new behavior rows change file contents in all three icon/list modes, using
small and larger private folders. They check retained selection and both scroll
axes after automatic refresh. Pre-fix Linux failed four rows: vertical position
changed for the larger icon modes and horizontal position changed in both List
cases. Existing table/peer, missing-folder, disabled-refresh, modal and async
completion regressions remain useful. No private-state/source assertion is
added.

Linux focused validation `20261102T080043Z-8edaa33c`: panel_auto_refresh,
filesystem_reload, filesystem_selection_runtime, modal_refresh_window and
panel_refresh_completion passed (152 Qt checks, no skips). Build 73.542 s,
tests 31.608 s, existing incremental outputs and compiler cache retained.
Removed five completed-run settings files. Astra independently reviewed the
changed source and completion order without duplicating runtime validation.
Windows/macOS were not run; native platform services and view geometry settings
are unchanged, and this is Linux runtime evidence only.

### Icon position after filesystem refresh

The Qt icon view keeps the first visible file at its previous vertical offset
when new files sort above it. If that file is gone, the view restores the saved
scroll position. The native refresh, sort and selection bodies are unchanged.
Linux checks cover insertion and content edits in both synchronous and deferred
lists.

## Filesystem publication ownership — 2026-11-01

Compared original FSFolder `LoadSubItems`/`LoadItems`, their folder types and
PanelItems `RefreshListCtrl` with the current loader, scanner, row publication,
selection completion, navigation and close consumers. The complete FSFolder
implementation/header and PanelItems inventory units in the repository source
archive match their existing baseline hashes at
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>. No code is imported or baseline advanced.

The Qt scanner now owns an unfinished publication as one object: the source
folder, its presentation directory and the next source index. Cancellation or
completion clears that owner instead of coordinating a folder pointer, path,
index and separate active flag. Signal delivery retains the current object on
the GUI stack; after a receiver navigates, restarts or destroys the scanner,
its identity determines whether that delivery may continue. Worker generations
still reject retired loads and queued batches. Completed source folders belong
to consumers that retain the documented borrowed `folderReady` interface.

The native filesystem walk, flat-mode policy, callback cancellation, worker
join, progress sampling, ordered source indices, bounded row delivery and panel
selection/error policy are unchanged. Existing scanner and panel regressions
cover restart from progress/batch delivery, destruction from signals, queued
completion after close, metadata retention and selection across disk/archive
transitions. No new representation test or pre-fix failure is claimed.

Linux acceptance passed `filesystem_scanner`, `filesystem_reload`,
`panel_runtime` and `panel_copy_reload`, together with the runner-command suite
after the requested default parallelism change. The original eight-job build
hit kernel-confirmed OOM before tests; the incremental four-job retry passed
without local overrides or clearing caches. No native OS branch changed, so
Windows/macOS were not selected. No appearance change or full/slow gate is
claimed.

## Filesystem row projection — 2026-11-03

Reviewed immediate and worker-backed loading, folder/batch publication,
cancellation, reload selection and viewport restoration. Compared original
FSFolder `LoadSubItems`, `LoadItems`, `GetRelPath` and panel loading/selection
boundaries at `9128b80e3a471108678e2b3ec3c985861c8d7b0f`; complete FSFolder.cpp,
FSFolder.h and PanelItems.cpp hashes match the recorded baseline. Native walks,
metadata, notifications and selection algorithms remain unchanged; no external
source is imported or provenance advanced.

The scanner publishes ordered source-index ranges for its retained folder.
The owning panel now constructs paths and rows for both immediate and background
loads, removing the scanner's intermediate QFileInfo list, path-conversion
dependencies and copied directory. Immediate loading keeps its displayed base
directory; background loading keeps the bound native folder's base directory.
Source indices, native properties/icons and portable change signatures retain
their existing mapping. Filesystem metadata is still taken from the loaded source,
not refreshed from QFileInfo during row presentation.

Each GUI delivery remains limited by row count and yields before the next range.
The old elapsed-time bound only covered preparation of the removed QFileInfo list;
it did not bound icon lookup or row insertion. No speed or latency improvement
is claimed. Worker generations, shared folder lifetime during reentrant signals,
destruction guards, cancellation and selection/viewport completion remain intact.
Scanner regressions consume ranges against the public retained-folder interface,
check fixture paths, ordering, bounds and restart/destruction behavior. Existing
panel checks cover actual path projection and retained metadata; no test pins the
batch size or a private state representation.

Linux acceptance passed `filesystem_scanner`, `filesystem_reload`,
`panel_runtime`, `panel_auto_refresh` and `panel_refresh_completion`: 167 Qt
checks without failures or skips. After preserving the existing icon/index-error
gate before direct native-vector access, the four affected window suites passed
again; unchanged scanner evidence was retained. The final window build took
5.676s and tests 40.609s. Completed test settings were removed, while incremental
trees and compiler caches remain. No secondary OS, full/slow suite or new visual
acceptance is claimed; native platform branches were not changed.

## Refresh routing and saved selection — 2026-11-01

Rechecked original PanelItems SaveSelectedState, RefreshListCtrl, OnReload and
OnTimer with the current panel callbacks, namespace/archive rendering,
filesystem scan completion and archive reopening. The original complete
PanelItems.cpp at `9128b80e3a471108678e2b3ec3c985861c8d7b0f` still matches the
recorded SHA256 `7ac9d1a31d4f468cffcd3807c8ae36e8b16f040e7c81aa074ad2ae6930135802`.
No source is imported or baseline changed.

Manual refresh now captures the source panel selection once before choosing
the namespace, filesystem or archive route. The reload presentation callback
shares successful selection restoration across bound archive, namespace and
changed-location loads. Existing route precedence, timer address behavior,
missing-folder results, archive reopen/fallback, viewport restoration and
asynchronous selection delivery remain unchanged. Source and pending Qt
selection retain their distinct lifetimes; no new state owner is introduced.
Post-change source comparison found no omitted branch or disconnected caller.
Existing public refresh, navigation, copy reload and archive command suites
already cover these contracts; no implementation-mirroring test is added.

The seven focused suites passed on Linux, macOS and interactive Windows:
filesystem_reload, panel_auto_refresh, navigation_reload, archive_reload_command,
panel_copy_reload, modal_refresh_window and panel_parent_reload. These checks
cover saved selection, independent peer state, missing folders, navigation during
publication and bound archive reload. Existing build trees and compiler caches
were reused. Disposable completed settings/fixtures were cleaned; compact
reports and incremental outputs are retained. No full/slow gate or appearance
change/visual acceptance is claimed.

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared PanelItems
`OnTimer`, `OnReload`, saved-selection restoration, Panel timer/notification
guards, and FSFolder initialization, loading and change notifications with
their callers and the Qt polling/list-publication boundary. Complete original
PanelItems.cpp, Panel.h and FSFolder.cpp/.h match the existing baseline hashes.

The native Windows watcher, portable signature fallback and original panel
timer guards remain intact. The file signature collection and pending viewport
now stay in the owning panel state. Switching the active projection no longer
copies either collection; immediate loading, worker batches, missing-folder
handling, navigation and archive/namespace transitions update that same owner.
Viewport consumption remains directory-bound and precedes widget callbacks.
No upstream algorithm is replaced, no source is imported and the baseline is
unchanged.

A public two-panel regression changes file contents without changing the entry
set, then adds entries to both folders. It checks each panel's updated sizes,
selection, focused row and independent scroll position, with keyboard focus
remaining in the peer panel. The fixtures exercise larger listings without
requiring a particular asynchronous threshold or notification implementation.
Existing tests cover refresh cancellation by navigation, missing folders and
recovery, source modal guards, alternative selection and list completion.
No pre-fix product failure or visible layout change is claimed.

Focused validation passed on Linux, macOS and the interactive Windows desktop:
filesystem reload, automatic refresh, namespace navigation reload, large-list
publication, modal refresh, selection completion and source selection. On Linux,
the initial new fixture used a single-panel row helper and failed during setup;
the corrected automatic-refresh suite passed separately, alongside the six
unchanged passing suites. macOS and Windows passed all seven together. Source
comparison and runtime validation are separate evidence; no pixel parity is
claimed.
