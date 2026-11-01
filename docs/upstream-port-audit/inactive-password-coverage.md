# Inactive password draft coverage — 2026-11-01

The current [Add contract](../7zfm-parity.md#основные-сценарии) excludes a retained
password draft when protection is unchecked or unsupported by the chosen format.
This intentional Qt workflow adaptation was already implemented and covered by
compression_encryption, including preservation of the draft and an accepted
request without a password.

The older compression_error_policy matrix still required rejection for that
same unsupported-format draft. Removed this contradictory negative row and its
fixture branch; active password mismatch, ASCII/length restrictions, error
suppression, memory/path/volume errors, settings preservation and writer recovery
remain covered. No application behavior or source password algorithm changes.

Compared the original CCompressDialog::OnOK and encryption-control enablement at
7-Zip commit `9128b80e3a471108678e2b3ec3c985861c8d7b0f`: classic OnOK reads retained
text even from disabled controls. The existing Qt adaptation excludes inactive
inputs before calling the unchanged source validator. The positive draft cases
and error-policy matrix passed together on Linux, macOS and Windows: 46
error-policy checks and 13 encryption checks per platform, without failures or
skips. Linux also passed its 504-suite default gate. The wider panel-state
acceptance is recorded [separately](panel-presentation-owner.md); slow suites
were not requested.
