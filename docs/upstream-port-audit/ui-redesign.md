# Редизайн интерфейса

Область и последовательность — [согласованный план](../archive/plans/ui-redesign-plan.md).
Это намеренная адаптация представления; полный исходный функциональный scope
и baseline сохраняются в [основном аудите](../upstream-port-audit.md).

## Проверка C++23: выбор шифрования — 27 октября 2026

Сопоставлены `OnCommand(IDC_COMPRESS_FORMAT)`, `FormatChanged`,
`SaveOptionsInMem` и `SetEncryptionMethod` из `CPP/7zip/UI/GUI/CompressDialog.cpp`
официального <https://github.com/ip7z/7zip.git>, baseline
`b88c6bc09339de32f7d18bbab900686b244f42ba`, с Qt-обработчиками смены формата,
инициализацией формы, хранением профилей и потребителями метода шифрования.
Исходный порядок сначала сохраняет уходящий формат, затем обновляет controls.
Раннее Qt-подключение обновляло список шифрования до сохранения и теряло AES-256
при переходе ZIP → 7z → ZIP. Обновление перенесено в существующий FormatChanged;
исходные методы, алгоритмы и baseline-инвентарь не заменяются.

Существующий тест воспроизвёл потерю выбора до исправления. На macOS после него
прошли compression_encryption, compression_preferences,
compression_profile_selection и compression_format, включая создание реальных
защищённых архивов. Основное приложение собрано и прошло изолированный
`--smoke-test`. Компоновка, текст и платформенные службы не менялись;
отрисованная визуальная паритетность этим прогоном не подтверждается.

## Итерация 1: короткая форма и диагностический отчёт

Дата: 18 октября 2026. Исходник: https://github.com/ip7z/7zip.git,
commit `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.

Сравнены полные `CPP/7zip/UI/FileManager/ComboDialog.cpp/.h/.rc` и
`MessagesDialog.cpp/.h/.rc` с портом; просмотрены подключения из
PanelOperations (создание/комментарий), PanelSelect (маска), BrowseDialog
(создание папки) и PanelDrag (stack-owned vector сообщений).
Зависимости: исходные UString/векторы, LangUtils, list-view adapter,
Qt font-DLU/screen services и button-box/localization adapters.

Сохранены Combo OnInit/OnOK, порядок history strings и необработанный Value;
Messages AddMessageDirect/AddMessage, включая разбиение строк, OnInit,
modal Create, результаты IDOK/IDCANCEL и начальный Close focus.
Новые функциональные алгоритмы не добавлялись, исходные ветви не исключались.
Изменяется только представление: общая компоновка содержимого и footer,
проектные отступы, нативные размеры кнопок; высота короткой формы по содержимому.
Ручная Win32-подобная геометрия Messages OnSize заменена Qt layout, сам путь
уведомления остаётся. Общий helper не владеет значениями, переводами или действиями.

Повторное сравнение изменённого scope: Combo OnOK и обе функции добавления
сообщений не изменены; виджеты и footer подключены к прежним обработчикам,
Create по-прежнему удерживает исходный modal lifetime. Новых ветвей операций нет.

Проверки macOS: сборка пяти focused targets; CTest `combo_dialog`,
`source_prompt_presentation`, `extract_prompt`, `language_dialog`,
`language_standard_buttons` — 5/5 PASS. После включения существующего review
entry point повторён `combo_dialog` — PASS. Нативный Cocoa запуск geometry/focus
Combo и EN/RU Messages в light/dark — PASS; просмотрены текущие widget JSON
и изображения длинного prompt/диагностики. Reports: `build/dev/ui-redesign-review`.
Первый sandboxed Cocoa запуск не получил экран; unsandboxed повтор прошёл.
Windows/Linux и native sheet/VoiceOver на этом этапе не проверены.
Итерация не означает готовность остальных окон или всего редизайна.

## Итерация 2: Add и Extract

Источник: тот же официальный repository/commit. До правки сопоставлены исходные
`GUI/CompressDialog.cpp/.h/.rc`, `ExtractDialog.cpp/.h/.rc`, их OnInit/OnOK,
FormatChanged, password/encryption и OnCommand-ветви с текущими адаптерами.
Проверены PresentSource/sourceInfo, UpdateGUI ShowDialog, ExtractGUI invocation,
переключение профилей, memory/volume validation и вложенный COptionsDialog.
Зависимости/существующие adaptations — в исходном baseline-аудите этих модулей.

Сохранены исходные расчёты, codec capability owner, метод/словарь/потоки/память,
имена архивов, история путей, параметры времени/метаданных и транзакция вложенных
параметров. Win32-контролы по-прежнему заменяются Qt; новых архивных алгоритмов
и таблиц возможностей нет. Повторно проверены подключения исходных валидаций
и итоговых DTO после изменения компоновки.

Новая общая часть — QtDisclosure: только раскрытие/сводка, без изменения значений
и enabled-state содержимого. Add переносит настройки в одну форму с постоянно
видимыми format/level и сворачиваемыми дополнительными параметрами. Extract
аналогично группирует путь/перезапись/пароль/дополнительные настройки. Опциональный
пароль — согласованная UI-адаптация: переданный пароль раскрывается автоматически;
выключение исключает его из запроса, не стирая черновик. Поддержка шифрования
остаётся у исходного GetFormatInfo; capability EncryptHeadersIsAllowed отделена
от текущего UI enable-state. Исходная password validation получает активные
значения либо пустую пару при явном выключении защиты.

QtLangButtonBox поддерживает локализуемую подпись основного действия, сохраняя
исходную кнопку/роль/сигналы и повторный перевод. Для Add/Extract это Create archive
и Extract. Новые EN-строки и RU-переводы находятся в контексте ArchiveRedesign.
Сводки не содержат паролей и произвольного текста дополнительных параметров.

Пересмотрены проверки, закреплявшие старые колонки и постоянную видимость всех
полей: теперь проверяются раскрытие, доступность через прокрутку, читаемость
и прежние результаты операций. Добавлены EN/RU regressions на сохранение опций
при сворачивании и сохранение/исключение password draft. Проверки исходных
профилей, ошибок, сохранения и фактических архивов сохранены.

Размер существующих presentation-модулей рассмотрен: формы остаются едиными
сценариями построения UI, а алгоритмы и зависимости уже находятся в исходных
модулях и отдельных adapters. Новая общая часть выделена отдельно; универсальный
движок форм и новый владелец настроек не вводились.

Проверки macOS: сборка приложения и затронутых targets PASS; focused CTest
`source_compress_dialog`, `extract_dialog`, `compression_encryption`,
`compression_advanced`, `language_standard_buttons`, `language_dialog`,
`compression_event`, `compression_profile_selection`, `compression_error_policy`,
`compression_resources`, `source_archive_browse` — 11/11 PASS.
Нативные Cocoa EN/RU Add/Extract и compact RU Add в light/dark — PASS;
проверены текущие widget JSON и снимки в `build/dev/ui-redesign-review`.
Исправлена начальная высота Add: пересчёт height-for-width выполняется после
инвалидации layout, чтобы сводка помещалась сразу после открытия.
Заголовок раскрытия рисуется средствами текущего Qt style без постоянной панели
кнопки; размер стрелки ограничен отдельно от области нажатия.
Windows/Linux, VoiceOver и новая sheet-модальность в этой итерации не проверены.
Вложенный COptionsDialog сохраняет прежнее представление и исходную транзакцию;
его отдельный редизайн и остальные окна остаются в scope следующих итераций.

## Итерация 3: изменение размеров двух панелей

Источник: https://github.com/ip7z/7zip.git,
commit `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Сопоставлены полные тела `UI/FileManager/FM.cpp::CSplitterPos`, WM_CREATE,
WM_LBUTTONDOWN/UP, WM_MOUSEMOVE, WM_SIZE, MoveSubWindows и SaveWindowInfo
с портом FM.cpp, QtFileManagerSplitter и текущими create/toggle/close adapters.
Проверены CApp::SwitchOnOffOnePanel и Qt-публикация видимости панелей.

CSplitterPos, его bounds/ratio/rounding и существующее CWindowInfo-хранилище
сохранены без изменений. Дополнение относится к presentation/input:
клавиатурный фокус, Left/Right (Shift — крупнее), Return и двойной щелчок для
равных долей. Этих keyboard/reset handlers в исходном splitter нет;
это прямо согласованная адаптация редизайна, а не новая реализация его расчётов.
Используются QSplitterHandle и системный focus primitive; EN/RU имя и tooltip
проходят через текущий переводчик. Модели и навигация не пересоздаются.

Qt minimum может временно ограничить рассчитанную позицию. При переключении
панелей такая применённая геометрия больше не записывается поверх исходной
предпочтительной пропорции. Явные drag/keyboard изменения используют SetPos.
Повторная сверка scope: исходные расчёты/ветви сохранены, обработчики подключены
к тому же экземпляру splitter, окно сохраняет позицию прежним путём.

macOS: приложение и три focused targets собраны; CTest window_placement,
panel_key, panel_shell — 3/3 PASS. Cocoa keyboard/reset/minimum/toggle и
normal/compact окно проверены в light/dark; pointer drag сохраняет фокус,
выделение, paths и модели обеих панелей. Проверены текущие JSON и RU-снимки
главного окна: обе status strips выровнены, узкие таблицы прокручиваются.
Native проверка выявила различие TabFocus/StrongFocus на macOS: сохранён
StrongFocus для обхода с клавиатуры, pointer press возвращает прежний фокус.
Reports остаются в `build/dev/ui-redesign-review`.
Windows/Linux runtime, физическая клавиатура и VoiceOver пока не проверены.
Toolbar, меню и современные сочетания из этапа 4 остаются следующей работой;
эта итерация не означает завершения главного окна.

## Итерация 4: компактный toolbar главного окна

Источник тот же: https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
До правки сопоставлены App.cpp::CButtonInfo, g_StandardButtons,
g_ArchiveButtons, оба SetButtonText, AddButton, ReloadToolbars,
SaveToolbarChanges и tooltip-ветви OnNotify с портом App.cpp и текущими
mainwindow_menus/preferences/language adapters. Проверены AttachActions,
AttachFileActions, refreshActions и CopyTo workflow; команды и source IDs
остаются общими для toolbar и меню. Сохранены исходные bitmap assets,
mask/default/visibility/size preferences и их storage owner.

Согласованная presentation-адаптация: текст beside icon только у Add/Extract,
остальные пять кнопок — icon-only. Выключение сохранённого labels-флага скрывает
также эти подписи. Добавлены доступные имена, native shortcut в tooltip и
StrongFocus; autoRaise оставляет hover/focus текущему стилю Qt. Подпись Extract
использует уже существующий ArchiveRedesign-перевод, общий с Extract-диалогом.
Существующее подключение QAction::changed обновляет подписи/подсказки;
второе подключение и новый command dispatcher не добавлены.

Повторная сверка: source button tables, capability/operation handlers и toolbar
state не менялись; все семь прежних QAction остаются теми же экземплярами.
Заменены хрупкие проверки имени ресурса/доступных bitmap размеров/углового
пикселя на проверку отображения значков и доступности всех кнопок на экране.
EN/RU проверка фиксирует согласованное различие двух основных и пяти остальных
действий; language_ui проверяет актуальность tooltip после смены языка,
выделения и shortcut. Keyboard Space вызывает один исходный Copy-диалог
с файлом правой панели и назначением в левой; Cancel не создаёт копию.

macOS: сборка приложения и direct consumers PASS; focused main_shell,
panel_shell, view_menu, language_ui PASS. Cocoa EN/RU normal/compact,
one/two panels и keyboard Copy/Cancel проверены; main_shell и language_ui
повторены в light/dark. Текущие JSON подтверждают видимость всех семи кнопок
при ширине 640, снимки toolbar просмотрены. Reports — прежний
`build/dev/ui-redesign-review`. Временный compile failure в параллельном
file-transfer изменении исправлен его владельцем; повторная сборка прошла.
Windows/Linux, физические раскладки и VoiceOver пока не проверены.
Перегруппировка File/View/context меню, новые основные сочетания и окончательный
выбор современных иконок остаются в дальнейшей области редизайна.

## Итерация 5: File/View/context меню

Источник: https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Сопоставлены resource.rc::IDM_MENU, MyLoadMenu.cpp::CFileMenu::Load,
OnMenuActivating (File/View, включая timestamp examples/UTC),
PanelMenu.cpp::CreateFileMenu/OnContextMenu и вызов plugin command с текущими
портированными телами, QtProgramFileMenu, QtPanelContextMenu и построением меню.
Проверены source filtering, read-only/hash/ADS/small-screen ветви, diff/version
control, hash submenu, plugin ownership и захват вызвавшей меню панели.

Исходные функциональные тела не изменены. Новый QtGroupFileCommands работает
после исходной projection: перемещает существующие QAction в Open as/Create/
Advanced либо context More. Контекстные QAction остаются отдельными копиями,
возвращающими прежний ID через QMenu::exec; центральной диспетчеризацией по-прежнему
владеет CApp. Все hash-алгоритмы сохранены, неизвестные plugin/extension строки
включаются в редкую группу. Системные подменю перемещены целиком с сохранением
внутреннего порядка и прежних callback/owner объектов. Вложенные группы
присоединены к существующему disposable-row lifecycle; исходный плоский
SourceActions остаётся инвентарём для filtering и контекстных копий.

