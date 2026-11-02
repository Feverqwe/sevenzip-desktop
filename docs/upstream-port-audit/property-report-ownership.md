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

## Independent raw-property count — 2026-11-02

Compared the complete property collector with the same recorded original
`PanelMenu.cpp`, including ordinary, raw, folder and archive-level groups, plus
the raw-property providers and public dialog consumer. The original file still
matches its recorded baseline. Its raw-count query reused the ordinary-property
count; a provider leaving that output untouched could therefore inherit an
unrelated count. The collector now initializes its own raw count to zero.

The initial correction also required S_OK from that query. Follow-up comparison
with original Properties, InitColumns and the existing column regressions found
no basis for that extra gate. The collector again retains source partial
enumeration when a count is written: each descriptor and value must still pass
its own successful query. Available ordinary and folder properties remain in
the report. Raw conversion, group order, archive metadata and the native Windows
property-sheet route are unchanged. No source import or provenance advance is
involved. The inspected built-in raw-count providers return S_OK; a built-in
positive-count failure is not claimed.

A public-report regression checks that an untouched raw count cannot reuse the
ordinary count, while written counts retain available CRC data even with E_FAIL
or S_FALSE from the count query. This follow-up reproduced the initial gate's
loss of available raw fields on Linux before removing it. File name and folder
path remain available in all cases. Linux passed `panel_property`, `properties`,
`source_properties` and `native_archive_properties`. This shared collector change
has no platform-specific branch or layout change; no new macOS/Windows runtime
acceptance or rendered-reference comparison is claimed.
