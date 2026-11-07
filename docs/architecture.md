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

Encrypted DMG has a separate read-only format owner which exposes decrypted
bytes on demand through `IInStream`. Recursive opening reuses the existing
UDIF, partition and filesystem readers. Password retry belongs to the opening
operation; decrypting streams retain no UI callback. The
[DMG source audit](upstream-port-audit/dmg-encryption.md) records provenance,
crypto ownership, supported variants and validation limits.

Apple sparseimage and sparsebundle have a separate container-mapping owner and
reuse encrypted-DMG key setup/CBC streams. Sparseimage maps the decrypted sparse
index; sparsebundle retains a scoped multi-file provider for token and bands.
Both return seekable disk bytes to the same recursive readers and typed backend.
[The sparse source audit](upstream-port-audit/sparse-images.md) records the
different layer order, provider lifetime and damaged-band boundaries.

The native APFS reader offers optional directory access to the source Agent.
Ordinary UI opening requests it, retaining stable native item indexes as folders
are appended. The Agent publishes refreshed directory projections while preserving
existing folder bindings. Directory reads use the joined native progress worker;
Cancel and failures leave already published items usable. Unknown subtree totals
are not presented as complete sizes. Selected-directory extraction expands that
subtree before collecting native extraction indexes. Explicit size calculation
uses the same selected-subtree worker and the existing proxy size aggregation.
Ordinary Copy projects literal selections, without implicitly exporting hidden
streams as ordinary files; Move and archive staging retain their metadata scope.
Flat View and the explicit complete read-only view request recursive enumeration;
SMB mounting requests directory access instead. For APFS, the retained view
opens an isolated filesystem handler over the retained, already decoded input,
using a cursor that serializes individual transport reads through the source
generation. Password/key setup and bounded container caches are reused; the
panel's mutable metadata is not shared with background directory enumeration.
The mount publishes a root and stable entry IDs, then validates and appends a directory on
request. Failed loads remain retryable. The native directory API returns its
direct child indexes, avoiding a scan of all previously visited objects for each
listing. QtRO carries directory results as well as file reads; the SMB process
validates them before publishing nodes. Deferred CREATE/QUERY_DIRECTORY replies
stay on the SMB event loop, including related compound dependencies. Default backend
and console enumeration retain their complete-list contract. The implementation
and source comparison are recorded in the [sparse audit](upstream-port-audit/sparse-images.md).
Nonlazy mount preparation publishes a mutex-protected status snapshot for the GUI timer,
without worker access to widgets or retained GUI callbacks. A selected published
APFS folder resolves only its ancestors before accepting directory requests.
One mount worker serializes its native operations; a large directory can delay
other source reads for that mount even though the SMB event loop remains available.

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

Each live tab owns a stable source panel and its Qt list, selection and navigation
state. `QtWindowWorkspace` projects the active tabs into CApp's two panel slots;
switching tabs keeps the inactive source and its archive-parent chain alive.
Operation guards capture the actual source before work starts. Switching or
moving tabs is unavailable during protected calls, publication and native drag.
External edits capture an immutable native target rather than depending on the
current tab. Publication notifies file windows by affected archive path; a source
view does not own the editor session or authorize writing a different archive.

Back and Forward belong to the live tab, including its selected/current item and
viewport. Parent is a new navigation visit. Only completed successful navigation
advances the cursor or publishes a recent place through the existing
`CFolderHistory`; refresh, tab activation and rejected/canceled requests do not.
Changing direction retains the forward branch until a different visit succeeds.
These live stacks follow tab transfers; they are not persisted in sessions or
copied into a newly duplicated tab. Native address fallback to an existing
ancestor retains its established success semantics.

Recent Places has a shared settings-scoped adapter around `CFolderHistory`.
The native history still owns comparison, order and limits; immediate removal
and clearing update every window's state before notifying views. Recent links
retain value-only native root/archive descriptors for reopening, with no handles,
passwords or consent. Other favorites and destination histories have separate
owners. The dialog uses the shared dialog layout and source identity with path tooltips;
its search and selection remain visible when opening fails. A request captures
the originating tab, waits for its retained publication if needed, and checks
the exact target before accepting it. Closing the form retires only that request.

