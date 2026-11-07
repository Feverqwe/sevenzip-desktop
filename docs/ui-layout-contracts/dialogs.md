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

Contextual name/mask/comment, Copy/Move, password, memory,
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

## Commands and reports

<a id="folder-history-list"></a>

## Recent places

The address bar opens a short list of recently visited places. Choosing an item
opens it in the pane that owns the address bar. Escape closes the menu without
navigating. Back and Forward keep their own history in each pane.

## Archive picker

Open Archive uses the system file picker. An initial path that needs the
source browser opens it with the same archive filters. Cancel leaves the pane
at its current location. The source browser keeps its path field and actions
visible while its file list scrolls. New Folder opens an inline field. An error
keeps the name available for correction, and Escape returns focus to New Folder.

## Hash results

A single file's checksum appears in its properties. Checksums for multiple
files appear in the shared operation result, with one line for each file.
Diagnostic and checksum text is selectable in the scrollable report. Results
without details show only their summary and closing action.
`hash_results` checks the values and report geometry with a CRC32 fixture.

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
filesystem/volume operations hide it. Pause/Continue and Stop remain reachable
in the footer, and the title bar provides Minimize. Full locations come from the
accepted operation plan, not from the changing current-file string.

Stop and closing a running window ask Continue working / Stop. Continue is the
default and Escape answer. Refusal restores the former pause state. Confirmed
Stop disables repeated requests and shows Stopping until the owner finishes;
completion retires an obsolete question without waiting for user input. Neither
an empty message list nor a full bar proves success. The operation supplies its
result and completeness after required publication/source removal. Full success
closes automatically. Failure, cancellation, skipped items or messages keep a
short result and any diagnostics until Close, with the running bar hidden.
Diagnostics appear directly in a scrollable list when messages are available.
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
folder creation.
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
