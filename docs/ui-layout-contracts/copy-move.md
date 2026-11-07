# Copy and Move

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

### Native Copy terminal errors (P3-05)

Reference App.cpp::OnCopy, PanelCopy.cpp::CopyTo/ProcessVirt and
Panel.cpp::MessageBox_Error_HRESULT/MessageBox_Error_Caption at the audit's pin.
The standard message has HRESULT plain text, Critical icon and OK, caption7-Zip
(Qt omits it on macOS). Preserve worker and final OnCopy diagnostic phases;
E_ABORT has neither. Writer error precedes source/peer publication; retained
Move source loads after guards release. Both panes/Copy/Move/result states use
copy_publication_order terminal reports at980×620/640×400. Error resize requests
440×220/340×220 auto-fit to400×116 Mac,399×98 Linux and214×88 Windows for both
requests; inspect actual visible text/OK bounds, not requested sizes. These
32 reports/OS have no meaningful clipping and do not prove rendered Windows
pixels or physical input. [Receipt](../archive/qt-port/upstream-port-audit.md#p3-05--native-copy-terminal-phases-and-retained-guards-2026-10-10).

### Source Copy/Move destination (P3-05.15, required scope)

Archive-to-filesystem Move retains the complete source selection when extraction
skips an item or cannot confirm complete output. Its terminal status explains
that retention. A late source-removal error asks to check the archive rather
than claiming that no deletion occurred.

Interarchive staging errors precede destination import and source deletion.
Unsupported target metadata and incompatible paths leave both archives intact;
private staging collisions do not open an overwrite question. EN/RU warnings
must keep their label and OK visible at normal/compact message sizes. Native
extraction diagnostics retain their existing progress-message presentation.

Interarchive F5/F6 preserves the destination provider and pre-update marked/focused
names after success. Refused source deletion or a known failure before source
publication restores backup bytes and physically reopens the destination with
that same saved state; source rows/bytes remain intact. A late failure after
source publication, or an unknown publication result, retains the destination
copy and reports the source error without claiming complete success. The warning
explains the retained copy and asks to check both archives, including when late
cancellation caused the incomplete result. Its EN/RU label and OK remain fitted
at normal/compact message sizes. Source marks clear and focus returns to its list.
Inspect both-pane roles, EN/RU, table/icon on both panels, standard/alternative
selection at980×620/640×400, including301-row asynchronous destination publication.
Both views and addresses must be visible/enabled after terminal completion.

If physical rollback reopen fails, the destination displays its return filesystem
folder instead of stale archive rows; source rows/bytes survive. Failed backup
restore adds an explicit rollback warning; reopen E_ABORT stays silent. Check
both failed and recovered states at the same sizes/modes, including301-row
filesystem fallback. Reopening a repaired private destination and F5 retry must
restore normal selection/focus/notify/redraw and enabled controls.

Filesystem-to-archive F5/F6 keeps the bound destination provider and its pre-update
marked/focused names after native update. Source marks clear and focus returns
to the source list; failure/Cancel still publish the destination through the
existing Copy completion. Inspect both panes EN/RU, source table/icon, destination
table and alternative selection at980×620/640×400, including301-row asynchronous ZIP publication.
Both views and addresses must remain visible and enabled at terminal state.

Manual Refresh/Ctrl+R uses the bound archive provider, retaining source selection
and focus without an engine/reopen request. Inspect EN/RU both panes, ZIP/7z,
table/icon and standard/alternative selection at980×620/640×400; peer provider,
rows and private fixture bytes stay unchanged. Operation transport reopen is a
separate scenario.

Source RefreshListCtrl also publishes the bound list before restoring saved
selection. Archive Drop reloads only after successful CopyFromNoAsk; confirmation
No/Cancel and update failure/cancellation retain the list and both providers.
The peer archive is not reopened or published. Inspect EN/RU both panes,
table/icon and standard/alternative selection at980×620/640×400; terminal views
must be enabled, preserve marks/focused names and restore redraw.
For actual native writers, a later attribute S_FALSE/E_FAIL/E_ABORT after success
leaves an empty enabled list without changing the writer result; Ctrl+R recovers
the same provider. Finish the source list-focus request after asynchronous
publication, preserving the peer. Native Write failure and actual Cancel retain
the original archive and permit a second real Drop. Inspect representative2/301
rows in the default bounded native_drop_publication suite; geometry does not
establish rendered Windows parity.

After attempted external-file update, publish the owning archive list for success,
failure and cancellation; retain source selection, peer focus/provider and edited
temporary bytes on failure. Read-only worker refusal and changed-folder/missing-
item exits precede Refresh. Inspect EN/RU both panes, standard/alternative and
table/icon at980×620/640×400; terminal views must be enabled with redraw restored.

Select All/Deselect All must keep callback names available during selection and
mark-role signals, with the original source mark vector already authoritative.
An outer notify guard still suppresses uncached text. Inspect both panes, EN/RU,
table/icon and standard/alternative selection at980×620/640×400; peer focus and
provider remain stable. Selection projection does not change control geometry.

Activation uses source OnNotifyActivateItems for Return/double-click in table/icon:
Alt alone shows Properties; Shift alone opens outside; other Ctrl/Shift/Alt
combinations permit internal opening. Preserve both-pane ownership, selection,
peer and notify guards. Inspect EN/RU FS/real ZIP at980×620/640×400 and Qt Properties
at540×360/360×240; Cancel retains provider and selection. Native Windows filesystem
property sheets require their own native acceptance. Removed-directory activation
must show the source error and allow recovery without rebinding the owning panel.

Each source catalog entry gets a separate Qt section, including generic/raw
entries with the same PropID and raw entries for reserved Size properties.
Requested cells/alignment use that exact source descriptor, not the first ID.
Visible logical order and widths persist through the source save flow; duplicate
saved IDs retain upstream first-match initialization policy. Inspect both panes
EN/RU normal980×620/compact640×400, owning provider/selection/peer, header Cancel,
notification guards and Refresh. Duplicate-ID menu entries follow the native
catalog checks: hiding removes the first visible PropID, showing appends a new
instance even if that descriptor is already visible. Each visible instance has
its own logical index, section, requested cells and width. Inspect normal/compact
geometry before Refresh as well as after it, with selected rows and peer focus.
Sorting those sections uses the first catalog PropID's raw flag. Clicking any
section of the currently sorted PropID toggles the source direction, including
switching between generic/raw sections or appended copies. Keep the indicator
on the clicked section and map sort actions by PropID. Physical clicks focus the
owning list; explicit programmatic sorts retain their requested direction and
existing inactive-owner focus behavior. Inspect both panes EN/RU, empty raw data,
selection/peer state and copies/Refresh at980×620/640×400.
Re-enumerate a schema even for the same provider/type. Consume written generic/raw
counts as upstream does, skip individual info errors and generic IsDir, and hide
the reserved Qt Name section when no generic Name descriptor exists. A raw Name
keeps its own source section and Name-first logical order. Empty schemas show no
invented columns while preserving item/selection/status state; Qt collapses the
empty header to zero height. Counter outputs start at zero if a failed provider
leaves them unwritten; native behavior for unwritten outputs is unverified.
Inspect count-status, raw-Name/no-Name/empty/unwritten/recovery transitions with
header Cancel and both panes EN/RU at980×620/640×400.
Label-only highlight belongs to source visible logical subitem zero, including
raw Name or the first non-Name descriptor when reserved Qt Name is hidden.
Large/small icon and list modes show that same source subitem's exact callback
text and existing row icon, including raw Name hide/re-add, no Name, empty schema
and recovery. Appending Name does not change the existing first logical subitem.
Inspect only the affected icon/list view for appearance captures, with both panes
EN/RU at980×620/640×400; preserve selection and the focused peer across mutations.
With no source-visible descriptors, Qt retains physical-zero metadata captions
and row icons. Native rendering of this empty-catalog fallback is unverified.
Published cell text uses its source item index without another count call.
A later count error must not blank a valid name/generic/raw property; property
errors retain source generic error text or empty raw text. Preserve notify/cache
and provider guards, selection/peer focus, and invalidate old model indices on
rebind. Inspect both panes EN/RU detail/icon recovery and rebind at980×620/640×400.

Walk filesystem → real ZIP/7z → same-provider namespace schema changes →
filesystem → archive reopen. Fresh filesystem columns use source hidden-property
defaults; archives expose their reported properties. Name starts160px and other
columns100px. Persist filesystem Name width/order and shown ATime separately from
archive Name width. Unchanged initialization retains the visible descriptor
snapshot; changing only Type or only Name must rebuild it and update cell alignment
or the header caption, preserving saved widths and selection/peer focus.
Inspect all seven phases under EN/RU in both panes at980×620/640×400.

Language changes retain provider-defined column names, including Unicode names
with IDs outside the translated property catalog, in both panes. Standard
property headers still follow EN/RU. Reuse the owning source descriptor as the
property-name fallback without re-enumerating the provider.

Column initialization keeps Name at source logical index zero while retaining
the saved visual header order, including Name moved after other visible columns.
Refresh and reopening must retain that order and widths in both panes under
EN/RU at980×620/640×400. Direct InitColumns clears native marks before property
enumeration, including unchanged column catalogs; it does not clear published
Qt rows or their widget selection. Subsequent source restoration owns marks.
Save selection before direct InitColumns; restore that source snapshot afterwards.
Selection deltas update native marks immediately in their owning pane, including
the inactive peer. Notification guards and alternative mode suppress ordinary
selection deltas; SaveSelectedState reads marks without overwriting them. Preserve
parent exclusion, sorted source indices and peer focus through source Reload.
Inspect both pane roles EN/RU at980×620/640×400.

Column header menus retain source property order/checks/first-item protection,
toggle the owning panel's source visible catalog and append a newly shown column
after all visible columns. Hide/show after moving a column must follow that append
policy, while hiding the sorted column restores name/ascending metadata and the
header indicator, preserving row order until Refresh. Context, resize
and move callbacks on an inactive pane preserve active focus/provider/selection
and peer header state. Cancel changes nothing. Persist visible compact order and
width through source ViewSettings; inspect EN/RU both panes normal980×620 and
compact640×400 menus, address/list/status groups and reload/reopen consumers.
SaveListViewInfo updates visible width/order and persisted settings without
rewriting the property catalog. A menu hide/show re-adds the catalog width,
as native AddColumn does; resizing before Refresh still persists that width for
the next initialization/reopening. Catalog and visible logical indices remain
separate from header visual order.

Deleted archive items retain source OnCustomDraw's red text (RGB255,0,0), enabled
by default only when the archive reports deleted items. Use source item indices
and the owning provider for every visible table cell and icon label; parent and
ordinary items keep platform text color. Preserve alternative-selection pink
background, focus/marks, peer state and bytes. Suppress provider queries during
notification guards and reject a replaced provider. Verify on/off archive metadata,
reload/rebind/reset and both pane roles under EN/RU table/icon, standard/alternative
selection at980×620/640×400. Geometry reports do not establish color pixels;
inspect the affected view capture when needed. Rendered Windows parity remains
unverified without the matching native reference state.

Parent navigation to a namespace provider that fails during load, directory
validation, count or icon projection must leave an empty, usable view. Publish
the bound source address before loading unless CalledFromTimer suppresses it,
release the operation/notification guards, restore sorting/redraw and leave peer
provider/rows intact. A later explicit Refresh recovers the same provider.
Inspect both panes and status/address groups under EN/RU normal980×620 and
compact640×400, including load cancellation. Parent retains its source ignored
reload-result policy; explicit OnReload remains the HRESULT message owner.

Rename activation uses original RenameFile's focused row, independently of
operated marks; OnBeginLabelEdit rejects parent/read-only targets. The table/icon
editor must show that row's raw source name in either pane. Escape preserves
names, selection and bytes; a subsequent Rename can commit normally. Bounded
`panel_rename_state` consumers cover ordinary multiselection and alternative
marks on a different item. Existing editor geometry reports below apply.
Twenty current activation reports cover both panes/table-icon/selection modes
and two symbolic-link cases at980×620/640×400, with visible inline editors.
An address Return's queued list-focus request must not close a newer inline
Rename editor in the same pane when the next events or resize are delivered.

Cocoa table Rename reserves native focus-frame vertical margins inside the row.
The visible QFocusFrame must fit both the viewport and that row's vertical band;
the editor must retain its minimum text height. Icon-editor geometry is retained.
[Native RED/GREEN and platform checks](../archive/qt-port/upstream-port-audit/p3-06-rename-focus-2026-10-16.md).

Main-panel navigation contains one Parent button before the address field,
matching original Panel.cpp. Every visible panel retains its own source status
fields. The global operation-feedback row is hidden while empty and appears for
an operation/error message. Successful publication clears it; failed archive
completion retains its diagnostic. Four EN/RU normal980×620/compact640×400
one→two→one consumers inspect these groups and transient visibility.
[Rendered/source and three-platform receipt](../archive/qt-port/upstream-port-audit/p3-06-main-chrome-2026-10-16.md).

Rename completion uses original OnEndLabelEdit's saved selected names and raw
prefix/name, focusing the new name on success and the old name on error/Cancel.
Do not replace the saved selection with a single Qt path. After the worker,
source callback text is suppressed until the next load begins; raw item roles
and editor values stay available. Verify real filesystem success/collision/
load cancellation, standard/alternative marks, preserved peer bytes and recovery.
Existing name-view/editor normal/compact geometry remains the presentation
contract; source text suppression does not change grouping or widget geometry.

Native archive Rename additionally uses real F2 and the visible table/icon
inline editor, with official native writer success/Write failure/source Cancel
followed by S_OK/S_FALSE/E_FAIL/E_ABORT publication at2/301 rows. The posted
source reload ignores its HRESULT; explicit Ctrl+R owns recovery diagnostics.
Verify new/old focused name and source marks, same provider, unchanged peer,
payload bytes and successful retry after failure/Cancel. Inspect published,
recovered and retried states at980×620/640×400 in both panes and EN/RU.

Reference: pinned App.cpp::OnCopy/Reduce_Path_To_RealFileSystemPath,
PanelKey Shift consumers, CopyDialog.cpp/.rc and Panel/Agent/FSFolder CopyTo/
CopyFrom interfaces. Keep the original title/prompt/summary and focused
versus operated selection. Ordinary destination begins with the source opposite-
panel rule; one-panel reduction follows the original filesystem/root-prefix
algorithm. Qt must not replace an empty peer target with its own current folder.
Classify filesystem/ADS/archive/root/drives through source capabilities/prefixes,
preserving relative/direct/multiple-item normalization, path diagnostics and
unsupported root/ADS refusals before publication. Original copy options, naming,
selection/row refresh, title and source focus must survive the async continuation.
Accepted archive Move keeps its checked source Copy/Delete/rollback extension.
Verify both panels, F5/F6 and Shift routes, success/failure/Cancel, Unicode/spaces,
no unintended output/input changes and operation guards. Inspect EN/RU normal/
compact Copy dialog and resulting panel JSON using their existing contracts.
Native Windows ADS/device and rendered/cold-stable acceptance remain mandatory.
Path failures use original Panel.cpp prefix+LF+HRESULT formatting. Its platform
message box owns wrapping and autosizing, without a fixed upstream dialog
resource geometry; inspect text exposure/fit and standard button grouping under
both size probes. Keep Copy dialog's existing normal/compact dimensions separate.
Directory summary suffixes use original Add_PathSepar.
Copy/Move menu and ordinary F5/F6 do not require marked rows: retain original
CFileMenu availability and let OnCopy's OperSmart choose items. With no marks,
verify the complete summary, Cancel and untouched input bytes in filesystem and
archive views under EN/RU normal/compact geometry. Empty writable folders are
an enabled no-op; busy/read-only/provider restrictions still apply. Retained
timer/notify guards must survive async handoff and release before terminal
synchronous panel refresh.
After Copy publication verify original source/destination saved states: ordinary
Copy/Move clears source marks, preserves the captured current item and peer
selection, updates the source title and restores source list focus. Shift keeps
its source saved-selection semantics. Inspect both source-panel roles under
EN/RU1100×640/640×400, including both address/list groups and toolbar. Do not
let delayed progress destruction replace the restored title. Offscreen window
activation may be restored by the driver; native focus/rendered parity still
requires native evidence.
Ordinary Copy/Move refreshes the captured destination panel even when typed
output is outside it. Verify both source roles and bounded/301-item publication,
preserved peer selection, successful bytes, overwrite Cancel and dialog Cancel.
Disable polling in the regression so the operation itself must publish rows.
Inspect the resulting address/list groups at1100×640/640×400. Namespace OnReload
must republish its existing provider rows through the same renderer.
Its source load stage must reject invalid directory-property types and property/
load failures before publishing rows. A failed reload leaves no stale rows and
restores notification guards; a later valid reload recovers the same list.
Inspect the empty namespace/error state at980×620/640×400 through the existing
address/list/toolbar contract. No new dialog layout is introduced by validation.
OnReload retains the original HRESULT message for load/property failures,
including LoadItems E_ABORT. GetNumberOfItems uses its written count regardless
of HRESULT; an unwritten output is initialized to zero by the Qt adapter.
Count metadata E_ABORT is not a load-operation Cancel. Inspect EN/RU both-pane
detail/icon written-zero/written-two/unwritten Refresh/timer/recovery states at
980×620/640×400; retain peer, selection and bytes. Parent and post-Copy/Move must
publish written counts without reporting that metadata status as a load failure.
The post-Copy selection stage must return an actual load/property failure before
reading failed provider state. Check both source roles for Copy/Move: output bytes, input
retention/deletion, marks, notification guards, source focus/title and successful
recovery on the next valid peer reload.
Verify direct result, explicit reload, changed-folder timer and
namespace/filesystem Refresh command under EN/RU; close the OS-managed error and recover
through a valid reload. Inspect message text, standard button group and exposure
under normal/compact probes, retaining platform autosizing. Archive synchronous
load errors must also reach this callback.
Filesystem reload keeps its bound provider through controlled E_ABORT and
allocation failure, empties failed rows, then recovers after a valid reload;
inspect both source roles and the Copy/Move filesystem-peer completion consumer.
Worker failures must retain their original HRESULT/provider, clear failed rows
and show the source message before async completion. Check both panels, EN/RU,
normal/flat navigation and Refresh, old-generation rejection and successful
worker recovery. A Copy/Move peer failure must restore source focus/title only
after temporary projection ends; inspect resulting panels at1100×640/640×400.
When a bound folder disappears, Refresh/timer must clear native items/operated
indices as well as visible rows. Preserve provider/guards/command recovery and
the existing missing-folder status; restoring the directory recovers control
bytes. Inspect both panels/EN-RU/standard-alternative empty views at980×620/640×400.
Typed missing descendants must publish the source closest existing ancestor,
its prefix/title and list focus, preserving peer selection and input bytes.
Verify both panels/EN-RU/absolute-relative/trailing separator at980×620/640×400;
Windows also strips original outer quotes before Qt path classification.
Native quoted-route execution and whole RefreshListCtrl acceptance remain required.
Source CalledFromTimer suppresses address publication during reload. Preserve
the draft text, cursor, selected text and address focus while updating rows;
ordinary Refresh must publish the current folder instead. Check both panels,
EN/RU, small/bounded-worker/flat filesystem and bound namespace/archive callers,
with peer state, native prefix/title and private control bytes unchanged.
Inspect resulting address/list groups at980×620/640×400.
When ShowDots exposes the parent row, source reload selects it when fallback
focus is row0 and restores it for an explicit focused name of "..". Exclude the
sentinel from native marks/operated indices. Open/Enter and Open Outside must
remain available for that selected row through the original source handlers.
Check hidden-row controls, both panels/EN-RU/standard-alternative selection,
small/worker/flat reload and existing parent icon/tooltip, with peer and control
bytes preserved. Inspect both groups at980×620/640×400.
Row icons follow original RefreshListCtrl system-interface/fallback policy,
including slow filesystem/stream folders, positive-index validity, attribute
masking and directory-bit correction. Keep icon lookup inside bounded GUI row
publication. Check real filesystem/ZIP small and301-item rows with real icons
on/off, source indices and unchanged private bytes. Attribute failures/Cancel
must reach reload owners and recover on a valid reload; asynchronous publication
stops once with its HRESULT and ignores cancelled/replaced generations. Inspect
normal/compact address/list groups; native shell appearance still requires a
rendered Windows reference.
Name display uses original OnNotify/SetItemText fast-name formatting: replace
RLO with underscore, compress runs of five or more spaces, show underscore for
an empty fast name and retain the Windows trailing-space marker. Preserve
unchanged native operation names and editor values. Failed generic name/prefix
properties display the original "Error: " cell text. During source notification
suppression, keep cached names or blank first requests. Check callback text and
indices at980×620/640×400, plus real ZIP Rename exact names/bytes, Escape and
invalid-name refusal. Qt keeps existing NFC and flat-prefix display adaptations;
native Windows marker rendering still needs a rendered reference.

## Copy/Move dialog

`native_archive_ads_copy` applies this same resource contract to a nested ZIP
source and opened native NTFS-stream peer. Both panes and EN/RU exercise Copy,
dialog Cancel, occupied-stream Skip/Cancel and original unsupported Move at
440×250/340×220, with main address/list pairs at980×620/640×400. It introduces
no new controls; JSON establishes Qt geometry, separately from exposed/rendered
Windows reference acceptance.

The filesystem-provider command consumer `fs_provider_command` reuses this
source CopyDialog resource and controls for the opened NTFS-stream peer. Its
EN/RU F5/F6/Cancel/Skip cases cover both source panes, path/Browse/summary/footer
visibility at440×250/340×220 and both main address/list pairs at980×620/640×400.
The same suite checks accepted case-only F2 and Escape for file/directory rows
in both panes/languages, retained provider/selection and protected bytes.
Widget JSON establishes Qt geometry; exposed native Windows reference and
pixel acceptance remain separate. No new destination control or stream-specific
dialog is introduced.

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/CopyDialog.rc` and `CopyDialog.cpp::OnSize`.
Implemented view: `src/dialogs/copy/CopyDialog.cpp`.

- Copy/Move begins with the shared source summary: one name or selection count,
  source paths in its tooltip and the total size only when all selected sizes are known.
  It reads provider metadata without walking the selected directories.
- Multiple selected paths are available in a collapsed disclosure with a bounded,
  independently scrollable list. The last selected path remains reachable.
  Expanding it must not displace the dialog footer outside the window.
- Destination label precedes the editable path; Browse stays to its right and
  uses a translated caption with the platform's ordinary button size.
  The generic Combine caller retains its twenty-DLU utility button and text summary.
- Copy/Move uses a plain path field, a system folder picker and an Other panel
  shortcut when the second panel is present.
- A writable peer panel supplies the initial destination; Other panel restores
  that proposal after editing. An unavailable peer leaves the current source
  filesystem location as the proposal and disables the peer shortcut.
- A single filesystem object offers Name at destination for supported directory
  destinations. An unchanged name preserves the existing provider route and raw
  destination. A changed name denotes the destination object, including when an
  existing directory occupies that name; it must not silently become a container.
  Typed file paths and unsupported providers retain their existing path
  interpretation without an added name.
- Empty or invalid input, a known unwritable destination and an unchanged source
  location disable acceptance with an inline explanation. Native operation checks
  remain authoritative. Cancel preserves the original value; accepted
  input preserves significant spaces and Unicode. Relative paths keep the caller's
  existing base directory.
- The redesigned form uses the shared dialog margins/footer and sizes the
  initial summary by its content area rather than the old RC frame.
  Source NormalizeSize(true) applies once at first native
  Show when the frame overlaps and exceeds the work area. EN/RU Copy/Move
  regressions cover9/12/32pt initial/compact states. Source resizable chrome
  requests minimize/maximize, subject to native modal-window conventions.
- The primary action names Copy, Move or Combine according to the caller,
  including live localization; its existing acceptance role is unchanged.
- Names preserve line boundaries. Read-only scrolling makes the displayed
  selection and long filenames reachable without enlarging the footer.
  The source text has no Tab stop; the disclosure, destination,
  Browse and footer remain keyboard targets. Enter preserves raw destination
  spaces/Unicode; Escape preserves the original public value.
- At 440×250 and 340×220 in Copy and Move states, the label fits, path and
  Browse do not overlap, the collapsed source and footer stay fully visible,
  and primary/Cancel remain separate. Qt may enforce its style minimum size.

Adaptations: Qt layouts replace Win32 resize arithmetic. The agreed catalog
replaces the always-visible source text in Copy/Move with SourceSummary and
Disclosure; Combine retains its native summary format. Native provider counting
and unknown-size propagation are shared with the existing formatter. Flat archive
directory size remains unknown rather than becoming a proxy total.

`copy_dialog` covers the bounded list, footer, draft preservation and language
change. `panel_runtime` covers normal/flat archive metadata, including unknown
sizes; provider and operation suites cover routing and cancellation. Historical
resource reports describe the earlier text-summary form and do not establish
acceptance of the redesigned source presentation. Current runtime evidence and
remaining design work belong in the source audit. Geometry reports alone do not
establish rendered native or pixel acceptance.
