# Главное окно и workspace

Изменение по согласованному дизайну, 2026-11-05. Вкладки, независимые окна,
сессия, именованное избранное и breadcrumb chrome — project-owned Qt integration.
Они не заимствуют алгоритмы архивов или файловых систем.

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
