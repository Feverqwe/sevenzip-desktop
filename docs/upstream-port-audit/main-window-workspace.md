# Главное окно и workspace

Изменение по согласованному дизайну, 2026-11-05. Вкладки, независимые окна,
сессия, именованное избранное и breadcrumb chrome — project-owned Qt integration.
Они не заимствуют алгоритмы архивов или файловых систем.

## Рамочный выбор в общей файловой панели — 2026-11-06

Диагностический жест из пустой области действительно оставлял таблицу без
выбора. Значки использовали встроенную рамку Qt, но Escape не имел общего
rollback-контракта. Новый view-owned controller обслуживает оба вида и общий
selection model: обычная рамка заменяет выбор, Ctrl переключает относительно
исходного набора, Shift добавляет, Escape восстанавливает выбор и current.
Отпускание сохраняет результат; край viewport прокручивает список без
голодания таймера при частых move. Имя/иконка сохраняют обычный Qt file drag,
пустой отступ внутри ячейки доступен рамке. Жест активирует свою панель,
приостанавливает hover и не сохраняет callback прежнего окна после transfer.

Перед изменением сопоставлены existing FileTable/Icon, delegate, MainWindow
filters/callers, shared QtPanelList и source selection/operated consumers.
Новые файлы относятся к локальной Qt-интеграции, используют protected view
selection API и зарегистрированы в существующем inventory; внешнего кода не
импортировано, исходные baseline hashes не менялись. Native marks и алгоритмы
архивных/файловых операций остаются у прежних владельцев.

Наблюдаемые проверки покрывают Ctrl/Shift, повторное пересечение рамки,
Escape с поглощением release, конечную координату release, частые move у края,
обычный item drag, неактивную панель и реальный Copy URL consumer. Focused Linux:
gesture, table и file-states PASS; macOS offscreen: gesture и file-states PASS.
`selection-gesture` и `file-states` имеют Cocoa PNG/widget JSON от 6 ноября
в обеих native темах в review-галерее. Представительные кадры просмотрены:
рамка ограничена viewport, header остаётся вне неё, current и selected
различимы; Cut не подменяет выбор. Метаданные сохраняют фактический QPA,
палитру, время и source-state каждого запуска. Это свидетельство выбранных
native состояний, а не полный платформенный прогон всех сочетаний клавиш.
Повторная сверка с живым каталогом подтверждает роли FileRow/IconItem/Places
и различие current/selected; точные CSS-пиксели не задают native геометрию.

## Повторная проверка после пользовательских дефектов меню

