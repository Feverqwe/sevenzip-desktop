# Progress controls without background priority

## Progress form and statistic-row ownership — 2026-11-04

Compared original ProgressDialog2.cpp/.h, ProgressDialog2.rc and its included
ProgressDialog2a.rc at `b88c6bc09339de32f7d18bbab900686b244f42ba` with the Qt
form, initialization, pause/cancel and completion callers. Complete inventory
hashes match the recorded baseline. No external code or revision is imported.

The complete Qt form construction now lives in qtprogresspresentation.cpp,
beside the native progress and control-service owners in the same panel target.
The constructor still establishes flags, dialog units and its elapsed clock
before building controls. Native statistics, timer, transport, message and
worker-lifetime bodies remain unchanged.

Each statistic row now records its label, value and optional second value
together. One builder uses these records for overview and Details, replacing
parallel arrays, independent loop lengths and the positional Files special case.
Control creation order, resource IDs, labels, alignment, Files total suffix,
Details scrolling, footer focus and hidden error controls retain their existing
behavior and geometry. Existing progress_dialog, progress_cancel and
worker_window_progress checks cover the full presentation lifecycle; no
representation-only test was added.

## Filesystem checksum traversal ownership — 2026-11-04

Compared the complete original `CPP/7zip/UI/FileManager/PanelCrc.cpp` at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git> with the current command, extracted filesystem
worker, GUI dispatch and result consumers. Its full-unit hash and the original
`Windows/FileFind.h` hash match the recorded baseline. No external source or
revision is imported.

The filesystem enumerator in `archive/hashing/qtfilecrc.cpp` now keeps each
native directory enumerator with its relative prefix in one stack record.
Push, exhaustion, failure and scan reset therefore retire one owner rather than
coordinating two parallel collections. Native traversal order, selected-path
iteration, error capture before destruction, drive-root handling and POSIX
no-follow enumeration remain unchanged. Enumerators still use the native
noncopyable handle owner; stack records are owned by `CObjectVector`.

The two-pass scan/read worker, error accounting, cancellation, hashing and
once-only result bodies are unchanged. Existing operation regressions exercise
recursive and flat selections, literal paths, partial inputs and cancellation;
no test of the private record representation is added. No UI layout, native I/O
adapter, archive-stream path or independent-operation protocol changes.

Linux `native_hash_workflow` and `operation_hash` passed 28 checks without
failures or skips. The native I/O and platform branches are unchanged; no new
Windows/macOS or visual acceptance is claimed.

## Complete progress display owner — 2026-11-04

Compared original ProgressDialog2.cpp/.h/.rc initialization, range/position,
statistics, time/size formatting, title and resize bodies with current timer,
pause, language-change and final-completion callers. Complete original hashes
match the recorded baseline at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. No source is imported or revision advanced.

ProgressDisplay.cpp now owns initialization of the existing display, its complete
numeric/string helpers, range/position, statistics, title and width-based text
reduction. All remain methods/helpers of the existing dialog in sevenzip_panel.
Modal/worker lifetime, message handling, pause/cancel and GUI event dispatch stay
in ProgressDialog2.cpp; synchronization and Qt widget construction retain their
existing owners. No state, controller, timer or second snapshot is introduced.

Moved and remaining method bodies are unchanged. Unknown-value initialization,
locked counter snapshot, elapsed/remaining-time and speed calculations,
compression/decompression direction, files/bytes mode, transport-percent fallback,
error indication, taskbar updates, shortened paths and paused/main-window titles
retain their existing rules and order. The display's private sentinels stay
together with initialization and use. Both implementations use the existing
shared native modal-result constants instead of three repeated local declarations;
values and commands are unchanged. Previous synchronization extraction mappings,
original hashes and compilation options are retained. Existing behavior tests
are reused; no source-layout/private-helper test or appearance change is added.

Focused Linux validation passed progress_dialog, progress_worker, progress_cancel,
archive_progress and language_dialog: 96 Qt checks without failures or skips.
Existing cases cover unknown totals, size/ratio and Unicode paths, range changes,
paused rendering/elapsed time, final messages, cancellation during completion,
transport pause and live language changes. Source separation preserves widget
construction, native platform branches and compilation options; no new
Windows/macOS, full/slow or appearance acceptance is claimed. Selected scratch
was empty after validation, with incremental outputs and compiler caches retained.

## Progress synchronization source owner — 2026-11-04

