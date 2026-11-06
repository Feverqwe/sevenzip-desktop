# External tools and temporary resources

## Extraction for mounted access

The mount command applies to the currently opened archive folder and retains its reader
independently of panel navigation. Its contextual setup shows the source and optional
archive password. Check file access in the background before enabling Mount; Cancel
remains responsive during this check. Show the temporary cache limit and Help/F1
only when files need materialization. Help opens a separate explanation of direct
access, extraction consent, eviction and files exceeding the limit; keep this text
out of the setup form. Reuse the prepared reader for the connection.
Cancel starts no mount. Mount management
is modeless: source/path/status rows, read-only details and errors, Open Folder,
Copy Path and Unmount. Closing this manager hides it without disconnecting mounts.
Remove from List is a context-menu action enabled only for a stopped session,
including a failed mount; it is absent from the dialog button rows.
It removes the selected record and its details without touching source files.
Details include a per-entry log of unsupported names, metadata and conflicts
skipped while preparing this folder; supported entries remain mounted.
Show the source path relative to the current container. Bound the initial source
column width, elide long text and retain the full path in its tooltip. Allow manual
column resizing and preserve the chosen width when rows/statuses refresh.
Closing the owner window offers Unmount and Close or Cancel. A busy unmount keeps
the reader/server alive, leaves the owner open and offers an explicit force option.
Keep file-access and disconnect actions in one row above the standard Close footer.
Compact resize requests respect native control minima so every action remains
visible in EN/RU without wrapping the row.
These new commands have no original Windows7zFM resource counterpart.

Before the first extraction for a read-only mount, show its source, first requested
path and decoded size. Explain that consent covers temporary extraction for this
mount, including reads after cache eviction, and that the source remains read-only.
Treat names as plain text. Cancel, Escape, Return on the initial default, closing
the prompt and destroying it decline extraction until remount. Existing cache
reads and direct seekable reads need no prompt.

Unverified recoverable output needs a second explicit choice. Explain the risk
of incomplete data and checksum errors; accepting extraction does not accept
unverified data. Keep the warning visible in mount details when such data is used.

On macOS the confirmation is contextual to the owning window and uses standard
dialog controls. Keep actions visible and separate at normal and compact widths;
long Unicode paths must wrap without concealing the decision. The caller must
close pending prompts when their mount session stops. Backend cancellation must
release a reader waiting for consent.

`mount_extraction_prompt` checks decisions and writes current widget geometry.
`archive_readonly_view` and `archive_mount_transport` check that denial produces
no decoded data, subsequent files reuse the mount decision and cancellation
releases a pending decision. These are feature checks, not Windows visual-parity evidence.

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

### Native external update publication (P3-05.15)

The following source callback contract applies to standalone panel services.
The redesigned File Manager uses the application-owned session described in
[the workspace audit](../upstream-port-audit/main-window-workspace.md): one explicit
decision covers the captured nested chain, and published outputs invalidate the
physical root in every affected tab. A failed fresh opening retains the previous
readable view and offers Retry; it does not publish a stale nested copy on close.

After the original modified-file Yes prompt, native CopyFromFile retains the
source unconditional RefreshListCtrl policy for writer success/failure/Cancel.
The refresh HRESULT is ignored, including deferred S_FALSE/E_FAIL/E_ABORT;
only actual writer failure produces the source update error. Failed publication
is empty/enabled; Ctrl+R recovers the same folder. Successful publication restores
the pre-update marks/focused name. Final focus returns to the previously focused
peer control after rows, redraw and operation guards finish. Owned editor copies
are deleted on success and retained on failed/cancelled update. Private archive
bytes change only on success; retry and filesystem OnTimer remain usable.
Inspect both panes, EN/RU, table/icon and both selection modes at980×620/640×400,
with2/301 rows. Rendered Windows and current native3OS acceptance remain separate.

### Delete Temporary Files (P1-15.2)

