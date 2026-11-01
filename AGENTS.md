# Sevenzip Desktop agent guide

Sevenzip Desktop is an independent C++23 / Qt 6 Widgets application for Windows,
macOS, and Linux, based on 7-Zip and other recorded dependencies. It owns its
architecture and source layout. Upstream repositories establish provenance and
provide comparison/update references; they do not define the project tree.

## Source-port requirements

- Start from upstream implementation, not a new implementation of its behavior.
  Before functional changes, inspect original bodies, callers, types and dependencies.
  Functional code inside `UI/` files is part of the port; replace Win32 presentation
  and platform services with Qt or narrow adapters without rewriting the whole module.
- Preserve usable upstream algorithms, control flow and portable types. Organize
  project-owned sources and adaptations by their local functional module, with
  explicit provenance mappings to original paths/symbols. Do not mirror an
  external repository's tree under `src/` or use existing overlay/include rules
  as a reason to require that layout; adapt those rules when moving sources.
  Under `third_party/<dependency>/`, preserve the used dependency's internal
  structure inside its local source archive. Expand unchanged sources only into
  owned build directories, checking archive/member hashes and safe output paths;
  retain licenses and provenance beside the archive. Keep one authoritative
  implementation per module.
- Keep each project module's interface, implementation, private helpers and
  platform variants in its owning directory. This applies to C++ named modules
  and native libraries/plugins as well as Qt components. Shared code has its own
  owner and target; consumers link it instead of copying it into their modules.
  Reusable unchanged dependencies remain under `third_party/` and are linked
  through their owners rather than copied into each consumer.
- Keep adapted dependency modules together under `src/dependencies/<module>/`:
  their sources, interfaces, wrappers and module-specific build definitions.
  Unchanged licenses remain under `third_party/<dependency>/`, including when
  that directory contains only notices because all used code is adapted.
  Do not move those notices into adapted source modules or a shared licenses
  collection merely to remove notice-only directories.
- Keep all 7-Zip sources, headers, resources and build inputs needed for the
  application, engine, console and SFX in this repository. Ordinary builds,
  runtime preparation, packaging and their tests must use these local owners,
  without reading, cloning, fetching or unpacking a separate upstream repository.
  Separate upstream snapshots are for provenance verification, update review and
  explicitly requested original-reference comparisons. Source archive extraction
  must preserve useful incremental outputs and must not trigger network access.
- Keep one authoritative archive reader, shared by the application and console
  `7z` through the same external format modules. Linux/GPL readers belong to
  their project format modules; do not keep parallel SDK implementations.
  Extract portable probes and shared compression dependencies
  into common units when needed by the core. Keep GPLv2, GPLv3 and unRAR-bearing
  modules separate; full GPL readers are not overlaid into the core runtime.
  Record extractions and their original inventory units in the existing baseline.
- Preserve the source language: imported C stays C and is compiled as C;
  imported C++ stays C++. Local UI/integration uses C++23 / Qt 6; necessary platform
  bridges may use their native language, with the reason recorded in the audit.
  Use existing upstream headers and their C linkage at C/C++ boundaries.
- Before independently implementing borrowed archive/filesystem behavior, check
  for a usable recorded upstream implementation and reuse it where available.
  Independent replacements of that behavior remain migration debt; address the
  relevant part when working on it without a repository-wide rewrite. Project
  features and Qt/platform integration have their own functional owners and are
  not migration debt merely because they have no upstream counterpart.
- Keep archive operations behind `ArchiveBackend` and the official native engine.
  `7zFM` and `7zG` do not invoke console clients. Build and ship the official `7z`
  as an independent sidecar; do not build or ship `7zz`. Do not reimplement archive
  formats or compression.
- Keep any external reference checkout read-only. Copying source into this
  repository is authorized; editing upstream or adding Qt to it requires a user
  request. Reference checkout locations do not prescribe local source paths.
- Record full upstream commit, retrievable source repository, paths/symbols,
  copied/adapted/omitted scope, dependencies and reasons in
  [the port audit](docs/upstream-port-audit.md). Preserve notices and licenses.
  Record observable workflow/platform differences in [parity](docs/7zfm-parity.md).

