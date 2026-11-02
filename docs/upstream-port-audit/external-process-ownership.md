# External-process ownership

## Editor settings signal routing — 2026-11-02

Compared complete original EditPage.cpp/.h/.rc and its EditPage2.rc controls at
recorded revision `b88c6bc09339de32f7d18bbab900686b244f42ba`; the three original
inventory hashes match the baseline. Reviewed current page initialization,
Apply/Cancel/modified state, Browse and OptionsDialog/launch consumers. No
external code import or provenance change.

Each Qt edit signal now marks its own settings row, and each Browse button
directly supplies its edit control. The synthetic Win32-ID round trip, two
dispatch searches and their stored IDs are removed from the Qt adapter. Native
initialization guards, changed flags, read/save mapping and command parsing are
preserved. Browse still retains the draft on Cancel and replaces parameters
when a file is selected. Confirmation policy, launch defaults, translation
resources, widget names, layout and geometry are unchanged.

Expanded the existing Browse behavior check across viewer/editor/diff: Cancel
retains the command, acceptance selects the Unicode/spaced executable and Apply
persists it for the corresponding program. Existing checks cover Apply/Cancel,
live language/geometry, focused-file arguments and independent saved commands.
No representation-only test or pre-fix failure is claimed.

Linux external_editors_ui, options_dialog and external_launch passed: 47 checks,
no skips; incremental build 13.057s, tests 5.372s. Removed completed settings and
an absent-object temp record; incremental trees and compiler caches retained.
No secondary-platform, full/slow or new appearance acceptance run.

## Failed listing during editor return — 2026-11-02

Reviewed the current stream/virtual-file/disk opening branches, temporary and
external-process owners, changed-file prompt and return-to-writer boundary.
Compared the original OnOpenItemChanged bodies at recorded 7-Zip revision
`b88c6bc09339de32f7d18bbab900686b244f42ba`; the complete PanelItemOpen.cpp
hash matches the existing manifest. No import or provenance advancement.

The returned edit now stops at a failed GetNumberOfItems before resolving its
item or starting an archive update. Previously the inherited code used an
uninitialized or failed output count. Initializing the count alone would not
protect against a provider writing a nonzero value before returning failure.
The native CAgentFolder already clears count when unbound; a user failure in
that provider is not claimed. Item relocation, duplicate-name limitation,
passwords, process waiting, native writer and retained temporary-copy policy
remain unchanged. The existing ownership boundaries did not justify another
structural extraction.

The behavior regression uses the existing folder fixture returning a failed
nonzero count. The changed copy must retain its bytes, no reload selection is
published and the unsupported-operation dialog must not be reached. Existing
external editing and native update tests cover successful write-back, refusal,
cancel, stale item/location and selection publication. No pre-fix execution or
representation-only test is claimed.

Linux archive_external_edit and source_update_backend passed: 63 checks, no
skips; incremental build 14.6s, tests 11.244s. Removed completed settings and
three registry records after verifying their temporary objects were absent.
Incremental trees and compiler caches retained. No secondary-platform,
full/slow or new appearance acceptance run.

## External editor launch boundary — 2026-11-01

Reviewed the complete source SplitCmdLineSmart, StartAppWithParams,
StartEditApplication, EditItem and both DiffFiles bodies and native CProcess
creation against the existing baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>. PanelItemOpen.cpp's complete original hash
matches the recorded inventory. The current disk/archive callers, association
adapter and process creation/handle/reaper implementations were compared before
changing the launch boundary. No source algorithm is replaced or imported.

QtNativeProcessServices now owns both waitable and detached executable launches.
QtAssociatedFileServices delegates creation while retaining system associations,
prompts and archive-update integration. The two launch paths share parameter
preparation: original quoted path appending on Windows, and the existing Qt
parameter splitting plus literal path arguments on POSIX. Native CProcess and
posix_spawn ownership, process groups, preallocated reaper, metadata-failure
handoff and detached QProcess launch retain their respective behavior. No shell
is introduced. Source selection, command parsing/defaults and editor wait/update
policy remain unchanged; no UI layout/text changes or new baseline units.