Reference: pinned BrowseDialog2.cpp/rc (dialog93), six Name/MTime/Size/Files/
Folders/Name-2 columns. Controls retain Delete/Refresh, Parent/read-only path,
list, scope information and Help/Close ordering. Owned membership replaces upstream's
7z[E/O/S]+eight-hex naming convention; active or unconfirmed external entries remain visible with an
explanatory tooltip and disabled Delete. The agreed redesign also shows the
selected/total count and the selected protected item's explanation below the
list. This replaces the single disabled filter, which had no selectable
alternative or active filtering handler. Live language changes preserve
selection and update the explanation. Enter navigates, Backspace returns,
Delete confirms eligible deletion, Ctrl+R refreshes, Ctrl+F3/F5/F6 sort by
name/time/size and toggle direction while retaining selection. Plain F5 remains
the Qt Refresh convenience. F1 opens fm/temp.htm, Escape
and Close exit without deletion. Close has initial focus. Focused buttons
activate on Return; no permanent Enter default remains on non-button focus.
Source OnOK only navigates with actual list focus; readonly-path Enter/public
accept is a no-op. Tab order follows the resource, with disabled controls skipped.

The frame uses the shared dialog margins and a separate native footer;
the initial size scales with the font. Buttons and the read-only path respect
native content minima. The scope/count/protection explanation wraps at compact
width; the six-column list scrolls horizontally. Source sample strings still
initialize column widths through native content autosizing.
Source minimize/maximize/resize capabilities are requested without context-help;
native window managers may suppress modal chrome. Initial EN/RU reports and
focus-sensitive Enter policy are checked in `temp_cleanup`.

temp_cleanup writes EN/RU900×580/500×320 temp-cleanup-* and temp-protected-*.json;
footer/path/status remain exposed, including the long protected-item explanation.
Geometry/source structure and a bounded original EN/RU comparison are checked
in the [current browser receipt](../archive/qt-port/upstream-port-audit/p3-06-source-browsers-2026-10-16.md).
Named controls remain exposed at normal/compact sizes; full workflow/pixel parity
is not inferred from those reports.

### Offline Help and About (P1-15.1)

Встроенная Help UI исключена из главного окна по запросу 2026-11-05:
главное Help-menu, Contents и его F1 больше не предлагаются. About остаётся
в Tools с нативной системной ролью. Ниже описаны сохранённые потребители справки
в остальных диалогах; их удаление сюда не входит.

References: pinned HelpUtils/MyLoadMenu/CompressDialog topic calls and exact
official CHM HTML/CSS with [pinned release provenance](../../third_party/7zip-help/help-provenance.json).
QTextBrowser replaces Win32 CHM presentation, retaining pages/links/fragments.
Help follows the application palette, including already open topics. Page,
table and syntax backgrounds, text and links remain readable in light/dark
palettes; theme changes preserve the topic, fragment, history and scroll position.
The page and browser viewport share the Help window background. Table headers,
code surfaces and semantic text accents adapt to that background, including
colored system palettes.
Original HTML and content structure remain unchanged. Tables use a thin shared
grid, a distinct header and more cell padding. Command blocks use a continuous
theme-colored surface and readable monospace text, including links inside code.
Source line breaks and whitespace remain intact; long lines scroll horizontally.
Legacy fixed-width table columns size to their contents, keeping parameter names
and headers intact. Percentage columns retain their source proportions; a narrow
view can scroll horizontally to reach the description column.
`help_source` checks palette transitions and writes
normal/compact `help-theme-*` and `help-syntax-*` widget reports.
help_source writes EN/RU760×560/420×300 help-*.json: browser, Back and standard
Close remain exposed.

