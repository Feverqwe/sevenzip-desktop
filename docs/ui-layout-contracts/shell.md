# Menus and command presentation

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

## Согласованная группировка меню

«Файл» оставляет частые действия на первом уровне, варианты открытия —
в «Открыть как», создание папки/файла/ссылки — в «Создать», остальные операции
и системные расширения — в «Дополнительно». Все алгоритмы контрольных сумм
остаются в отдельном подменю. Контекстное меню использует те же варианты
открытия и контрольные суммы, а редкие действия и расширения собирает в «Ещё».
В «Вид» режимы представления и сортировка находятся в подменю; навигация,
две панели, плоский вид и обновление остаются доступны непосредственно.
«Формат времени» имеет постоянную подпись, исходные актуальные примеры и UTC
сохраняются внутри. Эти изменения заменяют прежнее требование плоского RC-порядка.

Группировка применяется после исходного отбора/проверок доступности и сохраняет
command ID, состояние и обработчики. Главный File-menu сохраняет QAction;
контекстный popup сохраняет независимые копии. Системные подменю сохраняют
внутренний исходный порядок и lifetime. Повторное открытие не дублирует команды,
новые неизвестные расширения также остаются доступными в редкой группе.
`source_context_menu` проверяет реальное создание файла через вложенное «Ещё»
в вызвавшей меню панели; `view_menu` выбирает представление через подменю
и проверяет состояние обеих панелей. EN/RU normal/compact reports включают
новые подменю, доступность команд и сохранность провайдеров/данных.

<a id="namespace-hash-result-consumers-p3-058-validation-active"></a>

### Namespace Hash result consumers (P3-05.8)

`namespace_hash` invokes original PanelCrc/HashGUI results for Windows private
device and ADS fixtures. The existing ListView contract applies: two-column
propertiesList, then listViewButtons, with source OK/Cancel captions and roles.
EN/RU success and recovery inspect540/420×360 reports: rows and footer fully
exposed, vertically separate, buttons ordered without overlap and at least their
size hints. Twenty current native reports pass; rendered pixel parity and final
platform acceptance remain pending. Cancellation produces no successful result.

<a id="source-shell-popup-p2-09-in-progress"></a>

### Source shell popup (P2-09)

Reference: pinned PanelMenu.cpp::CreateFileMenu/CreateSevenZipMenu and Explorer
ContextMenu.cpp::QueryContextMenu, including source MenuLogo.bmp. System is
inserted before the 7-Zip group when requested; native System is connected on
Windows and unavailable on Unix. Cascaded source Open archive, Open archive submenu, Extract files,
Test archive and Add to archive remain in source query order. Available Open/
Extract/Test/Hash, direct Add to7z/ZIP and SHA256 generation use concrete services;
modal Add uses the source fixed-selection dialog. Windows email commands follow
the same native Add/update capabilities and retained MAPI workflow; Unix email
remains unavailable. The native_shell_email consumer checks all three original
email rows in EN/RU at980×620/640×400, including geometry and Unicode filenames.
Opening captions in the Qt File and panel context menus explain archive
navigation, external applications and independent windows. Internal extension
order, shortcuts and command routing remain intact; the agreed grouping above
changes their placement in File/context menus. Specialist `*`, `#` and
`#:e` entries retain their markers with explanatory captions. This requested
text adaptation also applies to the forced-format submenu; the native Windows
Explorer extension retains its original source captions.
The source append-separator block remains commented out; presentation separators
between the redesigned groups belong to the Qt layout.
shell_menu_window checks actual popup/independent Open success, forced-type
refusal, password cancel and selection result. It checks each submenu action
rectangle, order and text width at EN/RU980×620/640×400 and writes four
shell-menu-en/ru-980/640 JSON reports. The nested Open-as menu is hidden until
activated; separate reports check its explanatory captions when opened.
One affected EN submenu PNG was inspected specifically
for the source bitmap mask/rendering. These checks do not establish rendered
Windows pixel parity or complete native-extension UX acceptance.

