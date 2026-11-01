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
