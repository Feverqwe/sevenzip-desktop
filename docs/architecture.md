# Architecture

## Reading guide

Start with the affected boundary below, then inspect its current source bodies,
callers and dependencies. [Provenance](upstream-port-audit.md) records borrowed
code and adaptations; [testing](testing.md) routes behavior to focused checks.
Detailed dated component investigations remain in the
[archive](archive/qt-port/architecture/component-notes.md).

## Repository boundary

Sevenzip Desktop is an independent application based on 7-Zip and other recorded
dependencies. Its functional owners determine the project source layout.
Borrowed implementations retain their provenance, licenses and reusable native
types without requiring the project to reproduce an upstream directory tree.
[AGENTS.md](../AGENTS.md#source-port-requirements) owns these rules;
the [manifest](upstream-baseline.json) owns 7-Zip revisions and inventory.
The [reader ownership audit](upstream-port-audit/license-variants.md) records the
shared probes/compression dependencies and engine/module boundaries. The core
does not retain parallel SDK filesystem readers.

The application uses C++23 / Qt 6 Widgets and reuses FileManager/Agent/GUI logic.
Qt adapters provide presentation, event delivery and native services.
The 7-Zip-based native engine remains responsible for archive formats and
compression; project filesystem readers are shared by the application and
console through the same external format modules.

## Module directories and source ownership

Project-owned code and adaptations belong to their functional module directories.
A module keeps its interface, implementation, private helpers and platform
variants together. A C++ named module's interface is part of that owner, not a
reason to leave its implementation or adapter files in the project source root.
The same ownership rule applies to native libraries, format plugins and Qt
components. Module-specific build definitions and tests follow the same owner;
shared test helpers have a separate test owner.

Unchanged dependency sources are stored in local archives under
`third_party/<dependency>/`, preserving the used dependency's internal directory
structure inside each archive. Builds expand verified sources into their own
build directories; adapted project sources remain ordinary files in their owning
modules. Unchanged licenses and provenance stay under the corresponding
`third_party/<dependency>/`, including when only adapted sources are used and
there is no source archive for that dependency. Adapted dependency modules keep
their source, interfaces and integration together under `src/dependencies/`.
A project module may consume
several dependencies, and several modules may consume one shared dependency.
Shared project code likewise has one source owner and build target; consumers
link it without duplicating sources. License boundaries, including GPLv2, GPLv3
and unRAR-bearing components, remain explicit in the build.

All 7-Zip inputs needed to build the application, native engine, console and SFX
must be owned by this repository. Ordinary builds, runtime preparation, packaging
and their tests must use these local sources. External upstream checkouts belong
to provenance verification, update review and original-reference comparisons.
Origin paths and hashes are explicit baseline metadata; local paths must not
implicitly determine upstream ownership. If a native build needs an intermediate
tree, it is generated from local owners and does not dictate their source layout.

FileManager, archive integration, format readers, codecs, cryptography, platform
services and presentation now have their own module directories under `src/`.
Dependency facades keep their headers, C++ module interfaces and local native
adapters together. Linux compatibility retains the header namespaces required by
its C interfaces inside its own module, rather than determining the application
tree. Tests follow component owners, with shared fixture code in `tests/support/`
and owner-specific support directories. Component build definitions stay with
their owners; `cmake/components/` composes shared production libraries and
`cmake/tests/` registers scenarios without changing directory scope.

The 7-Zip dependency is archived locally. Application builds expand unchanged
units; generated forwarding headers preserve foreign include spellings while
the compiler reads and tracks the single live project module header. Native
runtime/SFX preparation expands the recorded native source set, rebasing project
include paths into that generated build view. These adapters preserve dependency
contracts without placing an upstream-shaped source tree in the project.
The baselines record original paths, current owners and local dependency archives
explicitly. Modified libsmb2 sources belong to its project module; generated
forwarding units connect them to the dependency's original build rules and keep
compiler dependency tracking live. The
[source-ownership audit](upstream-port-audit/module-source-ownership.md) records
the completed transfer and platform checks, including the interrupted full gate.
[Development](development.md) documents the current commands.

External C libraries keep their original compilation language and provenance.
C++ dependency facades own resource lifetimes and bounded buffer interfaces;
named modules expose those facades to C++ consumers on supporting toolchains.
The zlib facade is connected to the SquashFS stream adapter, and the Brotli
facade to format probing and stream decoding. Their behavior tests exercise
the production consumers. LZ4/lzlib facades connect the existing C frame loops
to bounded native stream services. Build selection and toolchain requirements are
documented in [development](development.md#dependencies).

Archive resource targets carry the typed backend implementation in their link
closure, including consumers that only present options. Shared object libraries
provide the same production implementation to application and test targets.
Consumers link these component targets so their required libraries follow the
implementation through the build graph, including archive resource libraries.
The application and window scenarios link the same production window library,
including its command, transfer and presentation components. Tests add their own
fixtures without copying the implementation or dependency list of another test.
Window shortcuts and mount commands belong to the shared window component.
Archive and compression scenarios link the resource and dialog libraries
directly. Shared test helpers have their own targets; native override fixtures
retain separate compilation where their definitions intentionally differ.
Compression request values share the source parameter record owned by the
compression module. Backend interfaces consume that contract independently of
the dialog/controller declarations used by presentation.

## Window and panel state

Original `CApp`/`CPanel` own command routing, selection, navigation and panel
operation semantics. `MainWindow` projects that state into widgets and connects
the platform services. It must not become a second owner of archive algorithms
or command-availability decisions.

Each panel owns its Qt controls and presentation state. Command dispatch and
temporary background callbacks select that existing state by panel identity;
switching panels does not save and restore a second mutable window copy.

List mode, Flat View and sort preferences stay with the source panel. Qt reads
them when presenting controls, changing columns or returning from another panel's
callback; it does not retain a second mutable copy of those preferences. Archive
command consumers read the bound resource's metadata rather than a window copy.

`QtAppOperationState` extends original timer/notification guards across asynchronous
work. Each window retains its own operation state and panel bindings. Completion
must update the captured owning panel even if focus changes while a modal dialog
or worker is active. Navigation, refresh and publication preserve independent
peer state and reject stale results after a location/provider change.

Window-owned asynchronous progress retains its dialog and transport in one
operation snapshot. Completion retires that snapshot before native final-message
callbacks; workers still join before progress storage is released. Source modal
progress keeps its native owner and lifecycle.

Filesystem enumeration and archive folder bindings feed the list/model projection.
Original item indices, marks and focused row are distinct from display text and
Qt selection. Sorting, column metadata and native namespaces retain their source
owners; presentation does not infer operation paths from rendered cells.

The table and icon views share a Qt table model and selection model. The model
publishes source item identities and row presentation metadata; property text and
comparison remain delegated to the source panel and its folder providers. Model
changes preserve Qt persistent indexes and selection across sorting and column
changes. The model does not retain providers beyond the panel's lifetime.
The table owns each Qt column's property and source-catalog/visible-column
binding together. Window code applies layout and persists the source panel's
settings through that binding rather than maintaining parallel property maps.

Inline rename has one scoped Qt controller for the editor, deferred delivery and
activation guard. It retains the sending panel/item identity through commit or
cancel and stops pending work before window shutdown. Model repaint and language
refresh are presentation updates; source CPanel still owns name validation,
rename operations, errors and restored selection.

Main-window chrome has a scoped Qt presentation adapter for toolbar spacing,
labels and the parent button. It does not style dialogs or own commands; the
editable address retains the original panel address/navigation owner.
Address callbacks require a live source panel and share a control-owned delivery
lifetime, so late signals and queued focus requests stop when either owner ends.

## Dialogs and preferences

Original dialog/page modules own values, validation, prompts and operation
decisions. Qt owns widget construction, native-font geometry, focus and signal
transport. Shared geometry services adapt dialog units and work-area positioning;
the [text specifications](ui-layout-contracts.md) own observable layout contracts.

Contextual forms opt into parent-window modality on macOS through the shared
presentation adapter. Layout construction itself does not make windows modal.
Operation progress blocks its owning process;
source synchronous dialog results and worker/callback ownership remain intact.

Preferences are applied through the source Options/page owners. Cancel must
preserve stored settings and registration state. Native capabilities and privilege
failures must remain visible to the owning operation rather than silently changing
the workflow. Platform menu, association, file picker and application-startup
services stay behind narrow adapters.

Settings presentation organizes those six owners into task pages with adaptive
navigation. Controls read the owners' live draft without another persistence
layer. Format search only filters the unified list; bulk selection covers the
whole list. Checkboxes project the source association states through the original
transition; advanced controls retain its full cycle and supported system scope.

Interface text is owned by Qt Linguist catalogs. The language service discovers
catalogs; module-owned translators support the GUI and the native shell host,
including DLL unload before its host application exits. Upstream resource IDs
remain an adapter boundary to those catalogs. Standard Qt widget catalogs
are merged at build time. Presentation bindings retain source/context for live
retranslation; namespace icons are selected by resource identity. See
[localization](localization.md) for contributor commands.

## Filesystem and volume workers

Original filesystem, copy and volume modules own operation plans and results.
Qt workers provide scheduling, progress and cancellation without changing those
algorithms. Widgets are updated on the GUI thread; worker and callback lifetimes
must outlive any queued completion or modal prompt they can produce.

Filesystem-list presentation has one window module for immediate loading,
worker startup, row delivery and completion. Navigation and path selection
remain separate; enumeration and cancellation stay with their native/worker owners.

Use deterministic private fixtures for partial writes, conflict decisions,
cross-volume behavior and cancellation. Native Trash, devices, links, metadata
and permissions have separate platform boundaries; do not infer their support
from an ordinary local file copy.

## Archive backend and safety

`ArchiveBackend` owns native archive integration, standalone GUI operation launch,
validation, cancellation and results. Widgets invoke operations through this
boundary and do not construct engine command syntax.
Source extract/update callbacks and Agent folder bindings retain the original
functional owners.

Codec discovery loads the official native providers and their version metadata
directly, retaining an immutable deployment image. It does not require a console
executable; usable external format providers can supply a deployment without
the main module, with the original loader's warning preserved.
The snapshot factory returns a C++23 value-or-error result with exclusive image
ownership. The catalog adopts a completed image before loading providers;
failed preparation releases the partial snapshot immediately. Snapshot copying
has its own library target shared by the catalog and focused lifetime checks.
Catalog publication is encapsulated in an atomic owner: worker readers retain
the acquired catalog across replacement, cancellation and backend shutdown.

File navigation acquires native providers before binding an input, retaining
the official reader and its item indexes. Standalone listing and archive testing
run official Open/Extract in a joined Qt worker, with operation-scoped password, pause and
cancel controls. A failed or cancelled listing does not publish a usable
snapshot. Open warnings and item/block errors remain separate diagnostics.
Single-entry extraction uses the same joined worker and an exclusive private
output stream. It preserves literal member selection and
typed partial-data results before the source caller handles final publication.
When no catalog is available, the read worker loads a retained deployment before
opening the archive. Missing providers produce a native failure. Read operations
never start the console. Destination extraction uses original Common/Extract
and ArchiveExtractCallback in the same worker, including overwrite decisions
delivered to the GUI and a final bounded-output policy. Archive writes use the
original native writers and update callbacks; no console transport is retained.

The File Manager's Test, Extract, Add and checksum commands for ordinary filesystem inputs
launch the bundled `7zG`. The child owns native discovery, source GUI dispatch, dialogs and
execution; the manager remains available. A generated Qt Remote Objects interface
over a private, current-user local channel carries literal selections, typed
options and outcomes. Both processes use the same build's interface signature.
The shared C++23 contract has its own build target, independent of filesystem
implementations. Validation returns a value or a typed error; request publication
and completion consume that result before changing operation state. Contract
tests exercise the same generated interface and validation as both processes.
The child copies and validates the complete request before explicitly acknowledging
acceptance. Native process launch/observation, startup deadlines and independent
child lifetime remain application responsibilities.
The process adapter and application observer have separate C++23 library targets;
the adapter exposes launch failure as `std::expected` and keeps native observation
ownership private. Process lifetime tests do not depend on archive/filesystem code.
Independent GUI jobs and external editors share one POSIX child-lifetime owner.
It preserves later child reaping when an observer is released while work continues;
each client retains its launch, identity and exit-result policy.
Completion waits briefly for
the observer to acknowledge the result before the child exits. Before extraction
starts, the child reports the actual output paths after any source dialog edits.
The observer retains that metadata through partial results or a crash. Native
process observation
distinguishes a source result from a crash or missing result. An accepted job
continues after the manager closes or the channel disconnects. Shell arguments
still enter the original parser. Toolbar and context-menu consumers share this
boundary. Completion refreshes currently affected filesystem panels after their
source guards release, preserving navigation made while the child was running.
The window owns one pending output-refresh queue for both panels. Overlapping
child results share affected paths; each panel consumes its batch when source
guards permit it, using its current provider. Shutdown discards pending refreshes
without stopping independent children.
Creation uses the original UpdateGUI and Common/Update workers, including volumes,
SFX and optional input deletion. A shared per-user write lease prevents competing
native writers. Its separate C++23 library returns either an acquired, movable
ownership token or an error; failed acquisition releases partial ownership.
Completed single-file outputs are published atomically; a
cross-filesystem move stages its cancellable copy beside the destination first.
Readers may retain the previous generation, but a stale file-backed reader must
refresh before editing. Completion reopens matching ordinary archive panels.
Creating an archive by dropping files also launches `7zG`. When the source requires
completion before releasing temporary files, the drag call waits for the child
while preventing manager teardown and further user input. Other drops return
without waiting. Updating an open archive folder or a nested parent chain retains
its direct source workflow. Raw-device namespace commands
also retain that owner. File Manager checksums preserve operated relative paths,
prefixes and flat-mode recursion through the original PanelCrc worker; shell
checksums use HashCalcGUI. File-backed Windows ADS retain their source prefix
handling in the child. Registered native hashers remain usable without an archive
DLL. Digest/results windows belong to the child, while typed completion preserves
cancellation and partial-input warnings. Benchmark also runs in its own `7zG`,
with the source normal/total modes and worker lifetime. Closing the manager leaves
it running; typed completion distinguishes a finished benchmark, cancellation and
engine/startup errors.

Seekable archive items can back nested readers directly. Each reader generation
retains its codec image, input and parents; independent cursors serialize access
to shared native handlers. Opening, stream acquisition and extraction bind the
current operation's pause/cancel request while they can read ancestors. Refresh
creates a new generation without closing one still used by a descendant.

Interactive opening keeps the original stream-first/CopyTo choice. A handler
that supplies no seekable stream uses one source extraction into bounded memory
or a temporary file, retaining the existing spill and edit-publication lifetime.
Direct streams and device views are read-only; materialized nested archives keep
their supported edit and parent-update workflow. Sequential Extract is never
exposed as seek through replay.

Read-only mounts use an `ArchiveBackend` snapshot retaining the same native
reader generation and resource leases. The mount owner is independent of panel
navigation. Reader work stays in the application's worker; the separate SMB
process receives immutable metadata and bounded range replies over private IPC.
The read-only view owns namespace projection and range validation; its private
cache unit owns stream acquisition, consented materialization and retained results.
The independent OS-client broker owns native mount cleanup. Extraction and use
of unverified output have separate, default-deny consent boundaries. See the
[source audit](upstream-port-audit/readonly-smb.md) for provenance and validation.

Treat archive names and metadata as untrusted. Validate output destinations and
member paths at the operation boundary, preserve literal Unicode arguments and
avoid shell interpretation. Transactional update and temporary-resource owners
retain original archives and leases until the operation completes. Extraction
has different partial-output semantics, described in [parity](7zfm-parity.md).

The shared archive input layer has a C++23 library target and publishes a stream
owner through `std::expected` after size/context initialization. Folder opening,
device readers and read-only views use the same layer and preserve native error
codes, cancellation and independent cursor ownership. Partial native archive
opening remains a separate contract with its original `S_FALSE` semantics.

Nested archives and external editing require explicit resource lifetimes across
open processes, pending writes and parent-chain updates. Module images, streams,
temporary files and folder providers cannot be released while consumers retain
them. Failure and cancellation must leave a reusable application state and
preserve retained edit data where the operation requires it.
The interarchive destination backup is a separate C++23 resource library.
The backend returns its exclusive owner through `std::expected`; the File Manager
keeps it through Move and rollback. Restoration retains the native copy stream
and destination-side atomic publication, after the caller closes the reader.

Temporary-object identity, registration and directory ownership form a C++23
object library composed into the shared native library. Native temporary creation
and Qt consumers use the same registry and lease implementation. Registry reads
return an optional validated record; callers acquire a lease before cleanup.
Object identity uses metadata handles where available, independently of permission
to read contents. Existing records retain their complete prior identity checks
and lease exclusion.
Directory ownership handoff returns a shared owner rather than a reference to
the directory's ownership field.

## Diagnostics

Original Progress/Messages and source error paths own user-facing operation
diagnostics. Process/worker adapters deliver progress, pause, cancel and completion
to those owners. Avoid logging passwords or user archive contents.

Widget JSON and geometry producers support targeted UI review. Native platform,
physical input, OS panels and rendered reference checks have distinct scopes;
see [runtime comparison](ui-runtime-comparison.md). Dated validation belongs in
[source audit](upstream-port-audit.md), rather than this architecture guide.
