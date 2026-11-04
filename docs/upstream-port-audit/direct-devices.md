# Прямое read-only чтение устройств

## Device picker result ownership — 2026-11-05

Compared complete native OpenAsArc bodies, callers, types and error paths in
PanelItemOpen.cpp at `b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git> with PanelArchiveLifecycle.cpp and the Qt device
command consumer. The complete original hash matches the existing baseline.
No source is imported or revision advanced; this picker is local Qt integration.

The command retains its initiating panel before the modal picker and activates
that panel only for an accepted selection. Native opening, error handling,
read-only stream leases, bounds checks, cancellation and limited-read consent
remain unchanged. Cancel preserves both locations and the restored focus.
The existing device UI suite now covers acceptance and cancellation from either
panel after modal close restores peer focus, including the peer's address draft,
selection and unchanged fixture bytes. It uses the recorded GPT image through a
memory-only read handle, without physical devices or authorization services.

Linux device_ui passed all 31 Qt checks without skips. Before the fix, both
accepted-selection cases failed while cancellation passed. The focused suite
also retains authorization retry, worker cancellation, read-only recovery,
copy/extract, refresh and parent-navigation coverage. Platform transports and
appearance are unchanged; no Windows/macOS, full or slow acceptance is claimed.

## Complete Windows device export owner — 2026-11-04

Rechecked original FSDrives.cpp/.h enumeration, device paths, image naming,
size queries, copy loop, callback decisions and operation refusals at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Both complete original hashes match the
baseline. Reviewed native panel copy dispatch, provider interfaces and existing
private-device command/callback tests. No source is imported or revision advanced.

The complete Windows IFolderOperations implementation, device-size query and
private copy buffer/loop now reside in FSDrivesCopy_win.cpp under the same
sevenzip_filesystem target, registered only for Windows. Drive enumeration,
properties, binding, icons, device-path construction and shared image naming
remain with the provider. Header, state and interfaces are unchanged.

Moved bodies and remaining provider bodies retain their bytes. Known/unknown
size accounting, direct-file/directory destinations, overwrite/skip decisions,
progress/cancellation, removable-media buffer policy and error handling remain
the original algorithms. Move and write/mutation operations retain their native
refusals. The retained private loop's writeToDisk branch has no new caller;
the only copy invocation still passes false. No source-device write capability,
disk authorization, decoder or parallel copy service is introduced. Existing
source extraction mappings and original hashes are preserved.

Windows fs_drives_device and fs_drives_device_ui passed: 15 Qt checks, no
failures or skips. Existing private read-only device fixtures cover direct and
directory image output, byte preservation, failure, Skip, cancellation and Move
refusal; the F5 consumer covers acceptance and Cancel in English/Russian.
The initial Limited-token invocation built successfully but failed fixture
preparation with ERROR_NOT_ALL_ASSIGNED (1300), before provider copying. The
same two suites passed with the documented --interactive --elevated runner
mode, without code or machine-policy changes. Session evidence confirms the
existing administrator token and normal priority. Incremental outputs and
compiler caches were retained; completed settings were removed and selected
scratch inspected. This Windows-only extraction does not change POSIX code;
no Linux/macOS, full/slow or new appearance acceptance is claimed.

## Volume record initialization — 2026-11-02

Reviewed volume enumeration, property publication, folder binding and native
device-size/copy consumers with the original FSDrives.cpp/.h at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Both complete original inventory
hashes match the recorded baseline. No external source is imported.

Numeric defaults now belong to the drive record instead of repeated assignments
in logical-drive, physical-drive and Qt mount enumeration. This also initializes
POSIX records whose provider cannot report total or available space before those
records are copied into the catalog. Existing availability flags still control
whether properties are published; unknown space is not presented as zero.
Native enumeration, removable-media policy, physical-drive copying and current
mount revalidation before folder binding are unchanged. No decoding, device
authorization, source-write or visual behavior changes are introduced.

Existing volume and root-navigation checks exercise public properties, opening,
disappearance, refresh and return navigation. No field-default test or pre-fix
runtime failure is claimed for this initialization/ownership change.

Focused volume_mount and root_navigation checks passed on Linux first (12 checks)
and Windows (nine checks), without skips. Windows exercises native logical-drive
enumeration and opening the drive containing a disposable fixture. The POSIX
fixture covers a mounted path becoming unavailable. Physical-device copying and
new macOS runtime evidence are not claimed. Whitespace and inventory checks passed;
temporary test settings were removed while incremental builds and compiler caches
were retained. Builds used configured four-job defaults; no full or slow gate ran.

## Fixed disks in Windows — 2026-10-24

Убран local transport filter, скрывавший non-removable SATA/NVMe disks без
hot-plug capability. Сравнены полные `CFSDrives::LoadItems`, device-path/CopyTo
consumers и headers в upstream <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`,
`CPP/7zip/UI/FileManager/FSDrives.{cpp,h}`: upstream physical-drive listing
не ограничен внешними интерфейсами. Новые тела upstream не копировались;
SetupAPI enumeration остаётся платформенной адаптацией существующего transport.
Проверены callers chooser и elevated reader: они используют один snapshot/open
путь. Проверки mounted volumes, identity, размера/сектора, повторной проверки
после открытия и read-only handle сохранены. macOS/Linux transport не менялся.
Реальный SATA-диск пользователя автоматикой не проверялся.
Focused Windows x64 build и `device_access_win`/`device_pipe_win` PASS,
receipt `20261024T051400Z-35b465ba`; incremental tree и ccache сохранены.
Эти проверки покрывают native read handle, stale selection/cancellation и
helper transport, но не обнаружение физического SATA-носителя. `git diff --check`
PASS. macOS/Linux не запускались: изменение ограничено Windows enumeration.

## Все поддерживаемые filesystem readers — 2026-10-23

