# Panels and file lists

[Все UI-спецификации](../ui-layout-contracts.md). Разделы сохраняют исходные требования,
якоря и границы датированных свидетельств. Новые результаты проверок —
в [source audit](../upstream-port-audit.md).

Обычная схема выбора — одиночное выделение, дополнительное с модификатором,
диапазон с Shift и рамка на пустом месте. Таблица и значки используют один выбор;
открытие, переименование, сортировка и команды действуют на текущие source items.
Рамка не подменяет файловый drag, начатый на выбранном объекте. Переключение
вкладки сохраняет её текущий объект, выбор и прокрутку. Явно включённый совместимый
альтернативный режим 7-Zip сохраняет независимые отметки.

Состояние Cut приходит от общего файлового буфера и относится к конкретному
операционному пути filesystem-записи. Сортировка, обновление и смена вида его
сохраняют; одинаковое имя в другом месте и цель символической ссылки его не
наследуют. Отмена Move сохраняет Cut, успешный Move или замена буфера снимают
его. Приглушается содержимое элемента; выбор, текущий элемент, отметки и
доступность действий остаются независимыми. Текущий элемент обозначает запись,
даже если последней активной ячейкой таблицы была колонка размера или даты.

Крупные значки имеют собственную область изображения и до двух строк подписи.
На macOS выделенная подпись использует акцентный фон, текущий элемент — рамку
вокруг изображения. Размеры учитывают шрифт; усечённое имя доступно в подсказке.

### Icon views

The file panel offers the table and Large Icons. Older preferences for Small
Icons or List open the table. Switching views preserves the shared selection
and brings the current file into view.
Large Icons centers wrapped labels below the icons. Cell geometry accommodates
the platform icon and two lines of the active font; longer names remain available
through the full-path tooltip. Font/style changes recalculate geometry, and
resizing reflows entries. Native focus/text margins must fit inside each cell.
Commands that reveal a focused item scroll the visible list, including when
the table sharing its model is hidden.

Check normal/compact windows and an enlarged list font through `ui_layout`;
`icon-layout-*` reports describe the surrounding widgets. Item paint geometry
is checked separately by the suite. `panel_selection` checks explicit scrolling
after Insert. The list activation and settings suites cover selection by single
click and opening by double click. The table highlights the full selected row
and displays its columns without a grid.

### Rename validation error (P3-05, 2026-10-15)

Rename edits the name inline in the active table/icon list. On macOS, the editor
and its native focus ring must remain inside the viewport, including the first
icon column; resize and Escape retain the source selection and file bytes.
Automatic refresh waits for inline Rename to finish in its panel. The other
panel continues to refresh while the name is being edited.

Rename rejects an empty file or archive entry name and other invalid filesystem
names with the shared HRESULT error. It uses the Critical icon, the source
7-Zip caption (macOS omits native captions), plain E_INVALIDARG text and OK.
It keeps the original name/selection and peer
state without loading rows. `panel_rename_state` covers both panes and EN/RU;
`rename-invalid-*` Qt reports check message and OK bounds at requested
440×220/340×220. macOS backing-widget reports fit400×116. Native Cocoa text/icon
checks do not use its hidden backing-widget geometry as rendered native proof.

Archive Rename accepts safe relative paths and rejects absolute paths and parent
traversal before writing. When a relative Rename leaves the result under the
current folder, the panel selects the renamed item or its first visible parent.
ZIP/7z file and implicit-folder scenarios
inspect the table at980×620/640×400 in eight `archive-rename-relative-*` reports.
Their geometry checks do not establish rendered Windows pixel parity.

<a id="refresh-focus-and-alternative-marks-p3-05"></a>

### Refresh focus and selection

Refresh restores focus and selection independently. A focused, unselected row
stays unselected, and the focused-size/date fields stay empty when the pane
has no selection. Check both panes, EN/RU and active/inactive ownership at
980×620 and 640×400 through the `source-refresh-status` widget reports.
Rebinding the view preserves selection notifications for the current provider.

### Archive directory CRC projection (P3-05)

