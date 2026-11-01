# Split/Combine plan ownership — 2026-11-01

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
