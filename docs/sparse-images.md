# Apple sparse images

Open a `.sparseimage` file or a `.sparsebundle` package to browse and copy files.
Encrypted images use the normal password prompt, including retry and Cancel.
Access is read-only and does not require mounting or making a decrypted copy
of the image. Keep the sparsebundle package together with its metadata and bands.

The package can reside on a mounted SMB share. Bands are read on demand with
bounded buffering; opening does not download the complete image first. Keep the
network connection available and avoid modifying the package while it is open.
Network and permission errors are reported as read failures.

APFS browsing loads the top level first and reads each folder when you open it.
Previously visited folders remain available without listing them again. Folder
totals can remain unknown until their contents have been read. Select a folder
and use **Calculate folder size** to read its subtree and display its size.
The calculation can be cancelled. Copying a folder
reads its descendants; Flat View and explicit full-tree export enumerate the
requested tree. These operations can take longer on large network backups.
Damage in an unopened folder may therefore be reported only when it is accessed.

APFS read-only mounts load directory contents on demand, including the mount
root. Connecting does not enumerate the complete selected subtree or probe all
of its files. Finder and other applications can still request a recursive
listing themselves. A first visit to a very large folder can take time; that
folder is currently read as a whole. Previously read lists are cached.

Other readers retain their existing complete-list preparation, with stage,
elapsed time and counts where the work is known. Cache settings remain available
for files that may require decoding when accessed. Leaving a folder in Finder
does not guarantee that it cancels an outstanding server request. Disconnecting
the mount cancels its reader work; already running network I/O may need to return
before cancellation completes. Source images remain read-only.

Ordinary copying does not export a file's hidden alternate streams or macOS
extended attributes as additional files. Streams explicitly selected through
the alternate-stream view can still be copied separately.

Copying preserves supported relative symbolic links whose destinations stay
inside the output folder. Read-only SMB mounts also expose relative links inside
the selected tree as links, subject to the native client's link support. The
macOS SMB client rejects targets with a parent-directory component (`..`). Such
links are omitted from macOS SMB mounts and their paths are reported in the mount
diagnostics, so other files can still be copied through Finder. Use direct
extraction from Sevenzip when preserving these links is required. Links that cannot be exposed safely are reported as
skipped, rather than presented as ordinary empty files. The mount remains
read-only without marking every exported file as individually locked.

When damage is detected during opening, Sevenzip explains the limitations and
lets you choose whether to continue read-only. Other readable files can remain
available even when a band cannot be read. Files that touch damaged data report
an error; partial output is not reported as a successful extraction.

Missing bands normally represent sparse holes. A deleted band cannot always be
distinguished from an unused one, and successful password verification does not
authenticate the file data. If the encryption metadata or filesystem structures
needed to find files are unreadable, opening may be impossible.

Filesystem and Time Machine snapshot support is limited to the existing readers.
Internal APFS encryption and FileVault/CoreStorage are separate formats and are
not unlocked by an image password.

The independent `7z` consumer can open the package's `Info.plist` with the
`SparseBundle` format; it uses the same reader and the neighboring bands.
See the [source audit](upstream-port-audit/sparse-images.md) for provenance,
tested variants and platform evidence.