Source Agent::GetProperty and AgentProxy::CalculateSizes supply directory CRC
to the existing list column and Properties collector. Native ZIP/7z checks cover
both panes, EN/RU and normal/flat, preserving source sorting/marks/Refresh,
peer provider and input/archive bytes. Main-window reports use980×620/640×400;
Properties uses the existing ListViewDialog.rc list and OK/Cancel grouping at
540×360/360×240. Verify the complete table/viewport and separate exposed footer,
then Escape rejection without changing the provider or selection. At compact
width, long metadata values remain reachable through the existing scrollbars.
Qt collector geometry is separate from native Windows Properties execution and
rendered upstream parity. `native_column_workflow` writes directory-crc and
native-columns-crc reports; the source audit owns dated platform evidence.

### Source panel creation (P3-05.15)

Native property failures during initial creation and OpenAsArc_Index retain the
source callers' ignored RefreshListCtrl result, without an extra error dialog.
The actual filesystem OpenItem caller must hand off its outer guards too;
after native recovery, filesystem navigation and OnTimer must still publish a
new private file. Direct OpenAsArc_Index alone does not prove this caller.
OnReload reports every non-S_OK result, including S_FALSE and E_ABORT, after
redraw and notification guards are restored. This policy also applies beyond
the asynchronous row boundary. A failed list is empty and enabled; a subsequent
Ctrl+R restores rows on the same native provider. Inspect failed and recovered
states in both panes, EN/RU, table/icon and standard/alternative selection at
980×620/640×400, with archive and peer bytes unchanged.
For real filesystem Copy/Move to a separate output folder, the archive peer's
unconditional reload uses that same policy. Its error precedes source selection
cleanup; terminal focus returns to the source view, output bytes are correct,
and the archive provider/bytes survive failure and retry. Check the same pane,
language, view and selection modes with a301-row native peer.

OnCreate starts list publication after its controls are constructed and before
the next panel opens. Initial and late second-panel creation must finish with
both lists/addresses visible and enabled, correct source modes, restored guards
and unchanged files/archive bytes. Check EN/RU table/icon and standard/alternative
selection at980×620/640×400, filesystem/transport/native ZIP/7z and2/301 rows.
Ctrl+R must actually publish and preserve marked/focused names; hide/show retains
the created peer. Original initial creation order starts with panel0 unless an
archive-only main-panel request chooses panel1; single-panel late creation covers
either role. Native rendered Windows parity remains unverified here.
Ordinary startup must use the requested main path rather than its saved path,
including a right main panel created second. Initial focus waits for the scans
to finish. Subsequent explicit address/list input or a public archive request
must retain the peer's focus instead of being overwritten by startup completion.
When a first scan completes during the second panel's binding, each source provider
must match its own visible rows. Public archive modal progress must return focus
to the selected address control, including its Qt combo focus proxy.
Showing a saved peer immediately after window show must retain its path and
source rows; pending initial focus cannot change projection during peer Create.
Source FM.cpp::MoveSubWindows fixes panel0 on the left and panel1 on the right,
including archive-first and late creation from an initially single right panel.

<a id="file-list-drag-target-p1-16-wip"></a>

### File-list drag target (P1-16)

Внешний левый Drop одной папки или распознанного архива на файловую панель
предлагает открыть этот путь, создать архив либо отменить действие.
Вопрос показывает имя объекта как plain text; Cancel — default и Escape.
Выбор создания сохраняет текущую папку назначения и открывает существующую
форму Add. Открытие адресуется панели Drop. Отмена сохраняет исходные файлы,
путь и доступность команд. Неподдерживаемые типы, несколько путей,
archive target и внутренние/native transfers сохраняют исходный сценарий.
Кнопки используют стандартные роли/геометрию QMessageBox и переводы Qt;
порядок определяется платформой. Свидетельства — в
[аудите изменения](../upstream-port-audit/startup-drop-choice.md).

Reference: pinned `PanelDrag.cpp::PositionCursor`, its `LVIS_DROPHILITED`
paint state, native right-drop popup pairs/masks and Copy-to-archive confirmation.
External middle-button entry uses the same source popup; source transfer flags
retain left/right precedence. Button release at Drop must not erase the entry
decision. `drag-middle-menu.json` records the current-build popup geometry.
Qt preserves per-panel path→view→status order. Hover highlights the folder through
a separate paint role without changing selection; DragLeave/Drop removes it.
Refresh clears the source panel's drop-folder name/index before loading or
restoring rows, including failure/Cancel. A subsequent leave does not address
an old row number in the new list; a new hover resolves its current folder.
Source-panel and disabled-view drops are rejected. Hover/SingleClick stays
suspended during the source operation, including archive byte preparation.
`drag_drop_ui` covers table/icon targets in both directions, popup Copy/Cancel,
archive Yes/No/Cancel and real source cancellation. Reports `drag-*.json` at
980×620/640×400 keep both containers, paths, current views and status strips
exposed. The reviewed resource/source establishes structure and states; no
Windows rendered reference is available for pixel fidelity.

