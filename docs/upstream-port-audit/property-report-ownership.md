# Property report row metadata — 2026-10-31

## Complete filesystem property provider — 2026-11-04

Compared original FSFolder.cpp/.h property readers, comparison, directory
statistics, comments, raw properties and icon lookup, their callers and current
enumeration/cache lifecycle at recorded revision
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Complete original inventory hashes match
the existing baseline. No source is imported or original revision advanced.

FSFolderProperties.cpp now owns that complete provider scenario, including
comment persistence and its existing SetProperty operation. Enumeration,
navigation, change notifications and destructive file operations remain in
FSFolder.cpp; transfer remains in FSFolderCopy.cpp. All implementations stay
in sevenzip_filesystem with the same interfaces and compilation options.

Moved bodies are unchanged, including Windows SDK declarations, lazy metadata
queries, compressed-size fallback, native comparison and cached name keys,
directory-stat cancellation and the existing comment-save return policy.
POSIX stat/time and no-follow adaptations, native Windows attributes/reparse
records and icon boundaries remain distinct. No cache, controller, property
table or filesystem algorithm is duplicated. The baseline records extraction
against the original complete unit and retains all previous mappings/hashes.
Existing behavioral coverage is used; no source-layout test is added.

Focused Linux validation passed fs_folder, panel_filesystem_sort,
filesystem_comment, filesystem_link, panel_item_icons and properties:
123 Qt checks, no failures, two expected native Windows icon skips. The checks
cover aggregate sizes/cancellation, metadata and compressed-size caches,
comparison/selection, comment read/write/reload/failure and actual property/icon
consumers. Windows then passed fs_folder and alt_streams_folder: 22 checks,
no failures or skips, validating native metadata queries and the direct ADS
consumer of MyGetCompressedFileSizeW after moving its definition. The function
retains its original external namespace linkage. No macOS, full/slow or new
appearance acceptance is claimed. Completed settings were removed and selected
scratch inspected; incremental builds, compiler caches and unrelated active
branch checks were preserved.

## Shared size presentation owner — 2026-11-04

Rechecked original OverwriteDialog size presentation and PanelListNotify numeric
formatting/property classification with current overwrite, panel cells/status,
property reports, App selection summaries, HashGUI/ExtractGUI reports and temporary
browser consumers. Complete original units, plus FormatUtils.cpp/.h, App.cpp,
HashGUI.cpp/.h and BrowseDialog.cpp/BrowseDialog2.cpp, match their existing
recorded hashes at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f` from
<https://github.com/ip7z/7zip.git>. No external code is imported or original
provenance advanced.

The existing shared formatting module now owns AddSizeValue, both
ConvertSizeToString forms, IsSizeProp and Browse_ConvertSizeToString. Their callers
use its declaration instead of manual forward declarations or declarations in the
panel/hash GUI interfaces. The moved native bodies and integer macro remain
unchanged; the SPACE macros stay with name presentation in PanelListNotify. All
definitions remain in their existing linked target, without a new formatter or
state owner.
The browser's distinct shortening policy and returned end pointer are preserved;
temporary-file presentation still appends its interrupted-result marker there.

The private App helper is renamed AddExactSizeValue to distinguish its original
exact-byte-only summary from the global exact-plus-binary-unit formatter. Its
body and callers are otherwise unchanged; these display contracts are not merged.
Unsigned grouping, thresholds, localized byte counts, property classification,
result/error/cancellation handling and visible layout/text are preserved.
The manifest maps the extracted symbols to their original inventory units.

Existing public formatting and component regressions cover boundary values and
their report/dialog consumers; no test of source placement is added.

Linux passed overwrite_layout, hash_results, panel_copy_info, panel_property,
panel_cell_text, temp_cleanup, operation_extraction and source_browse_dialog:
132 Qt checks without skips. This validates shared formatting and its direct
consumers, with no new Windows/macOS, full/slow or appearance acceptance. Completed
settings were removed; incremental outputs and compiler caches retained.

## Complete property command owner — 2026-11-04

Rechecked complete original PanelMenu.cpp and ListViewDialog.cpp/.h/.rc
inventory at `9128b80e3a471108678e2b3ec3c985861c8d7b0f`; their original
hashes match the baseline. Reviewed the current property command, typed/raw
and aggregate collection, folder/nested-archive levels, diagnostics, dialog
binding, full-value copy/detail and native Windows shell route. No external
code is imported and original provenance is unchanged.

The complete property command and its existing formatting/section helpers now
live in PanelProperties.cpp under the same panel module and target. PanelMenu
retains menu dispatch and read-only update gates. The extraction mapping records
the new owner against the existing original unit; no header/API, worker or
alternative collector is introduced.

All moved tokens, comments and literals are preserved. Partial raw enumeration,
empty/failed property handling, group order, unsigned sizes, nested levels,
diagnostic translation and modal/shell choice keep their current behavior.
Remaining menu and read-only gate bodies are unchanged. Existing report,
runtime, native archive and menu consumers provide behavioral validation; no
source-layout or private-helper regression is added.

Focused Linux panel-property, list-dialog, window-property, native archive-level
and context-menu checks passed. Normal/compact behavior remains covered by the
existing suites. This source separation changes no native service, visible text
or layout; no new Windows/macOS runtime, full/slow or appearance acceptance is
claimed.

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

## Typed report translation inputs — 2026-11-03

Rechecked the source dialog bodies, header/resource, property collection groups
and their current language, detail, clipboard and editable-history consumers.
Complete original ListViewDialog.cpp/.h/.rc and PanelMenu.cpp hashes match the
existing b88c baseline. No collector/native property-sheet branch is changed,
and no external source is imported or baseline advanced.

The dialog retains source property-name fallbacks and archive diagnostics as
owned UString values. Missing versus supplied-empty fallback stays explicit;
diagnostic text and numeric flags have distinct typed alternatives. This removes
QString round trips, dynamic QVariant type inspection and a separate fallback
presence flag. Translation still takes an owning row snapshot before updating
widgets; stable row identities survive deletion. Complete Strings/Values remain
the detail/clipboard source, independently of display truncation.

Existing behavior checks cover custom names, localized flags and diagnostics,
row deletion, retained selection, full values, clipboard/details and history
confirmation. No type-presence or representation-only test is added. Source
collection, raw partial enumeration, layout and visible text policy are unchanged.

One observable correction follows from retaining the original string: an unknown
archive diagnostic containing embedded NUL no longer loses its tail on language
change. The prior QString-to-UString reconstruction used a terminated pointer.
The existing language-change scenario now checks the complete displayed and
stored diagnostic before/after switching languages and deleting an earlier row.
