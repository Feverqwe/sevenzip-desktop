# External-process ownership

## Waiting belongs to the tracked-process owner — 2026-11-04

Compared complete original PanelItemOpen child discovery, process records,
wait/update loop, changed-file handling and exit/cleanup dependencies at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; the complete original source hash
matches the baseline. Reviewed current platform process statuses, launch,
registration, GUI update, temporary-copy protection and joined shutdown.
No source is imported or provenance advanced.

CChildProcesses now keeps its tracked records private and owns the complete
wait projection/poll/completion step. The worker receives an explicit empty,
process-exited, stopped or error result and the admitted count, instead of
reconstructing borrowed handle/index records and decoding count-relative
integer values. A completed entry is marked inside the owner before returning;
the subsequent timestamp check and child snapshot retain their order, and no
handle is released or external callback added at that transition.

Admission order/bound, polling and exit priority, first-pass filename discovery,
short-process timing, later snapshots and proof required before releasing
external temporary protection are preserved. Launch, discovery, archive update
and thread registration bodies were compared unchanged. Empty waits, stopping,
errors and exceptions retain their previous cleanup/return behavior. Native OS
services, archive operations and UI behavior are unchanged; no new controller,
public API or persistent state is introduced.

Linux external_launch, archive_external_lifetime and archive_external_security
passed, including real child survival, unchanged short/long lifetimes, owner
exit, launch/wait/snapshot failures and protected archive write-back. Compilation
took 15.055 seconds, tests 16.927 seconds, using configured four-job parallelism,
incremental outputs and compiler cache. Existing behavior coverage was reused;
no getter/private-layout tests were added. Inventory and whitespace checks
passed. Windows/macOS, full/slow gates and visual acceptance were not repeated.

## Archive-item opening owner — 2026-11-04

The complete archive-opening path now lives in `panels/PanelArchiveOpen.cpp`,
inside the existing panel target: native OpenAsArc result/error handling, direct
name/index opening, changed nested-archive return and OpenItemInArchive's
stream, bounded memory/spill, disk fallback and external-session handoff. Item
launch policy, filename warnings, ordinary external launch, comparison and
changed-file update presentation remain in PanelItemOpen. The common extension
predicate is a private static panel method; its body and extension inventory
are unchanged. No controller, state or public operation interface is added.

Compared complete original opening/return/member-opening bodies, folder-link
and temporary-file types, callers and native update dependencies at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. The complete original PanelItemOpen.cpp hash
matches the existing baseline. The transferred current bodies are byte-identical
apart from terminal whitespace. Existing typed manager, ownership lease,
password/progress, container depth, materialization, zone/ADS and async refresh
adaptations are retained; no decoding, recovery or external code is imported.
The baseline extends the existing extracted-symbol mapping and preserves its
previous external-session owners and original hashes.

The RAM source fixture compiles the new opening unit together with its existing
FM and launch units, retaining the same RAM-query override. Production and
fixture target boundaries and compile options remain unchanged. Existing
behavior regressions are reused for nested stream/materialized opening, parent
restoration/update, external launch, errors/cancellation and RAM/disk choices.

Linux source_extract_backend, archive_item_open, panel_navigation, nested_update,
external_launch and file_manager_ram passed: 58 Qt checks, no failures or skips.
Final incremental compilation took 63.911s and test execution 10.572s, with the
configured four jobs and compiler cache. Initial compilation identified missing
explicit includes for opening error resources and the HRESULT formatter; these
were restored before runtime acceptance. Source inventory and whitespace checks
passed. No platform branch, layout or text changed; Windows/macOS, full/slow
suites and new appearance acceptance are not claimed.

## Tracked external-file session owner — 2026-11-04

The complete tracked launch, child discovery/wait, changed-file update and joined
shutdown implementation now belongs to
`filemanager/external/qtexternalfilesession.cpp/.h`, within the existing panel
target. Its helpers remain private to the implementation. The existing exit
owner keeps its name, fields and methods in the focused header; the panel retains
its owning pointer and includes the complete type where it joins workers. The
worker-to-GUI adapter and its tests use that header directly.

