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
