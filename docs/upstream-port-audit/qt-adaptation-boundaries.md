# Qt adaptation boundary review — 2026-10-18

## Native build language boundary — 2026-10-27

The pinned SDK's `CPP/Build.mak`, `CPP/7zip/7zip_gcc.mak` and compiler-warning
makefiles were compared with the runtime snapshot builder. GNU Make wrappers
override the upstream C++11/default mode with C++23. The existing NMake recipe
adaptation adds the MSVC C++23 mode only to C++ compilation macros and extra C++
units. C and assembly recipes retain their source language. Generated build rules
are object prerequisites so changed compiler settings invalidate existing outputs
through normal incremental builds and the shared compiler cache. Original SDK
inventory hashes remain unchanged; no algorithm bodies were adapted.

The 2026-10-28 Release compiler audit found MSVC's CMake C++23 mapping still
selected `/std:c++latest`. The shared project/native-dependency configuration
now selects the dedicated C++23 mode on MSVC 19.43+, and generated NMake C++
recipes use `/std:c++23preview` too. C recipes remain unchanged. This keeps
producer and consumer language modes aligned without importing SDK algorithms
or changing the original build-rule baseline.

### Executable stack reserve — 2026-10-28

CMake now preserves the Windows executable stack reserve from the same pinned
`CPP/Build.mak`: 8 MiB for 64-bit and 2 MiB for 32-bit targets. The complete
original inventory unit remains byte-exact in `third_party/7zip/CPP/Build.mak`,
SHA256 `2fa73421ef3236655890cf479d6e5c8f0083667173248cea801e46fd698374df`,
at SDK revision `b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>, verified through the existing SDK baseline.
Only the `LFLAGS /STACK` executable setting is adapted into the root CMake
configuration; NMake platform selection becomes pointer-width selection.
DLLs retain their existing settings. The existing UDF ABI traversal fixture
reproduced an ASan stack overflow at depth 1022 with the default Windows reserve;
depths 256 and 512 passed. Reader recursion, limits and error paths are unchanged.
The retained original notices and licenses apply to the source inventory.

## Temporary-object identity on ext4 — 2026-10-28

The local cleanup registry is an OS adapter: upstream `CTempDir::Create`,
`CreateTempFile2` and their consumers were compared; the SDK has no persisted
ownership registry. Their name generation and deletion behavior remain unchanged.
The existing retained-edit regression failed on the zram/ext4 workspace because
ext4 reused both an inode and its birth timestamp. A disposable OS probe observed
943 such collisions in 1000 recreations, with distinct inode generations.
The Linux identity adapter now reads `FS_IOC_GETVERSION` through a read-only,
non-following descriptor, validates its device/inode and closes it with RAII.
Unsupported filesystems retain the existing identity fields. No object metadata,
filesystem decoder or archive algorithm is modified. The
[ext4 ioctl contract](https://kernel.org/doc/html/v5.19/admin-guide/ext4.html#ioctls)
documents the generation identifier; no external implementation was imported.

## Extraction callback initialization — 2026-10-26

The sanitizer-enabled C++23 build found an uninitialized archive-path diagnostic
flag when an already-open native handler called the extraction callback. The
original constructor, `BeforeOpen`, `OpenResult`, error reporting and callers
were compared with the pinned SDK. `_needWriteArchivePath` now starts false;
the original open callbacks still enable it when they establish an archive path.
No extraction or error-classification algorithm changes. Existing checksum,
volume-error and encrypted extraction cases reproduce the UBSan failure.

## Original review

### Non-BMP archive selections — 2026-10-26

The existing ISO/UDF backend extraction fixtures exposed a Qt-to-censor mismatch
for emoji names on hosts with 32-bit `wchar_t`: `toStdWString` supplied UTF-32
points, while SDK archive names and UTF conversion use UTF-16 units. Literal
selection/current-folder paths now pass through the existing SDK UTF-8-to-Unicode
converter before the unchanged censor/parser. No decoder or wildcard algorithm
is reimplemented. The official console extracted the same ISO fixture correctly
before this adapter correction, isolating the discrepancy to the host boundary.

### Windows development DLL dependencies — 2026-10-26

The complete SDK `C/DllSecur.c` and Console `MainAr.cpp` startup were compared.
The original default permits System32 and explicitly registered user directories;
it excludes PATH and the executable directory. Instrumented format plugins need
the compiler's CRT/ASan libraries even when the official console itself uses a
static CRT. CMake stages those dependencies in the owned deployment, and isolated
console fixtures copy them with the engine.

The adapted `src/platform/loader/DllSecur.c` registers only that executable's absolute directory
through `AddDllDirectory` for sanitizer development builds. Original default
directory flags and release behavior remain intact; PATH/current-directory lookup
is not enabled. The native Windows builder sets the development flag only for
this C inventory unit. Its original hash and runtime owner are pinned in the
existing SDK manifest. No archive algorithms or source language change.

Исходная точка проверки — `5c57c65`, upstream baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17` из
<https://github.com/ip7z/7zip.git>. Проверка началась с последних исправлений
режимов списка, выхода коротких процессов и доступности Quit, затем охватила
следующие границы. Сопоставлялись полные затронутые обработчики, их callers,
типы состояния и прямые Qt consumers; [базовый аудит переноса](../upstream-port-audit.md) сохраняет
учёт прежних omissions и платформенных решений.