View получает Presentation/Sort by, прежние action groups и checked-state
сохраняются. Подпись Time format устанавливается presentation adapter после
исходного OnMenuActivating; сами актуальные примеры времени, точность и UTC
не менялись. Добавлены восемь EN/RU подписей. Новые назначения клавиш не входят
в эту итерацию. Размер helper рассмотрен: классификация и размещение относятся
к одному представлению, функциональная политика остаётся в CFileMenu.

Повторное сравнение scope: все известные ветви исходного отбора сохранены;
нет новой capability-таблицы и переопределения enabled-state. Старые проверки
плоского расположения заменены поиском по дереву и реальным выбором подменю.
source_context_menu создаёт файл через More в правильной панели для table/icons,
EN/RU; View выбирается через Presentation без смены режима соседней панели.
Повторное открытие, stale plugin actions, source paths/providers и данные
сохраняют прежние проверки. В прямом shell_update consumer обновлён сценарий
раскрытия Add/password, оставшийся от прежней формы; SFX/password/volumes/Cancel
по-прежнему проверяются результатом операции. Failure cleanup закрывает форму,
чтобы ошибка проверки не запускала повторяющийся modal responder.

macOS: приложение и affected targets собраны. main_shell, panel_shell,
source_context_menu, shell_menu_window, file_menu_policy, view_menu, language_ui
PASS; shell_update_window после обновления consumer PASS отдельно (10,50 s).
Первый shell-menu запуск использовал Homebrew 7zz 26.03 с модулем 26.04;
для связанных проверок PATH направлен на существующий согласованный
`build/codec-runtime` 26.04, без пересоздания runtime/cache/build.
Cocoa File/context/View и новые подменю проверены EN/RU normal/compact в
light/dark; реальные nested Create/View команды PASS. Native shell-menu
normal EN / compact RU и stale rebuild PASS отдельно в текущей системной теме.
Проверены текущие widget JSON и снимки в `build/dev/ui-redesign-review`.
Windows/Linux native integration, физические раскладки и VoiceOver остаются
непроверенными; новые shortcuts и остальные семейства окон ещё предстоят.

## Уточнение: шеврон дополнительных параметров

По замечанию пользователя исправлен общий QtDisclosure в Add/Extract. Ранее
native arrow primitive в малом прямоугольнике давал слишком мелкий значок.
Теперь контур шеврона масштабируется по шрифту и поворачивается в общей области;
подпись рисуется с метриками кнопки. Сохранены checkable-кнопка, клавиатурный
фокус, RTL, disabled palette и прежнее управление видимостью содержимого.
Это уточнение presentation-адаптации итерации 2: новых upstream-ветвей нет,
архивные параметры и обработчики не изменены.

macOS: приложение и два прямых потребителя собраны; focused CTest
`source_compress_dialog` и `extract_dialog` — 2/2 PASS. Cocoa EN/RU в light/dark
прошёл для обеих форм; проверены widget JSON и снимки свёрнутого/раскрытого
состояния в `build/dev/ui-redesign-review`. В существующие сценарии добавлен
только report раскрытого состояния, без фиксации пиксельных размеров тестом.
Windows/Linux и VoiceOver для этой правки не проверены.

Повторное уточнение размера: контур уменьшен относительно cap height подписи,
линия стала тоньше; размер кнопки и положение текста сохранены. Меняется только
отрисовка общего QtDisclosure, обработчики Add/Extract и upstream-поведение
не затронуты. Приложение и оба прямых потребителя собраны; focused
`source_compress_dialog|extract_dialog` — 2/2 PASS. Cocoa EN/RU light/dark —
16/16 test functions PASS; актуальные widget JSON и снимки collapsed/expanded
проверены в прежнем каталоге review. Windows/Linux и VoiceOver для этого
уточнения не проверены.

## Итерация 6: современные сочетания

Источник: https://github.com/ip7z/7zip.git,
commit `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Сопоставлены целиком upstream PanelKey.cpp, CMyListView::OnMessage,
CMyComboBoxEdit::OnMessage, основной menu resource и соответствующие Qt dispatch,
command bindings, guards, context copies, language/toolbar consumers. Повторная
сверка сохранила Shift-F5/F6, выбор, clipboard, keypad, сортировку и оставшиеся
bookmark/path variants вне явно согласованных переназначений.

Назначения сосредоточены в Qt-представлении команд по
[схеме](../ui-shortcuts-design.md). Операционные тела CPanel и исходные command
owners не изменены. Новые сочетания файлового списка проходят существующие
closed/busy/provider/modal guards и проверки доступности; недоступное действие
поглощает своё сочетание, не передавая его прежней команде. Inline editor и
адрес сохраняют собственный ввод, Return и Undo. Фокус пути выбирает текст,
не раскрывая историю. Закрытие использует существующий command 8.
Основное назначение отображается самим QAction; toolbar Info получает подсказку
от Properties, поскольку upstream привязывает обе кнопки к IDM_PROPERTIES.

Намеренные различия относительно Windows 7zFM: Command/Control-Shift архивные
действия; macOS Return — rename, Command-Down — open, Command-Backspace — delete;
Control/Command-N и Z освобождены. Windows/Linux Alt-Up/Left — навигация,
синхронизация остаётся через Alt-Shift. Shift-2 переключает панели, сохраняя
Alt-Shift-2 для записи закладки. Удаление без корзины на macOS сохраняется
через исходное действие с Shift, без нового Command-Shift-Backspace.
Полная схема и остальные варианты описаны в её владельце, без дублирования тут.

Native command decoding использует Qt QKeyMapper для букв и цифр текущей
раскладки, не event.text(). Нативный Cocoa-прогон обнаружил исключение Qt:
virtual key 0 физической A не выдаёт варианты. Проверены установленный private
header Qt 6.12.0 и [тело QAppleKeyMapper](https://github.com/qt/qtbase/blob/dev/src/gui/platform/darwin/qapplekeymapper.mm).
Узкая macOS-адаптация разрешает только этот нулевой код через текущую системную
Command-layer (TIS/UCKeyTranslate), без таблицы букв и изменения глобальной
раскладки. Это platform input service, нового upstream-алгоритма операции нет.

Регрессии проверяют ровно один Copy/Move с правильной исходной/целевой панелью,
отмену без изменения файлов, Create/Open/Up/path, две панели, недоступную Add,
редактор имени, reserved keys, clipboard и modal/window isolation. Существующие
archive workflows вызываются новыми Add/Extract/Test сочетаниями с прежними
проверками результата. В прямых потребителях обновлены устаревшие назначения;
modal responders ожидают видимого окна. Синтетические modifiers освобождаются
между взаимодействиями, чтобы не загрязнять следующий сценарий.

macOS: приложение и affected targets собраны; 15 focused-наборов PASS.
Cocoa panel_shortcut в light/dark PASS; native Cyrillic logical-key protocol
для Copy, код A=0, main_shell и language_ui PASS. Сохранены текущие widget JSON
и снимки в `build/dev/ui-redesign-review`. Протокольные события не доказывают
физическую EN/RU клавиатуру; реальная смена раскладки, keyboard без F-ряда,
VoiceOver и Linux desktop conflicts остаются отдельной проверкой.

Linux: 15/15 focused-наборов PASS; после финальных input/fixture изменений
пять прямых потребителей PASS. Windows interactive: первый прогон 13/15,
затем исправлены native Shift-2 (единое декодирование виртуальной цифры с
bookmark path) и ожидание готового QMessageBox через общий widget fixture;
пять затронутых наборов PASS. Native Windows-цифры проверены протокольными
событиями, старые physical right-Control 1/8/9 также прошли через SendInput.
Сценарий удаления здесь проверяет Qt widget-подтверждение, не системную панель.
На Cocoa тесту сохранения закладки исправлено сравнение /var и /private/var
как одной реальной папки; 13 вариантов событий для закладок PASS в обеих темах.
Runners используют прежний current и compiler cache; obsolete source-delete
fixtures после timeout не обнаружены. Full/slow suites не запускались.
Финальная версия теста закладок с каноническим fixture path также отдельно
прошла на Linux и Windows после повторной синхронизации и сборки target.

## Итерация 7: короткие формы ввода и подтверждения

Источник: https://github.com/ip7z/7zip.git. Сохранён существующий baseline модулей
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`; при работе сопоставлены текущие
тела и ресурсы с checkout `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Область: FileManager CopyDialog, SplitDialog, PasswordDialog, MemDialog,
OverwriteDialog и LinkDialog, их заголовки/Res/RC, GuiCommon.rc и Qt controls.
Проверены callers CApp::OnCopy/Link, PanelSplitFile Combine, callback password,
AskOverwrite и RequestMemoryUse, window/backend consumers. Используемые
SplitUtils, пути, link services, память/formatting и source result IDs сохранены.

Общая компоновка принимает QWidget footer, поэтому стандартный QDialogButtonBox
и существующая сетка ответов используют одни внешние поля/интервал завершения.
Copy/Move/Combine, Split, Password, Memory и Link подключены к ней. Высота
небольших форм следует содержимому; native button hints заменяют жёсткие размеры.
Copy сохраняет не редактируемую прокручиваемую сводку без Tab stop и исходную
историю; Split даёт размеру тома растягиваться с окном. Source acceptance,
проверка размеров томов, browse normalization и password Show/read/commit прежние.
Основные подписи называют результат действия; EN-источники и RU-переводы
находятся в DialogRedesign, роли и существующие сигналы не подменены.

Memory переставляет Allow/Skip перед настройкой будущего лимита; Remember
остаётся самостоятельным выбором ниже. RAM-dependent defaults/range, независимый
текстовый ввод, проверка нуля/ошибок и Continue/Cancel не изменены. Link использует
QDialogButtonBox с тем же исходным handler, без автоматического accept при ошибке;
порядок кнопок теперь нативный, начальный фокус явно возвращён полю источника.
POSIX Junction/WSL остаются disabled по прежнему
контракту; исходные file/link classification и reparse операции не изменены.

Overwrite показывает existing/incoming metadata рядом. Replace/Skip и варианты
для всех возвращают прежние IDYES/IDNO/extra IDs; Auto Rename и Cancel сохранены.
ShowExtraButtons=false снова скрывает пакетные ответы, как upstream, и укладывает
остальные в одну строку. Сохранены ReduceString, byte/time formatting и source
default No. Полная область кнопок учитывается grid на Cocoa, чтобы native optical
margins не давали пересечение. Второй EXE/ICO icon остаётся прежним omission.

После реализации повторно сверены исходные handlers: новых операционных ветвей
и disconnected consumers в выбранной области нет. Большие BuildPresentation/OnInit
рассмотрены: каждый строит одну форму; алгоритмы остаются в исходных владельцах.
Выделен только уже используемый общий layout, универсальный form engine не добавлен.
Строки, геометрические отношения и source mappings обновлены в существующих
контрактах. Хрупкие проверки исходных DLU frames/пропорций заменены проверками
читаемости, порядка, resize и сохранённого результата, без новых тестов констант.

macOS: приложение и affected targets собраны; 17 focused suites PASS, затем
финальный набор с четырьмя ui_parity контрактами — 21/21 PASS. Отдельно просмотрены ошибки
и declared adaptations всех четырёх результатов: по четыре отчёта, без ошибок.
Cocoa EN/RU normal/compact, начальные Password/Memory, Copy/Move/Combine, Split,
Link, Overwrite/default/batch и hot language change прошли в light/dark.
Просмотрены widget JSON и снимки в `build/dev/ui-redesign-review`.
Новая sheet-модальность не вводилась; существующие modal lifetime/owners сохранены.
VoiceOver и rendered original Windows pixel comparison не выполнялись.

Linux: 21/21 focused suites PASS. Windows interactive с обычным токеном: 20/21,
link_dialog прошёл отдельно с повышенным токеном, требуемым существующими
symlink fixtures. Права учётной записи/UAC/developer-mode для этого не менялись.
Последующая правка начального фокуса отдельно прошла в Link и его window consumer
на macOS/Linux/Windows; Cocoa light/dark также PASS. Финальное скрытое состояние
Memory не оставляет spacer перед footer: четыре прямых потребителя PASS на трёх
ОС, нативные состояния с/без Remember проверены в light/dark.
Сохраняются прежние current/build/cache; до прогонов проверены свободное место
и отсутствие старых payload-каталогов (на обоих гостях только current).
Full/slow suites не запускались.

## Итерация 8: вложенные параметры формата

Источник: https://github.com/ip7z/7zip.git,
существующий baseline COptionsDialog — `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`;
сверка с checkout `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Прочитаны CompressOptionsDialog.rc, CompressDialogRes.h/GuiCommon.rc, полные
COptionsDialog Check/Get/Store_TimeBoxes, GetComboValue/AddPrec/SetPrec,
SetTimeMAC, OnInit/OnCommand/OnButtonClicked/OnOK/OnHelp, объявления и callers
основного Add/Qt advanced adapter. Сверены CArcInfoEx time flags, format profile,
TAR method/ZIP precision branches и вложенная транзакция локальной копии profile.

