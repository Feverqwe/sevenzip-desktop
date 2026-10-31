# Native-worker notification ownership

## Window progress publication — 2026-10-31

Compared complete original `CProgressSync::Set_FilePath`, `Set_Status` and
`CProgressDialog::UpdateStatInfo` in `CPP/7zip/UI/FileManager/ProgressDialog2.cpp`
from the recorded 7-Zip archive at `b88c6bc09339de32f7d18bbab900686b244f42ba`.
The original unit SHA256 matches the baseline. Reviewed the current transport,
window-owned and source-owned dialog creation, backend progress/completion,
transport retirement, cancellation, shutdown and sequential operation callers.
Native progress snapshots, timer rendering, pause/cancel and result formatting
remain unchanged; no external source is imported or omitted.

Window-owned archive progress already updates its dialog through
`ArchiveProgressTransport`. Removed the second percent/path writer from the
window's global backend listener. The status-bar observer now lives with the
current transport and captures its operation label; transport retirement also
retires this observer. Removed window-wide operation-name/current-detail fields
and unused label assignments from source-owned opening/extract/test paths.
Native filesystem/volume progress keeps its existing worker owner.

The existing window cancellation check now observes rendered percentage and
Unicode/spaced path, retains the last path on an empty detail update, rejects
post-retirement status changes and starts a different archive operation with
its own label/progress before cancelling it. This protects visible behavior
without pinning callback counts, source text or private transport state.

Seven focused suites passed: `ui_archive_states`, `archive_progress`,
`progress_cancel`, `progress_worker`, `worker_window_progress`, `volume_workflow`
and `binding_lifecycle`. Linux took 43.36 seconds, macOS 28.26 seconds and
interactive Windows 60.47 seconds: 120 passed Qt rows on each platform, no
skips. Original source hash, inventory and whitespace checks passed. Compact
receipts/logs are retained, with existing incremental trees and compiler caches.
No full/slow gate, heavy diagnostics or visual acceptance is claimed.

## Joined cancellation and prompt reentry — 2026-10-31

Reviewed complete backend cancellation, read/hash startup and completion,
password/overwrite waits, phase-start reentry, destruction and joined source
request consumers. Compared original `CProgressSync::Set_Stopped`/`CheckStop`,
`CProgressDialog::OnCancel` and `CExtractCallbackImp::AskOverwrite` in
`CPP/7zip/UI/FileManager/ProgressDialog2.h/.cpp` and `ExtractCallback.cpp` from
the recorded 7-Zip archive at `b88c6bc09339de32f7d18bbab900686b244f42ba`.
Native stop, answer mapping, file access and decoding remain unchanged.
The asynchronous backend notification/join boundary is local Qt integration;
no external source is imported and original baseline hashes remain unchanged.

A synchronous overwrite-dismissal consumer can join the cancelled read and
start another operation before the outer cancellation returns. Previously that
outer call looked up its worker after notification and joined the replacement.
It could also recursively emit the same dismissal. Cancellation now consumes
the active prompt before notifying consumers and retains weak identities of
the workers present at entry. Only a still-current original worker is completed;
the completion owner already joins it, so duplicate waits are removed. The
backend-deletion guard and independently coalesced progress generation remain.

Two real native overwrite cases reproduced the premature replacement join on
Linux: read-to-hash and read-to-read. The regression observes the replacement
still busy on return, then successful completion, one prompt dismissal, a
cancelled original result and unchanged destination bytes. It uses a Unicode
archive path and a destination with spaces; no private worker access or timing
race is needed. MainWindow's dismissal connection is queued, so this evidence
establishes the public backend's synchronous-consumer contract, rather than
claiming a reproduced window-dialog failure.

Seven focused suites passed: `extraction_options`, `worker_progress`,
`native_archive_test`, `native_hash_workflow`, `native_source_extract`,
`app_lifecycle_window` and `binding_lifecycle`. Linux took 40.42 seconds and
macOS 28.74 seconds, with 136 passed Qt rows per platform and no skips.
Elevated interactive Windows took 54.76 seconds: 135 passed rows and only the
existing ordinary-filename colon skip; native NTFS link cases passed.
The genuine pre-fix failure and final compact receipts/logs are retained.
Source inventory and whitespace checks passed. No new disposable scratch roots
remained; pre-existing unrelated local roots, incremental trees and compiler
caches were preserved. No full/slow gate, heavy diagnostics or visual acceptance
is claimed.

## Read/hash callback identity — 2026-10-31

Reviewed complete hash command routing, native read/hash worker startup,
password/overwrite requests, joined cancellation, detached result publication
and their consumers. Compared original `HashCalc` and `HashCalcGUI` bodies in
the recorded 7-Zip archive at `b88c6bc09339de32f7d18bbab900686b244f42ba` with
the current port. Hash enumeration/reads/aggregation, native decoding, result
formatting and modal progress remain with their existing source owners.

Queued native read/hash callbacks now retain a `QPointer` to their actual worker
and compare it with the backend's current owner. Separate mutable read/hash
generation counters are removed. A released worker's weak reference stays null
even if a later QObject reuses its address; a completed worker retained during
result publication also cannot act on its replacement. The progress generation
still protects the independently coalesced mutex snapshot. This is local Qt
lifetime integration: no external source is imported or omitted, and original
baseline hashes remain unchanged.

The existing joined-cancel/restart regression now pauses its replacement from
the public phase-start notification before the thread starts. The GUI delivers
pending old completion events while that replacement remains paused: it must
stay busy without another result, then complete successfully after Resume.
This checks event delivery and avoids relying only on a final completion count
after an already fast replacement has finished. Existing read/extraction suites
cover success, errors, password/overwrite handling and cancellation.

