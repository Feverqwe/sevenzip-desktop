# Diagnostic message dialog lifetime — 2026-10-31

## One Qt message-list adapter — 2026-11-05

Compared original MessagesDialog and ProgressDialog2 message insertion, splitting,
numbering, initialization, selection and clipboard handlers, their headers and
resources with the complete Qt list adapter and both consumers. Full original
units at `b88c6bc09339de32f7d18bbab900686b244f42ba` match the existing baseline.
Native message algorithms and clipboard line endings remain unchanged; no
external source or revision is imported.

Standalone diagnostics and operation reports now use their existing CListView
widgets directly, removing CQtProgressListView's forwarding layer and the
standalone dialog's duplicate pointer. Column insertion and the existing insertion-result calculation belong to
the common widget adapter; the other list-dialog consumer still ignores that
result. The progress pointer is bound at the same initialization stage as before.
Widget ownership, registration, geometry, headers and modal results are unchanged.

Line splitting, trailing empty rows, message numbering, successful-insert
accounting, selected/all-row clipboard behavior, incremental appends and bounded
column autosizing retain their existing source flow. This does not change the
progress UI or result/cancel policy. Existing public diagnostics, progress,
worker and list-dialog regressions cover these contracts, including reopening
and normal/compact geometry. No adapter-presence or private-pointer test is added.

Linux progress_dialog, progress_worker, extract_prompt, list_view_dialog and
source_prompt_presentation passed 90 checks without failures or skips. One-off
source comparison confirms that the full progress message/clipboard unit and
standalone splitting/numbering/initialization bodies differ only in list binding.
Inventory and whitespace checks passed. Four-job incremental builds retained
the compiler cache; selected scratch was empty and no active tests or disposable
settings remained. No native platform branch or visible behavior changed;
secondary-platform, full/slow-suite and new rendered-parity acceptance are not
claimed.

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete
MessagesDialog.cpp/.h/.rc, CModalDialog creation/initialization, the PanelDrag
message-vector consumer and Qt list/layout/language adapters. Complete original
MessagesDialog units, Windows/Control/Dialog.cpp and PanelDrag.cpp match their
existing baseline hashes. No source is imported or baseline advanced.

The native modal call initializes its controls for every opening. The Qt port's
one-time initialization flag instead retained the first message set when the
same dialog was opened again. Controls and columns now have their ordinary Qt
widget lifetime, while each Create clears and fills rows from the current
message vector and restores initial Close focus. The original line splitting,
including trailing empty rows, numbering and modal IDOK/IDCANCEL results remain.
Messages still belong to the caller; rendering does not modify its vector.

Qt layout now owns resizing directly. The redundant native-shaped resize
callback, its initialization flag and explicit layout activation are removed.
Language changes update the existing column header without rebuilding rows.
The operation-progress report has a separate owner and is unchanged; this does
not add clipboard or detail-view features to the standalone source dialog.

A public regression reopens one dialog with old messages, a new multiline
Unicode set, then no messages. It detects stale rows and duplicate columns
without inspecting private state. The pre-fix omission was established from
the initialization guard and the native creation path, without a separate
failing run. Existing modal, language, font and normal/compact geometry cases
cover the remaining affected behavior; drag consumers retain their source flow.

Focused validation passed without skips: extract_prompt,
source_prompt_presentation, language_dialog and drag_archive on Linux
(52 checks, 3.39 seconds) and macOS (53 checks, 9.20 seconds); Windows also
passed native_drag_archive (58 checks across five suites, 6.38 seconds).
The reopen regression passed on all three platforms. Current normal/compact
and English/Russian JSON confirms that the list and Close button are visible,
reachable and separated. Base geometry reports use offscreen QPA; Windows
language reports use native Windows QPA. This is source/behavior and bounded
geometry evidence, not rendered-original pixel parity. Incremental builds and
compiler caches were reused; disposable fixtures and superseded reports were
removed after preserving the evidence.
