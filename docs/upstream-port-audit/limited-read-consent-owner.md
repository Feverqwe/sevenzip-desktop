# Limited-read consent ownership — 2026-11-02

## One consent context for initial opening

The native and typed fallback branches of QtArchiveFolderManager now share one
selection of consent dialog and callback. Native COpenArchiveCallback keeps
precedence over IQtArchiveReadConsent, with the same null-context behavior.
Reviewed the complete initial opening body, direct batch adapter and all current
ReadConsentDialog/ReadConsentCallback implementations: the batch adapter returns
the fixed pointers supplied to that opening. Resolving them once preserves their
meaning across both paths.

Reader preparation, warning collection, cancellation, consent calls and their
order remain unchanged. No source-reader, recovery policy, reopen algorithm,
layout or diagnostic-text change; this is local Qt integration with no external
code import or provenance advancement. Existing opening/recovery/source-extract
checks cover the behavior; no representation test or pre-fix failure is claimed.

Linux agent_opening, archive_recovery_ui and source_extract_backend passed:
51 checks, no skips; incremental build 51.06s, tests 9.663s. Selected completed
scratch is empty; build trees and compiler caches retained. No secondary OS,
full/slow, broader reader matrix or new appearance acceptance run.

## Limitation collector boundary

Reviewed Qt initial/reopen and batch-opening consumers, read-policy collection,
callback dialog transport and their recovery regressions. Compared the original
7-Zip CArchiveLink::ReOpen callback lifetime at recorded revision
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Limited-read policy and its Qt presentation
are local integration; native readers and callback algorithms are unchanged.
No external code is imported or provenance advanced.

The filesystem limitation collector had a second callback/prompt branch, but
its sole consumer always passed a null callback. The actual opening flow already
collects filesystem and archive warnings together, applies opening-scoped consent
and checks cancellation around its GUI dialog. The collector is now private and
only reads the existing native properties; the unused alternate prompt route and
its dependency on COpenArchiveCallback are removed. Device consent keeps its
existing entry point, and initial/reopen/batch consumers keep the common archive
preparation path. Read-only access, supported recovery scope, warning text,
native metadata choice, passwords and bounds/output-path checks are preserved.

Existing behavior checks cover refusal and acceptance in English/Russian,
readable and corrupt archive entries, unsafe recovered paths, the visible mode,
non-success partial results, repeat opening and unchanged source bytes. The
agent-opening suite also exercises process fallback and metadata reopening.
No new test is added for the removal of an unreachable branch or private name.

Linux focused validation passed: agent_opening, archive_recovery_ui and
filesystem_ntfs_recovery_ui — 57 Qt checks without skips. This establishes the
checked common consent/opening behavior and NTFS integration on Linux; no new
Windows/macOS, full-format-matrix or rendered-original UI acceptance is claimed.
Selected scratch is empty; incremental builds/compiler caches are retained.

## Initial and repeated opening consent — 2026-11-03

Reviewed initial, batch and repeated opening, native metadata selection, retained
limitations, device confirmation and the callback dialog bridge. Rechecked the
complete original `CArc::ReOpen` and `CArchiveLink::ReOpen` bodies and declarations
in `CPP/7zip/UI/Common/OpenArchive.cpp` and `.h` at the recorded
`b88c6bc09339de32f7d18bbab900686b244f42ba`; original inventory hashes match.
Native algorithms and reader properties remain unchanged. This is local Qt
integration, with no external import or provenance advancement.

Filesystem and archive warnings now share the existing consent gate, worker-to-GUI
handoff and cancellation checks around the dialog. This removes repeated gates
and the second consecutive pre-dialog cancellation query on the archive path.
Their distinct messages and explicit read-only acceptance remain unchanged.
Preparation no longer returns a separate native-metadata flag: on success it was
exactly equivalent to nonempty collected limitations, including archive warnings
without detailed text. Reopening derives that condition from the limitations;
the retained metadata-source choice remains separate. Initial opening already
ignored the removed flag. The collector also drops a read-only condition already
established before its property loop.

Accepted limitations remain scoped to the resource and compared on reopening;
new openings still require acceptance. Reader capabilities, recovery scope,
read-only indication, source bytes, password and path/bounds protections are
preserved. Existing opening, archive recovery and NTFS recovery UI checks cover
the affected behavior; no test pins private fields or callback query counts.

Linux `agent_opening`, `archive_recovery_ui` and
`filesystem_ntfs_recovery_ui` passed: 57 Qt checks, no failures or skips.
Incremental compilation took 16.498s and tests 28.680s. Selected scratch is empty;
build trees and compiler caches are retained. No secondary platform, full/slow
suite or new visual acceptance is claimed.
