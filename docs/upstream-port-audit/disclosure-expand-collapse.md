# Disclosure assistive activation — 2026-11-01

The disclosure is a project-owned Qt presentation control used by Add, Extract,
progress details and settings. Original 7-Zip resources have fixed groups; the
existing [agreed presentation adaptation](../7zfm-parity.md) retains their
controls and operation handlers behind collapsible sections. Reviewed the
original CompressDialog.rc and ExtractDialog.rc at 7-Zip
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, the local control and its consumers.
No archive algorithm, upstream resource or baseline changes.

Source comparison found a missing platform dispatch route:
[Qt 6.12.0 Windows UIA ExpandCollapse](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/windows/uiautomation/qwindowsuiaexpandcollapseprovider.cpp)
calls the Qt show-menu action when changing expansion state. The local adapter
previously accepted only press. Both now use the existing button activation,
retaining disabled-button handling, state notifications, content/summary
visibility and edited values. The Windows fixture uses native Expand/Collapse
directly, removing its manual state-plus-Invoke workaround; observations no
longer retain stale controls from a previous enumeration.

The advertised action list and keyboard bindings are unchanged. In
[Qt's Cocoa adapter](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/cocoa/qcocoaaccessibility.mm),
an expandable button maps to a disclosure triangle and its advertised press
maps to the native press action. No menu action is newly advertised on macOS.
This retains the existing labeled disclosure behavior described by
[Apple's disclosure guidance](https://developer.apple.com/design/human-interface-guidelines/disclosure-controls).
No native API or deployment-target requirement is added; macOS remains 14.4+.
No external source implementation is imported; Qt is consulted for its platform
API contract, and installed Qt sources/binaries remain unchanged.

The common regression exercises both activation routes, expanded/collapsed
state, retained Unicode drafts, keyboard activation, names and disabled behavior.
Native Windows Add and Extract check Expand/Collapse/Expand and cancellation;
Extract additionally checks hidden/re-exposed controls. macOS offscreen widget
validation is distinct from native VoiceOver acceptance, which is unverified
while the console is locked. This change does not establish pixel parity.

Validation: disclosure_accessibility, compression_advanced, extract_dialog,
progress_dialog and options_dialog built and passed first on Linux, then on
macOS and Windows (86 checks per platform). Windows additionally passed the
native_gui_workflow suite (13 checks), including the real Add/Extract UIA cycles.
All checks passed without skips. Incremental builds and compiler caches were
reused; completed scratch was removed after preserving results. No full or slow
gate was repeated.