2026-11-05, после `da86ecee`: пользовательские снимки выявили шесть одинаковых
подменю контрольных сумм и неверные подписи Archive. Предыдущая проверка
композиции главного окна не установила корректность всех вложенных меню;
новая сквозная проверка остаётся отдельной активной задачей.
Меню/переводы и соответствующие runtime-свидетельства описаны в
[аудите меню](menu-language.md#workspace-menu-integration-regressions--2026-11-05).

Повторная проверка вкладок выявила использование позиции строки вместо выбранной
идентичности в командах меню, преждевременное завершение переноса восстановленной
вкладки и потерю завершения восстановления при закрытии окна. Команды разрешают
вкладку заново; отложенный перенос сохраняет предыдущую активную вкладку, если
пользователь не сделал новый выбор. Новое окно убирает стартовую вкладку после
фактического успеха и сохраняет папку, которую пользователь успел открыть.
При отказе нетронутое служебное окно закрывается с учётом исходного состава
панелей, включая сохранённый двухпанельный режим. Отмена и уничтожение владельца
завершают ожидающий callback однократно; сохранение соседнего окна возобновляется.
Это изменения project-owned Qt workspace; native reader/provider алгоритмы
и их provenance не изменены.

Focused Linux проверки меню вкладок, вкладок, навигации, архивов и сессии — PASS;
дополнительные detach-сценарии сохранённого режима панелей — PASS. macOS offscreen
вкладки/сессия — PASS. Native Cocoa QtTest: меню/перенос вкладок 17 PASS,
сессия 7 PASS, автообновление 15 PASS. Проверка реального файла в перенесённой
папке дополняет проверку descriptor и адреса. Общая сквозная проверка активна.
Актуальным источником дизайна теперь служит каталог в `master` (`ed197ad2`);
новые lifecycle-контракты каталога сами по себе не доказывают native-реализацию.

Native Cocoa дополнительно выявил перепроекцию при cold Activate: фокус peer
между созданием Home-контролов и открытием сохранённого места перенаправлял
навигацию в другую сторону. Адрес и native provider расходились. На фазу
построения/привязки удерживается Qt projection depth и существующий native
notification guard соседнего source; shared owner удерживает его lifetime.
Та же граница удерживается при создании receiver и публикации перенесённого
контекста; вызывающий completion получает результат после освобождения guards.
Точный failing native сценарий после исправления проходит, затем повторены
полные перечисленные Cocoa наборы. Native folder/navigation алгоритмы не заменены.

## Предыдущая визуальная сверка композиции

2026-11-05: главное окно сверено с rendered живым макетом и каталогом v38.
Проверены порядок вкладки/адрес/действия, начало sidebar у списка, равные файловые
области двух панелей, normal/compact EN/RU, обе Cocoa темы, длинные подписи и
доступность меню. Отдельные актуальные review producers сохраняют адрес,
selected/current/Cut, крупные значки и Places. Полный набор доступен в локальной
галерее `main-window-review` соответствующей ветки build artifacts.
Повторное read-only ревью композиции не выявило блокера к пользовательской
визуальной приёмке; сама пользовательская приёмка остаётся отдельным шагом.

Финальное уточнение вкладок использует общий контур Folder/Archive по native
`IsArcFolder`, сохраняя presentation snapshot вместе с вкладкой. Папка с именем
`.zip` остаётся папкой; проверка открывает настоящий вложенный архив и сравнивает
видимый символ. Действующие reader/binding/parent algorithms не изменены.
Геометрия существующего unavailable session состояния дополнительно проверена
EN/RU при 980×620/640×400. Новый просмотр выявил устаревшие счётчики fallback
источника: для недоступного места они очищаются, а после успешного открытия
статистика снова приходит от CPanel. Ошибка, Retry и Parent остаются видимыми.

Linux x86_64: финальные tab/archive/main-shell suites — PASS; дополнительный
session geometry suite — PASS; после исправления счётчиков session/main-shell —
PASS. Предшествующие focused свидетельства операций, выбора, переноса,
clipboard, rollback, сессии и именованного избранного применимы к неизменённым
владельцам. Это focused acceptance без полного/slow gate. Свежие Cocoa PNG/JSON
подтверждают главное окно и вложенный архив; native поведение остальных ОС и
пользовательская оценка не выводятся из этих снимков. Новых заимствований нет;
original baseline hashes сохраняются, локальный inventory проверен.
macOS arm64: финальные navigation/tabs/archive/session/main-shell и startup smoke —
6 suites PASS. Bundle обновлён. Cocoa review недоступного места обновлён после
исправления статистики; сообщения и оба действия помещаются в компактной форме,
предыдущая статистика не показывается. Физические IME/preedit и drag gestures
этим финальным проходом отдельно не проверялись.

## Native scope

PlacesSidebar, дополнительная сверка 2026-11-05: перечисление native мест
перенесено из виджета в общий value model без удержания COM-интерфейсов.
Повторно проверены полные `RootFolder.cpp/.h`, `NetFolder.cpp/.h`, `FSDrives`
и panel path/capture callers. Узкий локальный `CNetFolder::IsRootLocation`
читает существующее состояние корня; native перечисление, binding, parent
и сетевые операции не изменены. Типизированная идентичность native корня
сохраняет подсветку после смены языка, даже когда открытый provider сохраняет
первоначальное имя пути. Новые Qt model/projection units — project-owned
integration; оригинальные baseline units и хеши не меняются.

Повторная сверка маршрута Computer: POSIX `CRootFolder` связывает одноимённый
элемент с физическим `/`, а команда меню открывает `CFSDrives`. Локальная модель
Places теперь проецирует именно bound `CFSDrives` как Computer, сохраняя его
native путь; отдельный физический корень доступен через том. Native binding,
перечисление и Windows device namespace не изменены. EN/RU регрессия проверяет
переход реальным щелчком из файловой папки и командой меню: одинаковый provider,
подсветку и запрет создания папки в namespace. Linux: Places/root/navigation/
main shell/file policy — PASS; macOS offscreen: первые четыре набора — PASS.
Native Cocoa: EN/RU переходы Computer — PASS; компактный Places и независимый
фокус — PASS в обеих темах. Обновлённые снимки просмотрены; Windows runtime
этой POSIX-адаптацией не проверялся.

Повторная проверка сессии выявила потерю типа Computer после EN→RU: сохранённый
локализованный путь попадал в обычный Binder. Сессия и Duplicate теперь сохраняют
известную native root identity. Общий Qt-адаптер разрешает её в актуальный native
Path из модели Places (Locations — пустой native root), использует прежний Binder
и проверяет итоговый provider. Descriptor обновляется фактическим prefix только
после успешного bind/render; ошибка binding сохраняет приоритет над render.
Отсутствующая/неизвестная identity оставляет прежний path route. Native bindings,
алгоритмы перечисления и baseline hashes не изменены. Подписи всех root-вкладок
обновляются при смене языка; пустой Locations при Duplicate не становится Home.
Linux: session/tab menu/Places/language — PASS, включая active/cold Computer,
Locations и POSIX Network после EN→RU и реальные меню Duplicate. Ранее прошли
прямые потребители tabs/archive/address/rollback; Windows runtime не проверялся.
Native Cocoa обнаружил reentrant callback при уничтожении скрытых списков внутри
RestorePanel: обновление команд читало заменяемый набор вкладок. Замена теперь
защищена workspace transition и native notify guard активного слота; активация
начинается после их восстановления. Повторный Linux session/tab menu — PASS;
Cocoa session — 13 QtTest PASS, tab menu — 19 QtTest PASS, включая Duplicate.
Сценарии native focus выполнялись после разблокировки macOS; неуспешный прогон
меню на заблокированном desktop не считается свидетельством приёмки.

Текущее место отделено от клавиатурного курсора и локально для каждого sidebar;
общая боковая панель использует активную сторону окна. Действия меню повторно
разрешают идентичность закладки после перестановки/удаления, а редактор имени
открывается после закрытия меню. Linux x86_64: `workspace_places`,
`workspace_navigation`, `workspace_session` — PASS, включая MIME-перестановку
через proxy, переименование и смену языка native корня. Windows runtime новым
проходом не проверялся; Win/POSIX ветви дополнительно просмотрены по исходникам.
После уточнения общих контурных значков Linux Places/navigation/main shell —
PASS. macOS arm64: Places/navigation/session offscreen — PASS; native Cocoa
Places — 9 сценариев PASS, включая меню/редактор и независимый фокус.
Светлая/тёмная Cocoa геометрия и компактные EN/RU снимки обновлены и просмотрены
против живого каталога v38. Подсветка места, контур курсора, сокращение длинной
подписи и прокрутка сохраняются; sidebar использует общие контурные symbols.

Исходный репозиторий 7-Zip: <https://github.com/ip7z/7zip.git>, полная ревизия
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Полные исходные inventory units,
хеши и текущие локальные владельцы остаются в [основном baseline](../upstream-baseline.json).
До изменения и при итоговой сверке проверяются:

- `CPP/7zip/UI/FileManager/App.h`, `App.cpp`: два активных panel slots,
  CreateOnePanel, смена числа панелей и завершение окна. Qt slots удерживают
  стабильные CPanel; исходные объекты не перемещаются и не копируются.
- `Panel.h`, `Panel.cpp`, `PanelMenu.cpp`, `PanelFolderChange.cpp`: lifetime,
  фокус, binding/parent chain, EditCut/EditCopy/EditPaste и native callers.
  Прежняя upstream Copy копирует текст выбранных имён, а Cut/Paste не содержат
  активной файловой операции. В главном окне подключён явный Qt clipboard hook;
  native-only consumers сохраняют прежний fallback. Путь к источнику выводится
  native owner, не восстанавливается из текста ячейки.
- `PanelItemOpen.cpp`: запуск внешнего процесса и публикация изменения архива.
  Сохраняются исходные выбор, пароль, read-only/update checks, ожидание процесса
  и запрос пользователя. Дополнительный счётчик живых editor sessions ограничивает
  перенос между окнами; завершение inactive источника не перезаписывает peer UI.
- `resource.rc`, `MyLoadMenu.cpp`: command IDs/dispatch сохранены. Группировка
  меню, убранная главная Help UI и файловая toolbar — запрошенная адаптация.
- `PanelColumns.cpp`, `PanelItems.cpp`, `RegistryUtils.cpp`: сверены полные
  native каталог, загрузка/сохранение раскладки, sort/extension и FullRow
  ветви с callers. Начальные видимость и ширина файловых колонок применяются
  Qt callback после построения полного каталога и до существующей обработки
  сохранённой раскладки. Callback не меняет идентичности, порядок и raw свойства.
  Сохранённая раскладка и явно сохранённый FullRow имеют приоритет; обычное
  выделение всей строки стало начальным выбором. Native сортировка по доступному
  свойству сохраняется и при скрытой колонке; исчезнувшее из native binding
  свойство получает прежний fallback. Extension и исходный порядок остаются
  допустимыми synthetic sort keys. Qt-подпись
  типа использует только filename metadata и не заменяет native сортировку
  либо reader. Исходный формат native сохранения не расширен синтетическими
  колонками; их presentation preferences имеют отдельного Qt владельца.

Навигация, вложенные archive parents, CopyTo, валидация extraction paths,
ограниченное чтение, пароли, update/reopen и отмена остаются у существующих
native/backend владельцев. Новый код не возобновляет операции из сессии.
Полное удаление help subsystem и переработка самостоятельных Add/Extract/Copy
диалогов сюда не входят. Неизменённые source language и лицензии 7-Zip сохранены.

## LocationBar: общий native-путь — 2026-11-05

Повторно сверены полные PanelFolderChange.cpp/Panel.h и их address callers из
локального source archive указанного baseline, включая return-result, root/UNC,
device, namespace, `_parentFolders` и выбор из списка. Иерархия из существующего
OnComboBoxCommand выделена в AddressAncestors в том же владельце. Dropdown
сохраняет исходный состав, отступы и namespace entries; крошки получают готовые
цели и типы от этой же логики, включая текущий namespace и вложенные контейнеры.
Нет нового парсера filesystem/archive paths, внешнего импорта или новых hashes.

Qt возвращает исходный success-result Enter вместо безусловного закрытия поля.
При отказе восстанавливает отправленный текст и курсор; редактирование остаётся
доступным после native modal. Успех передаёт фокус прежним queued-маршрутом;
новый вход в редактор отменяет старое намерение фокуса. Workspace публикует
успешно привязанный путь отдельно от черновика. Native deepest-existing-parent
fallback остаётся успехом. Кэш сегментов учитывает смену binding и не удерживает
дополнительные native folder interfaces за lifetime их библиотек.

Для физического открытия другого архива повторно сверены BindToPath,
OpenAsArc, CloseOneLevel, CloseOpenFolders и OpenParentArchiveFolder вместе с
CFolderLink/CTempFileInfo и native/Qt loading callers. Новый project-owned
QtPanelLocationTransaction удерживает прежние интерфейсы, parent links, DLL и
load caches до успешной публикации кандидата. Отказ возвращает те же объекты,
пароль и OwnedTempLease; не переоткрывает прежний архив. Commit выполняет обычное
native-закрытие на исходном CPanel, включая modified-nested handling, временно
отсоединяя только Qt-адаптер уже опубликованного списка. Перемещение внутри
текущего контейнера не оборачивается новой транзакцией. QtFolderProjection
сообщает terminal result ожидающему physical-open владельцу; отмена освобождает
candidate provider до закрытия DLL. Это локальная интеграция, без переноса новых
внешних алгоритмов. Список предков и native archive lifecycle остаются единственными
реализациями своих операций.

У editable QComboBox отключено implicit editingFinished label matching:
выбор caption при потере фокуса не должен повторно активировать иерархию после
явного popup/Return-перехода. Остальные подписчики editingFinished подключаются
после этой настройки. Проверяется наблюдаемая навигация, не private Qt symbols.

Регрессии проверяют отказ/отмену и фоновое обновление черновика, успешный переход
и новый ввод во время Scan, реальный переход по крошке к каталогу с `.zip` в имени
и обе границы настоящего вложенного архива. Старые fixtures используют явный
вход в режим адреса вместо setFocus в скрытый редактор. Focused Linux-проверки
адреса, workspace navigation/archive, сохранённых мест, главной оболочки, паролей
и terminal projection result прошли. Новые сценарии проверяют одинаковый живой
reader после отказа/отмены, повторное извлечение без нового пароля, stream и
materialized nested contexts, ошибки metadata до/во время отложенной публикации
и запись изменённого вложенного файла в родителя при принятии другого архива.
macOS offscreen-проверки адреса, workspace navigation и rollback также прошли.
Native Cocoa-проход проверил normal/compact filesystem breadcrumbs и encrypted
nested → physical archive success/refusal в light/dark. Rendered-снимки сверены
с живым каталогом: общая рамка, усечение с доступными предками, Archive glyph,
сохранённый черновик при отказе и возврат к крошкам/списку после успеха.
Последний обнаруженный разрыв — editor оставался видимым после modal progress —
исправлен завершением его представления у владельца доставленного list-focus
request; новый Edit отменяет прежний запрос. Native Cocoa подтвердил actual focus,
Linux offscreen — remembered focus после деактивации модального окна, без
принудительной активации приложения. Windows runtime этим проходом не проверялся.
Эти свидетельства относятся к LocationBar, не устанавливают полную визуальную
приёмку всех компонентов главного окна.

## FileRow/Icon: общее состояние записи — 2026-11-05

Перед подключением Cut повторно сверены существующие native GetFilePaths,
GetFsPath, Add_ItemRelPath2_To_String/GetItemRelPath2, filesystem publication и
общая Qt selection/model binding. Алгоритмы путей и операций не заменяются:
filesystem row получает операционный путь через тот же GetFilePaths, что Copy/Cut,
сохраняя directory symlink alias отдельно от canonical display directory.
Новые filesystem/Cut признаки — project-owned presentation; внешнего импорта нет.

QtFileClipboard использует общий decoder GNOME/KDE/Windows Move hints для Paste
и Cut snapshot. Смена clipboard обновляет модели существующих окон и скрытых
вкладок; новая модель получает актуальный snapshot при публикации. CutRole читает
только опубликованные row values, исключая parent/container entries. Paint не
обращается к provider или filesystem для определения Cut. Исходная selection,
rename, drag/drop и операция Move остаются у прежних владельцев.

Общий FileRowDelegate отделяет content dimming от selection/marked/drop fill и
current outline. Текущий item определяется записью, а не колонкой QModelIndex.
Крупный вид сохраняет native иконки и Qt layout text/decoration, увеличивая
изображение и оставляя две строки активного шрифта. macOS-адаптация повторяет
состав каталога: нейтральная область изображения, акцентная подпись selected и
рамка current вокруг изображения. Остальные виды используют общий row/tile
контракт. Новые rendering helpers — Qt-интеграция проекта, не копия private Qt.

Focused Linux-проход проверил shared states, table layout, native Move Cancel/success,
normal/compact/font icon geometry и marked/deleted draw. macOS offscreen нашёл
потерю красного deleted-emphasis на акцентной selected caption; для source-colored
caption сохранён читаемый оттенок через более мягкий selection background.
После исправления color/state проверки прошли. Диагностический рендер длинной
подписи нашёл третью обрезанную строку; ограничение text layout двумя строками
проверено повторно. Native Cocoa light/dark normal/compact снимки таблицы и значков
сверены с живым каталогом: Cut приглушает content, selection background/current
outline сохранены, длинная подпись имеет две строки с многоточием, есть верхний
отступ сетки. Полный путь остаётся в tooltip. Windows runtime не проверялся;
это acceptance FileRow/Icon, не всего главного окна.

Повторный аудит общих FileRow/LocationBar: native operation paths, clipboard
snapshot, selection owner, ancestor targets и transaction/focus boundary
сохранены. Linux-проверки row states, таблицы, deleted draw, address/combo,
workspace navigation и archive rollback прошли. Legacy fixtures актуализированы:
геометрия/сохранение вида проверяют поддерживаемые Table/Large Icons, а клавиши
адреса входят через видимый редактор. Проверки выбора, черновика, F9/Tab/Backtab,
геометрии шрифта и сохранности файлов остаются. Cocoa-клавиши адреса — 10 QtTest
PASS; light/dark captures таблицы, Cut, адреса, отказа/успеха перехода и видов
прошли. Просмотр компактного снимка обнаружил потерю счётчиков за `Sele…`:
selection status теперь резервирует ширину чисел и сокращает подпись слева,
сохраняя полную подсказку. Native вычисление статуса и четыре части не меняются.
Финальный Linux panel shell/table/layout/address keys — PASS; после уточнения
ширины локализованных чисел и многоточия shell/table повторно прошли. Обновлённые
Cocoa table-снимки в обеих темах прошли и просмотрены: в компактной двухпанельной
геометрии видны оба числа, current outline и независимый selection сохранены.

## Сохранённая цепочка вложенного архива: повторный аудит — 2026-11-05

Свежий CPanel не открывает nested файл как архив одним BindToPath полного пути:
CAgentFolder связывает этим маршрутом каталоги. Повторно прочитаны полные native
BindToPath, LoadFullPath, OpenItemInArchive, archive links/lifecycle и их callers,
включая исходный OpenItemInArchive из записанного source archive. Новый локальный
value descriptor хранит physical target, ParentFolderPath/RelPath каждого nested
звена, текущий folder и native ancestor names/targets из существующего address
owner. Он подключён к Capture, session, Duplicate и Retry; не
содержит паролей, COM providers, DLL или materialized temp paths.

Qt replay явно связывает настоящий physical root, затем следует подготовленным
native каталогам через BindToFolder и internal native opener для nested звеньев.
Каждый необходимый каталог проходит прежний joined directory read с progress и
Cancel до поиска ребёнка; строковый Binder не загружает дочернее дерево на GUI
thread. Нового разбора UI-путей или рекурсивного чтения всего дерева нет.
Проверяются тип provider, depth,
RelPath и итоговый полный prefix; финальная projection отдаёт terminal HRESULT.
UI display-path fast path здесь неприменим: после Cancel provider может уже быть
в подпапке. Existing void OpenItemInArchive делегирует единственному полному телу;
узкий internal-only вход возвращает существующие error/E_ABORT/refresh results.
Stream, memory/disk fallback, password, limits, extraction и external-launch
ветви сохранены. Это адаптация результата у Qt boundary; оригинальные inventory
units/hashes не меняются. Новый replay unit отмечен как local integration.

Linux: workspace archive/session/tab menu, archive item open, nested edit chain
и archive rollback — PASS. Дополнительные проверки прошли для nested parent и
конечного folder, 301 entries, извлечённых bytes, исчезнувшего nested элемента,
Cancel→Retry пароля с `d/d`, deferred metadata failure→Retry. Отказ открытия
избранного очищает старый root/archive descriptor: Retry сохраняет новый target;
Places-регрессия прошла. Windows runtime этим проходом не проверялся.
Notify guards ограничены joined read и синхронным bind; они завершаются до
переходов retained scan guards и финальной projection. Retry и текущее избранное
используют общий panel-projection scope, сохраняя адресата при Cocoa modal focus.
Проверка encrypted Cancel/Retry охватывает две панели. После разблокировки macOS
native Cocoa прошёл: archive workspace — 9 QtTest PASS, archive rollback — 12,
Retry избранного — 3; ошибок и skipped нет. Сюда входят новый reader после
замены файла, password Cancel/Retry и deferred failure/Retry. Предыдущий
остановленный прогон на заблокированном desktop не считается PASS.
macOS offscreen: archive workspace/rollback/Places — PASS после окончательной
проверки двух панелей; ранее прошли session и общий navigation scope. Эти
результаты относятся к отдельному offscreen-прогону; native evidence приведено выше.

Повторное открытие failed candidate теперь сначала закрывает его прежнюю chain
штатным native bind к пустому root, затем открывает physical target заново. Это
не позволяет Retry использовать cached reader после замены архива на диске.
POSIX-регрессия заменяет открытый outer archive после missing-nested отказа и
проверяет восстановленную цепочку/301 entries и извлечённые bytes. Linux archive
workspace/rollback и macOS offscreen — PASS; Windows replacement semantics этим сценарием не
проверяются.

## Единый маршрут Computer из общего корня — 2026-11-05

Повторно сверены полные RootFolder index/name bindings из записанного original
unit, CFSDrives и native Parent/activation callers. POSIX Computer в composite
root теперь использует тот же существующий CFSDrives, что меню и Places. Прежний
InitToRoot перенесён в явный bind `/`, сохраняя физический filesystem root.
Legacy Mounts/Volumes остаётся alias logical namespace с прежним prefix; Places
проецирует identity Computer один раз. Windows Computer/device Volumes режимы
и raw device picker не меняются. Это локальная адаптация выбора provider под
согласованный дизайн; native перечисление и original baseline hashes прежние.
Linux root navigation/command, Places/navigation и persisted location — PASS:
EN/RU три входа Computer дают тот же namespace и disabled Create, sidebar не
дублирует alias, явный `/` остаётся FS folder. Старые address fixtures входят
через видимый редактор. Native alias rows в composite root сохранены.
macOS offscreen: те же пять наборов — PASS. Новых обработчиков ввода, геометрии
или native presentation эта итерация не вводит; прежние Cocoa-свидетельства
общих команд и отображения CFSDrives не переобозначаются как новый прогон.

## История переходов вкладки — 2026-11-05

Согласованный контракт: master `work/design/specs/recent-places.md`, Back/Forward
как история вкладки и отдельный Parent. Перед изменением сверены полные original
PanelFolderChange/PanelItemOpen bodies, CFolderLink, RefreshListCtrl, CFolderHistory
и Qt callers; повторная проверка original PanelFolderChange/App units из
записанного source archive дала прежние SHA256. Native Bind/Open/Parent/Close,
архивные алгоритмы и политика изменения вложенных архивов не заменены.
Новые requests и per-tab value snapshots — project-owned application integration.
Global recent ordering, equality и limit остаются у CFolderHistory.

Общий recorder получает результат адреса, сегментов, native activation, Parent,
root, Places и Retry. Фоновое перечисление подтверждает terminal result; отмена
не создаёт промежуточный визит. Back/Forward возвращают native target, выделение,
current и viewport. Новая успешная ветка отбрасывает forward tail, повтор текущего
места не добавляет соседний дубль. Transfer сохраняет live stack; Duplicate и
session reopen начинают отдельный stack без сохранения passwords/providers.
Существующий native успех с fallback к доступному предку сохранён; history Travel
должен достичь именно записанного target. Новый диалог недавних мест этим разделом
не объявляется реализованным.

Переход между разными источниками сохраняет прежний native context до результата.
Возврат внутри той же archive chain сохраняет reader и проходит по native parent
interfaces и подготовленным directory targets с joined directory read. Нет нового
парсера archive paths или decoder. Actual reload сообщает HRESULT после проверки
раннего пропуска; suppressed S_OK не считается загрузкой и не оставляет окно
Operating. Retained scan guards освобождаются перед checkpoint notification guard.
Forward icon — отражение уже записанной Lucide arrow-left geometry; новых SVG
или внешнего кода нет, original baseline hashes сохраняются.

Focused Linux: history, Places, session, archive workspace/rollback и normal/compact
UI layout — PASS после окончательной правки Retry. Отдельно прошли root navigation,
persisted locations, общий navigation, tab menu, command publication и native Parent;
проверка Parent обновлена для видимого LocationBar и полного отпускания клавиш.
Регрессии проверяют 301-entry viewport/selection/current, forward branch и Parent,
перенос вкладки, прерывание загрузки, свежий archive reader перед same-chain Back,
единственный парольный Cancel, initial/deferred native E_FAIL/E_ABORT и отсутствие
неуспешного визита в общей истории. Missing favorite сохраняет запрошенный адрес
для Retry/New Window; успешный Retry создаёт правильный Back/Forward step.

macOS offscreen: девять затронутых наборов — PASS. Cocoa: history — 10 QtTest PASS,
native Open failure/cancel — 6 PASS, missing favorite/Retry — 3 PASS; нулевые
fail/skip. Проверены реальные EN/RU shortcuts и обе panel buttons, popup password
Cancel/Retry, notifications после rollback и terminal publication. Native fixtures
созданы в canonical temporary root с official runtime и удалены после завершения.
Это focused evidence; Windows и full/slow suites здесь не проверялись.

Review bundle пересобран. Семь затронутых Cocoa producers обновлены в light/dark:
main, View menu, table, history, LocationBar, unavailable и file views. Native
EN/RU меню и обычная/компактная навигация просмотрены: обе стрелки, подписи,
shortcut и path frame помещаются. Capture manifest сохраняет фактический dirty
pre-commit source state; прежние снимки других компонентов не переименованы
в новое свидетельство. Одноразовые native fixtures удалены; incremental outputs
и compiler caches сохранены.

## Недавние места — 2026-11-05

Контракт из master `work/design/specs/recent-places.md`. Сверены original
App/AppState, полные ListViewDialog.cpp/.h и CPanel::FoldersHistory, Delete/Enter/OK,
ресурсная оболочка списка, current Qt callers и persistent history helpers.
Original ListViewDialog inventory units из записанного source archive совпадают
с прежним baseline; новых импортов нет. Новый интерфейс MainWindow отделён от
исходного list/report dialog, который остаётся у прежних потребителей.

QtRecentPlacesStore использует CFolderHistory для порядка, IsEqualTo_NoCase и
ограничения длины. Все успешные UI visits проходят через адаптер. Immediate
Remove/Clear публикуют сохранённое состояние во все CApp до model reset, чтобы
закрытие окна не возвращало удалённые ссылки. Метаданные native root/archive
следуют фактическим строкам native списка и удаляются вместе с отсутствующими
записями; passwords, COM providers и consent не сохраняются. Directory/archive
open использует существующие Navigate/QtArchiveLocation/native engine routes.
Exact target проверяется внутри request до commit; обычный address fallback
не изменён. Пустая история остаётся доступна из меню.

Новая форма проецирует поиск, единственный выбор, SourceSummary и immediate
удаление ссылок. QtCreateDialogLayout сохраняет footer вне прокрутки списка.
Enter/двойной щелчок отправляют один запрос; ошибка сохраняет поиск и выбор.
WindowModal форма не держит native command CallScope между действиями. UUID
вкладки и stable source определяют владельца; повторное открытие ждёт завершения
её activation refresh. Close отменяет только связанный publication/request;
joined native reader остаётся присоединённым до terminal result, после чего
отменённый результат откатывается. Отсутствующий владелец не создаёт новую вкладку.

Focused Linux: Recent Places, persisted locations, Back/Forward, archive workspace
и session — PASS в относящихся к изменениям прогонах. Финальный Recent Places
повторно прошёл после уточнения stable source captions и settled layout.
macOS offscreen: те же пять наборов — PASS. Native Cocoa: Recent Places 10 QtTest
PASS и Back/Forward 10 PASS, без failed/skipped. Проверены immediate filtered
Remove/Clear и отсутствие resurrection истории при закрытии второго окна,
неизменность файлов/избранного/истории назначения и стека переходов при Clear,
поиск и клавиатура EN/RU, исчезнувшее место и Retry, UUID owner после переключения
вкладки и её удаления, cancellation собственной публикации, activation refresh
неактивного source, closed nested chain с извлечением control bytes, native
Computer после смены языка. Native close сохраняет original policy.

Реальный Linux прогон выявил reentrant resize во время rowsInserted до установки
itemWidget; пересчёт строк теперь отложен с QObject lifetime и проверкой виджета.
Return поиска обрабатывается один раз. Archive display shortcut проверяет наличие
настоящего provider перед типовым запросом — parked context не считается открытым
архивом по старой строке Qt. Регрессия encrypted Back проверяет этот guard.

Свежие light/dark Cocoa PNG/JSON показывают populated/no-results/empty/unavailable
и wrapped long-path состояния. Проверены footer, wrapping и native focus;
снимки сделаны после завершения layout. Review bundle пересобран. Canonical
одноразовые native fixtures удалены, compiler cache и incremental outputs сохранены.
Windows/full/slow acceptance здесь не объявляется. Катя проверила source boundaries
и ограниченный diff; её вывод не заменяет приведённые runtime результаты.

Отдельно при проверке обнаружена прежняя stale FastFolders snapshot: закрытие
второго окна может перезаписать числовую закладку первого. Clear сам её не стирает;
эта отличная от FolderHistory ошибка исправлена в следующей описанной итерации.

## Межоконные числовые закладки — 2026-11-05

Проверены полные original CApp::Save/AppState/CFastFolders, Set/OpenBookmark,
MyLoadBookmarks и Qt mutation/close callers. Исходный native алгоритм хранения
слотов и сохранение при закрытии остаются на своих местах. Локальный общий
адаптер перечитывает native persisted slots перед изменением и использует
существующий CPanel::SetBookmark. Затем обновляет CApp snapshots всех окон,
включая закрывающиеся, и лишь после этого публикует изменения меню. Обновление
меню читает только FastFolders и QAction, без запросов к folder provider.
Новых импортов нет, original baseline hashes не меняются.

Поведенческая проверка создаёт два окна до записи, проверяет меню и открытие
из соседнего окна, сохраняет разные слоты и изменение через native persistence
API, закрывает окна в обоих порядках и реально открывает три targets после
повторного создания окна. Former Clear+close regression теперь также проверяет
сохранность числовой закладки после закрытия второго окна.

Linux focused: fast-folders window/persisted locations/Recent Places/session —
PASS; окончательная двухфазная публикация и Clear+close повторно — PASS.
macOS offscreen: те же четыре набора — PASS. Native Cocoa: оба порядка закрытия
и повторное открытие слотов — 4 QtTest PASS; Clear+close — 3 PASS. Нет failed/skipped.
Review bundle пересобран. Layout/видимость/оформление не менялись; прежние снимки
не переобозначаются как новые. Canonical native fixtures удалены, incremental
outputs и compiler cache сохранены. Windows/full/slow checks не выполнялись.

## Предварительная проверка Quit — 2026-11-05

Сопоставлены исходный FM close pipeline, Qt close/quiesce callers, external
sessions, mount manager и контракт application-lifecycle из master. Native
CloseFileManager остаётся окончательным teardown, а не пробным запросом:
он освобождает источники после остановки workers. Нового внешнего кода нет.

Общий Quit сначала проверяет все зарегистрированные файловые окна. Modal input,
защищённый source call, работающая in-process операция, external session или
живое подключение возвращают к владельцу до закрытия любого соседнего окна.
Проверка подключения не показывает вопрос отключения и не вызывает unmount.
После разрешения закрывается сохранённый список файловых окон; отдельные
modeless окна операций/результатов не получают общий closeAllWindows.
Сессия сохраняется перед teardown. Новое файловое окно снова разрешает
обычную публикацию сессии и последующий запрос Quit.

Это исправление atomic veto, а не реализация полного нового lifecycle.
Подтверждение отказа от draft и «Продолжить операции» пока не реализованы:
in-process/external владельцы ещё связаны с файловым окном. Последующая
граница состояния и показа независимой задачи описана ниже.
Перенос владельцев, общий реестр задач и ожидание просмотра результатов
остаются открытой границей приёмки. Обычное закрытие окна сохраняет прежнюю
политику; source-port полнота не выводится из passing runtime tests.

Linux focused: workspace session, window close и native close pipeline — три
набора PASS. macOS offscreen: те же три набора PASS. Native Cocoa: два порядка
защищённого окна и формы, сохранение ввода, повторный Quit, отдельное окно
результата, новое файловое окно и восстановление сессии — 7 QtTest PASS;
защищённый source open — ещё 3 PASS. Failed/skipped нет. Live mount/external
и полный сценарий продолжения операций этим прогоном не проверены.
Review bundle пересобран; геометрия не менялась, прежние снимки не переобозначены.
Одноразовые Cocoa fixtures удалены, incremental build и compiler cache сохранены.
Windows/full/slow checks не запускались.

## Окно независимой задачи и удержание приложения — 2026-11-05

Сверены launcher/process/endpoint, GUI callers, исходные progress/thread/result
границы и benchmark. Новые локальные UI hooks не заменяют native workers,
archive inputs, отмену или результат операции. Новых импортов нет; original
baseline не меняется, общий presentation owner зарегистрирован как local.

Независимый процесс передаёт initial snapshot и изменения реального владельца
окна отдельно от transport acceptance: незапущенная форма, работа, вопрос,
результат для просмотра. Начало работы отмечается на native progress/benchmark
границе. Закрытие/скрытие виджета не считается завершением задачи; observer
ждёт настоящий выход процесса и проверяет его report. Hash result, progress
final message и benchmark result имеют явную роль просмотра. Неизвестный
вложенный modal считается вопросом, а не безопасно закрываемым draft.

Команда показа передаётся существующему процессу и выбирает актуальный modal
owner, разворачивая его и свёрнутого родителя. Ранний запрос до acceptance
сохраняется, затем передаётся child; при отсутствии окна child ждёт его показа.
Команда не создаёт вторую задачу и не открывает закрытый результат заново.
Quit при независимой задаче возвращает к её окну до закрытия файловых окон.
Application-owned observer удерживает цикл приложения до actual finish, включая
обработчики finished. Обычное закрытие последнего файлового окна не обрывает
форму, вопрос или просмотр результата независимой задачи.

Это завершённая phase/show граница, но не весь lifecycle из макета. Общий вопрос
Continue/Cancel/Show, generation+ack для отказа от draft и перенос
in-process/external владельцев остаются открытыми. Владение результатом при
сбое child без своего итогового окна добавлено следующей описанной итерацией.

Linux: protocol/observer/launcher, session, progress и оба benchmark consumers —
семь наборов PASS; после выделения lifecycle suite отдельно task-lifecycle/session
— два PASS. macOS offscreen: все восемь соответствующих наборов PASS.
Native Cocoa: real child Draft/Reviewing → minimize → Show → явное закрытие,
password cancellation — 5 QtTest PASS; Quit с двумя file peers и настоящий
application event loop после last-window close — ещё 4 PASS. Failed/skipped нет.
Review bundle пересобран. Геометрия/тексты не менялись, прежние снимки сохраняют
исходную provenance. Canonical native fixtures удалены, incremental build/cache
сохранены. Windows/full/slow checks не выполнялись. Катя проверила ограниченный
source diff; это не заменяет runtime evidence и не закрывает перечисленные gaps.

## Просмотр непроверенного результата без файлового окна — 2026-11-05

Проверены все пять production GUI launch factories, общий finished consumer,
process poll/acceptance/report/exit и native error callers. Алгоритмы запуска,
проверки результата и независимость child сохранены. Новый application registry
регистрирует observer до старта и хранит только значения request/result.
Новых внешних импортов нет, original baseline hashes не меняются.

При crash или нарушении протокола наблюдаемого child результат больше не
теряется вместе с исходным MainWindow. Registry удерживает application lifetime
до явного просмотра: новый hold приобретается до освобождения hold observer.
Обычные ошибки/предупреждения, которые уже показал 7zG, повторно не открываются.
Немедленный отказ factory до запуска остаётся в прежнем protected-caller error
path; отказ started child и потеря полного report принадлежат application review.

Немодальный просмотр использует общие DialogLayout/TextReport/ActionBar,
сохраняет полную исходную диагностику и значения путей, не разбирая их как
локализованный текст. «Копировать» берёт весь отчёт, включая невидимый текст
и независимо от выделения. Close/Escape подтверждает только выбранный UUID.
Quit показывает непроверенный результат до закрытия файловых окон.
Уничтожение presentation owner само по себе результат не подтверждает.
Ни request/result, ни разрешения/секреты не добавлены в восстановление сессии.

Реальный Linux regression обнаружил потерю фокуса при одном только
WA_ShowWithoutActivating. Автопоказ теперь откладывается до закрытия чужого
modal или завершения пользователем редактирования поля; уведомление не вызывает
editingFinished. Явный Show/Quit возвращает к результату. Существующий view не
разворачивается автоматически от позднего события. Callback привязан к application
owner и защищён QPointer; скрытие modal возобновляет показ без polling.

Linux: observer/launcher/session/task-lifecycle/task-result — пять наборов PASS;
окончательная защита modeless input — три связанных набора PASS; финальные
Escape/live-language cases — task-result PASS. macOS offscreen: те же пять
наборов PASS, финальный task-result повторно PASS. Native Cocoa: task-result
8 QtTest PASS, task-lifecycle 4 PASS; после Escape/live-language дополнения
task-result 8 PASS. Failed/skipped нет. Проверены настоящий child crash и выход
без report после last-file-window close, удержание event loop, повторный Quit,
полное копирование, исходные bytes, modal/modeless ввод и EN/RU.

Review bundle пересобран; новый producer task-result дал 12 Cocoa PNG/JSON:
light/dark, EN/RU, 600×380, 480×360 и 480×640. Реальные light EN/dark compact RU
снимки просмотрены; footer и прокрутка доступны, исходная диагностика не
переписывается при смене языка. Макет master text/messages также просмотрен
rendered. Снимки сохраняют pre-commit dirty provenance, а прежние producers
не переобозначены. Canonical fixtures и временное удержание экрана завершены,
incremental build/cache сохранены. Windows/full/slow checks не выполнялись.
Катя проверила source boundaries и последующий focus diff, без runtime claims.

Обнаруженное здесь расхождение DialogLayout с surface/window tokens макета
устранено следующей общей итерацией, описанной ниже. Функциональная приёмка
owner сама по себе не подтверждает соответствие всей компонентной системе.
Полный Continue/Cancel/Show, безопасный draft discard и transfer in-process/
external владельцев также не закрыты этой итерацией.

## Shared dialog palette

2026-11-05: существующий project-owned theme вынесен из workspace в общий
UI target без изменения цветов и политики выделения главного окна. Общий
DialogSurface использует window/surface tokens согласованного master-макета;
его подключают DialogLayout и форма создания архива с собственным scroll layout.
Изменение не переносит заново алгоритмы upstream: новые local units добавлены
в baseline inventory, оригинальные upstream units и их hashes не менялись.

Диалог получает native brushes приложения для кнопок и выделения, включая
gradient/texture, вместо наследования file-selection palette родительского окна.
Смена темы обновляет palette существующих виджетов, сохраняя draft, password
echo mode, caret, selection и focus. Нативные controls и scrollbars сохранены.
Поведенческий regression проверяет также parent propagation и отсутствие
editingFinished при смене темы; значения цветовых constants тестом не закреплены.

Linux: девять focused suites dialog-surface/context/archive forms/volume/source
prompts/recent-places/task-result/layout — PASS. После test-only уточнения
Button brush fixture повторён dialog-surface — PASS. macOS offscreen: те же
девять наборов PASS и финальный dialog-surface PASS. Native Cocoa: dialog-surface
6, context-dialog 10, password-dialog 6, task-result 8 QtTest PASS, failed/skipped
нет. Финальный dialog-surface повторён после уточнения fixture: 6 PASS.

Bundle пересобран, десять affected capture producers обновлены: 224 Cocoa
PNG/JSON, light/dark, без capture errors. Просмотрены main window в обеих темах,
compact RU report и recent places, create/extract, password/edit и split формы.
Снимки имеют честный pre-commit dirty provenance; последующее изменение только
test fixture не меняет captured production views. Эти проверки подтверждают
общую палитру затронутых потребителей, а не полную реализацию всех каталоговых
страниц Create/Extract/Split. Windows/full/slow проверки не запускались.
Катя проверила source diff; после исправления brushes и проверки inheritance
замечаний не осталось. Полный application lifecycle contract остаётся открытым.

## Cancelled independent work requires a final review

2026-11-05: полные original ProgressDialog2 cancellation/finish bodies сверены
с локальным source archive, включая callers, WaitMode и deferred finish внутри
вопроса. В оригинале сообщения после подтверждённого Stop не удерживают progress.
Это сознательно адаптировано к текущему lifecycle-контракту: сообщения остаются
до Close. Close завершённого отчёта больше не проходит в OnCancel и не посылает
повторную отмену transport. Подтверждение Stop само по себе не меняет итог worker:
завершение внутри вопроса по-прежнему может вернуть успех.

Qt task presentation фиксирует начало работы и явный просмотр результата.
Endpoint передаёт признак непросмотренной отмены только при фактическом native
user-break outcome. Незапущенные формы не создают отчёт. Source-проверка реальных
GUI routes подтвердила: codec discovery и подготовка Compress/Extract до формы
не вызывают BeginWork. WaitMode early return сохраняет факт запуска worker.
Алгоритмы native progress, архивирования и benchmark не заменены; оригинальные
baseline units/hashes не менялись, новых внешних imports нет.

Benchmark OnCancel и завершение сверены с полными original bodies. Close во время
работы запрашивает остановку; его автоматическое закрытие после worker finish
не считается просмотром. Stop → finished → явный Close отмечает просмотр,
поэтому application не показывает второй cancellation report. Restart сбрасывает
признак просмотра. Blanket-исключения всех benchmark results нет.

Application registry принимает итог до освобождения observer hold и сохраняет
его после закрытия последнего файлового окна. Общий report показывает отмену,
сохраняет исходный outcome и reported output paths, позволяет полное Copy/Close.
Он не объявляет частичные outputs успешно завершёнными или проверенными.
Обычные ошибки остаются у native owner; секреты и разрешения в сессию не попадают.
Признак отмены несовместим с success/warning/failure и отвергается validator.

Linux: восемь focused suites protocol/launcher/hash/benchmark/benchmark-lifecycle/
progress-cancel/task-result/task-lifecycle PASS; после уточнения EN/RU report
fixture повторён task-result PASS. Финальная правка Close проверена четырьмя
прямыми consumers: progress-cancel/progress-worker/task-result/benchmark PASS.
macOS offscreen: восемь suites PASS, затем те же четыре финальных consumers PASS.
Первый Linux запуск выявил ошибку fixture: native mnemonic `&Close` сравнивался
с `Close`; исправлена проверка видимой подписи. Это не product failure.

Native Cocoa: launcher 5, benchmark 9, task-result 11 QtTest PASS. Финальный
progress-cancel 9 и progress-worker 4 PASS; failed/skipped нет. Проверены реальная
отмена hash после закрытия последнего файлового окна, удержание event loop до
Copy/Close/Escape, repeated Quit, draft cancel без report, benchmark active Close
и Stop → review → Close без повторного report, завершение worker внутри вопроса.
Первый direct native запуск не имел runtime `7z` в PATH; исправлен только harness,
прогон повторён успешно. Windows/full/slow проверки не запускались. Катя провела
source review, нашла и подтвердила исправление повторного Cancel; runtime у неё
не запускался. Временные native fixtures и awake helper удалены, cache сохранён.

Review bundle пересобран. Producer task-result обновлён: 24 Cocoa PNG/JSON,
EN/RU, light/dark, обычный и compact размеры, сбой и отмена; capture errors нет.
Compact dark RU и light EN cancellation снимки просмотрены: footer доступен,
исходный текст прокручивается, полное Copy сохраняет также output paths.
Подписи путей не обещают фактическую запись всех outputs. Последнее уточнение
этих подписей проверено focused Linux task-result и обоими native capture runs.
Capture provenance остаётся pre-commit dirty, остальные producers не переименованы
как новые проверки. Общая gallery обновлена без повторного захвата тех же кадров.

Полный Continue/Cancel/Show coordinator, draft-discard handshake и перенос
in-process/external/mount owners остаются отдельными незавершёнными границами
этого же активного аудита.

## Shared secret editing

2026-11-05: проверены исходные `PasswordDialog.cpp/.h`, обработчики
`ExtractDialog::UpdatePasswordControl` и `CCompressDialog::UpdatePasswordControl`
с их инициализацией/callers в записанном оригинальном архиве. `QtSecretEdit` —
общий project-owned presentation control. Win32 смена password char и повторная
запись текста заменены Qt echo-only переключением с сохранением числовых
cursor/selection positions. Нет дополнительного снимка секрета. Восстановление
выделения после native FocusOut ограничено связанным переключателем, клавиатурный
фокус не возвращается в поле. Изменившееся состояние восстанавливается без
безусловной повторной установки selection. Original Password/ShowPassword
ReadControls, accepted results, настройки и cancellation сохранены. Скрытие
подтверждения остаётся у формы создания; отдельный черновик не заменяется.

Linux x86_64: password_dialog/extract_dialog/source_compress_dialog — PASS.
macOS arm64: те же три offscreen suites — PASS, затем три focused native Cocoa
cases — PASS. Проверены реальный клик, перевод фокуса и Space, оба направления
показа, курсор в середине, forward/backward selection, Unicode/пробелы,
отсутствие textChanged при показе и независимый confirmation draft.
Геометрия формы не менялась; прежние rendered snapshots сохраняют применимость.
Windows runtime этим проходом не проверялся.

## Lucide

[Отдельный baseline](../lucide-baseline.json) фиксирует полные исходные SVG и
LICENSE из <https://github.com/lucide-icons/lucide.git> на
`1ef60af944de207e0d5176905a0759ba12e6ee8d`. Он включает прежние и новые command
symbols, чтобы обновление всего используемого набора было воспроизводимо.
Для границы контейнера в LocationBar добавлена геометрия `icons/archive.svg`
того же baseline; она обозначает native Archive segment и не определяет тип
источника по расширению. Для Places добавлены `folder`, `monitor`, `network`,
`hard-drive` того же baseline: полная геометрия, без SVG XML и package bindings.
Папка использует общий контур с существующим folder-plus; остальные фигуры
адаптированы к тому же QPainterPath renderer. Значки списка файлов и namespace
внутри списка остаются у native owner; sidebar получает chrome symbols по
типизированной идентичности места.
Перенесена только геометрия выбранных SVG в `geometry(QtCommandIcon)` существующего
palette-aware QPainterPath renderer. XML, JavaScript packages и generated bindings
не используются. SVG arc geometry адаптирована к QPainterPath; точное совпадение
пикселей SVG renderer не является контрактом. Новых runtime зависимостей нет.
Полный неизменённый LICENSE (ISC и Feather-derived MIT) остаётся у зависимости
в `third_party/lucide/`; файлы адаптации живут у UI theme owner.

Проверка исходных байтов и review предложенного обновления используют существующий
инструмент; reference checkout читается без изменения:

```sh
python3 scripts/upstream_baseline.py reference-verify \
  --manifest docs/lucide-baseline.json --upstream /path/to/lucide
python3 scripts/upstream_baseline.py reference-review \
  --manifest docs/lucide-baseline.json --upstream /path/to/lucide \
  --new-revision FULL_COMMIT --output /tmp/lucide-review
```

Review содержит исходный baseline, предложенные оригиналы и локальную адаптацию.
Основной inventory проверяется обычным `upstream_baseline.py inventory`.

## Проверки

Визуальное соответствие живому макету пока не принято. Первая функциональная
итерация не переносила достаточно точно вкладки, поверхности, sidebar и таблицу.
Следующий связанный проход оформляет вкладки и рамку пути, адаптивные подписи
действий и поверхности главного окна без изменения native алгоритмов.
`main_shell` дополнительно проверяет видимый текущий сегмент после resize и
освобождение места правой таблицы при скрытом sidebar. Sidebar, распределение
колонок и состояния выделения требуют дальнейшей визуальной сверки.
Для этого прохода Linux: `main_shell` и три workspace suites — PASS;
macOS: полный `main_shell` и Cocoa EN/RU light/dark normal/compact review — PASS.
Windows этим проходом не проверялся; прежние результаты ниже относятся к первой
функциональной итерации и не подтверждают новое оформление.

Следующий проход выделяет `QtPlacesSidebar`: общие строки избранного и native
расположений, типизированные запросы открытия и общая палитра. Начальные записи
используют тот же favorites model, что пользовательские; явное удаление не
отменяется при запуске. Native расположения получаются через существующий
`CRootFolder`/`IFolderFolder`, а открываются прежним navigation owner.
Повторная сверка `FM.cpp` из записанного source archive подтверждает сохранение
полных тел `CSplitterPos`: Qt-адаптер исключает sidebar и разницу ширины grip
из клиентских координат. Win32 захват/WM_SIZE заменены Qt handle/events, а
модель отношения и границ сохранена. Статус размещён под файловой областью,
активная сторона обозначена в общем navigation-row component. Восстановление
геометрии предшествует восстановлению разделителя. ShortcutOverride удерживает
стрелки и Return у сфокусированного grip, исключая запуск файлового Rename.
Колонки и выделение остаются следующим незавершённым визуальным сценарием.
Focused-проверки этого прохода: Linux — семь suites (main shell, workspace3,
root navigation, placement и panel shell); macOS — пять suites (main shell,
workspace navigation/session, placement и panel shell); Windows interactive —
main shell, workspace navigation, root navigation и placement. Все PASS.
Финальные Cocoa EN/RU light/dark normal/compact PNG/JSON просмотрены:
две файловые области при 640×400 имеют 246/246 логических пикселей;
grip Return не запускает Rename, status находится под списками.

Следующий проход выделяет общий компонент `QtWorkspaceFileTable` и использует
существующий row delegate для presentation. Начальная файловая раскладка стала
Name/Size/Type/Modified, с приоритетом пользовательских native настроек.
Исправлены восстановление Type после скрытия native колонки, сохранение сортировки
скрытого свойства, fallback при исчезновении свойства из provider и current outline
при горизонтальной прокрутке. FullRow=false сохранён. Marked и selected различимы;
фон рисуется до отступов контента, а deleted text сохраняет контраст в active,
inactive и disabled палитрах. Отсутствующий native каталог скрывает заголовок.
Linux x86_64: 12 focused suites (таблица, native columns/schema/changes,
sort/commands, deleted draw, list settings, main shell и workspace3) — PASS.
macOS arm64: шесть focused CTest suites (таблица, columns/changes, deleted draw,
sort commands и main shell) — PASS, offscreen QPA.
Native Cocoa EN/RU light/dark normal/compact review обычной таблицы обновлён;
геометрия selection/current/inactive дополнительно проверена offscreen.
Первый focus run был заблокирован рабочим столом macOS: AppKit сообщил
`com.apple.loginwindow`. После подтверждённой пользователем разблокировки
native Cocoa selection/current/inactive прошли в EN/RU, light/dark, normal/compact;
все 24 PNG обновлены и просмотрены. Эксперименты с активацией удалены.
MIME-комментарии Qt могут иметь язык системы; собственные заголовки и fallback
строки переводятся приложением. Windows новым проходом не проверялся.

Компактный workspace: общий tab bar скрывает подписи в самых узких вкладках,
сохраняя полное имя/путь и доступное закрытие active/hover. Home/End и стрелки
переключают реальные источники; middle click использует существующий lifecycle
закрытия с проверкой стабильного id. Hover пересчитывается по актуальному hit
после layout/scroll. Native scroll arrows подключены к общему chrome style;
Qt рисует и обрабатывает само управление прокруткой. Явный show после reparent
исправляет скрытые общие tabs/actions при возврате к retained runtime.
Deferred focus ждёт не только завершения операции, но и visible/enabled виджета;
новые ввод, shortcut или смена контекста отменяют прежнее намерение.
Native reader, CPanel selection, операции и async transport не заменены.

Linux: восемь focused suites workspace/places/tabs/main shell/shortcuts/rename —
PASS; после локальных уточнений повторены только затронутые tabs/main shell/rename,
затем финальный tabs suite — PASS. macOS/offscreen: workspace tabs/places,
navigation/session, main shell — PASS; rename lifecycle — PASS после открытия
редактора пути через действующее публичное управление, а не скрытый QLineEdit.
Native Cocoa tabs suite: keyboard, middle click и обе async focus ветви — PASS.
Физический hover scenario имеет явный SKIP: QCursor::setPos не переместил
реальный указатель в этой сессии. Он полностью проверен Linux/offscreen;
не выдаётся за native физическое наведение. Dense Cocoa EN/RU light/dark
normal/compact PNG/JSON просмотрены: значок и close помещаются, стрелки quiet.
Отдельный behavior test проверяет достижимость последнего native места и
добавления избранного при компактном sidebar и длинных подписях.

Source scope и runtime evidence учитываются отдельно. Focused workspace suites
проверяют возврат выбора вкладки, перенос живого источника после уничтожения
исходного окна/backend, реальные clipboard bytes, сессию нескольких окон,
недоступный descriptor и изменение inactive архива внешним редактором.
Геометрия и переводы проверяются вместе с существующими main-window consumers.
Сверка изменённой native области с исходными units завершена: активные native
ветви сохраняются; новые ветви относятся к Qt host/clipboard/workspace boundaries.
Алгоритмы reader/update/copy не заменены. Базовый inventory и 17 оригинальных
Lucide units прошли проверку.

Проверки 2026-11-05. Focused Linux x86_64: workspace/navigation/session/archive, main shell, layout,
root navigation, panel shortcuts/keys, language UI, binding lifecycle,
external edit, Copy routing, archive states и operations — PASS. После локальных
уточнений повторяются затронутые suites, а не весь набор. macOS arm64 — PASS:
workspace, source process/clipboard, commands и language consumers; Cocoa review
покрывает EN/RU, light/dark, 980×620/640×400, одну/две панели и несколько вкладок.
Windows x86_64 — PASS: interactive runner для workspace lifecycle, shell,
clipboard scenarios и shortcuts; зарегистрированные CTest suites используют
offscreen QPA, поэтому эти результаты не подтверждают native Qt focus/clipboard.
Заключительные workspace reruns на всех
трёх ОС включают отказ закрытия исходного окна, повторное открытие закрытой панели,
дубликаты избранного и недоступное место через session/favorites/new window.
Финальный macOS bundle собран, изолированный startup smoke завершился успешно.

Эти focused проверки не являются полным/slow gate. Снимки показывают реальный
Qt client chrome; pixel parity с оригинальным Windows и физические жесты
перетаскивания/раскладки клавиатуры ими не установлены. Финальная пользовательская
визуальная приёмка остаётся отдельным review согласованного редизайна.
