# Native drag session ownership — 2026-10-18

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
