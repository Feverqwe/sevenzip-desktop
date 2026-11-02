# Process notification ownership — 2026-10-18

Upstream: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed original ExtractCallback::AskOverwrite answer transfer, ProgressDialog2
completion/thread owners, the existing Console listing contract, and complete
Qt process callbacks, output consumers, completion/stage callers, listing,
overwrite, cancellation and destruction. ArchiveFolderManager and MainWindow
consumers were traced. Source algorithms and engine transport stay authoritative;
this supplies the QObject lifetime boundary absent from Win32.

[Qt 6.12 QProcess source](https://github.com/qt/qtbase/blob/v6.12.0/src/corelib/io/qprocess.cpp)
continues using private state after readyRead. A native read/death frame can
remain active when a receiver cancels and restarts work.
[QObject deferred deletion](https://doc.qt.io/qt-6/qobject.html) therefore needs
more than a backend pointer check. Each accepted CLI launch now has a separate
retained QProcess. Handlers verify transport identity; local leases protect
callback unwinding. Backend destruction synchronously stops/joins the child,
then defers wrapper release until Qt returns. The application owns fallback
cleanup on its thread if the loop stops first. A native host without a Qt
application destroys its idle wrapper directly. The chosen program, arguments,
channel routing and codec runtime selection are preserved.

Output parsing detaches a complete presentation batch before notifying receivers.
Nested draining cannot repeat a line or consume a replacement's buffer.
Incomplete lines/UTF-8 stay pending; raw metadata and password redaction retain
their contracts. Ownership/job/transport checks stop retired progress and output.
Completion retains its original job and transport before draining.

Listing publishes retained info/entry snapshots across receiver-triggered
requests. Its result is committed before notification, preserving the reentrant
request's lastResult. Terminal success/message still describe their original
operation without overwriting that subsequent result. Overwrite retains incoming
entries and checks ownership after notification. Stage arguments are transferred
before launch, and cancellation does not reset a replacement child's controls.

Five complete process methods moved into a focused adapter file, registered for
every backend consumer and the shared object owner. Unrelated extraction and
mutation workflows were not reorganized. No source body/dependency/significant
branch is omitted or duplicated; movement/formatting is separate from the fixes.

The process_notification suite uses short local child helpers/private inputs
with Unicode/spaces. It covers deletion from output, progress, metadata,
password, startup, completion, failure and overwrite signals; listing restart
from two receiver positions; a reentrant refused request; and output cancellation
without repeated lines or stale progress. Actual baselines: output deletion
SIGSEGV; both listing restarts received empty entries; old completion overwrote
E_INVALIDARG with S_OK. The overwrite fixture initially used RejectExisting;
it was corrected to Ask before accepting that scenario. No private/source-text
assertions were added.

Before mechanical extraction, app builds passed on all three OSes; 16 focused
suites passed on macOS (10.93 s) and Linux (18.87 s), 17 on Windows including
shell unload (57.49 s). Runner: `20261018T115115Z-a72a7695`, source
`766c702666ea4c9f1c3b0d9324a01b2029eb64d7b9c2142b21f62b811cd937fb`.
After extraction, app builds and direct suites passed: macOS 4/4 (2.60 s),
Linux 4/4 (2.19 s), Windows 5/5 including shell unload (7.50 s); runner
`20261018T115938Z-405fd170`, source
`2fe681c36d6423d8bdf4f8299e33f70b69857386f35ab1b302af7465c386d54b`.
The first extracted-module compile lacked the complete QtOwnedTempDir dependency;
its explicit include was added before these final checks. Partial macOS ASan
then passed all 15 cases (2449 ms), instrumenting eight affected translation
units with the existing dependencies and compiler cache.
Qt/official engine dependencies remain uninstrumented; no full sanitizer,
slow-suite, whole-project gate or appearance acceptance is claimed.

## Native listing completion publication — 2026-11-02

Rechecked the complete native read worker, single-entry output consumers,
backend read completion, listing/cache/extraction entrypoints and existing
notification regressions. The original ArchiveExtractCallback item/block error
projection was compared at `b88c6bc09339de32f7d18bbab900686b244f42ba`; its
complete original hash matches the recorded baseline. Native reader algorithms,
decoder results, cancellation and partial-output policy are unchanged. No
external source import or provenance advancement.

Native listing now commits its entries and reusable source snapshot before
emitting completion diagnostics. Previously the backend was already idle when
those messages arrived, but a receiver starting extraction still encountered
the missing listing prerequisite. Completion now has a single state-publication
phase followed by notifications carrying retained payloads. A reentrant request
can change backend state without the older listing writing its state afterward.
The existing signal order and original completion result remain intact.

The added regression lists a private Unicode-named 7z with trailing data, starts
extraction from its first diagnostic receiver and verifies exact extracted bytes,
the retained listing payload and separate listing/extraction outcomes. Trailing
data still prevents extraction from being reported as verified success. Existing
notification cases retain deletion, restart and reentrant refused-request checks.
No pre-fix run is claimed; the missing snapshot before the callback was established
from source.

Linux primary `20261102T050057Z-b752be99`: process_notification,
native_archive_test and native_source_extract passed, 68 checks, no skips.
Build 12.219 s; tests 4.505 s. Only shared Qt completion ordering changed;
no native platform branch or UI layout changed, and macOS/Windows acceptance
was not rerun. Selected scratch was empty before/after. Incremental outputs and
compiler caches retained; no full/slow gate, reports or screenshots.