Область расширения: существующий device transport теперь перебирает filesystem
handlers, реально зарегистрированные в выбранном codec catalog. Проверенный
набор динамического runtime: APFS, Ext, NTFS, exFAT (включая TexFAT), HFS,
EROFS, SquashFS, CramFS, Udf и Iso. FAT reader обслуживает FAT12/16/32,
HFS — classic/HFS+/HFSX, Ext — ext2/3/4. Это интеграция имеющихся readers,
а не добавление новых filesystem algorithms или обещание всех возможностей формата.
APFS входит в этот scope независимо от отсутствия Linux counterpart.

Источник SDK остаётся <https://github.com/ip7z/7zip.git>, полный commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; смешанные baselines сохраняются
в [manifest](../upstream-baseline.json). Для новой границы проверены
`CPP/7zip/UI/Common/OpenArchive.cpp` (`CArchiveLink::Open`, `Open2`,
`ParseOpenTypes`, `CArc::ReOpen`), source Agent opening и его Qt device consumer,
`CPP/7zip/Archive/ApmHandler.cpp` (`Open`, `Open2`, `CItem`, `GetProperty`)
и APFS stream boundary (`CHandler::Open`, `GetStream`, `CDatabase::GetStream2`,
registration). Существующие переносы остальных readers и их зависимости —
в [общем аудите форматов](filesystems-linux.md), без повторного inventory.

Новые upstream-тела не копировались. Неизменённый APFS reader поставляется
официальным runtime; GPL reader variants продолжают выбираться существующими
format modules. Открытие передаёт один ограниченный seekable stream штатному
handler, запрещает signature search со смещением и recursive descent, сохраняет
ошибки/cancellation и retained catalog. UDF проверяется перед ISO для hybrid media.
Обычный ZIP и другие архивы не становятся filesystem candidates.
Обработка GPT/MBR сохранена; подключён APM с его штатными byte offsets/sizes,
включая пропуск пустых Apple_Void записей. Служебные непустые разделы остаются
видны и выбираются явно. Новые проверки не разбирают superblocks в Qt.

Ext-specific feature gate применяется только к Ext. Общие reader-reported
`Read-only limitations` сохраняют запрос согласия и отдельный immutable
device lease; FAT/exFAT/NTFS не требуют несуществующих ext property descriptors.
Согласие не отключает reader checks и не меняет исходный lease. Сохраняются
native read-only handles, byte bounds, no replay/repair и mutation restrictions.
Qt resource сохраняет фактически обнаруженный формат при открытии и refresh.
Две ext-only подсказки заменены общими EN/RU текстами; структура chooser,
native buttons и роли предупреждения не меняются. Страница Apple Writing
потребовала JavaScript, поэтому HIG-ориентир — локальная выжимка от 2026-10-18;
новая сертификация соответствия Apple не заявляется.

Для MSVC восстановлен upstream unconditional `#include "StdAfx.h"` первым
include в адаптированном `OpenArchive.cpp`. Условие перед precompiled header
даёт C4654/C1020 при штатном `/Yu`; Qt использует уже имеющийся include path
к portable Common/StdAfx. Изменений алгоритма открытия в этой правке нет.
UI mock GPT приведён к 512-byte geometry самого sample вместо неверного 4Kn;
отдельные device backend regressions продолжают проверять реальный 4Kn input.

Новая focused suite читает точные payload bytes через aligned device stream
для каждого нового filesystem family, FAT12/16/32, classic/HFS+ и TexFAT,
на прямом томе и в MBR/APM. Проверяются выбор data partition, byte bounds,
disconnect, cancellation, dirty FAT/exFAT/NTFS opt-in и неизменность исходного
согласия. APFS sample создан нативно только из искусственных файлов; остальные
cases переиспользуют существующие fixtures/generators. Это не physical-device
проверка и не полный аудит всех APFS features. Расширенная приёмка Agent,
вложенных архивов и native сценариев учитывается отдельно от этой suite.

Проверки этой интеграции: `archive_device`, `device_filesystem`, `device_folder`,
`device_ui`, `language` PASS на macOS arm64/Qt 6.12, Linux x64/Qt 6.12 и
Windows x64/MSVC/Qt 6.12. Remote receipt `20261023T010718Z-bcde5114`:
пять suites на каждом госте; актуальный runtime уже собран в предыдущем запуске,
повтор использовал `--skip-runtime`. На macOS итоговые пять suites прошли за
56,93 с; после добавления clean/dirty exFAT Agent refresh/copy cases повторён
только `device_folder` (PASS, 11,61 с). Windows catalog loading требует ожидания
15 с вместо недостаточных 5 с; факт обнаружения/результат по-прежнему обязателен.
Общая local app также собрана. Incremental build trees и compiler caches сохранены.
Native Cocoa JSON chooser просмотрены: EN/RU, 620×390/420×320, device/denied/
partition states — видимые controls и labels помещаются. Гости используют
offscreen Qt; native Windows/Linux appearance, новые physical-device сценарии,
full и slow suites этой проверкой не подтверждены. Одноразовые raw APFS staging
и seed удалены; сжатый generated fixture сохранён для регрессии.

### Сквозная приёмка вложенных архивов

Дополнительная UI regression открывает штатно созданный nameless gzip из
exFAT с признаком некорректного отключения. EN Copy и RU Extract проверяют
точные распакованные bytes, Unicode/пробелы, selected/all extraction, refresh,
возврат через Parent, постоянный limited-access badge, запрет mutation commands
и неизменность source bytes. Отдельный Cancel оставляет исходную панель.
Тест использует существующий upstream `CPanel::OpenItemInArchive` путь
`IInArchiveGetStream`/`OpenAsArc_Msg` с сохранённым source resource;
сверена его stream/fallback ветка с тем же 7-Zip commit. Изменений production
code для этого сценария не потребовалось; новых format algorithms нет.
Переиспользована существующая UI automation, без нового окна или layout.

