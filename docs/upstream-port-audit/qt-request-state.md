# State of a new archive request — 2026-10-18

## Source-entry completion outcome owner — 2026-11-02

Reviewed the complete source-entry completion/cleanup helper, joined read
completion, partial-output consumers and callback-driven backend replacement.
Native item/open diagnostics, read-only recovery eligibility and source decoding
retain their current owners and contracts. No source import, decoder change or
baseline advancement is introduced.

Source-entry completion now accepts and returns its HRESULT instead of mutating
the backend's terminal result as a hidden side effect. Joined read completion
applies cancellation priority, obtains the source-specific outcome where needed
and stores the terminal result before publishing signals. Item-result
classification remains at its existing owner. Recovery format checks,
created/closed output requirements, empty-output cleanup, warning/item priority
and cancellation cleanup are unchanged. Backend state snapshots and destruction/
replacement guards keep their original notification order.

Existing native-source, recovery, stream-contract and backend completion suites
cover successful, partial and failed outcomes, output preservation/removal,
password/cancellation and reentrant completion. No internal-field or helper-shape
test is added.

Linux passed all four selected suites: 114 checks without skips. Source inventory
and whitespace checks passed. Shared completion-result handling changed without
OS-specific code or native engine interface changes, so secondary platforms were
not selected. Selected scratch was empty before and after validation; configured
four-job incremental builds and compiler caches were retained. No full/slow or
appearance gate ran.

## Native read worker terminal result — 2026-11-02

Reviewed the complete native worker/callback sequence and all result consumers.
Backend completion, retained cache and direct fixtures join the worker before
reading its terminal result; cancellation/progress callbacks do not read it.
Source opening, listing, decoding, output and callback contracts remain unchanged;
no new source import or baseline advancement is introduced.

The worker's processing body now returns its HRESULT, while the thread entry
stores that outcome and retains the existing exception-to-E_OUTOFMEMORY mapping.
This removes repeated writes/checks of the public terminal field during setup.
Checkpoints remain before and after codec loading, catalog diagnostics retain
their failure points, and failed Open still collects all available open errors
before returning. Callback attachment/detachment and final error aggregation keep
their order, including List's existing treatment of open warnings. Callback and
reader locals still unwind before thread completion is published.

Existing listing, native Test, single-entry, directory-option and retained-view
suites cover the affected modes and early exits. No private-method or
intermediate-field-value test is added.

Linux passed all five selected suites: 127 checks without skips. Source inventory
and whitespace checks passed. Only shared worker/result ownership changed,
without native OS branches or ABI changes, so secondary platforms were not
selected. Selected scratch was empty before and after validation; configured
four-job builds retained incremental outputs and compiler caches. No full/slow
or appearance gate ran.

## Native single-entry preparation — 2026-11-02

Reviewed the complete native read callback/worker, single-entry selection/output,
retained-cache caller, native extraction boundary and partial-output publication.
Compared the original Client7z GetStream/SetOperationResult output ownership and
close contract. Native decoding and stream-limit implementations remain intact;
no external code import or baseline advancement occurs.

Single-entry selection and validation now precede output processing through one
local preparation function. A supplied index still validates bounds, directory
and stream kind; name selection still scans for ambiguity before creating a
file. Output uses the existing always-created COM owner, and the worker callback
uses the existing typed owner instead of paired raw/interface aliases.
Reference acquisition/destruction order and open/list/test/directory interfaces
remain unchanged. Callback output binding/unbinding, explicit file close,
created/closed flags, partial item results and error precedence are preserved.

Existing native-source tests cover literal/duplicate names, solid siblings,
CRC/partial output, passwords and cancellation. Read-only view checks cover
retained index selection and bounded output; native test and directory options
exercise the other callback consumers. No private-preparation or pointer-type
test is added.

Linux passed all four selected suites: 111 checks without skips. Source inventory
and whitespace checks passed. Shared selection/ownership changed without native
OS branches or calling-convention changes, so secondary platforms were not
selected. Selected scratch was empty before and after validation; configured
four-job builds reused incremental outputs and compiler caches. No full/slow or
appearance gate ran.

## Staged extraction stream outcomes — 2026-11-02

Reviewed the complete typed file-only extraction adapter, native source-entry
publication, callback contracts and official CopyCoder error propagation. Native
decoding, selection/duplicate guards, password handling, staging and partial-data
eligibility remain unchanged. No external source import or baseline advancement.