The default panel constructor is defined out of line beside its destructor, and
the two existing constructors in App.cpp explicitly include the exit-owner
header. This supplies the complete owned type for exception cleanup without
exposing it through Panel.h. Constructor bodies and member initialization remain
unchanged. The RAM probe's separately compiled PanelItemOpen.cpp fixture is also
a direct build consumer; its override does not affect the transferred session.

A private panel entry point prepares and starts the session at the same point
after extraction and external-fallback confirmation. It receives the same file,
normalized directory, password and launch-mode inputs. Process records, GUI
update callbacks, guarded launch, exception cleanup, temporary-file protection,
native waiting and stop/join behavior are unchanged. The default external update
service also moves unchanged beside its session caller. No controller, shared
state, worker, public operation API or target is added.

Compared original PanelItemOpen.cpp child discovery, process record, wait/update,
exit and launch preparation bodies, their callers and Panel.h exit-owner types.
Both complete original units match the recorded hashes at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>. Moved current bodies and preparation statements
are unchanged, as are the remaining archive opening and nested-update bodies.
Existing Qt ownership and native-service adaptations remain in place. The
baseline maps transferred symbols to the new owner without advancing original
hashes or importing code. Existing launch, edit-return, lifetime, GUI delivery,
nested-edit and editor-configuration regressions are reused; no implementation
presence or private-call-count test is introduced.

Focused Linux validation passed all seven selected suites: 93 checks without
skips. The RAM source fixture built and ran with the production session owner.
Platform services and widget geometry are unchanged, so no secondary OS or new
appearance acceptance is claimed. Builds reused incremental outputs, compiler
cache and four jobs; completed temporary settings and records of absent test
objects were removed. No full or slow gate was selected.

## Configured application launch owner — 2026-11-04

Rechecked original PanelItemOpen command splitting, editor/viewer launch,
Diff callers and archive edit-return flow with EditPage configuration/browse,
registry values, Qt association/process services and current consumers. Complete
PanelItemOpen.cpp, Panel.h, EditPage.cpp/.h and RegistryUtils.cpp/.h originals match
their recorded hashes at `9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>. No external source is imported or provenance
advanced; existing platform adaptations remain explicit.

Configured application launch now owns the existing command splitter and launch
helpers beside the external-file service contract. Settings browsing and the
association adapter include that focused contract directly, without depending on
the panel declaration. Panel commands still select files and choose the operation;
the archive worker retains process observation, temporary-file protection,
changed-copy consent and write-back. Its default update service still performs
the same panel refresh after the supplied body.

All moved function and interface bodies remain unchanged. The existing native
quote rule, separate parameter/path transport, Windows editor fallback, POSIX
association fallback, known/detached process choice and launch-error reporting
are preserved. Settings Apply/Cancel behavior and editor/viewer/diff independence
are unchanged. One existing target links the implementation; no extra process
owner, service instance or result representation is introduced. The source
manifest maps the three extracted native functions to their original unit.

Existing settings, direct launch and archive edit regressions cover the meaningful
contracts; no test of source location or private representation is added. Visible
layout/text, native process spawning and archive-reader behavior are unchanged.

Linux passed external_launch, external_editors_ui, archive_external_edit and
archive_external_lifetime: 61 Qt checks without skips. This validates the shared
launch boundary and its settings/archive consumers; no new Windows/macOS,
full/slow or appearance acceptance is claimed. Completed test settings and
absent-object registry records were removed; incremental outputs/cache retained.

## Paired process wait entries — 2026-11-04

This intermediate projection was subsequently absorbed by the tracked-process
wait owner described above; its validation below records the earlier step.

Rechecked original PanelItemOpen child discovery, wait projection, completion,
temporary-copy and write-back flow at recorded
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`; the complete original unit matches
its baseline hash. Reviewed the current tracked-process owners, wait/error/exit
paths, first-pass name discovery, changed-file consent, update and cleanup.
No external source is imported or provenance advanced.

The wait projection now keeps each borrowed handle with its tracked-process
index in one record. Collection and completion consume the same record instead
of coordinating parallel handle/index vectors. Owned process records, admission
order, wait limit, polling and exit/error classification retain their existing
behavior. The first-pass timing/name rule and later child snapshots are unchanged.

