# Retired UI specifications

These specifications record the earlier Qt port before the File Manager
workflow changes. See [current UI specifications](../../ui-layout-contracts.md)
and [current behavior](../../7zfm-parity.md).

## Link (P2-05)

Reference: pinned FileManager/LinkDialog.rc and source OnInit/OnButton_Link.
Initial size follows visible controls and native minima. Root/footer use the
shared redesigned dialog layout;
Browse is20DLU wide. Empty Current target text reserves no row, and native
spacing separates paths and Link Type. The footer uses a native QDialogButtonBox
with content-sized Create link/Cancel actions.
The source resizable/minimize/maximize capabilities are requested through Qt
window flags; native window managers may suppress modal title-bar controls.
The resource has no context-help title-bar button.
Link from combo/browse precedes Link to combo/browse and current target text.
Link Type contains Hard→File Symbolic→Directory Symbolic→Junction→WSL in source
order. Create link is the default footer button; button ordering follows the
platform. Qt scrolls the
content when necessary; requested compact sizes expand to show the path inputs
and all link choices. Desktop-only Junction/WSL remain visibly disabled on
POSIX with a platform reason. Source radio/type choices and errors remain owned
by CLinkDialog; Qt controls bind original captions and LangSetDlgItems maps
IDCANCEL to its source language entry.
Radio captions use the existing220-pixel language wrapping binding so native
Windows EN/RU font metrics do not force horizontal clipping at360 pixels.
`link_dialog` writes EN/RU520×500/360×300 JSON and checks named controls after
scrolling and footer exposure, plus native-font initial frames. Review captures
return to the top after reachability probes. Historical native EN/RU reports and
rendered original initial states are recorded in the
[secondary-dialog receipt](../../archive/qt-port/upstream-port-audit/p3-06-secondary-dialogs-2026-10-16.md).
Native font metrics, compact scrolling and modal chrome prevent a claim of
complete rendered Windows pixel equality.

## Benchmark (P2-04)

Reference: pinned `CPP/7zip/UI/GUI/BenchmarkDialog.rc` and its source state bodies.
POSIX failed/empty affinity uses original System.h normalized processor fallback
in the hardware-threads leading CPU count and choice limits; that count must not
be zero or retain InitST's one when a larger processor count is available.
Windows retains its native source helper. These system failures do not change
grouping or controls.
The approved redesign places Dictionary/CPU threads above their fields, with
Memory alongside them. One table contains Operation/Result and the original
Size→Speed→CPU Usage→Rating / Usage→Rating columns. Compressing/Decompressing keep
Current/Resulting rows; Total Rating shares the numeric columns. Values and
numeric headers are right-aligned. Elapsed/Passes and the source pass limit
precede the log and CPU/version/system information.
The common footer holds Copy/Restart/Stop/Help/Close in normal mode and
Copy/Help/Close in total mode, using native button roles and content sizes.
Close calls the existing source cancellation/join workflow. Restart remains
initially focused in normal mode; total mode initially focuses its console.
Tab includes parameters, scroll viewport, nonempty log and footer actions.
Both source templates request minimize/maximize/resizable style and no
context-help title-bar button; Qt retains native modal-chrome conventions.
Both log and total console are read-only, use the system monospaced family at
the dialog text size and preserve horizontal/vertical scrolling without wrapping.
The normal log appears beneath the table when nonempty. Copy exports full
formatted source metrics, parameters, metadata and log; table columns retain
alignment when pasted as tab-separated text. Total Copy uses selected or full
console text and is disabled while empty. Neither copy action closes the window.
The normal report scrolls in both directions at compact sizes; keyboard arrows
scroll its focused viewport. System information wraps; a gutter protects it
from overlay scrollbars. The common footer remains exposed.

`benchmark_lifecycle` writes source-initial and normal/total EN/RU JSON at960×650 and640×400,
checks named labels/inputs after scrolling, footer exposure, keyboard traversal,
live language changes, long Unicode copy and source worker lifecycle. Lower system
rows may be below the compact viewport and remain reachable by scrolling.
Historical original Windows comparisons are retained in the
[secondary-dialog receipt](../../archive/qt-port/upstream-port-audit/p3-06-secondary-dialogs-2026-10-16.md);
they do not establish rendered parity for the approved redesign. Current scope
and platform validation are recorded in the [redesign audit](../../upstream-port-audit/ui-redesign.md).

