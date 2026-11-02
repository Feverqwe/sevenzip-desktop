# Callback password dialog boundary — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared open/extract/update password
bodies and callers, fscrypt key adaptation, progress creation/readiness and
worker join, plus the common callback-dialog transport. Complete original
OpenCallback.cpp, ExtractCallback.cpp and UpdateCallbackGUI2.cpp hashes match
the existing baseline. The reference checkout's progress unit differs from the
recorded baseline, so ProgressDialog2.cpp/.h readiness was compared against their
pinned original bytes in the repository source archive instead. No source import
or baseline advancement.

Password and filesystem-key prompts repeated owner-thread dispatch while their
callers separately waited for progress readiness. Both now use the existing
RunCallbackDialog boundary, which already combines worker-side readiness and
blocking GUI dispatch for extraction questions. Redundant waits leave the four
prompt call sites. Widgets are constructed/executed/destroyed on their GUI thread;
the operation retains its existing worker join before progress-parent destruction.
The GUI-thread direct path is retained. No queued ownership or mutable acceptance
flag is needed in either prompt adapter.

The source handlers still own password reuse, PasswordWasAsked/IsDefined,
accepted-only visibility persistence and BSTR output. Prompt adapters publish
password, visibility or filesystem keys only after acceptance; cancellation maps
to the same E_ABORT/false result. Empty and raw Unicode passwords remain valid.
No validation, encryption, key storage, progress layout or cancellation policy
changes. Existing worker/callback/window tests protect the integrated scenario;
no source-shape test or redundant dialog-only regression was added.

Validation: open_callback, update_callback, extract_prompt, password_window and
progress_worker passed on Linux first, then macOS and Windows. Linux/Windows
also passed fscrypt_key_ui: 83 passes each. macOS initially reported 62 common
passes, then passed all 21 native fscrypt_key_ui checks after the user unlocked
the console. All six selected suites are now checked on all three platforms,
without failures or skips. Existing tests cover
worker readiness, accepted empty/raw Unicode passwords, cancellation, reuse,
accepted-only visibility persistence and operation teardown. Incremental builds
and caches retained, completed scratch removed after evidence capture. No
full/slow gate or heavy diagnostics.

## Questions queued before cancellation — 2026-11-02

Rechecked open/fscrypt/password, extraction overwrite and memory callbacks with
progress readiness, confirmed stop, blocking GUI dispatch and joined teardown.
Original password/overwrite bodies and the complete RequestMemoryUse policy were
compared with the recorded source scope above. No external code is imported or
baseline advanced.

The common question wrapper now checks confirmed stop before requesting GUI
readiness and again when the queued GUI call is delivered. Password, filesystem
key and overwrite questions therefore cannot start after cancellation. Cached
credentials retain their source fast path. The checks read the stopped state;
they do not wait for pause on the GUI thread. Existing dialog acceptance, output
publication and parent/worker lifetimes remain unchanged. This closes a Qt
presentation gap; it does not dismiss a question already being displayed.

Neutral GUI dispatch remains available for memory policy. Automatic allowance,
remembered decisions and diagnostic reports keep their original branches and
outputs, including after stop. Only an actual interactive memory question checks
stop before opening and returns E_ABORT with the source stop answer. The memory
dialog object remains on the GUI thread even when used only to format a report.

Public opening-callback regressions cover already-stopped and queued requests.
An event filter applies stop at GUI delivery and lets the blocking invocation
complete; the fallback responder makes an unexpected late prompt observable
through its result, password output and visibility-setting side effects. Memory
callback cases check suppressed interactive prompting and retained report
diagnostics/outputs. Existing coverage also retains automatic allowance while
stopped, normal password acceptance/reuse and cancellation confirmation.

Focused Linux validation passed open_callback, update_callback, extract_prompt,
password_window, fscrypt_key_ui and progress_cancel: 93 checks without skips.
Only portable callback presentation changed, so no secondary OS was selected.
Whitespace and source-inventory checks passed; completed temporary settings were
removed. Configured four-job builds reused incremental outputs and compiler caches.
No full/slow gate, new rendered appearance or decoder-report runtime claim follows.

## Shared overwrite answer translation — 2026-11-02

Reviewed overwrite button creation/closure, native AskOverwrite and its AskWrite
consumer, and the backend/filesystem-transfer dialog consumers against original
ExtractCallback.cpp::AskOverwrite. The callback now reuses the existing Qt
button-to-answer mapper instead of maintaining its own five-answer switch.
It still distinguishes the original Cancel result (answer plus E_ABORT) from an
unknown raw button code (E_FAIL without changing the output answer). The latter
error branch was source-checked rather than exercised through a fabricated UI
button. Metadata, question cancellation, Skip/All/Rename policy and native
extraction/publication remain unchanged. No new interface, imported code or
provenance change is introduced.

Existing dialog, native batch extraction and archive-move regressions check the
integrated path through actual button responses and resulting files. No mapping
constant or source-shape test is added for this boundary consolidation.

Linux extract_prompt, source_extract_backend and archive_move_completion passed
45 checks without skips. Only portable answer translation changed; no native OS
branch or layout changed, so secondary OS checks were not selected. Whitespace
and source-inventory checks passed. Configured four-job builds reused incremental
outputs and compiler caches. Completed settings and an obsolete isolated runtime
fixture were removed after checking its stale lock and process references.
No full/slow gate or new rendered appearance acceptance is claimed.
