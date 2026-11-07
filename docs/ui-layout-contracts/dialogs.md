# Shared dialogs and operation feedback

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

### Standard source dialog buttons (P3-05.9, accepted button scope)

Reference: pinned LangUtils.cpp::kLangPairs/LangSetDlgItems, GuiCommon.rc and
each dialog's resource/OnInit caller. Selected 7-Zip language controls OK,
Cancel, Yes, No, Close, Help and Continue. Missing entries keep the resource
caption; Memory uses IDCONTINUE. Hot reload preserves roles and pending input.
The approved redesign replaces generic OK with the result of Copy/Move/Combine,
Split and password Continue, using application catalogs and the same button role.
Options property-sheet/native dialogs retain their platform language owner.
About uses Close with the original confirmation role and default Return.
`language_standard_buttons` checks twelve source consumers at their normal/
compact sizes below and produces48 EN/RU footer reports per platform. Reports on all three platforms reviewed
2026-10-06 show visible, separate standard controls and exact source captions;
rendered Windows parity and parent/final acceptance remain pending.

### Disclosure accessibility (redesign, 2026-10-18)

Expandable sections expose their current expanded/collapsed state through Qt
accessibility. Their accessible press action and Space operate the same header
button and preserve pending values. Disabled headers cannot change the section.
Accessible names follow the current translated caption unless explicitly supplied.
State changes notify accessibility clients; this Qt contract does not establish
screen-reader announcements on a particular platform.
An expanded section hides its collapsed-state indicator. The header and chevron
keep their position when that indicator disappears; expanding never clears a draft.

### Secret reveal and editing position

Password request, Extract and Create forms preserve the exact Unicode text,
cursor and selection direction when Show password changes. Mouse activation and
Space on the checkbox use the same behavior; keyboard focus stays on that checkbox.
Showing a password neither normalizes its text nor submits the form. Create keeps
the confirmation field's own draft when hiding and showing it. Validation,
accepted values and cancellation remain with the dialog and operation owner.

### Contextual modality on macOS (redesign, 2026-10-18)

Contextual name/mask/comment, Copy/Move/Combine, Split, Link, password, memory,
overwrite, Add/Extract, format-options and file/folder pickers use window modality
when they have a parent on macOS. Native Qt sheets block that owner while an
independent window remains interactive. Parentless entry points retain their
existing modal execution. Windows/Linux keep their existing modality.
Return/Escape, source result codes, raw drafts and the caller's synchronous
lifetime remain unchanged. Memory/Overwrite Cancel retains IDCANCEL.

Nested cancellation leaves the parent form and its draft available; closing
returns to the owner's previous control. Operation progress retains its existing
application-modal policy and worker/callback lifetime. Help and reports receive
no new modality from shared layout styling. `context_dialog` checks actual native
AppKit sheet attachment, owner/peer input, nested Cancel and draft/focus retention;
the worker password consumers separately check success, empty input and Cancel.

### Help and About: shared frame (redesign, 2026-10-18)

Help retains the original bundled HTML, links, fragment navigation, history,
text and horizontal/vertical scrolling. Only its Qt window uses the shared
content/footer spacing; Back follows actual browser history and Close keeps
its native role. Initial focus enters the reading area. Language changes keep
the current topic, and missing topics retain the existing warning.

About retains Sevenzip identity/version/project link and complete 7-Zip
attribution, release, copyright, license and homepage. Its content and footer
use the shared frame, with Close as the visible confirmation caption. Return
still accepts the dialog; Escape and window close preserve their existing result.
Native palette changes must restyle links in the existing window. Internal
attribution padding remains independent of the outer dialog frame.

### Shared ListView: Properties and Folder History (P3-06)

