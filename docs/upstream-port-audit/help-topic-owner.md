# Direct help topic dispatch — 2026-11-02

Reference: <https://github.com/ip7z/7zip.git>, recorded revision
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Reviewed complete original
FileManager HelpUtils.cpp/.h and verified their inventory hashes, together with
the Qt help browser, entry points and F1/dialog consumers. Original HtmlHelp and
hh.exe need a module-directory/CHM-member path. The Qt replacement reads bundled
HTML resources and only needs the topic and parent widget.

ShowHelpWindow now sends that topic directly to the existing Qt service. The
synthetic executable-directory prefix, CHM concatenation, string conversions
and matching parser are removed from the Qt boundary. Native caller topics,
parent ownership, case-insensitive CHM member lookup, fragments, internal and
external links, history, warning presentation and browser styling are unchanged.
No external source is imported and no baseline is advanced.

Existing help_source and help_ui regressions exercise the public workflow:
contents and contextual F1, pending settings/Add/Extract input, offline links
and Back/Forward, fragments, language/palette changes and unavailable help.
No new test is added merely to require removal of the intermediate adapters.
This is an internal dispatch simplification; no new layout or text is introduced.

Linux focused validation: help_source and help_ui passed, 35 Qt checks without
skips. This establishes the shared dispatch and checked GUI consumers on Linux;
no new Windows/macOS or rendered-original appearance acceptance is claimed.
Completed test settings were removed; incremental builds/compiler caches retained.

## Help document ownership — 2026-11-04

Rechecked original HelpUtils dispatch, the current topic/window service, browser
document adapter, source link/theme audits and contextual F1 consumers. Original
HelpUtils.cpp/.h hashes still match the recorded revision above. The local Qt
HelpBrowser implementation and table helper now belong to qthelpbrowser.cpp
with a private header; qthelpservices.cpp retains resource initialization,
topic validation, modeless window construction, external-link dispatch and Back.
The public topic/text entry points and browser class behavior are unchanged.

Complete document handlers were moved without rewriting their bodies: link
resolution uses the sourceChanged URL, CHM-relative path normalization retains
query/fragment/case rules, original CSS is cached, palette replacement remains
one-pass and theme reload restores both scroll positions. Table sizing, window
ownership, title/language binding and focus are retained. No HTML/CSS, native
algorithm, external import or provenance revision changes. Existing help_source
and help_ui suites cover real link activation, history, palette changes,
readability, unavailable topics and contextual Help preserving pending input;
no test is added for the internal class/file structure.
Both focused suites passed on Linux. Other platforms and new rendered-original
appearance acceptance were not selected for this nonvisual ownership change.
