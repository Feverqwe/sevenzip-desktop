# Documentation map

The Qt porting plan is complete; its scope and validation evidence are in the
[archive](archive/README.md). Choose documentation by the current task.
Keep each fact in one authoritative document and link to it elsewhere.

## Where to start

Agents start with [AGENTS.md](../AGENTS.md) and the local memory index, if present,
then read the relevant documentation, code and dependencies. Dated validation
evidence applies only to the recorded revision, platform and scenario.

| Task | Primary documentation |
| --- | --- |
| Learn about the application | [README](../README.md) |
| Read files directly from an external device | [Direct device access](direct-device-reading.md) |
| Open container files in other applications through a read-only mount | [Read-only mounts](readonly-mounts.md) |
| Browse Android/flash F2FS images | [Read-only F2FS](f2fs-reading.md) |
| Browse Apple sparseimage/sparsebundle packages | [Sparse images](sparse-images.md) |
| Read fscrypt files using raw master keys | [Encrypted ext files](ext-encryption.md) |
| Report a bug, contribute or check licenses | [Contributing](../CONTRIBUTING.md), [security](../SECURITY.md), [licenses](../THIRD_PARTY_NOTICES.md) |
| Set up, build, run or prepare runtime dependencies | [Development](development.md) |
| Review the completed C++23 migration | [Migration acceptance](archive/plans/cpp23-project-migration.md#приёмка-миграции--2026-10-28); current setup: [Development](development.md) |
| Update interface translations | [Localization](localization.md) |
| Select checks and clean up temporary artifacts | [Testing](testing.md) |
| Maintain agent setup, skills or validation helpers | [Agent rules](../AGENTS.md), [tooling checks](testing.md#проверка-tooling), [memory protocol](agent-memory.md) |
| Find workflows, source owners and regressions | [Regression matrix](regression-matrix.md) |
| Find a test suite or UI data producer | [Coverage](testing/coverage.md), [geometry](testing/geometry.md) |
| Understand UI/backend ownership and boundaries | [Architecture](architecture.md) |
| Organize project modules and repository-owned build sources | [Source ownership](architecture.md#module-directories-and-source-ownership) |
| Change direct read-only device access | [Usage and limits](direct-device-reading.md), [source audit](upstream-port-audit/direct-devices.md) |
| Change the redesigned interface or shortcuts | [UI contracts](ui-layout-contracts.md), [shortcut scheme](ui-shortcuts-design.md), [source audit](upstream-port-audit/ui-redesign.md) |
| Check behavior and platform differences | [Parity](7zfm-parity.md) |
| Review past workflow audits and source decisions | [Archive](archive/README.md), [source audit](upstream-port-audit.md) |
| Change layout, text, visibility or resizing | [UI contracts](ui-layout-contracts.md) |
| Extract resources and compare them with widgets | [UI reference tooling](ui-reference.md) |
| Capture an original Windows reference | [Windows reference](windows-ui-reference.md) |
| Compare native runtime geometry across three operating systems | [Runtime comparison](ui-runtime-comparison.md) |
| Configure remote hosts and validate Linux/Windows | [Local configuration](runner-configuration.md), [runner workflows](hyperv-runners.md) |
| Build local packages or understand CI/release scope | [Packaging and CI](ci.md) |
| Check source provenance and adaptation decisions | [Source audit](upstream-port-audit.md), [module decisions](omitted-scope-audit.md) |
| Compare or update upstream | [7-Zip baseline](upstream-baseline.json), [QEMU QCOW baseline](qcow-qemu-baseline.json), [update procedure](upstream-update.md) |
| Review the completed UX acceptance scope | [Acceptance](ux-port-audit.md) |
| Save an investigation or handoff | [Memory protocol](agent-memory.md) |
| Find a completed plan, past decision or validation record | [Archive](archive/README.md) |

Skills in `.agents/skills/` provide task-specific workflows. `.codex/` stores local
memory; its notes do not replace documentation or define a backlog.
Ordinary `rg` searches skip the documentation archive. For a specific historical
investigation, use `rg --no-ignore` with the relevant archive directory.

## Maintaining documentation

- Update the primary document when its facts change. Keep the README focused on
  purpose, usage and public limitations; put implementation details in the
  relevant technical document.
- Do not duplicate code, configuration lists, counters or run logs. Verify
  commands and test names against the current scripts, CMake files and tests.
- Link new source decisions and validation evidence from the
  [audit](upstream-port-audit.md), recording the date, revision, OS and scope.
  Update the corresponding architecture, parity or UI contract when it changes.
- Preserve unique commits, upstream symbols, omissions, limitations and reasons
  for deviations. Move completed plans and superseded investigations to the
  archive, updating incoming and relative links.
- Read archive entries for a specific symbol or scenario. Their past statuses,
  iteration rules and next steps are not current instructions.