The complete `CProgressSync` implementation, its state declaration and final
message types now live in `ui/progress/progresssync.cpp/.h`, compiled once by the
existing `sevenzip_progress_sync` target. The dialog includes that interface;
worker checkpoints, volume progress and the existing synchronization tests use
it directly. Split-build preprocessor definitions are removed from both targets
and from the volume adapter. The widget header and implementation now describe
the presentation unconditionally. Existing target relationships are unchanged.

Moved method bodies, fields and inline methods are preserved. The existing
single-worker wake adaptation, native polling fallback, locking, pause/stop
ordering, counters and diagnostic composition are unchanged. Dialog construction,
modal lifetime, native worker join, transport, message snapshots and cancellation
remain in their existing presentation owner. No new synchronization primitive,
state, policy or algorithm is introduced.

Compared original ProgressDialog2.cpp/.h bodies and their synchronization,
dialog/worker and volume consumers. Complete original units match the recorded
baseline at `b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>; the reviewed synchronization bodies also match
the separate `9128b80e3a471108678e2b3ec3c985861c8d7b0f` reference. The complete
Synchronization.h dependency matches its recorded hash as well. The existing
baseline maps extracted symbols to their focused owners; original hashes and
licenses are unchanged. Existing behavioral suites are reused without adding
tests of file layout or compile-definition presence.

Focused Linux validation passed ten synchronization, checkpoint, worker, dialog,
cancel, formatting, archive-progress and volume suites: 184 checks without skips.
The extraction preserves platform branches and widget behavior, so no secondary
OS or new appearance acceptance is claimed. Builds reused incremental outputs,
compiler cache and four configured jobs; completed temporary settings were removed.
No full or slow gate was selected.

## Shared archive diagnostic composition — 2026-11-04

The standalone native opening, extraction-result and error-flag report builders
now belong to `archive/diagnostics/archiveerrormessages.cpp/.h`, within the
existing panel target. Extraction and update callbacks, backend diagnostics,
archive properties, limited-read prompts and list presentation include this
focused interface. Consumers no longer redeclare these functions or depend on
the extraction callback header solely for report composition. The Agent callback
only forwards results; its unused formatter declaration is removed.

All transferred function/helper bodies are unchanged apart from formatting and
redundant declarations. Native message ordering, localized legacy fallbacks,
unknown flags, encryption hints, paths, nested archive levels, format warnings,
HRESULT fallback and trailing-newline handling remain intact. Existing Qt
translation of known diagnostics remains in its separate owner; system HRESULT
formatting remains with the existing progress implementation. Callback state,
error accounting, progress delivery, recovery consent and operation results are
unchanged. No new algorithm, target, state or external import is introduced.

Compared complete original ExtractCallback.cpp/.h units and formatter callers
in UpdateCallbackGUI.cpp, UpdateCallback100.cpp and UpdateCallbackAgent.cpp at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>, alongside current callers and their types.
All five complete original units match the recorded baseline hashes. The
existing manifest maps transferred symbols to the new owner without changing
original bytes, revisions or licenses. Existing behavior tests cover native
diagnostic values, localized presentation and direct operation consumers;
no source-layout or private-call-count test is added.

The first focused Linux build exposed two dependencies formerly supplied by the
callback header. Backend result reporting now includes the native archive
interface directly; limited-read prompts explicitly include the complete progress
dialog type. The corrected build and eight selected suites passed: 105 checks
without skips across native archive tests, extraction contracts, shell update
and extraction services, localized dialogs, archive error properties, panel
properties and native folder opening. Only portable composition ownership changed;
no secondary OS, full/slow gate or new appearance acceptance is claimed.
Checks reused incremental outputs, compiler cache and configured four-job builds.

## Original progress controls — 2026-10-23

User request, 2026-10-23. Transferred
`9808a74de17f851fc02d793a5237b0f2ef3959e2` and the guarded progress-transport
backend reference from `33828e5a4d3849d59d2547c1cae919d80ad5e9e4`.

Source: <https://github.com/ip7z/7zip.git>,
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`,
`CPP/7zip/UI/FileManager/ProgressDialog2.cpp/.h`, `ProgressDialog2.rc` and
`ProgressDialog2a.rc`; also compared the sibling at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Reviewed initialization, title,
priority, pause, completion, cancel, resource buttons and the Qt transport callers.
The user-requested omission removes Background/Foreground and process-priority
services, including phase inheritance and dedicated priority tests. Cancel
receives initial/default focus; Pause/Continue, confirmation and Close retain
the existing source adaptation. Qt owns layouts and native button roles.