For every external code import or adaptation, including individual functions and
snippets from any project, establish a reproducible baseline in the same change.
Follow the Linux/QEMU model: record the retrievable repository, full upstream
commit, SHA256 hashes of complete original inventory units and their mapping to
local source owners in a machine-readable manifest. Record what was borrowed and
where it lives locally, including selected symbols for partial imports; local
paths need not match upstream. Distinguish unchanged dependencies, adapted code
and local integration.
Record transferred symbols, dependencies, omitted scope, adaptation reasons and
licenses in the existing source audit. Pin original bytes, not the current
adaptation, so safe local refactoring does not require changing upstream hashes.

Reuse the existing baseline verification and review tooling; extend it only when
needed. Every import must have a documented, repeatable way to verify its original
baseline and compare a proposed upstream revision with both that baseline and
the local adaptation. A source URL or an audit note alone is insufficient.
Keep mixed revisions explicit. For updates follow
[the update procedure](docs/upstream-update.md): compare a separate snapshot,
review dependencies/omissions and retain local fixes. Do not overwrite partial
ports with complete upstream modules or advance provenance before validation.
Keep adaptations narrow; account for formatting separately from functional changes.

### Porting order: source comparison before runtime acceptance

1. Before implementation or acceptance runs, compare the selected upstream module
   with the current port: complete bodies, callers, types, dependencies, resources,
   handlers and significant branches. Identify missing code, stubs, independent
   replacements and disconnected consumers from source, rather than discovering
   them one at a time through runtime failures. Reuse the existing audit for scope;
   do not create another inventory.
2. Use upstream files with their headers/resources/dependencies as inventory units.
   Port and connect the dependencies needed for a complete component or scenario;
   copying an isolated file is not completion. Preserve usable upstream code and
   limit adaptations to the required Qt/platform boundaries.
3. Resolve source-visible gaps in the selected scenario together in the same
   iteration, or record a concrete blocker under the plan's scope rules. Do not
   launch a known-incomplete scenario merely to demonstrate a failure already
   established by missing code. Targeted diagnostic runs remain useful when they
   resolve an actual uncertainty about behavior or integration.
4. After implementation, compare the changed scope with upstream again for omitted
   branches and unconnected callers, then build affected targets and run focused
   behavior/integration checks. Incremental compilation and diagnostic checks are
   allowed during implementation; this order does not require a whole-project
   source audit before each check.
5. Regressions protect observable behavior. An actual pre-fix failing run is useful
   to establish reproduction or test sensitivity, but is not mandatory ceremony
   for an obvious missing implementation. Do not add source-text assertions or
   trivial tests as a substitute for behavior checks.
6. Report source-port completeness separately from runtime validation, including
   omissions/adaptations and checked platforms. Passing tests do not prove complete
   source transfer; source similarity does not prove a working platform adaptation.

## Read-only filesystem access

- This policy covers read-only recovery/partial access to filesystems and
  popular archive and compressed-stream formats. It does not make ordinary
  archive editing read-only; the restrictions below apply to recovery access.
- Apply the filesystem recovery/partial-access requirements below only to formats
  prioritized for common recovery needs or inconsistent-volume scenarios.
  The current scope is NTFS, FAT/exFAT and ext; do not automatically extend it
  to every filesystem supported by Linux or this project.
  For other filesystems, including EROFS, defer new recovery modes and partial
  access to damaged images unless the user specifically includes that scenario.
  A general Linux format audit does not require adding those recovery paths.
  Preserve already implemented limited-access behavior.
- For archives and compressed streams, the recovery scope includes 7z, ZIP, RAR,
  TAR, gzip, bzip2, xz and Zstandard. Favor access to readable entries or data in
  damaged, truncated or incomplete inputs when the official engine provides a
  usable reading mode. Do not automatically extend new recovery requirements
  to every supported format; preserve existing limited-access behavior elsewhere.
- For every selected format, correct reading of supported features and updates
  against the relevant reference remain in scope: Linux for filesystems and
  upstream 7-Zip for archives and compressed streams. Read-only source access and
  bounds/path validation are mandatory regardless of recovery priority.