Общие source-extraction suites покрывают owned streams и возврат к parent,
ошибки/отмену с очисткой, malformed archive, точные binary bytes, небезопасные
относительные/абсолютные цели ссылок и занятые symlink ancestors с сохранением
данных вне destination. Это проверка общей extraction boundary; filesystem
algorithms и path/name decoding остаются у ранее проверенных handlers.
У каждого нового filesystem family отдельно проверено чтение через device
stream в основной suite выше; полная UI matrix по всем вариантам форматов
не подменяется одним вложенным exFAT сценарием.

Итоговые `device_ui`, `source_extract_backend`, `source_extract_contract`,
`source_link_extract`, `extraction_options` и `application_starts` PASS на
macOS arm64, Linux x64 и Windows x64. Local запуск разделён на focused batches;
для fixture creators bundled `7zz` добавлен в PATH (первоначальные source suites
не доходили до проверки из-за отсутствующего executable). Remote receipt
`20261023T011354Z-7d6857b1`: 6/6 на каждом госте, приложение собрано в тех же
incremental worktrees. Native Cocoa JSON нового exFAT warning и badge EN/RU
при 980×620/640×400 просмотрены, clipping не обнаружен. Source integration
всех выбранных filesystem handlers закрыта; runtime evidence охватывает
перечисленные generated fixtures и workflows. Ограничения самих readers
сохраняются, новые recovery/repair algorithms не вводились. Физические носители,
системная авторизация и native Windows/Linux appearance остаются вне этой
автоматической приёмки; full/slow gates не запускались.

## Источник и границы

