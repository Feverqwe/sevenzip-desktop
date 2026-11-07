---
name: sz-regression
description: Design or review regression tests for Sevenzip Desktop bug fixes, archive edge cases, and platform-specific failures.
---

# Sevenzip Desktop regression work

Use [testing](../../../docs/testing.md) to select checks; search
[the scenario matrix](../../../docs/regression-matrix.md) for the affected suite
and inspect its current code/callers. Historical coverage is under
`docs/archive/qt-port/testing/`.
Use `python3 scripts/verify.py --list`, then `<profile> --plan` or
`--tests '<regex>' --plan` to inspect the registered suites, targets and fixtures
before building. Check platform conditions in `CMakeLists.txt` and `cmake/`.
Follow [the validation rules](../../../AGENTS.md#validation-and-ui-reference)
for coverage, batching remote checks, and reporting platform limits.

Prefer small public-behavior tests and disposable archives made by official
`7z` sidecar or the affected native provider. Select relevant failure cases rather
than repeating a universal edge-case list. Matching output alone does not prove
source reuse: compare port bodies with the recorded upstream implementation.
Do not replace behavior tests with source-text assertions or treat a startup
smoke test as archive-feature coverage.

For layout work use [UI contracts](../../../docs/ui-layout-contracts.md) and
`tests/ui/support/layout_inspection.h` for affected views/states and geometry reports.