Windows acceptance exposed two preexisting fixture assumptions in extraction
options: exact slash spelling in an overwrite path, and `QFile::link` standing in
for a filesystem symlink. The path assertion now accepts native separators;
the linked-destination cases use the existing native-link fixture on every OS.
They keep checking rejection and unchanged outside bytes, rather than a Windows
shortcut or an absent destination. Both destination and incoming native paths
are compared independently of separator spelling. The Windows native-link rerun
requires the elevated desktop; the ordinary-filename colon case remains
intentionally excluded on Windows. The directory-link fixture removes its link
after the backend joins, before Qt deletes the target directory, avoiding a
dangling NTFS reparse point left by `QTemporaryDir` cleanup.

Eight focused suites passed on Linux (14.33 seconds) and macOS (24.85 seconds),
128 Qt rows per platform without skips. The initial Windows batch passed the
other seven suites; extraction-options acceptance followed the fixture fixes.
The final affected-suite reruns passed on all three platforms, including the
elevated Windows native-link cases. Only the Windows ordinary-filename colon row
was skipped. Existing incremental trees and compiler caches were retained.
Inventory and whitespace checks passed. Compact test logs and verification
receipts are retained; no full/slow gate, heavy diagnostics or visual acceptance
is claimed.

## Notification lifetime — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed original `CPP/7zip/UI/GUI/HashGUI.cpp` completion and result formatting,
HashCalc callbacks, `CPP/7zip/UI/FileManager/ProgressDialog2.cpp` timer, posted
completion, final-message snapshot and thread join; the existing copy and volume
adapters retain the source FSFolder copy, CopyCoder and MultiOutStream owners.
The Qt side review covers the complete native hash, copy preparation, creation
and volume phase transitions, progress transport, cancellation, backend
destruction and direct consumers. Existing source provenance and dependencies
remain in the corresponding module audits.

The source modal progress dialog retains its thread and result owners through
completion. Qt signals additionally permit synchronous consumers to remove the
backend, cancel/join a phase or start a subsequent operation. There is no usable
Win32 implementation of that QObject lifetime boundary. This change is confined
to that adapter: no hash, copy, compression, volume, publication or cancellation
algorithm from the engine is replaced or omitted.

Confirmed failures and adaptations:

- Hash publication continued after a consumer deleted the backend. Retain a
  QPointer across each output delivery; the detached completed worker still
  owns its result and output data until the delivery returns.
- Hash completion read the shared last result after a reentrant operation
  changed it, dropping the completed operation's summary. Capture the completed
  HRESULT before publication; subsequent calls retain their own backend state.
- Copy progress passed a member QString by reference. A first receiver could
  synchronously cancel/join and clear that member, making a second receiver see
  an empty caption. Retain a local QString for both initial and timer delivery.
- Volume startup inspected members after its initial progress receiver deleted
  the backend. Check retained QObject ownership before inspecting the existing
  worker/generation guards or starting the worker.
- Joined native cancellation continued inspecting backend members after its
  terminal signal deleted the owner, or joined a new hash started from a copy
  completion. Return immediately after finishing the selected native phase.
  Native phases have no live CLI child to reset: copy/hash/creation preparation
  precede it, while volume/deletion follow process-exit cleanup. Retained
  creation metadata is not a concurrently active worker. The shared cancellation
  boundary also checks ownership after dismissing an overwrite prompt and after
  waiting for the process transport.

Regressions use public backend calls/signals, private local inputs and operation
results. Baselines: hash-owner deletion SIGSEGV; missing hash summary and changed
copy caption both failed assertions; copy-to-hash restart synchronously produced
two completions instead of one. Partial macOS ASan confirmed heap-use-after-free
in volume startup and in joined cancellation. It instruments the backend core,
hash/volume/preparation adapters and worker-progress test; Qt, official codecs
and other dependencies remain uninstrumented. Diagnostic optimization inlines
template accessors so uninstrumented debug weak-ODR accessors do not hide loads.
The normal build flags, persistent build outputs and compiler cache are retained.

The application built on macOS, Linux and Windows. Seven focused suites passed
on macOS (6.87 seconds) and Linux (8.42 seconds). Windows passed six suites in
the initial run; archive_creation_lifecycle's symbolic-link fixtures required
the elevated desktop and passed on their focused rerun. Tests and assertions
were retained. Runner evidence: `20261018T113232Z-9c89461f` and
`20261018T113421Z-8165d044`. Final partial macOS ASan worker_progress run:
11 passed, 0 failed, 799 ms.

Process-output, listing and overwrite callbacks have their separate
[ownership audit](process-notifications.md). No UI layout/text change,
slow-suite acceptance or whole-project gate is claimed here.

## Destruction and pipeline continuations — 2026-10-18

The backend destructor reused joined cancellation, which published finished
while the backend's owners were being destroyed. A connected pipeline could
start another operation at that point. Both copy-preparation and hash baselines
actually started a separate next backend during destruction (two failed rows,
3 ms). Starting work on the dying backend itself could also leave a live worker
for member destruction; the regression uses a separate owner to demonstrate the
unwanted continuation safely.

The original CApp::ReleaseApp stops all panel processing before releasing
owners. MainWindow::quiesceOperations already disconnects presentation consumers
before joining work. The backend now blocks its workflow signals at destructor
entry before any join. Explicit cancelAndWait keeps its result notifications
and supported continuation behavior; the existing reentrant cancel/restart
cases remain enabled. Native worker algorithms and resource cleanup are intact.

Application builds and six focused suites passed on macOS (3.24 s), Linux
(7.36 s) and Windows (16.23 s); runner `20261018T121239Z-278677db`.
Both new destruction rows and the existing explicit-cancel/restart, publication,
process-notification and closed-window cases are included. Build trees and
compiler caches were reused; no new sanitizer or full-gate acceptance is claimed.
