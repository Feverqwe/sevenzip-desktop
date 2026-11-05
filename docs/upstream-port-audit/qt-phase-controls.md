# Controls across asynchronous phases — 2026-10-18

## One current worker-launch path — 2026-11-05

Reviewed the complete current backend read/hash launch and completion paths,
retained pause transport, worker checkpoint/cancellation and synchronous receiver
lifetimes. The original ProgressDialog2.cpp CheckStop body at
`b88c6bc09339de32f7d18bbab900686b244f42ba` matches the recorded full-unit hash;
its native stop/pause policy is unchanged. The Qt phase-launch boundary is local
integration, with no new imported code or source revision.

The private phase starter now directly starts its retained QThread after
phaseStarted delivery. Both actual callers use this path; the unused alternate
launch callback and discarded boolean result are removed. Pause synchronization
still precedes native work, and both backend and worker lifetime checks remain
after synchronous notification. A receiver may still cancel/join, delete the
backend or replace the operation without starting a retired worker. No new
state, public API, worker, UI control or native synchronization rule is added.

Existing real read/hash regressions cover pause/cancel/join, replacement after
queued completion and deletion during phase notification. Transport checks cover
retained pause and failed pause application. These observe behavior rather than
requiring a private function signature; no additional test is needed for the
removed unused launch branch.

Source: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

Reviewed FileManager `ProgressDialog2.cpp` pause/priority handlers and
`CProgressSync::CheckStop`, GUI `UpdateCallbackGUI.cpp` scan, compression,
open and deletion callbacks, and Common `Update.cpp` post-compression deletion.
Upstream retains one progress Sync across the operation. The Qt process/worker
transport must carry its current controls across asynchronous phase boundaries.
There is no upstream QProcess/QThread phase-launch implementation to copy.

The review traced backend process startup/completion, extraction and update
stage advancement, copy preparation, hash workers, creation scan, volume output
and input deletion, together with their checkpoint and cancellation owners.
Previously only CLI startup reapplied background policy; it did not reapply
pause. New volume workers also started with independent, unpaused Sync state.
A phase could therefore start work while the progress dialog remained paused.

The adapter now synchronizes the retained pause state at every process/worker
start. Worker synchronization precedes thread launch; lifetime guards suppress
launch if a synchronous consumer cancels/joins and releases the worker or owner.
Both pause and resume states are projected, including when reusing the creation
worker for deletion. Control failures use the existing error/cancellation path.
The existing engine-start signal still identifies CLI processes only.

This is a Qt transport adaptation. No upstream algorithm, source body,
dependency or command branch is copied, replaced or omitted by this change.
Archive publication, checkpoint semantics and deletion eligibility retain their
existing owners.

Validation on macOS arm64 / Qt 6.12, working tree based on `5c57c65`:
incremental application and affected test targets built. Eight focused suites
passed outside sandbox: `archive_progress`, `archive_preparation`,
`archive_creation_lifecycle`, `archive_volume_creation`, `progress_cancel`,
`process_pause`, `progress_priority`, `progress_priority_failure`.
The sandbox attempt passed six suites; its two priority suites were refused
by macOS process-policy restrictions, then passed outside sandbox.

New behavior checks cover retained pause and failed pause on a replacement
engine, and real create/volume/delete workflows paused at scan, CLI, volume
and deletion boundaries, then resumed through successful completion. A further
preparation regression verifies synchronous cancel/join before worker launch:
one failure completion, no published output and no later duplicate completion.
The direct consumers `checksum`, `archive_backend` and `source_update_backend`
also passed on macOS after incremental builds.

Linux x64 and Windows x64 / Qt 6.12 checks reused their persistent `current`
worktrees and compiler caches. Run `20261017T213454Z-bdb4ff23` passed all seven
available selected suites on Linux. On Windows the phase/control suites passed;
three existing lifecycle symlink fixtures could not create their links under
the Limited interactive token. Run `20261017T213704Z-e126224b` passed lifecycle
plus the three direct consumers on both platforms, using the existing elevated
interactive task mode on Windows for the symlink prerequisites. The macOS-only
priority-failure suite is not registered on these guests. Evidence is retained
under `build/runners/hyperv/` with those run IDs.

These are focused source/build/behavior checks. No full gate or slow suites
were run. This change does not alter layout; rendered Windows UI comparison
is outside this validation. The broader Qt adaptation review and its limits are
recorded in the [Qt adaptation review](qt-adaptation-boundaries.md).