About uses the requested independent Sevenzip identity rather than the original
logo and layout. The application name and release precede its description.
The release appears beneath the application heading beside the archive icon.
Project and license actions precede a separate palette-based 7-Zip panel with
source version, upstream release date, original copyright and homepage action.
Build information is a disclosure; unavailable values are explicitly unknown.
Module-load diagnostics appear inline, with the complete report available through
Details. Failed browser handoff keeps the exact address available for copying.
The scrollable body preserves access to content at compact sizes; Copy information
and Close remain in the fixed footer. Close owns initial focus/default Return.
About has no F1/Help action. Both versions remain unchanged across live language
switches. Explanatory text, title and action captions follow the selected language.
Light/dark checks cover rendered attribution contrast and reachable controls.

Licenses opens an independent offline document viewer from the installed package.
Search filters document names and paths without editing their content; an existing
selection is retained when still visible. A missing document or empty result clears
the text and disables copying. The read-only report wraps long lines; Copy text
copies the entire original document, while ordinary selection copy stays available.
Compact windows stack the list above the text. Closing returns focus to About.
The package index describes the supplied documents, not loaded format capabilities.
No checkout fallback, replacement license or automatic download is used.
See the [identity adaptation](../upstream-port-audit/about-product-identity.md)
for the source comparison and current validation scope.
help_ui checks modal About, source Options page callbacks and
Add/Extract button/F1 without applying pending state; compression_advanced checks
original Options Help. Existing Add760/640, Extract600/470 and Options640/520
geometry reports cover the added Help footer. The earlier original-layout About
comparison is retained in the [historical presentation receipt](../archive/qt-port/upstream-port-audit/p3-06-primitives-2026-10-16.md);
it does not validate the new product layout.

## External editor settings

Reference: the same pinned revision, `EditPage2.rc`, `EditPage.cpp` and
`OptionsDialog.cpp`. Editor presents Viewer, Editor, Diff in source order, each
label above a command field and Browse button. The property-sheet footer is
OK/Cancel/Apply; Apply is initially disabled, tracks source modified flags and
resets after applying. Cancel after Apply retains committed values. Browse Cancel
preserves the command; selecting a program replaces it and drops old parameters.
`external_editors_ui` checks unclipped fields/buttons, separated controls and
footer at requested 640×390/520×380 and writes external-editor-options widget JSON.
Width remains exact; height may expand to the six-page sheet's native minimum.
On 2026-10-05 Qt6.12 Windows reports 640×390/520×390; macOS reports
640×390/520×380. All command fields, Browse buttons and footer remain visible
and separated in both states. The test compares the actual size to the requested
size expanded by `minimumSize`, while retaining the clipping assertions.
These reports validate Qt geometry and resource structure; rendered Windows
parity remains unverified.

Archive changed-file prompt reference: the pinned `PanelItemOpen.cpp`
`MyThreadFunction`, `IDS_WANT_UPDATE_MODIFIED_FILE` and Yes/No/Cancel MessageBox.
Qt uses plain text, the relative filename and three source answers. Source No
and Cancel both discard the temporary copy without updating; read-only or failed
update retains it. `archive_external_edit` checks fully visible labels/buttons,
text fit and actual Yes/No/Escape at normal/compact owner sizes 980×620/640×400,
writing `external-update-prompt-*` JSON. Qt chooses message-box sizing; these
reports establish usable geometry, not rendered Windows pixel parity.

## VerCtrl commands and Revert

VerCtrl reference: pinned `VerCtrl.cpp`, `MyLoadMenu.cpp` dynamic Zvc IDs and
`OverwriteDialog.rc/.cpp`. Commands append after the File menu's resource items
and use original names/mnemonics; Diff and `FM/7vc` must be configured. Readonly
files show Ver Edit; writable files show Commit/Revert/Diff. Revert changes
OverwriteDialog's default to No and hides Yes All/No All/Auto Rename. `verctrl`
checks actual Yes/No/Escape, default/visibility and unclipped geometry at
700×440/560×440 and writes current-build dialog and owner-window JSON.
Rendered Windows parity remains unverified.
