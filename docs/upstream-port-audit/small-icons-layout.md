# File-list view styles — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

Reviewed `CPP/7zip/UI/FileManager/Panel.cpp` (`kStyles`, list creation and
`CPanel::SetListViewMode`), `Panel.h`, `App.h::SetListViewMode`,
`MyLoadMenu.cpp` View dispatch, `PanelKey.cpp` Ctrl+1–4 and `resource.rc` View.
The source selects `LVS_SMALLICON` for Small Icons and retains the same list
control, model and selection. The corresponding Qt panel callback and shared
table/list model and selection were reviewed together with the view settings
adapter and row delegate.

Follow-up source review includes `PanelSelect.cpp::OnInsert`,
`PanelSort.cpp::SortItemsWithPropID`, `Panel.cpp::AttachPresentation` in the port,
and upstream `PanelListNotify.cpp` custom drawing and `App.cpp::SetListSettings`.
The source uses one ListView for selection, sorting, scrolling and repaint;
the Qt views share the model/selection but have independent viewports.

The Qt adapter now uses side-by-side icon/text presentation for Small Icons,
with horizontal flow and wrapping. Row height accommodates the active font.
The delegate bounds its size hint to the configured list grid so long names
elide inside their cells rather than overlap adjacent entries. These are narrow
Qt presentation adaptations of the native ListView style; upstream has no Qt
layout implementation to copy. No upstream bodies, command branches or
dependencies are added or omitted. Source view ownership, mode persistence,
selection and command dispatch are unchanged.

The follow-up restores wrapping under Large Icons and derives icon sizes from
the Qt platform style, with cell geometry following font metrics. Qt reserves
two label lines and supplies the existing full-path tooltip for longer names;
native Windows focused-label expansion is not reproduced. Font/style changes
recalculate geometry, and resizing reflows the visible entries. The extra
presentation binding directs EnsureVisible and repaint to the visible view,
while the table remains the authoritative shared model and sort adapter.
The row delegate reserves the native focus-frame margin inside icon cells:
Cocoa extends its text subelement to the left, which otherwise clips labels
in the first column. The existing FileIconList/delegate boundary was kept for
this focused correction rather than splitting the shared adapter module.

Screenshot review follow-up: Large Icons now overrides the shared table's
left-aligned filename role at the Qt delegate boundary, centering wrapped
captions beneath each icon. Small Icons and Details retain their existing
alignment. Upstream `kStyles` and `CPanel::SetListViewMode` still delegate this
presentation to native `LVS_ICON`; no source command logic changes.
The geometry producer explicitly opens its disposable fixture instead of
allowing saved panel paths to replace the intended long-filename scenario.
Native Cocoa light/dark capture runs pass the affected layout methods;
the full local `ui_layout` run also reports unrelated Extract-title language
and column-persistence failures in the concurrently edited working tree.
Windows/Linux and rendered Windows reference comparison remain unverified.

Single-click tracking now queries the Windows system mouse-hover timeout on
each new target. On other platforms, which have no upstream implementation,
the Qt style's tooltip wake-up delay provides the hover timing adaptation.
Cancellation, modifier guards and activation remain unchanged. The FullRow
review establishes its original role as a highlight style; the existing
name-only/full-row draw policy remains, with no independently confirmed
hit-testing defect warranting a change.

Validation: macOS arm64 / Qt 6.12.0 incremental application and affected test
targets built successfully. Ten focused suites passed: `ui_layout`,
`panel_selection`, `panel_key`, `list_activation`, `list_settings_runtime`,
`view_menu`, `panel_column_changes`, `list_view_dialog`, `panel_focus` and
`panel_selected_state`. Following the localized paint-margin correction,
the five affected layout/settings/key/menu/column suites passed again.

Native Cocoa passed all six Large/Small Icons geometry cases (normal/compact,
including an enlarged font changed while the view is active), explicit Insert
scrolling with the table hidden, and style-controlled hover selection without
activation. The geometry checks caught both adjacent-cell overlap and Cocoa
label-margin clipping before their corrections. Current `icon-layout-*` widget
reports were inspected under `build/dev/ui-reports/` and `file-list-native/`:
the active list fits its stack at both sizes. Paint assertions independently
check icon/text bounds, wrapping, height and adjacent cells. Existing selection
and persistence coverage remains; assertions pinning Qt mode/icon-size
configuration were replaced by geometry checks. Rendered Windows reference
comparison, Windows system-timeout execution and Windows/Linux runtime checks
remain unverified.

## Drag capability across view configuration — 2026-11-03

Rechecked original Panel mode/style selection, menu and Ctrl+1–4 dispatch with
the current Qt mode/stack, shared selection, per-panel focus and drag capability
consumers. Original Panel.cpp/.h, PanelKey.cpp, MyLoadMenu.cpp and resource.rc
hashes match the existing baseline. No source command, native drag/format
algorithm, cell geometry or platform adaptation was imported or changed.

The owning icon/list view now preserves its current drag capability while applying
mode configuration, including Qt Static movement. Window mode switching and
font/style refresh no longer restore that policy independently. The command-state
owner still enables/disables both views together; mode changes retain that state.
Shared model/selection, source focus/projection, persistence and layout metrics
keep their existing owners and behavior.

The regression performs actual mouse drag gestures through the view's existing
export boundary in all three icon/list modes, before and after live font/style
changes. Enabled drag must reach the export handler once per gesture; disabled
drag must remain unavailable. Existing window/menu/settings/archive-drop cases
cover integration. No private-state/getter or fixed-metric assertion is added,
and no pre-fix application failure is claimed: production callers previously
supplied the manual restoration now moved into the view.
