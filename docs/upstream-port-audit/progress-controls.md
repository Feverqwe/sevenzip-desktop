# Progress controls without background priority

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
