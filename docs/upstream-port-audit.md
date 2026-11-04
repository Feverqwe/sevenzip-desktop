# Происхождение перенесённого кода

Машиночитаемый [baseline](upstream-baseline.json) хранит исходный репозиторий,
полные ревизии и принадлежность заимствованных файлов. Проверка и обновление —
в [upstream update](upstream-update.md); требования к source port —
в [AGENTS.md](../AGENTS.md#source-port-requirements).

## Дополнительные форматы

[Зашифрованные DMG](upstream-port-audit/dmg-encryption.md) — seekable read-only
оболочка, пароль, исходный baseline и границы проверок.

[Apple sparseimage и sparsebundle](upstream-port-audit/sparse-images.md) — общие
native readers, отдельный token/bands input, пароль и ограниченное чтение повреждений.

[Read-only F2FS](upstream-port-audit/f2fs.md) — отдельный reader из GRUB,
современная геометрия inode по f2fs-tools, provenance и границы первой версии.

[Read-only XFS](upstream-port-audit/xfs.md) — GRUB C reader для образов
серверов/NAS, отдельный baseline, адаптации и границы проверок.

[Read-only Btrfs из Linux](upstream-port-audit/btrfs-linux.md) — baseline,
адаптация Linux read path, native/device-интеграция и границы проверок.

[Удаление Background](upstream-port-audit/progress-controls.md)
— запрошенное изменение controls/transport с сохранением Pause и Cancel.

[Выборочный перенос native readers](upstream-port-audit/container-layers.md)
— QCOW Zstandard/extended L2, границы потоков и удаление Background по запросу пользователя.

[Единые readers для приложения и 7z](upstream-port-audit/license-variants.md)
— удаление SDK-дублей, общие probes/dependencies и границы бинарных модулей.

[Проверка файловых систем по Linux](upstream-port-audit/filesystems-linux.md)
— текущий scope проверки существующих read handlers и добавления EROFS.

[Read-only Lzip](upstream-port-audit/lzip.md) — official lzlib C library,
native `.lz` and nested `.tar.lz` reading with a pinned source baseline.

[Read-only Brotli](upstream-port-audit/brotli.md) — Google C decoder,
адаптация format handler, границы распаковки `.br` и проверки.

[Read-only exFAT из Linux](upstream-port-audit/exfat-linux.md) — исходная ревизия,
границы C-переноса, адаптации потокового ввода и проверки отдельного модуля.

## Базовый аудит завершённого переноса

[Индекс Qt-портирования](archive/qt-port/upstream-port-audit.md) сохраняет
copied/adapted/omitted scope, зависимости, причины адаптаций и датированные
проверки. Архивирование фиксирует завершённую работу; эти сведения остаются
основанием для ревью изменений соответствующих модулей.

- [Решения по bodies/callers](omitted-scope-audit.md).
- [Закрытие исходного scope](archive/qt-port/upstream-port-audit/p3-05-source-closure-2026-10-17.md).
- [Последующая коррекция modal owner](archive/qt-port/upstream-port-audit/p3-03-owner-publication-2026-10-17.md).
- [Итоговая приёмка и границы свидетельств](ux-port-audit.md).

Искать старое обоснование по upstream-пути или символу:

```sh
rg -n --no-ignore -F 'CAgent::Open' docs/archive/qt-port/upstream-port-audit/ docs/archive/qt-port/omitted-scope-audit/
```

## Новые изменения

- [Хранение кешированных паролей](upstream-port-audit/archive-passwords.md)
  — защищённые allocations для открытых архивов и затираемые native adapters.

- [Снимок исходников для удаления после архивации](upstream-port-audit/archive-input-deletion.md)
  — цельное состояние до публикации и сохранение нативных проверок перед удалением.

- [Владелец согласия на ограниченное чтение](upstream-port-audit/limited-read-consent-owner.md)
  — сбор свойств и подтверждение открытия имеют отдельные обязанности.

- [Прямой запуск темы справки](upstream-port-audit/help-topic-owner.md)
  — Qt получает тему без промежуточного пути к CHM.

- [Владелец метаданных архива](upstream-port-audit/metadata-view-owner.md)
  — независимая привязка индексов каждого представления к нативному reader.

- [Планы Split/Combine](upstream-port-audit/volume-plan-owner.md)
  — типизированная передача исходных параметров от команды до worker.

- [Идентичность временных объектов при смене прав](upstream-port-audit/temp-identity-permissions.md)
  — Linux metadata handles, прежние записи и защита активных leases.
- [Неактивный черновик пароля Add](upstream-port-audit/inactive-password-coverage.md)
  — согласованы положительный сценарий и прежняя матрица ошибок.

- [Нативные тестовые сценарии](upstream-port-audit/native-validation-fixtures.md)
  — независимые операции, приватный MAPI, доступность и предпосылки проверок.

- [Общая подготовка метаданных Add](upstream-port-audit/add-metadata-owner.md)
  — параметры исходного запроса, возможности формата и сохранённые значения.

- [Общая проверка пути Add](upstream-port-audit/archive-path-validation.md)
  — Browse и подтверждение используют одинаковую начальную папку.

- [Раскрытие секций средствами доступности](upstream-port-audit/disclosure-expand-collapse.md)
  — общий путь активации и прямой Windows UIA Expand/Collapse.

- [Общий путь назначения Extract](upstream-port-audit/extract-destination-owner.md)
  — текущее значение, подтверждение и исходные правила истории.

- [Запрос выбора файла](upstream-port-audit/browse-request-owner.md)
  — локальное состояние picker и публикация пути/фильтра после подтверждения.

- [Представление ассоциаций после конфликта](upstream-port-audit/association-presentation-owner.md)
  — согласованное обновление обработчика и значка перед повторным Apply.

- [Тип относительной цели ссылки](upstream-port-audit/link-target-classification.md)
  — проверка относительно родителя ссылки с сохранением исходной цели.

- [Черновик рабочей папки](upstream-port-audit/work-directory-draft.md)
  — текущие значения в контролах и локальный запрос сохранения при Apply.

- [Общий текстовый отчёт](upstream-port-audit/text-report-owner.md)
  — режим чтения, шрифт и копирование полного текста либо выделения.

- [Единая граница callback-диалогов](upstream-port-audit/callback-dialog-owner.md)
  — готовность прогресса и показ пароля/ключей в GUI-потоке.

- [Владелец состояния представления панели](upstream-port-audit/panel-presentation-owner.md)
  — единое состояние каждой панели для команд и фоновых callbacks.

- [Жизненный цикл запроса backend](upstream-port-audit/backend-request-phases.md)
  — единое состояние запроса и защита другой выполняющейся операции.

- [Запуск и выбор внешнего Drop](upstream-port-audit/startup-drop-choice.md)
  — подготовка первого окна и запрошенный выбор открытия/упаковки;
  [аудит сценариев](archive/user-workflow-audit-2026-10-30.md) хранит датированные результаты и ограничения.

- [Build dependency overlays](upstream-port-audit/build-dependencies.md)
  — закреплённые vcpkg recipes и узкие исправления зависимостей сборки.

- [cp2 runner sync](upstream-port-audit/runner-sync.md)
  — закреплённая внешняя утилита, provenance, лицензии и процедура обновления.

- [Локальный read-only SMB](upstream-port-audit/readonly-smb.md)
  — исследовательский fork libsmb2, исходные blockers, native macOS probes и
  границы незавершённой reader/UI-интеграции.

- [Системные charset services](upstream-port-audit/isofs-linux.md#system-charset-services)
  — удаление Linux NLS tables, платформенные различия и сохранение FAT CP437.

- [Возврат upstream crypto и оценка библиотек](upstream-port-audit/crypto-backend-assessment.md)
  — отдельная сборка без OpenSSL, совместимость и границы выбора альтернатив.

- [Общий CRC64](upstream-port-audit/shared-crc64.md)
  — liblzma для SDK, native runtime и вложенных XZ-потоков.

- [Общие checksum/hash primitives](upstream-port-audit/shared-checksums.md)
  — XXHash, Adler32, CRC32/CRC32C и повторное использование SDK hash-кода в GPL readers;
  проверенные ограничения оставшихся замен записаны в том же аудите.

- [Файловая панель: Qt Model/View](upstream-port-audit/file-panel-model-view.md)
  — собственная модель над исходными folder providers и общий выбор видов.

- [Создание ZIP со Zstandard](upstream-port-audit/zip-zstd.md)
  — официальный libzstd encoder, узкий ZIP adapter и runtime/UI consumers.

- [ZIP: официальный компонент и независимые эталоны](upstream-port-audit/zip-refresh.md)
  — выбор основы и дополнительные проверки разрядности Shrink.

- [Полное обновление 7-Zip до 26.05](upstream-port-audit/upstream-26.05.md)
  — native engine/SFX, адаптации NTFS/container/progress и проверки платформ.

- [ZIP: обновление Shrink из 7-Zip 26.05](upstream-port-audit/zip-26.05.md)
  — исходные decoder units, точечная ревизия и проверки native/runtime consumers.

- [Политика read-only и частичного доступа](upstream-port-audit/read-only-access.md)
  — текущий аудит файловых систем и архивов, согласие, читаемые данные и результаты.

- [ELF по Linux](upstream-port-audit/elf-linux.md)
- [PE по Linux](upstream-port-audit/pe-linux.md)
- [EFI capsules по Linux](upstream-port-audit/uefi-linux.md)
- [Intel HEX по Linux](upstream-port-audit/ihex-linux.md)
  — ASCII records, адреса, IQS5xx map profile и handler lifecycle.
- [TAR по Linux](upstream-port-audit/tar-linux.md)
  — physical records, GNU/PAX/sparse, последовательное чтение и retained streams.
- [Legacy LZ4/lzop по Linux](upstream-port-audit/linux-frames.md)
- [Modern LZ4 Frame and shared official decoder](upstream-port-audit/lz4-frame.md)
  — block framing, checksum, точные consumed boundaries и CPIO consumers.
- [Base64 по Linux](upstream-port-audit/base64-linux.md)
  — standard/URL-safe/IMAP alphabets, canonical tails и handler lifecycle.
- [Zstandard по Linux](upstream-port-audit/zstd-linux.md)
  — frame discovery, parse metadata, Code/Read source failures и cancellation.
- [XZ по Linux](upstream-port-audit/xz-linux.md)
  — stream padding, независимые seekable readers, source failures и cancellation.
- [Bzip2 по Linux](upstream-port-audit/bzip2-linux.md)
  — end padding, integrity, progress/cancellation и multiblock consumers.
- [LZMA по Linux](upstream-port-audit/lzma-linux.md)
  — dictionary/size fields, member boundaries и decoder source failures.
- [Gzip по Linux](upstream-port-audit/gzip-linux.md)
  — recognition, длинные headers, progress/cancellation и завершённое открытие.
- [CPIO по Linux](upstream-port-audit/cpio-linux.md)
  — raw newc/crc members, hard links, integrity и независимые entry streams.
- [MBR primary/EBR по Linux](upstream-port-audit/mbr-linux.md)
  — logical chains, 64-bit geometry, EOD bounds и direct-device sector property.
- [APM по Linux](upstream-port-audit/apm-linux.md)
  — block geometry, reserved fields, map count и завершённое открытие.
- [GPT по Linux](upstream-port-audit/gpt-linux.md)
  — header/table validation, logical sectors, backup policy и independent partition streams.
- [HFS+/HFSX по Linux](upstream-port-audit/hfs-linux.md)
  — fork mapping/metadata bounds, Linux C Unicode/hardlinks, wrappers и streams.
- [ISO9660 по Linux](upstream-port-audit/isofs-linux.md)
  — logical blocks, XAR, multi-section directories и независимые extent streams.
- [UDF по Linux](upstream-port-audit/udf-linux.md)
  — sparse/continuation allocation, indirect ICB, sparable mapping и Linux C format-модуль.
- [NTFS по Linux](upstream-port-audit/ntfs-linux.md)
  — boot/MFT, attribute lists, sparse/compressed streams и Linux C LZNT1/LZX/XPRESS.

- [SquashFS по Linux](upstream-port-audit/squashfs-linux.md)
  — Linux codecs, index/xattr reads, inode/link properties и независимые streams.

- [ext2/3/4 по Linux](upstream-port-audit/ext-linux.md)
  — sparse/extent streams, Linux metadata checksums, inline data, геометрия и fs-verity.

- [FAT12/16/32 по Linux](upstream-port-audit/fat-linux.md)
  — BPB, NLS, metadata/partial streams и source audit модуля;
  общие исправления standalone и границы профилей указаны отдельно.

- [Удалённые форматы и legacy-варианты](upstream-port-audit/retired-formats.md)
- [cramfs по Linux — исторический аудит удалённого reader](upstream-port-audit/cramfs-linux.md)
  — C read path, metadata/stream границы, ограниченное чтение и source audit.

- [Read-only EROFS из Linux](upstream-port-audit/erofs-linux.md)
  — C read paths, четыре декодера, archive ABI и проверки приложения.

- [Прямое чтение устройств](upstream-port-audit/direct-devices.md)
  — потоковое открытие файловых систем, GPT/MBR/APM и границы native access.

- [COM server locks native shell DLL](upstream-port-audit/native-shell-server-lock.md)
  — запрет выгрузки модуля до освобождения всех LockServer-владельцев.

- [Владельцы результатов platform lookup](upstream-port-audit/platform-result-ownership.md)
  — очистка STRRET/GIO результатов при исключении и проверка ADS/launcher boundaries.

- [Смена владельца native drag-сессии](upstream-port-audit/native-drag-session.md)
  — прежний MIME-объект не очищает данные следующего перетаскивания.

- [Владение shell-подменю при отказе вставки](upstream-port-audit/shell-menu-ownership.md)
  — передача native/Qt menu tree только после успешной вставки в родителя.

- [Редизайн интерфейса](upstream-port-audit/ui-redesign.md)
  — поэтапная адаптация представления с сохранением исходных владельцев и поведения.

- [Владельцы process/listing/overwrite уведомлений](upstream-port-audit/process-notifications.md)
  — срок жизни Qt-транспорта и данных сигнала при удалении, отмене и повторном запросе.

- [Владение внешними процессами](upstream-port-audit/external-process-ownership.md)
  — OS handles, запуск, регистрация и освобождение без потери временной копии.

- [Подтверждение открытия неизвестных файлов](upstream-port-audit/unknown-file-open.md)
  — системный обработчик и настраиваемый вопрос перед автоматическим внешним открытием.

- [Поздняя смена языка после закрытия окна](upstream-port-audit/closed-language-event.md)
  — запрет обращения к освобождённым папкам панели из отложенного события Qt.

- [Владение историей посещённых папок](upstream-port-audit/location-history-ownership.md)
  — единая запись посещений при навигации и представление адреса без повторной записи.

- [Метаданные строк отчёта свойств](upstream-port-audit/property-report-ownership.md)
  — единый владелец данных для перевода, копирования и просмотра полного значения.

- [Граница команд выделения](upstream-port-audit/selection-command-boundary.md)
  — исходные индексы и отложенный снимок своей панели без дублирующей проекции.

- [Состояние автообновления панели](upstream-port-audit/auto-refresh-owner.md)
  — обнаружение изменений и восстановление прокрутки у одного владельца.

- [Общий ввод имени и маски](upstream-port-audit/name-input-owner.md)
  — данные представления сразу в контролах, исходный результат только после OK.

- [Жизненный цикл диагностических сообщений](upstream-port-audit/message-dialog-lifetime.md)
  — актуальные строки при повторном открытии и компоновка средствами Qt.

- [Владение при исключении копирования object vector](upstream-port-audit/object-vector-ownership.md)
  — освобождение уже скопированных владельцев при отказе очередного элемента.

- [Владение native-файлами при close и повторном открытии](upstream-port-audit/linux-file-close.md)
  — ошибки Linux close, очистка томов и изоляция отложенных метаданных POSIX-выходов.

- [Владение файлами и состояние volume writer](upstream-port-audit/multiout-lifetime.md)
  — очистка при отказе выделения памяти и корректное усечение предвыделенных томов.

- [Откат Windows Pause при нехватке памяти](upstream-port-audit/process-pause-ownership.md)
  — владение handles и suspend increments до операций, способных выбросить исключение.

- [Владелец отложенного Rename](upstream-port-audit/deferred-rename-owner.md)
  — привязка завершения редактора к исходной панели, папке и элементу списка.

- [Миграция на Qt Linguist](upstream-port-audit/qt-linguist.md)
  — каталоги Qt-текстов, live bindings и границы незавершённой миграции.

- [Адаптивные цвета интерфейса](upstream-port-audit/adaptive-colors.md)
  — общие семантические цвета, фон справки как у окна и обновление открытых видов.

- [Общий каталог кодеков в native workers](upstream-port-audit/codec-thread-ownership.md)
  — исходный атомарный COM-счётчик для владельца, разделяемого окнами.

- [Отмена файлового worker в ожидании перезаписи](upstream-port-audit/worker-prompt-cancellation.md)
  — согласование отмены и пробуждения при завершении операции и окна.

- [Владение файловым переносом при исключении](upstream-port-audit/file-transfer-ownership.md)
  — откат stages и опубликованного пакета, сохранение копии после частичного удаления исходника.

- [Общее рекурсивное удаление каталогов](upstream-port-audit/recursive-directory-removal.md)
  — исходный native-обход для Qt и native callers, отмена, ошибки и границы ссылок.

- [Папка архива после отката Move](upstream-port-audit/archive-close-reopen.md)
  — сохранение пути ADS до закрытия native handler на Windows.

- [Application and upstream versions](upstream-port-audit/application-version.md)
  — release identity includes the original 7-Zip source version in About and build metadata.

- [Independent About identity](upstream-port-audit/about-product-identity.md)
  — Sevenzip presentation with a separate upstream attribution and license section.

- [Доступность команд popup и Drop](upstream-port-audit/popup-command-state.md)
  — сохранение состояния контекстного меню и восстановление Copy после отмены Drop.

- [Обновление upstream до 26.04](upstream-port-audit/upstream-26.04.md)
  — перенос source changes, обновление engine/SFX и ресурсов справки.

- [Состояние нового архивного запроса](upstream-port-audit/qt-request-state.md)
  — независимый результат отказа и повторное извлечение после отмены.

- [FileManager / Agent / Qt: единый источник состояния](upstream-port-audit/fm-agent-qt-state.md)
  — metadata, режимы панели и shared mapping сортировки без параллельных Qt-копий.

- [Отложенные события файлового списка](upstream-port-audit/qt-list-events.md)
  — отмена hover при блокировке и завершение Shift-выделения до следующего нажатия.

- [Управление асинхронными фазами](upstream-port-audit/qt-phase-controls.md)
  — сохранение Pause при смене CLI-процесса или worker.

- [Режимы файлового списка](upstream-port-audit/small-icons-layout.md)
  — Small/Large Icons, размеры по шрифту, активная прокрутка и задержка наведения.

- [Фоновый приоритет завершившегося процесса](upstream-port-audit/background-process-exit.md)
  — повторная проверка архива после включения фонового режима.

- [Видимость платформенных настроек](upstream-port-audit/options-platform-visibility.md)
  — скрытие неподдерживаемых действий с сохранением исходных значений и Apply/Cancel.

- [Относительные ссылки справки](upstream-port-audit/help-relative-links.md)
  — переходы из Add к описаниям Method и SFX внутри корня справки.

- [Тема встроенной справки](upstream-port-audit/help-theme.md)
  — адаптация цветов исходных страниц к палитре приложения.

- [Стабильные колонки Add](upstream-port-audit/add-dialog-stable-columns.md)
  — смена параметров без сжатия окна и перемещения остальных полей.

- [Открытие перетащенного архива](upstream-port-audit/archive-drop-open.md)
  — запрошенное отличие от исходного действия упаковки внешних файлов.

- [Выход через меню macOS](upstream-port-audit/macos-quit-menu.md)
  — системная роль существующей команды выхода.

- [Проверка геометрии остальных диалогов](upstream-port-audit/dialog-layout-review.md)
  — native macOS EN/RU, resource-to-widget contracts и коррекция подписи Zone.Id.

- [Язык главного меню](upstream-port-audit/menu-language.md)
  — исходный перевод переключателя двух панелей при запуске и смене языка.

- [Проверка границ Qt-адаптации](upstream-port-audit/qt-adaptation-boundaries.md)
  — source comparison и focused runtime-свидетельства от 2026-10-18.

- [Понятные подписи открытия](upstream-port-audit/open-menu-captions.md)
  — уточнение названий Qt-меню с сохранением расположения и исходных команд.

- [Разметка дополнительных параметров архива](upstream-port-audit/compress-options-layout.md)
  — выравнивание флажков, перенос подписи и исходные строки NTFS.

- [Выбор архива через меню File](upstream-port-audit/open-archive-picker.md)
  — запрошенное расширение интерфейса и повторное использование открытия архива.

- [Относительные пути с `.` при распаковке](upstream-port-audit/dot-extraction-paths.md)
  — исправление предварительного отказа для CPIO payload и повторное использование
  исходной коррекции путей.

- [Имена с двоеточиями при извлечении на POSIX](upstream-port-audit/posix-colon-extraction.md)
  — native ext4 RECOVER, F5/Extract и сохранение защиты путей/Windows ADS.

- [Извлечение потоков без сохранённого имени](upstream-port-audit/nameless-stream-extraction.md)
  — source default path для Copy/Extract из gzip, открытого внутри ext4.

- [Локальное владение исходниками модулей](upstream-port-audit/module-source-ownership.md)
  — оригинальные build inputs 7-Zip в репозитории и явные соответствия provenance.

При изменении перенесённого модуля добавлять сюда ссылку на отдельную запись
в `docs/upstream-port-audit/`. Указывать дату, retrievable source repository,
полный commit, пути/символы, copied/adapted/omitted scope, зависимости и причины
адаптаций. Для обновления upstream сохранять прежний baseline и локальные fixes.
Runtime-проверки записывать с ревизией, ОС и фактически проверенным scope,
отдельно от полноты переноса исходников. Изменившееся поведение описывать
в [parity](7zfm-parity.md), границы компонентов — в [архитектуре](architecture.md).