Reference: pinned FileManager/ListViewDialog.rc and OnInit/OnEnter/OnOK callers.
The approved redesign uses the shared dialog layout and native content-sized
buttons. Reports have Copy/Close; Folder History has Open/Cancel. Preserve
one-column hidden History header and the blank, resizable two-column Properties
header. Copy uses the full selected values, or all rows when nothing is selected;
an empty report cannot overwrite the clipboard. Native button ordering is retained.
`list_view_dialog` writes EN/RU
source-initial and540×360/360×240 reports, checking exposed list/footer without
changing stored full values, clipboard/detail or acceptance behavior.

### Link (P2-05)

Reference: pinned FileManager/LinkDialog.rc and source OnInit/OnButton_Link.
Initial size follows visible controls and native minima. Root/footer use the
shared redesigned dialog layout;
Browse is20DLU wide. Empty Current target text reserves no row, and native
spacing separates paths and Link Type. The footer uses a native QDialogButtonBox
with content-sized Create link/Cancel actions.
The source resizable/minimize/maximize capabilities are requested through Qt
window flags; native window managers may suppress modal title-bar controls.
The resource has no context-help title-bar button.
Link from combo/browse precedes Link to combo/browse and current target text.
Link Type contains Hard→File Symbolic→Directory Symbolic→Junction→WSL in source
order. Create link is the default footer button; button ordering follows the
platform. Qt scrolls the
content when necessary; requested compact sizes expand to show the path inputs
and all link choices. Desktop-only Junction/WSL remain visibly disabled on
POSIX with a platform reason. Source radio/type choices and errors remain owned
by CLinkDialog; Qt controls bind original captions and LangSetDlgItems maps
IDCANCEL to its source language entry.
Radio captions use the existing220-pixel language wrapping binding so native
Windows EN/RU font metrics do not force horizontal clipping at360 pixels.
`link_dialog` writes EN/RU520×500/360×300 JSON and checks named controls after
scrolling and footer exposure, plus native-font initial frames. Review captures
return to the top after reachability probes. Historical native EN/RU reports and
rendered original initial states are recorded in the
[secondary-dialog receipt](../archive/qt-port/upstream-port-audit/p3-06-secondary-dialogs-2026-10-16.md).
Native font metrics, compact scrolling and modal chrome prevent a claim of
complete rendered Windows pixel equality.

### Benchmark (P2-04)

Reference: pinned `CPP/7zip/UI/GUI/BenchmarkDialog.rc` and its source state bodies.
POSIX failed/empty affinity uses original System.h normalized processor fallback
in the hardware-threads leading CPU count and choice limits; that count must not
be zero or retain InitST's one when a larger processor count is available.
Windows retains its native source helper. These system failures do not change
grouping or controls.
The approved redesign places Dictionary/CPU threads above their fields, with
Memory alongside them. One table contains Operation/Result and the original
Size→Speed→CPU Usage→Rating / Usage→Rating columns. Compressing/Decompressing keep
Current/Resulting rows; Total Rating shares the numeric columns. Values and
numeric headers are right-aligned. Elapsed/Passes and the source pass limit
precede the log and CPU/version/system information.
The common footer holds Copy, Restart, Stop and Close in normal mode and
Copy and Close in total mode, using native button roles and content sizes.
Close calls the existing source cancellation/join workflow. Restart remains
initially focused in normal mode; total mode initially focuses its console.
Tab includes parameters, scroll viewport, nonempty log and footer actions.
Both source templates request minimize/maximize/resizable style and no
context-help title-bar button; Qt retains native modal-chrome conventions.
Both log and total console are read-only, use the system monospaced family at
the dialog text size and preserve horizontal/vertical scrolling without wrapping.
The normal log appears beneath the table when nonempty. Copy exports full
formatted source metrics, parameters, metadata and log; table columns retain
alignment when pasted as tab-separated text. Total Copy uses selected or full
console text and is disabled while empty. Neither copy action closes the window.
The normal report scrolls in both directions at compact sizes; keyboard arrows
scroll its focused viewport. System information wraps; a gutter protects it
from overlay scrollbars. The common footer remains exposed.

