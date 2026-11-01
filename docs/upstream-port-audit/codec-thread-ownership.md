# Shared codec owner across native workers — 2026-10-18

## Discovery request lifetime — 2026-11-01

Reviewed probe preparation, queued delivery, cancellation/deadline, join,
destruction and backend publication with opening/startup consumers. Rechecked
the unchanged native loading boundary against recorded LoadCodecs.cpp/.h at
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>; complete original units match their existing
baseline hashes. No native loading code is imported, replaced or omitted.

The discovery request now owns its completion callback alongside the result and
stop/deadline state. Its presence defines probe activity; the separate callback
member and active flag are removed. Joined completion detaches the request before
calling a consumer, so a reentrant retry has its own owner and a consumer may
destroy the probe. Worker identity, unpublished-start cleanup, deadline/error
policy and the backend's separate queued publication state remain unchanged.
That backend state still spans the interval after the probe finishes and before
catalog publication on the GUI thread.

New public callback cases retry or destroy the probe from completion, checking
one result per accepted request and readiness for retry. Existing cases retain
native loading, rejected concurrent starts, failure/cancellation, deadlines,
responsiveness and joined teardown coverage. No private representation test or
pre-fix product failure is claimed.

Linux acceptance passed `codec_probe`, `codec_catalog`, `agent_opening` and
`codec_commands_ui`. Only the portable request owner changed; native module
loading and platform branches are untouched, so no secondary OS was selected.
Checks reused incremental outputs, compiler cache and the four-job default.
No full/slow gate or appearance acceptance is claimed.

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed complete `UI/Common/LoadCodecs.h` ownership declarations,
`LoadCodecs.cpp` provider/factory and library-release bodies,
`OpenArchive.cpp::CArc::PrepareToOpen`, `Common/CreateCoder.h/.cpp`
per-handler provider leases, and the `Common/MyCom.h` / `UI/FileManager/MyCom2.h`
COM implementations. Qt callers include native opening, hashing, independent
backend windows, codec discovery, immutable runtime images and catalog teardown.
Borrowed module baselines remain unchanged.

Qt windows share one immutable ArchiveCodecCatalog, while their native workers
independently open and release handlers. Each copied ZIP/RAR handler retains the
source CCodecs through CExternalCodecs. shared_ptr protects the catalog object,
but the source COM AddRef/Release and QueryInterface counters were non-atomic.
Concurrent handler construction and destruction therefore raced that shared
counter. Lost increments can invalidate COM ownership even while a catalog lease
still exists.

The CCodecs external-provider interfaces now use the original
Z7_COM_UNKNOWN_IMP_2_MT implementation from MyCom2.h. Existing source interface
declarations, storage, factory dispatch, codec loading and library-loop teardown
are retained. Synchronization.h supplies the existing native Interlocked
declarations, implemented by the unchanged C Threads module on POSIX. This is a
narrow shared-owner adaptation; other source COM objects retain their existing
threading contracts. No loading algorithm, provider method or branch is omitted.

ThreadSanitizer reproduced concurrent writes in CCodecs::AddRef from two native
ZIP opener threads before the fix. The same diagnostic passed after the fix,
with two threads each opening, extracting and releasing 32 readers. It
instrumented the catalog, LoadCodecs and a standalone diagnostic consumer,
linking existing uninstrumented dependencies. The synchronous consumer avoided
partial-instrumentation reports across Qt's uninstrumented worker publication;
this is evidence for the native ownership boundary, not a whole-application
TSan acceptance run.

The regular external-codec suite also checks parallel readers against one
retained catalog, extracted bytes and a subsequent open after both workers
release their handlers. Each reader owns its callbacks and output stream.

The application built and all 16 selected codec/provider, native opening/writing,
source update/extract, lifecycle, checksum and benchmark suites passed on macOS
arm64, Linux x64 and Windows x64 / Qt 6.12. Guest runs
`20261018T080528Z-19c4cd57` and `20261018T080527Z-e10abbb2` reused their persistent
worktrees and compiler caches; Windows used the elevated interactive token for
existing link fixtures. No full gate, opt-in slow matrix or visual comparison
was run for this nonvisual ownership change.

