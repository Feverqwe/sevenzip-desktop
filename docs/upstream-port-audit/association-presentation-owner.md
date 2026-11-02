# Association conflict presentation — 2026-11-01

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
