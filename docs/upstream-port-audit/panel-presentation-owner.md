# Panel presentation ownership — 2026-10-31

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete App.cpp/.h,
Panel.cpp/.h and PanelDrag.cpp inventory units, including panel creation,
focus callbacks, panel switching and list settings. Original bytes match the
existing baseline. No source is imported or baseline advanced.

The original application retains independent CPanel objects. Qt likewise now
retains one presentation state per panel, instead of copying controls, paths,
history, mode and scan state into window fields and back. Commands and temporary
callbacks select the existing panel state. Native CApp/CPanel navigation,
selection, command routing and operation algorithms remain unchanged.

The adaptation retains notification and reentrancy guards, captured callback
identity, source focus restoration and the window-destruction guard. Temporary
background projection still restores the foreground status message. Scan and
command availability read both panels' current state directly. Operation-wide
progress, cancellation and input state retain their existing owners.

This is a representation simplification; no new pre-fix product failure is
claimed. Existing public behavior suites cover startup, two-panel navigation,
background publication, selection, sorting, refresh, rename and archive/file
commands. The affected window target compiles on macOS. Earlier broad Linux
attempts were interrupted and do not establish acceptance. After storage repair
and the independently reproduced temporary-object identity fix, five focused
suites passed. The subsequent default Linux gate passed all 504 selected suites;
15 slow suites were excluded. Qt tests reported 7986 passes, no failures and
77 skips. This establishes Linux runtime validation separately from the source
comparison above. The macOS default run completed 501 suites; fixture/runtime
prerequisites and the measured UDF budget were corrected in focused follow-ups.
After the user unlocked the console, context_dialog and fscrypt_key_ui both
passed on native Cocoa against the current build: 31 checks, no failures or
skips. Console readiness was verified before running them sequentially; existing
focus, sheet/modality and encrypted-opening assertions were retained.

The Windows default run completed 520 suites with four failures: native GUI,
system-menu resource accounting, associations and shell e-mail. Their fixture
assumptions were corrected separately; focused follow-ups passed all four plus
direct MAPI, with 48 successful checks and no skips. The
[fixture audit](native-validation-fixtures.md) records scope and evidence.
These focused follow-ups resolve the failures from the initial secondary-platform
runs; they are not a repeated complete gate. Completed password-coverage,
temporary-identity and fixture changes have their own acceptance records and
commits. Source comparison and runtime validation remain separate evidence.

## Returning widget focus after native work — 2026-11-04

Reviewed explicit archive opening, external-update completion and Parent
navigation with Qt operation scopes, modal progress, address/list controls and
the existing presentation focus owner. Original Panel.cpp, PanelFolderChange.cpp,
PanelItemOpen.cpp and App.cpp hashes match the recorded reference above. Native
Parent binding/selection and SetFocusToList retain their original owners; Qt
restoration after disabling widgets is local presentation integration. No source
is imported or provenance advanced.

The three identical immediate-widget guards now belong to QtAppPresentation.
Each caller still captures its widget through QPointer and restores it at the
same point relative to publication and operation-scope release. Closed windows,
absent/hidden/disabled widgets and active modal dialogs still prevent restoration.
The presentation method performs no deferred delivery and retains no widget.
Parent's list branch still uses its existing pending-list request, including
scan/operation/projection guards. Other context-specific focus rules are unchanged.

Existing address/navigation and native external-update regressions observe
editing focus after opening refusal, large-list publication, new address drafts,
Parent navigation and peer-address focus after successful/failed/cancelled native
updates. No test pins the private helper or repeats its predicates. Layout,
visibility, text and geometry are unchanged; no new appearance acceptance is
needed for this common Qt policy extraction.

Linux passed `address_navigation`, `ui_navigation` and the focused
`native_external_update`: 82 checks, no failures or skips. Build took 27.839s
and tests 28.606s, with the existing incremental outputs, four jobs and compiler
caches retained. Completed settings and records for already absent temporary
objects were removed after checking processes. No platform branch changed, so
secondary platforms were not selected; no full/slow matrix or visual check ran.
