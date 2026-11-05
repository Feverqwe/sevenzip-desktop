# Main-menu language binding — 2026-10-17

## Workspace menu integration regressions — 2026-11-05

Пользовательские снимки после `da86ecee` установили два дефекта интеграции.
Команды workspace без native ID попадали в `CFileMenu` как нулевые записи,
а контекстный Qt adapter трактовал каждую как CRC submenu. Отбор теперь
исключает workspace actions на той же границе, что главное File-menu.
Переводчик считал toolbar command IDs идентификаторами language resources;
для них используется исходный `SetButtonText` с уже существующим соответствием
command/caption. QAction identity, shortcut, enabled state и dispatch сохранены.

Дополнительная сверка исходников нашла два соседних пропуска. Перевод только
текущего дерева оставлял старые подписи у временно отфильтрованных File actions;
теперь тот же `MyChangeMenu` обрабатывает весь исходный набор до Qt bindings.
Ранний выход при notification/operation guard освобождал shell context, сохраняя
его видимые команды; теперь динамические строки очищаются без запроса provider.
Повторное открытие в ready state использует прежний native projection owner.

До изменения повторно прочитаны полные исходные `MyChangeMenu`, создание
File-menu и `App.cpp` button/caption mapping, текущие callers и source adapters.
Новая source adaptation ограничена Qt menu presentation/reload boundary.
Новых upstream units, импортов и изменений original hashes нет.

Linux x86_64: source_context_menu, language_ui, file_menu_policy, shell_context,
main_shell — PASS. Реальные подменю проверяются EN/RU, повторно, в обеих панелях
и table/icon видах. Проверки охватывают единственный CRC submenu, Archive
captions, возврат отфильтрованного Diff после смены языка и отсутствие старых
shell commands в удерживаемом native list callback. Исправлен устаревший тестовый
ввод в скрытую строку пути: fixture сначала открывает видимый редактор и проверяет
фокус. Это исправление достоверности проверки, а не изменение навигации продукта.
Возврат Diff проверяется через реальный Settings Apply с обновлением видимости,
а не одной прямой записью registry/settings. После завершения list callback
проверяется повторное открытие обычных групп File-menu.
macOS arm64: source_context_menu/language_ui/main_shell/startup smoke — PASS;
после уточнения Settings-Apply и ready-menu проверок повторены language_ui и
main_shell — PASS. Native Cocoa: EN/RU context menu, раскрытые Archive/Checksums
и More, повторные мышь/клавиатурные открытия — PASS в светлой и тёмной теме.
Снимки просмотрены: одно CRC submenu и правильные подписи архивных действий.
Отдельные Cocoa cases startup/live-language, Settings Apply и guard→ready — PASS.
Windows native этим проходом не проверялся. Это завершённый menu integration
scope; новая задача сквозной перепроверки остального UI остаётся активной.

## Workspace View projection — 2026-11-05

Актуальный контракт и rendered меню из `master` (`ed197ad2`) исключают старые
режимы и toolbar/time настройки из нового View. Их native команды и сохранённые
настройки остаются доступны существующим владельцам. Qt показывает согласованные
варианты представления, явное направление сортировки, группы панелей, навигации
и обновления. Локальные EN/RU подписи не меняют command IDs и shortcuts.

Повторно прочитаны native `PanelSort.cpp::SortItemsWithPropID`, его header callers,
`App.h` auto-refresh helpers, dispatch и полные View activation/translation
branches. Для `MyLoadMenu.cpp::GetSortControlID` прочитан полный исходный helper
из записанного `third_party/7zip/source.tar.xz`: его default отмечает Name для
других метаданных. Узкая Qt presentation адаптация возвращает отсутствие пункта,
сохраняя реальное свойство и native сортировку. Направление использует тот же
native comparator, raw-property flag и selection owner. Нового внешнего импорта
и изменения original baseline hashes нет.

Popup удерживает исходный CPanel через guarded identity. Доступность, radio state
и вызов команды разрешают тот же сохранившийся bound source; смена проекции не
перенаправляет выбор в соседнюю панель. Удалённый или уже не bound источник
не получает действие. Снятие popup-контекста отделено от доставки QAction.
Saved places использует общий UUID закладки вместо сохранённого пути удалённой
записи. Auto-refresh публикует native flag всем зарегистрированным окнам и
синхронизирует новые/перенесённые tab bindings, без отдельного watcher.

Проверка автообновления выявила попытку передать фокус скрытому address editor
при отключении списка. Общий workspace focus owner удерживает видимый tab strip,
затем возвращает исходный FileView при сохранённых вкладке и input request.
Новый пользовательский ввод отменяет отложенный возврат.

