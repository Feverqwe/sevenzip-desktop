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

## Format transition ownership — 2026-11-03

Rechecked original FormatChanged/OnCommand, SetMethod2, SaveOptionsInMem,
SetArchiveName/SetArchiveName2 and SFX bodies with current initialization,
fixed-source, registry, history, method/encryption/memory and acceptance callers.
Complete original CompressDialog.cpp/.h/.rc and RegistryUtils.cpp hashes match
the existing b88c baseline. No native option or naming algorithm is imported or
changed.

One Qt format-change handler now saves the outgoing profile, applies the selected
format and dependent controls, then updates the archive name. The later second
handler and repeated format configuration in the SFX adapter are removed.
Initialization still configures the format before naming and constructing its
dependent controls; ordinary method changes retain that format owner. Memory
combo population restores its prior signal-blocking state through scope exit.

Existing format/profile/path, method/event, encryption and SFX workflows cover
the complete transition, including Cancel, reload of saved options, native output
and no-codec fallback. No handler-count/source-text assertion or pre-fix product
failure is added. Source priorities, metadata defaults, layout and control metrics
are unchanged.

## Resource estimate and acceptance — 2026-11-04

Reviewed the complete Qt thread/dictionary/solid/memory binding, format/method
transitions, draft/source option snapshots and confirmation with original
SetNumThreads2, memory-calculation wrappers, SetMemoryUsage and OnOK. The complete
CompressDialog.cpp/.h/.rc originals match the existing b88c baseline above.
Native memory/thread algorithms, limits and format policies are unchanged; no
external code is imported or provenance advanced.

Display and confirmation now share one calculation from the current controls,
as the original form does. Confirmation no longer builds the entire compression
request, including unrelated metadata/profile/SFX/volume values, merely to read
its level, thread count and memory limit. Auto-thread resolution, effective
dictionary, format, method, level and solid-block inputs retain the same values.
The explicit/default memory limit still goes to the original OnOK refusal and
diagnostic path before saving settings or accepting the form.

Thread-combo population now restores the prior signal-blocking state within the
same mutation range. An enclosing format/method transition therefore retains its
own block instead of having it cleared by the nested population. No new callback
or request state is introduced. Native sourceInfo's level semantics remain
distinct from the standalone compression snapshot.

Existing resource estimates, preference/event transitions, memory refusal and
successful native compression regressions cover the public behavior. No test
pins the private calculation method or merely inspects signalsBlocked. Layout,
captions, control values and geometry are unchanged.

Linux passed `compression_preferences`, `compression_error_policy` and
`compression_event`: 78 checks, no failures or skips (build 23.085s, tests
5.037s). The separately selected `compression_resources` then passed 15 checks,
also without skips (build 6.173s, tests 0.235s). The unchanged passing suites
were not repeated. Selected fixture directories were empty before and after
the runs. Existing incremental outputs, configured four jobs and compiler caches
were retained. No platform branch changed; no secondary OS, full/slow gate or
appearance check was selected.