P2-09.2 System presentation acceptance compares actual pinned Windows7zFM
File/System with the same source panel/provider query and Qt projection:
22 rows at96DPI preserve order, grouping, state and icons. Reference and Qt
PNGs were reviewed, including Share orientation. Both fonts are Segoe UI9pt;
QWindows11Style supplies24px rows/padding/rounded frame versus native22px rows
and9px separators. The adapter copies source GDI icons and forwards native
measure/draw/mnemonic messages for owner-drawn rows, submenus and callback
icons. Failed measurement disables that row. Geometry reports continue to
check EN/RU normal/compact popup visibility. This affected-state comparison
does not establish exact pixel parity or other DPI/platform rendering.

shell_update_window additionally checks direct Add7z/ZIP/generation success,
broken existing archive refusal, selection and result appearance. Its eight
EN/RU980×620/640×400 root/hash JSON reports include QAction texts, source verbs,
order, enabled state and rectangles. Source order is modal Add,
Add to7z, Add toZIP, CRC SHA submenu; the hash submenu preserves the eleven
calculation rows, separator, Generate and Test. Current root bounds287×92 EN/
316×92 RU, hash355×301 in both languages, every visible action fully exposed.

### Main menu and toolbar (P0-09.1)

On macOS the existing Exit command belongs in the native Sevenzip application
menu with Cmd+Q, independent of the selected interface language. Other platforms
retain the File-menu placement. Native menu placement is an OS adaptation;
offscreen widget geometry cannot establish it.
Exit must become available again after folder refresh and modal commands return;
closing during a protected source call remains blocked.

Source Panel.cpp::OnCreate creates status bars with WS_CHILD|WS_VISIBLE and no
SBARS_SIZEGRIP; Windows/Control/StatusBar.h forwards those flags unchanged.
Qt's per-panel and shared transient-message status bars must likewise have no
resize grip. `main_shell` checks EN/RU at980×620/640×400, one→two→one panels,
unchanged Unicode message text, exposed shared bar and absent visible QSizeGrip;
`status-no-grip-*` reports record each phase. The window border remains the
platform resize surface. These checks establish Qt structure, not rendered parity.
Set the Unicode message before each panel toggle: creating the second panel
must preserve it. Panel construction does not publish a new operation result.

Source SetButtonText supplies the seven short button captions AND tooltips;
shared File menu QAction captions do not replace them after language/selection
changes. `language_ui` inspects EN/RU normal980×620 and compact640×400 reports
`toolbar-text-{-,ru}-{980,640}.json`. Tooltip text is asserted directly, without
a popup-pixel claim. Native rendered tooltip appearance remains unverified.

Opening the parent View menu runs source OnMenuActivating before display. Its
time submenu caption is the current day; five current-time examples and UTC
reflect the focused source panel and shared UTC flag. Source view/sort/two-panel,
flat/toolbar/auto-refresh checks are projected without executing commands.
`view_menu` covers both panes and EN/RU at980×620 and640×400, writing24
`view-menu-*-main.json`, `view-menu-*.json` and `view-menu-*-time.json` reports.
Both addresses and the actual visible table/icon lists must fit. Command bindings
are retained in the new groups; reports do not establish rendered Windows parity.
For the closed address's decorative image, verify the complete painted glyph.
Qt6.12 Cocoa can clip one transparent pixel of its QLineEdit side-widget padding
while exposing the full16×16 logical icon. Reports retain strict widget bounds
and additionally record `iconBounds`/`iconFullyVisible`; clipping or hiding the
glyph remains a failure. A source selection's pending list-focus request survives
popup visibility until Hide, while a newer editor/peer interaction supersedes it.

