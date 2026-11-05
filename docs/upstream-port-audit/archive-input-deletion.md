# Archive input deletion snapshot — 2026-11-02

## Stop Move cleanup after a deletion callback failure — 2026-11-05

Reviewed source enumeration, processed statuses, native deletion, publication,
reopen and retry with the original Common/Update and Agent/ArchiveFolderOut
branches at the recorded b88c baseline. The native Move loop still requires a
successful operation. The local optional post-publication deletion step can
change that result, so Move cleanup now checks the updated result too. A callback
failure or cancellation leaves the remaining source inputs available while the
published archive is reopened as before. No source deletion algorithm, property
policy or original inventory changes.

The existing public folder-operation regression now covers deletion cancellation
with both Move and DeleteAfterCompressing selected, followed by Rename or
CreateFolder. On the old code both cases return E_ABORT but the input has already
been removed. Checks require preserved input bytes, successful next mutation and
matching bytes extracted from the published archive. Existing publication-failure
cases remain. This combination exercises the typed resource adapter; production
UI availability of that combination is not newly claimed.

Compared the original deletion loop in Common/Update.cpp, AgentOut's processed
input collection and ArchiveFolderOut's publication/Move cleanup at recorded
7-Zip commit `b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Complete original hashes of all three units
match the existing baseline. No source import or provenance advancement.

The Qt resource retains the native enumeration and processed statuses until
archive publication. Its snapshot now takes enumeration, statuses and symbolic
link options together at construction; consumers cannot separately mutate them.
The unused operation that marked every input processed is removed. The stored
enumeration callback is still cleared because its scan owner has ended. Failed
construction cannot publish a partially initialized snapshot.

Capture remains conditional on DeleteAfterCompressing, after successful writing.
The resource still clears old state before capture, moves the snapshot into a
local owner before deletion callbacks and clears it with compression options.
CommonUpdateOperation now also scopes pending deletion to its own invocation:
after acquiring the write lease it pins the resource, clears an old snapshot and
registers cleanup for every return or exception. Previously a failed publication
callback could leave that snapshot for a later Rename/CreateFolder operation.
The cleanup runs before the resource pin and write lease are released.
Publication/Move callbacks, reopening and native Move cleanup are unchanged.
The native deletion algorithm remains authoritative for processed-file checks,
size/time comparisons, link handling, directory order and cancellation; no new
deletion policy or independent filesystem algorithm is introduced.

The new archive_input_deletion regression injects E_FAIL at MoveArc_Finish,
requires the Add failure and then a successful Rename/CreateFolder through the
same folder operations, and checks that source bytes remain. This guards against
the second operation consuming the failed Add's snapshot. The failing callback
runs after actual archive publication; this is not a claim of atomic rollback
after publication. Current SetCompressionOptions callers are tests: a production
user path through this adapter is not established by this review.

Existing source_link_storage checks successful archive publication followed by
input deletion, failed/cancelled publication retaining inputs, link targets and
format capability refusals. source_update_backend checks the direct update
consumer. No representation-only test or pre-fix execution is claimed.

Linux archive_input_deletion, source_link_storage and source_update_backend
passed together: 66 passes, no skips; final incremental build 5.871s, tests
5.225s. Initial compile failures in the concrete COM pin and test-only HRESULT
constant were corrected; the resource is retained through public IUnknown.
Selected scratch contained no disposable files after the run; incremental trees
and compiler caches retained. No secondary-platform, full/slow or appearance
acceptance run.