Изменён только Qt presentation adapter. Сведения о текущем формате/методе
подняты над плоскими группами links/metadata и time. Общая компоновка даёт
согласованные margins/footer и Save/Cancel/Help; удалены фиксированные высоты
строк и ширина precision combo. Полная подпись Set time precision находится
над зависимыми label/combo. Остальные Set/value пары, их accessible names,
source IDs и поддержка скрытых значений сохранены. Новые EN/RU подписи хранятся
в существующих application catalogs.

Нет второй таблицы возможностей форматов: source ShowItem/Enable/Check по-прежнему
определяют видимость и доступность. Не менялись precision items/default/custom,
M/C/A/Z-time decisions, metadata bools, locale rebuild, Help/F1 и OnOK. Cancel
не затрагивает Add draft; Save использует прежний nested commit. Метрики берутся
из Qt controls; исходные labels могут переноситься. Extra height уходит под форму,
не распределяется между группами. Существующий keyboard traversal сохраняет
первый доступный source control и Tab/Shift-Tab. Cocoa group backing получает
один логический пиксель внутри content, исключая обрезание на minimum height.

Повторное сравнение после правки подтвердило подключения прежних handlers и
неизменность их тел. Presentation buildControls остаётся одним сценарием
построения формы; общая геометрия выделена в уже существующий dialog layout,
новый functional owner или form engine не добавлялся. Regression resize проверяет,
что заголовок не уезжает вниз при увеличении окна; существующие tests сохраняют
keyboard enable/accept, Cancel, ZIP Unix/Windows/DOS, TAR, реальные metadata/link
архивы и повторное открытие. Не добавлены проверки констант/наличия исходников.

macOS: приложение и affected targets собраны, семь focused suites PASS.
Cocoa EN/RU 7z/ZIP/TAR/WIM initial/compact, keyboard/precision transitions и hot
language change в light/dark PASS; просмотрены widget JSON и снимки в прежнем
`build/dev/ui-redesign-review`. Windows/Linux: те же семь focused suites PASS;
Windows interactive elevated покрывает действительные WIM security/ADS и link
fixtures. На macOS/Linux Windows-only metadata cases корректно исключены.
Runners сохранили current/build/cache, свободное место проверено; старых payload
каталогов нет. Full/slow, VoiceOver, физические клавиатуры и новый sheet behavior
не проверялись. Остальные семейства окон и общая приёмка редизайна ещё в работе.

## Итерация 9: свойства, контрольные суммы и полный текст

Источник: https://github.com/ip7z/7zip.git, полный baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`; текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Сопоставлены полные `CPP/7zip/UI/FileManager/EditDialog.cpp/.h/.rc`,
`ListViewDialog.cpp/.h/.rc`, source selection/clipboard/detail/delete/activation
и modal-result branches, Qt CListView adapter и зависимые ресурсы GuiCommon.
Проверены вызывающие `PanelMenu.cpp::CPanel::Properties`,
`PanelFolderChange.cpp::CPanel::FoldersHistory` и
`UI/GUI/HashGUI.cpp::ShowHashResults`/`CHashCallbackGUI`.

Qt presentation использует общий dialog layout: расширяемая область текста или
таблицы и нативные content-sized кнопки. В отчёте Copy/Close заменяют OK/Cancel;
Close сохраняет исходный OK result и focused index, Escape — прежний Cancel.
История сохраняет Accept/Cancel roles и транзакцию, OK подписан Open.
Скрытый одноколоночный и пустой двухколоночный header, изменение ширины колонок,
полное выделение строк и горизонтальная прокрутка сохранены.

У upstream нет отдельной кнопки Copy all; новый presentation-путь формирует
индексы всех строк только при отсутствии выделения и передаёт их прежнему
сериализатору CopyToClipboard. Выделение не меняется; Control+C/Insert сохраняют
исходную семантику. Полные Values, display-only truncation/newline normalization,
Enter/Alt+Enter detail, DeleteIsAllowed и синхронное удаление Strings/Values
не менялись. После удаления последней строки Copy недоступен и не очищает
clipboard. Нет второго форматтера свойств или реализации вычисления хешей.

Полный текст остаётся plain/read-only/NoWrap с обеими прокрутками и ES_WANTRETURN.
Системная fixed-font family сохраняет исходный размер текста. Copy использует
выделенный либо полный текст, не закрывает окно и не меняет выделение.
Close изначально имеет фокус/default; Tab проходит edit→Copy→Close.
Повторная source-сверка подтверждает неизменность генераторов данных,
source callers, активации, сохранения истории и полных значений.

Новая регрессия проверяет копирование длинного Unicode/multiline значения,
выделенных строк и пустого списка после Delete; текстовый сценарий расширен
копированием literal full/selected текста. Существующие проверки подтверждают
Cancel истории, открытие полного значения, исходные результаты хешей, locale
reload, focus/default и normal/compact geometry. Удалена фиксация старого DLU
размера ListView; полезные проверки видимости и достижимости сохранены.

macOS: приложение и прямые потребители собраны, 12 focused suites PASS.
Cocoa Edit/List/Hash в light/dark — 38/38 test functions PASS; EN/RU footer reload
проверен существующим language suite. Просмотрены актуальные widget JSON и
снимки normal/compact в прежнем `build/dev/ui-redesign-review`.
Windows interactive и Linux: те же 12 focused suites PASS; run
`20261018T164309Z-fe8cc8a3`. Current worktrees и compiler caches сохранены,
старых payload-каталогов нет; до запуска свободно около 648 GiB на host D:,
63 GiB в Linux и 45 GiB в Windows.
Full/slow, VoiceOver, физические клавиатуры, новая sheet-модальность и сравнение
с отрисованным оригинальным Windows 7zFM не проверялись. Остальные семейства
и общая приёмка редизайна остаются в active goal.

## Итерация 10: окно выполнения

Источник: https://github.com/ip7z/7zip.git, baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`; текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Сопоставлены `CPP/7zip/UI/FileManager/ProgressDialog2.cpp/.h`,
`ProgressDialog2.rc/ProgressDialog2a.rc/ProgressDialog2Res.h`, GuiCommon и Qt
progress controls/transport/wait services. Рассмотрены callers CProgressThreadVirt,
archive operation и filesystem/volume worker bridges, source statistic/converter,
messages/copy, Pause/priority, cancel question, completion and taskbar branches.

В Qt presentation статус, bar и двухстрочный путь предшествуют основным
счётчикам времени/файлов. Source file-total находится рядом с обработанным числом;
errors label/value и numbered message list остаются вне Details. Раскрываемый
блок содержит все исходные total/speed/processed/packed/ratio controls. Скрытые
поля продолжают получать source updates; ShowCompressionInfo сохраняет свои
условия. Значения выровнены справа. EN/RU Details использует application catalog.

Согласованная замена геометрии: общий dialog layout, нативный QDialogButtonBox,
scrollable overview/details и отдельный растягиваемый error list. Компактная
форма сохраняет footer; начальная высота следует collapsed content. Отдельный
gutter защищает значения от overlay scrollbar. Source OnSize сохраняет алгоритм
выбора _numReduceSymbols по доступной ширине; прямоугольники и DLU-позиционирование
заменены layouts. Повторная source-сверка подтверждает, что statistic formatting,
progress converter/cache, source messages/full clipboard, синхронизация, workers,
приоритет, pause/stop, final messages и taskbar не переписаны.

Native проверка выявила Qt-специфичную потерю Close focus: скрытие ActionRole
кнопок перестраивает QDialogButtonBox focus chain. SetDefaultButton теперь идёт
после HideItem Background/Pause, до прежнего completion callback. Это изменение
только presentation-порядка; modal results и source finish/cancel branches
сохранены. Исходная worker-регрессия воспроизводила потерю фокуса до исправления.
Scroll viewport не получает отдельную остановку Tab; Details и сообщения
остаются доступны с клавиатуры. Повторная активация не принуждает новый фокус.

Geometry scenario расширен проверкой достижимости каждого раскрытого поля,
неперекрытия scrollbar, обновления скрытого счётчика и его значения после открытия.
Существующие suites сохраняют реальные success/failure/cancel, Unicode archive,
pause/background failure и finished-error Close. Native fitting ожидает Qt layout
events вместо проверки прежнего размера кнопки сразу после изменения подписи.
Worker geometry допускает minimumSizeHint и пишет report также при ошибке фокуса.
Language/worker review producers теперь применяют запрошенную тему; проверяется
фактический colorScheme в JSON. Contract сохраняет mapping всех 26 source IDs и
24 state reports; координаты скрытого списка и свернутых Details не закрепляются.
Тела новых UI-блоков рассмотрены как единое построение presentation в существующем
адаптере; functional owner или второй form engine не добавлен.

macOS: приложение и прямые потребители собраны; 15 focused suites PASS.
Cocoa EN/RU, collapsed/expanded, paused и finished-error Close в light/dark —
36/36 test functions PASS; фактические темы подтверждены metadata, текущие JSON
и снимки просмотрены в прежнем `build/dev/ui-redesign-review`.
Linux — 13 focused suites PASS; Windows interactive — 12 PASS (платформенные
наборы различаются). Итоговый run `20261018T170337Z-a16ca9fe`, включая source
26-ID/24-report contract. Первоначальные focus и hidden-list relation failures
устранены и перепроверены. Current/build/compiler cache сохранены; obsolete
payload-каталогов нет, после проверок host D: имеет около 647 GiB свободно.
Полный gate и slow suites не требуются этим presentation-изменением и не запускались.
VoiceOver, физические клавиатуры, новый sheet behavior и отрисованный оригинальный
Windows reference не проверялись. Benchmark, настройки, остальные формы и общая
приёмка редизайна остаются в active goal.

## Итерация 11: обе формы benchmark

Источник: https://github.com/ip7z/7zip.git, baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`; текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Сверены `CPP/7zip/UI/GUI/BenchmarkDialog.rc/.h/.cpp`, source control IDs и
desktop/total resources, OnInit, параметры/зависимые значения, Start/Restart/Stop,
OnCancel, completion/UpdateGui и command handlers. Рассмотрены entry Benchmark,
Qt benchmark services, реальный engine caller и lifecycle/command consumers.
Зависимости ресурсов — `CPP/7zip/UI/GUI/BenchmarkDialogRes.h` и
`CPP/7zip/GuiCommon.rc`; Qt layout/language/clipboard services заменяют только
платформенную границу.
Ресурсы UNDER_CE по-прежнему не являются desktop-формами.

Изменён CQtBenchmarkWindow presentation adapter; functional BenchmarkDialog.cpp
и официальный benchmark engine не менялись. Dictionary/threads подписаны над
полями, память находится рядом; Current/Resulting строки обеих операций и все
Size/Speed/Usage/RPU/Rating значения объединены в одну grid-таблицу. Source Total
Rating находится под соответствующими числовыми колонками. Pass limit, elapsed,
сведения о CPU/системе/версии, исходные ошибки и накопленный лог сохранены.
GroupBox заменены подписями строк; обработчики продолжают обращаться к тем же IDs.

Общая строка действий использует нативные размеры и роли: Copy/Restart/Stop/
Help/Close в обычном режиме и Copy/Help/Close в полном. Close вызывает прежний
source OnCancel с ожиданием worker. Initial focus остаётся у Restart либо total
console; фокус Stop при выключении по-прежнему возвращается через source handler.
Tab включает параметры, scroll viewport, непустой лог и footer; стрелки позволяют
прокручивать широкую таблицу. Отдельная старая компоновка не сохраняется.

Лог перемещён под таблицу и заменён read-only QPlainTextEdit; total console
остаётся исходным текстовым потребителем. Оба используют NoWrap, две прокрутки
и системную fixed-font family с прежним размером текста. Пустой лог скрыт.
При широкой таблице CPU/системные строки и лог ограничены шириной viewport;
wrapped labels получают высоту по фактической ширине, поскольку QGridLayout
может оценить spanning label по более широкому grid. Эта проблема воспроизведена
в Windows/Linux geometry и устранена в Qt boundary. Gutter сохраняет место
для native overlay scrollbar; footer остаётся вне прокручиваемого отчёта.

В upstream нет команды экспорта всего GUI-отчёта в clipboard. Qt Copy собирает
уже отформатированные source labels, параметры, сведения и полный лог; вычисления
метрик и AddRatingsLine не дублируются. Табличные строки сохраняют пустые колонки
span-ячеек при копировании TSV. Total Copy использует полное либо выделенное
содержимое консоли, не меняет выделение/worker и не закрывает окно; пустой вывод
не очищает clipboard. Это согласованное дополнение presentation из концепта.

Таблица/summary/copy выделены в qtbenchmarkmetrics.cpp у того же UI-владельца,
с CMake-регистрацией. Файлы presentation остаются сфокусированными; нового
хранилища результатов, владельца worker или formatter для вычислений нет.
Повторная сверка подтверждает подключение всех исходных values/callers и прежние
memory refusal, restart, stop, cancel/join, frequency/history и total-output branches.

Существующая регрессия расширена полным/selected copy длинного Unicode-вывода,
положением total values в скопированных колонках, keyboard scrolling и сохранением
открытого окна. Сохранены actual engine/command, отказ памяти, ошибки, перезапуск,
остановка/join, affinity fallback и hot locale проверки. Старые DLU/frame/group
assertions заменены видимостью, достижимостью, выравниванием и результатом.