Команды: source `MyLoadMenu`/`PanelMenu`/`FM` и `CApp` сопоставлены с
QtFileManagerCommands и проекцией QAction. Просмотрены повторная проверка
доступности перед dispatch, capabilities текущего provider/selection,
защита Open/Operation/Scan, восстановление после внешнего CallScope,
toggle rollback и закрытие с modal/source guards. Нового подтверждённого
пробела в этой границе не установлено.

Навигация и список: source BindToPath, OpenParentFolder, SetNewFolder,
OnSetSameFolder/OnSetSubFolder и восстановление выделения сопоставлены с
двумя Qt-представлениями, per-panel projection, asynchronous folder publication
и отменой прежних producers. Проверены generation/lifetime guards,
сохранение owning panel при смене фокуса, source indices и общий selection
model. Исправления доставки hover/Shift описаны в
[аудите событий списка](qt-list-events.md).

События и lifetime: просмотрены QtAppOperationState, отложенные callbacks
списка, FilesystemScanner/QtFolderProjection и close/destruction workflow.
Сверка source FM close/ReleaseApp с Qt quiesce подтверждает порядок join
workers до освобождения provider/temporary inputs, отключение consumers
и отказ устаревших queued deliveries. После исправления событий списка
дополнительного подтверждённого дефекта в просмотренных lifetime-путях нет.

Архивный transport: startup/FailedToStart/exit, completion, cancellation,
multi-stage continuations и controls сопоставлены с source progress/result
owners и их callbacks. Найденные пробелы и узкие Qt-адаптации описаны в
[аудите управления фазами](qt-phase-controls.md) и
[аудите состояния запроса](qt-request-state.md).
Проверка охватывает границы transport; архивные форматы и алгоритмы сохраняют
официальных владельцев.

Настройки и диалоги: source OptionsDialog, PropertyPage apply/reset semantics
и применение значений шести страниц сопоставлены с QtFmOptionsDialog,
сервисами ассоциаций и применением настроек к обоим спискам. Просмотрены
Apply/Cancel, Settings validation refusal, продолжение Apply других страниц
после association error, language notification и сохранение значений скрытых
платформенных настроек. Нового подтверждённого дефекта этой границы нет;
[платформенная видимость](options-platform-visibility.md)
сохраняет прежние необходимые отличия.

Полнота этой source-проверки относится к названным границам и сценариям.
Новых omissions либо disconnected consumers в них после исправлений не
выявлено. Runtime-свидетельства каждого изменения записаны в связанных
аудитах; focused PASS подтверждает соответствующее поведение на указанных ОС.
Рендеренное сравнение с Windows 7zFM, физический ввод и полный/slow gate
в эту проверку не входят.

