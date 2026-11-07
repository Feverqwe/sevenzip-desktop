# Options and preferences

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

### Redesigned settings surface (2026-10-19)

General contains appearance and language; translation information is disclosed
on request. The translation description and expanded source information start
at the same position; disclosure does not add a spacer or move the header.
Opening files contains one searchable list of formats. Search matches displayed
names and extensions without changing the draft or enabling Apply.
Select all and Clear selection act on the whole list regardless of the filter;
the selected count covers all formats. An empty search has an explanation.
Advanced associations retains the original state matrix and all-users scope
only where supported. An unsupported integration has no navigation item.

Memory, Temporary files, Applications and File list are separate task pages;
the last groups display and control preferences. Supported File Explorer commands
have a separate page. Switching pages preserves every draft. The original six
owners retain validation, persistence and registration services. Invalid memory
input selects Memory and focuses its input. Undoing an unapplied format choice
preserves its previous external handler.

Applications also offers confirmation before automatically opening an unknown
archive member externally. The preference defaults to asking and follows the
same Apply/Save/Cancel draft contract. The confirmation identifies the file,
offers Open and Cancel, and defaults to Cancel. Explicit Open Outside and editor
commands do not require this fallback confirmation.

Navigation occupies a sidebar through the footer height. In a narrow window it
wraps above the content with complete section names in the same order. Each page
has a heading and can scroll while the native Apply, Cancel and Save actions
stay accessible in platform order. Initial focus is Appearance. Ctrl+Tab or
Control+Tab on Cocoa activates the next visible section and focuses an enabled
control. The reverse shortcut moves to the previous section. Source algorithms and original defaults
are unchanged. This surface supersedes the old six-page container geometry.

<a id="options-initial-frame-p3-06-2026-10-16"></a>
### Original Options frame reference (P3-06, 2026-10-16)

Original `OptionsDialog.cpp`/`MyPropertySheet` use the largest desktop page
template. `GuiCommon.rc` supplies300×280 content DLU plus eight-DLU margins:
316×296 DLU for System/Menu/Settings/Language. Editor/Folders are shorter.
The rendered pinned Windows System page exposes more than20 rows. This records
the original property-sheet reference; the redesigned General start page has
no association table. Existing640×390/520×380 normal/compact probes now check
reachable controls through disclosure and scrolling, with an exposed footer.


### Options keyboard activation (P3-06, 2026-10-17)

Ctrl+Tab and reverse operate from the footer as well as page controls. Cocoa's
physical Control route (Qt MetaModifier) must work without changing global
modifier policy; use the existing Qt page-selection algorithm and source focus
rule. [Original footer reference and native regression](../archive/qt-port/upstream-port-audit/p3-06-options-control-2026-10-17.md).

Reference: pinned `OptionsDialog.cpp`, `SystemPage.rc`, the other five page
resources, `CPropertyPage::OnNotify`/`OnSetActive` and `MyPropertySheet`.
In the original System page, the first enabled source tab stop is current-user `+`,
all-users `+`, then the association list. Tab and Backtab retain that order;
enabled footer buttons and the tab bar remain reachable. Map existing button
tab stops to Qt StrongFocus under Cocoa's restricted native button policy;
preserve intentional NoFocus controls and the OS keyboard preference.

The redesigned sections retain the first-enabled-control focus rule.
An exclusive radio group uses its selected enabled member; an initial text field
selects its contents. Pending values survive page switching and hide/show does
not reset an already focused field. A capability-disabled page can fall back to
the visible section navigation. `options_keyboard` checks EN/RU at640×390 and520×380,
all four System availability masks and navigation in both directions.
Current widget JSON checks visible control bounds; public assertions check focus,
selection and Cancel without association writes. [Source and native receipt](../archive/qt-port/upstream-port-audit/p3-06-options-keyboard-2026-10-17.md).

### Appearance preference (redesign, 2026-10-18)

Appearance offers Follow system, Light and Dark through the native Qt theme
preference. The choice is a draft until Apply or Save succeeds; Cancel discards
unapplied changes. Invalid memory input keeps the appearance draft unapplied.
Where Qt supports overriding the platform scheme, applying updates existing
windows; the saved preference is restored at
application startup. Changing language preserves the pending choice.
Qt documents this as a [platform hint](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop).
A minimal XCB environment can report an
unknown scheme and ignore an explicit override; it retains its actual native
palette rather than receiving a separate application theme.

Appearance is the first keyboard control in General. Six source page owners
continue to own the settings behind the task pages.

### Native System handler images (P3-06, 2026-10-17)