The existing real-child argument fixture now also exercises the waitable route.
It detects lost/split Unicode or space-containing paths, shell interpretation of
literal path characters, and failure to return a usable process owner. Both
routes still check an unavailable executable. Existing keyboard View/Edit/Diff,
archive editing and process-allocation regressions cover the direct consumers.

Linux passed all four selected suites first: 40 Qt checks without skips plus
process_spawn_allocation; run `20261101T191813Z-2e84ba64`, build 68.864 seconds,
tests 15.756 seconds. Interactive Windows passed the same three behavioral
suites (40 Qt checks without skips) plus process_handle_allocation; run
`20261101T191952Z-833385b7`, build 142.450 seconds, tests 22.690 seconds.
macOS passed external_launch (17 checks without skips) and
process_spawn_allocation. Its first launch suite stopped four archive fixtures
because the local reference runtime was absent; normal codec-only preparation
reused the compiler cache (3.40 seconds), then only external_launch was repeated
and passed (build 0.592 seconds, tests 4.468 seconds). The first macOS selected
build took 11.295 seconds. No product fix was needed for that prerequisite.

Disposable fixture settings and records whose temporary objects no longer
exist were removed after checking completed runs/processes. Incremental builds,
prepared runtime and compiler caches remain available. Slow native-publication
matrices and new visual parity acceptance were not part of this change.

## Child tracking ownership — 2026-11-01

Compared complete original PanelItemOpen child discovery, process tracking,
wait/update and temporary-file worker bodies and dependencies at the recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` revision in
<https://github.com/ip7z/7zip.git>, with current launch, GUI bridge, owner-exit
and archive-publication consumers. The complete original unit matches its
existing baseline hash. No external code is imported or baseline advanced.

Each tracked Qt process now keeps its owned handle and wait flag in one record,
using the existing native object-vector owner. Main and discovered handles stay
guarded until their record exists. Record destruction releases the interface;
the parallel wait-flag array, paired reservations and raw-handle cleanup loop
are removed. Original PID admission, child/name discovery, wait projection,
timing decisions, changed-file prompts, archive writer and externally protected
copy policy retain their existing control flow and owners. Native process
adapters are unchanged, including platform-specific close/reaper behavior.

Existing public editing/lifetime/security/launch suites cover a child outliving
its parent, update acceptance/refusal/cancellation, window exit, protected
archives and exceptions from name lookup, snapshot or launch after handle
creation. No representation test or pre-fix product failure is added.

Linux acceptance: `external_launch`, `archive_external_edit`,
`archive_external_lifetime` and `archive_external_security` pass. Linux is the
selected platform for this interface-level ownership change; no native adapter
branch changed. Windows/macOS acceptance is not claimed. The initial build hit
guest OOM before tests; the unchanged incremental retry passed with configured
parallelism and compiler cache, without memory or concurrency overrides.

## One-shot reaper handoff — 2026-10-31

Reviewed the complete native process adapter, creation and adoption paths,
source child tracker, wait/close and temporary-file launch owners against
PanelItemOpen.cpp at the recorded revision
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` in
<https://github.com/ip7z/7zip.git>. Its complete original hash matches the
existing baseline. Source command parsing, child discovery, wait policy and
archive update behavior are unchanged; the POSIX reaper remains a local
platform service without a usable Win32 implementation to import.

The reaper's single PID handoff now uses `std::latch` rather than a separate
mutex, condition variable and released flag. PID assignment precedes release;
the waiting thread reads it after the latch opens. Each reserved state receives
exactly one release, either from owner destruction or the thread-detach failure
cleanup, which joins before unwinding without publishing that state. Reaper
allocation remains before spawn. Closing an owner still neither allocates a
thread nor waits for an editor, and a metadata failure after launch still
returns the valid process owner. No Windows branch, external code baseline or
source algorithm changes.

Existing process regressions exercise a real child after post-spawn allocation
failure and a pipe-blocked child whose owner is released with allocation failure
armed. Launch, archive lifetime/security and focused native update consumers
cover the complete path through the retained source workflow. No new
implementation-mirroring test or pre-fix product failure is claimed.

