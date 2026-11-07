# Проверка компоновки Qt

[UI contracts](../ui-layout-contracts.md) задают состояния затронутого вида;
[testing](../testing.md) — выбор и объём checks. Начинайте с normal/compact
геометрии, текста, видимости, enabled-state, grouping, clipping и focus.

## Producers и отчёты

Найдите suite и producer по имени диалога в контракте и текущих tests.
`tests/ui/support/layout_inspection.h` предоставляет запись widget JSON.
Соберите producer через `build.sh`, затем запустите выбранные suites через
`ctest.sh`; [verify](../development.md#local-commands) умеет выбрать targets
и требуемые fixtures по CTest registrations.

```sh
rg -n 'SEVENZIP_UI_REPORT_DIR|layout_inspection' tests/
./scripts/build.sh dev <producer-targets>
SEVENZIP_UI_REPORTS=ON ./scripts/ctest.sh --preset dev -R '<affected-suites>' --output-on-failure
```

Используйте отчёты текущей сборки. Учитывайте язык, активную панель,
состояние операции и размер окна из контракта. Проверяйте фактические controls
и связи в JSON, включая скрытые/отключённые состояния; наличие файла само
по себе не является проверкой геометрии.

Widget-dialog fixtures отвечают на Qt-диалоги после готовности их nested loop
и активации. Они не проверяют настоящие OS panels. Для shared geometry,
клавиатуры и modal lifecycle проверяйте также прямых consumers адаптера,
а не только форму, из которой началась правка.

## Native и rendered evidence

Offscreen reports подходят для resource-to-widget contracts.
Для сравнения с оригинальным Windows runtime нужны actual reference,
native QPA на каждой заявленной ОС и записанные language/state.
Порядок — в [runtime comparison](../ui-runtime-comparison.md), сбор оригинала —
в [Windows reference](../windows-ui-reference.md), resource checks —
в [UI tooling](../ui-reference.md).

PASS геометрии не доказывает pixel equality, физический ввод или системный
workflow. При необходимости проверки fonts/icons/colors/style создавайте
скриншоты только затронутого вида с `SEVENZIP_CAPTURE_SCREENSHOTS=1`.
Сохраняйте текущие review/failure reports по
[artifact retention](../testing.md#test-artifact-retention).

Детальные исторические сценарии и ограничения producers сохранены в
[архивном каталоге](../archive/qt-port/testing/geometry.md).

## Каталог снимков для ревью цветов

`python3 scripts/capture-ui-review.py` собирает и запускает существующие
UI-producers с нативным QPA и process-only light/dark схемами. Нужен доступ
к desktop-сессии. `--list` показывает сценарии, `--only <id> ...` ограничивает
повторный сбор затронутыми окнами. Существующие build trees и compiler cache
сохраняются. Если ни один выбранный сценарий не поддерживает текущую платформу,
команда завершится до подготовки runtime и сборки.
Результат по умолчанию — `dev/ui-theme-review/index.html` внутри
artifact-каталога, который возвращает
`python3 scripts/lib/build_paths.py path --root "$PWD" --category artifacts`;
галерея открывает снимки с листанием стрелками, масштабом 100% и копированием
полного относительного имени для замечаний. Фильтры задают набор для листания;
адрес выбранного снимка можно сохранить. Доступны PNG, widget JSON и manifest
с результатами. `--gallery-only` обновляет просмотрщик по существующему manifest
без повторного запуска UI-producers.
Режим «Уникальные окна» оставляет один представитель каждого окна, включая
главное окно, меню и отдельные страницы настроек. Можно выбрать обычный или
компактный размер и отдельно отфильтровать язык и тему. Язык определяется по
метаданным и видимым подписям controls, а не по именам пользовательских файлов;
снимки без языковых свидетельств доступны в отдельном пункте фильтра.
Неудачный сценарий или неподдержанная нативная схема остаётся явным пробелом.

Сбор изображений не заменяет профильные CTest-проверки поведения. Видимая область
снимка ограничена Qt client area; native OS pickers и оконные рамки проверяются
отдельно при изменениях соответствующей платформенной интеграции.
