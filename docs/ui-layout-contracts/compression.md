# Archive creation and compression

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

## Redesign: Add form

The agreed November 5 catalog supersedes the older two-column and four-section
presentation requirements below for the Add form. Archive name/destination remain
visible above the scrolling form as separate editable rows with leading labels.
Destination edits use the existing source path resolver. Browse selects a folder
through the existing folder picker, retaining the name and format; history can
still restore the source folder/name/format split.
When the name field contains a full path, choosing a folder moves the archive
name to that folder. Cancelling preserves the full path as entered.
If a file blocks the destination folder, the form stays open so the user can
correct the path and keep the archive options.
Format is immediately available. Password protection has
an explicit enable control; its dependent fields appear together, without a
large framed heading. A source-supplied password enables the section. Turning
protection off excludes the password/headers from the effective request while
retaining the draft for re-enabling. Format support still comes from the source.
Changing a protected draft to an unsupported format keeps the protection choice
visible and blocks Create with an inline explanation. Choosing a supported format
restores the password draft; explicitly turning protection off permits an
unprotected request. Cancel does not save that decision. A single encryption
method does not occupy a selectable row; formats with multiple methods retain
their source-provided choice.

One Additional section contains compression level, volumes, a read-only list of
selected sources, Expert options and the Metadata and time dialog. It starts
collapsed. Its summary exposes
the selected level/volumes and marks changed expert options; collapsing does not
reset them. An invalid volume specification reveals the field.

The separate Expert editor groups sources, method/resources, files, metadata/time
and method properties in disclosures, with a fixed Apply/Cancel footer. Sources
and metadata start collapsed. Metadata uses the same source-backed controls as
the standalone format-options window. Switching method refreshes its capabilities
while retaining pending overrides. Fields with no applicable source
choices are hidden. Apply changes only the parent draft: it neither creates an
archive nor writes settings. Cancel, Escape and closing the editor restore the
prior choices, including Auto versus explicit values, raw parameter text and any
archive name and pending inline metadata. Only confirmation of
the main form publishes its request and settings. Invalid properties or memory limits lead to
their actual editor fields instead of leaving an error behind hidden controls.

The compact form keeps a separate scrollbar gutter and a visible native footer.
Short settings use aligned label/field rows and wrap when needed; long parameter
input keeps its label above it. Fields may reflow after a language/value change;
fixed column rectangles are no longer a contract. Unsupported Windows shared-file
controls are hidden on POSIX. Validation, saved format profiles
and official-engine operations retain their source owners.
The primary action is named Create archive, with live localization. Evidence:
[redesign audit](../upstream-port-audit/ui-redesign.md).

ZIP additionally offers ZSTD compression while retaining Deflate as the default.
ZSTD has no dictionary or word-size controls; existing levels, per-file parallelism
and encryption remain available. Other formats retain their method choices.
This explicit extension and its validation belong to the [ZIP audit](../upstream-port-audit/zip-zstd.md).

<a id="add-metadata-options-p2-07-in-progress"></a>

### Add metadata Options (P2-07)

Reference: pinned CompressOptionsDialog.rc NTFS group and COptionsDialog
OnInit/OnOK. Symbolic links precede hard links, alternate streams and security;
original handler flags control visibility. The redesign places real format/method
information first, then lightweight links/metadata and time groups, with the shared
dialog margins and Save/Cancel footer. Unix hides NTFS streams/security and
the metadata group disappears when unsupported. Initial size follows visible
content; resizing keeps the header and fields at the top.
source_link_storage requests EN/RU540×480/480×430 reports for actual TAR options;
native font/control minima may enlarge those requests, with links and footer
remaining fully exposed.
Normal and compact TAR states must expose both link controls and the footer,
with translated captions fitting their controls. This establishes
structure/usability, not rendered Windows pixel parity.
Explicit-value checkboxes retain separate Set/default and stored-value state.
Set time precision is a complete caption above its dependent label/combo; its
label wraps only when needed. Other Set captions stay aligned with their
indicators even in tall rows. Metadata controls keep source order, without
reserving fixed heights for unavailable rows. Cancel preserves the Add draft;
Save commits only the existing nested transaction, not an archive operation.
source_metadata_storage additionally checks WIM security/ADS and all visible
time captions, Options/Add Cancel/OK/restart, and writes four Windows
`source-metadata-wim-{language}-{width}` reports. Native font/control minima may
enlarge the requested normal or compact size for WIM as well as TAR. Supported
metadata controls, translated time captions and the footer must remain fully
exposed without clipping. Format support, precision choices and timestamp
transitions remain owned by the source rather than a second UI capability table.

