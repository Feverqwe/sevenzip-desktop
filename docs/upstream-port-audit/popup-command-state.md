# Доступность команд popup и Drop — 2026-10-18

Источник: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Desktop baseline до исправления: `5adc274`.

Сопоставлены `UI/FileManager/PanelMenu.cpp::{CreateFileMenu,OnContextMenu}`,
`MyLoadMenu.cpp::{CFileMenu::Load,ExecuteFileCommand}` и `PanelDrag.cpp::CDropTarget::Drop`
с Qt popup projection, командной политикой, focus/notification guards и drop adapter.
Исходный 7zFM создаёт отдельное popup menu, получает выбранный ID через
`TPM_RETURNCMD | TPM_NONOTIFY` и лишь затем вызывает исходный dispatcher.
Drop сохраняет исходное подтверждение Copy/Move и ветвь отмены.

Qt popup раньше повторно использовал QAction главного меню. События фокуса
внутри `QMenu::exec` обновляли общую командную проекцию, пока уведомления панели
были временно отключены: Rename/Create File становились недоступными прямо
в открытом popup. Теперь popup владеет отдельными действиями, включая CRC
submenu, и сохраняет доступность выбранной панели при построении. Порядок,
подписи, иконки, сочетания клавиш и исходные command IDs сохраняются.
Общих triggered connections нет: выбранный ID передаётся существующему
dispatcher после восстановления source guards, с прежней повторной проверкой
доступности команды. Native shell/plugin rows сохраняют прежнего владельца.

После завершения Qt Drop заново проецируется командная доступность. Раньше
фокус модального подтверждения мог опубликовать disabled Copy при активном
`sourceArchiveProgress_`, а отмена снимала флаг без обновления действий.
Обновление выполняется после возврата исходного Drop и восстановления флага;
активная асинхронная операция по-прежнему блокирует команды своей state policy.

Это узкие Qt presentation/service adaptations: исходные меню, обработчики
команд, selection policy, подтверждения и алгоритм Drop не переписаны,
новых функциональных omissions нет. Нет upstream QAction-реализации,
которую можно было бы скопировать для этих Qt-boundary исправлений.

Существующие regressions сохраняют наблюдаемое поведение. Rename fixture
ожидает готовую строку файла и открывает меню именно на ней; empty/Parent
fixture ожидает окончание асинхронного чтения обеих панелей. Popup suites
проверяют показанные команды, а не совпадение QAction с главным меню или
QObject-parent ownership. Copy после Cancel проверяется прежним тестом.

## Проверки

macOS arm64 / Qt 6.12: исходные `ui_operations`, `ui_archive_states`,
`source_context_menu` и `shell_menu_window` прошли 4/4. До исправления popup
Rename по-прежнему падал и после ожидания готовности filesystem fixture;
Copy прошёл сразу после исправления Drop. В расширенном batch 16/18 PASS:
`panel_shell` ещё требовал идентичности QAction, а `shell_menu_window` получил
несогласованный старый CLI через PATH. После исправления popup assertions
и выбора подготовленного 26.04 runtime все пять затронутых suites прошли 5/5.
Таким образом, все 18 разных suites выбранного scope прошли в focused batches.
Само приложение `sevenzip_desktop` также собрано incremental.

Linux x64 / Qt 6.12: 18/18 PASS в batch `20261018T064835Z-0c073efd`.
Первая сборка была остановлена OOM-killer при примерно 1,3 ГиБ RAM гостя,
что подтверждено kernel journal; повтор с одним build job прошёл без изменения
compiler flags, build tree или cache. Временный wrapper после проверки удалён.

Windows x64 / Qt 6.12, elevated interactive desktop: 17/18 PASS в batch
`20261018T064700Z-55f3ece7`; единственный отказ — старая проверка QAction
в `panel_shell`, исправленная после materialization этого snapshot.
Повтор `panel_shell` на итоговом коде, batch `20261018T065055Z-87070c87`: PASS.
Три исходные регрессии прошли в первом Windows batch.

Дополнительный scope: command policy, window lifecycle, native drop publication,
drag protocol/archive/UI/Add, source Drop Add, refresh/drop completion и
main/panel/view menus. Это прямые consumers двух изменённых Qt adapters;
полный gate и slow suites не запускались.

Проверены текущие widget JSON: 32 context reports для двух панелей,
table/icons, EN/RU, normal/compact geometry и четырёх empty/Parent состояний.
Сохранены порядок, группы и видимость пунктов; Rename без operated item
отключён, Create File доступен в пустой обычной папке. Скриншоты не требовались;
rendered Windows 7zFM comparison и физический ввод не заявляются.

## Привязка команд контрольных сумм — 2026-11-01

Сверены исходный `ExecuteFileCommand`, CRC resource menu и текущая цепочка
Qt menu → binding → capability projection → dispatcher → CalculateCrc.
Полные MyLoadMenu.cpp/.h и resource.rc/.h совпадают с существующими baseline
hashes ревизии `b88c6bc09339de32f7d18bbab900686b244f42ba` того же repository.
Внешний код не импортирован, исходные hashes и revision не меняются.

Исходное соответствие command ID и имени hash method вынесено в общий lookup
того же menu owner. Native switch сохраняет все прежние command branches и
проверку доступности перед side effects; вычисление digest и выбор файлов не
меняются. Qt действия хранятся по native ID вместо позиционного массива.
Их создание, привязка и проверка провайдера используют тот же ID; parsing
`objectName` и параллельный список методов/индексов удалены. Порядок resource
menu, подписи, action names, локализация и public command IDs сохранены.

Каталог и provider interfaces сохраняют прежнее время жизни. Кэширования
capabilities или отказов загрузки не добавлено; navigation, selection, busy/
closed guards и повторная проверка перед выполнением остаются прежними.
Существующий dispatch regression теперь проверяет правильный typed вызов для
всех hash command IDs; runtime suites проверяют native digest выбранного файла,
ошибки/отмену, независимый 7zG, смену папки во время публикации и языков меню.

Linux PASS: `native_command_dispatch`, `command_policy_runtime`,
`command_publication`, `checksum`, `manager_hash_workflow`, `language_ui` —
44 проверки без skips. Сборка 59,826 с, тесты 4,075 с. Изменение общей привязки
не затрагивает native OS branches; macOS/Windows и visual parity этим запуском
не проверялись. Обычный параллелизм 4 и compiler cache сохранены. После terminal
run удалены пять settings fixtures; full/slow gate и UI reports не запускались.
