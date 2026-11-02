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
