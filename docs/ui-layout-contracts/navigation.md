# Navigation and window state

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

## Недавние места

Кнопка у адресной строки открывает до пяти последних мест. Выбор действует на
панель, из которой открыт список. Escape закрывает список без перехода.
Недоступное место остаётся в списке, а панель сохраняет прежнее расположение.
Кнопки «Назад» и «Вперёд» используют собственную историю панели.

## Навигация и состояние окна

У каждой панели свой адрес, история и список. В адресе видны сегменты пути
и граница архива. При сужении окна текущий сегмент остаётся доступен, а ранние
предки открываются из меню. Полный путь можно редактировать отдельной командой.
Escape отменяет черновик. Ошибка перехода оставляет введённый адрес доступным для
исправления и сохраняет прежний список. Фокус переходит к списку после успешной
публикации, если пользователь тем временем не начал другое действие.

Переключение числа панелей сохраняет состояние скрытой панели. Размеры панелей
следуют положению разделителя. Приложение восстанавливает окна с их местами.
Для недоступного сохранённого места видны действия повтора и перехода к родителю.
Именованные сохранённые места доступны из меню избранного. Переход действует
на активную панель.

<a id="современные-сочетания"></a>

## Клавиатурная маршрутизация

Основные назначения определяет [схема клавиш](../ui-shortcuts-design.md).
Команда списка выполняется один раз и только в доступном состоянии. В редакторе
имени, адресе и модальном окне сохраняется обычный текстовый и clipboard-ввод.
Return в адресе подтверждает переход. Переключение числа панелей восстанавливает
доступность после защищённой операции. `panel_shortcut` проверяет маршрутизацию
клавиш и изоляцию ввода в редакторе.

### Parent keyboard focus (P3-05, 2026-10-15)

Parent keeps the keyboard owner while replacing rows: a focused list returns
to the same pane's visible table/icon view; an address caller retains its actual
Qt focus proxy. Preserve the source focused child row, peer address draft and
peer rows. Deferred publication must yield to newer address input or peer focus.
`native_navigation_window` covers both panes, four views, EN/RU and list/address
callers, with64 `parent-focus-*` reports at980×620/640×400 per QPA. Inspect both
addresses and visible views for enabled state and complete bounds. Focus and
draft contents are public behavioral assertions, deliberately absent from JSON.
This does not establish physical keyboard delivery or rendered Windows pixels.

### Panel address keys (P3-05)

Preserve the source header/address above its own list and pane visibility after
F9. Tab/Backtab focuses that pane's visible table/icon list without submitting
the address draft. Check both panes and EN/RU at980×620/640×400, with address
and current list fully inside the window. `panel_address_keys` writes16 reports.
At compact Windows widths the existing
QtStatusLabel may elide selectedItemsStatus, with full source text in its tooltip;
this is separate from clipping address/list controls. These are Qt geometry/key delivery
checks, not a rendered original Windows or physical-input claim.

Delete moves selected filesystem files and folders to Trash. The command is
unavailable for archive entries and read-only sources. The confirmation shows the
source, consequences, and an optional disclosure with all selected names. Cancel
initially has focus and is the default. Return on Cancel, Escape and closing the
dialog preserve the selection. On macOS, Command–Backspace opens the confirmation
from the file list.

The body scrolls while both footer actions remain reachable at normal and compact
sizes in English and Russian, including with long Unicode names. Trash uses the
common progress view with the current path and item counts. Stop is observed
between native calls. On refusal, the failed item stays in place and the source
is refreshed to reflect any completed moves. Archive Move owns its separate
confirmation and source cleanup.