Focused Linux проверки связанной проекции и фокуса — PASS. macOS offscreen
меню, сортировка, язык, места, вкладки и автообновление — PASS. Native Cocoa
перенос/сессия/автообновление — PASS. Native main/View/направления сортировки
прошли в light/dark; просмотрены реальные PNG корня, представления и сортировки.
EN/RU, normal/compact и обе панели входят в menu producer. После последующего
уточнения projection guards переноса повторены его прямые Linux и Cocoa проверки;
неизменённая геометрия меню повторно не снималась.
Общий Forward action и forward history пока отсутствуют: это открытый разрыв
с новым контрактом навигации, не закрытый изменением состава меню. Новые Quit
и independent-operation lifecycle контракты также не входят в эту итерацию.

## Bound caption resolution and Qt text delivery — 2026-11-05

Reviewed the complete native LangSetDlgItemText, standard control mappings,
colon/remove-colon and window-caption helpers, their current adapter and the
binding/reload callers. The complete original LangUtils.cpp hash matches the
existing baseline at `b88c6bc09339de32f7d18bbab900686b244f42ba`; no source import,
revision or native language algorithm changes.

Within the existing language-control owner, translation of bound properties
and native/resource caption resolution now have separate private functions.
Text delivery retains tooltip-first ordering, the no-caption early return,
suffixes, wrapping, mnemonic removal for accessibility/placeholders and the
existing QAction/label/button/group/window destinations. Resource fallback,
standard-control precedence, arguments and live reload retain the same bodies.
No metadata, public interface, text, geometry or reload policy is added.
Existing language, dialog and live-reload regressions cover observable behavior;
no test depends on these private functions or their placement.

## Single menu population — 2026-11-04

Compared the original FileManager resource menu, MyLoadMenu translation and
activation handlers, and application creation order with the Qt action/menu
construction, toolbar state, first-panel hookup and command/language consumers.
Complete original MyLoadMenu.cpp/.h, App.cpp and resource.rc/.h hashes match
the baseline at `b88c6bc09339de32f7d18bbab900686b244f42ba`. No external
source, resource, algorithm or revision is imported.

Qt now creates the reusable actions and submenus without first inserting them
into provisional main menus. The first panel populates those menus directly
in the existing final order, removing the intermediate population and reset.
Action creation order, QObject parents, submenu contents, toolbar action
identity, language contexts, command binding and final grouping remain intact.
Toolbar/settings initialization still precedes panel creation; complete menus
still precede command attachment, shortcuts, language reload and window display.
Native dynamic File-menu publication, View activation and bookmark updates
retain their existing owners. No control geometry, wording, availability rule
or native menu adapter changes. Existing main_shell, view_menu and language_ui
behavior checks cover these consumers; no private initialization-order test
is added.

Source reference: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

Reviewed FileManager `resource.rc::IDM_MENU`, `resource.h::IDM_VIEW_TWO_PANELS`,
`MyLoadMenu.cpp::MyChangeMenu/OnMenuActivating/ExecuteFileCommand`, and the
English/Russian language entries, together with the Qt menu construction,
command bindings and live language reload. The two-panel QAction lacked its
source command ID, so the existing source menu translator skipped it and its
initial Russian caption survived English startup and reload.

The Qt action now carries the original ID. Existing source translation,
English fallback and View-menu state projection consume it; the existing panel
toggle connection remains. No upstream code or language assets are copied or
changed, no functional source body is replaced, and no branches are omitted.
Dependencies remain the existing QAction menu projection and language services.
The source baseline and platform adaptations are unchanged.

Validation: macOS arm64 / Qt 6.12.0, working tree based on
`19006aac08755e0ee98de26eefaa45e21038a6f3`.
The language UI regression covers English/Russian startup, live EN/RU changes,
missing-language fallback, preserved toggle state/shortcut, and absence of
Russian captions in English main menus, excluding user bookmark paths. The
timestamp caption is checked after opening View, when source activation fills
its current-date example.

Incremental dev builds of `Sevenzip`, the nine language test targets and
`view_menu_tests` succeeded. Focused CTest: `language`, `language_ui`,
`language_dialog`, `language_standard_buttons`, `language_main_dialog`,
`language_archive_command`, `language_focus`, `language_source_update`,
`language_file_transfer`, `view_menu`: all ten pass. The initial menu scan also
examined the hidden timestamp's construction caption; the regression now opens
View before inspecting that dynamically populated menu, matching its source
contract. Current View JSON for both panes, EN/RU and 980×620/640×400 shows the
translated panel row fully visible (225-pixel English / 295-pixel Russian menu).
No Windows/Linux run, rendered Windows comparison, full gate or slow suite is
claimed. These checks cover the selected language consumers, not every possible
message or third-party/native dialog.
