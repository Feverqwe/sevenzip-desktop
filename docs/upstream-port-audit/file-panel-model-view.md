# Файловая панель: Qt Model/View — 2026-10-24

Область: таблица, значковые виды и их прямые потребители в File Manager.
Ветка `feature/file-panel-model-view`, база Desktop `df10e42b`.

## Исходная реализация и граница адаптации

Источник — <https://github.com/ip7z/7zip.git>, baseline
`b88c6bc09339de32f7d18bbab900686b244f42ba` (26.05), записанный в
[общем manifest](../upstream-baseline.json). Полные оригинальные units
`CPP/7zip/UI/FileManager/{PanelItems.cpp,PanelListNotify.cpp,PanelSort.cpp,Panel.h,IFolder.h,FSFolder.cpp,FSFolder.h}`
сверены с сохранённым runtime snapshot 26.05; они совпадают с read-only
sibling checkout 26.04. До реализации рассмотрены RefreshListCtrl/InitColumns,
LVIF_PARAM и callback text publication, notify/custom-draw/property branches,
CompareItems/CompareItems2, исходные интерфейсы providers и локальные callers
выбора, навигации, columns, rename, drag и refresh.

Перечисление и загрузка остаются в исходных providers/Agent и CPanel.
`PanelItems`, `PanelListNotify` и `PanelSort` сохраняют прежние authoritative
адаптации: LoadListItems/RefreshListCtrl, GetItemText, CompareItemIndices,
OnCustomDraw, source marks и восстановление выбора. Новых внешних imports
или скопированных алгоритмов в этом изменении нет; исходный baseline,
hashes, notices и LGPL-2.1-or-later license не меняются.
Повторяемая проверка оригиналов и сравнение будущих ревизий выполняются
[существующим baseline tooling](../upstream-update.md).

`FilePanelModel` — локальная Qt-интеграция, а `FileTable` — QTableView.
Они заменяют QTableWidget/ArchiveTableItem presentation storage, не исходные
folder providers. На строку публикуются source index, необходимые пути/имя,
иконка и состояния представления. Текст свойств запрашивается и кэшируется
при отображении; числовые свойства читает тот же provider. Таблица и QListView
используют одну модель и один QItemSelectionModel. Layout notifications и
перенос persistent indexes сохраняют текущий элемент и выбор при сортировке.
Parent sentinel, grouping, raw-property comparison и source sort defaults
остаются прежними; их вычисления делегируются исходному panel owner.

Notification guards запрещают новые обращения к provider; уже отображённый
текст остаётся доступен. Смена языка помечает текст устаревшим до безопасного
следующего запроса. Модель хранит только необладающий identity pointer provider,
чтобы не продлевать жизнь archive streams после освобождения CPanel.
Публикация больших папок сохраняет существующее ограничение GUI batches.

Старые cell-based обращения тестов заменены чтением Qt model roles/indexes.
Отдельные QTableWidget fixtures для общего QAbstractItemView-контракта CPanel
сохранены. Проверка Qt model contract дополнена сохранением выбора/current и
persistent indexes после sort/column insert/row removal; она обнаруживает
перенос выбора на другой файл и невалидные model notifications.

При проверках восстановлены стандартные semantics Qt view для установки
current index: вызов без explicit selection flags учитывает SelectRows;
вызов с NoUpdate сохраняет отдельный focus. Существующие source_create,
panel_key и panel_shell обнаружили потерю полного row selection в первой
версии адаптера. Дополнительно исправлен существующий Qt startup gap:
общий read-only badge создаётся при первой панели, включая восстановленную
правую, а не только при создании панели 0. Crash trace старого бинарника и
нового scope указывали на один null QObject при refreshPanelStatus;
полная существующая panel_creation_path matrix после исправления проходит.
Этот общий badge — локальная UI-интеграция direct-device режима, без
соответствующего upstream Win32 control; алгоритмы providers не затронуты.

## C++23: время жизни ключей сортировки имён — 2026-10-28

Сопоставлены исходные `CFSFolder::CompareItems`, загрузка/очистка списка,
Rename/Clone и `CompareFileNames_ForFolderList` с их Qt-потребителями.
В санитайзерной сборке завершающая сортировка 4096 файлов блокировала GUI
на 281–284 мс: преобразования имени повторялись при каждом сравнении.