The application/window identity uses source FM.ico, matching original
FM.cpp::InitInstance and GUI/FileManager IDI_ICON resources. The native Windows
EXE and macOS bundle retain that image; Qt window chrome uses its supported
sizes. This does not alter panel/footer geometry or file-type handler icons.
[Native resource verification](../archive/qt-port/upstream-port-audit/p3-06-application-icon-2026-10-17.md).

Source `SystemPage.cpp::AddIcon/OnInit/RefreshListItem` and
`SystemPage.h::CAssoc::GetIconIndex` choose the displayed handler image from the
source association state. A pending own selection uses the application image;
Clear removes it, and returning to Other restores that handler's image. Linux
adapts desktop-entry file/theme icons through GIO rather than treating an Exec
command as an icon path. Distinct handlers sharing an executable remain distinct.
The existing label/buttons/list/footer geometry is unchanged. Private-XDG
`linux_system_page_*` cases inspect EN/RU640×390/520×380 parent states and actual
Critical/OK after an external-choice conflict; all required groups remain visible
with a disabled explained all-users action. [Source/native acceptance](../archive/qt-port/upstream-port-audit/p3-06-system-native-2026-10-17.md).

macOS pending Own uses the native application bundle image even when the codec
manager supplies a nonempty module filename. `mac_system_page` checks the same
four language/size variants and Other/Own/Clear/restored images without native
registration writes. [Live Windows/macOS values and image receipt](../archive/qt-port/upstream-port-audit/p3-06-system-live-2026-10-17.md).

### Shell Menu page (P2-09, accepted)

Current platform policy: unsupported shell-registration controls and the Zone
label/combo are hidden. Missing Windows DLLs still disable their registration
controls. See [platform visibility](../upstream-port-audit/options-platform-visibility.md).

Reference: pinned MenuPage2.rc and source CShellDll discovery/OnApply.
Native then other-bitness integration checkboxes precede cascaded/icons/root
duplication controls, Zone row and scrollable command list. Missing DLL disables
its checkbox; a 32-bit process without another shell hides only the other-bitness
control. Explicit Apply runs other view first and resets each check to a fresh
registration query, including failure. Cancel initiates no registration.
The Zone caption uses the available row width and remains on one line when it
fits. At smaller widths it may wrap without overlapping the combo or list.
menu_page checks these states through a fake module/registry service and writes
menu-shell EN/RU640×390/520×380 reports. Four current macOS reports reviewed:
compact498-wide controls accommodate the391-wide RU bitness hint; Zone label
and combo do not overlap, the498×185 RU list scrolls within fully exposed bounds.
The real page uses a native factory; Unix has no Win32 DLL integration, Windows
requires adjacent project modules. Production policy controls and twelve current
EN/RU normal/compact reports across all three systems are reviewed in P2-09.3.
Installed native consumers and actual guest registration are verified in .2/.3;
rendered Menu-page pixel parity remains unverified. P2-09 is accepted; the full
Options container and its initial-page behavior belong to P1-12.2.

### System associations (P2-08, accepted)

Reference: pinned SystemPage.rc and CSystemPage OnInit/ChangeState/OnApply.
Associate label precedes current-user and all-users plus buttons; each button
is centered above its corresponding association column. File type is the first
column, followed by current user and all users; the list grows while the Options
footer remains visible. No selection targets all supported rows. Unavailable
groups/unknown native types show a reason and cannot initiate registration.
All four source controls are mapped in system-page.contract.json; the generated
reference retains source hashes and the exact revision. system_page produces
EN/RU640×390/520×380 reports; ui_parity_system checks their reviewed spatial
relations and visibility with injected services. Four-report comparisons pass
on macOS, Windows and Linux; twelve current reports were reviewed. Compact
Windows list476×248, Linux476×240 and both80-wide buttons are fully exposed;
RU labels fit476-wide controls and footers remain visible. Windows RU Folders/
removable captions wrap to prevent hidden pages enlarging Options. Time-only
ZIP controls wrap at300 pixels; NTFS views retain single-line mtime/ctime and
380-pixel atime wrapping. Four current Windows WIM reports confirm480×430.
Rendered Windows parity and existing Add language clipping remain P3 work.

### Live dialog language consumers (P1-14.2)

