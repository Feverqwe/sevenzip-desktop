# Local runner configuration

Local application builds do not require remote runners. To use the remote
verification commands, first create a private machine configuration:

```sh
cp scripts/config/runners.example.json scripts/config/runners.local.json
```

Edit the copy for your machines. You may configure Linux, Windows or both.
The example is the reference for connection, workspace and toolchain fields.
Use absolute remote paths with forward slashes. The workspace must be a dedicated
project directory; synchronization reuses a persistent worktree for the current
Git branch and preserves build outputs and compiler caches. The same branch
uses the same remote directory regardless of the local source directory.
Different branches can run concurrently. Existing nonempty directories without the
project ownership marker are refused.

The local file is ignored by Git and excluded from runner source transfers.
Linked worktrees use this file from the main checkout, so feature worktrees
under `.worktrees/` need no separate machine configuration. Run the scripts in
the feature worktree to send that branch's current source. Runner logs and
compiler/tool caches stay on the main checkout's ordinary disk. Source archives
travel with the project; no upstream Git bundle or separate checkout is needed.
Keep passwords and private keys outside it; use SSH's normal authentication and
host-key verification. An alternative file can be selected with
`SEVENZIP_RUNNER_CONFIG`. Keep alternative private files outside the checkout
or in an ignored `scripts/config/*.local.json` file.

## Connections and tools

The `ssh` array contains the SSH executable, options and destination. Prefer
aliases in your SSH config so the account, address, port and key stay in your
normal SSH setup. Verify the host keys and connectivity before running checks.
VM discovery is configured separately from build storage.

Set the Qt prefix, Python executable and additional tool directories for each
machine. Windows also needs its MSVC setup batch file and computer name.
Native registration/reference commands check that name before running.
CMake, Ninja, Git, ccache, clang-format and Bash must be available on PATH.
Install the dependencies described in [development](development.md#dependencies).

Select a dedicated `build/sources` workspace on the runner's ordinary disk.
Its parent `build` retains installed tools, dependencies, outputs and caches.
Native build intermediates and temporary data use the ordinary disk.
Project wrappers do not create or manage RAM disks.

The ordinary `dev` preset uses the configured Qt SDK. Optional `vcpkg-*` presets
use the checkout under the workspace's tools directory and obtain Qt from the
manifest. On Windows, configured tools such as Perl remain available to CMake
inside vcpkg's clean build environment. Selecting the native preset does not
install manifest dependencies.
Linux manifest builds disable Qt precompiled headers to bound dependency-build
disk usage. This uses a generated overlay from the pinned registry, retains
the manifest's features and builds both configurations. Local manifest builds
can select the same behavior with `SEVENZIP_VCPKG_QT_PCH=OFF`.
The helper forwards the selected `computer_name` as `SEVENZIP_WINDOWS_RUNNER_HOST`;
native registration/reference fixtures require a matching actual computer name.
Changing this environment selection refreshes the build wrapper's configuration.

For a Linux workspace on a separate mounted filesystem, set both `mount` and
`filesystem_uuid` in its workspace entry. The helper checks the actual UUID
before acquiring the worktree or synchronizing source; a missing or different
mount fails. It never creates, formats or mounts a volume.

Then inspect the selected toolchains:

```sh
python3 scripts/hyperv-runner.py probe linux
python3 scripts/hyperv-runner.py probe windows
python3 scripts/verify.py navigation --platform all
```

Use only configured platforms. Windows UI checks need an active desktop session;
see [runner workflows](hyperv-runners.md) for interactive execution, locking,
recovery and artifact retention.

### Source synchronization

`check` and `run` use cp2 over the configured SSH connection. The pinned tool
is installed automatically when absent, both locally and on the selected runner.
Installation builds the pinned source with a private Rust toolchain if needed;
it does not change system PATH. The runner's existing compiler is required.
The version, source checksum and toolchain are pinned in `tools/cp2/cp2.lock.json`.

To install ahead of a check, or reuse a matching binary already on that machine:

```sh
python3 scripts/install-runner-cp2.py local
python3 scripts/install-runner-cp2.py all
python3 scripts/install-runner-cp2.py windows --binary C:/tools/cp2.exe
```

cp2 compares actual receiver files by size and modification time. Use
`--sync-checksum` for content comparison and verification of transferred bytes.
Its signature cache can be retained;
deleted receiver files are still restored. A local timestamp cache keeps repeated
synchronization inexpensive while preserving incremental-build freshness,
including edits made during the preceding build and backdated checkouts.
Deleting that cache causes retransfers, not missing source. Original checkout
timestamps are unchanged. Windows synchronization omits Unix permissions.

On Unix clients, cp2 reuses an SSH connection during synchronization. Its
control sockets use a private local temporary directory and the connection is
closed after the transfer, including on failure.

## Optional VM discovery

For managed VMs, replace a runner's `ssh` setting with `ready_command`, an
argument array for your local discovery program. The program must return one
JSON object with `vm` and `ssh_argv`; `vm_id` and additional diagnostic fields
are optional. VM names do not determine the guest OS: the configuration entry
selects Linux or Windows.

The maintainer's local file invokes the separately installed runners skill with
explicit host, VM, identity-file and known-hosts arguments. Other installations
can use their own discovery program or plain SSH. No host addresses or local
profile names are supplied as source-code defaults.
