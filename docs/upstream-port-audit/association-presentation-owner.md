# Association conflict presentation — 2026-11-01

## Complete checkbox choice publication — 2026-11-04

An explicit checkbox choice can require two native ChangeState steps. Undoing
an uncommitted choice over another handler passes through Clear before restoring
Other. Previously each step emitted Changed; a synchronous Apply observer could
request an unwanted association write while that temporary Clear was exposed.

The Qt adapter now blocks its page notification across the complete checkbox
choice and publishes the final changed state afterward. The existing outer
bulk blocker continues to combine multiple choices into one completed update.
Native state cycles, row refresh, disabled-format filtering, Other restoration,
Apply/conflict/alias handling and native write policy are unchanged; no new
state or controller is introduced. Advanced source-list actions retain their
existing notification boundary.

The complete presentation implementation now lives beside SystemPage in
`settings/SystemPagePresentation.cpp`, in the same `sevenzip_location` target.
Translation contexts/text are unchanged; catalog source-location filenames
follow the move. Widget construction, layout, keyboard and focus bodies are
unchanged. A proposed extra row record was rejected because the existing fixed
source-index mapping already serves those consumers.

Compared complete original SystemPage.cpp/.h/.rc and SystemPageRes.h at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>, including initialization, state cycles,
Apply, resources and handlers, with current Qt selection and options consumers.
All complete original hashes match the existing baseline. Native SystemPage.cpp
and its header remain unchanged; the moved Qt adapter retains its local inventory.
No external code is imported or original provenance advanced.

The new public-signal regression stages a checkbox choice, subscribes an Apply
observer, then undoes that choice. It checks that the original handler is retained
without a native write through an injected service. Before the fix it reproduced
one unwanted write; it does not assert private state or notification call counts.
No user associations are used or modified by the checks.

Linux `system_page`, `options_integration` and `options_keyboard` passed:
40 checks without failures or skips, including the regression and the existing
filtered/bulk, source-cycle, conflict/alias, Apply/Cancel and keyboard cases.
The portable Qt notification boundary and unchanged platform services make Linux
sufficient; no new Windows/macOS, full/slow or appearance acceptance is claimed.
Selected scratch was empty before/after. Configured four-job incremental builds
and compiler caches were retained; source inventory/whitespace checks passed.

## Completed choice projection — 2026-11-04

Rechecked original SystemPage initialization, row refresh, mixed-selection
ChangeState and Apply with current Qt checkbox/bulk selection, availability,
native alias/conflict refresh and options observers. Complete SystemPage.cpp/.h/.rc
originals match the existing baseline at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. No external code is imported or
provenance advanced.

Single-row refresh now updates only its native row. The existing Changed
notification synchronously refreshes the Qt choice projection before external
observers run. The bulk selector's existing signal blocker therefore coalesces
that projection along with the state notification, instead of rebuilding every
checkbox, label and filter after each row. Initialization and Apply already finish
with availability/choice synchronization, including failure and native alias
redetection; those paths remain unchanged. No additional batching state is added.

Native ChangeState and Apply bodies are byte-identical. Original selection
cycles, Other-handler restoration, writes, revision conflicts, icons, error
signals, Apply/Cancel and visible layout/text retain their existing behavior.
The existing mass-selection regression now also observes Changed delivery and
checks the completed selection/count view inside that public notification.
This protects observer consistency and batch notification behavior, without
counting private refreshes or native detection calls.

Linux passed system_page, association_services, options_integration and one
isolated native MIME presentation case: 41 Qt checks, with the existing
Windows-registry-only case skipped. No user associations were modified. Scratch
was empty before/after; configured four-job incremental builds and compiler
caches were retained. Native registration, layout/text and platform branches
are unchanged; no new Windows/macOS, full/slow or appearance acceptance is claimed.

## Conflict presentation — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared SystemPage state,
initialization, icon selection, mixed-selection changes and Apply bodies,
their types/resources and local options/presentation/service consumers.
The complete original SystemPage.cpp hash matches the existing baseline.
No external import or provenance advancement.

The Qt service's existing concurrent-change protection reloads a handler after
ERROR_RETRY. That path updated the handler text but retained the previous icon
index. Initialization, conflict recovery and native alias refresh now share
preparation of the already-read state and icon. Read conditions and failure
semantics remain with their callers. Icon indices come from the icon collection,
removing a separately maintained count.

The source selection/Add/Delete/old-state/notification flow is retained. Native
MIME/UTType alias refresh and revision conflict handling remain local platform
adaptations; this change does not add them to upstream. No registry/MIME write
policy, layout, text, dialog or archive algorithm changes.

The existing conflict regression now observes distinct injected handler icons:
initial association, pending choice, externally changed handler after failed
Apply and subsequent successful Apply. This protects visible row consistency,
without pinning source structure or claiming native icon pixel parity. Platform
checks use owned fixtures or injected write services, preserving user associations.

Validation: system_page, association_services and options_dialog passed on
Linux first, then macOS and Windows. Linux additionally passed four native MIME
presentation variants and two isolated association-clear cases; macOS additionally
passed mac_system_page. Linux reported 55 passes, macOS 41 and Windows 36.
The Windows registry revision test was skipped on Linux/macOS and passed on
Windows. No failures. Existing incremental builds/compiler caches were retained;
completed scratch was removed after evidence capture. No full/slow gate repeated.

## Apply revision ownership — 2026-11-02

Reviewed the complete service detection/Add/Delete path, native platform
read/write boundaries, alias refresh and source page Apply/retry consumers.
Compared original SystemPage::OnApply at the recorded revision above; its full
original hash still matches the baseline. Source page algorithms and native
registration remain unchanged. No import or provenance advancement.

The current Apply now retains the expected revision as an owned value before
calling native Read. Previously its hash iterator referred to the mutable
observation cache across that call. An injected Read that performed fresh
detection after an external change replaced the cached revision, causing both
Add and Delete to accept the new state without reporting the conflict. The
public regression reproduced that false success before the fix, without
modifying user associations. It checks conflict/no write, retained handler and
successful explicit redetection/retry. No native OS reentry reproduction is
claimed. Own-write alias updates and post-write detection remain unchanged.

Linux association_services, system_page and options_dialog passed, 38 checks
with the existing Windows-registry-only skip. Incremental build/test times were
11.950/1.074 seconds. Only shared revision ownership changed; no native OS
registration, layout or text branch changed, so no secondary-platform batch,
full/slow gate or native visual acceptance was selected. The tests used injected
services; no user associations were modified. Scratch was inspected after
completion; incremental builds and compiler caches remain.
