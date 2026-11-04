# Archive folder lifetime across rollback — 2026-10-18

## Nested opening spool handoff — 2026-11-01

Reviewed resource-owned and panel-owned spool creation, opening, transport
publication, nested edits and release. Compared original CopyCoder copying,
Agent folder factory, FileFolderPluginOpen worker/modal handoff and Panel
temporary-file records at recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba` in
<https://github.com/ip7z/7zip.git>. Complete original inventory hashes match the
existing baseline. No external code is imported or baseline advanced.

Both spool paths share their existing seek/copy/progress/close sequence. Scoped
failure cleanup closes output before deleting it and is dismissed only after
successful publication. The opening manager likewise dismisses its cleanup
after a native or metadata transport is created, removing three separate
keep/completed flags. A resource-owned file still retains its registry lease;
materialized panel copies still belong to the original temporary-file record.
Direct streams/device inputs, native opening branches, limited-read consent,
changed-file prompts, update propagation and creation/permission policy remain
unchanged. Existing public ownership, cancellation and nested-edit checks are
used; no representation test or pre-fix product failure is claimed.

Linux acceptance passed `owned_temp_publication`, `archive_opening`,
`nested_edit_chain`, `native_parent_publication` and `binding_lifecycle`.
Windows additionally passed only `owned_temp_publication` and `archive_opening`
for its native close-before-delete behavior. macOS was not selected; shared
POSIX behavior is covered on primary Linux. The checks used existing incremental
trees, compiler caches and the four-job default. No full/slow gate or appearance
acceptance is claimed.

## Native copy password ownership — 2026-10-31

Compared original `CPanel::CopyTo` overloads, `TestArchives`, `CApp::OnCopy`,
open callback/password publication and extraction callback handling in upstream
FileManager `Panel.h`, `Panel.cpp`, `PanelCopy.cpp`, `App.cpp`,
`FileFolderPluginOpen.cpp` and `ExtractCallback.cpp` at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Complete original units match the
existing baseline hashes. The source keeps both a password value and whether
it is defined; an accepted empty password is distinct from an absent one.

Default native extraction and archive Copy/Move now use the existing panel
`CopyTo` overload to seed that state from its owning parent link. Removed two
duplicate password initializations from archive-to-archive and stream/device
copy callers. Scoped Extract no longer reconstructs password definedness from
the window's string projection. Explicit nonempty overrides still use the
existing reference-parameter overload. Callers consuming returned password state,
native algorithms, success/cancel rules, extraction validation, batch/fallback
credentials and read-only mount policy remain unchanged. No external code is
imported or baseline advanced.

A real encrypted-7z window scenario reproduced an extra prompt for an accepted
empty password. The regression now covers empty and Unicode/space credentials,
overwrite cancellation with retained destination bytes, native Test, two panels
with different passwords, return and successful extraction without extra prompts.
It uses existing native archive and widget-dialog fixtures; successful Test
summaries are distinguished from errors. Original geometry/redaction checks
remain in the same password-window component.

Eight focused suites passed on Linux, macOS and elevated interactive Windows:
`password_window`, `extraction_options`, `native_archive_test`,
`native_source_extract`, `ui_copy_routing`, `agent_stream_copy_ui`,
`ui_operations` and `archive_external_security`. Linux/macOS each passed 222 Qt
rows; Windows passed 221, with only the existing ordinary-filename colon skip.
Native link fixtures passed. CTest took 101.38, 68.21 and 155.18 seconds
respectively. Original-unit hashes, source inventory, task formatting and
whitespace checks passed. Incremental trees/caches were retained; completed
fixtures were cleaned after evidence capture. No full/slow gate, heavy
diagnostics or appearance acceptance is claimed.

## Archive navigation ownership — 2026-10-31

Reviewed opening selected/focused entries, ordinary archive folders, stream,
memory and disk-backed nested archives, failed/cancelled opening, Parent/Back,
temporary-file ownership and return selection. Compared the complete original
`OpenFocusedItemAsInternal`, `OpenSelectedItems`, `OpenItem`,
`OpenItemInArchive`, `OpenParentFolder` and parent-stack consumers in upstream
`PanelItems.cpp`, `PanelItemOpen.cpp` and `PanelFolderChange.cpp` at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. All three complete original units
match the existing baseline SHA256 values. Native opening, extraction, parent
stack, cancellation and selection algorithms remain unchanged; no source is
imported or omitted.

Removed three unused window navigation entry points and their declarations.
They reconstructed archive paths or searched entries separately from the active
source command path. Real row activation, Open Inside, folder entry and
Parent/Back already use the source panel with the existing Qt presentation and
focus adapters. Existing integrated navigation/nested-opening regressions cover
these consumers, including Unicode names, spaces, failed/cancelled opening and
temporary-file cleanup; no implementation-presence tests are added.

Windows acceptance exposed a pre-existing fixture limitation: the nested-update
test directly overwrote an inner archive while its disk reader was still open.
The fixture now edits the inner entry through the existing source native update
transaction, verifies the extracted new payload, and then tests returning and
publishing that changed archive into its parent. Success, failed update,
cancellation, original outer bytes and retained recovery copies remain checked.
No production file-sharing policy or archive update algorithm is changed.

Accepted `ui_navigation`, `nested_archive`, `nested_update`,
`native_folder_opening`, `root_command`, `address_combo`, `binding_lifecycle`
and `persisted_location` on Linux, macOS and interactive Windows. After the
fixture correction, only `nested_update` needed rebuilding/rechecking on each
platform; the seven unchanged suites retained their passing evidence. The
accepted scope has 116 passed Qt rows on Linux/macOS and 118 on Windows, without
skips. Source inventory, original-unit hashes, formatting and whitespace checks
passed. Incremental trees and compiler caches were retained; no full/slow gate,
heavy diagnostics or visual acceptance is claimed.

## Rollback folder lifetime — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed `CPP/7zip/UI/Agent/ArchiveFolderOut.cpp::CommonUpdateOperation`,
`Agent.cpp::ReOpen/Close`, `AgentProxy.cpp::CProxyArc2::Load/GetDirPathParts`,
and FileManager `App.cpp::OnCopy`, with the Qt transfer/rollback, reopen,
property transport and panel publication callers. The unchanged borrowed
module baseline remains recorded in `upstream-baseline.json`.

The source saves FolderItem before closing the handler and restores it after
reopening. The Qt interarchive Move rollback must additionally close the physical
reader before replacing the destination file. Its old bridge saved the folder
path after closing. On Windows the proxy can borrow UTF-16 names directly from
the native handler; reading those names after Close intermittently lost the ADS
folder and selection. On platforms with wider wchar_t the proxy copies these
names, which explains why the Windows failure did not reproduce on macOS.

The narrow close/reopen bridge now owns the path parts, ADS flag and folder
prefix before closing. Closed folders reject item access, while their saved path
remains available for reopening and selection restoration. The existing source
path walk and binding branches are retained; no format algorithm, update branch
or dependency is replaced or omitted. Upstream has no separate file-only
rollback bridge to copy. No upstream checkout or resource is changed.

The native folder opening regression now covers Close followed by ReOpen of a
Unicode ADS folder, including its path and contents. The existing peer-stream
Copy/Move scenario covers destination bytes, cancelled source deletion,
selection and subsequent operations. Its setup waits until the panel accepts
interaction; failures identify the operation stage and stop cascading actions.

Focused validation built the application and passed `native_folder_opening`,
`agent_stream_copy_ui`, `interarchive_recovery`, `panel_key_source`, `help_source`,
`help_ui` and `ui_layout` on macOS arm64, Linux x64 and Windows x64 / Qt 6.12.
Windows additionally passed three repeats each of peer-stream Copy/Move and
source keyboard tests. These cover a native Windows keyboard/recovery consumer;
the Help, icon geometry and peer-stream suites use their configured offscreen
platform. No full gate, opt-in slow suite or original Windows visual comparison
was run.

The other recorded failures were test synchronization/geometry assumptions:
Help waits for Qt's scroll-range publication, with a 60-second suite budget after
a measured successful Windows run of about 22 seconds; bookmark input waits for
an enabled list after navigation; icon cells may wrap when two do not fit the
viewport. Label/icon containment and non-overlap remain checked. These changes
do not alter product layout or key dispatch. Icon widget reports now survive
temporary fixture cleanup in the regular UI report directory.
The final `ui_layout` rerun passed on all three platforms after this report-path
change; current compact large-font widget JSON was reviewed on macOS and Windows.

## Retired interfaces and retained codec catalog

The subsequent lifetime review compared complete Agent close/reopen, proxy,
binding, extraction and update entry points with the same upstream revision,
including their panel text, sorting, properties and stream consumers. Upstream
FileManager releases its old folder interfaces around reopening; the Qt bridge
can retain them across rollback and asynchronous publication. The adapted owner
must therefore reject access through every retired interface.

Close now retires all published folders before releasing handler-owned names.
Fast name, prefix, size, comparison, ADS and stream entry points check the existing
binding generation, as do extraction and update entry points that previously
touched stale state before their common guard. Closed owners cannot publish a
fresh root backed by retired proxies. The initial-open diagnostic-folder branch
and its S_FALSE result remain preserved. No upstream proxy walk, sorting rule,
archive algorithm or dependency is replaced or omitted.

The Qt reopen adapter also retains the archive's original codec catalog instead
of consulting the backend's replaceable discovery result. The source opener
already receives its owning CCodecs; the existing Qt overload supplies that same
ownership boundary. A failing module discovery must not remove native WIM parent
and stream capabilities from an already open archive.

Expanded behavior regressions cover ZIP/WIM root and child interfaces in normal
and flat modes after Close, failed ReOpen and successful ReOpen, plus WIM reopen,
parent hierarchy, ADS support and extracted bytes after failed catalog replacement.
Before the fixes, all four closed-folder cases remained readable, and WIM reopen
after catalog replacement failed with E_NOTIMPL.

The application built and all 15 focused archive ownership, property, stream,
update/extract and interarchive recovery suites passed on macOS arm64, Linux x64
and Windows x64 with Qt 6.12. Partial AddressSanitizer builds of Agent.cpp,
ArchiveFolderOut.cpp and the lifecycle suite passed all eight cases on macOS and
Windows. Qt, codec modules and remaining dependencies were not instrumented.
Clang's static analyzer reported no findings for Agent.cpp. These checks validate
the selected lifecycle scenarios; no full gate, opt-in slow suite or visual
comparison was run for this nonvisual change.

## Item streams and property consumers — follow-up review

Reviewed the complete Qt item-stream wrapper, archive property transport,
native-property mapping, raw panel-property implementation and reopen adapter,
with resource/header owners, Agent GetStream and archive-to-archive transfer
callers. The wrapper retains its resource before native stream interfaces;
destruction releases those interfaces before the resource/catalog lease.
Raw property buffers belong to the retained metadata or native handler and
are consumed under its existing open/bound lifetime. Reopen replaces the
property binding after native metadata is available. The source update stream
is copied within the joined operation; source deletion and destination rollback
follow completion of that copy.

The source Agent GetStream and native TAR/FAT/NTFS/Ext stream constructors were
inspected at the revision above to distinguish retained input/handler references
from data validity after Close. Keeping a COM reference does not establish a
right to use a handler's invalidated data after explicit Close. No current
caller requiring that unsupported lifetime was established, and no speculative
stream snapshot or delayed-close policy was added. No new confirmed ownership
defect or omitted source branch was found in this selected scenario.

Existing agent_stream_update, agent_stream_copy_ui and native_archive_properties
passed together with verctrl on macOS arm64 (4/4, 7.20 s), Linux x64 (4/4,
6.77 s) and Windows x64 (4/4, 20.85 s), Qt 6.12, guest run
`20261018T134937Z-6240522b`. Filesystem ADS update rows remain Windows-only;
macOS/Linux skip those rows while exercising archive ADS. The targets rebuilt
incrementally with existing caches. No new production code/test, allocation
injection, standalone Qt reproducer, full/slow gate or visual acceptance was
part of this follow-up.

## Opening request scope — 2026-10-31

Compared the complete current opening service, source Agent folder binding,
resource/codec leases, stream materialization, native metadata publication and
queued metadata completion, including the original ArchiveFolderOpen binding
body. Existing source baselines and native open/reopen algorithms remain
unchanged; no external code is imported.

The optional metadata request is now created only when the opening needs the
queued metadata service. The per-opening service already retains the resource
until the published property transport takes its own reference. Removed a partial-native
check from the later metadata path: a native link has already returned through
the earlier native branch, so that check could never succeed. Actual partial
native opening, diagnostics, password/cancel handling and owned-file cleanup
retain their original branches. Existing behavior suites validate the complete
opening/return/failure and retained-layer workflows; no source-text or allocation
count tests are added.

Focused validation passed `native_folder_opening`, `nested_archive`,
`archive_layer_lifetime`, `binding_lifecycle` and `archive_layer_chain` on Linux
(52.35 seconds), macOS (57.56 seconds) and the Windows interactive runner
(92.42 seconds). Incremental builds and compiler caches were retained; no full
or slow suites, heavy diagnostics or appearance changes were introduced.

## Shared folder restoration — 2026-11-01

Compared the complete current CommonUpdateOperation, explicit folder ReOpen,
path capture and proxy lookup bodies with the original Agent/ArchiveFolderOut,
Agent and AgentProxy units in <https://github.com/ip7z/7zip.git> at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Their complete hashes, plus Agent.h,
match the recorded baseline. No external code or new algorithm is imported.

The original path restoration walk now has one private implementation shared
by archive update and explicit ReOpen. Ordinary path lookup, the nearest
surviving parent, file ADS and root ADS retain their original branches. Each
caller still owns root binding, proxy generation and result publication. The
unused commented alternative walk was removed. No archive writer, publication,
rollback, consent or cancellation policy changed.

Public regressions reopen real ZIP/WIM archives after replacing their contents,
checking preserved Unicode subfolders and fallback when one or both selected
ancestors disappear. Existing mutation checks now also verify the reopened
folder path after Delete/Rename/CreateFolder. The ADS update workflow additionally
reopens both the file's streams and the empty root stream namespace, checking
their path and entries through the folder interface.

Focused validation passed agent_lifecycle, archive_proxy, source_update_backend,
agent_stream_update, external_editors_ui, native_folder_opening and archive_opening
on Linux first, then macOS and Windows: 117/117/121 passing cases, no failures.
The three filesystem ADS rows skip on each POSIX platform and pass on Windows;
archive ADS runs everywhere. Incremental build trees and compiler caches were
retained, and completed temporary fixtures were removed after evidence capture.
No full/slow gate, heavy diagnostics or visual comparison was needed for this
nonvisual change.

## ReOpen binding and execution — 2026-11-04

Reviewed the complete resource ReOpen dispatch, archive/device attachments,
joined refresh worker, update consumers, Agent folder/proxy restoration and
CArc/CArchiveLink reopen callbacks. Complete original Agent.cpp/.h,
OpenArchive.cpp/.h and FileFolderPluginOpen.cpp match their existing baseline
hashes at the recorded upstream revision. No source is imported or provenance
advanced; native reopening and binding algorithms are unchanged.

The archive attachment now binds its existing captures to a named private
resource method, rather than containing the entire request workflow. The method
borrows the same weak backend and format captures by const reference for that
synchronous call. Its body, exception boundary, local object order and early
returns are unchanged. In particular, native reopening still precedes the weak
backend check, allowing retained native readers to reopen after backend teardown.
Direct versus outer callback password fallbacks retain their separate branches.

The device-specific callback and unconfigured-service result remain unchanged;
the existing callable still selects those distinct implementations. No service
record, flags, additional ownership or new lifetime policy is introduced. Current
behavior regressions exercise retained streams, backend release, cancellation,
fallback metadata and updates; no test pins a private method or callback shape.

Linux acceptance passed `archive_opening`, `archive_layer_lifetime`,
`agent_stream_update`, `agent_opening` and `native_folder_opening`: 59 successful
checks and three existing filesystem-ADS skips requiring Windows. Archive ADS,
Close→ReOpen and retained-reader behavior passed. Build took 44.014s and tests
8.602s; existing incremental outputs, four jobs and compiler caches were retained.
Selected scratch was empty before and after the run. No native OS branch changed;
secondary platforms, full/slow gates and visual acceptance were not selected.
