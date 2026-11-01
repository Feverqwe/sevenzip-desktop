# Archive metadata view ownership — 2026-11-01

Reviewed the complete property facade, native index matching, opening/reopening,
typed backend requests, item streams, update/extract services and source Agent
capability/property consumers. The original Agent contracts were compared with
<https://github.com/ip7z/7zip.git> at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Complete Agent.cpp and AgentOut.cpp
units match the existing archive/manifest hashes
`ae8ba20818071b76987a4d715994e058927413804411b55dba1caec5934c4786` and
`7b2b83432f6f46b534a09a2d7bf02ad13e44fb3c908ef9a9eec192f01c7c3b50`.
The source archive's container hash also matches its existing baseline. No
external code is imported or original revision/hash advanced.

Previously each property facade changed a shared resource's index-order flag.
Constructing unrelated metadata could disable a native facade with no fallback
properties; constructing another matching facade could make the unrelated one
return native properties at its unproven indexes. Index authorization now belongs
to each facade as a weak binding to the reader generation it matched. It cannot
enable another view or silently authorize a replacement reader's indexes, and
closing an unrelated or superseded view cannot close the current native reader.

The existing exact count/path/duplicate-name matching remains one pure resource
service. Direct native metadata retains duplicate-name support without an
independent index mapping. Item getters, raw properties, item streams,
writer/property interfaces and extraction use the facade's own binding. Streams
retain the already established native reader lease. Agent update readiness also
checks that view's writer interface before source operations can start.

The resource reports physical reader/service capabilities without a mutable
metadata-view flag. Extraction receives the view's native-index authorization;
folder progress selects the corresponding native or backend worker directly.
The direct resource overload retains its previous automatic progress selection.
Window extraction/test, batch validation and read-only mount commands use the
same per-view capability boundary. A read-only snapshot retains its own reader;
its duplicate native-metadata availability flag is removed.
Unproven RAR/hash indexes still refuse the source consumer. File-only typed
metadata services, including an absent codec catalog, retain their existing
fallback and update-service contracts. Explicit writer properties, source I/O,
callbacks, path validation, publication and cancellation algorithms are unchanged.

The Unicode normalization regression now creates native, unrelated and matching
public archive views, checking that each retains its own properties and writer
capability as other views are constructed. It also replaces the reader and checks
that an old view cannot return the replacement's unrelated entry or close that
reader. The RAR regression constructs an actual unrelated source Agent:
its extraction is refused while the original
folder remains usable. These replace assertions about mutation of the shared
resource with observable per-view behavior. No pre-fix runtime reproduction is
claimed; the failure mechanism was established from the complete source path.

Acceptance used a 16-suite core batch and a 12-suite follow-up for the remaining
extraction/window/mount consumers on Linux, macOS and interactive Windows.
Together they cover 24 distinct suites per platform: 461 Qt passes on Linux,
461 on macOS and 464 on Windows, without failures. The three filesystem ADS
rows skip on each POSIX system; Windows skips the existing colon-filename case.
The core batches took 91.00/43.65/104.25 seconds and the follow-ups
76.59/46.97/104.03 seconds respectively. Repeated suites are counted once in
these totals. Full logs and matching remote completion/verification receipts
were retained; completed disposable fixtures were removed. Builds reused the
existing incremental trees and compiler caches. No full gate, opt-in slow
nested matrices or appearance/visual-parity acceptance is claimed.

## Archive-wide properties and reader replacement — 2026-11-01

The original Agent's complete archive-property methods use its own archive link.
The Qt adaptation also needs the native outer chain behind its file-only operation
facade. That lookup previously followed the resource's current reader, so an old
view could show a replacement archive's path and properties despite its isolated
item indexes. The existing codec-update fixture reproduced that cross-view path
change through the public `IFolderArcProps` interface before the fix.

Archive-wide metadata now belongs to the view's opening. Native properties use
that generation and its codec owner under a scoped reader lease; an expired
generation returns an error. Typed metadata without a native reader retains its
own property snapshot. This preserves the complete original nested-level,
error/warning, offset, tail and property-enumeration branches, without keeping
input files open for the entire lifetime of a metadata view. Reopening rebinds
the requesting view. Native raw-property interfaces are queried through the
authorized item binding rather than cached across reader replacement.

The existing source units and hashes above were verified again; no new external
source is imported and provenance is unchanged. The property-source snapshot and
scoped context are local integration in `src/archive/resources/`, shared by the
resource and its views. Existing regression fixtures cover retained and expired
readers, old/new view independence, and typed metadata tail/offset/truncation
results across separate views.

Runtime validation passed eight focused suites on Linux, macOS and Windows:
codec update, archive opening, source properties, the archive property catalog,
native archive properties, nested opening/update and window property consumers.
The pre-fix Linux run failed the new public archive-path assertion while its
other 74 checks passed. The fixed retained/expired-reader and typed-snapshot
cases pass with the existing incremental builds and compiler caches. No full
gate, slow matrix or visual-parity acceptance is claimed for this change.
