# Видимость платформенных настроек

2026-10-17. По запросу пользователя неподдерживаемые настройки скрыты.

## Complete options navigation presentation — 2026-11-05

Reviewed the original OptionsDialog page/property-sheet owner and the current
section composition, navigation, language reload, resize, validation reveal,
keyboard activation and Apply/Cancel consumers. Complete original OptionsDialog.cpp
hash matches the existing baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; no native algorithm, platform branch,
dependency revision or imported scope changes.

The complete Qt navigation setup, section-name reload, resize/layout update and
invalid-control reveal now share `qtfmoptionsnavigation.cpp` in the existing
location component. Section content stays with its composition owner; native page
state, help dispatch and Apply/Cancel stay in the dialog's existing owners.
The dialog retains all widgets and state. Its builder calls the extracted
navigation setup at the same point before refreshing section summaries.

Complete expanded layout/dialog bodies match their preceding versions, including
quoted strings. Widget types/parents, construction and connection order, source
page identities, fonts, dimensions, visibility, focus and language contexts are
unchanged. No controller, runtime state or product interface is added. Existing
options, keyboard and live-language regressions provide behavior acceptance;
no test depends on private helper names or source placement. This source-owner
change does not establish new rendered visual parity.

## Settings page presentation owner — 2026-11-04

Compared complete original SettingsPage.cpp/.h/.rc at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; complete original hashes match the
baseline. Reviewed initialization/change/apply branches, standalone and embedded
callers, memory range/spinner, privilege service and page draft/close consumers.
No external code or revision is imported.

The complete Qt control setup, appearance-text reload and language-change body
now share `qtsettingspagepresentation.cpp` in the same panel component. Bodies
are unchanged. The constructor retains its settings/service initialization,
builds the same controls and then invokes the same OnInit. The existing instance,
widgets and independent modification flags remain authoritative; no controller,
public interface or state is added.

Native OnInit/OnApply stay together, retaining list persistence before memory
validation, supported-only privilege handling, unset/invalid limit behavior and
appearance publication order. Initialization still suppresses change signals.
Standalone buttons, embedded mode, captions, localization, focus, geometry and
Cancel remain unchanged. Existing list-settings, memory-lock and full options
behavior checks provide acceptance; no construction/source-presence test is
added.

## Options page activation owner — 2026-11-04

Rechecked complete original OptionsDialog.cpp property-sheet/page ownership at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; its complete original hash matches
the baseline. Reviewed current Qt page activation, initial focus, shortcuts,
section presentation, help, draft/apply and close consumers. No external code or
revision is imported; keyboard/focus handling remains the existing Qt adaptation.

The existing QtOptionsPages class now has its own focused header/implementation
beside the options dialog, in the same component. Constructor, focus traversal
and key handling bodies are unchanged. The dialog still owns the same widget
and initial-focus call; its apply/persistence and section-layout owners remain
unchanged. Page resource IDs and their widget pointers are paired in one local
record rather than parallel arrays, preserving help/provenance associations.

Qt still selects pages before the existing focus rule runs. Window shortcuts
include the footer, skip unavailable controls, choose the checked exclusive radio,
select line-edit text and retain the existing destruction guards and Cocoa
physical-Control mapping. Pending values, initial focus, language updates,
Apply/Cancel/error semantics and geometry remain unchanged. Existing keyboard
and options workflow checks provide acceptance; no private-class or page-table
test is added.

## Pending drafts across Options pages — 2026-11-04

Options now uses one query of its existing six page-modification flags for both
Apply availability and the final permission to close after saving. Previously
the close decision checked only Settings, so an Applied observer could create
an editor-page draft, enable Apply, and still have Save close the dialog without
persisting that new text. The existing memory-page protection now covers every
page through the same predicate; no new state or controller is introduced.

The apply order, association-error handling, memory validation, language/theme
publication and reentrancy guard are unchanged. SystemPage still clears its
pending flag after processing failures, preserving the original property-sheet
policy that an association error alone does not veto closing. Native page
algorithms, persistence and visible layout/text are unchanged.