Stream transfer now returns its own non-success result directly, leaving the
saved extraction outcome intact instead of overwriting and restoring it. The
staged input uses the existing always-created COM owner. Fatal extraction or
staging-open errors retain a separate path: Cancel has no item-error report,
other failures report DataError while preserving their original HRESULT.
Nonfatal item outcomes still reach the original callback and allow healthy
siblings to continue when accepted. Output release and progress publication
remain at their existing points.

Public extraction contract cases now reject output writes with E_ABORT, E_FAIL
and S_FALSE, both after successful decoding and after CRC-damaged partial output.
They require the write outcome and no completed-item report. A controlled missing
staged file verifies that the opening error takes priority over an error-report
callback's Cancel result. Existing cases cover CRC/healthy siblings, native
selection, password retry/cancellation and cumulative progress. These checks do
not inspect source text or private adapter structure.

Linux passed the four selected extraction/request suites: 60 checks without
skips, including all seven new error-precedence cases. Source inventory and
whitespace checks passed. Shared result handling changed without platform
branches or native ABI changes, so secondary platforms were not selected.
Selected scratch was empty before and after validation; configured four-job
builds retained incremental outputs and compiler caches. No full/slow or
appearance gate ran.

## Joining a stopped backend request — 2026-11-02

Reviewed the complete worker-to-GUI request/password helper and source-extraction,
listing/open and reopen consumers. The local adapter retains the recorded native
stop semantics and original algorithms; no source import or baseline changes.

The request now leaves password/progress processing once a stop result is
observed, performs its existing stop transition once, then waits only for
completion. This replaces the separate cancellation-sent flag with control flow.
Pending retirement still prevents delayed start captures from executing;
Running-only checks still protect unrelated backend work and late password
delivery. A stopped Running request continues to join completion before returning
the saved outcome. Non-S_OK password outcomes include S_FALSE, and an observed
stop retains priority over an already published completion result.

Pending/busy regressions move from the extraction suite into a focused backend
request suite. New public-boundary cases check password cancellation, callback
error, missing password interface, S_FALSE, Running cancellation and completion
with an observed stop. They require the exact outcome, one cancellation where
needed and completion before return; the backend fixture can finish on a timer
if cancellation regresses. COM callback exception injection is excluded because
that interface is noexcept. No private-state or source-shape checks are added.

Linux passed backend_request and its four selected direct-consumer suites:
52 checks without skips. The first build exposed a nonportable test error-name;
the fixture now uses a portable arbitrary HRESULT and the same selection passed.
Source inventory and whitespace checks passed. Shared Qt request control flow
changed without native OS/ABI changes, so secondary platforms were not selected.
Selected scratch was empty before and after; configured four-job incremental
builds and compiler caches were retained. No full/slow or appearance gate ran.

## Cancellation before backend request dispatch — 2026-11-02

Reviewed complete worker-to-GUI request/password delivery, listing/open/reopen
and source-extraction consumers, native codec probe/backend publication and
relevant cancellation fixtures. Original CProgressSync::CheckStop at
`b88c6bc09339de32f7d18bbab900686b244f42ba` was compared with the existing
adapter; the complete original ProgressDialog2.cpp hash matches the baseline.
Its stop semantics and native algorithms remain unchanged. No upstream import
or provenance advancement.

A stopped request can now retire while still Pending, before GUI dispatch.
The queued start checks the same mutex-protected phase and ignores the retired
request. Previously the caller always had to wait for GUI startup followed by
queued cancellation, so even a pre-cancelled request invoked its start callback.
This also prevents a delayed callback from invoking captures after their waiting
caller has returned. Running requests retain the existing joined cancellation,
password and result path; unrelated busy backend work remains untouched. No new
phase, platform service or native cancellation policy is introduced.

The regression withholds GUI dispatch, requires the cancelled caller to return
E_ABORT, then drains the delayed callback and verifies that no operation started
or emitted completion. Its failure path drains GUI work before joining the
fixture, so it does not strand its own future. Existing checks retain occupied
backend refusal/cancellation, extraction, nested opening and retry coverage.
No pre-fix run is claimed.