For layout work, read the affected view's specification below, run its focused
test, and inspect the generated JSON. Screenshots are a fallback for fonts,
icons, styling, or defects that geometry cannot explain. These checks establish
structure and usability on the tested Qt platform, not pixel-level Windows parity.

<a id="alternate-streams-namespace-p2-06-active"></a>

### Alternate streams namespace (P2-06)

The File menu has no separate stream browser command. Explicit stream paths
remain usable for supported file operations. The source reference for path
binding is the original AltStreamsFolder and Agent CProxyArc2 folder/parent
binding. Keep the existing main shell and file
list order: address, Name/Size/Packed/etc. columns and per-panel status. A colon
folder prefix names an ADS namespace, not a filesystem subdirectory. Stream
rows are files; no synthetic Unix xattr rows. Parent returns to the original
file owner. Filesystem CreateFolder/import remain disabled because their
original AltStreamsFolder methods return E_NOTIMPL.

`alt_streams_command` writes Windows filesystem `alt-streams-{language}-{size}`
JSON in EN/RU at 980×620 and 640×400. `alt_streams_archive_ui` writes
`archive-alt-streams-{language}-{size}` for the native archive namespace at the
same sizes. Both require the address and table to remain fully visible.
Current macOS/Linux/Windows archive and Windows filesystem reports retain
toolbar→address→table→status order, with address, complete table and status
exposed at both sizes in both languages. Sixteen current JSON were reviewed.
These checks establish Qt structure/usability, not rendered 7zFM pixel parity.

### Two disk panels (P0-08.1)

Reference: FM `App.cpp::SwitchOnOffOnePanel`, `FM.cpp::MoveSubWindows`,
`PanelKey.cpp::OnKeyDown` and `resource.rc` View → 2 Panels (F9) at
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. The panels sit left/right
under the shared toolbar, with an address row above each file list. F9 hides
the inactive panel and expands the active one; Tab moves list focus between
visible panels. `panel_key` writes `two-panel-disk-table-640.json`,
`two-panel-disk-980.json`, `two-panel-disk-640.json` and
`one-panel-after-f9-640.json`. It checks both addresses and active lists are
fully visible at 980×620 and 640×400. JSON confirms Qt structure and clipping
only; rendered Windows parity remains unverified.

`panel_folder_sync` extends this contract with Alt+Shift+Up (same folder) and
Alt+Shift+Left/Right (focused child or parent). The modern primary navigation
keys are defined in [the shortcut scheme](../ui-shortcuts-design.md).
Source focus remains in its panel;
the destination's pending list is disabled until source indices, sorting and
selection are coherent. Its address remains available during Scan. Commands
on the ready source panel remain available. Each panel publishes independently;
only destination replacement or close cancels that destination's pending list.
Current reports `alt-folder-{same-small,sub-left-small,sub-right-large}-{980,640}`
and `alt-archive-{small,large}-{980,640}` check both address rows and lists.
For a directly shared native archive folder with no destination parent stack,
the address presents its native relative folder prefix without a leading
archive separator. The archive resource retains the engine path separately.
Rendered Windows parity for these states remains unverified.

### Mixed and restored panels (P0-08.2)

The same 7zFM panel structure applies when either side displays an archive.
`panel_key` writes `two-panel-archive-980.json`,
`two-panel-archive-640.json`, `archive-hidden-panel.json`,
`two-panel-restored.json` and `hidden-left-restored.json`. Geometry checks
require both visible address rows and lists to fit at 980×620 and 640×400,
and check the saved splitter coordinate after reopening. A compact QSplitter
clamps its coordinate to the panels' minimum sizes. JSON covers Qt grouping
and clipping; rendered Windows parity remains unverified.

## Archive metadata cells

Reference: pinned `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/PanelListNotify.cpp` property-to-text branches and
`CPP/7zip/UI/Agent/Agent.cpp` typed GetProperty. Unknown size/packed metadata
must leave the corresponding file cell empty; defined zero must display `0`.
Directory size cells retain the current empty presentation. The hidden transport
columns, view modes and navigation grouping are unchanged. P0-06.2 adds the
source property catalog to the visible table columns.