Итог runtime-проверки: приложение и выбранные targets собраны incremental;
19 focused suites команд/настроек/навигации/lifetime/backend прошли на macOS
arm64 и Linux x64 / Qt 6.12. Windows x64 прошёл 18 из них в том же batch
`20261017T214943Z-5b5325cd`; последний `options_dialog` прошёл после коррекции
хрупкого test-only resize expectation в `20261017T215830Z-4e302ed8`.
[Геометрическое свидетельство](options-platform-visibility.md#переносимость-геометрической-проверки--2026-10-18)
разделяет реальные bounds и запрошенные размеры. Отдельные focused receipts
для controls и событий списка связаны выше. Кэши и постоянные build trees
сохранены. Проверка названных границ завершена; это ограниченная source/behavior
проверка, сохраняющая описанные в базовом аудите платформенные ограничения.

## Проверка предупреждений о владении — 2026-10-18

Дополнительная проверка выполнена после `217c2b0`, по upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` из того же репозитория.
Сопоставлены полные тела Rar5Handler и EnumDirItems с единым форматированием:
у первого различаются notices/includes, у второго также перевод диагностик.
Алгоритмы и их зависимости для этой проверки не изменялись.

Предупреждение анализатора в RAR5 не учитывает связь Create и Code:
оба caller сначала проверяют успешный Create для того же item, а IsEncrypted
и создание filterStream используют одну crypto extra record. COM-владелец
filterStream сохраняет raw filterStreamSpec до завершения распаковки.
Инициализацию указателя для подавления предупреждения не добавляли.

Для EnumDirItems проверены построение/удаление prefix chains, shrink vectors
и consumers в AgentOut, Update, HashCalc и QtArchiveCreationLifecycle.
Успешно опубликованный item ссылается на существующий prefix либо использует
корневой индекс; пустая ветвь удаляет свой prefix без оставшихся items.
Исключение при добавлении prefix прерывает перечисление до использования
результата. Wildcard::CheckPath сохраняет границы индексов через разность длин;
исходные vectors владеют строками на протяжении проверки. Подтверждённого
нарушения владения в этих предупреждениях не установлено.

Отдельно просмотрены QObject-владельцы menu action groups, Help shortcut,
queued callable contexts, отложенные rename/focus/hover события, leased temp
registry и native/Qt file-transfer callbacks. Родители, retained payloads,
контекст доставки и close/join сохраняют соответствующие объекты; уничтожение
окна отсоединяет consumers до освобождения provider. Это проверка названных
границ, а не доказательство безопасности произвольного удаления QObject
из любого пользовательского callback.

Без новых production-правок прошли существующие owned_temp,
owned_temp_publication, archive_creation_lifecycle, native_rar_memory и
source_delete: macOS arm64 5/5 (7,52 с), Linux x64 5/5 (9,68 с), Windows x64
5/5 (45,83 с), Qt 6.12. Guest run: `20261018T124841Z-7bdfff4c`;
Windows link fixtures выполнялись с elevated desktop. Постоянные сборки и
compiler caches сохранены. RAR-проверки подтверждают свои compressed/memory
сценарии; шифрованный RAR здесь проверен по исходникам, без нового runtime
свидетельства. Инъекция отказов, минимальный пример Qt, full/slow gate и
новая визуальная приёмка не выполнялись. Общий аудит проекта продолжается.

## Пакетная распаковка и ожидающие GUI-вызовы

Дополнительно просмотрены полные QtArchiveBatchOpen/QtArchiveBatchExtract,
QtArchiveFolderManager и QtShellArchiveServices с их заголовками, исходные
ExtractGUI/Common Extract и граница join в ProgressDialog2. Сравнение полных
ExtractGUI и Extract с upstream `9128b80e3a471108678e2b3ec3c985861c8d7b0f`
после общего форматирования сохраняет source extraction loop, COM-владельцев,
фильтрацию и учёт томов; отличия — Qt presentation, диагностики и уже учтённые
open/progress/path-policy services. Новых omissions в проверяемом сценарии нет.

Ссылки opener на параметры распаковки, native progress и output policy живут
до завершения исходного worker. Native-open callback отсоединяется scope guard
после Open; retained resource/catalog держат handler и его зависимости.
QtExternalGuiCall, callback dialogs/password и их callers просмотрены вместе
с pending/running/cancelled состояниями: ожидающий worker сохраняет captured
данные до результата либо отменяет ещё не начатую доставку. Таймеры используют
QObject context. Нового подтверждённого дефекта в этих границах не установлено.

Существующие source_extract_backend, archive_open_options и external_gui_call
прошли на macOS arm64 (3/3, 1,24 с), Linux x64 (3/3, 1,21 с), Windows x64
(3/3, 3,79 с), Qt 6.12; guest run `20261018T133638Z-d15df4e8`.
Проверки включают пакетную распаковку, отмену перезаписи и возврат исключения
из GUI в ожидающий worker; прежние результаты других сценариев ими не заменяются.
Сборки incremental и compiler caches сохранены. Новые тесты, инъекция отказов,
реальные пользовательские архивы, full/slow gate и UI appearance acceptance
для этой source-проверки не требовались и не выполнялись.

### Единый запрос отложенной GUI-доставки — 2026-10-30

Сопоставлены записанный PanelItemOpen.cpp::MyThreadFunction, SendMessage и
CExitEventLauncher::Exit из 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17,
локальный launcher, все четыре QtExternalGuiCall consumers и полный adapter.
Исходный process wait/update алгоритм, диалоги, source join и публичная сигнатура
не менялись. Нового импорта или изменения baseline нет.

Callbacks готовности и тела принадлежат одному Request; повторная доставка
удерживает этот запрос вместо копирования callbacks на каждом таймере.
Четыре числовые фазы заменены типизированными состояниями. Проверка готовности
сохраняет собственную фазу владения: запрос выхода отменяет только ещё не
доставленный вызов, уже выполняющийся callback удерживает worker до результата.
Исключение по-прежнему возвращается ожидающему worker.

В external_gui_call добавлены две проверки выхода во время готовности и тела:
worker не возвращается, пока GUI использует его captures, и получает результат
доставленного вызова. Они дополняют существующие отмену до доставки и возврат
исключений; не читают внутренние фазы и не закрепляют период polling.
Это упрощение протокола; отдельный воспроизведённый пользовательский баг не заявляется.

Focused external_gui_call, archive_external_lifetime и archive_external_edit:
Linux `20261030T132017Z-e561b745` — 3/3 PASS, 27,46 с CTest,
существующий RAM scratch для временных данных, disk incremental build/cache.
macOS — те же 3/3 PASS, 29,04 с CTest.
Windows `20261030T132241Z-55991dc3` — те же 3/3 PASS, 86,34 с CTest.
На macOS 36 Qt checks без пропусков. Сценарии и таймауты не менялись;
full/slow gate и визуальная паритетность не заявляются. Полезные incremental
outputs, compiler caches и небольшие логи/receipts сохранены.

## Локальные файловые и вспомогательные адаптеры

Полностью просмотрены VerCtrl.cpp и QtVerCtrlFileServices с заголовком.
Сравнение с upstream `9128b80e3a471108678e2b3ec3c985861c8d7b0f` после общего
форматирования сохраняет исходные чтение, сравнение, backup/history,
commit/revert и ветви отказа. Отличия ограничены ранее учтёнными файловыми
сервисами POSIX, настройками и Qt-диагностикой. CByteBuffer владеет прочитанными
байтами; файловые объекты локальны, а переданные временные FILETIME копируются
выходным NIO-владельцем. Нового дефекта времени жизни не установлено.
Существующий verctrl проверен на трёх ОС в составе batch
`20261018T134937Z-6240522b`; общие результаты и ограничения приведены в
[проверке потоков и свойств](archive-close-reopen.md#item-streams-and-property-consumers--follow-up-review).

Дополнительно просмотрены полные небольшие QtNativePanelKeys,
QtDialogScreenServices и QtCallbackProgressTransport с их заголовками:
keyboard candidates используются в пределах выражения, show-фильтр принадлежит
окну и снимается до отложенного удаления, cancel callable принадлежит transport.
ArchiveCodecImage и ArchiveSfxModule используют локальные файловые объекты,
owning строки/контейнеры и retained временную директорию при частично выполненном
копировании. Эти source-проверки не выявили нового подтверждённого нарушения
владения и не добавляют runtime-приёмку иных сценариев этих компонентов.

## Модели списка и обновлённый анализ

Повторно просмотрены полные FileTable/ArchiveTableItem, QtPanelList,
QtFileListHover, QtPanelAddressCombo, QtFolderProjection и QtAppOperationState
с заголовками, публикацией колонок/строк и source consumers PanelListNotify,
PanelSort и PanelSelect. Отложенный hover хранит persistent index и проверяет
его против текущего представления; публикация удерживает folder и прекращает
старое поколение после callback. Таблица получает source-текст в рамках
актуального panel/provider, сохраняя owning QVariant/QString. Адресные таймеры
имеют QObject context; отложенный Rename повторно проверяет исходную папку и
индекс. Нового подтверждённого дефекта в проверенных путях не установлено.

panel_columns, panel_name_text, list_activation, address_combo и
panel_address_keys прошли на macOS arm64 (5/5, 11,24 с), Linux x64
(5/5, 16,53 с) и Windows x64 (5/5, 28,43 с), Qt 6.12, guest run
`20261018T135402Z-8253eb6e`. Использованы существующие incremental targets и
compiler caches. Новых тестов, production-правок или appearance changes нет.

Clang-анализ обновлён для 288 production translation units из текущей macOS
compile database с её SDK/флагами и существующим ccache. Ошибок разбора нет;
результат содержит те же 47 диагностик в 31 единице, что прежний проход, без
новых diagnostic identities. Перемещение MyVector.h учтено при сравнении.
Полные plist-multi-file трассы сохранены также для предупреждений в заголовках:
обычный plist не включает межфайловые пути. Во время основного прохода менялись
два implementation-файла меню; они вместе с межфайловыми диагностическими
единицами перепроверены отдельно (19 units, без ошибок и изменений исходников
во время повторной проверки). Предупреждения по-прежнему оцениваются по
контрактам и callers, а не считаются подтверждёнными ошибками автоматически.

Это обновление macOS diagnostic evidence и проверка названных модельных границ.
Windows/Linux-only translation units не входят в эту compile database;
профильные runtime PASS не доказывают их полного статического анализа.
Новые инъекции отказов, минимальный пример Qt, полный gate, slow suites и
рендеренное сравнение с оригинальным 7zFM не выполнялись. Общая проверка
полноты аудита проекта остаётся незавершённой.

## Согласование областей аудита времени жизни

Проверка использует существующий
[компонентный реестр P2-11](../archive/qt-port/upstream-port-audit/p2-11.md#functionalcomponent-review-register).
Его историческая приёмка сама по себе не подтверждает текущую ревизию.
Последующие проверки владельцев соотносятся с его областями следующим образом:

| Области реестра | Проверенные границы и текущее свидетельство |
| --- | --- |
| P, H — окно, панели, представления | Публикация папки/строк, поколения, индексы и hover рассмотрены выше; [Rename](deferred-rename-owner.md) удерживает исходного владельца, [смена языка после Close](closed-language-event.md) прекращает обращение к освобождённой панели. |
| C, S, K — параметры и создание | Состояние source-диалогов, профили и consumers проверены вместе с DTO; [каталог кодеков](codec-thread-ownership.md) удерживается native workers, [volume writer](multiout-lifetime.md) и Qt-публикация томов имеют владельцев до передачи результата. |
| U, V, R — native open/update/transfer | [Close/ReOpen и свойства](archive-close-reopen.md), пакетная распаковка выше, retained catalog/link и callbacks просмотрены до join; заимствованные имена не переживают исходного handler. |
| J — backend и transport | [Process/listing/overwrite notifications](process-notifications.md), [внешние процессы](external-process-ownership.md), [фазы](qt-phase-controls.md) и [состояние запроса](qt-request-state.md) проверены с отменой, завершением и повторным запуском. |
| W, F — workers и файловые операции | [Ожидание перезаписи](worker-prompt-cancellation.md), [Pause](process-pause-ownership.md), [транзакционный перенос](file-transfer-ownership.md), checkpoints и shutdown проверены вместе с их непосредственными consumers. |
| B — общие зависимости | Общие source object groups сохраняют авторитетные реализации; [object vector](object-vector-ownership.md) и общий COM-владелец кодеков проверены как зависимости нескольких компонентов. |

Платформенные владельцы проверялись отдельно от macOS compile database:
[Shell/GIO/native error buffers](platform-result-ownership.md),
[native drag](native-drag-session.md), [shell menus](shell-menu-ownership.md),
[COM server locks](native-shell-server-lock.md) и [POSIX files](linux-file-close.md).
Просмотрены также Windows association adapter, ADS folder, shell presentation,
AppKit completion и Linux launcher/taskbar consumers; их runtime-ограничения
остаются в соответствующих свидетельствах. Отсутствие platform-only модуля
в macOS анализе не считается его статической проверкой.

После появления новых сочетаний клавиш дополнительно проанализированы десять
новых/изменённых production translation units с текущими macOS SDK/флагами.
Ошибок разбора и изменений файлов во время прохода нет. Три предупреждения
совпадают с ранее разобранными Qt callable/QActionGroup owners; новых нет.
Полные тела нового shortcut adapter и его panel/editor/close consumers
просмотрены: действия сохраняют исходных QObject-владельцев, callbacks имеют
контекст окна. Нового подтверждённого дефекта времени жизни не установлено.

Это согласование source-проверок по областям, а не доказательство отсутствия
всех ошибок. Общая runtime-проверка и разбор её сбоев продолжаются; slow suites,
полное sanitizer-покрытие зависимостей и визуальное сравнение с оригинальным
Windows 7zFM этим аудитом не установлены.

## Проверки Add/Extract после изменения представления

Общий gate выявил проверки прежнего двухколоночного вида и постоянно открытых
полей. Они сопоставлены с текущими конструкторами, исходными CompressDialog /
ExtractDialog resources и обработчиками upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, а также с
[согласованным редизайном](ui-redesign.md). Производственные алгоритмы здесь
не менялись. Проверки расширенного состояния теперь явно открывают Advanced и
пароль; сохраняются проверки полной видимости, текста, порядка, параметров и
результатов операций. На POSIX проверяется согласованное скрытие неподдерживаемых
Windows-настроек. Прокрутка в source_compress_dialog раскрывает весь combo,
а не только его input-method cursor rectangle.

compression, codec_creation_ui, compression_order, compression_dictionary,
compression_method_state, compression_memory, compression_limit,
compression_solid, sfx_creation, source_extract_advanced, dialog_path_rules,
source_compress_dialog, ui_geometry и language_focus прошли адресные проверки
на macOS arm64, Linux x64 и Windows x64 / Qt 6.12. Linux run
`20261018T143721Z-ea996e90`, Windows run `20261018T144745Z-441ad471`.
Остальные сбои этих batch не считаются приёмкой и разбираются отдельно.
Использованы существующие incremental targets и compiler caches.

Windows дополнительно прошёл native_gui_workflow и source_metadata_storage.
Native cancellation-сценарий раскрывает настройки через UI Automation перед
проверкой доступности полей, затем закрывает форму и проверяет неизменность
частных входов/отсутствие результата. WIM-проверка допускает расширение окна
по native font minima, сохраняя полную видимость контролов, Options/Add
Cancel/OK/restart и фактическое сохранение ADS/security в архиве. Геометрические
проверки и native accessibility не устанавливают rendered pixel parity с
оригинальным Windows 7zFM. Сбои остальных областей общего gate ещё не закрыты.

## Проверки меню и toolbar

ui_archive_states и list_settings_runtime теперь обходят фактическое дерево
подменю после согласованной группировки команд. Проверки состава, доступности
и native Windows system menu сохранены. Проверка истории явно фокусирует
адресное поле перед Return. Из ui_layout убраны два дублирующих ожидания общего
toolbar style: отдельные кнопки имеют собственное представление, которое
проверяется в main_shell. Геометрия, видимость и сохранение пользовательских
настроек по-прежнему проверяются.

Все четыре набора прошли на macOS arm64, Linux x64 и Windows x64 / Qt 6.12;
guest batches — `20261018T143721Z-ea996e90` и
`20261018T144745Z-441ad471`. Производственная логика и source command ownership
этой коррекцией тестов не менялись. Результат относится к названным наборам,
а не ко всему общему gate или визуальному паритету.

## Синхронизация ответов panel_key

В полном macOS прогоне panel_key завис в source OnCopy, ожидая QDialog::exec.
Одноразовый тестовый ответ мог выполниться до входа в modal loop. Ответы
Copy/Move/Extract теперь ждут фактически показанный активный CopyDialog,
останавливают локальный таймер и только затем отклоняют форму. После возврата
команды проверяются наблюдение диалога и назначение противоположной панели.
CTest ограничивает длительность всего набора. Производственные dispatch,
source OnCopy и владение окнами этой правкой не менялись.

panel_key прошёл на macOS arm64, Linux x64 и Windows x64 / Qt 6.12;
guest batches — `20261018T143721Z-ea996e90` и
`20261018T144745Z-441ad471`. Исправление относится к тестовому responder,
а не является новым свидетельством ошибки времени жизни приложения.

## Длинные подписи в компактных формах

Windows-прогон воспроизвёл обрезание comboPrompt, memMessage и подписи точности
времени в Options. Например, длинной подписи при ширине 228 требовалось 54
пикселя высоты при выделенных 50, русскому сообщению памяти — 110 при 93.
Сопоставлены полные ComboDialog.cpp/.h/.rc, MemDialog.cpp/.h/.rc и
CompressDialog COptionsDialog с CompressOptionsDialog.rc из
<https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, текущие Qt constructors и callers.
Исходные решения, значения и OnOK/Cancel не меняются; исходные Win32 resources
не предоставляют Qt height-for-width adaptation.

Узкий QtWrappingLabel в существующем dialog-layout adapter включает требуемую
при текущей ширине высоту перенесённого текста в layout minimum и обновляет
геометрию при изменении ширины. Используется только у трёх затронутых подписей.
QObject-владельцы, buddy/accessibility bindings и source module boundaries
сохранены; дополнительный upstream код не копировался. Одна смена size policy
оказалась недостаточной и в итоговую правку не вошла.

Существующие source_prompt_presentation, combo_dialog,
compress_options_presentation, language_dialog и language_ui прошли на macOS
arm64, Linux x64 и Windows x64 / Qt 6.12. Guest runs:
`20261018T145954Z-975d9c67` и `20261018T145940Z-a999a69e`;
неудача отдельного palette test в последнем batch сюда не включается.
Из проверки Options убрано ограничение высоты через внутренний unconstrained
sizeHint; проверки видимости, текста, порядка и клавиатурного поведения
сохранены. Регрессии существовали до исправления и воспроизвели ошибку на Windows.

Три затронутых producer дополнительно прошли с native Cocoa QPA; просмотрены
34 текущих widget JSON, видимые подписи помещаются целиком. Это EN/RU и
normal/compact состояния указанных форм, без новых screenshots и без сравнения
с рендеренным оригинальным Windows 7zFM. Для macOS использована локальная
HIG-выжимка от 2026-10-18: официальный Layout HTML требует JavaScript,
а Markdown endpoint не удалось прочитать доступным инструментом.
Финальные пять production units, включая последнюю правку shortcuts,
перепроверены Clang с текущими SDK/флагами: ошибок/предупреждений нет,
исходники во время этого прохода не менялись. Build trees и ccache сохранены.

## Итог аудита времени жизни — 2026-10-18

Проверка согласованного класса ошибок по областям проекта завершена:
заимствованные данные после Close/ReOpen/публикации, устаревшие индексы,
асинхронные callbacks, отмена, shutdown и передача владельцев при отказе.
Области соотнесены с существующим компонентным реестром выше; подтверждённые
дефекты исправлены отдельными изменениями с привязанными source/runtime
свидетельствами. Непогашенных подтверждённых дефектов этого аудита не осталось.
Прежние baselines, notices, необходимые платформенные адаптации и omissions
сохраняются в соответствующих module audits; проход не заменяет их новым
независимым переносом алгоритмов.

Общие интеграционные прогоны выполнили formatting и all-target builds, затем
выявили следующие результаты default CTest:

| Платформа | Первый общий прогон | Последующая проверка |
| --- | --- | --- |
| macOS arm64 | 324/342 passed, 18 failed | Все 18 сбоев закрыты адресными проверками. |
| Linux x64 | 327/344 passed, 17 failed | Все 17 сбоев закрыты адресными проверками; run `20261018T140422Z-4f4ca681` и follow-ups выше. |
| Windows x64 | 340/362 passed, 22 failed | Все 22 сбоя закрыты адресными проверками; run `20261018T140143Z-fa617ecc` и follow-ups выше, плюс [palette regression](adaptive-colors.md#контраст-выбранных-style-colors--2026-10-18). |

Это составная приёмка общего прогона и адресных исправлений, а не повторный
полностью зелёный full gate на финальной ревизии. После локальных follow-ups
повторялись затронутые наборы и прямые consumers. Финальное приложение заново
собрано и прошло application_starts на macOS, Linux и Windows; guest run
`20261018T150926Z-4357e8f8`. Windows дополнительно повторно прошёл
source_metadata_storage после изменения высоты подписи Options; его
Windows-specific bodies на Linux пропускаются. Сборки и compiler caches
сохранены, включая исправление передачи заданного числа compile jobs гостям.

Статические результаты относятся к macOS compile database и указанным
follow-ups; platform-only владельцы проверены отдельно по исходникам и
профильным consumers. Ранее выполненные частичные ASan/TSan проверки не
являются полным sanitizer-проходом всех зависимостей. Slow suites исключены,
новая приёмка рендеренного паритета с оригинальным Windows 7zFM не выполнялась.
Результат фиксирует проверенные области и найденные дефекты, а не гарантирует
отсутствие иных ошибок во всех возможных сценариях.

## Именованные маршруты архивных операций — 2026-11-01

Повторно просмотрены пакетные Test/Extract от CApp/CPanel до CompressCall2,
QtShellArchiveServices, native opener, ExtractGUI и независимого 7zG; вместе
рассмотрены связанные Add/hash/popup consumers. Полные оригиналы
`CPP/7zip/UI/Common/CompressCall2.cpp`, `CPP/7zip/UI/FileManager/Panel.cpp`
и `App.h` совпадают с baseline hashes ревизии
`b88c6bc09339de32f7d18bbab900686b244f42ba` из того же source repository.
Нового внешнего импорта и изменения baseline нет.

Concrete Qt adapter теперь принимает одну структуру именованных Routes вместо
длинного позиционного списка callback arguments. Она же владеет callback-ами
внутри сервиса. Toolbar batch/Add, shell popup, standalone GUI factory и все
прямые тестовые consumers переведены вместе. Отсутствующий маршрут сохраняет
прежний in-process fallback; virtual source interfaces и сами исходные
CompressCall2/GUI/Extract/Update/hash algorithms не меняются.

Сохранены callbacks, их параметры/captures, settings path, retained catalog,
ошибки/приоритет результата, output notifications и lifetime до source worker
join. Typed metadata opening без retained native reader и учёт томов остаются:
это поддерживаемые границы, а не доказанно мёртвые ветви. Новых правил выбора
архивов, decoding, path validation, recovery consent или OS launch не введено.
Контролы, тексты и геометрия также не меняются.

Проверка Linux x64: 11 focused suites для сервиса и прямых consumers прошли,
163 Qt checks без failures/skips; run `20261101T191046Z-1468e9b8`.
Инкрементальная сборка заняла 41.023 с, тесты — 20.058 с. Другие платформы
в этой итерации не запускались: изменение общей передачи callbacks не меняет
платформенные реализации. Source comparison и runtime evidence имеют указанный
выше scope; нового визуального parity acceptance нет.

## Folder mutation callback owner — 2026-11-02

Compared original CThreadFolderOperations, its ProcessVirt/DoOperation and
CreateFolder/Delete/Rename callers at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Complete PanelOperations.cpp SHA256
matches the existing baseline. Reviewed the local comment consumer, progress
callback implementation, transport scope and worker completion/join boundary.
The current typed COM owner comes from the already recorded MyCom dependency;
no source import or provenance advancement.

The common worker now keeps its IProgress reference and concrete update callback
through one CMyComPtr2 instead of a separately maintained raw pointer/reference
pair. Allocation still occurs at DoOperation entry, before initialization,
password inheritance and transport creation. Interface dispatch, one retained
reference, joined worker lifetime, operation results, cancellation, diagnostics
and caller-owned selection publication are unchanged. The source algorithms and
the separate trash/transaction branches are preserved. Existing behavior suites
exercise these consumers; no representation-only test was added.

Linux `20261102T080659Z-d707aade`: source_delete, source_create,
panel_rename_state, rename_lifecycle and native_comment_ui passed (131 Qt
checks, no skips). Build 23.795 s, tests 25.505 s. Astra source review confirmed
equivalent lifetime/interface conversion. Removed five completed settings files;
incremental outputs and compiler cache retained. No secondary OS, full/slow
gate or new visual acceptance was run; platform implementations are unchanged.
