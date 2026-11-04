# Главное окно и workspace

Изменение по согласованному дизайну, 2026-11-05. Вкладки, независимые окна,
сессия, именованное избранное и breadcrumb chrome — project-owned Qt integration.
Они не заимствуют алгоритмы архивов или файловых систем.

## Native scope

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

Навигация, вложенные archive parents, CopyTo, валидация extraction paths,
ограниченное чтение, пароли, update/reopen и отмена остаются у существующих
native/backend владельцев. Новый код не возобновляет операции из сессии.
Полное удаление help subsystem и переработка самостоятельных Add/Extract/Copy
диалогов сюда не входят. Неизменённые source language и лицензии 7-Zip сохранены.

## Lucide

[Отдельный baseline](../lucide-baseline.json) фиксирует полные исходные SVG и
LICENSE из <https://github.com/lucide-icons/lucide.git> на
`1ef60af944de207e0d5176905a0759ba12e6ee8d`. Он включает прежние и новые command
symbols, чтобы обновление всего используемого набора было воспроизводимо.
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
Windows x86_64 — PASS: native QPA и интерактивный desktop для clipboard,
workspace lifecycle, shell и shortcuts. Заключительные workspace reruns на всех
трёх ОС включают отказ закрытия исходного окна, повторное открытие закрытой панели,
дубликаты избранного и недоступное место через session/favorites/new window.
Финальный macOS bundle собран, изолированный startup smoke завершился успешно.

Эти focused проверки не являются полным/slow gate. Снимки показывают реальный
Qt client chrome; pixel parity с оригинальным Windows и физические жесты
перетаскивания/раскладки клавиатуры ими не установлены. Финальная пользовательская
визуальная приёмка остаётся отдельным review согласованного редизайна.
