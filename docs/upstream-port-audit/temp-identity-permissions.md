# Temporary-object identity and read permissions — 2026-11-01

## Dialog-owned temporary path scope — 2026-11-05

Reviewed the complete browser listing, navigation, confirmation/deletion,
context actions and detached opening with registry claims and retained snapshots.
Original BrowseDialog2 confirmation/enumeration and its recorded baseline remain
unchanged; no external source or revision is imported.

The existing owned-path check now belongs to the dialog and reads its temporary
root directly. All six listing/action call sites use the same owner-bound scope
instead of repeatedly passing that root. Identity, canonical parent, containment,
component link/junction/existence and final root-identity checks are unchanged.
Registry claims, active/external protection and modal snapshots remain separate.
No path policy, native filesystem action, UI or new state/cache is introduced;
existing cleanup and detached-launch tests exercise the observable contracts.

Linux temp_cleanup and temp_detached_launch passed 32 checks without failures or
skips. Incremental builds and compiler caches were retained. No native platform
service, visible text or layout changed; no secondary-platform, full/slow or
new appearance acceptance is claimed.

## Temporary browser presentation and input owner — 2026-11-04

Rechecked complete original BrowseDialog2.cpp/.h/.rc hashes at
`b88c6bc09339de32f7d18bbab900686b244f42ba`, with its OnInit, notification,
keyboard and Enter handlers and the current Qt callers. The existing Qt
presentation now lives beside its dialog in `dialogs/browse`, rather than
among platform temporary-object services. Complete signal binding, focus/default
button handling and key-event routing have their own qttempbrowseinput.cpp in
that same module and panel target. Original algorithms and inventory hashes
remain unchanged; no external code was imported.

The moved bodies preserve construction/connection order, the existing
translation context, row formatting/sorting and selection, single-click rules,
Close/Enter behavior and keyboard shortcuts. Native reload, property errors,
modal snapshot guards, deletion leases, identity/path checks and detached-open
protection remain with their existing owners. This changes code ownership
without changing controls, layout, wording or platform services. Existing
temp_cleanup and temp_detached_launch behavior checks cover this scenario;
no file-location or private-structure regression was added.

## Shared temporary-item inspection owner — 2026-11-04

Compared complete native BrowseDialog2.cpp enumeration, property formatting,
reload and confirmation/delete callers at
`b88c6bc09339de32f7d18bbab900686b244f42ba`; the complete original source/header
hashes match the recorded baseline. The existing inspection implementation and
portable item record now live together in `browseiteminspection.cpp/.h`, in the
same browse module and panel target. The baseline's extraction mapping records
these owners without advancing original hashes or importing new code.

Both listing and property/confirmation paths use this owner. Recursive counts,
early interruption, incomplete-result markers, single-child details, native
name/time/attribute formatting and the POSIX no-follow enumerator adapter are
unchanged. Moved bodies were compared with the previous owner; differences are
limited to namespace/linkage, declarations and signature formatting. The dialog
continues to own snapshots, navigation, errors, confirmation and deletion leases.
No ownership/path restrictions, UI state, geometry or native OS behavior changed.

Existing Linux `temp_cleanup`, `temp_detached_launch`, `main_shell` and
`language_standard_buttons` checks passed. Compilation took 21.143 seconds and
tests 3.285 seconds, with configured four-job parallelism, incremental outputs
and compiler cache retained. No new implementation-presence tests were added.
Inventory and whitespace checks passed. Windows/macOS and visual parity were
not revalidated; full/slow gates and extra UI reports were not requested.

## Confirmed cleanup listing lifetime — 2026-11-04

Rechecked original BrowseDialog2.cpp/.h deletion, property lookup, context-menu,
reload and callers at recorded revision
`b88c6bc09339de32f7d18bbab900686b244f42ba`; both complete hashes match the
existing baseline. Native confirmation/deletion control flow remains, with
the existing Qt ownership and path checks. No source is imported or revision
advanced.

The Qt delete command previously retained selected indices across a modal
question but read them from the current listing after Yes. A public event
callback could replace that listing while the question was open. The browser
now publishes immutable shared snapshots and retains the confirmed snapshot
through deletion. Names, paths, files and owner records come from that same
snapshot; no separate request or copied listing is needed. Failed reloads keep
the published listing, and final refresh still updates the currently open view.
Commands from a context menu are discarded if its listing changed while open.
Retaining that snapshot also prevents address reuse from hiding a replacement.

Snapshot ownership does not hold a filesystem lease. Deletion still acquires
the current registry lock and rechecks identity, external protection, ownership
and path components. Properties failure still aborts confirmation. Native
question choices, partial-failure ordering and read-only/link boundaries are
unchanged. No production callback replacing the form during confirmation was
identified; this is a reachable modal contract gap, not a reported user incident.

A two-row regression opens private owned folders with matching Unicode names,
changes the view while confirmation is open, and checks that only confirmed
files are removed. Both isolated pre-fix rows failed. Existing tests retain
No/Cancel, active and external protection, replaced identity, links, properties
errors, sorting, refresh and ordinary context-menu behavior.