Источник: https://github.com/ip7z/7zip.git,
ревизия `9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Форматы остаются в официальном динамическом runtime; новый парсер не добавляется.

Просмотрены upstream `CPP/7zip/Archive/GptHandler.cpp` (`CHandler::Open2`,
`Open`, `GetProperty`), `MbrHandler.cpp` (`ReadTables`, `Open`, `GetProperty`),
`ExtHandler.cpp` (`Open2`, `GetStream_Node`, `Extract`, свойства и флаги),
а также `CPP/7zip/Common/LimitedStreams.{h,cpp}` (`CLimitedInStream`,
`CreateLimitedInStream`) и file/stream opening в `UI/Common/OpenArchive`.
Зависимости: официальный codec catalog, COM stream/handler interfaces,
upstream stream bounds, callback progress/cancellation и retained module lease.

`ArchiveDevice` — локальная интеграция между разрешённым OS-потоком и этими
модулями. Она передаёт проверенный размер, предлагает разделы из GPT/MBR/APM,
проверяет их попадание в границы устройства и открывает выбранный раздел
штатным обработчиком поддерживаемой файловой системы. Автоматический recursive open отключён, чтобы
пользователь мог выбрать раздел. Синтетические записи свободного места MBR
исключаются по отсутствию FileSystem, которое upstream выдаёт только для
реальных разделов. Источник без таблицы разделов передаётся filesystem readers целиком.

Agent и QtArchiveFileResource принимают отдельный прямой stream transport;
обычные архивы продолжают использовать прежний file-only transport. Native I/O
и разрешения перечислены ниже; они сами по себе не означают готовность UX.

## Текущее состояние

Реализована экспериментальная [первая версия](../archive/plans/direct-device-reading-plan.md).
Подключены native access macOS/Linux/Windows, stream-level partition discovery,
filesystem readers и пользовательский выбор устройства/раздела. Просмотр и копирование
проверены на частных образцах. Автоматические проверки не используют реальные
носители и системные запросы прав. Позднее пользователь сообщил об успешном
получении доступа и чтении свойств реального раздела на macOS после включения
Full Disk Access; копирование его данных в новом режиме ещё не подтверждено.
Инструкция и ограничения — в [руководстве](../direct-device-reading.md).

## Agent и прямой поток

Сравнены полные тела и callers `CPP/7zip/UI/Agent/Agent.cpp`
(`Open`, `CanUpdate`, `ReOpen`, `ReadItems`), `ArchiveFolderOpen.cpp`
(`OpenFolderFile`), `ArchiveFolder.cpp` (`CopyTo`, `Extract`),
`ArchiveFolderOut.cpp` (`CommonUpdateOperation`) и
`CPP/7zip/UI/Common/OpenArchive.cpp` (`CArc::ReOpen`, `CArchiveLink::ReOpen`).
Ревизия источника — указанная выше; существующий смешанный baseline адаптированных
Agent-модулей сохраняется, новые upstream-тела не копируются.

Штатные Agent proxy/tree, выбор файлов, извлечение и пути назначения сохраняются.
Qt-граница распознаёт authorized device stream до spool/CLI listing и строит
property transport над тем же сохранённым native handler. Индексы не сопоставляются
по именам: метаданные и извлечение принадлежат одному обработчику. Размер и
read-only состояние берутся из потока и reader, виртуальный display path не
проверяется как обычный файл. При refresh переоткрывается разрешённый поток,
сохраняются исходные retirement/binding proxy и восстановление подпапки.
Признак устройства подключён к существующему `CanUpdate`; команды записи
отклоняются до создания выходного файла. Источник освобождается вместе с lease.

Upstream Ext публикует feature descriptions, но не применяет продуктовую политику
этой версии для неподдерживаемых возможностей. Direct-device open проверяет эти
публичные свойства до публикации reader и отклоняет неизвестные возможности,
шифрование и неподдерживаемые варианты размещения данных. Требование journal
recovery допускает явное чтение без replay после предупреждения о несогласованности;
по умолчанию такой reader не публикуется. Собственный superblock parser не добавляется; обычное открытие `.img`
не получает это ограничение. Список проверяемых возможностей остаётся в коде.

Актуализация ext по [Linux reference](ext-linux.md) подключает отдельный Ext
format-модуль через штатное обнаружение движка. Direct-device open принимает
реализованные современные metadata/inline layouts; encrypted content по-прежнему
не публикуется как готовый reader. Явный режим игнорирования журнала не выполняет
journal recovery и не изменяет файловую систему. Новые creator fixtures
проверяют извлечение точных байтов из inline, BIGALLOC и META_BG через
авторизованный поток. Следующая связанная итерация подключает fs-verity data
verification и проверяет её тем же device-потоком. Ключи fscrypt и trust policy
остаются в общем scope актуализации ext; разрешение layouts не реализует их.

## Выбор устройства и панель

Перед подключением сравнены upstream `CPP/7zip/UI/FileManager/PanelItemOpen.cpp`
(`OpenAsArc`, stream callers), `PanelFolderChange.cpp` (parent links, bind/close),
`PanelMenu.cpp` (read-only state) и File-menu `resource.rc`/handlers.
Source `OpenAsArc` принимает выбранный поток; его folder links, worker/modal
opening, Parent, выбор и существующий CopyTo остаются владельцами поведения.
Отдельного файлового браузера или второй модели навигации нет.

Оригинальный Windows device namespace не даёт переносимого выбора внешних
носителей с native authorization. Qt добавляет File → Open device, сохраняя
обычный Open archive и список томов. Диалог перечисляет внешние устройства,
блокирует busy rows, затем предлагает разделы или открывает единственный.
Native access и backend inspection выполняются owned worker; UI отменяет запрос
и ждёт завершения, не публикуя поздний источник. Задание и QThread принадлежат
одному owner: он снимается с активного состояния и joins поток до публикации
результата, повторного открытия или уничтожения диалога. Shared job сохраняет
время жизни native cancellation predicate. Acquisition lease освобождается до
разблокировки controls; успешный source сохраняет нужный handle для partition
streams. Back освобождает источник до следующего exclusive open.
Проверка устройства компилируется вместе с reader; лёгким потребителям
метаданных не требуется весь process backend.

Упрощение worker ownership от 2026-11-01 не меняет native access, reader,
fscrypt или layout. Просмотрены chooser, его command/backend/stream consumers
и Linux authorization cancellation; это локальная Qt integration без upstream
алгоритма для переноса. `device_input`, `archive_device`, `device_ui` и
`fscrypt_key_ui` PASS на Linux: 96 проверок, без skips. Отмена при уничтожении
диалога дополняет существующие reject, Back/exclusive reopen и partition tests.
Проверены controlled streams/authorization, не реальные устройства или системный
запрос прав; native appearance и другие платформы этим запуском не проверялись.

Для direct input главное окно использует native extract/test и refresh вместо
file-only listing prerequisites; извлечение всех файлов не переоткрывает display
path как файл. Read-only badge остаётся видимым в статусной строке, включая
просмотр вложенного архива; mutation availability учитывает source parent chain.
Во время незавершённого перечисления текущей панели команда открытия устройства
недоступна, чтобы не менять provider под его асинхронной проекцией.

EN/RU используют Qt Linguist; native access diagnostics также переведены.
Компоновка новая, поэтому rendered Windows 7zFM reference для неё отсутствует.
На macOS используются QDialog window modality и стандартные Qt/Cocoa роли кнопок;
AppKit sheet и pixel parity не заявляются. Контракт —
[direct device selection](../ui-layout-contracts/extraction.md#direct-device-selection).

## Native access macOS

Локальный platform adapter перечисляет внешние whole-media через IOKit и
Disk Arbitration. Upstream Windows FSDrives не предоставляет переносимой
реализации этого сервиса; POSIX device-size snippets в FileIO не активны.
Системное открытие и проверка native device identity/size — необходимая
платформенная граница, а не новая реализация файловой системы.

Устройство повторно сверяется с текущим inventory до и после открытия;
проверяются размер, размер сектора, тип дескриптора и отсутствие смонтированных
томов. Не выполняется принудительное размонтирование. Адаптер не создаёт
согласованный снимок носителя, который изменяет другая программа.

Пользователь подтвердил 2026-10-19: Developer ID отсутствует, функция должна
работать без него. Поэтому постоянный SMAppService/XPC-вариант исключён до
интеграции: Apple указывает проблемы ad hoc signing для такого сервиса.
Используется одноразовый helper через AppleScript administrator privileges,
который Apple рекомендует для ad hoc решений с разовым повышением прав:
[обсуждение signing](https://developer.apple.com/forums/thread/799910),
[варианты privilege escalation](https://developer.apple.com/forums/tags/servicemanagement/?sortBy=oldest&sortOrder=DESC).

Helper принимает выбор устройства, проверяет его самостоятельно и передаёт
O_RDONLY fd через частный Unix socket с проверкой UID сторон. Парсер ФС и
запись извлечённых файлов остаются в непривилегированном процессе.
Нет постоянной службы, изменения автозапуска, Authorization DB или требования
Apple-issued сертификата. Исходный язык native adapters — C++/Objective-C++
для доступа к системным framework; язык заимствованных модулей не меняется.

Новый поток адаптирует произвольные чтения к размеру сектора и использует
original CLimitedInStream для логических границ/seek. У каждого раздела
собственный курсор поверх readAt, чтобы чередование чтений двух открытых
разделов не нарушало cached physical cursor исходного limited stream.
Дескриптор остаётся жив, пока существуют его потоки. QtCore helper включается
в .app и deploy tool обрабатывает его loader paths; штатная упаковка подписывает
его ad hoc вместе с остальным пакетом.

## Native access Linux

Локальный адаптер использует системный UDisks2 ObjectManager для выбора внешних
whole devices; уже существующий upstream FSDrives этой службы не использует.
Отдельного Linux root-helper и разбора форматов в привилегированном процессе нет.
Размер логического сектора читается из kernel sysfs, открытие выполняется
через [Block.OpenDevice](https://storaged.org/doc/udisks2-api/latest/gdbus-org.freedesktop.UDisks2.Block.html)
в режиме `r` с exclusive-open и close-on-exec. Системную авторизацию обслуживает
polkit; noninteractive probe не разрешает показывать запрос прав.

Идентичность объединяет Block/Drive identifiers, device number и времена
обнаружения носителя из [Drive API](https://storaged.org/doc/udisks2-api/latest/gdbus-org.freedesktop.UDisks2.Drive.html).
После получения fd проверяются native block-device type, read-only access mode,
device number, размер и сектор; затем повторно сверяется inventory. Отдельные
разделы, внутренние диски, смонтированные тома и активные kernel holders не
предлагаются как доступный источник прямого чтения. Отмена ожидания не публикует
поздний дескриптор; поздний ответ принадлежит QtDBus и освобождается им.
Системный диалог polkit имеет собственное действие отмены.

Этот путь требует доступного UDisks2 и системного агента авторизации. Отсутствие
службы или отказ policy возвращаются вызывающему коду. Размонтирование, изменение
прав/политик и fallback к root-запуску GUI не выполняются.

## Native access Windows

Просмотрены upstream `CPP/7zip/UI/FileManager/FSDrives.cpp` (inventory,
`BindToFolderSpec`, `CopyTo`), `CPP/Windows/FileIO.{h,cpp}` (`CFileBase::Create`,
`CInFile::Open`, `CalcDeviceSize`, `ReadPart`) и
`CPP/7zip/Common/FileStreams.{h,cpp}` (`CInFileStream::Read`). Их path-based
открытие не предоставляет service boundary для уже разрешённого native handle
или ограниченной elevated session. Локальная граница использует WinAPI,
upstream `CPP/Windows/Handle.h` / `NWindows::CHandle` и общий sector-aligned
device stream. Форматные обработчики и файловый менеджер не копируются в helper.
Новых заимствованных тел в этой итерации нет; существующая baseline выше
остаётся источником переиспользуемых заголовков.

SetupDi и storage IOCTL предоставляют внешние whole disks, размер, сектор,
PnP identity/arrival и счётчик смены съёмного носителя. Смонтированные Windows
тома сопоставляются с physical disks через volume extents; неизвестное состояние
сопоставления запрещает открытие. До и после read-only открытия повторяются
проверки выбора; разделы не размонтируются.

Если обычному процессу не хватает прав, `ShellExecuteEx` с `runas` запускает
одноразовый `SevenzipDeviceReader.exe`. Частный локальный named pipe имеет ACL
текущего пользователя/администраторов/SYSTEM; стороны проверяют kernel peer PID.
Helper самостоятельно проверяет выбранное устройство, открывает его для чтения
и обслуживает только ограниченные выровненные запросы чтения. При закрытии
источника завершается elevated session. В helper нет парсеров архивов/ФС,
назначения извлечения или операций записи. GUI не получает повышение прав.
Helper устанавливается рядом с основным Windows executable.

## Проверки

Образцы в `tests/fixtures/devices` создаются штатными `mke2fs`/`sfdisk` как
обычные файлы, без монтирования. Образцы включают ext4 без таблицы, MBR-раздел
и GPT с несколькими разделами. Suite `archive_device` проверяет выбор раздела,
содержимое Unicode-файла, отказ неподдерживаемого раздела, отмену, ошибку чтения,
время жизни потока и соблюдение границ разрешённого источника.

Нативный доступ к устройствам, права, упаковка помощника и UI этой проверкой
не подтверждаются.

2026-10-19, первая итерация: `archive_device` и `native_archive_opening` PASS
на macOS arm64, Linux x64 и Windows x64. На macOS дополнительно повторён
`archive_device` после ограничения поиска началом потока. Все проверки использовали
существующие incremental build trees и compiler caches. Физические устройства,
системные запросы прав и UI в этой итерации не запускались.

2026-10-19, native access iteration: `archive_device`, `device_input`,
`native_archive_opening` PASS на macOS arm64, Linux x64 и Windows x64;
`device_access` PASS на macOS (O_RDONLY fd read/lifetime, отказ writable fd,
отмена и поддельный/stale выбор без elevation). macOS app/helper собраны;
private CMake install, ad hoc codesign verification и запуск установленного
helper без прав проверены. Helper использует QtCore из установленной .app.
Системный диалог авторизации, successful root fd handoff и реальный raw disk
не проверены; соответствие user workflow этим не установлено.

2026-10-19, Linux native access iteration: `udisks_device`, `device_access`,
`device_input`, `archive_device` PASS на настроенном Linux x64 runner.
Протокольная проверка использует частную D-Bus-шину, а не пользовательскую
system/session bus: внешние/внутренние устройства, partitions, busy/mounted,
смена носителя, policy denial/dismissal, отмена preflight без последующего
запроса прав, отмена pending authorization и отказ
regular-file fd вместо block-device. Kernel sysfs используется только как
источник метаданных сектора; содержимое физических устройств не читается.
Нативный successful OpenDevice/block-fd и реальное окно polkit ещё не проверены.

2026-10-19, Windows native access iteration: helper собран на Windows x64;
`device_access_win`, `device_pipe_win`, `device_input`, `archive_device`,
`native_archive_opening` PASS. После последнего уточнения mounted-state и
cleanup pending I/O повторены два native suites. Проверены Unicode-file read,
параллельные независимые readAt, освобождение handle, отказ write-only handle,
stale selection, отмена до elevation, обмен через частный named pipe,
ошибка/разрыв/неверный размер ответа и отмена pending read. Используются только
обычные временные файлы и pipe; виртуальные диски не создаются и не подключаются.
Реальный raw disk, UAC, privileged peer exchange и установленный Windows package
ещё не проверены. UI остаётся следующим интеграционным компонентом.

2026-10-19, stream-to-Agent integration: `archive_device`, `device_folder`,
`agent_opening`, `agent_lifecycle`, `native_folder_opening` PASS на macOS arm64,
Linux x64 и Windows x64. `device_folder` открывает частный GPT/ext4 fixture
через авторизованный stream, переходит в Unicode-папку и копирует выбранный
файл штатным `IFolderOperations::CopyTo`. Проверяются refresh с восстановлением
папки и retirement старого proxy, отмена/потеря источника при копировании,
отказ удаления, освобождение native lease, отсутствие CLI listing и полного
считывания раздела. Дополнительно проверены отмена/потеря источника при открытии
с успешным повторным открытием. Synthetic feature declarations проверяют отказ
прямого чтения при сохранении исходного поведения обычного образа.
На macOS первичный native-folder запуск использовал несовпадающий system `7zz`;
повтор с подготовленным pinned runtime PASS. Первичный device-folder test выявил
неподключённый device flag в Agent, исправление проверено тем же сценарием.
Отображение UI, нативная авторизация и физические устройства этой итерацией
не подтверждаются; явные ext2/ext3 fixtures ещё не проверены.

2026-10-19, UI integration: одиннадцать focused suites (`device_ui`,
`device_folder`, `archive_device`, `app_copy_routing`, `file_menu_policy`,
`ui_geometry`, `menu_policy`, `archive_metadata_ui`, `language`,
`language_main_dialog`, `language_standard_buttons`) PASS на macOS arm64,
Linux x64 и Windows x64. После локальных уточнений повторены UI/command-policy
consumers, затем финальный `device_ui` на трёх ОС. macOS app собрана.
Проверка выявила и устранила лишнюю зависимость лёгкого menu consumer от
process backend; source inspection factory выделена в reader module.

`device_ui` проходит от пункта меню через выбор GPT-раздела к текущей панели:
Unicode folder/file, выбранное и полное копирование, readonly availability,
refresh и Parent с возвратом в исходный каталог. Отдельно проверяются пустой
список/Refresh, отказ в правах и retry, Escape во время pending authorization,
Return на выбранном разделе и освобождение exclusive source после Back.
Настройки и содержимое — частные временные fixtures; реальная авторизация и
доступ к пользовательским устройствам не вызываются.

Просмотрены текущие normal/compact EN/RU widget JSON chooser (devices/partitions)
и главной панели с read-only badge: группировка, размеры, footer, clipping и
enabled state. macOS использует native Cocoa; sandbox WindowServer refusal
перепроверен вне sandbox, PASS. Linux/Windows используют offscreen Qt: rendered
native appearance, физическая клавиатура/VoiceOver и системные запросы прав
этими проверками не подтверждаются. Screenshots не требовались.

2026-10-19, итоговая проверка образцов и границ: добавлены обычные ext2/ext3
fixtures, созданные `mke2fs` без монтирования; ext3 содержит чистый journal.
`archive_device` проверяет их Unicode extraction, отмену и ошибку чтения вместе
с ext4/MBR/GPT. После последних исправлений `device_input`, `archive_device`,
`device_folder`, `device_ui` PASS на macOS arm64, Linux x64 и Windows x64.
Runner receipt: `20261019T044047Z-43c8e0a0`; локальный UI использует Cocoa.

Повторно сравнены upstream `CLimitedInStream::Read/Seek`, создание ограниченного
потока и GPT sector-size branches; исходные тела не менялись. Платформенная
граница дополнительно отказывает разделам с невыровненными к native sector
границами: логическое ограничение само не предотвращает физическое чтение
соседнего сектора. Второе исправление учитывает начальный sector prefix в лимите
native transfer. Регрессия большого невыровненного чтения воспроизвела отказ
до исправления и затем прошла; отдельный сценарий проверяет отказ неверных
границ до native I/O. Форматные алгоритмы не добавлялись и не изменялись.

Частные install-only пакеты macOS/Windows собраны штатным `package.py` на
`a10ef00`; helper не менялся в последующем исправлении потоков. macOS прошёл
ad hoc `codesign --verify --deep --strict`; установленный helper каждой ОС
запускается с системным PATH без Qt environment и ожидаемо отклоняет запуск
без аргументов. Это проверка loader closure, а не доступа с повышенными правами.
Windows runtime targets также собраны. Первичный link shell extension выдал
LNK1163; идентичный повтор и затем весь выбранный build прошли без удаления
objects, изменения flags или очистки compiler cache. Временные install payloads,
генерация fixtures и разобранный диагностический dump удалены; incremental
builds и caches сохранены.

Покрыты наблюдаемые сценарии выбора, просмотра, копирования, refresh/Parent,
readonly state, границ потока и отмены/ошибок с возможностью повторить операцию.
Обычные архивные consumers проверены в предыдущих focused итерациях. Реальные
raw-device I/O, успешная privileged session/fd передача с системной авторизацией,
UAC/polkit/AppleScript prompts и rendered native Windows/Linux UI не подтверждены.
Новые виртуальные диски не создавались и не подключались; slow suites и полная
release-приёмка в scope этой проверки не входили.

2026-10-19, отказ macOS после пароля: пользователь подтвердил запуск из Finder,
`Operation not permitted` после авторизации и успешное получение доступа после
включения Full Disk Access с последующим повторным вводом пароля. Это ручное
свидетельство пользователя об открытии устройства; чтение/копирование содержимого
им ещё не подтверждено. Пароль администратора и privacy permission независимы.
Helper EPERM/EACCES теперь объясняется в EN/RU с инструкцией включить доступ и
перезапустить приложение. Протокол, привилегии и форматные обработчики не менялись.
При проверке длинного русского сообщения найдена и исправлена коллизия со списком
в компактной геометрии. Native macOS `device_access`, `language`, `device_ui`
PASS; после изменения геометрии повторён `device_ui`. Linux/Windows `device_ui`
и `language` PASS (`20261019T162540Z-b0027ef6`). EN/RU JSON normal/compact
проверяют размещение отказа и успешную повторную попытку; native rendered
Windows/Linux не проверялись. Предыдущие записи о непроверенной авторизации
описывают границы автоматических тестов первоначальной версии.

2026-10-19, подробная причина отказа Ext: после предоставления прав пользователь
получил общий unsupported-feature отказ; обновлённая диагностика затем подтвердила
`RECOVER` на его разделе. Успешный просмотр/копирование этого раздела не заявляется:
текущая политика требует предварительного journal recovery в его Linux-системе.
Проверка возможностей сохраняет прежние ограничения, но передаёт отклонённые
имена и пояснения для journal recovery/encryption через Qt integration до окна
ошибки. Проверены текущие Ext property descriptors и полный source путь
`CArchiveFolderManager::OpenFolderFile`, `CPanel::OpenAsArc`,
`CFfpOpen::OpenFileFolderPlugin`, `COpenArchiveCallback` в upstream
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` и адаптации. Новые upstream-тела
не копировались. Необходимая Qt-адаптация — per-opening diagnostic в callback,
читаемый после завершения source worker: upstream HRESULT не содержит названий
возможностей локальной direct-device policy. Общий алгоритм открытия не менялся.