macOS: приложение и три focused targets собраны; benchmark_lifecycle,
benchmark_command и native_benchmark — 3/3 PASS. Cocoa lifecycle EN/RU normal/
compact и completed states в light/dark — 36/36 test functions PASS; фактические
темы подтверждены JSON metadata, текущие снимки/JSON просмотрены в прежнем review.
Linux и Windows interactive: те же три focused suites — 3/3 PASS; итоговый run
`20261018T173147Z-068f9719`. Windows affinity fallback cases корректно исключены:
там используется исходный native helper. Первоначальные CPU geometry/wrapping
failures устранены и перепроверены на обеих ОС. Current/build/cache сохранены;
старых payload-каталогов нет, host D: имеет около 647 GiB свободно.
Full/slow, физические клавиатуры, VoiceOver и отрисованный Windows reference для
этого редизайна не проверялись. Settings и общая приёмка остаются в active goal.

## Итерация 12: сохраняемое оформление приложения

Источник: https://github.com/ip7z/7zip.git, текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; baseline выбранных модулей
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Сопоставлены SettingsPage.cpp/.h/.rc,
RegistryUtils.cpp/.h (CFmSettings), OptionsDialog.cpp и page callers, а также
Qt entry point и существующий semantic palette consumer. Исходные семь флагов,
порядок OnApply, privilege/memory branches и обработка ошибок сохранены.
Старое частичное применение исходных флагов перед ошибкой памяти не изменено.

В upstream нет используемой реализации выбора системного/светлого/тёмного
оформления приложения. Для согласованного концепта добавлена Qt preference
в CFmSettings и существующий settings store. Новая ветвь сохраняется после
успешной проверки памяти; контейнер применяет изменённый подтверждённый выбор
через QStyleHints. Entry point восстанавливает его до создания окон, включая
GUI entry 7zG. Отдельного хранилища, QSS или таблицы цветов нет; существующие
semantic colors получают нативную палитру. Unknown означает системный выбор.

Открытие окна и неподтверждённый draft не меняют native preference. Cancel,
Apply/reopen, ошибка памяти, сохранение соседнего флага и hot language reload
покрыты поведенческими проверками с private settings и fake association service.
Нативная проверка ожидает обновление палитры существующего окна: изменение
style hint доставляет palette events асинхронно. Keyboard consumer учитывает
новый первый контрол Settings. Удалено требование точного frame size из
fm_settings: полезные проверки доступности полей/действий сохранены.

Это завершённый сценарий оформления, необходимый для новых настроек.
Трёхсекционная навигация и раскрываемые группы Settings ещё не реализованы.

macOS: app и focused targets собраны; appearance_settings, options_dialog,
options_integration, options_keyboard, fm_settings, options_window,
list_settings_runtime, memory_lock и application_starts прошли. Нативный
memory_lock перепроверен вне sandbox после отказа Cocoa создать экран внутри
sandbox. Cocoa appearance — 5/5 test functions PASS; просмотрены applied-light/
dark JSON и снимки в прежнем review, включая палитру существующего окна.
Linux: девять тех же suites PASS. Windows: восемь PASS первоначально; точное
равенство frame в fm_settings заменено проверками видимости, затем
appearance_settings и fm_settings — 2/2 PASS на обеих ОС
(`20261018T182215Z-9b78d8ba`). Appearance на Windows использует windows QPA
и существующую native UI resource lock. Linux offscreen подтверждает транзакцию
и геометрию, но не смену палитры оконного окружения. Full/slow, VoiceOver,
отрисованный upstream Windows reference и все 36 состояний не проверялись.

## Итерация 13: три раздела настроек

Источник: https://github.com/ip7z/7zip.git, текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Сопоставлены OptionsDialog.cpp,
SettingsPage, SystemPage, MenuPage, FoldersPage, EditPage и LangPage с их
headers/resources/handlers, RegistryUtils, ZipRegistry, page callers, native
association/menu services и существующая property-sheet keyboard adaptation.
UNDER_CE resources не являются desktop-сценарием.

Qt-компоновка теперь объединяет шесть исходных владельцев в General,
Integration и Advanced. Функциональные OnInit/OnApply, Browse, flags/defaults,
RAM calculation, privilege, menu command flags, language metadata и native
registration/error branches сохранены. Presentation вынесен в
qtfmoptionslayout.cpp; отдельного draft/store/apply pipeline нет. Подгруппы
SettingsPage передаются в общую форму; source owner остаётся жив до закрытия.
Сворачивание меняет только видимость. Сводки читают реальные controls и dirty
состояние владельцев, включая непустые внешние команды, путь временных файлов,
лимит памяти и выбранные флаги списка. Они сохраняют смысл после Apply/reopen.

General показывает оформление/язык и раскрываемые сведения о переводе.
Advanced раскрывает память, временные файлы, внешние программы и привычное
поведение списка. Параметры не удалены, исходные defaults не заменены.
Ошибочная память раскрывает соответствующую группу и фокусирует поле.
Ctrl+Tab/reverse используют прежний Qt page selection и first-enabled focus,
включая физический Control на Cocoa. Help/F1 сохраняет контекст исходной группы
при переходе к footer; HTML-справка не переработана.

SystemPage сохраняет исходные CAssoc/Pair, ChangeState и OnApply целиком;
добавлены только обновления presentation. qtsystempagepresentation.cpp выводит
7z/ZIP сразу и полный source format inventory через Other formats. Checkbox
проецирует Own/not Own через прежний Clear/Other/Own cycle: при необходимости
переходит через Other, не переписывая state algorithm. Полный исходный цикл,
mixed-selection buttons, handler labels/icons и поддерживаемый all-users scope
остаются в Advanced associations. Недоступная all-users колонка скрыта.
Новое checkbox-представление — согласованная Qt UX-адаптация; upstream содержит
трёхсостоянийный list control, но не такой checkbox surface. Форматы и команды
берутся из исходных владельцев, а не из сокращённых примеров HTML.

MenuPage остаётся владельцем всех команд/Zone/shell bitness branches.
При отсутствии ShellIntegrationSupported вся группа отсутствует. Если нет
ни association capability, ни shell integration, нет и пункта Integration.
Отсутствие прав для поддерживаемой Windows операции по-прежнему возвращается
через исходный native error flow. Адаптеры и архивный backend не изменены.

Общий frame/footer использует нативные роли и прокручиваемые секции. Вложенная
scroll surface не заливается белым поверх нативного tab pane; сводки используют
системный secondary foreground. Native layout items Cocoa исключают внешние
focus/shadow margins кнопки: нижний native layout margin оставлен внутри
раскрываемого содержимого, чтобы Browse не выходил за его границы. Между
переключателем памяти и вводом задан общий интервал, устранивший native overlap.

Behavior coverage сохраняет Apply/Cancel/OK/reopen, частичное source применение
при ошибке памяти/ассоциаций, native conflict и отсутствие регистрации при
Cancel. Добавлены Other→Own/Own→Clear/повторный Own через checkbox, сохранение
скрытого draft и его сводки после reopening. Прежние жёсткие six-tab/initial
20-row/frame assertions заменены доступностью реальных полей через раскрытие
и прокрутку. Full Keyboard Access включается только внутри соответствующего
теста с восстановлением; приложение не меняет системную keyboard policy.
Добавлен русский перевод и EN/RU numerus для новых сводок.

macOS: приложение и затронутые targets собраны; 14 focused scopes прошли
(13 offscreen/startup и memory_lock отдельно native вне sandbox). После
геометрических исправлений десять непосредственно затронутых suites повторены
успешно. Cocoa integration/keyboard и выбранные geometry/language сценарии в
light/dark — 74/74 test functions PASS. Просмотрены актуальные JSON и снимки
General, Advanced collapsed/expanded, translation, форматов и association matrix,
памяти/временной папки в прежнем `build/dev/ui-redesign-review`; фактические темы
подтверждены capture metadata. Native OS keyboard policy сохранена.

Linux: первоначальные 14/14 и итоговые 10/10 PASS. Windows: первоначальные
13/14, затем после исправления folder reachability и включения menu_page в native
Windows QPA/resource lock — итоговые 10/10 PASS. Финальный run
`20261018T185422Z-741da083`; исходный широкий
`20261018T184611Z-a0c8cb93`. Остальные четыре неизменённых потребителя прошли
в широком прогоне. Гостевые current/build/cache сохранены, лишних root payloads
нет; Linux около63GiB, Windows около48GB и host около647GiB свободно.
Нативная регистрация реальных пользовательских ассоциаций не выполнялась:
поведенческие изменения проверены через fake services и прежние native tests.
Full/slow, VoiceOver, физические раскладки, новая sheet-модальность и полная
36-state приёмка не выполнены; goal остаётся active.

Дополнительно обновлены native Appearance reports новой формы — 5/5 Cocoa
functions PASS. Устаревшие task-owned снимки шести вкладок и разобранного
geometry failure удалены. Текущий Windows Menu JSON подтверждает windows QPA,
достижимую через прокрутку таблицу команд и полностью видимый footer.

## Итерация 14: результатные действия общей формы ввода

Источник: https://github.com/ip7z/7zip.git, текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Сверены ComboDialog.cpp/.h/.rc,
GuiCommon.rc OK_CANCEL, BrowseDialog::Dlg_CreateFolder, PanelOperations::CreateFile/
CreateFolder/ChangeComment, PanelSelect::SelectSpec и соответствующие
Qt callers, raw-value/validation/Cancel и language/button services. В исходном
Combo нет отдельной функциональной реализации действий: он возвращает Value,
а операции выполняют callers. Rename остаётся исходным inline-сценарием списка.

Изменён только BindLanguage presentation: устойчивые source IDs выбирают
Create/Select/Deselect/Save через существующий QtLangButtonBox::SetAcceptText.
Не анализируются переведённые window titles или текст пользователя; titleID=0
продолжает сохранять динамический заголовок комментария. Generic неизвестные
использования оставляют OK. Native OK role, OnOK, Strings order, raw Value,
focus, accepted/rejected codes и все source operation bodies неизменны.
В upstream такого уточнения caption нет; это согласованная UX-адаптация.

Существующий language suite расширен пятью реальными operation identities:
EN→RU→EN сохраняет draft, caller-built title и соответствующее действие;
подтверждение через кнопку возвращает тот же Unicode/raw текст. Normal/compact
геометрия проверяется для обоих языков. Исходные success/error/Cancel сценарии
создания, выделения, комментариев и source browser остаются прямыми потребителями.

macOS: приложение и шесть focused targets собраны; combo_dialog,
language_dialog, source_create, source_selection, filesystem_comment и
source_browse_dialog — 6/6 PASS. После добавления EN geometry report
language_dialog повторён PASS. Cocoa пять сценариев EN/RU normal/compact в
light/dark — 14/14 test functions PASS; текущие JSON/PNG просмотрены, actual
theme metadata подтверждены. Linux и Windows interactive: те же шесть suites
и application_starts — 7/7 PASS, run `20261018T190407Z-6a5a46e4`. Current/build/
ccache сохранены, root гостей только current; full/slow и физические клавиатуры
не проверялись. Реальный Windows reference для pixel parity не получен.
Оболочка справки/About, оставшиеся случаи каталога и общая приёмка остаются
в active goal; эта итерация не означает завершение всего плана.

## Итерация 15: справка и About

Источник: https://github.com/ip7z/7zip.git, текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Сверены HelpUtils.cpp/.h,
AboutDialog.cpp/.h/.rc и зависимости MyVersion/CpuArch/LoadCodecs/GuiCommon.rc,
Qt help/about callers и адаптер bundled CHM HTML. Исходные OnInit с codec error,
OnHelp/start.htm, homepage dispatch и сведения об upstream сохранены.
Заменённая раньше продуктовая идентичность About остаётся текущей; исходный
логотип/геометрия не возвращаются. Нового upstream functional code не копировалось.

Обе оболочки используют QtCreateDialogLayout с общими outer/footer интервалами.
Help сохраняет существующие статьи, HTML/CSS adaptation, URL/fragment resolution,
history, external links и предупреждение недоступной темы. Browser получает
начальный фокус; Back остаётся связанным с actual history. Close использует
прежнюю native роль через существующий QtLangButtonBox. Не добавлены макетные
статьи, сокращённая sidebar-навигация или другой renderer.

About сохраняет Sevenzip/version/project URL и отдельную 7-Zip attribution panel
с версией/CPU/датой/copyright/free-software notice/license/homepage. Внутренние
интервалы identity panel не заменены outer frame. Caption Close локализован,
но прежняя OK/Accept роль и default Return сохранены; Escape/window close
не меняются. Footer вынесен в общий frame. Styled project link создаётся сразу
с конечным footer parent: создание style binding до reparent читало старую
палитру при parent PaletteChange и нарушало контраст живого окна. Существующая
регрессия воспроизвела это; глобальный semantic palette service не менялся.

