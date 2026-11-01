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