Backend regression проверяет сохранение `RECOVER`/`ENCRYPT` при отказе без
публикации reader; UI regression проходит весь путь меню → GPT-раздел → EN/RU
окно с конкретным `RECOVER` и сохраняет доступность повторного открытия.
macOS `archive_device`, `device_folder`, `device_ui`, `language`,
`native_archive_opening`, `native_folder_opening`, `agent_opening` PASS.
Первоначально добавленный compression sample отклонялся самим Ext reader до
проверяемой policy; эта неподходящая строка удалена, существующие recovery и
encryption сценарии сохранены. Private install-only macOS package собран и
прошёл deep/strict ad hoc signature verification и native startup smoke с
временными настройками; payload сохранён для пользователя в
`build/dev/device-access-fix/Sevenzip.app`. Это диагностическая сборка, без
автоматического доступа к пользовательскому устройству или записи на него.

Linux/Windows те же семь focused suites PASS по совокупности initial consumer
run `20261019T163024Z-8530c576` (шесть consumers PASS, указанная неподходящая
compression строка FAIL) и итогового `archive_device`
`20261019T163300Z-29f3390c` PASS. После добавления контроля видимости пояснения
повторён `device_ui` на всех трёх ОС; гости `20261019T163333Z-46a6833e` PASS.
Текущие native Cocoa EN/RU JSON окна RECOVER проверены: текст и кнопки помещаются;
гости используют offscreen Qt. Политика отказа не ослаблена; конкретная причина
на пользовательском диске установлена его повторным запуском, без автоматического
чтения содержимого диска агентом.

