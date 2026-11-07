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
В «Вид» представление и сортировка находятся в подменю; явное направление
сортировки отражает native порядок. Меню предлагает имя, тип, дату и размер.
Прежнее состояние «Без сортировки» открывается с сортировкой по имени.
Группы мест/панелей, навигации и обновления
следуют актуальному каталогу дизайна в master. В представлении остались таблица
и крупные значки. Плоский вид, настройки отображения времени и настройки
старых панелей инструментов удалены. Эти изменения заменяют прежнее требование
плоского RC-порядка. Forward пока остаётся открытым разрывом с новым контрактом.
Файловый список обновляется автоматически. Команда «Обновить» доступна вручную.

Группировка применяется после исходного отбора/проверок доступности и сохраняет
command ID, состояние и обработчики. Главный File-menu сохраняет QAction;
контекстный popup сохраняет независимые копии. Системные подменю сохраняют
внутренний исходный порядок и lifetime. Повторное открытие не дублирует команды,
новые неизвестные расширения также остаются доступными в редкой группе.
Контекстное меню содержит одно подменю алгоритмов контрольных сумм. Общие
команды вкладок/окон не становятся файловыми командами. Архивные действия
сохраняют свои подписи при запуске, смене языка и повторном открытии меню.
Временно скрытая команда появляется на текущем языке. Устаревшие пункты
расширений исчезают, пока источник заменяется и их контекст недоступен.
`source_context_menu` проверяет реальное создание папки через вложенное «Ещё»
в вызвавшей меню панели. Черновик имени сохраняется при обновлении папки
и закрывается после перехода в другое место. `view_menu` выбирает представление
через подменю и проверяет состояние обеих панелей. EN/RU normal/compact reports включают
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

Строка действий панели содержит создание папки, Cut, Copy, Paste,
Rename и Delete. Она действует на свою панель и использует существующую
проекцию доступности. Архивные Add/Extract/Test находятся в меню и контексте.
Подписи, accessible names и подсказки следуют текущему языку и сочетанию;
обновление выбора не возвращает устаревший перевод. Copy/Cut передают реальные
файлы, Paste использует действующий сценарий переноса с конфликтами и отменой.

Opening View runs the source activation before display. The popup retains its
source for command delivery, checked state and availability; a removed source
cannot redirect a pending choice. Metadata absent from the menu has no false
Name check. The retained native time commands still update examples and UTC,
but their hidden submenu is outside the new visible menu contract.
`view_menu` checks both panes and EN/RU at normal/compact geometry and emits
opt-in reports for the actual visible menu groups. Navigation and visible file
views must fit; reports do not establish rendered Windows parity.
For the closed address's decorative image, verify the complete painted glyph.
Qt6.12 Cocoa can clip one transparent pixel of its QLineEdit side-widget padding
while exposing the full16×16 logical icon. Reports retain strict widget bounds
and additionally record `iconBounds`/`iconFullyVisible`; clipping or hiding the
glyph remains a failure. A source selection's pending list-focus request survives
popup visibility until Hide, while a newer editor/peer interaction supersedes it.

Reference: `CPP/7zip/UI/FileManager/resource.rc::IDM_MENU`,
`App.cpp::g_ArchiveButtons/g_StandardButtons` and `MyLoadMenu.cpp` at
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Главные меню — File/Edit/View/Favorites/Tools. Встроенная справка исключена
из главного окна по запросу пользователя; About остаётся в Tools
(на macOS — системная роль About). File сохраняет исходные command IDs и
обработчики, а архивные действия собраны в подменю. Незавершённые команды
остаются недоступными. У каждой панели своя строка файловых действий.

Контурные значки используют локальную адаптацию Lucide с текущей палитрой и
масштабом экрана. Нативные файловые/namespace/application icons сохраняются.
Кнопки поддерживают клавиатурный фокус, нативную индикацию и обычный системный
шрифт. В широкой однопанельной компоновке действия показывают подписи;
в компактной и двухпанельной остаются значки с локализованным accessible name
и tooltip. Normal/compact проверки охватывают EN/RU,
одну→две→одну панели, видимые действия, навигацию и список. Это контракт Qt
и согласованного редизайна; совпадение пикселей с оригинальным Windows не заявлено.

### Per-panel context and status (P0-09.2)

`source_context_menu` also inspects both pane owners, EN/RU table/icon lists,
mouse/keyboard Cancel at980×620 and640×400, with32 popup JSON reports; four
additional empty/Parent reports cover keyboard no-item admission. Source
OnContextMenu uses exact(-1,-1), origin for no operated items and otherwise the
focused item's rectangle. Qt substitutes visual-item centre/viewport coordinates
and clamps its popup to the screen. Action order/state is asserted separately
from widget geometry. Rendered Windows pixel acceptance remains unverified.

The current design uses one window-owned strip for the active pane, including
two-pane mode. It shows explicit selection/counts and a known total size;
current-item metadata remains in the file view and Properties. Loading and
source-bound transient feedback share the left slot, hiding the selected size.
Expiry restores the current summary. Read-only/limited-access state belongs to
the source navigation context and remains visible independently of messages.
The strip follows font metrics, elides long summaries with full tooltips, and
retains space for known sizes at compact widths. Sizes and dates consume typed
native values through shared regional presentation, independently of UI language.

Historical reference: `Panel.cpp::Create/OnSize` gives every panel a status strip below
its list with four parts (`{220, 320, 420, -1}` Win32 boundaries), while
`PanelListNotify.cpp::Refresh_StatusBar` supplies selected/total, selected
size, focused size and focused date. `PanelMenu.cpp::OnContextMenu` calls
`CreateFileMenu(programMenu=false)` and anchors keyboard invocation to the
focused item. Qt uses four labels below each panel's file area, with adaptive
widths for compact two-panel layout in that earlier port; the main-window bar displayed operation
messages. The selection part reserves room for both counts and elides its
caption from the left; the complete text remains available in its tooltip.
`panel_shell` checks all four labels visible and independent at
980×620 and 640×400 and writes `panel-shell-*.json`. It also writes disk and
archive context popup JSON at normal/compact sizes; QActions are non-widget
objects, so their order/enabled state is asserted separately. System shell groups
are available through the Windows bridge and disabled on other platforms;
the independent SevenZip group remains disabled. The upstream Win32 resource/source
establishes structure, not rendered pixel dimensions on macOS.
Native popup fixtures observe the owned visible menu inside its nested loop,
after activating the window/list, for mouse, keyboard and compact peer consumers.
See the [current Cocoa receipt](../archive/qt-port/upstream-port-audit/p3-06-native-shell-2026-10-16.md).