Focused validation on macOS, Linux and Windows passed progress synchronization,
worker checkpoints/waits, progress worker, archive progress, cancel, formatting,
dialog geometry, resource contract comparison, checksum and toolbar archive
workflows. Current normal/compact widget JSON shows reachable Pause/Cancel and
separate errors/footer, including completion with Close. macOS/Linux used
offscreen Qt; Windows used the logged-on desktop and native QPA. No rendered
Windows reference was captured; this is structural usability evidence.

The expanded consumer check also reported failures outside the changed cases:
`language_dialog::archiveDiagnosticValuesKeepPathsAndSelection` and both
`ui_operations::encryptedAskUsesSourceCallback` rows, identically on all three
platforms. Their bodies and functional owners are unchanged by this transfer;
the progress-language and other UI operation cases passed. These failures are
recorded separately from acceptance of the removed priority capability.
Slow suites and a full gate were not run. The runner receipt is
`20261023T180121Z-40f9b011`; incremental builds and compiler caches were retained.

## Native worker entry owner — 2026-11-02

Compared progress worker entry, creation, exception/result publication, readiness,
completion and join with their original ProgressDialog2.cpp/.h bodies. Complete
original units at `b88c6bc09339de32f7d18bbab900686b244f42ba` match the recorded
inventory hashes. No code is imported or provenance advanced.

The native thread entry now belongs to its progress worker. It receives that
owner directly on every platform; POSIX no longer needs a separate stack context
pairing the owner with its own completion latch. The original native callback
signature/calling convention, Process/result/ThreadFinishedOK order, exception
handling and final POSIX wake remain unchanged. The modal creation boundary still
joins the native thread before releasing the owner. FileFolderPluginOpen keeps
its separate worker entry and existing completion wake; it is not routed through
the progress worker's entry. No pause, cancellation, message or UI policy changes.

Existing worker tests cover fast completion without a dialog, requested readiness,
delayed completion, error/exception composition and GUI completion delivery. Wait
tests cover latched notifications and finish precedence; opening callbacks cover
the independent native-thread/readiness path. No private entry/context test is added.

Linux progress_worker, progress_wait and open_callback passed 34 checks without
skips. Windows progress_worker and open_callback then passed 29 checks, validating
the native callback signature and completion path there. Selected scratch was
empty before and after both runs. Whitespace and inventory checks passed; builds
used configured four-job defaults, incremental outputs and compiler caches.
No new macOS, visual, full or slow-suite acceptance is claimed.

## Final message presentation — 2026-11-02

Reviewed final GUI hooks in copy, CRC, extraction and hashing together with
progress completion, message snapshots and close deferral during cancellation
confirmation. Compared the complete original OnExternalCloseMessage branches.
The final-message snapshot still follows the GUI result hook; deferred-close
states and each consumer's once-only result handling remain unchanged.

Error and success now share one presentation path for their existing message
pair. Error priority, suppression of success when item errors are present,
MessagesDisplayed for an empty successful result, default/custom titles, error
icon, disabled questions and the errors-list Close state retain their previous
behavior. No native operation, result aggregation, layout or text changes are
introduced; no external code is imported or baseline advanced. Existing actual
message-box, deferred-cancel and checksum workflow regressions cover the
observable behavior, without tests of the new local selection expression.

Linux progress_worker, progress_cancel and checksum passed 53 checks without
skips. Only shared message selection/presentation changed; no OS branch or native
dialog adapter changed, so secondary OS checks were not selected. Whitespace and
source-inventory checks passed, completed test settings were removed, and builds
used configured four-job defaults with incremental outputs and compiler caches.
No full/slow gate or new rendered appearance acceptance is claimed.

## Filesystem checksum worker owner — 2026-11-04

Rechecked complete original PanelCrc.cpp enumeration, scan/read loops, worker,
result presentation and CApp dispatch with current HashGUI/HashCalc, independent
GUI requests, archive stream hashing and backend consumers. PanelCrc.cpp,
Panel.h, PanelSplitFile.cpp, HashGUI.cpp/.h and HashCalc.cpp match their recorded
original hashes at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>. No external code is imported or baseline
advanced; the existing mixed-revision provenance remains explicit.

