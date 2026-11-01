# Выбор suites

[Testing](../testing.md) задаёт стратегию и объём проверки;
[regression matrix](../regression-matrix.md) связывает наблюдаемый сценарий,
source owner и CTest consumers. Для UI дополнительно нужна
[geometry](geometry.md).

Начинайте с существующего профиля, затем добавляйте прямых consumers изменения:

```sh
python3 scripts/verify.py --list
python3 scripts/verify.py <profile> --plan
python3 scripts/verify.py <profile>
python3 scripts/verify.py <profile> --platform all
```

Актуальный состав профилей хранит `scripts/config/verify-profiles.json`; `--plan`
показывает выбранные CTest suites и targets. Профиль не заменяет оценку влияния.
Для нативных Windows-профилей указывайте `--platform windows`; они используют
интерактивный рабочий стол раннера. Локально на Windows доступен `--platform local`.
Для сценария вне профилей используйте `--tests '<regex>'` или wrappers из
[development](../development.md#local-commands).

Ищите реализацию проверки по имени suite и наблюдаемому результату:

```sh
rg -n -F 'archive_transfer' tests/ CMakeLists.txt cmake/ docs/regression-matrix.md
```

Проверяйте текущую регистрацию и тела tests, включая fixtures, платформенные
условия и `slow`. Наличие теста не означает его выполнение; focused PASS
не заменяет native или full acceptance. Долгие suites требуют отдельного
запроса по [правилам opt-in](../development.md#long-suites).

Подробный накопленный [каталог портирования](../archive/qt-port/testing/coverage.md)
сохранён для поиска старых регрессий и обоснований. Его числовые результаты,
`stage` и `unverified` относятся к прежним ревизиям.
