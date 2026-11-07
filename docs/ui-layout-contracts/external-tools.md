# External tools and temporary resources

## Extraction for mounted access

The mount command applies to the currently opened archive folder and retains its reader
independently of panel navigation. Its contextual setup shows the source and optional
archive password. Check file access in the background before enabling Mount; Cancel
remains responsive during this check. When files need materialization, show the
temporary cache limit and explain that files larger than the limit cannot be
read through the connection. Reuse the prepared reader for the connection.
Cancel starts no mount. Mount management
is modeless: each connection has a shared source identity, status, read-only
details and its own actions. Closing this manager hides it without disconnecting
mounts. Open Folder and Copy Path remain visible but require an available mount
path; the disconnect action remains visible and reflects current availability.
Remove from List is available inline and through the record's context menu only after the
session stops, including a failed mount. It removes that record and its details
without touching source files.
Details include a per-entry log of unsupported names, metadata and conflicts
skipped while preparing this folder; supported entries remain mounted.
Show the source name/icon and retain the supplied path in its tooltip. Keep full
paths available in copyable diagnostic reports. Updates preserve each record's
identity and expanded details.
Closing the owner window offers Unmount and Close or Cancel. A busy unmount keeps
the reader/server alive, leaves the owner open and offers an explicit force option.
Connection actions wrap when needed at compact widths. The body scrolls while
the standard Close footer remains visible. Native control minima keep every
action reachable in EN/RU.
These new commands have no original Windows7zFM resource counterpart.

Before the first extraction for a read-only mount, show the first requested
file's shared source identity and decoded size, with full source/path context in
its tooltip. Explain that consent covers temporary extraction for this
mount, including reads after cache eviction, and that the source remains read-only.
Treat names as plain text. Cancel, Escape, Return on the initial default, closing
the prompt and destroying it decline extraction until remount. Existing cache
reads and direct seekable reads need no prompt.

Unverified recoverable output needs a second explicit choice. Explain the risk
of incomplete data and checksum errors; accepting extraction does not accept
unverified data. Keep the warning visible in mount details when such data is used.

On macOS the confirmation is contextual to the owning window and uses standard
dialog controls. Keep actions visible and separate at normal and compact widths;
long Unicode names must wrap without concealing the decision. The caller must
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

<a id="delete-temporary-files-p1-152"></a>

### Automatic temporary-file cleanup

Sevenzip cleans up inactive application-owned objects in the system temporary
folder when the File Manager starts. Active, externally protected and retained
edit copies remain on disk. While an external editing session is open, it can
offer to save a separate copy of the edited file.

### About

About shows the Sevenzip name, application release, project website and 7-Zip
attribution. The 7-Zip panel shows the upstream version, release date and
copyright. Build information expands in place and labels unavailable values
as unknown. At compact sizes the body scrolls and Close remains reachable.
Light and dark checks cover attribution contrast, layout and language changes.
The package includes the license documents and notices.

See the [identity adaptation](../upstream-port-audit/about-product-identity.md)
for source comparison and validation.

## External editor settings

Reference: the same pinned revision, `EditPage2.rc`, `EditPage.cpp` and
`OptionsDialog.cpp`. Editor presents Viewer and Editor in source order, each
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
