# Working-directory settings draft — 2026-11-01

## Complete Qt working-path input — 2026-11-05

Compared complete original FoldersPage.cpp/.h, FoldersPage2.rc, WorkDir.cpp,
OptionsDialog.cpp and the NWorkDir registry bodies with the current page,
Apply/Save/Cancel owner and temporary-file consumer. Original inventory hashes
match `b88c6bc09339de32f7d18bbab900686b244f42ba`; no source import or revision
change is introduced.

Folder browsing and Apply now share one page-owned path-reading boundary.
It rejects embedded NUL before native conversion rather than browsing or saving
only a shortened prefix. The page retains the complete draft and modified state;
the options container honors its failed Apply and keeps Save open for correction
or cancellation. It reveals the path when enabled, or the existing specified-mode
radio when the path is disabled, without changing the selected mode.

The same check covers a malformed inactive draft because the original Apply
saves that path in every mode. Ordinary inactive paths, empty/relative paths,
native mode and removable-drive policy, settings representation, page application
order and temporary creation/publication algorithms remain unchanged. No new
controller, persistent state, global conversion rule or filesystem policy is
added. Existing earlier-page partial application remains possible.

Public regressions exercise Browse refusal, Save refusal from another section,
retained draft/mode/settings and reachable correction at normal/compact sizes,
then cancellation or correction and real temporary creation in the accepted
Unicode directory. Existing tests retain Apply-then-Cancel and full options
integration coverage. No source-shape or private-helper test is added.

Linux passed `options_dialog`, `options_integration`, `options_window` and
`work_dir`: 64 checks without failures or skips. Compilation took 6.489 seconds,
test execution 4.178 seconds. An initial build caught an unavailable conversion
helper in the new fixture; the fixture now uses the suite's existing conversion
style. Production code needed no correction after that build. The original
mode/init/modified-state bodies and all options code outside the failed folder
Apply handling were compared unchanged. Whitespace and recorded inventory checks
passed; completed scratch was empty. Configured four-job incremental builds and
compiler cache were retained. No secondary-platform, full/slow or new rendered
appearance acceptance is claimed.

## Native temporary creation ownership — 2026-11-02

Reviewed settings-to-work-directory creation, native temporary file/directory
owners, spool and ordinary/SFX output registration. Compared original
CreateTempFile2/CTempFile/CTempDir creation, WorkDir and Update output bodies at
recorded commit `b88c6bc09339de32f7d18bbab900686b244f42ba` from the same
repository below. Full original Windows/FileDir.cpp, Common/WorkDir.cpp/.h and
Common/Update.cpp hashes match the existing baseline. No import or provenance
advancement; current POSIX directory creation uses the project's existing common
native retry loop, rather than a fresh port of upstream's POSIX branch.

The creation loop now prepares the cleanup owner's full path before creating the
OS object. Previously three owners allocated their path after successful
creation, before enabling cleanup. A project overload supplies the prepared
path to those owners and to direct spool/SFX consumers; the original four-argument
API remains available. The retry/name/collision algorithm remains one native
implementation. Failed prior removal retains its path/lease; ordinary creation
failure still clears the owner path without changing lastError. Registration
failure retains Close/Remove/error ordering.

Spool and SFX output establish Close/Delete guards before path conversion or
registration can allocate. Ordinary archive output also guards the interval
between Create_NEW and registration in TempFiles. Guards are dismissed only
after the existing owner accepts the file. MultiOutStream already allocates its
record and postfix before creation and links it without allocation; that path
is unchanged. Working-directory policy, publication/rollback, source deletion,
stream copying, native permissions and existing failed-publication retention
remain unchanged.

A Linux-only standalone regression injects one allocation failure immediately
after a real mkdir, then verifies that unwinding removes the temporary directory.
It interposes the OS boundary without a production hook or allocation-count
assumption. File/spool/SFX exception gaps are source-reviewed; they are not
separate runtime allocation-injection claims. Existing focused suites cover
normal/abandoned working files, invalid work paths, spool/publication failures,
cancellation, ownership relocation and SFX lifecycle. No pre-fix run, new
diagnostic build or representation-only test is claimed.

Linux acceptance: work_dir, owned_temp_publication, options_dialog,
native_update_publication and sfx_lifecycle passed 62 Qt checks without skips,
plus temp_directory_allocation passed. The combined build took 55.074s, tests
1.123s. After adding the ordinary-output registration guard, only the three
affected publication/SFX suites were repeated: 39 checks, build 3.504s, tests
0.647s. Earlier four-suite evidence established the primitive directory failure
case before the direct consumers were included. Completed scratch was empty
except two absent-object registry records, which were removed after checking
their targets. Incremental trees and compiler caches retained. No local
concurrency override, secondary OS, full/slow or appearance acceptance run.

## Settings draft

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete FoldersPage
bodies/header/resources, NWorkDir::CInfo fields/defaults/load/save, the options
Apply/Cancel caller and WorkDir temporary-file consumer. Complete original
FoldersPage.cpp/.h/.rc and FoldersPage2.rc hashes match the existing baseline.
No import or provenance advancement.

The page kept its loaded CInfo for its whole lifetime, while all current choices
already lived in Qt controls. Apply overwrote every field before saving, and
no consumer read the stored copy. Loading now uses a local record; Apply creates
and populates a local record directly from the controls. The original GetWorkDir
body moved to its sole Apply caller. The page header no longer depends on the
settings record. No replacement state or shared mutable owner was introduced.

Source mode selection, path conversion, modified/init guards, accepted-only
folder browsing and save timing are preserved. The hidden POSIX removable-drive
choice retains its loaded value. Registry/settings format and temporary-file
placement/creation algorithms are unchanged. No layout, labels or defaults change.

Existing regressions cover Browse cancellation, disabled/enabled path controls,
invalid stored configuration, Cancel, Apply followed by another edit and Cancel,
and creation/removal of a real temporary file in the selected Unicode directory.
No new test is needed for the removal of private state; source-shape assertions
would add no useful behavior coverage.

Validation: options_dialog, options_integration, options_window and
source_browse_dialog built and passed on Linux first, then macOS and Windows.
Linux/macOS reported 61 passes each, Windows 63, without failures or skips.
Incremental builds/compiler caches retained; completed selected scratch removed
after evidence capture. No full/slow gate or heavy diagnostics were needed.
