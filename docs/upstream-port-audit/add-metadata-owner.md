# Add metadata preparation — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared the original
CompressDialog.cpp bool-pair merge/finalization, FormatChanged, OnOK,
ShowOptionsString, COptionsDialog initialization/acceptance and their callers
with the corresponding port and Add consumers. The complete original inventory
SHA256 matches the existing baseline. No source is imported or revision advanced.

The Add form now prepares fixed-source metadata, registered handler capabilities
and saved defaults in one private owner, used by the options modal, summary and
final compression request. The original bool-pair algorithms remain unchanged.
The options modal retains its existing time-capability fallback without a codec
catalog, including ZIP precision choices. Its local profile transaction,
Options Cancel, Add Cancel, supported-property filtering and Windows-only access
time handling remain with their existing owners. No layout, text or visibility
changes are made.

The focused priority regression checks explicit command On/Off against contrary
saved values, the saved default when the command is unspecified, agreement of
the options control with the public request and unchanged request/settings after
cancelling a pending edit. Existing suites cover confirmed options, format
switching, timestamps and engine-backed link/metadata storage.

Focused validation: compression_advanced, compression_path, compression_event,
compression_format, source_link_storage and source_metadata_storage built and
passed on Linux, then macOS and Windows. Linux and macOS each reported 97 passes
and 10 expected Windows-only metadata skips; Windows reported 107 passes without
skips. Linux acceptance combines the five unchanged passing suites with the
final advanced-suite rerun after correcting its source-format fixture setup.
No full or slow suite was repeated. Incremental builds and compiler caches were
retained; completed test scratch was removed after capturing results.