## Shared name/mask dialog

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/ComboDialog.cpp/.rc` and
`BrowseDialog.cpp::Dlg_CreateFolder`. Implemented view: `ComboDialog.cpp`.

- The prompt precedes one editable combo, with OK/Cancel at the bottom.
  Initial Value remains visible after populating ordered Strings.
- The redesigned initial frame retains a font-scaled preferred width, while
  height follows the prompt, editable field and native actions. Long prompts
  can wrap and enlarge the minimum height. Explicit normal/compact resizing
  below remains a separate adaptation.
- Folder/file creation uses source title, prompt and default-name language IDs;
  selection masks use the same view with their own prompts.
- CPanel::SelectSpec owns the Select/Deselect titles, mask prompt, initial `*`
  and Cancel return. `source_selection` checks both languages/panes and standard/
  alternative marks, with32 current360×150/260×150 Combo reports. The same
  source owner handles an inactive-pane direct call without changing its peer.
- OnOK preserves raw text, including spaces and an empty value. Validation
  belongs to the calling operation. Cancel leaves the public Value unchanged.
- `combo_dialog` checks folder/file/mask views at 360×150 and 260×150:
  fitted prompt, reachable combo/footer, control order and separate buttons.
  JSON reports accompany the checks; screenshots remain optional.
- `source_create` exercises actual CreateFolder/CreateFile callers in both
  panes/languages and normal/flat views. Forty-eight current 360×150/260×150
  reports reuse this contract; creation keeps the source first-component focus,
  alternative marks and distinct failure-refresh policy. Cancel preserves the
  pre-dialog state. Source provenance and checks are in the
  [receipt](../../archive/qt-port/upstream-port-audit.md#p3-05--source-create-filefolder-scenario-2026-10-10).
- ZIP comment uses source `PanelOperations.cpp::ChangeComment` with the focused
  item name and localized Comment in its title, localized IDS_COMMENT2 prompt
  and the existing raw property value. `native_comment_ui` checks EN/RU at the
  same two sizes: current comment-en/ru-360/260 JSON reports preserve prompt,
  editable combo and separately reachable OK/Cancel. titleID=0 preserves the
  caller's dynamic title; source language ID0 is the application name.
- Original Set Comment Error/HRESULT remains the error presentation. On macOS,
  which omits QMessageBox window captions, the caption is the visible message
  heading and HRESULT detail is informative text. Tests exercise real writer
  refusal on both languages and ordinary/AES ZIP. Comment now uses the shared
  source panel HRESULT/caption helper; its source-owned refresh follows the
  error, while dialog Cancel returns before refresh. Existing Combo JSON and
  normal/compact geometry consumers retain the same controls and text contract.
- Filesystem comments use the same source dialog, focused name/title and
  IDS_COMMENT2 prompt. `filesystem_comment` checks existing value, EN/RU,
  normal360×150/compact260×150, File Comment and Ctrl+Z, OK and Escape, selected
  rows and unchanged file bytes. Eight filesystem-comment-en/ru-0/1-360/260 JSON
  show a fitted prompt, editable combo and separately visible/enabled OK/Cancel.
  The source FSFolder writer trims the accepted comment; ZIP's raw whitespace
  behavior remains separate. No new dialog or rendered Windows parity is claimed.
  The source Escape/Cancel path does not reload the folder. An externally added
  item while the dialog is open remains unpublished until explicit Refresh;
  selection, focus, provider and peer rows stay intact. Accepted Comment reaches
  the source reload phase even after operation failure. Load failure/cancellation
  leaves no stale rows and releases guards; a later explicit Refresh recovers.
  Preserve the original ignored reload-result policy in this operation caller.
  Inspect both panes in EN/RU at 980×620/640×400 after Cancel and terminal load.

Qt layouts replace OnSize. Plain-text wrapping handles Qt label rendering;
the native AddString behavior is adapted by restoring initial edit text after
Qt populates the combo. Windows rendering was unavailable; geometry establishes
structure/usability on the tested macOS Qt platform.

## Split and Combine dialogs

Reference: upstream `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/SplitDialog.rc`, `SplitDialog.cpp::{OnInit, OnOK,
OnButtonSetPath}`, and `PanelSplitFile.cpp::CApp::Combine` with `CopyDialog.rc`.
Implemented views: `SplitDialog.cpp` and the existing `CopyDialog.cpp`.

- Split puts the destination label above its editable combo, with Browse on
  the right. The volume-size label and editable preset combo follow below.
  The source filename is appended to the window title, as in OnInit.
- The source initial client template is304×112DLU. The volume combo remains
  left-aligned at96DLU, including after resize; Browse uses20DLU width with a
  native minimum hit target and the source `...` caption. Only the destination
  combo expands horizontally.
  Font mapping and Qt minimum sizes protect labels/footer on each platform.
- OK/Cancel remain at the bottom. Invalid volume input opens the source
  Critical/7-Zip/OK modal message and keeps Split open, including accept().
  OK or Escape dismisses only the message; raw input remains available for retry.
- Combine uses the original shared Copy dialog: destination, Browse,
  read-only scrolling summary and footer. The summary lists the count and
  size, source prefix, first two names, ellipsis when needed and final name.
- `volume_dialog` checks normal/compact Split at 480×240 and 340×240 in
  normal/error states, and Combine at 480×250 and 340×250 with 2/3/5 parts.
  It writes JSON and verifies order, reachable controls, fitted labels,
  separated path/Browse and separate OK/Cancel.

Qt layouts replace OnSize arithmetic; Qt native folder browsing replaces
MyBrowseForFolder. The earlier inline-error adaptation is removed2026-10-10;
no extra error control exists in the source resource. Separate current-build
message JSON checks Critical/plain text/OK and native auto-fit; macOS ignores
its caption. `language_dialog` also checks English/Russian prompts and raw input
across language changes. Windows volume/language consumers use native QPA/fonts
and the existing UI resource lock. The original parser and partial-failure
vector remain authoritative; invalid submission starts no operation or directory.
Reports establish structure/usability on macOS; a rendered Windows reference
was unavailable and pixel-level parity remains unverified.

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

## Panel address keys and flat view (P3-05)

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
Source scope/evidence are in the [receipt](../../archive/qt-port/upstream-port-audit.md#p3-05--source-delete-lifecycle-2026-10-10);
these probes do not establish rendered Windows pixel parity.

Find the affected dialog, command or plan ID with `rg -n` and read its contract
before changing layout/text/visibility. [Commands and reports](../../ui-layout-contracts/dialogs.md#commands-and-reports)
describes report generation; [UI reference tooling](../../ui-reference.md) owns the
resource-to-widget comparison procedure. These contracts specify controls and
geometry; [acceptance](../../ux-port-audit.md) records the completed validation scope.

## SFX Options group (P2-02)

Reference: pinned `GUI/CompressDialog.rc` Options control order SFX→Shared→Delete,
followed by Encryption, and original CompressDialog naming/method decisions.
The Add form retains its two columns and common scrolling content. New archive
SFX is enabled only with a compatible native module, 7z format and source-supported
method. Changing to ZIP clears SFX; toggling restores the source archive extension.
Module absence and existing-target update disable SFX with a reason.

`sfx_creation` writes `sfx-options---760/640` and `sfx-options-ru-760/640` JSON,
checks exposed controls/order and checkbox size hints at760×720/640×480, and
checks source name/format/method transitions. Reports reviewed2026-10-03 on
macOS, Linux and Windows:
all Options captions fit; Encryption below it uses the existing compact scroll.
Windows show/layout events settle before ensuring scroll visibility. Its EN
checkbox widths385 and RU421 meet all size hints; exposedBounds equal bounds
at normal/compact sizes. This is resource structure/usability evidence;
rendered 7zFM pixel parity remains unverified. P2-02 acceptance is in the audit.

P2-02 About integration restores original codec-error message initialization.
The retained catalog supplies source GetCodecsErrorMessage; a Qt critical message
substitutes the original MessageBoxW/MB_ICONERROR. `help_source` checks exact source
text and exposed/wrapped label with600×300/400×250 resize requests and writes
`about-codec-error-600/400` JSON. About's product identity follows the
[current About contract](../../ui-layout-contracts/external-tools.md#about).
No rendered Windows reference is claimed for the standard Qt error message.
The standard QMessageBox auto-fit resolves both requests to400×121 on this host;
these reports prove fitting source text, not two distinct error-window sizes.

For resource-derived comparison and reviewed C++ states, see
[upstream UI references](../../ui-reference.md) and `$sz-ui-parity`.
`ui_parity_copy` and `ui_parity_password` check fresh reports against pinned
resource baselines.

## Help and About: shared frame (redesign, 2026-10-18)

Help retains the original bundled HTML, links, fragment navigation, history,
text and horizontal/vertical scrolling. Only its Qt window uses the shared
content/footer spacing; Back follows actual browser history and Close keeps
its native role. Initial focus enters the reading area. Language changes keep
the current topic, and missing topics retain the existing warning.

About retains Sevenzip identity/version/project link and complete 7-Zip
attribution, release, copyright, license and homepage. Its content and footer
use the shared frame, with Close as the visible confirmation caption. Return
still accepts the dialog; Escape and window close preserve their existing result.
Native palette changes must restyle links in the existing window. Internal
attribution padding remains independent of the outer dialog frame.

## Shared ListView: Properties and Folder History (P3-06)

Reference: pinned FileManager/ListViewDialog.rc and OnInit/OnEnter/OnOK callers.
The approved redesign uses the shared dialog layout and native content-sized
buttons. Reports have Copy/Close; Folder History has Open/Cancel. Preserve
one-column hidden History header and the blank, resizable two-column Properties
header. Copy uses the full selected values, or all rows when nothing is selected;
an empty report cannot overwrite the clipboard. Native button ordering is retained.
`list_view_dialog` writes EN/RU
source-initial and540×360/360×240 reports, checking exposed list/footer without
changing stored full values, clipboard/detail or acceptance behavior.

## Workspace design before the removal of tabs and Sidebar

Главное окно показывает вкладки, затем навигацию и затем файловые действия.
Путь по умолчанию представлен сегментами; полная строка открывается отдельным
режимом и через сочетание перехода к пути. Сегменты и история используют тот же
native navigation owner. Escape отменяет черновик. Длинные сегменты сокращаются
с доступным полным путём в подсказке; ранние предки доступны через меню.
Граница архива обозначается в пути. Sidebar начинается у списка ниже chrome,
скрывается без смены места и не дублируется во второй панели.
Сегменты получают готовые цели и границы контейнеров от native-иерархии,
а не вычисляют их из текста редактора. Отказ отправленного адреса оставляет
черновик доступным для исправления и сохраняет прежнее содержимое. Фоновое
обновление не превращает черновик в успешно открытое место. Native-переход к
самому глубокому существующему родителю по-прежнему считается успехом.
Активная вкладка округлена сверху и соединяется с фоном навигации;
иконка места и кнопка закрытия справа остаются частью обычного управления
вкладками. Общая рамка объединяет сегменты пути и режим редактирования.
Контурные значки вкладок различают папку и открытый архив по типу источника;
имя обычной папки с архивным расширением не меняет её значок на архивный.
Изменение размера и переход одна→две→одна панели сохраняют видимый текущий
сегмент; скрытый sidebar не резервирует ширину у второй таблицы.
При нехватке ширины вкладка сокращает название, затем оставляет значок;
полное имя и путь остаются доступны. У узкой вкладки крестик виден для активной
вкладки и при наведении. Наведение соответствует месту указателя после прокрутки
и изменения размера полосы. Средняя кнопка закрывает указанную вкладку по тем же
правилам, что её крестик. Стрелки и Home/End в полосе переключают реальные места
и сохраняют фокус полосы, включая восстановленные вкладки с загрузкой списка.
Новое действие пользователя во время загрузки отменяет прежний запрос фокуса.

Новый файловый список начинает с колонок Имя, Размер, Тип и Изменён.
Сохранённая пользователем раскладка имеет приоритет; дополнительные native
свойства доступны из меню заголовка. Имя получает свободную ширину, пока
пользователь не изменит её вручную. В узкой области остальные колонки доступны
горизонтальной прокруткой. Размер выровнен вправо; длинные имена сокращаются
справа. Обычная строка имеет высоту 28 логических пикселей с увеличением для
крупного шрифта. Мягкое выделение строки сохраняется при переходе фокуса;
контур текущей строки виден у сфокусированного списка. Явная пользовательская
настройка выделения только имени сохраняется.
В совместимой альтернативной схеме отмеченная строка имеет сплошной мягкий фон;
выделенная отмеченная строка сохраняет фон выделения и отдельную отметку слева.

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
Начальные записи подчиняются тем же правилам; удалённые записи не появляются
повторно при запуске. Отдельная группа расположений открывает существующие
native namespace, а общая боковая навигация действует на активную сторону.
Подсветка текущего места сохраняется при перемещении клавиатурного курсора;
контур фокуса показывает отдельную строку. Запрос перехода сам по себе не меняет
текущее место. Окна сохраняют независимую подсветку при общем списке закладок.
Смена языка сохраняет текущее native расположение. Сокращённые названия избранного
имеют подсказку с полным именем и путём. Команда открытого меню относится к той же
закладке после перестановки; удалённая другим окном запись не затрагивает соседей.
Исходное деление двух панелей даёт равные файловые области с учётом sidebar;
ручное деление сохраняется при изменении размера, скрытии мест и восстановлении.
Перетаскивание обычной папки добавляет запись без перемещения исходных файлов.
Временный экспорт содержимого контейнера не становится сохранённой папкой.
Обычный режим выделения действует по умолчанию; явно включённая пользователем
совместимая альтернативная схема 7-Zip сохраняет свои исходные отметки.

Основные назначения определяет [схема клавиш](../../ui-shortcuts-design.md).
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

## Legacy toolbar defaults

Toolbar defaults retain source IsDialogSizeOK(200,200): when the dialog-unit
extent exceeds the available work area, default-mask bit31 disables labels.
Explicit saved labels/icons override this classification. Qt font metrics and
available screen geometry supply the original calculation; compact client
width alone does not change the saved/default policy. Actual large-font tests
check QAction/style without a rendered-geometry claim. Ordinary980×620/640×400
main-shell reports retain toolbar grouping/order, control visibility and menus.
