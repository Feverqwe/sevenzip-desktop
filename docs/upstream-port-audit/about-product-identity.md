# Independent About identity

Date: 2026-10-18. User-requested product presentation change.

Source repository: <https://github.com/ip7z/7zip.git>, full commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed complete `CPP/7zip/UI/FileManager/AboutDialog.cpp/.h/.rc`,
`AboutDialogRes.h`, `CPP/7zip/MyVersion.h` and the About branch in
`MyLoadMenu.cpp::ExecuteCommand`, alongside the Qt command dispatcher,
`QtFileManagerCommands::AboutDialog`, language bindings and Help service.
The About source bodies match the original recorded baseline
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

The upstream dialog displays its logo, source version/CPU, date, copyright and
free-software statement, opens the homepage/Help and reports codec-load errors.
The port retains the command, catalog lease, error-message body, modal owner,
Help topic and homepage dispatch. No archive behavior or codec ownership changes.
The existing offline license action remains available.

The authorized adaptation replaces the original logo, title and fixed resource
layout with Sevenzip's own application name, release and independent-port
description. A separate 7-Zip group retains source version/CPU,
explicitly labeled upstream date, original copyright and free-software text,
license and upstream-homepage actions. The footer adds the project repository
link and retains default Return (the redesign caption is Close). Local EN/RU
captions replace upstream captions only where product identity or attribution
context requires different text.
Font-based sizing protects both descriptions across a live language switch.
The presentation refinement places the release beside a larger application
heading and replaces the
group-box border with a palette-based attribution panel. Flat link buttons keep
their actions and native focus handling; confirmation remains the native default
button.
No new brand artwork or external font dependency is introduced.

No new upstream functional code was copied. The original logo and exact resource
geometry are intentionally omitted from this view; its unused Qt resource bundle
is removed from the target. Imported assets and notices elsewhere retain their
licenses. This view does not claim source-layout or pixel
parity. The [earlier version adaptation](application-version.md) remains the
record for unchanged application/package metadata.

Source re-review confirms that codec errors, Help and upstream-homepage dispatch
remain connected through their original owners; the differences above are
presentation changes authorized by the user.

Validation on macOS arm64 / Qt 6.12: incremental builds of the application,
`help_source_tests`, `help_ui_tests` and `language_standard_buttons_tests` passed;
all three focused CTest suites passed. Existing behavior checks cover modal menu
dispatch, codec-error text, offline license/Help, project/upstream link destinations,
default Return and live EN/RU identity/geometry. The new compact layout initially
clipped the translated free-software statement; its minimum vertical size policy
now protects the wrapped notice. Geometry checks permit native minimum expansion
instead of requiring the requested frame to match a size hint exactly.

An additional Cocoa run of the About font, Return and identity/geometry scenarios
passed. Reviewed current JSON reports in `build/dev/ui-reports/` and its
`about-native/` directory: normal/compact EN/RU, 8/12-point fonts, source grouping,
control order and labels/buttons exposed without text clipping. After the visual
refinement, both light/dark palette cases pass, including attribution text contrast
and heading/version separation. Native screenshots of the affected About view
were reviewed in EN/RU at normal/compact sizes. Compact requests expand to
325×352 logical pixels on the checked default font after removing the generic
contributor credit at the user's request; these measurements are
evidence, not fixed dimensions. Attribution and footer do not overlap. The
description keeps the 7-Zip name together rather than breaking it at the hyphen.
Windows/Linux, full/slow gates and rendered comparison with Windows 7zFM were
not run. The requested design intentionally differs from the original About
presentation.

Native-palette correction: a user screenshot exposed a bright attribution panel
in macOS dark appearance because `AlternateBase` was bright. The panel and
version badge now paint a translucent neutral surface over the actual window
background. The regression includes the unmodified system palette and a dark
palette with a bright alternate base, measuring text contrast against the
composited rendered background. The final `help_source` and `help_ui` suites pass;
native Cocoa system/light/dark EN/RU normal/compact reports and About-only
screenshots pass and were reviewed. This supersedes the earlier checks that
only used favorable custom alternate-base colors.

## Live catalog adaptation, 2026-11-06

The About/notice viewer now follows the agreed live component catalog. Re-review
of the original About initialization and command handlers retains native version,
date, copyright, codec-error text and upstream-homepage dispatch. The existing
modal owner and default Return remain. The separate free-software sentence and
the About Help/F1 route are intentionally replaced by the offline installed
license viewer; the original legal documents are retained in the package.
No new upstream code is imported and no archive behavior changes.

Shared identity, disclosure, metadata rows, responsive actions and scrollable
dialog components provide the presentation. Codec errors are an inline notice
with the complete existing report available on request. Build information copies
only displayed build values and applicable module diagnostics; an unavailable
application revision is labeled unknown. Browser handoff refusal offers the exact
address without claiming the website is unavailable.

The independent license viewer reads the actual installed notice index and full
original UTF-8 documents. The packaging owner creates that index after platform
install owners, and refreshes it after additional runtime notices and pruning,
before signing. It is a document inventory, not a report of loaded modules.
Install-only staging contains its local installed notices; external Qt/VC/ICU
notice inputs remain part of the existing release archive pipeline.

Focused Linux checks cover About, viewer/model, packaging and direct shared-control
consumers. Actual Cocoa checks cover modal routing, EN/RU, normal/compact geometry,
palette changes and rendered attribution contrast. A separate Cocoa run read and
copied all 69 documents in the local installed review payload. These checks do not
establish full release-package acceptance or original Windows pixel parity.
Windows interactive checks also passed for both forms; document-path and packaging
fixtures passed with the runner's elevated token for native symbolic links.
