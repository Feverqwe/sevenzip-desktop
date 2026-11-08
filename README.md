# Sevenzip

**Open an archive like a folder. Pull a file out of a disk image. Give another
app access to a folder inside a ZIP.**

Sevenzip is an independent file and archive manager for Windows, macOS and
Linux, based on 7-Zip. Keep the source and destination side by side, whether
you’re unpacking a download, creating a backup archive or looking for a file
on an old drive.

[Download Sevenzip](https://github.com/Feverqwe/7zip-qt-port/releases) ·
[Report a bug](https://github.com/Feverqwe/7zip-qt-port/issues)

## What you can do

- **Browse and extract.** Open archives like folders, including archives nested
  inside other archives. Extract selected files or the entire archive, and test
  archive integrity.
- **Create and update archives.** Create 7z and ZIP archives, choose compression
  settings, and use password protection, split volumes or self-extracting
  archives where the format supports them. Add, rename or remove entries in
  writable archives.
- **Manage files in two panels.** Copy and move files between locations, jump to
  favorites or recent locations, and open files in an external viewer or editor.
  Calculate and verify file checksums.

## Get files off a disk your system can’t open

Have an ext4 image from a Linux machine and need a file from it on Windows?
Open the image in Sevenzip, find the file and copy it to a local folder.
Sevenzip reads the filesystem itself, so Windows can do this without an
ext4 driver.

If you have the drive itself, use **File → Open device…** to browse a supported
partition without imaging the whole drive first. Unmount its volumes and grant
system access if prompted. Direct device access is experimental. See
[direct device access](docs/direct-device-reading.md).

### Copy readable files from damaged volumes

An interrupted write or a truncated image can leave some files out of reach
while others are still readable. Sevenzip’s adapted NTFS, FAT, exFAT and ext
readers can offer limited read-only access in some of these cases, including
ext volumes that need journal recovery.

Sevenzip explains the detected limitation before you choose whether to continue
and keeps a warning visible while you browse. You can copy files the reader can
still access. Some files may be missing, outdated or unreadable, and a copy may
be incomplete. Sevenzip leaves the source untouched and reads ext volumes in
this mode without replaying the journal.

## Use files from an archive in another app

Need to view photos from a ZIP or open a document from a disk image? Browse to
the folder in Sevenzip and choose **Tools → Mount read-only…**. The folder
appears as a read-only network drive or volume. Open it from your file manager
or directly from the app you want to use, without extracting the whole archive
first. Sevenzip reads the source format and your system connects to it over SMB.
See
[read-only mounts and system requirements](docs/readonly-mounts.md).

## What Sevenzip can open

| Type | Examples |
| --- | --- |
| Archives and packages | 7z, ZIP, RAR, TAR, WIM |
| Compressed files | gzip, bzip2, xz, Zstandard, LZ4, Lzip, Brotli |
| Disc and disk images | ISO, UDF, DMG, Apple sparseimage and sparsebundle, QCOW/QCOW2 |
| Filesystems inside images or on supported devices | NTFS, FAT, exFAT, ext2/3/4, APFS, HFS+, Btrfs, XFS, F2FS, EROFS |

Opening a format and editing it are separate capabilities. For example, you can
browse and extract RAR archives, while 7z and ZIP also support creation and
updates. Filesystem images are opened read-only. For format-specific limits and
platform differences, see [supported workflows and limitations](docs/7zfm-parity.md).

## Install

Download the package for your platform and extract the entire archive, keeping
its folders and libraries together.

| Platform | Open |
| --- | --- |
| Windows x64 | `bin/7zFM.exe` |
| macOS arm64 (14.4 or later) | `Sevenzip.app` |
| Linux x64 | `bin/7zFM` |

Release checksums are published in `SHA256SUMS.txt`. Windows packages are unsigned.
macOS packages use ad-hoc signing and are distributed without Apple notarization.

## Development

See the [development guide](docs/development.md) for setup, builds and tests,
and [contributing](CONTRIBUTING.md) for how to participate. AI coding agents play
a central role in development, guided by human direction, review and testing.

## Licenses

[MIT](LICENSE) applies to original project code unless another license is
specified. 7-Zip, Qt and other dependencies retain their own terms. See
[third-party notices](THIRD_PARTY_NOTICES.md).