References are pinned Password/Copy/Split/Overwrite/Mem/Messages RC/source,
GUI ExtractDialog.rc and CompressDialog/CompressOptionsDialog resources.
`language_dialog` writes EN/RU normal/compact `language-*` JSON: password360/260,
copy/split/folder480/300, overwrite/memory/messages700/560, extract600/470.
It verifies text bounds and pending state across hot switch. `language_ui` adds
Add760×720/640×480 and advanced520×400/440×340. Add content uses its existing
scroll viewport; the footer remains exposed. Advanced archive-time checkbox
uses Qt text wrapping for the original long source words at compact width.
Reachability of all scrolling Add controls, current message forms and source
accelerator/buddy review form the accepted P1-14.2 scope. These reports do not
establish rendered Windows parity.
Progress reports cover EN/RU760×450/640×380 paused state with the
approved overview/Details grouping; source values and control states remain.
Properties reports cover540×360/360×240 with the
existing table, rows and selection. Built-in viewer/editor reports cover760×560/
420×300 while preserving pending content; editable text is omitted from JSON.

P2-09 correction2026-10-04: toolbar Info uses the original IDM_PROPERTIES
consumer; the independent archive-summary screen is removed. Four current
`language-toolbar-properties-{en token -|ru}-{540|360}.json` reports retain
the source properties list/button grouping with full viewport and footer
exposure. The same source dialog shows archive/folder properties with zero
operated indices, tested through actual Info on an empty archive. These checks
validate source workflow/Qt geometry, not native Windows System Properties
or pixel parity.
`native_archive_properties` adds `native-error-type-{zip,7z}-{540,360}.json`:
the original Error Type row presents the rejected native handler while the
archive's Type remains the accepted handler. The same properties list remains
fully visible at540×360 and360×240. This checks values/grouping/geometry, not
rendered Windows pixel parity.
Archive preview-limit is a local bounded-viewer integration message, with no
equivalent native bounded-editor resource. `language_archive_command` checks
EN→RU→EN text/buttons at main-window1100×640/640×380; four
`language-archive-preview-*.json` reports show a complete400×116 Qt popup.
It also retains selected rows and verifies no extraction or listing restart.

Compression RAM rejection references the original CompressDialog.cpp
SetErrorMessage_MemUsage body rather than a separate resource dialog.
`language_archive_command` checks zero-limit rejection EN→RU→EN at Add760×720/
640×480, retaining pending values. Four `language-compress-memory-*.json`
reports show English400×226/Russian400×256 with full message/buttons. Dictionary,
memory and solid geometry tests expose their affected controls and write current
760×720/640×480 JSON with source B/KB/MB/GB and `*  ` values; other Add controls
retain the scrolling reachability requirement. Association error reports check
local EN/RU plaintext paths and full400×116 message/buttons. Progress cancellation
uses Qt platform standard button captions with original Yes/No/Cancel decisions.
No rendered Windows reference or cross-platform verification is claimed.

Add's language test now exposes every named atomic control by scrolling at both
sizes and checks full exposure, label/button widths and persistent footer.
`language-add-*.json` records its final bottom position; controls above the
viewport are verified at their own scroll positions. `language_focus` checks the
source RU Show-password/Parameters collision and composite CPU/RAM label buddies:
Tab reaches both controls, mnemonic cycling is checked when enabled by the OS,
and focus proxies reach the real inputs. Source RC fallback retains original
English accelerator positions when a translation is missing. Root language
reports at980×620/640×400 retain the same table/items/selection and source names;
the isolated `root_navigation` startup directory contains their current JSON.

### Menu page (P1-13.1)

Menu checkbox rows and Folders radio rows retain the resource's10-DLU height /
14-DLU pitch through a4-DLU gap mapped using the current font. Native QWidget
frames must remain separate at normal/compact sizes. Options geometry waits for
activation/configure and retains minimumSizeHint when a style requires a taller
frame. The [native Options receipt](../archive/qt-port/upstream-port-audit/p3-06-options-spacing-2026-10-16.md)
records RED/GREEN geometry and platform boundaries.

Reference: pinned `FileManager/MenuPage.cpp`, `MenuPage2.rc` and
`Explorer/ContextMenuFlags.h`. Options page 2300 is titled 7-Zip and keeps source
order: two shell registration checkboxes, Cascaded, Icons, Eliminate duplication,
Zone label/combo, Context menu items label and the 14-row checkable list.
P2-09.3 now enables Cascaded/Icons/ElimDup and the list for the retained source
in-app consumers. Windows registration controls discover adjacent original-name
7-zip.dll/7-zip32.dll/7-zip64.dll and disable missing modules; Unix registration
controls are hidden. Zone is
enabled on Windows; on POSIX its label and combo are hidden.
The source default is `* No`; existing custom integer values remain selectable.
Qt scrolls the list and uses a persistent OK/Cancel/Apply footer.