Локальный UTF-16 ключ теперь принадлежит загруженной записи `CDirItem`.
Он вычисляется лениво теми же `fs2us` и Qt-конвертерами, а новая запись
перечисления и `Clear`/`LoadItems` сбрасывают его вместе со снимком списка.
Чтение метаданных не меняет имя записи; исходный Rename обновляет файловую
систему, после чего потребитель перезагружает список. Ключ не хранится
в глобальном кеше и не заимствует память временной строки.

Исходный pointer comparator, правила цифр/регистра и остальные ветви свойств,
префикса, расширения и tie-break остаются прежними. Нового upstream-кода
не импортировано; исходные inventory hashes и лицензии не меняются.
Поведенческая проверка переименовывает Unicode/space-файлы, перезагружает
провайдер и проверяет новый порядок и неизменность данных. Существующий GUI
heartbeat сохраняет прежний лимит 250 мс.

Windows хранит исходные широкие имена как UTF-16. Его адаптер теперь передаёт
типизированные `wchar_t`-указатели непосредственно той же реализации сравнения
16-битных code units. Промежуточные Qt/`std::u16string` копии не создаются;
reinterpret cast не нужен. Для 32-битного `wchar_t` macOS/Linux прежнее
преобразование в Windows UTF-16 сохраняется. Нулевые wide-входы по-прежнему
считаются пустыми строками, как при прежнем Qt-преобразовании.
Цифровой и case-folding цикл
имеет одну реализацию, а compile-time проверка не допускает иной ширины units.

Приёмка этой адаптации, 2026-10-28: восемь suites сортировки, переименования,
перезагрузки и прямых оконных потребителей прошли на Linux, macOS и Windows.
После проверки nullable wide-входов повторно прошли `panel_sort` на трёх ОС,
а на Windows также запуск приложения и `large_list_window`.
Linux heartbeat отдельно прошёл три повторения. В финальном Windows замере
4096 элементов максимальная пауза GUI составила 31–77 мс для архива и 54 мс
для файловой системы; на macOS — 46–48 мс, в Linux диагностике — 44 мс
для файловой системы. Это измерения конкретных Debug-сборок с санитайзерами,
а не гарантия времени на любом оборудовании.

Актуальные установленные Release-сборки на всех трёх ОС прошли startup,
создание/проверку архива консольным sidecar, проверку/извлечение через GUI helper
и чтение fixture внешним кодеком с installed/system search paths.
Baseline verifier подтвердил 403 exact, 267 adapted и 23 local paths;
исходные inventory units не заменялись.

## Границы и проверка

Win32 ListView presentation заменён Qt model/view; исходные archive algorithms,
provider interfaces и операции не перенесены в модель. Новых пользовательских
команд, изменения структуры панели или намеренного изменения layout нет.
Это не полная виртуализация перечисления: provider по-прежнему загружает свои
items, а GUI публикует строковые metadata. Уменьшение памяти и скорости не
заявляется измеренным до отдельного замера.

macOS, Qt 6.12.0, 2026-10-24:

- Финальная сборка всех targets через `scripts/build.sh dev` — PASS.
  Использованы собственные incremental build/runtime trees worktree,
  установленный harness Qt kit и существующий compiler cache.
- Выбраны 69 suites панели и прямых consumers. После локальных исправлений
  подтверждены 66: первоначальная broad серия дала 62/68 PASS, затем
  `panel_creation_path`, `panel_key`, `source_create` и `panel_shell`
  перепроверены успешно. Follow-up batch из восьми suites — 8/8 PASS,
  затем `panel_shell` — 1/1 PASS. Полный исходный batch повторно не запускался.
- Три оставшиеся suites имеют воспроизведённые baseline failures:
  source-only `panel_navigation` — те же восемь ошибок в существующем
  основном бинарнике; `root_command::rootCommandClosesArchiveLinks` — оба
  состояния; `panel_activation` — представитель plain-Enter и представитель
  failed-folder recovery. Они не объявлены принятыми и не расширены в отдельный
  functional rewrite в рамках смены presentation model.
- Qt model contract/persistent selection, raw properties, timestamps,
  directory sizes, deleted-item colors, source icons, table/icon views,
  sort/column changes, rename, refresh, drag/drop, filesystem/archive copying
  и их прямые оконные consumers покрыты focused проверками.