Compared complete original OptionsDialog.cpp, EditPage initialization/change/
apply and declarations with current six page-modification contracts, Apply,
accept/reject and observer/persistence consumers. Complete OptionsDialog.cpp,
EditPage.cpp/.h, SettingsPage.cpp and SystemPage.cpp hashes match the recorded
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` baseline from
<https://github.com/ip7z/7zip.git>. This is a Qt dialog result-policy correction;
no external code is imported or original provenance advanced.

The existing public Save regression now covers an editor draft alongside the
memory draft. It verifies that the first Save retains the new text and leaves
the previous saved value unchanged; the next Save persists it and closes.
Before the fix the editor case reproduced premature closing. This checks the
dialog and persistence contract, not the new private query or flag representation.

Linux `options_dialog`, `options_integration` and `system_page` passed:
48 checks without failures or skips, including the editor/memory draft cases
and the existing non-vetoing association-error case. The portable Qt close-policy
change and unchanged native/platform bodies make Linux sufficient; no new
Windows/macOS, full/slow or appearance acceptance is claimed. Selected scratch
was empty before/after, configured four-job incremental builds/compiler caches
were preserved, and whitespace checks passed.

## Исходники и границы переноса

Source repository: <https://github.com/ip7z/7zip>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.
Сверены `CPP/7zip/UI/FileManager/OptionsDialog.cpp`, `SettingsPage.cpp`,
`FoldersPage.cpp`, `MenuPage.cpp`, `SystemPage.cpp`, связанные headers и
`SettingsPage2.rc`, `FoldersPage2.rc`, `MenuPage2.rc`, `SystemPage.rc`.

Сохранены перенесённые OnInit/OnApply, группы и порядок страниц, исходные
значения и границы сохранения. Новых алгоритмов архивных операций нет.
Изменения ограничены Qt presentation и существующими platform services:

- Settings: large pages видимы только при поддержке memory-lock service.
- Folders: removable-only скрыт на POSIX, где исходная проверка типа диска
  не используется; сохранённое значение не сбрасывается.
- Menu: Win32 shell registration скрыта на POSIX независимо от наличия файлов
  с DLL-именами. Windows сохраняет исходное отключение отсутствующих модулей
  и скрытие недоступной другой разрядности.
- Menu: неподдерживаемый Zone.Id скрыт вместе с подписью. Cascaded, Icons,
  Eliminate duplication и список команд остаются доступны своим in-app consumers.
- System: недоступная группе операция регистрации скрывает её кнопку `+`.
  Колонки текущих ассоциаций остаются для просмотра. Ограничения отдельных
  форматов сохраняют исходные отказы; алгоритм выбора и Apply не меняется.

Адаптация отличается от исходного Windows property sheet только видимостью
платформенных действий. Дополнительного omitted source scope нет; прежние
границы модулей и Win32-to-Qt адаптации остаются в базовом аудите.

## Проверки

macOS arm64 / Qt 6.12.0: затронутые test targets собраны incremental.
Focused CTest: `fm_settings`, `memory_lock`, `menu_page`, `options_dialog`,
`options_keyboard`, `options_integration`, `system_page`, `ui_parity_system`,
`mac_system_page`, `language_ui` — PASS (10 suites). Первый sandbox-запуск
`memory_lock` прерван Cocoa из-за отсутствия экрана; повтор с offscreen — PASS.

Native Cocoa direct consumers: Settings/Folders geometry (4 QtTest results),
Menu capabilities/geometry (3), EN/RU retranslation/geometry (3), Options keyboard
(27) — PASS. Просмотрены текущие JSON normal/compact 640×390/520×380:
скрытые shell/Zone/large-pages/removable элементы не занимают строк; список меню,
поля памяти/пути и footer полностью открыты, RU подписи помещаются. System
с одним доступным действием сохраняет кнопку над своей колонкой и полный список;
четыре маски доступности проверены клавиатурным suite. Скриншоты не требовались.

Регрессии расширяют существующие сценарии видимости, геометрии, сохранения
настроек и фокуса; исходный resource contract доступного fake System не изменён.
Windows/Linux runtime и rendered Windows pixel parity этой правкой не установлены.

### Коррекция растяжения Settings после пользовательского скриншота

Исходный `SettingsPage2.rc` располагает подпись лимита и spin в соседних строках.
Qt VBox без нижнего stretch распределял лишнюю высоту между подписью и строкой
памяти; прежняя проверка полной видимости не обнаруживала этот разрыв.
Свободная высота теперь отводится под группу настроек, до отдельного footer.
OnInit/OnApply и платформенная видимость не меняются.

Регрессия Settings дополнена размерами 640×720 и 900×800, проверяет отсутствие
растянутой подписи и близость строки ввода. Проверка переносимого QLabel
в standalone Settings оценивает размещение текста при текущей ширине.
macOS: приложение пересобрано; `fm_settings`, `options_dialog`,
`options_keyboard`, `language_ui` — PASS (после исправления проверки переносов
повторены первые два). Native Cocoa Settings — PASS, текущие JSON и скриншот
увеличенного 640×720 просмотрены: подпись и ввод рядом, пустое место ниже.
900×900 заменён на 900×800, поскольку macOS ограничивает высоту текущим экраном.

### Повторный обзор остальных страниц

По следующему запросу проверены все шесть страниц Options: EN/RU,
640×390, 520×380 и 640×720. Просмотрены 36 current-build Cocoa JSON и снимки
затронутых страниц. Аналогичного растяжения остальных форм не обнаружено:
Folders/Edit удерживают строки сверху, System/Menu/Language расширяют списки.
У исходных EditPage2.rc/LangPage.rc и текущих Qt layouts проверены назначение
свободного места и порядок элементов; функциональный перенос не менялся.

Существующий `language_ui` теперь включает System и увеличенное окно,
проверяет сохранение верхних координат элементов при росте высоты с той же
шириной и фактическую смену языка. CTest `language_ui` — PASS. Нативный запуск
использует тот же изолированный `build/dev/language_ui.ini`, что и CTest.

### Переносимость геометрической проверки — 2026-10-18

Follow-up проверки Qt-адаптации обнаружили хрупкое ожидание в
`options_dialog::settingsGeometryAndKeyboardSpin`: точный размер окна
приравнивался к запросу, расширенному только до minimumSizeHint. На Windows
при ширине 640 hint давал высоту 390, а heightForWidth и фактическая геометрия
требовали 512. Запрос 900×800 также получил допустимый native client 900×749;
расчёт по доступной области QWidget не воспроизводил ограничение оконной системы.

Сверены исходные SettingsPage2.rc/OptionsDialog/PropertyPage и текущие Qt
layouts/контракт Settings. Product code и перенос OnInit/OnApply не менялись.
Убрано только сравнение точного client size, проверявшее расчёт Qt/WM.
Сохранены все четыре исходных запроса, видимость, порядок, отсутствие
пересечений, реальная высота подписи и зазор до поля памяти. Отдельные JSON
`options-settings-<width>x<height>` сохраняют каждый запрос и фактические bounds.

macOS arm64 и Linux x64 / Qt 6.12 offscreen: `options_dialog` PASS. Windows x64
/ Qt 6.12, QPA windows в интерактивной сессии: PASS; remote receipt
`20261017T215830Z-4e302ed8`. Просмотрены 12 актуальных JSON: четыре размера на
каждой ОС. Settings и footer полностью видимы; при росте высоты координаты
группы настроек не расходятся. Windows compact states имеют высоту 512,
large state — 749; macOS/Linux сохранили запрошенные размеры. Доказана
геометрия Qt controls на этих платформах, без rendered Windows 7zFM reference.

## Результат применения для OK — 2026-11-02

Повторно сопоставлены исходные OptionsDialog/SettingsPage и текущие владельцы
страниц, Apply/accept, ошибки ассоциаций, валидация памяти, язык, оформление и
публикация в панели. SHA256 полных оригинальных OptionsDialog, SettingsPage и
SystemPage совпадают с baseline на ревизии
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Нового внешнего кода и изменения
исходных алгоритмов страниц нет.

Qt Apply теперь возвращает разрешение закрыть диалог. OK использует этот
результат вместо отдельной проверки внутреннего состояния страницы и второго
reentrancy guard. Владелец применения сохраняет прежний порядок, частичные
записи до ошибки валидации и невлияющую на закрытие ошибку System page.
Проверка свежего черновика после обработчиков Applied остаётся внутри этого
же владельца: он удерживает диалог открытым, как и раньше. Apply/Cancel,
перевод, оформление, обновление панелей и геометрия не меняются.

Новая поведенческая проверка оставляет изменённый лимит памяти через публичный
Applied и подтверждает, что первое OK сохраняет прежнее значение и оставляет
новое в редакторе, а второе OK применяет его и закрывает диалог. Она защищает от
потери нового черновика при упрощении закрытия; приватные флаги или реализация
возвращаемого значения не проверяются. Остальные сценарии используют имеющиеся
наборы настроек и прямых потребителей.

Linux: options_dialog, options_integration, options_window,
appearance_settings и system_page — 57 проверок прошли без пропусков.
Это проверка общего Qt workflow и тестовых association services; новая
приёмка native Windows/macOS services или внешнего вида не заявляется.
Временные настройки удалены, incremental-деревья и compiler caches сохранены.

## Уведомление о применении настроек — 2026-10-31

Сопоставлены полные исходные `OptionsDialog.cpp`,
`App.cpp::SetListSettings` и `SettingsPage.cpp::OnApply` из
<https://github.com/ip7z/7zip.git>, revision
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; SHA256 полных исходных единиц
совпадают с существующим baseline. Проверены владельцы страниц, Qt apply/accept/
reject, уведомление окна и применение к обеим панелям.

После успешного применения Qt-диалог теперь сообщает событие штатным сигналом.
Промежуточная копия настроек больше не передаётся окну: сохранённые значения
читает и публикует существующий `CApp::SetListSettings`. Удалён getter диалога,
который обслуживал только эту передачу. Сохранение страниц, порядок их применения,
частичное сохранение до ошибки валидации, запрет закрытия во время применения,
смена языка/темы и обновление панелей остаются прежними. Нового внешнего кода,
изменений baseline или пропущенных исходных ветвей нет.

Существующие проверки применения, отмены, перезапуска, ошибки ассоциаций,
валидации памяти и выделения в обеих панелях сохраняются; счётчик callback в
интеграционном сценарии заменён наблюдением за тем же публичным событием.

Runtime-проверка ShowDots теперь проходит весь маршрут от списка дисков к
виртуальному корню через родительскую папку. Ранее она ошибочно ожидала отсутствие
`..` сразу после команды «Корень», хотя исходный `App.h::OpenRootFolder` открывает
диски. Переходы сверены с `PanelFolderChange.cpp` и `FSDrives.cpp`; исходные
алгоритмы не изменены. Проверка дожидается готовности команды и списка.

Семь общих наборов настроек проверены на Linux, macOS и Windows. Дополнительно
прошли четыре native Linux-сценария System, macOS System page и Windows
current-user registration. Linux после коррекции маршрута ShowDots проверен
локализованным повтором; остальные прошедшие наборы не менялись. macOS: восемь
наборов, CTest 20,40 с; native interactive Windows: восемь наборов, 23,32 с.
Существующие incremental-деревья и compiler caches сохранены. Полный/slow gate,
machine-wide регистрация Windows и rendered visual parity не проверялись;
видимые элементы и геометрия этой итерацией не менялись.
