# Archive password storage

Source comparison: 2026-11-04. The existing 7-Zip baseline is
`b88c6bc09339de32f7d18bbab900686b244f42ba` from
<https://github.com/ip7z/7zip.git>. Original complete inventory units and SHA256
hashes remain pinned in [the baseline](../upstream-baseline.json) and the local
source archive; no upstream revision or original hashes changed.

Compared `CPP/Common/MyString.h` (`UString`, `UString_Wipe`),
`CPP/Common/MyCom.h` (`CMyComBSTR`, `CMyComBSTR_Wipe`), the FileManager
`PasswordDialog`, `OpenCallback`, `FileFolderPluginOpen`, `Panel.h`,
`PanelItemOpen`, `PanelCopy` password paths, and GUI `CompressDialog` and
`UpdateCallbackGUI2`. The original code stores reusable plaintext in ordinary
strings, while the common module already supplies wiping string/BSTR adapters.
Prompt decisions, cancellation, password retry, nested-opening ownership,
external-editor updates and engine callbacks retain their existing flow.

The project-owned `src/archive/password/` module adds immutable shared password
storage and a mutable upstream-string adapter. It reuses the existing
`UString_Wipe` facility rather than adding another native string implementation.
The adapter wipes before replacement and destruction, uses volatile writes for
live content and retains upstream's capacity wipe. Qt-to-native conversion writes
directly into that buffer. Existing temporary password BSTR owners use
`CMyComBSTR_Wipe`. These unchanged common types retain their LGPL notices and
baseline; the new storage/OS integration is local code, recorded as such.

Panel folder links, their Qt presentation, read workers, read-only views,
native-opening projections, queued password delivery and external-editor sessions
retain shared immutable secrets. Separate mappings avoid locking allocator pages
that belong to unrelated objects. POSIX uses `mmap`/`mlock`; Windows uses
`VirtualAlloc`/`VirtualLock`. Linux requests `MADV_DONTDUMP`. Lock/dump-advice failure
does not prevent archive access. The last reference overwrites the full mapping
through volatile stores before unlocking and releasing it. Replacing or clearing
one owner does not revoke a password still needed by another active owner.

## Boundaries

This is memory hygiene, not protection from an attacker who can read the running
process. OS locking is best effort; Windows/macOS dump exclusion is not claimed.
Plaintext still exists in Qt input controls, temporary Qt API values and native
engine buffers; native codecs also retain derived keys. Those buffers are not
covered by the mapped cache's locking/dump policy. Add/Extract dialog result values
and command-line option storage are outside the archive-opening cache change.
The work does not change the existing password-prompt UI or add timeout/revocation
policy, persistent credential storage, format decoding or password recovery.

## Verification

The focused callback regression checks supplementary Unicode characters,
length-aware native/BSTR transfer, password replacement after reset and
cancellation before another prompt. The encrypted read-only-view regression
checks that a retained view can still extract after the opening's password and
reader references have been released. Existing encrypted backend, opening,
navigation, update and dialog suites cover direct consumers. These behavior tests
do not establish resistance to memory inspection or universal OS locking success.

Runtime evidence on 2026-11-04: the twelve selected Linux suites passed, including
encrypted archives, nested updates and external-editor updates. On macOS the
password callback, password dialog and read-only-view suites passed; a separate
callback run with child-process `RLIMIT_MEMLOCK=0` also passed. On Windows the
password callback and dialog suites passed, covering its allocation API and
native character width. Windows archive-backend/read-only-view integration was
not accepted: preparation of the unchanged native runtime fails on APFS
`LoadDirectory` helper constructors with MSVC C4623 under `/WX`. No APFS source
or warning policy was changed to obtain password-storage acceptance.

Reproduce the original-source comparison and proposed upstream revision review
with [the existing baseline tooling](../upstream-update.md); no separate source
inventory or new external code import is introduced.