Linux temp_cleanup, owned_temp and temp_detached_launch passed 44 Qt checks,
without failures or skips. Initial full attempts timed out; verbose isolation
found that the new fixture's direct activation signal inherited keyboard
modifiers from an earlier Alt+Enter case. Using normal Enter events corrected
the fixture. No enumeration, timeout or concurrency change was made. Temporary
diagnostic logging was removed, affected consumers rebuilt, and the passing
behavior evidence retained. No Windows/macOS, full/slow or new appearance
acceptance is claimed. Incremental outputs and compiler caches are preserved.

Reference: <https://github.com/ip7z/7zip.git>, read-only snapshot
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared BrowseDialog2 reload,
Enter/parent callers, enumeration and publication, plus the native FileIO
handle-information service. Complete BrowseDialog2.cpp/.h/.rc and
Windows/FileIO.cpp/.h match the existing baseline hashes, whose default revision
is `b88c6bc09339de32f7d18bbab900686b244f42ba`. No baseline is advanced or external
source body imported. Ownership registration is project integration; native
enumeration, errors, deletion and link boundaries retain their existing owners.

Two interrupted Linux gates and a focused run after the storage repair exposed
an independent native-filesystem failure. A registered directory's birth time,
device and inode stayed unchanged after read permission was removed, while its
optional ioctl generation disappeared from the identity. The cleanup view then
lost the owned entry before its original enumeration error path could run.

Linux now binds metadata with O_PATH, validates the bound object and uses an
opaque file handle when supported. File handles are compared as opaque bytes;
no filesystem encoding is decoded and open_by_handle_at is not used. This
follows the [Linux file-handle API](https://man7.org/linux/man-pages/man2/open_by_handle_at.2.html).
The existing birth-time/generation fallback remains for unsupported filesystems.
Data-read permissions are still enforced by enumeration and file access.
Windows handle identity and macOS identity retain their previous representation.

Existing registry records use their complete previous identity for comparison.
Registration also respects their original lease key, preventing a second owner
from bypassing an active older lease after the fingerprint changes. Unknown or
unprovable identities do not become deletion candidates. The cleanup view uses
the same comparison boundary; active/external protection, path and link checks
remain intact. The source reload still publishes only a completed listing.

The existing cleanup regression covers denied navigation, retained root listing,
blocked root commands and recovery after permissions return. Existing ownership
tests cover replacement/inode reuse, links, publication and active leases. A new
compatibility fixture loads the previous serialized catalog, protects its active
lease, then claims it after release without modifying retained file contents.
It checks public registry behavior and runs only on Linux.

The ownership, cleanup and publication suites passed on Linux (44 checks),
macOS (43 checks) and Windows (42 checks and one existing Unix-permission skip).
The previous-catalog compatibility case runs only on Linux. The broader focused
Linux batch passed five suites, 103 checks without skips, and the default Linux
gate passed all 504 selected suites. Its 15 slow suites were excluded and Qt
tests reported 77 skips. Wider panel-state acceptance is recorded
[separately](panel-presentation-owner.md).

## Single refresh and complete selection publication — 2026-11-03

Reviewed the original BrowseDialog2 sort-key handler, sort/reload helpers,
enumeration and selection restoration together with current Qt controls,
ownership checks, properties/delete and external-open consumers. Complete
original BrowseDialog2.cpp/.h hashes match the existing baseline. No external
source, enumeration algorithm or ownership policy was added or changed.

The original key handler reloads again after the sort helper has already done
so. The Qt handler now delegates sorting and its single refresh to that helper,
as header sorting already does. This removes redundant directory/count work and
a second error dialog after a failed sort refresh. Sort direction, preserved
listing on failure, selection/focus and retry behavior retain their existing
owners. Claim locks, object identity, link/path checks and deletion are unchanged.

Qt row publication suppresses intermediate widget selection notifications while
rebuilding and restoring the listing. The existing language/status presenter runs
after sorting and cursor/selection restoration, publishing final availability and
counts without the previous per-selected-row work and duplicate terminal update.
Model insertion notifications and the existing wait-cursor lifetime remain intact.

A POSIX behavior regression denies directory read access after listing, invokes
sorting through the header or keyboard, and checks one error, preserved selection
and successful refresh after restoring access. It also verifies retained bytes.
Existing successful sort/navigation/delete/properties and detached-open cases
remain the direct consumers; no private call-count or source-text test is added.

## Properties before confirmation — 2026-11-01

Compared the complete original `PrintFileProps`, properties-window and deletion
bodies with their current Qt callers, row snapshot and error/reload path.
BrowseDialog2.cpp/.h/.rc and BrowseDialog2Res.h match the recorded original hashes
at `b88c6bc09339de32f7d18bbab900686b244f42ba`. No new source is imported.

The adapted properties call now returns its text or an absent result instead of
communicating success through a mutable dialog field. Both properties display
and single-item deletion consume that result before using row references: the
existing error path reloads the list and invalidates the old snapshot. Native
enumeration/counts, formatting, errors, ownership claims and deletion stay intact.
This preserves the existing refusal after a failed properties lookup.

A regression removes the selected file after listing, then invokes Properties
or Delete. It checks one error, no empty properties/confirmation dialog, refreshed
rows, a successful subsequent properties lookup and unchanged neighboring bytes.
Linux `temp_cleanup` and its direct browser consumer `temp_detached_launch` passed
28 checks without skips. Native shell launch branches and UI appearance were not
changed or newly accepted; no other platform run was required for this Qt result
adaptation.
