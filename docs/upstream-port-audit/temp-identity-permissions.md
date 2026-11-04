# Temporary-object identity and read permissions — 2026-11-01

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