Reference: `CPP/7zip/UI/FileManager/resource.rc::IDM_MENU`,
`App.cpp::g_ArchiveButtons/g_StandardButtons` and `MyLoadMenu.cpp` at
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Six top-level menus follow
File/Edit/View/Favorites/Tools/Help. File retains the RC command set;
independent Qt archive-workflow aliases have been removed. Unsupported source commands remain
visible disabled. The original Archive and Standard toolbar reference used seven
large/small bitmap pairs; the approved vector adaptation is described below.
The local folder-size command belongs to the archive group. Hiding that group
removes its toolbar button while retaining the command in the File menu.
Text/size/visibility derive from the native toolbar flags. `main_shell` checks
command identity, shortcuts, availability, active-panel routing and rendering, then writes
`main-shell-980x620.json`, `main-shell-640x400.json` and File/View popup reports
for both sizes. Qt QMenu may scroll when its source command list exceeds the
available screen; JSON records its geometry and action tests cover non-widget
QActions. The Windows rendered menu is not yet available for pixel comparison.

Command icon checks render palette changes and display density; application and
native namespace icon checks retain their separate source-resource contracts.

Redesign adaptation: Add and Extract show text beside their icons when the
existing button-label preference is enabled. Test, Copy, Move, Delete and Info
use icons, with localized accessible names and tooltips that include their
current native shortcut. The label preference can still hide the primary
captions; toolbar group visibility and large/small icons keep their existing
owners and saved values. Buttons use native hover/focus rendering and support
keyboard focus. The vector toolbar uses 16×16 logical icons to match the concept;
the larger preference uses 24×24 rather than the original bitmap canvas sizes.
The toolbar reserves space around and between command buttons. Captions use the
regular application font rather than Cocoa's smaller bold toolbar label.
The parent button and editable address occupy a separate padded navigation row;
the address grows with the system font and keeps an adequate input height.
These metrics belong to main-window chrome, not shared dialog controls.
The Extract caption uses the same translation as the redesigned
Extract dialog. Command/navigation symbols now adapt local Lucide vectors,
following the current palette in normal/disabled states and at display density.
Native file, namespace and application icons retain their original owners.
`main_shell` checks all seven buttons fit EN/RU at normal/compact widths and
checks keyboard invocation against the active panel and cancellation.
`language_ui` checks names/tooltips after language, selection and shortcut
changes. These checks replace fixed bitmap-size/corner-pixel assertions;
safe icon changes are permitted by the redesign.

### Per-panel context and status (P0-09.2)

`source_context_menu` also inspects both pane owners, EN/RU table/icon lists,
mouse/keyboard Cancel at980×620 and640×400, with32 popup JSON reports; four
additional empty/Parent reports cover keyboard no-item admission. Source
OnContextMenu uses exact(-1,-1), origin for no operated items and otherwise the
focused item's rectangle. Qt substitutes visual-item centre/viewport coordinates
and clamps its popup to the screen. Action order/state is asserted separately
from widget geometry. Rendered Windows pixel acceptance remains unverified.

Reference: `Panel.cpp::Create/OnSize` gives every panel a status strip below
its list with four parts (`{220, 320, 420, -1}` Win32 boundaries), while
`PanelListNotify.cpp::Refresh_StatusBar` supplies selected/total, selected
size, focused size and focused date. `PanelMenu.cpp::OnContextMenu` calls
`CreateFileMenu(programMenu=false)` and anchors keyboard invocation to the
focused item. Qt uses four labels inside each panel status bar, with adaptive
widths for compact two-panel layout; the main-window bar displays operation
messages. `panel_shell` checks all four labels visible and independent at
980×620 and 640×400 and writes `panel-shell-*.json`. It also writes disk and
archive context popup JSON at normal/compact sizes; QActions are non-widget
objects, so their order/enabled state is asserted separately. System shell groups
are available through the Windows bridge and disabled on other platforms;
the independent SevenZip group remains disabled. The upstream Win32 resource/source
establishes structure, not rendered pixel dimensions on macOS.
Native popup fixtures observe the owned visible menu inside its nested loop,
after activating the window/list, for mouse, keyboard and compact peer consumers.
See the [current Cocoa receipt](../archive/qt-port/upstream-port-audit/p3-06-native-shell-2026-10-16.md).
