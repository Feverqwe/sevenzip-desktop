# Main-menu language binding — 2026-10-17

## Single menu population — 2026-11-04

Compared the original FileManager resource menu, MyLoadMenu translation and
activation handlers, and application creation order with the Qt action/menu
construction, toolbar state, first-panel hookup and command/language consumers.
Complete original MyLoadMenu.cpp/.h, App.cpp and resource.rc/.h hashes match
the baseline at `b88c6bc09339de32f7d18bbab900686b244f42ba`. No external
source, resource, algorithm or revision is imported.

Qt now creates the reusable actions and submenus without first inserting them
into provisional main menus. The first panel populates those menus directly
in the existing final order, removing the intermediate population and reset.
Action creation order, QObject parents, submenu contents, toolbar action
identity, language contexts, command binding and final grouping remain intact.
Toolbar/settings initialization still precedes panel creation; complete menus
still precede command attachment, shortcuts, language reload and window display.
Native dynamic File-menu publication, View activation and bookmark updates
retain their existing owners. No control geometry, wording, availability rule
or native menu adapter changes. Existing main_shell, view_menu and language_ui
behavior checks cover these consumers; no private initialization-order test
is added.

Source reference: <https://github.com/ip7z/7zip.git>, commit
`0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`.

Reviewed FileManager `resource.rc::IDM_MENU`, `resource.h::IDM_VIEW_TWO_PANELS`,
`MyLoadMenu.cpp::MyChangeMenu/OnMenuActivating/ExecuteFileCommand`, and the
English/Russian language entries, together with the Qt menu construction,
command bindings and live language reload. The two-panel QAction lacked its
source command ID, so the existing source menu translator skipped it and its
initial Russian caption survived English startup and reload.

The Qt action now carries the original ID. Existing source translation,
English fallback and View-menu state projection consume it; the existing panel
toggle connection remains. No upstream code or language assets are copied or
changed, no functional source body is replaced, and no branches are omitted.
Dependencies remain the existing QAction menu projection and language services.
The source baseline and platform adaptations are unchanged.

Validation: macOS arm64 / Qt 6.12.0, working tree based on
`19006aac08755e0ee98de26eefaa45e21038a6f3`.
The language UI regression covers English/Russian startup, live EN/RU changes,
missing-language fallback, preserved toggle state/shortcut, and absence of
Russian captions in English main menus, excluding user bookmark paths. The
timestamp caption is checked after opening View, when source activation fills
its current-date example.

Incremental dev builds of `Sevenzip`, the nine language test targets and
`view_menu_tests` succeeded. Focused CTest: `language`, `language_ui`,
`language_dialog`, `language_standard_buttons`, `language_main_dialog`,
`language_archive_command`, `language_focus`, `language_source_update`,
`language_file_transfer`, `view_menu`: all ten pass. The initial menu scan also
examined the hidden timestamp's construction caption; the regression now opens
View before inspecting that dynamically populated menu, matching its source
contract. Current View JSON for both panes, EN/RU and 980×620/640×400 shows the
translated panel row fully visible (225-pixel English / 295-pixel Russian menu).
No Windows/Linux run, rendered Windows comparison, full gate or slow suite is
claimed. These checks cover the selected language consumers, not every possible
message or third-party/native dialog.
