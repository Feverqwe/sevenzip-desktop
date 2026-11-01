---
name: sz-navigation
description: Trace Sevenzip Desktop C++ code, Qt UI behavior, native engine integration, and corresponding upstream 7-Zip implementation when locating symbols or planning a refactor.
---

# Navigate Sevenzip Desktop and upstream 7-Zip

Search with `rg -n` and `rg --files`. Refresh compiler commands with
`./scripts/build.sh dev --configure-only` when build inputs change, then locate
the selected workspace and check a translation unit:

```sh
build_root="$(python3 scripts/lib/build_paths.py path --root "$PWD" --category work --preset dev)"
clangd --check=<source-file> --compile-commands-dir="$build_root/dev"
```

The compilation database is `$build_root/dev/compile_commands.json`; do not
assume a fixed path across branches, platforms or configured workspaces.

Locate current code by its project module owner. The
[7-Zip baseline](../../../docs/upstream-baseline.json) maps current owners to
original paths; other dependency baselines record their own owners. Do not infer
provenance from a project directory name. FileManager, archive/Agent integration,
GUI, format readers and dependency facades have separate project directories.

For original bytes, use the repository-owned source archive and the
[baseline workflow](../../../docs/upstream-update.md). An explicitly selected
read-only reference checkout is useful for upstream history and update review;
it is not an ordinary build prerequisite or a model for the project tree.
Use `git log -S'literal' -- path` or `git blame` when history matters.

Trace complete bodies, callers, supporting types and platform dependencies,
not just resources or names. Start with the current
[audit index](../../../docs/upstream-port-audit.md) and
[module decisions](../../../docs/omitted-scope-audit.md). For earlier baseline
path/symbol decisions, search `docs/archive/qt-port/upstream-port-audit/`
and `docs/archive/qt-port/omitted-scope-audit/` with `rg --no-ignore`.
Follow `AGENTS.md` for port requirements.
In a plan/review identify source revision/symbols, copied code and necessary
adaptations; cite concrete paths/lines and verify findings in current code/tests.
