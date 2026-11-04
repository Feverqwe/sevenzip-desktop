# Dialog layout review — 2026-10-17

## Memory-request presentation owner — 2026-11-04

Compared original MemDialog.cpp/.h/.rc initialization, action/limit controls,
spin behavior and Continue handling at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>, current RAM service, callback decision and
dialog consumers. Complete original dialog/resource hashes match the baseline;
no external source or revision is imported.

The complete Qt control construction now lives in
`mem/qtmemoryrequestpresentation.cpp`, under the same panel component and dialog
instance. The three construction bodies are unchanged. Source initialization,
RAM admission/default choice, spin and input parsing, error handling,
Allow/Skip/Remember/Save and Cancel retain their existing owner and behavior.
The extraction makes the resourceGui dependency for the limit caption explicit.

No new controller, state, memory limit, callback policy or platform adapter is
introduced. Grouping, sizing, focus, binding and connection order are unchanged.
Existing prompt and language regressions cover decisions, invalid input,
spinner keyboard/buttons, normal/compact geometry and language updates; no
source-shape test or new appearance acceptance is added.

Linux `extract_prompt`, `source_prompt_presentation` and `language_dialog`
passed 45 checks without failures or skips. Initial compilation identified the
missing resourceGui include after extraction; it was corrected before runtime
checks. Native platform adapters are unchanged, so no new Windows/macOS,
full/slow or rendered visual acceptance is claimed.

## Review gallery: compact forms and useful content — 2026-10-18

Reviewed native LinkDialog.rc/OnInit/OnSize, MemDialog.rc/OnInit and spin
handlers, CopyDialog.rc/OnInit, BenchmarkDialog.rc/OnSize and log consumers,
and CompressOptionsDialog.rc with COptionsDialog::SetPrec/OnLanguageChange
against their existing Qt control adapters. Baseline/repository below apply.
The changes stay at these presentation boundaries; source calculations,
path/link actions, timestamp policy, memory-limit validation and result mapping
remain authoritative. No source algorithm or error branch is omitted.

At the user's request, Link uses visible-content sizing and native minimums,
does not reserve an empty current-target row, and fits all choices initially;
the capture producer returns to the top after reachability checks. Memory
prompts size to their message/actions, align the message to the top and keep
both spin arrows within the edit height. Copy uses explicit source margins and
input-aligned native rectangles. Normal Benchmark allocates empty-log space
to metrics, protects their minimum width and wraps system information in the
available area, leaving a gutter beside native overlay scrollbars. A populated
log still appears; source-total output and metric calculations are unchanged.

Compression Options identifies the archive format using the existing localized
format caption, removes mnemonic markers from that display-only label, uses
native footer hints, and avoids reserving heights for hidden time controls.
Review producers retain width/compact stress checks but capture content-sized
Options frames. The common-controls fixture now supplies real 7z timestamp
capabilities instead of presenting an empty synthetic format. Two duplicate
English-literal Copy assertions were removed from the field/commit regression;
localized captions remain covered by language and standard-control suites.

macOS arm64 / Qt 6.12: focused geometry, modal/keyboard, memory, path, timestamp
and language consumers pass, including maintained Overwrite/Mem RC comparisons.
The overwrite contract records the requested visible-but-disabled adaptation.
Native Cocoa light/dark groups were refreshed and affected screenshots reviewed.
Windows/Linux and rendered Windows parity were not rerun for this correction.

## Review gallery: action availability and browser icons — 2026-10-18

Follow-up: the same parent-folder icon also replaces `<--` in the temporary-file
browser. `BrowseDialog2.rc`, parent-button dispatch and `OpenParentFolder` were
rechecked; navigation boundaries, localized accessible name and focus remain.
The temp-cleanup consumer and Cocoa light/dark review captures cover the change.

Compared `OverwriteDialog::OnInit` and its callers/answer mapping with the
native extra-button visibility branch, plus `BrowseDialog.rc` and parent/create
handlers. At the user's request, unavailable batch overwrite actions remain
visible but disabled; the answer mapping and caller capability decision remain
source-owned. Reinitialization restores availability. Browse replaces native
resource text glyphs with Qt parent-folder/new-folder icons, retaining localized
accessible names and the original navigation/create handlers. These are explicit
Qt presentation differences; no source workflow branches are omitted.
Focused macOS suites `overwrite_layout`, `source_browse_dialog` and
`panel_deleted_draw` pass. Light/dark Cocoa captures for these groups are updated,
including the previously stale icon-caption color gallery. Other platforms and
rendered Windows parity remain unverified.

## Extract folder-name toggle — 2026-10-18

