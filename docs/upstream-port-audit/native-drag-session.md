# Native drag session ownership — 2026-10-18

## Qt input and terminal reset — 2026-11-05

Reviewed the complete Qt event, gesture, choice, transfer and refresh path with
the original PanelDrag Enter/Over/Leave/Drop and source completion at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; the full original unit hash matches
the baseline. Native algorithms, transfer flags and platform transports remain
unchanged.

The Qt event filter now dispatches from one checked widget boundary. Blocked
targets and unreadable MIME data share the same refusal path, and final Drop
reset stays with the existing completion scope rather than a second rejection
branch. Rejected Enter still sends native Leave before discarding state, since
Qt does not deliver a later Leave for that refusal. Effect/acceptance publication
still precedes terminal cleanup and action refresh.

The existing hover regression adds an accepted entry followed by an unsupported
Drop, then a supported retry. It checks cleared highlighting, resumed ordinary
hover, no output files and a usable next drag through public events. Existing
source transfer, right/middle button, Open/Create/Cancel and archive refresh
checks retain their native behavior contracts.

## Completion belongs to the actual drop panel — 2026-11-04

Reviewed complete target positioning, Enter/Over/Leave/Drop, command choice,
source transfer flags, CompressDropFiles and CopyFromNoAsk completion against
recorded 7-Zip revision `b88c6bc09339de32f7d18bbab900686b244f42ba`.
Original PanelDrag, PanelCopy and PanelItems hashes match the existing baseline.
The native target uses the panel under the cursor; TargetPanelIndex is the
fallback for an application-level drop and follows focus/source-drag state.
Native CopyFromNoAsk refreshes and focuses its actual owning panel.

The Qt completion previously retained that actual panel but paired it with the
fallback index. Dropping into the inactive pane could therefore complete the
correct archive update and then focus its peer. Completion now derives both
projection and focus from the retained actual panel, removing the second,
potentially conflicting identity. The pointer is captured before native Drop
clears its target state. No new controller, persistent state or public API is
introduced. Native transfer decisions, ownership, cancellation, errors and
filesystem/archive operations remain unchanged; no source is imported or
provenance advanced.

Two existing-fixture regressions use the real native writer with focus on the
peer, covering left table and right icon targets. Both failed on the old code
at the final destination-focus check while the ten existing checks passed.
They also retain archive-byte, selection, provider and untouched-peer checks.
The final Linux native_drop_publication, drag_drop_ui, dropped_path_choice and
drag_protocol suites passed. Compilation took 15.401 seconds, tests 20.484
seconds, reusing incremental outputs, compiler cache and configured four jobs.
Inventory and whitespace checks passed. Only shared Qt completion changed;
Windows/macOS, physical OS gestures, rendered-original parity and full/slow
gates were not revalidated. Failure/result logs and useful builds were retained.

## Complete Windows OLE protocol owner — 2026-11-04

Rechecked the complete native protocol, its wire records, IDataObject and
IDropSource bodies, format enumeration and source/target callers against
PanelDrag.cpp at recorded revision
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Complete original PanelDrag,
EnumFormatEtc.cpp/.h and MyCom2.h hashes match the existing baseline.

The Windows transport now resides in `panels/PanelDrag_win.cpp`, beside its
owning typed panel interfaces and Qt MIME adapter, in the same sevenzip_panel
target. The complete moved block and remaining portable implementation are
unchanged. Transfer records, validation, HGLOBAL/STGMEDIUM ownership, deferred
archive extraction, capture restoration and receiver protection retain their
existing behavior. The three transport entry points keep the same interfaces;
Qt MIME presentation and shared Copy/Move, cancellation and temporary-lifetime
policy keep their existing owners. No algorithms are replaced or omitted and
no original hashes or source revisions advance.

Focused Linux validation passed drag_drop, drag_protocol and drag_archive
(52 Qt checks); interactive Windows passed native_drag_protocol, native_drag_qt
and native_drag_archive (24 checks), with no failures or skips. Existing behavior
checks cover wire validation and medium ownership, independent-process COM
exchange, retained MIME sessions, real archive bytes, cancellation and late data
requests after the panel closes. No new implementation-presence tests were
added. The checked scenarios do not establish manual Explorer drag gestures or
new visual parity. macOS was not needed for this Windows transport extraction.
Completed test scratch directories were empty; incremental builds and compiler
caches were retained.

## External receiver completion owner — 2026-11-04

