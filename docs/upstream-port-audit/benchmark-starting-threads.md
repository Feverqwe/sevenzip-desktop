# Benchmark starting-thread validation — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. The port retains its original
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17` provenance. Reviewed the
`CPP/7zip/UI/Common/Bench.cpp` property parsing, CPU calibration, hash/filter
thread-count table, callback and ordinary benchmark branches; `Bench.h`,
MethodProps numeric/thread parsing, the original BenchCon caller and
BenchmarkDialog property/worker forwarding, plus QtBenchmarkServices.

The original `mts` parser accepts zero. In the hash/filter table branch, the
thread-count loop then appends zero repeatedly: doubling zero never reaches
the configured thread limit. There is no cancellation point inside that loop,
so a valid cancellation callback cannot stop its unbounded allocation.
Nonzero values cannot overflow the doubling step in this branch because the
existing upper bound on the requested thread count limits loop entry.

The adaptation rejects zero with E_INVALIDARG immediately after the existing
numeric parser, using the same validation style as neighbouring properties.
The original table construction, valid thread counts, compression/hash work,
source callbacks and cancellation remain in place. No dependency, source body
or significant branch is omitted or replaced. The original GUI's separate
property parser remains unchanged; this fix protects the compiled Bench entry
and its Qt service boundary. The external official engine binary is not patched.

The regression calls Bench with CRC32 and `mts=0`, requiring E_INVALIDARG.
Its callback returns E_ABORT if benchmark work starts, safely bounding a run
on old code before calibration or the allocation loop. The actual pre-fix run
returned E_ABORT instead of rejecting the input (53 ms). It also verifies that
`mts=1` reaches the existing cancellation contract. Existing native-benchmark
checks cover successful encode/decode callbacks, cancellation and dialog join.
This validates the public result without consuming host memory to reproduce
the source-visible infinite loop.

The complete native_benchmark suite passed on macOS (16.66 seconds), Linux
(3.46 seconds) and Windows (included in a two-suite run, 2.71 seconds).
Runner evidence: `20261018T111855Z-8a7993af`, source snapshot
`36f586e4bf94028405efb29c734d1f253577ecbaaa5a40df532b0d0455bbba35`.
The local wrapper first stopped in codec-catalog initialization because PATH
selected a system 7zz with a different version; rerunning with the existing
project runtime passed. No codec or benchmark test assertion was relaxed.

No UI geometry or text change is introduced by this validation; slow suites are
not included. The selected suites do not establish whole-project acceptance.

## Qt action ownership — 2026-11-03

Reviewed benchmark initialization, parameter changes, restart/stop/cancel,
completion/join and report copying with the Qt control builder and File Manager
command consumer. Compared original command/button/cancel handlers and resources
at `b88c6bc09339de32f7d18bbab900686b244f42ba`; complete BenchmarkDialog.cpp,
BenchmarkDialog.h and BenchmarkDialog.rc hashes match the recorded baseline.
No original algorithm, source import or provenance revision changes.

The Qt footer now creates and connects its Restart/Stop actions directly.
Removed the separate button factory and its unreachable Close/Help dispatch,
along with unused standard-button caption/binding branches. Close/Help continue
through the existing standard button-box signals. Source command IDs, handlers,
worker state, creation/registration/connection order, action roles, translations,
default button, tab order and report behavior remain unchanged. The source
benchmark algorithm and its state transitions do not need another wrapper.
Existing lifecycle and command checks cover the public behavior; no new test
pins private factories, resource maps or the current control structure.

Linux `benchmark_lifecycle` and `benchmark_command` passed: 25 Qt checks,
no failures or skips; incremental build 17.174s, tests 6.084s. Existing normal/
compact and live-language checks passed without a layout/text change. Completed
settings were removed; incremental outputs and compiler caches retained.
No secondary platform, heavy benchmark, full/slow suite or new rendered-original
appearance acceptance is claimed.