- Проверены актуальные widget JSON/offscreen: 980×620 и 640×400,
  normal/enlarged font, table и icon views. Адрес, Parent, viewport и status
  полностью видимы без перекрытия. Это geometry evidence, а не rendered
  Windows 7zFM pixel comparison или physical input acceptance.

Локальные receipts: `build/model-final-build.log`,
`build/model-tests-unsandboxed.log`, `build/model-followup-tests.log`,
`build/model-status-tests.log` и отдельные baseline logs. Первые диагностические
запуски без task runtime и внутри sandbox остановлены; они не являются
приёмочными результатами. Последующая runtime preparation выполнилась штатно
из pinned upstream bundle с повторным использованием compiler cache.

Linux/Windows: selected 25-suite run `20261024T145533Z-d6889d6b`
собрал targets на обоих configured runners. Полные UI matrices упираются
в timeout; их acceptance не установлена. На Linux один из активных tests
наблюдался в D state с `jbd2_log_wait_commit`; это не устанавливает причину
всех timeout. Эта серия использует snapshot до финальной коррекции
row-selection convenience call, startup badge и старых layout assertions.
Остаток этой большой серии остановлен после повторных timeout; неизвестные
результаты не засчитаны как PASS.

Финальная адресная серия `20261024T154044Z-b63360d2` использует текущую модель,
исправленные row-selection/startup и layout assertions. Linux — 6/6 PASS,
Windows — 5/6 PASS: `panel_columns`, `panel_filesystem_sort`,
`panel_archive_sort`, `panel_refresh_notifications`, `ui_layout`.
Linux `panel_shell` также PASS; Windows native `panel_shell` достигает своего
30-секундного timeout и не принят. Пять успешных Windows suites и Linux
geometry/model checks используют Qt offscreen; это не native/pixel acceptance.
Проверки выполнялись через существующие source staging, branch locks, transport,
incremental build и CTest helpers. Состояние подключений сохранено в том же
harness runner registry; приватные настройки не копировались в worktree.

Slow suites, full/release gates, installed packages, rendered original Windows
reference и physical/native input acceptance не выполнялись. Source transfer
completeness для смены presentation storage подтверждена отдельно:
providers/Agent/CPanel algorithms и прежние omissions не изменены, новых
source-visible заглушек или не подключённых model consumers в выбранном
scope не осталось.

## Column customization bindings — 2026-10-31

Compared the complete baseline `PanelItems.cpp::InitColumns`,
`SaveListViewInfo`, `ShowColumnsContextMenu`, `PanelSort.cpp::OnColumnClick`,
their column types and current settings/header/menu/model consumers. The
recorded complete `PanelItems.cpp`, `PanelSort.cpp` and `Panel.h` hashes match
the repository-owned source archive at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Source catalog construction,
name-first ordering, sort rules and settings serialization remain unchanged.

Qt column binding construction and mutation now have one owner beside
`FileTable`, in `filetable_columns.cpp`. One record retains property ID, raw
status and both source indices, replacing four separately maintained vectors.
Initial catalog binding, Show/Hide, text/alignment lookup, sort dispatch and
settings projection use the same records. Duplicate-property identity matching,
the source-visible occurrence order, reserved Qt display slots and standalone
adapter fallback remain intact. Visible aliases are installed after matching
the complete catalog; removing a column uses the retained binding even after
the source has already removed its visible entry.

This is local Qt integration, recorded as such in the existing manifest and
linked through the existing window component. No external source was imported,
no original hashes advanced and no archive/provider algorithms moved into the
view. Window code retains layout, menu interaction and source settings calls.
Existing behavioral suites cover the selected workflow and standalone consumers;
their fixture setup now supplies properties through the adapter boundary.
No source-text assertions or duplicate implementation tests were added.

The initial Linux batch built the selected targets and passed 13 of 14 suites
(`20261031T034720Z-e0ac0e2f`, CTest 38.60 s). The existing column-menu regression
failed all four inactive-panel cancellation cases: popup teardown changed the
remembered focus from the peer address to a file list. Original
`PanelMenu.cpp::OnContextMenu` takes the header branch without a list command.
The Qt adapter now restores the captured caller focus after popup destruction
and panel projection unwind, instead of doing so inside both scopes. The
existing focus/owner/selection assertions are unchanged.