- Within the recovery scope above, favor useful read-only access when the reader
  can retrieve files or stream data, including partial or potentially inconsistent
  data. Distinguish actual reader
  limitations from restrictions introduced by product policy. A recovery flag,
  incomplete support or inability to guarantee consistency alone should not
  become an unconditional refusal when a usable reading mode exists.
- Explain the concrete limitations and offer an explicit user choice to proceed.
  Keep that choice scoped to the selected opening, retain a visible indication
  of the mode, and report unreadable/skipped files and incomplete results honestly.
  Reading a `RECOVER` filesystem without journal replay is the reference example:
  warn about missing, outdated or unreadable files while allowing copying.
  For archives, distinguish successfully extracted entries from partial output,
  checksum failures and unverified data; do not report a partial extraction as
  complete success. Scope consent to the selected archive opening or extraction.
- Check the relevant reader behavior before introducing or relaxing a gate;
  investigate whether a supported subset can be exposed. User consent does not
  implement missing decoding or make unreliable data verified. Preserve read-only
  source access and bounds/path validation; this choice does not authorize writes,
  repair or journal replay on the source. For archives, preserve extraction-path
  and bounds validation and password requirements; consent does not bypass
  encryption or supply missing volumes, codecs or recovery algorithms. Do not
  reimplement decoding or recovery outside the official engine. When useful
  reading is genuinely unavailable, explain the specific technical limitation.

## Feature worktrees and shared harness

Use the global `worktree` skill for worktree lifecycle and safe cleanup.
Work in the current checkout and branch by default. Create a new branch and
worktree only when the user explicitly asks to work in a new branch; a feature,
fix or audit request alone does not authorize that move. Continue existing
branch/worktree work in its checkout when the user asks to resume it.
When a new branch is requested, use
`<harness-root>/.worktrees/<feature-slug>` and the requested branch name, or
`codex/<feature-slug>` if none was given. Shared harness/instruction changes
may be made in the main checkout.

At task start distinguish the source root from the harness root:

```sh
task_root="$(git rev-parse --show-toplevel)"
harness_root="$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")"
git -C "$task_root" status --short --branch
```

The main checkout owns the shared harness: current agent instructions, skills
under `.agents/`, memory under `.codex/`, private runner configuration and installed
toolchains. Read its `AGENTS.md` as well as task-specific instructions in the
worktree. Resolve harness assets against `harness_root`, even when the current
directory is a linked worktree. Do not duplicate private settings or initialize
separate task memory there. When changing tracked harness files for a feature,
edit their worktree copies so the change belongs to the feature branch.

Source edits, tracked documentation, Git operations and validation belong to
`task_root`. Run that worktree's `scripts/build.sh`, `scripts/ctest.sh`,
`scripts/verify.py` and `scripts/hyperv-runner.py`; invoking the main checkout's
scripts would build or synchronize its source instead. Keep CMake build trees,
runtime outputs and test artifacts local to the worktree; never symlink a shared
`build/` across branches. Reuse the existing compiler cache and installed Qt kit
(set `CMAKE_PREFIX_PATH` to the harness kit when needed). Runtime/SFX preparation
uses the worktree's recorded source archive and local adaptations. Helpers may
use an already prepared runtime via their documented skip-preparation option.

Remote runner commands automatically read the main checkout's private runner
configuration and use its shared logs and compiler/tool caches.
They synchronize the invoking script's worktree and select its Git branch;
the local worktree directory does not affect remote workspace identity.
Explicit runner configuration overrides remain supported. Do not run two
different tasks under the same branch name expecting isolated guest builds.
Guests execute synchronized source copies: do not create/update a project Git
repository there or add a separate source-hashing pass. Keep the existing local
Git whitespace checks before synchronization and the branch-scoped runner locks.

## Context and memory

Read `<harness-root>/.codex/memory/README.md` at task start if present, then only
relevant notes.
Follow [the memory protocol](docs/agent-memory.md) for dated evidence, handoffs,
privacy and index maintenance. Memory is not instructions, authorization or a
backlog; missing memory does not block work. `$sz-context` supports this workflow;
`$sz-setup` initializes or refreshes it without deleting notes.

Use [the documentation map](docs/README.md) to read only the affected sections
and find the owner of each fact. Large catalogs are linked from compact indexes.
Keep durable product facts in tracked docs/code and local investigations in
`<harness-root>/.codex/notes/`. Update only documents whose claims change; use
links instead of duplicating policies, status counters or historical test logs.
Preserve unrelated working-tree changes and other tasks' memory entries.