Numeric bookmarks have their own settings-scoped publication adapter around
`CFastFolders`. Saving merges the selected slot into the current native persisted
state, updates every window snapshot, and then refreshes bookmark menus. Closing
another window therefore preserves the latest slots. Named favorites and recent
places remain separate owners.

Each tab is an independent application context: closing it applies the existing
source shutdown to that tab before releasing its UI or activating a neighbour.
The native saved-location rule is shared with the original single-window app:
an open archive resumes at its containing directory. Persistent workspace state
does not reopen archive chains; older saved archive locations migrate to the
containing directory without reading the archive or requesting its password.
Live navigation history and explicit duplication retain their own archive values.

`QtWorkspaceApplication` owns window discovery and session writes. Each window
has independent mutable backend/operation state while reusing the immutable codec
catalog. Moving a quiescent tab between windows rebinds host callbacks and archive
services before the original window can disappear. File Manager external-editor
sessions own their native process services, launcher, retained copy and update
context without retaining a file window. The originating tab tracks those owners
weakly and stops its observers through the existing shutdown boundary; closing
the tab does not terminate the external editor or later publish its edits.
Their codec factory captures immutable metadata,
without retaining the original backend or a hidden file window. Standalone
source-panel services can still use the joined panel-scoped transport.
Filesystem Copy/Move and Split/Combine tasks keep their value plans, workers,
progress, overwrite questions and results with the originating source context.
Closing that context cancels and joins the worker before releasing its folders.
Completion continuations stop with the owner; complete success clears the
original Cut payload, while skips retain a reviewable partial result. Questions
belong to the source's task window; automatic presentation waits while another
question or text edit owns input. Explicit Show restores a minimized task.
Archive/staging and mount resource owners retain their protected close boundaries.

The independent-operation registry projects live presentation and pending reviews
to the Operations menu. UI entries carry stable IDs; activation resolves their
current owner again, so a stale menu cannot restart finished work. Recent successes
retain only bounded display values and have no event-loop hold or session storage.
They do not replace pending review ownership or the child's own terminal view.

Selected properties use an owned snapshot from the native panel collector.
Typed membership separates selected entries, folder metadata and each archive
level before presentation. The nonmodal information window composes shared UI
controls and holds no reader or panel; its parent file window owns its lifetime.
Generic report rows share storage and language bindings with that snapshot.
Windows filesystem Properties retains the existing shell provider.

External-editor observation keeps the native wait/discovery algorithm. Unknown
completion, including a fast launcher or a no-handle association, retains cleanup
protection. Manual confirmation permits checking a stable copied input; it does
not prove process termination or clear that protection. Native archive publication
uses the existing writer lease and current-reader checks and reports each
inner-to-outer update. Partial results retain recoverable inputs. Exports pin
their source for reading without weakening deletion eligibility. GUI delivery
finishes before the observer is joined and its captures are retired.

The existing owned-temp registry retains nonsecret external-session context:
the archive path, native entry chain and initial temporary-file stamp.
Explicitly opening a retained copy's details creates a fresh context and requests passwords again;
it never launches an editor or publishes automatically. Published-copy metadata
prevents a later restart from offering the same publication again. The temporary
files dialog hands off to session details after closing its own modal surface.
File identities distinguish selected copies, including canonically equivalent
names on filesystems that treat them as separate files.

Workspace archive invalidation follows the physical root across active and
suspended tabs. Reopening uses the existing native archive chain with its accepted
formats and in-memory passwords, preserving selection, scroll and navigation
history. Automatic attempts defer to editing, questions and unstarted forms;
new consent or an unavailable chain leaves the previous readable view with Retry
and Parent actions. A later publication remains pending until its own generation
has been read.