2026-10-19, чтение без journal replay по явному запросу пользователя:
отдельный customer HRESULT обозначает случай, когда единственная причина отказа
policy — `RECOVER`. Qt-команда предлагает Warning с Cancel по умолчанию и
действием Open read-only. После принятия новый immutable input lease сохраняет
native read handle, выбранные физические границы и собственный курсор, разрешая
только чтение без replay. Исходный lease и другие открытия не меняются. Режим
сохраняется в retained stream при refresh; badge учитывает текущий source и
parent chain, остаётся связанным с переводами. Шифрование, неизвестные flags,
unsupported layouts и проверки целостности reader не отключаются.

Сравнены `ExtHandler::Open2`, properties/Extract/GetStream, platform stream и
`CLimitedInStream`, source `CPanel::OpenAsArc` и worker/callback lifetime в
указанном выше 7-Zip baseline; текущий Ext-модуль с Linux read helpers описан
в [ext audit](ext-linux.md). Новых заимствованных тел нет. Форматные алгоритмы,
JBD2 recovery и native privileges не менялись. Upstream archive reader не
выполняет journal replay; локальная policy теперь разрешает явно выбранный
режим. Согласованность filesystem без replay не обещается — это соответствует
ограничениям Linux [`ro,noload`](https://cdn.kernel.org/doc/html/latest/admin-guide/ext4.html).
Это изменение продуктового отказа/сессии, а не перенос восстановления журнала.

Регрессии покрывают копирование Unicode-файла, отмену и disconnect через
RECOVER-marked ext3 sample; исходный default stream после opt-in по-прежнему
отклоняется. Проверены границы отдельного partition lease и независимость курсора,
повторное открытие/refresh через source Agent, отсутствие мутаций и unchanged
fixture bytes. EN/RU UI проверяет Cancel/Return/Escape, принятие, selected/all
copy, refresh/Parent и постоянный badge в normal/compact geometry. Encrypted
source, включая сочетание с RECOVER, не становится доступным при opt-in.
Образцы используют управляемые feature declarations; реальные незавершённые
journal transactions и пользовательский носитель автоматикой не проверялись.

Восемь focused suites (`device_input`, `archive_device`, `device_folder`,
`device_ui`, `language`, `app_copy_routing`, `file_menu_policy`, `menu_policy`)
PASS на macOS arm64, Linux x64 и Windows x64; гости
`20261019T164530Z-ad32b95a`. После расширения сочетания RECOVER/ENCRYPT и
проверки default Return/Escape повторены `archive_device`/`device_ui` на всех
трёх ОС; гости `20261019T164806Z-655226ba` PASS. Native Cocoa EN/RU JSON
подтверждения и badge normal/compact просмотрены: текст, кнопки и badge помещаются.
Linux/Windows UI — offscreen, native appearance не заявляется. macOS app собрана,
обновлена private install-only сборка `build/dev/device-access-fix/Sevenzip.app`,
deep/strict ad hoc signature и native startup smoke с временными настройками PASS.
Incremental builds/caches сохранены; временный install staging удалён упаковщиком,
payload оставлен пользователю. Реальные pending journal transactions,
копирование пользовательских файлов и full/slow acceptance не проверялись.

## Read-only filesystem access: readable feature limitations — 2026-10-21

Проверка новой политики AGENTS.md выявила product gate: после успешного Ext Open
`CheckFeatures` запрещал `ORPHAN_PRESENT`, `HAS_SNAPSHOT`, `REPLICA` и неизвестные
readonly-compatible bits, предлагая выбор только для `RECOVER`. Сравнены полные
selected bodies Linux `fs/ext4/super.c:ext4_feature_set_ok` и compatibility callers,
header feature masks, archive `ext_linux_open`, SDK feature properties и consumers
OpenDeviceFilesystem/Qt device command/stream leases/panel badge. Linux reference
`58161ead29e50d70a317ea26b472e0c7508e69da`, repository
<https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git>;
SDK `9128b80e3a471108678e2b3ec3c985861c8d7b0f`, repository
<https://github.com/ip7z/7zip.git>. Kernel readonly branch принимает эти RO bits;
incompatible features по-прежнему требуют реальной поддержки reader.

Local policy теперь сообщает конкретные условия ограниченного чтения и предлагает
явный opt-in для уже открываемой reader файловой системы. Включены RECOVER и
незнакомые boundary readonly-compatible declarations, в том числе ORPHAN_PRESENT.
Это не принудительное открытие неизвестного disk layout: checks reader,
checksums, bounds, path validation и per-file operation results сохраняются.
Отсутствующий decoder не заменяется согласием; отсутствие/неверный тип feature
metadata также не становится разрешением на обход проверки.

Согласие принадлежит отдельному immutable limited-read lease выбранного открытия;
первоначальный input остаётся прежним. Partition views наследуют этот mode в
своих границах. Локальные API имена обобщены с journal-only на incomplete-read,
без изменения source parsing или reader language. Qt warning EN/RU перечисляет
feature conditions, объясняет missing/outdated/unreadable files и отсутствие
journal replay/orphan cleanup/repair. Cancel остаётся default/escape; постоянный
badge обозначает limited access при навигации и refresh. Source OS handles
остаются O_RDONLY/FILE_GENERIC_READ, записи на устройство не добавлено.

Backend regressions используют controlled declarations с неизменным payload:
RECOVER/encrypt, ORPHAN_PRESENT отдельно и вместе с RECOVER, snapshot, replica и
неизвестный RO bit. Проверяются подтверждение, exact extraction, неизменность
образа и отсутствие разрешения в исходном opening; неизвестный incompatible bit
не открывается и после opt-in. EN/RU UI scenarios добавляют orphan accept/cancel
к существующим Copy/Extract/refresh/Parent, bounded leases и mutation restrictions.
Это проверка selected direct-access policy, а не утверждение, что все filesystem
handlers допускают любой повреждённый или частично поддерживаемый образ.

Focused `device_input`, `archive_device`, `device_folder`, `device_ui`, `language`
PASS на macOS arm64/Qt 6.12, Linux x64/Qt 6.12 и Windows x64/MSVC/Qt 6.12;
remote receipt `20261021T031533Z-a30a7adc` (8,55/24,94 с). Первый sandbox Cocoa
запуск прервался до окна (`no screens available`); affected `device_ui` повторён
вне sandbox и PASS (45,98 с), остальные четыре macOS suites уже прошли.
Native Cocoa EN/RU JSON confirms ORPHAN_PRESENT и RECOVER, labels/buttons и
limited-access badge просмотрены при 980×620 и 640×400: clipping не выявлен.
Linux/Windows UI — offscreen; native appearance, VoiceOver и реальный носитель
не проверялись. Использована локальная HIG-выжимка от 2026-10-18: официальная
Alerts page потребовала JavaScript, Markdown retrieval недоступен; новое HIG
соответствие по этому источнику не заявляется. Штатные Qt warning roles сохранены.
Приложение также собрано на трёх ОС, remote receipt
`20261021T031840Z-8e06c889`. Старые task-owned warning JSON заменены текущими;
build trees/caches сохранены, full/slow suites и installation не выполнялись.

## POSIX descriptor adoption — 2026-11-02

Reviewed the complete POSIX handle/adoption implementation, Linux snapshot,
OpenDevice/descriptor validation and publication, macOS direct/helper adoption
callers, device input/partition ownership and chooser worker exception handling.
The existing native-access adaptations above remain project integration.
Original FileIO.h CFileBase/CInFile ownership interfaces at
`b88c6bc09339de32f7d18bbab900686b244f42ba` were checked: they do not expose
adoption of this authorized descriptor. The complete original header hash
matches the recorded baseline. No external import or reader algorithm change.

Adoption now gives the descriptor a stack cleanup owner before validation and
shared-handle allocation, then transfers responsibility after successful
construction. This closes the allocation-failure gap in the existing ownership
contract, including a duplicated Linux exclusive-open descriptor and macOS
direct/helper descriptors. Read-only validation, close-on-exec, native identity
checks, cancellation and positional reads are unchanged. No exception is hidden
or new privileged operation introduced.

A standalone fixture arms one allocation failure only around public adoption
of an O_RDONLY /dev/null descriptor and checks that the exception propagates and
the descriptor is closed. It follows the existing isolated allocation-fixture
pattern; there are no production hooks or source-text assertions. Existing tests
retain normal read/close, writable-descriptor refusal, bounded streams and
private UDisks authorization/cancellation checks. No pre-fix run is claimed.

Linux primary `20261102T045407Z-e8fc1a52` passed device_access, device_input,
device_descriptor_allocation and udisks_device: 25 Qt checks plus the standalone
allocation fixture, no skips; build 10.849 s, tests 0.883 s. macOS then passed
the three applicable suites: 15 Qt checks plus the allocation fixture, no skips;
build 3.690 s, tests 0.790 s. Windows excludes the changed adapter and fixture.
These checks do not open physical user disks or establish new native elevation
acceptance. Selected scratch directories were empty before/after, incremental
outputs and compiler caches retained. No full/slow gate, reports or screenshots.
