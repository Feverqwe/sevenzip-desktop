# Shared recursive directory removal — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, full commit
`b88c6bc09339de32f7d18bbab900686b244f42ba`. Compared complete original
`CPP/Windows/FileDir.cpp::RemoveDirWithSubItems`, its native delete/attribute
services and callers with the current Windows and independent Qt implementations.
Reviewed FileFind's Windows/POSIX enumeration, no-follow information conversion,
root lookup and the filesystem provider's cancellation/error consumers.
The original FileDir.cpp/.h and FileFind.cpp/.h match their recorded inventory
hashes in `upstream-baseline.json`. No source is imported or baseline advanced.

Both public overloads now use one traversal in the existing adapted owner,
`src/fileops/native/FileDir.cpp`. It retains the source native path, enumeration,
recursive directory/file dispatch, aggregate child-error handling, Windows
reparse exclusion, read-only-directory handling and final directory removal.
The former Qt recursion and its separately maintained directory listing are
removed. This addresses that part of the independent filesystem replacement;
it does not claim a complete review or transfer of every FileDir service.

POSIX uses the existing CEnumerator dir-entry and no-follow FileInfo boundary.
Its directory links are unlinked without enumerating the target. Direct root
links retain the previous directory-only contract. Terminal separators are
removed while preserving the native root prefix, so they cannot make a POSIX
lookup follow a link into an outside directory. Cancellation remains a Qt
callback at the native boundary; a distinct recursive result stops the entire
walk immediately instead of treating cancellation as a recoverable child error.

Enumeration errors are distinguished from end-of-list through the existing
native API. A failed file removal now records a child failure rather than
clearing the source's aggregate failure flag. Native error codes remain available
to the public provider; confirmation, progress dialogs, trash and selection
restoration keep their existing owners. Archive publication and rollback were
also inspected but are unchanged by this iteration.

Behavior checks exercise both public overloads on real synthetic directory
trees: nested/hidden/Unicode entries, outside directory links, cycles, dangling
links and a root directory link with and without a trailing separator. They
check actual removal and exact surviving outside bytes. Existing suites cover
recursive cancellation, permissions, native provider errors, partial deletion,
temporary ownership, cleanup confirmation and link commands.

Five focused suites passed on Linux, macOS and Windows: `native_fs_operations`,
`owned_temp`, `temp_cleanup`, `source_delete` and `link_command`. Linux passed
86 rows, macOS 85, Windows 83 with one existing POSIX-permissions skip. All four
new directory-tree rows ran on each platform, including both linked-tree rows.
The final Linux batch included the trailing-separator case before secondary
platform acceptance began.

The new root-prefix call initially used the POSIX byte-string type where the
portable helper accepts Unicode; that compilation failure was corrected before
secondary runs. macOS exposed a previous cleanup fixture's assumption about
Unicode composition. Its comparison now accepts canonical equivalents, followed
by passing focused Linux/macOS cleanup checks. The first Windows cleanup run
used SSH's noninteractive session and timed out in window-exposure checks.
Its interactive retry passed focus checks but lacked symlink privilege; the
existing link fixtures passed with the runner's elevated desktop mode. Only
that suite was repeated, retaining the other four suites' passing evidence.

Selected scratch directories were inspected before each validation batch and
after completion; completed settings files were removed. Incremental build trees
and compiler caches were reused with normal four-job defaults. No full/slow gate,
new system configuration, real user files, archive-format change or visual parity
acceptance is involved.