Find the affected dialog, command or plan ID with `rg -n` and read its contract
before changing layout/text/visibility. [Commands and reports](dialogs.md#commands-and-reports)
describes report generation; [UI reference tooling](../ui-reference.md) owns the
resource-to-widget comparison procedure. These contracts specify controls and
geometry; [acceptance](../ux-port-audit.md) records the completed validation scope.

### Source startup window placement (P3-05.16)

FM.cpp::InitInstance restores a valid saved normal rectangle and maximized flag
before the caller exposes the window. Missing or malformed Position leaves the
default rectangle; Panels is admitted independently, with source panel-count and
focus defaults. Construction itself stays hidden under the QWidget lifecycle.
Close saves the normal rectangle while maximized; the next launch replays it.

The launch show command overrides saved maximization except Normal/Show/Default,
whose original InitInstance branch applies the saved flag. Explicit Maximize,
Minimize, Restore and no-activation retain their requested states. Hide binds
without exposing the window. Qt uses WA_ShowWithoutActivating for source
no-activation commands; native focus/taskbar behavior remains unverified.
Seven `launch-show-*` JSON reports inspect nonminimized states after the visible
QSizeGrip finishes deferred layout. Hidden/minimized cases check state only.

`window_placement` covers980×620/640×400 normal rectangles, normal/maximized
startup, one/two panels, focused pane, splitter publication, Close and restart.
Current JSON checks the visible panel and its controls on each launch. Qt uses
logical client coordinates and native frame/handle/content minimums; a native
rendered Windows reference remains required for frame and pixel parity.

The source splitter has a4-unit handle and a16-bit fixed-point ratio. Saved
coordinates are actual left-panel widths, excluding the handle; initial width300
remains300 at client widths980 and640. Expansion/return and F9 while one panel is
hidden retain the source ratio and rounding. Public splitter fixtures cover the
source120-unit bounds and halving below244 total width. Actual panel content can
impose a larger Qt minimum to prevent clipped address/list controls. Current
`splitter-source-{980,640}` JSON accompanies the normal/compact window cases;
native pixel/frame comparison remains open.

Redesign adaptation: the panel divider supports Tab/Shift–Tab focus,
Left/Right adjustment (Shift for a larger step), and equal widths with Return
or a left-button double-click. Pointer resizing keeps the previously focused
list active; keyboard resizing keeps focus on the divider. A temporary content
minimum must not replace the preferred split when switching to one panel and
back. Panels stay non-collapsible; navigation, models and selection remain with
their existing owners. The existing window-state storage remains authoritative.
`window_placement` covers keyboard/reset and minimum/toggle restoration;
`panel_key` checks selected items, model continuity, paths and pointer focus.

### Persisted source locations (P3-05.14, required scope)

Reference: pinned PanelFolderChange SetBookmark/OpenBookmark/FoldersHistory,
App.h focused-panel forwards, MyLoadMenu bookmark popup/dispatch and
ListViewDialog.rc. Keep ten Set/Open slots and original captions/shortcuts.
Saving uses the focused panel's stable source prefix, including virtual archive
links and namespace locations; an empty root prefix follows the original slot
semantics, and opening an empty slot binds the original root namespace.
Materialized temporary engine paths must not enter settings.
Open/history selection bind the stored source location and publish the resulting
panel through existing owners. Source deepest-existing-folder behavior remains
authoritative; previously stored Qt display entries need explicit coverage.
Verify menu availability/handler agreement, both panels, Unicode/spaces/literal
` / `, nested archive close/reopen/restart, history editing/Cancel, operation
guards, unchanged archive/input bytes and no unintended peer navigation. Inspect
EN/RU normal980×620/compact640×400 address/list and bookmark/history widget JSON.
Native device/UNC/ADS and rendered Windows acceptance remain separate checks.
Original BindToPath reuses an already open nested chain. After that chain closes,
CAgentFolder::BindToFolder finds directories rather than opening archive files:
a stored nested location stops at its containing outer archive. Verify this
original fallback on close/restart as well as exact reuse while still open.
History uses its existing540×360/360×240 dialog geometry contract.
The root namespace's empty source prefix remains an actual selectable empty
history entry, including after restart; Cancel is distinct from accepting it.
Native Windows device/ADS/UNC bookmark/history/restart consumers inspect the
owning address and list at980×620/640×400 and preserve the peer's draft.

<a id="source-root-command-target-p3-0513-implementation-validation-active"></a>

### Source root command target (P3-05.13)

Reference: pinned resource.rc IDM_OPEN_ROOT_FOLDER, MyLoadMenu, App.h inline
OpenRootFolder, PanelFolderChange::OpenDrivesFolder and Panel.cpp list WM_CHAR.
The command/backslash/slash opens the drive list: Windows Computer prefix and
original drives; POSIX retains the documented CFSDrives mounted-volume service.
It closes archive links and uses the focused panel, leaving the peer intact.
Do not substitute the separate root namespace's Computer/Documents/Network rows.
Verify EN/RU normal/compact address/list/status geometry, command and both key
routes, source provider choice, parent/back restoration, selection and input bytes.
Current namespace consumers are recorded in the
[address/Root acceptance](../archive/qt-port/upstream-port-audit/p3-05-address-root-2026-10-17.md).
Full native/rendered parent acceptance remains separate.
The Show '..' preference exposes a parent row in the drive list, while the
virtual root has no parent row.
The integrated Qt command/key consumers use the retained source drive transition.
The separate root namespace is reached through Parent; Back from a filesystem
location restores its actual stored namespace prefix. After source selection
restoration, same-row Open remains available. Current checks belong to the audit.
Live EN→RU→EN updates both bound drive addresses and the focused window title.
Preserve a pending address draft, existing drive rows and selection; verify both
panels at normal/compact sizes. The header reload uses the source path/icon/title
stage without adding history or enumerating the drive list again.

<a id="source-address-dropdown-p3-0512-required-remaining-scope"></a>

### Source address dropdown (P3-05.12)

Reference: pinned PanelFolderChange.cpp::LoadFullPathAndShow,
OnComboBoxCommand and AddComboBoxItem, with RootFolder names and source path
bindings. The dropdown presents the current path hierarchy with indentation,
then the original Windows Documents/Computer/drives/Network entries. Display
names and binding paths are distinct; UNC server and archive-link boundaries
follow the source algorithm. POSIX drive/mount substitutions need an explicit
platform disposition. The visited-path history remains in FoldersHistory.
Selecting a row binds its stored source path and returns focus to the list;
Escape restores the current address. Each panel uses its own current prefix.
Check EN/RU normal980×620/compact640×400 widget JSON, popup order/indentation,
visible text, address/list exposure and keyboard selection/refusal. The former
history-only dropdown did not satisfy this source contract. Current Mac public
selection/EN-RU geometry coverage is recorded in the port audit; native/platform
and rendered Windows acceptance remains open. Escape restores the bound source
prefix, including its native separator, and posts list focus. A newer focus
interaction outside that panel cancels its stale Qt delivery.
When popup closure temporarily clears application focus, the adapter uses the
window's retained focus widget to preserve that guard and the source list request.

The closed editor also shows the current path icon, independently of popup
selection. Reference: the same LoadFullPathAndShow body and SysIconUtils helpers.
Disk folders and archive subfolders show a directory icon; an archive-link root
shows its file icon, with extension fallback for a virtual nested archive. Parent
navigation restores the corresponding outer archive or filesystem icon. Qt uses
a leading QLineEdit action for the original ComboBoxEx edit image; the action and
address/list must remain exposed in all four sizes/languages above. Namespace,
Windows shell queries retain the requested source attributes, including the
directory policy for an ADS colon. Current device/ADS/UNC selection, refusal,
focus and closed-editor icon checks are recorded in the
[address/Root acceptance](../archive/qt-port/upstream-port-audit/p3-05-address-root-2026-10-17.md).
Full rendered icon/style acceptance remains with the parent workflow audit.

<a id="source-caption-helpers-p3-0510-validation-active"></a>

### Source caption helpers (P3-05.10)

Reference: pinned LangUtils colon/remove-colon/window bodies and actual
EditPage2/ProgressDialog2/BenchmarkDialog resources and OnInit callers.
Translated Benchmark Speed loses exactly one trailing colon; missing entries
retain the full resource caption. Edit viewer retains `&View:` fallback.
Progress packed/files labels use original append-colon policy. Initial and live
reload share these rules, preserving pending input. Benchmark Cancel/Help,
Link Cancel and Progress Cancel use source standard-control mappings; completed
Progress rebinds to IDS_CLOSE. Existing form sizes/order and footer contracts
apply. Current Mac48 EN/RU footer reports pass exposure, size hints, source
captions and separation. Final native seven consumers and48 footer reports also
pass, followed by final native full256PASS. Linux and rendered acceptance remain open.

### Virtual root main window (P0-04.2)

Reference: `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/RootFolder.cpp`, `PanelFolderChange.cpp`,
`PanelKey.cpp`, `resource.rc` View menu. The native root has an empty address
and `7-Zip` caption; the list contains source-provided item names and indices.
Parent is disabled at the root, enabled at POSIX `/`, and mutations are disabled
on the root provider. The usual menu, toolbar, address, list, status order stays.
`root_navigation` checks visibility without clipping at 980×620 and 640×400 and
writes `macos-root-980x620.json`/`macos-root-640x400.json` on the checked host.
The JSON verifies Qt structure/usability; source resources establish grouping,
not Windows pixel appearance. Windows runtime capture is still unverified.

Windows Computer/Documents/Network row and address-popup icons use the original
CSIDL shell indices through SysIconUtils's native QIcon bridge. The closed
Computer/Network address uses that same source image. Icon selection does not
alter the control order or geometry; POSIX retains platform icons. Public
panel_item_icons verifies native image data and small/large sizes separately from these geometry
contracts. Whole rendered/HiDPI Windows reference acceptance remains open.

### Mounted volumes (P1-11.1)

Read-only archive mount preparation keeps Cancel available while Mount waits
for readiness. Show the current stage, elapsed time and progress: directory
discovery is indeterminate until its work is known; entry projection and direct
read checks show completed/total counts. A long current filename is elided with
the full path in its tooltip. Explain the preparation beside the disabled action.
Keep EN/RU text and controls visible at normal and compact dialog widths.
For APFS folders with an unknown size, File → Advanced offers
Calculate folder size for the selected folders. The command reads those
subtrees with cancellation and updates the size column; siblings remain unknown.

On macOS/Linux the virtual root adds «Тома» after «Компьютер». The mounted
volume list has the source `CFSDrives` name/properties, the normal menu → toolbar
→ address → list → status order, and disables filesystem mutations until a
volume opens. `root_navigation` writes current-build
`macos-mounts-980x620.json` and `macos-mounts-640x400.json`. At 640×400 the
toolbar, address, table and panel status are fully visible; hidden optional
status segments are not counted as clipping. Windows drive/resource layout
still needs runtime verification.

### Windows device namespace and F5 Copy (P3-05.7)

Reference: pinned RootFolder/FSDrives provider, PanelKey/App::OnCopy and the
existing resource.rc main shell/CopyDialog.rc contracts. Device rows are files
by source kpidIsDir; Copy is available, Move and filesystem mutations disabled.
The same menu → toolbar → address → list → per-panel status order remains.
`fs_drives_device_ui` writes sixteen EN/RU success/Cancel current-build reports:
namespace980×620/640×400 and Copy640×400/480×320. Native Windows Qt6.12.0 reports
reviewed2026-10-06: compact address537×22/list632×227, panel status632×21 are
fully visible. Compact Copy places label above path340×22/Browse81×26, selection
458×212 below it and separate OK/Cancel81×26 at y283; all remain inside480×320.
The source device properties use TotalSize, so the existing status Size field
is not an independent device-length report. Hidden menus are not clipping.
These reports verify native Qt grouping/geometry, not rendered 7zFM pixel parity.

### Network namespace (P1-11.3)

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`RootFolder.cpp`/`NetFolder.cpp` source hierarchy. On macOS/Linux «Сеть» is
the third root entry and lists already mounted network shares. The menu →
toolbar → address → list → status order remains. Back and Parent are enabled
inside this namespace; mutation commands are disabled. `root_navigation`
writes `macos-network-980x620.json` and `macos-network-640x400.json` and
checks address and list visibility at both sizes. These reports establish Qt
geometry, not Windows pixel appearance; live shares and Windows rendering
remain unverified.

P3-05.11 restores the original Windows server/container address fallback.
`native_network_navigation` enters an owned local server in EN/RU normal/flat
states, opens only its private share and returns with source focus. Eight
`network-server-{language}-{flat}-{width}.json` reports at980×620/640×400
check menu/toolbar/address/list/status order, full exposure and size hints.
Source-backed binding chooses the namespace even when Qt classifies its address
as a directory. Copy and mutations remain disabled without source operations;
Parent/Back stay available. Current native reports pass. Owned local SMB bytes/
refusal are proven by public tests; external shares/credentials and rendered
Windows pixel parity remain unverified.

## Source long-path browser

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`FileManager/BrowseDialog.cpp/.rc`, `IDD_BROWSE`. The original custom browser
is active for long/super/device paths; it is distinct from the normal native
folder/file picker. Preserve Parent, Create Folder, current folder on the top
row; single-selection Name/Modified/Size list; editable path, filter, accept/Cancel.
Folder mode hides the filter; zero/one file filter disables it. Parent and
directories remain before files regardless of sorting direction. Changing the
filter reloads the list, keeping the source-selected filename where possible.
Backspace/Control+R/F7 and list Enter retain source parent/reload/create/open
handlers. OK outside the list resolves raw input through source GetFullPath;
Cancel preserves the caller's FilePath/filter. Folder acceptance adds a separator.
Ordinary native pickers use source CommonDialog flags: Open permits a new name,
Save does not add an overwrite prompt; callers own subsequent validation.
Source settings control the parent row. A single click selects an item and a
double click opens it. Qt layouts, icons,
accessible button names and locale time presentation replace Win32 services.
The current-folder caption elides in the middle; tooltip and accessible name
retain the full source prefix, while the editable path keeps the raw value.
Normal 560×640 and compact 420×400 reports check grouping/order, fitting labels,
visible path/filter/footer and row values. Rendered Windows parity is unverified.

The agreed redesign uses the shared dialog frame and a separate native footer.
Path and file type have visible labels and accessible names. The accept caption
is Choose folder, Open or Save according to the existing mode; its source role
and resolution rules remain unchanged. A hidden folder filter reserves no empty
row. Parent/Create Folder remain native push buttons with accessible names and
tooltips, including live translation, and keep their focused Return behavior.
Their widths respect native content minima rather than fixed source DLU widths.
The initial frame scales with the font; compact resizing keeps fields and
actions exposed while the list scrolls.
