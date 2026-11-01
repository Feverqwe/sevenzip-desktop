# Automatic refresh state ownership — 2026-10-31

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
