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
