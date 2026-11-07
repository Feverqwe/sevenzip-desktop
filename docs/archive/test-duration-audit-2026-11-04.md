# Test duration audit — 2026-11-04

This audit starts from `f3e0b48b9dabc7e533bf1de97cf15985a09b1d41` in
`codex/test-duration-audit`. It reviews
CTest execution time separately from runtime preparation and compilation.
No application algorithm or upstream baseline is changed. Windows validation
also uses the existing master fix
`21ae5193d4529ab2e08b2c7ab7ca05d1a8420adc`: explicit constructors for two APFS
scope guards, preserving their previous aggregate initialization. This audit
reuses that master fix unchanged.

## Method

The primary measurement is an exhaustive Linux x86-64 `dev` run (GCC 13.3,
Qt 6.12), including the previously excluded suites, using the existing
incremental build and compiler cache, ordinary `lite` diagnostics and the
configured four CTest slots.
Per-suite times come from that run's `LastTest.log`, not from the moving
averages in `CTestCostData.txt` or from CMake timeout values.

The user-selected threshold is 30 seconds in an ordinary build,
not an automatic CI assertion. Borderline results need repeat measurements.
Memory-heavy boundary checks and native integration prerequisites need separate
consideration. A skipped native scenario does not establish its runtime.
Linux results do not establish native Windows or Cocoa performance.

For a newly excluded suite, a `_quick` registration runs selected existing
behavior checks through the same executable. The full suite keeps its name and
all its scenarios; `SEVENZIP_INCLUDE_SLOW_TESTS=1` enables it. These selections
retain basic success, failure and cancellation where applicable, while full
cross-products remain opt-in. They are not acceptance of omitted combinations.

## Classification changes

| New `slow` suite | Linux full run, seconds | Quick selection, seconds | Default coverage |
| --- | ---: | ---: | --- |
| `ntfs_stream` | 43.65 | 11.08 | Reads, malformed inputs, retry/cancellation and selected extraction |
| `codec_update` | 39.58 | 18.67 | Successful updates in each format, failure/cancellation, ownership, stale generations and path safety |
| `ui_copy_routing` | 49.08 | 5.66 | Copy/Move, modal selection, archive routes and cancellation |
| `archive_reload_command` | 42.27 | 2.53 | ZIP/7z, both panes and command/keyboard refresh |
| `drag_reload_completion` | 58.32 | 7.34 | Both panes, successful drop, failure and cancellation |
| `panel_activation` | 96.12 | 5.65 | All modifiers, both input routes, failed opening/retry and missing-folder recovery |
| `panel_key` | 31.31; repeat 31.57 | 13.23 | Each keyboard workflow with fewer view/source combinations |

`panel_creation_path` completed in 29.05/29.07 seconds. Its `slow` label is
removed on Linux; other platform exclusions are retained. `binding_lifecycle`
remains ordinary after 28.63/28.20-second measurements. No other previously
ordinary Linux suite exceeded the selected threshold in the complete run.

On macOS, `sparse_image_ui` completed in 55.44 seconds and repeated in 59.37
seconds, versus roughly 21 seconds on Linux. It is now `slow` only on macOS;
its Cocoa quick selection completed in 22.30/22.14 seconds. Linux keeps the
complete suite in its ordinary set.

The audited ordinary Linux selection contains 524 suites, including 11 quick
selections. Native Windows ran those same 11 quick selections successfully;
their individual times ranged from 3.90 to 20.50 seconds, with no Qt skips.

The measured times are dated evidence, not performance guarantees or fixed
configuration requirements. The authoritative labels remain in
[SlowTests.cmake](../../cmake/SlowTests.cmake), including platform conditions
registered by individual components.

## Previously excluded Linux suites

These are successful complete measurements from the original run or its
focused corrections. The batches differ; this table is not a wall-time sum.

| Suite or family | Seconds | Classification |
| --- | ---: | --- |
| `native_rename_2`, `native_rename_301` | 307.27; 354.04 | Retain `slow` |
| `native_external_2`, `native_external_301` | 201.01; 229.04 | Retain `slow` |
| `native_renderer_failure`, `native_renderer_copy` | 217.22; 93.14 | Retain `slow` |
| `native_nested_stream`, `native_nested_memory`, `native_nested_disk` | 170.91; 172.34; 172.38 | Retain `slow`, add quick selections |
| `interarchive_refresh`, `interarchive_recovery` | 159.81; 145.91 | Retain `slow` |
| `panel_creation_publication` | 278.80 | Retain `slow`, add quick selection |
| `copy_archive_refresh` | 64.16 | Retain `slow` |
| `panel_creation_path` | 29.05; 29.07 | Ordinary on Linux |
| `fat_maximum` | 1.98 | Retain resource opt-in |