`menu_page` writes `options-menu-640/520.json` at 640×390 and 520×380; checkboxes
fit their text, grouping/order and Zone label/combo separation are checked, and
the list and footer remain exposed. No rendered Windows reference is available;
geometry checks establish Qt structure/usability on the locally tested OS only.

## File list settings

При увеличении окна Settings флажки, подпись лимита памяти и строка ввода
остаются компактной группой сверху. Свободная высота остаётся под группой,
а подпись не растягивается и не отдаляет поле ввода.

Options applies pages in source System/Menu/Folders/Edit/Settings/Language order.
An association error shows one Critical message without changing the current tab
or vetoing OK/later pages. Invalid RAM selects Settings and keeps its raw input;
earlier page values may already be committed, as in the source property sheet.
MyMessages suppresses that error when user questions are disabled. Message-box
titles are native-hidden on macOS and retain `7-Zip` on Windows/Linux.
`options_integration` and `options_dialog` produce current normal/compact-owner
error reports; inspect every visible message label/button for fit and clipping.

Reference: `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/SettingsPage2.rc` and `SettingsPage.cpp`.
The Qt page includes the parent-row and alternative selection preferences and
the extraction memory limit. The file table uses full-row highlighting.
Alternative selection is enabled and persists the source mark mode.
Refresh resets the source group anchor to the first list row, independently of
restored focus. The next Shift-click marks that original inclusive row range;
a shown `..` remains excluded from operated indices. Ctrl-click starts a new
anchor. Selection colours, single focused row, totals and peer state stay in
sync after direct restoration, menu Refresh and timer reload.
The page has no heading absent from the resource.
Extraction-memory edit validation preserves raw input until Apply. The six-page property sheet
keeps System/Menu/Folders/Edit/Settings/Language order. Original OptionsDialog
and Windows/Control/PropertyPage.cpp set the initial page to System (nStartPage0);
the Qt container now follows that state. Ctrl+Tab/Shift+Ctrl+Tab move between
pages using Qt's tab-bar focus; the Tab cycle reaches the visible page and may
visit the footer first on Windows. Pending input
survives switching; unsupported platform actions are hidden within pages.
This establishes structure and interaction, not pixel correspondence.
Folders offers the system temporary directory and a user-specified directory.
Path and Browse are enabled for the specified directory. A legacy current-folder
setting uses the system temporary directory. `options_dialog` requests640×390 and
520×380 and checks actual JSON/geometry, native mode order and separated footer.
Qt height-for-width and native window constraints can change the actual client
size; checks use the exposed controls rather than a fixed height calculation.
`options_dialog` also checks Settings memory row grouping and input focus after
invalid Apply, writes options-settings JSON at both sizes. `options_integration`
checks all-six-page Apply/OK/Cancel/restart and visible keyboard focus;
P1-12.2 evidence is recorded in the port audit.
`fm_settings` checks unclipped labels, order and separated footer at 460×390
and 340×390, writing current-build settings JSON reports. Rendered Windows
parity remains unverified.

Language reference: pinned `LangPage.rc`/`LangPage.cpp`; imported translation
provenance is in third_party/7zip-help/provenance.json. Qt Linguist catalogs replace
native language files. The page keeps the label, combo and read-only scrollable
language/locale report in source order; catalog completion is reviewed with
the translation tools. `language` checks600×320/480×280; `language_ui` writes
`options-language-*-*.json` for the other five Options pages at640×390 and
520×380 in English/Russian, verifies bounds, checkbox widths and wrapped label
height. Reports reviewed2026-10-01 on macOS: largest shell-bitness RU caption
391 px fits476 px, memory label wraps within31 px, editor fields retain input.
Qt standard buttons retain platform conventions. This validates these Qt forms'
structure/usability, not rendered Windows pixel parity or remaining app dialogs.
Panel parent navigation follows source Panel.cpp VIEW_PARENTFOLDER's icon-only
control using Qt's standard up image; the menu caption stays independent and
button images survive language/state changes. Current640×400 two-panel JSON shows
29×28 parent buttons and all four status fields visible. QtStatusLabel permits
bounded status widths, elides long text and exposes full text in a tooltip;
the source value/accessibility remains intact. `panel_key` verifies640×400 and
restored splitter300, and `language_ui` verifies icons after EN/RU reload.
`panel_key` checks red marks and single focused selection across four view
modes, disk/archive and flat/normal folders, writing 980×620 and 640×400
window JSON reports. The parent sentinel stays unmarked. These reports establish
Qt geometry, not Windows pixel parity.
