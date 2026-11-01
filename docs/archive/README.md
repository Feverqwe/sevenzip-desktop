# Архив документации

Здесь сохранены завершённые работы и свидетельства прежних ревизий.
Для разработки начинайте с [рабочей карты](../README.md).
Архив не участвует в обычном `rg`; для поиска по выбранной теме используйте
`rg --no-ignore` с нужным каталогом.
Архивные `active`, `partial`, «следующий шаг» и правила итераций описывают
прошлый процесс; новые задачи определяются текущим поручением.

## Qt-портирование завершено 2026-10-17

Обязательные 47 пакетов приняты в согласованном объёме. Итоговые доказательства,
исключения и непроверенные сценарии сохранены в
[финальной приёмке](qt-port/ux-port-audit.md#final-acceptance-of-product3a0b3b7).
Результаты относятся к указанным там ревизиям и платформам.

| Материалы | Где искать |
| --- | --- |
| Исходный scope, зависимости и правила итераций | [План](qt-port/porting-plan.md) |
| Закрытые пакеты и подшаги | [Чеклист](qt-port/porting-checklist.md) |
| Происхождение кода, адаптации, причины пропусков и проверки | [Source audit](qt-port/upstream-port-audit.md) |
| Bodies/callers и решения по исходным модулям | [Source ledger](qt-port/omitted-scope-audit.md) |
| История сценариев и платформенных отличий | [Parity](qt-port/7zfm-parity.md) |
| UX-сверка и итоговая приёмка | [UX audit](qt-port/ux-port-audit.md) |
| Подробные архитектурные исследования | [Компоненты](qt-port/architecture/component-notes.md), [прежний обзор](qt-port/architecture.md) |
| Накопленные описания проверок | [Coverage](qt-port/testing/coverage.md), [geometry](qt-port/testing/geometry.md), [матрица](qt-port/regression-matrix.md) |
| Прежний индекс UI-спецификаций | [UI index](qt-port/ui-layout-contracts.md) |
| Ранние пробы runtime builders и tooling | [Workflow evidence](qt-port/workflow-evidence.md) |
| Ранние инвентаризации и checkpoints | [Исходный реестр](qt-port/history/p0-01-source-inventory.md), [первый перенос](qt-port/history/p0-02-source-port.md), [checkpoints](qt-port/history/checklist-checkpoints.md), [runners](qt-port/history/hyperv-runner-evidence.md), [baseline плана](qt-port/history/porting-plan-baseline.md) |

Уникальные commits, символы, ограничения и неудачные проверки сохранены.
Обращайтесь к ним при изменении соответствующего модуля или обновлении upstream.
Текущий машинный baseline остаётся в [manifest](../upstream-baseline.json).
Новые инструкции и действующие контракты поддерживаются в рабочих документах;
исторические receipts не дописываются новыми результатами.

## Other historical records

- [Agent documentation and tooling audit, 2026-10-25](agent-maintenance-2026-10-25.md).
- [Agent documentation and tooling audit, 2026-11-01](agent-maintenance-2026-11-01.md).
- Implemented plans: [direct device access](plans/direct-device-reading-plan.md),
  [interface redesign](plans/ui-redesign-plan.md), [ZIP refresh](plans/zip-refresh-plan.md),
  [C++23 migration and acceptance](plans/cpp23-project-migration.md).
  Their iteration orders and former next steps are historical; current behavior
  and limitations belong to the [documentation map](../README.md).
- [Runner installations and desktop/network diagnoses](runner-environment-2026-10-18.md).
- [Add dialog layout investigations and intermediate validation](add-dialog-layout-history.md).
- [QCOW scope review and rollback, 2026-10-24](qcow-review-2026-10-24.md).
- [User workflow and control-state audit, 2026-10-30](user-workflow-audit-2026-10-30.md).
