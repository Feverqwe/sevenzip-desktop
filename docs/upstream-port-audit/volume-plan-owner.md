# Split/Combine plan ownership — 2026-11-01

## Native worker implementation owner — 2026-11-04

The complete sequence-naming, native split/combine workers, preallocation,
part discovery and plan summary now live together under `fileops/volumes` in
`nativevolumeworker.cpp/.h`. Their output-creation adapter moves from the progress
adapter to the same owner. The File Manager retains its complete Split/Combine
command bodies, dialogs and launch/completion service. All consumers include
the volume interface directly; the progress adapter no longer depends on it.

Compared the complete original PanelSplitFile.cpp at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>; its recorded complete-unit hash still matches.
Reviewed the sequence/worker/command branches and current native I/O, progress,
transaction, input-consistency, command availability and window consumers.
Transferred bodies and declarations are unchanged, with explicit native buffer,
file-enumeration and translation dependencies. The baseline maps the extracted
symbols and original command fragments without changing original hashes.

The existing `sevenzip_panel_split` object target and its compilation context
are retained; no new target, controller or operation state is introduced.
The source summary formatter remains in the File Manager composition through
its original narrow declaration; this does not create a standalone worker
library. Cancellation, short reads/writes, output cleanup/publication, snapshots
and joined completion retain their existing owners and behavior. Existing
operation, workflow, dialog and window-progress regressions are sufficient for
this ownership move; no source-shape test is added.

Linux `volume_operations`, `volume_workflow`, `volume_dialog` and
`worker_window_progress` passed: 93 checks without failures or skips. Initial
compilation identified two resource-header dependencies, made explicit before
runtime checks. The same object target and unchanged native platform adapters
make Linux sufficient for this portable ownership move; no new Windows/macOS
or appearance acceptance is claimed. Configured four-job incremental builds
and compiler caches were preserved; completed temporary settings were removed.

## Preserve the discovered combine plan — 2026-11-03

Rechecked the complete original sequence/discovery, command and worker bodies
with the Qt plans, input validation, progress, output transaction and window
completion consumers. The original inventory and native algorithms remain
unchanged; no external code or new baseline is introduced.

Convenience Combine requests prepare the native plan directly in their worker
before input validation and execution. The public part-list query projects that
same preparation result into its existing path/size response; execution no longer
converts the response back into another native name list. Prepared command plans
are copied once into the worker, as before. Lazy discovery,
contiguous-prefix handling, empty parts, diagnostic precedence, input snapshots,
cancellation and publication rollback retain their existing behavior. Split
preparation and the source completion/window-readiness protocol are unchanged.

## Input consistency snapshots — 2026-11-02

Rechecked the complete original PanelSplitFile unit, its source plans/workers
and current Qt validation, progress, transaction and window consumers. The
original inventory still matches its recorded hash. Native algorithms and
plan preparation are unchanged; no external source is imported or baseline
advanced.

Qt post-operation consistency checks now use one private input record holding
the path and its initial size. Combine no longer coordinates paths and captured
sizes by index in separate lists. Split uses the same existing file-type,
symlink and size check before output publication. Failure messages, check order,
rollback and cancellation remain unchanged. This preserves the existing check's
limits; it does not introduce a content or file-identity guarantee.

Prepared native plans and lazy path requests retain their distinct preparation
timing, including destination snapshots, invalid-size diagnostics and part
discovery. Existing regressions exercise exact round-trip bytes, changed/missing
parts, completed-part growth, source growth/shrinkage, collisions, cancellation,
rollback and subsequent command readiness. No new test is added for the record
representation or private helper.

Linux focused validation passed volume_operations, volume_workflow and
worker_window_progress: 77 Qt checks without skips. This validates the portable
input-check owner and its operation/window consumers; no new Windows/macOS or
appearance acceptance is claimed. Completed test settings and the disposable
reference extraction were removed; incremental builds/compiler caches retained.

## Output transaction records

Rechecked the original workers, naming, preallocation, discovery and command
branches below with current plans, native I/O/progress adapters, window services
and transaction consumers. The complete original PanelSplitFile unit still
matches its recorded baseline; no source import or baseline change is involved.

The Qt no-overwrite transaction now keeps each staged path and its final target
in one output record, replacing parallel lists and paired admission guards.
Registration still precedes native creation, and a failed creation removes its
record. Publication order, successful-prefix counting, stage cleanup before
published-output rollback, commit behavior and error paths are unchanged. Native
Split/Combine algorithms and input-change/cancellation policy retain their owners.

A public operation regression introduces a conflicting second output after all
parts have been written. Failure must roll back the first published part, retain
the other owner's bytes, remove remaining stages and preserve the source. This
adds previously missing late-publication coverage without testing transaction
representation or claiming a pre-fix product failure.

Linux acceptance passed `volume_operations`, `volume_workflow` and
`worker_window_progress`. After strengthening the new regression to compare the
whole remaining directory, only `volume_operations` was repeated and passed;
other passing evidence was reused. This changes portable record ownership,
without native I/O/platform branches, so no secondary platform was selected.
Existing incremental outputs, compiler cache and the four-job default were
retained. No full/slow gate or appearance acceptance is claimed.

Compared the complete original `PanelSplitFile.cpp` declarations, Split/Combine
commands, worker bodies, sequence naming, preallocation, discovery and summary
branches with their current Qt callers, services, progress and transaction owners.
Reference: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. The complete original unit matches
its recorded SHA256
`10d60b7081dbcef2da034ee732d2d4e3ad9b42589ae9b264d2fb3b89bef84bf8`.
The existing baseline retains this original unit and its adapted `.cpp` owner;
the extracted declarations remain local integration in `PanelSplitFile.h`.
No external source is imported, original hash advanced or source algorithm omitted.

The original `CThreadSplit` and `CThreadCombine` parameters now form separate
`CSplitVolumePlan` and `CCombineVolumePlan` records beside their worker declarations.
Commands and discovery prepare these plans
without constructing an unused progress owner. The Qt operation retains the
selected complete plan instead of flattening both sets of fields and storing a
second operation-kind discriminator. A worker receives its own plan copy for
execution; repeated starts retain the original request. Typed dispatch removes
the base-to-worker casts. Source-plan preparation, unsigned volume values and
native paths remain available directly to the workers.

Sequence naming, reads/writes, preallocation and progress branches are unchanged.
Discovery and summary bodies retain their existing implementation with the plan
as owner. Qt validation, change detection, cancellation, no-overwrite publication,
rollback and completion readiness retain their existing boundaries. Convenience
path requests still discover/validate their input when the operation runs.

Existing operation and window suites cover source plans and convenience requests,
Unicode/spaces, varying part sizes, round trips, occupied outputs, changed or
missing inputs, cancellation and rollback. They also exercise two-panel defaults,
new destination folders, confirmation/retry and a subsequent command becoming
ready. No test of private representation or new product failure is claimed.

The six focused suites passed on Linux (7.18 seconds), macOS (4.36 seconds) and
interactive Windows (16.69 seconds): volume_operations, volume_workflow,
volume_dialog, split_destination, combine_names and worker_window_progress.
Each platform passed 115 Qt checks without skips. Current normal/compact dialog
reports were retained (26 per platform), with no clipped visible controls;
they use offscreen Qt on Linux/macOS and both native/offscreen Qt on Windows.
This is focused behavior/geometry evidence, without a full/slow gate or a rendered-original
visual comparison. Existing incremental builds and compiler caches were reused;
completed disposable fixtures were removed after preserving results.
