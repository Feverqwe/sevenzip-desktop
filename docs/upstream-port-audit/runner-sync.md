# cp2 runner synchronization

The runner uses [cp2](https://github.com/elemeng/cp2), built from the full revision
and build fingerprint pinned in [`cp2.lock.json`](../../tools/cp2/cp2.lock.json).
cp2 is tooling, outside the archive engine and application distribution.
Cargo.lock pins its build dependencies; the original MIT license and NOTICE
are retained with the source and in `tools/cp2/cp2-NOTICE.txt`.
The build uses the complete upstream compiler sources, Cargo manifests/build
script and setup sources. Development workflows, documentation and other
non-build inventory stay in the pinned archive rather than being installed.

The narrow adaptation to `src/sync/executor.rs::scan_dest` is recorded in
`tools/cp2/cp2-destination-filter.json`: pass the existing exclusion filter to the
destination scanner. Upstream's delete-mode destination scan otherwise enters
excluded build/upstream directories before the sender protects them from deletion.
The receiver wrapper supplies the same exclusions as the sender, including in
push mode where upstream does not forward them. Transfer, deletion, filtering
algorithms and scan control flow remain upstream; no Rust module is independently
rewritten. Exact original context is required before applying the recorded edit.

`tools/cp2/cp2-signature-cache.json` records the second adaptation to
`src/sync/receiver.rs`: `Receiver`/`Receiver::new` carry whether cached basis
signatures may be used, and `handle_signature_request` passes that decision to
`signature_for_path`. Explicit checksum/verification bypasses the metadata-keyed
cache. A cached signature otherwise hid a middle-byte destination change with
preserved size/mtime and produced incorrect output despite checksum comparison.
Normal metadata mode retains caching. Signature generation and reconstruction
remain upstream; checksum mode also requests its existing output verification.

The SMB adaptation is taken from the smbStorage `tools/cp2/timestamp-resolution.patch`
(SHA256 `23f495094c7891e900d49b3837722248fe61b025b9e3a24103dbdd50ffbab360`)
against the same original cp2 revision. The timestamp edit owners in the existing
baseline retain the CLI option, propagation to planner/sender/receiver and
post-apply verification at the selected precision. They also retain the patch's
portable unit checks and setup platform guards. The separate CLI integration
fixture adjustment is omitted because the installed tool excludes that test
inventory. The original complete units and licenses remain pinned. The unrelated
GNU sidecar adaptation is not needed by the project's explicit receiver install.
Both prior project protections are retained; the combined fingerprint differs
from the bundled smbStorage binary. Normal CIFS sync selects 100 ns without a
tree-wide checksum pass. Explicit verification still verifies transferred bytes.

The shared-storage follow-up checks the destination filesystem under the pinned
readiness wrapper. Only CIFS receives timestamp rounding; the Linux FUSE frontend
uses cp2's strict comparison. No service/frontend switch, source hashing pass,
cache reset or cp2 source/baseline change is involved. A real FUSE copy check
changes same-sized bytes with a one-nanosecond mtime change: the old rounded mode
skips the change, strict comparison transfers it, and an unchanged repeat skips
the file. The selected mount and synchronization result are retained as evidence.
The existing hyperv_runner behavior suite passed on Linux, macOS and Windows;
the Linux invocation also synchronized the real project through FUSE without
checksum mode.

Local integration owns installation, the configured SSH connection, protected
source exclusions and build-fresh snapshot timestamps. cp2 owns reconciliation,
file transfer, checksum mode and its signature cache. The small local SSH
launcher is necessary because cp2 fixes the SSH executable/default connection
arguments; it also replaces the Windows architecture probe whose quoting fails
with this guest shell. No cp2 algorithms or functions were reimplemented.

[`cp2.baseline.json`](../../tools/cp2/cp2.baseline.json) records the retrievable
repository, full commit, complete original inventory units, their SHA256 values,
source archive checksum, selected adapted symbol and local integration owners.
Baseline bytes are original
and independent of safe local integration refactoring. Reuse the existing review:

```sh
python3 scripts/upstream_baseline.py reference-verify --manifest tools/cp2/cp2.baseline.json --upstream /path/to/cp2
python3 scripts/upstream_baseline.py reference-review --manifest tools/cp2/cp2.baseline.json --upstream /path/to/cp2 --new-revision FULL_COMMIT --output /tmp/cp2-review.md
```

Before changing the pin, compare original source and dependency changes,
review Windows SSH and snapshot consumers, retain needed local adapters, then
validate installation and source reconciliation on the supported platforms.
The reference review materializes the recorded edit against the baseline and
checks its applicability to the proposed original, without modifying either tree.
The version banner/build fingerprint must match between client and receiver.
Update pins and provenance only after that review; do not replace integration
with upstream example scripts. The earlier custom Go/SFTP prototype was removed.