The wait entries remain local to one synchronous wait; they neither retain nor
release processes. Child completion disables the same tracked record, and the
external temporary lease is released only after the original loop proves all
known children finished. Error, owner exit and exception retention, native
launch/reaping and archive write-back ordering are unchanged. Existing real-child
and returned-edit regressions cover these outcomes, including a child outliving
its parent; no private projection or source-shape test is added.

Focused Linux external-edit, lifetime and security acceptance passed. The full
worker matches its prior body after the paired-entry substitutions, and later
launch/open/update bodies are unchanged. Native platform services and visual
behavior did not change; no new secondary-OS, full/slow or appearance acceptance
is claimed.

## Returned-edit callback ownership — 2026-11-03

Reviewed editor launch/tracking, changed-copy consent, the GUI handoff, source
`OnOpenItemChanged`/`CThreadCopyFrom`, callback initialization and joined progress,
with update and selection publication consumers. Compared the original worker
and write-back bodies at `b88c6bc09339de32f7d18bbab900686b244f42ba`; complete
PanelItemOpen.cpp matches the recorded hash. The existing typed COM owner comes
from recorded MyCom.h, whose original hash also matches. No external source is
imported or provenance advanced.

The write-back worker now holds one CMyComPtr2 for its IProgress reference and
concrete update callback, removing the separately maintained raw alias. Allocation,
one retained reference, interface conversion, transport creation, Init/password
ordering and joined destruction remain unchanged. Child discovery/wait policy,
temporary-copy protection, update consent, native writer and post-operation
selection publication retain their existing owners and algorithms. These distinct
lifetimes do not justify another structural wrapper. Existing public editing,
lifetime and update checks cover the change; no representation-only test is added.

Linux `archive_external_edit`, `archive_external_lifetime` and
`source_update_backend` passed: 81 Qt checks without failures or skips;
incremental build 14.716s, tests 15.920s. Completed settings and registry records
for absent test objects were removed; build trees and compiler caches retained.
No native platform branch, UI layout or launch policy changed, so no secondary
OS, full/slow suite or new visual acceptance is claimed.

## Independent operation completion — 2026-11-03

Reviewed the full launcher/channel, endpoint, typed contract, native process
adapter and File Manager completion consumers together with the existing
Call7zGui, CProcess and CHandle source boundaries. CompressCall.cpp,
ProcessUtils.cpp/.h and Handle.h match their recorded original inventory hashes.
No native algorithm, process launch policy or external source was changed.

The observer now selects its terminal result before passing it through one
completion path. Crash/observation errors, report validation, exit disagreement,
buffered-message draining and early output notifications retain their existing
semantics. Acceptance dispatch and independent child lifetime are unchanged.

A focused real-process peer exercises success, missing/invalid reports,
native-exit disagreement and a crash after a success report over the production
Remote Objects channel. It checks public acceptance/completion, diagnostics and
output metadata; native launcher/extraction/creation and output-refresh suites
cover the direct workflow consumers. No pre-fix product failure is claimed.
These six focused suites passed on Linux; the peer's initial synchronous QtRO
initialization fixture was corrected to queue work like the production endpoint.
Native platform adapters and UI geometry were unchanged; no additional platform,
full/slow or appearance acceptance is claimed.

## Independent child release — 2026-11-02

Reviewed POSIX child ownership, creation/adoption and polling in both process
adapters, the independent-operation observer and endpoint, and editor wait
consumers. Compared original Call7zGui,
CProcess creation and CHandle destruction at recorded upstream revision
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>; complete CompressCall.cpp, ProcessUtils.cpp/.h,
Handle.h and PanelItemOpen.cpp hashes match the existing baseline. Windows
releases observation by closing its handle. POSIX reaping remains a local service;
no external source is imported or provenance advanced.

The independent GUI adapter's one-time nonblocking destructor wait could leave
a child unreaped when its observer was deleted while the manager stayed alive.
Both clients now link one POSIX child owner, extracted from the existing editor
reaper. It reserves cleanup before spawn, takes the successful PID without
allocation, and transfers pending reaping on destruction without terminating or
waiting for the child. Confirmed reaping and ECHILD consume that responsibility;
other observation errors retain it. Failed launches release their reservation.
The editor handle no longer keeps a separate owned flag or handoff destructor.
Native launch/session/descriptor policy, process identity, result interpretation,
IPC, archive edit-return and Windows behavior are unchanged.
Observation failure is committed before allocating its diagnostic text, so an
allocation exception cannot leave a running state with a cleared child PID.

