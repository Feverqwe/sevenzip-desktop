# Add destination validation — 2026-11-02

Reference: <https://github.com/ip7z/7zip.git>, commit
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Compared the original
GetFinalPath_Smart, SetArcPathFields and complete OnButtonSetArchive with
the port's path helpers, Browse, history selection, format/SFX name updates,
confirmation and source-request projection. The complete original
CompressDialog.cpp SHA256 matches the existing baseline. No source import or
provenance advancement.

Browse now uses the same existing archivePath validation as form confirmation.
Its separate CCompressDialog copy supplied name, format and SFX fields even
though path resolution only needs the directory prefixes and name. In the
standalone form that copy also lacked the initial-directory fallback. Clearing
the destination field could therefore make Browse resolve a relative name from
the process directory while confirmation resolved it from the initial folder.
The native path algorithm, history and format/SFX naming remain unchanged.
The existing folder-picker adaptation retains the draft on Cancel; this change
does not restore the original Windows save-file picker or alter presentation.

The regression clears the destination, places the process and initial folders
at different depths, and supplies a relative Unicode path valid only from the
initial folder. Browse must open, Cancel must retain the draft, and confirmation
must accept the expected target without creating it. Existing suites retain
invalid/NUL path, history, format, password, volume and recovery checks.

Linux primary run `20261102T014432Z-3e7bc8de`: compression_path and
compression_error_policy passed, 65 checks, no skips. Build 13.960 s; tests
4.647 s. Portable Qt path preparation changed; native platform branches and
dialog layout did not. macOS/Windows native pickers were not newly validated.
Selected scratch directories were empty after completion; incremental outputs
and compiler caches retained. No full or slow gate.

## Live archive name state — 2026-11-02

Compared original GetFinalPath_Smart, SetArchiveName/SetArchiveName2,
SetArcPathFields, history/format/method handlers and OnButtonSFX with the Qt
state, source-form initialization and standalone callers. The reviewed 9128
snapshot's complete CompressDialog.cpp is identical to the recorded b88 baseline
above, whose original hash matches the manifest. No import or baseline advance.

The existing native name state now receives editable-name changes through one
Qt binding, like its existing destination-folder binding. Format, hash-method
and SFX handlers no longer copy the same control text before native naming.
Path validation reads that state directly instead of constructing another path
snapshot. The initial directory and its fallback are initialized there after
copying a source dialog; duplicate window fields are removed while preserving
their previous values. History
projection, native naming/path algorithms, folder NUL rejection and accepted-path
persistence remain unchanged.

Existing behavior checks cover typed Unicode names, edited/empty destinations,
history and Cancel, format/hash suffixes, SFX transitions and source-modal
creation. No private-binding test or pre-fix failure is claimed. Five Linux
suites passed: compression path, format, source dialog, SFX creation and error
policy. Final incremental build/test times were 27.654/6.030 seconds. Native OS path
branches and picker integrations did not change, so no secondary OS batch was
added. Existing Qt geometry checks passed; no native rendered parity, full/slow
gate or new screenshot/report run is claimed.