Help regression теперь использует реальную кнопку Back и проверяет её
availability/history, вместо прямого вызова backward. Сохранены проверки
исходного текста PRE, длинных таблиц, внутренних/внешних ссылок, fragment/topic,
language reload, missing topic, codec error, default Return и полного About
attribution. Проверка width>=sizeHint для кнопок About заменена помещением
реальной надписи в native contents с ожиданием queued layout после перевода.
Нативные Help/About tests на Windows включены в существующую QPA/resource lock
группу; отдельного lifecycle или storage не добавлено.

Native F1 диагностика показала, что ранний isActiveWindow мог пройти до
QApplication::activeWindow/focus: клавиша отправлялась при пустом active window
(иногда ApplicationInactive), и Shortcut event не возникал. Help UI использует
существующий WindowActivation fixture, проверяет actual active window перед
оконным вводом и дожидается появления справки. Обработчики F1 приложения не
менялись. Тест имеет private settings fallback при прямом запуске; временный
trace удалён, созданный ранним диагностическим запуском test-only config убран.

macOS: приложение и три focused targets собраны, help_source/help_ui/
language_standard_buttons — 3/3 PASS. Cocoa Help/About EN/RU normal/compact в
light/dark — 54/54 test functions PASS; после уточнения caption-fit повторены
оба help_source прогона — 46/46 PASS. Native help_ui — 8/8 PASS, включая
окончательную активацию окон/F1. Просмотрены текущие JSON/PNG обоих окон и
actual theme metadata в прежнем review. HTML, таблицы/PRE и ссылки сохранены.

Linux: первоначальные четыре suites со startup — 4/4 PASS; адресные проверки
последующих test-fixture изменений также проходят. Windows: startup и standard
buttons прошли первоначально; после замены жёсткого sizeHint assertion native
help_source PASS (`20261018T191814Z-f7780b96`), окончательный native help_ui PASS
(`20261018T193256Z-985cc3fd`). Общий исходный run —
`20261018T191306Z-85e5cf79`; native F1 диагностика —
`20261018T193019Z-166f3f4c`. Текущие Windows JSON подтверждают windows QPA,
видимые Help browser/footer и About footer с полной надписью Close.
Current/build/ccache сохранены; full/slow, физические клавиатуры, VoiceOver и
полная приёмка каталога не выполнялись. Browse/temporary формы и оставшиеся
расхождения каталога ещё требуют общей сверки; goal остаётся active.

## Итерация 16: встроенный выбор и временные файлы

Источник: https://github.com/ip7z/7zip.git, текущий upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, baseline браузеров
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Сопоставлены BrowseDialog.cpp/.h/.rc
и BrowseDialog2.cpp/.h/.rc, ресурсы, OnInit, OnOK/FinishOnOK, Reload,
parent/create/sort/key/context handlers и callers MyBrowseForFolder,
CBrowseInfo::BrowseForFile, MyBrowseForTempFolder. Проверены зависимости
FileName/Find/Dir, source vectors/formatting, Combo/Create Folder, LangUtils,
Qt owned-temp leases и существующие delete/external-open guards. Между baseline
и текущим upstream в этих модулях добавлены только lifetimebound annotations;
новый functional source в этой итерации не копировался.

Обе формы используют общую рамку и native footer. Встроенный long/super/device
browser получил видимые Path/File type и caption Choose folder/Open/Save по
существующему режиму; роль OK и source resolution не изменены. Скрытый folder
filter больше не резервирует пустую строку. Native QFileDialog для обычных путей
не менялся. Parent/Create Folder сохраняют QPushButton и focused Return,
получили tooltips/accessibility с live translation. Общий helper ограничивает
ширину по native sizeHint; первоначальный расчёт только от высоты/PM_ButtonMargin
давал на Cocoa 32px кнопку с 8px contents для 16px icon. Native geometry check
выявил это; окончательный размер сохраняет достаточную область иконки.

Временная форма сохраняет все шесть source columns, selection, counters,
sorting, context commands, readonly path, Help/Close, начальный Close focus и
особый OnOK с actual list focus. Единственный disabled IDC_BROWSE2_FILTER
заменён информационной сводкой: owned scope, selected/total и уже существующая
причина защиты выбранного элемента. Upstream filter handler закомментирован,
активного выбора/фильтра не было. Регистрация владельцев, обход каталогов,
удаление, подтверждения, защита symlink/active/external и запуск файлов не
переписаны; functional BrowseDialog2.cpp не менялся. После правки повторно
сверены подключения footer/toolbar/list и сохранность source branches.

В затронутых tests удалены проверки точного DLU-размера и скопированного
source sample/ширин колонок. Проверяются видимость и отсутствие пересечений,
помещение native icon, отсутствие пустой скрытой строки, mode captions,
обновление реального выделения/статуса, объяснение disabled Delete и длинный
русский текст при живой смене языка. Существующие проверки raw Unicode/пробелов,
Cancel, фильтра, навигации, Return, сортировки, фактического удаления и guards
сохранены. Temp test имеет private settings fallback для прямого native запуска.

macOS: приложение и пять focused targets собраны; source_browse_dialog,
temp_cleanup, browse_dialog, owned_temp, temp_detached_launch — 5/5 PASS.
Cocoa полные source_browse/temp_cleanup в light/dark — 88/88 test functions
PASS; финальное расширение protected EN/RU normal/compact — 6/6 PASS и
повторный temp_cleanup CTest PASS. Просмотрены current-build JSON/PNG с actual
light/dark metadata, 420×400 browser и 500×320 protected temp, без clipping.

Linux: шесть suites со startup — PASS; финальные temp_cleanup/owned_temp —
2/2 PASS. Windows: startup/browse/source_browse/temp_detached_launch прошли;
owned_temp/temp_cleanup первоначально упали на создании symlink обычным token.
Повтор с документированным interactive elevated test mode — 2/2 PASS,
без изменения UAC/developer mode/прав пользователя. Temp включён в существующую
native Windows QPA/resource-lock группу. Windows JSON подтверждает Segoe UI,
windows QPA и полную видимость полей/footer/защитного статуса EN/RU compact.
Runs: `20261018T195207Z-93fca917`, `20261018T195449Z-5325cb41`.
Current/build/ccache сохранены; устаревших отдельных guest worktrees не найдено.
Rendered original Windows pixel parity, native Linux appearance, VoiceOver,
sheet-modality и общая приёмка каталога этой итерацией не установлены.

## Итерация 17: актуальный каталог и inline-переименование

Каталог сверён с текущими владельцами всех 36 представлений. В macOS 35 записей
ссылаются на текущие Cocoa reports; Windows-only menu owner сохранён с явным
omission вместо ссылки на старую несуществующую вкладку. Шесть исходных owners
настроек связаны с состояниями трёх разделов и раскрытых групп. Инвентарь
пересобран из видимых контролов этих reports с revision/theme/font metadata;
он остаётся снимком, а не новым контрактом внутреннего устройства.

Сборщик включает новые Add/Extract collapsed/password/expanded, Settings groups,
protected temp и actual inline rename. Платформенная область учитывается и при
сборе, и при отображении старого manifest; устаревшие macOS menu-page captures
удалены. HTML-концепт больше не показывает отдельный Rename dialog: ввод идёт
в строке главного списка с Enter/Escape; короткая форма сохранена для Create,
Select и Comment. Wrapper обновлён из того же исходника. Браузерные проверки
подтвердили commit/cancel inline input, загрузку галереи, фильтры и viewer.

Native rename capture выявил обрезание QFocusFrame на первой колонке значков:
рамка выходила на 3px за viewport. После source comparison добавлен только
горизонтальный inset редактора QListView на macOS по PM_FocusFrameHMargin;
исходные алгоритмы и Qt deferred ownership не менялись. Сверены upstream
PanelOperations.cpp OnBeginLabelEdit/OnEndLabelEdit/RenameFile, PanelListNotify
LVN handlers и текущие mainwindow_rename/filetable/mainwindow_panel callers.
Источник https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; baseline не изменён, functional
source не копировался и ветви не исключались. Editor всё ещё использует source
validation/operation/error/refresh selection. Существующий native regression
воспроизвёл выход рамки; новый test ради наличия helper не добавлялся.
Review entry point включён у этого target для фактического light/dark выбора.

Текущая Cocoa галерея: 37 producers × две темы, 1022 снимка состояний, без
незавершённых producer runs. Это дополнительные состояния, не 1022 разных окна
и не 37 независимых классов. В initial pass один dark selection run потерял
активацию окна; отдельный повтор прошёл, диагностический лог сохранён.
Оба native rename повтора после fix прошли, включая table/icon, обе панели,
alternative marks, links, resize и Escape. Просмотрены текущие native ring
images и widget/theme metadata; все ссылки каталога разрешаются.

macOS app и прямые targets собраны; panel_rename_state/ui_layout/deferred_rename
3/3 PASS. Linux/Windows app + те же suites — 4/4 на каждой ОС, run
`20261018T200703Z-0addec14`; Windows interactive elevated для symlink fixtures.
Сохранены incremental current/build/cache; места достаточно, старых guest
worktrees нет. Полный gate и slow matrices не требовались для этого локального
geometry fix. Галерея — macOS evidence; native Linux appearance, оригинальные
Windows pixels, VoiceOver, physical layouts и modality требуют отдельной
проверки, goal остаётся active.

## Итерация 18: контекстные sheets на macOS

Источник: https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; baseline модулей не изменён.
Сверены Windows/Control/Dialog.cpp CModalDialog::Create/DialogBoxParam с Qt
exec boundaries, parent/result ownership у Combo/Copy/Split/Password/Mem/
Overwrite/Link и их ранее проверенными OnInit/accept/Cancel consumers.
Проверены Add/Extract, COptionsDialog::Create, source Browse/CommonDialog
и callback-password GUI-thread transport. Upstream Win32 modal owner не
содержит Cocoa sheet adaptation; нового functional source не копировалось.
Операции, validation, исходные ветви, sync results и worker lifetime сохранены.

Контекстные формы явно выбирают WindowModal при наличии parent на macOS;
это не побочный эффект shared layout. Progress сохраняет ApplicationModal,
Help/отчёты не получают новой модальности. Parentless и Windows/Linux entry points
сохраняют прежнюю политику. Native file picker получает тот же opt-in;
folder picker теперь использует явный QFileDialog с прежними Directory/
ShowDirsOnly, начальным путём, accepted-only output и cleanPath, чтобы задать
window modality. Native renderer и source long/super/device fallback сохранены.

Решение опирается на Qt QDialog/window-modality и Qt::Sheet documentation:
рекомендуется setWindowModality вместо ручного Sheet window type. Страница Apple
Sheets недоступна без JavaScript, её Markdown endpoint не ответил; использована
локальная HIG-выжимка, проверенная 18 октября 2026. Браузерная имитация sheet не
считается доказательством. Новая regression на Cocoa проверяет фактический
NSWindow.sheetParent через узкий test-only AppKit observer и реальное Qt window
input: owner action заблокирована, independent action доступна; после Cancel
сохраняется черновик и прежний focus target. Вложенный password Cancel оставляет
родительскую форму/Unicode-пробелы и последующее подтверждение работоспособными.
Memory/Overwrite сохраняют IDCANCEL, а не Qt Rejected.

macOS: приложение и все affected targets собраны; 13 direct suites PASS,
source_create/source_selection/modal_refresh_window/language_focus — 4/4 PASS.
Cocoa context_forms в light/dark — 20/20 test functions PASS, 14 current captures
добавлены в существующую галерею. Source open callback, update worker и archive
progress password callback нативно проверены отдельно: success/empty/Cancel,
15/15 test functions PASS, без зависания owner thread или потери результата.
Приватные fixtures/settings очищены после завершения.

Linux/Windows: application_starts и 13 direct suites — 14/14 на каждой ОС,
run `20261018T201836Z-800dcf60`; Windows interactive elevated для source symlink
fixtures. Cocoa-specific adapter не меняет их modality. Incremental trees/cache
сохранены, guest roots содержат только current. Full/slow не запускались:
изменение покрыто contextual behavior и прямыми consumers. VoiceOver, physical
EN/RU input и итоговый completion audit остаются отдельными проверками.
Параллельные HTML/catalog правки пользователь подтвердил как эксперимент;
они не входят в эту Qt-итерацию и не заменяют согласованный план.

## Итерация 19: доступность раскрываемых секций

Источник и baseline перенесённых модулей остаются прежними:
https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
QtDisclosure — ранее согласованное Qt-представление, отсутствующее в исходных
Win32 resource groups. Повторно проверены его toggle/visibility boundary и
потребители Add/Extract, Progress, Settings; параметры и source handlers
по-прежнему принадлежат существующим формам. Нового upstream-кода не копировалось,
baseline и полнота переноса операций этой итерацией не меняются.

