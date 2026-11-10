# Archive opening and extraction

## Direct device selection

File → Open device opens an independent chooser without replacing ordinary
volumes or archive entry points. It lists external whole devices with their
sizes and availability; busy devices cannot be opened. Discovery, authorization
and partition inspection keep the dialog responsive. Cancel publishes no source,
and a denied request can be retried. Refresh updates discovery; Back returns
from partition selection to devices. A single partition opens directly.

The device and partition lists, explanatory/error text and standard Open/Cancel
footer remain accessible in normal and compact EN/RU layouts. Long names and
partition types retain usable list columns. Return opens a valid selection;
Escape cancels. The file manager keeps a visible Read only indicator while
viewing the source, disables mutation commands and uses its existing panel,
copy, refresh and Parent interactions.

A readable partition with unfinished recovery or read-only-compatible limitations
offers an explicit limited opening. The warning lists the conditions and explains
potentially missing, outdated or unreadable files; Cancel is the default and
Escape cancels. Acceptance applies only to that opening. A persistent status
indicator distinguishes this mode, including after refresh and while browsing
within the source. The choice permits no journal replay, orphan cleanup or repair
and cannot bypass missing decoding support. The warning and indicator fit the
EN/RU layouts.

Filesystem images with reader-reported state limitations use the same explicit
choice before their folders are shown. Their limited-access indicator remains
visible during copying and navigation. Relisting the current reader retains the
chosen mode; a later opening asks again. Source data is never repaired or changed.
When NTFS reparse metadata cannot establish an entry's type, the warning explains
that readable underlying DATA streams are exposed. Such entries identify this
mode in their properties; unreadable known links remain file errors.

For a priority archive that the official reader opens with damage/incompleteness
diagnostics, ordinary opening asks whether to continue read-only. The alert
explains missing/unreadable files, partial or unverified extracted data and an
unchanged source. It includes the reader diagnostics, explicit Open read-only
and Cancel actions; Return defaults to Cancel and Escape cancels. Acceptance
belongs to the selected opening. The existing limited-access badge remains
visible in normal/compact EN/RU layouts. File errors remain visible during
copying or extracting readable entries. See the
[policy audit](../upstream-port-audit/read-only-access.md) for checked scope.

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

## Redesign: Extract form

The [approved redesign](../archive/plans/ui-redesign-plan.md) replaces the fixed columns with
one form: destination/history and browse, optional subfolder name, optional
password, then Advanced settings. Overwrite mode, path mode and root-folder duplication
remain effective when Advanced settings is collapsed. A short Modified indicator
identifies nondefault hidden options; expanded fields have no value summary.
Windows security is shown only on Windows.
The form body scrolls at compact heights while Extract/Cancel remain visible.
An invalid destination keeps the form open and focuses the destination or
subfolder field that needs correction. If the extraction policy disallows a
link in the destination path, the dialog shows the error before extraction.
For a concrete destination, it also reports a known lack of write or directory
search access in the destination or in the folder where it would be created.
Spaces in the name of an existing destination or subfolder are preserved.
If the destination field cannot be resolved or the path is blocked by a file,
Browse starts from the original folder and preserves the separate subfolder field.
After choosing a folder, any error in the resulting destination appears before
the user confirms extraction.
Choosing a history entry restores the saved destination, including its own
subfolder when the split fields are enabled. If the fields are disabled,
choosing a history entry discards the previous subfolder draft.
New relative destinations are saved as resolved paths so they can be reused
from a different working directory.
The history can retain separately addressable folders whose names differ only
in case.
The caller supplies the actual source archive names. One archive shows its name;
multiple archives show a translated count and an optional read-only list. Full
paths remain available in the source tooltip and accessibility description.
The scope distinguishes entire contents from existing native filters, including
file/directory exclusion. Showing or translating these values does not change
the request. Extraction from inside an open archive retains the existing Copy
route and its selected-item presentation.

