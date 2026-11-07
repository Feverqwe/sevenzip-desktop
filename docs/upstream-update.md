# Reviewing an upstream 7-Zip update

The recorded 7-Zip source baseline is [upstream-baseline.json](upstream-baseline.json).
It records the official repository, full default commit and every
borrowed unit. `exact` identifies unchanged dependency units in the local source
archive; their logical names retain the dependency's internal structure.
`original_archive` pins the local container, and `originals` pins complete original
members with SHA256. `origins` explicitly maps borrowed source owners to upstream
paths; it does not infer provenance from their local directories. `adapted` files belong to
project modules and need code review; original paths identify their provenance,
not their required local location. `local` units are project integration rather
than copies of an original inventory unit. `overrides` records full commits for
individual modules if an update is applied incrementally. The function/branch
scope and reasons for local changes
remain in [the port audit](upstream-port-audit.md).
`original_sha256` pins complete original inventory units for new selective imports;
`verify` checks these hashes against each owner's recorded revision. Exact-file
revision overrides also enter the native runtime snapshot and its cache/provenance,
so a selective codec update reaches both static consumers and the standalone engine.

Archive readers with Linux dependencies have one implementation in their owning
format modules, following the project's
[module directory organization](architecture.md#module-directories-and-source-ownership).
Both the application and console load these format modules. The [reader ownership
audit](upstream-port-audit/license-variants.md) records the shared portable support
units. `extracted` maps transferred symbols and local units to their original
handler owner; `original_sha256` pins that complete original inventory unit.
Review these units together with the full reader when updating upstream.

`runtime_adapted` selects adapted engine modules for both dynamic and standalone
runtime builds. Review these local changes during an upstream update as well;
their compiled provenance and prepared-runtime cache include the source hashes.
The current builders refresh one owned source snapshot while retaining incremental
compiler outputs. They do not compile in the upstream checkout. GPL reader
sources are excluded from this overlay and built as separate CMake modules;
the snapshot preparer rejects a full GPL reader in `runtime_adapted`.
`runtime_dependencies` adds non-7-Zip C modules and headers needed by these
modules; `runtime_paths` explicitly maps their owners into the native snapshot.
Colliding destination paths are rejected. Their source revisions and licenses
belong in the existing port audit. Runtime fingerprints and transitive dependencies
include them; generated native rules compile C sources as C while preserving
incremental build outputs. `runtime_format_sources` selects dependencies linked
only into Format7zF, such as probes and HFS compression shared with APFS. The
console executable and original SFX do not link these format support objects.

Native source snapshots read the repository-owned source archive, verify its
container and member hashes, and apply explicitly mapped local owners.
`dependency-snapshot` expands only unchanged units for application builds, so
original headers do not mask their adapted owners. `header-snapshot` creates
forwarding headers for foreign include contracts; these refer to live project
owners rather than copying their implementation. Native snapshots rebase project
include paths into their generated build view. `runtime_paths` records
destinations for non-7-Zip native dependencies; `runtime_excluded` records owners
that must remain in external format modules. Preserve original revisions/hashes
when moving a project owner. Upstream Git access remains appropriate for the
verification and update-review commands below.

Native UI reference builds use `runtime-snapshot --original-only --no-build-rules`
to retain the original makefiles as well as source bytes. This mode is separate
from runtime/SFX integration, which still generates the required build rules.

Start from a disposable Git snapshot of the official repository. The old
`../7zip-upstream` checkout is not required, and it must not be modified:

```sh
git clone --no-checkout https://github.com/ip7z/7zip.git /tmp/7zip-update-review
python3 scripts/upstream_baseline.py verify --upstream /tmp/7zip-update-review
```

`verify` rejects missing/stale manifest entries, duplicate source owners across roots,
unknown revisions, changed original archive members and changed exact bytes.
It checks the current working-tree inventory, including
nonignored new sources and excluding deleted files, so source moves can be
reviewed before staging.

Before changing a module, produce an old-to-proposed review:

```sh
python3 scripts/upstream_baseline.py review \
  --upstream /tmp/7zip-update-review --new-revision <full-commit> \
  --output /tmp/7zip-update-report.md
git -C /tmp/7zip-update-review diff <old-commit> <full-commit> -- CPP/7zip/UI/FileManager/NetFolder.cpp
git -C /tmp/7zip-update-review checkout <full-commit>
git diff --no-index /tmp/7zip-update-review/CPP/7zip/UI/FileManager/NetFolder.cpp src/filemanager/providers/NetFolder.cpp
```

The report lists changed recorded files and marks exact versus adapted owners.
For each changed adapted module, inspect the upstream diff, current local
deviations, callers and changed dependencies before transferring the relevant
source change. Keep Qt/platform/native-module adapters and local fixes; do not replace
adapted files wholesale. For unchanged vendors, copy exact new bytes only after
reviewing dependent code. Check omitted functions and branches in the audit as
well: a change there can still affect a copied dependency. Update the audit
with old/new full revisions, source paths, retained adaptations, deferred
changes and reason. Change the manifest default revision only if every recorded
module moves together; otherwise add per-path `overrides`. Update parity docs
when the workflow changes, add meaningful behavior regressions and run affected
checks. Use `./scripts/check.sh` only for the broader integration risks defined
in [AGENTS.md](../AGENTS.md#validation-scope). A failed check leaves provenance at the
last validated revision.

Upstream translations have separate provenance in
`third_party/7zip-help/provenance.json`. Compare the recorded language files
with the matching official release installer. Preserve exact member bytes and
update their provenance after review, including when a member is unchanged in
the new release.

Early verification and synthetic-review evidence is preserved in
[the tooling archive](archive/qt-port/workflow-evidence.md).

## External reference baselines

ZIP's libzstd encoder and its 7-Zip-zstd adapter use the same reference
verification/review commands with explicit source-to-owner maps. Their pinned
revisions, imported scope and example commands are in the
[ZIP encoder audit](upstream-port-audit/zip-zstd.md).

QEMU follows the Linux reference-baseline model: original inventory units and
SHA256 values are pinned to a retrievable commit, with an explicit mapping to
local source owners. Local paths follow project boundaries and need not mirror
the external repository. [QCOW's baseline](qcow-qemu-baseline.json)
maps the borrowed QEMU header to its local owner. The
[QCOW source audit](upstream-port-audit/container-layers.md#qemu-read-side-update--2026-10-24)
records transferred symbols, dependencies, omitted scope and licenses.
The 7-Zip handler keeps its separate 7-Zip provenance.

Use a separate QEMU Git checkout containing the recorded and proposed commits:

```sh
git clone --filter=blob:none --no-checkout https://github.com/qemu/qemu.git /tmp/qemu-update-review
python3 scripts/upstream_baseline.py reference-verify \
  --manifest docs/qcow-qemu-baseline.json --upstream /tmp/qemu-update-review
python3 scripts/upstream_baseline.py reference-review \
  --manifest docs/qcow-qemu-baseline.json --upstream /tmp/qemu-update-review \
  --new-revision <full-commit> --output /tmp/qcow-update-review.md
```

The report compares complete original units and shows the local adaptation
against the recorded original. It does not change sources or advance the
baseline. Review dependencies and omitted branches before transferring changes
into the partial port; merging the complete QEMU header over the selected
helpers would introduce unported block services. Retain local fixes, validate
the affected reading scenarios and update original hashes/revision together
with the source audit. `--path` can restrict review to recorded upstream paths.
The same reference commands accept the existing Linux baseline manifests;
explicit adaptation maps additionally enable local-difference reports.