The [current About contract](external-tools.md#about) covers the product identity
and build information.

For resource-derived comparison and reviewed C++ states, see
[upstream UI references](../ui-reference.md) and `$sz-ui-parity`.
`ui_parity_copy` and `ui_parity_password` check fresh reports against pinned
resource baselines.

### Add archive naming metadata (P2-01)

Reference: pinned CompressDialog.cpp/rc and UpdateGUI.cpp::ShowDialog.
The Archive folder prefix and editable name/browse share a left edge to the right
of the Archive caption; the caption aligns with the editable name.
The destination stays visible above the scrolling options. Long folder prefixes
elide in the middle, with the complete path in a tooltip. Display separators are
native and do not change the underlying path. The initial filename is selected
for editing; Tab moves from the name to Browse.

Format, compression, memory, volumes, parameters and the Options summary occupy
the left column; Update mode, Path mode, Options and Encryption occupy the right.
There is no Compression group box or embedded source picker. The panel selection
supplies sources through the original Add workflow; archive updates use the
original two-panel Copy workflow. The footer uses OK/Cancel.

At a fixed window size, changing compression settings preserves both column
boundaries and the positions of other controls. Unsupported memory/header
options reserve their space. Shared compression combos retain their alignment;
decompression memory uses a full-column row, and usage/limit/RAM text stays
readable. Manual resizing and language/font changes may adapt the layout.

Normal and compact EN/RU states must expose complete control rectangles, with
vertical scrolling for lower controls and no horizontal overflow. Source buttons
and checkboxes remain reachable by Tab; the fixed footer stays visible. Initial
sizing must keep the footer on the available screen. Scroll-state checks establish
reachability, rather than requiring all controls to be visible at once.

`source_compress_dialog`, `compression_path`, `shell_update_window`,
`codec_commands_ui` and the shared geometry/language suites cover these relations
and their direct consumers. Native styles and FieldsStayAtSizeHint are covered;
report counts and historical timings are not acceptance criteria.
See the [native Add receipt](../archive/qt-port/upstream-port-audit/p3-03-2026-10-15.md#p3-03p3-06--native-add-geometry-tab-traversal-and-display-fixtures-2026-10-15)
for the reviewed native geometry and limits. Earlier source comparisons,
application captures, hashes and intermediate failures remain in the
[layout investigation history](../archive/add-dialog-layout-history.md).
A geometry pass alone does not establish rendered Windows parity.

### Advanced compression options (P1-07.1)

Reference: pinned `GUI/CompressOptionsDialog.rc`, `CompressDialog.rc` parameter
and Options rows, and `CompressDialog.cpp::COptionsDialog`. Add shows Parameters
after volumes, then an Options button with the native summary. At 760×720 and
640×480 the form scrolls to these controls while the confirmation footer remains
visible. `compression_advanced` writes `archive-advanced-760/640.json`.

Options sizes its initial client to visible content, protected by native control
minimums. It keeps resource grouping/order: optional NTFS controls, format time info,
Time group with explicit/default precision and modification/creation/access/time
of archive rows, preserve-access checkbox, then OK/Cancel. Explicit/default
checkboxes use the local Set/Задать caption and descriptive accessible names;
their tooltip explains the override of the format default.
Native capability policy hides unsupported NTFS
controls; ZIP UNIX/DOS precision hides creation/access rows. Archive mtime is
enabled. Preserve access is enabled on Windows and hidden on
POSIX.

The NTFS group fits its visible rows with native control protection.
Hidden NTFS groups and unsupported time rows leave no reserved space.
Extra height after manual resizing remains below the groups. Time rows retain source
order/spacing with native minimum protection for wrapped captions. Cocoa gives
the NTFS slot one extra logical pixel for its native group backing and uses
control-dependent horizontal spacing to separate the combo backing from its label.
The format caption identifies the archive format. Hidden time rows reserve no
empty height, and the footer uses native button sizing. Review captures show
content-sized normal/compact forms after checking the requested widths.
Normal/compact requested sizes expand to actual native minima; text and controls
must remain exposed, rather than fit an impossible fixed-height request.

`compress_options_presentation` writes14 current reports:7z/ZIP/WIM × EN/RU ×
initial/compact, plus ZIP Unix precision. Initial focus and first Tab match the
actual original runtime:CTime→ATime explicit, Precision→CTime explicit, and
symbolic→hard links respectively. Backtab reverses that transition. Space/Down,
Cancel/reopen and Return→OK retain the source default/explicit and commit rules.
The existing advanced, timestamp and language suites cover persistence and live
language changes. Hidden NTFS controls do not force a compact minimum.

[The current receipt](../archive/qt-port/upstream-port-audit/p3-06-compression-options-2026-10-17.md)
maps all22 controls against eight actual original states and native reports on
three OS. Raw Windows runtime comparison passes8/8. POSIX comparisons retain
the expected Windows-only preserve-access/ADS/security state differences; they
are not exact Windows-state passes. No pixel or physical Mac-input claim.

Timestamp entries follow source SetPrec, including no selected item when the
handler's default precision is absent from its supported mask. Inserting entries
must not select the first item. With no selection the logical value is CB_ERR
(all-ones Auto), not zero. `compression_timestamp` covers eight capability/profile
states in EN/RU, explicit keyboard selection, OK/Cancel and restart. Its64
`timestamp-*.json` reports use520×400/440×340 and check precision reachability;
existing advanced-option probes cover the complete group order and footer.

### Compression lifecycle and memory rejection (P1-07.2)

Graphical compression leaves source files in place. Supported self-extracting
archives can still be opened and extracted. The expert editor offers shared
writing on Windows.

`compression_advanced` checks the expert form at 760×720 and 640×480.
The compact form scrolls while keeping its footer reachable.
`compression_limit` writes memory-limit-760/640.json after exposing the native
memory combo. Both sizes fully expose it. A memory rejection leaves Add open for
retry, while Cancel leaves INI unchanged. Reports verify macOS Qt usability and
resource order; decoder memory acceptance is separate from dialog geometry.

### Archive volume creation (P1-06.1)

Reference: pinned `GUI/CompressDialog.rc::IDC_COMPRESS_VOLUME` and source
`CompressDialog.cpp::OnOK`, `SplitUtils.cpp::AddVolumeItems`. The editable
«Разбить на тома, байт» combo follows compression controls, has nine source
presets and starts empty. Existing-target mode disables it. Invalid input keeps
the dialog open; final sizes below 100 KiB require Yes/No/Cancel before saving.
`archive_volume_creation` checks these states and scroll reachability at
760×720/640×480, writing `archive-volumes-760.json` and `archive-volumes-640.json`.
Current reports show the label/combo fully exposed with the footer below the
scroll area; compact layout uses scrolling. Windows rendering is unverified.

### Existing archive Add options (P1-05.1)

Reference: `CompressDialog.rc` update/path rows and `CompressDialog.cpp`
initialization at upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.
The obsolete separate existing-target form and its source picker have been
removed. Add uses the same source form with editable archive/format/browse,
volumes and encryption; source UpdateArchive owns existing-output eligibility.
`compression` checks the mode list, normal
760×720 and compact 640×480 footer/scroll reachability, then writes
`update-options-normal.json` and `update-options-compact.json`.
Current path/password checks are covered by the source modal and native
two-panel Copy consumers. These reports verify structure and clipping on the
checked host; a passing geometry check alone does not verify Windows rendering.

The main-window caption is native CApp::RefreshTitle's focused folder prefix,
with `7-Zip` for an empty prefix. Toolbar option checks mirror native CApp flags;
existing grouping and action order stay in the main-window normal/compact reports.
Command availability comes from native menu policy plus transport restrictions.
Screen-size menu filtering uses available monitor geometry and Qt system-font
units, independently of the main window's compact resize fixture.

## Create archive dialog

P2-01 creation states: native eligible formats are 7z/ZIP/gzip/bzip2/xz/tar/WIM;
the one-file KeepName constraint comes from source UpdateGUI. References for
enabled states are CompressDialog.cpp::FormatChanged (Encrypt_/EncryptFileNames_),
SetMethod2 (TAR GNU/POSIX remains selectable at level0), and SetSolidBlockSize2
(source Solid_ flag includes xz). Disabled encryption remains in the right
column; Encrypt file names is hidden outside 7z. `codec_creation_ui` produces
`{type}-creation-top-{760,640}.json` and `{type}-creation-{760,640}.json` at the
top and encryption scroll positions. All seven types expose their relevant
controls by scrolling and retain the footer. These are Qt structure/state
checks; platform and rendered-reference acceptance is recorded separately in the
[UX audit](../ux-port-audit.md).

Reference: the same upstream commit, `CPP/7zip/UI/GUI/CompressDialog.rc`,
`IDD_COMPRESS`. Implemented view: `src/createarchivedialog*.cpp`.

- The editable archive-path combo and format precede the options.
  The combo retains the extension and offers the last twenty destinations;
  its text and the format row do not overlap at either tested size.
- New and existing targets both expose Update mode above Path mode in the
  right column; Path mode offers Relative, Full and Absolute. Source
  CompressDialog.cpp::OnInit attaches/populates both combos without testing
  target existence, matching CompressDialog.rc and the current Qt constructor.
  The Qt form offers Add and replace, Update and add, and Freshen existing.
  Synchronization remains available to native archive operations.
  The new-target native Linux state is recorded in the
  [native receipt](../archive/qt-port/upstream-port-audit.md#p3-03p3-04--native-linux-package-workflow-and-desktop-recovery-2026-10-14).
- Compression controls retain level, method, dictionary, word size, solid block,
  CPU threads, and memory controls. Encryption retains the upstream order:
  password, confirmation, Show Password, method, Encrypt file names.
  Show Password hides confirmation and its label; headers are hidden for ZIP.
  Disabled password protection retains its draft but excludes it from validation
  and effective archive options, including after changing to a format without encryption.
  Compression stays in the left column and encryption in the right at both sizes.
  Password and confirmation labels sit above their fields, as in the resource.
  The encryption-method label/combo share one row; decompression memory is
  right-aligned. No source picker, Save in or preview is present. Long archive
  history does not widen the form.
- Options are scrollable. At 760×720 and 640×480, format, compression controls,
  password fields must each be fully reachable by scrolling.
- The OK/Cancel footer remains outside the scrolling
  area. Footer buttons remain visible and do not overlap in both 7z and ZIP.

Adaptations: the two columns remain in a scroll area; compression
rows wrap at compact widths rather than removing the second column. Unsupported
upstream options are still absent; this is a partial resource adaptation.
`ui_geometry` covers column ordering, absence of a source picker, reachability
and the footer; existing compression tests
cover method/format-dependent enabled states and persistence.
Its scroll probe uses each whole control rectangle with `ensureVisible`, since
Qt's `ensureWidgetVisible` can accept an exposed input-method cursor while a
field border remains outside the viewport. Initial source focus stays unchanged;
full-control visibility, ordering and footer assertions remain strict.
Saved method-dependent choices follow source SetDictionary2/SetOrder2/
SetSolidBlockSize2: nearest admissible entries, first-entry fallback for a value
below the minimum, large dictionary threshold/clamp and source BZip2 decimal
comparison. Solid below the first numeric entry stays Auto for 7z; full/non-solid
remain explicit. An unavailable explicit thread count leaves no selected entry,
as source SetNumThreads2 does. `compression_profile_selection` checks EN/RU
760×720/640×480 reachability and writes `profile-selection-*.json`; values and
Cancel/restart are asserted through public controls/options, outside JSON.
Changing the XZ block size refreshes the automatic thread count and memory
estimate together. It preserves the current thread selection, including an
unavailable saved selection. The dialog checks the refreshed estimate on OK.
`compression_event` covers EN/RU XZ state transitions, refusal/no-save,
restart and official-engine workflows, with reachable thread/memory controls at
760×720/640×480 in `compression-event-*.json`. Auto remains an all-ones operation
spec; the official engine owns the actual compression thread calculation.
The format combo follows the resource's CBS_SORT presentation order while its
selection follows source OnInit: explicit handler, then saved format, otherwise
the first native candidate before sorting. Browse filter indices refer to that
same displayed order. Hash method selection refreshes the source filename suffix
(SHA1 or Auto SHA256). `compression_format` covers current/reordered handler
indices and EN/RU 760×720/640×480 format reachability in24 `format-order-*.json`
reports, plus public handler/filter/name/profile and real ZIP workflow assertions.
`compression_encryption` checks the complete encryption group, control separation
and shown/hidden confirmation at both sizes, with a 400-character history path;
reports are `encryption-{760,640}-{0,1}.json`.
`compression` checks update and path-mode visibility and scroll reachability
at 760×720 and 640×480; current-build reports are
`update-options-normal.json` and `update-options-compact.json`.
