# Property report row metadata — 2026-10-31

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared the complete FileManager
ListViewDialog bodies/header/resource and PanelMenu property collector, with
their selection, raw-value, archive-level and native Windows shell consumers.
Complete original ListViewDialog.cpp/.h/.rc, PanelMenu.cpp and Panel.h match
their recorded SHA256 baselines.

Qt metadata for property names and archive diagnostics now has one persistent
owner in the dialog. View items retain a stable row identity instead of a second
copy of that metadata. Translation and full-value presentation resolve the
same record, including after earlier rows are deleted. The source property
collection, raw-value conversion, display truncation, complete clipboard/detail
values, selection and native Windows property-sheet route are unchanged.
This only simplifies the Qt binding adapter; no external code is imported,
no baseline advances and no original source branches are omitted.

Existing regressions exercise property types and unsigned ranges, real-engine
CRC/raw values and archive levels, hot language changes, row deletion with
selection retention, full clipboard/detail text and history editing. No new
implementation-presence test or pre-fix product failure is claimed. No visible
control, text format or geometry changes are intended.

Seven common suites passed on Linux and macOS: `properties`,
`source_properties`, `properties_runtime`, `native_archive_properties`,
`archive_error_properties`, `folder_history` and `language_dialog`. The same
seven passed on Windows, alongside the native property-sheet cases. The initial
Windows missing-file fixture searched for its row before folder publication
finished. It now waits for the row and enabled Properties command before
removing the file; a focused `native_properties_window` repeat passed. Product
code remained unchanged after the first Linux acceptance. Existing incremental
trees and compiler caches were retained; no full/slow gate, screenshots or
rendered Windows-reference comparison was run.
