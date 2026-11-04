# Shell submenu ownership — 2026-10-18

Source: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`.
Reviewed complete `CPP/7zip/UI/FileManager/PanelMenu.cpp::CreateSystemMenu`,
its CreateShellContextMenu/InvokePluginCommand callers and COM/PIDL owners;
`CPP/7zip/UI/Explorer/ContextMenu.cpp::QueryContextMenu`, MyAddSubMenu and
MyInsertMenu; and `CPP/Windows/Menu.h/.cpp` insertion/destroyer contracts.
Qt consumers include the portable menu tree, native tree renderer,
QtFileManagerShellMenu and native owner-draw/message presentation.

The copied system-menu body detached its popup and disabled CMenuDestroyer
before label construction and parent insertion. A refused insertion therefore
left the entire popup tree without an owner. The native Windows regression
actually reproduced six additional USER handles after three rejected insertions
(22 handles instead of the original 16). It warms the real shell providers,
passes an invalid parent to the public panel operation, checks process resource
balance and verifies that subsequent valid construction still works.

The original destroyer now remains armed until insertion succeeds. The same
source review found early release of the main portable popup and missing guards
for the Open/CRC submenu preparation. Those owners now transfer after successful
MyAddSubMenu. Command generation, IDs, branching, providers and invocation
remain in their original modules; no menu algorithm or command is replaced or
omitted. These are local ownership corrections to the copied source.

The Qt tree adapter previously adopted a raw child before the allocating vector
insertion. Its contract now matches native insertion: rejection or an exception
leaves the child with its caller; an accepted entry adopts it. The Windows tree
renderer likewise retains its original CMenuDestroyer through native insertion,
covering both returned failure and unwinding. Existing recursive destruction
releases the full unpublished tree, as specified by
[DestroyMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroymenu).
The regression uses
[GetGuiResources](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguiresources)
to observe the process's USER-handle balance, without pinning a shell menu's
structure or its initial resource count.

The returned native-failure case has runtime evidence. Exception paths are
reviewed from source; no allocation injection or standalone Qt reproducer is
part of this change. Menu layout, captions, command availability and visual
appearance are unchanged.

Focused acceptance after the complete ownership change: context_menu_settings,
shell_context, shell_menu_window and source_context_menu passed on macOS arm64
(4/4, 1.91 s) and Linux x64 (4/4, 3.16 s). Windows x64 additionally passed
native_system_menu, native_menu_presentation and native_shell_com
(7/7, 26.00 s). Guest evidence: `20261018T125513Z-50a6e4b1`, Qt 6.12.
The application also built on macOS. A subsequent application build on both
guests captured an unfinished parallel Add-dialog signature change and failed
there; run `20261018T125629Z-c58dd142` does not establish application acceptance.
The focused snapshot above predates that unrelated change. Persistent build
trees and compiler caches were reused. No full gate, slow suite or visual
acceptance was run.

## Windows owner-draw resources — 2026-11-02

Rechecked the complete Qt native menu presentation, retained-provider state,
menu construction, popup/command dispatch and bitmap fixture. Compared native
PanelMenu CreateSystemMenu and the Menu destroyer contract at recorded 7-Zip
revision `b88c6bc09339de32f7d18bbab900686b244f42ba`. Owner-draw rendering into a
Qt image is project presentation glue; the native provider, message ABI, command
generation and invocation remain unchanged. No external code is imported and
no baseline revision is advanced.

The drawing function now binds screen/memory DCs, bitmap and font cleanup to
their scopes. This removes repeated manual failure cleanup and also releases
resources during C++ unwinding. Previous bitmap/font selections are restored
before deleting their objects; the bitmap is still deselected before conversion
to QImage. Normal output, callback messages, menu generations, provider/menu
lifetimes and layout are unchanged. Exception cleanup is established by source
review here; no allocation-failure injection or reproduced leak is claimed.

Existing native_menu_presentation checks verify alpha/orientation, callback
bitmaps, measured/drawn states, native keyboard dispatch, refused measurement,
submenu behavior and provider/menu release. System/menu-window consumers cover
construction, command selection and stale actions. No new test is added solely
to require a particular cleanup implementation.

Windows interactive validation passed native_menu_presentation,
native_system_menu and shell_menu_window: 44 Qt checks without skips. Only the
Windows implementation changed, so Linux/macOS suites were not repeated.
This establishes the checked native rendering and dispatch behavior; no injected
allocation failure or new rendered-original parity acceptance is claimed.
Completed settings were removed; incremental builds/compiler caches retained.

## Complete panel shell provider owner — 2026-11-04

Compared the original PanelMenu.cpp construction and invocation bodies, Panel.h
interfaces, and App.h command range at recorded revision
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`, including callers in properties,
the associated-file launcher and both Qt menu projections. The full native
provider chain now resides in `panels/PanelShellMenu_win.cpp`, beside its owning
CPanel interface, in the same sevenzip_panel target. Shared menu policy remains
in PanelMenu.cpp; Qt rendering and retained-provider lifetimes keep their
existing owners.

The moved bodies preserve PIDL cleanup, super-path fallback, selection handling,
submenu insertion ownership and Unicode numeric invocation. The existing
ShellFolder_ParseDisplayName linkage also remains available to the Windows
associated-file launcher. A focused shared command-range header supplies the
same plugin/system boundary to provider construction and both dispatchers;
App.h continues to expose the existing plugin start through that header.
No algorithms, native API contracts or resources are replaced or omitted.
The existing baseline maps these extractions without advancing original hashes.

Source review confirms that the complete moved native block and all remaining
portable method bodies are unchanged. Focused runtime validation passed on
Linux (shell_context, shell_menu_window, source_context_menu: 44 Qt checks) and
on interactive Windows (native_system_menu, native_menu_presentation,
shell_menu_window, native_association_handle: 47 checks), with no failures or
skips. The native association check exercises the real launcher and shared
PIDL helper using a disposable helper executable. Existing checks cover Unicode
selection, numeric invocation, failure/resource ownership and stale commands;
no implementation-presence tests were added. No macOS or new visual-parity
acceptance is claimed. Completed test settings were removed; incremental
builds and compiler caches were retained.