A real-child regression releases its observer before permitting completion,
checks the payload, then observes reaping with waitid/WNOWAIT without consuming
the exit itself. Linux reproduced the unreaped-child failure before the fix.
Existing checks cover startup failure/retry, exit codes/crash, observer/parent
exit, archive extraction, editor handoff and cleanup with allocation failure.
Linux passed operation_process, operation_launcher, operation_extraction,
external_launch, process_spawn_allocation and archive_external_lifetime.
The same six suites then passed on macOS. Windows branches are unchanged and
were not rerun. Both platforms reused incremental builds and compiler caches;
no full/slow gate or visual acceptance was involved.

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

## Windows association launcher owner — 2026-11-04

Reviewed original BrowseDialog2 `StartApplication`/`StartApplicationDontWait`,
their shell PIDL/parser, string conversion and CProcess dependencies, and both
current consumers: associated-file services and the temporary-file browser.
Complete BrowseDialog2.cpp/.h/.rc hashes match the recorded original baseline
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. No new source or revision is imported.

The Windows launcher now belongs to the association module. Associated-file
services include its narrow interface instead of the whole temporary-browser
dialog; the browser consumes that same launcher. The baseline's existing
extraction mapping records both transferred symbols and their new owner against
the complete original inventory unit. Original hashes remain unchanged.

PIDL handling, extensionless-path correction, Unicode/legacy branches, DDE flags,
working directory, result mapping and process-handle attachment retain their
source bodies. The empty-name error presentation is local to the launcher with
the same critical dialog and text. Its Windows source retains the existing
Unicode compiler definitions; the browser's definitions are preserved too.
The shell-name parser remains with the panel menu. No new wrapper, process-wait
policy, ownership/protection state, dialog layout or source omission is introduced.

Linux passed `temp_cleanup`, `external_launch` and `archive_external_lifetime`:
62 checks without skips (build 33.425s, tests 15.892s). Interactive Windows passed
all four `native_association` scenarios: handle, disk, browser and workflow,
15 checks without skips (final build 139.296s, tests 16.958s). The first Windows
build exposed a previously transitive FileIO declaration; the final owner includes
that header explicitly. No new test was needed for the source move. Existing
incremental builds, four jobs and compiler caches were retained; completed
settings and registry records for absent fixture objects were removed. No full,
slow or macOS acceptance, screenshots or visual-parity claim is added.

## Native process platform ownership — 2026-11-04

Reviewed the native process interfaces, complete Windows/POSIX adapters,
associated editor callers, child tracker, preallocated reaper and launch/update/
shutdown ownership against the existing source comparisons above. Windows
CProcess/Toolhelp and POSIX spawn/wait/identity implementations are now complete
platform variants in the same process module. Its build definition selects one
variant for both the application component and allocation/snapshot fixtures;
the shared interface and component dependencies are unchanged.

The variants preserve their previous selected bodies, including native argument
construction, PID/start-time identity, process-group lineage, partial-launch
handle retention, ownership before metadata allocation and detached launch.
The original CChildProcesses/MyThreadFunction wait/update policy, archive
publication and temporary-copy protection remain unchanged. No algorithm,
external import, baseline revision or new process policy is introduced.
Existing external_launch/archive_external_lifetime and allocation checks cover
real child launches, argument boundaries, failure retention and shutdown;
Windows process_snapshot also covers its native creation-time correction.
No test is added to enforce source filenames or conditional structure.

Focused checks passed on Linux and macOS (external launch/lifetime and POSIX
allocation handoff), and Windows (external launch/lifetime, handle allocation
and snapshot). The first macOS archive fixtures lacked the console runtime;
normal codec-only preparation restored it, then only those two suites were
repeated. Platform bodies also match an independent unifdef projection of the
previous source. Incremental outputs, compiler caches and prepared runtime were
retained; completed settings and records for absent test objects were removed.
No full/slow or visual acceptance was run.