## Editing and organization

- Keep the root of `scripts/` limited to runnable entry-point scripts. Tool
  sources, manifests, patches and notices belong with their owner under `tools/`.
- Use `apply_patch` for ordinary edits. Scripts are appropriate for generators,
  data processing and bulk transformations with validated matches, safe repeated
  execution and diff inspection. Reuse project automation; keep reusable scripts
  in `scripts/`, but do not add a permanent script for a one-off edit.
- Format task-owned C++ changes with `clang-format -i`, then inspect the diff.
  Use narrow line ranges if whole-file formatting would obscure copied source.
  Do not format unrelated files or run a separate dry-run during iteration.
- Identify the owner and boundary before adding a feature. Widgets own presentation
  and interaction; application state owns navigation/command availability;
  `ArchiveBackend` owns native archive operations and validation. Independent
  `7zG` operations use the existing typed operation boundary described in
  [architecture](docs/architecture.md); do not introduce console argument/output
  parsing. Keep cancellation with the owning operation. Derive command
  availability from state and selection in one place.
- New Qt/integration classes have one responsibility, substantial bodies in
  `.cpp`, focused headers and CMake registration for affected app/test targets.
  Separate UI construction, state transitions, filesystem actions and archive
  results; avoid workflow-heavy constructors/lambdas and hidden mutable globals.
- Review splitting new glue around 400 lines/file, 80 lines/function or a second
  responsibility. These are review triggers, not limits on copied upstream modules.
  Preserve coherent implementation units; an upstream directory tree does not
  define project module ownership. Extract cohesive parts of large custom Qt files
  when practical; keep small fixes focused and explain deferred extraction.
  Do not add independent features wholesale to the main window header or
  unrelated suites to a window-state test component.
- Test public behavior/signals with deterministic local fixtures; do not expose
  internals just for tests. Keep suites focused by component. Extractions preserve
  behavior/parity, move relevant regressions and update architecture boundaries.
  Do not combine broad cleanup with unrelated features.

## Validation and UI reference

### Test usefulness

- Before adding a test, identify the concrete observable failure it detects and
  why existing coverage is insufficient. Adding code does not itself justify a
  new test. A useful test detects incorrect behavior and survives safe refactoring
  that preserves the public contract.
- Do not test mere implementation presence: source text, private symbol names,
  internal call order, file/key existence, trivial getters/assignments, or copies
  of current constants/configuration values. Check configuration through its
  effect on behavior; pin a value only when the value itself is an established
  compatibility, security or public-interface requirement.
- Prefer operation results, public signals/contracts, error handling, boundaries
  and reproducible regressions. For example, verify that Cancel leaves settings
  unchanged, rather than checking that a Cancel handler exists in source.
- When working in an area with implementation-mirroring tests, replace them with
  behavior checks or remove redundant assertions/tests while preserving useful
  coverage. Do not preserve accidental implementation details merely to satisfy
  an old test. Keep this cleanup scoped to the affected area.
- Do not remove a behavior test merely because the tested code now exists or the
  test passes: callers, dependencies and event handling can regress its result.
  For a simple change with no meaningful new regression to cover, use sufficient
  existing checks, a build or a focused manual check instead of inventing a test.

### Validation scope

Linux runner is the primary validation platform. For each code/build change,
build affected targets and run focused checks there first. After Linux passes,
validate the same change on macOS and Windows; those secondary checks may run
in parallel. Fix Linux failures before starting secondary acceptance runs.
If a runner is unavailable, continue independent work and report the missing
platform evidence rather than waiting indefinitely.

Incremental compilation and targeted diagnostics may run during implementation.
Batch remote validation when further related edits are already planned: do not
start remote suites after each minor/nonfinal patch. Run the affected suites and
direct consumers together when the combined change is ready. An intermediate
remote diagnostic is appropriate only to resolve a concrete platform uncertainty,
failure or prerequisite that prevents further work. Repeat remote checks only
when later edits affect their tested behavior or dependencies.

Select focused checks from the changed behavior and its platform dependencies.

