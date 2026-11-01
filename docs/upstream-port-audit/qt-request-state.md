# State of a new archive request — 2026-10-18

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
