# Relative link target classification — 2026-11-01

## Link command lifetime and deferred refresh — 2026-11-05

Compared complete original CApp::Link and menu routing, LinkDialog resources and
current dialog/binding, timer, reload and scan-completion owners. Complete
LinkDialog.cpp and LinkDialog.rc originals match the existing baseline hashes at
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Native link algorithms, selection, accepted/
Cancel results and the visible kpidNtReparse-column refresh condition are unchanged.
No source is imported or baseline revision advanced.

The Qt Link wrapper removes its redundant pair of native timer guards. Its
existing CallScope already prevents the only Qt auto-refresh dispatcher from
visiting either panel throughout the modal call. Retaining an extra disabled
timer snapshot through the source's optional deferred reload could leave that
panel's auto-refresh disabled after publication. The existing call owner now
protects the synchronous phase, and existing scan guards own deferred loading;
no new guard handoff, operation state or rendering pass is needed.

Command regressions check that a file created while the dialog is open does not
appear during the modal call, and that a new file appears automatically afterward.
Create/Cancel, original selection, Unicode paths and hard-link content identity
remain covered. Windows adds bounded large-list Create/Cancel cases and enables
the real Link column through its header menu: POSIX intentionally has no NT raw
reparse column, so that source refresh branch is checked on Windows.

Focused validation passed: Linux link_command (5 Qt checks) and unchanged
link_dialog (23); Windows link_command (7), including the real raw-column cases.
No dialog layout or native appearance changes are involved.

## Link dialog presentation/type owner — 2026-11-04

Reviewed the complete native LinkDialog initialization, browse, creation/error
and application command bodies, header and resource at
`b88c6bc09339de32f7d18bbab900686b244f42ba`, together with the Qt control adapter,
POSIX retarget and filesystem-link services. Complete original cpp/header/resource
hashes match the existing baseline. No external source or revision is imported.

The Qt presentation now keeps each link type's resource ID, object name, caption
and Windows-only restriction in one record. This replaces parallel arrays and a
row-index platform test while preserving all choices, order and restrictions.
Registration, path rows and complete control construction move unchanged to
`qtlinkpresentation.cpp` in the same panel component. The existing window instance
and registry still own the controls; input adaptation and modal/native hooks
remain in `qtlinkcontrols.cpp`. No interface or runtime state is added.

Native link/reparse algorithms, raw relative targets, exclusive copy destinations,
retarget staging, error preservation, Cancel/accept and source panel refresh are
unchanged. Caption/localization contexts, modality, geometry and default focus
are retained. Existing real link-dialog/command and geometry/language regressions
provide acceptance; no record-layout or source-presence test is added.

## Qt path input boundary — 2026-11-02

Compared the complete original LinkDialog.cpp/.h/.rc at recorded commit
`b88c6bc09339de32f7d18bbab900686b244f42ba`; all three original hashes match
the baseline. Reviewed the Qt control adapter, creation/browse callers and native
link services. No import or provenance change.

The Qt text adapter previously silently truncated either path at an embedded
NUL before the native workflow received it. It now rejects that input, and both
creation and Browse report the existing invalid-argument error before using the
path. Native link creation, relative target classification, retarget/clear and
platform-specific reparse handling retain their existing algorithms. No layout
or filename-normalization change.

Two behavior rows attempt a hard link with NUL in either path: an error must
leave the shortened link absent and original bytes intact. A separate valid
dialog then creates the expected link. No pre-fix execution or same-dialog retry
acceptance is claimed; invalid Browse is source-reviewed.

Linux link_dialog and link_command passed: 28 passes, no skips. Focused build
12.695s, tests 1.192s; completed settings fixtures removed, incremental trees and
compiler caches retained. No secondary-platform, full/slow or appearance
acceptance run.

## Relative target classification

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared complete LinkDialog
initialization, browse, creation, error and CApp::Link bodies, header/resources,
Qt controls, native link services and their command consumer. Original complete
LinkDialog.cpp/.h/.rc hashes match the existing baseline. No source import or
provenance advancement.

The existing POSIX adaptation follows a target to classify file versus directory,
but resolved relative targets from the process working directory. Creation stores
the raw target, which the filesystem resolves from the link's parent. An unrelated
same-named object in the working directory could therefore reject a valid choice
or conceal a wrong type.

Classification now prefixes a relative symbolic target with the link's parent;
creation still receives the original target. QFileInfo extracts the parent
without canonicalizing it; parent and target symlink/.. traversal remain for the
filesystem to resolve. The upstream full-path helpers were inspected but normalize
dot components and are unsuitable for this probe. Windows reparse handling and
hard-link target lookup retain their existing source semantics. No new decoding,
file mutation policy, link replacement algorithm or dialog layout is introduced.

The POSIX regression places opposite object types under the same Unicode name in
the link directory and an isolated process working directory. Correct file and
directory choices create a link with the unchanged relative target; incorrect
choices are rejected without creating a link. Existing coverage retains Cancel,
hard-link content identity, dangling/cyclic targets, retarget/clear and refusal
to hide existing file bytes. The fresh stack dialog in CApp::Link remains its
lifetime owner; no speculative dialog-reuse feature was added.

Validation: link_dialog, link_command, filesystem_link, folder_link and
browse_dialog built and passed on Linux first, then macOS and Windows: 69 passes
on each platform. Four new POSIX-only rows are explicitly skipped on Windows;
its native link cases passed. Initial build failures in test string conversion
and Windows moc slot registration were corrected in the fixture, with no product
workaround. The final slot-registration correction leaves the POSIX test body
unchanged. No full/slow gate or cache reset; incremental trees and compiler
caches retained, completed scratch removed after evidence capture.

## Qt event boundary — 2026-11-04

Reviewed the complete LinkDialog initialization/button/creation/browse and app
command path, its header/resources and Qt controls against the recorded b88c
reference. Complete LinkDialog.cpp/.h/.rc hashes match the baseline. Original
OnSize moves native controls; the port already delegates that geometry to Qt
layouts and its OnSize adaptation was empty. Native link/reparse algorithms,
validation, relative classification and source command refresh remain unchanged;
no code is imported or provenance advanced.

The Qt presentation no longer emulates an unused Win32 resize callback, an ignored
dialog-resource parameter or button-handle argument. Its initialization and
button hooks now express the void notifications actually consumed by Qt signals,
instead of returning ignored native handled flags and calling empty base hooks.
The three original button branches keep their order and actions. Cancel still
rejects directly, and link success still explicitly ends the dialog. QDialog's
existing resize handling/layouts remain responsible for the same geometry.

Initialization (apart from its discarded base result), browse/creation/error
handling and CApp::Link bodies were compared again after the change. Existing
link dialog/command regressions cover native creation, errors, retarget/clear,
Cancel, type/platform restrictions, language and compact/normal resizing. No test
pins the private hook signatures or requires the empty resize bridge. No layout,
caption, visibility or native platform policy changes are introduced.

Linux passed `link_dialog` and `link_command`: 28 checks without failures or
skips, including the existing normal/compact geometry checks. Build took 11.294s
and tests 1.198s. Completed selected settings were removed after checking test
processes; existing incremental builds, compiler caches and four jobs were
retained. No platform-native branch changed, so secondary OS checks were not
selected. No full/slow gate, screenshots or new visual-parity claim.
