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

## Batch input preparation — 2026-11-02

Reviewed the dialog/caller chain, complete Qt batch extraction/opening adapters,
the native Extract archive loop and its volume accounting, and original
EnumerateDirItemsAndSort/SortFileNames. Complete original Extract.cpp,
EnumDirItems.cpp and SortUtils.cpp inventory hashes match the recorded baseline
at `b88c6bc09339de32f7d18bbab900686b244f42ba` from the same upstream repository.
No external source is imported or provenance advanced.

The Qt batch adapter previously sorted only the full-path lookup list while
leaving the processing list in selection order. Native volume lookup uses the
same index for processing paths, sizes and skipped archives; independently
ordered lists could skip an unrelated archive or reopen an already used volume.
Both lists now derive from one ordering produced by the existing native
SortFileNames, including its platform filename comparison. Native extraction,
volume discovery, progress, cancellation and output-path policy are unchanged.

The existing volume regression now also extracts a split 7z and an independent
ZIP selected between its volumes, checking both payloads and the complete result.
The overwrite/Cancel fixture uses deterministically ordered input directories,
so its expected final bytes follow the native archive order rather than random
temporary-directory names. No new implementation-presence test is added.

Linux focused validation passed: source_extract_backend, source_extract_advanced
and source_link_extract, 37 checks. Four existing Windows ACL/ADS/reparse rows
are skipped on Linux. The batch adapter and direct consumers are checked here;
no new Windows/macOS acceptance is claimed. Build trees/compiler caches remain
incremental, and selected scratch directories are empty after completion.

## Dialog mode identity — 2026-11-03

Reviewed initialization/settings precedence, draft options, live translation,
confirmation and the GUI extraction consumer against original ExtractDialog
initialization/OnOK, public types/resources and ExtractGUI at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Complete ExtractDialog.cpp/.h/.rc
and ExtractGUI.cpp original hashes match the existing baseline. No source import
or provenance advancement; native extraction and settings algorithms remain.

Each Qt mode item now keeps its native value and caption identity together.
Initialization, summary, draft options and confirmation read the selected native
value directly, removing the ordinal-to-value round trip and parallel value/
caption lists. Language changes update captions without changing mode identity.
Unknown initialization still selects the first item; an unmatched later caller
override still leaves the current selection. CurrentPaths remains displayed as
Full while native confirmation retains CurrentPaths under the source rule.

Existing language/draft and settings-force tests now additionally check selected
modes after a live language change and CurrentPaths confirmation/persistence.
They observe captions and resulting options, not item roles or descriptor data.
Source, batch and directory extraction retain their context-specific path and
overwrite policies, including RejectExisting handling. Passwords, metadata,
history, Cancel behavior and the public draft accessor remain unchanged.

Linux `extract_dialog`, `source_extract_advanced`, `extract_prompt` and
`operation_extraction` passed: 64 Qt checks, no failures; two existing native
NTFS ACL/ADS checks require Windows and are skipped. Final incremental build
6.394s, tests 3.473s. Selected scratch is empty, and build trees/compiler caches
are retained. No platform-native behavior, layout or text changed; no secondary
OS, full/slow suite or new visual acceptance is claimed.