The panel owns invalidation of both its bound source and parked location
checkpoints, including local archive-opening transactions. A superseded chain
cannot publish its private child back into the changed root. Changed copies get
retained context before their owners are released; unmaterialized read-only
streams keep their separate lifetime. Candidate failure restores the old chain
and its restriction on writing. Ordinary navigation retains the native close
workflow for sources that have not been superseded.

Quit preflights every file/resource owner before closing peers. An existing
operation form, question or unreviewed result is shown to the user instead of
closing its owner. Independent 7zG operations retain their native lifecycle and
keep the event loop alive through completion and required review.

Local filesystem and volume workers belong to their source context. Closing
that context cancels and joins its workers before releasing folders; callbacks
cannot publish after that teardown. Their window-modal progress keeps ordinary
close requests out of an active question. Read-only mount managers belong to
the same tab runtime and move with it; disconnect-before-close resumes the
requested tab rather than closing its neighbouring contexts.

Sessions retain directory locations, tab order, view preferences and window
placement according to the native saved-location rule above. A failed directory
reopen keeps the requested location visibly failed. Live history, duplication
and transfer retain typed archive descriptors for the physical archive, native
nested links and final folder. Their navigation uses the native internal opener;
workspace persistence excludes those archive chains, temporary materialization
paths, handles, passwords, running operations and consent.
The application-wide favorites model owns stable identities, labels and ordering;
legacy numeric bookmarks remain compatible. Removing or renaming a favorite
does not mutate the source directory.

Shared UI theme owns the color tokens used by workspace and dialog surfaces.
Dialog surfaces retain native application brushes for selection and controls;
theme changes update existing widgets without replacing active drafts.
Main-window presentation shares workspace metrics, navigation-row
and path components, and a places component with one row delegate for favorites
and native locations. These components project navigation requests; they do not
own file or archive operations. Native locations share an application model of
value snapshots from the native root provider. Each sidebar projects its own
current location independently of keyboard selection. Native root identity
survives a language change without rebinding the open source. On POSIX, Computer
projects the native drives namespace used by the menu command; the physical
filesystem root remains accessible through its volume or explicit `/` path.
The POSIX native root retains Volumes as a logical-namespace alias; Places
projects that identity once. Favorite menu
actions resolve the stable bookmark identity when invoked; renaming starts after
the menu closes. The splitter applies the original ratio policy
to file areas, excluding the shared places width. Status belongs below each file
area. Restoring geometry precedes restoring its split so resizing retains the
user's file-area proportions.
The tab bar owns compact rendering and tab input. Workspace activation retains
strip focus through asynchronous publication only while the same request and
tab remain current; subsequent input takes priority. Shared chrome is shown
after moving it between retained views, rather than inheriting hidden state.
Tab menu commands retain the selected tab identity across reordering and moves.
Transfers of restored tabs report completion after their native context is ready;
opening that context does not replace a newer source-tab choice. A detached window
retires its untouched startup tab only after a successful transfer. Cancellation
and window destruction settle pending restoration so surviving windows can save
their sessions.
View menus retain their source context through popup delivery and resolve it
before dispatch. A removed or no longer bound source cannot redirect a pending
choice to another tab. Checked state and availability use that same context.
Automatic refresh is an application preference shared by open windows and
applied when a tab is bound. List publication retains focus within its pane
and restores the original view only while the tab and input request still match.

The workspace table owns adaptive presentation, descriptive file types and the
current-row outline through the existing model and row delegate. Native column
catalogs, sorting and saved layouts remain authoritative. Initial file views use
Name, Size, Type and Modified; provider-specific metadata remains available from
the header menu. Presentation preferences extend native settings without adding
synthetic columns to the native column order. Type descriptions use Qt's filename
metadata; they do not probe file contents or implement format recognition.
Qt MIME descriptions follow the system's available language metadata; application
translations own the column heading and fallback descriptions.

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
Workspace blank-area selection has one view-owned gesture controller for table
and icon modes. It projects into that shared selection model, owns cancellation
and edge scrolling, and leaves item-content dragging with the native Qt view.
The row delegate supplies content hit geometry; moving a retained view rebinds
its pane activation without retaining the previous window.
The table owns each Qt column's property and source-catalog/visible-column
binding together. Window code applies layout and persists the source panel's
settings through that binding rather than maintaining parallel property maps.