Обнаруженный дефект воспроизведён через публичный QAccessible: заголовок не
сообщал expandable/collapsed/expanded, хотя раскрывал содержимое. Узкий адаптер
на публичном QAccessibleWidget сообщает эти состояния и уведомляет об изменении;
press вызывает прежний button click. Qt владеет временем жизни интерфейса.
Имя следует текущей подписи или явно заданному accessibleName. Геометрия,
рисование шеврона, значения, сводки и source control flow не менялись.
Контракт API сверён с [Qt accessibility documentation](https://doc.qt.io/qt-6/qaccessible.html).

Новая regression до исправления падала на отсутствующем expandable. После
исправления проверяет assistive press, программное раскрытие и Space, сохранение
Unicode/пробелов черновика, смену имени и disabled guard. macOS: приложение и
affected targets собраны; disclosure_accessibility, source_create,
source_extract_advanced, progress_dialog, fm_settings, options_keyboard — 6/6 PASS.
Windows/Linux: те же шесть suites и application_starts — 7/7 на каждой ОС,
run `20261018T203348Z-5e7e0133`; Windows interactive elevated для существующих
source fixtures. Incremental trees/cache сохранены. Перед запуском оба runner
unlocked, корни содержали только current, устаревших snapshot payloads не было;
host/guest space достаточно. Full/slow не запускались: изменение покрыто общим
контролом и прямыми потребителями, геометрия не менялась.

По прямому указанию пользователя проверки продолжаются только внутри Qt.
VoiceOver и физические EN/RU раскладки остаются непроверенными; результат
QAccessible не выдаётся за нативную screen-reader приёмку. Приватная dev-сессия
завершилась, её disposable settings/fixtures удалены; пользовательская dist-сессия
не изменялась. Параллельные изменения плана и HTML-концепта сохранены отдельно.

## Итерация 20: архивные формы по обновлённому концепту

18–19 октября 2026 пользователь разрешил внедрять обновлённый план, потребовал
сверять Qt с концептом и уточнил: параметры сжатия тоже закрыты при первом показе.
План, HTML и preview приведены к этому начальному состоянию. Новая навигация
Settings и контурные иконки остаются отдельными незавершёнными сценариями.

Источник: https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; baseline модулей не изменён.
Сверены CompressDialog.h/.cpp/.rc: OnInit/OnOK, format/profile controls,
GetFinalPath_Smart/SetArcPathFields, OnButtonSetArchive и вложенные Options с
текущими construction/language/source/preferences callers. Для выбора папки
сопоставлены BrowseDialog::MyBrowseForFolder, native/custom ветви и
ExtractDialog::OnButtonSetPath. Нового upstream-кода не копировалось;
функциональные тела операций, профилей, SFX и метаданных не переписывались.

Прежние controls Add распределены по четырём независимым группам: сжатие, файлы,
тома, командная строка. Все изначально закрыты. Формат, уровень, имя, папка и
переключатель защиты видны сразу. Сворачивание сохраняет effective options и
черновик; скрытые nondefault compression/file/volume/custom options имеют короткую отметку
Modified. Extract также получил короткую отметку. Options открывает прежнюю
транзакцию метаданных. Ошибочный размер тома раскрывает поле перед возвратом фокуса.

Имя и редактируемая папка сохраняют исходные name/DirPrefix и GetFinalPath_Smart.
Qt проверяет embedded NUL также в новом поле папки: Win32 text control не мог
передать такую строку. Browse намеренно выбирает папку через существующий
MyBrowseForFolder, сохраняя имя/формат. Прежний общий save picker заменён по
концепту; формат остаётся в основной форме, история восстанавливает исходное
разделение папки/имени/формата. OnButtonSetArchive/filter implementation сохранён.

Сопоставление rendered HTML с Qt выявило и устранило первоначальные расхождения:
readonly папка вместо отдельного поля, правое выравнивание, отсутствие внутреннего
отступа групп, высокий блок памяти и длинные сводки. Теперь name/destination
имеют отдельные строки, подписи выровнены слева, раскрытое содержимое отступает
от заголовка. Лимит памяти — отдельная строка; исходные usage/limit/RAM и
decompression значения показаны компактно под ней. Защита использует компактные
строки. Modified находится справа в заголовке и скрыт при раскрытии; шеврон
остаётся на первой строке. Native QComboBox/history, размеры и кнопки Qt сохранены.

Регрессии проверяют effective options и password draft при смене групп,
редактирование Unicode-папки через source path resolver, Directory picker
success/Cancel без смены имени/формата и раскрытие ошибочного тома. Прежние tests
открывают группы общим fixture. Проверка alignment constant заменена видимостью
полного значения; pixel-exact центры нативных рамок — общей строкой текста.

macOS: приложение собрано; 27 focused suites PASS. Подпись метаданных и новый
picker fixture отдельно перепроверены. Cocoa light/dark проверка выявила выход
spanning checkbox за край группы: узкая macOS WA_LayoutUsesWidgetRect adaptation
устранила дефект. Archive-forms/compression-language после fix PASS в обеих темах;
Add/Extract и прямые Settings disclosure consumers имеют актуальные Qt captures.
Финальные ui_geometry/source_compress_dialog/language_ui/disclosure_accessibility
после Cocoa fix — 4/4 PASS. Использован проектный engine26.04 и прежние runtime/cache.

Windows/Linux: application_starts и 27 focused suites — 28/28 на каждой ОС,
`20261018T210242Z-49fc9712`. Финальные path/geometry consumers и app — 3/3
на каждой ОС (`20261018T211101Z-7aa3608f`), затем app/source_compress_dialog —
2/2 (`20261018T211513Z-17f13d16`) после сверки отметки сжатия с концептом.
Перепроверены и NUL в папке, и сохранение параметра при закрытии/открытии группы.
Incremental current/build/cache сохранены. Guest roots содержали только current;
устаревших payloads не было, место проверено. Full/slow не запускались: scope
покрыт архивными формами и прямыми потребителями общего раскрытия.

Отдельно закрыт пробел splitter-приёмки: window_placement/panel_key/panel_shell —
3/3 на macOS, Windows и Linux; remote `20261018T203811Z-4a836ce6`.
Проверены minimum/resize/keyboard/reset/toggle, пропорции, paths и фокус.
VoiceOver и физические EN/RU клавиатуры отложены пользователем. Проверки и
снимки выполнялись внутри Qt, без Computer Use.

## Итерация 21: страницы настроек и поиск типов файлов

19 октября 2026. Основание — обновлённый план и требование пользователя сверять
реализацию с отрисованным концептом. Источник: https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; baseline адаптированных Options,
System/Settings/Menu/Folders/Edit/Lang модулей остаётся
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

До изменений сопоставлены OptionsDialog.cpp и property-sheet activation/apply,
SystemPage.h/.cpp/.rc с полными ChangeState/OnInit/OnApply, текущие callers и
association services. Просмотрены ресурсы, чтение/применение и Qt controls
остальных пяти владельцев, FilePlugins extension inventory и IFolderManager.
Нового upstream-кода не копировалось. Win32 property-sheet presentation заменена
адаптивной навигацией; исходные state/apply owners, регистрация, проверки прав,
обработка ошибок и порядок применения сохранены. После изменений повторно
проверены подключения owners, Help и source transition/apply branches.

Вместо трёх больших разделов — самостоятельные задачи из концепта. Боковая панель
продолжается до нижнего края; в узком окне названия переносятся наверх целиком и
в прежнем порядке. Оформление и язык остаются в основных настройках; сведения
о переводе закрыты. Память имеет зависимое поле и подпись объёма RAM из прежнего
вычисления владельца. Настройки списка сгруппированы по отображению и управлению.
Программы и временные файлы имеют отдельные страницы. Неподдерживаемые страницы
системной интеграции скрыты вместе с навигацией; Windows commands остаются
отдельной страницей там, где доступны.

Типы файлов показаны единым списком с названием, расширением и выбором Sevenzip.
IFolderManager предоставляет расширения, поэтому подписи используют существующий
локализованный шаблон SystemPage, без собственной таблицы названий форматов.
Поиск фильтрует имя/расширение, не меняет выбор и не включает Apply. Массовые
действия охватывают весь список; есть общий счётчик и пустое состояние поиска.
Дополнительная исходная матрица доступна только при поддержке all-users scope.
Для отмены ещё не применённого выбора адаптер возвращает исходное Other через
source cycle; он не запрашивает удаление прежнего внешнего обработчика.
Применённый Own по-прежнему можно снять через Clear. ChangeState и системный
OnApply не переписаны.

Сверка HTML и Cocoa выявила и устранила светлую боковую панель в тёмной теме,
обрезанные названия компактной навигации, позднюю смену компоновки и сжатие
индикаторов таблицы. Палитра панели выводится из текущего Window; общий QSS не
добавлен. Нативный overhang кнопок учитывается внутри Settings на macOS, чтобы
нижний Browse не обрезался прокручиваемым телом. Скрытый старый tab bar исключён
из Tab chain; Ctrl+Tab сохраняет исходное правило активации, обычный Tab достигает
footer. «?» сохраняет переведённые accessible name/tooltip и контекст Help;
нативный QDialogButtonBox сохраняет платформенное расположение ролей действий.
Save/Apply/Cancel используют прежнюю транзакцию, отдельного черновика нет.

Регрессии проверяют поиск без системных записей, выбор всех при активном фильтре,
сохранение выбора при смене страниц, восстановление Other без записи после undo,
Apply/Cancel/restart и ошибки прежних владельцев. Проверка нижнего расположения
поля памяти теперь относится к его ближайшей подписи. Проверяются размеры
индикатора по текущему QStyle, EN/RU navigation и доступность footer.
Проверка непересечения соседних controls больше не требует старого порядка
Large Pages относительно нового блока памяти; видимость всех controls сохранена.

macOS: приложение собрано; 13 focused suites, включая help_ui, PASS. Cocoa EN/RU,
normal/compact, light/dark captures options-pages/settings-sections/settings-folders
проходят; просмотрены текущие JSON и затронутые изображения. Каталог обновлён
для отдельных страниц, поиска и пустого результата; общий реестр окон использует
текущие captures и source digest. Эти проверки не означают проверку VoiceOver
или физических EN/RU клавиатур: пользователь отложил их и разрешил только Qt.
Полный gate и slow suites не запускались: scope покрыт настройками, общей строкой
кнопок и прямыми потребителями. Контурные значки остаются следующим сценарием.

Linux: 12/12 suites (`20261018T215837Z-8a15ce18`), help_ui
(`20261018T220059Z-d691964f`) и финальные 7/7 прямых потребителей навигации
(`20261018T220411Z-f718a3e5`) PASS. Windows проверен на активном desktop:
12 suites PASS в `20261018T220410Z-525ea32a`, оставшийся options_dialog PASS
после снятия старого требования порядка (`20261018T220547Z-c1c4a7c3`).
Первый неинтерактивный Windows запуск с geometry/exposure failures не считается
приёмкой. Compact navigation после него перенесена по длине названий, как в HTML,
вместо одинаковых плиток: дополнительная Windows-страница не отнимает лишние ряды.
Приложение собрано на трёх ОС; rendered concept сравнивался с Cocoa, без заявления
пиксельного совпадения Windows/Linux. Использованы прежние build trees/cache;
guest roots содержали только current, свободно 62 ГиБ Linux и 44,7 ГиБ Windows.
Все прогоны завершены, locks освобождены; dist и пользовательская сессия не менялись.

## Итерация 22: контурные значки команд

19 октября 2026. Сверены исходные `App.cpp` CButtonInfo,
g_ArchiveButtons/g_StandardButtons/SetButtonText и создание toolbar, ресурсные
IDB_* пары, Panel.cpp VIEW_PARENTFOLDER и его QAction projection в Qt.
Для диалогов сопоставлены BrowseDialog OnInit/parent/create handlers и ресурсы,
BrowseDialog2 и Qt temp presentation. Источник — https://github.com/ip7z/7zip.git,
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; baseline перенесённых модулей
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Повторный diff подтверждает:
изменились только setIcon и поставщик графики; обработчики, ID, доступность,
локализация и владение командами не переписаны. Исходные bitmap resources
сохранены как часть baseline, но новые команды их не используют.

Векторная графика адаптирована из https://github.com/lucide-icons/lucide,
полная ревизия `1ef60af944de207e0d5176905a0759ba12e6ee8d`:
`icons/plus.svg`, `folder-output.svg`, `check.svg`, `copy.svg`, `move-right.svg`,
`trash.svg`, `info.svg`, `arrow-up.svg`, `folder-plus.svg`, `rotate-cw.svg`.
Канонический trash в этой ревизии соответствует прежнему trash-2 концепта.
Геометрия перенесена в QPainterPath, SVG arcs — в дуги/кривые Qt; это адаптация
ресурса, без собственного SVG parser или новой runtime-зависимости. Размеры,
round cap/join и stroke сохраняют единый язык набора. Сохранена полная исходная
ISC/Feather MIT LICENSE, включённая в notices для macOS/Linux/Windows installs.
Никакая графика не загружается приложением из сети.

QtCommandSymbol создаёт масштабируемый QIconEngine: текущие ButtonText,
Disabled и Selected цвета читаются при рисовании, физическая плотность задаётся
scaledPixmap. На toolbar сохранены семь исходных действий, подписи двух основных
и существующие preferences размеров/групп/текста. Тот же renderer используется
у перехода вверх в обеих панелях, встроенном Browse и очистке временных файлов;
создание папки, удаление и обновление получили соответствующие образы концепта.
File/namespace/system/application icons не входят в замену; native shell menu
brand bitmap также сохраняет прежнее назначение.

Отрисованный HTML main сопоставлен с текущими Cocoa main/Browse/temp captures:
образы команд, outline, отсутствие лишних рамок, disabled states и подписи.
Снимки обычного/компактного EN/RU в light/dark обновлены в текущей галерее.
Нативные размеры контролов сохранены, масштаб символа определяется заданным
QIcon size и плотностью экрана. Рамки фокуса остаются у нативного контрола.

Регрессия рисует реальные QAction icons из MainWindow при двух палитрах,
normal/disabled и 1×/2×: проверяет цвет непрозрачных штрихов, непустую графику,
логический размер и отсутствие clipping. Она обнаруживает прежние статичные
bitmap colours и неверный HiDPI contract без проверки имён ресурсов/файлов.
macOS: приложение собрано, десять focused suites PASS, включая toolbar geometry,
keyboard commands, Browse/temp, language и native file/namespace consumers.
Native Cocoa main/browse/temporary-files light/dark captures PASS; просмотрены
все три затронутых вида. Full/slow не запускались: изменён только поставщик
графики и прямые потребители. VoiceOver/физические клавиатуры по-прежнему отложены.

Windows/Linux: `20261018T221311Z-b1854a6a`, Linux10/10 PASS; Windows9/10 PASS.
Windows temp_cleanup повторён штатным interactive/elevated fixture:
`20261018T221545Z-c113e8cd`, PASS. Первый запуск не имел права создания тестовых
symlinks; код удаления/защиты и тестовые сценарии ради него не изменялись.
Все десять выбранных наборов проверены на трёх ОС, Windows — на активном desktop.
Приложения собраны; оригинальный Windows pixel reference не использовался:
визуальный reference этой адаптации — согласованный HTML, inspected runtime — Cocoa.
Build/cache сохранены; перед прогонами guest roots содержали только current,
свободно Linux62 и Windows44,7 ГиБ. Прогоны завершены, locks освобождены.
Пользовательская dist-сессия не обновлялась. Общий goal требует отдельной
сверки всех требований плана перед объявлением завершения.

## Итерация 23: итоговая сверка с концептом

Дата: 19 октября 2026. Reference — текущий rendered HTML-концепт,
`work/design/sevenzip-concept.html`, и согласованный план; это намеренная
адаптация Qt, не заявление о pixel parity с оригинальным Windows 7zFM.
Повторно сопоставлены изменённые области OptionsDialog и ExtractDialog с
полными исходными ресурсами/обработчиками и зависимостями baseline, указанного
выше. Функциональные тела страниц, порядок применения, source OnOK/OnCancel,
параметры распаковки и вложенного диалога метаданных не изменены; новых ветвей
архивных операций и пропущенных исходных зависимостей в этом изменении нет.

При визуальном сопоставлении исправлены три расхождения. Сведения о переводе
при раскрытии занимают место описания с тем же началом и отступом; заголовок
остаётся на месте. Кнопка метаданных архива имеет естественную ширину, как в
концепте, и полностью помещается по высоте на Cocoa. Компактная помощь настроек
находится слева от группы завершения на всех ОС; основная группа сохраняет
нативный порядок действий. У «?» убрана стандартная иконка, которая на XCB
не помещалась рядом с символом; accessible name, tooltip, F1 и исходный help
context сохранены. Это изменения представления и узких Qt-адаптеров.

Регрессии проверяют сохранение черновика при раскрытии, совпадение начальной
позиции описания/содержимого, доступность помощи, её порядок в фокусе и работу
Apply/Save/Cancel через реальные действия. Потребители Settings используют
явную группу основных действий, поскольку Help теперь отдельная группа.
Геометрия распаковки ожидает завершения нативной раскладки и проверяет
полностью доступные поля с высотой не меньше нативного minimumSizeHint.
Прежняя проверка точного размера окна заменена проверкой пригодности контролов:
оконный менеджер может применить новую минимальную высоту асинхронно. Ранний
XCB-снимок с полем высотой 2px выявил недостаточность одной fullyVisible;
итоговый снимок показывает поля нормальной высоты. Код операции ради снимка
не менялся.

Проверки затронутых владельцев и прямых потребителей:

- macOS: приложение собрано, двенадцать Settings/help/language suites PASS;
  четыре archive/UI suites PASS. После усиления проверки высоты ui_geometry
  повторён, PASS. Native Cocoa галерея обновлена: 76 выбранных запусков,
  1138 пар PNG/widget JSON, без ошибок и отсутствующих assets. Это датированное
  свидетельство, не обязательное число окон приложения.
- Linux: пятнадцать focused suites PASS, `20261018T230125Z-e5218aec`.
  Native XCB/Openbox в отдельном Xvfb проверил main, archive forms, disclosure,
  progress, benchmark, Settings, Help и Qt keyboard routing. Десять текущих
  категорий отчётов сохранены в `build/dev/ui-native-system-review-linux`.
  Финальная распаковка с контролем высоты PASS, `20261018T230516Z-dfdc9107`.
  Один Cocoa-specific native-key case пропущен на Linux по назначению.
- Windows: тринадцать Settings/archive/UI suites PASS,
  `20261018T224657Z-543e5fe8`; четыре archive suites на финальном коде PASS,
  `20261018T225126Z-1a60242d`. Усиленный ui_geometry повторён на интерактивном
  desktop, PASS, `20261018T230544Z-514804e8`.

На минимальном Linux/XCB Qt сообщает unknown color scheme и игнорирует explicit
override. Forced light/dark captures там не приняты как успешная проверка темы;
принят только фактический native System palette. Это ограничение платформенного
hint QStyleHints, сохранённое без второй системы тем. На Cocoa эффективные
light/dark подтверждаются JSON; Windows theme checks приведены в соответствующих
итерациях. Реальный desktop Linux с поддержкой override отдельно не проверен.
Полные/slow suites не запускались: изменения покрыты владельцами и прямыми
потребителями. Incremental trees и compiler caches сохранены; временная обвязка
Xvfb и транспортные архивы удалены. Dist-сессия пользователя не обновлялась.

Каталог и инвентарь актуализированы по текущим Cocoa reports, включая отдельные
страницы Memory/File list и Temporary files. Windows-only Explorer остаётся
явной платформенной записью без вымышленного macOS-снимка. Связи проверены;
инвентарь — данные конкретного capture, не контракт внутренних имён виджетов.

## Итоговая сверка плана

Сверка выполнена по всем разделам `ui-redesign-plan.md` и
`ui-shortcuts-design.md`, включая последнее указание сверяться с концептом
и закрытое начальное состояние параметров сжатия. Проверены текущие владельцы,
поведенческие сценарии и галерея, а не только заголовки групп.

| Требование | Реализация и свидетельства |
| --- | --- |
| Один свободный вариант, общие интервалы, native controls, никаких глобальных стилей Settings | Общая компоновка коротких форм/отчётов, собственные layouts остальных семейств; итерации 1, 7–11, 14–16. Обычная/компактная геометрия, переводы и native captures; прежнего переключателя плотности нет |
| Раскрываемые группы, стабильный шеврон, сохранение значений | Общий QtDisclosure; итерации 2, 18, 20, 23. Четыре группы Add закрыты по умолчанию, поля показаны сразу без длинных сводок; черновики и effective options проверены |
| Добавление, распаковка, пароль, дополнительные параметры и metadata | Итерации 2, 20, 23: исходные владельцы и форматные зависимости сохранены, вложенная отмена не меняет родительский draft, обязательные значения остаются в основной форме |
| Toolbar, облегчённые меню, две панели | Итерации 3–5: существующие QAction/command IDs и availability, только Add/Extract с основными подписями, группы меню без потери команд; splitter drag/keyboard/reset и сохранение долей без пересоздания моделей |
| Контурные значки, палитра и плотность | Итерация 22: локальные лицензированные Lucide paths, реальный QAction rendering normal/disabled и 1×/2×, native file/application icons сохранены |
| Современные shortcuts и совместимость | Итерация 6 и уточнения: единая Qt-граница назначений, NativeText, panel routing, reserved N/Z, clipboard/text editing, F aliases и source Shift/selection/bookmark routes. Qt native-key tests и отсутствие двойного выполнения; physical EN/RU отложены |
| Короткие формы, Browse, Temp, ошибки и подтверждения | Итерации 1, 7, 8, 14–16: source validation/handlers, нормальная высота по содержимому, доступные footer и длинный перевод; native pickers/alerts сохраняют свои метрики |
| Прогресс, логи, свойства, хеши и оба benchmark | Итерации 8–10: source управление, worker lifetime, pause/cancel/close, полный текст и копирование без потери значений. Текущие normal/compact captures и поведенческие проверки |
| Настройки по задачам и поиск форматов | Итерации 12, 21, 23: sidebar/compact navigation, шесть source owners, Windows-only Explorer скрыт на других ОС, search без dirty, bulk по всему списку, all-users matrix и undo pending association сохранены; Apply/Save/Cancel и повторное открытие проверены |
| Справка и About | Итерация 14: существующее HTML-содержимое и навигация, версии/ссылки/лицензия сохранены; новая HTML-структура из демонстрации не переносилась |
| Модальность и доступность | Итерации 17–18: реальные Cocoa sheets/owner blocking, вложенная отмена и возврат фокуса, независимое окно; Qt accessible expanded/collapsed/action states и клавиатура. VoiceOver отложен по указанию пользователя |
| Полный каталог, исходные возможности и платформенные отличия | Каталог прослежен до владельцев; текущие captures и source audits каждой итерации. Намеренные адаптации записаны в contracts/parity; screenshots не заменяют source comparison |

Реализация плана и приёмка в разрешённом пользователем Qt-scope завершены.
По прямому указанию «Пока проверять только внутри Qt» не проводились VoiceOver
и физические EN/RU-клавиатуры; они не объявлены пройденными. Нативный Windows
7zFM pixel reference и сертификация полного соответствия Apple HIG также не
заявляются. Эти границы не означают удаление функций или скрытые незавершённые
команды. Все реализации оформлены отдельными итерациями/коммитами; публикация,
push и замена пользовательской сборки в этот goal не входят.

## Итерация 24: размер значков главной панели

Дата: 19 октября 2026. После замечания пользователя измерены все семь SVG
в rendered HTML main: 16×16 CSS pixels. Qt ошибочно сохранял исходный bitmap
canvas 24×24/48×36 после замены графики на векторную в итерации 22.
Это пропущенное расхождение итоговой сверки, исправленное этой итерацией.

Повторно просмотрены полные CreateToolbar/AddButton/ReloadToolbars и callers
SaveToolbarChanges исходного `CPP/7zip/UI/FileManager/App.cpp`, текущие
ReadToolbar/SaveToolbar/SwitchLargeButtons и Qt updateToolbar/createMenusAndToolbar.
Источник и upstream commit — как в baseline этого аудита. Исходные команды,
mask persistence, группы, подписи и ветви выбора размера не меняются;
адаптация ограничена размерами в Qt presentation: обычные контурные символы
16×16, увеличенные 24×24. Ручного Retina-множителя нет, QIcon сохраняет DPR.
Новые функциональные алгоритмы и зависимости не добавлены, исходные ветви
не исключены; после изменения проверена связь с прежним toolbar preference.

Существующая проверка больше не закрепляет точный размер старого bitmap.
Она проверяет увеличение, возврат к обычному размеру и полную доступность
всех команд. Новый отдельный тест для двух констант не добавлялся.
Приложение и main_shell собраны на macOS, Linux и Windows; focused main_shell
PASS на трёх ОС, runner receipt `20261019T015735Z-30f7e3b3`, Windows interactive.
Native Cocoa main EN/RU normal/compact light/dark captures обновлены;
просмотрены обычная и увеличенная панели. Инвентарь main обновлён по текущему
снимку. Остальные формы/иконки и пользовательская dist-сессия не изменены.
Full/slow не запускались: изменение ограничено размером toolbar и его прямым
потребителем. Incremental builds и compiler caches сохранены.

## Итерация 25: компоновка верхней области главного окна

Дата: 19 октября 2026. Reference — присланное пользователем сравнение HTML/Qt
и текущий rendered main-концепт. Предыдущая сверка размеров значков не проверила
всю верхнюю область: toolbar оставался тесным, подписи — в нативном малом жирном
стиле, строка пути — без самостоятельных отступов. Статус плана уточнён:
реализация внедрена, визуальные уточнения по отзывам продолжаются.

В браузере измерены реальные toolbar/button/address boxes, шрифт и интервалы.
В исходном `CPP/7zip/UI/FileManager/App.cpp` повторно просмотрены
CreateToolbar/AddButton/ReloadToolbars/SaveToolbarChanges; для адреса —
PanelFolderChange LoadFullPathAndShow и combo notification/end-edit consumers,
текущие QtPanelAddressCombo, createFilePanel и updateToolbar. Источник/baseline —
как в предыдущих итерациях. Исходные навигация, история, команда перехода к
родителю, её доступность и toolbar preferences сохранены; адаптация касается
только Qt-представления. Ветви исходных функциональных тел не исключены.

Узкий QtMainWindowChrome отвечает за размер hit areas, интервалы toolbar,
обычный системный шрифт подписей и сплошной разделитель. Нативные hover/focus
и фон сохраняются; только label рисуется явно, поскольку Cocoa toolbar style
подменяет его шрифт. Цвета читаются из текущей палитры, без глобального QSS и
второй темы. В native Cocoa после polish проверен фактический шрифт: системный,
13pt, weight400. Диагностический вывод затем удалён.

Навигация получает собственные отступы и растущую с системным шрифтом высоту
адреса. Cocoa widget-rect adaptation устраняет вычитание внутренних native
полей из этой высоты. Кнопка родителя — нативная QPushButton, связанная с прежним
QAction и отражающая его состояние/tooltip; адресный backend и combo popup не
переписывались. Native window title, platform palette, реальные пути и файловые
иконки намеренно остаются настоящими данными приложения, а не примерами HTML.

Имеющиеся main-shell и language проверки принимают кнопку по её поведению,
а не конкретному tool-button классу; удалена привязка к числу прямых детей.
Проверяются видимость, иконка и локализация. Новые тесты констант отступов не
добавлялись. Текущие Cocoa EN/RU normal/compact light/dark captures обновлены,
просмотрены тёмная панель и светлая двухпанельная компоновка. В текущем Cocoa
JSON toolbar44 и navigation58 logical pixels сопоставлены с rendered HTML45/58;
это датированные измерения, не фиксированный размер всех платформ и шрифтов.

macOS: восемь focused suites PASS — main_shell, language_ui, panel_focus,
window_placement, address_navigation, address_combo, panel_address_keys,
panel_shortcut; приложение собрано. Linux8/8 и Windows9/9 PASS,
`20261019T021440Z-05ad6d2b`; Windows дополнительно проверил
native_address_navigation на interactive/elevated desktop. Первый Windows
прогон `20261019T021133Z-abe3f8f3` прошёл восемь наборов, но UNC fixture
получил NetShareAdd error5 без административного токена; штатный elevated
повтор прошёл, код операций/тестов ради разрешений не менялся.
После локальной правки цвета разделителя main_shell повторён на трёх ОС, PASS;
remote receipt `20261019T021630Z-27665b78`. Инвентарь главного окна обновлён
по текущему capture. Остальные виды галереи не объявляются заново проверенными
этой локальной итерацией. Full/slow, VoiceOver и физический ввод не запускались.
Сами приложения дополнительно собраны и проверены запуском на Linux/Windows,
PASS `20261019T021718Z-af317994`. Пользовательская dist-сессия не заменялась;
build trees/compiler caches сохранены, временная диагностика шрифта удалена.

## Поиск ошибок пользовательских сценариев — 2026-10-30

В продолжении [аудита сценариев](../user-workflow-audit.md) исправлены
доступность Two Panels, проверка отключённого пароля Add и результат
адресной навигации. Сопоставлены исходные `MyLoadMenu.cpp` (View checks и
dispatch), `App.cpp::SwitchOnOffOnePanel`, `CompressDialog.cpp::OnOK` и
включение encryption controls, `PanelFolderChange.cpp::OnNotifyComboBoxEnter`
и его callers. Оригиналы взяты из repository-owned baseline; ревизии и hashes
полных inventory units остаются в действующем manifest.
Дополнительно сопоставлены полные `CMyComboBoxEdit::OnMessage` из `Panel.cpp`
и `CPanel::OnKeyDown` из `PanelKey.cpp`: F9 сохраняет исходное назначение,
но Qt callers используют тот же command dispatch для асинхронных guards.

Исходный command dispatch теперь достигает существующего Qt-переключения
панелей через общий registry; асинхронные Qt scan/operation guards определяют
доступность команды. Исходная смена числа панелей и layout остаются у CApp.
Для локальной скрываемой секции защиты Add проверяются только эффективные
параметры, без удаления черновиков. Это намеренная адаптация безусловной
проверки пароля исходного диалога. Qt address adapter передаёт HRESULT
существующего владельца открытия, сохраняя исходное условие перевода фокуса
только при успешной навигации.
Для асинхронного списка исходный posted-focus запрос передаётся существующему
Qt-владельцу отложенного фокуса. Доставка ожидает завершения публикации и
отменяется новым вводом; прежние Escape/popup и guard более нового фокуса
сохранены. Повторно сопоставлены полные исходные OnNotifyComboBoxEnter/EndEdit,
их popup callers и kSetFocusToListView handler; новый алгоритм навигации не добавлен.

Новые заимствованные реализации и functional omissions не добавлены; pinned
original bytes не менялись. Текущие поведенческие свидетельства, найденные
ошибки и пробелы приёмки принадлежат отчёту сценариев.

## Единые правила команд и состояние продолжения Copy — 2026-10-30

Перед изменением сопоставлены полные исходные App.cpp::OnCopy,
SwitchOnOffOnePanel, их callers и текущие Qt completion/publication consumers
с repository-owned baseline. Исходные правила выбора панели, Copy/Move,
сохранения выделения, обновления источника/назначения, ошибок и отмены остаются
у прежних владельцев; новых импортов и изменений original hashes нет.

Доступность Two Panels и Exit теперь использует те же предикаты, что и
исполнение команды. Qt-переключение повторно обращается к общей политике;
состояние не кэшируется в QAction и не расходится с guards обработчика.
Быстрая проверка этих команд не требует запросов свойств файловой панели.

Проверки policy/F9/scan: command_policy_runtime, view_menu, panel_address_keys
и large_list_window PASS на Linux, macOS и native Windows. Linux batch
`20261030T071716Z-4de24868` имеет matching-token verify-result PASS при ошибке
SSH wrapper; это не успешный orchestration status. Windows batch
`20261030T080653Z-2c9dd14b` подтвердил эти четыре consumer-набора; отдельный
таймаут copy_publication_order рассматривается в проверке продолжения Copy.

Qt-метаданные продолжения Copy/Move теперь принадлежат одному optional-контексту.
Создание одновременно задаёт исходную панель и правила обновления; исходный
индекс неизменяем. После завершения удаляется весь контекст, а временный запрет
повторного завершения ограничен RAII-блоком. Контекст освобождается до активации
панели и исходного CompleteCopy: эти вызовы могут повторно обновить команды.
Сохранены завершение после синхронного возврата и асинхронного сигнала,
источник перед назначением, runtime-выбор peer и игнорирование reload HRESULT
исходным OnCopy. Archive/filesystem алгоритмы и selection continuation не менялись.

Объединённая проверка этих двух изменений: Linux — девять наборов PASS
(50,77 с тестов, matching-token receipt выше), macOS — те же девять PASS
(82,57 с). Windows main batch — 8/9 PASS (522,47 с), включая полный
ui_copy_routing: 85 сценариев / 87 Qt checks, 229,64 с внутри QtTest.
copy_publication_order превысил общий лимит 45 с без опубликованной ошибки
отдельной проверки. Полный native повтор `20261030T081915Z-4edc8e4d`:
12 сценариев success/error/cancel для Copy/Move обеих панелей, 14 Qt checks PASS,
28,64 с внутри QtTest, 30,59 с с процессом. Это объединённые свидетельства
focused запусков, а не успешный единый Windows main batch.

Для native Windows общий лимит copy_publication_order получил резерв по
измеренному полному времени процесса; ожидания отдельных состояний и все
12 сценариев сохранены. После настройки штатный CTest-повтор
`20261030T082410Z-9a0ead63` PASS за 27,60 с. Linux/macOS лимиты не менялись.
Исходный таймаут сохранён в свидетельствах; успешный повтор сам по себе не
доказывает устранение всех причин нестабильных задержек стенда.

## Удержание запроса Move из архива — 2026-10-30

Сопоставлены исходные App.cpp::OnCopy и PanelOperations.cpp::DeleteItems,
текущая адаптация извлечения/удаления и все start/finish/отказ consumers.
Исходник удерживает выбранную панель и индексы в течение операции; Qt сохраняет
разделение извлечения и удаления по завершению асинхронной стадии.
Прежние пара stage/indices заменены одним optional-запросом с обязательными
неизменяемыми исходной панелью и индексами. Отсутствие запроса заменяет None;
отказ и завершение очищают его целиком. Перед закрытием progress берётся
устойчивый снимок запроса, а удаление использует сохранённую панель вместо
текущего фокуса. Обычный refresh назначения остаётся отдельным механизмом
и не означает наличия Move. Native DeleteItems, проверка успешного извлечения,
сообщения, отмена и archive update алгоритмы сохранены; новых импортов нет.

Пять существующих consumer-наборов PASS на всех трёх платформах:
ui_copy_routing, ui_archive_states, ui_operations, ui_copy_same,
copy_publication_order. Linux: `20261030T084106Z-512eeeff`; macOS: 5/5,
73,50 с; native Windows: `20261030T084653Z-5b83a33a`, 5/5 в одном batch.
Проверены полные 85 Copy/Move-сценариев, обе панели, отмена на конфликте
с сохранением исходной записи и последующий успешный Move в том же окне.
Тесты приватной структуры и новые таймауты не добавлялись; full/slow gate
и pixel parity не заявляются.

## Владение прокруткой при обновлении панели — 2026-10-30

Сопоставлены исходные PanelItems.cpp::SaveSelectedState, RefreshListCtrl и
EnsureVisible, локальное восстановление выделения, публикация scanner и
сохранение/восстановление двух Qt-панелей. Исходный алгоритм выбора и фокуса
сохранён; удержание верхней видимой строки — адаптация Qt-представления.
Новых заимствований, изменений baseline или архивных алгоритмов нет.

Прежние отдельные path/scroll с sentinel неявно переживали showDirectory.
При навигации во время незавершённого Refresh они применялись к другой папке.
Теперь optional-снимок объединяет папку и позицию: Refresh передаёт его явно,
новая навигация и ошибка сканирования сбрасывают целиком, восстановление
проверяет владельца и забирает снимок до вызовов виджета. Переключение панелей
сохраняет один контекст вместо пары независимо обновляемых полей.

Поведенческая регрессия прерывает обновление папки переходом в другую после
первой порции строк. До исправления macOS показал scroll 466 вместо 0.
Существующий panel_auto_refresh проверяет сохранение прокрутки и выделения
при обычном обновлении. Linux: `20261030T093101Z-879a1adc`, 6/6 PASS
(46,36 с CTest): filesystem_reload, panel_auto_refresh, navigation_reload,
large_list_window, modal_refresh_window и panel_refresh_completion.
macOS: те же 6/6 PASS (30,03 с CTest). Полный gate, slow suites и
визуальная паритетность этой проверкой не подтверждаются.

Windows main batch `20261030T093432Z-48bf0d20`: 5/6 PASS, включая новую
регрессию; navigation_reload остановлен общим лимитом 60 с без опубликованной
ошибки отдельного сценария. Подробный native повтор
`20261030T094409Z-a2176ad3` прошёл все 20 сценариев обеих панелей и EN/RU:
22 Qt checks PASS, 51,11 с полного процесса. Windows-лимит этого набора
получил резерв до 120 с по измеренному времени; отдельные ожидания и состав
сценариев сохранены. Исходный таймаут остаётся частью свидетельств;
успешный повтор не доказывает устранение всех причин задержек стенда.
После настройки штатный CTest-повтор Windows
`20261030T094714Z-61f9a53f` PASS за 45,96 с. Итоговая конфигурация также
прошла focused navigation_reload на Linux (`20261030T094607Z-93a834fc`)
и macOS. Это совокупность focused запусков, а не единый успешный Windows
batch из шести наборов. Временный диагностический скрипт и исходный snapshot
удалены; логи исходных ошибок и итоговые receipts сохранены.

## Завершение backend без устаревших режимов — 2026-10-30

Проверены все writers/callers состояния завершения, действующие владельцы
AddToArchive, TestArchives, CalculateCrc2 и их исходные тела Panel.cpp/PanelCrc.cpp
из записанного baseline. Создание/изменение обслуживаются исходными командами
и typed operation boundary; флаги Qt creating/updating не имели ни одного
включающего присваивания. Удалены оба флага, недостижимые ветви повторного
открытия/обновления и всегда ложный результат вспомогательного завершения.
Два действующих перехода остались непосредственно в backendFinished;
порядок закрытия progress, восстановления режима, обновления назначения,
продолжения Copy и публикации результата сохранён.

Сохранённый режим синхронного Hash теперь локален вызову, а синхронный Test
не пишет неиспользуемый предыдущий режим. Асинхронные извлечение/Test по-прежнему
удерживают свой режим до завершения. Исходные архивные алгоритмы, guards,
ошибки, отмена и выбор источника не менялись; новых импортов нет.
Новые тесты приватных флагов не добавлены: проверяются существующие
ui_archive_states, ui_operations, ui_copy_same и native_hash_ui.

Linux: `20261030T095907Z-95cd6113`, 4/4 PASS, 26,79 с CTest.
macOS: те же 4/4 PASS, 30,38 с CTest; 72 Qt checks без skips.
Windows: `20261030T100158Z-52ca74e1`, 4/4 PASS одним batch, 217,36 с CTest.
Существующие сценарии проверили ошибки и отмену, восстановление команд,
извлечение, создание/изменение реальных архивов и источник hash-файлов.
Тесты и их лимиты не менялись. Исходный snapshot удалён после сравнения;
incremental outputs, compiler caches и итоговые receipts сохранены.
Сценарий Hash устройства не запускался: выбранные проверки не создают и
не подключают виртуальные диски. Изменение локального lifetime Hash не
означает новой приёмки этого device workflow. Full/slow gate и визуальная
паритетность не заявляются.