`panel_selected_state` checks archive cells in the table and large icons at
980×620 and 640×480, verifies central/active view visibility, and writes
`archive-runtime-mode*-*.json` under `ui-reports/` in the selected CMake
build directory. Review geometry,
control order and clipping in those current-build reports. These establish Qt
usability/typed value behavior, not rendered Windows pixel parity. The later
`panel_columns` reports cover source property columns and their header controls.

Large directories load in the background. Navigation and the list stay
visible during loading, and list commands return on completion or failure.

`panel_selected_state` writes `filesystem-runtime-*` reports at
980×620 and 640×480. Filesystem size cells use native typed metadata: uncalculated
directory size is absent, file zero is defined, and POSIX links display their own
length. Source item indices are independent of sorted rows. Path controls, shared
view stack and status bar keep the existing order and must remain fully visible.

### Source sorting in the file list (P0-06.1)

Reference: pinned `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/PanelSort.cpp::CompareItems/CompareItems2`,
`CPP/7zip/UI/Agent/Agent.cpp::CompareItems/CompareItems2/ComparePrefixes` and
`PanelItems.cpp` list state. Directories group before files in either direction;
the chosen typed property sorts first, then base name, flat prefix and source
index. The Agent interface compares flat prefixes naturally: equal `common.txt`
names under `dir2` and `dir10` place `dir2` first. The generic property fallback
for providers without IFolderCompare retains lexical BSTR order. Raw archive
properties pass the original raw flag to the provider comparator. Header and sort action
remain in sync.
Sort commands use the source property toggle; a newly chosen Size, PackSize,
CTime, ATime or MTime starts descending, all other properties ascending. Qt
headers use the same source default. A repeated PropID toggles even through
another generic/raw section; the source catalog owns its first-match raw flag.
The Qt header completes its automatic mouse sort before the source handler's
result is projected. Explicit programmatic direction requests remain explicit.
Sorting retains native selected indices
and ensures the focused row is visible, including a row moved outside the old
viewport. Reload retains the chosen property and direction.
The source restoration phase sorts after matching the focused name and before
choosing a fallback row by position. It publishes all four status parts after
selection restoration even if Qt emits no focus/selection signal. The owning
pane's labels update without switching focus or altering the peer's status.
The list, address, toolbar and status keep their existing order and stay
unclipped at 980×620 and 640×400. `panel_sort` writes
`source-sort-flat-980.json` and `source-sort-flat-640.json`; these are current Qt
structure reports, not a rendered Windows comparison. P0-06.2 covers raw-column
sorting and the column header menu; time precision controls remain P0-06.3.

### Source property columns (P0-06.2)

Reference: pinned `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/PanelItems.cpp::InitColumns/SaveListViewInfo` and
`PanelListNotify.cpp::GetItemText`. The header exposes folder-type property
columns, preserving visibility, width, order and sort between openings. Name
cannot be hidden. The shared list remains fully visible in all four modes at
980×620 and 640×400. `panel_columns` writes eight `source-columns-*` JSON
reports for those states. These establish the Qt structure and usability; they
do not establish rendered Windows parity. Native raw-metadata coverage is
recorded separately under P1-10.

### Time precision and UTC (P0-06.3)

The current workspace menu hides this legacy submenu under the agreed
[menu contract](shell.md#согласованная-группировка-меню). The native command,
formatting and persistence behavior below remains compatible; these historical
popup reports do not establish acceptance of the current visible menu.

Reference: pinned `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`MyLoadMenu.cpp` View/Time popup construction and command dispatch,
`PanelListNotify.cpp` property-to-text branch, and unchanged portable
`PropVariantConv.cpp`/`PropIDUtils.cpp`. The popup presents five current-time
examples (day, minute, second, 100 ns, nanosecond) and a separate UTC toggle.
The parent View menu fills these examples before the child popup opens.
Changing either setting redraws visible time columns in place; day-level ties
still sort by source `FILETIME`. `panel_columns` checks the menu text and state,
filesystem/archive cells, persistence and fully visible list at 980×620 and
640×400 in `source-time-*.json`. These Qt reports do not establish rendered
Windows parity.