## Regressions exposed by the excluded suites

The initial `native_nested_stream` run failed all 128 publication rows because
it required a physical spool file. The direct-stream opening had succeeded;
this requirement contradicted both the recorded upstream stream-opening path
and the current adapter. The test now copies an entry from the opened child
through `IFolderOperations` and checks its bytes after refresh. If opening did
create a temporary file, returning to the parent still checks its cleanup.
Reading must work with either storage strategy.

The initial disk run failed its 64 small-list publication rows. The delegating
test handler returned early for a nonmatching property fault and bypassed its
independent disk-route control. The fixture now applies only matching faults
and lets the other control handle its request. This repairs test injection,
not the application decoder.

The full panel-publication replay uncovered another stale assertion: its native
ZIP/7z rows expected a callback from the controlled transport backend, which
native opening legitimately bypasses. Controlled filesystem/transport rows keep
the publication-order observation. Native rows check both published panels,
selection, refresh, collapse/expand and unchanged input bytes. The low-level
creation policy still covers successful, failed and cancelled initial loading.
Backend call counts are compared across collapse/expand rather than pinned to
one opening implementation. A quick selection covers all input kinds.

Quick stream, memory and disk selections cover successful publication,
failure/cancellation, recovery and child extraction in the default set. They
reuse the same cases as the full matrices, so both failures are observable
without routinely replaying every presentation combination.

The nested routes previously shared the global artifact lock despite having
route-qualified report names and private runtimes. They now serialize full and
quick selections of the same route, while different offscreen routes can run
concurrently. Native Windows desktop locking is retained.

## Aggregate deadlines

The initial Linux Rename matrices passed in 307.27/354.04 seconds at a 360-second
cap; External passed in 201.01/229.04 seconds at a 240-second cap. The larger
cases left only six/eleven seconds of reserve. Their Linux caps now allow twice
the measured time rounded up to a minute. The memory nested matrix similarly
passed in 170.09 seconds at a 180-second cap.

The renderer-failure matrix completed in 217.22 seconds after its initial
180-second timeout. Interarchive recovery and refresh completed in
145.91/159.81 seconds after their initial 120-second timeouts. Individual state
waits are retained.

After fixing the native observer, the complete panel-publication matrix passed
in 278.80 seconds. Its Linux aggregate cap is 600 seconds. Its quick selection
passed in 6.43 seconds.

`fat_maximum` passed in 1.98 seconds. It remains opt-in because it exercises
roughly 1 GiB of decoded metadata; its exclusion is a resource boundary, not a
claim that it takes minutes.

## Validation and retention

The original exhaustive selection contained 534 suites: 528 passed, two failed
assertions and four timed out. Compilation took 920.39 seconds; CTest execution
took 3474.60 seconds. The first combined correction check rebuilt in 9.09
seconds and executed 30 suites in 1013.38 seconds: 29 passed and the remaining
panel-publication failure exposed the stale native observer described above.

Original and correction-run `LastTest.log`, cost data and CTest registration
were retained before the next run could overwrite them. Disposable scratch
from the six original failed suites was removed after confirming that their
processes had exited; approximately 17 MiB was discarded. Incremental build
trees, prepared runtime and compiler caches were preserved.

After the combined check, the corrected panel-publication quick/full selections
passed separately. The master APFS constructor prerequisite passed the existing
Linux directory and sparse-image UI checks. No unchanged multi-minute matrix was
repeated merely for the compiler prerequisite.

Windows interactive/elevated verification passed all 11 quick suites. Its build
took 473.37 seconds and CTest took 125.45 seconds. The first attempt stopped in
runtime preparation at MSVC C4623 before tests; using the existing master fix
resolved it with the normal warning policy intact.

The final macOS Cocoa full/quick pair passed after the same prerequisite:
runtime preparation 3.29 seconds, incremental build 0.92 seconds, CTest 81.51
seconds. Both original and final Cocoa logs were retained.

The audit does not establish a full Windows/macOS gate, pixel parity with
original 7zFM, native media/network opt-ins, or Windows ASan `ext` acceptance.
Their existing special exclusions remain. No screenshot capture, new diagnostic
build mode or cache clearing was used.

## Integration follow-up

The audit was replayed onto master `2a84e68d7e548e3de49c11f8e0635ca233fd562b`.
All 11 quick selections and `ctest_selection` passed on Linux after refreshing
the prepared runtime, reusing the existing build tree and compiler cache.
The complete-matrix timings and selection counts above describe the original
audit baseline. The existing master APFS constructor fix is unchanged and does
not appear in the integrated audit diff.
