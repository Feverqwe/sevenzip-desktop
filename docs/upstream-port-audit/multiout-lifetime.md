# Volume-writer allocation ownership and record state — 2026-10-18

## Native writer group owner — 2026-11-02

Reviewed the current complete MultiOutStream lifecycle/stream implementation,
header, native Update group construction/finalization and existing volume tests.
Compared the complete original group record, construction and cleanup bodies at
recorded `b88c6bc09339de32f7d18bbab900686b244f42ba`. Original MultiOutStream.cpp/.h
hashes match the baseline; this is not a new full upstream transfer audit of its
unchanged algorithms. No import or provenance advancement.

The Update group now uses the existing native CMyComPtr2 owner instead of a
separate raw implementation pointer and COM reference in every record. Its
class access and interface lifetime share one owner. AddNew allocates the record
before SetFromCls retains the writer; the local output interface already owns it
if that allocation fails. The existing cleanup pass retains first-error handling,
disables deletion at the same successful completion point and releases the group
after explicit destruction. No stream algorithm, restriction, close, rename or
publication policy changes.

The current native writer and SFX publication paths did not justify a larger
rewrite. In particular, replacing a retained SFX temp path with the shorter final
path reuses the recorded MyString capacity; it is not a current allocation gap.
Final volume rename clears Postfix without allocation. Existing constructor,
preallocation/truncation and file-close fixes remain intact.

Linux archive_volume_creation, sfx_lifecycle and native_update_publication passed
43 Qt checks without skips, plus standalone multiout_lifecycle passed. Incremental
build 11.634s, tests 1.182s. The existing regressions exercise variable-volume
output, protected archives, cancellation/failures, occupied paths and native
allocation cleanup; no representation-only test or pre-fix failure is claimed.
One completed registry record was removed after its object was confirmed absent;
other selected scratch was empty. Build trees/compiler caches retained. No
secondary-platform, full/slow or new appearance acceptance run.

## Original lifetime corrections

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed complete `CPP/7zip/Common/MultiOutStream.h`, its initialization,
creation/reopen/close/destruction and write paths in `MultiOutStream.cpp`, and
the native `UI/Common/Update.cpp` and QtArchiveVolumeOutput consumers. Stream,
COM and native file owners retain their existing source implementations.

Both production consumers allocate the writer and assign its Prefix before
calling Init. Prefix assignment can throw on allocation failure. The destructor
then reaches Destruct with an empty Streams vector but an uninitialized
NumListItems value. Reused allocation storage reproduced a failed cleanup after
an injected name-allocation failure.

CreateNewStream also created its file before allocating the owning Streams
record. An exception during registration left an untracked temporary file.
The same function updated RealSize on a local record after copying it into
Streams. The retained record could then skip a required truncation: preallocating
several volumes and shrinking to zero left bytes in the final first volume.

The constructor now calls the existing source InitLinkedList method. Registration
precedes file creation; a failed or throwing create removes the unlinked record
without deleting any unowned collision path. After successful creation the
original linked-list publication runs, and SetSize2 updates the retained record.
Init retains its initialization and size checks; no source operation or branch
is omitted. The adapted header moved from third_party into
`src/CPP/7zip/Common`, alongside the adapted implementation, with one
authoritative copy recorded in the baseline. Its FileStreams dependency uses
the existing include root after that move. Borrowed formatting is retained.

The standalone regression checks three observable failures: cleanup after a
failed Prefix assignment, files left after allocation failures during a
two-volume write, and stale data after preallocation followed by truncation.
The write case also preserves an occupied temporary name and its bytes. Reused
storage exposes the partial-construction defect. Tests use public stream methods,
without inspecting private fields or pinning allocation counts. All three failures
were reproduced before their fixes on macOS; the final focused suite passes there.

Related analyzer warnings were also reviewed: CVolStream's COM member releases
the local file owner on early CreateNewStream returns, and Init rejects a zero
final volume size before Write can divide by it. These warnings did not require
changes to the streaming algorithm.

The final application build and eight writer/volume/update/SFX/lifecycle suites
passed on macOS arm64, Linux x64 and Windows x64 / Qt 6.12 (guest run
`20261018T095007Z-7f90abf1`, identical guest source digest
`0e4a0ee3a67602ef26ab556fe001a0897eb9bb583c2bf929906f724f6a14cb18`).
On macOS, SFX discovery initially failed; the focused rerun with the project's
official runtime on PATH passed. The additional metadata-storage suite passed
on macOS/Linux and failed Options geometry on Windows before archive writes,
as in the earlier constructor-only snapshot; that UI scenario is not accepted.
The upstream baseline verifier passed after the header move. No full gate,
opt-in slow matrix or rendered UI comparison has been run for this focused change.

## Qt volume publication owners

The follow-up review found the same ownership ordering at the Qt boundaries.
QtVolumeTransaction created its stage before recording either path, and appended
to a separate rollback list after each successful rename. QtArchiveVolumeOutput
also appended published names after renaming. A throwing list allocation could
therefore leave a created or published file outside cleanup accounting.

Both adapters now prepare the owning names before filesystem changes. Failed
or throwing stage creation removes its pending records without deleting an
unowned collision path. Successful publication advances a scalar count over
the prepared names; the archive-output worker rolls that prefix back on failure
or cancellation and transfers the complete result with a list swap on success.
No path registration allocates after its file has been published.

Reviewed the complete adapters, VolumeOperation split/combine callers and the
source CThreadSplit/CThreadCombine bodies and types in PanelSplitFile, together
with the existing CMultiOutStream/CopyStream consumers. Upstream has no Qt
transaction/publication service to reuse: its workers write directly through
NIO. Those source algorithms, naming, reads/writes, preallocation and progress
branches remain unchanged; these corrections are confined to existing Qt
publication/rollback boundaries. No new source omission or dependency was added.

The allocation gaps are established by source review; no failure injection or
standalone Qt reproducer was used. Existing operation suites exercise Unicode
paths, success, occupied destinations, cancellation, rollback and SFX consumers.
The volume geometry test was also corrected to open the redesigned Additional
section before inspecting its volume field. It retains its visibility, footer
separation and parameter-validation checks; it no longer assumes the field is
visible inside a collapsed section.

Focused acceptance used existing incremental builds and compiler caches on
macOS arm64, Linux x64 and Windows x64 / Qt 6.12. volume_operations,
volume_workflow and sfx_lifecycle passed on all three systems in the initial
batch; archive_volume_creation passed its 24 operation cases but failed the
two stale collapsed-section geometry cases on each system. Guest run:
`20261018T133852Z-dfbd7a5c`. After correcting only the test preparation,
archive_volume_creation passed completely on macOS (3.13 s), Linux (3.53 s)
and Windows (7.61 s), guest run `20261018T134052Z-3493c8b8`.
The unchanged passing suites were not repeated. Generated widget reports cover
the normal/compact volume-field checks; no new appearance change, screenshot
comparison, full gate or opt-in slow suite is claimed.