`benchmark_lifecycle` writes source-initial and normal/total EN/RU JSON at960×650 and640×400,
checks named labels/inputs after scrolling, footer exposure, keyboard traversal,
live language changes, long Unicode copy and source worker lifecycle. Lower system
rows may be below the compact viewport and remain reachable by scrolling.
Historical original Windows comparisons are retained in the
[secondary-dialog receipt](../archive/qt-port/upstream-port-audit/p3-06-secondary-dialogs-2026-10-16.md);
they do not establish rendered parity for the approved redesign. Current scope
and platform validation are recorded in the [redesign audit](../upstream-port-audit/ui-redesign.md).

## Commands and reports

## Folder history list

Reference: `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/ListViewDialog.rc/.cpp` and
`PanelFolderChange.cpp::CPanel::FoldersHistory`.
The single-column list fills the resizable body; Open/Cancel sit below it.
It uses the shared dialog layout and native content-sized buttons. Native button
ordering is retained. Source minimize/maximize/resizable capabilities are
requested with Qt flags; no context-help title-bar button is added.
There is no separate Delete button in the reference. Delete affects all
selected rows, Ctrl+A/C copy/select, Enter confirms the focused path.
Long paths remain reachable through scrolling, without eliding stored text.
SingleClick changes activation to one click and enables hover selection without
confirmation. Modified selection clicks do not activate. Interaction coverage
is in `list_activation`. Cancel leaves stored history unchanged; Open retains
the source history update and focused-path navigation.
`folder_history` verifies list/footer visibility and separation at 540×360
and 360×240 and writes JSON reports. Qt geometry establishes structure and
usability. The original rendered EN/RU history states are recorded in the
[secondary-dialog receipt](../archive/qt-port/upstream-port-audit/p3-06-secondary-dialogs-2026-10-16.md);
widget geometry does not establish pixel equivalence or physical input.

## Hash results

The native HashGUI result presenter uses the same ListViewDialog resource and
two-column property/value body. Its title is «Контрольные суммы»; source bundle
rows include the filename, byte size and requested digest. It starts without
selected rows and retains the native DeleteIsAllowed flag. Copy/Close remain
below the list, using the shared report layout and full-value copy behavior.
`hash_results` checks exact source-generated rows against a
CRC32 fixture, title/state and unclipped, separate list/footer/buttons at
540×360 and 420×360; reports are `hash-results-540`/`hash-results-420`.
Horizontal scrolling remains available for long Unicode names/values. This
checks Qt structure/usability, not rendered Windows pixel parity. The complete
hash command/file workflow is connected and accepted in P2-03. PanelCrc and
HashCalcGUI invoke this same source presenter; current540/420 JSON reviewed
2026-10-03, macOS full gate199/199 and Windows13/Linux focused consumers passed.

## Full text detail

