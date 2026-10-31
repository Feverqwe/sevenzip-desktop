# Diagnostic message dialog lifetime — 2026-10-31

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