Linux primary `20261102T050434Z-2b65b62d` passed source_extract_backend and
agent_opening: 31 checks, no skips. Build 13.663 s; tests 4.117 s. Portable Qt
request scheduling changed, without native platform/UI changes; secondary OS
acceptance was not rerun. Selected scratch directories were empty before/after,
incremental outputs and compiler caches retained. No full/slow gate or reports.

## Compression parameter boundary — 2026-11-01

Compared source CompressDialog `CInfo`/update-mode declarations, the current
Add request projection and UpdateGUI property conversion with backend/native
writer consumers. The original CompressDialog.h inventory at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git> matches its pinned complete-unit hash. The
existing adapted declarations move unchanged into the compression module's
`CompressInfo.h`; field order, types, initial values and inherited Qt adaptations
are preserved. No new upstream code, omitted branch or revision advance is
involved. The existing manifest extraction map records both transferred symbols
and their local owner, so later source reviews include the extracted header.

ArchiveBackend now includes the parameter contract and native archive interface,
without importing compression controller/browse declarations through its public
header. The UpdateGUI service header consumes the same contract, while its
implementation, Add presentation and controller-based fixtures include their
controller explicitly. Selection,
validation, persistence, property overrides, encryption, update and writer
algorithms are unchanged. No new behavior test or measured build-speed gain is
claimed; existing creation/update suites and consumer compilation validate this
declaration/dependency refactoring.

Baseline inventory also lacked the preexisting local ArchivePropertySource
implementation/header from the generation-bound metadata change. They are now
registered as local integration; their code and original source hashes are
unchanged. Full baseline verification uses an isolated recorded-revision
reference; the existing external checkout remains untouched. Ordinary builds
continue to use repository-owned source archives only.

Linux validation passed the existing compression, compression_advanced,
codec_creation_ui, source_update_backend, native_update_publication,
archive_backend, ui_update_modes and shell_update_gui suites. GUI and File
Manager entry components also compiled. The first focused build exposed a
missing controller forward declaration in UpdateGUI; it was added before the
passing build. The publication suite initially lacked its official SFX runtime;
the existing runtime-preparation workflow supplied it, and that suite passed
on its own repeat. The other seven passing suites were not repeated.
Ordinary four-job builds and incremental compiler caches were retained.
No native platform branch changed, so secondary OS, independent application
runtime, appearance, full and slow-suite acceptance are not claimed.

Source: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

Reviewed FileManager `ProgressDialog2.h/.cpp` (`CProgressThreadVirt` result
initialization/Process and `CProgressSync` stop state), `ExtractCallback.cpp`
initialization and overwrite decisions, together with persistent ArchiveBackend
request entry points, process startup/completion, cancellation, early validation,
overwrite preparation and the HRESULT consumer in RunArchiveBackendRequest.
Upstream operation owners provide a fresh result/stop state for each request;
the Qt transport reuses its backend across requests. There is no upstream
persistent-QProcess request initializer to copy.

Seven entry points could refuse a request before process startup without
replacing the preceding request's HRESULT. Fourteen regressions reproduced
S_OK after a previous success or E_ABORT after a previous cancellation for
Create, Extract, Test, Add, Delete, Rename and Replace. They now initialize
their result before validation. The mutation entry points reject a busy
backend before changing request state or restoring listing metadata.
Empty Delete remains a no-op.

Extraction had a related retained-state error: overwrite preparation precedes
process startup, which was the point that cleared cancellation. After a
cancelled Test, accepting Yes in the next Extract overwrite prompt returned
"Operation canceled" and left the old file. A real ZIP regression reproduced
this. A newly accepted extraction now clears the prior cancellation before
overwrite preparation; cancellation of the current prompt is still honored.

Existing source-extract/source-update/hash request owners already initialize
their results. They were reviewed with listing, multi-stage completion and
failure/publication paths to keep the correction at the Qt request boundary.
No archive validation, mutation, overwrite algorithm, upstream body, dependency
or command branch is copied, replaced or omitted by this change.

The pre-fix failures were reproduced on macOS arm64 / Qt 6.12, working tree
based on `41a6c47`. Both regressions exercise operation signals/results and
unchanged or correctly replaced local fixture bytes. The existing overwrite
suite also retains Cancel/Yes-to-all/No-to-all and encrypted queue checks.