The four affected menu/schema/native suites passed Linux in
`20261031T035255Z-67e48e51`; the other ten suites had passed on unchanged code.
The two additional direct menu consumers, `panel_columns` and
`panel_column_sort`, passed again in `20261031T040325Z-8b98e523`.
All Linux batches reused the existing disk build and compiler cache with the existing
RAM scratch. Current normal/compact JSON reports were enabled for the affected
checks; no screenshots or rendered upstream comparison were requested.

macOS arm64 passed all fourteen suites together (CTest 49.74 s). Reviewed Linux
and macOS current-build JSON for native/schema and inactive-panel menu states
at 980×620 and 640×400: both address controls and tables are enabled and fully
visible. These are offscreen Qt geometry/behavior checks, separately from native
archive fixture execution; they do not establish rendered Windows pixel parity.

The first Windows Limited-desktop batch (`20261031T035407Z-dfcc3213`) passed
ten suites. `panel_column_changes`, `panel_cell_text` and `panel_column_sort`
reached their 60-second aggregate caps without a reported assertion failure;
their complete-matrix acceptance requires a diagnostic repeat. Rename passed
30 cases and failed two symbolic-link fixture creations under the Limited token;
those cases require the existing elevated runner mode. Product code and test
assertions were unchanged after the successful Linux/macOS checks.

The diagnostic `20261031T041422Z-7f141fd4` also ended at the three aggregate
300-second caps, with passing rows and no reported assertion failure. It
overlapped the physical runner migration: configuration alone took 1232.79 s
and the first test began 1574.47 s after invocation. These results do not
establish stable Windows deadlines or acceptance. A fresh run after migration
and the elevated symlink check remain necessary; further cap increases are
not justified by these timings.

After migration, Windows Limited desktop passed all three suites in
`20261031T052242Z-ff81068c`: column changes 21.89 s, cell text 10.64 s and
column sort 21.23 s, without assertion failures or skipped cases. The temporary
verbose output and 300-second caps were removed, restoring the original silent
mode and 60-second limits. No product code or assertions changed for the repeat.

The existing elevated desktop mode passed all 32 rename-state checks, including
both symbolic-link fixtures, in `20261031T053023Z-577a342b` (10.02 s).
Together with the first ten suites and the post-migration repeat, all fourteen
selected Windows suites are accepted. macOS/Linux results above remain valid;
no functional changes followed them. Small failure/acceptance receipts were
retained and completed fixtures cleaned their scopes. Build trees and compiler
caches were preserved throughout.

## Shared folder statistics traversal — 2026-11-01

Compared the complete original FSFolder statistics loop and the panel's F3
consumer with the current provider, property publication and redraw path.
Original FSFolder.cpp/.h and PanelItems.cpp/Panel.h match the recorded baseline
hashes at `b88c6bc09339de32f7d18bbab900686b244f42ba` in the source repository
above. No external code is imported or original inventory advanced.

Windows and POSIX now share the original counting/recursion body. Only retrieval
of the next native FileInfo remains platform-specific. Windows FileInfo and
POSIX directory-entry name buffers retain their former reuse. The duplicated
count and recursion body is removed. Existing symlink exclusion,
progress/cancellation, error/end handling
and successful statistics publication retain their prior behavior. In particular,
this refactor does not change the source's partial-enumeration policy or claim
that ignored enumeration errors establish complete totals.

No new UI state or background-operation owner is introduced. Existing provider
tests cover counts, cancellation before publication and repeated calculation;
the F3 view regression checks operated folders with a different focused file,
lazy property text, normal/compact layouts, retained selection and no reload.
No implementation-presence or trivial field test was added.

Linux passed `fs_folder` and `folder_size_view` (22 rows, no skips), then Windows
passed the affected `fs_folder` native provider suite (15 rows, no skips).
The final buffer-lifetime correction is POSIX-only; both Linux suites were
repeated after it, while Windows retained its passing evidence for unchanged
preprocessed code. No distinct macOS enumeration branch or Qt UI adaptation
changed, so no macOS run or repeat of Windows UI suites was selected. Selected
scratch directories were inspected pre/post; the completed view settings fixture
was removed after each Linux batch. Incremental builds and compiler caches were
retained with normal four-job defaults. No full/slow gate, screenshots, JSON
reports or new visual-parity acceptance is involved.
