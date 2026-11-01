# Agent documentation and tooling audit — 2026-11-01

Reviewed checkout: `master` at
`754c5723798c862c3df473817a290ed0125ff203`, with the uncommitted audit changes.
The working tree was clean before this task. No commit, branch, push or release
was requested or created. This is dated maintenance evidence; current commands
belong in [development](../development.md), policies in
[AGENTS](../../AGENTS.md), and navigation in the [documentation map](../README.md).

## Scope and disposition

| Area | Review and result |
| --- | --- |
| Agent instructions | Reviewed source-port, worktree, validation, recovery and runner rules against their linked workflows. Removed duplicate docs-only guidance and shortened repeated diagnostic/retention instructions without changing their requirements. |
| Project skills | Reviewed all six repository skills, their frontmatter, links and commands. Replaced the obsolete RAM-workspace wording in the C++/Qt skill. Setup templates intentionally use links relative to their installed location. |
| Documentation navigation | Checked local Markdown targets and fragments across tracked and new Markdown, including archives and fixture documentation. Repaired an old release-provenance link and HFS/ISO fixture source paths. |
| Current workflow information | Compared build/test/runtime/package/upstream commands with their entrypoints, CLI help and CMake registrations. Removed obsolete migration status, installation/binary paths, cache-location claims and contradictory default-target/output descriptions. The generated CMake artifact map confirms the corrected binary locations. |
| Historical material | Moved the completed QCOW scope review and user-workflow audit into this archive, updating relative and incoming links. Compared their content with the original files, allowing only link-path changes. Kept source-audit receipts, baseline manifests, licenses and unique decisions. |
| Local memory | Reduced the ignored starting index from 198 to 33 lines. Preserved all 97 original links in a dated historical index and retained the original notes. Removed historical goal statuses from the entrypoint; they remain dated evidence in the archived index. |
| Scripts and helpers | Checked Python/shell syntax and tooling JSON; inspected reference use and candidate unreferenced generators. Those generators retain fixture/source reproducibility value, so lack of callers was not treated as deletion proof. Fixed the broken ext4/fs-verity fixture generator and checked the existing tooling behavior suites. |

The generator imported Ext helpers from their former directory and assumed the
certificate header's closing braces occupied separate lines. It now uses the
current owners and accepts the formatted header. An explicit output path allows
disposable regeneration; an early capability check explains an unsuitable CMS
tool, and OpenSSL diagnostics remain visible. OpenSSL is an existing fixture
generation tool, not an application library dependency. Its workflow is described
in [testing](../testing.md#регенерация-криптографических-fixtures).

## Validation

The existing `tooling` profile passed on Linux first, then macOS and Windows.
It exercises CTest selection, developer wrappers, source/baseline/cache helpers,
runner synchronization and memory setup, plus the Windows reference builder on
Windows. All selections had no project executable targets to build; miniature
projects compiled by the behavior tests retained the configured toolchain/cache.

| Platform | Command | Result and timing |
| --- | --- | --- |
| Linux | `python3 scripts/verify.py tooling --platform linux` | 8/8 suites; configure 0.26 s, tests 11.98 s. Run `20261101T151405Z-b3a618ee`. |
| macOS | `python3 scripts/verify.py tooling --platform local` | 8/8 suites; configure 12.16 s, tests 23.89 s. |
| Windows | `python3 scripts/verify.py tooling --platform windows` | 9/9 suites; configure 11.285 s, tests 34.619 s. Run `20261101T151508Z-15dc0efe`. |
| Linux | `python3 scripts/hyperv-runner.py run linux -- scripts/doctor.sh` | PASS; run `20261101T151508Z-2d95846a`. |

Full fixture regeneration passed on macOS with the installed OpenSSL and on
Windows (`20261101T151346Z-e3cca785`). All four generated image cases matched
the existing decompressed image bytes and metadata; OpenSSL verified the three
signed cases. Original Linux test vectors were checked against the recorded
revision and SHA256 before use. No production fixture was changed.

Full Linux regeneration remains unverified: the installed CMS tool lacks
`-no_signing_time`. No new tool was installed. The early rejection of an
unsuitable tool was checked on macOS before reference reads or output writes.
A Windows diagnostic HTTPS timeout was resolved by transferring the same
hash-verified original input through normal source synchronization.

Final static checks covered 341 Markdown files, 3,041 local path links and 1,590
fragments with no unexpected missing targets. The three template-relative links
are checked in their installed context by `project_memory_setup`. Syntax checks
passed for 52 Python files, 17 shell scripts and 13 tooling JSON files.
The Windows PowerShell parser accepted all six project PowerShell scripts
without executing them.
`git diff --check` passed. These checks establish maintenance/tooling evidence,
not new product, UI parity, package, full-gate or slow-suite acceptance.

Temporary validation scripts, transferred original test input, generated images,
certificates and keys were removed. Normal synchronization removed the temporary
sources on both guests. Small diagnostic/verification logs were retained;
incremental build trees, compiler caches, installed tools and dependency archives
were preserved. Historical source audits were not rewritten as current results.