A supplied password enables/reveals its section. Disabling the section excludes
the password from the accepted request and retains its draft for re-enabling.
Changing sections or browsing does not recreate controls or lose pending values.
Source modes/force flags, history normalization, metadata bool-pair precedence,
confirmation and Cancel persistence are unchanged. The primary action is Extract;
the native button box preserves its roles and live translations.
[Source scope and checks](../upstream-port-audit/ui-redesign.md).

### Rendered Add/Extract reference (P3-06, 2026-10-15)

Add's compact form reserves a separate gutter for its non-transient scroll bar;
it must not cover fields or fade while scrolling is required. Native/translated
minimum widths include that gutter. The bar reaches the right window edge;
the source right margin separates it from the form, and footer buttons retain
their right inset. `source_compress_dialog::scrollBarKeepsItsOwnSpace`
checks EN/RU native and simulated overlay styles, compact top/bottom states and
expansion without scrolling. Cocoa640-wide requests may grow to651 for Russian
labels; the footer stays exposed and Parameters remains reachable.
[Correction and checks](../archive/qt-port/upstream-port-audit/p3-06-add-scroll-2026-10-15.md).

Rendered Add/Extract reference is separately available for original EN/RU initial
states. Its fixed desktop geometry is not a compact resize contract; Qt keeps
standard platform metrics and exposes compact Add content through scrolling with
a visible footer. Current native Add/Extract consumers supply12 JSON per OS;
Mac Extract may expand470-wide requests to554×343. [Reference/scope](../archive/qt-port/upstream-port-audit/p3-03-2026-10-15.md#p3-03p3-06--rendered-addextract-and-native-layout-consumers-2026-10-15).

### Changed nested archive Parent (P3-05.15)

Parent uses the source modified-archive Yes/No/Cancel question. No and Cancel
both exit and delete the spool without changing the outer archive. Yes updates
the original outer item; writer failure/Cancel retains the modified spool and
reports the source update error, but still returns to the outer folder.
Parent ignores its final Refresh HRESULT. Failed publication leaves an empty,
enabled list; Ctrl+R recovers that same outer provider. Check native2/301-row
outer archives with disk-extracted children, both panes, EN/RU, table/icon and selection modes using representative
cases at980×620/640×400. Peer bytes/provider and subsequent filesystem OnTimer
must survive. These checks do not establish rendered Windows parity.

### Native nested opening (P3-05.15)

OpenItemInArchive keeps original preferred provider stream, bounded memory
extraction and disk extraction paths. Native provider streams use original Agent
index mapping and keep the module/resource alive while read or seek is possible.
All successful opens ignore the subsequent Refresh result as upstream does;
S_FALSE/E_FAIL/E_ABORT during child row publication must not add a dialog.
Failed publication is empty/enabled; Ctrl+R restores the same child provider.
Parent restores the original outer provider/focused child name and removes its
owned spool. Source timer/notify/redraw remain usable, including filesystem
navigation and OnTimer afterwards. Check2/301 child rows, both panes, EN/RU,
table/icon and standard/alternative selection at980×620/640×400. Inspect failed
and recovered address/list groups; original archives and peer bytes stay intact.
Native open cancellation must retain the outer provider/rows and release owned
temporary files without a stale refresh or extra error. Rendered Windows and
native Windows/Linux execution remain separate acceptance requirements.

### Open archive picker (P2-01)

File → Open archive… opens the native OS file picker. Its shortcut is Cmd+O
on macOS and Ctrl+O on Windows/Linux. This user-requested extension supplements
source Open/Open Inside and address navigation; the original IDM_MENU has no
independent archive picker command.

The picker starts in the current filesystem folder or the open archive's
containing folder. Archive masks come from native handler metadata, followed by
All files; without metadata only All files is offered. Cancel preserves the
current location. Accepted files use the existing archive opening, password and
error workflow. The command is unavailable during blocking archive operations
and modal dialogs. EN/RU normal/compact File-menu geometry is covered by
`shell_menu_window`, along with Unicode/spaces selection, cancel and invalid-file
handling. `browse_dialog` and `codec_catalog` cover reusable filters. Native OS
panels and physical shortcuts require separate platform acceptance.

For long/super paths the original custom Browse fallback retains its top
Parent/Create/folder row, three-column list, path edit, conditional filter and
OK/Cancel footer. Its source initial client is272×336DLU, with8DLU margins/gaps,
exact24DLU top-button widths with16DLU minimum height and64×16DLU footer
buttons with native caption protection. Path/filter heights have14DLU minimums.
Hidden FolderMode filter retains its vacant row; Cocoa uses its actual widget
rectangle to preserve that source space. It requests source minimize/maximize/
resize without context-help. Source demonstration rows initialize column widths
through content autosizing, including the native icon slot.
`source_browse_dialog` checks the initial EN/RU frame and visible controls;
existing normal/compact navigation/filter/create/accept/Cancel tests remain.
Parent/Create activate on Return/keypad Enter; Create cancellation leaves the
filesystem unchanged. Initial focus remains the path edit. Bounded original
EN/RU and current native results are in the
[browser receipt](../archive/qt-port/upstream-port-audit/p3-06-source-browsers-2026-10-16.md).

### Source extraction prompts (P0-04.1 prerequisite)

`extract_prompt` checks OverwriteDialog at 700×440 and 560×440. When the
extraction memory limit is exceeded, the operation reports the required and
allowed memory. The user can change the limit in Settings and retry.

The agreed conflict form offers Replace, Skip and Keep both, with one unchecked
Apply to remaining conflicts in this operation checkbox. A known single-file
transfer hides that checkbox; one selected root directory does not establish a
single conflict. Cancel remains on the left of the footer and always cancels,
including when the checkbox is checked. Initial focus/default is Skip. Keep both without the
checkbox changes only the current conflict; the existing native auto-name algorithm
chooses an available name. Operation policy must not leak into another operation.

Existing/incoming metadata stays in separate areas, side by side when readable
and stacked at narrower widths. Full filenames and locations wrap in both forms;
unknown size/time is explicit. Known byte sizes and timestamps retain source
formatting. The three decisions sit on the right of the same footer. The body
scrolls independently of the footer. Native minimum button sizes remain fitted.
The restricted Revert caller retains its original Yes/No/Cancel result contract,
source default preference; Keep both and the scope
checkbox are unavailable there. Existing native result values are preserved;
the one-conflict rename answer is a separate project extension.
`overwrite_layout` covers EN/RU, normal/compact widths, extra-action availability
and reinitialization. Its initial-frame case checks readable controls at
8pt and 12pt fonts. Responsive Qt layouts and
native font metrics are deliberate adaptations of the fixed Win32 dialog;
they do not imply identical pixel dimensions. Rendered reference evidence is
recorded in the [P3-06 audit](../archive/qt-port/upstream-port-audit/p3-06-2026-10-15.md).
Qt supplies one file icon; the optional second Windows EXE/ICO icon is omitted
explicitly in the contract. Widths must fit labels and footer buttons without
clipping. Default and native result IDs are checked behaviorally.

Memory sizes its initial client to visible content, using the shared dialog
margins/footer. The message is top-aligned; message and Action follow their content.
The limit edit/spinner pair occupies50DLU with native minimum protection,
with both arrow buttons fitting within the edit height,
indented below its controlling checkbox; GB follows the pair. Continue/Cancel
use native caption metrics. The source has no minimize/
maximize/context-help title-bar buttons.
Memory order is requirement/limit/RAM message, Allow/Skip action group,
change-limit checkbox, independent edit/spinner and GB label, Remember,
then Continue/Cancel. Source RAM-dependent default and spinner range,
text validation, NeedSave/Remember/SkipArc and cancellation are preserved.
The text field remains independent of the spinner range, including source
acceptance of zero; invalid text does not close the dialog.
Invalid-limit errors use source MyMessages' Critical/OK and 7-Zip title, with
user-question suppression; macOS hides QMessageBox titles. The pending dialog
and raw text remain available for correction in either policy state.
Qt spinner buttons adapt the original edit/up-down pair, preserving
the original `UDS_ALIGNRIGHT` paired width and `UDS_ARROWKEYS` keyboard path:
enabled buddy edit ↑/↓ invokes the same step operation as the buttons; disabled
input is unchanged. Up/down buttons are not independent tab stops. The raw text
parser and source range policy remain separate. Native button-box optical
margins may shift its backing widget by one pixel; source footer gaps are
asserted through public layout-item geometry, with actual control visibility
checked separately. This corresponds to Qt's
[QWidgetItem rectangle conversion](https://github.com/qt/qtbase/blob/6.12/src/widgets/kernel/qlayoutitem.cpp#L456).
These checks establish
structure and local Qt usability. Initial focus is Change allowed limit, as in
the first source tab stop. Native PanelOpen integration is checked in P0-04.1.
The bounded EN/RU original/current rendered comparison is recorded in the
[source-prompts receipt](../archive/qt-port/upstream-port-audit/p3-06-source-prompts-2026-10-16.md);
whole workflow and pixel equality are not established by those initial views.

```sh
./scripts/build.sh dev ui_geometry_tests
./scripts/ctest.sh --preset dev -R '^ui_geometry$' --output-on-failure
```

CTest writes reports to `ui-reports/` in the selected CMake build directory
(see [build workspace lookup](../development.md#local-commands)). `tests/ui/support/layout_inspection.h`
also supports other Qt tests through `UiInspection::writeReport(widget, tag)`.
`SEVENZIP_UI_REPORT_DIR` overrides the output directory; standalone test runs
default to `ui-reports/` in the current directory. `ui_layout`, `extract_dialog`,
and `compression_preferences` now write JSON instead of mandatory PNGs.
Older screenshot-producing tests have not all been migrated.

Each JSON object contains class, object name, visible/enabled state, bounds in
root coordinates (`[x,y,width,height]`), exposed bounds after ancestor clipping,
size hint, and nested children in Qt object order. Static labels, button text,
group titles, and noneditable combo selections are included. Editable values
are omitted, including passwords in show-password mode. Static labels can still
contain generated paths; reports use disposable fixtures and are local artifacts.
Object-tree order is not tab order: use geometry for spatial relationships.

`fullyVisible` checks ancestor clipping, not occlusion by another sibling or
window. `separate` checks specified pairs; container/child overlap is intentional.
`labelFits` uses Qt size hints/height-for-width, not pixel analysis. Scrolled
content can be clipped until brought into view. Avoid blanket overlap or
visibility assertions on the entire widget tree.

For an unresolved rendering question:

```sh
SEVENZIP_CAPTURE_SCREENSHOTS=1 ctest --preset dev -R '^ui_geometry$' --output-on-failure
```

This writes optional PNGs next to the JSON. Open only the affected screenshot.

## Extract dialog

Reference: upstream commit `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/GUI/ExtractDialog.rc`, desktop `IDD_EXTRACT`.
Implemented view: `src/dialogs/extract/ExtractDialog.cpp`.

- Destination label and editable history occupy the top; Browse is on the right.
- Below the destination, the left column contains the optional folder-name
  row, path mode, root-duplication checkbox, and overwrite mode in that order.
  The password group is right of the modes, with Password and Show Password.
  Restore file security is below it; Unix disables it with a platform reason.
- The folder-name field stays visible when its checkbox is cleared, but becomes
  disabled and does not contribute to the destination. Toggling preserves its
  pending text and the positions of the other controls.
- OK, Cancel and Help stay visible,
  distinct, and inside the window at requested sizes 640×360 and 470×330;
  Qt may expand to its style-dependent minimum size. Static labels fit.

P2-07 source_extract_advanced additionally checks EN/RU700×390/600×340 exposed
controls, checkbox width and source grouping/order. Four JSON reports reviewed
2026-10-03: compact modes are286 pixels wide, Russian ElimDup requires285;
inspected bounds equal exposedBounds. Rendered Windows parity is unverified.

The source caller supplies the full single-archive path in the window caption;
multiple archives use the plain Extract caption. Language changes preserve
that path and pending input. Eliminate duplication remains enabled independently
of the caller. The source resource has no destination hint.

`dialog_path_rules` adds EN/RU × split on/off Create/Extract Cancel/confirm
reports at 700×390/600×340 (Extract) and 760×720/640×480 (Create).
It asserts complete destination, mode, password, duplication and visible
folder-name fields, archive-name input and footer exposure. Source grouping and
conditional folder-name availability remain as above. Editable values are checked
by the workflow, not inferred from redacted JSON; native separators and NBSP
preservation change values without introducing controls or captions.

Current 2026-10-07 toolbar, language and advanced Extract/Copy reports were
reviewed on macOS, native Windows Qt and Linux. The mode/password columns,
checkbox text and footer remain exposed at normal/compact sizes. Requested
Russian470×330 expands to the style-dependent minimum (macOS525, Windows483
pixels wide). The standalone Windows GUI consumer also checks public UIA
exposure/enabled states and source caption. This does not establish original
Win32 pixel parity; [the audit](../archive/qt-port/upstream-port-audit.md#p3-05--toolbar-extracttest-source-consumers-2026-10-07)
owns the platform evidence.

Adaptations: the pinned desktop resource and Qt use combo boxes for modes.
Help uses the source topic. The blank folder-name checkbox uses an accessible
tooltip; Qt layouts and platform button ordering replace Win32 positioning.
See `docs/7zfm-parity.md` for
behavioral limitations. The tests check these relationships rather than hardcoded
pixel coordinates or native button ordering.

## Filesystem encryption keys

The fscrypt prompt is a local Qt presentation adaptation for the existing ext
reader; upstream 7zFM's generic password resource has no filesystem-key form.
Ordinary encrypted archives keep the password dialog described below.

The key dialog explains raw master keys and the no-key view. Each key has a
version selector and masked hexadecimal master-key field; v1 also shows its
descriptor. Add/remove supports multiple keys in a scrollable area; adding a
key focuses and reveals its editor. Show keys changes visibility without
changing the input. Open is available only for valid hexadecimal entries and
unique v1 descriptors. Field labels, explanatory and error text wrap in EN/RU;
the native footer provides Open, Cancel and Browse without keys.

Open returns the keys for the current opening. Browse without keys explicitly
selects the reader's no-key view, even if the draft contains invalid input.
Cancel/Escape discards the draft. Ordinary archive passwords do not select this
dialog; images and direct devices use the same typed fscrypt request. Refresh
retains the accepted choice. Editable key values remain absent from widget JSON.

## Archive password prompt

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/PasswordDialog.cpp/.h/.rc` (`IDD_PASSWORD`).
`CPasswordDialog` shows a prompt, password edit, Show Password, then Continue/Cancel.
The redesign uses shared dialog margins/footer, a content-sized initial height
and native control/button metrics and ordering. The source has no minimize,
maximize or context-help title-bar buttons. Compact Qt resize remains an
accessibility adaptation of the fixed original form.
Initial focus is the password edit. Initial Password/ShowPassword are applied before display. Toggling copies the
current edit into public state, then changes echo mode without changing its text,
as in ReadControls/SetTextSpec. OK/Enter commit raw text; the caller discards a
rejected dialog and keeps the existing backend cancellation policy.

`password_dialog` checks masked/shown states at 360×170 and 260×170, control
order, label fitting, footer visibility and separation. `ui_parity_password`
maps all five resource controls to these current-build reports. Behavioral tests
cover initial values, Unicode/spaces, show/hide, OK/Enter and Cancel. Editable
password values stay out of reports. Russian captions and Qt button ordering
are documented adaptations. Windows rendering remains unverified.

`password_window` adds current-build 360×170/260×170 reports for accepted
Unicode, surrounding spaces and empty text during source archive opening.
It also checks the subsequent asynchronous process prompt, raw-value reuse,
Cancel/reopen and reset on another archive. The process prompt identifies the
archive by its displayed name and path. `open_callback`/`update_callback`
cover empty and supplementary Unicode through the portable UString/BSTR boundary.
These add caller coverage; the resource layout and labels are unchanged.