The complete filesystem worker and its existing CalculateFileCrc entry now live
beside that entry's interface under archive/hashing. PanelCrc retains CApp command
selection, archive stream routing and command result policy. Both implementation
units remain in the same existing target; no new service, worker, result type or
dependency cycle is introduced. The source manifest maps the extracted symbols
and filesystem setup fragment to their original inventory unit. The unchanged
source CMyBuffer declaration is shared under common/memory and included directly
by its two consumers, CRC and Split/Combine; it no longer makes the reusable
worker depend on the panel header. Allocation and release still use MidAlloc
and MidFree with the original class body.

The moved bodies preserve enumeration and path/ADS handling, native platform
reads, method selection, progress/cancellation, partial-error accounting, joined
worker lifetime and once-only result presentation, including the post-Create
fallback. Independent GUI hashing, direct API calls and panel dispatch share the
same implementation as before. The distinct censor-based HashCalcGUI and backend
callback consumers retain their existing responsibilities.

Existing native hash, checksum, independent operation and manager regressions
exercise digests, traversal, errors, cancellation and result delivery. Existing
volume operations cover the shared buffer's Split/Combine consumer. No
representation test is added; native algorithms and visible layout/text remain
unchanged.

Linux passed native_hash_workflow, checksum, operation_hash,
manager_hash_workflow and volume_operations: 83 Qt checks without skips. This
validates shared source separation and its direct consumers, with no new
Windows/macOS, full/slow or appearance acceptance. Completed settings were
removed; incremental build outputs and compiler caches were retained.

## Consuming checksum result availability — 2026-11-02

Reviewed complete CRC/copy worker result preparation, GUI completion hooks and
post-Create fallback consumers against original PanelCrc.cpp and PanelCopy.cpp.
Each worker is constructed for one operation. Readiness is published before the
completion hook, and the fallback follows the joined Create call; neither worker
writes result readiness after its first presentation.

Each worker now keeps one pending-result flag instead of separate readiness and
already-shown flags. Presentation consumes it before opening the modal result
view, retaining protection against nested delivery and the fallback call. The
original publication points remain: CRC can show a completed summary with
accumulated errors, while copy requires its previous success/no-message-error/
hash-method conditions. Hash contents, cancellation, source algorithms and
result policy are unchanged; no new source import or baseline advance occurs.

Public CRC and archive checksum cases count actual result windows and verify
their digest. This detects duplicate completion/fallback presentation through
user-visible behavior. Reviewing direct consumers also found two native hash
fixtures still using the constructor from before the validated-provider handoff;
they now supply the explicit built-in provider inventory used by those cases.

Linux native_hash_workflow, checksum and operation_hash passed 37 checks without
skips, including both once-only window checks and independent panel hash flows.
Only shared result ownership and portable test consumers changed; no OS branch
changed, so secondary OS checks were not selected. Original PanelCrc.cpp and
PanelCopy.cpp inventory hashes, source inventory and whitespace checks passed.
Configured four-job builds reused incremental outputs and compiler caches;
completed test settings were removed. No full/slow or visual gate ran.

## Copy callback ownership — 2026-11-02

Reviewed complete original PanelCopy.cpp workers and CopyTo/CopyFrom callers,
including filesystem path copying, archive extraction/testing, stream output,
hash results and update actions. Compared the recorded original inventory unit;
its baseline remains unchanged. Callback interfaces and implementations already
share one reference-counted lifetime through the joined progress operation.

Both copy workers now use the existing upstream typed COM owner instead of
maintaining a separate implementation pointer beside the owning interface.
Allocation and reference acquisition remain at the same preparation points;
interface conversions and extraction QueryInterface retain their original
interfaces. Hash/provider destruction order, progress transport lifetime,
passwords, overwrite decisions, extraction reports, update publication and
error/cancellation handling are unchanged. No new abstraction, imported source,
algorithm or platform policy is introduced.

Existing extraction, native drop, archive move, checksum and copy-publication
regressions exercise the affected public workflows and their direct consumers.
No test of pointer representation or private worker structure is added.

Linux passed all five selected suites: 58 checks without skips. Only shared
callback ownership changed; no platform-specific implementation or ABI changed,
so secondary OS checks were not selected. Source inventory, original inventory
hash and whitespace checks passed. Builds retained configured four-job defaults,
incremental outputs and compiler caches; completed scratch settings were removed.
No full/slow or appearance gate ran.

## Batch extraction callback ownership — 2026-11-02

Compared the complete original extraction worker and extraction-group callers
with the current ExtractGUI, shell-command and GUI-command paths. The application
batch adapter uses the same extraction boundary. Callback interfaces, extraction
options, volume ordering, path policy and report checks retain their existing
owners and behavior; original inventory hashes remain unchanged.

