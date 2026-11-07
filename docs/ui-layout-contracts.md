# UI specifications

Before changing layout, text, visibility or resize behavior, read the affected
specification and exact original resources/handlers/callers. Inspect current-build
normal/compact widget JSON; include language, focus and conditional states relevant
to the change. Commands and evidence boundaries are in
[geometry](testing/geometry.md), [resource tooling](ui-reference.md) and
[native runtime comparison](ui-runtime-comparison.md).

Specifications describe required observable behavior. Historical package IDs and
dated checks in the detailed sections identify their origin; they do not reopen
the completed port or establish current acceptance. New validation receipts belong
in [the audit](upstream-port-audit.md). Update the affected contract when its
observable requirements change; keep implementation history in the
[archive](archive/README.md).

## Компоненты

- [Options and preferences](ui-layout-contracts/options.md)
- [Navigation and window state](ui-layout-contracts/navigation.md)
- [Panels and file lists](ui-layout-contracts/file-list.md)
- [Copy and Move](ui-layout-contracts/copy-move.md)
- [Archive creation and compression](ui-layout-contracts/compression.md)
- [Archive opening and extraction](ui-layout-contracts/extraction.md)
- [Menus and command presentation](ui-layout-contracts/shell.md)
- [External tools and temporary resources](ui-layout-contracts/external-tools.md)
- [Shared dialogs and operation feedback](ui-layout-contracts/dialogs.md)

## Отдельные виды и состояния