Reference: EditDialog.rc; source scope and baseline are recorded in the
[properties audit](../archive/qt-port/upstream-port-audit.md#свойства-и-двухколоночный-список-2026-09-28).
CEditDialog uses the shared redesigned dialog layout and native button sizes.
It requests the source resize/minimize/maximize capabilities without a title-bar
context-help button. CEditDialog has one read-only multiline
edit above Copy/Close, with both scroll directions and no automatic wrapping.
Its system monospaced font preserves the dialog text size. Copy uses selected
text, or the full text when nothing is selected, without closing the dialog.
Text/Title are copied without truncation. Close has initial focus and is the
default button: initial Enter closes. Tab reaches the edit, where Enter remains
inside the edit (ES_WANTRETURN). Tab then reaches Copy and Close.
Geometry: 480×320 and 320×240,
visible, separate text area and footer. Coverage is described in
[testing](../testing.md). Rendered Windows parity remains unverified.

## Shared source identity

Shared source identity follows the current design catalog: show the source name
and icon, with the supplied full path in the hover tooltip and accessible context.
An optional second line conveys semantic information such as a file type,
application version or extraction scope. It does not repeat the parent path.
Long paths must not enlarge the identity's visible layout. Owners retain the
original path values for navigation and operations. Explicit location/destination
fields and diagnostic reports keep their own roles.

## Selected properties

The approved v34 design replaces the selected-properties list with a nonmodal
information window. The bound native source collector supplies a snapshot of
the selected objects, current folder and each archive level; subsequent
navigation cannot silently change it. Windows ordinary files retain the native
shell sheet. [Source comparison and adaptations](../upstream-port-audit/property-report-ownership.md#selected-properties-snapshot-and-information-window--2026-11-07).

SourceSummary shows the selected name/type or selected-object counts.
MetadataRows shows location, data size and known modification text. Unknown
is distinct from zero; folder and multiple-selection contents are not newly
calculated. Existing source totals remain in additional information, qualified
as potentially partial. A packed size is not presented as an exact independent
file size, and stored CRC is explicitly metadata rather than a content check.

Comments, technical values, current-folder metadata, parent archive entries and
archive levels have distinct disclosures. Duplicate property IDs across those
owners remain distinct. Unknown custom properties are preserved. Values wrap
and scroll; Copy information copies full values even while collapsed. Ordinary
text selection supports copying a fragment. Copy/Close stay outside the scroll
viewport. Escape closes and returns focus to the surviving source view.

The new form has EN/RU normal/compact geometry producers in `properties_window`;
`properties_runtime` retains source ownership checks across normal/flat and all
four views. Legacy `panel_property`/`source_properties` projections still exercise
the generic report's native formatting, truncation, detail and clipboard paths;
they are not geometry acceptance for this new window. Source/runtime validation
scope is recorded in the linked audit. Rendered original Windows pixel parity
is not claimed for the approved redesign.

## GUI early error messages

The standard early GUI errors have no custom upstream dialog resource:
UpdateGUI::ShowDialog and ExtractGUI call Explorer/MyMessages' MessageBox
presentation. `gui_error_policy` checks the corresponding Qt Critical/OK boxes
at requested 480×220/320×160 in EN/RU, including fitted message labels and visible
OK controls; each of the six visible states emits two current-build JSON reports.
Suppressed states emit no box. Qt owns any minimum size adjustment and native
button placement; macOS omits the window caption. The original hardcoded English
texts remain unchanged. This is a standard message adaptation, not rendered
Windows pixel parity or a contract for custom Extract/Compress dialog geometry.
Current macOS boxes auto-fit to 228/304×116, Linux to 204/274×98 and Windows to
293/400×96 for writer/output errors respectively. Both requested sizes resolve
to these platform sizes, with fitted labels and visible/enabled OK; the requested
compact width does not override QMessageBox's standard fit policy.

## Shared name/mask dialog

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/ComboDialog.cpp/.rc` and
`BrowseDialog.cpp::Dlg_CreateFolder`. Implemented view: `ComboDialog.cpp`.

- The prompt precedes one editable combo, with OK/Cancel at the bottom.
  Initial Value remains visible after populating ordered Strings.
- The redesigned initial frame retains a font-scaled preferred width, while
  height follows the prompt, editable field and native actions. Long prompts
  can wrap and enlarge the minimum height. Explicit normal/compact resizing
  below remains a separate adaptation.
- Folder/file creation uses source title, prompt and default-name language IDs;
  selection masks use the same view with their own prompts.
- CPanel::SelectSpec owns the Select/Deselect titles, mask prompt, initial `*`
  and Cancel return. `source_selection` checks both languages/panes and standard/
  alternative marks, with32 current360×150/260×150 Combo reports. The same
  source owner handles an inactive-pane direct call without changing its peer.
- OnOK preserves raw text, including spaces and an empty value. Validation
  belongs to the calling operation. Cancel leaves the public Value unchanged.
- `combo_dialog` checks folder/file/mask views at 360×150 and 260×150:
  fitted prompt, reachable combo/footer, control order and separate buttons.
  JSON reports accompany the checks; screenshots remain optional.
- `source_create` exercises actual CreateFolder/CreateFile callers in both
  panes/languages and normal/flat views. Forty-eight current 360×150/260×150
  reports reuse this contract; creation keeps the source first-component focus,
  alternative marks and distinct failure-refresh policy. Cancel preserves the
  pre-dialog state. Source provenance and checks are in the
  [receipt](../archive/qt-port/upstream-port-audit.md#p3-05--source-create-filefolder-scenario-2026-10-10).
- ZIP comment uses source `PanelOperations.cpp::ChangeComment` with the focused
  item name and localized Comment in its title, localized IDS_COMMENT2 prompt
  and the existing raw property value. `native_comment_ui` checks EN/RU at the
  same two sizes: current comment-en/ru-360/260 JSON reports preserve prompt,
  editable combo and separately reachable OK/Cancel. titleID=0 preserves the
  caller's dynamic title; source language ID0 is the application name.
- Original Set Comment Error/HRESULT remains the error presentation. On macOS,
  which omits QMessageBox window captions, the caption is the visible message
  heading and HRESULT detail is informative text. Tests exercise real writer
  refusal on both languages and ordinary/AES ZIP. Comment now uses the shared
  source panel HRESULT/caption helper; its source-owned refresh follows the
  error, while dialog Cancel returns before refresh. Existing Combo JSON and
  normal/compact geometry consumers retain the same controls and text contract.
- Filesystem comments use the same source dialog, focused name/title and
  IDS_COMMENT2 prompt. `filesystem_comment` checks existing value, EN/RU,
  normal360×150/compact260×150, File Comment and Ctrl+Z, OK and Escape, selected
  rows and unchanged file bytes. Eight filesystem-comment-en/ru-0/1-360/260 JSON
  show a fitted prompt, editable combo and separately visible/enabled OK/Cancel.
  The source FSFolder writer trims the accepted comment; ZIP's raw whitespace
  behavior remains separate. No new dialog or rendered Windows parity is claimed.
  The source Escape/Cancel path does not reload the folder. An externally added
  item while the dialog is open remains unpublished until explicit Refresh;
  selection, focus, provider and peer rows stay intact. Accepted Comment reaches
  the source reload phase even after operation failure. Load failure/cancellation
  leaves no stale rows and releases guards; a later explicit Refresh recovers.
  Preserve the original ignored reload-result policy in this operation caller.
  Inspect both panes in EN/RU at 980×620/640×400 after Cancel and terminal load.

Qt layouts replace OnSize. Plain-text wrapping handles Qt label rendering;
the native AddString behavior is adapted by restoring initial edit text after
Qt populates the combo. Windows rendering was unavailable; geometry establishes
structure/usability on the tested macOS Qt platform.

## Split and Combine dialogs

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/SplitDialog.rc`, `SplitDialog.cpp::{OnInit, OnOK,
OnButtonSetPath}`, and `PanelSplitFile.cpp::CApp::Combine` with `CopyDialog.rc`.
Implemented views: `SplitDialog.cpp` and the existing `CopyDialog.cpp`.

- Split puts the destination label above its editable combo, with Browse on
  the right. The volume-size label and editable preset combo follow below.
  The source filename is appended to the window title, as in OnInit.
- The source initial client template is304×112DLU. The volume combo remains
  left-aligned at96DLU, including after resize; Browse uses20DLU width with a
  native minimum hit target and the source `...` caption. Only the destination
  combo expands horizontally.
  Font mapping and Qt minimum sizes protect labels/footer on each platform.
- OK/Cancel remain at the bottom. Invalid volume input opens the source
  Critical/7-Zip/OK modal message and keeps Split open, including accept().
  OK or Escape dismisses only the message; raw input remains available for retry.
- At constrained height the Split form scrolls independently of its completion
  buttons. Keyboard traversal reveals focused fields without moving the footer.
- Combine uses the original shared Copy dialog: destination, Browse,
  read-only scrolling summary and footer. The summary lists the count and
  size, source prefix, first two names, ellipsis when needed and final name.
- `volume_dialog` checks normal/compact Split at 480×240 and 340×240 in
  normal/error states, and Combine at 480×250 and 340×250 with 2/3/5 parts.
  It writes JSON and verifies order, reachable controls, fitted labels,
  separated path/Browse and separate OK/Cancel.

Qt layouts replace OnSize arithmetic; Qt native folder browsing replaces
MyBrowseForFolder. The earlier inline-error adaptation is removed2026-10-10;
no extra error control exists in the source resource. Separate current-build
message JSON checks Critical/plain text/OK and native auto-fit; macOS ignores
its caption. `language_dialog` also checks English/Russian prompts and raw input
across language changes. Windows volume/language consumers use native QPA/fonts
and the existing UI resource lock. The original parser and partial-failure
vector remain authoritative; invalid submission starts no operation or directory.
Reports establish structure/usability on macOS; a rendered Windows reference
was unavailable and pixel-level parity remains unverified.

## Progress dialog and modal lifecycle

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
ProgressDialog2.rc/ProgressDialog2a.rc and complete progress, message, pause,
stop and worker handlers. The agreed November 5 operation design replaces the
old statistics presentation: current file and a separate percentage precede
the bar; accepted source/destination locations expand separately. Times, byte
statistics, counts and a disclosure containing those statistics are absent.
The source counters and range conversion remain operation state.

Unknown totals use an indeterminate indicator without a percentage. Known
native byte/count totals and percent-only transports drive the same display;
a new unknown phase clears the previous percentage. Preparation has a short
state caption. Pause freezes rendering and preserves reached progress. It is
available only when supported by that operation's transport; callback-only
filesystem/volume operations hide it. Minimize, Pause/Continue and Stop remain
in the footer, reachable when the body scrolls. Full locations come from the
accepted operation plan, not from the changing current-file string.

Stop and closing a running window ask Continue working / Stop. Continue is the
default and Escape answer. Refusal restores the former pause state. Confirmed
Stop disables repeated requests and shows Stopping until the owner finishes;
completion retires an obsolete question without waiting for user input. Neither
an empty message list nor a full bar proves success. The operation supplies its
result and completeness after required publication/source removal. Full success
closes automatically. Failure, cancellation, skipped items or messages keep a
short result and optional diagnostics until Close, with the running bar hidden.
Cancellation warns that changes may already have completed.

Diagnostics preserve source strings, line splitting and order. The ordinal
column is hidden. Copy selects the chosen messages, or all messages if nothing
is selected; macOS Meta is accepted along with Control. Long paths wrap and the
body scrolls; final actions remain separate. Existing source-owned tasks keep
their original owner and joined lifetime. Independent 7zG tasks retain their
existing process/typed-job boundary. Changing the form does not detach work
from a tab or add session recovery.

The reviewed source mapping records explicit omissions and Qt adaptations in
`progress-dialog.contract.json`. Focused behavior checks cover range boundaries,
unknown phases, pause, Stop races, worker results and clipboard; normal/compact
reports check reachability and grouping. The new presentation is undergoing
runtime acceptance. Source-resource comparison is not rendered Windows pixel
parity or proof of the complete operation catalog.

Independent 7zG operations are reachable from Operations in each file panel's
command bar. The same application-wide entries appear in other file windows.
Two-panel mode uses a compact clock button with the complete accessible caption.
Selecting a live entry returns its existing draft, question, progress or result;
it never starts the command again. A pending parent-owned result returns the same
result window. Completed successes are informational recent entries, without a
second result window or a process-lifetime hold. Pending problems take precedence
in the button caption and remain until their actual result is closed. This list
does not change source-owned local task cancellation or session restoration.

MessagesDialog.rc (`IDD_MESSAGES`) uses a two-column numbered/message list and
Close footer,456×176DLU initial client and resize/minimize/maximize capabilities
without context-help. Assigning a parent must preserve those source flags.
Close has initial focus, matching the first resource tab stop.
The Qt source port preserves message text, line splitting and numbering.
Each opening displays the current message set; `IDL_MESSAGE` is above `IDOK` at 760×450 and
640×380. Current-build JSON confirms both controls are visible, separated and
within the dialog. Create follows the source modal lifetime until Close/Enter
(IDOK) or Escape (IDCANCEL), retaining the stack-owned PanelDrag message vector.
The independent File-menu journal was removed under P2-09. Font-DLU scaling
may enlarge the Close button to its Qt size hint. Windows rendered appearance
remains unverified.

### Redesign: shared form and diagnostic layout (2026-10-18)

The approved [redesign](../archive/plans/ui-redesign-plan.md) replaces presentation geometry
for Combo and Messages with a shared content/footer layout. Combo retains its
label above the editable history control, raw accepted value, initial selection
and Cancel semantics; initial height fits content instead of a resource-height
spacer. Long prompts wrap without covering the input or actions. Both dialogs
use the project outer/footer spacing with native control size hints. Messages
retains numbered rows, modal result codes, Close default/focus and font-scaled
initial report capacity. Its list grows while the footer remains visible.
This is an intentional layout adaptation; other source-based dialog contracts
remain unchanged. Evidence and checked platforms are in the
[redesign audit](../upstream-port-audit/ui-redesign.md).
No Windows rendered reference; geometry does not establish pixel parity.

Combo's confirmation caption follows the source operation identity: Create for
folder/file creation, Select/Deselect for mask selection and Save for a comment.
Unknown generic uses retain OK. These captions update on language change without
resetting the input or caller-built item title; the native OK role, Return,
Cancel and raw-value acceptance remain unchanged. Rename stays inline in the
file list, following its source scenario.
The original IconID branch loads the exact FM.ico through a Qt ID/resource
service, including native 16/32/48 frames; default negative IDs preserve the
existing icon. Image decoding tests do not verify Windows title-bar rendering.

When extending a view, update its specification and add the relevant geometric
relationship or conditional state to its focused test. Add contracts for other
views as they are changed rather than rewriting all UI tests at once.

Create compression method states follow pinned CompressDialog.cpp/.h/.rc:
Store has empty dictionary/order/solid selectors, with threads still available
according to the original no-method policy. TAR GNU/POSIX has empty numeric and
thread selectors and hides both memory rows. GZip has its single Auto dictionary
and no threads/solid choices. An unrecognized external method retains empty
dictionary/order and unknown memory values; it does not borrow Copy's zero.
Empty selectors remain visible and disabled. Returning to ordinary 7z restores
the choices and memory rows. The two memory QWidget wrappers are presentation
containers, not additional resource groups. `compression_method_state` probes
EN/RU at760×720/640×480, reaches each field by scrolling and writes28 JSON reports.
These establish conditional visibility, row order, text fit and footer geometry;
original rendered Windows appearance remains unverified.

Create validation messages retain the original native call policy: password,
Browse/path and invalid-volume errors use MyMessages' suppressible Critical/OK
box with the 7-Zip caption; memory refusal uses unconditional Critical/OK, and
the small-volume question uses unconditional Yes/No/Cancel. Qt/macOS omits the
message-box caption. `compression_error_policy` checks EN/RU at requested
480×220/320×160 with Qt auto-fit, full label text and reachable buttons. These
standard platform boxes have no separate CompressDialog.rc control group.
Their JSON is geometry evidence; rendered original Windows appearance and native
desktop input remain separate acceptance requirements.
