# Navigation and window state

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

## Современные сочетания

Главное окно показывает вкладки, затем навигацию и затем файловые действия.
Путь по умолчанию представлен сегментами; полная строка открывается отдельным
режимом и через сочетание перехода к пути. Сегменты и история используют тот же
native navigation owner. Escape отменяет черновик. Длинные сегменты сокращаются
с доступным полным путём в подсказке; ранние предки доступны через меню.
Граница архива обозначается в пути. Sidebar начинается у списка ниже chrome,
скрывается без смены места и не дублируется во второй панели.
Активная вкладка округлена сверху и соединяется с фоном навигации;
иконка места и кнопка закрытия справа остаются частью обычного управления
вкладками. Общая рамка объединяет сегменты пути и режим редактирования.
Изменение размера и переход одна→две→одна панели сохраняют видимый текущий
сегмент; скрытый sidebar не резервирует ширину у второй таблицы.

Новая вкладка открывает Домашнюю; новое окно — текущее место. Вкладки сохраняют
свои место, историю, представление, выделение и прокрутку при переключении.
Перестановка и перенос сохраняют контекст; дублирование открывает место заново.
Закрытие последней вкладки закрывает панель, последней панели — окно.
Обычное скрытие второй панели сохраняет её вкладки. После закрытия левой панели
правая остаётся единственной со своим состоянием.

Выход из приложения сохраняет открытые окна, порядок и активные вкладки.
Восстановление повторно открывает дескрипторы через действующего владельца;
недоступное место остаётся явно недоступным с повтором и переходом к родителю.
Избранное поддерживает пользовательские подписи, порядок и открытие в текущей/
новой вкладке либо окне; переименование и удаление меняют только запись.
Перетаскивание обычной папки добавляет запись без перемещения исходных файлов.
Временный экспорт содержимого контейнера не становится сохранённой папкой.
Обычный режим выделения действует по умолчанию; явно включённая пользователем
совместимая альтернативная схема 7-Zip сохраняет свои исходные отметки.

Основные назначения определяет [схема клавиш](../ui-shortcuts-design.md).
Команды объектов действуют в файловом списке, включая таблицу и значки,
используют прежние QAction/command IDs и проверки доступности. Изменённые
сочетания имеют приоритет над старыми list notifications и выполняются один раз;
disabled-команда не должна превращаться в другую старую команду.
Редактор имени, строка пути и отдельное модальное окно сохраняют обычный ввод
и clipboard/Undo. Return/Enter адреса не запускает переименование списка.
Переход к строке пути выбирает её текст без раскрытия истории.
После отказа, ошибки или отмены открытия адреса редактирование остаётся
в строке пути. Перевод фокуса в список выполняется после успешного перехода.
При асинхронной загрузке он дожидается готовности списка; новый ввод пользователя
отменяет отложенный перевод фокуса.
Переключение числа панелей недоступно во время операции или публикации списка
и восстанавливается после завершения.
Меню показывают основное сочетание средствами Qt; совместимые F-клавиши
и дополнительные исходные варианты сохраняются по схеме. Source bookmark hints
остаются; устаревшие hints у переназначенных действий заменены реальным shortcut.
`panel_shortcut` проверяет маршрутизацию Copy/Move, Create/Open/Up/path/two-panel,
редактор имени, reserved keys и модальную изоляцию с clipboard-вводом.

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

### Panel address keys and flat view (P3-05)

Preserve the source header/address above its own list and pane visibility after
F9. Tab/Backtab focuses that pane's visible table/icon list without submitting
the address draft. Check both panes and EN/RU at980×620/640×400, with address
and current list fully inside the window. `panel_address_keys` writes16 reports.
`flat_view` adds24 reports for Drives with source flat enabled, filesystem flat,
and filesystem normal in both panes/languages at the same sizes. Address/list
grouping and source View checked state must survive navigation and toggles;
protected fixture bytes remain unchanged. At compact Windows widths the existing
QtStatusLabel may elide selectedItemsStatus, with full source text in its tooltip;
this is separate from clipping address/list controls. These are Qt geometry/key delivery
checks, not a rendered original Windows or physical-input claim.

Source Delete uses PanelOperations.cpp's file/folder/count language IDs and
standard Yes/No/Cancel question. Refusal keeps the source selection/list.
Yes is the source default button; Enter confirms, while explicit No/Cancel
returns before refresh. Deferred consumers exercise real Return-key dispatch.
Accepted worker error/Cancel refreshes it. Native message boxes retain wrapping,
auto-fit and standard button grouping rather than a fixed resource rectangle.
Trash failures reuse Panel.cpp's captioned HRESULT presentation; macOS exposes
the caption as a heading because native window captions are omitted. The
bounded source_delete consumers inspect both languages/panes at400×200 and
300×180 probes:32 current JSON reports check reachable buttons and fitted text.
Source scope/evidence are in the [receipt](../archive/qt-port/upstream-port-audit.md#p3-05--source-delete-lifecycle-2026-10-10);
these probes do not establish rendered Windows pixel parity.

Find the affected dialog, command or plan ID with `rg -n` and read its contract
before changing layout/text/visibility. [Commands and reports](dialogs.md#commands-and-reports)
describes report generation; [UI reference tooling](../ui-reference.md) owns the
resource-to-widget comparison procedure. These contracts specify controls and
geometry; [acceptance](../ux-port-audit.md) records the completed validation scope.

### Source startup window placement (P3-05.16)

Toolbar defaults retain source IsDialogSizeOK(200,200): when the dialog-unit
extent exceeds the available work area, default-mask bit31 disables labels.
Explicit saved labels/icons override this classification. Qt font metrics and
available screen geometry supply the original calculation; compact client
width alone does not change the saved/default policy. Actual large-font tests
check QAction/style without a rendered-geometry claim. Ordinary980×620/640×400
main-shell reports retain toolbar grouping/order, control visibility and menus.

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
For APFS folders with an unknown size, the File menu and toolbar offer
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
Source settings control dots and single-click behavior. Qt layouts, icons,
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