Screenshot review compared desktop `ExtractDialog.rc` and the complete
`CExtractDialog::OnInit`, `OnButtonClicked` and `OnOK` with the current Qt
constructor, initial state, destination preview, confirmation and persistence
consumers. Source baseline/repository are recorded below. Native Windows hides
the name edit in a fixed resource rectangle. Qt's equivalent visibility change
collapsed that row and shifted the modes below it.

The requested presentation correction keeps the name edit visible but disabled
when SplitDest is off. Toggling retains pending text and control geometry;
the source checkbox still controls whether the name contributes to the output
path. Path splitting, normalization, history, persistence, cancellation and
extraction algorithms are unchanged. This is an intentional Qt presentation
difference from native Windows visibility, with no omitted functional branches.

Normal/compact geometry now checks repeated toggles, unchanged rectangles,
pending text and visible/enabled state. Existing split-destination and path-rule
tests cover output paths and persistence. Focused macOS/Qt 6.12 suites:
`ui_geometry`, `split_destination`, `extract_dialog`, `dialog_path_rules`.
Native Cocoa light/dark screenshots are refreshed in the review gallery.
Windows/Linux and rendered original 7zFM comparison remain unverified.

Source reference: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`. Working tree based on
`b9d13fbca9f43fe3b9971e0a0ef6545b8fb99ec7`, with the preceding
[compression Options correction](compress-options-layout.md).

Reviewed hard-wrapping/checkable-control candidates in the existing Qt
presentation and historical widget JSON, using the existing UI contracts and
port audit for scope. Compared MenuPage.rc/MenuPage2.rc and complete
MenuPage.cpp initialization, language, Apply and event handlers with the port,
MenuPage.h and OptionsDialog's page consumer. SettingsPage2.rc/SettingsPage,
FoldersPage2.rc/FoldersPage and LinkDialog.rc/Qt link controls were also inspected
for similar spacing/wrapping risks. Existing source ownership is unchanged.

Confirmed defect: MenuPage's Zone.Id caption inserted a line break at a fixed
pixel width and left unused space before the combo. The Qt label now uses that
space and wraps only when necessary. No original functional bodies, registry
logic, control order or capability branches were copied anew, replaced or
omitted. Dependencies remain Qt Widgets and existing language services.
The EN/RU normal/compact regression uses a native-sized font and checks actual
caption fit and separation; it failed before the fix on both Cocoa and offscreen.

The Add default-size producer initially captured JSON before nested native
layouts settled. Its screenshot showed the complete RAM text. Processing pending
layout events before that report removes the stale measurement; no Add product
layout or memory calculation changes were needed.

Validation scope: macOS arm64 / Qt 6.12.0. Seventeen existing focused suites
passed before the caption change. Native producers cover Add/Extract, Copy/Move,
name/folder entry, Split/Combine, Password, Overwrite, memory prompt, messages,
details, properties/history, Link, Progress, normal/total Benchmark, About/Help,
and Options pages in EN/RU normal/compact states where the producer supports
them. Scrollable forms are inspected at the relevant scroll positions; hidden
or unsupported controls are not treated as visible clipping. Fixed multiline
memory/file-information text is intentional. This is the declared producer
scope, not every possible dialog state or OS panel.

Six maintained RC/Qt comparisons passed: Copy, Password, Overwrite, Mem,
Progress and System. Recorded adaptations (native footer order/captions,
scrolling summary and Qt control replacements) were reviewed, not suppressed.
Progress's process-priority fixture required an unsandboxed retry; that retry
passed. The original sandbox failure was environmental, not a product fix.
The final affected consumer checks are Menu page, Options, Options keyboard,
language UI, System page and UI geometry. Native evidence is retained under
`build/dev/dialog-layout-review/`, separate from offscreen fixture reports.

Final results: 5/5 affected Options suites PASS (1.87s), UI geometry PASS (0.39s),
16 native producer batches PASS. All 243 retained Cocoa reports were inspected:
no clipping outside the declared scroll-content boundaries, fitting visible
labels, and no checkable widgets stretched beyond their natural line height.
Affected Options and Add screenshots were reviewed against the JSON; the RAM
measurement now agrees with the rendered text. Incremental release app build,
`python3 scripts/package.py --install-only dist` and strict deep signature
verification passed; the local `dist/Sevenzip.app` includes the correction.

No full or slow suite is required for the local label/producer change.
Windows/Linux runtime and fresh rendered original 7zFM comparison remain
unverified; resource contracts do not establish pixel equality. Source review
completeness applies to the changed Menu presentation boundary, not a renewed
whole-application source-port audit.