Reviewed source OnDrag, data preparation, temporary protection, native OLE and
Qt transport completion with the receiver protocol, lease persistence and source
cleanup. Complete original PanelDrag.cpp at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` matches its baseline hash; original
source completion and target transfer semantics remain authoritative. No native
copy algorithm, external code import or provenance revision changes.

Both transports duplicated the criterion for releasing a temporary receiver's
protection. That criterion now belongs to CDataObject beside its existing
BeginExternalReceiver and FinishExternalReceiver transitions. Transports supply
their platform-specific cancellation confirmation at the same completion point;
the model retains the existing Drop_End and terminal flag checks. Cocoa's
uncertain native IgnoreAction rule remains in the Qt transport. Direct typed
recipient confirmation and exception-time retention are unchanged.

The existing real-ZIP receiver fixture additionally checks terminal processed
confirmation permits cleanup while a processed flag in an Over notification
retains readable bytes and prevents cleanup claims. Existing rows retain their
success, cancellation, extraction failure and throwing-recipient contracts.
No test pins the new private call structure or source layout.

Focused Linux archive-byte, source/target and protocol checks passed, followed
by Windows native archive-data and OLE protocol consumers. Source completion
and target decision bodies remain unchanged; the common criterion matches both
former transport conditions. Actual OS drag gestures and new Cocoa runtime
acceptance are not claimed for this policy extraction.

## Qt gesture completion scope — 2026-11-03

Rechecked original source OnDrag completion and target Enter/Over/Leave/Drop
cleanup with the current Qt gesture, transport, temporary-data, drop-choice,
hover and teardown consumers. Complete original PanelDrag and PanelListNotify
inventory hashes match the recorded baseline. No native transfer algorithm,
COM protocol, external-receiver retention rule or baseline changed.

The Qt adapter scopes source-drag cleanup around OnDrag and completed-drop
cleanup around target execution. Source guards restore before clearing the target
and resuming hover, with the active-drag guard restored last. Drop effect/acceptance
is published before target cleanup and action refresh. Enter/Move/Leave and refused
entry retain their existing separate lifetime and balancing notifications.

The live archive-source cancellation cases now verify ordinary hover selection
after cancellation in both table and icon views, while retaining source selection,
archive bytes and temporary-root checks before that hover. This detects a stuck
hover suspension through public mouse events; it does not test private guards or
claim a pre-fix product failure.

## Shared drop-menu decision — 2026-11-02

Compared original target ClearState, DragEnter, DragOver, DragLeave and Drop,
their button/source-flag types and consumers at recorded 7-Zip revision
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>. The complete PanelDrag.cpp inventory hash
matches the existing baseline. Current Qt input, drop-choice and native target
consumers were reviewed together; no source handler algorithm is changed or
imported.

The Qt Open/Create archive choice now reads the target's existing menu-drop
state through a const boundary. It no longer maintains another flag from
DragEnter. Right/middle-button retention, source transfer-flag precedence and
session cleanup stay with the target that already owns those decisions. The
current-event button check, ordinary external path choice, native context menu,
copy/move effects, source publication, cancellation and pending right-press
gesture retain their respective owners.

Existing public regressions cover source button flags, middle-button Drop after
release, both panels/views, cancellation, and opening/compressing/cancelling
Unicode archive or directory drops. No getter/representation test is needed.
Linux passed drag_protocol, drag_drop_ui and dropped_path_choice: 49 checks,
no skips. Inventory and whitespace checks pass. This common Qt state change
does not alter native COM/OS gestures, layouts or text; no new secondary-platform
or rendered-reference acceptance is claimed. Incremental outputs/compiler caches
were retained, and completed test settings were removed.

## Qt transport failure boundary — 2026-11-02

Reviewed the complete Qt transport/controller and source OnDrag, data materializing,
receiver protection and DragEnter/Over/Leave/Drop; traced the native receiver boundary.
Compared the original OnDrag return/error/DragEnd and temporary-input policy at
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>; complete PanelDrag.cpp bytes match the existing
baseline. No external code is imported or baseline changed.

A C++ exception from the Qt drag executor previously escaped the typed drag
service instead of returning to the source HRESULT workflow. In particular,
after exposing archive URLs it skipped DragEnd and the source's check retaining
an unconfirmed temporary root. The transport now reports allocation failures
as E_OUTOFMEMORY and other exceptions as E_FAIL through Copy_HRESULT, cancels
the drag with no transfer effect, and returns to those existing source handlers.
The active guard unwinds normally; the transport does not assert that an exposed
receiver has finished. Original copying, target decisions, protocol flags,
selection rules and external-receiver lifetime policy remain authoritative.

The real-archive fixture now throws an ordinary exception or std::bad_alloc from
its executor after reading the delivered URL/bytes. It checks an error is shown,
source selection and archive bytes remain, DragEnd has retired the source index,
the transport is inactive and the retained bytes have an inactive externally
protected registry entry that cannot be claimed for deletion. Fixture-owned
bytes are explicitly removed after checking retention. The failure mechanism
was established from source; no pre-fix process-aborting run is claimed.
No native format/COM/OS branch, dialog geometry/text or decoder changes.

Linux drag_archive, drag_drop and drag_drop_ui passed 45 checks without skips;
run `20261102T012716Z-f53259ee`, build 15.116 seconds, tests 9.344 seconds.
Both new exception rows passed through the real archive-byte fixture. This is
portable Qt error-boundary validation on the primary platform; no new native
OS gesture, Windows COM or Cocoa acceptance is claimed. No secondary platform
batch was needed for an unchanged OS-specific implementation. One completed
settings.ini was removed; incremental builds and compiler caches remain.
Inventory and whitespace checks passed.

---

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed `CPP/7zip/UI/FileManager/PanelDrag.cpp` DragEnter/DragLeave/Drop
COM retention and cleanup, the copied native IDataObject/medium adapters,
source-panel detachment, QtPanelDragTransport, QtFileManagerDrag::ReadData and
the complete QtNativePanelDragConverter/header with its direct tests.
Earlier module transfer/dependencies remain in the
[native protocol audit](../archive/qt-port/upstream-port-audit/p3-05-2026-10-10.md).

The original target retains the active IDataObject through its drag state.
Qt's MIME converter additionally caches a native owner and exposes a token
through lazy QMimeData conversion. A new conversion could replace that cached
owner before Read associated it with its new MIME object. Destruction of the
previous MIME object then cleared the new token and native owner. The Windows
regression reproduced a missing native receiver for exactly that order.

The converter now retires the previous destruction connection before publishing
a new session, rebinds even when the MIME object is reused, and checks the
captured token before clearing. Explicit Clear also disconnects the old owner.
Preparing the new owner/token before retiring the previous binding preserves
the existing session if preparation fails. This is a Qt lifetime adaptation;
there is no upstream QWindowsMimeConverter implementation to reuse. Native
formats, medium release, COM interfaces, archive-copy algorithms, source
callbacks and Drop decisions are unchanged; no source branch is omitted.

The regression checks both destruction orders around the next Read, Unicode
names, retained receiver access after MIME destruction and release after the
last receiver. Existing repeated-conversion tests cover reuse of the same MIME
owner; platform clipboard and independent-process COM tests exercise actual Qt
conversion. No allocation injection or standalone Qt reproducer is used.

Windows x64 / Qt 6.12 acceptance: native_drag_qt, native_drag_protocol,
native_drag_archive, drag_drop, drag_archive and temp_detached_launch passed
in `20261018T130147Z-c169d617`. The seventh selected suite, temp_cleanup,
initially could not create its symbolic-link fixtures under the ordinary token;
it passed with the elevated desktop in `20261018T130247Z-8553a04d` (3.22 s).
The new before-next-read row failed on the baseline in
`20261018T130058Z-591719ce`; both orders pass after the fix.
Selected targets were rebuilt in the persistent worktree using ccache.
This adapter is compiled only on Windows; no macOS/Linux runtime claim,
application-wide gate, slow suite or visual acceptance follows from these tests.
## Qt drag/drop input owner — 2026-11-04

Reviewed complete current Qt drag/input/presentation units and native
OnDrag, DragEnter/Over/Leave/Drop, target reset, command/result and panel
publication consumers against `PanelDrag.cpp` at
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Its complete original hash matches
the existing baseline. The original transfer/selection algorithms and recorded
Qt adaptations are unchanged; no external code or revision is imported.

The existing Qt input unit now owns event filtering, MIME acquisition, key/button
translation, drop delivery and terminal reset alongside right-button gesture
handling. Complete function bodies move unchanged from the general drag unit.
The same controller instance and state remain authoritative; source startup,
native presentation services and execution/publication continue with their
existing owners. No interface, data model or build component is added.

Rejected entry still balances native Leave, synthetic Drop still revalidates
the target, and reset still releases external native data and restores hover
according to the active source drag. Right/middle menus, deferred context-menu
delivery, transfer flags, Open/Compress/Cancel, selected target publication,
revocation and temporary-source ownership retain their current behavior.
Existing integrated event/result regressions provide acceptance; no test of
source placement is added.
