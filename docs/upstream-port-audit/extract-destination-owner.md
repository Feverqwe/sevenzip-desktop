# Extract destination preparation — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Reviewed the complete ExtractDialog
OnOK destination/history branch, initialization, source GUI caller and local
destination consumers. The original ExtractDialog.cpp inventory SHA256 matches
the existing baseline. No external source is imported or provenance advanced.

The local dialog previously assembled the editable destination independently in
its draft accessor and confirmation handler. Both now use one private builder
for the folder/subfolder fields, retaining original UString trimming and native
directory-prefix normalization. Confirmation still normalizes the final path,
updates history under the original selection/limit/deduplication rules and saves
settings. A confirmed destination remains a snapshot; later field edits do not
silently alter it. Current-edit text and a selected history item's text already
agree under the existing editable-combo compatibility check, so no second path
reader is needed for history selection.

The existing public behavior suites cover Unicode/spaces, split destination
on/off, native trimming, edited history selection, bounded/deduplicated history,
Cancel without saving, successful extraction and occupied-output failure. They
also exercise the real typed GUI extraction endpoint. No implementation-shaped
test is added for this behavior-preserving owner extraction. Layout, options,
archive algorithms and extraction-path validation are unchanged.

Validation: extract_dialog, split_destination, dialog_path_rules, extract_prompt,
operation_extraction and extraction_options built and passed first on Linux,
then macOS and Windows. Linux/macOS each reported 89 passes without skips.
Windows also passed native_gui_workflow, for 101 passes and one existing skip:
the POSIX filename-with-colons case is not an ordinary Windows filename.
Existing incremental outputs/compiler caches were reused. Completed selected
scratch was removed after saving results; no full or slow gate was repeated.

The backend follow-up shares the checked destination preparation used by scoped
source extraction and the listed-archive fallback. Validation, directory creation
and validation of the resulting absolute destination now have one backend owner;
the window retains error presentation. Native metadata and the exact bound source
prefix are passed through unchanged. The existing creation-error text is shared
with the backend; its English and Russian presentation remains the same.

Reviewed the complete local preparation paths and their consumers, plus the
original GUI/ExtractGUI destination setup and Common/Extract output-directory
creation. Both complete original units match the recorded baseline at the
reference revision above. The extra Qt preflight remains project integration;
native extraction and runtime output-path checks retain their existing owners.
No source imports, option-policy changes or provenance advancement.

Existing GUI extraction checks now create new nested Unicode destinations for
both selected and all entries. The backend mode checks likewise extract into an
initially absent nested destination. They verify extracted bytes through public
operations, alongside existing overwrite, cancellation, unsafe-output and link
checks; no helper-shaped test was added.

The follow-up passed extraction_options, ui_operations, source_extract_backend,
source_extract_advanced, source_link_extract, source_extract_contract and
native_rar_ui on Linux first, then macOS and Windows: 114/114/117 passes,
no failures. Each POSIX platform skips four Windows ACL/ADS/reparse rows;
Windows skips the ordinary POSIX filename-with-colons row. Existing incremental
builds and compiler caches were retained. Completed suite scratch was removed
after saving results; no full/slow gate or visual change was involved.
