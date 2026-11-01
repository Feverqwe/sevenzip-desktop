# Native validation fixtures — 2026-11-01

The panel-state default runs exposed fixture assumptions independent of the
representation refactor. These corrections retain the public operation paths
and existing scenario coverage; they change no production archive algorithms.

Compared the shell compression dispatch, menu invocation/selection, native menu
ownership and GUI benchmark callers with complete corresponding bodies in
Explorer/ContextMenu.cpp, FileManager/PanelMenu.cpp and GUI/GUI.cpp from
<https://github.com/ip7z/7zip.git> at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Existing baseline mappings remain
authoritative. No external code is imported or provenance revision advanced.

Shell compression starts an independent operation without waiting, as upstream
does. The e-mail regression now launches the existing private MAPI host through
the real typed operation endpoint, waits for the final result and drives dialogs
in that host. The host verifies the exact private provider before entering GUI
code, so it cannot fall back to a system mail client. The regression checks the
copied attachment's extracted bytes, input preservation, working-directory
restoration, cancellation and missing exports. Selection clears on successful
launch, including when the independent dialog is subsequently cancelled.
Unfinished fixture children have scoped failure cleanup.

The native extraction fixture uses the disclosure's press action and expanded
state, making repeated polling idempotent, while password enabling retains its
toggle action. Benchmark close expects the existing typed cancellation result;
the port intentionally reports cancellation separately from upstream's generic
S_OK dialog return. Native association lifetime tests disable the separate
unknown-handler question, whose behavior has its own regressions. They continue
to launch the real private executable and check cleanup after process completion.
The rejected-menu regression permits reclamation of pre-existing shell caches
but makes enough rejected calls to detect even one leaked menu per call.

macOS private D-Bus fixtures use a short private socket directory, including when
the project TMPDIR is long or contains address metacharacters. The existing
two-bus isolation regression now runs there too. The UDF suite's aggregate
timeout accommodates a measured complete 289-case run of 51.7 seconds; individual
operation deadlines and cases are unchanged.

Validation: launcher/taskbar fixtures passed on Linux and macOS (9 checks each).
UDF passed on Linux and macOS (289 each); Windows had already passed all 289
under the stricter previous aggregate deadline. Windows focused follow-ups passed
native system menu and association lifetime (6 each), native GUI (13), shell
e-mail (16) and direct MAPI (7), without failures or skips. Linux/macOS do not
build these Windows-only native fixtures. These results do not establish the
remaining focus-dependent macOS scenarios: the console was still locked.
