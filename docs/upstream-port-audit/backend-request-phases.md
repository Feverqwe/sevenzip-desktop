# Backend request lifecycle — 2026-11-01

The project-owned RunArchiveBackendRequest bridges a source worker to the GUI
backend for file-only extraction and metadata opening/reopening. Reviewed the
complete transport, its password/cancel/completion callbacks and consumers, plus
source CArchiveFolderManager::OpenFolderFile and CAgent::Open/ReOpen in
<https://github.com/ip7z/7zip.git> at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Native reopening, stream copying,
extraction results and archive algorithms retain their existing owners. No
external source import or provenance advancement.

The request's ownership/completion pair encoded three useful phases. An explicit
pending/running/finished state now expresses them directly. Completion, password
delivery and cancellation all retain their running-request guard. The password
request flag is consumed under the same mutex as the request state, removing a
separate atomic synchronization path. Signal ordering, result precedence,
one-shot cancellation, polling and scoped connections are unchanged.

The new public-behavior regression rejects a request while a real CRC operation
is paused, both with and without source cancellation. It verifies that the
rejected request never starts, the unrelated operation remains paused/running
and then completes successfully. Existing source extraction, opening, update,
external-editor and worker-progress suites cover success, failure, cancellation,
password handling and retry. This protects operation ownership rather than
private flags or enum values.

Validation: source_extract_backend, archive_opening, native_update_path,
external_editors_ui and worker_progress built and passed on Linux first, then
macOS and Windows: 59 passes per platform, no failures or skips. The new busy/
cancel rows run through the public worker-to-backend request boundary. Existing
incremental outputs/compiler caches were retained and completed scratch removed
after evidence capture. No full/slow gate or heavy diagnostics were needed.

The metadata follow-up also routes initial opening through this request owner.
The folder manager no longer holds a separate pending request, permanent backend
subscriptions or its own password/cancel wait loop. Initial opening and ReOpen
collect metadata only while their request is running; completion freezes that
snapshot before returning to the source worker. Metadata adapters and resource
services are then prepared by that worker, as already done for ReOpen. Native
reader opening, stream context, limited-read consent, transport format selection
and Agent folder binding retain their existing boundaries. The two complete
original Agent inventory units match their recorded baseline; no borrowed body,
source hash or revision was changed.

A two-row public Agent regression covers a failed listing or throwing start,
successful retry and ReOpen with different Unicode metadata. A later backend
publication must not replace the completed result. The seven focused suites
agent_opening, archive_opening, archive_layer_chain, source_extract_backend,
native_update_path, external_editors_ui and worker_progress passed on Linux
(including the focused Agent follow-up), then macOS and Windows. Each secondary
platform reports 134 passes with no failures or skips. Existing incremental
outputs and compiler caches were reused; no full or slow gate was required.
