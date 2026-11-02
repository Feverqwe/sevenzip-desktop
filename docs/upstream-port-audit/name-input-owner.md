# Shared name input ownership — 2026-10-31

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete ComboDialog
bodies, header and resource, CreateFolder/CreateFile/ChangeComment, SelectSpec,
and the browser's create-directory helper with their callers and Qt language
binding. Complete original ComboDialog.cpp/.h/.rc, PanelOperations.cpp,
PanelSelect.cpp and BrowseDialog.cpp match their existing baseline hashes.

The original accepted `UString Value` and OnOK transition remain authoritative.
Cancel leaves that value unchanged. Creation path validation, operations, error
handling, selection restoration, mask matching and comments retain their source
owners and algorithms. No external code is imported or baseline advanced.

Qt initialization now passes captions and ordered choices directly to the
controls. The temporary copies of those presentation inputs are removed,
including their QString/UString round trips. Language binding captures its
fallbacks from the already initialized controls; caller-built comment titles
still omit a title resource ID. The unused draft-value getter is removed after
reviewing direct and alias consumers; accepted source callers use `Value`.
Layout, modality, focus, raw input and initial-edit restoration after populating
choices are retained.

Existing regressions cover ordered choices without replacing the initial edit,
raw and empty confirmation, cancel, create-file/create-folder success and
failure, masks, comments, browser creation, modal ownership and language changes
with pending input. They also inspect normal/compact geometry. No new test that
merely asserts field removal is added; no pre-fix product failure is claimed.

Seven focused suites passed on Linux and macOS (133 checks each) and in the
interactive Windows session (135 checks), without skips: combo dialog, modal
context, source browser, language dialog, source selection, source creation and
filesystem comments. Current normal/compact widget JSON was inspected for
prompt/field/footer order, fitting and reachability, including long prompts and
English/Russian comment/file actions. Base geometry reports use offscreen QPA;
the Windows language/action reports use native Windows QPA. These establish
the checked layout relations and behavior, not rendered-original pixel parity.

## Qt text boundary — 2026-11-02

Rechecked the complete original ComboDialog implementation, header and resource
at recorded 7-Zip revision `b88c6bc09339de32f7d18bbab900686b244f42ba`,
CreateFile/CreateFolder and the Qt consumers for masks, comments and browser
directory creation. Native text controls supply terminated strings; Qt can carry
an embedded NUL that the existing conversion silently truncates. The common
dialog now rejects that input before conversion, displays the existing native
invalid-argument diagnostic and keeps the draft open for correction or Cancel.
Accepted values retain their whitespace, Unicode, masks and empty-value rules.
Operation-specific path validation, filesystem/archive operations, selection
and refresh ordering remain in their existing source owners. No external source
is imported and no baseline revision is advanced.

Focused regressions exercise correction and Cancel without changing the accepted
value, and real file/folder creation after correction without creating the
truncated name. Existing direct-consumer suites cover valid masks, comments,
browser creation, collisions and panel selection. Validation for this change
is recorded separately from the historical multi-platform layout evidence above.

Linux focused validation passed: `combo_dialog`, `source_create`,
`source_selection`, `filesystem_comment` and `browse_dialog` — 99 Qt checks,
without skips. This validates the shared Qt input boundary and its direct
consumers; no new Windows/macOS or rendered-original appearance acceptance is
claimed. No native platform service or layout implementation changed.