For behavior changes, use the [test usefulness criteria](#test-usefulness).
A pre-fix failing run follows the
[porting order](#porting-order-source-comparison-before-runtime-acceptance).
Archive-operation changes cover success, failure and cancellation, with
Unicode/spaces and malicious paths where relevant; never use real user archives
or live services. Build affected targets and run focused tests/direct consumers
with `./scripts/build.sh dev <targets>` and
`./scripts/ctest.sh --preset dev -R '<regex>' --output-on-failure`.
Do not invoke CTest directly.

Focused validation is the default, including before completing an iteration or
commit. Build only the required targets and explain the selected scope briefly. A local
code, test or build-script change does not by itself require the full suite.
Select granular suites for the changed component and its direct consumers with
`verify.py --tests` or the CTest wrapper. Do not run several broad, overlapping
profiles merely because their names relate to the task. Reuse passing evidence
when later edits do not affect the checked behavior or dependencies.
Use the configured build and CTest parallelism. Do not reduce build jobs or
force a sequential CTest run as a precaution; change concurrency only for an
observed problem or an explicit user request. Keep serialization scoped to
individual tests through `RUN_SERIAL` or resource locks when a real shared
resource requires it. Report compilation and test execution time separately
when investigating a slowdown.
Use the default `lite` build mode for ordinary work. For diagnostics, follow
[development](docs/development.md#build-diagnostics): use `debug` for debugger
work, `sanitize` for memory/undefined behavior, or the sufficient individual
option. Scope environment overrides to the relevant command; retain warnings,
input validation and ordinary Debug assertions. Before enabling expensive
symbols, sanitizers or Clang-Tidy, identify the smallest targets, dependencies
and focused tests that answer the question. Do not combine them with `all`,
a full gate or an unrelated matrix for convenience. Whole-project diagnostics
require a concrete cross-component reason or explicit request; explain their
scope and cost first. JSON reports and screenshots are separate opt-ins;
enable reports when the affected geometry needs inspection.
After heavy diagnostics, review disk usage and apply
[artifact retention](docs/testing.md#test-artifact-retention) when the question
is resolved. Remove obsolete task-owned binaries/symbols, dumps, fixtures,
snapshots and reports after checking processes, locks and references. Preserve
current review/failure evidence, useful incremental outputs, caches, tools and
dependencies; never clean unrelated targets or the workspace wholesale.
Report cleanup and any retained large artifacts with their reason.
Expand checks when failures or shared dependencies show a wider impact.
Run `./scripts/check.sh` only for broad cross-component changes whose impact
cannot be covered confidently by focused suites, unresolved integration risks,
or explicitly requested full/release acceptance. State the concrete reason
before launching it; an iteration/commit boundary alone is not a reason.
Run it **unsandboxed**: sandbox DBus sockets and process-priority calls fail.
It checks repository C++ formatting, builds all targets and runs the default
CTest set, excluding tests labeled `slow`. Do not repeat a passing full gate
on unchanged code. After localized follow-up edits rerun affected checks;
repeat the full gate only if the same broader-risk criteria apply.

All validation, including full/release gates on local and remote platforms, must
reuse the existing incremental build and compiler cache. Do not clear or bypass
`ccache`, select an empty cache directory, unset compiler launchers, force cache
misses, or delete/recreate build trees merely to obtain a clean validation run.
Do not change flags, timestamps or toolchain settings solely to invalidate cached
outputs. A request for a full check does not authorize a cold build or cache reset;
those require an explicit user request. Normal cache misses caused by necessary
source/configuration/toolchain changes are allowed. If cache corruption is
suspected, collect diagnostics and report the evidence before requesting a
specific repair; suspicion alone does not authorize clearing caches.

Docs/instruction-only changes need `git diff --check`.
Fix failures caused by the task and report unverified platforms.
Use [development](docs/development.md#local-commands) for wrapper/toolchain details
and [testing](docs/testing.md) for coverage. Wait for buffered formatting to exit
before diagnosing it.

Before each new validation batch, inspect and remove obsolete task-owned test
fixtures, temporary logs and generated reports from completed runs. Follow
[test artifact retention](docs/testing.md#test-artifact-retention) locally and
[runner storage policy](docs/hyperv-runners.md#storage-policy-user-instruction-2026-10-07)
on guests. Do this routinely, not only when space is low. Preserve active runs,
current review/failure evidence, incremental build trees and compiler caches.
Clean disposable task-owned files after validation too; do not create new
snapshot/build directories for each focused rerun when the current one is usable.

Long suites in [cmake/SlowTests.cmake](cmake/SlowTests.cmake) require an explicit
user request, including on runners. Classify new/expanded large matrices as
`slow` before a gate; revisit using measured runtime on an explicit long-test run.
Use [opt-in commands](docs/development.md#long-suites), build requested targets
in the same snapshot before CTest and report excluded suites separately.
A default gate does not establish their acceptance.

Windows 7zFM is the reference for structure, commands, shortcuts and behavior.
Before UI work, inspect exact upstream resources, handlers and callers; record
necessary native-dialog, accessibility, scaling or OS adaptations in parity or
architecture. Preserve the Windows layout where Qt permits and disable unfinished
commands. [Acceptance boundaries](docs/ux-port-audit.md) distinguish completed
UX evidence from checks needed for a new change.

For layout/text/visibility/resize changes, first use the affected
[text specification](docs/ui-layout-contracts.md), normal/compact geometry tests
and current-build widget JSON; inspect grouping, order, sizes, clipping and states.
Add missing geometry coverage when practical. Backend/nonvisual logic/tests/docs
do not require appearance inspection.
For requested runtime comparison with original 7zFM across Windows/Linux/macOS,
use the [agent JSON comparator](docs/ui-runtime-comparison.md) and `sz-ui-parity`.
Keep missing platform/state evidence explicit; a tool-test PASS is not UI acceptance.
Screenshots are a fallback for unresolved fonts/icons/colors/style or a user
request; capture only the affected view (`SEVENZIP_CAPTURE_SCREENSHOTS=1`).
Resources prove structure, not pixels. Identify the concrete reference; without
a rendered Windows reference state that visual parity is unverified.
Never claim cross-platform verification from one OS; report checked platforms
and native scenarios explicitly. [CI scope](docs/ci.md) describes automation.

## Workflows and runners

Use the matching skill in `.agents/skills/`; the
[documentation map](docs/README.md) routes tasks to the relevant workflow.
The Qt porting plan is complete. Its [archive](docs/archive/README.md) preserves
scope, decisions and dated evidence; old statuses, next steps and iteration rules
do not govern new work. Use the current request to choose the affected scenario.
Group related work into complete integrated scenarios before editing;
do not create separate iterations or commits for each helper, failure branch or
regression. Follow the source-comparison and focused-validation rules above.

For Linux/Windows checks read [runner configuration](docs/runner-configuration.md)
and [runner workflows](docs/hyperv-runners.md). Read the global `runners` skill
when using its Hyper-V discovery command. Use
`python3 scripts/hyperv-runner.py run <linux|windows|all> -- <project-script> [args]`
or `check all` with configured guests. Prefer `python3 scripts/verify.py <profile>
--platform <linux|windows|all>` for focused checks. Each guest uses a locked
persistent worktree per Git branch, independent of the local source directory;
source synchronization includes current
tracked and nonignored new source files and deletes removed source files.
Build selected targets after synchronization before running CTest; do not create
per-run build directories. Runner docs own transport/readiness/storage details.
Do not create or attach new virtual disks to the runners. When space is low,
clean obsolete project runs, build outputs and temporary data under the retention
and active-run safeguards in [Hyper-V runners](docs/hyperv-runners.md).
This scoped cleanup is authorized; a capacity warning alone does not require
another confirmation. Preserve active work, toolchains and unrelated data.
After installing or updating a tool inside a runner guest, verify the installed
tool and remove task-owned one-time installers, transferred installation archives,
extracted setup payloads and temporary installer environments that are no longer
needed. Do not retain installation leftovers for hypothetical future use. Check
that no installation is active; preserve installed tools, incremental build trees,
compiler caches, required repair/recovery data, licenses and provenance. Keep
failure evidence until diagnosis is complete and report the cleanup performed.
This guest cleanup does not authorize changes on the physical host.
Historical ARM64 results do not validate these guests.
Do not publish, push or update upstream unless the user's task calls for it.