## Discovery startup ownership

The Qt scheduling adapter `ArchiveCodecProbe::Start`, its Work/result owner,
cancel/destructor/completion paths and `ArchiveBackend::discoverCodecCatalog`
were reviewed together. Native discovery still uses the original LoadCodecs
provider and library-loading bodies listed above; no native loading branch or
dependency was replaced or omitted.

Startup previously published active state before allocating its worker state.
A preparation exception could therefore leave cancellation without a valid
owner and prevent a retry. Preparation now retains local owners until the
thread has started, joins any unpublished worker, and publishes the backend's
busy state only for an accepted launch. Queued completion checks its worker
identity. Exceptions from discovery are converted to an HRESULT at the worker
boundary rather than escaping the thread. These are Qt ownership adaptations;
they do not establish allocation-failure guarantees for Qt itself.

At that checkpoint, discovery tests covered rejected concurrent requests, retry
after joined failure/cancellation, deadlines, destruction, native loading and
child cleanup.
The selected acceptance scope is those tests plus codec catalog, capabilities,
creation, native services and window lifecycle; no visual or slow-suite claim
follows from this nonvisual change.

Application builds and all six selected suites passed on macOS arm64 (7.50 s),
Linux x64 (8.74 s) and Windows x64 (12.68 s), using Qt 6.12 and the persistent
incremental builds/caches. Guest evidence: `20261018T124053Z-a5f7c10a`.

Native-only discovery supersedes the child-process portion of this checkpoint;
see [the current migration audit](container-layers.md#native-catalog-discovery).

## Opening during discovery — 2026-10-22

An opening immediately after launching File Manager could snapshot an absent
catalog while asynchronous discovery still owned the backend. Its subsequent
process listing then failed the busy check with `E_FAIL`; a later retry worked.
Compared original `CAgent::Open`, `CFfpOpen`, `CPanel::OpenAsArc_Msg`,
`OpenAsArc_Index` and `OpenItem` bodies/callers at the revision above with Qt
discovery, opening, progress and command guards. Original codec loading is
synchronous; there is no upstream asynchronous readiness service to transplant.

The existing Qt opening worker now awaits the current discovery transaction
before obtaining its immutable catalog lease. State/subscription/publication
stay on the GUI thread, and cancellation uses the original progress callback.
Cancelling this opening releases its subscription without cancelling shared
discovery. Failed/missing-module discovery retains the existing process fallback;
handler selection, decoding and library-loading bodies are unchanged.

The original `OpenAsArc_Msg` already reports non-`S_FALSE` failures, and its
`OpenItem` caller reported the same HRESULT again. Removed that second call in
the Qt port while preserving error propagation, encrypted refusal and external
fallback decisions. This is an intentional presentation correction to upstream.

A controlled delayed-module fixture reproduced `E_FAIL` before the fix. Agent
tests now cover opening during discovery, cancellation and retry after shared
discovery finishes. A command-driven UI regression reproduced two dialogs on
the unchanged production sources and now requires exactly one, an unchanged
folder after failure and a successful retry. Its fixtures use local temporary
archives, including Unicode/spaces and a misleading `.pkg` suffix.

Final incremental application build and six ordinary macOS suites passed:
`agent_opening`, `native_folder_opening`, `archive_opening`, `codec_catalog`,
`codec_probe`, `upstream_baseline` (7.40 s). The new UI case passed separately
with offscreen and native Cocoa. A wider `panel_activation` run exposed existing
Enter-routing failures; the representative `modifierWorkflow:0-0-3-0-0-0`
also fails with all production changes reverted to the preceding commit.
Full acceptance of that matrix is not claimed. No slow suites, Linux/Windows
checks or installed-application replacement were performed. The user's package
was only listed read-only for diagnosis, never executed or extracted to disk.
