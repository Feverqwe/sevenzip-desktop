# Подготовка запуска и выбор внешнего Drop — 2026-10-30

База Desktop: `0c9c8ffae4b6b51b4e287e340aeca54fa7d4ffe7`.
Исходный репозиторий: <https://github.com/ip7z/7zip.git>, ревизии полных
inventory units и локальные владельцы — в [baseline](../upstream-baseline.json).
Default revision: `b88c6bc09339de32f7d18bbab900686b244f42ba`.

Сопоставлены оригинальные FM.cpp::WinMain2/InitInstance,
App.cpp::Create/CreateOnePanel, Panel.cpp::Create/OnCreate,
PanelDrag.cpp::Drop/CompressDropFiles и их текущие Qt consumers.
Ресурсы главного меню и исходные command IDs сохранены.
Исходные варианты Drop, target/source transfer protocol, временные файлы,
подтверждение Copy-to-archive и самостоятельный GUI Add остаются своими
существующими владельцами.

Обычный FM заранее готовит каталог кодеков, после чего показывает окно с
доступными архивными командами. Это Qt/macOS presentation adaptation:
фоновый dlopen может блокировать AppKit через общий dyld lock.
Архивный argv startup сохраняет исходную начальную BindToPath и отказ;
filesystem startup сохраняет доступ к папке и при ошибке discovery.
Document event сохраняет независимый backend и отменяемую загрузку, но показывает
новое окно после привязки документа, без промежуточного списка папки.
Стоимость подготовки runtime не устранена переносом момента показа.

Внешний левый Drop одного обычного каталога или распознанного архивного файла
на filesystem-панель теперь предлагает Open, Create archive и Cancel.
Оба действия предлагаются только при готовом каталоге и наличии writer.
Распознавание архива использует исходный FindFormatForArchiveName;
фактическая проверка архива, пароль и ошибка остаются в исходном открытии.
Create archive продолжает исходный Drop/Add; Cancel не запускает операцию.
Несколько путей, symlink, неизвестный тип, archive target, native 7-Zip transfer
и правый/средний Drop сохраняют прежний dispatch.

Upstream не имеет такого выбора Open/Compress: это прямо запрошенное
пользователем расширение существующей [адаптации archive Drop](archive-drop-open.md).
Новых внешних implementation units, перенесённых символов, лицензий или
functional omissions нет. Изменён только проектный Qt presentation/routing;
перезаписывать pinned original bytes и hashes не требуется.

Поведенческие проверки защищают первый показ готового окна, отсутствие
промежуточной папки, отмену/ошибку discovery и результаты трёх Drop-действий.
Состояние проверок, найденные дефекты и границы свидетельств — в
[отчёте аудита](../user-workflow-audit.md). Pixel parity с оригинальным Windows
не заявляется: новый вопрос отсутствует в оригинале.
