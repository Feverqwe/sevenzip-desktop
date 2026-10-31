# Selection command boundary — 2026-10-31

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared FileManager PanelSelect
mask/type/all/invert bodies and their selection notification, selected-index,
path-conversion and operation consumers. Complete original PanelSelect.cpp,
PanelItems.cpp, Panel.h and App.cpp match their recorded SHA256 baselines.

The retained CPanel algorithms own selection; current operations consume its
source indices and native path services. Two unused MainWindow filesystem and
archive-item path projections have no callers and are removed together with
their declarations and includes. The context menu also stops discarding the
result of a selected-index query: that const query does not synchronize or
modify selection. Its selection changes and command refresh remain in place.

The live archive selection expansion used by extraction remains intact, as do
mask matching, type selection, focus/parent handling and alternative marks.
Deferred selection snapshots now belong directly to their panel state, without
being copied into and out of the active-window projection. Completion takes an
owning local snapshot before entering the original restoration code, while
cancellation and failed publication clear only the corresponding panel request.
No upstream algorithm or functional consumer is replaced,
no external code is imported and no baseline changes.

Existing selection, two-panel, Copy/Move and extraction regressions cover the
active command boundary. A public two-panel regression refreshes both large
folders with distinct marks and unselected focused rows, switches to the peer,
and verifies each panel's selection, cursor and final focus in standard and
alternative modes. It checks restoration ownership without requiring a
particular scheduling or projection implementation. No implementation-presence
test or pre-fix product failure is claimed. No visible control or layout changes
are intended.

Focused acceptance passed on Linux, macOS and the interactive Windows desktop:
panel selection, source selection, filesystem/archive selection, Copy routing,
UI operations, native selected extraction, external archive security and
post-Copy panel reload. The new two-panel case passes in both selection modes.
Its focus check follows the owning panel: scanning can intentionally transfer
focus from a disabled list to that panel's address field. Source transfer was
reviewed separately from these runtime results; no algorithm import or visual
parity claim is involved.