Incremental application and test builds passed. A final set of 19 focused
suites passed on macOS and Linux x64 / Qt 6.12. Run
`20261017T214943Z-5b5325cd` passed all changed backend suites on interactive
Windows x64 as well: `archive_backend`, `archive_mutation`, `extraction_options`,
`archive_creation_lifecycle`, `source_update_backend` and `archive_progress`.
The broader Windows run passed 18/19; its sole failure was an unrelated Options
resize expectation. The [test-only correction](options-platform-visibility.md#переносимость-геометрической-проверки--2026-10-18)
passed separately in run `20261017T215830Z-4e302ed8`. Linux/Windows used the
persistent `current` worktree and compiler caches; Windows symlink fixtures
used the existing elevated task mode. No full/slow gate was run.

## Retained listing and password state — 2026-11-01

Reviewed the complete listing/start/completion, cached-list restoration,
extract/test, source-entry recovery, password and cancellation consumers. The
retained listing cache is local integration; the original Agent ReadItems owner
remains responsible for native folder metadata. No native reader, decoder or
upstream algorithm is imported or changed.

The backend no longer stores unused listed/encrypted/prompt flags or a second
password copy. The active read operation already owns password delivery and
waiting. Cached snapshots now retain only file identity, entries and the format
needed by source recovery. Removed the private allowLimited branch: both callers
always selected its permissive default. Actual native read-only capabilities,
limited-read consent and published encryption/limitations/container metadata
retain their existing owners and behavior. Cancellation and completion signal
ordering are unchanged.

Two public behavior rows cover plain ZIP and encrypted-header 7z: list two
archives, extract the first after listing the second, replace the first with a
different valid archive, reject cached Extract/Test until it is listed again,
then extract its new entry and test the other retained archive. The changed-file
case checks rejection before destination creation; extracted bytes establish
which source and listing were used. No private-field or source-presence checks
were added.

The seven focused listing, extraction, test and recovery suites passed first on
Linux x64 (`20261101T034936Z-567d79fd`), then native macOS arm64 and interactive
Windows x64 (`20261101T035448Z-e1245eed`), all with Qt 6.12 and existing incremental
trees/compiler caches. macOS recorded 165 passing cases; Windows recorded 164
and one expected skip for colon filenames. Linux's final LastTest.log retained
only the selection header, so a verbose archive_backend-only follow-up
(`20261101T040116Z-33238908`) independently confirmed all 60 cases, including
both new rows. The original seven-suite run's stdout and current-token report
record success; no complete Linux per-case total is claimed. This also accepted
the C++ changes after the operator's FUSE repair; the earlier dependency-file
EEXIST build failures are retained separately. No full/slow gate was run.

## Read worker result ownership — 2026-11-01

Compared the complete worker opening/dispatch/error/cancellation path and all
constructor consumers, including retained-reader materialization and bounded
output tests. The original Agent ReadItems and Extract bodies retain their
metadata and native Extract responsibilities; the Qt dispatcher is local
integration. Existing listing projection, callbacks, path validation, extraction
adapters and native algorithms are unchanged. No external code is imported.

The worker now dispatches its four declared operation kinds directly and owns
the listing result exclusively. This removes the additional generic callback
and backend-held shared output pointer. Completion takes the joined worker's
result before publishing signals, preserving reentrant publication and release
behavior. Existing checks already cover deletion during publication, opening
another archive from a diagnostic, retained lists, password retry, cancellation,
directory/entry output and bounded retained-reader extraction; no implementation
presence test was added.

The listing adapter is compiled once with its shared worker and directory-read
consumers in the filesystem resource library. Keeping it in the higher-level
location library caused an undefined reference in the Linux native-test link
after dispatch moved; the target ownership correction resolves that dependency
without repeating sources or adding executable-specific link ordering.

After that correction, eight focused suites passed first on Linux x64
(`20261101T040753Z-90f1c597`), then native macOS arm64 and interactive Windows x64
(`20261101T041001Z-eb1c55d3`). The direct native_source_extract consumer passed
separately, Linux first (`20261101T041333Z-f0e1b2c2`), followed by macOS and Windows
(`20261101T041512Z-c8682a67`): CRC/partial output, password cancellation/retry and
destruction while waiting are included. Combined totals are 221 passing cases
on Linux, 221 on macOS and 220 on Windows, with one expected Windows skip for
colon filenames. Full current test logs were retained for all nine suites on
each OS. Checks used Qt 6.12, existing incremental trees and compiler caches;
completed test fixtures were removed. No full/slow gate was run.

## Single-entry extraction request — 2026-11-01

Reviewed the complete source-entry request, native selection/output/close path,
partial-result projection, cancellation, cleanup and file-resource consumer.
Compared Client7z GetStream, SetOperationResult and Extract at the recorded
9128b80e3a471108678e2b3ec3c985861c8d7b0f reference; its complete source-unit hash
matches the baseline. The existing private-stream adaptation, native decoding,
bounds/literal selection and caller-owned metadata remain unchanged. No external
code is imported or replaced.

The source-entry caller now prepares its worker directly and uses the common
read launch boundary. Entry name, output path, alternate-stream selection and
recovery-format eligibility belong to that operation; four duplicate backend
fields are removed. Completion cleans the operation's own path. Requested,
cached and actual-format recovery eligibility, closed/nonempty partial-output
requirements and the public item result retain their previous behavior.
Existing native-source checks cover successful bytes, explicit/automatic CRC
failures, opening warnings, password cancellation/retry, rejected outputs and
destruction while waiting; no private-state assertions were added.

All nine selected source-entry, link, recovery/output and shared-launch suites
passed first on Linux x64 (`20261101T041826Z-8d5f75c9`), then native macOS arm64
and interactive Windows x64 (`20261101T042049Z-89e4c687`), using Qt 6.12 and the
existing incremental trees/compiler caches. Linux and macOS each recorded 209
passing cases and four expected Windows-only NTFS/reparse skips; Windows
recorded 212 passing cases and one expected colon-filename skip. Full logs and
current-token reports were retained, and completed fixtures were removed after
checking active processes and runner locks. No full/slow gate was run.

## Native overwrite question metadata — 2026-11-01

Compared original FileManager ExtractCallback AskOverwrite and OverwriteDialog
resources, types, initialization, formatting and answer handlers, plus the native
ArchiveExtractCallback caller, Qt directory adapter, joined wait, GUI publication
and cancellation/receiver-lifetime consumers. The reference ExtractCallback unit
matches the recorded SHA256 c3b4e671cc6cf45666ba40cacc9ae3596f4d6d88cfdfb08a29f3268a55dacda9;
OverwriteDialog resource dependencies match the existing pinned contract.
No native algorithm or dialog body is imported or replaced.

The directory adapter previously discarded existing-file metadata, formatted
incoming time as technical-listing text and narrowed its unsigned size. The
window reparsed the timestamp with a seconds-only pattern, restatted the existing
path and displayed zero for unknown or unrepresentable incoming sizes. A single
owned question now transports both native file snapshots, preserving optional
FILETIME and UInt64 values through the queued boundary to the existing dialog.
The new transport type is local integration. Native answer mapping, cancellation,
path revalidation after the answer, dialog structure and platform modality remain
unchanged.

The native selected-directory regression also checks both snapshots and an exact
subsecond timestamp. Two window behavior rows exercise a native metadata snapshot
with a size beyond INT64_MAX and completely absent metadata, checking visible
details and Cancel preserving existing bytes. Normal/compact reports cover both
states. Existing overwrite answer, cancellation/reentry and receiver-release
checks remain part of the focused acceptance selection.

Seven focused suites passed on Linux x64 (`20261101T044001Z-4ecad203`), with an
ui_operations-only follow-up (`20261101T044549Z-41d9776f`) after correcting the
test's UTC setup order and an explicit native type include needed on Windows.
macOS retained six unaffected passing suites and passed ui_operations plus the
two Cocoa question rows in the follow-up; Windows x64 passed all seven suites
in the interactive elevated run `20261101T045028Z-91906e21`. The accepted scope
contains 162 Qt cases on Linux, 166 on macOS and 161 on Windows; the sole skip
is the existing Windows colon-filename case. The resource comparator also
passed on each OS. The initial compile/fixture failures and passing follow-ups
are retained separately rather than reported as a new all-suite macOS run.

Current normal/compact JSON confirms complete unsigned sizes, optional missing
metadata, timestamps and visible detail/footer controls: Linux offscreen,
Windows native QPA and macOS Cocoa for the new question states. Existing EN/RU
geometry checks passed. This is source/data/geometry acceptance, without a new
rendered original-Windows pixel comparison. All checks used Qt 6.12 and the
existing incremental trees/compiler caches. Completed fixtures were removed;
current reports and failure/follow-up evidence were retained. No full/slow gate
was run.