Inline rename has one scoped Qt controller for the editor, deferred delivery and
activation guard. It retains the sending panel/item identity through commit or
cancel and stops pending work before window shutdown. Model repaint and language
refresh are presentation updates; source CPanel still owns name validation,
rename operations, errors and restored selection.

Main-window chrome presents tabs, breadcrumb navigation and a per-pane file action
bar above the list/sidebar. Its proxies query the existing command projection for
the owning pane. The editable address retains the original panel navigation owner.
Breadcrumbs consume typed ancestors from that same owner; editor drafts do not
define the bound location or navigation targets.
Physical archive navigation retains the live panel source until the candidate's
native loading and row publication succeed. A refusal restores that source and
its selection; accepting a candidate closes the old source through the existing
native archive lifecycle. Navigation inside the current container keeps its
native binding route.
`QtFileClipboard` supplies native file URL formats to the existing file-transfer
operation; exporting a container selection uses CPanel's native CopyTo and checks
its extraction result before publishing URLs. It does not implement archive I/O.
Its Cut snapshot is published into the shared file model, including inactive tab
models. Rows retain the native operation path separately from the display path;
Cut rendering consumes that presentation state without querying providers or
resolving symlinks during painting. Table and icon views retain the same selection/current
owner and render Cut independently from selection, marks and the current outline.
Address callbacks require a live source panel and share a control-owned delivery
lifetime, so late signals and queued focus requests stop when either owner ends.

## Dialogs and preferences

Original dialog/page modules own values, validation, prompts and operation
decisions. Qt owns widget construction, native-font geometry, focus and signal
transport. Shared geometry services adapt dialog units and work-area positioning;
the [text specifications](ui-layout-contracts.md) own observable layout contracts.

Shared dialog presentation offers fixed content or a vertically scrollable form
with completion actions outside the scrolling region. Split and Password use the
scrollable form; their native validation and result ownership stay unchanged.
Copy retains its bounded, independently scrolling selection summary.
Password request, extraction and creation forms share a secret edit control for
reveal and editing-position retention. It retains no second secret value and
performs no validation or persistence. Dialogs still own drafts, confirmation
visibility, accepted results and cancellation; operation owners retain secure
password lifetime and retry policy.

About presents application identity separately from upstream attribution and
build/module diagnostics. Its offline license viewer has an independent document
model; it reads complete original text from the installed notices payload.
Packaging indexes the documents actually supplied after deployment and pruning,
before signing. This index does not determine available archive formats. License
errors remain local to the selected document, without a network or source-checkout
fallback. Shared TextReport keeps its ordinary selection-copy behavior; the license
viewer separately offers copying the complete original document.

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

Archive-opening passwords retained by panels, read operations, read-only views
and external-editor sessions use the shared password-storage module. Its immutable
allocations have independent page ownership, best-effort OS memory locking and
Linux dump exclusion; the last owner wipes the allocation before releasing it.
Native callbacks use wiping upstream string/BSTR adapters. Qt controls and native
codecs still need plaintext while working, so these measures do not guarantee
that every transient copy or derived key is locked or absent from a process dump.
The [password-storage audit](upstream-port-audit/archive-passwords.md) records
the source comparison and protection boundaries.

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
The final typed result distinguishes cancellation of started work that still
needs review from dismissal of an unstarted form. Explicitly closing a finished
native result acknowledges it; requesting Stop or closing a running benchmark
does not. The application retains unreviewed cancellation results independently
of file windows, as it does transport failures. Closing a finished report never
sends another cancellation request to its worker.
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