Five focused suites passed on Linux (28.86 seconds), macOS (31.59 seconds)
and interactive Windows (33.94 seconds), without skips. Each platform passed
46 Qt checks across external_launch, archive_external_lifetime,
archive_external_security and native_external_update, plus its allocation
ownership executable: process_spawn_allocation on POSIX and
process_handle_allocation on Windows. The two large native external-publication
matrices remain excluded as slow. Incremental builds and compiler caches were
retained; disposable fixtures were cleaned after capturing results. These are
source-owner and focused runtime results, not full-suite or visual acceptance.

## Registration and allocation failures — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, reviewed commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. The existing PanelItemOpen port
and CSnapshotProcess provenance also retain their initial baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.
Reviewed complete source snapshot/child-process/wait/launch owners in
`CPP/7zip/UI/FileManager/PanelItemOpen.cpp`, native StartApplication branches
in BrowseDialog2, and AutoPtr, Handle and ProcessUtils dependencies. The
corresponding QtNativeProcessServices, QtAssociatedFileServices, archive editing
caller, worker entry and temporary-copy leases were traced through cleanup.

The Windows adapter had replaced the source snapshot's scoped owner with a raw
Toolhelp handle across allocating operations. OpenProcess also acquired its OS
handle before allocating the interface owner. Both now reuse the original
CHandle/CProcess RAII types. The snapshot enumeration and process identity
algorithm remain unchanged. A separate launch boundary allocates the interface
owner before native CreateProcess or the original association launcher runs;
a valid native process is transferred even if the launcher reports an error.
The source native launch implementations remain authoritative.

POSIX creation previously read allocating metadata after posix_spawn before
owning the child. An allocation failure left a child without a reaper. The
adapter prepares its path before launch and records child ownership immediately
after success. Metadata allocation failure returns E_OUTOFMEMORY with the valid
process owner, allowing the caller to retain the externally used file. Reaper
resources are reserved before spawn; close only hands the PID to that retained
state, without allocating a thread or waiting for the editor to exit. POSIX
process identity, wait and orphan-reaping services have no usable Win32 source
implementation; their existing narrow adapter remains in place.

The source child tracker now retains each received handle with CMyUniquePtr
through registration, and reserves both parallel arrays before publishing a
pair. Allocating folder/path metadata is prepared before launching an editor.
A scoped output guard also covers exceptions inside launch/error reporting;
transfer clears the output before handing ownership to the child tracker.
The queued GUI bridge now returns exceptions from its callback or readiness
predicate to the waiting worker and completes the semaphore handoff even on
failure. The native worker boundary catches exceptions, unwinds its owners and
preserves an unproven externally protected copy rather than allowing an exception to
escape the OS thread. Child discovery, wait policy, update prompts, archive
publication and normal cancellation retain their source algorithms and branches.
No source workflow or dependency is omitted.

Regressions observe real Windows handle counts across failed snapshot/open
allocations, and POSIX process ownership/reaping after a failure immediately
following a real spawn. A pipe-controlled child also verifies that releasing its
owner remains safe with allocation failure armed and returns before child exit.
The POSIX adoption leak was reproduced on macOS before the fix; reaper creation
inside a noexcept destructor was established from source without deliberately
terminating a parent and leaving its private child behind.
Public archive-editing fixtures also fail a name query, a worker snapshot, or a
launch after returning a handle; they verify released handles, a retained
protected copy and unchanged archive bytes. These checks do not expose private
trackers, read source text or pin allocation counts. A separate real cross-thread
GUI-bridge suite verifies callback/predicate exceptions and cancellation before
readiness; exceptions must not escape the GUI loop or strand the worker.

Focused application builds and process/GUI-worker regressions passed on macOS,
Linux and Windows. Windows also passed all four native association scenarios;
the POSIX-only reaper refinement was rebuilt and rechecked on macOS (3/3,
3.55 seconds) and Linux (3/3, 3.49 seconds; runner
`20261018T110904Z-b65fddac`). The earlier Windows check
`20261018T105902Z-fdcdaf37` passed its eight selected scenarios; the later
attempt did not start on Windows because another run owned the worktree.
No Windows production branch changed between those checks.

This is focused ownership evidence, not full-suite or UI parity acceptance.
The earlier broad run still has separate translation/UI failures and a Windows
native-shell shutdown crash after its functional assertions pass. Those remain
outside this process-registration fix. Slow suites were not run.
