# Backend request lifecycle — 2026-11-01

The project-owned RunArchiveBackendRequest bridges a source worker to the GUI
backend for file-only extraction and metadata reopening. Reviewed the complete
transport, its password/cancel/completion callbacks and both consumers, plus
source CAgent::ReOpen in <https://github.com/ip7z/7zip.git> at
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