| View or state | Contract |
| --- | --- |
| Rendered Add/Extract reference | [Спецификация](ui-layout-contracts/extraction.md#rendered-addextract-reference-p3-06-2026-10-15) |
| Native Copy terminal errors | [Спецификация](ui-layout-contracts/copy-move.md#native-copy-terminal-errors-p3-05) |
| Native System handler images | [Спецификация](ui-layout-contracts/options.md#native-system-handler-images-p3-06-2026-10-17) |
| Refresh focus and alternative marks | [Спецификация](ui-layout-contracts/file-list.md#refresh-focus-and-alternative-marks-p3-05) |
| Panel address keys | [Спецификация](ui-layout-contracts/navigation.md#panel-address-keys-p3-05) |
| Archive directory CRC projection | [Спецификация](ui-layout-contracts/file-list.md#archive-directory-crc-projection-p3-05) |
| Changed nested archive Parent | [Спецификация](ui-layout-contracts/extraction.md#changed-nested-archive-parent-p3-0515) |
| Native external update publication | [Спецификация](ui-layout-contracts/external-tools.md#native-external-update-publication-p3-0515) |
| Native nested opening | [Спецификация](ui-layout-contracts/extraction.md#native-nested-opening-p3-0515) |
| Source panel creation | [Спецификация](ui-layout-contracts/file-list.md#source-panel-creation-p3-0515) |
| Source startup window placement | [Спецификация](ui-layout-contracts/navigation.md#source-startup-window-placement-p3-0516) |
| Source Copy/Move destination | [Спецификация](ui-layout-contracts/copy-move.md#source-copymove-destination-p3-0515-required-scope) |
| Persisted source locations | [Спецификация](ui-layout-contracts/navigation.md#persisted-source-locations-p3-0514-required-scope) |
| Source root command target | [Спецификация](ui-layout-contracts/navigation.md#source-root-command-target-p3-0513-implementation-validation-active) |
| Source address dropdown | [Спецификация](ui-layout-contracts/navigation.md#source-address-dropdown-p3-0512-required-remaining-scope) |
| Source caption helpers | [Спецификация](ui-layout-contracts/navigation.md#source-caption-helpers-p3-0510-validation-active) |
| Namespace Hash result consumers | [Спецификация](ui-layout-contracts/shell.md#namespace-hash-result-consumers-p3-058-validation-active) |
| Standard source dialog buttons | [Спецификация](ui-layout-contracts/dialogs.md#standard-source-dialog-buttons-p3-059-accepted-button-scope) |
| Source shell popup | [Спецификация](ui-layout-contracts/shell.md#source-shell-popup-p2-09-in-progress) |
| Shell Menu page | [Спецификация](ui-layout-contracts/options.md#shell-menu-page-p2-09-accepted) |
| System associations | [Спецификация](ui-layout-contracts/options.md#system-associations-p2-08-accepted) |
| Add metadata Options | [Спецификация](ui-layout-contracts/compression.md#add-metadata-options-p2-07-in-progress) |
| Link | [Спецификация](ui-layout-contracts/dialogs.md#link-p2-05) |
| Benchmark | [Спецификация](ui-layout-contracts/dialogs.md#benchmark-p2-04) |
| SFX Options group | [Спецификация](ui-layout-contracts/compression.md#sfx-options-group-p2-02) |
| File-list drag target | [Спецификация](ui-layout-contracts/file-list.md#file-list-drag-target-p1-16-wip) |
| Commands and reports | [Спецификация](ui-layout-contracts/dialogs.md#commands-and-reports) |
| Alternate streams namespace | [Спецификация](ui-layout-contracts/file-list.md#alternate-streams-namespace-p2-06-active) |
| Add archive naming metadata | [Спецификация](ui-layout-contracts/compression.md#add-archive-naming-metadata-p2-01) |
| Open archive picker | [Спецификация](ui-layout-contracts/extraction.md#open-archive-picker-p2-01) |
| Delete Temporary Files | [Спецификация](ui-layout-contracts/external-tools.md#delete-temporary-files-p1-152) |
| About | [Спецификация](ui-layout-contracts/external-tools.md#about) |
| Live dialog language consumers | [Спецификация](ui-layout-contracts/options.md#live-dialog-language-consumers-p1-142) |
| Menu page | [Спецификация](ui-layout-contracts/options.md#menu-page-p1-131) |
| Advanced compression options | [Спецификация](ui-layout-contracts/compression.md#advanced-compression-options-p1-071) |
| Compression lifecycle and memory rejection | [Спецификация](ui-layout-contracts/compression.md#compression-lifecycle-and-memory-rejection-p1-072) |
| Archive volume creation | [Спецификация](ui-layout-contracts/compression.md#archive-volume-creation-p1-061) |
| Existing archive Add options | [Спецификация](ui-layout-contracts/compression.md#existing-archive-add-options-p1-051) |
| Main menu and toolbar | [Спецификация](ui-layout-contracts/shell.md#main-menu-and-toolbar-p0-091) |
| Per-panel context and status | [Спецификация](ui-layout-contracts/shell.md#per-panel-context-and-status-p0-092) |
| Two disk panels | [Спецификация](ui-layout-contracts/file-list.md#two-disk-panels-p0-081) |
| Mixed and restored panels | [Спецификация](ui-layout-contracts/file-list.md#mixed-and-restored-panels-p0-082) |
| Virtual root main window | [Спецификация](ui-layout-contracts/navigation.md#virtual-root-main-window-p0-042) |
| Mounted volumes | [Спецификация](ui-layout-contracts/navigation.md#mounted-volumes-p1-111) |
| Windows device namespace and F5 Copy | [Спецификация](ui-layout-contracts/navigation.md#windows-device-namespace-and-f5-copy-p3-057) |
| Network namespace | [Спецификация](ui-layout-contracts/navigation.md#network-namespace-p1-113) |
| Source extraction prompts | [Спецификация](ui-layout-contracts/extraction.md#source-extraction-prompts-p0-041-prerequisite) |
| Archive metadata cells | [Спецификация](ui-layout-contracts/file-list.md#archive-metadata-cells) |
| Source sorting in the file list | [Спецификация](ui-layout-contracts/file-list.md#source-sorting-in-the-file-list-p0-061) |
| Source property columns | [Спецификация](ui-layout-contracts/file-list.md#source-property-columns-p0-062) |
| Time precision and UTC | [Спецификация](ui-layout-contracts/file-list.md#time-precision-and-utc-p0-063) |
| File list settings | [Спецификация](ui-layout-contracts/options.md#file-list-settings) |
| External editor settings | [Спецификация](ui-layout-contracts/external-tools.md#external-editor-settings) |
| VerCtrl commands and Revert | [Спецификация](ui-layout-contracts/external-tools.md#verctrl-commands-and-revert) |
| Folder history list | [Спецификация](ui-layout-contracts/dialogs.md#folder-history-list) |
| Hash results | [Спецификация](ui-layout-contracts/dialogs.md#hash-results) |
| Full text detail | [Спецификация](ui-layout-contracts/dialogs.md#full-text-detail) |
| Selected properties | [Спецификация](ui-layout-contracts/dialogs.md#selected-properties) |
| GUI early error messages | [Спецификация](ui-layout-contracts/dialogs.md#gui-early-error-messages) |
| Shared name/mask dialog | [Спецификация](ui-layout-contracts/dialogs.md#shared-namemask-dialog) |
| Copy/Move dialog | [Спецификация](ui-layout-contracts/copy-move.md#copymove-dialog) |
| Extract dialog | [Спецификация](ui-layout-contracts/extraction.md#extract-dialog) |
| Create archive dialog | [Спецификация](ui-layout-contracts/compression.md#create-archive-dialog) |
| Source long-path browser | [Спецификация](ui-layout-contracts/navigation.md#source-long-path-browser) |
| Archive password prompt | [Спецификация](ui-layout-contracts/extraction.md#archive-password-prompt) |
| Progress dialog and modal lifecycle | [Спецификация](ui-layout-contracts/dialogs.md#progress-dialog-and-modal-lifecycle) |