Each extraction entry now uses the existing typed COM owner for its callback,
removing the separate implementation pointer and interface downcast. The worker
likewise retains the caller-provided implementation through one typed owner.
It acquires its additional reference at the original preparation point, after
dialog/path acceptance, and releases it after the joined operation. The caller's
reference still survives worker destruction and the final IsOK check. Dialog
Cancel and invalid-output exits do not acquire a worker callback reference.
Progress attachment, initialization, password updates, success/error summaries,
hash results and transport teardown order are unchanged. No public API,
independent archive algorithm or new source import is introduced.

Existing batch extraction, shell service, open-option and GUI error-policy tests
exercise success, overwrite cancellation, volume handling, type exclusion and
early exits. The native GUI workflow additionally exercises the independent
7zG command entry; it is registered only on Windows. No pointer-layout or
private-state test is added.

Linux passed 50 checks across the four shared suites without skips. Windows then
passed all 13 native GUI workflow checks, covering the changed 7zG entry through
its platform-specific driver. Selected scratch directories were empty before
and after both runs. Source inventory, original hashes and whitespace checks
passed. Builds reused configured four-job defaults, incremental outputs and
compiler caches. No macOS, full/slow or rendered-parity acceptance is claimed.

## Batch opening service ownership — 2026-11-02

Reviewed the complete batch opener and folder-manager opening path together with
their resource, progress and agent service boundaries. These are local Qt
integration owners around the recorded source opening/extraction implementations;
no original algorithm, source inventory or provenance baseline changes.

The batch opener now retains its known folder-manager implementation through
the existing typed COM owner. Opening and progress transport call that manager
directly, removing downcasts and the unreachable missing-progress-service branch.
The opening service likewise keeps its known archive resource in a typed owner,
while continuing to publish the same IUnknown interface to the agent boundary.
Reference-count interfaces and resource/catalog destruction order are unchanged.
The opener destructor is defined beside the complete manager implementation,
keeping the header's forward declaration usable by consumers.

Native and stream opening, codec discovery/cancellation, limited-read consent,
path validation, properties and volume accounting retain their existing branches.
Existing batch/open-option, shell extraction, agent opening and retained-layer
lifetime tests cover these owners and direct consumers. No test of private
pointer types, casts or constructor representation is added.

Linux passed all five selected suites: 59 checks without skips, including
discovery cancellation and retained stream/layer lifetimes. Source inventory and
whitespace checks passed. Only shared local ownership changed; no native OS
branch or ABI changed, so secondary platforms were not selected. Configured
four-job builds reused incremental outputs and compiler caches. Selected scratch
was empty before and after validation; no full/slow or appearance gate ran.

## Resource progress during archive reopen — 2026-11-02

Reviewed complete reopen, resource progress/update services and transport
attachment/cancellation with agent reopen and retained-reader consumers. Native
reopen/thread signatures, callback delegation and join remain unchanged; no
source import, archive algorithm or baseline changes are introduced.

Reopen's one-use Sync cleanup now uses the existing scope guard rather than a
local guard class. Attachment and clearing remain at the same points, including
thread-start failure and a missing backend transport. The independent binding
remains necessary when a retained reader outlives its backend.

The resource transport now forwards Cancel as well as Pause to its contained
archive transport. Reopen explicitly attaches this outer interface to the
dialog; previously its Cancel inherited the interface's empty default instead
of reaching the backend. Sync-based worker cancellation remains in place.
Composition and destruction order are retained: clear resource Sync, detach
the contained transport, then release the retained resource.

An existing lifetime suite now covers resource transports after caller-resource
and backend release, and verifies that cancelling a paused backend operation
through the public resource transport completes as cancelled without changing
input bytes. Reopen/stream-update and progress-cancel suites cover direct
consumers. No private pointer/state or source-shape test is added.

Linux passed the four selected suites: 49 checks, with three existing Windows
filesystem-ADS cases skipped. After making the paused-operation fixture use the
dialog's retained Sync pause state, all nine lifetime checks passed again;
the other passing suites remain applicable. Shared Qt delegation/cleanup changed,
without native OS branches or calling conventions, so secondary platforms were
not selected. Source inventory and whitespace checks passed. Selected scratch
was empty before and after both runs; configured four-job incremental builds
and compiler caches were retained. No full/slow or appearance gate ran.
